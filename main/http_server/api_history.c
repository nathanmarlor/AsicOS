#include "api_history.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"

static float s_hashrate[HISTORY_SIZE];
static float s_chip_temp[HISTORY_SIZE];
static float s_vr_temp[HISTORY_SIZE];
static float s_power[HISTORY_SIZE];
static float s_efficiency[HISTORY_SIZE];
static int   s_idx = 0;
static int   s_count = 0;

void history_record(float hashrate, float chip_temp, float vr_temp, float power_w, float efficiency_jth)
{
    s_hashrate[s_idx]   = hashrate;
    s_chip_temp[s_idx]  = chip_temp;
    s_vr_temp[s_idx]    = vr_temp;
    s_power[s_idx]      = power_w;
    s_efficiency[s_idx] = efficiency_jth;
    s_idx = (s_idx + 1) % HISTORY_SIZE;
    if (s_count < HISTORY_SIZE) s_count++;
}

static cJSON *ring_to_array(const float *buf, int idx, int count)
{
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        int ri = (idx - count + i + HISTORY_SIZE) % HISTORY_SIZE;
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(buf[ri]));
    }
    return arr;
}

esp_err_t api_history_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "application/json");

    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "hashrate",   ring_to_array(s_hashrate, s_idx, s_count));
    cJSON_AddItemToObject(root, "chip_temp",  ring_to_array(s_chip_temp, s_idx, s_count));
    cJSON_AddItemToObject(root, "vr_temp",    ring_to_array(s_vr_temp, s_idx, s_count));
    cJSON_AddItemToObject(root, "power",      ring_to_array(s_power, s_idx, s_count));
    cJSON_AddItemToObject(root, "efficiency", ring_to_array(s_efficiency, s_idx, s_count));

    char *str = cJSON_PrintUnformatted(root);
    httpd_resp_sendstr(req, str);
    free(str);
    cJSON_Delete(root);
    return ESP_OK;
}
