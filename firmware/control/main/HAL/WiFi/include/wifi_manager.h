/**
 * @file wifi_manager.h
 * @brief WiFi Station Manager — connects the ESP32 to an Access Point.
 *
 * Designed to work alongside ESP-NOW which already initialises WiFi in STA mode.
 * This module only configures the STA credentials and calls esp_wifi_connect().
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configure STA credentials and initiate connection to the AP.
 *
 * Must be called AFTER EspNow_Init() (which starts WiFi in STA mode).
 * This function sets the SSID/password and calls esp_wifi_connect().
 *
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Block until an IP address is obtained or a timeout occurs.
 *
 * @param timeout_ms  Maximum time to wait in milliseconds (0 = forever).
 * @return ESP_OK if connected, ESP_ERR_TIMEOUT on timeout
 */
esp_err_t wifi_manager_wait_connected(uint32_t timeout_ms);

/**
 * @brief Check if WiFi is currently connected.
 * @return true if connected with an IP, false otherwise.
 */
bool wifi_manager_is_connected(void);

/**
 * @brief Get the current IP address of the STA interface.
 * @param[out] ip_out  Pointer to store the IP address.
 * @return ESP_OK on success.
 */
esp_err_t wifi_manager_get_ip(esp_ip4_addr_t *ip_out);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_MANAGER_H */
