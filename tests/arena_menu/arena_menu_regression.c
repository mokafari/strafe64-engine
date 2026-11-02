/* Exercise the actual menu with a tiny virtual filesystem and command sink. */
#include "../../engine/code/q3_ui/ui_arena.c"

static int filesAvailable, handles, failures;
static char commands[16384];
static char settings[4096];
static char savedMap[MAX_QPATH] = "strafe64_showcase";
static float minplayers, savedOpponents, maxclients;
#define CHECK(c, msg) do { if (!(c)) { puts("FAIL: " msg); failures++; } } while (0)
void QDECL Com_Printf(const char *fmt, ...) { (void)fmt; }
void QDECL Com_Error(int level, const char *fmt, ...) { (void)level; (void)fmt; abort(); }
void trap_Cvar_Register(vmCvar_t *cv, const char *name, const char *value, int flags) {}
float trap_Cvar_VariableValue(const char *name) {
    if (!strcmp(name, "ui_arenaOpponents")) return 7;
    if (!strcmp(name, "g_spSkill")) return 3;
    return 0;
}
void trap_Cvar_VariableStringBuffer(const char *name, char *out, int size) { Q_strncpyz(out, savedMap, size); }
void trap_Cvar_Set(const char *name, const char *value) {
    if (!strcmp(name, "ui_arenaMap")) Q_strncpyz(savedMap, value, sizeof(savedMap));
    Q_strcat(settings, sizeof(settings), va("seta %s \"%s\"\n", name, value));
}
void trap_Cvar_SetValue(const char *name, float value) {
    if (!strcmp(name, "bot_minplayers")) minplayers = value;
    if (!strcmp(name, "ui_arenaOpponents")) savedOpponents = value;
    if (!strcmp(name, "sv_maxclients")) maxclients = value;
    trap_Cvar_Set(name, va("%g", value));
}
int trap_FS_FOpenFile(const char *path, fileHandle_t *file, fsMode_t mode) {
    /* Showcase has BSP+AAS; velodrome only BSP. Others are absent. */
    *file = filesAvailable && (!strcmp(path, "maps/strafe64_showcase.bsp") ||
        !strcmp(path, "maps/strafe64_showcase.aas") || !strcmp(path, "maps/strafe64dm_1337.bsp"));
    handles += !!*file;
    return *file ? 100 : -1;
}
void trap_FS_FCloseFile(fileHandle_t file) { handles--; }
void trap_Cmd_ExecuteText(int when, const char *text) { Q_strcat(commands, sizeof(commands), text); }
void trap_Key_SetCatcher(int catcher) {}
void UI_PopMenu(void) {}
void UI_PushMenu(menuframework_s *menu) {}
void Menu_AddItem(menuframework_s *menu, void *item) {}
void Menu_Draw(menuframework_s *menu) {}
void UI_FillRect(float x, float y, float w, float h, const float *color) {}
void UI_DrawString(int x, int y, const char *s, int style, vec4_t color) {}
void UI_DrawProportionalString(int x, int y, const char *s, int style, vec4_t color) {}

int main(int argc, char **argv) {
    char *preset, *clock, *map;
    filesAvailable = 1;
    ArenaMenu_Init();
    CHECK(installed_map_count == 1, "requires installed BSP and bot navigation");
    CHECK(!strcmp(installed_map_keys[0], "strafe64_showcase"), "filtered map key matches label");
    CHECK(s_arena.map.curvalue == 0 && !handles, "selection mapped and file handles closed");
    ArenaMenu_StartFight();
    preset = strstr(commands, "exec arena\n");
    clock = strstr(commands, "seta g_timeBind 0\n");
    map = strstr(commands, "map strafe64_showcase\n");
    CHECK(preset && clock && map && preset < clock && clock < map, "bullet-time OFF wins over preset before map load");
    CHECK(minplayers == 0 && savedOpponents == 7 && maxclients == 8, "exact selected roster has enough slots and persists independently");
    CHECK(strstr(commands, "free 400 ASH\n") && strstr(commands, "free 4600 NOVA\n"), "seven distinct named opponents queued");
    if (argc == 2 && !failures) {
        FILE *script = fopen(argv[1], "w");
        CHECK(script != NULL, "can save launch sequence for a live smoke run");
        if (script) { fputs(settings, script); fputs(commands, script); fclose(script); }
    }
    filesAvailable = 0;
    commands[0] = 0;
    ArenaMenu_Init();
    CHECK(!installed_map_count && (s_arena.fight.generic.flags & QMF_INACTIVE), "empty install disables FIGHT");
    ArenaMenu_StartFight();
    CHECK(!commands[0], "empty install cannot queue an invalid map");
    if (!failures) puts("PASS: arena availability, menu overrides, and exact bot roster");
    return failures ? 1 : 0;
}
