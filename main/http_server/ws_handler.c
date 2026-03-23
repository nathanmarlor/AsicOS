#include "ws_handler.h"

#include <stdarg.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "ws_handler";

/* ── State ─────────────────────────────────────────────────────────── */

static httpd_handle_t s_server = NULL;
static int            s_ws_fd  = -1;

/* ── Handler ───────────────────────────────────────────────────────── */

esp_err_t ws_handler(httpd_req_t *req)
{
    /* First call is the HTTP GET handshake – just store the fd */
    if (req->method == HTTP_GET) {
        s_server = req->handle;
        s_ws_fd  = httpd_req_to_sockfd(req);
        ESP_LOGI(TAG, "WebSocket handshake, fd=%d", s_ws_fd);
        return ESP_OK;
    }

    /* Subsequent calls deliver WebSocket frames */
    httpd_ws_frame_t frame = {
        .type    = HTTPD_WS_TYPE_TEXT,
        .payload = NULL,
    };

    /* Read incoming frame (first call to get length) */
    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ws recv len failed: %d", ret);
        return ret;
    }

    if (frame.len > 0) {
        uint8_t *buf = calloc(1, frame.len + 1);
        if (!buf) {
            return ESP_ERR_NO_MEM;
        }
        frame.payload = buf;
        ret = httpd_ws_recv_frame(req, &frame, frame.len);
        if (ret == ESP_OK) {
            ESP_LOGD(TAG, "WS rx (%d bytes): %s", (int)frame.len, buf);
        }
        free(buf);
    }

    /* Acknowledge with a simple "ok" */
    const char *ack = "{\"ack\":true}";
    httpd_ws_frame_t ack_frame = {
        .type    = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)ack,
        .len     = strlen(ack),
    };
    return httpd_ws_send_frame(req, &ack_frame);
}

/* ── Public: send log line to connected client ─────────────────────── */

void ws_send_log(const char *msg)
{
    if (s_server == NULL || s_ws_fd < 0 || msg == NULL) {
        return;
    }

    httpd_ws_frame_t frame = {
        .type    = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)msg,
        .len     = strlen(msg),
    };

    esp_err_t ret = httpd_ws_send_frame_async(s_server, s_ws_fd, &frame);
    if (ret != ESP_OK) {
        /* Don't use ESP_LOGW here to avoid recursion */
        s_ws_fd = -1;
    }
}

/* ── ESP log hook: forward all log output to WebSocket ─────────────── */

static vprintf_like_t s_original_vprintf = NULL;

static int ws_log_vprintf(const char *fmt, va_list args)
{
    /* Copy args before original handler consumes them */
    va_list args_copy;
    va_copy(args_copy, args);

    /* Original UART output */
    int ret = s_original_vprintf(fmt, args);

    /* Forward to WebSocket if connected, stripping ANSI escape codes */
    if (s_ws_fd >= 0 && s_server) {
        char raw[256];
        vsnprintf(raw, sizeof(raw), fmt, args_copy);

        /* Strip ANSI escape sequences: \033[...m or \e[...m */
        char clean[256];
        int ci = 0;
        for (int ri = 0; raw[ri] && ci < (int)sizeof(clean) - 1; ri++) {
            if (raw[ri] == '\033' || (raw[ri] == '[' && ri > 0 && raw[ri-1] == '\033')) {
                /* Skip until 'm' */
                if (raw[ri] == '\033') {
                    while (raw[ri] && raw[ri] != 'm') ri++;
                    continue;
                }
            }
            /* Skip standalone [0m etc that weren't caught */
            if (raw[ri] == '[' && ri + 1 < (int)sizeof(raw) && raw[ri+1] >= '0' && raw[ri+1] <= '9') {
                int peek = ri + 1;
                while (raw[peek] && raw[peek] != 'm' && peek - ri < 8) peek++;
                if (raw[peek] == 'm') { ri = peek; continue; }
            }
            clean[ci++] = raw[ri];
        }
        clean[ci] = '\0';
        if (ci > 0) ws_send_log(clean);
    }
    va_end(args_copy);

    return ret;
}

void ws_log_init(void)
{
    s_original_vprintf = esp_log_set_vprintf(ws_log_vprintf);
}
