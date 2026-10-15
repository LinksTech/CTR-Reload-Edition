#include <platform/engine.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ENGINE MODEL - retail (PS1 format) models to engine meshes (step 3). See
// include/platform/engine.h.
//
// The decoding is the retail twin's (platform/native_twin.c): NativeTwin_FromModel
// walks the command list of header 0 and decodes every pose of every
// animation (strip FIFO, fans, cached vertices, colour FIFO, winding of the
// one-sided triangles, half frames). This file only turns that source into an
// engine mesh:
//   - POSITIONS are the twin's model units: per axis g x scale / 16384, g the
//     GTE input value of the record ((pos + byte) x 4 for a full frame) and
//     scale {mh->scale.x, mh->scale.y, (u16)mh->scale.z}. The retail queue
//     applies the instance matrix (idpp->mvp) to g, so a draw with that
//     matrix has to multiply the positions by unitScale = 16384 / scale per
//     axis first (the layer's CharItemMatrix does exactly that).
//   - TEXCOORDS are page texels (u, v as the layout stores them, 0..255).
//   - COLOURS are the RGB of the colour table, alpha 255.
//   - Triangles keep the twin's winding: a one-sided one is written in the
//     order the engine's back-face cull keeps when retail keeps it; a
//     two-sided one is taken once (as listed) into a DOUBLE_SIDED material.
//   - One material per distinct (texture page + CLUT, semi-transparency,
//     two-sidedness); the twin's own material split (by 16-page texture) is
//     not used. A semi-transparent face is a textured one whose tpage word
//     has not abr 3 (retail packet code 0x36 against 0x34, RB:3116); its
//     material gets Engine_BlendFromAbr(abr) with ENGINE_MATERIAL_SEMI_STP.
//     The tpage word of the material is the layout's, with the abr bits of
//     the face (an opaque one: 3).
// Not followed: animated textures (a face keeps the layout of the build), and
// the full-height bit (NativeTwin fullHeight is 0: the odd height bit is
// dropped as for retail models).
//
// THE LIVE FRAME (round of 2026-10-15). A still model (no animations, an
// uncompressed frame) keeps, per mesh vertex, the vertex record it was decoded
// from (NativeTwinSource.cornerVertex). Engine_ModelLiveFrame rebuilds the
// mesh frame from the model's CURRENT data with it: position from the record's
// bytes in mh->ptrFrameData, colour from mh->ptrColors at the index the
// record's command names now. That is the state retail draws with for a model
// the game rewrites in place (see engine.h, Engine_ModelLiveFrame).

#define MODEL_ANIMS_MAX NATIVE_TWIN_ANIMS_MAX
#define MODEL_KEYS_MAX 256
// The x/y mask of a full frame without the full-height bit (native_twin.c
// NATIVE_TWIN_MASK_XY, RB:2468-2480): the importer decodes with fullHeight 0.
#define MODEL_LIVE_MASK_XY 0xfff8ffffu

// The frame table of an imported model, found by its mesh index.
typedef struct
{
	int used;
	int animCount;
	int frameTotal;
	int base[MODEL_ANIMS_MAX];
	int frames[MODEL_ANIMS_MAX];
	// A still model: the vertex record of every mesh vertex and the records per
	// frame; NULL and 0 for an animated one.
	u16 *record;
	u32 recordCount;
	u32 streamRecords;
} ModelFrameTable;

global_variable ModelFrameTable s_modelTables[ENGINE_MESHES_MAX];
// Engine_ModelLiveFrame: the colour index of every record of the current list.
global_variable u8 s_modelLiveColour[NATIVE_TWIN_STREAM_MAX];

// Why the last Engine_ModelImport failed ("" after a success), for the callers'
// log lines (the track's per-model report).
global_variable char s_modelWhy[256];

const char *Engine_ModelImportWhy(void)
{
	return s_modelWhy;
}

// What distinguishes one engine material of a model.
typedef struct
{
	int tile; // twin tile, -1 untextured
	int semi; // NATIVE_TWIN_SEMI_*
	int doubleSided;
} ModelKey;

internal float Model_ReadF32(const u8 *at)
{
	float f;

	memcpy(&f, at, sizeof(f));
	return f;
}

// The texel of a UV the twin wrote for a tile: (cell x 256 + texel) / edge.
internal u32 Model_TexelOf(float uv, u32 edge)
{
	return (u32)((double)uv * (double)edge + 0.5);
}

// The tile of a textured twin triangle t, from the UV of its first corner.
internal int Model_TileOf(const struct NativeTwinSource *src, u32 t, int material)
{
	const int texture = (s16)Rld_ReadLE16(&src->native.materials[((size_t)material * RLDCHAR_NET_MATERIAL_BYTES) + 4u]);
	const u8 *uv = &src->native.uv[(size_t)t * 3u * 8u];
	u32 k;
	u32 cellX;
	u32 cellY;

	if ((texture < 0) || ((u32)texture >= src->native.textureCount))
	{
		return -1;
	}
	cellX = Model_TexelOf(Model_ReadF32(uv), src->native.texture[texture].width) / (u32)NATIVE_TWIN_TILE_EDGE;
	cellY = Model_TexelOf(Model_ReadF32(uv + 4), src->native.texture[texture].height) / (u32)NATIVE_TWIN_TILE_EDGE;
	for (k = 0; k < src->tileCount; k++)
	{
		if ((src->tile[k].texture == texture) && (src->tile[k].cellX == cellX) && (src->tile[k].cellY == cellY))
		{
			return (int)k;
		}
	}
	return -1;
}

int Engine_ModelImport(const struct Model *model, const char *name, EngineModelImport *out)
{
	struct NativeTwinSource src;
	char why[192];
	ModelKey keys[MODEL_KEYS_MAX];
	int keyCount = 0;
	int *triKey = NULL;
	u32 *order = NULL;
	EngineVertex *vertices = NULL;
	u16 *indices = NULL;
	EngineRange *ranges = NULL;
	EngineMaterial *materials = NULL;
	u16 *record = NULL;
	u32 frames = 0;
	u32 perFrame = 0;
	u32 keptTris = 0;
	u32 t;
	u32 f;
	u32 a;
	int k;
	int firstMaterial = -1;
	int ok = 0;

	s_modelWhy[0] = '\0';
	if (out == NULL)
	{
		snprintf(s_modelWhy, sizeof(s_modelWhy), "no output");
		return 0;
	}
	memset(out, 0, sizeof(*out));
	out->mesh = -1;
	if ((model == NULL) || (model->numHeaders < 1) || (model->headers == NULL))
	{
		snprintf(s_modelWhy, sizeof(s_modelWhy), "no model header");
		return 0;
	}
	// A failed NativeTwin_FromModel leaves src freed and zeroed.
	if (!NativeTwin_FromModel(model, 0, &src, why, sizeof(why)))
	{
		snprintf(s_modelWhy, sizeof(s_modelWhy), "decoder: %s", why);
		return 0;
	}

	frames = (src.animCount == 0u) ? 1u : 0u;
	for (a = 0; a < src.animCount; a++)
	{
		frames += src.animFrames[a];
	}
	triKey = (int *)calloc(src.native.triangleCount, sizeof(int));
	order = (u32 *)calloc(src.native.triangleCount, sizeof(u32));
	if ((frames == 0u) || (triKey == NULL) || (order == NULL))
	{
		snprintf(s_modelWhy, sizeof(s_modelWhy), "%s", (frames == 0u) ? "no frame" : "out of memory");
		goto done;
	}

	// The kept triangles and their material keys, in order of first use. The
	// second copy of a two-sided triangle (triangleSide 2) is dropped.
	for (t = 0; t < src.native.triangleCount; t++)
	{
		const int m = (s16)Rld_ReadLE16(&src.native.triangleMaterials[(size_t)t * 2u]);
		ModelKey key;

		triKey[t] = -1;
		if (src.triangleSide[t] == 2u)
		{
			continue;
		}
		key.tile = src.material[m].textured ? Model_TileOf(&src, t, m) : -1;
		key.semi = src.material[m].textured ? src.material[m].semi : NATIVE_TWIN_SEMI_NONE;
		key.doubleSided = (src.triangleSide[t] == 1u) ? 1 : 0;
		if (src.material[m].textured && (key.tile < 0))
		{
			snprintf(s_modelWhy, sizeof(s_modelWhy), "textured triangle %u without a tile", (unsigned)t);
			goto done;
		}
		for (k = 0; k < keyCount; k++)
		{
			if ((keys[k].tile == key.tile) && (keys[k].semi == key.semi) && (keys[k].doubleSided == key.doubleSided))
			{
				break;
			}
		}
		if (k == keyCount)
		{
			if (keyCount >= MODEL_KEYS_MAX)
			{
				snprintf(s_modelWhy, sizeof(s_modelWhy), "more than %d materials", MODEL_KEYS_MAX);
				goto done;
			}
			keys[keyCount++] = key;
		}
		triKey[t] = k;
		keptTris++;
	}
	perFrame = keptTris * 3u;
	if ((keptTris == 0u) || (perFrame > 65535u) || ((u64)perFrame * frames > (1u << 20)))
	{
		snprintf(s_modelWhy, sizeof(s_modelWhy), "%u triangle(s) x %u frame(s) outside the mesh limits", (unsigned)keptTris, (unsigned)frames);
		goto done;
	}

	// Stable order by material.
	{
		u32 n = 0;

		for (k = 0; k < keyCount; k++)
		{
			for (t = 0; t < src.native.triangleCount; t++)
			{
				if (triKey[t] == k)
				{
					order[n++] = t;
				}
			}
		}
	}

	vertices = (EngineVertex *)calloc((size_t)perFrame * frames, sizeof(EngineVertex));
	indices = (u16 *)calloc(perFrame, sizeof(u16));
	ranges = (EngineRange *)calloc((size_t)keyCount, sizeof(EngineRange));
	materials = (EngineMaterial *)calloc((size_t)keyCount, sizeof(EngineMaterial));
	if ((vertices == NULL) || (indices == NULL) || (ranges == NULL) || (materials == NULL))
	{
		snprintf(s_modelWhy, sizeof(s_modelWhy), "out of memory");
		goto done;
	}
	// A still model with an uncompressed frame: the record of every mesh
	// vertex, for Engine_ModelLiveFrame (an allocation failure only leaves the
	// model without it).
	if ((src.animCount == 0u) && (model->headers[0].unk3 == 0u) && (src.streamVertices <= (u32)NATIVE_TWIN_STREAM_MAX))
	{
		record = (u16 *)calloc(perFrame, sizeof(u16));
		if (record != NULL)
		{
			for (t = 0; t < perFrame; t++)
			{
				record[t] = (u16)src.cornerVertex[(order[t / 3u] * 3u) + (t % 3u)];
			}
		}
	}

	for (f = 0; f < frames; f++)
	{
		for (t = 0; t < keptTris; t++)
		{
			const u32 tri = order[t];
			const int tile = keys[triKey[tri]].tile;
			int c;

			for (c = 0; c < 3; c++)
			{
				const u32 sv = (tri * 3u) + (u32)c; // twin vertex
				const u32 dv = (t * 3u) + (u32)c;
				EngineVertex *v = &vertices[((size_t)f * perFrame) + dv];
				const u8 *pos = &src.native.poses[(((size_t)f * src.native.vertexCount) + sv) * RLDCHAR_NET_VERTEX_BYTES];

				v->position[0] = Model_ReadF32(pos);
				v->position[1] = Model_ReadF32(pos + 4);
				v->position[2] = Model_ReadF32(pos + 8);
				if (tile >= 0)
				{
					const u8 *uv = &src.native.uv[(size_t)sv * 8u];
					const u32 tex = src.tile[tile].texture;

					v->texcoord[0] = (float)(Model_TexelOf(Model_ReadF32(uv), src.native.texture[tex].width) - ((u32)src.tile[tile].cellX * (u32)NATIVE_TWIN_TILE_EDGE));
					v->texcoord[1] = (float)(Model_TexelOf(Model_ReadF32(uv + 4), src.native.texture[tex].height) - ((u32)src.tile[tile].cellY * (u32)NATIVE_TWIN_TILE_EDGE));
				}
				memcpy(v->color, &src.native.colors[(size_t)sv * 4u], 4);
				indices[dv] = (u16)dv;
			}
		}
	}

	{
		u32 at = 0;

		for (k = 0; k < keyCount; k++)
		{
			ranges[k].firstIndex = at * 3u;
			while ((at < keptTris) && (triKey[order[at]] == k))
			{
				at++;
			}
			ranges[k].indexCount = (at * 3u) - ranges[k].firstIndex;
		}
	}

	// The materials. They can not be unregistered: a failed import after this
	// point leaves them registered and unused.
	for (k = 0; k < keyCount; k++)
	{
		EngineMaterial *m = &materials[k];
		const int semi = (keys[k].semi != NATIVE_TWIN_SEMI_NONE);
		int index;

		m->tint[0] = m->tint[1] = m->tint[2] = m->tint[3] = 1.0f;
		m->flags = (u16)((keys[k].doubleSided ? ENGINE_MATERIAL_DOUBLE_SIDED : 0u) | (semi ? ENGINE_MATERIAL_SEMI_STP : 0u));
		m->blend = (u8)ENGINE_BLEND_OPAQUE;
		if (keys[k].tile >= 0)
		{
			const struct NativeTwinTile *tile = &src.tile[keys[k].tile];
			const u32 abr = semi ? (u32)(keys[k].semi - NATIVE_TWIN_SEMI_MODE0) : 3u;

			m->texSource = ENGINE_TEX_VRAM;
			m->tpage = (u16)((tile->tpage & ~0x60u) | (abr << 5));
			m->clut = tile->clut;
			if (semi)
			{
				m->blend = (u8)Engine_BlendFromAbr((int)abr);
			}
		}
		else
		{
			m->texSource = ENGINE_TEX_NONE;
		}
		index = Engine_MaterialRegister(m);
		if ((index < 0) || ((k > 0) && (index != (firstMaterial + k))))
		{
			snprintf(s_modelWhy, sizeof(s_modelWhy), "%s", (index < 0) ? "material table full" : "materials not consecutive");
			goto done;
		}
		if (k == 0)
		{
			firstMaterial = index;
		}
		ranges[k].material = (u16)index;
	}

	{
		EngineMeshDesc desc;
		ModelFrameTable *table;
		int mesh;
		int base = 0;

		memset(&desc, 0, sizeof(desc));
		desc.vertices = vertices;
		desc.verticesPerFrame = perFrame;
		desc.frames = frames;
		desc.indices = indices;
		desc.indexCount = perFrame;
		desc.ranges = ranges;
		desc.rangeCount = (u32)keyCount;
		desc.name = (name != NULL) ? name : "model";
		mesh = Engine_MeshCreate(&desc);
		if (mesh < 0)
		{
			snprintf(s_modelWhy, sizeof(s_modelWhy), "mesh table full or mesh refused");
			goto done;
		}

		table = &s_modelTables[mesh];
		// The slot of a mesh of an earlier level: its records go with it.
		free(table->record);
		memset(table, 0, sizeof(*table));
		table->used = 1;
		table->animCount = (int)src.animCount;
		table->frameTotal = (int)frames;
		for (a = 0; a < src.animCount; a++)
		{
			table->base[a] = base;
			table->frames[a] = (int)src.animFrames[a];
			base += (int)src.animFrames[a];
		}
		if (record != NULL)
		{
			table->record = record;
			table->recordCount = perFrame;
			table->streamRecords = src.streamVertices;
			record = NULL;
		}
		out->mesh = mesh;
		out->firstMaterial = firstMaterial;
		out->materialCount = keyCount;
		out->headerCount = 1;
		out->animCount = (int)src.animCount;
		out->frameTotal = (int)frames;
	}
	ok = 1;

done:
	free(triKey);
	free(order);
	free(vertices);
	free(indices);
	free(ranges);
	free(materials);
	free(record);
	NativeTwin_Free(&src);
	if (!ok)
	{
		memset(out, 0, sizeof(*out));
		out->mesh = -1;
	}
	return ok;
}

// THE TILE LINES of a model (the track's per-model report): the twin's counts
// and, per texture page + CLUT, its triangles and the box of their page texels -
// the VRAM a draw of the import reads. Reads only.
void Engine_ModelLogTiles(const struct Model *model, const char *name)
{
	struct NativeTwinSource src;
	char why[192];
	u32 box[NATIVE_TWIN_TILES_MAX][4];
	u32 t;
	u32 k;

	if (!NativeTwin_FromModel(model, 0, &src, why, sizeof(why)))
	{
		Platform_Log("[CTR Engine] model '%s' tiles: decoder: %s\n", name, why);
		return;
	}
	Platform_Log("[CTR Engine] model '%s' tiles: %u, events textured %u untextured %u semi %u, animtex layouts %u (outside %u) triangles %u\n", name,
	             (unsigned)src.tileCount, (unsigned)src.count.textured, (unsigned)src.count.untextured, (unsigned)src.count.semi,
	             (unsigned)src.count.animatedLayouts, (unsigned)src.count.animatedOutside, (unsigned)src.count.animatedTriangles);
	for (k = 0; k < src.tileCount; k++)
	{
		box[k][0] = box[k][1] = 255u;
		box[k][2] = box[k][3] = 0u;
	}
	for (t = 0; t < src.native.triangleCount; t++)
	{
		const int m = (s16)Rld_ReadLE16(&src.native.triangleMaterials[(size_t)t * 2u]);
		const int tile = src.material[m].textured ? Model_TileOf(&src, t, m) : -1;
		u32 c;

		if (tile < 0)
		{
			continue;
		}
		for (c = 0; c < 3u; c++)
		{
			const u8 *uv = &src.native.uv[(((size_t)t * 3u) + c) * 8u];
			const u32 tex = src.tile[tile].texture;
			const u32 u = Model_TexelOf(Model_ReadF32(uv), src.native.texture[tex].width) - ((u32)src.tile[tile].cellX * (u32)NATIVE_TWIN_TILE_EDGE);
			const u32 v = Model_TexelOf(Model_ReadF32(uv + 4), src.native.texture[tex].height) - ((u32)src.tile[tile].cellY * (u32)NATIVE_TWIN_TILE_EDGE);

			box[tile][0] = (u < box[tile][0]) ? u : box[tile][0];
			box[tile][1] = (v < box[tile][1]) ? v : box[tile][1];
			box[tile][2] = (u > box[tile][2]) ? u : box[tile][2];
			box[tile][3] = (v > box[tile][3]) ? v : box[tile][3];
		}
	}
	for (k = 0; k < src.tileCount; k++)
	{
		const struct NativeTwinTile *tl = &src.tile[k];

		Platform_Log("[CTR Engine] model '%s' tile %u: tpage 0x%04x clut 0x%04x depth %u, page %u,%u clut at %u,%u, triangles %u, texels u %u-%u v %u-%u\n", name,
		             (unsigned)k, (unsigned)tl->tpage, (unsigned)tl->clut, (unsigned)tl->depth, (unsigned)tl->pageX, (unsigned)tl->pageY, (unsigned)tl->clutX,
		             (unsigned)tl->clutY, (unsigned)tl->triangles, (unsigned)box[k][0], (unsigned)box[k][2], (unsigned)box[k][1], (unsigned)box[k][3]);
	}
	NativeTwin_Free(&src);
}

int Engine_ModelFrame(const EngineModelImport *imp, int anim, int frame)
{
	const ModelFrameTable *table;
	const EngineMeshDesc *desc;

	if ((imp == NULL) || (imp->mesh < 0) || (imp->mesh >= ENGINE_MESHES_MAX) || (frame < 0))
	{
		return -1;
	}
	table = &s_modelTables[imp->mesh];
	desc = Engine_MeshDesc(imp->mesh);
	if (!table->used || (desc == NULL) || (desc->frames != (u32)table->frameTotal) || (table->animCount != imp->animCount))
	{
		return -1;
	}
	if (table->animCount == 0)
	{
		return ((anim == 0) && (frame == 0)) ? 0 : -1;
	}
	if ((anim < 0) || (anim >= table->animCount) || (frame >= table->frames[anim]))
	{
		return -1;
	}
	return table->base[anim] + frame;
}

internal int Model_S16(u32 value)
{
	value &= 0xffffu;
	return (value >= 0x8000u) ? ((int)value - 0x10000) : (int)value;
}

u32 Engine_ModelLiveFrame(const EngineModelImport *imp, const struct Model *model, EngineVertex *out, u32 outMax)
{
	const ModelFrameTable *table;
	const EngineMeshDesc *desc;
	const struct ModelHeader *mh;
	const struct ModelFrame *mf;
	const u32 *list;
	const u8 *bytes;
	double unit[3];
	u32 origin;
	u32 records = 0;
	u32 index;
	u32 v;

	if ((imp == NULL) || (imp->mesh < 0) || (imp->mesh >= ENGINE_MESHES_MAX) || (out == NULL) || (model == NULL) || (model->numHeaders < 1) ||
	    (model->headers == NULL))
	{
		return 0;
	}
	table = &s_modelTables[imp->mesh];
	desc = Engine_MeshDesc(imp->mesh);
	mh = &model->headers[0];
	if (!table->used || (table->record == NULL) || (desc == NULL) || (desc->vertices == NULL) || (desc->frames != 1u) ||
	    (desc->verticesPerFrame != table->recordCount) || (table->recordCount > outMax) || (mh->ptrAnimations != NULL) || (mh->ptrFrameData == NULL) ||
	    (mh->unk3 != 0u) || (mh->ptrColors == NULL) || (mh->ptrCommandList == 0u))
	{
		return 0;
	}

	// The colour index of every record as the list names it NOW: the command
	// that decodes the record (bit 26 clear), byte offset (command >> 7) & 0x1fc
	// (RB:2568-2579). A cached command (bit 26) takes the colour of its record.
	// Colour-only commands (upper half 0) are not followed. A list whose record
	// count is not the one of the import is refused.
	list = (const u32 *)(uintptr_t)mh->ptrCommandList;
	for (index = 1; (index <= (u32)NATIVE_TWIN_COMMANDS_MAX) && (list[index] != 0xffffffffu); index++)
	{
		const u32 command = list[index];

		if (((command >> 16) == 0u) || ((command & 0x04000000u) != 0u))
		{
			continue;
		}
		if (records >= table->streamRecords)
		{
			return 0;
		}
		s_modelLiveColour[records++] = (u8)((command >> 9) & 0x7fu);
	}
	if (records != table->streamRecords)
	{
		return 0;
	}

	// The GTE input of a record of a full frame, as the importer's decoder
	// takes it (native_twin.c NativeTwin_PackFull), in model units.
	mf = mh->ptrFrameData;
	bytes = (const u8 *)mf + mf->vertexOffset;
	origin = (u32)(u16)(mf->pos.x & 0x7fff) | ((u32)(u16)mf->pos.y << 16);
	unit[0] = (double)mh->scale.x / 16384.0;
	unit[1] = (double)mh->scale.y / 16384.0;
	unit[2] = (double)(u16)mh->scale.z / 16384.0;
	for (v = 0; v < table->recordCount; v++)
	{
		const u32 r = table->record[v];
		const u8 *b = &bytes[(size_t)r * 3u];
		const u32 word = ((((u32)b[0] | ((u32)b[2] << 16)) + origin) << 2) & MODEL_LIVE_MASK_XY;
		const u32 colour = mh->ptrColors[s_modelLiveColour[r]];
		EngineVertex *o = &out[v];

		*o = desc->vertices[v];
		o->position[0] = (float)((double)Model_S16(word) * unit[0]);
		o->position[1] = (float)((double)Model_S16(word >> 16) * unit[1]);
		o->position[2] = (float)((double)Model_S16((u32)((int)mf->pos.z + (int)b[1]) << 2) * unit[2]);
		o->color[0] = (u8)colour;
		o->color[1] = (u8)(colour >> 8);
		o->color[2] = (u8)(colour >> 16);
		o->color[3] = 255;
	}
	return table->recordCount;
}

// --------------------------------------------------------------------------
// SELFTEST: a made-up model in memory (no disc data): 1 header, 2 animations of
// 2 and 3 raw frames, 4 triangles of 3 records each.
//   tri 0: textured, layout 1, tpage A (abr 3 = opaque) clut X, one-sided, bit 29 clear (listed reversed)
//   tri 1: textured, layout 2, tpage A clut X, one-sided with bit 29 (listed as is)
//   tri 2: textured, layout 3, tpage B (abr 1) clut Y, two-sided
//   tri 3: untextured, one-sided, reversed
// Record r of pose f (global number) of animation a: bytes x = 10 + 5r (+ f for
// r = 0), y = 3 + 4r, z = 8 + 2r; origin (100a, 0, 10a); scale (4096, 4096,
// 8192) so the position is (x + ox, z + oy, 2 (oz + y)).

#define MODEL_TEST_TRIS 4
#define MODEL_TEST_RECORDS 12
#define MODEL_TEST_TPAGE_A 0x0062u
#define MODEL_TEST_CLUT_X 0x7814u
#define MODEL_TEST_TPAGE_B 0x00A5u
#define MODEL_TEST_CLUT_Y 0x7D00u

#define MODEL_CHECK(cond, ...)                                                                                                                       \
	do                                                                                                                                           \
	{                                                                                                                                            \
		(*checks)++;                                                                                                                         \
		if (!(cond))                                                                                                                         \
		{                                                                                                                                    \
			failures++;                                                                                                                  \
			printf("engine selftest: model: " __VA_ARGS__);                                                                              \
			printf("\n");                                                                                                                \
		}                                                                                                                                    \
	} while (0)

typedef struct
{
	struct Model model;
	struct ModelHeader header;
	struct ModelAnim *anims[2];
	u8 *animBytes[2];
	struct TextureLayout layouts[3];
	struct TextureLayout *layoutTable[3];
	u32 commands[1 + MODEL_TEST_RECORDS + 1];
	u32 colours[MODEL_TEST_RECORDS];
} ModelTest;

internal u32 ModelTest_Colour(int r)
{
	return (u32)(r * 10 + 1) | ((u32)(r * 20 + 2) << 8) | ((u32)(200 - r) << 16);
}

internal void ModelTest_Layout(struct TextureLayout *l, int u0, int v0, int u1, int v1, int u2, int v2, u16 clut, u16 tpage)
{
	l->u0 = (u8)u0;
	l->v0 = (u8)v0;
	l->clut = clut;
	l->u1 = (u8)u1;
	l->v1 = (u8)v1;
	l->tpage = tpage;
	l->u2 = (u8)u2;
	l->v2 = (u8)v2;
	l->u3 = 0;
	l->v3 = 0;
}

internal int ModelTest_Build(ModelTest *m)
{
	static const int frameCount[2] = {2, 3};
	static const u32 firstFlags[MODEL_TEST_TRIS] = {0x90u, 0xB0u, 0x80u, 0x90u};
	const u32 frameSize = (u32)sizeof(struct ModelFrame) + (MODEL_TEST_RECORDS * 3u);
	int r;
	int a;
	int f;
	int pose = 0;

	memset(m, 0, sizeof(*m));
	memcpy(m->model.name, "model-test", 11);
	m->model.numHeaders = 1;
	m->model.headers = &m->header;
	memcpy(m->header.name, "model-test_hi", 14);
	m->header.scale.x = 4096;
	m->header.scale.y = 4096;
	m->header.scale.z = 8192;

	for (r = 0; r < MODEL_TEST_RECORDS; r++)
	{
		m->colours[r] = ModelTest_Colour(r);
	}
	m->commands[0] = MODEL_TEST_RECORDS;
	for (r = 0; r < MODEL_TEST_RECORDS; r++)
	{
		const int tri = r / 3;
		const u32 flags = ((r % 3) == 0) ? firstFlags[tri] : 0x10u;
		const u32 tex = (tri < 3) ? (u32)(tri + 1) : 0u;

		m->commands[1 + r] = (flags << 24) | ((u32)r << 16) | ((u32)r << 9) | tex;
	}
	m->commands[1 + MODEL_TEST_RECORDS] = 0xffffffffu;

	ModelTest_Layout(&m->layouts[0], 10, 20, 40, 22, 25, 60, MODEL_TEST_CLUT_X, MODEL_TEST_TPAGE_A);
	ModelTest_Layout(&m->layouts[1], 0, 0, 255, 10, 128, 255, MODEL_TEST_CLUT_X, MODEL_TEST_TPAGE_A);
	ModelTest_Layout(&m->layouts[2], 5, 5, 200, 30, 100, 250, MODEL_TEST_CLUT_Y, MODEL_TEST_TPAGE_B);
	for (r = 0; r < 3; r++)
	{
		m->layoutTable[r] = &m->layouts[r];
	}

	for (a = 0; a < 2; a++)
	{
		m->animBytes[a] = (u8 *)calloc(1, sizeof(struct ModelAnim) + ((size_t)frameCount[a] * frameSize) + 4u);
		if (m->animBytes[a] == NULL)
		{
			return 0;
		}
		m->anims[a] = (struct ModelAnim *)m->animBytes[a];
		memcpy(m->anims[a]->name, a ? "second" : "first", a ? 7u : 6u);
		m->anims[a]->numFrames = (u16)frameCount[a];
		m->anims[a]->frameSize = (s16)frameSize;
		m->anims[a]->ptrDeltaArray = NULL;
		for (f = 0; f < frameCount[a]; f++)
		{
			struct ModelFrame *mf = (struct ModelFrame *)(m->animBytes[a] + sizeof(struct ModelAnim) + ((u32)f * frameSize));
			u8 *bytes = (u8 *)mf + sizeof(struct ModelFrame);

			mf->pos.x = (s16)(100 * a);
			mf->pos.y = 0;
			mf->pos.z = (s16)(10 * a);
			mf->vertexOffset = (int)sizeof(struct ModelFrame);
			for (r = 0; r < MODEL_TEST_RECORDS; r++)
			{
				bytes[(r * 3) + 0] = (u8)(10 + (5 * r) + ((r == 0) ? pose : 0));
				bytes[(r * 3) + 1] = (u8)(3 + (4 * r));
				bytes[(r * 3) + 2] = (u8)(8 + (2 * r));
			}
			pose++;
		}
	}

	m->header.ptrCommandList = (u32)(uintptr_t)m->commands;
	m->header.ptrFrameData = NULL;
	m->header.ptrTexLayout = m->layoutTable;
	m->header.ptrColors = m->colours;
	m->header.numAnimations = 2;
	m->header.ptrAnimations = m->anims;
	return 1;
}

int Engine_SelfTestModel(int *checks)
{
	static const int frameAnim[5] = {0, 0, 1, 1, 1};
	// Corner c of kept triangle t is record recordOf[t][c] (tri 0 and 3 reversed).
	static const int recordOf[MODEL_TEST_TRIS][3] = {{0, 2, 1}, {3, 4, 5}, {6, 7, 8}, {9, 11, 10}};
	// The texels of the layouts in the order the corners are listed.
	static const int texelOf[3][3][2] = {{{10, 20}, {25, 60}, {40, 22}}, {{0, 0}, {255, 10}, {128, 255}}, {{5, 5}, {200, 30}, {100, 250}}};
	int failures = 0;
	ModelTest *m = (ModelTest *)malloc(sizeof(ModelTest));
	EngineModelImport imp;
	EngineModelImport none;
	struct Model empty;
	const EngineMeshDesc *d;

	if (m == NULL)
	{
		MODEL_CHECK(0, "no memory");
		return failures;
	}
	memset(&imp, 0, sizeof(imp));
	imp.mesh = 7;
	MODEL_CHECK(!Engine_ModelImport(NULL, "x", &imp) && (imp.mesh == -1), "NULL model refused");
	MODEL_CHECK(!Engine_ModelImport(&m->model, "x", NULL), "NULL out refused");
	memset(&empty, 0, sizeof(empty));
	MODEL_CHECK(!Engine_ModelImport(&empty, "x", &imp) && (imp.mesh == -1), "model without headers refused");

	if (!ModelTest_Build(m))
	{
		MODEL_CHECK(0, "test model not built");
		free(m->animBytes[0]);
		free(m->animBytes[1]);
		free(m);
		return failures;
	}

	memset(&imp, 0, sizeof(imp));
	if (!Engine_ModelImport(&m->model, "model-test", &imp))
	{
		MODEL_CHECK(0, "import failed");
	}
	else
	{
		int tri;
		int c;
		int fi;

		MODEL_CHECK((imp.mesh >= 0) && (imp.headerCount == 1) && (imp.animCount == 2) && (imp.frameTotal == 5) && (imp.materialCount == 3),
		            "import result mesh %d headers %d anims %d frames %d materials %d", imp.mesh, imp.headerCount, imp.animCount, imp.frameTotal,
		            imp.materialCount);
		d = Engine_MeshDesc(imp.mesh);
		MODEL_CHECK((d != NULL) && (d->frames == 5) && (d->verticesPerFrame == 12) && (d->indexCount == 12) && (d->rangeCount == 3),
		            "mesh shape frames %u vertices %u indices %u ranges %u", d ? d->frames : 0u, d ? d->verticesPerFrame : 0u, d ? d->indexCount : 0u,
		            d ? d->rangeCount : 0u);
		if ((d != NULL) && (d->frames == 5) && (d->verticesPerFrame == 12) && (d->rangeCount == 3))
		{
			const EngineMaterial *mat[3];
			int k;

			for (k = 0; k < 3; k++)
			{
				mat[k] = Engine_Material(imp.firstMaterial + k);
			}
			MODEL_CHECK((mat[0] != NULL) && (mat[1] != NULL) && (mat[2] != NULL), "materials registered");
			if ((mat[0] != NULL) && (mat[1] != NULL) && (mat[2] != NULL))
			{
				MODEL_CHECK((mat[0]->texSource == ENGINE_TEX_VRAM) && (mat[0]->blend == ENGINE_BLEND_OPAQUE) && (mat[0]->tpage == MODEL_TEST_TPAGE_A) &&
				                (mat[0]->clut == MODEL_TEST_CLUT_X) && (mat[0]->flags == 0u),
				            "material 0 (opaque page A) source %d blend %d tpage 0x%x clut 0x%x flags 0x%x", mat[0]->texSource, mat[0]->blend, mat[0]->tpage,
				            mat[0]->clut, mat[0]->flags);
				MODEL_CHECK((mat[1]->texSource == ENGINE_TEX_VRAM) && (mat[1]->blend == ENGINE_BLEND_PSX_ADD) && (mat[1]->tpage == MODEL_TEST_TPAGE_B) &&
				                (mat[1]->clut == MODEL_TEST_CLUT_Y) && (mat[1]->flags == (ENGINE_MATERIAL_SEMI_STP | ENGINE_MATERIAL_DOUBLE_SIDED)),
				            "material 1 (semi page B) source %d blend %d tpage 0x%x clut 0x%x flags 0x%x", mat[1]->texSource, mat[1]->blend, mat[1]->tpage,
				            mat[1]->clut, mat[1]->flags);
				MODEL_CHECK((mat[2]->texSource == ENGINE_TEX_NONE) && (mat[2]->blend == ENGINE_BLEND_OPAQUE) && (mat[2]->flags == 0u),
				            "material 2 (untextured) source %d blend %d flags 0x%x", mat[2]->texSource, mat[2]->blend, mat[2]->flags);
			}
			MODEL_CHECK((d->ranges[0].material == imp.firstMaterial) && (d->ranges[0].firstIndex == 0) && (d->ranges[0].indexCount == 6) &&
			                (d->ranges[1].material == imp.firstMaterial + 1) && (d->ranges[1].firstIndex == 6) && (d->ranges[1].indexCount == 3) &&
			                (d->ranges[2].material == imp.firstMaterial + 2) && (d->ranges[2].firstIndex == 9) && (d->ranges[2].indexCount == 3),
			            "ranges by material");

			for (fi = 0; fi < 5; fi++)
			{
				for (tri = 0; tri < MODEL_TEST_TRIS; tri++)
				{
					for (c = 0; c < 3; c++)
					{
						const EngineVertex *v = &d->vertices[((u32)fi * 12u) + ((u32)tri * 3u) + (u32)c];
						const int r = recordOf[tri][c];
						const int a = frameAnim[fi];
						const float ex = (float)(10 + (5 * r) + ((r == 0) ? fi : 0) + (100 * a));
						const float ey = (float)(8 + (2 * r));
						const float ez = (float)(2 * ((10 * a) + 3 + (4 * r)));

						MODEL_CHECK((v->position[0] == ex) && (v->position[1] == ey) && (v->position[2] == ez),
						            "frame %d tri %d corner %d position %g %g %g, wanted %g %g %g", fi, tri, c, v->position[0], v->position[1],
						            v->position[2], ex, ey, ez);
					}
				}
			}
			for (tri = 0; tri < MODEL_TEST_TRIS; tri++)
			{
				for (c = 0; c < 3; c++)
				{
					const EngineVertex *v = &d->vertices[((u32)tri * 3u) + (u32)c];
					const u32 col = ModelTest_Colour(recordOf[tri][c]);
					const int ou = (tri < 3) ? texelOf[tri][c][0] : 0;
					const int ov = (tri < 3) ? texelOf[tri][c][1] : 0;

					MODEL_CHECK((v->texcoord[0] == (float)ou) && (v->texcoord[1] == (float)ov), "tri %d corner %d texel %g %g, wanted %d %d", tri, c,
					            v->texcoord[0], v->texcoord[1], ou, ov);
					MODEL_CHECK((v->color[0] == (u8)col) && (v->color[1] == (u8)(col >> 8)) && (v->color[2] == (u8)(col >> 16)) && (v->color[3] == 255),
					            "tri %d corner %d colour %u %u %u %u", tri, c, v->color[0], v->color[1], v->color[2], v->color[3]);
				}
			}
			{
				int identity = 1;
				u32 i;

				for (i = 0; i < d->indexCount; i++)
				{
					identity = identity && (d->indices[i] == i);
				}
				MODEL_CHECK(identity, "indices run 0..11");
			}
		}

		MODEL_CHECK((Engine_ModelFrame(&imp, 0, 0) == 0) && (Engine_ModelFrame(&imp, 0, 1) == 1) && (Engine_ModelFrame(&imp, 1, 0) == 2) &&
		                (Engine_ModelFrame(&imp, 1, 2) == 4),
		            "Engine_ModelFrame in range");
		MODEL_CHECK((Engine_ModelFrame(&imp, 0, 2) == -1) && (Engine_ModelFrame(&imp, 1, 3) == -1) && (Engine_ModelFrame(&imp, 2, 0) == -1) &&
		                (Engine_ModelFrame(&imp, -1, 0) == -1) && (Engine_ModelFrame(&imp, 0, -1) == -1) && (Engine_ModelFrame(NULL, 0, 0) == -1),
		            "Engine_ModelFrame out of range");
		memset(&none, 0, sizeof(none));
		none.mesh = -1;
		MODEL_CHECK(Engine_ModelFrame(&none, 0, 0) == -1, "Engine_ModelFrame of a failed import");

		// THE LIVE FRAME. An animated model has none; the same model made still
		// (pose 0 as its frame) has one that equals the import while nothing
		// changed, and follows a rewrite of one height byte, the colour indices
		// of the list and the colour table the way the start banner's thread
		// rewrites them (game/231/RB_Banner.c).
		{
			EngineVertex live[MODEL_TEST_RECORDS];
			EngineModelImport still;
			int r;

			MODEL_CHECK(Engine_ModelLiveFrame(&imp, &m->model, live, MODEL_TEST_RECORDS) == 0u, "live frame of an animated model refused");
			m->header.numAnimations = 0;
			m->header.ptrAnimations = NULL;
			m->header.ptrFrameData = (struct ModelFrame *)(m->animBytes[0] + sizeof(struct ModelAnim));
			memset(&still, 0, sizeof(still));
			if (!Engine_ModelImport(&m->model, "model-test still", &still) || (Engine_MeshDesc(still.mesh) == NULL))
			{
				MODEL_CHECK(0, "still import failed");
			}
			else
			{
				const EngineMeshDesc *sd = Engine_MeshDesc(still.mesh);
				u8 *bytes = (u8 *)m->header.ptrFrameData + m->header.ptrFrameData->vertexOffset;

				MODEL_CHECK((still.animCount == 0) && (sd->frames == 1u) && (sd->verticesPerFrame == 12u), "still import shape");
				MODEL_CHECK(Engine_ModelLiveFrame(&still, &m->model, live, MODEL_TEST_RECORDS) == 12u, "live frame of the still model");
				MODEL_CHECK(memcmp(live, sd->vertices, sizeof(live)) == 0, "live frame of unchanged data equals the import");
				MODEL_CHECK(Engine_ModelLiveFrame(&still, &m->model, live, MODEL_TEST_RECORDS - 1) == 0u, "live frame larger than the room refused");

				bytes[(4 * 3) + 1] = 50; // record 4: height byte
				for (r = 0; r < MODEL_TEST_RECORDS; r++)
				{
					m->commands[1 + r] = (m->commands[1 + r] & 0xffff01ffu) | ((u32)(MODEL_TEST_RECORDS - 1 - r) << 9);
				}
				m->colours[0] = 0x00303132u;
				MODEL_CHECK(Engine_ModelLiveFrame(&still, &m->model, live, MODEL_TEST_RECORDS) == 12u, "live frame after the rewrite");
				for (tri = 0; tri < MODEL_TEST_TRIS; tri++)
				{
					for (c = 0; c < 3; c++)
					{
						const EngineVertex *v = &live[(tri * 3) + c];
						const EngineVertex *s = &sd->vertices[(tri * 3) + c];
						const int rec = recordOf[tri][c];
						const int ci = MODEL_TEST_RECORDS - 1 - rec;
						const u32 col = (ci == 0) ? 0x00303132u : ModelTest_Colour(ci);
						const float ez = (rec == 4) ? 100.0f : s->position[2];

						MODEL_CHECK((v->position[0] == s->position[0]) && (v->position[1] == s->position[1]) && (v->position[2] == ez) &&
						                (v->texcoord[0] == s->texcoord[0]) && (v->texcoord[1] == s->texcoord[1]),
						            "live tri %d corner %d position %g %g %g, wanted z %g", tri, c, v->position[0], v->position[1], v->position[2], ez);
						MODEL_CHECK((v->color[0] == (u8)col) && (v->color[1] == (u8)(col >> 8)) && (v->color[2] == (u8)(col >> 16)) && (v->color[3] == 255),
						            "live tri %d corner %d colour %u %u %u", tri, c, v->color[0], v->color[1], v->color[2]);
					}
				}
				m->commands[1 + MODEL_TEST_RECORDS - 1] = 0xffffffffu; // one record fewer than imported
				MODEL_CHECK(Engine_ModelLiveFrame(&still, &m->model, live, MODEL_TEST_RECORDS) == 0u, "live frame of a list with other records refused");
				Engine_MeshDestroy(still.mesh);
				MODEL_CHECK(Engine_ModelLiveFrame(&still, &m->model, live, MODEL_TEST_RECORDS) == 0u, "live frame after destroy");
			}
		}

		Engine_MeshDestroy(imp.mesh);
		MODEL_CHECK(Engine_ModelFrame(&imp, 0, 0) == -1, "Engine_ModelFrame after destroy");
	}

	free(m->animBytes[0]);
	free(m->animBytes[1]);
	free(m);
	// Model runs last in Engine_SelfTest: nothing after it reads the table.
	Engine_MaterialsReset();
	return failures;
}
