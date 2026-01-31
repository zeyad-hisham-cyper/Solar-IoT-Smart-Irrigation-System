#ifndef SOIL_MOISTURE_H
#define SOIL_MOISTURE_H

/**
 * @file soil_moisture.h
 * @brief Soil Moisture Sensor Driver Header
 *
 * This module provides a Hardware Abstraction Layer (HAL) for interfacing with
 * analog soil moisture sensors on the ESP32. It handles ADC initialization,
 * calibration, and provides APIs for reading moisture levels.
 *
 * @version 1.1.0
 * @date 2024-05-20
 */

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "hal/adc_types.h"
#include "driver/gpio.h"
#include "driver/adc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                               VERSION INFO                                 */
/* ========================================================================== */

#define SOIL_MOISTURE_HAL_VERSION_MAJOR    (1)
#define SOIL_MOISTURE_HAL_VERSION_MINOR    (1)
#define SOIL_MOISTURE_HAL_VERSION_PATCH    (0)

/* ========================================================================== */
/*                              TYPE DEFINITIONS                              */
/* ========================================================================== */

/**
 * @brief Soil Moisture Classification States
 */
typedef enum {
    SOIL_STATE_DRY = 0,     /**< Soil moisture is low (Dry) */
    SOIL_STATE_NORMAL,      /**< Soil moisture is acceptable (Normal) */
    SOIL_STATE_WET          /**< Soil moisture is high (Wet) */
} Soil_Moisture_state_t;

/**
 * @brief Soil Moisture Reading Results
 */
typedef struct {
    uint8_t moisture_percentage;      /**< Calculated moisture percentage (0-100) */
    Soil_Moisture_state_t state;      /**< Classified state of the soil */
    uint16_t adc_reading;             /**< Raw ADC reading obtained from sensor */
} Soil_Moisture_Readings_t;

/**
 * @brief Soil Moisture Driver Configuration Structure
 */
typedef struct {
    adc_oneshot_unit_handle_t adc_handle;     /**< [OUTPUT] Handle for the initialized ADC unit */
    adc_unit_t adc_unit;                      /**< [INPUT] ADC Unit to use (e.g., ADC_UNIT_1) */
    adc_channel_t adc_channel;                /**< [INPUT] ADC Channel connected to sensor */
    adc_atten_t adc_attenuation;              /**< [INPUT] ADC Attenuation (recommended ADC_ATTEN_DB_11/12) */
    adc_bits_width_t adc_bitwidth;            /**< [INPUT] ADC Bit width (recommended ADC_BITWIDTH_12) */
    adc_cali_handle_t adc_cali_handle;        /**< [OUTPUT] Handle for ADC calibration scheme */
    gpio_num_t power_gpio;                    /**< [INPUT] GPIO to power the sensor (GPIO_NUM_NC if unused) */
    gpio_num_t calibration_button_gpio;       /**< [INPUT] GPIO for user calibration button */
    uint32_t read_delay_ms;                   /**< [INPUT] Stabilization delay (ms) after powering on sensor */
    uint32_t adc_samples;                     /**< [INPUT] Number of samples to average per read (default: 64) */
} Soil_Moisture_Config_t;

/* ========================================================================== */
/*                             FUNCTION PROTOTYPES                            */
/* ========================================================================== */

/**
 * @brief Initialize the Soil Moisture Driver
 *
 * Initializes the ADC unit, configures the channel, sets up power GPIOs,
 * and prepares the calibration scheme.
 *
 * @param[in,out] config Pointer to the configuration structure. Handles will be populated on success.
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: NULL config or invalid parameters
 *      - ESP_FAIL: Driver initialization failed
 */
esp_err_t Soil_Moisture_Init(Soil_Moisture_Config_t *config);

/**
 * @brief De-initialize the Soil Moisture Driver
 *
 * Frees ADC resources and handles.
 *
 * @param[in,out] config Pointer to the configuration structure. Handles will be NULLed.
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: NULL config
 */
esp_err_t Soil_Moisture_Deinit(Soil_Moisture_Config_t *config);

/**
 * @brief Read Moisture Level
 *
 * Powers on the sensor (if configured), takes multiple samples, averages them,
 * applies calibration, and returns the moisture percentage and state.
 *
 * @param[in] config Pointer to the configuration structure
 * @param[out] readings Pointer to the structure to store results
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: NULL pointers
 *      - ESP_FAIL: ADC read failed
 */
esp_err_t Soil_Moisture_Read(Soil_Moisture_Config_t *config, Soil_Moisture_Readings_t *readings);

/**
 * @brief Perform User Calibration interactively
 *
 * Enters a blocking routine that waits for user input (via button) to measure
 * Wet and Dry states, then saves these values to NVS.
 *
 * @param[in] config Pointer to the configuration structure
 * @return
 *      - ESP_OK: Calibration successful and saved
 *      - ESP_ERR_INVALID_ARG: NULL config
 *      - ESP_ERR_INVALID_STATE: Logic error (Dry <= Wet)
 *      - ESP_FAIL: Hardware or NVS error
 */
esp_err_t Soil_Moisture_UserCalibrate(Soil_Moisture_Config_t *config);


#ifdef __cplusplus
}
#endif

#endif // SOIL_MOISTURE_H
