/* Exercise the production font loader with real FreeType and stubbed GPU/FS I/O.
 * Pass a real .ttf path; no window or GL context is required. */
#include "../../engine/code/renderercommon/tr_common.h"
#include "../../engine/code/qcommon/qcommon.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <assert.h>

static int liveFaces, fileBuffers, allocations, images, legacyAvailable;
static int ttfAvailable = 1;
static byte *ttfData;
static long ttfSize;
static fontInfo_t legacyFont;
static char imageNames[128][MAX_QPATH], savedFontName[MAX_QPATH];
static fontInfo_t savedFont;

static FT_Error NewFace(FT_Library lib, const FT_Byte *data, FT_Long size,
                       FT_Long index, FT_Face *face) {
    FT_Error result = FT_New_Memory_Face(lib, data, size, index, face);
    if (!result) liveFaces++;
    return result;
}
static FT_Error DoneFace(FT_Face face) {
    liveFaces--;
    return FT_Done_Face(face);
}
#define FT_New_Memory_Face NewFace
#define FT_Done_Face DoneFace
#include "../../engine/code/renderercommon/tr_font.c"
#undef FT_New_Memory_Face
#undef FT_Done_Face

refimport_t ri;
static cvar_t saveFont;
cvar_t *r_saveFontData = &saveFont;

void QDECL Com_Printf(const char *fmt, ...) { (void)fmt; }
void QDECL Com_Error(int level, const char *fmt, ...) { (void)level; (void)fmt; abort(); }
static void QDECL Print(int level, const char *fmt, ...) { (void)level; (void)fmt; }
static void *Alloc(int size) { allocations++; return malloc(size); }
static void Free(void *ptr) { if (ptr) allocations--; free(ptr); }
static long Read(const char *name, void **buffer) {
    const void *data = NULL;
    long size = -1;
    if (ttfAvailable && strstr(name, ".ttf")) { data = ttfData; size = ttfSize; }
    else if (savedFontName[0] && !strcmp(name, savedFontName)) {
        data = &savedFont; size = sizeof(savedFont);
    }
    else if (legacyAvailable && !strcmp(name, "fonts/fontImage_48.dat")) {
        data = &legacyFont; size = sizeof(legacyFont);
    }
    if (buffer) {
        *buffer = NULL;
        if (size > 0) { *buffer = malloc(size); memcpy(*buffer, data, size); fileBuffers++; }
    }
    return size;
}
static void FreeFile(void *buffer) { fileBuffers--; free(buffer); }
static void Write(const char *name, const void *data, int size) {
    if (strstr(name, ".dat") && size == sizeof(savedFont)) {
        Q_strncpyz(savedFontName, name, sizeof(savedFontName));
        memcpy(&savedFont, data, size);
    }
}
void R_IssuePendingRenderCommands(void) {}
image_t *R_CreateImage(const char *name, byte *pic, int w, int h, imgType_t type, imgFlags_t flags, int format) {
    static image_t image;
    (void)name; (void)pic; (void)w; (void)h; (void)type; (void)flags; (void)format;
    if (images < 128) Q_strncpyz(imageNames[images], name, sizeof(imageNames[images]));
    images++; return &image;
}
qhandle_t RE_RegisterShaderFromImage(const char *name, int lightmap, image_t *image, qboolean mip) {
    (void)name; (void)lightmap; (void)image; (void)mip; return 1;
}
qhandle_t RE_RegisterShaderNoMip(const char *name) {
    int i;
    for (i = 0; i < images && i < 128; i++) if (!strcmp(name, imageNames[i])) return 2;
    return 0;
}

static int failures;
#define CHECK(condition, message) do { if (!(condition)) { puts("FAIL: " message); failures++; } } while (0)
int main(int argc, char **argv) {
    FILE *file;
    fontInfo_t font, cached;
    int before, i;
    if (argc != 2 || !(file = fopen(argv[1], "rb"))) return 2;
    fseek(file, 0, SEEK_END); ttfSize = ftell(file); rewind(file);
    ttfData = malloc(ttfSize);
    if (fread(ttfData, 1, ttfSize, file) != ttfSize) return 2;
    fclose(file);
    ri.Printf = Print; ri.Malloc = Alloc; ri.Free = Free;
    ri.FS_ReadFile = Read; ri.FS_FreeFile = FreeFile; ri.FS_WriteFile = Write;
    saveFont.integer = 1;
    R_InitFreeType();
    memset(&font, 0, sizeof(font));
    RE_RegisterFont("fonts/test.ttf", 48, &font);
    CHECK(font.glyphs['A'].glyph == 1, "generated font has a glyph atlas");
    CHECK(liveFaces == 0 && fileBuffers == 0 && allocations == 0, "font resources released");
    before = images;
    for (i = 0; i < 8; i++) {
        memset(&cached, 0, sizeof(cached));
        RE_RegisterFont("fonts/test.ttf", 48, &cached);
        CHECK(cached.glyphs['A'].glyph == 1, "repeat registration succeeds");
    }
    CHECK(images == before, "repeated font request reuses atlas");
    RE_RegisterFont("FONTS\\TEST.TTF", 48, &cached);
    CHECK(images == before && cached.glyphs['A'].glyph == 1, "font cache normalizes path case and separators");
    CHECK(RE_RegisterShaderNoMip(font.glyphs['A'].shaderName) == 2, "serialized atlas name resolves without truncation");
    R_DoneFreeType();
    R_InitFreeType();
    RE_RegisterFont("fonts/test.ttf", 48, &cached);
    CHECK(cached.glyphs['A'].glyph == 2 && images == before, "saved font reloads atlas without regenerating");
    savedFontName[0] = 0;
    saveFont.integer = 0;
    R_DoneFreeType();
    liveFaces = 0;
    R_InitFreeType();
    for (i = 0; i < MAX_FONTS; i++) RE_RegisterFont("fonts/test.ttf", 12 + i, &font);
    memset(&cached, 0, sizeof(cached));
    RE_RegisterFont("fonts/test.ttf", 12, &cached);
    CHECK(cached.glyphs['A'].glyph == 1, "cached fonts work at capacity");
    R_DoneFreeType();
    liveFaces = 0;
    R_InitFreeType();
    legacyAvailable = 1;
    legacyFont.glyphScale = 99.0f;
    RE_RegisterFont("fonts/test.ttf", 48, &font);
    CHECK(font.glyphScale != 99.0f, "available TTF wins over unrelated size-only cache");
    R_DoneFreeType();
    R_InitFreeType();
    ttfAvailable = 0;
    RE_RegisterFont("fonts/missing.ttf", 48, &font);
    CHECK(font.glyphScale == 99.0f, "legacy-only font packs still load");
    R_DoneFreeType();
    R_InitFreeType();
    ttfAvailable = 1;
    legacyAvailable = 0;
    before = images;
    RE_RegisterFont("fonts/oversized.ttf", 1024, &font);
    CHECK(font.glyphScale == 0 && images == before, "oversized glyphs fail without endless atlas allocation");
    CHECK(liveFaces == 0 && fileBuffers == 0 && allocations == 0, "oversized font releases its resources");
    R_DoneFreeType();
    R_InitFreeType();
    ttfAvailable = 1;
    legacyAvailable = 0;
    ttfSize = 8; /* invalid/truncated TTF: FreeType rejects it */
    RE_RegisterFont("fonts/invalid.ttf", 48, &font);
    CHECK(liveFaces == 0 && fileBuffers == 0 && allocations == 0, "invalid font releases its input buffer");
    R_DoneFreeType();
    free(ttfData);
    if (!failures) puts("PASS: font caching, capacity, legacy selection, and resource cleanup");
    return failures ? 1 : 0;
}
