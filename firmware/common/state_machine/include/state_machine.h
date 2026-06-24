/**
 * @file state_machine.h
 * @brief Tiny, reusable finite-state-machine helper for ESP-IDF firmware.
 *
 * Pure C. The engine is intentionally minimal: it stores the current state,
 * a table of human-readable state names, and an optional on-change callback.
 * Each application defines its own state enum and name table and calls
 * sm_transition() at the points where the lifecycle advances. Every transition
 * is logged, which makes field debugging and the README state diagrams match
 * the real firmware exactly.
 *
 * The callback is the integration hook used to drive status LEDs / buzzer when
 * the system changes state, keeping that policy out of the business logic.
 *
 * @version 1.0.0
 */
#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Application state identifier (cast your own enum to this). */
typedef int sm_state_t;

/** Maps a state value to a printable name. */
typedef struct {
    sm_state_t  state;
    const char *name;
} sm_state_def_t;

struct sm_ctx;

/** Called after every successful transition (old -> new). */
typedef void (*sm_on_change_t)(struct sm_ctx *sm, sm_state_t from, sm_state_t to, void *user);

/** State-machine context. Treat fields as private; use the API. */
typedef struct sm_ctx {
    sm_state_t            current;
    const sm_state_def_t *defs;
    size_t                num_defs;
    const char           *tag;
    sm_on_change_t        on_change;
    void                 *user;
} sm_t;

/**
 * @brief Initialise a state machine.
 * @param[out] sm        Caller-allocated context.
 * @param[in]  defs      State-name table (must outlive @p sm).
 * @param[in]  num_defs  Number of entries in @p defs.
 * @param[in]  initial   Initial state.
 * @param[in]  tag       Log tag.
 */
void sm_init(sm_t *sm, const sm_state_def_t *defs, size_t num_defs,
             sm_state_t initial, const char *tag);

/**
 * @brief Register a callback invoked on every transition (e.g. to set LEDs).
 */
void sm_set_on_change(sm_t *sm, sm_on_change_t cb, void *user);

/**
 * @brief Transition to a new state. A no-op (still logged at debug) if already
 *        in that state.
 * @return ESP_OK always (kept for API symmetry / future validation).
 */
esp_err_t sm_transition(sm_t *sm, sm_state_t to);

/** @brief Get the current state. */
sm_state_t sm_get(const sm_t *sm);

/** @brief Resolve a state's printable name (or "?" if unknown). */
const char *sm_name(const sm_t *sm, sm_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* STATE_MACHINE_H */
