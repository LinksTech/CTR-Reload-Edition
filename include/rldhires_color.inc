// rldhires_color.inc - the vertex colours of a hi-res driver (chunk CHRG):
// one way to read them for the game and Reload Studio
//
// Included by include/rldhires.inc (so the game, rldpack and Reload Studio all
// have it wherever they have the CHRG rules); it may also be included alone.
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
// linear float 0..1 per channel) into the factor the base colour is multiplied
// with; the product is clamped to 1. The engine (when it builds a driver's
// vertices) and Reload Studio (its preview) both call it, so both show the same
// colour; nothing else turns COLOR_0 into a colour.
//
// HOW A FACTOR MULTIPLIES. A factor is a linear value, like COLOR_0 itself. It
// multiplies the texel on the display curve, the way program "ns" has always
// multiplied COLOR_0 (platform/engine_static_shader.inc, LINEAR FACTORS): the
// factor and the texel are both put on the sRGB curve and their product is the
// displayed colour, clamped to 1 - RldHiresColor_Apply. For a pure power curve
// that is the same as the linear product; for sRGB it is what makes the PS1
// rule exact. A plain linear product with the linear texel would miss the PS1
// rule by up to 10 levels of 255 (the sRGB curve is no pure power: its offset
// 0.055 does not scale), so the preview must use RldHiresColor_Apply, not its
// own multiplication.
//
// THE PSX FACTOR. The PS1 rule works on display bytes: out = texel x byte / 128,
// clamped to 255, where byte is the sRGB byte of COLOR_0 (an exporter writes the
// PS1 byte b as the linear value whose sRGB byte is b). RldHiresColor_Factor
// rounds COLOR_0 to that byte and gives the linear factor whose display value is
// byte / 128 (the sRGB curve continued above 1, RldHiresColor_Decode): grey 128
// -> 1, 255 -> RLDHIRES_VCOL_FACTOR_MAX. rldpack's self-test (selftest-hires,
// also run inside Reload Studio) measures the whole chain - COLOR_0, factor, the
// engine's vertex byte, the display product - against the exact rule over all
// 256 x 256 texel / byte pairs: at most RLDHIRES_VCOL_PSX_TOLERANCE levels.
//
// A PRIMITIVE WITHOUT COLOR_0 has the factor 1 in every mode (its texture as it
// is). Do not pass glTF's default white to RldHiresColor_Factor: in PSX blend
// white is the byte 255, twice as bright.
//
// UNTEXTURED FACES IN PSX BLEND. The PS1 multiplies by colour / 128 only on
// textured polygons; a polygon without a texture shows its vertex colour as it
// is. So a primitive whose material has no base colour texture reads COLOR_0 as
// glTF does, also in PSX blend: RldHiresColor_ModeFor(mode, textured) gives the
// mode to use for it. Engine and Studio both call it per primitive.
//
// THE ENGINE'S VERTEX. The engine keeps one u8 per channel in its vertex
// (EngineVertex.color). RldHiresColor_VertexByte is that byte and
// RldHiresColor_VertexValue the display factor program "ns" makes of it:
//   glTF, off  the linear factor x 255 (as before: "ns" puts it on the sRGB
//              curve; off = 255 = white, the look of a model without COLOR_0)
//   PSX        the display factor x 128, i.e. the PS1 byte itself; "ns" (told
//              by the material, see platform/engine_hires_char.c) multiplies
//              it by 1 / 128 x 255 = RLDHIRES_VCOL_PSX_VERTEX_SCALE instead of
//              putting it on the curve, so the PS1 byte reaches the texel
//              unrounded.
//
// THE PREVIEW LIGHT (V-E5). The engine lights a hi-res driver with program "ns"
// and the light of the static model (platform/engine_hires_char.c EngHc_Light:
// g_cfg_engineStaticSun, whose default is ENGINE_STATIC_SUN_DEFAULT in
// include/platform/engine.h, and the "hires" style: saturation 1, contrast 1,
// smooth normals). The RLDHIRES_LIGHT_* values are those defaults, so that
// Reload Studio lights its preview the same; the engine's self-test checks that
// both are equal. Per pixel, with the normal N turned to the viewer:
//   ambient = mix(RLDHIRES_LIGHT_AMBIENT_BELOW, RLDHIRES_LIGHT_AMBIENT_ABOVE, 0.5 + 0.5 N.y)
//   light   = ambient + RLDHIRES_LIGHT_DIFFUSE x max(0, N . normalize(RLDHIRES_LIGHT_SUN_*))
//   colour  = clamp(base x light, 0, 1)    (base = RldHiresColor_Apply x tint, display values)
// The sun direction points TOWARDS the sun, in world axes: +y up. A kart at the
// game's angle 0 has its glTF axes on the world axes (glTF +Y up, +Z the front),
// so the Studio uses the direction as it is on the unrotated model. The
// light is on the display values (as "ns" does it), unlit materials
// (KHR_materials_unlit) take no light.

#ifndef RLDHIRES_COLOR_INC
#define RLDHIRES_COLOR_INC

#include <math.h>
#include <string.h>

#define RLDHIRES_FLAG_VCOL_MASK 0x3u

enum
{
	RLDHIRES_VCOL_GLTF = 0,
	RLDHIRES_VCOL_PSX = 1,
	RLDHIRES_VCOL_OFF = 2
};

// The largest PSX factor: the linear value whose display value is 255 / 128.
#define RLDHIRES_VCOL_FACTOR_MAX 4.9087667f
// The display value "ns" makes of one unit of a PSX vertex byte: x / 255 on
// the GPU (UNORM8) times this = x / 128.
#define RLDHIRES_VCOL_PSX_VERTEX_SCALE (255.0f / 128.0f)
// The most the PSX chain may miss the PS1 rule by, in levels of 255 (decision
// V-E2; the self-test measures it).
#define RLDHIRES_VCOL_PSX_TOLERANCE 3.0

// The light of the hi-res drivers in the game (see THE PREVIEW LIGHT):
// ENGINE_STATIC_SUN_DEFAULT {x, y, z, ambient above, diffuse, ambient below}.
#define RLDHIRES_LIGHT_SUN_X 0.35f
#define RLDHIRES_LIGHT_SUN_Y 0.85f
#define RLDHIRES_LIGHT_SUN_Z 0.40f
#define RLDHIRES_LIGHT_AMBIENT_ABOVE 0.95f
#define RLDHIRES_LIGHT_DIFFUSE 0.40f
#define RLDHIRES_LIGHT_AMBIENT_BELOW 0.80f

// The vertex colour mode of CHRG flags; -1 = not a mode (3).
static int RldHiresColor_Mode(unsigned flags)
{
	const unsigned mode = flags & RLDHIRES_FLAG_VCOL_MASK;

	return (mode == 3u) ? -1 : (int)mode;
}

// The mode a primitive uses: PSX blend on a primitive without a base colour
// texture is glTF (the PS1 shows an untextured polygon's colour as it is).
static int RldHiresColor_ModeFor(int mode, int textured)
{
	return ((mode == RLDHIRES_VCOL_PSX) && !textured) ? RLDHIRES_VCOL_GLTF : mode;
}

// The sRGB curve, continued above 1 by the same formula (a factor up to
// RLDHIRES_VCOL_FACTOR_MAX has a display value up to 255 / 128); 0 below 0.
static float RldHiresColor_Encode(float linear)
{
	if (!(linear > 0.0f))
	{
		return 0.0f;
	}
	return (linear <= 0.0031308f) ? (linear * 12.92f) : (1.055f * powf(linear, 1.0f / 2.4f) - 0.055f);
}

// The inverse of RldHiresColor_Encode (also above 1); 0 below 0.
static float RldHiresColor_Decode(float display)
{
	if (!(display > 0.0f))
	{
		return 0.0f;
	}
	return (display <= 0.04045f) ? (display / 12.92f) : powf((display + 0.055f) / 1.055f, 2.4f);
}

// The sRGB byte of a linear COLOR_0 channel (clamped to 0..1 first).
static int RldHiresColor_Byte(float colour0Linear)
{
	const float c = (colour0Linear > 0.0f) ? ((colour0Linear < 1.0f) ? colour0Linear : 1.0f) : 0.0f;

	return (int)(RldHiresColor_Encode(c) * 255.0f + 0.5f);
}

// The factor for one channel of COLOR_0 (linear 0..1) in a mode: GLTF the
// channel itself (clamped to 0..1), OFF 1, PSX the factor that gives texture x
// byte / 128 on the display curve (at most RLDHIRES_VCOL_FACTOR_MAX). Any other
// mode: 1. For a primitive, pass RldHiresColor_ModeFor(mode, textured).
static float RldHiresColor_Factor(int mode, float colour0Linear)
{
	if (mode == RLDHIRES_VCOL_GLTF)
	{
		return (colour0Linear > 0.0f) ? ((colour0Linear < 1.0f) ? colour0Linear : 1.0f) : 0.0f;
	}
	if (mode == RLDHIRES_VCOL_PSX)
	{
		return RldHiresColor_Decode((float)RldHiresColor_Byte(colour0Linear) / 128.0f);
	}
	return 1.0f;
}

// The displayed base colour of one channel: the factor and the texel (a
// display value 0..1, as the PNG holds it) multiplied on the display curve,
// clamped to 1. Without a texture pass texelDisplay 1. The material's tint
// (baseColorFactor, on the display curve as well) multiplies the result.
static float RldHiresColor_Apply(float factor, float texelDisplay)
{
	const float out = RldHiresColor_Encode(factor) * texelDisplay;

	return (out < 1.0f) ? out : 1.0f;
}

// The byte the engine stores for a factor in EngineVertex.color (see THE
// ENGINE'S VERTEX): glTF and off the linear factor x 255, PSX the display
// factor x 128. Rounded, clamped to 0..255.
static unsigned char RldHiresColor_VertexByte(int mode, float factor)
{
	const float x = (mode == RLDHIRES_VCOL_PSX) ? (RldHiresColor_Encode(factor) * 128.0f + 0.5f) : (factor * 255.0f + 0.5f);

	return (unsigned char)((x <= 0.0f) ? 0 : ((x >= 255.0f) ? 255 : (int)x));
}

// The display factor program "ns" makes of a stored vertex byte (for the
// self-test and a preview that wants the engine's rounding too).
static float RldHiresColor_VertexValue(int mode, unsigned char vertexByte)
{
	const float unorm = (float)vertexByte / 255.0f;

	return (mode == RLDHIRES_VCOL_PSX) ? (unorm * RLDHIRES_VCOL_PSX_VERTEX_SCALE) : RldHiresColor_Encode(unorm);
}

// The name of a mode, as rldpack's --vertex-colors and its reports spell it.
static const char *RldHiresColor_ModeName(int mode)
{
	return (mode == RLDHIRES_VCOL_GLTF) ? "gltf" : ((mode == RLDHIRES_VCOL_PSX) ? "psx" : ((mode == RLDHIRES_VCOL_OFF) ? "off" : "unknown"));
}

// A mode from its name ("gltf", "psx", "off"); -1 for anything else.
static int RldHiresColor_ModeByName(const char *name)
{
	if (name == NULL)
	{
		return -1;
	}
	if (strcmp(name, "gltf") == 0)
	{
		return RLDHIRES_VCOL_GLTF;
	}
	if (strcmp(name, "psx") == 0)
	{
		return RLDHIRES_VCOL_PSX;
	}
	if (strcmp(name, "off") == 0)
	{
		return RLDHIRES_VCOL_OFF;
	}
	return -1;
}

#endif
