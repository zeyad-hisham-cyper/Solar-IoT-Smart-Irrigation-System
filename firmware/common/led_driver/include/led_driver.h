/**
 * @file led_driver.h
 * @brief Reusable, non-blocking single-LED HAL driver for ESP32 (ESP-IDF).
 *
 * Pure C. Each LED is an independent handle bound to one GPIO. The driver
 * supports steady ON/OFF/toggle plus background BLINK and one-shot PULSE
 * patterns implemented with a per-LED esp_timer — no busy-waiting and no
 * dedicated task. Convenience "status pattern" helpers wrap the common
 * blink cadences used by the application.
 *
 * Active-high and active-low wiring are both supported via @ref led_init.
 *
 * @version 1.0.0
 */
#ifndef LED_DRIVER_H
#define LED_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Blink forever (use as the @p count argument to led_blink). */
#define LED_BLINK_FOREVER   (-1)

/**
 * @brief LED handle. Treat all fields as private after led_init().
 */
typedef struct {
    gpio_num_t        gpio;          /**< Driver GPIO pin                       */
    bool              active_high;   /**< true: HIGH=on; false: LOW=on          */
    bool              initialised;   /**< Set by led_init()                     */
    bool              phys_on;       /**< Current logical state (on/off)        */

    /* Blink/pulse engine (managed internally) */
    esp_timer_handle_t timer;        /**< Per-LED software timer                */
    uint32_t          on_ms;         /**< ON phase duration during blink        */
    uint32_t          off_ms;        /**< OFF phase duration during blink       */
    int32_t           remaining;     /**< Remaining ON phases, or LED_BLINK_FOREVER */
    bool              one_shot;      /**< true while a pulse is in progress      */
} led_t;

/**
 * @brief Initialise an LED on a GPIO and drive it OFF.
 *
 * @param[out] led          Caller-allocated handle.
 * @param[in]  gpio         GPIO pin (must be output-capable).
 * @param[in]  active_high  true if HIGH turns the LED on, false for active-low.
 * @return ESP_OK on success, or an ESP-IDF error code.
 */
esp_err_t led_init(led_t *led, gpio_num_t gpio, bool active_high);

/** @brief Turn the LED on steadily (cancels any blink/pulse). */
esp_err_t led_on(led_t *led);

/** @brief Turn the LED off (cancels any blink/pulse). */
esp_err_t led_off(led_t *led);

/** @brief Set the LED on/off (cancels any blink/pulse). */
esp_err_t led_set(led_t *led, bool on);

/** @brief Toggle the steady LED state (cancels any blink/pulse). */
esp_err_t led_toggle(led_t *led);

/**
 * @brief Start a background blink pattern.
 *
 * @param[in] led     Initialised handle.
 * @param[in] on_ms   ON phase in milliseconds.
 * @param[in] off_ms  OFF phase in milliseconds.
 * @param[in] count   Number of ON pulses, or LED_BLINK_FOREVER for continuous.
 * @return ESP_OK on success.
 */
esp_err_t led_blink(led_t *led, uint32_t on_ms, uint32_t off_ms, int32_t count);

/**
 * @brief One-shot pulse: turn ON immediately, then OFF after @p duration_ms.
 * @param[in] led          Initialised handle.
 * @param[in] duration_ms  ON duration in milliseconds.
 * @return ESP_OK on success.
 */
esp_err_t led_pulse(led_t *led, uint32_t duration_ms);

/** @brief Stop any active blink/pulse and turn the LED off. */
esp_err_t led_stop(led_t *led);

#ifdef __cplusplus
}
#endif

#endif /* LED_DRIVER_H */
