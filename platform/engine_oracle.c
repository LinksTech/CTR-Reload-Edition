#include <platform/engine.h>
#include <psx/gtereg.h>

#include "ctr_build_id.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ENGINE ORACLE - see include/platform/engine.h (section ENGINE ORACLE (P1)).
//
// The material comparison of the round of 2026-10-16: in a few chosen frames
// both paths write, per drawn face, which texture page, CLUT, texel window and
// blend they use. The PS1 path is the ORACLE for WHICH texture belongs on a
// face, not for pixels (the principle): R lines are the primitives the PS1
// path makes for a source in that frame, E lines what the engine reads for the
// same source. tools/material_compare.py compares the two per source as sets.
//
// FRAMES. Engine_OracleArm writes the frame line at once and keeps the oracle
// active until the next Engine_RenderFrameBegin (the swap), so exactly one
// MainFrame_RenderFrame is logged. Without --engine, or with no
// --engine-oracle file, nothing is opened, written or kept.
//
// SOURCES. Between Engine_OracleSourceBegin and Engine_OracleSourceEnd every
// successful Engine_RenderSubmitLinked hands its draw to this file, which
// buffers one face per triangle (the material the renderer will bind, the
// texture coordinates of the vertices it will draw). The letter of the source
// is given at the end, because the route that submits decides it
// (game/RenderBucket/RenderBucket_QueueExecute.c, RenderBucket_OracleDispatch).
//
// THE RETAIL SIDE of an instance the engine draws is made by running the
// instance's retail draw handler a second time in the armed frame, with the OT
// link switched off (RenderBucket_LinkPrimRaw writes the tag, links nothing),
// and then decoding the packets it wrote between the primMem cursor before and
// after (Engine_OracleRetailRange). The cursor, the instance's OT ranges, the
// GTE registers and the scratchpad are put back afterwards
// (Engine_OracleStateSave / Engine_OracleStateRestore), so the frame goes on
// exactly as an armed frame without the oracle.
//
// W3 (--engine-oracle-fault n): one engine material is drawn with a wrong CLUT
// (one row lower) from the armed frame that picks it until the level unloads,
// and its E lines say so (the faulted clut and fault=clut). Measuring only.

char g_cfg_engineOracle[ENGINE_ORACLE_PATH_MAX];
int g_cfg_engineOracleFaultClut = -1;
int g_cfg_engineOracleFaultCell = -1;
int g_engineOracleFaultMaterial = -1;

#define EO_SOURCE_DEPTH_MAX 4
#define EO_DECODE_MAX 8

// box= of the E lines: the texel indices the "ne" VRAM path can reach for a
// triangle. 1 since the round of 2026-10-16: "ne" clamps floor(v_uv) to the
// triangle's UV box (EngineVertex.uvBox, a flat attribute taken from the
// triangle's first vertex), so box = that vertex's box (Engine_VertexBoxGet). 0 was the rule before
// the clamp: a rounding of the interpolation reaches one texel before the
// smallest corner (umin - 1) and the largest corner itself; box = umin-1,
// vmin-1, umax, vmax of the corners.
#define ENGINE_ORACLE_SAMPLE_CLAMPED 1

typedef struct
{
	EngineOracleFace face;
	int material;
} EngOracleBuffered;

global_variable FILE *s_eoFile;
global_variable int s_eoOpenFailed;
global_variable int s_eoActive;
global_variable char s_eoTag[64];
global_variable u32 s_eoLinesR;
global_variable u32 s_eoLinesE;
global_variable u32 s_eoFrames;
global_variable u32 s_eoTotalR;
global_variable u32 s_eoTotalE;
global_variable u32 s_eoDumps;
global_variable char s_eoArgs[1024];
global_variable int s_eoSourceHires;

// VRAM writes (platform/native_libgpu.c) per tour entry, in race frames.
global_variable int s_eoTourRegistered;
global_variable int s_eoInRace;
global_variable u32 s_eoVramCount[2][3]; // [outside, race][LoadImage, MoveImage, ClearImage]
global_variable u32 s_eoVramWords[2];
global_variable int s_eoVramEntry = -1;
global_variable int s_eoVramLevel = -1;
global_variable u32 s_eoVramLinesEntry;
global_variable u32 s_eoVramLinesRun;
global_variable u32 s_eoVramLinesDropped;

global_variable int s_eoSourceDepth;
global_variable char s_eoSourceId[ENGINE_ORACLE_ID_MAX];
global_variable EngOracleBuffered *s_eoBuf;
global_variable int s_eoBufCount;
global_variable int s_eoBufCap;
global_variable int s_eoBufFull;

// W3: the distinct VRAM materials of instance sources seen in this armed frame.
global_variable u8 s_eoFaultSeen[ENGINE_MATERIALS_MAX / 8];
global_variable int s_eoFaultDistinct;
global_variable EngineMaterial s_eoFaultMat;

// anim=1 of the E lines: per mesh one byte per triangle (Engine_OracleNoteMeshAnimated).
global_variable u8 *s_eoMeshAnim[ENGINE_MESHES_MAX];
global_variable u32 s_eoMeshAnimCount[ENGINE_MESHES_MAX];

// Engine_OracleStateSave / Restore.
global_variable GTERegisters s_eoGte[2];
global_variable u8 s_eoScratch[2][CTR_SCRATCHPAD_SIZE];

// --------------------------------------------------------------------------
// Small helpers.

internal void EngOracle_Append(char *dst, int size, const char *add)
{
	size_t len;

	if ((dst == NULL) || (size <= 0) || (add == NULL) || (add[0] == '\0'))
	{
		return;
	}
	len = strlen(dst);
	if (len + 1 >= (size_t)size)
	{
		return;
	}
	if (len > 0)
	{
		dst[len++] = ' '; // pairs are separated by one space (values may hold ',' and ';')
		dst[len] = '\0';
	}
	snprintf(dst + len, (size_t)size - len, "%s", add);
}

// Copies text without tabs, spaces or line ends (they would break the columns).
internal void EngOracle_CopyField(char *dst, int size, const char *src)
{
	int i = 0;

	if ((dst == NULL) || (size <= 0))
	{
		return;
	}
	if (src != NULL)
	{
		for (; (src[i] != '\0') && (i < size - 1); i++)
		{
			const char c = src[i];

			dst[i] = ((c == '\t') || (c == ' ') || (c == '\r') || (c == '\n')) ? '_' : c;
		}
	}
	dst[i] = '\0';
}

internal void EngOracle_TpageToFace(u16 tpage, EngineOracleFace *f)
{
	const EngineTpage tp = Engine_DecodeTpage(tpage);

	f->tpX = tp.baseX;
	f->tpY = tp.baseY;
	f->depth = tp.depth;
}

internal void EngOracle_ClutToFace(u16 clut, EngineOracleFace *f)
{
	if (f->depth >= 2)
	{
		f->clutX = -1;
		f->clutY = -1;
		return;
	}
	Engine_DecodeClut(clut, &f->clutX, &f->clutY);
}

// --------------------------------------------------------------------------
// Frames.

void Engine_OracleArmFrame(const char *tag, int raceFrame)
{
	const struct GameTracker *gGT = sdata->gGT;
	int levelId;
	int container;
	int frame;
	int views;

	if (!g_cfg_engine || (g_cfg_engineOracle[0] == '\0'))
	{
		return;
	}
	if (s_eoFile == NULL)
	{
		if (s_eoOpenFailed)
		{
			return;
		}
		s_eoFile = fopen(g_cfg_engineOracle, "w");
		if (s_eoFile == NULL)
		{
			s_eoOpenFailed = 1;
			Platform_Log("[CTR Engine] oracle: cannot open '%s' - nothing is logged\n", g_cfg_engineOracle);
			return;
		}
		Platform_Log("[CTR Engine] oracle: writing '%s'%s%s\n", g_cfg_engineOracle, (g_cfg_engineOracleFaultClut >= 0) ? " (W3 fault clut)" : "",
		             (g_cfg_engineOracleFaultCell >= 0) ? " (W3 fault cell)" : "");
		// H: the build and the switches of the run (D-W1.1).
		if (s_eoArgs[0] == '\0')
		{
			snprintf(s_eoArgs, sizeof(s_eoArgs), "engine=%d track=%d menuscene=%d hide=%d fault-clut=%d fault-cell=%d", g_cfg_engine, g_cfg_engineTrack,
			         g_cfg_engineMenuScene, g_cfg_engineHide, g_cfg_engineOracleFaultClut, g_cfg_engineOracleFaultCell);
		}
		fprintf(s_eoFile, "H\t%s\t%s\n", CTR_NATIVE_BUILD_ID, s_eoArgs);
	}
	if (s_eoActive)
	{
		// Armed twice in one frame: the second tag names the same frame.
		Platform_Log("[CTR Engine] oracle: '%s' armed while '%s' is open - ignored\n", (tag != NULL) ? tag : "", s_eoTag);
		return;
	}

	levelId = (gGT != NULL) ? gGT->levelID : -1;
	container = (levelId >= 0) ? (NativeTrack_ActiveForLevel(levelId) ? 1 : 0) : 0;
	frame = (raceFrame >= 0) ? raceFrame : ((gGT != NULL) ? gGT->framesInThisLEV : 0);
	views = (gGT != NULL) ? (int)gGT->numPlyrCurrGame : 1;

	EngOracle_CopyField(s_eoTag, sizeof(s_eoTag), ((tag != NULL) && (tag[0] != '\0')) ? tag : "frame");
	// The VRAM of this frame, through the named dump of the platform (served at
	// the end of the frame): <--dump-prefix>-oracle-<tag>.tga. s_dumpPrefix is
	// native_platform.c's prefix (same translation unit, read only). The
	// platform keeps one named request per frame: one made before in this frame
	// (the tour's track dump) is replaced, said in the log.
	{
		char name[96];

		snprintf(name, sizeof(name), "oracle-%s", s_eoTag);
		if (s_dumpRequestName[0] != '\0')
		{
			Platform_Log("[CTR Engine] oracle: the dump request '%s' of this frame is replaced by '%s'\n", s_dumpRequestName, name);
		}
		Platform_DumpRequest(name);
		s_eoDumps++;
		fprintf(s_eoFile, "F\t%s\t%d\t%d\t%d\t%d\tvram=%s-%s.tga\n", s_eoTag, levelId, container, frame, views, s_dumpPrefix, name);
	}

	s_eoActive = 1;
	s_eoLinesR = 0;
	s_eoLinesE = 0;
	s_eoSourceDepth = 0;
	s_eoBufCount = 0;
	s_eoBufFull = 0;
	memset(s_eoFaultSeen, 0, sizeof(s_eoFaultSeen));
	s_eoFaultDistinct = 0;
	s_eoFrames++;
}

void Engine_OracleArm(const char *tag)
{
	Engine_OracleArmFrame(tag, -1);
}

int Engine_OracleActive(void)
{
	return s_eoActive;
}

void Engine_OracleFrameEnd(void)
{
	if (!s_eoActive)
	{
		return;
	}
	if (s_eoSourceDepth > 0)
	{
		// A source left open (a route that returned early): its faces are dropped, said once per frame.
		Platform_Log("[CTR Engine] oracle: frame '%s' ended with a source open ('%s') - %d face(s) dropped\n", s_eoTag, s_eoSourceId, s_eoBufCount);
		s_eoSourceDepth = 0;
		s_eoBufCount = 0;
	}
	s_eoActive = 0;
	if (s_eoFile != NULL)
	{
		fflush(s_eoFile);
	}
	Platform_Log("[CTR Engine] oracle: frame '%s' written: %u R line(s), %u E line(s)%s\n", s_eoTag, s_eoLinesR, s_eoLinesE,
	             s_eoBufFull ? " (source buffer full, E lines missing)" : "");
}

void Engine_OracleNoteMeshAnimated(int mesh, const u8 *triangleAnimated, u32 triangles)
{
	if ((g_cfg_engineOracle[0] == '\0') || (mesh < 0) || (mesh >= ENGINE_MESHES_MAX) || (triangleAnimated == NULL) || (triangles == 0u))
	{
		return;
	}
	free(s_eoMeshAnim[mesh]);
	s_eoMeshAnim[mesh] = (u8 *)malloc(triangles);
	s_eoMeshAnimCount[mesh] = 0;
	if (s_eoMeshAnim[mesh] != NULL)
	{
		memcpy(s_eoMeshAnim[mesh], triangleAnimated, triangles);
		s_eoMeshAnimCount[mesh] = triangles;
	}
}

void Engine_OracleLevelUnload(void)
{
	for (int i = 0; i < ENGINE_MESHES_MAX; i++)
	{
		free(s_eoMeshAnim[i]);
		s_eoMeshAnim[i] = NULL;
		s_eoMeshAnimCount[i] = 0;
	}
	// The material table goes with the level, so does the fault (the next level picks again).
	if (g_engineOracleFaultMaterial >= 0)
	{
		Platform_Log("[CTR Engine] oracle fault: material %d released at level unload\n", g_engineOracleFaultMaterial);
	}
	g_engineOracleFaultMaterial = -1;
}

const EngineMaterial *Engine_OracleFaultMaterial(void)
{
	return &s_eoFaultMat;
}

// --------------------------------------------------------------------------
// Lines.

int Engine_OracleFormatLine(char kind, const EngineOracleFace *f, char *out, int size)
{
	char tp[32];
	char clut[32];
	char uv[96];
	char blend[4];
	char cell[24];
	char id[ENGINE_ORACLE_ID_MAX];
	char extra[ENGINE_ORACLE_EXTRA_MAX + 96];
	int len;

	if ((f == NULL) || (out == NULL) || (size <= 0) || ((kind != 'R') && (kind != 'E')))
	{
		return 0;
	}

	if (f->textured)
	{
		snprintf(tp, sizeof(tp), "%d,%d,%d", f->tpX, f->tpY, f->depth);
		if ((f->depth >= 2) || (f->clutX < 0))
		{
			snprintf(clut, sizeof(clut), "-,-");
		}
		else
		{
			snprintf(clut, sizeof(clut), "%d,%d", f->clutX, f->clutY);
		}
		if ((f->uvCount == 3) || (f->uvCount == 4))
		{
			int pos = 0;

			for (int i = 0; i < f->uvCount; i++)
			{
				pos += snprintf(uv + pos, sizeof(uv) - (size_t)pos, "%s%d,%d", (i == 0) ? "" : ",", f->uv[i * 2], f->uv[i * 2 + 1]);
			}
		}
		else
		{
			snprintf(uv, sizeof(uv), "-");
		}
	}
	else
	{
		snprintf(tp, sizeof(tp), "-");
		snprintf(clut, sizeof(clut), "-,-");
		snprintf(uv, sizeof(uv), "-");
	}

	if ((f->blend >= 0) && (f->blend <= 3))
	{
		blend[0] = (char)('0' + f->blend);
	}
	else
	{
		blend[0] = 'o';
	}
	blend[1] = '\0';

	if ((f->cellRow >= 0) && (f->cellCol >= 0))
	{
		snprintf(cell, sizeof(cell), "%d,%d", f->cellRow, f->cellCol);
	}
	else
	{
		snprintf(cell, sizeof(cell), "-");
	}

	EngOracle_CopyField(id, sizeof(id), (f->srcId[0] != '\0') ? f->srcId : "-");

	// extra: the pairs with one space between them (no leading, trailing or
	// double spaces), then col= of the corner colours.
	{
		int at = 0;
		int space = 0;

		for (int i = 0; (f->extra[i] != '\0') && (at < (int)sizeof(extra) - 1); i++)
		{
			const char c = f->extra[i];

			if ((c == ' ') || (c == '\t'))
			{
				space = (at > 0);
				continue;
			}
			if (space && (at < (int)sizeof(extra) - 2))
			{
				extra[at++] = ' ';
			}
			space = 0;
			extra[at++] = c;
		}
		extra[at] = '\0';
		if (f->raw)
		{
			char words[40];

			snprintf(words, sizeof(words), "tp=0x%04x cl=0x%04x", (unsigned)f->rawTpage, (unsigned)f->rawClut);
			EngOracle_Append(extra, (int)sizeof(extra), words);
		}
		if ((f->colorCount >= 3) && (f->colorCount <= 4))
		{
			char col[96];
			int pos = snprintf(col, sizeof(col), "col=");

			for (int i = 0; i < f->colorCount; i++)
			{
				pos += snprintf(col + pos, sizeof(col) - (size_t)pos, "%s%d,%d,%d", (i == 0) ? "" : ";", f->color[i][0], f->color[i][1], f->color[i][2]);
			}
			EngOracle_Append(extra, (int)sizeof(extra), col);
		}
	}
	len = snprintf(out, (size_t)size, "%c\t%c\t%s\t%d\t%s\t%s\t%s\t%s\t%s\t%s\n", kind, (f->src != 0) ? f->src : '?', id, f->sub, tp, clut, uv, blend,
	               cell, (extra[0] != '\0') ? extra : "-");
	return ((len > 0) && (len < size)) ? len : 0;
}

void Engine_OracleWrite(char kind, const EngineOracleFace *f)
{
	char line[ENGINE_ORACLE_ID_MAX + ENGINE_ORACLE_EXTRA_MAX + 512];

	if (!s_eoActive || (s_eoFile == NULL))
	{
		return;
	}
	if (Engine_OracleFormatLine(kind, f, line, (int)sizeof(line)) == 0)
	{
		return;
	}
	fputs(line, s_eoFile);
	if (kind == 'R')
	{
		s_eoLinesR++;
		s_eoTotalR++;
	}
	else
	{
		s_eoLinesE++;
		s_eoTotalE++;
	}
}

// --------------------------------------------------------------------------
// Retail: GPU packets to faces.

int Engine_OracleDecodePacket(const u32 *words, int count, u32 *drawMode, EngineOracleFace *out, int outMax)
{
	int i = 0;
	int n = 0;
	u32 modeLocal = 0xffffffffu;
	u32 *mode = (drawMode != NULL) ? drawMode : &modeLocal;

	if ((words == NULL) || (count <= 0))
	{
		return 0;
	}

	while (i < count)
	{
		const u32 w = words[i];
		const u32 code = w >> 24;

		if (code == 0x00u)
		{
			i++; // NOP (the pad words of the ghost packets)
			continue;
		}
		if ((code >= 0xe1u) && (code <= 0xe6u))
		{
			if (code == 0xe1u)
			{
				*mode = w & 0x00ffffffu;
			}
			i++;
			continue;
		}
		if ((code & 0xe0u) == 0x20u)
		{
			// Polygon: bit 4 gouraud, bit 3 quad, bit 2 textured, bit 1 semi-transparent, bit 0 raw texture.
			const int gouraud = (code & 0x10u) != 0u;
			const int quad = (code & 0x08u) != 0u;
			const int textured = (code & 0x04u) != 0u;
			const int semi = (code & 0x02u) != 0u;
			const int raw = (code & 0x01u) != 0u;
			const int verts = quad ? 4 : 3;
			const int size = 1 + verts * (1 + (textured ? 1 : 0)) + (gouraud ? (verts - 1) : 0);
			EngineOracleFace f;
			u16 clut = 0;
			u16 tpage = 0;
			int j = i + 1;

			if (i + size > count)
			{
				break;
			}
			memset(&f, 0, sizeof(f));
			f.uvCount = verts;
			f.cellRow = -1;
			f.cellCol = -1;
			f.colorCount = verts;
			for (int v = 0; v < verts; v++)
			{
				// The colour of the corner: word 0 for corner 0 (and every corner of a flat face), its own word for a gouraud corner.
				u32 colour = w;

				if (gouraud && (v > 0))
				{
					colour = words[j++];
				}
				f.color[v][0] = (u8)colour;
				f.color[v][1] = (u8)(colour >> 8);
				f.color[v][2] = (u8)(colour >> 16);
				j++; // xy
				if (textured)
				{
					const u32 uvWord = words[j++];

					f.uv[v * 2] = (int)(uvWord & 0xffu);
					f.uv[v * 2 + 1] = (int)((uvWord >> 8) & 0xffu);
					if (v == 0)
					{
						clut = (u16)(uvWord >> 16);
					}
					else if (v == 1)
					{
						tpage = (u16)(uvWord >> 16);
					}
				}
			}
			f.textured = textured;
			if (textured)
			{
				f.raw = 1;
				f.rawTpage = tpage;
				f.rawClut = clut;
				EngOracle_TpageToFace(tpage, &f);
				EngOracle_ClutToFace(clut, &f);
				// The GPU takes the page bits of a textured polygon into its draw mode.
				*mode = ((*mode == 0xffffffffu) ? 0u : (*mode & ~0x1ffu)) | ((u32)tpage & 0x1ffu);
				if (raw)
				{
					EngOracle_Append(f.extra, sizeof(f.extra), "raw=1");
				}
			}
			else
			{
				f.clutX = -1;
				f.clutY = -1;
			}
			if (!semi)
			{
				f.blend = ENGINE_ORACLE_BLEND_OPAQUE;
			}
			else if (textured)
			{
				f.blend = (int)((tpage >> 5) & 3u);
			}
			else if (*mode != 0xffffffffu)
			{
				f.blend = (int)((*mode >> 5) & 3u);
			}
			else
			{
				f.blend = 0;
				EngOracle_Append(f.extra, sizeof(f.extra), "abr=unknown");
			}
			if (n < outMax)
			{
				out[n++] = f;
			}
			i += size;
			continue;
		}
		if ((code & 0xe0u) == 0x60u)
		{
			// Rectangle (sprite): bits 3-4 size (0 variable, 1 = 1x1, 2 = 8x8, 3 = 16x16), the page from the draw mode.
			const int textured = (code & 0x04u) != 0u;
			const int semi = (code & 0x02u) != 0u;
			const int sizeBits = (int)((code >> 3) & 3u);
			const int size = 2 + (textured ? 1 : 0) + ((sizeBits == 0) ? 1 : 0);
			EngineOracleFace f;
			int j = i + 2;
			int u = 0;
			int v = 0;
			int w16 = (sizeBits == 1) ? 1 : ((sizeBits == 2) ? 8 : 16);
			int h16 = w16;
			u16 clut = 0;

			if (i + size > count)
			{
				break;
			}
			if (textured)
			{
				const u32 uvWord = words[j++];

				u = (int)(uvWord & 0xffu);
				v = (int)((uvWord >> 8) & 0xffu);
				clut = (u16)(uvWord >> 16);
			}
			if (sizeBits == 0)
			{
				const u32 wh = words[j++];

				w16 = (int)(wh & 0xffffu);
				h16 = (int)(wh >> 16);
			}
			memset(&f, 0, sizeof(f));
			f.cellRow = -1;
			f.cellCol = -1;
			f.textured = textured;
			f.colorCount = 4;
			for (int c = 0; c < 4; c++)
			{
				f.color[c][0] = (u8)w;
				f.color[c][1] = (u8)(w >> 8);
				f.color[c][2] = (u8)(w >> 16);
			}
			EngOracle_Append(f.extra, sizeof(f.extra), "sprite=1");
			if (textured)
			{
				if (*mode != 0xffffffffu)
				{
					EngOracle_TpageToFace((u16)(*mode & 0x1ffu), &f);
					f.raw = 1;
					f.rawTpage = (u16)(*mode & 0x1ffu);
					f.rawClut = clut;
				}
				else
				{
					EngOracle_Append(f.extra, sizeof(f.extra), "tp=unknown");
				}
				EngOracle_ClutToFace(clut, &f);
				// Corners (u, v), (u + w, v), (u, v + h), (u + w, v + h): the end is exclusive.
				f.uvCount = 4;
				f.uv[0] = u;
				f.uv[1] = v;
				f.uv[2] = u + w16;
				f.uv[3] = v;
				f.uv[4] = u;
				f.uv[5] = v + h16;
				f.uv[6] = u + w16;
				f.uv[7] = v + h16;
			}
			else
			{
				f.clutX = -1;
				f.clutY = -1;
			}
			if (!semi)
			{
				f.blend = ENGINE_ORACLE_BLEND_OPAQUE;
			}
			else if (*mode != 0xffffffffu)
			{
				f.blend = (int)((*mode >> 5) & 3u);
			}
			else
			{
				f.blend = 0;
				EngOracle_Append(f.extra, sizeof(f.extra), "abr=unknown");
			}
			if (n < outMax)
			{
				out[n++] = f;
			}
			i += size;
			continue;
		}
		if ((code & 0xe0u) == 0x40u)
		{
			// Line (not a face): skipped. A poly-line ends with 0x5xxx5xxx.
			if ((code & 0x08u) != 0u)
			{
				i++;
				while ((i < count) && ((words[i] & 0xf000f000u) != 0x50005000u))
				{
					i++;
				}
				i++;
			}
			else
			{
				i += ((code & 0x10u) != 0u) ? 4 : 3;
			}
			continue;
		}
		if (code == 0x02u)
		{
			i += 3; // fill
			continue;
		}
		if ((code >= 0x80u) && (code <= 0x9fu))
		{
			i += 4; // VRAM to VRAM copy
			continue;
		}
		break; // anything else: the rest of the packet is not read
	}
	return n;
}

// The current layouts of the animated (animtex) table slots of a header, as
// faces (page, clut, uv), for anim=1 of the R lines. NativeTwin_AnimatedSlots
// (platform/native_twin.c) names the slots the way the import does.
#define EO_ANIM_LAYOUTS_MAX 64
internal int EngOracle_AnimLayouts(const struct ModelHeader *mh, EngineOracleFace *out, int outMax)
{
	u8 animated[NATIVE_TWIN_LAYOUT_SLOTS];
	struct NativeTwinCount count;
	int n = 0;

	if ((mh == NULL) || (mh->ptrTexLayout == NULL) || (mh->animtex == NULL))
	{
		return 0;
	}
	memset(&count, 0, sizeof(count));
	NativeTwin_AnimatedSlots(mh, NATIVE_TWIN_LAYOUT_SLOTS, animated, &count);
	for (int i = 0; (i < NATIVE_TWIN_LAYOUT_SLOTS) && (n < outMax); i++)
	{
		const struct TextureLayout *t = animated[i] ? mh->ptrTexLayout[i] : NULL;
		EngineOracleFace *f;

		if (t == NULL)
		{
			continue;
		}
		f = &out[n++];
		memset(f, 0, sizeof(*f));
		f->textured = 1;
		EngOracle_TpageToFace(t->tpage, f);
		EngOracle_ClutToFace(t->clut, f);
		f->uvCount = 3;
		f->uv[0] = t->u0;
		f->uv[1] = t->v0;
		f->uv[2] = t->u1;
		f->uv[3] = t->v1;
		f->uv[4] = t->u2;
		f->uv[5] = t->v2;
	}
	return n;
}

internal int EngOracle_SameLayout(const EngineOracleFace *a, const EngineOracleFace *b)
{
	if (!a->textured || !b->textured || (a->tpX != b->tpX) || (a->tpY != b->tpY) || (a->depth != b->depth))
	{
		return 0;
	}
	if ((a->depth < 2) && ((a->clutX != b->clutX) || (a->clutY != b->clutY)))
	{
		return 0;
	}
	for (int i = 0; i < 6; i++)
	{
		if (a->uv[i] != b->uv[i])
		{
			return 0;
		}
	}
	return 1;
}

int Engine_OracleRetailRange(char src, const char *srcId, const void *begin, const void *end, const char *extra)
{
	return Engine_OracleRetailRangeModel(src, srcId, begin, end, extra, NULL);
}

int Engine_OracleRetailRangeModel(char src, const char *srcId, const void *begin, const void *end, const char *extra, const struct ModelHeader *mh)
{
	const u8 *p = (const u8 *)begin;
	const u8 *stop = (const u8 *)end;
	u32 drawMode = 0xffffffffu;
	int sub = 0;
	EngineOracleFace anim[EO_ANIM_LAYOUTS_MAX];
	int animCount;

	if (!s_eoActive || (p == NULL) || (stop == NULL) || (stop <= p))
	{
		return 0;
	}
	animCount = EngOracle_AnimLayouts(mh, anim, EO_ANIM_LAYOUTS_MAX);
	while (p + 4 <= stop)
	{
		u32 tag;
		u32 len;
		EngineOracleFace faces[EO_DECODE_MAX];
		int n;

		memcpy(&tag, p, sizeof(tag));
		len = tag >> 24;
		if (p + 4 + (size_t)len * 4u > stop)
		{
			break;
		}
		n = Engine_OracleDecodePacket((const u32 *)(const void *)(p + 4), (int)len, &drawMode, faces, EO_DECODE_MAX);
		for (int k = 0; k < n; k++)
		{
			char add[ENGINE_ORACLE_EXTRA_MAX];

			faces[k].src = src;
			EngOracle_CopyField(faces[k].srcId, sizeof(faces[k].srcId), srcId);
			faces[k].sub = sub++;
			snprintf(add, sizeof(add), "%s", faces[k].extra);
			faces[k].extra[0] = '\0';
			EngOracle_Append(faces[k].extra, sizeof(faces[k].extra), extra);
			EngOracle_Append(faces[k].extra, sizeof(faces[k].extra), add);
			for (int a = 0; a < animCount; a++)
			{
				if (EngOracle_SameLayout(&faces[k], &anim[a]))
				{
					EngOracle_Append(faces[k].extra, sizeof(faces[k].extra), "anim=1");
					break;
				}
			}
			Engine_OracleWrite('R', &faces[k]);
		}
		p += 4 + (size_t)len * 4u;
	}
	return sub;
}

// --------------------------------------------------------------------------
// Engine: draws to faces.

void Engine_OracleFaceFromMaterial(const EngineMaterial *m, EngineOracleFace *f)
{
	if ((m == NULL) || (f == NULL))
	{
		return;
	}
	f->textured = 0;
	f->clutX = -1;
	f->clutY = -1;
	switch (m->texSource)
	{
	case ENGINE_TEX_VRAM:
	case ENGINE_TEX_VRAM_TABLE:
		f->textured = 1;
		f->raw = 1;
		f->rawTpage = m->tpage;
		f->rawClut = m->clut;
		EngOracle_TpageToFace(m->tpage, f);
		EngOracle_ClutToFace(m->clut, f);
		if (m->texSource == ENGINE_TEX_VRAM_TABLE)
		{
			EngOracle_Append(f->extra, sizeof(f->extra), "table=1");
		}
		break;
	case ENGINE_TEX_NATIVE:
		EngOracle_Append(f->extra, sizeof(f->extra), "tex=native");
		break;
	default:
		break;
	}
	switch (m->blend)
	{
	case ENGINE_BLEND_PSX_AVERAGE:
		f->blend = 0;
		break;
	case ENGINE_BLEND_PSX_ADD:
		f->blend = 1;
		break;
	case ENGINE_BLEND_PSX_SUBTRACT:
		f->blend = 2;
		break;
	case ENGINE_BLEND_PSX_ADD_QUARTER:
		f->blend = 3;
		break;
	case ENGINE_BLEND_MULTIPLY:
		f->blend = ENGINE_ORACLE_BLEND_OPAQUE;
		EngOracle_Append(f->extra, sizeof(f->extra), "blend=multiply");
		break;
	default:
		f->blend = ENGINE_ORACLE_BLEND_OPAQUE;
		break;
	}
}

typedef void (*EngOracleEmitFn)(const EngineOracleFace *f, int material, void *user);

internal int EngOracle_Round(float x)
{
	return (int)floorf(x + 0.5f);
}

void Engine_OracleModulatedColour(const u8 colour[4], const float materialTint[4], const float drawTint[4], u8 out[3])
{
	for (int c = 0; c < 3; c++)
	{
		// The vertex stage of "ne": v_color = a_color * tint (platform/native_shaders.inc, engine vertex
		// stage), tint = material->tint * draw->tint (engine_render.c, Engine_RenderDrawRange).
		const float value = (float)colour[c] * materialTint[c] * drawTint[c];
		const int rounded = EngOracle_Round(value);

		out[c] = (u8)((rounded < 0) ? 0 : ((rounded > 255) ? 255 : rounded));
	}
}

// The corner colours of an engine triangle as the program modulates with them
// (Engine_OracleModulatedColour: vertex x material tint x draw tint) and, for a
// VRAM face (not the mosaic table, P2 says that one), the texel box the shader
// can reach.
internal void EngOracle_FinishEngineFace(EngineOracleFace *f, const EngineMaterial *m, const float *drawTint, const EngineVertex *v0, const EngineVertex *v1,
                                         const EngineVertex *v2)
{
	const EngineVertex *v[3];

	v[0] = v0;
	v[1] = v1;
	v[2] = v2;
	f->colorCount = 3;
	for (int k = 0; k < 3; k++)
	{
		Engine_OracleModulatedColour(v[k]->color, m->tint, drawTint, f->color[k]);
	}
	if ((m->texSource == ENGINE_TEX_VRAM) && (f->uvCount == 3))
	{
		int umin = f->uv[0];
		int umax = f->uv[0];
		int vmin = f->uv[1];
		int vmax = f->uv[1];
		char box[64];

		for (int k = 1; k < 3; k++)
		{
			umin = (f->uv[k * 2] < umin) ? f->uv[k * 2] : umin;
			umax = (f->uv[k * 2] > umax) ? f->uv[k * 2] : umax;
			vmin = (f->uv[k * 2 + 1] < vmin) ? f->uv[k * 2 + 1] : vmin;
			vmax = (f->uv[k * 2 + 1] > vmax) ? f->uv[k * 2 + 1] : vmax;
		}
		if (ENGINE_ORACLE_SAMPLE_CLAMPED)
		{
			// What the program clamps to: the provoking (first) vertex's box (a zeroed
			// box is the whole page).
			int clamp[4];

			Engine_VertexBoxGet(v[0], clamp);
			umin = clamp[0];
			vmin = clamp[1];
			umax = clamp[2];
			vmax = clamp[3];
		}
		else
		{
			umin--;
			vmin--;
		}
		snprintf(box, sizeof(box), "box=%d,%d,%d,%d", umin, vmin, umax, vmax);
		EngOracle_Append(f->extra, sizeof(f->extra), box);
	}
}

// One face per triangle of the draw, the material as the renderer picks it
// (Engine_DrawMarker: dynamic geometry dynMaterial; a mesh per range the
// override, else the range's material).
internal int EngOracle_DrawFaces(const EngineDraw *draw, const char *dynTag, EngOracleEmitFn emit, void *user)
{
	int faces = 0;

	if (draw == NULL)
	{
		return 0;
	}
	if (draw->dynVertexCount > 0u)
	{
		// The renderer draws dynamic geometry with dynMaterial (materialOverride is not read there).
		const int material = draw->dynMaterial;
		const EngineMaterial *m = Engine_Material(material);
		const u32 triangles = draw->dynIndexCount / 3u;

		if ((m == NULL) || (draw->dynVertices == NULL) || (draw->dynIndices == NULL))
		{
			return 0;
		}
		for (u32 t = 0; t < triangles; t++)
		{
			EngineOracleFace f;

			memset(&f, 0, sizeof(f));
			f.cellRow = -1;
			f.cellCol = -1;
			Engine_OracleFaceFromMaterial(m, &f);
			EngOracle_Append(f.extra, sizeof(f.extra), dynTag);
			f.uvCount = 3;
			for (int k = 0; k < 3; k++)
			{
				const u16 idx = draw->dynIndices[t * 3u + (u32)k];

				if (idx >= draw->dynVertexCount)
				{
					f.uvCount = 0;
					break;
				}
				f.uv[k * 2] = EngOracle_Round(draw->dynVertices[idx].texcoord[0]);
				f.uv[k * 2 + 1] = EngOracle_Round(draw->dynVertices[idx].texcoord[1]);
			}
			if (f.uvCount == 3)
			{
				EngOracle_FinishEngineFace(&f, m, draw->tint, &draw->dynVertices[draw->dynIndices[t * 3u]], &draw->dynVertices[draw->dynIndices[t * 3u + 1u]],
				                           &draw->dynVertices[draw->dynIndices[t * 3u + 2u]]);
				// A back-side copy of a two-sided lit/key triangle (platform/engine_inst_light.c).
				if (Engine_InstLightBackFace(&draw->dynVertices[draw->dynIndices[t * 3u]]))
				{
					EngOracle_Append(f.extra, sizeof(f.extra), "side=back");
				}
			}
			emit(&f, material, user);
			faces++;
		}
		return faces;
	}
	{
		const EngineMeshDesc *desc = Engine_MeshDesc(draw->mesh);
		const int external = Engine_MeshIsExternal(draw->mesh);
		const EngineVertex *base = NULL;

		if ((desc == NULL) || (desc->ranges == NULL))
		{
			return 0;
		}
		if (!external && (desc->vertices != NULL) && (desc->indices != NULL) && (draw->frame < desc->frames))
		{
			base = desc->vertices + (size_t)draw->frame * desc->verticesPerFrame;
		}
		for (u32 r = 0; r < desc->rangeCount; r++)
		{
			const EngineRange *range = &desc->ranges[r];
			const int material = (draw->materialOverride >= 0) ? draw->materialOverride : (int)range->material;
			const EngineMaterial *m = Engine_Material(material);

			// What the renderer draws: not a range the draw leaves out (Engine_ModelRangeSkip).
			if ((m == NULL) || Engine_RangeSkipped(draw->rangeSkip, r))
			{
				continue;
			}
			if (base == NULL)
			{
				// An external mesh (no CPU copy) or a frame out of range: one line per range.
				EngineOracleFace f;

				memset(&f, 0, sizeof(f));
				f.cellRow = -1;
				f.cellCol = -1;
				Engine_OracleFaceFromMaterial(m, &f);
				EngOracle_Append(f.extra, sizeof(f.extra), external ? "external=1" : "frame=out");
				emit(&f, material, user);
				faces++;
				continue;
			}
			for (u32 t = 0; t + 3u <= range->indexCount; t += 3u)
			{
				EngineOracleFace f;

				if (range->firstIndex + t + 3u > desc->indexCount)
				{
					break;
				}
				memset(&f, 0, sizeof(f));
				f.cellRow = -1;
				f.cellCol = -1;
				Engine_OracleFaceFromMaterial(m, &f);
				f.uvCount = 3;
				for (int k = 0; k < 3; k++)
				{
					const u16 idx = desc->indices[range->firstIndex + t + (u32)k];

					if (idx >= desc->verticesPerFrame)
					{
						f.uvCount = 0;
						break;
					}
					f.uv[k * 2] = EngOracle_Round(base[idx].texcoord[0]);
					f.uv[k * 2 + 1] = EngOracle_Round(base[idx].texcoord[1]);
				}
				if (f.uvCount == 3)
				{
					const u16 *tri = &desc->indices[range->firstIndex + t];
					const u32 triangle = (range->firstIndex + t) / 3u;

					EngOracle_FinishEngineFace(&f, m, draw->tint, &base[tri[0]], &base[tri[1]], &base[tri[2]]);
					if ((draw->mesh >= 0) && (draw->mesh < ENGINE_MESHES_MAX) && (s_eoMeshAnim[draw->mesh] != NULL) &&
					    (triangle < s_eoMeshAnimCount[draw->mesh]) && s_eoMeshAnim[draw->mesh][triangle])
					{
						EngOracle_Append(f.extra, sizeof(f.extra), "anim=1");
					}
				}
				emit(&f, material, user);
				faces++;
			}
		}
	}
	return faces;
}

// The W3 fault on a face of the material that carries it: the clut it is drawn with, and fault=clut.
internal void EngOracle_ApplyFault(EngineOracleFace *f, int material)
{
	if ((g_engineOracleFaultMaterial < 0) || (material != g_engineOracleFaultMaterial))
	{
		return;
	}
	EngOracle_ClutToFace(s_eoFaultMat.clut, f);
	f->rawClut = s_eoFaultMat.clut;
	EngOracle_Append(f->extra, sizeof(f->extra), "fault=clut");
}

typedef struct
{
	char src;
	const char *srcId;
	const char *extra;
	int sub;
} EngOracleDirect;

internal void EngOracle_EmitDirect(const EngineOracleFace *face, int material, void *user)
{
	EngOracleDirect *d = (EngOracleDirect *)user;
	EngineOracleFace f = *face;
	char add[ENGINE_ORACLE_EXTRA_MAX];

	f.src = d->src;
	EngOracle_CopyField(f.srcId, sizeof(f.srcId), d->srcId);
	f.sub = d->sub++;
	snprintf(add, sizeof(add), "%s", f.extra);
	f.extra[0] = '\0';
	EngOracle_Append(f.extra, sizeof(f.extra), d->extra);
	EngOracle_Append(f.extra, sizeof(f.extra), add);
	EngOracle_ApplyFault(&f, material);
	Engine_OracleWrite('E', &f);
}

int Engine_OracleEngineDraw(char src, const char *srcId, int view, const EngineDraw *draw, const char *extra)
{
	EngOracleDirect d;
	char add[ENGINE_ORACLE_EXTRA_MAX];

	if (!s_eoActive || (draw == NULL))
	{
		return 0;
	}
	add[0] = '\0';
	EngOracle_Append(add, sizeof(add), extra);
	if (view > 0)
	{
		char v[16];

		snprintf(v, sizeof(v), "view=%d", view);
		EngOracle_Append(add, sizeof(add), v);
	}
	d.src = src;
	d.srcId = srcId;
	d.extra = add;
	d.sub = 0;
	return EngOracle_DrawFaces(draw, "dyn=1", EngOracle_EmitDirect, &d);
}

internal void EngOracle_EmitBuffered(const EngineOracleFace *face, int material, void *user)
{
	(void)user;
	if (s_eoBufCount >= s_eoBufCap)
	{
		const int cap = (s_eoBufCap == 0) ? 4096 : s_eoBufCap * 2;
		EngOracleBuffered *grown = (cap <= (1 << 22)) ? (EngOracleBuffered *)realloc(s_eoBuf, (size_t)cap * sizeof(*s_eoBuf)) : NULL;

		if (grown == NULL)
		{
			s_eoBufFull = 1;
			return;
		}
		s_eoBuf = grown;
		s_eoBufCap = cap;
	}
	s_eoBuf[s_eoBufCount].face = *face;
	s_eoBuf[s_eoBufCount].material = material;
	s_eoBufCount++;
}

void Engine_OracleSourceBegin(const char *srcId)
{
	if (!s_eoActive)
	{
		return;
	}
	if (s_eoSourceDepth++ > 0)
	{
		return; // nested: the faces go to the source already open
	}
	EngOracle_CopyField(s_eoSourceId, sizeof(s_eoSourceId), srcId);
	s_eoBufCount = 0;
	s_eoSourceHires = 0;
}

void Engine_OracleNoteHires(void)
{
	if (s_eoActive && (s_eoSourceDepth > 0))
	{
		s_eoSourceHires = 1;
	}
}

void Engine_OracleNoteSubmit(int view, const EngineDraw *draw)
{
	(void)view;
	if (!s_eoActive || (s_eoSourceDepth <= 0))
	{
		return;
	}
	// Dynamic geometry of an instance source is its live frame (EngInst_SubmitLinked: the start banner).
	EngOracle_DrawFaces(draw, "live=1", EngOracle_EmitBuffered, NULL);
}

// W3: picks the n-th distinct VRAM material with a CLUT of the instance faces of this armed frame.
internal void EngOracle_PickFault(void)
{
	if ((g_cfg_engineOracleFaultClut < 0) || (g_engineOracleFaultMaterial >= 0))
	{
		return;
	}
	for (int i = 0; i < s_eoBufCount; i++)
	{
		const int material = s_eoBuf[i].material;
		const EngineMaterial *m = Engine_Material(material);
		EngineTpage tp;
		int clutX;
		int clutY;

		if ((m == NULL) || (m->texSource != ENGINE_TEX_VRAM) || (material >= ENGINE_MATERIALS_MAX))
		{
			continue;
		}
		tp = Engine_DecodeTpage(m->tpage);
		if (tp.depth >= 2)
		{
			continue;
		}
		if (s_eoFaultSeen[material >> 3] & (u8)(1u << (material & 7)))
		{
			continue;
		}
		s_eoFaultSeen[material >> 3] |= (u8)(1u << (material & 7));
		if (s_eoFaultDistinct++ != g_cfg_engineOracleFaultClut)
		{
			continue;
		}
		Engine_DecodeClut(m->clut, &clutX, &clutY);
		s_eoFaultMat = *m;
		// One CLUT row lower (higher at the last row): another palette, the same layout.
		s_eoFaultMat.clut = (u16)((m->clut & 0x3fu) | ((u16)((clutY < 511) ? (clutY + 1) : (clutY - 1)) << 6));
		g_engineOracleFaultMaterial = material;
		Platform_Log("[CTR Engine] oracle fault: material %d of '%s' (tpage 0x%04x) drawn with clut 0x%04x instead of 0x%04x (%d,%d -> %d,%d) "
		             "from frame '%s' until the level unloads\n",
		             material, s_eoSourceId, (unsigned)m->tpage, (unsigned)s_eoFaultMat.clut, (unsigned)m->clut, clutX, clutY, clutX,
		             (clutY < 511) ? (clutY + 1) : (clutY - 1), s_eoTag);
		return;
	}
}

void Engine_OracleSourceEnd(char src, const char *extra)
{
	if (!s_eoActive || (s_eoSourceDepth <= 0))
	{
		return;
	}
	if (--s_eoSourceDepth > 0)
	{
		return;
	}
	if (src == 'I')
	{
		EngOracle_PickFault();
	}
	for (int i = 0; i < s_eoBufCount; i++)
	{
		EngineOracleFace f = s_eoBuf[i].face;
		char add[ENGINE_ORACLE_EXTRA_MAX];

		f.src = src;
		EngOracle_CopyField(f.srcId, sizeof(f.srcId), s_eoSourceId);
		f.sub = i;
		snprintf(add, sizeof(add), "%s", f.extra);
		f.extra[0] = '\0';
		EngOracle_Append(f.extra, sizeof(f.extra), extra);
		EngOracle_Append(f.extra, sizeof(f.extra), add);
		if (s_eoSourceHires)
		{
			EngOracle_Append(f.extra, sizeof(f.extra), "hires=1");
		}
		EngOracle_ApplyFault(&f, s_eoBuf[i].material);
		Engine_OracleWrite('E', &f);
	}
	s_eoBufCount = 0;
}

// --------------------------------------------------------------------------
// Instances.

void Engine_OracleInstanceTags(const struct Instance *inst, const struct InstDrawPerPlayer *idpp, const struct PushBuffer *pb, char *src, char *id, int idSize,
                               char *retailExtra, int retailSize, char *engineExtra, int engineSize)
{
	const struct GameTracker *gGT = sdata->gGT;
	const char *name = "?";
	char nameBuf[17];
	int view;
	char add[32];

	if (id != NULL && idSize > 0)
	{
		id[0] = '\0';
	}
	if (retailExtra != NULL && retailSize > 0)
	{
		retailExtra[0] = '\0';
	}
	if (engineExtra != NULL && engineSize > 0)
	{
		engineExtra[0] = '\0';
	}
	if (inst == NULL)
	{
		return;
	}
	if (inst->model != NULL)
	{
		memcpy(nameBuf, inst->model->name, 16);
		nameBuf[16] = '\0';
		name = nameBuf;
	}

	if (src != NULL)
	{
		*src = 'I';
	}
	// A driver's body: src D, srcId its seat.
	for (int seat = 0; (gGT != NULL) && (seat < 8); seat++)
	{
		if ((gGT->drivers[seat] != NULL) && (gGT->drivers[seat]->instSelf == inst))
		{
			if (src != NULL)
			{
				*src = 'D';
			}
			if (id != NULL)
			{
				snprintf(id, (size_t)idSize, "%d", seat);
				id = NULL; // named
			}
			break;
		}
	}

	if (id != NULL)
	{
		const struct Level *level = (gGT != NULL) ? gGT->level1 : NULL;
		const struct JitPool *pool = (gGT != NULL) ? &gGT->JitPools.instance : NULL;
		char tmp[ENGINE_ORACLE_ID_MAX];

		if ((inst->instDef != NULL) && (level != NULL) && (level->ptrInstDefs != NULL) && (inst->instDef >= level->ptrInstDefs) &&
		    (inst->instDef < level->ptrInstDefs + level->numInstances))
		{
			// A level instance: its InstDef index in the LEV, the same in every run.
			snprintf(tmp, sizeof(tmp), "%s#%d", name, (int)(inst->instDef - level->ptrInstDefs));
		}
		else if ((pool != NULL) && (pool->ptrPoolData != NULL) && (pool->itemSize > 0u) && ((const u8 *)inst >= (const u8 *)pool->ptrPoolData) &&
		         ((const u8 *)inst < (const u8 *)pool->ptrPoolData + (size_t)pool->itemSize * (size_t)((pool->maxItems > 0) ? pool->maxItems : 0)))
		{
			// A runtime instance: its slot in the instance pool.
			snprintf(tmp, sizeof(tmp), "%s#r%d", name, (int)(((const u8 *)inst - (const u8 *)pool->ptrPoolData) / pool->itemSize));
		}
		else
		{
			snprintf(tmp, sizeof(tmp), "%s#p%08x", name, (unsigned)(size_t)inst);
		}
		EngOracle_CopyField(id, idSize, tmp);
	}

	view = Engine_InstanceViewOf(pb);
	if ((retailExtra != NULL) && (idpp != NULL) && (inst->model != NULL) && (inst->model->headers != NULL) && (idpp->mh != NULL))
	{
		const int lod = (int)(idpp->mh - inst->model->headers);

		if ((lod >= 0) && (lod < inst->model->numHeaders))
		{
			snprintf(add, sizeof(add), "lod=%d", lod);
			EngOracle_Append(retailExtra, retailSize, add);
		}
	}
	// The engine imports and draws header 0 of every model (engine_model.c).
	EngOracle_Append(engineExtra, engineSize, "lod=0");
	if (view > 0)
	{
		snprintf(add, sizeof(add), "view=%d", view);
		EngOracle_Append(retailExtra, retailSize, add);
		EngOracle_Append(engineExtra, engineSize, add);
	}
}

void Engine_OracleCountersGet(u32 *frames, u32 *linesR, u32 *linesE, u32 *dumps)
{
	if (frames != NULL)
	{
		*frames = s_eoFrames;
	}
	if (linesR != NULL)
	{
		*linesR = s_eoTotalR;
	}
	if (linesE != NULL)
	{
		*linesE = s_eoTotalE;
	}
	if (dumps != NULL)
	{
		*dumps = s_eoDumps;
	}
}

void Engine_OracleSetCommandLine(int argc, char **argv)
{
	int pos = 0;

	s_eoArgs[0] = '\0';
	for (int i = 1; (argv != NULL) && (i < argc) && (pos < (int)sizeof(s_eoArgs) - 2); i++)
	{
		pos += snprintf(s_eoArgs + pos, sizeof(s_eoArgs) - (size_t)pos, "%s%s", (i > 1) ? " " : "", (argv[i] != NULL) ? argv[i] : "");
	}
	// Tabs would break the columns.
	for (int i = 0; s_eoArgs[i] != '\0'; i++)
	{
		if ((s_eoArgs[i] == '\t') || (s_eoArgs[i] == '\n') || (s_eoArgs[i] == '\r'))
		{
			s_eoArgs[i] = ' ';
		}
	}
}

void Engine_OracleNoteVramWrite(int kind, int x, int y, int w, int h, int fromX, int fromY)
{
	local_persist const char *const names[3] = {"LoadImage", "MoveImage", "ClearImage"};
	const int race = s_eoInRace ? 1 : 0;
	const struct GameTracker *gGT = sdata->gGT;

	if ((g_cfg_engineOracle[0] == '\0') || (kind < 0) || (kind > 2))
	{
		return;
	}
	s_eoVramCount[race][kind]++;
	if ((kind == 0) && (w > 0) && (h > 0))
	{
		s_eoVramWords[race] += (u32)w * (u32)h;
	}
	if (!race)
	{
		return;
	}
	// One line per call in race frames (FM D-I.3a): the rectangle, the source of
	// a move, the game frame. The caller is not named: native_libgpu.c is the
	// PsyQ facade every game writer goes through, and a stack walk would need
	// compiler-specific code; the rectangle and the frame identify the writer.
	if ((s_eoVramLinesEntry >= ENGINE_ORACLE_VRAM_LINES_ENTRY) || (s_eoVramLinesRun >= ENGINE_ORACLE_VRAM_LINES_RUN))
	{
		s_eoVramLinesDropped++;
		return;
	}
	s_eoVramLinesEntry++;
	s_eoVramLinesRun++;
	if (kind == 1)
	{
		Platform_Log("[CTR Engine] oracle: vram write in race frame %d, entry %d level %d: %s %d,%d %dx%d from %d,%d\n",
		             (gGT != NULL) ? gGT->framesInThisLEV : -1, s_eoVramEntry, s_eoVramLevel, names[kind], x, y, w, h, fromX, fromY);
	}
	else
	{
		Platform_Log("[CTR Engine] oracle: vram write in race frame %d, entry %d level %d: %s %d,%d %dx%d\n", (gGT != NULL) ? gGT->framesInThisLEV : -1,
		             s_eoVramEntry, s_eoVramLevel, names[kind], x, y, w, h);
	}
}

// The tour's report (Engine_TourNotify): phase 0 = the first race frame of an
// entry, phase 1 = after its last. The VRAM writes between them are the ones
// of race frames.
internal void EngOracle_TourReport(int entry, int levelID, int phase)
{
	if (phase == 0)
	{
		if (s_eoVramCount[0][0] + s_eoVramCount[0][1] + s_eoVramCount[0][2] > 0u)
		{
			Platform_Log("[CTR Engine] oracle: vram writes outside race frames before entry %d level %d: LoadImage %u (%u words), MoveImage %u, ClearImage %u\n",
			             entry, levelID, s_eoVramCount[0][0], s_eoVramWords[0], s_eoVramCount[0][1], s_eoVramCount[0][2]);
		}
		memset(s_eoVramCount, 0, sizeof(s_eoVramCount));
		memset(s_eoVramWords, 0, sizeof(s_eoVramWords));
		s_eoVramEntry = entry;
		s_eoVramLevel = levelID;
		s_eoVramLinesEntry = 0;
		s_eoVramLinesDropped = 0;
		s_eoInRace = 1;
		return;
	}
	s_eoInRace = 0;
	Platform_Log("[CTR Engine] oracle: vram writes in race frames, entry %d level %d: LoadImage %u (%u words), MoveImage %u, ClearImage %u; "
	             "%u call line(s) not written (cap %d per entry, %d per run)\n",
	             entry, levelID, s_eoVramCount[1][0], s_eoVramWords[1], s_eoVramCount[1][1], s_eoVramCount[1][2], s_eoVramLinesDropped,
	             ENGINE_ORACLE_VRAM_LINES_ENTRY, ENGINE_ORACLE_VRAM_LINES_RUN);
	memset(s_eoVramCount, 0, sizeof(s_eoVramCount));
	memset(s_eoVramWords, 0, sizeof(s_eoVramWords));
}

void Engine_OracleLoadStage(void)
{
	if (!g_cfg_engine || (g_cfg_engineOracle[0] == '\0') || s_eoTourRegistered)
	{
		return;
	}
	s_eoTourRegistered = Engine_TourRegister(EngOracle_TourReport);
}

void Engine_OracleStateSave(int slot)
{
	if ((slot < 0) || (slot > 1))
	{
		return;
	}
	s_eoGte[slot] = gteRegs;
	memcpy(s_eoScratch[slot], CTR_SCRATCHPAD_BASE, CTR_SCRATCHPAD_SIZE);
}

void Engine_OracleStateRestore(int slot)
{
	if ((slot < 0) || (slot > 1))
	{
		return;
	}
	gteRegs = s_eoGte[slot];
	memcpy(CTR_SCRATCHPAD_BASE, s_eoScratch[slot], CTR_SCRATCHPAD_SIZE);
}

// --------------------------------------------------------------------------
// Selftest: the packet decoder on synthetic primitives.

#define EO_CHECK(cond, ...)                              \
	do                                                   \
	{                                                    \
		(*checks)++;                                     \
		if (!(cond))                                     \
		{                                                \
			printf("engine selftest: oracle " __VA_ARGS__); \
			printf("\n");                                \
			failures++;                                  \
		}                                                \
	} while (0)

int Engine_SelfTestOracle(int *checks)
{
	int failures = 0;
	EngineOracleFace faces[4];
	u32 mode;
	int n;
	const u32 clut = 50u | (286u << 6);

	// a) POLY_G3.
	{
		const u32 w[6] = {0x30112233u, 0x00000000u, 0x00445566u, 0x00010001u, 0x00778899u, 0x00020002u};

		memset(faces, 0, sizeof(faces));
		mode = 0xffffffffu;
		n = Engine_OracleDecodePacket(w, 6, &mode, faces, 4);
		EO_CHECK(n == 1, "G3: %d faces, want 1", n);
		EO_CHECK(faces[0].textured == 0, "G3: textured %d", faces[0].textured);
		EO_CHECK(faces[0].clutX == -1, "G3: clutX %d", faces[0].clutX);
		EO_CHECK(faces[0].blend == ENGINE_ORACLE_BLEND_OPAQUE, "G3: blend %d", faces[0].blend);
		EO_CHECK(faces[0].colorCount == 3, "G3: colorCount %d", faces[0].colorCount);
		EO_CHECK((faces[0].color[0][0] == 0x33) && (faces[0].color[0][1] == 0x22) && (faces[0].color[0][2] == 0x11), "G3: colour 0");
		EO_CHECK((faces[0].color[1][0] == 0x66) && (faces[0].color[1][1] == 0x55) && (faces[0].color[1][2] == 0x44), "G3: colour 1");
		EO_CHECK((faces[0].color[2][0] == 0x99) && (faces[0].color[2][1] == 0x88) && (faces[0].color[2][2] == 0x77), "G3: colour 2");
	}

	// b) POLY_GT3 opaque.
	{
		const u32 w[9] = {0x34808080u, 0x0u, 252u | (228u << 8) | (clut << 16), 0x00808080u, 0x0u, 255u | (228u << 8) | (0x7eu << 16),
		                  0x00808080u, 0x0u, 252u | (231u << 8)};

		memset(faces, 0, sizeof(faces));
		mode = 0xffffffffu;
		n = Engine_OracleDecodePacket(w, 9, &mode, faces, 4);
		EO_CHECK(n == 1, "GT3: %d faces, want 1", n);
		EO_CHECK(faces[0].textured == 1, "GT3: textured %d", faces[0].textured);
		EO_CHECK((faces[0].tpX == 896) && (faces[0].tpY == 256) && (faces[0].depth == 0), "GT3: page %d,%d,%d", faces[0].tpX, faces[0].tpY,
		         faces[0].depth);
		EO_CHECK((faces[0].clutX == 800) && (faces[0].clutY == 286), "GT3: clut %d,%d", faces[0].clutX, faces[0].clutY);
		EO_CHECK(faces[0].uvCount == 3, "GT3: uvCount %d", faces[0].uvCount);
		EO_CHECK((faces[0].uv[0] == 252) && (faces[0].uv[1] == 228) && (faces[0].uv[2] == 255) && (faces[0].uv[3] == 228) &&
		             (faces[0].uv[4] == 252) && (faces[0].uv[5] == 231),
		         "GT3: uv");
		EO_CHECK(faces[0].blend == ENGINE_ORACLE_BLEND_OPAQUE, "GT3: blend %d", faces[0].blend);
		EO_CHECK((mode & 0x1ffu) == 0x7eu, "GT3: drawMode 0x%x", (unsigned)mode);
	}

	// c) POLY_GT3 semi-transparent.
	{
		const u32 tp = 0x38u | (1u << 5);
		const u32 w[9] = {0x36808080u, 0x0u, 1u | (2u << 8) | (clut << 16), 0x00808080u, 0x0u, 3u | (4u << 8) | (tp << 16),
		                  0x00808080u, 0x0u, 5u | (6u << 8)};

		memset(faces, 0, sizeof(faces));
		mode = 0xffffffffu;
		n = Engine_OracleDecodePacket(w, 9, &mode, faces, 4);
		EO_CHECK(n == 1, "GT3 semi: %d faces", n);
		EO_CHECK((faces[0].tpX == 512) && (faces[0].tpY == 256), "GT3 semi: page %d,%d", faces[0].tpX, faces[0].tpY);
		EO_CHECK(faces[0].blend == 1, "GT3 semi: blend %d", faces[0].blend);
	}

	// d) POLY_GT4.
	{
		const u32 w[12] = {0x3c808080u, 0x0u, 10u | (11u << 8) | (clut << 16), 0x00808080u, 0x0u, 20u | (21u << 8) | (0x7eu << 16),
		                   0x00808080u, 0x0u, 30u | (31u << 8), 0x00808080u, 0x0u, 40u | (41u << 8)};

		memset(faces, 0, sizeof(faces));
		mode = 0xffffffffu;
		n = Engine_OracleDecodePacket(w, 12, &mode, faces, 4);
		EO_CHECK(n == 1, "GT4: %d faces", n);
		EO_CHECK(faces[0].uvCount == 4, "GT4: uvCount %d", faces[0].uvCount);
		EO_CHECK((faces[0].uv[0] == 10) && (faces[0].uv[1] == 11) && (faces[0].uv[2] == 20) && (faces[0].uv[3] == 21) && (faces[0].uv[4] == 30) &&
		             (faces[0].uv[5] == 31) && (faces[0].uv[6] == 40) && (faces[0].uv[7] == 41),
		         "GT4: uv");
		EO_CHECK(faces[0].colorCount == 4, "GT4: colorCount %d", faces[0].colorCount);
	}

	// e) Depth from the tpage.
	{
		u32 w[9] = {0x34808080u, 0x0u, 1u | (2u << 8) | (clut << 16), 0x00808080u, 0x0u, 3u | (4u << 8) | (0xfeu << 16), 0x00808080u, 0x0u, 5u | (6u << 8)};

		memset(faces, 0, sizeof(faces));
		mode = 0xffffffffu;
		n = Engine_OracleDecodePacket(w, 9, &mode, faces, 4);
		EO_CHECK(n == 1, "depth 1: %d faces", n);
		EO_CHECK(faces[0].depth == 1, "depth 1: depth %d", faces[0].depth);
		EO_CHECK((faces[0].clutX == 800) && (faces[0].clutY == 286), "depth 1: clut %d,%d", faces[0].clutX, faces[0].clutY);

		w[5] = 3u | (4u << 8) | (0x10eu << 16);
		memset(faces, 0, sizeof(faces));
		mode = 0xffffffffu;
		n = Engine_OracleDecodePacket(w, 9, &mode, faces, 4);
		EO_CHECK(n == 1, "depth 2: %d faces", n);
		EO_CHECK(faces[0].depth == 2, "depth 2: depth %d", faces[0].depth);
		EO_CHECK(faces[0].clutX == -1, "depth 2: clutX %d", faces[0].clutX);
	}

	// f) A ghost-like packet: draw mode, F3, draw mode, G3.
	{
		const u32 w[14] = {0xe1000a40u, 0x0u, 0x22102030u, 0x0u, 0x00010001u, 0x00020002u, 0xe1000a20u, 0x0u,
		                   0x30112233u, 0x0u, 0x00445566u, 0x0u, 0x00778899u, 0x0u};

		memset(faces, 0, sizeof(faces));
		mode = 0xffffffffu;
		n = Engine_OracleDecodePacket(w, 14, &mode, faces, 4);
		EO_CHECK(n == 2, "ghost: %d faces, want 2", n);
		EO_CHECK(faces[0].textured == 0, "ghost: face 0 textured %d", faces[0].textured);
		EO_CHECK(faces[0].blend == 2, "ghost: face 0 blend %d", faces[0].blend);
		EO_CHECK(faces[1].blend == ENGINE_ORACLE_BLEND_OPAQUE, "ghost: face 1 blend %d", faces[1].blend);
	}

	// g) Semi-transparent F3 with an unknown draw mode.
	{
		const u32 w[4] = {0x22102030u, 0x0u, 0x00010001u, 0x00020002u};

		memset(faces, 0, sizeof(faces));
		mode = 0xffffffffu;
		n = Engine_OracleDecodePacket(w, 4, &mode, faces, 4);
		EO_CHECK(n == 1, "F3 semi: %d faces", n);
		EO_CHECK(faces[0].blend == 0, "F3 semi: blend %d", faces[0].blend);
		EO_CHECK(strstr(faces[0].extra, "abr=unknown") != NULL, "F3 semi: extra '%s'", faces[0].extra);
	}

	// h) Truncated and unknown packets.
	{
		const u32 w[9] = {0x34808080u, 0x0u, 1u | (clut << 16), 0x00808080u, 0x0u, 3u | (0x7eu << 16), 0x00808080u, 0x0u, 5u};
		const u32 bad[2] = {0xff000000u, 0x0u};

		memset(faces, 0, sizeof(faces));
		mode = 0xffffffffu;
		n = Engine_OracleDecodePacket(w, 8, &mode, faces, 4);
		EO_CHECK(n == 0, "truncated GT3: %d faces, want 0", n);
		memset(faces, 0, sizeof(faces));
		n = Engine_OracleDecodePacket(bad, 2, &mode, faces, 4);
		EO_CHECK(n == 0, "unknown code: %d faces, want 0", n);
	}

	// i) Sprite 0x64 with a known draw mode.
	{
		const u32 w[4] = {0x64808080u, 0x0u, 16u | (32u << 8) | (clut << 16), 16u | (8u << 16)};

		memset(faces, 0, sizeof(faces));
		mode = 0x7eu;
		n = Engine_OracleDecodePacket(w, 4, &mode, faces, 4);
		EO_CHECK(n == 1, "sprite: %d faces", n);
		EO_CHECK(faces[0].uvCount == 4, "sprite: uvCount %d", faces[0].uvCount);
		EO_CHECK((faces[0].uv[0] == 16) && (faces[0].uv[1] == 32) && (faces[0].uv[2] == 32) && (faces[0].uv[3] == 32) && (faces[0].uv[4] == 16) &&
		             (faces[0].uv[5] == 40) && (faces[0].uv[6] == 32) && (faces[0].uv[7] == 40),
		         "sprite: corners");
		EO_CHECK(faces[0].tpX == 896, "sprite: tpX %d", faces[0].tpX);
		EO_CHECK(strstr(faces[0].extra, "sprite=1") != NULL, "sprite: extra '%s'", faces[0].extra);
	}

	// j) outMax limit.
	{
		const u32 w[18] = {0x34808080u, 0x0u, 1u | (clut << 16), 0x00808080u, 0x0u, 3u | (0x7eu << 16), 0x00808080u, 0x0u, 5u,
		                   0x34808080u, 0x0u, 1u | (clut << 16), 0x00808080u, 0x0u, 3u | (0x7eu << 16), 0x00808080u, 0x0u, 5u};

		memset(faces, 0, sizeof(faces));
		mode = 0xffffffffu;
		n = Engine_OracleDecodePacket(w, 18, &mode, faces, 1);
		EO_CHECK(n == 1, "outMax: %d faces, want 1", n);
	}

	// k) Faces from engine materials.
	{
		EngineMaterial m;
		EngineOracleFace f;

		memset(&m, 0, sizeof(m));
		memset(&f, 0, sizeof(f));
		m.texSource = ENGINE_TEX_VRAM;
		m.tpage = 0x7e;
		m.clut = (u16)clut;
		m.blend = ENGINE_BLEND_PSX_ADD;
		Engine_OracleFaceFromMaterial(&m, &f);
		EO_CHECK(f.textured == 1, "material VRAM: textured %d", f.textured);
		EO_CHECK(f.tpX == 896, "material VRAM: tpX %d", f.tpX);
		EO_CHECK(f.clutY == 286, "material VRAM: clutY %d", f.clutY);
		EO_CHECK(f.blend == 1, "material VRAM: blend %d", f.blend);

		memset(&m, 0, sizeof(m));
		memset(&f, 0, sizeof(f));
		m.texSource = ENGINE_TEX_NATIVE;
		Engine_OracleFaceFromMaterial(&m, &f);
		EO_CHECK(f.textured == 0, "material native: textured %d", f.textured);
		EO_CHECK(strstr(f.extra, "tex=native") != NULL, "material native: extra '%s'", f.extra);
	}

	// m) Without --engine-oracle every entry point is a no-op (D-W1.10): nothing armed, written or counted.
	if (g_cfg_engineOracle[0] == '\0')
	{
		u32 frames = 1;
		u32 linesR = 1;
		u32 linesE = 1;
		u32 dumps = 1;
		EngineOracleFace f;

		memset(&f, 0, sizeof(f));
		f.src = 'I';
		Engine_OracleArm("selftest");
		EO_CHECK(Engine_OracleActive() == 0, "armed without --engine-oracle");
		Engine_OracleWrite('R', &f);
		Engine_OracleWrite('E', &f);
		Engine_OracleSourceBegin("selftest");
		Engine_OracleSourceEnd('I', NULL);
		Engine_OracleNoteVramWrite(0, 0, 0, 4, 4, -1, -1);
		Engine_OracleFrameEnd();
		Engine_OracleCountersGet(&frames, &linesR, &linesE, &dumps);
		EO_CHECK((frames == 0u) && (linesR == 0u) && (linesE == 0u) && (dumps == 0u), "counters without the switch: frames %u, R %u, E %u, dumps %u",
		         frames, linesR, linesE, dumps);
		EO_CHECK((s_eoVramCount[0][0] == 0u) && (s_eoVramCount[1][0] == 0u), "vram writes counted without the switch");
		EO_CHECK(s_eoVramLinesRun == 0u, "vram write lines without the switch");
		EO_CHECK(g_engineOracleFaultMaterial == -1, "a fault material without the switch");
	}

	// n) The line format (the round's oracle format): tabs, '-' fields, extra pairs with one space, raw words, col=.
	{
		EngineOracleFace f;
		char line[512];
		int len;

		memset(&f, 0, sizeof(f));
		f.src = 'I';
		snprintf(f.srcId, sizeof(f.srcId), "tikka#3");
		f.sub = 7;
		f.textured = 1;
		f.tpX = 896;
		f.tpY = 256;
		f.depth = 0;
		f.clutX = 800;
		f.clutY = 286;
		f.uvCount = 3;
		f.uv[0] = 252;
		f.uv[1] = 228;
		f.uv[2] = 255;
		f.uv[3] = 228;
		f.uv[4] = 252;
		f.uv[5] = 231;
		f.blend = ENGINE_ORACLE_BLEND_OPAQUE;
		f.cellRow = -1;
		f.cellCol = -1;
		snprintf(f.extra, sizeof(f.extra), "  lod=0  anim=1 ");
		f.raw = 1;
		f.rawTpage = 0x7e;
		f.rawClut = (u16)clut;
		f.colorCount = 3;
		f.color[0][0] = 1;
		f.color[0][1] = 2;
		f.color[0][2] = 3;
		f.color[1][0] = 4;
		f.color[1][1] = 5;
		f.color[1][2] = 6;
		f.color[2][0] = 7;
		f.color[2][1] = 8;
		f.color[2][2] = 9;
		len = Engine_OracleFormatLine('E', &f, line, (int)sizeof(line));
		EO_CHECK((len > 0) && (strcmp(line, "E\tI\ttikka#3\t7\t896,256,0\t800,286\t252,228,255,228,252,231\to\t-\tlod=0 anim=1 tp=0x007e cl=0x47b2 col=1,2,3;4,5,6;7,8,9\n") == 0),
		         "format textured: '%s'", line);

		memset(&f, 0, sizeof(f));
		f.src = 'X';
		snprintf(f.srcId, sizeof(f.srcId), "shadow");
		f.blend = 2;
		f.cellRow = 1;
		f.cellCol = 2;
		len = Engine_OracleFormatLine('R', &f, line, (int)sizeof(line));
		EO_CHECK((len > 0) && (strcmp(line, "R\tX\tshadow\t0\t-\t-,-\t-\t2\t1,2\t-\n") == 0), "format untextured: '%s'", line);
		EO_CHECK(Engine_OracleFormatLine('Q', &f, line, (int)sizeof(line)) == 0, "format bad kind");
	}

	// o) The modulated colour of E col (vertex x material tint x draw tint, rounded, held to 0..255).
	{
		const u8 c[4] = {20, 60, 80, 255};
		const u8 white[4] = {255, 255, 255, 255};
		const float one[4] = {1.0f, 1.0f, 1.0f, 1.0f};
		const float fade[4] = {0.75f, 0.75f, 0.75f, 1.0f};
		const float boost[4] = {2.0f, 0.5f, 1.0f, 1.0f};
		u8 out[3];

		Engine_OracleModulatedColour(c, one, one, out);
		EO_CHECK((out[0] == 20) && (out[1] == 60) && (out[2] == 80), "colour x 1: %d,%d,%d", out[0], out[1], out[2]);
		Engine_OracleModulatedColour(c, one, fade, out);
		EO_CHECK((out[0] == 15) && (out[1] == 45) && (out[2] == 60), "colour x draw tint 0.75: %d,%d,%d", out[0], out[1], out[2]);
		Engine_OracleModulatedColour(c, fade, fade, out);
		EO_CHECK((out[0] == 11) && (out[1] == 34) && (out[2] == 45), "colour x both tints: %d,%d,%d", out[0], out[1], out[2]);
		Engine_OracleModulatedColour(white, boost, one, out);
		EO_CHECK((out[0] == 255) && (out[1] == 128) && (out[2] == 255), "colour held to 255: %d,%d,%d", out[0], out[1], out[2]);
	}

	// l) A retail range while the oracle is not armed writes nothing.
	{
		const u32 buf[8] = {(4u << 24), 0x22102030u, 0x0u, 0x00010001u, 0x00020002u, 0u, 0u, 0u};

		EO_CHECK(Engine_OracleRetailRange('I', "x", buf, buf + 5, NULL) == 0, "retail range while not armed returned non-zero");
	}

	return failures;
}

#undef EO_CHECK
