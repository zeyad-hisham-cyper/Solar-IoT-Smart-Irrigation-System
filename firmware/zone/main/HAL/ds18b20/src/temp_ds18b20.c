#include "temp_ds18b20_private.h"

static const char *TAG = "TEMP_DS18B20";

/* ========================================================================== */
/*                              LOCAL UTILITIES                               */
/* ========================================================================== */

uint32_t temp_ds18b20_get_tconv_ms(Temp_DS18B20_Resolution_t res)
{
    switch (res)
    {
        case TEMP_DS18B20_RES_9_BIT:
            return TEMP_DS18B20_TCONV_9BIT_MS;
        case TEMP_DS18B20_RES_10_BIT:
            return TEMP_DS18B20_TCONV_10BIT_MS;
        case TEMP_DS18B20_RES_11_BIT:
            return TEMP_DS18B20_TCONV_11BIT_MS;
        case TEMP_DS18B20_RES_12_BIT:
        default:
            return TEMP_DS18B20_TCONV_12BIT_MS;
    }
}

static esp_err_t temp_ds18b20_validate_config(const Temp_DS18B20_Config_t *config)
{
    if (config == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if ((config->data_gpio < 0) || (config->data_gpio == GPIO_NUM_NC))
    {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t temp_ds18b20_enumerate_first_device(Temp_DS18B20_Config_t *config)
{
    if (config == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    onewire_device_iter_handle_t iter = NULL;
    onewire_device_t next_onewire_device;
    esp_err_t ret = onewire_new_device_iter(config->onewire_bus, &iter);
    if (ret != ESP_OK)
    {
        return ret;
    }

    int ds_found = 0;
    while (true)
    {
        ret = onewire_device_iter_get_next(iter, &next_onewire_device);
        if (ret == ESP_ERR_NOT_FOUND)
        {
            break;
        }
        if (ret != ESP_OK)
        {
            (void)onewire_del_device_iter(iter);
            return ret;
        }

        ds18b20_config_t ds_cfg = {};
        ds18b20_device_handle_t dev = NULL;
        if (ds18b20_new_device_from_enumeration(&next_onewire_device, &ds_cfg, &dev) == ESP_OK)
        {
            config->ds18b20_dev = dev;
            ds_found = 1;
            break; /* single-sensor use-case */
        }
        /* else: some other 1-Wire device, ignore */
    }

    (void)onewire_del_device_iter(iter);

    if (!ds_found)
    {
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}

/* ========================================================================== */
/*                         PUBLIC FUNCTION DEFINITIONS                        */
/* ========================================================================== */

esp_err_t Temp_DS18B20_Init(Temp_DS18B20_Config_t *config)
{
    esp_err_t ret = temp_ds18b20_validate_config(config);
    if (ret != ESP_OK)
    {
        return ret;
    }

    /* Defaults */
    if (config->max_rx_bytes == 0U)
    {
        config->max_rx_bytes = TEMP_DS18B20_DEFAULT_MAX_RX_BYTES;
    }
    if (config->max_retries == 0U)
    {
        config->max_retries = TEMP_DS18B20_DEFAULT_MAX_RETRIES;
    }
    if ((config->resolution != TEMP_DS18B20_RES_9_BIT) &&
        (config->resolution != TEMP_DS18B20_RES_10_BIT) &&
        (config->resolution != TEMP_DS18B20_RES_11_BIT) &&
        (config->resolution != TEMP_DS18B20_RES_12_BIT))
    {
        config->resolution = TEMP_DS18B20_RES_12_BIT;
    }

    config->stats = (Temp_DS18B20_Stats_t){0};

    /* Install 1-Wire bus using RMT backend */
    onewire_bus_config_t bus_config = {
        .bus_gpio_num = config->data_gpio,
        .flags = {
            .en_pull_up = config->enable_internal_pullup,
        }
    };

    onewire_bus_rmt_config_t rmt_config = {
        .max_rx_bytes = config->max_rx_bytes,
    };

    ret = onewire_new_bus_rmt(&bus_config, &rmt_config, &config->onewire_bus);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to install 1-Wire bus (%s)", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "1-Wire bus installed on GPIO%d", (int)config->data_gpio);

    /* Enumerate first DS18B20 */
    ret = temp_ds18b20_enumerate_first_device(config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "No DS18B20 found on bus (%s)", esp_err_to_name(ret));
        (void)onewire_bus_del(config->onewire_bus);
        config->onewire_bus = NULL;
        return ret;
    }

    onewire_device_address_t addr = 0;
    (void)ds18b20_get_device_address(config->ds18b20_dev, &addr);
    ESP_LOGI(TAG, "DS18B20 detected, address: %016llX", (unsigned long long)addr);

    return ESP_OK;
}

esp_err_t Temp_DS18B20_Deinit(Temp_DS18B20_Config_t *config)
{
    if (config == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (config->ds18b20_dev != NULL)
    {
        (void)ds18b20_del_device(config->ds18b20_dev);
        config->ds18b20_dev = NULL;
    }

    if (config->onewire_bus != NULL)
    {
        (void)onewire_bus_del(config->onewire_bus);
        config->onewire_bus = NULL;
    }

    return ESP_OK;
}

esp_err_t Temp_DS18B20_Read(Temp_DS18B20_Config_t *config, Temp_DS18B20_Readings_t *readings)
{
    if ((config == NULL) || (readings == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }
    if ((config->onewire_bus == NULL) || (config->ds18b20_dev == NULL))
    {
        return ESP_ERR_INVALID_STATE;
    }

    readings->temperature_c = 0.0f;
    readings->valid = false;

    const uint32_t tconv_ms = temp_ds18b20_get_tconv_ms(config->resolution);
    const uint32_t retries = config->max_retries;

    esp_err_t ret = ESP_FAIL;

    for (uint32_t attempt = 0; attempt < retries; attempt++)
    {
        /* Trigger conversion for all devices on the bus (even if only one) */
        ret = ds18b20_trigger_temperature_conversion_for_all(config->onewire_bus);
        if (ret != ESP_OK)
        {
            config->stats.retries++;
            ESP_LOGW(TAG, "Conversion trigger failed (attempt %lu/%lu): %s",
                     (unsigned long)(attempt + 1U), (unsigned long)retries, esp_err_to_name(ret));
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(tconv_ms));

        float temp_c = 0.0f;
        ret = ds18b20_get_temperature(config->ds18b20_dev, &temp_c);
        if (ret == ESP_OK)
        {
            readings->temperature_c = temp_c;
            readings->valid = true;
            config->stats.read_ok++;
            return ESP_OK;
        }

        config->stats.retries++;
        ESP_LOGW(TAG, "Temperature read failed (attempt %lu/%lu): %s",
                 (unsigned long)(attempt + 1U), (unsigned long)retries, esp_err_to_name(ret));
    }

    config->stats.read_fail++;
    return ret;
}

esp_err_t Temp_DS18B20_GetStats(const Temp_DS18B20_Config_t *config, Temp_DS18B20_Stats_t *stats)
{
    if ((config == NULL) || (stats == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    *stats = config->stats;
    return ESP_OK;
}
