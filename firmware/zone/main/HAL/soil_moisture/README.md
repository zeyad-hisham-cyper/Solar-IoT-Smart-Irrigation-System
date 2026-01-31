# Soil Moisture Sensor HAL

This module provides a Hardware Abstraction Layer (HAL) for interfacing with analog soil moisture sensors on the ESP32. It handles ADC initialization, calibration, and provides APIs for reading moisture levels and states.

## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Configuration](#configuration)
- [Usage](#usage)
  - [Initialization](#initialization)
  - [Reading Moisture](#reading-moisture)
  - [Calibration](#calibration)
- [API Reference](#api-reference)

## Overview

The Soil Moisture Sensor HAL simplifies the process of integrating analog soil moisture sensors. It abstracts away the low-level ADC details and provides a clean interface for application logic. It supports saving calibration data (dry/wet values) to NVS (Non-Volatile Storage) to ensure accurate readings across reboots.

## Features

- **ADC Oneshot Mode**: Uses the ESP-IDF ADC Oneshot driver for reliable readings.
- **Power Control**: Optional control of a GPIO pin to power the sensor only when reading, saving energy.
- **Calibration**:
    - Supports user-initiated calibration.
    - Stores "Dry" and "Wet" ADC values in NVS.
    - Automatically maps raw ADC values to 0-100% moisture.
- **State Classification**: Classifies moisture into `DRY`, `NORMAL`, and `WET` states.
- **Noise Reduction**: Implements simple averaging of multiple samples per read.

## Configuration

The module is configured using the `Soil_Moisture_Config_t` structure:

```c
typedef struct {
    adc_oneshot_unit_handle_t adc_handle;     // (Output) Handle for the created ADC unit
    adc_unit_t adc_unit;                      // ADC Unit (e.g., ADC_UNIT_1)
    adc_channel_t adc_channel;                // ADC Channel (e.g., ADC_CHANNEL_6)
    adc_atten_t adc_attenuation;              // Attenuation (e.g., ADC_ATTEN_DB_11)
    adc_bits_width_t adc_bitwidth;            // Bit width (e.g., ADC_BITWIDTH_12)
    adc_cali_handle_t adc_cali_handle;        // (Output) Handle for calibration scheme
    gpio_num_t power_gpio;                    // GPIO for sensor power (or GPIO_NUM_NC if unused)
    gpio_num_t calibration_button_gpio;       // GPIO for calibration button
    uint32_t read_delay_ms;                   // Stabilization delay after powering on
} Soil_Moisture_Config_t;
```

## Usage

### Initialization

Initialize the driver by passing a configuration structure:

```c
#include "soil_moisture.h"

Soil_Moisture_Config_t soil_config = {
    .adc_unit = ADC_UNIT_1,
    .adc_channel = ADC_CHANNEL_6,
    .adc_attenuation = ADC_ATTEN_DB_11,
    .adc_bitwidth = ADC_BITWIDTH_12,
    .power_gpio = GPIO_NUM_2,      // Example power pin
    .calibration_button_gpio = GPIO_NUM_0, // Example button
    .read_delay_ms = 50
};

esp_err_t err = Soil_Moisture_Init(&soil_config);
if (err != ESP_OK) {
    // Handle error
}
```

### Reading Moisture

Read the current moisture level and state:

```c
Soil_Moisture_Readings_t readings;
err = Soil_Moisture_Read(&soil_config, &readings);

if (err == ESP_OK) {
    printf("Moisture: %d%%\n", readings.moisture_percentage);
    printf("Raw ADC: %d\n", readings.adc_reading);
    
    switch (readings.state) {
        case SOIL_STATE_DRY:
            printf("State: Dry\n");
            break;
        case SOIL_STATE_NORMAL:
            printf("State: Normal\n");
            break;
        case SOIL_STATE_WET:
            printf("State: Wet\n");
            break;
    }
}
```

### Calibration

Trigger the user calibration process. This typically requires user interaction (pressing a button when the sensor is dry, then when wet).

```c
// This function blocks until calibration is complete
err = Soil_Moisture_UserCalibrate(&soil_config);
if (err == ESP_OK) {
    printf("Calibration successful!\n");
}
```

## API Reference

See `soil_moisture.h` for the full API documentation.
