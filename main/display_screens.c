#include "display.h"
#include "display_screens.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"
#include "hashrate_task.h"
#include "power_task.h"
#include "result_task.h"
#include "stratum_client.h"
#include "nvs_config.h"

static const char *TAG __attribute__((unused)) = "display_scr";

// Forward declarations for screen renderers
static void screen_mining(void);
static void screen_stats(void);
static void screen_network(void);

/* ── Helpers ───────────────────────────────────────────────────────── */

static void format_difficulty(double diff, char *buf, size_t len)
{
    if (diff >= 1e12)      snprintf(buf, len, "%.2fT", diff / 1e12);
    else if (diff >= 1e9)  snprintf(buf, len, "%.2fG", diff / 1e9);
    else if (diff >= 1e6)  snprintf(buf, len, "%.2fM", diff / 1e6);
    else if (diff >= 1e3)  snprintf(buf, len, "%.2fK", diff / 1e3);
    else                   snprintf(buf, len, "%.0f", diff);
}

static void format_uptime(char *buf, size_t len)
{
    TickType_t ticks = xTaskGetTickCount();
    uint32_t secs = ticks / configTICK_RATE_HZ;
    uint32_t days = secs / 86400;
    uint32_t hours = (secs % 86400) / 3600;
    uint32_t mins = (secs % 3600) / 60;

    if (days > 0) {
        snprintf(buf, len, "%lud %luh %lum", (unsigned long)days,
                 (unsigned long)hours, (unsigned long)mins);
    } else if (hours > 0) {
        snprintf(buf, len, "%luh %lum", (unsigned long)hours, (unsigned long)mins);
    } else {
        snprintf(buf, len, "%lum", (unsigned long)mins);
    }
}

static void get_ip_string(char *buf, size_t len)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL) {
        snprintf(buf, len, "No WiFi");
        return;
    }
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
        snprintf(buf, len, IPSTR, IP2STR(&ip_info.ip));
    } else {
        snprintf(buf, len, "Connecting...");
    }
}

static const char *stratum_state_str(stratum_state_t state)
{
    switch (state) {
        case STRATUM_STATE_DISCONNECTED: return "disconnected";
        case STRATUM_STATE_CONNECTING:   return "connecting";
        case STRATUM_STATE_SUBSCRIBING:  return "subscribing";
        case STRATUM_STATE_AUTHORIZING:  return "authorizing";
        case STRATUM_STATE_CONFIGURING:  return "configuring";
        case STRATUM_STATE_MINING:       return "connected";
        case STRATUM_STATE_ERROR:        return "error";
        default:                         return "unknown";
    }
}

/* ── Header bar (common to all screens) ────────────────────────────── */

static void draw_header(const char *title)
{
    // Dark gray header bar
    display_draw_rect(0, 0, 320, 18, COLOR_DKGRAY);
    display_draw_text(4, 1, title, COLOR_WHITE, COLOR_DKGRAY);

    // Board name on the right
    const board_config_t *board = board_get_config();
    int name_len = strlen(board->name);
    int name_x = 320 - (name_len * 8) - 4;
    display_draw_text(name_x, 1, board->name, COLOR_CYAN, COLOR_DKGRAY);

    // Separator line
    display_draw_hline(0, 18, 320, COLOR_GRAY);
}

/* ── Mining Screen ─────────────────────────────────────────────────── */

static void screen_mining(void)
{
    draw_header("AsicOS");

    const hashrate_info_t *hr = hashrate_task_get_info();
    const power_status_t *pwr = power_task_get_status();
    const mining_stats_t *stats = result_task_get_stats();
    stratum_state_t stratum_state = stratum_client_get_state();
    uint32_t accepted = stratum_client_get_accepted();

    char line[160];
    int y = 24;

    // Large hashrate display
    if (hr->total_hashrate_ghs >= 1000.0f) {
        snprintf(line, sizeof(line), "%.1f TH/s", hr->total_hashrate_ghs / 1000.0f);
    } else {
        snprintf(line, sizeof(line), "%.1f GH/s", hr->total_hashrate_ghs);
    }
    // Center the hashrate text
    int hr_x = (320 - (strlen(line) * 8)) / 2;
    display_draw_text(hr_x, y, line, COLOR_GREEN, COLOR_BLACK);
    y += 24;

    // Temperature and power row
    snprintf(line, sizeof(line), "ASIC: %.0fC   VRM: %.0fC",
             pwr->chip_temp, pwr->vr_temp);
    display_draw_text(8, y, line, COLOR_WHITE, COLOR_BLACK);
    y += 18;

    // Power and efficiency
    float eff = (pwr->power_w > 0.1f) ? hr->total_hashrate_ghs / pwr->power_w : 0.0f;
    snprintf(line, sizeof(line), "Power: %.1fW  Eff: %.1f GH/W",
             pwr->power_w, eff);
    display_draw_text(8, y, line, COLOR_WHITE, COLOR_BLACK);
    y += 18;

    // Fan speed
    snprintf(line, sizeof(line), "Fan: %u rpm", pwr->fan0_rpm);
    display_draw_text(8, y, line, COLOR_WHITE, COLOR_BLACK);
    y += 22;

    // Separator
    display_draw_hline(8, y - 4, 304, COLOR_DKGRAY);

    // Pool status and shares
    snprintf(line, sizeof(line), "Pool: %s  Shares: %lu",
             stratum_state_str(stratum_state), (unsigned long)accepted);
    uint16_t pool_color = (stratum_state == STRATUM_STATE_MINING) ? COLOR_GREEN : COLOR_YELLOW;
    display_draw_text(8, y, line, pool_color, COLOR_BLACK);
    y += 18;

    // Best difficulty
    char diff_str[16];
    format_difficulty(stats->best_difficulty, diff_str, sizeof(diff_str));
    snprintf(line, sizeof(line), "Best: %s", diff_str);
    display_draw_text(8, y, line, COLOR_WHITE, COLOR_BLACK);
    y += 18;

    // IP address
    char ip_str[20];
    get_ip_string(ip_str, sizeof(ip_str));
    display_draw_text(8, y, ip_str, COLOR_GRAY, COLOR_BLACK);
}

/* ── Stats Screen ──────────────────────────────────────────────────── */

static void screen_stats(void)
{
    draw_header("MINING STATS");

    const mining_stats_t *stats = result_task_get_stats();
    uint32_t accepted = stratum_client_get_accepted();
    uint32_t rejected = stratum_client_get_rejected();
    double pool_diff = stratum_client_get_current_difficulty();
    const board_config_t *board = board_get_config();

    char line[160];
    char diff_str[16];
    int y = 24;

    // Accepted
    snprintf(line, sizeof(line), "Accepted:   %lu", (unsigned long)accepted);
    display_draw_text(8, y, line, COLOR_GREEN, COLOR_BLACK);
    y += 16;

    // Rejected
    snprintf(line, sizeof(line), "Rejected:   %lu", (unsigned long)rejected);
    display_draw_text(8, y, line,
                      rejected > 0 ? COLOR_RED : COLOR_WHITE, COLOR_BLACK);
    y += 16;

    // Best difficulty
    format_difficulty(stats->best_difficulty, diff_str, sizeof(diff_str));
    snprintf(line, sizeof(line), "Best Diff:  %s", diff_str);
    display_draw_text(8, y, line, COLOR_WHITE, COLOR_BLACK);
    y += 16;

    // Pool difficulty
    format_difficulty(pool_diff, diff_str, sizeof(diff_str));
    snprintf(line, sizeof(line), "Pool Diff:  %s", diff_str);
    display_draw_text(8, y, line, COLOR_WHITE, COLOR_BLACK);
    y += 16;

    // Reject rate
    float reject_rate = (accepted + rejected > 0)
                        ? (100.0f * rejected / (accepted + rejected))
                        : 0.0f;
    snprintf(line, sizeof(line), "Reject Rate: %.2f%%", reject_rate);
    display_draw_text(8, y, line, COLOR_WHITE, COLOR_BLACK);
    y += 16;

    // Uptime
    char uptime_str[32];
    format_uptime(uptime_str, sizeof(uptime_str));
    snprintf(line, sizeof(line), "Uptime:     %s", uptime_str);
    display_draw_text(8, y, line, COLOR_WHITE, COLOR_BLACK);
    y += 16;

    // Frequency
    uint16_t freq = nvs_config_get_u16(NVS_KEY_ASIC_FREQ, board->freq_default);
    snprintf(line, sizeof(line), "Frequency:  %u MHz", freq);
    display_draw_text(8, y, line, COLOR_WHITE, COLOR_BLACK);
    y += 16;

    // Voltage
    uint16_t voltage = nvs_config_get_u16(NVS_KEY_ASIC_VOLTAGE, board->voltage_default);
    snprintf(line, sizeof(line), "Voltage:    %u mV", voltage);
    display_draw_text(8, y, line, COLOR_WHITE, COLOR_BLACK);
    y += 16;

    // Free heap
    uint32_t heap = esp_get_free_heap_size();
    snprintf(line, sizeof(line), "Free Heap:  %lu KB", (unsigned long)(heap / 1024));
    display_draw_text(8, y, line, COLOR_WHITE, COLOR_BLACK);
}

/* ── Network Screen ────────────────────────────────────────────────── */

static void screen_network(void)
{
    draw_header("NETWORK");

    char line[160];
    int y = 24;

    // WiFi SSID
    char ssid[33];
    nvs_config_get_string(NVS_KEY_WIFI_SSID, ssid, sizeof(ssid), "");
    snprintf(line, sizeof(line), "WiFi: %s", ssid[0] ? ssid : "(not set)");
    display_draw_text(8, y, line, COLOR_WHITE, COLOR_BLACK);
    y += 18;

    // IP address
    char ip_str[20];
    get_ip_string(ip_str, sizeof(ip_str));
    snprintf(line, sizeof(line), "IP:   %s", ip_str);
    display_draw_text(8, y, line, COLOR_WHITE, COLOR_BLACK);
    y += 22;

    // Separator
    display_draw_hline(8, y - 4, 304, COLOR_DKGRAY);

    // Pool URL
    char pool_url[128];
    nvs_config_get_string(NVS_KEY_POOL_URL, pool_url, sizeof(pool_url), "");
    uint16_t pool_port = nvs_config_get_u16(NVS_KEY_POOL_PORT, DEFAULT_POOL_PORT);
    snprintf(line, sizeof(line), "Pool: %s:%u", pool_url, pool_port);
    // Truncate if too long for display (40 chars max at 8px each = 320px)
    if (strlen(line) > 38) {
        line[38] = '\0';
    }
    display_draw_text(8, y, line, COLOR_CYAN, COLOR_BLACK);
    y += 18;

    // Pool user (truncated)
    char pool_user[128];
    nvs_config_get_string(NVS_KEY_POOL_USER, pool_user, sizeof(pool_user), "");
    snprintf(line, sizeof(line), "User: %s", pool_user);
    if (strlen(line) > 38) {
        line[35] = '.';
        line[36] = '.';
        line[37] = '.';
        line[38] = '\0';
    }
    display_draw_text(8, y, line, COLOR_WHITE, COLOR_BLACK);
    y += 22;

    // Separator
    display_draw_hline(8, y - 4, 304, COLOR_DKGRAY);

    // Stratum connection state
    stratum_state_t state = stratum_client_get_state();
    snprintf(line, sizeof(line), "Stratum: %s", stratum_state_str(state));
    uint16_t state_color = (state == STRATUM_STATE_MINING) ? COLOR_GREEN : COLOR_YELLOW;
    display_draw_text(8, y, line, state_color, COLOR_BLACK);
    y += 18;

    // Web UI URL
    if (ip_str[0] >= '0' && ip_str[0] <= '9') {
        snprintf(line, sizeof(line), "Web: http://%s", ip_str);
        display_draw_text(8, y, line, COLOR_GRAY, COLOR_BLACK);
    }
}

/* ── Public render callback (passed to display_task_start) ─────────── */

void display_render_screen(display_screen_t screen)
{
    switch (screen) {
        case DISPLAY_SCREEN_MINING:
            screen_mining();
            break;
        case DISPLAY_SCREEN_STATS:
            screen_stats();
            break;
        case DISPLAY_SCREEN_NETWORK:
            screen_network();
            break;
        default:
            break;
    }
}
