/* Full production pmove with deterministic swept-box/plane collision fixtures.
   This is collision-contract coverage, not a substitute for BSP map tests. */
#include "../../engine/code/qcommon/q_shared.h"
#include "../../engine/code/game/bg_public.h"
#include "../../engine/code/game/bg_local.h"
#include "../../engine/code/game/bg_bot_traversal.h"
extern int pm_wallRunDurationMs;
static pmove_t move;
static playerState_t state;
static double world,dt;
static float bob;
static int failures, floorOn=1, wallOn, sideOn, ceilingOn, slopeOn;
static int jumpEvents, airEvents, wallEvents;
static float ceilingHeight=50, obstacleHeight, blockFar=200;
static int vaultEvents, ledgeOn, canopyOn;
static float waterSurface=-10000;
#define CHECK(c) do {if(!(c)){printf("FAIL line %d: %s (xyz %.3f %.3f %.3f vel %.3f %.3f %.3f)\n",__LINE__,#c,state.origin[0],state.origin[1],state.origin[2],state.velocity[0],state.velocity[1],state.velocity[2]);failures++;}}while(0)
void QDECL Com_Printf(const char *fmt,...) {}
void QDECL Com_Error(int level,const char *fmt,...) {abort();}
void trap_SnapVector(float *v) {int i;for(i=0;i<3;i++)v[i]=(float)(int)v[i];}
static int contents(const vec3_t p,int pass) {
 if(obstacleHeight>0 && p[0]>=40 && p[0]<=blockFar && fabsf(p[1])<=100 && p[2]>=0 && p[2]<obstacleHeight)return CONTENTS_SOLID;
 return p[2]<waterSurface?CONTENTS_WATER:0;
}
/* Finite solid block for actual step-slide/vault traversal. Minkowski-expand
   its box by the player hull, then intersect the swept origin segment. */
static void blockTrace(trace_t *t,const vec3_t start,const vec3_t mins,const vec3_t maxs,const vec3_t end) {
 float lo[3]={40,-100,0},hi[3]={200,100,0},enter=-1e20f,leave=1e20f;
 int j,axis=-1,sign=0,inside=1,endInside=1;
 hi[2]=obstacleHeight;hi[0]=blockFar;
 for(j=0;j<3;j++) {
  float delta=end[j]-start[j],a,b;int face=-1;
  lo[j]-=maxs?maxs[j]:0;hi[j]-=mins?mins[j]:0;
  if(start[j]<=lo[j]+.00001f || start[j]>=hi[j]-.00001f)inside=0;
  if(end[j]<=lo[j]+.00001f || end[j]>=hi[j]-.00001f)endInside=0;
  if(fabsf(delta)<1e-8f) {if(start[j]<=lo[j] || start[j]>=hi[j])return;continue;}
  a=(lo[j]-start[j])/delta;b=(hi[j]-start[j])/delta;
  if(a>b){float swap=a;a=b;b=swap;face=1;}
  if(a>enter){enter=a;axis=j;sign=face;}if(b<leave)leave=b;
 }
 if(inside){t->startsolid=qtrue;if(endInside){t->allsolid=qtrue;t->fraction=0;t->entityNum=ENTITYNUM_WORLD;}return;}
 if(enter<0 || enter>leave || enter>=t->fraction || axis<0)return;
 t->fraction=enter;t->entityNum=ENTITYNUM_WORLD;VectorClear(t->plane.normal);t->plane.normal[axis]=(float)sign;
}
static void trace(trace_t *t,const vec3_t start,const vec3_t mins,const vec3_t maxs,
 const vec3_t end,int pass,int mask) {
 /* Free space: z>=0, x<=100, y<=100, z<=50 when each boundary is enabled. */
 float normals[4][3]={{0,0,1},{-1,0,0},{0,-1,0},{0,0,-1}};
 float distances[4]={0,-100,-100,0};
 int active[4]={floorOn,wallOn,sideOn,ceilingOn},i,j;
 distances[3]=-ceilingHeight;
 if(slopeOn) {normals[0][0]=-.242535625f;normals[0][2]=.9701425f;}
 memset(t,0,sizeof(*t));t->fraction=1;t->entityNum=ENTITYNUM_NONE;
 for(i=0;i<4;i++) if(active[i]) {
  if(i==3 && canopyOn && start[0]+(mins?mins[0]:0)>=10 && end[0]+(mins?mins[0]:0)>=10)continue;
  float from=-distances[i],to=-distances[i],fraction;
  for(j=0;j<3;j++) {
   float bound=normals[i][j]>=0?(mins?mins[j]:0):(maxs?maxs[j]:0);
   from+=normals[i][j]*(start[j]+bound);to+=normals[i][j]*(end[j]+bound);
  }
  if(from < -.001f) {
   t->startsolid=qtrue;
   if(to<-.001f){t->allsolid=qtrue;t->fraction=0;t->entityNum=ENTITYNUM_WORLD;}
   continue;
  }
  if(to>=0 || to>=from)continue;
  fraction=from/(from-to);if(fraction<0)fraction=0;
  if(i==0 && ledgeOn && start[0]+fraction*(end[0]-start[0])+(mins?mins[0]:0)>=0)continue;
  if(fraction<t->fraction) {t->fraction=fraction;VectorCopy(normals[i],t->plane.normal);t->entityNum=ENTITYNUM_WORLD;}
 }
 if(obstacleHeight>0)blockTrace(t,start,mins,maxs,end);
 for(j=0;j<3;j++)t->endpos[j]=start[j]+t->fraction*(end[j]-start[j]);
}
static void setup(void) {
 memset(&move,0,sizeof(move));memset(&state,0,sizeof(state));world=0;bob=0;
 jumpEvents=airEvents=wallEvents=0;ceilingHeight=50;obstacleHeight=0;vaultEvents=0;ledgeOn=canopyOn=0;waterSurface=-10000;floorOn=1;wallOn=sideOn=ceilingOn=slopeOn=0;
 move.ps=&state;move.trace=trace;move.pointcontents=contents;
 move.tracemask=MASK_PLAYERSOLID;move.fractionalStep=qtrue;move.timeScale=1;move.bobFraction=&bob;
 state.pm_type=PM_NORMAL;state.gravity=800;state.speed=320;state.stats[STAT_HEALTH]=100;
 state.origin[2]=24;state.groundEntityNum=ENTITYNUM_WORLD;
}
static void tick(void) {
 int old=state.eventSequence,j,before=(int)world;
 world+=dt;move.worldMsec=(float)dt;move.wholeMsec=(int)world-before;
 move.cmd.serverTime=(int)world;Pmove(&move);
 for(j=old;j<state.eventSequence;j++) {
  int e=state.events[j&(MAX_PS_EVENTS-1)];if(e==EV_JUMP)jumpEvents++;if(e==EV_DOUBLE_JUMP)airEvents++;if(e==EV_WALLJUMP)wallEvents++;if(e==EV_VAULT)vaultEvents++;
 }
 CHECK(isfinite(state.origin[0]) && isfinite(state.origin[1]) && isfinite(state.origin[2]));
}
static void run(double worldMs) {int i,n=(int)ceil(worldMs/dt);for(i=0;i<n;i++)tick();}
static float horizontal(void) {return sqrtf(state.velocity[0]*state.velocity[0]+state.velocity[1]*state.velocity[1]);}
int main(int argc,char **argv) {
 const char *name=argc>1?argv[1]:"";float straight,diagonal;
 dt=(argc>2 && !strcmp(argv[2],"slow"))?.32:8;setup();
 if(!strncmp(name,"route-",6)) {
  botRoute_t r;botRouteRun_t st;vec3_t target,angles;int result=0,jump=0,i;
  memset(&r,0,sizeof(r));memset(&st,0,sizeof(st));state.gravity=1000;
  if(!strcmp(name,"route-wallkick") || !strcmp(name,"route-no-wall")) {
   wallOn=1;r.wallkick=1;VectorSet(r.entry,-30,0,24);
   VectorSet(r.contact,83,0,80);VectorSet(r.exit,-30,160,24);
  } else {
   obstacleHeight=64;VectorSet(r.entry,-80,0,24);
   VectorSet(r.contact,20,0,110);VectorSet(r.exit,120,0,88);
  }
  if(!strcmp(name,"route-cover")) {
   obstacleHeight=40;blockFar=104;r.speed=260;r.airjump=1;
   VectorSet(r.entry,-60,0,24);VectorSet(r.contact,72,0,88);VectorSet(r.exit,180,0,24);
  }
  VectorCopy(r.entry,state.origin);
  if(!strcmp(name,"route-no-wall")) wallOn=0;
  if(!strcmp(name,"route-held")) state.pm_flags|=PMF_JUMP_HELD;
  if(!strcmp(name,"route-timeout")) {
   result=BotRouteStep(&r,&st,&state,4501,qfalse,target,&jump);
   CHECK(result==BOT_ROUTE_FAILED);return failures?1:0;
  }
  if(!strcmp(name,"route-death")) {
   state.pm_type=PM_DEAD;
   CHECK(BotRouteStep(&r,&st,&state,0,qfalse,target,&jump)==BOT_ROUTE_FAILED);
   return failures?1:0;
  }
  for(i=0;i<(int)(4600/dt);i++) {
   qboolean nearWall=wallOn && state.origin[0]>59;
   result=BotRouteStep(&r,&st,&state,(int)world,nearWall,target,&jump);
   if(result!=BOT_ROUTE_RUNNING) break;
   BotRouteSteer(&r,&st,&state,target,jump,&move.cmd,angles);
   tick();
  }
  if(!strcmp(name,"route-no-wall")) {CHECK(result==BOT_ROUTE_FAILED);CHECK(wallEvents==0);return failures?1:0;}
  CHECK(result==BOT_ROUTE_SUCCESS);CHECK(jumpEvents==1);
  if(r.wallkick) CHECK(wallEvents==1);
  if(r.airjump) CHECK(airEvents==1);
 } else if(!strcmp(name,"acceleration")) {
  move.cmd.forwardmove=127;run(500);CHECK(state.velocity[0]>300 && state.velocity[0]<=320.01f);
  CHECK(fabsf(state.origin[2]-24)<.01 && state.groundEntityNum==ENTITYNUM_WORLD);
 } else if(!strcmp(name,"braking")) {
  state.velocity[0]=320;run(1500);CHECK(horizontal()<.01);CHECK(state.origin[0]>=0 && state.origin[0]<100);
 } else if(!strcmp(name,"diagonal")) {
  move.cmd.forwardmove=127;run(500);straight=horizontal();
  setup();move.cmd.forwardmove=move.cmd.rightmove=127;run(500);diagonal=horizontal();
  CHECK(fabsf(straight-diagonal)<.05 && diagonal<=320.05);
 } else if(!strcmp(name,"jump-held")) {
  move.cmd.upmove=127;tick();CHECK(state.velocity[2]>290 && state.origin[2]>24);
  run(200);CHECK(jumpEvents==1 && airEvents==0 && state.stats[STAT_AIRJUMP_COUNT]==0);
 } else if(!strcmp(name,"air-jump-limit")) {
  move.cmd.upmove=127;tick();move.cmd.upmove=0;tick();move.cmd.upmove=127;tick();
  CHECK(airEvents==1 && state.stats[STAT_AIRJUMP_COUNT]==1);
  move.cmd.upmove=0;tick();move.cmd.upmove=127;tick();CHECK(airEvents==1);
 } else if(!strcmp(name,"landing")) {
  state.origin[2]=100;state.velocity[2]=-200;state.groundEntityNum=ENTITYNUM_NONE;
  state.stats[STAT_AIRJUMP_COUNT]=1;state.stats[STAT_WALLJUMP_COUNT]=1;run(1000);
  CHECK(state.origin[2]>=23.999f && state.origin[2]<=24.251f && fabsf(state.velocity[2])<.01);
  CHECK(state.groundEntityNum==ENTITYNUM_WORLD && state.stats[STAT_AIRJUMP_COUNT]==0 && state.stats[STAT_WALLJUMP_COUNT]==0);
 } else if(!strcmp(name,"crouch-clearance")) {
  move.cmd.upmove=-127;tick();CHECK(state.pm_flags&PMF_DUCKED);
  ceilingOn=1;move.cmd.upmove=0;tick();CHECK(state.pm_flags&PMF_DUCKED);
  ceilingOn=0;tick();CHECK(!(state.pm_flags&PMF_DUCKED) && state.viewheight==DEFAULT_VIEWHEIGHT);
 } else if(!strcmp(name,"wall-slide")) {
  wallOn=1;state.origin[0]=84;move.cmd.forwardmove=127;move.cmd.rightmove=-127;
  run(500);CHECK(state.origin[0]<=85.01 && state.origin[1]>50);CHECK(fabsf(state.origin[2]-24)<.02);
 } else if(!strcmp(name,"corner")) {
  wallOn=sideOn=1;move.cmd.forwardmove=127;move.cmd.rightmove=-127;run(1000);
  CHECK(state.origin[0]<=85.01 && state.origin[1]<=85.01);CHECK(state.origin[0]>84 && state.origin[1]>84);
 } else if(!strcmp(name,"coyote")) {
  tick();floorOn=0;run(32);move.cmd.upmove=127;tick();
  CHECK(jumpEvents==1 && airEvents==0 && state.velocity[2]>290);
 } else if(!strcmp(name,"slide")) {
  state.velocity[0]=400;move.cmd.upmove=-127;run(16);
  CHECK((state.pm_flags&PMF_SLIDING) && horizontal()>350);
  move.cmd.upmove=0;tick();CHECK(!(state.pm_flags&PMF_SLIDING));
 } else if(!strcmp(name,"slope")) {
  slopeOn=1;state.origin[2]=27.75f;move.cmd.forwardmove=127;run(500);
  CHECK(state.origin[0]>50 && state.origin[2]>40 && state.velocity[2]>0);
  CHECK(state.groundEntityNum==ENTITYNUM_WORLD);
  CHECK(fabsf(state.origin[2]-(24+.25f*(state.origin[0]+15)))<.26f);
 } else if(!strcmp(name,"coyote-expired")) {
  tick();floorOn=0;run(350);move.cmd.upmove=127;tick();
  CHECK(jumpEvents==0 && airEvents==1 && state.stats[STAT_AIRJUMP_COUNT]==1);
 } else if(!strcmp(name,"coyote-head-bump")) {
  ceilingOn=1;ceilingHeight=56.01f;move.cmd.upmove=127;tick();
  CHECK(jumpEvents==1);
  floorOn=ceilingOn=0;run(160);
  CHECK(jumpEvents==1 && airEvents==0);
 } else if(!strcmp(name,"coyote-held")) {
  tick();floorOn=0;tick();state.pm_flags|=PMF_JUMP_HELD;
  move.cmd.upmove=127;tick();CHECK(jumpEvents==0 && airEvents==0);
 } else if(!strcmp(name,"coyote-single-use")) {
  tick();floorOn=0;run(16);move.cmd.upmove=127;tick();CHECK(jumpEvents==1);
  run(120);move.cmd.upmove=0;tick();move.cmd.upmove=127;tick();
  CHECK(jumpEvents==1 && airEvents==1);
 } else if(!strcmp(name,"bunnyhop")) {
  move.cmd.upmove=127;run(1800);
  CHECK(jumpEvents>=2 && airEvents==0);
 } else if(!strcmp(name,"wall-jump") || !strcmp(name,"wall-jump-held")) {
  floorOn=0;wallOn=1;state.origin[0]=80;state.origin[2]=200;
  state.groundEntityNum=ENTITYNUM_NONE;state.stats[STAT_GROUND_MS]=-9000;
  if(!strcmp(name,"wall-jump-held"))state.pm_flags|=PMF_JUMP_HELD;
  move.cmd.upmove=127;tick();
  if(!strcmp(name,"wall-jump-held"))CHECK(wallEvents==0 && airEvents==0);
  else {CHECK(wallEvents==1 && state.velocity[0]<-100 && state.velocity[2]>200);run(32);CHECK(wallEvents==1);}
 } else if(!strcmp(name,"wall-catch") || !strcmp(name,"wall-catch-kick") || !strcmp(name,"wall-catch-expire") || !strcmp(name,"wall-catch-release")) {
  floorOn=0;wallOn=1;state.origin[0]=84;state.origin[2]=200;
  state.groundEntityNum=ENTITYNUM_NONE;state.stats[STAT_GROUND_MS]=-9000;
  state.velocity[2]=-200;state.pm_flags|=PMF_JUMP_HELD;
  move.cmd.forwardmove=127;move.cmd.upmove=127;
  run(160);CHECK(fabsf(state.origin[2]-200)<.1 && wallEvents==0 && airEvents==0);
  if(!strcmp(name,"wall-catch-kick")) {
   move.cmd.upmove=0;tick();move.cmd.upmove=127;tick();
   CHECK(wallEvents==1 && airEvents==0 && state.velocity[0]<-100 && state.velocity[2]>200);
   run(32);CHECK(wallEvents==1);
  } else if(!strcmp(name,"wall-catch-expire")) {
   run(300);CHECK(state.origin[2]<198 && state.velocity[2]<-50 && wallEvents==0);
   move.cmd.forwardmove=0;tick();move.cmd.forwardmove=127;tick();
   CHECK(state.velocity[2]<-50);
  } else if(!strcmp(name,"wall-catch-release")) {
   move.cmd.forwardmove=0;run(80);CHECK(state.origin[2]<199 && state.velocity[2]<-40);
  }
 } else if(!strcmp(name,"wall-catch-approach")) {
  wallOn=1;state.origin[0]=60;state.velocity[0]=320;
  move.cmd.forwardmove=127;move.cmd.upmove=127;run(120);
  CHECK(jumpEvents==1 && wallEvents==0 && airEvents==0 && state.origin[2]>30);
  CHECK(fabsf(state.velocity[2])<.1 && state.origin[0]>75);
  move.cmd.upmove=0;tick();move.cmd.upmove=127;tick();
  CHECK(wallEvents==1 && state.velocity[0]<-100);
 } else if(!strcmp(name,"wall-catch-no-input")) {
  floorOn=0;wallOn=1;state.origin[0]=84;state.origin[2]=200;
  state.groundEntityNum=ENTITYNUM_NONE;state.velocity[2]=-200;
  run(80);CHECK(state.velocity[2]<-250 && state.origin[2]<184 && wallEvents==0);
 } else if(!strcmp(name,"wall-run-detach")) {
  floorOn=0;wallOn=1;state.origin[0]=80;state.origin[2]=200;
  state.groundEntityNum=ENTITYNUM_NONE;state.stats[STAT_GROUND_MS]=-9000;
  state.velocity[1]=400;move.cmd.angles[YAW]=ANGLE2SHORT(90);move.cmd.forwardmove=127;
  run(32);CHECK(BG_WallRunSide(state.stats[STAT_WALLRUN])!=0);
  move.cmd.forwardmove=0;tick();CHECK(BG_WallRunSide(state.stats[STAT_WALLRUN])==0);
 } else if(!strcmp(name,"wall-jump-limit")) {
  floorOn=0;wallOn=1;state.origin[0]=80;state.origin[2]=200;
  state.groundEntityNum=ENTITYNUM_NONE;state.stats[STAT_GROUND_MS]=-9000;
  state.stats[STAT_WALLJUMP_COUNT]=pm_wallJumpMax;state.stats[STAT_AIRJUMP_COUNT]=pm_airJumpMax;
  move.cmd.upmove=127;tick();CHECK(wallEvents==0 && airEvents==0);
 } else if(!strcmp(name,"step-low")) {
  obstacleHeight=16;move.cmd.forwardmove=127;run(600);
  CHECK(state.origin[0]>60 && state.origin[2]>=39.99f && state.origin[2]<=40.26f);
  CHECK(vaultEvents==0);
 } else if(!strcmp(name,"step-high")) {
  obstacleHeight=160;move.cmd.forwardmove=127;run(600);
  CHECK(state.origin[0]<=25.01f && state.origin[2]<=24.26f && vaultEvents==0);
 } else if(!strcmp(name,"vault") || !strcmp(name,"vault-ceiling")) {
  obstacleHeight=48;state.velocity[0]=400;move.cmd.forwardmove=127;
  if(!strcmp(name,"vault-ceiling")){ceilingOn=1;ceilingHeight=72;}
  run(400);
  if(ceilingOn)CHECK(vaultEvents==0 && state.origin[0]<=25.01f);
  else CHECK(vaultEvents==1 && state.origin[0]>25 && state.origin[2]>48);
 } else if(!strcmp(name,"coyote-air-spawn")) {
  floorOn=0;state.origin[2]=200;state.groundEntityNum=ENTITYNUM_NONE;
  move.cmd.upmove=127;tick();CHECK(jumpEvents==0 && airEvents==1);
 } else if(!strcmp(name,"coyote-rearm")) {
  tick();floorOn=0;tick();move.cmd.upmove=127;tick();CHECK(jumpEvents==1);
  // A genuine new landing must restore the next ledge's grace period.
  floorOn=1;state.origin[2]=24;VectorClear(state.velocity);move.cmd.upmove=0;run(400);
  floorOn=0;tick();move.cmd.upmove=127;tick();CHECK(jumpEvents==2 && airEvents==0);
 } else if(!strcmp(name,"ledge-walkoff")) {
  int i;ledgeOn=1;state.origin[0]=-20;move.cmd.forwardmove=127;
  for(i=0;i<(int)(1000/dt);i++){tick();if(state.groundEntityNum==ENTITYNUM_NONE)break;}
  CHECK(state.origin[0]>=15 && state.groundEntityNum==ENTITYNUM_NONE);
  move.cmd.upmove=127;tick();CHECK(jumpEvents==1 && airEvents==0);
  run(120);CHECK(jumpEvents==1);
 } else if(!strcmp(name,"ledge-idle")) {
  ledgeOn=1;state.origin[0]=14.9f;run(1000);
  CHECK(jumpEvents==0 && vaultEvents==0 && airEvents==0);
  CHECK(fabsf(state.velocity[2])<.01 && state.origin[2]<=24.251f);
 } else if(!strcmp(name,"vault-ceiling-path")) {
  obstacleHeight=48;ceilingOn=canopyOn=1;ceilingHeight=56.01f;
  state.velocity[0]=400;move.cmd.forwardmove=127;run(64);
  if(vaultEvents)printf("Unexpected automatic vault attempts: %d in 64 world ms, jump input=%d\n",vaultEvents,move.cmd.upmove);
  CHECK(jumpEvents==0 && airEvents==0 && vaultEvents==0);
 } else if(!strcmp(name,"water-jump") || !strcmp(name,"water-jump-blocked") || !strcmp(name,"water-jump-fall")) {
  floorOn=0;waterSurface=30;obstacleHeight=32;state.origin[0]=15;state.groundEntityNum=ENTITYNUM_NONE;state.viewheight=DEFAULT_VIEWHEIGHT;
  if(!strcmp(name,"water-jump-blocked"))obstacleHeight=60;
  tick();
  if(!strcmp(name,"water-jump-blocked")) {
   CHECK(!(state.pm_flags&PMF_TIME_WATERJUMP) && state.pm_time==0 && state.velocity[2]<=0);
  } else {
   CHECK((state.pm_flags&PMF_TIME_WATERJUMP) && state.pm_time>0 && state.velocity[2]>300);
   CHECK(jumpEvents==0 && airEvents==0 && state.stats[STAT_AIRJUMP_COUNT]==0);
   if(!strcmp(name,"water-jump-fall")) {
    state.velocity[2]=-10;tick();
    CHECK(!(state.pm_flags&PMF_TIME_WATERJUMP) && state.pm_time==0);
   }
  }
 } else if(!strcmp(name,"water-levels")) {
  waterSurface=2;tick();CHECK(move.waterlevel==1);
  waterSurface=30;tick();CHECK(move.waterlevel==2);
  waterSurface=100;tick();CHECK(move.waterlevel==3);
  waterSurface=-10000;tick();CHECK(move.waterlevel==0);
 } else if(!strcmp(name,"water-swim")) {
  floorOn=0;waterSurface=1000;state.origin[2]=200;state.groundEntityNum=ENTITYNUM_NONE;
  move.cmd.upmove=127;run(500);CHECK(state.origin[2]>210 && state.velocity[2]>0);
  CHECK(jumpEvents==0 && airEvents==0 && state.velocity[2]<=160.01f);
 } else if(!strcmp(name,"water-sink")) {
  floorOn=0;waterSurface=1000;state.origin[2]=200;state.groundEntityNum=ENTITYNUM_NONE;
  run(500);CHECK(state.origin[2]<200 && state.velocity[2]<0 && state.velocity[2]>=-60.01f);
 } else if(!strcmp(name,"water-exit")) {
  floorOn=0;waterSurface=1000;state.origin[2]=200;state.groundEntityNum=ENTITYNUM_NONE;
  move.cmd.upmove=127;run(32);move.cmd.upmove=0;waterSurface=-10000;tick();
  CHECK(move.waterlevel==0);move.cmd.upmove=127;tick();CHECK(airEvents==1);
 } else if(!strcmp(name,"wall-run-expiry") || !strcmp(name,"wall-run-reentry")) {
  floorOn=0;wallOn=1;state.origin[0]=80;state.origin[2]=10000;
  state.groundEntityNum=ENTITYNUM_NONE;state.stats[STAT_GROUND_MS]=-9000;
  state.velocity[1]=400;move.cmd.angles[YAW]=ANGLE2SHORT(90);move.cmd.forwardmove=127;
  pm_wallRunDurationMs=80;run(64);
  if(!strcmp(name,"wall-run-reentry")){move.cmd.forwardmove=0;tick();move.cmd.forwardmove=127;}
  run(100);CHECK(BG_WallRunSide(state.stats[STAT_WALLRUN])==0);
  CHECK(BG_WallRunElapsed(state.stats[STAT_WALLRUN])>=80);
  floorOn=1;wallOn=0;state.origin[2]=24;VectorClear(state.velocity);move.cmd.forwardmove=0;tick();
  CHECK(state.stats[STAT_WALLRUN]==0);
 } else if(!strcmp(name,"wall-run-state")) {
  entityState_t entity;int side,n,values[]={0,1,80,1600,16382};
  for(n=0;n<5;n++)for(side=-1;side<=1;side++) {
   int encoded=BG_WallRunEncode(values[n],side);
   CHECK(encoded>=-32768 && encoded<=32767);
   CHECK(BG_WallRunElapsed(encoded)==values[n] && BG_WallRunSide(encoded)==side);
   state.stats[STAT_WALLRUN]=encoded;
   BG_PlayerStateToEntityState(&state,&entity,qfalse);
   CHECK((entity.frame&3)==(side>0?1:side<0?2:0));
   BG_PlayerStateToEntityStateExtraPolate(&state,&entity,0,qfalse);
   CHECK((entity.frame&3)==(side>0?1:side<0?2:0));
  }
 } else if(!strcmp(name,"terminal-speed")) {
  floorOn=0;state.origin[2]=10000;state.groundEntityNum=ENTITYNUM_NONE;run(3000);
  CHECK(state.velocity[2]>=-1200.01 && state.velocity[2]<-1190);
 } else CHECK(0);
 return failures?1:0;
}
