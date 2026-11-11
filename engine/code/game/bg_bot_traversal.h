/* Shared, deterministic traversal controller. Only produces ordinary user inputs.
 * Positions are player origins, clocks are world milliseconds. */
#ifndef BG_BOT_TRAVERSAL_H
#define BG_BOT_TRAVERSAL_H
typedef struct {
 vec3_t entry, contact, exit;
 float speed;
 int wallkick, airjump;
} botRoute_t;
typedef struct {
 int phase, started, launched, action, initialWallCount, initialAirCount, jumpSequence;
} botRouteRun_t;
enum { BOT_ROUTE_RUNNING, BOT_ROUTE_SUCCESS, BOT_ROUTE_FAILED };

static float BotRouteDistance(const vec3_t a, const vec3_t b) {
 vec3_t d; VectorSubtract(a,b,d); return VectorLength(d);
}

static void BotRouteSteer(const botRoute_t *r, const botRouteRun_t *s,
 const playerState_t *ps, const vec3_t target, int jump, usercmd_t *cmd, vec3_t angles) {
 vec3_t dir; float speed; int i;
 VectorSubtract(target,ps->origin,dir);dir[2]=0;
 if(s->phase!=4 && (!r->wallkick || s->phase==3 || !s->phase)) {
  dir[0]-=ps->velocity[0]*0.18f;dir[1]-=ps->velocity[1]*0.18f;
 }
 speed=VectorLength(dir);
 vectoangles(dir,angles);angles[PITCH]=0;angles[ROLL]=0;
 for(i=0;i<3;i++) cmd->angles[i]=ANGLE2SHORT(angles[i])-ps->delta_angles[i];
 cmd->forwardmove=(signed char)(speed<32?speed*3:127);
 cmd->rightmove=0;cmd->upmove=jump;
 cmd->buttons&=~(BUTTON_ATTACK|BUTTON_BLOCK|BUTTON_DASH|BUTTON_WALKING);
}

static int BotRouteStep(const botRoute_t *r, botRouteRun_t *s,
 const playerState_t *ps, int now, qboolean wall, vec3_t target, int *jump) {
 *jump=0;
 if (ps->pm_type!=PM_NORMAL || ps->stats[STAT_HEALTH]<=0 ||
     now<s->started || now-s->started>4500) return BOT_ROUTE_FAILED;
 VectorCopy(r->entry,target);
 if (!s->phase) {
  if (BotRouteDistance(ps->origin,r->entry)>36) return BOT_ROUTE_RUNNING;
  if (ps->groundEntityNum==ENTITYNUM_NONE) return BOT_ROUTE_RUNNING;
  if (ps->pm_flags & PMF_JUMP_HELD) return BOT_ROUTE_RUNNING;
  s->phase=4;
 }
 if(s->phase==4) {
  vec3_t approach;
  VectorCopy(r->wallkick?r->contact:r->exit,target);
  if(ps->groundEntityNum==ENTITYNUM_NONE) return BOT_ROUTE_RUNNING;
  VectorSubtract(target,ps->origin,approach);approach[2]=0;VectorNormalize(approach);
  if(DotProduct(ps->velocity,approach)<(r->speed>0?r->speed:180))
   return BOT_ROUTE_RUNNING;
  s->phase=1; s->launched=now; s->initialWallCount=ps->stats[STAT_WALLJUMP_COUNT];
  s->initialAirCount=ps->stats[STAT_AIRJUMP_COUNT];
  s->jumpSequence=ps->eventSequence;
  *jump=127;
  VectorCopy(r->wallkick?r->contact:r->exit,target);
  return BOT_ROUTE_RUNNING;
 }
 VectorCopy(r->wallkick && s->phase<3?r->contact:r->exit,target);
 if (s->phase==1) {
  int i, jumped=0;
  for(i=s->jumpSequence;i<ps->eventSequence;i++) {
   int event=ps->events[i&(MAX_PS_EVENTS-1)];
   if(event==EV_JUMP || event==EV_VAULT) jumped=1;
  }
  if (!jumped || ps->groundEntityNum!=ENTITYNUM_NONE) {
   if (now-s->launched>250) return BOT_ROUTE_FAILED;
   *jump=ps->groundEntityNum!=ENTITYNUM_NONE?127:0; return BOT_ROUTE_RUNNING;
  }
  s->phase=2;
  if (!r->wallkick && !r->airjump) s->action=1;
 }
 if (r->wallkick && ps->stats[STAT_WALLJUMP_COUNT]>s->initialWallCount) {
  s->action=1; s->phase=3; VectorCopy(r->exit,target);
 }
 if(r->airjump && ps->stats[STAT_AIRJUMP_COUNT]>s->initialAirCount) {
  s->action=1;s->phase=3;
 }
 if (ps->groundEntityNum!=ENTITYNUM_NONE) {
  if (s->action && BotRouteDistance(ps->origin,r->exit)<64 && fabs(ps->origin[2]-r->exit[2])<8)
   return BOT_ROUTE_SUCCESS;
  return BOT_ROUTE_FAILED;
 }
 if (r->wallkick && s->phase==2 && wall &&
     ps->origin[2]>r->entry[2]+16 && !(ps->pm_flags&PMF_JUMP_HELD)) {
  *jump=127;
 }
 if(r->airjump && s->phase==2 && ps->velocity[2]<=240 && !(ps->pm_flags&PMF_JUMP_HELD))
  *jump=127;
 return BOT_ROUTE_RUNNING;
}
#endif
