#include "wifi_task.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "nvs_config.h"

static const char *TAG = "wifi";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_CONNECT_TIMEOUT_MS 15000
#define DNS_PORT 53
#define DNS_TASK_STACK_SIZE 4096
#define DNS_TASK_PRIORITY 1

/* ── Static state ──────────────────────────────────────────────────── */

static EventGroupHandle_t s_wifi_events;
static volatile bool s_connected = false;

/* ── Event handler ─────────────────────────────────────────────────── */

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (s_wifi_events) {
            xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        }
        ESP_LOGW(TAG, "Disconnected from AP, attempting reconnect...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_connected = true;
        if (s_wifi_events) {
            xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
        }
    }
}

/* ── AP mode ───────────────────────────────────────────────────────── */

static void start_ap_mode(void)
{
    esp_netif_create_default_wifi_ap();

    wifi_config_t ap_config = {
        .ap = {
            .ssid = "AsicOS-Setup",
            .ssid_len = strlen("AsicOS-Setup"),
            .channel = 1,
            .authmode = WIFI_AUTH_OPEN,
            .max_connection = 4,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP mode started: SSID=\"AsicOS-Setup\"");
}

/* ── Captive portal DNS task ───────────────────────────────────────── */

static void dns_task(void *pvParameters)
{
    (void)pvParameters;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS socket creation failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "DNS socket bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Captive portal DNS responder started on port %d", DNS_PORT);

    uint8_t rx_buf[512];
    uint8_t tx_buf[512];

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int len = recvfrom(sock, rx_buf, sizeof(rx_buf), 0,
                           (struct sockaddr *)&client_addr, &addr_len);
        if (len < 12) {
            /* Too short to be a valid DNS query */
            continue;
        }

        /*
         * Build a minimal DNS response that redirects everything to
         * 192.168.4.1 (the AP gateway address).
         *
         * Header (12 bytes):
         *   - Transaction ID: copied from query
         *   - Flags: 0x8180 (standard response, recursion available, no error)
         *   - QDCOUNT: 1
         *   - ANCOUNT: 1
         *   - NSCOUNT: 0
         *   - ARCOUNT: 0
         */
        int offset = 0;

        /* Transaction ID */
        tx_buf[offset++] = rx_buf[0];
        tx_buf[offset++] = rx_buf[1];

        /* Flags */
        tx_buf[offset++] = 0x81;
        tx_buf[offset++] = 0x80;

        /* QDCOUNT = 1 */
        tx_buf[offset++] = 0x00;
        tx_buf[offset++] = 0x01;

        /* ANCOUNT = 1 */
        tx_buf[offset++] = 0x00;
        tx_buf[offset++] = 0x01;

        /* NSCOUNT = 0 */
        tx_buf[offset++] = 0x00;
        tx_buf[offset++] = 0x00;

        /* ARCOUNT = 0 */
        tx_buf[offset++] = 0x00;
        tx_buf[offset++] = 0x00;

        /*
         * Copy the question section from the query.
         * Walk past the QNAME (sequence of labels terminated by 0x00),
         * then copy QTYPE (2 bytes) and QCLASS (2 bytes).
         */
        int q_start = 12; /* question starts after 12-byte header */
        int q_offset = q_start;

        /* Walk the QNAME labels */
        while (q_offset < len && rx_buf[q_offset] != 0x00) {
            uint8_t label_len = rx_buf[q_offset];
            if (label_len > 63 || q_offset + 1 + label_len > len) {
                break; /* invalid label length */
            }
            q_offset += 1 + label_len; /* label length + label */
        }
        if (q_offset >= len || rx_buf[q_offset] != 0x00) {
            continue; /* malformed */
        }
        q_offset++; /* skip the terminating 0x00 */
        q_offset += 4; /* skip QTYPE (2) + QCLASS (2) */

        int question_len = q_offset - q_start;
        if (offset + question_len > (int)sizeof(tx_buf) - 16) {
            continue; /* would overflow */
        }
        memcpy(&tx_buf[offset], &rx_buf[q_start], question_len);
        offset += question_len;

        /*
         * Answer section: A record pointing to 192.168.4.1
         *   - Name: pointer to offset 0x0C (the QNAME in the question)
         *   - Type: A (0x0001)
         *   - Class: IN (0x0001)
         *   - TTL: 60 seconds
         *   - RDLENGTH: 4
         *   - RDATA: 192.168.4.1
         */
        tx_buf[offset++] = 0xC0; /* name pointer */
        tx_buf[offset++] = 0x0C;

        tx_buf[offset++] = 0x00; /* type A */
        tx_buf[offset++] = 0x01;

        tx_buf[offset++] = 0x00; /* class IN */
        tx_buf[offset++] = 0x01;

        tx_buf[offset++] = 0x00; /* TTL = 60 */
        tx_buf[offset++] = 0x00;
        tx_buf[offset++] = 0x00;
        tx_buf[offset++] = 0x3C;

        tx_buf[offset++] = 0x00; /* RDLENGTH = 4 */
        tx_buf[offset++] = 0x04;

        tx_buf[offset++] = 192;  /* RDATA: 192.168.4.1 */
        tx_buf[offset++] = 168;
        tx_buf[offset++] = 4;
        tx_buf[offset++] = 1;

        sendto(sock, tx_buf, offset, 0,
               (struct sockaddr *)&client_addr, addr_len);
    }

    /* Unreachable, but good practice */
    close(sock);
    vTaskDelete(NULL);
}

/* ── Main WiFi task ────────────────────────────────────────────────── */

static void wifi_task(void *pvParameters)
{
    (void)pvParameters;

    /* Initialise networking stack */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Register event handlers */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    /* Read credentials from NVS */
    char ssid[33] = {0};
    char pass[65] = {0};
    nvs_config_get_string(NVS_KEY_WIFI_SSID, ssid, sizeof(ssid), "");
    nvs_config_get_string(NVS_KEY_WIFI_PASS, pass, sizeof(pass), "");

    /* No SSID configured -> go straight to AP mode */
    if (strlen(ssid) == 0) {
        ESP_LOGW(TAG, "No WiFi SSID configured, starting AP mode for setup");
        start_ap_mode();
        xTaskCreate(dns_task, "dns", DNS_TASK_STACK_SIZE, NULL,
                    DNS_TASK_PRIORITY, NULL);
        vTaskDelete(NULL);
        return;
    }

    /* Configure and start STA mode */
    wifi_config_t sta_config = {0};
    strncpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
    strncpy((char *)sta_config.sta.password, pass, sizeof(sta_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to \"%s\"...", ssid);
    esp_wifi_connect();

    /* Wait up to 15 s for connection */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT,
                                           pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected successfully");
    } else {
        ESP_LOGW(TAG, "WiFi connection timed out, falling back to AP mode");
        esp_wifi_stop();
        start_ap_mode();
        xTaskCreate(dns_task, "dns", DNS_TASK_STACK_SIZE, NULL,
                    DNS_TASK_PRIORITY, NULL);
    }

    /* Task work is done; event handler keeps WiFi alive */
    vTaskDelete(NULL);
}

/* ── Public API ────────────────────────────────────────────────────── */

void wifi_task_start(void)
{
    s_wifi_events = xEventGroupCreate();
    xTaskCreate(wifi_task, "wifi", WIFI_TASK_STACK_SIZE, NULL,
                WIFI_TASK_PRIORITY, NULL);
}

bool wifi_is_connected(void)
{
    return s_connected;
}

int8_t wifi_get_rssi(void)
{
    if (!s_connected) return 0;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        return ap.rssi;
    }
    return 0;
}
