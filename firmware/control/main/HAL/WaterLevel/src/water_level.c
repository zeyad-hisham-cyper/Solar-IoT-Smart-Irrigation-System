/**
 * @file water_level.c
 * @brief Implementation of the reservoir water-level ADC driver.
 *
 * @version 1.0.0
 */
#include "water_level.h"
#include "esp_log.h"

static const char *TAG = "WATER_LEVEL";

esp_err_t water_level_init(water_level_t *wl)
{
    if (wl == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (wl->samples == 0) {
        wl->samples = 16;
    }

    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id  = wl->adc_unit,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_cfg, &wl->adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(ret));
        return ret;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = wl->adc_atten,
        .bitwidth = wl->adc_bitwidth,
    };
    ret = adc_oneshot_config_channel(wl->adc_handle, wl->adc_channel, &chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "config_channel failed: %s", esp_err_to_name(ret));
        (void)adc_oneshot_del_unit(wl->adc_handle);
        wl->adc_handle = NULL;
        return ret;
    }

    wl->initialised = true;
    ESP_LOGI(TAG, "Initialised (unit=%d ch=%d, empty=%u full=%u)",
             (int)wl->adc_unit, (int)wl->adc_channel, wl->raw_empty, wl->raw_full);
    return ESP_OK;
}

esp_err_t water_level_read(water_level_t *wl, uint8_t *percent, uint16_t *raw_out)
{
    if (wl == NULL || !wl->initialised) {
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t sum = 0;
    int raw = 0;
    for (uint32_t i = 0; i < wl->samples; i++) {
        esp_err_t ret = adc_oneshot_read(wl->adc_handle, wl->adc_channel, &raw);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "ADC read failed: %s", esp_err_to_name(ret));
            return ret;
        }
        sum += (uint32_t)raw;
    }
    uint16_t avg = (uint16_t)(sum / wl->samples);
    if (raw_out != NULL) {
        *raw_out = avg;
    }

    /* Linear map between EMPTY and FULL references, supporting either polarity. */
    int32_t pct;
    if (wl->raw_full >= wl->raw_empty) {
        int32_t span = (int32_t)wl->raw_full - (int32_t)wl->raw_empty;
        pct = (span > 0) ? (((int32_t)avg - (int32_t)wl->raw_empty) * 100) / span : 0;
    } else {
        int32_t span = (int32_t)wl->raw_empty - (int32_t)wl->raw_full;
        pct = (span > 0) ? (((int32_t)wl->raw_empty - (int32_t)avg) * 100) / span : 0;
    }
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;

    if (percent != NULL) {
        *percent = (uint8_t)pct;
    }
    return ESP_OK;
}

esp_err_t water_level_deinit(water_level_t *wl)
{
    if (wl == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (wl->adc_handle != NULL) {
        (void)adc_oneshot_del_unit(wl->adc_handle);
        wl->adc_handle = NULL;
    }
    wl->initialised = false;
    return ESP_OK;
}
