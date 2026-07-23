# System Architecture & Tradeoffs

## Firmware Architecture & OS Runtime Approach
The firmware is built using modern C++17 wrapped around Zephyr RTOS C APIs, specifically utilizing the Nordic nRF Connect SDK (NCS). 

*   **OS Comparison & Selection:**
    *   **Zephyr/NCS (Recommended):** Provides out-of-the-box integration with the nRF54L15's low-power states, a robust, certified open-source BLE stack, DeviceTree for hardware abstraction, and MCUBoot for OTA updates.
    *   **Bare-Metal (Super-loop):** While offering ultimate control and minimal footprint, bare-metal is highly unsuited for complex, multi-state BLE connectivity and OTA updates. Managing asynchronous radio events alongside sensor processing would lead to fragile, unmaintainable code.
    *   **Lightweight RTOS (e.g., FreeRTOS):** A viable alternative, but it requires significantly more manual effort to integrate Nordic's SoftDevice/BLE controller and tune power management compared to Zephyr's native ecosystem.
*   **Why C++?** Strong typing, RAII for hardware locks (mutexes), and interfaces (pure virtual classes) make unit testing hardware-dependent logic much easier via mocking. Dynamic allocation (`new`/`malloc`) is strictly forbidden after initialization.

## Major Firmware Modules
The system is logically partitioned to enforce separation of concerns:
1.  **Sensor Drivers:** Abstracted interfaces (`ISensor`) handling direct I2C/SPI register reads/writes and interrupt configuration.
2.  **Sensor Manager:** The orchestrator that manages sensor lifecycles, collects data into ring buffers, and handles power state transitions.
3.  **Algorithm Processing:** Consumes raw IMU and PPG data to compute derived metrics (e.g., heart rate, steps, sleep stages).
4.  **BLE/GATT Service Manager:** Manages connection parameters, advertising, and custom GATT profiles to expose sensor data and ring state.
5.  **Power Manager:** Interacts with the PMIC and Zephyr's power management API to aggressively suspend idle threads and peripherals.
6.  **Storage/Logging Manager:** Wraps a file system (e.g., LittleFS) over internal flash for offline data retention.
7.  **UI/Haptics & Touch Input:** Manages the capacitive touch controller for user input and drives the vibrator motor with specific PWM patterns for feedback.
8.  **Diagnostics & Crash Handling:** Responsible for watchdog feeding, capturing fault dumps, and logging system health.
9.  **OTA/DFU (MCUBoot):** Handles secure, dual-bank firmware updates over BLE.

## Thread, Workqueue, and Event Topology
To avoid fake precision, exact stack sizes, execution timing budgets, and integer priority levels must be determined empirically using tools like Thread Analyzer and Segger SystemView. However, the *relative* topology is as follows:

*   **Hardware Interrupts (ISRs) - Highest Priority:** 
    *   *Responsibility:* Minimal execution time. Clear hardware flags, push minimal data (e.g., a timestamp or a single byte), and defer actual processing to a workqueue.
*   **BLE Radio/Network Thread - Very High Priority:**
    *   *Responsibility:* Strict timing requirements defined by the BLE protocol. Must preempt other tasks to maintain the connection link and handle TX/RX events.
*   **Sensor/Algorithm Thread (or High-Priority Workqueue) - Medium Priority:**
    *   *Responsibility:* Triggered by data-ready events. Retrieves data from hardware FIFOs via DMA, runs DSP algorithms (HR calculation), and queues results. 
*   **System/Background Thread (or Standard Workqueue) - Low Priority:**
    *   *Responsibility:* Flash storage writes, battery monitoring, and housekeeping. Flash operations can be slow and block execution, so they must run at a low priority to prevent stalling the BLE link or sensor sampling.

## Synchronization Strategy (Sensor Data to BLE)
Directly accessing sensor variables from the BLE thread introduces race conditions. We utilize:
1.  **Thread-Safe Ring Buffers:** Raw, high-frequency data (like 50Hz IMU samples) is pushed into pre-allocated, mutex-guarded ring buffers by the sensor manager. The BLE thread safely pops this data.
2.  **Message Queues (Zephyr `k_msgq`):** For asynchronous events (e.g., "Tap Detected" or "Step Goal Reached"), we pass small, fixed-size structs by copy through message queues from the sensor/algorithm modules to the BLE module. 
3.  **Zero-Copy Memory Slabs:** If sending large bulk logs (e.g., overnight sleep data) over BLE, we allocate blocks from a fixed memory slab, populate them, and pass the pointer to the BLE stack to avoid copying massive arrays in constrained RAM.

## Error Handling, Watchdog, and OTA Update Strategy
*   **Hardware Watchdog (WDT):** Configured to reset the system if the low-priority background thread fails to feed it within a specified timeout. This ensures that if high-priority threads starve the system, a recovery reset occurs.
*   **Crash Logging:** Zephyr's fatal error handler is overridden. If a hard fault occurs, the CPU registers, program counter, and a minimal stack trace are written to a reserved section of non-volatile flash *before* the reboot. This crash log is transmitted via BLE upon the next successful connection.
*   **OTA/DFU:** Managed by MCUBoot. We use a dual-bank flash layout (Slot 0 for Active, Slot 1 for Download). The firmware is downloaded in the background. Upon reset, MCUBoot validates the cryptographic signature. If the new firmware crashes repeatedly, MCUBoot automatically rolls back to the previous known-good image.

## Unit and Integration Test Approach
*   **Unit Testing (Host/PC):** Business logic, algorithms, and buffer management (e.g., `RingBuffer`) are compiled natively on a PC and tested using GoogleTest/GoogleMock. Hardware dependencies (sensors, flash) are mocked out using C++ pure virtual interfaces (`ISensor`).
*   **Integration Testing (Hardware-in-the-Loop):** Zephyr's `Ztest` framework runs on the actual nRF54L15 hardware. We automate test scripts (via Python/pytest over a debug UART) to verify true hardware behavior, such as verifying SPI transactions, checking real memory allocations, and measuring true execution time of algorithms.

---

## Power-Aware Operating Model
To achieve the targets for multi-day battery life, the system relies on a strictly defined state machine that dictates subsystem power and wake behaviors.

### System States
1.  **Shipping State:** Lowest possible power. Sensors, BLE, and most RAM are powered off. 
2.  **Idle (Disconnected):** Ring is worn but disconnected from the phone. BLE advertising is active at a low duty cycle. Base sensors (IMU) are in low-power wake-on-motion mode.
3.  **Connected Idle:** Connected to the mobile app, but no active streaming is requested. Connection parameters use high slave latency.
4.  **Active Streaming:** Workout or live-view mode. The 24-hour battery tradeoff state. PPG and IMU sample continuously. BLE streams at low latency (15-30ms intervals).
5.  **Sleep Tracking (Background):** Ring is disconnected or in Connected Idle overnight. Heavy reliance on sensor batching.
6.  **Charging:** Wakes the system from Shipping or Fault states. BLE can operate, but vibration/haptics are disabled to minimize thermal buildup.
7.  **Fault:** A safe-mode state triggered by repeated watchdog resets or hard faults. BLE advertises a diagnostic profile; sensors are disabled.

### Battery Life Trade-Offs: 24-Hour vs. 1-Week Operation
Balancing the platform between active real-time data output and set-and-forget longevity requires two fundamentally different operating models.

The 24-hour target is mainly a research/development mode. The 1-week target requires aggressive duty cycling, long BLE intervals, low-leakage hardware states, and avoiding continuous PPG/radio activity.


| Feature | 24-Hour Active Streaming Mode | 1-Week Duty-Cycled Background Mode |
| :--- | :--- | :--- |
| **Sensor Sampling Rates** | IMU: 50Hz (Continuous)<br>PPG: 25Hz+ (Continuous) | IMU: 12.5Hz (Wake-on-Motion)<br>PPG: Duty-cycled (e.g., 5s ON / 55s OFF) |
| **BLE Behavior** | Low latency (15-30ms intervals)<br>Continuous Notifications | High slave latency (4-8)<br>Intermittent bulk data syncs |
| **Expected User Experience** | Live dashboard metrics (workouts)<br>Requires daily charging | "Set-and-forget" sleep tracking<br>Syncs upon app open, weekly charging |
| **Data Fidelity** | High (Beat-to-beat HRV, raw kinematics) | Low/Aggregated (Average HR/min, Sleep Stages) |
| **Estimated Power Impact** | ~800 µA – 1 mA average | ~100 µA average |
| **Engineering Risks** | Battery swelling (peak discharge), thermal throttling, dropped BLE packets | Flash memory wear/overflow, missing brief biometric anomalies during OFF cycles |

### Wake Sources and Transitions
*   **Charger Connect:** Wakes MCU from Shipping/Fault states via PMIC interrupt.
*   **Sensor FIFO Watermark:** The primary wake source during Sleep Tracking. The MCU sleeps for ~2 seconds, wakes to a GPIO interrupt from the IMU/PPG FIFO, empties it via DMA, processes the data, and returns to sleep.
*   **Touch Sensor GPIO:** Wakes the system from Idle to Connected Idle or triggers a user action (e.g., dismiss an alarm).
*   **RTC Timer:** Wakes the MCU periodically (e.g., every 500ms) to handle BLE advertising or connection events if the radio requires CPU intervention.

### Storage and Disconnected Logging Strategy
When the phone is disconnected, the ring acts as a standalone data logger. 
*   **Batching Strategy:** Instead of storing raw 50Hz IMU and 25Hz PPG data (which would fill the flash in minutes), the MCU calculates high-level features (e.g., heart rate in BPM, step counts, sleep motion index) and stores these summaries.
*   **LittleFS Circular Log:** Processed data is appended to a LittleFS partition on the nRF54L15's internal flash.
*   **Overwrite Policy:** If the user does not sync with the app and the flash fills up, the oldest logged blocks are silently overwritten to ensure the most recent days are always preserved.

### Current-Consumption Budget (Estimates)
*Assumptions: 3.7V nominal Li-Po chemistry, ~20mAh capacity.*

| System State | Estimated Avg. Current | Estimated Battery Life | Notes |
| :--- | :--- | :--- | :--- |
| **Shipping** | < 1 µA | > 2 Years | MCU System OFF, PMIC ship-mode. |
| **Sleep Tracking** | ~100 µA | ~8 Days | Achieves the 1-week target. Averages ultra-low sleep current (~2µA) with brief 5mA spikes for sensor processing and flash writes. |
| **Connected Idle** | ~120 µA | ~7 Days | Same as sleep tracking, plus low-frequency BLE connection events (Slave Latency = 8). |
| **Active Streaming** | ~800 µA | ~25 Hours | Achieves the 24-hour target. Continuous 50Hz BLE notifications, PPG LEDs active. |

### Power Measurement and Validation
*   **Tooling:** Nordic Power Profiler Kit II (PPK2) or a Joulescope.
*   **Methodology:** Power cannot be measured via instantaneous spot-checks due to the spiky nature of BLE and sensor bursts.
*   **Validation:** We measure the Area Under the Curve (AUC) over a full 60-second operational window (encompassing sleep, wake, sensor DMA read, algorithm processing, and a BLE TX event) to calculate the true average current in microamps (µA).

---

## Next-Revision Hardware Architecture
To address the severe constraints of a smart ring platform, the hardware architecture must minimize routing complexity, isolate noise, and strictly control power domains.

### Candidate Bus Topology
*   **High-Speed Bus (Shared SPI):** The IMU and PPG sensors reside on a shared SPI bus. SPI allows for high-throughput DMA transfers, minimizing the time the MCU spends in an active state reading FIFOs.
*   **Low-Speed Bus (Shared I2C):** The Temperature sensor, Capacitive Touch controller, and Fuel Gauge share a fast-mode (400kHz) I2C bus. These devices require lower bandwidth and internal pull-ups can be dynamically disabled during deep sleep.
*   **Haptics Interface:** The vibrator motor is driven via a dedicated hardware PWM pin on the nRF54L15 attached to a low-Rds(on) MOSFET, rather than an I2C motor driver, to save PCB area and cost.

### Interrupts, FIFO Watermarking, & Wake Sources
*   **Sensor Wake Sources:** The IMU and PPG each have a dedicated hardware interrupt pin routed to the MCU. They are configured to assert an interrupt only when their internal hardware FIFOs reach a predefined watermark (e.g., 80% full). This allows the MCU to sleep through dozens of sample periods.
*   **Touch Wake:** The capacitive touch sensor is configured in an ultra-low-power scanning mode, waking the MCU via a dedicated GPIO interrupt only upon a confirmed tap or swipe.
*   **Fuel Gauge Alert:** An interrupt from the fuel gauge alerts the MCU to critical low-battery events or state-of-charge milestones without requiring continuous polling.

### Power Domains & Power Gating
*   **Domain 1 (Always-On):** Connects directly to the PMIC's primary buck/LDO. Powers the nRF54L15 MCU, the Touch controller, and the Fuel Gauge. 
*   **Domain 2 (Sensor VDD):** Powers the IMU, PPG, and Temperature sensor. This domain is isolated behind an ultra-low-leakage load switch (or a dedicated PMIC LDO) controlled by an MCU GPIO. This allows for a hard physical power-down (0µA leakage) of the entire sensor suite during deep sleep.
*   **Domain 3 (Motor VDD):** The vibrator motor operates directly from the system battery voltage (VSYS) rather than a regulated rail. This prevents the high-current inductive spikes of the motor from drooping the regulated rails and causing MCU brown-outs.

### BLE Antenna & RF Layout Considerations
*   **Placement:** The ring form factor requires the antenna to be placed on the top surface of the finger. The fleshy underside of the finger absorbs too much RF energy.
*   **Technology:** An LDS (Laser Direct Structuring) antenna fabricated directly onto the plastic inner-top surface of the ring's outer shell is recommended, contacting the PCB via spring clips.
*   **Matching:** A pi-network matching circuit placeholder must be included as close to the nRF54L15 RF pin as possible to tune for the detuning effect of human tissue.

### Battery, Charger, Fuel Gauge, and Protection
*   **Battery Protection:** An integrated or discrete protection IC monitors for Over-Voltage (OVP), Under-Voltage (UVP), Over-Current (OCP), and features a thermal thermistor (NTC) to disable charging if the battery exceeds safe temperatures.
*   **Charger:** A linear PMIC handles the charging profile, limited to a low trickle charge suitable for a ~15-20mAh cell.
*   **Fuel Gauge:** A Coulomb-counting fuel gauge (e.g., MAX1726x series) is placed on the battery side of the system to track exact charge state, compensating for cell aging and temperature.

### DFM/DFT & Production Testing
*   **Rigid-Flex PCB:** The main board will be a rigid-flex design. The rigid sections house the MCU and IMU, while the flex sections wrap around the finger to position the PPG and touch sensors.
*   **Test Points:** Standardize pogo-pin test pads on the innermost surface of the flex tail (before it is tucked or potted). Required pads: `VSYS`, `GND`, `SWDIO`, `SWDCLK`, `RESET`, and a single `UART_TX` for high-speed logging.
*   **Production Testing:** 
    1. **In-Circuit Test (ICT):** Probes the flex tail pads to verify all voltage rails, program the initial firmware, and verify basic I2C/SPI `WHO_AM_I` register reads.
    2. **RF Calibration:** Conducted in a shielded box to trim the matching network and verify BLE transmit power.
    3. **Final Assembly Validation:** Ensures the vibrator motor actuates and the PPG LEDs illuminate before the device is permanently sealed/potted.

---

## BLE 5.x Interface Design
To optimize throughput and power, the BLE 5.x design utilizes Data Length Extension (DLE) to maximize MTU size, minimizing protocol overhead.

### Connection Parameter Strategy
We define two distinct BLE connection states driven by the app's current requirement:
1.  **Active Streaming Mode (Foreground):** Used when the user is actively viewing live data.
    *   *Connection Interval:* 15ms to 30ms.
    *   *Slave Latency:* 0.
    *   *Tradeoff:* High responsiveness, but significant battery drain.
2.  **Low-Power Background Mode (Default):** Used for periodic syncing throughout the day/week.
    *   *Connection Interval:* 200ms to 500ms.
    *   *Slave Latency:* 4 to 8 (allows the ring to skip up to 8 connection events if it has no data).
    *   *Tradeoff:* Extremely low average radio current; enables the 1-week battery target.

### MTU, Versioning, and Endianness
*   **Endianness:** Little-endian is strictly enforced for all multibyte fields, adhering to native BLE and ARM Cortex-M architecture standards, avoiding byte-swapping overhead.
*   **MTU/DLE:** We negotiate an MTU of 247 bytes. This allows 244-byte payloads (accommodating batches of 50Hz IMU samples) without fragmentation.
*   **Versioning:** Every payload header includes a 1-byte Protocol Version field.

### Custom GATT Services and Characteristics
Instead of forcing data into standard profiles, we utilize a primary custom service to efficiently batch multi-sensor data.

| Service | Characteristic | UUID | Properties | Purpose |
| :--- | :--- | :--- | :--- | :--- |
| **Ring Data** | IMU Stream | `1234...0001` | Notify | Batched 50Hz accelerometer/gyro data. |
| | Biometric Stream | `1234...0002` | Notify | HR, HRV, and SpO2 derived from PPG. |
| | Temperature | `1234...0003` | Read / Notify | 1Hz skin temperature readings. |
| **Command & Status** | Control RX | `5678...0001` | Write | App commands (e.g., "Start Workout", "Trigger Haptic"). |
| | Status TX | `5678...0002` | Notify | Ring state changes, battery alerts, diagnostic acks. |
| **OTA / DFU** | SMP / DFU | `8D53...0000` | Write No Rsp / Notify | Standard Zephyr Simple Management Protocol. |

### Packet Format Examples
To ensure data integrity, all streaming packets include a 16-bit sequence number (to detect dropped notification packets) and a 32-bit RTC timestamp (ticks since boot or epoch).

**1. IMU Batch Packet (Notify)**
*Payload packs multiple 50Hz samples into one MTU to save radio time.*
*   `uint8_t version;` (Protocol version)
*   `uint16_t seq_num;` (Sequence tracking)
*   `uint32_t timestamp;` (Base timestamp for the first sample)
*   `uint8_t sample_count;` (Number of IMU frames in this payload)
*   `struct { int16_t x, y, z; } accel[sample_count];`

**2. Biometric Data Packet (Notify)**
*   `uint8_t version;`
*   `uint16_t seq_num;`
*   `uint32_t timestamp;`
*   `uint8_t heart_rate_bpm;`
*   `uint16_t hrv_ms;`
*   `uint8_t confidence_level;` (0-100%, indicating PPG signal quality)

**3. Control Command (Write)**
*   `uint8_t command_id;` (e.g., `0x01` = Haptic Feedback, `0x02` = Set Time)
*   `uint8_t payload_len;`
*   `uint8_t payload[];` (e.g., If Haptic, payload is `duration_ms` and `intensity`)

**4. Status & Diagnostics (Notify)**
*   `uint8_t status_type;` (e.g., `0x0A` = Low Battery, `0x0B` = HardFault Dump Ready)
*   `uint32_t error_code;`

### OTA/DFU Approach
We utilize Zephyr's Simple Management Protocol (SMP) over BLE. 
*   The mobile app connects and negotiates the MTU.
*   The app sends firmware chunks via `Write Without Response` to the SMP characteristic to maximize throughput.
*   The ring writes these chunks to the secondary flash slot (Slot 1).
*   Upon completion and cryptographic verification, the MCU reboots, and MCUBoot swaps the images.

---



## Data Flow
1. **Sensors** fill hardware FIFOs autonomously.
2. **Interrupt (GPIO)** wakes the nRF54L15.
3. `SensorManager` schedules a Zephyr Work Queue item.
4. Where supported by the selected peripheral and driver stack, use DMA/easyDMA-style transfers to reduce CPU wake time.
5. Data is passed to `AlgorithmProcessing` (HR/Steps).
6. Results are queued to `BLEManager` for transmission.
7. MCU returns to sleep.

## Future Sensor Expansion
The system utilizes a modular driver interface. To add a new sensor:
1.  **Interface Adherence**: Implement the `ISensor` base class.
2.  **Bus Topology**: The hardware is designed with a shared SPI/I2C bus. New sensors must support standard addressing to avoid collisions.
3.  **Manager Registration**: Update the `SensorManager` to include the new sensor instance, allowing the RTOS Work Queue to poll/interrupt it independently without refactoring the core logic.

---

## Assumptions, Risks, and Unknowns
To effectively scope the architecture, several variables and edge cases must be addressed.

| Topic | Assumption | Risk / Unknown | Mitigation |
|---|---|---|---|
| IMU | 6-axis IMU with FIFO and data-ready interrupt | Exact part, FIFO depth, current consumption unknown | Abstract driver API and validate timing/current during bring-up |
| PPG | Integrated optical AFE with interrupt and configurable LED current | Optical/mechanical stack not specified | Prototype LED current, sampling rate, and motion rejection early |
| Battery | Small rechargeable Li-ion/LiPo cell suitable for ring form factor | Capacity, ESR, charger IC, and fuel gauge unknown | Build power budget using measured currents and battery characterization |
| BLE | Phone app supports notifications and control writes | MTU, connection interval, and background behavior vary by OS | Support adaptive connection parameters and packet versioning |
| PCB | Compact rigid-flex or high-density board | RF, antenna detuning, thermal, and assembly yield risks | Add test points, RF keepouts, impedance control, and DFM review |

### Assumptions
*   **Sensor Interfaces & FIFO/Interrupts:** The IMU and PPG communicate via a shared SPI or fast I2C bus to minimize MCU pin utilization. Crucially, I assume the selected sensors feature sufficiently deep internal hardware FIFOs and dedicated interrupt pins. This allows them to collect data autonomously while the nRF54L15 remains in deep sleep.
*   **Battery Capacity & Charging:** I assume a standard ring form-factor battery capacity of roughly 15–20mAh. The system will rely on a dedicated PMIC (Power Management IC) for charging to enforce strict over-voltage and thermal limits.
*   **Mobile App Readiness:** Because the mobile application is being developed in parallel, I assume the BLE GATT profile will experience churn. The firmware architecture must use a flexible serialization format (like Protocol Buffers or NanoPB) to easily version the interface.
*   **Hardware Ambiguity:** In the absence of specific schematics, layouts, or sensor part numbers, I am assuming the use of standard ultra-low-power wearable components (e.g., Bosch IMUs, Analog Devices AFEs) with standard digital interfaces.

### Risks & Unknowns
*   **Sensor Selection (Risk):** If a sensor selected by the hardware team lacks an adequate FIFO buffer, the MCU will be forced to wake up at the sampling frequency (e.g., 50Hz for IMU). This would have a significant impact on the power management strategy.
*   **Battery Swelling (Risk):** Drawing high peak currents (e.g., simultaneously running the vibrator motor and the BLE radio during a notification) can stress tiny pouch cells. This risks swelling, which could breach the waterproof sealing of the ring.
*   **App Protocol Mismatches (Risk):** Parallel development of the app and firmware risks synchronization issues. If the app team expects raw streaming but the firmware defaults to edge-compute summaries, data pipelines will break.
*   **RF Detuning (Unknown):** The human finger acts as a sink for RF energy. The precise layout of the BLE antenna and its real-world performance on a finger are unknown. If detuning is severe, the MCU will have to boost BLE transmit power, severely degrading battery life.