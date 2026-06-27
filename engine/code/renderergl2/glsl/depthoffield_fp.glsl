// STRAFE 64 cinematic depth of field.
//
// A single-pass "gather" DoF: each pixel reconstructs linear scene depth, forms
// a circle of confusion (CoC) from its distance to the focal plane, then gathers
// neighbours on a golden-angle spiral whose radius scales with the CoC. Samples
// are weighted by their own CoC (so blurred foreground spills over a sharp
// background but the background never bleeds into the blur) and by luminance (so
// bright neon points fatten into soft bokeh discs -- the cinematic tell). The
// spiral is per-pixel rotated to trade banding for fine noise. In-focus pixels
// keep a CoC of ~0 and stay pixel-crisp.
//
// Pairs with bullet-time: drive u_DepthOfField.z (blur amount) from the game's
// timescale to rack focus as the world slows.

uniform sampler2D u_ScreenImageMap;   // composited scene colour (TB_COLORMAP)
uniform sampler2D u_ScreenDepthMap;   // linear-sampleable depth (TB_LIGHTMAP)

uniform vec4 u_ViewInfo;       // zFar/zNear, zFar, 1/width, 1/height
uniform vec4 u_DepthOfField;   // focalDist(world), focalRange(world), maxBlur(px), autoFocus

varying vec2 var_ScreenTex;

#define DOF_TAPS 43
#define GOLDEN_ANGLE 2.39996323

// world-space linear depth from the hardware depth sample
float dofLinearDepth(vec2 uv)
{
	float d = texture2D(u_ScreenDepthMap, uv).r;
	return (1.0 / mix(u_ViewInfo.x, 1.0, d)) * u_ViewInfo.y;
}

// stabilised auto-focus: average depth over a small centre cross so a thin object
// (a blade, a far gap) flickering across the exact middle pixel doesn't pop focus
float dofAutoFocusDepth()
{
	vec2 t = u_ViewInfo.zw * 3.0;
	float d  = dofLinearDepth(vec2(0.5, 0.5));
	d += dofLinearDepth(vec2(0.5, 0.5) + vec2( t.x, 0.0));
	d += dofLinearDepth(vec2(0.5, 0.5) + vec2(-t.x, 0.0));
	d += dofLinearDepth(vec2(0.5, 0.5) + vec2(0.0,  t.y));
	d += dofLinearDepth(vec2(0.5, 0.5) + vec2(0.0, -t.y));
	return d * 0.2;
}

void main()
{
	vec2  uv     = var_ScreenTex;
	vec2  texel  = u_ViewInfo.zw;
	float range  = max(u_DepthOfField.y, 1.0);
	float maxR   = u_DepthOfField.z;

	float focal  = (u_DepthOfField.w > 0.5) ? dofAutoFocusDepth() : u_DepthOfField.x;

	// the in-focus band widens with focal distance (hyperfocal-like): things far
	// away keep more in focus, so the sharp zone breathes instead of gliding
	// through the world as a thin fixed slab
	float effRange = range * (1.0 + focal * 0.0009);

	vec3  center    = texture2D(u_ScreenImageMap, uv).rgb;
	float depth     = dofLinearDepth(uv);
	// smoothstep falloff eases the sharp->blur transition (no hard slab edge)
	float centerCoC = smoothstep(0.0, 1.0, clamp(abs(depth - focal) / effRange, 0.0, 1.0));

	float radius = centerCoC * maxR;
	if (radius < 0.75)
	{
		// effectively in focus -- skip the gather entirely
		gl_FragColor = vec4(center, 1.0);
		return;
	}

	// per-pixel spiral rotation breaks up the repeating tap pattern into noise
	float rot = fract(sin(dot(uv, vec2(12.9898, 78.233))) * 43758.5453) * 6.2831853;

	vec3  accum = center;
	float wsum  = 1.0;

	for (int i = 1; i <= DOF_TAPS; i++)
	{
		float t = float(i);
		float r = radius * sqrt(t / float(DOF_TAPS));
		float a = t * GOLDEN_ANGLE + rot;
		vec2  suv = uv + vec2(cos(a), sin(a)) * r * texel;

		vec3  sCol   = texture2D(u_ScreenImageMap, suv).rgb;
		float sDepth = dofLinearDepth(suv);
		float sCoC   = smoothstep(0.0, 1.0, clamp(abs(sDepth - focal) / effRange, 0.0, 1.0));

		// neighbour contributes as far as its own blur disc reaches; nearer
		// (foreground) samples are allowed to spill fully over the centre
		float w = (sDepth < depth) ? max(sCoC, centerCoC) : sCoC;

		// luminance weighting: bright points bloom into soft bokeh discs
		float lum = dot(sCol, vec3(0.299, 0.587, 0.114));
		w *= 1.0 + lum * lum * 1.5;

		accum += sCol * w;
		wsum  += w;
	}

	vec3 blurred = accum / wsum;

	// keep crisp where in focus, blend to the gathered blur by the centre CoC
	gl_FragColor = vec4(mix(center, blurred, centerCoC), 1.0);
}
