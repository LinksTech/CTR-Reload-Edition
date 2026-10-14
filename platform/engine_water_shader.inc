// The engine's water program, "nw" (platform/engine_water.c draws with it;
// include/platform/engine.h, section WATER). Included by native_shaders.inc, so
// that tools/shader_spirv_probe.c emits nw.vert / nw.frag with the other
// programs and the ctest shader_spirv_dialect compiles it. The block "NeBlock"
// is struct EngineUniforms field for field (ENGINE_UNIFORM_BLOCK_GLSL, shared
// with "ne").
//
// WHAT IT DRAWS. The water of a level as a reflection, not as the PS1 trick:
// the PS1 path gives every water vertex a list of 28 precomputed env-map
// coordinates and steps through them (game/RenderLevel/AnimateWater.c). Here
// every fragment reflects its view ray off the water surface and looks the
// reflected direction up in the level's own env map - on the retail disc a
// 64 x 64 sky panorama (rows 0-31 sky with the sun, the horizon at row 32).
//
// THE SURFACE NORMAL. The mesh's normal (a_texcoord = its x and z, the normal
// points up, so y = sqrt(1 - x^2 - z^2)), bent by three travelling sine waves
// of the world x/z position and the time (params3.y, gGT->timer / 30). On a
// slope the waves are carried downhill (a river flows): the pattern moves by
// EGW_FLOW world units per second per unit of slope, along the normal's x/z.
// The waves fade out between EGW_RIPPLE_FULL and EGW_RIPPLE_ZERO of view depth,
// so far water does not sparkle.
//
// THE LOOKUP. u = azimuth of the reflected ray / 2 pi * 64 (wraps), v = 32 *
// (1 - elevation / (pi / 2)), the elevation clamped at the horizon (a ray that
// points down reads the horizon row). The env region starts at params3.zw in
// texels of the page (vramPage / vramClut as in "ne"); u wraps inside the 64
// texels, v is clamped. Bilinear over four texels, each through the CLUT.
//
// THE COLOUR. Schlick's fresnel (EGW_F0) weights the reflection: env * (K_BASE +
// K_FRESNEL * F) * vertex colour * tint, faded out between EGW_FADE_START and
// EGW_FADE_END of view depth. The material blends it additively (the PS1 water
// is additive too), so the floor below shows through.
//
// THE NUMBERS. Every constant is a macro below and goes into the GLSL text from
// here, and platform/engine_water.c computes the same lookup on the CPU from
// the same macros (EngWater_Lookup) for its selftest.

#define EGW_XSTR(x) EGW_STR(x)
#define EGW_STR(x) #x

#define EGW_ENV_TEXELS 64
#define EGW_FADE_START 12000.0
#define EGW_FADE_END 16000.0
#define EGW_RIPPLE_FULL 2000.0
#define EGW_RIPPLE_ZERO 8000.0
#define EGW_FLOW 400.0
#define EGW_K_BASE 0.30
#define EGW_K_FRESNEL 0.60
#define EGW_F0 0.02
// The three waves: direction x, direction z (unit), wavelength and speed in
// world units, slope amplitude.
#define EGW_W0_DX 0.8
#define EGW_W0_DZ 0.6
#define EGW_W0_LEN 640.0
#define EGW_W0_SPEED 120.0
#define EGW_W0_SLOPE 0.08
#define EGW_W1_DX -0.5
#define EGW_W1_DZ 0.8660254
#define EGW_W1_LEN 420.0
#define EGW_W1_SPEED 90.0
#define EGW_W1_SLOPE 0.06
#define EGW_W2_DX 0.1961161
#define EGW_W2_DZ -0.9805807
#define EGW_W2_LEN 900.0
#define EGW_W2_SPEED 150.0
#define EGW_W2_SLOPE 0.05

global_variable const char *ctr_native_water_shader =
    "#ifdef VERTEX\n"
    "layout(location = 0) in vec3 a_position;\n"
    "layout(location = 1) in vec2 a_texcoord;\n"
    "layout(location = 2) in vec4 a_color;\n"
    "VARYING_LOC0 out vec2 v_normalXZ;\n"
    "VARYING_LOC1 out vec4 v_color;\n"
    "VARYING_LOC2 out vec3 v_world;\n"
    "VARYING_LOC3 out float v_depth;\n"
    ENGINE_UNIFORM_BLOCK_GLSL
    "void main() {\n"
    "	v_normalXZ = a_texcoord;\n"
    "	v_color = a_color * tint;\n"
    "	v_world = a_position;\n"
    "	gl_Position = clipFromModel * vec4(a_position, 1.0);\n"
    // w is the view depth (Engine_CameraScreenFromWorld).
    "	v_depth = gl_Position.w;\n"
    "	gl_Position.z *= (1.0 + params2.z);\n"
    "}\n"
    "#endif\n"
    "#ifdef FRAGMENT\n"
    "VARYING_LOC0 in vec2 v_normalXZ;\n"
    "VARYING_LOC1 in vec4 v_color;\n"
    "VARYING_LOC2 in vec3 v_world;\n"
    "VARYING_LOC3 in float v_depth;\n"
    "SAMPLER_SLOT0 uniform sampler2D s_texture;\n"
    "SAMPLER_SLOT1 uniform sampler2D s_vram;\n"
    ENGINE_UNIFORM_BLOCK_GLSL
    "const float ENV = " EGW_XSTR(EGW_ENV_TEXELS) ".0;\n"
    "const float PI = 3.14159265;\n"
    // direction x, direction z, wavelength, speed; slope separately.
    "const vec4 W0 = vec4(" EGW_XSTR(EGW_W0_DX) ", " EGW_XSTR(EGW_W0_DZ) ", " EGW_XSTR(EGW_W0_LEN) ", " EGW_XSTR(EGW_W0_SPEED) ");\n"
    "const vec4 W1 = vec4(" EGW_XSTR(EGW_W1_DX) ", " EGW_XSTR(EGW_W1_DZ) ", " EGW_XSTR(EGW_W1_LEN) ", " EGW_XSTR(EGW_W1_SPEED) ");\n"
    "const vec4 W2 = vec4(" EGW_XSTR(EGW_W2_DX) ", " EGW_XSTR(EGW_W2_DZ) ", " EGW_XSTR(EGW_W2_LEN) ", " EGW_XSTR(EGW_W2_SPEED) ");\n"
    "const vec3 SLOPES = vec3(" EGW_XSTR(EGW_W0_SLOPE) ", " EGW_XSTR(EGW_W1_SLOPE) ", " EGW_XSTR(EGW_W2_SLOPE) ");\n"
    // One VRAM word, as in "ne".
    "int vramWord(ivec2 p) {\n"
    "	ivec2 w = ivec2(p.x & 1023, p.y & 511);\n"
    "	ivec2 b = ivec2(texelFetch(s_vram, w, 0).rg * 255.0 + 0.5);\n"
    "	return b.x | (b.y << 8);\n"
    "}\n"
    // One texel of the env region (u wraps, v clamps), through the CLUT for 4 and 8 bit pages.
    "vec3 envTexel(ivec2 e) {\n"
    "	ivec2 t = ivec2(params3.zw + 0.5) + ivec2(e.x & (" EGW_XSTR(EGW_ENV_TEXELS) " - 1), clamp(e.y, 0, " EGW_XSTR(EGW_ENV_TEXELS) " - 1));\n"
    "	t = t & 255;\n"
    "	ivec2 page = ivec2(vramPage.xy + 0.5);\n"
    "	int depth = int(vramPage.z + 0.5);\n"
    "	int word;\n"
    "	if (depth == 0) {\n"
    "		int index = (vramWord(page + ivec2(t.x >> 2, t.y)) >> ((t.x & 3) * 4)) & 15;\n"
    "		word = vramWord(ivec2(vramClut.xy + 0.5) + ivec2(index, 0));\n"
    "	} else if (depth == 1) {\n"
    "		int index = (vramWord(page + ivec2(t.x >> 1, t.y)) >> ((t.x & 1) * 8)) & 255;\n"
    "		word = vramWord(ivec2(vramClut.xy + 0.5) + ivec2(index, 0));\n"
    "	} else {\n"
    "		word = vramWord(page + t);\n"
    "	}\n"
    "	return vec3(float((word & 31) << 3), float(((word >> 5) & 31) << 3), float(((word >> 10) & 31) << 3)) / 255.0;\n"
    "}\n"
    "vec3 envBilinear(vec2 uv) {\n"
    "	vec2 f = uv - 0.5;\n"
    "	vec2 b = floor(f);\n"
    "	vec2 w = f - b;\n"
    "	ivec2 i = ivec2(b);\n"
    "	vec3 top = mix(envTexel(i), envTexel(i + ivec2(1, 0)), w.x);\n"
    "	vec3 bottom = mix(envTexel(i + ivec2(0, 1)), envTexel(i + ivec2(1, 1)), w.x);\n"
    "	return mix(top, bottom, w.y);\n"
    "}\n"
    "vec2 waveSlope(vec4 wave, float slope, vec2 p, float time) {\n"
    "	float phase = (2.0 * PI / wave.z) * (dot(wave.xy, p) - wave.w * time);\n"
    "	return slope * wave.xy * cos(phase);\n"
    "}\n"
    "void main() {\n"
    "	float time = params3.y;\n"
    "	vec2 nxz = v_normalXZ;\n"
    "	float ny = sqrt(max(1.0 - dot(nxz, nxz), 0.0));\n"
    "	vec2 p = v_world.xz - nxz * (" EGW_XSTR(EGW_FLOW) " * time);\n"
    "	float ripple = clamp((" EGW_XSTR(EGW_RIPPLE_ZERO) " - v_depth) / (" EGW_XSTR(EGW_RIPPLE_ZERO) " - " EGW_XSTR(EGW_RIPPLE_FULL) "), 0.0, 1.0);\n"
    "	vec2 slope = (waveSlope(W0, SLOPES.x, p, time) + waveSlope(W1, SLOPES.y, p, time) + waveSlope(W2, SLOPES.z, p, time)) * ripple;\n"
    "	vec3 n = normalize(vec3(nxz.x - slope.x, ny, nxz.y - slope.y));\n"
    "	vec3 view = normalize(v_world - params4.xyz);\n"
    "	vec3 r = reflect(view, n);\n"
    "	float elevation = asin(clamp(r.y, 0.0, 1.0));\n"
    // Straight up has no azimuth (atan(0, 0) is undefined in GLSL).
    "	float azimuth = ((abs(r.x) + abs(r.z)) > 1.0e-6) ? atan(r.z, r.x) : 0.0;\n"
    "	float u = fract(azimuth / (2.0 * PI)) * ENV;\n"
    "	float v = (ENV * 0.5) * (1.0 - elevation / (0.5 * PI));\n"
    "	float cosine = clamp(dot(-view, n), 0.0, 1.0);\n"
    "	float fresnel = " EGW_XSTR(EGW_F0) " + (1.0 - " EGW_XSTR(EGW_F0) ") * pow(1.0 - cosine, 5.0);\n"
    "	float fade = clamp((" EGW_XSTR(EGW_FADE_END) " - v_depth) / (" EGW_XSTR(EGW_FADE_END) " - " EGW_XSTR(EGW_FADE_START) "), 0.0, 1.0);\n"
    "	vec3 colour = envBilinear(vec2(u, v)) * (" EGW_XSTR(EGW_K_BASE) " + " EGW_XSTR(EGW_K_FRESNEL) " * fresnel) * v_color.rgb * fade;\n"
    "	fragColor = vec4(colour, 1.0);\n"
    "}\n"
    "#endif\n";
