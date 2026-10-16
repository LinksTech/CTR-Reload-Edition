#include <platform/engine.h>

// cgltf: the implementation lives in this file; the warnings of the third-party header are silenced only around it.
#define CGLTF_IMPLEMENTATION
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <cgltf.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <rldpng.inc>

#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ENGINE STATIC MODEL, THE FILE SIDE - see include/platform/engine.h, STATIC MODEL.
//
// WHAT IT DOES: reads a large static glTF binary (.glb: millions of triangles,
// hundreds of MB, ~150 PNG textures) on the CPU, with no device and no game
// state, and hands it on as pieces (vertices, u32 indices, ranges, a box) and
// as RGBA8 images. engine_static.c makes the GPU side from that.
//
// THE MEMORY RULE: the process is 32 bit, so the file is NEVER read into memory
// as a whole. Only the JSON chunk is read and parsed with cgltf (the BIN chunk
// stays in the file, cgltf gets a copy of the header and the JSON chunk only).
// Every attribute is read per primitive, in chunks, through
// EngineStaticSource.read at binOffset + bufferView.offset + accessor.offset.
// BuildPieces holds at most: 12 bytes per triangle (centres) while the split is
// made, 1 byte per triangle (piece number), the arrays of the pieces, and the
// attributes of ONE primitive at a time.
//
// WHAT IS READ: the nodes of the scene (scene, else scenes[0], else every node
// without a parent) with a mesh, in node order; per primitive TRIANGLES with
// POSITION (float VEC3), optional TEXCOORD_0 (float, or normalized u8/u16 VEC2),
// NORMAL (float VEC3), COLOR_0 (float or normalized u8/u16, VEC3/VEC4), indices
// (none, u8, u16, u32); every accessor in the GLB BIN buffer, not sparse, inside
// the BIN chunk (u64 arithmetic). Materials: base colour texture (TEXCOORD_0
// only), factor, alpha mode, cutoff, double sided, KHR_materials_unlit, and the normal map
// (normalTexture, TEXCOORD_0 only: image, wrap, scale; its image is served like
// every other, the caller makes it as linear data). With ENGINE_STATIC_BUILD_NORMALS
// NORMAL is read too and packed into uvBox as the world normal
// (inverse-transpose of the node's 3x3, normalized, (n * 0.5 + 0.5) * 255
// rounded, w = 255; no NORMAL or a singular node gives 0, 0, 0, 0).
// Images: PNG, from the BIN or
// from a file beside the .glb.
// WHAT IS REFUSED OR COUNTED: a primitive that is not TRIANGLES, has no
// POSITION, or has an attribute of a supported kind in a type it does not read
// or outside the BIN is SKIPPED and counted (skippedPrimitives), the file goes
// on. Every other attribute (TANGENT, JOINTS, ...) is ignored. A file with no
// supported triangle, a bad header or JSON, or more than 2^32 - 1 triangles is
// refused with a short reason. A node matrix with a negative determinant turns
// the winding of its triangles (counted in mirroredNodes). Degenerate triangles
// are kept: the triangle count is the file's.
//
// DETERMINISM: the same file gives the same bytes. The scene walk, the split
// (longest side of the centres' box at its middle, stable, ties to the lower
// axis) and the vertex order depend on nothing but the file; all transforms
// are done in double and stored as float.

#define ENGSL_GLB_MAGIC 0x46546C67u // "glTF"
#define ENGSL_CHUNK_JSON 0x4E4F534Au
#define ENGSL_CHUNK_BIN 0x004E4942u
#define ENGSL_JSON_MAX 0x40000000u
#define ENGSL_STRIDE_MAX 4096u
#define ENGSL_CHUNK_ELEMS 2048u
#define ENGSL_TRI_CHUNK 1024u
#define ENGSL_IMAGE_MAX 0x10000000u
#define ENGSL_NODE_DEPTH_MAX 256
#define ENGSL_STACK_MAX 1024
#define ENGSL_ALLOC_MAX 0x7fffffffull

// What model->gltf points to: the parsed JSON, the block it was parsed from
// (cgltf_data keeps json pointing into it) and the nodes with a mesh.
typedef struct
{
	cgltf_data *data;
	void *block;
	const cgltf_node **nodes;
	u32 nodeCount;
} EngStaticLoadGltf;

// What the file source's user pointer holds.
typedef struct
{
	FILE *file;
	char *dir;
	u64 pos; // where the file position is, ~0 unknown
} EngStaticLoadFile;

// The supported accessors of one primitive.
typedef struct
{
	const cgltf_accessor *pos;
	const cgltf_accessor *uv;
	const cgltf_accessor *nrm;
	const cgltf_accessor *col;
	const cgltf_accessor *idx;
} EngStaticLoadPrim;

typedef struct
{
	const EngineStaticModel *model;
	const cgltf_node *node;
	float m[16];
	int mirrored;
	EngStaticLoadPrim p;
	int material;
	u32 firstTri;
	u32 tris;
} EngStaticLoadPrimInfo;

typedef int (*EngStaticLoadPrimFn)(void *ctx, const EngStaticLoadPrimInfo *pi);
typedef int (*EngStaticLoadTriFn)(void *ctx, u32 tri, u32 i0, u32 i1, u32 i2);

internal void EngStaticLoad_Why(char *why, int whySize, const char *fmt, ...)
{
	va_list ap;

	if (why == NULL || whySize <= 0)
	{
		return;
	}
	va_start(ap, fmt);
	vsnprintf(why, (size_t)whySize, fmt, ap);
	va_end(ap);
	why[whySize - 1] = 0;
}

internal u32 EngStaticLoad_LE32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

// malloc that refuses what a 32 bit process cannot hold; never returns NULL for 0.
internal void *EngStaticLoad_Alloc(u64 bytes)
{
	if (bytes > ENGSL_ALLOC_MAX)
	{
		return NULL;
	}
	return malloc((bytes != 0) ? (size_t)bytes : 1u);
}

// ---------------------------------------------------------------------------
// The file source

#if defined(_WIN32)
#define ENGSL_SEEK(f, o, w) _fseeki64((f), (__int64)(o), (w))
#define ENGSL_TELL(f) ((long long)_ftelli64(f))
#else
#define ENGSL_SEEK(f, o, w) fseeko((f), (off_t)(o), (w))
#define ENGSL_TELL(f) ((long long)ftello(f))
#endif

internal int EngStaticLoad_FileRead(void *user, u64 offset, void *dst, u32 bytes)
{
	EngStaticLoadFile *f = (EngStaticLoadFile *)user;

	if (f == NULL || f->file == NULL)
	{
		return 0;
	}
	if (bytes == 0)
	{
		return 1;
	}
	if (f->pos != offset)
	{
		f->pos = ~(u64)0;
		if (ENGSL_SEEK(f->file, offset, SEEK_SET) != 0)
		{
			return 0;
		}
	}
	if (fread(dst, 1, bytes, f->file) != bytes)
	{
		f->pos = ~(u64)0;
		return 0;
	}
	f->pos = offset + bytes;
	return 1;
}

int EngineStatic_SourceFile(const char *path, EngineStaticSource *out)
{
	FILE *fp;
	EngStaticLoadFile *f;
	char *dir;
	long long size;
	size_t dirLen = 0;
	size_t i;

	if (out != NULL)
	{
		memset(out, 0, sizeof(*out));
	}
	if (path == NULL || out == NULL)
	{
		return 0;
	}
	fp = fopen(path, "rb");
	if (fp == NULL)
	{
		return 0;
	}
	if (ENGSL_SEEK(fp, 0, SEEK_END) != 0)
	{
		fclose(fp);
		return 0;
	}
	size = ENGSL_TELL(fp);
	if (size < 0 || ENGSL_SEEK(fp, 0, SEEK_SET) != 0)
	{
		fclose(fp);
		return 0;
	}
	for (i = 0; path[i] != 0; i++)
	{
		if (path[i] == '/' || path[i] == '\\')
		{
			dirLen = i;
		}
	}
	f = (EngStaticLoadFile *)calloc(1, sizeof(*f));
	dir = (char *)malloc(dirLen + 1);
	if (f == NULL || dir == NULL)
	{
		free(f);
		free(dir);
		fclose(fp);
		return 0;
	}
	memcpy(dir, path, dirLen);
	dir[dirLen] = 0;
	f->file = fp;
	f->dir = dir;
	f->pos = 0;
	out->read = EngStaticLoad_FileRead;
	out->user = f;
	out->size = (u64)size;
	out->baseDir = dir;
	return 1;
}

void EngineStatic_SourceFileClose(EngineStaticSource *src)
{
	EngStaticLoadFile *f;

	if (src == NULL)
	{
		return;
	}
	f = (EngStaticLoadFile *)src->user;
	if (f != NULL)
	{
		if (f->file != NULL)
		{
			fclose(f->file);
		}
		free(f->dir);
		free(f);
	}
	memset(src, 0, sizeof(*src));
}

// ---------------------------------------------------------------------------
// Matrices (glTF column-major: element (row r, column c) = m[c * 4 + r])

internal void EngStaticLoad_NodeMatrix(const cgltf_node *n, float m[16])
{
	cgltf_node_transform_world(n, m);
}

internal double EngStaticLoad_Det3(const float m[16])
{
	const double a = m[0], b = m[4], c = m[8];
	const double d = m[1], e = m[5], f = m[9];
	const double g = m[2], h = m[6], i = m[10];

	return a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
}

// The inverse transpose of the 3 x 3 part (cofactors over the determinant), row-major.
// 0 when the matrix is singular.
internal int EngStaticLoad_NormalMatrix(const float m[16], double n[9])
{
	const double a = m[0], b = m[4], c = m[8];
	const double d = m[1], e = m[5], f = m[9];
	const double g = m[2], h = m[6], i = m[10];
	const double det = EngStaticLoad_Det3(m);

	if (!(fabs(det) > 1e-30))
	{
		return 0;
	}
	n[0] = (e * i - f * h) / det;
	n[1] = -(d * i - f * g) / det;
	n[2] = (d * h - e * g) / det;
	n[3] = -(b * i - c * h) / det;
	n[4] = (a * i - c * g) / det;
	n[5] = -(a * h - b * g) / det;
	n[6] = (b * f - c * e) / det;
	n[7] = -(a * f - c * d) / det;
	n[8] = (a * e - b * d) / det;
	return 1;
}

internal void EngStaticLoad_XformPoint(const float m[16], const double in[3], double out[3])
{
	out[0] = (double)m[0] * in[0] + (double)m[4] * in[1] + (double)m[8] * in[2] + (double)m[12];
	out[1] = (double)m[1] * in[0] + (double)m[5] * in[1] + (double)m[9] * in[2] + (double)m[13];
	out[2] = (double)m[2] * in[0] + (double)m[6] * in[1] + (double)m[10] * in[2] + (double)m[14];
}

// xyz triples in place: computed in double, stored as float.
internal void EngStaticLoad_XformPoints(const float m[16], float *p, u32 count)
{
	u32 i;

	for (i = 0; i < count; i++)
	{
		double in[3];
		double out[3];

		in[0] = p[3 * (size_t)i + 0];
		in[1] = p[3 * (size_t)i + 1];
		in[2] = p[3 * (size_t)i + 2];
		EngStaticLoad_XformPoint(m, in, out);
		p[3 * (size_t)i + 0] = (float)out[0];
		p[3 * (size_t)i + 1] = (float)out[1];
		p[3 * (size_t)i + 2] = (float)out[2];
	}
}

// ---------------------------------------------------------------------------
// The scene walk

typedef struct
{
	const cgltf_node **list;
	u32 count;
	u32 visited;
	u32 cap;
} EngStaticLoadWalk;

internal int EngStaticLoad_Visit(const cgltf_node *n, int depth, EngStaticLoadWalk *w)
{
	size_t i;

	if (n == NULL || depth > ENGSL_NODE_DEPTH_MAX || w->visited >= w->cap)
	{
		return 0;
	}
	w->visited++;
	if (n->mesh != NULL)
	{
		w->list[w->count++] = n;
	}
	for (i = 0; i < n->children_count; i++)
	{
		if (!EngStaticLoad_Visit(n->children[i], depth + 1, w))
		{
			return 0;
		}
	}
	return 1;
}

// The nodes with a mesh in visit order (parents before children, siblings in file order).
internal int EngStaticLoad_BuildNodeList(EngStaticLoadGltf *g, char *why, int whySize)
{
	const cgltf_data *d = g->data;
	EngStaticLoadWalk w;
	const cgltf_scene *scene = d->scene;
	size_t i;
	int ok = 1;

	w.cap = (u32)d->nodes_count;
	w.count = 0;
	w.visited = 0;
	w.list = (const cgltf_node **)EngStaticLoad_Alloc((u64)w.cap * sizeof(*w.list));
	if (w.list == NULL)
	{
		EngStaticLoad_Why(why, whySize, "out of memory (node list)");
		return 0;
	}
	if (scene == NULL && d->scenes_count > 0)
	{
		scene = &d->scenes[0];
	}
	if (scene != NULL)
	{
		for (i = 0; ok && i < scene->nodes_count; i++)
		{
			ok = EngStaticLoad_Visit(scene->nodes[i], 0, &w);
		}
	}
	else
	{
		for (i = 0; ok && i < d->nodes_count; i++)
		{
			if (d->nodes[i].parent == NULL)
			{
				ok = EngStaticLoad_Visit(&d->nodes[i], 0, &w);
			}
		}
	}
	if (!ok)
	{
		free((void *)w.list);
		EngStaticLoad_Why(why, whySize, "node tree too deep or not a tree");
		return 0;
	}
	g->nodes = w.list;
	g->nodeCount = w.count;
	return 1;
}

// ---------------------------------------------------------------------------
// Accessors

// 1 = the accessor is in the GLB BIN buffer and every element is inside the BIN chunk.
internal int EngStaticLoad_AccFits(const EngineStaticModel *model, const cgltf_accessor *a, u32 *elemSize, u32 *stride)
{
	const EngStaticLoadGltf *g = (const EngStaticLoadGltf *)model->gltf;
	const cgltf_buffer_view *v;
	u64 elem;
	u64 stride64;
	u64 span;

	if (a == NULL || a->is_sparse || a->count == 0 || a->buffer_view == NULL)
	{
		return 0;
	}
	v = a->buffer_view;
	if (g->data->buffers_count == 0 || v->buffer != &g->data->buffers[0] || g->data->buffers[0].uri != NULL)
	{
		return 0;
	}
	if (v->has_meshopt_compression || v->data != NULL)
	{
		return 0;
	}
	elem = (u64)cgltf_calc_size(a->type, a->component_type);
	if (elem == 0 || elem > 16)
	{
		return 0;
	}
	stride64 = (v->stride != 0) ? (u64)v->stride : elem;
	if (stride64 < elem || stride64 > ENGSL_STRIDE_MAX)
	{
		return 0;
	}
	if ((u64)v->offset > model->binSize || (u64)v->size > model->binSize - (u64)v->offset)
	{
		return 0;
	}
	if ((u64)a->offset > (u64)v->size)
	{
		return 0;
	}
	span = (u64)(a->count - 1) * stride64 + elem;
	if (span > (u64)v->size - (u64)a->offset)
	{
		return 0;
	}
	*elemSize = (u32)elem;
	*stride = (u32)stride64;
	return 1;
}

internal int EngStaticLoad_IsFloatOrNorm(const cgltf_accessor *a)
{
	return a->component_type == cgltf_component_type_r_32f ||
	       (a->normalized && (a->component_type == cgltf_component_type_r_8u || a->component_type == cgltf_component_type_r_16u));
}

// 1 = the primitive is drawn; *p its accessors, *tris its triangles.
internal int EngStaticLoad_PrimCheck(const EngineStaticModel *model, const cgltf_primitive *prim, EngStaticLoadPrim *p, u64 *tris)
{
	size_t i;
	u32 elem;
	u32 stride;

	memset(p, 0, sizeof(*p));
	*tris = 0;
	if (prim->type != cgltf_primitive_type_triangles)
	{
		return 0;
	}
	for (i = 0; i < prim->attributes_count; i++)
	{
		const cgltf_attribute *at = &prim->attributes[i];

		if (at->index != 0 || at->data == NULL)
		{
			continue;
		}
		switch (at->type)
		{
		case cgltf_attribute_type_position:
			if (p->pos == NULL)
			{
				p->pos = at->data;
			}
			break;
		case cgltf_attribute_type_texcoord:
			if (p->uv == NULL)
			{
				p->uv = at->data;
			}
			break;
		case cgltf_attribute_type_normal:
			if (p->nrm == NULL)
			{
				p->nrm = at->data;
			}
			break;
		case cgltf_attribute_type_color:
			if (p->col == NULL)
			{
				p->col = at->data;
			}
			break;
		default:
			break;
		}
	}
	p->idx = prim->indices;

	// Every attribute has the count of POSITION (a glTF 2.0 rule): the reads below fill arrays of that many vertices.
	if (p->pos == NULL || p->pos->type != cgltf_type_vec3 || p->pos->component_type != cgltf_component_type_r_32f ||
	    !EngStaticLoad_AccFits(model, p->pos, &elem, &stride))
	{
		return 0;
	}
	if (p->uv != NULL && (p->uv->type != cgltf_type_vec2 || !EngStaticLoad_IsFloatOrNorm(p->uv) || !EngStaticLoad_AccFits(model, p->uv, &elem, &stride) ||
	                      p->uv->count != p->pos->count))
	{
		return 0;
	}
	if (p->nrm != NULL && (p->nrm->type != cgltf_type_vec3 || p->nrm->component_type != cgltf_component_type_r_32f ||
	                       !EngStaticLoad_AccFits(model, p->nrm, &elem, &stride) || p->nrm->count != p->pos->count))
	{
		return 0;
	}
	if (p->col != NULL && ((p->col->type != cgltf_type_vec3 && p->col->type != cgltf_type_vec4) || !EngStaticLoad_IsFloatOrNorm(p->col) ||
	                       !EngStaticLoad_AccFits(model, p->col, &elem, &stride) || p->col->count != p->pos->count))
	{
		return 0;
	}
	if (p->idx != NULL &&
	    (p->idx->type != cgltf_type_scalar ||
	     (p->idx->component_type != cgltf_component_type_r_8u && p->idx->component_type != cgltf_component_type_r_16u &&
	      p->idx->component_type != cgltf_component_type_r_32u) ||
	     !EngStaticLoad_AccFits(model, p->idx, &elem, &stride)))
	{
		return 0;
	}
	*tris = (u64)((p->idx != NULL) ? p->idx->count : p->pos->count) / 3u;
	return 1;
}

// n elements from element `first` of the accessor, packed (elem bytes each) into dst.
internal int EngStaticLoad_ReadElems(const EngineStaticModel *model, const cgltf_accessor *a, u32 elem, u32 stride, u64 first, u32 n, u8 *dst)
{
	const EngineStaticSource *src = model->src;
	u64 at = model->binOffset + (u64)a->buffer_view->offset + (u64)a->offset + first * stride;
	u64 span;
	u8 *tmp;
	u32 i;
	int ok;

	if (n == 0)
	{
		return 1;
	}
	if (stride == elem)
	{
		return src->read(src->user, at, dst, n * elem);
	}
	span = (u64)(n - 1) * stride + elem;
	tmp = (u8 *)EngStaticLoad_Alloc(span);
	if (tmp == NULL)
	{
		return 0;
	}
	ok = src->read(src->user, at, tmp, (u32)span);
	if (ok)
	{
		for (i = 0; i < n; i++)
		{
			memcpy(dst + (size_t)i * elem, tmp + (size_t)i * stride, elem);
		}
	}
	free(tmp);
	return ok;
}

// n elements as floats (float, or normalized u8/u16).
internal int EngStaticLoad_ReadFloatChunk(const EngineStaticModel *model, const cgltf_accessor *a, u32 elem, u32 stride, u64 first, u32 n, float *out)
{
	u8 raw[ENGSL_CHUNK_ELEMS * 16];
	const u32 comps = (u32)cgltf_num_components(a->type);
	const u32 total = n * comps;
	u32 k;

	if (!EngStaticLoad_ReadElems(model, a, elem, stride, first, n, raw))
	{
		return 0;
	}
	for (k = 0; k < total; k++)
	{
		if (a->component_type == cgltf_component_type_r_32f)
		{
			float f;

			memcpy(&f, raw + (size_t)k * 4, 4);
			out[k] = f;
		}
		else if (a->component_type == cgltf_component_type_r_8u)
		{
			out[k] = (float)raw[k] / 255.0f;
		}
		else
		{
			u16 v;

			memcpy(&v, raw + (size_t)k * 2, 2);
			out[k] = (float)v / 65535.0f;
		}
	}
	return 1;
}

internal int EngStaticLoad_ReadFloats(const EngineStaticModel *model, const cgltf_accessor *a, float *out)
{
	u32 elem;
	u32 stride;
	const u32 comps = (u32)cgltf_num_components(a->type);
	u64 done = 0;

	if (!EngStaticLoad_AccFits(model, a, &elem, &stride))
	{
		return 0;
	}
	while (done < a->count)
	{
		u64 left = (u64)a->count - done;
		u32 n = (left > ENGSL_CHUNK_ELEMS) ? ENGSL_CHUNK_ELEMS : (u32)left;

		if (!EngStaticLoad_ReadFloatChunk(model, a, elem, stride, done, n, out + (size_t)done * comps))
		{
			return 0;
		}
		done += n;
	}
	return 1;
}

internal u8 EngStaticLoad_ColorByte(float v)
{
	if (!(v > 0.0f))
	{
		return 0;
	}
	if (v >= 1.0f)
	{
		return 255;
	}
	return (u8)(int)(v * 255.0f + 0.5f);
}

// COLOR_0 as RGBA bytes, 4 per vertex (VEC3: alpha 255).
internal int EngStaticLoad_ReadColors(const EngineStaticModel *model, const cgltf_accessor *a, u8 *out)
{
	u32 elem;
	u32 stride;
	const u32 comps = (u32)cgltf_num_components(a->type);
	u64 done = 0;

	if (!EngStaticLoad_AccFits(model, a, &elem, &stride))
	{
		return 0;
	}
	while (done < a->count)
	{
		u8 raw[ENGSL_CHUNK_ELEMS * 16];
		u64 left = (u64)a->count - done;
		u32 n = (left > ENGSL_CHUNK_ELEMS) ? ENGSL_CHUNK_ELEMS : (u32)left;
		u32 i;
		u32 c;

		if (!EngStaticLoad_ReadElems(model, a, elem, stride, done, n, raw))
		{
			return 0;
		}
		for (i = 0; i < n; i++)
		{
			u8 *o = out + ((size_t)done + i) * 4;

			o[3] = 255;
			for (c = 0; c < comps; c++)
			{
				const u8 *r = raw + (size_t)i * elem;
				u8 v;

				if (a->component_type == cgltf_component_type_r_32f)
				{
					float f;

					memcpy(&f, r + (size_t)c * 4, 4);
					v = EngStaticLoad_ColorByte(f);
				}
				else if (a->component_type == cgltf_component_type_r_8u)
				{
					v = r[c];
				}
				else
				{
					u16 w;

					memcpy(&w, r + (size_t)c * 2, 2);
					v = (u8)(((u32)w * 255u + 32767u) / 65535u);
				}
				o[c] = v;
			}
		}
		done += n;
	}
	return 1;
}

// n indices from index `first` (u8, u16 or u32 in the file) as u32.
internal int EngStaticLoad_ReadIndices(const EngineStaticModel *model, const cgltf_accessor *a, u64 first, u32 n, u32 *out)
{
	u32 elem;
	u32 stride;
	u8 raw[ENGSL_TRI_CHUNK * 3 * 4];
	u32 i;

	if (!EngStaticLoad_AccFits(model, a, &elem, &stride) || n > ENGSL_TRI_CHUNK * 3 || first + n > a->count)
	{
		return 0;
	}
	if (!EngStaticLoad_ReadElems(model, a, elem, stride, first, n, raw))
	{
		return 0;
	}
	for (i = 0; i < n; i++)
	{
		if (elem == 1)
		{
			out[i] = raw[i];
		}
		else if (elem == 2)
		{
			u16 v;

			memcpy(&v, raw + (size_t)i * 2, 2);
			out[i] = v;
		}
		else
		{
			out[i] = EngStaticLoad_LE32(raw + (size_t)i * 4);
		}
	}
	return 1;
}

// Calls fn for every triangle of the primitive in order, the three vertex indices checked against the vertex count.
internal int EngStaticLoad_EachTriangle(const EngineStaticModel *model, const EngStaticLoadPrim *p, EngStaticLoadTriFn fn, void *ctx, char *why, int whySize)
{
	const u64 vcount = p->pos->count;
	const u64 total = ((p->idx != NULL) ? (u64)p->idx->count : vcount) / 3u;
	u32 buf[ENGSL_TRI_CHUNK * 3];
	u64 t = 0;

	while (t < total)
	{
		const u64 left = total - t;
		const u32 n = (left > ENGSL_TRI_CHUNK) ? ENGSL_TRI_CHUNK : (u32)left;
		u32 k;

		if (p->idx != NULL)
		{
			if (!EngStaticLoad_ReadIndices(model, p->idx, t * 3u, n * 3u, buf))
			{
				EngStaticLoad_Why(why, whySize, "index read failed");
				return 0;
			}
		}
		else
		{
			for (k = 0; k < n * 3u; k++)
			{
				buf[k] = (u32)(t * 3u + k);
			}
		}
		for (k = 0; k < n; k++)
		{
			const u32 i0 = buf[3 * k + 0];
			const u32 i1 = buf[3 * k + 1];
			const u32 i2 = buf[3 * k + 2];

			if (i0 >= vcount || i1 >= vcount || i2 >= vcount)
			{
				EngStaticLoad_Why(why, whySize, "an index is past the vertex count");
				return 0;
			}
			if (!fn(ctx, (u32)t + k, i0, i1, i2))
			{
				return 0;
			}
		}
		t += n;
	}
	return 1;
}

internal int EngStaticLoad_MaterialOf(const EngineStaticModel *model, const cgltf_primitive *prim)
{
	const EngStaticLoadGltf *g = (const EngStaticLoadGltf *)model->gltf;
	const cgltf_data *d = g->data;

	if (prim->material != NULL && d->materials != NULL && prim->material >= d->materials &&
	    (size_t)(prim->material - d->materials) < d->materials_count)
	{
		return (int)(prim->material - d->materials);
	}
	return model->materialCount - 1;
}

// Every drawn primitive in scene order (the order of Open), with its node matrix.
internal int EngStaticLoad_EachPrim(const EngineStaticModel *model, EngStaticLoadPrimFn fn, void *ctx)
{
	const EngStaticLoadGltf *g = (const EngStaticLoadGltf *)model->gltf;
	u32 firstTri = 0;
	u32 ni;

	for (ni = 0; ni < g->nodeCount; ni++)
	{
		const cgltf_node *n = g->nodes[ni];
		EngStaticLoadPrimInfo pi;
		size_t pj;

		memset(&pi, 0, sizeof(pi));
		pi.model = model;
		pi.node = n;
		EngStaticLoad_NodeMatrix(n, pi.m);
		pi.mirrored = (EngStaticLoad_Det3(pi.m) < 0.0);
		for (pj = 0; pj < n->mesh->primitives_count; pj++)
		{
			const cgltf_primitive *prim = &n->mesh->primitives[pj];
			u64 tris;

			if (!EngStaticLoad_PrimCheck(model, prim, &pi.p, &tris))
			{
				continue;
			}
			pi.material = EngStaticLoad_MaterialOf(model, prim);
			pi.firstTri = firstTri;
			pi.tris = (u32)tris;
			if (!fn(ctx, &pi))
			{
				return 0;
			}
			firstTri += (u32)tris;
		}
	}
	return 1;
}

// ---------------------------------------------------------------------------
// Open / Close

internal int EngStaticLoad_WrapOf(cgltf_wrap_mode mode)
{
	if (mode == cgltf_wrap_mode_clamp_to_edge)
	{
		return 1;
	}
	if (mode == cgltf_wrap_mode_mirrored_repeat)
	{
		return 2;
	}
	return 0;
}

internal void EngStaticLoad_FillMaterial(const cgltf_data *d, const cgltf_material *m, EngineStaticMaterial *o)
{
	o->image = -1;
	o->wrapU = 0;
	o->wrapV = 0;
	o->baseColor[0] = 1.0f;
	o->baseColor[1] = 1.0f;
	o->baseColor[2] = 1.0f;
	o->baseColor[3] = 1.0f;
	o->alphaMode = ENGINE_STATIC_ALPHA_OPAQUE;
	o->alphaCutoff = 0.5f;
	o->doubleSided = 0;
	o->normalImage = -1;
	o->normalWrapU = 0;
	o->normalWrapV = 0;
	o->normalScale = 1.0f;
	o->unlit = 0;
	if (m == NULL)
	{
		return;
	}
	o->unlit = m->unlit ? 1 : 0;
	{
		const cgltf_texture_view *nv = &m->normal_texture;

		if (nv->texture != NULL && nv->texcoord == 0 && nv->texture->image != NULL && nv->texture->image >= d->images &&
		    (size_t)(nv->texture->image - d->images) < d->images_count)
		{
			o->normalImage = (int)(nv->texture->image - d->images);
			o->normalScale = (nv->scale != 0.0f) ? nv->scale : 1.0f;
			if (nv->texture->sampler != NULL)
			{
				o->normalWrapU = EngStaticLoad_WrapOf(nv->texture->sampler->wrap_s);
				o->normalWrapV = EngStaticLoad_WrapOf(nv->texture->sampler->wrap_t);
			}
		}
	}
	if (m->has_pbr_metallic_roughness)
	{
		const cgltf_texture_view *tv = &m->pbr_metallic_roughness.base_color_texture;
		int k;

		for (k = 0; k < 4; k++)
		{
			o->baseColor[k] = m->pbr_metallic_roughness.base_color_factor[k];
		}
		if (tv->texture != NULL && tv->texcoord == 0 && tv->texture->image != NULL && tv->texture->image >= d->images &&
		    (size_t)(tv->texture->image - d->images) < d->images_count)
		{
			o->image = (int)(tv->texture->image - d->images);
			if (tv->texture->sampler != NULL)
			{
				o->wrapU = EngStaticLoad_WrapOf(tv->texture->sampler->wrap_s);
				o->wrapV = EngStaticLoad_WrapOf(tv->texture->sampler->wrap_t);
			}
		}
	}
	if (m->alpha_mode == cgltf_alpha_mode_mask)
	{
		o->alphaMode = ENGINE_STATIC_ALPHA_MASK;
	}
	else if (m->alpha_mode == cgltf_alpha_mode_blend)
	{
		o->alphaMode = ENGINE_STATIC_ALPHA_BLEND;
	}
	o->alphaCutoff = m->alpha_cutoff;
	o->doubleSided = m->double_sided ? 1 : 0;
}

internal void EngStaticLoad_BoxAdd(float boxMin[3], float boxMax[3], const double p[3])
{
	int k;

	for (k = 0; k < 3; k++)
	{
		if ((float)p[k] < boxMin[k])
		{
			boxMin[k] = (float)p[k];
		}
		if ((float)p[k] > boxMax[k])
		{
			boxMax[k] = (float)p[k];
		}
	}
}

// The min/max of the POSITION accessor read from the source (when the file has none).
internal int EngStaticLoad_ScanMinMax(const EngineStaticModel *model, const cgltf_accessor *a, float mn[3], float mx[3])
{
	u32 elem;
	u32 stride;
	float tmp[ENGSL_CHUNK_ELEMS * 3];
	u64 done = 0;

	if (!EngStaticLoad_AccFits(model, a, &elem, &stride))
	{
		return 0;
	}
	mn[0] = mn[1] = mn[2] = FLT_MAX;
	mx[0] = mx[1] = mx[2] = -FLT_MAX;
	while (done < a->count)
	{
		u64 left = (u64)a->count - done;
		u32 n = (left > ENGSL_CHUNK_ELEMS) ? ENGSL_CHUNK_ELEMS : (u32)left;
		u32 i;

		if (!EngStaticLoad_ReadFloatChunk(model, a, elem, stride, done, n, tmp))
		{
			return 0;
		}
		for (i = 0; i < n; i++)
		{
			int k;

			for (k = 0; k < 3; k++)
			{
				if (tmp[3 * i + k] < mn[k])
				{
					mn[k] = tmp[3 * i + k];
				}
				if (tmp[3 * i + k] > mx[k])
				{
					mx[k] = tmp[3 * i + k];
				}
			}
		}
		done += n;
	}
	return 1;
}

int EngineStatic_Open(EngineStaticSource *src, EngineStaticModel *out, char *why, int whySize)
{
	u8 head[20];
	u8 binHead[8];
	u8 *block = NULL;
	cgltf_options options;
	cgltf_data *gdata = NULL;
	cgltf_result result;
	EngStaticLoadGltf *g;
	u32 total;
	u32 jsonLength;
	u64 triangles = 0;
	u32 ni;
	size_t i;

	if (why != NULL && whySize > 0)
	{
		why[0] = 0;
	}
	if (out == NULL)
	{
		return 0;
	}
	memset(out, 0, sizeof(*out));
	if (src == NULL || src->read == NULL || src->size < sizeof(head))
	{
		EngStaticLoad_Why(why, whySize, "file too short");
		return 0;
	}
	if (!src->read(src->user, 0, head, sizeof(head)))
	{
		EngStaticLoad_Why(why, whySize, "header read failed");
		return 0;
	}
	if (EngStaticLoad_LE32(head) != ENGSL_GLB_MAGIC)
	{
		EngStaticLoad_Why(why, whySize, "not a GLB (magic)");
		return 0;
	}
	if (EngStaticLoad_LE32(head + 4) != 2u)
	{
		EngStaticLoad_Why(why, whySize, "not a GLB (version %u, need 2)", EngStaticLoad_LE32(head + 4));
		return 0;
	}
	total = EngStaticLoad_LE32(head + 8);
	if (total < sizeof(head) || (u64)total > src->size)
	{
		EngStaticLoad_Why(why, whySize, "GLB length is past the end of the file");
		return 0;
	}
	jsonLength = EngStaticLoad_LE32(head + 12);
	if (EngStaticLoad_LE32(head + 16) != ENGSL_CHUNK_JSON)
	{
		EngStaticLoad_Why(why, whySize, "not a GLB (first chunk is not JSON)");
		return 0;
	}
	if (jsonLength == 0 || (u64)sizeof(head) + jsonLength > total)
	{
		EngStaticLoad_Why(why, whySize, "JSON chunk is past the end of the file");
		return 0;
	}
	if (jsonLength > ENGSL_JSON_MAX)
	{
		EngStaticLoad_Why(why, whySize, "JSON chunk too large");
		return 0;
	}

	// The BIN chunk, if the file has one: only where it is, never its bytes.
	if ((u64)sizeof(head) + jsonLength + sizeof(binHead) <= total)
	{
		if (!src->read(src->user, (u64)sizeof(head) + jsonLength, binHead, sizeof(binHead)))
		{
			EngStaticLoad_Why(why, whySize, "BIN header read failed");
			return 0;
		}
		if (EngStaticLoad_LE32(binHead + 4) == ENGSL_CHUNK_BIN)
		{
			const u64 start = (u64)sizeof(head) + jsonLength + sizeof(binHead);
			const u32 length = EngStaticLoad_LE32(binHead);

			if (start + length > total)
			{
				EngStaticLoad_Why(why, whySize, "BIN chunk is past the end of the file");
				return 0;
			}
			out->binOffset = start;
			out->binSize = length;
		}
	}

	// The header with the length cut to the JSON chunk, and the JSON chunk.
	block = (u8 *)EngStaticLoad_Alloc((u64)sizeof(head) + jsonLength);
	if (block == NULL)
	{
		EngStaticLoad_Why(why, whySize, "out of memory (JSON chunk)");
		return 0;
	}
	memcpy(block, head, sizeof(head));
	{
		const u32 cut = (u32)sizeof(head) + jsonLength;

		block[8] = (u8)(cut & 0xffu);
		block[9] = (u8)((cut >> 8) & 0xffu);
		block[10] = (u8)((cut >> 16) & 0xffu);
		block[11] = (u8)((cut >> 24) & 0xffu);
	}
	if (!src->read(src->user, sizeof(head), block + sizeof(head), jsonLength))
	{
		free(block);
		EngStaticLoad_Why(why, whySize, "JSON read failed");
		return 0;
	}
	memset(&options, 0, sizeof(options));
	result = cgltf_parse(&options, block, (cgltf_size)sizeof(head) + jsonLength, &gdata);
	if (result != cgltf_result_success || gdata == NULL)
	{
		free(block);
		EngStaticLoad_Why(why, whySize, "glTF JSON refused (cgltf result %d)", (int)result);
		return 0;
	}
	g = (EngStaticLoadGltf *)calloc(1, sizeof(*g));
	if (g == NULL)
	{
		cgltf_free(gdata);
		free(block);
		EngStaticLoad_Why(why, whySize, "out of memory");
		return 0;
	}
	g->data = gdata;
	g->block = block;
	out->gltf = g;
	out->src = src;

	// Materials: the file's, then the default one.
	out->materialCount = (int)gdata->materials_count + 1;
	out->materials = (EngineStaticMaterial *)calloc((size_t)out->materialCount, sizeof(EngineStaticMaterial));
	if (out->materials == NULL)
	{
		EngineStatic_Close(out);
		EngStaticLoad_Why(why, whySize, "out of memory (materials)");
		return 0;
	}
	for (i = 0; i < gdata->materials_count; i++)
	{
		EngStaticLoad_FillMaterial(gdata, &gdata->materials[i], &out->materials[i]);
	}
	EngStaticLoad_FillMaterial(gdata, NULL, &out->materials[gdata->materials_count]);
	out->imageCount = (int)gdata->images_count;

	if (!EngStaticLoad_BuildNodeList(g, why, whySize))
	{
		EngineStatic_Close(out);
		return 0;
	}

	out->boxMin[0] = out->boxMin[1] = out->boxMin[2] = FLT_MAX;
	out->boxMax[0] = out->boxMax[1] = out->boxMax[2] = -FLT_MAX;
	for (ni = 0; ni < g->nodeCount; ni++)
	{
		const cgltf_node *n = g->nodes[ni];
		float m[16];
		size_t pj;

		EngStaticLoad_NodeMatrix(n, m);
		if (EngStaticLoad_Det3(m) < 0.0)
		{
			out->mirroredNodes++;
		}
		for (pj = 0; pj < n->mesh->primitives_count; pj++)
		{
			EngStaticLoadPrim p;
			u64 tris;

			if (!EngStaticLoad_PrimCheck(out, &n->mesh->primitives[pj], &p, &tris))
			{
				out->skippedPrimitives++;
				continue;
			}
			out->primitives++;
			triangles += tris;
			if (p.uv == NULL)
			{
				out->noUvTriangles += (u32)tris;
			}
			if (tris > 0)
			{
				float mn[3];
				float mx[3];
				int k;
				int corner;
				int haveBox = p.pos->has_min && p.pos->has_max;

				for (k = 0; k < 3 && haveBox; k++)
				{
					haveBox = (p.pos->min[k] <= p.pos->max[k]);
				}
				if (haveBox)
				{
					for (k = 0; k < 3; k++)
					{
						mn[k] = p.pos->min[k];
						mx[k] = p.pos->max[k];
					}
				}
				else if (!EngStaticLoad_ScanMinMax(out, p.pos, mn, mx))
				{
					EngineStatic_Close(out);
					EngStaticLoad_Why(why, whySize, "position read failed");
					return 0;
				}
				for (corner = 0; corner < 8; corner++)
				{
					double in[3];
					double w[3];

					in[0] = (corner & 1) ? mx[0] : mn[0];
					in[1] = (corner & 2) ? mx[1] : mn[1];
					in[2] = (corner & 4) ? mx[2] : mn[2];
					EngStaticLoad_XformPoint(m, in, w);
					EngStaticLoad_BoxAdd(out->boxMin, out->boxMax, w);
				}
			}
		}
	}
	if (triangles == 0)
	{
		const u32 skipped = out->skippedPrimitives;

		EngineStatic_Close(out);
		EngStaticLoad_Why(why, whySize, "no triangles to draw (%u primitives skipped)", skipped);
		return 0;
	}
	if (triangles > 0xffffffffull)
	{
		EngineStatic_Close(out);
		EngStaticLoad_Why(why, whySize, "more than 4294967295 triangles");
		return 0;
	}
	out->triangles = (u32)triangles;
	return 1;
}

void EngineStatic_Close(EngineStaticModel *model)
{
	EngStaticLoadGltf *g;

	if (model == NULL)
	{
		return;
	}
	g = (EngStaticLoadGltf *)model->gltf;
	if (g != NULL)
	{
		if (g->data != NULL)
		{
			cgltf_free(g->data);
		}
		free((void *)g->nodes);
		free(g->block);
		free(g);
	}
	free(model->materials);
	memset(model, 0, sizeof(*model));
}

// ---------------------------------------------------------------------------
// BuildPieces

typedef struct
{
	EngineVertex *v;
	u32 vn;
	u32 *idx;
	u32 in;
	EngineRange *r;
	u32 rn;
	u32 tri;
	float bmin[3];
	float bmax[3];
} EngStaticLoadLeaf;

typedef struct
{
	u32 start;
	u32 count;
} EngStaticLoadSlice;

// Pass 1: the centre of every triangle.
typedef struct
{
	float *cen;
	u32 total;
	float *pos;
	u32 first;
	char *why;
	int whySize;
} EngStaticLoadP1;

internal int EngStaticLoad_P1Tri(void *vctx, u32 tri, u32 i0, u32 i1, u32 i2)
{
	EngStaticLoadP1 *c = (EngStaticLoadP1 *)vctx;
	const float *a = c->pos + 3 * (size_t)i0;
	const float *b = c->pos + 3 * (size_t)i1;
	const float *d = c->pos + 3 * (size_t)i2;
	float *o = c->cen + 3 * ((size_t)c->first + tri);
	int k;

	for (k = 0; k < 3; k++)
	{
		o[k] = (float)(((double)a[k] + (double)b[k] + (double)d[k]) / 3.0);
	}
	return 1;
}

internal int EngStaticLoad_P1Prim(void *vctx, const EngStaticLoadPrimInfo *pi)
{
	EngStaticLoadP1 *c = (EngStaticLoadP1 *)vctx;
	const u32 vc = (u32)pi->p.pos->count;
	int ok;

	if ((u64)pi->firstTri + pi->tris > c->total)
	{
		EngStaticLoad_Why(c->why, c->whySize, "triangle count differs from the file's");
		return 0;
	}
	c->pos = (float *)EngStaticLoad_Alloc((u64)vc * 12u);
	if (c->pos == NULL)
	{
		EngStaticLoad_Why(c->why, c->whySize, "out of memory (positions of a primitive)");
		return 0;
	}
	if (!EngStaticLoad_ReadFloats(pi->model, pi->p.pos, c->pos))
	{
		free(c->pos);
		c->pos = NULL;
		EngStaticLoad_Why(c->why, c->whySize, "position read failed");
		return 0;
	}
	EngStaticLoad_XformPoints(pi->m, c->pos, vc);
	c->first = pi->firstTri;
	ok = EngStaticLoad_EachTriangle(pi->model, &pi->p, EngStaticLoad_P1Tri, c, c->why, c->whySize);
	free(c->pos);
	c->pos = NULL;
	return ok;
}

// Pass 2: the pieces.
typedef struct
{
	EngStaticLoadLeaf *leaves;
	u32 leafCount;
	const u8 *leafId;
	int light;
	int normals;
	double lightDir[3];
	// the primitive in work
	const EngStaticLoadPrimInfo *pi;
	float *pos;
	float *uv;
	float *nrm;
	u8 *col;
	u32 *remapIndex;
	u8 *remapLeaf;
	double nmat[9];
	int hasNmat;
	int doubleSided;
	char *why;
	int whySize;
} EngStaticLoadP2;

internal void EngStaticLoad_MakeVertex(const EngStaticLoadP2 *c, u32 v, EngineVertex *o)
{
	int k;

	memset(o, 0, sizeof(*o));
	for (k = 0; k < 3; k++)
	{
		o->position[k] = c->pos[3 * (size_t)v + k];
	}
	if (c->uv != NULL)
	{
		o->texcoord[0] = c->uv[2 * (size_t)v + 0];
		o->texcoord[1] = c->uv[2 * (size_t)v + 1];
	}
	for (k = 0; k < 4; k++)
	{
		o->color[k] = (c->col != NULL) ? c->col[4 * (size_t)v + k] : 255;
	}
	if (c->nrm != NULL && c->hasNmat)
	{
		const double nx = c->nrm[3 * (size_t)v + 0];
		const double ny = c->nrm[3 * (size_t)v + 1];
		const double nz = c->nrm[3 * (size_t)v + 2];
		double n[3];
		double len;

		n[0] = c->nmat[0] * nx + c->nmat[1] * ny + c->nmat[2] * nz;
		n[1] = c->nmat[3] * nx + c->nmat[4] * ny + c->nmat[5] * nz;
		n[2] = c->nmat[6] * nx + c->nmat[7] * ny + c->nmat[8] * nz;
		len = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
		if (c->normals)
		{
			if (len > 1e-12)
			{
				for (k = 0; k < 3; k++)
				{
					int value = (int)((n[k] / len * 0.5 + 0.5) * 255.0 + 0.5);

					o->uvBox[k] = (u8)((value < 0) ? 0 : ((value > 255) ? 255 : value));
				}
				o->uvBox[3] = 255;
			}
			else
			{
				memset(o->uvBox, 0, sizeof(o->uvBox));
			}
		}
		if (c->light && len > 1e-12)
		{
			double d = (n[0] * c->lightDir[0] + n[1] * c->lightDir[1] + n[2] * c->lightDir[2]) / len;
			double factor;

			if (c->doubleSided)
			{
				d = fabs(d);
			}
			else if (d < 0.0)
			{
				d = 0.0;
			}
			factor = 0.55 + 0.45 * d;
			for (k = 0; k < 3; k++)
			{
				int value = (int)((double)o->color[k] * factor + 0.5);

				o->color[k] = (u8)((value < 0) ? 0 : ((value > 255) ? 255 : value));
			}
		}
	}
	if (!c->normals)
	{
		Engine_VertexBoxFull(o, 1);
	}
}

internal int EngStaticLoad_P2Tri(void *vctx, u32 tri, u32 i0, u32 i1, u32 i2)
{
	EngStaticLoadP2 *c = (EngStaticLoadP2 *)vctx;
	const u32 leafNo = c->leafId[c->pi->firstTri + tri];
	EngStaticLoadLeaf *lf;
	u32 corner[3];
	u32 ci;

	if (leafNo >= c->leafCount)
	{
		EngStaticLoad_Why(c->why, c->whySize, "internal error (piece number)");
		return 0;
	}
	lf = &c->leaves[leafNo];
	corner[0] = i0;
	corner[1] = c->pi->mirrored ? i2 : i1;
	corner[2] = c->pi->mirrored ? i1 : i2;

	// One range per run of triangles of one material.
	if (lf->rn > 0 && lf->r[lf->rn - 1].material == (u16)c->pi->material &&
	    lf->r[lf->rn - 1].firstIndex + lf->r[lf->rn - 1].indexCount == lf->in)
	{
		lf->r[lf->rn - 1].indexCount += 3;
	}
	else
	{
		EngineRange *r;

		if (lf->rn >= lf->tri)
		{
			EngStaticLoad_Why(c->why, c->whySize, "internal error (range capacity)");
			return 0;
		}
		r = &lf->r[lf->rn++];
		r->firstIndex = lf->in;
		r->indexCount = 3;
		r->material = (u16)c->pi->material;
	}
	for (ci = 0; ci < 3; ci++)
	{
		const u32 v = corner[ci];
		u32 at;

		if (c->remapLeaf[v] == (u8)leafNo)
		{
			at = c->remapIndex[v];
		}
		else
		{
			EngineVertex *o;
			int k;

			if (lf->vn >= 3u * lf->tri)
			{
				EngStaticLoad_Why(c->why, c->whySize, "internal error (vertex capacity)");
				return 0;
			}
			at = lf->vn++;
			o = &lf->v[at];
			EngStaticLoad_MakeVertex(c, v, o);
			for (k = 0; k < 3; k++)
			{
				if (at == 0 || o->position[k] < lf->bmin[k])
				{
					lf->bmin[k] = o->position[k];
				}
				if (at == 0 || o->position[k] > lf->bmax[k])
				{
					lf->bmax[k] = o->position[k];
				}
			}
			c->remapLeaf[v] = (u8)leafNo;
			c->remapIndex[v] = at;
		}
		lf->idx[lf->in++] = at;
	}
	return 1;
}

internal void EngStaticLoad_P2Free(EngStaticLoadP2 *c)
{
	free(c->pos);
	free(c->uv);
	free(c->nrm);
	free(c->col);
	free(c->remapIndex);
	free(c->remapLeaf);
	c->pos = NULL;
	c->uv = NULL;
	c->nrm = NULL;
	c->col = NULL;
	c->remapIndex = NULL;
	c->remapLeaf = NULL;
}

internal int EngStaticLoad_P2Prim(void *vctx, const EngStaticLoadPrimInfo *pi)
{
	EngStaticLoadP2 *c = (EngStaticLoadP2 *)vctx;
	const EngineStaticModel *model = pi->model;
	const u32 vc = (u32)pi->p.pos->count;
	int ok = 0;

	c->pi = pi;
	c->doubleSided = model->materials[pi->material].doubleSided;
	c->hasNmat = (c->light || c->normals) && pi->p.nrm != NULL && EngStaticLoad_NormalMatrix(pi->m, c->nmat);
	c->pos = (float *)EngStaticLoad_Alloc((u64)vc * 12u);
	c->uv = (pi->p.uv != NULL) ? (float *)EngStaticLoad_Alloc((u64)vc * 8u) : NULL;
	c->nrm = (c->hasNmat) ? (float *)EngStaticLoad_Alloc((u64)vc * 12u) : NULL;
	c->col = (pi->p.col != NULL) ? (u8 *)EngStaticLoad_Alloc((u64)vc * 4u) : NULL;
	c->remapIndex = (u32 *)EngStaticLoad_Alloc((u64)vc * 4u);
	c->remapLeaf = (u8 *)EngStaticLoad_Alloc(vc);
	if (c->pos == NULL || c->remapIndex == NULL || c->remapLeaf == NULL || (pi->p.uv != NULL && c->uv == NULL) ||
	    (c->hasNmat && c->nrm == NULL) || (pi->p.col != NULL && c->col == NULL))
	{
		EngStaticLoad_Why(c->why, c->whySize, "out of memory (attributes of a primitive)");
		EngStaticLoad_P2Free(c);
		return 0;
	}
	if (!EngStaticLoad_ReadFloats(model, pi->p.pos, c->pos) || (c->uv != NULL && !EngStaticLoad_ReadFloats(model, pi->p.uv, c->uv)) ||
	    (c->nrm != NULL && !EngStaticLoad_ReadFloats(model, pi->p.nrm, c->nrm)) ||
	    (c->col != NULL && !EngStaticLoad_ReadColors(model, pi->p.col, c->col)))
	{
		EngStaticLoad_Why(c->why, c->whySize, "attribute read failed");
		EngStaticLoad_P2Free(c);
		return 0;
	}
	EngStaticLoad_XformPoints(pi->m, c->pos, vc);
	memset(c->remapLeaf, 0xff, vc);
	ok = EngStaticLoad_EachTriangle(model, &pi->p, EngStaticLoad_P2Tri, c, c->why, c->whySize);
	EngStaticLoad_P2Free(c);
	return ok;
}

internal void EngStaticLoad_LeafFree(EngStaticLoadLeaf *lf)
{
	free(lf->v);
	free(lf->idx);
	free(lf->r);
	lf->v = NULL;
	lf->idx = NULL;
	lf->r = NULL;
}

int EngineStatic_BuildPieces(const EngineStaticModel *model, u32 maxTriangles, u32 flags, int (*onPiece)(void *user, const EngineStaticPiece *piece),
                             void *user, char *why, int whySize)
{
	EngStaticLoadSlice leafSlice[ENGINE_STATIC_PIECES_MAX];
	EngStaticLoadSlice splitStack[ENGSL_STACK_MAX];
	EngStaticLoadLeaf *leaves = NULL;
	EngStaticLoadP1 p1;
	EngStaticLoadP2 p2;
	float *cen = NULL;
	u32 *order = NULL;
	u32 *scratch = NULL;
	u8 *leafId = NULL;
	u32 total;
	u32 leafCount = 0;
	u32 sp = 0;
	u32 t;
	u32 k;
	int result = -1;

	if (why != NULL && whySize > 0)
	{
		why[0] = 0;
	}
	if (model == NULL || model->gltf == NULL || model->src == NULL || onPiece == NULL || model->triangles == 0)
	{
		EngStaticLoad_Why(why, whySize, "bad arguments");
		return -1;
	}
	if (maxTriangles == 0)
	{
		maxTriangles = 1;
	}
	if (maxTriangles > ENGINE_STATIC_PIECE_TRIANGLES)
	{
		maxTriangles = ENGINE_STATIC_PIECE_TRIANGLES;
	}
	total = model->triangles;

	// Pass 1: the centre of every triangle.
	cen = (float *)EngStaticLoad_Alloc((u64)total * 12u);
	order = (u32 *)EngStaticLoad_Alloc((u64)total * 4u);
	scratch = (u32 *)EngStaticLoad_Alloc((u64)total * 4u);
	leafId = (u8 *)EngStaticLoad_Alloc(total);
	if (cen == NULL || order == NULL || scratch == NULL || leafId == NULL)
	{
		EngStaticLoad_Why(why, whySize, "out of memory (%u triangles)", total);
		goto done;
	}
	memset(&p1, 0, sizeof(p1));
	p1.cen = cen;
	p1.total = total;
	p1.why = why;
	p1.whySize = whySize;
	if (!EngStaticLoad_EachPrim(model, EngStaticLoad_P1Prim, &p1))
	{
		if (why != NULL && whySize > 0 && why[0] == 0)
		{
			EngStaticLoad_Why(why, whySize, "read failed");
		}
		goto done;
	}

	// The split.
	for (t = 0; t < total; t++)
	{
		order[t] = t;
	}
	splitStack[sp].start = 0;
	splitStack[sp].count = total;
	sp++;
	while (sp > 0)
	{
		const EngStaticLoadSlice s = splitStack[--sp];
		double lo[3];
		double hi[3];
		double mid;
		u32 nl = 0;
		u32 nr = 0;
		int axis = 0;
		int a;

		if (s.count <= maxTriangles)
		{
			if (leafCount < ENGINE_STATIC_PIECES_MAX)
			{
				leafSlice[leafCount] = s;
			}
			leafCount++;
			continue;
		}
		lo[0] = lo[1] = lo[2] = DBL_MAX;
		hi[0] = hi[1] = hi[2] = -DBL_MAX;
		for (k = 0; k < s.count; k++)
		{
			const float *c = cen + 3 * (size_t)order[s.start + k];

			for (a = 0; a < 3; a++)
			{
				if (c[a] < lo[a])
				{
					lo[a] = c[a];
				}
				if (c[a] > hi[a])
				{
					hi[a] = c[a];
				}
			}
		}
		if (hi[1] - lo[1] > hi[axis] - lo[axis])
		{
			axis = 1;
		}
		if (hi[2] - lo[2] > hi[axis] - lo[axis])
		{
			axis = 2;
		}
		mid = (lo[axis] + hi[axis]) * 0.5;
		for (k = 0; k < s.count; k++)
		{
			const u32 tri = order[s.start + k];

			if ((double)cen[3 * (size_t)tri + axis] < mid)
			{
				order[s.start + nl++] = tri;
			}
			else
			{
				scratch[nr++] = tri;
			}
		}
		if (nl == 0 || nr == 0)
		{
			// One side is empty: nothing moved; split by count.
			nl = s.count / 2;
			nr = s.count - nl;
		}
		else
		{
			memcpy(order + s.start + nl, scratch, (size_t)nr * sizeof(u32));
		}
		if (sp + 2 > ENGSL_STACK_MAX)
		{
			EngStaticLoad_Why(why, whySize, "split too deep");
			goto done;
		}
		splitStack[sp].start = s.start + nl;
		splitStack[sp].count = nr;
		sp++;
		splitStack[sp].start = s.start;
		splitStack[sp].count = nl;
		sp++;
	}
	if (leafCount > ENGINE_STATIC_PIECES_MAX)
	{
		EngStaticLoad_Why(why, whySize, "the model needs %u pieces, at most %d", leafCount, ENGINE_STATIC_PIECES_MAX);
		goto done;
	}
	for (k = 0; k < leafCount; k++)
	{
		u32 i;

		for (i = 0; i < leafSlice[k].count; i++)
		{
			leafId[order[leafSlice[k].start + i]] = (u8)k;
		}
	}
	free(cen);
	free(order);
	free(scratch);
	cen = NULL;
	order = NULL;
	scratch = NULL;

	// Pass 2: the arrays of every piece, exact capacity.
	leaves = (EngStaticLoadLeaf *)calloc(leafCount, sizeof(*leaves));
	if (leaves == NULL)
	{
		EngStaticLoad_Why(why, whySize, "out of memory");
		goto done;
	}
	for (k = 0; k < leafCount; k++)
	{
		EngStaticLoadLeaf *lf = &leaves[k];

		lf->tri = leafSlice[k].count;
		lf->v = (EngineVertex *)EngStaticLoad_Alloc((u64)lf->tri * 3u * sizeof(EngineVertex));
		lf->idx = (u32 *)EngStaticLoad_Alloc((u64)lf->tri * 3u * sizeof(u32));
		lf->r = (EngineRange *)calloc(lf->tri, sizeof(EngineRange));
		if (lf->v == NULL || lf->idx == NULL || lf->r == NULL)
		{
			EngStaticLoad_Why(why, whySize, "out of memory (piece %u of %u, %u triangles)", k, leafCount, lf->tri);
			goto done;
		}
	}
	memset(&p2, 0, sizeof(p2));
	p2.leaves = leaves;
	p2.leafCount = leafCount;
	p2.leafId = leafId;
	p2.light = ((flags & ENGINE_STATIC_BUILD_LIGHT) != 0u) ? 1 : 0;
	p2.normals = ((flags & ENGINE_STATIC_BUILD_NORMALS) != 0u) ? 1 : 0;
	{
		const double len = sqrt(0.35 * 0.35 + 0.85 * 0.85 + 0.40 * 0.40);

		p2.lightDir[0] = 0.35 / len;
		p2.lightDir[1] = 0.85 / len;
		p2.lightDir[2] = 0.40 / len;
	}
	p2.why = why;
	p2.whySize = whySize;
	if (!EngStaticLoad_EachPrim(model, EngStaticLoad_P2Prim, &p2))
	{
		if (why != NULL && whySize > 0 && why[0] == 0)
		{
			EngStaticLoad_Why(why, whySize, "read failed");
		}
		goto done;
	}
	for (k = 0; k < leafCount; k++)
	{
		EngStaticLoadLeaf *lf = &leaves[k];
		void *shrunk;

		if (lf->in != 3u * lf->tri || lf->vn == 0)
		{
			EngStaticLoad_Why(why, whySize, "internal error (piece %u holds %u indices for %u triangles)", k, lf->in, lf->tri);
			goto done;
		}
		// Give back the capacity the vertex sharing did not need.
		shrunk = realloc(lf->v, (size_t)lf->vn * sizeof(EngineVertex));
		if (shrunk != NULL)
		{
			lf->v = (EngineVertex *)shrunk;
		}
		shrunk = realloc(lf->r, (size_t)lf->rn * sizeof(EngineRange));
		if (shrunk != NULL)
		{
			lf->r = (EngineRange *)shrunk;
		}
	}

	// Hand the pieces on, one at a time.
	for (k = 0; k < leafCount; k++)
	{
		EngStaticLoadLeaf *lf = &leaves[k];
		EngineStaticPiece piece;
		int go;

		piece.vertices = lf->v;
		piece.vertexCount = lf->vn;
		piece.indices = lf->idx;
		piece.indexCount = lf->in;
		piece.ranges = lf->r;
		piece.rangeCount = lf->rn;
		memcpy(piece.boxMin, lf->bmin, sizeof(piece.boxMin));
		memcpy(piece.boxMax, lf->bmax, sizeof(piece.boxMax));
		go = onPiece(user, &piece);
		EngStaticLoad_LeafFree(lf);
		if (!go)
		{
			EngStaticLoad_Why(why, whySize, "stopped");
			goto done;
		}
	}
	result = (int)leafCount;

done:
	if (leaves != NULL)
	{
		for (k = 0; k < leafCount; k++)
		{
			EngStaticLoad_LeafFree(&leaves[k]);
		}
		free(leaves);
	}
	free(cen);
	free(order);
	free(scratch);
	free(leafId);
	return result;
}

// ---------------------------------------------------------------------------
// Images

int EngineStatic_UpEdge(int edge)
{
	int e = 16;

	if (edge <= 0)
	{
		return 0;
	}
	while (e < edge)
	{
		e *= 2;
	}
	return (e > 2048) ? 0 : e;
}

int EngineStatic_ResampleUp(const u8 *src, int sw, int sh, u8 *dst, int dw, int dh)
{
	int x;
	int y;
	int c;

	if (src == NULL || dst == NULL || sw <= 0 || sh <= 0 || dw < sw || dh < sh)
	{
		return 0;
	}
	for (y = 0; y < dh; y++)
	{
		double sy = ((double)y + 0.5) * (double)sh / (double)dh - 0.5;
		int y0;
		int y1;
		double fy;

		sy = (sy < 0.0) ? 0.0 : ((sy > (double)(sh - 1)) ? (double)(sh - 1) : sy);
		y0 = (int)sy;
		y1 = (y0 + 1 < sh) ? y0 + 1 : y0;
		fy = sy - (double)y0;
		for (x = 0; x < dw; x++)
		{
			double sx = ((double)x + 0.5) * (double)sw / (double)dw - 0.5;
			int x0;
			int x1;
			double fx;

			sx = (sx < 0.0) ? 0.0 : ((sx > (double)(sw - 1)) ? (double)(sw - 1) : sx);
			x0 = (int)sx;
			x1 = (x0 + 1 < sw) ? x0 + 1 : x0;
			fx = sx - (double)x0;
			for (c = 0; c < 4; c++)
			{
				const double a = src[((size_t)y0 * sw + x0) * 4 + c];
				const double b = src[((size_t)y0 * sw + x1) * 4 + c];
				const double d = src[((size_t)y1 * sw + x0) * 4 + c];
				const double e = src[((size_t)y1 * sw + x1) * 4 + c];
				const double top = a * (1.0 - fx) + b * fx;
				const double bottom = d * (1.0 - fx) + e * fx;
				int v = (int)(top * (1.0 - fy) + bottom * fy + 0.5);

				dst[((size_t)y * dw + x) * 4 + c] = (u8)((v < 0) ? 0 : ((v > 255) ? 255 : v));
			}
		}
	}
	return 1;
}

// A uri beside the .glb: no data uris, no absolute paths, no schemes.
internal int EngStaticLoad_UriIsLocal(const char *uri)
{
	size_t i;

	if (uri == NULL || uri[0] == 0 || uri[0] == '/' || uri[0] == '\\')
	{
		return 0;
	}
	for (i = 0; uri[i] != 0; i++)
	{
		if (uri[i] == ':')
		{
			return 0;
		}
	}
	return 1;
}

// The bytes of an image: the BIN buffer view, or a file beside the .glb. NULL with why on failure.
internal u8 *EngStaticLoad_ImageBytes(const EngineStaticModel *model, const cgltf_image *img, size_t *outSize, char *why, int whySize)
{
	const EngStaticLoadGltf *g = (const EngStaticLoadGltf *)model->gltf;
	u8 *bytes = NULL;

	*outSize = 0;
	if (img->buffer_view != NULL)
	{
		const cgltf_buffer_view *v = img->buffer_view;

		if (g->data->buffers_count == 0 || v->buffer != &g->data->buffers[0] || g->data->buffers[0].uri != NULL || v->size == 0 ||
		    (u64)v->offset > model->binSize || (u64)v->size > model->binSize - (u64)v->offset || v->size > ENGSL_IMAGE_MAX)
		{
			EngStaticLoad_Why(why, whySize, "image is not inside the BIN chunk");
			return NULL;
		}
		bytes = (u8 *)EngStaticLoad_Alloc((u64)v->size);
		if (bytes == NULL)
		{
			EngStaticLoad_Why(why, whySize, "out of memory (image)");
			return NULL;
		}
		if (!model->src->read(model->src->user, model->binOffset + (u64)v->offset, bytes, (u32)v->size))
		{
			free(bytes);
			EngStaticLoad_Why(why, whySize, "image read failed");
			return NULL;
		}
		*outSize = (size_t)v->size;
		return bytes;
	}
	if (img->uri != NULL)
	{
		const char *dir = (model->src->baseDir != NULL) ? model->src->baseDir : "";
		size_t uriLen = strlen(img->uri);
		size_t dirLen = strlen(dir);
		char *path;
		FILE *fp;
		long long size;

		if (!EngStaticLoad_UriIsLocal(img->uri))
		{
			EngStaticLoad_Why(why, whySize, "image uri is not a local file name");
			return NULL;
		}
		path = (char *)malloc(dirLen + uriLen + 2);
		if (path == NULL)
		{
			EngStaticLoad_Why(why, whySize, "out of memory");
			return NULL;
		}
		memcpy(path, dir, dirLen);
		path[dirLen] = '/';
		memcpy(path + dirLen + 1, img->uri, uriLen + 1);
		cgltf_decode_uri(path + dirLen + 1);
		fp = fopen(path, "rb");
		free(path);
		if (fp == NULL)
		{
			EngStaticLoad_Why(why, whySize, "image file does not open");
			return NULL;
		}
		if (ENGSL_SEEK(fp, 0, SEEK_END) != 0 || (size = ENGSL_TELL(fp)) <= 0 || (u64)size > ENGSL_IMAGE_MAX || ENGSL_SEEK(fp, 0, SEEK_SET) != 0)
		{
			fclose(fp);
			EngStaticLoad_Why(why, whySize, "image file size not usable");
			return NULL;
		}
		bytes = (u8 *)EngStaticLoad_Alloc((u64)size);
		if (bytes == NULL || fread(bytes, 1, (size_t)size, fp) != (size_t)size)
		{
			free(bytes);
			fclose(fp);
			EngStaticLoad_Why(why, whySize, "image file read failed");
			return NULL;
		}
		fclose(fp);
		*outSize = (size_t)size;
		return bytes;
	}
	EngStaticLoad_Why(why, whySize, "image has no data");
	return NULL;
}

int EngineStatic_ReadImage(const EngineStaticModel *model, int image, EngineStaticImage *out, char *why, int whySize)
{
	static const u8 pngSignature[8] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
	const EngStaticLoadGltf *g;
	struct RldPngImage png;
	const char *pngWhy = NULL;
	u8 *bytes;
	size_t size = 0;
	int w;
	int h;
	int code;

	if (why != NULL && whySize > 0)
	{
		why[0] = 0;
	}
	if (out == NULL)
	{
		return 0;
	}
	memset(out, 0, sizeof(*out));
	if (model == NULL || model->gltf == NULL || model->src == NULL)
	{
		EngStaticLoad_Why(why, whySize, "no model");
		return 0;
	}
	g = (const EngStaticLoadGltf *)model->gltf;
	if (image < 0 || image >= model->imageCount)
	{
		EngStaticLoad_Why(why, whySize, "no such image");
		return 0;
	}
	bytes = EngStaticLoad_ImageBytes(model, &g->data->images[image], &size, why, whySize);
	if (bytes == NULL)
	{
		return 0;
	}
	if (size < sizeof(pngSignature) || memcmp(bytes, pngSignature, sizeof(pngSignature)) != 0)
	{
		free(bytes);
		EngStaticLoad_Why(why, whySize, "not PNG");
		return 0;
	}
	memset(&png, 0, sizeof(png));
	code = RldPng_Decode(bytes, size, &png, &pngWhy);
	free(bytes);
	if (code != RLDPNG_OK)
	{
		EngStaticLoad_Why(why, whySize, "PNG refused: %s", (pngWhy != NULL) ? pngWhy : "unknown");
		return 0;
	}
	if (png.width > 2048u || png.height > 2048u)
	{
		RldPng_Free(&png);
		EngStaticLoad_Why(why, whySize, "edge above 2048");
		return 0;
	}
	out->fileWidth = (int)png.width;
	out->fileHeight = (int)png.height;
	w = EngineStatic_UpEdge(out->fileWidth);
	h = EngineStatic_UpEdge(out->fileHeight);
	if (w == 0 || h == 0)
	{
		RldPng_Free(&png);
		memset(out, 0, sizeof(*out));
		EngStaticLoad_Why(why, whySize, "edge above 2048");
		return 0;
	}
	out->width = w;
	out->height = h;
	if (w == out->fileWidth && h == out->fileHeight)
	{
		out->rgba = png.rgba; // ownership moves to the caller
		png.rgba = NULL;
		out->resampled = 0;
		return 1;
	}
	out->rgba = (u8 *)EngStaticLoad_Alloc((u64)w * (u64)h * 4u);
	if (out->rgba == NULL || !EngineStatic_ResampleUp(png.rgba, out->fileWidth, out->fileHeight, out->rgba, w, h))
	{
		free(out->rgba);
		RldPng_Free(&png);
		memset(out, 0, sizeof(*out));
		EngStaticLoad_Why(why, whySize, "out of memory (image)");
		return 0;
	}
	RldPng_Free(&png);
	out->resampled = 1;
	return 1;
}

void EngineStatic_FreeImage(EngineStaticImage *img)
{
	if (img == NULL)
	{
		return;
	}
	free(img->rgba);
	memset(img, 0, sizeof(*img));
}

// ---------------------------------------------------------------------------
// Selftest: a made-up model built in memory (no game data, no files)

typedef struct
{
	const u8 *data;
	u64 size;
} EngStaticLoadMem;

typedef struct
{
	u32 pieces;
	u32 triangles;
	u32 maxPieceTriangles;
	int sizesOk;
	int rangesOk;
	int indicesOk;
	int boxOk;
	u32 hash;
	int keep;
	EngineVertex *keptVertices;
	u32 keptVertexCount;
	u32 *keptIndices;
	u32 keptIndexCount;
	int stop;
} EngStaticLoadTestPieces;

internal void EngStaticLoadTest_Check(int ok, const char *what, int *checks, int *failures)
{
	(*checks)++;
	if (!ok)
	{
		(*failures)++;
		printf("engine selftest: static load %s\n", what);
	}
}

internal int EngStaticLoadTest_MemRead(void *user, u64 offset, void *dst, u32 bytes)
{
	const EngStaticLoadMem *m = (const EngStaticLoadMem *)user;

	if (offset > m->size || (u64)bytes > m->size - offset)
	{
		return 0;
	}
	memcpy(dst, m->data + offset, bytes);
	return 1;
}

internal void EngStaticLoadTest_PutLE32(u8 *p, u32 v)
{
	p[0] = (u8)(v & 0xffu);
	p[1] = (u8)((v >> 8) & 0xffu);
	p[2] = (u8)((v >> 16) & 0xffu);
	p[3] = (u8)((v >> 24) & 0xffu);
}

internal void EngStaticLoadTest_PutBE32(u8 *p, u32 v)
{
	p[0] = (u8)((v >> 24) & 0xffu);
	p[1] = (u8)((v >> 16) & 0xffu);
	p[2] = (u8)((v >> 8) & 0xffu);
	p[3] = (u8)(v & 0xffu);
}

// A GLB: header, JSON chunk (padded with spaces), BIN chunk (padded with zeros). Free with free().
internal u8 *EngStaticLoadTest_Glb(const char *json, const u8 *bin, u32 binSize, u32 *outSize)
{
	const u32 jl = (u32)strlen(json);
	const u32 jp = (jl + 3u) & ~3u;
	const u32 bp = (binSize + 3u) & ~3u;
	const u32 total = 12u + 8u + jp + ((binSize > 0) ? 8u + bp : 0u);
	u8 *glb = (u8 *)calloc(1, total);

	if (glb == NULL)
	{
		return NULL;
	}
	EngStaticLoadTest_PutLE32(glb, ENGSL_GLB_MAGIC);
	EngStaticLoadTest_PutLE32(glb + 4, 2u);
	EngStaticLoadTest_PutLE32(glb + 8, total);
	EngStaticLoadTest_PutLE32(glb + 12, jp);
	EngStaticLoadTest_PutLE32(glb + 16, ENGSL_CHUNK_JSON);
	memset(glb + 20, ' ', jp);
	memcpy(glb + 20, json, jl);
	if (binSize > 0)
	{
		EngStaticLoadTest_PutLE32(glb + 20 + jp, bp);
		EngStaticLoadTest_PutLE32(glb + 24 + jp, ENGSL_CHUNK_BIN);
		memcpy(glb + 28 + jp, bin, binSize);
	}
	*outSize = total;
	return glb;
}

internal u32 EngStaticLoadTest_Crc32(const u8 *d, u32 n)
{
	u32 crc = 0xffffffffu;
	u32 i;
	int b;

	for (i = 0; i < n; i++)
	{
		crc ^= d[i];
		for (b = 0; b < 8; b++)
		{
			crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
		}
	}
	return ~crc;
}

// One PNG chunk (length, type, data, CRC of type + data); returns its size.
internal u32 EngStaticLoadTest_PngChunk(u8 *out, const char *type, const u8 *body, u32 n)
{
	EngStaticLoadTest_PutBE32(out, n);
	memcpy(out + 4, type, 4);
	if (n > 0)
	{
		memcpy(out + 8, body, n);
	}
	EngStaticLoadTest_PutBE32(out + 8 + n, EngStaticLoadTest_Crc32(out + 4, 4 + n));
	return 12u + n;
}

// A 3 x 2 RGBA8 PNG with an uncompressed (stored) deflate block; returns its size.
internal u32 EngStaticLoadTest_Png(u8 *out, const u8 rgba[24])
{
	static const u8 sig[8] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
	u8 ihdr[13];
	u8 idat[2 + 5 + 26 + 4];
	u8 raw[26];
	u32 a = 1;
	u32 b = 0;
	u32 n = 0;
	u32 i;

	for (i = 0; i < 2; i++)
	{
		raw[i * 13] = 0; // filter none
		memcpy(raw + i * 13 + 1, rgba + i * 12, 12);
	}
	for (i = 0; i < 26; i++)
	{
		a = (a + raw[i]) % 65521u;
		b = (b + a) % 65521u;
	}
	idat[0] = 0x78;
	idat[1] = 0x01;
	idat[2] = 0x01; // final stored block
	idat[3] = 26;
	idat[4] = 0;
	idat[5] = (u8)(~26u & 0xffu);
	idat[6] = 0xff;
	memcpy(idat + 7, raw, 26);
	EngStaticLoadTest_PutBE32(idat + 33, (b << 16) | a);

	memset(ihdr, 0, sizeof(ihdr));
	EngStaticLoadTest_PutBE32(ihdr, 3);
	EngStaticLoadTest_PutBE32(ihdr + 4, 2);
	ihdr[8] = 8;
	ihdr[9] = 6;
	memcpy(out, sig, 8);
	n = 8;
	n += EngStaticLoadTest_PngChunk(out + n, "IHDR", ihdr, sizeof(ihdr));
	n += EngStaticLoadTest_PngChunk(out + n, "IDAT", idat, sizeof(idat));
	n += EngStaticLoadTest_PngChunk(out + n, "IEND", NULL, 0);
	return n;
}

internal u32 EngStaticLoadTest_Fnv(u32 h, const void *bytes0, size_t n)
{
	const u8 *p = (const u8 *)bytes0;
	size_t i;

	for (i = 0; i < n; i++)
	{
		h ^= p[i];
		h *= 16777619u;
	}
	return h;
}

internal int EngStaticLoadTest_OnPiece(void *user, const EngineStaticPiece *p)
{
	EngStaticLoadTestPieces *t = (EngStaticLoadTestPieces *)user;
	u32 expect = 0;
	u32 i;
	int k;

	t->pieces++;
	if (p->indexCount == 0 || p->indexCount % 3u != 0 || p->vertexCount == 0)
	{
		t->sizesOk = 0;
	}
	t->triangles += p->indexCount / 3u;
	if (p->indexCount / 3u > t->maxPieceTriangles)
	{
		t->maxPieceTriangles = p->indexCount / 3u;
	}
	for (i = 0; i < p->rangeCount; i++)
	{
		if (p->ranges[i].firstIndex != expect || p->ranges[i].indexCount == 0 || p->ranges[i].indexCount % 3u != 0)
		{
			t->rangesOk = 0;
		}
		expect += p->ranges[i].indexCount;
	}
	if (expect != p->indexCount || p->rangeCount == 0)
	{
		t->rangesOk = 0;
	}
	for (i = 0; i < p->indexCount; i++)
	{
		if (p->indices[i] >= p->vertexCount)
		{
			t->indicesOk = 0;
		}
	}
	for (i = 0; i < p->vertexCount; i++)
	{
		for (k = 0; k < 3; k++)
		{
			if (p->vertices[i].position[k] < p->boxMin[k] || p->vertices[i].position[k] > p->boxMax[k])
			{
				t->boxOk = 0;
			}
		}
	}
	t->hash = EngStaticLoadTest_Fnv(t->hash, &p->vertexCount, sizeof(p->vertexCount));
	t->hash = EngStaticLoadTest_Fnv(t->hash, p->vertices, (size_t)p->vertexCount * sizeof(EngineVertex));
	t->hash = EngStaticLoadTest_Fnv(t->hash, p->indices, (size_t)p->indexCount * sizeof(u32));
	for (i = 0; i < p->rangeCount; i++)
	{
		t->hash = EngStaticLoadTest_Fnv(t->hash, &p->ranges[i].firstIndex, sizeof(u32));
		t->hash = EngStaticLoadTest_Fnv(t->hash, &p->ranges[i].indexCount, sizeof(u32));
		t->hash = EngStaticLoadTest_Fnv(t->hash, &p->ranges[i].material, sizeof(u16));
	}
	if (t->keep && t->keptVertices == NULL)
	{
		t->keptVertices = (EngineVertex *)malloc((size_t)p->vertexCount * sizeof(EngineVertex));
		t->keptIndices = (u32 *)malloc((size_t)p->indexCount * sizeof(u32));
		if (t->keptVertices != NULL && t->keptIndices != NULL)
		{
			memcpy(t->keptVertices, p->vertices, (size_t)p->vertexCount * sizeof(EngineVertex));
			memcpy(t->keptIndices, p->indices, (size_t)p->indexCount * sizeof(u32));
			t->keptVertexCount = p->vertexCount;
			t->keptIndexCount = p->indexCount;
		}
	}
	return !t->stop;
}

internal void EngStaticLoadTest_PiecesInit(EngStaticLoadTestPieces *t, int keep)
{
	memset(t, 0, sizeof(*t));
	t->sizesOk = 1;
	t->rangesOk = 1;
	t->indicesOk = 1;
	t->boxOk = 1;
	t->hash = 2166136261u;
	t->keep = keep;
}

internal void EngStaticLoadTest_PiecesFree(EngStaticLoadTestPieces *t)
{
	free(t->keptVertices);
	free(t->keptIndices);
	t->keptVertices = NULL;
	t->keptIndices = NULL;
}

internal int EngStaticLoadTest_Near(float a, float b)
{
	return fabsf(a - b) < 1e-3f;
}

internal void EngStaticLoadTest_Cross(const float a[3], const float b[3], float out[3])
{
	out[0] = a[1] * b[2] - a[2] * b[1];
	out[1] = a[2] * b[0] - a[0] * b[2];
	out[2] = a[0] * b[1] - a[1] * b[0];
}

// A triangle in the plane of unit normal n (counter-clockwise seen from n's side) at base.
internal void EngStaticLoadTest_Tri(const float n[3], const float base[3], float out[9])
{
	float h[3] = {0.0f, 0.0f, 1.0f};
	float a[3];
	float b[3];
	float len;
	int k;

	if (fabsf(n[2]) > 0.9f)
	{
		h[0] = 1.0f;
		h[1] = 0.0f;
		h[2] = 0.0f;
	}
	EngStaticLoadTest_Cross(n, h, a);
	len = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
	for (k = 0; k < 3; k++)
	{
		a[k] /= len;
	}
	EngStaticLoadTest_Cross(n, a, b);
	for (k = 0; k < 3; k++)
	{
		out[k] = base[k];
		out[3 + k] = base[k] + a[k];
		out[6 + k] = base[k] + b[k];
	}
}

int Engine_SelfTestStaticLoad(int *checks)
{
	enum
	{
		GRID = 40,
		GRID_VERTS = (GRID + 1) * (GRID + 1),
		GRID_TRIS = GRID * GRID * 2
	};
	int failures = 0;
	const float L[3] = {0.35f / 1.0025f, 0.85f / 1.0025f, 0.40f / 1.0025f};
	u8 rgba[24] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 0, 200, 0, 255, 255, 100, 255, 0, 255, 50};
	u8 png[160];
	u32 pngSize;
	float *gp = (float *)malloc(GRID_VERTS * 12);
	float *gt = (float *)malloc(GRID_VERTS * 8);
	float *gn = (float *)malloc(GRID_VERTS * 12);
	u16 *gi = (u16 *)malloc(GRID_TRIS * 3 * 2);
	u8 *bin = (u8 *)calloc(1, 160 * 1024);
	char *json = (char *)malloc(8192);
	float bp[18];
	float bn[18];
	u8 bc[24];
	u32 binSize = 0;
	u32 off[8];
	u32 len[8];
	u32 glbSize = 0;
	u8 *glb = NULL;
	EngStaticLoadMem mem;
	EngineStaticSource src;
	EngineStaticModel model;
	char why[200];
	int i;
	int j;

	if (gp == NULL || gt == NULL || gn == NULL || gi == NULL || bin == NULL || json == NULL)
	{
		free(gp);
		free(gt);
		free(gn);
		free(gi);
		free(bin);
		free(json);
		(*checks)++;
		printf("engine selftest: static load out of memory\n");
		return 1;
	}

	// ---- the made-up model -------------------------------------------------
	for (j = 0; j <= GRID; j++)
	{
		for (i = 0; i <= GRID; i++)
		{
			const int v = j * (GRID + 1) + i;

			gp[3 * v + 0] = (float)i;
			gp[3 * v + 1] = 0.0f;
			gp[3 * v + 2] = (float)j;
			gt[2 * v + 0] = (float)i / (float)GRID;
			gt[2 * v + 1] = (float)j / (float)GRID;
			gn[3 * v + 0] = 0.0f;
			gn[3 * v + 1] = 1.0f;
			gn[3 * v + 2] = 0.0f;
		}
	}
	for (j = 0; j < GRID; j++)
	{
		for (i = 0; i < GRID; i++)
		{
			const int q = (j * GRID + i) * 6;
			const int v00 = j * (GRID + 1) + i;

			gi[q + 0] = (u16)v00;
			gi[q + 1] = (u16)(v00 + 1);
			gi[q + 2] = (u16)(v00 + GRID + 2);
			gi[q + 3] = (u16)v00;
			gi[q + 4] = (u16)(v00 + GRID + 2);
			gi[q + 5] = (u16)(v00 + GRID + 1);
		}
	}
	{
		// Node B has scale (-1, 1, 1): its triangles are authored so that the normal
		// the node matrix gives (-x, y, z) is +L for the first and -L for the second.
		const float n1[3] = {-L[0], L[1], L[2]};
		const float n2[3] = {L[0], -L[1], -L[2]};
		const float base1[3] = {5.0f, 0.0f, 0.0f};
		const float base2[3] = {8.0f, 2.0f, 1.0f};

		EngStaticLoadTest_Tri(n1, base1, bp);
		EngStaticLoadTest_Tri(n2, base2, bp + 9);
		for (i = 0; i < 3; i++)
		{
			for (j = 0; j < 3; j++)
			{
				bn[3 * i + j] = n1[j];
				bn[9 + 3 * i + j] = n2[j];
			}
		}
		for (i = 0; i < 6; i++)
		{
			bc[4 * i + 0] = 200;
			bc[4 * i + 1] = 100;
			bc[4 * i + 2] = 40;
			bc[4 * i + 3] = 128;
		}
	}
	pngSize = EngStaticLoadTest_Png(png, rgba);

	{
		const void *srcs[8] = {gp, gt, gn, gi, bp, bn, bc, png};
		const u32 sizes[8] = {GRID_VERTS * 12, GRID_VERTS * 8, GRID_VERTS * 12, GRID_TRIS * 3 * 2, 72, 72, 24, pngSize};
		float bmin[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
		float bmax[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
		int k;

		for (i = 0; i < 8; i++)
		{
			off[i] = binSize;
			len[i] = sizes[i];
			memcpy(bin + binSize, srcs[i], sizes[i]);
			binSize += (sizes[i] + 3u) & ~3u;
		}
		for (i = 0; i < 6; i++)
		{
			for (k = 0; k < 3; k++)
			{
				if (bp[3 * i + k] < bmin[k])
				{
					bmin[k] = bp[3 * i + k];
				}
				if (bp[3 * i + k] > bmax[k])
				{
					bmax[k] = bp[3 * i + k];
				}
			}
		}
		snprintf(json, 8192,
		         "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0,1]}],"
		         "\"nodes\":[{\"mesh\":0,\"translation\":[100,0,0]},{\"mesh\":1,\"scale\":[-1,1,1]}],"
		         "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"TEXCOORD_0\":1,\"NORMAL\":2},\"indices\":3,\"material\":0}]},"
		         "{\"primitives\":[{\"attributes\":{\"POSITION\":4,\"NORMAL\":5,\"COLOR_0\":6},\"material\":1}]}],"
		         "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0},\"baseColorFactor\":[1,0.5,0.25,0.75]},"
		         "\"normalTexture\":{\"index\":1,\"scale\":0.5},\"alphaMode\":\"MASK\",\"alphaCutoff\":0.25,\"doubleSided\":true},"
		         "{\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,1,1,1]},\"extensions\":{\"KHR_materials_unlit\":{}}}],"
		         "\"extensionsUsed\":[\"KHR_materials_unlit\"],"
		         "\"textures\":[{\"sampler\":0,\"source\":0},{\"sampler\":0,\"source\":0}],\"samplers\":[{\"wrapS\":33071,\"wrapT\":33648}],"
		         "\"images\":[{\"bufferView\":7,\"mimeType\":\"image/png\"}],"
		         "\"buffers\":[{\"byteLength\":%u}],"
		         "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u},{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u},"
		         "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u},{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u},"
		         "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u},{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u},"
		         "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u},{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}],"
		         "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":%d,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[%d,0,%d]},"
		         "{\"bufferView\":1,\"componentType\":5126,\"count\":%d,\"type\":\"VEC2\"},"
		         "{\"bufferView\":2,\"componentType\":5126,\"count\":%d,\"type\":\"VEC3\"},"
		         "{\"bufferView\":3,\"componentType\":5123,\"count\":%d,\"type\":\"SCALAR\"},"
		         "{\"bufferView\":4,\"componentType\":5126,\"count\":6,\"type\":\"VEC3\",\"min\":[%.9g,%.9g,%.9g],\"max\":[%.9g,%.9g,%.9g]},"
		         "{\"bufferView\":5,\"componentType\":5126,\"count\":6,\"type\":\"VEC3\"},"
		         "{\"bufferView\":6,\"componentType\":5121,\"normalized\":true,\"count\":6,\"type\":\"VEC4\"}]}",
		         binSize, off[0], len[0], off[1], len[1], off[2], len[2], off[3], len[3], off[4], len[4], off[5], len[5], off[6], len[6], off[7],
		         len[7], GRID_VERTS, GRID, GRID, GRID_VERTS, GRID_VERTS, GRID_TRIS * 3, (double)bmin[0], (double)bmin[1], (double)bmin[2],
		         (double)bmax[0], (double)bmax[1], (double)bmax[2]);
		glb = EngStaticLoadTest_Glb(json, bin, binSize, &glbSize);
		if (glb == NULL)
		{
			free(gp);
			free(gt);
			free(gn);
			free(gi);
			free(bin);
			free(json);
			(*checks)++;
			printf("engine selftest: static load out of memory\n");
			return 1;
		}
		mem.data = glb;
		mem.size = glbSize;
		memset(&src, 0, sizeof(src));
		src.read = EngStaticLoadTest_MemRead;
		src.user = &mem;
		src.size = glbSize;
		src.baseDir = "";

		// ---- a) Open ---------------------------------------------------------
		why[0] = 0;
		i = EngineStatic_Open(&src, &model, why, (int)sizeof(why));
		EngStaticLoadTest_Check(i == 1, "open of the made-up model", checks, &failures);
		if (i == 1)
		{
			float ex[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
			float ey[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

			EngStaticLoadTest_Check(model.triangles == GRID_TRIS + 2, "triangles == 3202", checks, &failures);
			EngStaticLoadTest_Check(model.primitives == 2 && model.skippedPrimitives == 0, "2 primitives drawn, none skipped", checks, &failures);
			EngStaticLoadTest_Check(model.mirroredNodes == 1, "1 mirrored node", checks, &failures);
			EngStaticLoadTest_Check(model.materialCount == 3 && model.imageCount == 1, "3 materials, 1 image", checks, &failures);
			EngStaticLoadTest_Check(model.noUvTriangles == 2, "2 triangles without texcoords", checks, &failures);
			EngStaticLoadTest_Check(model.binSize == binSize && model.binOffset > 0, "BIN chunk found", checks, &failures);
			EngStaticLoadTest_Check(model.materials != NULL && model.materials[0].image == 0 && model.materials[0].wrapU == 1 &&
			                            model.materials[0].wrapV == 2 && model.materials[0].alphaMode == ENGINE_STATIC_ALPHA_MASK &&
			                            EngStaticLoadTest_Near(model.materials[0].alphaCutoff, 0.25f) && model.materials[0].doubleSided == 1 &&
			                            EngStaticLoadTest_Near(model.materials[0].baseColor[1], 0.5f) &&
			                            EngStaticLoadTest_Near(model.materials[0].baseColor[3], 0.75f),
			                        "material 0: MASK 0.25, double sided, texture 0, clamp / mirror, factor", checks, &failures);
			EngStaticLoadTest_Check(model.materials != NULL && model.materials[1].image == -1 && model.materials[1].alphaMode == ENGINE_STATIC_ALPHA_OPAQUE &&
			                            model.materials[1].doubleSided == 0 && EngStaticLoadTest_Near(model.materials[1].alphaCutoff, 0.5f),
			                        "material 1: opaque, single sided, no texture", checks, &failures);
			EngStaticLoadTest_Check(model.materials != NULL && model.materials[2].image == -1 && model.materials[2].wrapU == 0 &&
			                            model.materials[2].alphaMode == ENGINE_STATIC_ALPHA_OPAQUE && model.materials[2].doubleSided == 0 &&
			                            EngStaticLoadTest_Near(model.materials[2].baseColor[0], 1.0f) && EngStaticLoadTest_Near(model.materials[2].alphaCutoff, 0.5f),
			                        "material 2: the default", checks, &failures);
			EngStaticLoadTest_Check(model.materials != NULL && model.materials[0].normalImage == 0 && model.materials[0].normalWrapU == 1 &&
			                            model.materials[0].normalWrapV == 2 && EngStaticLoadTest_Near(model.materials[0].normalScale, 0.5f),
			                        "material 0: normal map image 0, clamp / mirror, scale 0.5", checks, &failures);
			EngStaticLoadTest_Check(model.materials != NULL && model.materials[1].normalImage == -1 &&
			                            EngStaticLoadTest_Near(model.materials[1].normalScale, 1.0f),
			                        "material 1: no normal map, scale 1", checks, &failures);
			EngStaticLoadTest_Check(model.materials != NULL && model.materials[2].normalImage == -1 &&
			                            EngStaticLoadTest_Near(model.materials[2].normalScale, 1.0f),
			                        "material 2 (the default): no normal map, scale 1", checks, &failures);
			EngStaticLoadTest_Check(model.materials != NULL && model.materials[0].unlit == 0 && model.materials[1].unlit == 1 && model.materials[2].unlit == 0,
			                        "KHR_materials_unlit: material 1 unlit, material 0 and the default lit", checks, &failures);
			for (i = 0; i < 6; i++)
			{
				for (j = 0; j < 3; j++)
				{
					const float x = (j == 0) ? -bp[3 * i] : bp[3 * i + j];

					if (x < ex[j])
					{
						ex[j] = x;
					}
					if (x > ey[j])
					{
						ey[j] = x;
					}
				}
			}
			ex[0] = (ex[0] < 100.0f) ? ex[0] : 100.0f;
			ey[0] = (ey[0] > 140.0f) ? ey[0] : 140.0f;
			ex[1] = (ex[1] < 0.0f) ? ex[1] : 0.0f;
			ey[1] = (ey[1] > 0.0f) ? ey[1] : 0.0f;
			ex[2] = (ex[2] < 0.0f) ? ex[2] : 0.0f;
			ey[2] = (ey[2] > 40.0f) ? ey[2] : 40.0f;
			EngStaticLoadTest_Check(EngStaticLoadTest_Near(model.boxMin[0], ex[0]) && EngStaticLoadTest_Near(model.boxMin[1], ex[1]) &&
			                            EngStaticLoadTest_Near(model.boxMin[2], ex[2]) && EngStaticLoadTest_Near(model.boxMax[0], ey[0]) &&
			                            EngStaticLoadTest_Near(model.boxMax[1], ey[1]) && EngStaticLoadTest_Near(model.boxMax[2], ey[2]),
			                        "box with the translation and the mirror", checks, &failures);

			// ---- b) BuildPieces ---------------------------------------------------
			{
				EngStaticLoadTestPieces t1;
				EngStaticLoadTestPieces t2;
				int n1;
				int n2;

				EngStaticLoadTest_PiecesInit(&t1, 0);
				EngStaticLoadTest_PiecesInit(&t2, 0);
				n1 = EngineStatic_BuildPieces(&model, 500, 1, EngStaticLoadTest_OnPiece, &t1, why, (int)sizeof(why));
				n2 = EngineStatic_BuildPieces(&model, 500, 1, EngStaticLoadTest_OnPiece, &t2, why, (int)sizeof(why));
				EngStaticLoadTest_Check(n1 > 1 && n1 == (int)t1.pieces, "split into several pieces", checks, &failures);
				EngStaticLoadTest_Check(t1.triangles == GRID_TRIS + 2, "pieces hold every triangle once", checks, &failures);
				EngStaticLoadTest_Check(t1.maxPieceTriangles <= 500 && t1.sizesOk, "every piece has at most 500 triangles", checks, &failures);
				EngStaticLoadTest_Check(t1.rangesOk, "ranges cover the indices without a gap", checks, &failures);
				EngStaticLoadTest_Check(t1.indicesOk, "every index is below the vertex count", checks, &failures);
				EngStaticLoadTest_Check(t1.boxOk, "every vertex is inside its piece's box", checks, &failures);
				EngStaticLoadTest_Check(n1 == n2 && t1.hash == t2.hash, "two runs give the same bytes", checks, &failures);

				EngStaticLoadTest_PiecesInit(&t1, 0);
				n1 = EngineStatic_BuildPieces(&model, 1u << 20, 1, EngStaticLoadTest_OnPiece, &t1, why, (int)sizeof(why));
				EngStaticLoadTest_Check(n1 == 1 && t1.triangles == GRID_TRIS + 2, "a large limit gives one piece", checks, &failures);

				EngStaticLoadTest_PiecesInit(&t1, 0);
				why[0] = 0;
				n1 = EngineStatic_BuildPieces(&model, 30, 1, EngStaticLoadTest_OnPiece, &t1, why, (int)sizeof(why));
				EngStaticLoadTest_Check(n1 == -1 && why[0] != 0 && t1.pieces == 0, "too many pieces is refused with a reason", checks, &failures);

				EngStaticLoadTest_PiecesInit(&t1, 0);
				t1.stop = 1;
				why[0] = 0;
				n1 = EngineStatic_BuildPieces(&model, 500, 1, EngStaticLoadTest_OnPiece, &t1, why, (int)sizeof(why));
				EngStaticLoadTest_Check(n1 == -1 && t1.pieces == 1 && why[0] != 0, "onPiece returning 0 stops the build", checks, &failures);
			}

			// ---- c) mirrored winding, d) colour -------------------------------------
			{
				EngStaticLoadTestPieces lit;
				EngStaticLoadTestPieces flat;
				int found[2] = {-1, -1};
				int foundFlat = -1;
				int count = 0;
				u32 tri;
				int n1;

				EngStaticLoadTest_PiecesInit(&lit, 1);
				EngStaticLoadTest_PiecesInit(&flat, 1);
				n1 = EngineStatic_BuildPieces(&model, 1u << 20, 1, EngStaticLoadTest_OnPiece, &lit, why, (int)sizeof(why));
				EngStaticLoadTest_Check(n1 == 1 && lit.keptVertices != NULL, "lit single piece for the winding and colour checks", checks, &failures);
				n1 = EngineStatic_BuildPieces(&model, 1u << 20, 0, EngStaticLoadTest_OnPiece, &flat, why, (int)sizeof(why));
				if (lit.keptVertices != NULL && flat.keptVertices != NULL)
				{
					for (tri = 0; tri < lit.keptIndexCount / 3u; tri++)
					{
						if (lit.keptVertices[lit.keptIndices[3 * tri]].position[0] < 50.0f)
						{
							if (count < 2)
							{
								found[count] = (int)tri;
							}
							count++;
						}
					}
					EngStaticLoadTest_Check(count == 2, "the two triangles of the mirrored node are in the piece", checks, &failures);
					if (count == 2)
					{
						const float tn[2][3] = {{L[0], L[1], L[2]}, {-L[0], -L[1], -L[2]}};
						int w;

						for (w = 0; w < 2; w++)
						{
							const EngineVertex *v0 = &lit.keptVertices[lit.keptIndices[3 * found[w] + 0]];
							const EngineVertex *v1 = &lit.keptVertices[lit.keptIndices[3 * found[w] + 1]];
							const EngineVertex *v2 = &lit.keptVertices[lit.keptIndices[3 * found[w] + 2]];
							float e1[3];
							float e2[3];
							float c[3];

							for (k = 0; k < 3; k++)
							{
								e1[k] = v1->position[k] - v0->position[k];
								e2[k] = v2->position[k] - v0->position[k];
							}
							EngStaticLoadTest_Cross(e1, e2, c);
							EngStaticLoadTest_Check(c[0] * tn[w][0] + c[1] * tn[w][1] + c[2] * tn[w][2] > 0.0f,
							                        (w == 0) ? "mirrored winding keeps triangle 1 facing its normal" : "mirrored winding keeps triangle 2 facing its normal",
							                        checks, &failures);
						}
						{
							const EngineVertex *a0 = &lit.keptVertices[lit.keptIndices[3 * found[0]]];
							const EngineVertex *a1 = &lit.keptVertices[lit.keptIndices[3 * found[1]]];

							EngStaticLoadTest_Check(abs((int)a0->color[0] - 200) <= 1 && abs((int)a0->color[1] - 100) <= 1 &&
							                            abs((int)a0->color[2] - 40) <= 1 && a0->color[3] == 128,
							                        "normal along the light keeps the colour", checks, &failures);
							EngStaticLoadTest_Check(abs((int)a1->color[0] - 110) <= 1 && abs((int)a1->color[1] - 55) <= 1 &&
							                            abs((int)a1->color[2] - 22) <= 1 && a1->color[3] == 128,
							                        "normal against the light on a single sided material gives 0.55", checks, &failures);
						}
						for (tri = 0; tri < flat.keptIndexCount / 3u; tri++)
						{
							if (flat.keptVertices[flat.keptIndices[3 * tri]].position[0] < 50.0f)
							{
								foundFlat = (int)tri;
							}
						}
						EngStaticLoadTest_Check(foundFlat >= 0 && flat.keptVertices[flat.keptIndices[3 * foundFlat]].color[0] == 200 &&
						                            flat.keptVertices[flat.keptIndices[3 * foundFlat]].color[2] == 40,
						                        "without light the colour stays as written", checks, &failures);
					}
				}
				else
				{
					EngStaticLoadTest_Check(0, "kept pieces missing", checks, &failures);
				}
				EngStaticLoadTest_PiecesFree(&lit);
				EngStaticLoadTest_PiecesFree(&flat);
			}

			// ---- c2) the packed normal (ENGINE_STATIC_BUILD_NORMALS) ------------------
			{
				EngStaticLoadTestPieces nrm;
				EngStaticLoadTestPieces nrm2;
				EngStaticLoadTestPieces plain;
				const float tn[2][3] = {{L[0], L[1], L[2]}, {-L[0], -L[1], -L[2]}};
				int gridOk = 1;
				int gridCount = 0;
				int mirrorOk = 1;
				int mirrorTris = 0;
				int offOk = 1;
				u32 vi;
				u32 tri;
				int n1;
				int n2;

				EngStaticLoadTest_PiecesInit(&nrm, 1);
				EngStaticLoadTest_PiecesInit(&nrm2, 0);
				EngStaticLoadTest_PiecesInit(&plain, 1);
				n1 = EngineStatic_BuildPieces(&model, 1u << 20, ENGINE_STATIC_BUILD_NORMALS, EngStaticLoadTest_OnPiece, &nrm, why, (int)sizeof(why));
				n2 = EngineStatic_BuildPieces(&model, 1u << 20, ENGINE_STATIC_BUILD_NORMALS, EngStaticLoadTest_OnPiece, &nrm2, why, (int)sizeof(why));
				EngStaticLoadTest_Check(n1 == 1 && n2 == 1 && nrm.keptVertices != NULL && nrm.hash == nrm2.hash, "NORMALS: two runs give the same bytes", checks, &failures);
				(void)EngineStatic_BuildPieces(&model, 1u << 20, 0, EngStaticLoadTest_OnPiece, &plain, why, (int)sizeof(why));
				if (nrm.keptVertices != NULL && plain.keptVertices != NULL)
				{
					// (0, 1, 0) packs to 0.5 * 255 = 127.5 -> 128 (rounded half up), 255, 128, w 255
					for (vi = 0; vi < nrm.keptVertexCount; vi++)
					{
						const EngineVertex *v = &nrm.keptVertices[vi];

						if (v->position[0] >= 50.0f)
						{
							gridCount++;
							if (v->uvBox[0] != 128 || v->uvBox[1] != 255 || v->uvBox[2] != 128 || v->uvBox[3] != 255)
							{
								gridOk = 0;
							}
						}
					}
					// the mirrored node: the world normal is (-x, y, z) of the authored one
					for (tri = 0; tri < nrm.keptIndexCount / 3u && mirrorTris < 2; tri++)
					{
						if (nrm.keptVertices[nrm.keptIndices[3 * tri]].position[0] < 50.0f)
						{
							for (k = 0; k < 3; k++)
							{
								const EngineVertex *v = &nrm.keptVertices[nrm.keptIndices[3 * tri + (u32)k]];
								int a2;

								for (a2 = 0; a2 < 3; a2++)
								{
									if (fabsf((float)v->uvBox[a2] / 127.5f - 1.0f - tn[mirrorTris][a2]) > 0.02f)
									{
										mirrorOk = 0;
									}
								}
								if (v->uvBox[3] != 255)
								{
									mirrorOk = 0;
								}
							}
							mirrorTris++;
						}
					}
					// without NORMALS every box is the full box, stored as zeros
					for (vi = 0; vi < plain.keptVertexCount; vi++)
					{
						const EngineVertex *v = &plain.keptVertices[vi];

						if (v->uvBox[0] != 0 || v->uvBox[1] != 0 || v->uvBox[2] != 0 || v->uvBox[3] != 0)
						{
							offOk = 0;
						}
					}
				}
				EngStaticLoadTest_Check(nrm.keptVertices != NULL && gridCount == GRID_VERTS && gridOk, "NORMALS: grid vertices pack (0, 1, 0) to 128, 255, 128, 255", checks, &failures);
				EngStaticLoadTest_Check(nrm.keptVertices != NULL && mirrorTris == 2 && mirrorOk, "NORMALS: the mirrored node's normals decode to (-x, y, z) within 0.02", checks, &failures);
				EngStaticLoadTest_Check(plain.keptVertices != NULL && offOk, "without NORMALS every uvBox is the full box (zeros)", checks, &failures);
				EngStaticLoadTest_PiecesFree(&nrm);
				EngStaticLoadTest_PiecesFree(&nrm2);
				EngStaticLoadTest_PiecesFree(&plain);
			}

			// ---- e) ReadImage ---------------------------------------------------------
			{
				EngineStaticImage img;
				int ok = EngineStatic_ReadImage(&model, 0, &img, why, (int)sizeof(why));

				EngStaticLoadTest_Check(ok == 1 && img.width == 16 && img.height == 16 && img.fileWidth == 3 && img.fileHeight == 2 && img.resampled == 1,
				                        "image 3 x 2 is scaled up to 16 x 16", checks, &failures);
				if (ok)
				{
					EngStaticLoadTest_Check(memcmp(img.rgba, rgba, 4) == 0, "image texel (0, 0) is the source's", checks, &failures);
					EngStaticLoadTest_Check(memcmp(img.rgba + (15 * 16 + 15) * 4, rgba + (1 * 3 + 2) * 4, 4) == 0, "image texel (15, 15) is the source's (2, 1)", checks, &failures);
					EngineStatic_FreeImage(&img);
					EngStaticLoadTest_Check(img.rgba == NULL && img.width == 0, "FreeImage empties the struct", checks, &failures);
				}
				EngStaticLoadTest_Check(EngineStatic_ReadImage(&model, 5, &img, why, (int)sizeof(why)) == 0 && why[0] != 0, "a missing image is refused", checks, &failures);
			}

			EngineStatic_Close(&model);
			EngStaticLoadTest_Check(model.gltf == NULL && model.materials == NULL, "Close empties the model", checks, &failures);
			EngineStatic_Close(&model);
			EngStaticLoadTest_Check(model.gltf == NULL, "Close twice is safe", checks, &failures);
		}

		// ---- g) refusals ---------------------------------------------------------------
		{
			u8 *bad = (u8 *)malloc(glbSize);
			EngStaticLoadMem badMem;
			EngineStaticSource badSrc = src;

			if (bad != NULL)
			{
				memcpy(bad, glb, glbSize);
				bad[0] = 'X';
				badMem.data = bad;
				badMem.size = glbSize;
				badSrc.user = &badMem;
				why[0] = 0;
				EngStaticLoadTest_Check(EngineStatic_Open(&badSrc, &model, why, (int)sizeof(why)) == 0 && why[0] != 0, "wrong magic is refused", checks, &failures);
				memcpy(bad, glb, glbSize);
				EngStaticLoadTest_PutLE32(bad + 12, glbSize);
				why[0] = 0;
				EngStaticLoadTest_Check(EngineStatic_Open(&badSrc, &model, why, (int)sizeof(why)) == 0 && why[0] != 0, "JSON length past the end is refused", checks, &failures);
				memcpy(bad, glb, glbSize);
				why[0] = 0;
				badMem.size = glbSize - 16;
				badSrc.size = glbSize - 16;
				EngStaticLoadTest_Check(EngineStatic_Open(&badSrc, &model, why, (int)sizeof(why)) == 0 && why[0] != 0, "a file shorter than its header says is refused", checks, &failures);
				free(bad);
			}
		}
		free(glb);
	}

	// A primitive whose data lies past the BIN chunk is skipped; alone it leaves nothing to draw.
	{
		const char *fmt =
		    "{\"asset\":{\"version\":\"2.0\"},\"scenes\":[{\"nodes\":[0]}],\"scene\":0,\"nodes\":[{\"mesh\":0}],"
		    "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}],\"buffers\":[{\"byteLength\":36}],"
		    "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":%u}],"
		    "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\"}]}";
		const float tri[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
		const u32 variants[3][2] = {{36, 3}, {36, 1000}, {36000, 3}};
		int v;

		for (v = 0; v < 3; v++)
		{
			u32 size = 0;
			u8 *g2;

			snprintf(json, 8192, fmt, variants[v][0], variants[v][1]);
			g2 = EngStaticLoadTest_Glb(json, (const u8 *)tri, sizeof(tri), &size);
			if (g2 == NULL)
			{
				EngStaticLoadTest_Check(0, "out of memory", checks, &failures);
				continue;
			}
			mem.data = g2;
			mem.size = size;
			src.size = size;
			why[0] = 0;
			i = EngineStatic_Open(&src, &model, why, (int)sizeof(why));
			if (v == 0)
			{
				EngStaticLoadTest_Check(i == 1 && model.triangles == 1 && model.primitives == 1, "control: the same file with valid data opens", checks, &failures);
			}
			else
			{
				EngStaticLoadTest_Check(i == 0 && why[0] != 0, (v == 1) ? "accessor past its buffer view is refused" : "buffer view past the BIN is refused",
				                        checks, &failures);
			}
			EngineStatic_Close(&model);
			free(g2);
		}
	}

	// ---- f) the pure helpers ---------------------------------------------------------------
	EngStaticLoadTest_Check(EngineStatic_UpEdge(1) == 16 && EngineStatic_UpEdge(16) == 16 && EngineStatic_UpEdge(17) == 32 &&
	                            EngineStatic_UpEdge(800) == 1024 && EngineStatic_UpEdge(2048) == 2048 && EngineStatic_UpEdge(2049) == 0 &&
	                            EngineStatic_UpEdge(0) == 0 && EngineStatic_UpEdge(-5) == 0,
	                        "UpEdge", checks, &failures);
	{
		u8 c3[3 * 3 * 4];
		u8 c4[4 * 4 * 4];
		int constant = 1;

		for (i = 0; i < 3 * 3; i++)
		{
			c3[4 * i + 0] = 17;
			c3[4 * i + 1] = 200;
			c3[4 * i + 2] = 99;
			c3[4 * i + 3] = 255;
		}
		EngStaticLoadTest_Check(EngineStatic_ResampleUp(c3, 3, 3, c4, 4, 4) == 1, "ResampleUp 3 x 3 to 4 x 4", checks, &failures);
		for (i = 0; i < 4 * 4; i++)
		{
			constant = constant && c4[4 * i + 0] == 17 && c4[4 * i + 1] == 200 && c4[4 * i + 2] == 99 && c4[4 * i + 3] == 255;
		}
		EngStaticLoadTest_Check(constant, "ResampleUp keeps a constant image constant", checks, &failures);
		EngStaticLoadTest_Check(EngineStatic_ResampleUp(c4, 4, 4, c3, 3, 3) == 0 && EngineStatic_ResampleUp(c3, 0, 3, c4, 4, 4) == 0,
		                        "ResampleUp never scales down", checks, &failures);
	}

	free(gp);
	free(gt);
	free(gn);
	free(gi);
	free(bin);
	free(json);
	return failures;
}
