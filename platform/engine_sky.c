#include <platform/engine.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ENGINE SKY - the sky of a level (--engine-track, round of 2026-10-15). See
// include/platform/engine.h, SKY.
//
// WHAT THE PS1 DRAWS (all into OT cell 0x3ff of the view, before everything
// else of the view):
//   - the clear split (CAM_ClearScreen, game/CAM.c): a top and a bottom colour,
//     split at a line from the camera pitch, or the draw environment's
//     background colour (clearColorRGBA, MainInit.c);
//   - the glow (CAM_SkyboxGlow, game/CAM.c; 1P only when configFlags & 1):
//     up to three colour bands a number of screen pixels above the horizon,
//     plus a fill above the first band (clearColor[2]);
//   - the dome (DrawSky_Full, game/DrawSky.c): Gouraud triangles with vertex
//     colours, no texture, both sides, rotated with the camera and never moved
//     (the GTE translation is zeroed), four of eight segments per frame. A face
//     with one corner behind the camera is dropped whole. Each face carries a
//     byte offset D into the ordering table: Dingo Canyon and Tiger Temple have
//     faces at D = -4 and -8, drawn over the D = 0 dome;
//   - the stars (RenderStars, game/RenderStars.c; Oxide Station, Tiny Arena):
//     one-pixel tiles in directions from a local random sequence.
//
// WHAT THE ENGINE DRAWS. The same parts as the PC version wants them, in view
// 0, one player, linked into OT cell 0x3ff from Engine_FrameCollect, which runs
// before the PS1 code links its sky - so the engine's markers are drawn last in
// that cell, after the PS1 draw environment, and before every other cell:
//   1. the backdrop: a sphere around the camera whose colour is a function of
//      the elevation only - top colour above the TRUE horizon, bottom colour
//      below it, the glow bands at the elevation of their pixel heights at the
//      view's distance to the screen. It covers the whole view, so the PS1
//      clear and glow (still drawn, earlier in the cell) never show;
//   2. the dome: one static mesh of all eight segments, built at load; the
//      GPU clips at the near plane, so there are no holes;
//   3. the stars: one quad of one PSX pixel per star, built every frame from
//      gGT->stars with the generator RenderStars uses.
// All three draw without depth test and without depth write, in this order.
// That they lie behind everything is the ORDER (first in the view's first
// cell) and the missing depth write: every later engine draw tests against the
// depth buffer the view's first engine draw cleared, every PS1 primitive comes
// later in the ordering table. The sky has no far depth of its own.
//
// DRAW TIME ONLY. The sky reads the level's skybox, the colours of the level
// and gGT->stars; it writes nothing the game reads. DrawSky_Full and
// RenderStars skip their primitives when the engine drew them
// (Engine_SkyOwnsView, Engine_SkyOwnsStars); the GTE setup and the scratch
// words of DrawSky_Full run as always.
//
// --engine-hide sky: the engine still owns the sky (the PS1 dome and stars are
// skipped) but draws the backdrop in one flat key colour and neither dome nor
// stars. The run with it (E-) gives the measurement the exact pixels where the
// sky is visible: every pixel in the key colour.

#define ESK_CELL 0x3ff                // the PS1 sky cell, drawn first of the view
#define ESK_BACKDROP_RADIUS 4096.0f    // any radius: the sky has no depth
#define ESK_BACKDROP_SEGMENTS 32       // around the camera
#define ESK_BACKDROP_INTERVALS 8       // elevation intervals: 2 base + 3 glow + 1 cap, with room
#define ESK_BACKDROP_VERTICES (ESK_BACKDROP_INTERVALS * (ESK_BACKDROP_SEGMENTS + 1) * 2)
#define ESK_BACKDROP_INDICES (ESK_BACKDROP_INTERVALS * ESK_BACKDROP_SEGMENTS * 6)
#define ESK_STARS_MAX 2048             // stars per frame (retail: 301)
#define ESK_KEY_R 255                  // --engine-hide sky: the key colour
#define ESK_KEY_G 0
#define ESK_KEY_B 255
#define ESK_PI 3.14159265358979323846f

// --------------------------------------------------------------------------
// Pure data side (no game state): the part the selftest checks.

typedef struct
{
	int faces;     // faces in the mesh
	int bad;       // faces skipped: a vertex offset not a multiple of 12 or out of range
	int layer0;    // D = 0
	int layer1;    // D = -4
	int layer2;    // D = -8
	int layerMore; // any other negative D
	int clamped;   // positive D, drawn with the D = 0 faces
} EngSkyBuildStats;

// One face of the dome with its layer and its place in the file.
typedef struct
{
	s16 d;
	u16 a, b, c; // vertex indices
	int order;   // file order: segment 0 first
} EngSkyFace;

internal int EngSky_FaceCompare(const void *pa, const void *pb)
{
	const EngSkyFace *x = (const EngSkyFace *)pa;
	const EngSkyFace *y = (const EngSkyFace *)pb;

	// Layers: the D = 0 cell is drawn first, then -4, then -8 (a lower cell is
	// drawn later). Inside a layer the PS1 draws the face linked LAST first.
	if (x->d != y->d)
	{
		return (x->d > y->d) ? -1 : 1;
	}
	return (x->order > y->order) ? -1 : ((x->order < y->order) ? 1 : 0);
}

// The skybox as a triangle list in drawing order. vertices: numVertex entries;
// indices: 3 per face of all eight segments. Returns the face count written, 0
// when the skybox is unusable (stats->bad tells why when faces exist).
internal int EngSky_BuildDome(const struct Skybox *sky, EngineVertex *vertices, u16 *indices, int maxFaces, EngSkyBuildStats *stats)
{
	EngSkyFace *list;
	int total = 0;
	int count = 0;
	int seg;
	int i;

	memset(stats, 0, sizeof(*stats));
	if ((sky == NULL) || (sky->ptrVertex == NULL) || (sky->numVertex <= 0) || (sky->numVertex > 65535))
	{
		return 0;
	}
	for (seg = 0; seg < NUM_SKYBOX_SEGMENTS; seg++)
	{
		if ((sky->numFaces[seg] > 0) && (sky->ptrFaces[seg] != NULL))
		{
			total += sky->numFaces[seg];
		}
	}
	if ((total <= 0) || (total > maxFaces))
	{
		return 0;
	}
	list = (EngSkyFace *)malloc((size_t)total * sizeof(EngSkyFace));
	if (list == NULL)
	{
		return 0;
	}

	for (i = 0; i < sky->numVertex; i++)
	{
		const struct ShortVertex *v = &sky->ptrVertex[i];

		vertices[i].position[0] = (float)v->Position.vx;
		vertices[i].position[1] = (float)v->Position.vy;
		vertices[i].position[2] = (float)v->Position.vz;
		vertices[i].texcoord[0] = 0.0f;
		vertices[i].texcoord[1] = 0.0f;
		vertices[i].color[0] = v->Color.r;
		vertices[i].color[1] = v->Color.g;
		vertices[i].color[2] = v->Color.b;
		vertices[i].color[3] = 255;
	}

	for (seg = 0; seg < NUM_SKYBOX_SEGMENTS; seg++)
	{
		const struct SkyboxFace *face = sky->ptrFaces[seg];
		int n = sky->numFaces[seg];

		if ((n <= 0) || (face == NULL))
		{
			continue;
		}
		for (i = 0; i < n; i++, face++)
		{
			const u32 off[3] = {face->A, face->B, face->C};
			EngSkyFace *f = &list[count];
			int ok = 1;
			int k;

			// A, B and C are byte offsets into the vertex array (12 bytes a vertex).
			for (k = 0; k < 3; k++)
			{
				if (((off[k] % sizeof(struct ShortVertex)) != 0u) || ((off[k] / sizeof(struct ShortVertex)) >= (u32)sky->numVertex))
				{
					ok = 0;
				}
			}
			if (!ok)
			{
				stats->bad++;
				continue;
			}
			f->a = (u16)(off[0] / sizeof(struct ShortVertex));
			f->b = (u16)(off[1] / sizeof(struct ShortVertex));
			f->c = (u16)(off[2] / sizeof(struct ShortVertex));
			f->d = (s16)face->D;
			f->order = count;
			if (f->d > 0)
			{
				stats->clamped++;
				f->d = 0;
			}
			if (f->d == 0)
			{
				stats->layer0++;
			}
			else if (f->d == -4)
			{
				stats->layer1++;
			}
			else if (f->d == -8)
			{
				stats->layer2++;
			}
			else
			{
				stats->layerMore++;
			}
			count++;
		}
	}

	qsort(list, (size_t)count, sizeof(EngSkyFace), EngSky_FaceCompare);
	for (i = 0; i < count; i++)
	{
		indices[i * 3 + 0] = list[i].a;
		indices[i * 3 + 1] = list[i].b;
		indices[i * 3 + 2] = list[i].c;
	}
	free(list);
	stats->faces = count;
	return count;
}

// THE STARS, as RenderStars (game/RenderStars.c) makes them: the same sequence
// of the same generator, so every star is where the PS1 puts it. Data, not
// drawing: the directions and the grey of each star.
internal u32 EngSky_RngSelfOr(u32 *state0, u32 *state1)
{
	u32 shifted1 = *state1 >> 8;
	u32 mixed = *state0 + shifted1;

	*state0 = (*state0 >> 8) | mixed;
	mixed += *state0 >> 8;
	mixed <<= 24;
	*state1 = shifted1 | mixed;
	return mixed;
}

internal u32 EngSky_RngWithOr(u32 *state0, u32 *state1, u32 seedOr)
{
	u32 shifted1 = *state1 >> 8;
	u32 mixed = *state0 + shifted1;

	*state0 = (*state0 >> 8) | seedOr;
	mixed += *state0 >> 8;
	mixed <<= 24;
	*state1 = shifted1 | mixed;
	return mixed;
}

internal s32 EngSky_MulLowShift(s32 value, s32 scale)
{
	return (s32)(u32)((s64)value * (s64)scale) >> 12;
}

// Writes up to max stars (position as the GTE gets it, grey 0x40..0xff).
// Returns the count; retail makes numStars + 1 of them.
internal int EngSky_StarPoints(const struct Stars *stars, s16 (*pos)[3], u8 *grey, int max)
{
	s32 starIndex = (u16)stars->numStars;
	const int spread = (u16)stars->spread;
	const int seedShift = (u16)stars->seed + 20;
	u32 state0 = 0x30125400;
	u32 state1 = 0x493583fe;
	int count = 0;

	while ((starIndex >= 0) && (count < max))
	{
		u32 seedX = EngSky_RngSelfOr(&state0, &state1);
		u32 seedY = EngSky_RngWithOr(&state0, &state1, seedX);
		u32 seedZ = EngSky_RngWithOr(&state0, &state1, seedX);
		s32 starX = (s32)seedX >> 20;
		s32 starY = (s32)seedY >> (seedShift & 0x1f);
		s32 starZ = (s32)seedZ >> 20;
		s32 absX = (starX < 0) ? -starX : starX;
		s32 absY = starY;
		s32 absZ = (starZ < 0) ? -starZ : starZ;
		s32 denominator;
		s32 scale;
		u32 packedXY;
		u32 colorSeed;

		if (absY < 0)
		{
			absY = -absY;
			if (spread == 0)
			{
				starY = absY;
			}
		}
		if (absX - absY <= 0)
		{
			s32 t = absX;
			absX = absY;
			absY = t;
		}
		if (absX - absZ <= 0)
		{
			s32 t = absX;
			absX = absZ;
			absZ = t;
		}
		denominator = absX + (absY >> 2) + (absZ >> 2);
		// All three zero: the MIPS divide gives no trap and no meaningful star; skip it.
		scale = (denominator != 0) ? (0x01000000 / denominator) : 0;

		packedXY = (u32)(EngSky_MulLowShift(starX, scale) & 0xffff);
		packedXY |= (u32)EngSky_MulLowShift(starY, scale) << 16;
		colorSeed = EngSky_RngWithOr(&state0, &state1, packedXY);

		if (denominator != 0)
		{
			pos[count][0] = (s16)(packedXY & 0xffffu);
			pos[count][1] = (s16)(packedXY >> 16);
			pos[count][2] = (s16)EngSky_MulLowShift(starZ, scale);
			grey[count] = (u8)(((colorSeed >> 24) | 0x40) & 0xff);
			count++;
		}
		starIndex--;
	}
	return count;
}

// THE BACKDROP: colour as a function of the elevation. An interval [lo, hi]
// (radians, -pi/2..pi/2) is drawn as one ring strip with colour cLo at lo and
// cHi at hi; later intervals are drawn over earlier ones.
typedef struct
{
	float lo, hi;
	u8 cLo[3], cHi[3];
} EngSkyBand;

typedef struct
{
	EngSkyBand band[ESK_BACKDROP_INTERVALS];
	int count;
} EngSkyBackdrop;

// What the backdrop of a level is made of (from struct Level).
typedef struct
{
	u8 top[3], bottom[3];  // the clear split (or the background colour)
	int glow;              // configFlags & 1
	s16 pointFrom[3], pointTo[3];
	u8 colorFrom[3][3], colorTo[3][3];
	int fillAbove;         // clearColor[2].enable: fill above glow 0's "from" line
	u8 fill[3];
} EngSkyBackdropIn;

internal void EngSky_Rgb(u32 word, u8 out[3])
{
	out[0] = (u8)(word & 0xffu);
	out[1] = (u8)((word >> 8) & 0xffu);
	out[2] = (u8)((word >> 16) & 0xffu);
}

internal void EngSky_AddBand(EngSkyBackdrop *bd, float lo, float hi, const u8 cLo[3], const u8 cHi[3])
{
	EngSkyBand *b;

	if ((bd->count >= ESK_BACKDROP_INTERVALS) || !(hi > lo))
	{
		return;
	}
	b = &bd->band[bd->count++];
	b->lo = lo;
	b->hi = hi;
	memcpy(b->cLo, cLo, 3);
	memcpy(b->cHi, cHi, 3);
}

// The elevation of a point p PSX pixels above the horizon at distance H.
internal float EngSky_Elevation(float pixels, float H)
{
	return atanf(pixels / H);
}

// The bands in drawing order, as the PS1 orders them in cell 0x3ff: the clear
// split first, then glow 2, glow 1, the fill above glow 0, glow 0 (the glow
// links entry 0 first, and a cell draws its last link first).
internal void EngSky_BackdropBands(const EngSkyBackdropIn *in, float H, EngSkyBackdrop *bd)
{
	const float half = ESK_PI * 0.5f;
	int i;

	bd->count = 0;
	EngSky_AddBand(bd, -half, 0.0f, in->bottom, in->bottom);
	EngSky_AddBand(bd, 0.0f, half, in->top, in->top);
	if (!in->glow || !(H > 0.0f))
	{
		return;
	}
	for (i = 2; i >= 0; i--)
	{
		const float eFrom = EngSky_Elevation((float)in->pointFrom[i], H);
		const float eTo = EngSky_Elevation((float)in->pointTo[i], H);

		if ((i == 0) && in->fillAbove)
		{
			EngSky_AddBand(bd, eFrom, half, in->fill, in->fill);
		}
		if (eFrom > eTo)
		{
			EngSky_AddBand(bd, eTo, eFrom, in->colorTo[i], in->colorFrom[i]);
		}
		else if (eTo > eFrom)
		{
			EngSky_AddBand(bd, eFrom, eTo, in->colorFrom[i], in->colorTo[i]);
		}
	}
}

// The bands as triangles around the origin: one strip of ESK_BACKDROP_SEGMENTS
// quads per band. key != NULL paints everything in that colour. Returns the
// vertex count (indices: 6 per quad, *indexCount).
internal int EngSky_BackdropMesh(const EngSkyBackdrop *bd, const u8 *key, EngineVertex *v, u16 *idx, int *indexCount)
{
	int nv = 0;
	int ni = 0;
	int b;

	for (b = 0; b < bd->count; b++)
	{
		const EngSkyBand *band = &bd->band[b];
		const int base = nv;
		int s;

		for (s = 0; s <= ESK_BACKDROP_SEGMENTS; s++)
		{
			const float az = (2.0f * ESK_PI * (float)s) / (float)ESK_BACKDROP_SEGMENTS;
			int r;

			for (r = 0; r < 2; r++)
			{
				const float el = (r == 0) ? band->lo : band->hi;
				const u8 *c = (key != NULL) ? key : ((r == 0) ? band->cLo : band->cHi);
				EngineVertex *out = &v[nv++];

				// World y is up in the level data (the domes lie at y >= 0).
				out->position[0] = ESK_BACKDROP_RADIUS * cosf(el) * cosf(az);
				out->position[1] = ESK_BACKDROP_RADIUS * sinf(el);
				out->position[2] = ESK_BACKDROP_RADIUS * cosf(el) * sinf(az);
				out->texcoord[0] = 0.0f;
				out->texcoord[1] = 0.0f;
				out->color[0] = c[0];
				out->color[1] = c[1];
				out->color[2] = c[2];
				out->color[3] = 255;
			}
		}
		for (s = 0; s < ESK_BACKDROP_SEGMENTS; s++)
		{
			const u16 a0 = (u16)(base + s * 2);
			const u16 a1 = (u16)(a0 + 1);
			const u16 b0 = (u16)(a0 + 2);
			const u16 b1 = (u16)(a0 + 3);

			idx[ni++] = a0;
			idx[ni++] = b0;
			idx[ni++] = a1;
			idx[ni++] = a1;
			idx[ni++] = b0;
			idx[ni++] = b1;
		}
	}
	*indexCount = ni;
	return nv;
}

// The view matrix of the sky: the camera's rotation, no translation.
internal void EngSky_Camera(const EngineCamera *cam, EngineCamera *out)
{
	*out = *cam;
	out->viewFromWorld.m[12] = 0.0f;
	out->viewFromWorld.m[13] = 0.0f;
	out->viewFromWorld.m[14] = 0.0f;
}

// The draws of a frame are prepared in DRAWING order and linked in reverse:
// a cell draws its last link first. linked[i] = 1 when draw i was linked.
typedef int (*EngSkyLinkFn)(const EngineDraw *draw, void *user);

internal void EngSky_LinkInDrawingOrder(const EngineDraw *draws, int count, EngSkyLinkFn link, void *user, int *linked)
{
	int i;

	for (i = count - 1; i >= 0; i--)
	{
		linked[i] = link(&draws[i], user) ? 1 : 0;
	}
}

// --------------------------------------------------------------------------
// Level side.

global_variable const struct Level *s_eskLevel; // the level the dome was built for
global_variable int s_eskLevelID = -1;
global_variable int s_eskMesh = -1;             // dome mesh, -1 = none
global_variable u32 s_eskMeshVertices;
global_variable int s_eskMaterial = -1;
global_variable int s_eskReportRegistered;

// This frame.
global_variable int s_eskEligible;      // Engine_SkyCollect found the frame eligible
global_variable int s_eskDomeSubmitted; // the dome was linked this frame
global_variable int s_eskStarsPromised; // RenderStars was told the engine draws the stars

// Per level (printed at the next load and at exit).
typedef struct
{
	u64 frames;
	u64 backdrop;
	u64 dome;
	u64 ps1SkyKept;
	u64 starsPromised;
	u64 starsSubmitted;
	u64 refused;
	u64 hidden;
} EngSkyCounters;
global_variable EngSkyCounters s_eskCount;

// Per frame geometry (built in Engine_SkyCollect, copied by the scene ring at submit).
global_variable EngineVertex s_eskBackdropV[ESK_BACKDROP_VERTICES];
global_variable u16 s_eskBackdropI[ESK_BACKDROP_INDICES];
global_variable EngineVertex s_eskStarV[ESK_STARS_MAX * 4];
global_variable u16 s_eskStarI[ESK_STARS_MAX * 6];
global_variable s16 s_eskStarPos[ESK_STARS_MAX][3];
global_variable u8 s_eskStarGrey[ESK_STARS_MAX];

internal void EngSky_Summary(void)
{
	if (s_eskLevelID < 0)
	{
		return;
	}
	Platform_Log("[CTR Engine] sky: level %d summary: frames %llu, backdrop submitted %llu, dome submitted %llu, PS1 sky kept %llu, "
	             "stars promised %llu, stars submitted %llu, submit refused %llu, hidden %llu\n",
	             s_eskLevelID, (unsigned long long)s_eskCount.frames, (unsigned long long)s_eskCount.backdrop, (unsigned long long)s_eskCount.dome,
	             (unsigned long long)s_eskCount.ps1SkyKept, (unsigned long long)s_eskCount.starsPromised, (unsigned long long)s_eskCount.starsSubmitted,
	             (unsigned long long)s_eskCount.refused, (unsigned long long)s_eskCount.hidden);
}

internal void EngSky_Report(void)
{
	EngSky_Summary();
}

// Whether the dome made at load is still the live mesh (the mesh table is
// reset at level unload and slots are reused).
internal int EngSky_DomeLive(void)
{
	const EngineMeshDesc *d;
	u32 vb = 0xffffffffu;
	u32 ib = 0xffffffffu;

	if (s_eskMesh < 0)
	{
		return 0;
	}
	d = Engine_MeshDesc(s_eskMesh);
	if ((d == NULL) || (d->name == NULL) || (strncmp(d->name, "sky ", 4) != 0) || (d->verticesPerFrame != s_eskMeshVertices))
	{
		return 0;
	}
	Engine_MeshGetGpu(s_eskMesh, &vb, &ib);
	return (vb != 0xffffffffu) && (ib != 0xffffffffu);
}

// The PS1 conditions under which the level (and so the sky) is drawn
// (MainFrame_RenderFrame: RENDER_FLAG_DRAW_LEVEL and a mesh_info), one player,
// a race level, the level the engine loaded.
internal int EngSky_Eligible(const struct GameTracker *gGT)
{
	if (!g_cfg_engine || !g_cfg_engineTrack || (gGT == NULL) || (gGT->numPlyrCurrGame != 1))
	{
		return 0;
	}
	if (((gGT->gameMode1 & MAIN_MENU) != 0) || ((gGT->renderFlags & RENDER_FLAG_DRAW_LEVEL) == 0))
	{
		return 0;
	}
	if ((gGT->level1 == NULL) || (gGT->level1 != s_eskLevel) || (gGT->level1->ptr_mesh_info == NULL))
	{
		return 0;
	}
	return (s_eskMaterial >= 0);
}

// What the level's backdrop is, as CAM_ClearScreen, the draw environment
// (MainInit.c) and CAM_SkyboxGlow take it.
internal void EngSky_BackdropInput(const struct GameTracker *gGT, EngSkyBackdropIn *in)
{
	const struct Level *lev = gGT->level1;
	const int splitOn = ((gGT->renderFlags & RENDER_FLAG_CLEAR_BACK_BUFFER) != 0) && ((lev->clearColor[0].enable != 0) || (lev->clearColor[1].enable != 0));
	u8 background[3];
	int i;

	memset(in, 0, sizeof(*in));
	// The draw environment clears to clearColorRGBA unless both halves are on (1P).
	if ((lev->clearColor[0].enable != 0) && (lev->clearColor[1].enable != 0))
	{
		memset(background, 0, sizeof(background));
	}
	else
	{
		EngSky_Rgb(lev->clearColorRGBA, background);
	}
	for (i = 0; i < 3; i++)
	{
		in->top[i] = (splitOn && (lev->clearColor[0].enable != 0)) ? (u8)lev->clearColor[0].rgb[i] : background[i];
		in->bottom[i] = (splitOn && (lev->clearColor[1].enable != 0)) ? (u8)lev->clearColor[1].rgb[i] : background[i];
		in->fill[i] = (u8)lev->clearColor[2].rgb[i];
	}
	in->glow = (lev->configFlags & 1u) != 0u;
	in->fillAbove = (lev->clearColor[2].enable != 0);
	for (i = 0; i < 3; i++)
	{
		in->pointFrom[i] = lev->glowGradient[i].pointFrom;
		in->pointTo[i] = lev->glowGradient[i].pointTo;
		EngSky_Rgb(lev->glowGradient[i].colorFrom, in->colorFrom[i]);
		EngSky_Rgb(lev->glowGradient[i].colorTo, in->colorTo[i]);
	}
}

// At load stage 8, next to Engine_TrackLoad: the dome mesh and the material.
void Engine_SkyLoad(void)
{
	struct GameTracker *gGT = sdata->gGT;
	const struct Level *level;
	const struct Skybox *sky;
	EngineMaterial material;
	EngSkyBuildStats stats;
	int starCount;

	EngSky_Summary();
	memset(&s_eskCount, 0, sizeof(s_eskCount));
	s_eskLevel = NULL;
	s_eskLevelID = -1;
	s_eskMesh = -1;
	s_eskMeshVertices = 0;
	s_eskMaterial = -1;
	s_eskEligible = 0;
	s_eskDomeSubmitted = 0;
	s_eskStarsPromised = 0;
	if (!g_cfg_engine || !g_cfg_engineTrack || (gGT == NULL) || (gGT->level1 == NULL))
	{
		return;
	}
	if (!s_eskReportRegistered)
	{
		s_eskReportRegistered = 1;
		Platform_AtExitReport(EngSky_Report);
	}
	level = gGT->level1;
	s_eskLevelID = gGT->levelID;
	starCount = level->stars.numStars;

	memset(&material, 0, sizeof(material));
	material.blend = ENGINE_BLEND_OPAQUE;
	material.texSource = ENGINE_TEX_NONE;
	material.flags = ENGINE_MATERIAL_DOUBLE_SIDED | ENGINE_MATERIAL_NO_DEPTH_WRITE;
	material.tint[0] = material.tint[1] = material.tint[2] = material.tint[3] = 1.0f;
	s_eskMaterial = Engine_MaterialRegister(&material);
	if (s_eskMaterial < 0)
	{
		Platform_Log("[CTR Engine] sky: level %d: declined (material table full), PS1 draws it\n", s_eskLevelID);
		return;
	}
	s_eskLevel = level;

	sky = level->ptr_skybox;
	if (sky == NULL)
	{
		Platform_Log("[CTR Engine] sky: level %d: no skybox, stars %d\n", s_eskLevelID, starCount);
		return;
	}
	{
		int total = 0;
		int seg;
		EngineVertex *vertices;
		u16 *indices;
		int faces;

		memset(&stats, 0, sizeof(stats));
		for (seg = 0; seg < NUM_SKYBOX_SEGMENTS; seg++)
		{
			total += (sky->numFaces[seg] > 0) ? sky->numFaces[seg] : 0;
		}
		if ((sky->numVertex <= 0) || (sky->numVertex > 65535) || (total <= 0))
		{
			Platform_Log("[CTR Engine] sky: level %d: declined (%d vertices, %d faces), PS1 draws the dome\n", s_eskLevelID, sky->numVertex, total);
			return;
		}
		vertices = (EngineVertex *)malloc((size_t)sky->numVertex * sizeof(EngineVertex));
		indices = (u16 *)malloc((size_t)total * 3u * sizeof(u16));
		faces = ((vertices != NULL) && (indices != NULL)) ? EngSky_BuildDome(sky, vertices, indices, total, &stats) : 0;
		if (faces > 0)
		{
			EngineMeshDesc desc;
			EngineRange range;
			char name[32];

			snprintf(name, sizeof(name), "sky %d", s_eskLevelID);
			range.firstIndex = 0;
			range.indexCount = (u32)faces * 3u;
			range.material = (u16)s_eskMaterial;
			memset(&desc, 0, sizeof(desc));
			desc.vertices = vertices;
			desc.verticesPerFrame = (u32)sky->numVertex;
			desc.frames = 1;
			desc.indices = indices;
			desc.indexCount = range.indexCount;
			desc.ranges = &range;
			desc.rangeCount = 1;
			desc.name = name;
			s_eskMesh = Engine_MeshCreate(&desc);
			s_eskMeshVertices = (u32)sky->numVertex;
		}
		free(vertices);
		free(indices);
		if (faces <= 0)
		{
			Platform_Log("[CTR Engine] sky: level %d: declined (no usable face, %d bad, or out of memory), PS1 draws the dome\n", s_eskLevelID,
			             stats.bad);
			return;
		}
		Platform_Log("[CTR Engine] sky: level %d: %d vertices, %d faces (layer 0: %d, layer -1: %d, layer -2: %d, other layers %d, positive D %d), "
		             "%d bad faces, stars %d, mesh %d\n",
		             s_eskLevelID, sky->numVertex, faces, stats.layer0, stats.layer1, stats.layer2, stats.layerMore, stats.clamped, stats.bad, starCount,
		             s_eskMesh);
	}
}

// The stars of this frame as quads of one PSX pixel. Returns the vertex count.
internal int EngSky_StarQuads(const struct GameTracker *gGT, const EngineCamera *skyCam, int *indexCount)
{
	const int count = EngSky_StarPoints(&gGT->stars, s_eskStarPos, s_eskStarGrey, ESK_STARS_MAX);
	int nv = 0;
	int ni = 0;
	int i;

	for (i = 0; i < count; i++)
	{
		const EngineVec3 p = {(float)s_eskStarPos[i][0], (float)s_eskStarPos[i][1], (float)s_eskStarPos[i][2]};
		const EngineVec3 view = Engine_Mat4MulPoint(&skyCam->viewFromWorld, p);
		const u8 colour[4] = {s_eskStarGrey[i], s_eskStarGrey[i], s_eskStarGrey[i], 255};
		float half;

		if (!(view.z > skyCam->zNear))
		{
			continue;
		}
		half = 0.5f * view.z / skyCam->H; // half a PSX pixel at the star's depth
		if (Engine_FxBillboard(skyCam, p, half, half, 0, colour, &s_eskStarV[nv], &s_eskStarI[ni], (u16)nv))
		{
			nv += 4;
			ni += 6;
		}
	}
	*indexCount = ni;
	return nv;
}

internal int EngSky_Link(const EngineDraw *draw, void *user)
{
	struct GameTracker *gGT = (struct GameTracker *)user;

	return Engine_RenderSubmitLinked(0, draw, gGT->pushBuffer[0].ptrOT, ESK_CELL);
}

internal void EngSky_DrawInit(EngineDraw *draw, const EngineMat4 *clip)
{
	memset(draw, 0, sizeof(*draw));
	draw->mesh = -1;
	draw->clipFromModel = *clip;
	draw->tint[0] = draw->tint[1] = draw->tint[2] = draw->tint[3] = 1.0f;
	draw->materialOverride = -1;
	draw->otCell = ESK_CELL;
	draw->flags = ENGINE_DRAW_NO_DEPTH;
	draw->dynMaterial = -1;
}

// Once per frame from Engine_FrameCollect, before the PS1 code links its sky.
void Engine_SkyCollect(struct GameTracker *gGT)
{
	const EngineCamera *cam;
	EngineCamera skyCam;
	EngineMat4 clip;
	EngineDraw draws[3];
	int kinds[3];
	int count = 0;
	const int hide = (g_cfg_engineHide & ENGINE_HIDE_SKY) != 0;

	s_eskDomeSubmitted = 0;
	s_eskEligible = EngSky_Eligible(gGT);
	if (!s_eskEligible)
	{
		s_eskStarsPromised = 0;
		return;
	}
	s_eskCount.frames++;
	if (hide)
	{
		s_eskCount.hidden++;
	}
	cam = Engine_FrameCamera(0);
	if (cam == NULL)
	{
		s_eskCount.refused++;
		s_eskStarsPromised = 0;
		return;
	}
	EngSky_Camera(cam, &skyCam);
	Engine_CameraScreenFromWorld(&skyCam, &clip);

	// 1. The backdrop (in the key colour under --engine-hide sky).
	{
		static const u8 key[3] = {ESK_KEY_R, ESK_KEY_G, ESK_KEY_B};
		EngSkyBackdropIn in;
		EngSkyBackdrop bands;
		int indexCount = 0;
		int vertexCount;

		EngSky_BackdropInput(gGT, &in);
		EngSky_BackdropBands(&in, skyCam.H, &bands);
		vertexCount = EngSky_BackdropMesh(&bands, hide ? key : NULL, s_eskBackdropV, s_eskBackdropI, &indexCount);
		if ((vertexCount > 0) && (indexCount > 0))
		{
			EngSky_DrawInit(&draws[count], &clip);
			draws[count].dynVertices = s_eskBackdropV;
			draws[count].dynVertexCount = (u32)vertexCount;
			draws[count].dynIndices = s_eskBackdropI;
			draws[count].dynIndexCount = (u32)indexCount;
			draws[count].dynMaterial = s_eskMaterial;
			kinds[count++] = 0;
		}
	}
	// 2. The dome.
	if (!hide && EngSky_DomeLive())
	{
		EngSky_DrawInit(&draws[count], &clip);
		draws[count].mesh = s_eskMesh;
		kinds[count++] = 1;
	}
	// 3. The stars (promised to RenderStars earlier this frame).
	if (!hide && s_eskStarsPromised)
	{
		int indexCount = 0;
		const int vertexCount = EngSky_StarQuads(gGT, &skyCam, &indexCount);

		if ((vertexCount > 0) && (indexCount > 0))
		{
			EngSky_DrawInit(&draws[count], &clip);
			draws[count].dynVertices = s_eskStarV;
			draws[count].dynVertexCount = (u32)vertexCount;
			draws[count].dynIndices = s_eskStarI;
			draws[count].dynIndexCount = (u32)indexCount;
			draws[count].dynMaterial = s_eskMaterial;
			kinds[count++] = 2;
		}
	}

	// Linked in reverse, so the cell draws them backdrop, dome, stars.
	{
		int linked[3];
		int i;

		EngSky_LinkInDrawingOrder(draws, count, EngSky_Link, gGT, linked);
		for (i = 0; i < count; i++)
		{
			if (!linked[i])
			{
				s_eskCount.refused++;
			}
			else if (kinds[i] == 0)
			{
				s_eskCount.backdrop++;
			}
			else if (kinds[i] == 1)
			{
				s_eskCount.dome++;
				s_eskDomeSubmitted = 1;
			}
			else
			{
				s_eskCount.starsSubmitted++;
			}
		}
	}
	s_eskStarsPromised = 0;
}

// From DrawSky_Full (game/DrawSky.c): 1 = the engine linked this view's dome
// this frame (or owns it under --engine-hide sky); the PS1 faces are skipped.
int Engine_SkyOwnsView(const struct PushBuffer *pb)
{
	struct GameTracker *gGT = sdata->gGT;
	int owns;

	if (!g_cfg_engine || (gGT == NULL) || (pb != &gGT->pushBuffer[0]) || !s_eskEligible)
	{
		return 0;
	}
	if (g_cfg_engineHide & ENGINE_HIDE_SKY)
	{
		owns = 1;
	}
	else
	{
		owns = s_eskDomeSubmitted;
	}
	if (!owns && EngSky_DomeLive())
	{
		s_eskCount.ps1SkyKept++;
	}
	return owns;
}

// From RenderStars (game/RenderStars.c), which runs BEFORE Engine_FrameCollect:
// 1 = the engine draws this view's stars this frame (decided from the same
// conditions Engine_SkyCollect checks a moment later; a failure there counts as
// a refused submit).
int Engine_SkyOwnsStars(const struct PushBuffer *pb)
{
	struct GameTracker *gGT = sdata->gGT;

	if (!g_cfg_engine || (gGT == NULL) || (pb != &gGT->pushBuffer[0]) || !EngSky_Eligible(gGT))
	{
		return 0;
	}
	if (!(g_cfg_engineHide & ENGINE_HIDE_SKY))
	{
		s_eskStarsPromised = 1;
		s_eskCount.starsPromised++;
	}
	return 1;
}

// --------------------------------------------------------------------------
// Selftest

internal void EngSky_Check(int ok, const char *what, int *checks, int *failures)
{
	(*checks)++;
	if (!ok)
	{
		(*failures)++;
		printf("engine selftest: sky: %s\n", what);
	}
}

global_variable struct ShortVertex s_eskTestVerts[6];
global_variable struct SkyboxFace s_eskTestFaces[6];
global_variable struct Skybox s_eskTestSky;
global_variable struct PushBuffer s_eskTestPb;
global_variable u32 s_eskTestOt[0x400];
global_variable u32 s_eskTestPrims[(ESK_STARS_MAX + 8) * 3];

typedef struct
{
	int order[4];
	int count;
} EngSkyTestLinks;

internal int EngSky_TestLink(const EngineDraw *draw, void *user)
{
	EngSkyTestLinks *l = (EngSkyTestLinks *)user;

	if (l->count < 4)
	{
		l->order[l->count++] = draw->mesh;
	}
	return 1;
}

int Engine_SelfTestSky(int *checks)
{
	int failures = 0;
	int n = 0;

	// 1. The dome builder: layers, order, bad faces.
	{
		EngineVertex v[6];
		u16 idx[18];
		EngSkyBuildStats st;
		int faces;
		int i;

		memset(&s_eskTestSky, 0, sizeof(s_eskTestSky));
		for (i = 0; i < 6; i++)
		{
			s_eskTestVerts[i].Position.vx = (s16)(100 * i);
			s_eskTestVerts[i].Position.vy = (s16)(-50 * i);
			s_eskTestVerts[i].Position.vz = (s16)(7 * i);
			s_eskTestVerts[i].Color.r = (u8)(10 + i);
			s_eskTestVerts[i].Color.g = (u8)(20 + i);
			s_eskTestVerts[i].Color.b = (u8)(30 + i);
			s_eskTestVerts[i].Color.cd = 0x30;
		}
		// segment 0: face 0 (D 0), face 1 (D -4); segment 3: face 2 (D 0), face 3 (D -8),
		// face 4 (bad offset), face 5 (D +4, clamped to 0)
		{
			static const u16 f[6][4] = {{0, 12, 24, 0}, {12, 24, 36, 0xfffc}, {24, 36, 48, 0}, {36, 48, 60, 0xfff8}, {0, 13, 24, 0}, {48, 60, 0, 4}};

			for (i = 0; i < 6; i++)
			{
				s_eskTestFaces[i].A = f[i][0];
				s_eskTestFaces[i].B = f[i][1];
				s_eskTestFaces[i].C = f[i][2];
				s_eskTestFaces[i].D = f[i][3];
			}
		}
		s_eskTestSky.numVertex = 6;
		s_eskTestSky.ptrVertex = s_eskTestVerts;
		s_eskTestSky.numFaces[0] = 2;
		s_eskTestSky.ptrFaces[0] = &s_eskTestFaces[0];
		s_eskTestSky.numFaces[3] = 4;
		s_eskTestSky.ptrFaces[3] = &s_eskTestFaces[2];
		faces = EngSky_BuildDome(&s_eskTestSky, v, idx, 6, &st);
		EngSky_Check(faces == 5 && st.bad == 1 && st.layer0 == 3 && st.layer1 == 1 && st.layer2 == 1 && st.clamped == 1 && st.layerMore == 0,
		             "dome builder counts (5 faces, 1 bad, layers 3/1/1, 1 clamped)", &n, &failures);
		// Drawing order: D 0 faces in reverse file order (face 5, face 2, face 0), then D -4 (face 1), then D -8 (face 3).
		EngSky_Check(idx[0] == 4 && idx[1] == 5 && idx[2] == 0 && idx[3] == 2 && idx[6] == 0 && idx[9] == 1 && idx[12] == 3,
		             "dome drawing order (layer, then reverse file order)", &n, &failures);
		EngSky_Check(v[5].position[0] == 500.0f && v[5].position[1] == -250.0f && v[5].position[2] == 35.0f && v[5].color[0] == 15 &&
		                 v[5].color[1] == 25 && v[5].color[2] == 35 && v[5].color[3] == 255,
		             "dome vertex copied, alpha 255", &n, &failures);
		s_eskTestSky.numVertex = 70000;
		EngSky_Check(EngSky_BuildDome(&s_eskTestSky, v, idx, 6, &st) == 0, "dome with more than 65535 vertices declined", &n, &failures);
		s_eskTestSky.numVertex = 6;
		EngSky_Check(EngSky_BuildDome(&s_eskTestSky, v, idx, 4, &st) == 0, "dome larger than the room declined", &n, &failures);
		EngSky_Check(EngSky_BuildDome(NULL, v, idx, 6, &st) == 0, "no skybox declined", &n, &failures);
	}

	// 2. The sky matrix: rotation only, the PS1 projection of a direction.
	{
		u32 seed = 0x5eedu;
		int setup;
		int ok = 1;
		int same = 1;

		for (setup = 0; setup < 8; setup++)
		{
			struct PushBuffer *pb = &s_eskTestPb;
			EngineCamera a, b, sa, sb;
			EngineMat4 ma, mb;
			SVec3 rot;
			MATRIX gm;
			int k;

			memset(pb, 0, sizeof(*pb));
			pb->rect.w = 512;
			pb->rect.h = 216;
			pb->distanceToScreen_PREV = 0x1c2;
			seed = seed * 1664525u + 1013904223u;
			rot.x = (s16)((seed >> 8) & 0xfff);
			seed = seed * 1664525u + 1013904223u;
			rot.y = (s16)((seed >> 8) & 0xfff);
			seed = seed * 1664525u + 1013904223u;
			rot.z = (s16)((seed >> 8) & 0xfff);
			ConvertRotToMatrix(&gm, &rot);
			pb->matrix_ViewProj = gm;
			pb->matrix_ViewProj.t[0] = 1234;
			pb->matrix_ViewProj.t[1] = -777;
			pb->matrix_ViewProj.t[2] = 4321;
			Engine_CameraFromPushBuffer(pb, 0, &a);
			pb->matrix_ViewProj.t[0] = -9000;
			pb->matrix_ViewProj.t[1] = 50;
			pb->matrix_ViewProj.t[2] = 2;
			Engine_CameraFromPushBuffer(pb, 0, &b);
			EngSky_Camera(&a, &sa);
			EngSky_Camera(&b, &sb);
			Engine_CameraScreenFromWorld(&sa, &ma);
			Engine_CameraScreenFromWorld(&sb, &mb);
			for (k = 0; k < 16; k++)
			{
				if (ma.m[k] != mb.m[k])
				{
					same = 0;
				}
			}
			// A point in front of the camera (its forward row plus a bit of its right
			// row): screen = offset + H * (R p / 4096).xy / (R p / 4096).z, no translation.
			{
				const float p[3] = {(float)gm.m[2][0] * 4.0f + (float)gm.m[0][0], (float)gm.m[2][1] * 4.0f + (float)gm.m[0][1],
				                    (float)gm.m[2][2] * 4.0f + (float)gm.m[0][2]};
				float v[3] = {0.0f, 0.0f, 0.0f};
				float clip[4] = {0.0f, 0.0f, 0.0f, 0.0f};
				int row;
				int col;

				for (row = 0; row < 3; row++)
				{
					for (col = 0; col < 3; col++)
					{
						v[row] += (float)gm.m[row][col] * p[col] / 4096.0f;
					}
				}
				for (row = 0; row < 4; row++)
				{
					clip[row] = ma.m[0 * 4 + row] * p[0] + ma.m[1 * 4 + row] * p[1] + ma.m[2 * 4 + row] * p[2] + ma.m[3 * 4 + row];
				}
				if (!(v[2] > 0.0f) || !(clip[3] > 0.0f) || (fabsf(clip[0] / clip[3] - (256.0f + 450.0f * v[0] / v[2])) > 0.01f) ||
				    (fabsf(clip[1] / clip[3] - (108.0f + 450.0f * v[1] / v[2])) > 0.01f))
				{
					ok = 0;
				}
			}
		}
		EngSky_Check(same, "sky matrix ignores the camera translation", &n, &failures);
		EngSky_Check(ok, "sky matrix projects a direction like the PS1 rotation-only projection (0.01 px)", &n, &failures);
	}

	// 3. Link order: prepared backdrop, dome, stars; linked stars, dome, backdrop.
	{
		EngineDraw d[3];
		EngSkyTestLinks links;
		int linked[3] = {0, 0, 0};
		int i;

		memset(d, 0, sizeof(d));
		memset(&links, 0, sizeof(links));
		for (i = 0; i < 3; i++)
		{
			d[i].mesh = 10 + i;
		}
		EngSky_LinkInDrawingOrder(d, 3, EngSky_TestLink, &links, linked);
		EngSky_Check(linked[0] && linked[1] && linked[2] && links.count == 3 && links.order[0] == 12 && links.order[1] == 11 && links.order[2] == 10,
		             "links in reverse drawing order (the cell draws its last link first)", &n, &failures);
	}

	// 4. Material and draw flags of the sky.
	{
		EngineDraw draw;
		EngineMat4 m;

		Engine_Mat4Identity(&m);
		EngSky_DrawInit(&draw, &m);
		EngSky_Check(draw.flags == ENGINE_DRAW_NO_DEPTH && draw.otCell == ESK_CELL && draw.materialOverride == -1,
		             "sky draw: no depth test, cell 0x3ff", &n, &failures);
	}

	// 5. The backdrop: bands, order and colours.
	{
		EngSkyBackdropIn in;
		EngSkyBackdrop bd;
		static EngineVertex v[ESK_BACKDROP_VERTICES];
		static u16 idx[ESK_BACKDROP_INDICES];
		static const u8 key[3] = {ESK_KEY_R, ESK_KEY_G, ESK_KEY_B};
		int ni = 0;
		int nv;
		int i;
		int keyed = 1;

		memset(&in, 0, sizeof(in));
		in.top[2] = 200;
		in.bottom[0] = 100;
		EngSky_BackdropBands(&in, 450.0f, &bd);
		EngSky_Check(bd.count == 2 && bd.band[0].hi == 0.0f && bd.band[0].cLo[0] == 100 && bd.band[1].lo == 0.0f && bd.band[1].cHi[2] == 200,
		             "backdrop without glow: bottom below the horizon, top above", &n, &failures);
		in.glow = 1;
		in.pointFrom[0] = 120;
		in.pointTo[0] = 0;
		in.pointFrom[1] = 0;
		in.pointTo[1] = -30;
		in.colorFrom[0][1] = 77;
		in.fillAbove = 1;
		in.fill[0] = 9;
		EngSky_BackdropBands(&in, 450.0f, &bd);
		// base 2, glow 2 (0,0: none), glow 1, fill, glow 0 = 5
		EngSky_Check(bd.count == 5 && fabsf(bd.band[2].lo - atanf(-30.0f / 450.0f)) < 1e-6f && bd.band[2].hi == 0.0f &&
		                 fabsf(bd.band[3].lo - atanf(120.0f / 450.0f)) < 1e-6f && bd.band[3].cLo[0] == 9 && bd.band[4].lo == 0.0f &&
		                 fabsf(bd.band[4].hi - atanf(120.0f / 450.0f)) < 1e-6f && bd.band[4].cHi[1] == 77,
		             "backdrop glow bands: elevations from pixel heights, PS1 drawing order", &n, &failures);
		nv = EngSky_BackdropMesh(&bd, key, v, idx, &ni);
		for (i = 0; i < nv; i++)
		{
			if ((v[i].color[0] != ESK_KEY_R) || (v[i].color[1] != ESK_KEY_G) || (v[i].color[2] != ESK_KEY_B))
			{
				keyed = 0;
			}
		}
		EngSky_Check(nv == 5 * (ESK_BACKDROP_SEGMENTS + 1) * 2 && ni == 5 * ESK_BACKDROP_SEGMENTS * 6 && keyed,
		             "backdrop mesh size, key colour everywhere", &n, &failures);
		{
			int inRange = 1;

			for (i = 0; i < ni; i++)
			{
				if (idx[i] >= (u16)nv)
				{
					inRange = 0;
				}
			}
			EngSky_Check(inRange, "backdrop indices in range", &n, &failures);
		}
	}

	// 6. The stars against RenderStars itself: same directions, same grey. Eight
	// cameras. Every engine star that projects into the PS1 window (one pixel of
	// margin) must have a PS1 tile within 1.5 pixels with its grey, and every PS1
	// tile in the window must have an engine star within 1.5 pixels. 1.5: the
	// GTE truncates to whole pixels and its division is approximate (the camera
	// selftest measures up to 1.1 px between the engine and the GTE).
	{
		void RenderStars(struct PushBuffer *pb, struct PrimMem *primMem, struct Stars *stars, u8 numPlyr);
		struct PushBuffer *pb = &s_eskTestPb;
		struct PrimMem prim;
		struct Stars st;
		const int savedEngine = g_cfg_engine;
		int count;
		int compared = 0;
		int matched = 0;
		int engineOnly = 0;
		int tiles = 0;
		int ps1Only = 0;
		int said = 0;
		int setup;

		st.numStars = 300;
		st.spread = 0;
		st.seed = 1;
		st.distance = 0x3fe;
		count = EngSky_StarPoints(&st, s_eskStarPos, s_eskStarGrey, ESK_STARS_MAX);
		EngSky_Check(count == 301, "stars: retail makes numStars + 1", &n, &failures);
		// RenderStars links its tiles through the GPU link bridge, which knows only
		// registered memory: the test's primitive store is registered once (the
		// selftest process ends after the tests).
		if (!NativeGpuLinks_IsRegisteredHostRange(s_eskTestPrims, sizeof(s_eskTestPrims)))
		{
			u32 token = 0;

			if (!NativeGpuLinks_RegisterRange(s_eskTestPrims, sizeof(s_eskTestPrims), &token))
			{
				EngSky_Check(0, "stars: the test primitive store could not be registered with the GPU link bridge", &n, &failures);
				*checks += n;
				return failures;
			}
		}

		for (setup = 0; setup < 8; setup++)
		{
			EngineCamera cam;
			SVec3 rot;
			MATRIX gm;
			const TILE_1 *first;
			int tileCount;
			int i;
			int t;

			memset(pb, 0, sizeof(*pb));
			memset(&prim, 0, sizeof(prim));
			memset(s_eskTestOt, 0, sizeof(s_eskTestOt));
			pb->rect.w = 512;
			pb->rect.h = 216;
			pb->distanceToScreen_PREV = 0x1c2;
			pb->ptrOT = s_eskTestOt;
			rot.x = (s16)(0x400 + setup * 0x100);
			rot.y = (s16)(setup * 0x200);
			rot.z = 0;
			ConvertRotToMatrix(&gm, &rot);
			pb->matrix_ViewProj = gm;
			// ConvertRotToMatrix leaves the translation as it was: no translation (RenderStars zeroes it in the GTE).
			pb->matrix_ViewProj.t[0] = 0;
			pb->matrix_ViewProj.t[1] = 0;
			pb->matrix_ViewProj.t[2] = 0;
			prim.cursor = s_eskTestPrims;
			g_cfg_engine = 0; // the PS1 stars, whatever the test run's switches
			RenderStars(pb, &prim, &st, 1);
			g_cfg_engine = savedEngine;
			Engine_CameraFromPushBuffer(pb, 0, &cam);

			first = (const TILE_1 *)(const void *)s_eskTestPrims;
			tileCount = (int)((const TILE_1 *)prim.cursor - first) - 1; // the last packet is the draw-mode packet (same size)
			tiles += tileCount;
			for (i = 0; i < count; i++)
			{
				const EngineVec3 p = {(float)s_eskStarPos[i][0], (float)s_eskStarPos[i][1], (float)s_eskStarPos[i][2]};
				EngineVec3 sp;
				int found = 0;

				if (!Engine_ProjectPoint(&cam, p, &sp) || (sp.x < 1.0f) || (sp.x >= 510.0f) || (sp.y < 1.0f) || (sp.y >= 215.0f))
				{
					continue;
				}
				compared++;
				for (t = 0; t < tileCount; t++)
				{
					if ((fabsf((float)first[t].x0 - sp.x) <= 1.5f) && (fabsf((float)first[t].y0 - sp.y) <= 1.5f) && (first[t].r0 == s_eskStarGrey[i]))
					{
						found = 1;
						break;
					}
				}
				if (found)
				{
					matched++;
				}
				else
				{
					engineOnly++;
					if (said++ < 8)
					{
						printf("engine selftest: sky: camera %d star %d (%d %d %d) at %.2f %.2f z %.1f has no PS1 tile\n", setup, i, s_eskStarPos[i][0],
						       s_eskStarPos[i][1], s_eskStarPos[i][2], sp.x, sp.y, sp.z);
					}
				}
			}
			for (t = 0; t < tileCount; t++)
			{
				int found = 0;

				// The GTE saturates a far-off star to -1024 and the PS1 window test
				// lets some of those through: off screen, nothing to compare.
				if ((first[t].x0 < 1) || (first[t].x0 >= 510) || (first[t].y0 < 1) || (first[t].y0 >= 215))
				{
					continue;
				}
				for (i = 0; (i < count) && !found; i++)
				{
					const EngineVec3 p = {(float)s_eskStarPos[i][0], (float)s_eskStarPos[i][1], (float)s_eskStarPos[i][2]};
					EngineVec3 sp;

					found = Engine_ProjectPoint(&cam, p, &sp) && (fabsf((float)first[t].x0 - sp.x) <= 1.5f) && (fabsf((float)first[t].y0 - sp.y) <= 1.5f);
				}
				if (!found)
				{
					ps1Only++;
					if (said++ < 8)
					{
						printf("engine selftest: sky: camera %d PS1 tile %d at %d %d grey %d has no engine star\n", setup, t, (int)first[t].x0, (int)first[t].y0,
						       (int)first[t].r0);
					}
				}
			}
		}
		EngSky_Check((compared >= 50) && (engineOnly == 0) && (ps1Only == 0), "stars: engine directions and grey match RenderStars (1.5 px)", &n, &failures);
		if ((compared < 50) || (engineOnly != 0) || (ps1Only != 0))
		{
			printf("engine selftest: sky: stars compared %d, matched %d, engine only %d; PS1 tiles %d, PS1 only %d\n", compared, matched, engineOnly, tiles,
			       ps1Only);
		}
	}

	*checks += n;
	return failures;
}
