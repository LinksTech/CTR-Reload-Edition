#include <platform/engine.h>
#include <platform/native_probe.h>
#include <platform/native_tex.h>

// ENGINE TESTSCENE - see include/platform/engine.h.
//
// A textured box (the native probe body, unit box x/z -1..1, y 0..1) 600 units
// in front of the camera of view 0, turning slowly with the game timer. It goes
// through the whole engine path: material and mesh tables, the upload at the
// load stage, a draw submitted into the scene and linked into the ordering
// table of the view's push buffer, drawn by the OT parser.

#define ENGINE_TESTSCENE_DISTANCE 600.0f // view z of the box's centre
#define ENGINE_TESTSCENE_SCALE 200.0f
#define ENGINE_TESTSCENE_TURN_PER_TICK 0.02f // radians of yaw per game tick

internal int s_testMesh = ENGINE_MESH_INVALID;
internal int s_testMaterial = -1;
internal unsigned long long s_testFramesDrawn = 0;
internal int s_testLogged = 0;

// The probe body is a struct NativeProbeVertex array (24 bytes, no UV box);
// the load copies it field by field into engine vertices (Engine_TestSceneLoad).
CTR_STATIC_ASSERT(sizeof(EngineVertex) == sizeof(struct NativeProbeVertex) + 4u);

// General affine inverse of a view matrix (3x3 A and translation t): inv(A) and
// -inv(A) t. A transpose is NOT enough: the game's matrix_ViewProj has its Y row
// scaled for the aspect (about 0x360/0x600), so it is not orthonormal; with the
// transpose the translation's y came back scaled by s^2 instead of cancelled and
// the box landed thousands of units off screen. Returns 0 (identity in out) when
// the matrix is singular.
internal int Engine_TestSceneInvertRigid(const EngineMat4 *v, EngineMat4 *out)
{
	// a(r, c) = v->m[c * 4 + r] (column-major)
	const double a00 = v->m[0], a10 = v->m[1], a20 = v->m[2];
	const double a01 = v->m[4], a11 = v->m[5], a21 = v->m[6];
	const double a02 = v->m[8], a12 = v->m[9], a22 = v->m[10];
	const double c00 = a11 * a22 - a12 * a21;
	const double c01 = a12 * a20 - a10 * a22;
	const double c02 = a10 * a21 - a11 * a20;
	const double det = a00 * c00 + a01 * c01 + a02 * c02;
	double inv[3][3];
	int i;

	Engine_Mat4Identity(out);
	if ((det < 1e-12) && (det > -1e-12))
	{
		return 0;
	}
	inv[0][0] = c00 / det;
	inv[0][1] = (a02 * a21 - a01 * a22) / det;
	inv[0][2] = (a01 * a12 - a02 * a11) / det;
	inv[1][0] = c01 / det;
	inv[1][1] = (a00 * a22 - a02 * a20) / det;
	inv[1][2] = (a02 * a10 - a00 * a12) / det;
	inv[2][0] = c02 / det;
	inv[2][1] = (a01 * a20 - a00 * a21) / det;
	inv[2][2] = (a00 * a11 - a01 * a10) / det;
	for (i = 0; i < 3; i++)
	{
		int k;

		for (k = 0; k < 3; k++)
		{
			out->m[k * 4 + i] = (float)inv[i][k];
		}
		out->m[12 + i] = (float)-(inv[i][0] * v->m[12] + inv[i][1] * v->m[13] + inv[i][2] * v->m[14]);
	}
	return 1;
}

void Engine_TestSceneLoad(void)
{
	NativeTexDesc texDesc;
	NativeTexResult texResult = NATIVE_TEX_OK;
	TextureID texture;
	EngineMaterial material;
	EngineRange range;
	EngineMeshDesc mesh;
	EngineVertex vertices[NATIVE_PROBE_VERTEX_COUNT];
	const struct NativeProbeVertex *probe = NativeProbe_TexturedVertices();

	// Once per load: a second load has had its tables reset by
	// Engine_RenderLevelUnload; a table that still holds the mesh keeps it.
	if ((s_testMesh != ENGINE_MESH_INVALID) && (Engine_MeshDesc(s_testMesh) != NULL))
	{
		return;
	}

	memset(&texDesc, 0, sizeof(texDesc));
	texDesc.width = NATIVE_PROBE_TEXTURE_SIZE;
	texDesc.height = NATIVE_PROBE_TEXTURE_SIZE;
	texDesc.rgba = NativeProbe_TexturePixels();
	texDesc.flags = 0; // sRGB colours, levels computed
	texDesc.wrapU = NATIVE_GFX_WRAP_CLAMP;
	texDesc.wrapV = NATIVE_GFX_WRAP_CLAMP;
	texDesc.name = "engine test scene";
	texture = NativeTex_Create(&texDesc, &texResult);
	if (texture == NATIVE_GFX_INVALID)
	{
		Platform_Log("[CTR Engine] test scene: texture refused (%s)\n", NativeTex_ResultName(texResult));
		return;
	}

	memset(&material, 0, sizeof(material));
	material.blend = ENGINE_BLEND_OPAQUE;
	material.texSource = ENGINE_TEX_NATIVE;
	// Double sided: the model matrix mirrors the probe body (view y points
	// down), which flips the winding of every face.
	material.flags = ENGINE_MATERIAL_SRGB | ENGINE_MATERIAL_DOUBLE_SIDED;
	material.nativeTexture = (u32)texture;
	material.tint[0] = material.tint[1] = material.tint[2] = material.tint[3] = 1.0f;
	s_testMaterial = Engine_MaterialRegister(&material);

	memset(&range, 0, sizeof(range));
	range.firstIndex = 0;
	range.indexCount = NATIVE_PROBE_INDEX_COUNT;
	range.material = (u16)s_testMaterial;

	// The probe's vertices as engine vertices (Engine_MeshCreate copies them): a
	// native texture reads no VRAM page, the UV box is the whole page.
	for (int i = 0; i < NATIVE_PROBE_VERTEX_COUNT; i++)
	{
		memcpy(vertices[i].position, probe[i].position, sizeof(vertices[i].position));
		memcpy(vertices[i].texcoord, probe[i].texcoord, sizeof(vertices[i].texcoord));
		memcpy(vertices[i].color, probe[i].color, sizeof(vertices[i].color));
		Engine_VertexBoxFull(&vertices[i], 1);
	}

	memset(&mesh, 0, sizeof(mesh));
	mesh.vertices = vertices;
	mesh.verticesPerFrame = NATIVE_PROBE_VERTEX_COUNT;
	mesh.frames = 1;
	mesh.indices = NativeProbe_Indices();
	mesh.indexCount = NATIVE_PROBE_INDEX_COUNT;
	mesh.ranges = &range;
	mesh.rangeCount = 1;
	mesh.name = "engine test box";
	s_testMesh = Engine_MeshCreate(&mesh);

	Platform_Log("[CTR Engine] test scene: mesh %d, material %d\n", s_testMesh, s_testMaterial);
}

void Engine_TestSceneCollect(struct GameTracker *gGT)
{
	const EngineCamera *cam;
	EngineMat4 screenFromWorld;
	EngineMat4 worldFromView;
	EngineMat4 model;
	EngineMat4 viewModel;
	EngineMat4 clip;
	EngineDraw draw;
	float yaw;
	float c;
	float s;
	int cell;

	// Only in a race: not the menus, a level loaded, a mesh made.
	if (((gGT->gameMode1 & MAIN_MENU) != 0) || (gGT->level1 == NULL) || (s_testMesh == ENGINE_MESH_INVALID))
	{
		return;
	}
	cam = Engine_FrameCamera(0);
	if (cam == NULL)
	{
		return;
	}

	// Model in view space: yaw about the vertical axis, scaled (y negative:
	// view y points down, the box stands up on screen), the box's middle moved
	// to the origin, then 600 units in front of the camera.
	yaw = (float)gGT->timer * ENGINE_TESTSCENE_TURN_PER_TICK;
	c = cosf(yaw) * ENGINE_TESTSCENE_SCALE;
	s = sinf(yaw) * ENGINE_TESTSCENE_SCALE;
	Engine_Mat4Identity(&model);
	// columns: x axis, y axis, z axis, translation
	model.m[0] = c;
	model.m[2] = -s;
	model.m[5] = -ENGINE_TESTSCENE_SCALE;
	model.m[8] = s;
	model.m[10] = c;
	// box middle (0, 0.5, 0) to the origin, then to (0, 0, distance)
	model.m[13] = 0.5f * ENGINE_TESTSCENE_SCALE;
	model.m[14] = ENGINE_TESTSCENE_DISTANCE;
	viewModel = model;

	// clipFromModel = screenFromWorld * worldFromView * viewModel
	Engine_CameraScreenFromWorld(cam, &screenFromWorld);
	if (!Engine_TestSceneInvertRigid(&cam->viewFromWorld, &worldFromView))
	{
		return;
	}
	Engine_Mat4Mul(&worldFromView, &viewModel, &model);
	Engine_Mat4Mul(&screenFromWorld, &model, &clip);

	memset(&draw, 0, sizeof(draw));
	draw.mesh = s_testMesh;
	draw.frame = 0;
	draw.clipFromModel = clip;
	draw.tint[0] = draw.tint[1] = draw.tint[2] = draw.tint[3] = 1.0f;
	draw.materialOverride = -1;
	draw.sortDepth = ENGINE_TESTSCENE_DISTANCE;
	// OT cell: the level's rule, view z >> 6 (DrawLevelOvr1P.c: otIndex =
	// (maxDepth >> 6) + drawOrder); a higher cell is farther.
	cell = (int)(ENGINE_TESTSCENE_DISTANCE) >> 6;
	if (cell < 0)
	{
		cell = 0;
	}
	if (cell > 0x3fe)
	{
		cell = 0x3fe;
	}
	draw.otCell = (u32)cell;
	draw.dynMaterial = -1;

	if (Engine_RenderSubmitLinked(0, &draw, gGT->pushBuffer[0].ptrOT, cell))
	{
		s_testFramesDrawn++;
		if (!s_testLogged)
		{
			s_testLogged = 1;
			Platform_Log("[CTR Engine] test scene: first draw submitted, view 0, OT cell %d\n", cell);
		}
	}
}

// --------------------------------------------------------------------------
// THE SAMPLE SELFTEST (--engine-sample-selftest, ctest engine_sample_selftest;
// round of 2026-10-16). Whether the GPU keeps "ne" inside the UV box of a
// triangle (engine.h, THE UV BOX) - the box= of the material oracle says what
// the code claims, this asks the device. A hidden window and the Vulkan device
// come up; no game data, no disc.
//
// THE VRAM. A 4-bit page at (896, 256) with its CLUT at (800, 286): index 1 is
// green, 2 the key colour red, 0 the transparent word 0x0000. Three boxes: 4 x 4
// texels at (40, 40), 3 x 1 with a degenerate v at (100, 230), and 4 x 4 at the
// page's right edge (252..255 x 228..231, where the start banner of Crash Cove
// has its white field), whose texel 256 wraps to column 0. The POISONED image
// has green in the boxes, a ring of 0x0000 and red (alternating) around each
// box, wrapped at the page edge, and red on the rest of the page; the CLEAN
// image is green everywhere. A fragment that reads any texel outside its box
// differs between the two: red, or discarded (the clear colour shows).
//
// THE DRAWS. 64 quads in an 8 x 8 grid of 512 x 512 pixels, the boxes in turn,
// each magnified to about 40 x 40 pixels: rotated by an angle of its own,
// perspective (w 1.0 to 2.35 over the corners), every fourth one axis-aligned
// with its edges through pixel centres. The same vertices are drawn twice, on
// the poisoned and on the clean VRAM, into a 4-sample target and a 1-sample
// one; a pixel whose colour differs is a ring pixel. Instance way: texSource 2
// with the vertex layout of the engine buffers (Engine_RenderFillVertexDesc).
// Mosaic way: texSource 3, one table row per box (one cell, its corners the box).
//
// PASS: 0 ring pixels for the boxes, both ways, both sample counts, and the
// built-in controls fire - the instance way with the box left zeroed (stored
// as zeros it means the whole page, so the clamp holds nothing back: what a
// vertex writer without a box gets) gives ring pixels with 4 samples, and the
// mosaic way with cells one texel wider than the box gives them too (the
// readback sees the ring). Without them a 0 would prove nothing.

#define ESS_W 512
#define ESS_H 512
#define ESS_GRID 8
#define ESS_QUADS (ESS_GRID * ESS_GRID)
#define ESS_VERTICES (ESS_QUADS * 6)
#define ESS_CASES 3
#define ESS_PAGE_X 896
#define ESS_PAGE_Y 256
#define ESS_TPAGE 0x001eu // page x 14 * 64 = 896, y 1 * 256, 4 bit
#define ESS_CLUT_X 800
#define ESS_CLUT_Y 286
#define ESS_CLUT ((u16)((ESS_CLUT_X / 16) | (ESS_CLUT_Y << 6)))
#define ESS_TABLE_ROWS 8

typedef struct
{
	int umin;
	int vmin;
	int umax;
	int vmax;
} EssBox;

global_variable const EssBox s_essBox[ESS_CASES] = {{40, 40, 43, 43}, {100, 230, 102, 230}, {252, 228, 255, 231}};

enum
{
	ESS_INSTANCE_BOX = 0, // texSource 2, the triangle's box
	ESS_INSTANCE_PAGE,    // texSource 2, box left zeroed = the whole page (control)
	ESS_MOSAIC,           // texSource 3, cell = box
	ESS_MOSAIC_WIDE,      // texSource 3, cell one texel wider (control)
	ESS_SETS
};

// A texel of the 4-bit page (u, v wrap inside it).
internal void Ess_SetIndex(u16 *vram, int u, int v, int index)
{
	u16 *w;

	u &= 255;
	v &= 255;
	w = &vram[((ESS_PAGE_Y + v) * 1024) + ESS_PAGE_X + (u >> 2)];
	*w = (u16)((*w & ~(0xfu << ((u & 3) * 4))) | ((u32)index << ((u & 3) * 4)));
}

internal void Ess_FillVram(u16 *vram, int poisoned)
{
	int u;
	int v;
	int k;

	memset(vram, 0, (size_t)1024 * 512 * sizeof(u16));
	vram[(ESS_CLUT_Y * 1024) + ESS_CLUT_X + 0] = 0x0000;
	vram[(ESS_CLUT_Y * 1024) + ESS_CLUT_X + 1] = 0x03e0; // green
	vram[(ESS_CLUT_Y * 1024) + ESS_CLUT_X + 2] = 0x001f; // red, the key colour
	for (v = 0; v < 256; v++)
	{
		for (u = 0; u < 256; u++)
		{
			Ess_SetIndex(vram, u, v, poisoned ? 2 : 1);
		}
	}
	if (!poisoned)
	{
		return;
	}
	for (k = 0; k < ESS_CASES; k++)
	{
		const EssBox *b = &s_essBox[k];

		for (v = b->vmin - 1; v <= b->vmax + 1; v++)
		{
			for (u = b->umin - 1; u <= b->umax + 1; u++)
			{
				const int inside = (u >= b->umin) && (u <= b->umax) && (v >= b->vmin) && (v <= b->vmax);

				Ess_SetIndex(vram, u, v, inside ? 1 : (((u + v) & 1) ? 2 : 0));
			}
		}
	}
}

// One table row per box (rows 0..2) and per widened box (rows 3..5): one cell,
// its corners u0v0 (0, 0), u1v1 (1, 0), u2v2 (0, 1), u3v3 (1, 1).
internal void Ess_FillTable(u8 *table)
{
	int k;

	memset(table, 0, (size_t)64 * ESS_TABLE_ROWS * 4);
	for (k = 0; k < 2 * ESS_CASES; k++)
	{
		const EssBox *b = &s_essBox[k % ESS_CASES];
		const int grow = (k >= ESS_CASES) ? 1 : 0;
		const int u0 = (b->umin - grow < 0) ? 0 : (b->umin - grow);
		const int v0 = (b->vmin - grow < 0) ? 0 : (b->vmin - grow);
		const int u1 = (b->umax + grow > 255) ? 255 : (b->umax + grow);
		const int v1 = (b->vmax + grow > 255) ? 255 : (b->vmax + grow);
		u8 *row = &table[(size_t)k * 64 * 4];

		row[0] = 1; // one cell per side
		row[4 + 0] = (u8)u0;
		row[4 + 1] = (u8)v0;
		row[4 + 2] = (u8)(ESS_CLUT & 0xffu);
		row[4 + 3] = (u8)(ESS_CLUT >> 8);
		row[8 + 0] = (u8)u1;
		row[8 + 1] = (u8)v0;
		row[8 + 2] = (u8)(ESS_TPAGE & 0xffu);
		row[8 + 3] = (u8)(ESS_TPAGE >> 8);
		row[12 + 0] = (u8)u0;
		row[12 + 1] = (u8)v1;
		row[12 + 2] = (u8)u1;
		row[12 + 3] = (u8)v1;
	}
}

// The 64 quads of one set, 6 vertices each (triangles 0 1 2 and 2 1 3).
internal void Ess_BuildQuads(EngineVertex *out, int set)
{
	static const float wOf[4] = {1.0f, 1.45f, 1.9f, 2.35f};
	static const int tri[6] = {0, 1, 2, 2, 1, 3};
	const float cell = (float)ESS_W / (float)ESS_GRID;
	int i;

	for (i = 0; i < ESS_QUADS; i++)
	{
		const int box = i % ESS_CASES;
		const EssBox *b = &s_essBox[box];
		const int axis = ((i % 4) == 0);
		const float angle = axis ? 0.0f : ((float)i * 0.37f);
		const float ca = cosf(angle);
		const float sa = sinf(angle);
		// Axis-aligned: the centre on a pixel corner and a half size of x.5, so the
		// edges run through pixel centres; else a fraction off.
		const float cx = (((float)(i % ESS_GRID) + 0.5f) * cell) + (axis ? 0.0f : (0.37f * (float)(i % 3)));
		const float cy = (((float)(i / ESS_GRID) + 0.5f) * cell) + (axis ? 0.0f : (0.21f * (float)(i % 5)));
		const float half = axis ? 20.5f : 19.0f;
		EngineVertex corner[4];
		EngineVertex *q = &out[i * 6];
		int k;

		for (k = 0; k < 4; k++)
		{
			const float lx = (k & 1) ? 1.0f : -1.0f;
			const float ly = (k & 2) ? 1.0f : -1.0f;
			const float px = cx + (half * ((lx * ca) - (ly * sa)));
			const float py = cy + (half * ((lx * sa) + (ly * ca)));
			const float w = wOf[(i + k) % 4];
			const float s = (k & 1) ? 1.0f : 0.0f;
			const float t = (k & 2) ? 1.0f : 0.0f;
			EngineVertex *v = &corner[k];

			memset(v, 0, sizeof(*v));
			v->position[0] = (((px / (float)ESS_W) * 2.0f) - 1.0f) * w;
			v->position[1] = (((py / (float)ESS_H) * 2.0f) - 1.0f) * w;
			v->position[2] = w;
			if ((set == ESS_MOSAIC) || (set == ESS_MOSAIC_WIDE))
			{
				const int row = box + ((set == ESS_MOSAIC_WIDE) ? ESS_CASES : 0);

				v->texcoord[0] = s + 2.0f + (4.0f * (float)row);
				v->texcoord[1] = t + 2.0f;
			}
			else
			{
				v->texcoord[0] = (float)((k & 1) ? b->umax : b->umin);
				v->texcoord[1] = (float)((k & 2) ? b->vmax : b->vmin);
			}
			v->color[0] = v->color[1] = v->color[2] = 128; // x 2 = the texel as it is
			v->color[3] = 255;
		}
		for (k = 0; k < 6; k++)
		{
			q[k] = corner[tri[k]];
		}
		// The other sets leave the box as memset left it: zero, the whole page -
		// what a writer without a box gets, the behaviour from before the clamp.
		for (k = 0; (set == ESS_INSTANCE_BOX) && (k < 2); k++)
		{
			Engine_VertexBoxOfCorners(&q[k * 3], 3);
		}
	}
}

// One set on one VRAM image into the target, read back.
internal void Ess_Draw(u32 program, NativeGfxTarget target, NativeGfxBuffer vb, TextureID vram, TextureID table, int mosaic, u8 *pixels)
{
	EngineUniforms u;
	NativeGfxDrawState state;

	memset(&u, 0, sizeof(u));
	// clip = (x, y, 0, z): the positions carry x * w, y * w, w.
	u.clipFromModel[0] = 1.0f;
	u.clipFromModel[5] = 1.0f;
	u.clipFromModel[11] = 1.0f;
	u.tint[0] = u.tint[1] = u.tint[2] = u.tint[3] = 1.0f;
	u.params[0] = mosaic ? 3.0f : 2.0f;
	u.params2[0] = 2.0f;
	u.vramPage[0] = (float)ESS_PAGE_X;
	u.vramPage[1] = (float)ESS_PAGE_Y;
	u.vramClut[0] = (float)ESS_CLUT_X;
	u.vramClut[1] = (float)ESS_CLUT_Y;

	memset(&state, 0, sizeof(state));
	state.cull = NATIVE_GFX_CULL_NONE;

	NativeGfx_BindTarget(target);
	NativeGfx_SetViewport(0, 0, ESS_W, ESS_H);
	NativeGfx_SetScissorEnabled(0);
	NativeGfx_ClearColor(0.0f, 0.0f, 1.0f, 1.0f);
	NativeGfx_ClearColorBuffer();
	NativeGfx_BindProgram((ShaderID)program);
	NativeGfx_BindTexture(0, table, NATIVE_GFX_FILTER_NEAREST);
	NativeGfx_BindTexture(1, vram, NATIVE_GFX_FILTER_NEAREST);
	NativeGfx_BindTexture(3, table, NATIVE_GFX_FILTER_NEAREST);
	NativeGfx_SetSampleShading(0);
	NativeGfx_SetDrawState(&state);
	NativeGfx_SetBlendMode(BM_NONE);
	NativeGfx_UpdateUniforms((ShaderID)program, &u);
	NativeGfx_BindVertexBuffer(vb);
	NativeGfx_Draw(0, ESS_VERTICES);
	memset(pixels, 0, (size_t)ESS_W * ESS_H * 4);
	NativeGfx_ReadPixels(0, 0, ESS_W, ESS_H, NATIVE_GFX_TEXFMT_RGBA8, pixels, ESS_W);
	NativeGfx_SetDrawState(NULL);
}

// Ring pixels per box (poisoned against clean) and the pixels the clean draw covered.
internal void Ess_Compare(const u8 *poisoned, const u8 *clean, int ring[ESS_CASES], int *drawn)
{
	const int cell = ESS_W / ESS_GRID;
	int x;
	int y;

	for (x = 0; x < ESS_CASES; x++)
	{
		ring[x] = 0;
	}
	*drawn = 0;
	for (y = 0; y < ESS_H; y++)
	{
		for (x = 0; x < ESS_W; x++)
		{
			const size_t at = (((size_t)y * ESS_W) + (size_t)x) * 4;
			const int quad = ((y / cell) * ESS_GRID) + (x / cell);

			if ((clean[at + 1] > 0) || (clean[at + 0] > 0))
			{
				(*drawn)++;
			}
			if ((poisoned[at] != clean[at]) || (poisoned[at + 1] != clean[at + 1]) || (poisoned[at + 2] != clean[at + 2]))
			{
				ring[quad % ESS_CASES]++;
			}
		}
	}
}

int Engine_SampleSelfTest(void)
{
	static const char *setName[ESS_SETS] = {"instance boxes", "instance zeroed box = whole page (control)", "mosaic cells", "mosaic wide cells (control)"};
	u16 *vram = NULL;
	u8 *table = NULL;
	u8 *pixels[2] = {NULL, NULL};
	EngineVertex *quads = NULL;
	TextureID vramTex[2] = {NATIVE_GFX_INVALID, NATIVE_GFX_INVALID};
	TextureID tableTex = NATIVE_GFX_INVALID;
	NativeGfxBuffer vb[ESS_SETS];
	NativeGfxTarget target[2] = {NATIVE_GFX_INVALID, NATIVE_GFX_INVALID};
	int samples[2] = {0, 0};
	int ring[2][ESS_SETS][ESS_CASES];
	int drawn[2][ESS_SETS];
	int failures = 0;
	int up = 0;
	u32 program = 0xffffffffu;
	int s;
	int t;

	for (s = 0; s < ESS_SETS; s++)
	{
		vb[s] = NATIVE_GFX_INVALID;
	}
	memset(ring, 0, sizeof(ring));
	memset(drawn, 0, sizeof(drawn));

	// The device as the game brings it up (NativeRenderer_InitialiseDevice), hidden.
	g_cfg_engine = 1;
	g_cfg_nativePreview = 1;
	g_cfg_windowHidden = 1;
	g_windowWidth = 64;
	g_windowHeight = 64;
	if (!SDL_Init(SDL_INIT_VIDEO) || !NativeRenderer_InitialiseDevice("CTR engine sample selftest", 0))
	{
		printf("engine sample selftest SKIPPED: no Vulkan device (%s)\n", SDL_GetError());
		fflush(stdout);
		SDL_Quit();
		return 0;
	}
	up = 1;
	Engine_ProgramsInit();
	program = Engine_ProgramOf(ENGINE_PROGRAM_NE);

	vram = (u16 *)malloc((size_t)1024 * 512 * sizeof(u16));
	table = (u8 *)malloc((size_t)64 * ESS_TABLE_ROWS * 4);
	pixels[0] = (u8 *)malloc((size_t)ESS_W * ESS_H * 4);
	pixels[1] = (u8 *)malloc((size_t)ESS_W * ESS_H * 4);
	quads = (EngineVertex *)malloc((size_t)ESS_VERTICES * sizeof(EngineVertex));
	if ((program == 0xffffffffu) || (vram == NULL) || (table == NULL) || (pixels[0] == NULL) || (pixels[1] == NULL) || (quads == NULL))
	{
		printf("engine sample selftest: %s\n", (program == 0xffffffffu) ? "program 'ne' missing" : "out of memory");
		failures++;
		goto done;
	}

	for (t = 0; t < 2; t++)
	{
		NativeGfxTextureDesc desc;

		Ess_FillVram(vram, t == 0);
		memset(&desc, 0, sizeof(desc));
		desc.width = 1024;
		desc.height = 512;
		desc.format = NATIVE_GFX_TEXFMT_RG8;
		desc.filter = NATIVE_GFX_FILTER_NEAREST;
		desc.wrap = NATIVE_GFX_WRAP_REPEAT;
		desc.pixels = vram; // little endian: the low byte first, as the RG8 texel holds it
		vramTex[t] = NativeGfx_CreateTexture(&desc);
	}
	{
		NativeGfxTextureDesc desc;

		Ess_FillTable(table);
		memset(&desc, 0, sizeof(desc));
		desc.width = 64;
		desc.height = ESS_TABLE_ROWS;
		desc.format = NATIVE_GFX_TEXFMT_RGBA8;
		desc.filter = NATIVE_GFX_FILTER_NEAREST;
		desc.wrap = NATIVE_GFX_WRAP_CLAMP;
		desc.pixels = table;
		tableTex = NativeGfx_CreateTexture(&desc);
	}
	for (s = 0; s < ESS_SETS; s++)
	{
		NativeGfxVertexBufferDesc desc;

		Ess_BuildQuads(quads, s);
		Engine_RenderFillVertexDesc(&desc, (int)(ESS_VERTICES * sizeof(EngineVertex)), 0, quads);
		vb[s] = NativeGfx_CreateVertexBuffer(&desc);
	}
	for (t = 0; t < 2; t++)
	{
		const NativeGfxTargetDesc desc = {.width = ESS_W, .height = ESS_H};

		target[t] = NativeGfx_CreateTarget(&desc);
		if (target[t] != NATIVE_GFX_INVALID)
		{
			NativeGfx_SetTargetSamples(target[t], (t == 0) ? 4 : 1);
			samples[t] = NativeGfx_TargetSamples(target[t]);
		}
	}
	if ((vramTex[0] == NATIVE_GFX_INVALID) || (vramTex[1] == NATIVE_GFX_INVALID) || (tableTex == NATIVE_GFX_INVALID) || (target[0] == NATIVE_GFX_INVALID) ||
	    (target[1] == NATIVE_GFX_INVALID))
	{
		printf("engine sample selftest: textures or targets refused by the device\n");
		failures++;
		goto done;
	}
	for (s = 0; s < ESS_SETS; s++)
	{
		if (vb[s] == NATIVE_GFX_INVALID)
		{
			printf("engine sample selftest: vertex buffer %d refused by the device\n", s);
			failures++;
			goto done;
		}
	}

	for (t = 0; t < 2; t++)
	{
		for (s = 0; s < ESS_SETS; s++)
		{
			const int mosaic = (s == ESS_MOSAIC) || (s == ESS_MOSAIC_WIDE);

			Ess_Draw(program, target[t], vb[s], vramTex[0], tableTex, mosaic, pixels[0]);
			Ess_Draw(program, target[t], vb[s], vramTex[1], tableTex, mosaic, pixels[1]);
			Ess_Compare(pixels[0], pixels[1], ring[t][s], &drawn[t][s]);
			printf("engine sample selftest: %d sample(s), %s: ring pixels 4x4 %d, 3x1 %d, edge u=255 %d; %d pixels drawn\n", samples[t], setName[s],
			       ring[t][s][0], ring[t][s][1], ring[t][s][2], drawn[t][s]);
		}
	}
	for (t = 0; t < 2; t++)
	{
		for (s = 0; s < ESS_SETS; s++)
		{
			const int sum = ring[t][s][0] + ring[t][s][1] + ring[t][s][2];

			// Not blind on the drawing side: the quads cover about 64 x 40 x 40 pixels.
			if (drawn[t][s] < (ESS_QUADS * 30 * 30))
			{
				printf("engine sample selftest: %d sample(s), %s drew only %d pixels\n", samples[t], setName[s], drawn[t][s]);
				failures++;
			}
			if (((s == ESS_INSTANCE_BOX) || (s == ESS_MOSAIC)) && (sum != 0))
			{
				printf("engine sample selftest: %d sample(s), %s read %d pixel(s) outside the box\n", samples[t], setName[s], sum);
				failures++;
			}
		}
	}
	if (samples[0] != 4)
	{
		printf("engine sample selftest: the device gave %d samples, not 4\n", samples[0]);
		failures++;
	}
	if ((ring[0][ESS_INSTANCE_PAGE][0] + ring[0][ESS_INSTANCE_PAGE][1] + ring[0][ESS_INSTANCE_PAGE][2]) == 0)
	{
		printf("engine sample selftest: control did not fire - the whole page as box read no texel outside the box with 4 samples (the test is blind)\n");
		failures++;
	}
	if ((ring[0][ESS_MOSAIC_WIDE][0] + ring[0][ESS_MOSAIC_WIDE][1] + ring[0][ESS_MOSAIC_WIDE][2]) == 0)
	{
		printf("engine sample selftest: mosaic control did not fire - cells one texel wider showed no ring pixel\n");
		failures++;
	}

done:
	for (s = 0; s < ESS_SETS; s++)
	{
		if (vb[s] != NATIVE_GFX_INVALID)
		{
			NativeGfx_DestroyVertexBuffer(vb[s]);
		}
	}
	for (t = 0; t < 2; t++)
	{
		if (vramTex[t] != NATIVE_GFX_INVALID)
		{
			NativeGfx_DestroyTexture(vramTex[t]);
		}
		if (target[t] != NATIVE_GFX_INVALID)
		{
			NativeGfx_DestroyTarget(target[t]);
		}
	}
	if (tableTex != NATIVE_GFX_INVALID)
	{
		NativeGfx_DestroyTexture(tableTex);
	}
	free(vram);
	free(table);
	free(pixels[0]);
	free(pixels[1]);
	free(quads);
	if (up)
	{
		NativeGfxVK_Shutdown();
		SDL_DestroyWindow(g_window);
		g_window = NULL;
	}
	SDL_Quit();

	if (failures == 0)
	{
		printf("engine sample selftest passed: %d samples, boxes 0 ring pixels on both ways and both sample counts, control fired: %d ring pixels "
		       "(whole page as box), mosaic control %d\n",
		       samples[0], ring[0][ESS_INSTANCE_PAGE][0] + ring[0][ESS_INSTANCE_PAGE][1] + ring[0][ESS_INSTANCE_PAGE][2],
		       ring[0][ESS_MOSAIC_WIDE][0] + ring[0][ESS_MOSAIC_WIDE][1] + ring[0][ESS_MOSAIC_WIDE][2]);
	}
	else
	{
		printf("engine sample selftest FAILED: %d failure(s)\n", failures);
	}
	fflush(stdout);
	return (failures == 0) ? 0 : 1;
}
