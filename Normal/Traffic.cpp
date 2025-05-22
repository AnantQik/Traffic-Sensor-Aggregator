// -------------------- traffic_sensor_improved.cpp (Revised) --------------------
#include <iostream>
#include <thread> // For std::jthread, std::stop_token, std::stop_source
#include <vector>
#include <mutex>
#include <random>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <string>
#include <iomanip>   // For std::put_time, std::fixed, std::setprecision
#include <sstream>   // For std::ostringstream
#include <algorithm> // For std::min, std::max
// #include <memory> // Removed as its utilities like std::make_unique are not used

// Configuration Constants
constexpr int SENSOR_COUNT = 5;
constexpr int AGGREGATION_INTERVAL_MS = 10000;
constexpr int SENSOR_UPDATE_INTERVAL_MS = 1500;
constexpr int CONGESTION_THRESHOLD = 80;

// Struct to hold one traffic reading from a sensor
struct TrafficData
{
    int sensor_id;
    int density;
    std::chrono::system_clock::time_point timestamp;
};

// Class to manage shared data between sensors and logger
class SharedDataStore
{
public:
    // Called by Sensors to add data
    void addData(TrafficData data)
    {
        std::unique_lock lock(mutex_);
        buffer_.push_back(std::move(data));
        lock.unlock();      // Unlock before notifying
        cv_.notify_one(); // Notify the logger
    }

    // Called by Logger to get a batch of data
    // Waits for data, a timeout, or a stop request
    std::vector<TrafficData> getDataBatch(std::chrono::milliseconds timeout, const std::stop_token &s_token)
    {
        std::vector<TrafficData> batch;
        std::unique_lock lock(mutex_);

        cv_.wait_for(lock, timeout, [&]
                     { return s_token.stop_requested() || !buffer_.empty(); });

        // If stop was requested and buffer is empty, return empty batch
        if (s_token.stop_requested() && buffer_.empty())
        {
            return batch;
        }

        // If buffer has data (either timeout occurred with data, or woken by notify, or stop requested with data)
        if (!buffer_.empty())
        {
            std::swap(batch, buffer_); // Efficiently move all current data to the batch
        }
        return batch;
    }

    // Called by Logger for final drain
    std::vector<TrafficData> getAllRemainingData()
    {
        std::vector<TrafficData> batch;
        std::lock_guard lock(mutex_); // Simple lock for final grab
        if (!buffer_.empty())
        {
            std::swap(batch, buffer_);
        }
        return batch;
    }

private:
    std::vector<TrafficData> buffer_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

// Sensor class, instances run in separate threads
class Sensor
{
public:
    Sensor(int id, SharedDataStore &store, int update_interval_ms)
        : id_(id),
          store_(store),
          update_interval_ms_(update_interval_ms),
          generator_(std::random_device{}() ^ (static_cast<unsigned int>(id) << 1)), // Seed robustly
          distribution_(10, 100) {}

    // Callable operator for std::jthread
    void operator()(std::stop_token s_token) const
    {
        std::cout << "Sensor " << id_ << " started.\n";
        while (!s_token.stop_requested())
        {
            // Responsive sleep: sleep in smaller chunks to check stop_token frequently
            auto next_iteration_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(update_interval_ms_);
            while (std::chrono::steady_clock::now() < next_iteration_time)
            {
                if (s_token.stop_requested())
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Poll interval
            }

            if (s_token.stop_requested()) // Check again after the sleep period
                break;

            TrafficData data{id_, distribution_(generator_), std::chrono::system_clock::now()};
            store_.addData(std::move(data));
        }
        std::cout << "Sensor " << id_ << " stopping.\n";
    }

private:
    int id_;
    SharedDataStore &store_; // Reference to the shared data store
    int update_interval_ms_;
    mutable std::mt19937 generator_; // Mutable because operator() is const
    mutable std::uniform_int_distribution<int> distribution_;
};

// Logger class, instance runs in a separate thread
class Logger
{
public:
    Logger(SharedDataStore &store, const std::string &filename, int agg_interval_ms, int congestion_threshold)
        : store_(store),
          log_filename_(filename),
          aggregation_interval_ms_(agg_interval_ms),
          congestion_threshold_(congestion_threshold) {}

    // Callable operator for std::jthread
    void operator()(std::stop_token s_token) const
    {
        std::cout << "Logger started.\n";
        std::ofstream log_file(log_filename_, std::ios::app); // Open log file in append mode
        if (!log_file.is_open())
        {
            std::cerr << "Error: Logger could not open log file: " << log_filename_ << std::endl;
            return;
        }

        while (!s_token.stop_requested())
        {
            std::vector<TrafficData> batch = store_.getDataBatch(std::chrono::milliseconds(aggregation_interval_ms_), s_token);

            if (!batch.empty())
            {
                processAndLog(batch, log_file);
            }
        }

        // Perform a final drain of the queue after the main loop exits
        std::cout << "Logger: Performing final drain of data...\n";
        std::vector<TrafficData> final_batch = store_.getAllRemainingData();
        if (!final_batch.empty())
        {
            processAndLog(final_batch, log_file);
        }

        std::cout << "Logger stopping.\n";
    }

private:
    void processAndLog(const std::vector<TrafficData> &data_batch, std::ofstream &file_stream) const
    {
        if (data_batch.empty())
        {
            return;
        }

        int total_density = 0;
        int min_density = data_batch.front().density;
        int max_density = data_batch.front().density;

        for (const auto &data : data_batch)
        {
            total_density += data.density;
            min_density = std::min(min_density, data.density);
            max_density = std::max(max_density, data.density);
        }
        double avg_density = static_cast<double>(total_density) / data_batch.size();

        auto now_log_time = std::chrono::system_clock::now();
        auto time_t_log = std::chrono::system_clock::to_time_t(now_log_time);
        std::tm buf_log{};

#if defined(_WIN32) || defined(_WIN64)
        localtime_s(&buf_log, &time_t_log); // Windows-specific
#else
        localtime_r(&time_t_log, &buf_log); // POSIX
#endif

        std::ostringstream log_entry;
        log_entry << std::put_time(&buf_log, "%Y-%m-%d %H:%M:%S")
                  << " - Count: " << data_batch.size()
                  << ", Avg Density: " << std::fixed << std::setprecision(2) << avg_density
                  << ", Min Density: " << min_density
                  << ", Max Density: " << max_density;

        file_stream << log_entry.str() << std::endl;

        if (max_density > congestion_threshold_)
        {
            std::cout << "[ALERT] Congestion: Max=" << max_density
                      << " (Avg=" << std::fixed << std::setprecision(2) << avg_density << ")\n";
        }
    }

    SharedDataStore &store_;
    std::string log_filename_;
    int aggregation_interval_ms_;
    int congestion_threshold_;
};

// Main function: sets up sensor and logger threads, then waits before shutting down
int main()
{
    std::cout << "Running Improved Traffic Sensor Version...\n";

    SharedDataStore data_store;              // Create the shared data store
    std::stop_source master_stop_source; // Create a stop source to signal all threads

    std::vector<std::jthread> sensor_threads;
    sensor_threads.reserve(SENSOR_COUNT);
    for (int i = 0; i < SENSOR_COUNT; ++i)
    {
        sensor_threads.emplace_back(
            Sensor(i, data_store, SENSOR_UPDATE_INTERVAL_MS),
            master_stop_source.get_token());
    }

    std::jthread logger_thread(
        Logger(data_store, "traffic_log_improved.txt", AGGREGATION_INTERVAL_MS, CONGESTION_THRESHOLD),
        master_stop_source.get_token());

    std::cout << "System running. Will shut down in 30 seconds...\n";
    std::this_thread::sleep_for(std::chrono::seconds(30));

    std::cout << "Requesting shutdown...\n";
    master_stop_source.request_stop(); // Signal all threads to stop

    std::cout << "Main thread waiting for jthreads to complete (automatic join)...\n";

    std::cout << "Finished Improved Traffic Sensor Version.\n";
    return 0;
}