// STRAFE 64 water: horizontal water plane.
//
// u_TextureMap is a snapshot of the colour buffer taken just before this surface is drawn. For a
// portal/mirror surface that buffer holds the full-screen MIRRORED view, so a screen-space lookup
// at this pixel is the planar reflection. We perturb that lookup with animated ripple slopes, blend
// it against a dark deep-water colour with a (stylised) fresnel term, and add a moon specular lobe.
//
//   u_BaseColor   rgb = deep water tint, a = reflection distortion (screen uv per unit slope)
//   u_VertColor   rgb = glint colour (HDR), a = specular power
//   u_LightOrigin xyz = unit direction towards the moon
//   u_ViewInfo    xy  = render target size in pixels

uniform sampler2D u_TextureMap;   // mirror view snapshot (HDR)
uniform sampler2D u_NormalMap;    // tileable ripple slopes in rg (0.5 = flat)

uniform vec3  u_ViewOrigin;
uniform float u_Time;
uniform vec4  u_ViewInfo;
uniform vec4  u_BaseColor;
uniform vec4  u_VertColor;
uniform vec4  u_LightOrigin;

varying vec3 var_WorldPos;

// two counter-moving octaves of ripples -> slope vector
vec2 ripple(vec2 p)
{
	vec2 uv1 = p * 0.0052 + vec2(u_Time * 0.0016, u_Time * 0.0009);
	mat2 rot = mat2(0.8, -0.6, 0.6, 0.8);
	vec2 uv2 = (rot * p) * 0.0131 + vec2(-u_Time * 0.0022, u_Time * 0.0013);
	vec2 s1 = texture2D(u_NormalMap, uv1).rg * 2.0 - 1.0;
	vec2 s2 = texture2D(u_NormalMap, uv2).rg * 2.0 - 1.0;
	return s1 + 0.55 * s2;
}

void main()
{
	vec3  toEye = u_ViewOrigin - var_WorldPos;
	float dist  = length(toEye);
	vec3  V     = toEye / max(dist, 0.001);

	// ripples calm down with distance (also hides texture shimmer)
	float calm  = 1.0 / (1.0 + dist * 0.00012);
	vec2  slope = ripple(var_WorldPos.xy) * calm;
	vec3  N     = normalize(vec3(slope * 0.42, 1.0));

	// distorted planar reflection
	vec2 uv   = gl_FragCoord.xy / u_ViewInfo.xy;
	// a ripple's screen-space size shrinks with distance (~1/dist), and the reflection of a vertical
	// edge mostly stretches vertically rather than shifting sideways
	float persp = clamp(500.0 / dist, 0.12, 2.2);
	vec2 off  = slope * vec2(0.3, 1.0) * u_BaseColor.a * persp;
	vec3 refl = texture2D(u_TextureMap, clamp(uv + off, vec2(0.002), vec2(0.998))).rgb;

	// stylised fresnel: never fully dark, mirror-like at grazing angles
	float cosT = clamp(dot(N, V), 0.0, 1.0);
	float F    = 0.10 + 0.90 * pow(1.0 - cosT, 4.0);

	vec3 col = mix(u_BaseColor.rgb, refl, F);

	// moon glints on the ripple facets
	vec3  L = normalize(u_LightOrigin.xyz);
	vec3  H = normalize(L + V);
	float spec = pow(max(dot(N, H), 0.0), u_VertColor.a);
	col += u_VertColor.rgb * spec;

	gl_FragColor = vec4(col, 1.0);
}
