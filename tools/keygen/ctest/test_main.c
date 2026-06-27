/* Standalone driver for the license interop test. Reads one key per stdin
   line, prints "<tier> <key>" (tier -1 == rejected). Stubs the engine API so
   the real qcommon/license.c links without the rest of the engine. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qcommon.h"

static cvar_t dummy = {0, 0, (char *)""};
cvar_t *Cvar_Get(const char *a, const char *b, int c) { (void)a;(void)b;(void)c; return &dummy; }
void Cvar_Set(const char *a, const char *b) { (void)a;(void)b; }
void Cvar_SetValue(const char *a, float b) { (void)a;(void)b; }
int  Cmd_Argc(void) { return 0; }
char *Cmd_Args(void) { return (char *)""; }
void Cmd_AddCommand(const char *a, void (*b)(void)) { (void)a;(void)b; }
void Com_Printf(const char *f, ...) { (void)f; }
void Com_Error(int e, const char *f, ...) { (void)e; fprintf(stderr, "Com_Error: %s\n", f); exit(2); }

int main(void) {
	char line[512];
	while (fgets(line, sizeof line, stdin)) {
		size_t n = strlen(line);
		while (n && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = 0;
		if (!n) continue;
		printf("%d %s\n", Com_LicenseSelfTest(line), line);
	}
	return 0;
}
