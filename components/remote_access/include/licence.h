#pragma once

#include <stdbool.h>
#include <stddef.h>

/**
 * Generate a licence key for the given device ID.
 * Returns base64-encoded HMAC-SHA256(secret, device_id).
 * Caller must free() the returned string.
 */
char *licence_generate(const char *device_id, const char *secret);

/**
 * Validate a licence key against the expected key for a device.
 * Returns true if the key is valid.
 */
bool licence_validate(const char *device_id, const char *key, const char *secret);

/**
 * Read the WiFi STA MAC and format as "ASICOS-XXXXXXXXXXX".
 */
void licence_get_device_id(char *out, size_t out_len);
