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
