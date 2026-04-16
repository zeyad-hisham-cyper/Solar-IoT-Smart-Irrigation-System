/**
 * @file firebase_sync.c
 * @brief Firebase Sync — upload data and poll commands/config implementation.
 *
 * Handles:
 *  - Periodic upload of gateway state, zone sensor data to Firebase
 *  - Polling gateway mode ("auto"/"manual"), zone configs (th, dur), and
 *    manual commands from /commands/g1
 *  - All reads/writes have failure handling: log and retry, never block
 *  - On read failure: preserve last known config values
 */

#include "firebase_sync.h"
#include "firebase_config.h"
#include "firebase_auth.h"
#include "firebase_db.h"
#include "irrigation_engine.h"
#include "app_config.h"

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
static int      s_water_level  = 0;
static firebase_commands_t s_commands = {0};

/* Cached gateway mode string */
static char s_gateway_mode[16] = "auto";

/* Upload task handle for immediate triggering */
static TaskHandle_t s_upload_task_handle = NULL;

/* Cached zone configs from Firebase */
static uint8_t  s_zone_threshold[NUM_ZONES] = { DEFAULT_Z1_THRESHOLD, DEFAULT_Z2_THRESHOLD };
static uint16_t s_zone_duration[NUM_ZONES]  = { DEFAULT_Z1_DURATION,  DEFAULT_Z2_DURATION };

/* Zone ID strings for building paths */
static const char *ZONE_IDS[NUM_ZONES] = { ZONE_ID_1, ZONE_ID_2 };

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

void firebase_sync_get_gateway_mode(char *mode_out, size_t len)
{
    if (mode_out == NULL || len == 0) return;
    strncpy(mode_out, s_gateway_mode, len - 1);
    mode_out[len - 1] = '\0';
}

void firebase_sync_get_zone_config(int zone_index, uint8_t *threshold,
                                    uint16_t *duration)
{
    if (zone_index < 0 || zone_index >= NUM_ZONES) return;
    if (threshold) *threshold = s_zone_threshold[zone_index];
    if (duration)  *duration  = s_zone_duration[zone_index];
}

void firebase_sync_trigger_immediate_upload(void)
{
    if (s_upload_task_handle != NULL) {
        xTaskNotifyGive(s_upload_task_handle);
    }
}

/* ========================================================================== */
/*                      HELPER: RE-AUTH ON 401                                 */
/* ========================================================================== */

static esp_err_t try_reauth(void)
{
    ESP_LOGW(TAG, "Token expired — re-authenticating...");
    return firebase_auth_login();
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

    cJSON_AddStringToObject(root, "mode",       s_gateway_mode);
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

    /* Get live state from the irrigation engine if available */
    irrigation_zone_t eng_state;
    bool have_engine_state = false;
    uint8_t zone_id = (uint8_t)(zone_index + 1);
    if (irrigation_engine_get_zone_state(zone_id, &eng_state) == ESP_OK) {
        have_engine_state = true;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return NULL;

    cJSON_AddStringToObject(root, "g",  GATEWAY_ID);

    if (have_engine_state && eng_state.data_valid) {
        cJSON_AddNumberToObject(root, "sm", eng_state.sm);
        cJSON_AddNumberToObject(root, "t",  eng_state.temp);
        cJSON_AddNumberToObject(root, "f",  eng_state.flow);
        cJSON_AddBoolToObject  (root, "v",  eng_state.valve_open);
        cJSON_AddNumberToObject(root, "th", eng_state.threshold);
        cJSON_AddNumberToObject(root, "dur",eng_state.duration);
    } else {
        /* Fallback to the zone cache */
        const firebase_zone_data_t *z = &s_zone_cache[zone_index];
        cJSON_AddNumberToObject(root, "sm", z->soil_moisture);
        cJSON_AddNumberToObject(root, "t",  z->valid ? z->temperature : 0);
        cJSON_AddNumberToObject(root, "f",  z->valid ? z->flow_rate   : 0);
        cJSON_AddBoolToObject  (root, "v",  z->valve_open);

        int th  = z->threshold > 0 ? z->threshold :
                  (zone_index == 0 ? DEFAULT_Z1_THRESHOLD : DEFAULT_Z2_THRESHOLD);
        int dur = z->duration  > 0 ? z->duration  :
                  (zone_index == 0 ? DEFAULT_Z1_DURATION  : DEFAULT_Z2_DURATION);
        cJSON_AddNumberToObject(root, "th",  th);
        cJSON_AddNumberToObject(root, "dur", dur);
    }

    cJSON_AddNumberToObject(root, "lu", (double)get_epoch());

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

    s_upload_task_handle = xTaskGetCurrentTaskHandle();

    while (1) {
        /* Wait for periodic interval or immediate trigger */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(FIREBASE_UPLOAD_INTERVAL_MS));

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
                ESP_LOGE(TAG, "Gateway upload failed (will retry next cycle)");
            }
            free(gw_json);
        }

        /* ---- Upload zone data ---- */
        for (int i = 0; i < NUM_ZONES; i++) {
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
        /* Keep previous valid commands — do not reset */
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

/**
 * @brief Parse the gateway JSON to extract mode.
 */
static void parse_gateway_mode(const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL || cJSON_IsNull(root)) {
        ESP_LOGW(TAG, "Failed to parse gateway JSON for mode — keeping: %s", s_gateway_mode);
        if (root) cJSON_Delete(root);
        return;
    }

    cJSON *mode_j = cJSON_GetObjectItem(root, "mode");
    if (cJSON_IsString(mode_j) && mode_j->valuestring != NULL) {
        strncpy(s_gateway_mode, mode_j->valuestring, sizeof(s_gateway_mode) - 1);
        s_gateway_mode[sizeof(s_gateway_mode) - 1] = '\0';
        ESP_LOGI(TAG, "Gateway mode: %s", s_gateway_mode);
    }

    cJSON_Delete(root);
}

/**
 * @brief Parse a zone JSON to extract th and dur, and feed to irrigation engine.
 */
static void parse_zone_config(int zone_index, const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL || cJSON_IsNull(root)) {
        ESP_LOGW(TAG, "Zone %s config parse failed — keeping previous values",
                 ZONE_IDS[zone_index]);
        if (root) cJSON_Delete(root);
        return;
    }

    cJSON *th_j  = cJSON_GetObjectItem(root, "th");
    cJSON *dur_j = cJSON_GetObjectItem(root, "dur");

    if (cJSON_IsNumber(th_j) && th_j->valuedouble > 0) {
        s_zone_threshold[zone_index] = (uint8_t)th_j->valuedouble;
    }
    if (cJSON_IsNumber(dur_j) && dur_j->valuedouble > 0) {
        s_zone_duration[zone_index] = (uint16_t)dur_j->valuedouble;
    }

    cJSON_Delete(root);

    /* Feed updated config to the irrigation engine */
    uint8_t zone_id = (uint8_t)(zone_index + 1);
    irrigation_engine_set_zone_config(zone_id,
                                       s_zone_threshold[zone_index],
                                       s_zone_duration[zone_index]);

    ESP_LOGI(TAG, "Zone %s config: th=%u dur=%u",
             ZONE_IDS[zone_index],
             s_zone_threshold[zone_index],
             s_zone_duration[zone_index]);
}

void firebase_poll_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Poll task started (interval: %d ms)", FIREBASE_POLL_INTERVAL_MS);

    char resp_buf[FIREBASE_RESPONSE_MAX_LEN];

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(FIREBASE_POLL_INTERVAL_MS));

        if (!firebase_auth_is_logged_in()) {
            ESP_LOGW(TAG, "Not logged in — skipping poll");
            continue;
        }

        /* ---- 1. Poll gateway mode ---- */
        {
            char path[64];
            snprintf(path, sizeof(path), "gateways/%s", GATEWAY_ID);

            esp_err_t err = firebase_db_get(path, resp_buf, sizeof(resp_buf));
            if (err == ESP_ERR_INVALID_STATE) {
                if (try_reauth() == ESP_OK) {
                    err = firebase_db_get(path, resp_buf, sizeof(resp_buf));
                }
            }
            if (err == ESP_OK) {
                parse_gateway_mode(resp_buf);

                /* Update irrigation engine mode */
                if (strcmp(s_gateway_mode, "manual") == 0) {
                    irrigation_engine_set_mode(IRRIGATION_MODE_MANUAL);
                } else {
                    irrigation_engine_set_mode(IRRIGATION_MODE_AUTO);
                }
            } else {
                ESP_LOGW(TAG, "Gateway mode poll failed — keeping: %s", s_gateway_mode);
            }
        }

        /* ---- 2. Poll zone configs (th, dur) ---- */
        for (int i = 0; i < NUM_ZONES; i++) {
            char path[64];
            snprintf(path, sizeof(path), "zones/%s", ZONE_IDS[i]);

            esp_err_t err = firebase_db_get(path, resp_buf, sizeof(resp_buf));
            if (err == ESP_ERR_INVALID_STATE) {
                if (try_reauth() == ESP_OK) {
                    err = firebase_db_get(path, resp_buf, sizeof(resp_buf));
                }
            }
            if (err == ESP_OK) {
                parse_zone_config(i, resp_buf);
            } else {
                ESP_LOGW(TAG, "Zone %s config poll failed — keeping previous", ZONE_IDS[i]);
            }
        }

        /* ---- 3. Poll commands (only relevant in manual mode) ---- */
        {
            char path[64];
            snprintf(path, sizeof(path), "commands/%s", GATEWAY_ID);

            esp_err_t err = firebase_db_get(path, resp_buf, sizeof(resp_buf));
            if (err == ESP_ERR_INVALID_STATE) {
                if (try_reauth() == ESP_OK) {
                    err = firebase_db_get(path, resp_buf, sizeof(resp_buf));
                }
            }
            if (err == ESP_OK) {
                parse_commands(resp_buf);
            } else {
                ESP_LOGW(TAG, "Command poll failed — keeping previous commands");
                /* Do NOT invalidate s_commands — keep last known */
            }
        }
    }
}
