#include "esp_now_driver.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_mac.h"
#include <string.h>

static const char *TAG = "ESP_NOW_DRIVER";

const uint8_t ESP_NOW_BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

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

esp_err_t EspNow_Init(void) {
    esp_err_t err;

    /* Initialize NVS (usually required by Wi-Fi Driver) */
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

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

    /* Set fixed channel (Channel 1) for both modules */
    err = esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) return err;

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

    /* By default, add the broadcast peer */
    EspNow_AddPeer(ESP_NOW_BROADCAST_MAC);

    /* Log MAC address */
    uint8_t mac_addr[6] = {0};
    esp_read_mac(mac_addr, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "ESP-NOW Initialized Successfully (Channel 1).");
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
    return esp_now_register_send_cb((esp_now_send_cb_t)cb);
}

esp_err_t EspNow_Send(const uint8_t *mac_addr, const uint8_t *data, size_t data_len) {
    if (mac_addr == NULL) {
        mac_addr = ESP_NOW_BROADCAST_MAC;
    }
    return esp_now_send(mac_addr, data, data_len);
}
