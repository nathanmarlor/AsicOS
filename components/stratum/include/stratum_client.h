#pragma once
#include "stratum_api.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    STRATUM_STATE_DISCONNECTED,
    STRATUM_STATE_CONNECTING,
    STRATUM_STATE_SUBSCRIBING,
    STRATUM_STATE_AUTHORIZING,
    STRATUM_STATE_CONFIGURING,
    STRATUM_STATE_MINING,
    STRATUM_STATE_ERROR,
} stratum_state_t;

typedef struct {
    char     pool_url[128];
    uint16_t pool_port;
    char     pool_user[128];
    char     pool_pass[64];
    bool     use_tls;
} stratum_pool_config_t;

typedef void (*stratum_notify_cb_t)(const stratum_notify_t *notify, int pool_id);
typedef void (*stratum_difficulty_cb_t)(double difficulty, int pool_id);

typedef struct {
    stratum_pool_config_t primary;
    stratum_pool_config_t fallback;
    stratum_notify_cb_t   on_notify;
    stratum_difficulty_cb_t on_difficulty;
} stratum_client_config_t;

esp_err_t stratum_client_init(const stratum_client_config_t *config);
void      stratum_client_task(void *param);

stratum_state_t stratum_client_get_state(void);
const char     *stratum_client_get_extranonce1(void);
int             stratum_client_get_extranonce2_size(void);
uint32_t        stratum_client_get_accepted(void);
uint32_t        stratum_client_get_rejected(void);
double          stratum_client_get_current_difficulty(void);

esp_err_t stratum_client_submit_share(const char *job_id, const char *extranonce2,
                                      const char *ntime, const char *nonce,
                                      const char *version_bits);
