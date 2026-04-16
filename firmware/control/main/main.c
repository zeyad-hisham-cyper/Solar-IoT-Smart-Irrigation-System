/*
 * Control Board Main Application
 *
 * Responsibilities:
 *  1. Discovery: broadcast a DISCOVERY_REQ so all zones announce themselves.
 *  2. Registration: assign an incremental Zone ID to each unique zone MAC and
 *     persist the MAC<->ID mapping in NVS.
 *  3. Data reception: accept ZONE_DATA packets, validate, and feed to the
 *     Irrigation Engine.
 *  4. Irrigation control: delegated to the Irrigation Engine — runs as a
 *     periodic FreeRTOS task evaluating auto/manual modes with safety.
 *  5. Weather: background task fetches OpenWeather API and caches data.
 *  6. Firebase integration: upload gateway + zone + weather data,
 *     poll cloud mode & commands.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
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

/* ---- Firebase / WiFi headers ---- */
#include "wifi_manager.h"
#include "esp_wifi.h"
#include "firebase_config.h"
#include "firebase_auth.h"
#include "firebase_sync.h"

/* ---- New modules ---- */
#include "weather_service.h"
#include "irrigation_engine.h"

static const char *TAG = "CONTROL_MAIN";

/* ========================================================================== */
/*                          GLOBAL ACTUATORS / STATE                          */
/* ========================================================================== */

/* Pump motor handle — also accessed by irrigation_engine.c via extern */
motor_t s_pump;

/* ========================================================================== */
/*                          NVS ZONE REGISTRY                                 */
/* ========================================================================== */

#define ZONE_NVS_NAMESPACE   "ctrl_zones"
#define ZONE_NVS_KEY_COUNT   "zone_count"      /**< Number of registered zones (uint8) */
/* Per-zone MAC stored as "zmac_<id>" -> 6-byte blob */
#define ZONE_NVS_MAC_KEY_FMT "zmac_%u"

/** In-RAM zone table (rebuilt from NVS on boot) */
static uint8_t  s_zone_macs[MAX_ZONES][6];
static uint8_t  s_zone_count = 0;

/* ---- Per-zone MAC cache for sending commands back to zone boards ---- */
/* Also accessed by irrigation_engine.c via extern */
uint8_t  s_zone_radio_mac[MAX_ZONES + 1][6];  /**< Radio MAC (index by zone_id, 1-based) */
bool     s_zone_radio_mac_valid[MAX_ZONES + 1] = {false};

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
/*                         ESP-NOW RECEIVE CALLBACK                           */
/* ========================================================================== */

void OnEspNowRecvCb(const uint8_t *mac_addr, const uint8_t *data, int data_len)
{
    if (data == NULL || data_len < (int)sizeof(EspNow_MessageHeader_t)) {
        ESP_LOGW(TAG, "ESP-NOW: Received too-short packet (%d bytes) — dropped", data_len);
        return;
    }

    const EspNow_MessageHeader_t *header = (const EspNow_MessageHeader_t *)data;

    if (header->version != ESP_NOW_PROTOCOL_VERSION) {
        ESP_LOGW(TAG, "Unknown protocol version %d (expected %d) — dropped",
                 header->version, ESP_NOW_PROTOCOL_VERSION);
        return;
    }

    ESP_LOGI(TAG, "ESP-NOW RX from zone %02X:%02X:%02X:%02X:%02X:%02X (type=%d, len=%d)",
             mac_addr[0], mac_addr[1], mac_addr[2],
             mac_addr[3], mac_addr[4], mac_addr[5], header->msg_type, data_len);

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
            ESP_LOGI(TAG, "zone discovered: %02X:%02X:%02X:%02X:%02X:%02X (Assigned ID: %u)",
                     raw_mac[0], raw_mac[1], raw_mac[2], raw_mac[3], raw_mac[4], raw_mac[5], zone_id);
        } else {
            ESP_LOGI(TAG, "Zone already known -> Zone ID %u", zone_id);
        }

        /* Cache the radio MAC for sending commands later */
        if (zone_id >= 1 && zone_id <= MAX_ZONES) {
            memcpy(s_zone_radio_mac[zone_id], mac_addr, 6);
            s_zone_radio_mac_valid[zone_id] = true;
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

        float temp_c = (pkt->temperature_c_x10 == -999)
                         ? NAN
                         : (float)pkt->temperature_c_x10 / 10.0f;

        ESP_LOGI(TAG, "--- Data from Zone %u ---", zone_id);
        ESP_LOGI(TAG, "  Moisture   : %u%%", pkt->moisture_percentage);
        if (isnan(temp_c)) {
            ESP_LOGI(TAG, "  Temperature: INVALID");
        } else {
            ESP_LOGI(TAG, "  Temperature: %.1f C", temp_c);
        }
        ESP_LOGI(TAG, "  Total Vol  : %lu mL", pkt->total_volume_ml);

        /* Cache radio MAC for this zone */
        if (zone_id >= 1 && zone_id <= MAX_ZONES) {
            memcpy(s_zone_radio_mac[zone_id], mac_addr, 6);
            s_zone_radio_mac_valid[zone_id] = true;
        }

        /*
         * Feed sensor data to the Irrigation Engine.
         * The engine handles validation, threshold comparison, and decisions.
         * We do NOT make any irrigation decisions here.
         */
        esp_err_t ret = irrigation_engine_update_zone(
            zone_id,
            pkt->moisture_percentage,
            temp_c,
            (float)pkt->total_volume_ml
        );

        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Zone %u data rejected by irrigation engine", zone_id);
        }

        /* Also update the Firebase zone data cache for upload */
        if (zone_id >= 1 && zone_id <= NUM_ZONES) {
            int fb_idx = zone_id - 1;
            firebase_zone_data_t zd = {
                .soil_moisture = pkt->moisture_percentage,
                .temperature   = isnan(temp_c) ? 0 : temp_c,
                .flow_rate     = 0,
                .valve_open    = irrigation_engine_is_zone_irrigating(zone_id),
                .last_update   = 0,  /* Will use epoch in sync task */
                .threshold     = 0,  /* Engine holds the authoritative values */
                .duration      = 0,
                .valid         = true,
            };
            firebase_sync_update_zone(fb_idx, &zd);
        }

        return;
    }

    ESP_LOGW(TAG, "Unhandled msg_type=%d len=%d", header->msg_type, data_len);
}

/* ========================================================================== */
/*                       IRRIGATION CYCLE TASK                                */
/* ========================================================================== */

/**
 * @brief Periodically runs the irrigation decision engine.
 *
 * This is the single entry point for all irrigation decisions (auto + manual).
 * Replaces the old cloud_command_apply_task.
 */
static void irrigation_cycle_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Irrigation cycle task started (interval: %d ms)",
             IRRIGATION_CYCLE_INTERVAL_MS);

    /* Initial delay to let sensors report and weather fetch */
    vTaskDelay(pdMS_TO_TICKS(5000));

    while (1) {
        irrigation_engine_run_cycle();

        /* Keep Firebase in sync with current pump state */
        firebase_sync_update_gateway(irrigation_engine_is_pump_on(), 0);

        vTaskDelay(pdMS_TO_TICKS(IRRIGATION_CYCLE_INTERVAL_MS));
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

    /* Initialize Irrigation Engine */
    irrigation_engine_init();

    /* Initialize ESP-NOW */
    ESP_LOGI(TAG, "Initializing ESP-NOW...");
    if (EspNow_Init(0) != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW init failed! Halting.");
        return;
    }
    EspNow_RegisterRecvCb(OnEspNowRecvCb);
    ESP_LOGI(TAG, "ESP-NOW Initialized and callback registered.");

    /* ================================================================== */
    /*                    WiFi + Firebase Initialization                   */
    /* ================================================================== */

    ESP_LOGI(TAG, "Connecting to WiFi AP \"%s\"...", WIFI_SSID);
    if (wifi_manager_init() != ESP_OK) {
        ESP_LOGE(TAG, "WiFi manager init failed");
    }

    ret = wifi_manager_wait_connected(30000);  /* 30 second timeout */
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi connection failed — Firebase and weather will be unavailable");
        ESP_LOGW(TAG, "Irrigation engine will run with default config and cached weather");
    } else {
        /* Log the WiFi channel — zone boards must scan to align with this */
        uint8_t primary_chan = 0;
        wifi_second_chan_t secondary_chan;
        esp_wifi_get_channel(&primary_chan, &secondary_chan);
        ESP_LOGI(TAG, "WiFi connected on channel %d.", primary_chan);
        
        /* Explicitly set ESP-NOW to that channel */
        ESP_LOGI(TAG, "Explicitly setting ESP-NOW to channel %d", primary_chan);
        esp_wifi_set_channel(primary_chan, WIFI_SECOND_CHAN_NONE);

        ESP_LOGI(TAG, "Logging in to Firebase...");

        ret = firebase_auth_login();
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Firebase login successful!");
        } else {
            ESP_LOGE(TAG, "Firebase login failed — will retry in background tasks");
        }
    }

    /* Start background tasks (they handle not-logged-in state internally) */
    xTaskCreate(firebase_upload_task, "fb_upload",  12288, NULL, 3, NULL);
    xTaskCreate(firebase_poll_task,   "fb_poll",    12288, NULL, 3, NULL);
    xTaskCreate(weather_task,         "weather",     8192, NULL, 3, NULL);
    xTaskCreate(irrigation_cycle_task,"irr_cycle",   4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "All background tasks started.");

    /* ================================================================== */
    /*                        ESP-NOW Discovery Loop                       */
    /* ================================================================== */

    /* Allow a small settle period, then broadcast discovery */
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI(TAG, "Broadcasting DISCOVERY_REQ...");

    EspNow_MessageHeader_t disc_req = {
        .version   = ESP_NOW_PROTOCOL_VERSION,
        .msg_type  = ESP_NOW_MSG_TYPE_DISCOVERY_REQ,
        .sender_id = 0
    };
    EspNow_Send(ESP_NOW_BROADCAST_MAC, (const uint8_t *)&disc_req, sizeof(disc_req));

    /* Main loop: periodically re-broadcast discovery */
    TickType_t last_discovery = xTaskGetTickCount();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        /* Periodic re-discovery so late-starting zones can join */
        if ((xTaskGetTickCount() - last_discovery) >= pdMS_TO_TICKS(REDISCOVERY_INTERVAL_MS)) {
            uint8_t current_chan = 0;
            wifi_second_chan_t second_chan;
            esp_wifi_get_channel(&current_chan, &second_chan);
            
            ESP_LOGI(TAG, "DISCOVERY_REQ sent (channel=%d)", current_chan);
            EspNow_Send(ESP_NOW_BROADCAST_MAC, (const uint8_t *)&disc_req, sizeof(disc_req));
            last_discovery = xTaskGetTickCount();
        }

        /* Note: heartbeat checking is now handled inside the irrigation engine */
    }
}
