#include "../../engine/code/game/g_local.h"
gentity_t g_entities[MAX_GENTITIES];
level_locals_t level;
static int transaction, rejectSecond;
static vec3_t linkedPositions[MAX_GENTITIES];
static int failures,blocked,linked;
#define CHECK(c) do{if(!(c)){printf("FAIL %d: %s\n",__LINE__,#c);failures++;}}while(0)
void QDECL Com_Printf(const char *fmt,...){}
void QDECL Com_Error(int code,const char *fmt,...){abort();}
void QDECL G_Error(const char *fmt,...){abort();}
void trap_LinkEntity(gentity_t *e){linked++;VectorCopy(e->r.currentOrigin,linkedPositions[e->s.number]);}
void trap_UnlinkEntity(gentity_t *e){}
int trap_EntitiesInBox(const vec3_t mins,const vec3_t maxs,int *list,int maxcount){if(!transaction)return 0;list[0]=0;list[1]=2;return 2;}
void G_Damage(gentity_t *a,gentity_t *b,gentity_t *c,vec3_t dir,vec3_t point,int damage,int flags,int mod){}

void trap_Trace(trace_t *t,const vec3_t start,const vec3_t mins,const vec3_t maxs,const vec3_t end,int pass,int mask){
 memset(t,0,sizeof(*t));t->fraction=1;t->entityNum=ENTITYNUM_NONE;
 if((blocked && start[1]>5) || (rejectSecond && pass==2)){t->startsolid=t->allsolid=qtrue;t->fraction=0;t->entityNum=ENTITYNUM_WORLD;}
 VectorCopy(end,t->endpos);
}
#include "mover_under_test.h"
int main(int argc,char **argv){
 const char *name=argc>1?argv[1]:"";gclient_t cl={0};gentity_t *r=&g_entities[0],*platform=&g_entities[1];
 vec3_t move={0,0,0},angles={0,0,0};qboolean result;
 r->client=&cl;r->s.number=0;platform->s.number=1;r->s.groundEntityNum=cl.ps.groundEntityNum=1;
 cl.ps.origin[0]=r->s.pos.trBase[0]=r->r.currentOrigin[0]=10;pushed_p=pushed;
 if(!strncmp(name,"multi-",6)) {
  gclient_t cl2={0};gentity_t *r2=&g_entities[2];transaction=1;rejectSecond=!strcmp(name,"multi-rollback");
  r->s.eType=ET_PLAYER;r2->s.eType=ET_PLAYER;r2->s.number=2;r2->client=&cl2;r2->s.groundEntityNum=1;
  cl2.ps.origin[0]=r2->s.pos.trBase[0]=r2->r.currentOrigin[0]=20;
  level.time=100;level.previousTime=0;
  platform->s.pos.trType=TR_LINEAR;platform->s.pos.trDelta[0]=30;
  G_MoverTeam(platform);
  if(rejectSecond) {
   CHECK(cl.ps.origin[0]==10 && cl2.ps.origin[0]==20 && platform->r.currentOrigin[0]==0);
   CHECK(r->r.currentOrigin[0]==10 && r2->r.currentOrigin[0]==20);
   CHECK(linkedPositions[0][0]==10 && linkedPositions[2][0]==20);
  } else CHECK(cl.ps.origin[0]==13 && cl2.ps.origin[0]==23 && platform->r.currentOrigin[0]==3);
  return failures?1:0;
 }
 if(!strcmp(name,"translation"))VectorSet(move,3,4,5);
 else if(!strcmp(name,"rotation") || !strcmp(name,"blocked-rotation")){angles[YAW]=90;blocked=!strcmp(name,"blocked-rotation");}
 else {platform->s.eFlags=EF_MOVER_STOP;move[0]=3;if(!strcmp(name,"stop-nonrider"))r->s.groundEntityNum=ENTITYNUM_NONE;}
 result=G_TryPushingEntity(r,platform,move,angles);
 if(!strcmp(name,"translation")){CHECK(result && fabs(cl.ps.origin[0]-13)<.001 && fabs(cl.ps.origin[1]-4)<.001 && fabs(cl.ps.origin[2]-5)<.001);CHECK(linked==1);}
 else if(!strcmp(name,"rotation")){CHECK(result && fabs(cl.ps.origin[0])<.001 && fabs(cl.ps.origin[1]-10)<.001);CHECK(cl.ps.delta_angles[YAW]==ANGLE2SHORT(90));}
 else if(!strcmp(name,"blocked-rotation")){CHECK(result && fabs(cl.ps.origin[0]-10)<.001 && fabs(cl.ps.origin[1])<.001);CHECK(cl.ps.delta_angles[YAW]==0);CHECK(r->s.groundEntityNum==ENTITYNUM_NONE && pushed_p==pushed);}
 else if(!strcmp(name,"stop-nonrider"))CHECK(!result && cl.ps.origin[0]==10 && pushed_p==pushed);
 else if(!strcmp(name,"stop-rider"))CHECK(result && cl.ps.origin[0]==13);
 else CHECK(0);
 return failures?1:0;
}
