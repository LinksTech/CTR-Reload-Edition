#include <platform/engine.h>
#include <platform/native_mods.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ENGINE MATERIAL - see include/platform/engine.h.
//
// The CPU side of materials, PS1 blend equations and NIGHT. Everything here is
// the reference the engine's GPU blend states and shaders must reproduce.

// --------------------------------------------------------------------------
// tpage and clut words
//
// include/psx/libgpu.h:206 getTPage(tp, abr, x, y):
//   bits 0..3 x / 64, bit 4 (y & 0x100), bits 5..6 abr, bits 7..8 depth,
//   bit 11 (y & 0x200). The engine contract has baseY 0 or 256, so bit 11 is
//   not decoded here.
// include/psx/libgpu.h:208 getClut(x, y): (y << 6) | ((x >> 4) & 0x3f).

EngineTpage Engine_DecodeTpage(u16 tpage)
{
	EngineTpage t;
	t.baseX = (tpage & 0xF) * 64;
	t.baseY = ((tpage >> 4) & 1) * 256;
	t.abr = (tpage >> 5) & 3;
	t.depth = (tpage >> 7) & 3;
	return t;
}

void Engine_DecodeClut(u16 clut, int *x, int *y)
{
	if (x != NULL)
	{
		*x = (clut & 0x3F) * 16;
	}
	if (y != NULL)
	{
		*y = (clut >> 6) & 0x1FF;
	}
}

EngineBlend Engine_BlendFromAbr(int abr)
{
	switch (abr)
	{
	case 0: return ENGINE_BLEND_PSX_AVERAGE;
	case 1: return ENGINE_BLEND_PSX_ADD;
	case 2: return ENGINE_BLEND_PSX_SUBTRACT;
	case 3: return ENGINE_BLEND_PSX_ADD_QUARTER;
	default: return ENGINE_BLEND_OPAQUE;
	}
}

// The blend equations match the Vulkan blend states of the PS1 path
// (platform/native_gfx_vk.c NativeVk_BlendAttachment, line ~8860, with the
// constants 0.5 (alpha) and 0.25 (colour) of line ~9130), which the engine's
// GPU blend will use too. The GPU computes in float on unorm8 and converts
// the result to the nearest integer; exact .5 ties are implementation
// defined, this reference rounds them up (half up).
//   AVERAGE      0.5*B + 0.5*F  = (B + F) / 2        -> (B + F + 1) >> 1
//   ADD          B + F, saturate                     -> min(B + F, 255)
//   SUBTRACT     REVERSE_SUBTRACT: B - F, saturate   -> max(B - F, 0)
//   ADD_QUARTER  B + 0.25*F                          -> min(B + ((F + 2) >> 2), 255)
//   MULTIPLY     (ZERO, SRC_COLOR) dst * src         -> (B * F + 127) / 255
//                (no tie is possible: 255 is odd)
u8 Engine_BlendChannel(EngineBlend blend, u8 background, u8 foreground)
{
	int b = background;
	int f = foreground;
	int r;

	switch (blend)
	{
	case ENGINE_BLEND_PSX_AVERAGE:
		r = (b + f + 1) >> 1;
		break;
	case ENGINE_BLEND_PSX_ADD:
		r = b + f;
		break;
	case ENGINE_BLEND_PSX_SUBTRACT:
		r = b - f;
		break;
	case ENGINE_BLEND_PSX_ADD_QUARTER:
		r = b + ((f + 2) >> 2);
		break;
	case ENGINE_BLEND_MULTIPLY:
		r = (b * f + 127) / 255;
		break;
	default: // OPAQUE, MASK and anything out of range: the foreground
		r = f;
		break;
	}

	r = (r < 0) ? 0 : r;
	r = (r > 255) ? 255 : r;
	return (u8)r;
}

// --------------------------------------------------------------------------
// material table

global_variable EngineMaterial s_materials[ENGINE_MATERIALS_MAX];
global_variable int s_materialCount = 0;

int Engine_MaterialRegister(const EngineMaterial *m)
{
	if ((m == NULL) || (s_materialCount >= ENGINE_MATERIALS_MAX))
	{
		return -1;
	}
	if ((m->blend >= ENGINE_BLEND_COUNT) || (m->texSource > ENGINE_TEX_VRAM_TABLE))
	{
		return -1;
	}
	s_materials[s_materialCount] = *m;
	return s_materialCount++;
}

const EngineMaterial *Engine_Material(int index)
{
	if ((index < 0) || (index >= s_materialCount))
	{
		return NULL;
	}
	return &s_materials[index];
}

void Engine_MaterialsReset(void)
{
	s_materialCount = 0;
}

// --------------------------------------------------------------------------
// light

global_variable EngineLight s_light = {{1.0f, 1.0f, 1.0f}, 0};

void Engine_SetLight(const EngineLight *light)
{
	if (light == NULL)
	{
		return;
	}
	s_light = *light;
	s_light.nightPercent = (s_light.nightPercent < 0) ? 0 : ((s_light.nightPercent > 90) ? 90 : s_light.nightPercent);
}

const EngineLight *Engine_GetLight(void)
{
	return &s_light;
}

// The PS1 path multiplies the view by the colour NativeMods_NightColor gives
// (platform/native_mods.c:1470), blue = 100 - night percent being the neutral
// channel; the BM_MULTIPLY blend is (B * F + 127) / 255 as above. The channel
// of the single-value call is blue; red and green are a little darker.
u8 Engine_NightChannel(int nightPercent, u8 value)
{
	u8 rgb[3];

	if (!NativeMods_NightColor(nightPercent, rgb))
	{
		return value;
	}
	return Engine_BlendChannel(ENGINE_BLEND_MULTIPLY, value, rgb[2]);
}

// --------------------------------------------------------------------------
// pipelines ahead of the race (round of 2026-10-15)
//
// The draw states a material can reach in Engine_RenderDrawRange
// (platform/engine_render.c): its program; its blend as Engine_RenderBlendMode
// maps it, and BM_NONE for the opaque first pass of a semi-transparent VRAM face
// (twoPass); depth test on with write on (opaque) or off (transparent draws,
// ENGINE_MATERIAL_NO_DEPTH_WRITE), and depth test off (ENGINE_DRAW_NO_DEPTH,
// the sky); cull none for a double-sided material, else back or front (by the
// determinant of the draw's matrix: a mirror image draws front); compare
// GREATER_OR_EQUAL, alpha not written. Engine_MaterialsWarmPipelines hands every
// distinct one of the loaded materials to the Vulkan backend, which builds
// their pipelines at the load point instead of inside a race frame.

#define ENG_WARM_STATES_MAX 512

int NativeGfxVK_WarmEngine(const uint32_t *programs, const uint8_t *blends, const NativeGfxDrawState *states, int count,
                           const NativeGfxVertexBufferDesc *layoutDesc);

global_variable u32 s_warmPrograms[ENG_WARM_STATES_MAX];
global_variable u8 s_warmBlends[ENG_WARM_STATES_MAX];
global_variable NativeGfxDrawState s_warmStates[ENG_WARM_STATES_MAX];

// Engine_RenderBlendMode (engine_render.c), the same mapping.
internal u8 EngMat_WarmBlend(u8 blend)
{
	switch (blend)
	{
	case ENGINE_BLEND_PSX_AVERAGE:
		return (u8)BM_AVERAGE;
	case ENGINE_BLEND_PSX_ADD:
		return (u8)BM_ADD;
	case ENGINE_BLEND_PSX_SUBTRACT:
		return (u8)BM_SUBTRACT;
	case ENGINE_BLEND_PSX_ADD_QUARTER:
		return (u8)BM_ADD_QUATER_SOURCE;
	case ENGINE_BLEND_MULTIPLY:
		return (u8)BM_MULTIPLY;
	default:
		return (u8)BM_NONE;
	}
}

// Adds the states of material m, drawn by program handle `program`, that are
// not in the lists yet; returns the new count (at most max).
internal int EngMat_WarmStatesOf(const EngineMaterial *m, u32 program, u32 *programs, u8 *blends, NativeGfxDrawState *states, int count, int max)
{
	// depth test, depth write
	static const int depth[3][2] = {{1, 1}, {1, 0}, {0, 0}};
	static const NativeGfxCull culls[2] = {NATIVE_GFX_CULL_BACK, NATIVE_GFX_CULL_FRONT};
	const int vram = (m->texSource == ENGINE_TEX_VRAM) || (m->texSource == ENGINE_TEX_VRAM_TABLE);
	const u8 blend = EngMat_WarmBlend(m->blend);
	const int twoPass = vram && (blend != (u8)BM_NONE) && (blend != (u8)BM_MULTIPLY) && ((m->flags & ENGINE_MATERIAL_SEMI_STP) != 0u);
	const int doubleSided = (m->flags & ENGINE_MATERIAL_DOUBLE_SIDED) != 0u;

	for (int b = 0; b < (twoPass ? 2 : 1); b++)
	{
		const u8 passBlend = (b == 0) ? blend : (u8)BM_NONE;

		for (int d = 0; d < 3; d++)
		{
			for (int c = 0; c < (doubleSided ? 1 : 2); c++)
			{
				NativeGfxDrawState s;
				int i;

				if ((depth[d][1] != 0) && ((m->flags & ENGINE_MATERIAL_NO_DEPTH_WRITE) != 0u))
				{
					continue;
				}
				memset(&s, 0, sizeof(s));
				s.depthTest = depth[d][0];
				s.depthWrite = depth[d][1];
				s.depthCompare = NATIVE_GFX_COMPARE_GREATER_OR_EQUAL;
				s.cull = doubleSided ? NATIVE_GFX_CULL_NONE : culls[c];
				s.colorWriteOff = NATIVE_GFX_COLOR_A;
				for (i = 0; i < count; i++)
				{
					if ((programs[i] == program) && (blends[i] == passBlend) && (states[i].depthTest == s.depthTest) && (states[i].depthWrite == s.depthWrite) &&
					    (states[i].cull == s.cull))
					{
						break;
					}
				}
				if ((i < count) || (count >= max))
				{
					continue;
				}
				programs[count] = program;
				blends[count] = passBlend;
				states[count] = s;
				count++;
			}
		}
	}
	return count;
}

void Engine_MaterialsWarmPipelines(void)
{
	NativeGfxVertexBufferDesc desc;
	int count = 0;

	if (!g_cfg_engine)
	{
		return;
	}
	for (int k = 0; k < s_materialCount; k++)
	{
		const u32 program = Engine_ProgramOf((int)s_materials[k].program);

		if (program != 0xffffffffu)
		{
			count = EngMat_WarmStatesOf(&s_materials[k], program, s_warmPrograms, s_warmBlends, s_warmStates, count, ENG_WARM_STATES_MAX);
		}
	}
	// The vertex layout of the engine buffers (Engine_RenderFillVertexDesc, engine_render.c).
	memset(&desc, 0, sizeof(desc));
	desc.stride = (int)sizeof(EngineVertex);
	desc.attribCount = 4;
	desc.attribs[0] = (NativeGfxVertexAttrib){.slot = a_position, .components = 3, .type = NATIVE_GFX_ATTR_FLOAT32, .offset = offsetof(EngineVertex, position)};
	desc.attribs[1] = (NativeGfxVertexAttrib){.slot = a_texcoord, .components = 2, .type = NATIVE_GFX_ATTR_FLOAT32, .offset = offsetof(EngineVertex, texcoord)};
	desc.attribs[2] = (NativeGfxVertexAttrib){.slot = a_color, .components = 4, .type = NATIVE_GFX_ATTR_UNORM8, .offset = offsetof(EngineVertex, color)};
	desc.attribs[3] = (NativeGfxVertexAttrib){.slot = a_extra, .components = 4, .type = NATIVE_GFX_ATTR_UINT8, .offset = offsetof(EngineVertex, uvBox)};
	NativeGfxVK_WarmEngine(s_warmPrograms, s_warmBlends, s_warmStates, count, &desc);
}

// --------------------------------------------------------------------------
// selftest

#define MAT_CHECK(cond, ...)                                   \
	do                                                         \
	{                                                          \
		(*checks)++;                                           \
		if (!(cond))                                           \
		{                                                      \
			failures++;                                        \
			printf("engine selftest: material: " __VA_ARGS__); \
			printf("\n");                                      \
		}                                                      \
	} while (0)

// Independent reference: exact integer rational arithmetic, rounded half up.
internal int MaterialRefBlend(int mode, int b, int f)
{
	int num, den, q;
	switch (mode)
	{
	case 2: num = b + f; den = 2; break;     // average
	case 3: num = b + f; den = 1; break;     // add
	case 4: num = b - f; den = 1; break;     // subtract
	case 5: num = 4 * b + f; den = 4; break; // add quarter
	case 6: num = b * f; den = 255; break;   // multiply
	default: return f;
	}
	q = (num >= 0) ? ((2 * num + den) / (2 * den)) : 0;
	return (q > 255) ? 255 : q;
}

int Engine_SelfTestMaterial(int *checks)
{
	int failures = 0;
	int localChecks = 0;
	int mode, b, f, i;

	if (checks == NULL)
	{
		checks = &localChecks;
	}

	// tpage decode: word, baseX, baseY, depth, abr
	{
		static const struct
		{
			u16 w;
			int x, y, depth, abr;
		} tp[] = {
		    {0x0000, 0, 0, 0, 0},     {0x000F, 960, 0, 0, 0},   {0x0010, 0, 256, 0, 0},   {0x001F, 960, 256, 0, 0},
		    {0x0020, 0, 0, 0, 1},     {0x0040, 0, 0, 0, 2},     {0x0060, 0, 0, 0, 3},     {0x0080, 0, 0, 1, 0},
		    {0x0100, 0, 0, 2, 0},     {0x0180, 0, 0, 3, 0},     {0x00FF, 960, 256, 1, 3}, {0x0145, 320, 0, 2, 2},
		    {0x0138, 512, 256, 2, 1}, {0xFFFF, 960, 256, 3, 3}, {0x0800, 0, 0, 0, 0},     {0x0094, 256, 256, 1, 0},
		};
		for (i = 0; i < 16; i++)
		{
			EngineTpage t = Engine_DecodeTpage(tp[i].w);
			MAT_CHECK((t.baseX == tp[i].x) && (t.baseY == tp[i].y) && (t.depth == tp[i].depth) && (t.abr == tp[i].abr), "tpage 0x%04x gives %d/%d/%d/%d",
			          tp[i].w, t.baseX, t.baseY, t.depth, t.abr);
		}
	}
	// the macro round trip
	{
		u16 w = (u16)getTPage(2, 3, 320, 256);
		EngineTpage t = Engine_DecodeTpage(w);
		MAT_CHECK((t.baseX == 320) && (t.baseY == 256) && (t.depth == 2) && (t.abr == 3), "getTPage round trip");
	}

	// clut decode
	{
		static const struct
		{
			u16 w;
			int x, y;
		} cl[] = {
		    {0x0000, 0, 0},     {0x0001, 16, 0},   {0x003F, 1008, 0}, {0x0040, 0, 1},   {0x7FC0, 0, 511},  {0x7FFF, 1008, 511}, {0x8000, 0, 0}, {0xFFC0, 0, 511},
		    {0x7C1F, 496, 496}, {0x0A52, 288, 41}, {0x01E5, 592, 7},  {0x4000, 0, 256}, {0x003E, 992, 0},  {0x0FC1, 16, 63},    {0x1234, 832, 72}, {0x7F7F, 1008, 509},
		};
		for (i = 0; i < 16; i++)
		{
			int x = -1, y = -1;
			Engine_DecodeClut(cl[i].w, &x, &y);
			MAT_CHECK((x == cl[i].x) && (y == cl[i].y), "clut 0x%04x gives %d/%d, expected %d/%d", cl[i].w, x, y, cl[i].x, cl[i].y);
		}
		{
			u16 w = (u16)getClut(320, 480);
			int x, y;
			Engine_DecodeClut(w, &x, &y);
			MAT_CHECK((x == 320) && (y == 480), "getClut round trip");
		}
	}

	// abr to blend
	MAT_CHECK(Engine_BlendFromAbr(0) == ENGINE_BLEND_PSX_AVERAGE, "abr 0");
	MAT_CHECK(Engine_BlendFromAbr(1) == ENGINE_BLEND_PSX_ADD, "abr 1");
	MAT_CHECK(Engine_BlendFromAbr(2) == ENGINE_BLEND_PSX_SUBTRACT, "abr 2");
	MAT_CHECK(Engine_BlendFromAbr(3) == ENGINE_BLEND_PSX_ADD_QUARTER, "abr 3");
	MAT_CHECK((Engine_BlendFromAbr(4) == ENGINE_BLEND_OPAQUE) && (Engine_BlendFromAbr(-1) == ENGINE_BLEND_OPAQUE), "abr out of range");

	// blend channel: all pairs against the reference
	for (mode = 0; mode < ENGINE_BLEND_COUNT; mode++)
	{
		int bad = 0;
		for (b = 0; b < 256; b++)
		{
			for (f = 0; f < 256; f++)
			{
				if (Engine_BlendChannel((EngineBlend)mode, (u8)b, (u8)f) != MaterialRefBlend(mode, b, f))
				{
					if (bad++ == 0)
					{
						printf("engine selftest: material: blend %d B=%d F=%d gives %d, expected %d\n", mode, b, f,
						       Engine_BlendChannel((EngineBlend)mode, (u8)b, (u8)f), MaterialRefBlend(mode, b, f));
					}
				}
			}
		}
		MAT_CHECK(bad == 0, "blend %d differs from the reference in %d pairs", mode, bad);
	}
	// By hand: average(100,50)=75, average(1,0)=1 (half up); add(200,100)=255, add(10,20)=30;
	// subtract(10,20)=0, subtract(200,50)=150; quarter(100,40)=110, quarter(250,100)=255, quarter(0,2)=1;
	// multiply(255,255)=255, multiply(128,128)=64 (16384/255=64.25), multiply(255,100)=100.
	MAT_CHECK(Engine_BlendChannel(ENGINE_BLEND_PSX_AVERAGE, 100, 50) == 75, "average 100,50");
	MAT_CHECK(Engine_BlendChannel(ENGINE_BLEND_PSX_AVERAGE, 1, 0) == 1, "average 1,0");
	MAT_CHECK(Engine_BlendChannel(ENGINE_BLEND_PSX_ADD, 200, 100) == 255, "add 200,100");
	MAT_CHECK(Engine_BlendChannel(ENGINE_BLEND_PSX_ADD, 10, 20) == 30, "add 10,20");
	MAT_CHECK(Engine_BlendChannel(ENGINE_BLEND_PSX_SUBTRACT, 10, 20) == 0, "subtract 10,20");
	MAT_CHECK(Engine_BlendChannel(ENGINE_BLEND_PSX_SUBTRACT, 200, 50) == 150, "subtract 200,50");
	MAT_CHECK(Engine_BlendChannel(ENGINE_BLEND_PSX_ADD_QUARTER, 100, 40) == 110, "quarter 100,40");
	MAT_CHECK(Engine_BlendChannel(ENGINE_BLEND_PSX_ADD_QUARTER, 250, 100) == 255, "quarter 250,100");
	MAT_CHECK(Engine_BlendChannel(ENGINE_BLEND_PSX_ADD_QUARTER, 0, 2) == 1, "quarter 0,2");
	MAT_CHECK(Engine_BlendChannel(ENGINE_BLEND_MULTIPLY, 255, 255) == 255, "multiply 255,255");
	MAT_CHECK(Engine_BlendChannel(ENGINE_BLEND_MULTIPLY, 128, 128) == 64, "multiply 128,128");
	MAT_CHECK(Engine_BlendChannel(ENGINE_BLEND_MULTIPLY, 255, 100) == 100, "multiply 255,100");
	MAT_CHECK((Engine_BlendChannel(ENGINE_BLEND_OPAQUE, 9, 77) == 77) && (Engine_BlendChannel(ENGINE_BLEND_MASK, 9, 78) == 78), "opaque/mask");

	// material table
	{
		EngineMaterial m;
		int idx, n;
		Engine_MaterialsReset();
		memset(&m, 0, sizeof(m));
		m.blend = ENGINE_BLEND_PSX_ADD;
		m.texSource = ENGINE_TEX_VRAM;
		m.tpage = 0x123;
		MAT_CHECK(Engine_Material(0) == NULL, "empty table lookup");
		MAT_CHECK(Engine_MaterialRegister(NULL) == -1, "register NULL");
		idx = Engine_MaterialRegister(&m);
		MAT_CHECK((idx == 0) && (Engine_Material(0) != NULL) && (Engine_Material(0)->tpage == 0x123) && (Engine_Material(0) != &m), "first register is a copy");
		m.blend = ENGINE_BLEND_COUNT;
		MAT_CHECK(Engine_MaterialRegister(&m) == -1, "blend out of range");
		m.blend = ENGINE_BLEND_OPAQUE;
		m.texSource = ENGINE_TEX_VRAM_TABLE + 1;
		MAT_CHECK(Engine_MaterialRegister(&m) == -1, "texSource out of range");
		m.texSource = ENGINE_TEX_VRAM_TABLE;
		MAT_CHECK(Engine_MaterialRegister(&m) == 1, "the mosaic table source (the last one) is a texSource");
		m.texSource = ENGINE_TEX_NATIVE;
		for (n = 2; n < ENGINE_MATERIALS_MAX; n++)
		{
			if (Engine_MaterialRegister(&m) != n)
			{
				break;
			}
		}
		MAT_CHECK(n == ENGINE_MATERIALS_MAX, "register up to full stopped at %d", n);
		MAT_CHECK(Engine_MaterialRegister(&m) == -1, "overflow");
		MAT_CHECK((Engine_Material(-1) == NULL) && (Engine_Material(ENGINE_MATERIALS_MAX) == NULL) && (Engine_Material(ENGINE_MATERIALS_MAX - 1) != NULL),
		          "lookup bounds");
		Engine_MaterialsReset();
		MAT_CHECK((Engine_Material(0) == NULL) && (Engine_MaterialRegister(&m) == 0), "reset");
		Engine_MaterialsReset();
	}

	// NIGHT: the multiply by the native_mods.c colour (blue channel)
	{
		static const int pct[] = {0, 1, 50, 90, 100};
		for (i = 0; i < 5; i++)
		{
			u8 rgb[3];
			int v, bad = 0;
			int on = NativeMods_NightColor(pct[i], rgb);
			for (v = 0; v < 256; v++)
			{
				int expect = on ? ((v * rgb[2] + 127) / 255) : v;
				if (Engine_NightChannel(pct[i], (u8)v) != expect)
				{
					bad++;
				}
			}
			MAT_CHECK(bad == 0, "night %d percent differs in %d values", pct[i], bad);
		}
		MAT_CHECK(Engine_NightChannel(0, 200) == 200, "night 0 unchanged");
		// blue keeps 100 - percent: (255 * 50 + 50) / 100 = 128; 90 percent: (255 * 10 + 50) / 100 = 26
		MAT_CHECK(Engine_NightChannel(50, 255) == 128, "night 50 white gives %d", Engine_NightChannel(50, 255));
		MAT_CHECK(Engine_NightChannel(90, 255) == 26, "night 90 white gives %d", Engine_NightChannel(90, 255));
	}

	// light
	{
		EngineLight l = {{0.5f, 0.25f, 2.0f}, 150};
		EngineLight saved = *Engine_GetLight();
		const EngineLight *g;
		MAT_CHECK((saved.nightPercent >= 0) && (saved.nightPercent <= 90), "light state in range");
		Engine_SetLight(&l);
		g = Engine_GetLight();
		MAT_CHECK((g->nightPercent == 90) && (g->ambient[0] == 0.5f) && (g->ambient[2] == 2.0f), "light clamp high");
		l.nightPercent = -5;
		Engine_SetLight(&l);
		MAT_CHECK(Engine_GetLight()->nightPercent == 0, "light clamp low");
		l.nightPercent = 40;
		Engine_SetLight(&l);
		MAT_CHECK(Engine_GetLight()->nightPercent == 40, "light value kept");
		Engine_SetLight(NULL);
		MAT_CHECK(Engine_GetLight()->nightPercent == 40, "light NULL ignored");
		Engine_SetLight(&saved);
	}

	// pipelines ahead of the race: the states of a semi-transparent double-sided
	// VRAM material (both passes, three depth states, no cull) and of an opaque
	// one-sided one without depth write (two depth states, back and front).
	{
		static u32 programs[16];
		static u8 blends[16];
		static NativeGfxDrawState states[16];
		EngineMaterial warmA;
		EngineMaterial warmB;
		int n;

		memset(&warmA, 0, sizeof(warmA));
		warmA.blend = ENGINE_BLEND_PSX_AVERAGE;
		warmA.texSource = ENGINE_TEX_VRAM;
		warmA.flags = ENGINE_MATERIAL_SEMI_STP | ENGINE_MATERIAL_DOUBLE_SIDED;
		memset(&warmB, 0, sizeof(warmB));
		warmB.blend = ENGINE_BLEND_OPAQUE;
		warmB.flags = ENGINE_MATERIAL_NO_DEPTH_WRITE;
		n = EngMat_WarmStatesOf(&warmA, 7u, programs, blends, states, 0, 16);
		MAT_CHECK((n == 6) && (blends[0] == (u8)BM_AVERAGE) && (blends[3] == (u8)BM_NONE) && (states[0].depthTest == 1) && (states[0].depthWrite == 1) &&
		              (states[2].depthTest == 0) && (states[0].cull == NATIVE_GFX_CULL_NONE) && (states[0].colorWriteOff == NATIVE_GFX_COLOR_A),
		          "warm states of a semi double-sided VRAM material: %d", n);
		n = EngMat_WarmStatesOf(&warmB, 7u, programs, blends, states, n, 16);
		MAT_CHECK((n == 10) && (states[6].depthWrite == 0) && (states[6].cull == NATIVE_GFX_CULL_BACK) && (states[7].cull == NATIVE_GFX_CULL_FRONT),
		          "warm states of an opaque one-sided material without depth write: %d", n);
		MAT_CHECK(EngMat_WarmStatesOf(&warmA, 7u, programs, blends, states, n, 16) == 10, "warm states not repeated");
		MAT_CHECK(EngMat_WarmStatesOf(&warmA, 8u, programs, blends, states, n, 12) == 12, "warm states of another program, held to the room");
		MAT_CHECK(EngMat_WarmBlend(ENGINE_BLEND_MASK) == (u8)BM_NONE && EngMat_WarmBlend(ENGINE_BLEND_PSX_SUBTRACT) == (u8)BM_SUBTRACT,
		          "warm blend mapping");
	}

	return failures;
}
