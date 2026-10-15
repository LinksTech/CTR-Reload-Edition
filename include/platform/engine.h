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
extern int g_cfg_engineMenuScene; // --engine-menuscene (needs --engine)
// --engine-hide <sky,water> (needs --engine; round of 2026-10-15): the engine
// still owns the item (the PS1 path does not draw it) but draws nothing of it.
// The "minus" run of the measurements: the pixels the item covers show what is
// behind it. The collectors of the sky and the water read the bits.
extern int g_cfg_engineHide;
#define ENGINE_HIDE_SKY 0x1
#define ENGINE_HIDE_WATER 0x2

// THE PRINCIPLE (2026-10-13): CTR Reload is a PC version, not a 1:1 port. The
// engine shows the game state the way we want it; it does NOT rebuild the PS1
// drawing pipeline (ordering tables as a goal, GTE rounding, PS1 sprite maths,
// PS1 water and mirror tricks). Game logic (physics, AI, the 30 Hz tick, the
// random numbers, every state the game writes while drawing) is never touched.
// In phase 1 the ordering-table marker is only the TRANSITION channel while PS1
// and engine objects share a frame; it is no model for new maths.

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
	ENGINE_TEX_VRAM_TABLE, // VRAM through the track mosaic table (texture slot 3, platform/engine_track_mosaic.c)
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
	// Depth offset for coplanar layers (track decals, round of 2026-10-13): the clip z
	// of every vertex is multiplied by (1 + depthBias), so with reverse Z a
	// positive value pulls the face NEARER by that fraction of its depth. 0 =
	// none (every material made before this round). The renderer hands it to
	// "ne" as params2.z.
	float depthBias;
	// The program that draws the material (ENGINE_PROGRAM_*, round of 2026-10-15);
	// 0 = "ne", so every material made with memset stays "ne". programParam goes
	// to the program as params3.z/w (its meaning is the program's).
	u8 program;
	float programParam[2];
} EngineMaterial;

#define ENGINE_PROGRAM_NE 0 // "ne", the engine program (platform/native_shaders.inc ctr_native_engine_shader)
#define ENGINE_PROGRAM_NW 1 // "nw", the water program (platform/engine_water_shader.inc)
#define ENGINE_PROGRAM_COUNT 2

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
// AN EXTERNAL MESH: GPU buffers that belong to someone else (the custom driver
// sets of platform/native_char_gpu.c), registered WITHOUT a CPU copy. The mesh
// has no vertices/indices in its description (NULL, indexCount = the sum of the
// ranges), is never uploaded by the renderer and its buffers are never
// destroyed by the engine (Engine_MeshDestroy only forgets them).
typedef struct
{
	u32 vertexBuffer; // NativeGfxBuffer
	u32 indexBuffer;  // NativeGfxBuffer, u16 indices
	u32 verticesPerFrame;
	u32 frames;
	const EngineRange *ranges;
	u32 rangeCount;
	const char *name;
} EngineExternalMeshDesc;
int Engine_MeshCreateExternal(const EngineExternalMeshDesc *desc); // index or -1
int Engine_MeshIsExternal(int mesh);
// Selftest of the external meshes; *checks is SET to its check count (not added).
int Engine_SelfTestMeshExternal(int *checks);
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
	// The water line of a custom driver (step 3): a plane in MODEL units,
	// d = split[3] - dot(split.xyz, position); splitKeep 1 keeps d >= 0 (below
	// the plane), -1 keeps d < 0 (above), 0 keeps everything (every other draw).
	float split[4];
	s8 splitKeep;
	// Per-draw data of the material's program (EngineUniforms.params4; "nw": the
	// eye position in model units, w 0). 0 for every submitter that zeroes the draw.
	float user[4];
} EngineDraw;

// clipFromModel ends in PSX screen pixels RELATIVE TO THE VIEW (the offset
// rect.w/2, rect.h/2 included); the split's draw offset of the ordering table
// (NativeGpu_SplitDrawOffset) is added by the renderer at draw time, as the
// native layer adds it (NativeRenderLayer_ScreenFromView).

#define ENGINE_DRAW_TRANSPARENT 0x0001u // draw after the opaque ones, back to front
#define ENGINE_DRAW_NO_DEPTH 0x0002u    // no depth test (overlays)

#define ENGINE_VIEWS 4
#define ENGINE_DRAWS_PER_VIEW 512
#define ENGINE_DYN_VERTICES_PER_FRAME 131072
#define ENGINE_SCENE_BANKS 2

// The scene has one bank per double buffer of the game (gGT->swapchainIndex):
// a frame collects into its bank while the other bank's draws may still be
// drawn from the other buffer's ordering table. Every call below works on the
// SELECTED bank (bank 0 at start); Engine_SceneSortedBank reads any bank.
void Engine_SceneSelectBank(int bank); // 0 or 1; anything else is ignored
int Engine_SceneBank(void);

void Engine_SceneBeginFrame(void); // empties every list of the selected bank, resets that bank's dynamic ring
// Copies the draw (and its dynamic vertices into the frame's ring). Returns the
// draw's index in its view, or -1 when the list or the ring is full (counted).
int Engine_SceneSubmit(int view, const EngineDraw *draw);
// The view's draws in drawing order: opaque sorted by (mesh, material, frame),
// then transparent sorted back to front (larger sortDepth first), stable.
// Returns the count; *out points into the scene's own storage.
int Engine_SceneSorted(int view, const EngineDraw **out);
// The same for a given bank (the draw side resolves a marker with the bank it
// was written in). Returns 0 and *out = NULL for an invalid bank or view.
int Engine_SceneSortedBank(int bank, int view, const EngineDraw **out);
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
// (native_twin.c NativeTwin_DecodeVram): texel 0x0000 is transparent (alpha 0),
// every other texel opaque (alpha 255) with its 5:5:5 colour shifted left by 3;
// the STP bit is not in the output (the "ne" program handles it, stpPass).
// depth: 0 = 4 bit, 1 = 8 bit, 2 = 15 bit. Returns 0 on bad
// arguments (window outside the page or VRAM).
int Engine_VramDecode(const u16 *vram, int pageX, int pageY, int depth, int clutX, int clutY, int u0, int v0, int w, int h,
                      u8 *rgbaOut);
// One texel as RGBA8 (same rules).
void Engine_VramTexel(const u16 *vram, int pageX, int pageY, int depth, int clutX, int clutY, int u, int v, u8 rgbaOut[4]);

// ENGINE TEXTURES (platform/engine_vram.c, round of 2026-10-15): a full-resolution
// RGBA8 texture with its whole mip chain for an ENGINE_TEX_NATIVE material, made
// through the native texture manager (platform/native_tex.c: the edge rule, the
// levels as a 2x2 box in linear light with alpha weights from include/rldmip.inc,
// the waited upload) and then given a sampling of its own that does NOT follow
// --native-filter: FILTERED is trilinear with anisotropy, PIXELATED nearest with
// nearest levels. The renderer binds it with the texture's own sampler
// (engine_render.c, NATIVE_GFX_FILTER_KEEP) and "ne" reads it with texture(),
// so the device picks the level from the pixel's footprint.
// Edges: powers of two, 16..2048 each (NATIVE_TEX_EDGE_MIN/MAX), at most the
// device's maxImageDimension2D. Make it while a loading or menu screen is up,
// never in a race frame (the manager counts every upload in one). Needs
// --engine (which switches the texture manager on); without it nothing is made
// and nothing is logged. Anisotropy exists only when the device was made with
// it (--engine or --native-preview at start); the device clamps it to its limit
// and to 16.
#define ENGINE_TEXTURE_FILTERED 0  // linear, linear, levels linear, anisotropy (the default)
#define ENGINE_TEXTURE_PIXELATED 1 // nearest, nearest, levels nearest, no anisotropy

typedef struct
{
	int width; // of level 0
	int height;
	// Level 0, RGBA8 with straight alpha, rows from the top, tightly packed.
	// sRGB colour unless linearData. Read during the call only.
	const u8 *rgba;
	int linearData;    // 1: not colour (UNORM, levels averaged in the stored bytes)
	int oneLevel;      // 1: level 0 only, no levels (a comparison texture)
	int filter;        // ENGINE_TEXTURE_FILTERED or ENGINE_TEXTURE_PIXELATED
	int maxAnisotropy; // FILTERED only: 0 = up to the device limit, 1 = off, n = at most n
	int wrapU;         // NativeGfxWrap: 0 repeat, 1 clamp, 2 mirror
	int wrapV;
	const char *name;  // for the log; may be NULL
} EngineTextureDesc;

// The TextureID (for EngineMaterial.nativeTexture), 0xffffffff when refused
// (logged with the reason, except without --engine).
u32 Engine_TextureCreate(const EngineTextureDesc *desc);
void Engine_TextureDestroy(u32 texture);
// Points a material at the texture: texSource ENGINE_TEX_NATIVE, nativeTexture,
// and ENGINE_MATERIAL_SRGB set or cleared after the texture's format. Nothing
// else of the material changes (blend, tint, the other flags).
void Engine_TextureMaterial(EngineMaterial *m, u32 texture);

// --------------------------------------------------------------------------
// PROGRAMS (platform/engine_programs.c)
//
// The engine's own program "ne" (GLSL in engine_programs.c, compiled like the
// other programs through NativeRenderer_Shader_CompileWithUniforms). It reads
// struct EngineVertex. Per draw range one EngineUniforms block (std140, 192
// bytes, field for field the GLSL block "NeBlock", one text for every engine
// program: ENGINE_UNIFORM_BLOCK_GLSL in platform/native_shaders.inc):
//   clipFromModel  finished clip matrix (PSX ortho * screen-from-model)
//   tint           RGBA multiplier (material tint * draw tint)
//   params         x texSource (0 none, 1 native, 2 VRAM), y sRGB (1: re-encode
//                  a native sRGB texel to gamma), z alphaCutoff (discard below,
//                  0 = none), w stpPass (0 all texels, 1 only texels WITHOUT
//                  the STP bit, 2 only texels WITH it - the PS1 rule that only
//                  STP texels of a semi-transparent face blend)
//   params2        x modulate (1.0 native data; 2.0 retail data: texel x colour
//                  x 2, the PSX bright factor), y splitKeep, z depthBias of the
//                  material, w table flags (track mosaic, P1: 1 = LOD fallback
//                  to level 2)
//   vramPage       x page x (VRAM words), y page y, z depth 0/1/2, w 0
//   vramClut       x clut x, y clut y, z..w 0
//   split          the water-line plane in model units (EngineDraw.split)
//   params3        x program (EngineMaterial.program), y frame time in seconds
//                  (gGT->timer / 30 of the bank's frame, Engine_FrameTime), z, w
//                  EngineMaterial.programParam[0..1]; "ne": z = the lift of a
//                  track decal in world units (0 = none, engine_track.c DECALS)
//   params4        EngineDraw.user[0..3], per-draw program data; "ne" with a
//                  decal lift: the decal plane's normal (xyz) and normal . camera
// VRAM sampling: the PSX VRAM texture (NativeRenderer_GetVRAMTexture, RG8
// 1024x512) through the same CLUT lookup as the PSX program; texel 0x0000 is
// transparent (discarded); the texel's 5:5:5 colour expanded as the PSX
// program does. Texture coordinates of VRAM materials are in texels of the
// page (0..255), as a PS1 primitive carries them.

typedef struct
{
	float clipFromModel[16];
	float tint[4];
	float params[4];
	float params2[4]; // x modulate, y splitKeep (-1, 0, 1), z depthBias of the material, w 0
	float vramPage[4];
	float vramClut[4];
	float split[4];   // the water-line plane in model units (EngineDraw.split)
	float params3[4]; // x program, y frame time s, z/w material programParam
	float params4[4]; // EngineDraw.user
} EngineUniforms; // 192 bytes

// Creates the engine programs ("ne", "nw") once (only with g_cfg_engine). 1 = "ne" ready.
int Engine_ProgramsInit(void);
// The handle of "ne" (ShaderID as u32), 0xffffffff when not made.
u32 Engine_Program(void);
// The handle of an ENGINE_PROGRAM_* program, 0xffffffff when not made or unknown.
u32 Engine_ProgramOf(int program);

// --------------------------------------------------------------------------
// RENDER (platform/engine_render.c)
//
// Phase 1 output: a draw of the scene is linked into the PS1 ordering table as
// a DR_PSYX_NATIVE marker whose item carries ENGINE_MARKER_FLAG; the PS1 OT
// parser (native_gpu.c) hands such an item to Engine_DrawMarker at its place in
// the OT. Depth: one clear per view and frame, at the first engine draw of that
// view; every engine draw tests and writes depth unless its material or flags
// say otherwise. PS1 primitives never test depth.

#define ENGINE_MARKER_FLAG 0x400000u
// item = ENGINE_MARKER_FLAG | (bank << 20) | (view << 16) | drawIndex (0..0xFFFF)
#define ENGINE_MARKER_ITEM(bank, view, index) (ENGINE_MARKER_FLAG | ((u32)(bank) << 20) | ((u32)(view) << 16) | ((u32)(index) & 0xFFFFu))

// Programs ("ne"), the dynamic vertex buffer. Only with g_cfg_engine; called
// by Engine_RenderUploadMeshes the first time (a load phase, never inside a
// frame); a second call does nothing. 1 = ready.
int Engine_RenderInit(void);
// At a level load stage (the load phase, where one-shot uploads may wait):
// Engine_RenderInit if not yet done, then uploads every mesh that has no GPU
// buffers yet. Returns the meshes uploaded.
int Engine_RenderUploadMeshes(void);
// At level unload: GPU buffers of all meshes freed (one load late, as the
// native layer does), meshes and materials reset.
void Engine_RenderLevelUnload(void);
// Once per game frame, right after the double buffers swapped (where
// NativeRenderLayer_RegisterMarkerArenas runs): selects the bank of the buffer
// now filled, empties it, resets that bank's marker arena and the per-view
// depth-clear flags.
void Engine_RenderFrameBegin(int bank);
// Submits a draw into the current bank and links its marker into OT cell
// `cell` (0..0x3ff) of the ordering table `ot` (the u32 array of the view's
// push buffer OT). Returns 1 when submitted AND linked.
int Engine_RenderSubmitLinked(int view, const EngineDraw *draw, u32 *ot, int cell);
// From the OT parser: draws the marker's draw. clip/dispenv/onScreen/ofs as the
// native layer's NativeRenderLayer_DrawNativeItem gets them. Returns 1 if drawn.
// clip is the split's RECT16 and dispenv its DISPENV (void here so this header
// needs no PSX types).
int Engine_DrawMarker(u32 item, const void *clip, const void *dispenv, int onScreen, int ofsX, int ofsY);
// Counters for the end-of-run log line "[CTR Engine] at exit: ...".
void Engine_RenderReport(void);
// The same counters, read only (the track tour prints them per track as deltas).
typedef struct
{
	u32 drawn, markers, ranges, submitted;
	u32 refusedNotReady, refusedScene, refusedCell, refusedArena, refusedDyn;
	u32 refusedAtDraw; // item + program + clip + offscreen + mesh + material + range
	u32 uploadFailed;
} EngineRenderCounters;
void Engine_RenderCountersGet(EngineRenderCounters *out);

// --------------------------------------------------------------------------
// FRAME HOOKS (platform/engine_frame.c, called from game/platform code)
struct GameTracker;
// From MainFrame_RenderFrame, where NativeRenderLayer_Pull is called: cameras
// of the views (Engine_CameraFromPushBuffer) and the frame's collectors.
void Engine_FrameCollect(struct GameTracker *gGT);
// The camera of a view this frame (valid only between collect and draw).
const EngineCamera *Engine_FrameCamera(int view);
// The game time of the frame last collected into a scene bank, in seconds
// (gGT->timer / 30: deterministic, stands still in a pause). 0 for a bank never
// collected. The renderer hands it to every program as params3.y.
float Engine_FrameTime(int bank);

// --------------------------------------------------------------------------
// TEST SCENE (platform/engine_testscene.c; --engine-testscene with --engine)
// A textured cube (the probe body) in front of the camera of view 0, drawn
// every race frame through the whole engine path.
extern int g_cfg_engineTestScene;
void Engine_TestSceneLoad(void);
void Engine_TestSceneCollect(struct GameTracker *gGT);

// --------------------------------------------------------------------------
// CUSTOM DRIVERS (platform/engine_driver.c; step 3)
//
// Under --engine the custom drivers are drawn by the engine instead of the
// native layer: the layer still decides per (instance, view) what to draw and
// computes it (pose, wheel poses, water line, mirror - its route), but instead
// of its own marker it hands the finished item to the engine, which turns it
// into engine draws (body + wheels) linked into the same ordering-table cell.
// The layer then draws nothing of its own for custom drivers.

// At the load stage, right after the custom driver sets were uploaded
// (NativeCharGpu_LoadSeats): one external mesh per set body (and per wheel
// mesh), engine materials from the set's materials. At unload: forgotten.
void Engine_DriverLoad(void);
void Engine_DriverUnload(void);
// The engine mesh of a set's body, -1 when none (set = index of the
// native_char_gpu set, 0..NATIVE_CHAR_GPU_SETS-1).
int Engine_DriverBodyMesh(int set);
struct NrDrawItem;
// From the layer's char route: submits the item's draws into OT range `ot`,
// cell `cell` (what the layer would have linked its marker into). Returns the
// number of engine draws submitted; 0 = nothing (the layer then falls back as
// it always did).
int Engine_DriverSubmitItem(const struct NrDrawItem *it, u32 *ot, int cell);

// --------------------------------------------------------------------------
// EFFECTS OF CUSTOM DRIVERS (step 4; platform/engine_fx_common.c and one
// platform/engine_fx_<effect>.c per effect)
//
// The game's producers of a driver's effects (ground shadow, exhaust smoke,
// turbo flames, skid marks) ask Engine_FxOwnsDriver first. When it answers 1
// the engine draws that effect of that driver and the producer writes NO PS1
// primitive for it. DRAW TIME ONLY: the game's simulation, its random numbers
// and every state it keeps run exactly as before; only the primitives of the
// owned driver are not written. Without --engine every answer is 0 and every
// producer is exactly as before.

typedef enum
{
	ENGINE_FX_SHADOW = 0,
	ENGINE_FX_EXHAUST,
	ENGINE_FX_TURBO,
	ENGINE_FX_SKIDS,
	ENGINE_FX_BODY,  // the driver's body (a retail draw of an owned driver's body = the layer and the CMDL route declined)
	ENGINE_FX_TIRES, // the wheels as sprites (drivers without a wheel mesh: CMDL files, CNET without WHLS)
	ENGINE_FX_KINDS
} EngineFxKind;

// The native_char_gpu set of a seat (0..7), -1 when the seat has none.
int Engine_DriverSetOfSeat(int seat);
struct Driver;
// 1 when --engine is on and this driver is one of the race's drivers
// (gGT->drivers[driverID] == d: a Time Trial ghost is a Driver of its own whose
// driverID names a seat, and is never owned) and a custom driver whose body the
// engine draws in this level: its seat's set has an engine body mesh
// (Engine_DriverBodyMesh >= 0), or its CMDL model was imported
// (Engine_DriverCmdlOwns). Cheap: called per producer per driver per frame.
int Engine_FxOwnsDriver(const struct Driver *d);
// Counters for the exit report: an effect the engine drew for an owned driver,
// and an effect primitive the retail path still wrote for an owned driver
// (must stay 0 - the measure of "no bridge to the old path").
void Engine_FxNoteDrawn(EngineFxKind kind);
void Engine_FxNoteRetail(EngineFxKind kind);
void Engine_FxReport(void); // "[CTR Engine] effects: ..." at exit
// The counters of Engine_FxNoteDrawn / Engine_FxNoteRetail, read only.
void Engine_FxCountersGet(u32 drawn[ENGINE_FX_KINDS], u32 retail[ENGINE_FX_KINDS]);
// Ground effects (shadow, skid marks) lie on the track surface. Once the track
// is an engine draw that writes depth (--engine-track), a coplanar effect fails
// the depth test and disappears; their materials therefore pull them nearer by
// this fraction of their depth (EngineMaterial.depthBias): 2 units at 512.
#define ENGINE_GROUND_DEPTH_BIAS (1.0f / 256.0f)
// The game keeps a driver on its COLLISION height (game/COLL.c, quadBlockHeight
// from the hit point), which lies below the drawn track faces: on Dingo Canyon
// 2302 under faces at 2304. The skid points are written at that height
// (game/Vehicle/VehEmitter.c, VehEmitter_Skidmarks, local y 0 through the
// instance matrix); the retail shadow lifts itself by 3 (VehGroundShadow.c,
// VEH_GROUND_SHADOW_GROUND_Y_OFFSET). The PS1 order table never asks, a depth
// test does: the engine draws the skid marks this many world units higher, the
// height the retail shadow already has. Drawing only; the game state is untouched.
#define ENGINE_GROUND_LIFT 3.0f

// --------------------------------------------------------------------------
// RETAIL MODELS (platform/engine_model.c; step 3)
//
// A model in the PS1 format (the CMDL of a .rldchar, or any retail struct
// Model in memory) to an engine mesh with VRAM materials: every frame of every
// animation of every header as one mesh frame, the triangles of the PS1 command
// list, texture coordinates in page texels, one material per (tpage, clut,
// semi-transparency) with ENGINE_MATERIAL_SEMI_STP for the PS1 rule.
struct Model;
typedef struct
{
	int mesh;          // engine mesh, -1 on failure
	int firstMaterial; // the materials made for it (consecutive indices)
	int materialCount;
	int headerCount;   // model headers (LODs) imported: only header 0 (the most detailed)
	int animCount;
	int frameTotal;
} EngineModelImport;
// Returns 1 on success. Reads only; the model stays as it is.
int Engine_ModelImport(const struct Model *model, const char *name, EngineModelImport *out);
// The mesh frame of (animation, frame) of an imported model, -1 if out of range.
int Engine_ModelFrame(const EngineModelImport *imp, int anim, int frame);
const char *Engine_ModelImportWhy(void); // why the last Engine_ModelImport failed ("" after a success)
// THE LIVE FRAME of a still model (no animations, uncompressed frame) the game
// rewrites in place while racing - the start banner: RB_Banner_LInB
// (game/231/RB_Banner.c) rewrites the colour index of every command and the
// colour table at birth, after the load stage that imports the model, and
// RB_Banner_Animate_Play rewrites the height byte of every vertex and turns the
// colour table every tick. The mesh frame rebuilt from the model's CURRENT
// frame bytes, command list and colour table into out (the vertex order and
// texture coordinates of the mesh). Returns the vertex count (the mesh's
// vertices per frame), 0 when the model has no live frame, its list no longer
// has the imported records, or out is too small. Reads only.
u32 Engine_ModelLiveFrame(const EngineModelImport *imp, const struct Model *model, EngineVertex *out, u32 outMax);
int Engine_SelfTestModel(int *checks);

// --------------------------------------------------------------------------
// INSTANCES IN WORLD SPACE (platform/engine_instance.c; round of 2026-10-13)
//
// One instance of the game (struct Instance) whose model was imported with
// Engine_ModelImport, drawn by the engine in one view in WORLD space: the
// model-to-world matrix from the instance (its rotation/translation and the
// unit scale of model header 0), the view's engine camera (Engine_FrameCamera),
// no idpp->mvp, no GTE. The view's retail draw handler (idpp->unkEC) only says
// WHAT the view wants, the engine decides HOW (the principle above):
//   NORMAL, NORMAL_ALT   one draw.
//   SPLIT (water line)   the part above the instance's world water plane, and
//                        the part below it with belowTint (or none when
//                        drawBelow is 0) - one draw each, clipped by the
//                        plane (EngineDraw.split in model units).
//   REFLECTION, SPECIAL  the draw, plus its mirror image about the world
//                        plane (reflection matrix in world space) with
//                        mirrorTint.
// Cells (phase-1 transition channel): the middle bin of idpp->otRangeNormal,
// the second draw into idpp->otRangeSecondary when that is non-zero.
struct Instance;
struct InstDrawPerPlayer;

typedef struct
{
	int mesh;           // engine mesh (Engine_ModelImport)
	u32 frame;          // mesh frame (Engine_ModelFrame)
	float tint[4];      // the draw's tint (above water / not mirrored)
	float belowTint[4]; // SPLIT: the part below the plane
	float mirrorTint[4];// REFLECTION / SPECIAL: the mirror image
	int drawBelow;      // SPLIT: 0 = the part below the plane is not drawn
	u16 flags;          // ENGINE_DRAW_*
	// NULL = the mesh frame. Else the frame's vertices this frame (verticesPerFrame
	// of the mesh, Engine_ModelLiveFrame): every range of the mesh is drawn from
	// them as dynamic geometry, one draw per range.
	const EngineVertex *liveVertices;
} EngineInstanceDraw;

// The instance's model-to-world matrix (model units of the imported mesh to
// world units). 0 when the instance has no model or a zero scale.
int Engine_InstanceWorldMatrix(const struct Instance *inst, EngineMat4 *worldFromModel);
// Submits the instance's draws for this view (see above). Returns the number
// of draws submitted; 0 = nothing submitted (the caller lets retail draw).
int Engine_InstanceSubmit(struct Instance *inst, struct InstDrawPerPlayer *idpp, struct PushBuffer *pb, const EngineInstanceDraw *d);
// The view of a push buffer (0..3), -1 when it is none of gGT->pushBuffer[].
int Engine_InstanceViewOf(const struct PushBuffer *pb);

// --------------------------------------------------------------------------
// CUSTOM DRIVERS WITH ONLY A CMDL (platform/engine_driver_cmdl.c; round of 2026-10-13)
//
// A bound custom seat without a native set (the file has no CNET): its CMDL
// model (NativeChar_SeatModel) is imported at the load stage, its body drawn by
// the engine through Engine_InstanceSubmit. Its wheels are retail sprites in
// the game; the engine draws them through the tire effect (ENGINE_FX_TIRES).
void Engine_DriverCmdlLoad(void);          // load stage 5, after Engine_DriverLoad
int Engine_DriverCmdlOwns(const struct Driver *d); // 1 = the engine draws this driver's CMDL body
// From RenderBucket_DispatchDrawFunc, before the layer: 1 = the engine drew
// this driver body instance in this view (the retail handler is skipped).
int Engine_DriverCmdlRoute(struct Instance *inst, struct InstDrawPerPlayer *idpp, struct PushBuffer *pb);
// From RenderBucket_DispatchDrawFunc, right before the retail handler runs:
// counts ENGINE_FX_BODY as retail when inst is the body of an owned driver.
void Engine_DriverNoteRetailBody(const struct Instance *inst);

// --------------------------------------------------------------------------
// MENU SCENE (platform/engine_menuscene.c; --engine-menuscene, round of 2026-10-13)
//
// Phase-1 finish line Z5: one custom driver drawn OUTSIDE a race, in the main
// menu level, through the same API (an engine mesh of a driver set, EngineDraw
// with its own matrix), turning.
void Engine_MenuSceneLoad(void);                 // load stage 5, before Engine_DriverLoad (main menu level only)
void Engine_MenuSceneCollect(struct GameTracker *gGT); // from Engine_FrameCollect

// --------------------------------------------------------------------------
// TRACK PROTOTYPE (platform/engine_track.c; --engine-track, round of 2026-10-13)
//
// The level's quadblocks as engine meshes with VRAM materials (built at load
// stage 8), drawn every race frame with the view's camera; the PS1 path skips
// the level buckets the engine draws (Engine_TrackOwnsBucket). Level instances
// through the model importer (Engine_TrackInstanceRoute). Not in this round:
// visibility, detail levels, water, sky.
void Engine_TrackLoad(void);                      // load stage 8, before Engine_RenderUploadMeshes
void Engine_TrackCollect(struct GameTracker *gGT); // from Engine_FrameCollect
// 1 = the PS1 level draw skips this bucket of DrawLevelOvr1P this frame.
int Engine_TrackOwnsBucket(int bucketIndex);
// From RenderBucket_DispatchDrawFunc: 1 = the engine drew this level instance.
int Engine_TrackInstanceRoute(struct Instance *inst, struct InstDrawPerPlayer *idpp, struct PushBuffer *pb);
// 1 = every track mesh was submitted this frame (the water draws only then).
int Engine_TrackDrawnThisFrame(void);

// --------------------------------------------------------------------------
// TRACK MOSAIC (platform/engine_track_mosaic.c; round of 2026-10-15)
//
// A mosaic mid face (level 3 of its IconGroup4 is a mosaic table reference)
// keeps its two triangles; the "ne" program picks the tile per fragment from a
// data texture (one row per table, mirror and form; texture slot 3) and samples
// the VRAM with the tile's own tpage and clut. No PS1 subdivision. Every
// function is a no-op returning 0 without g_cfg_engine && g_cfg_engineTrack.
#define ENGINE_MOSAIC_FORM_4X4 0 // leaf flag 4X4, DYNAMIC_SUBDIV or none (table 16 + 16)
#define ENGINE_MOSAIC_FORM_4X2 1 // leaf flag 4X2 (8 + 8)
#define ENGINE_MOSAIC_FORM_4X1 2 // leaf flag 4X1 (4 + 4)
struct TextureLayout;
void Engine_TrackMosaicBegin(int levelId); // Engine_TrackLoad start: empties the row set
// One built mid face. group = its resolved IconGroup4 (level 3 holds the mosaic word),
// mirrored = bit 23 of the face's scratch-table word, form = ENGINE_MOSAIC_FORM_* of its leaf,
// slotOf[v] = slot (0..3) whose UV vertex v took before the mirror swap (triangle mode: the
// earlier slot on the same corner). 1 = the face is a mosaic face: v[0..vertCount-1].texcoord
// rewritten and *keyTpage = 0x8000 | (abr of the row's tiles << 5): the caller keys the face with
// tpage *keyTpage and clut 0. 0 = unchanged (level 2 stays; reason counted).
int Engine_TrackMosaicFace(const struct TextureLayout *group, int mirrored, int form, const u8 *slotOf, EngineVertex *v, int vertCount, u16 *keyTpage);
// One built mid face of an animated texture (its texture pointer has bit 0 set; cell = that
// pointer - 1, the word holding the IconGroup4 shown now). 1 = the face became a whole-face
// face of the table (texcoords rewritten, keyed with *keyTpage and clut 0 like a mosaic face);
// 0 = unchanged (frozen at load, reason counted).
int Engine_TrackMosaicAnimFace(u32 cell, int mirrored, const u8 *slotOf, EngineVertex *v, int vertCount, u16 *keyTpage);
// From Engine_FrameCollect after Engine_TrackCollect: rewrites the table rows whose AnimTex cell names another group now.
void Engine_TrackMosaicFrame(void);
int Engine_TrackMosaicEnd(void);     // builds the data texture, logs the counters; 1 = ready
void Engine_TrackMosaicUnload(void); // level unload (deferred destroy)
u32 Engine_TrackMosaicTexture(void); // TextureID for slot 3, 0xffffffff when none
int Engine_SelfTestTrackMosaic(int *checks);

// --------------------------------------------------------------------------
// SKY (platform/engine_sky.c; --engine-track, round of 2026-10-15)
//
// The level's sky dome (struct Skybox, all 8 segments) as one static engine
// mesh with vertex colours, drawn every race frame in view 0 with the camera's
// rotation only (sky at infinity), no depth test, no depth write, linked into
// OT cell 0x3ff; the level's stars as engine billboards in the same cell; the
// back colour and glow as an engine backdrop. The PS1 DrawSky_Full and
// RenderStars skip their primitives when the engine drew them.
void Engine_SkyLoad(void);                       // load stage 8, next to Engine_TrackLoad, before Engine_RenderUploadMeshes
void Engine_SkyCollect(struct GameTracker *gGT); // from Engine_FrameCollect
int Engine_SkyOwnsView(const struct PushBuffer *pb);  // DrawSky_Full: 1 = the engine drew this view's dome this frame
int Engine_SkyOwnsStars(const struct PushBuffer *pb); // RenderStars: 1 = the engine draws this view's stars this frame
int Engine_SelfTestSky(int *checks);

// --------------------------------------------------------------------------
// WATER (platform/engine_water.c; --engine-track, round of 2026-10-15)
//
// The water blocks of the level as one engine mesh drawn by the program "nw";
// the PS1 water buckets 8, 9 are skipped while it is drawn. AnimateWater (game
// state, COLL reads color_hi) runs unchanged.
void Engine_WaterLoad(void);                       // load stage 8, after Engine_TrackLoad, before Engine_RenderUploadMeshes
void Engine_WaterCollect(struct GameTracker *gGT); // from Engine_FrameCollect, after Engine_TrackCollect
int Engine_WaterOwnsBucket(int bucketIndex);       // 1 = DrawLevelOvr1P skips this bucket (8 or 9) this frame
typedef struct
{
	int blocks;         // water blocks of the level
	int onScreen;       // blocks with a visible part in the view
	float share;        // estimated screen share 0..1 (clipped quads, overlaps counted twice, capped)
	float box[4];       // x0, y0, x1, y1 as fractions of the view rect
	float nearestDepth; // view depth of the nearest visible water corner
} EngineWaterProbe;
// Pure read of gGT->level1 and gGT->pushBuffer[view] (dev tour only). 1 = valid.
int Engine_WaterProbe(const struct GameTracker *gGT, int view, EngineWaterProbe *out);
void Engine_WaterProbeLog(const struct GameTracker *gGT, int view, int frame, const char *tag); // the probe log line; frame = the tour's race frame
int Engine_SelfTestWater(int *checks);

// --------------------------------------------------------------------------
// TRACK TOUR (--level-tour, game/DebugMenu.c; platform/engine_tour.c, round of 2026-10-15)
//
// The tour calls Engine_TourNotify at the first race frame of an entry (phase 0) and after
// its last (phase 1). A package that logs per-track lines registers one function at its
// first load under --engine; without --engine nothing is registered and nothing is logged.
#define ENGINE_TOUR_REPORTS_MAX 16
typedef void (*EngineTourReportFn)(int entry, int levelID, int phase);
int Engine_TourRegister(EngineTourReportFn fn); // 1 = registered (duplicates ignored), 0 = table full
void Engine_TourNotify(int entry, int levelID, int phase);
int Engine_SelfTestTour(int *checks); // engine_tour.c: registry, burst schedule, progress fold, fingerprint

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
// (Engine_SelfTestModel above.)
int Engine_SelfTestInstance(int *checks); // engine_instance.c: world matrix, water plane, mirror
int Engine_SelfTestExhaust(int *checks);  // engine_fx_exhaust.c: the sprite from particle state
int Engine_SelfTestTrack(int *checks);    // engine_track.c: quadblock to triangles
// (Engine_SelfTestTrackMosaic, Engine_SelfTestSky, Engine_SelfTestWater and Engine_SelfTestTour are above.)
// All of them; prints "engine selftest passed: N checks, 0 failures" or
// "engine selftest FAILED: N checks, F failures"; returns 0 / 1.
int Engine_SelfTest(void);

#endif
