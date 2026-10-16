#include <platform/engine.h>

// ENGINE STATIC MODEL, THE ENGINE SIDE - see include/platform/engine.h, STATIC
// MODEL (round of 2026-10-16). The file side (glTF binary, pieces, images) is
// platform/engine_static_load.c.
//
// WHAT. --engine-static-model <file.glb> puts an external model into every race
// level as static geometry, drawn by the engine with the camera of each view.
// The positions of the file are world units in world axes (the axis and
// winding rules are the engine's: y up, right-handed, front faces counter-
// clockwise seen from outside - the glTF rules, so the file is taken as it
// is). With --engine-hide track the level's own geometry is owned and not
// drawn (platform/engine_track.c, HIDDEN TRACK): only the model shows, the game
// still drives on the level's collision.
//
// LOAD, ONCE PER RUN (the first race level, load stage 8, Engine_StaticLoad):
//   textures  one per (image, wrap) pair the materials use, through
//             Engine_TextureCreate as the hi-res driver makes its skin: RGBA8 in
//             the file's resolution (an edge that is not a power of two, or
//             below 16, scaled UP to the next one by the file side; never down),
//             the whole mip chain, trilinear with anisotropy;
//   pieces    the triangles split into pieces of at most
//             ENGINE_STATIC_PIECE_TRIANGLES (EngineStatic_BuildPieces); each
//             piece one vertex buffer of its own, all of them one shared u32
//             index buffer (the device has few index buffer slots,
//             NATIVE_VK_MAX_INDEX_BUFFERS); each piece keeps its box;
//   the log   one line with triangles, pieces, vertices, materials, textures
//             (made, missing, scaled up), mip levels, GPU memory and load time.
// The GPU buffers and textures stay for the run (host memory of this file holds
// only the small tables); the staging buffer is given back to its size before.
//
// PER LEVEL (every race level's stage 8): the materials are registered and each
// piece becomes an external mesh (Engine_MeshCreateExternal) - the renderer's
// level unload forgets both, never the buffers.
//
// DRAW (every frame, from Engine_FrameCollect, before the track): per view and
// piece one draw into OT cell ENG_STATIC_CELL (the track's opaque cell, after
// the sky's), clip matrix of the view, the piece's ranges in the file's
// material order; a piece whose box lies wholly outside the view's pyramid is
// left out (Engine_StaticBoxVisible). Materials: OPAQUE as they are, MASK with
// the file's cutoff, BLEND as MASK with cutoff 0.5 (no sorting; counted),
// double sided without culling.
//
// DRAW TIME ONLY: nothing of the game is read but the frame's cameras and the
// level's state flags; nothing is written.

char g_cfg_engineStaticModel[ENGINE_STATIC_PATH_MAX] = "";
int g_cfg_engineStaticLight = 0;

#define ENG_STATIC_CELL 0x3fe // the track's opaque cell (engine_track.c EGT_CELL_OPAQUE), after the sky's 0x3ff
#define ENG_STATIC_TEXTURES_MAX 512
#define ENG_STATIC_MISSING_LOGGED 8
#define ENG_STATIC_RANGES_MAX 1024 // per piece
// THE CORE BOX (round of 2026-10-16, after the first run with a model): the box
// the fly camera's ring is built over. The box of every vertex holds a sky
// sphere or other far scenery (in that run x -127531..133973, ten times the
// track), so the ring flew at the clamp, 32000 above it. The core box is the
// 1st to 99th percentile of the vertex positions on each axis, from a histogram
// of ENG_STATIC_HIST_BINS bins over the file's box, filled while the pieces go
// up: a few thousand far vertices against millions on the track move it by at
// most a bin.
#define ENG_STATIC_HIST_BINS 4096
#define ENG_STATIC_CORE_LOW 0.01
#define ENG_STATIC_CORE_HIGH 0.99

typedef struct
{
	int image;
	int wrapU;
	int wrapV;
	u32 texture; // TextureID, 0xffffffff = missing
} EngStaticTexture;

global_variable struct
{
	int tried;
	int ok;
	int reportOn;
	// Run-wide (GPU side)
	int materialCount;
	EngineStaticMaterial *materials;
	int *materialTexture; // per material: index into textures, -1 none
	int textureCount;
	EngStaticTexture textures[ENG_STATIC_TEXTURES_MAX];
	int pieceCount;
	u32 vb[ENGINE_STATIC_PIECES_MAX];
	u32 pieceVertices[ENGINE_STATIC_PIECES_MAX];
	u32 pieceTriangles[ENGINE_STATIC_PIECES_MAX];
	EngineRange *ranges[ENGINE_STATIC_PIECES_MAX];
	u32 rangeCount[ENGINE_STATIC_PIECES_MAX];
	float boxMin[ENGINE_STATIC_PIECES_MAX][3];
	float boxMax[ENGINE_STATIC_PIECES_MAX][3];
	u32 ib;
	float modelMin[3];
	float modelMax[3];
	float coreMin[3]; // THE CORE BOX
	float coreMax[3];
	u32 triangles;
	// Per level
	int levelReady;
	int levelId;
	const void *level1;
	int mesh[ENGINE_STATIC_PIECES_MAX];
	int *levelMaterial;
	// Counters (the exit line)
	u64 frames;
	u64 viewFrames;
	u64 trianglesDrawn;
	u32 trianglesMin;
	u32 trianglesMax;
	u64 piecesDrawn;
	u64 piecesCulled;
	u64 refused;
} s_engStatic;

// The piece builder's hand-over (EngineStatic_BuildPieces calls EngStatic_OnPiece).
typedef struct
{
	u32 *indices; // all pieces, piece-local indices
	u32 indexCap;
	u32 indexCount;
	u32 vertices;
	u64 vertexBytes;
	char why[96];
	// THE CORE BOX: the histogram's range (the file's box) and its vertex count.
	float histMin[3];
	float histScale[3]; // bins per world unit
	u64 histTotal;
} EngStaticBuild;

global_variable u32 s_engStaticHist[3][ENG_STATIC_HIST_BINS];

internal int EngStatic_OnPiece(void *user, const EngineStaticPiece *piece)
{
	EngStaticBuild *b = (EngStaticBuild *)user;
	const int p = s_engStatic.pieceCount;
	u32 r;

	if ((p >= ENGINE_STATIC_PIECES_MAX) || (piece->indexCount > (b->indexCap - b->indexCount)) || (piece->rangeCount == 0u) ||
	    (piece->rangeCount > ENG_STATIC_RANGES_MAX))
	{
		snprintf(b->why, sizeof(b->why), "piece %d does not fit the tables (%u ranges)", p, piece->rangeCount);
		return 0;
	}
	for (r = 0; r < piece->rangeCount; r++)
	{
		if ((int)piece->ranges[r].material >= s_engStatic.materialCount)
		{
			snprintf(b->why, sizeof(b->why), "piece %d names material %u of %d", p, (unsigned)piece->ranges[r].material, s_engStatic.materialCount);
			return 0;
		}
	}
	s_engStatic.vb[p] = Engine_RenderStaticVertexBuffer(piece->vertices, piece->vertexCount);
	if (s_engStatic.vb[p] == 0xffffffffu)
	{
		snprintf(b->why, sizeof(b->why), "the vertex buffer of piece %d (%u vertices) was refused", p, piece->vertexCount);
		return 0;
	}
	s_engStatic.ranges[p] = (EngineRange *)malloc((size_t)piece->rangeCount * sizeof(EngineRange));
	if (s_engStatic.ranges[p] == NULL)
	{
		Engine_RenderStaticBuffersFree(s_engStatic.vb[p], 0xffffffffu);
		snprintf(b->why, sizeof(b->why), "out of memory (ranges of piece %d)", p);
		return 0;
	}
	// The piece's indices go behind the ones before; its ranges move with them.
	memcpy(&b->indices[b->indexCount], piece->indices, (size_t)piece->indexCount * sizeof(u32));
	for (r = 0; r < piece->rangeCount; r++)
	{
		s_engStatic.ranges[p][r] = piece->ranges[r];
		s_engStatic.ranges[p][r].firstIndex += b->indexCount;
	}
	s_engStatic.rangeCount[p] = piece->rangeCount;
	s_engStatic.pieceVertices[p] = piece->vertexCount;
	s_engStatic.pieceTriangles[p] = piece->indexCount / 3u;
	memcpy(s_engStatic.boxMin[p], piece->boxMin, sizeof(piece->boxMin));
	memcpy(s_engStatic.boxMax[p], piece->boxMax, sizeof(piece->boxMax));
	b->indexCount += piece->indexCount;
	b->vertices += piece->vertexCount;
	b->vertexBytes += (u64)piece->vertexCount * sizeof(EngineVertex);
	for (r = 0; r < piece->vertexCount; r++)
	{
		int k;

		for (k = 0; k < 3; k++)
		{
			const float at = (piece->vertices[r].position[k] - b->histMin[k]) * b->histScale[k];
			const int bin = (at <= 0.0f) ? 0 : ((at >= (float)(ENG_STATIC_HIST_BINS - 1)) ? (ENG_STATIC_HIST_BINS - 1) : (int)at);

			s_engStaticHist[k][bin]++;
		}
	}
	b->histTotal += piece->vertexCount;
	s_engStatic.pieceCount = p + 1;
	return 1;
}

// The texture of a material's (image, wrap) pair: index into the table, a new
// entry on first use; -1 without an image or when the table is full.
internal int EngStatic_TextureSlot(const EngineStaticMaterial *m)
{
	int i;

	if (m->image < 0)
	{
		return -1;
	}
	for (i = 0; i < s_engStatic.textureCount; i++)
	{
		const EngStaticTexture *t = &s_engStatic.textures[i];

		if ((t->image == m->image) && (t->wrapU == m->wrapU) && (t->wrapV == m->wrapV))
		{
			return i;
		}
	}
	if (s_engStatic.textureCount >= ENG_STATIC_TEXTURES_MAX)
	{
		return -1;
	}
	i = s_engStatic.textureCount++;
	s_engStatic.textures[i].image = m->image;
	s_engStatic.textures[i].wrapU = m->wrapU;
	s_engStatic.textures[i].wrapV = m->wrapV;
	s_engStatic.textures[i].texture = 0xffffffffu;
	return i;
}

internal double EngStatic_Ms(u64 from)
{
	return (double)(SDL_GetPerformanceCounter() - from) * 1000.0 / (double)SDL_GetPerformanceFrequency();
}

internal void EngStatic_Report(void)
{
	const double avg = (s_engStatic.viewFrames > 0u) ? (double)s_engStatic.trianglesDrawn / (double)s_engStatic.viewFrames : 0.0;

	Platform_Log("[CTR Engine] static model at exit: frames %llu, view frames %llu, triangles drawn per view frame min %u avg %.0f max %u (model %u); "
	             "pieces drawn %llu, left out by the view test %llu; draws refused %llu\n",
	             (unsigned long long)s_engStatic.frames, (unsigned long long)s_engStatic.viewFrames,
	             (s_engStatic.viewFrames > 0u) ? s_engStatic.trianglesMin : 0u, avg, s_engStatic.trianglesMax, s_engStatic.triangles,
	             (unsigned long long)s_engStatic.piecesDrawn, (unsigned long long)s_engStatic.piecesCulled, (unsigned long long)s_engStatic.refused);
}

// The textures of the materials. Counts into the out values; a missing image is
// logged (the first ENG_STATIC_MISSING_LOGGED) and its materials stay untextured.
internal void EngStatic_MakeTextures(const EngineStaticModel *model, u32 *made, u32 *missing, u32 *scaled, u32 *levelsTotal, int *levelsMax,
                                     u64 *bytes)
{
	u32 missingLogged = 0;
	int i;

	for (i = 0; i < s_engStatic.textureCount; i++)
	{
		EngStaticTexture *t = &s_engStatic.textures[i];
		EngineStaticImage img;
		EngineTextureDesc desc;
		char imgWhy[96];
		char name[32];

		memset(&img, 0, sizeof(img));
		imgWhy[0] = 0;
		if (!EngineStatic_ReadImage(model, t->image, &img, imgWhy, (int)sizeof(imgWhy)))
		{
			(*missing)++;
			if (missingLogged < ENG_STATIC_MISSING_LOGGED)
			{
				missingLogged++;
				Platform_Log("[CTR Engine] static model: image %d missing: %s\n", t->image, imgWhy);
			}
			continue;
		}
		memset(&desc, 0, sizeof(desc));
		snprintf(name, sizeof(name), "static image %d", t->image);
		desc.width = img.width;
		desc.height = img.height;
		desc.rgba = img.rgba;
		desc.filter = ENGINE_TEXTURE_FILTERED;
		desc.maxAnisotropy = 0;
		desc.wrapU = t->wrapU;
		desc.wrapV = t->wrapV;
		desc.name = name;
		t->texture = Engine_TextureCreate(&desc);
		if (t->texture == 0xffffffffu)
		{
			(*missing)++;
			if (missingLogged < ENG_STATIC_MISSING_LOGGED)
			{
				missingLogged++;
				Platform_Log("[CTR Engine] static model: image %d (%dx%d) refused by the texture manager\n", t->image, img.width, img.height);
			}
		}
		else
		{
			const int levels = NativeTex_LevelCount(img.width, img.height);
			u64 w = (u64)img.width;
			u64 h = (u64)img.height;
			int level;

			(*made)++;
			*scaled += (img.resampled != 0) ? 1u : 0u;
			*levelsTotal += (u32)levels;
			*levelsMax = (levels > *levelsMax) ? levels : *levelsMax;
			for (level = 0; level < levels; level++)
			{
				*bytes += w * h * 4u;
				w = (w > 1u) ? (w / 2u) : 1u;
				h = (h > 1u) ? (h / 2u) : 1u;
			}
		}
		EngineStatic_FreeImage(&img);
	}
}

// THE CORE BOX from the histogram EngStatic_OnPiece filled: per axis the bins
// where the count passes ENG_STATIC_CORE_LOW and reaches ENG_STATIC_CORE_HIGH of
// the vertices, held inside the box of the pieces.
internal void EngStatic_CoreBox(const EngStaticBuild *b)
{
	int k;

	for (k = 0; k < 3; k++)
	{
		const double low = (double)b->histTotal * ENG_STATIC_CORE_LOW;
		const double high = (double)b->histTotal * ENG_STATIC_CORE_HIGH;
		u64 sum = 0;
		int lowBin = 0;
		int highBin = ENG_STATIC_HIST_BINS - 1;
		int lowFound = 0;
		int bin;

		for (bin = 0; bin < ENG_STATIC_HIST_BINS; bin++)
		{
			sum += s_engStaticHist[k][bin];
			if (!lowFound && ((double)sum > low))
			{
				lowBin = bin;
				lowFound = 1;
			}
			if ((double)sum >= high)
			{
				highBin = bin;
				break;
			}
		}
		if (b->histScale[k] > 0.0f)
		{
			s_engStatic.coreMin[k] = b->histMin[k] + (float)lowBin / b->histScale[k];
			s_engStatic.coreMax[k] = b->histMin[k] + (float)(highBin + 1) / b->histScale[k];
		}
		else
		{
			s_engStatic.coreMin[k] = s_engStatic.modelMin[k];
			s_engStatic.coreMax[k] = s_engStatic.modelMax[k];
		}
		s_engStatic.coreMin[k] = (s_engStatic.coreMin[k] < s_engStatic.modelMin[k]) ? s_engStatic.modelMin[k] : s_engStatic.coreMin[k];
		s_engStatic.coreMax[k] = (s_engStatic.coreMax[k] > s_engStatic.modelMax[k]) ? s_engStatic.modelMax[k] : s_engStatic.coreMax[k];
	}
}

// The file, once per run: textures, pieces, buffers, the log line. Sets
// s_engStatic.ok; on failure one line with the reason (the model stays off for
// the run, nothing of its GPU side is kept).
internal void EngStatic_ReadOnce(void)
{
	EngineStaticSource src;
	EngineStaticModel model;
	EngStaticBuild build;
	char why[160];
	const u64 started = SDL_GetPerformanceCounter();
	const u32 stagingBefore = NativeGfx_StagingBytes();
	u32 texturesMade = 0;
	u32 texturesMissing = 0;
	u32 texturesScaled = 0;
	u32 levelsTotal = 0;
	int levelsMax = 0;
	u64 textureBytes = 0;
	u32 counts[3] = {0, 0, 0}; // opaque, mask, blend
	u32 doubleSided = 0;
	u32 untextured = 0;
	int pieces;
	int i;

	s_engStatic.tried = 1;
	memset(&src, 0, sizeof(src));
	memset(&model, 0, sizeof(model));
	memset(&build, 0, sizeof(build));
	why[0] = 0;

	if (!EngineStatic_SourceFile(g_cfg_engineStaticModel, &src))
	{
		Platform_Log("[CTR Engine] static model '%s' refused: the file does not open - the model is off for this run\n", g_cfg_engineStaticModel);
		return;
	}
	if (!EngineStatic_Open(&src, &model, why, (int)sizeof(why)))
	{
		Platform_Log("[CTR Engine] static model '%s' refused: %s - the model is off for this run\n", g_cfg_engineStaticModel, why);
		EngineStatic_SourceFileClose(&src);
		return;
	}

	// The materials and the textures they name.
	s_engStatic.materialCount = model.materialCount;
	s_engStatic.materials = (EngineStaticMaterial *)malloc((size_t)model.materialCount * sizeof(EngineStaticMaterial));
	s_engStatic.materialTexture = (int *)malloc((size_t)model.materialCount * sizeof(int));
	s_engStatic.levelMaterial = (int *)malloc((size_t)model.materialCount * sizeof(int));
	if ((model.materialCount < 1) || (s_engStatic.materials == NULL) || (s_engStatic.materialTexture == NULL) || (s_engStatic.levelMaterial == NULL))
	{
		snprintf(why, sizeof(why), "out of memory (material tables)");
		goto fail;
	}
	memcpy(s_engStatic.materials, model.materials, (size_t)model.materialCount * sizeof(EngineStaticMaterial));
	for (i = 0; i < model.materialCount; i++)
	{
		const EngineStaticMaterial *m = &s_engStatic.materials[i];

		s_engStatic.materialTexture[i] = EngStatic_TextureSlot(m);
		counts[((m->alphaMode >= 0) && (m->alphaMode <= 2)) ? m->alphaMode : 0]++;
		doubleSided += (m->doubleSided != 0) ? 1u : 0u;
		untextured += (m->image < 0) ? 1u : 0u;
	}
	EngStatic_MakeTextures(&model, &texturesMade, &texturesMissing, &texturesScaled, &levelsTotal, &levelsMax, &textureBytes);

	// The pieces: a vertex buffer each, one index buffer for all.
	if (model.triangles > (0xffffffffu / 3u))
	{
		snprintf(why, sizeof(why), "%u triangles are too many for one index buffer", model.triangles);
		goto fail;
	}
	build.indexCap = model.triangles * 3u;
	build.indices = (u32 *)malloc((size_t)build.indexCap * sizeof(u32));
	if (build.indices == NULL)
	{
		snprintf(why, sizeof(why), "out of memory (%u indices)", build.indexCap);
		goto fail;
	}
	memset(s_engStaticHist, 0, sizeof(s_engStaticHist));
	for (i = 0; i < 3; i++)
	{
		const float extent = model.boxMax[i] - model.boxMin[i];

		build.histMin[i] = model.boxMin[i];
		build.histScale[i] = (extent > 0.0f) ? ((float)ENG_STATIC_HIST_BINS / extent) : 0.0f;
	}
	pieces = EngineStatic_BuildPieces(&model, ENGINE_STATIC_PIECE_TRIANGLES, g_cfg_engineStaticLight, EngStatic_OnPiece, &build, why, (int)sizeof(why));
	if (pieces < 0)
	{
		if (build.why[0] != 0)
		{
			snprintf(why, sizeof(why), "%s", build.why);
		}
		goto fail;
	}
	if ((pieces != s_engStatic.pieceCount) || (build.indexCount != build.indexCap))
	{
		snprintf(why, sizeof(why), "the pieces hold %u indices, the file %u", build.indexCount, build.indexCap);
		goto fail;
	}
	s_engStatic.ib = Engine_RenderStaticIndexBuffer(build.indices, build.indexCount);
	if (s_engStatic.ib == 0xffffffffu)
	{
		snprintf(why, sizeof(why), "the index buffer (%u indices) was refused", build.indexCount);
		goto fail;
	}
	free(build.indices);
	build.indices = NULL;
	s_engStatic.triangles = model.triangles;
	for (i = 0; i < 3; i++)
	{
		s_engStatic.modelMin[i] = s_engStatic.boxMin[0][i];
		s_engStatic.modelMax[i] = s_engStatic.boxMax[0][i];
	}
	for (i = 1; i < s_engStatic.pieceCount; i++)
	{
		int k;

		for (k = 0; k < 3; k++)
		{
			s_engStatic.modelMin[k] = (s_engStatic.boxMin[i][k] < s_engStatic.modelMin[k]) ? s_engStatic.boxMin[i][k] : s_engStatic.modelMin[k];
			s_engStatic.modelMax[k] = (s_engStatic.boxMax[i][k] > s_engStatic.modelMax[k]) ? s_engStatic.boxMax[i][k] : s_engStatic.modelMax[k];
		}
	}
	EngStatic_CoreBox(&build);
	s_engStatic.ok = 1;
	{
		const u32 given = NativeGfx_ShrinkStaging(stagingBefore);

		Platform_Log("[CTR Engine] static model '%s': %u triangles in %d pieces, %u vertices; materials %d (opaque %u, mask %u, blend drawn as mask %u, "
		             "double sided %u, untextured %u); textures made %u of %d (missing %u, scaled up %u), mip levels %u (at most %d per texture); "
		             "GPU vertices %.1f MB, indices %.1f MB, textures %.1f MB; primitives %u (skipped %u), mirrored nodes %u, triangles without UV %u; "
		             "box x %.0f..%.0f y %.0f..%.0f z %.0f..%.0f (core x %.0f..%.0f y %.0f..%.0f z %.0f..%.0f); staging given back %u KB; load %.0f ms\n",
		             g_cfg_engineStaticModel, model.triangles, s_engStatic.pieceCount, build.vertices, model.materialCount, counts[0], counts[1], counts[2],
		             doubleSided, untextured, texturesMade, s_engStatic.textureCount, texturesMissing, texturesScaled, levelsTotal, levelsMax,
		             (double)build.vertexBytes / 1048576.0, (double)build.indexCount * 4.0 / 1048576.0, (double)textureBytes / 1048576.0,
		             model.primitives, model.skippedPrimitives, model.mirroredNodes, model.noUvTriangles, (double)s_engStatic.modelMin[0],
		             (double)s_engStatic.modelMax[0], (double)s_engStatic.modelMin[1], (double)s_engStatic.modelMax[1], (double)s_engStatic.modelMin[2],
		             (double)s_engStatic.modelMax[2], (double)s_engStatic.coreMin[0], (double)s_engStatic.coreMax[0], (double)s_engStatic.coreMin[1],
		             (double)s_engStatic.coreMax[1], (double)s_engStatic.coreMin[2], (double)s_engStatic.coreMax[2], given / 1024u, EngStatic_Ms(started));
	}
	EngineStatic_Close(&model);
	EngineStatic_SourceFileClose(&src);
	return;

fail:
	Platform_Log("[CTR Engine] static model '%s' refused: %s - the model is off for this run\n", g_cfg_engineStaticModel, why);
	free(build.indices);
	for (i = 0; i < s_engStatic.pieceCount; i++)
	{
		Engine_RenderStaticBuffersFree(s_engStatic.vb[i], 0xffffffffu);
		free(s_engStatic.ranges[i]);
		s_engStatic.ranges[i] = NULL;
	}
	s_engStatic.pieceCount = 0;
	for (i = 0; i < s_engStatic.textureCount; i++)
	{
		Engine_TextureDestroy(s_engStatic.textures[i].texture);
	}
	s_engStatic.textureCount = 0;
	(void)NativeGfx_ShrinkStaging(stagingBefore);
	EngineStatic_Close(&model);
	EngineStatic_SourceFileClose(&src);
}

// This level's materials and meshes for the run's buffers. 1 = ready.
internal int EngStatic_RegisterLevel(void)
{
	EngineRange ranges[ENG_STATIC_RANGES_MAX];
	int i;
	int p;

	// The materials (the table was reset at the last unload).
	for (i = 0; i < s_engStatic.materialCount; i++)
	{
		const EngineStaticMaterial *m = &s_engStatic.materials[i];
		const int slot = s_engStatic.materialTexture[i];
		EngineMaterial em;

		memset(&em, 0, sizeof(em));
		em.blend = (m->alphaMode == ENGINE_STATIC_ALPHA_OPAQUE) ? ENGINE_BLEND_OPAQUE : ENGINE_BLEND_MASK;
		em.alphaCutoff = (m->alphaMode == ENGINE_STATIC_ALPHA_MASK) ? m->alphaCutoff : ((m->alphaMode == ENGINE_STATIC_ALPHA_BLEND) ? 0.5f : 0.0f);
		memcpy(em.tint, m->baseColor, sizeof(em.tint));
		em.flags = (m->doubleSided != 0) ? (u16)ENGINE_MATERIAL_DOUBLE_SIDED : (u16)0u;
		em.program = ENGINE_PROGRAM_NE;
		if ((slot >= 0) && (s_engStatic.textures[slot].texture != 0xffffffffu))
		{
			Engine_TextureMaterial(&em, s_engStatic.textures[slot].texture);
		}
		s_engStatic.levelMaterial[i] = Engine_MaterialRegister(&em);
		if (s_engStatic.levelMaterial[i] < 0)
		{
			Platform_Log("[CTR Engine] static model: the material table is full at material %d - not drawn in this level\n", i);
			return 0;
		}
	}

	// The pieces as external meshes (the mesh copies the ranges).
	for (p = 0; p < s_engStatic.pieceCount; p++)
	{
		EngineExternalMeshDesc desc;
		u32 r;

		for (r = 0; r < s_engStatic.rangeCount[p]; r++)
		{
			ranges[r] = s_engStatic.ranges[p][r];
			ranges[r].material = (u16)s_engStatic.levelMaterial[s_engStatic.ranges[p][r].material];
		}
		memset(&desc, 0, sizeof(desc));
		desc.vertexBuffer = s_engStatic.vb[p];
		desc.indexBuffer = s_engStatic.ib;
		desc.verticesPerFrame = s_engStatic.pieceVertices[p];
		desc.frames = 1;
		desc.ranges = ranges;
		desc.rangeCount = s_engStatic.rangeCount[p];
		desc.name = "static model piece";
		s_engStatic.mesh[p] = Engine_MeshCreateExternal(&desc);
		if (s_engStatic.mesh[p] < 0)
		{
			Platform_Log("[CTR Engine] static model: the mesh table refused piece %d - not drawn in this level\n", p);
			return 0;
		}
	}
	return 1;
}

void Engine_StaticLoad(void)
{
	struct GameTracker *gGT = sdata->gGT;

	s_engStatic.levelReady = 0;
	if (!g_cfg_engine || (g_cfg_engineStaticModel[0] == '\0') || (gGT == NULL) || (gGT->level1 == NULL) || ((gGT->gameMode1 & MAIN_MENU) != 0) ||
	    (gGT->levelID == MAIN_MENU_LEVEL))
	{
		return;
	}
	if (!s_engStatic.reportOn)
	{
		s_engStatic.reportOn = 1;
		Platform_AtExitReport(EngStatic_Report);
	}
	if (!s_engStatic.tried)
	{
		EngStatic_ReadOnce();
	}
	if (!s_engStatic.ok || !EngStatic_RegisterLevel())
	{
		return;
	}
	s_engStatic.levelReady = 1;
	s_engStatic.levelId = gGT->levelID;
	s_engStatic.level1 = gGT->level1;
	Platform_Log("[CTR Engine] static model: level %d - %d materials, %d pieces registered\n", gGT->levelID, s_engStatic.materialCount,
	             s_engStatic.pieceCount);
}

int Engine_StaticBoxVisible(const EngineCamera *cam, const float boxMin[3], const float boxMax[3])
{
	// The view's pyramid as five planes in view space (x right, y down, z
	// ahead): screen x = offsetX + H x / z inside 0..rectW, the same for y, and
	// z at least the near plane. A box is left out only when all eight corners
	// lie outside one plane.
	int outside[5] = {0, 0, 0, 0, 0};
	int c;

	if ((cam == NULL) || !cam->valid)
	{
		return 0;
	}
	for (c = 0; c < 8; c++)
	{
		const EngineVec3 world = {(c & 1) ? boxMax[0] : boxMin[0], (c & 2) ? boxMax[1] : boxMin[1], (c & 4) ? boxMax[2] : boxMin[2]};
		const EngineVec3 v = Engine_Mat4MulPoint(&cam->viewFromWorld, world);

		outside[0] += ((cam->H * v.x + cam->offsetX * v.z) < 0.0f) ? 1 : 0;
		outside[1] += ((((float)cam->rectW - cam->offsetX) * v.z - cam->H * v.x) < 0.0f) ? 1 : 0;
		outside[2] += ((cam->H * v.y + cam->offsetY * v.z) < 0.0f) ? 1 : 0;
		outside[3] += ((((float)cam->rectH - cam->offsetY) * v.z - cam->H * v.y) < 0.0f) ? 1 : 0;
		outside[4] += (v.z < cam->zNear) ? 1 : 0;
	}
	for (c = 0; c < 5; c++)
	{
		if (outside[c] == 8)
		{
			return 0;
		}
	}
	return 1;
}

void Engine_StaticCollect(struct GameTracker *gGT)
{
	int views;
	int view;

	if (!g_cfg_engine || !s_engStatic.levelReady || (gGT == NULL) || ((gGT->gameMode1 & MAIN_MENU) != 0) || ((const void *)gGT->level1 != s_engStatic.level1) ||
	    ((int)gGT->levelID != s_engStatic.levelId))
	{
		return;
	}
	s_engStatic.frames++;
	views = gGT->numPlyrCurrGame;
	views = (views > ENGINE_VIEWS) ? ENGINE_VIEWS : views;
	for (view = 0; view < views; view++)
	{
		const EngineCamera *cam = Engine_FrameCamera(view);
		EngineMat4 screen;
		u32 triangles = 0;
		int p;

		if (cam == NULL)
		{
			continue;
		}
		Engine_CameraScreenFromWorld(cam, &screen);
		for (p = 0; p < s_engStatic.pieceCount; p++)
		{
			EngineDraw draw;

			if (!Engine_StaticBoxVisible(cam, s_engStatic.boxMin[p], s_engStatic.boxMax[p]))
			{
				s_engStatic.piecesCulled++;
				continue;
			}
			memset(&draw, 0, sizeof(draw));
			draw.mesh = s_engStatic.mesh[p];
			draw.frame = 0;
			draw.clipFromModel = screen; // the model is in world units
			draw.tint[0] = draw.tint[1] = draw.tint[2] = draw.tint[3] = 1.0f;
			draw.materialOverride = -1;
			draw.otCell = ENG_STATIC_CELL;
			draw.dynMaterial = -1;
			if (Engine_RenderSubmitLinked(view, &draw, gGT->pushBuffer[view].ptrOT, ENG_STATIC_CELL))
			{
				s_engStatic.piecesDrawn++;
				triangles += s_engStatic.pieceTriangles[p];
			}
			else
			{
				s_engStatic.refused++;
			}
		}
		if ((s_engStatic.viewFrames == 0u) || (triangles < s_engStatic.trianglesMin))
		{
			s_engStatic.trianglesMin = triangles;
		}
		s_engStatic.trianglesMax = (triangles > s_engStatic.trianglesMax) ? triangles : s_engStatic.trianglesMax;
		s_engStatic.trianglesDrawn += triangles;
		s_engStatic.viewFrames++;
	}
}

int Engine_StaticBox(float boxMin[3], float boxMax[3])
{
	if (!s_engStatic.ok)
	{
		return 0;
	}
	memcpy(boxMin, s_engStatic.coreMin, sizeof(s_engStatic.coreMin));
	memcpy(boxMax, s_engStatic.coreMax, sizeof(s_engStatic.coreMax));
	return 1;
}

// --------------------------------------------------------------------------
// Selftest: the view test of the pieces on the camera of the game's 1P view.

internal void EngStatic_Check(int ok, const char *what, int *n, int *failures)
{
	(*n)++;
	if (!ok)
	{
		(*failures)++;
		printf("engine selftest: static view test %s\n", what);
	}
}

int Engine_SelfTestStatic(int *checks)
{
	static struct PushBuffer pb;
	EngineCamera cam;
	RECT16 rect;
	int distance = 0;
	int failures = 0;
	int n = 0;

	memset(&pb, 0, sizeof(pb));
	if (!CTR_Canvas_Viewport(512, 216, 0, 1, &rect, &distance))
	{
		EngStatic_Check(0, "no 1P viewport", &n, &failures);
		*checks += n;
		return failures;
	}
	pb.rect = rect;
	pb.distanceToScreen_PREV = distance;
	// Identity view: the camera at the origin looks along +z (view space = world space).
	pb.matrix_ViewProj.m[0][0] = 0x1000;
	pb.matrix_ViewProj.m[1][1] = 0x1000;
	pb.matrix_ViewProj.m[2][2] = 0x1000;
	EngStatic_Check(Engine_CameraFromPushBuffer(&pb, 0, &cam) == 1, "camera", &n, &failures);
	{
		static const float boxes[][2][3] = {
		    {{-50.0f, -50.0f, 900.0f}, {50.0f, 50.0f, 1100.0f}},          // ahead
		    {{-50.0f, -50.0f, -1100.0f}, {50.0f, 50.0f, -900.0f}},        // behind
		    {{-20000.0f, -50.0f, 900.0f}, {-19000.0f, 50.0f, 1100.0f}},   // far left
		    {{19000.0f, -50.0f, 900.0f}, {20000.0f, 50.0f, 1100.0f}},     // far right
		    {{-50.0f, 19000.0f, 900.0f}, {50.0f, 20000.0f, 1100.0f}},     // far below the view
		    {{-50.0f, -20000.0f, 900.0f}, {50.0f, -19000.0f, 1100.0f}},   // far above the view
		    {{-5000.0f, -5000.0f, -5000.0f}, {5000.0f, 5000.0f, 5000.0f}}, // around the camera
		    {{-10.0f, -10.0f, 1.0f}, {10.0f, 10.0f, 2.0f}},               // before the near plane
		    {{-20000.0f, -50.0f, 900.0f}, {0.0f, 50.0f, 1100.0f}},        // across the left edge
		};
		static const int seen[] = {1, 0, 0, 0, 0, 0, 1, 0, 1};
		static const char *what[] = {"a box ahead",           "a box behind",      "a box far left",
		                             "a box far right",       "a box far below",   "a box far above",
		                             "a box around the camera", "a box before the near plane", "a box across the left edge"};
		int b;

		for (b = 0; b < (int)(sizeof(seen) / sizeof(seen[0])); b++)
		{
			EngStatic_Check(Engine_StaticBoxVisible(&cam, boxes[b][0], boxes[b][1]) == seen[b], what[b], &n, &failures);
		}
		EngStatic_Check(Engine_StaticBoxVisible(NULL, boxes[0][0], boxes[0][1]) == 0, "no camera", &n, &failures);
	}
	// Every point the projection puts on the screen lies in a box that is seen:
	// small boxes around projected points of a turned and moved camera.
	{
		u32 seed = 0x5eedu;
		int i;
		int missed = 0;
		int used = 0;
		SVec3 rot;
		MATRIX gm;

		rot.x = 0x123;
		rot.y = 0x456;
		rot.z = 0x078;
		ConvertRotToMatrix(&gm, &rot);
		pb.matrix_ViewProj = gm;
		pb.matrix_ViewProj.t[0] = 300;
		pb.matrix_ViewProj.t[1] = -200;
		pb.matrix_ViewProj.t[2] = 100;
		(void)Engine_CameraFromPushBuffer(&pb, 0, &cam);
		for (i = 0; i < 4000; i++)
		{
			EngineVec3 world;
			EngineVec3 screenPoint;
			float bmin[3];
			float bmax[3];

			seed = seed * 1664525u + 1013904223u;
			world.x = (float)((int)((seed >> 8) % 8001u) - 4000);
			seed = seed * 1664525u + 1013904223u;
			world.y = (float)((int)((seed >> 8) % 8001u) - 4000);
			seed = seed * 1664525u + 1013904223u;
			world.z = (float)((int)((seed >> 8) % 8001u) - 4000);
			if (!Engine_ProjectPoint(&cam, world, &screenPoint) || (screenPoint.x < 1.0f) || (screenPoint.y < 1.0f) ||
			    (screenPoint.x > (float)cam.rectW - 1.0f) || (screenPoint.y > (float)cam.rectH - 1.0f))
			{
				continue;
			}
			bmin[0] = world.x - 1.0f;
			bmin[1] = world.y - 1.0f;
			bmin[2] = world.z - 1.0f;
			bmax[0] = world.x + 1.0f;
			bmax[1] = world.y + 1.0f;
			bmax[2] = world.z + 1.0f;
			used++;
			missed += Engine_StaticBoxVisible(&cam, bmin, bmax) ? 0 : 1;
		}
		EngStatic_Check(used >= 50, "too few points on the screen", &n, &failures);
		EngStatic_Check(missed == 0, "a box around a point on the screen is left out", &n, &failures);
	}

	*checks += n;
	return failures;
}
