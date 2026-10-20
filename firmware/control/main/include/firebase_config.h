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

/** FIREBASE_API_KEY, FIREBASE_AUTH_EMAIL, FIREBASE_AUTH_PASSWORD and
 *  FIREBASE_DB_URL come from secrets.h (template: secrets.example.h). */
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Missing secrets.h - copy secrets.example.h to secrets.h and fill in your credentials"
#endif

/** Firebase Identity Toolkit sign-in endpoint */
#define FIREBASE_AUTH_URL           "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=" FIREBASE_API_KEY

/** Maximum lengths for token storage */
#define FIREBASE_ID_TOKEN_MAX_LEN   1200
#define FIREBASE_REFRESH_TOKEN_LEN  512

/* ========================================================================== */
/*                        FIREBASE REALTIME DATABASE                          */
/* ========================================================================== */

/** Base URL of the Realtime Database (FIREBASE_DB_URL, no trailing slash) comes from secrets.h */

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

/** Minimum interval between periodic history records per zone.
 *  History is also written immediately whenever a zone's valve changes. */
#define FIREBASE_HISTORY_INTERVAL_MS    60000

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
