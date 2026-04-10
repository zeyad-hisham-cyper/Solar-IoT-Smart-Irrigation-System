/**
 * @file firebase_sync.c
 * @brief Firebase Sync — upload data and poll commands implementation.
 */

#include "firebase_sync.h"
#include "firebase_config.h"
#include "firebase_auth.h"
#include "firebase_db.h"

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"

static const char *TAG = "FB_SYNC";

/* ========================================================================== */
/*                          SHARED DATA CACHE                                 */
/* ========================================================================== */

static firebase_zone_data_t s_zone_cache[NUM_ZONES] = {0};
static bool     s_pump_on      = false;
static int      s_water_level  = 100;       /* Default: full */
static firebase_commands_t s_commands = {0};

/* Zone ID strings for building paths */
static const char *ZONE_IDS[NUM_ZONES] = { ZONE_ID_1, ZONE_ID_2 };

/* Default thresholds/durations (can be overridden from the app or cloud) */
#define DEFAULT_Z1_THRESHOLD   35
#define DEFAULT_Z1_DURATION    120
#define DEFAULT_Z2_THRESHOLD   40
#define DEFAULT_Z2_DURATION    90

/* ========================================================================== */
/*                   UPDATE FUNCTIONS (called from main.c)                    */
/* ========================================================================== */

void firebase_sync_update_zone(int zone_index, const firebase_zone_data_t *data)
{
    if (zone_index < 0 || zone_index >= NUM_ZONES || data == NULL) return;
    s_zone_cache[zone_index] = *data;
    s_zone_cache[zone_index].valid = true;
}

void firebase_sync_update_gateway(bool pump_on, int water_level)
{
    s_pump_on     = pump_on;
    s_water_level = water_level;
}

void firebase_sync_get_commands(firebase_commands_t *out)
{
    if (out == NULL) return;
    *out = s_commands;
}

/* ========================================================================== */
/*                      HELPER: RE-AUTH ON 401                                 */
/* ========================================================================== */

/**
 * @brief Re-authenticate if the previous request returned ESP_ERR_INVALID_STATE (401).
 * @return ESP_OK if re-auth succeeded, ESP_FAIL otherwise.
 */
static esp_err_t try_reauth(void)
{
    ESP_LOGW(TAG, "Token expired — re-authenticating...");
    return firebase_auth_login();
}

/* ========================================================================== */
/*                         EPOCH HELPER                                       */
/* ========================================================================== */

/**
 * @brief Get a rough epoch timestamp.
 *
 * If SNTP is not configured, falls back to esp_timer uptime in seconds.
 */
static uint32_t get_epoch(void)
{
    time_t now;
    time(&now);
    if (now > 1700000000) {  /* Sanity check: after ~2023 */
        return (uint32_t)now;
    }
    /* Fallback: uptime in seconds */
    return (uint32_t)(esp_timer_get_time() / 1000000ULL);
}

/* ========================================================================== */
/*                      JSON BUILDERS                                         */
/* ========================================================================== */

/**
 * @brief Build the gateway JSON payload.
 * @return Heap-allocated string (caller must free), or NULL on error.
 */
static char *build_gateway_json(void)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return NULL;

    cJSON_AddBoolToObject(root,   "pump",       s_pump_on);
    cJSON_AddNumberToObject(root, "waterLevel", s_water_level);
    cJSON_AddNumberToObject(root, "lastSeen",   (double)get_epoch());

    char *str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return str;
}

/**
 * @brief Build a zone JSON payload for the given zone index.
 * @return Heap-allocated string (caller must free), or NULL on error.
 */
static char *build_zone_json(int zone_index)
{
    if (zone_index < 0 || zone_index >= NUM_ZONES) return NULL;
    const firebase_zone_data_t *z = &s_zone_cache[zone_index];

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return NULL;

    cJSON_AddStringToObject(root, "g",  GATEWAY_ID);
    cJSON_AddNumberToObject(root, "sm", z->soil_moisture);
    cJSON_AddNumberToObject(root, "t",  z->valid ? z->temperature : 0);
    cJSON_AddNumberToObject(root, "f",  z->valid ? z->flow_rate   : 0);
    cJSON_AddBoolToObject  (root, "v",  z->valve_open);
    cJSON_AddNumberToObject(root, "lu", (double)get_epoch());

    /* Threshold and duration — use zone-specific defaults if not set */
    int th  = z->threshold > 0 ? z->threshold :
              (zone_index == 0 ? DEFAULT_Z1_THRESHOLD : DEFAULT_Z2_THRESHOLD);
    int dur = z->duration  > 0 ? z->duration  :
              (zone_index == 0 ? DEFAULT_Z1_DURATION  : DEFAULT_Z2_DURATION);

    cJSON_AddNumberToObject(root, "th",  th);
    cJSON_AddNumberToObject(root, "dur", dur);

    char *str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return str;
}

/* ========================================================================== */
/*                       UPLOAD TASK                                          */
/* ========================================================================== */

void firebase_upload_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Upload task started (interval: %d ms)", FIREBASE_UPLOAD_INTERVAL_MS);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(FIREBASE_UPLOAD_INTERVAL_MS));

        if (!firebase_auth_is_logged_in()) {
            ESP_LOGW(TAG, "Not logged in — skipping upload");
            continue;
        }

        /* ---- Upload gateway state ---- */
        char *gw_json = build_gateway_json();
        if (gw_json != NULL) {
            char path[64];
            snprintf(path, sizeof(path), "gateways/%s", GATEWAY_ID);

            esp_err_t err = firebase_db_put(path, gw_json);
            if (err == ESP_ERR_INVALID_STATE) {
                if (try_reauth() == ESP_OK) {
                    err = firebase_db_put(path, gw_json);
                }
            }
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Gateway state uploaded");
            } else {
                ESP_LOGE(TAG, "Gateway upload failed");
            }
            free(gw_json);
        }

        /* ---- Upload zone data ---- */
        for (int i = 0; i < NUM_ZONES; i++) {
            if (!s_zone_cache[i].valid) {
                ESP_LOGW(TAG, "Zone %s: no data yet, skipping", ZONE_IDS[i]);
                continue;
            }

            char *z_json = build_zone_json(i);
            if (z_json != NULL) {
                char path[64];
                snprintf(path, sizeof(path), "zones/%s", ZONE_IDS[i]);

                esp_err_t err = firebase_db_put(path, z_json);
                if (err == ESP_ERR_INVALID_STATE) {
                    if (try_reauth() == ESP_OK) {
                        err = firebase_db_put(path, z_json);
                    }
                }
                if (err == ESP_OK) {
                    ESP_LOGI(TAG, "Zone %s data uploaded", ZONE_IDS[i]);
                } else {
                    ESP_LOGE(TAG, "Zone %s upload failed", ZONE_IDS[i]);
                }
                free(z_json);
            }
        }
    }
}

/* ========================================================================== */
/*                       POLL / COMMAND TASK                                  */
/* ========================================================================== */

/**
 * @brief Parse the commands JSON response and update s_commands.
 */
static void parse_commands(const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse commands JSON");
        s_commands.valid = false;
        return;
    }

    /* Handle "null" response (no commands written yet) */
    if (cJSON_IsNull(root)) {
        ESP_LOGD(TAG, "Commands node is null — no commands pending");
        cJSON_Delete(root);
        s_commands.valid = false;
        return;
    }

    /* pump */
    cJSON *pump_j = cJSON_GetObjectItem(root, "pump");
    if (cJSON_IsBool(pump_j)) {
        s_commands.pump = cJSON_IsTrue(pump_j);
    }

    /* ts */
    cJSON *ts_j = cJSON_GetObjectItem(root, "ts");
    if (cJSON_IsNumber(ts_j)) {
        s_commands.timestamp = (uint32_t)ts_j->valuedouble;
    }

    /* z */
    cJSON *z_obj = cJSON_GetObjectItem(root, "z");
    if (cJSON_IsObject(z_obj)) {
        /* z1 */
        cJSON *z1 = cJSON_GetObjectItem(z_obj, ZONE_ID_1);
        if (cJSON_IsObject(z1)) {
            cJSON *v = cJSON_GetObjectItem(z1, "v");
            cJSON *d = cJSON_GetObjectItem(z1, "d");
            if (cJSON_IsBool(v))   s_commands.zone_valve[0]    = cJSON_IsTrue(v);
            if (cJSON_IsNumber(d)) s_commands.zone_duration[0] = (uint16_t)d->valuedouble;
        }
        /* z2 */
        cJSON *z2 = cJSON_GetObjectItem(z_obj, ZONE_ID_2);
        if (cJSON_IsObject(z2)) {
            cJSON *v = cJSON_GetObjectItem(z2, "v");
            cJSON *d = cJSON_GetObjectItem(z2, "d");
            if (cJSON_IsBool(v))   s_commands.zone_valve[1]    = cJSON_IsTrue(v);
            if (cJSON_IsNumber(d)) s_commands.zone_duration[1] = (uint16_t)d->valuedouble;
        }
    }

    s_commands.valid = true;
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Commands parsed — pump:%d  z1(v:%d d:%d)  z2(v:%d d:%d)",
             s_commands.pump,
             s_commands.zone_valve[0], s_commands.zone_duration[0],
             s_commands.zone_valve[1], s_commands.zone_duration[1]);
}

void firebase_poll_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Poll task started (interval: %d ms)", FIREBASE_POLL_INTERVAL_MS);

    char resp_buf[FIREBASE_RESPONSE_MAX_LEN];
    char path[64];
    snprintf(path, sizeof(path), "commands/%s", GATEWAY_ID);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(FIREBASE_POLL_INTERVAL_MS));

        if (!firebase_auth_is_logged_in()) {
            ESP_LOGW(TAG, "Not logged in — skipping poll");
            continue;
        }

        esp_err_t err = firebase_db_get(path, resp_buf, sizeof(resp_buf));
        if (err == ESP_ERR_INVALID_STATE) {
            if (try_reauth() == ESP_OK) {
                err = firebase_db_get(path, resp_buf, sizeof(resp_buf));
            }
        }

        if (err == ESP_OK) {
            parse_commands(resp_buf);
        } else {
            ESP_LOGE(TAG, "Command poll failed");
            s_commands.valid = false;
        }
    }
}
