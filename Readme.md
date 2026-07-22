# Open-Source Smart Ring Platform - Technical Assessment

This repository contains architectural and firmware design assessment for the Open-Source Smart Ring platform. The focus is on system architecture, power management, modular C++ design, and validation strategies.

## Overview
Given the severe space and power constraints of a smart ring, the primary design driver is maximizing battery life while ensuring reliable physiological data collection. The architecture assumes an event-driven RTOS environment (Zephyr RTOS) utilizing the Nordic nRF54L15's low-power modes.

## Contents
*   `docs/Architecture.md`: System Architecture
*   `docs/Debugging.md`: Root cause analysis for manufacturing and operational issues.
*   `src/`: Sensor manager simplified implementation
*   `tests/`: Unit tests for sensor manager logic.

## Simplified Sensor Manager
A hardware-independent C++ implementation simulating sensor manager workflow is provided with the following features:

*   **Thread-Safe Ring Buffers:** Protects asynchronous sensor pushes and BLE reads using `std::mutex`.
*   **Data Retention:** Retains the latest 100 IMU samples and 20 Temperature samples, overwriting the oldest data when full.
*   **Overflow Tracking:** Gracefully tracks dropped samples via an internal overflow counter.
*   **BLE Snapshot API:** Allows a Bluetooth subsystem to pull batched sensor data safely, clearing the buffer in the process.

### Build and Test Run Instructions
1.  **Requirements**: CMake 3.14+, C++17 compliant compiler, GoogleTest/Mock.
2.  **Build**: `mkdir build && cd build && cmake .. && make`
3.  **Run Unit Tests**: `./run_tests`

