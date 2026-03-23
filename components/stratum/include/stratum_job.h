#pragma once
#include "stratum_api.h"
#include "asic.h"

int stratum_build_asic_job(const stratum_notify_t *notify, const char *extranonce1,
                           uint32_t extranonce2_counter, int extranonce2_size,
                           double pool_diff, int pool_id, asic_job_t *job_out);

int  hex_to_bytes(const char *hex, uint8_t *out, size_t out_len);
void bytes_to_hex(const uint8_t *data, size_t len, char *out);
