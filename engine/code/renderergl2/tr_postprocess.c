/*
===========================================================================
Copyright (C) 2011 Andrei Drexler, Richard Allen, James Canete

This file is part of Reaction source code.

Reaction source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Reaction source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Reaction source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#include "tr_local.h"

void RB_ToneMap(FBO_t *hdrFbo, ivec4_t hdrBox, FBO_t *ldrFbo, ivec4_t ldrBox, int autoExposure)
{
	ivec4_t srcBox, dstBox;
	vec4_t color;
	static int lastFrameCount = 0;

	if (autoExposure)
	{
		if (lastFrameCount == 0 || tr.frameCount < lastFrameCount || tr.frameCount - lastFrameCount > 5)
		{
			// determine average log luminance
			FBO_t *srcFbo, *dstFbo, *tmp;
			int size = 256;

			lastFrameCount = tr.frameCount;

			VectorSet4(dstBox, 0, 0, size, size);

			FBO_Blit(hdrFbo, hdrBox, NULL, tr.textureScratchFbo[0], dstBox, &tr.calclevels4xShader[0], NULL, 0);

			srcFbo = tr.textureScratchFbo[0];
			dstFbo = tr.textureScratchFbo[1];

			// downscale to 1x1 texture
			while (size > 1)
			{
				VectorSet4(srcBox, 0, 0, size, size);
				//size >>= 2;
				size >>= 1;
				VectorSet4(dstBox, 0, 0, size, size);

				if (size == 1)
					dstFbo = tr.targetLevelsFbo;

				//FBO_Blit(targetFbo, srcBox, NULL, tr.textureScratchFbo[nextScratch], dstBox, &tr.calclevels4xShader[1], NULL, 0);
				FBO_FastBlit(srcFbo, srcBox, dstFbo, dstBox, GL_COLOR_BUFFER_BIT, GL_LINEAR);

				tmp = srcFbo;
				srcFbo = dstFbo;
				dstFbo = tmp;
			}
		}

		// blend with old log luminance for gradual change
		VectorSet4(srcBox, 0, 0, 0, 0);

		color[0] = 
		color[1] =
		color[2] = 1.0f;
		if (glRefConfig.textureFloat)
			color[3] = 0.03f;
		else
			color[3] = 0.1f;

		FBO_Blit(tr.targetLevelsFbo, srcBox, NULL, tr.calcLevelsFbo, NULL,  NULL, color, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
	}

	// tonemap
	color[0] =
	color[1] =
	color[2] = pow(2, r_cameraExposure->value - autoExposure); //exp2(r_cameraExposure->value);
	color[3] = 1.0f;

	if (autoExposure)
		GL_BindToTMU(tr.calcLevelsImage,  TB_LEVELSMAP);
	else
		GL_BindToTMU(tr.fixedLevelsImage, TB_LEVELSMAP);

	FBO_Blit(hdrFbo, hdrBox, NULL, ldrFbo, ldrBox, &tr.tonemapShader, color, 0);
}

/*
=============
RB_BokehBlur


Blurs a part of one framebuffer to another.

Framebuffers can be identical. 
=============
*/
void RB_BokehBlur(FBO_t *src, ivec4_t srcBox, FBO_t *dst, ivec4_t dstBox, float blur)
{
//	ivec4_t srcBox, dstBox;
	vec4_t color;
	
	blur *= 10.0f;

	if (blur < 0.004f)
		return;

	if (glRefConfig.framebufferObject)
	{
		// bokeh blur
		if (blur > 0.0f)
		{
			ivec4_t quarterBox;

			quarterBox[0] = 0;
			quarterBox[1] = tr.quarterFbo[0]->height;
			quarterBox[2] = tr.quarterFbo[0]->width;
			quarterBox[3] = -tr.quarterFbo[0]->height;

			// create a quarter texture
			//FBO_Blit(NULL, NULL, NULL, tr.quarterFbo[0], NULL, NULL, NULL, 0);
			FBO_FastBlit(src, srcBox, tr.quarterFbo[0], quarterBox, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		}

#ifndef HQ_BLUR
		if (blur > 1.0f)
		{
			// create a 1/16th texture
			//FBO_Blit(tr.quarterFbo[0], NULL, NULL, tr.textureScratchFbo[0], NULL, NULL, NULL, 0);
			FBO_FastBlit(tr.quarterFbo[0], NULL, tr.textureScratchFbo[0], NULL, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		}
#endif

		if (blur > 0.0f && blur <= 1.0f)
		{
			// Crossfade original with quarter texture
			VectorSet4(color, 1, 1, 1, blur);

			FBO_Blit(tr.quarterFbo[0], NULL, NULL, dst, dstBox, NULL, color, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
		}
#ifndef HQ_BLUR
		// ok blur, but can see some pixelization
		else if (blur > 1.0f && blur <= 2.0f)
		{
			// crossfade quarter texture with 1/16th texture
			FBO_Blit(tr.quarterFbo[0], NULL, NULL, dst, dstBox, NULL, NULL, 0);

			VectorSet4(color, 1, 1, 1, blur - 1.0f);

			FBO_Blit(tr.textureScratchFbo[0], NULL, NULL, dst, dstBox, NULL, color, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
		}
		else if (blur > 2.0f)
		{
			// blur 1/16th texture then replace
			int i;

			for (i = 0; i < 2; i++)
			{
				vec2_t blurTexScale;
				float subblur;

				subblur = ((blur - 2.0f) / 2.0f) / 3.0f * (float)(i + 1);

				blurTexScale[0] =
				blurTexScale[1] = subblur;

				color[0] =
				color[1] =
				color[2] = 0.5f;
				color[3] = 1.0f;

				if (i != 0)
					FBO_Blit(tr.textureScratchFbo[0], NULL, blurTexScale, tr.textureScratchFbo[1], NULL, &tr.bokehShader, color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
				else
					FBO_Blit(tr.textureScratchFbo[0], NULL, blurTexScale, tr.textureScratchFbo[1], NULL, &tr.bokehShader, color, 0);
			}

			FBO_Blit(tr.textureScratchFbo[1], NULL, NULL, dst, dstBox, NULL, NULL, 0);
		}
#else // higher quality blur, but slower
		else if (blur > 1.0f)
		{
			// blur quarter texture then replace
			int i;

			src = tr.quarterFbo[0];
			dst = tr.quarterFbo[1];

			VectorSet4(color, 0.5f, 0.5f, 0.5f, 1);

			for (i = 0; i < 2; i++)
			{
				vec2_t blurTexScale;
				float subblur;

				subblur = (blur - 1.0f) / 2.0f * (float)(i + 1);

				blurTexScale[0] =
				blurTexScale[1] = subblur;

				color[0] =
				color[1] =
				color[2] = 1.0f;
				if (i != 0)
					color[3] = 1.0f;
				else
					color[3] = 0.5f;

				FBO_Blit(tr.quarterFbo[0], NULL, blurTexScale, tr.quarterFbo[1], NULL, &tr.bokehShader, color, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
			}

			FBO_Blit(tr.quarterFbo[1], NULL, NULL, dst, dstBox, NULL, NULL, 0);
		}
#endif
	}
}


static void RB_RadialBlur(FBO_t *srcFbo, FBO_t *dstFbo, int passes, float stretch, float x, float y, float w, float h, float xcenter, float ycenter, float alpha)
{
	ivec4_t srcBox, dstBox;
	int srcWidth, srcHeight;
	vec4_t color;
	const float inc = 1.f / passes;
	const float mul = powf(stretch, inc);
	float scale;

	alpha *= inc;
	VectorSet4(color, alpha, alpha, alpha, 1.0f);

	srcWidth  = srcFbo ? srcFbo->width  : glConfig.vidWidth;
	srcHeight = srcFbo ? srcFbo->height : glConfig.vidHeight;

	VectorSet4(srcBox, 0, 0, srcWidth, srcHeight);

	VectorSet4(dstBox, x, y, w, h);
	FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, NULL, color, 0);

	--passes;
	scale = mul;
	while (passes > 0)
	{
		float iscale = 1.f / scale;
		float s0 = xcenter * (1.f - iscale);
		float t0 = (1.0f - ycenter) * (1.f - iscale);

		srcBox[0] = s0 * srcWidth;
		srcBox[1] = t0 * srcHeight;
		srcBox[2] = iscale * srcWidth;
		srcBox[3] = iscale * srcHeight;
			
		FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, NULL, color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE );

		scale *= mul;
		--passes;
	}
}


static qboolean RB_UpdateSunFlareVis(void)
{
	GLuint sampleCount = 0;
	if (!glRefConfig.occlusionQuery)
		return qtrue;

	tr.sunFlareQueryIndex ^= 1;
	if (!tr.sunFlareQueryActive[tr.sunFlareQueryIndex])
		return qtrue;

	/* debug code */
	if (0)
	{
		int iter;
		for (iter=0 ; ; ++iter)
		{
			GLint available = 0;
			qglGetQueryObjectiv(tr.sunFlareQuery[tr.sunFlareQueryIndex], GL_QUERY_RESULT_AVAILABLE, &available);
			if (available)
				break;
		}

		ri.Printf(PRINT_DEVELOPER, "Waited %d iterations\n", iter);
	}
	
	// Note: On desktop OpenGL this is a sample count (glRefConfig.occlusionQueryTarget == GL_SAMPLES_PASSED)
	// but on OpenGL ES this is a boolean (glRefConfig.occlusionQueryTarget == GL_ANY_SAMPLES_PASSED)
	qglGetQueryObjectuiv(tr.sunFlareQuery[tr.sunFlareQueryIndex], GL_QUERY_RESULT, &sampleCount);
	return sampleCount > 0;
}

void RB_SunRays(FBO_t *srcFbo, ivec4_t srcBox, FBO_t *dstFbo, ivec4_t dstBox)
{
	vec4_t color;
	float dot;
	const float cutoff = 0.25f;
	qboolean colorize = qtrue;

//	float w, h, w2, h2;
	mat4_t mvp;
	vec4_t pos, hpos;

	dot = DotProduct(tr.sunDirection, backEnd.viewParms.or.axis[0]);
	if (dot < cutoff)
		return;

	if (!RB_UpdateSunFlareVis())
		return;

	// From RB_DrawSun()
	{
		float dist;
		mat4_t trans, model;

		Mat4Translation( backEnd.viewParms.or.origin, trans );
		Mat4Multiply( backEnd.viewParms.world.modelMatrix, trans, model );
		Mat4Multiply(backEnd.viewParms.projectionMatrix, model, mvp);

		dist = backEnd.viewParms.zFar / 1.75;		// div sqrt(3)

		VectorScale( tr.sunDirection, dist, pos );
	}

	// project sun point
	//Mat4Multiply(backEnd.viewParms.projectionMatrix, backEnd.viewParms.world.modelMatrix, mvp);
	Mat4Transform(mvp, pos, hpos);

	// transform to UV coords
	hpos[3] = 0.5f / hpos[3];

	pos[0] = 0.5f + hpos[0] * hpos[3];
	pos[1] = 0.5f + hpos[1] * hpos[3];

	// initialize quarter buffers
	{
		float mul = 1.f;
		ivec4_t rayBox, quarterBox;
		int srcWidth  = srcFbo ? srcFbo->width  : glConfig.vidWidth;
		int srcHeight = srcFbo ? srcFbo->height : glConfig.vidHeight;

		VectorSet4(color, mul, mul, mul, 1);

		rayBox[0] = srcBox[0] * tr.sunRaysFbo->width  / srcWidth;
		rayBox[1] = srcBox[1] * tr.sunRaysFbo->height / srcHeight;
		rayBox[2] = srcBox[2] * tr.sunRaysFbo->width  / srcWidth;
		rayBox[3] = srcBox[3] * tr.sunRaysFbo->height / srcHeight;

		quarterBox[0] = 0;
		quarterBox[1] = tr.quarterFbo[0]->height;
		quarterBox[2] = tr.quarterFbo[0]->width;
		quarterBox[3] = -tr.quarterFbo[0]->height;

		// first, downsample the framebuffer
		if (colorize)
		{
			FBO_FastBlit(srcFbo, srcBox, tr.quarterFbo[0], quarterBox, GL_COLOR_BUFFER_BIT, GL_LINEAR);
			FBO_Blit(tr.sunRaysFbo, rayBox, NULL, tr.quarterFbo[0], quarterBox, NULL, color, GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO);
		}
		else
		{
			FBO_FastBlit(tr.sunRaysFbo, rayBox, tr.quarterFbo[0], quarterBox, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		}
	}

	// radial blur passes, ping-ponging between the two quarter-size buffers
	{
		const float stretch_add = 2.f/3.f;
		float stretch = 1.f + stretch_add;
		int i;
		for (i=0; i<2; ++i)
		{
			RB_RadialBlur(tr.quarterFbo[i&1], tr.quarterFbo[(~i) & 1], 5, stretch, 0.f, 0.f, tr.quarterFbo[0]->width, tr.quarterFbo[0]->height, pos[0], pos[1], 1.125f);
			stretch += stretch_add;
		}
	}
	
	// add result back on top of the main buffer
	{
		float mul = 1.f;

		VectorSet4(color, mul, mul, mul, 1);

		FBO_Blit(tr.quarterFbo[0], NULL, NULL, dstFbo, dstBox, NULL, color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
	}
}

static void RB_BlurAxis(FBO_t *srcFbo, FBO_t *dstFbo, float strength, qboolean horizontal)
{
	float dx, dy;
	float xmul, ymul;
	float weights[3] = {
		0.227027027f,
		0.316216216f,
		0.070270270f,
	};
	float offsets[3] = {
		0.f,
		1.3846153846f,
		3.2307692308f,
	};

	xmul = horizontal;
	ymul = 1.f - xmul;

	xmul *= strength;
	ymul *= strength;

	{
		ivec4_t srcBox, dstBox;
		vec4_t color;

		VectorSet4(color, weights[0], weights[0], weights[0], 1.0f);
		VectorSet4(srcBox, 0, 0, srcFbo->width, srcFbo->height);
		VectorSet4(dstBox, 0, 0, dstFbo->width, dstFbo->height);
		FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, NULL, color, 0);

		VectorSet4(color, weights[1], weights[1], weights[1], 1.0f);
		dx = offsets[1] * xmul;
		dy = offsets[1] * ymul;
		VectorSet4(srcBox, dx, dy, srcFbo->width, srcFbo->height);
		FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, NULL, color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
		VectorSet4(srcBox, -dx, -dy, srcFbo->width, srcFbo->height);
		FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, NULL, color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);

		VectorSet4(color, weights[2], weights[2], weights[2], 1.0f);
		dx = offsets[2] * xmul;
		dy = offsets[2] * ymul;
		VectorSet4(srcBox, dx, dy, srcFbo->width, srcFbo->height);
		FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, NULL, color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
		VectorSet4(srcBox, -dx, -dy, srcFbo->width, srcFbo->height);
		FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, NULL, color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
	}
}

static void RB_HBlur(FBO_t *srcFbo, FBO_t *dstFbo, float strength)
{
	RB_BlurAxis(srcFbo, dstFbo, strength, qtrue);
}

static void RB_VBlur(FBO_t *srcFbo, FBO_t *dstFbo, float strength)
{
	RB_BlurAxis(srcFbo, dstFbo, strength, qfalse);
}

void RB_GaussianBlur(FBO_t *srcFbo, FBO_t *dstFbo, float blur)
{
	//float mul = 1.f;
	float factor = Com_Clamp(0.f, 1.f, blur);

	if (factor <= 0.f)
		return;

	{
		ivec4_t srcBox, dstBox;
		vec4_t color;

		VectorSet4(color, 1, 1, 1, 1);

		// first, downsample the framebuffer
		FBO_FastBlit(srcFbo, NULL, tr.quarterFbo[0], NULL, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		FBO_FastBlit(tr.quarterFbo[0], NULL, tr.textureScratchFbo[0], NULL, GL_COLOR_BUFFER_BIT, GL_LINEAR);

		// set the alpha channel
		qglColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
		FBO_BlitFromTexture(tr.whiteImage, NULL, NULL, tr.textureScratchFbo[0], NULL, NULL, color, GLS_DEPTHTEST_DISABLE);
		qglColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

		// blur the tiny buffer horizontally and vertically
		RB_HBlur(tr.textureScratchFbo[0], tr.textureScratchFbo[1], factor);
		RB_VBlur(tr.textureScratchFbo[1], tr.textureScratchFbo[0], factor);

		// finally, merge back to framebuffer
		VectorSet4(srcBox, 0, 0, tr.textureScratchFbo[0]->width, tr.textureScratchFbo[0]->height);
		VectorSet4(dstBox, 0, 0, glConfig.vidWidth,              glConfig.vidHeight);
		color[3] = factor;
		FBO_Blit(tr.textureScratchFbo[0], srcBox, NULL, dstFbo, dstBox, NULL, color, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
	}
}


/*
=============
RB_Bloom

STRAFE 64 neon bloom. The scene is dark with bright vertex-colored neon and a
luminous sky, so a cheap soft-knee bloom reads beautifully: downsample, square
the colour for a soft bright-pass (dark midtones collapse toward black, bright
highlights survive — no dedicated threshold shader needed), separable-blur the
survivors to a wide glow, then add it back over the scene. Built entirely from
the existing FBO blit + blur primitives, so it needs no new GLSL program and is
live-tunable (r_bloom intensity / r_bloomBlur spread, neither latched).
=============
*/
void RB_Bloom(FBO_t *srcFbo, ivec4_t srcBox)
{
	// CRITICAL: the whole chain below relies on an EVEN count of FBO_Blit
	// (Y-flipping) blits so the glow composites right-side-up. Do not swap any
	// FBO_Blit here for FBO_FastBlit (or vice versa) without re-counting the
	// flips — see the parity note on the downsample step below.
	vec4_t color;
	float intensity = r_bloom->value;
	float blur = r_bloomBlur->value;
	int i;

	if (!glRefConfig.framebufferObject || intensity <= 0.0f)
		return;

	if (blur < 0.0f)
		blur = 0.0f;

	// 1. downsample the (tonemapped) scene into the quarter buffer: [0] = c
	//    Use FBO_Blit (a flipping textured-quad blit), NOT FBO_FastBlit
	//    (glBlitFramebuffer, which preserves Y). The whole bright-pass + blur
	//    chain below is built from FBO_Blit and the final additive composite
	//    (step 4) is a single FBO_Blit = one net vertical flip. Capturing the
	//    scene here with the same flipping primitive makes the flip count EVEN,
	//    so the glow lands right-side-up. With FastBlit the parity was odd and
	//    the bloom was added back upside-down — bright floors/ramps ghosting up
	//    into the sky.
	FBO_Blit(srcFbo, srcBox, NULL, tr.quarterFbo[0], NULL, NULL, NULL, 0);

	// 2. bright-pass by raising the colour to the 4th power, done with two
	//    multiplicative self-blits (dst*src). c^4 is a hard soft-knee: only
	//    near-white neon / luminous highlights survive, while the medium-bright
	//    sky and walls collapse toward black so they don't haze. No threshold
	//    shader needed.
	//    [1] = c ; [1] *= [0]  ->  c^2
	FBO_Blit(tr.quarterFbo[0], NULL, NULL, tr.quarterFbo[1], NULL, NULL, NULL, 0);
	FBO_Blit(tr.quarterFbo[0], NULL, NULL, tr.quarterFbo[1], NULL, NULL, NULL,
	         GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO);
	//    [0] = c^2 ; [0] *= [1] ->  c^4
	FBO_Blit(tr.quarterFbo[1], NULL, NULL, tr.quarterFbo[0], NULL, NULL, NULL, 0);
	FBO_Blit(tr.quarterFbo[1], NULL, NULL, tr.quarterFbo[0], NULL, NULL, NULL,
	         GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO);

	// 3. separable gaussian blur, ping-ponging the quarter buffers; highlights
	//    start in [0], two passes widen and smooth the glow, result back in [0]
	for (i = 0; i < 2; i++)
	{
		RB_HBlur(tr.quarterFbo[0], tr.quarterFbo[1], blur);
		RB_VBlur(tr.quarterFbo[1], tr.quarterFbo[0], blur);
	}

	// 4. add the blurred highlights back over the scene
	VectorSet4(color, intensity, intensity, intensity, 1.0f);
	FBO_Blit(tr.quarterFbo[0], NULL, NULL, srcFbo, srcBox, NULL, color,
	         GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
}


/*
=============
RB_DepthOfField

STRAFE 64 cinematic depth of field. Reads the composited scene colour plus the
linear depth texture (tr.hdrDepthImage, populated by the r_depthPrepass path),
runs the gather-DoF shader into the scratch FBO, then copies the result back
over the source so the rest of the post chain sees focused/defocused pixels.

Assumes the single full-screen view STRAFE 64 renders (srcBox == whole FBO), so
it draws a plain full-screen quad with [0,1] texcoords. No-ops cleanly when DoF
is off, the blur amount is zero, or the depth texture isn't available.
=============
*/
void RB_DepthOfField(FBO_t *srcFbo, ivec4_t srcBox)
{
	vec4_t viewInfo, dofInfo;
	vec4_t quadVerts[4];
	vec2_t texCoords[4];
	mat4_t mvp;
	float  zmax, zmin, width, height;

	if (!r_dof->integer || r_dofAmount->value <= 0.0f)
		return;

	// the gather samples the linear depth texture (allocated when r_dof is set at
	// init/vid_restart) and pings through the scratch FBO; bail if either is absent
	if (!srcFbo || !tr.hdrDepthFbo || !tr.hdrDepthImage || !tr.screenScratchFbo)
		return;

	width  = (float)srcFbo->width;
	height = (float)srcFbo->height;
	zmax   = backEnd.viewParms.zFar;
	zmin   = r_znear->value;

	VectorSet4(viewInfo, zmax / zmin, zmax, 1.0f / width, 1.0f / height);
	VectorSet4(dofInfo,
	           r_dofFocalDist->value,
	           r_dofFocalRange->value,
	           r_dofAmount->value,
	           r_dofAutoFocus->integer ? 1.0f : 0.0f);

	VectorSet4(quadVerts[0], -1.0f,  1.0f, 0.0f, 1.0f);
	VectorSet4(quadVerts[1],  1.0f,  1.0f, 0.0f, 1.0f);
	VectorSet4(quadVerts[2],  1.0f, -1.0f, 0.0f, 1.0f);
	VectorSet4(quadVerts[3], -1.0f, -1.0f, 0.0f, 1.0f);

	texCoords[0][0] = 0.0f; texCoords[0][1] = 1.0f;
	texCoords[1][0] = 1.0f; texCoords[1][1] = 1.0f;
	texCoords[2][0] = 1.0f; texCoords[2][1] = 0.0f;
	texCoords[3][0] = 0.0f; texCoords[3][1] = 0.0f;

	// pass 1: DoF into the scratch FBO from (scene colour, scene depth)
	FBO_Bind(tr.screenScratchFbo);
	qglViewport(0, 0, tr.screenScratchFbo->width, tr.screenScratchFbo->height);
	qglScissor (0, 0, tr.screenScratchFbo->width, tr.screenScratchFbo->height);

	GL_State(GLS_DEPTHTEST_DISABLE);

	// NDC fullscreen quad -> identity transform (don't inherit stale MVP state)
	Mat4Identity(mvp);

	GLSL_BindProgram(&tr.depthOfFieldShader);
	GLSL_SetUniformMat4(&tr.depthOfFieldShader, UNIFORM_MODELVIEWPROJECTIONMATRIX, mvp);
	GL_BindToTMU(srcFbo->colorImage[0], TB_COLORMAP);
	GL_BindToTMU(tr.hdrDepthImage,      TB_LIGHTMAP);
	GLSL_SetUniformVec4(&tr.depthOfFieldShader, UNIFORM_VIEWINFO,     viewInfo);
	GLSL_SetUniformVec4(&tr.depthOfFieldShader, UNIFORM_DEPTHOFFIELD, dofInfo);
	RB_InstantQuad2(quadVerts, texCoords);

	// pass 2: copy the focused result back over the source for the rest of the chain
	FBO_FastBlit(tr.screenScratchFbo, srcBox, srcFbo, srcBox, GL_COLOR_BUFFER_BIT, GL_NEAREST);
}


/*
=============
RB_ColorGrade

STRAFE 64 photoreal-finish pass. The closing move of the post chain: one
full-screen shader on the tonemapped LDR scene doing FXAA + cinematic colour
grade + vignette + film grain. "Photoreal cinematic finish over a lofi base" --
melt the low-poly jaggies, warm/shape the colour like graded film, then vignette
+ a whisper of animated grain for the nostalgic tell.

Fullscreen NDC quad with an identity MVP, sampling the tonemapped scene and
writing the graded result straight into dstFbo. dstFbo == NULL means the default
framebuffer (the screen), in which case this pass doubles as the present blit --
no scratch FBO and no blit-back. No-ops cleanly when r_grade is off or the source
has no colour texture. The program is always compiled, so every knob is live (no
vid_restart, no depth texture needed).
=============
*/
void RB_ColorGrade(FBO_t *srcFbo, FBO_t *dstFbo)
{
	vec4_t viewInfo, grade, gradeFx;
	vec4_t quadVerts[4];
	vec2_t texCoords[4];
	mat4_t mvp;
	float  width, height;
	int    dstW, dstH;

	if (!r_grade->integer)
		return;

	if (!srcFbo || !srcFbo->colorImage[0])
		return;

	width  = (float)srcFbo->width;
	height = (float)srcFbo->height;

	// x,y unused; zw = rcpFrame for FXAA neighbour taps (source texel size)
	VectorSet4(viewInfo, 0.0f, 0.0f, 1.0f / width, 1.0f / height);
	VectorSet4(grade,
	           r_gradeContrast->value,
	           r_gradeSaturation->value,
	           r_gradeTemp->value,
	           r_vignette->value);
	VectorSet4(gradeFx,
	           r_filmGrain->value,
	           r_fxaa->integer ? 1.0f : 0.0f,
	           (float)(tr.frameCount % 1024) * 0.137f,   // animate the grain
	           0.0f);

	VectorSet4(quadVerts[0], -1.0f,  1.0f, 0.0f, 1.0f);
	VectorSet4(quadVerts[1],  1.0f,  1.0f, 0.0f, 1.0f);
	VectorSet4(quadVerts[2],  1.0f, -1.0f, 0.0f, 1.0f);
	VectorSet4(quadVerts[3], -1.0f, -1.0f, 0.0f, 1.0f);

	texCoords[0][0] = 0.0f; texCoords[0][1] = 1.0f;
	texCoords[1][0] = 1.0f; texCoords[1][1] = 1.0f;
	texCoords[2][0] = 1.0f; texCoords[2][1] = 0.0f;
	texCoords[3][0] = 0.0f; texCoords[3][1] = 0.0f;

	// grade straight into the destination (NULL == screen). No scratch, no
	// blit-back: each FBO transition we drop is a tile flush saved on TBDR GPUs.
	if (dstFbo)
	{
		dstW = dstFbo->width;
		dstH = dstFbo->height;
	}
	else
	{
		dstW = glConfig.vidWidth;
		dstH = glConfig.vidHeight;
	}

	FBO_Bind(dstFbo);
	qglViewport(0, 0, dstW, dstH);
	qglScissor (0, 0, dstW, dstH);

	GL_State(GLS_DEPTHTEST_DISABLE);

	Mat4Identity(mvp);

	GLSL_BindProgram(&tr.colorGradeShader);
	GLSL_SetUniformMat4(&tr.colorGradeShader, UNIFORM_MODELVIEWPROJECTIONMATRIX, mvp);
	GL_BindToTMU(srcFbo->colorImage[0], TB_COLORMAP);
	GLSL_SetUniformVec4(&tr.colorGradeShader, UNIFORM_VIEWINFO,     viewInfo);
	GLSL_SetUniformVec4(&tr.colorGradeShader, UNIFORM_COLORGRADE,   grade);
	GLSL_SetUniformVec4(&tr.colorGradeShader, UNIFORM_COLORGRADEFX, gradeFx);
	RB_InstantQuad2(quadVerts, texCoords);
}


/*
=============
RB_Bodycam

STRAFE 64 bodycam finish pass. The cheap-vest-cam look layered onto the
tonemapped LDR scene as the present blit: barrel warp, low-res sensor crunch,
chromatic aberration, rolling-shutter scanlines, animated sensor grain, heavy
vignette and blown highlights. Runs after RB_ColorGrade (when both are on) so
the artifacts sit on final display values.

Same shape as RB_ColorGrade: a fullscreen NDC quad with an identity MVP,
sampling the source scene and writing straight into dstFbo (NULL == the default
framebuffer / screen), so this pass doubles as the present blit -- no scratch
round-trip. Everything is keyed off the output resolution (no hardcoded pixel
sizes), reusing the colour-grade uniform slots:
  u_ViewInfo     = resW, resH, time(sec), -
  u_ColorGrade   = warp, chroma, crunch, scanline
  u_ColorGradeFx = grain, vignette, highlight-clip, -
No-ops cleanly when r_bodycam is off or the source has no colour texture.
=============
*/
void RB_Bodycam(FBO_t *srcFbo, FBO_t *dstFbo)
{
	vec4_t viewInfo, params0, params1;
	vec4_t quadVerts[4];
	vec2_t texCoords[4];
	mat4_t mvp;
	int    dstW, dstH;

	if (!r_bodycam->integer)
		return;

	if (!srcFbo || !srcFbo->colorImage[0])
		return;

	// resolution drives crunch/aberration/scanline/grain (native or dynamic);
	// floatTime gives the grain/scanline drift a real-time second clock.
	VectorSet4(viewInfo,
	           (float)glConfig.vidWidth,
	           (float)glConfig.vidHeight,
	           backEnd.refdef.floatTime,
	           0.0f);
	VectorSet4(params0,
	           r_bodycamWarp->value,
	           r_bodycamChroma->value,
	           r_bodycamCrunch->value,
	           r_bodycamScanline->value);
	VectorSet4(params1,
	           r_bodycamGrain->value,
	           r_bodycamVignette->value,
	           r_bodycamClip->value,
	           0.0f);

	VectorSet4(quadVerts[0], -1.0f,  1.0f, 0.0f, 1.0f);
	VectorSet4(quadVerts[1],  1.0f,  1.0f, 0.0f, 1.0f);
	VectorSet4(quadVerts[2],  1.0f, -1.0f, 0.0f, 1.0f);
	VectorSet4(quadVerts[3], -1.0f, -1.0f, 0.0f, 1.0f);

	texCoords[0][0] = 0.0f; texCoords[0][1] = 1.0f;
	texCoords[1][0] = 1.0f; texCoords[1][1] = 1.0f;
	texCoords[2][0] = 1.0f; texCoords[2][1] = 0.0f;
	texCoords[3][0] = 0.0f; texCoords[3][1] = 0.0f;

	if (dstFbo)
	{
		dstW = dstFbo->width;
		dstH = dstFbo->height;
	}
	else
	{
		dstW = glConfig.vidWidth;
		dstH = glConfig.vidHeight;
	}

	FBO_Bind(dstFbo);
	qglViewport(0, 0, dstW, dstH);
	qglScissor (0, 0, dstW, dstH);

	GL_State(GLS_DEPTHTEST_DISABLE);

	Mat4Identity(mvp);

	GLSL_BindProgram(&tr.bodycamShader);
	GLSL_SetUniformMat4(&tr.bodycamShader, UNIFORM_MODELVIEWPROJECTIONMATRIX, mvp);
	GL_BindToTMU(srcFbo->colorImage[0], TB_COLORMAP);
	GLSL_SetUniformVec4(&tr.bodycamShader, UNIFORM_VIEWINFO,     viewInfo);
	GLSL_SetUniformVec4(&tr.bodycamShader, UNIFORM_COLORGRADE,   params0);
	GLSL_SetUniformVec4(&tr.bodycamShader, UNIFORM_COLORGRADEFX, params1);
	RB_InstantQuad2(quadVerts, texCoords);
}
