/* Production sword functions, deterministic segment/AABB collision fixtures.
   Damage stub records dispatch: armor/parry resolution is outside this harness. */
#include "../../engine/code/game/g_local.h"
#include "../../engine/code/game/bg_local.h"
level_locals_t level;
gentity_t g_entities[MAX_GENTITIES];
static gclient_t clients[4];
static gentity_t effect;
static float s_quadFactor=1;
static vec3_t muzzle;
vmCvar_t g_swordAimSnap,g_swordMinRange,g_swordCounterHit,g_swordChainRedirect;
vmCvar_t g_swordKnockback,g_swordJuggle,g_swordWhiffScale,g_debugDamage;
static int failures,hits,events,damageTotal;
static float wallX;
#define CHECK(c) do { if (!(c)) {printf("FAIL line %d: %s\n",__LINE__,#c); failures++;} } while(0)
void trap_SnapVector(float *v) {int i;for(i=0;i<3;i++)v[i]=(float)(int)v[i];}
static int contents(const vec3_t p,int pass) {return 0;}
void QDECL Com_Printf(const char *fmt,...) {}
void QDECL Com_Error(int level,const char *fmt,...) {abort();}
void QDECL G_Printf(const char *fmt,...) {}
gentity_t *G_TempEntity(vec3_t origin,int event) {return &effect;}
void G_AddEvent(gentity_t *e,int event,int parm) {events++;}
qboolean OnSameTeam(gentity_t *a,gentity_t *b) {
 return a->client && b->client && a->client->sess.sessionTeam!=TEAM_FREE
  && a->client->sess.sessionTeam==b->client->sess.sessionTeam;
}
void G_Damage(gentity_t *t,gentity_t *inf,gentity_t *att,vec3_t dir,vec3_t point,int damage,int flags,int mod) {
 hits++;damageTotal+=damage;t->health-=damage;
}
/* Slab intersection with client bounds; walls take priority at nearest impact. */
void trap_Trace(trace_t *tr,const vec3_t start,const vec3_t mins,const vec3_t maxs,
 const vec3_t end,int pass,int mask) {
 int n,j; float enter,leave,d,lo,hi,a,b,tmp;
 memset(tr,0,sizeof(*tr));tr->fraction=1;tr->entityNum=ENTITYNUM_NONE;
 if(wallX>0 && start[0]<wallX && end[0]>=wallX) {
  tr->fraction=(wallX-start[0])/(end[0]-start[0]);tr->entityNum=ENTITYNUM_WORLD;
 }
 for(n=0;n<level.num_entities;n++) {
  gentity_t *e=&g_entities[n];if(n==pass || !e->inuse || !(e->r.contents & mask)) continue;
  enter=0;leave=1;
  for(j=0;j<3;j++) {
   lo=e->r.currentOrigin[j]+e->r.mins[j]-(maxs?maxs[j]:0);
   hi=e->r.currentOrigin[j]+e->r.maxs[j]-(mins?mins[j]:0);
   d=end[j]-start[j];
   if(fabs(d)<1e-8) {if(start[j]<lo || start[j]>hi) {enter=2;break;}continue;}
   a=(lo-start[j])/d;b=(hi-start[j])/d;if(a>b){tmp=a;a=b;b=tmp;}
   if(a>enter)enter=a;if(b<leave)leave=b;
  }
  if(enter<=leave && enter>=0 && enter<tr->fraction) {tr->fraction=enter;tr->entityNum=n;}
 }
 for(j=0;j<3;j++)tr->endpos[j]=start[j]+tr->fraction*(end[j]-start[j]);
 tr->plane.normal[0]=-1;
}
#include "sword_under_test.h"
static void setup(float x,float y) {
 int n;
 memset(g_entities,0,sizeof(g_entities));memset(clients,0,sizeof(clients));
 level.time=2000;level.num_entities=2;hits=events=damageTotal=0;wallX=0;
 for(n=0;n<2;n++) {
  gentity_t *e=&g_entities[n];e->s.number=n;e->client=&clients[n];
  e->inuse=e->takedamage=qtrue;e->health=1000;e->r.contents=CONTENTS_BODY;
  VectorSet(e->r.mins,-15,-15,-24);VectorSet(e->r.maxs,15,15,32);
  e->client->ps.viewheight=26;e->client->ps.clientNum=n;
 }
 VectorSet(g_entities[1].r.currentOrigin,x,y,0);
 VectorSet(muzzle,14,0,26); /* FireWeapon's historical gun muzzle */
 clients[0].swordSwingParm=SWORD_PACK_QUADS(SQ_L,SQ_R);
}
int main(int argc,char **argv) {
 const char *name=argc>1?argv[1]:"";
 vec3_t from={0,0,26},axis={1,0,0},dir;float left,right;
 pmove_t move;
 setup(70,0);memset(&move,0,sizeof(move));pm=&move;pm->ps=&clients[0].ps;
 pm->trace=trap_Trace;VectorSet(pml.forward,1,0,0);
 if(!strcmp(name,"reach-still")) {
  setup(94,0);Weapon_Sword(&g_entities[0]);CHECK(hits==1);
  setup(96,0);Weapon_Sword(&g_entities[0]);CHECK(hits==0);
 } else if(!strcmp(name,"reach-fast") || !strcmp(name,"bot-reach")) {
  setup(174,0);clients[0].ps.velocity[0]=960;
  if(!strcmp(name,"bot-reach"))g_entities[0].r.svFlags=SVF_BOT;
  Weapon_Sword(&g_entities[0]);CHECK(hits==1);
  setup(180,0);clients[0].ps.velocity[0]=960;
  if(!strcmp(name,"bot-reach"))g_entities[0].r.svFlags=SVF_BOT;
  Weapon_Sword(&g_entities[0]);CHECK(hits==0);
 } else if(!strcmp(name,"wall-hit")) {
  wallX=35;Weapon_Sword(&g_entities[0]);CHECK(hits==0);
 } else if(!strcmp(name,"wall-near")) {
  wallX=8;Weapon_Sword(&g_entities[0]);CHECK(hits==0);
 } else if(!strcmp(name,"deduplicate")) {
  Weapon_Sword(&g_entities[0]);CHECK(hits==1 && damageTotal==45);
 } else if(!strcmp(name,"corpse")) {
  g_entities[1].health=0;clients[0].ps.velocity[0]=200;
  Weapon_Sword(&g_entities[0]);CHECK(hits==0 && events==0);CHECK(clients[0].ps.velocity[0]==200);
 } else if(!strcmp(name,"target-wall")) {
  wallX=35;CHECK(G_SwordFindTarget(&g_entities[0],from,axis,300,-1,qfalse)==NULL);
 } else if(!strcmp(name,"target-team")) {
  clients[0].sess.sessionTeam=clients[1].sess.sessionTeam=TEAM_RED;
  CHECK(G_SwordFindTarget(&g_entities[0],from,axis,300,-1,qfalse)==NULL);
 } else if(!strcmp(name,"target-dead")) {
  g_entities[1].health=0;CHECK(G_SwordFindTarget(&g_entities[0],from,axis,300,-1,qfalse)==NULL);
 } else if(!strcmp(name,"lunge-mirror")) {
  setup(200,50);left=PM_SwordMagnetLunge(dir);CHECK(left>120);
  setup(200,-50);right=PM_SwordMagnetLunge(dir);CHECK(right>120);CHECK(fabs(left-right)<.01);
 } else if(!strcmp(name,"lunge-wall")) {
  setup(200,0);wallX=50;CHECK(PM_SwordMagnetLunge(dir)==120);
 } else if(!strcmp(name,"lunge-bot")) {
  setup(200,0);pm->botBlade=qtrue;CHECK(PM_SwordMagnetLunge(dir)==120);
 } else if(!strcmp(name,"lunge-close")) {
  setup(40,0);CHECK(PM_SwordMagnetLunge(dir)==120);
 } else if(!strcmp(name,"corpse-shield") || !strcmp(name,"chain-corpse")) {
  setup(50,0);g_entities[1].health=0;g_entities[1].r.contents=CONTENTS_CORPSE;
  g_entities[2]=g_entities[1];g_entities[2].client=&clients[2];g_entities[2].s.number=2;
  g_entities[2].health=100;g_entities[2].r.contents=CONTENTS_BODY;
  VectorSet(g_entities[2].r.currentOrigin,80,0,0);level.num_entities=3;
  if(!strcmp(name,"corpse-shield")) {Weapon_Sword(&g_entities[0]);CHECK(hits==1 && g_entities[2].health<100);}
  else CHECK(G_SwordFindTarget(&g_entities[0],from,axis,300,-1,qfalse)==&g_entities[2]);
 } else if(!strcmp(name,"lunge-far")) {
  setup(330,0);CHECK(PM_SwordMagnetLunge(dir)==120);
 } else if(!strcmp(name,"target-cone")) {
  setup(70,70);CHECK(G_SwordFindTarget(&g_entities[0],from,axis,300,.95f,qtrue)==NULL);
 } else if(!strcmp(name,"lunge-corpse")) {
  setup(200,0);g_entities[1].r.contents=CONTENTS_CORPSE;
  CHECK(PM_SwordMagnetLunge(dir)==120);
 } else if(!strcmp(name,"min-range")) {
  g_swordMinRange.value=28;
  setup(40,0);Weapon_Sword(&g_entities[0]);CHECK(hits==0);
  setup(44,0);Weapon_Sword(&g_entities[0]);CHECK(hits==1);
  setup(40,0);clients[0].ps.velocity[0]=600;Weapon_Sword(&g_entities[0]);CHECK(hits==1);
 } else if(!strcmp(name,"chain-wall") || !strcmp(name,"chain-visible")) {
  g_swordChainRedirect.integer=1;
  g_entities[1].health=1;clients[0].ps.velocity[0]=200;
  g_entities[2]=g_entities[1];g_entities[2].s.number=2;g_entities[2].client=&clients[2];
  g_entities[2].health=100;VectorSet(g_entities[2].r.currentOrigin,200,100,0);level.num_entities=3;
  if(!strcmp(name,"chain-wall"))wallX=100;
  Weapon_Sword(&g_entities[0]);CHECK(hits==1 && g_entities[1].health<=0);
  CHECK(clients[0].ps.velocity[0]>200);
  if(wallX)CHECK(clients[0].ps.velocity[1]==0);else CHECK(clients[0].ps.velocity[1]>0);
 } else if(!strncmp(name,"swing-",6) || !strncmp(name,"buffer-",7)) {
  int i,j,previousEvents,fireCount=0,steps;
  float world=0,dt=strstr(name,"slow")?.32f:8.0f;
  qboolean buffer=!strncmp(name,"buffer-",7);
  level.num_entities=1;
  pm->pointcontents=contents;pm->fractionalStep=qtrue;pm->worldMsec=dt;
  pm->ps->weapon=pm->cmd.weapon=WP_SWORD;pm->ps->weaponstate=WEAPON_READY;
  pm->ps->stats[STAT_HEALTH]=100;pm->ps->stats[STAT_WEAPONS]=1<<WP_SWORD;
  pm->ps->ammo[WP_SWORD]=-1;pm->ps->speed=320;pm->ps->groundEntityNum=ENTITYNUM_NONE;
  steps=buffer?(int)(320/dt):(int)(40/dt);
  for(i=0;i<steps;i++) {
   previousEvents=pm->ps->eventSequence;
   pm->cmd.buttons=(i==0 || (buffer && i==2))?BUTTON_ATTACK:0;
   pm->wholeMsec=(int)(world+dt)-(int)world;world+=dt;pm->cmd.serverTime=(int)world;
   Pmove(pm);
   for(j=previousEvents;j<pm->ps->eventSequence;j++) if(pm->ps->events[j&(MAX_PS_EVENTS-1)]==EV_FIRE_WEAPON)fireCount++;
  }
  CHECK(fireCount==(buffer?2:1));
  CHECK(fabs(pm->ps->velocity[0]-(buffer?240:120))<.01);
 } else if(!strcmp(name,"combo")) {
  Weapon_Sword(&g_entities[0]);CHECK(damageTotal==45);
  level.time+=300;Weapon_Sword(&g_entities[0]);CHECK(damageTotal==45+51);
  level.time+=300;Weapon_Sword(&g_entities[0]);CHECK(damageTotal==45+51+69);
  level.time+=SWORD_COMBO_WINDOW+1;Weapon_Sword(&g_entities[0]);CHECK(damageTotal==45+51+69+45);
 } else CHECK(0);
 return failures?1:0;
}
