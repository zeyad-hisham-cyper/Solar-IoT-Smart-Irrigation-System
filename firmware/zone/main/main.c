#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "app_config.h"
#include "soil_moisture.h"
#include "temp_ds18b20.h"

static const char *TAG = "TEST_APP";

void app_main(void)
{
    esp_err_t ret;

    /* Initialize NVS (Required for Calibration storage) */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Starting Soil Moisture Driver Test...");

    /* 1. Setup Configuration */
    Soil_Moisture_Config_t soil_config = {
        .adc_unit = SOIL_ADC_UNIT,
        .adc_channel = SOIL_ADC_CHANNEL,
        .adc_attenuation = SOIL_ADC_ATTEN,
        .adc_bitwidth = SOIL_ADC_BITWIDTH,
        .power_gpio = SOIL_POWER_GPIO,
        .calibration_button_gpio = SOIL_CALIBRATION_BUTTON, 
        .read_delay_ms = SOIL_READ_DELAY_MS,
        .adc_samples = SOIL_ADC_SAMPLES
    };

    /* 2. Initialize Driver */
    ESP_LOGI(TAG, "Initializing Driver...");
    ret = Soil_Moisture_Init(&soil_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Initialization Failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "Driver Initialized.");

    /* 3. Initialize DS18B20 (Temperature) */
    ESP_LOGI(TAG, "Initializing DS18B20...");
    Temp_DS18B20_Config_t temp_config = {
        .data_gpio = DS18B20_DATA_GPIO,
        .enable_internal_pullup = DS18B20_INTERNAL_PULLUP, // Note: External 4.7k recommended
        .max_retries = DS18B20_MAX_RETRIES,
        .resolution = (Temp_DS18B20_Resolution_t)DS18B20_RESOLUTION_BITS
    };
    
    /* We don't abort if temp fails, just log warning */
    if (Temp_DS18B20_Init(&temp_config) != ESP_OK) {
        ESP_LOGW(TAG, "DS18B20 not detected or init failed.");
    }

    /* 4. Offer Calibration (Optional - Wait 5s to skip) */
    ESP_LOGI(TAG, "If you want to CALIBRATE, press the button (GPIO %d) now.", soil_config.calibration_button_gpio);
    ESP_LOGI(TAG, "Waiting 5 seconds...");
    vTaskDelay(pdMS_TO_TICKS(5000));

    if (gpio_get_level(soil_config.calibration_button_gpio) == 0) { // Active Low
        ESP_LOGI(TAG, "Button Detected! Entering Calibration Mode...");
        ret = Soil_Moisture_UserCalibrate(&soil_config);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Calibration Completed Successfully.");
        } else {
            ESP_LOGE(TAG, "Calibration Failed: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGI(TAG, "No button press. Skipping Calibration, using stored values.");
    }

    /* 5. Reading Loop */
    ESP_LOGI(TAG, "Entering Reading Loop...");
    Soil_Moisture_Readings_t soil_readings;
    Temp_DS18B20_Readings_t temp_readings;

    while (1) {
        /* Read Soil */
        ret = Soil_Moisture_Read(&soil_config, &soil_readings);
        
        /* Read Temp */
        (void)Temp_DS18B20_Read(&temp_config, &temp_readings);

        if (ret == ESP_OK) {
            const char* state_str = "UNKNOWN";
            switch(soil_readings.state) {
                case SOIL_STATE_DRY: state_str = "DRY"; break;
                case SOIL_STATE_NORMAL: state_str = "NORMAL"; break;
                case SOIL_STATE_WET: state_str = "WET"; break;
            }

            char temp_str[16] = "--.-";
            if (temp_readings.valid) {
                snprintf(temp_str, sizeof(temp_str), "%.1f", temp_readings.temperature_c);
            }

            ESP_LOGI(TAG, "Moisture: %3d%% | Temp: %s C | ADC: %4d | State: %s", 
                     soil_readings.moisture_percentage, 
                     temp_str,
                     soil_readings.adc_reading, 
                     state_str);
        } else {
            ESP_LOGE(TAG, "Read Failed: %s", esp_err_to_name(ret));
        }

        vTaskDelay(pdMS_TO_TICKS(2000)); // Read every 2 seconds
    }
}
