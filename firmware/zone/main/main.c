/*
 * Zone Board Main Application
 *
 * Responsibilities:
 *  1. Sensor Initialization: soil moisture + DS18B20 temperature.
 *  2. Zone ID Assignment:
 *       - On boot, try to load a previously assigned zone_id from NVS.
 *       - If not found, wait for a DISCOVERY_REQ from the control, reply with
 *         a DISCOVERY_RESP containing the obfuscated local MAC, then wait for
 *         a ZONE_ASSIGN packet and save the assigned ID to NVS.
 *  3. Interrupt-Driven Calibration:
 *       - A GPIO ISR on the calibration button wakes a CalibrationTask at any
 *         time during normal operation (no more 5-second boot window).
 *       - The task pauses the reading loop, runs full calibration, then resumes.
 *  4. Sensor Reading Loop: reads soil + temp and sends ZONE_DATA via ESP-NOW.
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#include "app_config.h"
#include "soil_moisture.h"
#include "temp_ds18b20.h"
#include "esp_now_driver.h"
#include "valve.h"
#include "flow_meter.h"

static const char *TAG = "ZONE_APP";

/* ========================================================================== */
/*                         GLOBAL ACTUATORS / SENSORS                         */
/* ========================================================================== */

static valve_t       s_valve;
static flow_meter_t  s_flow_meter;


/* ========================================================================== */
/*                         NVS ZONE ID STORAGE                                */
/* ========================================================================== */

#define ZONE_NVS_NAMESPACE  "zone_cfg"
#define ZONE_NVS_KEY_ID     "zone_id"
#define ZONE_NVS_KEY_MAC    "ctrl_mac"

/** @brief Maximum consecutive send failures before clearing NVS and restarting discovery. */
#define SEND_FAIL_THRESHOLD  5

static uint8_t s_zone_id = 0; /**< 0 means "not yet assigned" */
static uint8_t s_control_mac[6] = {0};
static int     s_send_fail_count = 0;  /**< Consecutive ESP-NOW send failures */

/**
 * @brief Check whether a 6-byte MAC address is all zeros (invalid).
 * @return true if every byte is 0x00.
 */
static bool is_zero_mac(const uint8_t mac[6])
{
    for (int i = 0; i < 6; i++) {
        if (mac[i] != 0) return false;
    }
    return true;
}

/**
 * @brief Erase the stored zone config (zone ID + gateway MAC) from NVS.
 */
static esp_err_t zone_nvs_clear_config(void)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(ZONE_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (ret != ESP_OK) return ret;
    nvs_erase_key(h, ZONE_NVS_KEY_ID);
    nvs_erase_key(h, ZONE_NVS_KEY_MAC);
    ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}

/**
 * @brief Ensure the gateway MAC is registered as an ESP-NOW peer.
 *
 * Removes any stale entry first, then adds fresh.  Logs every step.
 *
 * @param mac  6-byte gateway MAC to register.
 * @return ESP_OK on success.
 */
static esp_err_t ensure_gateway_peer(const uint8_t mac[6])
{
    ESP_LOGI(TAG, "ensure_gateway_peer: MAC=%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    if (is_zero_mac(mac)) {
        ESP_LOGE(TAG, "ensure_gateway_peer: REFUSED — MAC is all zeros");
        return ESP_ERR_INVALID_ARG;
    }

    /* Remove old entry (ignore "not found" — it just means it wasn't there) */
    if (esp_now_is_peer_exist(mac)) {
        esp_err_t del = esp_now_del_peer(mac);
        ESP_LOGI(TAG, "ensure_gateway_peer: del_peer result=%s", esp_err_to_name(del));
    }

    esp_now_peer_info_t pi = {0};
    memcpy(pi.peer_addr, mac, 6);
    pi.channel = 0;        /* use current channel */
    pi.encrypt = false;
    pi.ifidx   = WIFI_IF_STA;

    esp_err_t add = esp_now_add_peer(&pi);
    ESP_LOGI(TAG, "ensure_gateway_peer: add_peer result=%s  peer_exist=%s",
             esp_err_to_name(add),
             esp_now_is_peer_exist(mac) ? "YES" : "NO");
    return add;
}

static esp_err_t zone_nvs_save_config(uint8_t id, const uint8_t *mac)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(ZONE_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (ret != ESP_OK) return ret;
    ret = nvs_set_u8(h, ZONE_NVS_KEY_ID, id);
    if (ret == ESP_OK) ret = nvs_set_blob(h, ZONE_NVS_KEY_MAC, mac, 6);
    if (ret == ESP_OK) ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}

static uint8_t zone_nvs_load_config(void)
{
    nvs_handle_t h;
    if (nvs_open(ZONE_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return 0;
    
    uint8_t id = 0;
    size_t mac_len = 6;
    if (nvs_get_u8(h, ZONE_NVS_KEY_ID, &id) == ESP_OK) {
        if (nvs_get_blob(h, ZONE_NVS_KEY_MAC, s_control_mac, &mac_len) == ESP_OK && mac_len == 6) {
            nvs_close(h);
            return id;
        }
    }
    nvs_close(h);
    return 0;
}

/* ========================================================================== */
/*                         DISCOVERY / ASSIGNMENT                             */
/* ========================================================================== */

/* Event bits */
#define EVT_DISC_REQ_RECEIVED  BIT0
#define EVT_ZONE_ASSIGNED      BIT1

static EventGroupHandle_t  s_discovery_eg = NULL;
static uint8_t             s_assigned_id  = 0; /**< ID received from control */

/* Current sensor states for the transmission task */
static uint8_t  s_current_sm = 0;
static float    s_current_temp = 0.0f;
static bool     s_current_temp_valid = false;
static float    s_current_flow = 0.0f;
static uint32_t s_current_total_vol = 0;
static uint8_t  s_current_needs_water = 0;

/* ========================================================================== */
/*                         CALIBRATION INTERRUPT                              */
/* ========================================================================== */

static SemaphoreHandle_t s_cal_semaphore  = NULL;   /**< ISR → CalibrationTask */
static volatile bool     s_is_calibrating = false;  /**< reading loop checks this */

/**
 * @brief GPIO ISR for the calibration button.
 *
 * Runs in interrupt context – only post the semaphore, nothing else.
 */
static void IRAM_ATTR calibration_button_isr(void *arg)
{
    BaseType_t higher_prio_woken = pdFALSE;
    xSemaphoreGiveFromISR(s_cal_semaphore, &higher_prio_woken);
    portYIELD_FROM_ISR(higher_prio_woken);
}

/* ========================================================================== */
/*                         ESP-NOW RECEIVE CALLBACK                           */
/* ========================================================================== */

static void OnEspNowSendCb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    if (status == ESP_NOW_SEND_SUCCESS) {
        s_send_fail_count = 0;
        ESP_LOGI(TAG, "ZONE: send OK (fail counter reset)");
    } else {
        s_send_fail_count++;
        ESP_LOGE(TAG, "ZONE: send FAIL (consecutive failures: %d/%d)",
                 s_send_fail_count, SEND_FAIL_THRESHOLD);
    }
}

static void OnEspNowRecvCb(const uint8_t *mac_addr, const uint8_t *data, int data_len)
{
    if (data == NULL || data_len < (int)sizeof(EspNow_MessageHeader_t)) return;

    const EspNow_MessageHeader_t *header = (const EspNow_MessageHeader_t *)data;

    if (header->version != ESP_NOW_PROTOCOL_VERSION) return;

    switch (header->msg_type) {

    case ESP_NOW_MSG_TYPE_DISCOVERY_REQ:
        /* Control is broadcasting; save its MAC and signal the discovery task */
        memcpy(s_control_mac, mac_addr, 6);
        ESP_LOGI(TAG, "DISCOVERY_REQ from sender MAC %02X:%02X:%02X:%02X:%02X:%02X",
                 mac_addr[0], mac_addr[1], mac_addr[2],
                 mac_addr[3], mac_addr[4], mac_addr[5]);
        ESP_LOGI(TAG, "  -> s_control_mac updated to %02X:%02X:%02X:%02X:%02X:%02X",
                 s_control_mac[0], s_control_mac[1], s_control_mac[2],
                 s_control_mac[3], s_control_mac[4], s_control_mac[5]);
        xEventGroupSetBits(s_discovery_eg, EVT_DISC_REQ_RECEIVED);
        break;

    case ESP_NOW_MSG_TYPE_ZONE_ASSIGN:
        if (data_len >= (int)sizeof(EspNow_ZoneAssignPayload_t)) {
            const EspNow_ZoneAssignPayload_t *pkt = (const EspNow_ZoneAssignPayload_t *)data;
            s_assigned_id = pkt->assigned_zone_id;
            /*
             * CRITICAL FIX: Always capture the sender's MAC here.
             * When the control responds directly to a broadcast DISCOVERY_RESP
             * with ZONE_ASSIGN (without first sending DISCOVERY_REQ),
             * this is the ONLY place we learn the gateway's real MAC.
             */
            memcpy(s_control_mac, mac_addr, 6);
            ESP_LOGI(TAG, "ZONE_ASSIGN from sender MAC %02X:%02X:%02X:%02X:%02X:%02X, Zone ID=%u",
                     mac_addr[0], mac_addr[1], mac_addr[2],
                     mac_addr[3], mac_addr[4], mac_addr[5],
                     s_assigned_id);
            ESP_LOGI(TAG, "  -> s_control_mac set to %02X:%02X:%02X:%02X:%02X:%02X",
                     s_control_mac[0], s_control_mac[1], s_control_mac[2],
                     s_control_mac[3], s_control_mac[4], s_control_mac[5]);
            xEventGroupSetBits(s_discovery_eg, EVT_ZONE_ASSIGNED);
        }
        break;

    case ESP_NOW_MSG_TYPE_COMMAND:
        if (data_len >= (int)sizeof(EspNow_CommandPayload_t)) {
            const EspNow_CommandPayload_t *cmd = (const EspNow_CommandPayload_t *)data;
            ESP_LOGI(TAG, "Evaluating COMMAND: Target Zone = %u, Open_Valve = %u. My Zone = %u",
                     cmd->target_zone_id, cmd->open_valve, s_zone_id);

            if (cmd->target_zone_id == s_zone_id) {
                if (cmd->open_valve) {
                    ESP_LOGI(TAG, "==== Command Executing: OPEN VALVE ====");
                    esp_err_t err = valve_open(&s_valve);
                    if (err != ESP_OK) ESP_LOGE(TAG, "Valve Open Error: %s", esp_err_to_name(err));
                } else {
                    ESP_LOGI(TAG, "==== Command Executing: CLOSE VALVE ====");
                    esp_err_t err = valve_close(&s_valve);
                    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
                         ESP_LOGW(TAG, "Valve Close Notice: %s", esp_err_to_name(err));
                    }
                }
            } else {
                ESP_LOGI(TAG, "Command ignored (Target ID %u does not match my ID %u)", cmd->target_zone_id, s_zone_id);
            }
        } else {
            ESP_LOGW(TAG, "Received COMMAND with invalid length %d", data_len);
        }
        break;

    default:
        break;
    }
}

/* ========================================================================== */
/*                         DISCOVERY HANDSHAKE                                */
/* ========================================================================== */

/**
 * @brief Perform the discovery handshake with the control board.
 *
 * Blocks until the control sends a DISCOVERY_REQ, replies with an obfuscated
 * MAC, then waits for a ZONE_ASSIGN packet.  Sets s_zone_id on success.
 */
static void zone_perform_discovery(void)
{
    ESP_LOGI(TAG, "=== DISCOVERY START ===");

    /* Clear any stale state so we don't trust leftovers */
    memset(s_control_mac, 0, 6);

    /* Prepare DISCOVERY_RESP payload to actively ping the gateway */
    uint8_t my_mac[6];
    esp_read_mac(my_mac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "My STA MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             my_mac[0], my_mac[1], my_mac[2],
             my_mac[3], my_mac[4], my_mac[5]);

    EspNow_DiscoveryRespPayload_t resp = {0};
    resp.header.version   = ESP_NOW_PROTOCOL_VERSION;
    resp.header.msg_type  = ESP_NOW_MSG_TYPE_DISCOVERY_RESP;
    resp.header.sender_id = 0; /* ID not yet known */
    memcpy(resp.obfuscated_mac, my_mac, 6);
    EspNow_ObfuscateMAC(resp.obfuscated_mac);

    uint8_t current_channel = 1;

    while (1) {
        esp_wifi_set_channel(current_channel, WIFI_SECOND_CHAN_NONE);
        ESP_LOGI(TAG, "Scanning channel %d...", current_channel);

        /* Actively probe for the control board */
        EspNow_Send(ESP_NOW_BROADCAST_MAC, (const uint8_t *)&resp, sizeof(resp));

        /* Wait up to 500ms for a ZONE_ASSIGN response (or serendipitous DISCOVERY_REQ) */
        EventBits_t bits = xEventGroupWaitBits(s_discovery_eg, 
                                               EVT_DISC_REQ_RECEIVED | EVT_ZONE_ASSIGNED,
                                               pdTRUE, pdFALSE,
                                               pdMS_TO_TICKS(500));

        if (bits & EVT_ZONE_ASSIGNED) {
            /* ZONE_ASSIGN handler already captured mac_addr → s_control_mac */
            ESP_LOGI(TAG, "ZONE_ASSIGN received directly on channel %d", current_channel);
            ESP_LOGI(TAG, "  gateway MAC from callback: %02X:%02X:%02X:%02X:%02X:%02X",
                     s_control_mac[0], s_control_mac[1], s_control_mac[2],
                     s_control_mac[3], s_control_mac[4], s_control_mac[5]);

            /* Validate before committing */
            if (is_zero_mac(s_control_mac)) {
                ESP_LOGE(TAG, "BUG: gateway MAC is still 00:00:00:00:00:00 after ZONE_ASSIGN — retrying");
                continue; /* keep scanning */
            }

            s_zone_id = s_assigned_id;
            s_send_fail_count = 0;

            /* Register peer with ESP-NOW */
            ensure_gateway_peer(s_control_mac);

            /* Save to NVS */
            esp_err_t err = zone_nvs_save_config(s_zone_id, s_control_mac);
            ESP_LOGI(TAG, "NVS save: zone_id=%u MAC=%02X:%02X:%02X:%02X:%02X:%02X result=%s",
                     s_zone_id,
                     s_control_mac[0], s_control_mac[1], s_control_mac[2],
                     s_control_mac[3], s_control_mac[4], s_control_mac[5],
                     esp_err_to_name(err));

            ESP_LOGI(TAG, "=== DISCOVERY SUCCESS (zone_id=%u, channel=%d) ===",
                     s_zone_id, current_channel);
            return;
        }

        if (bits & EVT_DISC_REQ_RECEIVED) {
            ESP_LOGI(TAG, "DISCOVERY_REQ on channel %d, replying with DISCOVERY_RESP", current_channel);
            ESP_LOGI(TAG, "  gateway MAC from callback: %02X:%02X:%02X:%02X:%02X:%02X",
                     s_control_mac[0], s_control_mac[1], s_control_mac[2],
                     s_control_mac[3], s_control_mac[4], s_control_mac[5]);

            /* Add peer so unicast DISCOVERY_RESP can be sent */
            ensure_gateway_peer(s_control_mac);
            EspNow_Send(s_control_mac, (const uint8_t *)&resp, sizeof(resp));

            /* Wait a bit longer for the ZONE_ASSIGN on this matched channel */
            EventBits_t bits2 = xEventGroupWaitBits(s_discovery_eg, EVT_ZONE_ASSIGNED,
                                                    pdTRUE, pdTRUE, pdMS_TO_TICKS(3000));
            if (bits2 & EVT_ZONE_ASSIGNED) {
                /* ZONE_ASSIGN handler may have updated s_control_mac again */
                ESP_LOGI(TAG, "ZONE_ASSIGN after DISCOVERY_REQ on channel %d", current_channel);
                ESP_LOGI(TAG, "  gateway MAC from callback: %02X:%02X:%02X:%02X:%02X:%02X",
                         s_control_mac[0], s_control_mac[1], s_control_mac[2],
                         s_control_mac[3], s_control_mac[4], s_control_mac[5]);

                if (is_zero_mac(s_control_mac)) {
                    ESP_LOGE(TAG, "BUG: gateway MAC is still zero after ZONE_ASSIGN — retrying");
                    continue;
                }

                s_zone_id = s_assigned_id;
                s_send_fail_count = 0;

                /* Re-ensure peer (MAC may have changed between REQ and ASSIGN) */
                ensure_gateway_peer(s_control_mac);

                esp_err_t save_err = zone_nvs_save_config(s_zone_id, s_control_mac);
                ESP_LOGI(TAG, "NVS save: zone_id=%u MAC=%02X:%02X:%02X:%02X:%02X:%02X result=%s",
                         s_zone_id,
                         s_control_mac[0], s_control_mac[1], s_control_mac[2],
                         s_control_mac[3], s_control_mac[4], s_control_mac[5],
                         esp_err_to_name(save_err));

                ESP_LOGI(TAG, "=== DISCOVERY SUCCESS (zone_id=%u, channel=%d) ===",
                         s_zone_id, current_channel);
                return;
            }
        }

        current_channel++;
        if (current_channel > 13) current_channel = 1;
    }
}

/* ========================================================================== */
/*                         CALIBRATION TASK                                   */
/* ========================================================================== */

/**
 * @brief Calibration FreeRTOS task.
 *
 * Waits on the semaphore posted by the button ISR.  When triggered, it sets a
 * flag that pauses the reading loop, runs the full interactive calibration
 * routine, then clears the flag so the reading loop continues.
 *
 * @param arg  Pointer to the Soil_Moisture_Config_t used by the reading loop.
 */
static void CalibrationTask(void *arg)
{
    Soil_Moisture_Config_t *soil_cfg = (Soil_Moisture_Config_t *)arg;

    while (true) {
        /* Block until button ISR posts the semaphore */
        if (xSemaphoreTake(s_cal_semaphore, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Calibration triggered by button press!");

            /*
             * Disable the GPIO interrupt for the duration of calibration.
             * This prevents extra semaphore posts from button presses that
             * soil_wait_for_button_press() handles internally via polling,
             * which would otherwise cause a spurious re-trigger after the
             * calibration routine returns.
             */
            gpio_isr_handler_remove(soil_cfg->calibration_button_gpio);

            /* Signal reading loop to pause.
             *
             * Wait longer than the worst-case read cycle before taking over
             * the ADC/power GPIO.  Worst case: DS18B20 with 3 retries at
             * 750 ms each (~2.5 s) + ADC samples + margins ≈ 3 s.
             * Without this, soil_moisture_power_off() from the in-progress
             * reading-loop iteration can race against us and power the sensor
             * off while we are mid-calibration-read.
             */
            s_is_calibrating = true;
            ESP_LOGI(TAG, "Waiting for any active sensor read to finish...");
            vTaskDelay(pdMS_TO_TICKS(3000));

            esp_err_t ret = Soil_Moisture_UserCalibrate(soil_cfg);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Calibration completed successfully. Resuming normal operation.");
            } else {
                ESP_LOGE(TAG, "Calibration failed: %s. Resuming normal operation.", esp_err_to_name(ret));
            }

            /* Resume reading loop */
            s_is_calibrating = false;

            /*
             * Drain any semaphore counts that accumulated from button presses
             * during calibration before re-arming the ISR, so we don't
             * immediately start another calibration cycle.
             */
            while (xSemaphoreTake(s_cal_semaphore, 0) == pdTRUE) { /* flush */ }

            /* Re-arm the ISR so future presses can trigger calibration again */
            gpio_isr_handler_add(soil_cfg->calibration_button_gpio,
                                 calibration_button_isr, NULL);
        }
    }
}

/* ========================================================================== */
/*                         VALVE SAFETY TIMEOUT TASK                          */
/* ========================================================================== */

/**
 * @brief Auto-close the valve if it has been open longer than VALVE_TIMEOUT_MS.
 *
 * Runs as a background FreeRTOS task. This is a safety mechanism to prevent
 * flooding if communication with the Control board is lost.
 */
static void ValveSafetyTask(void *arg)
{
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000)); /* Check every 5 seconds */

        if (s_valve.state == VALVE_STATE_OPENED && s_valve.open_start_us != 0U) {
            uint64_t elapsed_ms = ((uint64_t)esp_timer_get_time() - s_valve.open_start_us) / 1000ULL;
            if (elapsed_ms >= VALVE_TIMEOUT_MS) {
                ESP_LOGW(TAG, "SAFETY: Valve open for %llu ms (limit=%u ms). Auto-closing!",
                         elapsed_ms, VALVE_TIMEOUT_MS);
                valve_close(&s_valve);
            }
        }
    }
}

/* ========================================================================== */
/*                         PERIODIC TRANSMISSION TASK                         */
/* ========================================================================== */

static void ZoneTransmissionTask(void *arg)
{
    (void)arg;
    uint32_t tick = 0;
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        
        ESP_LOGI(TAG, "ZONE: loop tick %lu", (unsigned long)tick++);
        
        if (s_zone_id == 0 || is_zero_mac(s_control_mac)) {
            /* Not paired yet — do not attempt to send */
            ESP_LOGW(TAG, "ZONE: not paired (zone_id=%u, mac_valid=%s) — waiting",
                     s_zone_id, is_zero_mac(s_control_mac) ? "NO" : "YES");
            continue;
        }

        /* ------ Send-fail recovery: too many consecutive failures → re-discover ------ */
        if (s_send_fail_count >= SEND_FAIL_THRESHOLD) {
            ESP_LOGW(TAG, "RECOVERY: %d consecutive send failures → clearing NVS and restarting discovery",
                     s_send_fail_count);
            zone_nvs_clear_config();
            memset(s_control_mac, 0, 6);
            s_zone_id = 0;
            s_send_fail_count = 0;
            zone_perform_discovery();
            ESP_LOGI(TAG, "RECOVERY: discovery complete, resuming transmissions");
            continue;
        }

        /* Verify peer is registered before every send */
        if (!esp_now_is_peer_exist(s_control_mac)) {
            ESP_LOGW(TAG, "ZONE: peer not in table — re-adding");
            ensure_gateway_peer(s_control_mac);
        }

        /* Build and send ZONE_DATA packet using last known good values */
        EspNow_ZoneDataPayload_t pkt = {0};
        pkt.header.version   = ESP_NOW_PROTOCOL_VERSION;
        pkt.header.msg_type  = ESP_NOW_MSG_TYPE_ZONE_DATA;
        pkt.header.sender_id = s_zone_id;

        pkt.moisture_percentage = s_current_sm;
        pkt.temperature_c_x10   = s_current_temp_valid
                                  ? (int16_t)(s_current_temp * 10.0f)
                                  : -999;
        pkt.needs_water = s_current_needs_water;
        pkt.total_volume_ml = s_current_total_vol;

        ESP_LOGI(TAG, "ZONE: TX -> %02X:%02X:%02X:%02X:%02X:%02X  zone_id=%u  SM=%u  fail_cnt=%d",
                 s_control_mac[0], s_control_mac[1], s_control_mac[2],
                 s_control_mac[3], s_control_mac[4], s_control_mac[5],
                 s_zone_id, pkt.moisture_percentage, s_send_fail_count);

        esp_err_t send_err = EspNow_Send(s_control_mac, (const uint8_t *)&pkt, sizeof(pkt));
        if (send_err != ESP_OK) {
            ESP_LOGE(TAG, "ZONE: esp_now_send error: %s", esp_err_to_name(send_err));
            if (send_err == ESP_ERR_ESPNOW_NOT_FOUND) {
                ESP_LOGW(TAG, "ZONE: peer NOT_FOUND — forcing re-add");
                ensure_gateway_peer(s_control_mac);
            }
        }
    }
}

/* ========================================================================== */
/*                               APP MAIN                                     */
/* ==========================================================================  */

void app_main(void)
{
    esp_err_t ret;

    /* --- NVS Init --- */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Zone Board starting...");

    /* --- Soil Moisture Sensor --- */
    Soil_Moisture_Config_t soil_config = {
        .adc_unit              = SOIL_ADC_UNIT,
        .adc_channel           = SOIL_ADC_CHANNEL,
        .adc_attenuation       = SOIL_ADC_ATTEN,
        .adc_bitwidth          = SOIL_ADC_BITWIDTH,
        .power_gpio            = SOIL_POWER_GPIO,
        .calibration_button_gpio = SOIL_CALIBRATION_BUTTON,
        .read_delay_ms         = SOIL_READ_DELAY_MS,
        .adc_samples           = SOIL_ADC_SAMPLES
    };

    ESP_LOGI(TAG, "Initializing Soil Moisture Driver...");
    ret = Soil_Moisture_Init(&soil_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Soil Moisture Init failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "Soil Moisture Driver ready.");

    /* --- DS18B20 Temperature --- */
    Temp_DS18B20_Config_t temp_config = {
        .data_gpio             = DS18B20_DATA_GPIO,
        .enable_internal_pullup = DS18B20_INTERNAL_PULLUP,
        .max_retries           = DS18B20_MAX_RETRIES,
        .resolution            = (Temp_DS18B20_Resolution_t)DS18B20_RESOLUTION_BITS
    };
    if (Temp_DS18B20_Init(&temp_config) != ESP_OK) {
        ESP_LOGW(TAG, "DS18B20 not detected or init failed. Temp readings will be invalid.");
    }

    /* --- Valve --- */
    s_valve.control_pin = VALVE_CONTROL_PIN;
    s_valve.timeout_ms  = VALVE_TIMEOUT_MS;
    if (valve_init(&s_valve) != ESP_OK) {
        ESP_LOGE(TAG, "Valve init failed");
    }

    /* --- Flow Meter --- */
    s_flow_meter.signal_pin = FLOW_METER_PIN;
    if (flow_meter_init(&s_flow_meter) != ESP_OK) {
        ESP_LOGE(TAG, "Flow meter init failed");
    }

    /* --- Calibration Button: GPIO Interrupt Setup --- */
    s_cal_semaphore = xSemaphoreCreateBinary();
    configASSERT(s_cal_semaphore != NULL);

    /*
     * The calibration button GPIO was already configured as INPUT with
     * pull-up by Soil_Moisture_Init(). We only need to install the ISR.
     *
     * We reconfigure the interrupt type here to FALLING (active-low button).
     */
    gpio_set_intr_type(soil_config.calibration_button_gpio, GPIO_INTR_NEGEDGE);
    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(ret));
    }
    gpio_isr_handler_add(soil_config.calibration_button_gpio,
                         calibration_button_isr, NULL);
    ESP_LOGI(TAG, "Calibration button ISR installed on GPIO %d.",
             soil_config.calibration_button_gpio);

    /* Create the calibration task (priority just above idle) */
    xTaskCreate(CalibrationTask, "calibration", 4096, &soil_config, 2, NULL);

    /* Create the valve safety timeout task */
    xTaskCreate(ValveSafetyTask, "valve_safety", 2048, NULL, 1, NULL);

    /* --- ESP-NOW --- */
    s_discovery_eg = xEventGroupCreate();
    configASSERT(s_discovery_eg != NULL);

    ESP_LOGI(TAG, "Initializing ESP-NOW...");
    if (EspNow_Init(ESP_NOW_CHANNEL) != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW init failed! Halting.");
        return;
    }
    EspNow_RegisterRecvCb(OnEspNowRecvCb);
    EspNow_RegisterSendCb(OnEspNowSendCb);

    /* --- Zone ID: load from NVS or perform discovery handshake --- */
    s_zone_id = zone_nvs_load_config();
    ESP_LOGI(TAG, "NVS load: zone_id=%u, MAC=%02X:%02X:%02X:%02X:%02X:%02X, mac_valid=%s",
             s_zone_id,
             s_control_mac[0], s_control_mac[1], s_control_mac[2],
             s_control_mac[3], s_control_mac[4], s_control_mac[5],
             is_zero_mac(s_control_mac) ? "NO" : "YES");

    if (s_zone_id != 0 && !is_zero_mac(s_control_mac)) {
        ESP_LOGI(TAG, "MAC validation PASSED — using stored config.");
        ensure_gateway_peer(s_control_mac);
    } else {
        if (s_zone_id != 0 && is_zero_mac(s_control_mac)) {
            ESP_LOGW(TAG, "Invalid stored MAC with zone_id=%u → clearing NVS, forcing discovery", s_zone_id);
            zone_nvs_clear_config();
            s_zone_id = 0;
        } else {
            ESP_LOGI(TAG, "No stored zone config → starting discovery");
        }
        zone_perform_discovery();
    }

    /* Final gate: do not proceed unless pairing is fully valid */
    if (s_zone_id == 0 || is_zero_mac(s_control_mac)) {
        ESP_LOGE(TAG, "FATAL: Discovery returned without valid pairing — restarting");
        esp_restart();
    }
    ESP_LOGI(TAG, "=== PAIRING CONFIRMED: zone_id=%u  gateway=%02X:%02X:%02X:%02X:%02X:%02X  peer_exist=%s ===",
             s_zone_id,
             s_control_mac[0], s_control_mac[1], s_control_mac[2],
             s_control_mac[3], s_control_mac[4], s_control_mac[5],
             esp_now_is_peer_exist(s_control_mac) ? "YES" : "NO");

    ESP_LOGI(TAG, "Zone ID confirmed: %u. Entering sensor reading loop...", s_zone_id);

    /* --- Sensor Reading Loop --- */
    Soil_Moisture_Readings_t soil_readings;
    Temp_DS18B20_Readings_t  temp_readings;

    /* Create the transmission task (highest priority among bg tasks) */
    xTaskCreate(ZoneTransmissionTask, "zone_tx", 4096, NULL, 5, NULL);

    while (1) {
        /* Pause while calibrating */
        if (s_is_calibrating) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        /* Read sensors */
        ret = Soil_Moisture_Read(&soil_config, &soil_readings);
        (void)Temp_DS18B20_Read(&temp_config, &temp_readings);

        if (ret == ESP_OK) {
            s_current_sm = soil_readings.moisture_percentage;
            s_current_temp_valid = temp_readings.valid;
            s_current_temp = temp_readings.temperature_c;

            /* Read flow meter data */
            flow_meter_get_flow_rate(&s_flow_meter, &s_current_flow);
            flow_meter_get_total_volume_ml(&s_flow_meter, &s_current_total_vol);

            s_current_needs_water = (s_current_sm < SOIL_MOISTURE_WATER_THRESHOLD) ? 1 : 0;

            ESP_LOGI(TAG, "ZONE: SM=%u TEMP=%.1f", s_current_sm, 
                     s_current_temp_valid ? s_current_temp : -99.9f);
        } else {
            ESP_LOGE(TAG, "Sensor read failed: %s (will reuse last known values)", esp_err_to_name(ret));
        }

        /* Read delay (sensor task keeps running, TX task runs independently every 2s) */
        vTaskDelay(pdMS_TO_TICKS(SOIL_READ_DELAY_MS));
    }
}