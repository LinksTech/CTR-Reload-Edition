#include <platform/engine.h>
#include <math.h>
#include <stdio.h>

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
internal int EngineFx_ExhaustQuad(const EngineCamera *cam, EngineVec3 centre, int texW, int texH, int large, int scaleX, int hasRotY, int rotY,
                                  const u8 colour[4], EngineVertex v[4], u16 idx[6])
{
	const float s = (scaleX != 0) ? ((float)scaleX / 4096.0f) : 1.0f;
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
	return Engine_FxBillboard(cam, centre, s * halfW / (2.0f * row0), s * halfH / (4.0f * row1), roll, colour, v, idx, 0) == 4;
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
	if (lookHide)
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
	Engine_FxNoteDrawn(ENGINE_FX_EXHAUST);
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

	return failures;
}
