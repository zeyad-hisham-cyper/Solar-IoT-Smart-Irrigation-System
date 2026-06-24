/**
 * @file firebase_auth.h
 * @brief Firebase Email/Password Authentication module.
 *
 * Authenticates with Firebase Identity Toolkit and stores the idToken
 * for use in subsequent Realtime Database requests.
 */

#ifndef FIREBASE_AUTH_H
#define FIREBASE_AUTH_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sign in to Firebase using email and password.
 *
 * Sends a POST request to the Identity Toolkit endpoint, parses the
 * response, and stores the idToken internally.
 *
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t firebase_auth_login(void);

/**
 * @brief Get a pointer to the current ID token string.
 *
 * @return Pointer to the null-terminated token, or NULL if not logged in.
 */
const char *firebase_auth_get_token(void);

/**
 * @brief Check if we currently have a valid token stored.
 *
 * @return true if logged in (token is non-empty), false otherwise.
 */
bool firebase_auth_is_logged_in(void);

/**
 * @brief Get the user's UID parsed from the login response.
 * @return String pointer containing UID or NULL.
 */
const char *firebase_auth_get_uid(void);

/**
 * @brief Get status of the last auth attempt.
 */
int firebase_auth_get_last_status(void);
esp_err_t firebase_auth_get_last_esp_err(void);
const char *firebase_auth_get_last_error_desc(void);

#ifdef __cplusplus
}
#endif

#endif /* FIREBASE_AUTH_H */
