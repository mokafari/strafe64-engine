"""strafegen_shaders — the STRAFE 64 identity shader script + assembly.

SHADER_SCRIPT is the scripts/strafe64.shader text; build_shader() optionally arms
the q3gl2 graphics recipes (strafegen_gfx.augment). Shipped once via the shared
asset pak (strafegen_pack.write_shared_assets), not per-map.
"""
import strafegen_gfx as gfx

SHADER_SCRIPT = """\
// STRAFE 64 identity — SOURCE DEV-TEXTURE look. The geometry is still
// vertex-colored, but the bulk world now wears the Hammer measure-grid
// palette: orange floors, grey walls (see SRC_ORANGE/SRC_GREY). Mechanic
// identity lives on the ACCENTS (start/finish/checkpoints/hazards/pads/
// portals), which keep vivid hues and pop against the neutral dev base.
// The detail map is a subtle uniform measure grid (16/32/64u); rgbGen
// exactVertex multiplies the near-white detail texel by the vertex colour,
// so the palette supplies orange/grey and the grid just darkens it. Detail
// TGAs are generated procedurally (see build_detail_textures) — a few KB,
// no hand-painted art. Bundled in every strafegen pk3.
textures/strafe64/surf
{
	surfaceparm nolightmap
	{
		map textures/strafe64/d_floor.tga
		rgbGen exactVertex
	}
}
textures/strafe64/wall
{
	surfaceparm nolightmap
	{
		map textures/strafe64/d_wall.tga
		rgbGen exactVertex
	}
	// CLASSIC SOURCE LOOK: the audio-reactive scrolling/bass-pulsing accent
	// conduit stage was removed — it read as ugly and busy. Walls are now a
	// clean Hammer dev-grid (d_wall.tga * vertex colour), nothing animated.
}
// CONCRETE — the lun3dm5 brutalist theme material. One pale-grey shader does the
// whole map (like lun3dm5's single c_crete6gs), vertex-lit like surf/wall: the
// near-white crete.tga (blotchy tone + faint 16u panel seams + pitting) is
// multiplied by the grey PAL_CRETE* vertex colour and the bake's sun-dome, so big
// eroded cube masses read as poured concrete differentiated only by light. Faces
// are routed here by theme_remap when --theme concrete is active; always shipped
// so one shared pak serves both themes.
textures/strafe64/crete
{
	surfaceparm nolightmap
	{
		map textures/strafe64/crete.tga
		// world ST is 1 repeat / 64u. The tile is a 1024 mirrored (seamless) cut
		// of the concrete photo, so 1 repeat = 2x2 mirrored copies of it. Scale so
		// 1 repeat ~ 320u => the photo content spans ~160u: pits/grain stay
		// realistic-sized (covering ~1024u made the holes huge), the mirror keeps
		// it seam-free, and a 2nd huge-scale overlay pass (below) breaks the
		// repeat so big platforms don't read as a tiled grid.
		tcMod scale 0.2 0.2
		rgbGen exactVertex
	}
	{
		// macro break-up: the SAME concrete at a huge scale (~2560u/repeat),
		// overlay-multiplied (GL_DST_COLOR GL_SRC_COLOR = ~2x, brightness-neutral
		// around mid-grey) so slow large-scale light/dark patches roll across the
		// surface and hide the fine tile's repetition. rgbGen identity so it does
		// not re-apply the vertex light.
		map textures/strafe64/crete.tga
		blendFunc GL_DST_COLOR GL_SRC_COLOR
		tcMod scale 0.025 0.025
		rgbGen identity
	}
}
// ACCENT GLOW — start / finish / checkpoints / jump-pads / gates / hazards /
// portals. Same dev-grid base as surf/wall, PLUS a second ADDITIVE pass of the
// same grid texture (rgbGen exactVertex) so the bright accent vertex colour is
// laid down a second time as light. The panels read as self-illuminated neon
// beacons that blow into colour under the GL2 HDR/bloom path, while the grid
// lines keep the measure-grid structure. Accent faces are routed to these
// shaders by their palette (see ACCENT_GLOW / _glow_tex) — geometry is unchanged.
textures/strafe64/glow_floor
{
	surfaceparm nolightmap
	{
		map textures/strafe64/d_floor.tga
		rgbGen exactVertex
	}
	{
		map textures/strafe64/d_floor.tga
		blendFunc GL_ONE GL_ONE
		rgbGen exactVertex
	}
}
textures/strafe64/glow_wall
{
	surfaceparm nolightmap
	{
		map textures/strafe64/d_wall.tga
		rgbGen exactVertex
	}
	{
		map textures/strafe64/d_wall.tga
		blendFunc GL_ONE GL_ONE
		rgbGen exactVertex
	}
}
// soft plasma glow for the arena speed-trail datamosh chips. Alpha-scaled
// additive (GL_SRC_ALPHA GL_ONE) so the per-chip alpha controls translucency,
// rgbGen/alphaGen vertex so each blob wears the pilot's stream hue. The radial
// trailglow texture turns each quad into a translucent plasma blob, not a
// hard square — bloom then lifts it into neon.
strafe64/trailglow
{
	nopicmip
	{
		map textures/strafe64/trailglow.tga
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen vertex
		alphaGen vertex
	}
}
textures/strafe64/sky
{
	qer_editorimage textures/strafe64/env/synth_ft.tga
	surfaceparm noimpact
	surfaceparm nolightmap
	surfaceparm nomarks
	surfaceparm sky
	// BRYCE 3D sky. A STATIC box (env/synth_{rt,lf,ft,bk,up,dn} from
	// _build_synthsky: a soft dusk gradient, a big hazy sun, and SMOOTH fractal
	// mountain ranges receding into atmospheric haze) PLUS two ANIMATED cloud
	// layers for gentle motion. idTech3 renders a sky shader's stages as cloud
	// layers on the dome (R_BuildCloudData -> RB_StageIteratorGeneric in
	// tr_sky.c) when skyparms sets a cloud height (the 512 below), and those
	// stages run the full tcMod pipeline — so soft clouds (env/clouds) DRIFT
	// overhead. Two layers at different scale/scroll give parallax; a slow sine
	// rgbGen breathes the upper layer. Additive so only the cloud crests show.
	// The cloud TILE is warm/amber (sun-lit, see _build_clouds) and both stages
	// run at ~60% level (rgbGen const/wave below) so the clouds stay wispy and
	// the dusk gradient reads THROUGH them — thinner, sunlit, less overcast.
	skyparms textures/strafe64/env/synth 512 -
	{
		map textures/strafe64/env/clouds.tga
		blendFunc GL_ONE GL_ONE
		rgbGen const ( 0.60 0.60 0.60 )
		tcMod scale 2 2
		tcMod scroll 0.006 0.0020
	}
	{
		map textures/strafe64/env/clouds.tga
		blendFunc GL_ONE GL_ONE
		rgbGen wave sin 0.40 0.16 0 0.05
		tcMod scale 3.5 3.5
		tcMod scroll -0.010 0.0035
	}
}
// PHOTOREAL SKY — the concrete (lun3dm5) theme's sky. A STATIC photo skybox
// (env/realsky_{rt,lf,ft,bk,up,dn}, baked from a sky photo by
// skybox_from_photo.py: bright blue daylight, real cumulus, mountains on the
// horizon). No animated cloud stages — the clouds are in the photo. Faces are
// routed here from textures/strafe64/sky by theme_remap when --theme concrete is
// active; always shipped so one shared pak serves both themes.
textures/strafe64/skyreal
{
	qer_editorimage textures/strafe64/env/realsky_ft.tga
	surfaceparm noimpact
	surfaceparm nolightmap
	surfaceparm nomarks
	surfaceparm sky
	skyparms textures/strafe64/env/realsky - -
}
// DAY FOG — the concrete theme's haze: a pale blue, pushed far back so the bright
// sky and pale concrete read airy (lun3dm5) instead of buried in dusk murk. The
// concrete theme routes its fog volume here (see write()); the default dusk fog
// (textures/strafe64/fog) is unchanged.
textures/strafe64/fog_day
{
	qer_editorimage textures/common/fog
	surfaceparm fog
	surfaceparm nolightmap
	surfaceparm nomarks
	surfaceparm nonsolid
	surfaceparm trans
	fogparms ( 0.62 0.70 0.82 ) 5200
}
// global atmospheric fog volume. The whole play area is wrapped in one
// CONTENTS_FOG brush (see write()), and every non-sky world surface is tagged
// to this fog, so distance fades the world toward the synthwave horizon colour
// — depth gives the scene air without hiding the near track. fogParms is
// ( red green blue ) <distance-to-opaque>.
textures/strafe64/fog
{
	qer_editorimage textures/common/fog
	surfaceparm fog
	surfaceparm nolightmap
	surfaceparm nomarks
	surfaceparm nonsolid
	surfaceparm trans
	// denser, dusk-tinted haze (purple-blue): closes the world in sooner so the
	// scene reads moodier/foggier and distant geometry melts into the dusk. The
	// arenas are ~3400u across, so 2800 puts the far walls/sky in real haze while
	// the near fight stays clear — the heavy light/bloom/neon then glows THROUGH
	// the murk instead of floating on a flat-bright box.
	fogparms ( 0.09 0.07 0.15 ) 2000
}
// the rising void plane, drawn client-side by the cgame race layer
strafe64/void
{
	surfaceparm trans
	surfaceparm nonsolid
	surfaceparm nomarks
	surfaceparm nolightmap
	cull none
	// a churning digital lattice, not a flat sheet: the texture luminance
	// modulates the red, two layers scroll + warp against each other so
	// the kill-plane looks like dissolving data rising to eat the world.
	// deformVertexes on au_bass makes the whole plane heave on the kick —
	// the void physically breathes with the music. Safe here: the plane is
	// translucent + cull none + nonsolid, so the render-only vertex push has
	// no seams to crack and never affects collision.
	deformVertexes wave 64 bass 0 10 0 0
	{
		map textures/strafe64/void_hex.tga
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen const ( 1.00 0.08 0.28 )
		alphaGen const 0.55
		tcMod scroll 0.05 0.07
		tcMod turb 0 0.15 0 0.25
	}
	{
		map textures/strafe64/void_hex.tga
		blendFunc GL_ONE GL_ONE
		// brightness rides the bass envelope: base glow + a kick-driven
		// flare (was a fixed sine throb)
		rgbGen wave bass 0.06 0.22 0 0
		tcMod scale 2 2
		tcMod scroll -0.03 -0.04
	}
}
// the racing ghost — a flat translucent silhouette of the player model,
// drawn client-side by the cgame race layer over the best run. rgbGen /
// alphaGen entity hand tint + opacity to cgame, so the ghost colour and
// cg_ghostAlpha ride the entity's shaderRGBA, tunable live. A cool hologram
// tint that never reads as a live (opaque) player, legible at speed under
// the PSX point-sampling preset. No texture needed.
strafe64/ghost
{
	cull none
	{
		map $whiteimage
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen entity
		alphaGen entity
	}
}
// MATRIX RAIN — wall-of-death / velodrome banking. Green digital-rain streaks
// cascade down: a SOLID dark base (so only the scrolling layers show — a static
// full-bright copy would drown the motion and freeze the rain) plus two additive
// layers scrolling at different speeds/scales for parallax depth. Purely
// time-driven (sin waves), NO audio reactivity. The brush stays solid and
// walkable; this is visual only. matrix.tga is generated procedurally
// (build_detail_textures) and bundled in every strafegen pk3.
textures/strafe64/matrix
{
	surfaceparm nolightmap
	{
		map $whiteimage
		rgbGen const ( 0.00 0.05 0.02 )
	}
	{
		map textures/strafe64/matrix.tga
		blendFunc GL_ONE GL_ONE
		rgbGen identity
		tcMod scroll 0 -0.60
	}
	{
		map textures/strafe64/matrix.tga
		blendFunc GL_ONE GL_ONE
		rgbGen wave sin 0.50 0.50 0 0.5
		tcMod scale 0.5 0.75
		tcMod scroll 0 -1.05
	}
}
"""

# The console override lives in its OWN shader file: shader FILES parse in
# reverse-alphabetical order and the first-parsed definition of a name wins,
# so overriding OA's "console" (defined in a file sorting after s*) needs a
# file name sorting after ALL stock scripts — scripts/zzz_s64_console.shader.
CONSOLE_SHADER = """\
// STRAFE 64 console: replaces the stock OpenArena logo-watermark background
// with a NERV terminal plate — near-black base + a faint drifting matrix-rain
// layer. UNIQUE name (s64console): duplicate shader names across files resolve
// to the lowest-priority pak, so shadowing stock "console" by name can't work —
// the engine prefers this name and falls back to stock if the pak is absent.
s64console
{
	nopicmip
	nomipmaps
	{
		map $whiteimage
		rgbGen const ( 0.010 0.018 0.024 )
	}
	{
		map textures/strafe64/matrix.tga
		blendFunc GL_ONE GL_ONE
		rgbGen const ( 0.05 0.09 0.07 )
		tcMod scale 1.0 1.5
		tcMod scroll 0 -0.05
	}
}
"""



def build_shader(gfx_on=True):
    """The scripts/strafe64.shader text. With gfx_on, inject the q3gl2 sun and
    append the component materials (strafegen_gfx.augment); else vanilla identity."""
    return gfx.augment(SHADER_SCRIPT) if gfx_on else SHADER_SCRIPT
