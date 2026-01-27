#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "hal/adc_types.h"          // adc_atten_t, adc_channel_t, adc_unit_t
#include "esp_adc/adc_oneshot.h"    // oneshot driver

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    adc_channel_t channel;
    adc_atten_t atten;
    uint8_t samples;     // averaging samples (1..64)

    // calibration endpoints
    uint16_t raw_dry;
    uint16_t raw_wet;
    bool calibrated;
    bool inverted;

    // oneshot
    adc_oneshot_unit_handle_t oneshot_handle;
    bool oneshot_inited;
} soil_moisture_t;

/**
 * Initialize soil moisture sensor using ADC_UNIT_1 (recommended with Wi-Fi).
 *
 * @param dev      sensor handle
 * @param channel  ADC channel (ADC_CHANNEL_0..)
 * @param atten    attenuation (use ADC_ATTEN_DB_12 instead of deprecated DB_11)
 * @param samples  averaging samples (1..64)
 */
esp_err_t soil_moisture_init(soil_moisture_t *dev,
                            adc_channel_t channel,
                            adc_atten_t atten,
                            uint8_t samples);

esp_err_t soil_moisture_deinit(soil_moisture_t *dev);

esp_err_t soil_moisture_set_calibration(soil_moisture_t *dev,
                                       uint16_t raw_dry,
                                       uint16_t raw_wet,
                                       bool inverted);

esp_err_t soil_moisture_read_raw(soil_moisture_t *dev, uint16_t *out_raw);

esp_err_t soil_moisture_read_percentage(soil_moisture_t *dev, uint8_t *out_pct);

#ifdef __cplusplus
}
#endif
