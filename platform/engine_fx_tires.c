#include <platform/engine.h>
#include <math.h>

// ENGINE FX TIRES - the wheels of owned drivers without a wheel mesh, drawn by
// the engine as sprites (round of 2026-10-13). See include/platform/engine.h,
// EFFECTS OF CUSTOM DRIVERS (ENGINE_FX_TIRES). The seat of the hi-res driver
// (--engine-hires-dir) is handed to Engine_HiresWheels first: 3D wheel meshes
// at the same place in the game's frame, no sprite (platform/engine_hires.c).
//
// WHAT THE GAME DOES (game/DrawTires.c). A retail kart, a CMDL-only custom
// driver and a CNET driver without WHLS have no wheel geometry; RenderAllTires
// (game/MAIN/MainFrame_RenderFrame.c:164, :850) calls DrawTires_Solid and, for
// reflective views, DrawTires_Reflection. Per driver and view (StagePlayer,
// DrawTires.c:802 / :1411) each draws four sprite quads (POLY_FT4, code 0x2e).
// The game state that decides them:
//   - the four wheel middles, model space of the kart, in the FOUR-TIMES units
//     of the GTE camera-relative space (BuildWheelLocalPairs, DrawTires.c:337):
//     x = +-(inst->scale.x * 0x90) >> 12, y = (inst->scale.y * 0x40) >> 12,
//     z = (inst->scale.z * 0xc7) >> 12 in front (wheels 0, 1) and
//     (inst->scale.z * -0x60) >> 12 behind (wheels 2, 3); wheels 0, 2 are +x.
//     World = inst->matrix.t + inst->matrix (4.12 rotation) * middle / 4 (the
//     GTE translation is (t - pb->pos) << 2, SetupGteState :392; the middle is
//     added in that space, so it is a quarter in world units).
//   - the axle direction, "rim" (BuildWheelLocalPairs :337ff): the middle moved
//     by the STEERING (front wheels: driver->wheelRotation << 2 as an angle,
//     rim.x = middle.x -+ cos, rim.z = middle.z +- sin), 4096 units along x for
//     the rear wheels, and a small HAZARD wobble (driver->hazardTimer << 5,
//     1/512 or 1/64 of the unit) on rim.y / rim.z. It is rotated by
//     inst->matrix like the middle (without the translation, BuildWheelAxes
//     lcv1, :423ff).
//   - the size: driver->wheelSize. Both quad axes are wheelSize / 64 long in
//     the four-times space (BuildWheelAxes: >> 0x12 and >> 10), i.e. a square
//     with half side wheelSize / 256 world units.
//   - the orientation: the quad stands PERPENDICULAR TO THE RAY from the camera
//     to the wheel middle (normal n = toward the camera, viewNormal :423ff);
//     axis A = unit(n x axle), axis B = unit(A x n) (the second op12, scaled by
//     a negative length). The sprite is therefore camera-facing - it is NOT
//     screen-aligned (Engine_FxBillboard, which faces the camera AXIS and
//     rolls about it, would differ at the edge of a wide view) - and its roll is
//     decided by the projected axle. Corners P0 = c-A-B, P1 = c+A-B, P2 = c-A+B,
//     P3 = c+A+B (LoadCorner order, :536).
//   - the tire icon: wheelSprites[17] (driver->wheelSprites), index
//     DrawTiresSolid_SelectSpriteIndex(angle) (:551, sDrawTiresSpriteIndexTable)
//     with angle = 4096 * (kart axle . n): 0 = edge-on tread ... 16 = side
//     face. There is no rolling animation: the sprite depends on the viewing
//     angle only. The sign of the angle mirrors the sprite (jump index + 4).
//   - the mirror (left/right flip) and the vertex order: the jump index
//     (wheel index, +4 when the angle is negative) picks which projected
//     corner is v0..v3 of the FT4 (ApplyCornerOrder :621), i.e. which of
//     P0..P3 gets the icon corner (u0,v0) top-left, (u1,v1) top-right, (u2,v2)
//     bottom-left, (u3,v3) bottom-right. s_engTiresOrder below is that table.
//   - the colour: PUSHBUFFER_EXISTS views use 0x2e808080, else driver->tireColor
//     (:802ff); r g b = the low three bytes (128 = neutral), the code byte 0x2e
//     means semi-transparent (bit 1) and shaded (bit 0 clear).
//   - the LOD: idpp->lodIndex against the threshold (two with one or two
//     players, else 0), unless NO LOD forces the wheels - decided in StagePlayer
//     BEFORE this function is called, so a wheel set the game would not draw is
//     not drawn here either. NativeChar_ViewHidesWheels (file says no wheels,
//     or the native layer drew native wheels in that view) is checked there
//     first, too: a CNET driver WITH native wheels never reaches this function.
//   - the OT cell: the range pointer idpp->otRangeNormal (idpp->otRangeSecondary
//     when the wheel middle is above the driver's water line, vertSplit >= y,
//     and always the secondary range in the reflection pass), the cell
//     (MAC0 >> 17) = (0x400 * sum of the four corner SZ) >> 17 where SZ = 4 x view
//     z (OTZ >> 5), minus the front-wheel bias (2 cells, set by a rear wheel
//     seen from the negative side, cornerDepthBias :621ff), clamped to the
//     window idpp->depthOffset[0..1] (SelectProjectedWheel :568, LinkPrimitive
//     :698). A wheel in the secondary range while idpp->instFlags & 0x4000 is
//     set ends the set (EmitProjectedWheel :728).
//
// WHAT DrawTires_Reflection DOES DIFFERENTLY (:1201ff). The same sprite, but
// the wheel middle and the axle are reflected about the instance's water plane
// y = inst->vertSplit (world units, the field StagePlayer copies into
// scratch->vertSplit; splitCameraY = (vertSplit - pb->pos.y) << 2): y' = 2 L - y
// for the middle, y -> -y for the axle. The pass only runs for instFlags &
// 0x4000, always links into otRangeSecondary, and draws a wheel only when the
// ORIGINAL middle is at or below the plane (splitDelta >= 0, EmitProjectedWheel
// :1343). The corner assignment is the same table with P0<->P1 and P2<->P3
// swapped (the reflected projection stores the corners in that order). One
// retail oddity is not repeated: BuildWheelLocalPairs of the reflection pass
// takes wheelX from (instFlags & PUSHBUFFER_EXISTS) instead of inst->scale.x
// (:978, a decompilation artefact: 9 units instead of 36), the engine mirrors
// the solid wheel positions.
// WATER (split) views: the sprite is drawn whole, not clipped by the plane,
// exactly as retail (the quads have no clip either).
//
// THE ENGINE'S WAY. Nothing here replays the GTE: the middle and the axle are
// computed in float world space from the state above, the camera is the
// engine's (Engine_FrameCamera: view = R * world / 4096 + t, camera position
// = pb->pos, which is also what the retail code subtracts), the quad is world
// geometry drawn through Engine_CameraScreenFromWorld, depth-tested, with the
// icon as VRAM material (STP texels blend when the code says semi-transparent).
// Only two retail DECISIONS are kept: the sprite choice (the game's table,
// called through DrawTiresSolid_SelectSpriteIndex) and the OT cell, which is the
// phase-1 transition channel. The angle uses the kart's real axle (column 0 of
// inst->matrix); retail dots the viewing normal with the first ROW of the
// scaled m3x3 (idpp->m3x3, LLM) - the same value at heading 0, a transposed
// and header-scaled quantity otherwise - which only changes which sprite and
// which mirror is picked, not where the wheel is.

#define ENG_TIRES_WHEELS 4
#define ENG_TIRES_MATERIALS 16

typedef struct
{
	EngineMaterial wanted;
	int index;
	int used;
} EngineTiresMaterial;

global_variable EngineTiresMaterial s_engTiresMat[ENG_TIRES_MATERIALS];

// Which of P0..P3 becomes v0..v3 (top-left, top-right, bottom-left,
// bottom-right of the icon) per jump index: ApplyCornerOrder, DrawTires.c:621
// (the projectedSxy[] indices of its cases). Cases 2, 3 equal 0, 1; 6, 7 equal
// 4, 5.
global_variable const u8 s_engTiresOrder[8][4] = {
    {3, 1, 2, 0}, {2, 0, 3, 1}, {3, 1, 2, 0}, {2, 0, 3, 1}, {1, 3, 0, 2}, {0, 2, 1, 3}, {1, 3, 0, 2}, {0, 2, 1, 3},
};

typedef struct
{
	EngineVertex v[4];
	u32 *ot;
	int cell;
	float depth;
	int material;
	int semi;
} EngineTiresWheel;

internal EngineVec3 EngineTires_V3(float x, float y, float z)
{
	EngineVec3 v;

	v.x = x;
	v.y = y;
	v.z = z;
	return v;
}

internal EngineVec3 EngineTires_Add(EngineVec3 a, EngineVec3 b)
{
	return EngineTires_V3(a.x + b.x, a.y + b.y, a.z + b.z);
}

internal EngineVec3 EngineTires_Sub(EngineVec3 a, EngineVec3 b)
{
	return EngineTires_V3(a.x - b.x, a.y - b.y, a.z - b.z);
}

internal EngineVec3 EngineTires_Scale(EngineVec3 a, float s)
{
	return EngineTires_V3(a.x * s, a.y * s, a.z * s);
}

internal float EngineTires_Dot(EngineVec3 a, EngineVec3 b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

internal EngineVec3 EngineTires_Cross(EngineVec3 a, EngineVec3 b)
{
	return EngineTires_V3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

// 0 when the vector is too short to have a direction.
internal int EngineTires_Normalise(EngineVec3 *v)
{
	const float len = sqrtf(EngineTires_Dot(*v, *v));

	if (!(len > 1e-6f))
	{
		return 0;
	}
	*v = EngineTires_Scale(*v, 1.0f / len);
	return 1;
}

// The material of (tpage, clut, semi), cached, as the exhaust and shadow
// effects do it: before every use the table entry is checked (the table is
// reset at a level change) and registered again when it differs. Semi: the
// blend of the tpage's abr, only STP texels blend. Not semi: opaque (texel
// 0x0000 is still transparent in the VRAM path).
internal int EngineFx_TiresMaterial(u16 tpage, u16 clut, int semi)
{
	EngineMaterial want;
	int slot;
	int freeSlot = -1;
	int index;

	memset(&want, 0, sizeof(want));
	want.texSource = (u8)ENGINE_TEX_VRAM;
	want.flags = (u16)ENGINE_MATERIAL_DOUBLE_SIDED;
	if (semi)
	{
		want.blend = (u8)Engine_BlendFromAbr((int)((tpage >> 5) & 3u));
		want.flags |= (u16)ENGINE_MATERIAL_SEMI_STP;
		want.tpage = tpage;
	}
	else
	{
		want.blend = (u8)ENGINE_BLEND_OPAQUE;
		want.tpage = (u16)((tpage & ~0x60u) | (3u << 5));
	}
	want.clut = clut;
	want.tint[0] = want.tint[1] = want.tint[2] = want.tint[3] = 1.0f;

	for (slot = 0; slot < ENG_TIRES_MATERIALS; slot++)
	{
		EngineTiresMaterial *c = &s_engTiresMat[slot];
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
		freeSlot = 0; // cache full: replace the first (a handful of keys exist)
	}
	index = Engine_MaterialRegister(&want);
	if (index < 0)
	{
		s_engTiresMat[freeSlot].used = 0;
		return -1;
	}
	s_engTiresMat[freeSlot].wanted = want;
	s_engTiresMat[freeSlot].index = index;
	s_engTiresMat[freeSlot].used = 1;
	return index;
}

// The 3x3 rotation of the instance as floats: r[row][col], model to world.
internal void EngineTires_Rotation(const struct Instance *inst, float r[3][3])
{
	int row;
	int col;

	for (row = 0; row < 3; row++)
	{
		for (col = 0; col < 3; col++)
		{
			r[row][col] = (float)inst->matrix.m[row][col] / 4096.0f;
		}
	}
}

internal EngineVec3 EngineTires_Rotate(const float r[3][3], float x, float y, float z)
{
	return EngineTires_V3(r[0][0] * x + r[0][1] * y + r[0][2] * z, r[1][0] * x + r[1][1] * y + r[1][2] * z, r[2][0] * x + r[2][1] * y + r[2][2] * z);
}

// Called from DrawTiresSolid_StagePlayer (reflection 0) and
// DrawTiresReflection_StagePlayer (reflection 1) for one driver and one view,
// after every suppression check, at the point where retail would start writing
// the driver's tire primitives. colourWord = scratch->tireColor.
//   0   not an owned driver / engine off: the caller does what it always did
//   1   the engine drew the wheels (or retail would have drawn none): the caller
//       writes NO tire primitive for this driver and view
//  -1   owned, but the engine could not draw: the caller lets retail draw, the
//       wheels are counted as retail (Engine_FxNoteRetail, four per set)
// Phase 1 computes all four wheels (so a failure is known before anything is
// submitted), phase 2 submits them. A submit that fails after another wheel was
// submitted leaves that wheel out (rare: draw list or marker arena full).
int EngineFx_Tires(const struct Driver *d, const struct Instance *inst, const struct InstDrawPerPlayer *idpp, const struct PushBuffer *pb, int reflection,
                   u32 colourWord)
{
	EngineTiresWheel wheel[ENG_TIRES_WHEELS];
	const EngineCamera *cam;
	EngineMat4 screenFromWorld;
	float rot[3][3];
	EngineVec3 axle;
	EngineVec3 camPos;
	struct Icon **sprites;
	int view;
	int count = 0;
	int bias[2] = {0, 0}; // cells; cornerDepthBias of the scratch, bytes / 4
	int wheelX;
	int wheelY;
	int wheelFrontZ;
	int wheelRearZ;
	int steerSin;
	int steerCos;
	int hazardAngle;
	int hazardShift = 9;
	int hazardRim[4][2]; // per wheel: additions to rim.y and rim.z
	float halfSize;
	float waterLevel;
	int code;
	int semi;
	int raw;
	u8 colour[4];
	int w;
	int i;
	int submitted = 0;

	if (!g_cfg_engine || !Engine_FxOwnsDriver(d))
	{
		return 0;
	}
	// The hi-res driver's seat (engine_hires.c): wheel meshes, never sprites.
	{
		const int hires = Engine_HiresWheels(d, inst, idpp, pb, reflection);

		if (hires != 0)
		{
			return hires;
		}
	}

	// From here on the driver is owned: whatever cannot be drawn is drawn by the
	// retail path and counted as such.
	view = Engine_InstanceViewOf(pb);
	cam = (view >= 0) ? Engine_FrameCamera(view) : NULL;
	sprites = d->wheelSprites;
	halfSize = (float)(s16)d->wheelSize / 256.0f;
	if ((inst == NULL) || (idpp == NULL) || (pb == NULL) || (cam == NULL) || !cam->valid || (sprites == NULL) || (halfSize == 0.0f))
	{
		goto retail;
	}

	// ---- the game state ---------------------------------------------------
	// Wheel middles in the four-times model units (BuildWheelLocalPairs).
	wheelX = (inst->scale.x * 0x90) >> 12;
	wheelY = (inst->scale.y * 0x40) >> 12;
	wheelFrontZ = (inst->scale.z * 0xc7) >> 12;
	wheelRearZ = (inst->scale.z * -0x60) >> 12;
	{
		const struct TrigPair steer = DrawTiresSolid_TrigAngleSinCos(d->wheelRotation << 2);

		steerSin = steer.sin;
		steerCos = steer.cos;
	}
	hazardAngle = d->hazardTimer << 5;
	if ((d->hazardTimer & 1) != 0)
	{
		hazardShift = 6;
		hazardAngle <<= 1;
	}
	// The wobble phases of the wheels 0, 1, 2, 3 (retail AddHazardOffset order
	// is wheels 0, 2, 1, 3 with +0, +0x400, +0x800, +0xc00).
	{
		static const int phase[4] = {0x0, 0x800, 0x400, 0xc00};

		for (w = 0; w < 4; w++)
		{
			const struct TrigPair spin = DrawTiresSolid_TrigAngleSinCos(hazardAngle + phase[w]);

			hazardRim[w][0] = spin.cos >> hazardShift;
			hazardRim[w][1] = spin.sin >> hazardShift;
		}
	}

	EngineTires_Rotation(inst, rot);
	axle = EngineTires_V3(rot[0][0], rot[1][0], rot[2][0]); // the kart's +x axis in the world
	if (!EngineTires_Normalise(&axle))
	{
		goto retail;
	}
	camPos = EngineTires_V3((float)pb->pos.x, (float)pb->pos.y, (float)pb->pos.z);
	waterLevel = (float)inst->vertSplit;

	code = (int)(colourWord >> 24);
	semi = (code & 0x02) != 0;
	raw = (code & 0x01) != 0;
	colour[0] = (u8)(raw ? 128 : (int)(colourWord & 0xffu));
	colour[1] = (u8)(raw ? 128 : (int)((colourWord >> 8) & 0xffu));
	colour[2] = (u8)(raw ? 128 : (int)((colourWord >> 16) & 0xffu));
	colour[3] = 255;

	// ---- phase 1: the four wheels, in the retail order 3, 2, 1, 0 --------
	for (w = 3; w >= 0; w--)
	{
		const int right = (w & 1) == 0; // wheels 0 and 2 are on +x
		const int front = w < 2;
		const int middleX = right ? wheelX : -wheelX;
		const int middleZ = front ? wheelFrontZ : wheelRearZ;
		int rimX;
		int rimZ;
		EngineVec3 centre;
		EngineVec3 originalCentre;
		EngineVec3 rim;
		EngineVec3 spriteAxle;
		EngineVec3 n;
		EngineVec3 a;
		EngineVec3 b;
		EngineVec3 p[4];
		const struct Icon *icon;
		EngineTiresWheel *out = &wheel[count];
		float depthSum = 0.0f;
		int angle;
		int jump;
		int cell;
		int sumSz = 0;
		int abortSet = 0;
		u32 *ot;

		// The axle point ("rim"): steered for the front, 4096 units along x for the rear.
		if (front)
		{
			rimX = middleX - (right ? steerCos : -steerCos);
			rimZ = middleZ + (right ? steerSin : -steerSin);
		}
		else
		{
			rimX = middleX - (right ? 0x1000 : -0x1000);
			rimZ = middleZ;
		}
		rimZ += hazardRim[w][1];

		// World middle: t + R * middle / 4; the axle direction R * rim.
		centre = EngineTires_Rotate(rot, (float)middleX * 0.25f, (float)wheelY * 0.25f, (float)middleZ * 0.25f);
		centre = EngineTires_Add(centre, EngineTires_V3((float)inst->matrix.t[0], (float)inst->matrix.t[1], (float)inst->matrix.t[2]));
		rim = EngineTires_Rotate(rot, (float)rimX, (float)hazardRim[w][0], (float)rimZ);
		spriteAxle = axle;
		originalCentre = centre;
		if (reflection)
		{
			centre.y = 2.0f * waterLevel - centre.y;
			rim.y = -rim.y;
			spriteAxle.y = -spriteAxle.y;
		}

		// The viewing normal (toward the camera), the two axes of the quad.
		n = EngineTires_Sub(camPos, centre);
		if (!EngineTires_Normalise(&n))
		{
			continue;
		}
		a = EngineTires_Cross(n, rim);
		if (!EngineTires_Normalise(&a))
		{
			continue;
		}
		b = EngineTires_Cross(a, n);
		a = EngineTires_Scale(a, halfSize);
		b = EngineTires_Scale(b, halfSize);
		p[0] = EngineTires_Sub(EngineTires_Sub(centre, a), b);
		p[1] = EngineTires_Sub(EngineTires_Add(centre, a), b);
		p[2] = EngineTires_Add(EngineTires_Sub(centre, a), b);
		p[3] = EngineTires_Add(EngineTires_Add(centre, a), b);

		// The icon: the viewing angle against the axle, mirrored by its sign.
		angle = (int)(EngineTires_Dot(spriteAxle, n) * 4096.0f);
		jump = w + ((angle < 0) ? 4 : 0);
		icon = sprites[DrawTiresSolid_SelectSpriteIndex(angle)];
		if (icon == NULL)
		{
			continue; // retail leaves such a wheel out before the corner order
		}

		// The OT range and cell.
		for (i = 0; i < 4; i++)
		{
			const float vz = Engine_Mat4MulPoint(&cam->viewFromWorld, p[i]).z;
			int sz = (int)(vz * 4.0f);

			sz = (sz < 0) ? 0 : ((sz > 0xffff) ? 0xffff : sz);
			sumSz += sz;
			depthSum += vz;
		}
		cell = (int)(((u32)sumSz * 0x400u) >> 17);
		if (reflection)
		{
			ot = (u32 *)(size_t)(u32)idpp->otRangeSecondary;
		}
		else
		{
			ot = (u32 *)(size_t)(u32)((waterLevel >= originalCentre.y) ? idpp->otRangeSecondary : idpp->otRangeNormal);
			abortSet = ((u32)idpp->otRangeSecondary == (u32)(size_t)ot) && ((idpp->instFlags & 0x4000) != 0);
		}
		if (ot == NULL)
		{
			goto retail;
		}

		// The front-wheel bias belongs to the corner order step: it is updated
		// before the skips below, like the retail ApplyCornerOrder.
		switch (jump)
		{
		case 0:
		case 4:
			cell -= bias[0];
			break;
		case 1:
		case 5:
			cell -= bias[1];
			break;
		case 2:
			bias[0] = 0;
			break;
		case 3:
			bias[1] = 0;
			break;
		case 6:
			cell -= 2;
			bias[0] = 2;
			break;
		default: // 7
			cell -= 2;
			bias[1] = 2;
			break;
		}
		if (abortSet)
		{
			break; // retail: the emit returns 0 and the whole set ends
		}
		if (reflection && (originalCentre.y < waterLevel))
		{
			continue; // the original wheel is above the water: no mirror image
		}
		if (cell < (int)idpp->depthOffset[0])
		{
			cell = (int)idpp->depthOffset[0];
		}
		if (cell > (int)idpp->depthOffset[1])
		{
			cell = (int)idpp->depthOffset[1];
		}
		// The depth window does not always hold the cell inside the table (seen
		// in reflection views on Polar Pass: 3 refused submits, one set drawn by
		// retail): then the instance's own bin, the one the body and the other
		// engine draws of this instance use (NativeRenderLayer_MarkerBin). The
		// cell is only the phase-1 transition channel; the engine sorts by depth.
		if ((cell < 0) || (cell > 0x3ff))
		{
			cell = NativeRenderLayer_MarkerBin(idpp, 0);
		}
		out->material = EngineFx_TiresMaterial(icon->texLayout.tpage, icon->texLayout.clut, semi);
		if (out->material < 0)
		{
			goto retail;
		}

		{
			const u8 *order = s_engTiresOrder[jump];
			const float us[4] = {(float)icon->texLayout.u0, (float)icon->texLayout.u1, (float)icon->texLayout.u2, (float)icon->texLayout.u3};
			const float vs[4] = {(float)icon->texLayout.v0, (float)icon->texLayout.v1, (float)icon->texLayout.v2, (float)icon->texLayout.v3};

			for (i = 0; i < 4; i++)
			{
				// The reflected projection stores P0/P1 and P2/P3 swapped.
				const EngineVec3 q = p[reflection ? (order[i] ^ 1) : order[i]];

				out->v[i].position[0] = q.x;
				out->v[i].position[1] = q.y;
				out->v[i].position[2] = q.z;
				out->v[i].texcoord[0] = us[i];
				out->v[i].texcoord[1] = vs[i];
				memcpy(out->v[i].color, colour, 4);
			}
			Engine_VertexBoxOfCorners(out->v, 4); // the icon's texels: their box (engine.h, THE UV BOX)
		}
		out->ot = ot;
		out->cell = cell;
		out->depth = depthSum * 0.25f; // the middle's view z, world units
		out->semi = semi;
		count++;
	}

	// ---- phase 2: submit --------------------------------------------------
	Engine_CameraScreenFromWorld(cam, &screenFromWorld);
	for (i = 0; i < count; i++)
	{
		static const u16 idx[6] = {0, 1, 2, 1, 3, 2}; // FT4: (v0 v1 v2) (v1 v3 v2)
		EngineDraw draw;

		memset(&draw, 0, sizeof(draw));
		draw.mesh = -1;
		draw.materialOverride = -1;
		draw.clipFromModel = screenFromWorld;
		draw.tint[0] = draw.tint[1] = draw.tint[2] = draw.tint[3] = 1.0f;
		draw.sortDepth = wheel[i].depth;
		draw.otCell = (u32)wheel[i].cell;
		draw.flags = (u16)(wheel[i].semi ? ENGINE_DRAW_TRANSPARENT : 0u);
		draw.dynVertices = wheel[i].v;
		draw.dynVertexCount = 4;
		draw.dynIndices = idx;
		draw.dynIndexCount = 6;
		draw.dynMaterial = wheel[i].material;
		if (Engine_RenderSubmitLinked(view, &draw, wheel[i].ot, wheel[i].cell))
		{
			submitted++;
			Engine_FxNoteDrawn(ENGINE_FX_TIRES);
			// The engine oracle (armed frames only): E lines of the effect, no retail side (the producer writes none for an owned driver).
			Engine_OracleEngineDraw('X', "tires", view, &draw, NULL);
		}
		else if (submitted == 0)
		{
			// Nothing of the set is in the scene yet: retail can still draw all of it.
			goto retail;
		}
	}
	return 1;

retail:
	for (w = 0; w < ENG_TIRES_WHEELS; w++)
	{
		Engine_FxNoteRetail(ENGINE_FX_TIRES);
	}
	return -1;
}
