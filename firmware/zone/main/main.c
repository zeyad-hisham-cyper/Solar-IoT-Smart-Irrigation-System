#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "app_config.h"
#include "soil_moisture.h"

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

    /* 3. Offer Calibration (Optional - Wait 5s to skip) */
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

    /* 4. Reading Loop */
    ESP_LOGI(TAG, "Entering Reading Loop...");
    Soil_Moisture_Readings_t readings;

    while (1) {
        ret = Soil_Moisture_Read(&soil_config, &readings);
        if (ret == ESP_OK) {
            const char* state_str = "UNKNOWN";
            switch(readings.state) {
                case SOIL_STATE_DRY: state_str = "DRY"; break;
                case SOIL_STATE_NORMAL: state_str = "NORMAL"; break;
                case SOIL_STATE_WET: state_str = "WET"; break;
            }

            ESP_LOGI(TAG, "Moisture: %3d%% | ADC: %4d | State: %s", 
                     readings.moisture_percentage, readings.adc_reading, state_str);
        } else {
            ESP_LOGE(TAG, "Read Failed: %s", esp_err_to_name(ret));
        }

        vTaskDelay(pdMS_TO_TICKS(2000)); // Read every 2 seconds
    }
}
