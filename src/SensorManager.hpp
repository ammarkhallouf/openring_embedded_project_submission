#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>


// ---------------------------------------------------------
// Sensor Data Structures
// ---------------------------------------------------------
struct ImuSample {
    std::uint64_t timestamp_ms;
    float accel_x_g;
    float accel_y_g;
    float accel_z_g;
    float gyro_x_dps;
    float gyro_y_dps;
    float gyro_z_dps;
};

struct TemperatureSample {
    std::uint64_t timestamp_ms;
    float temperature_c;
};

// ---------------------------------------------------------
// Thread-Safe Ring Buffer
// ---------------------------------------------------------
template <typename T, std::size_t Capacity>
class RingBuffer {
public:
    static_assert(Capacity > 0U, "RingBuffer capacity must be greater than zero");

    void push(const T& item)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        buffer_[head_] = item;
        head_ = (head_ + 1U) % Capacity;

        if (full_) {
            tail_ = (tail_ + 1U) % Capacity;
            ++overflow_count_;
        } else if (head_ == tail_) {
            full_ = true;
        }
    }

    // Retrieves all unread data and clears the buffer
    std::vector<T> snapshotAndClear()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<T> out;
        out.reserve(sizeUnlocked());

        const std::size_t count = sizeUnlocked();
        for (std::size_t i = 0U; i < count; ++i) {
            const std::size_t index = (tail_ + i) % Capacity;
            out.push_back(buffer_[index]);
        }

        head_ = 0U;
        tail_ = 0U;
        full_ = false;

        return out;
    }

    std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return sizeUnlocked();
    }

    std::size_t overflowCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return overflow_count_;
    }

private:
    std::size_t sizeUnlocked() const
    {
        if (full_) {
            return Capacity;
        }

        if (head_ >= tail_) {
            return head_ - tail_;
        }

        return Capacity - tail_ + head_;
    }

    std::array<T, Capacity> buffer_{};
    std::size_t head_{0U};
    std::size_t tail_{0U};
    bool full_{false};
    std::size_t overflow_count_{0U};
    mutable std::mutex mutex_;
};

// ---------------------------------------------------------
// Sensor Manager
// ---------------------------------------------------------
class SensorManager {
public:
    // Data Insertion APIs
    void pushImuSample(const ImuSample& sample);
    void pushTemperatureSample(const TemperatureSample& sample);
    // BLE-style Data Retrieval APIs
    std::vector<ImuSample> getImuSnapshot();
    std::vector<TemperatureSample> getTemperatureSnapshot();

    // Diagnostics
    std::size_t imuOverflowCount() const;
    std::size_t temperatureOverflowCount() const;

private:
    // Retain latest 100 IMU samples
    RingBuffer<ImuSample, 100U> imu_buffer_;
    // Retain latest 20 temperature samples
    RingBuffer<TemperatureSample, 20U> temperature_buffer_;
};