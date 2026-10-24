// rldhires_morph.inc - the shape keys (glTF morph targets) of a hi-res driver,
// once, for the game, rldpack and Reload Studio (round "Dummy Glider" phase 1b,
// 2026-10-24).
//
// AN INCLUDE, NOT A MODULE. Header-only and pure: no device, no game state, no
// log, no file. include/rldhires.inc includes it (its checker counts with the
// same code as the engine loads), so every file that includes rldhires.inc has
// it. Two halves, as include/rldhires_anim.inc:
//
//   THE RUNTIME HALF (no cgltf): a SET (the keys of one file: their names and
//   their deltas, copied out of the file) and a STATE (the weights one driver is
//   drawn with: a key layer and a key chain), the weights of a state, and the
//   blend of a set's vertices with those weights.
//
//   THE BUILD HALF (needs cgltf.h before this file): RldMorph_BuildBegin,
//   RldMorph_BuildPrimitive per drawn primitive in the engine's vertex order,
//   RldMorph_BuildEnd.
//
// WHAT A KEY IS. A glTF primitive holds morph targets (POSITION and optional
// NORMAL deltas; TANGENT is not read); a mesh names them in extras.targetNames
// (cgltf: mesh->target_names; Blender writes the shape key names there). A KEY
// of the driver is a NAME: every target of that name in any drawn primitive
// belongs to it, so one weight moves every mesh that has the key. The Basis is
// the mesh itself (no key). The file's format is not touched: the GLB of the
// CHRG chunk is stored as exported, the deltas are read from it as stored.
//
// THE BLEND (glTF's rule): p = p0 + sum over the keys of weight_k x dp_k, and
// n = n0 + sum of weight_k x dn_k (normalised later by the skinning), in key
// order, before the skinning; a vertex no key moves is not touched. Weights are
// 0..1 per key. A delta that is exactly zero is not stored.
//
// THE WEIGHTS OF A STATE: the KEY LAYER (one weight per key, set by name) plus
// the KEY CHAIN, each final weight clamped to 0..1. The chain is an ordered
// list of keys k_1 .. k_n after the Basis k_0 (k_0 is no key); its position t
// runs 0..n; at t = i + f (0 <= f < 1) only the two neighbours are mixed: k_i
// with 1 - f (nothing when i = 0, the Basis) and k_(i+1) with f; at t = n, k_n
// with 1. So folded (Basis) -> 33 -> 66 -> open is the chain
// {wings_fold_33, wings_fold_66, wings_open}, and the single keys (flap, dive,
// pull_up: deltas stored on top of the last chain key) go into the key layer
// and add on top. The chain moves in whole steps: frame 0..frames, t = n x
// frame / frames - so a motion over N frames ends exactly at n.
//
// LIMITS (what is left out is counted, the checker's note HRG-23 says it):
// RLDMORPH_KEYS_MAX distinct names (later names are left out), names up to
// RLDMORPH_NAME_MAX - 1 bytes (a longer or missing name: the target is left
// out), RLDMORPH_BYTES_MAX bytes of kept deltas (keys in their order; a key that
// would pass it is left out whole).
//
// u8, u32 come from the including file (the game: include/macros.h).

#ifndef RLDHIRES_MORPH_INC
#define RLDHIRES_MORPH_INC

#include <math.h>
#include <stdlib.h>
#include <string.h>

//========================================================================================
// THE RUNTIME HALF
//========================================================================================

#define RLDMORPH_KEYS_MAX 32u
#define RLDMORPH_NAME_MAX 64u
#define RLDMORPH_BYTES_MAX (4u * 1024u * 1024u)
#define RLDMORPH_CHAIN_MAX 16
#define RLDMORPH_FRAMES_MAX 100000
// The bytes one kept delta costs at most: its entry (u32 vertex slot + 6
// floats) and, when its vertex is new, the vertex (u32 index + 6 base floats).
#define RLDMORPH_ENTRY_BYTES 28u
#define RLDMORPH_VERTEX_BYTES 28u

typedef struct
{
	u32 keyCount;
	char name[RLDMORPH_KEYS_MAX][RLDMORPH_NAME_MAX];
	u32 normalMask; // bit k: key k has NORMAL deltas
	// THE VERTICES any kept key moves, ascending: their index in the driver's
	// vertex array and their base position and normal (as the file has them).
	u32 vertCount;
	u32 *vert;
	float *baseP; // vertCount x 3
	float *baseN; // vertCount x 3 (0 0 0 for a vertex without a normal)
	// PER KEY its deltas: entries[k] of them, ascending by vertex; at[k][e] the
	// slot in vert, delta[k][e * 6 ..] dp xyz then dn xyz.
	u32 entries[RLDMORPH_KEYS_MAX];
	u32 *at[RLDMORPH_KEYS_MAX];
	float *delta[RLDMORPH_KEYS_MAX];
	void *block; // one allocation for all of the above
	u32 bytes;
	// WHAT WAS READ AND WHAT WAS LEFT OUT (the checker's note).
	u32 targets;      // targets met in the primitives read
	u32 unnamed;      // targets without a name, or with one of RLDMORPH_NAME_MAX bytes or more
	u32 overKeys;     // targets whose name came after RLDMORPH_KEYS_MAX others
	u32 overBudget;   // keys left out whole for RLDMORPH_BYTES_MAX
	u32 badAccessor;  // target attributes not float VEC3 of the primitive's vertex count (left out)
	u32 kept;         // deltas kept (all keys)
} RldMorphSet;

typedef struct
{
	float key[RLDMORPH_KEYS_MAX]; // the key layer, 0..1 each
	int chain[RLDMORPH_CHAIN_MAX];  // key indices k_1 .. k_n
	int chainCount;                 // n, 0 = no chain
	int frames;                     // N >= 1 (the chain's 0..n over N frames)
	int frame;                      // 0..N
	int dir;                        // +1 forwards, -1 backwards, 0 hold
} RldMorphState;

static void RldMorph_Free(RldMorphSet *set)
{
	free(set->block);
	memset(set, 0, sizeof(*set));
}

// The key of a name, -1 none (also NULL).
static int RldMorph_Find(const RldMorphSet *set, const char *name)
{
	u32 k;

	if (name == NULL)
	{
		return -1;
	}
	for (k = 0; k < set->keyCount; k++)
	{
		if (strcmp(set->name[k], name) == 0)
		{
			return (int)k;
		}
	}
	return -1;
}

static void RldMorph_StateReset(RldMorphState *st)
{
	memset(st, 0, sizeof(*st));
	st->frames = 1;
}

// The chain's weights at position pos (0..count) added to w (indexed by key).
static void RldMorph_ChainWeightsAdd(const int *chain, int count, float pos, float *w)
{
	int i;
	float f;

	if ((count <= 0) || !(pos > 0.0f))
	{
		return;
	}
	if (pos >= (float)count)
	{
		w[chain[count - 1]] += 1.0f;
		return;
	}
	i = (int)floorf(pos);
	f = pos - (float)i;
	if (i > 0)
	{
		w[chain[i - 1]] += 1.0f - f;
	}
	w[chain[i]] += f;
}

// The chain's position 0..n (0 without a chain).
static float RldMorph_ChainPos(const RldMorphState *st)
{
	if (st->chainCount <= 0)
	{
		return 0.0f;
	}
	return (float)((double)st->chainCount * (double)st->frame / (double)st->frames);
}

// A chain of count keys (indices < keyCount) over frames frames. 1 = set; 0 =
// refused, nothing changed. The same keys again keep the position (rounded to
// the new frames); other keys start at the Basis. The direction is 0 (hold).
static int RldMorph_ChainSet(RldMorphState *st, const int *keys, int count, int frames, u32 keyCount)
{
	int same;
	int k;

	if ((keys == NULL) || (count < 1) || (count > RLDMORPH_CHAIN_MAX) || (frames < 1) || (frames > RLDMORPH_FRAMES_MAX))
	{
		return 0;
	}
	for (k = 0; k < count; k++)
	{
		if ((keys[k] < 0) || ((u32)keys[k] >= keyCount) || ((u32)keys[k] >= RLDMORPH_KEYS_MAX))
		{
			return 0;
		}
	}
	same = (count == st->chainCount);
	for (k = 0; same && (k < count); k++)
	{
		same = (keys[k] == st->chain[k]);
	}
	if (same)
	{
		const double pos = (double)st->chainCount * (double)st->frame / (double)st->frames;

		st->frame = (int)floor(pos / (double)count * (double)frames + 0.5);
	}
	else
	{
		st->frame = 0;
	}
	for (k = 0; k < count; k++)
	{
		st->chain[k] = keys[k];
	}
	st->chainCount = count;
	st->frames = frames;
	st->frame = (st->frame < 0) ? 0 : ((st->frame > frames) ? frames : st->frame);
	st->dir = 0;
	return 1;
}

// The position set at once (clamped to 0..n, rounded to a whole frame). 0 = no chain.
static int RldMorph_ChainSetPos(RldMorphState *st, float pos)
{
	double f;

	if (st->chainCount <= 0)
	{
		return 0;
	}
	f = (double)pos / (double)st->chainCount * (double)st->frames;
	f = (f > 0.0) ? floor(f + 0.5) : 0.0;
	st->frame = (f >= (double)st->frames) ? st->frames : (int)f;
	return 1;
}

// +1 forwards (towards k_n), -1 backwards (towards the Basis), 0 hold. 0 = no chain.
static int RldMorph_ChainPlay(RldMorphState *st, int dir)
{
	if (st->chainCount <= 0)
	{
		return 0;
	}
	st->dir = (dir > 0) ? 1 : ((dir < 0) ? -1 : 0);
	return 1;
}

// One frame of the chain's motion (the caller's clock: one call per frame).
static void RldMorph_ChainStep(RldMorphState *st)
{
	if ((st->chainCount <= 0) || (st->dir == 0))
	{
		return;
	}
	st->frame += st->dir;
	st->frame = (st->frame < 0) ? 0 : ((st->frame > st->frames) ? st->frames : st->frame);
}

// 1 = the chain stands at the end of the direction played (or holds, or there is none).
static int RldMorph_ChainDone(const RldMorphState *st)
{
	if ((st->chainCount <= 0) || (st->dir == 0))
	{
		return 1;
	}
	return (st->dir > 0) ? (st->frame >= st->frames) : (st->frame <= 0);
}

// The final weights of a state for keyCount keys into out (key layer + chain,
// each clamped to 0..1). Returns how many are not 0 (0 = draw the Basis).
static int RldMorph_Weights(const RldMorphState *st, u32 keyCount, float *out)
{
	int nonzero = 0;
	u32 k;

	keyCount = (keyCount > RLDMORPH_KEYS_MAX) ? RLDMORPH_KEYS_MAX : keyCount;
	for (k = 0; k < keyCount; k++)
	{
		out[k] = st->key[k];
	}
	if ((st->chainCount > 0) && (keyCount > 0u))
	{
		RldMorph_ChainWeightsAdd(st->chain, st->chainCount, RldMorph_ChainPos(st), out);
	}
	for (k = 0; k < keyCount; k++)
	{
		const float w = out[k];

		out[k] = (w > 0.0f) ? ((w < 1.0f) ? w : 1.0f) : 0.0f; // NaN -> 0
		nonzero += (out[k] != 0.0f) ? 1 : 0;
	}
	return nonzero;
}

// The set's vertices blended with the weights w (set->keyCount of them) into
// outP and outN (set->vertCount x 3 each): base + w_k x delta_k in key order.
static void RldMorph_Blend(const RldMorphSet *set, const float *w, float *outP, float *outN)
{
	u32 k;
	u32 e;

	memcpy(outP, set->baseP, (size_t)set->vertCount * 3u * sizeof(float));
	memcpy(outN, set->baseN, (size_t)set->vertCount * 3u * sizeof(float));
	for (k = 0; k < set->keyCount; k++)
	{
		const float wk = w[k];
		const int normals = ((set->normalMask >> k) & 1u) != 0u;

		if (wk == 0.0f)
		{
			continue;
		}
		for (e = 0; e < set->entries[k]; e++)
		{
			const u32 u = set->at[k][e];
			const float *d = &set->delta[k][(size_t)e * 6u];

			outP[u * 3u + 0u] += wk * d[0];
			outP[u * 3u + 1u] += wk * d[1];
			outP[u * 3u + 2u] += wk * d[2];
			if (normals)
			{
				outN[u * 3u + 0u] += wk * d[3];
				outN[u * 3u + 1u] += wk * d[4];
				outN[u * 3u + 2u] += wk * d[5];
			}
		}
	}
}

// The box every blend of the set can reach, per vertex of the set (weights 0..1
// each, any combination): lo = base + sum of the negative deltas, hi = base +
// sum of the positive ones, per axis. Calls grow(user, vertex, lo, hi) once per
// vertex of the set (vertex = its index in the driver's array).
static void RldMorph_Reach(const RldMorphSet *set, void (*grow)(void *user, u32 vertex, const float lo[3], const float hi[3]), void *user)
{
	u32 u;

	for (u = 0; u < set->vertCount; u++)
	{
		float lo[3];
		float hi[3];
		u32 k;
		int a;

		for (a = 0; a < 3; a++)
		{
			lo[a] = hi[a] = set->baseP[u * 3u + (u32)a];
		}
		for (k = 0; k < set->keyCount; k++)
		{
			// The entries of a key ascend by slot: a binary search for u.
			u32 first = 0;
			u32 last = set->entries[k];

			while (first < last)
			{
				const u32 mid = first + (last - first) / 2u;

				if (set->at[k][mid] < u)
				{
					first = mid + 1u;
				}
				else
				{
					last = mid;
				}
			}
			for (; (first < set->entries[k]) && (set->at[k][first] == u); first++)
			{
				const float *d = &set->delta[k][(size_t)first * 6u];

				for (a = 0; a < 3; a++)
				{
					if (d[a] < 0.0f)
					{
						lo[a] += d[a];
					}
					else
					{
						hi[a] += d[a];
					}
				}
			}
		}
		grow(user, set->vert[u], lo, hi);
	}
}

//========================================================================================
// THE BUILD HALF (cgltf.h before this file)
//========================================================================================

#ifdef CGLTF_H_INCLUDED__

typedef struct
{
	u32 vertex;
	u32 key;
	u32 seq;
	float d[6];
	float bp[3];
	float bn[3];
} RldMorphEntry;

typedef struct
{
	RldMorphSet *set;
	RldMorphEntry *e;
	u32 count;
	u32 cap;
	int fail;                         // out of memory
	u32 keyEntries[RLDMORPH_KEYS_MAX]; // deltas met per key
	u8 keyOver[RLDMORPH_KEYS_MAX];     // the key met more deltas than the budget can hold
} RldMorphBuild;

static void RldMorph_BuildBegin(RldMorphBuild *b, RldMorphSet *set)
{
	memset(b, 0, sizeof(*b));
	memset(set, 0, sizeof(*set));
	b->set = set;
}

// The targets of one drawn primitive whose vertices start at `first` in the
// driver's vertex array (the engine's order: EngHc_BuildCpu). A primitive that
// is not triangles or has no float VEC3 POSITION adds nothing (the engine draws
// none of it either).
static void RldMorph_BuildPrimitive(RldMorphBuild *b, const cgltf_mesh *mesh, const cgltf_primitive *prim, u32 first)
{
	RldMorphSet *set = b->set;
	const cgltf_accessor *pos = cgltf_find_accessor(prim, cgltf_attribute_type_position, 0);
	const cgltf_accessor *nrm = cgltf_find_accessor(prim, cgltf_attribute_type_normal, 0);
	const u32 maxEntries = RLDMORPH_BYTES_MAX / RLDMORPH_ENTRY_BYTES;
	float *dp = NULL;
	float *dn = NULL;
	cgltf_size t;
	u32 count;

	if (b->fail || (prim->targets_count == 0u) || (pos == NULL) || (prim->type != cgltf_primitive_type_triangles) || (pos->type != cgltf_type_vec3) ||
	    (pos->component_type != cgltf_component_type_r_32f))
	{
		return;
	}
	count = (u32)pos->count;
	dp = (float *)malloc((size_t)((count > 0u) ? count : 1u) * 3u * sizeof(float));
	dn = (float *)malloc((size_t)((count > 0u) ? count : 1u) * 3u * sizeof(float));
	if ((dp == NULL) || (dn == NULL))
	{
		free(dp);
		free(dn);
		b->fail = 1;
		return;
	}
	for (t = 0; t < prim->targets_count; t++)
	{
		const cgltf_morph_target *target = &prim->targets[t];
		const char *name = ((mesh != NULL) && (t < mesh->target_names_count)) ? mesh->target_names[t] : NULL;
		int haveP = 0;
		int haveN = 0;
		int key;
		cgltf_size a;
		u32 v;

		set->targets++;
		if ((name == NULL) || (name[0] == '\0') || (strlen(name) >= RLDMORPH_NAME_MAX))
		{
			set->unnamed++;
			continue;
		}
		key = RldMorph_Find(set, name);
		if (key < 0)
		{
			if (set->keyCount >= RLDMORPH_KEYS_MAX)
			{
				set->overKeys++;
				continue;
			}
			key = (int)set->keyCount++;
			memcpy(set->name[key], name, strlen(name) + 1u);
		}
		for (a = 0; a < target->attributes_count; a++)
		{
			const cgltf_attribute *at = &target->attributes[a];
			const int isP = (at->type == cgltf_attribute_type_position) && (at->index == 0);
			const int isN = (at->type == cgltf_attribute_type_normal) && (at->index == 0);

			if (!isP && !isN)
			{
				continue; // TANGENT and others: not read
			}
			if ((at->data == NULL) || (at->data->count != (cgltf_size)count) || (at->data->type != cgltf_type_vec3) ||
			    (cgltf_accessor_unpack_floats(at->data, isP ? dp : dn, (cgltf_size)count * 3u) != (cgltf_size)count * 3u))
			{
				set->badAccessor++;
				continue;
			}
			haveP |= isP;
			haveN |= isN;
		}
		if (!haveP)
		{
			memset(dp, 0, (size_t)count * 3u * sizeof(float));
		}
		if (!haveN)
		{
			memset(dn, 0, (size_t)count * 3u * sizeof(float));
		}
		if (haveN)
		{
			set->normalMask |= 1u << (u32)key;
		}
		for (v = 0; v < count; v++)
		{
			const float *p = &dp[v * 3u];
			const float *n = &dn[v * 3u];
			RldMorphEntry *e;

			if ((p[0] == 0.0f) && (p[1] == 0.0f) && (p[2] == 0.0f) && (n[0] == 0.0f) && (n[1] == 0.0f) && (n[2] == 0.0f))
			{
				continue;
			}
			if (b->keyOver[key])
			{
				continue;
			}
			if (b->keyEntries[key] >= maxEntries)
			{
				b->keyOver[key] = 1; // left out whole at the end
				continue;
			}
			if (b->count >= b->cap)
			{
				const u32 cap = (b->cap != 0u) ? (b->cap * 2u) : 1024u;
				RldMorphEntry *grown = (RldMorphEntry *)realloc(b->e, (size_t)cap * sizeof(RldMorphEntry));

				if (grown == NULL)
				{
					b->fail = 1;
					free(dp);
					free(dn);
					return;
				}
				b->e = grown;
				b->cap = cap;
			}
			e = &b->e[b->count];
			memset(e, 0, sizeof(*e));
			e->vertex = first + v;
			e->key = (u32)key;
			e->seq = b->count;
			memcpy(e->d, p, 3u * sizeof(float));
			memcpy(&e->d[3], n, 3u * sizeof(float));
			cgltf_accessor_read_float(pos, v, e->bp, 3);
			if ((nrm != NULL) && (nrm->count == pos->count))
			{
				(void)cgltf_accessor_read_float(nrm, v, e->bn, 3);
			}
			b->count++;
			b->keyEntries[key]++;
		}
	}
	free(dp);
	free(dn);
}

static int RldMorph_EntryOrder(const void *pa, const void *pb)
{
	const RldMorphEntry *a = (const RldMorphEntry *)pa;
	const RldMorphEntry *b = (const RldMorphEntry *)pb;

	if (a->vertex != b->vertex)
	{
		return (a->vertex < b->vertex) ? -1 : 1;
	}
	return (a->seq < b->seq) ? -1 : ((a->seq > b->seq) ? 1 : 0);
}

// The set made from what was read. 1 = made (also with no key); 0 = out of
// memory (the set is empty: the driver draws as without keys).
static int RldMorph_BuildEnd(RldMorphBuild *b)
{
	RldMorphSet *set = b->set;
	int newIndex[RLDMORPH_KEYS_MAX];
	char names[RLDMORPH_KEYS_MAX][RLDMORPH_NAME_MAX];
	u32 normals = 0;
	u32 keys = 0;
	u32 budget = 0;
	u32 kept = 0;
	u32 verts = 0;
	u32 k;
	u32 i;
	size_t bytes;
	u8 *block;

	if (b->fail)
	{
		free(b->e);
		b->e = NULL;
		{
			RldMorphSet keep = *set;

			memset(set, 0, sizeof(*set));
			set->targets = keep.targets;
		}
		return 0;
	}
	// Which keys stay: in their order, while the budget holds.
	for (k = 0; k < set->keyCount; k++)
	{
		const u32 cost = b->keyEntries[k] * (RLDMORPH_ENTRY_BYTES + RLDMORPH_VERTEX_BYTES);

		if (b->keyOver[k] || ((budget + cost) > RLDMORPH_BYTES_MAX))
		{
			newIndex[k] = -1;
			set->overBudget++;
			continue;
		}
		budget += cost;
		newIndex[k] = (int)keys;
		memcpy(names[keys], set->name[k], RLDMORPH_NAME_MAX);
		normals |= ((set->normalMask >> k) & 1u) << keys;
		keys++;
	}
	// The kept deltas, by vertex (then as read), and the vertices they move.
	for (i = 0; i < b->count; i++)
	{
		if (newIndex[b->e[i].key] >= 0)
		{
			b->e[kept] = b->e[i];
			b->e[kept].key = (u32)newIndex[b->e[i].key];
			kept++;
		}
	}
	if (kept > 1u)
	{
		qsort(b->e, kept, sizeof(RldMorphEntry), RldMorph_EntryOrder);
	}
	for (i = 0; i < kept; i++)
	{
		verts += ((i == 0u) || (b->e[i].vertex != b->e[i - 1u].vertex)) ? 1u : 0u;
	}
	memset(set->name, 0, sizeof(set->name));
	for (k = 0; k < keys; k++)
	{
		memcpy(set->name[k], names[k], RLDMORPH_NAME_MAX);
	}
	set->keyCount = keys;
	set->normalMask = normals;
	set->kept = kept;
	memset(set->entries, 0, sizeof(set->entries));
	for (i = 0; i < kept; i++)
	{
		set->entries[b->e[i].key]++;
	}
	if (kept == 0u)
	{
		free(b->e);
		b->e = NULL;
		return 1;
	}
	bytes = (size_t)verts * RLDMORPH_VERTEX_BYTES + (size_t)kept * RLDMORPH_ENTRY_BYTES;
	block = (u8 *)malloc(bytes);
	if (block == NULL)
	{
		free(b->e);
		b->e = NULL;
		set->keyCount = 0;
		set->kept = 0;
		memset(set->entries, 0, sizeof(set->entries));
		return 0;
	}
	set->block = block;
	set->bytes = (u32)bytes;
	set->vertCount = verts;
	// floats first (4-byte aligned like the u32s), then the u32 arrays
	set->baseP = (float *)block;
	set->baseN = set->baseP + (size_t)verts * 3u;
	{
		float *d = set->baseN + (size_t)verts * 3u;
		u32 *u;

		for (k = 0; k < keys; k++)
		{
			set->delta[k] = d;
			d += (size_t)set->entries[k] * 6u;
		}
		u = (u32 *)d;
		set->vert = u;
		u += verts;
		for (k = 0; k < keys; k++)
		{
			set->at[k] = u;
			u += set->entries[k];
		}
	}
	{
		u32 fill[RLDMORPH_KEYS_MAX];
		u32 slot = 0;

		memset(fill, 0, sizeof(fill));
		for (i = 0; i < kept; i++)
		{
			const RldMorphEntry *e = &b->e[i];
			const u32 key = e->key;

			if ((i == 0u) || (e->vertex != b->e[i - 1u].vertex))
			{
				slot = (i == 0u) ? 0u : (slot + 1u);
				set->vert[slot] = e->vertex;
				memcpy(&set->baseP[slot * 3u], e->bp, 3u * sizeof(float));
				memcpy(&set->baseN[slot * 3u], e->bn, 3u * sizeof(float));
			}
			set->at[key][fill[key]] = slot;
			memcpy(&set->delta[key][(size_t)fill[key] * 6u], e->d, 6u * sizeof(float));
			fill[key]++;
		}
	}
	free(b->e);
	b->e = NULL;
	return 1;
}

#endif // CGLTF_H_INCLUDED__

#endif // RLDHIRES_MORPH_INC
