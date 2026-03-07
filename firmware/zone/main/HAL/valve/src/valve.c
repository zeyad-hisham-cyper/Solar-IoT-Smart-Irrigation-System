/**
 * @file valve.c
 * @brief Solenoid/latching valve HAL driver for ESP32 (ESP-IDF).
 *
 * Controls a single GPIO-driven valve.  The driver tracks cumulative open
 * time and open-event count so the application layer can log irrigation
 * statistics without maintaining extra state.
 *
 * @version 1.0.0
 */

#include "valve.h"
#include "esp_timer.h"

/* ========================================================================== */
/*                              PRIVATE DEFINITIONS                           */
/* ========================================================================== */

static const char *TAG = "VALVE";

/** GPIO output level aliases */
#define VALVE_GPIO_OPEN   (1)
#define VALVE_GPIO_CLOSED (0)

/* ========================================================================== */
/*                            LOCAL UTILITY FUNCTIONS                         */
/* ========================================================================== */

/**
 * @brief Validate that the valve handle pointer is not NULL.
 * @param[in] valve  Pointer to valve handle.
 * @return ESP_OK if valid, ESP_ERR_INVALID_ARG otherwise.
 */
static esp_err_t valve_check_handle(const valve_t *valve)
{
    if (valve == NULL)
    {
        ESP_LOGE(TAG, "NULL valve handle");
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

/* ========================================================================== */
/*                          PUBLIC FUNCTION DEFINITIONS                       */
/* ========================================================================== */

/**
 * @brief Initialise the valve driver and configure the control GPIO.
 *
 * Sets the GPIO as a push-pull output and drives it LOW (valve closed).
 * All statistics counters are cleared.
 *
 * @param[in,out] valve  Pointer to a caller-allocated valve_t structure that
 *                       has at least `control_pin` and `timeout_ms` filled in.
 * @return ESP_OK on success, or an ESP-IDF error code.
 */
esp_err_t valve_init(valve_t *valve)
{
    esp_err_t ret = valve_check_handle(valve);
    if (ret != ESP_OK)
    {
        return ret;
    }

    /* Configure the control GPIO as a digital output */
    gpio_config_t io_cfg = {
        .pin_bit_mask  = (1ULL << (uint32_t)valve->control_pin),
        .mode          = GPIO_MODE_OUTPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };

    ret = gpio_config(&io_cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "GPIO config failed (%s)", esp_err_to_name(ret));
        return ret;
    }

    /* Drive GPIO low → valve physically closed */
    (void)gpio_set_level(valve->control_pin, VALVE_GPIO_CLOSED);

    /* Initialise statistics */
    valve->state               = VALVE_STATE_CLOSED;
    valve->total_open_time_ms  = 0U;
    valve->open_count          = 0U;
    valve->open_start_us       = 0U;

    ESP_LOGI(TAG, "Initialised on GPIO %d (timeout=%"PRIu32" ms)",
             (int)valve->control_pin, valve->timeout_ms);

    return ESP_OK;
}

/* -------------------------------------------------------------------------- */

/**
 * @brief Open the valve (set GPIO HIGH).
 *
 * Records the open-start timestamp so that the elapsed open time can be
 * accumulated when valve_close() is called.
 *
 * @param[in,out] valve  Initialised valve handle.
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already open.
 */
esp_err_t valve_open(valve_t *valve)
{
    esp_err_t ret = valve_check_handle(valve);
    if (ret != ESP_OK)
    {
        return ret;
    }

    if (valve->state == VALVE_STATE_OPENED)
    {
        /* Already open, no-op. Silently return ESP_OK so repeated commands from Control do not throw an error. */
        return ESP_OK;
    }

    valve->state = VALVE_STATE_OPENING;

    ESP_LOGI(TAG, ">>> HARDWARE ACTION: Driving GPIO %d to HIGH (OPEN) <<<", valve->control_pin);
    (void)gpio_set_level(valve->control_pin, VALVE_GPIO_OPEN);

    /* Record start time in microseconds */
    valve->open_start_us = (uint64_t)esp_timer_get_time();

    valve->state = VALVE_STATE_OPENED;
    valve->open_count++;

    ESP_LOGI(TAG, "Opened  (count=%"PRIu32")", valve->open_count);

    return ESP_OK;
}

/* -------------------------------------------------------------------------- */

/**
 * @brief Close the valve (set GPIO LOW) and accumulate open time.
 *
 * @param[in,out] valve  Initialised valve handle.
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already closed.
 */
esp_err_t valve_close(valve_t *valve)
{
    esp_err_t ret = valve_check_handle(valve);
    if (ret != ESP_OK)
    {
        return ret;
    }

    if (valve->state == VALVE_STATE_CLOSED)
    {
        /* Already closed, no-op. Silently return ESP_OK so repeated commands from Control do not throw an error. */
        return ESP_OK;
    }

    valve->state = VALVE_STATE_CLOSING;

    ESP_LOGI(TAG, ">>> HARDWARE ACTION: Driving GPIO %d to LOW (CLOSED) <<<", valve->control_pin);
    (void)gpio_set_level(valve->control_pin, VALVE_GPIO_CLOSED);

    /* Accumulate open duration */
    if (valve->open_start_us != 0U)
    {
        uint64_t now_us     = (uint64_t)esp_timer_get_time();
        uint64_t elapsed_us = now_us - valve->open_start_us;
        valve->total_open_time_ms += (uint32_t)(elapsed_us / 1000ULL);
        valve->open_start_us = 0U;
    }

    valve->state = VALVE_STATE_CLOSED;

    ESP_LOGI(TAG, "Closed  (total open time=%"PRIu32" ms, count=%"PRIu32")",
             valve->total_open_time_ms, valve->open_count);

    return ESP_OK;
}

/* -------------------------------------------------------------------------- */

/**
 * @brief Get the current valve state.
 *
 * @param[in] valve  Initialised valve handle.
 * @return Current valve_state_t, or VALVE_STATE_ERROR if handle is NULL.
 */
valve_state_t valve_get_state(valve_t *valve)
{
    if (valve == NULL)
    {
        return VALVE_STATE_ERROR;
    }
    return valve->state;
}

/* -------------------------------------------------------------------------- */

/**
 * @brief De-initialise the valve driver.
 *
 * Closes the valve if open, then resets the GPIO to its default state.
 *
 * @param[in,out] valve  Initialised valve handle.
 * @return ESP_OK on success.
 */
esp_err_t valve_deinit(valve_t *valve)
{
    esp_err_t ret = valve_check_handle(valve);
    if (ret != ESP_OK)
    {
        return ret;
    }

    /* Ensure valve is physically closed before releasing the GPIO */
    if (valve->state != VALVE_STATE_CLOSED)
    {
        (void)valve_close(valve);
    }

    /* Reset GPIO to input (high-impedance) */
    (void)gpio_reset_pin(valve->control_pin);

    valve->state = VALVE_STATE_CLOSED;
    valve->open_start_us = 0U;

    ESP_LOGI(TAG, "Deinit on GPIO %d", (int)valve->control_pin);

    return ESP_OK;
}

/* -------------------------------------------------------------------------- */

/**
 * @brief Set the auto-close timeout.
 *
 * The timeout value is stored in the handle for use by higher-level logic
 * (e.g. a watchdog task).  The driver itself does not enforce the timeout
 * automatically.
 *
 * @param[in,out] valve       Initialised valve handle.
 * @param[in]     timeout_ms  Timeout in milliseconds.
 * @return ESP_OK on success.
 */
esp_err_t valve_setTimeOut(valve_t *valve, uint32_t timeout_ms)
{
    esp_err_t ret = valve_check_handle(valve);
    if (ret != ESP_OK)
    {
        return ret;
    }

    valve->timeout_ms = timeout_ms;
    ESP_LOGD(TAG, "Timeout set to %"PRIu32" ms", valve->timeout_ms);

    return ESP_OK;
}

/* -------------------------------------------------------------------------- */

/**
 * @brief Get the cumulative open time.
 *
 * @param[in]  valve              Initialised valve handle.
 * @param[out] total_open_time_ms Pointer to store the result.
 * @return ESP_OK on success.
 */
esp_err_t Valve_GetTotalOpenTime(valve_t *valve, uint32_t *total_open_time_ms)
{
    esp_err_t ret = valve_check_handle(valve);
    if (ret != ESP_OK)
    {
        return ret;
    }

    if (total_open_time_ms == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    *total_open_time_ms = valve->total_open_time_ms;
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */

/**
 * @brief Get the total number of open events.
 *
 * @param[in]  valve       Initialised valve handle.
 * @param[out] open_count  Pointer to store the result.
 * @return ESP_OK on success.
 */
esp_err_t Valve_GetOpenCount(valve_t *valve, uint32_t *open_count)
{
    esp_err_t ret = valve_check_handle(valve);
    if (ret != ESP_OK)
    {
        return ret;
    }

    if (open_count == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    *open_count = valve->open_count;
    return ESP_OK;
}
