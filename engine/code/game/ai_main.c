/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
//

/*****************************************************************************
 * name:		ai_main.c
 *
 * desc:		Quake3 bot AI
 *
 * $Archive: /MissionPack/code/game/ai_main.c $
 *
 *****************************************************************************/


#include "g_local.h"
#include "../qcommon/q_shared.h"
#include "../botlib/botlib.h"		//bot lib interface
#include "../botlib/be_aas.h"
#include "../botlib/be_ea.h"
#include "../botlib/be_ai_char.h"
#include "../botlib/be_ai_chat.h"
#include "../botlib/be_ai_gen.h"
#include "../botlib/be_ai_goal.h"
#include "../botlib/be_ai_move.h"
#include "../botlib/be_ai_weap.h"
//
#include "ai_main.h"
#include "ai_dmq3.h"
#include "ai_chat.h"
#include "ai_cmd.h"
#include "ai_dmnet.h"
#include "ai_vcmd.h"

//
#include "chars.h"
#include "inv.h"
#include "syn.h"


//bot states
bot_state_t	*botstates[MAX_CLIENTS];
//number of bots
int numbots;
//floating point time
float floattime;
//time to do a regular update
float regularupdate_time;
//
int bot_interbreed;
int bot_interbreedmatchcount;
//
vmCvar_t bot_thinktime;
vmCvar_t bot_memorydump;
vmCvar_t bot_saveroutingcache;
vmCvar_t bot_pause;
vmCvar_t bot_report;
vmCvar_t bot_testsolid;
vmCvar_t bot_testclusters;
vmCvar_t bot_developer;
vmCvar_t bot_interbreedchar;
vmCvar_t bot_interbreedbots;
vmCvar_t bot_interbreedcycle;
vmCvar_t bot_interbreedwrite;
vmCvar_t bot_moveset;			// STRAFE 64: bots use the bhop/wall/double-jump kit
vmCvar_t bot_combatBhop;		// STRAFE 64: bots fight on the move (bhop-rush + peel); arena/DM only — leaks onto movement courses, so off by default
vmCvar_t bot_swordBlock;		// STRAFE 64: bots parry incoming katana swings (reactive, skill-gated)
vmCvar_t bot_dash;				// STRAFE 64: bots tap the SHIFT dash (BUTTON_DASH) to close on enemies / extend gap-leaps
vmCvar_t bot_slide;				// STRAFE 64: bots crouch-slide (slidehops on fast, straight sections)
vmCvar_t bot_airStrafe;			// STRAFE 64: bots carve for speed in the air (pure A/D strafe at the optimal angle while turning toward the goal)
vmCvar_t bot_brake;				// STRAFE 64: bots ease off the bhop when closing on a goal so they settle onto items instead of overshooting at full speed
vmCvar_t bot_unstick;			// STRAFE 64: bots break out of wall grinding/circling with a ninja wall-kick

void ExitLevel( void );


/*
==================
BotAI_Print
==================
*/
void QDECL BotAI_Print(int type, char *fmt, ...) {
	char str[2048];
	va_list ap;

	va_start(ap, fmt);
	Q_vsnprintf(str, sizeof(str), fmt, ap);
	va_end(ap);

	switch(type) {
		case PRT_MESSAGE: {
			G_Printf("%s", str);
			break;
		}
		case PRT_WARNING: {
			G_Printf( S_COLOR_YELLOW "Warning: %s", str );
			break;
		}
		case PRT_ERROR: {
			G_Printf( S_COLOR_RED "Error: %s", str );
			break;
		}
		case PRT_FATAL: {
			G_Printf( S_COLOR_RED "Fatal: %s", str );
			break;
		}
		case PRT_EXIT: {
			G_Error( S_COLOR_RED "Exit: %s", str );
			break;
		}
		default: {
			G_Printf( "unknown print type\n" );
			break;
		}
	}
}


/*
==================
BotAI_Trace
==================
*/
void BotAI_Trace(bsp_trace_t *bsptrace, vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, int passent, int contentmask) {
	trace_t trace;

	trap_Trace(&trace, start, mins, maxs, end, passent, contentmask);
	//copy the trace information
	bsptrace->allsolid = trace.allsolid;
	bsptrace->startsolid = trace.startsolid;
	bsptrace->fraction = trace.fraction;
	VectorCopy(trace.endpos, bsptrace->endpos);
	bsptrace->plane.dist = trace.plane.dist;
	VectorCopy(trace.plane.normal, bsptrace->plane.normal);
	bsptrace->plane.signbits = trace.plane.signbits;
	bsptrace->plane.type = trace.plane.type;
	bsptrace->surface.value = 0;
	bsptrace->surface.flags = trace.surfaceFlags;
	bsptrace->ent = trace.entityNum;
	bsptrace->exp_dist = 0;
	bsptrace->sidenum = 0;
	bsptrace->contents = 0;
}

/*
==================
BotAI_GetClientState
==================
*/
int BotAI_GetClientState( int clientNum, playerState_t *state ) {
	gentity_t	*ent;

	ent = &g_entities[clientNum];
	if ( !ent->inuse ) {
		return qfalse;
	}
	if ( !ent->client ) {
		return qfalse;
	}

	memcpy( state, &ent->client->ps, sizeof(playerState_t) );
	return qtrue;
}

/*
==================
BotAI_GetEntityState
==================
*/
int BotAI_GetEntityState( int entityNum, entityState_t *state ) {
	gentity_t	*ent;

	ent = &g_entities[entityNum];
	memset( state, 0, sizeof(entityState_t) );
	if (!ent->inuse) return qfalse;
	if (!ent->r.linked) return qfalse;
	if (ent->r.svFlags & SVF_NOCLIENT) return qfalse;
	memcpy( state, &ent->s, sizeof(entityState_t) );
	return qtrue;
}

/*
==================
BotAI_GetSnapshotEntity
==================
*/
int BotAI_GetSnapshotEntity( int clientNum, int sequence, entityState_t *state ) {
	int		entNum;

	entNum = trap_BotGetSnapshotEntity( clientNum, sequence );
	if ( entNum == -1 ) {
		memset(state, 0, sizeof(entityState_t));
		return -1;
	}

	BotAI_GetEntityState( entNum, state );

	return sequence + 1;
}

/*
==================
BotAI_BotInitialChat
==================
*/
void QDECL BotAI_BotInitialChat( bot_state_t *bs, char *type, ... ) {
	int		i, mcontext;
	va_list	ap;
	char	*p;
	char	*vars[MAX_MATCHVARIABLES];

	memset(vars, 0, sizeof(vars));
	va_start(ap, type);
	p = va_arg(ap, char *);
	for (i = 0; i < MAX_MATCHVARIABLES; i++) {
		if( !p ) {
			break;
		}
		vars[i] = p;
		p = va_arg(ap, char *);
	}
	va_end(ap);

	mcontext = BotSynonymContext(bs);

	trap_BotInitialChat( bs->cs, type, mcontext, vars[0], vars[1], vars[2], vars[3], vars[4], vars[5], vars[6], vars[7] );
}


/*
==================
BotTestAAS
==================
*/
void BotTestAAS(vec3_t origin) {
	int areanum;
	aas_areainfo_t info;

	trap_Cvar_Update(&bot_testsolid);
	trap_Cvar_Update(&bot_testclusters);
	if (bot_testsolid.integer) {
		if (!trap_AAS_Initialized()) return;
		areanum = BotPointAreaNum(origin);
		if (areanum) BotAI_Print(PRT_MESSAGE, "\rempty area");
		else BotAI_Print(PRT_MESSAGE, "\r^1SOLID area");
	}
	else if (bot_testclusters.integer) {
		if (!trap_AAS_Initialized()) return;
		areanum = BotPointAreaNum(origin);
		if (!areanum)
			BotAI_Print(PRT_MESSAGE, "\r^1Solid!                              ");
		else {
			trap_AAS_AreaInfo(areanum, &info);
			BotAI_Print(PRT_MESSAGE, "\rarea %d, cluster %d       ", areanum, info.cluster);
		}
	}
}

/*
==================
BotReportStatus
==================
*/
void BotReportStatus(bot_state_t *bs) {
	char goalname[MAX_MESSAGE_SIZE];
	char netname[MAX_MESSAGE_SIZE];
	char *leader, flagstatus[32];
	//
	ClientName(bs->client, netname, sizeof(netname));
	if (Q_stricmp(netname, bs->teamleader) == 0) leader = "L";
	else leader = " ";

	strcpy(flagstatus, "  ");
	if (gametype == GT_CTF) {
		if (BotCTFCarryingFlag(bs)) {
			if (BotTeam(bs) == TEAM_RED) strcpy(flagstatus, S_COLOR_RED"F ");
			else strcpy(flagstatus, S_COLOR_BLUE"F ");
		}
	}
#ifdef MISSIONPACK
	else if (gametype == GT_1FCTF) {
		if (Bot1FCTFCarryingFlag(bs)) {
			if (BotTeam(bs) == TEAM_RED) strcpy(flagstatus, S_COLOR_RED"F ");
			else strcpy(flagstatus, S_COLOR_BLUE"F ");
		}
	}
	else if (gametype == GT_HARVESTER) {
		if (BotHarvesterCarryingCubes(bs)) {
			if (BotTeam(bs) == TEAM_RED) Com_sprintf(flagstatus, sizeof(flagstatus), S_COLOR_RED"%2d", bs->inventory[INVENTORY_REDCUBE]);
			else Com_sprintf(flagstatus, sizeof(flagstatus), S_COLOR_BLUE"%2d", bs->inventory[INVENTORY_BLUECUBE]);
		}
	}
#endif

	switch(bs->ltgtype) {
		case LTG_TEAMHELP:
		{
			EasyClientName(bs->teammate, goalname, sizeof(goalname));
			BotAI_Print(PRT_MESSAGE, "%-20s%s%s: helping %s\n", netname, leader, flagstatus, goalname);
			break;
		}
		case LTG_TEAMACCOMPANY:
		{
			EasyClientName(bs->teammate, goalname, sizeof(goalname));
			BotAI_Print(PRT_MESSAGE, "%-20s%s%s: accompanying %s\n", netname, leader, flagstatus, goalname);
			break;
		}
		case LTG_DEFENDKEYAREA:
		{
			trap_BotGoalName(bs->teamgoal.number, goalname, sizeof(goalname));
			BotAI_Print(PRT_MESSAGE, "%-20s%s%s: defending %s\n", netname, leader, flagstatus, goalname);
			break;
		}
		case LTG_GETITEM:
		{
			trap_BotGoalName(bs->teamgoal.number, goalname, sizeof(goalname));
			BotAI_Print(PRT_MESSAGE, "%-20s%s%s: getting item %s\n", netname, leader, flagstatus, goalname);
			break;
		}
		case LTG_KILL:
		{
			ClientName(bs->teamgoal.entitynum, goalname, sizeof(goalname));
			BotAI_Print(PRT_MESSAGE, "%-20s%s%s: killing %s\n", netname, leader, flagstatus, goalname);
			break;
		}
		case LTG_CAMP:
		case LTG_CAMPORDER:
		{
			BotAI_Print(PRT_MESSAGE, "%-20s%s%s: camping\n", netname, leader, flagstatus);
			break;
		}
		case LTG_PATROL:
		{
			BotAI_Print(PRT_MESSAGE, "%-20s%s%s: patrolling\n", netname, leader, flagstatus);
			break;
		}
		case LTG_GETFLAG:
		{
			BotAI_Print(PRT_MESSAGE, "%-20s%s%s: capturing flag\n", netname, leader, flagstatus);
			break;
		}
		case LTG_RUSHBASE:
		{
			BotAI_Print(PRT_MESSAGE, "%-20s%s%s: rushing base\n", netname, leader, flagstatus);
			break;
		}
		case LTG_RETURNFLAG:
		{
			BotAI_Print(PRT_MESSAGE, "%-20s%s%s: returning flag\n", netname, leader, flagstatus);
			break;
		}
		case LTG_ATTACKENEMYBASE:
		{
			BotAI_Print(PRT_MESSAGE, "%-20s%s%s: attacking the enemy base\n", netname, leader, flagstatus);
			break;
		}
		case LTG_HARVEST:
		{
			BotAI_Print(PRT_MESSAGE, "%-20s%s%s: harvesting\n", netname, leader, flagstatus);
			break;
		}
		default:
		{
			BotAI_Print(PRT_MESSAGE, "%-20s%s%s: roaming\n", netname, leader, flagstatus);
			break;
		}
	}
}

/*
==================
BotTeamplayReport
==================
*/
void BotTeamplayReport(void) {
	int i;
	char buf[MAX_INFO_STRING];

	BotAI_Print(PRT_MESSAGE, S_COLOR_RED"RED\n");
	for (i = 0; i < level.maxclients; i++) {
		//
		if ( !botstates[i] || !botstates[i]->inuse ) continue;
		//
		trap_GetConfigstring(CS_PLAYERS+i, buf, sizeof(buf));
		//if no config string or no name
		if (!strlen(buf) || !strlen(Info_ValueForKey(buf, "n"))) continue;
		//skip spectators
		if (atoi(Info_ValueForKey(buf, "t")) == TEAM_RED) {
			BotReportStatus(botstates[i]);
		}
	}
	BotAI_Print(PRT_MESSAGE, S_COLOR_BLUE"BLUE\n");
	for (i = 0; i < level.maxclients; i++) {
		//
		if ( !botstates[i] || !botstates[i]->inuse ) continue;
		//
		trap_GetConfigstring(CS_PLAYERS+i, buf, sizeof(buf));
		//if no config string or no name
		if (!strlen(buf) || !strlen(Info_ValueForKey(buf, "n"))) continue;
		//skip spectators
		if (atoi(Info_ValueForKey(buf, "t")) == TEAM_BLUE) {
			BotReportStatus(botstates[i]);
		}
	}
}

/*
==================
BotSetInfoConfigString
==================
*/
void BotSetInfoConfigString(bot_state_t *bs) {
	char goalname[MAX_MESSAGE_SIZE];
	char netname[MAX_MESSAGE_SIZE];
	char action[MAX_MESSAGE_SIZE];
	char *leader, carrying[32], *cs;
	bot_goal_t goal;
	//
	ClientName(bs->client, netname, sizeof(netname));
	if (Q_stricmp(netname, bs->teamleader) == 0) leader = "L";
	else leader = " ";

	strcpy(carrying, "  ");
	if (gametype == GT_CTF) {
		if (BotCTFCarryingFlag(bs)) {
			strcpy(carrying, "F ");
		}
	}
#ifdef MISSIONPACK
	else if (gametype == GT_1FCTF) {
		if (Bot1FCTFCarryingFlag(bs)) {
			strcpy(carrying, "F ");
		}
	}
	else if (gametype == GT_HARVESTER) {
		if (BotHarvesterCarryingCubes(bs)) {
			if (BotTeam(bs) == TEAM_RED) Com_sprintf(carrying, sizeof(carrying), "%2d", bs->inventory[INVENTORY_REDCUBE]);
			else Com_sprintf(carrying, sizeof(carrying), "%2d", bs->inventory[INVENTORY_BLUECUBE]);
		}
	}
#endif

	switch(bs->ltgtype) {
		case LTG_TEAMHELP:
		{
			EasyClientName(bs->teammate, goalname, sizeof(goalname));
			Com_sprintf(action, sizeof(action), "helping %s", goalname);
			break;
		}
		case LTG_TEAMACCOMPANY:
		{
			EasyClientName(bs->teammate, goalname, sizeof(goalname));
			Com_sprintf(action, sizeof(action), "accompanying %s", goalname);
			break;
		}
		case LTG_DEFENDKEYAREA:
		{
			trap_BotGoalName(bs->teamgoal.number, goalname, sizeof(goalname));
			Com_sprintf(action, sizeof(action), "defending %s", goalname);
			break;
		}
		case LTG_GETITEM:
		{
			trap_BotGoalName(bs->teamgoal.number, goalname, sizeof(goalname));
			Com_sprintf(action, sizeof(action), "getting item %s", goalname);
			break;
		}
		case LTG_KILL:
		{
			ClientName(bs->teamgoal.entitynum, goalname, sizeof(goalname));
			Com_sprintf(action, sizeof(action), "killing %s", goalname);
			break;
		}
		case LTG_CAMP:
		case LTG_CAMPORDER:
		{
			Com_sprintf(action, sizeof(action), "camping");
			break;
		}
		case LTG_PATROL:
		{
			Com_sprintf(action, sizeof(action), "patrolling");
			break;
		}
		case LTG_GETFLAG:
		{
			Com_sprintf(action, sizeof(action), "capturing flag");
			break;
		}
		case LTG_RUSHBASE:
		{
			Com_sprintf(action, sizeof(action), "rushing base");
			break;
		}
		case LTG_RETURNFLAG:
		{
			Com_sprintf(action, sizeof(action), "returning flag");
			break;
		}
		case LTG_ATTACKENEMYBASE:
		{
			Com_sprintf(action, sizeof(action), "attacking the enemy base");
			break;
		}
		case LTG_HARVEST:
		{
			Com_sprintf(action, sizeof(action), "harvesting");
			break;
		}
		default:
		{
			trap_BotGetTopGoal(bs->gs, &goal);
			trap_BotGoalName(goal.number, goalname, sizeof(goalname));
			Com_sprintf(action, sizeof(action), "roaming %s", goalname);
			break;
		}
	}
  	cs = va("l\\%s\\c\\%s\\a\\%s",
				leader,
				carrying,
				action);
  	trap_SetConfigstring (CS_BOTINFO + bs->client, cs);
}

/*
==============
BotUpdateInfoConfigStrings
==============
*/
void BotUpdateInfoConfigStrings(void) {
	int i;
	char buf[MAX_INFO_STRING];

	for (i = 0; i < level.maxclients; i++) {
		//
		if ( !botstates[i] || !botstates[i]->inuse )
			continue;
		//
		trap_GetConfigstring(CS_PLAYERS+i, buf, sizeof(buf));
		//if no config string or no name
		if (!strlen(buf) || !strlen(Info_ValueForKey(buf, "n")))
			continue;
		BotSetInfoConfigString(botstates[i]);
	}
}

/*
==============
BotInterbreedBots
==============
*/
void BotInterbreedBots(void) {
	float ranks[MAX_CLIENTS];
	int parent1, parent2, child;
	int i;

	// get rankings for all the bots
	for (i = 0; i < MAX_CLIENTS; i++) {
		if ( botstates[i] && botstates[i]->inuse ) {
			ranks[i] = botstates[i]->num_kills * 2 - botstates[i]->num_deaths;
		}
		else {
			ranks[i] = -1;
		}
	}

	if (trap_GeneticParentsAndChildSelection(MAX_CLIENTS, ranks, &parent1, &parent2, &child)) {
		trap_BotInterbreedGoalFuzzyLogic(botstates[parent1]->gs, botstates[parent2]->gs, botstates[child]->gs);
		trap_BotMutateGoalFuzzyLogic(botstates[child]->gs, 1);
	}
	// reset the kills and deaths
	for (i = 0; i < MAX_CLIENTS; i++) {
		if (botstates[i] && botstates[i]->inuse) {
			botstates[i]->num_kills = 0;
			botstates[i]->num_deaths = 0;
		}
	}
}

/*
==============
BotWriteInterbreeded
==============
*/
void BotWriteInterbreeded(char *filename) {
	float rank, bestrank;
	int i, bestbot;

	bestrank = 0;
	bestbot = -1;
	// get the best bot
	for (i = 0; i < MAX_CLIENTS; i++) {
		if ( botstates[i] && botstates[i]->inuse ) {
			rank = botstates[i]->num_kills * 2 - botstates[i]->num_deaths;
		}
		else {
			rank = -1;
		}
		if (rank > bestrank) {
			bestrank = rank;
			bestbot = i;
		}
	}
	if (bestbot >= 0) {
		//write out the new goal fuzzy logic
		trap_BotSaveGoalFuzzyLogic(botstates[bestbot]->gs, filename);
	}
}

/*
==============
BotInterbreedEndMatch

add link back into ExitLevel?
==============
*/
void BotInterbreedEndMatch(void) {

	if (!bot_interbreed) return;
	bot_interbreedmatchcount++;
	if (bot_interbreedmatchcount >= bot_interbreedcycle.integer) {
		bot_interbreedmatchcount = 0;
		//
		trap_Cvar_Update(&bot_interbreedwrite);
		if (strlen(bot_interbreedwrite.string)) {
			BotWriteInterbreeded(bot_interbreedwrite.string);
			trap_Cvar_Set("bot_interbreedwrite", "");
		}
		BotInterbreedBots();
	}
}

/*
==============
BotInterbreeding
==============
*/
void BotInterbreeding(void) {
	int i;

	trap_Cvar_Update(&bot_interbreedchar);
	if (!strlen(bot_interbreedchar.string)) return;
	//make sure we are in tournament mode
	if (gametype != GT_TOURNAMENT) {
		trap_Cvar_Set("g_gametype", va("%d", GT_TOURNAMENT));
		ExitLevel();
		return;
	}
	//shutdown all the bots
	for (i = 0; i < MAX_CLIENTS; i++) {
		if (botstates[i] && botstates[i]->inuse) {
			BotAIShutdownClient(botstates[i]->client, qfalse);
		}
	}
	//make sure all item weight configs are reloaded and Not shared
	trap_BotLibVarSet("bot_reloadcharacters", "1");
	//add a number of bots using the desired bot character
	for (i = 0; i < bot_interbreedbots.integer; i++) {
		trap_SendConsoleCommand( EXEC_INSERT, va("addbot %s 4 free %i %s%d\n",
						bot_interbreedchar.string, i * 50, bot_interbreedchar.string, i) );
	}
	//
	trap_Cvar_Set("bot_interbreedchar", "");
	bot_interbreed = qtrue;
}

/*
==============
BotEntityInfo
==============
*/
void BotEntityInfo(int entnum, aas_entityinfo_t *info) {
	trap_AAS_EntityInfo(entnum, info);
}

/*
==============
NumBots
==============
*/
int NumBots(void) {
	return numbots;
}

/*
==============
BotTeamLeader
==============
*/
int BotTeamLeader(bot_state_t *bs) {
	int leader;

	leader = ClientFromName(bs->teamleader);
	if (leader < 0) return qfalse;
	if (!botstates[leader] || !botstates[leader]->inuse) return qfalse;
	return qtrue;
}

/*
==============
AngleDifference
==============
*/
float AngleDifference(float ang1, float ang2) {
	float diff;

	diff = ang1 - ang2;
	if (ang1 > ang2) {
		if (diff > 180.0) diff -= 360.0;
	}
	else {
		if (diff < -180.0) diff += 360.0;
	}
	return diff;
}

/*
==============
BotChangeViewAngle
==============
*/
float BotChangeViewAngle(float angle, float ideal_angle, float speed) {
	float move;

	angle = AngleMod(angle);
	ideal_angle = AngleMod(ideal_angle);
	if (angle == ideal_angle) return angle;
	move = ideal_angle - angle;
	if (ideal_angle > angle) {
		if (move > 180.0) move -= 360.0;
	}
	else {
		if (move < -180.0) move += 360.0;
	}
	if (move > 0) {
		if (move > speed) move = speed;
	}
	else {
		if (move < -speed) move = -speed;
	}
	return AngleMod(angle + move);
}

/*
==============
BotChangeViewAngles
==============
*/
void BotChangeViewAngles(bot_state_t *bs, float thinktime) {
	float diff, factor, maxchange, anglespeed, disired_speed;
	int i;

	if (bs->ideal_viewangles[PITCH] > 180) bs->ideal_viewangles[PITCH] -= 360;
	//
	if (bs->enemy >= 0) {
		factor = trap_Characteristic_BFloat(bs->character, CHARACTERISTIC_VIEW_FACTOR, 0.01f, 1);
		maxchange = trap_Characteristic_BFloat(bs->character, CHARACTERISTIC_VIEW_MAXCHANGE, 1, 1800);
		if (maxchange < 240) maxchange = 240;
		// STRAFE 64: a melee duel tracks a CLOSE enemy that subtends a big angular
		// rate as it weaves. The stock "over reaction" model (below) overshoots that
		// and the aim wobble feeds back into the bot's view-relative strafe — so it
		// loops in circles instead of trading. Lock on FAST and smooth: a high factor
		// + high cap keeps the aim planted on the enemy, the feet stay pointed at it,
		// and the duel reads as an exchange, not an orbit.
		if (g_botSwordOnly.integer) {
			factor = 0.5f;
			maxchange = 900;
		}
	}
	else {
		// STRAFE 64: while TRAVELLING (no enemy) the stock view snaps at up to
		// 360 deg/s through the "over reaction" model below, which overshoots and
		// makes bot headings — and so their paths — read as erratic. Sweep
		// smoothly instead: track the goal heading directly (higher factor) at a
		// capped rate via the smooth model. Result: flowing arcs, not jitter.
		factor = 0.22f;
		maxchange = 160;
	}
	maxchange *= thinktime;
	for (i = 0; i < 2; i++) {
		//
		if (bot_challenge.integer || bs->enemy < 0 || g_botSwordOnly.integer) {
			//smooth slowdown view model (STRAFE 64: also for sword duels — see above)
			diff = fabs(AngleDifference(bs->viewangles[i], bs->ideal_viewangles[i]));
			anglespeed = diff * factor;
			if (anglespeed > maxchange) anglespeed = maxchange;
			bs->viewangles[i] = BotChangeViewAngle(bs->viewangles[i],
											bs->ideal_viewangles[i], anglespeed);
		}
		else {
			//over reaction view model
			bs->viewangles[i] = AngleMod(bs->viewangles[i]);
			bs->ideal_viewangles[i] = AngleMod(bs->ideal_viewangles[i]);
			diff = AngleDifference(bs->viewangles[i], bs->ideal_viewangles[i]);
			disired_speed = diff * factor;
			bs->viewanglespeed[i] += (bs->viewanglespeed[i] - disired_speed);
			if (bs->viewanglespeed[i] > 180) bs->viewanglespeed[i] = maxchange;
			if (bs->viewanglespeed[i] < -180) bs->viewanglespeed[i] = -maxchange;
			anglespeed = bs->viewanglespeed[i];
			if (anglespeed > maxchange) anglespeed = maxchange;
			if (anglespeed < -maxchange) anglespeed = -maxchange;
			bs->viewangles[i] += anglespeed;
			bs->viewangles[i] = AngleMod(bs->viewangles[i]);
			//demping
			bs->viewanglespeed[i] *= 0.45 * (1 - factor);
		}
		//BotAI_Print(PRT_MESSAGE, "ideal_angles %f %f\n", bs->ideal_viewangles[0], bs->ideal_viewangles[1], bs->ideal_viewangles[2]);`
		//bs->viewangles[i] = bs->ideal_viewangles[i];
	}
	//bs->viewangles[PITCH] = 0;
	if (bs->viewangles[PITCH] > 180) bs->viewangles[PITCH] -= 360;
	//elementary action: view
	trap_EA_View(bs->client, bs->viewangles);
}

/*
==============
BotInputToUserCommand
==============
*/
void BotInputToUserCommand(bot_input_t *bi, usercmd_t *ucmd, int delta_angles[3], int time) {
	vec3_t angles, forward, right;
	short temp;
	int j;
	float f, r, u, m;

	//clear the whole structure
	memset(ucmd, 0, sizeof(usercmd_t));
	//the duration for the user command in milli seconds
	ucmd->serverTime = time;
	//
	if (bi->actionflags & ACTION_DELAYEDJUMP) {
		bi->actionflags |= ACTION_JUMP;
		bi->actionflags &= ~ACTION_DELAYEDJUMP;
	}
	//set the buttons
	if (bi->actionflags & ACTION_RESPAWN) ucmd->buttons = BUTTON_ATTACK;
	if (bi->actionflags & ACTION_ATTACK) ucmd->buttons |= BUTTON_ATTACK;
	if (bi->actionflags & ACTION_TALK) ucmd->buttons |= BUTTON_TALK;
	if (bi->actionflags & ACTION_GESTURE) ucmd->buttons |= BUTTON_GESTURE;
	if (bi->actionflags & ACTION_USE) ucmd->buttons |= BUTTON_USE_HOLDABLE;
	if (bi->actionflags & ACTION_WALK) ucmd->buttons |= BUTTON_WALKING;
	if (bi->actionflags & ACTION_AFFIRMATIVE) ucmd->buttons |= BUTTON_AFFIRMATIVE;
	if (bi->actionflags & ACTION_NEGATIVE) ucmd->buttons |= BUTTON_NEGATIVE;
	if (bi->actionflags & ACTION_GETFLAG) ucmd->buttons |= BUTTON_GETFLAG;
	if (bi->actionflags & ACTION_GUARDBASE) ucmd->buttons |= BUTTON_GUARDBASE;
	if (bi->actionflags & ACTION_PATROL) ucmd->buttons |= BUTTON_PATROL;
	if (bi->actionflags & ACTION_FOLLOWME) ucmd->buttons |= BUTTON_FOLLOWME;
	//
	ucmd->weapon = bi->weapon;
	//set the view angles
	//NOTE: the ucmd->angles are the angles WITHOUT the delta angles
	ucmd->angles[PITCH] = ANGLE2SHORT(bi->viewangles[PITCH]);
	ucmd->angles[YAW] = ANGLE2SHORT(bi->viewangles[YAW]);
	ucmd->angles[ROLL] = ANGLE2SHORT(bi->viewangles[ROLL]);
	//subtract the delta angles
	for (j = 0; j < 3; j++) {
		temp = ucmd->angles[j] - delta_angles[j];
		/*NOTE: disabled because temp should be mod first
		if ( j == PITCH ) {
			// don't let the player look up or down more than 90 degrees
			if ( temp > 16000 ) temp = 16000;
			else if ( temp < -16000 ) temp = -16000;
		}
		*/
		ucmd->angles[j] = temp;
	}
	//NOTE: movement is relative to the REAL view angles
	//get the horizontal forward and right vector
	//get the pitch in the range [-180, 180]
	if (bi->dir[2]) angles[PITCH] = bi->viewangles[PITCH];
	else angles[PITCH] = 0;
	angles[YAW] = bi->viewangles[YAW];
	angles[ROLL] = 0;
	AngleVectors(angles, forward, right, NULL);
	//bot input speed is in the range [0, 400]
	bi->speed = bi->speed * 127 / 400;
	//set the view independent movement
	f = DotProduct(forward, bi->dir);
	r = DotProduct(right, bi->dir);
	u = fabs(forward[2]) * bi->dir[2];
	m = fabs(f);

	if (fabs(r) > m) {
		m = fabs(r);
	}

	if (fabs(u) > m) {
		m = fabs(u);
	}

	if (m > 0) {
		f *= bi->speed / m;
		r *= bi->speed / m;
		u *= bi->speed / m;
	}

	ucmd->forwardmove = f;
	ucmd->rightmove = r;
	ucmd->upmove = u;

	if (bi->actionflags & ACTION_MOVEFORWARD) ucmd->forwardmove = 127;
	if (bi->actionflags & ACTION_MOVEBACK) ucmd->forwardmove = -127;
	if (bi->actionflags & ACTION_MOVELEFT) ucmd->rightmove = -127;
	if (bi->actionflags & ACTION_MOVERIGHT) ucmd->rightmove = 127;
	//jump/moveup
	if (bi->actionflags & ACTION_JUMP) ucmd->upmove = 127;
	//crouch/movedown
	if (bi->actionflags & ACTION_CROUCH) ucmd->upmove = -127;
}

/*
==============
BotApplyMoveset

Teach the bots the STRAFE 64 kit. The AI still paths with the stock AAS
navigation; here we augment the finished command each frame so the bots
actually *use* the new moves while travelling:
  - grounded at pace : hold jump, which our mod rehops on every landing into
                       a bunny-hop chain that keeps and builds speed
  - airborne, falling: spend one mid-air jump to extend the leap. Pulsing it
                       (release then press) gives the engine a fresh press,
                       which fires a wall jump next to a wall or a double jump
                       in the open — letting bots clear the wider gaps the
                       snappier physics created.
Crouch-slides and wall-runs then fall out for free: ducking at speed slides,
and holding forward along a wall while airborne wall-runs.
==============
*/
// surf decision stashed between the view phase (BotSurfControl, before the view
// is committed) and the move phase (BotApplyMoveset). 0 = not surfing, else the
// strafe command (+/-127) to hold into the bank.
static int g_botSurfStrafe[MAX_CLIENTS];
// air-strafe carve decision, set in the view phase (BotAirStrafe, before the view
// is committed) and consumed in BotApplyMoveset. 0 = not carving, else the strafe
// command (+/-127) to hold while the view leads velocity by phi_opt.
static int g_botAirStrafe[MAX_CLIENTS];
// committed point-blank peel direction (-1/0/+1): held through a close pass so
// the bot carries momentum PAST the enemy instead of flip-flopping in an orbit.
static signed char g_botPass[MAX_CLIENTS];
// per-swing parry latch: the enemy's torsoAnim (incl. the toggle bit) keys each
// distinct swing, so we roll the skill gate ONCE per swing and hold that verdict
// for its whole duration instead of re-rolling (and flickering) every frame.
static int g_botBlockKey[MAX_CLIENTS];
static signed char g_botBlockRoll[MAX_CLIENTS];

/*
==============
BotSurfControl

Teach bots to SURF. Stock AAS nav has no surf reachabilities — a bot treats a
steep ramp as a wall and slides off. So when a bot is airborne over a surface
too steep to stand on, we take the wheel: aim the view along its travel (so the
air-accel curve builds speed instead of fighting it) and, in BotApplyMoveset,
hold the strafe key INTO the bank to stay up on the face. Must run before the
view is committed (BotChangeViewAngles), so it's called from BotUpdateInput.
==============
*/
static void BotSurfControl(bot_state_t *bs) {
	playerState_t	*ps = &bs->cur_ps;
	bsp_trace_t		tr;
	vec3_t			start, end, travel, uphill, right, ang;
	vec3_t			mins = {-15, -15, -24}, maxs = {15, 15, 16};
	vec3_t			up = {0, 0, 1};
	float			speed;

	g_botSurfStrafe[bs->client] = 0;
	if (!bot_moveset.integer) return;
	if (ps->pm_type != PM_NORMAL) return;
	if (ps->groundEntityNum != ENTITYNUM_NONE) return;		// only while airborne
	speed = sqrt(ps->velocity[0] * ps->velocity[0] + ps->velocity[1] * ps->velocity[1]);
	if (speed < 100) return;								// need travel to surf

	// a too-steep-to-stand face just below us is a surf ramp
	VectorCopy(ps->origin, start);
	VectorCopy(start, end);
	end[2] -= 48;
	BotAI_Trace(&tr, start, mins, maxs, end, bs->client, MASK_PLAYERSOLID);
	if (tr.fraction >= 1.0f) return;						// nothing under us
	if (tr.plane.normal[2] >= 0.7f || tr.plane.normal[2] <= 0.05f) return;	// walkable / wall

	// travel = the bot's GOAL heading, not its momentary velocity. Sliding off
	// the edge points velocity off the ramp; the goal heading keeps it aimed
	// across the line. We don't hijack the aim — only ADD the surf-hold strafe.
	(void)ang;
	AngleVectors(bs->ideal_viewangles, travel, NULL, NULL);
	travel[2] = 0;
	if (VectorNormalize(travel) < 0.1f) return;
	uphill[0] = tr.plane.normal[0]; uphill[1] = tr.plane.normal[1]; uphill[2] = 0;
	if (VectorNormalize(uphill) < 0.1f) return;

	// pitch level so it doesn't aim into the ramp; keep the goal yaw
	bs->ideal_viewangles[PITCH] = 0;

	// hold the strafe toward the bank (uphill of travel) to ride the face up
	// rather than slide down and off it
	CrossProduct(travel, up, right);						// bot's right of travel
	g_botSurfStrafe[bs->client] = (DotProduct(right, uphill) > 0.0f) ? 127 : -127;
}

/*
==============
BotAirStrafe

STRAFE 64: bots carve for speed like a human air-strafer. Holding forward keys
the bot through the gentle pm_airaccelerate regime; pure A/D strafe instead taps
the high-accel pm_strafeAccelerate cap regime, gaining speed every tick the view
sits past acos(clamp/|v|) of velocity — peaking at PM_OptimalStrafeAngle (the same
phi_opt the HUD meter draws). Real strafe jumping does this WHILE turning: aim a
hair off velocity toward where you want to go, hold strafe that way, and velocity
both speeds up AND curves toward the goal. So this isn't a detour from the AAS
nav — it bends momentum toward the goal heading faster than a forward-hold would.

Runs in the view phase (before BotChangeViewAngles), mirroring BotSurfControl: it
nudges only the yaw and stashes the strafe side for BotApplyMoveset. Heavily
gated — airborne, carrying real speed, no enemy (combat footwork owns the view),
and only when there's a meaningful-but-not-reversing turn to make, so it stays out
of the way on straights and disengages the instant velocity lines up with the goal.
==============
*/
static void BotAirStrafe(bot_state_t *bs) {
	playerState_t	*ps = &bs->cur_ps;
	float			speed, velYaw, goalYaw, delta, phiOpt;
	float			wishYaw, candR, candL;
	int				side;

	g_botAirStrafe[bs->client] = 0;
	if (!bot_moveset.integer || !bot_airStrafe.integer) return;
	if (ps->pm_type != PM_NORMAL) return;
	if (ps->groundEntityNum != ENTITYNUM_NONE) return;		// airborne only
	if (g_botSurfStrafe[bs->client]) return;				// surf owns the view this frame
	if (bs->enemy >= 0) return;								// let combat footwork drive

	speed = sqrt(ps->velocity[0] * ps->velocity[0] + ps->velocity[1] * ps->velocity[1]);
	if (speed < 320.0f) return;								// only worth it carrying real speed

	velYaw  = AngleMod((float)(atan2(ps->velocity[1], ps->velocity[0]) * 180.0 / M_PI));
	goalYaw = AngleMod(bs->ideal_viewangles[YAW]);			// the AAS nav heading (pre-commit)
	delta   = AngleSubtract(goalYaw, velYaw);				// how far velocity must turn toward the goal
	if (fabs(delta) < 8.0f) return;							// already on line — run straight, don't wobble
	if (fabs(delta) > 110.0f) return;						// near a reversal — a carve can't make that; leave nav

	phiOpt = PM_OptimalStrafeAngle(speed, pmove_msec.integer * 0.001f);
	if (phiOpt < 1.0f) return;

	// lead velocity by phi_opt toward the goal: that's the wishdir heading we want
	side    = (delta > 0.0f) ? 1 : -1;						// +1 = goal is CCW of velocity
	wishYaw = AngleMod(velYaw + side * phiOpt);

	// wishdir = the strafe direction. Strafe-right (+127) gives wishdir = yaw-90,
	// strafe-left (-127) gives wishdir = yaw+90 — and BOTH keys can produce the
	// same wishdir via opposite view yaws, so the carve physics is identical either
	// way. Pick the view that looks most along VELOCITY (natural strafe-jump head
	// position): the bot faces where it's travelling while the strafe key turns it,
	// instead of craning backward at wide turn angles.
	candR = AngleMod(wishYaw + 90.0f);						// view yaw if strafing right
	candL = AngleMod(wishYaw - 90.0f);						// view yaw if strafing left
	if (fabs(AngleSubtract(velYaw, candR)) <= fabs(AngleSubtract(velYaw, candL))) {
		bs->ideal_viewangles[YAW] = candR;
		g_botAirStrafe[bs->client] = 127;
	} else {
		bs->ideal_viewangles[YAW] = candL;
		g_botAirStrafe[bs->client] = -127;
	}
	// leave PITCH to the nav — only the flattened yaw drives the air-strafe wishdir
}

static void BotApplyMoveset(bot_state_t *bs) {
	usercmd_t		*cmd = &bs->lastucmd;
	playerState_t	*ps = &bs->cur_ps;
	float			speed;

	if (!bot_moveset.integer) return;
	if (ps->pm_type != PM_NORMAL) return;

	// SURF: BotSurfControl flagged us riding a steep face this frame — hold the
	// strafe into the bank, no forward, and DON'T jump (stay on to keep surfing)
	if (g_botSurfStrafe[bs->client]) {
		cmd->rightmove = g_botSurfStrafe[bs->client];
		cmd->forwardmove = 0;
		cmd->upmove = 0;			// stay on the face — don't pop off
		return;
	}

	// SWORD DUEL (g_botSwordOnly): cinematic slow-mo blade trading. Unlike the rail
	// peel below (which carries momentum PAST the enemy), close aggressively then
	// TRADE at blade range. Attacks + BotApplyDefense parries do the rest. Takes
	// priority over the combat-bhop peel whenever the field is pure melee.
	if (g_botSwordOnly.integer && bs->enemy >= 0) {
		aas_entityinfo_t	enemyinfo;
		vec3_t				d;
		float				edist;
		BotEntityInfo(bs->enemy, &enemyinfo);
		VectorSubtract(enemyinfo.origin, ps->origin, d);
		edist = VectorLength(d);
		if (edist < 220.0f) {
			// at blade range. Damage is SPEED-SCALED (a standing trade barely
			// scratches), so the orbit isn't just for show — the lateral speed is
			// what makes the cuts bite. The old bug was a CONSTANT one-way orbit at a
			// fixed radius (plus the view overshoot above): that reads as an endless
			// loop and never resolves. Keep the speed, break the loop: orbit but
			// REVERSE it on a timer, and every ~2.6s LUNGE straight through for a
			// committed speed cut, then resume. Reads as duelling footwork, not a ring.
			int		t = level.time + bs->client * 777;
			if ((t % 2600) < 520) {
				// committed pass — dash through, speed = damage
				cmd->upmove = 0;
				cmd->forwardmove = 127;
				cmd->rightmove = 0;
			} else {
				// hold the ring and circle, flipping direction so it's back-and-forth
				cmd->upmove = 0;
				cmd->forwardmove = (edist > 110.0f) ? 95 : 25;
				cmd->rightmove = ((t / 1500) & 1) ? 90 : -90;
			}
			g_botPass[bs->client] = 0;
			return;
		}
		if (edist < 3000.0f) {
			// not yet in reach: bhop straight in, aggressive close
			cmd->upmove = 127;
			cmd->forwardmove = 127;
			g_botPass[bs->client] = 0;
			return;
		}
	}

	// COMBAT AT SPEED (bot_combatBhop, arena/DM only). Not a standing gunfight and
	// not an in-place orbit (both slow) — bhop-RUSH: hold jump and drive forward
	// (the aim tracks the enemy, so forward = straight at it) to build speed on the
	// rehop chain, then PEEL through at point-blank with a committed strafe so the
	// momentum carries past for another fast pass. Cvar-gated because bots target
	// each other on the movement dojos too; only an arena/DM map turns it on.
	if (bot_combatBhop.integer && bs->enemy >= 0) {
		aas_entityinfo_t	enemyinfo;
		vec3_t				d;
		float				edist;

		BotEntityInfo(bs->enemy, &enemyinfo);
		VectorSubtract(enemyinfo.origin, ps->origin, d);
		edist = VectorLength(d);
		if (edist < 1500.0f) {
			cmd->upmove = 127;					// bhop — keep momentum
			if (cmd->forwardmove < 80) {
				cmd->forwardmove = 127;			// rush the target at speed
			}
			if (edist < 300.0f) {
				// point-blank: commit a peel and hold it so we pass clean through
				if (!g_botPass[bs->client]) {
					g_botPass[bs->client] = (level.time & 1024) ? 1 : -1;
				}
				cmd->rightmove = g_botPass[bs->client] * 110;
			} else {
				g_botPass[bs->client] = 0;		// broke off — next pass picks fresh
			}
			return;
		}
	}

	// APPROACH BRAKE: the moveset bhops flat-out, but settling onto an item / making a
	// tight nav turn needs precision — at full speed the bot sails past the goal and
	// clips geometry, then stalls recovering (the speed-crashing-to-0 we measured). So
	// when the actual destination (the AAS top goal) is close and we're carrying too
	// much speed to land on it, stop forcing the bhop: drop jump so the bot touches
	// down and ground friction scrubs the overshoot. Self-clearing — once the goal is
	// reached the top goal jumps far away and the bhop chain resumes.
	if (bot_brake.integer) {
		bot_goal_t	goal;
		if (trap_BotGetTopGoal(bs->gs, &goal)) {
			float	dx = goal.origin[0] - ps->origin[0];
			float	dy = goal.origin[1] - ps->origin[1];
			float	gdist = (float)sqrt(dx * dx + dy * dy);
			speed = (float)sqrt(ps->velocity[0] * ps->velocity[0] + ps->velocity[1] * ps->velocity[1]);
			if (gdist < 200.0f && speed > 400.0f) {
				cmd->upmove = 0;	// drop the bhop — land and let friction brake onto the goal
				return;
			}
		}
	}

	// AIR-STRAFE CARVE: BotAirStrafe (view phase) led the view phi_opt off velocity
	// toward the goal — hold pure strafe, no forward, so the high-accel A/D cap
	// regime builds speed while the carve curves velocity toward the goal. The air
	// jump stays on the same apex-pulse schedule as the generic chain below.
	if (g_botAirStrafe[bs->client] && ps->groundEntityNum == ENTITYNUM_NONE) {
		cmd->forwardmove = 0;
		cmd->rightmove = g_botAirStrafe[bs->client];
		if (ps->velocity[2] < 40 && ps->stats[STAT_AIRJUMP_COUNT] < 1) {
			cmd->upmove = (ps->pm_flags & PMF_JUMP_HELD) ? 0 : 127;	// pulse for a fresh wall/air jump
		} else {
			cmd->upmove = 127;
		}
		return;
	}

	// only while actually travelling, and never when the AI is deliberately
	// walking carefully (near ledges) or already crouch-sliding
	if (!cmd->forwardmove && !cmd->rightmove) return;
	if (cmd->buttons & BUTTON_WALKING) return;
	if (cmd->upmove < 0) return;

	speed = sqrt(ps->velocity[0] * ps->velocity[0]
		+ ps->velocity[1] * ps->velocity[1]);
	if (speed < 200) return;	// too slow to bother (and avoids twitchy hops)

	if (ps->groundEntityNum != ENTITYNUM_NONE) {
		// bunny hop: hold jump so the chain rehops the instant we land
		cmd->upmove = 127;
	} else if (ps->velocity[2] < 40 && ps->stats[STAT_AIRJUMP_COUNT] < 1) {
		// at/past the apex with the air jump still in hand: pulse jump for a
		// fresh press (wall jump if a wall is there, else double jump)
		cmd->upmove = (ps->pm_flags & PMF_JUMP_HELD) ? 0 : 127;
	} else {
		// otherwise keep jump held so the landing rehop continues the chain
		cmd->upmove = 127;
	}
}

/*
==============
BotApplyDash

STRAFE 64: let bots use the SHIFT dash (BUTTON_DASH). The dash itself (G_ClientDash)
bursts along the bot's motion and REVECTORS toward a nearby enemy / slice-gate, so
here we only decide WHEN to tap it: closing on an enemy to connect a slice, or to
extend a fast forward leap across a gap. G_ClientDash wants a FRESH press on its own
650ms cooldown, so we pulse a single isolated frame spaced just past that, tracked
per-bot in g_botDashTime. Buttons are rebuilt fresh each frame (BotInputToUserCommand),
so a lone set frame is naturally surrounded by clear ones — a clean press. Touches only
the button; the moveset keeps driving the feet. Runs after BotApplyMoveset so the dash
reads the finalised forward/strafe steer it lunges along.
==============
*/
static int g_botDashTime[MAX_CLIENTS];

static void BotApplyDash(bot_state_t *bs) {
	usercmd_t		*cmd = &bs->lastucmd;
	playerState_t	*ps = &bs->cur_ps;
	float			speed;
	qboolean		want = qfalse;

	if (!bot_dash.integer) return;
	if (!bot_moveset.integer) return;			// shares the master moveset switch
	if (ps->pm_type != PM_NORMAL) return;
	if (g_botSurfStrafe[bs->client]) return;	// never dash off a surf face
	if (cmd->buttons & BUTTON_WALKING) return;	// careful walking near ledges
	if (cmd->forwardmove <= 0) return;			// dash lunges along forward steer; need it
	if (level.time < g_botDashTime[bs->client]) return;

	speed = sqrt(ps->velocity[0] * ps->velocity[0]
		+ ps->velocity[1] * ps->velocity[1]);

	// COMBAT / FLOW-GATE CLOSE: an enemy a dash-length ahead (but not already at blade
	// range, where a dash would overshoot) — tap to lunge onto the kill-line; the dash
	// homing carries the rest. Drives the sword duel and the arena bhop-rush.
	if (bs->enemy >= 0) {
		aas_entityinfo_t	enemyinfo;
		vec3_t				d;
		float				edist;

		BotEntityInfo(bs->enemy, &enemyinfo);
		VectorSubtract(enemyinfo.origin, ps->origin, d);
		edist = VectorLength(d);
		if (edist > 200.0f && edist < 700.0f) {		// ~DASH_RANGE, past blade reach
			want = qtrue;
		}
	}

	// MOVEMENT: extend a fast forward leap. Airborne, at/past the apex and moving
	// quick — the dash carries the jump further (and revectors toward a slice-gate in
	// the cone if one's there), reading as a deliberate gap-dash rather than a lurch
	// that would wreck ground navigation.
	if (!want && ps->groundEntityNum == ENTITYNUM_NONE
			&& ps->velocity[2] < 60.0f && speed > 450.0f) {
		want = qtrue;
	}

	if (!want) return;

	cmd->buttons |= BUTTON_DASH;
	g_botDashTime[bs->client] = level.time + 800;	// just past DASH_COOLDOWN (650ms)
}

/*
==============
BotApplySlide

STRAFE 64: bots crouch-slide. A slide is duck (upmove<0) held on the GROUND at speed
(>pm_slideMinSpeed) — slick, low, momentum-locked. Weaving slidehops into the hop chain
took two corrections the obvious version missed:

  1. Bhopping bots are airborne ~90% of the time, so a fixed wall-clock "crouch window"
     mostly landed the bot late and gave a single frame of slide — slide% measured 0.
  2. So instead we COMMIT: the moment the bot catches a grounded, fast frame (off a hop
     landing) we latch a real slide DURATION and hold crouch for all of it — the crouch
     suppresses the moveset's rehop, so the bot genuinely rides the slide, then pops back
     into the bhop chain. A per-bot cooldown spaces the slidehops out.

Don't gate on forward/strafe axes: a bhop chain air-strafes constantly (forwardmove can
be ~0, rightmove large), and the slide is momentum-locked anyway — it follows velocity,
not the move keys — so those gates fired ~never. Runs after BotApplyMoveset and OVERRIDES
its upmove.
==============
*/
#define BOT_SLIDE_HOLD	380		// ms to RIDE a slide once it engages on the ground
#define BOT_SLIDE_ARM	300		// ms to stay crouched (descending) waiting to land into one
#define BOT_SLIDE_GAP	1500	// ms of bhop between slidehops

static int g_botSlideUntil[MAX_CLIENTS];	// level.time we keep RIDING (grounded slide) until
static int g_botSlideArm[MAX_CLIENTS];		// level.time we keep ARMING (crouch in air) until
static int g_botSlideNext[MAX_CLIENTS];		// earliest level.time the next slidehop may start

static void BotApplySlide(bot_state_t *bs) {
	usercmd_t		*cmd = &bs->lastucmd;
	playerState_t	*ps = &bs->cur_ps;
	int				client = bs->client;
	float			speed;

	if (!bot_slide.integer || !bot_moveset.integer || ps->pm_type != PM_NORMAL
			|| g_botSurfStrafe[client] || (cmd->buttons & BUTTON_WALKING)) {
		g_botSlideUntil[client] = 0;
		g_botSlideArm[client] = 0;
		return;
	}

	// RIDING: a slide engaged on the ground — hold the crouch so it LASTS (the crouch
	// also suppresses the moveset's rehop, so the bot stays down and rides it).
	if (level.time < g_botSlideUntil[client]) {
		cmd->upmove = -127;
		return;
	}

	// ARMING: crouch THROUGH the air waiting to touch down — bhopping bots are airborne
	// ~90% of the time, so requiring a grounded trigger frame caught almost nothing. The
	// moment we land into an actual slide, latch the ride and start the cooldown.
	if (level.time < g_botSlideArm[client]) {
		cmd->upmove = -127;
		if (ps->groundEntityNum != ENTITYNUM_NONE && (ps->pm_flags & PMF_SLIDING)) {
			g_botSlideUntil[client] = level.time + BOT_SLIDE_HOLD;
			g_botSlideArm[client]   = 0;
			g_botSlideNext[client]  = g_botSlideUntil[client] + BOT_SLIDE_GAP;
		}
		return;
	}

	if (level.time < g_botSlideNext[client]) return;	// cooldown — bhop between slides

	// not in a close duel — let the combat footwork (orbit / committed pass) run
	if (bs->enemy >= 0) {
		aas_entityinfo_t	enemyinfo;
		vec3_t				d;
		BotEntityInfo(bs->enemy, &enemyinfo);
		VectorSubtract(enemyinfo.origin, ps->origin, d);
		if (VectorLength(d) < 320.0f) return;
	}

	speed = sqrt(ps->velocity[0] * ps->velocity[0]
		+ ps->velocity[1] * ps->velocity[1]);
	if (speed < 340.0f) return;					// fast enough that a slide reads and out-glides a run

	// only arm when GROUNDED (slide at once) or already DESCENDING — never crouch on the
	// way UP, which would kill the bhop's rising air-strafe accel (that tanked speed-map flow)
	if (ps->groundEntityNum == ENTITYNUM_NONE && ps->velocity[2] > -40.0f) return;

	// arm it: start crouching now (slides at once if already grounded, else lands into one).
	// Re-arm budget is short so a long airtime that never lands doesn't crouch-spam forever.
	g_botSlideArm[client]  = level.time + BOT_SLIDE_ARM;
	g_botSlideNext[client] = level.time + BOT_SLIDE_ARM + BOT_SLIDE_GAP;	// floor; tightened on a real ride
	cmd->upmove = -127;
}

/*
==============
BotApplyUnstick

STRAFE 64: bots sometimes grind or circle against a wall when the nav path hugs
geometry. Detect it — commanding movement but barely moving for a sustained beat — and
break out with a ninja wall-kick: pulse CLEAN jumps (fresh airborne presses) so
PM_CheckWallJump, which probes every side for the wall itself, kicks us off it, plus a
hard strafe (flipping sides on a slow timer) to break the circle and feed tangential
speed. Hands straight back to the nav the instant real speed returns. Runs LAST so it
has final say on a stuck bot's feet.
==============
*/
static int g_botStuckSince[MAX_CLIENTS];

static void BotApplyUnstick(bot_state_t *bs) {
	usercmd_t		*cmd = &bs->lastucmd;
	playerState_t	*ps = &bs->cur_ps;
	float			speed;
	int				client = bs->client;
	int				jphase, side;

	if (!bot_unstick.integer || !bot_moveset.integer
			|| ps->pm_type != PM_NORMAL || g_botSurfStrafe[client]) {
		g_botStuckSince[client] = 0;
		return;
	}
	// only when the bot is actually trying to go somewhere
	if (!cmd->forwardmove && !cmd->rightmove) {
		g_botStuckSince[client] = 0;
		return;
	}
	// a close enemy means low speed is DELIBERATE duel footwork (orbit / blade trade),
	// not stuck — don't hijack the fight with an escape hop. Beyond melee range a real
	// stall is still a stall, so only suppress when the enemy is genuinely close.
	if (bs->enemy >= 0) {
		aas_entityinfo_t	enemyinfo;
		vec3_t				d;
		BotEntityInfo(bs->enemy, &enemyinfo);
		VectorSubtract(enemyinfo.origin, ps->origin, d);
		if (VectorLength(d) < 600.0f) {
			g_botStuckSince[client] = 0;
			return;
		}
	}

	speed = sqrt(ps->velocity[0] * ps->velocity[0]
		+ ps->velocity[1] * ps->velocity[1]);
	if (speed > 150.0f) {						// moving fine — not stuck
		g_botStuckSince[client] = 0;
		return;
	}

	if (!g_botStuckSince[client]) {
		g_botStuckSince[client] = level.time;
	}
	if (level.time - g_botStuckSince[client] < 350) {
		return;									// brief stalls (starts, tight corners) are normal
	}

	// grinding against geometry — ninja-kick out
	jphase = (level.time / 120) & 1;
	cmd->upmove = jphase ? 127 : 0;				// clean pulse so the airborne wall-jump can fire
	side = ((level.time + client * 333) / 600) & 1;
	cmd->rightmove = side ? 127 : -127;			// strafe out, flipping so a corner can't lock us in
	if (cmd->forwardmove > 40) {
		cmd->forwardmove = 40;					// stop ramming straight into the wall
	}
}

/*
==============
BotApplyDefense

Teach bots to GUARD and PARRY with the katana. The guard (EF_BLOCKING) only raises
with the sword up and only parries FRONTAL melee — so we gate on the weapon and on
the bot actually facing its enemy (its aim usually is, but it peels off mid-pass).
Holding block also suppresses the bot's own swing, so it can't just turtle. Two
ways in, both skill-gated so weak bots stay beatable and aces feel like duelists:

  REACTIVE PARRY — the enemy is mid-cut (sword/gauntlet alternates TORSO_ATTACK/
  ATTACK2). Roll the read ONCE per swing and, if it lands, raise the guard to clash
  the blow. A clean sword-vs-sword parry deals zero damage and shoves the attacker
  back, flipping the duel.

  ANTICIPATORY GUARD — nose-to-nose with the enemy and no swing yet: raise the
  guard in BURSTS (a duty cycle) so a clash lands on a raised blade, dropping it in
  the gaps to riposte. Better bots spend more of the cycle on guard.

Runs after BotApplyMoveset, touching only buttons — the bhop/surf moveset keeps
driving the bot's feet underneath the guard.
==============
*/
static void BotApplyDefense(bot_state_t *bs) {
	usercmd_t			*cmd = &bs->lastucmd;
	playerState_t		*ps = &bs->cur_ps;
	aas_entityinfo_t	enemyinfo;
	vec3_t				dir, fwd, to;
	float				dist, skill, chance;
	int					eanim, key;

	if (!bot_swordBlock.integer) return;
	if (ps->pm_type != PM_NORMAL) return;
	if (ps->weapon != WP_SWORD) return;			// guard only raises with the katana
	if (bs->enemy < 0) return;

	BotEntityInfo(bs->enemy, &enemyinfo);
	if (!enemyinfo.valid) return;

	VectorSubtract(enemyinfo.origin, ps->origin, dir);
	dist = VectorLength(dir);
	if (dist > 260.0f) return;					// out of blade reach — keep pressing

	// FRONTAL only: the guard parries frontal hits, so don't raise it unless we're
	// actually looking at the enemy
	AngleVectors(ps->viewangles, fwd, NULL, NULL);
	VectorCopy(dir, to);
	if (VectorNormalize(to) <= 0.0f || DotProduct(fwd, to) < 0.30f) return;

	skill = trap_Characteristic_BFloat(bs->character, CHARACTERISTIC_ATTACK_SKILL, 0, 1);
	eanim = enemyinfo.torsoAnim & ~ANIM_TOGGLEBIT;

	if (eanim == TORSO_ATTACK || eanim == TORSO_ATTACK2) {
		// REACTIVE: roll the skill gate ONCE per swing (key = the raw torsoAnim,
		// which flips its toggle bit / alternates ATTACK<->ATTACK2 each new swing)
		key = enemyinfo.torsoAnim;
		if (key != g_botBlockKey[bs->client]) {
			g_botBlockKey[bs->client] = key;
			chance = 0.35f + 0.60f * skill;		// ~35% reads at skill 0 → ~95% at skill 1
			g_botBlockRoll[bs->client] = (random() <= chance) ? 1 : 0;
		}
		if (!g_botBlockRoll[bs->client]) return;	// missed the read this swing
	} else if (dist < 150.0f && ps->weaponTime <= 0) {
		// ANTICIPATORY: point-blank, no swing from either of us yet — flick the guard
		// up briefly so a sudden cut lands on a raised blade, but stay mostly on the
		// ATTACK so the duel doesn't stall into a turtle standoff. Don't interrupt our
		// own swing (weaponTime>0). Phase-offset per client so bots don't lockstep.
		int		period = 520;
		float	frac = 0.10f + 0.20f * skill;	// only 10%..30% of the cycle on guard
		int		phase = (level.time + bs->client * 137) % period;
		if (phase >= (int)(frac * period)) return;	// riposte window — blade down, swing
	} else {
		return;									// in reach but not swinging and not point-blank
	}

	cmd->buttons |= BUTTON_BLOCK;				// raise the blade
	cmd->buttons &= ~BUTTON_ATTACK;				// (a raised guard suppresses the swing anyway)

	if (bot_swordBlock.integer >= 2) {			// debug: confirm the guard path fires
		BotAI_Print(PRT_MESSAGE, "GUARD: bot %d vs enemy %d (dist %.0f, %s)\n",
			bs->client, bs->enemy, dist,
			(eanim == TORSO_ATTACK || eanim == TORSO_ATTACK2) ? "parry" : "anticip");
	}
}

/*
==============
BotUpdateInput
==============
*/
void BotUpdateInput(bot_state_t *bs, int time, int elapsed_time) {
	bot_input_t bi;
	int j;

	//add the delta angles to the bot's current view angles
	for (j = 0; j < 3; j++) {
		bs->viewangles[j] = AngleMod(bs->viewangles[j] + SHORT2ANGLE(bs->cur_ps.delta_angles[j]));
	}
	//STRAFE 64: surf control steers the view along the ramp before it's committed
	BotSurfControl(bs);
	//STRAFE 64: air-strafe carve leads the view to gain speed toward the goal (after
	//surf, which has first claim on the view; bails itself if surf took the wheel)
	BotAirStrafe(bs);
	//change the bot view angles
	BotChangeViewAngles(bs, (float) elapsed_time / 1000);
	//retrieve the bot input
	trap_EA_GetInput(bs->client, (float) time / 1000, &bi);
	//respawn hack
	if (bi.actionflags & ACTION_RESPAWN) {
		if (bs->lastucmd.buttons & BUTTON_ATTACK) bi.actionflags &= ~(ACTION_RESPAWN|ACTION_ATTACK);
	}
	//convert the bot input to a usercmd
	BotInputToUserCommand(&bi, &bs->lastucmd, bs->cur_ps.delta_angles, time);
	//STRAFE 64: augment the command so bots use the new moveset
	BotApplyMoveset(bs);
	//STRAFE 64: weave crouch-slidehops into the hop chain on fast, straight runs
	BotApplySlide(bs);
	//STRAFE 64: tap the SHIFT dash to close on enemies / extend gap-leaps
	BotApplyDash(bs);
	//STRAFE 64: raise the katana guard to parry an incoming swing
	BotApplyDefense(bs);
	//STRAFE 64: break out of wall grinding/circling — runs last (final say on the feet)
	BotApplyUnstick(bs);
	//subtract the delta angles
	for (j = 0; j < 3; j++) {
		bs->viewangles[j] = AngleMod(bs->viewangles[j] - SHORT2ANGLE(bs->cur_ps.delta_angles[j]));
	}
}

/*
==============
BotAIRegularUpdate
==============
*/
void BotAIRegularUpdate(void) {
	if (regularupdate_time < FloatTime()) {
		trap_BotUpdateEntityItems();
		regularupdate_time = FloatTime() + 0.3;
	}
}

/*
==============
RemoveColorEscapeSequences
==============
*/
void RemoveColorEscapeSequences( char *text ) {
	int i, l;

	l = 0;
	for ( i = 0; text[i]; i++ ) {
		if (Q_IsColorString(&text[i])) {
			i++;
			continue;
		}
		if (text[i] > 0x7E)
			continue;
		text[l++] = text[i];
	}
	text[l] = '\0';
}

/*
==============
BotAI
==============
*/
int BotAI(int client, float thinktime) {
	bot_state_t *bs;
	char buf[1024], *args;
	int j;

	trap_EA_ResetInput(client);
	//
	bs = botstates[client];
	if (!bs || !bs->inuse) {
		BotAI_Print(PRT_FATAL, "BotAI: client %d is not setup\n", client);
		return qfalse;
	}

	//retrieve the current client state
	if (!BotAI_GetClientState(client, &bs->cur_ps)) {
		BotAI_Print(PRT_FATAL, "BotAI: failed to get player state for player %d\n", client);
		return qfalse;
	}
	//retrieve any waiting server commands
	while( trap_BotGetServerCommand(client, buf, sizeof(buf)) ) {
		//have buf point to the command and args to the command arguments
		args = strchr( buf, ' ');
		if (!args) continue;
		*args++ = '\0';

		//remove color espace sequences from the arguments
		RemoveColorEscapeSequences( args );

		if (!Q_stricmp(buf, "cp "))
			{ /*CenterPrintf*/ }
		else if (!Q_stricmp(buf, "cs"))
			{ /*ConfigStringModified*/ }
		else if (!Q_stricmp(buf, "print")) {
			//remove first and last quote from the chat message
			memmove(args, args+1, strlen(args));
			args[strlen(args)-1] = '\0';
			trap_BotQueueConsoleMessage(bs->cs, CMS_NORMAL, args);
		}
		else if (!Q_stricmp(buf, "chat")) {
			//remove first and last quote from the chat message
			memmove(args, args+1, strlen(args));
			args[strlen(args)-1] = '\0';
			trap_BotQueueConsoleMessage(bs->cs, CMS_CHAT, args);
		}
		else if (!Q_stricmp(buf, "tchat")) {
			//remove first and last quote from the chat message
			memmove(args, args+1, strlen(args));
			args[strlen(args)-1] = '\0';
			trap_BotQueueConsoleMessage(bs->cs, CMS_CHAT, args);
		}
#ifdef MISSIONPACK
		else if (!Q_stricmp(buf, "vchat")) {
			BotVoiceChatCommand(bs, SAY_ALL, args);
		}
		else if (!Q_stricmp(buf, "vtchat")) {
			BotVoiceChatCommand(bs, SAY_TEAM, args);
		}
		else if (!Q_stricmp(buf, "vtell")) {
			BotVoiceChatCommand(bs, SAY_TELL, args);
		}
#endif
		else if (!Q_stricmp(buf, "scores"))
			{ /*FIXME: parse scores?*/ }
		else if (!Q_stricmp(buf, "clientLevelShot"))
			{ /*ignore*/ }
	}
	//add the delta angles to the bot's current view angles
	for (j = 0; j < 3; j++) {
		bs->viewangles[j] = AngleMod(bs->viewangles[j] + SHORT2ANGLE(bs->cur_ps.delta_angles[j]));
	}
	//increase the local time of the bot
	bs->ltime += thinktime;
	//
	bs->thinktime = thinktime;
	//origin of the bot
	VectorCopy(bs->cur_ps.origin, bs->origin);
	//eye coordinates of the bot
	VectorCopy(bs->cur_ps.origin, bs->eye);
	bs->eye[2] += bs->cur_ps.viewheight;
	//get the area the bot is in
	bs->areanum = BotPointAreaNum(bs->origin);
	//the real AI
	BotDeathmatchAI(bs, thinktime);
	//set the weapon selection every AI frame
	trap_EA_SelectWeapon(bs->client, bs->weaponnum);
	//subtract the delta angles
	for (j = 0; j < 3; j++) {
		bs->viewangles[j] = AngleMod(bs->viewangles[j] - SHORT2ANGLE(bs->cur_ps.delta_angles[j]));
	}
	//everything was ok
	return qtrue;
}

/*
==================
BotScheduleBotThink
==================
*/
void BotScheduleBotThink(void) {
	int i, botnum;

	botnum = 0;

	for( i = 0; i < MAX_CLIENTS; i++ ) {
		if( !botstates[i] || !botstates[i]->inuse ) {
			continue;
		}
		//initialize the bot think residual time
		botstates[i]->botthink_residual = bot_thinktime.integer * botnum / numbots;
		botnum++;
	}
}

/*
==============
BotWriteSessionData
==============
*/
void BotWriteSessionData(bot_state_t *bs) {
	const char	*s;
	const char	*var;

	s = va(
			"%i %i %i %i %i %i %i %i"
			" %f %f %f"
			" %f %f %f"
			" %f %f %f"
			" %f",
		bs->lastgoal_decisionmaker,
		bs->lastgoal_ltgtype,
		bs->lastgoal_teammate,
		bs->lastgoal_teamgoal.areanum,
		bs->lastgoal_teamgoal.entitynum,
		bs->lastgoal_teamgoal.flags,
		bs->lastgoal_teamgoal.iteminfo,
		bs->lastgoal_teamgoal.number,
		bs->lastgoal_teamgoal.origin[0],
		bs->lastgoal_teamgoal.origin[1],
		bs->lastgoal_teamgoal.origin[2],
		bs->lastgoal_teamgoal.mins[0],
		bs->lastgoal_teamgoal.mins[1],
		bs->lastgoal_teamgoal.mins[2],
		bs->lastgoal_teamgoal.maxs[0],
		bs->lastgoal_teamgoal.maxs[1],
		bs->lastgoal_teamgoal.maxs[2],
		bs->formation_dist
		);

	var = va( "botsession%i", bs->client );

	trap_Cvar_Set( var, s );
}

/*
==============
BotReadSessionData
==============
*/
void BotReadSessionData(bot_state_t *bs) {
	char	s[MAX_STRING_CHARS];
	const char	*var;

	var = va( "botsession%i", bs->client );
	trap_Cvar_VariableStringBuffer( var, s, sizeof(s) );

	sscanf(s,
			"%i %i %i %i %i %i %i %i"
			" %f %f %f"
			" %f %f %f"
			" %f %f %f"
			" %f",
		&bs->lastgoal_decisionmaker,
		&bs->lastgoal_ltgtype,
		&bs->lastgoal_teammate,
		&bs->lastgoal_teamgoal.areanum,
		&bs->lastgoal_teamgoal.entitynum,
		&bs->lastgoal_teamgoal.flags,
		&bs->lastgoal_teamgoal.iteminfo,
		&bs->lastgoal_teamgoal.number,
		&bs->lastgoal_teamgoal.origin[0],
		&bs->lastgoal_teamgoal.origin[1],
		&bs->lastgoal_teamgoal.origin[2],
		&bs->lastgoal_teamgoal.mins[0],
		&bs->lastgoal_teamgoal.mins[1],
		&bs->lastgoal_teamgoal.mins[2],
		&bs->lastgoal_teamgoal.maxs[0],
		&bs->lastgoal_teamgoal.maxs[1],
		&bs->lastgoal_teamgoal.maxs[2],
		&bs->formation_dist
		);
}

/*
==============
BotAISetupClient
==============
*/
int BotAISetupClient(int client, struct bot_settings_s *settings, qboolean restart) {
	char filename[144], name[144], gender[144];
	bot_state_t *bs;
	int errnum;

	if (!botstates[client]) botstates[client] = G_Alloc(sizeof(bot_state_t));
	bs = botstates[client];

	if (!bs) {
		return qfalse;
	}

	if (bs && bs->inuse) {
		BotAI_Print(PRT_FATAL, "BotAISetupClient: client %d already setup\n", client);
		return qfalse;
	}

	if (!trap_AAS_Initialized()) {
		// Throttle: a batch addbot on a map with no .aas (e.g. a forged map
		// whose bspc pass failed) would otherwise print this once per bot every
		// attempt. Collapse to one informative line every few seconds.
		static int lastAASWarn;
		if (!lastAASWarn || level.time - lastAASWarn > 5000 || level.time < lastAASWarn) {
			BotAI_Print(PRT_FATAL, "AAS not initialized (map has no .aas — bots disabled)\n");
			lastAASWarn = level.time;
		}
		return qfalse;
	}

	//load the bot character
	bs->character = trap_BotLoadCharacter(settings->characterfile, settings->skill);
	if (!bs->character) {
		BotAI_Print(PRT_FATAL, "couldn't load skill %f from %s\n", settings->skill, settings->characterfile);
		return qfalse;
	}
	//copy the settings
	memcpy(&bs->settings, settings, sizeof(bot_settings_t));
	//allocate a goal state
	bs->gs = trap_BotAllocGoalState(client);
	//load the item weights
	trap_Characteristic_String(bs->character, CHARACTERISTIC_ITEMWEIGHTS, filename, sizeof(filename));
	errnum = trap_BotLoadItemWeights(bs->gs, filename);
	if (errnum != BLERR_NOERROR) {
		trap_BotFreeGoalState(bs->gs);
		return qfalse;
	}
	//allocate a weapon state
	bs->ws = trap_BotAllocWeaponState();
	//load the weapon weights
	trap_Characteristic_String(bs->character, CHARACTERISTIC_WEAPONWEIGHTS, filename, sizeof(filename));
	errnum = trap_BotLoadWeaponWeights(bs->ws, filename);
	if (errnum != BLERR_NOERROR) {
		trap_BotFreeGoalState(bs->gs);
		trap_BotFreeWeaponState(bs->ws);
		return qfalse;
	}
	//allocate a chat state
	bs->cs = trap_BotAllocChatState();
	//load the chat file
	trap_Characteristic_String(bs->character, CHARACTERISTIC_CHAT_FILE, filename, sizeof(filename));
	trap_Characteristic_String(bs->character, CHARACTERISTIC_CHAT_NAME, name, sizeof(name));
	errnum = trap_BotLoadChatFile(bs->cs, filename, name);
	if (errnum != BLERR_NOERROR) {
		trap_BotFreeChatState(bs->cs);
		trap_BotFreeGoalState(bs->gs);
		trap_BotFreeWeaponState(bs->ws);
		return qfalse;
	}
	//get the gender characteristic
	trap_Characteristic_String(bs->character, CHARACTERISTIC_GENDER, gender, sizeof(gender));
	//set the chat gender
	if (*gender == 'f' || *gender == 'F') trap_BotSetChatGender(bs->cs, CHAT_GENDERFEMALE);
	else if (*gender == 'm' || *gender == 'M') trap_BotSetChatGender(bs->cs, CHAT_GENDERMALE);
	else trap_BotSetChatGender(bs->cs, CHAT_GENDERLESS);

	bs->inuse = qtrue;
	bs->client = client;
	bs->entitynum = client;
	bs->setupcount = 4;
	bs->entergame_time = FloatTime();
	bs->ms = trap_BotAllocMoveState();
	bs->walker = trap_Characteristic_BFloat(bs->character, CHARACTERISTIC_WALKER, 0, 1);
	numbots++;

	if (trap_Cvar_VariableIntegerValue("bot_testichat")) {
		trap_BotLibVarSet("bot_testichat", "1");
		BotChatTest(bs);
	}
	//NOTE: reschedule the bot thinking
	BotScheduleBotThink();
	//if interbreeding start with a mutation
	if (bot_interbreed) {
		trap_BotMutateGoalFuzzyLogic(bs->gs, 1);
	}
	// if we kept the bot client
	if (restart) {
		BotReadSessionData(bs);
	}
	//bot has been setup successfully
	return qtrue;
}

/*
==============
BotAIShutdownClient
==============
*/
int BotAIShutdownClient(int client, qboolean restart) {
	bot_state_t *bs;

	bs = botstates[client];
	if (!bs || !bs->inuse) {
		//BotAI_Print(PRT_ERROR, "BotAIShutdownClient: client %d already shutdown\n", client);
		return qfalse;
	}

	if (restart) {
		BotWriteSessionData(bs);
	}

	if (BotChat_ExitGame(bs)) {
		trap_BotEnterChat(bs->cs, bs->client, CHAT_ALL);
	}

	trap_BotFreeMoveState(bs->ms);
	//free the goal state
	trap_BotFreeGoalState(bs->gs);
	//free the chat file
	trap_BotFreeChatState(bs->cs);
	//free the weapon weights
	trap_BotFreeWeaponState(bs->ws);
	//free the bot character
	trap_BotFreeCharacter(bs->character);
	//
	BotFreeWaypoints(bs->checkpoints);
	BotFreeWaypoints(bs->patrolpoints);
	//clear activate goal stack
	BotClearActivateGoalStack(bs);
	//clear the bot state
	memset(bs, 0, sizeof(bot_state_t));
	//set the inuse flag to qfalse
	bs->inuse = qfalse;
	//there's one bot less
	numbots--;
	//everything went ok
	return qtrue;
}

/*
==============
BotResetState

called when a bot enters the intermission or observer mode and
when the level is changed
==============
*/
void BotResetState(bot_state_t *bs) {
	int client, entitynum, inuse;
	int movestate, goalstate, chatstate, weaponstate;
	bot_settings_t settings;
	int character;
	playerState_t ps;							//current player state
	float entergame_time;

	//save some things that should not be reset here
	memcpy(&settings, &bs->settings, sizeof(bot_settings_t));
	memcpy(&ps, &bs->cur_ps, sizeof(playerState_t));
	inuse = bs->inuse;
	client = bs->client;
	entitynum = bs->entitynum;
	character = bs->character;
	movestate = bs->ms;
	goalstate = bs->gs;
	chatstate = bs->cs;
	weaponstate = bs->ws;
	entergame_time = bs->entergame_time;
	//free checkpoints and patrol points
	BotFreeWaypoints(bs->checkpoints);
	BotFreeWaypoints(bs->patrolpoints);
	//reset the whole state
	memset(bs, 0, sizeof(bot_state_t));
	//copy back some state stuff that should not be reset
	bs->ms = movestate;
	bs->gs = goalstate;
	bs->cs = chatstate;
	bs->ws = weaponstate;
	memcpy(&bs->cur_ps, &ps, sizeof(playerState_t));
	memcpy(&bs->settings, &settings, sizeof(bot_settings_t));
	bs->inuse = inuse;
	bs->client = client;
	bs->entitynum = entitynum;
	bs->character = character;
	bs->entergame_time = entergame_time;
	//reset several states
	if (bs->ms) trap_BotResetMoveState(bs->ms);
	if (bs->gs) trap_BotResetGoalState(bs->gs);
	if (bs->ws) trap_BotResetWeaponState(bs->ws);
	if (bs->gs) trap_BotResetAvoidGoals(bs->gs);
	if (bs->ms) trap_BotResetAvoidReach(bs->ms);
}

/*
==============
BotAILoadMap
==============
*/
int BotAILoadMap( int restart ) {
	int			i;
	vmCvar_t	mapname;

	if (!restart) {
		trap_Cvar_Register( &mapname, "mapname", "", CVAR_SERVERINFO | CVAR_ROM );
		trap_BotLibLoadMap( mapname.string );
	}

	for (i = 0; i < MAX_CLIENTS; i++) {
		if (botstates[i] && botstates[i]->inuse) {
			BotResetState( botstates[i] );
			botstates[i]->setupcount = 4;
		}
	}

	BotSetupDeathmatchAI();

	return qtrue;
}

#ifdef MISSIONPACK
void ProximityMine_Trigger( gentity_t *trigger, gentity_t *other, trace_t *trace );
#endif

/*
==================
BotAIStartFrame
==================
*/
int BotAIStartFrame(int time) {
	int i;
	gentity_t	*ent;
	bot_entitystate_t state;
	int elapsed_time, thinktime;
	static int local_time;
	static int botlib_residual;
	static int lastbotthink_time;

	G_CheckBotSpawn();

	trap_Cvar_Update(&bot_rocketjump);
	trap_Cvar_Update(&bot_grapple);
	trap_Cvar_Update(&bot_fastchat);
	trap_Cvar_Update(&bot_nochat);
	trap_Cvar_Update(&bot_testrchat);
	trap_Cvar_Update(&bot_thinktime);
	trap_Cvar_Update(&bot_memorydump);
	trap_Cvar_Update(&bot_saveroutingcache);
	trap_Cvar_Update(&bot_pause);
	trap_Cvar_Update(&bot_report);
	trap_Cvar_Update(&bot_moveset);			// STRAFE 64: live-tunable bot kit cvars
	trap_Cvar_Update(&bot_combatBhop);
	trap_Cvar_Update(&bot_swordBlock);
	trap_Cvar_Update(&bot_dash);
	trap_Cvar_Update(&bot_slide);
	trap_Cvar_Update(&bot_unstick);
	trap_Cvar_Update(&bot_airStrafe);
	trap_Cvar_Update(&bot_brake);

	if (bot_report.integer) {
//		BotTeamplayReport();
//		trap_Cvar_Set("bot_report", "0");
		BotUpdateInfoConfigStrings();
	}

	if (bot_pause.integer) {
		// execute bot user commands every frame
		for( i = 0; i < MAX_CLIENTS; i++ ) {
			if( !botstates[i] || !botstates[i]->inuse ) {
				continue;
			}
			if( g_entities[i].client->pers.connected != CON_CONNECTED ) {
				continue;
			}
			botstates[i]->lastucmd.forwardmove = 0;
			botstates[i]->lastucmd.rightmove = 0;
			botstates[i]->lastucmd.upmove = 0;
			botstates[i]->lastucmd.buttons = 0;
			botstates[i]->lastucmd.serverTime = time;
			trap_BotUserCommand(botstates[i]->client, &botstates[i]->lastucmd);
		}
		return qtrue;
	}

	if (bot_memorydump.integer) {
		trap_BotLibVarSet("memorydump", "1");
		trap_Cvar_Set("bot_memorydump", "0");
	}
	if (bot_saveroutingcache.integer) {
		trap_BotLibVarSet("saveroutingcache", "1");
		trap_Cvar_Set("bot_saveroutingcache", "0");
	}
	//check if bot interbreeding is activated
	BotInterbreeding();
	//cap the bot think time
	if (bot_thinktime.integer > 200) {
		trap_Cvar_Set("bot_thinktime", "200");
	}
	//if the bot think time changed we should reschedule the bots
	if (bot_thinktime.integer != lastbotthink_time) {
		lastbotthink_time = bot_thinktime.integer;
		BotScheduleBotThink();
	}

	elapsed_time = time - local_time;
	local_time = time;

	botlib_residual += elapsed_time;

	if (elapsed_time > bot_thinktime.integer) thinktime = elapsed_time;
	else thinktime = bot_thinktime.integer;

	// update the bot library
	if ( botlib_residual >= thinktime ) {
		botlib_residual -= thinktime;

		trap_BotLibStartFrame((float) time / 1000);

		if (!trap_AAS_Initialized()) return qfalse;

		//update entities in the botlib
		for (i = 0; i < MAX_GENTITIES; i++) {
			ent = &g_entities[i];
			if (!ent->inuse) {
				trap_BotLibUpdateEntity(i, NULL);
				continue;
			}
			if (!ent->r.linked) {
				trap_BotLibUpdateEntity(i, NULL);
				continue;
			}
			if (ent->r.svFlags & SVF_NOCLIENT) {
				trap_BotLibUpdateEntity(i, NULL);
				continue;
			}
			// do not update missiles
			if (ent->s.eType == ET_MISSILE && ent->s.weapon != WP_GRAPPLING_HOOK) {
				trap_BotLibUpdateEntity(i, NULL);
				continue;
			}
			// do not update event only entities
			if (ent->s.eType > ET_EVENTS) {
				trap_BotLibUpdateEntity(i, NULL);
				continue;
			}
#ifdef MISSIONPACK
			// never link prox mine triggers
			if (ent->r.contents == CONTENTS_TRIGGER) {
				if (ent->touch == ProximityMine_Trigger) {
					trap_BotLibUpdateEntity(i, NULL);
					continue;
				}
			}
#endif
			//
			memset(&state, 0, sizeof(bot_entitystate_t));
			//
			VectorCopy(ent->r.currentOrigin, state.origin);
			if (i < MAX_CLIENTS) {
				VectorCopy(ent->s.apos.trBase, state.angles);
			} else {
				VectorCopy(ent->r.currentAngles, state.angles);
			}
			VectorCopy(ent->s.origin2, state.old_origin);
			VectorCopy(ent->r.mins, state.mins);
			VectorCopy(ent->r.maxs, state.maxs);
			state.type = ent->s.eType;
			state.flags = ent->s.eFlags;
			if (ent->r.bmodel) state.solid = SOLID_BSP;
			else state.solid = SOLID_BBOX;
			state.groundent = ent->s.groundEntityNum;
			state.modelindex = ent->s.modelindex;
			state.modelindex2 = ent->s.modelindex2;
			state.frame = ent->s.frame;
			state.event = ent->s.event;
			state.eventParm = ent->s.eventParm;
			state.powerups = ent->s.powerups;
			state.legsAnim = ent->s.legsAnim;
			state.torsoAnim = ent->s.torsoAnim;
			state.weapon = ent->s.weapon;
			//
			trap_BotLibUpdateEntity(i, &state);
		}

		BotAIRegularUpdate();
	}

	floattime = trap_AAS_Time();

	// execute scheduled bot AI
	for( i = 0; i < MAX_CLIENTS; i++ ) {
		if( !botstates[i] || !botstates[i]->inuse ) {
			continue;
		}
		//
		botstates[i]->botthink_residual += elapsed_time;
		//
		if ( botstates[i]->botthink_residual >= thinktime ) {
			botstates[i]->botthink_residual -= thinktime;

			if (!trap_AAS_Initialized()) return qfalse;

			if (g_entities[i].client->pers.connected == CON_CONNECTED) {
				BotAI(i, (float) thinktime / 1000);
			}
		}
	}


	// execute bot user commands every frame
	for( i = 0; i < MAX_CLIENTS; i++ ) {
		if( !botstates[i] || !botstates[i]->inuse ) {
			continue;
		}
		if( g_entities[i].client->pers.connected != CON_CONNECTED ) {
			continue;
		}

		BotUpdateInput(botstates[i], time, elapsed_time);
		trap_BotUserCommand(botstates[i]->client, &botstates[i]->lastucmd);
	}

	return qtrue;
}

/*
==============
BotInitLibrary
==============
*/
int BotInitLibrary(void) {
	char buf[144];

	//set the maxclients and maxentities library variables before calling BotSetupLibrary
	Com_sprintf(buf, sizeof(buf), "%d", level.maxclients);
	trap_BotLibVarSet("maxclients", buf);
	Com_sprintf(buf, sizeof(buf), "%d", MAX_GENTITIES);
	trap_BotLibVarSet("maxentities", buf);
	//bsp checksum
	trap_Cvar_VariableStringBuffer("sv_mapChecksum", buf, sizeof(buf));
	if (strlen(buf)) trap_BotLibVarSet("sv_mapChecksum", buf);
	//maximum number of aas links
	trap_Cvar_VariableStringBuffer("max_aaslinks", buf, sizeof(buf));
	if (strlen(buf)) trap_BotLibVarSet("max_aaslinks", buf);
	//maximum number of items in a level
	trap_Cvar_VariableStringBuffer("max_levelitems", buf, sizeof(buf));
	if (strlen(buf)) trap_BotLibVarSet("max_levelitems", buf);
	//game type
	trap_Cvar_VariableStringBuffer("g_gametype", buf, sizeof(buf));
	if (!strlen(buf)) strcpy(buf, "0");
	trap_BotLibVarSet("g_gametype", buf);
	//bot developer mode and log file
	trap_BotLibVarSet("bot_developer", bot_developer.string);
	trap_Cvar_VariableStringBuffer("logfile", buf, sizeof(buf));
	trap_BotLibVarSet("log", buf);
	//no chatting
	trap_Cvar_VariableStringBuffer("bot_nochat", buf, sizeof(buf));
	if (strlen(buf)) trap_BotLibVarSet("nochat", buf);
	//visualize jump pads
	trap_Cvar_VariableStringBuffer("bot_visualizejumppads", buf, sizeof(buf));
	if (strlen(buf)) trap_BotLibVarSet("bot_visualizejumppads", buf);
	//forced clustering calculations
	trap_Cvar_VariableStringBuffer("bot_forceclustering", buf, sizeof(buf));
	if (strlen(buf)) trap_BotLibVarSet("forceclustering", buf);
	//forced reachability calculations
	trap_Cvar_VariableStringBuffer("bot_forcereachability", buf, sizeof(buf));
	if (strlen(buf)) trap_BotLibVarSet("forcereachability", buf);
	//force writing of AAS to file
	trap_Cvar_VariableStringBuffer("bot_forcewrite", buf, sizeof(buf));
	if (strlen(buf)) trap_BotLibVarSet("forcewrite", buf);
	//no AAS optimization
	trap_Cvar_VariableStringBuffer("bot_aasoptimize", buf, sizeof(buf));
	if (strlen(buf)) trap_BotLibVarSet("aasoptimize", buf);
	//
	trap_Cvar_VariableStringBuffer("bot_saveroutingcache", buf, sizeof(buf));
	if (strlen(buf)) trap_BotLibVarSet("saveroutingcache", buf);
	//reload instead of cache bot character files
	trap_Cvar_VariableStringBuffer("bot_reloadcharacters", buf, sizeof(buf));
	if (!strlen(buf)) strcpy(buf, "0");
	trap_BotLibVarSet("bot_reloadcharacters", buf);
	//base directory
	trap_Cvar_VariableStringBuffer("fs_basepath", buf, sizeof(buf));
	if (strlen(buf)) trap_BotLibVarSet("basedir", buf);
	//game directory
	trap_Cvar_VariableStringBuffer("fs_game", buf, sizeof(buf));
	if (strlen(buf)) trap_BotLibVarSet("gamedir", buf);
	//
#ifdef MISSIONPACK
	trap_BotLibDefine("MISSIONPACK");
#endif
	//setup the bot library
	return trap_BotLibSetup();
}

/*
==============
BotAISetup
==============
*/
int BotAISetup( int restart ) {
	int			errnum;

	trap_Cvar_Register(&bot_thinktime, "bot_thinktime", "100", CVAR_CHEAT);
	trap_Cvar_Register(&bot_memorydump, "bot_memorydump", "0", CVAR_CHEAT);
	trap_Cvar_Register(&bot_saveroutingcache, "bot_saveroutingcache", "0", CVAR_CHEAT);
	trap_Cvar_Register(&bot_pause, "bot_pause", "0", CVAR_CHEAT);
	trap_Cvar_Register(&bot_report, "bot_report", "0", CVAR_CHEAT);
	trap_Cvar_Register(&bot_testsolid, "bot_testsolid", "0", CVAR_CHEAT);
	trap_Cvar_Register(&bot_testclusters, "bot_testclusters", "0", CVAR_CHEAT);
	trap_Cvar_Register(&bot_developer, "bot_developer", "0", CVAR_CHEAT);
	trap_Cvar_Register(&bot_interbreedchar, "bot_interbreedchar", "", 0);
	trap_Cvar_Register(&bot_interbreedbots, "bot_interbreedbots", "10", 0);
	trap_Cvar_Register(&bot_interbreedcycle, "bot_interbreedcycle", "20", 0);
	trap_Cvar_Register(&bot_interbreedwrite, "bot_interbreedwrite", "", 0);
	trap_Cvar_Register(&bot_moveset, "bot_moveset", "1", 0);
	trap_Cvar_Register(&bot_combatBhop, "bot_combatBhop", "1", 0);
	trap_Cvar_Register(&bot_swordBlock, "bot_swordBlock", "1", 0);
	trap_Cvar_Register(&bot_dash, "bot_dash", "1", 0);
	trap_Cvar_Register(&bot_slide, "bot_slide", "1", 0);
	trap_Cvar_Register(&bot_unstick, "bot_unstick", "1", 0);
	trap_Cvar_Register(&bot_airStrafe, "bot_airStrafe", "1", 0);
	trap_Cvar_Register(&bot_brake, "bot_brake", "1", 0);

	//if the game is restarted for a tournament
	if (restart) {
		return qtrue;
	}

	//initialize the bot states
	memset( botstates, 0, sizeof(botstates) );

	errnum = BotInitLibrary();
	if (errnum != BLERR_NOERROR) return qfalse;
	return qtrue;
}

/*
==============
BotAIShutdown
==============
*/
int BotAIShutdown( int restart ) {

	int i;

	//if the game is restarted for a tournament
	if ( restart ) {
		//shutdown all the bots in the botlib
		for (i = 0; i < MAX_CLIENTS; i++) {
			if (botstates[i] && botstates[i]->inuse) {
				BotAIShutdownClient(botstates[i]->client, restart);
			}
		}
		//don't shutdown the bot library
	}
	else {
		trap_BotLibShutdown();
	}
	return qtrue;
}

