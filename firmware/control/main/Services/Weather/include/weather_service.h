/**
 * @file weather_service.h
 * @brief OpenWeather API integration for the Smart Irrigation System.
 *
 * Provides a background FreeRTOS task that periodically fetches weather data
 * from the OpenWeather API and caches the result.  On failure, the last known
 * good values are preserved; if no successful fetch has ever occurred, safe
 * defaults (rain=false, temp=25 °C) are used.
 */

#ifndef WEATHER_SERVICE_H
#define WEATHER_SERVICE_H

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
 * @brief Cached weather data.
 */
typedef struct {
    bool     rainNow;   /**< true if weather indicates rain */
    float    temp_c;    /**< Temperature in Celsius */
    uint32_t ts;        /**< Epoch timestamp of last successful fetch */
    bool     valid;     /**< true once at least one fetch succeeded */
} weather_data_t;

/* ========================================================================== */
/*                              PUBLIC API                                    */
/* ========================================================================== */

/**
 * @brief Perform a single weather fetch from the OpenWeather API.
 *
 * On success, updates the internal cache and writes @p out.
 * On failure, @p out is populated with cached / default values
 * and the error is logged.
 *
 * @param[out] out  Destination for the weather data.
 * @return ESP_OK on successful HTTP fetch, ESP_FAIL otherwise.
 */
esp_err_t weather_service_fetch(weather_data_t *out);

/**
 * @brief Get the current cached weather data (non-blocking).
 *
 * @param[out] out  Destination for the cached data.
 */
void weather_service_get_cached(weather_data_t *out);

/**
 * @brief FreeRTOS task: periodically fetches weather and writes to Firebase.
 *
 * Create with xTaskCreate().  Task parameter is unused (pass NULL).
 * Stack size recommendation: 8192 bytes.
 */
void weather_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* WEATHER_SERVICE_H */
