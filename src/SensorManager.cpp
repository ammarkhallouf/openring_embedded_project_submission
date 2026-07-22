#include "SensorManager.hpp"

// Push IMU sample (simulating 50Hz incoming ISR/Timer data)
void SensorManager::pushImuSample(const ImuData& data) {
    imuBuffer.push(data);
}

// Push Temp sample (simulating 1Hz incoming Timer data)
void SensorManager::pushTemperatureSample(const TempData& data) {
    tempBuffer.push(data);
}

// Retrieve IMU snapshot for BLE subsystem
std::vector<ImuData> SensorManager::getImuSnapshot() {
    return imuBuffer.getSnapshot();
}

// Retrieve Temp snapshot for BLE subsystem
std::vector<TempData> SensorManager::getTemperatureSnapshot() {
    return tempBuffer.getSnapshot();
}

// Fetch IMU overflow count
size_t SensorManager::imuOverflowCount() const {
    return imuBuffer.getOverflowCount();
}

// Fetch Temp overflow count
size_t SensorManager::temperatureOverflowCount() const {
    return tempBuffer.getOverflowCount();
}