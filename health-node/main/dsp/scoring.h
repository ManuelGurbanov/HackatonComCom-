#pragma once

#include "mhn_config.h"
#include "baseline.h"
#include "features.h"

/*
    Anomaly scoring: distance between live features and the learned baseline
    -> a 0..100 health score, smoothed over time, with hysteresis between
    OK / WARNING / ALERT so the state does not flicker.

    The distance is a per-band z-score aggregated as an RMS z (a diagonal
    Mahalanobis distance, i.e. we trust per-band variance but ignore
    cross-band covariance -- cheap and robust for this many bands).
*/

typedef enum { HEALTH_OK = 0, HEALTH_WARNING, HEALTH_ALERT } health_state_t;

typedef struct {
    float         score;               /* smoothed health 0..100              */
    float         raw_score;           /* last instantaneous score            */
    float         deviation;           /* last RMS z-score (for the UI/debug)  */
    health_state_t state;
    float         sensitivity;         /* 0..1 joystick; 0.5 = nominal         */
    float         false_alarm_margin;  /* extra z slack, raised by the button  */
    int           initialized;
} scoring_t;

void scoring_init(scoring_t *s);

/* One frame. `tonal_penalty` (>=0, in z units) lets the tonal tracker push
   the score down when a new/shifted tone appears; pass 0 if unused.
   Returns the rounded smoothed score. */
int scoring_update(scoring_t *s, const baseline_t *base,
                   const features_t *f, float tonal_penalty);

void scoring_mark_false_alarm(scoring_t *s);  /* nudges threshold up a notch  */
void scoring_mark_serviced(scoring_t *s);     /* reset state + false-alarm bias */
void scoring_set_sensitivity(scoring_t *s, float sens01);

const char *scoring_state_name(health_state_t st);
