#include "../../engine/code/game/g_local.h"
level_locals_t level;
gentity_t g_entities[MAX_GENTITIES];
vmCvar_t g_speedDamage,g_speed,g_speedDamageMax,g_speedKnockback,g_swordGuardRaise,g_swordGuardBreak,g_swordRiposte;
vmCvar_t g_noArmor;
vmCvar_t g_knockback,g_friendlyFire,g_gametype,g_selfDamageScale,g_debugDamage;
static gclient_t clients[2];static gentity_t spark;static int failures, deaths;
#define CHECK(c) do {if(!(c)){printf("FAIL %d: %s (health %d)\n",__LINE__,#c,g_entities[1].health);failures++;}}while(0)
void QDECL Com_Printf(const char *fmt,...){}
void QDECL Com_Error(int code,const char *fmt,...){abort();}
void QDECL G_Printf(const char *fmt,...){}
gentity_t *G_TempEntity(vec3_t origin,int event){return &spark;}
void G_AddEvent(gentity_t *ent,int event,int parm){}
void Team_CheckHurtCarrier(gentity_t *a,gentity_t *b){}
qboolean OnSameTeam(gentity_t *a,gentity_t *b){return a->client && b->client && a->client->sess.sessionTeam!=TEAM_FREE && a->client->sess.sessionTeam==b->client->sess.sessionTeam;}
static void die(gentity_t *a,gentity_t *b,gentity_t *c,int damage,int mod){deaths++;}
#include "combat_under_test.h"
