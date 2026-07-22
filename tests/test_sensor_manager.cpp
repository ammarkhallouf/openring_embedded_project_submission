#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <chrono>
#include "../src/SensorManager.hpp"

class SensorManagerTest : public ::testing::Test {
protected:
    SensorManager manager;
};

TEST_F(SensorManagerTest, EmptyBufferBehavior) {
    auto imuSnap = manager.getImuSnapshot();
    auto tempSnap = manager.getTemperatureSnapshot();
    
    EXPECT_TRUE(imuSnap.empty());
    EXPECT_TRUE(tempSnap.empty());
    EXPECT_EQ(manager.imuOverflowCount(), 0);
}

TEST_F(SensorManagerTest, NormalInsertionAndRetrievalOrder) {
    manager.pushImuSample({1000, 1.0f, 0.0f, 0.0f});
    manager.pushImuSample({1020, 2.0f, 0.0f, 0.0f}); // 20ms later (50Hz)
    
    auto snap = manager.getImuSnapshot();
    
    ASSERT_EQ(snap.size(), 2);
    EXPECT_EQ(snap[0].timestamp, 1000);
    EXPECT_EQ(snap[1].timestamp, 1020);
    EXPECT_EQ(snap[0].x, 1.0f);
    EXPECT_EQ(snap[1].x, 2.0f);
    
    // Buffer should be cleared after snapshot
    auto snap2 = manager.getImuSnapshot();
    EXPECT_TRUE(snap2.empty());
}

TEST_F(SensorManagerTest, RetainLatestNSamplesAndOverflowCounter) {
    // Push 105 samples into a 100-sample buffer
    for(uint32_t i = 1; i <= 105; ++i) {
        manager.pushImuSample({i * 20, 1.0f, 1.0f, 1.0f});
    }
    
    // Expect 5 overflows
    EXPECT_EQ(manager.imuOverflowCount(), 5);
    
    auto snap = manager.getImuSnapshot();
    ASSERT_EQ(snap.size(), 100); // Max capacity is 100
    
    // Oldest 5 samples (timestamps 20, 40, 60, 80, 100) should be overwritten.
    // First retained sample should be the 6th inserted (i = 6)
    EXPECT_EQ(snap.front().timestamp, 6 * 20);
    EXPECT_EQ(snap.back().timestamp, 105 * 20);
}

TEST_F(SensorManagerTest, BleStyleSnapshotRetrieval) {
    manager.pushTemperatureSample({1000, 36.5f});
    manager.pushTemperatureSample({2000, 36.6f});
    
    // BLE subsystem reads snapshot
    auto snap = manager.getTemperatureSnapshot();
    EXPECT_EQ(snap.size(), 2);
    
    // Add new data
    manager.pushTemperatureSample({3000, 36.7f});
    
    // Next read should only contain the new data
    auto snap2 = manager.getTemperatureSnapshot();
    ASSERT_EQ(snap2.size(), 1);
    EXPECT_EQ(snap2[0].timestamp, 3000);
}

TEST_F(SensorManagerTest, BasicProducerConsumerConcurrency) {
    std::atomic<bool> running{true};
    std::atomic<size_t> total_read{0};
    
    // Producer thread: Simulating 50Hz IMU interrupts
    std::thread producer([&]() {
        for(uint32_t i = 0; i < 500; ++i) {
            manager.pushImuSample({i, 0.0f, 0.0f, 0.0f});
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        running = false;
    });
    
    // Consumer thread: Simulating BLE asynchronous reads
    std::thread consumer([&]() {
        // 1. Consume while producer is active
        while(running) {
            auto snap = manager.getImuSnapshot();
            total_read += snap.size();
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
        // 2. Drain any remaining items after producer finishes
        auto snap = manager.getImuSnapshot();
        total_read += snap.size();
    });
    
    producer.join();
    consumer.join();
    
    // The total read + the total overflowed must equal the total produced
    EXPECT_EQ(total_read + manager.imuOverflowCount(), 500);
}