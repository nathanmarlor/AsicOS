#include "licence.h"

#include "mbedtls/md.h"
#include "mbedtls/base64.h"
#include "esp_mac.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

char *licence_generate(const char *device_id, const char *secret)
{
    unsigned char hmac[32]; /* SHA-256 output */

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);

    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md_info) {
        mbedtls_md_free(&ctx);
        return NULL;
    }

    if (mbedtls_md_setup(&ctx, md_info, 1) != 0) {
        mbedtls_md_free(&ctx);
        return NULL;
    }

    if (mbedtls_md_hmac_starts(&ctx, (const unsigned char *)secret, strlen(secret)) != 0 ||
        mbedtls_md_hmac_update(&ctx, (const unsigned char *)device_id, strlen(device_id)) != 0 ||
        mbedtls_md_hmac_finish(&ctx, hmac) != 0) {
        mbedtls_md_free(&ctx);
        return NULL;
    }

    mbedtls_md_free(&ctx);

    /* Base64 encode: 32 bytes -> 44 chars + null */
    size_t b64_len = 0;
    mbedtls_base64_encode(NULL, 0, &b64_len, hmac, sizeof(hmac));

    char *b64 = malloc(b64_len + 1);
    if (!b64) {
        return NULL;
    }

    if (mbedtls_base64_encode((unsigned char *)b64, b64_len + 1, &b64_len,
                              hmac, sizeof(hmac)) != 0) {
        free(b64);
        return NULL;
    }

    b64[b64_len] = '\0';
    return b64;
}

bool licence_validate(const char *device_id, const char *key, const char *secret)
{
    if (!device_id || !key || !secret) {
        return false;
    }

    char *expected = licence_generate(device_id, secret);
    if (!expected) {
        return false;
    }

    bool valid = (strcmp(expected, key) == 0);
    free(expected);
    return valid;
}

void licence_get_device_id(char *out, size_t out_len)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    snprintf(out, out_len, "ASICOS-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
