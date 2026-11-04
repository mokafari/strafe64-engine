/* Real impact -> deflection / real G_Damage; effects and unrelated hook/splash branches are stubbed. */
#include "combat_fixture.h"
static int obstructed;
static gentity_t effect;
void G_Sound(gentity_t *e,int channel,int index){}
int G_SoundIndex(char *name){return 1;}
void G_BounceMissile(gentity_t *e,trace_t *t){}
void Grapple_Anchor(gentity_t *e,gentity_t *owner,vec3_t p,gentity_t *hit){}
gentity_t *G_Spawn(void){return &effect;}
void G_SetOrigin(gentity_t *e,vec3_t p){VectorCopy(p,e->r.currentOrigin);VectorCopy(p,e->s.pos.trBase);}
void trap_LinkEntity(gentity_t *e){}
void trap_SendServerCommand(int n,const char *cmd){}
qboolean G_RadiusDamage(vec3_t p,gentity_t *a,float damage,float radius,gentity_t *ignore,int mod){return qfalse;}
qboolean LogAccuracyHit(gentity_t *target,gentity_t *attacker){return target!=attacker && target->client && attacker->client && target->health>0;}
void SnapVectorTowards(vec3_t v,vec3_t to){}
void trap_Trace(trace_t *t,const vec3_t from,const vec3_t mins,const vec3_t maxs,const vec3_t to,int pass,int mask){memset(t,0,sizeof(*t));t->fraction=obstructed?.5f:1;t->entityNum=obstructed?ENTITYNUM_WORLD:ENTITYNUM_NONE;VectorCopy(to,t->endpos);}
#include "missile_under_test.h"
int main(int argc,char **argv){
 const char *name=argc>1?argv[1]:"";int i;gentity_t missile={0};trace_t impact={0};
 level.time=1000;level.maxclients=2;g_swordGuardRaise.value=80;g_selfDamageScale.value=1;srand(1);
 for(i=0;i<2;i++){g_entities[i].s.number=i;g_entities[i].s.eType=ET_PLAYER;g_entities[i].client=&clients[i];g_entities[i].inuse=g_entities[i].takedamage=qtrue;g_entities[i].health=100;g_entities[i].die=die;clients[i].ps.stats[STAT_MAX_HEALTH]=clients[i].ps.stats[STAT_HEALTH]=100;}
 clients[1].ps.eFlags=EF_BLOCKING;clients[1].guardRaiseTime=900;
 missile.classname="testbolt";missile.s.eType=ET_MISSILE;missile.s.weapon=WP_MACHINEGUN;missile.damage=40;missile.methodOfDeath=MOD_MACHINEGUN;
 missile.r.ownerNum=0;missile.parent=&g_entities[0];missile.s.pos.trType=TR_LINEAR;missile.s.pos.trTime=900;
 VectorSet(missile.s.pos.trBase,100,0,0);VectorSet(missile.s.pos.trDelta,-1000,0,0);
 VectorSet(missile.r.currentOrigin,10,0,0);impact.entityNum=1;VectorSet(impact.endpos,10,0,0);
 if(!strcmp(name,"guard-early"))clients[1].guardRaiseTime=921;
 if(!strcmp(name,"guard-boundary"))clients[1].guardRaiseTime=920;
 if(!strcmp(name,"rear-hit"))missile.s.pos.trDelta[0]=1000;
 if(!strcmp(name,"hidden-target") || !strcmp(name,"visible-target")){VectorSet(g_entities[0].r.currentOrigin,100,100,0);obstructed=!strcmp(name,"hidden-target");}
 G_MissileImpact(&missile,&impact);
 if(!strcmp(name,"guard-early") || !strcmp(name,"rear-hit")){CHECK(g_entities[1].health==60 && missile.freeAfterEvent);CHECK(missile.r.ownerNum==0);}
 else if(!strcmp(name,"deflect") || !strcmp(name,"guard-boundary")){CHECK(g_entities[1].health==100 && !missile.freeAfterEvent);CHECK(missile.r.ownerNum==1 && missile.parent==&g_entities[1]);CHECK(missile.s.generic1==1 && missile.s.pos.trDelta[0]>1800);}
 else if(!strcmp(name,"impact-origin"))CHECK(fabs(missile.s.pos.trBase[0]-10)<.001);
 else if(!strcmp(name,"hidden-target"))CHECK(fabs(missile.s.pos.trDelta[1])<100);
 else if(!strcmp(name,"visible-target"))CHECK(missile.s.pos.trDelta[0]>1000 && missile.s.pos.trDelta[1]>1000);
 else if(!strcmp(name,"return-damage")){
  impact.entityNum=0;clients[0].ps.eFlags=0;G_MissileImpact(&missile,&impact);
  CHECK(g_entities[0].health==60 && g_entities[1].health==100);
  CHECK(clients[0].ps.persistant[PERS_ATTACKER]==1 && clients[1].accuracy_hits==1);
 }else CHECK(0);
 return failures?1:0;
}
