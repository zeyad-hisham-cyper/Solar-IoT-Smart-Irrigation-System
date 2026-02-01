# DS18B20 Temperature HAL (Wrapper)

This module provides a lightweight, project-style HAL wrapper for the **DS18B20** 1-Wire temperature sensor on **ESP32**.

Internally, it uses Espressif-managed components:
- `espressif/onewire_bus` (RMT backend) for 1-Wire timing
- `espressif/ds18b20` for the sensor protocol

So your project benefits from Espressif-maintained low-level timing, while your application code sees a clean, consistent API.

## Why this approach?

- **Reliability**: 1-Wire timing is tricky; the RMT-based driver handles it robustly.
- **Maintainability**: You keep your own API style, error handling, naming, logging, and configurability.
- **Future-proofing**: If Espressif fixes bugs or improves performance, you get it via component updates.

## Dependencies

Add these dependencies to your project (via IDF Component Manager):

```bash
idf.py add-dependency "espressif/onewire_bus^1.0.4"
idf.py add-dependency "espressif/ds18b20^0.2.0"
```

(See Espressif component registry pages for versions and notes.)

## Usage

### Configuration

```c
#include "temp_ds18b20.h"

Temp_DS18B20_Config_t temp_cfg = {
    .data_gpio = GPIO_NUM_18,
    .enable_internal_pullup = true,  // external 4.7k is still recommended
    .max_rx_bytes = 10,
    .max_retries = 3,
    .resolution = TEMP_DS18B20_RES_12_BIT,
};
```

### Init

```c
ESP_ERROR_CHECK(Temp_DS18B20_Init(&temp_cfg));
```

### Read temperature

```c
Temp_DS18B20_Readings_t r;
if (Temp_DS18B20_Read(&temp_cfg, &r) == ESP_OK && r.valid) {
    printf("Temperature: %.2f C\n", r.temperature_c);
}
```

### Deinit

```c
ESP_ERROR_CHECK(Temp_DS18B20_Deinit(&temp_cfg));
```

## Notes

- **Pull-up resistor**: Use an external ~4.7k pull-up resistor from DQ to 3.3V. The internal pull-up can help during bring-up but is not a replacement in all setups.
- **Conversion time**: Max conversion time depends on resolution (up to ~750ms at 12-bit). This wrapper uses a conservative delay based on the DS18B20 datasheet.
- **Single sensor**: This wrapper enumerates the first DS18B20 on the bus. If you later put multiple sensors on one pin, you can extend the enumeration logic to keep an array of handles.
