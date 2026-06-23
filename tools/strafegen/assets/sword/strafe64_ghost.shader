// ============================================================
// STRAFE 64 — DASH CHROMATIC GHOST
//
// A short, audio-reactive chromatic-aberration after-image left on an air-dash
// (EV_DOUBLE_JUMP). CG_DashGlitchGhost (cg_players.c) re-renders the pilot's
// legs/torso/head a few times, trailing back along the dash velocity and split
// into red / cyan copies, so the dash smears into a glitchy neon speed-ghost.
//
// Flat ADDITIVE $whiteimage tinted by the refEntity colour (rgbGen entity): it
// only ADDS light, so the ghost blooms to neon under the GL2 HDR/tonemap path
// and never crushes the model to a black silhouette. The cgame fades it by
// scaling the entity RGB toward black over the ~260ms window (additive ignores
// dst alpha, so RGB IS the fade).
//
// Deploy: lives in baseoa/scripts/ — the engine loads every scripts/*.shader at
// startup (sv_pure 0). Source of truth: tools/strafegen/assets/sword/.
// ============================================================

strafe64/glitchghost
{
	nomipmaps
	nopicmip
	cull none
	{
		map $whiteimage
		blendfunc GL_ONE GL_ONE
		rgbGen entity
	}
}
