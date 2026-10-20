/**
 * @file secrets.example.h
 * @brief Credentials template for the gateway firmware.
 *
 * Copy this file to secrets.h (same folder) and fill in your own values.
 * secrets.h is git-ignored so your credentials never get committed.
 */
#ifndef SECRETS_H
#define SECRETS_H

/* Wi-Fi */
#define WIFI_SSID                   "your-wifi-ssid"
#define WIFI_PASS                   "your-wifi-password"

/* OpenWeather (https://openweathermap.org/api) */
#define WEATHER_API_KEY             "your-openweather-api-key"

/* Firebase (Console -> Project Settings / Authentication) */
#define FIREBASE_API_KEY            "your-firebase-web-api-key"
#define FIREBASE_AUTH_EMAIL         "device@example.com"
#define FIREBASE_AUTH_PASSWORD      "your-device-password"
#define FIREBASE_DB_URL             "https://your-project-default-rtdb.region.firebasedatabase.app"

#endif /* SECRETS_H */
