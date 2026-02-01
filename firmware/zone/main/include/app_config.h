#ifndef APP_CONFIG_H
#define APP_CONFIG_H

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


#endif // APP_CONFIG_H
