/* Shim qcommon.h for the standalone license interop test. Declares the engine
   API surface qcommon/license.c calls; stubs live in test_main.c. */
#ifndef QCOMMON_SHIM
#define QCOMMON_SHIM
#include "q_shared.h"
typedef struct cvar_s { int integer; float value; char *string; } cvar_t;
#define CVAR_ROM 1
#define CVAR_ARCHIVE 2
#define CVAR_PROTECTED 4
typedef enum { ERR_FATAL } errParm_t;
cvar_t *Cvar_Get(const char*,const char*,int);
void Cvar_Set(const char*,const char*);
void Cvar_SetValue(const char*,float);
int  Cmd_Argc(void);
char *Cmd_Args(void);
void Cmd_AddCommand(const char*,void(*)(void));
void Com_Printf(const char*,...);
void Com_Error(int,const char*,...);
#define Com_Memset memset
int Com_LicenseSelfTest(const char*);
#endif
