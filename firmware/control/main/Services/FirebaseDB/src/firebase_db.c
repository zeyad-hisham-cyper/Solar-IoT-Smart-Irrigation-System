/**
 * @file firebase_db.c
 * @brief Firebase Realtime Database PUT/GET implementation using esp_http_client.
 */

#include "firebase_db.h"
#include "firebase_config.h"
#include "firebase_auth.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

static const char *TAG = "FB_DB";

/* ========================================================================== */
/*                      RESPONSE ACCUMULATION                                 */
/* ========================================================================== */

/**
 * @brief Context passed through the HTTP event handler user_data pointer
 *        so that multiple concurrent requests don't collide.
 */
typedef struct {
    char   *buf;       /**< Destination buffer */
    size_t  buf_len;   /**< Total capacity */
    size_t  received;  /**< Bytes written so far */
} http_response_ctx_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_response_ctx_t *ctx = (http_response_ctx_t *)evt->user_data;
    if (ctx == NULL) return ESP_OK;

    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (!esp_http_client_is_chunked_response(evt->client)) {
            if (ctx->received + evt->data_len < ctx->buf_len - 1) {
                memcpy(ctx->buf + ctx->received, evt->data, evt->data_len);
                ctx->received += evt->data_len;
                ctx->buf[ctx->received] = '\0';
            } else {
                ESP_LOGW(TAG, "Response truncated (buf=%d, got=%d)",
                         (int)ctx->buf_len, (int)(ctx->received + evt->data_len));
            }
        } else {
            /* Chunked response — still accumulate */
            if (ctx->received + evt->data_len < ctx->buf_len - 1) {
                memcpy(ctx->buf + ctx->received, evt->data, evt->data_len);
                ctx->received += evt->data_len;
                ctx->buf[ctx->received] = '\0';
            }
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

/* ========================================================================== */
/*                         URL BUILDER                                        */
/* ========================================================================== */

/**
 * @brief Build the full Firebase REST URL including the auth token.
 *
 * Uses a heap-allocated buffer to avoid large stack usage.
 *
 * Example output:
 *   https://<db>.firebasedatabase.app/<path>.json?auth=<idToken>
 *
 * @param[in]  path     Database path (e.g. "gateways/g1")
 * @param[out] url_out  Will be set to point to a heap-allocated URL string.
 *                      Caller MUST free() this pointer.
 * @return ESP_OK on success, error otherwise.
 */
static esp_err_t build_url(const char *path, char **url_out)
{
    const char *token = firebase_auth_get_token();
    if (token == NULL) {
        ESP_LOGE(TAG, "No auth token — cannot build URL");
        return ESP_ERR_INVALID_STATE;
    }

    /* Calculate required size:
     *   base_url + "/" + path + ".json?auth=" + token + '\0' */
    size_t needed = strlen(FIREBASE_DB_URL) + 1 + strlen(path) +
                    strlen(".json?auth=") + strlen(token) + 1;

    char *url = (char *)malloc(needed);
    if (url == NULL) {
        ESP_LOGE(TAG, "Failed to allocate URL buffer (%d bytes)", (int)needed);
        return ESP_ERR_NO_MEM;
    }

    int written = snprintf(url, needed, "%s/%s.json?auth=%s",
                           FIREBASE_DB_URL, path, token);
    if (written < 0 || (size_t)written >= needed) {
        ESP_LOGE(TAG, "URL formatting error");
        free(url);
        return ESP_ERR_NO_MEM;
    }

    *url_out = url;
    return ESP_OK;
}

/* ========================================================================== */
/*                            PUBLIC API                                      */
/* ========================================================================== */

esp_err_t firebase_db_put(const char *path, const char *json_string)
{
    char *url = NULL;
    esp_err_t err = build_url(path, &url);
    if (err != ESP_OK) return err;

    /* We don't need to collect the response body for PUT, but we set up a
       small buffer so the event handler doesn't crash if data arrives. */
    char resp_buf[256] = {0};
    http_response_ctx_t ctx = { .buf = resp_buf, .buf_len = sizeof(resp_buf), .received = 0 };

    esp_http_client_config_t config = {
        .url               = url,
        .method            = HTTP_METHOD_PUT,
        .event_handler     = http_event_handler,
        .user_data         = &ctx,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms        = 10000,
        .buffer_size       = 2048,    /* HTTP receive buffer */
        .buffer_size_tx    = 2048,    /* HTTP transmit buffer (headers + URL) */
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "PUT: Failed to init HTTP client");
        free(url);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_string, strlen(json_string));

    err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(url);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PUT %s failed: %s", path, esp_err_to_name(err));
        return ESP_FAIL;
    }

    if (status == 401) {
        ESP_LOGW(TAG, "PUT %s returned 401 — token expired", path);
        return ESP_ERR_INVALID_STATE;
    }

    if (status != 200) {
        ESP_LOGE(TAG, "PUT %s returned HTTP %d — resp: %.128s", path, status, resp_buf);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "PUT %s OK (HTTP %d)", path, status);
    return ESP_OK;
}

esp_err_t firebase_db_get(const char *path, char *out_buf, size_t buf_len)
{
    if (out_buf == NULL || buf_len == 0) return ESP_ERR_INVALID_ARG;

    char *url = NULL;
    esp_err_t err = build_url(path, &url);
    if (err != ESP_OK) return err;

    out_buf[0] = '\0';
    http_response_ctx_t ctx = { .buf = out_buf, .buf_len = buf_len, .received = 0 };

    esp_http_client_config_t config = {
        .url               = url,
        .method            = HTTP_METHOD_GET,
        .event_handler     = http_event_handler,
        .user_data         = &ctx,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms        = 10000,
        .buffer_size       = 2048,    /* HTTP receive buffer */
        .buffer_size_tx    = 2048,    /* HTTP transmit buffer (headers + URL) */
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "GET: Failed to init HTTP client");
        free(url);
        return ESP_FAIL;
    }

    err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(url);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GET %s failed: %s", path, esp_err_to_name(err));
        return ESP_FAIL;
    }

    if (status == 401) {
        ESP_LOGW(TAG, "GET %s returned 401 — token expired", path);
        return ESP_ERR_INVALID_STATE;
    }

    if (status != 200) {
        ESP_LOGE(TAG, "GET %s returned HTTP %d", path, status);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "GET %s OK (%d bytes)", path, (int)ctx.received);
    return ESP_OK;
}
