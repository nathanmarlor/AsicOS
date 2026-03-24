#pragma once
#include "esp_http_server.h"

#define HISTORY_SIZE 120  /* 120 points = 10 min at 5s poll */

void history_record(float hashrate, float chip_temp, float vr_temp, float power_w, float efficiency_jth);
esp_err_t api_history_handler(httpd_req_t *req);
