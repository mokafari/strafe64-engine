// STRAFE 64 cinematic depth of field.
//
// A single-pass "gather" DoF: each pixel reconstructs linear scene depth, forms
// a circle of confusion (CoC) from its distance to the focal plane, then gathers
// neighbours on a golden-angle spiral whose radius scales with the CoC. Samples
// are weighted by their OWN CoC, so a blurred foreground spills softly over a
// sharp background while the sharp background never bleeds into the blur -- the
// cheap trick that keeps focal edges believable. In-focus pixels keep a CoC of
// ~0 and stay pixel-crisp.
//
// Pairs with bullet-time: drive u_DepthOfField.z (blur amount) from the game's
// timescale to rack focus as the world slows.

uniform sampler2D u_ScreenImageMap;   // composited scene colour (TB_COLORMAP)
uniform sampler2D u_ScreenDepthMap;   // linear-sampleable depth (TB_LIGHTMAP)

uniform vec4 u_ViewInfo;       // zFar/zNear, zFar, 1/width, 1/height
uniform vec4 u_DepthOfField;   // focalDist(world), focalRange(world), maxBlur(px), autoFocus

varying vec2 var_ScreenTex;

#define DOF_TAPS 28
#define GOLDEN_ANGLE 2.39996323

// world-space linear depth from the hardware depth sample
float dofLinearDepth(vec2 uv)
{
	float d = texture2D(u_ScreenDepthMap, uv).r;
	return (1.0 / mix(u_ViewInfo.x, 1.0, d)) * u_ViewInfo.y;
}

void main()
{
	vec2  uv     = var_ScreenTex;
	vec2  texel  = u_ViewInfo.zw;
	float range  = max(u_DepthOfField.y, 1.0);
	float maxR   = u_DepthOfField.z;

	// focal distance: auto-focus on whatever sits under screen centre (great for
	// duels/bullet-time -- the target stays sharp), else a fixed world distance.
	float focal  = (u_DepthOfField.w > 0.5)
	             ? dofLinearDepth(vec2(0.5, 0.5))
	             : u_DepthOfField.x;

	vec3  center   = texture2D(u_ScreenImageMap, uv).rgb;
	float depth    = dofLinearDepth(uv);
	float centerCoC = clamp(abs(depth - focal) / range, 0.0, 1.0);

	float radius = centerCoC * maxR;
	if (radius < 0.75)
	{
		// effectively in focus -- skip the gather entirely
		gl_FragColor = vec4(center, 1.0);
		return;
	}

	vec3  accum = center;
	float wsum  = 1.0;

	for (int i = 1; i <= DOF_TAPS; i++)
	{
		float t = float(i);
		float r = radius * sqrt(t / float(DOF_TAPS));
		float a = t * GOLDEN_ANGLE;
		vec2  suv = uv + vec2(cos(a), sin(a)) * r * texel;

		float sDepth = dofLinearDepth(suv);
		float sCoC   = clamp(abs(sDepth - focal) / range, 0.0, 1.0);

		// a neighbour only contributes as far as its own blur disc reaches; this
		// lets blurred foreground bleed outward but blocks sharp background bleed
		float w = sCoC;
		accum += texture2D(u_ScreenImageMap, suv).rgb * w;
		wsum  += w;
	}

	vec3 blurred = accum / wsum;

	// keep crisp where in focus, blend to the gathered blur by the centre CoC
	gl_FragColor = vec4(mix(center, blurred, centerCoC), 1.0);
}
