/**
 * @file firebase_sync.h
 * @brief Firebase Sync — background tasks for uploading data and polling commands.
 *
 * Provides FreeRTOS task functions that periodically:
 *  - Upload gateway state, zone sensor data, and weather to Firebase RTDB
 *  - Poll the commands endpoint, gateway mode, and zone configs
 *
 * The module exposes a shared data cache that main.c / irrigation_engine
 * populate from ESP-NOW callbacks.  The sync tasks read this cache to build
 * JSON payloads and feed configuration back to the irrigation engine.
 */

#ifndef FIREBASE_SYNC_H
#define FIREBASE_SYNC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum length of a command id string (Firebase push key / UUID). */
#define FIREBASE_COMMAND_ID_MAX     40

/* ========================================================================== */
/*                          ZONE DATA CACHE                                   */
/* ========================================================================== */

/**
 * @brief Cached sensor data for one zone (populated from ESP-NOW).
 */
typedef struct {
    uint8_t  soil_moisture;       /**< 0-100 % */
    float    temperature;         /**< °C (NAN if invalid) */
    float    flow_rate;           /**< L/min */
    uint32_t total_volume_ml;     /**< Cumulative volume reported by the flow meter (mL) */
    bool     valve_open;          /**< Current valve state */
    double   last_update;         /**< Epoch milliseconds (set by uploader if 0) */
    uint8_t  threshold;           /**< Moisture threshold (from config) */
    uint16_t duration;            /**< Default irrigation duration in seconds */
    bool     valid;               /**< true once at least one reading received */
} firebase_zone_data_t;

/**
 * @brief Cloud command parsed from /commands/g1.
 *
 * The legacy "v"/"d" fields are still honoured. An optional per-zone
 * "commandId" enables de-duplication and acknowledgement: when present and
 * different from the last applied id, the gateway (re)issues the valve command
 * and writes /commandAcks/g1/<zone>. An empty id means "no id supplied" and
 * the command is applied with the legacy behaviour (no ack).
 */
typedef struct {
    bool     pump;                /**< Desired pump state */
    bool     zone_valve[2];       /**< Desired valve state for z1, z2 */
    uint16_t zone_duration[2];    /**< Command duration for z1, z2 */
    char     zone_command_id[2][FIREBASE_COMMAND_ID_MAX]; /**< Per-zone command id ("" if none) */
    double   timestamp;           /**< Command timestamp */
    bool     valid;               /**< true if last poll succeeded */
} firebase_commands_t;

/* ========================================================================== */
/*                     UPDATE FUNCTIONS (called from main.c)                  */
/* ========================================================================== */

/**
 * @brief Update the cached zone data for a given zone index.
 *
 * @param zone_index 0-based index (0 = z1, 1 = z2)
 * @param data       Pointer to the new data
 */
void firebase_sync_update_zone(int zone_index, const firebase_zone_data_t *data);

/**
 * @brief Update the cached gateway / pump state.
 *
 * @param pump_on      true if pump is running
 * @param water_level  Water level percentage (0-100)
 */
void firebase_sync_update_gateway(bool pump_on, int water_level);

/**
 * @brief Get the latest parsed commands from the cloud.
 *
 * @param[out] out  Destination to copy the commands into
 */
void firebase_sync_get_commands(firebase_commands_t *out);

/**
 * @brief Get the current gateway mode from Firebase.
 *
 * @param[out] mode_out  Buffer to write mode string ("auto" or "manual").
 * @param      len       Size of mode_out buffer.
 */
void firebase_sync_get_gateway_mode(char *mode_out, size_t len);

/**
 * @brief Get zone configuration (threshold, duration) from Firebase.
 *
 * @param zone_index   0-based index (0 = z1, 1 = z2).
 * @param[out] threshold  Pointer to receive threshold value.
 * @param[out] duration   Pointer to receive duration value.
 */
void firebase_sync_get_zone_config(int zone_index, uint8_t *threshold,
                                    uint16_t *duration);

/**
 * @brief Trigger an immediate upload of states (e.g. on critical changes).
 */
void firebase_sync_trigger_immediate_upload(void);

/**
 * @brief Queue a command acknowledgement to be written to /commandAcks/g1/<zone>.
 *
 * Called by the irrigation engine after it acts on a new commandId. The actual
 * HTTP write is performed by the upload task, so this function is safe to call
 * from the engine context (it never blocks on the network). The most recent
 * pending ack per zone wins if several are queued before the next upload.
 *
 * @param zone_index 0-based index (0 = z1, 1 = z2).
 * @param command_id The command id being acknowledged.
 * @param status     "executed" or "failed".
 * @param message    Human-readable detail (may be NULL/empty).
 */
void firebase_sync_post_command_ack(int zone_index, const char *command_id,
                                    const char *status, const char *message);

/* ========================================================================== */
/*                        FREERTOS TASK FUNCTIONS                             */
/* ========================================================================== */

/**
 * @brief FreeRTOS task: periodically uploads gateway + zone data to Firebase.
 *
 * Create with xTaskCreate(). Task parameter is unused (pass NULL).
 * Stack size recommendation: 12288 bytes.
 */
void firebase_upload_task(void *arg);

/**
 * @brief FreeRTOS task: periodically polls commands, mode, and zone configs.
 *
 * Create with xTaskCreate(). Task parameter is unused (pass NULL).
 * Stack size recommendation: 12288 bytes.
 */
void firebase_poll_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* FIREBASE_SYNC_H */
