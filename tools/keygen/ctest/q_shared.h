/* Shim q_shared.h for the standalone license interop test.
   Provides only what qcommon/license.c pulls from the real header, so the
   ACTUAL license.c (copied into build/) compiles outside the engine. */
#ifndef Q_SHARED_SHIM
#define Q_SHARED_SHIM
#include <string.h>
typedef unsigned char byte;
typedef enum { qfalse, qtrue } qboolean;
#define ARRAY_LEN(x) (sizeof(x)/sizeof(*(x)))
#define S_COLOR_RED ""
#define S_COLOR_GREEN ""
#define S_COLOR_YELLOW ""
#define S_COLOR_WHITE ""
#endif
