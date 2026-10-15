#include <platform/engine.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ENGINE INSTANCE LIGHT - the colour and blend retail's primitive writer gives
// an instance's triangles, shown by the engine's instance draw (round 18,
// classes K-WEG-LIT and K-WEG-SPLIT). See include/platform/engine.h, THE
// RETAIL WRITER OF AN INSTANCE.
//
// ONE RULE, BY WHAT RETAIL CHOSE. RenderBucket_WriteInstanceCallbackLabels
// (game/RenderBucket/RenderBucket_QueueExecute.c:899-914) writes, for every
// instance it queues, the labels its draw then runs:
//   funcPtr[1] the primitive writer, (flags >> 16) & 7 (table :823-825):
//              0 normal, 1 depth fade, 2 key/relic/token, 3 lit texture,
//              4 clamp depth, 5 select range, 6 ghost.
//   funcPtr[2] the water/mirror side selector, (flags >> 20) & 3 (:828-833):
//              0 both+mask, 1 negative, 2 dim xor, 3 xor.
// The engine reads those labels when it draws the instance (the dispatch runs
// after the queue). Nothing here asks for a model, a level or a track.
//
// THE WRITERS.
//   normal, clamp depth, select range: the vertex colours of the colour table
//     (RenderBucket_DrawInstPrim_NormalAtOTEntry :3108-3156); clamp and range
//     only pick another OT cell. The import's vertex colours are that - nothing
//     to do here.
//   ghost and the writers with bit 18 (clamp, range, ghost) carry
//     DRAW_BILLBOARD (0x40000) and never reach the engine
//     (EngTrack_RouteInstance declines them before).
//   depth fade (DRAW_TRANSPARENT alone; no setter in the game code): not
//     followed, retail draws the instance (Engine_InstLightWriterFollowed 0).
//   key/relic/token (:3164-3268) and lit texture (:3381-3506): the colour
//     table holds NORMALS. Per triangle retail lights the normal of its FIRST
//     corner (tempColor[1], the oldest colour of the strip FIFO = corner 0 of
//     the twin triangle, which the import keeps first in both corner orders,
//     native_twin.c:1004-1031) and writes one FLAT textured triangle (FT3):
//       n        = the three bytes as signed, times 32 (IR1..IR3, :3426-3428)
//       IR1      = row 1 of the GTE colour matrix . n / 4096 (gte_lcir, sf 1)
//       IR3      = row 3 . n / 4096
//                  row 1 = (specLightX (s8), its sign (-1 or 0),
//                           (s16) reflectionRGBA) - CTC2 16/17 of the light
//                           setup (:5383-5385): the instance's light direction
//                           in model units (Vector_SpecLight*, game/Vector.c)
//                  row 3 = idpp->halfVector (CTC2 19/20, :5386-5387)
//       b        = ((IR1 + 4096) * 7 + 8192) / 16, halved on the back side
//       colour   = far colour * b / 4096 (gte_dpcs with RGBC 0, the far
//                  colour from inst->colorRGBA: R bits 20-27, G 12-19, B 4-11,
//                  RenderBucket_SetFarColorFromInstance :5369-5377)
//       specular = sRenderBucketKeyRelicBrightness8008a2c4[|IR3 >> 3| - 384]
//                  (clamped to the table, none below 384), / 8 on the back
//       result   = colour + specular, saturated to 255.
//     THE SIDE: signedTest = NCLIP ^ instance flag bit 15 ^ command bit 29
//     (:3199, :3419); negative = back. A one-sided triangle retail draws is
//     always front (the same expression decides its cull, :2946-2961). A
//     two-sided one is front or back by the view.
//     THE BLEND (layout abr = the tpage bits of the triangle's layout):
//       lit: abr != 0 -> opaque (code 0x24, :3470-3484); abr 0 -> 0x26 with
//            abr 1 (add) on the front, abr 2 (subtract) on the back (:3485-3494).
//       key: abr 0 and front -> 0x26 with abr 1 (add); else opaque (:3253-3259).
//     An untextured triangle is not drawn by either (tex == 0 returns, :3185,
//     :3404).
//
// HOW THE ENGINE SHOWS IT. A VARIANT of the imported mesh, made once per
// import and writer (Eil_Build): every textured triangle as one-sided copies -
// a one-sided triangle once (its import order is the one the engine's cull
// keeps when retail draws it), a two-sided one twice, a front copy in the order
// the engine's cull keeps when retail calls the view's side the front (corner
// order as listed with bit 29, reversed without - the twin's rule for one-sided
// triangles, native_twin.c:999-1007) and a back copy in the other order. Each
// copy goes into a range whose material is the import's material with the
// writer's blend for its side (and one-sided). The GPU's cull then picks the
// side retail picks with NCLIP; no side is worked out on the CPU. Every frame
// the copies get their flat colour (front or back) from the instance's light
// state, as live vertices (EngineInstanceDraw.liveVertices) of the variant.
// The maths is float; only the specular index is taken in integers: it picks
// a step of a table whose neighbours differ by up to 15, and a float rounding
// at a step would cost 15/255 - the rest stays within 1/255 of retail
// (Engine_SelfTestInstLight checks every case against the GTE sequence of the
// writer, tolerance 2/255 of the material comparison).
// Not followed: instance flag bit 15 (REVERSE_CULL_DIRECTION) swapping the
// sides of a lit/key instance, and the mirror draw of one (retail colours the
// mirrored triangles as back sides). No lit/key instance with either is known.
//
// THE WATER AND MIRROR SIDES (funcPtr[2], RenderBucket_ApplyWaterSplitSideSelector
// :4429-4480). For a piece cut at the plane, below (guardDist = its splitDist
// >= 0) or above, in the mirror pass (waterSplitSide 0, :4918) or the original
// pass (-1, :4932, and the split handler :5827):
//   both+mask  drawn; below: colour >> (specLightX & 31) & reflectionRGBA
//   negative   below not drawn; above: shift and mask in the mirror pass
//   xor        drawn when (below) == (specLightX >= 0), colour unchanged
//   dim xor    drawn; dimmed (half + quarter, :4416-4422) when
//              (below) == (specLightX >= 0), and moved 3 pixels right
// The special handler's mirror pass shifts and masks always (:4806-4826).
// The engine draws (engine_instance.c): SPLIT the part above (tint) and below
// (belowTint, drawBelow); REFLECTION the part above and its mirror
// (mirrorTint); SPECIAL the model and, reflective, its mirror (mirrorTint).
// Engine_InstLightSplit sets those tints from the selector:
//   shift and mask: a tint per channel. With the mask taking no bit of the
//     next channel and each channel's mask byte either 0xff >> s or 0, that is
//     exact up to the floor (c >> s against c / 2^s, under 1/255); default
//     instances have s 1, mask 0x7f7f7f (INSTANCE.c:59/72). Other masks get the
//     nearest tint, logged once.
//   dim: 0.75 (half + quarter is 0.75 c - 1.25 at most).
// Not followed: the 3 pixel move of the dimmed side (a PS1 water trick) and
// the white generated corners of WATER_SPLIT_WHITE (RenderBucket_
// WaterSplitInterpolateVertex :616-619, the garage). Where retail draws pieces
// the engine's draws can not show (xor with specLightX >= 0: only the part
// below; a reflection with parts below) the instance is left to retail.

#define EIL_VERTICES_MAX 4096 // EngInst_SubmitLinked draws a live frame of at most ENG_INST_LIVE_MAX vertices
#define EIL_LOG_MAX 16

#define EIL_WRITER_NORMAL 0
#define EIL_WRITER_KEY 1
#define EIL_WRITER_LIT 2
#define EIL_WRITER_OTHER 3

#define EIL_VARIANT_OPAQUE 0
#define EIL_VARIANT_ADD 1
#define EIL_VARIANT_SUBTRACT 2
#define EIL_VARIANTS 3

#define EIL_OP_NONE 0
#define EIL_OP_SHIFT_MASK 1
#define EIL_OP_DIM 2

// The variant of one source mesh for one writer (key, lit).
typedef struct
{
	int state;        // 0 not made, 1 made, -1 refused (logged)
	int mesh;         // the variant mesh (one frame, its ranges by material and side)
	u32 vertexCount;
	u16 *source;      // per variant vertex: the vertex of a source frame it copies
	u8 *back;         // per variant triangle: 1 = retail's back side
	int transparent;  // a range blends (add or subtract)
} EilVariant;

typedef struct
{
	u32 generation; // Engine_ModelTriangleBits generation the variants belong to
	EilVariant v[2]; // key, lit
} EilEntry;

// The light state of one instance in one view, as the setup callback loads it.
typedef struct
{
	float light[3]; // row 1 of the GTE colour matrix
	s32 half[3];    // row 3 (idpp->halfVector)
	float far[3];   // far colour, 0..255
} EilLight;

global_variable EilEntry s_eil[ENGINE_MESHES_MAX];
global_variable EngineVertex s_eilLive[EIL_VERTICES_MAX];
global_variable int s_eilLogged = 0;

internal void Eil_Log(const char *line)
{
	if (s_eilLogged < EIL_LOG_MAX)
	{
		s_eilLogged++;
		Platform_Log("[CTR Engine] instance light: %s\n", line);
	}
}

internal int Eil_WriterOf(const struct Instance *inst)
{
	switch ((u32)(uintptr_t)inst->funcPtr[1])
	{
	case RB_RETAIL_INST_PRIM_NORMAL:
	case RB_RETAIL_INST_PRIM_CLAMP_DEPTH:
	case RB_RETAIL_INST_PRIM_SELECT_RANGE:
		return EIL_WRITER_NORMAL;
	case RB_RETAIL_INST_PRIM_KEY_TOKEN:
		return EIL_WRITER_KEY;
	case RB_RETAIL_INST_PRIM_LIT_TEXTURE:
		return EIL_WRITER_LIT;
	default:
		return EIL_WRITER_OTHER;
	}
}

int Engine_InstLightWriterFollowed(const struct Instance *inst)
{
	return (inst != NULL) && (Eil_WriterOf(inst) != EIL_WRITER_OTHER);
}

// --------------------------------------------------------------------------
// The lit colour.

internal void Eil_LightOf(const struct Instance *inst, const struct InstDrawPerPlayer *idpp, EilLight *l)
{
	const int sx = (int)inst->specLightX;
	const u32 c = inst->colorRGBA;

	l->light[0] = (float)sx;
	l->light[1] = (sx < 0) ? -1.0f : 0.0f; // the upper half of the sign-extended word in CTC2 16
	l->light[2] = (float)(s16)(u16)inst->reflectionRGBA;
	l->half[0] = idpp->halfVector.x;
	l->half[1] = idpp->halfVector.y;
	l->half[2] = idpp->halfVector.z;
	l->far[0] = (float)((c >> 20) & 0xffu);
	l->far[1] = (float)((c >> 12) & 0xffu);
	l->far[2] = (float)((c >> 4) & 0xffu);
}

// The flat colour of a triangle whose first corner holds the normal rgb.
internal void Eil_LitColour(const EilLight *l, const u8 rgb[3], int back, u8 out[3])
{
	const s32 n[3] = {(s32)(s8)rgb[0] * 32, (s32)(s8)rgb[1] * 32, (s32)(s8)rgb[2] * 32};
	float ir1 = (l->light[0] * (float)n[0] + l->light[1] * (float)n[1] + l->light[2] * (float)n[2]) / 4096.0f;
	float b;
	s32 ir3;
	int add = 0;
	int k;

	ir1 = (ir1 < -32768.0f) ? -32768.0f : ((ir1 > 32767.0f) ? 32767.0f : ir1);
	b = ((ir1 + 4096.0f) * 7.0f + 8192.0f) / 16.0f;
	if (back)
	{
		b *= 0.5f;
	}

	// The specular step, in integers (see the file comment).
	ir3 = (l->half[0] * n[0] + l->half[1] * n[1] + l->half[2] * n[2]) >> 12;
	ir3 = (ir3 < -32768) ? -32768 : ((ir3 > 32767) ? 32767 : ir3);
	{
		int distance = ir3 >> 3;

		distance = (distance < 0) ? -distance : distance;
		distance -= 0x180;
		if (distance >= 0)
		{
			add = sRenderBucketKeyRelicBrightness8008a2c4[(distance >= 0x80) ? 0x7f : distance];
		}
	}
	if (back)
	{
		add >>= 3;
	}

	for (k = 0; k < 3; k++)
	{
		float c = floorf(l->far[k] * b / 4096.0f);
		int v;

		c = (c < 0.0f) ? 0.0f : ((c > 255.0f) ? 255.0f : c);
		v = (int)c + add;
		out[k] = (u8)((v > 255) ? 255 : v);
	}
}

// The writer's blend for a layout abr and a side.
internal int Eil_VariantOf(int writer, int abr, int back)
{
	if (writer == EIL_WRITER_LIT)
	{
		return (abr != 0) ? EIL_VARIANT_OPAQUE : (back ? EIL_VARIANT_SUBTRACT : EIL_VARIANT_ADD);
	}
	return ((abr == 0) && !back) ? EIL_VARIANT_ADD : EIL_VARIANT_OPAQUE;
}

// The import's material with the writer's blend: one-sided, the tpage's abr
// bits as the packet carries them (the writer ORs 0x60/0x20/0x40 into the
// layout's word, abr 0 there when it blends).
internal int Eil_RegisterVariant(const EngineMaterial *base, int variant)
{
	EngineMaterial m = *base;

	m.flags = (u16)(m.flags & ~(ENGINE_MATERIAL_DOUBLE_SIDED | ENGINE_MATERIAL_SEMI_STP));
	m.tpage = (u16)(m.tpage & ~0x60u);
	switch (variant)
	{
	case EIL_VARIANT_ADD:
		m.flags |= ENGINE_MATERIAL_SEMI_STP;
		m.blend = (u8)Engine_BlendFromAbr(1);
		m.tpage |= 0x20u;
		break;
	case EIL_VARIANT_SUBTRACT:
		m.flags |= ENGINE_MATERIAL_SEMI_STP;
		m.blend = (u8)Engine_BlendFromAbr(2);
		m.tpage |= 0x40u;
		break;
	default:
		m.blend = (u8)ENGINE_BLEND_OPAQUE;
		m.tpage |= 0x60u;
		break;
	}
	return Engine_MaterialRegister(&m);
}

internal void Eil_FreeVariant(EilVariant *v)
{
	free(v->source);
	free(v->back);
	memset(v, 0, sizeof(*v));
	v->mesh = -1;
}

// The variant of srcMesh (its triangle bits: bits, one per mesh triangle) for
// writer. 1 = made into *out; 0 with why.
internal int Eil_Build(int srcMesh, const u8 *bits, u32 bitCount, int writer, EilVariant *out, char *why, size_t whySize)
{
	const EngineMeshDesc *desc = Engine_MeshDesc(srcMesh);
	EngineRange *ranges = NULL;
	u16 *source = NULL;
	u16 *indices = NULL;
	u8 *back = NULL;
	EngineVertex *vertices = NULL;
	u32 copies = 0;
	u32 maxCopies = 0;
	u32 rangeCount = 0;
	u32 r;
	u32 i;
	int transparent = 0;
	int mesh = -1;
	int ok = 0;

	memset(out, 0, sizeof(*out));
	out->mesh = -1;
	if ((desc == NULL) || (desc->vertices == NULL) || (desc->ranges == NULL) || (bits == NULL) || (desc->indexCount != desc->verticesPerFrame) ||
	    (bitCount * 3u != desc->verticesPerFrame))
	{
		snprintf(why, whySize, "mesh %d without a usable description or triangle bits", srcMesh);
		return 0;
	}
	// How many copies: one per one-sided, two per two-sided textured triangle.
	for (r = 0; r < desc->rangeCount; r++)
	{
		const EngineRange *range = &desc->ranges[r];
		const EngineMaterial *m = Engine_Material((int)range->material);
		u32 t;

		if ((m == NULL) || ((range->firstIndex % 3u) != 0u) || ((range->indexCount % 3u) != 0u) || (range->firstIndex + range->indexCount > bitCount * 3u))
		{
			snprintf(why, whySize, "range %u of mesh %d unusable", (unsigned)r, srcMesh);
			return 0;
		}
		if (m->texSource == ENGINE_TEX_NONE)
		{
			continue;
		}
		for (t = range->firstIndex / 3u; t < (range->firstIndex + range->indexCount) / 3u; t++)
		{
			maxCopies += ((bits[t] & ENGINE_MODEL_TRI_TWO_SIDED) != 0u) ? 2u : 1u;
		}
	}
	if ((maxCopies == 0u) || (maxCopies * 3u > EIL_VERTICES_MAX))
	{
		snprintf(why, whySize, "%u textured triangle copies (1..%u drawable)", (unsigned)maxCopies, (unsigned)(EIL_VERTICES_MAX / 3));
		return 0;
	}

	ranges = (EngineRange *)calloc((size_t)desc->rangeCount * EIL_VARIANTS, sizeof(EngineRange));
	source = (u16 *)calloc((size_t)maxCopies * 3u, sizeof(u16));
	indices = (u16 *)calloc((size_t)maxCopies * 3u, sizeof(u16));
	back = (u8 *)calloc(maxCopies, 1);
	vertices = (EngineVertex *)calloc((size_t)maxCopies * 3u, sizeof(EngineVertex));
	if ((ranges == NULL) || (source == NULL) || (indices == NULL) || (back == NULL) || (vertices == NULL))
	{
		snprintf(why, whySize, "out of memory");
		goto done;
	}

	for (r = 0; r < desc->rangeCount; r++)
	{
		const EngineRange *range = &desc->ranges[r];
		const EngineMaterial *m = Engine_Material((int)range->material);
		const int abr = (int)((m->tpage >> 5) & 3u);
		int variant;

		if (m->texSource == ENGINE_TEX_NONE)
		{
			continue;
		}
		for (variant = 0; variant < EIL_VARIANTS; variant++)
		{
			const u32 start = copies;
			u32 t;

			for (t = range->firstIndex / 3u; t < (range->firstIndex + range->indexCount) / 3u; t++)
			{
				static const u8 kept[3] = {0, 1, 2};
				static const u8 reversed[3] = {0, 2, 1};
				const int twoSided = (bits[t] & ENGINE_MODEL_TRI_TWO_SIDED) != 0u;
				const int flip = (bits[t] & ENGINE_MODEL_TRI_FLIP) != 0u;
				int side;

				for (side = 0; side < (twoSided ? 2 : 1); side++)
				{
					const u8 *order;
					int c;

					if (Eil_VariantOf(writer, abr, side) != variant)
					{
						continue;
					}
					if (!twoSided)
					{
						order = kept;
					}
					else
					{
						// front: listed with bit 29, reversed without; back: the other
						order = ((side == 0) == flip) ? kept : reversed;
					}
					for (c = 0; c < 3; c++)
					{
						source[copies * 3u + (u32)c] = (u16)(t * 3u + order[c]);
					}
					back[copies] = (u8)side;
					copies++;
				}
			}
			if (copies > start)
			{
				const int material = Eil_RegisterVariant(m, variant);

				if (material < 0)
				{
					snprintf(why, whySize, "material table full");
					goto done;
				}
				ranges[rangeCount].firstIndex = start * 3u;
				ranges[rangeCount].indexCount = (copies - start) * 3u;
				ranges[rangeCount].material = (u16)material;
				rangeCount++;
				transparent |= (variant != EIL_VARIANT_OPAQUE);
			}
		}
	}

	for (i = 0; i < copies * 3u; i++)
	{
		vertices[i] = desc->vertices[source[i]];
		indices[i] = (u16)i;
	}
	{
		EngineMeshDesc vd;
		char name[96];

		snprintf(name, sizeof(name), "%s (%s)", (desc->name != NULL) ? desc->name : "model", (writer == EIL_WRITER_LIT) ? "lit" : "key");
		memset(&vd, 0, sizeof(vd));
		vd.vertices = vertices;
		vd.verticesPerFrame = copies * 3u;
		vd.frames = 1;
		vd.indices = indices;
		vd.indexCount = copies * 3u;
		vd.ranges = ranges;
		vd.rangeCount = rangeCount;
		vd.name = name;
		mesh = Engine_MeshCreate(&vd);
		if (mesh < 0)
		{
			snprintf(why, whySize, "mesh table full or mesh refused");
			goto done;
		}
	}
	out->state = 1;
	out->mesh = mesh;
	out->vertexCount = copies * 3u;
	out->source = source;
	out->back = back;
	out->transparent = transparent;
	source = NULL;
	back = NULL;
	ok = 1;

done:
	free(ranges);
	free(source);
	free(indices);
	free(back);
	free(vertices);
	return ok;
}

// The live vertices of a variant: the source frame's vertices in the copies'
// order, each triangle in its side's flat colour.
internal void Eil_Fill(const EilVariant *v, const EngineVertex *src, const EilLight *l, EngineVertex *out)
{
	u32 t;

	for (t = 0; t < v->vertexCount / 3u; t++)
	{
		u8 colour[3];
		int c;

		// corner 0 of a copy is corner 0 of its source triangle (both orders keep it)
		Eil_LitColour(l, src[v->source[t * 3u]].color, v->back[t], colour);
		for (c = 0; c < 3; c++)
		{
			EngineVertex *o = &out[t * 3u + (u32)c];

			*o = src[v->source[t * 3u + (u32)c]];
			o->color[0] = colour[0];
			o->color[1] = colour[1];
			o->color[2] = colour[2];
			o->color[3] = 255;
		}
	}
}

int Engine_InstLightApply(const struct Instance *inst, const struct InstDrawPerPlayer *idpp, int mesh, u32 frame, EngineInstanceDraw *d)
{
	const EngineMeshDesc *desc;
	const EngineMeshDesc *vdesc;
	const EngineVertex *src;
	const u8 *bits;
	EilEntry *e;
	EilVariant *v;
	EilLight light;
	u32 count = 0;
	u32 generation = 0;
	int writer;

	if ((inst == NULL) || (idpp == NULL) || (d == NULL))
	{
		return 0;
	}
	writer = Eil_WriterOf(inst);
	if (writer == EIL_WRITER_NORMAL)
	{
		return 1;
	}
	if ((writer != EIL_WRITER_KEY) && (writer != EIL_WRITER_LIT))
	{
		return 0;
	}
	desc = Engine_MeshDesc(mesh);
	bits = Engine_ModelTriangleBits(mesh, &count, &generation);
	if ((desc == NULL) || (bits == NULL) || (mesh < 0) || (mesh >= ENGINE_MESHES_MAX))
	{
		char line[160];

		snprintf(line, sizeof(line), "mesh %d '%s' has no triangle sides: retail draws it", mesh, ((desc != NULL) && (desc->name != NULL)) ? desc->name : "?");
		Eil_Log(line);
		return 0;
	}
	e = &s_eil[mesh];
	if (e->generation != generation)
	{
		// A new import into this slot: the old variants went with the old level.
		Eil_FreeVariant(&e->v[0]);
		Eil_FreeVariant(&e->v[1]);
		e->generation = generation;
	}
	v = &e->v[(writer == EIL_WRITER_LIT) ? 1 : 0];
	if (v->state == 0)
	{
		char why[128];
		char line[256];

		if (!Eil_Build(mesh, bits, count, writer, v, why, sizeof(why)))
		{
			v->state = -1;
			snprintf(line, sizeof(line), "mesh %d '%s' (%s writer) refused: %s - retail draws it", mesh, (desc->name != NULL) ? desc->name : "?",
			         (writer == EIL_WRITER_LIT) ? "lit" : "key", why);
			Eil_Log(line);
		}
		else
		{
			snprintf(line, sizeof(line), "mesh %d '%s' (%s writer): variant mesh %d, %u vertices", mesh, (desc->name != NULL) ? desc->name : "?",
			         (writer == EIL_WRITER_LIT) ? "lit" : "key", v->mesh, (unsigned)v->vertexCount);
			Eil_Log(line);
		}
	}
	if (v->state < 0)
	{
		return 0;
	}
	vdesc = Engine_MeshDesc(v->mesh);
	if ((vdesc == NULL) || (vdesc->verticesPerFrame != v->vertexCount))
	{
		return 0;
	}
	if (d->liveVertices != NULL)
	{
		src = d->liveVertices;
	}
	else
	{
		if (frame >= desc->frames)
		{
			return 0;
		}
		src = desc->vertices + (size_t)frame * desc->verticesPerFrame;
	}
	Eil_LightOf(inst, idpp, &light);
	Eil_Fill(v, src, &light, s_eilLive);
	d->mesh = v->mesh;
	d->frame = 0;
	d->liveVertices = s_eilLive;
	d->flags = (u16)(v->transparent ? (d->flags | ENGINE_DRAW_TRANSPARENT) : (d->flags & ~ENGINE_DRAW_TRANSPARENT));
	return 1;
}

// --------------------------------------------------------------------------
// The water and mirror sides.

// The side selector for a piece below (1) or above (0) the plane in the
// mirror pass (1) or the original pass (0): 1 = drawn, *op its colour.
internal int Eil_SideSelect(u32 func2, int below, int mirrorPass, int specLightX, int *op)
{
	*op = EIL_OP_NONE;
	switch (func2)
	{
	case RB_RETAIL_INST_FUNC2_SPLIT_BOTH_MASK:
		*op = below ? EIL_OP_SHIFT_MASK : EIL_OP_NONE;
		return 1;
	case RB_RETAIL_INST_FUNC2_SPLIT_NEGATIVE:
		if (below)
		{
			return 0;
		}
		*op = mirrorPass ? EIL_OP_SHIFT_MASK : EIL_OP_NONE;
		return 1;
	case RB_RETAIL_INST_FUNC2_SPLIT_XOR:
		return below == (specLightX >= 0);
	case RB_RETAIL_INST_FUNC2_SPLIT_DIM_XOR:
		*op = (below == (specLightX >= 0)) ? EIL_OP_DIM : EIL_OP_NONE;
		return 1;
	default:
		return !below;
	}
}

// The tint of (c >> s) & mask per channel. 1 = within the floor of the shift
// (under 1/255 off), 0 = the nearest tint of a mask that takes bits of the
// next channel or keeps part of a channel.
internal int Eil_ShiftMaskTint(int specLightX, u32 mask, float tint[3])
{
	const int s = specLightX & 31;
	const u32 keep = (s >= 8) ? 0u : (0xffu >> s);
	int exact = 1;
	int k;

	for (k = 0; k < 3; k++)
	{
		const u32 m = (mask >> (8 * k)) & 0xffu;

		if (m == 0u)
		{
			tint[k] = 0.0f;
		}
		else if ((m == keep) && (keep != 0u))
		{
			tint[k] = 1.0f / (float)(1u << s);
		}
		else
		{
			exact = 0;
			tint[k] = (keep != 0u) ? ((float)(m & keep) / (float)keep) / (float)(1u << s) : 0.0f;
		}
	}
	return exact;
}

internal void Eil_OpTint(int op, int specLightX, u32 mask, float tint[4])
{
	float t[3] = {1.0f, 1.0f, 1.0f};
	int k;

	if (op == EIL_OP_SHIFT_MASK)
	{
		if (!Eil_ShiftMaskTint(specLightX, mask, t))
		{
			char line[128];

			snprintf(line, sizeof(line), "side colour shift %d mask 0x%06x is not a tint per channel: nearest tint %.3f %.3f %.3f", specLightX & 31,
			         (unsigned)(mask & 0xffffffu), t[0], t[1], t[2]);
			Eil_Log(line);
		}
	}
	else if (op == EIL_OP_DIM)
	{
		t[0] = t[1] = t[2] = 0.75f;
	}
	for (k = 0; k < 3; k++)
	{
		tint[k] *= t[k];
	}
}

// The engine's draws from the selector; 0 = retail draws pieces they can not show.
internal int Eil_SplitTints(int handler, u32 func2, int specLightX, u32 mask, EngineInstanceDraw *d)
{
	int op;

	switch ((u32)handler)
	{
	case RB_RETAIL_DRAWFUNC_SPLIT:
	{
		int opBelow;

		if (!Eil_SideSelect(func2, 0, 0, specLightX, &op))
		{
			return 0;
		}
		d->drawBelow = Eil_SideSelect(func2, 1, 0, specLightX, &opBelow) ? d->drawBelow : 0;
		Eil_OpTint(op, specLightX, mask, d->tint);
		Eil_OpTint(opBelow, specLightX, mask, d->belowTint);
		return 1;
	}
	case RB_RETAIL_DRAWFUNC_REFLECTION:
	{
		int opMirror;
		int unused;

		if (!Eil_SideSelect(func2, 0, 0, specLightX, &op) || !Eil_SideSelect(func2, 0, 1, specLightX, &opMirror) ||
		    Eil_SideSelect(func2, 1, 0, specLightX, &unused) || Eil_SideSelect(func2, 1, 1, specLightX, &unused))
		{
			return 0;
		}
		Eil_OpTint(op, specLightX, mask, d->tint);
		Eil_OpTint(opMirror, specLightX, mask, d->mirrorTint);
		return 1;
	}
	case RB_RETAIL_DRAWFUNC_SPECIAL:
		Eil_OpTint(EIL_OP_SHIFT_MASK, specLightX, mask, d->mirrorTint);
		return 1;
	default:
		return 1;
	}
}

int Engine_InstLightSplit(const struct Instance *inst, const struct InstDrawPerPlayer *idpp, EngineInstanceDraw *d)
{
	const u32 func2 = (u32)(uintptr_t)((inst != NULL) ? inst->funcPtr[2] : NULL);

	if ((inst == NULL) || (idpp == NULL) || (d == NULL))
	{
		return 0;
	}
	if (!Eil_SplitTints(idpp->unkEC, func2, (int)inst->specLightX, inst->reflectionRGBA, d))
	{
		char line[160];

		snprintf(line, sizeof(line), "handler 0x%08x side selector 0x%08x specLightX %d draws pieces the engine does not: retail draws it", (unsigned)idpp->unkEC,
		         (unsigned)func2, (int)inst->specLightX);
		Eil_Log(line);
		return 0;
	}
	return 1;
}

// --------------------------------------------------------------------------
// Selftest

#define EIL_CHECK(cond, ...)                                     \
	do                                                           \
	{                                                            \
		(*checks)++;                                             \
		if (!(cond))                                             \
		{                                                        \
			failures++;                                          \
			printf("engine selftest: instance light: " __VA_ARGS__); \
			printf("\n");                                        \
		}                                                        \
	} while (0)

internal u32 EilTest_Next(u32 *seed)
{
	*seed = (*seed * 1103515245u) + 12345u;
	return (*seed >> 8) & 0xffffffu;
}

// The expectation, independent of Eil_LitColour: the light setup and the
// writer's GTE sequence as retail runs them (RB:5383-5389 with :5369-5377,
// then :3424-3459 / :3204-3246) on the emulated GTE.
internal void EilTest_RetailLit(int specLightX, u32 reflection, const s16 half[3], u32 colorRGBA, const u8 rgb[3], int back, u8 out[3])
{
	const u32 sourceColor = (u32)rgb[0] | ((u32)rgb[1] << 8) | ((u32)rgb[2] << 16);
	int brightness;
	int distance;
	int add;
	u32 litColor;
	int r;
	int g;
	int b;

	CTC2((u32)specLightX, 16);
	CTC2((u16)reflection, 17);
	CTC2((u32)(u16)half[0] | ((u32)(u16)half[1] << 16), 19);
	CTC2((u16)half[2], 20);
	MTC2(0, 6);
	CTC2((colorRGBA >> 16) & 0xff0, 21);
	CTC2((colorRGBA >> 8) & 0xff0, 22);
	CTC2(colorRGBA & 0xff0, 23);

	MTC2((u32)((s32)(sourceColor << 24) >> 19), 9);
	MTC2((u32)((s32)((sourceColor >> 8) << 24) >> 19), 10);
	MTC2((u32)((s32)((sourceColor >> 16) << 24) >> 19), 11);
	gte_lcir();
	brightness = MFC2_S(9) + 0x1000;
	distance = MFC2_S(11) >> 3;
	brightness = (((brightness * 7) + 0x2000) >> 4);
	if (back)
	{
		brightness >>= 1;
	}
	MTC2((u32)brightness, 8);
	gte_dpcs();
	if (distance < 0)
	{
		distance = -distance;
	}
	add = 0;
	distance -= 0x180;
	if (distance >= 0)
	{
		if (distance >= 0x80)
		{
			distance = 0x7f;
		}
		add = sRenderBucketKeyRelicBrightness8008a2c4[distance];
	}
	litColor = (u32)MFC2(22);
	if (back)
	{
		add >>= 3;
	}
	r = (int)(litColor & 0xff) + add;
	g = (int)((litColor >> 8) & 0xff) + add;
	b = (int)((litColor >> 16) & 0xff) + add;
	out[0] = (u8)((r > 255) ? 255 : r);
	out[1] = (u8)((g > 255) ? 255 : g);
	out[2] = (u8)((b > 255) ? 255 : b);
}

internal int EilTest_Diff(const u8 a[3], const u8 b[3])
{
	int worst = 0;
	int k;

	for (k = 0; k < 3; k++)
	{
		const int dlt = abs((int)a[k] - (int)b[k]);

		worst = (dlt > worst) ? dlt : worst;
	}
	return worst;
}

global_variable struct RenderBucketDrawContext s_eilTestCtx;

int Engine_SelfTestInstLight(int *checks)
{
	static const u32 farColours[4] = {0x0dca6000u, 0x0d22fff0u, 0x0ffc8000u, 0x0ffffff0u}; // flamejet, crystal, letters (game/231), white
	static const u32 func2s[5] = {RB_RETAIL_INST_FUNC2_SPLIT_BOTH_MASK, RB_RETAIL_INST_FUNC2_SPLIT_NEGATIVE, RB_RETAIL_INST_FUNC2_SPLIT_XOR,
	                              RB_RETAIL_INST_FUNC2_SPLIT_DIM_XOR, 0x8006d000u};
	static const int shifts[4] = {1, -3, 0, 2};
	static const u32 masks[4] = {0x7f7f7fu, 0xffffffu, 0x7f007fu, 0x3f3f3fu};
	int failures = 0;
	u32 seed = 20261016u;
	int worst = 0;
	int over = 0;
	int exactCount = 0;
	int i;

	failures += Engine_SelfTestModelTriangles(checks);

	// THE WRITERS that are followed.
	{
		struct Instance inst;
		static const u32 labels[7] = {RB_RETAIL_INST_PRIM_NORMAL,      RB_RETAIL_INST_PRIM_SELECT_RANGE, RB_RETAIL_INST_PRIM_CLAMP_DEPTH, RB_RETAIL_INST_PRIM_KEY_TOKEN,
		                              RB_RETAIL_INST_PRIM_LIT_TEXTURE, RB_RETAIL_INST_PRIM_DEPTH_FADE,   RB_RETAIL_INST_PRIM_GHOST};
		static const int followed[7] = {1, 1, 1, 1, 1, 0, 0};

		memset(&inst, 0, sizeof(inst));
		for (i = 0; i < 7; i++)
		{
			inst.funcPtr[1] = (void *)(uintptr_t)labels[i];
			EIL_CHECK(Engine_InstLightWriterFollowed(&inst) == followed[i], "writer 0x%08x followed %d", (unsigned)labels[i], followed[i]);
		}
		EIL_CHECK(!Engine_InstLightWriterFollowed(NULL), "no instance");
	}

	// THE LIT COLOUR against the writer's GTE sequence: random normals, light
	// directions (specLightX is an s8, the z a 16-bit word), half vectors and
	// far colours, both sides; plus a normal along the half vector (top of the
	// specular table) and against it (the absolute value).
	for (i = 0; i < 4000; i++)
	{
		EilLight l;
		struct Instance inst;
		struct InstDrawPerPlayer idpp;
		s16 half[3];
		u8 rgb[3];
		u8 want[3];
		u8 got[3];
		const int back = i & 1;
		int dlt;

		memset(&inst, 0, sizeof(inst));
		memset(&idpp, 0, sizeof(idpp));
		inst.specLightX = (s8)(EilTest_Next(&seed) & 0xffu);
		inst.reflectionRGBA = (u32)((s32)(EilTest_Next(&seed) % 8193u) - 4096) & 0xffffu;
		inst.colorRGBA = ((i % 5) == 4) ? (EilTest_Next(&seed) << 4) : farColours[i & 3];
		half[0] = (s16)((s32)(EilTest_Next(&seed) % 8193u) - 4096);
		half[1] = (s16)((s32)(EilTest_Next(&seed) % 8193u) - 4096);
		half[2] = (s16)((s32)(EilTest_Next(&seed) % 8193u) - 4096);
		if ((i % 7) == 3)
		{
			// a normal along (or against) the half vector: |IR3| near the top
			const s16 h = (s16)(((i % 14) == 3) ? 4000 : -4000);

			half[0] = 0;
			half[1] = 0;
			half[2] = h;
			rgb[0] = 0;
			rgb[1] = 0;
			rgb[2] = (u8)(127 - (i % 40));
		}
		else
		{
			rgb[0] = (u8)EilTest_Next(&seed);
			rgb[1] = (u8)EilTest_Next(&seed);
			rgb[2] = (u8)EilTest_Next(&seed);
		}
		idpp.halfVector.x = half[0];
		idpp.halfVector.y = half[1];
		idpp.halfVector.z = half[2];
		Eil_LightOf(&inst, &idpp, &l);
		Eil_LitColour(&l, rgb, back, got);
		EilTest_RetailLit((int)inst.specLightX, inst.reflectionRGBA, half, inst.colorRGBA, rgb, back, want);
		dlt = EilTest_Diff(got, want);
		worst = (dlt > worst) ? dlt : worst;
		exactCount += (dlt == 0);
		if (dlt > 2)
		{
			over++;
			if (over <= 4)
			{
				EIL_CHECK(0, "lit colour %u %u %u, retail %u %u %u (normal %u %u %u, back %d, light %d %d, half %d %d %d, far 0x%08x)", got[0], got[1], got[2],
				          want[0], want[1], want[2], rgb[0], rgb[1], rgb[2], back, (int)inst.specLightX, (int)(s16)inst.reflectionRGBA, half[0], half[1],
				          half[2], (unsigned)inst.colorRGBA);
			}
		}
	}
	EIL_CHECK(over == 0, "lit colour: %d of 4000 off by more than 2/255 (worst %d)", over, worst);
	printf("engine selftest info: instance light colour worst %d/255 against the writer's GTE sequence (4000 cases, %d exact)\n", worst, exactCount);

	// THE BLEND per writer, layout abr and side (RB:3253-3259, :3470-3494).
	{
		int abr;

		for (abr = 0; abr < 4; abr++)
		{
			EIL_CHECK(Eil_VariantOf(EIL_WRITER_LIT, abr, 0) == ((abr != 0) ? EIL_VARIANT_OPAQUE : EIL_VARIANT_ADD), "lit front abr %d", abr);
			EIL_CHECK(Eil_VariantOf(EIL_WRITER_LIT, abr, 1) == ((abr != 0) ? EIL_VARIANT_OPAQUE : EIL_VARIANT_SUBTRACT), "lit back abr %d", abr);
			EIL_CHECK(Eil_VariantOf(EIL_WRITER_KEY, abr, 0) == ((abr != 0) ? EIL_VARIANT_OPAQUE : EIL_VARIANT_ADD), "key front abr %d", abr);
			EIL_CHECK(Eil_VariantOf(EIL_WRITER_KEY, abr, 1) == EIL_VARIANT_OPAQUE, "key back abr %d", abr);
		}
	}

	// THE SIDE SELECTOR against retail's (RenderBucket_ApplyWaterSplitSideSelector
	// on a context with only the instance and the pass set): drawn or not, and
	// the colour retail writes against the engine's tint times the colour.
	{
		struct Instance inst;
		int f;
		int s;
		int below;
		int pass;

		memset(&inst, 0, sizeof(inst));
		for (f = 0; f < 5; f++)
		{
			for (s = 0; s < 4; s++)
			{
				for (below = 0; below < 2; below++)
				{
					for (pass = 0; pass < 2; pass++)
					{
						struct RenderBucketSplitVertex v[3];
						int op;
						int drawnEngine;
						int drawnRetail;
						int c;

						memset(&s_eilTestCtx, 0, sizeof(s_eilTestCtx));
						memset(v, 0, sizeof(v));
						inst.funcPtr[2] = (void *)(uintptr_t)func2s[f];
						inst.specLightX = (s8)shifts[s];
						inst.reflectionRGBA = masks[s];
						s_eilTestCtx.inst = &inst;
						s_eilTestCtx.waterSplitSide = pass ? 0 : -1;
						for (c = 0; c < 3; c++)
						{
							v[c].color = EilTest_Next(&seed) & 0xffffffu;
						}
						{
							const u32 before[3] = {v[0].color, v[1].color, v[2].color};

							drawnRetail = RenderBucket_ApplyWaterSplitSideSelector(&s_eilTestCtx, below ? 5 : -5, &v[0], &v[1], &v[2]) != 0;
							drawnEngine = Eil_SideSelect(func2s[f], below, pass, shifts[s], &op);
							EIL_CHECK(drawnEngine == drawnRetail, "selector 0x%08x shift %d below %d mirror pass %d: drawn %d, retail %d", (unsigned)func2s[f],
							          shifts[s], below, pass, drawnEngine, drawnRetail);
							if (drawnRetail && drawnEngine)
							{
								float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
								int exact = 1;

								if (op == EIL_OP_SHIFT_MASK)
								{
									float t3[3];

									exact = Eil_ShiftMaskTint(shifts[s], masks[s], t3);
								}
								Eil_OpTint(op, shifts[s], masks[s], tint);
								for (c = 0; (c < 3) && exact; c++)
								{
									int k;

									for (k = 0; k < 3; k++)
									{
										const float engine = floorf((float)((before[c] >> (8 * k)) & 0xffu) * tint[k] + 0.5f);
										const float retail = (float)((v[c].color >> (8 * k)) & 0xffu);

										EIL_CHECK(fabsf(engine - retail) <= 2.0f, "selector 0x%08x shift %d mask 0x%06x below %d pass %d corner %d channel %d: %g, retail %g",
										          (unsigned)func2s[f], shifts[s], (unsigned)masks[s], below, pass, c, k, engine, retail);
									}
								}
							}
						}
					}
				}
			}
		}
		// The masks of the cases: exact ones and one that takes bits of the next channel.
		{
			float t3[3];

			EIL_CHECK(Eil_ShiftMaskTint(1, 0x7f7f7fu, t3) && (t3[0] == 0.5f) && (t3[1] == 0.5f) && (t3[2] == 0.5f), "default instance: half");
			EIL_CHECK(Eil_ShiftMaskTint(0, 0xffffffu, t3) && (t3[0] == 1.0f), "shift 0: unchanged");
			EIL_CHECK(Eil_ShiftMaskTint(1, 0x7f007fu, t3) && (t3[1] == 0.0f) && (t3[2] == 0.5f), "a channel masked out");
			EIL_CHECK(!Eil_ShiftMaskTint(1, 0xffffffu, t3), "a mask that takes the next channel's bit is not a tint");
		}
	}

	// THE ENGINE'S DRAWS from the selector per handler.
	{
		struct Instance inst;
		struct InstDrawPerPlayer idpp;
		EngineInstanceDraw d;
		int r;

		memset(&inst, 0, sizeof(inst));
		memset(&idpp, 0, sizeof(idpp));
		inst.reflectionRGBA = 0x7f7f7fu;

#define EIL_DRAW_RESET()                                                                          \
	do                                                                                            \
	{                                                                                             \
		memset(&d, 0, sizeof(d));                                                                 \
		d.tint[0] = d.tint[1] = d.tint[2] = d.tint[3] = 1.0f;                                     \
		d.belowTint[0] = d.belowTint[1] = d.belowTint[2] = d.belowTint[3] = 1.0f;                 \
		d.mirrorTint[0] = d.mirrorTint[1] = d.mirrorTint[2] = d.mirrorTint[3] = 1.0f;             \
		d.drawBelow = 1;                                                                          \
	} while (0)

		idpp.unkEC = (int)RB_RETAIL_DRAWFUNC_SPLIT;
		inst.specLightX = 1;
		inst.funcPtr[2] = (void *)(uintptr_t)RB_RETAIL_INST_FUNC2_SPLIT_BOTH_MASK;
		EIL_DRAW_RESET();
		r = Engine_InstLightSplit(&inst, &idpp, &d);
		EIL_CHECK(r && (d.tint[0] == 1.0f) && d.drawBelow && (d.belowTint[0] == 0.5f) && (d.belowTint[2] == 0.5f) && (d.belowTint[3] == 1.0f),
		          "split, both+mask: above 1, below half");
		inst.funcPtr[2] = (void *)(uintptr_t)RB_RETAIL_INST_FUNC2_SPLIT_NEGATIVE;
		EIL_DRAW_RESET();
		r = Engine_InstLightSplit(&inst, &idpp, &d);
		EIL_CHECK(r && (d.tint[0] == 1.0f) && !d.drawBelow, "split, negative: above only");
		inst.funcPtr[2] = (void *)(uintptr_t)RB_RETAIL_INST_FUNC2_SPLIT_DIM_XOR;
		EIL_DRAW_RESET();
		r = Engine_InstLightSplit(&inst, &idpp, &d);
		EIL_CHECK(r && (d.tint[0] == 1.0f) && d.drawBelow && (d.belowTint[1] == 0.75f), "split, dim xor, specLightX 1: below dimmed");
		inst.specLightX = -2;
		EIL_DRAW_RESET();
		r = Engine_InstLightSplit(&inst, &idpp, &d);
		EIL_CHECK(r && (d.tint[1] == 0.75f) && d.drawBelow && (d.belowTint[1] == 1.0f), "split, dim xor, specLightX -2: above dimmed");
		inst.funcPtr[2] = (void *)(uintptr_t)RB_RETAIL_INST_FUNC2_SPLIT_XOR;
		EIL_DRAW_RESET();
		r = Engine_InstLightSplit(&inst, &idpp, &d);
		EIL_CHECK(r && !d.drawBelow && (d.tint[0] == 1.0f), "split, xor, specLightX -2: above only");
		inst.specLightX = 1;
		EIL_DRAW_RESET();
		EIL_CHECK(!Engine_InstLightSplit(&inst, &idpp, &d), "split, xor, specLightX 1: below only - retail draws");

		idpp.unkEC = (int)RB_RETAIL_DRAWFUNC_REFLECTION;
		inst.funcPtr[2] = (void *)(uintptr_t)RB_RETAIL_INST_FUNC2_SPLIT_NEGATIVE;
		EIL_DRAW_RESET();
		r = Engine_InstLightSplit(&inst, &idpp, &d);
		EIL_CHECK(r && (d.tint[0] == 1.0f) && (d.mirrorTint[0] == 0.5f) && (d.mirrorTint[3] == 1.0f), "reflection, negative: mirror half");
		inst.funcPtr[2] = (void *)(uintptr_t)RB_RETAIL_INST_FUNC2_SPLIT_XOR;
		inst.specLightX = -1;
		EIL_DRAW_RESET();
		r = Engine_InstLightSplit(&inst, &idpp, &d);
		EIL_CHECK(r && (d.tint[0] == 1.0f) && (d.mirrorTint[0] == 1.0f), "reflection, xor, specLightX -1: both unchanged");
		inst.specLightX = 1;
		EIL_DRAW_RESET();
		EIL_CHECK(!Engine_InstLightSplit(&inst, &idpp, &d), "reflection, xor, specLightX 1: parts below only - retail draws");

		idpp.unkEC = (int)RB_RETAIL_DRAWFUNC_SPECIAL;
		inst.funcPtr[2] = (void *)(uintptr_t)RB_RETAIL_INST_FUNC2_SPLIT_XOR;
		EIL_DRAW_RESET();
		r = Engine_InstLightSplit(&inst, &idpp, &d);
		EIL_CHECK(r && (d.tint[0] == 1.0f) && (d.mirrorTint[1] == 0.5f), "special: the mirror shifted and masked whatever the selector");

		idpp.unkEC = (int)RB_RETAIL_DRAWFUNC_NORMAL;
		EIL_DRAW_RESET();
		r = Engine_InstLightSplit(&inst, &idpp, &d);
		EIL_CHECK(r && (d.tint[0] == 1.0f) && (d.belowTint[0] == 1.0f) && (d.mirrorTint[0] == 1.0f) && d.drawBelow, "normal: nothing changes");
#undef EIL_DRAW_RESET
	}

	// THE VARIANT of a made-up mesh: tri 0 one-sided on an opaque material
	// (abr 3), tri 1 two-sided with bit 29 and tri 2 two-sided without on a
	// double-sided abr 0 material, tri 3 untextured.
	{
		static const float corner[12][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {2, 0, 0}, {3, 0, 0}, {2, 1, 0}, {4, 0, 0}, {5, 0, 0}, {4, 1, 0}, {6, 0, 0}, {7, 0, 0}, {6, 1, 0}};
		static const u8 bits[4] = {0u, ENGINE_MODEL_TRI_TWO_SIDED | ENGINE_MODEL_TRI_FLIP, ENGINE_MODEL_TRI_TWO_SIDED, 0u};
		EngineMaterial mo;
		EngineMaterial ms;
		EngineMaterial mu;
		EngineVertex vertices[12];
		u16 indices[12];
		EngineRange ranges[3];
		EngineMeshDesc desc;
		int base;
		int srcMesh;
		int writer;

		memset(&mo, 0, sizeof(mo));
		mo.texSource = ENGINE_TEX_VRAM;
		mo.tpage = 0x0068u; // abr 3: opaque
		mo.clut = 0x1234u;
		mo.tint[0] = mo.tint[1] = mo.tint[2] = mo.tint[3] = 1.0f;
		ms = mo;
		ms.tpage = 0x0008u; // abr 0
		ms.flags = ENGINE_MATERIAL_DOUBLE_SIDED | ENGINE_MATERIAL_SEMI_STP;
		ms.blend = (u8)Engine_BlendFromAbr(0);
		mu = mo;
		mu.texSource = ENGINE_TEX_NONE;
		base = Engine_MaterialRegister(&mo);
		Engine_MaterialRegister(&ms);
		Engine_MaterialRegister(&mu);
		memset(vertices, 0, sizeof(vertices));
		for (i = 0; i < 12; i++)
		{
			vertices[i].position[0] = corner[i][0];
			vertices[i].position[1] = corner[i][1];
			vertices[i].position[2] = corner[i][2];
			vertices[i].texcoord[0] = (float)(10 + i);
			vertices[i].color[0] = (u8)(i * 3);       // normal bytes
			vertices[i].color[1] = (u8)(100 + i);
			vertices[i].color[2] = (u8)(200 + i);
			vertices[i].color[3] = 255;
			indices[i] = (u16)i;
		}
		ranges[0].firstIndex = 0;
		ranges[0].indexCount = 3;
		ranges[0].material = (u16)base;
		ranges[1].firstIndex = 3;
		ranges[1].indexCount = 6;
		ranges[1].material = (u16)(base + 1);
		ranges[2].firstIndex = 9;
		ranges[2].indexCount = 3;
		ranges[2].material = (u16)(base + 2);
		memset(&desc, 0, sizeof(desc));
		desc.vertices = vertices;
		desc.verticesPerFrame = 12;
		desc.frames = 1;
		desc.indices = indices;
		desc.indexCount = 12;
		desc.ranges = ranges;
		desc.rangeCount = 3;
		desc.name = "light-test";
		srcMesh = (base >= 0) ? Engine_MeshCreate(&desc) : -1;
		EIL_CHECK(srcMesh >= 0, "test mesh made");

		for (writer = EIL_WRITER_KEY; (writer <= EIL_WRITER_LIT) && (srcMesh >= 0); writer++)
		{
			// LIT: tri 0 opaque front; tri 1/2 front add (1 listed, 2 reversed), back subtract (1 reversed, 2 listed).
			// KEY: tri 0 opaque front; tri 1/2 backs opaque (1 reversed, 2 listed), fronts add (1 listed, 2 reversed).
			static const u16 litSource[15] = {0, 1, 2, 3, 4, 5, 6, 8, 7, 3, 5, 4, 6, 7, 8};
			static const u8 litBack[5] = {0, 0, 0, 1, 1};
			static const u16 keySource[15] = {0, 1, 2, 3, 5, 4, 6, 7, 8, 3, 4, 5, 6, 8, 7};
			static const u8 keyBack[5] = {0, 1, 1, 0, 0};
			const u16 *wantSource = (writer == EIL_WRITER_LIT) ? litSource : keySource;
			const u8 *wantBack = (writer == EIL_WRITER_LIT) ? litBack : keyBack;
			EilVariant v;
			char why[128];
			int ok;

			why[0] = '\0';
			ok = Eil_Build(srcMesh, bits, 4u, writer, &v, why, sizeof(why));
			EIL_CHECK(ok && (v.vertexCount == 15u) && (v.transparent == 1), "%s variant: made %d (%s), %u vertices", (writer == EIL_WRITER_LIT) ? "lit" : "key", ok, why,
			          ok ? (unsigned)v.vertexCount : 0u);
			if (ok && (v.vertexCount == 15u))
			{
				const EngineMeshDesc *vd = Engine_MeshDesc(v.mesh);
				EilLight l;
				EngineVertex live[15];
				int t;

				EIL_CHECK(memcmp(v.source, wantSource, sizeof(litSource)) == 0, "%s variant: corner order", (writer == EIL_WRITER_LIT) ? "lit" : "key");
				EIL_CHECK(memcmp(v.back, wantBack, sizeof(litBack)) == 0, "%s variant: sides", (writer == EIL_WRITER_LIT) ? "lit" : "key");
				EIL_CHECK((vd != NULL) && (vd->rangeCount == 3u) && (vd->frames == 1u) && (vd->verticesPerFrame == 15u), "%s variant mesh shape",
				          (writer == EIL_WRITER_LIT) ? "lit" : "key");
				if ((vd != NULL) && (vd->rangeCount == 3u))
				{
					const EngineMaterial *m0 = Engine_Material(vd->ranges[0].material);
					const EngineMaterial *m1 = Engine_Material(vd->ranges[1].material);
					const EngineMaterial *m2 = Engine_Material(vd->ranges[2].material);
					const int blend2 = (writer == EIL_WRITER_LIT) ? (int)Engine_BlendFromAbr(2) : (int)Engine_BlendFromAbr(1);
					const u16 tpage1 = (writer == EIL_WRITER_LIT) ? 0x0028u : 0x0068u;
					const u16 tpage2 = (writer == EIL_WRITER_LIT) ? 0x0048u : 0x0028u;

					EIL_CHECK((m0 != NULL) && (m0->blend == ENGINE_BLEND_OPAQUE) && (m0->tpage == 0x0068u) && (m0->flags == 0u) && (m0->clut == 0x1234u) &&
					              (vd->ranges[0].firstIndex == 0u) && (vd->ranges[0].indexCount == 3u),
					          "%s variant range 0: opaque one-sided", (writer == EIL_WRITER_LIT) ? "lit" : "key");
					EIL_CHECK((m1 != NULL) && (m1->tpage == tpage1) && ((m1->flags & ENGINE_MATERIAL_DOUBLE_SIDED) == 0u) && (vd->ranges[1].firstIndex == 3u) &&
					              (vd->ranges[1].indexCount == 6u) &&
					              ((writer == EIL_WRITER_LIT) ? ((m1->blend == (u8)Engine_BlendFromAbr(1)) && (m1->flags == ENGINE_MATERIAL_SEMI_STP))
					                                          : ((m1->blend == ENGINE_BLEND_OPAQUE) && (m1->flags == 0u))),
					          "%s variant range 1: blend %d tpage 0x%04x flags 0x%x", (writer == EIL_WRITER_LIT) ? "lit" : "key", m1 ? m1->blend : -1,
					          m1 ? m1->tpage : 0u, m1 ? m1->flags : 0u);
					EIL_CHECK((m2 != NULL) && (m2->blend == (u8)blend2) && (m2->tpage == tpage2) && (m2->flags == ENGINE_MATERIAL_SEMI_STP) &&
					              (vd->ranges[2].firstIndex == 9u) && (vd->ranges[2].indexCount == 6u),
					          "%s variant range 2: blend %d tpage 0x%04x flags 0x%x", (writer == EIL_WRITER_LIT) ? "lit" : "key", m2 ? m2->blend : -1,
					          m2 ? m2->tpage : 0u, m2 ? m2->flags : 0u);
				}

				// The live vertices: positions and texels of the source in the copy's
				// order, the flat colour of the side from corner 0's normal.
				memset(&l, 0, sizeof(l));
				l.light[2] = 3000.0f;
				l.half[2] = 4000;
				l.far[0] = 220.0f;
				l.far[1] = 166.0f;
				l.far[2] = 0.0f;
				Eil_Fill(&v, vertices, &l, live);
				for (t = 0; t < 5; t++)
				{
					u8 want[3];
					int c;

					Eil_LitColour(&l, vertices[wantSource[t * 3]].color, wantBack[t], want);
					for (c = 0; c < 3; c++)
					{
						const EngineVertex *o = &live[t * 3 + c];
						const EngineVertex *s = &vertices[wantSource[t * 3 + c]];

						EIL_CHECK((o->position[0] == s->position[0]) && (o->position[1] == s->position[1]) && (o->texcoord[0] == s->texcoord[0]) &&
						              (o->color[0] == want[0]) && (o->color[1] == want[1]) && (o->color[2] == want[2]) && (o->color[3] == 255),
						          "%s live triangle %d corner %d", (writer == EIL_WRITER_LIT) ? "lit" : "key", t, c);
					}
				}
				{
					u8 front[3];
					u8 backColour[3];

					Eil_LitColour(&l, vertices[3].color, 0, front);
					Eil_LitColour(&l, vertices[3].color, 1, backColour);
					EIL_CHECK((front[0] > backColour[0]) && (front[1] > backColour[1]), "the back side is darker (%u %u against %u %u)", front[0], front[1],
					          backColour[0], backColour[1]);
				}
				Engine_MeshDestroy(v.mesh);
				Eil_FreeVariant(&v);
			}
		}
		{
			EilVariant v;
			char why[128];
			static const u8 untexturedBits[4] = {0u, 0u, 0u, 0u};

			EIL_CHECK(!Eil_Build(srcMesh, untexturedBits, 3u, EIL_WRITER_LIT, &v, why, sizeof(why)), "bits of another triangle count refused");
		}
		if (srcMesh >= 0)
		{
			Engine_MeshDestroy(srcMesh);
		}
		Engine_MaterialsReset();
	}
	// The selftest's log lines (inexact masks, refusals) do not count against a run's.
	s_eilLogged = 0;
	return failures;
}
