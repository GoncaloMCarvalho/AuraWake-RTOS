# AuraWake-RTOS: Smart Alarm & Sunrise Simulator

## Overview
AuraWake is an advanced, multi-tasking smart alarm clock built on the ESP32. Leveraging the FreeRTOS real-time operating system, it seamlessly manages hardware peripherals (RTC, SPI OLED display) and a WS2812B NeoPixel strip to provide a gradual, customizable sunrise simulation alongside ambient light monitoring.

## Key Features
- **FreeRTOS Multitasking:** Efficiently divides workloads across the ESP32's dual cores (Display, LED animations, Button debouncing, and Alarm logic run independently).
- **Sunrise Simulation:** A 10-minute gradual wake-up sequence that transitions the LED strip from a deep, dim orange to a bright white, mimicking a natural sunrise.
- **Dynamic Ambient Lighting:** Utilizes an LDR (photoresistor) to actively measure room brightness and automatically adjust the LED strip intensity in `AUTO` mode.
- **Custom LED Engine:** Features 11 unique LED patterns (Rainbow, Confetti, Juggle, Solid colors, etc.) rendering at a smooth 60 FPS.
- **Hardware UI:** A robust 4-button interface with custom debouncing logic and dual-action states (short press vs. long press) for on-the-fly time and alarm configuration without menus.

## Software Architecture (Tasks)
The system utilizes `xTaskCreatePinnedToCore` to ensure smooth LED animations without blocking the user interface:
- `taskDisplay` (Core 0): Refreshes the OLED UI at 10Hz, handling blinking cursors during edit modes and real-time sensor updates.
- `taskButton` (Core 0): Polls GPIO states, handling debouncing timers and triggering state-machine updates.
- `taskAlarm` (Core 0): Monitors the DS3231 RTC at 1Hz to trigger the sunrise event.
- `taskLED` (Core 1): Dedicated strictly to computing math-heavy LED patterns and driving the NeoPixel data line, ensuring zero animation stutter.

## Hardware Setup & Pinout
- **Microcontroller:** ESP32 Dev Module
- **Timekeeping:** DS3231 RTC (I2C: SDA=21, SCL=22)
- **Display:** 128x64 SSD1306 OLED (SPI Interface)
- **Lighting:** WS2812B LED Strip (60 LEDs)

| Component | ESP32 Pin | Description |
| :--- | :--- | :--- |
| **OLED MOSI** | `GPIO 23` | SPI Data |
| **OLED SCK** | `GPIO 18` | SPI Clock |
| **OLED CS** | `GPIO 5` | SPI Chip Select |
| **OLED DC** | `GPIO 19` | SPI Data/Command |
| **OLED RES** | `GPIO 4` | SPI Reset |
| **NeoPixel** | `GPIO 27` | LED Strip Data Line |
| **LDR Sensor**| `GPIO 34` | Analog Light Sensor |
| **Button 1** | `GPIO 26` | Light Mode / Stop Alarm |
| **Button 2** | `GPIO 32` | Change LED Pattern |
| **Button 3** | `GPIO 33` | Edit Time (Long Press) / Increment Editing Time Field |
| **Button 4** | `GPIO 25` | Edit Alarm (Long Press) / Increment Editing Alarm Field |