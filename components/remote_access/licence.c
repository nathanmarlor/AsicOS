#include "licence.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/sha256.h"
#include "mbedtls/base64.h"
#include "esp_mac.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "licence";

// Ed25519 public key - REPLACE WITH YOUR ACTUAL KEY IN PRODUCTION
// Generate keypair: openssl genpkey -algorithm Ed25519 -out private.pem
// Extract public:   openssl pkey -in private.pem -pubout -outform DER | tail -c 32 | xxd -i
const unsigned char LICENCE_PUBLIC_KEY[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};  // PLACEHOLDER - replace for production

bool licence_validate(const char *device_id, const char *licence_b64)
{
    if (!device_id || !licence_b64 || strlen(licence_b64) == 0) return false;

    // Decode base64 licence (should be 64 bytes - Ed25519 signature)
    uint8_t signature[64];
    size_t sig_len = 0;
    int ret = mbedtls_base64_decode(signature, sizeof(signature), &sig_len,
                                     (const uint8_t *)licence_b64, strlen(licence_b64));
    if (ret != 0 || sig_len != 64) {
        ESP_LOGW(TAG, "Invalid licence format (base64 decode failed or wrong length)");
        return false;
    }

    // Hash the device_id (Ed25519 signs the message directly, but we hash for consistency)
    uint8_t hash[32];
    mbedtls_sha256((const uint8_t *)device_id, strlen(device_id), hash, 0);

    // Verify Ed25519 signature using mbedtls
    mbedtls_ecdsa_context ctx;
    mbedtls_ecdsa_init(&ctx);

    // Load the public key
    mbedtls_ecp_group_load(&ctx.MBEDTLS_PRIVATE(grp), MBEDTLS_ECP_DP_CURVE25519);

    ret = mbedtls_ecp_point_read_binary(&ctx.MBEDTLS_PRIVATE(grp), &ctx.MBEDTLS_PRIVATE(Q),
                                         LICENCE_PUBLIC_KEY, 32);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to load public key: %d", ret);
        mbedtls_ecdsa_free(&ctx);
        return false;
    }

    // Verify signature
    ret = mbedtls_ecdsa_read_signature(&ctx, hash, sizeof(hash),
                                        signature, sig_len);

    mbedtls_ecdsa_free(&ctx);

    if (ret == 0) {
        ESP_LOGI(TAG, "Licence valid for device %s", device_id);
        return true;
    } else {
        ESP_LOGW(TAG, "Licence verification failed for device %s (err=%d)", device_id, ret);
        return false;
    }
}

void licence_get_device_id(char *out, size_t out_len)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, out_len, "ASICOS-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
