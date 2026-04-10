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
/*                         APPLICATION LOGIC                                  */
/* ========================================================================== */

/** Interval between re-discovery broadcasts (ms) */
#define REDISCOVERY_INTERVAL_MS         30000

/** If no ZONE_DATA arrives within this window, consider the zone offline
 *  and clear its needs_water flag so the pump can turn off.               */
#define ZONE_HEARTBEAT_TIMEOUT_MS       60000

/** Maximum number of zones the control can manage */
#define MAX_ZONES                       8

#endif // APP_CONFIG_H
