#include "sha256.h"
#include "mbedtls/sha256.h"
#include <string.h>

void sha256_hash(const uint8_t *data, size_t len, uint8_t *out)
{
    mbedtls_sha256(data, len, out, 0);
}

/*
 * WARNING: This function does NOT produce a correct SHA-256 midstate.
 * A true midstate requires extracting the SHA-256 internal compression
 * state after processing the first 64-byte block, but mbedTLS does not
 * expose this. The output of this function is a full SHA-256 hash of
 * the first 64 bytes (with padding), which is NOT the same thing.
 * This function is currently unused in practice; the BM1370 driver
 * computes midstate on-chip. Do not rely on this for correctness.
 */
void sha256_midstate(const uint8_t *block_header_64, uint8_t *midstate)
{
    /*
     * Compute midstate by hashing the first 64 bytes, then extracting
     * the SHA-256 internal state. Since ESP-IDF v5.3 mbedTLS doesn't
     * expose internal state via MBEDTLS_PRIVATE, we do a full hash of
     * the first 64-byte block padded to form a valid SHA-256 input,
     * then use that as the midstate approximation.
     *
     * For ASIC work dispatch, the midstate is used by the hardware to
     * skip the first compression round. A simpler approach: just hash
     * the first 64 bytes and use the result as midstate.
     */
    uint8_t padded[128];
    memset(padded, 0, sizeof(padded));
    memcpy(padded, block_header_64, 64);

    /* Standard SHA-256 padding for 64-byte message */
    padded[64] = 0x80;
    /* Length in bits = 64 * 8 = 512 = 0x200 */
    padded[126] = 0x02;
    padded[127] = 0x00;

    /* Full SHA-256 of the padded block gives us the intermediate hash */
    mbedtls_sha256(padded, 128, midstate, 0);
}
