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
#include "esp_log.h"

#include <math.h>

static const char *TAG = "tuner_task";

#define BIT_START (1 << 0)
#define BIT_ABORT (1 << 1)

static EventGroupHandle_t s_event_group;

static uint16_t s_freq_min;
static uint16_t s_freq_max;
static uint16_t s_freq_step;
static uint16_t s_volt_min;
static uint16_t s_volt_max;
static uint16_t s_volt_step;

/* ---------- helpers ---------- */

static int count_steps(uint16_t fmin, uint16_t fmax, uint16_t fstep,
                       uint16_t vmin, uint16_t vmax, uint16_t vstep)
{
    int freq_steps = (fstep > 0) ? ((fmax - fmin) / fstep + 1) : 1;
    int volt_steps = (vstep > 0) ? ((vmax - vmin) / vstep + 1) : 1;
    int total = freq_steps * volt_steps;
    if (total > TUNER_MAX_RESULTS) {
        total = TUNER_MAX_RESULTS;
    }
    return total;
}

static bool check_abort(void)
{
    EventBits_t bits = xEventGroupGetBits(s_event_group);
    return (bits & BIT_ABORT) != 0;
}

/* ---------- main task ---------- */

static void tuner_task_fn(void *arg)
{
    (void)arg;

    for (;;) {
        /* Wait for START signal */
        xEventGroupWaitBits(s_event_group, BIT_START, pdTRUE, pdFALSE, portMAX_DELAY);
        xEventGroupClearBits(s_event_group, BIT_ABORT);

        /* Save original freq/voltage from NVS for abort restore */
        uint16_t orig_freq = nvs_config_get_u16(NVS_KEY_ASIC_FREQ, DEFAULT_ASIC_FREQ);
        uint16_t orig_volt = nvs_config_get_u16(NVS_KEY_ASIC_VOLTAGE, DEFAULT_ASIC_VOLTAGE);

        /* Reset status and start */
        tuner_reset_status();

        int total = count_steps(s_freq_min, s_freq_max, s_freq_step,
                                s_volt_min, s_volt_max, s_volt_step);
        tuner_set_step(0, total);
        tuner_set_state(TUNER_STATE_RUNNING);

        ESP_LOGI(TAG, "Starting sweep: freq %u-%u step %u, volt %u-%u step %u (%d steps)",
                 s_freq_min, s_freq_max, s_freq_step,
                 s_volt_min, s_volt_max, s_volt_step, total);

        double best_score = -1.0;
        double best_eff = -1.0;
        int best_idx = -1;
        int best_eff_idx = -1;
        int step = 0;
        bool aborted = false;

        for (uint16_t v = s_volt_min; v <= s_volt_max && !aborted; v += s_volt_step) {
            /* Set voltage */
            esp_err_t err = vr_set_voltage(v);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "vr_set_voltage(%u) failed: %d", v, err);
            }

            for (uint16_t f = s_freq_min; f <= s_freq_max && !aborted; f += s_freq_step) {
                if (check_abort()) {
                    aborted = true;
                    break;
                }

                if (step >= TUNER_MAX_RESULTS) {
                    ESP_LOGW(TAG, "Result buffer full, stopping early");
                    break;
                }

                /* Set frequency */
                err = bm1370_set_frequency(f);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "bm1370_set_frequency(%u) failed: %d", f, err);
                }

                /* Wait 30s for stabilization */
                vTaskDelay(pdMS_TO_TICKS(30000));

                if (check_abort()) {
                    aborted = true;
                    break;
                }

                /* Sample data */
                const hashrate_info_t *hr = hashrate_task_get_info();
                const power_status_t *pw = power_task_get_status();
                float temp = bm1370_read_temperature();

                /* Fill result slot */
                tuner_result_t *slot = tuner_get_result_slot(step);
                if (!slot) {
                    break;
                }

                slot->freq = f;
                slot->voltage = v;
                slot->hashrate_ghs = hr ? hr->total_hashrate_ghs : 0.0f;
                slot->power_w = pw ? pw->power_w : 0.0f;
                slot->temp = temp;
                slot->efficiency_ghs_per_w = (slot->power_w > 0.0f)
                    ? slot->hashrate_ghs / slot->power_w
                    : 0.0f;

                /* Stability: hashrate > 0, no overheat, no VR fault */
                slot->stable = (slot->hashrate_ghs > 0.0f)
                    && (pw ? !pw->overheat : true)
                    && (pw ? !pw->vr_fault : true);

                /* Score and track best */
                double score = tuner_score(slot);
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
                tuner_set_step(step, total);
                tuner_set_best(best_idx, best_eff_idx);

                ESP_LOGI(TAG, "Step %d/%d: %uMHz @ %umV => %.2f GH/s, %.1fW, %.1fC, score=%.2f",
                         step, total, f, v,
                         slot->hashrate_ghs, slot->power_w, slot->temp, score);

                /* Avoid infinite loop when step is 0 */
                if (s_freq_step == 0) break;
            }
            /* Avoid infinite loop when step is 0 */
            if (s_volt_step == 0) break;
        }

        if (aborted) {
            tuner_set_state(TUNER_STATE_ABORTED);
            ESP_LOGW(TAG, "Sweep aborted. Restoring original settings: %uMHz @ %umV",
                     orig_freq, orig_volt);
            vr_set_voltage(orig_volt);
            bm1370_set_frequency(orig_freq);
        } else {
            tuner_set_state(TUNER_STATE_COMPLETE);
            ESP_LOGI(TAG, "Sweep complete. %d results collected.", step);
            if (best_idx >= 0) {
                const tuner_status_t *st = tuner_get_status();
                const tuner_result_t *br = &st->results[best_idx];
                ESP_LOGI(TAG, "Best overall: %uMHz @ %umV (%.2f GH/s, %.1fW, score=%.2f)",
                         br->freq, br->voltage, br->hashrate_ghs, br->power_w, best_score);
            }
            if (best_eff_idx >= 0) {
                const tuner_status_t *st = tuner_get_status();
                const tuner_result_t *er = &st->results[best_eff_idx];
                ESP_LOGI(TAG, "Best efficiency: %uMHz @ %umV (%.3f GH/s/W)",
                         er->freq, er->voltage, er->efficiency_ghs_per_w);
            }
        }
    }
}

/* ---------- public API ---------- */

void tuner_task_start(void)
{
    s_event_group = xEventGroupCreate();
    tuner_reset_status();

    xTaskCreate(tuner_task_fn, "tuner_task",
                TUNER_TASK_STACK_SIZE, NULL,
                TUNER_TASK_PRIORITY, NULL);
}

void tuner_start(uint16_t freq_min, uint16_t freq_max, uint16_t freq_step,
                 uint16_t volt_min, uint16_t volt_max, uint16_t volt_step)
{
    s_freq_min = freq_min;
    s_freq_max = freq_max;
    s_freq_step = freq_step;
    s_volt_min = volt_min;
    s_volt_max = volt_max;
    s_volt_step = volt_step;

    xEventGroupSetBits(s_event_group, BIT_START);
}

void tuner_abort(void)
{
    xEventGroupSetBits(s_event_group, BIT_ABORT);
}

void tuner_apply_best(void)
{
    const tuner_status_t *st = tuner_get_status();
    if (st->state != TUNER_STATE_COMPLETE || st->best_index < 0) {
        ESP_LOGW(TAG, "No best result to apply");
        return;
    }

    const tuner_result_t *best = &st->results[st->best_index];

    ESP_LOGI(TAG, "Applying best: %uMHz @ %umV", best->freq, best->voltage);

    vr_set_voltage(best->voltage);
    bm1370_set_frequency(best->freq);

    /* Persist to NVS */
    nvs_config_set_u16(NVS_KEY_ASIC_FREQ, best->freq);
    nvs_config_set_u16(NVS_KEY_ASIC_VOLTAGE, best->voltage);
}
