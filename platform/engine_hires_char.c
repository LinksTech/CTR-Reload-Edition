#include <platform/engine.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ENGINE HIRES CHAR - the hi-res drivers of the .rldchar (round of 2026-10-17).
// See include/platform/engine.h, HI-RES DRIVERS FROM THE .rldchar.
//
// THE PATH OF A FILE: load stage 5 (Engine_HiresCharsLoad, from
// Engine_DriverCmdlLoad): every bound seat whose file has a CHRG chunk
// (NativeChar_EntryHasChrg; in a main menu level the roster's files) -> the
// chunk read again from the file (NativeChar_ReadChrg: CMDL hash, envelope
// checks, SHA-256) -> RldHires_ParseChunk -> RldHires_Open -> RldHires_CheckBudget
// (include/rldhires.inc; the first error refuses: one line with its rule, the
// classic model) -> EngHc_BuildCpu (skeleton, clips, vertices, palette, ranges,
// hull) -> EngHc_Upload (textures, index buffer) -> the GLB and cgltf freed.
// Frame: Engine_HiresCharsCollect steps the seats' clip state per logic tick
// (engine_hires_pose.c) and their wheels' roll; the route (from
// Engine_HiresRoute) evaluates the pose, skins it into the seat's slot once per
// frame and submits the draw(s); Engine_HiresCharSubmit does the same for a
// forced clip in a slot of its caller. The pure parts are in
// include/rldhires_anim.inc (pose maths) and platform/engine_hires_pose.c.
//
// THE DESIGN:
//   SKINNING ON THE CPU, per frame, once per pose slot, into a per-bank CPU
//   array that the renderer copies into a STREAMED vertex buffer (three
//   regions, the dynamic buffer's region rule: frames-in-flight safe;
//   Engine_RenderStream* in engine_render.c). Every vertex leaves the skinning
//   in WORLD units and axes (the instance matrix with inst->scale - the game's
//   matrix animations and squash act on the whole model - times 64 model units
//   per metre times the node/joint matrices of the pose): program "ns" lights
//   in world space (v_world = a_position, the normal packed into a_uvBox), and
//   the draw's matrix is the camera's alone. No vertex format, program or
//   uniform block changes; no other draw changes; the dynamic geometry budget
//   ENGINE_DYN_VERTICES_PER_FRAME of the track and the effects is not touched.
//   WHEELS are part of the same vertex buffer: four rigid palette entries whose
//   matrices come from the wheel rules of the prototype (engine_hires.c:
//   middles from inst->scale, steer from wheelRotation, roll integrated per
//   logic tick from speedApprox, wobble from hazardTimer) - the file's wheel
//   nodes when all four are there, else standard 3D wheels made in code.

int g_cfg_engineHiresChars = 0;
int g_cfg_engineHiresCharsLog = 0;

// ---------------------------------------------------------------------------
// THE DATA
//
// A CHARACTER is one file's hi-res model, loaded once and kept across level
// loads (at most ENGINE_HIRES_CHARS_MAX; a level that needs room drops the one
// no level has needed for longest): its skeleton and clips (rldhires_anim.inc),
// the skinning source of every streamed vertex (position, normal, four palette
// indices and weights), the texcoord and colour template of the vertices, the
// u32 index buffer on the GPU with one range per material, the textures. The GLB
// and its cgltf data are freed once the GPU side is made (32-bit process).
//
// THE PALETTE of a character: [0, joints) the skin's joints (world x inverse
// bind), [joints, joints + rigid) the body nodes with a mesh but no skin (their
// node matrix), then the four wheels. A vertex names up to four entries.
//
// A POSE SLOT holds one pose per frame: its streamed vertex buffer (sized for
// the largest character it may draw), its CPU array per bank, the external mesh
// per character in this level. Slots 0..7 are the seats, 8..11 the forced
// submits.

#define ENG_HC_UNITS RLDHIRES_UNITS_PER_METRE // kart model units per glTF metre
#define ENG_HC_FILE_MAX 256
// The engine's hard limits are include/rldhires.inc's (HRG-32..35, checked by
// RldHires_Open as the packer and the Studio check them); the checks here are
// the second line with the same rule ids.
#define ENG_HC_MATERIALS_MAX (RLDHIRES_MATERIALS_MAX + 2u) // the file's materials + the default + the standard wheels'
#define ENG_HC_TEXTURES_MAX RLDHIRES_TEXTURE_SLOTS_MAX
#define ENG_HC_PALETTE_MAX RLDHIRES_PALETTE_MAX
#define ENG_HC_VERTICES_MAX RLDHIRES_VERTICES_MAX // an external mesh's frame (engine_mesh.c MESH_VERTICES_TOTAL_MAX)
#define ENG_HC_HULL_SAMPLES RLDHIRES_HULL_SAMPLES // samples per clip for the hull (both ends included)
#define ENG_HC_HULL_MAX (RLDHIRES_BOUNDS_MAX_M * RLDHIRES_UNITS_PER_METRE)
// The culling margin of the hull, per side, a fraction of the axis's extent
// (blends and the times between the samples; not part of the 4 m rule, which
// tests the plain extent): include/rldhires.inc's RLDHIRES_HULL_MARGIN.
#define ENG_HC_HULL_MARGIN RLDHIRES_HULL_MARGIN
#define ENG_HC_STD_SEGMENTS 20        // the standard wheel: a cylinder of this many sides
#define ENG_HC_STD_RADIUS 16.0f       // model units, the prototype's wheel (engine_hires.c ENG_HIRES_WHEEL_RADIUS)
#define ENG_HC_STD_HALF_WIDTH 5.0f
#define ENG_HC_STD_VERTICES (ENG_HC_STD_SEGMENTS * 4 + 2) // tread 2 rings, two side rings, two hubs
#define ENG_HC_STD_INDICES (ENG_HC_STD_SEGMENTS * 12)
// The slots: the API's (seats, forced submits) and one of the engine's own,
// the driver select preview (EngHc_SelectRoute).
#define ENG_HC_SLOT_SELECT ENGINE_HIRES_SLOTS
#define ENG_HC_SLOTS_ALL (ENGINE_HIRES_SLOTS + 1)
#define ENG_HC_GRAVE_MAX 64
#define ENG_HC_TWO_PI 6.283185307179586
#define ENG_HC_ANGLE (ENG_HC_TWO_PI / 4096.0) // one game angle unit

typedef struct
{
	float p[3];
	float n[3];
	float w[4];
	u8 j[4];
	u8 hasNormal;
	u8 pad[3];
} EngHcSrc;

typedef struct
{
	int baseTex;   // index into the character's textures, -1 none
	int normalTex; // -1 none
	float factor[4];
	int mask;      // MASK (and BLEND as MASK 0.5)
	float cutoff;
	int doubleSided;
	int unlit;
	float normalScale;
} EngHcMat;

typedef struct
{
	int image;
	int linear;
	int wrapU;
	int wrapV;
	u32 texture; // Engine_TextureCreate, 0xffffffff none
	int width;
	int height;
} EngHcTex;

typedef struct
{
	int used;
	char file[ENG_HC_FILE_MAX];
	int entry;
	const struct Model *model;
	u32 lastLevel;
	RldAnimSkeleton skel;
	RldAnimClip clip[ENGINE_HIRES_CLIPS];
	u32 clipMask;
	u32 vertexCount;
	u32 indexCount;
	u32 triangles;
	EngHcSrc *src;
	EngineVertex *base;
	u32 *indices; // until the upload
	u32 joints;
	u32 rigidCount;
	u32 *rigidNode;
	u32 wheelFirst;
	u32 paletteCount;
	int ownWheels;
	int wheelNode[4];
	float wheelRadius[4];  // model units at the node's rest scale (before the wheel size)
	float wheelSign[4];    // +1 / -1: the roll about the node's X that moves the top forward
	float wheelRestYaw[4]; // the node's axle yaw in kart space at rest
	u32 indexBuffer;
	EngineRange ranges[ENG_HC_MATERIALS_MAX];
	u32 rangeCount;
	EngHcMat mats[ENG_HC_MATERIALS_MAX];
	int matCount;
	EngHcTex tex[ENG_HC_TEXTURES_MAX];
	int texCount;
	int texOverflow; // a material needed a texture upload past ENG_HC_TEXTURES_MAX (HRG-35)
	u64 texBytes;
	float hullMin[3];
	float hullMax[3];
	u32 hostBytes;
	int levelMaterial[ENG_HC_MATERIALS_MAX];
	u32 materialLevel;
	char clipText[320];
	char texText[160];
} EngHcChar;

typedef struct
{
	u32 stream;
	u32 capacity;
	EngineVertex *cpu[ENGINE_SCENE_BANKS];
	int cpuChar[ENGINE_SCENE_BANKS];
	u32 skinnedFrame; // s_hc.frame + 1 of the last skin
	int skinnedChar;
	int mesh[ENGINE_HIRES_CHARS_MAX];
	u32 meshLevel[ENGINE_HIRES_CHARS_MAX];
	float yawDrawn;  // wheel 0 read back from the last skin (kart space, rest yaw taken off)
	float rollDrawn;
} EngHcSlot;

typedef struct
{
	const struct Driver *driver;
	const struct Instance *inst;
	u32 timer;
	int valid;
	EngHcSeatState st;
	RldAnimTrs *snapshot;
	u32 snapshotNodes;
	double roll[4];
	double step[4];
	double radius[4]; // world units of the last tick
	u32 logTimer;
	int logValid;
	int force;
} EngHcSeatRun;

// What a pose was made of (the log line).
typedef struct
{
	int want;
	int clip;
	float t;
	float steer;
	int steerClip;
	float blend;
	int prevClip;
} EngHcPoseInfo;

// Where a pose is drawn: the model-to-world matrix and the wheels' outer rotation.
typedef struct
{
	EngineMat4 world;           // model units -> world units
	double rot[3][3];           // the STANDARD wheels' rotation and size outer (instance: R x wheelSize / 4096; a matrix: its 3x3);
	                            // own wheel nodes are part of the model and take the 3x3 of world (inst->scale included)
	const struct Instance *inst; // set: the standard wheels by the prototype's integer middles
	double k;                   // wheelSize / 4096 (instance)
	double steer;               // radians, front wheels
	double roll[4];
	int hazard;
} EngHcPlace;

internal struct
{
	EngHcChar chars[ENGINE_HIRES_CHARS_MAX];
	EngHcSlot slots[ENG_HC_SLOTS_ALL];
	const struct Instance *selectInst; // the driver select instance drawn hi-res in the frame selectFrame
	u32 selectFrame;
	int selectLogged; // the character last logged there + 1
	EngHcSeatRun seats[ENGINE_HIRES_SLOT_SEATS];
	int seatChar[ENGINE_HIRES_SLOT_SEATS];
	int lastRaceEntry[ENGINE_HIRES_SLOT_SEATS];
	int haveLastRace;
	u32 levelStamp;
	u32 frame;
	int levelReady;
	// scratch of the pose and the skinning (the largest loaded character)
	RldAnimTrs *poseA;
	RldAnimTrs *poseB;
	float *worlds;
	float *palette;
	u32 scratchNodes;
	u32 scratchPalette;
	float sun[3];
	float style[4];
	int reportOn;
	// counters (exit report)
	u32 loaded, refused, skins, draws, declined, mirrorDraws, submits, ticks, lines, notDrawnTicks;
	u64 skinnedVertices;
	double skinMs;
	u32 skinFrames;
	u32 hostPeak;
	char ruleCopy[16];       // the rule id of the last refusal from a report (EngHc_FirstError)
	// THE GRAVE: GPU buffers let go at a load, destroyed at the next load (the
	// renderer's rule, Engine_RenderLevelUnload: a command buffer of the load may
	// still be recording with them). Textures need none: the backend retires them.
	u32 graveVB[ENG_HC_GRAVE_MAX];
	u32 graveIB[ENG_HC_GRAVE_MAX];
	int graveCount;
	u32 refusedSlotSkins;    // a slot asked for a second character in one frame (refused)
	int refusedSlotLogged;
} s_hc;

// ---------------------------------------------------------------------------
// Small maths.

internal void EngHc_MatScale(EngineMat4 *m, float s)
{
	int c;
	int r;

	for (c = 0; c < 3; c++)
	{
		for (r = 0; r < 3; r++)
		{
			m->m[c * 4 + r] *= s;
		}
	}
}

// The world matrix of a wheel: [rot | t], rot row-major 3 x 3, column-major out.
internal void EngHc_WheelMatrix(const double rot[3][3], const double t[3], float out[16])
{
	int r;
	int c;

	for (c = 0; c < 3; c++)
	{
		for (r = 0; r < 3; r++)
		{
			out[c * 4 + r] = (float)rot[r][c];
		}
		out[c * 4 + 3] = 0.0f;
	}
	out[12] = (float)t[0];
	out[13] = (float)t[1];
	out[14] = (float)t[2];
	out[15] = 1.0f;
}

internal void EngHc_Mul3(const double a[3][3], const double b[3][3], double out[3][3])
{
	int r;
	int c;

	for (r = 0; r < 3; r++)
	{
		for (c = 0; c < 3; c++)
		{
			out[r][c] = a[r][0] * b[0][c] + a[r][1] * b[1][c] + a[r][2] * b[2][c];
		}
	}
}

internal u8 EngHc_PackUnit(float v)
{
	const float x = (v * 0.5f + 0.5f) * 255.0f + 0.5f;

	return (u8)((x <= 0.0f) ? 0 : ((x >= 255.0f) ? 255 : (int)x));
}

internal int EngHc_WrapOf(int mode)
{
	if (mode == 33071)
	{
		return NATIVE_GFX_WRAP_CLAMP;
	}
	if (mode == 33648)
	{
		return NATIVE_GFX_WRAP_MIRROR;
	}
	return NATIVE_GFX_WRAP_REPEAT;
}

// ---------------------------------------------------------------------------
// THE CPU SIDE OF A CHARACTER (EngHc_BuildCpu, pure: no device, no game state;
// the selftest runs it on code-made GLBs).

internal void EngHc_FreeCpu(EngHcChar *c)
{
	int k;

	RldAnim_FreeSkeleton(&c->skel);
	for (k = 0; k < ENGINE_HIRES_CLIPS; k++)
	{
		RldAnim_FreeClip(&c->clip[k]);
	}
	free(c->src);
	free(c->base);
	free(c->indices);
	free(c->rigidNode);
	c->src = NULL;
	c->base = NULL;
	c->indices = NULL;
	c->rigidNode = NULL;
}

internal void EngHc_Fail(const char **rule, char *detail, int detailSize, const char *r, const char *text)
{
	*rule = r;
	snprintf(detail, (size_t)detailSize, "%s", text);
}

// The standard wheel (model units, axle = x, middle at the origin): a tread of
// two rings and two flat sides with a hub vertex; tyre dark, sides grey.
internal void EngHc_StdWheel(EngHcSrc *src, EngineVertex *base, u32 *indices, u32 first, u32 palette)
{
	u32 v = 0;
	u32 i = 0;
	int s;
	int side;

	for (s = 0; s < ENG_HC_STD_SEGMENTS; s++)
	{
		const double a = ENG_HC_TWO_PI * (double)s / (double)ENG_HC_STD_SEGMENTS;
		const float cy = (float)cos(a);
		const float sz = (float)sin(a);

		for (side = 0; side < 2; side++)
		{
			const float x = (side == 0) ? ENG_HC_STD_HALF_WIDTH : -ENG_HC_STD_HALF_WIDTH;
			EngHcSrc *t = &src[v];
			EngHcSrc *f = &src[v + 1];

			// tread vertex (radial normal) and side vertex (axial normal)
			t->p[0] = x;
			t->p[1] = cy * ENG_HC_STD_RADIUS;
			t->p[2] = sz * ENG_HC_STD_RADIUS;
			t->n[0] = 0.0f;
			t->n[1] = cy;
			t->n[2] = sz;
			*f = *t;
			f->n[0] = (side == 0) ? 1.0f : -1.0f;
			f->n[1] = 0.0f;
			f->n[2] = 0.0f;
			v += 2;
		}
	}
	for (side = 0; side < 2; side++)
	{
		EngHcSrc *h = &src[v++];

		memset(h, 0, sizeof(*h));
		h->p[0] = (side == 0) ? ENG_HC_STD_HALF_WIDTH : -ENG_HC_STD_HALF_WIDTH;
		h->n[0] = (side == 0) ? 1.0f : -1.0f;
	}
	for (v = 0; v < (u32)ENG_HC_STD_VERTICES; v++)
	{
		EngHcSrc *e = &src[v];
		EngineVertex *b = &base[v];
		const int tread = (v < (u32)(ENG_HC_STD_SEGMENTS * 4)) && ((v & 1u) == 0u);
		const u8 grey = tread ? 38 : ((v >= (u32)(ENG_HC_STD_SEGMENTS * 4)) ? 170 : 112);

		e->w[0] = 1.0f;
		e->w[1] = e->w[2] = e->w[3] = 0.0f;
		e->j[0] = (u8)palette;
		e->j[1] = e->j[2] = e->j[3] = 0;
		e->hasNormal = 1;
		memset(b, 0, sizeof(*b));
		b->color[0] = b->color[1] = b->color[2] = grey;
		b->color[3] = 255;
	}
	// vertex (s, side, kind): 4 * s + 2 * side + kind (kind 0 tread, 1 side)
	for (s = 0; s < ENG_HC_STD_SEGMENTS; s++)
	{
		const u32 a0 = (u32)(4 * s);
		const u32 a1 = (u32)(4 * ((s + 1) % ENG_HC_STD_SEGMENTS));
		const u32 hub0 = (u32)(ENG_HC_STD_SEGMENTS * 4);
		const u32 hub1 = hub0 + 1u;

		// tread quad: (s, +x), (s, -x), (s+1, -x), (s+1, +x)
		indices[i++] = first + a0 + 0u;
		indices[i++] = first + a1 + 0u;
		indices[i++] = first + a0 + 2u;
		indices[i++] = first + a0 + 2u;
		indices[i++] = first + a1 + 0u;
		indices[i++] = first + a1 + 2u;
		// the +x side (seen from +x) and the -x side
		indices[i++] = first + hub0;
		indices[i++] = first + a1 + 1u;
		indices[i++] = first + a0 + 1u;
		indices[i++] = first + hub1;
		indices[i++] = first + a0 + 3u;
		indices[i++] = first + a1 + 3u;
	}
}

// One primitive's vertices into src/base from vertex `first`: positions,
// normals (none = flat: hasNormal 0), texcoords, colours (linear factors, as
// glTF COLOR_0), palette entries (skinned: JOINTS_0 / WEIGHTS_0 renormalised;
// rigid: the one entry).
internal void EngHc_ReadPrimitive(const cgltf_primitive *prim, int skinned, u32 entry, EngHcSrc *src, EngineVertex *base, u32 first, u32 count)
{
	const cgltf_accessor *pos = cgltf_find_accessor(prim, cgltf_attribute_type_position, 0);
	const cgltf_accessor *nrm = cgltf_find_accessor(prim, cgltf_attribute_type_normal, 0);
	const cgltf_accessor *uv = cgltf_find_accessor(prim, cgltf_attribute_type_texcoord, 0);
	const cgltf_accessor *col = cgltf_find_accessor(prim, cgltf_attribute_type_color, 0);
	const cgltf_accessor *jo = skinned ? cgltf_find_accessor(prim, cgltf_attribute_type_joints, 0) : NULL;
	const cgltf_accessor *we = skinned ? cgltf_find_accessor(prim, cgltf_attribute_type_weights, 0) : NULL;
	u32 v;

	for (v = 0; v < count; v++)
	{
		EngHcSrc *s = &src[first + v];
		EngineVertex *b = &base[first + v];
		float t[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		float c[4] = {1.0f, 1.0f, 1.0f, 1.0f};
		int k;

		memset(s, 0, sizeof(*s));
		memset(b, 0, sizeof(*b));
		cgltf_accessor_read_float(pos, v, s->p, 3);
		if ((nrm != NULL) && (nrm->count == pos->count) && cgltf_accessor_read_float(nrm, v, s->n, 3))
		{
			s->hasNormal = 1;
		}
		if ((uv != NULL) && (uv->count == pos->count))
		{
			cgltf_accessor_read_float(uv, v, t, 2);
		}
		b->texcoord[0] = t[0];
		b->texcoord[1] = t[1];
		if ((col != NULL) && (col->count == pos->count))
		{
			cgltf_accessor_read_float(col, v, c, cgltf_num_components(col->type));
		}
		for (k = 0; k < 4; k++)
		{
			const float x = c[k] * 255.0f + 0.5f;

			b->color[k] = (u8)((x <= 0.0f) ? 0 : ((x >= 255.0f) ? 255 : (int)x));
		}
		if ((jo != NULL) && (we != NULL) && (jo->count == pos->count) && (we->count == pos->count))
		{
			cgltf_uint jj[4] = {0, 0, 0, 0};
			float sum;

			cgltf_accessor_read_uint(jo, v, jj, 4);
			cgltf_accessor_read_float(we, v, s->w, 4);
			sum = s->w[0] + s->w[1] + s->w[2] + s->w[3];
			for (k = 0; k < 4; k++)
			{
				s->j[k] = (u8)((jj[k] < 255u) ? jj[k] : 0u);
				s->w[k] = (sum > 0.0f) ? (s->w[k] / sum) : ((k == 0) ? 1.0f : 0.0f);
			}
		}
		else
		{
			s->j[0] = (u8)entry;
			s->w[0] = 1.0f;
		}
	}
}

// The materials and textures of the file (CPU side: what to make).
internal int EngHc_TextureOf(EngHcChar *c, const cgltf_data *gltf, const cgltf_texture_view *view, int linear)
{
	const cgltf_texture *tex = view->texture;
	int image;
	int wrapU = NATIVE_GFX_WRAP_REPEAT;
	int wrapV = NATIVE_GFX_WRAP_REPEAT;
	int k;

	if ((tex == NULL) || (tex->image == NULL))
	{
		return -1;
	}
	image = (int)(tex->image - gltf->images);
	if (tex->sampler != NULL)
	{
		wrapU = EngHc_WrapOf((int)tex->sampler->wrap_s);
		wrapV = EngHc_WrapOf((int)tex->sampler->wrap_t);
	}
	for (k = 0; k < c->texCount; k++)
	{
		if ((c->tex[k].image == image) && (c->tex[k].linear == linear) && (c->tex[k].wrapU == wrapU) && (c->tex[k].wrapV == wrapV))
		{
			return k;
		}
	}
	if (c->texCount >= (int)ENG_HC_TEXTURES_MAX)
	{
		c->texOverflow = 1;
		return -1;
	}
	c->tex[c->texCount].image = image;
	c->tex[c->texCount].linear = linear;
	c->tex[c->texCount].wrapU = wrapU;
	c->tex[c->texCount].wrapV = wrapV;
	c->tex[c->texCount].texture = 0xffffffffu;
	return c->texCount++;
}

internal void EngHc_Materials(EngHcChar *c, const cgltf_data *gltf)
{
	u32 i;

	c->matCount = (int)gltf->materials_count + 2;
	for (i = 0; i < (u32)c->matCount; i++)
	{
		EngHcMat *m = &c->mats[i];

		memset(m, 0, sizeof(*m));
		m->baseTex = -1;
		m->normalTex = -1;
		m->factor[0] = m->factor[1] = m->factor[2] = m->factor[3] = 1.0f;
		m->normalScale = 1.0f;
		if (i < (u32)gltf->materials_count)
		{
			const cgltf_material *g = &gltf->materials[i];

			if (g->has_pbr_metallic_roughness)
			{
				memcpy(m->factor, g->pbr_metallic_roughness.base_color_factor, sizeof(m->factor));
				m->baseTex = EngHc_TextureOf(c, gltf, &g->pbr_metallic_roughness.base_color_texture, 0);
			}
			if (g->normal_texture.texture != NULL)
			{
				m->normalTex = EngHc_TextureOf(c, gltf, &g->normal_texture, 1);
				m->normalScale = (g->normal_texture.scale != 0.0f) ? g->normal_texture.scale : 1.0f;
			}
			m->mask = (g->alpha_mode != cgltf_alpha_mode_opaque);
			m->cutoff = (g->alpha_mode == cgltf_alpha_mode_mask) ? g->alpha_cutoff : 0.5f;
			m->doubleSided = g->double_sided ? 1 : 0;
			m->unlit = g->unlit ? 1 : 0;
		}
		else
		{
			// The default material (a primitive without one) and the standard wheels': white, two-sided.
			m->doubleSided = 1;
		}
	}
}

// The material index of a primitive in c->mats.
internal int EngHc_MaterialOf(const cgltf_data *gltf, const cgltf_primitive *prim)
{
	return (prim->material != NULL) ? (int)(prim->material - gltf->materials) : (int)gltf->materials_count;
}

// The hull: per palette entry the box of its vertices (local), through the
// rest pose and every clip at ENG_HC_HULL_SAMPLES times; the wheels as their
// middles +- their radius. Model units.
internal void EngHc_Hull(EngHcChar *c, RldAnimTrs *pose, float *worlds)
{
	float *box = (float *)malloc((size_t)c->paletteCount * 6u * sizeof(float));
	int sample;
	int k;
	u32 e;
	u32 v;

	c->hullMin[0] = c->hullMin[1] = c->hullMin[2] = 1e30f;
	c->hullMax[0] = c->hullMax[1] = c->hullMax[2] = -1e30f;
	if (box == NULL)
	{
		return;
	}
	for (e = 0; e < c->paletteCount; e++)
	{
		box[e * 6 + 0] = box[e * 6 + 1] = box[e * 6 + 2] = 1e30f;
		box[e * 6 + 3] = box[e * 6 + 4] = box[e * 6 + 5] = -1e30f;
	}
	for (v = 0; v < c->vertexCount; v++)
	{
		const EngHcSrc *s = &c->src[v];

		for (k = 0; k < 4; k++)
		{
			if ((s->w[k] > 0.0f) && ((u32)s->j[k] < c->paletteCount))
			{
				float *b = &box[(u32)s->j[k] * 6u];
				int a;

				for (a = 0; a < 3; a++)
				{
					b[a] = (s->p[a] < b[a]) ? s->p[a] : b[a];
					b[3 + a] = (s->p[a] > b[3 + a]) ? s->p[a] : b[3 + a];
				}
			}
		}
	}
	for (sample = -1; sample < ENGINE_HIRES_CLIPS * ENG_HC_HULL_SAMPLES; sample++)
	{
		const int clip = (sample < 0) ? -1 : (sample / ENG_HC_HULL_SAMPLES);

		if ((clip >= 0) && ((c->clipMask & (1u << (u32)clip)) == 0u))
		{
			continue;
		}
		RldAnim_PoseRest(&c->skel, pose);
		if (clip >= 0)
		{
			const RldAnimClip *cl = &c->clip[clip];
			const float t = cl->start + (cl->end - cl->start) * (float)(sample % ENG_HC_HULL_SAMPLES) / (float)(ENG_HC_HULL_SAMPLES - 1);

			RldAnim_SampleClip(cl, t, pose, c->skel.nodeCount);
		}
		RldAnim_NodeMatrices(&c->skel, pose, worlds);
		for (e = 0; e < c->wheelFirst; e++)
		{
			const float *b = &box[e * 6u];
			float m[16];
			int corner;

			if (b[0] > b[3])
			{
				continue;
			}
			if (e < c->joints)
			{
				RldAnim_JointMatrix(&c->skel, worlds, e, m);
			}
			else
			{
				memcpy(m, &worlds[(size_t)c->rigidNode[e - c->joints] * 16u], sizeof(m));
			}
			for (corner = 0; corner < 8; corner++)
			{
				const float x = (corner & 1) ? b[3] : b[0];
				const float y = (corner & 2) ? b[4] : b[1];
				const float z = (corner & 4) ? b[5] : b[2];
				int a;

				for (a = 0; a < 3; a++)
				{
					const float q = (m[a] * x + m[4 + a] * y + m[8 + a] * z + m[12 + a]) * ENG_HC_UNITS;

					c->hullMin[a] = (q < c->hullMin[a]) ? q : c->hullMin[a];
					c->hullMax[a] = (q > c->hullMax[a]) ? q : c->hullMax[a];
				}
			}
		}
		for (k = 0; k < 4; k++)
		{
			float mid[3];
			float r;
			int a;

			if (c->ownWheels)
			{
				const float *w = &worlds[(size_t)c->wheelNode[k] * 16u];

				mid[0] = w[12] * ENG_HC_UNITS;
				mid[1] = w[13] * ENG_HC_UNITS;
				mid[2] = w[14] * ENG_HC_UNITS;
				r = c->wheelRadius[k];
			}
			else
			{
				Engine_HiresWheelMiddle(k, 0x1000, 0x1000, 0x1000, mid);
				r = ENG_HC_STD_RADIUS;
			}
			for (a = 0; a < 3; a++)
			{
				c->hullMin[a] = ((mid[a] - r) < c->hullMin[a]) ? (mid[a] - r) : c->hullMin[a];
				c->hullMax[a] = ((mid[a] + r) > c->hullMax[a]) ? (mid[a] + r) : c->hullMax[a];
			}
		}
	}
	free(box);
}

// Everything of a character that needs no device, from an opened model
// (RldHires_Open said usable, RldHires_CheckBudget within). 1 = made; 0 with
// *rule and detail (the character is left empty). pose and worlds: scratch of
// the file's node count (the hull).
internal int EngHc_BuildCpu(const RldHiresModel *m, EngHcChar *c, const char **rule, char *detail, int detailSize)
{
	const cgltf_data *gltf = m->data;
	const char *why = "";
	u32 matIndices[ENG_HC_MATERIALS_MAX];
	u32 matCursor[ENG_HC_MATERIALS_MAX];
	u32 vertices = 0;
	u32 indices = 0;
	u32 vfirst;
	RldAnimTrs *pose = NULL;
	float *worlds = NULL;
	int n;
	int k;

	*rule = "";
	detail[0] = '\0';
	memset(matIndices, 0, sizeof(matIndices));
	if (gltf == NULL)
	{
		EngHc_Fail(rule, detail, detailSize, "HRG-7", "the model is not open");
		return 0;
	}
	if ((u32)gltf->materials_count > RLDHIRES_MATERIALS_MAX)
	{
		snprintf(detail, (size_t)detailSize, "the model has %u materials - the engine draws at most 62; join materials that share a texture",
		         (unsigned)gltf->materials_count);
		*rule = "HRG-32";
		return 0;
	}
	if (!RldAnim_SkeletonFromGltf(gltf, (m->skin >= 0) ? &gltf->skins[m->skin] : NULL, &c->skel, &why))
	{
		EngHc_Fail(rule, detail, detailSize, "HRG-13", why);
		return 0;
	}
	for (k = 0; k < ENGINE_HIRES_CLIPS; k++)
	{
		u32 skipped = 0;

		if (m->clipAnim[k] < 0)
		{
			continue;
		}
		if (!RldAnim_ClipFromGltf(gltf, &gltf->animations[m->clipAnim[k]], &c->clip[k], &skipped, &why))
		{
			char text[160];

			snprintf(text, sizeof(text), "clip %s: %s", Engine_HiresClipName(k), why);
			EngHc_Fail(rule, detail, detailSize, "HRG-7", text);
			EngHc_FreeCpu(c);
			return 0;
		}
		c->clipMask |= 1u << (u32)k;
	}

	// The palette layout and the counts.
	c->joints = (m->skin >= 0) ? m->joints : 0u;
	c->rigidCount = 0;
	c->rigidNode = (u32 *)malloc((size_t)(m->drawNodeCount + 1) * sizeof(u32));
	if (c->rigidNode == NULL)
	{
		EngHc_Fail(rule, detail, detailSize, "HRG-7", "out of memory");
		EngHc_FreeCpu(c);
		return 0;
	}
	EngHc_Materials(c, gltf);
	if (c->texOverflow)
	{
		snprintf(detail, (size_t)detailSize, "the materials need more than %u texture uploads - the engine makes at most 64 (one per image, colour space and wrap mode)",
		         (unsigned)ENG_HC_TEXTURES_MAX);
		*rule = "HRG-35";
		EngHc_FreeCpu(c);
		return 0;
	}
	for (n = 0; n < m->drawNodeCount; n++)
	{
		const cgltf_node *node = &gltf->nodes[m->drawNodes[n]];
		cgltf_size p;

		if (node->skin == NULL)
		{
			c->rigidNode[c->rigidCount++] = (u32)m->drawNodes[n];
		}
		for (p = 0; p < node->mesh->primitives_count; p++)
		{
			const cgltf_primitive *prim = &node->mesh->primitives[p];
			const cgltf_accessor *pos = cgltf_find_accessor(prim, cgltf_attribute_type_position, 0);
			const u32 ic = (prim->indices != NULL) ? (u32)prim->indices->count : ((pos != NULL) ? (u32)pos->count : 0u);

			if ((pos == NULL) || (prim->type != cgltf_primitive_type_triangles))
			{
				continue;
			}
			vertices += (u32)pos->count;
			indices += ic - (ic % 3u);
			matIndices[EngHc_MaterialOf(gltf, prim)] += ic - (ic % 3u);
		}
	}
	c->wheelFirst = c->joints + c->rigidCount;
	c->paletteCount = c->wheelFirst + 4u;
	c->ownWheels = (m->wheelNode[0] >= 0) && (m->wheelNode[1] >= 0) && (m->wheelNode[2] >= 0) && (m->wheelNode[3] >= 0);
	for (k = 0; k < 4; k++)
	{
		if (c->ownWheels)
		{
			const cgltf_mesh *mesh = gltf->nodes[m->wheelNode[k]].mesh;
			cgltf_size p;

			for (p = 0; p < mesh->primitives_count; p++)
			{
				const cgltf_primitive *prim = &mesh->primitives[p];
				const cgltf_accessor *pos = cgltf_find_accessor(prim, cgltf_attribute_type_position, 0);
				const u32 ic = (prim->indices != NULL) ? (u32)prim->indices->count : ((pos != NULL) ? (u32)pos->count : 0u);

				if ((pos == NULL) || (prim->type != cgltf_primitive_type_triangles))
				{
					continue;
				}
				vertices += (u32)pos->count;
				indices += ic - (ic % 3u);
				matIndices[EngHc_MaterialOf(gltf, prim)] += ic - (ic % 3u);
			}
		}
		else
		{
			vertices += (u32)ENG_HC_STD_VERTICES;
			indices += (u32)ENG_HC_STD_INDICES;
			matIndices[gltf->materials_count + 1u] += (u32)ENG_HC_STD_INDICES;
		}
	}
	if ((vertices == 0u) || (vertices > ENG_HC_VERTICES_MAX))
	{
		snprintf(detail, (size_t)detailSize, "the model has %u vertices (the wheels counted) - the engine holds at most 1048576; export with shared vertices (indexed)",
		         vertices);
		*rule = "HRG-33";
		EngHc_FreeCpu(c);
		return 0;
	}
	if (c->paletteCount > ENG_HC_PALETTE_MAX)
	{
		snprintf(detail, (size_t)detailSize, "%u joints and %u separate mesh objects need %u skinning entries - the engine has 255; join mesh objects", c->joints,
		         c->rigidCount, c->paletteCount);
		*rule = "HRG-34";
		EngHc_FreeCpu(c);
		return 0;
	}
	c->src = (EngHcSrc *)malloc((size_t)vertices * sizeof(EngHcSrc));
	c->base = (EngineVertex *)malloc((size_t)vertices * sizeof(EngineVertex));
	c->indices = (u32 *)malloc((size_t)((indices > 0u) ? indices : 1u) * sizeof(u32));
	pose = (RldAnimTrs *)malloc((size_t)c->skel.nodeCount * sizeof(RldAnimTrs));
	worlds = (float *)malloc((size_t)c->skel.nodeCount * 16u * sizeof(float));
	if ((c->src == NULL) || (c->base == NULL) || (c->indices == NULL) || (pose == NULL) || (worlds == NULL))
	{
		free(pose);
		free(worlds);
		EngHc_Fail(rule, detail, detailSize, "HRG-7", "out of memory");
		EngHc_FreeCpu(c);
		return 0;
	}
	c->vertexCount = vertices;
	c->indexCount = indices;
	c->triangles = indices / 3u;
	c->hostBytes = vertices * (u32)(sizeof(EngHcSrc) + sizeof(EngineVertex)) + c->skel.bytes;
	for (k = 0; k < ENGINE_HIRES_CLIPS; k++)
	{
		c->hostBytes += c->clip[k].bytes;
	}

	// The ranges: one per material in use, in material order.
	{
		u32 at = 0;

		c->rangeCount = 0;
		for (k = 0; k < c->matCount; k++)
		{
			matCursor[k] = at;
			if (matIndices[k] > 0u)
			{
				c->ranges[c->rangeCount].firstIndex = at;
				c->ranges[c->rangeCount].indexCount = matIndices[k];
				c->ranges[c->rangeCount].material = (u16)k; // the character's material; made a table index per level
				c->rangeCount++;
			}
			at += matIndices[k];
		}
	}

	// The vertices and indices.
	vfirst = 0;
	for (n = 0; n < m->drawNodeCount + 4; n++)
	{
		const int wheel = n - m->drawNodeCount;
		const cgltf_node *node;
		u32 entry;
		cgltf_size p;
		int skinned;

		if ((wheel >= 0) && !c->ownWheels)
		{
			const int mat = (int)gltf->materials_count + 1;

			EngHc_StdWheel(&c->src[vfirst], &c->base[vfirst], &c->indices[matCursor[mat]], vfirst, c->wheelFirst + (u32)wheel);
			matCursor[mat] += (u32)ENG_HC_STD_INDICES;
			vfirst += (u32)ENG_HC_STD_VERTICES;
			continue;
		}
		node = (wheel >= 0) ? &gltf->nodes[m->wheelNode[wheel]] : &gltf->nodes[m->drawNodes[n]];
		skinned = (wheel < 0) && (node->skin != NULL);
		entry = 0;
		if (wheel >= 0)
		{
			entry = c->wheelFirst + (u32)wheel;
		}
		else if (!skinned)
		{
			u32 r;

			for (r = 0; r < c->rigidCount; r++)
			{
				if (c->rigidNode[r] == (u32)m->drawNodes[n])
				{
					entry = c->joints + r;
				}
			}
		}
		for (p = 0; p < node->mesh->primitives_count; p++)
		{
			const cgltf_primitive *prim = &node->mesh->primitives[p];
			const cgltf_accessor *pos = cgltf_find_accessor(prim, cgltf_attribute_type_position, 0);
			const int mat = EngHc_MaterialOf(gltf, prim);
			u32 count;
			u32 ic;
			u32 q;

			if ((pos == NULL) || (prim->type != cgltf_primitive_type_triangles))
			{
				continue;
			}
			count = (u32)pos->count;
			ic = (prim->indices != NULL) ? (u32)prim->indices->count : count;
			ic -= ic % 3u;
			EngHc_ReadPrimitive(prim, skinned, entry, c->src, c->base, vfirst, count);
			for (q = 0; q < ic; q++)
			{
				const u32 idx = (prim->indices != NULL) ? (u32)cgltf_accessor_read_index(prim->indices, q) : q;

				c->indices[matCursor[mat]++] = vfirst + ((idx < count) ? idx : 0u);
			}
			vfirst += count;
		}
	}

	// The wheel nodes at rest: the radius in model units, the roll sign and the rest yaw.
	RldAnim_PoseRest(&c->skel, pose);
	RldAnim_NodeMatrices(&c->skel, pose, worlds);
	for (k = 0; k < 4; k++)
	{
		c->wheelNode[k] = c->ownWheels ? m->wheelNode[k] : -1;
		c->wheelSign[k] = 1.0f;
		c->wheelRestYaw[k] = 0.0f;
		c->wheelRadius[k] = ENG_HC_STD_RADIUS;
		if (c->ownWheels)
		{
			const float *w = &worlds[(size_t)c->wheelNode[k] * 16u];
			const double yLen = sqrt((double)w[4] * w[4] + (double)w[5] * w[5] + (double)w[6] * w[6]);

			c->wheelRadius[k] = (float)((double)m->wheelRadius[k] * yLen * ENG_HC_UNITS);
			c->wheelSign[k] = (w[0] < 0.0f) ? -1.0f : 1.0f;
			c->wheelRestYaw[k] = (float)atan2(-(double)w[2], (double)w[0]);
		}
	}
	EngHc_Hull(c, pose, worlds);
	free(pose);
	free(worlds);
	for (k = 0; k < 3; k++)
	{
		if (!((c->hullMax[k] - c->hullMin[k]) <= ENG_HC_HULL_MAX))
		{
			snprintf(detail, (size_t)detailSize, "with its clips the model reaches %.2f m along %c - at most 4 m; keep every pose inside 4 m", (double)((c->hullMax[k] - c->hullMin[k]) / ENG_HC_UNITS),
			         "xyz"[k]);
			*rule = "HRG-18";
			EngHc_FreeCpu(c);
			return 0;
		}
	}
	for (k = 0; k < 3; k++)
	{
		const float grow = (c->hullMax[k] - c->hullMin[k]) * ENG_HC_HULL_MARGIN;

		c->hullMin[k] -= grow;
		c->hullMax[k] += grow;
	}

	// The summary of the clips for the ready line.
	{
		size_t used = 0;
		int present = 0;

		for (k = 0; k < ENGINE_HIRES_CLIPS; k++)
		{
			present += ((c->clipMask & (1u << (u32)k)) != 0u) ? 1 : 0;
		}
		used += (size_t)snprintf(c->clipText, sizeof(c->clipText), "%d/12", present);
		for (k = 0; (k < ENGINE_HIRES_CLIPS) && (used < sizeof(c->clipText)); k++)
		{
			if ((c->clipMask & (1u << (u32)k)) == 0u)
			{
				int full = 0;
				const int r = EngHc_Resolve(c->clipMask, k, &full);

				used += (size_t)snprintf(c->clipText + used, sizeof(c->clipText) - used, "%s%s->%s%s", (used <= 5) ? " (missing " : ", ", Engine_HiresClipName(k),
				                         Engine_HiresClipName(r), full ? " full" : "");
			}
		}
		if ((present < ENGINE_HIRES_CLIPS) && (used < sizeof(c->clipText)))
		{
			snprintf(c->clipText + used, sizeof(c->clipText) - used, ")");
		}
	}
	return 1;
}

// ---------------------------------------------------------------------------
// THE GPU SIDE (load stage only).

// Parks a buffer pair (0xffffffff for none) for the next load's EngHc_GraveEmpty;
// destroys at once only when the grave is full (logged).
internal void EngHc_GravePark(u32 vertexBuffer, u32 indexBuffer)
{
	if (s_hc.graveCount < ENG_HC_GRAVE_MAX)
	{
		s_hc.graveVB[s_hc.graveCount] = vertexBuffer;
		s_hc.graveIB[s_hc.graveCount] = indexBuffer;
		s_hc.graveCount++;
		return;
	}
	Platform_Log("[CTR Engine] hires chars: the grave is full - a buffer is destroyed during its load\n");
	Engine_RenderStaticBuffersFree(vertexBuffer, indexBuffer);
}

// At the start of a load, before anything of this load is parked: what the
// earlier loads let go.
internal void EngHc_GraveEmpty(void)
{
	int i;

	for (i = 0; i < s_hc.graveCount; i++)
	{
		Engine_RenderStaticBuffersFree(s_hc.graveVB[i], s_hc.graveIB[i]);
	}
	s_hc.graveCount = 0;
}

internal void EngHc_FreeGpu(EngHcChar *c)
{
	int k;

	for (k = 0; k < c->texCount; k++)
	{
		if ((c->tex[k].texture != 0xffffffffu) && (c->tex[k].texture != 0u))
		{
			Engine_TextureDestroy(c->tex[k].texture);
		}
		c->tex[k].texture = 0xffffffffu;
	}
	if ((c->indexBuffer != 0xffffffffu) && (c->indexBuffer != 0u))
	{
		EngHc_GravePark(0xffffffffu, c->indexBuffer);
	}
	c->indexBuffer = 0xffffffffu;
}

internal void EngHc_FreeChar(EngHcChar *c)
{
	EngHc_FreeGpu(c);
	EngHc_FreeCpu(c);
	memset(c, 0, sizeof(*c));
	c->indexBuffer = 0xffffffffu;
	c->entry = -1;
}

// The textures (PNG decoded, edges rounded up to a power of two, the whole
// mip chain, sRGB colour or linear normal map) and the index buffer. *peak
// grows by the largest decoded image. 1 = made.
internal int EngHc_Upload(const RldHiresModel *m, EngHcChar *c, const char **rule, char *detail, int detailSize, u32 *peak)
{
	size_t used = 0;
	int k;

	c->texText[0] = '\0';
	for (k = 0; k < c->texCount; k++)
	{
		EngHcTex *t = &c->tex[k];
		struct RldPngImage png;
		const char *pngWhy = NULL;
		const u8 *bytes;
		u32 size = 0;
		EngineTextureDesc desc;
		u8 *rgba;
		int w;
		int h;

		bytes = RldHires_ImageBytes(m, t->image, &size);
		memset(&png, 0, sizeof(png));
		if ((bytes == NULL) || (RldPng_Decode(bytes, size, &png, &pngWhy) != RLDPNG_OK))
		{
			snprintf(detail, (size_t)detailSize, "image %d: %s", t->image, (pngWhy != NULL) ? pngWhy : "no bytes");
			*rule = "HRG-16";
			return 0;
		}
		w = EngineStatic_UpEdge((int)png.width);
		h = EngineStatic_UpEdge((int)png.height);
		if ((w == 0) || (h == 0))
		{
			RldPng_Free(&png);
			snprintf(detail, (size_t)detailSize, "image %d: an edge above 2048", t->image);
			*rule = "HRG-17";
			return 0;
		}
		if ((c->texBytes + (u64)w * (u64)h * 4u) > (u64)RLDHIRES_BUDGET_TEXTURE_BYTES)
		{
			RldPng_Free(&png);
			snprintf(detail, (size_t)detailSize, "over budget: %llu texture bytes uploaded (RGBA8, an image used two ways counts twice), the limit is %u",
			         (unsigned long long)(c->texBytes + (u64)w * (u64)h * 4u), (unsigned)RLDHIRES_BUDGET_TEXTURE_BYTES);
			*rule = "HRG-27";
			return 0;
		}
		rgba = png.rgba;
		if ((w != (int)png.width) || (h != (int)png.height))
		{
			rgba = (u8 *)malloc((size_t)w * (size_t)h * 4u);
			if ((rgba == NULL) || !EngineStatic_ResampleUp(png.rgba, (int)png.width, (int)png.height, rgba, w, h))
			{
				free(rgba);
				RldPng_Free(&png);
				EngHc_Fail(rule, detail, detailSize, "HRG-16", "out of memory (image)");
				return 0;
			}
		}
		{
			const u32 bytesNow = (u32)png.width * png.height * 4u + ((rgba != png.rgba) ? (u32)w * (u32)h * 4u : 0u);

			*peak = (bytesNow > *peak) ? bytesNow : *peak;
		}
		memset(&desc, 0, sizeof(desc));
		desc.width = w;
		desc.height = h;
		desc.rgba = rgba;
		desc.linearData = t->linear;
		desc.oneLevel = 0;
		desc.filter = ENGINE_TEXTURE_FILTERED;
		desc.maxAnisotropy = 0;
		desc.wrapU = t->wrapU;
		desc.wrapV = t->wrapV;
		desc.name = t->linear ? "hires char normal map" : "hires char base colour";
		t->texture = Engine_TextureCreate(&desc);
		t->width = w;
		t->height = h;
		if (rgba != png.rgba)
		{
			free(rgba);
		}
		RldPng_Free(&png);
		if (t->texture == 0xffffffffu)
		{
			snprintf(detail, (size_t)detailSize, "image %d: Engine_TextureCreate refused %dx%d", t->image, w, h);
			*rule = "GPU";
			return 0;
		}
		c->texBytes += (u64)w * (u64)h * 4u;
		if (used < sizeof(c->texText))
		{
			used += (size_t)snprintf(c->texText + used, sizeof(c->texText) - used, " %dx%d%s", w, h, t->linear ? "n" : "");
		}
	}
	(void)m;
	c->indexBuffer = Engine_RenderStaticIndexBuffer(c->indices, c->indexCount);
	if (c->indexBuffer == 0xffffffffu)
	{
		EngHc_Fail(rule, detail, detailSize, "GPU", "the index buffer was refused");
		return 0;
	}
	free(c->indices);
	c->indices = NULL;
	return 1;
}

// This level's materials of a character (the table was reset at the unload).
internal int EngHc_RegisterMaterials(EngHcChar *c)
{
	int k;

	if (c->materialLevel == s_hc.levelStamp)
	{
		return 1;
	}
	for (k = 0; k < c->matCount; k++)
	{
		const EngHcMat *m = &c->mats[k];
		EngineMaterial em;
		const int hasNormal = (m->normalTex >= 0) && (c->tex[m->normalTex].texture != 0xffffffffu);

		memset(&em, 0, sizeof(em));
		em.blend = m->mask ? ENGINE_BLEND_MASK : ENGINE_BLEND_OPAQUE;
		em.alphaCutoff = m->mask ? m->cutoff : 0.0f;
		memcpy(em.tint, m->factor, sizeof(em.tint));
		em.flags = m->doubleSided ? (u16)ENGINE_MATERIAL_DOUBLE_SIDED : (u16)0u;
		if ((m->baseTex >= 0) && (c->tex[m->baseTex].texture != 0xffffffffu))
		{
			Engine_TextureMaterial(&em, c->tex[m->baseTex].texture);
		}
		em.program = ENGINE_PROGRAM_NS;
		// 2 = unlit (KHR_materials_unlit), 1 = lit with its normal map, 0 = lit (engine.h, ENGINE_PROGRAM_NS).
		em.programParam[0] = m->unlit ? 2.0f : (hasNormal ? 1.0f : 0.0f);
		em.programParam[1] = m->normalScale;
		em.nativeTexture2 = hasNormal ? c->tex[m->normalTex].texture : 0xffffffffu;
		c->levelMaterial[k] = Engine_MaterialRegister(&em);
		if (c->levelMaterial[k] < 0)
		{
			return 0;
		}
	}
	c->materialLevel = s_hc.levelStamp;
	return 1;
}

// ---------------------------------------------------------------------------
// THE POSE.

// One clip into pose: its resolved clip at time (or the scrub fraction),
// sampled on top of base (NULL = the rest pose; the steer overlay passes the
// idle pose, so that the nodes the steer clip does not key keep idle's
// transform - the composition of Reload Studio's preview). *resolved and
// *local for the log (may be NULL).
internal void EngHc_EvalClipOn(const EngHcChar *c, int clip, float time, float fraction, const RldAnimTrs *base, RldAnimTrs *pose, int *resolved, float *local)
{
	int full = 0;
	const int r = EngHc_Resolve(c->clipMask, clip, &full);
	float t = 0.0f;

	if (base != NULL)
	{
		memcpy(pose, base, (size_t)c->skel.nodeCount * sizeof(RldAnimTrs));
	}
	else
	{
		RldAnim_PoseRest(&c->skel, pose);
	}
	if (r != ENGINE_HIRES_CLIP_REST)
	{
		const RldAnimClip *cl = &c->clip[r];

		t = EngHc_LocalTime(EngHc_PlayOf(r), time, fraction, RldAnim_ClipLength(cl), full);
		RldAnim_SampleClip(cl, RldAnim_ClipTime(cl, t, RLDANIM_PLAY_ONCE), pose, c->skel.nodeCount);
	}
	if (resolved != NULL)
	{
		*resolved = r;
	}
	if (local != NULL)
	{
		*local = t;
	}
}

internal void EngHc_EvalClip(const EngHcChar *c, int clip, float time, float fraction, RldAnimTrs *pose, int *resolved, float *local)
{
	EngHc_EvalClipOn(c, clip, time, fraction, NULL, pose, resolved, local);
}

// The pose a seat state draws (the fade over the snapshot included) into out;
// uses s_hc.poseB as scratch. info may be NULL.
internal void EngHc_StatePose(const EngHcChar *c, const EngHcSeatState *st, const RldAnimTrs *snapshot, RldAnimTrs *out, EngHcPoseInfo *info)
{
	EngHcPoseInfo i;

	memset(&i, 0, sizeof(i));
	i.want = EngHc_StateClip(st);
	i.steerClip = -2;
	i.prevClip = -2;
	if (st->state == ENG_HC_STATE_BASE)
	{
		int sideClip = ENGINE_HIRES_CLIP_STEER_LEFT;
		const float frac = EngHc_SteerFraction(st->steerRaw, &sideClip);
		int full = 0;
		const int sr = EngHc_Resolve(c->clipMask, sideClip, &full);

		EngHc_EvalClip(c, ENGINE_HIRES_CLIP_IDLE, st->baseTime, 0.0f, out, &i.clip, &i.t);
		i.steer = (st->steerRaw > 0) ? frac : -frac;
		// Only a real steer clip blends; a steer that falls back to idle IS the
		// idle above. The overlay is the steer clip sampled ON the idle pose,
		// then blended over idle by the share (as Reload Studio's preview).
		if ((frac > 0.0f) && (EngHc_PlayOf(sr) == RLDHIRES_PLAY_POSE) && (sr != ENGINE_HIRES_CLIP_REST))
		{
			EngHc_EvalClipOn(c, sideClip, 0.0f, frac, out, s_hc.poseB, &i.steerClip, NULL);
			RldAnim_BlendPose(out, s_hc.poseB, frac, out, c->skel.nodeCount);
		}
	}
	else if (EngHc_PlayOf(EngHc_Resolve(c->clipMask, i.want, NULL)) == RLDHIRES_PLAY_POSE)
	{
		// A drift that falls back to its steer clip at full lock: on the idle pose, as the steer overlay.
		EngHc_EvalClip(c, ENGINE_HIRES_CLIP_IDLE, st->baseTime, 0.0f, s_hc.poseB, NULL, NULL);
		EngHc_EvalClipOn(c, i.want, st->stateTime, 0.0f, s_hc.poseB, out, &i.clip, &i.t);
	}
	else
	{
		EngHc_EvalClip(c, i.want, st->stateTime, 0.0f, out, &i.clip, &i.t);
	}
	i.blend = EngHc_FadeWeight(st);
	if (st->fading && (snapshot != NULL))
	{
		RldAnim_BlendPose(snapshot, out, i.blend, out, c->skel.nodeCount);
		EngHcSeatState prev;

		memset(&prev, 0, sizeof(prev));
		prev.state = st->prevState;
		prev.forceClip = st->prevForce;
		i.prevClip = EngHc_StateClip(&prev);
	}
	if (info != NULL)
	{
		*info = i;
	}
}

// ---------------------------------------------------------------------------
// THE SKINNING.

// The palette of a pose at a place (s_hc.worlds and s_hc.palette); the wheel
// read-back of wheel 0 into *yaw and *roll.
internal void EngHc_Palette(const EngHcChar *c, const RldAnimTrs *pose, const EngHcPlace *pl, float *yaw, float *roll)
{
	EngineMat4 m64 = pl->world;
	double body[3][3]; // the 3x3 of world, row-major: the own wheels' outer (they keep the model's proportions)
	u32 e;
	int w;

	for (e = 0; e < 3u; e++)
	{
		u32 col;

		for (col = 0; col < 3u; col++)
		{
			body[e][col] = (double)pl->world.m[col * 4u + e];
		}
	}
	EngHc_MatScale(&m64, ENG_HC_UNITS);
	RldAnim_NodeMatrices(&c->skel, pose, s_hc.worlds);
	for (e = 0; e < c->joints; e++)
	{
		float jm[16];

		RldAnim_JointMatrix(&c->skel, s_hc.worlds, e, jm);
		RldAnim_PaletteEntry(m64.m, jm, &s_hc.palette[e * 12u]);
	}
	for (e = 0; e < c->rigidCount; e++)
	{
		RldAnim_PaletteEntry(m64.m, &s_hc.worlds[(size_t)c->rigidNode[e] * 16u], &s_hc.palette[(c->joints + e) * 12u]);
	}
	*yaw = 0.0f;
	*roll = 0.0f;
	for (w = 0; w < 4; w++)
	{
		double F0[3][3];
		double unrolled[3][3];
		float wm[16];

		Engine_HiresWheelFrame(w, pl->steer, 0.0, pl->hazard, F0);
		if (!c->ownWheels)
		{
			double F[3][3];

			Engine_HiresWheelFrame(w, pl->steer, pl->roll[w], pl->hazard, F);
			if (pl->inst != NULL)
			{
				float middle[3];
				EngineMat4 out;

				// The prototype's rule exactly (engine_hires.c EngHires_WheelWorld).
				Engine_HiresWheelMiddle(w, pl->inst->scale.x, pl->inst->scale.y, pl->inst->scale.z, middle);
				EngHires_WheelWorld(pl->inst, F, middle, pl->k, &out);
				memcpy(wm, out.m, sizeof(wm));
			}
			else
			{
				float middle[3];
				double r[3][3];
				double t[3];
				int a;

				Engine_HiresWheelMiddle(w, 0x1000, 0x1000, 0x1000, middle);
				EngHc_Mul3(pl->rot, F, r);
				for (a = 0; a < 3; a++)
				{
					t[a] = (double)pl->world.m[a] * middle[0] + (double)pl->world.m[4 + a] * middle[1] + (double)pl->world.m[8 + a] * middle[2] +
					       (double)pl->world.m[12 + a];
				}
				EngHc_WheelMatrix(r, t, wm);
			}
			EngHc_Mul3(pl->rot, F0, unrolled);
		}
		else
		{
			const float *n = &s_hc.worlds[(size_t)c->wheelNode[w] * 16u];
			const double sr = (double)c->wheelSign[w] * pl->roll[w];
			double Wn[3][3];
			double Rx[3][3] = {{1.0, 0.0, 0.0}, {0.0, cos(sr), -sin(sr)}, {0.0, sin(sr), cos(sr)}};
			double a1[3][3];
			double r[3][3];
			double t[3];
			int a;
			int b;

			for (a = 0; a < 3; a++)
			{
				for (b = 0; b < 3; b++)
				{
					Wn[a][b] = (double)n[b * 4 + a] * ENG_HC_UNITS;
				}
			}
			EngHc_Mul3(body, F0, a1);
			EngHc_Mul3(a1, Wn, unrolled);
			EngHc_Mul3(unrolled, Rx, r);
			for (a = 0; a < 3; a++)
			{
				const double p0 = (double)n[12] * ENG_HC_UNITS;
				const double p1 = (double)n[13] * ENG_HC_UNITS;
				const double p2 = (double)n[14] * ENG_HC_UNITS;

				t[a] = (double)pl->world.m[a] * p0 + (double)pl->world.m[4 + a] * p1 + (double)pl->world.m[8 + a] * p2 + (double)pl->world.m[12 + a];
			}
			EngHc_WheelMatrix(r, t, wm);
		}
		RldAnim_PaletteEntry(NULL, wm, &s_hc.palette[(c->wheelFirst + (u32)w) * 12u]);
		if (w == 0)
		{
			// THE READ-BACK from the drawn matrix: the axle (column 0) into kart
			// space through the inverse of the outer rotation, its yaw less the
			// rest yaw; the roll as the angle of the drawn rim-up column (1)
			// against the unrolled frame (up = column 1, front = column 2).
			double inv[3][3];
			double axle[3];
			double up0[3];
			double fr0[3];
			double c1[3];
			double ka[3];
			double lu = 0.0;
			double lf = 0.0;
			double lc = 0.0;
			int a;

			for (a = 0; a < 3; a++)
			{
				axle[a] = wm[a];
				c1[a] = wm[4 + a];
				up0[a] = unrolled[a][1];
				fr0[a] = unrolled[a][2];
				lu += up0[a] * up0[a];
				lf += fr0[a] * fr0[a];
				lc += c1[a] * c1[a];
			}
			if (EngHires_Inverse3(c->ownWheels ? body : pl->rot, inv))
			{
				for (a = 0; a < 3; a++)
				{
					ka[a] = inv[a][0] * axle[0] + inv[a][1] * axle[1] + inv[a][2] * axle[2];
				}
				*yaw = (float)remainder(atan2(-ka[2], ka[0]) - (double)c->wheelRestYaw[0], ENG_HC_TWO_PI);
			}
			if ((lu > 0.0) && (lf > 0.0) && (lc > 0.0))
			{
				const double du = (c1[0] * up0[0] + c1[1] * up0[1] + c1[2] * up0[2]) / sqrt(lu * lc);
				const double df = (c1[0] * fr0[0] + c1[1] * fr0[1] + c1[2] * fr0[2]) / sqrt(lf * lc);

				*roll = (float)(atan2(df, du) * (double)c->wheelSign[0]);
			}
		}
	}
}

// Skins a character's pose at a place into the slot's array of this bank and
// hands it to the renderer. 1 = ready to draw. Once per slot and frame: a
// second call in the same frame keeps the first pose.
internal int EngHc_SkinSlot(int slot, int ch, const RldAnimTrs *pose, const EngHcPlace *pl)
{
	EngHcSlot *s = &s_hc.slots[slot];
	const EngHcChar *c = &s_hc.chars[ch];
	const int bank = Engine_SceneBank();
	EngineVertex *out;
	u64 started;
	u32 v;

	if ((bank < 0) || (bank >= ENGINE_SCENE_BANKS) || (s->stream == 0xffffffffu) || (s->stream == 0u) || (s->capacity < c->vertexCount) || (s->cpu[bank] == NULL))
	{
		return 0;
	}
	if (s->skinnedFrame == s_hc.frame + 1u)
	{
		if (s->skinnedChar == ch)
		{
			return 1;
		}
		// ONE CHARACTER PER SLOT AND FRAME: the renderer keeps the array's pointer
		// until the draw, so a second character would replace the first one's
		// vertices under its draws. Refused (counted, logged once).
		s_hc.refusedSlotSkins++;
		if (!s_hc.refusedSlotLogged)
		{
			s_hc.refusedSlotLogged = 1;
			Platform_Log("[CTR Engine] hires char: slot %d asked for a second character in one frame (%s after %s) - refused, one character per slot and frame\n",
			             slot, s_hc.chars[ch].file, s_hc.chars[s->skinnedChar].file);
		}
		return 0;
	}
	started = SDL_GetPerformanceCounter();
	out = s->cpu[bank];
	if (s->cpuChar[bank] != ch)
	{
		memcpy(out, c->base, (size_t)c->vertexCount * sizeof(EngineVertex));
		s->cpuChar[bank] = ch;
	}
	EngHc_Palette(c, pose, pl, &s->yawDrawn, &s->rollDrawn);
	for (v = 0; v < c->vertexCount; v++)
	{
		const EngHcSrc *src = &c->src[v];
		EngineVertex *o = &out[v];
		float n[3];

		RldAnim_SkinVertex(s_hc.palette, src->j, src->w, src->p, src->n, o->position, n);
		if (src->hasNormal)
		{
			o->uvBox[0] = EngHc_PackUnit(n[0]);
			o->uvBox[1] = EngHc_PackUnit(n[1]);
			o->uvBox[2] = EngHc_PackUnit(n[2]);
			o->uvBox[3] = 255;
		}
		else
		{
			o->uvBox[0] = o->uvBox[1] = o->uvBox[2] = o->uvBox[3] = 0; // "ns": the face's own normal
		}
	}
	s->skinnedFrame = s_hc.frame + 1u;
	s->skinnedChar = ch;
	s_hc.skins++;
	s_hc.skinnedVertices += c->vertexCount;
	s_hc.skinMs += (double)(SDL_GetPerformanceCounter() - started) * 1000.0 / (double)SDL_GetPerformanceFrequency();
	return Engine_RenderStreamSource(s->stream, out, c->vertexCount);
}

// The slot's mesh of a character in this level (made at its first use: CPU only).
internal int EngHc_SlotMesh(int slot, int ch)
{
	EngHcSlot *s = &s_hc.slots[slot];
	EngHcChar *c = &s_hc.chars[ch];
	EngineExternalMeshDesc desc;
	EngineRange ranges[ENG_HC_MATERIALS_MAX];
	u32 r;

	if ((s->meshLevel[ch] == s_hc.levelStamp) && (s->mesh[ch] >= 0))
	{
		return s->mesh[ch];
	}
	if (!EngHc_RegisterMaterials(c))
	{
		return -1;
	}
	for (r = 0; r < c->rangeCount; r++)
	{
		ranges[r] = c->ranges[r];
		ranges[r].material = (u16)c->levelMaterial[c->ranges[r].material];
	}
	memset(&desc, 0, sizeof(desc));
	desc.vertexBuffer = s->stream;
	desc.indexBuffer = c->indexBuffer;
	desc.verticesPerFrame = c->vertexCount;
	desc.frames = 1;
	desc.ranges = ranges;
	desc.rangeCount = c->rangeCount;
	desc.name = "hires char";
	s->mesh[ch] = Engine_MeshCreateExternal(&desc);
	s->meshLevel[ch] = s_hc.levelStamp;
	return s->mesh[ch];
}

// The light of program "ns" for a draw in view cam (the static model's sun and style).
internal void EngHc_Light(const EngineCamera *cam, EngineDraw *draw)
{
	float eye[3] = {0.0f, 0.0f, 0.0f};

	(void)EngStatic_EyeOf(&cam->viewFromWorld, eye);
	draw->split[0] = s_hc.sun[0];
	draw->split[1] = s_hc.sun[1];
	draw->split[2] = s_hc.sun[2];
	draw->split[3] = g_cfg_engineStaticSun[3];
	draw->user[0] = eye[0];
	draw->user[1] = eye[1];
	draw->user[2] = eye[2];
	draw->user[3] = g_cfg_engineStaticSun[4];
	memcpy(draw->user2, s_hc.style, sizeof(draw->user2));
}

// One draw of the slot's skinned vertices (world units: the matrix is the
// camera's; mirror: the reflection about y = planeY in front).
internal int EngHc_Draw(int view, int mesh, const float tint[4], float depth, u32 *ot, int cell, const float *mirrorPlane)
{
	const EngineCamera *cam = Engine_FrameCamera(view);
	EngineMat4 screen;
	EngineDraw draw;

	if ((cam == NULL) || (ot == NULL))
	{
		return 0;
	}
	Engine_CameraScreenFromWorld(cam, &screen);
	memset(&draw, 0, sizeof(draw));
	draw.mesh = mesh;
	draw.frame = 0;
	draw.materialOverride = -1;
	draw.dynMaterial = -1;
	if (mirrorPlane != NULL)
	{
		EngineMat4 reflect;

		EngInst_Reflect(*mirrorPlane, &reflect);
		Engine_Mat4Mul(&screen, &reflect, &draw.clipFromModel);
	}
	else
	{
		draw.clipFromModel = screen;
	}
	memcpy(draw.tint, tint, sizeof(draw.tint));
	draw.sortDepth = depth;
	draw.otCell = (u32)cell;
	EngHc_Light(cam, &draw);
	return Engine_RenderSubmitLinked(view, &draw, ot, cell);
}

// The draws of a slot at an instance in the view of pb, as the view's retail
// handler asks (Engine_InstanceSubmit's handlers: NORMAL, SPLIT drawn whole -
// "ns" has no water line -, REFLECTION / SPECIAL REFLECTIVE with the mirror).
// Returns the draws submitted, 0 = declined.
internal int EngHc_DrawAtInstance(int slot, int ch, struct Instance *inst, struct InstDrawPerPlayer *idpp, struct PushBuffer *pb, const EngineMat4 *world)
{
	const int view = Engine_InstanceViewOf(pb);
	const EngineCamera *cam = (view >= 0) ? Engine_FrameCamera(view) : NULL;
	const int mesh = EngHc_SlotMesh(slot, ch);
	EngineInstanceDraw tints;
	const u32 handler = (u32)idpp->unkEC;
	const float planeY = (float)inst->vertSplit;
	int mirror = 0;
	int bin;
	int drawn = 0;
	float depth;

	if ((cam == NULL) || (mesh < 0) || (idpp->otRangeNormal == 0))
	{
		return 0;
	}
	if ((handler != (u32)RB_RETAIL_DRAWFUNC_NORMAL) && (handler != (u32)RB_RETAIL_DRAWFUNC_NORMAL_ALT) && (handler != (u32)RB_RETAIL_DRAWFUNC_SPLIT))
	{
		if (handler == (u32)RB_RETAIL_DRAWFUNC_REFLECTION)
		{
			mirror = 1;
		}
		else if ((handler == (u32)RB_RETAIL_DRAWFUNC_SPECIAL) && (((u32)idpp->instFlags & SPLIT_SPECIAL) == 0u))
		{
			mirror = (((u32)idpp->instFlags & REFLECTIVE) != 0u);
		}
		else
		{
			return 0;
		}
	}
	memset(&tints, 0, sizeof(tints));
	EngCmdl_Tints(inst, idpp, &tints);
	{
		const EngineVec3 origin = {world->m[12], world->m[13], world->m[14]};

		depth = Engine_Mat4MulPoint(&cam->viewFromWorld, origin).z;
	}
	bin = NativeRenderLayer_MarkerBin(idpp, 0);
	if (EngHc_Draw(view, mesh, tints.tint, depth, (u32 *)(size_t)(u32)idpp->otRangeNormal, bin, NULL))
	{
		drawn++;
	}
	if (drawn && mirror)
	{
		u32 *ot2 = (u32 *)(size_t)(u32)((idpp->otRangeSecondary != 0) ? idpp->otRangeSecondary : idpp->otRangeNormal);

		if (EngHc_Draw(view, mesh, tints.mirrorTint, depth, ot2, bin, &planeY))
		{
			drawn++;
			s_hc.mirrorDraws++;
		}
	}
	return drawn;
}

// ---------------------------------------------------------------------------
// THE LOGIC TICKS OF THE SEATS (Engine_HiresCharsCollect, before every draw).

internal const EngHcChar *EngHc_SeatCharacter(int seat)
{
	const int ch = s_hc.seatChar[seat];

	return (ch >= 0) ? &s_hc.chars[ch] : NULL;
}

internal void EngHc_SeatTick(const struct GameTracker *gGT, int seat)
{
	EngHcSeatRun *run = &s_hc.seats[seat];
	const EngHcChar *c = EngHc_SeatCharacter(seat);
	const struct Driver *d = gGT->drivers[seat];
	EngHcInput in;
	int forceClip = -1;
	int state;
	int w;

	if ((c == NULL) || (d == NULL) || (d->instSelf == NULL))
	{
		run->valid = 0;
		return;
	}
	if (!run->valid || (run->driver != d) || (run->inst != d->instSelf))
	{
		run->valid = 1;
		run->driver = d;
		run->inst = d->instSelf;
		run->timer = (u32)gGT->timer;
		run->logValid = 0;
		EngHc_SeatReset(&run->st, run->force);
		for (w = 0; w < 4; w++)
		{
			run->roll[w] = 0.0;
			run->step[w] = 0.0;
			run->radius[w] = 0.0;
		}
		return;
	}
	if ((u32)gGT->timer == run->timer)
	{
		return;
	}
	run->timer = (u32)gGT->timer;
	memset(&in, 0, sizeof(in));
	in.touch = ((d->actionsFlagSet & ACTION_TOUCH_GROUND) != 0) ? 1 : 0;
	in.kartState = (int)d->kartState;
	in.wheelRotation = (int)d->wheelRotation;
	in.multDrift = (int)d->multDrift;
	in.simpTurnState = (int)d->simpTurnState;
	in.elapsedMs = gGT->elapsedTimeMS;
	in.force = run->force;
	in.finished = ((d->actionsFlagSet & ACTION_RACE_FINISHED) != 0) ? 1 : 0;
	in.rank = (int)d->driverRank;
	EngHc_Advance(&run->st, in.elapsedMs);
	state = EngHc_Decide(&run->st, &in, &forceClip);
	if (EngHc_Changes(&run->st, state, forceClip))
	{
		// The snapshot: the pose drawn at this tick under the old state.
		if (run->snapshot != NULL)
		{
			EngHc_StatePose(c, &run->st, run->snapshot, s_hc.poseA, NULL);
			memcpy(run->snapshot, s_hc.poseA, (size_t)c->skel.nodeCount * sizeof(RldAnimTrs));
		}
		EngHc_Enter(&run->st, state, forceClip);
	}
	// The wheels: the way of the tick over each wheel's radius (the prototype's rule).
	for (w = 0; w < 4; w++)
	{
		// Own wheels scale with the body (inst->scale, its rolling plane y-z);
		// standard wheels with the wheel size (the prototype's rule).
		const double k = c->ownWheels ? (((double)d->instSelf->scale.y + (double)d->instSelf->scale.z) / 2.0 / 4096.0) : ((double)d->wheelSize / 4096.0);
		const double radius = (double)c->wheelRadius[w] * k;
		const double way = (double)d->speedApprox * (double)gGT->elapsedTimeMS / 32.0 / 256.0;

		run->radius[w] = radius;
		run->step[w] = (radius > 0.0) ? (way / radius) : 0.0;
		run->roll[w] = fmod(run->roll[w] + run->step[w], ENG_HC_TWO_PI);
		if (run->roll[w] < 0.0)
		{
			run->roll[w] += ENG_HC_TWO_PI;
		}
	}
	s_hc.ticks++;
}

void Engine_HiresCharsCollect(struct GameTracker *gGT)
{
	int seat;

	if (!g_cfg_engineHiresChars || !g_cfg_engine || !s_hc.levelReady || (gGT == NULL))
	{
		return;
	}
	s_hc.frame++;
	for (seat = 0; seat < ENGINE_HIRES_SLOT_SEATS; seat++)
	{
		if (s_hc.seatChar[seat] >= 0)
		{
			EngHc_SeatTick(gGT, seat);
		}
	}
}

// ---------------------------------------------------------------------------
// THE ROUTE (a seat's body instance, from Engine_HiresRoute) AND THE LOG.

// The hull on the screen of a view, fractions of the view (x0 y0 x1 y1); 0 when a corner is behind the camera.
internal int EngHc_HullBox(const EngHcChar *c, const EngineCamera *cam, const EngineMat4 *world, float box[4])
{
	int i;

	if ((cam->rectW <= 0) || (cam->rectH <= 0))
	{
		return 0;
	}
	box[0] = box[1] = 1e9f;
	box[2] = box[3] = -1e9f;
	for (i = 0; i < 8; i++)
	{
		EngineVec3 p;
		EngineVec3 s;

		p.x = (i & 1) ? c->hullMax[0] : c->hullMin[0];
		p.y = (i & 2) ? c->hullMax[1] : c->hullMin[1];
		p.z = (i & 4) ? c->hullMax[2] : c->hullMin[2];
		if (!Engine_ProjectPoint(cam, Engine_Mat4MulPoint(world, p), &s))
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

internal void EngHc_LogTick(const struct GameTracker *gGT, int seat, const struct Driver *d, const EngHcChar *c, const EngHcPoseInfo *info,
                            const EngineMat4 *world)
{
	EngHcSeatRun *run = &s_hc.seats[seat];
	const EngHcSlot *slot = &s_hc.slots[seat];
	const EngineCamera *cam = Engine_FrameCamera(0);
	const double dt = (gGT->elapsedTimeMS > 0) ? ((double)gGT->elapsedTimeMS / 1000.0) : 0.0;
	const double way = (double)d->speedApprox * (double)gGT->elapsedTimeMS / 32.0 / 256.0;
	float box[4] = {-1.0f, -1.0f, -1.0f, -1.0f};

	if (!g_cfg_engineHiresCharsLog || (run->logValid && (run->logTimer == (u32)gGT->timer)))
	{
		return;
	}
	run->logValid = 1;
	run->logTimer = (u32)gGT->timer;
	if (cam != NULL)
	{
		(void)EngHc_HullBox(c, cam, world, box);
	}
	s_hc.lines++;
	Platform_Log("[CTR Engine] hires char: vb %d seat %d timer %u state %s want %s clip %s t %.4f blend %.4f prev %s steer %.4f steerclip %s steerRaw %d air %d "
	             "kart %d drift %d rank %d yaw %.5f yawgame %.5f roll %.5f omega %.4f radius %.4f v %.4f pos %d %d %d wheels %s box %.4f %.4f %.4f %.4f\n",
	             Platform_GetVBlankCount(), seat, (unsigned)gGT->timer, EngHc_StateName(run->st.state), Engine_HiresClipName(info->want),
	             Engine_HiresClipName(info->clip), (double)info->t, (double)info->blend, (info->prevClip == -2) ? "none" : Engine_HiresClipName(info->prevClip),
	             (double)info->steer, (info->steerClip == -2) ? "none" : Engine_HiresClipName(info->steerClip), (int)d->wheelRotation, run->st.airTicks,
	             (int)d->kartState, run->st.driftSide, (int)d->driverRank, (double)slot->yawDrawn, (double)((int)d->wheelRotation * 4) * ENG_HC_ANGLE,
	             (double)slot->rollDrawn, (dt > 0.0) ? (run->step[0] / dt) : 0.0, run->radius[0], (dt > 0.0) ? (way / dt) : 0.0, (int)d->posCurr.x,
	             (int)d->posCurr.y, (int)d->posCurr.z, c->ownWheels ? "own" : "standard", (double)box[0], (double)box[1], (double)box[2], (double)box[3]);
}

int Engine_HiresCharOwns(const struct Driver *d)
{
	const struct GameTracker *gGT = sdata->gGT;
	int seat;

	if (!g_cfg_engineHiresChars || !g_cfg_engine || !s_hc.levelReady || (d == NULL) || (gGT == NULL))
	{
		return 0;
	}
	seat = (int)d->driverID;
	if ((seat < 0) || (seat >= ENGINE_HIRES_SLOT_SEATS) || (s_hc.seatChar[seat] < 0) || (gGT->drivers[seat] != d) || (d->instSelf == NULL))
	{
		return 0;
	}
	return (d->instSelf->model == NativeChar_SeatModel(seat)) ? 1 : 0;
}

internal int EngHc_SelectRoute(struct Instance *inst, struct InstDrawPerPlayer *idpp, struct PushBuffer *pb, const struct GameTracker *gGT); // below

int Engine_HiresCharRoute(struct Instance *inst, struct InstDrawPerPlayer *idpp, struct PushBuffer *pb)
{
	const struct GameTracker *gGT = sdata->gGT;
	const struct Driver *d;
	const EngHcChar *c;
	EngHcSeatRun *run;
	EngHcPoseInfo info;
	EngHcPlace pl;
	int seat;
	int ch;
	int drawn;
	int w;

	if (!g_cfg_engineHiresChars || !g_cfg_engine || !s_hc.levelReady || (inst == NULL) || (idpp == NULL) || (pb == NULL) || (gGT == NULL))
	{
		return 0;
	}
	for (seat = 0; seat < ENGINE_HIRES_SLOT_SEATS; seat++)
	{
		if ((gGT->drivers[seat] != NULL) && (gGT->drivers[seat]->instSelf == inst))
		{
			break;
		}
	}
	if ((seat == 0) && ((gGT->gameMode1 & MAIN_MENU) != 0) && ((inst->flags & HIDE_MODEL) == 0))
	{
		return EngHc_SelectRoute(inst, idpp, pb, gGT);
	}
	if ((seat >= ENGINE_HIRES_SLOT_SEATS) || !Engine_HiresCharOwns(gGT->drivers[seat]))
	{
		return 0;
	}
	d = gGT->drivers[seat];
	ch = s_hc.seatChar[seat];
	c = &s_hc.chars[ch];
	run = &s_hc.seats[seat];
	if ((inst->thread == NULL) || (inst->thread->object != (void *)d) || ((inst->flags & (SCREENSPACE_INSTANCE | HIDE_MODEL)) != 0) || !run->valid ||
	    (Engine_InstanceViewOf(pb) < 0))
	{
		s_hc.declined++;
		return 0;
	}
	memset(&pl, 0, sizeof(pl));
	if (!Engine_InstanceWorldMatrix(inst, &pl.world))
	{
		s_hc.declined++;
		return 0;
	}
	{
		int r;
		int k;

		pl.k = (double)d->wheelSize / 4096.0;
		for (r = 0; r < 3; r++)
		{
			for (k = 0; k < 3; k++)
			{
				pl.rot[r][k] = (double)inst->matrix.m[r][k] / 4096.0 * pl.k;
			}
		}
	}
	pl.inst = inst;
	pl.steer = (double)((int)d->wheelRotation * 4) * ENG_HC_ANGLE;
	pl.hazard = (int)d->hazardTimer;
	for (w = 0; w < 4; w++)
	{
		pl.roll[w] = run->roll[w];
	}
	EngHc_StatePose(c, &run->st, run->snapshot, s_hc.poseA, &info);
	if (!EngHc_SkinSlot(seat, ch, s_hc.poseA, &pl))
	{
		s_hc.declined++;
		return 0;
	}
	drawn = EngHc_DrawAtInstance(seat, ch, inst, idpp, pb, &pl.world);
	if (drawn <= 0)
	{
		s_hc.declined++;
		return 0;
	}
	s_hc.draws++;
	if (Engine_InstanceViewOf(pb) == 0)
	{
		EngHc_LogTick(gGT, seat, d, c, &info, &pl.world);
	}
	Engine_FxNoteDrawn(ENGINE_FX_BODY);
	return 1;
}

// ---------------------------------------------------------------------------
// THE FORCED SUBMIT (podium, results, driver select, menu scene: manager MC).

internal int EngHc_Submit(int ch, const EngineHiresSubmit *s)
{
	const EngHcChar *c;
	EngHcPlace pl;
	int resolved = ENGINE_HIRES_CLIP_REST;
	int w;
	int drawn = 0;

	if (!g_cfg_engineHiresChars || !g_cfg_engine || !s_hc.levelReady || (s == NULL) || (ch < 0) || (ch >= ENGINE_HIRES_CHARS_MAX) || !s_hc.chars[ch].used ||
	    (s->slot < ENGINE_HIRES_SLOT_SEATS) || (s->slot >= ENG_HC_SLOTS_ALL))
	{
		return 0;
	}
	c = &s_hc.chars[ch];
	memset(&pl, 0, sizeof(pl));
	if (s->inst != NULL)
	{
		int r;
		int k;

		if ((s->idpp == NULL) || (s->pb == NULL) || ((s->inst->flags & (SCREENSPACE_INSTANCE | HIDE_MODEL)) != 0) || !Engine_InstanceWorldMatrix(s->inst, &pl.world))
		{
			return 0;
		}
		pl.k = 0xccc / 4096.0;
		for (r = 0; r < 3; r++)
		{
			for (k = 0; k < 3; k++)
			{
				pl.rot[r][k] = (double)s->inst->matrix.m[r][k] / 4096.0 * pl.k;
			}
		}
		pl.inst = s->inst;
	}
	else
	{
		int r;
		int k;

		pl.world = s->worldFromModel;
		for (r = 0; r < 3; r++)
		{
			for (k = 0; k < 3; k++)
			{
				pl.rot[r][k] = (double)s->worldFromModel.m[k * 4 + r];
			}
		}
	}
	pl.steer = (double)s->wheelSteer;
	for (w = 0; w < 4; w++)
	{
		pl.roll[w] = (double)s->wheelRoll;
	}
	EngHc_EvalClip(c, s->clip, s->time, 0.0f, s_hc.poseA, &resolved, NULL);
	if ((s->prevClip >= 0) && (s->fade < 1.0f))
	{
		EngHc_EvalClip(c, s->prevClip, s->prevTime, 0.0f, s_hc.poseB, NULL, NULL);
		RldAnim_BlendPose(s_hc.poseB, s_hc.poseA, (s->fade < 0.0f) ? 0.0f : s->fade, s_hc.poseA, c->skel.nodeCount);
	}
	if (!EngHc_SkinSlot(s->slot, ch, s_hc.poseA, &pl))
	{
		return 0;
	}
	if (s->inst != NULL)
	{
		drawn = EngHc_DrawAtInstance(s->slot, ch, s->inst, s->idpp, s->pb, &pl.world);
	}
	else
	{
		const int mesh = EngHc_SlotMesh(s->slot, ch);
		const EngineCamera *cam = Engine_FrameCamera(s->view);

		if ((mesh >= 0) && (cam != NULL))
		{
			const EngineVec3 origin = {pl.world.m[12], pl.world.m[13], pl.world.m[14]};

			drawn = EngHc_Draw(s->view, mesh, s->tint, Engine_Mat4MulPoint(&cam->viewFromWorld, origin).z, s->ot, s->cell, NULL);
		}
	}
	s_hc.submits += (drawn > 0) ? 1u : 0u;
	return drawn;
}

int Engine_HiresCharSubmit(int ch, const EngineHiresSubmit *s)
{
	// The callers' slots only (the select slot is the engine's).
	if ((s == NULL) || (s->slot < ENGINE_HIRES_SLOT_SEATS) || (s->slot >= ENGINE_HIRES_SLOTS))
	{
		return 0;
	}
	return EngHc_Submit(ch, s);
}

// THE DRIVER SELECT (a main menu level): seat 0's instance carries the model of
// the roster entry on the preview tile (MM_NativeCharGrid_PreviewPose,
// game/230/MM_NativeCharGrid.c: driverInst->model = NativeChar_EntryModel), so
// the model names the file; a hi-res character of it is drawn there with clip
// "menu" (game time: gGT->timer / 30 s), wheels at rest, at the instance's
// matrix. Read only. 1 = drawn (the retail handler is skipped).
internal int EngHc_SelectRoute(struct Instance *inst, struct InstDrawPerPlayer *idpp, struct PushBuffer *pb, const struct GameTracker *gGT)
{
	EngineHiresSubmit s;
	const int ch = Engine_HiresCharOfModel(inst->model);
	int drawn;

	if (ch < 0)
	{
		return 0;
	}
	memset(&s, 0, sizeof(s));
	s.slot = ENG_HC_SLOT_SELECT;
	s.clip = ENGINE_HIRES_CLIP_MENU;
	s.time = (float)gGT->timer / 30.0f;
	s.prevClip = -1;
	s.fade = 1.0f;
	s.inst = inst;
	s.idpp = idpp;
	s.pb = pb;
	drawn = EngHc_Submit(ch, &s);
	if (drawn > 0)
	{
		s_hc.selectInst = inst;
		s_hc.selectFrame = s_hc.frame;
		if (s_hc.selectLogged != ch + 1)
		{
			s_hc.selectLogged = ch + 1;
			Platform_Log("[CTR Engine] hires char select: vb %d, %s, clip menu drawn as %s\n", Platform_GetVBlankCount(), s_hc.chars[ch].file,
			             Engine_HiresClipName(Engine_HiresCharResolveClip(ch, ENGINE_HIRES_CLIP_MENU, NULL)));
		}
		Engine_FxNoteDrawn(ENGINE_FX_BODY);
		return 1;
	}
	return 0;
}

int Engine_HiresCharSelectDrawn(const struct Instance *inst)
{
	return g_cfg_engineHiresChars && (inst != NULL) && (inst == s_hc.selectInst) && (s_hc.selectFrame == s_hc.frame) && s_hc.levelReady;
}

// ---------------------------------------------------------------------------
// THE LOOKUPS.

internal int EngHc_CharOfEntry(int entry)
{
	int k;

	for (k = 0; k < ENGINE_HIRES_CHARS_MAX; k++)
	{
		if (s_hc.chars[k].used && (s_hc.chars[k].entry == entry))
		{
			return k;
		}
	}
	return -1;
}

int Engine_HiresCharOfSeat(int seat)
{
	return ((seat >= 0) && (seat < ENGINE_HIRES_SLOT_SEATS) && s_hc.levelReady) ? s_hc.seatChar[seat] : -1;
}

int Engine_HiresCharOfFile(const char *file)
{
	int k;

	if ((file == NULL) || (file[0] == '\0'))
	{
		return -1;
	}
	for (k = 0; k < ENGINE_HIRES_CHARS_MAX; k++)
	{
		if (s_hc.chars[k].used && (strcmp(s_hc.chars[k].file, file) == 0))
		{
			return k;
		}
	}
	return -1;
}

int Engine_HiresCharOfEntry(int rosterEntry)
{
	return (rosterEntry >= 0) ? EngHc_CharOfEntry(rosterEntry) : -1;
}

int Engine_HiresCharOfModel(const struct Model *model)
{
	int k;

	if (model == NULL)
	{
		return -1;
	}
	for (k = 0; k < ENGINE_HIRES_CHARS_MAX; k++)
	{
		if (s_hc.chars[k].used && (s_hc.chars[k].model == model))
		{
			return k;
		}
	}
	return -1;
}

int Engine_HiresCharOfLastRaceSeat(int seat)
{
	if ((seat < 0) || (seat >= ENGINE_HIRES_SLOT_SEATS) || !s_hc.haveLastRace || (s_hc.lastRaceEntry[seat] < 0))
	{
		return -1;
	}
	return EngHc_CharOfEntry(s_hc.lastRaceEntry[seat]);
}

int Engine_HiresCharOfPlace(int place)
{
	if ((place < 1) || (place > 3))
	{
		return -1;
	}
	return Engine_HiresCharOfLastRaceSeat(data.cupPositionPerPlayer[place - 1]);
}

const char *Engine_HiresCharFile(int ch)
{
	return ((ch >= 0) && (ch < ENGINE_HIRES_CHARS_MAX) && s_hc.chars[ch].used) ? s_hc.chars[ch].file : "";
}

int Engine_HiresCharResolveClip(int ch, int clip, int *full)
{
	if (full != NULL)
	{
		*full = 0;
	}
	if ((ch < 0) || (ch >= ENGINE_HIRES_CHARS_MAX) || !s_hc.chars[ch].used)
	{
		return ENGINE_HIRES_CLIP_REST;
	}
	return EngHc_Resolve(s_hc.chars[ch].clipMask, clip, full);
}

int Engine_HiresCharHull(int ch, float hullMin[3], float hullMax[3])
{
	if ((ch < 0) || (ch >= ENGINE_HIRES_CHARS_MAX) || !s_hc.chars[ch].used)
	{
		return 0;
	}
	memcpy(hullMin, s_hc.chars[ch].hullMin, 3u * sizeof(float));
	memcpy(hullMax, s_hc.chars[ch].hullMax, 3u * sizeof(float));
	return 1;
}

void Engine_HiresSeatForceClip(int seat, int clip)
{
	if ((seat >= 0) && (seat < ENGINE_HIRES_SLOT_SEATS))
	{
		s_hc.seats[seat].force = ((clip >= 0) && (clip < ENGINE_HIRES_CLIPS)) ? clip : -1;
	}
}

// ---------------------------------------------------------------------------
// THE LOAD (stage 5, from Engine_DriverCmdlLoad).

internal void EngHc_Report(void)
{
	Platform_Log("[CTR Engine] hires chars at exit: loaded %u, refused %u; body draws %u (mirrored %u), declined %u; forced submits %u; skins %u, vertices "
	             "skinned %llu, skin time %.1f ms in all (%.3f ms per skin); seat ticks %u, tick lines %u; host peak of the loader %u KB\n",
	             s_hc.loaded, s_hc.refused, s_hc.draws, s_hc.mirrorDraws, s_hc.declined, s_hc.submits, s_hc.skins, (unsigned long long)s_hc.skinnedVertices,
	             s_hc.skinMs, (s_hc.skins > 0u) ? (s_hc.skinMs / (double)s_hc.skins) : 0.0, s_hc.ticks, s_hc.lines, s_hc.hostPeak / 1024u);
}

// The first error of a report (rule, text), or the first message.
internal void EngHc_FirstError(const RldHiresReport *rep, const char **rule, char *detail, int detailSize)
{
	int i;

	for (i = 0; i < rep->count; i++)
	{
		if (rep->msg[i].level == RLDHIRES_ERROR)
		{
			// Copied: the report is the caller's local, the rule is printed after it is gone.
			snprintf(s_hc.ruleCopy, sizeof(s_hc.ruleCopy), "%s", rep->msg[i].rule);
			*rule = s_hc.ruleCopy;
			snprintf(detail, (size_t)detailSize, "%s", rep->msg[i].text);
			return;
		}
	}
	*rule = "HRG-7";
	snprintf(detail, (size_t)detailSize, "%s", "refused");
}

// A free character slot for entry: an empty one, else the one no level has
// needed for longest and that this level does not need. -1 = none.
internal int EngHc_FreeCharSlot(const int *needed)
{
	int k;
	int best = -1;

	for (k = 0; k < ENGINE_HIRES_CHARS_MAX; k++)
	{
		if (!s_hc.chars[k].used)
		{
			return k;
		}
	}
	for (k = 0; k < ENGINE_HIRES_CHARS_MAX; k++)
	{
		if (!needed[k] && ((best < 0) || (s_hc.chars[k].lastLevel < s_hc.chars[best].lastLevel)))
		{
			best = k;
		}
	}
	if (best >= 0)
	{
		EngHc_FreeChar(&s_hc.chars[best]);
	}
	return best;
}

// The checks of a CHRG chunk's bytes (pure; the selftest feeds it made-up
// chunks): the header, RldHires_Open, RldHires_CheckBudget. 1 = model opened
// and within the budget (close it with RldHires_Close in every case; the bytes
// must live until then); 0 with the first error's rule and text.
internal int EngHc_CheckChunk(const u8 *bytes, size_t size, RldHiresModel *model, RldHiresReport *rep, const char **rule, char *detail, int detailSize)
{
	RldHiresChunk chunk;

	memset(model, 0, sizeof(*model));
	RldHires_ReportClear(rep);
	if (!RldHires_ParseChunk(bytes, size, &chunk, rule, detail, detailSize))
	{
		return 0;
	}
	if (!RldHires_Open(chunk.glb, chunk.glbSize, model, rep) || !RldHires_CheckBudget(model, rep))
	{
		EngHc_FirstError(rep, rule, detail, detailSize);
		return 0;
	}
	return 1;
}

// Loads entry into chars[ch]. 1 = ready; 0 with rule and detail.
internal int EngHc_LoadChar(int entry, int ch, const char **rule, char *detail, int detailSize)
{
	EngHcChar *c = &s_hc.chars[ch];
	RldHiresModel model;
	RldHiresReport rep;
	const char *why = "";
	size_t size = 0;
	u8 *bytes;
	u32 peak = 0;
	int ok = 0;
	int i;

	memset(c, 0, sizeof(*c));
	c->indexBuffer = 0xffffffffu;
	c->entry = entry;
	snprintf(c->file, sizeof(c->file), "%s", NativeChar_EntryFile(entry));
	c->model = NativeChar_EntryModel(entry);
	bytes = NativeChar_ReadChrg(entry, &size, &why);
	if (bytes == NULL)
	{
		EngHc_Fail(rule, detail, detailSize, "HRG-5", why);
		memset(c, 0, sizeof(*c));
		return 0;
	}
	peak = (u32)size;
	if (!EngHc_CheckChunk(bytes, size, &model, &rep, rule, detail, detailSize))
	{
		ok = 0; // refused: rule and detail are set
	}
	else if (EngHc_BuildCpu(&model, c, rule, detail, detailSize))
	{
		peak += c->hostBytes + c->indexCount * 4u;
		ok = EngHc_Upload(&model, c, rule, detail, detailSize, &peak);
		peak += c->hostBytes;
	}
	// The warnings and notes of the file, once per load of the character.
	for (i = 0; ok && (i < rep.count); i++)
	{
		Platform_Log("[CTR Engine] hires char %s (%s): %s: %s\n", (rep.msg[i].level == RLDHIRES_WARNING) ? "warning" : "note", rep.msg[i].rule, c->file,
		             rep.msg[i].text);
	}
	RldHires_Close(&model);
	free(bytes);
	s_hc.hostPeak = (peak > s_hc.hostPeak) ? peak : s_hc.hostPeak;
	if (!ok)
	{
		EngHc_FreeChar(c);
		return 0;
	}
	c->used = 1;
	return 1;
}

// The scratch of the pose and the palette for the largest loaded character.
internal int EngHc_Scratch(void)
{
	u32 nodes = 1;
	u32 palette = 1;
	int k;

	for (k = 0; k < ENGINE_HIRES_CHARS_MAX; k++)
	{
		if (s_hc.chars[k].used)
		{
			nodes = (s_hc.chars[k].skel.nodeCount > nodes) ? s_hc.chars[k].skel.nodeCount : nodes;
			palette = (s_hc.chars[k].paletteCount > palette) ? s_hc.chars[k].paletteCount : palette;
		}
	}
	if ((nodes > s_hc.scratchNodes) || (s_hc.poseA == NULL))
	{
		free(s_hc.poseA);
		free(s_hc.poseB);
		free(s_hc.worlds);
		s_hc.poseA = (RldAnimTrs *)malloc((size_t)nodes * sizeof(RldAnimTrs));
		s_hc.poseB = (RldAnimTrs *)malloc((size_t)nodes * sizeof(RldAnimTrs));
		s_hc.worlds = (float *)malloc((size_t)nodes * 16u * sizeof(float));
		s_hc.scratchNodes = nodes;
	}
	if ((palette > s_hc.scratchPalette) || (s_hc.palette == NULL))
	{
		free(s_hc.palette);
		s_hc.palette = (float *)malloc((size_t)palette * 12u * sizeof(float));
		s_hc.scratchPalette = palette;
	}
	return (s_hc.poseA != NULL) && (s_hc.poseB != NULL) && (s_hc.worlds != NULL) && (s_hc.palette != NULL);
}

// A slot's stream and CPU arrays for capacity vertices (0 = none: freed).
internal int EngHc_SlotSize(int slot, u32 capacity)
{
	EngHcSlot *s = &s_hc.slots[slot];
	int b;

	if ((capacity > 0u) && (s->capacity >= capacity) && (s->stream != 0xffffffffu) && (s->stream != 0u))
	{
		return 1;
	}
	if ((s->stream != 0xffffffffu) && (s->stream != 0u))
	{
		Engine_RenderStreamRelease(s->stream);
		EngHc_GravePark(s->stream, 0xffffffffu);
	}
	for (b = 0; b < ENGINE_SCENE_BANKS; b++)
	{
		free(s->cpu[b]);
		s->cpu[b] = NULL;
		s->cpuChar[b] = -1;
	}
	s->stream = 0xffffffffu;
	s->capacity = 0;
	s->skinnedFrame = 0;
	if (capacity == 0u)
	{
		return 1;
	}
	s->stream = Engine_RenderStreamCreate(capacity);
	for (b = 0; b < ENGINE_SCENE_BANKS; b++)
	{
		s->cpu[b] = (EngineVertex *)malloc((size_t)capacity * sizeof(EngineVertex));
	}
	if ((s->stream == 0xffffffffu) || (s->cpu[0] == NULL) || (s->cpu[1] == NULL))
	{
		(void)EngHc_SlotSize(slot, 0u);
		return 0;
	}
	s->capacity = capacity;
	return 1;
}

void Engine_HiresCharsLoad(void)
{
	const struct GameTracker *gGT = sdata->gGT;
	int needed[ENGINE_HIRES_CHARS_MAX];
	int seatEntry[ENGINE_HIRES_SLOT_SEATS];
	int seats = 0;
	int menu;
	int k;
	int seat;
	u32 maxVertices = 0;
	u64 started;

	s_hc.levelStamp++;
	s_hc.levelReady = 0;
	for (seat = 0; seat < ENGINE_HIRES_SLOT_SEATS; seat++)
	{
		s_hc.seatChar[seat] = -1;
		s_hc.seats[seat].valid = 0;
		s_hc.seats[seat].force = -1;
	}
	if (!g_cfg_engineHiresChars || !g_cfg_engine || (gGT == NULL))
	{
		return;
	}
	started = SDL_GetPerformanceCounter();
	EngHc_GraveEmpty();
	if (!s_hc.reportOn)
	{
		s_hc.reportOn = 1;
		Platform_AtExitReport(EngHc_Report);
	}
	(void)Engine_ProgramsInit();
	if (Engine_ProgramOf(ENGINE_PROGRAM_NS) == 0xffffffffu)
	{
		Platform_Log("[CTR Engine] hires chars off for this level: the program 'ns' is not made - every seat draws its classic model\n");
		return;
	}
	{
		const double len = sqrt((double)g_cfg_engineStaticSun[0] * g_cfg_engineStaticSun[0] + (double)g_cfg_engineStaticSun[1] * g_cfg_engineStaticSun[1] +
		                        (double)g_cfg_engineStaticSun[2] * g_cfg_engineStaticSun[2]);

		for (k = 0; k < 3; k++)
		{
			s_hc.sun[k] = (len > 0.0) ? (float)(g_cfg_engineStaticSun[k] / len) : ((k == 1) ? 1.0f : 0.0f);
		}
		// The "hires" style of the static model: colour as it is, smooth, the ambient from below.
		s_hc.style[0] = 1.0f;
		s_hc.style[1] = 1.0f;
		s_hc.style[2] = 0.0f;
		s_hc.style[3] = g_cfg_engineStaticSun[5];
	}
	menu = ((gGT->gameMode1 & MAIN_MENU) != 0);
	memset(needed, 0, sizeof(needed));

	// The seats of this level (a race): their files, and the last race's map.
	for (seat = 0; seat < ENGINE_HIRES_SLOT_SEATS; seat++)
	{
		seatEntry[seat] = NativeChar_SeatEntry(seat);
		seats += (seatEntry[seat] >= 0) ? 1 : 0;
	}
	if (seats > 0)
	{
		for (seat = 0; seat < ENGINE_HIRES_SLOT_SEATS; seat++)
		{
			s_hc.lastRaceEntry[seat] = (seatEntry[seat] >= 0) && NativeChar_EntryHasChrg(seatEntry[seat]) ? seatEntry[seat] : -1;
		}
		s_hc.haveLastRace = 1;
	}
	for (seat = 0; seat < ENGINE_HIRES_SLOT_SEATS; seat++)
	{
		const int entry = seatEntry[seat];
		const int set = Engine_DriverSetOfSeat(seat);
		const char *rule = "";
		char detail[RLDHIRES_TEXT_MAX];
		int ch;

		if ((entry < 0) || !NativeChar_EntryHasChrg(entry))
		{
			continue;
		}
		if ((set >= 0) && (Engine_DriverBodyMesh(set) >= 0))
		{
			Platform_Log("[CTR Engine] hires char off (native set): seat %d, %s: the engine draws its native set\n", seat, NativeChar_EntryFile(entry));
			continue;
		}
		ch = EngHc_CharOfEntry(entry);
		if (ch < 0)
		{
			ch = EngHc_FreeCharSlot(needed);
			if ((ch < 0) || !EngHc_LoadChar(entry, ch, &rule, detail, (int)sizeof(detail)))
			{
				s_hc.refused++;
				Platform_Log("[CTR Engine] hires char refused (%s): seat %d, %s: %s, using the classic model\n", (ch < 0) ? "table" : rule, seat,
				             NativeChar_EntryFile(entry), (ch < 0) ? "no free character slot" : detail);
				continue;
			}
			s_hc.loaded++;
		}
		needed[ch] = 1;
		s_hc.chars[ch].lastLevel = s_hc.levelStamp;
		s_hc.seatChar[seat] = ch;
		{
			const EngHcChar *c = &s_hc.chars[ch];

			Platform_Log("[CTR Engine] hires char ready: seat %d, %s, %u triangles, %u vertices, %u joints, clips %s, wheels %s, textures %d%s, %u bytes "
			             "(host %u, textures %llu)\n",
			             seat, c->file, c->triangles, c->vertexCount, c->joints, c->clipText, c->ownWheels ? "own" : "standard", c->texCount, c->texText,
			             c->hostBytes + (u32)c->texBytes + c->indexCount * 4u, c->hostBytes, (unsigned long long)c->texBytes);
		}
	}

	// A menu level: the roster's files with CHRG (driver select, menu scene).
	if (menu)
	{
		const int files = NativeChar_RosterFileCount();
		int entry;

		for (entry = 0; entry < files; entry++)
		{
			const char *rule = "";
			char detail[RLDHIRES_TEXT_MAX];
			int ch;

			if (!NativeChar_EntryHasChrg(entry))
			{
				continue;
			}
			ch = EngHc_CharOfEntry(entry);
			if (ch < 0)
			{
				ch = EngHc_FreeCharSlot(needed);
				if (ch < 0)
				{
					break; // the table is full of this level's characters
				}
				if (!EngHc_LoadChar(entry, ch, &rule, detail, (int)sizeof(detail)))
				{
					s_hc.refused++;
					Platform_Log("[CTR Engine] hires char refused (%s): entry %d, %s: %s, using the classic model\n", rule, entry, NativeChar_EntryFile(entry), detail);
					continue;
				}
				s_hc.loaded++;
				Platform_Log("[CTR Engine] hires char ready: entry %d, %s, %u triangles, %u vertices, %u joints, clips %s, wheels %s, textures %d%s\n", entry,
				             s_hc.chars[ch].file, s_hc.chars[ch].triangles, s_hc.chars[ch].vertexCount, s_hc.chars[ch].joints, s_hc.chars[ch].clipText,
				             s_hc.chars[ch].ownWheels ? "own" : "standard", s_hc.chars[ch].texCount, s_hc.chars[ch].texText);
			}
			needed[ch] = 1;
			s_hc.chars[ch].lastLevel = s_hc.levelStamp;
		}
	}

	// This level's materials of every loaded character now, at the load stage:
	// Engine_MaterialsWarmPipelines (load stage 8) builds their pipelines, so
	// no pipeline is made inside a race frame.
	for (k = 0; k < ENGINE_HIRES_CHARS_MAX; k++)
	{
		if (s_hc.chars[k].used && !EngHc_RegisterMaterials(&s_hc.chars[k]))
		{
			Platform_Log("[CTR Engine] hires chars: the material table is full at %s - its draws are refused in this level\n", s_hc.chars[k].file);
		}
	}

	// The scratch, the seats' snapshots, the slots.
	for (k = 0; k < ENGINE_HIRES_CHARS_MAX; k++)
	{
		if (s_hc.chars[k].used)
		{
			maxVertices = (s_hc.chars[k].vertexCount > maxVertices) ? s_hc.chars[k].vertexCount : maxVertices;
		}
	}
	if ((maxVertices > 0u) && !EngHc_Scratch())
	{
		Platform_Log("[CTR Engine] hires chars off for this level: no memory for the pose scratch\n");
		return;
	}
	for (seat = 0; seat < ENGINE_HIRES_SLOT_SEATS; seat++)
	{
		EngHcSeatRun *run = &s_hc.seats[seat];
		const int ch = s_hc.seatChar[seat];
		const u32 nodes = (ch >= 0) ? s_hc.chars[ch].skel.nodeCount : 0u;

		if (run->snapshotNodes < nodes)
		{
			free(run->snapshot);
			run->snapshot = (RldAnimTrs *)malloc((size_t)nodes * sizeof(RldAnimTrs));
			run->snapshotNodes = (run->snapshot != NULL) ? nodes : 0u;
		}
		if ((ch >= 0) && !EngHc_SlotSize(seat, s_hc.chars[ch].vertexCount))
		{
			Platform_Log("[CTR Engine] hires char refused (GPU): seat %d, %s: the streamed buffer was refused, using the classic model\n", seat, s_hc.chars[ch].file);
			s_hc.seatChar[seat] = -1;
		}
		if (ch < 0)
		{
			(void)EngHc_SlotSize(seat, 0u);
		}
	}
	// The forced slots (podium, menu scene: MC) only in a level without hi-res
	// seats (a menu, the podium of a cup), the select slot only in a main menu
	// level: a race level keeps none of them (32-bit address space).
	{
		int hiresSeats = 0;

		for (seat = 0; seat < ENGINE_HIRES_SLOT_SEATS; seat++)
		{
			hiresSeats += (s_hc.seatChar[seat] >= 0) ? 1 : 0;
		}
		for (k = ENGINE_HIRES_SLOT_SEATS; k < ENG_HC_SLOTS_ALL; k++)
		{
			const int wanted = (k == ENG_HC_SLOT_SELECT) ? menu : (hiresSeats == 0);

			(void)EngHc_SlotSize(k, wanted ? maxVertices : 0u);
		}
	}
	for (k = 0; k < ENG_HC_SLOTS_ALL; k++)
	{
		int c;

		for (c = 0; c < ENGINE_HIRES_CHARS_MAX; c++)
		{
			s_hc.slots[k].mesh[c] = -1;
		}
		s_hc.slots[k].cpuChar[0] = -1;
		s_hc.slots[k].cpuChar[1] = -1;
		s_hc.slots[k].skinnedFrame = 0;
	}
	s_hc.levelReady = (maxVertices > 0u);
	{
		int hires = 0;
		int loaded = 0;

		for (seat = 0; seat < ENGINE_HIRES_SLOT_SEATS; seat++)
		{
			hires += (s_hc.seatChar[seat] >= 0) ? 1 : 0;
		}
		for (k = 0; k < ENGINE_HIRES_CHARS_MAX; k++)
		{
			loaded += s_hc.chars[k].used ? 1 : 0;
		}
		Platform_Log("[CTR Engine] hires chars: level %d, %d seat(s) hi-res, %d character(s) loaded, streams %u bytes, host peak of the loader %u KB, %.1f ms\n",
		             (int)gGT->levelID, hires, loaded, Engine_RenderStreamBytes(), s_hc.hostPeak / 1024u, EngStatic_Ms(started));
	}
}

// THE LOADER'S SELFTEST: the pure path of a file (chunk checks, the CPU side,
// the palette and the wheel read-back, the state pose) on GLBs made in code
// (engine_hires_testglb.c). No device: the GPU side is the static model's
// proven calls (Engine_TextureCreate, Engine_RenderStaticIndexBuffer).
internal int EngHc_SelfTestLoader(int *checks)
{
	int failures = 0;
	int n = 0;
	EngHiresTestGlbDesc d;
	RldHiresModel model;
	RldHiresReport rep;
	const char *rule = "";
	char detail[RLDHIRES_TEXT_MAX];
	size_t glbSize = 0;
	size_t chunkSize = 0;
	u8 *glb;
	u8 *chunk;
	EngHcChar c;
	int ok;

#define HL_CHECK(cond, what)                                            \
	do                                                                  \
	{                                                                   \
		n++;                                                            \
		if (!(cond))                                                    \
		{                                                               \
			failures++;                                                 \
			printf("engine selftest: hires loader " what " FAILED\n"); \
		}                                                               \
	} while (0)

	// (A) skinned, four wheel nodes, every clip, PNG base colour + normal map.
	memset(&d, 0, sizeof(d));
	d.skinned = 1;
	d.wheels = 4;
	d.clipMask = 0xfffu;
	d.texture = 1;
	d.normalMap = 1;
	glb = EngHiresTest_Glb(&d, &glbSize);
	chunk = (glb != NULL) ? RldHires_BuildChunk(glb, (u32)glbSize, &chunkSize) : NULL;
	ok = (chunk != NULL) && EngHc_CheckChunk(chunk, chunkSize, &model, &rep, &rule, detail, (int)sizeof(detail));
	if (!ok)
	{
		printf("engine selftest: hires loader A refused: %s %s\n", rule, detail);
	}
	HL_CHECK(ok, "test driver A opens and is within the budget");
	memset(&c, 0, sizeof(c));
	c.indexBuffer = 0xffffffffu;
	ok = ok && EngHc_BuildCpu(&model, &c, &rule, detail, (int)sizeof(detail));
	HL_CHECK(ok, "test driver A: the CPU side is made");
	if (ok)
	{
		// 4 body vertices + 4 wheels of 12; 6 + 48 indices; palette 2 joints, no rigid node, 4 wheels.
		HL_CHECK((c.vertexCount == 52u) && (c.indexCount == 54u) && (c.triangles == 18u), "A: vertex, index and triangle counts");
		HL_CHECK((c.joints == 2u) && (c.rigidCount == 0u) && (c.wheelFirst == 2u) && (c.paletteCount == 6u) && c.ownWheels, "A: palette layout, own wheels");
		HL_CHECK((c.clipMask == 0xfffu) && (strcmp(c.clipText, "12/12") == 0), "A: 12 of 12 clips");
		HL_CHECK((c.rangeCount == 1u) && (c.ranges[0].firstIndex == 0u) && (c.ranges[0].indexCount == 54u) && (c.ranges[0].material == 0u),
		         "A: one range of material 0");
		HL_CHECK((c.texCount == 2) && (c.tex[0].linear == 0) && (c.tex[1].linear == 1) && (c.mats[0].baseTex == 0) && (c.mats[0].normalTex == 1) &&
		             (c.tex[0].wrapU == NATIVE_GFX_WRAP_CLAMP),
		         "A: base colour sRGB and normal map linear, clamped");
		HL_CHECK((fabsf(c.mats[0].factor[1] - 0.5f) < 1e-6f) && c.mats[0].doubleSided && !c.mats[0].mask && !c.mats[0].unlit, "A: material factor and flags");
		HL_CHECK((c.src[3].j[0] == 0) && (c.src[3].j[1] == 1) && (fabsf(c.src[3].w[0] - 0.5f) < 1e-6f) && (fabsf(c.src[3].w[1] - 0.5f) < 1e-6f) &&
		             c.src[3].hasNormal && (c.base[3].texcoord[0] == 1.0f) && (c.base[3].color[0] == 255),
		         "A: vertex 3 weights, normal, texcoord, white colour");
		HL_CHECK((c.src[4].j[0] == 2) && (c.src[4 + 12].j[0] == 3) && (c.src[51].j[0] == 5) && (c.src[51].w[0] == 1.0f), "A: wheel vertices on their palette entries");
		HL_CHECK((fabsf(c.wheelRadius[0] - 16.0f) < 1e-3f) && (c.wheelSign[0] == 1.0f) && (fabsf(c.wheelRestYaw[2]) < 1e-6f),
		         "A: wheel radius 0.25 m = 16 units, sign, rest yaw");
		// wheels: middles x +-36, y 16, z 49.75 / -24, each +-16 on every axis; the body up to y 32 at rest, and
		// the clips lift the root by up to 0.12 m (7.68) with the bone turned up to 120 degrees: about 45
		// (plus the culling margin: 5 % of each axis's extent per side; x 104 -> +-57.2, z -40..65.7472 -> -45.2874..71.0346)
		HL_CHECK((fabsf(c.hullMax[0] - 57.2f) < 0.01f) && (fabsf(c.hullMin[0] + 57.2f) < 0.01f) && (c.hullMin[1] < -1.9f) && (c.hullMin[1] > -2.4f) &&
		             (c.hullMax[1] > 41.5f) && (c.hullMax[1] < 48.5f) && (fabsf(c.hullMax[2] - 71.0346f) < 0.01f) && (fabsf(c.hullMin[2] + 45.2874f) < 0.01f),
		         "A: hull over the wheels and the body (model units)");

		// The palette and the skinning at a place (identity world, at rest).
		s_hc.chars[0] = c;
		s_hc.chars[0].used = 1;
		if (EngHc_Scratch())
		{
			EngHcPlace pl;
			float yaw = 9.0f;
			float roll = 9.0f;
			float p[3];
			float nn[3];

			memset(&pl, 0, sizeof(pl));
			Engine_Mat4Identity(&pl.world);
			pl.rot[0][0] = pl.rot[1][1] = pl.rot[2][2] = 1.0;
			RldAnim_PoseRest(&c.skel, s_hc.poseA);
			pl.roll[0] = ENG_HC_TWO_PI / 4.0;
			EngHc_Palette(&c, s_hc.poseA, &pl, &yaw, &roll);
			RldAnim_SkinVertex(s_hc.palette, c.src[2].j, c.src[2].w, c.src[2].p, c.src[2].n, p, nn);
			HL_CHECK((fabsf(p[0]) < 1e-4f) && (fabsf(p[1] - 32.0f) < 1e-4f) && (fabsf(p[2]) < 1e-4f) && (fabsf(nn[2] - 1.0f) < 1e-5f),
			         "A: bone vertex at rest = 64 x (0, 0.5, 0)");
			// wheel fl: middle 64 x (0.5625, 0.25, 0.7773); its top (0, 0.25, 0) rolled by pi/2 goes to the front
			{
				const float *e = &s_hc.palette[2u * 12u];
				const float top[3] = {0.0f, 0.25f, 0.0f};
				float q[3];
				int a;

				for (a = 0; a < 3; a++)
				{
					q[a] = e[a * 4 + 0] * top[0] + e[a * 4 + 1] * top[1] + e[a * 4 + 2] * top[2] + e[a * 4 + 3];
				}
				HL_CHECK((fabsf(e[3] - 36.0f) < 1e-3f) && (fabsf(e[7] - 16.0f) < 1e-3f) && (fabsf(e[11] - 49.7472f) < 1e-3f), "A: wheel fl middle in model units");
				HL_CHECK((fabsf(q[1] - 16.0f) < 0.1f) && (fabsf(q[2] - (49.7472f + 16.0f)) < 0.1f), "A: a roll of pi/2 turns the top to the front");
			}
			HL_CHECK((fabsf(roll - (float)(ENG_HC_TWO_PI / 4.0)) < 0.01f) && (fabsf(yaw) < 0.01f), "A: read-back roll pi/2, yaw 0");
			pl.steer = 0.25;
			pl.roll[0] = 0.0;
			EngHc_Palette(&c, s_hc.poseA, &pl, &yaw, &roll);
			HL_CHECK((fabsf(yaw - 0.25f) < 0.005f) && (fabsf(roll) < 0.01f), "A: read-back yaw = steer of a front wheel");
			// Own wheels keep the model's proportions: a squashed body (x 0.5, y 0.75, z 0.5) squashes them too.
			pl.steer = 0.0;
			pl.world.m[0] = 0.5f;
			pl.world.m[5] = 0.75f;
			pl.world.m[10] = 0.5f;
			EngHc_Palette(&c, s_hc.poseA, &pl, &yaw, &roll);
			{
				const float *e = &s_hc.palette[2u * 12u];
				const double axle = sqrt((double)e[0] * e[0] + (double)e[4] * e[4] + (double)e[8] * e[8]);
				const double up = sqrt((double)e[1] * e[1] + (double)e[5] * e[5] + (double)e[9] * e[9]);

				HL_CHECK((fabs(axle - 32.0) < 0.05) && (fabs(up - 48.0) < 0.05) && (fabsf(e[7] - 12.0f) < 1e-3f) && (fabsf(e[3] - 18.0f) < 1e-3f),
				         "A: own wheels scale with the body's matrix (axle 64 x 0.5, up 64 x 0.75, middle y 16 x 0.75)");
				HL_CHECK(fabsf(yaw) < 0.01f, "A: read-back yaw through the squashed matrix is still 0");
			}
			Engine_Mat4Identity(&pl.world);
			// The clip pose: idle at t = 1 rotates the bone 10 degrees about z.
			{
				EngHcSeatState st;
				EngHcPoseInfo info;

				EngHc_SeatReset(&st, -1);
				st.baseTime = 1.5f; // the loop wraps: 0.5 s into idle
				st.steerRaw = 0;
				EngHc_StatePose(&c, &st, NULL, s_hc.poseA, &info);
				HL_CHECK((info.clip == ENGINE_HIRES_CLIP_IDLE) && (info.want == ENGINE_HIRES_CLIP_IDLE) && (fabsf(info.t - 0.5f) < 1e-5f) && (info.blend == 1.0f) &&
				             (info.steerClip == -2),
				         "A: base at 1.5 s draws idle at 0.5 s (looped), no steer");
				HL_CHECK(fabsf(s_hc.poseA[2].r[2] - sinf(2.5f * 3.14159265f / 180.0f)) < 1e-5f, "A: idle halfway rotates the bone 5 degrees");
				st.steerRaw = 64;
				EngHc_StatePose(&c, &st, NULL, s_hc.poseA, &info);
				// full left lock: the steer_left pose (20 degrees at its end) alone
				HL_CHECK((info.steerClip == ENGINE_HIRES_CLIP_STEER_LEFT) && (fabsf(info.steer - 1.0f) < 1e-6f) &&
				             (fabsf(s_hc.poseA[2].r[2] - sinf(10.0f * 3.14159265f / 180.0f)) < 1e-4f),
				         "A: full left lock draws steer_left at its end");
				st.state = ENG_HC_STATE_JUMP;
				st.stateTime = 9.0f;
				EngHc_StatePose(&c, &st, NULL, s_hc.poseA, &info);
				HL_CHECK((info.clip == ENGINE_HIRES_CLIP_JUMP) && (fabsf(info.t - 0.5f) < 1e-6f) &&
				             (fabsf(s_hc.poseA[2].r[2] - sinf(20.0f * 3.14159265f / 180.0f)) < 1e-4f),
				         "A: jump once holds its last pose");
				// A fade halfway from a snapshot at rest towards the held jump pose.
				{
					RldAnimTrs *snap = (RldAnimTrs *)malloc((size_t)c.skel.nodeCount * sizeof(RldAnimTrs));

					if (snap != NULL)
					{
						RldAnim_PoseRest(&c.skel, snap);
						st.fading = 1;
						st.fadeTime = 0.075f;
						EngHc_StatePose(&c, &st, snap, s_hc.poseA, &info);
						HL_CHECK((fabsf(info.blend - 0.5f) < 1e-5f) && (fabsf(s_hc.poseA[2].r[2] - sinf(10.0f * 3.14159265f / 180.0f)) < 2e-3f),
						         "A: a half fade from rest gives about half the jump rotation");
						free(snap);
					}
				}
			}
		}
		else
		{
			HL_CHECK(0, "A: scratch");
		}
		memset(&s_hc.chars[0], 0, sizeof(s_hc.chars[0]));
		EngHc_FreeCpu(&c);
	}
	RldHires_Close(&model);
	free(chunk);

	// Version 2, an unknown flag bit, a broken GLB: refused with their rules.
	chunk = (glb != NULL) ? RldHires_BuildChunk(glb, (u32)glbSize, &chunkSize) : NULL;
	HL_CHECK(chunk != NULL, "a chunk is made");
	if (chunk != NULL)
	{
		chunk[0] = 2;
		HL_CHECK(!EngHc_CheckChunk(chunk, chunkSize, &model, &rep, &rule, detail, (int)sizeof(detail)) && (strcmp(rule, "HRG-1") == 0), "version 2 refused (HRG-1)");
		RldHires_Close(&model);
		chunk[0] = 1;
		chunk[4] = 0x10;
		HL_CHECK(!EngHc_CheckChunk(chunk, chunkSize, &model, &rep, &rule, detail, (int)sizeof(detail)) && (strcmp(rule, "HRG-3") == 0), "a flag bit refused (HRG-3)");
		RldHires_Close(&model);
		chunk[4] = 0;
		chunk[16] = 'x'; // the GLB magic
		HL_CHECK(!EngHc_CheckChunk(chunk, chunkSize, &model, &rep, &rule, detail, (int)sizeof(detail)) && (strcmp(rule, "HRG-6") == 0), "a broken GLB refused (HRG-6)");
		RldHires_Close(&model);
		free(chunk);
	}
	free(glb);

	// A JPEG base colour (HRG-16), a required extension (HRG-8), an outside buffer (HRG-9).
	{
		static const int texture[3] = {2, 1, 1};
		static const int ext[3] = {0, 1, 0};
		static const int uri[3] = {0, 0, 1};
		static const char *const rules[3] = {"HRG-16", "HRG-8", "HRG-9"};
		int t;

		for (t = 0; t < 3; t++)
		{
			memset(&d, 0, sizeof(d));
			d.skinned = 1;
			d.wheels = 4;
			d.clipMask = 1u;
			d.texture = texture[t];
			d.requiredExt = ext[t];
			d.externalUri = uri[t];
			glb = EngHiresTest_Glb(&d, &glbSize);
			chunk = (glb != NULL) ? RldHires_BuildChunk(glb, (u32)glbSize, &chunkSize) : NULL;
			ok = (chunk != NULL) && !EngHc_CheckChunk(chunk, chunkSize, &model, &rep, &rule, detail, (int)sizeof(detail)) && (strcmp(rule, rules[t]) == 0);
			if (!ok)
			{
				printf("engine selftest: hires loader refusal %d gave rule %s (%s), expected %s\n", t, rule, detail, rules[t]);
			}
			HL_CHECK(ok, "JPEG, required extension and outside buffer refused with their rules");
			RldHires_Close(&model);
			free(chunk);
			free(glb);
		}
	}

	// Over the budget (HRG-27): the loader's budget call on a model past each limit.
	{
		RldHiresModel big;

		memset(&big, 0, sizeof(big));
		big.triangles = (u64)RLDHIRES_BUDGET_TRIANGLES + 1u;
		RldHires_ReportClear(&rep);
		HL_CHECK(!RldHires_CheckBudget(&big, &rep) && RldHires_ReportHas(&rep, "HRG-27"), "triangles over the budget refused (HRG-27)");
		big.triangles = 10u;
		big.textureBytes = (u64)RLDHIRES_BUDGET_TEXTURE_BYTES + 1u;
		RldHires_ReportClear(&rep);
		HL_CHECK(!RldHires_CheckBudget(&big, &rep), "texture bytes over the budget refused");
		{
			const char *r = "";
			char text[RLDHIRES_TEXT_MAX];

			EngHc_FirstError(&rep, &r, text, (int)sizeof(text));
			HL_CHECK(strcmp(r, "HRG-27") == 0, "the refusal line names HRG-27");
			// The id is a copy: it outlives the report (the loader prints it after its report is gone).
			memset(&rep, 0x55, sizeof(rep));
			HL_CHECK(strcmp(r, "HRG-27") == 0, "the refusal's rule id outlives its report");
		}
	}

	// (B) no skin, three wheel nodes (standard wheels), idle and jump, CUBICSPLINE, no texture.
	memset(&d, 0, sizeof(d));
	d.skinned = 0;
	d.wheels = 3;
	d.clipMask = (1u << ENGINE_HIRES_CLIP_IDLE) | (1u << ENGINE_HIRES_CLIP_JUMP);
	d.interpolation = 2;
	glb = EngHiresTest_Glb(&d, &glbSize);
	chunk = (glb != NULL) ? RldHires_BuildChunk(glb, (u32)glbSize, &chunkSize) : NULL;
	ok = (chunk != NULL) && EngHc_CheckChunk(chunk, chunkSize, &model, &rep, &rule, detail, (int)sizeof(detail));
	if (!ok)
	{
		printf("engine selftest: hires loader B refused: %s %s\n", rule, detail);
	}
	HL_CHECK(ok && RldHires_ReportHas(&rep, "HRG-20") && RldHires_ReportHas(&rep, "HRG-19"), "B opens with the wheel and clip warnings");
	memset(&c, 0, sizeof(c));
	c.indexBuffer = 0xffffffffu;
	ok = ok && EngHc_BuildCpu(&model, &c, &rule, detail, (int)sizeof(detail));
	HL_CHECK(ok, "B: the CPU side is made");
	if (ok)
	{
		u32 i;
		int inRange = 1;

		HL_CHECK(!c.ownWheels && (c.joints == 0u) && (c.rigidCount == 1u) && (c.wheelFirst == 1u) && (c.paletteCount == 5u), "B: rigid body, standard wheels");
		HL_CHECK((c.vertexCount == 4u + 4u * (u32)ENG_HC_STD_VERTICES) && (c.indexCount == 6u + 4u * (u32)ENG_HC_STD_INDICES) && (c.rangeCount == 2u) &&
		             (c.ranges[1].material == 2u),
		         "B: counts, a range for the standard wheels");
		HL_CHECK((c.src[0].j[0] == 0) && (c.src[4].j[0] == 1) && (c.src[4 + ENG_HC_STD_VERTICES * 3].j[0] == 4) && (c.wheelRadius[1] == ENG_HC_STD_RADIUS),
		         "B: palette entries of the rigid body and the standard wheels");
		if (strncmp(c.clipText, "2/12 (missing steer_left->idle, steer_right->idle, drift_left->idle", 66) != 0)
		{
			printf("engine selftest: hires loader B clip text: %s\n", c.clipText);
		}
		HL_CHECK(strncmp(c.clipText, "2/12 (missing steer_left->idle, steer_right->idle, drift_left->idle", 66) == 0, "B: the missing clips with their fallbacks");
		HL_CHECK(EngHc_Resolve(c.clipMask, ENGINE_HIRES_CLIP_PODIUM_3, NULL) == ENGINE_HIRES_CLIP_IDLE, "B: podium_3 falls back to idle");
		for (i = 0; i < c.indexCount; i++)
		{
			inRange = inRange && (c.indices[i] < c.vertexCount);
		}
		HL_CHECK(inRange, "B: every index inside the vertices");
		EngHc_FreeCpu(&c);
	}
	RldHires_Close(&model);
	free(chunk);
	free(glb);
#undef HL_CHECK
	*checks += n;
	return failures;
}

// The selftests of the hi-res drivers: the GLB builder's own check, the pose
// maths (engine_hires_selftest.c), the clip state machine and its fallback
// chains (engine_hires_pose.c).
int Engine_SelfTestHiresChar(int *checks)
{
	int failures = 0;
	int n = 0;

#define HC_CHECK(cond, what)                                           \
	do                                                                 \
	{                                                                  \
		n++;                                                           \
		if (!(cond))                                                   \
		{                                                              \
			failures++;                                                \
			printf("engine selftest: hires char " what " FAILED\n"); \
		}                                                              \
	} while (0)

	failures += EngHiresTest_GlbSelfTest(checks);
	failures += EngHires_SelfTestAnim(checks);

	// Fallback chains (FORMAT section 4) with every clip missing in turn.
	{
		const u32 all = (1u << ENGINE_HIRES_CLIPS) - 1u;
		int full = -1;
		int c;
		int same = 1;

		for (c = 0; c < ENGINE_HIRES_CLIPS; c++)
		{
			same = same && (EngHc_Resolve(all, c, &full) == c) && (full == 0);
		}
		HC_CHECK(same, "every present clip is drawn as itself");
		HC_CHECK((EngHc_Resolve(0u, ENGINE_HIRES_CLIP_IDLE, &full) == ENGINE_HIRES_CLIP_REST) && (full == 0), "idle missing: the rest pose");
		HC_CHECK(EngHc_Resolve(1u << ENGINE_HIRES_CLIP_IDLE, ENGINE_HIRES_CLIP_STEER_LEFT, &full) == ENGINE_HIRES_CLIP_IDLE, "steer_left -> idle");
		HC_CHECK(EngHc_Resolve(1u << ENGINE_HIRES_CLIP_IDLE, ENGINE_HIRES_CLIP_STEER_RIGHT, &full) == ENGINE_HIRES_CLIP_IDLE, "steer_right -> idle");
		HC_CHECK(EngHc_Resolve(1u << ENGINE_HIRES_CLIP_IDLE, ENGINE_HIRES_CLIP_JUMP, &full) == ENGINE_HIRES_CLIP_IDLE, "jump -> idle");
		HC_CHECK((EngHc_Resolve((1u << ENGINE_HIRES_CLIP_IDLE) | (1u << ENGINE_HIRES_CLIP_STEER_LEFT), ENGINE_HIRES_CLIP_DRIFT_LEFT, &full) ==
		          ENGINE_HIRES_CLIP_STEER_LEFT) &&
		             (full == 1),
		         "drift_left -> steer_left at full lock");
		HC_CHECK((EngHc_Resolve((1u << ENGINE_HIRES_CLIP_IDLE) | (1u << ENGINE_HIRES_CLIP_STEER_RIGHT), ENGINE_HIRES_CLIP_DRIFT_RIGHT, &full) ==
		          ENGINE_HIRES_CLIP_STEER_RIGHT) &&
		             (full == 1),
		         "drift_right -> steer_right at full lock");
		HC_CHECK((EngHc_Resolve(1u << ENGINE_HIRES_CLIP_IDLE, ENGINE_HIRES_CLIP_DRIFT_LEFT, &full) == ENGINE_HIRES_CLIP_IDLE) && (full == 0),
		         "drift_left without steer_left -> idle (not at full lock)");
		HC_CHECK((EngHc_Resolve(1u << ENGINE_HIRES_CLIP_STEER_LEFT, ENGINE_HIRES_CLIP_DRIFT_RIGHT, &full) == ENGINE_HIRES_CLIP_REST), "drift_right ends at rest");
		HC_CHECK(EngHc_Resolve(1u << ENGINE_HIRES_CLIP_IDLE, ENGINE_HIRES_CLIP_HIT, &full) == ENGINE_HIRES_CLIP_IDLE, "hit -> idle");
		HC_CHECK(EngHc_Resolve((1u << ENGINE_HIRES_CLIP_IDLE) | (1u << ENGINE_HIRES_CLIP_PODIUM_1), ENGINE_HIRES_CLIP_PODIUM_3, &full) ==
		             ENGINE_HIRES_CLIP_PODIUM_1,
		         "podium_3 -> podium_2 -> podium_1");
		HC_CHECK(EngHc_Resolve((1u << ENGINE_HIRES_CLIP_PODIUM_2), ENGINE_HIRES_CLIP_PODIUM_3, &full) == ENGINE_HIRES_CLIP_PODIUM_2, "podium_3 -> podium_2");
		HC_CHECK(EngHc_Resolve(1u << ENGINE_HIRES_CLIP_IDLE, ENGINE_HIRES_CLIP_PODIUM_1, &full) == ENGINE_HIRES_CLIP_IDLE, "podium_1 -> idle");
		HC_CHECK(EngHc_Resolve(1u << ENGINE_HIRES_CLIP_IDLE, ENGINE_HIRES_CLIP_LOSE, &full) == ENGINE_HIRES_CLIP_IDLE, "lose -> idle");
		HC_CHECK(EngHc_Resolve(1u << ENGINE_HIRES_CLIP_IDLE, ENGINE_HIRES_CLIP_MENU, &full) == ENGINE_HIRES_CLIP_IDLE, "menu -> idle");
		HC_CHECK((EngHc_Resolve(all, 99, NULL) == ENGINE_HIRES_CLIP_REST) && (EngHc_Resolve(all, -3, NULL) == ENGINE_HIRES_CLIP_REST), "unknown clip: rest");
		HC_CHECK((strcmp(Engine_HiresClipName(ENGINE_HIRES_CLIP_PODIUM_3), "podium_3") == 0) && (strcmp(Engine_HiresClipName(ENGINE_HIRES_CLIP_REST), "rest") == 0) &&
		             (strcmp(Engine_HiresClipName(42), "?") == 0),
		         "clip names");
		HC_CHECK((EngHc_PlayOf(ENGINE_HIRES_CLIP_IDLE) == ENG_HC_PLAY_LOOP) && (EngHc_PlayOf(ENGINE_HIRES_CLIP_STEER_RIGHT) == ENG_HC_PLAY_SCRUB) &&
		             (EngHc_PlayOf(ENGINE_HIRES_CLIP_JUMP) == ENG_HC_PLAY_ONCE) && (EngHc_PlayOf(ENGINE_HIRES_CLIP_HIT) == ENG_HC_PLAY_ONCE) &&
		             (EngHc_PlayOf(ENGINE_HIRES_CLIP_DRIFT_LEFT) == ENG_HC_PLAY_LOOP) && (EngHc_PlayOf(ENGINE_HIRES_CLIP_PODIUM_2) == ENG_HC_PLAY_LOOP) &&
		             (EngHc_PlayOf(ENGINE_HIRES_CLIP_LOSE) == ENG_HC_PLAY_LOOP) && (EngHc_PlayOf(ENGINE_HIRES_CLIP_MENU) == ENG_HC_PLAY_LOOP),
		         "play modes");
	}

	// The state machine: a scripted run of ticks (33 ms each).
	{
		EngHcSeatState s;
		EngHcInput in;
		int forceClip = -1;
		int st;
		int i;
		int ok;

		memset(&in, 0, sizeof(in));
		in.touch = 1;
		in.kartState = 0;
		in.elapsedMs = 33;
		in.force = -1;
		EngHc_SeatReset(&s, -1);
		HC_CHECK((s.state == ENG_HC_STATE_BASE) && (s.driftSide == 1) && (s.forceClip == -1) && !s.fading, "reset: base, no fade");

		// Steering: no state change, the fraction and the side.
		in.wheelRotation = 32;
		EngHc_Advance(&s, in.elapsedMs);
		st = EngHc_Decide(&s, &in, &forceClip);
		HC_CHECK((st == ENG_HC_STATE_BASE) && !EngHc_Changes(&s, st, forceClip) && (s.steerRaw == 32), "steering stays in base");
		{
			int clip = -1;
			const float f = EngHc_SteerFraction(32, &clip);
			const float g = EngHc_SteerFraction(-85, &clip);

			HC_CHECK((fabsf(f - 0.5f) < 1e-6f) && (fabsf(g - 1.0f) < 1e-6f) && (clip == ENGINE_HIRES_CLIP_STEER_RIGHT), "steer fraction clamps, negative = right");
			(void)EngHc_SteerFraction(5, &clip);
			HC_CHECK(clip == ENGINE_HIRES_CLIP_STEER_LEFT, "positive wheelRotation = left");
		}

		// Air: two ticks off the ground stay base (a bump), the third is the jump.
		in.touch = 0;
		ok = 1;
		for (i = 0; i < 2; i++)
		{
			EngHc_Advance(&s, in.elapsedMs);
			st = EngHc_Decide(&s, &in, &forceClip);
			ok = ok && (st == ENG_HC_STATE_BASE);
		}
		HC_CHECK(ok, "a bump of 2 ticks stays on the ground");
		EngHc_Advance(&s, in.elapsedMs);
		st = EngHc_Decide(&s, &in, &forceClip);
		HC_CHECK((st == ENG_HC_STATE_JUMP) && EngHc_Changes(&s, st, forceClip), "the third tick in the air is the jump");
		EngHc_Enter(&s, st, forceClip);
		HC_CHECK((s.state == ENG_HC_STATE_JUMP) && (s.stateTime == 0.0f) && s.fading && (s.prevState == ENG_HC_STATE_BASE) &&
		             (EngHc_StateClip(&s) == ENGINE_HIRES_CLIP_JUMP) && (EngHc_FadeWeight(&s) == 0.0f),
		         "jump entered from time 0, fade from base starts at weight 0");
		// The fade: 33 ms steps of 150 ms - the weight rises without a step, 5 ticks.
		{
			float last = 0.0f;
			int rising = 1;

			for (i = 0; i < 4; i++)
			{
				float w;

				EngHc_Advance(&s, in.elapsedMs);
				w = EngHc_FadeWeight(&s);
				rising = rising && (w > last) && (w < 1.0f) && ((w - last) < 0.25f);
				last = w;
			}
			HC_CHECK(rising, "fade weight rises in steps below 0.25 over 4 ticks");
			EngHc_Advance(&s, in.elapsedMs);
			HC_CHECK((EngHc_FadeWeight(&s) == 1.0f) && !s.fading, "fade done after 150 ms");
		}
		// Once, held: the local time of a once clip clamps at its length.
		HC_CHECK((fabsf(EngHc_LocalTime(ENG_HC_PLAY_ONCE, 9.0f, 0.0f, 0.5f, 0) - 0.5f) < 1e-6f) &&
		             (fabsf(EngHc_LocalTime(ENG_HC_PLAY_ONCE, 0.2f, 0.0f, 0.5f, 0) - 0.2f) < 1e-6f),
		         "once clip holds its last pose");
		HC_CHECK((fabsf(EngHc_LocalTime(ENG_HC_PLAY_LOOP, 2.25f, 0.0f, 1.0f, 0) - 0.25f) < 1e-5f) &&
		             (fabsf(EngHc_LocalTime(ENG_HC_PLAY_SCRUB, 5.0f, 0.25f, 2.0f, 0) - 0.5f) < 1e-6f) &&
		             (fabsf(EngHc_LocalTime(ENG_HC_PLAY_SCRUB, 5.0f, 0.25f, 2.0f, 1) - 2.0f) < 1e-6f) && (EngHc_LocalTime(ENG_HC_PLAY_LOOP, 3.0f, 0.0f, 0.0f, 0) == 0.0f),
		         "loop wraps, scrub = fraction x length, full lock = the end");

		// Landing: back on the ground is base again (a change, a fade).
		in.touch = 1;
		EngHc_Advance(&s, in.elapsedMs);
		st = EngHc_Decide(&s, &in, &forceClip);
		HC_CHECK((st == ENG_HC_STATE_BASE) && EngHc_Changes(&s, st, forceClip) && (s.airTicks == 0), "landing: base");
		EngHc_Enter(&s, st, forceClip);

		// Drift: the side from the first tick (multDrift first), kept while it lasts.
		in.kartState = KS_DRIFTING;
		in.multDrift = -300;
		in.wheelRotation = 40;
		EngHc_Advance(&s, in.elapsedMs);
		st = EngHc_Decide(&s, &in, &forceClip);
		HC_CHECK((st == ENG_HC_STATE_DRIFT_RIGHT) && (s.driftSide == -1), "drift right by multDrift");
		EngHc_Enter(&s, st, forceClip);
		in.multDrift = 300;
		EngHc_Advance(&s, in.elapsedMs);
		st = EngHc_Decide(&s, &in, &forceClip);
		HC_CHECK((st == ENG_HC_STATE_DRIFT_RIGHT) && !EngHc_Changes(&s, st, forceClip) && (s.driftTicks == 2), "the side stays for the drift");
		HC_CHECK((EngHc_DriftSide(0, 0, 0) == 1) && (EngHc_DriftSide(0, -2, 9) == -1) && (EngHc_DriftSide(0, 0, -1) == -1) && (EngHc_DriftSide(7, -2, -9) == 1),
		         "drift side: first non-zero of multDrift, simpTurnState, wheelRotation");

		// Hit beats drift and air; entered once, the time from the entry.
		in.kartState = KS_SPINNING;
		in.touch = 0;
		EngHc_Advance(&s, in.elapsedMs);
		st = EngHc_Decide(&s, &in, &forceClip);
		HC_CHECK((st == ENG_HC_STATE_HIT) && EngHc_Changes(&s, st, forceClip) && (s.driftTicks == 0), "spinning is the hit");
		EngHc_Enter(&s, st, forceClip);
		ok = 1;
		for (i = 0; i < 5; i++)
		{
			in.kartState = (i < 2) ? KS_BLASTED : KS_CRASHING;
			EngHc_Advance(&s, in.elapsedMs);
			st = EngHc_Decide(&s, &in, &forceClip);
			ok = ok && (st == ENG_HC_STATE_HIT) && !EngHc_Changes(&s, st, forceClip);
		}
		HC_CHECK(ok && (fabsf(s.stateTime - 0.165f) < 1e-4f) && (s.airTicks >= ENG_HC_AIR_DELAY), "blasted and crashing stay the same hit (in the air too)");

		// Forced: first, a change of the forced clip is a change too; -1 gives it back.
		in.force = ENGINE_HIRES_CLIP_PODIUM_2;
		EngHc_Advance(&s, in.elapsedMs);
		st = EngHc_Decide(&s, &in, &forceClip);
		HC_CHECK((st == ENG_HC_STATE_FORCED) && (forceClip == ENGINE_HIRES_CLIP_PODIUM_2) && EngHc_Changes(&s, st, forceClip), "forced beats hit");
		EngHc_Enter(&s, st, forceClip);
		HC_CHECK(EngHc_StateClip(&s) == ENGINE_HIRES_CLIP_PODIUM_2, "forced clip asked for");
		in.force = ENGINE_HIRES_CLIP_LOSE;
		st = EngHc_Decide(&s, &in, &forceClip);
		HC_CHECK(EngHc_Changes(&s, st, forceClip), "another forced clip is a change");
		in.force = -1;
		in.kartState = 0;
		in.touch = 1;
		st = EngHc_Decide(&s, &in, &forceClip);
		HC_CHECK(st == ENG_HC_STATE_BASE, "force -1 gives the seat back");
		// Finished: podium by rank, lose from rank 3; above hit, below a force.
		in.finished = 1;
		in.rank = 1;
		in.kartState = KS_SPINNING;
		st = EngHc_Decide(&s, &in, &forceClip);
		HC_CHECK((st == ENG_HC_STATE_FINISHED) && (forceClip == ENGINE_HIRES_CLIP_PODIUM_2), "finished on place 2: podium_2, above hit");
		in.rank = 5;
		st = EngHc_Decide(&s, &in, &forceClip);
		HC_CHECK((st == ENG_HC_STATE_FINISHED) && (forceClip == ENGINE_HIRES_CLIP_LOSE), "finished on place 6: lose");
		HC_CHECK((EngHc_FinishedClip(0) == ENGINE_HIRES_CLIP_PODIUM_1) && (EngHc_FinishedClip(2) == ENGINE_HIRES_CLIP_PODIUM_3) &&
		             (EngHc_FinishedClip(3) == ENGINE_HIRES_CLIP_LOSE) && (EngHc_FinishedClip(-1) == ENGINE_HIRES_CLIP_LOSE),
		         "finished clips by rank");
		in.force = ENGINE_HIRES_CLIP_MENU;
		st = EngHc_Decide(&s, &in, &forceClip);
		HC_CHECK((st == ENG_HC_STATE_FORCED) && (forceClip == ENGINE_HIRES_CLIP_MENU), "a force beats finished");
		in.force = -1;
		in.finished = 0;
		in.kartState = 0;
		HC_CHECK((strcmp(EngHc_StateName(ENG_HC_STATE_DRIFT_LEFT), "drift_left") == 0) && (strcmp(EngHc_StateName(9), "?") == 0), "state names");

		// A pause (elapsed 0) moves no time.
		{
			const float before = s.baseTime;

			EngHc_Advance(&s, 0);
			HC_CHECK(s.baseTime == before, "no time without elapsed ms");
		}
		EngHc_SeatReset(&s, ENGINE_HIRES_CLIP_MENU);
		HC_CHECK((s.state == ENG_HC_STATE_FORCED) && (s.forceClip == ENGINE_HIRES_CLIP_MENU), "reset with a force starts forced");
	}

	// The API without a loaded character: no character anywhere.
	HC_CHECK((Engine_HiresCharOfSeat(0) == -1) && (Engine_HiresCharOfFile("x.rldchar") == -1) && (Engine_HiresCharOfEntry(0) == -1) &&
	             (Engine_HiresCharSubmit(0, NULL) == 0) && (Engine_HiresCharOfPlace(1) == -1) && (strcmp(Engine_HiresCharFile(3), "") == 0),
	         "no character without a load");
	// "ns" is made for the hi-res drivers too (R1: it was made for the static model only).
	HC_CHECK(Engine_ProgramStaticWanted("", 1) && Engine_ProgramStaticWanted("m.glb", 0) && !Engine_ProgramStaticWanted("", 0) &&
	             !Engine_ProgramStaticWanted(NULL, 0),
	         "program ns wanted with the static model or the hi-res drivers, else not");
	failures += EngHc_SelfTestLoader(checks);
#undef HC_CHECK
	*checks += n;
	return failures;
}
