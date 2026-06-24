/**
 * @file irrigation_engine.c
 * @brief Irrigation Decision Engine implementation.
 *
 * Implements auto/manual irrigation modes with full safety constraints,
 * parallel zone irrigation (hardware has one pump → shared tube → per-zone
 * valves), non-blocking timer-based duration control, and pump sequencing.
 * All shared state is protected by a FreeRTOS mutex.
 */

#include "irrigation_engine.h"
#include "weather_service.h"
#include "firebase_sync.h"
#include "firebase_config.h"
#include "app_config.h"
#include "pump.h"
#include "esp_now_driver.h"

#include <string.h>
#include <stdio.h>
#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "IRR_ENGINE";

/* ========================================================================== */
/*                          EXTERNAL REFERENCES                               */
/* ========================================================================== */

/* Pump handle and zone radio MACs are owned by main.c */
extern pump_t   s_pump;
extern uint8_t  s_zone_radio_mac[][6];
extern bool     s_zone_radio_mac_valid[];

/* Number of reliable-send attempts for valve commands (ACK + retransmit). */
#define VALVE_CMD_ATTEMPTS  4

/* ========================================================================== */
/*                          INTERNAL STATE                                    */
/* ========================================================================== */

static irrigation_zone_t  s_zones[NUM_ZONES];
static irrigation_mode_t  s_mode = IRRIGATION_MODE_AUTO;
static bool               s_pump_on = false;
static SemaphoreHandle_t  s_mutex = NULL;

/* Reservoir water level (fed by the water-level task in main.c). */
static uint8_t            s_water_level_pct  = 0;
static bool               s_water_level_valid = false;
static bool               s_tank_dry          = false;

/** Per-zone irrigation tracking (parallel — multiple zones can irrigate) */
static bool      s_zone_irrigating[NUM_ZONES] = {false};
static TickType_t s_zone_irr_start[NUM_ZONES]  = {0};
static TickType_t s_zone_irr_duration[NUM_ZONES] = {0};

/** Last manual command id applied per zone (for de-duplication + acks). */
static char      s_zone_last_cmd_id[NUM_ZONES][FIREBASE_COMMAND_ID_MAX] = {{0}};

/* ========================================================================== */
/*                     MUTEX HELPERS                                          */
/* ========================================================================== */

#define ENGINE_LOCK()    xSemaphoreTake(s_mutex, portMAX_DELAY)
#define ENGINE_UNLOCK()  xSemaphoreGive(s_mutex)

/* ========================================================================== */
/*                     PUMP CONTROL (INTERNAL)                                */
/* ========================================================================== */

/**
 * @brief Check if any zone is currently irrigating.
 */
static bool any_zone_irrigating(void)
{
    for (int i = 0; i < NUM_ZONES; i++) {
        if (s_zone_irrigating[i]) return true;
    }
    return false;
}

/**
 * @brief Whether there is enough water in the reservoir to run the pump.
 *
 * Returns false only when we have a VALID level reading below the dry
 * threshold. If no valid reading exists yet (sensor absent/failed), we allow
 * operation but the caller logs a warning — this keeps installations without a
 * level sensor functional while still protecting tanks that do have one.
 */
static bool water_available(void)
{
    if (s_water_level_valid && s_water_level_pct < WATER_LEVEL_DRY_THRESHOLD) {
        return false;
    }
    return true;
}

/**
 * @brief Turn the pump ON if not already running and water is available.
 */
static void engine_pump_on(void)
{
    if (!s_pump_on) {
        ESP_LOGI(TAG, ">>> PUMP ON");
        pump_on(&s_pump);
        s_pump_on = true;
        firebase_sync_trigger_immediate_upload();
        /* Small delay to let pressure build */
        vTaskDelay(pdMS_TO_TICKS(PUMP_SETTLE_DELAY_MS));
    }
}

/**
 * @brief Turn the pump OFF if no zones are actively irrigating.
 */
static void pump_off_if_idle(void)
{
    if (s_pump_on && !any_zone_irrigating()) {
        ESP_LOGI(TAG, ">>> PUMP OFF (no active zones)");
        pump_off(&s_pump);
        s_pump_on = false;
        firebase_sync_trigger_immediate_upload();
    }
}

/* ========================================================================== */
/*                   VALVE COMMAND (INTERNAL)                                 */
/* ========================================================================== */

/**
 * @brief Send a valve OPEN/CLOSE command to a zone via ESP-NOW.
 *
 * @param zone_id     1-based zone ID.
 * @param open_valve  true to open, false to close.
 * @return ESP_OK if the zone acknowledged the reliable send, otherwise the
 *         underlying error (used to populate command acknowledgements).
 */
static esp_err_t send_valve_command(uint8_t zone_id, bool open_valve)
{
    if (zone_id < 1 || zone_id > MAX_ZONES) return ESP_ERR_INVALID_ARG;
    if (!s_zone_radio_mac_valid[zone_id]) {
        ESP_LOGW(TAG, "Zone %u radio MAC unknown — cannot send valve cmd", zone_id);
        return ESP_ERR_INVALID_STATE;
    }

    EspNow_AddPeer(s_zone_radio_mac[zone_id]);

    EspNow_CommandPayload_t cmd = {0};
    cmd.header.version   = ESP_NOW_PROTOCOL_VERSION;
    cmd.header.msg_type  = ESP_NOW_MSG_TYPE_COMMAND;
    cmd.header.sender_id = 0; /* Control */
    cmd.target_zone_id   = zone_id;
    cmd.open_valve       = open_valve ? 1 : 0;

    /* Valve commands are safety-critical — send reliably with ACK + retransmit
     * rather than fire-and-forget, so a dropped frame doesn't leave a valve in
     * the wrong state until the zone's 10-minute safety timeout. */
    esp_err_t err = EspNow_SendReliable(s_zone_radio_mac[zone_id],
                                        (const uint8_t *)&cmd, sizeof(cmd),
                                        VALVE_CMD_ATTEMPTS);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Valve CMD -> Zone %u: %s (acked)", zone_id,
                 open_valve ? "OPEN" : "CLOSE");
    } else {
        ESP_LOGE(TAG, "Valve CMD NOT acked for Zone %u: %s (will be retried next cycle)",
                 zone_id, esp_err_to_name(err));
    }
    return err;
}

/* ========================================================================== */
/*                   IRRIGATION START / STOP                                  */
/* ========================================================================== */

/**
 * @brief Start irrigating a specific zone for a given duration.
 *
 * Multiple zones can irrigate simultaneously (shared pump, per-zone valves).
 * Safety: pump must be ON before valve opens.
 *
 * @param zone_id   1-based zone ID.
 * @param duration  Duration in seconds.
 * @return ESP_OK if the valve command was acknowledged (or the zone was already
 *         irrigating), ESP_ERR_INVALID_STATE if safety blocked the start, or the
 *         ESP-NOW error if the reliable send was not acknowledged.
 */
static esp_err_t start_irrigation(uint8_t zone_id, uint16_t duration)
{
    if (zone_id < 1 || zone_id > NUM_ZONES) return ESP_ERR_INVALID_ARG;
    int idx = zone_id - 1;

    /* Already irrigating? Skip. */
    if (s_zone_irrigating[idx]) return ESP_OK;

    /* Safety: never irrigate if moisture is above threshold */
    if (s_zones[idx].sm >= s_zones[idx].threshold) {
        ESP_LOGW(TAG, "Zone %u sm=%u >= th=%u — safety limit, skipping",
                 zone_id, s_zones[idx].sm, s_zones[idx].threshold);
        return ESP_ERR_INVALID_STATE;
    }

    /* Safety: never run the pump dry. */
    if (!water_available()) {
        ESP_LOGW(TAG, "Zone %u: reservoir low (%u%% < %d%%) — irrigation inhibited",
                 zone_id, s_water_level_pct, WATER_LEVEL_DRY_THRESHOLD);
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_water_level_valid) {
        ESP_LOGW(TAG, "Zone %u: no valid water-level reading — proceeding without dry-run protection",
                 zone_id);
    }

    ESP_LOGI(TAG, "Starting irrigation: Zone %u for %u seconds", zone_id, duration);

    /* 1. Pump ON first */
    engine_pump_on();

    /* 2. Open valve */
    s_zone_irrigating[idx] = true;
    s_zones[idx].valve_open = true;
    s_zone_irr_start[idx] = xTaskGetTickCount();
    s_zone_irr_duration[idx] = pdMS_TO_TICKS((uint32_t)duration * 1000UL);

    esp_err_t send_res = send_valve_command(zone_id, true);

    /* Update Firebase with current state */
    firebase_sync_update_gateway(s_pump_on, 0);
    firebase_sync_trigger_immediate_upload();

    return send_res;
}

/**
 * @brief Stop irrigating a specific zone.
 *
 * @param zone_id  1-based zone ID.
 * @return ESP_OK if the close command was acknowledged (or the zone was already
 *         idle), otherwise the ESP-NOW error from the reliable send.
 */
static esp_err_t stop_zone_irrigation(uint8_t zone_id)
{
    if (zone_id < 1 || zone_id > NUM_ZONES) return ESP_ERR_INVALID_ARG;
    int idx = zone_id - 1;

    if (!s_zone_irrigating[idx]) return ESP_OK;

    ESP_LOGI(TAG, "Stopping irrigation: Zone %u", zone_id);

    /* 1. Close valve */
    s_zones[idx].valve_open = false;
    s_zone_irrigating[idx] = false;
    s_zone_irr_start[idx] = 0;
    s_zone_irr_duration[idx] = 0;

    esp_err_t send_res = send_valve_command(zone_id, false);

    /* 2. Pump OFF if no other zone is active */
    pump_off_if_idle();

    /* Update Firebase */
    firebase_sync_update_gateway(s_pump_on, 0);
    firebase_sync_trigger_immediate_upload();

    return send_res;
}

/**
 * @brief Stop all active irrigations.
 */
static void stop_all_irrigation(void)
{
    for (int i = 0; i < NUM_ZONES; i++) {
        if (s_zone_irrigating[i]) {
            stop_zone_irrigation((uint8_t)(i + 1));
        }
    }
}

/* ========================================================================== */
/*                   ZONE VALIDATION                                         */
/* ========================================================================== */

/**
 * @brief Check if zone sensor data is valid and fresh.
 *
 * @param idx  0-based zone index.
 * @return true if the zone can be considered for irrigation.
 */
static bool is_zone_eligible(int idx)
{
    const irrigation_zone_t *z = &s_zones[idx];

    if (!z->data_valid) {
        ESP_LOGD(TAG, "Zone %d: no data yet — skipping", idx + 1);
        return false;
    }

    if (!z->zone_active) {
        ESP_LOGW(TAG, "Zone %d: not responding — skipping", idx + 1);
        return false;
    }

    /* Check for stale data */
    TickType_t now = xTaskGetTickCount();
    TickType_t elapsed = now - z->last_update;
    if (elapsed >= pdMS_TO_TICKS(ZONE_STALE_DATA_MS)) {
        ESP_LOGW(TAG, "Zone %d: data stale (%lu ms) — skipping",
                 idx + 1, (unsigned long)(elapsed * portTICK_PERIOD_MS));
        return false;
    }

    /* Validate sensor range */
    if (z->sm > SENSOR_SM_MAX) {
        ESP_LOGW(TAG, "Zone %d: moisture %u out of range — skipping", idx + 1, z->sm);
        return false;
    }

    return true;
}

/* ========================================================================== */
/*                   DURATION CHECK (non-blocking)                            */
/* ========================================================================== */

/**
 * @brief Check all zones for irrigation duration expiry.
 */
static void check_irrigation_durations(void)
{
    for (int i = 0; i < NUM_ZONES; i++) {
        if (!s_zone_irrigating[i]) continue;
        if (s_zone_irr_duration[i] == 0) continue; /* 0 means infinite duration (sensor-driven) */

        TickType_t elapsed = xTaskGetTickCount() - s_zone_irr_start[i];
        if (elapsed >= s_zone_irr_duration[i]) {
            ESP_LOGI(TAG, "Zone %d: duration elapsed — closing valve", i + 1);
            stop_zone_irrigation((uint8_t)(i + 1));
        }
    }
}

/* ========================================================================== */
/*                   AUTO MODE LOGIC                                         */
/* ========================================================================== */

static void run_auto_mode(void)
{
    weather_data_t weather;
    weather_service_get_cached(&weather);

    /* If it's raining, stop all active irrigations */
    if (weather.rainNow) {
        ESP_LOGI(TAG, "AUTO: Rain detected — stopping all irrigation");
        stop_all_irrigation();
        return;
    }

    /* Evaluate each zone independently — parallel irrigation supported */
    for (int i = 0; i < NUM_ZONES; i++) {
        uint8_t zone_id = (uint8_t)(i + 1);

        if (!is_zone_eligible(i)) {
            /* Safety: stop if data becomes ineligible during irrigation */
            if (s_zone_irrigating[i]) {
                ESP_LOGW(TAG, "AUTO: Zone %u ineligible while irrigating -> STOP", zone_id);
                stop_zone_irrigation(zone_id);
            }
            continue;
        }

        const irrigation_zone_t *z = &s_zones[i];

        if (s_zone_irrigating[i]) {
            /* Sensor-driven: stop when threshold is reached */
            if (z->sm >= z->threshold) {
                ESP_LOGI(TAG, "AUTO: Zone %u reached threshold (sm=%u >= th=%u -> STOP)", zone_id, z->sm, z->threshold);
                stop_zone_irrigation(zone_id);
            }
            continue;
        }

        if (z->sm < z->threshold) {
            ESP_LOGI(TAG, "AUTO: Zone %u needs water (sm=%u < th=%u, rainNow=%d -> START)",
                     zone_id, z->sm, z->threshold, weather.rainNow);
            /* 0 = infinite duration; stopped by threshold */
            start_irrigation(zone_id, 0);
        } else {
            ESP_LOGD(TAG, "AUTO: Zone %u sufficient (sm=%u >= th=%u -> SKIP)",
                     zone_id, z->sm, z->threshold);
        }
    }
}

/* ========================================================================== */
/*                   MANUAL MODE LOGIC                                       */
/* ========================================================================== */

static void run_manual_mode(void)
{
    firebase_commands_t cmd;
    firebase_sync_get_commands(&cmd);

    if (!cmd.valid) {
        ESP_LOGD(TAG, "MANUAL: No valid commands");
        return;
    }

    /* Master pump kill from the cloud: pump=false forces every zone closed.
     * Handled inside the per-zone loop so a per-zone commandId still gets
     * acknowledged in that case. */
    bool master_off = !cmd.pump;

    /* Check each zone's command independently — parallel supported. */
    for (int i = 0; i < NUM_ZONES; i++) {
        uint8_t zone_id = (uint8_t)(i + 1);

        const char *cid = cmd.zone_command_id[i];
        bool has_id = (cid[0] != '\0');
        bool new_id = has_id && (strcmp(cid, s_zone_last_cmd_id[i]) != 0);

        bool want_open = master_off ? false : cmd.zone_valve[i];

        esp_err_t send_res    = ESP_OK;   /* result of a start/stop we issued */
        bool       acted      = false;    /* true if we attempted a valve send */
        bool       blocked    = false;    /* true if safety refused an open    */
        const char *block_why = "";

        if (want_open) {
            /* Command ON ------------------------------------------------ */
            if (!s_zone_irrigating[i]) {
                /* Safety: data must be fresh/valid even in manual mode. */
                if (!is_zone_eligible(i)) {
                    blocked = true;
                    block_why = "zone data stale or invalid";
                } else if (s_zones[i].sm >= s_zones[i].threshold) {
                    blocked = true;
                    block_why = "moisture above threshold";
                    ESP_LOGW(TAG, "MANUAL: Zone %u sm=%u >= th=%u — safety limit, ignoring cmd",
                             zone_id, s_zones[i].sm, s_zones[i].threshold);
                } else {
                    ESP_LOGI(TAG, "MANUAL: Zone %u commanded ON for %u s",
                             zone_id, cmd.zone_duration[i]);
                    /* duration 0 => run until cloud sets v=false (threshold still applies). */
                    send_res = start_irrigation(zone_id, cmd.zone_duration[i]);
                    acted = true;
                }
            }
            /* else already irrigating — converged, no action needed. */
        } else {
            /* Command OFF: proactively close a running zone. */
            if (s_zone_irrigating[i]) {
                ESP_LOGI(TAG, "MANUAL: Zone %u commanded OFF — closing valve", zone_id);
                send_res = stop_zone_irrigation(zone_id);
                acted = true;
            }
            /* else already closed — converged. */
        }

        /* Acknowledge only genuinely new command ids (de-duplication). */
        if (new_id) {
            const char *status;
            char message[64];

            if (blocked) {
                status = "failed";
                snprintf(message, sizeof(message), "rejected: %s", block_why);
            } else if (acted) {
                if (send_res == ESP_OK) {
                    status = "executed";
                    snprintf(message, sizeof(message), "valve %s",
                             want_open ? "opened" : "closed");
                } else {
                    status = "failed";
                    snprintf(message, sizeof(message), "ESP-NOW not acked: %s",
                             esp_err_to_name(send_res));
                }
            } else {
                status = "executed";
                snprintf(message, sizeof(message), "already %s",
                         want_open ? "open" : "closed");
            }

            firebase_sync_post_command_ack(i, cid, status, message);

            strncpy(s_zone_last_cmd_id[i], cid, FIREBASE_COMMAND_ID_MAX - 1);
            s_zone_last_cmd_id[i][FIREBASE_COMMAND_ID_MAX - 1] = '\0';
        }
    }
}

/* ========================================================================== */
/*                     HEARTBEAT CHECK                                       */
/* ========================================================================== */

/**
 * @brief Mark zones as inactive if heartbeat timed out.
 */
static void check_zone_heartbeats(void)
{
    TickType_t now = xTaskGetTickCount();

    for (int i = 0; i < NUM_ZONES; i++) {
        if (!s_zones[i].data_valid) continue;
        if (s_zones[i].last_update == 0) continue;

        TickType_t elapsed = now - s_zones[i].last_update;
        if (elapsed >= pdMS_TO_TICKS(ZONE_HEARTBEAT_TIMEOUT_MS)) {
            if (s_zones[i].zone_active) {
                ESP_LOGW(TAG, "Zone %d: heartbeat timeout — marking inactive", i + 1);
                s_zones[i].zone_active = false;

                /* If this zone was irrigating, stop it */
                if (s_zone_irrigating[i]) {
                    stop_zone_irrigation((uint8_t)(i + 1));
                }
            }
        }
    }
}

/* ========================================================================== */
/*                          PUBLIC API                                        */
/* ========================================================================== */

void irrigation_engine_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    configASSERT(s_mutex != NULL);

    memset(s_zones, 0, sizeof(s_zones));
    memset(s_zone_irrigating, 0, sizeof(s_zone_irrigating));
    memset(s_zone_irr_start, 0, sizeof(s_zone_irr_start));
    memset(s_zone_irr_duration, 0, sizeof(s_zone_irr_duration));
    memset(s_zone_last_cmd_id, 0, sizeof(s_zone_last_cmd_id));

    /* Set default thresholds and durations */
    s_zones[0].threshold = DEFAULT_Z1_THRESHOLD;
    s_zones[0].duration  = DEFAULT_Z1_DURATION;
    if (NUM_ZONES > 1) {
        s_zones[1].threshold = DEFAULT_Z2_THRESHOLD;
        s_zones[1].duration  = DEFAULT_Z2_DURATION;
    }

    s_pump_on = false;
    s_mode = IRRIGATION_MODE_AUTO;

    ESP_LOGI(TAG, "Irrigation engine initialised (mode=AUTO, zones=%d, parallel=YES)",
             NUM_ZONES);
}

void irrigation_engine_run_cycle(void)
{
    ENGINE_LOCK();

    /* 1. Check for heartbeat timeouts */
    check_zone_heartbeats();

    /* 2. Check if any zone's irrigation duration has elapsed */
    check_irrigation_durations();

    /* 2b. Dry-run protection: if the reservoir is confirmed low, stop
     *     everything and skip irrigation this cycle regardless of mode. */
    bool dry = !water_available();
    s_tank_dry = dry;
    if (dry) {
        if (any_zone_irrigating() || s_pump_on) {
            ESP_LOGW(TAG, "SAFETY: reservoir low (%u%%) — stopping all irrigation",
                     s_water_level_pct);
            stop_all_irrigation();
        }
        pump_off_if_idle();
        ENGINE_UNLOCK();
        return;
    }

    /* 3. Run mode-specific logic */
    switch (s_mode) {
    case IRRIGATION_MODE_AUTO:
        run_auto_mode();
        break;
    case IRRIGATION_MODE_MANUAL:
        run_manual_mode();
        break;
    default:
        ESP_LOGE(TAG, "Unknown mode %d — defaulting to AUTO", s_mode);
        s_mode = IRRIGATION_MODE_AUTO;
        run_auto_mode();
        break;
    }

    /* 4. Safety: ensure pump is OFF if no valves are open */
    pump_off_if_idle();

    ENGINE_UNLOCK();
}

esp_err_t irrigation_engine_update_zone(uint8_t zone_id, uint8_t moisture,
                                         float temperature, float flow)
{
    if (zone_id < 1 || zone_id > NUM_ZONES) {
        ESP_LOGW(TAG, "update_zone: invalid zone_id %u", zone_id);
        return ESP_ERR_INVALID_ARG;
    }

    /* Validate sensor values */
    if (moisture > SENSOR_SM_MAX) {
        ESP_LOGW(TAG, "Zone %u: moisture %u out of range [%d,%d] — rejected",
                 zone_id, moisture, SENSOR_SM_MIN, SENSOR_SM_MAX);
        return ESP_ERR_INVALID_ARG;
    }

    if (!isnan(temperature) &&
        (temperature < SENSOR_TEMP_MIN || temperature > SENSOR_TEMP_MAX)) {
        ESP_LOGW(TAG, "Zone %u: temperature %.1f out of range — using value anyway",
                 zone_id, temperature);
        /* Don't reject — just warn. Temperature doesn't affect irrigation decision. */
    }

    ENGINE_LOCK();

    int idx = zone_id - 1;
    s_zones[idx].sm          = moisture;
    s_zones[idx].temp        = isnan(temperature) ? 0.0f : temperature;
    s_zones[idx].flow        = flow;
    s_zones[idx].last_update = xTaskGetTickCount();
    s_zones[idx].data_valid  = true;
    s_zones[idx].zone_active = true;

    ENGINE_UNLOCK();

    ESP_LOGD(TAG, "Zone %u updated: sm=%u t=%.1f f=%.1f",
             zone_id, moisture, temperature, flow);

    return ESP_OK;
}

void irrigation_engine_set_mode(irrigation_mode_t mode)
{
    ENGINE_LOCK();

    if (s_mode != mode) {
        ESP_LOGI(TAG, "Mode changed: %s -> %s",
                 s_mode == IRRIGATION_MODE_AUTO ? "AUTO" : "MANUAL",
                 mode == IRRIGATION_MODE_AUTO ? "AUTO" : "MANUAL");

        /* If switching modes, stop all active irrigation for safety */
        stop_all_irrigation();
        s_mode = mode;
        firebase_sync_trigger_immediate_upload();
    }

    ENGINE_UNLOCK();
}

irrigation_mode_t irrigation_engine_get_mode(void)
{
    return s_mode;
}

void irrigation_engine_set_zone_config(uint8_t zone_id, uint8_t threshold,
                                        uint16_t duration)
{
    if (zone_id < 1 || zone_id > NUM_ZONES) return;

    ENGINE_LOCK();

    int idx = zone_id - 1;
    if (threshold > 0) s_zones[idx].threshold = threshold;
    if (duration > 0)  s_zones[idx].duration  = duration;

    ESP_LOGI(TAG, "Zone %u config updated: th=%u dur=%u",
             zone_id, s_zones[idx].threshold, s_zones[idx].duration);

    ENGINE_UNLOCK();
}

bool irrigation_engine_is_zone_irrigating(uint8_t zone_id)
{
    if (zone_id < 1 || zone_id > NUM_ZONES) return false;
    return s_zone_irrigating[zone_id - 1];
}

bool irrigation_engine_is_pump_on(void)
{
    return s_pump_on;
}

void irrigation_engine_set_water_level(uint8_t percent, bool valid)
{
    ENGINE_LOCK();
    if (percent > 100) percent = 100;
    s_water_level_pct   = percent;
    s_water_level_valid = valid;
    ENGINE_UNLOCK();
}

uint8_t irrigation_engine_get_water_level(void)
{
    return s_water_level_pct;
}

bool irrigation_engine_is_tank_dry(void)
{
    return s_tank_dry;
}

esp_err_t irrigation_engine_get_zone_state(uint8_t zone_id,
                                            irrigation_zone_t *out)
{
    if (zone_id < 1 || zone_id > NUM_ZONES || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ENGINE_LOCK();
    *out = s_zones[zone_id - 1];
    ENGINE_UNLOCK();

    return ESP_OK;
}
