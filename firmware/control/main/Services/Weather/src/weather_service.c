/**
 * @file weather_service.c
 * @brief OpenWeather API fetch implementation.
 *
 * Uses esp_http_client to GET weather data, parses the JSON response with
 * cJSON, and caches the result.  Designed to never block the irrigation loop.
 */

#include "weather_service.h"
#include "app_config.h"
#include "firebase_config.h"
#include "firebase_db.h"
#include "firebase_auth.h"

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"

static const char *TAG = "WEATHER";

/* ========================================================================== */
/*                          INTERNAL CACHE                                    */
/* ========================================================================== */

static weather_data_t s_weather = {
    .rainNow= WEATHER_DEFAULT_RAIN,
    .temp_c = WEATHER_DEFAULT_TEMP,
    .ts     = 0,
    .valid  = false,
};

/* ========================================================================== */
/*                    HTTP RESPONSE ACCUMULATOR                               */
/* ========================================================================== */

/** Buffer for the HTTP response body. */
#define WEATHER_RESP_BUF_LEN  2048
static char s_resp_buf[WEATHER_RESP_BUF_LEN];
static size_t s_resp_len = 0;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (s_resp_len + evt->data_len < WEATHER_RESP_BUF_LEN - 1) {
            memcpy(s_resp_buf + s_resp_len, evt->data, evt->data_len);
            s_resp_len += evt->data_len;
            s_resp_buf[s_resp_len] = '\0';
        } else {
            ESP_LOGW(TAG, "Weather response truncated");
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

/* ========================================================================== */
/*                         EPOCH HELPER                                       */
/* ========================================================================== */

static uint32_t get_epoch(void)
{
    time_t now;
    time(&now);
    if (now > 1700000000) {
        return (uint32_t)now;
    }
    return (uint32_t)(esp_timer_get_time() / 1000000ULL);
}

/* ========================================================================== */
/*                         URL BUILDER                                        */
/* ========================================================================== */

/**
 * @brief Build the OpenWeather API URL from config constants.
 *
 * @param[out] url_buf   Buffer to write the URL into.
 * @param      buf_len   Size of url_buf.
 * @return ESP_OK on success.
 */
static esp_err_t build_weather_url(char *url_buf, size_t buf_len)
{
    int written = snprintf(url_buf, buf_len, WEATHER_API_URL_FMT,
                           WEATHER_API_LAT, WEATHER_API_LON, WEATHER_API_KEY);
    if (written < 0 || (size_t)written >= buf_len) {
        ESP_LOGE(TAG, "Weather URL buffer too small");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/* ========================================================================== */
/*                         PUBLIC API                                         */
/* ========================================================================== */

esp_err_t weather_service_fetch(weather_data_t *out)
{
    char url[256];
    esp_err_t err = build_weather_url(url, sizeof(url));
    if (err != ESP_OK) {
        if (out) *out = s_weather;
        return err;
    }

    /* Reset response buffer */
    s_resp_len = 0;
    memset(s_resp_buf, 0, sizeof(s_resp_buf));

    esp_http_client_config_t config = {
        .url               = url,
        .method            = HTTP_METHOD_GET,
        .event_handler     = http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms        = WEATHER_HTTP_TIMEOUT_MS,
        .buffer_size       = 2048,
        .buffer_size_tx    = 1024,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to init HTTP client for weather");
        if (out) *out = s_weather;
        return ESP_FAIL;
    }

    err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Weather HTTP request failed: %s", esp_err_to_name(err));
        if (out) *out = s_weather;
        return ESP_FAIL;
    }

    if (status != 200) {
        ESP_LOGE(TAG, "Weather API returned HTTP %d", status);
        if (out) *out = s_weather;
        return ESP_FAIL;
    }

    /* ---- Parse JSON ---- */
    cJSON *root = cJSON_Parse(s_resp_buf);
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse weather JSON");
        if (out) *out = s_weather;
        return ESP_FAIL;
    }

    /* Rain: check if "rain" key exists (any value means raining) */
    cJSON *rain_obj = cJSON_GetObjectItem(root, "rain");
    bool is_raining = (rain_obj != NULL);

    /* Temperature: main.temp is in Kelvin */
    float temp_c = WEATHER_DEFAULT_TEMP;
    cJSON *main_obj = cJSON_GetObjectItem(root, "main");
    if (cJSON_IsObject(main_obj)) {
        cJSON *temp_j = cJSON_GetObjectItem(main_obj, "temp");
        if (cJSON_IsNumber(temp_j)) {
            temp_c = (float)temp_j->valuedouble - 273.15f;
        }
    }

    cJSON_Delete(root);

    /* Update cache */
    s_weather.rainNow= is_raining;
    s_weather.temp_c = temp_c;
    s_weather.ts     = get_epoch();
    s_weather.valid  = true;

    ESP_LOGI(TAG, "Weather fetched — rain:%s  temp:%.1f°C",
             is_raining ? "YES" : "NO", temp_c);

    if (out) *out = s_weather;
    return ESP_OK;
}

void weather_service_get_cached(weather_data_t *out)
{
    if (out == NULL) return;
    *out = s_weather;
}

/* ========================================================================== */
/*                     FIREBASE WEATHER UPLOAD HELPER                         */
/* ========================================================================== */

/**
 * @brief Upload current weather data to Firebase /weather node.
 */
static void upload_weather_to_firebase(void)
{
    if (!firebase_auth_is_logged_in()) return;

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return;

    cJSON_AddBoolToObject(root, "rainNow", s_weather.rainNow);
    cJSON_AddNumberToObject(root, "temp", s_weather.temp_c);
    cJSON_AddNumberToObject(root, "ts", (double)s_weather.ts);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str == NULL) return;

    esp_err_t err = firebase_db_put("weather", json_str);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Weather data uploaded to Firebase");
    } else {
        ESP_LOGW(TAG, "Weather upload failed (will retry next cycle)");
    }

    free(json_str);
}

/* ========================================================================== */
/*                       BACKGROUND TASK                                      */
/* ========================================================================== */

void weather_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Weather task started (interval: %d ms)", WEATHER_FETCH_INTERVAL_MS);

    /* Small initial delay to let WiFi stabilize */
    vTaskDelay(pdMS_TO_TICKS(5000));

    while (1) {
        weather_data_t data;
        esp_err_t err = weather_service_fetch(&data);

        if (err == ESP_OK) {
            upload_weather_to_firebase();
        } else {
            ESP_LOGW(TAG, "Weather fetch failed — using cached: rainNow=%s temp=%.1f°C",
                     s_weather.rainNow ? "YES" : "NO", s_weather.temp_c);
        }

        vTaskDelay(pdMS_TO_TICKS(WEATHER_FETCH_INTERVAL_MS));
    }
}
