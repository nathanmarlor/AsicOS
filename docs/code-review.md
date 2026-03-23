# AsicOS Code & UX Review

Reviewed: 2026-03-23
Scope: All firmware source files, all Vue UI source files

---

## Critical Issues

### C-CRIT-1: Race condition on `s_current_pool_diff` (no synchronisation)
**File:** `main/tasks/mining_task.c`, line 21, 38, 93
**Severity:** Critical
**Description:** `s_current_pool_diff` is written by the stratum task (via `mining_on_difficulty()` callback) and read by the mining task, with no mutex, atomic, or volatile qualifier. On the Xtensa dual-core ESP32-S3, this can cause torn reads of the `double` (64-bit) value, leading to corrupt difficulty that could cause every share to be submitted or none.
**Fix:** Either protect with the existing `s_jobs_mutex`, use `_Atomic double`, or at minimum declare `volatile` and accept the (small) risk on 64-bit tearing.

### C-CRIT-2: `mining_get_job` returns pointer to mutex-protected data without copy
**File:** `main/tasks/mining_task.c`, lines 44-57
**Severity:** Critical
**Description:** `mining_get_job()` takes the mutex, gets a pointer to `s_active_jobs[job_id]`, releases the mutex, then returns the pointer. The result_task then reads the job fields *after* the mutex is released, while the mining task may overwrite that slot concurrently. This is a use-after-unlock bug that can cause corrupted share submissions.
**Fix:** Copy the `asic_job_t` into a caller-provided buffer while holding the mutex, or hold the mutex for the duration of result processing.

### C-CRIT-3: OTA endpoint has no authentication
**File:** `main/http_server/api_system.c`, lines 240-317
**Severity:** Critical
**Description:** `POST /api/system/ota` accepts arbitrary firmware binary uploads with zero authentication. Anyone on the same network can flash malicious firmware to the device.
**Fix:** Add at minimum a shared secret / bearer token check, or require a firmware signature verification (e.g., ESP-IDF Secure Boot signing) before committing the OTA partition.

### C-CRIT-4: Settings API has no authentication
**File:** `main/http_server/api_system.c`, lines 168-222
**Severity:** Critical
**Description:** `POST /api/system` allows unauthenticated changes to WiFi credentials, pool config, ASIC frequency, and voltage. An attacker on the local network can redirect mining payouts to their own wallet.
**Fix:** Add authentication (API key, session token, or digest auth) to all mutating endpoints.

### C-CRIT-5: Licence public key is all zeros (placeholder)
**File:** `components/remote_access/licence.c`, lines 17-22
**Severity:** Critical
**Description:** `LICENCE_PUBLIC_KEY` is all zeros. The ECDSA verification will either always pass or always fail depending on the mbedtls behaviour with a zero key, effectively disabling licence validation.
**Fix:** Replace with the actual production Ed25519 public key before release.

### C-CRIT-6: Licence uses ECDSA/Curve25519, not Ed25519
**File:** `components/remote_access/licence.c`, lines 43-59
**Severity:** Critical
**Description:** Code comments say "Ed25519" but uses `mbedtls_ecdsa_context` with `MBEDTLS_ECP_DP_CURVE25519`. Curve25519 is a Diffie-Hellman curve, not a signature curve. mbedtls does not support Ed25519 natively. The signature verification will always fail.
**Fix:** Use a proper Ed25519 library (e.g., libsodium's `crypto_sign_verify_detached`, or the `esp_ed25519` component), or switch to ECDSA with SECP256R1.

### C-CRIT-7: `remote_access.c` stores licence password in stack variable used after function exits
**File:** `components/remote_access/remote_access.c`, lines 88, 126-127
**Severity:** Critical
**Description:** `licence_key` is a stack-local `char[128]` in `remote_task_loop()`. It is passed to `esp_mqtt_client_config_t.credentials.authentication.password` which just stores the pointer. After the MQTT client is started, the MQTT library may read this pointer at any time, but the `licence_key` array goes out of scope if the stack frame changes. Since the task never returns (infinite loop on line 144), this works *in practice*, but the pointer semantics are fragile.
**Fix:** Make `licence_key` static or heap-allocated to make the lifetime explicit.

---

## Important Issues

### C-IMP-1: `s_stats` in result_task accessed from multiple tasks without protection
**File:** `main/tasks/result_task.c`, line 30, 33-36
**Severity:** Important
**Description:** `s_stats` is written by the result task and read by the HTTP server task (via `result_task_get_stats()`) with no synchronisation. The `best_difficulty` field is a `double` (64-bit) which can tear on 32-bit reads. Similarly, `s_nonce_count` (uint64_t) has the same issue.
**Fix:** Use atomics, a mutex, or `volatile` + accept potential tearing.

### C-IMP-2: `power_status_t` shared between tasks without protection
**File:** `main/tasks/power_task.c`, line 42, 212-215
**Severity:** Important
**Description:** `s_status` (a multi-field struct) is written by the power task and read by HTTP handlers and the tuner task via `power_task_get_status()` which returns a raw pointer. Concurrent reads during writes can see inconsistent state (e.g., chip_temp from one cycle, power_w from another).
**Fix:** Use a mutex, or copy the struct atomically in the getter.

### C-IMP-3: `hashrate_info_t` shared without synchronisation
**File:** `main/tasks/hashrate_task.c`, line 20, 22-25
**Severity:** Important
**Description:** Same pattern: `s_info` written by hashrate task, read by HTTP handlers.
**Fix:** Mutex or atomic copy.

### C-IMP-4: `s_connected` in wifi_task not volatile
**File:** `main/tasks/wifi_task.c`, line 27
**Severity:** Important
**Description:** `s_connected` is set in the event handler (which runs on a different task context) and read by `wifi_is_connected()` from other tasks. Without `volatile`, the compiler may cache the value.
**Fix:** Declare as `volatile bool` or use atomic.

### C-IMP-5: Boot continues after I2C init failure
**File:** `main/main.c`, lines 163-166
**Severity:** Important
**Description:** If I2C init fails, the error is logged but boot continues. All subsequent I2C operations (VR, temp sensors, fans, INA260) will fail silently, and the power task will read zero temps/no faults, potentially allowing unchecked ASIC operation.
**Fix:** On I2C failure, either halt boot and signal error via LED, or set a persistent flag that the power task checks to force safe mode (fans 100%, no mining).

### C-IMP-6: ASIC enumeration called twice during boot
**File:** `main/main.c` line 235, `components/self_test/self_test.c` line 81
**Severity:** Important
**Description:** `bm1370_init()` calls `asic_enumerate()` internally, and then `selftest_run()` calls `asic_enumerate()` again. The second enumeration sends broadcast chip ID reads which may interfere with the init register sequence already written.
**Fix:** Have self_test read `asic_get_state()->chip_count` instead of re-enumerating.

### C-IMP-7: LED state set to CONNECTED before stratum actually connects
**File:** `main/main.c`, lines 266-268
**Severity:** Important
**Description:** `led_set_state(LED_STATE_CONNECTED)` is called immediately after `init_stratum()` returns, but `init_stratum()` only creates the task -- the actual TCP connection happens asynchronously. The LED shows "connected" while the device may still be connecting.
**Fix:** Move LED state changes to stratum connection callbacks, or at least wait for `STRATUM_STATE_MINING`.

### C-IMP-8: WiFi AP mode has no captive portal HTTP handler for setup
**File:** `main/tasks/wifi_task.c`, lines 52-71
**Severity:** Important
**Description:** When in AP mode, the DNS captive portal redirects all domains to 192.168.4.1, but the HTTP server only serves the SPA if SPIFFS is mounted. A first-time user with no configured SSID will see the captive portal redirect but may not get a functional UI if the SPIFFS partition is empty.
**Fix:** Ensure the SPA is always included in the SPIFFS partition, or provide a minimal hardcoded HTML setup page for AP mode.

### C-IMP-9: Tuner task modifies freq/voltage parameters without protection
**File:** `main/tasks/tuner_task.c`, lines 403-411
**Severity:** Important
**Description:** `tuner_start()` sets `s_freq_min/max` and `s_volt_min/max` from the HTTP handler task, while the tuner task reads them. There is a race between the write and the read after `BIT_START` is set.
**Fix:** Pass parameters via a struct in a queue or protected by the event group.

### C-IMP-10: `stratum_api.c` message ID counter not thread-safe
**File:** `components/stratum/stratum_api.c`, line 8
**Severity:** Important
**Description:** `s_msg_id` is a plain `int` incremented from both the stratum client task (subscribe/authorize/configure) and potentially from the result task (via `stratum_client_submit_share`). Concurrent increments could produce duplicate IDs.
**Fix:** Use `_Atomic int` or protect with a mutex.

### C-IMP-11: `stratum_build_submit` is vulnerable to format string content
**File:** `components/stratum/stratum_api.c`, lines 33-52
**Severity:** Important
**Description:** User-provided `pool_user` is inserted directly into a JSON string via `%s` in `snprintf`. If the username contains quotes or backslashes, the resulting JSON will be malformed. While not a security vulnerability per se (no format string attack), it will cause share submissions to be rejected by the pool.
**Fix:** JSON-escape the user string, or use cJSON to build the message.

### C-IMP-12: `mining.c` `le256todouble` has strict aliasing violation
**File:** `components/mining/mining.c`, lines 21-38
**Severity:** Important
**Description:** The function casts `uint8_t*` to `uint64_t*` which violates C strict aliasing rules and may produce incorrect results with aggressive compiler optimisation. Also requires 8-byte alignment which is not guaranteed for the hash buffer.
**Fix:** Use `memcpy` to load uint64_t values from the buffer instead of pointer casts.

### C-IMP-13: `sha256_midstate` produces incorrect midstate
**File:** `components/mining/sha256.c`, lines 10-35
**Severity:** Important
**Description:** The function comment acknowledges this is an approximation. A full SHA-256 of a padded 64-byte block is NOT the same as the SHA-256 internal state after processing the first 64-byte block. The midstate should be the raw H0-H7 state values. Since `midstate_count` is hardcoded to 1 and the ASIC job format sends full merkle_root (not midstate), this may not affect mining, but it means the midstate function is broken if ever used.
**Fix:** Either expose mbedtls internal state properly (requires platform-specific access), or document clearly that this function is non-functional and should not be used.

### C-IMP-14: No exponential backoff for pool reconnection
**File:** `components/stratum/stratum_client.c`, line 235
**Severity:** Important
**Description:** `retry_delay_s` is fixed at 5 seconds. If the pool is down, the device will hammer it every 5 seconds indefinitely. Some pools may blacklist the IP.
**Fix:** Implement exponential backoff with jitter (e.g., 5s, 10s, 20s, 40s, capped at 5 minutes).

### C-IMP-15: No CORS preflight (OPTIONS) handler
**File:** `main/http_server/http_server.c`
**Severity:** Important
**Description:** All API endpoints set `Access-Control-Allow-Origin: *` on responses, but there is no OPTIONS handler for CORS preflight requests. Browsers making cross-origin PUT/POST/DELETE requests with custom headers will fail the preflight check.
**Fix:** Register a wildcard OPTIONS handler that returns appropriate CORS headers (Allow-Origin, Allow-Methods, Allow-Headers).

---

## Minor Issues

### C-MIN-1: `s_fan_override` is not volatile
**File:** `main/tasks/power_task.c`, line 44
**Severity:** Minor
**Description:** Written by `power_set_fan_override()` from HTTP task, read by the power task loop. Should be `volatile int`.

### C-MIN-2: `read_body` in api_system.c and api_remote.c is duplicated
**File:** `main/http_server/api_system.c` lines 44-63, `main/http_server/api_remote.c` lines 32-51
**Severity:** Minor
**Description:** Identical `read_body()` and `set_cors()`/`send_json()` helper functions are copy-pasted across multiple API files.
**Fix:** Move to a shared `http_helpers.c`.

### C-MIN-3: `set_cors` and `send_json` duplicated across 4 API files
**File:** `api_mining.c`, `api_system.c`, `api_tuner.c`, `api_remote.c`
**Severity:** Minor
**Fix:** Centralise in a shared header/source file.

### C-MIN-4: NVS write on every best difficulty improvement
**File:** `main/tasks/result_task.c`, lines 147-149
**Severity:** Minor
**Description:** NVS flash has limited write endurance (~100K cycles). Writing `NVS_KEY_BEST_DIFF` every time a new best share is found could wear the flash on devices with high share rates. Early in mining, the best diff improves frequently.
**Fix:** Debounce NVS writes (e.g., write at most once per minute, or only on clean shutdown).

### C-MIN-5: `selftest_run` returns a large struct by value
**File:** `components/self_test/self_test.c`, line 38
**Severity:** Minor
**Description:** `selftest_report_t` contains an array of checks and is returned by value. Depending on `SELFTEST_MAX_CHECKS`, this could be hundreds of bytes copied on the stack.
**Fix:** Return via pointer parameter or allocate on heap.

### C-MIN-6: `asic_is_nonce_response` reads byte 10 without length validation
**File:** `components/asic_driver/asic_packet.c`, lines 100-107
**Severity:** Minor
**Description:** The function takes a `const uint8_t *resp` pointer but doesn't know the buffer length. If called with a buffer shorter than 11 bytes, it reads out-of-bounds.
**Fix:** Add a length parameter or document that the buffer must be at least `ASIC_RESP_SIZE`.

### C-MIN-7: DNS response parsing doesn't validate QNAME label lengths
**File:** `main/tasks/wifi_task.c`, lines 162-164
**Severity:** Minor
**Description:** The QNAME walk `q_offset += 1 + rx_buf[q_offset]` trusts the label length byte from the client. A malicious DNS query could set a label length that jumps past the buffer end. The `q_offset < len` check mitigates this partially, but a label length of 255 could skip far past the actual packet boundary.
**Fix:** Add `if (q_offset + 1 + rx_buf[q_offset] > len) break;` inside the loop.

### C-MIN-8: `loki_task` reads NVS URL every push cycle
**File:** `main/tasks/loki_task.c`, lines 197-198
**Severity:** Minor
**Description:** Reading NVS every 10-30 seconds is unnecessary overhead. The URL rarely changes.
**Fix:** Cache the URL and only re-read on a signal (e.g., after settings are saved).

### C-MIN-9: `asic_enumerate` bulk read buffer is only 128 bytes
**File:** `components/asic_driver/asic.c`, line 181
**Severity:** Minor
**Description:** Each chip response is 11 bytes (`ASIC_RESP_SIZE`). With 128 bytes, only ~11 chips can be detected. If `expected_chip_count` grows beyond 11, responses will be truncated.
**Fix:** Size the buffer based on `ASIC_RESP_SIZE * MAX_EXPECTED_CHIPS`.

### C-MIN-10: `i2c_mux_select` has a fixed 10ms delay every call
**File:** `components/power/i2c_mux.c`, line 42
**Severity:** Minor
**Description:** Every mux channel select adds 10ms latency. With 2 EMC2101 sensors and 2 fan reads per power cycle, that is 40-80ms of unnecessary delay per 2s poll.
**Fix:** Reduce to 1ms or check if the channel is already selected before switching.

### C-MIN-11: `ws_handler.c` only supports a single WebSocket client
**File:** `main/http_server/ws_handler.c`, lines 13-14
**Severity:** Minor
**Description:** `s_ws_fd` is a single integer. If a second client connects, it overwrites the first, silently disconnecting it. The first client's socket will leak.
**Fix:** Track a small array of connected FDs, or reject additional connections with an error.

### C-MIN-12: Fan speed logged at INFO level every 2 seconds
**File:** `components/power/fan_controller.c`, line 78, 86
**Severity:** Minor
**Description:** `fan_set_speed` logs at INFO level every time it is called (every 2s from power_task). This floods the log.
**Fix:** Change to `ESP_LOGD` or only log on speed changes.

### C-MIN-13: `s_pending_ids` in stratum_client.c can overflow
**File:** `components/stratum/stratum_client.c`, lines 28-36
**Severity:** Minor
**Description:** `add_pending_id` silently drops IDs when `s_pending_count >= MAX_PENDING_IDS` (64). If shares are submitted faster than they are acknowledged, the counter will stop tracking, and accepted/rejected counts will be inaccurate.
**Fix:** Increase buffer or implement a circular buffer with eviction of oldest IDs.

### C-MIN-14: `stratum_transport.c` TLS path lacks receive timeout
**File:** `components/stratum/stratum_transport.c`, lines 151-157
**Severity:** Minor
**Description:** For TCP sockets, `SO_RCVTIMEO` is set per `recv_line` call. For TLS, there is no equivalent timeout configuration on `esp_tls_conn_read()`. A hung TLS connection will block the stratum task indefinitely.
**Fix:** Set `esp_tls_cfg_t.timeout_ms` or use `esp_tls_conn_read()` with a poll/select wrapper.

### C-MIN-15: Tuner temperature reading conflicts with power_task
**File:** `main/tasks/tuner_task.c`, line 84
**Severity:** Minor
**Description:** `bm1370_read_temperature()` sends a UART command and reads the response. This conflicts with the result_task which is also reading UART nonces. The comment in power_task.c (lines 100-105) explicitly notes this conflict, yet the tuner calls it anyway.
**Fix:** Use `power_task_get_status()->chip_temp` instead of reading UART directly.

---

## UX Findings

### UX-CRIT-1: No feedback on settings save requiring restart
**File:** `ui/src/views/SettingsView.vue`
**Severity:** Important
**Description:** When the user changes WiFi SSID, pool URL, frequency, or voltage and clicks "Save", the response is "Saved" -- but none of these settings take effect until the device is restarted. The user has no indication that a restart is required.
**Fix:** After save, show a persistent banner: "Settings saved. Restart required for changes to take effect." with a restart button.

### UX-CRIT-2: Voltage slider range allows unsafe values
**File:** `ui/src/views/SettingsView.vue`, lines 214-226
**Severity:** Important
**Description:** The voltage slider goes from 800mV to 1500mV with no board-specific limits. Setting 1500mV on a board with a max of 1300mV can physically damage the ASIC. The API also does not validate against board limits.
**Fix:** Fetch `voltage_min`/`voltage_max` from the board config API and use those as slider bounds. Add server-side validation in `api_system_patch_handler`.

### UX-CRIT-3: No server-side input validation on frequency/voltage settings
**File:** `main/http_server/api_system.c`, lines 197-201
**Severity:** Important
**Description:** The HTTP handler blindly writes whatever frequency/voltage the client sends to NVS. A value of 0 MHz or 65535 mV will be accepted and applied on next boot, potentially bricking the device.
**Fix:** Validate against `board->freq_min`, `board->freq_max`, `board->voltage_min`, `board->voltage_max`.

### UX-1: Hardcoded network difficulty in solo block time estimate
**File:** `ui/src/views/SimpleView.vue`, line 105
**Severity:** Minor
**Description:** `networkDiff` is hardcoded to `80e12`. The actual network difficulty changes. The displayed estimate will drift significantly over time.
**Fix:** Either fetch current network difficulty from a public API, or remove the estimate, or add a prominent "approximate" disclaimer.

### UX-2: No loading states for API calls
**File:** Various views
**Severity:** Minor
**Description:** The settings page has a `saving` ref, but the main dashboard views (`SimpleView`, `AdvancedView`) show `0` values on initial load before the first poll completes. There are no skeleton loaders or loading indicators.
**Fix:** Show loading spinners or skeleton placeholders until `system.info` and `mining.info` are populated.

### UX-3: Share feed shows synthetic data, not real shares
**File:** `ui/src/stores/mining.ts`, lines 70-99
**Severity:** Minor
**Description:** The share feed generates fake share entries with `Math.random()` difficulty values based on nonce count deltas. Users see fabricated difficulty numbers that don't correspond to any real share. This is misleading.
**Fix:** Either have the firmware send actual share events via WebSocket, or clearly label the feed as "estimated" / "approximate".

### UX-4: No error recovery or stale data indicator
**File:** `ui/src/stores/system.ts`, `ui/src/stores/mining.ts`
**Severity:** Minor
**Description:** If the polling API call fails (e.g., device reboots), `error.value` is set but there is no visual indicator in the dashboard. The displayed values become stale with no user-visible warning.
**Fix:** Show a disconnection banner when `error` is non-null, and grey out or timestamp the stale data.

### UX-5: Thermal/fan settings not persisted to API
**File:** `ui/src/views/SettingsView.vue`, lines 31-32, 75-91
**Severity:** Minor
**Description:** `fanTargetTemp` and `overheatTemp` are shown in the settings form but are never sent in the `save()` payload. The values are hardcoded defaults (65, 95) and changing the sliders does nothing.
**Fix:** Either add NVS keys for these settings and include them in the save payload, or remove the sliders to avoid confusing users.

### UX-6: Fallback pool settings not persisted
**File:** `ui/src/views/SettingsView.vue`, lines 162-185
**Severity:** Minor
**Description:** The fallback pool section has URL/port/user fields, but the `save()` function never sends `pool2_url`, `pool2_port`, or `pool2_user` to the API.
**Fix:** Include fallback pool fields in the save payload.

### UX-7: `confirmRestart` in SettingsView is a plain variable, not reactive
**File:** `ui/src/views/SettingsView.vue`, line 49
**Severity:** Minor
**Description:** `let confirmRestart = false` is not wrapped in `ref()`. The template expression `{{ confirmRestart ? 'Confirm Restart?' : 'Restart Device' }}` will never update reactively.
**Fix:** Change to `const confirmRestart = ref(false)`.

### UX-8: Tuner results table field name mismatch
**File:** `ui/src/views/TunerView.vue`, line 151
**Severity:** Minor
**Description:** The template accesses `r.frequency` but the API returns `freq` (see `api_tuner.c` line 53). This will show `undefined` in the results table.
**Fix:** Use `r.freq` instead of `r.frequency`, or update the `TunerResult` interface to match.

### UX-9: Profile cards in TunerView lack accessibility
**File:** `ui/src/views/TunerView.vue`
**Severity:** Minor
**Description:** Profile buttons ("Apply Eco", "Apply Balanced", "Apply Power") have no confirmation step and no loading/success feedback. Clicking immediately changes ASIC frequency and voltage with no undo.
**Fix:** Add a confirmation dialog or a 3-second "Confirm?" pattern (like the restart button).

### UX-10: Color contrast fails WCAG AA for text-muted
**File:** `ui/src/style.css`, line 12
**Severity:** Minor
**Description:** `--text-muted: #4b4b4b` on `--bg: #0a0a0a` is approximately 3.0:1 contrast ratio, which fails WCAG AA (requires 4.5:1 for normal text). Many labels use this color.
**Fix:** Lighten `--text-muted` to at least `#767676` for AA compliance.

### UX-11: No keyboard navigation support
**File:** Various
**Severity:** Minor
**Description:** None of the interactive elements (mode toggle, theme toggle, profile cards) have visible focus indicators or `aria-` labels.
**Fix:** Add `:focus-visible` ring styles and ARIA attributes.

### UX-12: 320px mobile layout issues
**File:** `ui/src/views/AdvancedView.vue`
**Severity:** Minor
**Description:** The AdvancedView uses `grid-cols-6` for KPI cards at `lg` breakpoint, but at 320px mobile the `grid-cols-2` still produces cards that are too narrow to display the sparkline and value. The status banner text overflows.
**Fix:** Use `grid-cols-1` on very small screens (`<375px`), and add `overflow-hidden text-ellipsis` on the status banner.

### UX-13: Remote Access view shows numeric state, not human-readable
**File:** `ui/src/views/RemoteView.vue`, `main/http_server/api_remote.c` line 60
**Severity:** Minor
**Description:** `remote_get_state()` returns an enum integer (0-4), and the API sends `"state": 0`. The RemoteView does not map this to "disabled"/"connecting"/"connected"/etc. The `status.connected` property referenced in the template is never set by the API.
**Fix:** Map the numeric state to a string on the firmware side (like `stratum_state_str`), and align the Vue template to use `state` instead of `connected`.

### UX-14: WebSocket log console may not receive any data
**File:** `main/main.c`, lines 258-260
**Severity:** Minor
**Description:** `ws_log_init()` is commented out, meaning the WebSocket log hook is disabled. The `LogConsole` component in the advanced view will never receive any log data.
**Fix:** Either re-enable `ws_log_init()`, or implement a non-intrusive log forwarding mechanism, or show a message in the console saying "Log forwarding is disabled."

---

## Recommendations

### Architecture
1. **Add authentication layer.** All mutating HTTP endpoints should require a bearer token or session cookie. At minimum, add a configurable password stored in NVS.
2. **Centralise shared state access.** Create a `system_state_t` struct with a single mutex, updated atomically by each task. HTTP handlers read a snapshot, eliminating all the individual race conditions.
3. **Add a watchdog task.** Monitor that all critical tasks (mining, result, power, stratum) are alive. Restart the device if any task stops responding.
4. **Implement OTA signature verification.** Use ESP-IDF's Secure Boot V2 or app signature verification to prevent malicious firmware uploads.

### Reliability
5. **Add exponential backoff with jitter** for pool reconnection and WiFi reconnection.
6. **Debounce NVS writes.** Batch them or write on a timer to preserve flash endurance.
7. **Add a safe mode.** If the VR or temp sensor fails at boot, run fans at 100% and refuse to enable the ASIC.
8. **Handle pool disconnect in mining_task.** Currently if the stratum connection drops mid-job, the mining task keeps sending jobs with stale data. Pause job dispatch when `stratum_client_get_state() != STRATUM_STATE_MINING`.

### Performance
9. **Reduce I2C mux settle delay** from 10ms to 1-2ms (PAC9544 datasheet specifies <1ms).
10. **Lower fan log verbosity** from INFO to DEBUG.
11. **Cache NVS reads** in the Loki task and settings API.

### UI
12. **Show "restart required" banner** after saving settings that need a reboot.
13. **Fetch board limits** (freq min/max, voltage min/max) from the API and use them for slider bounds.
14. **Replace synthetic share feed** with real share events from WebSocket or polling.
15. **Add visible error/disconnection state** when polling fails.
16. **Fix TunerView field name mismatch** (`frequency` vs `freq`).
17. **Fix SettingsView non-reactive `confirmRestart`**.
18. **Persist fallback pool and thermal settings** through the save flow.
19. **Improve colour contrast** for WCAG AA compliance.
20. **Add a "Factory Reset" button** in settings to clear NVS and restart.
