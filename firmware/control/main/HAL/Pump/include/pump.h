/**
 * @file pump.h
 * @brief Water-pump HAL driver for the Control board (single MOSFET/relay switch).
 *
 * The pump is energised through one low-side MOSFET (or relay) driven by a
 * single GPIO. Both active-high and active-LOW (negative-logic) wiring are
 * supported. For the deployed hardware the switch is ACTIVE-LOW.
 *
 * Safety: pump_init() configures the GPIO as an output, enables the internal
 * pull resistor that corresponds to the OFF level, and drives OFF immediately,
 * so the pump can never be energised while the ESP32 boots or after a crash
 * (when GPIOs revert to high-impedance inputs).
 *
 * @version 1.0.0
 */
#ifndef PUMP_H
#define PUMP_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Pump handle. Treat fields as private after pump_init().
 */
typedef struct {
    gpio_num_t gpio;          /**< Switch GPIO                                  */
    bool       active_high;   /**< true: HIGH = ON; false: LOW = ON (negative)  */
    bool       on;            /**< Current logical state                        */
    bool       initialised;
} pump_t;

/**
 * @brief Initialise the pump driver and force it OFF.
 *
 * @param[out] pump         Caller-allocated handle.
 * @param[in]  gpio         Switch GPIO.
 * @param[in]  active_high  true if HIGH energises the pump; false for active-LOW.
 * @return ESP_OK on success, or an ESP-IDF error code.
 */
esp_err_t pump_init(pump_t *pump, gpio_num_t gpio, bool active_high);

/** @brief Energise the pump. */
esp_err_t pump_on(pump_t *pump);

/** @brief De-energise the pump (safe state). */
esp_err_t pump_off(pump_t *pump);

/** @brief Query whether the pump is currently energised. */
bool pump_is_on(const pump_t *pump);

#ifdef __cplusplus
}
#endif

#endif /* PUMP_H */
