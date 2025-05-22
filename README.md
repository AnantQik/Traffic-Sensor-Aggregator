# 🚦 Traffic Sensor Simulation Project

## 📝 Description
Welcome to the Traffic Sensor Simulation project! This system simulates a network of traffic sensors that concurrently generate data (like traffic density). This data is then collected, aggregated, and logged periodically. The simulation also includes a feature to detect and alert for traffic congestion based on predefined thresholds.

The primary goal of this project is to demonstrate and contrast different C++ multithreading and concurrency management techniques. It showcases two distinct implementations:
1.  A **basic version** using fundamental C++11 threading primitives.
2.  A **modern version** leveraging C++20 features like `std::jthread`, `std::stop_token`, and a custom `ThreadPool` for more robust and cleaner concurrent programming.

This project is a great way to learn about thread creation, synchronization, data sharing between threads, and graceful shutdown in C++.

## ✨ Key Features
* **Multiple Sensors**: Simulates a configurable number of traffic sensors.
* **Concurrent Data Generation**: Each sensor operates in its own thread or as a task in a thread pool, producing data in parallel.
* **Centralized Data Aggregation**: Data from all sensors is collected into a shared data store.
* **Periodic Logging**: A dedicated logger thread periodically processes the collected data, calculates statistics (average, minimum, maximum density), and writes them to a log file.
* **Congestion Alerts**: The system can print alerts to the console if traffic density exceeds a specified threshold.
* **Graceful Shutdown**: Both versions implement mechanisms to signal all operations to stop and ensure threads terminate cleanly.
* **Two distinct C++ concurrency approaches** for comparison and learning.

---

##  versões do código (Code Versions)

This project includes two primary source files, each representing a different approach:

### 1. Modern C++ Version (`traffic_sensor_threadpool_specific.cpp`)
This implementation uses modern C++ (C++20) features to build a more robust, safe, and maintainable concurrent application.

* **Key Characteristics**:
    * **Threads**: Uses `std::jthread`, which provides RAII (Resource Acquisition Is Initialization) for threads (automatic joining on destruction).
    * **Interruption**: Employs `std::stop_source` and `std::stop_token` for a clean, cooperative interruption mechanism.
    * **Sensor Management**: A custom `ThreadPool` class manages a pool of worker threads. Sensor operations are enqueued as tasks to this pool.
    * **Object-Oriented Design**: Logic is encapsulated within classes:
        * `Sensor`: Represents an individual sensor.
        * `Logger`: Handles data aggregation and file logging.
        * `SharedDataStore`: A thread-safe class for storing and retrieving traffic data, encapsulating the mutex and condition variable.
        * `ThreadPool`: Manages worker threads for tasks.
    * **Data Flow**: Sensors (running in the thread pool) add data to `SharedDataStore`. The `Logger` (in its own `jthread`) retrieves data from `SharedDataStore`.

### 2. Basic C++11 Version (`traffic_sensor_basic.cpp`)
This version demonstrates how to build a similar concurrent application using more fundamental C++11 threading primitives.

* **Key Characteristics**:
    * **Threads**: Uses `std::thread` directly, requiring manual `join()` calls.
    * **Interruption**: Relies on a global `bool shutdown_flag` that all threads must check periodically.
    * **Sensor Management**: Sensor logic is in a free function (`sensor_thread_basic`), and each sensor runs in a separately created `std::thread`.
    * **Data Sharing**: Uses global variables for the shared data buffer (`std::vector<TrafficData> sensor_data`), a global `std::mutex` (`data_mutex`), and a global `std::condition_variable` (`cv`).
    * **Callbacks**: Sensors use a C-style function pointer (`void (*callback)(TrafficData)`) to pass data to an `aggregator_callback` function, which then writes to the global data store.
    * **Procedural Elements**: While using structs, the overall organization leans more towards free functions managing global state.

---

## 💡 Core Concepts Demonstrated

This project is a practical showcase of several important concurrency concepts:

* **Multithreading**:
    * Creating and managing threads (`std::thread` vs. `std::jthread`).
    * Understanding thread lifecycles and termination.
* **Synchronization Primitives**:
    * `std::mutex` (with `std::lock_guard` and `std::unique_lock`) for ensuring exclusive access to shared resources and preventing data races.
    * `std::condition_variable` for building efficient producer-consumer patterns (e.g., logger waiting for data from sensors).
* **Thread-Safe Data Sharing**:
    * Strategies for sharing data between threads (global variables with external locks vs. encapsulated data structures with internal synchronization).
* **Cooperative Interruption**:
    * Signaling threads to stop their work gracefully (boolean flag vs. `std::stop_token`).
* **Thread Pools (Modern Version)**:
    * Concept and basic implementation of a thread pool to manage a group of reusable worker threads.
* **RAII (Resource Acquisition Is Initialization)**:
    * Evident in `std::jthread`, `std::ofstream`, and `std::unique_lock`/`std::lock_guard`.
* **Design Approaches**:
    * Object-Oriented Design for concurrent systems (encapsulating shared state and behavior).
    * Callback mechanisms for inter-thread communication.
* **C++ Standard Library Features**:
    * `<chrono>` for time-based delays and timestamps.
    * `<random>` for simulating data.
    * `<fstream>` for file I/O.
    * `<functional>` for `std::function` (used in the ThreadPool).
    * `<vector>`, `<queue>`, `<string>`, `<sstream>`, `<iomanip>`, `<algorithm>`.

---

## 🛠️ How to Compile

You'll need a C++ compiler.
* For the **Modern Version**, a compiler supporting C++20 is required (for `std::jthread`, `std::stop_token`).
* For the **Basic Version**, a compiler supporting C++11 is sufficient.

**Example compilation commands using g++:**

* **Modern Version (`traffic_sensor_threadpool_specific.cpp`)**:
    ```bash
    g++ -std=c++20 traffic_sensor_threadpool_specific.cpp -o traffic_simulation_modern -pthread
    ```

* **Basic Version (`traffic_sensor_basic.cpp`)**:
    ```bash
    g++ -std=c++11 traffic_sensor_basic.cpp -o traffic_simulation_basic -pthread
    ```

*(Note: Replace `g++` with your compiler like `clang++` if needed. The `-pthread` flag is typically required on Linux/macOS for linking the pthreads library.)*

---

## 🚀 How to Run

Once compiled, you can execute the programs from your terminal:

* To run the **Modern Version**:
    ```bash
    ./traffic_simulation_modern
    ```
    This will generate `traffic_log_threadpool_specific.txt`.

* To run the **Basic Version**:
    ```bash
    ./traffic_simulation_basic
    ```
    This will generate `traffic_log_basic.txt`.

Both simulations will print status messages to the console indicating sensor and logger activity. They are set to run for a predefined duration (e.g., 30 seconds) and then automatically initiate a shutdown sequence. The generated log files will contain the aggregated traffic statistics.

---

## 🏗️ Code Structure Overview

### Modern Version (`traffic_sensor_threadpool_specific.cpp`)
* **`TrafficData` (struct):** Defines the structure for a single sensor reading.
* **`SharedDataStore` (class):** A thread-safe container for `TrafficData`. It manages internal locking using `std::mutex` and `std::condition_variable`.
* **`Sensor` (class):** Represents a sensor. Its `operator()` contains the logic for generating data and adding it to the `SharedDataStore`. Designed to be run as a task.
* **`Logger` (class):** Handles fetching data batches from `SharedDataStore`, processing them (calculating aggregates), and writing to the log file. Its `operator()` contains the main logging loop.
* **`ThreadPool` (class):** A custom thread pool that creates a fixed number of worker `std::jthread`s. It has a queue for `std::function<void()>` tasks and distributes these tasks to its workers.
* **`main()`:**
    1.  Initializes `SharedDataStore` and `std::stop_source`.
    2.  Creates a `ThreadPool` for sensor tasks.
    3.  Launches `SENSOR_COUNT` sensor tasks, enqueuing them to the `ThreadPool`. Each task is a lambda capturing a `Sensor` instance and a `std::stop_token`.
    4.  Launches the `Logger` in a separate `std::jthread`, also passing it a `std::stop_token`.
    5.  Waits for a period, then requests a stop via the `std::stop_source`.
    6.  Relies on RAII for `ThreadPool` and `logger_thread` to join threads.

### Basic Version (`traffic_sensor_basic.cpp`)
* **`TrafficData` (struct):** Defines the structure for a single sensor reading.
* **Global Variables**:
    * `sensor_data` (`std::vector<TrafficData>`): Stores all sensor readings.
    * `data_mutex` (`std::mutex`): Protects access to `sensor_data`.
    * `cv` (`std::condition_variable`): Used by sensors to notify the logger of new data.
    * `shutdown_flag` (`bool`): Signals all threads to terminate.
* **`sensor_thread_basic(int id, void (*callback)(TrafficData))` (function):** The main function for each sensor thread. It generates data and passes it to the `callback`.
* **`aggregator_callback(TrafficData data)` (function):** This function is called by `sensor_thread_basic`. It locks `data_mutex`, adds the data to the global `sensor_data` vector, and notifies the logger using `cv.notify_all()`.
* **`logger_thread_basic()` (function):** The main function for the logger thread. It periodically waits on the `cv` (with a timeout), then locks `data_mutex`, processes data from `sensor_data`, logs it, and clears `sensor_data`.
* **`main()`:**
    1.  Creates a `std::vector<std::thread>` to hold sensor threads.
    2.  Launches `SENSOR_COUNT` sensor threads, each running `sensor_thread_basic` and passing `aggregator_callback`.
    3.  Launches the logger thread running `logger_thread_basic`.
    4.  Waits for a period, then sets `shutdown_flag` to `true`.
    5.  Manually `join()`s all sensor threads and the logger thread.

---

## 🔗 Dependencies
* A C++ compiler (supporting C++11 for basic, C++20 for modern).
* Standard C++ Library (STL).
* The pthreads library (usually linked with `-pthread` on Linux/macOS when using `std::thread`).

Happy Coding! 🚀