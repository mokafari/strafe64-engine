/* Included by sv_main.c. Local-only authoritative movement/render bridge. */
#include "../qcommon/slowmo_clock.h"
static smClock_t smClock;
static slowmoStep_t smStep;
static slowmoPose_t smPoses[MAX_CLIENTS];
static int smFlags[MAX_CLIENTS], smFrameMsec, smEnabled;

qboolean SV_SlowmoActive(void) {
    return smEnabled && sv.state == SS_GAME && !sv.restarting;
}
void SV_SlowmoInit(void) {
    int i;
    cvar_t *mode = Cvar_Get("com_slowmo", "0", CVAR_ARCHIVE | CVAR_LATCH);
    smEnabled = mode->integer && !com_dedicated->integer
        && Cvar_VariableIntegerValue("vm_game") == 0
        && Cvar_VariableIntegerValue("vm_cgame") == 0;
    for (i=0; i<sv_maxclients->integer; i++) {
        client_t *client = &svs.clients[i];
        if (client->state >= CS_CONNECTED && client->netchan.remoteAddress.type != NA_LOOPBACK
            && client->netchan.remoteAddress.type != NA_BOT) smEnabled = 0;
        client->lastSnapshotTime = 0;
    }
    SM_Init(&smClock, sv.time);
    memset(&smStep, 0, sizeof(smStep));
    memset(smPoses, 0, sizeof(smPoses));
    memset(smFlags, 0, sizeof(smFlags));
    smFrameMsec = 0;
    if (smEnabled) {
#ifndef DEDICATED
        CL_PrepareSlowmo();
#endif
        Cvar_Set("timescale", "1");
        Com_Printf("SLOWMO experimental: local native 125Hz, fractional movement; remote clients/demos disabled\n");
    } else if (mode->integer) {
        Com_Printf("SLOWMO unavailable: requires a local server and native game/cgame\n");
    }
}
int SV_SlowmoFrameMsec(void) { return smFrameMsec; }
int SV_SlowmoRealTime(void) { return smClock.tick * 8; }
void SV_SlowmoGetStep(slowmoStep_t *step) {
    *step = smStep;
    step->active = SV_SlowmoActive();
}
void SV_SlowmoRequest(int owner, float target, float rate) {
    if (SV_SlowmoActive()) SM_Request(&smClock, owner, target, rate);
}
qboolean SV_SlowmoGetRender(int clientNum, slowmoRender_t *frame) {
    memset(frame, 0, sizeof(*frame));
    if (!SV_SlowmoActive() || !smStep.tick || clientNum < 0
        || clientNum >= sv_maxclients->integer || svs.clients[clientNum].state != CS_ACTIVE
        || svs.clients[clientNum].netchan.remoteAddress.type != NA_LOOPBACK) return qfalse;
    frame->active = 1;
    frame->tick = smStep.tick;
    frame->scale = (float)smClock.scale;
    frame->alpha = (float)(smClock.accumulator / SM_REAL_STEP_MS);
    frame->ps = *SV_GameClientNum(clientNum);
    memcpy(frame->poses, smPoses, sizeof(smPoses));
    return qtrue;
}
static void SV_SlowmoCapture(void) {
    int i;
    for (i = 0; i < MAX_CLIENTS; i++) {
        playerState_t *ps;
        slowmoPose_t *pose = &smPoses[i];
        if (i >= sv_maxclients->integer || svs.clients[i].state != CS_ACTIVE) {
            pose->valid = 0; continue;
        }
        ps = SV_GameClientNum(i);
        VectorCopy(pose->current, pose->previous);
        VectorCopy(ps->origin, pose->current);
        if (!pose->valid || ((ps->eFlags ^ smFlags[i]) & EF_TELEPORT_BIT)
            || Distance(pose->current, pose->previous) > 120.0f) {
            VectorCopy(pose->current, pose->previous);
        }
        smFlags[i] = ps->eFlags;
        pose->valid = 1;
    }
}
static void SV_SlowmoRun(int realMsec) {
    double delta;
    int before, clientNum;
    usercmd_t cmd;
    smFrameMsec = 0;
    SM_Accumulate(&smClock, realMsec);
    while (1) {
        before = (int)smClock.worldMs;
        if (!SM_Step(&smClock, &delta)) break;
        smStep.active = 1;
        smStep.tick = smClock.tick;
        smStep.worldMsec = (float)delta;
        smStep.wholeMsec = (int)smClock.worldMs - before;
        smStep.scale = (float)smClock.scale;
        smFrameMsec += smStep.wholeMsec;
        sv.time = (int)smClock.worldMs;
        svs.time += smStep.wholeMsec;
        SV_BotFrame(sv.time);
#ifndef DEDICATED
        if (CL_LocalSlowmoCommand(&clientNum, &cmd) && clientNum >= 0
            && clientNum < sv_maxclients->integer && svs.clients[clientNum].state == CS_ACTIVE
            && svs.clients[clientNum].netchan.remoteAddress.type == NA_LOOPBACK) {
            cmd.serverTime = sv.time;
            svs.clients[clientNum].lastUsercmd = cmd;
            VM_Call(gvm, GAME_CLIENT_THINK, clientNum);
        }
#endif
        VM_Call(gvm, GAME_RUN_FRAME, sv.time);
        SV_SlowmoCapture();
        if (Cvar_VariableIntegerValue("com_slowmoTrace") && smStep.tick % 16 == 0) {
            Com_Printf("SM_CLOCK tick=%d world=%.6f dt=%.6f scale=%.6f target=%.4f owner=%d droppedRealMs=%.1f\n",
                smStep.tick, smClock.worldMs, delta, smClock.scale, smClock.target, smClock.owner, smClock.droppedMs);
            for (clientNum=0; clientNum<sv_maxclients->integer; clientNum++) {
                if (smPoses[clientNum].valid) Com_Printf("SM_POSE tick=%d client=%d xyz=%.6f,%.6f,%.6f step=%.6f\n",
                    smStep.tick, clientNum, smPoses[clientNum].current[0], smPoses[clientNum].current[1],
                    smPoses[clientNum].current[2], Distance(smPoses[clientNum].current, smPoses[clientNum].previous));
            }
        }
    }
    if (cvar_modifiedFlags & CVAR_SERVERINFO) {
        SV_SetConfigstring(CS_SERVERINFO, Cvar_InfoString(CVAR_SERVERINFO));
        cvar_modifiedFlags &= ~CVAR_SERVERINFO;
    }
    if (cvar_modifiedFlags & CVAR_SYSTEMINFO) {
        SV_SetConfigstring(CS_SYSTEMINFO, Cvar_InfoString_Big(CVAR_SYSTEMINFO));
        cvar_modifiedFlags &= ~CVAR_SYSTEMINFO;
    }
    com_effectiveTimeScale = (float)smClock.scale;
}
