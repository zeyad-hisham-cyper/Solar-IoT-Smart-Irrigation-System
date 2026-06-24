/**
 * @file firebase_db.h
 * @brief Firebase Realtime Database — generic PUT/GET helpers.
 */

#ifndef FIREBASE_DB_H
#define FIREBASE_DB_H

#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief PUT a JSON string to the specified database path.
 *
 * @param path          Database path, e.g. "gateways/g1"
 * @param json_string   Serialised JSON payload to write
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_STATE if the server returned 401 (token expired),
 *         ESP_FAIL on other errors
 */
esp_err_t firebase_db_put(const char *path, const char *json_string);

/**
 * @brief POST a JSON string to the specified database path.
 *
 * Unlike PUT (which overwrites the node), POST appends a new child under
 * @p path with a server-generated push key. Used for append-only data such
 * as history records (e.g. "history/z1").
 *
 * @param path          Database path to push under, e.g. "history/z1"
 * @param json_string   Serialised JSON payload to write
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_STATE if the server returned 401 (token expired),
 *         ESP_FAIL on other errors
 */
esp_err_t firebase_db_post(const char *path, const char *json_string);

/**
 * @brief GET JSON data from the specified database path.
 *
 * @param path       Database path, e.g. "commands/g1"
 * @param out_buf    Buffer to receive the JSON response body
 * @param buf_len    Size of out_buf
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_STATE if 401 (token expired),
 *         ESP_FAIL on other errors
 */
esp_err_t firebase_db_get(const char *path, char *out_buf, size_t buf_len);

/**
 * @brief Get diagnostic details about the last database request.
 */
int firebase_db_get_last_status(void);
esp_err_t firebase_db_get_last_esp_err(void);
const char *firebase_db_get_last_error_desc(void);
const char *firebase_db_get_last_path(void);

#ifdef __cplusplus
}
#endif

#endif /* FIREBASE_DB_H */
