#include "SensorManager.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <gtest/gtest.h>
#include <thread>

namespace {

ImuSample makeImu(std::uint64_t timestamp_ms)
{
    return ImuSample{
        timestamp_ms,
        1.0F,
        2.0F,
        3.0F,
        4.0F,
        5.0F,
        6.0F
    };
}

TemperatureSample makeTemp(std::uint64_t timestamp_ms, float temperature_c)
{
    return TemperatureSample{timestamp_ms, temperature_c};
}

} // namespace

TEST(SensorManagerTest, EmptyBufferBehavior)
{
    SensorManager manager;

    EXPECT_TRUE(manager.getImuSnapshot().empty());
    EXPECT_TRUE(manager.getTemperatureSnapshot().empty());
    EXPECT_EQ(manager.imuOverflowCount(), 0U);
    EXPECT_EQ(manager.temperatureOverflowCount(), 0U);
}

TEST(SensorManagerTest, ImuInsertionAndRetrievalOrder)
{
    SensorManager manager;

    manager.pushImuSample(makeImu(1000U));
    manager.pushImuSample(makeImu(1020U));

    const auto snapshot = manager.getImuSnapshot();

    ASSERT_EQ(snapshot.size(), 2U);
    EXPECT_EQ(snapshot[0].timestamp_ms, 1000U);
    EXPECT_EQ(snapshot[1].timestamp_ms, 1020U);
    EXPECT_EQ(snapshot[0].gyro_z_dps, 6.0F);

    EXPECT_TRUE(manager.getImuSnapshot().empty());
}

TEST(SensorManagerTest, ImuRetainsLatestOneHundredSamples)
{
    SensorManager manager;

    for (std::uint64_t i = 1U; i <= 105U; ++i) {
        manager.pushImuSample(makeImu(i * 20U));
    }

    EXPECT_EQ(manager.imuOverflowCount(), 5U);

    const auto snapshot = manager.getImuSnapshot();

    ASSERT_EQ(snapshot.size(), 100U);
    EXPECT_EQ(snapshot.front().timestamp_ms, 6U * 20U);
    EXPECT_EQ(snapshot.back().timestamp_ms, 105U * 20U);
}

TEST(SensorManagerTest, TemperatureRetainsLatestTwentySamples)
{
    SensorManager manager;

    for (std::uint64_t i = 1U; i <= 25U; ++i) {
        manager.pushTemperatureSample(makeTemp(i * 1000U, 30.0F + static_cast<float>(i)));
    }

    EXPECT_EQ(manager.temperatureOverflowCount(), 5U);

    const auto snapshot = manager.getTemperatureSnapshot();

    ASSERT_EQ(snapshot.size(), 20U);
    EXPECT_EQ(snapshot.front().timestamp_ms, 6U * 1000U);
    EXPECT_EQ(snapshot.back().timestamp_ms, 25U * 1000U);
}

TEST(SensorManagerTest, BleStyleSnapshotOnlyReturnsNewDataAfterClear)
{
    SensorManager manager;

    manager.pushTemperatureSample(makeTemp(1000U, 36.5F));
    manager.pushTemperatureSample(makeTemp(2000U, 36.6F));

    const auto first_snapshot = manager.getTemperatureSnapshot();

    ASSERT_EQ(first_snapshot.size(), 2U);

    manager.pushTemperatureSample(makeTemp(3000U, 36.7F));

    const auto second_snapshot = manager.getTemperatureSnapshot();

    ASSERT_EQ(second_snapshot.size(), 1U);
    EXPECT_EQ(second_snapshot[0].timestamp_ms, 3000U);
    EXPECT_FLOAT_EQ(second_snapshot[0].temperature_c, 36.7F);
}

TEST(SensorManagerTest, BasicProducerConsumerConcurrencySmokeTest)
{
    SensorManager manager;
    std::atomic<bool> producer_done{false};
    std::atomic<std::size_t> total_read{0U};

    std::thread producer([&manager, &producer_done]() {
        for (std::uint64_t i = 0U; i < 500U; ++i) {
            manager.pushImuSample(makeImu(i));
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }

        producer_done.store(true);
    });

    std::thread consumer([&manager, &producer_done, &total_read]() {
        while (!producer_done.load()) {
            const auto snapshot = manager.getImuSnapshot();
            total_read.fetch_add(snapshot.size());
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }

        const auto final_snapshot = manager.getImuSnapshot();
        total_read.fetch_add(final_snapshot.size());
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(total_read.load() + manager.imuOverflowCount(), 500U);
}