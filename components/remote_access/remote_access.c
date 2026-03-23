#include "remote_access.h"
#include "licence.h"
#include "nvs_config.h"

#include "mqtt_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "remote_access";

/* ---- Static state ---- */

static remote_state_t        s_state  = REMOTE_STATE_DISABLED;
static remote_config_t       s_config;
static esp_mqtt_client_handle_t s_mqtt_client = NULL;

/* Topic buffers */
#define TOPIC_MAX_LEN 80
static char s_topic_status[TOPIC_MAX_LEN];
static char s_topic_cmd[TOPIC_MAX_LEN];
static char s_topic_resp[TOPIC_MAX_LEN];

/* ---- MQTT event handler ---- */

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        s_state = REMOTE_STATE_CONNECTED;
        esp_mqtt_client_subscribe(s_mqtt_client, s_topic_cmd, 1);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected");
        if (s_state == REMOTE_STATE_CONNECTED) {
            s_state = REMOTE_STATE_CONNECTING;
        }
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT data on topic %.*s: %.*s",
                 event->topic_len, event->topic,
                 event->data_len, event->data);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        s_state = REMOTE_STATE_ERROR;
        break;

    default:
        break;
    }
}

/* ---- Public API ---- */

esp_err_t remote_init(const remote_config_t *config)
{
    if (!config || !config->relay_host) {
        return ESP_ERR_INVALID_ARG;
    }

    s_config = *config;

    /* Build topic strings */
    snprintf(s_topic_status, sizeof(s_topic_status),
             "asicos/%s/status", config->device_id);
    snprintf(s_topic_cmd, sizeof(s_topic_cmd),
             "asicos/%s/cmd", config->device_id);
    snprintf(s_topic_resp, sizeof(s_topic_resp),
             "asicos/%s/resp", config->device_id);

    return ESP_OK;
}

void remote_task_loop(void *param)
{
    char device_id[32];
    char licence_key[128];

    /* Step 1: Get device ID */
    licence_get_device_id(device_id, sizeof(device_id));
    ESP_LOGI(TAG, "Device ID: %s", device_id);

    /* Step 2: Licence validation loop */
    for (;;) {
        /* Read licence key from NVS */
        nvs_config_get_string(NVS_KEY_LICENCE, licence_key, sizeof(licence_key), "");

        if (licence_key[0] != '\0' &&
            licence_validate(device_id, licence_key)) {
            ESP_LOGI(TAG, "Licence valid");
            break;
        }

        ESP_LOGW(TAG, "No valid licence, retrying in 10s...");
        s_state = REMOTE_STATE_UNLICENSED;
        vTaskDelay(pdMS_TO_TICKS(10000));
    }

    /* Step 3: Initialise remote config with device_id */
    remote_config_t cfg = {
        .relay_host = "relay.asicos.io",
        .relay_port = 8883,
        .device_id  = device_id,
    };
    remote_init(&cfg);

    /* Step 4: Connect MQTT with TLS */
    char uri[128];
    snprintf(uri, sizeof(uri), "mqtts://%s:%u", s_config.relay_host, s_config.relay_port);

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri       = uri,
        .credentials.client_id    = device_id,
        .credentials.username     = device_id,
        .credentials.authentication.password = licence_key,
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_mqtt_client) {
        ESP_LOGE(TAG, "Failed to init MQTT client");
        s_state = REMOTE_STATE_ERROR;
        vTaskDelete(NULL);
        return;
    }

    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);

    s_state = REMOTE_STATE_CONNECTING;
    esp_mqtt_client_start(s_mqtt_client);

    /* Step 5: Periodic status publishing every 30s */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(30000));

        if (s_state == REMOTE_STATE_CONNECTED) {
            esp_mqtt_client_publish(s_mqtt_client, s_topic_status,
                                    "{\"status\":\"online\"}", 0, 1, 0);
            ESP_LOGD(TAG, "Published status heartbeat");
        }
    }
}

remote_state_t remote_get_state(void)
{
    return s_state;
}

bool remote_is_licensed(void)
{
    return s_state != REMOTE_STATE_UNLICENSED && s_state != REMOTE_STATE_DISABLED;
}
