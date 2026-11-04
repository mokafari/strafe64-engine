/* Production smoothing/reset functions driven by deterministic render samples. */
#include "../../engine/code/cgame/cg_local.h"
cg_t cg;
cgs_t cgs;
vmCvar_t pmove_msec, cg_debugPosition;
static int now = 1000, failures;
int trap_Milliseconds(void) { return now; }
void QDECL CG_Printf(const char *fmt, ...) {}
void QDECL Com_Printf(const char *fmt, ...) {}
void QDECL Com_Error(int level, const char *fmt, ...) { abort(); }
static void CG_ClearLerpFrame(clientInfo_t *ci, lerpFrame_t *lf, int anim) {}
/* Reset cases use stationary trajectories; animation is outside this harness. */
void BG_EvaluateTrajectory(const trajectory_t *tr, int atTime, vec3_t result) {
    VectorCopy(tr->trBase, result);
}
#include "motion_under_test.h"
#define CHECK(c, msg) do { if (!(c)) { puts("FAIL: " msg); failures++; } } while (0)
int main(int argc, char **argv) {
    centity_t ent = {0};
    int i;
    float previous;
    if (argc != 2) return 2;
    cg.time = 100;
    cg.timeScaleNow = 0.5f;
    pmove_msec.integer = 8;
    ent.currentState.number = 1;
    ent.smoothTime = cg.time;
    ent.smoothRealMs = now;
    if (!strcmp(argv[1], "normal-to-slow")) {
        now = 1100;
        cg.timeScaleNow = 1;
        ent.lerpOrigin[0] = 10;
        CG_SmoothEntityMotion(&ent);
        now = 1110;
        cg.timeScaleNow = 0.5f;
        ent.lerpOrigin[0] = 14;
        ent.currentState.pos.trDelta[0] = 100;
        CG_SmoothEntityMotion(&ent);
        CHECK(ent.lerpOrigin[0] > 10 && ent.lerpOrigin[0] < 11.5f,
              "entering slow motion advances one frame, not the entire bypass interval");
    } else if (!strcmp(argv[1], "reset-nearby")) {
        ent.currentState.pos.trBase[0] = 40;
        CG_ResetPlayerEntity(&ent);
        now += 10;
        CG_SmoothEntityMotion(&ent);
        CHECK(fabs(ent.lerpOrigin[0] - 40) < 0.001f,
              "nearby teleport/reappearance must not blend from an obsolete position");
    } else if (!strcmp(argv[1], "stale-history")) {
        now += 1000;
        ent.lerpOrigin[0] = 60;
        CG_SmoothEntityMotion(&ent);
        CHECK(fabs(ent.lerpOrigin[0] - 60) < 0.001f,
              "long render gap resumes at the current location, not a frozen old location");
    } else if (!strcmp(argv[1], "steady-motion")) {
        ent.currentState.pos.trDelta[0] = 100;
        previous = 0;
        for (i = 1; i <= 100; i++) {
            float raw = (float)(i / 4) * 2;
            now += 10;
            ent.lerpOrigin[0] = raw;
            CG_SmoothEntityMotion(&ent);
            CHECK(ent.lerpOrigin[0] >= previous && ent.lerpOrigin[0] <= raw + 0.001f,
                  "steady bot motion stays monotonic without running ahead of authority");
            previous = ent.lerpOrigin[0];
        }
    } else if (!strcmp(argv[1], "predicted-player")) {
        cg.predictedPlayerEntity.lerpOrigin[0] = 25;
        CG_SmoothEntityMotion(&cg.predictedPlayerEntity);
        CHECK(cg.predictedPlayerEntity.lerpOrigin[0] == 25,
              "local predicted body is not independently smoothed");
    } else return 2;
    if (!failures) printf("PASS: %s\n", argv[1]);
    return failures ? 1 : 0;
}
