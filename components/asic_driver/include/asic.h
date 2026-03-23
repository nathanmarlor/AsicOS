#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "asic_result.h"

// BM1370 registers
#define ASIC_REG_CHIP_ID        0x00
#define ASIC_REG_PLL            0x08
#define ASIC_REG_VR_FREQ        0x10
#define ASIC_REG_TICKET_MASK    0x14
#define ASIC_REG_MISC_CTRL      0x18
#define ASIC_REG_FAST_UART      0x28
#define ASIC_REG_CORE_CTRL      0x3C
#define ASIC_REG_ANALOG_MUX     0x54
#define ASIC_REG_IO_DRIVE       0x58
#define ASIC_REG_DOMAIN_0       0x88  /* Domain 0 hash count */
#define ASIC_REG_DOMAIN_1       0x89  /* Domain 1 hash count */
#define ASIC_REG_DOMAIN_2       0x8A  /* Domain 2 hash count */
#define ASIC_REG_DOMAIN_3       0x8B  /* Domain 3 hash count */
#define ASIC_REG_NONCE_COUNT    0x8C  /* Total hash count */

#define ASIC_NUM_DOMAINS        4
#define ASIC_REG_VR_MASK        0xA4
#define ASIC_REG_TEMPERATURE    0xB4

typedef struct {
    uint8_t  job_id;
    uint8_t  midstate_count;
    uint32_t starting_nonce;
    uint32_t nbits;
    uint32_t ntime;
    uint8_t  merkle_root[32];
    uint8_t  prev_block_hash[32];
    uint32_t version;
    // Metadata for share submission (not sent to ASIC)
    char     stratum_job_id[64];
    char     extranonce2[64];
    double   pool_diff;
    int      pool_id;
} asic_job_t;

typedef struct {
    int      chip_count;
    uint16_t current_freq;
    uint16_t current_voltage;
    float    chip_temp;
    bool     initialised;
} asic_state_t;

// Init & enumeration
int       asic_enumerate(void);  // Returns chip count found
esp_err_t asic_set_chip_address(int chip_index, uint8_t addr);

// Configuration
esp_err_t asic_set_difficulty_mask(uint64_t difficulty);
esp_err_t asic_set_version_mask(uint32_t mask);

// Work
int       asic_receive_result(asic_result_t *result, uint32_t timeout_ms);

// State
const asic_state_t *asic_get_state(void);

// Hash counter relay (result_task stores, hashrate_task reads)
void      asic_request_hash_counter(uint8_t chip_addr);
void      asic_store_hash_counter(int chip, uint32_t value);
uint32_t  asic_get_stored_hash_counter(int chip);

// Domain counter relay (per-domain hash counts)
void      asic_request_domain_counters(uint8_t chip_addr);
void      asic_store_domain_counter(int chip, int domain, uint32_t value);
uint32_t  asic_get_stored_domain_counter(int chip, int domain);

// PLL helper
typedef struct {
    uint8_t  refdiv;
    uint16_t fb_divider;
    uint8_t  postdiv1;
    uint8_t  postdiv2;
    float    actual_freq;
} pll_params_t;

pll_params_t asic_calc_pll(float target_freq_mhz);
