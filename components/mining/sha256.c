#include "sha256.h"
#include "mbedtls/sha256.h"
#include "mbedtls/private_access.h"
#include <string.h>

void sha256_hash(const uint8_t *data, size_t len, uint8_t *out)
{
    mbedtls_sha256(data, len, out, 0);
}

void sha256_midstate(const uint8_t *block_header_64, uint8_t *midstate)
{
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, block_header_64, 64);

    /* Extract internal H0..H7 state as big-endian bytes */
    for (int i = 0; i < 8; i++) {
        uint32_t val = ctx.MBEDTLS_PRIVATE(state)[i];
        midstate[i * 4 + 0] = (uint8_t)(val >> 24);
        midstate[i * 4 + 1] = (uint8_t)(val >> 16);
        midstate[i * 4 + 2] = (uint8_t)(val >>  8);
        midstate[i * 4 + 3] = (uint8_t)(val >>  0);
    }

    mbedtls_sha256_free(&ctx);
}
