// ============================================================
// STRAFE 64 — sword slash arc
//
// Additive energy ribbon drawn procedurally by CG_SwordSlashTrail
// (cg_weapons.c) as a triangle strip swept along the swing. Colour/fade
// comes from per-vertex modulate (rgbGen vertex), so the cgame controls
// the slash's brightness and taper; the shader just adds it as light.
//
// Deploy: lives in baseoa/scripts/ — the engine loads every scripts/*.shader
// at startup (sv_pure 0). Source of truth: tools/strafegen/assets/sword/.
// ============================================================

strafe64/sword_slash
{
	nomipmaps
	nopicmip
	cull none
	{
		map $whiteimage
		blendfunc add
		rgbGen vertex
	}
}
// STRAFE 64 — kill slash-arc streak (CG_AddSwordCuts in cg_ragdoll.c).
// A textured energy streak mapped st 0..1 across a single quad and faded by
// per-vertex alpha. GL_SRC_ALPHA GL_ONE keeps it additive (never black) while
// still honouring the cgame's alpha fade. Distinct from strafe64/sword_slash
// above, which is the FP swing trail's flat $whiteimage ribbon.
strafe64/sword_cut
{
	nomipmaps
	nopicmip
	cull none
	{
		map textures/strafe64/sword_slash.tga
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen vertex
		alphaGen vertex
	}
}
