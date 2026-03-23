#pragma once
#include "esp_http_server.h"

esp_err_t api_tuner_status_handler(httpd_req_t *req);
esp_err_t api_tuner_start_handler(httpd_req_t *req);
esp_err_t api_tuner_abort_handler(httpd_req_t *req);
esp_err_t api_tuner_apply_handler(httpd_req_t *req);
