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
