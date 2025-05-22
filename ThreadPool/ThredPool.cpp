// -------------------- traffic_sensor_threadpool_specific.cpp --------------------
#include <iostream>
#include <thread>        // For std::jthread, std::this_thread::sleep_for
#include <vector>
#include <mutex>
#include <random>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <string>
#include <iomanip>       // For std::put_time, std::fixed, std::setprecision
#include <sstream>       // For std::ostringstream
#include <algorithm>     // For std::min, std::max
#include <queue>         // For std::queue (in ThreadPool)
#include <functional>    // For std::function (in ThreadPool)
#include <stop_token>    // For std::stop_source, std::stop_token

// Configuration Constants
constexpr int SENSOR_COUNT = 5; // This will be the size of our sensor thread pool
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
    void addData(TrafficData data)
    {
        std::unique_lock lock(mutex_);
        buffer_.push_back(std::move(data));
        lock.unlock();
        cv_.notify_one();
    }

    std::vector<TrafficData> getDataBatch(std::chrono::milliseconds timeout, const std::stop_token &s_token)
    {
        std::vector<TrafficData> batch;
        std::unique_lock lock(mutex_);
        cv_.wait_for(lock, timeout, [&]
                     { return s_token.stop_requested() || !buffer_.empty(); });
        if (s_token.stop_requested() && buffer_.empty())
        {
            return batch;
        }
        if (!buffer_.empty())
        {
            std::swap(batch, buffer_);
        }
        return batch;
    }

    std::vector<TrafficData> getAllRemainingData()
    {
        std::vector<TrafficData> batch;
        std::lock_guard lock(mutex_);
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

// Sensor class (identical to your provided version)
class Sensor
{
public:
    Sensor(int id, SharedDataStore &store, int update_interval_ms)
        : id_(id),
          store_(store),
          update_interval_ms_(update_interval_ms),
          generator_(std::random_device{}() ^ (static_cast<unsigned int>(id) << 1)),
          distribution_(10, 100) {}

    void operator()(std::stop_token s_token) const
    {
        std::cout << "Sensor " << id_ << " started on thread " << std::this_thread::get_id() << ".\n";
        while (!s_token.stop_requested())
        {
            auto next_iteration_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(update_interval_ms_);
            while (std::chrono::steady_clock::now() < next_iteration_time)
            {
                if (s_token.stop_requested())
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            if (s_token.stop_requested())
                break;

            TrafficData data{id_, distribution_(generator_), std::chrono::system_clock::now()};
            store_.addData(std::move(data));
        }
        std::cout << "Sensor " << id_ << " stopping on thread " << std::this_thread::get_id() << ".\n";
    }

private:
    int id_;
    SharedDataStore &store_;
    int update_interval_ms_;
    mutable std::mt19937 generator_;
    mutable std::uniform_int_distribution<int> distribution_;
};

// Logger class (identical to your provided version)
class Logger
{
public:
    Logger(SharedDataStore &store, const std::string &filename, int agg_interval_ms, int congestion_threshold)
        : store_(store),
          log_filename_(filename),
          aggregation_interval_ms_(agg_interval_ms),
          congestion_threshold_(congestion_threshold) {}

    void operator()(std::stop_token s_token) const
    {
        std::cout << "Logger started on thread " << std::this_thread::get_id() << ".\n";
        std::ofstream log_file(log_filename_, std::ios::app);
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

        std::cout << "Logger: Performing final drain of data...\n";
        std::vector<TrafficData> final_batch = store_.getAllRemainingData();
        if (!final_batch.empty())
        {
            processAndLog(final_batch, log_file);
        }
        std::cout << "Logger stopping on thread " << std::this_thread::get_id() << ".\n";
    }

private:
    void processAndLog(const std::vector<TrafficData> &data_batch, std::ofstream &file_stream) const
    {
        if (data_batch.empty()) return;

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
        localtime_s(&buf_log, &time_t_log);
#else
        localtime_r(&time_t_log, &buf_log);
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

// -------------------- ThreadPool Class --------------------
class ThreadPool
{
public:
    ThreadPool(size_t num_threads)
    {
        if (num_threads == 0) num_threads = 1; 
        std::cout << "ThreadPool: Creating " << num_threads << " worker threads.\n";
        for (size_t i = 0; i < num_threads; ++i)
        {
            workers_.emplace_back(&ThreadPool::worker_loop, this, pool_internal_stop_source_.get_token());
        }
    }

    ~ThreadPool()
    {
        std::cout << "ThreadPool: Destructor called. Requesting stop for worker threads...\n";
        pool_internal_stop_source_.request_stop(); 
        condition_.notify_all();                   
        std::cout << "ThreadPool: All worker threads joined.\n";
    }

    template <class F>
    void enqueue(F &&task_function)
    {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            tasks_.emplace(std::forward<F>(task_function)); 
        }
        condition_.notify_one(); 
    }

private:
    void worker_loop(std::stop_token worker_stop_token) 
    {
        std::cout << "ThreadPool: Worker thread " << std::this_thread::get_id() << " started.\n";
        while (true)
        {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                condition_.wait(lock, [&]
                                { return worker_stop_token.stop_requested() || !tasks_.empty(); });

                if (worker_stop_token.stop_requested() && tasks_.empty())
                {
                    std::cout << "ThreadPool: Worker thread " << std::this_thread::get_id() << " stopping (stop requested, queue empty).\n";
                    return;
                }
                
                if (!tasks_.empty())
                {
                    task = std::move(tasks_.front());
                    tasks_.pop();
                } else { 
                    continue;
                }
            }

            if (task)
            {
                task(); 
            }
        }
    }

    std::vector<std::jthread> workers_;                 
    std::queue<std::function<void()>> tasks_;           
    std::mutex queue_mutex_;                            
    std::condition_variable condition_;                 
    std::stop_source pool_internal_stop_source_;        
};
// -------------------- End ThreadPool Class --------------------


int main()
{
    std::cout << "Running Traffic Sensor with a dedicated 5-thread pool for sensors...\n";

    SharedDataStore data_store;
    std::stop_source master_stop_source; // Used to signal tasks (sensors, logger) to stop their work

    // --- ThreadPool for Sensors ---
    // Create a thread pool with SENSOR_COUNT (5) threads.
    const size_t sensor_pool_size = SENSOR_COUNT;
    ThreadPool sensor_pool(sensor_pool_size);

    std::cout << "Main: Submitting " << SENSOR_COUNT << " sensor tasks to the ThreadPool.\n";
    for (int i = 0; i < SENSOR_COUNT; ++i)
    {
        Sensor sensor_instance(i, data_store, SENSOR_UPDATE_INTERVAL_MS);
        
        // Enqueue a lambda function that calls the sensor's operator().
        // This lambda captures the sensor_instance and the master_stop_source's token.
        // 'mutable' allows modification of captured 'sensor_instance' (specifically its mutable members).
        sensor_pool.enqueue([s = std::move(sensor_instance), token = master_stop_source.get_token()]() mutable
                              { 
                                s(token); // s(token) calls Sensor::operator()(token)
                              });
    }
    std::cout << "Main: All sensor tasks submitted.\n";

    // --- Logger Thread ---
    // The logger runs in its own dedicated std::jthread, as requested.
    std::jthread logger_thread(
        Logger(data_store, "traffic_log_threadpool_specific.txt", AGGREGATION_INTERVAL_MS, CONGESTION_THRESHOLD),
        master_stop_source.get_token() // Logger also respects the master_stop_source
    );

    std::cout << "System running. Sensors in ThreadPool (" << sensor_pool_size << " threads), Logger in dedicated thread. Will shut down in 30 seconds...\n";
    std::this_thread::sleep_for(std::chrono::seconds(30));

    std::cout << "Requesting shutdown of tasks and logger...\n";
    master_stop_source.request_stop(); // Signal all sensor tasks and the logger task to stop

    std::cout << "Main thread waiting for ThreadPool and Logger to complete...\n";
    // ThreadPool destructor (called when sensor_pool goes out of scope) handles joining its worker threads.
    // logger_thread (std::jthread) automatically joins upon its destruction.

    std::cout << "Finished Traffic Sensor with ThreadPool for sensors.\n";
    return 0;
}