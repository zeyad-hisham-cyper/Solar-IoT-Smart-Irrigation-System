#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "hal/adc_types.h"

/* ========================================================================== */
/*                          WIFI CONFIGURATION                                */
/* ========================================================================== */

/** WiFi credentials (WIFI_SSID / WIFI_PASS) come from secrets.h */
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Missing secrets.h - copy secrets.example.h to secrets.h and fill in your credentials"
#endif

/* ========================================================================== */
/*                     PUMP CONFIGURATION (CONTROL BOARD)                     */
/* ========================================================================== */

/*
 * The water pump is driven by a single low-side MOSFET (or relay) switch on
 * one GPIO. The hardware uses NEGATIVE (active-LOW) logic:
 *   - GPIO LOW  -> pump ON
 *   - GPIO HIGH -> pump OFF (safe state)
 *
 * On reset all ESP32 GPIOs default to inputs, so the pump driver configures an
 * internal pull-up and drives the OFF level immediately to guarantee the pump
 * is never energised during boot or after a crash.
 */
#define PUMP_RELAY_GPIO         GPIO_NUM_25
#define PUMP_ACTIVE_HIGH        false       /**< false = active-LOW (negative logic) */

/* ========================================================================== */
/*                  STATUS INDICATORS (CONTROL BOARD)                         */
/* ========================================================================== */

/* Status LEDs (all active-HIGH unless your hardware differs) */
#define LED_GREEN_GPIO          GPIO_NUM_26  /**< System healthy / idle        */
#define LED_YELLOW_GPIO         GPIO_NUM_27  /**< Busy: pump running / activity */
#define LED_RED_GPIO            GPIO_NUM_14  /**< Fault / alarm                 */
#define STATUS_LED_ACTIVE_HIGH  true

/* Buzzer */
#define BUZZER_GPIO             GPIO_NUM_32
#define BUZZER_ACTIVE_HIGH      true
#define BUZZER_USE_LEDC         true         /**< true = passive buzzer (tone), false = active buzzer */
#define BUZZER_LEDC_CHANNEL     LEDC_CHANNEL_4
#define BUZZER_LEDC_TIMER       LEDC_TIMER_2

/* ========================================================================== */
/*                  WATER LEVEL SENSOR (CONTROL BOARD)                        */
/* ========================================================================== */

/*
 * Reservoir water-level sensor on an ADC1 input. GPIO34 is input-only
 * (ADC1_CHANNEL_6) so it cannot conflict with any output peripheral.
 * Map the raw ADC reading linearly between EMPTY and FULL reference points
 * (calibrate these in the field for your specific sensor/tank geometry).
 */
#define WATER_LEVEL_ADC_UNIT        ADC_UNIT_1
#define WATER_LEVEL_ADC_CHANNEL     ADC_CHANNEL_6   /**< GPIO34 */
#define WATER_LEVEL_ADC_ATTEN       ADC_ATTEN_DB_12
#define WATER_LEVEL_ADC_BITWIDTH    ADC_BITWIDTH_12
#define WATER_LEVEL_ADC_SAMPLES     32

/** Raw ADC references (12-bit). Calibrate per tank: dry tank vs full tank. */
#define WATER_LEVEL_RAW_EMPTY       0
#define WATER_LEVEL_RAW_FULL        4095

/** Below this percentage the tank is considered dry: pump is inhibited. */
#define WATER_LEVEL_DRY_THRESHOLD   10

/** How often the gateway samples the water-level sensor (ms). */
#define WATER_LEVEL_POLL_INTERVAL_MS 5000

/* ========================================================================== */
/*                      OPENWEATHER API CONFIGURATION                         */
/* ========================================================================== */

/** Geographic coordinates (MIU) */
#define WEATHER_API_LAT         "30.166734786337404"
#define WEATHER_API_LON         "31.49253916004326"

/** OpenWeather API key (WEATHER_API_KEY) comes from secrets.h */

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
