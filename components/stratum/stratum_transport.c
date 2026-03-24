#include "stratum_transport.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static const char *TAG = "stratum_transport";

#define RECV_BUF_SIZE 4096

struct stratum_conn {
    int          sock;
    esp_tls_t   *tls;
    bool         use_tls;
    char         recv_buf[RECV_BUF_SIZE];
    size_t       recv_pos;
};

stratum_conn_t *stratum_connect(const char *host, uint16_t port, bool tls)
{
    stratum_conn_t *conn = calloc(1, sizeof(*conn));
    if (!conn) return NULL;
    conn->sock = -1;
    conn->use_tls = tls;

    if (tls) {
        esp_tls_cfg_t cfg = {
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = 10000,
        };

        conn->tls = esp_tls_init();
        if (!conn->tls) {
            ESP_LOGE(TAG, "esp_tls_init failed");
            free(conn);
            return NULL;
        }

        char port_str[8];
        snprintf(port_str, sizeof(port_str), "%u", port);

        if (esp_tls_conn_new_sync(host, strlen(host), port, &cfg, conn->tls) < 0) {
            ESP_LOGE(TAG, "TLS connection to %s:%u failed", host, port);
            esp_tls_conn_destroy(conn->tls);
            free(conn);
            return NULL;
        }

        /* Set SO_RCVTIMEO on the underlying socket for TLS connections */
        int tls_sockfd = -1;
        if (esp_tls_get_conn_sockfd(conn->tls, &tls_sockfd) == ESP_OK && tls_sockfd >= 0) {
            struct timeval tv = { .tv_sec = 30, .tv_usec = 0 };
            setsockopt(tls_sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        }

        ESP_LOGI(TAG, "TLS connected to %s:%u", host, port);
    } else {
        struct addrinfo hints = {
            .ai_family = AF_INET,
            .ai_socktype = SOCK_STREAM,
        };
        struct addrinfo *res = NULL;
        char port_str[8];
        snprintf(port_str, sizeof(port_str), "%u", port);

        int err = getaddrinfo(host, port_str, &hints, &res);
        if (err != 0 || !res) {
            ESP_LOGE(TAG, "DNS resolve failed for %s: %d", host, err);
            free(conn);
            return NULL;
        }

        conn->sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (conn->sock < 0) {
            ESP_LOGE(TAG, "socket() failed: %d", errno);
            freeaddrinfo(res);
            free(conn);
            return NULL;
        }

        if (connect(conn->sock, res->ai_addr, res->ai_addrlen) != 0) {
            ESP_LOGE(TAG, "connect() to %s:%u failed: %d", host, port, errno);
            close(conn->sock);
            freeaddrinfo(res);
            free(conn);
            return NULL;
        }

        freeaddrinfo(res);

        /* Set 30s receive timeout */
        struct timeval tv = { .tv_sec = 30, .tv_usec = 0 };
        setsockopt(conn->sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        ESP_LOGI(TAG, "TCP connected to %s:%u", host, port);
    }

    return conn;
}

void stratum_disconnect(stratum_conn_t *conn)
{
    if (!conn) return;

    if (conn->use_tls && conn->tls) {
        esp_tls_conn_destroy(conn->tls);
        conn->tls = NULL;
    } else if (conn->sock >= 0) {
        close(conn->sock);
        conn->sock = -1;
    }

    free(conn);
}

int stratum_send(stratum_conn_t *conn, const char *data, size_t len)
{
    if (!conn) return -1;

    if (conn->use_tls && conn->tls) {
        size_t written = 0;
        while (written < len) {
            int ret = esp_tls_conn_write(conn->tls, data + written, len - written);
            if (ret < 0) {
                ESP_LOGE(TAG, "TLS write failed: %d", ret);
                return -1;
            }
            written += (size_t)ret;
        }
        return (int)written;
    }

    if (conn->sock >= 0) {
        size_t written = 0;
        while (written < len) {
            int ret = send(conn->sock, data + written, len - written, 0);
            if (ret < 0) {
                ESP_LOGE(TAG, "TCP send failed: %d", errno);
                return -1;
            }
            written += (size_t)ret;
        }
        return (int)written;
    }

    return -1;
}

int stratum_recv_line(stratum_conn_t *conn, char *buf, size_t buf_len, uint32_t timeout_ms)
{
    if (!conn || !buf || buf_len < 2) return -1;

    /* Set timeout for this call */
    if (!conn->use_tls && conn->sock >= 0) {
        struct timeval tv = {
            .tv_sec  = timeout_ms / 1000,
            .tv_usec = (timeout_ms % 1000) * 1000,
        };
        setsockopt(conn->sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    while (1) {
        /* Scan existing buffer for a newline */
        for (size_t i = 0; i < conn->recv_pos; i++) {
            if (conn->recv_buf[i] == '\n') {
                size_t line_len = i; /* not including '\n' */
                if (line_len >= buf_len)
                    line_len = buf_len - 1;
                memcpy(buf, conn->recv_buf, line_len);
                buf[line_len] = '\0';

                /* Shift remaining data forward */
                size_t remaining = conn->recv_pos - (i + 1);
                if (remaining > 0)
                    memmove(conn->recv_buf, conn->recv_buf + i + 1, remaining);
                conn->recv_pos = remaining;

                return (int)line_len;
            }
        }

        /* No newline found - need more data */
        if (conn->recv_pos >= RECV_BUF_SIZE - 1) {
            ESP_LOGE(TAG, "recv buffer full without newline, flushing");
            conn->recv_pos = 0;
        }

        int ret;
        size_t space = RECV_BUF_SIZE - conn->recv_pos - 1;

        if (conn->use_tls && conn->tls) {
            ret = esp_tls_conn_read(conn->tls, conn->recv_buf + conn->recv_pos, space);
        } else if (conn->sock >= 0) {
            ret = recv(conn->sock, conn->recv_buf + conn->recv_pos, space, 0);
        } else {
            return -1;
        }

        if (ret <= 0) {
            if (ret == 0) {
                ESP_LOGW(TAG, "Connection closed by peer");
            } else {
                ESP_LOGW(TAG, "recv error: %d (errno=%d)", ret, errno);
            }
            return -1;
        }

        conn->recv_pos += (size_t)ret;
    }
}

bool stratum_is_connected(stratum_conn_t *conn)
{
    if (!conn) return false;
    if (conn->use_tls) return conn->tls != NULL;
    return conn->sock >= 0;
}
