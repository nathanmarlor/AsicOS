#include "http_server.h"

#include <string.h>
#include <sys/stat.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_spiffs.h"

#include "api_mining.h"
#include "api_remote.h"
#include "api_system.h"
#include "api_tuner.h"
#include "api_metrics.h"
#include "ws_handler.h"

static const char *TAG = "http_server";

static httpd_handle_t s_server = NULL;

/* ── Content-type detection ────────────────────────────────────────── */

static const char *get_content_type(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";

    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".css")  == 0) return "text/css";
    if (strcmp(ext, ".js")   == 0) return "application/javascript";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".png")  == 0) return "image/png";
    if (strcmp(ext, ".jpg")  == 0) return "image/jpeg";
    if (strcmp(ext, ".ico")  == 0) return "image/x-icon";
    if (strcmp(ext, ".svg")  == 0) return "image/svg+xml";
    if (strcmp(ext, ".woff") == 0) return "font/woff";
    if (strcmp(ext, ".woff2") == 0) return "font/woff2";

    return "application/octet-stream";
}

/* ── SPIFFS static-file handler (SPA fallback) ─────────────────────── */

static esp_err_t spiffs_handler(httpd_req_t *req)
{
    char filepath[128];
    const char *uri = req->uri;

    /* Strip query string if present */
    const char *query = strchr(uri, '?');
    size_t uri_len = query ? (size_t)(query - uri) : strlen(uri);

    /* Build file path */
    if (uri_len == 1 && uri[0] == '/') {
        snprintf(filepath, sizeof(filepath), "/www/index.html");
    } else {
        snprintf(filepath, sizeof(filepath), "/www%.*s", (int)uri_len, uri);
    }

    /* Check if file exists; SPA fallback to index.html */
    struct stat st;
    if (stat(filepath, &st) != 0) {
        snprintf(filepath, sizeof(filepath), "/www/index.html");
        if (stat(filepath, &st) != 0) {
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
            return ESP_FAIL;
        }
    }

    /* Open and serve */
    FILE *f = fopen(filepath, "r");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open file");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, get_content_type(filepath));

    /* Cache headers for static assets */
    const char *ext = strrchr(filepath, '.');
    if (ext && (strcmp(ext, ".js") == 0 || strcmp(ext, ".css") == 0)) {
        httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
    }

    /* For small files (< 8KB like index.html), send in one shot.
     * For larger files, use chunked transfer with heap buffer. */
    if (st.st_size <= 8192) {
        char *content = malloc(st.st_size);
        if (content) {
            fread(content, 1, st.st_size, f);
            fclose(f);
            httpd_resp_send(req, content, st.st_size);
            free(content);
            return ESP_OK;
        }
    }

    char *buf = malloc(4096);
    if (!buf) {
        fclose(f);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
        return ESP_FAIL;
    }
    size_t read_bytes;
    while ((read_bytes = fread(buf, 1, 4096, f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, read_bytes) != ESP_OK) {
            fclose(f);
            free(buf);
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_FAIL;
        }
    }
    fclose(f);
    free(buf);

    /* End chunked response */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* ── SPIFFS init ───────────────────────────────────────────────────── */

static esp_err_t init_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path              = "/www",
        .partition_label        = "www",
        .max_files              = 8,
        .format_if_mount_failed = false,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0, used = 0;
    esp_spiffs_info("www", &total, &used);
    ESP_LOGI(TAG, "SPIFFS: total=%d, used=%d", (int)total, (int)used);

    return ESP_OK;
}

/* ── CORS preflight handler ────────────────────────────────────────── */

static esp_err_t cors_options_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type, Authorization");
    httpd_resp_set_hdr(req, "Access-Control-Max-Age", "86400");
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* ── Route registration ────────────────────────────────────────────── */

static void register_routes(httpd_handle_t server)
{
    /* CORS preflight (OPTIONS) catch-all - must be registered early */
    httpd_uri_t cors_preflight = {
        .uri      = "/api/*",
        .method   = HTTP_OPTIONS,
        .handler  = cors_options_handler,
    };
    httpd_register_uri_handler(server, &cors_preflight);

    /* System API */
    httpd_uri_t system_info = {
        .uri      = "/api/system/info",
        .method   = HTTP_GET,
        .handler  = api_system_info_handler,
    };
    httpd_register_uri_handler(server, &system_info);

    httpd_uri_t system_patch = {
        .uri      = "/api/system",
        .method   = HTTP_POST,
        .handler  = api_system_patch_handler,
    };
    httpd_register_uri_handler(server, &system_patch);

    httpd_uri_t system_restart = {
        .uri      = "/api/system/restart",
        .method   = HTTP_POST,
        .handler  = api_system_restart_handler,
    };
    httpd_register_uri_handler(server, &system_restart);

    httpd_uri_t system_ota = {
        .uri      = "/api/system/ota",
        .method   = HTTP_POST,
        .handler  = api_system_ota_handler,
    };
    httpd_register_uri_handler(server, &system_ota);

    httpd_uri_t ota_check = {
        .uri      = "/api/system/ota/check",
        .method   = HTTP_GET,
        .handler  = api_system_ota_check_handler,
    };
    httpd_register_uri_handler(server, &ota_check);

    httpd_uri_t ota_github = {
        .uri      = "/api/system/ota/github",
        .method   = HTTP_POST,
        .handler  = api_system_ota_github_handler,
    };
    httpd_register_uri_handler(server, &ota_github);

    /* Mining API */
    httpd_uri_t mining_info = {
        .uri      = "/api/mining/info",
        .method   = HTTP_GET,
        .handler  = api_mining_info_handler,
    };
    httpd_register_uri_handler(server, &mining_info);

    /* Tuner API */
    httpd_uri_t tuner_status = {
        .uri      = "/api/tuner/status",
        .method   = HTTP_GET,
        .handler  = api_tuner_status_handler,
    };
    httpd_register_uri_handler(server, &tuner_status);

    httpd_uri_t tuner_start = {
        .uri      = "/api/tuner/start",
        .method   = HTTP_POST,
        .handler  = api_tuner_start_handler,
    };
    httpd_register_uri_handler(server, &tuner_start);

    httpd_uri_t tuner_abort = {
        .uri      = "/api/tuner/abort",
        .method   = HTTP_POST,
        .handler  = api_tuner_abort_handler,
    };
    httpd_register_uri_handler(server, &tuner_abort);

    httpd_uri_t tuner_apply = {
        .uri      = "/api/tuner/apply",
        .method   = HTTP_POST,
        .handler  = api_tuner_apply_handler,
    };
    httpd_register_uri_handler(server, &tuner_apply);

    /* Remote API */
    httpd_uri_t remote_status = {
        .uri      = "/api/remote/status",
        .method   = HTTP_GET,
        .handler  = api_remote_status_handler,
    };
    httpd_register_uri_handler(server, &remote_status);

    httpd_uri_t remote_activate = {
        .uri      = "/api/remote/activate",
        .method   = HTTP_POST,
        .handler  = api_remote_activate_handler,
    };
    httpd_register_uri_handler(server, &remote_activate);

    /* WebSocket */
#ifdef CONFIG_HTTPD_WS_SUPPORT
    httpd_uri_t ws = {
        .uri                    = "/api/ws",
        .method                 = HTTP_GET,
        .handler                = ws_handler,
        .is_websocket           = true,
        .handle_ws_control_frames = true,
    };
    httpd_register_uri_handler(server, &ws);
#else
    ESP_LOGW(TAG, "WebSocket support not enabled (CONFIG_HTTPD_WS_SUPPORT)");
#endif

    /* Prometheus metrics */
    httpd_uri_t metrics = {
        .uri      = "/metrics",
        .method   = HTTP_GET,
        .handler  = api_metrics_handler,
    };
    httpd_register_uri_handler(server, &metrics);

    /* SPIFFS catch-all (must be last — wildcard) */
    httpd_uri_t spiffs_catch_all = {
        .uri      = "/*",
        .method   = HTTP_GET,
        .handler  = spiffs_handler,
    };
    httpd_register_uri_handler(server, &spiffs_catch_all);
}

/* ── Public API ────────────────────────────────────────────────────── */

esp_err_t http_server_start(void)
{
    if (s_server) {
        ESP_LOGW(TAG, "HTTP server already running");
        return ESP_OK;
    }

    esp_err_t ret = init_spiffs();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SPIFFS init failed, static files will not be served");
    }

    httpd_config_t config   = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 24;
    config.stack_size       = 12288;
    config.max_resp_headers = 16;
    config.max_open_sockets   = 10;
    config.recv_wait_timeout  = 10;
    config.send_wait_timeout  = 10;
    config.lru_purge_enable   = true;
    config.uri_match_fn     = httpd_uri_match_wildcard;

    ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    register_routes(s_server);

    ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
    return ESP_OK;
}

void http_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "HTTP server stopped");
    }
}
