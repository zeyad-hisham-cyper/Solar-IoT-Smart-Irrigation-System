/**
 * @file led_driver.c
 * @brief Implementation of the reusable non-blocking LED driver.
 *
 * The blink/pulse engine uses a single per-LED esp_timer:
 *   - BLINK: a one-shot timer is re-armed at the end of every phase. On each
 *     fire the logical level is toggled and (for ON->OFF transitions) the
 *     remaining-pulse counter is decremented. When it reaches zero the LED is
 *     left OFF and the timer is not re-armed.
 *   - PULSE: the timer is armed once for the ON duration; on fire the LED is
 *     switched OFF.
 *
 * esp_timer callbacks run in the esp_timer task context, so calling
 * gpio_set_level() from them is safe.
 *
 * @version 1.0.0
 */
#include "led_driver.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "LED";

/* -------------------------------------------------------------------------- */
/* Internal helpers                                                            */
/* -------------------------------------------------------------------------- */

/** Drive the physical pin to reflect the desired logical on/off state. */
static void led_drive(led_t *led, bool on)
{
    int level = (on == led->active_high) ? 1 : 0;
    (void)gpio_set_level(led->gpio, level);
    led->phys_on = on;
}

/** Cancel a running software timer (safe if not running). */
static void led_timer_cancel(led_t *led)
{
    if (led->timer != NULL) {
        (void)esp_timer_stop(led->timer);
    }
    led->remaining = 0;
    led->one_shot  = false;
}

/** esp_timer callback driving both the blink and pulse state machines. */
static void led_timer_cb(void *arg)
{
    led_t *led = (led_t *)arg;

    if (led->one_shot) {
        /* End of a single pulse: switch off and stop. */
        led_drive(led, false);
        led->one_shot = false;
        return;
    }

    /* Blink: toggle phase. */
    bool next_on = !led->phys_on;
    led_drive(led, next_on);

    if (!next_on) {
        /* Just completed an ON phase. */
        if (led->remaining > 0) {
            led->remaining--;
            if (led->remaining == 0) {
                /* Pattern finished — leave LED off, do not re-arm. */
                return;
            }
        }
        (void)esp_timer_start_once(led->timer, (uint64_t)led->off_ms * 1000ULL);
    } else {
        (void)esp_timer_start_once(led->timer, (uint64_t)led->on_ms * 1000ULL);
    }
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                  */
/* -------------------------------------------------------------------------- */

esp_err_t led_init(led_t *led, gpio_num_t gpio, bool active_high)
{
    if (led == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(led, 0, sizeof(*led));
    led->gpio        = gpio;
    led->active_high = active_high;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << (uint32_t)gpio),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO %d config failed: %s", (int)gpio, esp_err_to_name(ret));
        return ret;
    }

    /* Create the per-LED software timer. */
    const esp_timer_create_args_t targs = {
        .callback = led_timer_cb,
        .arg      = led,
        .name     = "led",
    };
    ret = esp_timer_create(&targs, &led->timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "timer create failed: %s", esp_err_to_name(ret));
        return ret;
    }

    led->initialised = true;
    led_drive(led, false);   /* default OFF */
    return ESP_OK;
}

esp_err_t led_set(led_t *led, bool on)
{
    if (led == NULL || !led->initialised) {
        return ESP_ERR_INVALID_STATE;
    }
    led_timer_cancel(led);
    led_drive(led, on);
    return ESP_OK;
}

esp_err_t led_on(led_t *led)   { return led_set(led, true);  }
esp_err_t led_off(led_t *led)  { return led_set(led, false); }

esp_err_t led_toggle(led_t *led)
{
    if (led == NULL || !led->initialised) {
        return ESP_ERR_INVALID_STATE;
    }
    return led_set(led, !led->phys_on);
}

esp_err_t led_blink(led_t *led, uint32_t on_ms, uint32_t off_ms, int32_t count)
{
    if (led == NULL || !led->initialised) {
        return ESP_ERR_INVALID_STATE;
    }
    if (on_ms == 0 || off_ms == 0 || count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    led_timer_cancel(led);

    led->on_ms     = on_ms;
    led->off_ms    = off_ms;
    led->remaining = count;          /* >0 finite, LED_BLINK_FOREVER (-1) infinite */
    led->one_shot  = false;

    led_drive(led, true);            /* start with an ON phase */
    return esp_timer_start_once(led->timer, (uint64_t)on_ms * 1000ULL);
}

esp_err_t led_pulse(led_t *led, uint32_t duration_ms)
{
    if (led == NULL || !led->initialised) {
        return ESP_ERR_INVALID_STATE;
    }
    if (duration_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    led_timer_cancel(led);
    led->one_shot = true;
    led_drive(led, true);
    return esp_timer_start_once(led->timer, (uint64_t)duration_ms * 1000ULL);
}

esp_err_t led_stop(led_t *led)
{
    if (led == NULL || !led->initialised) {
        return ESP_ERR_INVALID_STATE;
    }
    led_timer_cancel(led);
    led_drive(led, false);
    return ESP_OK;
}
