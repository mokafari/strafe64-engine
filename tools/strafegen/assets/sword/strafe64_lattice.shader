// ============================================================
// STRAFE 64 — LATTICE speed-trail wall (renderergl2 / neon-bloom path)
//
// The damaging vertical light-wall each pilot leaves behind, drawn
// procedurally by CG_LatticeFrame (cg_lattice.c) as upright quads.
//
// CLEAN TRANSLUCENT look: a flat $whiteimage alpha blend tinted entirely by
// per-vertex modulate. The cgame drives EVERYTHING through the vertex colour +
// alpha — per-pilot hue, the white-hot leading edge, the travelling/breathing
// pulse, the slow-mo swell, the datamosh glitch, and the music reactivity. The
// bright (near-white) head + kick peaks exceed the gl2 bloom threshold so the
// wall GLOWS; cgame also casts dynamic neon lights along the trail.
//
// (An earlier revision sampled a scrolling scanline texture here; it read as a
// "grid material" and was dropped in favour of this clean translucent wall.)
//
// Deploy: lives in baseoa/scripts/ — the engine loads every scripts/*.shader
// at startup (sv_pure 0). Source of truth: tools/strafegen/assets/sword/.
// ============================================================

strafe64/lattice
{
	nomipmaps
	nopicmip
	cull none
	{
		map $whiteimage
		blendfunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}
