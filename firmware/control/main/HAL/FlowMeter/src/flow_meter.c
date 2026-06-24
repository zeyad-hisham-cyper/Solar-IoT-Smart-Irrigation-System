/**
 * @file flow_meter.c
 * @brief YFS401 Hall-effect flow meter HAL driver for ESP32 (ESP-IDF) – implementation.
 *
 * @version 1.0.0
 */

#include "flow_meter.h"
#include "esp_timer.h"

/* ========================================================================== */
/*                            PRIVATE DEFINITIONS                             */
/* ========================================================================== */

static const char *TAG = "FLOW_METER";

/** Internal state shared between the ISR and the public API. */
static volatile uint32_t s_pulse_count  = 0U;  /**< Rising-edge pulse counter (ISR-written) */
static uint64_t          s_last_call_us = 0U;  /**< Timestamp of the last flow-rate call   */

/* ========================================================================== */
/*                              ISR HANDLER                                   */
/* ========================================================================== */

/**
 * @brief GPIO ISR – increments the pulse counter on every rising edge.
 *        Runs in IRAM; must be as short as possible.
 */
static void IRAM_ATTR flow_meter_isr_handler(void *arg)
{
    (void)arg;
    s_pulse_count++;
}

/* ========================================================================== */
/*                          LOCAL UTILITY                                     */
/* ========================================================================== */

static esp_err_t flow_check_handle(const flow_meter_t *flow)
{
    if (flow == NULL)
    {
        ESP_LOGE(TAG, "NULL flow meter handle");
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

/* ========================================================================== */
/*                          PUBLIC FUNCTION DEFINITIONS                       */
/* ========================================================================== */

/**
 * @brief Initialise the flow meter driver.
 */
esp_err_t flow_meter_init(flow_meter_t *flow)
{
    esp_err_t ret = flow_check_handle(flow);
    if (ret != ESP_OK) { return ret; }

    /* Configure signal pin as input with pull-up */
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << (uint32_t)flow->signal_pin),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,    /* YFS401 open-drain output */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_POSEDGE,     /* Count on rising edge     */
    };

    ret = gpio_config(&io_cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "GPIO config failed (%s)", esp_err_to_name(ret));
        return ret;
    }

    /* Install global GPIO ISR service (safe to call multiple times) */
    gpio_install_isr_service(0);

    /* Attach ISR to this specific pin */
    ret = gpio_isr_handler_add(flow->signal_pin, flow_meter_isr_handler, NULL);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "ISR handler add failed (%s)", esp_err_to_name(ret));
        return ret;
    }

    /* Clear runtime state */
    s_pulse_count          = 0U;
    s_last_call_us         = (uint64_t)esp_timer_get_time();
    flow->flow_rate_lpm    = 0.0f;
    flow->total_volume_ml  = 0U;

    ESP_LOGI(TAG, "Initialised on GPIO %d  (%.0f pulses/L)",
             (int)flow->signal_pin, (float)FLOW_METER_PULSES_PER_LITER);

    return ESP_OK;
}

/* -------------------------------------------------------------------------- */

/**
 * @brief Compute the instantaneous flow rate.
 *
 * Atomically snapshots and clears s_pulse_count, then uses the elapsed time
 * since the last call to derive L/min.
 */
esp_err_t flow_meter_get_flow_rate(flow_meter_t *flow, float *flow_rate_lpm)
{
    esp_err_t ret = flow_check_handle(flow);
    if (ret != ESP_OK) { return ret; }

    /* Snapshot and reset the pulse counter atomically */
    portDISABLE_INTERRUPTS();
    uint32_t pulses   = s_pulse_count;
    s_pulse_count     = 0U;
    portENABLE_INTERRUPTS();

    uint64_t now_us      = (uint64_t)esp_timer_get_time();
    uint64_t elapsed_us  = now_us - s_last_call_us;
    s_last_call_us       = now_us;

    /* Guard against division by zero on very first call */
    if (elapsed_us == 0U)
    {
        flow->flow_rate_lpm = 0.0f;
        if (flow_rate_lpm != NULL) { *flow_rate_lpm = 0.0f; }
        return ESP_OK;
    }

    /*
     * flow_rate (L/min) = (pulses / PULSES_PER_LITER) / (elapsed_s / 60)
     *                   = pulses * 60 / (PULSES_PER_LITER * elapsed_s)
     */
    float elapsed_s     = (float)elapsed_us / 1000000.0f;
    float rate_lpm      = ((float)pulses * 60.0f) /
                          ((float)FLOW_METER_PULSES_PER_LITER * elapsed_s);

    flow->flow_rate_lpm = rate_lpm;

    /* Accumulate volume: volume_mL = (pulses / PULSES_PER_LITER) * 1000 */
    flow->total_volume_ml += (uint32_t)((pulses * 1000U) / FLOW_METER_PULSES_PER_LITER);

    ESP_LOGD(TAG, "Pulses=%"PRIu32"  elapsed=%.2f s  rate=%.2f L/min  total=%"PRIu32" mL",
             pulses, elapsed_s, rate_lpm, flow->total_volume_ml);

    if (flow_rate_lpm != NULL) { *flow_rate_lpm = rate_lpm; }

    return ESP_OK;
}

/* -------------------------------------------------------------------------- */

/**
 * @brief Get the cumulative volume dispensed (mL).
 */
esp_err_t flow_meter_get_total_volume_ml(flow_meter_t *flow, uint32_t *volume_ml)
{
    esp_err_t ret = flow_check_handle(flow);
    if (ret != ESP_OK) { return ret; }

    if (volume_ml != NULL) { *volume_ml = flow->total_volume_ml; }

    return ESP_OK;
}

/* -------------------------------------------------------------------------- */

/**
 * @brief Reset cumulative volume and pulse counter.
 */
esp_err_t flow_meter_reset(flow_meter_t *flow)
{
    esp_err_t ret = flow_check_handle(flow);
    if (ret != ESP_OK) { return ret; }

    portDISABLE_INTERRUPTS();
    s_pulse_count = 0U;
    portENABLE_INTERRUPTS();

    flow->total_volume_ml = 0U;
    flow->flow_rate_lpm   = 0.0f;
    s_last_call_us        = (uint64_t)esp_timer_get_time();

    ESP_LOGI(TAG, "Reset – volume and pulse counter cleared");
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */

/**
 * @brief De-initialise the driver and release the GPIO interrupt.
 */
esp_err_t flow_meter_deinit(flow_meter_t *flow)
{
    esp_err_t ret = flow_check_handle(flow);
    if (ret != ESP_OK) { return ret; }

    gpio_isr_handler_remove(flow->signal_pin);
    gpio_reset_pin(flow->signal_pin);

    portDISABLE_INTERRUPTS();
    s_pulse_count = 0U;
    portENABLE_INTERRUPTS();

    flow->flow_rate_lpm   = 0.0f;
    flow->total_volume_ml = 0U;

    ESP_LOGI(TAG, "Deinit on GPIO %d", (int)flow->signal_pin);
    return ESP_OK;
}
