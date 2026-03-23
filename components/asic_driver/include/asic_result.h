#pragma once
#include <stdint.h>

typedef struct {
    uint32_t nonce;
    uint8_t  midstate_num;
    uint8_t  job_id;
    uint16_t rolled_version;
} asic_result_t;
