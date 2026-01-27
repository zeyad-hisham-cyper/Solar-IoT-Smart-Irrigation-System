#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "soil_moisture.h"

static const char *TAG = "CALIBRATION";

void app_main(void)
{
    soil_moisture_t sensor;

    // GPIO35 = ADC1_CHANNEL_7
    ESP_ERROR_CHECK(
        soil_moisture_init(
            &sensor,
            ADC_CHANNEL_7,
            ADC_ATTEN_DB_12,
            64          // averaging
        )
    );

    uint16_t raw;
    uint16_t min_val = 4095;
    uint16_t max_val = 0;

    while (1) {
        if (soil_moisture_read_raw(&sensor, &raw) == ESP_OK) {

            if (raw < min_val) min_val = raw;
            if (raw > max_val) max_val = raw;

            ESP_LOGI(TAG,
                     "RAW=%u | MIN=%u | MAX=%u",
                     raw, min_val, max_val);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
