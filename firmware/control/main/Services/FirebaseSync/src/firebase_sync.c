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
#include "wifi_manager.h"
#include "esp_wifi.h"

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"

static const char *TAG = "FB_SYNC";

/* Forward declaration */
static double get_epoch_ms(void);

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

/* ---- Command acknowledgement queue (engine -> upload task) ---- */
typedef struct {
    bool   pending;                              /**< true if an ack awaits upload */
    char   command_id[FIREBASE_COMMAND_ID_MAX];  /**< id being acknowledged */
    char   status[16];                           /**< "executed" | "failed" */
    char   message[64];                          /**< human-readable detail */
    double executed_at;                          /**< epoch ms when applied */
} ack_slot_t;

static ack_slot_t      s_ack[NUM_ZONES];
static portMUX_TYPE    s_ack_mux = portMUX_INITIALIZER_UNLOCKED;

/* ---- History bookkeeping (per zone) ---- */
static double   s_hist_last_time[NUM_ZONES]   = {0};      /**< epoch ms of last record (0 = none yet) */
static bool     s_hist_valve_last[NUM_ZONES]  = {false};  /**< valve state at last record */
static bool     s_hist_valve_known[NUM_ZONES] = {false};  /**< valid s_hist_valve_last */
static uint32_t s_hist_vol_last[NUM_ZONES]    = {0};      /**< cumulative volume at last record (mL) */
static bool     s_hist_vol_known[NUM_ZONES]   = {false};  /**< valid s_hist_vol_last */

/* ========================================================================== */
/*                   UPDATE FUNCTIONS (called from main.c)                    */
/* ========================================================================== */

void firebase_sync_update_zone(int zone_index, const firebase_zone_data_t *data)
{
    if (zone_index < 0 || zone_index >= NUM_ZONES || data == NULL) return;

    double prev_lu = s_zone_cache[zone_index].last_update;
    s_zone_cache[zone_index] = *data;

    /* Only stamp a real epoch; keep the previous one until SNTP has synced
     * (get_epoch_ms() returns -1 before time is valid). */
    double now = get_epoch_ms();
    s_zone_cache[zone_index].last_update = (now > 0.0) ? now : prev_lu;
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

void firebase_sync_post_command_ack(int zone_index, const char *command_id,
                                    const char *status, const char *message)
{
    if (zone_index < 0 || zone_index >= NUM_ZONES) return;
    if (command_id == NULL || status == NULL) return;

    /* Prepare the slot contents off the spinlock, then publish atomically. */
    ack_slot_t slot = {0};
    slot.pending = true;
    strncpy(slot.command_id, command_id, sizeof(slot.command_id) - 1);
    strncpy(slot.status,     status,     sizeof(slot.status) - 1);
    if (message != NULL) {
        strncpy(slot.message, message, sizeof(slot.message) - 1);
    }
    double now = get_epoch_ms();
    slot.executed_at = (now > 0.0) ? now : 0.0;

    portENTER_CRITICAL(&s_ack_mux);
    s_ack[zone_index] = slot;
    portEXIT_CRITICAL(&s_ack_mux);

    ESP_LOGI(TAG, "Ack queued for %s: id=%s status=%s",
             ZONE_IDS[zone_index], command_id, status);

    /* Get it to the cloud promptly. */
    firebase_sync_trigger_immediate_upload();
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

static double get_epoch_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    double ms = (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
    
    /* 1700000000 sec is ~Nov 2023. In ms: 1700000000000 */
    if (ms > 1700000000000.0) {
        return ms;
    }
    return -1.0;
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

    /* Only publish a real epoch timestamp once SNTP has synced.
     * "lastSeen" and "ts" mirror each other to match the db.json schema
     * (lastSeen = gateway liveness, ts = last update; both epoch ms). */
    double epoch = get_epoch_ms();
    if (epoch > 0.0) {
        cJSON_AddNumberToObject(root, "lastSeen", epoch);
        cJSON_AddNumberToObject(root, "ts",       epoch);
    }

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

    const firebase_zone_data_t *z = &s_zone_cache[zone_index];

    /* If we have no recent data at all, do not overwrite Firebase with zeros */
    if (!(have_engine_state && eng_state.data_valid) && !z->valid) {
        return NULL;
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
        cJSON_AddNumberToObject(root, "sm", z->soil_moisture);
        cJSON_AddNumberToObject(root, "t",  z->temperature);
        cJSON_AddNumberToObject(root, "f",  z->flow_rate);
        cJSON_AddBoolToObject  (root, "v",  z->valve_open);

        int th  = z->threshold > 0 ? z->threshold :
                  (zone_index == 0 ? DEFAULT_Z1_THRESHOLD : DEFAULT_Z2_THRESHOLD);
        int dur = z->duration  > 0 ? z->duration  :
                  (zone_index == 0 ? DEFAULT_Z1_DURATION  : DEFAULT_Z2_DURATION);
        cJSON_AddNumberToObject(root, "th",  th);
        cJSON_AddNumberToObject(root, "dur", dur);
    }

    /* Only publish a real "last updated" epoch once it is valid. */
    if (s_zone_cache[zone_index].last_update > 0.0) {
        cJSON_AddNumberToObject(root, "lu", s_zone_cache[zone_index].last_update);
    }

    char *str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return str;
}

/**
 * @brief Build a history record JSON payload for a zone.
 * @return Heap-allocated string (caller must free), or NULL on error.
 */
static char *build_history_json(int zone_index, bool valve_open,
                                double water_used_liters, double epoch_ms)
{
    const firebase_zone_data_t *z = &s_zone_cache[zone_index];

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return NULL;

    cJSON_AddNumberToObject(root, "temperature",     z->temperature);
    cJSON_AddNumberToObject(root, "moisture",        z->soil_moisture);
    cJSON_AddNumberToObject(root, "flowRate",        z->flow_rate);
    cJSON_AddNumberToObject(root, "waterUsedLiters", water_used_liters);
    cJSON_AddBoolToObject  (root, "valveOpen",       valve_open);
    cJSON_AddNumberToObject(root, "timestamp",       epoch_ms);
    cJSON_AddStringToObject(root, "source",          "gateway");

    char *str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return str;
}

/**
 * @brief Resolve a zone's authoritative valve state.
 */
static bool zone_valve_is_open(int zone_index)
{
    uint8_t zone_id = (uint8_t)(zone_index + 1);
    irrigation_zone_t eng_state;
    if (irrigation_engine_get_zone_state(zone_id, &eng_state) == ESP_OK) {
        return eng_state.valve_open;
    }
    return s_zone_cache[zone_index].valve_open;
}

/**
 * @brief Write any pending command acknowledgements to /commandAcks/g1/<zone>.
 *
 * Runs in the upload task context (HTTP allowed). Snapshots and clears each
 * pending slot under the spinlock, then performs the network write.
 */
static void upload_pending_acks(void)
{
    for (int i = 0; i < NUM_ZONES; i++) {
        ack_slot_t slot;

        portENTER_CRITICAL(&s_ack_mux);
        slot = s_ack[i];
        s_ack[i].pending = false;
        portEXIT_CRITICAL(&s_ack_mux);

        if (!slot.pending) continue;

        cJSON *root = cJSON_CreateObject();
        if (root == NULL) continue;
        cJSON_AddStringToObject(root, "commandId",  slot.command_id);
        cJSON_AddStringToObject(root, "status",     slot.status);
        cJSON_AddNumberToObject(root, "executedAt", slot.executed_at);
        cJSON_AddStringToObject(root, "message",    slot.message);
        char *json = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (json == NULL) continue;

        char path[64];
        snprintf(path, sizeof(path), "commandAcks/%s/%s", GATEWAY_ID, ZONE_IDS[i]);

        esp_err_t err = firebase_db_put(path, json);
        if (err == ESP_ERR_INVALID_STATE) {
            if (try_reauth() == ESP_OK) {
                err = firebase_db_put(path, json);
            }
        }
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Ack written: %s id=%s status=%s",
                     path, slot.command_id, slot.status);
        } else {
            /* Re-queue so we retry on the next cycle (latest ack still wins). */
            portENTER_CRITICAL(&s_ack_mux);
            if (!s_ack[i].pending) {
                s_ack[i] = slot;
            }
            portEXIT_CRITICAL(&s_ack_mux);
            ESP_LOGE(TAG, "Ack write failed for %s — will retry", ZONE_IDS[i]);
        }
        free(json);
    }
}

/**
 * @brief Append history records for zones that are due (periodic or on valve change).
 *
 * A record is written when either FIREBASE_HISTORY_INTERVAL_MS has elapsed since
 * the zone's last record, or the zone's valve state changed since then. Records
 * are only written once SNTP time is valid (timestamps are epoch milliseconds).
 */
static void record_history_if_due(void)
{
    double epoch = get_epoch_ms();
    if (epoch <= 0.0) {
        return; /* No valid time yet — cannot timestamp history. */
    }

    for (int i = 0; i < NUM_ZONES; i++) {
        const firebase_zone_data_t *z = &s_zone_cache[i];
        if (!z->valid) continue; /* Never overwrite/insert without real data. */

        bool valve_open = zone_valve_is_open(i);

        bool first_record  = (s_hist_last_time[i] == 0.0);
        bool interval_due  = (epoch - s_hist_last_time[i]) >= (double)FIREBASE_HISTORY_INTERVAL_MS;
        bool valve_changed = s_hist_valve_known[i] && (valve_open != s_hist_valve_last[i]);

        if (!first_record && !interval_due && !valve_changed) {
            continue;
        }

        /* waterUsedLiters = delta of cumulative volume since the last record,
         * clamped to >= 0 so a flow-meter counter reset never goes negative. */
        double used_liters = 0.0;
        if (s_hist_vol_known[i] && z->total_volume_ml >= s_hist_vol_last[i]) {
            used_liters = (double)(z->total_volume_ml - s_hist_vol_last[i]) / 1000.0;
        }

        char *json = build_history_json(i, valve_open, used_liters, epoch);
        if (json == NULL) continue;

        char path[64];
        snprintf(path, sizeof(path), "history/%s", ZONE_IDS[i]);

        esp_err_t err = firebase_db_post(path, json);
        if (err == ESP_ERR_INVALID_STATE) {
            if (try_reauth() == ESP_OK) {
                err = firebase_db_post(path, json);
            }
        }
        free(json);

        if (err == ESP_OK) {
            /* Advance bookkeeping only on a successful write so we don't lose
             * the volume baseline or skip the next record on transient failure. */
            s_hist_last_time[i]   = epoch;
            s_hist_valve_last[i]  = valve_open;
            s_hist_valve_known[i] = true;
            s_hist_vol_last[i]    = z->total_volume_ml;
            s_hist_vol_known[i]   = true;
            ESP_LOGI(TAG, "History %s recorded (used=%.3f L, valve=%d)",
                     ZONE_IDS[i], used_liters, valve_open);
        } else {
            ESP_LOGE(TAG, "History %s write failed — will retry next cycle", ZONE_IDS[i]);
        }
    }
}

static void print_firebase_diagnostics(const char *task_name)
{
    bool wifi_ok = wifi_manager_is_connected();
    
    char ip_str[32] = "N/A";
    esp_ip4_addr_t ip;
    if (wifi_manager_get_ip(&ip) == ESP_OK) {
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip));
    }
    
    bool auth_ok = firebase_auth_is_logged_in();
    const char *token = firebase_auth_get_token();
    size_t token_len = token ? strlen(token) : 0;
    const char *uid = firebase_auth_get_uid();
    
    int auth_status = firebase_auth_get_last_status();
    esp_err_t auth_esp_err = firebase_auth_get_last_esp_err();
    const char *auth_err_desc = firebase_auth_get_last_error_desc();
    
    int db_status = firebase_db_get_last_status();
    esp_err_t db_esp_err = firebase_db_get_last_esp_err();
    const char *db_err_desc = firebase_db_get_last_error_desc();
    const char *db_path = firebase_db_get_last_path();
    
    ESP_LOGI(TAG, "==== %s DIAGNOSTICS ====", task_name);
    ESP_LOGI(TAG, "  WiFi State    : %s", wifi_ok ? "CONNECTED" : "DISCONNECTED");
    ESP_LOGI(TAG, "  IP Address    : %s", ip_str);
    ESP_LOGI(TAG, "  Firebase Ready: %s", (wifi_ok && auth_ok) ? "YES" : "NO");
    ESP_LOGI(TAG, "  Token Status  : %s (len: %u)", auth_ok ? "VALID" : "INVALID/MISSING", (unsigned)token_len);
    ESP_LOGI(TAG, "  User UID      : %s", uid ? uid : "N/A");
    if (auth_status != 0 || auth_esp_err != ESP_OK || (auth_err_desc && auth_err_desc[0])) {
        ESP_LOGI(TAG, "  Last Auth Err : %s (HTTP: %d, ESP: %s)", 
                 (auth_err_desc && auth_err_desc[0]) ? auth_err_desc : "None", 
                 auth_status, esp_err_to_name(auth_esp_err));
    }
    ESP_LOGI(TAG, "  Last DB Path  : %s", (db_path && db_path[0]) ? db_path : "None");
    if (db_status != 0 || db_esp_err != ESP_OK || (db_err_desc && db_err_desc[0])) {
        ESP_LOGI(TAG, "  Last DB Error : %s (HTTP: %d, ESP: %s)", 
                 (db_err_desc && db_err_desc[0]) ? db_err_desc : "None", 
                 db_status, esp_err_to_name(db_esp_err));
    }
    ESP_LOGI(TAG, "=================================");
}

/* ========================================================================== */
/*                       UPLOAD TASK                                          */
/* ========================================================================== */

void firebase_upload_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Upload task started (interval: %d ms)", FIREBASE_UPLOAD_INTERVAL_MS);

    s_upload_task_handle = xTaskGetCurrentTaskHandle();
    uint32_t loop_count = 0;

    while (1) {
        /* Wait for periodic interval or immediate trigger */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(FIREBASE_UPLOAD_INTERVAL_MS));
        loop_count++;

        bool wifi_connected = wifi_manager_is_connected();
        bool logged_in = firebase_auth_is_logged_in();

        if (wifi_connected && !logged_in) {
            ESP_LOGI(TAG, "WiFi connected but not logged in. Attempting Firebase login...");
            if (firebase_auth_login() == ESP_OK) {
                logged_in = true;
                ESP_LOGI(TAG, "Firebase login succeeded in upload task!");
            } else {
                ESP_LOGE(TAG, "Firebase login failed in upload task.");
            }
        }

        if (!logged_in) {
            if (loop_count % 5 == 0) {
                ESP_LOGW(TAG, "Not logged in — skipping upload");
                print_firebase_diagnostics("UPLOAD_TASK");
            }
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
                print_firebase_diagnostics("UPLOAD_TASK");
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
                    print_firebase_diagnostics("UPLOAD_TASK");
                }
                free(z_json);
            }
        }

        /* ---- Write any pending command acknowledgements ---- */
        upload_pending_acks();

        /* ---- Append history records (periodic + on valve change) ---- */
        record_history_if_due();

        /* Periodically print success diagnostics (every 30 loops / 1 minute) */
        if (loop_count % 30 == 0) {
            print_firebase_diagnostics("UPLOAD_TASK");
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
        s_commands.timestamp = ts_j->valuedouble;
    }

    /* z */
    cJSON *z_obj = cJSON_GetObjectItem(root, "z");
    if (cJSON_IsObject(z_obj)) {
        const char *z_keys[NUM_ZONES] = { ZONE_ID_1, ZONE_ID_2 };
        for (int i = 0; i < NUM_ZONES; i++) {
            cJSON *zj = cJSON_GetObjectItem(z_obj, z_keys[i]);
            if (!cJSON_IsObject(zj)) continue;

            cJSON *v = cJSON_GetObjectItem(zj, "v");
            cJSON *d = cJSON_GetObjectItem(zj, "d");
            if (cJSON_IsBool(v))   s_commands.zone_valve[i]    = cJSON_IsTrue(v);
            if (cJSON_IsNumber(d)) s_commands.zone_duration[i] = (uint16_t)d->valuedouble;

            /* Optional per-zone command id for ack + de-duplication.
             * Absent id => legacy command (applied, but not acknowledged). */
            cJSON *cid = cJSON_GetObjectItem(zj, "commandId");
            if (cJSON_IsString(cid) && cid->valuestring != NULL) {
                strncpy(s_commands.zone_command_id[i], cid->valuestring,
                        FIREBASE_COMMAND_ID_MAX - 1);
                s_commands.zone_command_id[i][FIREBASE_COMMAND_ID_MAX - 1] = '\0';
            } else {
                s_commands.zone_command_id[i][0] = '\0';
            }
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
    uint32_t loop_count = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(FIREBASE_POLL_INTERVAL_MS));
        loop_count++;

        bool wifi_connected = wifi_manager_is_connected();
        bool logged_in = firebase_auth_is_logged_in();

        if (wifi_connected && !logged_in) {
            ESP_LOGI(TAG, "WiFi connected but not logged in. Attempting Firebase login...");
            if (firebase_auth_login() == ESP_OK) {
                logged_in = true;
                ESP_LOGI(TAG, "Firebase login succeeded in poll task!");
            } else {
                ESP_LOGE(TAG, "Firebase login failed in poll task.");
            }
        }

        if (!logged_in) {
            if (loop_count % 5 == 0) {
                ESP_LOGW(TAG, "Not logged in — skipping poll");
            }
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
                print_firebase_diagnostics("POLL_TASK");
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
                print_firebase_diagnostics("POLL_TASK");
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
                print_firebase_diagnostics("POLL_TASK");
            }
        }
    }
}
