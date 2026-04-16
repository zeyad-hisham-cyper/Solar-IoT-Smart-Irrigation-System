#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "driver/gpio.h"

/* ========================================================================== */
/*                          WIFI CONFIGURATION                                */
/* ========================================================================== */

/** WiFi Access Point credentials */
#define WIFI_SSID               "your-wifi-ssid"
#define WIFI_PASS               "your-wifi-password"

/* ========================================================================== */
/*                           MOTOR CONFIGURATION                              */
/* ========================================================================== */

#define MOTOR_PIN_IN_A          GPIO_NUM_23
#define MOTOR_PIN_IN_B          GPIO_NUM_22

/* ========================================================================== */
/*                      OPENWEATHER API CONFIGURATION                         */
/* ========================================================================== */

/** Geographic coordinates (MIU) */
#define WEATHER_API_LAT         "30.166734786337404"
#define WEATHER_API_LON         "31.49253916004326"

/** OpenWeather API key */
#define WEATHER_API_KEY         "your-openweather-api-key"

/** Full URL format string for the weather endpoint */
#define WEATHER_API_URL_FMT     "https://api.openweathermap.org/data/2.5/weather?lat=%s&lon=%s&appid=%s"

/** How often to fetch weather data (ms) - 3 hours */
#define WEATHER_FETCH_INTERVAL_MS       (3 * 60 * 60 * 1000)

/** HTTP request timeout for weather fetch (ms) */
#define WEATHER_HTTP_TIMEOUT_MS         10000

/** Default weather values when API is unreachable */
#define WEATHER_DEFAULT_RAIN            false
#define WEATHER_DEFAULT_TEMP            25.0f

/* ========================================================================== */
/*                     IRRIGATION ENGINE CONFIGURATION                        */
/* ========================================================================== */

/** Default moisture thresholds per zone (can be overridden from Firebase) */
#define DEFAULT_Z1_THRESHOLD            35
#define DEFAULT_Z2_THRESHOLD            40

/** Default irrigation durations per zone in seconds */
#define DEFAULT_Z1_DURATION             120
#define DEFAULT_Z2_DURATION             90

/** Decision cycle interval — how often the engine evaluates all zones (ms) */
#define IRRIGATION_CYCLE_INTERVAL_MS    1000

/** Delay between pump ON and valve OPEN to let pressure build (ms) */
#define PUMP_SETTLE_DELAY_MS            500

/* ========================================================================== */
/*                        SENSOR VALIDATION RANGES                            */
/* ========================================================================== */

/** Valid soil moisture range (percentage) */
#define SENSOR_SM_MIN                   0
#define SENSOR_SM_MAX                   100

/** Valid temperature range (°C) */
#define SENSOR_TEMP_MIN                 (-20.0f)
#define SENSOR_TEMP_MAX                 80.0f

/* ========================================================================== */
/*                         APPLICATION LOGIC                                  */
/* ========================================================================== */

/** Interval between re-discovery broadcasts (ms) */
#define REDISCOVERY_INTERVAL_MS         90000

/** If no ZONE_DATA arrives within this window, consider the zone offline
 *  and clear its needs_water flag so the pump can turn off.               */
#define ZONE_HEARTBEAT_TIMEOUT_MS       10000

/** Zone data older than this is considered stale — skip irrigation (ms) */
#define ZONE_STALE_DATA_MS              10000

/** Maximum number of zones the control can manage */
#define MAX_ZONES                       8

#endif // APP_CONFIG_H
