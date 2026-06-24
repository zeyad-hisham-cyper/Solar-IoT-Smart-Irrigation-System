#include "soil_moisture_private.h"

static const char *TAG = "SOIL_MOISTURE_SENSOR";

/* ========================================================================== */
/*                              LOCAL UTILITIES                               */
/* ========================================================================== */

/**
 * @brief Check if a valid power GPIO is configured
 * @param[in] config
 * @return true if valid, false otherwise
 */
static bool soil_moisture_has_power_gpio(const Soil_Moisture_Config_t *config)
{
    bool valid = false;
    if (config != NULL)
    {
        if ((config->power_gpio >= 0) && (config->power_gpio != GPIO_NUM_NC))
        {
            valid = true;
        }
    }
    return valid;
}

/**
 * @brief Power on the sensor
 * @param[in] config
 * @return ESP_OK
 */
static esp_err_t soil_moisture_power_on(const Soil_Moisture_Config_t *config)
{
    /* If no power GPIO is used (always on), do nothing but return OK */
    if (!soil_moisture_has_power_gpio(config))
    {
        return ESP_OK;
    }

    /* Set GPIO high to power structure */
    (void)gpio_set_level(config->power_gpio, OUTPUT_HIGH);

    /* Wait for sensor to stabilize */
    if (config->read_delay_ms > 0U)
    {
        vTaskDelay(pdMS_TO_TICKS(config->read_delay_ms));
    }
    return ESP_OK;
}

/**
 * @brief Power off the sensor to save energy
 * @param[in] config
 * @return ESP_OK
 */
static esp_err_t soil_moisture_power_off(const Soil_Moisture_Config_t *config)
{
    if (!soil_moisture_has_power_gpio(config))
    {
        return ESP_OK;
    }

    (void)gpio_set_level(config->power_gpio, OUTPUT_LOW);
    return ESP_OK;
}

/* ========================================================================== */
/*                        NVS CALIBRATION STORAGE                             */
/* ========================================================================== */

/**
 * @brief Save calibration values to NVS with a magic integrity marker.
 * @param[in] dry Raw ADC value for dry soil
 * @param[in] wet Raw ADC value for wet soil
 * @return ESP_OK or NVS error
 */
static esp_err_t soil_nvs_save_calibration(uint16_t dry, uint16_t wet)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(SOIL_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS open for save failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_u32(h, SOIL_NVS_KEY_ADC_DRY, (uint32_t)dry);
    if (ret == ESP_OK)
    {
        ret = nvs_set_u32(h, SOIL_NVS_KEY_ADC_WET, (uint32_t)wet);
    }
    if (ret == ESP_OK)
    {
        /* Write magic marker to prove calibration data is intact */
        ret = nvs_set_u8(h, SOIL_CAL_NVS_MAGIC_KEY, SOIL_CAL_NVS_MAGIC_VAL);
    }
    if (ret == ESP_OK)
    {
        ret = nvs_commit(h);
    }

    nvs_close(h);

    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Calibration saved successfully: dry_ADC=%u, wet_ADC=%u, magic=0x%02X",
                 (unsigned)dry, (unsigned)wet, SOIL_CAL_NVS_MAGIC_VAL);
    }
    else
    {
        ESP_LOGE(TAG, "Calibration save FAILED: %s", esp_err_to_name(ret));
    }

    return ret;
}

/**
 * @brief Load calibration values from NVS, validating the magic marker and
 *        plausibility of the stored values.
 * @param[out] dry Pointer to store dry value
 * @param[out] wet Pointer to store wet value
 * @return ESP_OK only if data is present, intact, and plausible
 */
static esp_err_t soil_nvs_load_calibration(uint16_t *dry, uint16_t *wet)
{
    if ((dry == NULL) || (wet == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t h;
    esp_err_t ret = nvs_open(SOIL_NVS_NAMESPACE, NVS_READONLY, &h);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "NVS open for calibration load failed: %s (namespace may not exist yet)",
                 esp_err_to_name(ret));
        return ret;
    }

    /* Check magic marker first — if absent or wrong, treat as no calibration */
    uint8_t magic = 0;
    ret = nvs_get_u8(h, SOIL_CAL_NVS_MAGIC_KEY, &magic);
    if (ret != ESP_OK || magic != SOIL_CAL_NVS_MAGIC_VAL)
    {
        ESP_LOGW(TAG, "Calibration magic check FAILED (ret=%s, magic=0x%02X, expected=0x%02X) "
                      "— treating as uncalibrated",
                 esp_err_to_name(ret), (unsigned)magic, SOIL_CAL_NVS_MAGIC_VAL);
        nvs_close(h);
        return ESP_ERR_NOT_FOUND;
    }

    uint32_t dry_val = 0;
    uint32_t wet_val = 0;

    ret = nvs_get_u32(h, SOIL_NVS_KEY_ADC_DRY, &dry_val);
    if (ret == ESP_OK)
    {
        ret = nvs_get_u32(h, SOIL_NVS_KEY_ADC_WET, &wet_val);
    }

    nvs_close(h);

    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Calibration keys missing in NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Plausibility checks */
    if (dry_val > SOIL_CAL_ADC_MAX || wet_val > SOIL_CAL_ADC_MAX)
    {
        ESP_LOGE(TAG, "Calibration CORRUPTED: dry=%lu, wet=%lu (max=%d) — rejecting",
                 (unsigned long)dry_val, (unsigned long)wet_val, SOIL_CAL_ADC_MAX);
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t diff = (dry_val > wet_val) ? (dry_val - wet_val) : (wet_val - dry_val);
    if (diff < SOIL_CAL_MIN_ADC_DIFF)
    {
        ESP_LOGE(TAG, "Calibration INVALID: dry=%lu, wet=%lu, diff=%lu (min=%d) — rejecting",
                 (unsigned long)dry_val, (unsigned long)wet_val,
                 (unsigned long)diff, SOIL_CAL_MIN_ADC_DIFF);
        return ESP_ERR_INVALID_STATE;
    }

    *dry = (uint16_t)dry_val;
    *wet = (uint16_t)wet_val;

    ESP_LOGI(TAG, "Calibration loaded: dry_ADC=%u, wet_ADC=%u, diff=%lu ✓",
             (unsigned)*dry, (unsigned)*wet, (unsigned long)diff);
    return ESP_OK;
}

/**
 * @brief Wait for button press with debounce (short press).
 * @param[in] button_gpio
 * @return ESP_OK or error
 */
static esp_err_t soil_wait_for_button_press(gpio_num_t button_gpio)
{
    if ((button_gpio < 0) || (button_gpio == GPIO_NUM_NC))
    {
        return ESP_ERR_INVALID_ARG;
    }

    while (true)
    {
        int level = gpio_get_level(button_gpio);
        if (level == CAL_BUTTON_ACTIVE_LEVEL)
        {
            vTaskDelay(pdMS_TO_TICKS(CAL_DEBOUNCE_MS));
            level = gpio_get_level(button_gpio);
            if (level == CAL_BUTTON_ACTIVE_LEVEL)
            {
                /* Wait for release */
                while (gpio_get_level(button_gpio) == CAL_BUTTON_ACTIVE_LEVEL)
                {
                    vTaskDelay(pdMS_TO_TICKS(CAL_POLL_MS));
                }
                vTaskDelay(pdMS_TO_TICKS(CAL_DEBOUNCE_MS));
                return ESP_OK;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(CAL_POLL_MS));
    }
}

/* ========================================================================== */
/*                         INTERNAL FUNCTION DEFINITIONS                      */
/* ========================================================================== */

esp_err_t Soil_Moisture_Init(Soil_Moisture_Config_t *config)
{
    if (config == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK;

    if (soil_moisture_has_power_gpio(config))
    {
        gpio_config_t power_io_cfg = {
            .pin_bit_mask = (1ULL << (uint32_t)config->power_gpio),
            .mode = GPIO_MODE_OUTPUT,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        ret = gpio_config(&power_io_cfg);
        if (ret != ESP_OK)
        {
            return ret;
        }
        (void)gpio_set_level(config->power_gpio, OUTPUT_LOW);
    }

    /* Configure Button if valid.
     * Note: GPIO 34-39 on ESP32 are input-only and have NO internal pull-up.
     * For those pins, rely on external pull-up resistor — we disable the
     * internal pull-up unconditionally since it's either a no-op (34-39)
     * or the external hardware already provides one. */
    if ((config->calibration_button_gpio >= 0) && (config->calibration_button_gpio != GPIO_NUM_NC))
    {
        bool is_input_only = (config->calibration_button_gpio >= 34 &&
                              config->calibration_button_gpio <= 39);
        gpio_config_t button_io_cfg = {
            .pin_bit_mask = (1ULL << (uint32_t)config->calibration_button_gpio),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en   = is_input_only ? GPIO_PULLUP_DISABLE : GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        ret = gpio_config(&button_io_cfg);
        if (ret != ESP_OK)
        {
            return ret;
        }
        ESP_LOGI(TAG, "Calibration button configured on GPIO %d (input-only=%s, ext pull-up expected)",
                 config->calibration_button_gpio, is_input_only ? "YES" : "NO");
    }

    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = config->adc_unit,
        .ulp_mode = ADC_ULP_MODE_DISABLE
    };

    ret = adc_oneshot_new_unit(&init_cfg, &(config->adc_handle));
    if (ret != ESP_OK)
    {
        return ret;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = config->adc_attenuation,
        .bitwidth = config->adc_bitwidth,
    };

    ret = adc_oneshot_config_channel(config->adc_handle, config->adc_channel, &chan_cfg);
    if (ret != ESP_OK)
    {
        (void)adc_oneshot_del_unit(config->adc_handle);
        return ret;
    }

    ret = soil_moisture_calibrate_adc(config);

    /* On boot, attempt to load existing calibration and report status */
    {
        uint16_t boot_dry = 0, boot_wet = 0;
        esp_err_t cal_ret = soil_nvs_load_calibration(&boot_dry, &boot_wet);
        if (cal_ret == ESP_OK)
        {
            ESP_LOGI(TAG, "==== BOOT: Calibration PRESENT — dry=%u, wet=%u ====",
                     (unsigned)boot_dry, (unsigned)boot_wet);
        }
        else
        {
            ESP_LOGW(TAG, "==== BOOT: NO valid calibration in NVS — sensor reads will be INVALID until calibrated ====");
        }
    }

    return ret;
}

esp_err_t soil_moisture_calibrate_adc(Soil_Moisture_Config_t *config)
{
    adc_cali_line_fitting_config_t calibration_cfg = {
        .atten = config->adc_attenuation,
        .bitwidth = config->adc_bitwidth,
        .unit_id = config->adc_unit,
        .default_vref = 0,
    };
    return adc_cali_create_scheme_line_fitting(&calibration_cfg, &(config->adc_cali_handle));
}

esp_err_t Soil_Moisture_Deinit(Soil_Moisture_Config_t *config)
{
    if (config == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (config->adc_cali_handle != NULL)
    {
        (void)adc_cali_delete_scheme_line_fitting(config->adc_cali_handle);
    }

    if (config->adc_handle != NULL)
    {
        (void)adc_oneshot_del_unit(config->adc_handle);
        config->adc_handle = NULL;
    }

    return soil_moisture_power_off(config);
}

esp_err_t soil_moisture_read_adc(const Soil_Moisture_Config_t *config, uint16_t *adc_reading)
{
    if ((config == NULL) || (adc_reading == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->adc_handle == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    int raw = 0;
    uint32_t sum = 0;
    esp_err_t ret = ESP_OK;

    uint32_t samples = (config->adc_samples > 0) ? config->adc_samples : 64;

    /* Oversampling */
    for (uint32_t i = 0; i < samples; i++)
    {
        ret = adc_oneshot_read(config->adc_handle, config->adc_channel, &raw);
        if (ret != ESP_OK)
        {
            return ret;
        }
        sum += (uint32_t)raw;
        raw = 0;
    }

    *adc_reading = (uint16_t)(sum / samples);
    return ESP_OK;
}

esp_err_t Soil_Moisture_Read(Soil_Moisture_Config_t *config, Soil_Moisture_Readings_t *readings)
{
    if ((config == NULL) || (readings == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = soil_moisture_power_on(config);
    if (ret != ESP_OK)
    {
        return ret;
    }

    uint16_t adc_reading = 0;
    ret = soil_moisture_read_adc(config, &adc_reading);
    if (ret != ESP_OK)
    {
        /* Attempt to power off before returning error */
        (void)soil_moisture_power_off(config);
        return ret;
    }

    ret = soil_moisture_calculate_moisture(config, adc_reading, readings);

    /* Turn off power after reading */
    (void)soil_moisture_power_off(config);

    /* Diagnostic log for every read */
    ESP_LOGI(TAG, "READ: raw_ADC=%u  moisture=%u%%  valid=%s  state=%s",
             (unsigned)readings->adc_reading,
             (unsigned)readings->moisture_percentage,
             readings->valid ? "YES" : "NO",
             readings->valid
                 ? (readings->state == SOIL_STATE_DRY ? "DRY"
                    : readings->state == SOIL_STATE_NORMAL ? "NORMAL" : "WET")
                 : "UNCALIBRATED");

    return ret;
}

/**
 * @brief Full interactive calibration with long-press gate.
 *
 * Procedure (matches user spec):
 *  1. Wait for long press (>3 s) to enter calibration mode
 *  2. User places sensor in AIR → short press → capture dry ADC
 *  3. User places sensor in WATER → short press → capture wet ADC
 *  4. Validate & save to NVS with magic key
 *  5. Print moisture % and exit
 */
esp_err_t Soil_Moisture_UserCalibrate(Soil_Moisture_Config_t *config)
{
    uint16_t wet = 0;
    uint16_t dry = 0;
    esp_err_t ret;

    if (config == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    /* Ensure sensor is powered for calibration */
    (void)soil_moisture_power_on(config);

    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "    CALIBRATION MODE STARTED");
    ESP_LOGI(TAG, "============================================");

    /* ---- Step 1: Capture DRY (air) reference ---- */
    ESP_LOGI(TAG, "STEP 1: Place sensor in AIR (dry reference), then press the button once.");

    ret = soil_wait_for_button_press(config->calibration_button_gpio);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Button wait failed during DRY step: %s", esp_err_to_name(ret));
        (void)soil_moisture_power_off(config);
        return ret;
    }

    ret = soil_moisture_read_adc(config, &dry);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "ADC read failed during DRY step: %s", esp_err_to_name(ret));
        (void)soil_moisture_power_off(config);
        return ret;
    }
    ESP_LOGI(TAG, "Dry value captured: %u", (unsigned)dry);

    /* ---- Step 2: Capture WET (water) reference ---- */
    ESP_LOGI(TAG, "STEP 2: Place sensor in WATER (wet reference), then press the button once.");

    ret = soil_wait_for_button_press(config->calibration_button_gpio);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Button wait failed during WET step: %s", esp_err_to_name(ret));
        (void)soil_moisture_power_off(config);
        return ret;
    }

    ret = soil_moisture_read_adc(config, &wet);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "ADC read failed during WET step: %s", esp_err_to_name(ret));
        (void)soil_moisture_power_off(config);
        return ret;
    }
    ESP_LOGI(TAG, "Wet value captured: %u", (unsigned)wet);

    /* ---- Validation ---- */
    uint32_t diff = (dry > wet) ? (dry - wet) : (wet - dry);

    ESP_LOGI(TAG, "Calibration values: dry=%u, wet=%u, diff=%lu",
             (unsigned)dry, (unsigned)wet, (unsigned long)diff);

    if (diff < SOIL_CAL_MIN_ADC_DIFF)
    {
        ESP_LOGE(TAG, "CALIBRATION REJECTED: difference %lu < minimum %d — "
                      "values are too close. Re-run calibration with proper dry/wet placement.",
                 (unsigned long)diff, SOIL_CAL_MIN_ADC_DIFF);
        (void)soil_moisture_power_off(config);
        return ESP_ERR_INVALID_STATE;
    }

    if (dry > SOIL_CAL_ADC_MAX || wet > SOIL_CAL_ADC_MAX)
    {
        ESP_LOGE(TAG, "CALIBRATION REJECTED: values out of range (dry=%u, wet=%u, max=%d)",
                 (unsigned)dry, (unsigned)wet, SOIL_CAL_ADC_MAX);
        (void)soil_moisture_power_off(config);
        return ESP_ERR_INVALID_STATE;
    }

    /* For capacitive sensors, dry is typically > wet.
     * For resistive sensors, it can be reversed.
     * We accept both orientations — the calculation logic handles it. */
    if (dry <= wet)
    {
        ESP_LOGW(TAG, "Note: Dry value (%u) <= Wet value (%u). "
                      "This is unusual for capacitive sensors but OK for resistive types.",
                 (unsigned)dry, (unsigned)wet);
    }

    /* ---- Save to NVS ---- */
    ret = soil_nvs_save_calibration(dry, wet);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to save calibration to NVS: %s", esp_err_to_name(ret));
        (void)soil_moisture_power_off(config);
        return ret;
    }

    /* ---- Print a sample moisture reading using the new calibration ---- */
    uint16_t sample_adc = 0;
    if (soil_moisture_read_adc(config, &sample_adc) == ESP_OK)
    {
        Soil_Moisture_Readings_t sample = {0};
        soil_moisture_calculate_moisture(config, sample_adc, &sample);
        ESP_LOGI(TAG, "Post-calibration test read: ADC=%u → moisture=%u%% (valid=%s)",
                 (unsigned)sample_adc,
                 (unsigned)sample.moisture_percentage,
                 sample.valid ? "YES" : "NO");
    }

    (void)soil_moisture_power_off(config);

    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "    CALIBRATION SAVED SUCCESSFULLY");
    ESP_LOGI(TAG, "    Dry ADC = %u", (unsigned)dry);
    ESP_LOGI(TAG, "    Wet ADC = %u", (unsigned)wet);
    ESP_LOGI(TAG, "============================================");
    return ESP_OK;
}

esp_err_t soil_moisture_calculate_moisture(const Soil_Moisture_Config_t *config,
                                           uint16_t adc_reading,
                                           Soil_Moisture_Readings_t *readings)
{
    uint16_t adc_dry = 0;
    uint16_t adc_wet = 0;
    esp_err_t ret;

    if ((config == NULL) || (readings == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    readings->adc_reading = adc_reading;
    readings->valid       = false;   /* pessimistic default until proven good */

    ret = soil_nvs_load_calibration(&adc_dry, &adc_wet);
    if (ret != ESP_OK)
    {
        /*
         * No calibration data. Report 0% but flag the reading INVALID so the
         * control board never starts irrigation based on an uncalibrated
         * sensor (which could otherwise read a constant low value and flood
         * the zone). The user must run the calibration routine first.
         */
        ESP_LOGW(TAG, "No calibration data found — reading marked INVALID. "
                      "Run calibration (hold button >3s) to fix.");
        readings->moisture_percentage = 0;
        readings->state = SOIL_STATE_DRY;
        readings->valid = false;
        return ESP_OK; /* Not a hardware error, just uncalibrated */
    }

    int32_t moisture_percentage = 0; // Use signed for calculation

    /* Standard Capacitive Logic: Dry > Wet */
    if (adc_dry > adc_wet)
    {
        /* Normal case for capacitive sensors */
        if (adc_reading <= adc_wet)
        {
            moisture_percentage = 100;
        }
        else if (adc_reading >= adc_dry)
        {
            moisture_percentage = 0;
        }
        else
        {
            float range = (float)(adc_dry - adc_wet);
            float val = (float)(adc_dry - adc_reading);
            moisture_percentage = (int32_t)((val * 100.0f) / range);
        }
    }
    else if (adc_wet > adc_dry)
    {
        /* Resistive or Inverted Logic: Wet > Dry */
        if (adc_reading >= adc_wet)
        {
            moisture_percentage = 100;
        }
        else if (adc_reading <= adc_dry)
        {
            moisture_percentage = 0;
        }
        else
        {
            float range = (float)(adc_wet - adc_dry);
            float val = (float)(adc_reading - adc_dry);
            moisture_percentage = (int32_t)((val * 100.0f) / range);
        }
    }
    else
    {
        /* adc_dry == adc_wet: Invalid calibration or division by zero risk */
        ESP_LOGW(TAG, "Calibration invalid (Dry == Wet) — reading marked INVALID.");
        readings->moisture_percentage = 0;
        readings->state = SOIL_STATE_DRY;
        readings->valid = false;
        return ESP_OK;
    }

    /* Clamp */
    if (moisture_percentage > 100) moisture_percentage = 100;
    if (moisture_percentage < 0) moisture_percentage = 0;

    readings->moisture_percentage = (uint8_t)moisture_percentage;
    readings->valid = true;   /* calibrated and within range */

    if (moisture_percentage < 30)
    {
        readings->state = SOIL_STATE_DRY;
    }
    else if (moisture_percentage < 70)
    {
        readings->state = SOIL_STATE_NORMAL;
    }
    else
    {
        readings->state = SOIL_STATE_WET;
    }

    /* Detailed diagnostic — logged on every successful calculation */
    ESP_LOGD(TAG, "CALC: adc=%u, cal_dry=%u, cal_wet=%u → moisture=%d%%",
             (unsigned)adc_reading, (unsigned)adc_dry, (unsigned)adc_wet,
             (int)moisture_percentage);

    return ESP_OK;
}