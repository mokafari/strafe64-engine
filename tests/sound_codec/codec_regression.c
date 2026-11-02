/* Real codec routing with fake decoders, testing both load and stream paths. */
#include "../../engine/code/client/snd_codec.c"

snd_codec_t wav_codec;
static snd_stream_t result;
static const char *available;
static char publishedFormats[128];
static int resets;

void QDECL Com_Printf(const char *fmt, ...) { (void)fmt; }
void QDECL Com_DPrintf(const char *fmt, ...) { (void)fmt; }
void QDECL Com_Error(int level, const char *fmt, ...) { (void)level; (void)fmt; abort(); }
long FS_FOpenFileRead(const char *path, fileHandle_t *file, qboolean unique) {
    (void)path; (void)unique; *file = 0; return -1;
}
void FS_FCloseFile(fileHandle_t file) { (void)file; }
void *Z_Malloc(int size) { return calloc(1, size); }
void Z_Free(void *p) { free(p); }
void S_AudioAnalyzeReset(void) { resets++; }
void S_AudioAnalyze(const void *p, int frames, int rate, int width, int channels) {
    (void)p; (void)frames; (void)rate; (void)width; (void)channels;
}
cvar_t *Cvar_Get(const char *name, const char *value, int flags) {
    (void)name; (void)value; (void)flags; return NULL;
}
void Cvar_Set(const char *name, const char *value) {
    if (!strcmp(name, "s_supportedCodecs")) Q_strncpyz(publishedFormats, value, sizeof(publishedFormats));
}
static void *Load(const char *name, snd_info_t *info) {
    (void)info; return available && !strcmp(name, available) ? &result : NULL;
}
static snd_stream_t *Open(const char *name) { return Load(name, NULL); }
static int failures;
#define CHECK(c, msg) do { if (!(c)) { puts("FAIL: " msg); failures++; } } while (0)
int main(void) {
    snd_info_t info;
    wav_codec.ext = "wav"; wav_codec.load = Load; wav_codec.open = Open;
    S_CodecInit();
    CHECK(!strcmp(publishedFormats, "wav"), "publishes only compiled codecs");
    CHECK(S_CodecSupportsExtension("WAV") && !S_CodecSupportsExtension("mp3"), "codec support query");
    available = "music/test.wav";
    CHECK(S_CodecLoad("music/test.mp3", &info) == &result, "unsupported-extension load falls back to same basename");
    CHECK(S_CodecOpenStream("music/test.mp3") == &result, "unsupported-extension stream falls back");
    CHECK(resets == 1, "successful stream resets analyzer");
    CHECK(S_CodecLoad("music/test", &info) == &result, "extensionless requests still work");
    CHECK(S_CodecLoad("music/test.wav", &info) == &result, "native-extension requests still work");
    CHECK(S_CodecOpenStream("music/missing.mp3") == NULL, "missing file returns no stream");
    CHECK(resets == 1, "failed stream leaves analyzer untouched");
    S_CodecShutdown();
    if (!failures) puts("PASS: codec fallback, supported formats, and stream handling");
    return failures ? 1 : 0;
}
