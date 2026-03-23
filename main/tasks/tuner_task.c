#include "tuner_task.h"
#include "auto_tuner.h"
#include "bm1370.h"
#include "voltage_regulator.h"
#include "hashrate_task.h"
#include "power_task.h"
#include "nvs_config.h"
#include "board.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include <inttypes.h>
#include <math.h>
#include <string.h>

static const char *TAG = "tuner_task";

#define BIT_START (1 << 0)
#define BIT_ABORT (1 << 1)

/* Coarse sweep parameters */
#define COARSE_FREQ_STEP  100   /* MHz */
#define COARSE_VOLT_STEP  100   /* mV  */
#define COARSE_SETTLE_MS  20000 /* 20s stabilisation */

/* Fine sweep parameters */
#define FINE_FREQ_RANGE   75    /* +/- MHz around best coarse */
#define FINE_FREQ_STEP    25    /* MHz */
#define FINE_VOLT_RANGE   50    /* +/- mV around best coarse */
#define FINE_VOLT_STEP    25    /* mV  */
#define FINE_SETTLE_MS    30000 /* 30s stabilisation */

/* Early-exit thresholds */
#define FINE_SKIP_RATIO       0.50  /* skip if score < 50% of best coarse */
#define FINE_CONSEC_WORSE_MAX 3     /* stop direction after 3 consecutive worse */

static EventGroupHandle_t s_event_group;

typedef struct {
    uint16_t freq_min;
    uint16_t freq_max;
    uint16_t volt_min;
    uint16_t volt_max;
} tuner_params_t;

static tuner_params_t s_params;
static SemaphoreHandle_t s_params_mutex;

/* Local copies used within the task after being copied under mutex */
static uint16_t s_freq_min;
static uint16_t s_freq_max;
static uint16_t s_volt_min;
static uint16_t s_volt_max;

/* ---------- helpers ---------- */

static bool check_abort(void)
{
    EventBits_t bits = xEventGroupGetBits(s_event_group);
    return (bits & BIT_ABORT) != 0;
}

static int estimate_total_steps(void)
{
    int freq_coarse = (s_freq_max - s_freq_min) / COARSE_FREQ_STEP + 1;
    int volt_coarse = (s_volt_max - s_volt_min) / COARSE_VOLT_STEP + 1;
    int coarse = freq_coarse * volt_coarse;

    /* Fine: up to 7 freq * 5 volt = 35, but expect ~15-20 with skipping */
    int fine_est = 20;

    int total = coarse + fine_est;
    if (total > TUNER_MAX_RESULTS) {
        total = TUNER_MAX_RESULTS;
    }
    return total;
}

/**
 * Sample the current operating point and fill a result slot.
 * Returns the slot pointer, or NULL on failure.
 */
static tuner_result_t *sample_point(int step, uint16_t freq, uint16_t voltage)
{
    tuner_result_t *slot = tuner_get_result_slot(step);
    if (!slot) {
        return NULL;
    }

    const hashrate_info_t *hr = hashrate_task_get_info();
    const power_status_t *pw = power_task_get_status();
    float temp = pw ? pw->chip_temp : 0.0f;

    slot->freq = freq;
    slot->voltage = voltage;
    slot->hashrate_ghs = hr ? hr->total_hashrate_ghs : 0.0f;
    slot->power_w = pw ? pw->power_w : 0.0f;
    slot->temp = temp;
    slot->efficiency_ghs_per_w = (slot->power_w > 0.0f)
        ? slot->hashrate_ghs / slot->power_w
        : 0.0f;

    slot->stable = (slot->hashrate_ghs > 0.0f)
        && (pw ? !pw->overheat : true)
        && (pw ? !pw->vr_fault : true);

    return slot;
}

/**
 * After the sweep completes, score all results with each mode and pick the
 * best for eco, balanced, and power profiles.
 */
static void compute_profiles(void)
{
    const tuner_status_t *st = tuner_get_status();
    tuner_profiles_t profiles;
    memset(&profiles, 0, sizeof(profiles));

    double best_eco_score = -1.0;
    double best_bal_score = -1.0;
    double best_pwr_score = -1.0;

    for (int i = 0; i < st->result_count; i++) {
        const tuner_result_t *r = &st->results[i];

        double eco_score = tuner_score(r, TUNER_MODE_ECO);
        double bal_score = tuner_score(r, TUNER_MODE_BALANCED);
        double pwr_score = tuner_score(r, TUNER_MODE_POWER);

        if (eco_score > best_eco_score) {
            best_eco_score = eco_score;
            profiles.eco = *r;
        }
        if (bal_score > best_bal_score) {
            best_bal_score = bal_score;
            profiles.balanced = *r;
        }
        if (pwr_score > best_pwr_score) {
            best_pwr_score = pwr_score;
            profiles.power = *r;
        }
    }

    profiles.eco_valid = best_eco_score > 0.0;
    profiles.balanced_valid = best_bal_score > 0.0;
    profiles.power_valid = best_pwr_score > 0.0;

    tuner_set_profiles(&profiles);

    ESP_LOGI(TAG, "Profiles computed:");
    if (profiles.eco_valid) {
        ESP_LOGI(TAG, "  Eco:      %" PRIu16 "MHz @ %" PRIu16 "mV (%.2f GH/s, %.1fW, %.3f GH/s/W)",
                 profiles.eco.freq, profiles.eco.voltage,
                 profiles.eco.hashrate_ghs, profiles.eco.power_w,
                 profiles.eco.efficiency_ghs_per_w);
    }
    if (profiles.balanced_valid) {
        ESP_LOGI(TAG, "  Balanced: %" PRIu16 "MHz @ %" PRIu16 "mV (%.2f GH/s, %.1fW, %.3f GH/s/W)",
                 profiles.balanced.freq, profiles.balanced.voltage,
                 profiles.balanced.hashrate_ghs, profiles.balanced.power_w,
                 profiles.balanced.efficiency_ghs_per_w);
    }
    if (profiles.power_valid) {
        ESP_LOGI(TAG, "  Power:    %" PRIu16 "MHz @ %" PRIu16 "mV (%.2f GH/s, %.1fW, %.3f GH/s/W)",
                 profiles.power.freq, profiles.power.voltage,
                 profiles.power.hashrate_ghs, profiles.power.power_w,
                 profiles.power.efficiency_ghs_per_w);
    }
}

/* ---------- main task ---------- */

static void tuner_task_fn(void *arg)
{
    (void)arg;

    for (;;) {
        /* Wait for START signal */
        xEventGroupWaitBits(s_event_group, BIT_START, pdTRUE, pdFALSE, portMAX_DELAY);
        xEventGroupClearBits(s_event_group, BIT_ABORT);

        /* Copy params under mutex to avoid race with tuner_start() */
        if (xSemaphoreTake(s_params_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            s_freq_min = s_params.freq_min;
            s_freq_max = s_params.freq_max;
            s_volt_min = s_params.volt_min;
            s_volt_max = s_params.volt_max;
            xSemaphoreGive(s_params_mutex);
        }

        /* Save original freq/voltage from NVS for abort restore */
        uint16_t orig_freq = nvs_config_get_u16(NVS_KEY_ASIC_FREQ, DEFAULT_ASIC_FREQ);
        uint16_t orig_volt = nvs_config_get_u16(NVS_KEY_ASIC_VOLTAGE, DEFAULT_ASIC_VOLTAGE);

        /* Reset status and start */
        tuner_reset_status();

        int total_est = estimate_total_steps();
        tuner_set_step(0, total_est);
        tuner_set_state(TUNER_STATE_RUNNING);

        ESP_LOGI(TAG, "Starting adaptive 2-phase sweep: freq %" PRIu16 "-%" PRIu16 ", volt %" PRIu16 "-%" PRIu16 " (~%d steps)",
                 s_freq_min, s_freq_max, s_volt_min, s_volt_max, total_est);

        /* Use balanced mode for coarse scoring (fine phase uses it too) */
        double best_score = -1.0;
        double best_eff = -1.0;
        int best_idx = -1;
        int best_eff_idx = -1;
        int step = 0;
        bool aborted = false;

        /* -- Phase 1: Coarse sweep ---------------------------------------- */
        ESP_LOGI(TAG, "Phase 1: Coarse sweep (step %d/%d/%d)",
                 COARSE_FREQ_STEP, COARSE_VOLT_STEP, COARSE_SETTLE_MS);

        uint16_t coarse_best_freq = s_freq_min;
        uint16_t coarse_best_volt = s_volt_min;
        double coarse_best_score = -1.0;

        for (uint16_t v = s_volt_min; v <= s_volt_max && !aborted; v += COARSE_VOLT_STEP) {
            esp_err_t err = vr_set_voltage(v);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "vr_set_voltage(%" PRIu16 ") failed: %d", v, err);
            }

            bool skip_higher_freq = false;

            for (uint16_t f = s_freq_min; f <= s_freq_max && !aborted; f += COARSE_FREQ_STEP) {
                if (check_abort()) { aborted = true; break; }
                if (step >= TUNER_MAX_RESULTS) { break; }

                if (skip_higher_freq) {
                    ESP_LOGI(TAG, "Coarse: skipping %" PRIu16 "MHz @ %" PRIu16 "mV (previous unstable/overheat)", f, v);
                    continue;
                }

                err = bm1370_set_frequency(f);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "bm1370_set_frequency(%" PRIu16 ") failed: %d", f, err);
                }

                vTaskDelay(pdMS_TO_TICKS(COARSE_SETTLE_MS));
                if (check_abort()) { aborted = true; break; }

                tuner_result_t *slot = sample_point(step, f, v);
                if (!slot) { break; }

                double score = tuner_score(slot, TUNER_MODE_BALANCED);

                /* Early exit: if score is 0 (unstable/overheat), skip higher freqs at this voltage */
                if (score <= 0.0) {
                    skip_higher_freq = true;
                }

                if (score > best_score) {
                    best_score = score;
                    best_idx = step;
                }
                if (score > coarse_best_score) {
                    coarse_best_score = score;
                    coarse_best_freq = f;
                    coarse_best_volt = v;
                }
                if (slot->stable && slot->efficiency_ghs_per_w > best_eff) {
                    best_eff = slot->efficiency_ghs_per_w;
                    best_eff_idx = step;
                }

                step++;
                tuner_set_result_count(step);
                tuner_set_step(step, total_est);
                tuner_set_best(best_idx, best_eff_idx);

                ESP_LOGI(TAG, "Coarse %d: %" PRIu16 "MHz @ %" PRIu16 "mV => %.2f GH/s, %.1fW, %.1fC, score=%.2f",
                         step, f, v, slot->hashrate_ghs, slot->power_w, slot->temp, score);
            }
        }

        if (aborted) {
            goto sweep_done;
        }

        if (coarse_best_score <= 0.0) {
            ESP_LOGW(TAG, "No viable coarse point found, skipping fine phase");
            goto sweep_done;
        }

        /* -- Phase 2: Fine sweep around best coarse result ---------------- */
        ESP_LOGI(TAG, "Phase 2: Fine sweep around %" PRIu16 "MHz @ %" PRIu16 "mV (coarse score=%.2f)",
                 coarse_best_freq, coarse_best_volt, coarse_best_score);

        /* Update total estimate now we know how many coarse steps we used */
        {
            int fine_freq_steps = (FINE_FREQ_RANGE * 2) / FINE_FREQ_STEP + 1; /* 7 */
            int fine_volt_steps = (FINE_VOLT_RANGE * 2) / FINE_VOLT_STEP + 1; /* 5 */
            int fine_max = fine_freq_steps * fine_volt_steps;
            int new_total = step + fine_max;
            if (new_total > TUNER_MAX_RESULTS) {
                new_total = TUNER_MAX_RESULTS;
            }
            total_est = new_total;
            tuner_set_step(step, total_est);
        }

        /* Compute fine sweep bounds, clamped to original range */
        uint16_t fine_fmin = (coarse_best_freq > s_freq_min + FINE_FREQ_RANGE)
            ? coarse_best_freq - FINE_FREQ_RANGE : s_freq_min;
        uint16_t fine_fmax = (coarse_best_freq + FINE_FREQ_RANGE < s_freq_max)
            ? coarse_best_freq + FINE_FREQ_RANGE : s_freq_max;
        uint16_t fine_vmin = (coarse_best_volt > s_volt_min + FINE_VOLT_RANGE)
            ? coarse_best_volt - FINE_VOLT_RANGE : s_volt_min;
        uint16_t fine_vmax = (coarse_best_volt + FINE_VOLT_RANGE < s_volt_max)
            ? coarse_best_volt + FINE_VOLT_RANGE : s_volt_max;

        double skip_threshold = coarse_best_score * FINE_SKIP_RATIO;

        for (uint16_t v = fine_vmin; v <= fine_vmax && !aborted; v += FINE_VOLT_STEP) {
            esp_err_t err = vr_set_voltage(v);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "vr_set_voltage(%" PRIu16 ") failed: %d", v, err);
            }

            int consec_worse = 0;

            for (uint16_t f = fine_fmin; f <= fine_fmax && !aborted; f += FINE_FREQ_STEP) {
                if (check_abort()) { aborted = true; break; }
                if (step >= TUNER_MAX_RESULTS) { break; }

                err = bm1370_set_frequency(f);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "bm1370_set_frequency(%" PRIu16 ") failed: %d", f, err);
                }

                vTaskDelay(pdMS_TO_TICKS(FINE_SETTLE_MS));
                if (check_abort()) { aborted = true; break; }

                tuner_result_t *slot = sample_point(step, f, v);
                if (!slot) { break; }

                double score = tuner_score(slot, TUNER_MODE_BALANCED);

                /* Skip clearly worse combos */
                if (score < skip_threshold) {
                    ESP_LOGI(TAG, "Fine: %" PRIu16 "MHz @ %" PRIu16 "mV score=%.2f below threshold, skipping", f, v, score);
                    consec_worse++;
                    if (consec_worse >= FINE_CONSEC_WORSE_MAX) {
                        ESP_LOGI(TAG, "Fine: %d consecutive worse, stopping this voltage", consec_worse);
                        break;
                    }
                    /* Still record the result */
                } else {
                    consec_worse = 0;
                }

                if (score > best_score) {
                    best_score = score;
                    best_idx = step;
                }
                if (slot->stable && slot->efficiency_ghs_per_w > best_eff) {
                    best_eff = slot->efficiency_ghs_per_w;
                    best_eff_idx = step;
                }

                step++;
                tuner_set_result_count(step);
                tuner_set_step(step, total_est);
                tuner_set_best(best_idx, best_eff_idx);

                ESP_LOGI(TAG, "Fine %d: %" PRIu16 "MHz @ %" PRIu16 "mV => %.2f GH/s, %.1fW, %.1fC, score=%.2f",
                         step, f, v, slot->hashrate_ghs, slot->power_w, slot->temp, score);
            }
        }

sweep_done:
        if (aborted) {
            tuner_set_state(TUNER_STATE_ABORTED);
            ESP_LOGW(TAG, "Sweep aborted. Restoring original settings: %" PRIu16 "MHz @ %" PRIu16 "mV",
                     orig_freq, orig_volt);
            vr_set_voltage(orig_volt);
            bm1370_set_frequency(orig_freq);
        } else {
            /* Update final total to actual step count */
            tuner_set_step(step, step);

            /* Compute eco/balanced/power profiles from all results */
            compute_profiles();

            tuner_set_state(TUNER_STATE_COMPLETE);
            ESP_LOGI(TAG, "Sweep complete. %d results collected.", step);
            if (best_idx >= 0) {
                const tuner_status_t *st = tuner_get_status();
                const tuner_result_t *br = &st->results[best_idx];
                ESP_LOGI(TAG, "Best overall: %" PRIu16 "MHz @ %" PRIu16 "mV (%.2f GH/s, %.1fW, score=%.2f)",
                         br->freq, br->voltage, br->hashrate_ghs, br->power_w, best_score);
            }
            if (best_eff_idx >= 0) {
                const tuner_status_t *st = tuner_get_status();
                const tuner_result_t *er = &st->results[best_eff_idx];
                ESP_LOGI(TAG, "Best efficiency: %" PRIu16 "MHz @ %" PRIu16 "mV (%.3f GH/s/W)",
                         er->freq, er->voltage, er->efficiency_ghs_per_w);
            }
        }
    }
}

/* ---------- public API ---------- */

void tuner_task_start(void)
{
    s_event_group = xEventGroupCreate();
    s_params_mutex = xSemaphoreCreateMutex();
    tuner_reset_status();

    xTaskCreate(tuner_task_fn, "tuner_task",
                TUNER_TASK_STACK_SIZE, NULL,
                TUNER_TASK_PRIORITY, NULL);
}

void tuner_start(uint16_t freq_min, uint16_t freq_max,
                 uint16_t volt_min, uint16_t volt_max)
{
    /* Write params under mutex so the task reads a consistent snapshot */
    if (xSemaphoreTake(s_params_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_params.freq_min = freq_min;
        s_params.freq_max = freq_max;
        s_params.volt_min = volt_min;
        s_params.volt_max = volt_max;
        xSemaphoreGive(s_params_mutex);
    }

    xEventGroupSetBits(s_event_group, BIT_START);
}

void tuner_abort(void)
{
    xEventGroupSetBits(s_event_group, BIT_ABORT);
}

void tuner_apply_profile(tuner_mode_t mode)
{
    const tuner_profiles_t *p = tuner_get_profiles();
    const tuner_result_t *target = NULL;
    bool valid = false;

    switch (mode) {
    case TUNER_MODE_ECO:
        target = &p->eco;
        valid = p->eco_valid;
        break;
    case TUNER_MODE_BALANCED:
        target = &p->balanced;
        valid = p->balanced_valid;
        break;
    case TUNER_MODE_POWER:
        target = &p->power;
        valid = p->power_valid;
        break;
    }

    if (!valid || !target) {
        ESP_LOGW(TAG, "No valid profile for mode %d", (int)mode);
        return;
    }

    ESP_LOGI(TAG, "Applying profile (mode %d): %" PRIu16 "MHz @ %" PRIu16 "mV", (int)mode, target->freq, target->voltage);

    vr_set_voltage(target->voltage);
    bm1370_set_frequency(target->freq);

    /* Persist to NVS */
    nvs_config_set_u16(NVS_KEY_ASIC_FREQ, target->freq);
    nvs_config_set_u16(NVS_KEY_ASIC_VOLTAGE, target->voltage);
}
