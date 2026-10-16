#include <platform/engine.h>
#include <math.h>
#include <stdio.h>

// ENGINE FX WEATHER - the track's rain and snow, the red beaker's cloud rain and
// the confetti drawn by the engine (day round of 2026-10-16); the heat shimmer is
// counted only (PARKED below). See include/platform/engine.h, ENGINE FX WEATHER.
//
// WHERE AND WHAT. Three retail drawers run early in MainFrame_RenderFrame, before
// Engine_FrameCollect: RenderWeather (game/RenderWeather/RenderWeather.c, one
// LINE_G2 per drop, all in one OT cell, then one draw-mode packet with the
// blend), RedBeaker_RenderRain (RedBeaker_RenderRain.c, per cloud two passes of
// LINE_G2 into one cell, each pass closed by its draw-mode packet: 0xe1000a20 =
// abr 1, 0xe1000a40 = abr 2) and DrawConfetti (game/DrawConfetti.c, one flat
// POLY_F4 per piece, each in its own depth cell). Each of them writes its game
// state first - the particle count fade, the scroll words, the previous camera of
// the rain buffer, the clouds' scroll, the scratch pad, the GTE registers - and
// then writes and links one primitive per drop or piece. The hook sits at the
// link: the primitive is written and the prim cursor advances exactly as before,
// only the link is left out when the engine answers 1. So every state the game
// writes while drawing, the prim memory it uses and the draw-mode packets (still
// linked, the PS1 draw state after them is unchanged) stay as they were.
//
// THE GEOMETRY. Retail loads each end (or corner) as a GTE vector v and projects
// view = R * v / 4096 + TR with R = pb->matrix_ViewProj and TR = 0 (weather,
// confetti: v is the offset from the camera along the world axes) or the cloud's
// view position (beaker: v is the offset from the cloud). The engine camera of the
// view is view = R * w / 4096 + t with t = pb->matrix_ViewProj.t
// (Engine_CameraFromPushBuffer), so the world point is w = v + base with
// base = 4096 * R^-1 * (TR - t), once per batch. No GTE, no PS1 screen maths: a
// drop is a world-space line, a confetti piece a world-space quad, both drawn
// with a depth test against the engine track (the PS1 primitives have none and
// show through walls).
//   - a line: a quad 1 PSX pixel wide (built on the screen and taken back to the
//     world at each end's depth, as the world-particle lines of
//     platform/engine_fx_exhaust.c), gouraud between retail's two colours, the
//     blend of the drawer's draw mode (the PS1 blends an untextured
//     semi-transparent primitive as a whole), no depth write. An end nearer than
//     the near plane is moved onto it (colour interpolated).
//   - a confetti piece: retail corners 0, 1, 2, 3 as engine 0, 1, 3, 2, flat
//     colour, opaque, double-sided, depth write.
// Untextured, so every vertex gets the whole-page box (Engine_VertexBoxFull).
//
// BATCHES. One engine draw per drawer call (weather), per beaker pass and per
// confetti call, not one per drop: Begin opens the batch, every hook adds its
// primitive, End (before the drawer links its draw-mode packet) submits it,
// linked into the batch's OT cell (lines: the cell retail links them into;
// confetti: the nearest cell of its pieces). The view's camera is made from the
// same push buffer the frame's collect uses later (Engine_FrameCollect has not
// run yet at this point of the frame; the bank was selected by
// Engine_RenderFrameBegin in MainFrame_ResetDB).
//
// THE RULE (Begin). Taken is every primitive of a drawer call made while the
// engine draws the track: g_cfg_engine and g_cfg_engineTrack (a level the engine
// could not load turns both off, platform/engine.c Engine_LevelBegin), one
// player, not the menu, the level drawn - the conditions of EngTrack_Active that
// the game state shows before the collect - and the engine drew the track in the
// frame before (Engine_TrackDrawnThisFrame still holds the last collect's answer
// here). So the race levels and the podium scene (it loads the engine track,
// TRACK PODIUM in engine.h: its confetti is taken) are in, the hub, whose level
// the engine does not load, and a frame after one the PS1 walk drew are out. The
// first frame of a level is therefore left to the PS1 path. In every other call the hooks answer 0 and count
// nothing: retail links as always.
//
// LEFT TO RETAIL (counted per kind): the batch store is full (the primitive is
// linked at once), no usable camera or a singular matrix at Begin, no material,
// or the scene refused the draw at End. At End the batch's packets are then
// linked in their order into their cells, as retail would have linked them (the
// same OT, the drawer has linked nothing in between). "near cut": both ends of a
// drop nearer than the near plane (retail's projection rejects such a line
// already); taken and not drawn.
//
// HEAT SHIMMER (Torch_Main, PARKED). Retail draws per heat particle 9 POLY_FT3 /
// POLY_FT4 (opaque, 15-bit pages) whose texture coordinates are addresses IN THE
// FRAME BUFFER: the picture already drawn around the particle, read back with the
// rings swapped (radius 1 against radius 2), so it wobbles. The native layer
// serves that with a feedback barrier (platform/native_gpu.c
// NativeGpu_PrepareFramebufferFeedback: the main target, engine pixels included,
// is packed into the VRAM before such a polygon). An engine version needs a copy
// of the engine's colour target in the middle of the frame and a program that
// samples it at screen positions - an architecture question of the renderer, not
// of this file. Until then the PS1 path draws it as before; EngineFx_HeatNote
// counts the particles it draws in a frame with the engine track ("left to
// retail").

#define ENGINE_FX_WEATHER_ITEMS 4096 // primitives per batch (u16 indices: 4 vertices each)
#define ENGINE_FX_WEATHER_MATERIALS 8

#define EFW_KIND_WEATHER 0
#define EFW_KIND_RAIN 1
#define EFW_KIND_CONFETTI 2
#define EFW_KIND_HEAT 3
#define EFW_KINDS 4

#define EFW_WHY_CAMERA 0
#define EFW_WHY_STORE 1
#define EFW_WHY_MATERIAL 2
#define EFW_WHY_SCENE 3
#define EFW_WHY_COUNT 4

#define EFW_LINK_LINE_G2 0x04000000u // the tag retail links a LINE_G2 with
#define EFW_LINK_POLY_F4 0x05000000u // the tag retail links a POLY_F4 with

// The fallback links a packet by its first word, the tag (as retail does).
CTR_STATIC_ASSERT(offsetof(LINE_G2, tag) == 0);
CTR_STATIC_ASSERT(offsetof(POLY_F4, tag) == 0);

typedef struct
{
	float p[4][3]; // a line: ends 0, 1; a quad: corners in retail order 0..3 (world units)
	u8 c[2][4];    // a line: the colour at each end; a quad: c[0]
	u32 *ot;       // the cell word retail links the packet into
	void *packet;  // the written packet (its tag word first)
} EngFxWItem;

typedef struct
{
	int open; // 0 = no batch, 1 = taking, 2 = counting only (refused at Begin)
	int kind;
	int view;
	int blend; // EngineBlend
	int opaque;
	u32 *otBase;
	u32 *otCell; // lines: the batch's cell word
	u32 linkTag;
	EngineCamera cam;
	float inv[9]; // R^-1 in world units per view unit (row-major)
	float base[3];
	u32 count;
} EngFxWBatch;

typedef struct
{
	EngineMaterial wanted;
	int index;
	int used;
} EngFxWMaterial;

global_variable EngFxWBatch s_efwBatch;
global_variable EngFxWItem s_efwItems[ENGINE_FX_WEATHER_ITEMS];
global_variable EngineVertex s_efwVerts[ENGINE_FX_WEATHER_ITEMS * 4];
global_variable u16 s_efwIndices[ENGINE_FX_WEATHER_ITEMS * 6];
global_variable EngFxWMaterial s_efwMat[ENGINE_FX_WEATHER_MATERIALS];
global_variable u32 s_efwDrawn[EFW_KINDS];
global_variable u32 s_efwLeft[EFW_KINDS];
global_variable u32 s_efwTourDrawn[EFW_KINDS];
global_variable u32 s_efwTourLeft[EFW_KINDS];
global_variable u32 s_efwWhy[EFW_WHY_COUNT];
global_variable u32 s_efwTourWhy[EFW_WHY_COUNT];
global_variable u32 s_efwNear;
global_variable u32 s_efwTourNear;
global_variable u32 s_efwDraws;
global_variable u32 s_efwTourDraws;
global_variable int s_efwRegistered;

// --------------------------------------------------------------------------
// Pure parts (also the self test's subjects)

// The rule on plain values: the engine draws the track in this frame
// (trackDrawn: the last collect's Engine_TrackDrawnThisFrame).
internal int EngFxW_Rule(int engine, int engineTrack, int players, u32 gameMode1, u32 renderFlags, int trackDrawn)
{
	const int view = engine && engineTrack && (players == 1) && ((gameMode1 & MAIN_MENU) == 0);

	return (view && ((renderFlags & RENDER_FLAG_DRAW_LEVEL) != 0) && trackDrawn) ? 1 : 0;
}

// A GTE vector as the registers take it: VXY = x in the low, y in the high
// half (each s16), VZ = the low 16 bits as s16.
internal void EngFxW_Vector(u32 xy, u32 z, float out[3])
{
	out[0] = (float)(s16)(u16)(xy & 0xffffu);
	out[1] = (float)(s16)(u16)(xy >> 16);
	out[2] = (float)(s16)(u16)(z & 0xffffu);
}

// R^-1 of a GTE matrix in world units per view unit (4096 * M^-1, row-major).
// 0 when M is singular.
internal int EngFxW_Inverse(const MATRIX *m, float inv[9])
{
	const double a = m->m[0][0], b = m->m[0][1], c = m->m[0][2];
	const double d = m->m[1][0], e = m->m[1][1], f = m->m[1][2];
	const double g = m->m[2][0], h = m->m[2][1], i = m->m[2][2];
	const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
	double k;

	if ((det > -1.0) && (det < 1.0))
	{
		return 0;
	}
	k = 4096.0 / det;
	inv[0] = (float)((e * i - f * h) * k);
	inv[1] = (float)((c * h - b * i) * k);
	inv[2] = (float)((b * f - c * e) * k);
	inv[3] = (float)((f * g - d * i) * k);
	inv[4] = (float)((a * i - c * g) * k);
	inv[5] = (float)((c * d - a * f) * k);
	inv[6] = (float)((d * h - e * g) * k);
	inv[7] = (float)((b * g - a * h) * k);
	inv[8] = (float)((a * e - b * d) * k);
	return 1;
}

internal void EngFxW_MulInv(const float inv[9], float x, float y, float z, float out[3])
{
	out[0] = inv[0] * x + inv[1] * y + inv[2] * z;
	out[1] = inv[3] * x + inv[4] * y + inv[5] * z;
	out[2] = inv[6] * x + inv[7] * y + inv[8] * z;
}

// base = R^-1 * (TR - t): the world point of a retail vector v is v + base.
internal void EngFxW_Base(const MATRIX *m, const float inv[9], const s32 tr[3], float base[3])
{
	EngFxW_MulInv(inv, (float)((double)tr[0] - (double)m->t[0]), (float)((double)tr[1] - (double)m->t[1]), (float)((double)tr[2] - (double)m->t[2]), base);
}

// The blend of a draw-mode word (its abr bits 5..6).
internal int EngFxW_BlendOfDrawMode(u32 drawMode)
{
	return (int)Engine_BlendFromAbr((int)((drawMode >> 5) & 3u));
}

internal void EngFxW_Colour(u32 word, u8 out[4])
{
	out[0] = (u8)(word & 0xffu);
	out[1] = (u8)((word >> 8) & 0xffu);
	out[2] = (u8)((word >> 16) & 0xffu);
	out[3] = 255;
}

// A line from p0 to p1 (world) as a quad halfPx PSX pixels to each side and past
// each end, built on the screen and taken back to the world at each end's depth;
// an end nearer than the near plane moved onto it first. v0 = p0 left, v1 = p1
// left, v2 = p1 right, v3 = p0 right. 0 when both ends are nearer.
internal int EngFxW_LineQuad(const EngineCamera *cam, const float inv[9], const float p0in[3], const float p1in[3], const u8 c0in[4], const u8 c1in[4],
                             float halfPx, EngineVertex v[4])
{
	EngineVec3 end[2];
	EngineVec3 view[2];
	u8 col[2][4];
	const float zMin = cam->zNear * 1.001f;
	float dx;
	float dy;
	float len;

	end[0].x = p0in[0];
	end[0].y = p0in[1];
	end[0].z = p0in[2];
	end[1].x = p1in[0];
	end[1].y = p1in[1];
	end[1].z = p1in[2];
	memcpy(col[0], c0in, 4);
	memcpy(col[1], c1in, 4);
	view[0] = Engine_Mat4MulPoint(&cam->viewFromWorld, end[0]);
	view[1] = Engine_Mat4MulPoint(&cam->viewFromWorld, end[1]);
	if (((view[0].z < zMin) && (view[1].z < zMin)) || (cam->H <= 0.0f))
	{
		return 0;
	}
	for (int e = 0; e < 2; e++)
	{
		const int o = 1 - e;

		if (view[e].z < zMin)
		{
			const float t = (zMin - view[e].z) / (view[o].z - view[e].z);

			end[e].x += (end[o].x - end[e].x) * t;
			end[e].y += (end[o].y - end[e].y) * t;
			end[e].z += (end[o].z - end[e].z) * t;
			for (int ch = 0; ch < 3; ch++)
			{
				col[e][ch] = (u8)((float)col[e][ch] + ((float)col[o][ch] - (float)col[e][ch]) * t + 0.5f);
			}
			view[e] = Engine_Mat4MulPoint(&cam->viewFromWorld, end[e]);
		}
	}
	dx = cam->H * (view[1].x / view[1].z - view[0].x / view[0].z);
	dy = cam->H * (view[1].y / view[1].z - view[0].y / view[0].z);
	len = sqrtf(dx * dx + dy * dy);
	if (len < 1.0e-4f)
	{
		dx = 1.0f;
		dy = 0.0f;
	}
	else
	{
		dx /= len;
		dy /= len;
	}
	for (int c = 0; c < 4; c++)
	{
		const int e = ((c == 1) || (c == 2)) ? 1 : 0;
		const float along = e ? halfPx : -halfPx;
		const float side = (c < 2) ? -halfPx : halfPx;
		const float sx = dx * along - dy * side;
		const float sy = dy * along + dx * side;
		const float perPx = view[e].z / cam->H;
		float w[3];

		EngFxW_MulInv(inv, sx * perPx, sy * perPx, 0.0f, w);
		v[c].position[0] = end[e].x + w[0];
		v[c].position[1] = end[e].y + w[1];
		v[c].position[2] = end[e].z + w[2];
		v[c].texcoord[0] = 0.0f;
		v[c].texcoord[1] = 0.0f;
		memcpy(v[c].color, col[e], 4);
	}
	Engine_VertexBoxFull(v, 4); // untextured (engine.h, THE UV BOX)
	return 1;
}

// A confetti piece: retail corners 0, 1, 2, 3 (a PS1 quad: 0-1-2, 1-2-3) as
// engine corners 0, 1, 3, 2 (a fan 0-1-2, 0-2-3), flat colour.
internal void EngFxW_QuadVerts(const float p[4][3], const u8 colour[4], EngineVertex v[4])
{
	static const int fromRetail[4] = {0, 1, 3, 2};

	for (int c = 0; c < 4; c++)
	{
		const float *q = p[fromRetail[c]];

		v[c].position[0] = q[0];
		v[c].position[1] = q[1];
		v[c].position[2] = q[2];
		v[c].texcoord[0] = 0.0f;
		v[c].texcoord[1] = 0.0f;
		memcpy(v[c].color, colour, 4);
	}
	Engine_VertexBoxFull(v, 4); // untextured (engine.h, THE UV BOX)
}

// --------------------------------------------------------------------------
// Counters and the log lines

internal const char *EngFxW_KindName(int kind)
{
	static const char *const names[EFW_KINDS] = {"weather", "rain", "confetti", "heat"};

	return ((kind >= 0) && (kind < EFW_KINDS)) ? names[kind] : "?";
}

internal void EngFxW_TourReport(int entry, int levelID, int phase)
{
	if (phase == 0)
	{
		memcpy(s_efwTourDrawn, s_efwDrawn, sizeof(s_efwTourDrawn));
		memcpy(s_efwTourLeft, s_efwLeft, sizeof(s_efwTourLeft));
		memcpy(s_efwTourWhy, s_efwWhy, sizeof(s_efwTourWhy));
		s_efwTourNear = s_efwNear;
		s_efwTourDraws = s_efwDraws;
		return;
	}
	Platform_Log("[CTR Engine] tour %d level %d weather/rain/confetti/heat: engine %u/%u/%u/%u, left to retail %u/%u/%u/%u (draws %u, near cut %u; "
	             "left: camera %u, store %u, material %u, scene %u; heat is parked)\n",
	             entry, levelID, s_efwDrawn[0] - s_efwTourDrawn[0], s_efwDrawn[1] - s_efwTourDrawn[1], s_efwDrawn[2] - s_efwTourDrawn[2],
	             s_efwDrawn[3] - s_efwTourDrawn[3], s_efwLeft[0] - s_efwTourLeft[0], s_efwLeft[1] - s_efwTourLeft[1], s_efwLeft[2] - s_efwTourLeft[2],
	             s_efwLeft[3] - s_efwTourLeft[3], s_efwDraws - s_efwTourDraws, s_efwNear - s_efwTourNear, s_efwWhy[0] - s_efwTourWhy[0],
	             s_efwWhy[1] - s_efwTourWhy[1], s_efwWhy[2] - s_efwTourWhy[2], s_efwWhy[3] - s_efwTourWhy[3]);
}

internal void EngFxW_Report(void)
{
	Platform_Log("[CTR Engine] weather/rain/confetti/heat: engine %u/%u/%u/%u, left to retail %u/%u/%u/%u (draws %u, near cut %u; "
	             "left: camera %u, store %u, material %u, scene %u; heat is parked)\n",
	             s_efwDrawn[0], s_efwDrawn[1], s_efwDrawn[2], s_efwDrawn[3], s_efwLeft[0], s_efwLeft[1], s_efwLeft[2], s_efwLeft[3], s_efwDraws, s_efwNear,
	             s_efwWhy[0], s_efwWhy[1], s_efwWhy[2], s_efwWhy[3]);
}

// Once, at the first hook of a run with the engine. RenderWeather runs in every
// race frame (RENDER_FLAG_RAIN is set with the race flags, also without drops),
// so the tour gets a line for every track of a race tour.
internal void EngFxW_Register(void)
{
	if (s_efwRegistered)
	{
		return;
	}
	s_efwRegistered = 1;
	Engine_TourRegister(EngFxW_TourReport);
	Platform_AtExitReport(EngFxW_Report);
}

// --------------------------------------------------------------------------
// Materials: untextured, cached per level (the table resets at level unload; a
// cached index is checked against the table, as the particle materials are).

internal int EngFxW_Material(int blend, int opaque)
{
	EngineMaterial want;
	int freeSlot = -1;
	int index;

	memset(&want, 0, sizeof(want));
	want.flags = ENGINE_MATERIAL_DOUBLE_SIDED | (opaque ? 0u : ENGINE_MATERIAL_NO_DEPTH_WRITE);
	want.tint[0] = want.tint[1] = want.tint[2] = want.tint[3] = 1.0f;
	want.texSource = ENGINE_TEX_NONE;
	want.blend = (u8)(opaque ? ENGINE_BLEND_OPAQUE : blend);

	for (int slot = 0; slot < ENGINE_FX_WEATHER_MATERIALS; slot++)
	{
		EngFxWMaterial *c = &s_efwMat[slot];
		const EngineMaterial *have;

		if (!c->used || (memcmp(&c->wanted, &want, sizeof(want)) != 0))
		{
			continue;
		}
		have = Engine_Material(c->index);
		if ((have != NULL) && (memcmp(have, &want, sizeof(want)) == 0))
		{
			return c->index;
		}
		c->used = 0; // same key, the table was reset: register again
	}
	for (int slot = 0; slot < ENGINE_FX_WEATHER_MATERIALS; slot++)
	{
		const EngineMaterial *have = s_efwMat[slot].used ? Engine_Material(s_efwMat[slot].index) : NULL;

		if (!s_efwMat[slot].used || (have == NULL) || (memcmp(have, &s_efwMat[slot].wanted, sizeof(want)) != 0))
		{
			freeSlot = slot;
			break;
		}
	}
	if (freeSlot < 0)
	{
		return -1;
	}
	index = Engine_MaterialRegister(&want);
	if (index < 0)
	{
		s_efwMat[freeSlot].used = 0;
		return -1;
	}
	s_efwMat[freeSlot].wanted = want;
	s_efwMat[freeSlot].index = index;
	s_efwMat[freeSlot].used = 1;
	return index;
}

// --------------------------------------------------------------------------
// The batch

// Links every packet of the batch into its cell, in the order retail would have.
internal void EngFxW_LinkAll(void)
{
	for (u32 i = 0; i < s_efwBatch.count; i++)
	{
		EngFxWItem *it = &s_efwItems[i];

		CtrGpu_LinkPacket24(it->ot, (u32 *)it->packet, it->packet, s_efwBatch.linkTag);
	}
}

internal void EngFxW_Begin(int kind, const struct PushBuffer *pb, const s32 tr[3], int blend, int opaque, u32 *otCell, u32 linkTag)
{
	const struct GameTracker *gGT = sdata->gGT;
	int view;

	EngFxW_Register();
	if (s_efwBatch.open != 0)
	{
		EngineFx_WeatherEnd(); // a batch left open is finished first
	}
	memset(&s_efwBatch, 0, sizeof(s_efwBatch));
	if ((gGT == NULL) || (pb == NULL) || !EngFxW_Rule(g_cfg_engine, g_cfg_engineTrack, gGT->numPlyrCurrGame, gGT->gameMode1, gGT->renderFlags,
	                                                    Engine_TrackDrawnThisFrame()))
	{
		return;
	}
	s_efwBatch.open = 2;
	s_efwBatch.kind = kind;
	s_efwBatch.blend = blend;
	s_efwBatch.opaque = opaque;
	s_efwBatch.linkTag = linkTag;
	view = (int)(pb - gGT->pushBuffer);
	if ((view < 0) || (view >= ENGINE_VIEWS) || (view >= gGT->numPlyrCurrGame) || (pb->ptrOT == NULL) ||
	    !Engine_CameraFromPushBuffer(pb, view, &s_efwBatch.cam) || !EngFxW_Inverse(&pb->matrix_ViewProj, s_efwBatch.inv))
	{
		return; // counting only: every primitive is linked by retail (reason camera)
	}
	EngFxW_Base(&pb->matrix_ViewProj, s_efwBatch.inv, tr, s_efwBatch.base);
	s_efwBatch.view = view;
	s_efwBatch.otBase = pb->ptrOT;
	s_efwBatch.otCell = otCell;
	if ((otCell != NULL) && ((otCell < pb->ptrOT) || (otCell > pb->ptrOT + 0x3ff)))
	{
		return;
	}
	s_efwBatch.open = 1;
}

// A primitive the batch cannot take: retail links it (answer 0), counted.
internal int EngFxW_Left(void)
{
	s_efwLeft[s_efwBatch.kind]++;
	s_efwWhy[(s_efwBatch.open == 2) ? EFW_WHY_CAMERA : EFW_WHY_STORE]++;
	return 0;
}

void EngineFx_WeatherBegin(const struct PushBuffer *pb, u32 drawMode, u32 *otCell)
{
	static const s32 zero[3] = {0, 0, 0};

	EngFxW_Begin(EFW_KIND_WEATHER, pb, zero, EngFxW_BlendOfDrawMode(drawMode), 0, otCell, EFW_LINK_LINE_G2);
}

void EngineFx_BeakerRainBegin(const struct PushBuffer *pb, s32 trX, s32 trY, s32 trZ, u32 drawMode, u32 *otCell)
{
	const s32 tr[3] = {trX, trY, trZ};

	EngFxW_Begin(EFW_KIND_RAIN, pb, tr, EngFxW_BlendOfDrawMode(drawMode), 0, otCell, EFW_LINK_LINE_G2);
}

void EngineFx_ConfettiBegin(const struct PushBuffer *pb)
{
	static const s32 zero[3] = {0, 0, 0};

	EngFxW_Begin(EFW_KIND_CONFETTI, pb, zero, (int)ENGINE_BLEND_OPAQUE, 1, NULL, EFW_LINK_POLY_F4);
}

int EngineFx_WeatherLine(void *packet, u32 xy0, u32 z0, u32 xy1, u32 z1, u32 color0, u32 color1)
{
	EngFxWItem *it;
	float v[3];

	if ((s_efwBatch.open == 0) || (s_efwBatch.kind == EFW_KIND_CONFETTI))
	{
		return 0;
	}
	if ((s_efwBatch.open != 1) || (s_efwBatch.count >= ENGINE_FX_WEATHER_ITEMS) || (packet == NULL) || (s_efwBatch.otCell == NULL))
	{
		return EngFxW_Left();
	}
	it = &s_efwItems[s_efwBatch.count++];
	EngFxW_Vector(xy0, z0, v);
	for (int a = 0; a < 3; a++)
	{
		it->p[0][a] = v[a] + s_efwBatch.base[a];
	}
	EngFxW_Vector(xy1, z1, v);
	for (int a = 0; a < 3; a++)
	{
		it->p[1][a] = v[a] + s_efwBatch.base[a];
	}
	EngFxW_Colour(color0, it->c[0]);
	EngFxW_Colour(color1, it->c[1]);
	it->ot = s_efwBatch.otCell;
	it->packet = packet;
	return 1;
}

int EngineFx_ConfettiQuad(void *packet, u32 *otCell, const u32 xy[4], const s32 z[4], u32 color)
{
	EngFxWItem *it;
	float v[3];

	if ((s_efwBatch.open == 0) || (s_efwBatch.kind != EFW_KIND_CONFETTI))
	{
		return 0;
	}
	if ((s_efwBatch.open != 1) || (s_efwBatch.count >= ENGINE_FX_WEATHER_ITEMS) || (packet == NULL) || (otCell == NULL) || (otCell < s_efwBatch.otBase) ||
	    (otCell > s_efwBatch.otBase + 0x3ff))
	{
		return EngFxW_Left();
	}
	it = &s_efwItems[s_efwBatch.count++];
	for (int c = 0; c < 4; c++)
	{
		EngFxW_Vector(xy[c], (u32)z[c], v);
		for (int a = 0; a < 3; a++)
		{
			it->p[c][a] = v[a] + s_efwBatch.base[a];
		}
	}
	EngFxW_Colour(color, it->c[0]);
	it->ot = otCell;
	it->packet = packet;
	return 1;
}

void EngineFx_WeatherEnd(void)
{
	EngineDraw draw;
	u32 verts = 0;
	u32 nearCut = 0;
	int cell = 0x3ff;
	int material;
	float zSum = 0.0f;

	if ((s_efwBatch.open != 1) || (s_efwBatch.count == 0u))
	{
		s_efwBatch.open = 0;
		return;
	}
	for (u32 i = 0; i < s_efwBatch.count; i++)
	{
		const EngFxWItem *it = &s_efwItems[i];
		EngineVertex *v = &s_efwVerts[verts];
		EngineVec3 p;

		if (s_efwBatch.kind == EFW_KIND_CONFETTI)
		{
			const int itemCell = (int)(it->ot - s_efwBatch.otBase);

			EngFxW_QuadVerts(it->p, it->c[0], v);
			cell = (itemCell < cell) ? itemCell : cell;
		}
		else if (!EngFxW_LineQuad(&s_efwBatch.cam, s_efwBatch.inv, it->p[0], it->p[1], it->c[0], it->c[1], 0.5f, v))
		{
			nearCut++;
			continue;
		}
		p.x = it->p[0][0];
		p.y = it->p[0][1];
		p.z = it->p[0][2];
		zSum += Engine_Mat4MulPoint(&s_efwBatch.cam.viewFromWorld, p).z;
		for (int k = 0; k < 6; k++)
		{
			static const u16 quad[6] = {0, 1, 2, 0, 2, 3};

			s_efwIndices[(verts / 4u) * 6u + (u32)k] = (u16)(verts + quad[k]);
		}
		verts += 4;
	}
	if (s_efwBatch.kind != EFW_KIND_CONFETTI)
	{
		cell = (int)(s_efwBatch.otCell - s_efwBatch.otBase);
	}
	if (verts == 0u)
	{
		s_efwNear += nearCut;
		s_efwBatch.open = 0;
		return;
	}
	material = EngFxW_Material(s_efwBatch.blend, s_efwBatch.opaque);
	if (material >= 0)
	{
		memset(&draw, 0, sizeof(draw));
		draw.mesh = -1;
		Engine_CameraScreenFromWorld(&s_efwBatch.cam, &draw.clipFromModel);
		draw.tint[0] = draw.tint[1] = draw.tint[2] = draw.tint[3] = 1.0f;
		draw.materialOverride = -1;
		draw.sortDepth = zSum / (float)(verts / 4u);
		draw.otCell = (u32)cell;
		draw.flags = s_efwBatch.opaque ? 0 : ENGINE_DRAW_TRANSPARENT;
		draw.dynVertices = s_efwVerts;
		draw.dynVertexCount = verts;
		draw.dynIndices = s_efwIndices;
		draw.dynIndexCount = (verts / 4u) * 6u;
		draw.dynMaterial = material;
		if (Engine_RenderSubmitLinked(s_efwBatch.view, &draw, s_efwBatch.otBase, cell))
		{
			s_efwDrawn[s_efwBatch.kind] += s_efwBatch.count - nearCut;
			s_efwNear += nearCut;
			s_efwDraws++;
			s_efwBatch.open = 0;
			return;
		}
	}
	// Not drawn: retail's packets go into their cells as retail would have linked them.
	EngFxW_LinkAll();
	s_efwLeft[s_efwBatch.kind] += s_efwBatch.count;
	s_efwWhy[(material < 0) ? EFW_WHY_MATERIAL : EFW_WHY_SCENE] += s_efwBatch.count;
	s_efwBatch.open = 0;
}

void EngineFx_HeatNote(void)
{
	EngFxW_Register();
	if (g_cfg_engine && Engine_TrackDrawnThisFrame())
	{
		s_efwLeft[EFW_KIND_HEAT]++;
	}
}

// --------------------------------------------------------------------------
// Selftest: made-up cameras and primitives, no game state.

internal void EngFxW_Check(int ok, const char *what, int *checks, int *failures)
{
	(*checks)++;
	if (!ok)
	{
		(*failures)++;
		printf("engine selftest weather: FAILED %s\n", what);
	}
}

internal float EngFxW_Abs(float v)
{
	return (v < 0.0f) ? -v : v;
}

// A push buffer with a yawed, pitched view whose Y row is squeezed (as the
// game's), a translation and a 320 x 240 view.
internal void EngFxW_TestPushBuffer(struct PushBuffer *pb, float yaw, float pitch, s32 tx, s32 ty, s32 tz)
{
	const float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
	// view rows: x = (cy, 0, -sy); y = squeezed (sy*sp, cp, cy*sp) * 0.75; z = (sy*cp, -sp, cy*cp)
	const float r[3][3] = {{cy, 0.0f, -sy}, {0.75f * sy * sp, 0.75f * cp, 0.75f * cy * sp}, {sy * cp, -sp, cy * cp}};

	memset(pb, 0, sizeof(*pb));
	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			pb->matrix_ViewProj.m[i][j] = (s16)lrintf(r[i][j] * 4096.0f);
		}
	}
	pb->matrix_ViewProj.t[0] = tx;
	pb->matrix_ViewProj.t[1] = ty;
	pb->matrix_ViewProj.t[2] = tz;
	pb->rect.w = 320;
	pb->rect.h = 240;
	pb->distanceToScreen_PREV = 256;
}

// Retail's view point of vector v with translation tr (GTE: R v / 4096 + TR).
internal void EngFxW_TestRetailView(const MATRIX *m, const float v[3], const s32 tr[3], float out[3])
{
	for (int r = 0; r < 3; r++)
	{
		out[r] = ((float)m->m[r][0] * v[0] + (float)m->m[r][1] * v[1] + (float)m->m[r][2] * v[2]) / 4096.0f + (float)tr[r];
	}
}

internal int EngFxW_TestScreen(const EngineCamera *cam, const float w[3], float *sx, float *sy)
{
	EngineVec3 p;
	EngineVec3 s;

	p.x = w[0];
	p.y = w[1];
	p.z = w[2];
	if (!Engine_ProjectPoint(cam, p, &s))
	{
		return 0;
	}
	*sx = s.x;
	*sy = s.y;
	return 1;
}

int Engine_SelfTestWeather(int *checks)
{
	int failures = 0;
	struct PushBuffer pb;
	EngineCamera cam;
	float inv[9];
	float base[3];

	// THE RULE: engine, engine track, one player, not the menu, the level drawn.
	EngFxW_Check(EngFxW_Rule(1, 1, 1, 0, RENDER_FLAG_DRAW_LEVEL, 1) == 1, "rule: a one-player race frame of the engine track", checks, &failures);
	EngFxW_Check(EngFxW_Rule(0, 1, 1, 0, RENDER_FLAG_DRAW_LEVEL, 1) == 0, "rule: no engine", checks, &failures);
	EngFxW_Check(EngFxW_Rule(1, 0, 1, 0, RENDER_FLAG_DRAW_LEVEL, 1) == 0, "rule: no engine track", checks, &failures);
	EngFxW_Check(EngFxW_Rule(1, 1, 2, 0, RENDER_FLAG_DRAW_LEVEL, 1) == 0, "rule: two players", checks, &failures);
	EngFxW_Check(EngFxW_Rule(1, 1, 1, MAIN_MENU, RENDER_FLAG_DRAW_LEVEL, 1) == 0, "rule: the menu", checks, &failures);
	EngFxW_Check(EngFxW_Rule(1, 1, 1, 0, RENDER_FLAG_RAIN, 1) == 0, "rule: the level not drawn", checks, &failures);
	EngFxW_Check(EngFxW_Rule(1, 1, 1, 0, RENDER_FLAG_DRAW_LEVEL, 0) == 0, "rule: the last collect left the track to the PS1 path (the hub)", checks,
	             &failures);

	// THE VECTOR as the GTE registers take it.
	{
		float v[3];

		EngFxW_Vector(0xfffe0010u, 0x0000fff0u, v);
		EngFxW_Check((v[0] == 16.0f) && (v[1] == -2.0f) && (v[2] == -16.0f), "vector: s16 halves and the low half of z", checks, &failures);
		EngFxW_Vector(0x7fff8000u, 0xabcd0005u, v);
		EngFxW_Check((v[0] == -32768.0f) && (v[1] == 32767.0f) && (v[2] == 5.0f), "vector: limits, z ignores its high half", checks, &failures);
	}

	// THE BLEND of the draw modes: the beaker's two passes, an abr-0 weather mode.
	EngFxW_Check(EngFxW_BlendOfDrawMode(0xe1000a20u) == (int)ENGINE_BLEND_PSX_ADD, "blend: beaker pass 1 is abr 1 (add)", checks, &failures);
	EngFxW_Check(EngFxW_BlendOfDrawMode(0xe1000a40u) == (int)ENGINE_BLEND_PSX_SUBTRACT, "blend: beaker pass 2 is abr 2 (subtract)", checks, &failures);
	EngFxW_Check(EngFxW_BlendOfDrawMode(0xe1000000u) == (int)ENGINE_BLEND_PSX_AVERAGE, "blend: abr 0 (average)", checks, &failures);
	EngFxW_Check(EngFxW_BlendOfDrawMode(0xe1000060u) == (int)ENGINE_BLEND_PSX_ADD_QUARTER, "blend: abr 3 (add a quarter)", checks, &failures);

	// THE GEOMETRY: v + base seen by the engine camera is retail's view point, for
	// TR = 0 (weather, confetti) and a cloud's TR (beaker), several cameras.
	{
		static const float poses[3][2] = {{0.3f, 0.1f}, {2.4f, -0.25f}, {-1.1f, 0.4f}};
		static const s32 trs[2][3] = {{0, 0, 0}, {-120, 35, 900}};
		static const float vs[4][3] = {{0.0f, 0.0f, 0.0f}, {300.0f, -40.0f, 700.0f}, {-1024.0f, 1000.0f, -512.0f}, {12.0f, -510.0f, 127.0f}};
		float worst = 0.0f;
		int ok = 1;

		for (int pose = 0; pose < 3; pose++)
		{
			EngFxW_TestPushBuffer(&pb, poses[pose][0], poses[pose][1], 1234 * (pose + 1), -567, 89012 - 40000 * pose);
			ok &= Engine_CameraFromPushBuffer(&pb, 0, &cam);
			ok &= EngFxW_Inverse(&pb.matrix_ViewProj, inv);
			for (int t = 0; t < 2; t++)
			{
				EngFxW_Base(&pb.matrix_ViewProj, inv, trs[t], base);
				for (int k = 0; k < 4; k++)
				{
					float want[3];
					EngineVec3 w;
					EngineVec3 got;

					EngFxW_TestRetailView(&pb.matrix_ViewProj, vs[k], trs[t], want);
					w.x = vs[k][0] + base[0];
					w.y = vs[k][1] + base[1];
					w.z = vs[k][2] + base[2];
					got = Engine_Mat4MulPoint(&cam.viewFromWorld, w);
					worst = fmaxf(worst, fmaxf(EngFxW_Abs(got.x - want[0]), fmaxf(EngFxW_Abs(got.y - want[1]), EngFxW_Abs(got.z - want[2]))));
				}
			}
		}
		EngFxW_Check(ok, "geometry: cameras and inverses made", checks, &failures);
		EngFxW_Check(worst < 0.05f, "geometry: v + base lands on retail's view point (< 0.05 view units)", checks, &failures);
	}
	{
		MATRIX singular;

		memset(&singular, 0, sizeof(singular));
		singular.m[0][0] = 4096;
		singular.m[1][0] = 4096;
		singular.m[2][2] = 4096;
		EngFxW_Check(EngFxW_Inverse(&singular, inv) == 0, "geometry: a singular matrix is refused", checks, &failures);
	}

	// THE LINE: ends on retail's screen points, 1 PSX pixel wide, colours per end,
	// whole-page box; an end behind the near plane moved onto it; both behind: none.
	{
		static const u8 top[4] = {200, 210, 220, 255};
		static const u8 bottom[4] = {10, 20, 30, 255};
		EngineVertex v[4];
		float p0[3];
		float p1[3];
		float s[4][2];
		float e0x, e0y, e1x, e1y;
		int ok;

		EngFxW_TestPushBuffer(&pb, 0.7f, 0.05f, 500, -300, 2000);
		Engine_CameraFromPushBuffer(&pb, 0, &cam);
		EngFxW_Inverse(&pb.matrix_ViewProj, inv);
		{
			static const s32 zero[3] = {0, 0, 0};

			EngFxW_Base(&pb.matrix_ViewProj, inv, zero, base);
		}
		// a drop 60 units long, about 900 units ahead of the camera: v = R^-1 (view - t)
		{
			float a[3];
			float b[3];

			EngFxW_MulInv(inv, 30.0f, -20.0f, 900.0f, a);
			EngFxW_MulInv(inv, 30.0f, 40.0f, 905.0f, b);
			for (int k = 0; k < 3; k++)
			{
				p0[k] = a[k] + base[k];
				p1[k] = b[k] + base[k];
			}
		}
		ok = EngFxW_LineQuad(&cam, inv, p0, p1, top, bottom, 0.5f, v);
		EngFxW_Check(ok == 1, "line: built", checks, &failures);
		for (int c = 0; c < 4; c++)
		{
			ok &= EngFxW_TestScreen(&cam, v[c].position, &s[c][0], &s[c][1]);
		}
		EngFxW_TestScreen(&cam, p0, &e0x, &e0y);
		EngFxW_TestScreen(&cam, p1, &e1x, &e1y);
		{
			const float w0 = sqrtf((s[0][0] - s[3][0]) * (s[0][0] - s[3][0]) + (s[0][1] - s[3][1]) * (s[0][1] - s[3][1]));
			const float w1 = sqrtf((s[1][0] - s[2][0]) * (s[1][0] - s[2][0]) + (s[1][1] - s[2][1]) * (s[1][1] - s[2][1]));
			const float m0x = 0.5f * (s[0][0] + s[3][0]), m0y = 0.5f * (s[0][1] + s[3][1]);
			const float m1x = 0.5f * (s[1][0] + s[2][0]), m1y = 0.5f * (s[1][1] + s[2][1]);
			const float l0 = sqrtf((m0x - e0x) * (m0x - e0x) + (m0y - e0y) * (m0y - e0y));
			const float l1 = sqrtf((m1x - e1x) * (m1x - e1x) + (m1y - e1y) * (m1y - e1y));

			EngFxW_Check(ok && (EngFxW_Abs(w0 - 1.0f) < 0.02f) && (EngFxW_Abs(w1 - 1.0f) < 0.02f), "line: 1 PSX pixel wide at both ends", checks, &failures);
			EngFxW_Check(ok && (EngFxW_Abs(l0 - 0.5f) < 0.02f) && (EngFxW_Abs(l1 - 0.5f) < 0.02f), "line: half a pixel past each end", checks, &failures);
		}
		EngFxW_Check((memcmp(v[0].color, top, 4) == 0) && (memcmp(v[3].color, top, 4) == 0) && (memcmp(v[1].color, bottom, 4) == 0) &&
		                 (memcmp(v[2].color, bottom, 4) == 0),
		             "line: retail's colour at each end", checks, &failures);
		{
			int box[4];

			Engine_VertexBoxGet(&v[2], box);
			EngFxW_Check((box[0] == 0) && (box[1] == 0) && (box[2] == 255) && (box[3] == 255), "line: whole-page box", checks, &failures);
		}

		// one end behind the near plane (zNear = 32 here)
		{
			float a[3];
			EngineVec3 q;
			EngineVec3 qv;

			EngFxW_MulInv(inv, 0.0f, 0.0f, 10.0f, a);
			for (int k = 0; k < 3; k++)
			{
				p1[k] = a[k] + base[k];
			}
			ok = EngFxW_LineQuad(&cam, inv, p0, p1, top, bottom, 0.5f, v);
			q.x = 0.5f * (v[1].position[0] + v[2].position[0]);
			q.y = 0.5f * (v[1].position[1] + v[2].position[1]);
			q.z = 0.5f * (v[1].position[2] + v[2].position[2]);
			qv = Engine_Mat4MulPoint(&cam.viewFromWorld, q);
			EngFxW_Check(ok && (qv.z >= cam.zNear) && (qv.z < cam.zNear * 1.05f), "line: a near end is moved onto the near plane", checks, &failures);
			EngFxW_Check(ok && (v[1].color[0] > bottom[0]) && (v[1].color[0] < top[0]), "line: its colour interpolated", checks, &failures);
			memcpy(p0, p1, sizeof(p0));
			EngFxW_Check(EngFxW_LineQuad(&cam, inv, p0, p1, top, bottom, 0.5f, v) == 0, "line: both ends behind the near plane: none", checks, &failures);
		}
	}

	// THE CONFETTI PIECE: retail 0, 1, 2, 3 -> engine 0, 1, 3, 2, flat colour, whole-page box.
	{
		const float p[4][3] = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}, {10, 11, 12}};
		u8 colour[4];
		EngineVertex v[4];
		int box[4];

		EngFxW_Colour(0x28c03010u, colour);
		EngFxW_QuadVerts(p, colour, v);
		EngFxW_Check((v[0].position[0] == 1.0f) && (v[1].position[0] == 4.0f) && (v[2].position[0] == 10.0f) && (v[3].position[0] == 7.0f),
		             "confetti: corner order 0, 1, 3, 2", checks, &failures);
		EngFxW_Check((colour[0] == 0x10) && (colour[1] == 0x30) && (colour[2] == 0xc0) && (colour[3] == 255) && (memcmp(v[3].color, colour, 4) == 0),
		             "confetti: the code byte is not a colour, flat colour on every corner", checks, &failures);
		Engine_VertexBoxGet(&v[0], box);
		EngFxW_Check((box[0] == 0) && (box[1] == 0) && (box[2] == 255) && (box[3] == 255), "confetti: whole-page box", checks, &failures);
	}

	// THE HOOKS without a batch (the state of every run without the engine): 0, nothing counted.
	{
		u32 left[EFW_KINDS];
		u32 packet[5] = {0, 0, 0, 0, 0};
		const u32 xy[4] = {0, 0, 0, 0};
		const s32 z[4] = {0, 0, 0, 0};
		const EngFxWBatch saved = s_efwBatch;

		memcpy(left, s_efwLeft, sizeof(left));
		s_efwBatch.open = 0;
		EngFxW_Check((EngineFx_WeatherLine(packet, 0, 0, 0, 0, 0, 0) == 0) && (EngineFx_ConfettiQuad(packet, packet, xy, z, 0) == 0) &&
		                 (memcmp(left, s_efwLeft, sizeof(left)) == 0),
		             "hooks: no batch, retail links, nothing counted", checks, &failures);
		s_efwBatch = saved;
	}

	return failures;
}
