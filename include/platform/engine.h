#ifndef PLATFORM_ENGINE_H
#define PLATFORM_ENGINE_H

// THE NATIVE ENGINE (phase 1: foundation and drivers). See PLAN-ENGINE.md of
// the project notes for the architecture; this header is the one contract all
// engine files (platform/engine_*.c) share.
//
// The engine draws what used to be drawn through the PS1 ordering table:
// in phase 1 the custom drivers completely, and a track as a prototype. It
// only READS game state. Everything runs behind g_cfg_engine: on by default
// for one-player levels (section DEFAULT), or by --engine; with --no-engine no
// engine function allocates, logs or draws, and the PS1 path is exactly as
// before.
//
// Coordinates: world units of the game (s16 positions, 4096 = 1.0 in rotation
// matrices). Screen: PSX screen pixels of a view (as SetGeomOffset/SetGeomScreen
// give them), before the internal resolution factor.

#include <macros.h>
#include <rldinstances.h>

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
// "track" (round of 2026-10-16, static model): the level's own geometry - its
// faces, its water and its level instances (the ones with an InstDef) - is
// owned and not drawn (platform/engine_track.c); drivers, weapons and effects
// stay. Needs --engine-track as well (the default engine has it).
#define ENGINE_HIDE_TRACK 0x4

// --------------------------------------------------------------------------
// FALLBACK (platform/engine.c; day round of 2026-10-15, per level since the
// night round of 2026-10-16)
//
// When the engine cannot start (its programs "ne"/"nw" or its dynamic vertex
// buffer are not made, Engine_RenderInit; stage "start"), the run goes on over
// the PS1 path: one log line with the stage and the reason, then every engine
// switch above goes to 0 for the rest of the run. When it cannot draw a level
// it loads (Engine_TrackLoad without memory or without the mosaic data
// texture, stage "track load"; a mesh that does not upload, stage "level
// load"), the switches go to 0 for THAT level only: one log line, the PS1 path
// draws the level, and Engine_LevelEnd gives the switches back at the next
// level change, so the next level tries the engine again. The log line is the
// only notice (no menu, no screen text). Every hook asks g_cfg_engine first,
// so the game draws as in a run without the engine. Called only at load points
// (never inside a frame). Game state is not touched: the engine only ever read
// it.
#define ENGINE_FALLBACK_RUN 0
#define ENGINE_FALLBACK_LEVEL 1
void Engine_Fallback(const char *stage, const char *reason);
int Engine_FallbackScope(const char *stage); // ENGINE_FALLBACK_LEVEL for "track load" / "level load", else ENGINE_FALLBACK_RUN
int Engine_FellBack(void); // 1 = Engine_Fallback switched the engine off (for a level or the run) at least once in this run
// The level change (game/LOAD/LOAD_TenStages.c, stage 0 after the first boot):
// Engine_LevelEnd before the engine's unload gives back what one level had off;
// Engine_LevelBegin after it, with the players of the next level, applies the
// players rule of the DEFAULT below.
void Engine_LevelEnd(void);
void Engine_LevelBegin(int players);
// --engine-fail <start|track> (needs --engine; track needs --engine-track):
// measuring only, the fallback forced at the place of the real failure -
// start: the programs are not made (Engine_ProgramsInit), so Engine_RenderInit
// falls back; track: Engine_TrackLoad fails at the first level it would draw.
extern int g_cfg_engineFail;
#define ENGINE_FAIL_START 1
#define ENGINE_FAIL_TRACK 2
int Engine_FailModeFromName(const char *name); // "start" / "track" -> ENGINE_FAIL_*, -1 for any other word

// DEFAULT (night round of 2026-10-16): with neither --engine nor --no-engine on
// the command line, main.c (NativeArgs_ReadDisplayFlags, before the window)
// switches the engine and its track on as --engine --engine-track would, and
// sets g_cfg_engineDefault. The default is for one player: a level of more
// players (split screen, battle) goes to the PS1 path (Engine_LevelBegin). An
// --engine named on the command line is not the default and keeps its old
// meaning. --engine and --engine-track are player switches (no --dev); the
// other engine switches stay developer switches and still want --engine.
extern int g_cfg_engineDefault;
int Engine_DefaultOn(int engineOnLine, int noEngine); // 1 = the default switches the engine on
// --no-engine (a player switch; the emergency exit and the switch of the
// PS1-path gates): the engine stays off for the run, whatever ctr-settings.cfg
// says. Not with --engine.
extern int g_cfg_engineOff;
// THE SETTING "video engine 0|1" (ctr-settings.cfg, read before the window by
// Platform_SettingsPreloadDisplay; prepared, no menu entry, written only while
// 1). Engine_SettingDecision is the pure rule, Engine_SettingApply acts on it.
// It stays a developer's key: 0 is no way out (that is --no-engine alone).
#define ENGINE_SETTING_OFF 0       // the value 0: nothing
#define ENGINE_SETTING_ON 1        // the engine and its track on for the run (--engine --engine-track)
#define ENGINE_SETTING_NEEDS_DEV 2 // 1 without --dev: stays off in this build
#define ENGINE_SETTING_NO_ENGINE 3 // 1 with --no-engine: off
#define ENGINE_SETTING_LINE 4      // 1 with --engine on the command line: the command line decides
#define ENGINE_SETTING_DEFAULT 5   // 1 with the engine on by default: changes nothing
int Engine_SettingDecision(int setting, int dev, int engineOnLine, int noEngine, int byDefault);
void Engine_SettingApply(int setting);
int Engine_SelfTestSwitches(int *checks); // engine.c: --engine-fail reader, the setting's rule, the default, the fallback per run and per level

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
	// A second native texture (round of 2026-10-16): program "ns" reads it as
	// its normal map in slot 3 (linear RGBA8, Engine_TextureCreate with
	// linearData). 0 or 0xffffffff = none; every material made with memset has none.
	u32 nativeTexture2;
} EngineMaterial;

#define ENGINE_PROGRAM_NE 0 // "ne", the engine program (platform/native_shaders.inc ctr_native_engine_shader)
#define ENGINE_PROGRAM_NW 1 // "nw", the water program (platform/engine_water_shader.inc)
// "ns", the lit program of the static model (round of 2026-10-16,
// platform/engine_static_shader.inc; --engine-static-lit). Its inputs besides
// the ones of "ne" (texSource 1 only, alpha cutoff, sRGB re-encode, tint):
//   a_uvBox      the vertex normal in world axes, packed: xyz = (n * 0.5 + 0.5)
//                * 255 rounded, w = 255; w 0 = no normal (the face's own normal
//                from the screen derivatives of the position stands in)
//   slot 3       "s_normal", the material's normal map (nativeTexture2):
//                tangent space, glTF convention (+X right, +Y up = towards
//                smaller v, +Z out), linear values, n = texel * 2 - 1
//   params3.z    0 = lit without a normal map, 1 = lit with the normal map,
//                2 = UNLIT (glTF KHR_materials_unlit: base colour x vertex
//                colour x tint only - no light, no grade; a sky, a glow)
//   params3.w    the normal map's scale (glTF normalTexture.scale) on x and y
//   split        xyz the direction towards the sun (unit, world axes), w the
//                ambient light from above (a normal straight up gets it all)
//   params4      xyz the camera position in world units (the side a double-sided
//                face is seen from), w the sun's diffuse strength
//   vramPage     the STYLE (EngineDraw.user2; "ns" has no VRAM page): x the
//                saturation (1 = unchanged), y the contrast around mid grey
//                (1 = unchanged), z 1 = flat shading (the face's own normal from
//                the screen derivatives, no vertex normal, no normal map), w the
//                ambient light from below (a normal straight down gets it all;
//                in between the two mix with 0.5 + 0.5 n.y - a hemisphere light)
//   params.x     0 = no texture (the colour is vertex colour x tint), 1 = the
//                native texture of slot 0, as in "ne"
//   tint         rgb the MATERIAL tint alone (linear, put on the display curve),
//                a the material alpha x the draw alpha (Engine_NsTints)
//   vramClut     rgb the DRAW tint ("ns" has no CLUT): the game's colour of the
//                draw (a driver's depth cue toward the ground's far colour,
//                EngCmdl_Tints), a factor on display values as "ne" applies it to
//                the classic driver - never put on the curve; w 1
// The tangent frame comes from the screen derivatives of the world position and
// the texture coordinates (no tangents in the vertex); the colour is
// texel x vertex colour x tint x draw tint x (ambient(n) + diffuse * max(0, n . sun)).
#define ENGINE_PROGRAM_NS 2
// The uniform tints of a draw of "ns" from the material's and the draw's
// (platform/engine_render.c): tint = the material tint (alpha: the product),
// displayTint = the draw tint (w 1). Pure.
void Engine_NsTints(const float materialTint[4], const float drawTint[4], float tint[4], float displayTint[4]);
#define ENGINE_PROGRAM_COUNT 3

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

// The vertex of every engine mesh (28 bytes): location 0 position, 1 texcoord,
// 2 colour (the layout of struct NativeProbeVertex), then 3 the UV box.
//
// THE UV BOX (round of 2026-10-16). The texels the "ne" VRAM path (texSource 2)
// may read for the triangle, umin..umax x vmin..vmax in texels of the page,
// STORED AS uvBox = {umin, vmin, 255 - umax, 255 - vmax}: a vertex whose box
// was never written (zeroed) means the whole page, no clamp - the behaviour
// from before the box. Read and write it only through Engine_VertexSetBox /
// Engine_VertexBoxGet. The program clamps floor(v_uv) into it, so the rounding of the
// interpolation never reaches a texel outside the box of the corners - one
// before the smallest corner, or one past 255 that wraps to texel 0 of the same
// page. Such a texel is foreign or the transparent word 0x0000, and a magnified
// small box showed it as noise along its border. The PS1 rasteriser never
// leaves that box; the clamp keeps today's mapping and adds only that rule.
// It reaches the fragment stage as a flat attribute, taken from the provoking
// (first) vertex of each triangle, so every vertex of a triangle - and of a
// face or sprite whose triangles share vertices - carries the same box.
// The writer of the texcoords fills it, from the same corners:
//   - a VRAM triangle, face or sprite: Engine_VertexBoxOfCorners (or the box of
//     the corners its triangles use);
//   - every other vertex: Engine_VertexBoxFull (0, 0, 255, 255, stored as four
//     zeros), no limit inside the page. Only texSource 2 of "ne" reads the box;
//     the mosaic table (3) clamps to its cell itself, native textures (1) and
//     "nw" never read it.
// A writer that zeroes its vertices and leaves the box out gets the whole page
// (no clamp, as before), never texel (0, 0) alone; stack bytes that are not
// zeroed still give a wrong box, so every writer of a VRAM vertex sets it.
typedef struct
{
	float position[3];
	float texcoord[2];
	u8 color[4];
	u8 uvBox[4];
} EngineVertex;

CTR_STATIC_ASSERT(sizeof(EngineVertex) == 28);

// One texel of a corner for the box: a texcoord of a VRAM layout is a whole
// texel; rounded to the nearest and held inside the page.
static inline u8 Engine_UvBoxTexel(float t)
{
	if (!(t > 0.0f))
	{
		return 0;
	}
	if (t >= 255.0f)
	{
		return 255;
	}
	return (u8)(int)(t + 0.5f);
}

// Writes the box umin..umax x vmin..vmax into v[0..count), stored with the
// maxima as 255 - max (zero = the whole page).
static inline void Engine_VertexSetBox(EngineVertex *v, int count, u8 umin, u8 vmin, u8 umax, u8 vmax)
{
	for (int i = 0; i < count; i++)
	{
		v[i].uvBox[0] = umin;
		v[i].uvBox[1] = vmin;
		v[i].uvBox[2] = (u8)(255u - umax);
		v[i].uvBox[3] = (u8)(255u - vmax);
	}
}

// The box of a vertex as texels: out = umin, vmin, umax, vmax.
static inline void Engine_VertexBoxGet(const EngineVertex *v, int out[4])
{
	out[0] = v->uvBox[0];
	out[1] = v->uvBox[1];
	out[2] = 255 - (int)v->uvBox[2];
	out[3] = 255 - (int)v->uvBox[3];
}

// The box of a vertex that reads no VRAM page: the whole page.
static inline void Engine_VertexBoxFull(EngineVertex *v, int count)
{
	Engine_VertexSetBox(v, count, 0, 0, 255, 255);
}

// The box of the corners v[0..count) (their texcoords), written into all of them:
// a triangle (3), a sprite quad (4).
static inline void Engine_VertexBoxOfCorners(EngineVertex *v, int count)
{
	u8 umin = 255;
	u8 vmin = 255;
	u8 umax = 0;
	u8 vmax = 0;

	for (int i = 0; i < count; i++)
	{
		const u8 u = Engine_UvBoxTexel(v[i].texcoord[0]);
		const u8 t = Engine_UvBoxTexel(v[i].texcoord[1]);

		umin = (u < umin) ? u : umin;
		umax = (u > umax) ? u : umax;
		vmin = (t < vmin) ? t : vmin;
		vmax = (t > vmax) ? t : vmax;
	}
	if (count > 0)
	{
		Engine_VertexSetBox(v, count, umin, vmin, umax, vmax);
	}
}

// --engine-sample-selftest (ctest engine_sample_selftest, platform/
// engine_testscene.c): on the GPU, whether "ne" keeps a fragment inside the UV
// box (instance and mosaic way, 4 and 1 samples), with built-in controls. Opens
// a hidden window and the Vulkan device, no game data. 0 = passed (or skipped
// without a device, said on stdout).
int Engine_SampleSelfTest(void);

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
	// More per-draw data of the material's program (EngineUniforms.vramPage,
	// only for a program that reads no VRAM page: "ns", its STYLE). 0 for every
	// submitter that zeroes the draw.
	float user2[4];
	// The mesh ranges this draw leaves out: bit r = range r (Engine_RangeSkipped;
	// ranges from 64 up are always drawn). 0 = every range, for every submitter
	// that zeroes the draw. Engine_InstanceSubmit sets it from the animated
	// layout slots of the instance's model (Engine_ModelRangeSkip).
	u64 rangeSkip;
} EngineDraw;

// 1 = range r of a mesh draw is left out (EngineDraw.rangeSkip).
static inline int Engine_RangeSkipped(u64 rangeSkip, u32 range)
{
	return (range < 64u) && (((rangeSkip >> range) & 1u) != 0u);
}

// clipFromModel ends in PSX screen pixels RELATIVE TO THE VIEW (the offset
// rect.w/2, rect.h/2 included); the split's draw offset of the ordering table
// (NativeGpu_SplitDrawOffset) is added by the renderer at draw time, as the
// native layer adds it (NativeRenderLayer_ScreenFromView).

#define ENGINE_DRAW_TRANSPARENT 0x0001u // draw after the opaque ones, back to front
#define ENGINE_DRAW_NO_DEPTH 0x0002u    // no depth test (overlays)

#define ENGINE_VIEWS 4
// Draws one view holds per frame: one for every slot of the largest instance
// pool (RLD_INSTANCE_POOL_LARGE, include/rldinstances.h - a track may place
// up to RLD_LEVEL_INSTANCES_MAX objects) and as many again for the track's
// draw groups, the effects and the second draw of split or reflecting models.
// A full list refuses the rest of the frame's draws in that view; the exit
// report gives the peak ("draws per view").
#define ENGINE_DRAWS_PER_VIEW (2 * RLD_INSTANCE_POOL_LARGE)
// The dynamic geometry per bank and frame: ENGINE_DYN_VERTICES_PER_FRAME vertices
// and 3/2 of that in indices (the scene's rings) and in expanded vertices (the
// renderer's store and each region of the GPU buffer). Heap memory made when the
// engine starts (Engine_RenderInit), none without --engine; 262144 holds every
// face of the largest track at once (49556 faces, 93931 triangles).
#define ENGINE_DYN_VERTICES_PER_FRAME 262144
#define ENGINE_SCENE_BANKS 2

// The dynamic rings of both banks (heap). Engine_RenderInit makes them; 1 = there,
// 0 = out of memory (none kept). Without them a dynamic draw is refused (counted
// as Engine_SceneOverflows).
int Engine_SceneRingsAlloc(void);
void Engine_SceneRingsFree(void);
u32 Engine_SceneRingsBytes(void); // what Engine_SceneRingsAlloc takes

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
int Engine_ScenePeak(int *view);  // the most draws one view held in a frame since the start; view: which one

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
//   tint           RGBA multiplier (material tint * draw tint; "ns": the
//                  material tint, the draw tint in vramClut - Engine_NsTints)
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
//   vramClut       x clut x, y clut y, z..w 0 ("ns": rgb the draw tint, w 1)
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
// Why a program is missing, for the log line of Engine_Fallback (Engine_RenderInit).
const char *Engine_ProgramsFailReason(void);
// The handle of "ne" (ShaderID as u32), 0xffffffff when not made.
u32 Engine_Program(void);
// The handle of an ENGINE_PROGRAM_* program, 0xffffffff when not made or unknown.
u32 Engine_ProgramOf(int program);
// Pure: 1 = "ns" is made at the start (--engine-static-model, or the hi-res
// drivers on: by default, kept on only with a file with CHRG in the roster).
int Engine_ProgramStaticWanted(const char *staticModel, int hiresChars);

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

// STREAMED VERTEX BUFFERS (round of 2026-10-17, for the hi-res drivers): a
// vertex buffer of ENGINE_RENDER_STREAM_REGIONS regions of verticesPerRegion
// vertices each, refilled by the renderer from a CPU array the owner hands it
// per frame. The owner registers it as an external mesh (verticesPerFrame <=
// verticesPerRegion, frames 1, its own index buffer). At the first draw of the
// buffer in a frame (Engine_DrawMarker) the renderer copies the bank's array
// into the region of that bank's frame and draws from there: the region rule of
// the dynamic buffer (a region is written only every third frame, the Vulkan
// device keeps two in flight with one frame of OT lag), so a frame never
// overwrites vertices the GPU still reads. A draw of a streamed buffer whose
// array was not set in its bank's frame is refused (counted as a bad mesh).
// Without a streamed buffer (every run without a hi-res driver to draw: no
// file with CHRG in the roster, or --no-hires-chars) the draw path is the one
// from before: one compare of a zero counter.
#define ENGINE_RENDER_STREAM_REGIONS 3
#define ENGINE_RENDER_STREAMS_MAX 16
// At a load stage only (the buffer is made host visible, nothing waits). The
// buffer's handle, 0xffffffff when refused (table full, no memory).
u32 Engine_RenderStreamCreate(u32 verticesPerRegion);
// A load stage only: the buffer leaves the stream table (no draw names it any
// more); the vertex buffer itself stays and is the caller's to destroy with
// Engine_RenderStaticBuffersFree ONE LOAD LATER (a command buffer of the load
// may still be recording with it, as the renderer's own grave assumes).
void Engine_RenderStreamRelease(u32 vertexBuffer);
// In a frame, before the submits that draw it: the array of the selected
// bank's frame (count <= verticesPerRegion). The caller keeps it unchanged
// until that bank's next Engine_RenderFrameBegin. 1 = set.
int Engine_RenderStreamSource(u32 vertexBuffer, const EngineVertex *vertices, u32 count);
u32 Engine_RenderStreamBytes(void); // the host-visible bytes of every live streamed buffer (the load line)
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
// Log lines: the model's twin counts and per texture page + CLUT its triangles and page texel box. Reads only.
void Engine_ModelLogTiles(const struct Model *model, const char *name);
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
// ANIMATED LAYOUTS (round of 2026-10-16). Every frame CTR_CycleTex_AllModels
// writes into a slot of header 0's layout table the layout of the frame
// (game/CTR/CTR_CycleTex.c: *ptrActiveTex = ptrArray[frame]). The import makes
// the triangles of such a slot once per layout the slot can hold (their own
// ranges and materials); a draw keeps the ranges of the layout the slot holds
// NOW. Returns the bits of EngineDraw.rangeSkip for mesh drawn for model this
// frame: read from model's slots at the call (nothing written), 0 for a mesh
// without animated slots; for another model than the imported one, or a slot
// holding a pointer outside its animtex list, the ranges of the load's layout.
u64 Engine_ModelRangeSkip(int mesh, const struct Model *model);
int Engine_SelfTestModel(int *checks);
// THE TRIANGLE SIDES (round 18, for the instance light below): per mesh
// triangle of an imported model, whether it is two-sided (its command has no
// cull bit) and whether the command it is drawn with has bit 29. Written by
// Engine_ModelImport (Engine_ModelNoteTriangles); NULL when the mesh has none
// (or is no longer the one noted). *generation changes with every import.
#define ENGINE_MODEL_TRI_TWO_SIDED 0x01u
#define ENGINE_MODEL_TRI_FLIP 0x02u
struct NativeTwinSource;
void Engine_ModelNoteTriangles(int mesh, const struct Model *model, const struct NativeTwinSource *src, const u32 *order, u32 keptTris);
const u8 *Engine_ModelTriangleBits(int mesh, u32 *count, u32 *generation);
int Engine_SelfTestModelTriangles(int *checks);

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
// Where the instance is on the screen: the box (x0, y0, x1, y1, fractions of
// the view rectangle) of the mesh frame's bounding box through the engine's
// matrix and through the layer's matrix of the retail queue (idpp). 1 = both
// boxes made (every corner in front of the camera). A measurement; draws nothing.
int Engine_InstanceScreenBoxes(const struct Instance *inst, const struct InstDrawPerPlayer *idpp, const struct PushBuffer *pb, int mesh, u32 frame,
                               float engineBox[4], float retailBox[4]);

// --------------------------------------------------------------------------
// THE RETAIL WRITER OF AN INSTANCE (platform/engine_inst_light.c; round 18)
//
// The colour and blend retail's primitive writer gives an instance's triangles,
// shown by the engine's instance draw. Chosen by what retail chose for the
// instance this frame - the labels RenderBucket_WriteInstanceCallbackLabels
// wrote from its flags (funcPtr[1] the primitive writer, funcPtr[2] the
// water/mirror side selector) - never by model or level. Reads the game state
// only (instance, idpp); the maths in float (see the file).
//
// 1 = the engine follows the instance's primitive writer (normal, key, lit);
// 0 = it does not (depth fade): retail draws the instance.
int Engine_InstLightWriterFollowed(const struct Instance *inst);
// The tints of the draws by the side selector and the view's handler (SPLIT:
// tint above, belowTint and drawBelow; REFLECTION / SPECIAL: mirrorTint).
// 0 = retail draws pieces the engine's draws can not show: retail draws.
int Engine_InstLightSplit(const struct Instance *inst, const struct InstDrawPerPlayer *idpp, EngineInstanceDraw *d);
// An instance drawn with the key or lit writer: its triangles lit as retail
// lights them (flat, per side) and blended per side, drawn from a variant of
// mesh (made once per import) as live vertices of frame (d->liveVertices, when
// set, is the source frame). Sets d->mesh, d->frame, d->liveVertices and the
// transparent flag. 1 = d is ready (also: nothing to do for another writer);
// 0 = refused (logged once per mesh): retail draws.
int Engine_InstLightApply(const struct Instance *inst, const struct InstDrawPerPlayer *idpp, int mesh, u32 frame, EngineInstanceDraw *d);
// For the engine oracle (side=back on E lines): 1 = corner0 is corner 0 of a
// back-side copy in the live vertices of the last Engine_InstLightApply.
int Engine_InstLightBackFace(const EngineVertex *corner0);
int Engine_SelfTestInstLight(int *checks);

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
// --engine-menuscene-zoom <f>[,<x>[,<yaw>]] (dev, round of 2026-10-16, Z3): the
// measuring camera of the scene - zoom f, the middle's place x (parts of half
// the view width), a still view at yaw degrees. Engine_MenuSceneZoomParse: 1 =
// valid (outputs set), 0 = refused.
extern float g_cfg_engineMenuSceneZoom;
extern float g_cfg_engineMenuSceneX;
extern int g_cfg_engineMenuSceneYawFixed;
extern float g_cfg_engineMenuSceneYaw;
int Engine_MenuSceneZoomParse(const char *text, float *zoom, float *x, int *yawFixed, float *yawRad);

// --------------------------------------------------------------------------
// HI-RES DRIVER, PROTOTYPE (platform/engine_hires.c; round of 2026-10-15, stage H)
//
// --engine-hires-dir <dir> (needs --engine): one synthetic test driver from a
// local folder in a dev format of its own (body.hrm, wheel.hrm, skin.hrt of
// tools/hires_testdriver.py; the .rldchar format is not touched) replaces the
// body and the wheels of seat 0 in a race and the driver of the menu scene: an
// RGBA texture at full resolution with its mip chain (Engine_TextureCreate), 3D
// wheels that spin, steer and spring from game state. --engine-hires-tex64: the
// texture reduced to 64 x 64 (the comparison run). Without the switch every
// function below returns at once.
#define ENGINE_HIRES_DIR_MAX 512
extern char g_cfg_engineHiresDir[ENGINE_HIRES_DIR_MAX];
extern int g_cfg_engineHiresTex64;
void Engine_HiresLoad(void); // from Engine_DriverCmdlLoad (load stage 5, before the upload)
int Engine_HiresOwns(const struct Driver *d); // 1 = d is the race's driver of the hi-res seat (Engine_DriverCmdlOwns answers 1)
// From Engine_DriverCmdlRoute, first: 1 = the engine drew the seat's body instance in this view.
int Engine_HiresRoute(struct Instance *inst, struct InstDrawPerPlayer *idpp, struct PushBuffer *pb);
// From EngineFx_Tires, first: 0 = not the hi-res seat, 1 = wheel meshes drawn (no sprite), -1 = sprites stand in (counted).
int Engine_HiresWheels(const struct Driver *d, const struct Instance *inst, const struct InstDrawPerPlayer *idpp, const struct PushBuffer *pb, int reflection);
int Engine_HiresMenuReady(void); // the menu scene draws the hi-res driver
void Engine_HiresMenuParts(int *bodyMesh, int *wheelMesh, float hullMin[3], float hullMax[3]);
void Engine_HiresNoteMenuFrame(void);
// --engine-hires-texcycle (dev, round of 2026-10-16, Z3): in the menu scene the
// skin alternates every ENGINE_HIRES_CYCLE_VBLANKS VBlanks: full, 64 x 64, flat
// key colour (the mask of the sharpness measurement). Engine_HiresMenuMaterial:
// the material the scene draws the hi-res body and wheels with, -1 = their own.
#define ENGINE_HIRES_CYCLE_VBLANKS 32
extern int g_cfg_engineHiresTexCycle;
// --engine-hires-anim (dev, round of 2026-10-16, Z4): the body from anim.hra /
// anim.hrt of the same folder (tools/hires_blender_driver.py), pose frames
// picked from game state (steer, air, drift); see platform/engine_hires.c.
extern int g_cfg_engineHiresAnim;
int Engine_HiresMenuMaterial(void);
// Pure: the wheel middle in world units relative to the instance origin, kart
// axes (DrawTires.c integers: ((scale * (+-0x90, 0x40, 0xc7 | -0x60)) >> 12) / 4);
// the wheel frame (columns axle, rim up, rim front; steer and roll in radians);
// the roll step of one tick (speedApprox * elapsedTimeMS / 8192 over the radius).
void Engine_HiresWheelMiddle(int wheel, s16 scaleX, s16 scaleY, s16 scaleZ, float out[3]);
void Engine_HiresWheelFrame(int wheel, double steer, double roll, int hazardTimer, double out[3][3]);
double Engine_HiresRollStep(int speedApprox, int elapsedTimeMS, int wheelSize);
int Engine_SelfTestHires(int *checks);

// --------------------------------------------------------------------------
// HI-RES DRIVERS FROM THE .rldchar (round of 2026-10-17; format:
// the hi-res format notes (include/rldhires.inc head), the chunk CHRG in
// include/rldhires.inc, the pose maths in include/rldhires_anim.inc; the code in
// platform/engine_hires_char.c (load, GPU side, draw) and
// platform/engine_hires_pose.c (clip state machine, wheels, log))
//
// ON BY DEFAULT (round of 2026-10-17, format 4.2; until then --dev
// --engine-hires-chars, now a player switch that changes nothing): whenever
// the engine draws, every custom character bound to a seat whose
// file has a CHRG chunk is drawn from its glTF model in a race - skinned, its
// clips picked from game state (read only), its own wheel nodes or the
// standard 3D wheels - instead of its CMDL; the CMDL stays the game's model
// (physics, LOD, the engine's FX). In a main menu level the roster's files
// with CHRG are loaded instead (driver select, menu scene). --no-hires-chars
// (a player switch) turns it off for the run: a file with CHRG then behaves
// exactly as one without. A run whose roster has no file with CHRG turns it
// off by itself at start (platform/native_chars.c, NativeChar_LoadRoster):
// no program "ns", no load stage, no exit report, no log line - a retail
// race does what it did before the default. Retail drivers are never drawn
// hi-res; the hi-res path only draws (game state is read, never written).
// --engine-hires-chars-log (--dev, not with --no-hires-chars): one line per
// logic tick and hi-res seat, from its first draw in view 0 of the tick:
//   [CTR Engine] hires char: vb <VBlank> seat <s> timer <gGT->timer> state
//   <base|jump|drift_left|drift_right|hit|forced|finished> want <clip asked for>
//   clip <clip drawn after the fallback, or rest> t <its time in s> blend <the
//   state's pose weight over the fade snapshot, 1 = no fade> prev <the clip
//   faded from, or none> steer <signed steer fraction -1..1, + = left>
//   steerclip <the steer clip drawn in base, or none> steerRaw <wheelRotation>
//   air <ticks off the ground> kart <kartState> drift <side +1 left / -1
//   right> rank <driverRank> yaw <front-left wheel yaw in kart space, read back
//   from the drawn matrix, rad> yawgame <wheelRotation x 4 game angle units,
//   rad> roll <front-left wheel roll read back from the drawn matrix, rad>
//   omega <roll step of the tick / tick time, rad/s> radius <front-left wheel
//   radius, world units> v <speedApprox x elapsedTimeMS / 8192 / tick time,
//   world units/s> pos <posCurr x y z> wheels <own|standard> box <x0 y0 x1 y1:
//   the hull on the screen of view 0, fractions of the view>
// A FINISHED race driver (ACTION_RACE_FINISHED, game/PlayLevel.c:240) shows
// podium_<driverRank + 1> for ranks 0..2 and lose from rank 3 (state
// finished; only a forced clip comes before it).
// With the engine off (--no-engine, or a level the engine falls back on) every
// bound file with CHRG logs "hires char off (--no-engine)" and draws its CMDL.
//
// THE DRAW (decision, see platform/engine_hires_char.c head): CPU skinning per
// frame into a streamed vertex buffer of the pose slot (Engine_RenderStream*,
// three regions as the renderer's dynamic buffer), every vertex in WORLD
// units and axes, drawn with program "ns" in the static model's "hires"
// style (vertex normals, normal maps, the sun of --engine-static-lit-sun).
// No change to any other draw, no share of ENGINE_DYN_VERTICES_PER_FRAME.
extern int g_cfg_engineHiresChars;
extern int g_cfg_engineHiresCharsLog;
// THE TEXTURE FILTER of the hi-res drivers: trilinear with anisotropy
// (ENGINE_TEXTURE_FILTERED), independent of --native-filter and of the glTF
// sampler's filters (only its wraps are read). --engine-hires-chars-filter
// <nearest|linear> (--dev, measuring only: the comparison image; not with
// --no-hires-chars) sets
// ENGINE_TEXTURE_PIXELATED or ENGINE_TEXTURE_FILTERED (the default).
extern int g_cfg_engineHiresCharsFilter;

// The fixed clip list of the format (section 4), index = clip id.
#define ENGINE_HIRES_CLIP_IDLE 0
#define ENGINE_HIRES_CLIP_STEER_LEFT 1
#define ENGINE_HIRES_CLIP_STEER_RIGHT 2
#define ENGINE_HIRES_CLIP_JUMP 3
#define ENGINE_HIRES_CLIP_DRIFT_LEFT 4
#define ENGINE_HIRES_CLIP_DRIFT_RIGHT 5
#define ENGINE_HIRES_CLIP_HIT 6
#define ENGINE_HIRES_CLIP_PODIUM_1 7
#define ENGINE_HIRES_CLIP_PODIUM_2 8
#define ENGINE_HIRES_CLIP_PODIUM_3 9
#define ENGINE_HIRES_CLIP_LOSE 10
#define ENGINE_HIRES_CLIP_MENU 11
#define ENGINE_HIRES_CLIP_FINISH_WIN 12  // optional (4.3): after the finish line on place 1..3
#define ENGINE_HIRES_CLIP_FINISH_LOSE 13 // optional (4.3): after the finish line from place 4
#define ENGINE_HIRES_CLIPS 14
#define ENGINE_HIRES_CLIP_REST (-1) // the rest pose: the end of every fallback chain
const char *Engine_HiresClipName(int clip); // "idle" .. "finish_lose", "rest" for ENGINE_HIRES_CLIP_REST, "?" else

#define ENGINE_HIRES_CHARS_MAX 8  // characters loaded at once (one per distinct file)
#define ENGINE_HIRES_SLOTS 12     // pose slots: one pose per slot and frame
#define ENGINE_HIRES_SLOT_SEATS 8 // slots 0..7: the seats of a race (slot = seat)
// Slots ENGINE_HIRES_SLOT_SEATS .. ENGINE_HIRES_SLOTS - 1 belong to the forced
// submits below (podium, results, driver select, menu scene); the caller picks
// one per thing it draws in a frame.

// THE LOOKUPS (valid from load stage 5 until the next level unload). A
// character handle is 0 .. ENGINE_HIRES_CHARS_MAX - 1, -1 = none (classic model).
int Engine_HiresCharOfSeat(int seat);              // the hi-res character the seat is drawn with in this level
int Engine_HiresCharOfFile(const char *file);      // by the file name (NativeChar_SeatFile / NativeChar_EntryFile)
int Engine_HiresCharOfEntry(int rosterEntry);      // a roster entry (NativeChar_EntryFile of it)
int Engine_HiresCharOfModel(const struct Model *model); // the CMDL model of a bound file (an instance's inst->model)
// The podium after a cup (a level without bound seats): the character of the
// seat on place 1..3 - seat data.cupPositionPerPlayer[place - 1], its file as
// bound in the LAST race level loaded. Loaded characters stay across level
// loads (dropped only when another level needs their room, at most
// ENGINE_HIRES_CHARS_MAX), and a level without bound seats keeps them all and
// makes the forced slots. -1 = none (a classic seat, or no race before).
int Engine_HiresCharOfPlace(int place);
int Engine_HiresCharOfLastRaceSeat(int seat);
const char *Engine_HiresCharFile(int ch); // the file name of a character, "" for none
// The clip a request draws after the fallback chain (FORMAT section 4): clip
// itself when the file has it, else the chain's first present one, else
// ENGINE_HIRES_CLIP_REST. *full (may be NULL) = 1 when a drift falls back to its
// steer clip held at full lock.
int Engine_HiresCharResolveClip(int ch, int clip, int *full);
// The hull of the character in model units (kart model units before the
// instance scale: x left, y up, z front; rest pose and every clip). 0 = none.
int Engine_HiresCharHull(int ch, float hullMin[3], float hullMax[3]);

// ONE CHARACTER PER SLOT AND FRAME: the renderer draws a slot from the array of
// its last skin in the frame, so a second character in the same slot and frame
// is refused (Engine_HiresCharSubmit returns 0, logged once); a second call for
// the same character draws the first call's pose.
//
// SEAT OVERRIDE (podium, results screen in a race level): from the next logic
// tick the seat's race state machine is replaced by clip (0 .. ENGINE_HIRES_CLIPS
// - 1, played as the clip plays: podium, lose, menu loop; cross-faded like any
// clip change, its time from the tick it starts); a negative clip gives the
// seat back to the game state. Cleared at every level load.
void Engine_HiresSeatForceClip(int seat, int clip);

// THE FORCED SUBMIT: one hi-res character with a clip the caller chooses, at an
// instance (its world matrix with inst->scale and the tints of the driver
// route; inst, idpp, pb from the draw dispatch) or at a world matrix (inst
// NULL: worldFromModel maps model units to world units, drawn in view `view`
// into OT cell `cell` of `ot`, tint as given). Draw time only; the pose is
// computed and skinned into the slot at this call (one pose per slot and frame:
// a second call with the same slot in a frame draws the first one's pose).
typedef struct
{
	int slot;          // ENGINE_HIRES_SLOT_SEATS .. ENGINE_HIRES_SLOTS - 1
	int clip;          // ENGINE_HIRES_CLIP_* (resolved by Engine_HiresCharResolveClip)
	float time;        // seconds from the clip's start, the caller's game-time clock (loops wrap, others hold)
	int prevClip;      // -1 = none; else the pose is blended from prevClip ...
	float prevTime;    // ... at prevTime ...
	float fade;        // ... towards clip by fade (0 = prevClip only, 1 = clip only)
	float wheelRoll;   // radians: the roll of every wheel (0 = at rest)
	float wheelSteer;  // radians: the yaw of the front wheels (positive = left, as wheelRotation)
	struct Instance *inst;
	struct InstDrawPerPlayer *idpp;
	struct PushBuffer *pb;
	EngineMat4 worldFromModel; // inst NULL
	int view;                  // inst NULL
	u32 *ot;                   // inst NULL
	int cell;                  // inst NULL
	float tint[4];             // inst NULL
	int hideKart;              // 1 = a file with the kart part (CHRG parts bit 0) is drawn without its kart and wheels
} EngineHiresSubmit;
int Engine_HiresCharSubmit(int ch, const EngineHiresSubmit *s); // draws submitted; 0 = none (the caller draws its classic way)

// The file side (platform/native_chars.c): the roster entry a seat is bound to
// (-1 none), whether an entry's file lists a CHRG chunk (noted at start, only
// while the hi-res drivers are on), and the chunk read again from the file at a load
// stage (malloc'd bytes, SHA-256 checked, the file's CMDL still the loaded one;
// NULL with *why).
int NativeChar_SeatEntry(int seat);
int NativeChar_EntryHasChrg(int entry);
u8 *NativeChar_ReadChrg(int entry, size_t *size, const char **why);

// The engine side's hooks (load stage 5 and the frame).
void Engine_HiresCharsLoad(void);              // from Engine_DriverCmdlLoad, before the upload
void Engine_HiresCharsCollect(struct GameTracker *gGT); // from Engine_FrameCollect: the logic ticks of the hi-res seats
int Engine_HiresCharOwns(const struct Driver *d); // 1 = d's body and wheels are drawn hi-res in this level
int Engine_HiresCharRoute(struct Instance *inst, struct InstDrawPerPlayer *idpp, struct PushBuffer *pb); // 1 = drawn (Engine_DriverCmdlRoute)
// 1 = inst is the driver select instance the engine drew hi-res in this frame
// (clip menu; its wheels are part of it: the tire pass draws no sprite).
int Engine_HiresCharSelectDrawn(const struct Instance *inst);
int Engine_SelfTestHiresChar(int *checks);

// --------------------------------------------------------------------------
// STATIC MODEL (round of 2026-10-16; platform/engine_static_load.c reads the
// file, platform/engine_static.c makes the GPU side and draws it)
//
// --engine-static-model <file.glb> (needs --engine): an external model in glTF
// binary form, its positions already in world units and world axes (no
// transform of its own beyond the glTF node matrices), drawn in every race
// level as static geometry: full-resolution textures with their mip chains,
// u32 indices, pieces with a box each for the view test. With --engine-hide
// track the level's own geometry is not drawn, so only the model shows.
#define ENGINE_STATIC_PATH_MAX 512
extern char g_cfg_engineStaticModel[ENGINE_STATIC_PATH_MAX];
// --engine-static-light: a fixed directional light baked into the vertex
// colours at load (from the model's normals); without it texture x vertex
// colour x base colour only.
extern int g_cfg_engineStaticLight;
// --engine-static-lit (round of 2026-10-16): the model drawn with program "ns"
// (normal maps, a sun and ambient light) instead of "ne"; the vertex normals are
// packed into the vertex at load (ENGINE_STATIC_BUILD_NORMALS).
// --engine-static-lit-sun <x,y,z,ambient,diffuse[,below]>: the sun's direction
// (towards it, world axes, normalised at load), the ambient light from above,
// the sun's strength and the ambient light from below (without it the same as
// from above); without the switch ENGINE_STATIC_SUN_DEFAULT. The default keeps
// the mean brightness of the flat model (round of 2026-10-16: the first lit
// runs with 0.50 / 0.60 came out at 0.64 of the flat mean): a road in the sun
// about 1.29, a wall away from it about 0.88, a ceiling 0.80.
extern int g_cfg_engineStaticLit;
extern float g_cfg_engineStaticSun[6];
#define ENGINE_STATIC_SUN_DEFAULT {0.35f, 0.85f, 0.40f, 0.95f, 0.40f, 0.80f}
// --engine-static-style <hires|lowpoly> (the look stage, round of 2026-10-16): the
// look of the lit model (it switches --engine-static-lit on).
//   hires    vertex normals and the normal maps, colour as it is (style 1, 1, 0)
//   lowpoly  flat shading by the faces, no normal maps, saturation and contrast
//            raised (ENGINE_STATIC_LOWPOLY_GRADE)
// --engine-static-grade <saturation,contrast> (each 0..3): the colour grade of
// the style instead of its own.
#define ENGINE_STATIC_STYLE_HIRES 0
#define ENGINE_STATIC_STYLE_LOWPOLY 1
extern int g_cfg_engineStaticStyle;
extern float g_cfg_engineStaticGrade[2]; // < 0 = the style's own
#define ENGINE_STATIC_LOWPOLY_GRADE {1.25f, 1.10f}

// THE FILE SIDE (engine_static_load.c): no device, no game state - what the
// selftest checks on a made-up model.
//
// A source of bytes: the file, or memory for the selftest. read() returns 1
// when all `bytes` from `offset` were read.
typedef struct
{
	int (*read)(void *user, u64 offset, void *dst, u32 bytes);
	void *user;
	u64 size;
	const char *baseDir; // folder of the .glb for image uris ("" or NULL: none)
} EngineStaticSource;
// The file source (fopen/seek); 0 when the file does not open. Close it with
// EngineStatic_SourceFileClose.
int EngineStatic_SourceFile(const char *path, EngineStaticSource *out);
void EngineStatic_SourceFileClose(EngineStaticSource *src);

#define ENGINE_STATIC_ALPHA_OPAQUE 0
#define ENGINE_STATIC_ALPHA_MASK 1
#define ENGINE_STATIC_ALPHA_BLEND 2 // drawn as MASK with cutoff 0.5 (counted)

typedef struct
{
	int image;          // EngineStaticModel image of the base colour texture, -1 none
	int wrapU, wrapV;   // NativeGfxWrap of its sampler: 0 repeat, 1 clamp, 2 mirror
	float baseColor[4]; // baseColorFactor
	int alphaMode;      // ENGINE_STATIC_ALPHA_*
	float alphaCutoff;  // MASK: glTF alphaCutoff (default 0.5)
	int doubleSided;
	// The normal map (glTF normalTexture, TEXCOORD_0 only): image, -1 none; its
	// sampler's wrap; scale (default 1).
	int normalImage;
	int normalWrapU, normalWrapV;
	float normalScale;
	// glTF KHR_materials_unlit: drawn without light and grade (program "ns").
	int unlit;
} EngineStaticMaterial;

typedef struct
{
	void *gltf; // the parsed JSON (cgltf_data), owned
	EngineStaticSource *src;
	u64 binOffset; // where the GLB BIN chunk's data starts in the source
	u64 binSize;
	int materialCount; // the file's materials + 1: the last one is the default (a primitive without material)
	EngineStaticMaterial *materials;
	int imageCount;
	// Counts of the file (EngineStatic_Open)
	u32 triangles;         // of every TRIANGLES primitive of every node of the scene
	u32 primitives;        // drawn ones
	u32 skippedPrimitives; // not TRIANGLES, no POSITION, a type not read, sparse
	u32 mirroredNodes;     // node matrices with a negative determinant (winding turned)
	u32 noUvTriangles;     // triangles of primitives without TEXCOORD_0
	float boxMin[3];       // from the POSITION accessors' min/max through the node matrices (world units)
	float boxMax[3];
} EngineStaticModel;

// Parses the JSON chunk of the GLB of src (the BIN chunk is not read: the
// attributes are read from the source when the pieces are made). 1 = ok; else
// 0 and why. Free with EngineStatic_Close.
int EngineStatic_Open(EngineStaticSource *src, EngineStaticModel *out, char *why, int whySize);
void EngineStatic_Close(EngineStaticModel *model);

// One piece: its vertices, its u32 indices (into its own vertices), its ranges
// (EngineRange.material = the model's material index, one range per run of
// triangles of one material, in file order) and the box of its vertices.
typedef struct
{
	EngineVertex *vertices;
	u32 vertexCount;
	u32 *indices;
	u32 indexCount;
	EngineRange *ranges;
	u32 rangeCount;
	float boxMin[3];
	float boxMax[3];
} EngineStaticPiece;

// Triangles per piece at most (ENGINE_STATIC_PIECE_TRIANGLES): a piece holds
// at most 3 x that many vertices, below the mesh limit of 1 << 20.
#define ENGINE_STATIC_PIECE_TRIANGLES 262144u
#define ENGINE_STATIC_PIECES_MAX 64
// The pieces of the model: the triangles split by their centres, the longest
// side of the centres' box at its middle, until a piece has at most
// maxTriangles (deterministic: the same file gives the same bytes). Every
// triangle is in exactly one piece; the vertex colour is COLOR_0 (white
// without) times the light with ENGINE_STATIC_BUILD_LIGHT in flags; with
// ENGINE_STATIC_BUILD_NORMALS the vertex normal in world axes is packed into
// the UV box (program "ns": xyz = (n * 0.5 + 0.5) * 255 rounded, w = 255; a
// primitive without NORMAL gives four zeros), else the box is the full page as
// before. onPiece is called once per piece in
// piece order and keeps nothing of it (the arrays are freed after the call);
// it returns 0 to stop. Returns the piece count, or -1 with why (more than
// ENGINE_STATIC_PIECES_MAX pieces, a read that failed, out of memory, onPiece
// stopped).
#define ENGINE_STATIC_BUILD_LIGHT 0x1u
#define ENGINE_STATIC_BUILD_NORMALS 0x2u
int EngineStatic_BuildPieces(const EngineStaticModel *model, u32 maxTriangles, u32 flags, int (*onPiece)(void *user, const EngineStaticPiece *piece),
                             void *user, char *why, int whySize);

// One image as RGBA8 for Engine_TextureCreate: decoded (PNG only) and, if an
// edge is not a power of two or below 16, scaled UP to the next power of two
// of at least 16 (bilinear, never down). 1 = ok (free with
// EngineStatic_FreeImage); 0 with why (not PNG, unreadable, an edge above
// 2048).
typedef struct
{
	int width, height;         // as made (powers of two, 16..2048)
	int fileWidth, fileHeight; // as in the file
	int resampled;             // 1 = scaled up
	u8 *rgba;
} EngineStaticImage;
int EngineStatic_ReadImage(const EngineStaticModel *model, int image, EngineStaticImage *out, char *why, int whySize);
void EngineStatic_FreeImage(EngineStaticImage *img);
// The pure helpers the selftest checks directly.
int EngineStatic_UpEdge(int edge); // next power of two >= max(edge, 16); 0 above 2048
int EngineStatic_ResampleUp(const u8 *src, int sw, int sh, u8 *dst, int dw, int dh); // bilinear, clamped edges; 1 = ok
int Engine_SelfTestStaticLoad(int *checks);

// GPU buffers that belong to the caller (engine_render.c; registered with
// Engine_MeshCreateExternal): the vertex layout of every engine buffer, u32
// indices. Made at a load stage only (the upload waits); 0xffffffff when
// refused. Engine_RenderStaticBuffersFree takes either handle as 0xffffffff.
u32 Engine_RenderStaticVertexBuffer(const EngineVertex *vertices, u32 count);
u32 Engine_RenderStaticIndexBuffer(const u32 *indices, u32 count);
void Engine_RenderStaticBuffersFree(u32 vertexBuffer, u32 indexBuffer);

// THE ENGINE SIDE (engine_static.c).
void Engine_StaticLoad(void);                     // load stage 8, after Engine_TrackLoad, before Engine_RenderUploadMeshes
void Engine_StaticCollect(struct GameTracker *gGT); // from Engine_FrameCollect, before the track
// The CORE box of the model in world units (1st to 99th percentile of the
// vertices per axis, without a sky sphere or other far scenery); 0 when none is
// loaded. The fly camera's ring is built over it.
int Engine_StaticBox(float boxMin[3], float boxMax[3]);
// Pure: 1 = the box may be seen by the camera (not wholly behind the near
// plane, not wholly outside one side of the view's pyramid).
int Engine_StaticBoxVisible(const EngineCamera *cam, const float boxMin[3], const float boxMax[3]);
int Engine_SelfTestStatic(int *checks);

// --------------------------------------------------------------------------
// FLY CAMERA (round of 2026-10-16; platform/engine_flycam.c)
//
// --engine-flycam (needs --engine): view 0 is taken off the game's camera
// (CAMERA_FLAG_FROZEN, as the preview path camera of game/native_flyin.c) and
// flown by keyboard and mouse; never under --deterministic.
// --engine-flycam-auto <frames>: a deterministic flight instead - a closed
// loop over stations, one loop in <frames> game frames (each station held
// ENGINE_FLYCAM_HOLD frames, then a smooth move to the next), repeated. The
// stations: --engine-flycam-path <file> (lines "x y z  lx ly lz": camera and
// the point it looks at, world units; '#' comments), else
// ENGINE_FLYCAM_RING stations on a ring low above the static model's core box
// (the level's vertex box without a model), looking down at a slant. Both
// modes wait for the end of the level's intro (START_OF_RACE cleared by the
// game camera's fly-in) before they take the camera, and count their frames
// from there. With --shot-name one snapshot per station in the first loop:
// <shot-name>-sNN.bmp.
// Only pb->pos, pb->rot of view 0 and the freeze flag of its camera are
// written; nothing else of the game.
extern int g_cfg_engineFlycam;
extern int g_cfg_engineFlycamAuto; // frames of one loop, 0 = off
#define ENGINE_FLYCAM_PATH_MAX 512
extern char g_cfg_engineFlycamPath[ENGINE_FLYCAM_PATH_MAX];
#define ENGINE_FLYCAM_STATIONS_MAX 256
#define ENGINE_FLYCAM_RING 8
#define ENGINE_FLYCAM_HOLD 30

typedef struct
{
	float pos[3];
	float look[3];
} EngineFlycamStation;

// From game/MAIN/MainMain.c after the game logic of a frame (not while loading).
void Engine_FlycamTick(struct GameTracker *gGT);
// Pure helpers (selftest):
// the ring of ENGINE_FLYCAM_RING stations above a box; returns the count.
int Engine_FlycamRing(const float boxMin[3], const float boxMax[3], EngineFlycamStation *out);
// A path file's text to stations; returns the count, -1 with *badLine (1-based) on a bad line.
int Engine_FlycamParsePath(const char *text, EngineFlycamStation *out, int max, int *badLine);
// The camera at game frame `frame` of a loop of `loopFrames` over `count`
// stations: position and look point; *station = the station held now or -1
// while moving, *holdFrame = frames into that hold (0 while moving).
void Engine_FlycamAt(const EngineFlycamStation *st, int count, int loopFrames, u32 frame, float pos[3], float look[3], int *station, int *holdFrame);
// The game rotation (pb->rot, 4096 a turn) that looks from pos at look, the
// convention of game/CAM.c (rot.x = 0x800 - pitch, rot.y = yaw, rot.z = 0).
void Engine_FlycamRot(const float pos[3], const float look[3], s16 rot[3]);
int Engine_SelfTestFlycam(int *checks);
// --engine-flycam-smooth (with --engine-flycam-auto; round of 2026-10-16, the
// demo video): no hold at the stations; one centripetal Catmull-Rom curve
// through the station positions and one through the look points, travelled at
// a constant speed along the position curve's arc length in <frames> game
// frames, from the first station to the last (an open path: no way back);
// after the last frame the camera stays at the last station. No snapshot per
// station (--shot-series takes the frames of a video).
extern int g_cfg_engineFlycamSmooth;
// The pose at game frame `frame` of a flight of `frames` frames; pure and
// deterministic (the arc-length table is rebuilt when st/count change). Returns
// 0 for count < 1 (pos/look zero).
int Engine_FlycamSmoothAt(const EngineFlycamStation *st, int count, int frames, u32 frame, float pos[3], float look[3]);

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
// TRACK PODIUM (Z2) (platform/engine_track.c; round of 2026-10-16)
//
// The podium scene after a cup (an Adventure Arena level with podiumRewardID
// set, game/LOAD/LOAD_TenStages.c stage 8): the track loads as on a race
// level, and the podium's own models (data.podiumModel_*: the three dancers,
// Tawna, the Dingo fire, the stands; the reward gGT->modelPtr[podiumRewardID])
// join the track's model table. Their instances - CS threads, no InstDef - go
// through the instance route of the level instances while the podium plays.
// The hub levels themselves stay with the PS1 path.
void Engine_TrackPodiumLoad(void); // load stage 8, right after Engine_TrackLoad, podium only
void Engine_TrackPodiumEnd(void);  // load stage 0 of the next level: the summary lines, podium state off

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
// pointer - 1, the AnimTex whose first word holds the IconGroup4 shown now), mosaic or not;
// form = ENGINE_MOSAIC_FORM_* of its leaf. 1 = the face became a face of an animated row (keyed
// by cell, mirror and form) holding what the group shown now gives - the cells of its mosaic
// table, or its finest layout as one cell - texcoords rewritten, keyed with *keyTpage (the abr of
// the row now) and clut 0 like a mosaic face; *row = the row, *abrSet = bit a for every abr a
// group of the cell's frames gives the row. 0 = unchanged (frozen at load, reason counted).
int Engine_TrackMosaicAnimFace(u32 cell, int mirrored, int form, const u8 *slotOf, EngineVertex *v, int vertCount, u16 *keyTpage, int *row, u8 *abrSet);
// From Engine_FrameCollect after Engine_TrackCollect: rewrites the animated rows whose AnimTex cell names another group now.
void Engine_TrackMosaicFrame(void);
// The abr (0..3) an animated row holds now (its tiles' blend), -1 for any other row.
int Engine_TrackMosaicAnimAbr(int row);
int Engine_TrackMosaicEnd(void);     // builds the data texture, logs the counters; 1 = ready
void Engine_TrackMosaicUnload(void); // level unload (deferred destroy)
u32 Engine_TrackMosaicTexture(void); // TextureID for slot 3, 0xffffffff when none
int Engine_SelfTestTrackMosaic(int *checks);

// --------------------------------------------------------------------------
// ENGINE ORACLE (track) - the track side of the material oracle (--engine-oracle,
// round of 2026-10-16; platform/engine_track.c, RETAIL ORACLE). Every function
// does nothing outside a logged frame (or the load block of a logged run).
//
// From DrawLevelOvr1P (game/226): 1 = run the oracle pass now (the whole PS1
// walk, logged and never drawn, before the frame's own walk).
int Engine_TrackOracleWanted(void);
struct QuadBlock;
struct mesh_info;
// During the oracle pass, from the 226 packet writers: a packet's corner
// positions (level units, 3 per corner, the packet's vertex order) and their count;
// sourceEntry = the table entry the walk's last mosaic reload read in this block (-1 none).
void Engine_TrackOracleNotePrim(const void *prim, const s16 *cornerPositions, int corners, int sourceEntry);
// During the oracle pass: the packets one quadblock's handler wrote [primFrom,
// primTo) and the clip records it left [clipFrom, clipTo); water = a water
// bucket; marks: bit 0 the sieve dropped a layout while the block was drawn,
// bit 1 the distance fade moved its midpoints (colour and place, the geomorph);
// walkGrid: its nine grid points as the walk holds them (27 values).
void Engine_TrackOracleBlock(const struct QuadBlock *block, int water, int marks, const s16 *walkGrid, const void *primFrom, const void *primTo,
                             const void *clipFrom, const void *clipTo);
// The packets the clip consumer wrote for one record (R lines of the block that wrote it, clip=1).
void Engine_TrackOracleClip(const void *record, const s32 *positionSum, int corners, const void *primFrom, const void *primTo);
// End of the pass: one log line with the counts (completed 0 = the walk stopped).
void Engine_TrackOracleDone(int completed, const void *primFrom, const void *primTo);
// From DrawLevelOvr1P_OracleLayouts: one layout 226 resolved for a face (mirrored =
// the walk draws the face mirrored: the line gets the face's corner order; level
// 0..3; face -1 / level -1 = the block's low layout; layout NULL with sieve 1 =
// the sieve dropped the face's group) and the table a face's level 3 names.
void Engine_TrackOracleLayout(const struct QuadBlock *block, int face, int level, int finest, const struct TextureLayout *layout, int mirrored, int anim,
                              int sieve);
void Engine_TrackOracleTable(const struct QuadBlock *block, int face, const struct TextureLayout *table, int anim, int mirrored);
// game/226: the layouts the walk resolves for every face of the level (animOnly
// = only the faces of AnimTex cells), handed to the two functions above.
void DrawLevelOvr1P_OracleLayouts(const struct mesh_info *mesh, int animOnly);
// The table row a table face reads (its texcoord u, v): cells per side (4, or 1
// for a whole-face row), its form (ENGINE_MOSAIC_FORM_*), the level a whole-face
// row holds (3 or 2), 1 = faulted (--engine-oracle-fault cell), its 16 cells and
// its levels 0, 1, 2 (pointers into the row set, valid until the next load).
// 0 = no such row.
int Engine_TrackMosaicOracleUv(float rowU, float rowV, float s, float t, float pickS, float pickT, float *uOut, float *vOut); // the shader's UV at (s, t)
int Engine_TrackMosaicOracleSpread(float u, float v, int block, int mid); // --engine-oracle-fault spread
int Engine_TrackMosaicOracleRow(float u, float v, int *side, int *form, int *finest, int *faulted, const struct TextureLayout **cells,
                                 const struct TextureLayout **levels);
// --engine-oracle-fault cell: swaps two different cells of the row of the table face at
// (u, v) (block / mid face for the log line), before the data texture is made. 1 = done.
int Engine_TrackMosaicOracleFault(float u, float v, int block, int mid);

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
// ENGINE ORACLE (core) (platform/engine_oracle.c; round of 2026-10-16)
//
// The material comparison W1: in chosen frames both paths write, per drawn
// face, the texture page, CLUT, texel window and blend they use, one text line
// each (tab separated):
//   F <tag> <levelId> <container 0|1> <raceFrame> <views>
//   R|E <src> <srcId> <sub> <tpX>,<tpY>,<depth> <clutX>,<clutY> <u0>,<v0>,...
//       <blend o|0..3> <cell row,col|-> <extra key=value key=value ...|->
// (F has a 7th column, extra: vram=<the frame's VRAM dump>.)
// R = the primitives the PS1 path makes for the source in that frame, E = what
// the engine reads for it. Dev only (--engine-oracle <file>, set in main.c);
// without --engine or without the file every function returns at once.
#define ENGINE_ORACLE_PATH_MAX 512
extern char g_cfg_engineOracle[ENGINE_ORACLE_PATH_MAX]; // --engine-oracle <file>; "" = off
// --engine-oracle-fault <clut|cell>:<n> (W3, repeatable; main.c): n >= 0 plants the
// fault, -1 = off. clut: P1, the n-th VRAM material of the instance sources;
// cell: P2, the mosaic side.
extern int g_cfg_engineOracleFaultClut;
extern int g_cfg_engineOracleFaultCell;
extern int g_cfg_engineOracleFaultSpread; // --engine-oracle-fault spread:<n> (engine_track_mosaic.c), -1 = off
// Arms the oracle for the frame made now: call it in the frame's logic, before
// MainFrame_RenderFrame of that frame. Writes the F line at once and stays
// active until the next Engine_RenderFrameBegin. raceFrame -1: gGT->framesInThisLEV.
void Engine_OracleArm(const char *tag);
void Engine_OracleArmFrame(const char *tag, int raceFrame);
int Engine_OracleActive(void);    // 1 while an armed frame is made
void Engine_OracleFrameEnd(void); // from Engine_RenderFrameBegin
void Engine_OracleLevelUnload(void); // from Engine_RenderLevelUnload: the W3 fault goes with the material table

#define ENGINE_ORACLE_ID_MAX 48
#define ENGINE_ORACLE_EXTRA_MAX 256
#define ENGINE_ORACLE_BLEND_OPAQUE (-1)
typedef struct
{
	char src;                         // 'T','I','D','X','S','W','M'
	char srcId[ENGINE_ORACLE_ID_MAX]; // T: quadblock index; I: "<model>#<n>"; D: seat; X: effect kind
	int sub;                          // running number inside the source (R: primitive face, E: triangle)
	int textured;                     // 0: page, clut and uv written as '-'
	int tpX, tpY, depth;              // page origin in VRAM words, depth 0/1/2 (4/8/15 bit)
	int clutX, clutY;                 // VRAM words; '-,-' when depth == 2 or clutX < 0
	int uvCount;                      // 3 or 4 (anything else: uv '-')
	int uv[8];                        // u0,v0,u1,v1,u2,v2[,u3,v3] page texels
	int blend;                        // ENGINE_ORACLE_BLEND_OPAQUE or abr 0..3
	int cellRow, cellCol;             // mosaic tile, -1 = '-'
	char extra[ENGINE_ORACLE_EXTRA_MAX]; // "key=value key=value" (one space between pairs); "" = '-'
	int colorCount;                   // 0, or 3/4 corner colours written as col=r,g,b;r,g,b;...
	u8 color[4][3];
	int raw;                          // 1: the raw words are written as tp=0x.... cl=0x....
	u16 rawTpage, rawClut;
} EngineOracleFace;
// One R or E line (kind 'R' / 'E'); only while Engine_OracleActive().
void Engine_OracleWrite(char kind, const EngineOracleFace *f);
// The line Engine_OracleWrite writes, with its line end, into out (pure, for
// the selftest). Returns its length, 0 for a bad kind or face.
int Engine_OracleFormatLine(char kind, const EngineOracleFace *f, char *out, int size);
// The command words of ONE packet (after its tag, count = tag >> 24) to faces:
// polygons 0x20..0x3f, sprites 0x60..0x7f, 0xE1 draw mode (kept in *drawMode,
// 0xffffffff = unknown; a textured polygon sets its page bits as the GPU does),
// NOPs, lines/fills/copies skipped; stops at an unknown code. src, srcId and
// sub are not set. Returns the faces written (at most outMax).
int Engine_OracleDecodePacket(const u32 *words, int count, u32 *drawMode, EngineOracleFace *out, int outMax);
// The packets in [begin, end) (written in order, each with its tag word): one
// R line per face, sub from 0, extra in front of each face's own. Returns the faces.
int Engine_OracleRetailRange(char src, const char *srcId, const void *begin, const void *end, const char *extra);
// The same, and anim=1 on every face equal to the current layout of an animated
// (animtex) table slot of mh, the header retail draws (NULL: no such check).
struct ModelHeader;
int Engine_OracleRetailRangeModel(char src, const char *srcId, const void *begin, const void *end, const char *extra, const struct ModelHeader *mh);
// From the model import (platform/engine_model.c), only with --engine-oracle: one
// byte per mesh triangle (in index order), 1 = from a cycled (animtex) slot.
// Copied; forgotten at level unload. A mesh with none needs no call.
void Engine_OracleNoteMeshAnimated(int mesh, const u8 *triangleAnimated, u32 triangles);
// The page, clut and blend of an engine material (VRAM_TABLE: the material words
// as they are and table=1; NATIVE: untextured with tex=native).
void Engine_OracleFaceFromMaterial(const EngineMaterial *m, EngineOracleFace *f);
// E lines of one engine draw: one per triangle (mesh frame or dynamic vertices,
// materialOverride honoured, uv rounded to texels); an external mesh one per
// range with uv '-'. Returns the lines.
int Engine_OracleEngineDraw(char src, const char *srcId, int view, const EngineDraw *draw, const char *extra);
// Source context: between Begin and End every successful Engine_RenderSubmitLinked
// buffers its faces (Engine_OracleNoteSubmit); End writes them with the letter
// the route decided. No open source: a submit writes nothing.
void Engine_OracleSourceBegin(const char *srcId);
void Engine_OracleSourceEnd(char src, const char *extra);
void Engine_OracleNoteSubmit(int view, const EngineDraw *draw); // from Engine_RenderSubmitLinked
// The id ("<model>#<InstDef index>" for level instances, "#r<pool slot>" for
// runtime ones) and the extra of the R lines (lod=<header>, view=<v> for v > 0)
// and of the E lines (view) of an instance in a view.
struct Instance;
struct InstDrawPerPlayer;
struct PushBuffer;
// *src: 'D' for a driver's body (srcId = its seat), else 'I'.
void Engine_OracleInstanceTags(const struct Instance *inst, const struct InstDrawPerPlayer *idpp, const struct PushBuffer *pb, char *src, char *id, int idSize,
                               char *retailExtra, int retailSize, char *engineExtra, int engineSize);
// From Engine_DriverCmdlRoute when the hi-res driver drew the body: the open source gets hires=1.
void Engine_OracleNoteHires(void);
// The H line's switches: the command line without the program name (main.c, optional).
void Engine_OracleSetCommandLine(int argc, char **argv);
// Sums since the start; all 0 without --engine-oracle (selftest, D-W1.10).
void Engine_OracleCountersGet(u32 *frames, u32 *linesR, u32 *linesE, u32 *dumps);
// VRAM writes of the game (platform/native_libgpu.c, only with --engine-oracle):
// kind 0 LoadImage, 1 MoveImage, 2 ClearImage; x, y, w, h the rectangle written
// (MoveImage: the destination), fromX/fromY the source of a MoveImage (-1 else).
// Counted per tour entry; in race frames one log line per call (capped, see
// ENGINE_ORACLE_VRAM_LINES_*), and one summary line per entry.
void Engine_OracleNoteVramWrite(int kind, int x, int y, int w, int h, int fromX, int fromY);
#define ENGINE_ORACLE_VRAM_LINES_ENTRY 64 // per tour entry
#define ENGINE_ORACLE_VRAM_LINES_RUN 2048 // per run
// The colour the "ne" program modulates a vertex with, as 8-bit channels: the
// vertex colour times the material tint times the draw tint
// (platform/native_shaders.inc v_color = a_color * tint; engine_render.c
// u.tint = material->tint * draw->tint), rounded and held to 0..255. Pure.
void Engine_OracleModulatedColour(const u8 colour[4], const float materialTint[4], const float drawTint[4], u8 out[3]);
void Engine_OracleLoadStage(void); // from Engine_RenderUploadMeshes: registers the tour report once
// GTE registers and scratchpad into slot 0/1 and back (the retail capture of
// game/RenderBucket/RenderBucket_QueueExecute.c leaves both as they were).
void Engine_OracleStateSave(int slot);
void Engine_OracleStateRestore(int slot);
// W3: the material index drawn with the wrong clut (-1 = none) and the faulted copy the renderer binds.
extern int g_engineOracleFaultMaterial;
const EngineMaterial *Engine_OracleFaultMaterial(void);
int Engine_SelfTestOracle(int *checks); // the packet decoder on synthetic primitives
// ENGINE ORACLE (tour) - the oracle frames of the material comparison in the tour
// (round of 2026-10-16, W2; game/DebugMenu.c, platform/engine_tour.c,
// platform/engine_menuscene.c; switches in main.c). The tour calls
// Engine_OracleArmFrame(tag, race frame) in the game logic of a race frame,
// before that frame is drawn; the menu scene calls Engine_OracleArm from its
// collect, before its draw and before the retail instances of the frame are
// queued. Only with --level-tour-oracle.
#define ENGINE_TOUR_TEXT_MAX 512       // the texts below (game/DebugMenu.c defines them with 512)
#define ENGINE_TOUR_ORACLE_MAX 64      // entries of --level-tour-oracle
#define ENGINE_TOUR_SEE_MAX 8          // entries of --level-tour-oracle-see
#define ENGINE_TOUR_SEE_NAMES 3        // model names per entry (game/DebugMenu.c: DBG_TOUR_SEE_NAMES)
#define ENGINE_TOUR_SEE_NAME_BYTES 16  // a model name and its NUL (MODEL_NAME_BYTE_COUNT)
extern char g_cfg_levelTourOracle[ENGINE_TOUR_TEXT_MAX];     // --level-tour-oracle as text; empty: no oracle frames
extern char g_cfg_levelTourOracleSee[ENGINE_TOUR_TEXT_MAX];  // --level-tour-oracle-see as text
extern char g_cfg_levelTourFramesText[ENGINE_TOUR_TEXT_MAX]; // --level-tour-frames as given: one count, L<level>@<count> overrides
int EngineTour_OracleAt(const int *levels, const int *frames, int count, int level, int raceFrame);
int EngineTour_OracleTag(char *out, size_t size, int entry, int level, const char *kind); // t<NN>-L<level>-<kind>
int EngineTour_OracleShotName(char *out, size_t size, const char *shotName, const char *tag);
int EngineTour_LineSoon(s32 remaining, s32 step, int frames);
int EngineTour_ParseSee(const char *text, int *levels, char (*names)[ENGINE_TOUR_SEE_NAMES][ENGINE_TOUR_SEE_NAME_BYTES], int *counts, int max, int *badAt);
int EngineTour_DriveForwardFolds(void);
int EngineTour_ParseShots(const char *text, int *levels, int *frames, int max, int *badAt);
int EngineTour_ParseFrames(const char *text, int *levels, int *frames, int max, int *badAt);
int EngineTour_FramesForLevel(const int *levels, const int *frames, int count, int level, int fallback);

// --------------------------------------------------------------------------
// ENGINE FX WEATHER (platform/engine_fx_weather.c; day round of 2026-10-16)
// The track's rain and snow (RenderWeather), the red beaker's cloud rain
// (RedBeaker_RenderRain) and the confetti (DrawConfetti) drawn by the engine as
// world-space lines and quads with a depth test, in a frame in which the engine
// draws the track; the heat shimmer (Torch_Main) stays on the PS1 path (parked:
// it reads the frame buffer back) and is counted. The game files declare the
// hooks themselves (CTR_NATIVE). The drawer writes its state, its primitive and
// advances its prim cursor as always; a hook answering 1 means retail does not
// link that primitive. Begin opens one batch per drawer call (per beaker pass),
// End submits it before the drawer links its draw-mode packet - or, when the
// engine cannot draw it, links the batch's packets as retail would have.
// Without --engine nothing is called.
void EngineFx_WeatherBegin(const struct PushBuffer *pb, u32 drawMode, u32 *otCell);
void EngineFx_BeakerRainBegin(const struct PushBuffer *pb, s32 trX, s32 trY, s32 trZ, u32 drawMode, u32 *otCell);
void EngineFx_ConfettiBegin(const struct PushBuffer *pb);
// A LINE_G2 (packet) of the open weather or beaker batch: its two GTE vectors
// (VXY, VZ) and its two colour words. 1 = taken, 0 = retail links it.
int EngineFx_WeatherLine(void *packet, u32 xy0, u32 z0, u32 xy1, u32 z1, u32 color0, u32 color1);
// A POLY_F4 (packet) of the open confetti batch with the cell retail links it
// into, its four GTE vectors and its colour word. 1 = taken, 0 = retail links it.
int EngineFx_ConfettiQuad(void *packet, u32 *otCell, const u32 xy[4], const s32 z[4], u32 color);
void EngineFx_WeatherEnd(void);
// One heat particle the PS1 path draws (counted as left to retail in a frame
// with the engine track).
void EngineFx_HeatNote(void);
int Engine_SelfTestWeather(int *checks); // engine_fx_weather.c: rule, geometry, line and quad corners

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
