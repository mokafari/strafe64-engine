/* Exercise production clock and pmove, including sub-millisecond movement. */
#include "../../engine/code/qcommon/q_shared.h"
#include "../../engine/code/game/bg_public.h"
#include "../../engine/code/qcommon/slowmo_clock.h"
static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)
void QDECL Com_Printf(const char *fmt, ...) {}
void QDECL Com_Error(int level, const char *fmt, ...) { abort(); }
void trap_SnapVector(float *v) { int i; for (i=0;i<3;i++) v[i]=(float)(int)v[i]; }
static void emptyTrace(trace_t *t, const vec3_t start, const vec3_t mins,
    const vec3_t maxs, const vec3_t end, int pass, int mask) {
    memset(t,0,sizeof(*t)); t->fraction=1; t->entityNum=ENTITYNUM_NONE;
    VectorCopy(end,t->endpos);
}
static void floorTrace(trace_t *t, const vec3_t start, const vec3_t mins,
    const vec3_t maxs, const vec3_t end, int pass, int mask) {
    float from=start[2]+mins[2], to=end[2]+mins[2];
    emptyTrace(t,start,mins,maxs,end,pass,mask);
    if (to<0 && from>=0) {
        t->fraction=from/(from-to);
        VectorSubtract(end,start,t->endpos);
        VectorMA(start,t->fraction,t->endpos,t->endpos);
        t->plane.normal[2]=1; t->entityNum=ENTITYNUM_WORLD;
    }
}
static int emptyContents(const vec3_t p, int pass) { return 0; }
static void setup(pmove_t *p, playerState_t *ps) {
    memset(p,0,sizeof(*p)); memset(ps,0,sizeof(*ps));
    p->ps=ps; p->trace=emptyTrace; p->pointcontents=emptyContents;
    p->fractionalStep=qtrue; p->timeScale=1;
    ps->pm_type=PM_NORMAL; ps->gravity=800; ps->speed=320;
    ps->stats[STAT_HEALTH]=100; ps->groundEntityNum=ENTITYNUM_NONE;
    ps->origin[2]=1000;
}
static void step(pmove_t *p, smClock_t *c) {
    int before=(int)c->worldMs;
    p->worldMsec=(float)SM_Integrate(c,8);
    p->wholeMsec=(int)c->worldMs-before;
    p->cmd.serverTime=(int)c->worldMs;
    Pmove(p);
}
int main(int argc,char **argv) {
    smClock_t a,b; pmove_t p; playerState_t ps;
    double d,previous; int i,n=0,total=0;
    const char *test=argc>1?argv[1]:"";
    SM_Init(&a,0); SM_Init(&b,0); setup(&p,&ps);
    if (!strcmp(test,"integration")) {
        SM_Request(&a,1,.04,18); SM_Request(&b,1,.04,18);
        SM_Integrate(&a,1000);
        for(i=0;i<125;i++) SM_Integrate(&b,8);
        CHECK(fabs(a.worldMs-b.worldMs)<1e-9);
        CHECK(fabs(a.scale-b.scale)<1e-12);
        CHECK(a.worldMs>40 && a.worldMs<100);
    } else if (!strcmp(test,"cadence")) {
        SM_Request(&a,1,.04,0);
        for(i=0;i<1000;i++) { SM_Accumulate(&a,1); while(SM_Step(&a,&d)) { n++; CHECK(fabs(d-.32)<1e-9); } }
        CHECK(n==125); CHECK(fabs(a.worldMs-40)<1e-9);
        CHECK(a.accumulator==0 && a.droppedMs==0);
    } else if (!strcmp(test,"ownership")) {
        SM_Request(&a,1,.04,18); SM_Request(&a,2,.1,0);
        SM_Request(&a,1,1,40); CHECK(a.target==.1 && a.owner==2);
        SM_Request(&a,0,1,30); CHECK(a.target==1 && a.rate==40 && a.owner==1);
        SM_Request(&a,1,NAN,18); SM_Request(&a,1,.04,INFINITY);
        CHECK(a.target==1 && a.rate==40);
    } else if (!strcmp(test,"overload")) {
        SM_Accumulate(&a,1000); while(SM_Step(&a,&d)) n++;
        CHECK(n==16 && a.droppedMs==872 && a.worldMs==128);
        SM_Accumulate(&a,-2); CHECK(!SM_Step(&a,&d));
    } else if (!strcmp(test,"movement")) {
        SM_Request(&a,1,.04,0); ps.velocity[0]=100;
        for(i=0;i<125;i++) { previous=ps.origin[0]; step(&p,&a); CHECK(ps.origin[0]>previous); CHECK(ps.origin[0]-previous<.033); }
        CHECK(fabs(ps.origin[0]-4)<.0001);
        CHECK(ps.commandTime==39 || ps.commandTime==40);
    } else if (!strcmp(test,"grounded")) {
        float bob=0;
        p.trace=floorTrace; p.bobFraction=&bob; p.cmd.forwardmove=127;
        ps.origin[2]=24; ps.velocity[0]=100;
        SM_Request(&a,1,.04,0);
        for(i=0;i<125;i++) { previous=ps.origin[0]; step(&p,&a); CHECK(ps.origin[0]>previous); }
        CHECK(ps.groundEntityNum==ENTITYNUM_WORLD);
        CHECK(fabs(ps.origin[2]-24)<.01 && ps.velocity[0]>150);
        CHECK(ps.bobCycle>=15 && ps.bobCycle<=17);
    } else if (!strcmp(test,"frame-rates")) {
        int fpsList[]={30,60,144,240}, k, elapsed, next;
        for(k=0;k<4;k++) {
            SM_Init(&a,0); SM_Request(&a,1,.04,0); elapsed=0;
            for(i=1;i<=fpsList[k];i++) {
                next=i*1000/fpsList[k]; SM_Accumulate(&a,next-elapsed); elapsed=next;
                while(SM_Step(&a,&d)) {}
            }
            CHECK(a.tick==125 && fabs(a.worldMs-40)<1e-9 && a.droppedMs==0);
        }
    } else if (!strcmp(test,"gravity")) {
        SM_Request(&a,1,.04,0);
        for(i=0;i<125;i++) step(&p,&a);
        CHECK(fabs(ps.velocity[2]+32)<.001);
        CHECK(fabs(ps.origin[2]-999.36)<.01);
    } else if (!strcmp(test,"timers")) {
        SM_Request(&a,1,.04,0); ps.legsTimer=100; ps.torsoTimer=100;
        ps.stats[STAT_JUMP_MS]=100;
        for(i=0;i<125;i++) { step(&p,&a); total+=p.wholeMsec; }
        CHECK(total>=39 && total<=40);
        CHECK(ps.legsTimer==100-total && ps.torsoTimer==100-total);
        CHECK(ps.stats[STAT_JUMP_MS]==100-total);
    } else if (!strcmp(test,"angles")) {
        SM_Request(&a,1,.04,0); p.cmd.angles[YAW]=ANGLE2SHORT(90);
        step(&p,&a); CHECK(fabs(ps.viewangles[YAW]-90)<.01);
        p.cmd.angles[YAW]=ANGLE2SHORT(180); step(&p,&a);
        CHECK(fabs(AngleNormalize180(ps.viewangles[YAW]-180))<.01 && ps.commandTime==0);
    } else if (!strcmp(test,"reset")) {
        SM_Request(&a,2,.04,18); SM_Accumulate(&a,1000); SM_Step(&a,&d);
        SM_Init(&a,1200); CHECK(a.worldMs==1200 && a.scale==1 && a.owner==0);
        CHECK(a.tick==0 && a.accumulator==0 && a.droppedMs==0);
    } else CHECK(0);
    return failures?1:0;
}

