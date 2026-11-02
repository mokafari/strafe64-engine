// STRAFE 64 water: pass world position through; the fragment shader does everything.
attribute vec3 attr_Position;
attribute vec4 attr_TexCoord0;

uniform mat4 u_ModelViewProjectionMatrix;

varying vec3 var_WorldPos;

void main()
{
	vec4 p = vec4(attr_Position, 1.0);
	gl_Position = u_ModelViewProjectionMatrix * p;
	var_WorldPos = attr_Position;   // water is world geometry: vertex positions are world-space
}
