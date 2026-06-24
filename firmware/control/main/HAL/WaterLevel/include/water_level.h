/**
 * @file water_level.h
 * @brief Reservoir water-level sensor HAL driver (ADC) for the Control board.
 *
 * Reads an analog water-level sensor on an ADC1 input, oversamples for noise
 * reduction, and maps the raw reading to a 0–100 % level using calibrated
 * EMPTY/FULL references. Supports both sensor polarities (rising or falling
 * voltage with level). The application uses this to inhibit the pump when the
 * tank is dry (dry-run protection).
 *
 * @version 1.0.0
 */
#ifndef WATER_LEVEL_H
#define WATER_LEVEL_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"
#include "hal/adc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Water-level sensor configuration / handle.
 */
typedef struct {
    /* -- Configuration [IN] -- */
    adc_unit_t      adc_unit;       /**< ADC unit (e.g. ADC_UNIT_1)            */
    adc_channel_t   adc_channel;    /**< ADC channel (e.g. ADC_CHANNEL_6/GPIO34)*/
    adc_atten_t     adc_atten;      /**< Attenuation                            */
    adc_bitwidth_t  adc_bitwidth;   /**< Bit width                              */
    uint32_t        samples;        /**< Samples to average (>=1)               */
    uint16_t        raw_empty;      /**< Raw ADC at empty tank (0 %)            */
    uint16_t        raw_full;       /**< Raw ADC at full tank (100 %)          */

    /* -- Internal [managed] -- */
    adc_oneshot_unit_handle_t adc_handle;
    bool            initialised;
} water_level_t;

/**
 * @brief Initialise the water-level ADC channel.
 * @param[in,out] wl  Handle with configuration fields populated.
 * @return ESP_OK on success, or an ESP-IDF error code.
 */
esp_err_t water_level_init(water_level_t *wl);

/**
 * @brief Read the water level as a percentage (0–100 %).
 *
 * @param[in]  wl       Initialised handle.
 * @param[out] percent  Resulting level 0–100 (clamped). May be NULL.
 * @param[out] raw_out  Raw averaged ADC value. May be NULL.
 * @return ESP_OK on success; an error code if the ADC read failed (in which
 *         case @p percent is left unmodified — caller should keep last value).
 */
esp_err_t water_level_read(water_level_t *wl, uint8_t *percent, uint16_t *raw_out);

/** @brief De-initialise and free ADC resources. */
esp_err_t water_level_deinit(water_level_t *wl);

#ifdef __cplusplus
}
#endif

#endif /* WATER_LEVEL_H */
