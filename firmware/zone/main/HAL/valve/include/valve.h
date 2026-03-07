#ifndef VALVE_H
#define VALVE_H

#include <stdio.h>
#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                               VERSION INFO                                 */
/* ========================================================================== */

#define VALVE_HAL_VERSION_MAJOR    (1)
#define VALVE_HAL_VERSION_MINOR    (0)
#define VALVE_HAL_VERSION_PATCH    (0)

/* ========================================================================== */
/*                              TYPE DEFINITIONS                              */
/* ========================================================================== */

typedef enum{
    VALVE_STATE_CLOSED = 0,
    VALVE_STATE_OPENED,
    VALVE_STATE_OPENING,
    VALVE_STATE_CLOSING,
    VALVE_STATE_ERROR
} valve_state_t;

typedef struct {
    gpio_num_t control_pin;
    valve_state_t state;
    uint32_t timeout_ms;
    uint32_t total_open_time_ms;
    uint32_t open_count;
    uint64_t open_start_us;        /**< Internal: timestamp when valve was opened */
} valve_t;

/* ========================================================================== */
/*                             FUNCTION PROTOTYPES                            */
/* ========================================================================== */
esp_err_t valve_init(valve_t *valve);
esp_err_t valve_open(valve_t *valve);
esp_err_t valve_close(valve_t *valve);
valve_state_t valve_get_state(valve_t *valve);
esp_err_t valve_deinit(valve_t *valve);
esp_err_t valve_setTimeOut(valve_t *valve, uint32_t timeout_ms);
esp_err_t Valve_GetTotalOpenTime(valve_t *valve, uint32_t *total_open_time_ms);
esp_err_t Valve_GetOpenCount(valve_t *valve, uint32_t *open_count);

#ifdef __cplusplus
}
#endif

#endif // VALVE_H