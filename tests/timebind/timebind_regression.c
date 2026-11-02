/* CMake extracts these functions directly from the current g_active.c. */
#include "../../engine/code/game/g_local.h"
#include "clock_under_test.h"
level_locals_t level;
gentity_t g_entities[MAX_GENTITIES];
vmCvar_t g_timeBind, g_timeBindMin, g_timeBindMax, g_timeBindRef, g_timeBindCurve;
vmCvar_t g_timeBindFire, g_timeBindLog, g_timeBindCrouch, g_timeBindBlock;
vmCvar_t g_timeBindRise, g_timeBindSmooth, pmove_fixed;
static float clockValue = 1;
static int fixedValue, writes, failures;
int trap_Milliseconds(void) { static int now; return now += 16; }
void trap_Cvar_Set(const char *name, const char *value) {
    if (!strcmp(name, "timescale")) clockValue = (float)atof(value);
    if (!strcmp(name, "pmove_fixed")) fixedValue = atoi(value);
    writes++;
}
void QDECL Com_Printf(const char *fmt, ...) {}
void QDECL Com_Error(int level, const char *fmt, ...) { abort(); }
#define CHECK(c, msg) do { if (!(c)) { puts("FAIL: " msg); failures++; } } while (0)
int main(void) {
    gclient_t clients[2] = {0};
    usercmd_t cmd = {0};
    int before;
    level.maxclients = 2;
    g_entities[0].client = &clients[0];
    g_entities[1].client = &clients[1];
    g_entities[1].s.number = 1;
    g_timeBind.integer = 1;
    g_timeBindMin.value = 0.05f;
    g_timeBindMax.value = 1;
    g_timeBindRef.value = 700;
    g_timeBindCurve.value = 1.5f;
    G_UpdateTimeBind(&g_entities[0], &cmd, 16);
    CHECK(clockValue < 0.06f && fixedValue == 1, "idle pilot takes clock and fixed movement");
    before = writes;
    G_ReleaseTimeBind(1);
    CHECK(writes == before && clockValue < 0.06f, "other spectator cannot release clock");
    G_ReleaseTimeBind(0);
    CHECK(clockValue == 1 && fixedValue == 0, "owning pilot release restores clock and movement");
    before = writes;
    G_ReleaseTimeBind(0);
    CHECK(writes == before, "repeated release does not clobber another clock controller");
    G_UpdateTimeBind(&g_entities[0], &cmd, 16);
    CHECK(clockValue < 0.06f, "returning pilot can drive slow motion again");
    g_timeBind.integer = 0;
    G_UpdateTimeBind(&g_entities[0], &cmd, 16);
    CHECK(clockValue == 1, "toggle off still releases clock");
    if (!failures) puts("PASS: time-bind ownership, spectator cleanup, reentry, and disable");
    return failures ? 1 : 0;
}
