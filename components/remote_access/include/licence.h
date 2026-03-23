#pragma once
#include <stdbool.h>
#include <stddef.h>

// Ed25519 public key (compiled into firmware, safe to distribute)
// This is the verification key - it CANNOT be used to generate licences
// The corresponding private key is kept on the licence server only
extern const unsigned char LICENCE_PUBLIC_KEY[32];

// Validate a licence. The licence is a base64-encoded Ed25519 signature of the device_id.
// Returns true if the signature is valid for this device_id.
bool licence_validate(const char *device_id, const char *licence_b64);

// Get this device's unique ID (based on WiFi MAC)
void licence_get_device_id(char *out, size_t out_len);
