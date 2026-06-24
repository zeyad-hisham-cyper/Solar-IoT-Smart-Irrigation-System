/**
 * @file firebase_auth.c
 * @brief Firebase Email/Password Authentication implementation.
 *
 * Uses esp_http_client to POST to the Firebase Identity Toolkit endpoint,
 * parses the JSON response with cJSON, and stores the idToken.
 */

#include "firebase_auth.h"
#include "firebase_config.h"

#include <string.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

static const char *TAG = "FB_AUTH";

/* ========================================================================== */
/*                          TOKEN STORAGE                                     */
/* ========================================================================== */

static char s_id_token[FIREBASE_ID_TOKEN_MAX_LEN]       = {0};
static char s_refresh_token[FIREBASE_REFRESH_TOKEN_LEN]  = {0};
static char s_uid[128] = {0};

/* Diagnostics storage */
static int s_last_auth_http_status = 0;
static esp_err_t s_last_auth_esp_err = ESP_OK;
static char s_last_auth_error_desc[256] = {0};

/* ========================================================================== */
/*                      HTTP RESPONSE BUFFER                                  */
/* ========================================================================== */

/** Buffer that collects the HTTP response body during the request. */
static char s_response_buf[2048];
static int  s_response_len = 0;

/**
 * @brief HTTP event handler — accumulates the response body.
 */
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (s_response_len + evt->data_len < (int)sizeof(s_response_buf) - 1) {
            memcpy(s_response_buf + s_response_len, evt->data, evt->data_len);
            s_response_len += evt->data_len;
            s_response_buf[s_response_len] = '\0';
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

/* ========================================================================== */
/*                           PUBLIC API                                       */
/* ========================================================================== */

esp_err_t firebase_auth_login(void)
{
    ESP_LOGI(TAG, "Authenticating with Firebase...");

    /* Clear last login diagnostics */
    s_last_auth_http_status = 0;
    s_last_auth_esp_err = ESP_OK;
    s_last_auth_error_desc[0] = '\0';

    /* Build the POST body */
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        s_last_auth_esp_err = ESP_ERR_NO_MEM;
        snprintf(s_last_auth_error_desc, sizeof(s_last_auth_error_desc), "Failed to create JSON object");
        return ESP_FAIL;
    }
    cJSON_AddStringToObject(root, "email",             FIREBASE_AUTH_EMAIL);
    cJSON_AddStringToObject(root, "password",          FIREBASE_AUTH_PASSWORD);
    cJSON_AddBoolToObject  (root, "returnSecureToken", true);

    char *post_data = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (post_data == NULL) {
        ESP_LOGE(TAG, "Failed to serialise JSON");
        s_last_auth_esp_err = ESP_ERR_NO_MEM;
        snprintf(s_last_auth_error_desc, sizeof(s_last_auth_error_desc), "Failed to serialise JSON");
        return ESP_FAIL;
    }

    /* Reset response buffer */
    s_response_len = 0;
    memset(s_response_buf, 0, sizeof(s_response_buf));

    /* Configure HTTP client */
    esp_http_client_config_t config = {
        .url            = FIREBASE_AUTH_URL,
        .method         = HTTP_METHOD_POST,
        .event_handler  = http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms     = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        free(post_data);
        ESP_LOGE(TAG, "Failed to init HTTP client");
        s_last_auth_esp_err = ESP_ERR_NO_MEM;
        snprintf(s_last_auth_error_desc, sizeof(s_last_auth_error_desc), "Failed to init HTTP client");
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);

    esp_http_client_cleanup(client);
    free(post_data);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        s_last_auth_esp_err = err;
        snprintf(s_last_auth_error_desc, sizeof(s_last_auth_error_desc), "HTTP request failed: %s", esp_err_to_name(err));
        return ESP_FAIL;
    }

    s_last_auth_http_status = status_code;

    if (status_code != 200) {
        ESP_LOGE(TAG, "Auth failed with HTTP %d: %s", status_code, s_response_buf);
        /* Ensure we never overflow s_last_auth_error_desc when copying a large response */
        size_t avail = sizeof(s_last_auth_error_desc);
        /* Reserve room for the "HTTP <code>: " prefix (max 10 chars) */
        if (avail > 16) {
            /* Copy a truncated portion of s_response_buf */
            int n = snprintf(s_last_auth_error_desc, avail, "HTTP %d: ", status_code);
            if (n < 0) n = 0;
            size_t buf_space = (size_t)avail - (size_t)n;
            if (buf_space > 0) {
                strncat(s_last_auth_error_desc, s_response_buf, buf_space - 1);
            }
        } else {
            /* Fallback: simple snprintf */
            snprintf(s_last_auth_error_desc, sizeof(s_last_auth_error_desc), "HTTP %d", status_code);
        }
        return ESP_FAIL;
    }

    /* Parse the response */
    cJSON *resp = cJSON_Parse(s_response_buf);
    if (resp == NULL) {
        ESP_LOGE(TAG, "Failed to parse auth response JSON");
        s_last_auth_esp_err = ESP_ERR_INVALID_RESPONSE;
        snprintf(s_last_auth_error_desc, sizeof(s_last_auth_error_desc), "Failed to parse auth JSON response");
        return ESP_FAIL;
    }

    cJSON *id_token_json      = cJSON_GetObjectItem(resp, "idToken");
    cJSON *refresh_token_json = cJSON_GetObjectItem(resp, "refreshToken");
    cJSON *local_id_json      = cJSON_GetObjectItem(resp, "localId");

    if (!cJSON_IsString(id_token_json) || id_token_json->valuestring == NULL) {
        ESP_LOGE(TAG, "idToken not found in response");
        s_last_auth_esp_err = ESP_ERR_INVALID_RESPONSE;
        snprintf(s_last_auth_error_desc, sizeof(s_last_auth_error_desc), "idToken not found in JSON response");
        cJSON_Delete(resp);
        return ESP_FAIL;
    }

    /* Store tokens and UID */
    strncpy(s_id_token, id_token_json->valuestring, sizeof(s_id_token) - 1);
    s_id_token[sizeof(s_id_token) - 1] = '\0';

    if (cJSON_IsString(refresh_token_json) && refresh_token_json->valuestring != NULL) {
        strncpy(s_refresh_token, refresh_token_json->valuestring, sizeof(s_refresh_token) - 1);
        s_refresh_token[sizeof(s_refresh_token) - 1] = '\0';
    }

    if (cJSON_IsString(local_id_json) && local_id_json->valuestring != NULL) {
        strncpy(s_uid, local_id_json->valuestring, sizeof(s_uid) - 1);
        s_uid[sizeof(s_uid) - 1] = '\0';
    } else {
        s_uid[0] = '\0';
    }

    cJSON_Delete(resp);

    ESP_LOGI(TAG, "Firebase login successful (token length: %d)", (int)strlen(s_id_token));
    return ESP_OK;
}

const char *firebase_auth_get_token(void)
{
    if (s_id_token[0] == '\0') {
        return NULL;
    }
    return s_id_token;
}

bool firebase_auth_is_logged_in(void)
{
    return (s_id_token[0] != '\0');
}

const char *firebase_auth_get_uid(void)
{
    if (s_uid[0] == '\0') {
        return NULL;
    }
    return s_uid;
}

int firebase_auth_get_last_status(void)
{
    return s_last_auth_http_status;
}

esp_err_t firebase_auth_get_last_esp_err(void)
{
    return s_last_auth_esp_err;
}

const char *firebase_auth_get_last_error_desc(void)
{
    return s_last_auth_error_desc;
}
