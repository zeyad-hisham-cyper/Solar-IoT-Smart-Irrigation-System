/**
 * @file firebase_config.h
 * @brief Firebase configuration constants for the Smart Irrigation System.
 *
 * Centralises all Firebase-related settings so they are easy to find and change.
 */

#ifndef FIREBASE_CONFIG_H
#define FIREBASE_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                        FIREBASE AUTHENTICATION                             */
/* ========================================================================== */

/** Firebase Web API Key (from Firebase Console → Project Settings) */
#define FIREBASE_API_KEY            "your-firebase-web-api-key"

/** Firebase Identity Toolkit sign-in endpoint */
#define FIREBASE_AUTH_URL           "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=" FIREBASE_API_KEY

/** Login credentials */
#define FIREBASE_AUTH_EMAIL         "device@example.com"
#define FIREBASE_AUTH_PASSWORD      "your-device-password"

/** Maximum lengths for token storage */
#define FIREBASE_ID_TOKEN_MAX_LEN   1200
#define FIREBASE_REFRESH_TOKEN_LEN  512

/* ========================================================================== */
/*                        FIREBASE REALTIME DATABASE                          */
/* ========================================================================== */

/** Base URL of the Realtime Database (no trailing slash) */
#define FIREBASE_DB_URL             "https://smart-irrigation-system-e415d-default-rtdb.europe-west1.firebasedatabase.app"

/* ========================================================================== */
/*                             SYSTEM IDS                                     */
/* ========================================================================== */

/** Gateway identifier used in database paths */
#define GATEWAY_ID                  "g1"

/** Zone identifiers — must match Firebase schema */
#define ZONE_ID_1                   "z1"
#define ZONE_ID_2                   "z2"
#define NUM_ZONES                   2

/* ========================================================================== */
/*                           TIMING (ms)                                      */
/* ========================================================================== */

/** Interval between gateway + zone data uploads */
#define FIREBASE_UPLOAD_INTERVAL_MS     2000

/** Interval between command polls */
#define FIREBASE_POLL_INTERVAL_MS       2000

/* ========================================================================== */
/*                          HTTP BUFFER SIZES                                 */
/* ========================================================================== */

/** Maximum URL length (base URL + path + token) */
#define FIREBASE_URL_MAX_LEN            1536

/** Maximum HTTP response body we will accept */
#define FIREBASE_RESPONSE_MAX_LEN       1024

#ifdef __cplusplus
}
#endif

#endif /* FIREBASE_CONFIG_H */
