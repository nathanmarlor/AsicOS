#include "stratum_job.h"
#include "mining.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

int hex_to_bytes(const char *hex, uint8_t *out, size_t out_len)
{
    size_t hex_len = strlen(hex);
    if (hex_len % 2 != 0) return -1;
    size_t byte_len = hex_len / 2;
    if (byte_len > out_len) return -1;

    for (size_t i = 0; i < byte_len; i++) {
        unsigned int val;
        char tmp[3] = { hex[i * 2], hex[i * 2 + 1], '\0' };
        if (sscanf(tmp, "%02x", &val) != 1)
            return -1;
        out[i] = (uint8_t)val;
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

int stratum_build_asic_job(const stratum_notify_t *notify, const char *extranonce1,
                           uint32_t extranonce2_counter, int extranonce2_size,
                           double pool_diff, int pool_id_val, asic_job_t *job_out)
{
    if (!notify || !extranonce1 || !job_out) return -1;

    memset(job_out, 0, sizeof(*job_out));

    /* Build extranonce2 hex from counter in little-endian byte order.
     * Matches extranonce_2_generate() which uses bin2hex on
     * the raw (little-endian) bytes of the uint32_t counter. */
    char extranonce2_hex[64];
    memset(extranonce2_hex, '0', sizeof(extranonce2_hex));
    extranonce2_hex[extranonce2_size * 2] = '\0';
    /* Write counter bytes in little-endian order (LSB first) */
    uint32_t counter_copy = extranonce2_counter;
    for (int i = 0; i < extranonce2_size && i < 4; i++) {
        sprintf(extranonce2_hex + i * 2, "%02x", (uint8_t)(counter_copy & 0xFF));
        counter_copy >>= 8;
    }
    /* Ensure null terminator at correct position (sprintf may have overwritten it) */
    extranonce2_hex[extranonce2_size * 2] = '\0';

    /* Construct coinbase: coinbase1 + extranonce1 + extranonce2 + coinbase2 */
    size_t cb1_len = strlen(notify->coinbase_1);
    size_t en1_len = strlen(extranonce1);
    size_t en2_len = extranonce2_size * 2;
    size_t cb2_len = strlen(notify->coinbase_2);
    size_t coinbase_hex_len = cb1_len + en1_len + en2_len + cb2_len;

    char *coinbase_hex = malloc(coinbase_hex_len + 1);
    if (!coinbase_hex) return -1;

    memcpy(coinbase_hex, notify->coinbase_1, cb1_len);
    memcpy(coinbase_hex + cb1_len, extranonce1, en1_len);
    memcpy(coinbase_hex + cb1_len + en1_len, extranonce2_hex, en2_len);
    memcpy(coinbase_hex + cb1_len + en1_len + en2_len, notify->coinbase_2, cb2_len);
    coinbase_hex[coinbase_hex_len] = '\0';

    /* Convert coinbase hex to bytes */
    size_t coinbase_byte_len = coinbase_hex_len / 2;
    uint8_t *coinbase = malloc(coinbase_byte_len);
    if (!coinbase) {
        free(coinbase_hex);
        return -1;
    }

    if (hex_to_bytes(coinbase_hex, coinbase, coinbase_byte_len) < 0) {
        free(coinbase_hex);
        free(coinbase);
        return -1;
    }
    free(coinbase_hex);

    /* Double-SHA256 the coinbase */
    uint8_t coinbase_hash[32];
    mining_double_sha256(coinbase, coinbase_byte_len, coinbase_hash);
    free(coinbase);

    /* Parse merkle branches from hex to binary */
    uint8_t branches[STRATUM_MAX_MERKLE_BRANCHES][32];
    for (int i = 0; i < notify->merkle_branch_count; i++) {
        if (hex_to_bytes(notify->merkle_branches[i], branches[i], 32) != 32)
            return -1;
    }

    /* Compute merkle root (result is in standard byte order) */
    uint8_t merkle_root[32];
    mining_compute_merkle_root(coinbase_hash, branches,
                               notify->merkle_branch_count, merkle_root);

    /* Parse version, nbits, ntime from hex strings */
    uint32_t version = (uint32_t)strtoul(notify->version, NULL, 16);
    uint32_t nbits   = (uint32_t)strtoul(notify->nbits, NULL, 16);
    uint32_t ntime   = (uint32_t)strtoul(notify->ntime, NULL, 16);

    /* Parse prev_block_hash from hex.
     * Stratum sends prev_block_hash with each 4-byte word byte-swapped
     * relative to the internal byte order used in block header hashing.
     * We must reverse each 4-byte word to get the correct format.
     * This matches swap_endian_words(params->prev_block_hash, ...). */
    uint8_t prev_hash_raw[32];
    uint8_t prev_hash[32];
    if (hex_to_bytes(notify->prev_block_hash, prev_hash_raw, 32) != 32) {
        memset(prev_hash_raw, 0, sizeof(prev_hash_raw));
        hex_to_bytes(notify->prev_block_hash, prev_hash_raw, sizeof(prev_hash_raw));
    }
    /* Swap bytes within each 4-byte word (stratum wire -> internal LE) */
    for (int i = 0; i < 32; i += 4) {
        prev_hash[i + 0] = prev_hash_raw[i + 3];
        prev_hash[i + 1] = prev_hash_raw[i + 2];
        prev_hash[i + 2] = prev_hash_raw[i + 1];
        prev_hash[i + 3] = prev_hash_raw[i + 0];
    }

    /* Fill the asic_job_t with standard byte order.
     * The ASIC-specific endian transformations are applied in bm1370_send_work(). */
    job_out->version = version;
    job_out->nbits   = nbits;
    job_out->ntime   = ntime;
    memcpy(job_out->merkle_root, merkle_root, 32);
    memcpy(job_out->prev_block_hash, prev_hash, 32);
    job_out->starting_nonce = 0;
    job_out->midstate_count = 1;
    job_out->pool_diff = pool_diff;
    job_out->pool_id   = pool_id_val;

    /* Metadata for share submission */
    snprintf(job_out->stratum_job_id, sizeof(job_out->stratum_job_id), "%s", notify->job_id);
    snprintf(job_out->extranonce2, sizeof(job_out->extranonce2), "%s", extranonce2_hex);

    return 0;
}
