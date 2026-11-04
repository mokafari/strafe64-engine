/* Compile production enumeration/mapping code, with SDL's mode list mocked. */
#include "../../engine/code/q3_ui/ui_local.h"
#include "video_under_test.h"
uiStatic_t uis;
static const char *available = "";
static int failures;
void trap_Cvar_VariableStringBuffer(const char *name, char *out, int size) {
    Q_strncpyz(out, available, size);
}
void QDECL Com_Printf(const char *fmt, ...) {}
void QDECL Com_Error(int level, const char *fmt, ...) { abort(); }
#define CHECK(c, msg) do { if (!(c)) { puts("FAIL: " msg); failures++; } } while (0)
static int find(const char *size) {
    int i;
    for (i = 0; resolutions[i]; i++) if (!strcmp(resolutions[i], size)) return i;
    return -1;
}
int main(void) {
    int i, j;
    char many[MAX_STRING_CHARS];
    uis.glconfig.vidWidth = 3440;
    uis.glconfig.vidHeight = 1440;
    GraphicsOptions_GetResolutions();
    GraphicsOptions_GetAspectRatios();
    CHECK(find("1920x1080") >= 0 && find("2560x1440") >= 0 && find("3840x2160") >= 0,
          "modern sizes available without SDL mode enumeration");
    CHECK(find("3440x1440") == 0, "running ultrawide size remains selectable");
    CHECK(!strcmp(ratios[resToRatio[find("3440x1440")]], "21:9"), "ultrawide aspect label");
    CHECK(GraphicsOptions_FindBuiltinResolution(find("3840x2160")) == -1, "4K uses custom mode, not a legacy index");
    CHECK(GraphicsOptions_FindBuiltinResolution(find("640x480")) == 3, "legacy mode indices preserved");
    CHECK(GraphicsOptions_FindDetectedResolution(3) == find("640x480"), "legacy mode roundtrip");
    CHECK(GraphicsOptions_FindDetectedResolution(999) == -1, "invalid stored mode is bounded");
    CHECK(GraphicsOptions_FindDetectedResolution(-2) == -1, "native mode uses running-size selection");
    available = "1920x1080  1920x1080 garbage 0x0 2560x1440 ";
    GraphicsOptions_GetResolutions();
    GraphicsOptions_GetAspectRatios();
    for (i = 0; resolutions[i]; i++) {
        CHECK(strchr(resolutions[i], 'x') && atoi(resolutions[i]) > 0, "invalid SDL tokens excluded");
        for (j = i+1; resolutions[j]; j++) CHECK(strcmp(resolutions[i], resolutions[j]), "no duplicate modes");
    }
    many[0] = 0;
    for (i = 0; i < 110; i++) Q_strcat(many, sizeof(many), va("%dx240 ", 320+i));
    available = many;
    GraphicsOptions_GetResolutions();
    GraphicsOptions_GetAspectRatios();
    for (i = 0; i < MAX_RESOLUTIONS && resolutions[i]; i++) {}
    CHECK(i < MAX_RESOLUTIONS, "large mode list is terminated within capacity");
    CHECK(find("3440x1440") == 0, "large mode list retains current resolution");
    available = "";
    uis.glconfig.vidWidth = 1920;
    uis.glconfig.vidHeight = 1080;
    GraphicsOptions_GetResolutions();
    GraphicsOptions_GetAspectRatios();
    CHECK(!strcmp(resolutions[0], "1920x1080"), "reopening menu refreshes running dimensions");
    CHECK(!strcmp(ratios[resToRatio[0]], "16:9"), "reopening menu clears old aspect ratios");
    if (!failures) puts("PASS: modern, native, ultrawide, fallback and bounded resolution selection");
    return failures ? 1 : 0;
}
