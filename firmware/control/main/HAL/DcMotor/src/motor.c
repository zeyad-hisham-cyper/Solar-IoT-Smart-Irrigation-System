/**
 * @file motor.c
 * @brief DC Motor HAL driver for ESP32 (ESP-IDF) – L298N H-bridge implementation.
 *
 * @version 1.0.0
 */

#include "motor.h"

/* ========================================================================== */
/*                           PRIVATE DEFINITIONS                              */
/* ========================================================================== */

static const char *TAG = "MOTOR";

/* ========================================================================== */
/*                           PUBLIC FUNCTIONS                                 */
/* ========================================================================== */

/**
 * @brief Initialise the motor driver.
 */
esp_err_t motor_init(motor_t *motor)
{
    if (motor == NULL)
    {
        ESP_LOGE(TAG, "NULL motor handle");
        return ESP_ERR_INVALID_ARG;
    }

    /* --- Configure direction pins as push-pull outputs, initially LOW --- */
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << (uint32_t)motor->pin_in_a) |
                        (1ULL << (uint32_t)motor->pin_in_b),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Direction GPIO config failed (%s)", esp_err_to_name(ret));
        return ret;
    }

    gpio_set_level(motor->pin_in_a, 0);
    gpio_set_level(motor->pin_in_b, 0);

#if MOTOR_PWM_ENABLE
    /* --- Configure LEDC timer --- */
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = MOTOR_PWM_RESOLUTION,
        .timer_num       = motor->ledc_timer,
        .freq_hz         = MOTOR_PWM_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };

    ret = ledc_timer_config(&timer_cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "LEDC timer config failed (%s)", esp_err_to_name(ret));
        return ret;
    }

    /* --- Configure LEDC channel on EN pin, duty = 0 --- */
    ledc_channel_config_t ch_cfg = {
        .gpio_num   = motor->pin_en,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = motor->ledc_channel,
        .timer_sel  = motor->ledc_timer,
        .duty       = 0U,
        .hpoint     = 0,
        .intr_type  = LEDC_INTR_DISABLE,
    };

    ret = ledc_channel_config(&ch_cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "LEDC channel config failed (%s)", esp_err_to_name(ret));
        return ret;
    }
#endif /* MOTOR_PWM_ENABLE */

    ESP_LOGI(TAG, "Initialised  IN-A=%d  IN-B=%d"
#if MOTOR_PWM_ENABLE
             "  EN(PWM)=%d"
#endif
             , (int)motor->pin_in_a, (int)motor->pin_in_b
#if MOTOR_PWM_ENABLE
             , (int)motor->pin_en
#endif
    );

    return ESP_OK;
}

/* -------------------------------------------------------------------------- */

/**
 * @brief Rotate the motor in the given direction at the given speed.
 */
esp_err_t motor_rotate(motor_t *motor, motor_dir_t dir, uint32_t speed)
{
    if (motor == NULL)
    {
        ESP_LOGE(TAG, "NULL motor handle");
        return ESP_ERR_INVALID_ARG;
    }

    /* Set direction pins */
    if (dir == MOTOR_DIR_CW)
    {
        gpio_set_level(motor->pin_in_a, 1);
        gpio_set_level(motor->pin_in_b, 0);
    }
    else
    {
        gpio_set_level(motor->pin_in_a, 0);
        gpio_set_level(motor->pin_in_b, 1);
    }

#if MOTOR_PWM_ENABLE
    /* Clamp speed to valid range */
    if (speed > MOTOR_SPEED_MAX)
    {
        speed = MOTOR_SPEED_MAX;
    }

    esp_err_t ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, motor->ledc_channel, speed);
    if (ret != ESP_OK) { return ret; }

    ret = ledc_update_duty(LEDC_LOW_SPEED_MODE, motor->ledc_channel);
    if (ret != ESP_OK) { return ret; }
#endif /* MOTOR_PWM_ENABLE */

    ESP_LOGI(TAG, "Rotating %s  speed=%"PRIu32,
             (dir == MOTOR_DIR_CW) ? "CW" : "CCW", speed);

    return ESP_OK;
}

/* -------------------------------------------------------------------------- */

/**
 * @brief Stop the motor (coast: IN-A = L, IN-B = L, EN duty = 0).
 */
esp_err_t motor_stop(motor_t *motor)
{
    if (motor == NULL)
    {
        ESP_LOGE(TAG, "NULL motor handle");
        return ESP_ERR_INVALID_ARG;
    }

#if MOTOR_PWM_ENABLE
    /* Cut enable before releasing direction pins */
    (void)ledc_set_duty(LEDC_LOW_SPEED_MODE, motor->ledc_channel, 0U);
    (void)ledc_update_duty(LEDC_LOW_SPEED_MODE, motor->ledc_channel);
#endif

    gpio_set_level(motor->pin_in_a, 0);
    gpio_set_level(motor->pin_in_b, 0);

    ESP_LOGI(TAG, "Stopped");
    return ESP_OK;
}
