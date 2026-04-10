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

static uint8_t s_zone_id = 0; /**< 0 means "not yet assigned" */

static esp_err_t zone_nvs_save_id(uint8_t id)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(ZONE_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (ret != ESP_OK) return ret;
    ret = nvs_set_u8(h, ZONE_NVS_KEY_ID, id);
    if (ret == ESP_OK) ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}

static uint8_t zone_nvs_load_id(void)
{
    nvs_handle_t h;
    if (nvs_open(ZONE_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return 0;
    uint8_t id = 0;
    nvs_get_u8(h, ZONE_NVS_KEY_ID, &id);
    nvs_close(h);
    return id;
}

/* ========================================================================== */
/*                         DISCOVERY / ASSIGNMENT                             */
/* ========================================================================== */

/* Event bits */
#define EVT_DISC_REQ_RECEIVED  BIT0
#define EVT_ZONE_ASSIGNED      BIT1

static EventGroupHandle_t  s_discovery_eg = NULL;
static uint8_t             s_control_mac[6];   /**< MAC of the control board */
static uint8_t             s_assigned_id  = 0; /**< ID received from control */

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

static void OnEspNowRecvCb(const uint8_t *mac_addr, const uint8_t *data, int data_len)
{
    if (data == NULL || data_len < (int)sizeof(EspNow_MessageHeader_t)) return;

    const EspNow_MessageHeader_t *header = (const EspNow_MessageHeader_t *)data;

    if (header->version != ESP_NOW_PROTOCOL_VERSION) return;

    switch (header->msg_type) {

    case ESP_NOW_MSG_TYPE_DISCOVERY_REQ:
        /* Control is broadcasting; save its MAC and signal the discovery task */
        memcpy(s_control_mac, mac_addr, 6);
        ESP_LOGI(TAG, "DISCOVERY_REQ from control %02X:%02X:%02X:%02X:%02X:%02X",
                 mac_addr[0], mac_addr[1], mac_addr[2],
                 mac_addr[3], mac_addr[4], mac_addr[5]);
        xEventGroupSetBits(s_discovery_eg, EVT_DISC_REQ_RECEIVED);
        break;

    case ESP_NOW_MSG_TYPE_ZONE_ASSIGN:
        if (data_len >= (int)sizeof(EspNow_ZoneAssignPayload_t)) {
            const EspNow_ZoneAssignPayload_t *pkt = (const EspNow_ZoneAssignPayload_t *)data;
            s_assigned_id = pkt->assigned_zone_id;
            ESP_LOGI(TAG, "ZONE_ASSIGN received: Zone ID = %u", s_assigned_id);
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
    ESP_LOGI(TAG, "No stored Zone ID — waiting for DISCOVERY_REQ from control...");

    while (1) {
        /* Wait for a DISCOVERY_REQ (no timeout: we cannot operate without a zone ID) */
        xEventGroupWaitBits(s_discovery_eg, EVT_DISC_REQ_RECEIVED, pdTRUE, pdTRUE, portMAX_DELAY);

        /* Build and send DISCOVERY_RESP with obfuscated local MAC */
        uint8_t my_mac[6];
        esp_read_mac(my_mac, ESP_MAC_WIFI_STA);

        EspNow_DiscoveryRespPayload_t resp = {0};
        resp.header.version   = ESP_NOW_PROTOCOL_VERSION;
        resp.header.msg_type  = ESP_NOW_MSG_TYPE_DISCOVERY_RESP;
        resp.header.sender_id = 0; /* ID not yet known */
        memcpy(resp.obfuscated_mac, my_mac, 6);
        EspNow_ObfuscateMAC(resp.obfuscated_mac); /* XOR obfuscation */

        /* Add control as a peer so we can unicast back */
        EspNow_AddPeer(s_control_mac);

        ESP_LOGI(TAG, "Sending DISCOVERY_RESP to control...");
        EspNow_Send(s_control_mac, (const uint8_t *)&resp, sizeof(resp));

        /* Wait for ZONE_ASSIGN (10 s timeout – then retry entire handshake) */
        EventBits_t bits = xEventGroupWaitBits(s_discovery_eg, EVT_ZONE_ASSIGNED,
                                               pdTRUE, pdTRUE,
                                               pdMS_TO_TICKS(10000));
        if (bits & EVT_ZONE_ASSIGNED) {
            s_zone_id = s_assigned_id;
            esp_err_t err = zone_nvs_save_id(s_zone_id);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Zone ID %u saved to NVS.", s_zone_id);
            } else {
                ESP_LOGW(TAG, "Zone ID assigned but NVS save failed: %s", esp_err_to_name(err));
            }
            return; /* Success – exit the loop */
        }

        ESP_LOGW(TAG, "No ZONE_ASSIGN received in time. Retrying...");
        xEventGroupClearBits(s_discovery_eg, EVT_DISC_REQ_RECEIVED | EVT_ZONE_ASSIGNED);
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
    gpio_install_isr_service(0);
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

    /* --- Zone ID: load from NVS or perform discovery handshake --- */
    s_zone_id = zone_nvs_load_id();
    if (s_zone_id != 0) {
        ESP_LOGI(TAG, "Loaded Zone ID %u from NVS. Skipping discovery.", s_zone_id);
    } else {
        zone_perform_discovery();
    }

    ESP_LOGI(TAG, "Zone ID confirmed: %u. Entering sensor reading loop...", s_zone_id);

    /* --- Sensor Reading Loop --- */
    Soil_Moisture_Readings_t soil_readings;
    Temp_DS18B20_Readings_t  temp_readings;

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
            const char *state_str = "UNKNOWN";
            switch (soil_readings.state) {
                case SOIL_STATE_DRY:    state_str = "DRY";    break;
                case SOIL_STATE_NORMAL: state_str = "NORMAL"; break;
                case SOIL_STATE_WET:    state_str = "WET";    break;
            }

            char temp_str[16] = "--.-";
            if (temp_readings.valid) {
                snprintf(temp_str, sizeof(temp_str), "%.1f", temp_readings.temperature_c);
            }

            /* Read flow meter data */
            float flow_rate = 0.0f;
            uint32_t total_volume = 0;
            flow_meter_get_flow_rate(&s_flow_meter, &flow_rate);
            flow_meter_get_total_volume_ml(&s_flow_meter, &total_volume);

            uint8_t needs_water = (soil_readings.moisture_percentage < SOIL_MOISTURE_WATER_THRESHOLD) ? 1 : 0;

            ESP_LOGI(TAG, "[Zone %u] Moisture: %3u%% | Temp: %s C | State: %s | Needs Water: %d",
                     s_zone_id,
                     soil_readings.moisture_percentage,
                     temp_str,
                     state_str,
                     needs_water);
            ESP_LOGI(TAG, "          Flow Rate: %.2f L/min | Total Vol: %lu mL | Valve State: %d",
                     flow_rate, total_volume, s_valve.state);

            /* Build and send ZONE_DATA packet */
            EspNow_ZoneDataPayload_t pkt = {0};
            pkt.header.version   = ESP_NOW_PROTOCOL_VERSION;
            pkt.header.msg_type  = ESP_NOW_MSG_TYPE_ZONE_DATA;
            pkt.header.sender_id = s_zone_id;

            pkt.moisture_percentage = soil_readings.moisture_percentage;
            pkt.temperature_c_x10   = temp_readings.valid
                                      ? (int16_t)(temp_readings.temperature_c * 10.0f)
                                      : -999;
            pkt.needs_water = needs_water;
            pkt.total_volume_ml = total_volume;

            esp_err_t send_err = EspNow_Send(ESP_NOW_BROADCAST_MAC,
                                             (const uint8_t *)&pkt, sizeof(pkt));
            if (send_err != ESP_OK) {
                ESP_LOGE(TAG, "Send failed: %s", esp_err_to_name(send_err));
            }
        } else {
            ESP_LOGE(TAG, "Sensor read failed: %s", esp_err_to_name(ret));
        }

        vTaskDelay(pdMS_TO_TICKS(SENSOR_READ_INTERVAL_MS));
    }
}