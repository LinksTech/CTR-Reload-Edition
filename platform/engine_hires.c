#include <platform/engine.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ENGINE HIRES - a hi-res driver, PROTOTYPE (round of 2026-10-15, stage H). See
// include/platform/engine.h, HI-RES DRIVER.
//
// WHAT IT PROVES: the engine can draw a driver with a real RGBA texture at full
// resolution (no VRAM palette, the mip chain of Engine_TextureCreate), real
// 3D wheels that spin with the speed, steer and follow the squash of the kart,
// all from game state. Only behind --engine --engine-hires-dir <dir> (--dev).
// The .rldchar format is NOT touched: the data comes from a local folder in a
// dev format of its own (written by tools/hires_testdriver.py, one synthetic
// test driver):
//   body.hrm, wheel.hrm  "HRM1", u32 vertexCount, u32 indexCount, then the
//                        vertices as 24-byte records (position 3 x f32,
//                        texcoord 2 x f32 0..1, colour 4 x u8 - the layout of
//                        struct EngineVertex before its UV box; the reader
//                        gives every vertex the whole page as box, a native
//                        texture reads none), then u16 indices (triangles)
//   skin.hrt             "HRT1", u32 width, u32 height, RGBA8 rows from the top,
//                        sRGB colour (both edges a power of two in 16..2048)
// Model space of both meshes: x the kart's left, y up, z front, origin on the
// ground under the kart; units are the kart model units of
// Engine_InstanceWorldMatrix (world = t + R * scale / 4096 * p): the wheel
// middles of game/DrawTires.c lie at (+-36, 16, 49.75) and (+-36, 16, -24), a
// wheel is 16 units in radius.
//
// LOAD (stage 5, from Engine_DriverCmdlLoad, every level incl. the main menu):
// the files are read and the texture made ONCE per run (static host memory, a
// texture of Engine_TextureCreate with its levels, trilinear and anisotropic;
// --engine-hires-tex64
// makes it from a 64 x 64 box reduction of the file instead - the comparison
// run of H1); the two engine meshes and the material are made at every load
// (the renderer's level unload resets the tables).
//
// THE SEAT: ENG_HIRES_SEAT. Its driver is owned (Engine_DriverCmdlOwns answers 1,
// so Engine_FxOwnsDriver too: shadow, exhaust, flames and skids are the engine's)
// unless the seat has a native set the engine draws (a CNET custom driver: the
// layer's own route, hi-res off for the seat, logged at load).
//   BODY   Engine_HiresRoute, called first in Engine_DriverCmdlRoute: the body
//          instance of the seat (gGT->drivers[seat]->instSelf, its thread's
//          object is the driver) goes through Engine_InstanceSubmit with the
//          hi-res body mesh, frame 0 - the world matrix of the instance (with
//          the squash and stretch of inst->scale), its water line and mirror,
//          the depth cue tint of the CMDL route (EngCmdl_Tints).
//   WHEELS Engine_HiresWheels, called first in EngineFx_Tires, i.e. where the
//          game would write the seat's tire sprites, after every LOD and hiding
//          check of DrawTires.c: four draws of the wheel mesh, no sprite. The
//          frame of each wheel comes only from game state:
//            middle  t + R * ((inst->scale * (+-0x90, 0x40, 0xc7 | -0x60)) >> 12) / 4,
//                    the integer rule of DrawTires_BuildWheelLocalPairs
//                    (engine_fx_tires.c, "wheel middles") - the SPRING: the
//                    middle follows inst->scale.y, which VehPhysForce.c:739/828/835
//                    writes from driver->jumpSquishStretch;
//            steer   front wheels yaw by wheelRotation << 2 game angle units
//                    (4096 a turn), DrawTires.c rim rule, as native_wheels.c;
//            wobble  hazardTimer, the rim rule of DrawTires.c (native_wheels.c);
//            roll    integrated per logic tick from driver->speedApprox: the
//                    way of a tick is speedApprox * elapsedTimeMS / 32 / 256
//                    world units (native_wheels.c head, VehPhysCrash.c:87-92),
//                    the step that way over the radius;
//            size    the mesh scaled by wheelSize / 4096: radius 16 units ->
//                    wheelSize / 256 world units, the retail wheel size.
//          The mirror pass (DrawTires_Reflection) draws the four wheels
//          reflected about the instance's plane y = vertSplit with the skip rule
//          of engine_fx_tires.c.
//   LOG    one line per logic tick of the seat, from the solid pass of view 0
//          ("[CTR Engine] hires frame: ..."): the state read, the roll and step,
//          and what was DRAWN, read back from the finished wheel matrices (the
//          front axle's yaw, the wheel middle's height in kart space), plus the
//          screen box of the body in view 0 (fractions of the view) for the crop
//          of the measurements. Main checks |omega * r - v| / v from roll and
//          posCurr, and drawn = state from the pairs of the line.
//
// MENU SCENE (--engine-menuscene): Engine_MenuSceneCollect draws the hi-res body
// and its four wheels at rest instead of the roster's set (engine_menuscene.c).
//
// ANIMATION (--engine-hires-anim, round of 2026-10-16, Z4, prototype): the
// body becomes an animated one from the same folder (anim.hra, anim.hrt of
// tools/hires_blender_driver.py - a Blender rig baked to pose frames, as the
// stage-H analysis proposed: poses as mesh frames, no skeleton at run time, no
// shader change). anim.hra "HRA1": u32 vertexCount, indexCount, frameCount, clipCount;
// clipCount x {char name[16], u32 first, u32 count}; texcoords (2 f32 per
// vertex, v = 0 the top row); frameCount x vertexCount x {3 f32 position, 4 u8
// colour}; u16 indices. Its own layout, converted field by field into
// EngineVertex (EngHires_ParseAnim), so it does not depend on the size of
// EngineVertex. Clips: steer (odd count, first = full right, last = full left),
// air, drift_l, drift_r. The frame is picked per draw from game state, read
// only (EngHires_AnimPick, the counters of EngHires_AnimTick, one per logic tick):
//   air    ACTION_TOUCH_GROUND clear for ENG_HIRES_AIR_DELAY ticks or more
//          (bumps stay on the ground); then one frame per tick, held at the last
//   drift  kartState == KS_DRIFTING (players and bots, BOTS.c:2210); the side
//          is taken at the first drift tick from the first non-zero sign of
//          multDrift, simpTurnState, wheelRotation (positive = left,
//          game/Vehicle/VehPhysProc.c:2091-2099);
//          one frame per tick into the drift, held at the last
//   steer  otherwise: wheelRotation (the front wheels' value, +-0x40 a full
//          lock, clamped; bots reach about 0x55) to the nearest steer frame
// One log line per tick ("hires anim: ...") with the state read and the frame
// drawn. With --shot-name (as the oracle's snapshots) the series for the eye:
// a snapshot <shot-name>-<tag>-<vblank>.bmp when a pose is reached in full -
// tags steer-l / steer-r (a full lock), air, drift-l, drift-r (the clip's last
// frame) - at most ENG_HIRES_SHOTS_PER_TAG per tag, ENG_HIRES_SHOT_GAP ticks
// apart, one per frame. The wheels, the matrix animations (wheelie, jump arc,
// tumble) and the squash of inst->scale stay as before. The menu scene keeps
// the still body.
//
// DRAW TIME ONLY: nothing of the game is written; host memory of this file only.

char g_cfg_engineHiresDir[ENGINE_HIRES_DIR_MAX] = "";
int g_cfg_engineHiresTex64 = 0;
int g_cfg_engineHiresTexCycle = 0;
int g_cfg_engineHiresAnim = 0;

#define ENG_HIRES_SEAT 0
#define ENG_HIRES_SMALL 64
// One vertex record of an HRM1 file: position, texcoord, colour (no UV box).
#define ENG_HIRES_FILE_VERTEX_BYTES 24u
#define ENG_HIRES_TWO_PI 6.283185307179586
#define ENG_HIRES_ANGLE (ENG_HIRES_TWO_PI / 4096.0) // one game angle unit in radians
#define ENG_HIRES_WHEEL_RADIUS 16.0                 // model units of the wheel mesh
#define ENG_HIRES_CLIP_STEER 0
#define ENG_HIRES_CLIP_AIR 1
#define ENG_HIRES_CLIP_DRIFT_L 2
#define ENG_HIRES_CLIP_DRIFT_R 3
#define ENG_HIRES_CLIPS 4
#define ENG_HIRES_AIR_DELAY 3     // ticks off the ground before the air clip starts
#define ENG_HIRES_STEER_FULL 64   // wheelRotation of a full lock (VEH_PHYS_PROC_WHEEL_ROTATION_STRENGTH)
#define ENG_HIRES_TICKS_CAP 10000 // counters stop here
#define ENG_HIRES_SHOT_TAGS 5
#define ENG_HIRES_SHOTS_PER_TAG 2
#define ENG_HIRES_SHOT_GAP 90     // ticks between two snapshots of one tag

extern char g_cfg_shotName[PLATFORM_ARG_PATH_MAX]; // --shot-name, native_platform.c
void Platform_ShotRequest(const char *path);       // native_platform.c

typedef struct
{
	EngineVertex *vertices;
	u16 *indices;
	u32 vertexCount;
	u32 indexCount;
	float hullMin[3];
	float hullMax[3];
} EngHiresMeshData;

// The animated body of anim.hra (EngHires_ParseAnim).
typedef struct
{
	EngineVertex *vertices; // frames * vertexCount
	u16 *indices;
	u32 vertexCount;
	u32 indexCount;
	u32 frames;
	u32 clipFirst[ENG_HIRES_CLIPS];
	u32 clipCount[ENG_HIRES_CLIPS]; // 0 = the clip is not in the file
	float hullMin[3];               // over all frames
	float hullMax[3];
} EngHiresAnimData;

typedef struct
{
	int tried;       // the files were read (once per run)
	int ok;          // files and texture fine
	EngHiresMeshData body;
	EngHiresMeshData wheel;
	u32 texture;     // Engine_TextureCreate, 0xffffffff = none
	int texW, texH;  // of the texture made (64 x 64 with --engine-hires-tex64)
	int fileW, fileH;
	// per load
	int bodyMesh;
	int wheelMesh;
	int material;
	int seatOff;     // the seat has a native set: hi-res off in this level
	EngineRange bodyRange;
	EngineRange wheelRange;
	// the wheel pose of the seat (one entry: the seat's driver and instance)
	const struct Driver *poseDriver;
	const struct Instance *poseInst;
	u32 poseTimer;
	int poseValid;
	u32 poseTicks;
	double roll;
	double step;
	// the log
	u32 logTimer;
	int logValid;
	// --engine-hires-texcycle: the textures full (= texture), 64 x 64 and key,
	// their materials per load, the variant logged last (0 none, else variant + 1)
	u32 cycleTex[3];
	int cycleMaterial[3];
	int cycleLogged;
	// --engine-hires-anim: the data (once per run), its texture, per load its
	// mesh and material; the tick counters of the seat; the log
	EngHiresAnimData anim;
	int animOk;
	u32 animTexture;
	int animMesh;
	int animMaterial;
	EngineRange animRange;
	const struct Driver *animDriver;
	const struct Instance *animInst;
	u32 animTimer;
	int animValid;
	int airTicks;
	int driftTicks;
	int driftSide; // +1 left, -1 right
	u32 animLogTimer;
	int animLogValid;
	u32 animDraws[ENG_HIRES_CLIPS];
	u32 animLines;
	u32 shotCount[ENG_HIRES_SHOT_TAGS];
	u32 shotTimer[ENG_HIRES_SHOT_TAGS];
	// counters (exit report)
	u32 bodyDrawn, bodyDeclined, wheelDraws, wheelSets, mirrorWheelDraws, spriteFallbacks, menuFrames, logLines;
	int reportOn;
} EngHiresState;

internal EngHiresState s_engHires;

// ---------------------------------------------------------------------------
// The dev files (pure parsers: the selftest feeds them made-up bytes).

internal u32 EngHires_U32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

internal void EngHires_FreeMesh(EngHiresMeshData *m)
{
	free(m->vertices);
	free(m->indices);
	memset(m, 0, sizeof(*m));
}

// 1 = out holds copies of the vertices and indices (free with EngHires_FreeMesh).
// 0 = refused, *why says why.
internal int EngHires_ParseMesh(const u8 *bytes, size_t size, EngHiresMeshData *out, const char **why)
{
	u32 vc;
	u32 ic;
	u32 i;
	int c;

	memset(out, 0, sizeof(*out));
	if ((bytes == NULL) || (size < 12u) || (memcmp(bytes, "HRM1", 4) != 0))
	{
		*why = "not an HRM1 file";
		return 0;
	}
	vc = EngHires_U32(bytes + 4);
	ic = EngHires_U32(bytes + 8);
	if ((vc == 0u) || (vc > 65535u) || (ic == 0u) || ((ic % 3u) != 0u) || (ic > 3u * 1000000u))
	{
		*why = "vertex or index count out of range";
		return 0;
	}
	if (size != (12u + ((size_t)vc * ENG_HIRES_FILE_VERTEX_BYTES) + ((size_t)ic * 2u)))
	{
		*why = "size does not match the counts";
		return 0;
	}
	out->vertices = (EngineVertex *)malloc((size_t)vc * sizeof(EngineVertex));
	out->indices = (u16 *)malloc((size_t)ic * sizeof(u16));
	if ((out->vertices == NULL) || (out->indices == NULL))
	{
		EngHires_FreeMesh(out);
		*why = "no host memory";
		return 0;
	}
	// The file keeps the 24-byte records of the dev format; each becomes an
	// engine vertex with the whole page as UV box (engine.h, THE UV BOX).
	for (i = 0; i < vc; i++)
	{
		const u8 *r = bytes + 12 + ((size_t)i * ENG_HIRES_FILE_VERTEX_BYTES);
		EngineVertex *v = &out->vertices[i];

		memcpy(v->position, r, sizeof(v->position));
		memcpy(v->texcoord, r + 12, sizeof(v->texcoord));
		memcpy(v->color, r + 20, sizeof(v->color));
		Engine_VertexBoxFull(v, 1);
	}
	memcpy(out->indices, bytes + 12 + ((size_t)vc * ENG_HIRES_FILE_VERTEX_BYTES), (size_t)ic * 2u);
	out->vertexCount = vc;
	out->indexCount = ic;
	for (i = 0; i < ic; i++)
	{
		if ((u32)out->indices[i] >= vc)
		{
			EngHires_FreeMesh(out);
			*why = "index out of range";
			return 0;
		}
	}
	for (c = 0; c < 3; c++)
	{
		out->hullMin[c] = out->vertices[0].position[c];
		out->hullMax[c] = out->vertices[0].position[c];
	}
	for (i = 0; i < vc; i++)
	{
		const EngineVertex *v = &out->vertices[i];

		for (c = 0; c < 3; c++)
		{
			if (!(v->position[c] == v->position[c]) || (fabsf(v->position[c]) > 100000.0f))
			{
				EngHires_FreeMesh(out);
				*why = "position not finite";
				return 0;
			}
			out->hullMin[c] = (v->position[c] < out->hullMin[c]) ? v->position[c] : out->hullMin[c];
			out->hullMax[c] = (v->position[c] > out->hullMax[c]) ? v->position[c] : out->hullMax[c];
		}
		if (!(v->texcoord[0] == v->texcoord[0]) || !(v->texcoord[1] == v->texcoord[1]))
		{
			EngHires_FreeMesh(out);
			*why = "texcoord not finite";
			return 0;
		}
	}
	*why = "";
	return 1;
}

// 1 = *rgba points into bytes (w x h RGBA8). Edges as the texture manager takes them.
internal int EngHires_ParseTexture(const u8 *bytes, size_t size, int *w, int *h, const u8 **rgba, const char **why)
{
	u32 tw;
	u32 th;

	if ((bytes == NULL) || (size < 12u) || (memcmp(bytes, "HRT1", 4) != 0))
	{
		*why = "not an HRT1 file";
		return 0;
	}
	tw = EngHires_U32(bytes + 4);
	th = EngHires_U32(bytes + 8);
	if ((tw > 4096u) || (th > 4096u) || (NativeTex_CheckEdges((int)tw, (int)th, 0) != NATIVE_TEX_OK))
	{
		*why = "edges not a power of two in 16..2048";
		return 0;
	}
	if (size != (12u + ((size_t)tw * th * 4u)))
	{
		*why = "size does not match the edges";
		return 0;
	}
	*w = (int)tw;
	*h = (int)th;
	*rgba = bytes + 12;
	*why = "";
	return 1;
}

internal void EngHires_FreeAnim(EngHiresAnimData *a)
{
	free(a->vertices);
	free(a->indices);
	memset(a, 0, sizeof(*a));
}

internal float EngHires_F32(const u8 *p)
{
	const u32 bits = EngHires_U32(p);
	float f;

	memcpy(&f, &bits, sizeof(f));
	return f;
}

// One vertex of the engine from the file's fields (by name: independent of the
// size and the other fields of EngineVertex, which stay zero).
internal void EngHires_AnimVertex(EngineVertex *v, const u8 *pos, const u8 *uv)
{
	memset(v, 0, sizeof(*v));
	v->position[0] = EngHires_F32(pos + 0);
	v->position[1] = EngHires_F32(pos + 4);
	v->position[2] = EngHires_F32(pos + 8);
	v->color[0] = pos[12];
	v->color[1] = pos[13];
	v->color[2] = pos[14];
	v->color[3] = pos[15];
	v->texcoord[0] = EngHires_F32(uv + 0);
	v->texcoord[1] = EngHires_F32(uv + 4);
}

// 1 = out holds the frames as engine vertices and the indices (free with
// EngHires_FreeAnim). 0 = refused, *why says why. Pure: the selftest feeds it
// made-up bytes.
internal int EngHires_ParseAnim(const u8 *bytes, size_t size, EngHiresAnimData *out, const char **why)
{
	static const char *const names[ENG_HIRES_CLIPS] = {"steer", "air", "drift_l", "drift_r"};
	u32 vc;
	u32 ic;
	u32 fc;
	u32 cc;
	u32 i;
	u32 f;
	size_t need;
	const u8 *uvs;
	const u8 *frames;
	const u8 *idx;
	int c;

	memset(out, 0, sizeof(*out));
	if ((bytes == NULL) || (size < 20u) || (memcmp(bytes, "HRA1", 4) != 0))
	{
		*why = "not an HRA1 file";
		return 0;
	}
	vc = EngHires_U32(bytes + 4);
	ic = EngHires_U32(bytes + 8);
	fc = EngHires_U32(bytes + 12);
	cc = EngHires_U32(bytes + 16);
	if ((vc == 0u) || (vc > 65535u) || (ic == 0u) || ((ic % 3u) != 0u) || (ic > 600000u) || (fc == 0u) || (fc > 256u) || (cc == 0u) || (cc > 16u))
	{
		*why = "a count out of range";
		return 0;
	}
	need = 20u + ((size_t)cc * 24u) + ((size_t)vc * 8u) + ((size_t)vc * fc * 16u) + ((size_t)ic * 2u);
	if (size != need)
	{
		*why = "size does not match the counts";
		return 0;
	}
	for (i = 0; i < cc; i++)
	{
		const u8 *clip = bytes + 20 + (i * 24u);
		const u32 first = EngHires_U32(clip + 16);
		const u32 count = EngHires_U32(clip + 20);

		if (memchr(clip, 0, 16) == NULL)
		{
			*why = "a clip name without its end";
			return 0;
		}
		if ((count == 0u) || (first >= fc) || (count > (fc - first)))
		{
			*why = "a clip outside the frames";
			return 0;
		}
		for (c = 0; c < ENG_HIRES_CLIPS; c++)
		{
			if (strcmp((const char *)clip, names[c]) == 0)
			{
				out->clipFirst[c] = first;
				out->clipCount[c] = count;
			}
		}
	}
	if ((out->clipCount[ENG_HIRES_CLIP_STEER] < 3u) || ((out->clipCount[ENG_HIRES_CLIP_STEER] % 2u) == 0u))
	{
		memset(out, 0, sizeof(*out));
		*why = "no steer clip of an odd count of 3 or more";
		return 0;
	}
	uvs = bytes + 20 + ((size_t)cc * 24u);
	frames = uvs + ((size_t)vc * 8u);
	idx = frames + ((size_t)vc * fc * 16u);
	for (i = 0; i < vc; i++)
	{
		const float u = EngHires_F32(uvs + (i * 8u));
		const float v = EngHires_F32(uvs + (i * 8u) + 4u);

		if (!(u >= 0.0f) || !(u <= 1.0f) || !(v >= 0.0f) || !(v <= 1.0f))
		{
			memset(out, 0, sizeof(*out));
			*why = "a texcoord outside 0..1";
			return 0;
		}
	}
	for (i = 0; i < ic; i++)
	{
		if ((u32)(idx[i * 2u] | (idx[i * 2u + 1u] << 8)) >= vc)
		{
			memset(out, 0, sizeof(*out));
			*why = "index out of range";
			return 0;
		}
	}
	out->vertices = (EngineVertex *)malloc((size_t)vc * fc * sizeof(EngineVertex));
	out->indices = (u16 *)malloc((size_t)ic * sizeof(u16));
	if ((out->vertices == NULL) || (out->indices == NULL))
	{
		EngHires_FreeAnim(out);
		*why = "no host memory";
		return 0;
	}
	for (i = 0; i < ic; i++)
	{
		out->indices[i] = (u16)(idx[i * 2u] | (idx[i * 2u + 1u] << 8));
	}
	for (f = 0; f < fc; f++)
	{
		for (i = 0; i < vc; i++)
		{
			EngineVertex *v = &out->vertices[((size_t)f * vc) + i];

			EngHires_AnimVertex(v, frames + ((((size_t)f * vc) + i) * 16u), uvs + (i * 8u));
			for (c = 0; c < 3; c++)
			{
				if (!(v->position[c] == v->position[c]) || (fabsf(v->position[c]) > 100000.0f))
				{
					EngHires_FreeAnim(out);
					*why = "position not finite";
					return 0;
				}
				if (((f == 0u) && (i == 0u)) || (v->position[c] < out->hullMin[c]))
				{
					out->hullMin[c] = v->position[c];
				}
				if (((f == 0u) && (i == 0u)) || (v->position[c] > out->hullMax[c]))
				{
					out->hullMax[c] = v->position[c];
				}
			}
		}
	}
	out->vertexCount = vc;
	out->indexCount = ic;
	out->frames = fc;
	*why = "";
	return 1;
}

// The side of a drift from its first tick: the first non-zero sign of
// multDrift, simpTurnState, wheelRotation (positive = left); left when all are 0.
internal int EngHires_DriftSide(int multDrift, int simpTurnState, int wheelRotation)
{
	if (multDrift != 0)
	{
		return (multDrift > 0) ? 1 : -1;
	}
	if (simpTurnState != 0)
	{
		return (simpTurnState > 0) ? 1 : -1;
	}
	return (wheelRotation < 0) ? -1 : 1;
}

// The file frame to draw (pure): air before drift before steer (see the head
// of the file); *clip names the clip.
internal u32 EngHires_AnimPick(const EngHiresAnimData *a, int steerRaw, int airTicks, int driftTicks, int driftSide, int *clip)
{
	int steer = steerRaw;
	int n;

	if ((a->clipCount[ENG_HIRES_CLIP_AIR] > 0u) && (airTicks >= ENG_HIRES_AIR_DELAY))
	{
		const u32 i = (u32)(airTicks - ENG_HIRES_AIR_DELAY);

		*clip = ENG_HIRES_CLIP_AIR;
		return a->clipFirst[ENG_HIRES_CLIP_AIR] + ((i < a->clipCount[ENG_HIRES_CLIP_AIR]) ? i : (a->clipCount[ENG_HIRES_CLIP_AIR] - 1u));
	}
	if (driftTicks > 0)
	{
		const int c = (driftSide >= 0) ? ENG_HIRES_CLIP_DRIFT_L : ENG_HIRES_CLIP_DRIFT_R;

		if (a->clipCount[c] > 0u)
		{
			const u32 i = (u32)(driftTicks - 1);

			*clip = c;
			return a->clipFirst[c] + ((i < a->clipCount[c]) ? i : (a->clipCount[c] - 1u));
		}
	}
	steer = (steer > ENG_HIRES_STEER_FULL) ? ENG_HIRES_STEER_FULL : ((steer < -ENG_HIRES_STEER_FULL) ? -ENG_HIRES_STEER_FULL : steer);
	n = (int)a->clipCount[ENG_HIRES_CLIP_STEER];
	*clip = ENG_HIRES_CLIP_STEER;
	// round((steer + FULL) / (2 FULL) * (n - 1)), half up
	return a->clipFirst[ENG_HIRES_CLIP_STEER] + (u32)((((steer + ENG_HIRES_STEER_FULL) * (n - 1) * 2) + (2 * ENG_HIRES_STEER_FULL)) / (4 * ENG_HIRES_STEER_FULL));
}

// A box reduction to ow x oh (edges dividing w and h): the mean of each block,
// rounded half up, in the stored (sRGB) values - a stand-in for a small
// texture of the same image, nothing finer.
internal int EngHires_Reduce(const u8 *src, int w, int h, int ow, int oh, u8 *dst)
{
	const int bx = (ow > 0) ? (w / ow) : 0;
	const int by = (oh > 0) ? (h / oh) : 0;
	int x;
	int y;

	if ((bx <= 0) || (by <= 0) || ((bx * ow) != w) || ((by * oh) != h))
	{
		return 0;
	}
	for (y = 0; y < oh; y++)
	{
		for (x = 0; x < ow; x++)
		{
			u32 sum[4] = {0, 0, 0, 0};
			const u32 n = (u32)(bx * by);
			int i;
			int j;
			int c;

			for (j = 0; j < by; j++)
			{
				const u8 *row = src + ((((size_t)(y * by + j) * (size_t)w) + (size_t)(x * bx)) * 4u);

				for (i = 0; i < bx; i++)
				{
					for (c = 0; c < 4; c++)
					{
						sum[c] += row[i * 4 + c];
					}
				}
			}
			for (c = 0; c < 4; c++)
			{
				dst[(((size_t)y * (size_t)ow) + (size_t)x) * 4u + (size_t)c] = (u8)((sum[c] + n / 2u) / n);
			}
		}
	}
	return 1;
}

internal u8 *EngHires_ReadFile(const char *dir, const char *name, size_t *size)
{
	char path[ENGINE_HIRES_DIR_MAX + 32];
	FILE *f;
	long length;
	u8 *bytes;

	*size = 0;
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	f = fopen(path, "rb");
	if (f == NULL)
	{
		return NULL;
	}
	if ((fseek(f, 0, SEEK_END) != 0) || ((length = ftell(f)) <= 0) || (length > (64L << 20)) || (fseek(f, 0, SEEK_SET) != 0))
	{
		fclose(f);
		return NULL;
	}
	bytes = (u8 *)malloc((size_t)length);
	if ((bytes != NULL) && (fread(bytes, 1, (size_t)length, f) != (size_t)length))
	{
		free(bytes);
		bytes = NULL;
	}
	fclose(f);
	if (bytes != NULL)
	{
		*size = (size_t)length;
	}
	return bytes;
}

internal void EngHires_Report(void)
{
	if (!g_cfg_engine || (g_cfg_engineHiresDir[0] == '\0'))
	{
		return;
	}
	Platform_Log("[CTR Engine] hires at exit: body drawn %u, declined %u; wheel mesh draws %u in %u set(s), mirrored %u; wheel sprites of the hires seat %u; "
	             "menu frames %u; frame lines %u\n",
	             s_engHires.bodyDrawn, s_engHires.bodyDeclined, s_engHires.wheelDraws, s_engHires.wheelSets, s_engHires.mirrorWheelDraws,
	             s_engHires.spriteFallbacks, s_engHires.menuFrames, s_engHires.logLines);
	if (g_cfg_engineHiresAnim)
	{
		Platform_Log("[CTR Engine] hires anim at exit: %s; body draws by clip - steer %u, air %u, drift_l %u, drift_r %u; anim lines %u\n",
		             s_engHires.animOk ? "loaded" : "not loaded", s_engHires.animDraws[ENG_HIRES_CLIP_STEER], s_engHires.animDraws[ENG_HIRES_CLIP_AIR],
		             s_engHires.animDraws[ENG_HIRES_CLIP_DRIFT_L], s_engHires.animDraws[ENG_HIRES_CLIP_DRIFT_R], s_engHires.animLines);
	}
}

// One texture of the skin's settings (sRGB, the whole mip chain, trilinear,
// anisotropic, clamped); 0xffffffff when the manager refused it.
internal u32 EngHires_SkinTexture(const u8 *rgba, int w, int h, const char *name)
{
	EngineTextureDesc desc;

	memset(&desc, 0, sizeof(desc));
	desc.width = w;
	desc.height = h;
	desc.rgba = rgba;
	desc.linearData = 0; // sRGB colour
	desc.oneLevel = 0;   // the whole chain, also for the small variants
	desc.filter = ENGINE_TEXTURE_FILTERED;
	desc.maxAnisotropy = 0; // the device limit
	desc.wrapU = NATIVE_GFX_WRAP_CLAMP;
	desc.wrapV = NATIVE_GFX_WRAP_CLAMP;
	desc.name = name;
	return Engine_TextureCreate(&desc);
}

// --engine-hires-texcycle: the 64 x 64 reduction of the file and a flat key
// texture beside the run's texture (variant 0). The key colour is pure magenta,
// a colour the skin does not have; the scene modulates it with the grey vertex
// shade only, so a key pixel is (s, 0, s) - tools/hires_mask_sharp.py finds the
// driver's textured pixels by it. A refusal leaves the cycle off.
internal void EngHires_MakeCycleTextures(const u8 *rgba)
{
	static u8 key[16 * 16 * 4];
	u8 *small = (u8 *)malloc((size_t)ENG_HIRES_SMALL * ENG_HIRES_SMALL * 4u);
	int i;

	s_engHires.cycleTex[0] = s_engHires.texture;
	s_engHires.cycleTex[1] = 0xffffffffu;
	s_engHires.cycleTex[2] = 0xffffffffu;
	for (i = 0; i < 16 * 16; i++)
	{
		key[i * 4 + 0] = 255;
		key[i * 4 + 1] = 0;
		key[i * 4 + 2] = 255;
		key[i * 4 + 3] = 255;
	}
	if ((small != NULL) && EngHires_Reduce(rgba, s_engHires.fileW, s_engHires.fileH, ENG_HIRES_SMALL, ENG_HIRES_SMALL, small))
	{
		s_engHires.cycleTex[1] = EngHires_SkinTexture(small, ENG_HIRES_SMALL, ENG_HIRES_SMALL, "hires driver skin 64 (cycle)");
		s_engHires.cycleTex[2] = EngHires_SkinTexture(key, 16, 16, "hires driver key (cycle)");
	}
	free(small);
	Platform_Log("[CTR Engine] hires texcycle: %s - in the menu scene every %d VBlanks: full, 64x64, key (255,0,255); variant = (vblank / %d) %% 3\n",
	             ((s_engHires.cycleTex[1] != 0xffffffffu) && (s_engHires.cycleTex[2] != 0xffffffffu)) ? "textures made" : "a texture was refused, cycle off",
	             ENGINE_HIRES_CYCLE_VBLANKS, ENGINE_HIRES_CYCLE_VBLANKS);
}

// --engine-hires-anim, once per run after the base files: anim.hra and its
// texture anim.hrt. A refusal is logged and leaves the still body.
internal void EngHires_ReadAnimOnce(void)
{
	const char *why = "";
	size_t animSize = 0;
	size_t texSize = 0;
	u8 *animData = EngHires_ReadFile(g_cfg_engineHiresDir, "anim.hra", &animSize);
	u8 *texData = EngHires_ReadFile(g_cfg_engineHiresDir, "anim.hrt", &texSize);
	const u8 *rgba = NULL;
	int w = 0;
	int h = 0;

	s_engHires.animTexture = 0xffffffffu;
	if ((animData == NULL) || (texData == NULL))
	{
		why = "anim.hra or anim.hrt is missing or unreadable";
	}
	else if (EngHires_ParseAnim(animData, animSize, &s_engHires.anim, &why) && EngHires_ParseTexture(texData, texSize, &w, &h, &rgba, &why))
	{
		s_engHires.animTexture = EngHires_SkinTexture(rgba, w, h, "hires driver anim skin");
		if (s_engHires.animTexture != 0xffffffffu)
		{
			s_engHires.animOk = 1;
		}
		else
		{
			why = "Engine_TextureCreate refused anim.hrt";
		}
	}
	if (s_engHires.animOk)
	{
		const EngHiresAnimData *a = &s_engHires.anim;

		Platform_Log("[CTR Engine] hires anim: loaded %u vertices %u triangles, %u frames (steer %u, air %u, drift_l %u, drift_r %u), hull %.1f..%.1f, "
		             "%.1f..%.1f, %.1f..%.1f, texture %dx%d\n",
		             a->vertexCount, a->indexCount / 3u, a->frames, a->clipCount[ENG_HIRES_CLIP_STEER], a->clipCount[ENG_HIRES_CLIP_AIR],
		             a->clipCount[ENG_HIRES_CLIP_DRIFT_L], a->clipCount[ENG_HIRES_CLIP_DRIFT_R], (double)a->hullMin[0], (double)a->hullMax[0],
		             (double)a->hullMin[1], (double)a->hullMax[1], (double)a->hullMin[2], (double)a->hullMax[2], w, h);
	}
	else
	{
		EngHires_FreeAnim(&s_engHires.anim);
		Platform_Log("[CTR Engine] hires anim: refused (%s), the still body is drawn\n", why);
	}
	free(animData);
	free(texData);
}

// Once per run: the files and the texture. A refusal is logged and turns the
// hi-res driver off for the run.
internal void EngHires_ReadOnce(void)
{
	const char *why = "";
	u8 *bodyData = NULL;
	u8 *wheelData = NULL;
	u8 *texData = NULL;
	u8 *small = NULL;
	size_t bodySize = 0;
	size_t wheelSize = 0;
	size_t texSize = 0;
	const u8 *rgba = NULL;
	EngineTextureDesc desc;

	s_engHires.tried = 1;
	s_engHires.texture = 0xffffffffu;
	bodyData = EngHires_ReadFile(g_cfg_engineHiresDir, "body.hrm", &bodySize);
	wheelData = EngHires_ReadFile(g_cfg_engineHiresDir, "wheel.hrm", &wheelSize);
	texData = EngHires_ReadFile(g_cfg_engineHiresDir, "skin.hrt", &texSize);
	if ((bodyData == NULL) || (wheelData == NULL) || (texData == NULL))
	{
		why = "a file of body.hrm, wheel.hrm, skin.hrt is missing or unreadable";
		goto out;
	}
	if (!EngHires_ParseMesh(bodyData, bodySize, &s_engHires.body, &why) || !EngHires_ParseMesh(wheelData, wheelSize, &s_engHires.wheel, &why) ||
	    !EngHires_ParseTexture(texData, texSize, &s_engHires.fileW, &s_engHires.fileH, &rgba, &why))
	{
		goto out;
	}

	memset(&desc, 0, sizeof(desc));
	desc.width = s_engHires.fileW;
	desc.height = s_engHires.fileH;
	desc.rgba = rgba;
	desc.linearData = 0; // sRGB colour
	desc.oneLevel = 0;   // the whole chain, also for the 64 variant
	desc.filter = ENGINE_TEXTURE_FILTERED;
	desc.maxAnisotropy = 0; // the device limit
	desc.wrapU = NATIVE_GFX_WRAP_CLAMP;
	desc.wrapV = NATIVE_GFX_WRAP_CLAMP;
	desc.name = "hires driver skin";
	if (g_cfg_engineHiresTex64)
	{
		small = (u8 *)malloc((size_t)ENG_HIRES_SMALL * ENG_HIRES_SMALL * 4u);
		if ((small == NULL) || !EngHires_Reduce(rgba, s_engHires.fileW, s_engHires.fileH, ENG_HIRES_SMALL, ENG_HIRES_SMALL, small))
		{
			why = "the 64 x 64 reduction failed";
			goto out;
		}
		desc.width = ENG_HIRES_SMALL;
		desc.height = ENG_HIRES_SMALL;
		desc.rgba = small;
		desc.name = "hires driver skin 64";
	}
	s_engHires.texture = Engine_TextureCreate(&desc);
	if (s_engHires.texture == 0xffffffffu)
	{
		why = "Engine_TextureCreate refused it (see its log line)";
		goto out;
	}
	s_engHires.texW = desc.width;
	s_engHires.texH = desc.height;
	s_engHires.ok = 1;
	if (g_cfg_engineHiresTexCycle)
	{
		EngHires_MakeCycleTextures(rgba);
	}
	if (g_cfg_engineHiresAnim)
	{
		EngHires_ReadAnimOnce();
	}

out:
	if (s_engHires.ok)
	{
		Platform_Log("[CTR Engine] hires: loaded %s: body %u vertices %u triangles (hull %.1f..%.1f, %.1f..%.1f, %.1f..%.1f), wheel %u vertices %u triangles, "
		             "texture %dx%d from %dx%d (%s), %d levels, trilinear\n",
		             g_cfg_engineHiresDir, s_engHires.body.vertexCount, s_engHires.body.indexCount / 3u, (double)s_engHires.body.hullMin[0],
		             (double)s_engHires.body.hullMax[0], (double)s_engHires.body.hullMin[1], (double)s_engHires.body.hullMax[1],
		             (double)s_engHires.body.hullMin[2], (double)s_engHires.body.hullMax[2], s_engHires.wheel.vertexCount, s_engHires.wheel.indexCount / 3u,
		             s_engHires.texW, s_engHires.texH, s_engHires.fileW, s_engHires.fileH, g_cfg_engineHiresTex64 ? "64 variant" : "full", NativeTex_LevelCount(s_engHires.texW, s_engHires.texH));
	}
	else
	{
		EngHires_FreeMesh(&s_engHires.body);
		EngHires_FreeMesh(&s_engHires.wheel);
		Platform_Log("[CTR Engine] hires: %s refused (%s), the hi-res driver is off for this run\n", g_cfg_engineHiresDir, why);
	}
	free(small);
	free(bodyData);
	free(wheelData);
	free(texData); // the manager made its own copy of the levels
}

// ---------------------------------------------------------------------------
// Load (stage 5) and the questions of the other files.

void Engine_HiresLoad(void)
{
	EngineMaterial mat;
	EngineMeshDesc desc;
	const int set = Engine_DriverSetOfSeat(ENG_HIRES_SEAT);
	int variant;

	s_engHires.bodyMesh = -1;
	s_engHires.wheelMesh = -1;
	s_engHires.material = -1;
	s_engHires.cycleMaterial[0] = s_engHires.cycleMaterial[1] = s_engHires.cycleMaterial[2] = -1;
	s_engHires.animMesh = -1;
	s_engHires.animMaterial = -1;
	s_engHires.animValid = 0;
	s_engHires.animLogValid = 0;
	s_engHires.seatOff = 0;
	s_engHires.poseValid = 0;
	s_engHires.logValid = 0;
	if (!g_cfg_engine || (g_cfg_engineHiresDir[0] == '\0'))
	{
		return;
	}
	if (!s_engHires.reportOn)
	{
		s_engHires.reportOn = 1;
		Platform_AtExitReport(EngHires_Report);
	}
	if (!s_engHires.tried)
	{
		EngHires_ReadOnce();
	}
	if (!s_engHires.ok)
	{
		return;
	}

	memset(&mat, 0, sizeof(mat));
	mat.blend = ENGINE_BLEND_OPAQUE;
	mat.tint[0] = mat.tint[1] = mat.tint[2] = mat.tint[3] = 1.0f;
	Engine_TextureMaterial(&mat, s_engHires.texture);
	// Double sided: the mirror pass flips the winding; the meshes are closed.
	mat.flags |= (u16)ENGINE_MATERIAL_DOUBLE_SIDED;
	s_engHires.material = Engine_MaterialRegister(&mat);
	if (s_engHires.material < 0)
	{
		Platform_Log("[CTR Engine] hires: material table full, not drawn in this level\n");
		return;
	}
	// The texcycle variants: the same material with the other textures.
	s_engHires.cycleMaterial[0] = s_engHires.material;
	for (variant = 1; variant < 3; variant++)
	{
		s_engHires.cycleMaterial[variant] = -1;
		if (g_cfg_engineHiresTexCycle && (s_engHires.cycleTex[variant] != 0xffffffffu) && (s_engHires.cycleTex[variant] != 0u))
		{
			EngineMaterial cm = mat;

			Engine_TextureMaterial(&cm, s_engHires.cycleTex[variant]);
			cm.flags |= (u16)ENGINE_MATERIAL_DOUBLE_SIDED;
			s_engHires.cycleMaterial[variant] = Engine_MaterialRegister(&cm);
		}
	}

	s_engHires.bodyRange.firstIndex = 0;
	s_engHires.bodyRange.indexCount = s_engHires.body.indexCount;
	s_engHires.bodyRange.material = (u16)s_engHires.material;
	memset(&desc, 0, sizeof(desc));
	desc.vertices = s_engHires.body.vertices;
	desc.verticesPerFrame = s_engHires.body.vertexCount;
	desc.frames = 1;
	desc.indices = s_engHires.body.indices;
	desc.indexCount = s_engHires.body.indexCount;
	desc.ranges = &s_engHires.bodyRange;
	desc.rangeCount = 1;
	desc.name = "hires body";
	s_engHires.bodyMesh = Engine_MeshCreate(&desc);

	s_engHires.wheelRange = s_engHires.bodyRange;
	s_engHires.wheelRange.indexCount = s_engHires.wheel.indexCount;
	desc.vertices = s_engHires.wheel.vertices;
	desc.verticesPerFrame = s_engHires.wheel.vertexCount;
	desc.indices = s_engHires.wheel.indices;
	desc.indexCount = s_engHires.wheel.indexCount;
	desc.ranges = &s_engHires.wheelRange;
	desc.name = "hires wheel";
	s_engHires.wheelMesh = Engine_MeshCreate(&desc);

	// The animated body: its own texture, its pose frames as mesh frames.
	if (s_engHires.animOk)
	{
		EngineMaterial am = mat;

		Engine_TextureMaterial(&am, s_engHires.animTexture);
		am.flags |= (u16)ENGINE_MATERIAL_DOUBLE_SIDED;
		s_engHires.animMaterial = Engine_MaterialRegister(&am);
		if (s_engHires.animMaterial >= 0)
		{
			s_engHires.animRange.firstIndex = 0;
			s_engHires.animRange.indexCount = s_engHires.anim.indexCount;
			s_engHires.animRange.material = (u16)s_engHires.animMaterial;
			desc.vertices = s_engHires.anim.vertices;
			desc.verticesPerFrame = s_engHires.anim.vertexCount;
			desc.frames = s_engHires.anim.frames;
			desc.indices = s_engHires.anim.indices;
			desc.indexCount = s_engHires.anim.indexCount;
			desc.ranges = &s_engHires.animRange;
			desc.rangeCount = 1;
			desc.name = "hires body animated";
			s_engHires.animMesh = Engine_MeshCreate(&desc);
		}
		Platform_Log("[CTR Engine] hires anim: mesh %d (%u frames), material %d\n", s_engHires.animMesh, s_engHires.anim.frames, s_engHires.animMaterial);
	}

	// A seat with a native set the engine draws keeps its own route.
	if ((set >= 0) && (Engine_DriverBodyMesh(set) >= 0))
	{
		s_engHires.seatOff = 1;
	}
	Platform_Log("[CTR Engine] hires: body mesh %d, wheel mesh %d, material %d; seat %d %s\n", s_engHires.bodyMesh, s_engHires.wheelMesh, s_engHires.material,
	             ENG_HIRES_SEAT, s_engHires.seatOff ? "has a native set: hi-res off for the seat in this level" : "drawn hi-res");
}

int Engine_HiresMenuReady(void)
{
	return g_cfg_engine && s_engHires.ok && (s_engHires.bodyMesh >= 0) && (s_engHires.wheelMesh >= 0);
}

int Engine_HiresOwns(const struct Driver *d)
{
	const struct GameTracker *gGT = sdata->gGT;

	if (!g_cfg_engine || !s_engHires.ok || s_engHires.seatOff || (s_engHires.bodyMesh < 0) || (d == NULL) || (gGT == NULL))
	{
		return 0;
	}
	return ((int)d->driverID == ENG_HIRES_SEAT) && (gGT->drivers[ENG_HIRES_SEAT] == d) && (d->instSelf != NULL);
}

// The meshes and the hull of the body (menu scene).
void Engine_HiresMenuParts(int *bodyMesh, int *wheelMesh, float hullMin[3], float hullMax[3])
{
	int c;

	*bodyMesh = s_engHires.bodyMesh;
	*wheelMesh = s_engHires.wheelMesh;
	for (c = 0; c < 3; c++)
	{
		hullMin[c] = s_engHires.body.hullMin[c];
		hullMax[c] = s_engHires.body.hullMax[c];
	}
}

void Engine_HiresNoteMenuFrame(void)
{
	s_engHires.menuFrames++;
}

// The texcycle variant of this frame: (vblank / ENGINE_HIRES_CYCLE_VBLANKS) % 3
// -> 0 full, 1 64 x 64, 2 key; a change is logged with its VBlank. -1 without
// the switch or when a variant's material is missing.
int Engine_HiresMenuMaterial(void)
{
	static const char *const names[3] = {"full", "64x64", "key"};
	const int vblank = Platform_GetVBlankCount();
	const int variant = ((vblank < 0) ? 0 : (vblank / ENGINE_HIRES_CYCLE_VBLANKS)) % 3;

	if (!g_cfg_engineHiresTexCycle || !s_engHires.ok || (s_engHires.cycleMaterial[1] < 0) || (s_engHires.cycleMaterial[2] < 0))
	{
		return -1;
	}
	if (s_engHires.cycleLogged != (variant + 1))
	{
		s_engHires.cycleLogged = variant + 1;
		Platform_Log("[CTR Engine] hires texcycle: vblank %d: %s\n", vblank, names[variant]);
	}
	return s_engHires.cycleMaterial[variant];
}

// ---------------------------------------------------------------------------
// The wheel frames (pure).

// The wheel middle in kart model units (x, y, z before the instance scale is
// applied in world units: the result IS world units relative to t, rotated by
// R): DrawTires_BuildWheelLocalPairs' integers, a quarter of them.
void Engine_HiresWheelMiddle(int wheel, s16 scaleX, s16 scaleY, s16 scaleZ, float out[3])
{
	const int x = ((int)scaleX * 0x90) >> 12;
	const int y = ((int)scaleY * 0x40) >> 12;
	const int z = (wheel < 2) ? (((int)scaleZ * 0xc7) >> 12) : (((int)scaleZ * -0x60) >> 12);

	out[0] = (float)(((wheel & 1) == 0) ? x : -x) * 0.25f;
	out[1] = (float)y * 0.25f;
	out[2] = (float)z * 0.25f;
}

// The rotation of a wheel in kart space, out[row][col]; columns: the axle
// (toward the kart's +x for every wheel: the one mesh is not mirrored), the rim
// up and the rim front after rolling. Steering (front only), wobble and the
// rim plane as native_wheels.c NativeWheels_Frames (the outward axle there is
// side * the axle here); rolling by +roll moves the top toward the front.
void Engine_HiresWheelFrame(int wheel, double steer, double roll, int hazardTimer, double out[3][3])
{
	static const int phase[4] = {0x000, 0x800, 0x400, 0xc00};
	const double side = ((wheel & 1) != 0) ? -1.0 : 1.0;
	int hazardShift = 9;
	int hazardAngle = hazardTimer * 32;
	double outward[3];
	double axle[3];
	double front[3];
	double up[3];
	double len;
	double a;
	double wobble;
	int r;

	if ((hazardTimer & 1) != 0)
	{
		hazardShift = 6;
		hazardAngle *= 2;
	}
	a = (double)(hazardAngle + phase[wheel & 3]) * ENG_HIRES_ANGLE;
	wobble = 1.0 / (double)(1 << hazardShift);
	if (wheel < 2)
	{
		outward[0] = side * cos(steer);
		outward[1] = 0.0;
		outward[2] = -side * sin(steer);
	}
	else
	{
		outward[0] = side;
		outward[1] = 0.0;
		outward[2] = 0.0;
	}
	outward[1] -= cos(a) * wobble;
	outward[2] -= sin(a) * wobble;
	len = sqrt(outward[0] * outward[0] + outward[1] * outward[1] + outward[2] * outward[2]);
	for (r = 0; r < 3; r++)
	{
		axle[r] = side * outward[r] / len;
	}
	// front = axle x (0, 1, 0), up = front x axle.
	front[0] = -axle[2];
	front[1] = 0.0;
	front[2] = axle[0];
	len = sqrt(front[0] * front[0] + front[2] * front[2]);
	front[0] /= len;
	front[2] /= len;
	up[0] = front[1] * axle[2] - front[2] * axle[1];
	up[1] = front[2] * axle[0] - front[0] * axle[2];
	up[2] = front[0] * axle[1] - front[1] * axle[0];
	for (r = 0; r < 3; r++)
	{
		out[r][0] = axle[r];
		out[r][1] = (up[r] * cos(roll)) + (front[r] * sin(roll));
		out[r][2] = (-up[r] * sin(roll)) + (front[r] * cos(roll));
	}
}

// The roll step of one logic tick, radians: the way over the radius.
double Engine_HiresRollStep(int speedApprox, int elapsedTimeMS, int wheelSize)
{
	const double radius = ENG_HIRES_WHEEL_RADIUS * (double)wheelSize / 4096.0;
	const double way = (double)speedApprox * (double)elapsedTimeMS / 32.0 / 256.0;

	if (wheelSize <= 0)
	{
		return 0.0;
	}
	return way / radius;
}

// worldFromWheel = [R * F * k | middle], R the instance rotation (4.12), F the
// wheel frame, k = wheelSize / 4096 (mesh radius 16 -> wheelSize / 256 world).
internal void EngHires_WheelWorld(const struct Instance *inst, const double F[3][3], const float middle[3], double k, EngineMat4 *out)
{
	double R[3][3];
	int row;
	int col;

	for (row = 0; row < 3; row++)
	{
		for (col = 0; col < 3; col++)
		{
			R[row][col] = (double)inst->matrix.m[row][col] / 4096.0;
		}
	}
	Engine_Mat4Identity(out);
	for (col = 0; col < 3; col++)
	{
		for (row = 0; row < 3; row++)
		{
			const double v = (R[row][0] * F[0][col]) + (R[row][1] * F[1][col]) + (R[row][2] * F[2][col]);

			out->m[col * 4 + row] = (float)(v * k);
		}
	}
	for (row = 0; row < 3; row++)
	{
		const double p = (R[row][0] * middle[0]) + (R[row][1] * middle[1]) + (R[row][2] * middle[2]);

		out->m[12 + row] = (float)((double)inst->matrix.t[row] + p);
	}
}

// The pose of the seat: a new driver or instance starts anew (roll 0); every
// new logic tick (gGT->timer moved) adds one step.
internal void EngHires_PoseTick(const struct GameTracker *gGT, const struct Driver *d, const struct Instance *inst)
{
	if (!s_engHires.poseValid || (s_engHires.poseDriver != d) || (s_engHires.poseInst != inst))
	{
		s_engHires.poseValid = 1;
		s_engHires.poseDriver = d;
		s_engHires.poseInst = inst;
		s_engHires.poseTimer = (u32)gGT->timer;
		s_engHires.poseTicks = 0;
		s_engHires.roll = 0.0;
		s_engHires.step = 0.0;
		return;
	}
	if ((u32)gGT->timer == s_engHires.poseTimer)
	{
		return;
	}
	s_engHires.poseTimer = (u32)gGT->timer;
	s_engHires.poseTicks++;
	s_engHires.step = Engine_HiresRollStep((int)d->speedApprox, gGT->elapsedTimeMS, (int)d->wheelSize);
	s_engHires.roll = fmod(s_engHires.roll + s_engHires.step, ENG_HIRES_TWO_PI);
	if (s_engHires.roll < 0.0)
	{
		s_engHires.roll += ENG_HIRES_TWO_PI;
	}
}

// The screen box of the body in a view, fractions of the view rect (x0 y0 x1 y1);
// 0 when a hull corner is behind the near plane.
internal int EngHires_BodyBox(const EngineCamera *cam, const struct Instance *inst, float box[4])
{
	EngineMat4 world;
	int i;

	if (!Engine_InstanceWorldMatrix(inst, &world) || (cam->rectW <= 0) || (cam->rectH <= 0))
	{
		return 0;
	}
	box[0] = box[1] = 1e9f;
	box[2] = box[3] = -1e9f;
	for (i = 0; i < 8; i++)
	{
		EngineVec3 p;
		EngineVec3 s;

		const float *lo = (s_engHires.animMesh >= 0) ? s_engHires.anim.hullMin : s_engHires.body.hullMin;
		const float *hi = (s_engHires.animMesh >= 0) ? s_engHires.anim.hullMax : s_engHires.body.hullMax;

		p.x = (i & 1) ? hi[0] : lo[0];
		p.y = (i & 2) ? hi[1] : lo[1];
		p.z = (i & 4) ? hi[2] : lo[2];
		if (!Engine_ProjectPoint(cam, Engine_Mat4MulPoint(&world, p), &s))
		{
			return 0;
		}
		box[0] = (s.x < box[0]) ? s.x : box[0];
		box[1] = (s.y < box[1]) ? s.y : box[1];
		box[2] = (s.x > box[2]) ? s.x : box[2];
		box[3] = (s.y > box[3]) ? s.y : box[3];
	}
	box[0] /= (float)cam->rectW;
	box[2] /= (float)cam->rectW;
	box[1] /= (float)cam->rectH;
	box[3] /= (float)cam->rectH;
	return 1;
}

// The inverse of a 3x3 matrix (rows as stored), 0 when it is singular.
internal int EngHires_Inverse3(const double m[3][3], double inv[3][3])
{
	const double c00 = m[1][1] * m[2][2] - m[1][2] * m[2][1];
	const double c01 = m[1][2] * m[2][0] - m[1][0] * m[2][2];
	const double c02 = m[1][0] * m[2][1] - m[1][1] * m[2][0];
	const double det = m[0][0] * c00 + m[0][1] * c01 + m[0][2] * c02;

	if (fabs(det) < 1e-12)
	{
		return 0;
	}
	inv[0][0] = c00 / det;
	inv[1][0] = c01 / det;
	inv[2][0] = c02 / det;
	inv[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / det;
	inv[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / det;
	inv[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) / det;
	inv[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / det;
	inv[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) / det;
	inv[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / det;
	return 1;
}

// One line per logic tick (solid pass, view 0). Drawn values are read back from
// the finished matrices into kart space through the INVERSE of the instance
// rotation: the front-left wheel's axle yaw, and the height of its middle
// (world middle - t). inst->matrix is not orthonormal: its 4.12 entries are
// rounded (up to about 0.03 world units at the wheel middle through the
// transpose), and the game writes a stretched matrix for a few ticks (seen on
// Dingo Canyon every 30 ticks: the up column 1.04 long), so a read-back through
// the transpose shows up to 1.47 units and 0.088 rad that were never drawn.
internal void EngHires_LogFrame(const struct GameTracker *gGT, const struct Driver *d, const struct Instance *inst, const EngineCamera *cam,
                                const EngineMat4 *frontLeft)
{
	double R[3][3];
	double Rinv[3][3];
	double axle[3];
	double rel[3];
	double kart[3];
	double drawnSteer;
	float box[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
	int row;

	if (s_engHires.logValid && (s_engHires.logTimer == (u32)gGT->timer))
	{
		return;
	}
	s_engHires.logValid = 1;
	s_engHires.logTimer = (u32)gGT->timer;
	for (row = 0; row < 3; row++)
	{
		int k;

		for (k = 0; k < 3; k++)
		{
			R[row][k] = (double)inst->matrix.m[row][k] / 4096.0;
		}
		rel[row] = (double)frontLeft->m[12 + row] - (double)inst->matrix.t[row];
	}
	if (!EngHires_Inverse3(R, Rinv))
	{
		return;
	}
	// R^-1 * column 0 of the wheel matrix (scale k is a common factor of the direction).
	for (row = 0; row < 3; row++)
	{
		int k;

		axle[row] = 0.0;
		kart[row] = 0.0;
		for (k = 0; k < 3; k++)
		{
			axle[row] += Rinv[row][k] * (double)frontLeft->m[k];
			kart[row] += Rinv[row][k] * rel[k];
		}
	}
	drawnSteer = atan2(-axle[2], axle[0]);
	(void)EngHires_BodyBox(cam, inst, box);
	s_engHires.logLines++;
	Platform_Log("[CTR Engine] hires frame: vb %d timer %u speed %d ms %d pos %d %d %d up %.4f %.4f %.4f roll %.5f step %.5f radius %.4f "
	             "steerRaw %d steerState %.5f steerDrawn %.5f scaleY %d squish %d middleYState %.3f middleYDrawn %.3f hazard %d box %.4f %.4f %.4f %.4f\n",
	             Platform_GetVBlankCount(), (unsigned)gGT->timer, (int)d->speedApprox, gGT->elapsedTimeMS, (int)d->posCurr.x, (int)d->posCurr.y,
	             (int)d->posCurr.z, (double)inst->matrix.m[0][1] / 4096.0, (double)inst->matrix.m[1][1] / 4096.0, (double)inst->matrix.m[2][1] / 4096.0,
	             s_engHires.roll, s_engHires.step, ENG_HIRES_WHEEL_RADIUS * (double)d->wheelSize / 4096.0, (int)d->wheelRotation,
	             (double)((int)d->wheelRotation * 4) * ENG_HIRES_ANGLE, drawnSteer, (int)inst->scale.y, (int)d->jumpSquishStretch,
	             (double)((((int)inst->scale.y * 0x40) >> 12) * 0.25f), kart[1], (int)d->hazardTimer, (double)box[0], (double)box[1], (double)box[2],
	             (double)box[3]);
}

// The animation counters of the seat, one step per logic tick (gGT->timer
// moved); a new driver or instance starts at 0. Read only.
internal void EngHires_AnimTick(const struct GameTracker *gGT, const struct Driver *d, const struct Instance *inst)
{
	if (!s_engHires.animValid || (s_engHires.animDriver != d) || (s_engHires.animInst != inst))
	{
		s_engHires.animValid = 1;
		s_engHires.animDriver = d;
		s_engHires.animInst = inst;
		s_engHires.animTimer = (u32)gGT->timer;
		s_engHires.airTicks = 0;
		s_engHires.driftTicks = 0;
		s_engHires.driftSide = 1;
		return;
	}
	if ((u32)gGT->timer == s_engHires.animTimer)
	{
		return;
	}
	s_engHires.animTimer = (u32)gGT->timer;
	if ((d->actionsFlagSet & ACTION_TOUCH_GROUND) != 0)
	{
		s_engHires.airTicks = 0;
	}
	else if (s_engHires.airTicks < ENG_HIRES_TICKS_CAP)
	{
		s_engHires.airTicks++;
	}
	if (d->kartState == KS_DRIFTING)
	{
		if (s_engHires.driftTicks == 0)
		{
			s_engHires.driftSide = EngHires_DriftSide((int)d->multDrift, (int)d->simpTurnState, (int)d->wheelRotation);
		}
		if (s_engHires.driftTicks < ENG_HIRES_TICKS_CAP)
		{
			s_engHires.driftTicks++;
		}
	}
	else
	{
		s_engHires.driftTicks = 0;
	}
}

// One line per logic tick (view 0): the state read and the frame drawn.
internal void EngHires_AnimLog(const struct GameTracker *gGT, const struct Driver *d, const struct Instance *inst, int clip, u32 frame)
{
	static const char *const names[ENG_HIRES_CLIPS] = {"steer", "air", "drift_l", "drift_r"};

	if (s_engHires.animLogValid && (s_engHires.animLogTimer == (u32)gGT->timer))
	{
		return;
	}
	s_engHires.animLogValid = 1;
	s_engHires.animLogTimer = (u32)gGT->timer;
	s_engHires.animLines++;
	Platform_Log("[CTR Engine] hires anim: vb %d timer %u clip %s frame %u (of clip %u) steerRaw %d touch %d airTicks %d kartState %d driftTicks %d side %d "
	             "multDrift %d simpTurn %d animIndex %d animFrame %d speed %d\n",
	             Platform_GetVBlankCount(), (unsigned)gGT->timer, names[clip], frame, frame - s_engHires.anim.clipFirst[clip], (int)d->wheelRotation,
	             ((d->actionsFlagSet & ACTION_TOUCH_GROUND) != 0) ? 1 : 0, s_engHires.airTicks, (int)d->kartState, s_engHires.driftTicks,
	             s_engHires.driftSide, (int)d->multDrift, (int)d->simpTurnState, (int)inst->animIndex, (int)inst->animFrame, (int)d->speedApprox);

	// The snapshot series (only with --shot-name).
	if (g_cfg_shotName[0] != '\0')
	{
		static const char *const tags[ENG_HIRES_SHOT_TAGS] = {"steer-l", "steer-r", "air", "drift-l", "drift-r"};
		const u32 inClip = frame - s_engHires.anim.clipFirst[clip];
		const u32 last = s_engHires.anim.clipCount[clip] - 1u;
		int tag = -1;

		if (clip == ENG_HIRES_CLIP_STEER)
		{
			tag = (inClip == last) ? 0 : ((inClip == 0u) ? 1 : -1);
		}
		else if (inClip == last)
		{
			tag = clip + 1; // air 2, drift_l 3, drift_r 4
		}
		if ((tag >= 0) && (s_engHires.shotCount[tag] < ENG_HIRES_SHOTS_PER_TAG) &&
		    ((s_engHires.shotCount[tag] == 0u) || (((u32)gGT->timer - s_engHires.shotTimer[tag]) >= ENG_HIRES_SHOT_GAP)))
		{
			char base[PLATFORM_ARG_PATH_MAX];
			char path[PLATFORM_ARG_PATH_MAX + 48];
			size_t length;

			snprintf(base, sizeof(base), "%s", g_cfg_shotName);
			length = strlen(base);
			if ((length > 4) && ((strcmp(&base[length - 4], ".bmp") == 0) || (strcmp(&base[length - 4], ".BMP") == 0)))
			{
				base[length - 4] = '\0';
			}
			snprintf(path, sizeof(path), "%s-%s-%d.bmp", base, tags[tag], Platform_GetVBlankCount());
			Platform_ShotRequest(path);
			s_engHires.shotCount[tag]++;
			s_engHires.shotTimer[tag] = (u32)gGT->timer;
			Platform_Log("[CTR Engine] hires anim: snapshot %s (timer %u)\n", path, (unsigned)gGT->timer);
		}
	}
}

// ---------------------------------------------------------------------------
// Draws.

int Engine_HiresRoute(struct Instance *inst, struct InstDrawPerPlayer *idpp, struct PushBuffer *pb)
{
	struct GameTracker *gGT = sdata->gGT;
	const struct Driver *d;
	EngineInstanceDraw draw;
	int clip = ENG_HIRES_CLIP_STEER;

	if (!g_cfg_engine || !s_engHires.ok || (inst == NULL) || (idpp == NULL) || (pb == NULL) || (gGT == NULL))
	{
		return 0;
	}
	d = gGT->drivers[ENG_HIRES_SEAT];
	if ((d == NULL) || (d->instSelf != inst) || !Engine_HiresOwns(d))
	{
		return 0;
	}
	if (s_engHires.animMesh >= 0)
	{
		EngHires_AnimTick(gGT, d, inst);
	}
	if ((inst->thread == NULL) || (inst->thread->object != (void *)d) || ((inst->flags & (SCREENSPACE_INSTANCE | HIDE_MODEL)) != 0) ||
	    (Engine_InstanceViewOf(pb) < 0) || !EngCmdl_MeshReady(s_engHires.bodyMesh))
	{
		s_engHires.bodyDeclined++;
		return 0;
	}
	memset(&draw, 0, sizeof(draw));
	draw.mesh = s_engHires.bodyMesh;
	draw.frame = 0u;
	if ((s_engHires.animMesh >= 0) && EngCmdl_MeshReady(s_engHires.animMesh))
	{
		draw.mesh = s_engHires.animMesh;
		draw.frame = EngHires_AnimPick(&s_engHires.anim, (int)d->wheelRotation, s_engHires.airTicks, s_engHires.driftTicks, s_engHires.driftSide, &clip);
		if (Engine_InstanceViewOf(pb) == 0)
		{
			EngHires_AnimLog(gGT, d, inst, clip, draw.frame);
		}
	}
	EngCmdl_Tints(inst, idpp, &draw);
	draw.drawBelow = 1;
	draw.flags = 0;
	if (Engine_InstanceSubmit(inst, idpp, pb, &draw) > 0)
	{
		if (draw.mesh == s_engHires.animMesh)
		{
			s_engHires.animDraws[clip]++;
		}
		s_engHires.bodyDrawn++;
		Engine_FxNoteDrawn(ENGINE_FX_BODY);
		return 1;
	}
	s_engHires.bodyDeclined++;
	return 0;
}

int Engine_HiresWheels(const struct Driver *d, const struct Instance *inst, const struct InstDrawPerPlayer *idpp, const struct PushBuffer *pb, int reflection)
{
	const struct GameTracker *gGT = sdata->gGT;
	const EngineCamera *cam;
	EngineMat4 screenFromWorld;
	EngineMat4 world[4];
	EngineMat4 mirror;
	float middle[4][3];
	double steer;
	double k;
	float waterLevel;
	int view;
	int w;
	int submitted = 0;

	if (!Engine_HiresOwns(d))
	{
		return 0;
	}
	view = Engine_InstanceViewOf(pb);
	cam = (view >= 0) ? Engine_FrameCamera(view) : NULL;
	if ((inst == NULL) || (idpp == NULL) || (cam == NULL) || !cam->valid || (gGT == NULL) || (d->wheelSize == 0) || (idpp->otRangeNormal == 0) ||
	    !EngCmdl_MeshReady(s_engHires.wheelMesh))
	{
		goto sprites;
	}

	EngHires_PoseTick(gGT, d, inst);
	steer = (double)((int)d->wheelRotation * 4) * ENG_HIRES_ANGLE;
	k = (double)d->wheelSize / 4096.0;
	waterLevel = (float)inst->vertSplit;
	for (w = 0; w < 4; w++)
	{
		double F[3][3];

		Engine_HiresWheelMiddle(w, inst->scale.x, inst->scale.y, inst->scale.z, middle[w]);
		Engine_HiresWheelFrame(w, steer, s_engHires.roll, (int)d->hazardTimer, F);
		EngHires_WheelWorld(inst, F, middle[w], k, &world[w]);
	}
	if ((view == 0) && !reflection)
	{
		EngHires_LogFrame(gGT, d, inst, cam, &world[0]);
	}

	Engine_CameraScreenFromWorld(cam, &screenFromWorld);
	Engine_Mat4Identity(&mirror);
	if (reflection)
	{
		// y' = 2 L - y (engine_instance.c EngInst_Reflect).
		EngInst_Reflect(waterLevel, &mirror);
	}
	for (w = 0; w < 4; w++)
	{
		const float centreY = world[w].m[13];
		EngineMat4 placed;
		EngineDraw draw;
		EngineVec3 centre;
		u32 *ot;
		int cell;

		// The skip rule and the range of engine_fx_tires.c (water line of the instance).
		if (reflection && (centreY < waterLevel))
		{
			continue;
		}
		if (reflection)
		{
			ot = (u32 *)(size_t)(u32)((idpp->otRangeSecondary != 0) ? idpp->otRangeSecondary : idpp->otRangeNormal);
		}
		else
		{
			ot = (u32 *)(size_t)(u32)(((waterLevel >= centreY) && (idpp->otRangeSecondary != 0)) ? idpp->otRangeSecondary : idpp->otRangeNormal);
		}
		cell = NativeRenderLayer_MarkerBin(idpp, 0);
		Engine_Mat4Mul(&mirror, &world[w], &placed);
		centre.x = placed.m[12];
		centre.y = placed.m[13];
		centre.z = placed.m[14];

		memset(&draw, 0, sizeof(draw));
		draw.mesh = s_engHires.wheelMesh;
		draw.frame = 0u;
		draw.materialOverride = -1;
		draw.dynMaterial = -1;
		Engine_Mat4Mul(&screenFromWorld, &placed, &draw.clipFromModel);
		draw.tint[0] = draw.tint[1] = draw.tint[2] = draw.tint[3] = 1.0f;
		draw.sortDepth = Engine_Mat4MulPoint(&cam->viewFromWorld, centre).z;
		draw.otCell = (u32)cell;
		if (Engine_RenderSubmitLinked(view, &draw, ot, cell))
		{
			submitted++;
			if (reflection)
			{
				s_engHires.mirrorWheelDraws++;
			}
			else
			{
				s_engHires.wheelDraws++;
			}
		}
	}
	if (!reflection && (submitted > 0))
	{
		s_engHires.wheelSets++;
	}
	if ((submitted == 0) && !reflection)
	{
		goto sprites;
	}
	return 1;

sprites:
	// Nothing drawn: the game's sprites stand in (counted, must stay 0).
	s_engHires.spriteFallbacks += 4;
	for (w = 0; w < 4; w++)
	{
		Engine_FxNoteRetail(ENGINE_FX_TIRES);
	}
	return -1;
}

// ---------------------------------------------------------------------------
// Selftest: parsers, reduction, wheel frames, roll step, middles.

int Engine_SelfTestHires(int *checks)
{
	int failures = 0;
	int n = 0;

#define HIRES_CHECK(cond, what)                                   \
	do                                                            \
	{                                                             \
		n++;                                                      \
		if (!(cond))                                              \
		{                                                         \
			failures++;                                           \
			printf("engine selftest: hires " what " FAILED\n"); \
		}                                                         \
	} while (0)

	// A one-triangle mesh, good and broken.
	{
		u8 buf[12 + 3 * 24 + 6];
		EngHiresMeshData m;
		const char *why = "";
		const u32 vc = 3;
		const u32 ic = 3;
		const u16 idx[3] = {0, 1, 2};
		const float x1 = 2.0f;
		const float y2 = -3.0f;
		const float t2 = 0.5f;
		int ok;

		// Three 24-byte records (position, texcoord, colour), all zero but x of
		// vertex 1, y of vertex 2, the second texcoord of vertex 2 and its colour.
		memset(buf, 0, sizeof(buf));
		memcpy(buf, "HRM1", 4);
		memcpy(buf + 4, &vc, 4);
		memcpy(buf + 8, &ic, 4);
		memcpy(buf + 12 + 24 + 0, &x1, 4);
		memcpy(buf + 12 + 48 + 4, &y2, 4);
		memcpy(buf + 12 + 48 + 16, &t2, 4);
		buf[12 + 48 + 20] = 10;
		buf[12 + 48 + 23] = 40;
		memcpy(buf + 12 + 3 * 24, idx, sizeof(idx));
		ok = EngHires_ParseMesh(buf, sizeof(buf), &m, &why);
		HIRES_CHECK(ok && (m.vertexCount == 3u) && (m.indexCount == 3u), "mesh parses");
		HIRES_CHECK(ok && (m.hullMax[0] == 2.0f) && (m.hullMin[1] == -3.0f), "mesh hull");
		HIRES_CHECK(ok && (m.vertices[2].texcoord[1] == 0.5f) && (m.vertices[2].color[0] == 10) && (m.vertices[2].color[3] == 40) &&
		                (m.vertices[1].position[0] == 2.0f) && (m.indices[2] == 2),
		            "mesh records read field by field");
		HIRES_CHECK(ok && (m.vertices[0].uvBox[0] == 0) && (m.vertices[0].uvBox[1] == 0) && (m.vertices[2].uvBox[2] == 0) && (m.vertices[2].uvBox[3] == 0),
		            "mesh vertices get the whole page as UV box (stored as zeros)");
		EngHires_FreeMesh(&m);
		HIRES_CHECK(!EngHires_ParseMesh(buf, sizeof(buf) - 1u, &m, &why), "short mesh refused");
		buf[12 + 3 * 24 + 4] = 7; // index 2 -> 7
		HIRES_CHECK(!EngHires_ParseMesh(buf, sizeof(buf), &m, &why), "index out of range refused");
		buf[0] = 'X';
		HIRES_CHECK(!EngHires_ParseMesh(buf, sizeof(buf), &m, &why), "bad magic refused");
	}
	// Textures: good 16 x 16, bad edge, short.
	{
		static u8 tex[12 + 16 * 16 * 4];
		const u32 e16 = 16;
		const u32 e24 = 24;
		const u8 *rgba = NULL;
		const char *why = "";
		int w = 0;
		int h = 0;

		memset(tex, 0, sizeof(tex));
		memcpy(tex, "HRT1", 4);
		memcpy(tex + 4, &e16, 4);
		memcpy(tex + 8, &e16, 4);
		HIRES_CHECK(EngHires_ParseTexture(tex, sizeof(tex), &w, &h, &rgba, &why) && (w == 16) && (h == 16) && (rgba == tex + 12), "texture parses");
		HIRES_CHECK(!EngHires_ParseTexture(tex, sizeof(tex) - 4u, &w, &h, &rgba, &why), "short texture refused");
		memcpy(tex + 4, &e24, 4);
		HIRES_CHECK(!EngHires_ParseTexture(tex, sizeof(tex), &w, &h, &rgba, &why), "edge 24 refused");
	}
	// The reduction: a 2 x 2 checker of 0/255 per 128 x 128 block gives 128 (half up).
	{
		static u8 src[128 * 128 * 4];
		u8 dst[64 * 64 * 4];
		int x;
		int y;
		int same = 1;

		for (y = 0; y < 128; y++)
		{
			for (x = 0; x < 128; x++)
			{
				const u8 v = (u8)((((x ^ y) & 1) != 0) ? 255 : 0);

				src[(y * 128 + x) * 4 + 0] = v;
				src[(y * 128 + x) * 4 + 1] = v;
				src[(y * 128 + x) * 4 + 2] = v;
				src[(y * 128 + x) * 4 + 3] = 255;
			}
		}
		HIRES_CHECK(EngHires_Reduce(src, 128, 128, 64, 64, dst), "reduce runs");
		for (x = 0; x < 64 * 64; x++)
		{
			same = same && (dst[x * 4] == 128) && (dst[x * 4 + 3] == 255);
		}
		HIRES_CHECK(same, "reduce averages");
		HIRES_CHECK(!EngHires_Reduce(src, 100, 128, 64, 64, dst), "reduce refuses an odd ratio");
	}
	// Wheel frames: rest = identity for every wheel; roll pi/2 turns up to the
	// front; a steer s gives the front axle yaw s (as the log reads it back), the
	// rear none; every frame a rotation (det +1).
	{
		double F[3][3];
		int wheel;
		int rest = 1;
		int dets = 1;

		for (wheel = 0; wheel < 4; wheel++)
		{
			int r;
			int c;

			Engine_HiresWheelFrame(wheel, 0.0, 0.0, 0, F);
			for (r = 0; r < 3; r++)
			{
				for (c = 0; c < 3; c++)
				{
					// The retail wobble tilts the axle by up to 1/512 even at hazardTimer 0.
					rest = rest && (fabs(F[r][c] - ((r == c) ? 1.0 : 0.0)) < 0.004);
				}
			}
			Engine_HiresWheelFrame(wheel, 0.3, 1.1, 5, F);
			{
				const double det = F[0][0] * (F[1][1] * F[2][2] - F[1][2] * F[2][1]) - F[0][1] * (F[1][0] * F[2][2] - F[1][2] * F[2][0]) +
				                   F[0][2] * (F[1][0] * F[2][1] - F[1][1] * F[2][0]);

				dets = dets && (fabs(det - 1.0) < 1e-9);
			}
		}
		HIRES_CHECK(rest, "wheel frame at rest is the identity (but the wobble)");
		HIRES_CHECK(dets, "wheel frames are rotations");
		Engine_HiresWheelFrame(0, 0.0, ENG_HIRES_TWO_PI / 4.0, 0, F);
		HIRES_CHECK((fabs(F[2][1] - 1.0) < 1e-9) && (fabs(F[1][1]) < 1e-9), "roll moves the top to the front");
		Engine_HiresWheelFrame(1, 0.25, 0.0, 0, F);
		HIRES_CHECK(fabs(atan2(-F[2][0], F[0][0]) - 0.25) < 0.004, "front axle yaw = steer (but the wobble)");
		Engine_HiresWheelFrame(3, 0.25, 0.0, 0, F);
		HIRES_CHECK(fabs(atan2(-F[2][0], F[0][0])) < 0.004, "rear axle does not steer (but the wobble)");
	}
	// Roll step: way / radius, so omega * r = v exactly.
	{
		const double step = Engine_HiresRollStep(1000, 32, 0xccc);
		const double way = 1000.0 * 32.0 / 32.0 / 256.0;
		const double radius = 16.0 * (double)0xccc / 4096.0;

		HIRES_CHECK(fabs(step * radius - way) < 1e-9, "roll step times radius is the way");
		HIRES_CHECK(Engine_HiresRollStep(-500, 33, 0xccc) < 0.0, "backward speed rolls backward");
		HIRES_CHECK(Engine_HiresRollStep(1000, 32, 0) == 0.0, "no wheel no roll");
	}
	// The read-back through the inverse: a yaw with a stretched up column (as the
	// game writes for a few ticks) gives the kart-space point back; the transpose
	// does not.
	{
		const double c = cos(0.4);
		const double sn = sin(0.4);
		const double R[3][3] = {{c, 0.0, sn}, {0.0, 1.04, 0.0}, {-sn, 0.0, c}};
		const double m[3] = {28.75, 12.75, 39.75};
		double inv[3][3];
		double w[3];
		double back[3];
		double viaT = 0.0;
		int r;

		for (r = 0; r < 3; r++)
		{
			w[r] = R[r][0] * m[0] + R[r][1] * m[1] + R[r][2] * m[2];
		}
		HIRES_CHECK(EngHires_Inverse3(R, inv), "inverse exists");
		for (r = 0; r < 3; r++)
		{
			back[r] = inv[r][0] * w[0] + inv[r][1] * w[1] + inv[r][2] * w[2];
			viaT += (r == 1) ? (R[0][1] * w[0] + R[1][1] * w[1] + R[2][1] * w[2]) : 0.0;
		}
		HIRES_CHECK((fabs(back[0] - m[0]) < 1e-9) && (fabs(back[1] - m[1]) < 1e-9) && (fabs(back[2] - m[2]) < 1e-9), "inverse reads the middle back");
		HIRES_CHECK(fabs(viaT - m[1]) > 0.5, "the transpose would not (stretched matrix)");
	}
	// Middles: the integer rule of DrawTires.c at the rest scale 0xccc.
	{
		float m[3];

		Engine_HiresWheelMiddle(0, 0xccc, 0xccc, 0xccc, m);
		HIRES_CHECK((m[0] == 28.75f) && (m[1] == 12.75f) && (m[2] == 39.75f), "front +x middle");
		Engine_HiresWheelMiddle(3, 0xccc, 0xccc, 0xccc, m);
		HIRES_CHECK((m[0] == -28.75f) && (m[2] == -19.25f), "rear -x middle");
		Engine_HiresWheelMiddle(0, 0xccc, 0x1000, 0xccc, m);
		HIRES_CHECK(m[1] == 16.0f, "middle follows scale.y");
	}
	// The value of --engine-menuscene-zoom (engine_menuscene.c): zoom alone,
	// with the place, with a still yaw; out of range and malformed refused and
	// the outputs untouched.
	{
		float zoom = -1.0f;
		float x = -9.0f;
		float yaw = -9.0f;
		int fixed = -1;

		HIRES_CHECK(Engine_MenuSceneZoomParse("2", &zoom, &x, &fixed, &yaw) && (zoom == 2.0f) && (fabsf(x - 0.30f) < 1e-6f) && (fixed == 0),
		            "zoom alone keeps the place and the turn");
		HIRES_CHECK(Engine_MenuSceneZoomParse("2.5,-0.78,90", &zoom, &x, &fixed, &yaw) && (zoom == 2.5f) && (fabsf(x + 0.78f) < 1e-6f) && (fixed == 1) &&
		                (fabsf(yaw - 1.5707963f) < 1e-5f),
		            "zoom, place and still yaw");
		zoom = 7.0f;
		HIRES_CHECK(!Engine_MenuSceneZoomParse("9", &zoom, &x, &fixed, &yaw) && (zoom == 7.0f), "zoom out of range refused, outputs untouched");
		HIRES_CHECK(!Engine_MenuSceneZoomParse("2,1.5", &zoom, &x, &fixed, &yaw), "place out of range refused");
		HIRES_CHECK(!Engine_MenuSceneZoomParse("2,0,10,4", &zoom, &x, &fixed, &yaw), "a fourth value refused");
		HIRES_CHECK(!Engine_MenuSceneZoomParse("2x", &zoom, &x, &fixed, &yaw) && !Engine_MenuSceneZoomParse("", &zoom, &x, &fixed, &yaw) &&
		                !Engine_MenuSceneZoomParse("2,,1", &zoom, &x, &fixed, &yaw),
		            "malformed values refused");
	}
	// The animated body file (HRA1): a triangle in 4 frames, clips steer 0/3 and
	// air 3/1; fields land by name; broken files refused.
	{
		static u8 buf[20 + 2 * 24 + 3 * 8 + 4 * 3 * 16 + 3 * 2];
		const u32 head[4] = {3, 3, 4, 2};
		const u32 steerClip[2] = {0, 3};
		const u32 airClip[2] = {3, 1};
		EngHiresAnimData a;
		const char *why = "";
		u8 *q = buf;
		u32 f;
		u32 i;
		int ok;
		int pickClip = -1;

		memset(buf, 0, sizeof(buf));
		memcpy(q, "HRA1", 4);
		memcpy(q + 4, head, sizeof(head));
		q += 20;
		memcpy(q, "steer", 5);
		memcpy(q + 16, steerClip, sizeof(steerClip));
		memcpy(q + 24, "air", 3);
		memcpy(q + 40, airClip, sizeof(airClip));
		q += 48;
		for (i = 0; i < 3; i++)
		{
			const float uv[2] = {0.25f * (float)i, 1.0f - 0.25f * (float)i};

			memcpy(q, uv, sizeof(uv));
			q += 8;
		}
		for (f = 0; f < 4; f++)
		{
			for (i = 0; i < 3; i++)
			{
				const float pos[3] = {(float)f, (float)i, -(float)(f + i)};

				memcpy(q, pos, sizeof(pos));
				q[12] = (u8)(10 * f + i);
				q[13] = 7;
				q[14] = 8;
				q[15] = 255;
				q += 16;
			}
		}
		q[0] = 0;
		q[2] = 1;
		q[4] = 2;
		ok = EngHires_ParseAnim(buf, sizeof(buf), &a, &why);
		HIRES_CHECK(ok && (a.vertexCount == 3u) && (a.indexCount == 3u) && (a.frames == 4u) && (a.clipCount[ENG_HIRES_CLIP_STEER] == 3u) &&
		                (a.clipFirst[ENG_HIRES_CLIP_AIR] == 3u) && (a.clipCount[ENG_HIRES_CLIP_AIR] == 1u) && (a.clipCount[ENG_HIRES_CLIP_DRIFT_L] == 0u),
		            "anim parses with its clips");
		HIRES_CHECK(ok && (a.vertices[3 * 2 + 1].position[0] == 2.0f) && (a.vertices[3 * 2 + 1].position[1] == 1.0f) &&
		                (a.vertices[3 * 2 + 1].position[2] == -3.0f) && (a.vertices[3 * 2 + 1].color[0] == 21) && (a.vertices[3 * 2 + 1].color[3] == 255) &&
		                (a.vertices[3 * 2 + 1].texcoord[0] == 0.25f) && (a.vertices[3 * 2 + 1].texcoord[1] == 0.75f),
		            "anim vertex fields by name");
		HIRES_CHECK(ok && (a.hullMin[0] == 0.0f) && (a.hullMax[0] == 3.0f) && (a.hullMin[2] == -5.0f), "anim hull over all frames");
		HIRES_CHECK(ok && (EngHires_AnimPick(&a, 0, 0, 0, 1, &pickClip) == 1u), "a steer clip of 3: straight is its middle");
		EngHires_FreeAnim(&a);
		HIRES_CHECK(!EngHires_ParseAnim(buf, sizeof(buf) - 2u, &a, &why), "short anim refused");
		buf[20 + 48 + 24 + 192 + 4] = 3; // index 2 -> 3
		HIRES_CHECK(!EngHires_ParseAnim(buf, sizeof(buf), &a, &why), "anim index out of range refused");
		buf[20 + 48 + 24 + 192 + 4] = 2;
		memcpy(buf + 20 + 20, &head[3], 4); // steer count 3 -> 2 (even)
		HIRES_CHECK(!EngHires_ParseAnim(buf, sizeof(buf), &a, &why), "even steer clip refused");
		memcpy(buf + 20 + 20, &steerClip[1], 4);
		memcpy(buf + 20 + 24 + 16, &head[0], 4); // air first 3 -> 3 ok; count 1 -> first 3 count 3: outside
		memcpy(buf + 20 + 24 + 20, &head[0], 4);
		HIRES_CHECK(!EngHires_ParseAnim(buf, sizeof(buf), &a, &why), "clip outside the frames refused");
		memcpy(buf + 20 + 24 + 20, &airClip[1], 4);
		{
			const float bad = 1.5f;

			memcpy(buf + 20 + 48, &bad, 4);
			HIRES_CHECK(!EngHires_ParseAnim(buf, sizeof(buf), &a, &why), "texcoord outside 0..1 refused");
		}
		buf[0] = 'X';
		HIRES_CHECK(!EngHires_ParseAnim(buf, sizeof(buf), &a, &why), "anim bad magic refused");
	}
	// The pick: the clip table of tools/hires_blender_driver.py (steer 0/9, air
	// 9/4, drift_l 13/5, drift_r 18/5); air before drift before steer.
	{
		EngHiresAnimData a;
		int clip = -1;

		memset(&a, 0, sizeof(a));
		a.clipFirst[ENG_HIRES_CLIP_STEER] = 0;
		a.clipCount[ENG_HIRES_CLIP_STEER] = 9;
		a.clipFirst[ENG_HIRES_CLIP_AIR] = 9;
		a.clipCount[ENG_HIRES_CLIP_AIR] = 4;
		a.clipFirst[ENG_HIRES_CLIP_DRIFT_L] = 13;
		a.clipCount[ENG_HIRES_CLIP_DRIFT_L] = 5;
		a.clipFirst[ENG_HIRES_CLIP_DRIFT_R] = 18;
		a.clipCount[ENG_HIRES_CLIP_DRIFT_R] = 5;
		HIRES_CHECK((EngHires_AnimPick(&a, 0, 0, 0, 1, &clip) == 4u) && (clip == ENG_HIRES_CLIP_STEER), "straight is the steer middle");
		HIRES_CHECK((EngHires_AnimPick(&a, 64, 0, 0, 1, &clip) == 8u) && (EngHires_AnimPick(&a, 85, 0, 0, 1, &clip) == 8u), "full left lock and beyond: last steer frame");
		HIRES_CHECK((EngHires_AnimPick(&a, -64, 0, 0, 1, &clip) == 0u) && (EngHires_AnimPick(&a, 32, 0, 0, 1, &clip) == 6u) &&
		                (EngHires_AnimPick(&a, -8, 0, 0, 1, &clip) == 4u) && (EngHires_AnimPick(&a, -9, 0, 0, 1, &clip) == 3u),
		            "steer to the nearest frame");
		HIRES_CHECK((EngHires_AnimPick(&a, 0, ENG_HIRES_AIR_DELAY - 1, 0, 1, &clip) == 4u) && (clip == ENG_HIRES_CLIP_STEER), "a bump stays on the ground");
		HIRES_CHECK((EngHires_AnimPick(&a, 0, ENG_HIRES_AIR_DELAY, 0, 1, &clip) == 9u) && (clip == ENG_HIRES_CLIP_AIR) &&
		                (EngHires_AnimPick(&a, 0, ENG_HIRES_AIR_DELAY + 40, 0, 1, &clip) == 12u),
		            "air: a frame per tick, held at the last");
		HIRES_CHECK((EngHires_AnimPick(&a, 30, 0, 1, 1, &clip) == 13u) && (clip == ENG_HIRES_CLIP_DRIFT_L) && (EngHires_AnimPick(&a, 30, 0, 9, 1, &clip) == 17u),
		            "drift left ramps in and holds");
		HIRES_CHECK((EngHires_AnimPick(&a, 30, 0, 2, -1, &clip) == 19u) && (clip == ENG_HIRES_CLIP_DRIFT_R), "drift right");
		HIRES_CHECK((EngHires_AnimPick(&a, 30, ENG_HIRES_AIR_DELAY + 1, 4, 1, &clip) == 10u) && (clip == ENG_HIRES_CLIP_AIR), "air before drift");
		a.clipCount[ENG_HIRES_CLIP_DRIFT_R] = 0;
		HIRES_CHECK((EngHires_AnimPick(&a, -64, 0, 2, -1, &clip) == 0u) && (clip == ENG_HIRES_CLIP_STEER), "a missing clip falls back to steer");
		HIRES_CHECK((EngHires_DriftSide(0, 0, 0) == 1) && (EngHires_DriftSide(-5, 3, 3) == -1) && (EngHires_DriftSide(0, -2, 9) == -1) &&
		                (EngHires_DriftSide(0, 0, -1) == -1) && (EngHires_DriftSide(7, -2, -9) == 1),
		            "drift side: first non-zero of multDrift, simpTurnState, wheelRotation");
	}
#undef HIRES_CHECK
	*checks += n;
	return failures;
}
