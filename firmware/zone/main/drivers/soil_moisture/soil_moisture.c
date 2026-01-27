#include "soil_moisture.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "soil_moisture";

#define SAMPLES_MIN 1
#define SAMPLES_MAX 64

static uint8_t clamp_u8(uint8_t v, uint8_t lo, uint8_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static uint16_t clamp_u16(uint16_t v, uint16_t lo, uint16_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

esp_err_t soil_moisture_init(soil_moisture_t *dev,
                            adc_channel_t channel,
                            adc_atten_t atten,
                            uint8_t samples)
{
    if (!dev) return ESP_ERR_INVALID_ARG;

    memset(dev, 0, sizeof(*dev));
    dev->channel = channel;
    dev->atten = atten;
    dev->samples = clamp_u8(samples, SAMPLES_MIN, SAMPLES_MAX);
    dev->calibrated = false;
    dev->inverted = false;

    // Create ADC oneshot unit (ADC1 only)
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    esp_err_t err = adc_oneshot_new_unit(&init_cfg, &dev->oneshot_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(err));
        return err;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = dev->atten,
        .bitwidth = ADC_BITWIDTH_12,
    };

    err = adc_oneshot_config_channel(dev->oneshot_handle, dev->channel, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel failed: %s", esp_err_to_name(err));
        adc_oneshot_del_unit(dev->oneshot_handle);
        dev->oneshot_handle = NULL;
        return err;
    }

    dev->oneshot_inited = true;
    return ESP_OK;
}

esp_err_t soil_moisture_deinit(soil_moisture_t *dev)
{
    if (!dev) return ESP_ERR_INVALID_ARG;

    if (dev->oneshot_inited && dev->oneshot_handle) {
        esp_err_t err = adc_oneshot_del_unit(dev->oneshot_handle);
        dev->oneshot_handle = NULL;
        dev->oneshot_inited = false;
        return err;
    }

    return ESP_OK;
}

esp_err_t soil_moisture_set_calibration(soil_moisture_t *dev,
                                       uint16_t raw_dry,
                                       uint16_t raw_wet,
                                       bool inverted)
{
    if (!dev) return ESP_ERR_INVALID_ARG;
    if (raw_dry == raw_wet) return ESP_ERR_INVALID_ARG;

    dev->raw_dry = raw_dry;
    dev->raw_wet = raw_wet;
    dev->inverted = inverted;
    dev->calibrated = true;
    return ESP_OK;
}

static esp_err_t read_raw_once(soil_moisture_t *dev, int *out_raw)
{
    if (!dev || !out_raw) return ESP_ERR_INVALID_ARG;
    if (!dev->oneshot_inited || !dev->oneshot_handle) return ESP_ERR_INVALID_STATE;

    int raw = 0;
    esp_err_t err = adc_oneshot_read(dev->oneshot_handle, dev->channel, &raw);
    if (err != ESP_OK) return err;

    // clamp to 12-bit range
    if (raw < 0) raw = 0;
    if (raw > 4095) raw = 4095;

    *out_raw = raw;
    return ESP_OK;
}

esp_err_t soil_moisture_read_raw(soil_moisture_t *dev, uint16_t *out_raw)
{
    if (!dev || !out_raw) return ESP_ERR_INVALID_ARG;

    const uint8_t n = clamp_u8(dev->samples, SAMPLES_MIN, SAMPLES_MAX);

    uint32_t sum = 0;
    for (uint8_t i = 0; i < n; i++) {
        int raw = 0;
        esp_err_t err = read_raw_once(dev, &raw);
        if (err != ESP_OK) return err;
        sum += (uint32_t)raw;
    }

    *out_raw = (uint16_t)(sum / n);
    return ESP_OK;
}

esp_err_t soil_moisture_read_percentage(soil_moisture_t *dev, uint8_t *out_pct)
{
    if (!dev || !out_pct) return ESP_ERR_INVALID_ARG;
    if (!dev->calibrated) return ESP_ERR_INVALID_STATE;

    uint16_t raw = 0;
    esp_err_t err = soil_moisture_read_raw(dev, &raw);
    if (err != ESP_OK) return err;

    // clamp raw between calibration endpoints
    uint16_t lo = (dev->raw_dry < dev->raw_wet) ? dev->raw_dry : dev->raw_wet;
    uint16_t hi = (dev->raw_dry < dev->raw_wet) ? dev->raw_wet : dev->raw_dry;
    raw = clamp_u16(raw, lo, hi);

    // Compute percent from dry->wet (works even if raw_wet < raw_dry)
    int32_t numerator = (int32_t)raw - (int32_t)dev->raw_dry;
    int32_t denom     = (int32_t)dev->raw_wet - (int32_t)dev->raw_dry;

    int32_t pct = 0;
    if (denom != 0) {
        pct = (numerator * 100) / denom;
    }

    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    if (dev->inverted) pct = 100 - pct;

    *out_pct = (uint8_t)pct;
    return ESP_OK;
}
