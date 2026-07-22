#pragma once
#include <cstdint>
#include <cstddef>
#include <mutex>
#include <vector>
#include <array>

// ---------------------------------------------------------
// Thread-Safe Ring Buffer
// ---------------------------------------------------------
template <typename T, size_t N>
class RingBuffer {
public:
    void push(const T& item) {
        std::lock_guard<std::mutex> lock(mtx);
        buffer[head] = item;
        head = (head + 1) % N;
        if (full) {
            tail = (tail + 1) % N; 
            overflow_count++;
        }
        full = (head == tail);
    }

    // Retrieves all unread data and clears the buffer
    std::vector<T> getSnapshot() {
        std::lock_guard<std::mutex> lock(mtx);
        std::vector<T> snapshot;
        if (isEmpty()) return snapshot;

        size_t count = full ? N : (head >= tail ? head - tail : N - tail + head);
        snapshot.reserve(count);

        size_t current = tail;
        for (size_t i = 0; i < count; ++i) {
            snapshot.push_back(buffer[current]);
            current = (current + 1) % N;
        }

        // Reset buffer state after reading
        head = 0;
        tail = 0;
        full = false;
        
        return snapshot;
    }

    size_t getOverflowCount() const {
        std::lock_guard<std::mutex> lock(mtx);
        return overflow_count;
    }

    bool isEmpty() const { return (!full && (head == tail)); }

private:
    std::array<T, N> buffer;
    size_t head = 0;
    size_t tail = 0;
    bool full = false;
    size_t overflow_count = 0;
    mutable std::mutex mtx; 
};

// ---------------------------------------------------------
// Sensor Data Structures
// ---------------------------------------------------------
struct ImuData { 
    uint32_t timestamp; 
    float x, y, z; 
};

struct TempData { 
    uint32_t timestamp; 
    float celsius; 
};

// ---------------------------------------------------------
// Sensor Manager
// ---------------------------------------------------------
class SensorManager {
public:
    SensorManager() = default;

    // Data Insertion APIs
    void pushImuSample(const ImuData& data);
    void pushTemperatureSample(const TempData& data);

    // BLE-style Data Retrieval APIs
    std::vector<ImuData> getImuSnapshot();
    std::vector<TempData> getTemperatureSnapshot();

    // Diagnostics
    size_t imuOverflowCount() const;
    size_t temperatureOverflowCount() const;

private:
    // Retain latest 100 IMU samples
    RingBuffer<ImuData, 100> imuBuffer;
    // Retain latest 20 temperature samples
    RingBuffer<TempData, 20> tempBuffer;
};