/**
 * @file state_machine.c
 * @brief Implementation of the tiny reusable FSM helper.
 *
 * @version 1.0.0
 */
#include "state_machine.h"
#include "esp_log.h"

void sm_init(sm_t *sm, const sm_state_def_t *defs, size_t num_defs,
             sm_state_t initial, const char *tag)
{
    if (sm == NULL) {
        return;
    }
    sm->current   = initial;
    sm->defs      = defs;
    sm->num_defs  = num_defs;
    sm->tag       = (tag != NULL) ? tag : "SM";
    sm->on_change = NULL;
    sm->user      = NULL;

    ESP_LOGI(sm->tag, "FSM init -> %s", sm_name(sm, initial));
}

void sm_set_on_change(sm_t *sm, sm_on_change_t cb, void *user)
{
    if (sm == NULL) {
        return;
    }
    sm->on_change = cb;
    sm->user      = user;
}

const char *sm_name(const sm_t *sm, sm_state_t state)
{
    if (sm != NULL && sm->defs != NULL) {
        for (size_t i = 0; i < sm->num_defs; i++) {
            if (sm->defs[i].state == state) {
                return sm->defs[i].name;
            }
        }
    }
    return "?";
}

esp_err_t sm_transition(sm_t *sm, sm_state_t to)
{
    if (sm == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (sm->current == to) {
        ESP_LOGD(sm->tag, "(stay) %s", sm_name(sm, to));
        return ESP_OK;
    }

    sm_state_t from = sm->current;
    ESP_LOGI(sm->tag, "%s -> %s", sm_name(sm, from), sm_name(sm, to));
    sm->current = to;

    if (sm->on_change != NULL) {
        sm->on_change(sm, from, to, sm->user);
    }
    return ESP_OK;
}

sm_state_t sm_get(const sm_t *sm)
{
    return (sm != NULL) ? sm->current : 0;
}
