#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    REMOTE_STATE_DISABLED,
    REMOTE_STATE_CONNECTING,
    REMOTE_STATE_CONNECTED,
    REMOTE_STATE_ERROR,
    REMOTE_STATE_UNLICENSED,
} remote_state_t;

typedef struct {
    const char *relay_host;
    uint16_t    relay_port;
    const char *device_id;
} remote_config_t;

esp_err_t      remote_init(const remote_config_t *config);
void           remote_task_loop(void *param);
remote_state_t remote_get_state(void);
bool           remote_is_licensed(void);
