#!/usr/bin/env python3
"""Test the actual ragdoll scheduler/render interpolation with a deterministic motion fixture."""
from pathlib import Path
import subprocess,tempfile
s=(Path(__file__).resolve().parents[1]/'engine/code/cgame/cg_ragdoll.c').read_text()
a=s.index('static void CG_RagdollStep('); b=s.index('\n/*\n===============\nCG_RagdollSegment',a)
pre=r'''
#include <math.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#define RS_COUNT 2
#define RB_COUNT 1
#define RAG_FIXED_DT 0.008f
#define qtrue 1
#define Com_Clamp(a,b,x) fmin(b,fmax(a,x))
typedef struct { double lastTime,accumulator; int historyValid,settled,skel; float pos[2][3],prev[2][3],renderPrev[2][3],boneRot[1][4],rotationPrev[1][4],spin,spinPrev; } cgRagdoll_t;
struct {double animTime;} cg;
static int steps;
void RagQuatNormalize(float *q){float l=sqrt(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]);for(int i=0;i<4;i++)q[i]/=l;}
void CG_RagdollSubstep(cgRagdoll_t *r){steps++;r->prev[0][0]=r->pos[0][0];r->pos[0][0]+=RAG_FIXED_DT*100;r->spin+=RAG_FIXED_DT;}
'''
post=r'''
int main(void){
 for(int fps=30;fps<=240;fps+=30){
  cgRagdoll_t r={0},out;steps=0;cg.animTime=0;
  r.skel=1;r.boneRot[0][3]=1;
  for(int i=1;i<=fps*2;i++){
   cg.animTime=i*1000.0/fps;CG_RagdollStep(&r);CG_RagdollRenderPose(&r,&out);
   assert(isfinite(out.pos[0][0]));assert(out.pos[0][0]<=r.pos[0][0]);
   assert(fabs(out.boneRot[0][3]-1)<.00001);
  }
  assert(steps>=249 && steps<=250);
  int old=steps;CG_RagdollStep(&r);assert(steps==old);
  cg.animTime+=10000;CG_RagdollStep(&r);assert(steps-old<=8);
  cg.animTime=0;CG_RagdollStep(&r);assert(r.accumulator==0);
 }
 cgRagdoll_t r={0},out;double last=-1;cg.animTime=0;
 for(int i=1;i<1000;i++){
  cg.animTime=i*7*.05;CG_RagdollStep(&r);CG_RagdollRenderPose(&r,&out);
  if(i>25)assert(out.pos[0][0]>last);
  last=out.pos[0][0];
 }
 puts("PASS: 30–240 FPS fixed-step equivalence, bounded hitch work, rewind reset, quaternion interpolation, continuous 0.05x presentation");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test.c';p.write_text(pre+s[a:b]+post)
 subprocess.run(['cc',str(p),'-lm','-o',d+'/test'],check=True)
 subprocess.run([d+'/test'],check=True)
