/**
 * @file buzzer_driver.h
 * @brief Reusable, non-blocking buzzer HAL driver for ESP32 (ESP-IDF).
 *
 * Pure C. Supports both active buzzers (simple GPIO on/off) and passive
 * buzzers (tone generation via LEDC PWM). Sound patterns are played in the
 * background with an esp_timer sequencer, so callers never block.
 *
 * High-level helpers provide the standard UX cues: single beep, multi-beep,
 * success / warning / error / alarm patterns. A generic buzzer_play() accepts
 * an arbitrary step sequence.
 *
 * @version 1.0.0
 */
#ifndef BUZZER_DRIVER_H
#define BUZZER_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of steps a single pattern may contain. */
#define BUZZER_MAX_STEPS    24

/**
 * @brief One step of a sound pattern.
 *
 * For passive (LEDC) buzzers @ref freq_hz sets the tone; 0 Hz means silence.
 * For active buzzers any non-zero freq_hz means "on", 0 means "off".
 */
typedef struct {
    uint16_t freq_hz;       /**< Tone frequency in Hz (0 = silent gap) */
    uint16_t duration_ms;   /**< How long this step lasts               */
} buzzer_step_t;

/**
 * @brief Buzzer handle. Treat all fields as private after buzzer_init().
 */
typedef struct {
    gpio_num_t      gpio;
    bool            active_high;
    bool            use_ledc;          /**< true: passive buzzer (tone) via LEDC */
    ledc_channel_t  ledc_channel;
    ledc_timer_t    ledc_timer;
    bool            initialised;

    /* Pattern sequencer (internal) */
    esp_timer_handle_t timer;
    buzzer_step_t   seq[BUZZER_MAX_STEPS];
    size_t          seq_len;
    size_t          seq_idx;
    bool            loop;              /**< true: repeat the sequence (alarm) */
} buzzer_t;

/**
 * @brief Configuration for buzzer_init().
 */
typedef struct {
    gpio_num_t      gpio;          /**< Buzzer GPIO                              */
    bool            active_high;   /**< true: HIGH = sound (active buzzer)       */
    bool            use_ledc;      /**< true: passive buzzer driven with a tone  */
    ledc_channel_t  ledc_channel;  /**< LEDC channel (passive buzzers only)      */
    ledc_timer_t    ledc_timer;    /**< LEDC timer  (passive buzzers only)       */
} buzzer_config_t;

/**
 * @brief Initialise the buzzer and leave it silent.
 * @param[out] bz   Caller-allocated handle.
 * @param[in]  cfg  Hardware configuration.
 * @return ESP_OK on success, or an ESP-IDF error code.
 */
esp_err_t buzzer_init(buzzer_t *bz, const buzzer_config_t *cfg);

/** @brief Stop any pattern and silence the buzzer. */
esp_err_t buzzer_stop(buzzer_t *bz);

/**
 * @brief Play an arbitrary background pattern (copied internally).
 * @param[in] bz     Initialised handle.
 * @param[in] steps  Array of pattern steps.
 * @param[in] n      Number of steps (<= BUZZER_MAX_STEPS).
 * @param[in] loop   true to repeat until buzzer_stop() (e.g. alarm).
 * @return ESP_OK on success.
 */
esp_err_t buzzer_play(buzzer_t *bz, const buzzer_step_t *steps, size_t n, bool loop);

/** @brief One short beep. */
esp_err_t buzzer_single_beep(buzzer_t *bz);

/** @brief @p count short beeps. */
esp_err_t buzzer_multi_beep(buzzer_t *bz, uint8_t count);

/** @brief Rising two-tone "success" chirp. */
esp_err_t buzzer_success(buzzer_t *bz);

/** @brief Two medium "warning" beeps. */
esp_err_t buzzer_warning(buzzer_t *bz);

/** @brief Three low "error" beeps. */
esp_err_t buzzer_error(buzzer_t *bz);

/** @brief Continuous urgent "alarm" until buzzer_stop() is called. */
esp_err_t buzzer_alarm(buzzer_t *bz);

#ifdef __cplusplus
}
#endif

#endif /* BUZZER_DRIVER_H */
