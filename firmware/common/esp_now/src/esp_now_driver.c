#include "esp_now_driver.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "ESP_NOW_DRIVER";

const uint8_t ESP_NOW_BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/* ---- Send acknowledgement plumbing (for EspNow_SendReliable) ---- */
static EspNow_SendCb_t        s_user_send_cb = NULL;  /**< Optional app send cb */
static SemaphoreHandle_t      s_send_done    = NULL;  /**< Posted by send cb    */
static volatile esp_now_send_status_t s_last_send_status = ESP_NOW_SEND_FAIL;

/**
 * @brief Internal ESP-NOW send callback (IDF 5.x signature).
 *
 * In ESP-IDF 5.4+ the send callback takes `const wifi_tx_info_t *` instead of a
 * raw MAC pointer. This adapter extracts the destination MAC (des_addr) and
 * keeps the application-facing EspNow_SendCb_t (mac_addr, status) signature
 * stable — mirroring the receive adapter.
 *
 * Records the delivery status, unblocks any EspNow_SendReliable() waiter, then
 * forwards to the application-registered callback (if any). Runs in the Wi-Fi
 * task context (not an ISR), so the regular FreeRTOS give is correct here.
 */
static void esp_now_send_adapter(const wifi_tx_info_t *tx_info, esp_now_send_status_t status)
{
    const uint8_t *mac_addr = (tx_info != NULL) ? tx_info->des_addr : NULL;

    s_last_send_status = status;
    if (s_send_done != NULL) {
        (void)xSemaphoreGive(s_send_done);
    }
    if (s_user_send_cb != NULL) {
        s_user_send_cb(mac_addr, status);
    }
}

/* ========================================================================== */
/*                         INTERNAL RECV ADAPTER                              */
/* ========================================================================== */

/*
 * In ESP-IDF 5.x the ESP-NOW receive callback signature changed from:
 *   void cb(const uint8_t *mac_addr, const uint8_t *data, int data_len)
 * to:
 *   void cb(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len)
 *
 * To keep the application-facing API stable (old 3-arg signature) we store the
 * user callback pointer and wrap it in an internal adapter that has the correct
 * IDF 5.x signature, extracting src_addr before forwarding.
 */
static EspNow_RecvCb_t s_recv_cb = NULL;

static void esp_now_recv_adapter(const esp_now_recv_info_t *esp_now_info,
                                 const uint8_t *data, int data_len)
{
    if (s_recv_cb != NULL && esp_now_info != NULL) {
        s_recv_cb(esp_now_info->src_addr, data, data_len);
    }
}

/* ========================================================================== */
/*                             DRIVER INTERFACE                               */
/* ========================================================================== */

esp_err_t EspNow_Init(uint8_t channel) {
    esp_err_t err;

    /*
     * NVS is NOT initialised here.  The caller (app_main) must call
     * nvs_flash_init() before EspNow_Init().  The previous code called
     * nvs_flash_erase() on NVS_NO_FREE_PAGES, which wiped ALL namespaces
     * — including sensor calibration data — and was the root cause of
     * persistent calibration loss.
     */

    /* The caller might have already initialized netif and event_loop, so ignore INVALID_STATE */
    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
        return err;
    }

    /*
     * Create the default STA netif BEFORE esp_wifi_init().
     * This is required so that when WiFi connects to an AP the DHCP client
     * handler exists. If created after esp_wifi_init(), the Wi-Fi driver
     * won't attach properly.
     */
    esp_netif_create_default_wifi_sta();

    /* Configure Wi-Fi in Station Mode (needed for ESP-NOW) */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) return err;

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) return err;

    err = esp_wifi_start();
    if (err != ESP_OK) return err;

    /* Set WiFi channel if explicitly configured (> 0). */
    if (channel > 0) {
        err = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
        if (err != ESP_OK) return err;
    }

    /* Initialize ESP-NOW */
    err = esp_now_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW initialization failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Register the internal adapter as the real ESP-NOW receive callback.
     * This ensures we always use the correct IDF 5.x signature. */
    err = esp_now_register_recv_cb(esp_now_recv_adapter);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_register_recv_cb failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Create the send-done semaphore and register our internal send callback so
     * EspNow_SendReliable() can wait for link-layer acknowledgement. */
    if (s_send_done == NULL) {
        s_send_done = xSemaphoreCreateBinary();
        if (s_send_done == NULL) {
            ESP_LOGE(TAG, "Failed to create send-done semaphore");
            return ESP_ERR_NO_MEM;
        }
    }
    err = esp_now_register_send_cb(esp_now_send_adapter);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_register_send_cb failed: %s", esp_err_to_name(err));
        return err;
    }

    /* By default, add the broadcast peer */
    EspNow_AddPeer(ESP_NOW_BROADCAST_MAC);

    /* Log MAC address */
    uint8_t mac_addr[6] = {0};
    esp_read_mac(mac_addr, ESP_MAC_WIFI_STA);

    if (channel > 0) {
        ESP_LOGI(TAG, "ESP-NOW Initialized Successfully (Channel %d).", channel);
    } else {
        ESP_LOGI(TAG, "ESP-NOW Initialized Successfully (Channel: auto).");
    }

    ESP_LOGI(TAG, "My MAC Address: %02X:%02X:%02X:%02X:%02X:%02X",
             mac_addr[0], mac_addr[1], mac_addr[2],
             mac_addr[3], mac_addr[4], mac_addr[5]);

    return ESP_OK;
}

esp_err_t EspNow_Deinit(void) {
    s_recv_cb = NULL;
    esp_now_deinit();
    esp_wifi_stop();
    esp_wifi_deinit();
    return ESP_OK;
}

esp_err_t EspNow_AddPeer(const uint8_t *mac_addr) {
    if (esp_now_is_peer_exist(mac_addr)) {
        return ESP_OK; /* Peer already exists */
    }

    esp_now_peer_info_t peer_info = {0};
    memcpy(peer_info.peer_addr, mac_addr, 6);
    peer_info.channel = 0; /* Use current channel */
    peer_info.encrypt = false;
    peer_info.ifidx = WIFI_IF_STA;

    esp_err_t ret = esp_now_add_peer(&peer_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add peer: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t EspNow_RemovePeer(const uint8_t *mac_addr) {
    if (!esp_now_is_peer_exist(mac_addr)) {
        return ESP_OK;
    }
    return esp_now_del_peer(mac_addr);
}

/**
 * @brief Register a user receive callback.
 *
 * The callback uses the classic 3-argument signature:
 *   void cb(const uint8_t *mac_addr, const uint8_t *data, int data_len)
 *
 * Internally, the adapter registered during EspNow_Init() forwards calls
 * to this stored pointer using the correct IDF 5.x recv_info struct.
 */
esp_err_t EspNow_RegisterRecvCb(EspNow_RecvCb_t cb) {
    s_recv_cb = cb;
    return ESP_OK;
}

esp_err_t EspNow_RegisterSendCb(EspNow_SendCb_t cb) {
    /* Store the app callback; the internal send cb (registered in EspNow_Init)
     * forwards to it after handling delivery acknowledgement. */
    s_user_send_cb = cb;
    return ESP_OK;
}

esp_err_t EspNow_Send(const uint8_t *mac_addr, const uint8_t *data, size_t data_len) {
    if (mac_addr == NULL) {
        mac_addr = ESP_NOW_BROADCAST_MAC;
    }
    return esp_now_send(mac_addr, data, data_len);
}

esp_err_t EspNow_SendReliable(const uint8_t *mac_addr, const uint8_t *data,
                              size_t data_len, uint8_t max_attempts) {
    if (mac_addr == NULL || data == NULL || data_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (max_attempts == 0) {
        max_attempts = 1;
    }
    if (s_send_done == NULL) {
        /* Driver not initialised — fall back to fire-and-forget. */
        return esp_now_send(mac_addr, data, data_len);
    }

    esp_err_t last_err = ESP_FAIL;

    for (uint8_t attempt = 0; attempt < max_attempts; attempt++) {
        /* Clear any stale completion before sending. */
        (void)xSemaphoreTake(s_send_done, 0);

        last_err = esp_now_send(mac_addr, data, data_len);
        if (last_err != ESP_OK) {
            ESP_LOGW(TAG, "send attempt %u/%u enqueue failed: %s",
                     attempt + 1, max_attempts, esp_err_to_name(last_err));
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        /* Wait up to 100 ms for the link-layer ACK callback. */
        if (xSemaphoreTake(s_send_done, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (s_last_send_status == ESP_NOW_SEND_SUCCESS) {
                return ESP_OK;
            }
            ESP_LOGW(TAG, "send attempt %u/%u not acked", attempt + 1, max_attempts);
        } else {
            ESP_LOGW(TAG, "send attempt %u/%u timed out", attempt + 1, max_attempts);
            last_err = ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    return (last_err == ESP_OK) ? ESP_FAIL : last_err;
}
