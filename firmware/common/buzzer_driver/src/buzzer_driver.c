/**
 * @file buzzer_driver.c
 * @brief Implementation of the reusable non-blocking buzzer driver.
 *
 * The pattern sequencer copies the requested steps into the handle, then walks
 * them with a one-shot esp_timer: each fire silences the current step, advances
 * the index, and applies the next step (re-arming the timer for its duration).
 * When the sequence ends it either stops (silent) or, if @c loop is set,
 * restarts from the first step (used by the alarm pattern).
 *
 * @version 1.0.0
 */
#include "buzzer_driver.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "BUZZER";

/* LEDC tone settings for passive buzzers. */
#define BUZZER_LEDC_RES        LEDC_TIMER_10_BIT
#define BUZZER_LEDC_DUTY_50    (512U)            /* 50% of 2^10 */
#define BUZZER_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define BUZZER_LEDC_INIT_FREQ  (2000U)

/* -------------------------------------------------------------------------- */
/* Low-level tone control                                                      */
/* -------------------------------------------------------------------------- */

/** Apply a single tone (freq_hz == 0 silences the buzzer). */
static void buzzer_apply_tone(buzzer_t *bz, uint16_t freq_hz)
{
    if (bz->use_ledc) {
        if (freq_hz == 0) {
            (void)ledc_set_duty(BUZZER_LEDC_MODE, bz->ledc_channel, 0);
            (void)ledc_update_duty(BUZZER_LEDC_MODE, bz->ledc_channel);
        } else {
            (void)ledc_set_freq(BUZZER_LEDC_MODE, bz->ledc_timer, freq_hz);
            (void)ledc_set_duty(BUZZER_LEDC_MODE, bz->ledc_channel, BUZZER_LEDC_DUTY_50);
            (void)ledc_update_duty(BUZZER_LEDC_MODE, bz->ledc_channel);
        }
    } else {
        /* Active buzzer: any non-zero freq means "on". */
        int on_level = bz->active_high ? 1 : 0;
        (void)gpio_set_level(bz->gpio, (freq_hz != 0) ? on_level : !on_level);
    }
}

/** esp_timer callback advancing the pattern sequencer. */
static void buzzer_timer_cb(void *arg)
{
    buzzer_t *bz = (buzzer_t *)arg;

    bz->seq_idx++;

    if (bz->seq_idx >= bz->seq_len) {
        if (bz->loop) {
            bz->seq_idx = 0;
        } else {
            buzzer_apply_tone(bz, 0);    /* done — silence */
            return;
        }
    }

    const buzzer_step_t *step = &bz->seq[bz->seq_idx];
    buzzer_apply_tone(bz, step->freq_hz);
    (void)esp_timer_start_once(bz->timer, (uint64_t)step->duration_ms * 1000ULL);
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                  */
/* -------------------------------------------------------------------------- */

esp_err_t buzzer_init(buzzer_t *bz, const buzzer_config_t *cfg)
{
    if (bz == NULL || cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(bz, 0, sizeof(*bz));
    bz->gpio         = cfg->gpio;
    bz->active_high  = cfg->active_high;
    bz->use_ledc     = cfg->use_ledc;
    bz->ledc_channel = cfg->ledc_channel;
    bz->ledc_timer   = cfg->ledc_timer;

    esp_err_t ret;

    if (bz->use_ledc) {
        ledc_timer_config_t tcfg = {
            .speed_mode      = BUZZER_LEDC_MODE,
            .duty_resolution = BUZZER_LEDC_RES,
            .timer_num       = bz->ledc_timer,
            .freq_hz         = BUZZER_LEDC_INIT_FREQ,
            .clk_cfg         = LEDC_AUTO_CLK,
        };
        ret = ledc_timer_config(&tcfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "LEDC timer config failed: %s", esp_err_to_name(ret));
            return ret;
        }

        ledc_channel_config_t ccfg = {
            .gpio_num   = bz->gpio,
            .speed_mode = BUZZER_LEDC_MODE,
            .channel    = bz->ledc_channel,
            .timer_sel  = bz->ledc_timer,
            .duty       = 0,
            .hpoint     = 0,
            .intr_type  = LEDC_INTR_DISABLE,
        };
        ret = ledc_channel_config(&ccfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "LEDC channel config failed: %s", esp_err_to_name(ret));
            return ret;
        }
    } else {
        gpio_config_t io = {
            .pin_bit_mask = (1ULL << (uint32_t)bz->gpio),
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        ret = gpio_config(&io);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "GPIO %d config failed: %s", (int)bz->gpio, esp_err_to_name(ret));
            return ret;
        }
    }

    const esp_timer_create_args_t targs = {
        .callback = buzzer_timer_cb,
        .arg      = bz,
        .name     = "buzzer",
    };
    ret = esp_timer_create(&targs, &bz->timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "timer create failed: %s", esp_err_to_name(ret));
        return ret;
    }

    bz->initialised = true;
    buzzer_apply_tone(bz, 0);   /* silent */
    return ESP_OK;
}

esp_err_t buzzer_stop(buzzer_t *bz)
{
    if (bz == NULL || !bz->initialised) {
        return ESP_ERR_INVALID_STATE;
    }
    (void)esp_timer_stop(bz->timer);
    bz->loop    = false;
    bz->seq_len = 0;
    bz->seq_idx = 0;
    buzzer_apply_tone(bz, 0);
    return ESP_OK;
}

esp_err_t buzzer_play(buzzer_t *bz, const buzzer_step_t *steps, size_t n, bool loop)
{
    if (bz == NULL || !bz->initialised || steps == NULL || n == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (n > BUZZER_MAX_STEPS) {
        n = BUZZER_MAX_STEPS;
    }

    (void)esp_timer_stop(bz->timer);

    memcpy(bz->seq, steps, n * sizeof(buzzer_step_t));
    bz->seq_len = n;
    bz->seq_idx = 0;
    bz->loop    = loop;

    buzzer_apply_tone(bz, bz->seq[0].freq_hz);
    return esp_timer_start_once(bz->timer, (uint64_t)bz->seq[0].duration_ms * 1000ULL);
}

esp_err_t buzzer_single_beep(buzzer_t *bz)
{
    static const buzzer_step_t s[] = { {2000, 120} };
    return buzzer_play(bz, s, 1, false);
}

esp_err_t buzzer_multi_beep(buzzer_t *bz, uint8_t count)
{
    if (bz == NULL || !bz->initialised) {
        return ESP_ERR_INVALID_STATE;
    }
    if (count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    buzzer_step_t s[BUZZER_MAX_STEPS];
    size_t n = 0;
    for (uint8_t i = 0; i < count && (n + 1) < BUZZER_MAX_STEPS; i++) {
        s[n].freq_hz     = 2000; s[n].duration_ms = 100; n++;   /* beep */
        s[n].freq_hz     = 0;    s[n].duration_ms = 100; n++;   /* gap  */
    }
    return buzzer_play(bz, s, n, false);
}

esp_err_t buzzer_success(buzzer_t *bz)
{
    static const buzzer_step_t s[] = {
        {1500, 100}, {0, 40}, {2500, 160},
    };
    return buzzer_play(bz, s, sizeof(s) / sizeof(s[0]), false);
}

esp_err_t buzzer_warning(buzzer_t *bz)
{
    static const buzzer_step_t s[] = {
        {1800, 180}, {0, 120}, {1800, 180},
    };
    return buzzer_play(bz, s, sizeof(s) / sizeof(s[0]), false);
}

esp_err_t buzzer_error(buzzer_t *bz)
{
    static const buzzer_step_t s[] = {
        {700, 200}, {0, 100}, {700, 200}, {0, 100}, {700, 200},
    };
    return buzzer_play(bz, s, sizeof(s) / sizeof(s[0]), false);
}

esp_err_t buzzer_alarm(buzzer_t *bz)
{
    static const buzzer_step_t s[] = {
        {2600, 300}, {0, 150}, {2000, 300}, {0, 150},
    };
    return buzzer_play(bz, s, sizeof(s) / sizeof(s[0]), true);   /* loop */
}
