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

/**
 * @brief Save calibration values to NVS
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
        return ret;
    }

    ret = nvs_set_u32(h, SOIL_NVS_KEY_ADC_DRY, (uint32_t)dry);
    if (ret == ESP_OK)
    {
        ret = nvs_set_u32(h, SOIL_NVS_KEY_ADC_WET, (uint32_t)wet);
    }
    
    if (ret == ESP_OK)
    {
        ret = nvs_commit(h);
    }
    
    nvs_close(h);
    return ret;
}

/**
 * @brief Load calibration values from NVS
 * @param[out] dry Pointer to store dry value
 * @param[out] wet Pointer to store wet value
 * @return ESP_OK or NVS error
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
        return ret;
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
        return ret;
    }

    *dry = (uint16_t)dry_val;
    *wet = (uint16_t)wet_val;
    return ESP_OK;
}

/**
 * @brief Wait for button press with debounce
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
    
    /* Removed incorrect check against ADC_HANDLE macro */

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

    /* Configure Button if valid */
    if ((config->calibration_button_gpio >= 0) && (config->calibration_button_gpio != GPIO_NUM_NC))
    {
        gpio_config_t button_io_cfg = {
            .pin_bit_mask = (1ULL << (uint32_t)config->calibration_button_gpio),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        ret = gpio_config(&button_io_cfg);
        if (ret != ESP_OK)
        {
            return ret;
        }
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

    return ret;
}

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

    ESP_LOGI(TAG, "Calibration Started");
    ESP_LOGI(TAG, "Place the sensor in water/mud (max moisture), then press the button");
    
    ret = soil_wait_for_button_press(config->calibration_button_gpio);
    if (ret != ESP_OK)
    {
        (void)soil_moisture_power_off(config);
        return ret;
    }

    ret = soil_moisture_read_adc(config, &wet);
    if (ret != ESP_OK)
    {
        (void)soil_moisture_power_off(config);
        return ret;
    }
    ESP_LOGI(TAG, "WET ADC Reading: %u", (unsigned int)wet);

    ESP_LOGI(TAG, "Place the sensor in dry air/soil (min moisture), then press the button");
    ret = soil_wait_for_button_press(config->calibration_button_gpio);
    if (ret != ESP_OK) // Fixed logic: previously check was (ret == ESP_OK) return ret; which was buggy
    {
        (void)soil_moisture_power_off(config);
        return ret;
    }

    ret = soil_moisture_read_adc(config, &dry);
    if (ret != ESP_OK)
    {
        (void)soil_moisture_power_off(config);
        return ret;
    }
    ESP_LOGI(TAG, "DRY ADC Reading: %u", (unsigned int)dry);

    /* Logic check: Dry reading (usually higher resistance -> ? ADC) depends on circuit. 
       Usually capacitive sensors: Air=High Voltage (High ADC), Water=Low Voltage.
       Resistive: Dry=Low Voltage, Wet=High Voltage (Vout across R).
       
       The code in calculate_moisture implies: if (adc >= wet) -> 100%. 
       This suggests Wet is a HIGH ADC value.
       If Dry <= Wet, it logged "Unexpected". This implies Dry MUST be > Wet.
       
       Wait, let's re-read the original calculation:
       `moisture_percentage = (int)((float)(adc_dry - adc_reading) * 100.0f / (float)(adc_dry - adc_wet));`
       
       If reading = dry, (dry-dry) = 0%. Correct.
       If reading = wet, (dry-wet)/(dry-wet) = 100%. Correct.
       
       This implies (dry - wet) must be positive, so Dry > Wet. 
       Usually typical for Capacitive Sensor (Air ~ 3000, Water ~ 1200).
       
       The Check `if (dry <= wet)` was raising an error "Unexpected". 
       So Dry MUST be > Wet. 
       
       Wait, the original code had:
       ```c
       if (readings->state < 0) ...
       if (dry <= wet) { ESP_LOGW... }
       ```
       
       I will preserve this logic.
    */
    
    if (dry <= wet)
    {
        ESP_LOGW(TAG, "Unexpected Behavior: Dry value (%u) <= Wet value (%u). Check sensor type.", (unsigned)dry, (unsigned)wet);
        // We might choose to return error or allow it if the user insists, but returning error is safer
        (void)soil_moisture_power_off(config);
        return ESP_ERR_INVALID_STATE;
    }

    ret = soil_nvs_save_calibration(dry, wet);
    (void)soil_moisture_power_off(config); // Power off done

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to save calibration to NVS (%s)", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Calibration saved to NVS");
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

    ret = soil_nvs_load_calibration(&adc_dry, &adc_wet);
    if (ret != ESP_OK)
    {
        /* Fallback if no calibration data */
        ESP_LOGW(TAG, "No calibration data found. Returning raw 0%%.");
        readings->moisture_percentage = 0;
        readings->state = SOIL_STATE_DRY;
        return ESP_OK; /* Not necessarily an error, just uncalibrated */
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
        ESP_LOGW(TAG, "Calibration invalid (Dry == Wet). Returning 0%%.");
        readings->moisture_percentage = 0;
        readings->state = SOIL_STATE_DRY;
        return ESP_OK;
    }

    /* Clamp */
    if (moisture_percentage > 100) moisture_percentage = 100;
    if (moisture_percentage < 0) moisture_percentage = 0;

    readings->moisture_percentage = (uint8_t)moisture_percentage;

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

    return ESP_OK;
}