#include "mining.h"
#include "sha256.h"
#include <string.h>

static const double TRUEDIFFONE =
    26959535291011309493156476344723991336010898738574164086137773096960.0;

void mining_double_sha256(const uint8_t *data, size_t len, uint8_t *out)
{
    uint8_t tmp[32];
    sha256_hash(data, len, tmp);
    sha256_hash(tmp, 32, out);
}

double mining_difficulty_from_hash(const uint8_t *hash)
{
    double d = 0.0;

    /* Interpret 32-byte hash as little-endian number (matching forge-os
     * le256todouble: byte[0] is the least significant byte).
     * The double-SHA256 result is in LE byte order. */
    for (int i = 31; i >= 0; i--) {
        d = d * 256.0 + (double)hash[i];
    }

    if (d == 0.0) {
        return TRUEDIFFONE;
    }

    return TRUEDIFFONE / d;
}

void mining_compute_merkle_root(const uint8_t *coinbase_hash,
                                const uint8_t (*branches)[32],
                                int branch_count,
                                uint8_t *root_out)
{
    uint8_t current[32];
    uint8_t concat[64];

    memcpy(current, coinbase_hash, 32);

    for (int i = 0; i < branch_count; i++) {
        memcpy(concat, current, 32);
        memcpy(concat + 32, branches[i], 32);
        mining_double_sha256(concat, 64, current);
    }

    memcpy(root_out, current, 32);
}

void mining_build_block_header(uint8_t *header_out,
                               uint32_t version,
                               const uint8_t *prev_hash,
                               const uint8_t *merkle_root,
                               uint32_t ntime,
                               uint32_t nbits,
                               uint32_t nonce)
{
    /* version: 4 bytes little-endian */
    header_out[0] = (uint8_t)(version >>  0);
    header_out[1] = (uint8_t)(version >>  8);
    header_out[2] = (uint8_t)(version >> 16);
    header_out[3] = (uint8_t)(version >> 24);

    /* prev_hash: 32 bytes as-is */
    memcpy(header_out + 4, prev_hash, 32);

    /* merkle_root: 32 bytes as-is */
    memcpy(header_out + 36, merkle_root, 32);

    /* ntime: 4 bytes little-endian */
    header_out[68] = (uint8_t)(ntime >>  0);
    header_out[69] = (uint8_t)(ntime >>  8);
    header_out[70] = (uint8_t)(ntime >> 16);
    header_out[71] = (uint8_t)(ntime >> 24);

    /* nbits: 4 bytes little-endian */
    header_out[72] = (uint8_t)(nbits >>  0);
    header_out[73] = (uint8_t)(nbits >>  8);
    header_out[74] = (uint8_t)(nbits >> 16);
    header_out[75] = (uint8_t)(nbits >> 24);

    /* nonce: 4 bytes little-endian */
    header_out[76] = (uint8_t)(nonce >>  0);
    header_out[77] = (uint8_t)(nonce >>  8);
    header_out[78] = (uint8_t)(nonce >> 16);
    header_out[79] = (uint8_t)(nonce >> 24);
}

bool mining_test_nonce(const uint8_t *block_header_80,
                       double *out_difficulty)
{
    uint8_t hash[32];

    /* Header is already fully constructed (version, nonce, etc. applied).
     * Just double-SHA256 and compute difficulty, matching forge-os
     * test_nonce_value(). */
    mining_double_sha256(block_header_80, 80, hash);

    if (out_difficulty) {
        *out_difficulty = mining_difficulty_from_hash(hash);
    }

    return true;
}
