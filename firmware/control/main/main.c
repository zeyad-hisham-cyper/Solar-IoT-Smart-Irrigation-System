/*
 * Control Board Main Application
 *
 * Responsibilities:
 *  1. Discovery: broadcast a DISCOVERY_REQ so all zones announce themselves.
 *  2. Registration: assign an incremental Zone ID to each unique zone MAC and
 *     persist the MAC<->ID mapping in NVS.
 *  3. Data reception: accept ZONE_DATA packets and log them with their Zone ID.
 *  4. Irrigation control: manage the water pump and send valve commands to zones.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_mac.h"
#include "esp_now_driver.h"
#include "app_config.h"
#include "motor.h"

static const char *TAG = "CONTROL_MAIN";

/* ========================================================================== */
/*                          GLOBAL ACTUATORS / STATE                          */
/* ========================================================================== */

static motor_t s_pump;
static bool    s_pump_running = false;     /**< Tracks actual pump state to avoid redundant logs */

/* ========================================================================== */
/*                          NVS ZONE REGISTRY                                 */
/* ========================================================================== */

#define ZONE_NVS_NAMESPACE   "ctrl_zones"
#define ZONE_NVS_KEY_COUNT   "zone_count"      /**< Number of registered zones (uint8) */
/* Per-zone MAC stored as "zmac_<id>" -> 6-byte blob */
#define ZONE_NVS_MAC_KEY_FMT "zmac_%u"

/* Array mapped by Zone ID (1-based to MAX_ZONES). Index 0 is unused. */
static uint8_t   s_zone_needs_water[MAX_ZONES + 1] = {0};
static TickType_t s_zone_last_seen[MAX_ZONES + 1]  = {0};  /**< Last tick a ZONE_DATA was received */

/** In-RAM zone table (rebuilt from NVS on boot) */
static uint8_t  s_zone_macs[MAX_ZONES][6];
static uint8_t  s_zone_count = 0;

/**
 * @brief Load the zone registry from NVS into RAM.
 */
static void control_nvs_load_zones(void)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(ZONE_NVS_NAMESPACE, NVS_READONLY, &h);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "No zone registry found in NVS - starting fresh.");
        return;
    }

    uint8_t count = 0;
    if (nvs_get_u8(h, ZONE_NVS_KEY_COUNT, &count) == ESP_OK) {
        s_zone_count = (count <= MAX_ZONES) ? count : MAX_ZONES;
    }

    for (uint8_t i = 0; i < s_zone_count; i++) {
        char key[16];
        snprintf(key, sizeof(key), ZONE_NVS_MAC_KEY_FMT, (unsigned)(i + 1));
        size_t len = 6;
        nvs_get_blob(h, key, s_zone_macs[i], &len);
        ESP_LOGI(TAG, "  Loaded Zone %u -> %02X:%02X:%02X:%02X:%02X:%02X",
                 i + 1,
                 s_zone_macs[i][0], s_zone_macs[i][1], s_zone_macs[i][2],
                 s_zone_macs[i][3], s_zone_macs[i][4], s_zone_macs[i][5]);
    }

    nvs_close(h);
    ESP_LOGI(TAG, "Zone registry loaded: %u zone(s) known.", s_zone_count);
}

/**
 * @brief Look up the zone ID for a given raw MAC.
 * @return 1-based zone ID, or 0 if not found.
 */
static uint8_t control_nvs_find_zone(const uint8_t mac[6])
{
    for (uint8_t i = 0; i < s_zone_count; i++) {
        if (memcmp(s_zone_macs[i], mac, 6) == 0) {
            return i + 1; /* 1-based */
        }
    }
    return 0;
}

/**
 * @brief Register a new zone: assign next ID, save to NVS & RAM table.
 * @param mac Raw MAC of the new zone.
 * @return Newly assigned 1-based Zone ID, or 0 on failure.
 */
static uint8_t control_nvs_save_zone(const uint8_t mac[6])
{
    if (s_zone_count >= MAX_ZONES) {
        ESP_LOGE(TAG, "Zone table full! Cannot register more zones.");
        return 0;
    }

    uint8_t new_id = s_zone_count + 1;

    nvs_handle_t h;
    esp_err_t ret = nvs_open(ZONE_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(ret));
        return 0;
    }

    char key[16];
    snprintf(key, sizeof(key), ZONE_NVS_MAC_KEY_FMT, (unsigned)new_id);
    ret = nvs_set_blob(h, key, mac, 6);
    if (ret == ESP_OK) {
        ret = nvs_set_u8(h, ZONE_NVS_KEY_COUNT, new_id);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(h);
    }
    nvs_close(h);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS write failed: %s", esp_err_to_name(ret));
        return 0;
    }

    memcpy(s_zone_macs[s_zone_count], mac, 6);
    s_zone_count = new_id;

    ESP_LOGI(TAG, "New zone registered -> Zone ID %u (%02X:%02X:%02X:%02X:%02X:%02X)",
             new_id,
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return new_id;
}

/* ========================================================================== */
/*                             PUMP CONTROL LOGIC                             */
/* ========================================================================== */

/**
 * @brief Checks if any registered zone currently needs water.
 * @return true if at least one zone needs water, false otherwise.
 */
static bool control_any_zone_needs_water(void)
{
    for (uint8_t i = 1; i <= s_zone_count; i++) {
        if (s_zone_needs_water[i]) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Updates the motor state based on all zones' water needs.
 *        Only logs + acts when the pump state actually transitions.
 */
static void control_eval_pump_state(void)
{
    bool need = control_any_zone_needs_water();

    if (need && !s_pump_running) {
        ESP_LOGI(TAG, ">>> PUMP ON (at least one zone needs water)");
        motor_rotate(&s_pump, MOTOR_DIR_CW, MOTOR_SPEED_MAX);
        s_pump_running = true;
    } else if (!need && s_pump_running) {
        ESP_LOGI(TAG, ">>> PUMP OFF (no zones need water)");
        motor_stop(&s_pump);
        s_pump_running = false;
    }
    /* else: state unchanged, do nothing */
}

/* ========================================================================== */
/*                         ESP-NOW RECEIVE CALLBACK                           */
/* ========================================================================== */

void OnEspNowRecvCb(const uint8_t *mac_addr, const uint8_t *data, int data_len)
{
    if (data == NULL || data_len < (int)sizeof(EspNow_MessageHeader_t)) {
        return;
    }

    const EspNow_MessageHeader_t *header = (const EspNow_MessageHeader_t *)data;

    if (header->version != ESP_NOW_PROTOCOL_VERSION) {
        ESP_LOGW(TAG, "Unknown protocol version %d from %02X:%02X:%02X:%02X:%02X:%02X",
                 header->version,
                 mac_addr[0], mac_addr[1], mac_addr[2],
                 mac_addr[3], mac_addr[4], mac_addr[5]);
        return;
    }

    /* ---- DISCOVERY RESPONSE ---- */
    if (header->msg_type == ESP_NOW_MSG_TYPE_DISCOVERY_RESP &&
        data_len >= (int)sizeof(EspNow_DiscoveryRespPayload_t))
    {
        const EspNow_DiscoveryRespPayload_t *resp = (const EspNow_DiscoveryRespPayload_t *)data;

        /* De-obfuscate the MAC */
        uint8_t raw_mac[6];
        memcpy(raw_mac, resp->obfuscated_mac, 6);
        EspNow_ObfuscateMAC(raw_mac); /* XOR is its own inverse */

        ESP_LOGI(TAG, "DISCOVERY_RESP received. Zone real MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                 raw_mac[0], raw_mac[1], raw_mac[2],
                 raw_mac[3], raw_mac[4], raw_mac[5]);

        /* Look up or register the zone */
        uint8_t zone_id = control_nvs_find_zone(raw_mac);
        if (zone_id == 0) {
            zone_id = control_nvs_save_zone(raw_mac);
            if (zone_id == 0) return; /* Registration failed */
        } else {
            ESP_LOGI(TAG, "Zone already known -> Zone ID %u", zone_id);
        }

        /* Make sure the zone's radio MAC is a known peer before replying */
        EspNow_AddPeer(mac_addr);

        /* Send ZONE_ASSIGN back to the zone */
        EspNow_ZoneAssignPayload_t assign_pkt = {0};
        assign_pkt.header.version   = ESP_NOW_PROTOCOL_VERSION;
        assign_pkt.header.msg_type  = ESP_NOW_MSG_TYPE_ZONE_ASSIGN;
        assign_pkt.header.sender_id = 0; /* 0 = control */
        assign_pkt.assigned_zone_id = zone_id;

        esp_err_t err = EspNow_Send(mac_addr, (const uint8_t *)&assign_pkt, sizeof(assign_pkt));
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "ZONE_ASSIGN sent -> Zone ID %u", zone_id);
        } else {
            ESP_LOGE(TAG, "Failed to send ZONE_ASSIGN: %s", esp_err_to_name(err));
        }
        return;
    }

    /* ---- ZONE DATA ---- */
    if (header->msg_type == ESP_NOW_MSG_TYPE_ZONE_DATA &&
        data_len >= (int)sizeof(EspNow_ZoneDataPayload_t))
    {
        const EspNow_ZoneDataPayload_t *pkt = (const EspNow_ZoneDataPayload_t *)data;
        uint8_t zone_id = header->sender_id;

        float temp_c = (float)pkt->temperature_c_x10 / 10.0f;

        ESP_LOGI(TAG, "--- Data from Zone %u (MAC: %02X:%02X:%02X:%02X:%02X:%02X) ---",
                 zone_id,
                 mac_addr[0], mac_addr[1], mac_addr[2],
                 mac_addr[3], mac_addr[4], mac_addr[5]);
        ESP_LOGI(TAG, "  Moisture   : %u%%", pkt->moisture_percentage);
        ESP_LOGI(TAG, "  Needs Water: %s", pkt->needs_water ? "YES" : "NO");
        ESP_LOGI(TAG, "  Total Vol  : %lu mL", pkt->total_volume_ml);

        if (pkt->temperature_c_x10 == -999) {
            ESP_LOGI(TAG, "  Temperature: INVALID");
        } else {
            ESP_LOGI(TAG, "  Temperature: %.1f C", temp_c);
        }

        /* Update water tracking for this zone */
        if (zone_id >= 1 && zone_id <= MAX_ZONES) {
            bool state_changed = (s_zone_needs_water[zone_id] != pkt->needs_water);
            s_zone_needs_water[zone_id] = pkt->needs_water;
            s_zone_last_seen[zone_id]   = xTaskGetTickCount();

            /*
             * Only send a valve COMMAND when the water-need state has actually
             * changed. This avoids flooding the zone with redundant commands
             * on every sensor packet.
             */
            if (state_changed) {
                /* Ensure the zone is a peer before sending */
                EspNow_AddPeer(mac_addr);

                EspNow_CommandPayload_t cmd = {0};
                cmd.header.version   = ESP_NOW_PROTOCOL_VERSION;
                cmd.header.msg_type  = ESP_NOW_MSG_TYPE_COMMAND;
                cmd.header.sender_id = 0; /* Control */
                cmd.target_zone_id   = zone_id;
                cmd.open_valve       = pkt->needs_water;

                esp_err_t err = EspNow_Send(mac_addr, (const uint8_t *)&cmd, sizeof(cmd));
                if (err == ESP_OK) {
                    ESP_LOGI(TAG, "Sent COMMAND to Zone %u: %s VALVE", zone_id, cmd.open_valve ? "OPEN" : "CLOSE");
                } else {
                    ESP_LOGE(TAG, "Failed to send COMMAND: %s", esp_err_to_name(err));
                }

                /* Evaluate if the pump needs to turn ON or OFF */
                control_eval_pump_state();
            }
        }
        return;
    }

    ESP_LOGW(TAG, "Unhandled msg_type=%d len=%d", header->msg_type, data_len);
}

/* ========================================================================== */
/*                           ZONE HEARTBEAT CHECK                             */
/* ========================================================================== */

/**
 * @brief Check all zones for heartbeat timeout.
 *
 * If a zone hasn't sent ZONE_DATA within ZONE_HEARTBEAT_TIMEOUT_MS,
 * clear its needs_water flag. This prevents the pump from running
 * indefinitely if a zone board loses power or connectivity.
 */
static void control_check_zone_heartbeats(void)
{
    TickType_t now = xTaskGetTickCount();
    bool any_changed = false;

    for (uint8_t i = 1; i <= s_zone_count; i++) {
        if (s_zone_last_seen[i] == 0) continue; /* Never heard from this zone */

        TickType_t elapsed = now - s_zone_last_seen[i];
        if (elapsed >= pdMS_TO_TICKS(ZONE_HEARTBEAT_TIMEOUT_MS)) {
            if (s_zone_needs_water[i]) {
                ESP_LOGW(TAG, "Zone %u heartbeat timeout (%lu ms). Clearing needs_water.",
                         i, (unsigned long)(elapsed * portTICK_PERIOD_MS));
                s_zone_needs_water[i] = 0;
                any_changed = true;
            }
        }
    }

    if (any_changed) {
        control_eval_pump_state();
    }
}

/* ========================================================================== */
/*                                  APP MAIN                                  */
/* ========================================================================== */

void app_main(void)
{
    esp_err_t ret;

    /* Initialize NVS */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Load known zones from NVS */
    control_nvs_load_zones();

    ESP_LOGI(TAG, "Starting Control Board...");

    /* Initialize Water Pump */
    s_pump.pin_in_a = MOTOR_PIN_IN_A;
    s_pump.pin_in_b = MOTOR_PIN_IN_B;
    if (motor_init(&s_pump) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize water pump");
    }

    /* Initialize ESP-NOW */
    if (EspNow_Init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ESP-NOW! Halting.");
        return;
    }
    EspNow_RegisterRecvCb(OnEspNowRecvCb);
    ESP_LOGI(TAG, "ESP-NOW Initialized and callback registered.");

    /* Allow a small settle period, then broadcast discovery */
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI(TAG, "Broadcasting DISCOVERY_REQ...");

    EspNow_MessageHeader_t disc_req = {
        .version   = ESP_NOW_PROTOCOL_VERSION,
        .msg_type  = ESP_NOW_MSG_TYPE_DISCOVERY_REQ,
        .sender_id = 0
    };
    EspNow_Send(ESP_NOW_BROADCAST_MAC, (const uint8_t *)&disc_req, sizeof(disc_req));

    /* Main loop: periodically re-broadcast discovery and check heartbeats */
    TickType_t last_discovery = xTaskGetTickCount();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        /* Periodic re-discovery so late-starting zones can join */
        if ((xTaskGetTickCount() - last_discovery) >= pdMS_TO_TICKS(REDISCOVERY_INTERVAL_MS)) {
            ESP_LOGI(TAG, "Re-broadcasting DISCOVERY_REQ...");
            EspNow_Send(ESP_NOW_BROADCAST_MAC, (const uint8_t *)&disc_req, sizeof(disc_req));
            last_discovery = xTaskGetTickCount();
        }

        /* Check if any zone has gone silent */
        control_check_zone_heartbeats();
    }
}
