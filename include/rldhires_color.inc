// rldhires_color.inc - the vertex colours of a hi-res driver (chunk CHRG):
// one way to read them for the game and Reload Studio
//
// THE MODE is bits 0..1 of the CHRG header's flags (include/rldhires.inc):
//   0  RLDHIRES_VCOL_GLTF  COLOR_0 as glTF says: white is neutral, the base
//                          colour is multiplied 1:1. Every CHRG written before
//                          the field existed has flags 0, so it keeps its look.
//   1  RLDHIRES_VCOL_PSX   PSX blend, as the PS1 mixes texture and vertex
//                          colour: grey 128 is neutral, darker shades, lighter
//                          lights up to twice; texture x colour / 128, clamped.
//   2  RLDHIRES_VCOL_OFF   the shading is in the textures: COLOR_0 is ignored.
//   3  not a mode: refused like an unknown flag bit (HRG-3).
//
// ONE WAY. RldHiresColor_Factor turns COLOR_0 as the glTF file holds it (a
// linear float 0..1 per channel) into the factor the linear base colour texel
// is multiplied with; the product is clamped to 1. The engine (when it builds
// a driver's vertices) and Reload Studio (its preview) both call it, so both
// show the same colour; nothing else turns COLOR_0 into a colour.

#ifndef RLDHIRES_COLOR_INC
#define RLDHIRES_COLOR_INC

#define RLDHIRES_FLAG_VCOL_MASK 0x3u

enum {
	RLDHIRES_VCOL_GLTF = 0,
	RLDHIRES_VCOL_PSX = 1,
	RLDHIRES_VCOL_OFF = 2
};

// The vertex colour mode of CHRG flags; -1 = not a mode (3).
static int RldHiresColor_Mode(unsigned flags);

// The factor for one channel of COLOR_0 (linear 0..1) in a mode: GLTF the
// channel itself, OFF 1, PSX the factor that gives texture x byte / 128 on the
// display curve (at most RLDHIRES_VCOL_FACTOR_MAX).
static float RldHiresColor_Factor(int mode, float colour0Linear);

#endif
