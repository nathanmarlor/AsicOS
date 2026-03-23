#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct stratum_conn stratum_conn_t;

stratum_conn_t *stratum_connect(const char *host, uint16_t port, bool tls);
void            stratum_disconnect(stratum_conn_t *conn);
int             stratum_send(stratum_conn_t *conn, const char *data, size_t len);
int             stratum_recv_line(stratum_conn_t *conn, char *buf, size_t buf_len,
                                  uint32_t timeout_ms);
bool            stratum_is_connected(stratum_conn_t *conn);
