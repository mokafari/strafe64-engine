// STRAFE 64 photoreal-finish pass: FXAA + cinematic colour grade + vignette + film grain.
//
// One full-screen pass at the very end of the post chain, on the tonemapped LDR
// image. The goal is "photoreal cinematic finish over a lofi base": clean the
// jaggies off the low-poly geometry (FXAA), warm and shape the colour like a
// graded film (white balance + contrast S-curve + saturation), then nod to the
// nostalgia with a soft vignette and a whisper of animated film grain. Every
// stage is individually switchable from cvars so it can be dialled from "off"
// to "heavy VHS" without a rebuild.

uniform sampler2D u_ScreenImageMap;   // composited tonemapped scene (TB_COLORMAP)

uniform vec4 u_ViewInfo;     // x = highlight shoulder, y = split-tone, zw = 1/width, 1/height (rcpFrame for FXAA)
uniform vec4 u_ColorGrade;   // contrast, saturation, temperature, vignette
uniform vec4 u_ColorGradeFx; // filmGrain, fxaaEnable, frameSeed, black lift

varying vec2 var_ScreenTex;

#define LUMA vec3(0.299, 0.587, 0.114)

// ---- FXAA 3.11 (console/PC-lite variant) -----------------------------------
// Cheap edge-directed antialias. Reads luma of a 3x3 neighbourhood, finds the
// local gradient and blends a couple of taps along the edge. Good enough to melt
// the low-poly silhouettes without the cost/latency of MSAA.
vec3 fxaa(vec2 uv, vec2 rcp)
{
	float lM  = dot(texture2D(u_ScreenImageMap, uv).rgb, LUMA);
	float lNW = dot(texture2D(u_ScreenImageMap, uv + vec2(-1.0, -1.0) * rcp).rgb, LUMA);
	float lNE = dot(texture2D(u_ScreenImageMap, uv + vec2( 1.0, -1.0) * rcp).rgb, LUMA);
	float lSW = dot(texture2D(u_ScreenImageMap, uv + vec2(-1.0,  1.0) * rcp).rgb, LUMA);
	float lSE = dot(texture2D(u_ScreenImageMap, uv + vec2( 1.0,  1.0) * rcp).rgb, LUMA);

	float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
	float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));

	// flat area? leave it crisp.
	if (lMax - lMin < max(0.05, lMax * 0.125))
		return texture2D(u_ScreenImageMap, uv).rgb;

	vec2 dir;
	dir.x = -((lNW + lNE) - (lSW + lSE));
	dir.y =  ((lNW + lSW) - (lNE + lSE));

	float reduce = max((lNW + lNE + lSW + lSE) * 0.03125, 0.0078125);
	float rcpDir = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
	dir = clamp(dir * rcpDir, -8.0, 8.0) * rcp;

	vec3 rgbA = 0.5 * (
		texture2D(u_ScreenImageMap, uv + dir * (1.0 / 3.0 - 0.5)).rgb +
		texture2D(u_ScreenImageMap, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
	vec3 rgbB = rgbA * 0.5 + 0.25 * (
		texture2D(u_ScreenImageMap, uv + dir * -0.5).rgb +
		texture2D(u_ScreenImageMap, uv + dir *  0.5).rgb);

	float lB = dot(rgbB, LUMA);
	return (lB < lMin || lB > lMax) ? rgbA : rgbB;
}

// cheap hash for animated grain
float hash(vec2 p)
{
	p = fract(p * vec2(123.34, 456.21));
	p += dot(p, p + 45.32);
	return fract(p.x * p.y);
}

void main()
{
	vec2 rcp = u_ViewInfo.zw;

	vec3 col = (u_ColorGradeFx.y > 0.5)
		? fxaa(var_ScreenTex, rcp)
		: texture2D(u_ScreenImageMap, var_ScreenTex).rgb;

	// --- white balance / temperature (warm = nostalgic) ---
	// push red up & blue down (or the reverse for cool), pivoting around grey.
	float temp = u_ColorGrade.z;
	col += vec3(temp, temp * 0.2, -temp) * col;

	// --- contrast S-curve around mid-grey ---
	col = (col - 0.5) * u_ColorGrade.x + 0.5;

	// --- saturation ---
	float l = dot(col, LUMA);
	col = mix(vec3(l), col, u_ColorGrade.y);

	col = max(col, vec3(0.0));

	// --- split-toning: teal-cool shadows, warm peach highlights ---
	// Multiplicative so it follows the image's own values, strength 1 = a clear but
	// restrained grade; luma-preserved so it separates hue without changing exposure.
	float split = u_ViewInfo.y;
	if (split > 0.0)
	{
		float l0 = dot(col, LUMA);
		float lt = smoothstep(0.08, 0.85, l0);
		vec3 tint = mix(vec3(-0.12, 0.05, 0.08), vec3(0.16, 0.06, -0.05), lt);
		col *= 1.0 + tint * split;
		col *= l0 / max(dot(col, LUMA), 0.0001);
	}

	// --- highlight shoulder: roll the top of the range off smoothly instead of a hard
	// cut. Above the knee the curve flattens, then is rescaled so 1.0 in is still 1.0
	// out; bright marble keeps its veining and the neon keeps its punch.
	float sh = u_ViewInfo.x;
	if (sh > 0.0)
	{
		float knee = 0.9 - 0.4 * clamp(sh, 0.0, 1.0);
		float peak = knee + (1.0 - knee) * 0.5;
		// roll off by the brightest channel and scale all three by the same factor, so
		// near-white pixels keep their hue. The curve is continuous and smooth at the knee
		// (y = m below it); the whole image is then gained by 1/peak so 1.0 stays white.
		// (Rescaling only the part above the knee made a visible step there.)
		float m = max(col.r, max(col.g, col.b));
		float d = max(m - knee, 0.0);
		float rolled = m - d + d / (1.0 + d / (1.0 - knee));
		col *= (m > 0.0001 ? rolled / m : 1.0) / peak;
	}

	// --- black lift: milky floor in the darks (ethereal haze) ---
	col = col * (1.0 - u_ColorGradeFx.w) + u_ColorGradeFx.w;
	col = max(col, vec3(0.0));

	// --- vignette: darken toward the frame edge, filmic falloff ---
	vec2 vd = var_ScreenTex - 0.5;
	float vig = 1.0 - u_ColorGrade.w * dot(vd, vd) * 2.6;
	col *= clamp(vig, 0.0, 1.0);

	// --- film grain: animated, luma-aware (darker areas grain more, like film) ---
	float g = u_ColorGradeFx.x;
	if (g > 0.0)
	{
		float n = hash(var_ScreenTex + u_ColorGradeFx.z) - 0.5;
		col += n * g * (1.0 - 0.6 * dot(col, LUMA));
	}

	gl_FragColor = vec4(col, 1.0);
}
