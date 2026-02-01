#ifndef _DS18B20_PRIVATE_H
#define _DS18B20_PRIVATE_H

#include "temp_ds18b20.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                           INTERNAL CONSTANTS                               */
/* ========================================================================== */

/**
 * For ds18b20 + onewire enumeration, the bus needs RX capacity for:
 *  1 byte ROM command + 8 bytes ROM number + 1 byte device command = 10 bytes
 */
#define TEMP_DS18B20_DEFAULT_MAX_RX_BYTES   (10U)

#define TEMP_DS18B20_DEFAULT_MAX_RETRIES    (3U)

/* DS18B20 conversion time (max) in ms depends on resolution.
 * Datasheet tCONV max at 12-bit is 750ms, scaling by 1/2 each step.
 */
#define TEMP_DS18B20_TCONV_12BIT_MS         (750U)
#define TEMP_DS18B20_TCONV_11BIT_MS         (375U)
#define TEMP_DS18B20_TCONV_10BIT_MS         (188U)
#define TEMP_DS18B20_TCONV_9BIT_MS          (94U)

/* ========================================================================== */
/*                        INTERNAL FUNCTION PROTOTYPES                        */
/* ========================================================================== */

uint32_t temp_ds18b20_get_tconv_ms(Temp_DS18B20_Resolution_t res);

#ifdef __cplusplus
}
#endif

#endif /* TEMP_DS18B20_PRIVATE_H */
