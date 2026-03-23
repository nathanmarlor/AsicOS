# AsicOS Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a complete ESP32-S3 firmware ("AsicOS") that controls BM1370 ASIC miners for Bitcoin SHA-256 mining, with dual-mode UI (simple/advanced), auto-tuning, self-test, and remote access monetisation.

**Architecture:** FreeRTOS-based task system on ESP-IDF v5.3. UART-driven ASIC communication layer, Stratum v1 protocol client with version rolling (AsicBoost), PID thermal management, and a Vue 3 web UI served from SPIFFS. Remote access via MQTT tunnelling to a cloud relay. Auto-tuner sweeps frequency/voltage combinations measuring hashrate and power to recommend optimal overclocking profiles.

**Tech Stack:** C (ESP-IDF 5.3, FreeRTOS), Vue 3 + Vite (web UI, built to SPIFFS), MQTT (remote access), TLS 1.3, NVS for persistent config, CMake build system.

---

## Scope & Phasing

This plan is broken into **7 phases**, each producing working, testable software:

| Phase | What it delivers | Dependency |
|-------|-----------------|------------|
| 1 - Foundation | Build system, board abstraction, UART driver, NVS config | None |
| 2 - ASIC Driver | BM1370 init, enumeration, job send/receive, nonce validation | Phase 1 |
| 3 - Stratum Client | Pool connection, job construction, share submission | Phase 2 |
| 4 - Power & Thermal | Voltage/frequency control, temp monitoring, PID fan, overheat protection | Phase 2 |
| 5 - Auto-Tuner & Self-Test | Frequency/voltage sweep, stability scoring, POST diagnostics | Phases 3 & 4 |
| 6 - Web UI | Vue 3 SPA with simple/advanced modes, REST API, WebSocket logs | Phase 3 |
| 7 - Remote Access & Monetisation | MQTT tunnel, cloud relay design, licence system | Phase 6 |

---

## File Structure

```
AsicOS/
├── CMakeLists.txt                          # Top-level build, board selection via $BOARD
├── partitions.csv                          # 16MB flash layout
├── sdkconfig.defaults                      # ESP-IDF Kconfig defaults
├── components/
│   ├── asic_driver/
│   │   ├── CMakeLists.txt
│   │   ├── include/
│   │   │   ├── asic.h                      # Abstract ASIC interface
│   │   │   ├── bm1370.h                    # BM1370-specific implementation
│   │   │   ├── asic_packet.h               # Packet framing (0x55/0xAA, CRC5/16)
│   │   │   └── asic_result.h               # Nonce result struct
│   │   ├── asic.c                          # Base ASIC: PLL calc, difficulty mask, send/recv
│   │   ├── bm1370.c                        # BM1370 init, job ID mapping, register map
│   │   └── asic_packet.c                   # Packet encode/decode, CRC
│   ├── serial/
│   │   ├── CMakeLists.txt
│   │   ├── include/serial.h                # UART abstraction
│   │   └── serial.c                        # UART1 driver, mutex-protected TX/RX
│   ├── stratum/
│   │   ├── CMakeLists.txt
│   │   ├── include/
│   │   │   ├── stratum_api.h               # JSON-RPC message encode/decode
│   │   │   ├── stratum_client.h            # Connection lifecycle, reconnect
│   │   │   ├── stratum_job.h               # Job struct, merkle root calc
│   │   │   └── stratum_transport.h         # TCP/TLS socket abstraction
│   │   ├── stratum_api.c                   # mining.subscribe/authorize/submit
│   │   ├── stratum_client.c                # Stratum state machine, dual pool
│   │   ├── stratum_job.c                   # Coinbase construction, merkle, double-SHA256
│   │   └── stratum_transport.c             # mbedTLS socket with cert bundle
│   ├── power/
│   │   ├── CMakeLists.txt
│   │   ├── include/
│   │   │   ├── voltage_regulator.h         # VR abstraction (I2C PMBus)
│   │   │   ├── temp_sensor.h               # Temp sensor abstraction
│   │   │   ├── fan_controller.h            # Fan PWM abstraction
│   │   │   └── pid.h                       # PID controller
│   │   ├── voltage_regulator.c             # TPS53647 PMBus driver
│   │   ├── temp_sensor.c                   # TMP1075 I2C driver + ASIC internal temp
│   │   ├── fan_controller.c                # EMC2302 I2C driver
│   │   └── pid.c                           # PID algorithm
│   ├── mining/
│   │   ├── CMakeLists.txt
│   │   ├── include/
│   │   │   ├── mining.h                    # Block header, nonce test, difficulty calc
│   │   │   └── sha256.h                    # SHA-256 (hardware-accelerated where possible)
│   │   ├── mining.c                        # Double-SHA256, difficulty comparison
│   │   └── sha256.c                        # SHA-256 implementation
│   ├── nvs_config/
│   │   ├── CMakeLists.txt
│   │   ├── include/nvs_config.h            # Typed config get/set API
│   │   └── nvs_config.c                    # NVS wrapper, defaults, migration
│   ├── auto_tuner/
│   │   ├── CMakeLists.txt
│   │   ├── include/auto_tuner.h            # Tuner API: start sweep, get results
│   │   └── auto_tuner.c                    # Freq/voltage sweep, stability scoring
│   ├── self_test/
│   │   ├── CMakeLists.txt
│   │   ├── include/self_test.h             # POST API: run all checks, get report
│   │   └── self_test.c                     # UART loopback, ASIC ping, temp read, VR check
│   └── remote_access/
│       ├── CMakeLists.txt
│       ├── include/
│       │   ├── remote_access.h             # Remote tunnel API
│       │   └── licence.h                   # Licence validation
│       ├── remote_access.c                 # MQTT tunnel client
│       └── licence.c                       # HMAC-SHA256 licence check, grace period
├── main/
│   ├── CMakeLists.txt
│   ├── main.c                              # app_main: init, task creation, boot sequence
│   ├── board.h                             # Board abstraction (pin map, limits, defaults)
│   ├── boards/
│   │   └── asicos_v1.c                     # First board definition
│   ├── tasks/
│   │   ├── mining_task.c                   # Job creation + dispatch to ASICs
│   │   ├── result_task.c                   # ASIC result processing + share submission
│   │   ├── power_task.c                    # Thermal monitoring, fan PID, fault detection
│   │   ├── hashrate_task.c                 # HW hash counter polling, smoothing
│   │   ├── wifi_task.c                     # WiFi STA/AP, captive portal
│   │   ├── tuner_task.c                    # Auto-tuner orchestration
│   │   └── remote_task.c                   # Remote access MQTT loop
│   ├── http_server/
│   │   ├── http_server.c                   # REST API routes + SPIFFS static serving
│   │   ├── api_system.c                    # /api/system/* endpoints
│   │   ├── api_mining.c                    # /api/mining/* endpoints
│   │   ├── api_tuner.c                     # /api/tuner/* endpoints
│   │   ├── api_remote.c                    # /api/remote/* endpoints
│   │   └── ws_handler.c                    # WebSocket log streaming
│   └── Kconfig.projbuild                   # Menu config for board selection
├── ui/                                     # Vue 3 SPA (built -> SPIFFS www partition)
│   ├── package.json
│   ├── vite.config.ts
│   ├── tsconfig.json
│   ├── index.html
│   └── src/
│       ├── main.ts
│       ├── App.vue
│       ├── router.ts
│       ├── stores/
│       │   ├── system.ts                   # System info store (Pinia)
│       │   ├── mining.ts                   # Mining stats store
│       │   └── tuner.ts                    # Auto-tuner store
│       ├── composables/
│       │   ├── useApi.ts                   # Fetch wrapper for REST API
│       │   └── useWebSocket.ts             # WS connection for live logs
│       ├── views/
│       │   ├── SimpleView.vue              # Simple mode: hashrate dial, BTC earned, temp
│       │   ├── AdvancedView.vue            # Advanced mode: per-domain stats, ASIC detail
│       │   ├── TunerView.vue              # Auto-tuner results + controls
│       │   ├── SettingsView.vue            # Pool config, WiFi, voltage/freq
│       │   └── RemoteView.vue             # Remote access config + licence
│       └── components/
│           ├── HashrateDial.vue            # Animated gauge for simple mode
│           ├── TemperatureBar.vue          # Temp indicator with colour coding
│           ├── AsicGrid.vue                # Per-chip status grid (advanced)
│           ├── PoolStatus.vue              # Pool connection status
│           ├── LogConsole.vue              # WebSocket log viewer
│           └── ModeToggle.vue              # Simple/Advanced switch
├── test/                                   # Host-side unit tests (Unity + CMock)
│   ├── CMakeLists.txt
│   ├── test_asic_packet.c                  # Packet framing, CRC
│   ├── test_mining.c                       # SHA-256, difficulty, nonce validation
│   ├── test_stratum_api.c                  # JSON-RPC encode/decode
│   ├── test_stratum_job.c                  # Merkle root, coinbase construction
│   ├── test_pid.c                          # PID controller
│   ├── test_nvs_config.c                   # Config defaults, get/set
│   ├── test_auto_tuner.c                   # Scoring algorithm
│   └── test_licence.c                      # Licence validation
└── docs/
    ├── superpowers/plans/                  # This plan
    ├── remote-access-architecture.md       # Cloud relay design doc
    └── api-reference.md                    # REST API docs
```

---

## Phase 1: Foundation

### Task 1.1: Build System & Partition Table

**Files:**
- Create: `CMakeLists.txt`
- Create: `partitions.csv`
- Create: `sdkconfig.defaults`
- Create: `main/CMakeLists.txt`
- Create: `main/main.c`
- Create: `main/Kconfig.projbuild`

- [ ] **Step 1: Create top-level CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "components/serial"
    "components/asic_driver"
    "components/stratum"
    "components/power"
    "components/mining"
    "components/nvs_config"
    "components/auto_tuner"
    "components/self_test"
    "components/remote_access"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(AsicOS)
```

- [ ] **Step 2: Create partition table**

```csv
# Name,    Type, SubType,  Offset,   Size,     Flags
nvs,       data, nvs,      0x9000,   0x6000,
phy_init,  data, phy,      0xf000,   0x1000,
factory,   app,  factory,  0x10000,  0x400000,
www,       data, spiffs,   0x410000, 0x300000,
ota_0,     app,  ota_0,    0x710000, 0x400000,
ota_1,     app,  ota_1,    0xB10000, 0x400000,
otadata,   data, ota,      0xF10000, 0x2000,
coredump,  data, coredump, 0xF12000, 0x10000,
```

- [ ] **Step 3: Create sdkconfig.defaults**

```ini
# Target
CONFIG_IDF_TARGET="esp32s3"

# CPU
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y

# PSRAM
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_40M=y
CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y

# Partition
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y

# LWIP
CONFIG_LWIP_MAX_SOCKETS=16

# TLS
CONFIG_MBEDTLS_TLS_ENABLED=y
CONFIG_MBEDTLS_SSL_PROTO_TLS1_2=y
CONFIG_MBEDTLS_SSL_PROTO_TLS1_3=y
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y

# Bluetooth off
CONFIG_BT_ENABLED=n

# Watchdog
CONFIG_ESP_TASK_WDT_INIT=n

# SPIFFS
CONFIG_SPIFFS_MAX_PARTITIONS=3

# Stack
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
```

- [ ] **Step 4: Create main/Kconfig.projbuild**

```kconfig
menu "AsicOS Configuration"

    choice ASICOS_BOARD
        prompt "Board variant"
        default ASICOS_BOARD_V1
        help
            Select the hardware board variant.

        config ASICOS_BOARD_V1
            bool "AsicOS V1"
    endchoice

endmenu
```

- [ ] **Step 5: Create minimal main.c**

```c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "asicos";

void app_main(void)
{
    ESP_LOGI(TAG, "AsicOS starting...");
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
}
```

- [ ] **Step 6: Create main/CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES "nvs_flash" "esp_wifi" "esp_http_server" "spiffs"
)
```

- [ ] **Step 7: Verify build compiles**

Run: `idf.py set-target esp32s3 && idf.py build`
Expected: Clean build, binary output in `build/`

- [ ] **Step 8: Commit**

```bash
git add CMakeLists.txt partitions.csv sdkconfig.defaults main/
git commit -m "feat: foundation build system, partition table, minimal main"
```

---

### Task 1.2: Serial/UART Driver

**Files:**
- Create: `components/serial/CMakeLists.txt`
- Create: `components/serial/include/serial.h`
- Create: `components/serial/serial.c`

- [ ] **Step 1: Create serial component CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "serial.c"
    INCLUDE_DIRS "include"
    REQUIRES "driver"
)
```

- [ ] **Step 2: Create serial.h**

```c
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "driver/uart.h"

typedef struct {
    uart_port_t port;
    int tx_pin;
    int rx_pin;
    int baud_rate;
} serial_config_t;

/**
 * Initialise UART with mutex-protected access.
 * Returns ESP_OK on success.
 */
esp_err_t serial_init(const serial_config_t *config);

/**
 * Send data over UART. Thread-safe (mutex protected).
 * Returns number of bytes written, or -1 on error.
 */
int serial_tx(const uint8_t *data, size_t len);

/**
 * Receive data from UART with timeout.
 * Returns number of bytes read, or -1 on timeout/error.
 */
int serial_rx(uint8_t *buf, size_t len, uint32_t timeout_ms);

/**
 * Change baud rate on the fly.
 */
esp_err_t serial_set_baud(int baud_rate);

/**
 * Flush RX buffer.
 */
void serial_flush_rx(void);
```

- [ ] **Step 3: Implement serial.c**

```c
#include "serial.h"
#include "esp_log.h"
#include "freertos/semphr.h"

static const char *TAG = "serial";
static uart_port_t s_port;
static SemaphoreHandle_t s_tx_mutex;

esp_err_t serial_init(const serial_config_t *config)
{
    s_port = config->port;

    uart_config_t uart_cfg = {
        .baud_rate = config->baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(s_port, 1024, 0, 0, NULL, 0);
    if (err != ESP_OK) return err;

    err = uart_param_config(s_port, &uart_cfg);
    if (err != ESP_OK) return err;

    err = uart_set_pin(s_port, config->tx_pin, config->rx_pin,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) return err;

    s_tx_mutex = xSemaphoreCreateMutex();
    if (!s_tx_mutex) return ESP_ERR_NO_MEM;

    ESP_LOGI(TAG, "UART%d init: baud=%d tx=%d rx=%d",
             s_port, config->baud_rate, config->tx_pin, config->rx_pin);
    return ESP_OK;
}

int serial_tx(const uint8_t *data, size_t len)
{
    if (xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "TX mutex timeout");
        return -1;
    }
    int written = uart_write_bytes(s_port, data, len);
    xSemaphoreGive(s_tx_mutex);
    return written;
}

int serial_rx(uint8_t *buf, size_t len, uint32_t timeout_ms)
{
    return uart_read_bytes(s_port, buf, len, pdMS_TO_TICKS(timeout_ms));
}

esp_err_t serial_set_baud(int baud_rate)
{
    if (xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = uart_set_baudrate(s_port, baud_rate);
    xSemaphoreGive(s_tx_mutex);
    ESP_LOGI(TAG, "Baud rate changed to %d", baud_rate);
    return err;
}

void serial_flush_rx(void)
{
    uart_flush_input(s_port);
}
```

- [ ] **Step 4: Verify component builds**

Run: `idf.py build`
Expected: Clean build with serial component included

- [ ] **Step 5: Commit**

```bash
git add components/serial/
git commit -m "feat: UART serial driver with mutex-protected TX"
```

---

### Task 1.3: NVS Config System

**Files:**
- Create: `components/nvs_config/CMakeLists.txt`
- Create: `components/nvs_config/include/nvs_config.h`
- Create: `components/nvs_config/nvs_config.c`
- Create: `test/test_nvs_config.c`

- [ ] **Step 1: Write failing test for config defaults**

```c
// test/test_nvs_config.c
#include "unity.h"
#include "nvs_config.h"

// These tests run on host with mocked NVS, or on target with real NVS

void test_get_string_returns_default_when_unset(void)
{
    char buf[64];
    nvs_config_get_string("nonexistent_key", buf, sizeof(buf), "default_val");
    TEST_ASSERT_EQUAL_STRING("default_val", buf);
}

void test_get_u16_returns_default_when_unset(void)
{
    uint16_t val = nvs_config_get_u16("nonexistent_key", 1234);
    TEST_ASSERT_EQUAL_UINT16(1234, val);
}

void test_set_and_get_string(void)
{
    nvs_config_set_string("test_key", "hello");
    char buf[64];
    nvs_config_get_string("test_key", buf, sizeof(buf), "nope");
    TEST_ASSERT_EQUAL_STRING("hello", buf);
}
```

- [ ] **Step 2: Create nvs_config.h**

```c
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

// Config keys (centralised to prevent typos)
#define NVS_KEY_WIFI_SSID       "wifi_ssid"
#define NVS_KEY_WIFI_PASS       "wifi_pass"
#define NVS_KEY_POOL_URL        "pool_url"
#define NVS_KEY_POOL_PORT       "pool_port"
#define NVS_KEY_POOL_USER       "pool_user"
#define NVS_KEY_POOL_PASS       "pool_pass"
#define NVS_KEY_ASIC_FREQ       "asic_freq"
#define NVS_KEY_ASIC_VOLTAGE    "asic_voltage"
#define NVS_KEY_FAN_TARGET_TEMP "fan_target"
#define NVS_KEY_OVERHEAT_TEMP   "overheat"
#define NVS_KEY_BEST_DIFF       "best_diff"
#define NVS_KEY_UI_MODE         "ui_mode"
#define NVS_KEY_LICENCE         "licence"
#define NVS_KEY_DEVICE_ID       "device_id"
#define NVS_KEY_TUNER_PROFILE   "tuner_prof"
#define NVS_KEY_POOL2_URL       "pool2_url"
#define NVS_KEY_POOL2_PORT      "pool2_port"
#define NVS_KEY_POOL2_USER      "pool2_user"

// Defaults
#define DEFAULT_ASIC_FREQ       490
#define DEFAULT_ASIC_VOLTAGE    1250
#define DEFAULT_FAN_TARGET      55
#define DEFAULT_OVERHEAT_TEMP   70
#define DEFAULT_POOL_PORT       3333

esp_err_t nvs_config_init(void);

void nvs_config_get_string(const char *key, char *out, size_t out_len,
                           const char *default_val);
esp_err_t nvs_config_set_string(const char *key, const char *value);

uint16_t nvs_config_get_u16(const char *key, uint16_t default_val);
esp_err_t nvs_config_set_u16(const char *key, uint16_t value);

uint64_t nvs_config_get_u64(const char *key, uint64_t default_val);
esp_err_t nvs_config_set_u64(const char *key, uint64_t value);
```

- [ ] **Step 3: Implement nvs_config.c**

```c
#include "nvs_config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "nvs_config";
static const char *NVS_NAMESPACE = "asicos";

esp_err_t nvs_config_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition issue, erasing...");
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    return err;
}

void nvs_config_get_string(const char *key, char *out, size_t out_len,
                           const char *default_val)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        strncpy(out, default_val, out_len);
        out[out_len - 1] = '\0';
        return;
    }
    size_t len = out_len;
    if (nvs_get_str(handle, key, out, &len) != ESP_OK) {
        strncpy(out, default_val, out_len);
        out[out_len - 1] = '\0';
    }
    nvs_close(handle);
}

esp_err_t nvs_config_set_string(const char *key, const char *value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, key, value);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

uint16_t nvs_config_get_u16(const char *key, uint16_t default_val)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return default_val;
    }
    uint16_t val = default_val;
    nvs_get_u16(handle, key, &val);
    nvs_close(handle);
    return val;
}

esp_err_t nvs_config_set_u16(const char *key, uint16_t value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u16(handle, key, value);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

uint64_t nvs_config_get_u64(const char *key, uint64_t default_val)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return default_val;
    }
    uint64_t val = default_val;
    nvs_get_u64(handle, key, &val);
    nvs_close(handle);
    return val;
}

esp_err_t nvs_config_set_u64(const char *key, uint64_t value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u64(handle, key, value);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
```

- [ ] **Step 4: Create component CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "nvs_config.c"
    INCLUDE_DIRS "include"
    REQUIRES "nvs_flash"
)
```

- [ ] **Step 5: Verify build**

Run: `idf.py build`
Expected: Clean build

- [ ] **Step 6: Commit**

```bash
git add components/nvs_config/ test/test_nvs_config.c
git commit -m "feat: NVS config system with typed get/set and defaults"
```

---

### Task 1.4: Board Abstraction

**Files:**
- Create: `main/board.h`
- Create: `main/boards/asicos_v1.c`

- [ ] **Step 1: Create board.h**

```c
#pragma once

#include <stdint.h>

typedef struct {
    // Identity
    const char *name;
    const char *asic_model;

    // UART pins
    int uart_tx_pin;
    int uart_rx_pin;

    // I2C pins
    int i2c_sda_pin;
    int i2c_scl_pin;

    // Control pins
    int asic_reset_pin;
    int buck_enable_pin;
    int ldo_enable_pin;
    int led_pin;

    // ASIC limits
    uint16_t freq_default;
    uint16_t freq_min;
    uint16_t freq_max;
    uint16_t voltage_default;
    uint16_t voltage_min;
    uint16_t voltage_max;

    // Thermal
    uint8_t fan_target_temp;
    uint8_t overheat_temp;
    uint8_t vr_target_temp;

    // Timing
    uint16_t job_interval_ms;

    // Chip info
    uint8_t expected_chip_count;
    uint16_t small_core_count;
} board_config_t;

const board_config_t *board_get_config(void);
```

- [ ] **Step 2: Create asicos_v1.c**

```c
#include "board.h"

static const board_config_t s_board = {
    .name = "AsicOS V1",
    .asic_model = "BM1370",

    .uart_tx_pin = 17,
    .uart_rx_pin = 18,

    .i2c_sda_pin = 47,
    .i2c_scl_pin = 48,

    .asic_reset_pin = 1,
    .buck_enable_pin = 10,
    .ldo_enable_pin = 11,
    .led_pin = 2,

    .freq_default = 490,
    .freq_min = 200,
    .freq_max = 800,
    .voltage_default = 1250,
    .voltage_min = 1000,
    .voltage_max = 1400,

    .fan_target_temp = 55,
    .overheat_temp = 70,
    .vr_target_temp = 65,

    .job_interval_ms = 1200,

    .expected_chip_count = 4,
    .small_core_count = 2040,
};

const board_config_t *board_get_config(void)
{
    return &s_board;
}
```

- [ ] **Step 3: Update main/CMakeLists.txt to include board source**

```cmake
idf_component_register(
    SRCS "main.c"
          "boards/asicos_v1.c"
    INCLUDE_DIRS "."
    REQUIRES "nvs_flash" "esp_wifi" "esp_http_server" "spiffs" "nvs_config" "serial"
)
```

- [ ] **Step 4: Update main.c to use board config**

```c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_config.h"
#include "board.h"

static const char *TAG = "asicos";

void app_main(void)
{
    ESP_LOGI(TAG, "AsicOS starting...");

    // Init NVS
    ESP_ERROR_CHECK(nvs_config_init());

    // Load board config
    const board_config_t *board = board_get_config();
    ESP_LOGI(TAG, "Board: %s, ASIC: %s", board->name, board->asic_model);
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
}
```

- [ ] **Step 5: Verify build**

Run: `idf.py build`
Expected: Clean build

- [ ] **Step 6: Commit**

```bash
git add main/
git commit -m "feat: board abstraction with AsicOS V1 pin map and limits"
```

---

## Phase 2: ASIC Driver

### Task 2.1: Packet Framing & CRC

**Files:**
- Create: `components/asic_driver/CMakeLists.txt`
- Create: `components/asic_driver/include/asic_packet.h`
- Create: `components/asic_driver/asic_packet.c`
- Create: `test/test_asic_packet.c`

- [ ] **Step 1: Write failing tests for CRC and packet framing**

```c
// test/test_asic_packet.c
#include "unity.h"
#include "asic_packet.h"

void test_crc5_known_value(void)
{
    // BM1370 uses CRC5 for command packets
    uint8_t data[] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x08};
    uint8_t crc = asic_crc5(data, 6);
    // CRC5 should produce a valid 5-bit value
    TEST_ASSERT_LESS_THAN(32, crc);
}

void test_crc16_known_value(void)
{
    uint8_t data[] = {0x00, 0x01, 0x02, 0x03};
    uint16_t crc = asic_crc16(data, 4);
    TEST_ASSERT_NOT_EQUAL(0, crc);
}

void test_build_cmd_packet(void)
{
    uint8_t buf[12];
    int len = asic_build_cmd(buf, sizeof(buf), ASIC_CMD_WRITE,
                             ASIC_GROUP_ALL, 0x08,
                             (uint8_t[]){0x40, 0xA0, 0x02, 0x41}, 4);
    TEST_ASSERT_EQUAL(0x55, buf[0]);
    TEST_ASSERT_EQUAL(0xAA, buf[1]);
    TEST_ASSERT_GREATER_THAN(4, len);
}

void test_build_job_packet_has_crc16(void)
{
    // Job packets use CRC16 not CRC5
    uint8_t job_data[86] = {0}; // Minimal job
    uint8_t buf[128];
    int len = asic_build_job(buf, sizeof(buf), job_data, 86);
    TEST_ASSERT_GREATER_THAN(88, len); // header + data + crc16
}
```

- [ ] **Step 2: Create asic_packet.h**

```c
#pragma once

#include <stdint.h>
#include <stddef.h>

// Preamble
#define ASIC_PREAMBLE_1     0x55
#define ASIC_PREAMBLE_2     0xAA

// Header type bits
#define ASIC_TYPE_JOB       0x20
#define ASIC_TYPE_CMD       0x40

// Group bits
#define ASIC_GROUP_SINGLE   0x00
#define ASIC_GROUP_ALL      0x10

// Command types
#define ASIC_CMD_SETADDR    0x00
#define ASIC_CMD_WRITE      0x01
#define ASIC_CMD_READ       0x02
#define ASIC_CMD_INACTIVE   0x03

// Response sizes
#define ASIC_RESP_SIZE      11

// CRC
uint8_t  asic_crc5(const uint8_t *data, size_t len);
uint16_t asic_crc16(const uint8_t *data, size_t len);

// Build a command packet. Returns total packet length.
int asic_build_cmd(uint8_t *buf, size_t buf_len,
                   uint8_t cmd, uint8_t group,
                   uint8_t reg_addr,
                   const uint8_t *data, size_t data_len);

// Build a job packet. Returns total packet length.
int asic_build_job(uint8_t *buf, size_t buf_len,
                   const uint8_t *job_data, size_t job_len);

// Parse response type. Returns 1 if nonce result, 0 if register read.
int asic_is_nonce_response(const uint8_t *resp);
```

- [ ] **Step 3: Implement asic_packet.c**

```c
#include "asic_packet.h"
#include <string.h>

uint8_t asic_crc5(const uint8_t *data, size_t len)
{
    uint8_t crc = 0x1f;
    for (size_t i = 0; i < len; i++) {
        uint8_t byte = data[i];
        for (int bit = 7; bit >= 0; bit--) {
            uint8_t d_bit = (byte >> bit) & 1;
            uint8_t c4 = (crc >> 4) & 1;
            crc = ((crc << 1) | d_bit) & 0x1f;
            if (c4 != d_bit) {
                crc ^= 0x05;
            }
        }
    }
    return crc;
}

uint16_t asic_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc & 0xFFFF;
}

int asic_build_cmd(uint8_t *buf, size_t buf_len,
                   uint8_t cmd, uint8_t group,
                   uint8_t reg_addr,
                   const uint8_t *data, size_t data_len)
{
    size_t pkt_len = 6 + data_len; // preamble(2) + header + length + addr + data + crc5_in_last
    if (buf_len < pkt_len) return -1;

    uint8_t header = ASIC_TYPE_CMD | group | cmd;
    buf[0] = ASIC_PREAMBLE_1;
    buf[1] = ASIC_PREAMBLE_2;
    buf[2] = header;
    buf[3] = (uint8_t)(data_len + 2); // length: addr + data + crc
    buf[4] = reg_addr;

    if (data && data_len > 0) {
        memcpy(&buf[5], data, data_len);
    }

    // CRC5 over entire packet excluding preamble, appended to last byte
    uint8_t crc = asic_crc5(&buf[2], 3 + data_len);
    buf[5 + data_len] = crc;

    return (int)(6 + data_len);
}

int asic_build_job(uint8_t *buf, size_t buf_len,
                   const uint8_t *job_data, size_t job_len)
{
    size_t pkt_len = 4 + job_len + 2; // preamble(2) + header + len + data + crc16(2)
    if (buf_len < pkt_len) return -1;

    buf[0] = ASIC_PREAMBLE_1;
    buf[1] = ASIC_PREAMBLE_2;
    buf[2] = ASIC_TYPE_JOB | ASIC_GROUP_ALL;
    buf[3] = (uint8_t)(job_len + 2); // includes CRC16

    memcpy(&buf[4], job_data, job_len);

    uint16_t crc = asic_crc16(&buf[2], 2 + job_len);
    buf[4 + job_len] = (uint8_t)(crc >> 8);
    buf[5 + job_len] = (uint8_t)(crc & 0xFF);

    return (int)pkt_len;
}

int asic_is_nonce_response(const uint8_t *resp)
{
    // Response: [0xAA][0x55][nonce:4][midstate:1][job_id:1][version:2][crc:1]
    // If CRC byte MSB is set, it's a nonce result
    return (resp[10] & 0x80) ? 1 : 0;
}
```

- [ ] **Step 4: Create component CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "asic_packet.c" "asic.c" "bm1370.c"
    INCLUDE_DIRS "include"
    REQUIRES "serial"
)
```

- [ ] **Step 5: Verify build**

Run: `idf.py build`
Expected: Clean build

- [ ] **Step 6: Commit**

```bash
git add components/asic_driver/ test/test_asic_packet.c
git commit -m "feat: ASIC packet framing with CRC5/CRC16"
```

---

### Task 2.2: ASIC Base Driver & BM1370

**Files:**
- Create: `components/asic_driver/include/asic.h`
- Create: `components/asic_driver/include/bm1370.h`
- Create: `components/asic_driver/include/asic_result.h`
- Create: `components/asic_driver/asic.c`
- Create: `components/asic_driver/bm1370.c`

- [ ] **Step 1: Create asic_result.h**

```c
#pragma once

#include <stdint.h>

typedef struct {
    uint32_t nonce;
    uint8_t  midstate_num;
    uint8_t  job_id;
    uint16_t rolled_version;
} asic_result_t;
```

- [ ] **Step 2: Create asic.h**

```c
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "asic_result.h"

// BM1370 registers
#define ASIC_REG_CHIP_ID        0x00
#define ASIC_REG_PLL            0x08
#define ASIC_REG_VR_FREQ        0x10
#define ASIC_REG_TICKET_MASK    0x14
#define ASIC_REG_MISC_CTRL      0x18
#define ASIC_REG_FAST_UART      0x28
#define ASIC_REG_CORE_CTRL      0x3C
#define ASIC_REG_ANALOG_MUX     0x54
#define ASIC_REG_IO_DRIVE       0x58
#define ASIC_REG_NONCE_COUNT    0x90
#define ASIC_REG_VR_MASK        0xA4
#define ASIC_REG_TEMPERATURE    0xB4

// Job struct sent to ASIC
typedef struct {
    uint8_t  job_id;
    uint8_t  midstate_count;
    uint32_t starting_nonce;
    uint32_t nbits;
    uint32_t ntime;
    uint8_t  merkle_root[32];
    uint8_t  prev_block_hash[32];
    uint32_t version;
    // Metadata (not sent to ASIC, used for share submission)
    char     stratum_job_id[32];
    char     extranonce2[32];
    double   pool_diff;
    int      pool_id;
} asic_job_t;

typedef struct {
    int      chip_count;
    uint16_t current_freq;
    uint16_t current_voltage;
    float    chip_temp;
    bool     initialised;
} asic_state_t;

// Init & enumeration
esp_err_t asic_init(void);
int       asic_enumerate(void);
esp_err_t asic_set_chip_address(int chip_index, uint8_t addr);

// Configuration
esp_err_t asic_set_frequency(uint16_t freq_mhz);
esp_err_t asic_set_difficulty_mask(uint64_t difficulty);
esp_err_t asic_set_version_mask(uint32_t mask);

// Work
esp_err_t asic_send_work(const asic_job_t *job);
int       asic_receive_result(asic_result_t *result, uint32_t timeout_ms);

// Monitoring
float     asic_read_temperature(void);
uint64_t  asic_read_hash_counter(int chip_index);

// State
const asic_state_t *asic_get_state(void);

// PLL helper: calculate best PLL params for target frequency
typedef struct {
    uint8_t  refdiv;
    uint16_t fb_divider;
    uint8_t  postdiv1;
    uint8_t  postdiv2;
    float    actual_freq;
} pll_params_t;

pll_params_t asic_calc_pll(float target_freq_mhz);
```

- [ ] **Step 3: Implement asic.c (base driver)**

```c
#include "asic.h"
#include "asic_packet.h"
#include "serial.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>

static const char *TAG = "asic";
static asic_state_t s_state = {0};

pll_params_t asic_calc_pll(float target_freq_mhz)
{
    pll_params_t best = {0};
    float best_error = 999.0f;

    for (uint16_t fb = 0xA0; fb <= 0xEF; fb++) {
        for (uint8_t pd1 = 1; pd1 <= 7; pd1++) {
            for (uint8_t pd2 = 1; pd2 <= 7; pd2++) {
                float freq = 25.0f * fb / (1.0f * pd2 * pd1);
                float err = fabsf(freq - target_freq_mhz);
                if (err < best_error) {
                    best_error = err;
                    best.refdiv = 1;
                    best.fb_divider = fb;
                    best.postdiv1 = pd1;
                    best.postdiv2 = pd2;
                    best.actual_freq = freq;
                }
            }
        }
    }
    ESP_LOGI(TAG, "PLL: target=%.1f actual=%.1f fb=0x%02X pd1=%d pd2=%d",
             target_freq_mhz, best.actual_freq, best.fb_divider,
             best.postdiv1, best.postdiv2);
    return best;
}

esp_err_t asic_set_difficulty_mask(uint64_t difficulty)
{
    // Largest power-of-two <= difficulty, then mask = that - 1
    // Bits reversed per byte for the ASIC register format
    uint64_t mask = difficulty;
    mask |= mask >> 1;
    mask |= mask >> 2;
    mask |= mask >> 4;
    mask |= mask >> 8;
    mask |= mask >> 16;
    mask |= mask >> 32;
    mask = mask >> 1;

    uint8_t data[8];
    for (int i = 0; i < 8; i++) {
        uint8_t byte = (mask >> (56 - i * 8)) & 0xFF;
        // Reverse bits in byte
        byte = ((byte & 0xF0) >> 4) | ((byte & 0x0F) << 4);
        byte = ((byte & 0xCC) >> 2) | ((byte & 0x33) << 2);
        byte = ((byte & 0xAA) >> 1) | ((byte & 0x55) << 1);
        data[i] = byte;
    }

    uint8_t buf[16];
    int len = asic_build_cmd(buf, sizeof(buf), ASIC_CMD_WRITE,
                             ASIC_GROUP_ALL, ASIC_REG_TICKET_MASK, data, 8);
    if (len < 0) return ESP_FAIL;
    return (serial_tx(buf, len) == len) ? ESP_OK : ESP_FAIL;
}

esp_err_t asic_set_version_mask(uint32_t mask)
{
    uint8_t data[4] = {
        (mask >> 24) & 0xFF,
        (mask >> 16) & 0xFF,
        (mask >> 8) & 0xFF,
        mask & 0xFF
    };
    uint8_t buf[12];
    int len = asic_build_cmd(buf, sizeof(buf), ASIC_CMD_WRITE,
                             ASIC_GROUP_ALL, ASIC_REG_VR_MASK, data, 4);
    if (len < 0) return ESP_FAIL;
    return (serial_tx(buf, len) == len) ? ESP_OK : ESP_FAIL;
}

int asic_receive_result(asic_result_t *result, uint32_t timeout_ms)
{
    uint8_t resp[ASIC_RESP_SIZE];
    int read = serial_rx(resp, ASIC_RESP_SIZE, timeout_ms);
    if (read != ASIC_RESP_SIZE) return -1;

    // Validate preamble (reversed for response: 0xAA 0x55)
    if (resp[0] != 0xAA || resp[1] != 0x55) return -1;

    if (!asic_is_nonce_response(resp)) return 0; // Register read, not nonce

    result->nonce = ((uint32_t)resp[2] << 24) | ((uint32_t)resp[3] << 16) |
                    ((uint32_t)resp[4] << 8)  | resp[5];
    result->midstate_num = resp[6];
    result->job_id = resp[7];
    result->rolled_version = ((uint16_t)resp[8] << 8) | resp[9];

    return 1;
}

const asic_state_t *asic_get_state(void)
{
    return &s_state;
}
```

- [ ] **Step 4: Implement bm1370.c**

```c
#include "bm1370.h"
#include "asic.h"
#include "asic_packet.h"
#include "serial.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "bm1370";

// BM1370 chip ID signature
static const uint8_t BM1370_CHIP_ID[] = {0xAA, 0x55, 0x13, 0x70};

// BM1370-specific job ID interleaving
uint8_t bm1370_job_to_asic_id(uint8_t job_id)
{
    return (job_id * 24) & 0x7F;
}

uint8_t bm1370_asic_to_job_id(uint8_t asic_id)
{
    // Inverse mapping: precompute or search
    for (uint8_t j = 0; j < 128; j++) {
        if (bm1370_job_to_asic_id(j) == asic_id) return j;
    }
    return 0xFF; // Invalid
}

int bm1370_nonce_to_chip(uint32_t nonce, int chip_count)
{
    // Chip number encoded in nonce bits 11-16
    return (nonce >> 11) & (chip_count - 1);
}

esp_err_t bm1370_init(int expected_chips)
{
    ESP_LOGI(TAG, "Initialising BM1370 chain (expecting %d chips)...", expected_chips);

    // Flush any stale data
    serial_flush_rx();

    // Broadcast read of chip ID register
    uint8_t buf[12];
    int len = asic_build_cmd(buf, sizeof(buf), ASIC_CMD_READ,
                             ASIC_GROUP_ALL, ASIC_REG_CHIP_ID, NULL, 0);
    if (len < 0) return ESP_FAIL;

    serial_tx(buf, len);

    // Count responses
    int chip_count = 0;
    for (int i = 0; i < expected_chips + 2; i++) {
        uint8_t resp[ASIC_RESP_SIZE];
        int read = serial_rx(resp, ASIC_RESP_SIZE, 500);
        if (read != ASIC_RESP_SIZE) break;
        if (memcmp(resp, BM1370_CHIP_ID, 4) == 0) {
            chip_count++;
        }
    }

    ESP_LOGI(TAG, "Found %d BM1370 chips", chip_count);
    if (chip_count == 0) return ESP_ERR_NOT_FOUND;

    // Assign addresses
    for (int i = 0; i < chip_count; i++) {
        uint8_t addr = i * 4;
        ESP_ERROR_CHECK(asic_set_chip_address(i, addr));
    }

    // Configure version rolling mask (full 16-bit AsicBoost)
    asic_set_version_mask(0x1FFFE000);

    ESP_LOGI(TAG, "BM1370 init complete: %d chips", chip_count);
    return ESP_OK;
}

esp_err_t bm1370_set_frequency(uint16_t freq_mhz)
{
    pll_params_t pll = asic_calc_pll((float)freq_mhz);

    uint8_t data[4] = {
        pll.fb_divider & 0xFF,
        (pll.fb_divider >> 8) & 0xFF,
        (pll.postdiv1 << 4) | pll.postdiv2,
        pll.refdiv
    };

    uint8_t buf[12];
    int len = asic_build_cmd(buf, sizeof(buf), ASIC_CMD_WRITE,
                             ASIC_GROUP_ALL, ASIC_REG_PLL, data, 4);
    if (len < 0) return ESP_FAIL;

    ESP_LOGI(TAG, "Setting frequency to %d MHz (actual: %.1f)",
             freq_mhz, pll.actual_freq);
    return (serial_tx(buf, len) == len) ? ESP_OK : ESP_FAIL;
}

float bm1370_read_temperature(void)
{
    // Request temperature register
    uint8_t buf[12];
    int len = asic_build_cmd(buf, sizeof(buf), ASIC_CMD_READ,
                             ASIC_GROUP_SINGLE, ASIC_REG_TEMPERATURE, NULL, 0);
    if (len < 0) return -1.0f;
    serial_tx(buf, len);

    // Read response
    uint8_t resp[ASIC_RESP_SIZE];
    int read = serial_rx(resp, ASIC_RESP_SIZE, 1000);
    if (read != ASIC_RESP_SIZE) return -1.0f;

    // Parse temperature: (raw & 0xFFFF) * 0.171342 - 299.5144
    uint16_t raw = ((uint16_t)resp[4] << 8) | resp[5];
    return raw * 0.171342f - 299.5144f;
}

esp_err_t bm1370_send_work(const asic_job_t *job)
{
    // Build BM1370 job packet (86 bytes)
    uint8_t job_data[86] = {0};
    uint8_t asic_job_id = bm1370_job_to_asic_id(job->job_id);

    job_data[0] = asic_job_id;
    job_data[1] = job->midstate_count;
    // starting_nonce (4 bytes, big endian)
    job_data[2] = (job->starting_nonce >> 24) & 0xFF;
    job_data[3] = (job->starting_nonce >> 16) & 0xFF;
    job_data[4] = (job->starting_nonce >> 8) & 0xFF;
    job_data[5] = job->starting_nonce & 0xFF;
    // nbits (4 bytes)
    job_data[6] = (job->nbits >> 24) & 0xFF;
    job_data[7] = (job->nbits >> 16) & 0xFF;
    job_data[8] = (job->nbits >> 8) & 0xFF;
    job_data[9] = job->nbits & 0xFF;
    // ntime (4 bytes)
    job_data[10] = (job->ntime >> 24) & 0xFF;
    job_data[11] = (job->ntime >> 16) & 0xFF;
    job_data[12] = (job->ntime >> 8) & 0xFF;
    job_data[13] = job->ntime & 0xFF;
    // merkle_root (32 bytes)
    memcpy(&job_data[14], job->merkle_root, 32);
    // prev_block_hash (32 bytes)
    memcpy(&job_data[46], job->prev_block_hash, 32);
    // version (4 bytes)
    job_data[78] = (job->version >> 24) & 0xFF;
    job_data[79] = (job->version >> 16) & 0xFF;
    job_data[80] = (job->version >> 8) & 0xFF;
    job_data[81] = job->version & 0xFF;

    uint8_t buf[128];
    int len = asic_build_job(buf, sizeof(buf), job_data, 86);
    if (len < 0) return ESP_FAIL;

    return (serial_tx(buf, len) == len) ? ESP_OK : ESP_FAIL;
}
```

- [ ] **Step 5: Create bm1370.h**

```c
#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "asic.h"

// BM1370 constants
#define BM1370_CORE_COUNT       128
#define BM1370_SMALL_CORE_COUNT 2040
#define BM1370_MAX_JOB_ID       128

// Job ID mapping
uint8_t bm1370_job_to_asic_id(uint8_t job_id);
uint8_t bm1370_asic_to_job_id(uint8_t asic_id);
int     bm1370_nonce_to_chip(uint32_t nonce, int chip_count);

// Init, frequency, temp, work
esp_err_t bm1370_init(int expected_chips);
esp_err_t bm1370_set_frequency(uint16_t freq_mhz);
float     bm1370_read_temperature(void);
esp_err_t bm1370_send_work(const asic_job_t *job);
```

- [ ] **Step 6: Verify build**

Run: `idf.py build`
Expected: Clean build

- [ ] **Step 7: Commit**

```bash
git add components/asic_driver/
git commit -m "feat: BM1370 ASIC driver with PLL, enumeration, job dispatch"
```

---

## Phase 3: Stratum Client

### Task 3.1: SHA-256 & Mining Core

**Files:**
- Create: `components/mining/CMakeLists.txt`
- Create: `components/mining/include/sha256.h`
- Create: `components/mining/include/mining.h`
- Create: `components/mining/sha256.c`
- Create: `components/mining/mining.c`
- Create: `test/test_mining.c`

- [ ] **Step 1: Write failing tests**

```c
// test/test_mining.c
#include "unity.h"
#include "mining.h"
#include "sha256.h"

void test_sha256_empty_string(void)
{
    uint8_t hash[32];
    sha256_hash((const uint8_t *)"", 0, hash);
    // SHA-256("") = e3b0c44298fc1c14...
    TEST_ASSERT_EQUAL_HEX8(0xe3, hash[0]);
    TEST_ASSERT_EQUAL_HEX8(0xb0, hash[1]);
}

void test_double_sha256(void)
{
    uint8_t hash[32];
    mining_double_sha256((const uint8_t *)"test", 4, hash);
    // Double SHA-256 is SHA-256(SHA-256("test"))
    TEST_ASSERT_NOT_EQUAL(0, hash[0]); // Just verify it runs
}

void test_difficulty_from_hash(void)
{
    // All-zero hash = maximum difficulty
    uint8_t hash[32] = {0};
    hash[31] = 1; // Minimal hash
    double diff = mining_difficulty_from_hash(hash);
    TEST_ASSERT_GREATER_THAN(0.0, diff);
}

void test_merkle_root_single_branch(void)
{
    uint8_t coinbase_hash[32] = {0x01};
    uint8_t branch[32] = {0x02};
    uint8_t root[32];
    mining_compute_merkle_root(coinbase_hash, &branch, 1, root);
    // Verify it produces a 32-byte result
    TEST_ASSERT_NOT_EQUAL(0, root[0] | root[1]);
}
```

- [ ] **Step 2: Implement sha256.h and sha256.c**

```c
// components/mining/include/sha256.h
#pragma once
#include <stdint.h>
#include <stddef.h>

void sha256_hash(const uint8_t *data, size_t len, uint8_t *out);
void sha256_midstate(const uint8_t *block_header_64, uint8_t *midstate);
```

```c
// components/mining/sha256.c
#include "sha256.h"
#include "mbedtls/sha256.h"

void sha256_hash(const uint8_t *data, size_t len, uint8_t *out)
{
    mbedtls_sha256(data, len, out, 0);
}

void sha256_midstate(const uint8_t *block_header_64, uint8_t *midstate)
{
    // Midstate = SHA-256 internal state after processing first 64 bytes
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, block_header_64, 64);
    // Extract state (H0..H7)
    for (int i = 0; i < 8; i++) {
        midstate[i * 4 + 0] = (ctx.MBEDTLS_PRIVATE(state)[i] >> 24) & 0xFF;
        midstate[i * 4 + 1] = (ctx.MBEDTLS_PRIVATE(state)[i] >> 16) & 0xFF;
        midstate[i * 4 + 2] = (ctx.MBEDTLS_PRIVATE(state)[i] >> 8) & 0xFF;
        midstate[i * 4 + 3] = ctx.MBEDTLS_PRIVATE(state)[i] & 0xFF;
    }
    mbedtls_sha256_free(&ctx);
}
```

- [ ] **Step 3: Implement mining.h and mining.c**

```c
// components/mining/include/mining.h
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

void mining_double_sha256(const uint8_t *data, size_t len, uint8_t *out);

double mining_difficulty_from_hash(const uint8_t *hash);

void mining_compute_merkle_root(const uint8_t *coinbase_hash,
                                const uint8_t (*branches)[32],
                                int branch_count,
                                uint8_t *root_out);

bool mining_test_nonce(const uint8_t *block_header_80,
                       uint32_t nonce, uint32_t version_bits,
                       double *out_difficulty);

void mining_build_block_header(uint8_t *header_out,
                               uint32_t version,
                               const uint8_t *prev_hash,
                               const uint8_t *merkle_root,
                               uint32_t ntime,
                               uint32_t nbits,
                               uint32_t nonce);
```

```c
// components/mining/mining.c
#include "mining.h"
#include "sha256.h"
#include <string.h>
#include <math.h>

static const double TRUEDIFFONE = 26959535291011309493156476344723991336010898738574164086137773096960.0;

void mining_double_sha256(const uint8_t *data, size_t len, uint8_t *out)
{
    uint8_t tmp[32];
    sha256_hash(data, len, tmp);
    sha256_hash(tmp, 32, out);
}

double mining_difficulty_from_hash(const uint8_t *hash)
{
    double d = 0.0;
    for (int i = 0; i < 32; i++) {
        d *= 256.0;
        d += hash[i];
    }
    if (d == 0.0) return TRUEDIFFONE;
    return TRUEDIFFONE / d;
}

void mining_compute_merkle_root(const uint8_t *coinbase_hash,
                                const uint8_t (*branches)[32],
                                int branch_count,
                                uint8_t *root_out)
{
    uint8_t concat[64];
    memcpy(concat, coinbase_hash, 32);

    for (int i = 0; i < branch_count; i++) {
        memcpy(concat + 32, branches[i], 32);
        mining_double_sha256(concat, 64, concat); // Result goes into first 32 bytes
    }
    memcpy(root_out, concat, 32);
}

void mining_build_block_header(uint8_t *header_out,
                               uint32_t version,
                               const uint8_t *prev_hash,
                               const uint8_t *merkle_root,
                               uint32_t ntime,
                               uint32_t nbits,
                               uint32_t nonce)
{
    // 80-byte block header: version(4) + prev_hash(32) + merkle_root(32) + ntime(4) + nbits(4) + nonce(4)
    header_out[0] = version & 0xFF;
    header_out[1] = (version >> 8) & 0xFF;
    header_out[2] = (version >> 16) & 0xFF;
    header_out[3] = (version >> 24) & 0xFF;
    memcpy(header_out + 4, prev_hash, 32);
    memcpy(header_out + 36, merkle_root, 32);
    header_out[68] = ntime & 0xFF;
    header_out[69] = (ntime >> 8) & 0xFF;
    header_out[70] = (ntime >> 16) & 0xFF;
    header_out[71] = (ntime >> 24) & 0xFF;
    header_out[72] = nbits & 0xFF;
    header_out[73] = (nbits >> 8) & 0xFF;
    header_out[74] = (nbits >> 16) & 0xFF;
    header_out[75] = (nbits >> 24) & 0xFF;
    header_out[76] = nonce & 0xFF;
    header_out[77] = (nonce >> 8) & 0xFF;
    header_out[78] = (nonce >> 16) & 0xFF;
    header_out[79] = (nonce >> 24) & 0xFF;
}

bool mining_test_nonce(const uint8_t *block_header_80,
                       uint32_t nonce, uint32_t version_bits,
                       double *out_difficulty)
{
    uint8_t header[80];
    memcpy(header, block_header_80, 80);

    // Apply version rolling bits
    uint32_t version = header[0] | (header[1] << 8) |
                       (header[2] << 16) | (header[3] << 24);
    version = (version & ~0x1FFFE000) | (version_bits & 0x1FFFE000);
    header[0] = version & 0xFF;
    header[1] = (version >> 8) & 0xFF;
    header[2] = (version >> 16) & 0xFF;
    header[3] = (version >> 24) & 0xFF;

    // Apply nonce
    header[76] = nonce & 0xFF;
    header[77] = (nonce >> 8) & 0xFF;
    header[78] = (nonce >> 16) & 0xFF;
    header[79] = (nonce >> 24) & 0xFF;

    uint8_t hash[32];
    mining_double_sha256(header, 80, hash);

    *out_difficulty = mining_difficulty_from_hash(hash);
    return true;
}
```

- [ ] **Step 4: Create component CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "sha256.c" "mining.c"
    INCLUDE_DIRS "include"
    REQUIRES "mbedtls"
)
```

- [ ] **Step 5: Verify build**

Run: `idf.py build`
Expected: Clean build

- [ ] **Step 6: Commit**

```bash
git add components/mining/ test/test_mining.c
git commit -m "feat: SHA-256 mining core with merkle root, nonce test, difficulty"
```

---

### Task 3.2: Stratum v1 Protocol Client

**Files:**
- Create: `components/stratum/CMakeLists.txt`
- Create: `components/stratum/include/stratum_api.h`
- Create: `components/stratum/include/stratum_transport.h`
- Create: `components/stratum/include/stratum_client.h`
- Create: `components/stratum/include/stratum_job.h`
- Create: `components/stratum/stratum_api.c`
- Create: `components/stratum/stratum_transport.c`
- Create: `components/stratum/stratum_client.c`
- Create: `components/stratum/stratum_job.c`
- Create: `test/test_stratum_api.c`
- Create: `test/test_stratum_job.c`

- [ ] **Step 1: Write failing tests for stratum API**

```c
// test/test_stratum_api.c
#include "unity.h"
#include "stratum_api.h"

void test_build_subscribe_message(void)
{
    char buf[256];
    int len = stratum_build_subscribe(buf, sizeof(buf), "AsicOS/1.0", "BM1370");
    TEST_ASSERT_GREATER_THAN(0, len);
    // Should contain mining.subscribe
    TEST_ASSERT_NOT_NULL(strstr(buf, "mining.subscribe"));
}

void test_build_authorize_message(void)
{
    char buf[256];
    int len = stratum_build_authorize(buf, sizeof(buf), "user.worker", "pass");
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(buf, "mining.authorize"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "user.worker"));
}

void test_build_submit_message(void)
{
    char buf[256];
    int len = stratum_build_submit(buf, sizeof(buf), "user.worker",
                                   "job123", "00000001", "6543abcd",
                                   "12345678", "00002000");
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(buf, "mining.submit"));
}

void test_parse_notify(void)
{
    const char *json = "{\"id\":null,\"method\":\"mining.notify\","
        "\"params\":[\"bf\",\"4d16b6f85af6e2198f44ae2a6de67f78487ae5611b77c6a0\","
        "\"01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff20020e13\","
        "\"0a4d696e656420627920416e74506f6f6c\","
        "[\"17975b97c18ed1f7e255adf297599b55330edab87803c8170100000000000000\"],"
        "\"20000000\",\"17034219\",\"654f7c25\",true]}";
    stratum_notify_t notify;
    int ret = stratum_parse_notify(json, &notify);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL_STRING("bf", notify.job_id);
    TEST_ASSERT_TRUE(notify.clean_jobs);
}

void test_parse_set_difficulty(void)
{
    const char *json = "{\"id\":null,\"method\":\"mining.set_difficulty\",\"params\":[512]}";
    double diff;
    int ret = stratum_parse_set_difficulty(json, &diff);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL_DOUBLE(512.0, diff);
}
```

- [ ] **Step 2: Create stratum_api.h**

```c
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define STRATUM_MAX_MERKLE_BRANCHES 32
#define STRATUM_MAX_FIELD_LEN       128

typedef struct {
    char job_id[32];
    char prev_block_hash[72];
    char coinbase_1[512];
    char coinbase_2[512];
    char merkle_branches[STRATUM_MAX_MERKLE_BRANCHES][72];
    int  merkle_branch_count;
    char version[12];
    char nbits[12];
    char ntime[12];
    bool clean_jobs;
} stratum_notify_t;

typedef struct {
    char subscription_id[64];
    char extranonce1[32];
    int  extranonce2_size;
} stratum_subscribe_result_t;

// Build JSON-RPC messages. Returns length written, -1 on error.
int stratum_build_subscribe(char *buf, size_t len,
                            const char *user_agent, const char *asic_model);
int stratum_build_authorize(char *buf, size_t len,
                            const char *user, const char *pass);
int stratum_build_submit(char *buf, size_t len,
                         const char *user, const char *job_id,
                         const char *extranonce2, const char *ntime,
                         const char *nonce, const char *version_bits);
int stratum_build_configure(char *buf, size_t len, uint32_t version_mask);
int stratum_build_suggest_difficulty(char *buf, size_t len, double diff);

// Parse server messages. Returns 0 on success, -1 on error.
int stratum_parse_notify(const char *json, stratum_notify_t *out);
int stratum_parse_set_difficulty(const char *json, double *out);
int stratum_parse_subscribe_result(const char *json, stratum_subscribe_result_t *out);
int stratum_parse_set_version_mask(const char *json, uint32_t *out);

// Detect message type from JSON. Returns method string or NULL.
const char *stratum_detect_method(const char *json);
```

- [ ] **Step 3: Implement stratum_api.c**

```c
#include "stratum_api.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>

static int s_msg_id = 1;

int stratum_build_subscribe(char *buf, size_t len,
                            const char *user_agent, const char *asic_model)
{
    return snprintf(buf, len,
        "{\"id\":%d,\"method\":\"mining.subscribe\","
        "\"params\":[\"%s/%s\"]}\n",
        s_msg_id++, user_agent, asic_model);
}

int stratum_build_authorize(char *buf, size_t len,
                            const char *user, const char *pass)
{
    return snprintf(buf, len,
        "{\"id\":%d,\"method\":\"mining.authorize\","
        "\"params\":[\"%s\",\"%s\"]}\n",
        s_msg_id++, user, pass);
}

int stratum_build_submit(char *buf, size_t len,
                         const char *user, const char *job_id,
                         const char *extranonce2, const char *ntime,
                         const char *nonce, const char *version_bits)
{
    return snprintf(buf, len,
        "{\"id\":%d,\"method\":\"mining.submit\","
        "\"params\":[\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"]}\n",
        s_msg_id++, user, job_id, extranonce2, ntime, nonce, version_bits);
}

int stratum_build_configure(char *buf, size_t len, uint32_t version_mask)
{
    return snprintf(buf, len,
        "{\"id\":%d,\"method\":\"mining.configure\","
        "\"params\":[[\"version-rolling\"],"
        "{\"version-rolling.mask\":\"%08x\"}]}\n",
        s_msg_id++, version_mask);
}

int stratum_build_suggest_difficulty(char *buf, size_t len, double diff)
{
    return snprintf(buf, len,
        "{\"id\":%d,\"method\":\"mining.suggest_difficulty\","
        "\"params\":[%g]}\n",
        s_msg_id++, diff);
}

int stratum_parse_notify(const char *json, stratum_notify_t *out)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;

    cJSON *params = cJSON_GetObjectItem(root, "params");
    if (!cJSON_IsArray(params) || cJSON_GetArraySize(params) < 9) {
        cJSON_Delete(root);
        return -1;
    }

    strncpy(out->job_id, cJSON_GetArrayItem(params, 0)->valuestring, sizeof(out->job_id) - 1);
    strncpy(out->prev_block_hash, cJSON_GetArrayItem(params, 1)->valuestring, sizeof(out->prev_block_hash) - 1);
    strncpy(out->coinbase_1, cJSON_GetArrayItem(params, 2)->valuestring, sizeof(out->coinbase_1) - 1);
    strncpy(out->coinbase_2, cJSON_GetArrayItem(params, 3)->valuestring, sizeof(out->coinbase_2) - 1);

    cJSON *branches = cJSON_GetArrayItem(params, 4);
    out->merkle_branch_count = cJSON_GetArraySize(branches);
    for (int i = 0; i < out->merkle_branch_count && i < STRATUM_MAX_MERKLE_BRANCHES; i++) {
        strncpy(out->merkle_branches[i], cJSON_GetArrayItem(branches, i)->valuestring, 71);
    }

    strncpy(out->version, cJSON_GetArrayItem(params, 5)->valuestring, sizeof(out->version) - 1);
    strncpy(out->nbits, cJSON_GetArrayItem(params, 6)->valuestring, sizeof(out->nbits) - 1);
    strncpy(out->ntime, cJSON_GetArrayItem(params, 7)->valuestring, sizeof(out->ntime) - 1);
    out->clean_jobs = cJSON_IsTrue(cJSON_GetArrayItem(params, 8));

    cJSON_Delete(root);
    return 0;
}

int stratum_parse_set_difficulty(const char *json, double *out)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;

    cJSON *params = cJSON_GetObjectItem(root, "params");
    if (!cJSON_IsArray(params) || cJSON_GetArraySize(params) < 1) {
        cJSON_Delete(root);
        return -1;
    }

    *out = cJSON_GetArrayItem(params, 0)->valuedouble;
    cJSON_Delete(root);
    return 0;
}

int stratum_parse_subscribe_result(const char *json, stratum_subscribe_result_t *out)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;

    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!cJSON_IsArray(result) || cJSON_GetArraySize(result) < 3) {
        cJSON_Delete(root);
        return -1;
    }

    // result[0] = subscriptions array, result[1] = extranonce1, result[2] = extranonce2_size
    cJSON *subs = cJSON_GetArrayItem(result, 0);
    if (cJSON_IsArray(subs) && cJSON_GetArraySize(subs) > 0) {
        cJSON *first = cJSON_GetArrayItem(subs, 0);
        if (cJSON_IsArray(first) && cJSON_GetArraySize(first) > 1) {
            strncpy(out->subscription_id,
                    cJSON_GetArrayItem(first, 1)->valuestring,
                    sizeof(out->subscription_id) - 1);
        }
    }

    strncpy(out->extranonce1,
            cJSON_GetArrayItem(result, 1)->valuestring,
            sizeof(out->extranonce1) - 1);
    out->extranonce2_size = cJSON_GetArrayItem(result, 2)->valueint;

    cJSON_Delete(root);
    return 0;
}

int stratum_parse_set_version_mask(const char *json, uint32_t *out)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;

    cJSON *params = cJSON_GetObjectItem(root, "params");
    if (!cJSON_IsArray(params) || cJSON_GetArraySize(params) < 1) {
        cJSON_Delete(root);
        return -1;
    }

    const char *mask_str = cJSON_GetArrayItem(params, 0)->valuestring;
    *out = (uint32_t)strtoul(mask_str, NULL, 16);

    cJSON_Delete(root);
    return 0;
}

const char *stratum_detect_method(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return NULL;

    cJSON *method = cJSON_GetObjectItem(root, "method");
    static char method_buf[64];
    if (cJSON_IsString(method)) {
        strncpy(method_buf, method->valuestring, sizeof(method_buf) - 1);
        cJSON_Delete(root);
        return method_buf;
    }

    cJSON_Delete(root);
    return NULL;
}
```

- [ ] **Step 4: Create stratum_transport.h and stratum_transport.c**

```c
// components/stratum/include/stratum_transport.h
#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>

typedef struct stratum_conn stratum_conn_t;

// Connect to pool. Use TLS if port is 443 or tls=true.
stratum_conn_t *stratum_connect(const char *host, uint16_t port, bool tls);

// Disconnect and free.
void stratum_disconnect(stratum_conn_t *conn);

// Send null-terminated string. Returns bytes sent or -1.
int stratum_send(stratum_conn_t *conn, const char *data, size_t len);

// Receive a line (up to \n). Returns length or -1 on error, 0 on timeout.
int stratum_recv_line(stratum_conn_t *conn, char *buf, size_t buf_len,
                      uint32_t timeout_ms);

// Check if connected.
bool stratum_is_connected(stratum_conn_t *conn);
```

```c
// components/stratum/stratum_transport.c
#include "stratum_transport.h"
#include "esp_log.h"
#include "esp_tls.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "stratum_transport";

struct stratum_conn {
    int sock;
    esp_tls_t *tls;
    bool use_tls;
    char recv_buf[4096];
    int recv_pos;
};

stratum_conn_t *stratum_connect(const char *host, uint16_t port, bool tls)
{
    stratum_conn_t *conn = calloc(1, sizeof(stratum_conn_t));
    if (!conn) return NULL;

    conn->use_tls = tls;
    conn->sock = -1;

    if (tls) {
        esp_tls_cfg_t cfg = {
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = 10000,
        };
        conn->tls = esp_tls_init();
        char port_str[8];
        snprintf(port_str, sizeof(port_str), "%d", port);

        if (esp_tls_conn_new_sync(host, strlen(host), port, &cfg, conn->tls) != 1) {
            ESP_LOGE(TAG, "TLS connection failed to %s:%d", host, port);
            esp_tls_conn_destroy(conn->tls);
            free(conn);
            return NULL;
        }
    } else {
        struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
        struct addrinfo *res;
        char port_str[8];
        snprintf(port_str, sizeof(port_str), "%d", port);

        if (getaddrinfo(host, port_str, &hints, &res) != 0) {
            ESP_LOGE(TAG, "DNS failed for %s", host);
            free(conn);
            return NULL;
        }

        conn->sock = socket(res->ai_family, res->ai_socktype, 0);
        if (conn->sock < 0 || connect(conn->sock, res->ai_addr, res->ai_addrlen) != 0) {
            ESP_LOGE(TAG, "TCP connect failed to %s:%d", host, port);
            if (conn->sock >= 0) close(conn->sock);
            freeaddrinfo(res);
            free(conn);
            return NULL;
        }
        freeaddrinfo(res);

        // Set receive timeout
        struct timeval tv = {.tv_sec = 30};
        setsockopt(conn->sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    ESP_LOGI(TAG, "Connected to %s:%d (%s)", host, port, tls ? "TLS" : "TCP");
    return conn;
}

void stratum_disconnect(stratum_conn_t *conn)
{
    if (!conn) return;
    if (conn->use_tls && conn->tls) {
        esp_tls_conn_destroy(conn->tls);
    } else if (conn->sock >= 0) {
        close(conn->sock);
    }
    free(conn);
}

int stratum_send(stratum_conn_t *conn, const char *data, size_t len)
{
    if (conn->use_tls) {
        return esp_tls_conn_write(conn->tls, data, len);
    }
    return send(conn->sock, data, len, 0);
}

int stratum_recv_line(stratum_conn_t *conn, char *buf, size_t buf_len,
                      uint32_t timeout_ms)
{
    // Check buffer for existing newline
    for (int i = 0; i < conn->recv_pos; i++) {
        if (conn->recv_buf[i] == '\n') {
            int line_len = i + 1;
            memcpy(buf, conn->recv_buf, line_len < (int)buf_len ? line_len : (int)buf_len - 1);
            buf[line_len < (int)buf_len ? line_len : (int)buf_len - 1] = '\0';
            // Shift remaining data
            memmove(conn->recv_buf, conn->recv_buf + line_len, conn->recv_pos - line_len);
            conn->recv_pos -= line_len;
            return line_len;
        }
    }

    // Read more data
    int space = sizeof(conn->recv_buf) - conn->recv_pos;
    if (space <= 0) return -1; // Buffer full, no newline found

    int read;
    if (conn->use_tls) {
        read = esp_tls_conn_read(conn->tls, conn->recv_buf + conn->recv_pos, space);
    } else {
        read = recv(conn->sock, conn->recv_buf + conn->recv_pos, space, 0);
    }

    if (read <= 0) return read;
    conn->recv_pos += read;

    // Retry extracting a line
    return stratum_recv_line(conn, buf, buf_len, 0);
}

bool stratum_is_connected(stratum_conn_t *conn)
{
    if (!conn) return false;
    if (conn->use_tls) return conn->tls != NULL;
    return conn->sock >= 0;
}
```

- [ ] **Step 5: Create stratum_job.h and stratum_job.c**

```c
// components/stratum/include/stratum_job.h
#pragma once

#include "stratum_api.h"
#include "asic.h"
#include <stdint.h>

// Build an ASIC job from stratum notify data
// extranonce1/2 are hex strings, extranonce2_counter is incremented each call
int stratum_build_asic_job(const stratum_notify_t *notify,
                           const char *extranonce1,
                           uint32_t extranonce2_counter,
                           int extranonce2_size,
                           double pool_diff,
                           int pool_id,
                           asic_job_t *job_out);

// Convert hex string to bytes. Returns number of bytes written.
int hex_to_bytes(const char *hex, uint8_t *out, size_t out_len);

// Convert bytes to hex string.
void bytes_to_hex(const uint8_t *data, size_t len, char *out);
```

```c
// components/stratum/stratum_job.c
#include "stratum_job.h"
#include "mining.h"
#include "sha256.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int hex_to_bytes(const char *hex, uint8_t *out, size_t out_len)
{
    size_t hex_len = strlen(hex);
    size_t byte_len = hex_len / 2;
    if (byte_len > out_len) byte_len = out_len;

    for (size_t i = 0; i < byte_len; i++) {
        char byte_str[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
        out[i] = (uint8_t)strtoul(byte_str, NULL, 16);
    }
    return (int)byte_len;
}

void bytes_to_hex(const uint8_t *data, size_t len, char *out)
{
    for (size_t i = 0; i < len; i++) {
        sprintf(out + i * 2, "%02x", data[i]);
    }
    out[len * 2] = '\0';
}

int stratum_build_asic_job(const stratum_notify_t *notify,
                           const char *extranonce1,
                           uint32_t extranonce2_counter,
                           int extranonce2_size,
                           double pool_diff,
                           int pool_id,
                           asic_job_t *job_out)
{
    memset(job_out, 0, sizeof(*job_out));

    // Build extranonce2 hex
    char en2_hex[32];
    for (int i = 0; i < extranonce2_size; i++) {
        sprintf(en2_hex + i * 2, "%02x",
                (extranonce2_counter >> ((extranonce2_size - 1 - i) * 8)) & 0xFF);
    }
    en2_hex[extranonce2_size * 2] = '\0';

    // Build coinbase: coinbase1 + extranonce1 + extranonce2 + coinbase2
    char coinbase_hex[1024];
    snprintf(coinbase_hex, sizeof(coinbase_hex), "%s%s%s%s",
             notify->coinbase_1, extranonce1, en2_hex, notify->coinbase_2);

    uint8_t coinbase_bin[512];
    int cb_len = hex_to_bytes(coinbase_hex, coinbase_bin, sizeof(coinbase_bin));

    // Double-SHA256 the coinbase to get coinbase hash
    uint8_t coinbase_hash[32];
    mining_double_sha256(coinbase_bin, cb_len, coinbase_hash);

    // Build merkle root
    uint8_t branches[STRATUM_MAX_MERKLE_BRANCHES][32];
    for (int i = 0; i < notify->merkle_branch_count; i++) {
        hex_to_bytes(notify->merkle_branches[i], branches[i], 32);
    }

    uint8_t merkle_root[32];
    mining_compute_merkle_root(coinbase_hash,
                               (const uint8_t (*)[32])branches,
                               notify->merkle_branch_count,
                               merkle_root);

    // Fill job
    static uint8_t s_job_id = 0;
    job_out->job_id = s_job_id++;
    job_out->midstate_count = 1;
    job_out->starting_nonce = 0;
    job_out->nbits = (uint32_t)strtoul(notify->nbits, NULL, 16);
    job_out->ntime = (uint32_t)strtoul(notify->ntime, NULL, 16);
    job_out->version = (uint32_t)strtoul(notify->version, NULL, 16);
    memcpy(job_out->merkle_root, merkle_root, 32);
    hex_to_bytes(notify->prev_block_hash, job_out->prev_block_hash, 32);
    strncpy(job_out->stratum_job_id, notify->job_id, sizeof(job_out->stratum_job_id) - 1);
    strncpy(job_out->extranonce2, en2_hex, sizeof(job_out->extranonce2) - 1);
    job_out->pool_diff = pool_diff;
    job_out->pool_id = pool_id;

    return 0;
}
```

- [ ] **Step 6: Create stratum_client.h and stratum_client.c**

```c
// components/stratum/include/stratum_client.h
#pragma once

#include "stratum_api.h"
#include "stratum_transport.h"
#include <stdbool.h>

typedef enum {
    STRATUM_STATE_DISCONNECTED,
    STRATUM_STATE_CONNECTING,
    STRATUM_STATE_SUBSCRIBING,
    STRATUM_STATE_AUTHORIZING,
    STRATUM_STATE_CONFIGURING,
    STRATUM_STATE_MINING,
    STRATUM_STATE_ERROR,
} stratum_state_t;

typedef struct {
    char pool_url[128];
    uint16_t pool_port;
    char pool_user[128];
    char pool_pass[64];
    bool use_tls;
} stratum_pool_config_t;

typedef void (*stratum_notify_cb_t)(const stratum_notify_t *notify, int pool_id);
typedef void (*stratum_difficulty_cb_t)(double difficulty, int pool_id);

typedef struct {
    stratum_pool_config_t primary;
    stratum_pool_config_t fallback;
    stratum_notify_cb_t on_notify;
    stratum_difficulty_cb_t on_difficulty;
} stratum_client_config_t;

// Lifecycle
esp_err_t stratum_client_init(const stratum_client_config_t *config);
void      stratum_client_task(void *param); // FreeRTOS task entry

// State
stratum_state_t stratum_client_get_state(void);
const char     *stratum_client_get_extranonce1(void);
int             stratum_client_get_extranonce2_size(void);

// Stats
uint32_t stratum_client_get_accepted(void);
uint32_t stratum_client_get_rejected(void);
double   stratum_client_get_current_difficulty(void);

// Share submission (called from result task)
esp_err_t stratum_client_submit_share(const char *job_id,
                                      const char *extranonce2,
                                      const char *ntime,
                                      const char *nonce,
                                      const char *version_bits);
```

```c
// components/stratum/stratum_client.c
#include "stratum_client.h"
#include "stratum_api.h"
#include "stratum_transport.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "stratum_client";

static stratum_client_config_t s_config;
static stratum_conn_t *s_conn = NULL;
static stratum_state_t s_state = STRATUM_STATE_DISCONNECTED;
static stratum_subscribe_result_t s_sub_result;
static double s_current_diff = 1.0;
static uint32_t s_accepted = 0;
static uint32_t s_rejected = 0;
static char s_line_buf[4096];

esp_err_t stratum_client_init(const stratum_client_config_t *config)
{
    memcpy(&s_config, config, sizeof(s_config));
    s_state = STRATUM_STATE_DISCONNECTED;
    return ESP_OK;
}

static int do_subscribe(void)
{
    char buf[512];
    int len = stratum_build_subscribe(buf, sizeof(buf), "AsicOS/1.0", "BM1370");
    if (stratum_send(s_conn, buf, len) < 0) return -1;

    len = stratum_recv_line(s_conn, s_line_buf, sizeof(s_line_buf), 10000);
    if (len <= 0) return -1;

    return stratum_parse_subscribe_result(s_line_buf, &s_sub_result);
}

static int do_authorize(const stratum_pool_config_t *pool)
{
    char buf[256];
    int len = stratum_build_authorize(buf, sizeof(buf), pool->pool_user, pool->pool_pass);
    if (stratum_send(s_conn, buf, len) < 0) return -1;

    len = stratum_recv_line(s_conn, s_line_buf, sizeof(s_line_buf), 10000);
    if (len <= 0) return -1;

    // Check for {"result": true}
    if (strstr(s_line_buf, "true") == NULL) {
        ESP_LOGE(TAG, "Authorization rejected");
        return -1;
    }
    return 0;
}

static int do_configure(void)
{
    char buf[256];
    int len = stratum_build_configure(buf, sizeof(buf), 0x1FFFE000);
    if (stratum_send(s_conn, buf, len) < 0) return -1;

    len = stratum_recv_line(s_conn, s_line_buf, sizeof(s_line_buf), 10000);
    // Configure response is optional, don't fail if missing
    return 0;
}

static void handle_line(const char *line, int pool_id)
{
    const char *method = stratum_detect_method(line);
    if (!method) {
        // Could be a response to our submit
        if (strstr(line, "\"result\":true")) {
            s_accepted++;
        } else if (strstr(line, "\"result\":false") || strstr(line, "\"error\"")) {
            s_rejected++;
        }
        return;
    }

    if (strcmp(method, "mining.notify") == 0) {
        stratum_notify_t notify;
        if (stratum_parse_notify(line, &notify) == 0 && s_config.on_notify) {
            s_config.on_notify(&notify, pool_id);
        }
    } else if (strcmp(method, "mining.set_difficulty") == 0) {
        double diff;
        if (stratum_parse_set_difficulty(line, &diff) == 0) {
            s_current_diff = diff;
            if (s_config.on_difficulty) {
                s_config.on_difficulty(diff, pool_id);
            }
        }
    } else if (strcmp(method, "mining.set_version_mask") == 0) {
        uint32_t mask;
        stratum_parse_set_version_mask(line, &mask);
        // TODO: Update ASIC version mask
    }
}

void stratum_client_task(void *param)
{
    const stratum_pool_config_t *pool = &s_config.primary;
    int pool_id = 0;
    int reconnect_delay_s = 5;

    while (1) {
        // Connect
        s_state = STRATUM_STATE_CONNECTING;
        ESP_LOGI(TAG, "Connecting to %s:%d...", pool->pool_url, pool->pool_port);

        s_conn = stratum_connect(pool->pool_url, pool->pool_port, pool->use_tls);
        if (!s_conn) {
            ESP_LOGE(TAG, "Connection failed, retrying in %ds", reconnect_delay_s);
            vTaskDelay(pdMS_TO_TICKS(reconnect_delay_s * 1000));
            // Try fallback
            if (pool == &s_config.primary && s_config.fallback.pool_url[0]) {
                pool = &s_config.fallback;
                pool_id = 1;
            } else {
                pool = &s_config.primary;
                pool_id = 0;
            }
            continue;
        }

        // Subscribe
        s_state = STRATUM_STATE_SUBSCRIBING;
        if (do_subscribe() != 0) {
            ESP_LOGE(TAG, "Subscribe failed");
            stratum_disconnect(s_conn);
            s_conn = NULL;
            vTaskDelay(pdMS_TO_TICKS(reconnect_delay_s * 1000));
            continue;
        }

        // Configure (version rolling)
        s_state = STRATUM_STATE_CONFIGURING;
        do_configure();

        // Authorize
        s_state = STRATUM_STATE_AUTHORIZING;
        if (do_authorize(pool) != 0) {
            stratum_disconnect(s_conn);
            s_conn = NULL;
            vTaskDelay(pdMS_TO_TICKS(reconnect_delay_s * 1000));
            continue;
        }

        // Mining loop
        s_state = STRATUM_STATE_MINING;
        reconnect_delay_s = 5; // Reset on successful connection
        ESP_LOGI(TAG, "Mining on pool %d: %s", pool_id, pool->pool_url);

        while (stratum_is_connected(s_conn)) {
            int len = stratum_recv_line(s_conn, s_line_buf, sizeof(s_line_buf), 60000);
            if (len > 0) {
                handle_line(s_line_buf, pool_id);
            } else if (len < 0) {
                ESP_LOGW(TAG, "Connection lost");
                break;
            }
            // len == 0 is timeout, which is OK (keepalive)
        }

        stratum_disconnect(s_conn);
        s_conn = NULL;
        s_state = STRATUM_STATE_DISCONNECTED;
    }
}

stratum_state_t stratum_client_get_state(void) { return s_state; }
const char *stratum_client_get_extranonce1(void) { return s_sub_result.extranonce1; }
int stratum_client_get_extranonce2_size(void) { return s_sub_result.extranonce2_size; }
uint32_t stratum_client_get_accepted(void) { return s_accepted; }
uint32_t stratum_client_get_rejected(void) { return s_rejected; }
double stratum_client_get_current_difficulty(void) { return s_current_diff; }

esp_err_t stratum_client_submit_share(const char *job_id,
                                      const char *extranonce2,
                                      const char *ntime,
                                      const char *nonce,
                                      const char *version_bits)
{
    if (!s_conn || s_state != STRATUM_STATE_MINING) return ESP_ERR_INVALID_STATE;

    char buf[512];
    int len = stratum_build_submit(buf, sizeof(buf),
                                   s_config.primary.pool_user,
                                   job_id, extranonce2, ntime, nonce, version_bits);
    if (len < 0) return ESP_FAIL;
    return (stratum_send(s_conn, buf, len) >= 0) ? ESP_OK : ESP_FAIL;
}
```

- [ ] **Step 7: Create component CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "stratum_api.c" "stratum_transport.c" "stratum_client.c" "stratum_job.c"
    INCLUDE_DIRS "include"
    REQUIRES "cJSON" "esp_tls" "lwip" "mining" "asic_driver"
)
```

- [ ] **Step 8: Verify build**

Run: `idf.py build`
Expected: Clean build

- [ ] **Step 9: Commit**

```bash
git add components/stratum/ test/test_stratum_api.c test/test_stratum_job.c
git commit -m "feat: Stratum v1 client with TLS, version rolling, dual pool"
```

---

### Task 3.3: Mining & Result FreeRTOS Tasks

**Files:**
- Create: `main/tasks/mining_task.c`
- Create: `main/tasks/mining_task.h`
- Create: `main/tasks/result_task.c`
- Create: `main/tasks/result_task.h`
- Create: `main/tasks/hashrate_task.c`
- Create: `main/tasks/hashrate_task.h`

- [ ] **Step 1: Create mining_task.h**

```c
#pragma once

#define MINING_TASK_STACK_SIZE  8192
#define MINING_TASK_PRIORITY    10

void mining_task_start(void);
```

- [ ] **Step 2: Implement mining_task.c**

```c
#include "mining_task.h"
#include "stratum_client.h"
#include "stratum_job.h"
#include "bm1370.h"
#include "board.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "mining_task";

static asic_job_t s_active_jobs[BM1370_MAX_JOB_ID];
static SemaphoreHandle_t s_jobs_mutex;
static volatile bool s_new_work_available = false;
static stratum_notify_t s_current_notify;
static double s_current_pool_diff = 1.0;
static uint32_t s_extranonce2_counter = 0;

// Called by stratum client on new work
void mining_on_notify(const stratum_notify_t *notify, int pool_id)
{
    memcpy(&s_current_notify, notify, sizeof(stratum_notify_t));
    s_new_work_available = true;
}

void mining_on_difficulty(double diff, int pool_id)
{
    s_current_pool_diff = diff;
    ESP_LOGI(TAG, "Pool difficulty set to %f", diff);
}

const asic_job_t *mining_get_job(uint8_t job_id)
{
    if (job_id >= BM1370_MAX_JOB_ID) return NULL;
    return &s_active_jobs[job_id];
}

static void mining_task(void *param)
{
    const board_config_t *board = board_get_config();
    s_jobs_mutex = xSemaphoreCreateMutex();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(board->job_interval_ms));

        if (stratum_client_get_state() != STRATUM_STATE_MINING) continue;
        if (!s_new_work_available && s_extranonce2_counter > 0) {
            // Increment extranonce2 for same template
        }

        asic_job_t job;
        int ret = stratum_build_asic_job(&s_current_notify,
                                         stratum_client_get_extranonce1(),
                                         s_extranonce2_counter++,
                                         stratum_client_get_extranonce2_size(),
                                         s_current_pool_diff,
                                         0,
                                         &job);
        if (ret != 0) continue;

        // Set difficulty mask on ASIC
        asic_set_difficulty_mask((uint64_t)s_current_pool_diff);

        // Store job
        uint8_t asic_job_id = bm1370_job_to_asic_id(job.job_id);
        if (xSemaphoreTake(s_jobs_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            memcpy(&s_active_jobs[asic_job_id], &job, sizeof(job));
            xSemaphoreGive(s_jobs_mutex);
        }

        // Send to ASICs
        bm1370_send_work(&job);
        s_new_work_available = false;
    }
}

void mining_task_start(void)
{
    xTaskCreate(mining_task, "mining", MINING_TASK_STACK_SIZE, NULL,
                MINING_TASK_PRIORITY, NULL);
}
```

- [ ] **Step 3: Create result_task.h and result_task.c**

```c
// main/tasks/result_task.h
#pragma once

#define RESULT_TASK_STACK_SIZE  8192
#define RESULT_TASK_PRIORITY    15

typedef struct {
    double   best_difficulty;
    uint64_t total_shares;
    uint64_t accepted_shares;
    uint64_t rejected_shares;
    uint64_t duplicate_nonces;
} mining_stats_t;

void result_task_start(void);
const mining_stats_t *result_task_get_stats(void);
```

```c
// main/tasks/result_task.c
#include "result_task.h"
#include "mining_task.h"
#include "stratum_client.h"
#include "stratum_job.h"
#include "bm1370.h"
#include "mining.h"
#include "nvs_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "result_task";

static mining_stats_t s_stats = {0};

// Simple duplicate detection ring buffer
#define DEDUP_SIZE 32
static uint64_t s_dedup_ring[DEDUP_SIZE];
static int s_dedup_pos = 0;

static bool is_duplicate(uint32_t nonce, uint16_t version)
{
    uint64_t key = ((uint64_t)nonce << 16) | version;
    for (int i = 0; i < DEDUP_SIZE; i++) {
        if (s_dedup_ring[i] == key) return true;
    }
    s_dedup_ring[s_dedup_pos] = key;
    s_dedup_pos = (s_dedup_pos + 1) % DEDUP_SIZE;
    return false;
}

static void result_task(void *param)
{
    s_stats.best_difficulty = (double)nvs_config_get_u64(NVS_KEY_BEST_DIFF, 0);

    while (1) {
        asic_result_t result;
        int ret = asic_receive_result(&result, 60000);

        if (ret < 0) {
            ESP_LOGW(TAG, "ASIC receive timeout");
            continue;
        }
        if (ret == 0) continue; // Register read, not nonce

        // Map ASIC job ID back to our job ID
        uint8_t job_id = bm1370_asic_to_job_id(result.job_id);
        const asic_job_t *job = mining_get_job(job_id);
        if (!job) {
            ESP_LOGW(TAG, "Unknown job ID %d", job_id);
            continue;
        }

        // Check duplicate
        if (is_duplicate(result.nonce, result.rolled_version)) {
            s_stats.duplicate_nonces++;
            continue;
        }

        // Build block header and test nonce
        uint8_t header[80];
        mining_build_block_header(header, job->version, job->prev_block_hash,
                                  job->merkle_root, job->ntime, job->nbits, 0);

        uint32_t version_bits = (uint32_t)result.rolled_version << 13;
        double difficulty = 0;
        mining_test_nonce(header, result.nonce, version_bits, &difficulty);

        // Update best difficulty
        if (difficulty > s_stats.best_difficulty) {
            s_stats.best_difficulty = difficulty;
            nvs_config_set_u64(NVS_KEY_BEST_DIFF, (uint64_t)difficulty);
            ESP_LOGI(TAG, "New best difficulty: %f", difficulty);
        }

        // Submit if meets pool difficulty
        if (difficulty >= job->pool_diff) {
            s_stats.total_shares++;

            char nonce_hex[12], ntime_hex[12], version_hex[12];
            snprintf(nonce_hex, sizeof(nonce_hex), "%08x", result.nonce);
            snprintf(ntime_hex, sizeof(ntime_hex), "%08x", job->ntime);
            snprintf(version_hex, sizeof(version_hex), "%08x", version_bits);

            esp_err_t err = stratum_client_submit_share(
                job->stratum_job_id, job->extranonce2,
                ntime_hex, nonce_hex, version_hex);

            if (err == ESP_OK) {
                s_stats.accepted_shares++;
            } else {
                s_stats.rejected_shares++;
            }

            ESP_LOGI(TAG, "Share submitted: diff=%.2f pool_diff=%.2f",
                     difficulty, job->pool_diff);
        }
    }
}

void result_task_start(void)
{
    xTaskCreate(result_task, "result", RESULT_TASK_STACK_SIZE, NULL,
                RESULT_TASK_PRIORITY, NULL);
}

const mining_stats_t *result_task_get_stats(void)
{
    return &s_stats;
}
```

- [ ] **Step 4: Create hashrate_task.h and hashrate_task.c**

```c
// main/tasks/hashrate_task.h
#pragma once

#define HASHRATE_TASK_STACK_SIZE 4096
#define HASHRATE_TASK_PRIORITY   10

typedef struct {
    float total_hashrate_ghs;
    float per_chip_hashrate_ghs[16]; // Up to 16 chips
    int   chip_count;
} hashrate_info_t;

void hashrate_task_start(void);
const hashrate_info_t *hashrate_task_get_info(void);
```

```c
// main/tasks/hashrate_task.c
#include "hashrate_task.h"
#include "asic.h"
#include "board.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "hashrate";

static hashrate_info_t s_info = {0};
static uint64_t s_prev_counter[16] = {0};
static int64_t s_prev_time_us = 0;

// 3-tap median filter
static float median3(float a, float b, float c)
{
    if ((a >= b && a <= c) || (a <= b && a >= c)) return a;
    if ((b >= a && b <= c) || (b <= a && b >= c)) return b;
    return c;
}

static float s_history[16][3] = {{0}};
static int s_hist_idx = 0;

static void hashrate_task(void *param)
{
    const board_config_t *board = board_get_config();
    s_info.chip_count = board->expected_chip_count;

    vTaskDelay(pdMS_TO_TICKS(5000)); // Let ASICs warm up

    s_prev_time_us = esp_timer_get_time();
    for (int i = 0; i < s_info.chip_count; i++) {
        s_prev_counter[i] = asic_read_hash_counter(i);
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000)); // Poll every 10s

        int64_t now_us = esp_timer_get_time();
        int64_t dt_us = now_us - s_prev_time_us;
        if (dt_us <= 0) continue;

        float total = 0;
        for (int i = 0; i < s_info.chip_count; i++) {
            uint64_t counter = asic_read_hash_counter(i);
            uint64_t delta = counter - s_prev_counter[i];
            s_prev_counter[i] = counter;

            // GH/s = (delta * 2^32) / dt_us / 1000
            float ghs = (float)((double)delta * 4294967296.0 / (double)dt_us / 1000.0);

            // Median filter
            s_history[i][s_hist_idx % 3] = ghs;
            if (s_hist_idx >= 2) {
                ghs = median3(s_history[i][0], s_history[i][1], s_history[i][2]);
            }

            // Exponential smoothing (50/50)
            s_info.per_chip_hashrate_ghs[i] =
                0.5f * s_info.per_chip_hashrate_ghs[i] + 0.5f * ghs;
            total += s_info.per_chip_hashrate_ghs[i];
        }
        s_hist_idx++;

        s_info.total_hashrate_ghs = total;
        s_prev_time_us = now_us;

        ESP_LOGI(TAG, "Hashrate: %.2f GH/s", total);
    }
}

void hashrate_task_start(void)
{
    xTaskCreate(hashrate_task, "hashrate", HASHRATE_TASK_STACK_SIZE, NULL,
                HASHRATE_TASK_PRIORITY, NULL);
}

const hashrate_info_t *hashrate_task_get_info(void)
{
    return &s_info;
}
```

- [ ] **Step 5: Wire tasks into main.c**

Update `main/main.c` to create all tasks after init:

```c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_config.h"
#include "board.h"
#include "serial.h"
#include "bm1370.h"
#include "stratum_client.h"
#include "tasks/mining_task.h"
#include "tasks/result_task.h"
#include "tasks/hashrate_task.h"

static const char *TAG = "asicos";

// Forward declarations for mining callbacks
extern void mining_on_notify(const stratum_notify_t *notify, int pool_id);
extern void mining_on_difficulty(double diff, int pool_id);

void app_main(void)
{
    ESP_LOGI(TAG, "AsicOS starting...");

    // Init NVS
    ESP_ERROR_CHECK(nvs_config_init());

    const board_config_t *board = board_get_config();
    ESP_LOGI(TAG, "Board: %s, ASIC: %s", board->name, board->asic_model);

    // Init UART
    serial_config_t serial_cfg = {
        .port = UART_NUM_1,
        .tx_pin = board->uart_tx_pin,
        .rx_pin = board->uart_rx_pin,
        .baud_rate = 115200,
    };
    ESP_ERROR_CHECK(serial_init(&serial_cfg));

    // Init BM1370
    esp_err_t err = bm1370_init(board->expected_chip_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ASIC init failed!");
    }

    // Ramp frequency
    uint16_t freq = nvs_config_get_u16(NVS_KEY_ASIC_FREQ, board->freq_default);
    bm1370_set_frequency(freq);

    // Switch to fast UART
    serial_set_baud(1000000);

    // Load pool config
    stratum_client_config_t stratum_cfg = {0};
    nvs_config_get_string(NVS_KEY_POOL_URL, stratum_cfg.primary.pool_url,
                          sizeof(stratum_cfg.primary.pool_url), "public-pool.io");
    stratum_cfg.primary.pool_port = nvs_config_get_u16(NVS_KEY_POOL_PORT, DEFAULT_POOL_PORT);
    nvs_config_get_string(NVS_KEY_POOL_USER, stratum_cfg.primary.pool_user,
                          sizeof(stratum_cfg.primary.pool_user), "");
    nvs_config_get_string(NVS_KEY_POOL_PASS, stratum_cfg.primary.pool_pass,
                          sizeof(stratum_cfg.primary.pool_pass), "x");
    stratum_cfg.on_notify = mining_on_notify;
    stratum_cfg.on_difficulty = mining_on_difficulty;

    ESP_ERROR_CHECK(stratum_client_init(&stratum_cfg));

    // Start tasks
    xTaskCreate(stratum_client_task, "stratum", 8192, NULL, 5, NULL);
    mining_task_start();
    result_task_start();
    hashrate_task_start();

    ESP_LOGI(TAG, "All tasks started. Free heap: %lu", esp_get_free_heap_size());
}
```

- [ ] **Step 6: Verify build**

Run: `idf.py build`
Expected: Clean build

- [ ] **Step 7: Commit**

```bash
git add main/tasks/ main/main.c
git commit -m "feat: mining, result, and hashrate FreeRTOS tasks"
```

---

## Phase 4: Power & Thermal Management

### Task 4.1: Power Component (VR, Temp, Fan, PID)

**Files:**
- Create: `components/power/CMakeLists.txt`
- Create: `components/power/include/voltage_regulator.h`
- Create: `components/power/include/temp_sensor.h`
- Create: `components/power/include/fan_controller.h`
- Create: `components/power/include/pid.h`
- Create: `components/power/voltage_regulator.c`
- Create: `components/power/temp_sensor.c`
- Create: `components/power/fan_controller.c`
- Create: `components/power/pid.c`
- Create: `test/test_pid.c`

- [ ] **Step 1: Write PID test**

```c
// test/test_pid.c
#include "unity.h"
#include "pid.h"

void test_pid_output_increases_when_below_setpoint(void)
{
    pid_controller_t pid;
    pid_init(&pid, 6.0, 0.1, 10.0, 0.0, 100.0);
    pid_set_target(&pid, 55.0);

    // Current temp is below target -> output should be low (less fan)
    double out = pid_compute(&pid, 40.0, 1.0);
    TEST_ASSERT_LESS_THAN(50.0, out);
}

void test_pid_output_maxes_when_above_setpoint(void)
{
    pid_controller_t pid;
    pid_init(&pid, 6.0, 0.1, 10.0, 0.0, 100.0);
    pid_set_target(&pid, 55.0);

    double out = pid_compute(&pid, 70.0, 1.0);
    TEST_ASSERT_GREATER_THAN(50.0, out);
}
```

- [ ] **Step 2: Create pid.h and pid.c**

```c
// components/power/include/pid.h
#pragma once

typedef struct {
    double kp, ki, kd;
    double target;
    double output_min, output_max;
    double integral;
    double prev_error;
    int    first_run;
} pid_controller_t;

void   pid_init(pid_controller_t *pid, double kp, double ki, double kd,
                double out_min, double out_max);
void   pid_set_target(pid_controller_t *pid, double target);
double pid_compute(pid_controller_t *pid, double input, double dt);
void   pid_reset(pid_controller_t *pid);
```

```c
// components/power/pid.c
#include "pid.h"

void pid_init(pid_controller_t *pid, double kp, double ki, double kd,
              double out_min, double out_max)
{
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->output_min = out_min;
    pid->output_max = out_max;
    pid->integral = 0;
    pid->prev_error = 0;
    pid->first_run = 1;
    pid->target = 0;
}

void pid_set_target(pid_controller_t *pid, double target)
{
    pid->target = target;
}

double pid_compute(pid_controller_t *pid, double input, double dt)
{
    double error = input - pid->target; // Positive = too hot

    // Proportional
    double p = pid->kp * error;

    // Integral (with anti-windup)
    pid->integral += error * dt;
    double i = pid->ki * pid->integral;

    // Derivative
    double d = 0;
    if (!pid->first_run && dt > 0) {
        d = pid->kd * (error - pid->prev_error) / dt;
    }
    pid->first_run = 0;
    pid->prev_error = error;

    double output = p + i + d;

    // Clamp
    if (output > pid->output_max) {
        output = pid->output_max;
        pid->integral -= error * dt; // Anti-windup
    }
    if (output < pid->output_min) {
        output = pid->output_min;
        pid->integral -= error * dt;
    }

    return output;
}

void pid_reset(pid_controller_t *pid)
{
    pid->integral = 0;
    pid->prev_error = 0;
    pid->first_run = 1;
}
```

- [ ] **Step 3: Create voltage_regulator.h and voltage_regulator.c**

```c
// components/power/include/voltage_regulator.h
#pragma once

#include "esp_err.h"
#include "driver/i2c.h"

typedef struct {
    i2c_port_t i2c_port;
    uint8_t    address;  // PMBus address (typically 0x47)
} vr_config_t;

typedef struct {
    float vin;
    float vout;
    float iin;
    float iout;
    float pin;
    float pout;
    float temperature;
} vr_telemetry_t;

esp_err_t vr_init(const vr_config_t *config);
esp_err_t vr_set_voltage(uint16_t millivolts);
esp_err_t vr_enable(bool enable);
esp_err_t vr_read_telemetry(vr_telemetry_t *out);
bool      vr_check_faults(void);
```

```c
// components/power/voltage_regulator.c
// TPS53647 PMBus driver
#include "voltage_regulator.h"
#include "esp_log.h"

static const char *TAG = "vr";
static vr_config_t s_config;

// PMBus commands
#define PMBUS_OPERATION     0x01
#define PMBUS_VOUT_COMMAND  0x21
#define PMBUS_VOUT_MODE     0x20
#define PMBUS_READ_VIN      0x88
#define PMBUS_READ_VOUT     0x8B
#define PMBUS_READ_IOUT     0x8C
#define PMBUS_READ_TEMP     0x8D
#define PMBUS_STATUS_BYTE   0x78
#define PMBUS_STATUS_IOUT   0x7B
#define PMBUS_STATUS_VOUT   0x7A
#define PMBUS_READ_IIN      0x89
#define PMBUS_READ_PIN      0x97
#define PMBUS_READ_POUT     0x96

static esp_err_t pmbus_write16(uint8_t cmd, uint16_t value)
{
    uint8_t data[3] = {cmd, value & 0xFF, (value >> 8) & 0xFF};
    return i2c_master_write_to_device(s_config.i2c_port, s_config.address,
                                       data, 3, pdMS_TO_TICKS(100));
}

static esp_err_t pmbus_read16(uint8_t cmd, uint16_t *value)
{
    uint8_t buf[2];
    esp_err_t err = i2c_master_write_read_device(s_config.i2c_port, s_config.address,
                                                  &cmd, 1, buf, 2, pdMS_TO_TICKS(100));
    if (err == ESP_OK) {
        *value = buf[0] | (buf[1] << 8);
    }
    return err;
}

esp_err_t vr_init(const vr_config_t *config)
{
    s_config = *config;
    ESP_LOGI(TAG, "VR init: I2C port %d, addr 0x%02X", config->i2c_port, config->address);
    return ESP_OK;
}

esp_err_t vr_set_voltage(uint16_t millivolts)
{
    // TPS53647 VOUT_COMMAND in linear format
    // Convert millivolts to PMBus linear16 (exponent = -9)
    uint16_t val = (millivolts * 512) / 1000;
    ESP_LOGI(TAG, "Setting voltage to %d mV (reg=0x%04X)", millivolts, val);
    return pmbus_write16(PMBUS_VOUT_COMMAND, val);
}

esp_err_t vr_enable(bool enable)
{
    return pmbus_write16(PMBUS_OPERATION, enable ? 0x80 : 0x00);
}

static float linear11_to_float(uint16_t raw)
{
    int16_t exponent = (int16_t)(raw >> 11);
    if (exponent > 15) exponent -= 32; // Sign extend 5-bit
    int16_t mantissa = raw & 0x7FF;
    if (mantissa > 1023) mantissa -= 2048; // Sign extend 11-bit
    return (float)mantissa * (float)(1 << (exponent >= 0 ? exponent : 0)) /
           (float)(1 << (exponent < 0 ? -exponent : 0));
}

esp_err_t vr_read_telemetry(vr_telemetry_t *out)
{
    uint16_t raw;

    if (pmbus_read16(PMBUS_READ_VIN, &raw) == ESP_OK)
        out->vin = linear11_to_float(raw);
    if (pmbus_read16(PMBUS_READ_VOUT, &raw) == ESP_OK)
        out->vout = (float)raw / 512.0f; // Linear16 with exponent -9
    if (pmbus_read16(PMBUS_READ_IOUT, &raw) == ESP_OK)
        out->iout = linear11_to_float(raw);
    if (pmbus_read16(PMBUS_READ_TEMP, &raw) == ESP_OK)
        out->temperature = linear11_to_float(raw);

    out->pout = out->vout * out->iout;
    out->pin = out->vin * (out->iout * out->vout / out->vin); // Estimate

    return ESP_OK;
}

bool vr_check_faults(void)
{
    uint16_t status;
    if (pmbus_read16(PMBUS_STATUS_BYTE, &status) != ESP_OK) return true;
    return (status & 0xFF) != 0;
}
```

- [ ] **Step 4: Create temp_sensor.h, temp_sensor.c, fan_controller.h, fan_controller.c**

```c
// components/power/include/temp_sensor.h
#pragma once
#include "esp_err.h"
#include "driver/i2c.h"

#define MAX_TEMP_SENSORS 4

typedef struct {
    i2c_port_t i2c_port;
    uint8_t    addresses[MAX_TEMP_SENSORS];
    int        count;
} temp_sensor_config_t;

esp_err_t temp_sensor_init(const temp_sensor_config_t *config);
float     temp_sensor_read(int index);
float     temp_sensor_read_max(void);
```

```c
// components/power/temp_sensor.c
#include "temp_sensor.h"
#include "esp_log.h"

static temp_sensor_config_t s_config;

// TMP1075 register
#define TMP1075_TEMP_REG 0x00

esp_err_t temp_sensor_init(const temp_sensor_config_t *config)
{
    s_config = *config;
    return ESP_OK;
}

float temp_sensor_read(int index)
{
    if (index >= s_config.count) return -1.0f;

    uint8_t reg = TMP1075_TEMP_REG;
    uint8_t data[2];
    esp_err_t err = i2c_master_write_read_device(
        s_config.i2c_port, s_config.addresses[index],
        &reg, 1, data, 2, pdMS_TO_TICKS(100));

    if (err != ESP_OK) return -1.0f;

    int16_t raw = (data[0] << 8) | data[1];
    return (float)(raw >> 4) * 0.0625f;
}

float temp_sensor_read_max(void)
{
    float max_temp = -999.0f;
    for (int i = 0; i < s_config.count; i++) {
        float t = temp_sensor_read(i);
        if (t > max_temp) max_temp = t;
    }
    return max_temp;
}
```

```c
// components/power/include/fan_controller.h
#pragma once
#include "esp_err.h"
#include "driver/i2c.h"

typedef struct {
    i2c_port_t i2c_port;
    uint8_t    address; // EMC2302 address
} fan_config_t;

esp_err_t fan_init(const fan_config_t *config);
esp_err_t fan_set_speed(int channel, uint8_t percent); // 0-100
uint16_t  fan_get_rpm(int channel);
```

```c
// components/power/fan_controller.c
#include "fan_controller.h"
#include "esp_log.h"

static fan_config_t s_config;

#define EMC2302_FAN1_SETTING    0x30
#define EMC2302_FAN2_SETTING    0x40
#define EMC2302_FAN1_TACH_READ  0x3E
#define EMC2302_FAN2_TACH_READ  0x4E

esp_err_t fan_init(const fan_config_t *config)
{
    s_config = *config;
    return ESP_OK;
}

esp_err_t fan_set_speed(int channel, uint8_t percent)
{
    if (percent > 100) percent = 100;
    uint8_t reg = (channel == 0) ? EMC2302_FAN1_SETTING : EMC2302_FAN2_SETTING;
    uint8_t val = (uint8_t)((uint16_t)percent * 255 / 100);
    uint8_t data[2] = {reg, val};
    return i2c_master_write_to_device(s_config.i2c_port, s_config.address,
                                       data, 2, pdMS_TO_TICKS(100));
}

uint16_t fan_get_rpm(int channel)
{
    uint8_t reg = (channel == 0) ? EMC2302_FAN1_TACH_READ : EMC2302_FAN2_TACH_READ;
    uint8_t data[2];
    esp_err_t err = i2c_master_write_read_device(
        s_config.i2c_port, s_config.address,
        &reg, 1, data, 2, pdMS_TO_TICKS(100));
    if (err != ESP_OK) return 0;

    uint16_t tach = (data[0] << 5) | (data[1] >> 3);
    if (tach == 0) return 0;
    return (uint16_t)(3932160 / tach); // RPM from tach count
}
```

- [ ] **Step 5: Create component CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "pid.c" "voltage_regulator.c" "temp_sensor.c" "fan_controller.c"
    INCLUDE_DIRS "include"
    REQUIRES "driver"
)
```

- [ ] **Step 6: Verify build**

Run: `idf.py build`
Expected: Clean build

- [ ] **Step 7: Commit**

```bash
git add components/power/ test/test_pid.c
git commit -m "feat: power management - VR, temp sensor, fan, PID controller"
```

---

### Task 4.2: Power Management Task

**Files:**
- Create: `main/tasks/power_task.h`
- Create: `main/tasks/power_task.c`

- [ ] **Step 1: Create power_task.h**

```c
#pragma once

#define POWER_TASK_STACK_SIZE 8192
#define POWER_TASK_PRIORITY   10

typedef struct {
    float chip_temp;
    float vr_temp;
    float board_temp;
    float vin;
    float vout;
    float power_w;
    uint16_t fan0_rpm;
    uint16_t fan1_rpm;
    bool  overheat;
    bool  vr_fault;
} power_status_t;

void power_task_start(void);
const power_status_t *power_task_get_status(void);
```

- [ ] **Step 2: Implement power_task.c**

```c
#include "power_task.h"
#include "voltage_regulator.h"
#include "temp_sensor.h"
#include "fan_controller.h"
#include "pid.h"
#include "bm1370.h"
#include "board.h"
#include "nvs_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "power";
static power_status_t s_status = {0};
static pid_controller_t s_fan_pid[2];
static int s_asic_temp_counter = 0;

static void power_task(void *param)
{
    const board_config_t *board = board_get_config();

    // Init PIDs for fan control
    pid_init(&s_fan_pid[0], 6.0, 0.1, 10.0, 0.0, 100.0);
    pid_set_target(&s_fan_pid[0], board->fan_target_temp);

    pid_init(&s_fan_pid[1], 6.0, 0.1, 10.0, 0.0, 100.0);
    pid_set_target(&s_fan_pid[1], board->vr_target_temp);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));

        // Read board temps from TMP1075
        s_status.board_temp = temp_sensor_read_max();

        // Read ASIC temp every ~30s (slow serial read)
        s_asic_temp_counter++;
        if (s_asic_temp_counter >= 15) {
            s_status.chip_temp = bm1370_read_temperature();
            s_asic_temp_counter = 0;
        }

        // Read VR telemetry
        vr_telemetry_t vr;
        if (vr_read_telemetry(&vr) == ESP_OK) {
            s_status.vr_temp = vr.temperature;
            s_status.vin = vr.vin;
            s_status.vout = vr.vout;
            s_status.power_w = vr.pout;
        }

        // Check VR faults
        s_status.vr_fault = vr_check_faults();

        // Overheat protection
        uint8_t overheat_temp = nvs_config_get_u16(NVS_KEY_OVERHEAT_TEMP, board->overheat_temp);
        s_status.overheat = (s_status.chip_temp > overheat_temp) ||
                            (s_status.vr_temp > 90.0f);

        if (s_status.overheat || s_status.vr_fault) {
            ESP_LOGE(TAG, "OVERHEAT/FAULT! chip=%.1fC vr=%.1fC fault=%d",
                     s_status.chip_temp, s_status.vr_temp, s_status.vr_fault);
            vr_set_voltage(0); // Emergency shutdown
            fan_set_speed(0, 100);
            fan_set_speed(1, 100);
            continue;
        }

        // PID fan control
        double fan0_pct = pid_compute(&s_fan_pid[0], s_status.chip_temp, 2.0);
        double fan1_pct = pid_compute(&s_fan_pid[1], s_status.vr_temp, 2.0);

        fan_set_speed(0, (uint8_t)fan0_pct);
        fan_set_speed(1, (uint8_t)fan1_pct);

        s_status.fan0_rpm = fan_get_rpm(0);
        s_status.fan1_rpm = fan_get_rpm(1);

        ESP_LOGD(TAG, "T:%.1f/%.1f P:%.1fW F:%d/%d",
                 s_status.chip_temp, s_status.vr_temp, s_status.power_w,
                 s_status.fan0_rpm, s_status.fan1_rpm);
    }
}

void power_task_start(void)
{
    xTaskCreate(power_task, "power", POWER_TASK_STACK_SIZE, NULL,
                POWER_TASK_PRIORITY, NULL);
}

const power_status_t *power_task_get_status(void)
{
    return &s_status;
}
```

- [ ] **Step 3: Verify build, commit**

```bash
git add main/tasks/power_task.*
git commit -m "feat: power management task with PID fan and overheat protection"
```

---

## Phase 5: Auto-Tuner & Self-Test

### Task 5.1: Auto-Tuner

**Files:**
- Create: `components/auto_tuner/CMakeLists.txt`
- Create: `components/auto_tuner/include/auto_tuner.h`
- Create: `components/auto_tuner/auto_tuner.c`
- Create: `main/tasks/tuner_task.h`
- Create: `main/tasks/tuner_task.c`
- Create: `test/test_auto_tuner.c`

- [ ] **Step 1: Write failing test for scoring**

```c
// test/test_auto_tuner.c
#include "unity.h"
#include "auto_tuner.h"

void test_score_prefers_higher_hashrate(void)
{
    tuner_result_t a = {.freq = 490, .voltage = 1250, .hashrate_ghs = 500.0,
                        .power_w = 15.0, .temp = 55.0, .stable = true};
    tuner_result_t b = {.freq = 525, .voltage = 1250, .hashrate_ghs = 540.0,
                        .power_w = 17.0, .temp = 58.0, .stable = true};
    TEST_ASSERT_GREATER_THAN(tuner_score(&a), tuner_score(&b));
}

void test_score_penalizes_instability(void)
{
    tuner_result_t stable = {.freq = 490, .voltage = 1250, .hashrate_ghs = 500.0,
                             .power_w = 15.0, .temp = 55.0, .stable = true};
    tuner_result_t unstable = {.freq = 490, .voltage = 1250, .hashrate_ghs = 500.0,
                               .power_w = 15.0, .temp = 55.0, .stable = false};
    TEST_ASSERT_GREATER_THAN(tuner_score(&unstable), tuner_score(&stable));
}

void test_score_penalizes_high_temp(void)
{
    tuner_result_t cool = {.freq = 490, .voltage = 1250, .hashrate_ghs = 500.0,
                           .power_w = 15.0, .temp = 50.0, .stable = true};
    tuner_result_t hot = {.freq = 490, .voltage = 1250, .hashrate_ghs = 500.0,
                          .power_w = 15.0, .temp = 68.0, .stable = true};
    TEST_ASSERT_GREATER_THAN(tuner_score(&hot), tuner_score(&cool));
}
```

- [ ] **Step 2: Create auto_tuner.h**

```c
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define TUNER_MAX_RESULTS 64

typedef struct {
    uint16_t freq;
    uint16_t voltage;
    float    hashrate_ghs;
    float    power_w;
    float    temp;
    float    efficiency_ghs_per_w;
    bool     stable;
} tuner_result_t;

typedef enum {
    TUNER_STATE_IDLE,
    TUNER_STATE_RUNNING,
    TUNER_STATE_COMPLETE,
    TUNER_STATE_ABORTED,
} tuner_state_t;

typedef struct {
    tuner_state_t state;
    int           total_steps;
    int           current_step;
    tuner_result_t results[TUNER_MAX_RESULTS];
    int           result_count;
    int           best_index;       // Index of recommended profile
    int           best_eff_index;   // Index of most efficient profile
} tuner_status_t;

// Scoring function (higher = better)
double tuner_score(const tuner_result_t *r);

// Start auto-tune sweep
void tuner_start(uint16_t freq_min, uint16_t freq_max, uint16_t freq_step,
                 uint16_t volt_min, uint16_t volt_max, uint16_t volt_step);

// Abort a running sweep
void tuner_abort(void);

// Get current status
const tuner_status_t *tuner_get_status(void);

// Apply the recommended profile
void tuner_apply_best(void);
```

- [ ] **Step 3: Implement auto_tuner.c**

```c
#include "auto_tuner.h"
#include <math.h>

static tuner_status_t s_status = {0};

double tuner_score(const tuner_result_t *r)
{
    if (!r->stable) return 0.0;

    // Score = hashrate * efficiency_bonus * temp_penalty
    double score = r->hashrate_ghs;

    // Efficiency bonus: GH/s per watt (normalized)
    double eff = (r->power_w > 0) ? r->hashrate_ghs / r->power_w : 0;
    score *= (1.0 + eff * 0.1);

    // Temperature penalty: ramp down above 60C, zero above 70C
    if (r->temp > 70.0) {
        score = 0.0;
    } else if (r->temp > 60.0) {
        score *= 1.0 - ((r->temp - 60.0) / 10.0);
    }

    return score;
}

const tuner_status_t *tuner_get_status(void)
{
    return &s_status;
}
```

- [ ] **Step 4: Implement tuner_task.c**

```c
// main/tasks/tuner_task.h
#pragma once

#define TUNER_TASK_STACK_SIZE 8192
#define TUNER_TASK_PRIORITY   5

void tuner_task_start(void);
```

```c
// main/tasks/tuner_task.c
#include "tuner_task.h"
#include "auto_tuner.h"
#include "bm1370.h"
#include "voltage_regulator.h"
#include "power_task.h"
#include "hashrate_task.h"
#include "nvs_config.h"
#include "board.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

static const char *TAG = "tuner_task";
static EventGroupHandle_t s_tuner_events;
#define TUNER_START_BIT  BIT0
#define TUNER_ABORT_BIT  BIT1

static uint16_t s_freq_min, s_freq_max, s_freq_step;
static uint16_t s_volt_min, s_volt_max, s_volt_step;

extern tuner_status_t s_status; // from auto_tuner.c - needs to be non-static or use getter

void tuner_start(uint16_t freq_min, uint16_t freq_max, uint16_t freq_step,
                 uint16_t volt_min, uint16_t volt_max, uint16_t volt_step)
{
    s_freq_min = freq_min; s_freq_max = freq_max; s_freq_step = freq_step;
    s_volt_min = volt_min; s_volt_max = volt_max; s_volt_step = volt_step;
    xEventGroupSetBits(s_tuner_events, TUNER_START_BIT);
}

void tuner_abort(void)
{
    xEventGroupSetBits(s_tuner_events, TUNER_ABORT_BIT);
}

void tuner_apply_best(void)
{
    const tuner_status_t *st = tuner_get_status();
    if (st->state != TUNER_STATE_COMPLETE || st->best_index < 0) return;

    tuner_result_t *best = (tuner_result_t *)&st->results[st->best_index];
    ESP_LOGI(TAG, "Applying best profile: %dMHz @ %dmV (%.1f GH/s, %.1fW)",
             best->freq, best->voltage, best->hashrate_ghs, best->power_w);

    vr_set_voltage(best->voltage);
    vTaskDelay(pdMS_TO_TICKS(500));
    bm1370_set_frequency(best->freq);

    nvs_config_set_u16(NVS_KEY_ASIC_FREQ, best->freq);
    nvs_config_set_u16(NVS_KEY_ASIC_VOLTAGE, best->voltage);
}

static void tuner_task(void *param)
{
    s_tuner_events = xEventGroupCreate();

    while (1) {
        // Wait for start signal
        xEventGroupWaitBits(s_tuner_events, TUNER_START_BIT, pdTRUE, pdFALSE, portMAX_DELAY);

        tuner_status_t *st = (tuner_status_t *)tuner_get_status();
        st->state = TUNER_STATE_RUNNING;
        st->result_count = 0;
        st->best_index = -1;
        st->best_eff_index = -1;

        // Calculate total steps
        int freq_steps = (s_freq_max - s_freq_min) / s_freq_step + 1;
        int volt_steps = (s_volt_max - s_volt_min) / s_volt_step + 1;
        st->total_steps = freq_steps * volt_steps;
        st->current_step = 0;

        ESP_LOGI(TAG, "Starting auto-tune: %d steps", st->total_steps);

        double best_score = 0;
        double best_eff = 0;
        bool aborted = false;

        for (uint16_t v = s_volt_min; v <= s_volt_max && !aborted; v += s_volt_step) {
            // Set voltage first (safer to go low->high)
            vr_set_voltage(v);
            vTaskDelay(pdMS_TO_TICKS(1000));

            for (uint16_t f = s_freq_min; f <= s_freq_max && !aborted; f += s_freq_step) {
                // Check abort
                EventBits_t bits = xEventGroupGetBits(s_tuner_events);
                if (bits & TUNER_ABORT_BIT) {
                    xEventGroupClearBits(s_tuner_events, TUNER_ABORT_BIT);
                    aborted = true;
                    break;
                }

                st->current_step++;
                ESP_LOGI(TAG, "Step %d/%d: %dMHz @ %dmV",
                         st->current_step, st->total_steps, f, v);

                // Apply frequency
                bm1370_set_frequency(f);

                // Stabilisation period
                vTaskDelay(pdMS_TO_TICKS(30000)); // 30s warmup

                // Sample hashrate and power
                const hashrate_info_t *hr = hashrate_task_get_info();
                const power_status_t *pwr = power_task_get_status();

                tuner_result_t *r = &st->results[st->result_count];
                r->freq = f;
                r->voltage = v;
                r->hashrate_ghs = hr->total_hashrate_ghs;
                r->power_w = pwr->power_w;
                r->temp = pwr->chip_temp;
                r->efficiency_ghs_per_w = (r->power_w > 0) ?
                    r->hashrate_ghs / r->power_w : 0;

                // Stability check: is hashrate within 5% of expected?
                float expected_ghs = f * 0.001f * BM1370_SMALL_CORE_COUNT; // rough estimate
                r->stable = (r->hashrate_ghs > expected_ghs * 0.5f) &&
                            (r->temp < 70.0f) &&
                            !pwr->overheat && !pwr->vr_fault;

                double score = tuner_score(r);
                if (score > best_score) {
                    best_score = score;
                    st->best_index = st->result_count;
                }
                if (r->stable && r->efficiency_ghs_per_w > best_eff) {
                    best_eff = r->efficiency_ghs_per_w;
                    st->best_eff_index = st->result_count;
                }

                st->result_count++;
                if (st->result_count >= TUNER_MAX_RESULTS) {
                    aborted = true;
                }
            }
        }

        st->state = aborted ? TUNER_STATE_ABORTED : TUNER_STATE_COMPLETE;

        // Restore original settings if aborted, otherwise stay at last tested
        if (aborted) {
            const board_config_t *board = board_get_config();
            uint16_t orig_freq = nvs_config_get_u16(NVS_KEY_ASIC_FREQ, board->freq_default);
            uint16_t orig_volt = nvs_config_get_u16(NVS_KEY_ASIC_VOLTAGE, board->voltage_default);
            vr_set_voltage(orig_volt);
            bm1370_set_frequency(orig_freq);
        }

        ESP_LOGI(TAG, "Auto-tune %s. Best: idx=%d",
                 aborted ? "aborted" : "complete", st->best_index);
    }
}

void tuner_task_start(void)
{
    xTaskCreate(tuner_task, "tuner", TUNER_TASK_STACK_SIZE, NULL,
                TUNER_TASK_PRIORITY, NULL);
}
```

- [ ] **Step 5: Create component CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "auto_tuner.c"
    INCLUDE_DIRS "include"
)
```

- [ ] **Step 6: Verify build, commit**

```bash
git add components/auto_tuner/ main/tasks/tuner_task.* test/test_auto_tuner.c
git commit -m "feat: auto-tuner with freq/voltage sweep and scoring"
```

---

### Task 5.2: Self-Test / POST

**Files:**
- Create: `components/self_test/CMakeLists.txt`
- Create: `components/self_test/include/self_test.h`
- Create: `components/self_test/self_test.c`

- [ ] **Step 1: Create self_test.h**

```c
#pragma once

#include <stdbool.h>

#define SELFTEST_MAX_CHECKS 10

typedef enum {
    SELFTEST_PASS,
    SELFTEST_FAIL,
    SELFTEST_WARN,
    SELFTEST_SKIP,
} selftest_result_t;

typedef struct {
    const char       *name;
    selftest_result_t result;
    char              detail[64];
} selftest_check_t;

typedef struct {
    selftest_check_t checks[SELFTEST_MAX_CHECKS];
    int              check_count;
    bool             all_pass;
} selftest_report_t;

// Run all POST checks. Blocks until complete (~5-10s).
selftest_report_t selftest_run(void);
```

- [ ] **Step 2: Implement self_test.c**

```c
#include "self_test.h"
#include "serial.h"
#include "bm1370.h"
#include "voltage_regulator.h"
#include "temp_sensor.h"
#include "fan_controller.h"
#include "nvs_config.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_psram.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "selftest";

static void add_check(selftest_report_t *r, const char *name,
                      selftest_result_t result, const char *detail)
{
    if (r->check_count >= SELFTEST_MAX_CHECKS) return;
    selftest_check_t *c = &r->checks[r->check_count++];
    c->name = name;
    c->result = result;
    strncpy(c->detail, detail, sizeof(c->detail) - 1);
    if (result == SELFTEST_FAIL) r->all_pass = false;
}

selftest_report_t selftest_run(void)
{
    selftest_report_t report = {.all_pass = true};
    ESP_LOGI(TAG, "Running POST...");

    // 1. PSRAM check
    size_t psram = esp_psram_get_size();
    if (psram > 0) {
        char detail[64];
        snprintf(detail, sizeof(detail), "%zu MB", psram / (1024 * 1024));
        add_check(&report, "PSRAM", SELFTEST_PASS, detail);
    } else {
        add_check(&report, "PSRAM", SELFTEST_FAIL, "Not detected");
    }

    // 2. Free heap
    size_t heap = esp_get_free_heap_size();
    char heap_detail[64];
    snprintf(heap_detail, sizeof(heap_detail), "%zu KB free", heap / 1024);
    add_check(&report, "Heap", (heap > 100000) ? SELFTEST_PASS : SELFTEST_WARN, heap_detail);

    // 3. NVS
    char buf[16];
    nvs_config_get_string("_selftest", buf, sizeof(buf), "ok");
    add_check(&report, "NVS", SELFTEST_PASS, "Accessible");

    // 4. UART / ASIC detection
    int chips = asic_enumerate();
    if (chips > 0) {
        char detail[64];
        snprintf(detail, sizeof(detail), "%d BM1370 found", chips);
        add_check(&report, "ASIC Chain", SELFTEST_PASS, detail);
    } else {
        add_check(&report, "ASIC Chain", SELFTEST_FAIL, "No chips detected");
    }

    // 5. Temperature sensors
    float temp = temp_sensor_read(0);
    if (temp > -40.0f && temp < 125.0f) {
        char detail[64];
        snprintf(detail, sizeof(detail), "%.1f C", temp);
        add_check(&report, "Temp Sensor", SELFTEST_PASS, detail);
    } else {
        add_check(&report, "Temp Sensor", SELFTEST_FAIL, "Read error");
    }

    // 6. Voltage regulator
    vr_telemetry_t vr;
    if (vr_read_telemetry(&vr) == ESP_OK && vr.vout > 0.5f) {
        char detail[64];
        snprintf(detail, sizeof(detail), "Vin=%.1fV Vout=%.3fV", vr.vin, vr.vout);
        add_check(&report, "VR", SELFTEST_PASS, detail);
    } else {
        add_check(&report, "VR", SELFTEST_WARN, "Cannot read telemetry");
    }

    // 7. Fan
    fan_set_speed(0, 50);
    vTaskDelay(pdMS_TO_TICKS(2000));
    uint16_t rpm = fan_get_rpm(0);
    fan_set_speed(0, 0);
    if (rpm > 0) {
        char detail[64];
        snprintf(detail, sizeof(detail), "%d RPM @ 50%%", rpm);
        add_check(&report, "Fan", SELFTEST_PASS, detail);
    } else {
        add_check(&report, "Fan", SELFTEST_WARN, "No RPM detected (may be OK)");
    }

    ESP_LOGI(TAG, "POST complete: %s", report.all_pass ? "ALL PASS" : "FAILURES");
    return report;
}
```

- [ ] **Step 3: Create component CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "self_test.c"
    INCLUDE_DIRS "include"
    REQUIRES "serial" "asic_driver" "power" "nvs_config"
)
```

- [ ] **Step 4: Verify build, commit**

```bash
git add components/self_test/
git commit -m "feat: POST self-test with PSRAM, ASIC, VR, temp, fan checks"
```

---

## Phase 6: Web UI

### Task 6.1: HTTP Server & REST API

**Files:**
- Create: `main/http_server/http_server.c`
- Create: `main/http_server/http_server.h`
- Create: `main/http_server/api_system.c`
- Create: `main/http_server/api_mining.c`
- Create: `main/http_server/api_tuner.c`
- Create: `main/http_server/ws_handler.c`

- [ ] **Step 1: Create http_server.h**

```c
#pragma once

#include "esp_err.h"

esp_err_t http_server_start(void);
void      http_server_stop(void);
```

- [ ] **Step 2: Implement http_server.c (router + SPIFFS serving)**

```c
#include "http_server.h"
#include "esp_http_server.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "http";
static httpd_handle_t s_server = NULL;

// Forward declarations for API handlers
esp_err_t api_system_info_handler(httpd_req_t *req);
esp_err_t api_system_patch_handler(httpd_req_t *req);
esp_err_t api_system_restart_handler(httpd_req_t *req);
esp_err_t api_mining_info_handler(httpd_req_t *req);
esp_err_t api_tuner_status_handler(httpd_req_t *req);
esp_err_t api_tuner_start_handler(httpd_req_t *req);
esp_err_t api_tuner_abort_handler(httpd_req_t *req);
esp_err_t api_tuner_apply_handler(httpd_req_t *req);
esp_err_t ws_handler(httpd_req_t *req);

static const char *get_content_type(const char *path)
{
    if (strstr(path, ".html")) return "text/html";
    if (strstr(path, ".js"))   return "application/javascript";
    if (strstr(path, ".css"))  return "text/css";
    if (strstr(path, ".json")) return "application/json";
    if (strstr(path, ".svg"))  return "image/svg+xml";
    if (strstr(path, ".ico"))  return "image/x-icon";
    return "application/octet-stream";
}

static esp_err_t spiffs_handler(httpd_req_t *req)
{
    char path[128] = "/www";
    if (strcmp(req->uri, "/") == 0) {
        strcat(path, "/index.html");
    } else {
        strncat(path, req->uri, sizeof(path) - strlen(path) - 1);
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        // SPA fallback: serve index.html for unknown routes
        f = fopen("/www/index.html", "r");
        if (!f) {
            httpd_resp_send_404(req);
            return ESP_OK;
        }
        httpd_resp_set_type(req, "text/html");
    } else {
        httpd_resp_set_type(req, get_content_type(path));
    }

    // Enable caching for static assets
    if (strstr(path, ".js") || strstr(path, ".css")) {
        httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
    }

    char buf[1024];
    size_t read;
    while ((read = fread(buf, 1, sizeof(buf), f)) > 0) {
        httpd_resp_send_chunk(req, buf, read);
    }
    httpd_resp_send_chunk(req, NULL, 0);
    fclose(f);
    return ESP_OK;
}

static void init_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/www",
        .partition_label = "www",
        .max_files = 5,
        .format_if_mount_failed = false,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
    }
}

esp_err_t http_server_start(void)
{
    init_spiffs();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 20;
    config.stack_size = 8192;
    config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) return err;

    // API routes
    httpd_uri_t routes[] = {
        {"/api/system/info",    HTTP_GET,  api_system_info_handler, NULL},
        {"/api/system",         HTTP_PATCH, api_system_patch_handler, NULL},  // Note: ESP HTTP server doesn't support PATCH natively, use POST
        {"/api/system/restart", HTTP_POST, api_system_restart_handler, NULL},
        {"/api/mining/info",    HTTP_GET,  api_mining_info_handler, NULL},
        {"/api/tuner/status",   HTTP_GET,  api_tuner_status_handler, NULL},
        {"/api/tuner/start",    HTTP_POST, api_tuner_start_handler, NULL},
        {"/api/tuner/abort",    HTTP_POST, api_tuner_abort_handler, NULL},
        {"/api/tuner/apply",    HTTP_POST, api_tuner_apply_handler, NULL},
        {"/api/ws",             HTTP_GET,  ws_handler, NULL},
    };

    for (int i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(s_server, &routes[i]);
    }

    // Wildcard for SPA
    httpd_uri_t spa = {"/*", HTTP_GET, spiffs_handler, NULL};
    httpd_register_uri_handler(s_server, &spa);

    ESP_LOGI(TAG, "HTTP server started");
    return ESP_OK;
}

void http_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
```

- [ ] **Step 3: Implement api_system.c**

```c
#include "esp_http_server.h"
#include "cJSON.h"
#include "nvs_config.h"
#include "board.h"
#include "power_task.h"
#include "hashrate_task.h"
#include "result_task.h"
#include "stratum_client.h"
#include "self_test.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include <string.h>

esp_err_t api_system_info_handler(httpd_req_t *req)
{
    const board_config_t *board = board_get_config();
    const power_status_t *pwr = power_task_get_status();
    const hashrate_info_t *hr = hashrate_task_get_info();
    const mining_stats_t *stats = result_task_get_stats();

    cJSON *root = cJSON_CreateObject();

    // System
    cJSON_AddStringToObject(root, "board", board->name);
    cJSON_AddStringToObject(root, "asic_model", board->asic_model);
    cJSON_AddStringToObject(root, "firmware", "AsicOS 1.0.0");
    cJSON_AddNumberToObject(root, "uptime_s", esp_timer_get_time() / 1000000);
    cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());

    // Hashrate
    cJSON_AddNumberToObject(root, "hashrate_ghs", hr->total_hashrate_ghs);
    cJSON *chips = cJSON_AddArrayToObject(root, "chip_hashrate");
    for (int i = 0; i < hr->chip_count; i++) {
        cJSON_AddItemToArray(chips, cJSON_CreateNumber(hr->per_chip_hashrate_ghs[i]));
    }

    // Power & thermal
    cJSON_AddNumberToObject(root, "chip_temp", pwr->chip_temp);
    cJSON_AddNumberToObject(root, "vr_temp", pwr->vr_temp);
    cJSON_AddNumberToObject(root, "board_temp", pwr->board_temp);
    cJSON_AddNumberToObject(root, "power_w", pwr->power_w);
    cJSON_AddNumberToObject(root, "voltage_mv", pwr->vout * 1000);
    cJSON_AddNumberToObject(root, "fan0_rpm", pwr->fan0_rpm);
    cJSON_AddNumberToObject(root, "fan1_rpm", pwr->fan1_rpm);
    cJSON_AddBoolToObject(root, "overheat", pwr->overheat);

    // Mining stats
    cJSON_AddNumberToObject(root, "accepted", stats->accepted_shares);
    cJSON_AddNumberToObject(root, "rejected", stats->rejected_shares);
    cJSON_AddNumberToObject(root, "best_diff", stats->best_difficulty);
    cJSON_AddNumberToObject(root, "pool_diff", stratum_client_get_current_difficulty());

    // Pool status
    const char *state_names[] = {"disconnected","connecting","subscribing",
                                 "authorizing","configuring","mining","error"};
    cJSON_AddStringToObject(root, "pool_state",
        state_names[stratum_client_get_state()]);

    // Config
    char pool_url[128], pool_user[128];
    nvs_config_get_string(NVS_KEY_POOL_URL, pool_url, sizeof(pool_url), "");
    nvs_config_get_string(NVS_KEY_POOL_USER, pool_user, sizeof(pool_user), "");
    cJSON_AddStringToObject(root, "pool_url", pool_url);
    cJSON_AddStringToObject(root, "pool_user", pool_user);
    cJSON_AddNumberToObject(root, "frequency", nvs_config_get_u16(NVS_KEY_ASIC_FREQ, board->freq_default));
    cJSON_AddNumberToObject(root, "voltage", nvs_config_get_u16(NVS_KEY_ASIC_VOLTAGE, board->voltage_default));

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    cJSON_free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t api_system_patch_handler(httpd_req_t *req)
{
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }

    cJSON *item;
    if ((item = cJSON_GetObjectItem(root, "pool_url")))
        nvs_config_set_string(NVS_KEY_POOL_URL, item->valuestring);
    if ((item = cJSON_GetObjectItem(root, "pool_port")))
        nvs_config_set_u16(NVS_KEY_POOL_PORT, item->valueint);
    if ((item = cJSON_GetObjectItem(root, "pool_user")))
        nvs_config_set_string(NVS_KEY_POOL_USER, item->valuestring);
    if ((item = cJSON_GetObjectItem(root, "pool_pass")))
        nvs_config_set_string(NVS_KEY_POOL_PASS, item->valuestring);
    if ((item = cJSON_GetObjectItem(root, "frequency")))
        nvs_config_set_u16(NVS_KEY_ASIC_FREQ, item->valueint);
    if ((item = cJSON_GetObjectItem(root, "voltage")))
        nvs_config_set_u16(NVS_KEY_ASIC_VOLTAGE, item->valueint);
    if ((item = cJSON_GetObjectItem(root, "wifi_ssid")))
        nvs_config_set_string(NVS_KEY_WIFI_SSID, item->valuestring);
    if ((item = cJSON_GetObjectItem(root, "wifi_pass")))
        nvs_config_set_string(NVS_KEY_WIFI_PASS, item->valuestring);
    if ((item = cJSON_GetObjectItem(root, "ui_mode")))
        nvs_config_set_string(NVS_KEY_UI_MODE, item->valuestring);

    cJSON_Delete(root);
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

esp_err_t api_system_restart_handler(httpd_req_t *req)
{
    httpd_resp_sendstr(req, "{\"status\":\"restarting\"}");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}
```

- [ ] **Step 4: Implement api_mining.c, api_tuner.c, ws_handler.c**

```c
// main/http_server/api_mining.c
#include "esp_http_server.h"
#include "cJSON.h"
#include "result_task.h"
#include "hashrate_task.h"
#include "stratum_client.h"

esp_err_t api_mining_info_handler(httpd_req_t *req)
{
    const mining_stats_t *stats = result_task_get_stats();
    const hashrate_info_t *hr = hashrate_task_get_info();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "hashrate_ghs", hr->total_hashrate_ghs);
    cJSON_AddNumberToObject(root, "accepted", stats->accepted_shares);
    cJSON_AddNumberToObject(root, "rejected", stats->rejected_shares);
    cJSON_AddNumberToObject(root, "best_diff", stats->best_difficulty);
    cJSON_AddNumberToObject(root, "pool_diff", stratum_client_get_current_difficulty());
    cJSON_AddNumberToObject(root, "total_shares", stats->total_shares);
    cJSON_AddNumberToObject(root, "duplicates", stats->duplicate_nonces);

    // Per-chip hashrate
    cJSON *chips = cJSON_AddArrayToObject(root, "chips");
    for (int i = 0; i < hr->chip_count; i++) {
        cJSON *chip = cJSON_CreateObject();
        cJSON_AddNumberToObject(chip, "id", i);
        cJSON_AddNumberToObject(chip, "hashrate_ghs", hr->per_chip_hashrate_ghs[i]);
        cJSON_AddItemToArray(chips, chip);
    }

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    cJSON_free(json);
    cJSON_Delete(root);
    return ESP_OK;
}
```

```c
// main/http_server/api_tuner.c
#include "esp_http_server.h"
#include "cJSON.h"
#include "auto_tuner.h"
#include "board.h"

esp_err_t api_tuner_status_handler(httpd_req_t *req)
{
    const tuner_status_t *st = tuner_get_status();
    cJSON *root = cJSON_CreateObject();

    const char *state_names[] = {"idle", "running", "complete", "aborted"};
    cJSON_AddStringToObject(root, "state", state_names[st->state]);
    cJSON_AddNumberToObject(root, "progress", st->total_steps > 0 ?
        (float)st->current_step / st->total_steps * 100 : 0);
    cJSON_AddNumberToObject(root, "best_index", st->best_index);
    cJSON_AddNumberToObject(root, "best_eff_index", st->best_eff_index);

    cJSON *results = cJSON_AddArrayToObject(root, "results");
    for (int i = 0; i < st->result_count; i++) {
        cJSON *r = cJSON_CreateObject();
        cJSON_AddNumberToObject(r, "freq", st->results[i].freq);
        cJSON_AddNumberToObject(r, "voltage", st->results[i].voltage);
        cJSON_AddNumberToObject(r, "hashrate", st->results[i].hashrate_ghs);
        cJSON_AddNumberToObject(r, "power", st->results[i].power_w);
        cJSON_AddNumberToObject(r, "temp", st->results[i].temp);
        cJSON_AddNumberToObject(r, "efficiency", st->results[i].efficiency_ghs_per_w);
        cJSON_AddBoolToObject(r, "stable", st->results[i].stable);
        cJSON_AddItemToArray(results, r);
    }

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    cJSON_free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t api_tuner_start_handler(httpd_req_t *req)
{
    const board_config_t *board = board_get_config();
    tuner_start(board->freq_min, board->freq_max, 25,
                board->voltage_min, board->voltage_max, 50);
    httpd_resp_sendstr(req, "{\"status\":\"started\"}");
    return ESP_OK;
}

esp_err_t api_tuner_abort_handler(httpd_req_t *req)
{
    tuner_abort();
    httpd_resp_sendstr(req, "{\"status\":\"aborting\"}");
    return ESP_OK;
}

esp_err_t api_tuner_apply_handler(httpd_req_t *req)
{
    tuner_apply_best();
    httpd_resp_sendstr(req, "{\"status\":\"applied\"}");
    return ESP_OK;
}
```

```c
// main/http_server/ws_handler.c
#include "esp_http_server.h"
#include "esp_log.h"
#include <string.h>

static httpd_handle_t s_ws_server = NULL;
static int s_ws_fd = -1;

esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        // Handshake
        s_ws_server = req->handle;
        s_ws_fd = httpd_req_to_sockfd(req);
        return ESP_OK;
    }

    // Read incoming WS frame (ping/pong etc)
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(ws_pkt));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}

// Call from log handler to push log lines to WS client
void ws_send_log(const char *msg)
{
    if (s_ws_fd < 0 || !s_ws_server) return;

    httpd_ws_frame_t ws_pkt = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)msg,
        .len = strlen(msg),
    };
    httpd_ws_send_frame_async(s_ws_server, s_ws_fd, &ws_pkt);
}
```

- [ ] **Step 5: Verify build, commit**

```bash
git add main/http_server/
git commit -m "feat: HTTP server with REST API, SPIFFS serving, WebSocket logs"
```

---

### Task 6.2: Vue 3 Web UI

**Files:**
- Create: `ui/package.json`
- Create: `ui/vite.config.ts`
- Create: `ui/tsconfig.json`
- Create: `ui/index.html`
- Create: `ui/src/main.ts`
- Create: `ui/src/App.vue`
- Create: `ui/src/router.ts`
- Create: `ui/src/stores/system.ts`
- Create: `ui/src/stores/mining.ts`
- Create: `ui/src/stores/tuner.ts`
- Create: `ui/src/composables/useApi.ts`
- Create: `ui/src/composables/useWebSocket.ts`
- Create: `ui/src/views/SimpleView.vue`
- Create: `ui/src/views/AdvancedView.vue`
- Create: `ui/src/views/TunerView.vue`
- Create: `ui/src/views/SettingsView.vue`
- Create: `ui/src/components/HashrateDial.vue`
- Create: `ui/src/components/TemperatureBar.vue`
- Create: `ui/src/components/AsicGrid.vue`
- Create: `ui/src/components/PoolStatus.vue`
- Create: `ui/src/components/LogConsole.vue`
- Create: `ui/src/components/ModeToggle.vue`

This task creates the full Vue 3 SPA. Due to the number of files, I'll list key files with complete code:

- [ ] **Step 1: Scaffold Vue project**

```bash
cd ui && npm create vite@latest . -- --template vue-ts
npm install vue-router@4 pinia
npm install -D @types/node tailwindcss postcss autoprefixer
npx tailwindcss init -p
```

- [ ] **Step 2: Create useApi.ts composable**

```typescript
// ui/src/composables/useApi.ts
const BASE = import.meta.env.DEV ? 'http://192.168.4.1' : '';

export async function api<T>(path: string, options?: RequestInit): Promise<T> {
  const res = await fetch(`${BASE}${path}`, {
    headers: { 'Content-Type': 'application/json' },
    ...options,
  });
  if (!res.ok) throw new Error(`API ${res.status}: ${res.statusText}`);
  return res.json();
}

export function apiPatch(path: string, body: Record<string, unknown>) {
  return api(path, { method: 'POST', body: JSON.stringify(body) });
}
```

- [ ] **Step 3: Create system store**

```typescript
// ui/src/stores/system.ts
import { defineStore } from 'pinia';
import { ref } from 'vue';
import { api } from '../composables/useApi';

export interface SystemInfo {
  board: string;
  asic_model: string;
  firmware: string;
  uptime_s: number;
  free_heap: number;
  hashrate_ghs: number;
  chip_hashrate: number[];
  chip_temp: number;
  vr_temp: number;
  board_temp: number;
  power_w: number;
  voltage_mv: number;
  fan0_rpm: number;
  fan1_rpm: number;
  overheat: boolean;
  accepted: number;
  rejected: number;
  best_diff: number;
  pool_diff: number;
  pool_state: string;
  pool_url: string;
  pool_user: string;
  frequency: number;
  voltage: number;
}

export const useSystemStore = defineStore('system', () => {
  const info = ref<SystemInfo | null>(null);
  const loading = ref(false);
  const error = ref<string | null>(null);

  let interval: ReturnType<typeof setInterval> | null = null;

  async function fetch_info() {
    try {
      loading.value = true;
      info.value = await api<SystemInfo>('/api/system/info');
      error.value = null;
    } catch (e: any) {
      error.value = e.message;
    } finally {
      loading.value = false;
    }
  }

  function startPolling(ms = 3000) {
    fetch_info();
    interval = setInterval(fetch_info, ms);
  }

  function stopPolling() {
    if (interval) clearInterval(interval);
  }

  return { info, loading, error, fetch_info, startPolling, stopPolling };
});
```

- [ ] **Step 4: Create router with simple/advanced modes**

```typescript
// ui/src/router.ts
import { createRouter, createWebHistory } from 'vue-router';

const router = createRouter({
  history: createWebHistory(),
  routes: [
    { path: '/',         redirect: '/simple' },
    { path: '/simple',   component: () => import('./views/SimpleView.vue') },
    { path: '/advanced', component: () => import('./views/AdvancedView.vue') },
    { path: '/tuner',    component: () => import('./views/TunerView.vue') },
    { path: '/settings', component: () => import('./views/SettingsView.vue') },
  ],
});

export default router;
```

- [ ] **Step 5: Create App.vue with mode toggle**

```vue
<!-- ui/src/App.vue -->
<script setup lang="ts">
import { useSystemStore } from './stores/system';
import { onMounted, onUnmounted } from 'vue';
import ModeToggle from './components/ModeToggle.vue';

const system = useSystemStore();
onMounted(() => system.startPolling());
onUnmounted(() => system.stopPolling());
</script>

<template>
  <div class="min-h-screen bg-gray-950 text-white">
    <header class="border-b border-gray-800 px-4 py-3 flex items-center justify-between">
      <h1 class="text-xl font-bold tracking-tight">
        <span class="text-orange-500">Asic</span>OS
      </h1>
      <div class="flex items-center gap-4">
        <ModeToggle />
        <router-link to="/settings" class="text-gray-400 hover:text-white text-sm">
          Settings
        </router-link>
      </div>
    </header>
    <main class="p-4">
      <router-view />
    </main>
  </div>
</template>
```

- [ ] **Step 6: Create SimpleView.vue (lottery mode)**

```vue
<!-- ui/src/views/SimpleView.vue -->
<script setup lang="ts">
import { useSystemStore } from '../stores/system';
import { computed } from 'vue';
import HashrateDial from '../components/HashrateDial.vue';
import TemperatureBar from '../components/TemperatureBar.vue';
import PoolStatus from '../components/PoolStatus.vue';

const system = useSystemStore();
const info = computed(() => system.info);

const uptimeFormatted = computed(() => {
  if (!info.value) return '--';
  const s = info.value.uptime_s;
  const d = Math.floor(s / 86400);
  const h = Math.floor((s % 86400) / 3600);
  const m = Math.floor((s % 3600) / 60);
  return d > 0 ? `${d}d ${h}h` : `${h}h ${m}m`;
});
</script>

<template>
  <div class="max-w-md mx-auto space-y-6">
    <!-- Big hashrate dial -->
    <HashrateDial :value="info?.hashrate_ghs ?? 0" :max="1000" />

    <!-- Key stats -->
    <div class="grid grid-cols-2 gap-4">
      <div class="bg-gray-900 rounded-xl p-4 text-center">
        <div class="text-3xl font-bold text-green-400">{{ info?.accepted ?? 0 }}</div>
        <div class="text-xs text-gray-500 mt-1">Shares Accepted</div>
      </div>
      <div class="bg-gray-900 rounded-xl p-4 text-center">
        <div class="text-3xl font-bold text-orange-400">
          {{ info?.best_diff ? info.best_diff.toExponential(1) : '0' }}
        </div>
        <div class="text-xs text-gray-500 mt-1">Best Difficulty</div>
      </div>
    </div>

    <TemperatureBar :temp="info?.chip_temp ?? 0" :max="70" label="Chip" />
    <PoolStatus :state="info?.pool_state ?? 'disconnected'" :url="info?.pool_url ?? ''" />

    <div class="text-center text-gray-600 text-xs">
      Uptime: {{ uptimeFormatted }} | {{ info?.power_w?.toFixed(1) ?? '0' }}W
    </div>
  </div>
</template>
```

- [ ] **Step 7: Create AdvancedView.vue**

```vue
<!-- ui/src/views/AdvancedView.vue -->
<script setup lang="ts">
import { useSystemStore } from '../stores/system';
import { computed } from 'vue';
import AsicGrid from '../components/AsicGrid.vue';
import PoolStatus from '../components/PoolStatus.vue';
import LogConsole from '../components/LogConsole.vue';

const system = useSystemStore();
const info = computed(() => system.info);

const efficiency = computed(() => {
  if (!info.value || !info.value.power_w) return 0;
  return info.value.hashrate_ghs / info.value.power_w;
});
</script>

<template>
  <div class="space-y-4">
    <!-- Top stats bar -->
    <div class="grid grid-cols-4 gap-3">
      <div class="bg-gray-900 rounded-lg p-3">
        <div class="text-lg font-mono font-bold">{{ info?.hashrate_ghs?.toFixed(1) ?? '0' }}</div>
        <div class="text-xs text-gray-500">GH/s Total</div>
      </div>
      <div class="bg-gray-900 rounded-lg p-3">
        <div class="text-lg font-mono font-bold">{{ efficiency.toFixed(1) }}</div>
        <div class="text-xs text-gray-500">GH/s/W</div>
      </div>
      <div class="bg-gray-900 rounded-lg p-3">
        <div class="text-lg font-mono font-bold">{{ info?.power_w?.toFixed(1) ?? '0' }}W</div>
        <div class="text-xs text-gray-500">Power</div>
      </div>
      <div class="bg-gray-900 rounded-lg p-3">
        <div class="text-lg font-mono font-bold">{{ info?.chip_temp?.toFixed(0) ?? '--' }}C</div>
        <div class="text-xs text-gray-500">Chip Temp</div>
      </div>
    </div>

    <!-- Per-chip hashrate -->
    <AsicGrid :chips="info?.chip_hashrate ?? []" />

    <!-- Detailed stats table -->
    <div class="bg-gray-900 rounded-lg p-4">
      <h3 class="text-sm font-semibold text-gray-400 mb-3">Mining Details</h3>
      <table class="w-full text-sm">
        <tbody class="divide-y divide-gray-800">
          <tr><td class="text-gray-500 py-1">Pool Difficulty</td><td class="text-right font-mono">{{ info?.pool_diff }}</td></tr>
          <tr><td class="text-gray-500 py-1">Best Difficulty</td><td class="text-right font-mono">{{ info?.best_diff?.toExponential(2) }}</td></tr>
          <tr><td class="text-gray-500 py-1">Accepted / Rejected</td><td class="text-right font-mono">{{ info?.accepted }} / {{ info?.rejected }}</td></tr>
          <tr><td class="text-gray-500 py-1">Frequency</td><td class="text-right font-mono">{{ info?.frequency }} MHz</td></tr>
          <tr><td class="text-gray-500 py-1">Core Voltage</td><td class="text-right font-mono">{{ info?.voltage }} mV</td></tr>
          <tr><td class="text-gray-500 py-1">VR Temp</td><td class="text-right font-mono">{{ info?.vr_temp?.toFixed(1) }} C</td></tr>
          <tr><td class="text-gray-500 py-1">Fan 0 / Fan 1</td><td class="text-right font-mono">{{ info?.fan0_rpm }} / {{ info?.fan1_rpm }} RPM</td></tr>
          <tr><td class="text-gray-500 py-1">Free Heap</td><td class="text-right font-mono">{{ ((info?.free_heap ?? 0) / 1024).toFixed(0) }} KB</td></tr>
        </tbody>
      </table>
    </div>

    <PoolStatus :state="info?.pool_state ?? 'disconnected'" :url="info?.pool_url ?? ''" />

    <!-- Live log console -->
    <LogConsole />

    <!-- Tuner link -->
    <router-link to="/tuner"
      class="block text-center bg-orange-600 hover:bg-orange-500 rounded-lg py-2 text-sm font-medium">
      Auto-Tuner
    </router-link>
  </div>
</template>
```

- [ ] **Step 8: Create key components (HashrateDial, ModeToggle, AsicGrid, etc.)**

```vue
<!-- ui/src/components/HashrateDial.vue -->
<script setup lang="ts">
import { computed } from 'vue';

const props = defineProps<{ value: number; max: number }>();
const pct = computed(() => Math.min(props.value / props.max * 100, 100));
const dashOffset = computed(() => 283 - (283 * pct.value / 100));
</script>

<template>
  <div class="flex flex-col items-center">
    <svg viewBox="0 0 100 100" class="w-48 h-48">
      <circle cx="50" cy="50" r="45" fill="none" stroke="#1f2937" stroke-width="8" />
      <circle cx="50" cy="50" r="45" fill="none" stroke="#f97316" stroke-width="8"
              stroke-dasharray="283" :stroke-dashoffset="dashOffset"
              stroke-linecap="round" transform="rotate(-90 50 50)"
              class="transition-all duration-1000" />
      <text x="50" y="45" text-anchor="middle" class="fill-white text-lg font-bold" font-size="14">
        {{ value.toFixed(1) }}
      </text>
      <text x="50" y="60" text-anchor="middle" class="fill-gray-500" font-size="8">
        GH/s
      </text>
    </svg>
  </div>
</template>
```

```vue
<!-- ui/src/components/ModeToggle.vue -->
<script setup lang="ts">
import { useRoute, useRouter } from 'vue-router';
import { computed } from 'vue';

const route = useRoute();
const router = useRouter();
const isAdvanced = computed(() => route.path === '/advanced');

function toggle() {
  router.push(isAdvanced.value ? '/simple' : '/advanced');
}
</script>

<template>
  <button @click="toggle"
    class="flex items-center gap-2 bg-gray-800 rounded-full px-3 py-1 text-xs">
    <span :class="!isAdvanced ? 'text-orange-400' : 'text-gray-500'">Simple</span>
    <div class="w-8 h-4 bg-gray-700 rounded-full relative cursor-pointer">
      <div class="w-3 h-3 bg-orange-500 rounded-full absolute top-0.5 transition-all"
           :class="isAdvanced ? 'left-4' : 'left-0.5'" />
    </div>
    <span :class="isAdvanced ? 'text-orange-400' : 'text-gray-500'">Advanced</span>
  </button>
</template>
```

```vue
<!-- ui/src/components/TemperatureBar.vue -->
<script setup lang="ts">
import { computed } from 'vue';
const props = defineProps<{ temp: number; max: number; label: string }>();
const pct = computed(() => Math.min(props.temp / props.max * 100, 100));
const color = computed(() => {
  if (props.temp > 65) return 'bg-red-500';
  if (props.temp > 55) return 'bg-orange-500';
  return 'bg-green-500';
});
</script>

<template>
  <div class="bg-gray-900 rounded-lg p-3">
    <div class="flex justify-between text-xs mb-1">
      <span class="text-gray-500">{{ label }}</span>
      <span class="font-mono">{{ temp.toFixed(1) }}C</span>
    </div>
    <div class="h-2 bg-gray-800 rounded-full overflow-hidden">
      <div :class="color" class="h-full rounded-full transition-all duration-500"
           :style="{ width: pct + '%' }" />
    </div>
  </div>
</template>
```

```vue
<!-- ui/src/components/AsicGrid.vue -->
<script setup lang="ts">
const props = defineProps<{ chips: number[] }>();
</script>

<template>
  <div class="bg-gray-900 rounded-lg p-4">
    <h3 class="text-sm font-semibold text-gray-400 mb-2">Per-Chip Hashrate</h3>
    <div class="grid grid-cols-4 gap-2">
      <div v-for="(hr, i) in chips" :key="i"
           class="bg-gray-800 rounded p-2 text-center">
        <div class="text-xs text-gray-500">#{{ i }}</div>
        <div class="font-mono text-sm">{{ hr.toFixed(1) }}</div>
        <div class="text-xs text-gray-600">GH/s</div>
      </div>
    </div>
  </div>
</template>
```

```vue
<!-- ui/src/components/PoolStatus.vue -->
<script setup lang="ts">
import { computed } from 'vue';
const props = defineProps<{ state: string; url: string }>();
const color = computed(() => {
  if (props.state === 'mining') return 'bg-green-500';
  if (props.state === 'error' || props.state === 'disconnected') return 'bg-red-500';
  return 'bg-yellow-500';
});
</script>

<template>
  <div class="flex items-center gap-2 bg-gray-900 rounded-lg px-3 py-2">
    <div :class="color" class="w-2 h-2 rounded-full" />
    <span class="text-sm text-gray-400">{{ url }}</span>
    <span class="text-xs text-gray-600 ml-auto">{{ state }}</span>
  </div>
</template>
```

```vue
<!-- ui/src/components/LogConsole.vue -->
<script setup lang="ts">
import { ref, onMounted, onUnmounted } from 'vue';

const logs = ref<string[]>([]);
let ws: WebSocket | null = null;

onMounted(() => {
  const host = import.meta.env.DEV ? '192.168.4.1' : window.location.host;
  ws = new WebSocket(`ws://${host}/api/ws`);
  ws.onmessage = (e) => {
    logs.value.push(e.data);
    if (logs.value.length > 200) logs.value.shift();
  };
});

onUnmounted(() => ws?.close());
</script>

<template>
  <div class="bg-gray-900 rounded-lg p-3">
    <h3 class="text-sm font-semibold text-gray-400 mb-2">Live Log</h3>
    <div class="h-40 overflow-y-auto font-mono text-xs text-gray-500 space-y-0.5">
      <div v-for="(line, i) in logs" :key="i">{{ line }}</div>
    </div>
  </div>
</template>
```

- [ ] **Step 9: Build UI and verify**

```bash
cd ui && npm run build
# Output should go to ui/dist/
# Copy to SPIFFS: will be done via build script
```

- [ ] **Step 10: Commit**

```bash
git add ui/
git commit -m "feat: Vue 3 web UI with simple/advanced modes, auto-tuner view"
```

---

## Phase 7: Remote Access & Monetisation

### Task 7.1: Remote Access Component

**Files:**
- Create: `components/remote_access/CMakeLists.txt`
- Create: `components/remote_access/include/remote_access.h`
- Create: `components/remote_access/include/licence.h`
- Create: `components/remote_access/remote_access.c`
- Create: `components/remote_access/licence.c`
- Create: `main/tasks/remote_task.h`
- Create: `main/tasks/remote_task.c`
- Create: `main/http_server/api_remote.c`
- Create: `test/test_licence.c`
- Create: `ui/src/views/RemoteView.vue`

- [ ] **Step 1: Write licence validation test**

```c
// test/test_licence.c
#include "unity.h"
#include "licence.h"

void test_valid_licence_accepted(void)
{
    // Device ID + secret -> expected key
    const char *device_id = "ASICOS-001122334455";
    const char *key = licence_generate(device_id, "test_secret_key_32bytes_long!!!!");
    TEST_ASSERT_NOT_NULL(key);
    TEST_ASSERT_TRUE(licence_validate(device_id, key, "test_secret_key_32bytes_long!!!!"));
}

void test_invalid_licence_rejected(void)
{
    TEST_ASSERT_FALSE(licence_validate("ASICOS-001122334455", "INVALID-KEY-HERE",
                                       "test_secret_key_32bytes_long!!!!"));
}

void test_wrong_device_rejected(void)
{
    const char *key = licence_generate("ASICOS-001122334455", "test_secret_key_32bytes_long!!!!");
    // Same key, different device
    TEST_ASSERT_FALSE(licence_validate("ASICOS-AABBCCDDEEFF", key,
                                       "test_secret_key_32bytes_long!!!!"));
}
```

- [ ] **Step 2: Create licence.h and licence.c**

```c
// components/remote_access/include/licence.h
#pragma once

#include <stdbool.h>

// Generate licence key for a device (server-side, included for testing)
// Returns base64-encoded HMAC-SHA256. Caller must free().
char *licence_generate(const char *device_id, const char *secret);

// Validate a licence key against device ID
bool licence_validate(const char *device_id, const char *key, const char *secret);

// Get this device's unique ID (based on MAC)
void licence_get_device_id(char *out, size_t out_len);
```

```c
// components/remote_access/licence.c
#include "licence.h"
#include "mbedtls/md.h"
#include "mbedtls/base64.h"
#include "esp_mac.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

char *licence_generate(const char *device_id, const char *secret)
{
    uint8_t hmac[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, (const uint8_t *)secret, strlen(secret));
    mbedtls_md_hmac_update(&ctx, (const uint8_t *)device_id, strlen(device_id));
    mbedtls_md_hmac_finish(&ctx, hmac);
    mbedtls_md_free(&ctx);

    size_t b64_len = 0;
    mbedtls_base64_encode(NULL, 0, &b64_len, hmac, 32);
    char *key = malloc(b64_len + 1);
    if (!key) return NULL;
    mbedtls_base64_encode((uint8_t *)key, b64_len + 1, &b64_len, hmac, 32);
    key[b64_len] = '\0';
    return key;
}

bool licence_validate(const char *device_id, const char *key, const char *secret)
{
    char *expected = licence_generate(device_id, secret);
    if (!expected) return false;
    bool valid = (strcmp(expected, key) == 0);
    free(expected);
    return valid;
}

void licence_get_device_id(char *out, size_t out_len)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, out_len, "ASICOS-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
```

- [ ] **Step 3: Create remote_access.h and remote_access.c**

```c
// components/remote_access/include/remote_access.h
#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef enum {
    REMOTE_STATE_DISABLED,
    REMOTE_STATE_CONNECTING,
    REMOTE_STATE_CONNECTED,
    REMOTE_STATE_ERROR,
    REMOTE_STATE_UNLICENSED,
} remote_state_t;

typedef struct {
    const char *relay_host;     // Cloud relay MQTT broker
    uint16_t    relay_port;
    const char *device_id;      // Unique device identifier
} remote_config_t;

esp_err_t     remote_init(const remote_config_t *config);
void          remote_task_loop(void *param); // FreeRTOS task
remote_state_t remote_get_state(void);
bool          remote_is_licensed(void);
```

```c
// components/remote_access/remote_access.c
#include "remote_access.h"
#include "licence.h"
#include "nvs_config.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "remote";
static remote_config_t s_config;
static remote_state_t s_state = REMOTE_STATE_DISABLED;
static esp_mqtt_client_handle_t s_mqtt = NULL;

// MQTT topics
// Device publishes to:  asicos/{device_id}/status  (telemetry)
// Device subscribes to: asicos/{device_id}/cmd     (remote commands)
// Device publishes to:  asicos/{device_id}/resp    (command responses)

static char s_topic_status[64];
static char s_topic_cmd[64];
static char s_topic_resp[64];

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        s_state = REMOTE_STATE_CONNECTED;
        esp_mqtt_client_subscribe(s_mqtt, s_topic_cmd, 1);
        ESP_LOGI(TAG, "Remote access connected");
        break;

    case MQTT_EVENT_DISCONNECTED:
        s_state = REMOTE_STATE_CONNECTING;
        ESP_LOGW(TAG, "Remote access disconnected");
        break;

    case MQTT_EVENT_DATA:
        // Handle incoming remote command
        // Forward to HTTP API internally and send response
        ESP_LOGI(TAG, "Remote cmd: %.*s", event->data_len, event->data);
        // TODO: Parse command, execute via internal API, publish response
        break;

    default:
        break;
    }
}

esp_err_t remote_init(const remote_config_t *config)
{
    s_config = *config;

    snprintf(s_topic_status, sizeof(s_topic_status), "asicos/%s/status", config->device_id);
    snprintf(s_topic_cmd, sizeof(s_topic_cmd), "asicos/%s/cmd", config->device_id);
    snprintf(s_topic_resp, sizeof(s_topic_resp), "asicos/%s/resp", config->device_id);

    return ESP_OK;
}

void remote_task_loop(void *param)
{
    // Check licence
    char device_id[32];
    licence_get_device_id(device_id, sizeof(device_id));

    char licence_key[64];
    nvs_config_get_string(NVS_KEY_LICENCE, licence_key, sizeof(licence_key), "");

    // Licence secret is compiled in (in production, use secure element)
    const char *licence_secret = CONFIG_ASICOS_LICENCE_SECRET;

    if (!licence_validate(device_id, licence_key, licence_secret)) {
        ESP_LOGW(TAG, "Remote access not licensed for device %s", device_id);
        s_state = REMOTE_STATE_UNLICENSED;
        // Stay in loop to allow licence activation via local API
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(10000));
            nvs_config_get_string(NVS_KEY_LICENCE, licence_key, sizeof(licence_key), "");
            if (licence_validate(device_id, licence_key, licence_secret)) {
                ESP_LOGI(TAG, "Licence activated!");
                break;
            }
        }
    }

    // Connect MQTT
    s_state = REMOTE_STATE_CONNECTING;
    char broker_uri[128];
    snprintf(broker_uri, sizeof(broker_uri), "mqtts://%s:%d",
             s_config.relay_host, s_config.relay_port);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = broker_uri,
        .credentials.client_id = device_id,
        .credentials.username = device_id,
        .credentials.authentication.password = licence_key,
    };

    s_mqtt = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(s_mqtt, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt);

    // Periodic status publishing
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));

        if (s_state == REMOTE_STATE_CONNECTED) {
            // Publish status JSON (reuse system info endpoint format)
            // TODO: Build JSON status and publish
            esp_mqtt_client_publish(s_mqtt, s_topic_status,
                                    "{\"online\":true}", 0, 1, 0);
        }
    }
}

remote_state_t remote_get_state(void)
{
    return s_state;
}

bool remote_is_licensed(void)
{
    return s_state != REMOTE_STATE_UNLICENSED;
}
```

- [ ] **Step 4: Create api_remote.c**

```c
// main/http_server/api_remote.c
#include "esp_http_server.h"
#include "cJSON.h"
#include "remote_access.h"
#include "licence.h"
#include "nvs_config.h"

esp_err_t api_remote_status_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();

    const char *states[] = {"disabled","connecting","connected","error","unlicensed"};
    cJSON_AddStringToObject(root, "state", states[remote_get_state()]);
    cJSON_AddBoolToObject(root, "licensed", remote_is_licensed());

    char device_id[32];
    licence_get_device_id(device_id, sizeof(device_id));
    cJSON_AddStringToObject(root, "device_id", device_id);

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    cJSON_free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t api_remote_activate_handler(httpd_req_t *req)
{
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }

    cJSON *key = cJSON_GetObjectItem(root, "licence_key");
    if (!key || !cJSON_IsString(key)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing licence_key");
        return ESP_OK;
    }

    nvs_config_set_string(NVS_KEY_LICENCE, key->valuestring);

    // Validate immediately
    char device_id[32];
    licence_get_device_id(device_id, sizeof(device_id));
    bool valid = licence_validate(device_id, key->valuestring,
                                  CONFIG_ASICOS_LICENCE_SECRET);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "valid", valid);
    cJSON_AddStringToObject(resp, "device_id", device_id);

    char *json = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    cJSON_free(json);
    cJSON_Delete(resp);
    cJSON_Delete(root);
    return ESP_OK;
}
```

- [ ] **Step 5: Create RemoteView.vue**

```vue
<!-- ui/src/views/RemoteView.vue -->
<script setup lang="ts">
import { ref, onMounted } from 'vue';
import { api, apiPatch } from '../composables/useApi';

interface RemoteStatus {
  state: string;
  licensed: boolean;
  device_id: string;
}

const status = ref<RemoteStatus | null>(null);
const licenceKey = ref('');
const activating = ref(false);
const activationResult = ref<string | null>(null);

async function fetchStatus() {
  status.value = await api<RemoteStatus>('/api/remote/status');
}

async function activate() {
  activating.value = true;
  activationResult.value = null;
  try {
    const res = await api<{ valid: boolean }>('/api/remote/activate', {
      method: 'POST',
      body: JSON.stringify({ licence_key: licenceKey.value }),
    });
    activationResult.value = res.valid ? 'Licence activated!' : 'Invalid licence key';
    fetchStatus();
  } catch (e: any) {
    activationResult.value = e.message;
  } finally {
    activating.value = false;
  }
}

onMounted(fetchStatus);
</script>

<template>
  <div class="max-w-md mx-auto space-y-4">
    <h2 class="text-lg font-semibold">Remote Access</h2>

    <div class="bg-gray-900 rounded-lg p-4 space-y-2">
      <div class="flex justify-between text-sm">
        <span class="text-gray-500">Status</span>
        <span :class="status?.state === 'connected' ? 'text-green-400' : 'text-gray-400'">
          {{ status?.state ?? 'loading...' }}
        </span>
      </div>
      <div class="flex justify-between text-sm">
        <span class="text-gray-500">Device ID</span>
        <span class="font-mono text-xs">{{ status?.device_id ?? '--' }}</span>
      </div>
      <div class="flex justify-between text-sm">
        <span class="text-gray-500">Licensed</span>
        <span :class="status?.licensed ? 'text-green-400' : 'text-red-400'">
          {{ status?.licensed ? 'Yes' : 'No' }}
        </span>
      </div>
    </div>

    <div v-if="!status?.licensed" class="bg-gray-900 rounded-lg p-4 space-y-3">
      <h3 class="text-sm font-semibold text-gray-400">Activate Licence</h3>
      <input v-model="licenceKey" type="text" placeholder="Enter licence key"
             class="w-full bg-gray-800 rounded px-3 py-2 text-sm font-mono" />
      <button @click="activate" :disabled="activating || !licenceKey"
              class="w-full bg-orange-600 hover:bg-orange-500 disabled:bg-gray-700 rounded py-2 text-sm">
        {{ activating ? 'Activating...' : 'Activate' }}
      </button>
      <p v-if="activationResult" class="text-xs text-center"
         :class="activationResult.includes('activated') ? 'text-green-400' : 'text-red-400'">
        {{ activationResult }}
      </p>
    </div>

    <div class="bg-gray-900 rounded-lg p-4 text-xs text-gray-600 space-y-1">
      <p>Remote access allows you to monitor and control your miner from anywhere.</p>
      <p>Purchase a licence at asicos.io/remote</p>
    </div>
  </div>
</template>
```

- [ ] **Step 6: Create component CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "remote_access.c" "licence.c"
    INCLUDE_DIRS "include"
    REQUIRES "mqtt" "mbedtls" "nvs_config"
)
```

- [ ] **Step 7: Verify build, commit**

```bash
git add components/remote_access/ main/http_server/api_remote.c main/tasks/remote_task.* test/test_licence.c ui/src/views/RemoteView.vue
git commit -m "feat: remote access via MQTT with licence system"
```

---

## Cloud Relay Architecture (Recommendation)

This section documents the recommended infrastructure for the remote access monetisation layer.

### Architecture Overview

```
[AsicOS Device] --MQTTS--> [MQTT Relay Broker] <--WSS-- [Web Dashboard]
                                    |
                              [Licence Server]
                                    |
                              [PostgreSQL DB]
                                    |
                              [Payment Gateway]
```

### Components

| Component | Technology | Purpose |
|-----------|-----------|---------|
| MQTT Relay Broker | EMQX or Mosquitto on AWS/GCP | Stateless message relay between devices and dashboard |
| Licence Server | Go or Node.js REST API | Generate/validate licence keys, handle payments |
| Web Dashboard | Vue 3 SPA (same tech as device UI) | Remote monitoring/control via WebSocket-to-MQTT bridge |
| Database | PostgreSQL | Device registry, licences, telemetry history |
| Payment | Stripe or BTCPay Server | Subscription billing (fiat + BTC) |
| CDN/Hosting | Cloudflare Pages + Workers | Dashboard hosting, API edge caching |

### MQTT Topic Structure

```
asicos/{device_id}/status    # Device -> Cloud (telemetry, 30s interval)
asicos/{device_id}/cmd       # Cloud -> Device (remote commands)
asicos/{device_id}/resp      # Device -> Cloud (command responses)
asicos/{device_id}/alert     # Device -> Cloud (overheat, fault alerts)
```

### Licence Model Options

| Tier | Price | Features |
|------|-------|----------|
| Free | $0 | Local access only, community pool support |
| Remote | $5/mo or 10k sats/mo | Remote monitoring + control via web dashboard |
| Fleet | $3/device/mo | Bulk management, API access, telemetry export |

### Security

- Device authenticates to MQTT broker using device_id + licence_key
- All MQTT traffic over TLS 1.3
- HMAC-SHA256 licence keys tied to device MAC address
- Broker ACLs restrict each device to its own topic prefix
- Rate limiting on command topic (10 cmd/min)
- Web dashboard authenticated via OAuth2 (Google/GitHub)

### Deployment

1. **Phase 1 (MVP):** Single EMQX instance on a $20/mo VPS, SQLite for licences, manual key generation
2. **Phase 2 (Scale):** EMQX cluster on Kubernetes, PostgreSQL, Stripe integration, auto-provisioning
3. **Phase 3 (Fleet):** Multi-region MQTT, telemetry pipeline (TimescaleDB), Grafana dashboards, mobile app

---

## WiFi Task (Required for networking)

### Task 6.0: WiFi STA/AP Task

**Files:**
- Create: `main/tasks/wifi_task.h`
- Create: `main/tasks/wifi_task.c`

- [ ] **Step 1: Create wifi_task.h**

```c
#pragma once

#define WIFI_TASK_STACK_SIZE 4096
#define WIFI_TASK_PRIORITY   3

void wifi_task_start(void);
bool wifi_is_connected(void);
```

- [ ] **Step 2: Implement wifi_task.c**

```c
#include "wifi_task.h"
#include "nvs_config.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "wifi";
static EventGroupHandle_t s_wifi_events;
#define WIFI_CONNECTED_BIT BIT0
static bool s_connected = false;

static void event_handler(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_connected = true;
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

static void start_ap_mode(void)
{
    ESP_LOGI(TAG, "Starting AP mode for setup...");
    esp_netif_create_default_wifi_ap();

    wifi_config_t ap_config = {
        .ap = {
            .ssid = "AsicOS-Setup",
            .ssid_len = 12,
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    esp_wifi_start();
}

void wifi_task_start(void)
{
    s_wifi_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL);

    char ssid[32], pass[64];
    nvs_config_get_string(NVS_KEY_WIFI_SSID, ssid, sizeof(ssid), "");
    nvs_config_get_string(NVS_KEY_WIFI_PASS, pass, sizeof(pass), "");

    if (strlen(ssid) == 0) {
        start_ap_mode();
        return;
    }

    wifi_config_t sta_config = {0};
    strncpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid));
    strncpy((char *)sta_config.sta.password, pass, sizeof(sta_config.sta.password));

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    esp_wifi_start();
    esp_wifi_connect();

    ESP_LOGI(TAG, "Connecting to WiFi: %s", ssid);

    // Wait for connection, fall back to AP after 15s
    EventBits_t bits = xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT,
                                            pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));
    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGW(TAG, "WiFi connection timeout, starting AP mode");
        esp_wifi_stop();
        start_ap_mode();
    }
}

bool wifi_is_connected(void)
{
    return s_connected;
}
```

- [ ] **Step 3: Verify build, commit**

```bash
git add main/tasks/wifi_task.*
git commit -m "feat: WiFi STA/AP task with setup fallback"
```

---

## Final Integration

### Task 8.1: Wire Everything Together in main.c

- [ ] **Step 1: Update main.c with full boot sequence**

The final `main.c` should follow this boot order:
1. NVS init
2. Board config load
3. Self-test (POST)
4. WiFi (STA or AP)
5. Serial / UART init
6. ASIC init + frequency ramp
7. Power subsystem init (VR, temp, fan)
8. HTTP server start
9. Stratum client start
10. Mining, result, hashrate tasks
11. Power management task
12. Auto-tuner task (idle until triggered)
13. Remote access task (if licensed)

- [ ] **Step 2: Verify full build**

Run: `idf.py build`
Expected: Clean build with all components linked

- [ ] **Step 3: Commit**

```bash
git add main/main.c main/CMakeLists.txt
git commit -m "feat: full boot sequence with all subsystems"
```

---

## Errata & Required Fixes

The following issues were identified during plan review. Each MUST be addressed during implementation:

### Critical (build-breaking)

| # | Issue | Fix |
|---|-------|-----|
| 3 | Task 2.1 CMakeLists.txt lists `asic.c` and `bm1370.c` before they exist | Only list `asic_packet.c` in Task 2.1, update CMakeLists.txt in Task 2.2 to add `asic.c` and `bm1370.c` |
| 14 | `tuner_task.c` uses `extern` on `s_status` which is `static` in `auto_tuner.c` | Remove `extern`, use `tuner_get_status()` exclusively. Make `tuner_get_status()` return mutable pointer, or add setter functions for state/results |
| 17 | `HTTP_PATCH` not supported by ESP HTTP server | Change route to `HTTP_POST` for `/api/system` settings endpoint |
| 20 | `CONFIG_ASICOS_LICENCE_SECRET` not defined | Add to `Kconfig.projbuild`: `config ASICOS_LICENCE_SECRET / string "Licence signing secret" / default "CHANGE_ME_IN_PRODUCTION_32BYTES!"` |
| 27 | `asic_enumerate()` declared but never implemented | Implement in `asic.c` as a wrapper that calls `bm1370_init()` and returns chip count, or rename references to use `bm1370_init()` directly |
| 28 | `asic_set_chip_address()` declared but never implemented | Implement in `asic.c`: build SETADDR command packet and send via serial |

### Architecture (must fix before coding)

| # | Issue | Fix |
|---|-------|-----|
| 1 | Partition table has factory + 2 OTA slots (wastes 4MB) | Drop `factory` partition. Use `ota_0` + `ota_1` only. Adjust offsets. |
| 6 | WiFi task listed after Phase 7 but needed by Phase 3 | Move WiFi task to Phase 1 (Task 1.5). It must run before stratum connects. |
| 7 | `wifi_task_start()` blocks caller for 15s | Wrap in `xTaskCreate` like other tasks, use event group for readiness signalling |
| 12 | Job storage uses ASIC job ID as index but lookup uses original job ID | Store jobs at `s_active_jobs[job->job_id]` (original), lookup by: `bm1370_asic_to_job_id(result.job_id)` -> original ID -> `s_active_jobs[original_id]` |
| 18 | Remote API routes not registered in HTTP server | Add `/api/remote/status` (GET) and `/api/remote/activate` (POST) to route table in `http_server.c` |
| 19 | `remote_task.h`/`.c` listed in file tree but never created | Create `remote_task.h` with `remote_task_start()` that wraps `xTaskCreate(remote_task_loop, ...)` |
| 26 | WebSocket handler missing `.is_websocket = true` | Add `.is_websocket = true, .handle_ws_control_frames = true` to the `/api/ws` `httpd_uri_t` |

### Correctness (fix during implementation)

| # | Issue | Fix |
|---|-------|-----|
| 4 | `stratum_detect_method()` returns static buffer (not thread-safe) | Accept caller-provided `char *method_out, size_t len` parameter |
| 5 | `stratum_client_submit_share` always uses primary pool user | Track `current_pool` pointer, use `current_pool->pool_user` |
| 11 | `mining_on_notify` race condition on `s_current_notify` | Use a FreeRTOS queue (`xQueueSend/xQueueReceive`) to pass notify structs from stratum to mining task |
| 13 | `mining_stats_t.accepted_shares` counted on send, not on pool ACK | Remove accepted/rejected from `result_task` stats. Expose `stratum_client_get_accepted/rejected()` directly to API layer. |
| 16 | Emergency shutdown calls `vr_set_voltage(0)` instead of `vr_enable(false)` | Call `vr_enable(false)` for clean shutdown |
| 23 | `vr_enable` uses `pmbus_write16` for single-byte OPERATION command | Create `pmbus_write8()` for single-byte PMBus commands, use it in `vr_enable()` |

### Missing features (add during relevant phase)

| # | Issue | Fix |
|---|-------|-----|
| 8 | No captive portal DNS | Add a minimal DNS server task (respond to all queries with 192.168.4.1) in Phase 1 WiFi task |
| 9 | No OTA update mechanism | Add `api_ota.c` with `/api/system/ota` POST endpoint using `esp_ota_ops`. Add to Phase 6. |
| 10 | SPIFFS build integration missing | Add CMake custom target: `add_custom_target(build_ui COMMAND npm run build WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/ui)` + `spiffsgen.py` step. Document in Phase 6. |
| 21 | No test runner setup | Add `test/CMakeLists.txt` using ESP-IDF's unit test app framework. Document: `cd test && idf.py build flash monitor` for on-target tests, or use a host-based Unity runner. |
| 24 | `SettingsView.vue` not implemented | Implement with forms for: WiFi SSID/password, pool URL/port/user/pass, frequency, voltage, fan target temp. POST to `/api/system`. |
| 25 | `main.ts` and `vite.config.ts` not fully defined | Provide: `main.ts` with `createApp(App).use(router).use(createPinia()).mount('#app')`. `vite.config.ts` with `base: './'` and `build.outDir: '../build/www'` |

### Low priority (address if time permits)

| # | Issue | Fix |
|---|-------|-----|
| 2 | Packet framing may not match BM1370 datasheet exactly | Verify against real hardware during Phase 2 testing |
| 15 | `overheat_temp` type mismatch (u16 vs u8) | Add `nvs_config_get_u8()` or widen board config field to `uint16_t` |
| 22 | Tight coupling: stratum depends on asic_driver for `asic_job_t` | Acceptable for now. Could extract `asic_job_t` to a shared `mining_types.h` later. |
