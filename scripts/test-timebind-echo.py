#!/usr/bin/env python3
"""Exercise the actual loopback SYSTEMINFO guard against delayed clock echoes."""
from pathlib import Path
import subprocess,tempfile
s=(Path(__file__).resolve().parents[1]/'engine/code/client/cl_parse.c').read_text()
a=s.index('static qboolean CL_IsLocalTimeScaleEcho');b=s.index('\n}',a)+2
pre=r'''
#include <assert.h>
#include <strings.h>
#include <stdio.h>
typedef int qboolean;
struct {int integer;} running={1};
#define com_sv_running (&running)
struct {int serverAddress;} clc={1};
int NET_IsLocalAddress(int a){return a==1;}
#define Q_stricmp strcasecmp
'''
post=r'''
int main(void){
 float clock=1, delayed[]={.05,.1,.2,.05};
 for(int i=0;i<4;i++){if(!CL_IsLocalTimeScaleEcho("timescale"))clock=delayed[i];assert(clock==1);}
 assert(!CL_IsLocalTimeScaleEcho("pmove_msec"));
 assert(!CL_IsLocalTimeScaleEcho("fs_game"));
 assert(CL_IsLocalTimeScaleEcho("TimeScale"));
 clc.serverAddress=2;assert(!CL_IsLocalTimeScaleEcho("timescale"));
 clc.serverAddress=1;running.integer=0;assert(!CL_IsLocalTimeScaleEcho("timescale"));
 puts("PASS: delayed loopback clock echoes ignored; remote and unrelated settings preserved");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test.c';p.write_text(pre+s[a:b]+post)
 subprocess.run(['cc',str(p),'-o',d+'/test'],check=True)
 subprocess.run([d+'/test'],check=True)
