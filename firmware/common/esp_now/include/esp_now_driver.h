#ifndef ESP_NOW_DRIVER_H
#define ESP_NOW_DRIVER_H

/**
 * @file esp_now_driver.h
 * @brief Simplified ESP-NOW Driver and Protocol definitions
 *
 * This module provides a simple interface to initialize and use ESP-NOW
 * for communicating between nodes (e.g. Zone and Control).
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_now.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                           PROTOCOL DEFINITIONS                             */
/* ========================================================================== */

/**
 * @brief ESP-NOW Protocol Version
 * Increment this version if breaking changes are made to the structures below.
 *
 * v4: ZONE_DATA gained `flow_rate_x100`; `moisture_percentage` now uses
 *     ESP_NOW_MOISTURE_INVALID (0xFF) to signal an unusable/uncalibrated read.
 */
#define ESP_NOW_PROTOCOL_VERSION 4

/**
 * @brief Sentinel moisture value meaning "no valid reading".
 *
 * Sent by a zone when its soil sensor is uncalibrated or the read failed.
 * The control board treats this as out-of-range and refuses to irrigate on it.
 */
#define ESP_NOW_MOISTURE_INVALID  0xFFU

/** @brief Sentinel temperature_c_x10 value meaning "no valid reading". */
#define ESP_NOW_TEMP_INVALID      ((int16_t)-999)

/**
 * @brief WiFi channel for ESP-NOW communication.
 *
 * Set to 0 to use the current channel (determined by WiFi STA connection).
 * Set to 1-13 to force a specific channel.
 *
 * On the **control board** (which also connects to a WiFi AP), this should
 * be 0 so the channel is automatically set by the AP.
 *
 * On **zone boards** (no WiFi AP), this should match the AP channel that
 * the control board connects to.
 *
 * Override in your project's app_config.h by defining ESP_NOW_CHANNEL
 * before including this header.
 */
#ifndef ESP_NOW_CHANNEL
#define ESP_NOW_CHANNEL  0
#endif

/**
 * @brief XOR key used to obfuscate MAC addresses in-transit.
 * Both control and zone must use the same key.
 * (Not cryptographic – a lightweight deterrent against casual sniffing.)
 */
#define ESP_NOW_MAC_OBFUSCATION_KEY 0xA5U

/**
 * @brief Message Types
 * Extensible enum for supporting different payload types in the future.
 */
typedef enum {
    ESP_NOW_MSG_TYPE_ZONE_DATA       = 1,  /**< Sensor data from Zone to Control */
    ESP_NOW_MSG_TYPE_COMMAND         = 2,  /**< Command from Control to Zone */
    ESP_NOW_MSG_TYPE_DISCOVERY_REQ   = 3,  /**< Control broadcasts to discover zones */
    ESP_NOW_MSG_TYPE_DISCOVERY_RESP  = 4,  /**< Zone unicasts its obfuscated MAC to control */
    ESP_NOW_MSG_TYPE_ZONE_ASSIGN     = 5,  /**< Control unicasts the assigned zone ID back */
} EspNow_MsgType_t;

/**
 * @brief Common Header for all ESP-NOW packets
 */
typedef struct __attribute__((packed)) {
    uint8_t version;     /**< Protocol version, MUST be ESP_NOW_PROTOCOL_VERSION */
    uint8_t msg_type;    /**< Message type (EspNow_MsgType_t) */
    uint8_t sender_id;   /**< Identifier for the sender (Zone ID or 0 for Control) */
} EspNow_MessageHeader_t;

/**
 * @brief Zone Data Payload Structure
 *
 * Designed to be extensible in the future by adding fields at the end.
 */
typedef struct __attribute__((packed)) {
    EspNow_MessageHeader_t header;  /**< Must be first */

    /* Current Sensors */
    uint8_t moisture_percentage;    /**< Soil moisture 0-100%, or ESP_NOW_MOISTURE_INVALID */
    int16_t temperature_c_x10;      /**< Temperature in Celsius * 10, or ESP_NOW_TEMP_INVALID */

    /* Valve / Flow State */
    uint8_t needs_water;            /**< 1 if zone needs water, 0 otherwise */
    uint32_t total_volume_ml;       /**< Cumulative flow volume in mL */
    int16_t flow_rate_x100;         /**< Instantaneous flow rate (L/min) * 100 */

    /* Future Expansion: Add battery level, ambient light, etc. below */
} EspNow_ZoneDataPayload_t;

/**
 * @brief Discovery Response Payload (Zone → Control)
 *
 * Contains the zone's XOR-obfuscated MAC address so control can identify it.
 */
typedef struct __attribute__((packed)) {
    EspNow_MessageHeader_t header;          /**< Must be first; sender_id = 0 (unknown yet) */
    uint8_t obfuscated_mac[6];              /**< XOR-obfuscated MAC of this zone */
} EspNow_DiscoveryRespPayload_t;

/**
 * @brief Zone Assignment Payload (Control → Zone)
 *
 * Sent unicast to a zone after control assigns it a zone ID.
 */
typedef struct __attribute__((packed)) {
    EspNow_MessageHeader_t header;  /**< Must be first; sender_id = 0 (from control) */
    uint8_t assigned_zone_id;       /**< The Zone ID assigned by the control (1-based) */
} EspNow_ZoneAssignPayload_t;

/**
 * @brief Command Payload (Control → Zone)
 *
 * Used to instruct a zone to open or close its valve.
 */
typedef struct __attribute__((packed)) {
    EspNow_MessageHeader_t header;  /**< Must be first; sender_id = 0 (from control) */
    uint8_t target_zone_id;         /**< The zone ID this command is intended for */
    uint8_t open_valve;             /**< 1 to open valve, 0 to close valve */
} EspNow_CommandPayload_t;


/* ========================================================================== */
/*                          MAC OBFUSCATION HELPERS                           */
/* ========================================================================== */

/**
 * @brief XOR-obfuscate a 6-byte MAC address in place.
 *
 * Apply the same function twice to reverse it (XOR is its own inverse).
 * Both the sender and receiver must use the same @ref ESP_NOW_MAC_OBFUSCATION_KEY.
 *
 * @param[in,out] mac  6-byte MAC array to obfuscate (or de-obfuscate).
 */
static inline void EspNow_ObfuscateMAC(uint8_t mac[6])
{
    for (int i = 0; i < 6; i++) {
        mac[i] ^= ESP_NOW_MAC_OBFUSCATION_KEY;
    }
}


/* ========================================================================== */
/*                             DRIVER INTERFACE                               */
/* ========================================================================== */

/**
 * @brief ESP-NOW Broadcast MAC Address
 */
extern const uint8_t ESP_NOW_BROADCAST_MAC[6];

typedef void (*EspNow_RecvCb_t)(const uint8_t *mac_addr, const uint8_t *data, int data_len);
typedef void (*EspNow_SendCb_t)(const uint8_t *mac_addr, esp_now_send_status_t status);

/**
 * @brief Initialise Wi-Fi (STA mode) and ESP-NOW.
 *
 * Brings up NVS, netif, the default event loop, Wi-Fi in station mode, and the
 * ESP-NOW layer. Registers an internal receive adapter (IDF 5.x signature) and
 * an internal send callback used for delivery acknowledgement, then provisions
 * the broadcast peer.
 *
 * @param channel The Wi-Fi channel to use (1-13) or 0 to dynamically track the AP.
 * @return ESP_OK on success, or an error code.
 */
esp_err_t EspNow_Init(uint8_t channel);

/**
 * @brief Deinitializes ESP-NOW and Wi-Fi.
 */
esp_err_t EspNow_Deinit(void);

/**
 * @brief Adds a peer device to the ESP-NOW peer list.
 *
 * @param[in] mac_addr MAC address of the peer (6 bytes)
 * @return ESP_OK on success
 */
esp_err_t EspNow_AddPeer(const uint8_t *mac_addr);

/**
 * @brief Removes a peer device from the ESP-NOW peer list.
 *
 * @param[in] mac_addr MAC address of the peer (6 bytes)
 * @return ESP_OK on success
 */
esp_err_t EspNow_RemovePeer(const uint8_t *mac_addr);

/**
 * @brief Register a callback for receiving packets.
 *
 * @param[in] cb Callback function to execute on receive.
 * @return ESP_OK on success
 */
esp_err_t EspNow_RegisterRecvCb(EspNow_RecvCb_t cb);

/**
 * @brief Register a callback for successful/failed sends.
 *
 * @param[in] cb Callback function to execute on send completion.
 * @return ESP_OK on success
 */
esp_err_t EspNow_RegisterSendCb(EspNow_SendCb_t cb);

/**
 * @brief Send data to a specific peer or broadcast.
 *
 * @param[in] mac_addr Destination MAC (or ESP_NOW_BROADCAST_MAC)
 * @param[in] data Pointer to data to send
 * @param[in] data_len Length of data to send (max 250 bytes)
 * @return ESP_OK on success
 */
esp_err_t EspNow_Send(const uint8_t *mac_addr, const uint8_t *data, size_t data_len);

/**
 * @brief Send data and wait for the link-layer delivery acknowledgement,
 *        retransmitting on failure.
 *
 * Unlike EspNow_Send() (fire-and-forget), this blocks until the ESP-NOW send
 * callback reports the result for each attempt. Use it for important unicast
 * messages such as valve commands. Do NOT use it for broadcast frames (which
 * always report success) or from within the ESP-NOW receive callback.
 *
 * @param[in] mac_addr     Destination unicast MAC (must be a registered peer).
 * @param[in] data         Payload pointer.
 * @param[in] data_len     Payload length (<= 250).
 * @param[in] max_attempts Total attempts including the first (>= 1).
 * @return ESP_OK if an attempt was acknowledged, ESP_ERR_TIMEOUT/ESP_FAIL otherwise.
 */
esp_err_t EspNow_SendReliable(const uint8_t *mac_addr, const uint8_t *data,
                              size_t data_len, uint8_t max_attempts);

#ifdef __cplusplus
}
#endif

#endif // ESP_NOW_DRIVER_H
