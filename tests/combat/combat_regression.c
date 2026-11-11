#include "combat_fixture.h"
int main(int argc,char **argv){
 const char *test=argc>1?argv[1]:"";int i,damage=40,flags=DAMAGE_NO_KNOCKBACK;vec3_t direction={-1,0,0},point={0,0,0};
 level.time=1000;g_swordGuardRaise.value=80;g_swordRiposte.value=500;g_selfDamageScale.value=1;
 for(i=0;i<2;i++){g_entities[i].s.number=i;g_entities[i].s.eType=ET_PLAYER;g_entities[i].client=&clients[i];g_entities[i].health=100;g_entities[i].takedamage=qtrue;g_entities[i].die=die;clients[i].ps.stats[STAT_HEALTH]=clients[i].ps.stats[STAT_MAX_HEALTH]=100;}
 clients[1].ps.eFlags=EF_BLOCKING;clients[1].guardRaiseTime=900;
 clients[0].swordSwingParm=SWORD_PACK_QUADS(SQ_B,SQ_T);
 if(!strcmp(test,"armor-limited")){clients[1].ps.eFlags=0;clients[1].ps.stats[STAT_ARMOR]=10;}
 else if(!strcmp(test,"guard-break-boundary") || !strcmp(test,"guard-break-expired")){g_swordGuardBreak.integer=1;clients[0].guardReleaseTime=!strcmp(test,"guard-break-boundary")?750:749;}
 else if(!strcmp(test,"protection-bypass"))flags|=DAMAGE_NO_PROTECTION;
 else if(!strcmp(test,"knockback")){clients[1].ps.eFlags=0;flags=0;g_knockback.value=1000;}
 else if(!strcmp(test,"raise-boundary"))clients[1].guardRaiseTime=920;
 else if(!strcmp(test,"raise-early"))clients[1].guardRaiseTime=921;
 else if(!strcmp(test,"rear-hit"))direction[0]=1;
 else if(!strcmp(test,"glancing"))clients[0].swordSwingParm=SWORD_PACK_QUADS(SQ_L,SQ_R);
 else if(!strcmp(test,"wrong-guard"))clients[0].swordSwingParm=SWORD_PACK_QUADS(SQ_T,SQ_B);
 else if(!strcmp(test,"guard-break")){g_swordGuardBreak.integer=1;clients[0].guardReleaseTime=900;}
 else if(!strcmp(test,"no-armor")){g_noArmor.integer=1;clients[1].ps.eFlags=0;clients[1].ps.stats[STAT_ARMOR]=50;}
 else if(!strcmp(test,"armor") || !strcmp(test,"armor-bypass")){clients[1].ps.eFlags=0;clients[1].ps.stats[STAT_ARMOR]=50;if(!strcmp(test,"armor-bypass"))flags|=DAMAGE_NO_ARMOR;}
 else if(!strcmp(test,"friendly-fire")){clients[1].ps.eFlags=0;clients[0].sess.sessionTeam=clients[1].sess.sessionTeam=TEAM_RED;}
 else if(!strcmp(test,"lethal")){clients[1].ps.eFlags=0;damage=120;}
 G_Damage(&g_entities[1],&g_entities[0],&g_entities[0],direction,point,damage,flags,MOD_SWORD);
 if(!strcmp(test,"clean-parry") || !strcmp(test,"raise-boundary")){CHECK(g_entities[1].health==100);CHECK(clients[1].riposteTime==1500 && clients[0].ps.weaponTime==350);CHECK(clients[1].damage_blood==0);}
 else if(!strcmp(test,"raise-early") || !strcmp(test,"rear-hit")){CHECK(g_entities[1].health==60 && clients[1].riposteTime==0);}
 else if(!strcmp(test,"glancing"))CHECK(g_entities[1].health==84);
 else if(!strcmp(test,"wrong-guard"))CHECK(g_entities[1].health==66);
 else if(!strcmp(test,"guard-break"))CHECK(g_entities[1].health==70 && clients[1].riposteTime==0);
 else if(!strcmp(test,"no-armor"))CHECK(g_entities[1].health==60);
 else if(!strcmp(test,"armor"))CHECK(g_entities[1].health==87 && clients[1].ps.stats[STAT_ARMOR]==23);
 else if(!strcmp(test,"armor-bypass"))CHECK(g_entities[1].health==60 && clients[1].ps.stats[STAT_ARMOR]==50);
 else if(!strcmp(test,"friendly-fire"))CHECK(g_entities[1].health==100);
 else if(!strcmp(test,"armor-limited"))CHECK(g_entities[1].health==70 && clients[1].ps.stats[STAT_ARMOR]==0);
 else if(!strcmp(test,"guard-break-boundary"))CHECK(g_entities[1].health==70);
 else if(!strcmp(test,"guard-break-expired"))CHECK(g_entities[1].health==100 && clients[1].riposteTime==1500);
 else if(!strcmp(test,"protection-bypass"))CHECK(g_entities[1].health==60 && clients[1].riposteTime==0);
 else if(!strcmp(test,"knockback"))CHECK(g_entities[1].health==60 && fabs(clients[1].ps.velocity[0]+200)<.01 && clients[1].ps.pm_time==80);
 else if(!strcmp(test,"lethal"))CHECK(deaths==1 && g_entities[1].health==-20 && clients[1].ps.stats[STAT_HEALTH]==-20);
 else CHECK(0);
 return failures?1:0;
}
