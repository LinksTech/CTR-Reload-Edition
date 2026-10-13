#include <platform/engine.h>

// ENGINE FX EXHAUST - the exhaust smoke of custom drivers drawn by the engine (step 4).
// See include/platform/engine.h, EFFECTS OF CUSTOM DRIVERS.
//
// DRAW TIME ONLY. The game births and simulates the exhaust particles exactly as
// before (the emitter uses the RNG). Particle_RenderList (game/Particle.c) still
// projects every particle through the GTE and writes its POLY_FT4 as always; for
// a particle of an owned driver it then hands that finished primitive to
// EngineFx_ExhaustParticle instead of linking it into the ordering table. The
// engine therefore reproduces the retail sprite exactly, with no second copy of
// the size / rotation / scale maths:
//   - corners   : the primitive's four screen points (x0..x3, y0..y3: PSX pixels
//                 of the view, the geom offset included - the space the engine's
//                 clipFromModel ends in). The custom look's moved exhaust point
//                 is already in them (NativeChar_ExhaustDraw ran before the
//                 projection); a quad the look hides is never handed over.
//   - size      : the primitive's, i.e. the retail size in pixels, unchanged.
//   - depth     : the GTE view z of the centre (SZ3). Particles live in a space
//                 of four times the world scale (position << 2), so world view z
//                 is depth / 4. It only decides the depth value and the sort key;
//                 the screen position is given directly.
//   - UVs/CLUT/tpage : the primitive's u0..u3 / v0..v3, clut, tpage (page texels).
//   - colour    : r0 g0 b0 (128 = neutral, the "ne" VRAM path multiplies by 2);
//                 the raw-texture bit of the code means no shading (128).
//   - blend     : the code's semi-transparency bit with the abr of the tpage
//                 (Engine_BlendFromAbr) and ENGINE_MATERIAL_SEMI_STP; otherwise
//                 opaque (the VRAM path still discards texel 0x0000).
//   - OT cell   : the caller's (the one LinkAndAdvance would have used).
//
// Screen-space quad: the vertices are (X, Y, 0) in "model" space and the matrix
// is  x' = X * Zv,  y' = Y * Zv,  z' = zNear,  w = Zv.  After the divide the
// vertex is exactly (X, Y) pixels at reverse-Z depth zNear / Zv.

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

// Returns 0 = not an exhaust particle of an owned driver (the caller does what
// it always did), 1 = the engine took it (the caller must not link the retail
// primitive), -1 = an owned particle the engine could not take (no camera,
// refused by the scene): the caller links the retail primitive and it is
// counted with Engine_FxNoteRetail here.
int EngineFx_ExhaustParticle(const struct Particle *p, int view, const POLY_FT4 *poly, s32 depth, u32 *ot, int cell)
{
	const EngineCamera *cam;
	EngineVertex v[4];
	static const u16 idx[6] = {0, 1, 2, 2, 1, 3};
	EngineDraw draw;
	float zv;
	int material;
	int semi;
	int raw;
	int i;

	if (!EngineFx_ExhaustOwned(p))
	{
		return 0;
	}

	cam = Engine_FrameCamera(view);
	if ((cam == NULL) || !cam->valid || (poly == NULL) || (ot == NULL))
	{
		Engine_FxNoteRetail(ENGINE_FX_EXHAUST);
		return -1;
	}

	semi = (poly->code & 0x02u) != 0;
	raw = (poly->code & 0x01u) != 0;
	material = EngineFx_ExhaustMaterial(poly->tpage, poly->clut, semi);
	if (material < 0)
	{
		Engine_FxNoteRetail(ENGINE_FX_EXHAUST);
		return -1;
	}

	zv = (float)depth * 0.25f;
	if (zv < cam->zNear)
	{
		zv = cam->zNear; // depth value only; the screen position is the primitive's
	}

	for (i = 0; i < 4; i++)
	{
		const VERTTYPE *xy = (i == 0) ? &poly->x0 : (i == 1) ? &poly->x1 : (i == 2) ? &poly->x2 : &poly->x3;
		const u8 *uv = (i == 0) ? &poly->u0 : (i == 1) ? &poly->u1 : (i == 2) ? &poly->u2 : &poly->u3;

		v[i].position[0] = (float)xy[0];
		v[i].position[1] = (float)xy[1];
		v[i].position[2] = 0.0f;
		v[i].texcoord[0] = (float)uv[0];
		v[i].texcoord[1] = (float)uv[1];
		v[i].color[0] = raw ? 128 : poly->r0;
		v[i].color[1] = raw ? 128 : poly->g0;
		v[i].color[2] = raw ? 128 : poly->b0;
		v[i].color[3] = 255;
	}

	memset(&draw, 0, sizeof(draw));
	draw.mesh = -1;
	Engine_Mat4Identity(&draw.clipFromModel);
	draw.clipFromModel.m[0] = zv;          // x' = X * Zv
	draw.clipFromModel.m[5] = zv;          // y' = Y * Zv
	draw.clipFromModel.m[10] = 0.0f;
	draw.clipFromModel.m[14] = cam->zNear; // z' = zNear
	draw.clipFromModel.m[15] = zv;         // w = Zv
	draw.tint[0] = draw.tint[1] = draw.tint[2] = draw.tint[3] = 1.0f;
	draw.materialOverride = -1;
	draw.sortDepth = zv;
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

// Scaffold: no checks yet.
int Engine_SelfTestExhaust(int *checks)
{
	(void)checks;
	return 0;
}
