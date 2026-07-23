#include "SensorManager.hpp"
// Push IMU sample (simulating 50Hz incoming ISR/Timer data)
void SensorManager::pushImuSample(const ImuSample& sample)
{
    imu_buffer_.push(sample);
}

// Push Temp sample (simulating 1Hz incoming Timer data)
void SensorManager::pushTemperatureSample(const TemperatureSample& sample)
{
    temperature_buffer_.push(sample);
}
// Retrieve IMU snapshot for BLE subsystem
std::vector<ImuSample> SensorManager::getImuSnapshot()
{
    return imu_buffer_.snapshotAndClear();
}

// Retrieve Temp snapshot for BLE subsystem
std::vector<TemperatureSample> SensorManager::getTemperatureSnapshot()
{
    return temperature_buffer_.snapshotAndClear();
}

// Fetch IMU overflow count
std::size_t SensorManager::imuOverflowCount() const
{
    return imu_buffer_.overflowCount();
}

// Fetch Temp overflow count
std::size_t SensorManager::temperatureOverflowCount() const
{
    return temperature_buffer_.overflowCount();
}