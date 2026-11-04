/* CMake extracts these functions directly from the current g_active.c. */
#include "../../engine/code/game/g_local.h"
#include "clock_under_test.h"
slowmoStep_t g_slowmoStep;
static int requestedOwner;
static float requestedTarget, requestedRate;
void trap_SlowmoRequest(int owner, float target, float rate) {
    requestedOwner=owner; requestedTarget=target; requestedRate=rate;
}
level_locals_t level;
gentity_t g_entities[MAX_GENTITIES];
vmCvar_t g_timeBind, g_timeBindMin, g_timeBindMax, g_timeBindRef, g_timeBindCurve;
vmCvar_t g_timeBindFire, g_timeBindLog, g_timeBindCrouch, g_timeBindBlock;
vmCvar_t g_timeBindRise, g_timeBindSmooth, pmove_fixed;
static float clockValue = 1;
static int fixedValue, writes, failures, now = 1000, frameMsec = 16;
int trap_Milliseconds(void) { return now += frameMsec; }
float trap_Cvar_VariableValue(const char *name) { return clockValue; }
int trap_Cvar_VariableIntegerValue(const char *name) { return 0; }
void QDECL G_Printf(const char *fmt, ...) {}
void trap_Cvar_Set(const char *name, const char *value) {
    if (!strcmp(name, "timescale")) clockValue = (float)atof(value);
    if (!strcmp(name, "pmove_fixed")) fixedValue = atoi(value);
    writes++;
}
void QDECL Com_Printf(const char *fmt, ...) {}
void QDECL Com_Error(int level, const char *fmt, ...) { abort(); }
#define CHECK(c, msg) do { if (!(c)) { puts("FAIL: " msg); failures++; } } while (0)
int main(int argc, char **argv) {
    gclient_t clients[2] = {0};
    usercmd_t cmd = {0};
    int before, i;
    level.maxclients = 2;
    g_entities[0].client = &clients[0];
    g_entities[1].client = &clients[1];
    g_entities[1].s.number = 1;
    clients[0].ps.stats[STAT_HEALTH] = 100;
    clients[1].ps.stats[STAT_HEALTH] = 100;
    g_timeBind.integer = 1;
    g_timeBindMin.value = 0.05f;
    g_timeBindMax.value = 1;
    g_timeBindRef.value = 700;
    g_timeBindCurve.value = 1.5f;
    g_timeBindRise.value = 40;
    g_timeBindSmooth.value = 18;
    g_timeBindCrouch.value = 1;
    g_timeBindBlock.value = 0.5f;
    if (argc > 1 && !strcmp(argv[1], "experimental-intent")) {
        g_slowmoStep.active=1; g_slowmoStep.scale=.05f;
        G_UpdateTimeBind(&g_entities[0], &cmd);
        CHECK(requestedOwner==1 && fabs(requestedTarget-.05f)<.0001, "idle submits fractional clock target");
        cmd.forwardmove=127; G_UpdateTimeBind(&g_entities[0], &cmd);
        CHECK(requestedTarget==1 && requestedRate==40, "movement requests fast rise");
        CHECK(writes==0, "experimental path never publishes timescale or forces pmove_fixed");
        clients[0].ps.stats[STAT_HEALTH]=0; G_UpdateTimeBind(&g_entities[0], &cmd);
        CHECK(requestedTarget==1 && requestedRate==30, "death releases gameplay clock while killcam may override");
        clients[0].ps.stats[STAT_HEALTH]=100; G_ReleaseTimeBind(0);
        CHECK(requestedTarget==1 && writes==0, "spectator release uses clock request");
        goto done;
    }
    if (argc > 1 && !strcmp(argv[1], "bot-exclusion")) {
        g_entities[0].r.svFlags = SVF_BOT;
        G_UpdateTimeBind(&g_entities[0], &cmd);
        G_ReleaseTimeBind(0);
        G_ReleaseTimeBind(-1);
        G_ReleaseTimeBind(MAX_GENTITIES);
        CHECK(writes == 0 && clockValue == 1, "bots and invalid client IDs cannot change the clock");
        goto done;
    }
    if (argc > 1 && !strcmp(argv[1], "movement-intent")) {
        clients[0].ps.velocity[0] = 700;
        G_UpdateTimeBind(&g_entities[0], &cmd);
        CHECK(clockValue < 0.06f, "residual velocity without input does not wake time");
        cmd.upmove = 127;
        for (i = 0; i < 40; i++) G_UpdateTimeBind(&g_entities[0], &cmd);
        CHECK(clockValue > 0.99f, "jump input wakes time even without directional input");
        cmd.upmove = 0;
        for (i = 0; i < 80; i++) G_UpdateTimeBind(&g_entities[0], &cmd);
        CHECK(clockValue < 0.06f, "releasing input settles back to slow motion");
        goto done;
    }
    if (argc > 1 && !strcmp(argv[1], "guard-brake")) {
        cmd.forwardmove = 127;
        G_UpdateTimeBind(&g_entities[0], &cmd);
        CHECK(clockValue > 0.99f, "full input runs at real time");
        cmd.buttons = BUTTON_BLOCK;
        for (i = 0; i < 80; i++) G_UpdateTimeBind(&g_entities[0], &cmd);
        CHECK(fabs(clockValue - 0.5f) < 0.003f, "guard slows a moving pilot to configured multiplier");
        cmd.buttons = 0;
        for (i = 0; i < 40; i++) G_UpdateTimeBind(&g_entities[0], &cmd);
        CHECK(clockValue > 0.99f, "releasing guard restores real time");
        goto done;
    }
    if (argc > 1 && !strcmp(argv[1], "focus-recharge")) {
        g_timeBind.integer = 2;
        cmd.buttons = BUTTON_FOCUS;
        G_UpdateTimeBind(&g_entities[0], &cmd);
        CHECK(fabs(clockValue - 0.333f) < 0.002f, "focus begins at one-third speed");
        for (i = 0; i < 120; i++) G_UpdateTimeBind(&g_entities[0], &cmd);
        CHECK(clockValue > 0.99f, "held focus expires after its real-time budget");
        for (i = 0; i < 120; i++) G_UpdateTimeBind(&g_entities[0], &cmd);
        CHECK(clockValue > 0.99f, "holding exhausted focus cannot restart it");
        cmd.buttons = 0;
        for (i = 0; i < 70; i++) G_UpdateTimeBind(&g_entities[0], &cmd);
        cmd.buttons = BUTTON_FOCUS;
        for (i = 0; i < 30; i++) G_UpdateTimeBind(&g_entities[0], &cmd);
        CHECK(fabs(clockValue - 0.333f) < 0.003f, "released focus recharges and can be used again");
        goto done;
    }
    if (argc > 1 && !strcmp(argv[1], "clock-recovery")) {
        G_UpdateTimeBind(&g_entities[0], &cmd);
        clockValue = 1;
        for (i = 0; i < 5; i++) G_UpdateTimeBind(&g_entities[0], &cmd);
        CHECK(clockValue < 0.06f, "live pilot recovers clock after an external override");
        before = writes;
        for (i = 0; i < 200; i++) G_UpdateTimeBind(&g_entities[0], &cmd);
        CHECK(writes == before, "settled clock does not flood reliable config updates");
        goto done;
    }
    if (argc > 1 && !strcmp(argv[1], "death-respawn")) {
        G_UpdateTimeBind(&g_entities[0], &cmd);
        clients[0].ps.stats[STAT_HEALTH] = 0;
        clockValue = 0.25f;
        before = writes;
        for (i = 0; i < 30; i++) G_UpdateTimeBind(&g_entities[0], &cmd);
        CHECK(writes == before && clockValue == 0.25f, "death yields clock to killcam");
        clients[0].ps.stats[STAT_HEALTH] = 100;
        G_UpdateTimeBind(&g_entities[0], &cmd);
        CHECK(clockValue < 0.06f, "respawn republishes the living pilot clock");
        G_ReleaseTimeBind(0);
        CHECK(clockValue == 1 && fixedValue == 0, "disconnect after respawn restores defaults");
        goto done;
    }
    if (argc > 1 && !strcmp(argv[1], "fixed-movement")) {
        pmove_fixed.integer = fixedValue = 1;
        G_UpdateTimeBind(&g_entities[0], &cmd);
        G_ReleaseTimeBind(0);
        CHECK(fixedValue == 1, "release preserves preexisting fixed-movement setting");
        goto done;
    }
    if (argc > 1 && strcmp(argv[1], "ownership")) {
        fprintf(stderr, "Unknown case: %s\n", argv[1]);
        return 2;
    }
    G_UpdateTimeBind(&g_entities[0], &cmd);
    CHECK(clockValue < 0.06f && fixedValue == 1, "idle pilot takes clock and fixed movement");
    before = writes;
    G_ReleaseTimeBind(1);
    CHECK(writes == before && clockValue < 0.06f, "other spectator cannot release clock");
    G_ReleaseTimeBind(0);
    CHECK(clockValue == 1 && fixedValue == 0, "owning pilot release restores clock and movement");
    before = writes;
    G_ReleaseTimeBind(0);
    CHECK(writes == before, "repeated release does not clobber another clock controller");
    G_UpdateTimeBind(&g_entities[0], &cmd);
    CHECK(clockValue < 0.06f, "returning pilot can drive slow motion again");
    g_timeBind.integer = 0;
    G_UpdateTimeBind(&g_entities[0], &cmd);
    CHECK(clockValue == 1, "toggle off still releases clock");
    g_timeBind.integer = 2;
    cmd.buttons = BUTTON_FOCUS;
    G_UpdateTimeBind(&g_entities[0], &cmd);
    CHECK(fabs(clockValue - 0.333f) < 0.002f, "focus mode slows the clock");
    for (i = 0; i < 100; i++) {
        G_ReleaseTimeBind(1);
        G_UpdateTimeBind(&g_entities[0], &cmd);
    }
    CHECK(clockValue > 0.99f, "spectator cleanup cannot refill another pilot's focus charge");
    G_ReleaseTimeBind(0);
    G_UpdateTimeBind(&g_entities[0], &cmd);
    CHECK(fabs(clockValue - 0.333f) < 0.002f, "owning pilot reentry resets focus charge");
    clients[0].ps.stats[STAT_HEALTH] = 0;
    before = writes;
    G_UpdateTimeBind(&g_entities[0], &cmd);
    CHECK(writes == before, "dead pilot leaves the killcam clock alone");
    G_ReleaseTimeBind(0);
    CHECK(clockValue == 1 && fixedValue == 0, "dead owner can still release the clock");
done:
    if (!failures) printf("PASS: %s\n", argc > 1 ? argv[1] : "ownership");
    return failures ? 1 : 0;
}
