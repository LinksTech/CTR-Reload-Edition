#ifndef PLATFORM_ENGINE_H
#define PLATFORM_ENGINE_H

// THE NATIVE ENGINE (phase 1: foundation and drivers). See PLAN-ENGINE.md of
// the project notes for the architecture; this header is the one contract all
// engine files (platform/engine_*.c) share.
//
// The engine draws what used to be drawn through the PS1 ordering table:
// in phase 1 the custom drivers completely, and a track as a prototype. It
// only READS game state. Everything runs behind --engine (with --dev); without
// it no engine function allocates, logs or draws, and the PS1 path is exactly
// as before.
//
// Coordinates: world units of the game (s16 positions, 4096 = 1.0 in rotation
// matrices). Screen: PSX screen pixels of a view (as SetGeomOffset/SetGeomScreen
// give them), before the internal resolution factor.

#include <macros.h>

// --------------------------------------------------------------------------
// Switches (main.c). 0 = off, the default.
extern int g_cfg_engine;      // --engine
extern int g_cfg_engineTrack; // --engine-track (needs --engine)

// --------------------------------------------------------------------------
// MATH (platform/engine_math.c)

typedef struct
{
	float x, y, z;
} EngineVec3;

// Column-major 4x4: m[col * 4 + row]. Points are column vectors: p' = M * p.
typedef struct
{
	float m[16];
} EngineMat4;

void Engine_Mat4Identity(EngineMat4 *out);
void Engine_Mat4Mul(const EngineMat4 *a, const EngineMat4 *b, EngineMat4 *out); // out = a * b
EngineVec3 Engine_Mat4MulPoint(const EngineMat4 *m, EngineVec3 p);             // w = 1, no divide
// A GTE rotation (s16 4.12, rows as the GTE stores them: m[row][col]) and a
// translation (s32) as one affine matrix: p' = (R * p) / 4096 + t.
void Engine_Mat4FromGte(const s16 r[3][3], const s32 t[3], EngineMat4 *out);
// Euler angles of the game (s16, 4096 = full turn) as the game builds them
// (ConvertRotToMatrix order); used for driver and effect orientation.
void Engine_Mat4FromGameRot(s16 rx, s16 ry, s16 rz, EngineMat4 *out);

// --------------------------------------------------------------------------
// CAMERA (platform/engine_camera.c)
//
// One camera per view, taken from the game's push buffer of that view. The
// projection is the PS1 one: view = R * world / 4096 + t (matrix_ViewProj),
// screen = offset + H * view.xy / view.z, so an engine draw lines up with
// what the PS1 path draws in the same view.

typedef struct
{
	int valid;
	int view;             // 0..3
	EngineMat4 viewFromWorld;
	float H;              // distance to the screen plane (distanceToScreen_PREV)
	float offsetX;        // screen centre in PSX pixels (SetGeomOffset of the view)
	float offsetY;
	int rectX, rectY, rectW, rectH; // the view's rectangle (pb->rect)
	float zNear;          // H / 8, the reverse-Z near plane (native_gfx.h DEPTH)
} EngineCamera;

struct PushBuffer;

// 0 if pb is NULL or not usable (rect empty, H <= 0).
int Engine_CameraFromPushBuffer(const struct PushBuffer *pb, int view, EngineCamera *out);
// A world point to the view's screen: out.x/out.y PSX pixels, out.z view depth.
// 0 when the point is behind the near plane.
int Engine_ProjectPoint(const EngineCamera *cam, EngineVec3 world, EngineVec3 *out);
// Clip matrix for a draw in this view: clipFromWorld for points in world units,
// reverse Z with the far plane at infinity (native_gfx.h DEPTH). The caller
// multiplies the PSX ortho projection in front in phase 1 (as the native layer
// does), so this matrix ends in PSX screen pixels with w = view z.
void Engine_CameraScreenFromWorld(const EngineCamera *cam, EngineMat4 *out);

// --------------------------------------------------------------------------
// MATERIALS AND LIGHT (platform/engine_material.c)

typedef enum
{
	ENGINE_BLEND_OPAQUE = 0,
	ENGINE_BLEND_MASK,          // opaque, texel alpha below cutoff discarded
	ENGINE_BLEND_PSX_AVERAGE,   // PS1 mode 0: B/2 + F/2
	ENGINE_BLEND_PSX_ADD,       // PS1 mode 1: B + F
	ENGINE_BLEND_PSX_SUBTRACT,  // PS1 mode 2: B - F
	ENGINE_BLEND_PSX_ADD_QUARTER, // PS1 mode 3: B + F/4
	ENGINE_BLEND_MULTIPLY,      // B * F (NIGHT)
	ENGINE_BLEND_COUNT
} EngineBlend;

typedef enum
{
	ENGINE_TEX_NONE = 0,
	ENGINE_TEX_NATIVE, // a texture of the native texture manager (RGBA8)
	ENGINE_TEX_VRAM,   // a texture page of the PS1 VRAM with its CLUT
} EngineTexSource;

#define ENGINE_MATERIAL_DOUBLE_SIDED 0x0001u
#define ENGINE_MATERIAL_NO_DEPTH_WRITE 0x0002u
#define ENGINE_MATERIAL_SRGB 0x0004u  // native texture holds sRGB colours
#define ENGINE_MATERIAL_SEMI_STP 0x0008u // VRAM: only texels with the STP bit blend (PS1 rule)

typedef struct
{
	u8 blend;      // EngineBlend
	u8 texSource;  // EngineTexSource
	u16 flags;     // ENGINE_MATERIAL_*
	u32 nativeTexture; // TextureID for ENGINE_TEX_NATIVE
	// ENGINE_TEX_VRAM: the PS1 tpage and clut words as a primitive carries them
	u16 tpage;
	u16 clut;
	float tint[4];     // RGBA multiplier, 1.0 = unchanged
	float alphaCutoff; // ENGINE_BLEND_MASK
} EngineMaterial;

// Decoded tpage word: x base (multiple of 64), y base (0 or 256), colour depth
// 0 = 4 bit, 1 = 8 bit, 2 = 15 bit, semi-transparency mode 0..3.
typedef struct
{
	int baseX, baseY, depth, abr;
} EngineTpage;
EngineTpage Engine_DecodeTpage(u16 tpage);
// The CLUT position of a clut word (x multiple of 16).
void Engine_DecodeClut(u16 clut, int *x, int *y);
// PS1 semi-transparency mode (abr 0..3) to the engine blend.
EngineBlend Engine_BlendFromAbr(int abr);
// The blend equation on one 8-bit channel, as the PS1 GPU computes it
// (saturating, B = background, F = foreground). For the selftest and for the
// CPU reference of the shaders.
u8 Engine_BlendChannel(EngineBlend blend, u8 background, u8 foreground);

// Material table: register once (at load), use the index in draws.
#define ENGINE_MATERIALS_MAX 1024
int Engine_MaterialRegister(const EngineMaterial *m); // index, or -1 when full
const EngineMaterial *Engine_Material(int index);
void Engine_MaterialsReset(void);                     // at level unload

// Light: the engine's own model is the retail look (vertex colour * tint *
// texture, no lights). NIGHT multiplies the frame (0 = off ... 90 percent),
// the same factor the PS1 path uses (platform/native_mods.c).
typedef struct
{
	float ambient[3]; // 1.0 = retail look
	int nightPercent; // 0..90
} EngineLight;
void Engine_SetLight(const EngineLight *light);
const EngineLight *Engine_GetLight(void);
// The colour a NIGHT of nightPercent leaves of a channel, as the PS1 path
// multiplies it (CPU reference for the shader).
u8 Engine_NightChannel(int nightPercent, u8 value);

// --------------------------------------------------------------------------
// MESHES AND SCENE (platform/engine_mesh.c, platform/engine_scene.c)

// The vertex of every engine mesh; the same layout as struct NativeProbeVertex
// (24 bytes), so the existing "nr" programs can read it.
typedef struct
{
	float position[3];
	float texcoord[2];
	u8 color[4];
} EngineVertex;

typedef struct
{
	u32 firstIndex;
	u32 indexCount;
	u16 material; // Engine_MaterialRegister index
} EngineRange;

typedef struct
{
	const EngineVertex *vertices; // frames * verticesPerFrame
	u32 verticesPerFrame;
	u32 frames;                   // >= 1 (poses)
	const u16 *indices;
	u32 indexCount;
	const EngineRange *ranges;
	u32 rangeCount;
	const char *name;
} EngineMeshDesc;

#define ENGINE_MESHES_MAX 256
#define ENGINE_MESH_INVALID (-1)
// Copies the description's arrays (CPU side); the GPU buffers are made by the
// renderer (engine_render.c) at load time. Returns the mesh index or -1.
int Engine_MeshCreate(const EngineMeshDesc *desc);
void Engine_MeshDestroy(int mesh);
void Engine_MeshesReset(void);
// The CPU copy (for the renderer and the tests); NULL for an invalid index.
const EngineMeshDesc *Engine_MeshDesc(int mesh);
// Bytes held by all live meshes (CPU copies), for the budget log.
u32 Engine_MeshBytes(void);
// GPU handles, set by the renderer once uploaded (u32 so this header needs no
// device types); 0xffffffff = not uploaded.
void Engine_MeshSetGpu(int mesh, u32 vertexBuffer, u32 indexBuffer);
void Engine_MeshGetGpu(int mesh, u32 *vertexBuffer, u32 *indexBuffer);

// One thing to draw in one view this frame.
typedef struct
{
	int mesh;          // or -1 with dynamic geometry
	u32 frame;         // pose / frame of the mesh
	EngineMat4 clipFromModel; // finished matrix (screen pixels, w = view z)
	float tint[4];     // multiplies the material tint
	int materialOverride; // -1 = the mesh's materials
	float sortDepth;   // view z of the object's centre, for transparent order
	u32 otCell;        // phase 1: index of the OT cell the draw is linked into (0..0x3ff)
	u16 flags;         // ENGINE_DRAW_*
	// dynamic geometry (effects): vertices built this frame, drawn as one range
	const EngineVertex *dynVertices;
	u32 dynVertexCount;
	const u16 *dynIndices;
	u32 dynIndexCount;
	int dynMaterial;
} EngineDraw;

#define ENGINE_DRAW_TRANSPARENT 0x0001u // draw after the opaque ones, back to front
#define ENGINE_DRAW_NO_DEPTH 0x0002u    // no depth test (overlays)

#define ENGINE_VIEWS 4
#define ENGINE_DRAWS_PER_VIEW 512
#define ENGINE_DYN_VERTICES_PER_FRAME 65536

void Engine_SceneBeginFrame(void); // empties every list, resets the dynamic ring
// Copies the draw (and its dynamic vertices into the frame's ring). Returns the
// draw's index in its view, or -1 when the list or the ring is full (counted).
int Engine_SceneSubmit(int view, const EngineDraw *draw);
// The view's draws in drawing order: opaque sorted by (mesh, material, frame),
// then transparent sorted back to front (larger sortDepth first), stable.
// Returns the count; *out points into the scene's own storage.
int Engine_SceneSorted(int view, const EngineDraw **out);
u32 Engine_SceneOverflows(void); // draws refused since the start (list or ring full)

// --------------------------------------------------------------------------
// EFFECT GEOMETRY (platform/engine_fx.c)
//
// Every builder writes 4 vertices and 6 indices (two triangles, counter-
// clockwise seen from the camera / from above) or returns 0 when out is too
// small. Colours are RGBA bytes.

// A quad facing the camera: centre in world units, half sizes in world units,
// rotation around the view axis in game angle units (4096 = full turn).
int Engine_FxBillboard(const EngineCamera *cam, EngineVec3 centre, float halfW, float halfH, s16 roll, const u8 colour[4],
                       EngineVertex *outV, u16 *outI, u16 baseIndex);
// A quad lying on a ground plane: centre, the plane's normal (unit), the quad's
// forward direction (projected onto the plane), half length and half width.
int Engine_FxGroundQuad(EngineVec3 centre, EngineVec3 normal, EngineVec3 forward, float halfLength, float halfWidth,
                        const u8 colour[4], EngineVertex *outV, u16 *outI, u16 baseIndex);
// A strip through points (left/right edge given per point): 2 vertices per
// point, 6 indices per segment. Returns the vertex count written, 0 if too few
// points or out too small. uScale: texture u per world unit along the strip.
int Engine_FxRibbon(const EngineVec3 *left, const EngineVec3 *right, const u8 (*colour)[4], int points, float uScale,
                    EngineVertex *outV, int maxVertices, u16 *outI, int maxIndices, u16 baseIndex);

// --------------------------------------------------------------------------
// VRAM (platform/engine_vram.c)

#define ENGINE_VRAM_WIDTH 1024
#define ENGINE_VRAM_HEIGHT 512

// Decodes a w x h window (u0,v0 in texels of the page) of a texture page into
// RGBA8, rows from the top, with the PS1 rules the retail twin decoder uses
// (native_twin.c NativeTwin_DecodeVram): texel 0x0000 transparent, the STP bit
// carried in alpha. depth: 0 = 4 bit, 1 = 8 bit, 2 = 15 bit. Returns 0 on bad
// arguments (window outside the page or VRAM).
int Engine_VramDecode(const u16 *vram, int pageX, int pageY, int depth, int clutX, int clutY, int u0, int v0, int w, int h,
                      u8 *rgbaOut);
// One texel as RGBA8 (same rules).
void Engine_VramTexel(const u16 *vram, int pageX, int pageY, int depth, int clutX, int clutY, int u, int v, u8 rgbaOut[4]);

// --------------------------------------------------------------------------
// SELFTEST (platform/engine_selftest.c; main.c --engine-selftest, ctest
// engine_selftest). Each module's test returns its failure count and adds its
// checks to *checks; it prints one line per failing check.
int Engine_SelfTestMath(int *checks);
int Engine_SelfTestCamera(int *checks);
int Engine_SelfTestMaterial(int *checks);
int Engine_SelfTestMeshScene(int *checks);
int Engine_SelfTestFx(int *checks);
int Engine_SelfTestVram(int *checks);
// All of them; prints "engine selftest passed: N checks, 0 failures" or
// "engine selftest FAILED: N checks, F failures"; returns 0 / 1.
int Engine_SelfTest(void);

#endif
