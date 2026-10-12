#include <platform/engine.h>

#include <stdio.h>
#include <string.h>

// ENGINE DRIVER - custom drivers through the engine (step 3). See
// include/platform/engine.h (section CUSTOM DRIVERS).
//
// Under --engine the native render layer still decides per (instance, view)
// what a custom driver draws and works out the finished struct NrDrawItem (pose,
// wheel maps, water line, mirror); only the DRAWING moves here. This file turns
// an item into engine draws exactly as NativeRenderLayer_DrawCharItem turns it
// into NativeRenderer_DrawNativeMesh calls:
//   body    one mesh per set (the set's body buffers, one frame per pose of the
//           buffer, the ranges of the set), drawn with CharItemMatrix and the
//           item's pose, tint scale and water line;
//   wheels  the set's wheel buffers as FOUR external meshes of one range each
//           (front, its mirror image, rear, its mirror image - the wheelRange[]
//           of DrawCharItem), because an EngineDraw has no range selector; one
//           draw per wheel bit of the item, the wheel's own map.
// The mesh and material tables are made at the load stage, right after
// NativeCharGpu_LoadSeats, and reset by the renderer's level unload. The set's
// buffers stay the set's (external meshes are never destroyed by the engine).
//
// This file is part of the unity build (main.c) and comes after
// native_render_layer.c and native_char_gpu.c, so it uses their internal
// functions and the layer's item struct directly. It only READS the item.

#define ENGINE_DRIVER_WHEEL_RANGES 4 // front, front mirror, rear, rear mirror

struct EngineDriverSet
{
	const struct NativeCharGpu *gpu; // the set (a pointer into s_ncgSets), NULL = not the engine's
	int bodyMesh;                    // engine mesh, -1 when none
	int wheelMesh[ENGINE_DRIVER_WHEEL_RANGES]; // -1 when the set has no such wheel mesh
	int materialBase;                // engine material index of the set's material 0
	int materialCount;
	EngineRange bodyRange[NATIVE_CHAR_GPU_MATERIALS];
	EngineRange wheelRange[ENGINE_DRIVER_WHEEL_RANGES];
	char name[ENGINE_DRIVER_WHEEL_RANGES + 1][48];
};

internal struct EngineDriverSet s_engDrvSets[NATIVE_CHAR_GPU_SETS];

// The draw kinds of the cull check.
enum
{
	ENG_DRV_KIND_BODY = 0,
	ENG_DRV_KIND_MIRROR,
	ENG_DRV_KIND_WATER,
	ENG_DRV_KIND_WHEEL,
	ENG_DRV_KINDS
};

internal u32 s_engDrvCullChecked[ENG_DRV_KINDS];
internal u32 s_engDrvCullDiffers[ENG_DRV_KINDS];
internal int s_engDrvCullLogged;
internal u32 s_engDrvDeclined; // first items the engine declined (the layer drew them)
internal u32 s_engDrvPoseOut; // body draws skipped: the item's pose lies outside the set's buffer

internal int Engine_DriverFindSet(const struct NativeCharGpu *gpu)
{
	int i;

	if (gpu == NULL)
	{
		return -1;
	}
	for (i = 0; i < NATIVE_CHAR_GPU_SETS; i++)
	{
		if (s_engDrvSets[i].gpu == gpu)
		{
			return i;
		}
	}
	return -1;
}

// Mirrors the upload of NativeCharGpu (native_char_gpu.c) and the range set-up
// of NativeRenderLayer_DrawCharItem (native_render_layer.c, "ranges[r]" and
// "wheelRange[half]").
void Engine_DriverLoad(void)
{
	int i;
	int sets = 0;

	if (!g_cfg_engine)
	{
		return;
	}

	memset(s_engDrvSets, 0, sizeof(s_engDrvSets));
	memset(s_engDrvCullChecked, 0, sizeof(s_engDrvCullChecked));
	memset(s_engDrvCullDiffers, 0, sizeof(s_engDrvCullDiffers));
	s_engDrvCullLogged = 0;
	s_engDrvPoseOut = 0;
	s_engDrvDeclined = 0;

	for (i = 0; i < NATIVE_CHAR_GPU_SETS; i++)
	{
		const struct NativeCharGpu *gpu = &s_ncgSets[i];
		struct EngineDriverSet *ds = &s_engDrvSets[i];
		u32 m;
		int h;

		ds->bodyMesh = -1;
		for (h = 0; h < ENGINE_DRIVER_WHEEL_RANGES; h++)
		{
			ds->wheelMesh[h] = -1;
		}
		if (gpu->state != NATIVE_CHAR_GPU_READY)
		{
			continue;
		}

		// The materials of the set, in the set's own order, so a range's
		// material index is materialBase + the set's. Texture -1 = no texture
		// (a plain tinted material); a masked material discards below 0.5 as
		// DrawCharItem's alphaCutoff does.
		ds->materialBase = -1;
		ds->materialCount = 0;
		for (m = 0; m < gpu->materialCount; m++)
		{
			EngineMaterial mat;
			const int texture = gpu->materialTexture[m];
			int index;

			memset(&mat, 0, sizeof(mat));
			if ((texture >= 0) && ((u32)texture < NATIVE_CHAR_GPU_TEXTURES))
			{
				mat.texSource = ENGINE_TEX_NATIVE;
				mat.nativeTexture = (u32)gpu->texture[texture];
				if (gpu->textureSrgb[texture])
				{
					mat.flags |= ENGINE_MATERIAL_SRGB;
				}
			}
			else
			{
				mat.texSource = ENGINE_TEX_NONE;
			}
			mat.blend = gpu->materialMask[m] ? (u8)ENGINE_BLEND_MASK : (u8)ENGINE_BLEND_OPAQUE;
			mat.alphaCutoff = gpu->materialMask[m] ? 0.5f : 0.0f;
			memcpy(mat.tint, gpu->materialTint[m], sizeof(mat.tint));
			index = Engine_MaterialRegister(&mat);
			if (index < 0)
			{
				break;
			}
			if (m == 0u)
			{
				ds->materialBase = index;
			}
			else if (index != (ds->materialBase + (int)m))
			{
				break; // not consecutive: cannot map by base
			}
			ds->materialCount++;
		}
		if ((ds->materialCount == 0) || ((u32)ds->materialCount != gpu->materialCount))
		{
			Platform_Log("[CTR Engine] driver set %d: materials not registered (%d of %u), set stays with the layer\n", i, ds->materialCount,
			             gpu->materialCount);
			continue;
		}

		// The body: all poses of the buffer (poseTotal = the file's poses and
		// the win/lose stages), the sorted index ranges.
		if ((gpu->bodyVB != NATIVE_GFX_INVALID) && (gpu->bodyIB != NATIVE_GFX_INVALID) && (gpu->rangeCount > 0u))
		{
			EngineExternalMeshDesc desc;

			for (m = 0; m < gpu->rangeCount; m++)
			{
				ds->bodyRange[m].firstIndex = gpu->range[m].firstIndex;
				ds->bodyRange[m].indexCount = gpu->range[m].indexCount;
				ds->bodyRange[m].material = (u16)(ds->materialBase + (int)gpu->range[m].material);
			}
			snprintf(ds->name[0], sizeof(ds->name[0]), "driver set %d body", i);
			memset(&desc, 0, sizeof(desc));
			desc.vertexBuffer = (u32)gpu->bodyVB;
			desc.indexBuffer = (u32)gpu->bodyIB;
			desc.verticesPerFrame = gpu->vertexCount;
			desc.frames = (gpu->poseTotal > 0u) ? gpu->poseTotal : 1u;
			desc.ranges = ds->bodyRange;
			desc.rangeCount = gpu->rangeCount;
			desc.name = ds->name[0];
			ds->bodyMesh = Engine_MeshCreateExternal(&desc);
		}

		// The wheels: the same four ranges DrawCharItem builds (wheelRange[]),
		// each as a mesh of its own. The rear pair has its own mesh and
		// material only with an own rear wheel (WHLS version 3), else it is the
		// front one.
		if (gpu->hasWheels && (gpu->wheelVB != NATIVE_GFX_INVALID) && (gpu->wheelIB != NATIVE_GFX_INVALID))
		{
			for (h = 0; h < ENGINE_DRIVER_WHEEL_RANGES; h++)
			{
				const int rearMesh = (h >= 2) && gpu->wheelRearOwn;
				const u16 material = rearMesh ? gpu->wheelRearMaterial : gpu->wheelMaterial;
				EngineExternalMeshDesc desc;

				if (rearMesh)
				{
					ds->wheelRange[h].firstIndex = ((h & 1) != 0) ? gpu->wheelRearMirrorFirst : gpu->wheelRearFirst;
					ds->wheelRange[h].indexCount = gpu->wheelRearIndexCount;
				}
				else
				{
					ds->wheelRange[h].firstIndex = ((h & 1) != 0) ? gpu->wheelMirrorFirst : 0u;
					ds->wheelRange[h].indexCount = gpu->wheelIndexCount;
				}
				if ((u32)material >= gpu->materialCount)
				{
					continue;
				}
				ds->wheelRange[h].material = (u16)(ds->materialBase + (int)material);
				snprintf(ds->name[1 + h], sizeof(ds->name[1 + h]), "driver set %d wheel %d", i, h);
				memset(&desc, 0, sizeof(desc));
				desc.vertexBuffer = (u32)gpu->wheelVB;
				desc.indexBuffer = (u32)gpu->wheelIB;
				desc.verticesPerFrame = gpu->wheelVertexCount;
				desc.frames = 1u;
				desc.ranges = &ds->wheelRange[h];
				desc.rangeCount = 1u;
				desc.name = ds->name[1 + h];
				ds->wheelMesh[h] = Engine_MeshCreateExternal(&desc);
			}
		}

		// Only a set with a body mesh is the engine's.
		ds->gpu = (ds->bodyMesh >= 0) ? gpu : NULL;
		if (ds->gpu != NULL)
		{
			sets++;
		}
		Platform_Log("[CTR Engine] driver set %d: body mesh %d (%u frames x %u vertices, %u ranges), wheel meshes %d %d %d %d, %d materials from %d\n", i,
		             ds->bodyMesh, gpu->poseTotal, gpu->vertexCount, gpu->rangeCount, ds->wheelMesh[0], ds->wheelMesh[1], ds->wheelMesh[2], ds->wheelMesh[3],
		             ds->materialCount, ds->materialBase);
	}
	Platform_Log("[CTR Engine] driver: %d set(s) drawn by the engine\n", sets);
}

void Engine_DriverUnload(void)
{
	if (!g_cfg_engine)
	{
		return;
	}
	// The meshes and materials are reset by Engine_RenderLevelUnload; the
	// buffers belong to the sets. Only the tables are forgotten here.
	if (((s_engDrvCullChecked[0] + s_engDrvCullChecked[1] + s_engDrvCullChecked[2] + s_engDrvCullChecked[3]) > 0u) || (s_engDrvDeclined > 0u))
	{
		Platform_Log("[CTR Engine] driver cull check (differs/checked): body %u/%u, mirror %u/%u, water %u/%u, wheel %u/%u; pose outside buffer %u\n",
		             s_engDrvCullDiffers[0], s_engDrvCullChecked[0], s_engDrvCullDiffers[1], s_engDrvCullChecked[1], s_engDrvCullDiffers[2],
		             s_engDrvCullChecked[2], s_engDrvCullDiffers[3], s_engDrvCullChecked[3], s_engDrvPoseOut, s_engDrvDeclined);
	}
	memset(s_engDrvSets, 0, sizeof(s_engDrvSets));
}

int Engine_DriverBodyMesh(int set)
{
	if ((set < 0) || (set >= NATIVE_CHAR_GPU_SETS) || (s_engDrvSets[set].gpu == NULL))
	{
		return -1;
	}
	return s_engDrvSets[set].bodyMesh;
}

// What the engine will derive for a draw (Engine_RenderCullOfMatrix) against
// what the layer draws the item with (it->cull). Counted per kind; the first
// difference is logged once.
internal void Engine_DriverCullCheck(const struct NrDrawItem *it, int kind, const EngineMat4 *m)
{
	static const char *const names[ENG_DRV_KINDS] = {"body", "mirror", "water", "wheel"};
	const NativeGfxCull engine = Engine_RenderCullOfMatrix(m->m);

	s_engDrvCullChecked[kind]++;
	if ((int)engine != (int)it->cull)
	{
		s_engDrvCullDiffers[kind]++;
		if (!s_engDrvCullLogged)
		{
			s_engDrvCullLogged = 1;
			Platform_Log("[CTR Engine] driver cull differs: %s item view %d seat %d, layer cull %d, engine would derive %d (mirror %d part %d mvpShift %d)\n",
			             names[kind], (int)it->view, (int)it->seat, (int)it->cull, (int)engine, (int)it->mirror, (int)it->part, (int)it->mvpShift);
		}
	}
}

// MatrixToDraw writes out[col * 4 + row] = S[row][col], the order of EngineMat4.
internal void Engine_DriverMatrix(const double S[4][4], EngineMat4 *out)
{
	NativeRenderLayer_MatrixToDraw(S, out->m);
}

// Mirrors the body and wheel part of NativeRenderLayer_DrawCharItem
// (native_render_layer.c): the body with CharItemMatrix and vertexOffset =
// pose * vertexCount (here the mesh frame), the wheels with ScreenFromView of
// wheelA / wheelB and the range picked by (mirrored, rear).
int Engine_DriverSubmitItem(const struct NrDrawItem *it, u32 *ot, int cell)
{
	const struct EngineDriverSet *ds;
	EngineDraw draw;
	double S[4][4];
	int submitted = 0;
	int set;
	int bodyKind;

	if (!g_cfg_engine || (it == NULL) || (ot == NULL))
	{
		return 0;
	}
	set = Engine_DriverFindSet(it->gpu);
	if (set < 0)
	{
		s_engDrvDeclined++;
		return 0;
	}
	ds = &s_engDrvSets[set];
	bodyKind = it->mirror ? ENG_DRV_KIND_MIRROR : ((it->part != NR_PART_WHOLE) ? ENG_DRV_KIND_WATER : ENG_DRV_KIND_BODY);

	memset(&draw, 0, sizeof(draw));
	draw.materialOverride = -1;
	draw.tint[0] = it->tintScale[0];
	draw.tint[1] = it->tintScale[1];
	draw.tint[2] = it->tintScale[2];
	draw.tint[3] = 1.0f;
	draw.flags = 0;

	// The body. A side whose body the selector leaves out draws none. The
	// layer adds ofsXExtra (the SXY offset of dim-xor) to the body only; as a
	// screen offset it is folded into the matrix here (0 for every item but
	// the dim-xor ones).
	if (it->bodyOn && (ds->bodyMesh >= 0))
	{
		if ((u32)it->pose < it->gpu->poseTotal)
		{
			NativeRenderLayer_CharItemMatrix(it, (double)it->ofsXExtra, 0.0, S);
			Engine_DriverMatrix(S, &draw.clipFromModel);
			draw.mesh = ds->bodyMesh;
			draw.frame = (u32)it->pose;
			draw.sortDepth = (float)S[3][3];
			draw.split[0] = (float)it->split[0];
			draw.split[1] = (float)it->split[1];
			draw.split[2] = (float)it->split[2];
			draw.split[3] = (float)it->split[3];
			draw.splitKeep = it->splitKeep;
			Engine_DriverCullCheck(it, bodyKind, &draw.clipFromModel);
			if (Engine_RenderSubmitLinked((int)it->view, &draw, ot, cell))
			{
				submitted++;
			}
		}
		else
		{
			s_engDrvPoseOut++;
		}
	}

	// The wheels of this item's side. DrawCharItem sets no water line on a
	// wheel draw (the item's wheelMask already picks the wheels of the side),
	// so none is set here either: split zero, splitKeep 0.
	if (it->nativeWheels && (it->wheelMask != 0))
	{
		int wheel;

		memset(draw.split, 0, sizeof(draw.split));
		draw.splitKeep = 0;
		for (wheel = 0; wheel < NATIVE_WHEELS_COUNT; wheel++)
		{
			const int mirrored = it->gpu->wheelOwn && ((wheel & 1) != 0);
			const int rangeIndex = (mirrored ? 1 : 0) + ((wheel >= 2) ? 2 : 0);

			if ((it->wheelMask & (1u << wheel)) == 0)
			{
				continue;
			}
			if (ds->wheelMesh[rangeIndex] < 0)
			{
				continue;
			}
			NativeRenderLayer_ScreenFromView(it, (const double(*)[3])it->wheelA[wheel], it->wheelB[wheel], 0.0, 0.0, S);
			Engine_DriverMatrix(S, &draw.clipFromModel);
			draw.mesh = ds->wheelMesh[rangeIndex];
			draw.frame = 0u;
			draw.sortDepth = (float)S[3][3];
			Engine_DriverCullCheck(it, ENG_DRV_KIND_WHEEL, &draw.clipFromModel);
			if (Engine_RenderSubmitLinked((int)it->view, &draw, ot, cell))
			{
				submitted++;
			}
		}
	}
	return submitted;
}
