/* Included by ai_main.c: authored movement edges supplement ordinary AAS routing.
 * Normal execution never teleports, writes velocity, or accepts proximity alone. */
#include "bg_bot_traversal.h"
#define BOT_ROUTE_MAX 64
static vmCvar_t bot_traversal, bot_traversalDebug, bot_traversalReport;
static struct { botRoute_t route; gentity_t *ent; int attempts, success, failed; } botRoutes[BOT_ROUTE_MAX];
static struct { int route, cooldown[BOT_ROUTE_MAX]; botRouteRun_t run; } botRouteClients[MAX_CLIENTS];
static int botRouteCount;
static int botRouteLogTime[MAX_CLIENTS];

static void BotRoutesLoad(void) {
 int i,j;
 memset(botRoutes,0,sizeof(botRoutes));
 memset(botRouteClients,0,sizeof(botRouteClients));
 for (i=0;i<MAX_CLIENTS;i++) botRouteClients[i].route=-1;
 botRouteCount=0;
 for(i=MAX_CLIENTS;i<level.num_entities && botRouteCount<BOT_ROUTE_MAX;i++) {
  gentity_t *e=&g_entities[i];
  if(!e->inuse || !e->classname || strcmp(e->classname,"info_bot_route")) continue;
  j=botRouteCount++;
  botRoutes[j].ent=e;
  VectorCopy(e->s.pos.trBase,botRoutes[j].route.entry);
  VectorCopy(e->s.origin2,botRoutes[j].route.contact);
  VectorCopy(e->s.angles2,botRoutes[j].route.exit);
  botRoutes[j].route.wallkick=e->spawnflags&1;
  botRoutes[j].route.airjump=e->spawnflags&2;
  botRoutes[j].route.speed=e->speed>0?e->speed:180;
  e->s.modelindex=j+1;
 }
 G_Printf("TRAVERSAL: loaded %d authored routes\n",botRouteCount);
}

static void BotRouteFinish(int client, int result) {
 int n=botRouteClients[client].route;
 if(n<0 || n>=botRouteCount) return;
 if(result==BOT_ROUTE_SUCCESS) botRoutes[n].success++;
 else botRoutes[n].failed++;
 botRouteClients[client].cooldown[n]=level.time+(result==BOT_ROUTE_SUCCESS?4000:12000);
 botRouteClients[client].route=-1;
 G_Printf("TRAVERSAL: bot %d route %d %s (%d/%d completed) phase=%d xyz=%.0f %.0f %.0f\n",client,n+1,
  result==BOT_ROUTE_SUCCESS?"completed":"failed",botRoutes[n].success,botRoutes[n].attempts,botRouteClients[client].run.phase,
  (g_entities[client].client?g_entities[client].client->ps.origin[0]:0),(g_entities[client].client?g_entities[client].client->ps.origin[1]:0),(g_entities[client].client?g_entities[client].client->ps.origin[2]:0));
}

/* Explicit cheat-only fixture setup, never called by normal route selection. */
void BotRouteTest(int client, int route) {
 vec3_t start,dir,angles;
 if(!g_cheats.integer || client<0 || client>=level.maxclients ||
    route<1 || route>botRouteCount || !botstates[client] || !botstates[client]->inuse ||
    !g_entities[client].inuse || g_entities[client].health<=0) return;
 BotRouteFinish(client,BOT_ROUTE_FAILED);
 route--;
 VectorCopy(botRoutes[route].route.entry,start);start[2]+=1;
 VectorSubtract(botRoutes[route].route.contact,start,dir);vectoangles(dir,angles);angles[0]=0;
 TeleportPlayer(&g_entities[client],start,angles);
 VectorClear(g_entities[client].client->ps.velocity);
 g_entities[client].client->ps.pm_time=0;
 g_entities[client].client->ps.groundEntityNum=ENTITYNUM_NONE;
 g_entities[client].client->ps.stats[STAT_JUMP_MS]=0;
 g_entities[client].client->ps.stats[STAT_BHOP_STREAK]=0;
 g_entities[client].client->ps.stats[STAT_WALLJUMP_COUNT]=0;
 g_entities[client].client->ps.stats[STAT_AIRJUMP_COUNT]=0;
 g_entities[client].client->ps.stats[STAT_WALLRUN]=0;
 g_entities[client].client->ps.pm_flags&=~(PMF_TIME_KNOCKBACK|PMF_JUMP_HELD);
 memset(&botRouteClients[client].run,0,sizeof(botRouteRun_t));
 botRouteClients[client].run.started=level.time;
 botRouteClients[client].route=route;
 botRoutes[route].attempts++;
 G_Printf("TRAVERSAL TEST: placed bot %d at route %d entry; execution uses player physics\n",client,route+1);
}

static void BotRoutesFrame(void) {
 int i,c,active;
 trap_Cvar_Update(&bot_traversal);
 trap_Cvar_Update(&bot_traversalDebug);
 trap_Cvar_Update(&bot_traversalReport);
 for(c=0;c<MAX_CLIENTS;c++) {
  if(botRouteClients[c].route>=0 && (!bot_traversal.integer || !bot_moveset.integer ||
      !g_entities[c].inuse || !g_entities[c].client ||
      g_entities[c].client->pers.connected!=CON_CONNECTED ||
      g_entities[c].client->ps.pm_type!=PM_NORMAL || g_entities[c].health<=0))
   BotRouteFinish(c,BOT_ROUTE_FAILED);
 }
 for(i=0;i<botRouteCount;i++) {
  gentity_t *e=botRoutes[i].ent;
  active=0;
  for(c=0;c<MAX_CLIENTS;c++) if(botRouteClients[c].route==i) active++;
  e->s.frame=active; e->s.time=botRoutes[i].attempts;
  e->s.time2=botRoutes[i].success; e->s.apos.trDuration=botRoutes[i].failed;
  if(bot_traversalDebug.integer) {
   e->r.svFlags=SVF_BROADCAST; trap_LinkEntity(e);
  } else { e->r.svFlags=SVF_NOCLIENT; trap_UnlinkEntity(e); }
  if(bot_traversalReport.integer)
   G_Printf("ROUTE %d %s: attempts=%d completed=%d failed=%d active=%d\n",
    i+1,e->message?e->message:"",e->s.time,e->s.time2,e->s.apos.trDuration,active);
 }
 if(bot_traversalReport.integer) trap_Cvar_Set("bot_traversalReport","0");
}

static qboolean BotApplyTraversal(bot_state_t *bs) {
 int c=bs->client,n=botRouteClients[c].route,i,result,jump;
 playerState_t *ps=&g_entities[c].client->ps; /* authoritative, not 10 Hz AI cache */
 usercmd_t *cmd=&bs->lastucmd;
 vec3_t target,dir,angles,probe,mins={-15,-15,-24},maxs={15,15,32};
 trace_t tr;
 qboolean wall=qfalse;
 float best=280,dist;
 if(!bot_traversal.integer || !bot_moveset.integer || ps->pm_type!=PM_NORMAL ||
    ps->stats[STAT_HEALTH]<=0 || ps->powerups[PW_FLIGHT] || ps->pm_flags&PMF_GRAPPLE_PULL) return qfalse;
 if(n<0) {
  bot_goal_t goal;
  qboolean hasGoal=trap_BotGetTopGoal(bs->gs,&goal);
  if(bs->enemy>=0) {
   aas_entityinfo_t enemy;
   BotEntityInfo(bs->enemy,&enemy);
   if(enemy.valid) {VectorCopy(enemy.origin,goal.origin);hasGoal=qtrue;}
  }
  for(i=0;i<botRouteCount;i++) {
   botRoute_t *r=&botRoutes[i].route;
   if(level.time<botRouteClients[c].cooldown[i]) continue;
   dist=BotRouteDistance(ps->origin,r->entry);
   if(dist>=best || fabs(ps->origin[2]-r->entry[2])>96) continue;
   /* Mode 2 is a local practice override. Normal play requires goal progress. */
   if(bot_traversal.integer<2 && (!hasGoal ||
      BotRouteDistance(r->exit,goal.origin)+40>=BotRouteDistance(r->entry,goal.origin))) continue;
   VectorCopy(r->entry,probe);probe[2]+=1; /* avoid tracing into the floor contact epsilon */
   trap_Trace(&tr,ps->origin,mins,maxs,probe,c,MASK_PLAYERSOLID);
   if(tr.startsolid || tr.fraction<1) continue;
   best=dist;n=i;
  }
  if(n<0) return qfalse;
  botRouteClients[c].route=n;
  memset(&botRouteClients[c].run,0,sizeof(botRouteRun_t));
  botRouteClients[c].run.started=level.time;
  botRoutes[n].attempts++;
 }
 VectorSubtract(botRoutes[n].route.contact,ps->origin,dir);
 dir[2]=0; VectorNormalize(dir);
 VectorMA(ps->origin,26,dir,probe);
 trap_Trace(&tr,ps->origin,mins,maxs,probe,c,MASK_PLAYERSOLID);
 wall=tr.fraction<1 && !tr.startsolid && tr.entityNum==ENTITYNUM_WORLD &&
  fabs(tr.plane.normal[2])<0.2f && !(tr.surfaceFlags&SURF_NOIMPACT);
 result=BotRouteStep(&botRoutes[n].route,&botRouteClients[c].run,ps,level.time,wall,target,&jump);
 if(result!=BOT_ROUTE_RUNNING) { BotRouteFinish(c,result);return qfalse; }
 if(bot_traversalDebug.integer>=2 && level.time>=botRouteLogTime[c]) {
  botRouteLogTime[c]=level.time+100;
  G_Printf("ROUTE STEP %d phase=%d xyz=%.1f %.1f %.1f vel=%.0f %.0f %.0f ground=%d jump=%d flags=%d\n",
   n+1,botRouteClients[c].run.phase,ps->origin[0],ps->origin[1],ps->origin[2],
   ps->velocity[0],ps->velocity[1],ps->velocity[2],ps->groundEntityNum,jump,ps->pm_flags);
 }
 BotRouteSteer(&botRoutes[n].route,&botRouteClients[c].run,ps,target,jump,cmd,angles);
 VectorCopy(angles,bs->viewangles);
 return qtrue;
}
