/**
 * @file flow_meter.h
 * @brief YFS401 Hall-effect flow meter HAL driver for ESP32 (ESP-IDF).
 *
 * Counts pulses from the sensor via a GPIO interrupt and exposes:
 *   - Instantaneous flow rate (L/min)
 *   - Cumulative total volume (mL)
 *
 * Calibration
 * -----------
 * The YFS401 outputs F(Hz) = 98 × Q(L/min), so there are 5880 pulses per
 * litre at any flow rate.  Override FLOW_METER_PULSES_PER_LITER with your
 * measured value if your sensor differs.
 *
 * Usage
 * -----
 *   1. Fill in flow_meter_t hardware fields.
 *   2. Call flow_meter_init().
 *   3. Call flow_meter_get_flow_rate() / flow_meter_get_total_volume_ml()
 *      from a periodic task (every 1–5 s recommended).
 *   4. Optionally call flow_meter_reset() to clear totals.
 *
 * @version 1.0.0
 */

#ifndef FLOW_METER_H_
#define FLOW_METER_H_

#include <stdint.h>
#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                              CONFIGURATION                                 */
/* ========================================================================== */

/**
 * Pulses the sensor outputs per litre of water.
 * YFS401 datasheet: F(Hz) = 98 × Q(L/min)  →  98 × 60 = 5880 pulses / L.
 * Replace with your own calibrated value for higher accuracy.
 */
#ifndef FLOW_METER_PULSES_PER_LITER
#define FLOW_METER_PULSES_PER_LITER  (5880U)
#endif

/* ========================================================================== */
/*                              TYPE DEFINITIONS                               */
/* ========================================================================== */

/**
 * @brief Flow meter handle.
 *
 * Populate the hardware field(s) then pass to flow_meter_init().
 * All other fields are managed by the driver – do not modify them.
 */
typedef struct {
    /* -- Hardware configuration [IN] --------------------------------------- */
    gpio_num_t signal_pin;          /**< GPIO connected to sensor signal wire */

    /* -- Runtime state [OUT] ----------------------------------------------- */
    float    flow_rate_lpm;         /**< Last computed flow rate (L/min)      */
    uint32_t total_volume_ml;       /**< Cumulative volume dispensed (mL)     */
} flow_meter_t;

/* ========================================================================== */
/*                             FUNCTION PROTOTYPES                            */
/* ========================================================================== */

/**
 * @brief Initialise the flow meter driver.
 *
 * Configures the signal GPIO as an input with a rising-edge interrupt that
 * increments an internal pulse counter.  Clears all statistics.
 *
 * @param[in,out] flow  Handle with signal_pin set by the caller.
 * @return ESP_OK on success, or an ESP-IDF error code.
 */
esp_err_t flow_meter_init(flow_meter_t *flow);

/**
 * @brief Compute the instantaneous flow rate.
 *
 * Reads and resets the internal pulse counter, then derives the flow rate
 * from the elapsed time since the last call.  Call this function at a fixed
 * interval (e.g. every 1 s) for consistent results.
 *
 * @param[in,out] flow         Initialised flow meter handle.
 * @param[out]    flow_rate_lpm Resolved flow rate in L/min (can be NULL if not needed).
 * @return ESP_OK on success.
 */
esp_err_t flow_meter_get_flow_rate(flow_meter_t *flow, float *flow_rate_lpm);

/**
 * @brief Get the cumulative volume dispensed since init (or last reset).
 *
 * @param[in]  flow            Initialised flow meter handle.
 * @param[out] volume_ml       Destination for the result in mL (can be NULL).
 * @return ESP_OK on success.
 */
esp_err_t flow_meter_get_total_volume_ml(flow_meter_t *flow, uint32_t *volume_ml);

/**
 * @brief Reset cumulative volume and internal pulse counter to zero.
 *
 * Does not affect the GPIO or interrupt configuration.
 *
 * @param[in,out] flow  Initialised flow meter handle.
 * @return ESP_OK on success.
 */
esp_err_t flow_meter_reset(flow_meter_t *flow);

/**
 * @brief De-initialise the driver and release the GPIO interrupt.
 *
 * @param[in,out] flow  Initialised flow meter handle.
 * @return ESP_OK on success.
 */
esp_err_t flow_meter_deinit(flow_meter_t *flow);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_METER_H_ */
