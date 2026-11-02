// STRAFE 64 moon shafts: screen-space light shafts (GPU Gems 3 ch.13) for scenes with no sun
// shadow map (baked maps). Each pixel marches toward the moon's screen position and counts how
// many samples see open sky in the depth buffer; geometry between the pixel and the moon (the
// portal frame, pillars) cuts the light into shafts. Rendered at half res and added over the
// tonemapped image.
//
//   u_ViewInfo  xy = moon position in screen uv, z = density (march length fraction), w = decay
//   u_Color     rgb = shaft colour * intensity, a = 1 when the moon is in front of the camera
//   u_ScreenDepthMap  raw depth (1.0 = sky)

uniform sampler2D u_ScreenDepthMap;
uniform vec4 u_ViewInfo;
uniform vec4 u_Color;

varying vec2 var_ScreenTex;

#define NUM_SAMPLES 48

void main()
{
	vec2 uv    = var_ScreenTex;
	vec2 delta = (u_ViewInfo.xy - uv) * (u_ViewInfo.z / float(NUM_SAMPLES));

	// interleaved-gradient noise: jitter the start so 48 samples read as smooth shafts, not bands
	float j = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
	vec2 p  = uv + delta * j;

	float illum = 0.0;
	float w     = 1.0;
	for (int i = 0; i < NUM_SAMPLES; i++)
	{
		float d   = texture2D(u_ScreenDepthMap, clamp(p, vec2(0.001), vec2(0.999))).r;
		float sky = step(0.99995, d);          // open sky = light reaches this sample
		illum += sky * w;
		w     *= u_ViewInfo.w;
		p     += delta;
	}
	illum *= u_ViewInfo.z / float(NUM_SAMPLES) * 3.0;
	// contrast: rays that are partly blocked fall off fast, fully open ones stay bright -> distinct shafts
	illum = pow(clamp(illum, 0.0, 2.0), 2.2);

	// fade with distance from the moon on screen (shafts radiate from it), and off-screen-moon
	float r   = length(u_ViewInfo.xy - uv);
	float rad = exp(-r * 1.6);

	// sky pixels are the light source already: only a faint halo there, shafts show on geometry/air
	float skyHere = step(0.99995, texture2D(u_ScreenDepthMap, uv).r);
	rad *= mix(1.0, 0.4, skyHere);

	gl_FragColor = vec4(u_Color.rgb * illum * rad * u_Color.a, 1.0);
}
