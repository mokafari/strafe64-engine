/* STRAFE64 local experimental clock. No renderer, VM or network dependencies. */
#ifndef S64_SLOWMO_CLOCK_H
#define S64_SLOWMO_CLOCK_H
#include <math.h>
#include <string.h>

#define SM_REAL_STEP_MS 8.0
#define SM_MAX_STEPS 16
typedef struct {
    double worldMs, scale, target, rate, accumulator, droppedMs;
    double gameplayTarget, gameplayRate;
    int tick, owner;
} smClock_t;

static void SM_Init(smClock_t *c, double worldMs) {
    memset(c, 0, sizeof(*c));
    c->worldMs = worldMs;
    c->scale = c->target = c->gameplayTarget = 1.0;
    c->rate = c->gameplayRate = 30.0;
}

/* owner 1 = gameplay, 2 = killcam, 0 = release killcam. */
static void SM_Request(smClock_t *c, int owner, double target, double rate) {
    if (!(target >= 0.009999999 && target <= 1.0) || !(rate >= 0.0 && rate <= 1000.0)) return;
    if (target < 0.01) target = 0.01;
    if (owner == 1) {
        c->gameplayTarget = target; c->gameplayRate = rate;
        if (c->owner == 2) return;
    } else if (owner == 0) {
        if (c->owner != 2) return;
        owner = 1; target = c->gameplayTarget; rate = c->gameplayRate;
    } else if (owner != 2) return;
    c->owner = owner; c->target = target; c->rate = rate;
}

static double SM_Integrate(smClock_t *c, double realMs) {
    double seconds = realMs * 0.001, decay, area;
    if (c->rate == 0.0) {
        c->scale = c->target;
        area = c->scale * seconds;
    } else {
        decay = exp(-c->rate * seconds);
        area = c->target * seconds + (c->scale - c->target) * (1.0 - decay) / c->rate;
        c->scale = c->target + (c->scale - c->target) * decay;
    }
    c->worldMs += area * 1000.0;
    return area * 1000.0;
}

static void SM_Accumulate(smClock_t *c, double realMs) {
    double limit = SM_MAX_STEPS * SM_REAL_STEP_MS;
    if (realMs < 0.0) realMs = 0.0;
    c->accumulator += realMs;
    if (c->accumulator > limit) {
        c->droppedMs += c->accumulator - limit;
        c->accumulator = limit;
    }
}

static int SM_Step(smClock_t *c, double *worldDelta) {
    if (c->accumulator + 1e-9 < SM_REAL_STEP_MS) return 0;
    c->accumulator -= SM_REAL_STEP_MS;
    if (c->accumulator < 0.0) c->accumulator = 0.0;
    *worldDelta = SM_Integrate(c, SM_REAL_STEP_MS);
    c->tick++;
    return 1;
}
#endif
