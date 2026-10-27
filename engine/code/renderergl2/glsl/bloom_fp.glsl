// STRAFE 64 neon bloom in three programmable passes (replaces a ~25-blit chain).
//
//   mode 0  bright-pass downsample: 4 bilinear taps (16 source texels) of the
//           tonemapped scene, raised to the 4th power -- the same c^4 soft knee
//           the old multiplicative self-blits produced, minus the shimmer of a
//           single-tap 4x downsample
//   mode 1  separable gaussian: 5 linear-filtered taps (9-texel kernel) along dir
//   mode 2  composite: bilinear upsample scaled by intensity (additive blend)

uniform sampler2D u_ScreenImageMap;

uniform vec4 u_ViewInfo; // xy = tap step in uv, z = mode, w = intensity

varying vec2 var_ScreenTex;

void main()
{
	vec2 uv = var_ScreenTex;
	vec2 d  = u_ViewInfo.xy;

	if (u_ViewInfo.z < 0.5)
	{
		vec3 c = texture2D(u_ScreenImageMap, uv + vec2(-d.x, -d.y)).rgb
		       + texture2D(u_ScreenImageMap, uv + vec2( d.x, -d.y)).rgb
		       + texture2D(u_ScreenImageMap, uv + vec2(-d.x,  d.y)).rgb
		       + texture2D(u_ScreenImageMap, uv + vec2( d.x,  d.y)).rgb;
		c = clamp(c * 0.25, 0.0, 1.0);
		c *= c;
		gl_FragColor = vec4(c * c, 1.0);
	}
	else if (u_ViewInfo.z < 1.5)
	{
		vec3 c = texture2D(u_ScreenImageMap, uv).rgb * 0.227027027;
		c += (texture2D(u_ScreenImageMap, uv + d * 1.3846153846).rgb
		    + texture2D(u_ScreenImageMap, uv - d * 1.3846153846).rgb) * 0.316216216;
		c += (texture2D(u_ScreenImageMap, uv + d * 3.2307692308).rgb
		    + texture2D(u_ScreenImageMap, uv - d * 3.2307692308).rgb) * 0.070270270;
		gl_FragColor = vec4(c, 1.0);
	}
	else
	{
		gl_FragColor = vec4(texture2D(u_ScreenImageMap, uv).rgb * u_ViewInfo.w, 1.0);
	}
}
