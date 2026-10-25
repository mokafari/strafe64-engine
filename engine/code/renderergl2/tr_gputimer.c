/*
===========================================================================
STRAFE 64 GPU pass timers (r_gpuTimers).

Back-to-back GL_TIME_ELAPSED queries, one per segment between fixed marks in
the backend frame (Apple's GL-on-Metal returns 0 for GL_TIMESTAMP), read back
GPUT_FRAMES-1 frames later (no stall) and averaged over r_gpuTimers frames, then
printed as one "gpu:" line. Windowed macOS is paced by the compositor, so
com_speeds cannot see renderer cost at all -- these are the numbers to A/B.

Marks are written unconditionally at their call sites, so a pass that is off
reads as ~0 instead of shifting its cost into a neighbour.
===========================================================================
*/
#ifdef USE_LOCAL_HEADERS
#	include "SDL.h"
#else
#	include <SDL.h>
#endif
#include "tr_local.h"

#ifndef GL_TIME_ELAPSED
#define GL_TIME_ELAPSED 0x88BF
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif
#ifndef GL_QUERY_RESULT_AVAILABLE
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#endif

#define GPUT_FRAMES 8

typedef void (APIENTRY *getQueryObjectui64vProc)(GLuint id, GLenum pname, GLuint64 *params);

static getQueryObjectui64vProc qglGetQueryObjectui64v;

static const char *gputNames[GPUT_COUNT] = {
	"frame", "scene", "tonemap", "bloom", "dof", "hud", "present"
};

static GLuint   gputQuery[GPUT_FRAMES][GPUT_COUNT];
static qboolean gputIssued[GPUT_FRAMES][GPUT_COUNT];
static int      gputFrame;
static double   gputSum[GPUT_COUNT];
static int      gputSamples;
static qboolean gputReady;
static qboolean gputTried;
static int      gputOpen = -1;	// segment whose query is running

void R_GpuTimerInit(void)
{
	gputReady = qfalse;
	gputTried = qtrue;

	qglGetQueryObjectui64v = (getQueryObjectui64vProc)SDL_GL_GetProcAddress("glGetQueryObjectui64v");
	if (!qglGetQueryObjectui64v || !qglGenQueries)
	{
		ri.Printf(PRINT_WARNING, "r_gpuTimers: GL_ARB_timer_query not available\n");
		return;
	}

	qglGenQueries(GPUT_FRAMES * GPUT_COUNT, &gputQuery[0][0]);
	Com_Memset(gputIssued, 0, sizeof(gputIssued));
	Com_Memset(gputSum, 0, sizeof(gputSum));
	gputSamples = 0;
	gputFrame = 0;
	gputReady = qtrue;
}

void R_GpuTimerShutdown(void)
{
	if (gputReady)
		qglDeleteQueries(GPUT_FRAMES * GPUT_COUNT, &gputQuery[0][0]);
	gputReady = qfalse;
	gputTried = qfalse;
}

void R_GpuMark(gpuTimerMark_t mark)
{
	int f;

	if (!gputReady || r_gpuTimers->integer <= 0)
		return;

	f = gputFrame % GPUT_FRAMES;

	// first write of a mark wins: extra 3D views (portals, killcam insets) keep
	// the main view's numbers
	if (gputIssued[f][mark])
		return;

	// mark N closes segment N (slot N) and opens segment N+1; GPUT_FRAME has no
	// segment of its own, so slot 0 is free to hold the frame total
	if (gputOpen >= 0)
	{
		qglEndQuery(GL_TIME_ELAPSED);
		gputIssued[f][gputOpen] = qtrue;
		gputOpen = -1;
	}

	gputIssued[f][mark] = qtrue;

	if (mark + 1 < GPUT_COUNT)
	{
		qglBeginQuery(GL_TIME_ELAPSED, gputQuery[f][mark + 1]);
		gputOpen = mark + 1;
	}
}

// call once per frame, after the present
void R_GpuTimerEndFrame(void)
{
	int f, i;
	GLuint64 t[GPUT_COUNT];
	GLuint avail;

	if (!gputReady)
	{
		// created lazily so the cvar works live, without a vid_restart
		if (r_gpuTimers->integer > 0 && !gputTried)
			R_GpuTimerInit();
		return;
	}

	if (gputOpen >= 0)
	{
		qglEndQuery(GL_TIME_ELAPSED);
		gputOpen = -1;
	}

	if (r_gpuTimers->integer <= 0)
		return;

	gputFrame++;

	// collect the oldest frame in the ring (written GPUT_FRAMES-1 frames ago)
	f = gputFrame % GPUT_FRAMES;
	t[0] = 0;
	for (i = 1; i < GPUT_COUNT; i++)
	{
		if (!gputIssued[f][i])
			break;
		qglGetQueryObjectuiv(gputQuery[f][i], GL_QUERY_RESULT_AVAILABLE, &avail);
		if (!avail)
			break;
		qglGetQueryObjectui64v(gputQuery[f][i], GL_QUERY_RESULT, &t[i]);
	}

	if (i == GPUT_COUNT)
	{
		for (i = 1; i < GPUT_COUNT; i++)
		{
			gputSum[i] += (double)t[i];
			gputSum[0] += (double)t[i];
		}
		gputSamples++;
	}

	for (i = 0; i < GPUT_COUNT; i++)
		gputIssued[f][i] = qfalse;

	if (gputSamples >= r_gpuTimers->integer)
	{
		char line[256];
		int  len;

		len = Com_sprintf(line, sizeof(line), "gpu:");
		for (i = 1; i < GPUT_COUNT; i++)
			len += Com_sprintf(line + len, sizeof(line) - len, " %s %.3f", gputNames[i], gputSum[i] / gputSamples * 1e-6);
		Com_sprintf(line + len, sizeof(line) - len, " total %.3f ms\n", gputSum[0] / gputSamples * 1e-6);
		ri.Printf(PRINT_ALL, "%s", line);

		Com_Memset(gputSum, 0, sizeof(gputSum));
		gputSamples = 0;
	}
}
