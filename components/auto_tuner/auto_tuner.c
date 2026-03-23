#include "auto_tuner.h"
#include <string.h>

static tuner_status_t s_status;

double tuner_score(const tuner_result_t *r, tuner_mode_t mode)
{
    if (!r->stable) {
        return 0.0;
    }

    double score = 0.0;

    switch (mode) {
    case TUNER_MODE_ECO:
        /* Pure efficiency: GH/s per watt, penalise high temps heavily */
        score = (r->power_w > 0) ? r->hashrate_ghs / r->power_w : 0;
        if (r->temp > 60.0) score *= 1.0 - ((r->temp - 60.0) / 10.0);
        if (r->temp > 70.0) score = 0.0;
        break;

    case TUNER_MODE_BALANCED:
        /* Hashrate weighted by efficiency and thermal headroom */
        score = r->hashrate_ghs;
        {
            double eff = (r->power_w > 0) ? r->hashrate_ghs / r->power_w : 0;
            score *= (1.0 + eff * 0.1);
        }
        if (r->temp > 60.0) score *= 1.0 - ((r->temp - 60.0) / 10.0);
        if (r->temp > 70.0) score = 0.0;
        break;

    case TUNER_MODE_POWER:
        /* Raw hashrate, only penalise near overheat */
        score = r->hashrate_ghs;
        if (r->temp > 68.0) score *= 1.0 - ((r->temp - 68.0) / 5.0);
        if (r->temp > 73.0) score = 0.0;
        break;
    }

    return score;
}

const tuner_status_t *tuner_get_status(void)
{
    return &s_status;
}

const tuner_profiles_t *tuner_get_profiles(void)
{
    return &s_status.profiles;
}

void tuner_set_state(tuner_state_t state)
{
    s_status.state = state;
}

void tuner_reset_status(void)
{
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = TUNER_STATE_IDLE;
    s_status.best_index = -1;
    s_status.best_eff_index = -1;
}

tuner_result_t *tuner_get_result_slot(int index)
{
    if (index < 0 || index >= TUNER_MAX_RESULTS) {
        return NULL;
    }
    return &s_status.results[index];
}

void tuner_set_result_count(int count)
{
    s_status.result_count = count;
}

void tuner_set_step(int current, int total)
{
    s_status.current_step = current;
    s_status.total_steps = total;
}

void tuner_set_best(int best_idx, int best_eff_idx)
{
    s_status.best_index = best_idx;
    s_status.best_eff_index = best_eff_idx;
}

void tuner_set_profiles(const tuner_profiles_t *p)
{
    if (p) {
        s_status.profiles = *p;
    }
}
