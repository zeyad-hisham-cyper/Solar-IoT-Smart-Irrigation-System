/**
 * @file pump.c
 * @brief Implementation of the Control-board pump switch driver.
 *
 * @version 1.0.0
 */
#include "pump.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "PUMP";

/** Physical GPIO level for a given logical on/off state. */
static int pump_level_for(const pump_t *pump, bool on)
{
    return (on == pump->active_high) ? 1 : 0;
}

esp_err_t pump_init(pump_t *pump, gpio_num_t gpio, bool active_high)
{
    if (pump == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(pump, 0, sizeof(*pump));
    pump->gpio        = gpio;
    pump->active_high = active_high;

    /*
     * Pull the line towards the OFF level so the pump stays de-energised while
     * the GPIO is high-impedance during reset/boot:
     *   - active-LOW  pump: OFF = HIGH -> internal pull-UP
     *   - active-HIGH pump: OFF = LOW  -> internal pull-DOWN
     */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << (uint32_t)gpio),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = active_high ? GPIO_PULLUP_DISABLE : GPIO_PULLUP_ENABLE,
        .pull_down_en = active_high ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO %d config failed: %s", (int)gpio, esp_err_to_name(ret));
        return ret;
    }

    /* Drive OFF immediately. */
    (void)gpio_set_level(gpio, pump_level_for(pump, false));
    pump->on          = false;
    pump->initialised = true;

    ESP_LOGI(TAG, "Initialised on GPIO %d (%s, OFF)",
             (int)gpio, active_high ? "active-HIGH" : "active-LOW");
    return ESP_OK;
}

esp_err_t pump_on(pump_t *pump)
{
    if (pump == NULL || !pump->initialised) {
        return ESP_ERR_INVALID_STATE;
    }
    (void)gpio_set_level(pump->gpio, pump_level_for(pump, true));
    pump->on = true;
    ESP_LOGI(TAG, ">>> PUMP ON");
    return ESP_OK;
}

esp_err_t pump_off(pump_t *pump)
{
    if (pump == NULL || !pump->initialised) {
        return ESP_ERR_INVALID_STATE;
    }
    (void)gpio_set_level(pump->gpio, pump_level_for(pump, false));
    pump->on = false;
    ESP_LOGI(TAG, ">>> PUMP OFF");
    return ESP_OK;
}

bool pump_is_on(const pump_t *pump)
{
    return (pump != NULL) && pump->initialised && pump->on;
}
