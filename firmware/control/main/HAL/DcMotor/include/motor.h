/**
 * @file motor.h
 * @brief DC Motor HAL driver for ESP32 (ESP-IDF) – L298N H-bridge.
 *
 * Controls one motor channel via two direction GPIO pins (IN-A, IN-B) and an
 * optional LEDC PWM pin (EN) for speed control.
 * If speed control is not needed, tie the EN pin HIGH in hardware and set
 * MOTOR_PWM_ENABLE to 0 – the EN pin field is then ignored entirely.
 *
 * @version 1.0.0
 */

#ifndef MOTOR_H_
#define MOTOR_H_

#include <stdint.h>
#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                           CONFIGURATION                                    */
/* ========================================================================== */

/** Set to 1 to enable PWM speed control via the EN pin (L298N ENA/ENB).
 *  Set to 0 if EN is tied HIGH in hardware (full speed, no software control). */
#define MOTOR_PWM_ENABLE        (0)

/** LEDC PWM resolution – 10-bit gives a 0..1023 speed range. */
#define MOTOR_PWM_RESOLUTION    LEDC_TIMER_10_BIT

/** LEDC PWM frequency in Hz. */
#define MOTOR_PWM_FREQ_HZ       (5000U)

/** Maximum speed value (= 2^resolution – 1). */
#define MOTOR_SPEED_MAX         ((1U << MOTOR_PWM_RESOLUTION) - 1U)

/* ========================================================================== */
/*                           TYPE DEFINITIONS                                 */
/* ========================================================================== */

/** Motor rotation direction. */
typedef enum {
    MOTOR_DIR_CW  = 0,   /**< Clockwise:         IN-A = H, IN-B = L */
    MOTOR_DIR_CCW = 1,   /**< Counter-clockwise:  IN-A = L, IN-B = H */
} motor_dir_t;

/**
 * @brief Motor handle.
 *
 * Fill in ALL hardware fields before calling motor_init().
 * On targets where MOTOR_PWM_ENABLE == 0, the pin_en / ledc_* fields
 * are compiled out and do not need to be set.
 */
typedef struct {
    gpio_num_t  pin_in_a;           /**< [IN] L298N IN-A (direction)    */
    gpio_num_t  pin_in_b;           /**< [IN] L298N IN-B (direction)    */
#if MOTOR_PWM_ENABLE
    gpio_num_t      pin_en;         /**< [IN] L298N EN  (PWM speed)     */
    ledc_channel_t  ledc_channel;   /**< [IN] LEDC channel to use       */
    ledc_timer_t    ledc_timer;     /**< [IN] LEDC timer to use         */
#endif
} motor_t;

/* ========================================================================== */
/*                           FUNCTION PROTOTYPES                              */
/* ========================================================================== */

/**
 * @brief Initialise the motor driver.
 *
 * Configures IN-A and IN-B as GPIO outputs (both driven LOW).
 * If MOTOR_PWM_ENABLE is set, also configures the LEDC timer and channel
 * on pin_en and sets the initial duty to 0.
 *
 * @param[in] motor  Pointer to a caller-allocated, hardware-filled motor_t.
 * @return ESP_OK on success, or an ESP-IDF error code on failure.
 */
esp_err_t motor_init(motor_t *motor);

/**
 * @brief Rotate the motor in the given direction at the given speed.
 *
 * @param[in] motor  Initialised motor handle.
 * @param[in] dir    Rotation direction (MOTOR_DIR_CW or MOTOR_DIR_CCW).
 * @param[in] speed  Duty cycle: 0 (stop) … MOTOR_SPEED_MAX (full speed).
 *                   Ignored when MOTOR_PWM_ENABLE == 0.
 * @return ESP_OK on success.
 */
esp_err_t motor_rotate(motor_t *motor, motor_dir_t dir, uint32_t speed);

/**
 * @brief Stop the motor (coast – IN-A = L, IN-B = L, EN = 0).
 *
 * @param[in] motor  Initialised motor handle.
 * @return ESP_OK on success.
 */
esp_err_t motor_stop(motor_t *motor);

#ifdef __cplusplus
}
#endif

#endif /* MOTOR_H_ */