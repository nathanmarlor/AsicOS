#pragma once
#include "esp_http_server.h"

esp_err_t api_system_info_handler(httpd_req_t *req);
esp_err_t api_system_patch_handler(httpd_req_t *req);
esp_err_t api_system_restart_handler(httpd_req_t *req);
esp_err_t api_system_ota_handler(httpd_req_t *req);
esp_err_t api_system_ota_check_handler(httpd_req_t *req);
esp_err_t api_system_ota_github_handler(httpd_req_t *req);
esp_err_t api_system_ota_www_handler(httpd_req_t *req);
