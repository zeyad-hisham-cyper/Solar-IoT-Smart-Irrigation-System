#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* ========================================================================== */
/*                       ESP-NOW CHANNEL CONFIGURATION                        */
/* ========================================================================== */

/**
 * @brief WiFi channel for ESP-NOW communication.
 *
 * This MUST match the channel of the WiFi AP that the control board
 * connects to. Check your router settings or control board logs for
 * the actual channel.
 *
 * Control board log example: "wifi:new:<8,0>" means channel 8.
 */
#define ESP_NOW_CHANNEL  8

#include "driver/gpio.h"
#include "hal/adc_types.h"

/* ========================================================================== */
/*                       SOIL MOISTURE CONFIGURATION                          */
/* ========================================================================== */

/* Hardware Pin Definitions */
#define SOIL_ADC_UNIT           ADC_UNIT_1
#define SOIL_ADC_CHANNEL        ADC_CHANNEL_7
#define SOIL_POWER_GPIO         GPIO_NUM_25
#define SOIL_CALIBRATION_BUTTON GPIO_NUM_0      /* Button for user calibration (e.g., Boot Button) */

/* Sensor Behavior */
#define SOIL_READ_DELAY_MS      100              /* Stabilization delay after powering sensor */
#define SOIL_ADC_SAMPLES        64              /* Number of samples for averaging (Noise Reduction) */

/* ADC Settings (Defaults) */
#define SOIL_ADC_ATTEN          ADC_ATTEN_DB_12
#define SOIL_ADC_BITWIDTH       ADC_BITWIDTH_12


/* ========================================================================== */
/*                          DS18B20 CONFIGURATION                             */
/* ========================================================================== */

#define DS18B20_DATA_GPIO          GPIO_NUM_18
#define DS18B20_INTERNAL_PULLUP    1
#define DS18B20_MAX_RETRIES        3
#define DS18B20_RESOLUTION_BITS    12


/* ========================================================================== */
/*                             VALVE CONFIGURATION                            */
/* ========================================================================== */

#define VALVE_CONTROL_PIN          GPIO_NUM_23
#define VALVE_TIMEOUT_MS           600000       /* 10 minutes — safety auto-close */


/* ========================================================================== */
/*                          FLOW METER CONFIGURATION                          */
/* ========================================================================== */

#define FLOW_METER_PIN             GPIO_NUM_19
/* Typical Calibration: F(Hz) = 98 * Q(L/min) -> 5880 pulses/L */
#define FLOW_METER_PULSES_PER_LITER    5880


/* ========================================================================== */
/*                              APPLICATION LOGIC                             */
/* ========================================================================== */

/* Moisture threshold below which the zone requests water (0-100%) */
#define SOIL_MOISTURE_WATER_THRESHOLD   40

/* Interval between sensor readings in milliseconds */
#define SENSOR_READ_INTERVAL_MS         3000

#endif // APP_CONFIG_H
