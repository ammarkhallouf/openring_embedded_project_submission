# Debugging & Investigation Methodology

Debugging ultra-low-power, highly integrated wearables requires a disciplined, evidence-based methodology. My approach prioritizes isolation of the physical layer (hardware) from the control layer (firmware) before analyzing system-level interactions.

## Phase 1: Hardware Bring-Up & Validation
1.  **Smoke Test & Rails:** Before flashing, measure impedance on all power rails to ensure no shorts. Power via a bench supply with strict current limiting to prevent damage.
2.  **Clocks:** Verify the 32.768 kHz crystal (LFXO) is oscillating, as BLE and RTOS timing depend on it.
3.  **Comms:** Use a logic analyzer (e.g., Saleae) to verify I2C/SPI traffic to sensors. Read the `WHO_AM_I` register of the IMU and PPG first to confirm physical connectivity.

## Phase 2: Firmware Debugging
*   **Logging:** `printf` over UART is too power-hungry and slow. I utilize **SEGGER RTT** (Real-Time Transfer). It writes logs directly to RAM, which the J-Link debugger reads out instantly without stalling the CPU or polluting timing.
*   **HardFault Tracking:** In Zephyr, I enable `CONFIG_FAULT_DUMP`. If the MCU crashes, I parse the stack frame and registers to identify the offending instruction or memory access violation.

## Phase 3: Power Validation
Software bugs often hide in power signatures (e.g., an unreleased mutex preventing sleep, or a floating GPIO pin leaking 50µA).
*   **Tooling:** Nordic Power Profiler Kit II (PPK2) or a Joulescope.
*   **Method:** Establish a baseline sleep current (target: < 2µA). Isolate sensor current to detect leakage, and profile the full active cycle (Wake -> DMA Read -> Process -> BLE TX -> Sleep) to calculate true energy consumption.

---

## Issue A: PPG measurements become unreliable when BLE throughput increases
*   **Likely Root Causes**:
    1.  **EMI/Noise**: The BLE radio frequency is inducing noise into the high-impedance analog signal path of the PPG sensor.
    2.  **Interrupt Contention**: High-priority BLE stack interrupts are starving the PPG's data-ready ISR, causing FIFO overflows on the sensor.
    3.  **Supply Droop**: Transient current spikes during BLE transmission are causing voltage dips at the PPG AFE (Analog Front End).
*   **Evidence to Collect**: PPG noise floor FFT (looking for spikes at BLE frequency), ISR latency histograms (using RTOS tracing), and VDD_Sensor ripple measurements during transmission.
*   **Debugging Process**: Correlate PPG noise timing with "Radio Active" GPIO toggles. Use a spectrum analyzer to confirm the noise source.
*   **Tools**: Oscilloscope (high BW), Spectrum Analyzer, Nordic PPK2, Saleae Logic Analyzer.
*   **Design Changes**:
    *   **Firmware**: Implement an "Anticipatory Power" scheme to mute PPG sampling during heavy BLE transmissions, or utilize DMA for sensor data transfer to remove CPU jitter.
    *   **Hardware**: Add ferrite beads on the PPG power rail and increase decoupling capacitor density. Add a dedicated LDO for the PPG AFE to improve PSRR (Power Supply Rejection Ratio).

## Issue B: Battery life is approximately 40% lower than expected
*   **Likely Root Causes**:
    1.  **Zombie States**: Firmware is not entering true `System OFF` or `System ON` (with RAM retention) sleep due to floating GPIOs or unreleased mutexes.
    2.  **Clocking**: The 32.768kHz crystal is failing to start, forcing the MCU to keep the high-speed RC oscillator running.
    3.  **Peripheral Leakage**: Sensors or motors are not being fully power-gated during sleep.
*   **Evidence to Collect**: 24-hour power profile capture, "Deep Sleep" baseline current measurement, and GPIO state logs.
*   **Debugging Process**: Measure baseline current with all peripherals disconnected (verify MCU floor). Use a `power_log` thread that periodically prints state machine status. Use the PPK2 to identify which subsystem prevents the "floor" current from reaching the < 5µA target.
*   **Tools**: PPK2, Joulescope, Multimeter with µA resolution.
*   **Design Changes**:
    *   **Firmware**: Enforce strict `configureForDeepSleep()` routines that force all unused GPIOs into known states (Hi-Z or pull-up/down) to eliminate leakage.
    *   **Hardware**: Verify that sensor load switches are correctly disconnecting power rails when commanded.

## Issue C: 12% first-pass test failure rate
*   **Likely Root Causes**:
    1.  **Flex-Rigid Stress**: The flex-rigid interface is prone to micro-cracks during the potting or ring-forming assembly process.
    2.  **Solder Voiding**: Fine-pitch BGA or CSP components for the IMU/PPG are sensitive to reflow thermal profiles.
    3.  **ESD Vulnerability**: Sensor AFE inputs are exposed during handling, leading to latent damage.
*   **Evidence to Collect**: Failure distribution map (do failures occur on a specific sensor?), cross-sectional analysis of failed PCBA, and ICT (In-Circuit Test) logs.
*   **Debugging Process**: Analyze ICT/FCT failure logs to determine the common denominator (e.g., always the PPG sensor). Perform destructive physical analysis (DPA) on samples to inspect solder joints under a microscope.
*   **Tools**: Digital Microscope, X-ray inspection, ICT Test Jig.
*   **Design Changes**:
    *   **Test**: Update the ICT jig to probe flex tail pads before potting, enabling immediate rejection of failed boards.
    *   **DFM/DFT**: Add test coupons or dedicated test points to the flex-tail to allow verification of the rigid-flex connection integrity.
    *   **Manufacturing**: Add protective conformal coating or implement ESD-dissipative handling procedures during assembly.