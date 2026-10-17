#include <platform/engine.h>
#include <math.h>
#include <stdio.h>

// platform/native_render_layer.c: the measuring switch --native-hide-exhaust.
int NativeRenderLayer_HideExhaustQuad(const struct Particle *particle);

// ENGINE FX EXHAUST - the exhaust smoke of custom drivers drawn by the engine (step 4).
// See include/platform/engine.h, EFFECTS OF CUSTOM DRIVERS.
//
// DRAW TIME ONLY. The game births and simulates the exhaust particles exactly as
// before (the emitter uses the RNG). The engine computes the sprite from the
// PARTICLE STATE; it neither reads the retail POLY_FT4 nor uses the GTE.
// Particle_RenderList (game/Particle.c) calls EngineFx_ExhaustSprite for every
// driver-local particle, right after NativeChar_ExhaustDraw (the particle position
// with the custom look's offset is known, no GTE call has been made yet), and goes
// to the next particle when it answers 1.
//
// WHAT THE RETAIL CODE DOES (and what the engine takes from it)
//   position : axis[0..2].startVal >> 6 = world * 4 ("4x space"), plus for a
//              driver-local particle inst->matrix.t << 2 (Y skipped with IGNORE_Y).
//              The engine uses (that + the look's offset) / 4 as the world centre.
//   gates    : DRAW_SUCCESSFUL of the instance for this view; the near-camera box
//              |pos - cameraPos*4| < 30001 per axis; depth < 0; renderDepthLimit<<2
//              < depth (depth = 4 * view z). The engine tests the same with the
//              engine camera (cameraOffset is the scratch-pad value of the caller).
//   size     : halfW = (u1-u0+1) << 1, halfH = (v3-v0+1) << 1 (<< 4 with LARGE_QUAD)
//              in 4x VIEW units. Retail matrix: r11 = 0x2000 (2.0), r22 = 0x1000
//              (1.0); with SCALE_X: r11 = scaleX << 1, r22 = scaleX; with ROT_Y the
//              corner (a, b) is rotated by the angle and x is doubled afterwards.
//              The corners are added to the view position directly, so the sprite
//              is a screen-aligned quad of a WORLD size (it shrinks with distance).
//   colour   : Particle_SetColors, which is also the only game-state write of this
//              path (it writes the clamped colour axis back; see the proof below).
//
// WHAT THE ENGINE DRAWS: a camera-facing billboard (Engine_FxBillboard) in world
// space, projected by the engine camera.
//   Retail, a corner (a, b) with s = scaleX / 4096 (1 without SCALE_X) and
//   (c, n) = (cos, sin) of ROT_Y, at view depth z (world units):
//       X = H * 2s * (c*a + n*b) / (4z)        Y = H * s * (c*b - n*a) / (4z)
//   (the 4 of "4z" is the 4x space; it cancels against the 4x of the sizes). At
//   roll 0 the sprite is  W = H * s * halfW / z  wide and  Hh = H * s * halfH / (2z)
//   high, in pixels.
//   The engine camera projects a billboard of world half sizes (hw, hh) to
//       W = 2 * H * |row0| * hw / z      Hh = 2 * H * |row1| * hh / z
//   where row0 / row1 are the rows of the view matrix. Engine_FxBillboard
//   normalises the rows to find the camera axes, the projection does not:
//   |row0| = 1 and |row1| = 9/16 (PushBuffer_SetMatrixVP squeezes the Y row by
//   0x360/0x600 for the 512 x 216 canvas). Equating gives
//       hw = s * halfW / (2 * |row0|)          hh = s * halfH / (4 * |row1|)
//   The retail X factor 2 (r11 = 2.0 against r22 = 1.0) is the game's own
//   compensation of that squeeze (9/16 is nearly 1/2): in the squeezed view space
//   a sprite has to be twice as wide as high to look square on the display. The
//   engine sprite lives in the unsqueezed world plane, so the factor appears as
//   hw : hh = (1/2) : (1 / (4 * 9/16)) = 1.125 : 1 in world units for a square
//   icon. The rows are measured from the camera instead of assuming 9/16, so
//   roll 0 stays exact for any squeeze.
//   ROLL: retail rotates the corner offsets in the squeezed view space and then
//   doubles x; the engine rotates the billboard in the world plane. These are the
//   same rotation for a squeeze of exactly 1/2; with 9/16 the rotated sprite's
//   extent differs by up to 12.5 per cent from retail's. The sign: a positive
//   retail ROT_Y turns the sprite counter-clockwise on the screen, Engine_FxBillboard's
//   positive roll clockwise, so roll = -(ROT_Y & 0xfff).
//
// DEPTH AND CELL (phase-1 transition channel only): the view z of the centre (world
// units) is the sort depth. The OT cell is the one Particle_RenderList_LinkAndAdvance
// would pick for depth = 4 * view z (Particle_NativeOtSlot): with an idpp
// depth >> 5 clamped to its depthOffset range, in idpp->otRangeNormal; without one
// (PUSHBUFFER_EXISTS) (depth >> 8) + otIndexOffset clamped to 0..0x3ff in the view's
// own table. Neither pointer needs the GTE: otRangeNormal is a field of the
// instance's per-view record and the default table is pb->ptrOT (scratch->ot of
// the caller).
//
// THE GAME-STATE PROOF (clamp write-back). Particle_SetColors clamps every used
// colour axis to [0, 0xff00] and writes the clamped value back
// (Particle_BitwiseClampByte). The engine takes exactly what EngineFx_ExhaustOwned
// accepts: driver-local, owner set, driverID -1, icon group 1, 7 or 8. The only
// producers of such particles are the exhaust emitters (VehEmitter_Exhaust; the
// burn smoke, mud and wake emitters set a driverID, the sparks and terrain
// emitters use icon group 0 or 13; group 8 is the bubble pop the water exhaust
// turns into in Particle_FuncPtr_ExhaustUnderwater):
//   emitter (game/zGlobal_DATA.c)  group  colour axis 7: start / velocity  lifespan
//   emSet_Exhaust_High   (~7278)   1      16384 / -2560                    5
//   emSet_Exhaust_Med    (~7381)   1      32768 / -4608                    5
//   emSet_Exhaust_Low    (~7481)   1      40960 / -6144                    5
//   emSet_Exhaust_Water  (~7192)   7 -> 8 none (no colour axis)            30
// The colour axes 8 and 9 are never initialised, so G = B = R. The colour entry
// has flags 3 (base start + base velocity: no accel, no random, no oscillator) and
// nothing else writes it (VehEmitter_Exhaust touches axes 0..2 and flagsSetColor).
// Particle_UpdateList adds the velocity once per tick and destroys the particle
// when framesLeftInLife reaches -1, so lifespan 5 means at most 5 additions:
// High 16384 - 5 * 2560 = 3584, Med 32768 - 5 * 4608 = 9728, Low
// 40960 - 5 * 6144 = 10240. Every value stays inside [0, 0xff00], so the clamp
// never changes a stored value. The water exhaust has no COLOR_R axis flag, so
// Particle_SetColors never enters its colour branch for it and writes nothing.
// The engine still calls Particle_SetColors for every particle it takes, at the
// point where retail calls it (after the gates), so even this no-op write happens
// as before. Engine_SelfTestExhaust checks the table against the emitter data.
// The other retail writes are particle->ptrIconArray (icon-frame branch of
// Particle_RenderList, before the hook, unchanged) and GTE registers / scratch pad
// / the primitive memory cursor, which only the retail primitive path touches and
// which every later particle rewrites.
//
// Not taken (-1, the retail primitive as before, counted): ROT_X or SCALE_Y axes,
// the special line kind, or a scale outside 1..0x3fff (the matrix field would
// wrap). None of these occurs in the exhaust emitters.

#define ENGINE_FX_EXHAUST_MATERIALS 16

typedef struct
{
	EngineMaterial wanted;
	int index;
	int used;
} EngineFxExhaustMaterial;

global_variable EngineFxExhaustMaterial s_engFxExhaustMat[ENGINE_FX_EXHAUST_MATERIALS];

// Material for (tpage, clut, blend), cached; registered again when the table
// was reset (level change) and no longer holds the wanted material.
internal int EngineFx_ExhaustMaterial(u16 tpage, u16 clut, int semi)
{
	EngineMaterial want;
	int slot;
	int freeSlot = -1;
	int index;

	memset(&want, 0, sizeof(want));
	want.texSource = ENGINE_TEX_VRAM;
	want.flags = ENGINE_MATERIAL_DOUBLE_SIDED | ENGINE_MATERIAL_NO_DEPTH_WRITE;
	if (semi)
	{
		want.blend = (u8)Engine_BlendFromAbr((tpage >> 5) & 3);
		want.flags |= ENGINE_MATERIAL_SEMI_STP;
		want.tpage = tpage;
	}
	else
	{
		want.blend = (u8)ENGINE_BLEND_OPAQUE;
		want.tpage = (u16)((tpage & ~0x60u) | (3u << 5));
	}
	want.clut = clut;
	want.tint[0] = want.tint[1] = want.tint[2] = want.tint[3] = 1.0f;

	for (slot = 0; slot < ENGINE_FX_EXHAUST_MATERIALS; slot++)
	{
		EngineFxExhaustMaterial *c = &s_engFxExhaustMat[slot];
		const EngineMaterial *have;

		if (!c->used)
		{
			if (freeSlot < 0)
			{
				freeSlot = slot;
			}
			continue;
		}
		if (memcmp(&c->wanted, &want, sizeof(want)) != 0)
		{
			continue;
		}
		have = Engine_Material(c->index);
		if ((have != NULL) && (memcmp(have, &want, sizeof(want)) == 0))
		{
			return c->index;
		}
		// Same key, but the table was reset: register again in this slot.
		freeSlot = slot;
		break;
	}

	if (freeSlot < 0)
	{
		freeSlot = 0; // cache full: replace the first (only a handful of keys exist)
	}
	index = Engine_MaterialRegister(&want);
	if (index < 0)
	{
		s_engFxExhaustMat[freeSlot].used = 0;
		return -1;
	}
	s_engFxExhaustMat[freeSlot].wanted = want;
	s_engFxExhaustMat[freeSlot].index = index;
	s_engFxExhaustMat[freeSlot].used = 1;
	return index;
}

// 1 when the particle is an exhaust particle (or bubble pop) of a driver the
// engine owns. Same test as NativeChar_ExhaustDraw: driver-local, owner set,
// driverID -1 (burning smoke has one), icon group 1 / 7 / 8.
internal int EngineFx_ExhaustOwned(const struct Particle *p)
{
	const struct GameTracker *gGT = sdata->gGT;
	const struct IconGroup *group;
	const struct Instance *inst;

	if (!g_cfg_engine || (p == NULL) || (gGT == NULL) || ((p->flagsSetColor & PARTICLE_SET_COLOR_FLAG_DRIVER_LOCAL) == 0) ||
	    (p->owner.driverInst == NULL) || (p->driverID != -1) || (p->ptrIconGroup == NULL))
	{
		return 0;
	}
	group = p->ptrIconGroup;
	if ((group != gGT->iconGroup[1]) && (group != gGT->iconGroup[7]) && (group != gGT->iconGroup[8]))
	{
		return 0;
	}
	inst = p->owner.driverInst;
	if ((inst->thread == NULL) || (inst->thread->object == NULL))
	{
		return 0;
	}
	return Engine_FxOwnsDriver((const struct Driver *)inst->thread->object);
}

// Retail colour word (Particle_SetColors) to what the engine needs: R, G, B
// bytes (128 = neutral, the "ne" VRAM path multiplies by 2), the raw-texture bit
// (SHADE_TEXTURE: no shading, so 128) and the semi-transparency bit.
internal void EngineFx_ExhaustDecodeColor(u32 word, u8 rgba[4], int *semi, int *raw)
{
	*raw = (word & PARTICLE_GPU_CODE_SHADE_TEXTURE) != 0;
	*semi = (word & PARTICLE_GPU_CODE_SEMI_TRANS) != 0;
	rgba[0] = *raw ? 128 : (u8)(word & 0xffu);
	rgba[1] = *raw ? 128 : (u8)((word >> 8) & 0xffu);
	rgba[2] = *raw ? 128 : (u8)((word >> 16) & 0xffu);
	rgba[3] = 255;
}

// Length of row `row` of the camera's view matrix (column-major m[col * 4 + row]).
internal float EngineFx_ExhaustRowLength(const EngineCamera *cam, int row)
{
	const float *m = cam->viewFromWorld.m;

	return sqrtf(m[row] * m[row] + m[4 + row] * m[4 + row] + m[8 + row] * m[8 + row]);
}

// The billboard of one exhaust particle: world half sizes from the retail size
// (see the header), roll from ROT_Y. texW / texH: the icon's size in texels;
// scaleX: 1..0x3fff, or 0 = the particle has no SCALE_X axis; rotY: the ROT_Y
// axis start value (used only with hasRotY). Writes 4 vertices (world units,
// colour in every vertex, texcoords left to the caller) and 6 indices. 0 when the
// camera's rows are degenerate.
// scaleY: 1..0x3fff, or 0 = no SCALE_Y axis. Retail (Particle_RenderList_BuildNormalMatrix,
// no ROT axis): r11 = scaleX << 1 and r22 = scaleX, then r22 = scaleY with SCALE_Y; so
// the height follows scaleY and the width scaleX. With a ROT_Y axis SCALE_Y is never
// passed here (the callers refuse it).
internal int EngineFx_ExhaustQuadXY(const EngineCamera *cam, EngineVec3 centre, int texW, int texH, int large, int scaleX, int scaleY, int hasRotY, int rotY,
                                    const u8 colour[4], EngineVertex v[4], u16 idx[6])
{
	const float s = (scaleX != 0) ? ((float)scaleX / 4096.0f) : 1.0f;
	const float sy = (scaleY != 0) ? ((float)scaleY / 4096.0f) : s;
	const float halfW = (float)(texW << (large ? 4 : 1));
	const float halfH = (float)(texH << (large ? 4 : 1));
	const float row0 = EngineFx_ExhaustRowLength(cam, 0);
	const float row1 = EngineFx_ExhaustRowLength(cam, 1);
	s16 roll = 0;

	if ((row0 < 0.01f) || (row1 < 0.01f))
	{
		return 0;
	}
	if (hasRotY)
	{
		roll = (s16)(-(rotY & 0xfff));
	}
	return Engine_FxBillboard(cam, centre, s * halfW / (2.0f * row0), sy * halfH / (4.0f * row1), roll, colour, v, idx, 0) == 4;
}

internal int EngineFx_ExhaustQuad(const EngineCamera *cam, EngineVec3 centre, int texW, int texH, int large, int scaleX, int hasRotY, int rotY,
                                  const u8 colour[4], EngineVertex v[4], u16 idx[6])
{
	return EngineFx_ExhaustQuadXY(cam, centre, texW, texH, large, scaleX, 0, hasRotY, rotY, colour, v, idx);
}

// Returns 0 = not an exhaust particle of an owned driver (the caller does what
// it always did), 1 = the engine took it (the caller writes and links nothing:
// drawn, or culled / hidden exactly where retail would not have drawn it),
// -1 = an owned particle the engine could not take (no camera, an axis kind it
// does not build, refused by the scene): the caller continues with the retail
// path and it is counted with Engine_FxNoteRetail here.
int EngineFx_ExhaustSprite(struct Particle *p, int view, const struct Icon *icon, s32 posX, s32 posY, s32 posZ, const s32 *cameraOffset, u32 *defaultOT,
                           int lookHide)
{
	const EngineCamera *cam;
	const struct InstDrawPerPlayer *idpp;
	const u16 flagsAxis = p->flagsAxis;
	const u16 flagsSetColor = p->flagsSetColor;
	EngineVertex v[4];
	u16 idx[6];
	u8 rgba[4];
	EngineVec3 world;
	EngineVec3 viewPos;
	EngineDraw draw;
	u32 *ot = NULL;
	u32 color;
	float zv;
	s32 depth;
	int scaleX = 0;
	int material;
	int semi;
	int raw;
	int cell;
	u16 tpage;

	if (!EngineFx_ExhaustOwned(p))
	{
		return 0;
	}

	cam = Engine_FrameCamera(view);
	if ((cam == NULL) || !cam->valid || (icon == NULL) || (cameraOffset == NULL) || (defaultOT == NULL))
	{
		Engine_FxNoteRetail(ENGINE_FX_EXHAUST);
		return -1;
	}

	// Only the axis kinds the exhaust emitters use are built here.
	if (((flagsAxis & (PARTICLE_AXIS_FLAG_ROT_X | PARTICLE_AXIS_FLAG_SCALE_Y)) != 0) || ((flagsSetColor & PARTICLE_SET_COLOR_FLAG_SPECIAL_LINE) != 0))
	{
		Engine_FxNoteRetail(ENGINE_FX_EXHAUST);
		return -1;
	}
	if ((flagsAxis & PARTICLE_AXIS_FLAG_SCALE_X) != 0)
	{
		scaleX = p->axis[PARTICLE_AXIS_SCALE_X_OR_LINE_SCALE].startVal;
		if ((scaleX < 1) || (scaleX > 0x3fff))
		{
			Engine_FxNoteRetail(ENGINE_FX_EXHAUST);
			return -1;
		}
	}

	// Gate 1: DRAW_SUCCESSFUL of the instance for this view. Particle_RenderList
	// already tested it before the hook; the same lookup is repeated here so the
	// function stands on its own. PUSHBUFFER_EXISTS turns the idpp off for the
	// OT choice (the same two lines as retail).
	idpp = Particle_RenderList_GetIdpp(p->owner.driverInst, view);
	if ((idpp->instFlags & DRAW_SUCCESSFUL) == 0)
	{
		return 1;
	}
	if ((idpp->instFlags & PUSHBUFFER_EXISTS) != 0)
	{
		idpp = NULL;
	}

	// Gate 2: the near-camera box (retail IsNearCamera on pos - cameraPos * 4).
	if (!Particle_RenderList_IsNearCamera(posX - cameraOffset[0]) || !Particle_RenderList_IsNearCamera(posY - cameraOffset[1]) ||
	    !Particle_RenderList_IsNearCamera(posZ - cameraOffset[2]))
	{
		return 1;
	}

	// Gates 3 and 4: depth. Retail depth is the GTE's MAC3 = 4 * view z (4x
	// space); the engine view z is in world units, so depth = 4 * z. "depth < 0"
	// and the near plane become one test: a point in front of the camera but
	// nearer than zNear cannot be projected by the engine (retail would project
	// it to a saturated huge quad). renderDepthLimit<<2 < depth is retail's.
	world.x = (float)posX * 0.25f;
	world.y = (float)posY * 0.25f;
	world.z = (float)posZ * 0.25f;
	viewPos = Engine_Mat4MulPoint(&cam->viewFromWorld, world);
	zv = viewPos.z;
	if (zv < cam->zNear)
	{
		return 1;
	}
	depth = (s32)(zv * 4.0f);
	if (CTR_MipsSll(p->renderDepthLimit, 2) < depth)
	{
		return 1;
	}

	// The colour: the one game-state write of this path (a proven no-op, see the
	// header), called at the point where retail calls it.
	color = Particle_SetColors(flagsAxis, flagsSetColor, p);
	// The measuring switch --native-hide-exhaust hides it here as retail does
	// (game/Particle.c, Particle_RenderList: the same test with "|", so the
	// hidden quads are counted alike); without the switch the call is 0 and
	// counts nothing.
	if (NativeRenderLayer_HideExhaustQuad(p) | lookHide)
	{
		return 1; // retail projects the quad, then leaves it out of the OT
	}
	EngineFx_ExhaustDecodeColor(color, rgba, &semi, &raw);

	if (!EngineFx_ExhaustQuad(cam, world, (icon->texLayout.u1 - icon->texLayout.u0) + 1, (icon->texLayout.v3 - icon->texLayout.v0) + 1,
	                          (flagsSetColor & PARTICLE_SET_COLOR_FLAG_LARGE_QUAD) != 0, scaleX, (flagsAxis & PARTICLE_AXIS_FLAG_ROT_Y) != 0,
	                          p->axis[PARTICLE_AXIS_ROT_Y_OR_LINE_PREV_Z].startVal, rgba, v, idx))
	{
		Engine_FxNoteRetail(ENGINE_FX_EXHAUST);
		return -1;
	}

	// Texture: the icon's layout (retail wrote the same words into its primitive:
	// u0 v0 clut / u1 v1 tpage / u2 v2 / u3 v3, the draw-mode bits of
	// flagsSetColor replacing the tpage's abr bits). Billboard order is
	// top-left, top-right, bottom-right, bottom-left = retail corners 0, 1, 3, 2.
	tpage = (u16)((icon->texLayout.tpage & ~PARTICLE_SET_COLOR_FLAG_DRAW_MODE_MASK) | (flagsSetColor & PARTICLE_SET_COLOR_FLAG_DRAW_MODE_MASK));
	v[0].texcoord[0] = (float)icon->texLayout.u0;
	v[0].texcoord[1] = (float)icon->texLayout.v0;
	v[1].texcoord[0] = (float)icon->texLayout.u1;
	v[1].texcoord[1] = (float)icon->texLayout.v1;
	v[2].texcoord[0] = (float)icon->texLayout.u3;
	v[2].texcoord[1] = (float)icon->texLayout.v3;
	v[3].texcoord[0] = (float)icon->texLayout.u2;
	v[3].texcoord[1] = (float)icon->texLayout.v2;
	Engine_VertexBoxOfCorners(v, 4); // the icon's texels: their box (engine.h, THE UV BOX)

	material = EngineFx_ExhaustMaterial(tpage, icon->texLayout.clut, semi);
	if (material < 0)
	{
		Engine_FxNoteRetail(ENGINE_FX_EXHAUST);
		return -1;
	}

	// The transition channel: the OT cell retail would link into.
	cell = Particle_NativeOtSlot(p, idpp, depth, defaultOT, &ot);

	memset(&draw, 0, sizeof(draw));
	draw.mesh = -1;
	Engine_CameraScreenFromWorld(cam, &draw.clipFromModel);
	draw.tint[0] = draw.tint[1] = draw.tint[2] = draw.tint[3] = 1.0f;
	draw.materialOverride = -1;
	draw.sortDepth = zv;
	draw.otCell = (u32)cell;
	draw.flags = ENGINE_DRAW_TRANSPARENT;
	draw.dynVertices = v;
	draw.dynVertexCount = 4;
	draw.dynIndices = idx;
	draw.dynIndexCount = 6;
	draw.dynMaterial = material;

	if (!Engine_RenderSubmitLinked(view, &draw, ot, cell))
	{
		Engine_FxNoteRetail(ENGINE_FX_EXHAUST);
		return -1;
	}
	// The engine oracle (armed frames only): E lines of the effect, no retail side (the producer writes none for an owned driver).
	Engine_OracleEngineDraw('X', "exhaust", view, &draw, NULL);
	Engine_FxNoteDrawn(ENGINE_FX_EXHAUST);
	return 1;
}

// --------------------------------------------------------------------------
// TRACK MOD FLAMES (round of 2026-10-15, S1): the flame columns of a track mod
// drawn by the engine, as billboards with a depth test against the engine
// track (the PS1 primitive has none and shows through walls).
//
// WHICH PARTICLES. Particle_RenderList (game/Particle.c) asks for every WORLD
// particle (not driver-local) under --engine. Taken are only those that meet all of:
//   - a container track plays on this level (NativeTrack_ActiveForLevel: a
//     container is loaded and the level is its donor slot) and the engine drew
//     the track this frame (Engine_TrackDrawnThisFrame);
//   - the icon group is the flame slot gGT->iconGroup[0xA];
//   - the axes are exactly POS_X, POS_Y, POS_Z, SCALE_X, SCALE_Y (0x67), no
//     function, not driver-local, no special line.
// That is the column a track mod sets every tick (game/native_trackmod.c,
// TrackMod_FlameSpawn: Particle_Init with its own group, which it put into
// iconGroup[0xA] (TrackMod_FlameBuildIcons), then flagsAxis = 0x67, funcPtr NULL,
// LARGE_QUAD). The retail users of the slot init other axes: emSet_fjFire
// (game/231/RB_FlameJet.c, RB_FlameJet_Particles) and emSet_Fireball
// (game/231/RB_Fireball.c) both have ROT_Y and COLOR_R (0xb7). Every other
// particle answers 0 after these compares and retail draws it as before.
//
// GEOMETRY. As the exhaust (header above), without the driver-local offset and
// the DRAW_SUCCESSFUL gate (a world particle has no instance): position
// axis >> 6 = 4x world, near-camera box, depth 4z against renderDepthLimit << 2.
// The size of a SCALE_X + SCALE_Y particle: r11 = scaleX << 1, r22 = scaleY
// (Particle_RenderList_BuildNormalMatrix), EngineFx_ExhaustQuadXY. The OT cell
// is the one retail links into without an idpp (Particle_NativeOtSlot).
//
// THE GAME-STATE PROOF. Particle_SetColors (game/Particle.c) writes back only in
// its colour branch, entered when the first argument (the axis flags) has
// PARTICLE_SET_COLOR_FLAG_RED (0x80, the COLOR_R axis bit). 0x67 has no such bit,
// so the call writes nothing; it is still made where retail makes it.
// Engine_SelfTestExhaust checks the rule and the size of a known column.

#define ENGINE_FX_FLAME_ICON_GROUP 0xA
// The three position axes (0x7) and both scale axes.
#define ENGINE_FX_FLAME_AXES (0x0007u | PARTICLE_AXIS_FLAG_SCALE_X | PARTICLE_AXIS_FLAG_SCALE_Y)

global_variable u32 s_engFxFlameDrawn;  // taken and submitted
global_variable u32 s_engFxFlameRetail; // taken by the rule, but left to retail (its PS1 primitive is drawn)
global_variable u32 s_engFxFlameTourDrawn;
global_variable u32 s_engFxFlameTourRetail;
global_variable int s_engFxFlameRegistered;

internal void EngineFx_FlameTourReport(int entry, int levelID, int phase)
{
	if (phase == 0)
	{
		s_engFxFlameTourDrawn = s_engFxFlameDrawn;
		s_engFxFlameTourRetail = s_engFxFlameRetail;
		return;
	}
	Platform_Log("[CTR Engine] tour %d level %d track mod flames: drawn %u, left to retail %u\n", entry, levelID, s_engFxFlameDrawn - s_engFxFlameTourDrawn,
	             s_engFxFlameRetail - s_engFxFlameTourRetail);
}

internal void EngineFx_FlameReport(void)
{
	Platform_Log("[CTR Engine] track mod flames: drawn %u, left to retail %u\n", s_engFxFlameDrawn, s_engFxFlameRetail);
}

// The rule above on plain values (also the self test's subject).
internal int EngineFx_FlameRule(int containerLevel, int trackDrawn, int flameSlot, u32 flagsAxis, u32 flagsSetColor, int hasFunc)
{
	return containerLevel && trackDrawn && flameSlot && ((flagsAxis & 0xffffu) == ENGINE_FX_FLAME_AXES) && !hasFunc &&
	       ((flagsSetColor & (PARTICLE_SET_COLOR_FLAG_DRIVER_LOCAL | PARTICLE_SET_COLOR_FLAG_SPECIAL_LINE)) == 0);
}

// 0 = not a track mod flame (the caller goes on as always), 1 = the engine took
// it (drawn, or outside the gates where retail draws nothing either), -1 = a
// flame the engine could not take (counted; retail draws its primitive).
int EngineFx_WorldSprite(struct Particle *p, int view, const struct Icon *icon, s32 posX, s32 posY, s32 posZ, const s32 *cameraOffset, u32 *defaultOT)
{
	const struct GameTracker *gGT = sdata->gGT;
	const EngineCamera *cam;
	EngineVertex v[4];
	u16 idx[6];
	u8 rgba[4];
	EngineVec3 world;
	EngineDraw draw;
	u32 *ot = NULL;
	u32 color;
	float zv;
	s32 depth;
	int scaleX;
	int scaleY;
	int material;
	int semi;
	int raw;
	int cell;
	u16 tpage;

	if (!g_cfg_engine || (p == NULL) || (gGT == NULL))
	{
		return 0;
	}
	if (!s_engFxFlameRegistered)
	{
		s_engFxFlameRegistered = 1;
		Engine_TourRegister(EngineFx_FlameTourReport);
		Platform_AtExitReport(EngineFx_FlameReport);
	}
	if (!EngineFx_FlameRule(NativeTrack_ActiveForLevel((int)gGT->levelID), 1,
	                        (p->ptrIconGroup != NULL) && (p->ptrIconGroup == gGT->iconGroup[ENGINE_FX_FLAME_ICON_GROUP]), p->flagsAxis, p->flagsSetColor,
	                        p->funcPtr != NULL))
	{
		return 0;
	}
	// A flame in a frame the PS1 walk drew the track: retail draws it, counted.
	if (!Engine_TrackDrawnThisFrame())
	{
		s_engFxFlameRetail++;
		return 0;
	}

	cam = Engine_FrameCamera(view);
	scaleX = p->axis[PARTICLE_AXIS_SCALE_X_OR_LINE_SCALE].startVal;
	scaleY = p->axis[PARTICLE_AXIS_SCALE_Y_OR_LINE_PREV_Y].startVal;
	if ((cam == NULL) || !cam->valid || (icon == NULL) || (cameraOffset == NULL) || (defaultOT == NULL) || (scaleX < 1) || (scaleX > 0x3fff) ||
	    (scaleY < 1) || (scaleY > 0x3fff))
	{
		s_engFxFlameRetail++;
		return -1;
	}

	// The near-camera box and the depth, as retail (see the exhaust).
	if (!Particle_RenderList_IsNearCamera(posX - cameraOffset[0]) || !Particle_RenderList_IsNearCamera(posY - cameraOffset[1]) ||
	    !Particle_RenderList_IsNearCamera(posZ - cameraOffset[2]))
	{
		return 1;
	}
	world.x = (float)posX * 0.25f;
	world.y = (float)posY * 0.25f;
	world.z = (float)posZ * 0.25f;
	zv = Engine_Mat4MulPoint(&cam->viewFromWorld, world).z;
	if (zv < cam->zNear)
	{
		return 1;
	}
	depth = (s32)(zv * 4.0f);
	if (CTR_MipsSll(p->renderDepthLimit, 2) < depth)
	{
		return 1;
	}

	// The colour, where retail calls it (writes nothing for these axes, see above).
	color = Particle_SetColors(p->flagsAxis, p->flagsSetColor, p);
	EngineFx_ExhaustDecodeColor(color, rgba, &semi, &raw);

	if (!EngineFx_ExhaustQuadXY(cam, world, (icon->texLayout.u1 - icon->texLayout.u0) + 1, (icon->texLayout.v3 - icon->texLayout.v0) + 1,
	                            (p->flagsSetColor & PARTICLE_SET_COLOR_FLAG_LARGE_QUAD) != 0, scaleX, scaleY, 0, 0, rgba, v, idx))
	{
		s_engFxFlameRetail++;
		return -1;
	}
	tpage = (u16)((icon->texLayout.tpage & ~PARTICLE_SET_COLOR_FLAG_DRAW_MODE_MASK) | (p->flagsSetColor & PARTICLE_SET_COLOR_FLAG_DRAW_MODE_MASK));
	v[0].texcoord[0] = (float)icon->texLayout.u0;
	v[0].texcoord[1] = (float)icon->texLayout.v0;
	v[1].texcoord[0] = (float)icon->texLayout.u1;
	v[1].texcoord[1] = (float)icon->texLayout.v1;
	v[2].texcoord[0] = (float)icon->texLayout.u3;
	v[2].texcoord[1] = (float)icon->texLayout.v3;
	v[3].texcoord[0] = (float)icon->texLayout.u2;
	v[3].texcoord[1] = (float)icon->texLayout.v2;
	Engine_VertexBoxOfCorners(v, 4); // the icon's texels: their box (engine.h, THE UV BOX)

	material = EngineFx_ExhaustMaterial(tpage, icon->texLayout.clut, semi);
	if (material < 0)
	{
		s_engFxFlameRetail++;
		return -1;
	}
	cell = Particle_NativeOtSlot(p, NULL, depth, defaultOT, &ot);

	memset(&draw, 0, sizeof(draw));
	draw.mesh = -1;
	Engine_CameraScreenFromWorld(cam, &draw.clipFromModel);
	draw.tint[0] = draw.tint[1] = draw.tint[2] = draw.tint[3] = 1.0f;
	draw.materialOverride = -1;
	draw.sortDepth = zv;
	draw.otCell = (u32)cell;
	draw.flags = ENGINE_DRAW_TRANSPARENT;
	draw.dynVertices = v;
	draw.dynVertexCount = 4;
	draw.dynIndices = idx;
	draw.dynIndexCount = 6;
	draw.dynMaterial = material;
	if (!Engine_RenderSubmitLinked(view, &draw, ot, cell))
	{
		s_engFxFlameRetail++;
		return -1;
	}
	// The engine oracle (armed frames only): E lines of the world sprite, no retail side.
	Engine_OracleEngineDraw('X', "worldsprite", view, &draw, NULL);
	s_engFxFlameDrawn++;
	return 1;
}

// --------------------------------------------------------------------------
// WORLD PARTICLES (night round of 2026-10-16): every particle of the ordinary
// list (wheel spray, terrain dust, water and snow spray, sparks, smoke, flames,
// exhaust of retail drivers, ...) drawn by the engine whenever it draws the
// track (the default for one player since the round of 2026-10-16), with a depth
// test against the engine track. The PS1 primitive has none and shows through
// walls.
//
// WHERE AND WHAT. Particle_RenderList (game/Particle.c) offers a particle at the
// one point where retail is about to link its primitive into the ordering table:
// after the gates (driver filter, DRAW_SUCCESSFUL of a driver-local particle's
// instance, the near-camera box, depth < 0, renderDepthLimit), after
// Particle_SetColors and after the primitive is written. So every game-state write
// of that function happens exactly as before, whatever the engine does:
// Particle_SetColors' clamp write-back, the line colour's velocity word of
// Particle_RenderList_WriteSpecialPrimitive, ptrIconArray of the icon-frame branch
// (no proof per emitter is needed, unlike the exhaust and the track mod flames
// above, which are taken earlier and stay as they are). The engine answers 1 and
// retail links nothing (the written primitive is neither linked nor kept), or 0
// and retail links it as always.
//
// THE RULE. Taken is every particle that reaches that point while the engine
// is on and draws the track in this frame (Engine_TrackDrawnThisFrame: decided by the
// frame's collect, before the particles; 1 player, not the menu). One path for
// every kind: the kind only names the counter (the icon group's slot in
// gGT->iconGroup, 0x00..0x10, or "other"); the geometry follows the axes:
//   - a normal quad (any of ROT_X, ROT_Y, SCALE_X, SCALE_Y): retail's corner (a, b),
//     a = +-halfW, b = +-halfH (texels << 1, << 4 with LARGE_QUAD), lies at
//     M * (a, b, 0) / 4096 from the centre in the 4x view space, with M the matrix
//     Particle_RenderList_BuildNormalMatrix made from the particle's axes (the
//     game's own reading of its scale and rotation axes; no GTE). The engine puts
//     the corner at centre + inverse(view rotation) * that / 4 in world units, so
//     the quad projects to retail's corners (each corner by its own depth, as
//     RTPS) and lies in the world, where the depth test sees it.
//   - a special line (SPECIAL_LINE): from the position to position + the delta
//     retail feeds the GTE ((prev - pos) >> 6, times scale >> 16 with SCALE_X,
//     each s16 as the GTE register takes it), 1 PSX pixel wide as the PS1 draws a
//     line (the quad's width is built on the screen and taken back to the world
//     at each end's depth), gouraud between retail's two colour words.
//     A line of a driver-local particle whose height comes from the driver
//     (wheel spray, landing and wall sparks; not IGNORE_Y) starts at the driver's
//     COLLISION height, below the drawn track faces: it is lifted exactly as the
//     skid marks are (engine_fx_skids.c), ENGINE_GROUND_LIFT world units up and
//     ENGINE_GROUND_DEPTH_BIAS in its material, so the depth test does not cut
//     its start off (EngineFx_ParticleLineGround). Drawing only.
// Colour and blend as retail's primitive: the colour word of Particle_SetColors
// (no colour axis: the raw texture, 128), semi-transparency with the abr bits of
// flagsSetColor (draw mode), the icon's tpage and clut. The OT cell is the one
// retail links into (Particle_NativeOtSlot with retail's depth and idpp), the
// transition channel for the PS1 objects in the same frame.
//
// NOT TAKEN, counted as "left to retail" (and why): no camera for the view, a
// singular camera, no material (cache or table full), the scene refused the draw.
// "near cut": the centre (or a line end) lies nearer than the engine's near plane;
// the engine cannot project it, retail would draw a screen-filling quad - taken
// and not drawn, as the exhaust does. Outside a frame with the engine track the
// hook answers 0 and counts nothing as left (that is the PS1 track's frame).

#define ENGINE_FX_PARTICLE_SLOTS 0x11 // gGT->iconGroup[0x11]
#define ENGINE_FX_PARTICLE_OTHER ENGINE_FX_PARTICLE_SLOTS
#define ENGINE_FX_PARTICLE_KINDS (ENGINE_FX_PARTICLE_SLOTS + 1)
#define ENGINE_FX_PARTICLE_MATERIALS 128
#define ENGINE_FX_PARTICLE_WHY 4 // camera, geometry, material, scene

CTR_STATIC_ASSERT(sizeof(POLY_FT4) == 40);

global_variable EngineFxExhaustMaterial s_engFxPartMat[ENGINE_FX_PARTICLE_MATERIALS];
global_variable u32 s_engFxPartDrawn[ENGINE_FX_PARTICLE_KINDS];
global_variable u32 s_engFxPartRetail[ENGINE_FX_PARTICLE_KINDS];
global_variable u32 s_engFxPartTourDrawn[ENGINE_FX_PARTICLE_KINDS];
global_variable u32 s_engFxPartTourRetail[ENGINE_FX_PARTICLE_KINDS];
global_variable u32 s_engFxPartWhy[ENGINE_FX_PARTICLE_WHY];
global_variable u32 s_engFxPartNear;
global_variable u32 s_engFxPartTourNear;
global_variable u32 s_engFxPartLines;
global_variable u32 s_engFxPartTourLines;
global_variable int s_engFxPartRegistered;

internal u32 EngineFx_ParticleSum(const u32 *a, const u32 *base)
{
	u32 sum = 0;

	for (int k = 0; k < ENGINE_FX_PARTICLE_KINDS; k++)
	{
		sum += a[k] - ((base != NULL) ? base[k] : 0u);
	}
	return sum;
}

// What the retail producers put into a slot (their use sites; the group names
// come from the level pack): wheel spray lines, landing and wall sparks (0x0),
// exhaust, burn smoke and dust puffs (0x1), bubbles (0x7, 0x8), wake and fall
// splash (0x9), flames (0xA), mud (0xD), mask hint puffs (0x10).
internal const char *EngineFx_ParticleKindName(int kind)
{
	static const char *const names[ENGINE_FX_PARTICLE_KINDS] = {"-spray", "-smoke", "", "", "", "", "", "-bubble", "-bubblepop", "-wake", "-flame", "", "", "-mud", "",
	                                                            "", "-puff", ""};

	return ((kind >= 0) && (kind < ENGINE_FX_PARTICLE_KINDS)) ? names[kind] : "";
}

// " gNN-name e/l" for every kind with a count since base (NULL = since the start).
internal void EngineFx_ParticleKinds(char *out, size_t size, const u32 *baseDrawn, const u32 *baseRetail)
{
	size_t used = 0;

	out[0] = '\0';
	for (int k = 0; k < ENGINE_FX_PARTICLE_KINDS; k++)
	{
		const u32 d = s_engFxPartDrawn[k] - ((baseDrawn != NULL) ? baseDrawn[k] : 0u);
		const u32 r = s_engFxPartRetail[k] - ((baseRetail != NULL) ? baseRetail[k] : 0u);
		int n;

		if ((d == 0u) && (r == 0u))
		{
			continue;
		}
		if (k == ENGINE_FX_PARTICLE_OTHER)
		{
			n = snprintf(out + used, size - used, " other %u/%u", d, r);
		}
		else
		{
			n = snprintf(out + used, size - used, " g%02X%s %u/%u", k, EngineFx_ParticleKindName(k), d, r);
		}
		if ((n < 0) || ((size_t)n >= size - used))
		{
			break;
		}
		used += (size_t)n;
	}
}

internal void EngineFx_ParticleTourReport(int entry, int levelID, int phase)
{
	char kinds[512];

	if (phase == 0)
	{
		memcpy(s_engFxPartTourDrawn, s_engFxPartDrawn, sizeof(s_engFxPartTourDrawn));
		memcpy(s_engFxPartTourRetail, s_engFxPartRetail, sizeof(s_engFxPartTourRetail));
		s_engFxPartTourNear = s_engFxPartNear;
		s_engFxPartTourLines = s_engFxPartLines;
		return;
	}
	EngineFx_ParticleKinds(kinds, sizeof(kinds), s_engFxPartTourDrawn, s_engFxPartTourRetail);
	Platform_Log("[CTR Engine] tour %d level %d world particles: engine %u, left to retail %u (lines %u, near cut %u; by icon group engine/left:%s)\n", entry,
	             levelID, EngineFx_ParticleSum(s_engFxPartDrawn, s_engFxPartTourDrawn), EngineFx_ParticleSum(s_engFxPartRetail, s_engFxPartTourRetail),
	             s_engFxPartLines - s_engFxPartTourLines, s_engFxPartNear - s_engFxPartTourNear, (kinds[0] != '\0') ? kinds : " none");
}

internal void EngineFx_ParticleReport(void)
{
	char kinds[512];

	EngineFx_ParticleKinds(kinds, sizeof(kinds), NULL, NULL);
	Platform_Log("[CTR Engine] world particles: engine %u, left to retail %u (lines %u, near cut %u; left: camera %u, geometry %u, material %u, scene %u; "
	             "by icon group engine/left:%s)\n",
	             EngineFx_ParticleSum(s_engFxPartDrawn, NULL), EngineFx_ParticleSum(s_engFxPartRetail, NULL), s_engFxPartLines, s_engFxPartNear, s_engFxPartWhy[0],
	             s_engFxPartWhy[1], s_engFxPartWhy[2], s_engFxPartWhy[3], (kinds[0] != '\0') ? kinds : " none");
}

void EngineFx_ParticlesBegin(int view)
{
	(void)view;
	if (!g_cfg_engine || s_engFxPartRegistered)
	{
		return;
	}
	s_engFxPartRegistered = 1;
	Engine_TourRegister(EngineFx_ParticleTourReport);
	Platform_AtExitReport(EngineFx_ParticleReport);
}

// The counter of a particle: the slot of its icon group in the level's table, or
// ENGINE_FX_PARTICLE_OTHER (no group, or one the table does not hold).
internal int EngineFx_ParticleKindOf(const struct IconGroup *group, struct IconGroup *const *slots, int slotCount)
{
	if ((group != NULL) && (slots != NULL))
	{
		for (int k = 0; k < slotCount; k++)
		{
			if (slots[k] == group)
			{
				return k;
			}
		}
	}
	return ENGINE_FX_PARTICLE_OTHER;
}

// The rule on plain values (also the self test's subject): the engine is on and
// draws the track in this frame. Every particle that reaches the hook is then taken.
internal int EngineFx_ParticleRule(int engine, int trackDrawn)
{
	return engine && trackDrawn;
}

// A line that starts at the driver's collision height (header above): driver-local
// with the driver's height (not IGNORE_Y). Then *lift and *depthBias are the skid
// marks' (ENGINE_GROUND_LIFT, ENGINE_GROUND_DEPTH_BIAS), else 0. Returns 1 for such a line.
internal int EngineFx_ParticleLineGround(u16 flagsSetColor, float *lift, float *depthBias)
{
	const int ground = ((flagsSetColor & PARTICLE_SET_COLOR_FLAG_DRIVER_LOCAL) != 0) && ((flagsSetColor & PARTICLE_SET_COLOR_FLAG_DRIVER_LOCAL_IGNORE_Y) == 0);

	*lift = ground ? ENGINE_GROUND_LIFT : 0.0f;
	*depthBias = ground ? ENGINE_GROUND_DEPTH_BIAS : 0.0f;
	return ground;
}

// Material for a particle, cached per level (the table resets at level unload;
// a cached index is checked against the table, as EngineFx_ExhaustMaterial).
// textured 0: a gouraud line, no texture; the blend from abr when semi.
// depthBias: EngineMaterial.depthBias (a ground line's, else 0).
internal int EngineFx_ParticleMaterial(int textured, u16 tpage, u16 clut, int semi, int abr, float depthBias)
{
	EngineMaterial want;
	int freeSlot = -1;
	int index;

	memset(&want, 0, sizeof(want));
	want.flags = ENGINE_MATERIAL_DOUBLE_SIDED | ENGINE_MATERIAL_NO_DEPTH_WRITE;
	want.depthBias = depthBias;
	want.tint[0] = want.tint[1] = want.tint[2] = want.tint[3] = 1.0f;
	if (textured)
	{
		want.texSource = ENGINE_TEX_VRAM;
		want.clut = clut;
		if (semi)
		{
			want.blend = (u8)Engine_BlendFromAbr((tpage >> 5) & 3);
			want.flags |= ENGINE_MATERIAL_SEMI_STP;
			want.tpage = tpage;
		}
		else
		{
			want.blend = (u8)ENGINE_BLEND_OPAQUE;
			want.tpage = (u16)((tpage & ~0x60u) | (3u << 5));
		}
	}
	else
	{
		// The PS1 blends an untextured semi-transparent primitive as a whole.
		want.texSource = ENGINE_TEX_NONE;
		want.blend = (u8)(semi ? Engine_BlendFromAbr(abr & 3) : ENGINE_BLEND_OPAQUE);
	}

	for (int slot = 0; slot < ENGINE_FX_PARTICLE_MATERIALS; slot++)
	{
		EngineFxExhaustMaterial *c = &s_engFxPartMat[slot];
		const EngineMaterial *have;

		if (!c->used)
		{
			if (freeSlot < 0)
			{
				freeSlot = slot;
			}
			continue;
		}
		if (memcmp(&c->wanted, &want, sizeof(want)) != 0)
		{
			continue;
		}
		have = Engine_Material(c->index);
		if ((have != NULL) && (memcmp(have, &want, sizeof(want)) == 0))
		{
			return c->index;
		}
		freeSlot = slot; // same key, the table was reset: register again here
		break;
	}
	if (freeSlot < 0)
	{
		// Full: drop every entry whose index no longer holds its key (a new level).
		for (int slot = 0; slot < ENGINE_FX_PARTICLE_MATERIALS; slot++)
		{
			const EngineMaterial *have = Engine_Material(s_engFxPartMat[slot].index);

			if ((have == NULL) || (memcmp(have, &s_engFxPartMat[slot].wanted, sizeof(want)) != 0))
			{
				s_engFxPartMat[slot].used = 0;
				if (freeSlot < 0)
				{
					freeSlot = slot;
				}
			}
		}
		if (freeSlot < 0)
		{
			return -1;
		}
	}
	index = Engine_MaterialRegister(&want);
	if (index < 0)
	{
		s_engFxPartMat[freeSlot].used = 0;
		return -1;
	}
	s_engFxPartMat[freeSlot].wanted = want;
	s_engFxPartMat[freeSlot].index = index;
	s_engFxPartMat[freeSlot].used = 1;
	return index;
}

// The inverse of the camera's 3 x 3 part (view = R * world + t; R is the game's
// view rotation with its Y row squeezed): world direction = inv * view direction,
// inv row-major. 0 when R is singular.
internal int EngineFx_ViewInverse(const EngineCamera *cam, float inv[9])
{
	const float *m = cam->viewFromWorld.m;
	// R row r, column c = m[c * 4 + r]
	const float a = m[0], b = m[4], c = m[8];
	const float d = m[1], e = m[5], f = m[9];
	const float g = m[2], h = m[6], i = m[10];
	const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);

	if ((det > -1.0e-6f) && (det < 1.0e-6f))
	{
		return 0;
	}
	inv[0] = (e * i - f * h) / det;
	inv[1] = (c * h - b * i) / det;
	inv[2] = (b * f - c * e) / det;
	inv[3] = (f * g - d * i) / det;
	inv[4] = (a * i - c * g) / det;
	inv[5] = (c * d - a * f) / det;
	inv[6] = (d * h - e * g) / det;
	inv[7] = (b * g - a * h) / det;
	inv[8] = (a * e - b * d) / det;
	return 1;
}

internal EngineVec3 EngineFx_ViewDirToWorld(const float inv[9], float x, float y, float z)
{
	EngineVec3 w;

	w.x = inv[0] * x + inv[1] * y + inv[2] * z;
	w.y = inv[3] * x + inv[4] * y + inv[5] * z;
	w.z = inv[6] * x + inv[7] * y + inv[8] * z;
	return w;
}

// The four corners of a normal quad in world units (header above): retail corner
// (a, b) at M * (a, b, 0) / 4096 in the 4x view space. Engine order top-left,
// top-right, bottom-right, bottom-left = retail corners 0, 1, 3, 2. Positions only.
internal int EngineFx_ParticleCorners(const EngineCamera *cam, EngineVec3 centre, const struct ParticleRenderListMatrix *mx, int halfW, int halfH,
                                      EngineVertex v[4])
{
	static const int sa[4] = {-1, 1, 1, -1};
	static const int sb[4] = {-1, -1, 1, 1};
	const float r11 = (float)(s16)(mx->r11r12 & 0xffffu);
	const float r12 = (float)(s16)(mx->r11r12 >> 16);
	const float r21 = (float)(s16)(mx->r13r21 >> 16);
	const float r22 = (float)(s16)(mx->r22r23 & 0xffffu);
	const float r31 = (float)(s16)(mx->r31r32 & 0xffffu);
	const float r32 = (float)(s16)(mx->r31r32 >> 16);
	// 4096 (the 4.12 matrix) times 4 (the 4x view space)
	const float k = 1.0f / 16384.0f;
	float inv[9];

	if (!EngineFx_ViewInverse(cam, inv))
	{
		return 0;
	}
	for (int c = 0; c < 4; c++)
	{
		const float a = (float)(sa[c] * halfW);
		const float b = (float)(sb[c] * halfH);
		const EngineVec3 w = EngineFx_ViewDirToWorld(inv, (r11 * a + r12 * b) * k, (r21 * a + r22 * b) * k, (r31 * a + r32 * b) * k);

		v[c].position[0] = centre.x + w.x;
		v[c].position[1] = centre.y + w.y;
		v[c].position[2] = centre.z + w.z;
	}
	return 1;
}

// A line from p0 to p1 (world units) as a quad halfPx PSX pixels to each side
// and past each end, built on the screen and taken back to the world at each
// end's depth. Colours c0 at p0, c1 at p1. 0 when an end lies nearer than the
// near plane or the camera is singular.
internal int EngineFx_ParticleLineQuad(const EngineCamera *cam, EngineVec3 p0, EngineVec3 p1, float halfPx, const u8 c0[4], const u8 c1[4], EngineVertex v[4])
{
	EngineVec3 view[2];
	EngineVec3 end[2];
	float inv[9];
	float dx;
	float dy;
	float len;

	view[0] = Engine_Mat4MulPoint(&cam->viewFromWorld, p0);
	view[1] = Engine_Mat4MulPoint(&cam->viewFromWorld, p1);
	end[0] = p0;
	end[1] = p1;
	if ((view[0].z < cam->zNear) || (view[1].z < cam->zNear) || (cam->H <= 0.0f) || !EngineFx_ViewInverse(cam, inv))
	{
		return 0;
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
	// v0 = p0 left, v1 = p1 left, v2 = p1 right, v3 = p0 right (side normal (-dy, dx))
	for (int c = 0; c < 4; c++)
	{
		const int e = ((c == 1) || (c == 2)) ? 1 : 0;
		const float along = e ? halfPx : -halfPx;
		const float side = (c < 2) ? -halfPx : halfPx;
		const float sx = dx * along - dy * side;
		const float sy = dy * along + dx * side;
		const float perPx = view[e].z / cam->H;
		const EngineVec3 w = EngineFx_ViewDirToWorld(inv, sx * perPx, sy * perPx, 0.0f);
		const u8 *col = e ? c1 : c0;

		v[c].position[0] = end[e].x + w.x;
		v[c].position[1] = end[e].y + w.y;
		v[c].position[2] = end[e].z + w.z;
		v[c].texcoord[0] = 0.0f;
		v[c].texcoord[1] = 0.0f;
		v[c].color[0] = col[0];
		v[c].color[1] = col[1];
		v[c].color[2] = col[2];
		v[c].color[3] = col[3];
	}
	Engine_VertexBoxFull(v, 4); // untextured (engine.h, THE UV BOX)
	return 1;
}

// The line's end offset as retail feeds it to the GTE (Particle_RenderList_WriteSpecialPrimitive):
// (prev - pos) >> 6, with SCALE_X times scale >> 16; the register keeps 16 bits.
internal void EngineFx_ParticleLineDelta(const struct Particle *p, u16 flagsAxis, s32 out[3])
{
	static const int prevAxis[3] = {PARTICLE_AXIS_ROT_X_OR_LINE_PREV_X, PARTICLE_AXIS_SCALE_Y_OR_LINE_PREV_Y, PARTICLE_AXIS_ROT_Y_OR_LINE_PREV_Z};

	for (int a = 0; a < 3; a++)
	{
		s32 d = (p->axis[prevAxis[a]].startVal - p->axis[PARTICLE_AXIS_POS_X + a].startVal) >> 6;

		if ((flagsAxis & PARTICLE_AXIS_FLAG_SCALE_X) != 0)
		{
			d = Particle_RenderList_MulLo(d, p->axis[PARTICLE_AXIS_SCALE_X_OR_LINE_SCALE].startVal) >> 16;
		}
		out[a] = (s32)(s16)(u16)((u32)d & 0xffffu);
	}
}

// The particle's id in oracle lines: its icon group slot and its pool slot.
internal void EngineFx_ParticleId(const struct Particle *p, int kind, char *out, size_t size)
{
	const struct GameTracker *gGT = sdata->gGT;
	long slot = -1;

	if ((gGT != NULL) && (gGT->JitPools.particle.ptrPoolData != NULL) && (gGT->JitPools.particle.itemSize != 0u))
	{
		const char *base = (const char *)gGT->JitPools.particle.ptrPoolData;
		const ptrdiff_t off = (const char *)p - base;

		if ((off >= 0) && (off < (ptrdiff_t)gGT->JitPools.particle.itemSize * (ptrdiff_t)gGT->JitPools.particle.maxItems))
		{
			slot = (long)(off / (ptrdiff_t)gGT->JitPools.particle.itemSize);
		}
	}
	if (kind == ENGINE_FX_PARTICLE_OTHER)
	{
		snprintf(out, size, "particle-other#%ld", slot);
	}
	else
	{
		snprintf(out, size, "particle-g%02X#%ld", kind, slot);
	}
}

// Common start of both hooks: -1 = outside the rule (nothing counted), else the
// particle's kind, with its view's camera in *cam (NULL: no valid camera).
internal int EngineFx_ParticleEnter(const struct Particle *p, int view, const EngineCamera **cam)
{
	const struct GameTracker *gGT = sdata->gGT;
	int kind;

	if ((p == NULL) || (gGT == NULL) || !EngineFx_ParticleRule(g_cfg_engine, Engine_TrackDrawnThisFrame()))
	{
		return -1;
	}
	kind = EngineFx_ParticleKindOf(p->ptrIconGroup, gGT->iconGroup, ENGINE_FX_PARTICLE_SLOTS);
	*cam = Engine_FrameCamera(view);
	if ((*cam != NULL) && !(*cam)->valid)
	{
		*cam = NULL;
	}
	return kind;
}

// A taken particle the engine could not draw: retail links it (answer 0), counted.
internal int EngineFx_ParticleLeft(int kind, int why)
{
	s_engFxPartRetail[kind]++;
	s_engFxPartWhy[why]++;
	return 0;
}

internal int EngineFx_ParticleSubmit(const EngineCamera *cam, int view, float zv, int material, const EngineVertex v[4], const struct Particle *p, s32 depth,
                                     const struct InstDrawPerPlayer *idpp, u32 *defaultOT, EngineDraw *draw)
{
	static const u16 idx[6] = {0, 1, 2, 0, 2, 3};
	u32 *ot = NULL;
	const int cell = Particle_NativeOtSlot(p, idpp, depth, defaultOT, &ot);

	memset(draw, 0, sizeof(*draw));
	draw->mesh = -1;
	Engine_CameraScreenFromWorld(cam, &draw->clipFromModel);
	draw->tint[0] = draw->tint[1] = draw->tint[2] = draw->tint[3] = 1.0f;
	draw->materialOverride = -1;
	draw->sortDepth = zv;
	draw->otCell = (u32)cell;
	draw->flags = ENGINE_DRAW_TRANSPARENT;
	draw->dynVertices = v;
	draw->dynVertexCount = 4;
	draw->dynIndices = idx;
	draw->dynIndexCount = 6;
	draw->dynMaterial = material;
	return Engine_RenderSubmitLinked(view, draw, ot, cell);
}

int EngineFx_ParticleQuad(struct Particle *p, int view, const struct Icon *icon, u16 flagsSetColor, u32 color, const struct ParticleRenderListMatrix *matrix,
                          s32 relX, s32 relY, s32 relZ, const s32 *cameraOffset, s32 depth, const struct InstDrawPerPlayer *idpp, u32 *defaultOT,
                          const POLY_FT4 *retail)
{
	const EngineCamera *cam = NULL;
	const int kind = EngineFx_ParticleEnter(p, view, &cam);
	const int shift = ((flagsSetColor & PARTICLE_SET_COLOR_FLAG_LARGE_QUAD) != 0) ? 4 : 1;
	EngineVertex v[4];
	EngineVec3 world;
	EngineDraw draw;
	u8 rgba[4];
	float zv;
	int semi;
	int raw;
	int material;
	u16 tpage;

	if (kind < 0)
	{
		return 0;
	}
	if ((cam == NULL) || (icon == NULL) || (matrix == NULL) || (cameraOffset == NULL) || (defaultOT == NULL))
	{
		return EngineFx_ParticleLeft(kind, 0);
	}
	world.x = (float)(relX + cameraOffset[0]) * 0.25f;
	world.y = (float)(relY + cameraOffset[1]) * 0.25f;
	world.z = (float)(relZ + cameraOffset[2]) * 0.25f;
	zv = Engine_Mat4MulPoint(&cam->viewFromWorld, world).z;
	if (zv < cam->zNear)
	{
		s_engFxPartNear++;
		return 1;
	}
	if (!EngineFx_ParticleCorners(cam, world, matrix, ((icon->texLayout.u1 - icon->texLayout.u0) + 1) << shift,
	                              ((icon->texLayout.v3 - icon->texLayout.v0) + 1) << shift, v))
	{
		return EngineFx_ParticleLeft(kind, 1);
	}
	EngineFx_ExhaustDecodeColor(color, rgba, &semi, &raw);
	for (int c = 0; c < 4; c++)
	{
		memcpy(v[c].color, rgba, sizeof(rgba));
	}
	// The icon's layout, as retail writes it (the draw-mode bits of flagsSetColor
	// replace the tpage's abr bits); corners 0, 1, 3, 2.
	v[0].texcoord[0] = (float)icon->texLayout.u0;
	v[0].texcoord[1] = (float)icon->texLayout.v0;
	v[1].texcoord[0] = (float)icon->texLayout.u1;
	v[1].texcoord[1] = (float)icon->texLayout.v1;
	v[2].texcoord[0] = (float)icon->texLayout.u3;
	v[2].texcoord[1] = (float)icon->texLayout.v3;
	v[3].texcoord[0] = (float)icon->texLayout.u2;
	v[3].texcoord[1] = (float)icon->texLayout.v2;
	Engine_VertexBoxOfCorners(v, 4); // the icon's texels: their box (engine.h, THE UV BOX)
	tpage = (u16)((icon->texLayout.tpage & ~PARTICLE_SET_COLOR_FLAG_DRAW_MODE_MASK) | (flagsSetColor & PARTICLE_SET_COLOR_FLAG_DRAW_MODE_MASK));
	material = EngineFx_ParticleMaterial(1, tpage, icon->texLayout.clut, semi, 0, 0.0f);
	if (material < 0)
	{
		return EngineFx_ParticleLeft(kind, 2);
	}
	if (!EngineFx_ParticleSubmit(cam, view, zv, material, v, p, depth, idpp, defaultOT, &draw))
	{
		return EngineFx_ParticleLeft(kind, 3);
	}
	// The engine oracle (armed frames only): the retail quad as R lines (the
	// primitive retail wrote and did not link, with a tag of its length) and the
	// engine's E lines, under the same source id.
	if (Engine_OracleActive())
	{
		char id[ENGINE_ORACLE_ID_MAX];
		u32 words[10];

		EngineFx_ParticleId(p, kind, id, sizeof(id));
		if (retail != NULL)
		{
			memcpy(words, retail, sizeof(words));
			words[0] = 9u << 24;
			Engine_OracleRetailRange('X', id, words, words + 10, "prim=particle");
		}
		Engine_OracleEngineDraw('X', id, view, &draw, "prim=particle");
	}
	s_engFxPartDrawn[kind]++;
	return 1;
}

int EngineFx_ParticleLine(struct Particle *p, int view, u16 flagsAxis, u16 flagsSetColor, u32 color, s32 relX, s32 relY, s32 relZ, const s32 *cameraOffset,
                          s32 depth, const struct InstDrawPerPlayer *idpp, u32 *defaultOT)
{
	const EngineCamera *cam = NULL;
	const int kind = EngineFx_ParticleEnter(p, view, &cam);
	EngineVertex v[4];
	EngineVec3 p0;
	EngineVec3 p1;
	EngineDraw draw;
	s32 delta[3];
	u32 word0;
	u32 word1;
	u8 c0[4];
	u8 c1[4];
	float lift;
	float depthBias;
	int semi;
	int material;

	if (kind < 0)
	{
		return 0;
	}
	if ((cam == NULL) || (cameraOffset == NULL) || (defaultOT == NULL))
	{
		return EngineFx_ParticleLeft(kind, 0);
	}
	p0.x = (float)(relX + cameraOffset[0]) * 0.25f;
	p0.y = (float)(relY + cameraOffset[1]) * 0.25f;
	p0.z = (float)(relZ + cameraOffset[2]) * 0.25f;
	EngineFx_ParticleLineDelta(p, flagsAxis, delta);
	p1.x = p0.x + (float)delta[0] * 0.25f;
	p1.y = p0.y + (float)delta[1] * 0.25f;
	p1.z = p0.z + (float)delta[2] * 0.25f;
	// A line from the driver's collision height: lifted as the skid marks are.
	(void)EngineFx_ParticleLineGround(flagsSetColor, &lift, &depthBias);
	p0.y += lift;
	p1.y += lift;

	// Retail's two colour words; the first one's code byte carries the semi bit.
	word0 = color;
	word1 = (u32)p->axis[PARTICLE_AXIS_ICON_FRAME_OR_LINE_COLOR].startVal;
	if ((flagsSetColor & PARTICLE_SET_COLOR_FLAG_SPECIAL_LINE_SWAP_COLORS) != 0)
	{
		word0 = word1;
		word1 = color;
	}
	semi = (word0 & PARTICLE_GPU_CODE_SEMI_TRANS) != 0;
	c0[0] = (u8)(word0 & 0xffu);
	c0[1] = (u8)((word0 >> 8) & 0xffu);
	c0[2] = (u8)((word0 >> 16) & 0xffu);
	c0[3] = 255;
	c1[0] = (u8)(word1 & 0xffu);
	c1[1] = (u8)((word1 >> 8) & 0xffu);
	c1[2] = (u8)((word1 >> 16) & 0xffu);
	c1[3] = 255;

	if (!EngineFx_ParticleLineQuad(cam, p0, p1, 0.5f, c0, c1, v))
	{
		s_engFxPartNear++;
		return 1;
	}
	material = EngineFx_ParticleMaterial(0, 0, 0, semi, (flagsSetColor & PARTICLE_SET_COLOR_FLAG_DRAW_MODE_MASK) >> 5, depthBias);
	if (material < 0)
	{
		return EngineFx_ParticleLeft(kind, 2);
	}
	if (!EngineFx_ParticleSubmit(cam, view, Engine_Mat4MulPoint(&cam->viewFromWorld, p0).z, material, v, p, depth, idpp, defaultOT, &draw))
	{
		return EngineFx_ParticleLeft(kind, 3);
	}
	// The oracle: retail's line as one untextured R line (the decoder skips lines)
	// with its two colours (col c0;c1;c1), the engine's quad as E lines.
	if (Engine_OracleActive())
	{
		char id[ENGINE_ORACLE_ID_MAX];
		EngineOracleFace f;

		EngineFx_ParticleId(p, kind, id, sizeof(id));
		memset(&f, 0, sizeof(f));
		f.src = 'X';
		snprintf(f.srcId, sizeof(f.srcId), "%s", id);
		f.textured = 0;
		f.clutX = -1;
		f.clutY = -1;
		f.blend = semi ? (int)((flagsSetColor & PARTICLE_SET_COLOR_FLAG_DRAW_MODE_MASK) >> 5) : ENGINE_ORACLE_BLEND_OPAQUE;
		f.cellRow = -1;
		f.cellCol = -1;
		snprintf(f.extra, sizeof(f.extra), "prim=line");
		f.colorCount = 3;
		memcpy(f.color[0], c0, 3);
		memcpy(f.color[1], c1, 3);
		memcpy(f.color[2], c1, 3);
		Engine_OracleWrite('R', &f);
		Engine_OracleEngineDraw('X', id, view, &draw, "prim=line");
	}
	s_engFxPartLines++;
	s_engFxPartDrawn[kind]++;
	return 1;
}

// --------------------------------------------------------------------------
// Selftest: a made-up camera and particles, no game state.

internal void EngineFx_ExhaustCheck(int ok, const char *what, int *checks, int *failures)
{
	(*checks)++;
	if (!ok)
	{
		(*failures)++;
		printf("engine selftest: exhaust: %s\n", what);
	}
}

internal float EngineFx_ExhaustAbs(float v)
{
	return (v < 0.0f) ? -v : v;
}

// A camera as the game builds it: rotation about Y by `yaw` (4096 = turn), the
// Y row squeezed by 9/16, translation (tx, ty, tz); H and offsets of the 512 x 216
// view of a 1 player race.
internal void EngineFx_ExhaustTestCamera(EngineCamera *cam, float yaw, float tx, float ty, float tz)
{
	const float a = yaw * (6.28318530717958647692f / 4096.0f);
	const float c = cosf(a);
	const float n = sinf(a);
	const float squeeze = 9.0f / 16.0f;

	memset(cam, 0, sizeof(*cam));
	Engine_Mat4Identity(&cam->viewFromWorld);
	// column-major m[col * 4 + row]; rows (c, 0, n), (0, squeeze, 0), (-n, 0, c)
	cam->viewFromWorld.m[0] = c;
	cam->viewFromWorld.m[8] = n;
	cam->viewFromWorld.m[5] = squeeze;
	cam->viewFromWorld.m[2] = -n;
	cam->viewFromWorld.m[10] = c;
	cam->viewFromWorld.m[12] = tx;
	cam->viewFromWorld.m[13] = ty * squeeze;
	cam->viewFromWorld.m[14] = tz;
	cam->valid = 1;
	cam->H = 256.0f;
	cam->offsetX = 256.0f;
	cam->offsetY = 108.0f;
	cam->rectW = 512;
	cam->rectH = 216;
	cam->zNear = cam->H / 8.0f;
}

// The world point whose view position is (vx, vy, vz) for the test camera.
internal EngineVec3 EngineFx_ExhaustTestWorld(const EngineCamera *cam, float vx, float vy, float vz)
{
	const float *m = cam->viewFromWorld.m;
	const float squeeze = 9.0f / 16.0f;
	// view = R * world + t with R's Y row squeezed: undo t, undo the squeeze, then
	// R^T (R is a rotation about Y, so its transpose is its inverse).
	const float x = vx - m[12];
	const float y = (vy - m[13]) / squeeze;
	const float z = vz - m[14];
	EngineVec3 w;

	w.x = m[0] * x + m[2] * z;
	w.y = y;
	w.z = m[8] * x + m[10] * z;
	return w;
}

// Screen extent (PSX pixels) of the four projected vertices.
internal void EngineFx_ExhaustExtent(const EngineCamera *cam, const EngineVertex v[4], float *width, float *height)
{
	float minX = 1.0e30f;
	float maxX = -1.0e30f;
	float minY = 1.0e30f;
	float maxY = -1.0e30f;
	int i;

	for (i = 0; i < 4; i++)
	{
		EngineVec3 w;
		EngineVec3 view;
		float sx;
		float sy;

		w.x = v[i].position[0];
		w.y = v[i].position[1];
		w.z = v[i].position[2];
		view = Engine_Mat4MulPoint(&cam->viewFromWorld, w);
		sx = cam->offsetX + cam->H * view.x / view.z;
		sy = cam->offsetY + cam->H * view.y / view.z;
		minX = (sx < minX) ? sx : minX;
		maxX = (sx > maxX) ? sx : maxX;
		minY = (sy < minY) ? sy : minY;
		maxY = (sy > maxY) ? sy : maxY;
	}
	*width = maxX - minX;
	*height = maxY - minY;
}

// The colour axes of the emitters this file takes, against the proof in the header:
// the colour entry (axis 7..9) of an exhaust set never leaves [0, 0xff00] within
// its lifespan, and is only base start + base velocity.
internal void EngineFx_ExhaustTestEmitter(const struct ParticleEmitter *set, const char *name, int expectColour, int *checks, int *failures)
{
	const struct ParticleEmitter *e;
	int lifespan = -1;
	int colourEntries = 0;
	int ok = 1;
	char msg[96];

	for (e = set; e->flags != 0; e++)
	{
		if (e->initOffset == PARTICLE_EMITTER_INIT_FUNC_OFFSET)
		{
			lifespan = e->InitTypes.FuncInit.particle_lifespan;
		}
		else if ((e->initOffset >= PARTICLE_AXIS_COLOR_R) && (e->initOffset <= PARTICLE_AXIS_COLOR_B))
		{
			int n;
			s32 value = e->InitTypes.AxisInit.baseValue.startVal;

			colourEntries++;
			// only base start + base velocity (flags 3): no accel, random or oscillator
			ok = ok && (e->flags == (PARTICLE_EMITTER_FLAG_BASE_START | PARTICLE_EMITTER_FLAG_BASE_VELOCITY)) && (e->initOffset == PARTICLE_AXIS_COLOR_R) &&
			     (e->InitTypes.AxisInit.baseValue.accel == 0);
			for (n = 0; n <= lifespan; n++)
			{
				ok = ok && (value >= PARTICLE_COLOR_CHANNEL_MIN) && (value <= PARTICLE_COLOR_CHANNEL_MAX);
				value += e->InitTypes.AxisInit.baseValue.velocity;
			}
		}
	}
	snprintf(msg, sizeof(msg), "%s: colour entry %s and inside the clamp for its whole life", name, expectColour ? "present" : "absent");
	EngineFx_ExhaustCheck((lifespan > 0) && (colourEntries == (expectColour ? 1 : 0)) && ok, msg, checks, failures);
}

// WORLD PARTICLES: the rule, the kind, the corners of every axis kind against a
// float RTPS of retail's own matrix, the size against the exhaust formula, the line.
internal void EngineFx_ParticleTestScreen(const EngineCamera *cam, float x, float y, float z, float *sx, float *sy)
{
	EngineVec3 w;
	EngineVec3 view;

	w.x = x;
	w.y = y;
	w.z = z;
	view = Engine_Mat4MulPoint(&cam->viewFromWorld, w);
	*sx = cam->offsetX + cam->H * view.x / view.z;
	*sy = cam->offsetY + cam->H * view.y / view.z;
}

internal int EngineFx_ParticleSelfTest(int *checks)
{
	// axis kinds: flags, rotX, rotY, scaleX, scaleY (only the flagged ones are read)
	static const struct
	{
		u16 flags;
		s32 rotX, rotY, scaleX, scaleY;
	} kinds[] = {
	    {0, 0, 0, 0, 0},
	    {PARTICLE_AXIS_FLAG_SCALE_X, 0, 0, 0x1800, 0},
	    {PARTICLE_AXIS_FLAG_SCALE_X | PARTICLE_AXIS_FLAG_SCALE_Y, 0, 0, 0x1200, 0x2a00},
	    {PARTICLE_AXIS_FLAG_SCALE_Y, 0, 0, 0, 0x0c00},
	    {PARTICLE_AXIS_FLAG_ROT_Y, 0, 300, 0, 0},
	    {PARTICLE_AXIS_FLAG_ROT_Y, 0, 3000, 0, 0},
	    {PARTICLE_AXIS_FLAG_ROT_Y | PARTICLE_AXIS_FLAG_SCALE_X, 0, 1500, 0x0900, 0},
	    {PARTICLE_AXIS_FLAG_ROT_Y | PARTICLE_AXIS_FLAG_SCALE_Y, 0, 700, 0, 0x2000},
	    {PARTICLE_AXIS_FLAG_ROT_Y | PARTICLE_AXIS_FLAG_SCALE_X | PARTICLE_AXIS_FLAG_SCALE_Y, 0, 2500, 0x1400, 0x0a00},
	    {PARTICLE_AXIS_FLAG_ROT_X, 400, 0, 0, 0},
	    {PARTICLE_AXIS_FLAG_ROT_X | PARTICLE_AXIS_FLAG_SCALE_X, 900, 0, 0x1100, 0},
	    {PARTICLE_AXIS_FLAG_ROT_X | PARTICLE_AXIS_FLAG_SCALE_Y, 3500, 0, 0, 0x1600},
	    {PARTICLE_AXIS_FLAG_ROT_X | PARTICLE_AXIS_FLAG_ROT_Y, 600, 200, 0, 0},
	    {PARTICLE_AXIS_FLAG_ROT_X | PARTICLE_AXIS_FLAG_ROT_Y | PARTICLE_AXIS_FLAG_SCALE_X, 300, 1200, 0x0e00, 0},
	    {PARTICLE_AXIS_FLAG_ROT_X | PARTICLE_AXIS_FLAG_ROT_Y | PARTICLE_AXIS_FLAG_SCALE_X | PARTICLE_AXIS_FLAG_SCALE_Y, 800, 2900, 0x1300, 0x0b00},
	};
	static const int sa[4] = {-1, 1, 1, -1};
	static const int sb[4] = {-1, -1, 1, 1};
	static const u8 colour[4] = {10, 20, 30, 255};
	int failures = 0;
	char msg[160];

	// The rule.
	EngineFx_ExhaustCheck(EngineFx_ParticleRule(1, 1), "particle rule: taken with the engine on and the engine track drawn", checks, &failures);
	EngineFx_ExhaustCheck(!EngineFx_ParticleRule(0, 1), "particle rule: not without the engine (--no-engine, the fallback)", checks, &failures);
	EngineFx_ExhaustCheck(!EngineFx_ParticleRule(1, 0), "particle rule: not in a frame the PS1 walk draws the track", checks, &failures);

	// The lift of a ground line: the skid marks' lift and depth bias for a
	// driver-local line with the driver's height, nothing for the others.
	{
		// 0x58a2, the wheel spray and sparks of VehEmitter.c
		const u16 spray = PARTICLE_SET_COLOR_FLAG_SPECIAL_LINE | PARTICLE_SET_COLOR_FLAG_SPECIAL_LINE_KEEP_PREVIOUS | PARTICLE_SET_COLOR_FLAG_DRIVER_LOCAL |
		                  PARTICLE_SET_COLOR_FLAG_SEMI_TRANSPARENT | 0x22u;
		float lift = -1.0f;
		float bias = -1.0f;

		EngineFx_ExhaustCheck(EngineFx_ParticleLineGround(spray, &lift, &bias) && (lift == ENGINE_GROUND_LIFT) && (bias == ENGINE_GROUND_DEPTH_BIAS),
		                      "particle line: wheel spray lifted as the skid marks", checks, &failures);
		EngineFx_ExhaustCheck(EngineFx_ParticleLineGround(PARTICLE_SET_COLOR_FLAG_SPECIAL_LINE | PARTICLE_SET_COLOR_FLAG_DRIVER_LOCAL, &lift, &bias) &&
		                          (lift == ENGINE_GROUND_LIFT),
		                      "particle line: grass spray (0x1800) lifted", checks, &failures);
		EngineFx_ExhaustCheck(!EngineFx_ParticleLineGround(PARTICLE_SET_COLOR_FLAG_SPECIAL_LINE | PARTICLE_SET_COLOR_FLAG_SEMI_TRANSPARENT, &lift, &bias) &&
		                          (lift == 0.0f) && (bias == 0.0f),
		                      "particle line: a world line is not lifted", checks, &failures);
		EngineFx_ExhaustCheck(!EngineFx_ParticleLineGround(PARTICLE_SET_COLOR_FLAG_SPECIAL_LINE | PARTICLE_SET_COLOR_FLAG_DRIVER_LOCAL |
		                                                       PARTICLE_SET_COLOR_FLAG_DRIVER_LOCAL_IGNORE_Y,
		                                                   &lift, &bias) &&
		                          (lift == 0.0f),
		                      "particle line: a driver-local line with its own height (IGNORE_Y) is not lifted", checks, &failures);
	}

	// The kind: the slot of the icon group; the first slot wins; no group or an unknown one is "other".
	{
		static char groups[ENGINE_FX_PARTICLE_SLOTS + 2];
		struct IconGroup *slots[ENGINE_FX_PARTICLE_SLOTS];

		for (int k = 0; k < ENGINE_FX_PARTICLE_SLOTS; k++)
		{
			slots[k] = (struct IconGroup *)(void *)&groups[k];
		}
		slots[0x7] = slots[0x1]; // the same group in two slots
		EngineFx_ExhaustCheck(EngineFx_ParticleKindOf(slots[0x3], slots, ENGINE_FX_PARTICLE_SLOTS) == 0x3, "particle kind: slot 3", checks, &failures);
		EngineFx_ExhaustCheck(EngineFx_ParticleKindOf(slots[0xA], slots, ENGINE_FX_PARTICLE_SLOTS) == 0xA, "particle kind: the flame slot 0xA", checks, &failures);
		EngineFx_ExhaustCheck(EngineFx_ParticleKindOf(slots[0x10], slots, ENGINE_FX_PARTICLE_SLOTS) == 0x10, "particle kind: the last slot", checks, &failures);
		EngineFx_ExhaustCheck(EngineFx_ParticleKindOf(slots[0x7], slots, ENGINE_FX_PARTICLE_SLOTS) == 0x1, "particle kind: the first of two slots", checks,
		                      &failures);
		EngineFx_ExhaustCheck(EngineFx_ParticleKindOf((struct IconGroup *)(void *)&groups[ENGINE_FX_PARTICLE_SLOTS + 1], slots, ENGINE_FX_PARTICLE_SLOTS) ==
		                          ENGINE_FX_PARTICLE_OTHER,
		                      "particle kind: a group outside the table is other", checks, &failures);
		EngineFx_ExhaustCheck(EngineFx_ParticleKindOf(NULL, slots, ENGINE_FX_PARTICLE_SLOTS) == ENGINE_FX_PARTICLE_OTHER, "particle kind: no group is other",
		                      checks, &failures);
	}

	for (int cameraSet = 0; cameraSet < 2; cameraSet++)
	{
		EngineCamera cam;

		if (cameraSet == 0)
		{
			EngineFx_ExhaustTestCamera(&cam, 0.0f, 0.0f, 0.0f, 0.0f);
		}
		else
		{
			EngineFx_ExhaustTestCamera(&cam, 700.0f, 30.0f, -10.0f, 20.0f);
		}

		// Normal quads: every corner where retail's RTPS puts it (its matrix, its
		// corner order, each corner by its own depth), within a quarter pixel.
		for (int i = 0; i < (int)(sizeof(kinds) / sizeof(kinds[0])); i++)
		{
			for (int large = 0; large < 2; large++)
			{
				const int texW = 24;
				const int texH = 16;
				const int halfW = texW << (large ? 4 : 1);
				const int halfH = texH << (large ? 4 : 1);
				const float zw = large ? 3000.0f : 400.0f;
				const EngineVec3 centre = EngineFx_ExhaustTestWorld(&cam, 40.0f, -25.0f, zw);
				const EngineVec3 centreView = Engine_Mat4MulPoint(&cam.viewFromWorld, centre);
				struct Particle p;
				struct ParticleRenderListMatrix mx;
				EngineVertex v[4];
				float worst = 0.0f;

				memset(&p, 0, sizeof(p));
				p.axis[PARTICLE_AXIS_ROT_X_OR_LINE_PREV_X].startVal = kinds[i].rotX;
				p.axis[PARTICLE_AXIS_ROT_Y_OR_LINE_PREV_Z].startVal = kinds[i].rotY;
				p.axis[PARTICLE_AXIS_SCALE_X_OR_LINE_SCALE].startVal = kinds[i].scaleX;
				p.axis[PARTICLE_AXIS_SCALE_Y_OR_LINE_PREV_Y].startVal = kinds[i].scaleY;
				mx = Particle_RenderList_BuildNormalMatrix(&p, kinds[i].flags);

				EngineFx_ExhaustCheck(EngineFx_ParticleCorners(&cam, centre, &mx, halfW, halfH, v), "particle corners build", checks, &failures);
				for (int c = 0; c < 4; c++)
				{
					// retail: V = TR + M * (a, b, 0) / 4096 in the 4x view space, TR = 4 * view(centre)
					const float a = (float)(sa[c] * halfW);
					const float b = (float)(sb[c] * halfH);
					const float vx = 4.0f * centreView.x + ((float)(s16)(mx.r11r12 & 0xffffu) * a + (float)(s16)(mx.r11r12 >> 16) * b) / 4096.0f;
					const float vy = 4.0f * centreView.y + ((float)(s16)(mx.r13r21 >> 16) * a + (float)(s16)(mx.r22r23 & 0xffffu) * b) / 4096.0f;
					const float vz = 4.0f * centreView.z + ((float)(s16)(mx.r31r32 & 0xffffu) * a + (float)(s16)(mx.r31r32 >> 16) * b) / 4096.0f;
					const float rx = cam.offsetX + cam.H * vx / vz;
					const float ry = cam.offsetY + cam.H * vy / vz;
					float ex;
					float ey;

					EngineFx_ParticleTestScreen(&cam, v[c].position[0], v[c].position[1], v[c].position[2], &ex, &ey);
					worst = (EngineFx_ExhaustAbs(ex - rx) > worst) ? EngineFx_ExhaustAbs(ex - rx) : worst;
					worst = (EngineFx_ExhaustAbs(ey - ry) > worst) ? EngineFx_ExhaustAbs(ey - ry) : worst;
				}
				snprintf(msg, sizeof(msg), "particle axes 0x%03x large %d camera %d: corners off retail's RTPS by %.3f px", kinds[i].flags, large, cameraSet,
				         worst);
				EngineFx_ExhaustCheck(worst < 0.25f, msg, checks, &failures);

				// Without a rotation the size is the exhaust formula's (an independent derivation).
				if ((kinds[i].flags & (PARTICLE_AXIS_FLAG_ROT_X | PARTICLE_AXIS_FLAG_ROT_Y)) == 0)
				{
					EngineVertex q[4];
					u16 idx[6];
					float w0;
					float h0;
					float w1;
					float h1;
					const int scaleX = ((kinds[i].flags & PARTICLE_AXIS_FLAG_SCALE_X) != 0) ? kinds[i].scaleX : 0;
					const int scaleY = ((kinds[i].flags & PARTICLE_AXIS_FLAG_SCALE_Y) != 0) ? kinds[i].scaleY : 0;

					// The exhaust formula reads a missing SCALE_X as 1.0 for the width and a
					// missing SCALE_Y as SCALE_X for the height, as retail's matrix does.
					EngineFx_ExhaustCheck(EngineFx_ExhaustQuadXY(&cam, centre, texW, texH, large, scaleX, scaleY, 0, 0, colour, q, idx),
					                      "exhaust quad builds", checks, &failures);
					EngineFx_ExhaustExtent(&cam, q, &w0, &h0);
					EngineFx_ExhaustExtent(&cam, v, &w1, &h1);
					snprintf(msg, sizeof(msg), "particle axes 0x%03x large %d: %.2f x %.2f against the exhaust formula %.2f x %.2f", kinds[i].flags, large, w1,
					         h1, w0, h0);
					EngineFx_ExhaustCheck((EngineFx_ExhaustAbs(w1 - w0) < 0.5f) && (EngineFx_ExhaustAbs(h1 - h0) < 0.5f), msg, checks, &failures);
				}
			}
		}

		// Lines: the ends where the points project, 1 pixel wide, half a pixel past each end.
		{
			const EngineVec3 p0 = EngineFx_ExhaustTestWorld(&cam, -30.0f, 10.0f, 400.0f);
			const EngineVec3 p1 = EngineFx_ExhaustTestWorld(&cam, 25.0f, -40.0f, 520.0f);
			static const u8 c0[4] = {200, 100, 50, 255};
			static const u8 c1[4] = {5, 6, 7, 255};
			EngineVertex v[4];
			float s[4][2];
			float e0x;
			float e0y;
			float e1x;
			float e1y;
			float width;
			float length;
			float want;

			EngineFx_ExhaustCheck(EngineFx_ParticleLineQuad(&cam, p0, p1, 0.5f, c0, c1, v), "particle line builds", checks, &failures);
			EngineFx_ExhaustCheck((v[0].uvBox[0] == 0) && (v[0].uvBox[2] == 0) && (v[3].uvBox[1] == 0) && (v[3].uvBox[3] == 0),
			                      "particle line: untextured, the UV box is the whole page", checks, &failures);
			for (int c = 0; c < 4; c++)
			{
				EngineFx_ParticleTestScreen(&cam, v[c].position[0], v[c].position[1], v[c].position[2], &s[c][0], &s[c][1]);
			}
			EngineFx_ParticleTestScreen(&cam, p0.x, p0.y, p0.z, &e0x, &e0y);
			EngineFx_ParticleTestScreen(&cam, p1.x, p1.y, p1.z, &e1x, &e1y);
			// the middle of the p0 side (v0, v3) and of the p1 side (v1, v2), half a pixel past the ends
			width = sqrtf((s[0][0] - s[3][0]) * (s[0][0] - s[3][0]) + (s[0][1] - s[3][1]) * (s[0][1] - s[3][1]));
			length = sqrtf(((s[1][0] + s[2][0]) - (s[0][0] + s[3][0])) * ((s[1][0] + s[2][0]) - (s[0][0] + s[3][0])) * 0.25f +
			               ((s[1][1] + s[2][1]) - (s[0][1] + s[3][1])) * ((s[1][1] + s[2][1]) - (s[0][1] + s[3][1])) * 0.25f);
			want = sqrtf((e1x - e0x) * (e1x - e0x) + (e1y - e0y) * (e1y - e0y)) + 1.0f;
			snprintf(msg, sizeof(msg), "particle line camera %d: width %.3f, length %.3f (want 1, %.3f)", cameraSet, width, length, want);
			EngineFx_ExhaustCheck((EngineFx_ExhaustAbs(width - 1.0f) < 0.01f) && (EngineFx_ExhaustAbs(length - want) < 0.01f), msg, checks, &failures);
			EngineFx_ExhaustCheck((v[0].color[0] == 200) && (v[3].color[1] == 100) && (v[1].color[2] == 7) && (v[2].color[0] == 5), "particle line colours per end",
			                      checks, &failures);

			// a line of no length is a 1 x 1 pixel point, as the PS1 draws it
			EngineFx_ExhaustCheck(EngineFx_ParticleLineQuad(&cam, p0, p0, 0.5f, c0, c1, v), "particle point builds", checks, &failures);
			for (int c = 0; c < 4; c++)
			{
				EngineFx_ParticleTestScreen(&cam, v[c].position[0], v[c].position[1], v[c].position[2], &s[c][0], &s[c][1]);
			}
			EngineFx_ExhaustCheck((EngineFx_ExhaustAbs(EngineFx_ExhaustAbs(s[1][0] - s[0][0]) - 1.0f) < 0.01f) &&
			                          (EngineFx_ExhaustAbs(EngineFx_ExhaustAbs(s[3][1] - s[0][1]) - 1.0f) < 0.01f),
			                      "particle point: 1 x 1 pixel", checks, &failures);

			// an end nearer than the near plane is refused (counted as near cut by the caller)
			EngineFx_ExhaustCheck(!EngineFx_ParticleLineQuad(&cam, p0, EngineFx_ExhaustTestWorld(&cam, 0.0f, 0.0f, cam.zNear * 0.5f), 0.5f, c0, c1, v),
			                      "particle line: an end before the near plane is refused", checks, &failures);
		}
	}

	// The line's delta as the GTE register takes it.
	{
		struct Particle p;
		s32 d[3];

		memset(&p, 0, sizeof(p));
		p.axis[PARTICLE_AXIS_POS_X].startVal = 64 * 1000;
		p.axis[PARTICLE_AXIS_ROT_X_OR_LINE_PREV_X].startVal = 64 * 1100;
		p.axis[PARTICLE_AXIS_POS_Y].startVal = 64 * 50;
		p.axis[PARTICLE_AXIS_SCALE_Y_OR_LINE_PREV_Y].startVal = 0;
		p.axis[PARTICLE_AXIS_POS_Z].startVal = 0;
		p.axis[PARTICLE_AXIS_ROT_Y_OR_LINE_PREV_Z].startVal = 64 * 40000;
		EngineFx_ParticleLineDelta(&p, 0, d);
		EngineFx_ExhaustCheck((d[0] == 100) && (d[1] == -50) && (d[2] == (s32)(s16)40000), "particle line delta: (prev - pos) >> 6, 16 bits", checks, &failures);
		p.axis[PARTICLE_AXIS_SCALE_X_OR_LINE_SCALE].startVal = 0x8000;
		p.axis[PARTICLE_AXIS_ROT_Y_OR_LINE_PREV_Z].startVal = 64 * 300;
		EngineFx_ParticleLineDelta(&p, PARTICLE_AXIS_FLAG_SCALE_X, d);
		EngineFx_ExhaustCheck((d[0] == 50) && (d[1] == -25) && (d[2] == 150), "particle line delta: times scale >> 16", checks, &failures);
	}

	return failures;
}

int Engine_SelfTestExhaust(int *checks)
{
	static const struct
	{
		int texW, texH, large, scaleX;
	} sizes[] = {{32, 32, 0, 0}, {24, 16, 0, 1536}, {16, 16, 0, 3536}, {8, 8, 1, 0}, {20, 12, 0, 512}};
	static const u8 colour[4] = {10, 20, 30, 255};
	int failures = 0;
	int cameraSet;
	int i;

	for (cameraSet = 0; cameraSet < 2; cameraSet++)
	{
		EngineCamera cam;
		float row0;
		float row1;

		// 0: the camera looks straight along z. 1: turned by 700 units, moved.
		if (cameraSet == 0)
		{
			EngineFx_ExhaustTestCamera(&cam, 0.0f, 0.0f, 0.0f, 0.0f);
		}
		else
		{
			EngineFx_ExhaustTestCamera(&cam, 700.0f, 30.0f, -10.0f, 20.0f);
		}
		row0 = EngineFx_ExhaustRowLength(&cam, 0);
		row1 = EngineFx_ExhaustRowLength(&cam, 1);
		EngineFx_ExhaustCheck((EngineFx_ExhaustAbs(row0 - 1.0f) < 0.0001f) && (EngineFx_ExhaustAbs(row1 - 0.5625f) < 0.0001f), "test camera rows 1 and 9/16",
		                      checks, &failures);

		for (i = 0; i < (int)(sizeof(sizes) / sizeof(sizes[0])); i++)
		{
			const float zw = 400.0f; // view z of the centre in world units
			const EngineVec3 centre = EngineFx_ExhaustTestWorld(&cam, 40.0f, -25.0f, zw);
			// retail, in float: corner offsets are r11/4096 * halfW and r22/4096 * halfH
			// in 4x view units; screen = H * offset / (4 * zw)
			const float r11 = (sizes[i].scaleX != 0) ? (float)(sizes[i].scaleX * 2) : 8192.0f;
			const float r22 = (sizes[i].scaleX != 0) ? (float)sizes[i].scaleX : 4096.0f;
			const float halfW = (float)(sizes[i].texW << (sizes[i].large ? 4 : 1));
			const float halfH = (float)(sizes[i].texH << (sizes[i].large ? 4 : 1));
			const float retailW = 2.0f * cam.H * (r11 / 4096.0f * halfW) / (4.0f * zw);
			const float retailH = 2.0f * cam.H * (r22 / 4096.0f * halfH) / (4.0f * zw);
			EngineVertex v[4];
			u16 idx[6];
			float w;
			float h;
			char msg[96];

			// roll 0: the same screen size as retail within half a pixel
			EngineFx_ExhaustCheck(EngineFx_ExhaustQuad(&cam, centre, sizes[i].texW, sizes[i].texH, sizes[i].large, sizes[i].scaleX, 0, 0, colour, v, idx),
			                      "quad builds", checks, &failures);
			EngineFx_ExhaustExtent(&cam, v, &w, &h);
			snprintf(msg, sizeof(msg), "roll 0 size %d x %d large %d scale %d: engine %.2f x %.2f, retail %.2f x %.2f", sizes[i].texW, sizes[i].texH,
			         sizes[i].large, sizes[i].scaleX, w, h, retailW, retailH);
			EngineFx_ExhaustCheck((EngineFx_ExhaustAbs(w - retailW) < 0.5f) && (EngineFx_ExhaustAbs(h - retailH) < 0.5f), msg, checks, &failures);

			// ROT_Y present with angle 0 is the same as no ROT_Y
			EngineFx_ExhaustCheck(EngineFx_ExhaustQuad(&cam, centre, sizes[i].texW, sizes[i].texH, sizes[i].large, sizes[i].scaleX, 1, 0, colour, v, idx),
			                      "quad builds (rot 0)", checks, &failures);
			EngineFx_ExhaustExtent(&cam, v, &w, &h);
			EngineFx_ExhaustCheck((EngineFx_ExhaustAbs(w - retailW) < 0.5f) && (EngineFx_ExhaustAbs(h - retailH) < 0.5f), "ROT_Y 0 keeps the roll 0 size", checks,
			                      &failures);

			// a quarter turn (ROT_Y 3072 = roll 1024): width and height swap their
			// world half sizes. Expected from the header formula, then compared with
			// retail's own quarter turn (its width is H*s*halfH/z): within 13 per cent.
			{
				const float s = (sizes[i].scaleX != 0) ? ((float)sizes[i].scaleX / 4096.0f) : 1.0f;
				const float expectW = cam.H * s * halfH * row0 / (2.0f * row1 * zw);
				const float expectH = cam.H * s * halfW * row1 / (row0 * zw);
				const float retailQuarterW = cam.H * s * halfH / zw;
				const float retailQuarterH = cam.H * s * halfW / (2.0f * zw);

				EngineFx_ExhaustCheck(EngineFx_ExhaustQuad(&cam, centre, sizes[i].texW, sizes[i].texH, sizes[i].large, sizes[i].scaleX, 1, 3072, colour, v, idx),
				                      "quad builds (quarter turn)", checks, &failures);
				EngineFx_ExhaustExtent(&cam, v, &w, &h);
				snprintf(msg, sizeof(msg), "quarter turn size %d x %d scale %d: engine %.2f x %.2f, expected %.2f x %.2f", sizes[i].texW, sizes[i].texH,
				         sizes[i].scaleX, w, h, expectW, expectH);
				EngineFx_ExhaustCheck((EngineFx_ExhaustAbs(w - expectW) < 0.5f) && (EngineFx_ExhaustAbs(h - expectH) < 0.5f), msg, checks, &failures);
				EngineFx_ExhaustCheck((w > 0.85f * retailQuarterW) && (w < 1.15f * retailQuarterW) && (h > 0.85f * retailQuarterH) && (h < 1.15f * retailQuarterH),
				                      "quarter turn within 15 per cent of the retail rotation", checks, &failures);
			}
		}

		// roll sign: a positive ROT_Y turns the sprite counter-clockwise, so the
		// top edge (vertex 0 to vertex 1) rises on the screen (y decreases).
		{
			const EngineVec3 centre = EngineFx_ExhaustTestWorld(&cam, 0.0f, 0.0f, 400.0f);
			EngineVertex v[4];
			u16 idx[6];
			float sy0;
			float sy1;
			EngineVec3 w0;
			EngineVec3 w1;

			EngineFx_ExhaustCheck(EngineFx_ExhaustQuad(&cam, centre, 32, 32, 0, 0, 1, 256, colour, v, idx), "quad builds (roll sign)", checks, &failures);
			w0.x = v[0].position[0];
			w0.y = v[0].position[1];
			w0.z = v[0].position[2];
			w1.x = v[1].position[0];
			w1.y = v[1].position[1];
			w1.z = v[1].position[2];
			w0 = Engine_Mat4MulPoint(&cam.viewFromWorld, w0);
			w1 = Engine_Mat4MulPoint(&cam.viewFromWorld, w1);
			sy0 = cam.offsetY + cam.H * w0.y / w0.z;
			sy1 = cam.offsetY + cam.H * w1.y / w1.z;
			EngineFx_ExhaustCheck(sy1 < sy0 - 1.0f, "ROT_Y > 0 turns counter-clockwise on the screen", checks, &failures);
			EngineFx_ExhaustCheck(v[0].color[0] == 10 && v[1].color[1] == 20 && v[2].color[2] == 30 && v[3].color[3] == 255, "billboard colour", checks,
			                      &failures);
		}
	}

	// A degenerate camera is refused.
	{
		EngineCamera cam;
		EngineVertex v[4];
		u16 idx[6];

		memset(&cam, 0, sizeof(cam));
		EngineFx_ExhaustCheck(!EngineFx_ExhaustQuad(&cam, EngineFx_ExhaustTestWorld(&cam, 0, 0, 400), 32, 32, 0, 0, 0, 0, colour, v, idx),
		                      "a camera without rows builds nothing", checks, &failures);
	}

	// Colour, raw and semi decoding through the retail Particle_SetColors.
	{
		struct Particle p;
		u8 rgba[4];
		int semi;
		int raw;
		u32 word;

		// red axis only: G = B = R; 16384 >> 8 = 64; semi-transparent; value not changed
		memset(&p, 0, sizeof(p));
		p.axis[PARTICLE_AXIS_COLOR_R].startVal = 16384;
		word = Particle_SetColors(PARTICLE_AXIS_FLAG_COLOR_R, PARTICLE_SET_COLOR_FLAG_DRIVER_LOCAL | PARTICLE_SET_COLOR_FLAG_SEMI_TRANSPARENT, &p);
		EngineFx_ExhaustDecodeColor(word, rgba, &semi, &raw);
		EngineFx_ExhaustCheck((rgba[0] == 64) && (rgba[1] == 64) && (rgba[2] == 64) && (rgba[3] == 255) && semi && !raw, "exhaust colour 16384 -> 64 64 64, semi",
		                      checks, &failures);
		EngineFx_ExhaustCheck(p.axis[PARTICLE_AXIS_COLOR_R].startVal == 16384, "an in-range colour axis is not changed by SetColors", checks, &failures);

		// all three axes
		memset(&p, 0, sizeof(p));
		p.axis[PARTICLE_AXIS_COLOR_R].startVal = 0x8000;
		p.axis[PARTICLE_AXIS_COLOR_G].startVal = 0x2000;
		p.axis[PARTICLE_AXIS_COLOR_B].startVal = 0xff00;
		word = Particle_SetColors(PARTICLE_AXIS_FLAG_COLOR_R | PARTICLE_AXIS_FLAG_COLOR_G | PARTICLE_AXIS_FLAG_COLOR_B, 0, &p);
		EngineFx_ExhaustDecodeColor(word, rgba, &semi, &raw);
		EngineFx_ExhaustCheck((rgba[0] == 128) && (rgba[1] == 32) && (rgba[2] == 255) && !semi && !raw, "exhaust colour R G B axes, not semi", checks, &failures);

		// no colour axis (the water exhaust): raw texture, neutral colour, semi from the flag
		memset(&p, 0, sizeof(p));
		word = Particle_SetColors(0, PARTICLE_SET_COLOR_FLAG_DRIVER_LOCAL | PARTICLE_SET_COLOR_FLAG_SEMI_TRANSPARENT, &p);
		EngineFx_ExhaustDecodeColor(word, rgba, &semi, &raw);
		EngineFx_ExhaustCheck((rgba[0] == 128) && (rgba[1] == 128) && (rgba[2] == 128) && semi && raw, "no colour axis: raw texture, 128, semi", checks, &failures);
		EngineFx_ExhaustCheck(p.axis[PARTICLE_AXIS_COLOR_R].startVal == 0, "no colour axis: nothing written", checks, &failures);

		// the clamp itself (what the emitter data never triggers): high and low
		memset(&p, 0, sizeof(p));
		p.axis[PARTICLE_AXIS_COLOR_R].startVal = 70000;
		word = Particle_SetColors(PARTICLE_AXIS_FLAG_COLOR_R, 0, &p);
		EngineFx_ExhaustDecodeColor(word, rgba, &semi, &raw);
		EngineFx_ExhaustCheck((rgba[0] == 255) && (p.axis[PARTICLE_AXIS_COLOR_R].startVal == PARTICLE_COLOR_CHANNEL_MAX), "colour above 0xff00 clamps and is written back",
		                      checks, &failures);
		p.axis[PARTICLE_AXIS_COLOR_R].startVal = -5;
		word = Particle_SetColors(PARTICLE_AXIS_FLAG_COLOR_R, 0, &p);
		EngineFx_ExhaustDecodeColor(word, rgba, &semi, &raw);
		EngineFx_ExhaustCheck((rgba[0] == 0) && (p.axis[PARTICLE_AXIS_COLOR_R].startVal == 0), "colour below 0 clamps and is written back", checks, &failures);
	}

	// The proof of the header, against the emitter data.
	EngineFx_ExhaustTestEmitter(data.emSet_Exhaust_High, "Exhaust_High", 1, checks, &failures);
	EngineFx_ExhaustTestEmitter(data.emSet_Exhaust_Med, "Exhaust_Med", 1, checks, &failures);
	EngineFx_ExhaustTestEmitter(data.emSet_Exhaust_Low, "Exhaust_Low", 1, checks, &failures);
	EngineFx_ExhaustTestEmitter(data.emSet_Exhaust_Water, "Exhaust_Water", 0, checks, &failures);

	// TRACK MOD FLAMES: the rule, the size of a column, the no-op colour call.
	{
		const u32 col = PARTICLE_SET_COLOR_FLAG_LARGE_QUAD | PARTICLE_SET_COLOR_FLAG_SEMI_TRANSPARENT | 0x20u;
		EngineCamera cam;
		EngineVertex v[4];
		u16 idx[6];
		struct Particle p;
		float w;
		float h;
		u32 word;

		EngineFx_ExhaustCheck(EngineFx_FlameRule(1, 1, 1, 0x67u, col, 0), "flame rule: the column is taken", checks, &failures);
		EngineFx_ExhaustCheck(!EngineFx_FlameRule(0, 1, 1, 0x67u, col, 0), "flame rule: not on a retail level", checks, &failures);
		EngineFx_ExhaustCheck(!EngineFx_FlameRule(1, 0, 1, 0x67u, col, 0), "flame rule: not when the PS1 walk drew the track", checks, &failures);
		EngineFx_ExhaustCheck(!EngineFx_FlameRule(1, 1, 0, 0x67u, col, 0), "flame rule: not outside the flame slot", checks, &failures);
		EngineFx_ExhaustCheck(!EngineFx_FlameRule(1, 1, 1, 0xb7u, col, 0), "flame rule: not the retail flame jet / fireball axes", checks, &failures);
		EngineFx_ExhaustCheck(!EngineFx_FlameRule(1, 1, 1, 0x67u, col, 1), "flame rule: not with a function", checks, &failures);
		EngineFx_ExhaustCheck(!EngineFx_FlameRule(1, 1, 1, 0x67u, col | PARTICLE_SET_COLOR_FLAG_DRIVER_LOCAL, 0), "flame rule: not driver-local", checks,
		                      &failures);

		// A 32 x 32 large column, scaleX 0x1200, scaleY 0x2a00, at view z 400: retail
		// r11 = 0x2400, r22 = 0x2a00, halfW = halfH = 512; screen = 2 H r/4096 half / (4 z).
		EngineFx_ExhaustTestCamera(&cam, 0.0f, 0.0f, 0.0f, 0.0f);
		{
			const float retailW = 2.0f * cam.H * ((float)(0x1200 * 2) / 4096.0f * 512.0f) / (4.0f * 400.0f);
			const float retailH = 2.0f * cam.H * ((float)0x2a00 / 4096.0f * 512.0f) / (4.0f * 400.0f);

			EngineFx_ExhaustCheck(EngineFx_ExhaustQuadXY(&cam, EngineFx_ExhaustTestWorld(&cam, 0.0f, 0.0f, 400.0f), 32, 32, 1, 0x1200, 0x2a00, 0, 0, colour, v,
			                                             idx),
			                      "flame quad builds", checks, &failures);
			EngineFx_ExhaustExtent(&cam, v, &w, &h);
			EngineFx_ExhaustCheck((EngineFx_ExhaustAbs(w - retailW) < 0.5f) && (EngineFx_ExhaustAbs(h - retailH) < 0.5f), "flame column: retail screen size",
			                      checks, &failures);
		}

		// Particle_SetColors with the column's axes writes nothing (no COLOR_R bit).
		memset(&p, 0x5a, sizeof(p));
		word = Particle_SetColors(0x67u, col, &p);
		EngineFx_ExhaustCheck(p.axis[PARTICLE_AXIS_COLOR_R].startVal == (int)0x5a5a5a5a,
		                      "flame colour call writes no axis", checks, &failures);
		EngineFx_ExhaustCheck((word & PARTICLE_GPU_CODE_SHADE_TEXTURE) != 0, "flame colour: raw texture", checks, &failures);
	}

	// WORLD PARTICLES.
	failures += EngineFx_ParticleSelfTest(checks);

	return failures;
}
