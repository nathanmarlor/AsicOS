#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "asic.h"

#define BM1370_CORE_COUNT       128
#define BM1370_SMALL_CORE_COUNT 2040
#define BM1370_MAX_JOB_ID       128

uint8_t bm1370_job_to_asic_id(uint8_t job_id);
uint8_t bm1370_asic_to_job_id(uint8_t asic_id);
int     bm1370_nonce_to_chip(uint32_t nonce, int chip_count);
void    bm1370_set_address_interval(int interval);
int     bm1370_get_address_interval(void);

esp_err_t bm1370_init(int expected_chips);
esp_err_t bm1370_set_frequency(uint16_t freq_mhz);
int       bm1370_set_max_baud(void);
float     bm1370_read_temperature(void);
esp_err_t bm1370_send_work(const asic_job_t *job);
