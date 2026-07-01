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

#include "g_local.h"


/*
===============
G_DamageFeedback

Called just before a snapshot is sent to the given player.
Totals up all damage and generates both the player_state_t
damage values to that client for pain blends and kicks, and
global pain sound events for all clients.
===============
*/
void P_DamageFeedback( gentity_t *player ) {
	gclient_t	*client;
	float	count;
	vec3_t	angles;

	client = player->client;
	if ( client->ps.pm_type == PM_DEAD ) {
		return;
	}

	// total points of damage shot at the player this frame
	count = client->damage_blood + client->damage_armor;
	if ( count == 0 ) {
		return;		// didn't take any damage
	}

	if ( count > 255 ) {
		count = 255;
	}

	// send the information to the client

	// world damage (falling, slime, etc) uses a special code
	// to make the blend blob centered instead of positional
	if ( client->damage_fromWorld ) {
		client->ps.damagePitch = 255;
		client->ps.damageYaw = 255;

		client->damage_fromWorld = qfalse;
	} else {
		vectoangles( client->damage_from, angles );
		client->ps.damagePitch = angles[PITCH]/360.0 * 256;
		client->ps.damageYaw = angles[YAW]/360.0 * 256;
	}

	// play an appropriate pain sound
	if ( (level.time > player->pain_debounce_time) && !(player->flags & FL_GODMODE) ) {
		player->pain_debounce_time = level.time + 700;
		G_AddEvent( player, EV_PAIN, player->health );
		client->ps.damageEvent++;
	}


	client->ps.damageCount = count;

	//
	// clear totals
	//
	client->damage_blood = 0;
	client->damage_armor = 0;
	client->damage_knockback = 0;
}



/*
=============
P_WorldEffects

Check for lava / slime contents and drowning
=============
*/
void P_WorldEffects( gentity_t *ent ) {
	qboolean	envirosuit;
	int			waterlevel;

	if ( ent->client->noclip ) {
		ent->client->airOutTime = level.time + 12000;	// don't need air
		return;
	}

	waterlevel = ent->waterlevel;

	envirosuit = ent->client->ps.powerups[PW_BATTLESUIT] > level.time;

	//
	// check for drowning
	//
	if ( waterlevel == 3 ) {
		// envirosuit give air
		if ( envirosuit ) {
			ent->client->airOutTime = level.time + 10000;
		}

		// if out of air, start drowning
		if ( ent->client->airOutTime < level.time) {
			// drown!
			ent->client->airOutTime += 1000;
			if ( ent->health > 0 ) {
				// take more damage the longer underwater
				ent->damage += 2;
				if (ent->damage > 15)
					ent->damage = 15;

				// don't play a normal pain sound
				ent->pain_debounce_time = level.time + 200;

				G_Damage (ent, NULL, NULL, NULL, NULL, 
					ent->damage, DAMAGE_NO_ARMOR, MOD_WATER);
			}
		}
	} else {
		ent->client->airOutTime = level.time + 12000;
		ent->damage = 2;
	}

	//
	// check for sizzle damage (move to pmove?)
	//
	if (waterlevel && 
		(ent->watertype&(CONTENTS_LAVA|CONTENTS_SLIME)) ) {
		if (ent->health > 0
			&& ent->pain_debounce_time <= level.time	) {

			if ( envirosuit ) {
				G_AddEvent( ent, EV_POWERUP_BATTLESUIT, 0 );
			} else {
				if (ent->watertype & CONTENTS_LAVA) {
					G_Damage (ent, NULL, NULL, NULL, NULL, 
						30*waterlevel, 0, MOD_LAVA);
				}

				if (ent->watertype & CONTENTS_SLIME) {
					G_Damage (ent, NULL, NULL, NULL, NULL, 
						10*waterlevel, 0, MOD_SLIME);
				}
			}
		}
	}
}



/*
===============
G_SetClientSound
===============
*/
void G_SetClientSound( gentity_t *ent ) {
#ifdef MISSIONPACK
	if( ent->s.eFlags & EF_TICKING ) {
		ent->client->ps.loopSound = G_SoundIndex( "sound/weapons/proxmine/wstbtick.wav");
	}
	else
#endif
	if (ent->waterlevel && (ent->watertype&(CONTENTS_LAVA|CONTENTS_SLIME)) ) {
		ent->client->ps.loopSound = level.snd_fry;
	} else if ( ( ent->client->ps.pm_flags & PMF_DUCKED )
		&& ent->client->ps.groundEntityNum != ENTITYNUM_NONE
		&& ( ent->client->ps.velocity[0] * ent->client->ps.velocity[0]
		   + ent->client->ps.velocity[1] * ent->client->ps.velocity[1] )
		     > 250.0f * 250.0f ) {
		// crouch-slide hiss: the rush of a fast ground slide (matches
		// pm_slideMinSpeed). naturally gated by the grounded check, so it
		// sustains on real slides but stays silent through airborne bhops.
		ent->client->ps.loopSound = G_SoundIndex( "sound/world/wind2.wav" );
	} else {
		ent->client->ps.loopSound = 0;
	}
}



//==============================================================

/*
==============
ClientImpacts
==============
*/
void ClientImpacts( gentity_t *ent, pmove_t *pm ) {
	int		i, j;
	trace_t	trace;
	gentity_t	*other;

	memset( &trace, 0, sizeof( trace ) );
	for (i=0 ; i<pm->numtouch ; i++) {
		for (j=0 ; j<i ; j++) {
			if (pm->touchents[j] == pm->touchents[i] ) {
				break;
			}
		}
		if (j != i) {
			continue;	// duplicated
		}
		other = &g_entities[ pm->touchents[i] ];

		if ( ( ent->r.svFlags & SVF_BOT ) && ( ent->touch ) ) {
			ent->touch( ent, other, &trace );
		}

		if ( !other->touch ) {
			continue;
		}

		other->touch( other, ent, &trace );
	}

}

/*
============
G_TouchTriggers

Find all trigger entities that ent's current position touches.
Spectators will only interact with teleporters.
============
*/
void	G_TouchTriggers( gentity_t *ent ) {
	int			i, num;
	int			touch[MAX_GENTITIES];
	gentity_t	*hit;
	trace_t		trace;
	vec3_t		mins, maxs;
	static vec3_t	range = { 40, 40, 52 };

	if ( !ent->client ) {
		return;
	}

	// dead clients don't activate triggers!
	if ( ent->client->ps.stats[STAT_HEALTH] <= 0 ) {
		return;
	}

	VectorSubtract( ent->client->ps.origin, range, mins );
	VectorAdd( ent->client->ps.origin, range, maxs );

	num = trap_EntitiesInBox( mins, maxs, touch, MAX_GENTITIES );

	// can't use ent->absmin, because that has a one unit pad
	VectorAdd( ent->client->ps.origin, ent->r.mins, mins );
	VectorAdd( ent->client->ps.origin, ent->r.maxs, maxs );

	for ( i=0 ; i<num ; i++ ) {
		hit = &g_entities[touch[i]];

		if ( !hit->touch && !ent->touch ) {
			continue;
		}
		if ( !( hit->r.contents & CONTENTS_TRIGGER ) ) {
			continue;
		}

		// ignore most entities if a spectator
		if ( ent->client->sess.sessionTeam == TEAM_SPECTATOR ) {
			if ( hit->s.eType != ET_TELEPORT_TRIGGER &&
				// this is ugly but adding a new ET_? type will
				// most likely cause network incompatibilities
				hit->touch != Touch_DoorTrigger) {
				continue;
			}
		}

		// use separate code for determining if an item is picked up
		// so you don't have to actually contact its bounding box
		if ( hit->s.eType == ET_ITEM ) {
			if ( !BG_PlayerTouchesItem( &ent->client->ps, &hit->s, level.time ) ) {
				continue;
			}
		} else {
			if ( !trap_EntityContact( mins, maxs, hit ) ) {
				continue;
			}
		}

		memset( &trace, 0, sizeof(trace) );

		if ( hit->touch ) {
			hit->touch (hit, ent, &trace);
		}

		if ( ( ent->r.svFlags & SVF_BOT ) && ( ent->touch ) ) {
			ent->touch( ent, hit, &trace );
		}
	}

	// if we didn't touch a jump pad this pmove frame
	if ( ent->client->ps.jumppad_frame != ent->client->ps.pmove_framecount ) {
		ent->client->ps.jumppad_frame = 0;
		ent->client->ps.jumppad_ent = 0;
	}
}

/*
=================
SpectatorThink
=================
*/
void SpectatorThink( gentity_t *ent, usercmd_t *ucmd ) {
	pmove_t	pm;
	gclient_t	*client;

	client = ent->client;

	if ( client->sess.spectatorState != SPECTATOR_FOLLOW || !( client->ps.pm_flags & PMF_FOLLOW ) ) {
		if ( client->sess.spectatorState == SPECTATOR_FREE ) {
			if ( client->noclip ) {
				client->ps.pm_type = PM_NOCLIP;
			} else {
				client->ps.pm_type = PM_SPECTATOR;
			}
		} else {
			client->ps.pm_type = PM_FREEZE;
		}

		client->ps.speed = 400;	// faster than normal

		// set up for pmove
		memset (&pm, 0, sizeof(pm));
		pm.ps = &client->ps;
		pm.cmd = *ucmd;
		pm.tracemask = MASK_PLAYERSOLID & ~CONTENTS_BODY;	// spectators can fly through bodies
		pm.trace = trap_Trace;
		pm.pointcontents = trap_PointContents;

		// perform a pmove
		Pmove (&pm);
		// save results of pmove
		VectorCopy( client->ps.origin, ent->s.origin );

		G_TouchTriggers( ent );
		trap_UnlinkEntity( ent );
	}

	client->oldbuttons = client->buttons;
	client->buttons = ucmd->buttons;

	// attack button cycles through spectators
	if ( ( client->buttons & BUTTON_ATTACK ) && ! ( client->oldbuttons & BUTTON_ATTACK ) ) {
		Cmd_FollowCycle_f( ent, 1 );
	}
}



/*
=================
ClientInactivityTimer

Returns qfalse if the client is dropped
=================
*/
qboolean ClientInactivityTimer( gclient_t *client ) {
	if ( ! g_inactivity.integer ) {
		// give everyone some time, so if the operator sets g_inactivity during
		// gameplay, everyone isn't kicked
		client->inactivityTime = level.time + 60 * 1000;
		client->inactivityWarning = qfalse;
	} else if ( client->pers.cmd.forwardmove || 
		client->pers.cmd.rightmove || 
		client->pers.cmd.upmove ||
		(client->pers.cmd.buttons & BUTTON_ATTACK) ) {
		client->inactivityTime = level.time + g_inactivity.integer * 1000;
		client->inactivityWarning = qfalse;
	} else if ( !client->pers.localClient ) {
		if ( level.time > client->inactivityTime ) {
			trap_DropClient( client - level.clients, "Dropped due to inactivity" );
			return qfalse;
		}
		if ( level.time > client->inactivityTime - 10000 && !client->inactivityWarning ) {
			client->inactivityWarning = qtrue;
			trap_SendServerCommand( client - level.clients, "cp \"Ten seconds until inactivity drop!\n\"" );
		}
	}
	return qtrue;
}

/*
==================
ClientTimerActions

Actions that happen once a second
==================
*/
void ClientTimerActions( gentity_t *ent, int msec ) {
	gclient_t	*client;
#ifdef MISSIONPACK
	int			maxHealth;
#endif

	client = ent->client;
	client->timeResidual += msec;

	while ( client->timeResidual >= 1000 ) {
		client->timeResidual -= 1000;

		// the floor is never safe: idling on the ground burns,
		// escalating every second past the grace period
		if ( g_hotFloor.integer && ent->health > 0
			&& client->ps.pm_type == PM_NORMAL
			&& client->ps.groundEntityNum != ENTITYNUM_NONE
			&& client->sess.sessionTeam != TEAM_SPECTATOR ) {
			float	idleSpeed;

			idleSpeed = sqrt( client->ps.velocity[0] * client->ps.velocity[0]
				+ client->ps.velocity[1] * client->ps.velocity[1] );
			if ( idleSpeed < 120 ) {
				client->idleSeconds++;
				if ( client->idleSeconds == 2 ) {
					trap_SendServerCommand( ent - g_entities, "cp \"MOVE!\"" );
				} else if ( client->idleSeconds > 2 ) {
					int		burn;

					burn = ( client->idleSeconds - 2 ) * 5;
					if ( burn > 20 ) {
						burn = 20;
					}
					G_Damage( ent, NULL, NULL, NULL, NULL, burn,
						DAMAGE_NO_ARMOR, MOD_TRIGGER_HURT );
				}
			} else {
				client->idleSeconds = 0;
			}
		}

		// flow regen: speed is life. sustained flow (the milestone
		// threshold) tops health back up — the reward inverse of the
		// hot floor, and a reason to outrun the void rather than hide.
		// caps at max health, never overheals into megahealth territory.
		if ( g_flowRegen.integer > 0 && ent->health > 0
			&& client->ps.pm_type == PM_NORMAL
			&& client->sess.sessionTeam != TEAM_SPECTATOR
			&& ent->health < client->ps.stats[STAT_MAX_HEALTH] ) {
			float	flowSpeed;

			flowSpeed = sqrt( client->ps.velocity[0] * client->ps.velocity[0]
				+ client->ps.velocity[1] * client->ps.velocity[1] );
			if ( flowSpeed >= 500 ) {
				ent->health += g_flowRegen.integer;
				if ( ent->health > client->ps.stats[STAT_MAX_HEALTH] ) {
					ent->health = client->ps.stats[STAT_MAX_HEALTH];
				}
			}
		}

		// ammo regen: never stop to scavenge
		if ( g_ammoRegen.integer > 0 ) {
			int		w;

			for ( w = WP_MACHINEGUN ; w < WP_NUM_WEAPONS ; w++ ) {
				if ( !( client->ps.stats[STAT_WEAPONS] & ( 1 << w ) ) ) {
					continue;
				}
				// -1 is infinite ammo, leave it alone
				if ( client->ps.ammo[w] < 0 || client->ps.ammo[w] >= 50 ) {
					continue;
				}
				client->ps.ammo[w] += g_ammoRegen.integer;
				if ( client->ps.ammo[w] > 50 ) {
					client->ps.ammo[w] = 50;
				}
			}
		}

		// regenerate
#ifdef MISSIONPACK
		if( bg_itemlist[client->ps.stats[STAT_PERSISTANT_POWERUP]].giTag == PW_GUARD ) {
			maxHealth = client->ps.stats[STAT_MAX_HEALTH] / 2;
		}
		else if ( client->ps.powerups[PW_REGEN] ) {
			maxHealth = client->ps.stats[STAT_MAX_HEALTH];
		}
		else {
			maxHealth = 0;
		}
		if( maxHealth ) {
			if ( ent->health < maxHealth ) {
				ent->health += 15;
				if ( ent->health > maxHealth * 1.1 ) {
					ent->health = maxHealth * 1.1;
				}
				G_AddEvent( ent, EV_POWERUP_REGEN, 0 );
			} else if ( ent->health < maxHealth * 2) {
				ent->health += 5;
				if ( ent->health > maxHealth * 2 ) {
					ent->health = maxHealth * 2;
				}
				G_AddEvent( ent, EV_POWERUP_REGEN, 0 );
			}
#else
		if ( client->ps.powerups[PW_REGEN] ) {
			if ( ent->health < client->ps.stats[STAT_MAX_HEALTH]) {
				ent->health += 15;
				if ( ent->health > client->ps.stats[STAT_MAX_HEALTH] * 1.1 ) {
					ent->health = client->ps.stats[STAT_MAX_HEALTH] * 1.1;
				}
				G_AddEvent( ent, EV_POWERUP_REGEN, 0 );
			} else if ( ent->health < client->ps.stats[STAT_MAX_HEALTH] * 2) {
				ent->health += 5;
				if ( ent->health > client->ps.stats[STAT_MAX_HEALTH] * 2 ) {
					ent->health = client->ps.stats[STAT_MAX_HEALTH] * 2;
				}
				G_AddEvent( ent, EV_POWERUP_REGEN, 0 );
			}
#endif
		} else {
			// count down health when over max
			if ( ent->health > client->ps.stats[STAT_MAX_HEALTH] ) {
				ent->health--;
			}
		}

		// count down armor when over max
		if ( client->ps.stats[STAT_ARMOR] > client->ps.stats[STAT_MAX_HEALTH] ) {
			client->ps.stats[STAT_ARMOR]--;
		}
	}
#ifdef MISSIONPACK
	if( bg_itemlist[client->ps.stats[STAT_PERSISTANT_POWERUP]].giTag == PW_AMMOREGEN ) {
		int w, max, inc, t, i;
    int weapList[]={WP_MACHINEGUN,WP_SHOTGUN,WP_GRENADE_LAUNCHER,WP_ROCKET_LAUNCHER,WP_LIGHTNING,WP_RAILGUN,WP_PLASMAGUN,WP_BFG,WP_NAILGUN,WP_PROX_LAUNCHER,WP_CHAINGUN};
    int weapCount = ARRAY_LEN( weapList );
		//
    for (i = 0; i < weapCount; i++) {
		  w = weapList[i];

		  switch(w) {
			  case WP_MACHINEGUN: max = 50; inc = 4; t = 1000; break;
			  case WP_SHOTGUN: max = 10; inc = 1; t = 1500; break;
			  case WP_GRENADE_LAUNCHER: max = 10; inc = 1; t = 2000; break;
			  case WP_ROCKET_LAUNCHER: max = 10; inc = 1; t = 1750; break;
			  case WP_LIGHTNING: max = 50; inc = 5; t = 1500; break;
			  case WP_RAILGUN: max = 10; inc = 1; t = 1750; break;
			  case WP_PLASMAGUN: max = 50; inc = 5; t = 1500; break;
			  case WP_BFG: max = 10; inc = 1; t = 4000; break;
			  case WP_NAILGUN: max = 10; inc = 1; t = 1250; break;
			  case WP_PROX_LAUNCHER: max = 5; inc = 1; t = 2000; break;
			  case WP_CHAINGUN: max = 100; inc = 5; t = 1000; break;
			  default: max = 0; inc = 0; t = 1000; break;
		  }
		  client->ammoTimes[w] += msec;
		  if ( client->ps.ammo[w] >= max ) {
			  client->ammoTimes[w] = 0;
		  }
		  if ( client->ammoTimes[w] >= t ) {
			  while ( client->ammoTimes[w] >= t )
				  client->ammoTimes[w] -= t;
			  client->ps.ammo[w] += inc;
			  if ( client->ps.ammo[w] > max ) {
				  client->ps.ammo[w] = max;
			  }
		  }
    }
	}
#endif
}

/*
====================
ClientIntermissionThink
====================
*/
void ClientIntermissionThink( gclient_t *client ) {
	client->ps.eFlags &= ~EF_TALK;
	client->ps.eFlags &= ~EF_FIRING;

	// the level will exit when everyone wants to or after timeouts

	// swap and latch button actions
	client->oldbuttons = client->buttons;
	client->buttons = client->pers.cmd.buttons;
	if ( client->buttons & ( BUTTON_ATTACK | BUTTON_USE_HOLDABLE ) & ( client->oldbuttons ^ client->buttons ) ) {
		// this used to be an ^1 but once a player says ready, it should stick
		client->readyToExit = 1;
	}
}


/*
================
ClientEvents

Events will be passed on to the clients for presentation,
but any server game effects are handled here
================
*/
void ClientEvents( gentity_t *ent, int oldEventSequence ) {
	int		i, j;
	int		event;
	gclient_t *client;
	int		damage;
	vec3_t	origin, angles;
//	qboolean	fired;
	gitem_t *item;
	gentity_t *drop;

	client = ent->client;

	if ( oldEventSequence < client->ps.eventSequence - MAX_PS_EVENTS ) {
		oldEventSequence = client->ps.eventSequence - MAX_PS_EVENTS;
	}
	for ( i = oldEventSequence ; i < client->ps.eventSequence ; i++ ) {
		event = client->ps.events[ i & (MAX_PS_EVENTS-1) ];

		switch ( event ) {
		case EV_FALL_MEDIUM:
		case EV_FALL_FAR:
			if ( ent->s.eType != ET_PLAYER ) {
				break;		// not in the player model
			}
			if ( g_dmflags.integer & DF_NO_FALLING ) {
				break;
			}
			if ( event == EV_FALL_FAR ) {
				damage = 10;
			} else {
				damage = 5;
			}
			ent->pain_debounce_time = level.time + 200;	// no normal pain sound
			G_Damage (ent, NULL, NULL, NULL, NULL, damage, 0, MOD_FALLING);
			break;

		case EV_FIRE_WEAPON:
			// STRAFE 64: the sword carries its directional swing (packed start|end
			// quadrant) on the event parm — hand it to Weapon_Sword for the cut.
			ent->client->swordSwingParm = client->ps.eventParms[ i & (MAX_PS_EVENTS-1) ];
			FireWeapon( ent );
			break;

		case EV_USE_ITEM1:		// teleporter
			// drop flags in CTF
			item = NULL;
			j = 0;

			if ( ent->client->ps.powerups[ PW_REDFLAG ] ) {
				item = BG_FindItemForPowerup( PW_REDFLAG );
				j = PW_REDFLAG;
			} else if ( ent->client->ps.powerups[ PW_BLUEFLAG ] ) {
				item = BG_FindItemForPowerup( PW_BLUEFLAG );
				j = PW_BLUEFLAG;
			} else if ( ent->client->ps.powerups[ PW_NEUTRALFLAG ] ) {
				item = BG_FindItemForPowerup( PW_NEUTRALFLAG );
				j = PW_NEUTRALFLAG;
			}

			if ( item ) {
				drop = Drop_Item( ent, item, 0 );
				// decide how many seconds it has left
				drop->count = ( ent->client->ps.powerups[ j ] - level.time ) / 1000;
				if ( drop->count < 1 ) {
					drop->count = 1;
				}

				ent->client->ps.powerups[ j ] = 0;
			}

#ifdef MISSIONPACK
			if ( g_gametype.integer == GT_HARVESTER ) {
				if ( ent->client->ps.generic1 > 0 ) {
					if ( ent->client->sess.sessionTeam == TEAM_RED ) {
						item = BG_FindItem( "Blue Cube" );
					} else {
						item = BG_FindItem( "Red Cube" );
					}
					if ( item ) {
						for ( j = 0; j < ent->client->ps.generic1; j++ ) {
							drop = Drop_Item( ent, item, 0 );
							if ( ent->client->sess.sessionTeam == TEAM_RED ) {
								drop->spawnflags = TEAM_BLUE;
							} else {
								drop->spawnflags = TEAM_RED;
							}
						}
					}
					ent->client->ps.generic1 = 0;
				}
			}
#endif
			SelectSpawnPoint( ent->client->ps.origin, origin, angles, qfalse );
			TeleportPlayer( ent, origin, angles );
			break;

		case EV_USE_ITEM2:		// medkit
			ent->health = ent->client->ps.stats[STAT_MAX_HEALTH] + 25;

			break;

#ifdef MISSIONPACK
		case EV_USE_ITEM3:		// kamikaze
			// make sure the invulnerability is off
			ent->client->invulnerabilityTime = 0;
			// start the kamikze
			G_StartKamikaze( ent );
			break;

		case EV_USE_ITEM4:		// portal
			if( ent->client->portalID ) {
				DropPortalSource( ent );
			}
			else {
				DropPortalDestination( ent );
			}
			break;
		case EV_USE_ITEM5:		// invulnerability
			ent->client->invulnerabilityTime = level.time + 10000;
			break;
#endif

		default:
			break;
		}
	}

}

#ifdef MISSIONPACK
/*
==============
StuckInOtherClient
==============
*/
static int StuckInOtherClient(gentity_t *ent) {
	int i;
	gentity_t	*ent2;

	ent2 = &g_entities[0];
	for ( i = 0; i < MAX_CLIENTS; i++, ent2++ ) {
		if ( ent2 == ent ) {
			continue;
		}
		if ( !ent2->inuse ) {
			continue;
		}
		if ( !ent2->client ) {
			continue;
		}
		if ( ent2->health <= 0 ) {
			continue;
		}
		//
		if (ent2->r.absmin[0] > ent->r.absmax[0])
			continue;
		if (ent2->r.absmin[1] > ent->r.absmax[1])
			continue;
		if (ent2->r.absmin[2] > ent->r.absmax[2])
			continue;
		if (ent2->r.absmax[0] < ent->r.absmin[0])
			continue;
		if (ent2->r.absmax[1] < ent->r.absmin[1])
			continue;
		if (ent2->r.absmax[2] < ent->r.absmin[2])
			continue;
		return qtrue;
	}
	return qfalse;
}
#endif

void BotTestSolid(vec3_t origin);

/*
==============
SendPendingPredictableEvents
==============
*/
void SendPendingPredictableEvents( playerState_t *ps ) {
	gentity_t *t;
	int event, seq;
	int extEvent, number;

	// if there are still events pending
	if ( ps->entityEventSequence < ps->eventSequence ) {
		// create a temporary entity for this event which is sent to everyone
		// except the client who generated the event
		seq = ps->entityEventSequence & (MAX_PS_EVENTS-1);
		event = ps->events[ seq ] | ( ( ps->entityEventSequence & 3 ) << 8 );
		// set external event to zero before calling BG_PlayerStateToEntityState
		extEvent = ps->externalEvent;
		ps->externalEvent = 0;
		// create temporary entity for event
		t = G_TempEntity( ps->origin, event );
		number = t->s.number;
		BG_PlayerStateToEntityState( ps, &t->s, qtrue );
		t->s.number = number;
		t->s.eType = ET_EVENTS + event;
		t->s.eFlags |= EF_PLAYER_EVENT;
		t->s.otherEntityNum = ps->clientNum;
		// send to everyone except the client who generated the event
		t->r.svFlags |= SVF_NOTSINGLECLIENT;
		t->r.singleClient = ps->clientNum;
		// set back external event
		ps->externalEvent = extEvent;
	}
}

/*
==============
G_UpdateTimeBind

SUPERHOT-style world-clock dilation. Maps the player's movement "intent"
(how hard they're moving/inputting) onto the global timescale: slow when still
or crawling, realtime at full chain. Driven once per frame by the local human.

Intent = max( normalized horizontal speed, normalized movement input ), with an
optional floor while firing so combat keeps time flowing. The result is eased
toward the target on REAL frametime (trap_Milliseconds), so the ramp in/out takes
the same wall-clock time at any current timescale — slow-mo snaps in and releases
cleanly instead of crawling back up.

UNIFORM model (by design): this scales the WHOLE sim — player + bots + world all
slow together; the player drives the clock with movement intent, so they never
feel sluggish (push the stick and time returns). Two things are deliberately
EXEMPT and stay on real time: the rising VOID (G_RunVoid / CG_VoidZ) and the
race/ghost CLOCK, so stopping is never safe and slow-mo can't cheat the timer.
To keep movement FEEL consistent across timescales (Q3 air-accel is framerate-
dependent and PM_Pmove chops by frame dt), we force pmove_fixed 1 for the duration
that g_timeBind is active, restoring the prior value when it's turned off. All of
this is gated behind g_timeBind (default 0): default play and the headless dojo
are untouched. (Per-entity time — only the human dilates, SUPERHOT-style — was
explored and set aside; see ROADMAP "Bots work under slow-mo".)
==============
*/
/*
==============
G_ClientDash

STRAFE 64: a SHIFT dash (BUTTON_DASH) that lunges along your motion and REVECTORS
a little toward the nearest enemy in a forward cone — so a dash can carry you onto
the kill-line and connect a slice you'd otherwise sail past. Burst = g_dashSpeed,
homing blend = g_dashHoming, on a short cooldown. Server-side (it must see enemies);
the velocity lands in the playerState the client predicts.
==============
*/
#define DASH_COOLDOWN	650		// ms between dashes
#define DASH_RANGE		720.0f	// how far ahead an enemy can be to pull the dash

static void G_ClientDash( gentity_t *ent, usercmd_t *ucmd ) {
	gclient_t	*client = ent->client;
	vec3_t		fwd, dir, to, best;
	float		bestdot, dist, h;
	gentity_t	*e, *target;
	int			i;

	// fresh press only (client->buttons is still last frame's here — it's
	// restamped further down ClientThink), on cooldown, alive and in normal play
	if ( !( ucmd->buttons & BUTTON_DASH ) || ( client->buttons & BUTTON_DASH ) ) {
		return;
	}
	if ( client->ps.pm_type != PM_NORMAL || level.time < client->dashTime ) {
		return;
	}
	client->dashTime = level.time + DASH_COOLDOWN;
	// WAKE the clock for a short window so the dash reads as a FAST real-time
	// lunge, not a slow drift (a 430u/s burst at timescale 0.06 would crawl).
	client->dashSurge = level.time + 300;

	// base direction: where you're STEERING — held move keys relative to your
	// full view (so the dash follows your aim pitch up and down), else straight
	// down your crosshair. This lets a dash REVECTOR your flight toward where
	// you point, instead of locking to the line you're already flying.
	{
		vec3_t	vf, vr;

		AngleVectors( client->ps.viewangles, vf, vr, NULL );
		dir[0] = vf[0] * ucmd->forwardmove + vr[0] * ucmd->rightmove;
		dir[1] = vf[1] * ucmd->forwardmove + vr[1] * ucmd->rightmove;
		dir[2] = vf[2] * ucmd->forwardmove;	// strafe stays level; forward carries aim pitch
		if ( VectorNormalize( dir ) < 1.0f ) {	// no steer input: dash where you look
			VectorCopy( vf, dir );
			VectorNormalize( dir );
		}
	}

	// revector toward the nearest enemy / slice-gate inside a ~70deg forward cone
	AngleVectors( client->ps.viewangles, fwd, NULL, NULL );
	fwd[2] = 0.0f;
	VectorNormalize( fwd );
	target = NULL;
	bestdot = 0.35f;
	VectorClear( best );
	for ( i = 0 ; i < level.num_entities ; i++ ) {
		e = &g_entities[i];
		if ( e == ent || !e->inuse || e->health <= 0 ) {
			continue;
		}
		if ( !( e->client || ( e->flags & FL_SLICE_GATE ) ) ) {
			continue;
		}
		VectorSubtract( e->r.currentOrigin, ent->r.currentOrigin, to );
		to[2] = 0.0f;
		dist = VectorNormalize( to );
		if ( dist < 1.0f || dist > DASH_RANGE ) {
			continue;
		}
		if ( DotProduct( to, fwd ) > bestdot ) {
			bestdot = DotProduct( to, fwd );
			target = e;
			VectorCopy( to, best );
		}
	}
	if ( target ) {
		h = g_dashHoming.value;
		if ( h < 0.0f ) h = 0.0f;
		if ( h > 1.0f ) h = 1.0f;
		dir[0] = dir[0] * ( 1.0f - h ) + best[0] * h;
		dir[1] = dir[1] * ( 1.0f - h ) + best[1] * h;
		if ( VectorNormalize( dir ) < 0.01f ) {
			VectorCopy( fwd, dir );
		}
	}

	client->ps.velocity[0] += dir[0] * g_dashSpeed.value;
	client->ps.velocity[1] += dir[1] * g_dashSpeed.value;
	// vertical follows your aim: dash UP when you look up, DIVE when you look down
	client->ps.velocity[2] += dir[2] * g_dashSpeed.value;
	// keep a little lift for a level/upward dash so an air-dash stays airborne to
	// chain a slice — but never when you're deliberately aiming down to dive
	if ( dir[2] > -0.1f && client->ps.velocity[2] < 90.0f ) {
		client->ps.velocity[2] = 90.0f;
	}

	// STRAFE 64: fire the chromatic-ghost strobe trail off the dash (cgame EV_DASH).
	G_AddPredictableEvent( ent, EV_DASH, 0 );
}

static void G_UpdateTimeBind( gentity_t *ent, usercmd_t *ucmd, int msec ) {
	static float	cur = -1.0f;		// last applied timescale (one human assumed)
	static qboolean	active = qfalse;	// did we take over the timescale cvar?
	static int		lastReal = 0;		// trap_Milliseconds at the previous update
	static int		savedPmoveFixed = 0;	// pmove_fixed to restore when slow-mo ends
	int				now, realMsec;
	float			hspeed, speedFrac, inputMag, intent, target, t, rate;
	float			tmin, tmax, ref, curve;

	// bots never drive the clock — keeps headless playtest runs untouched
	if ( ent->r.svFlags & SVF_BOT ) {
		return;
	}

	if ( !g_timeBind.integer ) {
		// hand the clock back exactly once when disabled
		if ( active ) {
			trap_Cvar_Set( "timescale", "1" );
			trap_Cvar_Set( "pmove_fixed", va( "%i", savedPmoveFixed ) );
			active = qfalse;
			cur = -1.0f;
		}
		lastReal = 0;
		return;
	}

	// First frame slow-mo takes over: force pmove_fixed on for the session.
	// Without it, PM_Pmove chops the move into chunks sized by the (timescale-
	// scaled) frame dt, so air-accel — which is famously framerate-dependent in
	// Q3 — changes as you dilate time: the player's own strafe/air control would
	// feel DIFFERENT in slow-mo than at speed, and bots ping/wedge instead of
	// tracking their line. pmove_fixed pins integration to fixed pmove_msec
	// substeps, so movement physics is identical at every timescale — the core of
	// making slow-mo feel consistent and crisp. Gated to g_timeBind: default play
	// and the headless dojo (g_timeBind 0) keep their tuned, unchanged movement.
	if ( !active ) {
		savedPmoveFixed = pmove_fixed.integer;
		trap_Cvar_Set( "pmove_fixed", "1" );
	}

	tmin  = g_timeBindMin.value;
	tmax  = g_timeBindMax.value;
	ref   = g_timeBindRef.value;
	curve = g_timeBindCurve.value;
	if ( ref < 1.0f )    ref = 1.0f;
	if ( curve < 0.1f )  curve = 0.1f;
	if ( tmin < 0.01f )  tmin = 0.01f;	// keep the log map well-defined

	// speed component: how fast we're actually moving on the floor plane
	hspeed = sqrt( ent->client->ps.velocity[0] * ent->client->ps.velocity[0]
				 + ent->client->ps.velocity[1] * ent->client->ps.velocity[1] );
	speedFrac = hspeed / ref;
	if ( speedFrac > 1.0f ) speedFrac = 1.0f;

	// input component: SUPERHOT keys on *wanting* to move, not just moving
	inputMag = sqrt( (float)( ucmd->forwardmove * ucmd->forwardmove
							+ ucmd->rightmove   * ucmd->rightmove ) ) / 127.0f;
	if ( inputMag > 1.0f ) inputMag = 1.0f;

	intent = ( speedFrac > inputMag ) ? speedFrac : inputMag;

	// firing nudges time forward even when stationary (tunable, 0 = off)
	if ( ( ucmd->buttons & BUTTON_ATTACK ) && g_timeBindFire.value > intent ) {
		intent = g_timeBindFire.value;
	}

	// DASH wakes the clock: for a short window after a dash, force intent high so
	// the lunge is a FAST real-time burst, not a slow-mo drift (a 430u/s dash at
	// timescale 0.06 would crawl). This is what makes SHIFT read as "dash", not
	// "slow time". (See G_ClientDash, which stamps client->dashSurge.)
	if ( level.time < ent->client->dashSurge && intent < 0.95f ) {
		intent = 0.95f;
	}

	if ( intent > 1.0f ) intent = 1.0f;

	if ( g_timeBindLog.integer ) {
		// logarithmic (geometric) slow-mo: each step of intent MULTIPLIES the
		// timescale rather than adding to it, so the deep end feels far slower
		// and the ramp back to realtime is perceptually even — the Matrix curve
		target = tmin * pow( tmax / tmin, pow( intent, curve ) );
	} else {
		// linear interpolation of the shaped intent
		target = tmin + ( tmax - tmin ) * pow( intent, curve );
	}

	// raising the GUARD dips the world into a defensive slow-mo beat — a moment to
	// read the room / time a parry. A MULTIPLIER on the target (not an intent cap),
	// so it ALWAYS ramps time down a notch while BUTTON_BLOCK is held, whether
	// you're sprinting or nearly still. g_timeBindBlock = the dip (0.5 = half-speed
	// of whatever the clock would otherwise be; lower = deeper; 1 = off).
	if ( ( ucmd->buttons & BUTTON_BLOCK ) && g_timeBindBlock.value < 1.0f ) {
		target *= g_timeBindBlock.value;
		if ( target < tmin ) target = tmin;
	}

	// crouch/slide TIME BRAKE: while ducked, CAP the clock at a KINETIC slow-mo —
	// and scale it with speed so a fast slide RIPS near-realtime (Titanfall feel)
	// while a slow slide keeps more dilation. g_timeBindCrouch is the slow-end
	// floor (at slide-min speed); a full-speed slide eases up toward ~0.92, so the
	// ground rushes past when you commit hard. Caps only (never speeds up), so a
	// still crouch stays at its already-lower freeze. 1.0 = brake off entirely.
	if ( ( ent->client->ps.pm_flags & PMF_DUCKED ) && g_timeBindCrouch.value < 1.0f ) {
		float slideTs = g_timeBindCrouch.value
					  + ( 0.92f - g_timeBindCrouch.value ) * speedFrac;
		if ( slideTs > 0.92f ) slideTs = 0.92f;
		if ( target > slideTs ) target = slideTs;
	}

	// smooth toward target so the world eases in/out instead of snapping.
	// Step the ease on REAL time (trap_Milliseconds), NOT the passed-in msec:
	// msec is the timescale-SCALED frame delta, so deep in slow-mo it shrinks
	// and the ramp BACK up to realtime crawls — the mushy exit that reads as
	// not-crisp. Real frametime makes the ramp take the same wall-clock time
	// in and out at any current timescale, so slow-mo snaps in and releases
	// cleanly. (Game-frames tick at sv_fps real-rate, so realMsec ~ 1000/sv_fps.)
	now = trap_Milliseconds();
	realMsec = lastReal ? ( now - lastReal ) : 0;
	lastReal = now;
	if ( realMsec < 0 || realMsec > 250 ) {
		realMsec = 0;		// first frame / hitch: don't lurch the clock
	}
	if ( cur < 0.0f ) {
		cur = target;
	} else {
		// ASYMMETRIC ramp: snap UP fast when you start moving (g_timeBindRise),
		// ease DOWN gently into the freeze (g_timeBindSmooth). A symmetric ramp
		// made a step "happen in slow time" before the clock caught up — time
		// felt stuck near the floor as you stepped off. Rising fast makes a step
		// bring the world instantly to life; falling slow keeps the dreamy settle
		// back into near-freeze. (Time only pumps up quick, never down, so no
		// jitter.)
		rate = ( target > cur ) ? g_timeBindRise.value : g_timeBindSmooth.value;
		t = rate * ( realMsec * 0.001f );
		if ( t > 1.0f ) t = 1.0f;
		if ( t < 0.0f ) t = 0.0f;
		cur += ( target - cur ) * t;
	}

	trap_Cvar_Set( "timescale", va( "%.3f", cur ) );
	active = qtrue;
}

/*
==============
ClientThink

This will be called once for each client frame, which will
usually be a couple times for each server frame on fast clients.

If "g_synchronousClients 1" is set, this will be called exactly
once for each server frame, which makes for smooth demo recording.
==============
*/
void ClientThink_real( gentity_t *ent ) {
	gclient_t	*client;
	pmove_t		pm;
	int			oldEventSequence;
	int			msec;
	usercmd_t	*ucmd;

	client = ent->client;

	// don't think if the client is not yet connected (and thus not yet spawned in)
	if (client->pers.connected != CON_CONNECTED) {
		return;
	}
	// mark the time, so the connection sprite can be removed
	ucmd = &ent->client->pers.cmd;

	// sanity check the command time to prevent speedup cheating
	if ( ucmd->serverTime > level.time + 200 ) {
		ucmd->serverTime = level.time + 200;
//		G_Printf("serverTime <<<<<\n" );
	}
	if ( ucmd->serverTime < level.time - 1000 ) {
		ucmd->serverTime = level.time - 1000;
//		G_Printf("serverTime >>>>>\n" );
	} 

	msec = ucmd->serverTime - client->ps.commandTime;
	// following others may result in bad times, but we still want
	// to check for follow toggles
	if ( msec < 1 && client->sess.spectatorState != SPECTATOR_FOLLOW ) {
		return;
	}
	if ( msec > 200 ) {
		msec = 200;
	}

	if ( pmove_msec.integer < 8 ) {
		trap_Cvar_Set("pmove_msec", "8");
		trap_Cvar_Update(&pmove_msec);
	}
	else if (pmove_msec.integer > 33) {
		trap_Cvar_Set("pmove_msec", "33");
		trap_Cvar_Update(&pmove_msec);
	}

	if ( pmove_fixed.integer || client->pers.pmoveFixed ) {
		ucmd->serverTime = ((ucmd->serverTime + pmove_msec.integer-1) / pmove_msec.integer) * pmove_msec.integer;
		//if (ucmd->serverTime - client->ps.commandTime <= 0)
		//	return;
	}

	//
	// check for exiting intermission
	//
	if ( level.intermissiontime ) {
		ClientIntermissionThink( client );
		return;
	}

	// spectators don't do much
	if ( client->sess.sessionTeam == TEAM_SPECTATOR ) {
		if ( client->sess.spectatorState == SPECTATOR_SCOREBOARD ) {
			return;
		}
		SpectatorThink( ent, ucmd );
		return;
	}

	// check for inactivity timer, but never drop the local client of a non-dedicated server
	if ( !ClientInactivityTimer( client ) ) {
		return;
	}

	// clear the rewards if time
	if ( level.time > client->rewardTime ) {
		client->ps.eFlags &= ~(EF_AWARD_IMPRESSIVE | EF_AWARD_EXCELLENT | EF_AWARD_GAUNTLET | EF_AWARD_ASSIST | EF_AWARD_DEFEND | EF_AWARD_CAP );
	}

	if ( client->noclip ) {
		client->ps.pm_type = PM_NOCLIP;
	} else if ( client->ps.stats[STAT_HEALTH] <= 0 ) {
		client->ps.pm_type = PM_DEAD;
	} else {
		client->ps.pm_type = PM_NORMAL;
	}

	client->ps.gravity = g_gravity.value;

	// set speed
	client->ps.speed = g_speed.value;

	// STRAFE 64 run mutators (g_mutator): per-run rule twists for replayability,
	// applied every frame so they survive the per-frame gravity/speed reset above
	switch ( g_mutator.integer ) {
	case 1:		// LOW-G — floaty surf, longer air-strafes
		client->ps.gravity = client->ps.gravity * 55 / 100;
		break;
	case 2:		// RUSH — everything faster
		client->ps.speed = client->ps.speed * 140 / 100;
		break;
	case 3:		// HEAVY — snappier falls + a bit faster, less forgiving
		client->ps.gravity = client->ps.gravity * 135 / 100;
		client->ps.speed = client->ps.speed * 115 / 100;
		break;
	case 4:		// VECTORGUN-ONLY — the speed-scaled rail is the only gun (G_VECTORGUN_ON
		break;	// handles the loadout at spawn + item economy); no physics twist
	default:
		break;
	}

#ifdef MISSIONPACK
	if( bg_itemlist[client->ps.stats[STAT_PERSISTANT_POWERUP]].giTag == PW_SCOUT ) {
		client->ps.speed *= 1.5;
	}
	else
#endif
	if ( client->ps.powerups[PW_HASTE] ) {
		client->ps.speed *= 1.3;
	}

	// Let go of the hook if we aren't firing
	if ( client->ps.weapon == WP_GRAPPLING_HOOK &&
		client->hook && !( ucmd->buttons & BUTTON_ATTACK ) ) {
		Weapon_HookFree(client->hook);
	}

	// set up for pmove
	oldEventSequence = client->ps.eventSequence;

	memset (&pm, 0, sizeof(pm));

	// check for the hit-scan gauntlet, don't let the action
	// go through as an attack unless it actually hits something
	if ( client->ps.weapon == WP_GAUNTLET && !( ucmd->buttons & BUTTON_TALK ) &&
		( ucmd->buttons & BUTTON_ATTACK ) && client->ps.weaponTime <= 0 ) {
		pm.gauntletHit = CheckGauntletAttack( ent );
	}

	if ( ent->flags & FL_FORCE_GESTURE ) {
		ent->flags &= ~FL_FORCE_GESTURE;
		ent->client->pers.cmd.buttons |= BUTTON_GESTURE;
	}

#ifdef MISSIONPACK
	// check for invulnerability expansion before doing the Pmove
	if (client->ps.powerups[PW_INVULNERABILITY] ) {
		if ( !(client->ps.pm_flags & PMF_INVULEXPAND) ) {
			vec3_t mins = { -INVUL_RADIUS, -INVUL_RADIUS, -INVUL_RADIUS };
			vec3_t maxs = { INVUL_RADIUS, INVUL_RADIUS, INVUL_RADIUS };
			vec3_t oldmins, oldmaxs;

			VectorCopy (ent->r.mins, oldmins);
			VectorCopy (ent->r.maxs, oldmaxs);
			// expand
			VectorCopy (mins, ent->r.mins);
			VectorCopy (maxs, ent->r.maxs);
			trap_LinkEntity(ent);
			// check if this would get anyone stuck in this player
			if ( !StuckInOtherClient(ent) ) {
				// set flag so the expanded size will be set in PM_CheckDuck
				client->ps.pm_flags |= PMF_INVULEXPAND;
			}
			// set back
			VectorCopy (oldmins, ent->r.mins);
			VectorCopy (oldmaxs, ent->r.maxs);
			trap_LinkEntity(ent);
		}
	}
#endif

	pm.ps = &client->ps;
	pm.cmd = *ucmd;
	if ( pm.ps->pm_type == PM_DEAD ) {
		pm.tracemask = MASK_PLAYERSOLID & ~CONTENTS_BODY;
	}
	else if ( ent->r.svFlags & SVF_BOT ) {
		pm.tracemask = MASK_PLAYERSOLID | CONTENTS_BOTCLIP;
	}
	else {
		pm.tracemask = MASK_PLAYERSOLID;
	}
	pm.trace = trap_Trace;
	pm.pointcontents = trap_PointContents;
	pm.debugLevel = g_debugMove.integer;
	pm.noFootsteps = ( g_dmflags.integer & DF_NO_FOOTSTEPS ) > 0;

	pm.pmove_fixed = pmove_fixed.integer | client->pers.pmoveFixed;
	pm.pmove_msec = pmove_msec.integer;
	pm.vectorgun = G_VECTORGUN_ON;
	// SUPERHOT: current world timescale so pmove can keep the player time-invariant
	pm.timeScale = g_timeBind.integer ? trap_Cvar_VariableValue( "timescale" ) : 1.0f;
	if ( pm.timeScale <= 0.01f ) {
		pm.timeScale = 1.0f;
	}

	// live air-strafe tuning: drive the pmove feel constants from cvars
	pm_strafeAccelerate  = g_strafeAccel.value;
	pm_wishSpeedClamp    = g_airWishClamp.value;
	pm_airaccelerate     = g_airAccel.value;
	pm_airStopAccelerate = g_airStopAccel.value;
	pm_airControlAmount  = g_airControl.value;
	// sword lunge magnetism (predicted): drive the steer from cvars so client and
	// server pmove agree on how hard the swing snaps onto a target
	pm_swordMagnet       = g_swordMagnet.value;
	pm_swordMagnetRange  = g_swordMagnetRange.value;
	pm_swordRecovery     = g_swordRecovery.value;
	pm_swordRecoveryMin  = g_swordRecoveryMin.value;

	VectorCopy( client->ps.origin, client->oldOrigin );

#ifdef MISSIONPACK
		if (level.intermissionQueued != 0 && g_singlePlayer.integer) {
			if ( level.time - level.intermissionQueued >= 1000  ) {
				pm.cmd.buttons = 0;
				pm.cmd.forwardmove = 0;
				pm.cmd.rightmove = 0;
				pm.cmd.upmove = 0;
				if ( level.time - level.intermissionQueued >= 2000 && level.time - level.intermissionQueued <= 2500 ) {
					trap_SendConsoleCommand( EXEC_APPEND, "centerview\n");
				}
				ent->client->ps.pm_type = PM_SPINTERMISSION;
			}
		}
		Pmove (&pm);
#else
		Pmove (&pm);
#endif

	// STRAFE 64 guard commitment: stamp when the blade was first raised so the
	// parry only becomes protective after g_swordGuardRaise ms — reading the swing
	// early is the skill, not panic-blocking on reaction (Sekiro block-early).
	if ( ent->client->ps.eFlags & EF_BLOCKING ) {
		if ( !ent->client->wasBlocking ) {
			ent->client->guardRaiseTime = level.time;
		}
		ent->client->wasBlocking = qtrue;
	} else {
		ent->client->wasBlocking = qfalse;
		ent->client->guardRaiseTime = 0;
	}

	// SUPERHOT-style time dilation: drive the world clock from movement intent
	G_UpdateTimeBind( ent, ucmd, msec );

	// SHIFT dash that revectors toward enemies (after pmove so it sets the
	// resulting velocity; lands in the predicted playerState)
	G_ClientDash( ent, ucmd );

	// save results of pmove
	if ( ent->client->ps.eventSequence != oldEventSequence ) {
		ent->eventTime = level.time;
	}
	if (g_smoothClients.integer) {
		BG_PlayerStateToEntityStateExtraPolate( &ent->client->ps, &ent->s, ent->client->ps.commandTime, qtrue );
	}
	else {
		BG_PlayerStateToEntityState( &ent->client->ps, &ent->s, qtrue );
	}
	SendPendingPredictableEvents( &ent->client->ps );

	if ( !( ent->client->ps.eFlags & EF_FIRING ) ) {
		client->fireHeld = qfalse;		// for grapple
	}

	// use the snapped origin for linking so it matches client predicted versions
	VectorCopy( ent->s.pos.trBase, ent->r.currentOrigin );

	VectorCopy (pm.mins, ent->r.mins);
	VectorCopy (pm.maxs, ent->r.maxs);

	ent->waterlevel = pm.waterlevel;
	ent->watertype = pm.watertype;

	// execute client events
	ClientEvents( ent, oldEventSequence );

	// link entity now, after any personal teleporters have been used
	trap_LinkEntity (ent);
	if ( !ent->client->noclip ) {
		G_TouchTriggers( ent );
	}

	// NOTE: now copy the exact origin over otherwise clients can be snapped into solid
	VectorCopy( ent->client->ps.origin, ent->r.currentOrigin );

	//test for solid areas in the AAS file
	BotTestAAS(ent->r.currentOrigin);

	// touch other objects
	ClientImpacts( ent, &pm );

	// save results of triggers and client events
	if (ent->client->ps.eventSequence != oldEventSequence) {
		ent->eventTime = level.time;
	}

	// swap and latch button actions
	client->oldbuttons = client->buttons;
	client->buttons = ucmd->buttons;
	client->latched_buttons |= client->buttons & ~client->oldbuttons;

	// check for respawning
	if ( client->ps.stats[STAT_HEALTH] <= 0 ) {
		// LATTICE: death is elimination, not a respawn. After the death-cam
		// delay, drop the pilot to a spectator and let the heat play out.
		if ( g_lattice.integer ) {
			if ( level.time > client->respawnTime ) {
				G_LatticeEliminate( ent );
			}
			return;
		}
		// wait for the attack button to be pressed
		if ( level.time > client->respawnTime ) {
			// forcerespawn is to prevent users from waiting out powerups
			if ( g_forcerespawn.integer > 0 && 
				( level.time - client->respawnTime ) > g_forcerespawn.integer * 1000 ) {
				ClientRespawn( ent );
				return;
			}
		
			// pressing attack or use is the normal respawn method
			if ( ucmd->buttons & ( BUTTON_ATTACK | BUTTON_USE_HOLDABLE ) ) {
				ClientRespawn( ent );
			}
		}
		return;
	}

	// perform once-a-second actions
	ClientTimerActions( ent, msec );
}

/*
==================
ClientThink

A new command has arrived from the client
==================
*/
void ClientThink( int clientNum ) {
	gentity_t *ent;

	ent = g_entities + clientNum;
	trap_GetUsercmd( clientNum, &ent->client->pers.cmd );

	// mark the time we got info, so we can display the
	// phone jack if they don't get any for a while
	ent->client->lastCmdTime = level.time;

	if ( !(ent->r.svFlags & SVF_BOT) && !g_synchronousClients.integer ) {
		ClientThink_real( ent );
	}
}


void G_RunClient( gentity_t *ent ) {
	if ( !(ent->r.svFlags & SVF_BOT) && !g_synchronousClients.integer ) {
		return;
	}
	ent->client->pers.cmd.serverTime = level.time;
	ClientThink_real( ent );
}


/*
==================
SpectatorClientEndFrame

==================
*/
void SpectatorClientEndFrame( gentity_t *ent ) {
	gclient_t	*cl;

	// if we are doing a chase cam or a remote view, grab the latest info
	if ( ent->client->sess.spectatorState == SPECTATOR_FOLLOW ) {
		int		clientNum, flags;

		clientNum = ent->client->sess.spectatorClient;

		// team follow1 and team follow2 go to whatever clients are playing
		if ( clientNum == -1 ) {
			clientNum = level.follow1;
		} else if ( clientNum == -2 ) {
			clientNum = level.follow2;
		}
		if ( clientNum >= 0 ) {
			cl = &level.clients[ clientNum ];
			if ( cl->pers.connected == CON_CONNECTED && cl->sess.sessionTeam != TEAM_SPECTATOR ) {
				flags = (cl->ps.eFlags & ~(EF_VOTED | EF_TEAMVOTED)) | (ent->client->ps.eFlags & (EF_VOTED | EF_TEAMVOTED));
				ent->client->ps = cl->ps;
				ent->client->ps.pm_flags |= PMF_FOLLOW;
				ent->client->ps.eFlags = flags;
				return;
			}
		}

		if ( ent->client->ps.pm_flags & PMF_FOLLOW ) {
			// drop them to free spectators unless they are dedicated camera followers
			if ( ent->client->sess.spectatorClient >= 0 ) {
				ent->client->sess.spectatorState = SPECTATOR_FREE;
			}

			ClientBegin( ent->client - level.clients );
		}
	}

	if ( ent->client->sess.spectatorState == SPECTATOR_SCOREBOARD ) {
		ent->client->ps.pm_flags |= PMF_SCOREBOARD;
	} else {
		ent->client->ps.pm_flags &= ~PMF_SCOREBOARD;
	}
}

/*
==============
ClientEndFrame

Called at the end of each server frame for each connected client
A fast client will have multiple ClientThink for each ClientEdFrame,
while a slow client may have multiple ClientEndFrame between ClientThink.
==============
*/
void ClientEndFrame( gentity_t *ent ) {
	int			i;

	if ( ent->client->sess.sessionTeam == TEAM_SPECTATOR ) {
		SpectatorClientEndFrame( ent );
		return;
	}

	// LATTICE: lay one speed-trail point behind this pilot
	G_LatticeEmitTrail( ent );

	// turn off any expired powerups
	for ( i = 0 ; i < MAX_POWERUPS ; i++ ) {
		if ( ent->client->ps.powerups[ i ] < level.time ) {
			ent->client->ps.powerups[ i ] = 0;
		}
	}

#ifdef MISSIONPACK
	// set powerup for player animation
	if( bg_itemlist[ent->client->ps.stats[STAT_PERSISTANT_POWERUP]].giTag == PW_GUARD ) {
		ent->client->ps.powerups[PW_GUARD] = level.time;
	}
	if( bg_itemlist[ent->client->ps.stats[STAT_PERSISTANT_POWERUP]].giTag == PW_SCOUT ) {
		ent->client->ps.powerups[PW_SCOUT] = level.time;
	}
	if( bg_itemlist[ent->client->ps.stats[STAT_PERSISTANT_POWERUP]].giTag == PW_DOUBLER ) {
		ent->client->ps.powerups[PW_DOUBLER] = level.time;
	}
	if( bg_itemlist[ent->client->ps.stats[STAT_PERSISTANT_POWERUP]].giTag == PW_AMMOREGEN ) {
		ent->client->ps.powerups[PW_AMMOREGEN] = level.time;
	}
	if ( ent->client->invulnerabilityTime > level.time ) {
		ent->client->ps.powerups[PW_INVULNERABILITY] = level.time;
	}
#endif

	// save network bandwidth
#if 0
	if ( !g_synchronousClients->integer && ent->client->ps.pm_type == PM_NORMAL ) {
		// FIXME: this must change eventually for non-sync demo recording
		VectorClear( ent->client->ps.viewangles );
	}
#endif

	//
	// If the end of unit layout is displayed, don't give
	// the player any normal movement attributes
	//
	if ( level.intermissiontime ) {
		return;
	}

	// burn from lava, etc
	P_WorldEffects (ent);

	// apply all the damage taken this frame
	P_DamageFeedback (ent);

	// add the EF_CONNECTION flag if we haven't gotten commands recently
	if ( level.time - ent->client->lastCmdTime > 1000 ) {
		ent->client->ps.eFlags |= EF_CONNECTION;
	} else {
		ent->client->ps.eFlags &= ~EF_CONNECTION;
	}

	ent->client->ps.stats[STAT_HEALTH] = ent->health;	// FIXME: get rid of ent->health...

	G_SetClientSound (ent);

	// set the latest infor
	if (g_smoothClients.integer) {
		BG_PlayerStateToEntityStateExtraPolate( &ent->client->ps, &ent->s, ent->client->ps.commandTime, qtrue );
	}
	else {
		BG_PlayerStateToEntityState( &ent->client->ps, &ent->s, qtrue );
	}
	SendPendingPredictableEvents( &ent->client->ps );

	// set the bit for the reachability area the client is currently in
//	i = trap_AAS_PointReachabilityAreaIndex( ent->client->ps.origin );
//	ent->client->areabits[i >> 3] |= 1 << (i & 7);
}


