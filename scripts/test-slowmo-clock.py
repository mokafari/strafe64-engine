#!/usr/bin/env python3
"""Compile and exercise the actual engine clock with minimal host stubs."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root / 'engine/code/qcommon/common.c').read_text()
clock = source[source.index('double com_frameFraction'):source.index('/*\n=================\nCom_TimeVal')]
prefix = '#include <math.h>\n#include <stdio.h>\n#include <assert.h>\ntypedef struct { float value; int integer; } cvar_t;\ncvar_t ts={1,1}, ft={0,0}, cam={0,0}, ded={0,0}, sv={1,1};\ncvar_t *com_timescale=&ts,*com_fixedtime=&ft,*com_cameraMode=&cam,*com_dedicated=&ded,*com_sv_running=&sv;\n#define Com_Printf printf\n'
checks = 'int main(void) {\n float rates[]={0.01f,0.05f,0.06f,0.1f,0.5f,1.0f};\n for(int r=0;r<6;r++) {\n  ts.value=com_effectiveTimeScale=rates[r]; com_frameFraction=0;\n  int whole=0; double last=0;\n  for(int i=0;i<1000;i++) {\n   whole+=Com_ModifyMsec(7); double now=whole+com_frameFraction;\n   assert(now>last); assert(fabs((now-last)-7*rates[r])<0.00001); last=now;\n  }\n  assert(fabs(last-7000*rates[r])<0.001);\n }\n ts.value=0.05f; com_effectiveTimeScale=1; float prev=1;\n for(int i=0;i<100;i++){Com_ModifyMsec(7);assert(com_effectiveTimeScale==ts.value);prev=com_effectiveTimeScale;}\n assert(fabs(prev-0.05f)<0.00001);\n double elapsed=0; com_frameFraction=0; int whole=0;\n for(int i=0;i<300;i++){ts.value=i<150 ? .05f+.95f*i/149 : 1.0f-.95f*(i-150)/149; whole+=Com_ModifyMsec(7); double next=whole+com_frameFraction; assert(com_effectiveTimeScale==ts.value); assert(next>elapsed); assert(fabs(next-elapsed-7*ts.value)<0.00001); elapsed=next;}\n ft.integer=16; assert(Com_ModifyMsec(7)==16 && com_frameFraction==0);\n puts("PASS: six slow-motion rates, fractional pose progression, elapsed-time conservation, single-owner speed changes, fixed-time reset");\n}\n'
# Compile the renderer's actual clock too: changing speed must never
# reinterpret elapsed uptime or move its timestamp backwards.
renderer = (root / 'engine/code/client/cl_main.c').read_text()
a = renderer.index('int CL_ScaledMilliseconds(void)')
b = renderer.index('\n}', a) + 2
prefix += 'struct { int realtime; } cls;\n'
clock += '\n' + renderer[a:b] + '\n'
checks = checks.replace(' ft.integer=16;', ' cls.realtime=123456; ts.value=.05f; assert(CL_ScaledMilliseconds()==123456); ts.value=1; assert(CL_ScaledMilliseconds()==123456); cls.realtime++; assert(CL_ScaledMilliseconds()==123457);\n ft.integer=16;')
with tempfile.TemporaryDirectory(prefix='s64-clock-') as tmp:
    src = Path(tmp) / 'clock.c'
    exe = Path(tmp) / 'clock'
    src.write_text(prefix + clock + checks)
    subprocess.run(['cc', str(src), '-lm', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
