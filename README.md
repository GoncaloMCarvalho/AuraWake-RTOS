# AuraWake-RTOS 🌅

AuraWake is an advanced, multithreaded Smart Alarm Clock built on the ESP32 platform using FreeRTOS. It combines environmental sensing, natural light wake-up simulation, and dynamic UI protection into a robust embedded system.

This project goes beyond a simple clock, implementing professional engineering concepts such as Exponential Moving Average (EMA) filtering, Gamma correction for human eye perception, and rigorous I2C hardware protection using Mutexes across dual-core processing.

## Key Features

* **Sunrise Simulation Alarm:** Gradually fades a NeoPixel LED strip from dark warm colors to bright white over 10 minutes to simulate a natural sunrise, promoting a smoother wake-up cycle.
* **Smart Auto-Brightness & Smart Wake:** * Uses an LDR (Photoresistor) coupled with an **EMA (Exponential Moving Average) filter** to dynamically adjust the LED strip brightness, ignoring sudden light spikes or temporary shadows.
    * Applies a **Quadratic (Gamma) Curve** to the brightness calculation, translating linear voltage readings into a logarithmic scale that matches human eye perception.
    * Instantly wakes the OLED display from sleep mode upon detecting significant positive light variations (e.g., turning on the room light).
* **OLED Burn-in Protection:** An automatic screensaver activates after 60 seconds of inactivity, shifting the clock display coordinates continuously to prevent pixel degradation.
* **Robust FreeRTOS Architecture:** Tasks are distributed across the ESP32's dual cores (Core 0 handles UI, Buttons, and I2C; Core 1 is dedicated to precise NeoPixel timing).
* **Hardware-Level Concurrency Protection:** Implements a `SemaphoreHandle_t` (Mutex) to strictly protect the DS3231 RTC I2C bus from concurrent access by overlapping tasks.
* **Rich LED Animations:** Includes 11 independent lighting patterns (Rainbow, Confetti, BPM, Solid Colors, etc.) utilizing the FastLED/NeoPixel logic.

## Hardware Requirements

* **Microcontroller:** ESP32 (e.g., NodeMCU-32S)
* **Display:** 0.96" OLED SSD1306 (SPI Interface)
* **RTC:** DS3231 Real-Time Clock module (I2C Interface)
* **Lighting:** WS2812B NeoPixel LED Strip (60 LEDs used)
* **Sensors:** LDR (Photoresistor) arranged in a voltage divider
* **Inputs:** 4x Push Buttons (Pull-up configuration)

## Software Architecture (FreeRTOS)

The system relies on 4 independent tasks managed by the FreeRTOS scheduler:

| Task Name | Core | Priority | Description |
| :--- | :---: | :---: | :--- |
| `taskDisplay` | 0 | 1 | Handles the SSD1306 UI, screensaver logic, and continuous LDR background polling. |
| `taskAlarm` | 0 | 1 | Periodically checks the RTC time against the alarm trigger using a single-fire daily lock. |
| `taskButton` | 0 | 3 | High-priority task with non-blocking debounce logic and long-press detection for UI navigation. |
| `taskLED` | 1 | 2 | Runs isolated on Core 1 to ensure smooth, uninterrupted 60FPS NeoPixel rendering. |

## Controls & Interface

The physical interface consists of 4 buttons with dual-action (Short Press / Long Press) capabilities:

* **Button 1:** Toggles global lighting modes (`OFF` > `DAY` > `NIGHT` > `AUTO`). Also serves as the "Stop Alarm" button during a sunrise event.
* **Button 2:** Cycles through the available LED patterns.
* **Button 3:** * *Long Press:* Enters/Exits Time Editing mode.
    * *Short Press:* Increments the currently selected time parameter (Hour > Min > Sec > Day > Month > Year).
* **Button 4:** * *Long Press:* Enters/Exits Alarm Editing mode.
    * *Short Press:* Increments the alarm parameter (Hour > Min).

*Note: Pressing any button while the screensaver is active will simply wake the display without executing its primary function.*

## Dependencies

This project requires the following libraries:
* `SPI.h` & `Wire.h` (Native Arduino)
* `RTClib` (Adafruit)
* `Adafruit_NeoPixel`
* `Adafruit_GFX`
* `Adafruit_SSD1306`

---
*Built to bridge the gap between physical hardware limitations and robust software architecture.*