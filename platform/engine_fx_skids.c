#include <platform/engine.h>

// ENGINE FX SKIDS - the skid marks of custom drivers drawn by the engine (step 4).
// See include/platform/engine.h, EFFECTS OF CUSTOM DRIVERS.
//
// DRAW TIME ONLY. The history of skid points (VehEmitter.c: d->skidmarks,
// d->skidmarkEnableFlags, d->skidmarkFrameIndex) is written exactly as before.
// game/Vehicle/VehGroundSkids.c calls EngineFx_Skids instead of its own loop
// for a driver the engine owns. This function walks the history in the SAME
// order as VehGroundSkids_Main (same frames, same tire bits, same culling,
// same colour/fade, same OT depth rule) and, instead of POLY_GT4 primitives,
// builds one textured quad per segment in world units:
//
//   retail POLY_GT4 (VehGroundSkids_Subset1)      engine quad
//   v0 = current  frame, edge point p             curr[p]     colour near
//   v1 = current  frame, edge point p + 1         curr[p + 1] colour near
//   v2 = previous frame, edge point p             prev[p]     colour far
//   v3 = previous frame, edge point p + 1         prev[p + 1] colour far
//   uv0..uv3 = the four corners of icon 0x2f      the same corners (texels of the page)
//   semi-transparent, abr 3 (B + F/4) if the      material tpage/clut of the icon with the
//   segment's flag bit 0 is set, else abr 2       same abr, STP rule on, VRAM texture
//   (B - F)
//
// Every quad has its own UV (the whole icon), so quads share no vertices.
// Draws: the quads of one tire with the same abr are collected and drawn as ONE
// engine draw (at most 4 tires x 2 abr per driver and view, instead of one per
// segment, which would eat the 512 draws per view). The retail code links every
// segment into its own OT cell, cell = ((viewZ >> 2) + (color << 6)) >> 6 with
// the ground-skid colour bias in `color`; the run's draw is linked into the
// cell of its MIDDLE quad (the segments of one run lie a few hundred units
// apart, so their cells differ by only a few and the order against cars and
// track stays within that). sortDepth = the view z of that quad's current edge.

#define SKID_TIRES 4
#define SKID_MAX_QUADS 8 // a history walk has at most 8 frames, so at most 7 segments per tire

// Retail constants of VehGroundSkids.c, repeated here (the engine file does not
// reach into its private enum).
#define SKID_ICON_TIREMARK 0x2f
#define SKID_CULL_ABS_MAX 0x1771
#define SKID_MIN_VISIBLE_DEPTH 0x20
#define SKID_FULL_INTENSITY 0x7f
#define SKID_FULL_INTENSITY_DEPTH 0x180
#define SKID_MIN_INTENSITY 0x10

typedef struct
{
	EngineVertex v[SKID_MAX_QUADS * 4];
	u16 idx[SKID_MAX_QUADS * 6];
	int cell[SKID_MAX_QUADS];
	float depth[SKID_MAX_QUADS];
	int quads;
} EngineSkidRun;

typedef struct
{
	int used;
	int index;
} EngineSkidMaterial;

// One cached material index per abr (2 and 3); the table is reset at every
// level change, so the entry is checked against the table before use.
global_variable EngineSkidMaterial s_skidMaterial[2];

// Retail uses the GTE: point relative to the camera, times 4 (u16 wrap, s16
// result), rotated by matrix_ViewProj with a zero translation, then >> 12.
static s32 EngineSkid_Relative(s16 value, s32 origin)
{
	return (s32)(s16)(u16)(((u32)(u16)value - (u32)(u16)origin) << 2);
}

// SZ of the GTE for one point (VehGroundSkids_ProjectFrame -> stsz3): 4 x view
// z, clamped 0..0xffff.
static s32 EngineSkid_Sz(const MATRIX *r, const s32 *origin, const SVECTOR *p)
{
	const s32 x = EngineSkid_Relative(p->vx, origin[0]);
	const s32 y = EngineSkid_Relative(p->vy, origin[1]);
	const s32 z = EngineSkid_Relative(p->vz, origin[2]);
	s32 sz = ((s32)r->m[2][0] * x + (s32)r->m[2][1] * y + (s32)r->m[2][2] * z) >> 12;

	if (sz < 0)
	{
		sz = 0;
	}
	if (sz > 0xffff)
	{
		sz = 0xffff;
	}
	return sz;
}

// VehGroundSkids_InitPoint + rtv0: the newest frame's first point must lie
// within the cull box; *outDepth is MAC3 (4 x view z, signed, NOT clamped).
static int EngineSkid_FirstPoint(const MATRIX *r, const s32 *origin, const SVECTOR *p, s32 *outDepth)
{
	s32 c[3];
	const s32 in[3] = {p->vx, p->vy, p->vz};
	int i;

	for (i = 0; i < 3; i++)
	{
		c[i] = (s32)(((u32)in[i] - (u32)origin[i]) << 2);
		if (((c[i] < 0) ? -c[i] : c[i]) >= SKID_CULL_ABS_MAX)
		{
			return 0;
		}
	}
	*outDepth = ((s32)r->m[2][0] * c[0] + (s32)r->m[2][1] * c[1] + (s32)r->m[2][2] * c[2]) >> 12;
	return 1;
}

// VehGroundSkids_IntensityFromDepth: -1 = too far, draw nothing.
static int EngineSkid_Intensity(s32 depth)
{
	u32 n;
	int lz = 32;
	int shift;
	int intensity;

	depth >>= 2;
	if (depth < SKID_FULL_INTENSITY_DEPTH)
	{
		return SKID_FULL_INTENSITY;
	}
	n = (u32)(depth - SKID_FULL_INTENSITY_DEPTH);
	if (n != 0u)
	{
		lz = 0;
		while ((n & 0x80000000u) == 0u)
		{
			n <<= 1;
			lz++;
		}
	}
	shift = 0x1a - lz;
	if (shift < 0)
	{
		shift = 0;
	}
	intensity = SKID_FULL_INTENSITY >> (shift & 0x1f);
	return (intensity < SKID_MIN_INTENSITY) ? -1 : intensity;
}

// The material of the tire mark for one abr (2 = B - F, 3 = B + F/4). -1 on failure.
static int EngineSkid_Material(const struct TextureLayout *icon, int abr)
{
	EngineSkidMaterial *slot = &s_skidMaterial[abr - 2];
	EngineMaterial m;
	const EngineMaterial *cur;

	memset(&m, 0, sizeof(m));
	m.texSource = ENGINE_TEX_VRAM;
	m.blend = (u8)Engine_BlendFromAbr(abr);
	m.flags = ENGINE_MATERIAL_DOUBLE_SIDED | ENGINE_MATERIAL_SEMI_STP;
	m.tpage = (u16)((icon->tpage & ~0x60u) | ((u32)abr << 5));
	m.clut = icon->clut;
	m.tint[0] = m.tint[1] = m.tint[2] = m.tint[3] = 1.0f;
	m.depthBias = ENGINE_GROUND_DEPTH_BIAS; // on the ground: in front of an engine track

	if (slot->used)
	{
		cur = Engine_Material(slot->index);
		if ((cur != NULL) && (memcmp(cur, &m, sizeof(m)) == 0))
		{
			return slot->index;
		}
	}
	slot->index = Engine_MaterialRegister(&m);
	slot->used = (slot->index >= 0);
	return slot->index;
}

static EngineVec3 EngineSkid_World(const SVECTOR *p)
{
	EngineVec3 w;

	w.x = (float)p->vx;
	w.y = (float)p->vy;
	w.z = (float)p->vz;
	return w;
}

static void EngineSkid_AddQuad(EngineSkidRun *run, const struct TextureLayout *icon, const EngineVec3 pos[4], int nearC, int farC, int cell, float depth)
{
	const u8 uv[4][2] = {{icon->u0, icon->v0}, {icon->u1, icon->v1}, {icon->u2, icon->v2}, {icon->u3, icon->v3}};
	const int col[4] = {nearC, nearC, farC, farC};
	EngineVertex *v = &run->v[run->quads * 4];
	u16 *i = &run->idx[run->quads * 6];
	const u16 b = (u16)(run->quads * 4);
	int k;

	for (k = 0; k < 4; k++)
	{
		v[k].position[0] = pos[k].x;
		v[k].position[1] = pos[k].y + ENGINE_GROUND_LIFT; // collision height -> drawn surface
		v[k].position[2] = pos[k].z;
		v[k].texcoord[0] = (float)uv[k][0];
		v[k].texcoord[1] = (float)uv[k][1];
		v[k].color[0] = v[k].color[1] = v[k].color[2] = (u8)col[k]; // PS1 colour byte, 0x80 = unchanged
		v[k].color[3] = 255;
	}
	// GT4 vertex order 0,1,2,3 is a Z: triangles (0,1,2) and (1,3,2)
	i[0] = b;
	i[1] = (u16)(b + 1);
	i[2] = (u16)(b + 2);
	i[3] = (u16)(b + 1);
	i[4] = (u16)(b + 3);
	i[5] = (u16)(b + 2);
	run->cell[run->quads] = cell;
	run->depth[run->quads] = depth;
	run->quads++;
}

// Called by VehGroundSkids_Main for a driver the engine owns, once per view
// (pb = that view's push buffer).
void EngineFx_Skids(const struct Driver *d, const struct PushBuffer *pb)
{
	struct GameTracker *gGT = sdata->gGT;
	const struct Icon *iconObj;
	const struct TextureLayout *icon;
	const EngineCamera *cam;
	EngineMat4 clip;
	EngineSkidRun runs[SKID_TIRES][2];
	int tire;
	int a;
	int view = -1;
	int i;
	int drewAny = 0;
	u32 flags;
	int frameIndex;
	s32 origin[3];
	s32 firstDepth;
	int intensity;
	int nearC;
	int farC;
	u32 prevFlags;

	if ((d == NULL) || (pb == NULL) || (gGT == NULL))
	{
		return;
	}
	flags = d->skidmarkEnableFlags;
	if (flags <= DRIVER_SKIDMARK_CURRENT_FRAME_MASK)
	{
		return;
	}
	for (i = 0; i < ENGINE_VIEWS; i++)
	{
		if (pb == &gGT->pushBuffer[i])
		{
			view = i;
		}
	}
	cam = (view >= 0) ? Engine_FrameCamera(view) : NULL;
	iconObj = gGT->ptrIcons[SKID_ICON_TIREMARK];
	if ((cam == NULL) || (!cam->valid) || (iconObj == NULL))
	{
		return;
	}
	icon = &iconObj->texLayout;

	origin[0] = pb->matrix_Camera.t[0];
	origin[1] = pb->matrix_Camera.t[1];
	origin[2] = pb->matrix_Camera.t[2];

	frameIndex = ((u8)d->skidmarkFrameIndex - 1) & DRIVER_SKIDMARK_FRAME_INDEX_MASK;
	if (!EngineSkid_FirstPoint(&pb->matrix_ViewProj, origin, &d->skidmarks[frameIndex][0].edge[0], &firstDepth))
	{
		return;
	}
	intensity = EngineSkid_Intensity(firstDepth);
	if (intensity < 0)
	{
		return;
	}

	for (tire = 0; tire < SKID_TIRES; tire++)
	{
		runs[tire][0].quads = 0;
		runs[tire][1].quads = 0;
	}

	nearC = intensity;
	farC = -1; // retail: sentinel in the first pass
	prevFlags = 0;
	while (flags != 0u)
	{
		const u32 currFlags = flags;

		if ((currFlags & DRIVER_SKIDMARK_CURRENT_FRAME_MASK) != 0u)
		{
			const union VehEmitterSkidmark *frame = d->skidmarks[frameIndex];
			// The walk's previous frame is the ring slot before this one (the
			// index only ever counts up by one per pass).
			const union VehEmitterSkidmark *prevFrame = d->skidmarks[(frameIndex - 1) & DRIVER_SKIDMARK_FRAME_INDEX_MASK];

			for (tire = 0; tire < SKID_TIRES; tire++)
			{
				const u32 bit = 1u << tire; // BACK_LEFT, BACK_RIGHT, FRONT_LEFT, FRONT_RIGHT
				s32 cs[2];
				s32 ps[2];

				if ((currFlags & prevFlags & bit) == 0u)
				{
					continue;
				}
				for (i = 0; i < 2; i++)
				{
					cs[i] = EngineSkid_Sz(&pb->matrix_ViewProj, origin, &frame[tire].edge[i]);
					ps[i] = EngineSkid_Sz(&pb->matrix_ViewProj, origin, &prevFrame[tire].edge[i]);
				}
				if ((cs[0] <= SKID_MIN_VISIBLE_DEPTH) || (cs[1] <= SKID_MIN_VISIBLE_DEPTH) || (ps[0] <= SKID_MIN_VISIBLE_DEPTH) ||
				    (ps[1] <= SKID_MIN_VISIBLE_DEPTH))
				{
					continue;
				}
				{
					const int abr = ((frame[tire].fields.flags & 1u) != 0u) ? 3 : 2;
					const int depth = (cs[0] >> 2) + ((int)frame[tire].fields.color << 6);
					int cell = depth >> 6;
					EngineSkidRun *run = &runs[tire][abr - 2];

					if (cell < 0)
					{
						cell = 0;
					}
					if (cell > 0x3ff)
					{
						cell = 0x3ff;
					}
					if (run->quads < SKID_MAX_QUADS)
					{
						EngineVec3 pos[4];

						pos[0] = EngineSkid_World(&frame[tire].edge[0]);
						pos[1] = EngineSkid_World(&frame[tire].edge[1]);
						pos[2] = EngineSkid_World(&prevFrame[tire].edge[0]);
						pos[3] = EngineSkid_World(&prevFrame[tire].edge[1]);
						EngineSkid_AddQuad(run, icon, pos, nearC & 0xff, farC & 0xff, cell, (float)(cs[0] >> 2));
					}
				}
			}
		}

		frameIndex = (frameIndex + 1) & DRIVER_SKIDMARK_FRAME_INDEX_MASK;

		if (farC == -1)
		{
			farC = nearC;
			prevFlags = DRIVER_SKIDMARK_CURRENT_FRAME_MASK;
		}
		else
		{
			prevFlags = currFlags;
			flags = currFlags >> DRIVER_SKIDMARK_HISTORY_SHIFT;
			farC = nearC;
			nearC = (nearC & 0xff) >> 1;
		}
		// retail: `if (colorFar == 0) break;` compares the whole colour WORD, which
		// always carries the 0x3e prefix, so it never breaks; neither do we.
	}

	Engine_CameraScreenFromWorld(cam, &clip);
	for (tire = 0; tire < SKID_TIRES; tire++)
	{
		for (a = 0; a < 2; a++)
		{
			EngineSkidRun *run = &runs[tire][a];
			EngineDraw draw;
			int mid;
			int material;

			if (run->quads == 0)
			{
				continue;
			}
			material = EngineSkid_Material(icon, a + 2);
			if (material < 0)
			{
				continue;
			}
			mid = run->quads / 2;
			memset(&draw, 0, sizeof(draw));
			draw.mesh = -1;
			draw.clipFromModel = clip;
			draw.tint[0] = draw.tint[1] = draw.tint[2] = draw.tint[3] = 1.0f;
			draw.materialOverride = -1;
			draw.sortDepth = run->depth[mid];
			draw.otCell = (u32)run->cell[mid];
			draw.flags = ENGINE_DRAW_TRANSPARENT;
			draw.dynVertices = run->v;
			draw.dynVertexCount = (u32)(run->quads * 4);
			draw.dynIndices = run->idx;
			draw.dynIndexCount = (u32)(run->quads * 6);
			draw.dynMaterial = material;
			if (Engine_RenderSubmitLinked(view, &draw, pb->ptrOT, run->cell[mid]))
			{
				drewAny = 1;
			}
		}
	}
	if (drewAny)
	{
		Engine_FxNoteDrawn(ENGINE_FX_SKIDS);
	}
}

// For the call site in VehGroundSkids.c, which is compiled before engine.h.
int EngineFx_SkidsOwns(const struct Driver *d)
{
	return Engine_FxOwnsDriver(d);
}
