// ===========================================================================
// THE GPU SET OF A CUSTOM CHARACTER (renderer plan C.5, step 4c). See
// include/platform/native_char_gpu.h for what a set holds and when it exists.
//
// THE RULES, as for the render layer: nothing here touches the game - no
// Instance, Driver, GameTracker, scratchpad, MEMPACK, primMem or otMem, no
// coprocessor, no random numbers, no clock. The input is the checked native
// part a seat holds (platform/native_chars.c, include/rldchar.inc); the output
// is device objects and host statics of this file. Every upload is made at a
// loading screen (load stage 5); one asked for in a race frame is counted and
// refused, never made.
// ===========================================================================

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <common.h>
#include <macros.h>
#include <platform.h>

#include <rldtrack.inc>
#include <rldchar.inc>

#include "platform/native_char_gpu.h"
#include "platform/native_chars.h"
#include "platform/native_gfx.h"
#include "platform/native_probe.h"
#include "platform/native_renderer.h"
#include "platform/native_tex.h"
#include "platform/native_twin.h"
#include "platform/native_wheels.h"

extern int g_cfg_nativePreview;

// --native-twin (main.c, only with --dev and --native-preview; measuring only):
// seat 0's retail model drawn through the native path as its retail twin
// (step 4d, platform/native_twin.c). Set in the first loop of main.
int g_cfg_nativeTwin = 0;

CTR_STATIC_ASSERT(NATIVE_CHAR_GPU_MATERIALS == RLDCHAR_NET_MATERIALS_MAX);
CTR_STATIC_ASSERT(NATIVE_CHAR_GPU_TEXTURES == RLDCHAR_TEX_COUNT_MAX);
CTR_STATIC_ASSERT(NATIVE_CHAR_GPU_POSES == RLDCHAR_NET_POSES);
CTR_STATIC_ASSERT(NATIVE_CHAR_GPU_SETS == 8);
CTR_STATIC_ASSERT(sizeof(struct NativeProbeVertex) == 24);

// u16 indices are enough: a CNET has at most 20000 vertices (RC, CNET head).
CTR_STATIC_ASSERT(RLDCHAR_NET_VERTICES_MAX <= 0xFFFFu);
CTR_STATIC_ASSERT(RLDCHAR_WHEEL_VERTICES_MAX <= 0xFFFFu);

// One piece of the body upload: at most 1 MiB, a whole number of vertices, so
// the shared staging buffer grows by 1 MiB at most (its growth is given back
// after the set, NativeGfx_ShrinkStaging).
#define NATIVE_CHAR_GPU_PIECE_VERTICES ((1024u * 1024u) / (u32)sizeof(struct NativeProbeVertex))

// The first pose of each animation (21, 7, 15, 4 frames: s_rldCharFrames).
internal const u16 s_ncgPoseBase[RLDCHAR_ANIM_COUNT] = {0, 21, 28, 43};

internal struct NativeCharGpu s_ncgSets[NATIVE_CHAR_GPU_SETS];

// The preview's set (step 5a) and the entry it was made for (-1: none).
internal struct NativeCharGpu s_ncgPreview;
internal int s_ncgPreviewEntry = -1;

// The retail twin (step 4d, --native-twin): its source (platform/
// native_twin.c - the key of the set is &s_ncgTwinSource.native, so it lives
// until the set is let go), its set, and the retail model it was made from.
internal struct NativeTwinSource s_ncgTwinSource;
internal int s_ncgTwinHeld = 0;
internal struct NativeCharGpu s_ncgTwin;
internal const struct Model *s_ncgTwinModel = NULL;
internal int s_ncgSetCount = 0;
internal int s_ncgSeatSet[NATIVE_CHAR_GPU_SETS] = {-1, -1, -1, -1, -1, -1, -1, -1};

// The exit line (NativeCharGpu_ReportLine).
internal struct
{
	u32 made;
	u32 refused;
	u32 released;
	u32 vertexBytes;
	u32 indexBytes;
	u32 textures;
	u32 textureBytes;
	u32 uploads;
	u32 uploadsInRaceFrame;
} s_ncgCount;

int NativeCharGpu_PoseIndex(u32 netPoseCount, int anim, int frame)
{
	if (netPoseCount == 0u)
	{
		return 0;
	}
	if ((anim < 0) || (anim >= RLDCHAR_ANIM_COUNT) || (frame < 0) || (frame >= (int)s_rldCharFrames[anim]))
	{
		return -1;
	}
	return (int)s_ncgPoseBase[anim] + frame;
}

void NativeCharGpu_WheelMiddle(const float front[3], const float rear[3], int wheel, double out[3])
{
	const float *m = (wheel < 2) ? front : rear;

	out[0] = ((wheel & 1) != 0) ? -(double)m[0] : (double)m[0];
	out[1] = (double)m[1];
	out[2] = (double)m[2];
}

void NativeCharGpu_PoseVertices(const struct RldCharNative *n, u32 pose, u32 first, u32 count, struct NativeProbeVertex *out)
{
	u32 i;

	for (i = 0; i < count; i++)
	{
		const u32 v = first + i;
		const u8 *p = &n->poses[(((size_t)pose * n->vertexCount) + v) * RLDCHAR_NET_VERTEX_BYTES];
		struct NativeProbeVertex *o = &out[i];

		o->position[0] = RldChar_F32(&p[0]);
		o->position[1] = RldChar_F32(&p[4]);
		o->position[2] = RldChar_F32(&p[8]);
		if (n->uv != NULL)
		{
			o->texcoord[0] = RldChar_F32(&n->uv[(size_t)v * 8u]);
			o->texcoord[1] = RldChar_F32(&n->uv[((size_t)v * 8u) + 4u]);
		}
		else
		{
			o->texcoord[0] = 0.0f;
			o->texcoord[1] = 0.0f;
		}
		if (n->colors != NULL)
		{
			memcpy(o->color, &n->colors[(size_t)v * 4u], 4u);
		}
		else
		{
			o->color[0] = 255;
			o->color[1] = 255;
			o->color[2] = 255;
			o->color[3] = 255;
		}
	}
}

void NativeCharGpu_FinishLayout(const struct RldCharNative *n, u32 poseCount, int withFinish, u32 *targets, u32 first[NATIVE_CHAR_GPU_FINISH_TARGETS],
                                u32 *total)
{
	int target;

	*targets = 0;
	*total = poseCount;
	for (target = 0; target < NATIVE_CHAR_GPU_FINISH_TARGETS; target++)
	{
		const u8 *positions = NULL;
		const u8 *normals = NULL;

		first[target] = 0;
		if (!withFinish || (n == NULL) || (n->morph == NULL) || !RldChar_MorphTarget(n, target, &positions, &normals))
		{
			continue;
		}
		*targets |= 1u << target;
		first[target] = *total;
		*total += NATIVE_CHAR_GPU_FINISH_STAGES;
	}
}

void NativeCharGpu_BufferVertices(const struct RldCharNative *n, u32 poseCount, const u32 first[NATIVE_CHAR_GPU_FINISH_TARGETS], u32 targets, u32 pose,
                                  u32 v0, u32 count, struct NativeProbeVertex *out)
{
	int target;

	if (pose < poseCount)
	{
		NativeCharGpu_PoseVertices(n, pose, v0, count, out);
		return;
	}

	// A stage: the texture coordinates and colours of the neutral pose, the
	// position of the blend (the normal is not part of the "nr" layout).
	for (target = 0; target < NATIVE_CHAR_GPU_FINISH_TARGETS; target++)
	{
		if (((targets & (1u << target)) != 0u) && (pose >= first[target]) && (pose < (first[target] + NATIVE_CHAR_GPU_FINISH_STAGES)))
		{
			const float t = (float)(pose - first[target] + 1u) / (float)NATIVE_CHAR_GPU_FINISH_STAGES;
			u32 i;

			NativeCharGpu_PoseVertices(n, n->morphBasePose, v0, count, out);
			for (i = 0; i < count; i++)
			{
				float normal[3];

				RldChar_MorphVertex(n, target, t, v0 + i, out[i].position, normal);
			}
			return;
		}
	}
	NativeCharGpu_PoseVertices(n, 0u, v0, count, out);
}

int NativeCharGpu_FinishTarget(int rank)
{
	if ((rank < 0) || (rank >= NATIVE_CHAR_GPU_FINISH_RANKS))
	{
		return -1;
	}
	return (rank < NATIVE_CHAR_GPU_FINISH_WIN_RANKS) ? NATIVE_CHAR_GPU_FINISH_WIN : NATIVE_CHAR_GPU_FINISH_LOSE;
}

// THE RACES WITH A PLACING. driverRank is the place only where the race is
// run to the line: in battle it is a sort or start order (game/PlayLevel.c:
// 416, 424) and the flag comes from being knocked out (game/231/RB_Player.c:87)
// or the clock (RB_Player.c:61, 132; game/UI/UI_Clock.c:472); the crystal
// challenge ends on the crystals or the clock (game/UI/UI_RenderFrame.c:1031,
// UI_Clock.c:472); a boss race is won only in first place; the adventure
// races, relic and CTR token races and time trial have rules of their own.
// Those stay neutral (as before); arcade and versus races, single and cup,
// take the place. The funnel binds custom characters in single arcade races
// alone today (platform/native_chars.c, NativeChar_ModeRefusal); this rule
// holds for whatever it lets through later.
const char *NativeCharGpu_FinishModeRefusal(u32 gameMode1, u32 gameMode2)
{
	if ((gameMode1 & BATTLE_MODE) != 0u)
	{
		return "battle";
	}
	if ((gameMode1 & CRYSTAL_CHALLENGE) != 0u)
	{
		return "crystal challenge";
	}
	if ((gameMode1 & ADVENTURE_BOSS) != 0u)
	{
		return "boss race";
	}
	if ((gameMode1 & (ADVENTURE_MODE | ADVENTURE_ARENA | ADVENTURE_CUP)) != 0u)
	{
		return "adventure";
	}
	if ((gameMode1 & RELIC_RACE) != 0u)
	{
		return "relic race";
	}
	if ((gameMode2 & TOKEN_RACE) != 0u)
	{
		return "token race";
	}
	if ((gameMode1 & TIME_TRIAL) != 0u)
	{
		return "time trial";
	}
	return NULL;
}

int NativeCharGpu_FinishStage(u32 ticksSinceFinish)
{
	const u32 stage = ticksSinceFinish / NATIVE_CHAR_GPU_FINISH_TICKS;

	return (stage >= NATIVE_CHAR_GPU_FINISH_STAGES) ? (NATIVE_CHAR_GPU_FINISH_STAGES - 1) : (int)stage;
}

int NativeCharGpu_FinishPose(const struct NativeCharGpu *set, int target, int stage)
{
	if ((set == NULL) || (target < 0) || (target >= NATIVE_CHAR_GPU_FINISH_TARGETS) || ((set->finishTargets & (1u << target)) == 0u) || (stage < 0) ||
	    (stage >= NATIVE_CHAR_GPU_FINISH_STAGES))
	{
		return -1;
	}
	return (int)(set->finishFirst[target] + (u32)stage);
}

const char *NativeCharGpu_FinishName(int target)
{
	return (target == NATIVE_CHAR_GPU_FINISH_WIN) ? "win" : ((target == NATIVE_CHAR_GPU_FINISH_LOSE) ? "lose" : "none");
}

void NativeCharGpu_FreeCpu(struct NativeCharGpuCpu *cpu)
{
	free(cpu->indices);
	free(cpu->triangleOrder);
	free(cpu->wheelVertices);
	free(cpu->wheelIndices);
	cpu->indices = NULL;
	cpu->triangleOrder = NULL;
	cpu->wheelVertices = NULL;
	cpu->wheelIndices = NULL;
}

internal u32 NativeCharGpu_Index(const struct RldCharNative *n, u32 at)
{
	return (n->indexSize == 4u) ? Rld_ReadLE32(&n->indices[(size_t)at * 4u]) : (u32)Rld_ReadLE16(&n->indices[(size_t)at * 2u]);
}

int NativeCharGpu_Build(const struct RldCharNative *n, struct NativeCharGpuCpu *out)
{
	u32 count[NATIVE_CHAR_GPU_MATERIALS];
	u32 start[NATIVE_CHAR_GPU_MATERIALS];
	u8 plainUse[NATIVE_CHAR_GPU_TEXTURES];
	u8 nearestUse[NATIVE_CHAR_GPU_TEXTURES];
	u32 m;
	u32 t;
	u32 next;
	int pass;

	memset(out, 0, sizeof(*out));
	memset(count, 0, sizeof(count));
	memset(start, 0, sizeof(start));
	memset(plainUse, 0, sizeof(plainUse));
	memset(nearestUse, 0, sizeof(nearestUse));

	if ((n == NULL) || (n->state != RLDCHAR_NATIVE_READY) || (n->materialCount == 0u) || (n->materialCount > NATIVE_CHAR_GPU_MATERIALS) ||
	    (n->textureCount > NATIVE_CHAR_GPU_TEXTURES) || (n->vertexCount == 0u) || (n->triangleCount == 0u))
	{
		snprintf(out->why, sizeof(out->why), "no checked native part");
		return 0;
	}

	out->vertexCount = n->vertexCount;
	out->netPoseCount = n->poseCount;
	out->poseCount = (n->poseCount == 0u) ? 1u : n->poseCount;
	NativeCharGpu_FinishLayout(n, out->poseCount, 1, &out->finishTargets, out->finishFirst, &out->poseTotal);
	out->triangleCount = n->triangleCount;
	out->materialCount = n->materialCount;
	for (t = 0; t < 3u; t++)
	{
		out->hullMin[t] = n->hullMin[t];
		out->hullMax[t] = n->hullMax[t];
	}

	// The materials: tint = rgba / 255, a texture or none, the alpha mode. A
	// blend material refuses the whole set (renderer plan C.12).
	for (m = 0; m < n->materialCount; m++)
	{
		const u8 *mat = &n->materials[(size_t)m * RLDCHAR_NET_MATERIAL_BYTES];
		const s16 texture = (s16)Rld_ReadLE16(&mat[4]);
		const u32 alphaMode = mat[6];
		int c;

		if (alphaMode == 2u)
		{
			snprintf(out->why, sizeof(out->why), "blend material %u is not drawn natively yet", (unsigned)m);
			return 0;
		}

		for (c = 0; c < 4; c++)
		{
			out->materialTint[m][c] = (float)mat[c] / 255.0f;
		}
		out->materialTexture[m] = (texture >= 0) && ((u32)texture < n->textureCount) ? texture : (s16)-1;
		out->materialMask[m] = (alphaMode == 1u) ? 1u : 0u;

		if (out->materialTexture[m] >= 0)
		{
			if ((mat[7] & RLDCHAR_NET_MATERIAL_NEAREST) != 0u)
			{
				nearestUse[out->materialTexture[m]] = 1;
			}
			else
			{
				plainUse[out->materialTexture[m]] = 1;
			}
		}
	}

	for (t = 0; t < n->textureCount; t++)
	{
		out->textureNearest[t] = (nearestUse[t] || ((n->texture[t].flags & RLDCHAR_TEX_NEAREST) != 0u)) ? 1u : 0u;
		out->textureNearestMixed[t] = (nearestUse[t] && plainUse[t]) ? 1u : 0u;
	}

	// The triangles sorted by material, stable: first the opaque materials in
	// ascending order, then the masked ones.
	for (t = 0; t < n->triangleCount; t++)
	{
		const u32 material = (u32)Rld_ReadLE16(&n->triangleMaterials[(size_t)t * 2u]);

		if (material >= n->materialCount)
		{
			snprintf(out->why, sizeof(out->why), "triangle %u names material %u", (unsigned)t, (unsigned)material);
			return 0;
		}
		count[material]++;
	}

	next = 0;
	for (pass = 0; pass < 2; pass++)
	{
		for (m = 0; m < n->materialCount; m++)
		{
			if ((int)out->materialMask[m] != pass)
			{
				continue;
			}
			start[m] = next;
			if (count[m] > 0u)
			{
				struct NativeCharRange *r = &out->range[out->rangeCount++];

				r->firstIndex = next * 3u;
				r->indexCount = count[m] * 3u;
				r->material = (u16)m;
				r->mask = out->materialMask[m];
			}
			next += count[m];
		}
	}

	out->indices = (u16 *)malloc((size_t)n->triangleCount * 3u * sizeof(u16));
	out->triangleOrder = (u32 *)malloc((size_t)n->triangleCount * sizeof(u32));
	if ((out->indices == NULL) || (out->triangleOrder == NULL))
	{
		snprintf(out->why, sizeof(out->why), "memory");
		return 0;
	}

	for (t = 0; t < n->triangleCount; t++)
	{
		const u32 material = (u32)Rld_ReadLE16(&n->triangleMaterials[(size_t)t * 2u]);
		const u32 place = start[material]++;
		int k;

		out->triangleOrder[place] = t;
		for (k = 0; k < 3; k++)
		{
			out->indices[(place * 3u) + (u32)k] = (u16)NativeCharGpu_Index(n, (t * 3u) + (u32)k);
		}
	}

	// The wheels (WHLS): white vertices of the "nr" layout, u16 indices. An
	// author's wheel (version 2 or 3) gets its mirror image behind the mesh for
	// the -X wheels: x negated and every triangle's winding turned (a, c, b), so
	// it is wound counter-clockwise from outside like the mesh and the cull of
	// the body holds for both; the UV stays, so a tread runs mirrored. The
	// normals of WHLS are not part of the "nr" layout (the native mesh is not
	// lit), so nothing else changes. A version 3 wheel with a rear mesh of its
	// own puts that mesh and its mirror image behind, the same way. At most
	// 4 x 2048 vertices: u16 holds them.
	if (n->wheel != NULL)
	{
		const int own = RldChar_WheelIsOwn(n);
		const u32 halves = own ? 2u : 1u;
		struct RldCharWheelMesh mesh[2];
		u32 meshes = 1u;
		u32 vertexTotal;
		u32 indexTotal;
		u32 vertexAt = 0;
		u32 indexAt = 0;
		u32 i;

		memset(mesh, 0, sizeof(mesh));
		RldChar_WheelMesh(n, 0, &mesh[0]);
		if (own && RldChar_WheelMesh(n, 1, &mesh[1]) && mesh[1].own)
		{
			meshes = 2u;
		}
		vertexTotal = (mesh[0].vertexCount + ((meshes == 2u) ? mesh[1].vertexCount : 0u)) * halves;
		indexTotal = (mesh[0].triangleCount + ((meshes == 2u) ? mesh[1].triangleCount : 0u)) * 3u * halves;

		out->wheelVertices = (struct NativeProbeVertex *)malloc((size_t)vertexTotal * sizeof(struct NativeProbeVertex));
		out->wheelIndices = (u16 *)malloc((size_t)indexTotal * sizeof(u16));
		if ((out->wheelVertices == NULL) || (out->wheelIndices == NULL))
		{
			snprintf(out->why, sizeof(out->why), "memory");
			return 0;
		}

		for (m = 0; m < meshes; m++)
		{
			const u32 nw = mesh[m].vertexCount;
			const u32 tw = mesh[m].triangleCount;
			struct NativeProbeVertex *verts = &out->wheelVertices[vertexAt];
			u16 *idx = &out->wheelIndices[indexAt];

			for (i = 0; i < nw; i++)
			{
				const u8 *v = &mesh[m].vertices[i * RLDCHAR_WHEEL_VERTEX_BYTES];
				struct NativeProbeVertex *o = &verts[i];

				o->position[0] = RldChar_F32(&v[0]);
				o->position[1] = RldChar_F32(&v[4]);
				o->position[2] = RldChar_F32(&v[8]);
				o->texcoord[0] = RldChar_F32(&v[24]);
				o->texcoord[1] = RldChar_F32(&v[28]);
				o->color[0] = 255;
				o->color[1] = 255;
				o->color[2] = 255;
				o->color[3] = 255;
			}
			for (i = 0; i < (tw * 3u); i++)
			{
				idx[i] = (u16)(Rld_ReadLE16(&mesh[m].indices[i * 2u]) + vertexAt);
			}
			if (halves == 2u)
			{
				for (i = 0; i < nw; i++)
				{
					verts[nw + i] = verts[i];
					verts[nw + i].position[0] = -verts[i].position[0];
				}
				for (i = 0; i < tw; i++)
				{
					const u16 *src = &idx[i * 3u];
					u16 *dst = &idx[(tw * 3u) + (i * 3u)];

					dst[0] = (u16)(src[0] + nw);
					dst[1] = (u16)(src[2] + nw);
					dst[2] = (u16)(src[1] + nw);
				}
			}

			// The tread count of an author's wheel, from the mesh as it is (the
			// first half): the stroboscope clamp of its drawn roll (render plan A3).
			if (own)
			{
				struct NativeWheelTreads treads;

				NativeWheels_EstimateTreads(&verts[0].position[0], sizeof(struct NativeProbeVertex), nw, idx, tw * 3u, vertexAt, &treads);
				if (m == 0u)
				{
					out->wheelTreads = treads.treads;
					out->wheelTreadsEstimated = (u8)treads.estimated;
					out->wheelTreadOuter = treads.outer;
					out->wheelTreadAngles = treads.angles;
					out->wheelTreadOpen = treads.open;
					out->wheelTreadImplausible = treads.implausible;
					out->wheelTreadStrength = (float)treads.strength;
				}
				else
				{
					out->wheelRearTreads = treads.treads;
					out->wheelRearTreadsEstimated = (u8)treads.estimated;
				}
			}

			if (m == 1u)
			{
				out->wheelRearOwn = 1u;
				out->wheelRearFirst = indexAt;
				out->wheelRearMirrorFirst = indexAt + (tw * 3u);
				out->wheelRearIndexCount = tw * 3u;
				out->wheelRearMaterial = (u16)mesh[1].material;
				out->wheelRearRadius = mesh[1].radius;
				out->wheelRearHalfWidth = mesh[1].halfWidth;
				if (mesh[1].material >= n->materialCount)
				{
					snprintf(out->why, sizeof(out->why), "the rear wheels name material %u", (unsigned)mesh[1].material);
					return 0;
				}
			}
			vertexAt += nw * halves;
			indexAt += tw * 3u * halves;
		}

		out->hasWheels = 1;
		out->wheelOwn = own ? 1u : 0u;
		out->wheelVertexCount = vertexTotal;
		out->wheelIndexCount = mesh[0].triangleCount * 3u;
		out->wheelIndexTotal = indexTotal;
		out->wheelMirrorFirst = (halves == 2u) ? (mesh[0].triangleCount * 3u) : 0u;
		out->wheelMaterial = (u16)mesh[0].material;
		out->wheelRadius = mesh[0].radius;
		out->wheelHalfWidth = mesh[0].halfWidth;
		out->wheelAlways = (own && ((n->wheelFlags & RLDCHAR_WHEEL_ALWAYS_DRAW) != 0u)) ? 1u : 0u;
		for (i = 0; i < 3u; i++)
		{
			out->wheelFront[i] = RldChar_F32(&n->wheel[0x14 + (i * 4u)]);
			out->wheelRear[i] = RldChar_F32(&n->wheel[0x20 + (i * 4u)]);
		}
		if (out->wheelMaterial >= n->materialCount)
		{
			snprintf(out->why, sizeof(out->why), "the wheels name material %u", (unsigned)out->wheelMaterial);
			return 0;
		}
	}

	return 1;
}

// --- The device side ---------------------------------------------------------

// THE BUFFERS LET GO LATER. A set is released at a loading screen (load stage
// 0), but the frame that stage runs in may already hold recorded draws of the
// race before - a buffer destroyed now would be named by commands not yet
// submitted. So the buffers of a released set wait here and are destroyed at
// the next load stage 5 (NativeCharGpu_LoadSeats), stages later, when no frame
// in flight can name them; the texture manager defers its images itself.
#define NATIVE_CHAR_GPU_GRAVE (NATIVE_CHAR_GPU_SETS * 2)
internal NativeGfxBuffer s_ncgGraveVB[NATIVE_CHAR_GPU_GRAVE];
internal NativeGfxBuffer s_ncgGraveIB[NATIVE_CHAR_GPU_GRAVE];
internal int s_ncgGraveVBCount = 0;
internal int s_ncgGraveIBCount = 0;

internal void NativeCharGpu_DropVB(NativeGfxBuffer buffer, int defer)
{
	if ((buffer == NATIVE_GFX_INVALID) || (buffer == 0u))
	{
		return;
	}
	if (defer && (s_ncgGraveVBCount < NATIVE_CHAR_GPU_GRAVE))
	{
		s_ncgGraveVB[s_ncgGraveVBCount++] = buffer;
		return;
	}
	NativeGfx_DestroyVertexBuffer(buffer);
}

internal void NativeCharGpu_DropIB(NativeGfxBuffer buffer, int defer)
{
	if ((buffer == NATIVE_GFX_INVALID) || (buffer == 0u))
	{
		return;
	}
	if (defer && (s_ncgGraveIBCount < NATIVE_CHAR_GPU_GRAVE))
	{
		s_ncgGraveIB[s_ncgGraveIBCount++] = buffer;
		return;
	}
	NativeGfx_DestroyIndexBuffer(buffer);
}

internal void NativeCharGpu_EmptyGrave(void)
{
	int i;

	for (i = 0; i < s_ncgGraveVBCount; i++)
	{
		NativeGfx_DestroyVertexBuffer(s_ncgGraveVB[i]);
	}
	for (i = 0; i < s_ncgGraveIBCount; i++)
	{
		NativeGfx_DestroyIndexBuffer(s_ncgGraveIB[i]);
	}
	s_ncgGraveVBCount = 0;
	s_ncgGraveIBCount = 0;
}

internal void NativeCharGpu_Release(struct NativeCharGpu *set, int defer)
{
	u32 t;

	NativeCharGpu_DropVB(set->bodyVB, defer);
	NativeCharGpu_DropIB(set->bodyIB, defer);
	NativeCharGpu_DropVB(set->wheelVB, defer);
	NativeCharGpu_DropIB(set->wheelIB, defer);
	for (t = 0; t < set->textureCount; t++)
	{
		if (set->texture[t] != NATIVE_GFX_INVALID)
		{
			NativeTex_Destroy(set->texture[t]);
		}
	}
	memset(set, 0, sizeof(*set));
	set->bodyVB = NATIVE_GFX_INVALID;
	set->bodyIB = NATIVE_GFX_INVALID;
	set->wheelVB = NATIVE_GFX_INVALID;
	set->wheelIB = NATIVE_GFX_INVALID;
	for (t = 0; t < NATIVE_CHAR_GPU_TEXTURES; t++)
	{
		set->texture[t] = NATIVE_GFX_INVALID;
	}
}

void NativeCharGpu_ReleaseAll(void)
{
	int i;

	for (i = 0; i < s_ncgSetCount; i++)
	{
		if (s_ncgSets[i].state == NATIVE_CHAR_GPU_READY)
		{
			s_ncgCount.released++;
		}
		NativeCharGpu_Release(&s_ncgSets[i], 1);
	}
	s_ncgSetCount = 0;
	for (i = 0; i < NATIVE_CHAR_GPU_SETS; i++)
	{
		s_ncgSeatSet[i] = -1;
	}
	NativeCharGpu_ReleasePreview();
	NativeCharGpu_ReleaseTwin();
}

internal int NativeCharGpu_Refuse(struct NativeCharGpu *set, const char *why)
{
	snprintf(set->why, sizeof(set->why), "%s", why);
	set->state = NATIVE_CHAR_GPU_REFUSED;
	return 0;
}

// The device objects of one set; 0 with set->why. Everything made so far is
// let go again on a refusal.
// seat >= 0: a seat's set; else tag names the place ("preview", "twin") in the
// lines. extraTexFlags go onto every texture (the twin: NativeTwin_TextureFlags,
// ONE_LEVEL above all); 0 for the sets of CNET/CTXT, which take their flags from
// CTXT alone as before.
internal int NativeCharGpu_Upload(struct NativeCharGpu *set, const struct RldCharNative *n, int seat, const char *file, const char *tag, u32 extraTexFlags)
{
	struct NativeCharGpuCpu cpu;
	u32 stagingBefore;
	u32 t;
	int ok = 0;

	set->key = n;
	set->seat = seat;

	// Never in a race frame: counted and refused.
	if (NativeTex_InRaceFrame())
	{
		s_ncgCount.uploadsInRaceFrame++;
		return NativeCharGpu_Refuse(set, "race frame");
	}

	if (!NativeCharGpu_Build(n, &cpu))
	{
		NativeCharGpu_Refuse(set, cpu.why);
		NativeCharGpu_FreeCpu(&cpu);
		return 0;
	}

	set->vertexCount = cpu.vertexCount;
	set->poseCount = cpu.poseCount;
	set->netPoseCount = cpu.netPoseCount;
	// The win/lose stages only for a seat (never the preview or the twin).
	NativeCharGpu_FinishLayout(n, cpu.poseCount, seat >= 0, &set->finishTargets, set->finishFirst, &set->poseTotal);
	set->triangleCount = cpu.triangleCount;
	set->rangeCount = cpu.rangeCount;
	memcpy(set->range, cpu.range, sizeof(set->range));
	set->materialCount = cpu.materialCount;
	memcpy(set->materialTint, cpu.materialTint, sizeof(set->materialTint));
	memcpy(set->materialTexture, cpu.materialTexture, sizeof(set->materialTexture));
	memcpy(set->materialMask, cpu.materialMask, sizeof(set->materialMask));
	memcpy(set->hullMin, cpu.hullMin, sizeof(set->hullMin));
	memcpy(set->hullMax, cpu.hullMax, sizeof(set->hullMax));
	set->hasWheels = cpu.hasWheels;
	set->wheelOwn = cpu.wheelOwn;
	set->wheelVertexCount = cpu.wheelVertexCount;
	set->wheelIndexCount = cpu.wheelIndexCount;
	set->wheelIndexTotal = cpu.wheelIndexTotal;
	set->wheelMirrorFirst = cpu.wheelMirrorFirst;
	set->wheelMaterial = cpu.wheelMaterial;
	set->wheelRadius = cpu.wheelRadius;
	set->wheelHalfWidth = cpu.wheelHalfWidth;
	memcpy(set->wheelFront, cpu.wheelFront, sizeof(set->wheelFront));
	memcpy(set->wheelRear, cpu.wheelRear, sizeof(set->wheelRear));
	set->wheelTreads = cpu.wheelTreads;
	set->wheelTreadsEstimated = cpu.wheelTreadsEstimated;
	set->wheelTreadOuter = cpu.wheelTreadOuter;
	set->wheelTreadAngles = cpu.wheelTreadAngles;
	set->wheelTreadOpen = cpu.wheelTreadOpen;
	set->wheelTreadImplausible = cpu.wheelTreadImplausible;
	set->wheelTreadStrength = cpu.wheelTreadStrength;
	set->wheelAlways = cpu.wheelAlways;
	set->wheelRearOwn = cpu.wheelRearOwn;
	set->wheelRearFirst = cpu.wheelRearFirst;
	set->wheelRearMirrorFirst = cpu.wheelRearMirrorFirst;
	set->wheelRearIndexCount = cpu.wheelRearIndexCount;
	set->wheelRearMaterial = cpu.wheelRearMaterial;
	set->wheelRearRadius = cpu.wheelRearRadius;
	set->wheelRearHalfWidth = cpu.wheelRearHalfWidth;
	set->wheelRearTreads = cpu.wheelRearTreads;
	set->wheelRearTreadsEstimated = cpu.wheelRearTreadsEstimated;

	stagingBefore = NativeGfx_StagingBytes();

	// The body: every pose and the win/lose stages, in pieces of at most 1 MiB.
	{
		const u64 vertices = (u64)set->poseTotal * (u64)set->vertexCount;
		const u64 bytes = vertices * (u64)sizeof(struct NativeProbeVertex);
		struct NativeProbeVertex *piece = (struct NativeProbeVertex *)malloc((size_t)NATIVE_CHAR_GPU_PIECE_VERTICES * sizeof(struct NativeProbeVertex));
		u64 done = 0;

		if ((piece == NULL) || (bytes > 0x7FFFFFFFu))
		{
			free(piece);
			NativeCharGpu_Refuse(set, "vertex buffer");
			goto out;
		}

		set->bodyVB = NativeRenderer_CreateNativeMeshVertexBuffer((int)bytes, NULL);
		if (set->bodyVB == NATIVE_GFX_INVALID)
		{
			free(piece);
			NativeCharGpu_Refuse(set, "vertex buffer");
			goto out;
		}

		// A piece may run across the end of a pose: it is filled in runs, each
		// inside one pose.
		while (done < vertices)
		{
			u32 filled = 0;

			while ((filled < NATIVE_CHAR_GPU_PIECE_VERTICES) && ((done + filled) < vertices))
			{
				const u64 at = done + filled;
				const u32 pose = (u32)(at / set->vertexCount);
				const u32 first = (u32)(at % set->vertexCount);
				u32 run = set->vertexCount - first;

				if (run > (NATIVE_CHAR_GPU_PIECE_VERTICES - filled))
				{
					run = NATIVE_CHAR_GPU_PIECE_VERTICES - filled;
				}
				NativeCharGpu_BufferVertices(n, set->poseCount, set->finishFirst, set->finishTargets, pose, first, run, &piece[filled]);
				filled += run;
			}

			NativeGfx_UpdateVertexBuffer(set->bodyVB, (int)(done * sizeof(struct NativeProbeVertex)), (int)(filled * sizeof(struct NativeProbeVertex)),
			                             piece);
			s_ncgCount.uploads++;
			done += filled;
		}
		free(piece);
		set->vertexBytes = (u32)bytes;
	}

	set->bodyIB = NativeGfx_CreateIndexBuffer(&(NativeGfxIndexBufferDesc){
	    .bytes = (int)(set->triangleCount * 3u * sizeof(u16)),
	    .type = NATIVE_GFX_INDEX_U16,
	    .initial = cpu.indices,
	});
	if (set->bodyIB == NATIVE_GFX_INVALID)
	{
		NativeCharGpu_Refuse(set, "index buffer");
		goto out;
	}
	s_ncgCount.uploads++;
	set->indexBytes = set->triangleCount * 3u * (u32)sizeof(u16);

	if (set->hasWheels)
	{
		set->wheelVB = NativeRenderer_CreateNativeMeshVertexBuffer((int)(set->wheelVertexCount * sizeof(struct NativeProbeVertex)), cpu.wheelVertices);
		if (set->wheelVB == NATIVE_GFX_INVALID)
		{
			NativeCharGpu_Refuse(set, "vertex buffer");
			goto out;
		}
		s_ncgCount.uploads++;
		set->wheelIB = NativeGfx_CreateIndexBuffer(&(NativeGfxIndexBufferDesc){
		    .bytes = (int)(set->wheelIndexTotal * sizeof(u16)),
		    .type = NATIVE_GFX_INDEX_U16,
		    .initial = cpu.wheelIndices,
		});
		if (set->wheelIB == NATIVE_GFX_INVALID)
		{
			NativeCharGpu_Refuse(set, "index buffer");
			goto out;
		}
		s_ncgCount.uploads++;
		set->vertexBytes += set->wheelVertexCount * (u32)sizeof(struct NativeProbeVertex);
		set->indexBytes += set->wheelIndexTotal * (u32)sizeof(u16);
	}

	// The textures, with their levels, through the native texture manager.
	set->textureCount = n->textureCount;
	for (t = 0; t < n->textureCount; t++)
	{
		const struct RldCharTexture *src = &n->texture[t];
		const u32 wrapU = (src->flags >> RLDCHAR_TEX_WRAP_U_SHIFT) & 3u;
		const u32 wrapV = (src->flags >> RLDCHAR_TEX_WRAP_V_SHIFT) & 3u;
		NativeTexDesc desc;
		NativeTexResult result = NATIVE_TEX_OK;
		char name[96];

		snprintf(name, sizeof(name), "%s tex %u", file, (unsigned)t);
		memset(&desc, 0, sizeof(desc));
		desc.width = (int)src->width;
		desc.height = (int)src->height;
		desc.rgba = src->rgba;
		desc.flags = (((src->flags & RLDCHAR_TEX_LINEAR) != 0u) ? NATIVE_TEX_FLAG_LINEAR_DATA : 0u) |
		             (cpu.textureNearest[t] ? NATIVE_TEX_FLAG_ALWAYS_NEAREST : 0u) | extraTexFlags;
		desc.wrapU = (wrapU == 1u) ? NATIVE_GFX_WRAP_CLAMP : ((wrapU == 2u) ? NATIVE_GFX_WRAP_MIRROR : NATIVE_GFX_WRAP_REPEAT);
		desc.wrapV = (wrapV == 1u) ? NATIVE_GFX_WRAP_CLAMP : ((wrapV == 2u) ? NATIVE_GFX_WRAP_MIRROR : NATIVE_GFX_WRAP_REPEAT);
		desc.name = name;

		if (cpu.textureNearestMixed[t])
		{
			if (seat >= 0)
			{
				Platform_Log("[CTR NativeChar] seat %d: %s texture %u is used with and without the nearest flag - drawn nearest\n", seat, file, (unsigned)t);
			}
			else
			{
				Platform_Log("[CTR NativeChar] %s: %s texture %u is used with and without the nearest flag - drawn nearest\n", tag, file, (unsigned)t);
			}
		}

		set->texture[t] = NativeTex_Create(&desc, &result);
		s_ncgCount.uploads++;
		if (set->texture[t] == NATIVE_GFX_INVALID)
		{
			char why[96];

			snprintf(why, sizeof(why), "texture %u: %s", (unsigned)t, NativeTex_ResultName(result));
			NativeCharGpu_Refuse(set, why);
			goto out;
		}
		set->textureSrgb[t] = (u8)NativeTex_IsSrgb(set->texture[t]);
		set->textureBytes += (u32)NativeTex_ChainBytes((int)src->width, (int)src->height,
		                                               ((desc.flags & NATIVE_TEX_FLAG_ONE_LEVEL) != 0u) ? 1 : NativeTex_LevelCount((int)src->width, (int)src->height));
	}

	set->state = NATIVE_CHAR_GPU_READY;
	ok = 1;

out:
	NativeGfx_ShrinkStaging(stagingBefore);
	NativeCharGpu_FreeCpu(&cpu);
	if (!ok)
	{
		char why[96];

		snprintf(why, sizeof(why), "%s", set->why);
		NativeCharGpu_Release(set, 0);
		set->key = n;
		set->seat = seat;
		NativeCharGpu_Refuse(set, why);
	}
	return ok;
}

// The stroboscope of an author's wheel (render plan A3), a line of its own
// after the upload line: the tread count and the clamp of the drawn roll.
internal void NativeCharGpu_LogTreads(const char *who, const struct NativeCharGpu *set)
{
	if (!set->hasWheels || !set->wheelOwn || (set->wheelTreads <= 0))
	{
		return;
	}
	Platform_Log("[CTR NativeChar] %s: own wheel treads %d (%s, %u outer points at %u angles, open gaps %d, strength %.2f, not believed %d), "
	             "roll clamp %.4f rad per tick (half pitch %.4f)\n",
	             who, set->wheelTreads, set->wheelTreadsEstimated ? "estimated" : "default", (unsigned)set->wheelTreadOuter, (unsigned)set->wheelTreadAngles,
	             set->wheelTreadOpen, (double)set->wheelTreadStrength, set->wheelTreadImplausible, NativeWheels_StrobeClamp(set->wheelTreads),
	             3.141592653589793 / (double)set->wheelTreads);

	// WHLS version 3: the rear wheel, the middles with the axle offsets and the
	// level of detail, a line of its own (a version 2 wheel keeps the lines of
	// before).
	if (set->wheelRearOwn || set->wheelAlways || (set->key != NULL && set->key->wheelVersion == RLDCHAR_WHEEL_VERSION_USER3))
	{
		char rear[96];

		if (set->wheelRearOwn)
		{
			snprintf(rear, sizeof(rear), "own %u triangles, material %u, radius %.3f, treads %d (%s)", (unsigned)(set->wheelRearIndexCount / 3u),
			         (unsigned)set->wheelRearMaterial, (double)set->wheelRearRadius, set->wheelRearTreads,
			         set->wheelRearTreadsEstimated ? "estimated" : "default");
		}
		else
		{
			snprintf(rear, sizeof(rear), "%s", "the front mesh");
		}
		Platform_Log("[CTR NativeChar] %s: own wheels v3: rear %s; middles front %.3f %.3f %.3f rear %.3f %.3f %.3f model units (axle offsets "
		             "front %.3f %.3f %.3f rear %.3f %.3f %.3f included); level of detail %s\n",
		             who, rear, (double)set->wheelFront[0], (double)set->wheelFront[1], (double)set->wheelFront[2], (double)set->wheelRear[0],
		             (double)set->wheelRear[1], (double)set->wheelRear[2], (double)set->key->wheelAxle[0][0], (double)set->key->wheelAxle[0][1],
		             (double)set->key->wheelAxle[0][2], (double)set->key->wheelAxle[1][0], (double)set->key->wheelAxle[1][1],
		             (double)set->key->wheelAxle[1][2], set->wheelAlways ? "always drawn (ALWAYS_DRAW)" : "as the retail tyres");
	}
}

// The materials as drawn - the palette source of the colour checks (the tint
// is the MATL colour, the texture the CTXT entry it names). who: "seat N" or
// "preview".
internal void NativeCharGpu_LogMaterials(const char *who, const char *file, const struct NativeCharGpu *set, const struct RldCharNative *n)
{
	u32 m;

	for (m = 0; m < set->materialCount; m++)
	{
		const int texture = set->materialTexture[m];
		char what[64];

		if ((texture >= 0) && ((u32)texture < n->textureCount))
		{
			snprintf(what, sizeof(what), "texture %d (%ux%u, %s)", texture, (unsigned)n->texture[texture].width, (unsigned)n->texture[texture].height,
			         set->textureSrgb[texture] ? "srgb" : "linear");
		}
		else
		{
			snprintf(what, sizeof(what), "no texture");
		}
		Platform_Log("[CTR NativeChar] %s: %s material %u: tint %d %d %d %d, %s, %s\n", who, file, (unsigned)m, (int)(set->materialTint[m][0] * 255.0f + 0.5f),
		             (int)(set->materialTint[m][1] * 255.0f + 0.5f), (int)(set->materialTint[m][2] * 255.0f + 0.5f),
		             (int)(set->materialTint[m][3] * 255.0f + 0.5f), set->materialMask[m] ? "mask" : "opaque", what);
	}
}

// The wheels word of an upload line: "%u triangles" (the test wheel),
// "hidden", or for an author's wheel (WHLS version 2) "own %u triangles,
// mirrored, material %u, texture %d WxH" (with levels, as every texture of the
// set) or "..., one colour".
internal void NativeCharGpu_WheelsWord(const struct NativeCharGpu *set, const struct RldCharNative *n, char *out, size_t size)
{
	if (set->hasWheels && set->wheelOwn)
	{
		const int texture = set->materialTexture[set->wheelMaterial];

		if ((texture >= 0) && ((u32)texture < n->textureCount))
		{
			snprintf(out, size, "own %u triangles, mirrored, material %u, texture %d %ux%u", (unsigned)(set->wheelIndexCount / 3u),
			         (unsigned)set->wheelMaterial, texture, (unsigned)n->texture[texture].width, (unsigned)n->texture[texture].height);
		}
		else
		{
			snprintf(out, size, "own %u triangles, mirrored, material %u, one colour", (unsigned)(set->wheelIndexCount / 3u), (unsigned)set->wheelMaterial);
		}
	}
	else if (set->hasWheels)
	{
		snprintf(out, size, "%u triangles", (unsigned)(set->wheelIndexCount / 3u));
	}
	else
	{
		snprintf(out, size, "hidden");
	}
}

void NativeCharGpu_LoadSeats(int levelID)
{
	int seat;

	(void)levelID;

	if (!g_cfg_nativePreview)
	{
		return;
	}

	// What the last load let go, now that no frame can name it.
	NativeCharGpu_EmptyGrave();

	for (seat = 0; seat < NATIVE_CHAR_GPU_SETS; seat++)
	{
		const struct RldCharNative *n = NativeChar_SeatNative(seat);
		char who[16];
		const char *file;
		struct NativeCharGpu *set;
		int i;
		int shared = -1;

		s_ncgSeatSet[seat] = -1;
		if (n == NULL)
		{
			continue;
		}
		file = NativeChar_SeatFile(seat);

		for (i = 0; i < s_ncgSetCount; i++)
		{
			if (s_ncgSets[i].key == n)
			{
				shared = i;
				break;
			}
		}
		if (shared >= 0)
		{
			s_ncgSeatSet[seat] = shared;
			if (s_ncgSets[shared].state == NATIVE_CHAR_GPU_READY)
			{
				Platform_Log("[CTR NativeChar] seat %d: %s shares the upload of seat %d\n", seat, file, s_ncgSets[shared].seat);
			}
			else
			{
				Platform_Log("[CTR NativeChar] seat %d: %s not uploaded (%s), using CMDL\n", seat, file, s_ncgSets[shared].why);
			}
			continue;
		}

		if (s_ncgSetCount >= NATIVE_CHAR_GPU_SETS)
		{
			s_ncgCount.refused++;
			Platform_Log("[CTR NativeChar] seat %d: %s not uploaded (table full), using CMDL\n", seat, file);
			continue;
		}

		set = &s_ncgSets[s_ncgSetCount];
		NativeCharGpu_Release(set, 0);
		s_ncgSeatSet[seat] = s_ncgSetCount;
		s_ncgSetCount++;

		if (!NativeCharGpu_Upload(set, n, seat, file, "seat", 0u))
		{
			s_ncgCount.refused++;
			Platform_Log("[CTR NativeChar] seat %d: %s not uploaded (%s), using CMDL\n", seat, file, set->why);
			continue;
		}

		snprintf(who, sizeof(who), "seat %d", seat);
		s_ncgCount.made++;
		s_ncgCount.vertexBytes += set->vertexBytes;
		s_ncgCount.indexBytes += set->indexBytes;
		s_ncgCount.textures += set->textureCount;
		s_ncgCount.textureBytes += set->textureBytes;

		{
			char wheels[96];

			NativeCharGpu_WheelsWord(set, n, wheels, sizeof(wheels));
			Platform_Log("[CTR NativeChar] seat %d: %s uploaded: %u vertices x %u pose(s) (%u KB), %u triangles in %u material range(s), %u texture(s) "
			             "(%u KB with levels), wheels %s\n",
			             seat, file, (unsigned)set->vertexCount, (unsigned)set->poseCount,
			             (unsigned)((set->poseCount * set->vertexCount * (u32)sizeof(struct NativeProbeVertex)) / 1024u), (unsigned)set->triangleCount,
			             (unsigned)set->rangeCount, (unsigned)set->textureCount, (unsigned)(set->textureBytes / 1024u), wheels);
		}
		NativeCharGpu_LogTreads(who, set);
		if (set->finishTargets != 0u)
		{
			Platform_Log("[CTR NativeChar] seat %d: win/lose poses: win %s, lose %s, %d stages each behind pose %u, %u poses in all (%u KB)\n", seat,
			             ((set->finishTargets & (1u << NATIVE_CHAR_GPU_FINISH_WIN)) != 0u) ? "yes" : "no",
			             ((set->finishTargets & (1u << NATIVE_CHAR_GPU_FINISH_LOSE)) != 0u) ? "yes" : "no", NATIVE_CHAR_GPU_FINISH_STAGES,
			             (unsigned)(set->poseCount - 1u), (unsigned)set->poseTotal,
			             (unsigned)(((set->poseTotal - set->poseCount) * set->vertexCount * (u32)sizeof(struct NativeProbeVertex)) / 1024u));
		}

		NativeCharGpu_LogMaterials(who, file, set, n);
	}
}

void NativeCharGpu_ReleasePreview(void)
{
	if (s_ncgPreviewEntry < 0)
	{
		return;
	}
	if (s_ncgPreview.state == NATIVE_CHAR_GPU_READY)
	{
		s_ncgCount.released++;
	}
	NativeCharGpu_Release(&s_ncgPreview, 1);
	s_ncgPreviewEntry = -1;
}

void NativeCharGpu_LoadPreview(int entry, u64 started)
{
	const struct RldCharNative *n = NativeChar_PreviewNative(entry);
	const char *file = NativeChar_EntryFile(entry);
	u32 ms;

	if (!g_cfg_nativePreview || (n == NULL) || (entry == s_ncgPreviewEntry))
	{
		return;
	}
	if (started == 0u)
	{
		started = SDL_GetPerformanceCounter();
	}

	NativeCharGpu_ReleasePreview();

	// What the last preview or load let go, a frame ago at least (this runs in
	// the pull, before the queue of this frame records anything).
	NativeCharGpu_EmptyGrave();

	NativeCharGpu_Release(&s_ncgPreview, 0);
	s_ncgPreviewEntry = entry;
	if (!NativeCharGpu_Upload(&s_ncgPreview, n, -1, file, "preview", 0u))
	{
		s_ncgCount.refused++;
		Platform_Log("[CTR NativeChar] preview: %s not uploaded (%s), using CMDL\n", file, s_ncgPreview.why);
		return;
	}
	ms = (u32)(((SDL_GetPerformanceCounter() - started) * 1000u) / SDL_GetPerformanceFrequency());

	s_ncgCount.made++;
	s_ncgCount.vertexBytes += s_ncgPreview.vertexBytes;
	s_ncgCount.indexBytes += s_ncgPreview.indexBytes;
	s_ncgCount.textures += s_ncgPreview.textureCount;
	s_ncgCount.textureBytes += s_ncgPreview.textureBytes;

	{
		const struct NativeCharGpu *set = &s_ncgPreview;
		char wheels[96];

		NativeCharGpu_WheelsWord(set, n, wheels, sizeof(wheels));
		Platform_Log("[CTR NativeChar] preview: %s uploaded: %u vertices x %u pose(s) (%u KB), %u triangles in %u material range(s), %u texture(s) "
		             "(%u KB with levels), wheels %s, took %u ms\n",
		             file, (unsigned)set->vertexCount, (unsigned)set->poseCount,
		             (unsigned)((set->poseCount * set->vertexCount * (u32)sizeof(struct NativeProbeVertex)) / 1024u), (unsigned)set->triangleCount,
		             (unsigned)set->rangeCount, (unsigned)set->textureCount, (unsigned)(set->textureBytes / 1024u), wheels, (unsigned)ms);
		NativeCharGpu_LogMaterials("preview", file, set, n);
	}
}

const struct NativeCharGpu *NativeCharGpu_ForPreview(int entry)
{
	if ((entry < 0) || (entry != s_ncgPreviewEntry) || (s_ncgPreview.state != NATIVE_CHAR_GPU_READY) || (s_ncgPreview.key != NativeChar_PreviewNative(entry)))
	{
		return NULL;
	}
	return &s_ncgPreview;
}

// --- The retail twin (step 4d) ---------------------------------------------------

void NativeCharGpu_ReleaseTwin(void)
{
	if (!s_ncgTwinHeld)
	{
		return;
	}
	if (s_ncgTwin.state == NATIVE_CHAR_GPU_READY)
	{
		s_ncgCount.released++;
	}
	NativeCharGpu_Release(&s_ncgTwin, 1);
	NativeTwin_Free(&s_ncgTwinSource);
	memset(&s_ncgTwinSource, 0, sizeof(s_ncgTwinSource));
	s_ncgTwinHeld = 0;
	s_ncgTwinModel = NULL;
}

// Load stage 5, right after the seats (game/LOAD/LOAD_TenStages.c), only with
// --native-twin: the twin of seat 0's retail model - the model the birth will
// look up for data.characterIDs[0] in the driver pack of this load
// (NativeChar_RetailSeatModel) - built on the CPU (NativeTwin_FromModel,
// header 0), its pages decoded from the VRAM mirror (NativeTwin_DecodeVram),
// then uploaded like a seat's set with NativeTwin_TextureFlags on every texture
// (linear, nearest, one level). Only for a race the custom funnel would bind in
// (one-player arcade), never with a custom character on seat 0; silent for a
// menu load. One line "[CTR Twin] ..." (NativeTwin_Describe) and one line
// "twin: ... uploaded" or "not built/uploaded (why)".
void NativeCharGpu_LoadTwin(int levelID)
{
	const struct GameTracker *gGT = sdata->gGT;
	const struct Model *model;
	const char *modeWhy;
	char why[160];
	char line[1024];

	(void)levelID;
	if (!g_cfg_nativePreview || !g_cfg_nativeTwin || (gGT == NULL) || (((u32)gGT->gameMode1 & MAIN_MENU) != 0))
	{
		return;
	}

	NativeCharGpu_ReleaseTwin();
	NativeCharGpu_EmptyGrave();

	modeWhy = NativeChar_ModeRefusal(gGT);
	if (modeWhy != NULL)
	{
		Platform_Log("[CTR NativeChar] twin: not built (%s)\n", modeWhy);
		return;
	}
	if (NativeChar_SeatModel(0) != NULL)
	{
		Platform_Log("[CTR NativeChar] twin: not built (custom character on seat 0)\n");
		return;
	}
	model = NativeChar_RetailSeatModel(0);
	if (model == NULL)
	{
		Platform_Log("[CTR NativeChar] twin: not built (no retail model for seat 0 in the driver pack)\n");
		return;
	}

	memset(&s_ncgTwinSource, 0, sizeof(s_ncgTwinSource));
	why[0] = '\0';
	s_ncgTwinHeld = 1;
	s_ncgTwinModel = model;
	NativeCharGpu_Release(&s_ncgTwin, 0);

	if (!NativeTwin_FromModel(model, 0, &s_ncgTwinSource, why, sizeof(why)))
	{
		s_ncgCount.refused++;
		Platform_Log("[CTR NativeChar] twin: not built (%s)\n", why);
		NativeTwin_Free(&s_ncgTwinSource);
		memset(&s_ncgTwinSource, 0, sizeof(s_ncgTwinSource));
		s_ncgTwinHeld = 0;
		s_ncgTwinModel = NULL;
		return;
	}

	if (s_ncgTwinSource.native.state != RLDCHAR_NATIVE_READY)
	{
		const u16 *vram = NativeRenderer_VramMirror();

		if ((vram == NULL) || !NativeTwin_DecodeVram(vram, &s_ncgTwinSource))
		{
			NativeTwin_Describe(&s_ncgTwinSource, line, sizeof(line));
			Platform_Log("%s\n", line);
			s_ncgCount.refused++;
			Platform_Log("[CTR NativeChar] twin: not built (the pages could not be decoded from the VRAM mirror)\n");
			NativeCharGpu_ReleaseTwin();
			return;
		}
	}

	NativeTwin_Describe(&s_ncgTwinSource, line, sizeof(line));
	Platform_Log("%s\n", line);

	if (!NativeCharGpu_Upload(&s_ncgTwin, &s_ncgTwinSource.native, -1, s_ncgTwinSource.model, "twin", NativeTwin_TextureFlags()))
	{
		s_ncgCount.refused++;
		Platform_Log("[CTR NativeChar] twin: %s not uploaded (%s), the seat stays retail\n", s_ncgTwinSource.model, s_ncgTwin.why);
		return;
	}

	s_ncgCount.made++;
	s_ncgCount.vertexBytes += s_ncgTwin.vertexBytes;
	s_ncgCount.indexBytes += s_ncgTwin.indexBytes;
	s_ncgCount.textures += s_ncgTwin.textureCount;
	s_ncgCount.textureBytes += s_ncgTwin.textureBytes;
	Platform_Log("[CTR NativeChar] twin: %s uploaded: %u vertices x %u pose(s) (%u KB), %u triangles in %u material range(s), %u texture(s) "
	             "(%u KB, one level), no wheels (the retail wheels stay on)\n",
	             s_ncgTwinSource.model, (unsigned)s_ncgTwin.vertexCount, (unsigned)s_ncgTwin.poseCount,
	             (unsigned)((s_ncgTwin.poseCount * s_ncgTwin.vertexCount * (u32)sizeof(struct NativeProbeVertex)) / 1024u), (unsigned)s_ncgTwin.triangleCount,
	             (unsigned)s_ncgTwin.rangeCount, (unsigned)s_ncgTwin.textureCount, (unsigned)(s_ncgTwin.textureBytes / 1024u));
	NativeCharGpu_LogMaterials("twin", s_ncgTwinSource.model, &s_ncgTwin, &s_ncgTwinSource.native);
}

const struct NativeCharGpu *NativeCharGpu_ForTwin(void)
{
	if (!s_ncgTwinHeld || (s_ncgTwin.state != NATIVE_CHAR_GPU_READY) || (s_ncgTwin.key != &s_ncgTwinSource.native))
	{
		return NULL;
	}
	return &s_ncgTwin;
}

const struct NativeTwinSource *NativeCharGpu_TwinSource(void)
{
	return (NativeCharGpu_ForTwin() != NULL) ? &s_ncgTwinSource : NULL;
}

const struct Model *NativeCharGpu_TwinModel(void)
{
	return (NativeCharGpu_ForTwin() != NULL) ? s_ncgTwinModel : NULL;
}

const struct NativeCharGpu *NativeCharGpu_ForSeat(int seat)
{
	const struct NativeCharGpu *set;

	if ((seat < 0) || (seat >= NATIVE_CHAR_GPU_SETS) || (s_ncgSeatSet[seat] < 0))
	{
		return NULL;
	}

	set = &s_ncgSets[s_ncgSeatSet[seat]];
	if ((set->state != NATIVE_CHAR_GPU_READY) || (set->key != NativeChar_SeatNative(seat)))
	{
		return NULL;
	}
	return set;
}

u32 NativeCharGpu_SetsMade(void)
{
	return s_ncgCount.made + s_ncgCount.refused;
}

void NativeCharGpu_ReportLine(void)
{
	u32 live = 0;
	int i;

	for (i = 0; i < s_ncgSetCount; i++)
	{
		live += (s_ncgSets[i].state == NATIVE_CHAR_GPU_READY) ? 1u : 0u;
	}
	live += ((s_ncgPreviewEntry >= 0) && (s_ncgPreview.state == NATIVE_CHAR_GPU_READY)) ? 1u : 0u;
	live += (s_ncgTwinHeld && (s_ncgTwin.state == NATIVE_CHAR_GPU_READY)) ? 1u : 0u;

	Platform_Log("[CTR NativeChar] at exit: sets made %u, refused %u, released %u, live %u; vertex %u KB, index %u KB, textures %u (%u KB with levels); "
	             "uploads %u, uploads during a race frame %u\n",
	             (unsigned)s_ncgCount.made, (unsigned)s_ncgCount.refused, (unsigned)s_ncgCount.released, (unsigned)live,
	             (unsigned)(s_ncgCount.vertexBytes / 1024u), (unsigned)(s_ncgCount.indexBytes / 1024u), (unsigned)s_ncgCount.textures,
	             (unsigned)(s_ncgCount.textureBytes / 1024u), (unsigned)s_ncgCount.uploads, (unsigned)s_ncgCount.uploadsInRaceFrame);
}

// --- The self-test -------------------------------------------------------------
//
// --native-char-gpu-selftest <folder>: the good files of rldpack
// make-native-tests (good_probe-still, good_probe-poses,
// good_probe-wheels-hidden, good_untextured) through the game's native read
// (NativeChar_ReadNativeFile) and NativeCharGpu_Build, without a device:
//   ranges     the counts per material add up to the triangles, the order is
//              opaque then mask, ascending, back to back from index 0;
//   sort       every CNET triangle exactly once, stable inside a material, its
//              three indices unchanged;
//   vertices   the bytes of every pose (NativeCharGpu_PoseVertices) hashed;
//              one SHA-256 over all files in the verdict line (golden in ctest);
//   poses      the pose index of all 47 (animation, frame) and of the first
//              frame past each animation; a still file maps everything to 0;
//   wheels     the -X middles are the +X ones mirrored in x, and the part's
//              wheel taken as an author's (WHLS 2) gets its mirror image as
//              the -X mesh (NativeCharGpu_TestOwnWheel);
//   blend      the same part with material 0 made a blend material is refused
//              with its reason.

internal void NativeCharGpu_Expect(int *checks, int *failures, int ok, const char *name, const char *what)
{
	(*checks)++;
	if (!ok)
	{
		(*failures)++;
		printf("native char gpu selftest FAILED: %s: %s\n", name, what);
	}
}

// THE CASES MADE IN MEMORY, after the pattern of the blend case: copies of the
// sections of a checked part, rewritten -
//   three materials, 0 MASK, 1 and 2 OPAQUE, triangle t on material t % 3:
//     ranges 1, 2, 0 (opaque before mask, ascending), back to back from 0,
//     each holding exactly its triangles in CNET order with their indices,
//     only range 0 masked;
//   COL0 with a colour of its own per vertex: PoseVertices hands it out
//     unchanged (and 255 when there is none);
//   indexSize 4 (the same indices as u32): the same sorted u16 indices.
internal void NativeCharGpu_TestMade(const struct RldCharNative *n, const char *name, int *checks, int *failures)
{
	const u32 T = n->triangleCount;
	u8 *materials = (u8 *)malloc(3u * RLDCHAR_NET_MATERIAL_BYTES);
	u8 *tmat = (u8 *)malloc((size_t)T * 2u);
	u8 *colors = (u8 *)malloc((size_t)n->vertexCount * 4u);
	u8 *indices32 = (u8 *)malloc((size_t)T * 3u * 4u);
	struct NativeProbeVertex *v = (struct NativeProbeVertex *)malloc((size_t)n->vertexCount * sizeof(struct NativeProbeVertex));
	struct RldCharNative copy = *n;
	struct NativeCharGpuCpu a;
	struct NativeCharGpuCpu b;
	char what[160];
	u32 t;
	u32 i;
	int m;

	memset(&a, 0, sizeof(a));
	memset(&b, 0, sizeof(b));
	if ((materials == NULL) || (tmat == NULL) || (colors == NULL) || (indices32 == NULL) || (v == NULL) || (T < 3u))
	{
		NativeCharGpu_Expect(checks, failures, 0, name, "no memory for the cases made in memory");
		goto done;
	}

	for (m = 0; m < 3; m++)
	{
		u8 *mat = &materials[(size_t)m * RLDCHAR_NET_MATERIAL_BYTES];

		memcpy(mat, n->materials, RLDCHAR_NET_MATERIAL_BYTES);
		mat[0] = (u8)(10 + m);
		mat[6] = (m == 0) ? 1u : 0u;
	}
	for (t = 0; t < T; t++)
	{
		tmat[(size_t)t * 2u] = (u8)(t % 3u);
		tmat[((size_t)t * 2u) + 1u] = 0;
	}
	for (i = 0; i < n->vertexCount; i++)
	{
		colors[(size_t)i * 4u + 0u] = (u8)i;
		colors[(size_t)i * 4u + 1u] = (u8)(255u - i);
		colors[(size_t)i * 4u + 2u] = (u8)(i * 7u);
		colors[(size_t)i * 4u + 3u] = (u8)(128u + i);
	}
	for (i = 0; i < (T * 3u); i++)
	{
		const u32 index = NativeCharGpu_Index(n, i);

		indices32[(size_t)i * 4u + 0u] = (u8)index;
		indices32[(size_t)i * 4u + 1u] = (u8)(index >> 8);
		indices32[(size_t)i * 4u + 2u] = (u8)(index >> 16);
		indices32[(size_t)i * 4u + 3u] = (u8)(index >> 24);
	}

	copy.materials = materials;
	copy.materialCount = 3;
	copy.triangleMaterials = tmat;
	copy.colors = colors;

	if (!NativeCharGpu_Build(&copy, &a))
	{
		snprintf(what, sizeof(what), "the three-material case was refused: %s", a.why);
		NativeCharGpu_Expect(checks, failures, 0, name, what);
		goto done;
	}

	// The ranges: 1, 2, 0.
	{
		static const u16 order[3] = {1, 2, 0};
		u32 next = 0;
		int ok = (a.rangeCount == 3u);
		int k;

		for (k = 0; ok && (k < 3); k++)
		{
			const struct NativeCharRange *r = &a.range[k];
			const u32 count = (T / 3u) + (((T % 3u) > order[k]) ? 1u : 0u);

			ok &= (r->material == order[k]) && (r->firstIndex == next) && (r->indexCount == (count * 3u)) && (r->mask == ((order[k] == 0) ? 1u : 0u));
			next += r->indexCount;
		}
		NativeCharGpu_Expect(checks, failures, ok, name, "three materials: the ranges are not 1, 2, 0 back to back with mask on 0 alone");
		NativeCharGpu_Expect(checks, failures, (a.materialMask[0] == 1u) && (a.materialMask[1] == 0u) && (a.materialMask[2] == 0u) &&
		                                           (a.materialTint[1][0] == (11.0f / 255.0f)),
		                     name, "three materials: the mask flags or the tints are wrong");

		// Each range holds its triangles in CNET order, indices unchanged.
		{
			int stable = 1;

			for (k = 0; stable && (k < 3); k++)
			{
				const struct NativeCharRange *r = &a.range[k];
				u32 expect = order[k];
				u32 place;

				for (place = r->firstIndex / 3u; stable && (place < ((r->firstIndex + r->indexCount) / 3u)); place++)
				{
					int c;

					stable &= (a.triangleOrder[place] == expect);
					for (c = 0; c < 3; c++)
					{
						stable &= (a.indices[(place * 3u) + (u32)c] == (u16)NativeCharGpu_Index(n, (expect * 3u) + (u32)c));
					}
					expect += 3u;
				}
			}
			NativeCharGpu_Expect(checks, failures, stable, name, "three materials: a range does not hold its triangles in CNET order");
		}
	}

	// COL0.
	{
		int same = 1;

		NativeCharGpu_PoseVertices(&copy, 0, 0, n->vertexCount, v);
		for (i = 0; i < n->vertexCount; i++)
		{
			same &= (memcmp(v[i].color, &colors[(size_t)i * 4u], 4u) == 0);
		}
		copy.colors = NULL;
		NativeCharGpu_PoseVertices(&copy, 0, 0, 1u, v);
		same &= (v[0].color[0] == 255) && (v[0].color[1] == 255) && (v[0].color[2] == 255) && (v[0].color[3] == 255);
		copy.colors = colors;
		NativeCharGpu_Expect(checks, failures, same, name, "COL0 is not handed out unchanged, or white without it");
	}

	// indexSize 4.
	copy.indices = indices32;
	copy.indexSize = 4;
	if (!NativeCharGpu_Build(&copy, &b))
	{
		snprintf(what, sizeof(what), "the indexSize 4 case was refused: %s", b.why);
		NativeCharGpu_Expect(checks, failures, 0, name, what);
		goto done;
	}
	NativeCharGpu_Expect(checks, failures,
	                     (b.rangeCount == a.rangeCount) && (memcmp(b.indices, a.indices, (size_t)T * 3u * sizeof(u16)) == 0) &&
	                         (memcmp(b.triangleOrder, a.triangleOrder, (size_t)T * sizeof(u32)) == 0),
	                     name, "indexSize 4 does not give the same sorted u16 indices");

	printf("native char gpu selftest: %s made in memory: 3 materials (ranges %u %u %u, mask on 0), COL0, indexSize 4\n", name,
	       (unsigned)a.range[0].material, (unsigned)a.range[1].material, (unsigned)a.range[2].material);

done:
	NativeCharGpu_FreeCpu(&a);
	NativeCharGpu_FreeCpu(&b);
	free(materials);
	free(tmat);
	free(colors);
	free(indices32);
	free(v);
}

// THE OWN WHEEL MADE IN MEMORY: the part of a file with the test wheel, its
// WHLS taken as version 2 (an author's wheel). 1 when the set has the mesh as
// it is and behind it its mirror image: x negated, y, z, UV and colour kept,
// every triangle (a, b, c) as (c', b', a') turned to (a', c', b') with ' = + Nw,
// so its face normal is the original's with x negated (outside stays outside);
// the test wheel itself keeps one mesh. Prints one line.
internal int NativeCharGpu_TestOwnWheel(const struct RldCharNative *n, const struct NativeCharGpuCpu *test, const char *name)
{
	struct RldCharNative copy = *n;
	struct NativeCharGpuCpu own;
	const u32 nw = n->wheelVertexCount;
	const u32 tw = n->wheelTriangleCount;
	int ok;
	u32 i;

	ok = (test->wheelOwn == 0u) && (test->wheelVertexCount == nw) && (test->wheelIndexTotal == (tw * 3u)) && (test->wheelMirrorFirst == 0u);

	copy.wheelVersion = RLDCHAR_WHEEL_VERSION_USER;
	if (!NativeCharGpu_Build(&copy, &own))
	{
		NativeCharGpu_FreeCpu(&own);
		printf("native char gpu selftest: %s own wheel made in memory: refused (%s)\n", name, own.why);
		return 0;
	}

	ok &= (own.wheelOwn == 1u) && (own.wheelVertexCount == (2u * nw)) && (own.wheelIndexCount == (tw * 3u)) && (own.wheelIndexTotal == (tw * 6u)) &&
	      (own.wheelMirrorFirst == (tw * 3u));
	for (i = 0; ok && (i < nw); i++)
	{
		const struct NativeProbeVertex *a = &own.wheelVertices[i];
		const struct NativeProbeVertex *b = &own.wheelVertices[nw + i];

		ok &= (memcmp(a, &test->wheelVertices[i], sizeof(*a)) == 0) && (b->position[0] == -a->position[0]) && (b->position[1] == a->position[1]) &&
		      (b->position[2] == a->position[2]) && (b->texcoord[0] == a->texcoord[0]) && (b->texcoord[1] == a->texcoord[1]) &&
		      (memcmp(b->color, a->color, sizeof(a->color)) == 0);
	}
	for (i = 0; ok && (i < tw); i++)
	{
		const u16 *src = &own.wheelIndices[i * 3u];
		const u16 *dst = &own.wheelIndices[own.wheelMirrorFirst + (i * 3u)];
		double face[2][3];
		int side;

		ok &= (memcmp(src, &test->wheelIndices[i * 3u], 3u * sizeof(u16)) == 0) && (dst[0] == (u16)(src[0] + nw)) && (dst[1] == (u16)(src[2] + nw)) &&
		      (dst[2] == (u16)(src[1] + nw));
		for (side = 0; ok && (side < 2); side++)
		{
			const u16 *t = side ? dst : src;
			const float *p0 = own.wheelVertices[t[0]].position;
			const float *p1 = own.wheelVertices[t[1]].position;
			const float *p2 = own.wheelVertices[t[2]].position;
			const double e1[3] = {(double)p1[0] - p0[0], (double)p1[1] - p0[1], (double)p1[2] - p0[2]};
			const double e2[3] = {(double)p2[0] - p0[0], (double)p2[1] - p0[1], (double)p2[2] - p0[2]};

			face[side][0] = (e1[1] * e2[2]) - (e1[2] * e2[1]);
			face[side][1] = (e1[2] * e2[0]) - (e1[0] * e2[2]);
			face[side][2] = (e1[0] * e2[1]) - (e1[1] * e2[0]);
		}
		ok &= (fabs(face[1][0] + face[0][0]) <= 1e-6 * (1.0 + fabs(face[0][0]))) && (fabs(face[1][1] - face[0][1]) <= 1e-6 * (1.0 + fabs(face[0][1]))) &&
		      (fabs(face[1][2] - face[0][2]) <= 1e-6 * (1.0 + fabs(face[0][2])));
	}

	printf("native char gpu selftest: %s own wheel (WHLS 2) made in memory: %u + %u vertices, %u + %u indices, -X half %s\n", name, (unsigned)nw,
	       (unsigned)(own.wheelVertexCount - nw), (unsigned)own.wheelIndexCount, (unsigned)(own.wheelIndexTotal - own.wheelIndexCount),
	       ok ? "the mirror image" : "WRONG");
	NativeCharGpu_FreeCpu(&own);
	return ok;
}

internal void NativeCharGpu_TestFile(const char *dir, const char *name, struct Sha256 *all, int *files, int *checks, int *failures)
{
	char path[1024];
	struct RldCharNative n;
	struct NativeCharGpuCpu cpu;
	u8 *seen = NULL;
	u32 r;
	u32 t;
	u32 p;
	int a;
	int f;

	snprintf(path, sizeof(path), "%s/%s.rldchar", dir, name);
	memset(&n, 0, sizeof(n));
	if (!NativeChar_ReadNativeFile(path, name, &n))
	{
		NativeCharGpu_Expect(checks, failures, 0, name, "the file is missing or its native part is not ready");
		RldChar_FreeNative(&n);
		return;
	}
	(*files)++;

	if (!NativeCharGpu_Build(&n, &cpu))
	{
		NativeCharGpu_Expect(checks, failures, 0, name, cpu.why);
		NativeCharGpu_FreeCpu(&cpu);
		RldChar_FreeNative(&n);
		return;
	}

	// Ranges.
	{
		u32 next = 0;
		u32 sum = 0;
		int order = 1;
		int counts = 1;

		for (r = 0; r < cpu.rangeCount; r++)
		{
			const struct NativeCharRange *range = &cpu.range[r];
			u32 expected = 0;

			order &= (range->firstIndex == next) && (range->indexCount > 0u);
			if (r > 0u)
			{
				const struct NativeCharRange *before = &cpu.range[r - 1u];

				order &= (before->mask < range->mask) || ((before->mask == range->mask) && (before->material < range->material));
			}
			for (t = 0; t < n.triangleCount; t++)
			{
				expected += (Rld_ReadLE16(&n.triangleMaterials[(size_t)t * 2u]) == range->material) ? 3u : 0u;
			}
			counts &= (expected == range->indexCount) && (range->mask == cpu.materialMask[range->material]);
			next += range->indexCount;
			sum += range->indexCount;
		}
		NativeCharGpu_Expect(checks, failures, order, name, "the ranges are not back to back in the order opaque, mask, ascending");
		NativeCharGpu_Expect(checks, failures, counts, name, "a range does not hold the triangles of its material");
		NativeCharGpu_Expect(checks, failures, sum == (n.triangleCount * 3u), name, "the ranges do not add up to the triangles");
	}

	// Sort.
	seen = (u8 *)calloc(n.triangleCount, 1u);
	if (seen != NULL)
	{
		int once = 1;
		int stable = 1;
		int same = 1;

		for (r = 0; r < cpu.rangeCount; r++)
		{
			const struct NativeCharRange *range = &cpu.range[r];
			const u32 first = range->firstIndex / 3u;
			const u32 last = first + (range->indexCount / 3u);

			for (t = first; t < last; t++)
			{
				const u32 src = cpu.triangleOrder[t];
				int k;

				once &= (src < n.triangleCount) && !seen[src];
				if (src < n.triangleCount)
				{
					seen[src] = 1;
					for (k = 0; k < 3; k++)
					{
						same &= (cpu.indices[(t * 3u) + (u32)k] == (u16)NativeCharGpu_Index(&n, (src * 3u) + (u32)k));
					}
				}
				stable &= (t == first) || (cpu.triangleOrder[t - 1u] < src);
			}
		}
		for (t = 0; t < n.triangleCount; t++)
		{
			once &= seen[t];
		}
		NativeCharGpu_Expect(checks, failures, once, name, "a triangle is missing or twice");
		NativeCharGpu_Expect(checks, failures, stable, name, "the order inside a material is not the CNET order");
		NativeCharGpu_Expect(checks, failures, same, name, "the indices of a triangle changed");
		free(seen);
	}

	// Vertices of every pose.
	{
		struct NativeProbeVertex *v = (struct NativeProbeVertex *)malloc((size_t)n.vertexCount * sizeof(struct NativeProbeVertex));
		struct Sha256 one;
		u8 digest[32];

		Sha256_Init(&one);
		for (p = 0; (v != NULL) && (p < cpu.poseCount); p++)
		{
			NativeCharGpu_PoseVertices(&n, p, 0, n.vertexCount, v);
			Sha256_Update(&one, v, (size_t)n.vertexCount * sizeof(struct NativeProbeVertex));
			Sha256_Update(all, v, (size_t)n.vertexCount * sizeof(struct NativeProbeVertex));
		}
		Sha256_Final(&one, digest);
		free(v);
		printf("native char gpu selftest: %s: %u vertices x %u pose(s), %u triangles in %u range(s), %u texture(s), wheels %s, vertex sha256 "
		       "%02x%02x%02x%02x%02x%02x%02x%02x\n",
		       name, (unsigned)cpu.vertexCount, (unsigned)cpu.poseCount, (unsigned)cpu.triangleCount, (unsigned)cpu.rangeCount, (unsigned)n.textureCount,
		       cpu.hasWheels ? "yes" : "hidden", digest[0], digest[1], digest[2], digest[3], digest[4], digest[5], digest[6], digest[7]);
	}

	// Poses.
	{
		int mapped = 1;

		for (a = 0; a < RLDCHAR_ANIM_COUNT; a++)
		{
			for (f = 0; f <= (int)s_rldCharFrames[a]; f++)
			{
				const int pose = NativeCharGpu_PoseIndex(cpu.netPoseCount, a, f);

				if (cpu.netPoseCount == 0u)
				{
					mapped &= (pose == 0);
				}
				else if (f == (int)s_rldCharFrames[a])
				{
					mapped &= (pose == -1);
				}
				else
				{
					mapped &= (pose == ((int)s_ncgPoseBase[a] + f)) && (pose >= 0) && (pose < (int)NATIVE_CHAR_GPU_POSES);
				}
			}
		}
		mapped &= (cpu.netPoseCount == 0u) || ((NativeCharGpu_PoseIndex(cpu.netPoseCount, 3, 3) == 46) && (NativeCharGpu_PoseIndex(cpu.netPoseCount, -1, 0) == -1) &&
		                                       (NativeCharGpu_PoseIndex(cpu.netPoseCount, 4, 0) == -1));
		NativeCharGpu_Expect(checks, failures, mapped, name, "the pose index of an animation frame is wrong");
	}

	// Wheels.
	if (cpu.hasWheels)
	{
		double m[4][3];
		int w;
		int mirrored = 1;

		for (w = 0; w < 4; w++)
		{
			NativeCharGpu_WheelMiddle(cpu.wheelFront, cpu.wheelRear, w, m[w]);
		}
		mirrored &= (m[0][0] == (double)cpu.wheelFront[0]) && (m[1][0] == -(double)cpu.wheelFront[0]) && (m[1][1] == m[0][1]) && (m[1][2] == m[0][2]);
		mirrored &= (m[2][0] == (double)cpu.wheelRear[0]) && (m[3][0] == -(double)cpu.wheelRear[0]) && (m[3][1] == m[2][1]) && (m[3][2] == m[2][2]);
		mirrored &= (cpu.wheelFront[0] > 0.0f) && (cpu.wheelRear[0] > 0.0f) && (cpu.wheelIndexCount == (n.wheelTriangleCount * 3u));
		// The same check holds the own wheel (WHLS 2) made from this part: the
		// mesh, then its mirror image (NativeCharGpu_TestOwnWheel).
		mirrored &= NativeCharGpu_TestOwnWheel(&n, &cpu, name);
		NativeCharGpu_Expect(checks, failures, mirrored, name, "the wheel middles are not +X and the -X ones mirrored, or an own wheel (WHLS 2) is not mirrored");
	}

	// The cases the files of the set do not have, made in memory from this
	// part (only once, for the first file): several ranges, MASK, COL0 and
	// indexSize 4.
	if (*files == 1)
	{
		NativeCharGpu_TestMade(&n, name, checks, failures);
	}

	// Blend: material 0 as a blend material refuses the set.
	{
		u8 *materials = (u8 *)malloc((size_t)n.materialCount * RLDCHAR_NET_MATERIAL_BYTES);
		struct RldCharNative copy = n;
		struct NativeCharGpuCpu refused;

		if (materials != NULL)
		{
			memcpy(materials, n.materials, (size_t)n.materialCount * RLDCHAR_NET_MATERIAL_BYTES);
			materials[6] = 2u;
			copy.materials = materials;
			NativeCharGpu_Expect(checks, failures, !NativeCharGpu_Build(&copy, &refused) && (strcmp(refused.why, "blend material 0 is not drawn natively yet") == 0),
			                     name, "a blend material was not refused");
			NativeCharGpu_FreeCpu(&refused);
			free(materials);
		}
	}

	NativeCharGpu_FreeCpu(&cpu);
	RldChar_FreeNative(&n);
}

// THE FILES OF WHLS VERSION 3 AND MRPH (render plan A1, A4, B3), a second
// line after the one above: the good files rldpack make-native-tests writes for
// them, through the native read and NativeCharGpu_Build -
//   good_mrph-win-lose     win and lose get 8 stages behind the poses; stage
//                          s of each is RldChar_MorphVertex at (s + 1) / 8
//   good_mrph-lose-only    lose alone, right behind the poses
//   good_mrph-version-2... skipped section: no stages
//   good_wheel-v3-rear     an own rear mesh behind the front one: its range,
//                          its mirror, indices of its own points, its material
//   good_wheel-v3-flags    ALWAYS_DRAW as the file says, the front mesh behind
//   good_wheel-v2-textured as before: no rear mesh, not always drawn
// Returns the failures.
internal int NativeCharGpu_TestFormatFiles(const char *dir, int *files, int *checks)
{
	static const char *const names[] = {"good_mrph-win-lose", "good_mrph-lose-only", "good_mrph-version-2-skipped",
	                                    "good_wheel-v3-rear", "good_wheel-v3-flags", "good_wheel-v2-textured"};
	static struct NativeProbeVertex stage[64];
	int failures = 0;
	size_t f;

	for (f = 0; f < (sizeof(names) / sizeof(names[0])); f++)
	{
		const char *name = names[f];
		char path[1024];
		struct RldCharNative n;
		struct NativeCharGpuCpu cpu;
		u32 targets = 0;
		u32 first[NATIVE_CHAR_GPU_FINISH_TARGETS];
		u32 total = 0;

		snprintf(path, sizeof(path), "%s/%s.rldchar", dir, name);
		memset(&n, 0, sizeof(n));
		memset(&cpu, 0, sizeof(cpu));
		if (!NativeChar_ReadNativeFile(path, name, &n) || !NativeCharGpu_Build(&n, &cpu))
		{
			NativeCharGpu_Expect(checks, &failures, 0, name, "the file is missing, its native part is not ready or the set is refused");
			NativeCharGpu_FreeCpu(&cpu);
			RldChar_FreeNative(&n);
			continue;
		}
		(*files)++;
		NativeCharGpu_FinishLayout(&n, cpu.poseCount, 1, &targets, first, &total);

		if (f == 0u)
		{
			int target;
			int ok = (targets == 3u) && (first[0] == cpu.poseCount) && (first[1] == (cpu.poseCount + NATIVE_CHAR_GPU_FINISH_STAGES)) &&
			         (total == (cpu.poseCount + (2u * NATIVE_CHAR_GPU_FINISH_STAGES))) && (cpu.poseTotal == total);

			for (target = 0; ok && (target < NATIVE_CHAR_GPU_FINISH_TARGETS); target++)
			{
				int s;

				for (s = 0; ok && (s < NATIVE_CHAR_GPU_FINISH_STAGES); s++)
				{
					const u32 count = (n.vertexCount < 64u) ? n.vertexCount : 64u;
					u32 v;

					NativeCharGpu_BufferVertices(&n, cpu.poseCount, first, targets, first[target] + (u32)s, 0u, count, stage);
					for (v = 0; ok && (v < count); v++)
					{
						float position[3];
						float normal[3];

						RldChar_MorphVertex(&n, target, (float)(s + 1) / (float)NATIVE_CHAR_GPU_FINISH_STAGES, v, position, normal);
						ok = (memcmp(stage[v].position, position, sizeof(position)) == 0);
					}
				}
			}
			NativeCharGpu_Expect(checks, &failures, ok, name, "the win and lose stages are not the blends of the file");
		}
		else if (f == 1u)
		{
			NativeCharGpu_Expect(checks, &failures,
			                     (targets == (1u << NATIVE_CHAR_GPU_FINISH_LOSE)) && (first[NATIVE_CHAR_GPU_FINISH_LOSE] == cpu.poseCount) &&
			                         (total == (cpu.poseCount + NATIVE_CHAR_GPU_FINISH_STAGES)),
			                     name, "lose alone is not right behind the poses");
		}
		else if (f == 2u)
		{
			NativeCharGpu_Expect(checks, &failures, (targets == 0u) && (total == cpu.poseCount), name, "a skipped MRPH got stages");
		}
		else if (f == 3u)
		{
			int ok = cpu.hasWheels && cpu.wheelOwn && cpu.wheelRearOwn && (cpu.wheelRearFirst == (cpu.wheelIndexCount * 2u)) &&
			         (cpu.wheelRearMirrorFirst == (cpu.wheelRearFirst + cpu.wheelRearIndexCount)) &&
			         (cpu.wheelIndexTotal == (cpu.wheelRearMirrorFirst + cpu.wheelRearIndexCount)) && (cpu.wheelRearMaterial < cpu.materialCount) &&
			         (cpu.wheelRearTreads >= NATIVE_WHEELS_TREADS_MIN) && (cpu.wheelAlways == (((n.wheelFlags & RLDCHAR_WHEEL_ALWAYS_DRAW) != 0u) ? 1u : 0u));
			const u32 frontPoints = n.wheelVertexCount * 2u;
			u32 i;

			for (i = cpu.wheelRearFirst; ok && (i < cpu.wheelIndexTotal); i++)
			{
				ok = (cpu.wheelIndices[i] >= frontPoints) && (cpu.wheelIndices[i] < cpu.wheelVertexCount);
			}
			NativeCharGpu_Expect(checks, &failures, ok, name, "the rear mesh is not behind the front one with its own points and material");
		}
		else if (f == 4u)
		{
			NativeCharGpu_Expect(checks, &failures,
			                     cpu.wheelOwn && (cpu.wheelAlways == (((n.wheelFlags & RLDCHAR_WHEEL_ALWAYS_DRAW) != 0u) ? 1u : 0u)) &&
			                         (cpu.wheelRearOwn == (((n.wheelFlags & RLDCHAR_WHEEL_REAR_MESH) != 0u) ? 1u : 0u)),
			                     name, "the flags of WHLS version 3 are not the ones of the file");
		}
		else
		{
			NativeCharGpu_Expect(checks, &failures, cpu.wheelOwn && !cpu.wheelRearOwn && !cpu.wheelAlways && (targets == 0u), name,
			                     "a version 2 wheel got a rear mesh, ALWAYS_DRAW or stages");
		}
		NativeCharGpu_FreeCpu(&cpu);
		RldChar_FreeNative(&n);
	}
	return failures;
}

int NativeCharGpu_SelfTest(const char *dir)
{
	static const char *const names[] = {"good_probe-still", "good_probe-poses", "good_probe-wheels-hidden", "good_untextured"};
	struct Sha256 all;
	u8 digest[32];
	int files = 0;
	int checks = 0;
	int failures = 0;
	size_t i;

	Sha256_Init(&all);
	for (i = 0; i < (sizeof(names) / sizeof(names[0])); i++)
	{
		NativeCharGpu_TestFile(dir, names[i], &all, &files, &checks, &failures);
	}
	Sha256_Final(&all, digest);

	printf("native char gpu selftest %s: %d files, %d checks, %d failures, sha256 ", (failures == 0) ? "passed" : "FAILED", files, checks, failures);
	for (i = 0; i < 32u; i++)
	{
		printf("%02x", digest[i]);
	}
	printf("\n");

	// WHLS version 3 and MRPH, a line of their own after the one of before.
	{
		int formatFiles = 0;
		int formatChecks = 0;
		const int formatFailures = NativeCharGpu_TestFormatFiles(dir, &formatFiles, &formatChecks);

		printf("native char gpu selftest wheels v3 and win lose %s: %d files, %d checks, %d failures\n", (formatFailures == 0) ? "passed" : "FAILED",
		       formatFiles, formatChecks, formatFailures);
		failures += formatFailures;
	}

	return (failures == 0) && (files == (int)(sizeof(names) / sizeof(names[0]))) ? 0 : 1;
}
