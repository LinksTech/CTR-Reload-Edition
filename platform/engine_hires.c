#include <platform/engine.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ENGINE HIRES - a hi-res driver, PROTOTYPE (round of 2026-10-15, stage H). See
// include/platform/engine.h, HI-RES DRIVER.
//
// WHAT IT PROVES: the engine can draw a driver with a real RGBA texture at full
// resolution (no VRAM palette, the mip chain of Engine_TextureCreate), real
// 3D wheels that spin with the speed, steer and follow the squash of the kart,
// all from game state. Only behind --engine --engine-hires-dir <dir> (--dev).
// The .rldchar format is NOT touched: the data comes from a local folder in a
// dev format of its own (written by tools/hires_testdriver.py, one synthetic
// test driver):
//   body.hrm, wheel.hrm  "HRM1", u32 vertexCount, u32 indexCount, then the
//                        vertices as struct EngineVertex (24 bytes: position,
//                        texcoord 0..1, colour), then u16 indices (triangles)
//   skin.hrt             "HRT1", u32 width, u32 height, RGBA8 rows from the top,
//                        sRGB colour (both edges a power of two in 16..2048)
// Model space of both meshes: x the kart's left, y up, z front, origin on the
// ground under the kart; units are the kart model units of
// Engine_InstanceWorldMatrix (world = t + R * scale / 4096 * p): the wheel
// middles of game/DrawTires.c lie at (+-36, 16, 49.75) and (+-36, 16, -24), a
// wheel is 16 units in radius.
//
// LOAD (stage 5, from Engine_DriverCmdlLoad, every level incl. the main menu):
// the files are read and the texture made ONCE per run (static host memory, a
// texture of Engine_TextureCreate with its levels, trilinear and anisotropic;
// --engine-hires-tex64
// makes it from a 64 x 64 box reduction of the file instead - the comparison
// run of H1); the two engine meshes and the material are made at every load
// (the renderer's level unload resets the tables).
//
// THE SEAT: ENG_HIRES_SEAT. Its driver is owned (Engine_DriverCmdlOwns answers 1,
// so Engine_FxOwnsDriver too: shadow, exhaust, flames and skids are the engine's)
// unless the seat has a native set the engine draws (a CNET custom driver: the
// layer's own route, hi-res off for the seat, logged at load).
//   BODY   Engine_HiresRoute, called first in Engine_DriverCmdlRoute: the body
//          instance of the seat (gGT->drivers[seat]->instSelf, its thread's
//          object is the driver) goes through Engine_InstanceSubmit with the
//          hi-res body mesh, frame 0 - the world matrix of the instance (with
//          the squash and stretch of inst->scale), its water line and mirror,
//          the depth cue tint of the CMDL route (EngCmdl_Tints).
//   WHEELS Engine_HiresWheels, called first in EngineFx_Tires, i.e. where the
//          game would write the seat's tire sprites, after every LOD and hiding
//          check of DrawTires.c: four draws of the wheel mesh, no sprite. The
//          frame of each wheel comes only from game state:
//            middle  t + R * ((inst->scale * (+-0x90, 0x40, 0xc7 | -0x60)) >> 12) / 4,
//                    the integer rule of DrawTires_BuildWheelLocalPairs
//                    (engine_fx_tires.c, "wheel middles") - the SPRING: the
//                    middle follows inst->scale.y, which VehPhysForce.c:739/828/835
//                    writes from driver->jumpSquishStretch;
//            steer   front wheels yaw by wheelRotation << 2 game angle units
//                    (4096 a turn), DrawTires.c rim rule, as native_wheels.c;
//            wobble  hazardTimer, the rim rule of DrawTires.c (native_wheels.c);
//            roll    integrated per logic tick from driver->speedApprox: the
//                    way of a tick is speedApprox * elapsedTimeMS / 32 / 256
//                    world units (native_wheels.c head, VehPhysCrash.c:87-92),
//                    the step that way over the radius;
//            size    the mesh scaled by wheelSize / 4096: radius 16 units ->
//                    wheelSize / 256 world units, the retail wheel size.
//          The mirror pass (DrawTires_Reflection) draws the four wheels
//          reflected about the instance's plane y = vertSplit with the skip rule
//          of engine_fx_tires.c.
//   LOG    one line per logic tick of the seat, from the solid pass of view 0
//          ("[CTR Engine] hires frame: ..."): the state read, the roll and step,
//          and what was DRAWN, read back from the finished wheel matrices (the
//          front axle's yaw, the wheel middle's height in kart space), plus the
//          screen box of the body in view 0 (fractions of the view) for the crop
//          of the measurements. Main checks |omega * r - v| / v from roll and
//          posCurr, and drawn = state from the pairs of the line.
//
// MENU SCENE (--engine-menuscene): Engine_MenuSceneCollect draws the hi-res body
// and its four wheels at rest instead of the roster's set (engine_menuscene.c).
//
// DRAW TIME ONLY: nothing of the game is written; host memory of this file only.

char g_cfg_engineHiresDir[ENGINE_HIRES_DIR_MAX] = "";
int g_cfg_engineHiresTex64 = 0;
int g_cfg_engineHiresTexCycle = 0;

#define ENG_HIRES_SEAT 0
#define ENG_HIRES_SMALL 64
#define ENG_HIRES_TWO_PI 6.283185307179586
#define ENG_HIRES_ANGLE (ENG_HIRES_TWO_PI / 4096.0) // one game angle unit in radians
#define ENG_HIRES_WHEEL_RADIUS 16.0                 // model units of the wheel mesh

typedef struct
{
	EngineVertex *vertices;
	u16 *indices;
	u32 vertexCount;
	u32 indexCount;
	float hullMin[3];
	float hullMax[3];
} EngHiresMeshData;

typedef struct
{
	int tried;       // the files were read (once per run)
	int ok;          // files and texture fine
	EngHiresMeshData body;
	EngHiresMeshData wheel;
	u32 texture;     // Engine_TextureCreate, 0xffffffff = none
	int texW, texH;  // of the texture made (64 x 64 with --engine-hires-tex64)
	int fileW, fileH;
	// per load
	int bodyMesh;
	int wheelMesh;
	int material;
	int seatOff;     // the seat has a native set: hi-res off in this level
	EngineRange bodyRange;
	EngineRange wheelRange;
	// the wheel pose of the seat (one entry: the seat's driver and instance)
	const struct Driver *poseDriver;
	const struct Instance *poseInst;
	u32 poseTimer;
	int poseValid;
	u32 poseTicks;
	double roll;
	double step;
	// the log
	u32 logTimer;
	int logValid;
	// --engine-hires-texcycle: the textures full (= texture), 64 x 64 and key,
	// their materials per load, the variant logged last (0 none, else variant + 1)
	u32 cycleTex[3];
	int cycleMaterial[3];
	int cycleLogged;
	// counters (exit report)
	u32 bodyDrawn, bodyDeclined, wheelDraws, wheelSets, mirrorWheelDraws, spriteFallbacks, menuFrames, logLines;
	int reportOn;
} EngHiresState;

internal EngHiresState s_engHires;

// ---------------------------------------------------------------------------
// The dev files (pure parsers: the selftest feeds them made-up bytes).

internal u32 EngHires_U32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

internal void EngHires_FreeMesh(EngHiresMeshData *m)
{
	free(m->vertices);
	free(m->indices);
	memset(m, 0, sizeof(*m));
}

// 1 = out holds copies of the vertices and indices (free with EngHires_FreeMesh).
// 0 = refused, *why says why.
internal int EngHires_ParseMesh(const u8 *bytes, size_t size, EngHiresMeshData *out, const char **why)
{
	u32 vc;
	u32 ic;
	u32 i;
	int c;

	memset(out, 0, sizeof(*out));
	if ((bytes == NULL) || (size < 12u) || (memcmp(bytes, "HRM1", 4) != 0))
	{
		*why = "not an HRM1 file";
		return 0;
	}
	vc = EngHires_U32(bytes + 4);
	ic = EngHires_U32(bytes + 8);
	if ((vc == 0u) || (vc > 65535u) || (ic == 0u) || ((ic % 3u) != 0u) || (ic > 3u * 1000000u))
	{
		*why = "vertex or index count out of range";
		return 0;
	}
	if (size != (12u + ((size_t)vc * sizeof(EngineVertex)) + ((size_t)ic * 2u)))
	{
		*why = "size does not match the counts";
		return 0;
	}
	out->vertices = (EngineVertex *)malloc((size_t)vc * sizeof(EngineVertex));
	out->indices = (u16 *)malloc((size_t)ic * sizeof(u16));
	if ((out->vertices == NULL) || (out->indices == NULL))
	{
		EngHires_FreeMesh(out);
		*why = "no host memory";
		return 0;
	}
	memcpy(out->vertices, bytes + 12, (size_t)vc * sizeof(EngineVertex));
	memcpy(out->indices, bytes + 12 + ((size_t)vc * sizeof(EngineVertex)), (size_t)ic * 2u);
	out->vertexCount = vc;
	out->indexCount = ic;
	for (i = 0; i < ic; i++)
	{
		if ((u32)out->indices[i] >= vc)
		{
			EngHires_FreeMesh(out);
			*why = "index out of range";
			return 0;
		}
	}
	for (c = 0; c < 3; c++)
	{
		out->hullMin[c] = out->vertices[0].position[c];
		out->hullMax[c] = out->vertices[0].position[c];
	}
	for (i = 0; i < vc; i++)
	{
		const EngineVertex *v = &out->vertices[i];

		for (c = 0; c < 3; c++)
		{
			if (!(v->position[c] == v->position[c]) || (fabsf(v->position[c]) > 100000.0f))
			{
				EngHires_FreeMesh(out);
				*why = "position not finite";
				return 0;
			}
			out->hullMin[c] = (v->position[c] < out->hullMin[c]) ? v->position[c] : out->hullMin[c];
			out->hullMax[c] = (v->position[c] > out->hullMax[c]) ? v->position[c] : out->hullMax[c];
		}
		if (!(v->texcoord[0] == v->texcoord[0]) || !(v->texcoord[1] == v->texcoord[1]))
		{
			EngHires_FreeMesh(out);
			*why = "texcoord not finite";
			return 0;
		}
	}
	*why = "";
	return 1;
}

// 1 = *rgba points into bytes (w x h RGBA8). Edges as the texture manager takes them.
internal int EngHires_ParseTexture(const u8 *bytes, size_t size, int *w, int *h, const u8 **rgba, const char **why)
{
	u32 tw;
	u32 th;

	if ((bytes == NULL) || (size < 12u) || (memcmp(bytes, "HRT1", 4) != 0))
	{
		*why = "not an HRT1 file";
		return 0;
	}
	tw = EngHires_U32(bytes + 4);
	th = EngHires_U32(bytes + 8);
	if ((tw > 4096u) || (th > 4096u) || (NativeTex_CheckEdges((int)tw, (int)th, 0) != NATIVE_TEX_OK))
	{
		*why = "edges not a power of two in 16..2048";
		return 0;
	}
	if (size != (12u + ((size_t)tw * th * 4u)))
	{
		*why = "size does not match the edges";
		return 0;
	}
	*w = (int)tw;
	*h = (int)th;
	*rgba = bytes + 12;
	*why = "";
	return 1;
}

// A box reduction to ow x oh (edges dividing w and h): the mean of each block,
// rounded half up, in the stored (sRGB) values - a stand-in for a small
// texture of the same image, nothing finer.
internal int EngHires_Reduce(const u8 *src, int w, int h, int ow, int oh, u8 *dst)
{
	const int bx = (ow > 0) ? (w / ow) : 0;
	const int by = (oh > 0) ? (h / oh) : 0;
	int x;
	int y;

	if ((bx <= 0) || (by <= 0) || ((bx * ow) != w) || ((by * oh) != h))
	{
		return 0;
	}
	for (y = 0; y < oh; y++)
	{
		for (x = 0; x < ow; x++)
		{
			u32 sum[4] = {0, 0, 0, 0};
			const u32 n = (u32)(bx * by);
			int i;
			int j;
			int c;

			for (j = 0; j < by; j++)
			{
				const u8 *row = src + ((((size_t)(y * by + j) * (size_t)w) + (size_t)(x * bx)) * 4u);

				for (i = 0; i < bx; i++)
				{
					for (c = 0; c < 4; c++)
					{
						sum[c] += row[i * 4 + c];
					}
				}
			}
			for (c = 0; c < 4; c++)
			{
				dst[(((size_t)y * (size_t)ow) + (size_t)x) * 4u + (size_t)c] = (u8)((sum[c] + n / 2u) / n);
			}
		}
	}
	return 1;
}

internal u8 *EngHires_ReadFile(const char *dir, const char *name, size_t *size)
{
	char path[ENGINE_HIRES_DIR_MAX + 32];
	FILE *f;
	long length;
	u8 *bytes;

	*size = 0;
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	f = fopen(path, "rb");
	if (f == NULL)
	{
		return NULL;
	}
	if ((fseek(f, 0, SEEK_END) != 0) || ((length = ftell(f)) <= 0) || (length > (64L << 20)) || (fseek(f, 0, SEEK_SET) != 0))
	{
		fclose(f);
		return NULL;
	}
	bytes = (u8 *)malloc((size_t)length);
	if ((bytes != NULL) && (fread(bytes, 1, (size_t)length, f) != (size_t)length))
	{
		free(bytes);
		bytes = NULL;
	}
	fclose(f);
	if (bytes != NULL)
	{
		*size = (size_t)length;
	}
	return bytes;
}

internal void EngHires_Report(void)
{
	if (!g_cfg_engine || (g_cfg_engineHiresDir[0] == '\0'))
	{
		return;
	}
	Platform_Log("[CTR Engine] hires at exit: body drawn %u, declined %u; wheel mesh draws %u in %u set(s), mirrored %u; wheel sprites of the hires seat %u; "
	             "menu frames %u; frame lines %u\n",
	             s_engHires.bodyDrawn, s_engHires.bodyDeclined, s_engHires.wheelDraws, s_engHires.wheelSets, s_engHires.mirrorWheelDraws,
	             s_engHires.spriteFallbacks, s_engHires.menuFrames, s_engHires.logLines);
}

// One texture of the skin's settings (sRGB, the whole mip chain, trilinear,
// anisotropic, clamped); 0xffffffff when the manager refused it.
internal u32 EngHires_SkinTexture(const u8 *rgba, int w, int h, const char *name)
{
	EngineTextureDesc desc;

	memset(&desc, 0, sizeof(desc));
	desc.width = w;
	desc.height = h;
	desc.rgba = rgba;
	desc.linearData = 0; // sRGB colour
	desc.oneLevel = 0;   // the whole chain, also for the small variants
	desc.filter = ENGINE_TEXTURE_FILTERED;
	desc.maxAnisotropy = 0; // the device limit
	desc.wrapU = NATIVE_GFX_WRAP_CLAMP;
	desc.wrapV = NATIVE_GFX_WRAP_CLAMP;
	desc.name = name;
	return Engine_TextureCreate(&desc);
}

// --engine-hires-texcycle: the 64 x 64 reduction of the file and a flat key
// texture beside the run's texture (variant 0). The key colour is pure magenta,
// a colour the skin does not have; the scene modulates it with the grey vertex
// shade only, so a key pixel is (s, 0, s) - tools/hires_mask_sharp.py finds the
// driver's textured pixels by it. A refusal leaves the cycle off.
internal void EngHires_MakeCycleTextures(const u8 *rgba)
{
	static u8 key[16 * 16 * 4];
	u8 *small = (u8 *)malloc((size_t)ENG_HIRES_SMALL * ENG_HIRES_SMALL * 4u);
	int i;

	s_engHires.cycleTex[0] = s_engHires.texture;
	s_engHires.cycleTex[1] = 0xffffffffu;
	s_engHires.cycleTex[2] = 0xffffffffu;
	for (i = 0; i < 16 * 16; i++)
	{
		key[i * 4 + 0] = 255;
		key[i * 4 + 1] = 0;
		key[i * 4 + 2] = 255;
		key[i * 4 + 3] = 255;
	}
	if ((small != NULL) && EngHires_Reduce(rgba, s_engHires.fileW, s_engHires.fileH, ENG_HIRES_SMALL, ENG_HIRES_SMALL, small))
	{
		s_engHires.cycleTex[1] = EngHires_SkinTexture(small, ENG_HIRES_SMALL, ENG_HIRES_SMALL, "hires driver skin 64 (cycle)");
		s_engHires.cycleTex[2] = EngHires_SkinTexture(key, 16, 16, "hires driver key (cycle)");
	}
	free(small);
	Platform_Log("[CTR Engine] hires texcycle: %s - in the menu scene every %d VBlanks: full, 64x64, key (255,0,255); variant = (vblank / %d) %% 3\n",
	             ((s_engHires.cycleTex[1] != 0xffffffffu) && (s_engHires.cycleTex[2] != 0xffffffffu)) ? "textures made" : "a texture was refused, cycle off",
	             ENGINE_HIRES_CYCLE_VBLANKS, ENGINE_HIRES_CYCLE_VBLANKS);
}

// Once per run: the files and the texture. A refusal is logged and turns the
// hi-res driver off for the run.
internal void EngHires_ReadOnce(void)
{
	const char *why = "";
	u8 *bodyData = NULL;
	u8 *wheelData = NULL;
	u8 *texData = NULL;
	u8 *small = NULL;
	size_t bodySize = 0;
	size_t wheelSize = 0;
	size_t texSize = 0;
	const u8 *rgba = NULL;
	EngineTextureDesc desc;

	s_engHires.tried = 1;
	s_engHires.texture = 0xffffffffu;
	bodyData = EngHires_ReadFile(g_cfg_engineHiresDir, "body.hrm", &bodySize);
	wheelData = EngHires_ReadFile(g_cfg_engineHiresDir, "wheel.hrm", &wheelSize);
	texData = EngHires_ReadFile(g_cfg_engineHiresDir, "skin.hrt", &texSize);
	if ((bodyData == NULL) || (wheelData == NULL) || (texData == NULL))
	{
		why = "a file of body.hrm, wheel.hrm, skin.hrt is missing or unreadable";
		goto out;
	}
	if (!EngHires_ParseMesh(bodyData, bodySize, &s_engHires.body, &why) || !EngHires_ParseMesh(wheelData, wheelSize, &s_engHires.wheel, &why) ||
	    !EngHires_ParseTexture(texData, texSize, &s_engHires.fileW, &s_engHires.fileH, &rgba, &why))
	{
		goto out;
	}

	memset(&desc, 0, sizeof(desc));
	desc.width = s_engHires.fileW;
	desc.height = s_engHires.fileH;
	desc.rgba = rgba;
	desc.linearData = 0; // sRGB colour
	desc.oneLevel = 0;   // the whole chain, also for the 64 variant
	desc.filter = ENGINE_TEXTURE_FILTERED;
	desc.maxAnisotropy = 0; // the device limit
	desc.wrapU = NATIVE_GFX_WRAP_CLAMP;
	desc.wrapV = NATIVE_GFX_WRAP_CLAMP;
	desc.name = "hires driver skin";
	if (g_cfg_engineHiresTex64)
	{
		small = (u8 *)malloc((size_t)ENG_HIRES_SMALL * ENG_HIRES_SMALL * 4u);
		if ((small == NULL) || !EngHires_Reduce(rgba, s_engHires.fileW, s_engHires.fileH, ENG_HIRES_SMALL, ENG_HIRES_SMALL, small))
		{
			why = "the 64 x 64 reduction failed";
			goto out;
		}
		desc.width = ENG_HIRES_SMALL;
		desc.height = ENG_HIRES_SMALL;
		desc.rgba = small;
		desc.name = "hires driver skin 64";
	}
	s_engHires.texture = Engine_TextureCreate(&desc);
	if (s_engHires.texture == 0xffffffffu)
	{
		why = "Engine_TextureCreate refused it (see its log line)";
		goto out;
	}
	s_engHires.texW = desc.width;
	s_engHires.texH = desc.height;
	s_engHires.ok = 1;
	if (g_cfg_engineHiresTexCycle)
	{
		EngHires_MakeCycleTextures(rgba);
	}

out:
	if (s_engHires.ok)
	{
		Platform_Log("[CTR Engine] hires: loaded %s: body %u vertices %u triangles (hull %.1f..%.1f, %.1f..%.1f, %.1f..%.1f), wheel %u vertices %u triangles, "
		             "texture %dx%d from %dx%d (%s), %d levels, trilinear\n",
		             g_cfg_engineHiresDir, s_engHires.body.vertexCount, s_engHires.body.indexCount / 3u, (double)s_engHires.body.hullMin[0],
		             (double)s_engHires.body.hullMax[0], (double)s_engHires.body.hullMin[1], (double)s_engHires.body.hullMax[1],
		             (double)s_engHires.body.hullMin[2], (double)s_engHires.body.hullMax[2], s_engHires.wheel.vertexCount, s_engHires.wheel.indexCount / 3u,
		             s_engHires.texW, s_engHires.texH, s_engHires.fileW, s_engHires.fileH, g_cfg_engineHiresTex64 ? "64 variant" : "full", NativeTex_LevelCount(s_engHires.texW, s_engHires.texH));
	}
	else
	{
		EngHires_FreeMesh(&s_engHires.body);
		EngHires_FreeMesh(&s_engHires.wheel);
		Platform_Log("[CTR Engine] hires: %s refused (%s), the hi-res driver is off for this run\n", g_cfg_engineHiresDir, why);
	}
	free(small);
	free(bodyData);
	free(wheelData);
	free(texData); // the manager made its own copy of the levels
}

// ---------------------------------------------------------------------------
// Load (stage 5) and the questions of the other files.

void Engine_HiresLoad(void)
{
	EngineMaterial mat;
	EngineMeshDesc desc;
	const int set = Engine_DriverSetOfSeat(ENG_HIRES_SEAT);
	int variant;

	s_engHires.bodyMesh = -1;
	s_engHires.wheelMesh = -1;
	s_engHires.material = -1;
	s_engHires.cycleMaterial[0] = s_engHires.cycleMaterial[1] = s_engHires.cycleMaterial[2] = -1;
	s_engHires.seatOff = 0;
	s_engHires.poseValid = 0;
	s_engHires.logValid = 0;
	if (!g_cfg_engine || (g_cfg_engineHiresDir[0] == '\0'))
	{
		return;
	}
	if (!s_engHires.reportOn)
	{
		s_engHires.reportOn = 1;
		Platform_AtExitReport(EngHires_Report);
	}
	if (!s_engHires.tried)
	{
		EngHires_ReadOnce();
	}
	if (!s_engHires.ok)
	{
		return;
	}

	memset(&mat, 0, sizeof(mat));
	mat.blend = ENGINE_BLEND_OPAQUE;
	mat.tint[0] = mat.tint[1] = mat.tint[2] = mat.tint[3] = 1.0f;
	Engine_TextureMaterial(&mat, s_engHires.texture);
	// Double sided: the mirror pass flips the winding; the meshes are closed.
	mat.flags |= (u16)ENGINE_MATERIAL_DOUBLE_SIDED;
	s_engHires.material = Engine_MaterialRegister(&mat);
	if (s_engHires.material < 0)
	{
		Platform_Log("[CTR Engine] hires: material table full, not drawn in this level\n");
		return;
	}
	// The texcycle variants: the same material with the other textures.
	s_engHires.cycleMaterial[0] = s_engHires.material;
	for (variant = 1; variant < 3; variant++)
	{
		s_engHires.cycleMaterial[variant] = -1;
		if (g_cfg_engineHiresTexCycle && (s_engHires.cycleTex[variant] != 0xffffffffu) && (s_engHires.cycleTex[variant] != 0u))
		{
			EngineMaterial cm = mat;

			Engine_TextureMaterial(&cm, s_engHires.cycleTex[variant]);
			cm.flags |= (u16)ENGINE_MATERIAL_DOUBLE_SIDED;
			s_engHires.cycleMaterial[variant] = Engine_MaterialRegister(&cm);
		}
	}

	s_engHires.bodyRange.firstIndex = 0;
	s_engHires.bodyRange.indexCount = s_engHires.body.indexCount;
	s_engHires.bodyRange.material = (u16)s_engHires.material;
	memset(&desc, 0, sizeof(desc));
	desc.vertices = s_engHires.body.vertices;
	desc.verticesPerFrame = s_engHires.body.vertexCount;
	desc.frames = 1;
	desc.indices = s_engHires.body.indices;
	desc.indexCount = s_engHires.body.indexCount;
	desc.ranges = &s_engHires.bodyRange;
	desc.rangeCount = 1;
	desc.name = "hires body";
	s_engHires.bodyMesh = Engine_MeshCreate(&desc);

	s_engHires.wheelRange = s_engHires.bodyRange;
	s_engHires.wheelRange.indexCount = s_engHires.wheel.indexCount;
	desc.vertices = s_engHires.wheel.vertices;
	desc.verticesPerFrame = s_engHires.wheel.vertexCount;
	desc.indices = s_engHires.wheel.indices;
	desc.indexCount = s_engHires.wheel.indexCount;
	desc.ranges = &s_engHires.wheelRange;
	desc.name = "hires wheel";
	s_engHires.wheelMesh = Engine_MeshCreate(&desc);

	// A seat with a native set the engine draws keeps its own route.
	if ((set >= 0) && (Engine_DriverBodyMesh(set) >= 0))
	{
		s_engHires.seatOff = 1;
	}
	Platform_Log("[CTR Engine] hires: body mesh %d, wheel mesh %d, material %d; seat %d %s\n", s_engHires.bodyMesh, s_engHires.wheelMesh, s_engHires.material,
	             ENG_HIRES_SEAT, s_engHires.seatOff ? "has a native set: hi-res off for the seat in this level" : "drawn hi-res");
}

int Engine_HiresMenuReady(void)
{
	return g_cfg_engine && s_engHires.ok && (s_engHires.bodyMesh >= 0) && (s_engHires.wheelMesh >= 0);
}

int Engine_HiresOwns(const struct Driver *d)
{
	const struct GameTracker *gGT = sdata->gGT;

	if (!g_cfg_engine || !s_engHires.ok || s_engHires.seatOff || (s_engHires.bodyMesh < 0) || (d == NULL) || (gGT == NULL))
	{
		return 0;
	}
	return ((int)d->driverID == ENG_HIRES_SEAT) && (gGT->drivers[ENG_HIRES_SEAT] == d) && (d->instSelf != NULL);
}

// The meshes and the hull of the body (menu scene).
void Engine_HiresMenuParts(int *bodyMesh, int *wheelMesh, float hullMin[3], float hullMax[3])
{
	int c;

	*bodyMesh = s_engHires.bodyMesh;
	*wheelMesh = s_engHires.wheelMesh;
	for (c = 0; c < 3; c++)
	{
		hullMin[c] = s_engHires.body.hullMin[c];
		hullMax[c] = s_engHires.body.hullMax[c];
	}
}

void Engine_HiresNoteMenuFrame(void)
{
	s_engHires.menuFrames++;
}

// The texcycle variant of this frame: (vblank / ENGINE_HIRES_CYCLE_VBLANKS) % 3
// -> 0 full, 1 64 x 64, 2 key; a change is logged with its VBlank. -1 without
// the switch or when a variant's material is missing.
int Engine_HiresMenuMaterial(void)
{
	static const char *const names[3] = {"full", "64x64", "key"};
	const int vblank = Platform_GetVBlankCount();
	const int variant = ((vblank < 0) ? 0 : (vblank / ENGINE_HIRES_CYCLE_VBLANKS)) % 3;

	if (!g_cfg_engineHiresTexCycle || !s_engHires.ok || (s_engHires.cycleMaterial[1] < 0) || (s_engHires.cycleMaterial[2] < 0))
	{
		return -1;
	}
	if (s_engHires.cycleLogged != (variant + 1))
	{
		s_engHires.cycleLogged = variant + 1;
		Platform_Log("[CTR Engine] hires texcycle: vblank %d: %s\n", vblank, names[variant]);
	}
	return s_engHires.cycleMaterial[variant];
}

// ---------------------------------------------------------------------------
// The wheel frames (pure).

// The wheel middle in kart model units (x, y, z before the instance scale is
// applied in world units: the result IS world units relative to t, rotated by
// R): DrawTires_BuildWheelLocalPairs' integers, a quarter of them.
void Engine_HiresWheelMiddle(int wheel, s16 scaleX, s16 scaleY, s16 scaleZ, float out[3])
{
	const int x = ((int)scaleX * 0x90) >> 12;
	const int y = ((int)scaleY * 0x40) >> 12;
	const int z = (wheel < 2) ? (((int)scaleZ * 0xc7) >> 12) : (((int)scaleZ * -0x60) >> 12);

	out[0] = (float)(((wheel & 1) == 0) ? x : -x) * 0.25f;
	out[1] = (float)y * 0.25f;
	out[2] = (float)z * 0.25f;
}

// The rotation of a wheel in kart space, out[row][col]; columns: the axle
// (toward the kart's +x for every wheel: the one mesh is not mirrored), the rim
// up and the rim front after rolling. Steering (front only), wobble and the
// rim plane as native_wheels.c NativeWheels_Frames (the outward axle there is
// side * the axle here); rolling by +roll moves the top toward the front.
void Engine_HiresWheelFrame(int wheel, double steer, double roll, int hazardTimer, double out[3][3])
{
	static const int phase[4] = {0x000, 0x800, 0x400, 0xc00};
	const double side = ((wheel & 1) != 0) ? -1.0 : 1.0;
	int hazardShift = 9;
	int hazardAngle = hazardTimer * 32;
	double outward[3];
	double axle[3];
	double front[3];
	double up[3];
	double len;
	double a;
	double wobble;
	int r;

	if ((hazardTimer & 1) != 0)
	{
		hazardShift = 6;
		hazardAngle *= 2;
	}
	a = (double)(hazardAngle + phase[wheel & 3]) * ENG_HIRES_ANGLE;
	wobble = 1.0 / (double)(1 << hazardShift);
	if (wheel < 2)
	{
		outward[0] = side * cos(steer);
		outward[1] = 0.0;
		outward[2] = -side * sin(steer);
	}
	else
	{
		outward[0] = side;
		outward[1] = 0.0;
		outward[2] = 0.0;
	}
	outward[1] -= cos(a) * wobble;
	outward[2] -= sin(a) * wobble;
	len = sqrt(outward[0] * outward[0] + outward[1] * outward[1] + outward[2] * outward[2]);
	for (r = 0; r < 3; r++)
	{
		axle[r] = side * outward[r] / len;
	}
	// front = axle x (0, 1, 0), up = front x axle.
	front[0] = -axle[2];
	front[1] = 0.0;
	front[2] = axle[0];
	len = sqrt(front[0] * front[0] + front[2] * front[2]);
	front[0] /= len;
	front[2] /= len;
	up[0] = front[1] * axle[2] - front[2] * axle[1];
	up[1] = front[2] * axle[0] - front[0] * axle[2];
	up[2] = front[0] * axle[1] - front[1] * axle[0];
	for (r = 0; r < 3; r++)
	{
		out[r][0] = axle[r];
		out[r][1] = (up[r] * cos(roll)) + (front[r] * sin(roll));
		out[r][2] = (-up[r] * sin(roll)) + (front[r] * cos(roll));
	}
}

// The roll step of one logic tick, radians: the way over the radius.
double Engine_HiresRollStep(int speedApprox, int elapsedTimeMS, int wheelSize)
{
	const double radius = ENG_HIRES_WHEEL_RADIUS * (double)wheelSize / 4096.0;
	const double way = (double)speedApprox * (double)elapsedTimeMS / 32.0 / 256.0;

	if (wheelSize <= 0)
	{
		return 0.0;
	}
	return way / radius;
}

// worldFromWheel = [R * F * k | middle], R the instance rotation (4.12), F the
// wheel frame, k = wheelSize / 4096 (mesh radius 16 -> wheelSize / 256 world).
internal void EngHires_WheelWorld(const struct Instance *inst, const double F[3][3], const float middle[3], double k, EngineMat4 *out)
{
	double R[3][3];
	int row;
	int col;

	for (row = 0; row < 3; row++)
	{
		for (col = 0; col < 3; col++)
		{
			R[row][col] = (double)inst->matrix.m[row][col] / 4096.0;
		}
	}
	Engine_Mat4Identity(out);
	for (col = 0; col < 3; col++)
	{
		for (row = 0; row < 3; row++)
		{
			const double v = (R[row][0] * F[0][col]) + (R[row][1] * F[1][col]) + (R[row][2] * F[2][col]);

			out->m[col * 4 + row] = (float)(v * k);
		}
	}
	for (row = 0; row < 3; row++)
	{
		const double p = (R[row][0] * middle[0]) + (R[row][1] * middle[1]) + (R[row][2] * middle[2]);

		out->m[12 + row] = (float)((double)inst->matrix.t[row] + p);
	}
}

// The pose of the seat: a new driver or instance starts anew (roll 0); every
// new logic tick (gGT->timer moved) adds one step.
internal void EngHires_PoseTick(const struct GameTracker *gGT, const struct Driver *d, const struct Instance *inst)
{
	if (!s_engHires.poseValid || (s_engHires.poseDriver != d) || (s_engHires.poseInst != inst))
	{
		s_engHires.poseValid = 1;
		s_engHires.poseDriver = d;
		s_engHires.poseInst = inst;
		s_engHires.poseTimer = (u32)gGT->timer;
		s_engHires.poseTicks = 0;
		s_engHires.roll = 0.0;
		s_engHires.step = 0.0;
		return;
	}
	if ((u32)gGT->timer == s_engHires.poseTimer)
	{
		return;
	}
	s_engHires.poseTimer = (u32)gGT->timer;
	s_engHires.poseTicks++;
	s_engHires.step = Engine_HiresRollStep((int)d->speedApprox, gGT->elapsedTimeMS, (int)d->wheelSize);
	s_engHires.roll = fmod(s_engHires.roll + s_engHires.step, ENG_HIRES_TWO_PI);
	if (s_engHires.roll < 0.0)
	{
		s_engHires.roll += ENG_HIRES_TWO_PI;
	}
}

// The screen box of the body in a view, fractions of the view rect (x0 y0 x1 y1);
// 0 when a hull corner is behind the near plane.
internal int EngHires_BodyBox(const EngineCamera *cam, const struct Instance *inst, float box[4])
{
	EngineMat4 world;
	int i;

	if (!Engine_InstanceWorldMatrix(inst, &world) || (cam->rectW <= 0) || (cam->rectH <= 0))
	{
		return 0;
	}
	box[0] = box[1] = 1e9f;
	box[2] = box[3] = -1e9f;
	for (i = 0; i < 8; i++)
	{
		EngineVec3 p;
		EngineVec3 s;

		p.x = (i & 1) ? s_engHires.body.hullMax[0] : s_engHires.body.hullMin[0];
		p.y = (i & 2) ? s_engHires.body.hullMax[1] : s_engHires.body.hullMin[1];
		p.z = (i & 4) ? s_engHires.body.hullMax[2] : s_engHires.body.hullMin[2];
		if (!Engine_ProjectPoint(cam, Engine_Mat4MulPoint(&world, p), &s))
		{
			return 0;
		}
		box[0] = (s.x < box[0]) ? s.x : box[0];
		box[1] = (s.y < box[1]) ? s.y : box[1];
		box[2] = (s.x > box[2]) ? s.x : box[2];
		box[3] = (s.y > box[3]) ? s.y : box[3];
	}
	box[0] /= (float)cam->rectW;
	box[2] /= (float)cam->rectW;
	box[1] /= (float)cam->rectH;
	box[3] /= (float)cam->rectH;
	return 1;
}

// The inverse of a 3x3 matrix (rows as stored), 0 when it is singular.
internal int EngHires_Inverse3(const double m[3][3], double inv[3][3])
{
	const double c00 = m[1][1] * m[2][2] - m[1][2] * m[2][1];
	const double c01 = m[1][2] * m[2][0] - m[1][0] * m[2][2];
	const double c02 = m[1][0] * m[2][1] - m[1][1] * m[2][0];
	const double det = m[0][0] * c00 + m[0][1] * c01 + m[0][2] * c02;

	if (fabs(det) < 1e-12)
	{
		return 0;
	}
	inv[0][0] = c00 / det;
	inv[1][0] = c01 / det;
	inv[2][0] = c02 / det;
	inv[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / det;
	inv[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / det;
	inv[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) / det;
	inv[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / det;
	inv[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) / det;
	inv[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / det;
	return 1;
}

// One line per logic tick (solid pass, view 0). Drawn values are read back from
// the finished matrices into kart space through the INVERSE of the instance
// rotation: the front-left wheel's axle yaw, and the height of its middle
// (world middle - t). inst->matrix is not orthonormal: its 4.12 entries are
// rounded (up to about 0.03 world units at the wheel middle through the
// transpose), and the game writes a stretched matrix for a few ticks (seen on
// Dingo Canyon every 30 ticks: the up column 1.04 long), so a read-back through
// the transpose shows up to 1.47 units and 0.088 rad that were never drawn.
internal void EngHires_LogFrame(const struct GameTracker *gGT, const struct Driver *d, const struct Instance *inst, const EngineCamera *cam,
                                const EngineMat4 *frontLeft)
{
	double R[3][3];
	double Rinv[3][3];
	double axle[3];
	double rel[3];
	double kart[3];
	double drawnSteer;
	float box[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
	int row;

	if (s_engHires.logValid && (s_engHires.logTimer == (u32)gGT->timer))
	{
		return;
	}
	s_engHires.logValid = 1;
	s_engHires.logTimer = (u32)gGT->timer;
	for (row = 0; row < 3; row++)
	{
		int k;

		for (k = 0; k < 3; k++)
		{
			R[row][k] = (double)inst->matrix.m[row][k] / 4096.0;
		}
		rel[row] = (double)frontLeft->m[12 + row] - (double)inst->matrix.t[row];
	}
	if (!EngHires_Inverse3(R, Rinv))
	{
		return;
	}
	// R^-1 * column 0 of the wheel matrix (scale k is a common factor of the direction).
	for (row = 0; row < 3; row++)
	{
		int k;

		axle[row] = 0.0;
		kart[row] = 0.0;
		for (k = 0; k < 3; k++)
		{
			axle[row] += Rinv[row][k] * (double)frontLeft->m[k];
			kart[row] += Rinv[row][k] * rel[k];
		}
	}
	drawnSteer = atan2(-axle[2], axle[0]);
	(void)EngHires_BodyBox(cam, inst, box);
	s_engHires.logLines++;
	Platform_Log("[CTR Engine] hires frame: vb %d timer %u speed %d ms %d pos %d %d %d up %.4f %.4f %.4f roll %.5f step %.5f radius %.4f "
	             "steerRaw %d steerState %.5f steerDrawn %.5f scaleY %d squish %d middleYState %.3f middleYDrawn %.3f hazard %d box %.4f %.4f %.4f %.4f\n",
	             Platform_GetVBlankCount(), (unsigned)gGT->timer, (int)d->speedApprox, gGT->elapsedTimeMS, (int)d->posCurr.x, (int)d->posCurr.y,
	             (int)d->posCurr.z, (double)inst->matrix.m[0][1] / 4096.0, (double)inst->matrix.m[1][1] / 4096.0, (double)inst->matrix.m[2][1] / 4096.0,
	             s_engHires.roll, s_engHires.step, ENG_HIRES_WHEEL_RADIUS * (double)d->wheelSize / 4096.0, (int)d->wheelRotation,
	             (double)((int)d->wheelRotation * 4) * ENG_HIRES_ANGLE, drawnSteer, (int)inst->scale.y, (int)d->jumpSquishStretch,
	             (double)((((int)inst->scale.y * 0x40) >> 12) * 0.25f), kart[1], (int)d->hazardTimer, (double)box[0], (double)box[1], (double)box[2],
	             (double)box[3]);
}

// ---------------------------------------------------------------------------
// Draws.

int Engine_HiresRoute(struct Instance *inst, struct InstDrawPerPlayer *idpp, struct PushBuffer *pb)
{
	struct GameTracker *gGT = sdata->gGT;
	const struct Driver *d;
	EngineInstanceDraw draw;

	if (!g_cfg_engine || !s_engHires.ok || (inst == NULL) || (idpp == NULL) || (pb == NULL) || (gGT == NULL))
	{
		return 0;
	}
	d = gGT->drivers[ENG_HIRES_SEAT];
	if ((d == NULL) || (d->instSelf != inst) || !Engine_HiresOwns(d))
	{
		return 0;
	}
	if ((inst->thread == NULL) || (inst->thread->object != (void *)d) || ((inst->flags & (SCREENSPACE_INSTANCE | HIDE_MODEL)) != 0) ||
	    (Engine_InstanceViewOf(pb) < 0) || !EngCmdl_MeshReady(s_engHires.bodyMesh))
	{
		s_engHires.bodyDeclined++;
		return 0;
	}
	memset(&draw, 0, sizeof(draw));
	draw.mesh = s_engHires.bodyMesh;
	draw.frame = 0u;
	EngCmdl_Tints(inst, idpp, &draw);
	draw.drawBelow = 1;
	draw.flags = 0;
	if (Engine_InstanceSubmit(inst, idpp, pb, &draw) > 0)
	{
		s_engHires.bodyDrawn++;
		Engine_FxNoteDrawn(ENGINE_FX_BODY);
		return 1;
	}
	s_engHires.bodyDeclined++;
	return 0;
}

int Engine_HiresWheels(const struct Driver *d, const struct Instance *inst, const struct InstDrawPerPlayer *idpp, const struct PushBuffer *pb, int reflection)
{
	const struct GameTracker *gGT = sdata->gGT;
	const EngineCamera *cam;
	EngineMat4 screenFromWorld;
	EngineMat4 world[4];
	EngineMat4 mirror;
	float middle[4][3];
	double steer;
	double k;
	float waterLevel;
	int view;
	int w;
	int submitted = 0;

	if (!Engine_HiresOwns(d))
	{
		return 0;
	}
	view = Engine_InstanceViewOf(pb);
	cam = (view >= 0) ? Engine_FrameCamera(view) : NULL;
	if ((inst == NULL) || (idpp == NULL) || (cam == NULL) || !cam->valid || (gGT == NULL) || (d->wheelSize == 0) || (idpp->otRangeNormal == 0) ||
	    !EngCmdl_MeshReady(s_engHires.wheelMesh))
	{
		goto sprites;
	}

	EngHires_PoseTick(gGT, d, inst);
	steer = (double)((int)d->wheelRotation * 4) * ENG_HIRES_ANGLE;
	k = (double)d->wheelSize / 4096.0;
	waterLevel = (float)inst->vertSplit;
	for (w = 0; w < 4; w++)
	{
		double F[3][3];

		Engine_HiresWheelMiddle(w, inst->scale.x, inst->scale.y, inst->scale.z, middle[w]);
		Engine_HiresWheelFrame(w, steer, s_engHires.roll, (int)d->hazardTimer, F);
		EngHires_WheelWorld(inst, F, middle[w], k, &world[w]);
	}
	if ((view == 0) && !reflection)
	{
		EngHires_LogFrame(gGT, d, inst, cam, &world[0]);
	}

	Engine_CameraScreenFromWorld(cam, &screenFromWorld);
	Engine_Mat4Identity(&mirror);
	if (reflection)
	{
		// y' = 2 L - y (engine_instance.c EngInst_Reflect).
		EngInst_Reflect(waterLevel, &mirror);
	}
	for (w = 0; w < 4; w++)
	{
		const float centreY = world[w].m[13];
		EngineMat4 placed;
		EngineDraw draw;
		EngineVec3 centre;
		u32 *ot;
		int cell;

		// The skip rule and the range of engine_fx_tires.c (water line of the instance).
		if (reflection && (centreY < waterLevel))
		{
			continue;
		}
		if (reflection)
		{
			ot = (u32 *)(size_t)(u32)((idpp->otRangeSecondary != 0) ? idpp->otRangeSecondary : idpp->otRangeNormal);
		}
		else
		{
			ot = (u32 *)(size_t)(u32)(((waterLevel >= centreY) && (idpp->otRangeSecondary != 0)) ? idpp->otRangeSecondary : idpp->otRangeNormal);
		}
		cell = NativeRenderLayer_MarkerBin(idpp, 0);
		Engine_Mat4Mul(&mirror, &world[w], &placed);
		centre.x = placed.m[12];
		centre.y = placed.m[13];
		centre.z = placed.m[14];

		memset(&draw, 0, sizeof(draw));
		draw.mesh = s_engHires.wheelMesh;
		draw.frame = 0u;
		draw.materialOverride = -1;
		draw.dynMaterial = -1;
		Engine_Mat4Mul(&screenFromWorld, &placed, &draw.clipFromModel);
		draw.tint[0] = draw.tint[1] = draw.tint[2] = draw.tint[3] = 1.0f;
		draw.sortDepth = Engine_Mat4MulPoint(&cam->viewFromWorld, centre).z;
		draw.otCell = (u32)cell;
		if (Engine_RenderSubmitLinked(view, &draw, ot, cell))
		{
			submitted++;
			if (reflection)
			{
				s_engHires.mirrorWheelDraws++;
			}
			else
			{
				s_engHires.wheelDraws++;
			}
		}
	}
	if (!reflection && (submitted > 0))
	{
		s_engHires.wheelSets++;
	}
	if ((submitted == 0) && !reflection)
	{
		goto sprites;
	}
	return 1;

sprites:
	// Nothing drawn: the game's sprites stand in (counted, must stay 0).
	s_engHires.spriteFallbacks += 4;
	for (w = 0; w < 4; w++)
	{
		Engine_FxNoteRetail(ENGINE_FX_TIRES);
	}
	return -1;
}

// ---------------------------------------------------------------------------
// Selftest: parsers, reduction, wheel frames, roll step, middles.

int Engine_SelfTestHires(int *checks)
{
	int failures = 0;
	int n = 0;

#define HIRES_CHECK(cond, what)                                   \
	do                                                            \
	{                                                             \
		n++;                                                      \
		if (!(cond))                                              \
		{                                                         \
			failures++;                                           \
			printf("engine selftest: hires " what " FAILED\n"); \
		}                                                         \
	} while (0)

	// A one-triangle mesh, good and broken.
	{
		u8 buf[12 + 3 * 24 + 6];
		EngHiresMeshData m;
		const char *why = "";
		const u32 vc = 3;
		const u32 ic = 3;
		EngineVertex v[3];
		const u16 idx[3] = {0, 1, 2};
		int ok;

		memset(v, 0, sizeof(v));
		v[1].position[0] = 2.0f;
		v[2].position[1] = -3.0f;
		memcpy(buf, "HRM1", 4);
		memcpy(buf + 4, &vc, 4);
		memcpy(buf + 8, &ic, 4);
		memcpy(buf + 12, v, sizeof(v));
		memcpy(buf + 12 + sizeof(v), idx, sizeof(idx));
		ok = EngHires_ParseMesh(buf, sizeof(buf), &m, &why);
		HIRES_CHECK(ok && (m.vertexCount == 3u) && (m.indexCount == 3u), "mesh parses");
		HIRES_CHECK(ok && (m.hullMax[0] == 2.0f) && (m.hullMin[1] == -3.0f), "mesh hull");
		EngHires_FreeMesh(&m);
		HIRES_CHECK(!EngHires_ParseMesh(buf, sizeof(buf) - 1u, &m, &why), "short mesh refused");
		buf[12 + sizeof(v) + 4] = 7; // index 2 -> 7
		HIRES_CHECK(!EngHires_ParseMesh(buf, sizeof(buf), &m, &why), "index out of range refused");
		buf[0] = 'X';
		HIRES_CHECK(!EngHires_ParseMesh(buf, sizeof(buf), &m, &why), "bad magic refused");
	}
	// Textures: good 16 x 16, bad edge, short.
	{
		static u8 tex[12 + 16 * 16 * 4];
		const u32 e16 = 16;
		const u32 e24 = 24;
		const u8 *rgba = NULL;
		const char *why = "";
		int w = 0;
		int h = 0;

		memset(tex, 0, sizeof(tex));
		memcpy(tex, "HRT1", 4);
		memcpy(tex + 4, &e16, 4);
		memcpy(tex + 8, &e16, 4);
		HIRES_CHECK(EngHires_ParseTexture(tex, sizeof(tex), &w, &h, &rgba, &why) && (w == 16) && (h == 16) && (rgba == tex + 12), "texture parses");
		HIRES_CHECK(!EngHires_ParseTexture(tex, sizeof(tex) - 4u, &w, &h, &rgba, &why), "short texture refused");
		memcpy(tex + 4, &e24, 4);
		HIRES_CHECK(!EngHires_ParseTexture(tex, sizeof(tex), &w, &h, &rgba, &why), "edge 24 refused");
	}
	// The reduction: a 2 x 2 checker of 0/255 per 128 x 128 block gives 128 (half up).
	{
		static u8 src[128 * 128 * 4];
		u8 dst[64 * 64 * 4];
		int x;
		int y;
		int same = 1;

		for (y = 0; y < 128; y++)
		{
			for (x = 0; x < 128; x++)
			{
				const u8 v = (u8)((((x ^ y) & 1) != 0) ? 255 : 0);

				src[(y * 128 + x) * 4 + 0] = v;
				src[(y * 128 + x) * 4 + 1] = v;
				src[(y * 128 + x) * 4 + 2] = v;
				src[(y * 128 + x) * 4 + 3] = 255;
			}
		}
		HIRES_CHECK(EngHires_Reduce(src, 128, 128, 64, 64, dst), "reduce runs");
		for (x = 0; x < 64 * 64; x++)
		{
			same = same && (dst[x * 4] == 128) && (dst[x * 4 + 3] == 255);
		}
		HIRES_CHECK(same, "reduce averages");
		HIRES_CHECK(!EngHires_Reduce(src, 100, 128, 64, 64, dst), "reduce refuses an odd ratio");
	}
	// Wheel frames: rest = identity for every wheel; roll pi/2 turns up to the
	// front; a steer s gives the front axle yaw s (as the log reads it back), the
	// rear none; every frame a rotation (det +1).
	{
		double F[3][3];
		int wheel;
		int rest = 1;
		int dets = 1;

		for (wheel = 0; wheel < 4; wheel++)
		{
			int r;
			int c;

			Engine_HiresWheelFrame(wheel, 0.0, 0.0, 0, F);
			for (r = 0; r < 3; r++)
			{
				for (c = 0; c < 3; c++)
				{
					// The retail wobble tilts the axle by up to 1/512 even at hazardTimer 0.
					rest = rest && (fabs(F[r][c] - ((r == c) ? 1.0 : 0.0)) < 0.004);
				}
			}
			Engine_HiresWheelFrame(wheel, 0.3, 1.1, 5, F);
			{
				const double det = F[0][0] * (F[1][1] * F[2][2] - F[1][2] * F[2][1]) - F[0][1] * (F[1][0] * F[2][2] - F[1][2] * F[2][0]) +
				                   F[0][2] * (F[1][0] * F[2][1] - F[1][1] * F[2][0]);

				dets = dets && (fabs(det - 1.0) < 1e-9);
			}
		}
		HIRES_CHECK(rest, "wheel frame at rest is the identity (but the wobble)");
		HIRES_CHECK(dets, "wheel frames are rotations");
		Engine_HiresWheelFrame(0, 0.0, ENG_HIRES_TWO_PI / 4.0, 0, F);
		HIRES_CHECK((fabs(F[2][1] - 1.0) < 1e-9) && (fabs(F[1][1]) < 1e-9), "roll moves the top to the front");
		Engine_HiresWheelFrame(1, 0.25, 0.0, 0, F);
		HIRES_CHECK(fabs(atan2(-F[2][0], F[0][0]) - 0.25) < 0.004, "front axle yaw = steer (but the wobble)");
		Engine_HiresWheelFrame(3, 0.25, 0.0, 0, F);
		HIRES_CHECK(fabs(atan2(-F[2][0], F[0][0])) < 0.004, "rear axle does not steer (but the wobble)");
	}
	// Roll step: way / radius, so omega * r = v exactly.
	{
		const double step = Engine_HiresRollStep(1000, 32, 0xccc);
		const double way = 1000.0 * 32.0 / 32.0 / 256.0;
		const double radius = 16.0 * (double)0xccc / 4096.0;

		HIRES_CHECK(fabs(step * radius - way) < 1e-9, "roll step times radius is the way");
		HIRES_CHECK(Engine_HiresRollStep(-500, 33, 0xccc) < 0.0, "backward speed rolls backward");
		HIRES_CHECK(Engine_HiresRollStep(1000, 32, 0) == 0.0, "no wheel no roll");
	}
	// The read-back through the inverse: a yaw with a stretched up column (as the
	// game writes for a few ticks) gives the kart-space point back; the transpose
	// does not.
	{
		const double c = cos(0.4);
		const double sn = sin(0.4);
		const double R[3][3] = {{c, 0.0, sn}, {0.0, 1.04, 0.0}, {-sn, 0.0, c}};
		const double m[3] = {28.75, 12.75, 39.75};
		double inv[3][3];
		double w[3];
		double back[3];
		double viaT = 0.0;
		int r;

		for (r = 0; r < 3; r++)
		{
			w[r] = R[r][0] * m[0] + R[r][1] * m[1] + R[r][2] * m[2];
		}
		HIRES_CHECK(EngHires_Inverse3(R, inv), "inverse exists");
		for (r = 0; r < 3; r++)
		{
			back[r] = inv[r][0] * w[0] + inv[r][1] * w[1] + inv[r][2] * w[2];
			viaT += (r == 1) ? (R[0][1] * w[0] + R[1][1] * w[1] + R[2][1] * w[2]) : 0.0;
		}
		HIRES_CHECK((fabs(back[0] - m[0]) < 1e-9) && (fabs(back[1] - m[1]) < 1e-9) && (fabs(back[2] - m[2]) < 1e-9), "inverse reads the middle back");
		HIRES_CHECK(fabs(viaT - m[1]) > 0.5, "the transpose would not (stretched matrix)");
	}
	// Middles: the integer rule of DrawTires.c at the rest scale 0xccc.
	{
		float m[3];

		Engine_HiresWheelMiddle(0, 0xccc, 0xccc, 0xccc, m);
		HIRES_CHECK((m[0] == 28.75f) && (m[1] == 12.75f) && (m[2] == 39.75f), "front +x middle");
		Engine_HiresWheelMiddle(3, 0xccc, 0xccc, 0xccc, m);
		HIRES_CHECK((m[0] == -28.75f) && (m[2] == -19.25f), "rear -x middle");
		Engine_HiresWheelMiddle(0, 0xccc, 0x1000, 0xccc, m);
		HIRES_CHECK(m[1] == 16.0f, "middle follows scale.y");
	}
	// The value of --engine-menuscene-zoom (engine_menuscene.c): zoom alone,
	// with the place, with a still yaw; out of range and malformed refused and
	// the outputs untouched.
	{
		float zoom = -1.0f;
		float x = -9.0f;
		float yaw = -9.0f;
		int fixed = -1;

		HIRES_CHECK(Engine_MenuSceneZoomParse("2", &zoom, &x, &fixed, &yaw) && (zoom == 2.0f) && (fabsf(x - 0.30f) < 1e-6f) && (fixed == 0),
		            "zoom alone keeps the place and the turn");
		HIRES_CHECK(Engine_MenuSceneZoomParse("2.5,-0.78,90", &zoom, &x, &fixed, &yaw) && (zoom == 2.5f) && (fabsf(x + 0.78f) < 1e-6f) && (fixed == 1) &&
		                (fabsf(yaw - 1.5707963f) < 1e-5f),
		            "zoom, place and still yaw");
		zoom = 7.0f;
		HIRES_CHECK(!Engine_MenuSceneZoomParse("9", &zoom, &x, &fixed, &yaw) && (zoom == 7.0f), "zoom out of range refused, outputs untouched");
		HIRES_CHECK(!Engine_MenuSceneZoomParse("2,1.5", &zoom, &x, &fixed, &yaw), "place out of range refused");
		HIRES_CHECK(!Engine_MenuSceneZoomParse("2,0,10,4", &zoom, &x, &fixed, &yaw), "a fourth value refused");
		HIRES_CHECK(!Engine_MenuSceneZoomParse("2x", &zoom, &x, &fixed, &yaw) && !Engine_MenuSceneZoomParse("", &zoom, &x, &fixed, &yaw) &&
		                !Engine_MenuSceneZoomParse("2,,1", &zoom, &x, &fixed, &yaw),
		            "malformed values refused");
	}
#undef HIRES_CHECK
	*checks += n;
	return failures;
}
