#!/usr/bin/env python3
"""Exercise the actual time-bind controller against real-time command sequences."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'engine/code/game/g_active.c').read_text()
start=s.index('static void G_UpdateTimeBind(')
fn=s[start:s.index('\n/*',start)]
# Regression: clock updates must precede the zero-delta movement early return.
think=s[s.index('void ClientThink_real'):]
assert think.index('G_UpdateTimeBind( ent, ucmd )') < think.index('if ( msec < 1')
pre=r'''
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
typedef int qboolean;
#define qfalse 0
#define qtrue 1
#define SVF_BOT 1
#define STAT_HEALTH 0
#define PMF_DUCKED 1
#define BUTTON_ATTACK 1
#define BUTTON_BLOCK 2
#define BUTTON_WALKING 4
typedef struct { int integer; float value; } vmCvar_t;
vmCvar_t g_timeBind={1,1},g_timeBindMin={0,.05},g_timeBindMax={1,1},g_timeBindRef={700,700},g_timeBindCurve={1,1.5},g_timeBindFire={0,.8},g_timeBindLog={1,1},g_timeBindBlock={0,.5},g_timeBindCrouch={0,.5},g_timeBindRise={30,30},g_timeBindSmooth={8,8},pmove_fixed={0,0};
typedef struct { int forwardmove,rightmove,upmove,buttons; } usercmd_t;
typedef struct { struct { int stats[1],pm_flags; float velocity[3]; } ps; int dashSurge; } client_t;
typedef struct {struct {int svFlags;} r; client_t *client;} gentity_t;
struct {int time;} level;
static int real=100; static float applied=1;
#define G_Printf printf
float trap_Cvar_VariableValue(const char *name){return applied;}
int trap_Cvar_VariableIntegerValue(const char *name){return 0;}
int trap_Milliseconds(void){return real;}
char *va(const char *f,...){static char b[64];va_list a;va_start(a,f);vsnprintf(b,64,f,a);va_end(a);return b;}
void trap_Cvar_Set(const char *key,const char *v){if(!strcmp(key,"timescale"))applied=atof(v);}
'''
post=r'''
void advance(gentity_t *e,usercmd_t *u,int ms){for(int i=0;i<ms;i+=10){real+=10;G_UpdateTimeBind(e,u);}}
int main(void){
 client_t c={0}; c.ps.stats[0]=100; gentity_t e={{0},&c}; usercmd_t u={0};
 advance(&e,&u,500);assert(fabs(applied-.05)<.003);
 for(int n=0;n<4;n++){
  u.forwardmove=127;advance(&e,&u,200);assert(applied>.97);
  u.forwardmove=0;c.ps.velocity[0]=700;advance(&e,&u,1000);assert(applied<.053);
 }
 u.forwardmove=64;u.buttons=BUTTON_WALKING;advance(&e,&u,200);assert(applied>.97);
 // Another clock writer changes actual speed while held input/target stay unchanged.
 applied=.05;advance(&e,&u,100);assert(applied>.97);
 applied=.16;advance(&e,&u,100);assert(applied>.97);
 u.forwardmove=0;u.buttons=0;advance(&e,&u,1000);assert(applied<.053);
 u.upmove=127;advance(&e,&u,200);assert(applied>.97);
 u.upmove=0;advance(&e,&u,1000);assert(applied<.053);
 g_timeBind.integer=0;advance(&e,&u,10);assert(applied==1);
 g_timeBind.integer=1;advance(&e,&u,10);assert(applied<.053);
 puts("PASS: idle, repeated move/release, residual velocity, walking, jump, disable/re-enable, pre-physics input ordering");
}
'''
with tempfile.TemporaryDirectory(prefix='s64-timebind-') as d:
 p=Path(d)/'test.c';p.write_text(pre+fn+post)
 exe=Path(d)/'test'
 subprocess.run(['cc',str(p),'-lm','-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
