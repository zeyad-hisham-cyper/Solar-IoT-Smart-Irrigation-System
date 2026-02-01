#ifndef TEMP_DS18B20_H
#define TEMP_DS18B20_H

/**
 * @file temp_ds18b20.h
 * @brief DS18B20 Temperature Sensor Driver (Wrapper HAL)
...
 */

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"
#include "driver/gpio.h"

/* Espressif components (dependencies) */
#include "onewire_bus.h"
#include "onewire_bus_impl_rmt.h"
#include "ds18b20.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                               VERSION INFO                                 */
/* ========================================================================== */

#define TEMP_DS18B20_HAL_VERSION_MAJOR    (1)
#define TEMP_DS18B20_HAL_VERSION_MINOR    (0)
#define TEMP_DS18B20_HAL_VERSION_PATCH    (0)

/* ========================================================================== */
/*                              TYPE DEFINITIONS                              */
/* ========================================================================== */

/**
 * @brief DS18B20 Resolution
 *
 * Note: The DS18B20 power-up default is 12-bit. This wrapper uses the selected
 * resolution mainly to compute the required conversion delay. If you want the
 * driver to also *configure* the DS18B20 resolution, confirm the component API
 * available in your dependency version.
 */
typedef enum {
    TEMP_DS18B20_RES_9_BIT  = 9,
    TEMP_DS18B20_RES_10_BIT = 10,
    TEMP_DS18B20_RES_11_BIT = 11,
    TEMP_DS18B20_RES_12_BIT = 12,
} Temp_DS18B20_Resolution_t;

/**
 * @brief DS18B20 reading result
 */
typedef struct {
    float temperature_c;              /**< Measured temperature in Celsius */
    bool  valid;                      /**< True if temperature is valid */
} Temp_DS18B20_Readings_t;

/**
 * @brief DS18B20 driver statistics (optional, but useful for debugging)
 */
typedef struct {
    uint32_t read_ok;
    uint32_t read_fail;
    uint32_t retries;
} Temp_DS18B20_Stats_t;

/**
 * @brief DS18B20 Driver Configuration Structure
 */
typedef struct {
    /* Handles (OUTPUT) */
    onewire_bus_handle_t        onewire_bus;      /**< [OUTPUT] 1-Wire bus handle */
    ds18b20_device_handle_t     ds18b20_dev;      /**< [OUTPUT] DS18B20 device handle */

    /* Hardware (INPUT) */
    gpio_num_t                  data_gpio;        /**< [INPUT] 1-Wire data GPIO */

    /* Bus setup (INPUT) */
    bool                        enable_internal_pullup; /**< [INPUT] Enable internal pull-up (use external pull-up preferred) */
    uint32_t                    max_rx_bytes;     /**< [INPUT] RMT max RX bytes (default 10 is usually enough) */

    /* Driver behavior (INPUT) */
    uint32_t                    max_retries;      /**< [INPUT] Max retries for a read cycle (default 3) */
    Temp_DS18B20_Resolution_t   resolution;       /**< [INPUT] Selected resolution (affects conversion delay) */

    /* Diagnostics (OUTPUT) */
    Temp_DS18B20_Stats_t        stats;            /**< [OUTPUT] Counters updated by the driver */
} Temp_DS18B20_Config_t;

/* ========================================================================== */
/*                             FUNCTION PROTOTYPES                            */
/* ========================================================================== */

/**
 * @brief Initialize DS18B20 wrapper driver
 *
 * Installs the 1-Wire bus (RMT backend), enumerates devices on the bus and
 * upgrades the first DS18B20 found to a ds18b20 handle.
 *
 * @param[in,out] config Pointer to configuration struct. Handles will be populated on success.
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: NULL config or invalid parameters
 *      - ESP_ERR_NOT_FOUND: No DS18B20 detected on the bus
 *      - ESP_FAIL: Bus install / enumeration failed
 */
esp_err_t Temp_DS18B20_Init(Temp_DS18B20_Config_t *config);

/**
 * @brief De-initialize DS18B20 wrapper driver
 *
 * Frees device and bus resources.
 *
 * @param[in,out] config Pointer to configuration struct
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: NULL config
 */
esp_err_t Temp_DS18B20_Deinit(Temp_DS18B20_Config_t *config);

/**
 * @brief Read temperature (blocking)
 *
 * Triggers a temperature conversion on the bus, waits based on the selected
 * resolution, then reads temperature.
 *
 * @param[in,out] config Pointer to configuration struct
 * @param[out] readings Pointer to store the temperature result
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: NULL pointers
 *      - ESP_ERR_INVALID_STATE: Driver not initialized
 *      - ESP_FAIL: Sensor read failed (after retries)
 */
esp_err_t Temp_DS18B20_Read(Temp_DS18B20_Config_t *config, Temp_DS18B20_Readings_t *readings);

/**
 * @brief Get driver statistics
 *
 * @param[in] config Pointer to configuration struct
 * @param[out] stats Pointer to stats struct to copy into
 * @return ESP_OK or ESP_ERR_INVALID_ARG
 */
esp_err_t Temp_DS18B20_GetStats(const Temp_DS18B20_Config_t *config, Temp_DS18B20_Stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* TEMP_DS18B20_H */
