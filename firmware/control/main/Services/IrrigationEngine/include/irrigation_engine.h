/**
 * @file irrigation_engine.h
 * @brief Irrigation Decision Engine for the Smart Irrigation System.
 *
 * Centralises all irrigation logic: auto mode (sensor + weather based),
 * manual mode (Firebase command based), pump sequencing, valve control,
 * and safety constraints.  Designed to run as a periodic FreeRTOS task
 * on the Control Board.
 */

#ifndef IRRIGATION_ENGINE_H
#define IRRIGATION_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                            DATA STRUCTURES                                 */
/* ========================================================================== */

/**
 * @brief Per-zone state tracked by the irrigation engine.
 */
typedef struct {
    uint8_t  sm;              /**< Soil moisture % (0-100) */
    float    temp;            /**< Temperature °C */
    float    flow;            /**< Flow sensor value */
    bool     valve_open;      /**< Current commanded valve state */
    uint32_t last_update;     /**< Tick count of last sensor data received */
    uint8_t  threshold;       /**< Moisture threshold (from Firebase config) */
    uint16_t duration;        /**< Irrigation duration in seconds */
    bool     data_valid;      /**< true once at least one valid reading received */
    bool     zone_active;     /**< true if zone is responding (not timed out) */
} irrigation_zone_t;

/**
 * @brief Operating modes for the irrigation engine.
 */
typedef enum {
    IRRIGATION_MODE_AUTO   = 0,  /**< Automatic: sensor + weather based */
    IRRIGATION_MODE_MANUAL = 1,  /**< Manual: Firebase command based */
} irrigation_mode_t;

/* ========================================================================== */
/*                              PUBLIC API                                    */
/* ========================================================================== */

/**
 * @brief Initialise the irrigation engine.
 *
 * Must be called once before irrigation_engine_run_cycle().
 * Sets up the mutex, default thresholds, and initial state.
 */
void irrigation_engine_init(void);

/**
 * @brief Run one irrigation decision cycle.
 *
 * Reads cached sensor data + weather, evaluates each zone,
 * and issues pump/valve commands as needed.  Non-blocking.
 *
 * Call this periodically from a FreeRTOS task.
 */
void irrigation_engine_run_cycle(void);

/**
 * @brief Update zone sensor data from an ESP-NOW packet.
 *
 * Called from the ESP-NOW receive callback when a ZONE_DATA packet arrives.
 *
 * @param zone_id      1-based zone ID.
 * @param moisture     Soil moisture percentage (0-100).
 * @param temperature  Temperature in °C (use NAN if invalid).
 * @param flow         Flow rate / cumulative volume.
 * @return ESP_OK, or ESP_ERR_INVALID_ARG if values are out of range.
 */
esp_err_t irrigation_engine_update_zone(uint8_t zone_id, uint8_t moisture,
                                         float temperature, float flow);

/**
 * @brief Set the operating mode.
 *
 * @param mode  IRRIGATION_MODE_AUTO or IRRIGATION_MODE_MANUAL.
 */
void irrigation_engine_set_mode(irrigation_mode_t mode);

/**
 * @brief Get the current operating mode.
 */
irrigation_mode_t irrigation_engine_get_mode(void);

/**
 * @brief Update zone configuration (threshold and duration) from Firebase.
 *
 * @param zone_id    1-based zone ID.
 * @param threshold  Moisture threshold %.
 * @param duration   Irrigation duration in seconds.
 */
void irrigation_engine_set_zone_config(uint8_t zone_id, uint8_t threshold,
                                        uint16_t duration);

/**
 * @brief Check if a specific zone is currently irrigating.
 *
 * @param zone_id  1-based zone ID.
 * @return true if the zone's valve is commanded open.
 */
bool irrigation_engine_is_zone_irrigating(uint8_t zone_id);

/**
 * @brief Check if the pump is currently running.
 */
bool irrigation_engine_is_pump_on(void);

/**
 * @brief Get a snapshot of a zone's state (for Firebase upload).
 *
 * @param zone_id  1-based zone ID.
 * @param[out] out  Destination for the zone state copy.
 * @return ESP_OK if valid, ESP_ERR_INVALID_ARG if zone_id is out of range.
 */
esp_err_t irrigation_engine_get_zone_state(uint8_t zone_id,
                                            irrigation_zone_t *out);

#ifdef __cplusplus
}
#endif

#endif /* IRRIGATION_ENGINE_H */
