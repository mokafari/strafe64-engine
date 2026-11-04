#!/usr/bin/env python3
"""Compile the actual scarf collision helper and check contact edge cases."""
from pathlib import Path
import subprocess
import tempfile
source=(Path(__file__).resolve().parents[1]/'engine/code/cgame/cg_players.c').read_text()
helper=source[source.index('static void CG_ScarfCollide('):source.index('static void CG_SkelScarf(')]
shim=r'''
#include <math.h>
#include <assert.h>
typedef float vec3_t[3];
#define VectorSubtract(a,b,c) do { for(int k=0;k<3;k++) (c)[k]=(a)[k]-(b)[k]; } while(0)
#define VectorCopy(a,c) do { for(int k=0;k<3;k++) (c)[k]=(a)[k]; } while(0)
#define VectorMA(a,s,b,c) do { for(int k=0;k<3;k++) (c)[k]=(a)[k]+(s)*(b)[k]; } while(0)
#define DotProduct(a,b) ((a)[0]*(b)[0]+(a)[1]*(b)[1]+(a)[2]*(b)[2])
static float Com_Clamp(float lo,float hi,float x){return fminf(hi,fmaxf(lo,x));}
static float VectorNormalize(vec3_t v){float n=sqrtf(DotProduct(v,v));if(n>0)for(int k=0;k<3;k++)v[k]/=n;return n;}
'''
tests=r'''
int main(void){
 vec3_t a={0,0,0},b={0,0,20},back={-1,0,0};
 vec3_t p={-2,0,10},v={4,3,0};
 CG_ScarfCollide(p,v,a,b,back,10.5f);
 assert(fabsf(p[0]+10.5f)<1e-5f && v[0]==0 && v[1]==3);
 vec3_t center={0,0,10},zero={0,0,0};
 CG_ScarfCollide(center,zero,a,b,back,10.5f);
 assert(center[0]==-10.5f && isfinite(center[2]));
 vec3_t outside={-20,0,10},outv={-3,2,0};
 CG_ScarfCollide(outside,outv,a,b,back,10.5f);
 assert(outside[0]==-20 && outv[0]==-3);
 vec3_t cap={0,0,23};
 CG_ScarfCollide(cap,zero,a,b,back,10.5f);
 assert(cap[2]==30.5f);
 vec3_t degenerate={0,0,0};
 CG_ScarfCollide(degenerate,zero,a,a,back,10.5f);
 assert(degenerate[0]==-10.5f && isfinite(degenerate[2]));
 return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'test.c').write_text(shim+helper+tests)
 subprocess.run(['cc','-std=c99',str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Scarf collision: interior contact, inward velocity, centerline, exterior, end cap and zero-length torso passed.')
