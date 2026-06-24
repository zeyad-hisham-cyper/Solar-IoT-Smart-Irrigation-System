#ifndef SOIL_MOISTURE_PRIVATE_H
#define SOIL_MOISTURE_PRIVATE_H

#include "soil_moisture.h"


#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_check.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                           INTERNAL CONSTANTS                               */
/* ========================================================================== */

#define SOIL_MOISTURE_ADC_MAX_VALUE   4095    /**< 2^12 - 1 for 12-bit ADC */
#define SOIL_MOISTURE_VOLTAGE_REF     3300    /**< in millivolts (fallback if no calibration) */

/* NVS Usage */
#define SOIL_NVS_NAMESPACE        "soil_moisture"
#define SOIL_NVS_KEY_ADC_DRY      "adc_dry"
#define SOIL_NVS_KEY_ADC_WET      "adc_wet"

/* Calibration validation */
#define SOIL_CAL_MIN_ADC_DIFF     200     /**< Min acceptable difference between dry & wet ADC */
#define SOIL_CAL_NVS_MAGIC_KEY    "cal_ok" /**< NVS key written alongside calibration data      */
#define SOIL_CAL_NVS_MAGIC_VAL    0xCAU    /**< Expected value — proves calibration is intact    */
#define SOIL_CAL_ADC_MAX          4095     /**< Maximum plausible 12-bit ADC reading             */

/* Hardware Consts */
#define CAL_BUTTON_ACTIVE_LEVEL   0
#define CAL_DEBOUNCE_MS           50       /**< Increased debounce for external button           */
#define CAL_POLL_MS               10
#define CAL_LONG_PRESS_MS         3000     /**< Hold >3 s to enter calibration mode              */

#define OUTPUT_HIGH 1
#define OUTPUT_LOW  0

/* ========================================================================== */
/*                        INTERNAL FUNCTION PROTOTYPES                        */
/* ========================================================================== */

/**
 * @brief Read raw ADC value (internal helper)
 * @param[in] config Driver configuration
 * @param[out] adc_reading Pointer to store the averaged reading
 * @return ESP_OK or error code
 */
esp_err_t soil_moisture_read_adc(const Soil_Moisture_Config_t *config, uint16_t *adc_reading);

/**
 * @brief Calculate moisture percentage and state based on calibration
 * @param[in] config Driver configuration
 * @param[in] adc_reading Raw ADC value
 * @param[out] readings Pointer to result structure
 * @return ESP_OK or error code
 */
esp_err_t soil_moisture_calculate_moisture(const Soil_Moisture_Config_t *config,
                                           uint16_t adc_reading,
                                           Soil_Moisture_Readings_t *readings);

/**
 * @brief Setup calibration scheme
 * @param[in,out] config Driver configuration
 * @return ESP_OK or error code
 */
esp_err_t soil_moisture_calibrate_adc(Soil_Moisture_Config_t *config);

#ifdef __cplusplus
}
#endif

#endif // SOIL_MOISTURE_PRIVATE_H
