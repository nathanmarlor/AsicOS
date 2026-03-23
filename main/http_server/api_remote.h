#pragma once
#include "esp_http_server.h"

esp_err_t api_remote_status_handler(httpd_req_t *req);
esp_err_t api_remote_activate_handler(httpd_req_t *req);
