// STRAFE 64 bodycam finish pass: barrel warp + sensor crunch + chromatic
// aberration + rolling-shutter scanlines + sensor grain + vignette + highlight
// clip. Runs on the tonemapped LDR scene (after the colour grade, when both are
// on) as the present blit, so all the cheap-camera artifacts sit on final
// display values rather than linear HDR.
//
// Everything is keyed off u_Resolution (native/dynamic px) -- no hardcoded pixel
// sizes -- so it reads the same crunch/aberration at any output resolution. Each
// stage is driven by a cvar so it can be dialled from "off" to "Unrecord-heavy"
// without a rebuild.

uniform sampler2D u_ScreenImageMap;   // composited tonemapped scene (TB_COLORMAP)

uniform vec4 u_ViewInfo;     // x=resW, y=resH, z=time(sec), w=unused
uniform vec4 u_ColorGrade;   // x=warp, y=chroma(px@edge), z=crunch, w=scanline
uniform vec4 u_ColorGradeFx; // x=grain, y=vignette, z=highlight clip lift, w=unused

varying vec2 var_ScreenTex;

#define LUMA vec3(0.299, 0.587, 0.114)

// cheap animated hash noise
float hash(vec2 p)
{
	p = fract(p * vec2(443.897, 441.423));
	p += dot(p, p + 19.19);
	return fract((p.x + p.y) * p.x);
}

void main()
{
	vec2  res      = u_ViewInfo.xy;
	float time     = u_ViewInfo.z;
	float warp     = u_ColorGrade.x;
	float chroma   = u_ColorGrade.y;
	float crunch   = u_ColorGrade.z;
	float scanAmt  = u_ColorGrade.w;
	float grainAmt = u_ColorGradeFx.x;
	float vigAmt   = u_ColorGradeFx.y;
	float clipLift = u_ColorGradeFx.z;

	vec2  uv = var_ScreenTex;
	vec2  c  = uv - 0.5;
	float r2 = dot(c, c);

	// 01. BARREL WARP -- radial-exponential UV pull toward centre (cheap lens).
	vec2 wuv = uv + c * r2 * warp;

	// 02. PIXEL CRUNCH -- quantise UV to a fraction of native res to fake a
	//     low-resolution sensor. crunch 1.0 = native (no-op), lower = crunchier.
	vec2 cuv = wuv;
	if (crunch < 0.999)
	{
		vec2 cres = max(res * crunch, vec2(1.0));
		cuv = (floor(wuv * cres) + 0.5) / cres;
	}

	// 03. CHROMATIC ABERRATION -- per-channel UV offset, grows toward the edge.
	vec2 off = c * (chroma / res.x) * (1.0 + r2 * 2.0);
	vec3 col;
	col.r = texture2D(u_ScreenImageMap, cuv + off).r;
	col.g = texture2D(u_ScreenImageMap, cuv      ).g;
	col.b = texture2D(u_ScreenImageMap, cuv - off).b;

	// 04. ROLLING-SHUTTER SCANLINES -- cos x screen-y with a slow vertical drift.
	float line = cos(uv.y * res.y * 0.5 + time * 12.0) * 0.5 + 0.5;
	col *= 1.0 - scanAmt * line;

	// 05. SENSOR GRAIN -- animated hash, luma-weighted (darker reads noisier).
	if (grainAmt > 0.0)
	{
		float luma = dot(col, LUMA);
		float g = hash(uv * res + time) - 0.5;
		col += g * grainAmt * (1.2 - luma);
	}

	// 06. VIGNETTE -- filmic edge falloff.
	col *= 1.0 - vigAmt * smoothstep(0.2, 0.75, r2);

	// 07. HIGHLIGHT CLIP -- push highlights toward pure-white blowout (the narrow
	//     dynamic range of a cheap vest-cam) then clamp.
	col = min(col * (1.0 + clipLift), vec3(1.0));

	gl_FragColor = vec4(col, 1.0);
}
