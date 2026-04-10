/**
 * @file wifi_manager.c
 * @brief WiFi Station Manager implementation.
 *
 * Registers event handlers for WiFi and IP events, configures the STA
 * interface with the user's SSID and password, and provides a blocking
 * wait function until the ESP32 obtains an IP address.
 */

#include "wifi_manager.h"
#include "app_config.h"

#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "WIFI_MGR";

/* Event bits */
#define WIFI_CONNECTED_BIT   BIT0
#define WIFI_FAIL_BIT        BIT1

static EventGroupHandle_t s_wifi_event_group = NULL;
static int s_retry_count = 0;

#define WIFI_MAX_RETRY  10

/* ========================================================================== */
/*                         EVENT HANDLERS                                     */
/* ========================================================================== */

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_count < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_count++;
            ESP_LOGI(TAG, "Retrying WiFi connection (%d/%d)...", s_retry_count, WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "WiFi connection failed after %d retries.", WIFI_MAX_RETRY);
        }
    }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* ========================================================================== */
/*                          PUBLIC API                                        */
/* ========================================================================== */

esp_err_t wifi_manager_init(void)
{
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }

    /*
     * NOTE: esp_netif_create_default_wifi_sta() is called inside
     * EspNow_Init() (before esp_wifi_start) so we don't create it here.
     */

    /* Register event handlers */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL, NULL));

    /*
     * WiFi is already initialised and started by EspNow_Init().
     * IMPORTANT: Do NOT call esp_wifi_stop() here — that would destroy
     * the ESP-NOW layer which was initialised during EspNow_Init().
     * esp_wifi_set_config() can be called while WiFi is running.
     */
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    /* Copy SSID and password safely */
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_connect());

    ESP_LOGI(TAG, "WiFi STA configured. Connecting to \"%s\"...", WIFI_SSID);
    return ESP_OK;
}

esp_err_t wifi_manager_wait_connected(uint32_t timeout_ms)
{
    TickType_t ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE,
        ticks);

    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    }
    if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "WiFi connection failed.");
        return ESP_FAIL;
    }

    ESP_LOGE(TAG, "WiFi connection timed out.");
    return ESP_ERR_TIMEOUT;
}
