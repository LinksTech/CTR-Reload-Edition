// rldhires_anim.inc - the pose maths of a hi-res driver, once, for the game and
// Reload Studio (round of 2026-10-17; format: the hi-res format notes of the
// project notes, the chunk itself in include/rldhires.inc)
//
// AN INCLUDE, NOT A MODULE. Header-only and pure: no device, no game state, no
// log, no file. The game includes it in platform/engine_hires_char.c (unity
// build, main.c), Reload Studio may include it for its preview. Two halves:
//
//   THE RUNTIME HALF (no cgltf): a skeleton (nodes with parents and their rest
//   transforms, the joints of the skin with their inverse bind matrices) and
//   clips (channels of keys: times and values, copied out of the file), the
//   sampling of a clip at a time (LINEAR / STEP; CUBICSPLINE read linearly
//   between its values, the format's rule), the blend of two poses, the node
//   matrices of a pose, the joint matrices and the skinning of one vertex.
//   After the build half has run, the GLB and its cgltf data can be freed: a
//   32-bit process keeps only these arrays (a few KB per driver).
//
//   THE BUILD HALF (needs cgltf.h included before this file, as the game and the
//   studio do): RldAnim_SkeletonFromGltf, RldAnim_ClipFromGltf. They read from
//   a cgltf_data whose buffers are loaded (cgltf_load_buffers; a GLB's BIN chunk).
//
// CONVENTIONS. Matrices are float[16], COLUMN-MAJOR as glTF stores them:
// m[col * 4 + row], points are column vectors (p' = M * p). A rotation is a unit
// quaternion (x, y, z, w) as glTF stores it. A palette entry for skinning is a
// 3 x 4 ROW-MAJOR float[12] (the three rows of the affine matrix; the fourth row
// is 0 0 0 1) - the form the skinning loop reads fastest. Times are seconds of
// the clip as the file has them (glTF animation input).
//
// THE POSE. One RldAnimTrs per node (translation, rotation, scale). A clip
// writes only the nodes and paths it has channels for; the caller starts from
// the rest pose (RldAnim_PoseRest), so a node no clip moves keeps the file's
// transform. glTF forbids animating a node that has a matrix; such a node keeps
// its matrix, decomposed once into TRS (RldAnim_DecomposeMatrix: no shear, as
// glTF requires of an animatable node anyway).
//
// u8, u16, u32 come from the including file (the game: include/macros.h).

#ifndef RLDHIRES_ANIM_INC
#define RLDHIRES_ANIM_INC

#include <math.h>
#include <stdlib.h>
#include <string.h>

//========================================================================================
// THE RUNTIME HALF
//========================================================================================

#define RLDANIM_PATH_TRANSLATION 0
#define RLDANIM_PATH_ROTATION 1
#define RLDANIM_PATH_SCALE 2

#define RLDANIM_INTERP_LINEAR 0
#define RLDANIM_INTERP_STEP 1
#define RLDANIM_INTERP_CUBIC 2 // CUBICSPLINE: only its values are kept, read linearly between them

// How a time maps onto a clip (RldAnim_ClipTime).
#define RLDANIM_PLAY_LOOP 0 // wraps over the clip's length
#define RLDANIM_PLAY_ONCE 1 // clamped: the last pose is held after the end
// A pose scrub (steering) is RLDANIM_PLAY_ONCE at fraction * length.

#define RLDANIM_NODES_MAX 1024u  // nodes of one file (the build half refuses more)
#define RLDANIM_JOINTS_MAX 255u  // joints of one skin as stored here (the format allows 64)
#define RLDANIM_KEYS_MAX 1000000u // keys of one channel (a bound for the 32-bit arithmetic)

typedef struct
{
	float t[3];
	float r[4]; // x, y, z, w
	float s[3];
} RldAnimTrs;

typedef struct
{
	u32 node;   // the node it moves
	u8 path;    // RLDANIM_PATH_*
	u8 interp;  // RLDANIM_INTERP_*
	u32 keys;   // >= 1
	const float *times;  // keys, ascending (equal neighbours allowed: a jump)
	const float *values; // keys * 3 (translation, scale) or keys * 4 (rotation)
} RldAnimChannel;

typedef struct
{
	u32 channelCount;
	RldAnimChannel *channels; // inside block
	float start;              // the smallest key time of every channel
	float end;                // the largest
	void *block;              // one allocation: channels, times, values
	u32 bytes;
} RldAnimClip;

typedef struct
{
	u32 nodeCount;
	int *parent;        // -1 = a root
	u32 *order;         // node indices, every parent before its children
	RldAnimTrs *rest;   // the file's transform of every node
	u32 jointCount;     // 0 = no skin
	u32 *jointNode;     // the node of joint j
	float *inverseBind; // jointCount x 16, column-major (identity when the file has none)
	void *block;        // one allocation for all of the above
	u32 bytes;
} RldAnimSkeleton;

static void RldAnim_TrsIdentity(RldAnimTrs *trs)
{
	memset(trs, 0, sizeof(*trs));
	trs->r[3] = 1.0f;
	trs->s[0] = trs->s[1] = trs->s[2] = 1.0f;
}

static void RldAnim_QuatNormalize(float q[4])
{
	const double len = sqrt((double)q[0] * q[0] + (double)q[1] * q[1] + (double)q[2] * q[2] + (double)q[3] * q[3]);

	if (!(len > 1e-12))
	{
		q[0] = q[1] = q[2] = 0.0f;
		q[3] = 1.0f;
		return;
	}
	q[0] = (float)(q[0] / len);
	q[1] = (float)(q[1] / len);
	q[2] = (float)(q[2] / len);
	q[3] = (float)(q[3] / len);
}

// Normalised linear interpolation along the shorter arc (b is turned to a's
// hemisphere first): the blend of two poses. u = 0 gives a, 1 gives b.
static void RldAnim_QuatNlerp(const float a[4], const float b[4], float u, float out[4])
{
	const float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
	const float sb = (d < 0.0f) ? -1.0f : 1.0f;
	int i;

	for (i = 0; i < 4; i++)
	{
		out[i] = a[i] + (sb * b[i] - a[i]) * u;
	}
	RldAnim_QuatNormalize(out);
}

// Spherical interpolation along the shorter arc: the glTF rule for a LINEAR
// rotation sampler. Nearly equal quaternions take the nlerp (the slerp's
// division by sin(theta) loses its precision there; the two agree to 1e-7).
static void RldAnim_QuatSlerp(const float a[4], const float b[4], float u, float out[4])
{
	double d = (double)a[0] * b[0] + (double)a[1] * b[1] + (double)a[2] * b[2] + (double)a[3] * b[3];
	double sb = 1.0;
	double theta;
	double sinTheta;
	double wa;
	double wb;
	int i;

	if (d < 0.0)
	{
		d = -d;
		sb = -1.0;
	}
	if (d > 0.9995)
	{
		RldAnim_QuatNlerp(a, b, u, out);
		return;
	}
	theta = acos(d);
	sinTheta = sin(theta);
	wa = sin((1.0 - (double)u) * theta) / sinTheta;
	wb = sb * sin((double)u * theta) / sinTheta;
	for (i = 0; i < 4; i++)
	{
		out[i] = (float)(wa * a[i] + wb * b[i]);
	}
	RldAnim_QuatNormalize(out);
}

// The clip's time for a time from its start (seconds): looped over its length,
// or clamped (once; a scrub passes fraction * length). A clip of length 0 is
// always at its start.
static float RldAnim_ClipTime(const RldAnimClip *clip, float time, int play)
{
	const double length = (double)clip->end - (double)clip->start;
	double local = (double)time;

	if (!(length > 0.0))
	{
		return clip->start;
	}
	if (play == RLDANIM_PLAY_LOOP)
	{
		local = fmod(local, length);
		if (local < 0.0)
		{
			local += length;
		}
	}
	else
	{
		local = (local < 0.0) ? 0.0 : ((local > length) ? length : local);
	}
	return (float)((double)clip->start + local);
}

static float RldAnim_ClipLength(const RldAnimClip *clip)
{
	return (clip->end > clip->start) ? (clip->end - clip->start) : 0.0f;
}

// One channel at time t into out (3 or 4 floats): before the first key its
// first value, after the last its last; STEP the value of the key at or before
// t; LINEAR (and CUBIC, read linearly) between the two keys around t - a
// rotation by slerp (glTF), the rest by lerp.
static void RldAnim_SampleChannel(const RldAnimChannel *ch, float t, float *out)
{
	const u32 n = (ch->path == RLDANIM_PATH_ROTATION) ? 4u : 3u;
	u32 lo;
	u32 hi;
	u32 c;
	float u;

	if ((ch->keys <= 1u) || !(t > ch->times[0]))
	{
		memcpy(out, ch->values, n * sizeof(float));
		return;
	}
	if (t >= ch->times[ch->keys - 1u])
	{
		memcpy(out, ch->values + (size_t)(ch->keys - 1u) * n, n * sizeof(float));
		return;
	}
	// times[lo] <= t < times[hi], hi = lo + 1
	lo = 0;
	hi = ch->keys - 1u;
	while ((hi - lo) > 1u)
	{
		const u32 mid = lo + (hi - lo) / 2u;

		if (ch->times[mid] <= t)
		{
			lo = mid;
		}
		else
		{
			hi = mid;
		}
	}
	if (ch->interp == RLDANIM_INTERP_STEP)
	{
		memcpy(out, ch->values + (size_t)lo * n, n * sizeof(float));
		return;
	}
	u = (ch->times[hi] > ch->times[lo]) ? ((t - ch->times[lo]) / (ch->times[hi] - ch->times[lo])) : 1.0f;
	if (ch->path == RLDANIM_PATH_ROTATION)
	{
		RldAnim_QuatSlerp(ch->values + (size_t)lo * 4u, ch->values + (size_t)hi * 4u, u, out);
		return;
	}
	for (c = 0; c < n; c++)
	{
		const float a = ch->values[(size_t)lo * n + c];
		const float b = ch->values[(size_t)hi * n + c];

		out[c] = a + (b - a) * u;
	}
}

// The clip at clip time t (RldAnim_ClipTime) written into pose[nodeCount]: only
// the nodes and paths the clip has channels for.
static void RldAnim_SampleClip(const RldAnimClip *clip, float t, RldAnimTrs *pose, u32 nodeCount)
{
	u32 i;

	for (i = 0; i < clip->channelCount; i++)
	{
		const RldAnimChannel *ch = &clip->channels[i];
		RldAnimTrs *trs;

		if (ch->node >= nodeCount)
		{
			continue;
		}
		trs = &pose[ch->node];
		if (ch->path == RLDANIM_PATH_TRANSLATION)
		{
			RldAnim_SampleChannel(ch, t, trs->t);
		}
		else if (ch->path == RLDANIM_PATH_ROTATION)
		{
			RldAnim_SampleChannel(ch, t, trs->r);
			RldAnim_QuatNormalize(trs->r);
		}
		else
		{
			RldAnim_SampleChannel(ch, t, trs->s);
		}
	}
}

static void RldAnim_PoseRest(const RldAnimSkeleton *skel, RldAnimTrs *pose)
{
	memcpy(pose, skel->rest, (size_t)skel->nodeCount * sizeof(RldAnimTrs));
}

// out = a blended towards b by w (0 = a, 1 = b): translation and scale by lerp,
// rotation by nlerp. out may be a or b.
static void RldAnim_BlendPose(const RldAnimTrs *a, const RldAnimTrs *b, float w, RldAnimTrs *out, u32 nodeCount)
{
	u32 i;
	int c;

	for (i = 0; i < nodeCount; i++)
	{
		RldAnimTrs r;

		for (c = 0; c < 3; c++)
		{
			r.t[c] = a[i].t[c] + (b[i].t[c] - a[i].t[c]) * w;
			r.s[c] = a[i].s[c] + (b[i].s[c] - a[i].s[c]) * w;
		}
		RldAnim_QuatNlerp(a[i].r, b[i].r, w, r.r);
		out[i] = r;
	}
}

// M = T * R * S, column-major.
static void RldAnim_TrsMatrix(const RldAnimTrs *trs, float m[16])
{
	const float x = trs->r[0];
	const float y = trs->r[1];
	const float z = trs->r[2];
	const float w = trs->r[3];
	const float r[3][3] = {
	    {1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y - z * w), 2.0f * (x * z + y * w)},
	    {2.0f * (x * y + z * w), 1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z - x * w)},
	    {2.0f * (x * z - y * w), 2.0f * (y * z + x * w), 1.0f - 2.0f * (x * x + y * y)},
	};
	int row;
	int col;

	for (col = 0; col < 3; col++)
	{
		for (row = 0; row < 3; row++)
		{
			m[col * 4 + row] = r[row][col] * trs->s[col];
		}
		m[col * 4 + 3] = 0.0f;
	}
	m[12] = trs->t[0];
	m[13] = trs->t[1];
	m[14] = trs->t[2];
	m[15] = 1.0f;
}

// out = a * b (column-major); out may not be a or b.
static void RldAnim_Mat4Mul(const float a[16], const float b[16], float out[16])
{
	int row;
	int col;

	for (col = 0; col < 4; col++)
	{
		for (row = 0; row < 4; row++)
		{
			out[col * 4 + row] = a[0 * 4 + row] * b[col * 4 + 0] + a[1 * 4 + row] * b[col * 4 + 1] + a[2 * 4 + row] * b[col * 4 + 2] +
			                     a[3 * 4 + row] * b[col * 4 + 3];
		}
	}
}

static void RldAnim_Mat4Identity(float m[16])
{
	memset(m, 0, 16 * sizeof(float));
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

// A node matrix into TRS (translation from column 3, scales from the column
// lengths, a negative determinant on the x scale, the rotation from the rest).
// Shear is lost; glTF allows none in an animatable node.
static void RldAnim_DecomposeMatrix(const float m[16], RldAnimTrs *out)
{
	double c[3][3];
	double s[3];
	double r[3][3];
	double det;
	double tr;
	int i;
	int k;

	for (i = 0; i < 3; i++)
	{
		for (k = 0; k < 3; k++)
		{
			c[i][k] = (double)m[i * 4 + k]; // column i, row k
		}
		s[i] = sqrt(c[i][0] * c[i][0] + c[i][1] * c[i][1] + c[i][2] * c[i][2]);
	}
	det = c[0][0] * (c[1][1] * c[2][2] - c[1][2] * c[2][1]) - c[1][0] * (c[0][1] * c[2][2] - c[0][2] * c[2][1]) +
	      c[2][0] * (c[0][1] * c[1][2] - c[0][2] * c[1][1]);
	if (det < 0.0)
	{
		s[0] = -s[0];
	}
	for (i = 0; i < 3; i++)
	{
		for (k = 0; k < 3; k++)
		{
			r[k][i] = (s[i] != 0.0) ? (c[i][k] / s[i]) : ((i == k) ? 1.0 : 0.0); // r[row][col]
		}
	}
	out->t[0] = m[12];
	out->t[1] = m[13];
	out->t[2] = m[14];
	out->s[0] = (float)s[0];
	out->s[1] = (float)s[1];
	out->s[2] = (float)s[2];
	tr = r[0][0] + r[1][1] + r[2][2];
	if (tr > 0.0)
	{
		const double q = sqrt(tr + 1.0) * 2.0;

		out->r[3] = (float)(0.25 * q);
		out->r[0] = (float)((r[2][1] - r[1][2]) / q);
		out->r[1] = (float)((r[0][2] - r[2][0]) / q);
		out->r[2] = (float)((r[1][0] - r[0][1]) / q);
	}
	else if ((r[0][0] > r[1][1]) && (r[0][0] > r[2][2]))
	{
		const double q = sqrt(1.0 + r[0][0] - r[1][1] - r[2][2]) * 2.0;

		out->r[3] = (float)((r[2][1] - r[1][2]) / q);
		out->r[0] = (float)(0.25 * q);
		out->r[1] = (float)((r[0][1] + r[1][0]) / q);
		out->r[2] = (float)((r[0][2] + r[2][0]) / q);
	}
	else if (r[1][1] > r[2][2])
	{
		const double q = sqrt(1.0 + r[1][1] - r[0][0] - r[2][2]) * 2.0;

		out->r[3] = (float)((r[0][2] - r[2][0]) / q);
		out->r[0] = (float)((r[0][1] + r[1][0]) / q);
		out->r[1] = (float)(0.25 * q);
		out->r[2] = (float)((r[1][2] + r[2][1]) / q);
	}
	else
	{
		const double q = sqrt(1.0 + r[2][2] - r[0][0] - r[1][1]) * 2.0;

		out->r[3] = (float)((r[1][0] - r[0][1]) / q);
		out->r[0] = (float)((r[0][2] + r[2][0]) / q);
		out->r[1] = (float)((r[1][2] + r[2][1]) / q);
		out->r[2] = (float)(0.25 * q);
	}
	RldAnim_QuatNormalize(out->r);
}

// The model-space matrix of every node for a pose: world[n] = world[parent] *
// local[n] (column-major, nodeCount x 16), in the skeleton's order (parents
// first). The scene root is the model space: a node without a parent stands on
// its own transform.
static void RldAnim_NodeMatrices(const RldAnimSkeleton *skel, const RldAnimTrs *pose, float *world)
{
	u32 i;

	for (i = 0; i < skel->nodeCount; i++)
	{
		const u32 n = skel->order[i];
		float local[16];

		RldAnim_TrsMatrix(&pose[n], local);
		if (skel->parent[n] < 0)
		{
			memcpy(&world[(size_t)n * 16u], local, sizeof(local));
		}
		else
		{
			RldAnim_Mat4Mul(&world[(size_t)skel->parent[n] * 16u], local, &world[(size_t)n * 16u]);
		}
	}
}

// The joint matrix of joint j: world[jointNode[j]] * inverseBind[j] (glTF: the
// skinned mesh node's own transform does not act on its vertices).
static void RldAnim_JointMatrix(const RldAnimSkeleton *skel, const float *world, u32 j, float out[16])
{
	RldAnim_Mat4Mul(&world[(size_t)skel->jointNode[j] * 16u], &skel->inverseBind[(size_t)j * 16u], out);
}

// pre * m as a 3 x 4 row-major palette entry (pre may be NULL: m alone).
static void RldAnim_PaletteEntry(const float *pre, const float m[16], float out[12])
{
	float full[16];
	const float *src = m;
	int row;

	if (pre != NULL)
	{
		RldAnim_Mat4Mul(pre, m, full);
		src = full;
	}
	for (row = 0; row < 3; row++)
	{
		out[row * 4 + 0] = src[0 * 4 + row];
		out[row * 4 + 1] = src[1 * 4 + row];
		out[row * 4 + 2] = src[2 * 4 + row];
		out[row * 4 + 3] = src[3 * 4 + row];
	}
}

// One skinned vertex: M = sum of weight[i] * palette[joint[i]] (weights 0 are
// skipped), p' = M * (p, 1), n' = M3x3 * n normalised (the inverse transpose is
// not taken: the palettes of a driver carry rotations and near-uniform scales).
// A vertex whose weights sum to 0 is left at the origin (the loader refuses it).
static void RldAnim_SkinVertex(const float *palette, const u8 joint[4], const float weight[4], const float p[3], const float n[3], float outP[3],
                               float outN[3])
{
	float m[12];
	double len;
	int i;
	int k;

	memset(m, 0, sizeof(m));
	for (i = 0; i < 4; i++)
	{
		const float w = weight[i];

		if (w != 0.0f)
		{
			const float *e = &palette[(size_t)joint[i] * 12u];

			for (k = 0; k < 12; k++)
			{
				m[k] += w * e[k];
			}
		}
	}
	outP[0] = m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3];
	outP[1] = m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7];
	outP[2] = m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11];
	outN[0] = m[0] * n[0] + m[1] * n[1] + m[2] * n[2];
	outN[1] = m[4] * n[0] + m[5] * n[1] + m[6] * n[2];
	outN[2] = m[8] * n[0] + m[9] * n[1] + m[10] * n[2];
	len = sqrt((double)outN[0] * outN[0] + (double)outN[1] * outN[1] + (double)outN[2] * outN[2]);
	if (len > 1e-12)
	{
		outN[0] = (float)(outN[0] / len);
		outN[1] = (float)(outN[1] / len);
		outN[2] = (float)(outN[2] / len);
	}
}

static void RldAnim_FreeClip(RldAnimClip *clip)
{
	free(clip->block);
	memset(clip, 0, sizeof(*clip));
}

static void RldAnim_FreeSkeleton(RldAnimSkeleton *skel)
{
	free(skel->block);
	memset(skel, 0, sizeof(*skel));
}

// The order of a skeleton's nodes, parents first (a stable walk by depth); 0
// when the parents form a cycle or name a node outside.
static int RldAnim_BuildOrder(RldAnimSkeleton *skel)
{
	u32 placed = 0;
	u32 depth = 0;

	while ((placed < skel->nodeCount) && (depth <= skel->nodeCount))
	{
		u32 n;

		for (n = 0; n < skel->nodeCount; n++)
		{
			u32 d = 0;
			int p = skel->parent[n];

			while ((p >= 0) && (d <= skel->nodeCount))
			{
				if ((u32)p >= skel->nodeCount)
				{
					return 0;
				}
				p = skel->parent[p];
				d++;
			}
			if (d > skel->nodeCount)
			{
				return 0; // a cycle
			}
			if (d == depth)
			{
				skel->order[placed++] = n;
			}
		}
		depth++;
	}
	return placed == skel->nodeCount;
}

// A skeleton of nodeCount nodes and jointCount joints with every array made:
// rest transforms identity, no parents, inverse binds identity. 1 = made (free
// with RldAnim_FreeSkeleton). The build half fills it from a file; the
// selftests fill it by hand.
static int RldAnim_SkeletonAlloc(RldAnimSkeleton *skel, u32 nodeCount, u32 jointCount)
{
	size_t bytes;
	u8 *p;
	u32 i;

	memset(skel, 0, sizeof(*skel));
	if ((nodeCount == 0u) || (nodeCount > RLDANIM_NODES_MAX) || (jointCount > RLDANIM_JOINTS_MAX))
	{
		return 0;
	}
	bytes = (size_t)nodeCount * (sizeof(int) + sizeof(u32) + sizeof(RldAnimTrs)) + (size_t)jointCount * (sizeof(u32) + 16u * sizeof(float));
	p = (u8 *)malloc(bytes);
	if (p == NULL)
	{
		return 0;
	}
	skel->block = p;
	skel->bytes = (u32)bytes;
	skel->nodeCount = nodeCount;
	skel->jointCount = jointCount;
	skel->rest = (RldAnimTrs *)p;
	p += (size_t)nodeCount * sizeof(RldAnimTrs);
	skel->inverseBind = (float *)p;
	p += (size_t)jointCount * 16u * sizeof(float);
	skel->parent = (int *)p;
	p += (size_t)nodeCount * sizeof(int);
	skel->order = (u32 *)p;
	p += (size_t)nodeCount * sizeof(u32);
	skel->jointNode = (u32 *)p;
	for (i = 0; i < nodeCount; i++)
	{
		RldAnim_TrsIdentity(&skel->rest[i]);
		skel->parent[i] = -1;
		skel->order[i] = i;
	}
	for (i = 0; i < jointCount; i++)
	{
		RldAnim_Mat4Identity(&skel->inverseBind[(size_t)i * 16u]);
		skel->jointNode[i] = 0;
	}
	return 1;
}

//========================================================================================
// THE BUILD HALF (cgltf.h before this file: CGLTF_H_INCLUDED__)
//========================================================================================

#ifdef CGLTF_H_INCLUDED__

// The skeleton of a file: every node of data (index = cgltf node index), its
// parent, its rest TRS (a matrix node decomposed), the parents-first order;
// the joints of skin (NULL: none) with their inverse bind matrices. 1 = made;
// 0 with *why (too many nodes or joints, a cycle, an unreadable accessor, no memory).
static int RldAnim_SkeletonFromGltf(const cgltf_data *gltf, const cgltf_skin *skin, RldAnimSkeleton *out, const char **why)
{
	const u32 jointCount = (skin != NULL) ? (u32)skin->joints_count : 0u;
	u32 i;

	*why = "";
	if ((gltf == NULL) || (gltf->nodes_count == 0u) || (gltf->nodes_count > RLDANIM_NODES_MAX) || (jointCount > RLDANIM_JOINTS_MAX))
	{
		*why = "no nodes, or too many nodes or joints";
		memset(out, 0, sizeof(*out));
		return 0;
	}
	if (!RldAnim_SkeletonAlloc(out, (u32)gltf->nodes_count, jointCount))
	{
		*why = "out of memory (skeleton)";
		return 0;
	}
	for (i = 0; i < out->nodeCount; i++)
	{
		const cgltf_node *node = &gltf->nodes[i];
		RldAnimTrs *trs = &out->rest[i];

		out->parent[i] = (node->parent != NULL) ? (int)(node->parent - gltf->nodes) : -1;
		if (node->has_matrix)
		{
			RldAnim_DecomposeMatrix(node->matrix, trs);
			continue;
		}
		if (node->has_translation)
		{
			memcpy(trs->t, node->translation, sizeof(trs->t));
		}
		if (node->has_rotation)
		{
			memcpy(trs->r, node->rotation, sizeof(trs->r));
			RldAnim_QuatNormalize(trs->r);
		}
		if (node->has_scale)
		{
			memcpy(trs->s, node->scale, sizeof(trs->s));
		}
	}
	if (!RldAnim_BuildOrder(out))
	{
		RldAnim_FreeSkeleton(out);
		*why = "the node parents form a cycle";
		return 0;
	}
	for (i = 0; i < jointCount; i++)
	{
		if (skin->joints[i] == NULL)
		{
			RldAnim_FreeSkeleton(out);
			*why = "a joint without its node";
			return 0;
		}
		out->jointNode[i] = (u32)(skin->joints[i] - gltf->nodes);
		if (skin->inverse_bind_matrices != NULL)
		{
			if ((skin->inverse_bind_matrices->count < jointCount) || (skin->inverse_bind_matrices->type != cgltf_type_mat4) ||
			    !cgltf_accessor_read_float(skin->inverse_bind_matrices, i, &out->inverseBind[(size_t)i * 16u], 16))
			{
				RldAnim_FreeSkeleton(out);
				*why = "the inverse bind matrices cannot be read";
				return 0;
			}
		}
	}
	return 1;
}

// The channels of one animation copied out of the file: translation, rotation
// and scale of nodes (weights and channels without a node are left out; the
// count of those goes to *skipped). Inputs must ascend; CUBICSPLINE keeps the
// middle value of each (in-tangent, value, out-tangent) triple. 1 = made (free
// with RldAnim_FreeClip); a clip without one usable channel is made too (empty,
// length 0: the rest pose). 0 with *why.
static int RldAnim_ClipFromGltf(const cgltf_data *gltf, const cgltf_animation *anim, RldAnimClip *out, u32 *skipped, const char **why)
{
	size_t floats = 0;
	u32 channels = 0;
	size_t bytes;
	u8 *p;
	float *f;
	u32 i;
	int first = 1;

	memset(out, 0, sizeof(*out));
	*skipped = 0;
	*why = "";
	for (i = 0; i < (u32)anim->channels_count; i++)
	{
		const cgltf_animation_channel *ch = &anim->channels[i];
		const cgltf_animation_sampler *s = ch->sampler;
		const u32 n = (ch->target_path == cgltf_animation_path_type_rotation) ? 4u : 3u;
		u32 keys;
		u32 mult;

		if ((ch->target_node == NULL) || (s == NULL) || (s->input == NULL) || (s->output == NULL) ||
		    ((ch->target_path != cgltf_animation_path_type_translation) && (ch->target_path != cgltf_animation_path_type_rotation) &&
		     (ch->target_path != cgltf_animation_path_type_scale)))
		{
			(*skipped)++;
			continue;
		}
		keys = (u32)s->input->count;
		mult = (s->interpolation == cgltf_interpolation_type_cubic_spline) ? 3u : 1u;
		if ((keys == 0u) || (keys > RLDANIM_KEYS_MAX) || (s->output->count != (cgltf_size)keys * mult) || (cgltf_num_components(s->output->type) != n) ||
		    (cgltf_num_components(s->input->type) != 1u))
		{
			*why = "a sampler whose input and output do not match";
			return 0;
		}
		floats += (size_t)keys * (1u + n);
		channels++;
	}
	bytes = (size_t)channels * sizeof(RldAnimChannel) + floats * sizeof(float);
	p = (u8 *)malloc((bytes > 0u) ? bytes : 1u);
	if (p == NULL)
	{
		*why = "out of memory (clip)";
		return 0;
	}
	out->block = p;
	out->bytes = (u32)bytes;
	out->channels = (RldAnimChannel *)p;
	f = (float *)(p + (size_t)channels * sizeof(RldAnimChannel));
	for (i = 0; i < (u32)anim->channels_count; i++)
	{
		const cgltf_animation_channel *ch = &anim->channels[i];
		const cgltf_animation_sampler *s = ch->sampler;
		RldAnimChannel *dst;
		u32 n;
		u32 keys;
		u32 k;

		if ((ch->target_node == NULL) || (s == NULL) || (s->input == NULL) || (s->output == NULL) ||
		    ((ch->target_path != cgltf_animation_path_type_translation) && (ch->target_path != cgltf_animation_path_type_rotation) &&
		     (ch->target_path != cgltf_animation_path_type_scale)))
		{
			continue;
		}
		dst = &out->channels[out->channelCount++];
		n = (ch->target_path == cgltf_animation_path_type_rotation) ? 4u : 3u;
		keys = (u32)s->input->count;
		dst->node = (u32)(ch->target_node - gltf->nodes);
		dst->path = (u8)((ch->target_path == cgltf_animation_path_type_translation) ? RLDANIM_PATH_TRANSLATION
		                 : ((ch->target_path == cgltf_animation_path_type_rotation) ? RLDANIM_PATH_ROTATION : RLDANIM_PATH_SCALE));
		dst->interp = (u8)((s->interpolation == cgltf_interpolation_type_step) ? RLDANIM_INTERP_STEP
		                   : ((s->interpolation == cgltf_interpolation_type_cubic_spline) ? RLDANIM_INTERP_CUBIC : RLDANIM_INTERP_LINEAR));
		dst->keys = keys;
		dst->times = f;
		for (k = 0; k < keys; k++)
		{
			if (!cgltf_accessor_read_float(s->input, k, &f[k], 1) || !(f[k] == f[k]) || ((k > 0u) && (f[k] < f[k - 1u])))
			{
				RldAnim_FreeClip(out);
				*why = "key times that are not finite or do not ascend";
				return 0;
			}
		}
		if (first || (f[0] < out->start))
		{
			out->start = f[0];
		}
		if (first || (f[keys - 1u] > out->end))
		{
			out->end = f[keys - 1u];
		}
		first = 0;
		f += keys;
		dst->values = f;
		for (k = 0; k < keys; k++)
		{
			const cgltf_size element = (dst->interp == RLDANIM_INTERP_CUBIC) ? ((cgltf_size)k * 3u + 1u) : (cgltf_size)k;
			u32 c;

			if (!cgltf_accessor_read_float(s->output, element, &f[(size_t)k * n], n))
			{
				RldAnim_FreeClip(out);
				*why = "key values that cannot be read";
				return 0;
			}
			for (c = 0; c < n; c++)
			{
				if (!(f[(size_t)k * n + c] == f[(size_t)k * n + c]))
				{
					RldAnim_FreeClip(out);
					*why = "key values that are not finite";
					return 0;
				}
			}
			if (n == 4u)
			{
				RldAnim_QuatNormalize(&f[(size_t)k * n]);
			}
		}
		f += (size_t)keys * n;
	}
	return 1;
}

#endif // CGLTF_H_INCLUDED__

#endif // RLDHIRES_ANIM_INC
