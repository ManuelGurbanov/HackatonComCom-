#include "scoring.h"

#include <math.h>

void scoring_init(scoring_t *s)
{
    s->score = 100.0f;
    s->raw_score = 100.0f;
    s->deviation = 0.0f;
    s->state = HEALTH_OK;
    s->sensitivity = 0.5f;
    s->false_alarm_margin = 0.0f;
    s->initialized = 0;
}

void scoring_set_sensitivity(scoring_t *s, float sens01)
{
    if (sens01 < 0.0f) sens01 = 0.0f;
    if (sens01 > 1.0f) sens01 = 1.0f;
    s->sensitivity = sens01;
}

void scoring_mark_false_alarm(scoring_t *s)
{
    /* Each press makes the node a little more tolerant; cap it so the node
       can never be made completely deaf. */
    s->false_alarm_margin += 0.5f;
    if (s->false_alarm_margin > 3.0f) s->false_alarm_margin = 3.0f;
}

void scoring_mark_serviced(scoring_t *s)
{
    s->false_alarm_margin = 0.0f;
    s->score = 100.0f;
    s->raw_score = 100.0f;
    s->state = HEALTH_OK;
    s->initialized = 0;
}

static float rms_zscore(const baseline_t *base, const features_t *f)
{
    double sumsq = 0.0;
    int n = 0;
    for (int b = 0; b < MHN_NUM_BANDS; b++) {
        float sigma = sqrtf(base->band_var[b]);
        if (sigma <= 0.0f) continue;
        float z = (f->bands[b] - base->band_mean[b]) / sigma;
        sumsq += (double)z * z;
        n++;
    }
    /* Fold in broadband level so a flat-but-louder machine is also caught. */
    if (base->rms_var > 0.0f) {
        float z = (f->rms_db - base->rms_mean) / sqrtf(base->rms_var);
        sumsq += (double)z * z;
        n++;
    }
    return n > 0 ? (float)sqrt(sumsq / n) : 0.0f;
}

int scoring_update(scoring_t *s, const baseline_t *base,
                   const features_t *f, float tonal_penalty)
{
    if (!baseline_valid(base)) {
        /* No baseline yet: report full health, nothing to compare against. */
        s->raw_score = 100.0f;
        s->score = 100.0f;
        s->deviation = 0.0f;
        s->state = HEALTH_OK;
        return 100;
    }

    float d = rms_zscore(base, f) + (tonal_penalty > 0.0f ? tonal_penalty : 0.0f);
    s->deviation = d;

    /* Slack of ~1 sigma is normal variation; the false-alarm button and the
       sensitivity slider widen/narrow that band. sens01=0.5 -> gain 1.0. */
    float gain = 0.5f + s->sensitivity;                 /* 0.5 .. 1.5          */
    float excess = (d - 1.0f - s->false_alarm_margin) * gain;
    if (excess < 0.0f) excess = 0.0f;

    float raw = 100.0f * expf(-excess / 2.0f);          /* smooth, monotone    */
    s->raw_score = raw;

    if (!s->initialized) {
        s->score = raw;
        s->initialized = 1;
    } else {
        s->score += MHN_SCORE_EMA_ALPHA * (raw - s->score);
    }

    /* Hysteresis: drop into a worse state as soon as we cross, but require an
       extra HYSTERESIS margin to climb back out. */
    int sc = (int)(s->score + 0.5f);
    switch (s->state) {
    case HEALTH_OK:
        if (sc <= MHN_ALERT_SCORE)      s->state = HEALTH_ALERT;
        else if (sc <= MHN_WARN_SCORE)  s->state = HEALTH_WARNING;
        break;
    case HEALTH_WARNING:
        if (sc <= MHN_ALERT_SCORE)              s->state = HEALTH_ALERT;
        else if (sc >= MHN_WARN_SCORE + MHN_HYSTERESIS) s->state = HEALTH_OK;
        break;
    case HEALTH_ALERT:
        if (sc >= MHN_ALERT_SCORE + MHN_HYSTERESIS) {
            s->state = (sc <= MHN_WARN_SCORE) ? HEALTH_WARNING : HEALTH_OK;
        }
        break;
    }
    return sc;
}

const char *scoring_state_name(health_state_t st)
{
    switch (st) {
    case HEALTH_OK:      return "OK";
    case HEALTH_WARNING: return "WARNING";
    case HEALTH_ALERT:   return "ALERT";
    default:             return "?";
    }
}
