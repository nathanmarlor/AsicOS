#pragma once
#include <stdint.h>
#include <stddef.h>

void sha256_hash(const uint8_t *data, size_t len, uint8_t *out);
void sha256_midstate(const uint8_t *block_header_64, uint8_t *midstate);
