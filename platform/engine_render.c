#include <platform/engine.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ENGINE RENDER - see include/platform/engine.h (section RENDER).
//
// The render core of the native engine, phase 1: the engine's draws are drawn
// out of the PS1 ordering table. A draw of the scene is linked into an OT cell
// as a DR_PSYX_NATIVE marker (the same packet the native render layer links, see
// NativeRenderLayer_LinkMarkerInCell) whose item carries ENGINE_MARKER_FLAG; the
// OT parser (platform/native_gpu.c) hands the item to Engine_DrawMarker at that
// place, and the draw is made with the same sequence, in the same order, as
// NativeRenderer_DrawNativeMesh makes a native mesh: clip, projection, the PSX
// ortho in front of the draw's screen-from-model matrix, the depth clear of the
// split's scissor box, the draw state, program and buffers, one uniform block
// plus one draw per range, then the PSX state put back.
//
// WHAT LIVES WHERE
//   load phase  (Engine_RenderUploadMeshes): one static vertex buffer (all
//               frames of the mesh) and one static u16 index buffer per mesh.
//   frame       (Engine_RenderFrameBegin / SubmitLinked / DrawMarker): nothing is
//               created; dynamic effect geometry is written into a dynamic
//               vertex buffer that exists since Engine_RenderInit.
//
// THE DRAW TABLE. A marker names a submission index (the number
// Engine_SceneSubmit returned), but the scene only hands out SORTED copies, and
// the sort is unrelated to the marker's index. So this file keeps its own table
// s_engDraws[bank][view][index], filled in Engine_RenderSubmitLinked, and the
// draw side reads from it. It is valid until Engine_RenderFrameBegin of THAT
// bank, the same lifetime as the marker arena the markers sit in. The table
// holds a slim copy of the EngineDraw; for dynamic draws it holds no pointer at
// all, see below.
//
// DYNAMIC GEOMETRY is drawn NON-indexed. The dynamic vertex buffer is one
// buffer without an index buffer, so at submit time every dynamic draw's
// indices are expanded on the CPU into a triangle list (three EngineVertex per
// triangle, in the index order) in a per-bank CPU store, s_engDynCpu[bank]. At
// draw time the draw's slice of that store is copied into the GPU buffer
// (NativeGfx_UpdateVertexBuffer) and drawn with NativeGfx_Draw. Expanding at
// submit (not at draw) means no pointer into the scene's ring is ever kept, so
// nothing depends on the scene's storage lifetime. A triangle list of an
// indexed quad pair is 1.5 times its vertex count, hence the expanded capacity
// ENGINE_DYN_VERTICES_PER_FRAME * 3 / 2 per bank (a draw that does not fit is
// refused and counted). The store and the scene's rings are heap memory made
// by Engine_RenderInit, so a run without --engine has none of them.
//
// THE GPU SIDE OF THE DYNAMIC BUFFER. The GPU may still read frame N while
// frame N+1 is recorded, and the host memory behind a dynamic buffer is written
// at once. The native pose buffer solves this with NATIVE_LAYER_POSE_FRAMES = 3
// regions, each written only every third frame; this file does the same: the
// buffer has ENGINE_DYN_REGIONS regions of one bank's expanded capacity, a bank
// takes region (frame counter % ENGINE_DYN_REGIONS) at its FrameBegin and writes
// only there. Two regions (one per bank) would only be enough if a frame were
// never in flight for two game frames, which the Vulkan device (two frames in
// flight, one game frame of OT lag) does not promise.

#define ENGINE_DYN_EXPANDED (ENGINE_DYN_VERTICES_PER_FRAME * 3 / 2) // expanded vertices per bank and frame
#define ENGINE_DYN_REGIONS 3
#define ENGINE_MARKERS_PER_BANK (ENGINE_VIEWS * ENGINE_DRAWS_PER_VIEW)

// The texture unit the "ne" program reads the VRAM texture from, and the unit of
// the immutable PSX colour lookup table that has to be put back after a VRAM
// draw (NativeRenderer_InitialiseRender binds it once and nothing rebinds it).
#define ENGINE_VRAM_UNIT 1
#define ENGINE_LUT_UNIT 1
// The track mosaic table (platform/engine_track_mosaic.c), bound per range of an
// ENGINE_TEX_VRAM_TABLE material. Slot 2 is the PSX page atlas and never moves;
// nothing of the PSX path reads slot 3, so it is not restored.
#define ENGINE_TABLE_UNIT 3

// The slim copy of a draw. Dynamic geometry is in s_engDynCpu[bank], from dynFirst.
typedef struct
{
	float clipFromModel[16];
	float tint[4];
	u32 frame;
	u32 dynFirst;
	u32 dynCount; // expanded vertices; 0 for a mesh draw
	int mesh;
	int materialOverride;
	int dynMaterial;
	u16 flags;
	// The water line (EngineDraw.split / splitKeep); splitKeep 0 = no split.
	float split[4];
	s8 splitKeep;
	float user[4]; // EngineDraw.user, the program's per-draw data (params4)
	float user2[4]; // EngineDraw.user2, more of it ("ns": its STYLE in vramPage)
	float time;    // Engine_FrameTime of the bank at submit (params3.y)
	u64 rangeSkip; // EngineDraw.rangeSkip: the mesh ranges left out
} EngineRenderDraw;

// The counters of Engine_RenderReport.
typedef struct
{
	u32 submitted;
	u32 submitRefusedNotReady;
	u32 submitRefusedScene;
	u32 submitRefusedCell;
	u32 submitRefusedArena;
	u32 submitRefusedDyn;
	u32 drawn;       // markers that drew at least one range
	u32 markersSeen; // calls of Engine_DrawMarker
	u32 rangesDrawn;
	u32 refusedNoProgram;
	u32 refusedOffscreenOrNoDepth;
	u32 refusedClip;
	u32 refusedBadItem;
	u32 refusedBadMesh;
	u32 refusedBadMaterial;
	u32 refusedBadRange;
	u32 uploadFailed;
	u32 tableRanges; // ranges drawn through the track mosaic table (texture slot 3)
	u32 submittedPerView[ENGINE_VIEWS];
} EngineRenderCounts;

global_variable int s_engRenderTried = 0;
global_variable int s_engRenderReady = 0;
global_variable NativeGfxBuffer s_engDynBuffer = NATIVE_GFX_INVALID;
global_variable u32 s_engDynBytes = 0;
global_variable u32 s_engFrameCounter = 0;
global_variable u32 s_engRegion[ENGINE_SCENE_BANKS];

global_variable DR_PSYX_NATIVE s_engMarkers[ENGINE_SCENE_BANKS][ENGINE_MARKERS_PER_BANK];
global_variable int s_engMarkerCount[ENGINE_SCENE_BANKS];
global_variable EngineRenderDraw s_engDraws[ENGINE_SCENE_BANKS][ENGINE_VIEWS][ENGINE_DRAWS_PER_VIEW];
global_variable int s_engDrawCount[ENGINE_SCENE_BANKS][ENGINE_VIEWS];
global_variable int s_engDepthCleared[ENGINE_SCENE_BANKS][ENGINE_VIEWS];
global_variable EngineVertex *s_engDynCpu[ENGINE_SCENE_BANKS]; // ENGINE_DYN_EXPANDED each, made by Engine_RenderInit
global_variable u32 s_engDynUsed[ENGINE_SCENE_BANKS];

// GPU buffers of the meshes: the live ones, and the ones of the last load that
// is waiting to be destroyed (one load late, see Engine_RenderLevelUnload).
global_variable NativeGfxBuffer s_engLiveVB[ENGINE_MESHES_MAX];
global_variable NativeGfxBuffer s_engLiveIB[ENGINE_MESHES_MAX];
global_variable int s_engLiveCount = 0;
global_variable NativeGfxBuffer s_engGraveVB[ENGINE_MESHES_MAX];
global_variable NativeGfxBuffer s_engGraveIB[ENGINE_MESHES_MAX];
global_variable int s_engGraveCount = 0;

global_variable EngineRenderCounts s_engCount;

// The vertex layout every engine buffer has, struct EngineVertex: location 0
// position, 1 texture coordinate, 2 colour (the three the "nr" programs read),
// 3 the UV box (u8 x 4, read as whole numbers: USCALED, not normalised).
internal void Engine_RenderFillVertexDesc(NativeGfxVertexBufferDesc *desc, int bytes, int dynamic, const void *initial)
{
	memset(desc, 0, sizeof(*desc));
	desc->bytes = bytes;
	desc->dynamic = dynamic;
	desc->initial = initial;
	desc->stride = (int)sizeof(EngineVertex);
	desc->attribCount = 4;
	desc->attribs[0] = (NativeGfxVertexAttrib){.slot = a_position, .components = 3, .type = NATIVE_GFX_ATTR_FLOAT32, .offset = offsetof(EngineVertex, position)};
	desc->attribs[1] = (NativeGfxVertexAttrib){.slot = a_texcoord, .components = 2, .type = NATIVE_GFX_ATTR_FLOAT32, .offset = offsetof(EngineVertex, texcoord)};
	desc->attribs[2] = (NativeGfxVertexAttrib){.slot = a_color, .components = 4, .type = NATIVE_GFX_ATTR_UNORM8, .offset = offsetof(EngineVertex, color)};
	desc->attribs[3] = (NativeGfxVertexAttrib){.slot = a_extra, .components = 4, .type = NATIVE_GFX_ATTR_UINT8, .offset = offsetof(EngineVertex, uvBox)};
}

// The native layer treats handle 0 as "none" as well (NativeCharGpu_DropVB).
internal int Engine_RenderBufferValid(NativeGfxBuffer buffer)
{
	return (buffer != NATIVE_GFX_INVALID) && (buffer != 0u);
}

// The CPU side of the dynamic geometry, heap memory only when the engine
// starts: the expanded store per bank and the scene's rings. 1 = there (also
// when it already was), 0 = out of memory (nothing kept).
internal u32 Engine_RenderDynBytes(void)
{
	return ((u32)ENGINE_SCENE_BANKS * (u32)ENGINE_DYN_EXPANDED * (u32)sizeof(EngineVertex)) + Engine_SceneRingsBytes();
}

internal int Engine_RenderDynAlloc(void)
{
	int bank;
	int ok = Engine_SceneRingsAlloc();

	for (bank = 0; bank < ENGINE_SCENE_BANKS; bank++)
	{
		if (s_engDynCpu[bank] == NULL)
		{
			s_engDynCpu[bank] = (EngineVertex *)malloc((size_t)ENGINE_DYN_EXPANDED * sizeof(EngineVertex));
		}
		ok = ok && (s_engDynCpu[bank] != NULL);
		s_engDynUsed[bank] = 0;
	}
	if (!ok)
	{
		for (bank = 0; bank < ENGINE_SCENE_BANKS; bank++)
		{
			free(s_engDynCpu[bank]);
			s_engDynCpu[bank] = NULL;
		}
		Engine_SceneRingsFree();
	}
	return ok;
}

int Engine_RenderInit(void)
{
	NativeGfxVertexBufferDesc desc;
	const u32 bytes = (u32)ENGINE_DYN_EXPANDED * ENGINE_DYN_REGIONS * (u32)sizeof(EngineVertex);

	if (!g_cfg_engine)
	{
		return 0;
	}
	if (s_engRenderTried)
	{
		return s_engRenderReady;
	}
	s_engRenderTried = 1;
	// The exit report also in a run that falls back below (it says what the
	// engine drew before; Engine_RenderReport).
	Platform_AtExitReport(Engine_RenderReport);

	// Both programs or none: a material names "ne" or "nw", and a range without
	// its program would be refused at every draw (refusedNoProgram) - a hole
	// in the picture, not a picture. Then the PS1 path draws (Engine_Fallback).
	Engine_ProgramsInit();
	if ((Engine_ProgramOf(ENGINE_PROGRAM_NE) == 0xffffffffu) || (Engine_ProgramOf(ENGINE_PROGRAM_NW) == 0xffffffffu))
	{
		Platform_Log("[CTR Engine] render NOT ready: program 'ne' %s, program 'nw' %s\n", (Engine_ProgramOf(ENGINE_PROGRAM_NE) == 0xffffffffu) ? "missing" : "ready",
		             (Engine_ProgramOf(ENGINE_PROGRAM_NW) == 0xffffffffu) ? "missing" : "ready");
		Engine_Fallback("start", Engine_ProgramsFailReason());
		return 0;
	}

	if (!Engine_RenderDynAlloc())
	{
		Platform_Log("[CTR Engine] render NOT ready: the dynamic rings (%u bytes) could not be allocated\n", Engine_RenderDynBytes());
		Engine_Fallback("start", "the dynamic rings of the engine could not be allocated");
		return 0;
	}

	Engine_RenderFillVertexDesc(&desc, (int)bytes, 1, NULL);
	s_engDynBuffer = NativeGfx_CreateVertexBuffer(&desc);
	if (!Engine_RenderBufferValid(s_engDynBuffer))
	{
		s_engDynBuffer = NATIVE_GFX_INVALID;
		Platform_Log("[CTR Engine] render NOT ready: dynamic vertex buffer (%u bytes) could not be created\n", bytes);
		Engine_Fallback("start", "the dynamic vertex buffer of the engine could not be created");
		return 0;
	}
	s_engDynBytes = bytes;
	s_engRenderReady = 1;

	Platform_Log("[CTR Engine] render ready: program %s, dynamic buffer %u bytes (%d regions x %d vertices)\n",
	             (Engine_Program() != 0xffffffffu) ? "ne ready" : "ne missing", bytes, ENGINE_DYN_REGIONS, ENGINE_DYN_EXPANDED);
	return 1;
}

// Destroys what the last unload parked. Outside a frame only (the Vulkan
// destroy drains the device): the load phase, where the native layer does the
// same (NativeCharGpu_EmptyGrave).
internal void Engine_RenderEmptyGrave(void)
{
	for (int i = 0; i < s_engGraveCount; i++)
	{
		if (Engine_RenderBufferValid(s_engGraveVB[i]))
		{
			NativeGfx_DestroyVertexBuffer(s_engGraveVB[i]);
		}
		if (Engine_RenderBufferValid(s_engGraveIB[i]))
		{
			NativeGfx_DestroyIndexBuffer(s_engGraveIB[i]);
		}
	}
	s_engGraveCount = 0;
}

u32 Engine_RenderStaticVertexBuffer(const EngineVertex *vertices, u32 count)
{
	NativeGfxVertexBufferDesc desc;
	NativeGfxBuffer buffer;
	const u64 bytes = (u64)count * sizeof(EngineVertex);

	if ((vertices == NULL) || (count == 0u) || (bytes > 0x7fffffffu))
	{
		return 0xffffffffu;
	}
	Engine_RenderFillVertexDesc(&desc, (int)bytes, 0, vertices);
	buffer = NativeGfx_CreateVertexBuffer(&desc);
	return Engine_RenderBufferValid(buffer) ? (u32)buffer : 0xffffffffu;
}

u32 Engine_RenderStaticIndexBuffer(const u32 *indices, u32 count)
{
	NativeGfxIndexBufferDesc desc;
	NativeGfxBuffer buffer;
	const u64 bytes = (u64)count * sizeof(u32);

	if ((indices == NULL) || (count == 0u) || (bytes > 0x7fffffffu))
	{
		return 0xffffffffu;
	}
	desc.bytes = (int)bytes;
	desc.type = NATIVE_GFX_INDEX_U32;
	desc.initial = indices;
	buffer = NativeGfx_CreateIndexBuffer(&desc);
	return Engine_RenderBufferValid(buffer) ? (u32)buffer : 0xffffffffu;
}

void Engine_RenderStaticBuffersFree(u32 vertexBuffer, u32 indexBuffer)
{
	if (Engine_RenderBufferValid((NativeGfxBuffer)vertexBuffer) && (vertexBuffer != 0xffffffffu))
	{
		NativeGfx_DestroyVertexBuffer((NativeGfxBuffer)vertexBuffer);
	}
	if (Engine_RenderBufferValid((NativeGfxBuffer)indexBuffer) && (indexBuffer != 0xffffffffu))
	{
		NativeGfx_DestroyIndexBuffer((NativeGfxBuffer)indexBuffer);
	}
}

// STREAMED VERTEX BUFFERS (engine.h, RENDER). One entry per buffer: its
// region size, the CPU array of each bank's frame (set by the owner in the
// frame, stamped with that frame's number) and the frame whose array was
// copied last per bank (the copy happens once, at the buffer's first draw of
// the frame). s_engStreamCount 0 - every run without a streamed buffer - keeps
// the draw path to one compare. The region of a bank's frame is the dynamic
// buffer's (s_engRegion), so both follow the same rule.
CTR_STATIC_ASSERT(ENGINE_RENDER_STREAM_REGIONS == ENGINE_DYN_REGIONS);

typedef struct
{
	NativeGfxBuffer buffer; // NATIVE_GFX_INVALID or 0 = free entry
	u32 verticesPerRegion;
	const EngineVertex *source[ENGINE_SCENE_BANKS];
	u32 count[ENGINE_SCENE_BANKS];
	u32 sourceFrame[ENGINE_SCENE_BANKS];   // s_engStreamFrame[bank] + 1 when the array was set (0 = never)
	u32 uploadedFrame[ENGINE_SCENE_BANKS]; // the same for the copy
} EngineRenderStream;

global_variable EngineRenderStream s_engStreams[ENGINE_RENDER_STREAMS_MAX];
global_variable int s_engStreamCount = 0;
global_variable u32 s_engStreamFrame[ENGINE_SCENE_BANKS]; // the frame number Engine_RenderFrameBegin gave the bank (its region's)

u32 Engine_RenderStreamCreate(u32 verticesPerRegion)
{
	NativeGfxVertexBufferDesc desc;
	NativeGfxBuffer buffer;
	const u64 bytes = (u64)verticesPerRegion * (u64)ENGINE_RENDER_STREAM_REGIONS * sizeof(EngineVertex);
	int slot = -1;

	if (!g_cfg_engine || (verticesPerRegion == 0u) || (bytes > 0x7fffffffu))
	{
		return 0xffffffffu;
	}
	for (int i = 0; i < ENGINE_RENDER_STREAMS_MAX; i++)
	{
		if (!Engine_RenderBufferValid(s_engStreams[i].buffer))
		{
			slot = i;
			break;
		}
	}
	if (slot < 0)
	{
		return 0xffffffffu;
	}
	// Host visible (dynamic) and not initialised: every region is written before it is drawn.
	Engine_RenderFillVertexDesc(&desc, (int)bytes, 1, NULL);
	buffer = NativeGfx_CreateVertexBuffer(&desc);
	if (!Engine_RenderBufferValid(buffer))
	{
		return 0xffffffffu;
	}
	memset(&s_engStreams[slot], 0, sizeof(s_engStreams[slot]));
	s_engStreams[slot].buffer = buffer;
	s_engStreams[slot].verticesPerRegion = verticesPerRegion;
	s_engStreamCount++;
	return (u32)buffer;
}

void Engine_RenderStreamRelease(u32 vertexBuffer)
{
	for (int i = 0; i < ENGINE_RENDER_STREAMS_MAX; i++)
	{
		if (Engine_RenderBufferValid(s_engStreams[i].buffer) && ((u32)s_engStreams[i].buffer == vertexBuffer))
		{
			// Forgotten only: the owner destroys the buffer one load later (engine.h).
			memset(&s_engStreams[i], 0, sizeof(s_engStreams[i]));
			s_engStreamCount--;
			return;
		}
	}
}

internal EngineRenderStream *Engine_RenderStreamOf(u32 vertexBuffer)
{
	for (int i = 0; i < ENGINE_RENDER_STREAMS_MAX; i++)
	{
		if (Engine_RenderBufferValid(s_engStreams[i].buffer) && ((u32)s_engStreams[i].buffer == vertexBuffer))
		{
			return &s_engStreams[i];
		}
	}
	return NULL;
}

int Engine_RenderStreamSource(u32 vertexBuffer, const EngineVertex *vertices, u32 count)
{
	const int bank = Engine_SceneBank();
	EngineRenderStream *s = (s_engStreamCount > 0) ? Engine_RenderStreamOf(vertexBuffer) : NULL;

	if ((s == NULL) || (vertices == NULL) || (count == 0u) || (count > s->verticesPerRegion) || (bank < 0) || (bank >= ENGINE_SCENE_BANKS))
	{
		return 0;
	}
	s->source[bank] = vertices;
	s->count[bank] = count;
	s->sourceFrame[bank] = s_engStreamFrame[bank] + 1u;
	return 1;
}

u32 Engine_RenderStreamBytes(void)
{
	u32 bytes = 0;

	for (int i = 0; i < ENGINE_RENDER_STREAMS_MAX; i++)
	{
		if (Engine_RenderBufferValid(s_engStreams[i].buffer))
		{
			bytes += s_engStreams[i].verticesPerRegion * (u32)ENGINE_RENDER_STREAM_REGIONS * (u32)sizeof(EngineVertex);
		}
	}
	return bytes;
}

// At the draw of a streamed buffer: 1 = the bank's frame has its array,
// *vertexOffset = the first vertex of the bank's region; with upload the array
// is copied into that region, once per frame (right before the bind). 0 = no
// array for this frame (the draw is refused).
internal int Engine_RenderStreamReady(EngineRenderStream *s, int bank, int upload, int *vertexOffset)
{
	const u32 frame = s_engStreamFrame[bank] + 1u;
	const u32 first = s_engRegion[bank] * s->verticesPerRegion;

	if ((s->sourceFrame[bank] != frame) || (s->source[bank] == NULL) || (s->count[bank] == 0u))
	{
		return 0;
	}
	if (upload && (s->uploadedFrame[bank] != frame))
	{
		NativeGfx_UpdateVertexBuffer(s->buffer, (int)(first * sizeof(EngineVertex)), (int)(s->count[bank] * sizeof(EngineVertex)), s->source[bank]);
		s->uploadedFrame[bank] = frame;
	}
	*vertexOffset = (int)first;
	return 1;
}

int Engine_RenderUploadMeshes(void)
{
	int uploaded = 0;
	u64 bytes = 0;
	const u32 failedBefore = s_engCount.uploadFailed;

	if (!g_cfg_engine)
	{
		return 0;
	}
	// The engine oracle's per-track report (--engine-oracle only), registered once at the first load.
	Engine_OracleLoadStage();
	if (!Engine_RenderInit())
	{
		return 0;
	}

	// What the last load let go, now that no frame can name it.
	Engine_RenderEmptyGrave();

	for (int mesh = 0; mesh < ENGINE_MESHES_MAX; mesh++)
	{
		const EngineMeshDesc *desc = Engine_MeshDesc(mesh);
		u32 vb = 0xffffffffu;
		u32 ib = 0xffffffffu;
		u64 vBytes;
		u64 iBytes;
		NativeGfxVertexBufferDesc vDesc;
		NativeGfxIndexBufferDesc iDesc;
		NativeGfxBuffer vbuf;
		NativeGfxBuffer ibuf;

		if ((desc == NULL) || Engine_MeshIsExternal(mesh))
		{
			// An external mesh already has its buffers, which are not ours.
			continue;
		}
		Engine_MeshGetGpu(mesh, &vb, &ib);
		if ((vb != 0xffffffffu) || (ib != 0xffffffffu))
		{
			continue;
		}

		vBytes = (u64)desc->frames * desc->verticesPerFrame * sizeof(EngineVertex);
		iBytes = (u64)desc->indexCount * sizeof(u16);
		if ((vBytes == 0u) || (iBytes == 0u) || (vBytes > 0x7fffffffu) || (iBytes > 0x7fffffffu) || (desc->vertices == NULL) || (desc->indices == NULL) ||
		    (s_engLiveCount >= ENGINE_MESHES_MAX))
		{
			s_engCount.uploadFailed++;
			continue;
		}

		Engine_RenderFillVertexDesc(&vDesc, (int)vBytes, 0, desc->vertices);
		iDesc.bytes = (int)iBytes;
		iDesc.type = NATIVE_GFX_INDEX_U16;
		iDesc.initial = desc->indices;

		vbuf = NativeGfx_CreateVertexBuffer(&vDesc);
		ibuf = NativeGfx_CreateIndexBuffer(&iDesc);
		if (!Engine_RenderBufferValid(vbuf) || !Engine_RenderBufferValid(ibuf))
		{
			if (Engine_RenderBufferValid(vbuf))
			{
				NativeGfx_DestroyVertexBuffer(vbuf);
			}
			if (Engine_RenderBufferValid(ibuf))
			{
				NativeGfx_DestroyIndexBuffer(ibuf);
			}
			s_engCount.uploadFailed++;
			continue;
		}

		Engine_MeshSetGpu(mesh, (u32)vbuf, (u32)ibuf);
		s_engLiveVB[s_engLiveCount] = vbuf;
		s_engLiveIB[s_engLiveCount] = ibuf;
		s_engLiveCount++;
		uploaded++;
		bytes += vBytes + iBytes;
	}

	Platform_Log("[CTR Engine] meshes uploaded: %d (%llu bytes)\n", uploaded, (unsigned long long)bytes);
	// A mesh without buffers is refused at every draw (refusedBadMesh): the
	// driver or the track faces it carries would be missing from the picture.
	if (s_engCount.uploadFailed != failedBefore)
	{
		char reason[96];

		snprintf(reason, sizeof(reason), "%u mesh(es) could not be uploaded", s_engCount.uploadFailed - failedBefore);
		Engine_Fallback("level load", reason);
	}
	return uploaded;
}

// The buffers of the level that goes are not destroyed here, a frame may still
// name them: they are parked and destroyed at the next load (the next
// Engine_RenderUploadMeshes) or the next unload, whichever comes first - one
// load late, as the native layer's grave is (NativeCharGpu_EmptyGrave). The
// grave is emptied first, so it holds at most one level's buffers.
void Engine_RenderLevelUnload(void)
{
	if (!g_cfg_engine)
	{
		return;
	}

	Engine_RenderEmptyGrave();
	// The mosaic data texture goes with the level (retired by the backend, not destroyed under a frame).
	Engine_TrackMosaicUnload();
	for (int i = 0; i < s_engLiveCount; i++)
	{
		s_engGraveVB[i] = s_engLiveVB[i];
		s_engGraveIB[i] = s_engLiveIB[i];
	}
	s_engGraveCount = s_engLiveCount;
	s_engLiveCount = 0;

	// The handles in the mesh table die with the table.
	Engine_MeshesReset();
	Engine_MaterialsReset();
	Engine_OracleLevelUnload();

	// Nothing of the old level's draws may be drawn from either bank.
	for (int bank = 0; bank < ENGINE_SCENE_BANKS; bank++)
	{
		s_engMarkerCount[bank] = 0;
		for (int view = 0; view < ENGINE_VIEWS; view++)
		{
			s_engDrawCount[bank][view] = 0;
		}
		s_engDynUsed[bank] = 0;
	}
}

// Like NativeRenderLayer_RegisterMarkerArenas, which runs every frame: the link
// map is rebuilt per frame and later registrations get lower tokens, so this
// runs after it and the engine's two arenas are registered last. The bank that
// is filled now starts empty; the other one stays, its OT may still be read.
void Engine_RenderFrameBegin(int bank)
{
	if (!g_cfg_engine || (bank < 0) || (bank >= ENGINE_SCENE_BANKS))
	{
		return;
	}

	// An armed oracle frame ends at the swap: its RenderFrame is done.
	Engine_OracleFrameEnd();

	NativeGpuLinks_RegisterRangeChecked("engine native markers db0", s_engMarkers[0], sizeof(s_engMarkers[0]));
	NativeGpuLinks_RegisterRangeChecked("engine native markers db1", s_engMarkers[1], sizeof(s_engMarkers[1]));

	Engine_SceneSelectBank(bank);
	Engine_SceneBeginFrame();

	s_engMarkerCount[bank] = 0;
	s_engDynUsed[bank] = 0;
	s_engRegion[bank] = s_engFrameCounter % ENGINE_DYN_REGIONS;
	// The frame of the bank's region, for the streamed buffers (their arrays are per frame).
	s_engStreamFrame[bank] = s_engFrameCounter;
	s_engFrameCounter++;
	for (int view = 0; view < ENGINE_VIEWS; view++)
	{
		s_engDrawCount[bank][view] = 0;
		s_engDepthCleared[bank][view] = 0;
	}

	if (s_engRenderReady)
	{
		// The main target needs its depth image while the engine draws.
		NativeRenderer_WantNativeDepth(1);
	}
}

int Engine_RenderSubmitLinked(int view, const EngineDraw *draw, u32 *ot, int cell)
{
	const int bank = Engine_SceneBank();
	const int dynamic = (draw != NULL) && (draw->dynVertexCount > 0);
	u32 triangles = 0;
	int index;
	EngineRenderDraw *rec;
	DR_PSYX_NATIVE *marker;

	if (!s_engRenderReady || (Engine_Program() == 0xffffffffu))
	{
		s_engCount.submitRefusedNotReady++;
		return 0;
	}
	if ((draw == NULL) || (ot == NULL) || (cell < 0) || (cell > 0x3ff) || (view < 0) || (view >= ENGINE_VIEWS) || (bank < 0) || (bank >= ENGINE_SCENE_BANKS))
	{
		s_engCount.submitRefusedCell++;
		return 0;
	}
	if (s_engMarkerCount[bank] >= ENGINE_MARKERS_PER_BANK)
	{
		s_engCount.submitRefusedArena++;
		return 0;
	}
	if (dynamic)
	{
		// Room in the expanded store first, so the scene's index and ours stay
		// in step (a scene refusal below returns before anything is written).
		triangles = draw->dynIndexCount / 3u;
		if ((draw->dynIndices == NULL) || (draw->dynVertices == NULL) || (triangles == 0u) ||
		    ((triangles * 3u) > (ENGINE_DYN_EXPANDED - s_engDynUsed[bank])))
		{
			s_engCount.submitRefusedDyn++;
			return 0;
		}
		for (u32 i = 0; i < triangles * 3u; i++)
		{
			if (draw->dynIndices[i] >= draw->dynVertexCount)
			{
				s_engCount.submitRefusedDyn++;
				return 0;
			}
		}
	}

	index = Engine_SceneSubmit(view, draw);
	if ((index < 0) || (index >= ENGINE_DRAWS_PER_VIEW))
	{
		s_engCount.submitRefusedScene++;
		return 0;
	}

	// The table entry. The scene hands out indices 0,1,2.. in order, so this
	// entry is the one the marker will name.
	rec = &s_engDraws[bank][view][index];
	memcpy(rec->clipFromModel, draw->clipFromModel.m, sizeof(rec->clipFromModel));
	memcpy(rec->tint, draw->tint, sizeof(rec->tint));
	rec->frame = draw->frame;
	rec->mesh = draw->mesh;
	rec->materialOverride = draw->materialOverride;
	rec->dynMaterial = draw->dynMaterial;
	rec->flags = draw->flags;
	// EngineDraw is zeroed by its callers: split all 0 and splitKeep 0 mean no split.
	memcpy(rec->split, draw->split, sizeof(rec->split));
	rec->splitKeep = draw->splitKeep;
	memcpy(rec->user, draw->user, sizeof(rec->user));
	memcpy(rec->user2, draw->user2, sizeof(rec->user2));
	rec->rangeSkip = draw->rangeSkip;
	rec->time = Engine_FrameTime(bank);
	rec->dynFirst = 0;
	rec->dynCount = 0;
	if (dynamic)
	{
		EngineVertex *out = &s_engDynCpu[bank][s_engDynUsed[bank]];

		for (u32 i = 0; i < triangles * 3u; i++)
		{
			out[i] = draw->dynVertices[draw->dynIndices[i]];
		}
		rec->dynFirst = s_engDynUsed[bank];
		rec->dynCount = triangles * 3u;
		s_engDynUsed[bank] += triangles * 3u;
	}
	if (index >= s_engDrawCount[bank][view])
	{
		s_engDrawCount[bank][view] = index + 1;
	}

	// The marker, written as NativeRenderLayer_LinkMarkerInCell writes it: the
	// two code words, the tag with length 2, then the cell points to it.
	marker = &s_engMarkers[bank][s_engMarkerCount[bank]];
	marker->code[1] = 0;
	marker->code[0] = ((u32)PSYX_NATIVE_CODE << 24) | (ENGINE_MARKER_ITEM(bank, view, index) & 0xFFFFFFu);
	CTR_GPU_WriteTagWord(marker, CtrGpu_PackOTTag(ot[cell], 0x02000000u));
	ot[cell] = CtrGpu_PrimToOTLink24(marker);
	s_engMarkerCount[bank]++;
	s_engCount.submitted++;
	s_engCount.submittedPerView[view & (ENGINE_VIEWS - 1)]++;
	// The oracle's E lines of the source open now (only in an armed frame).
	Engine_OracleNoteSubmit(view, draw);
	return 1;
}

// Once-only diagnostics (defined below): the first refusal of each kind.
internal void Engine_RenderFirstRefusal(int kind, const char *reason);

// The orientation of a draw as the native layer takes it (NativeRenderLayer
// item->cull from the determinant of the model-to-view rotation): the sign of
// the 3x3 determinant of rows x, y and w of the clip matrix, which is that
// rotation scaled by H*H (the screen offset only adds multiples of the w row).
// Column-major: element (row r, column c) is m[c * 4 + r].
internal NativeGfxCull Engine_RenderCullOfMatrix(const float *m)
{
	const double a00 = m[0], a01 = m[4], a02 = m[8];
	const double a10 = m[1], a11 = m[5], a12 = m[9];
	const double a20 = m[3], a21 = m[7], a22 = m[11];
	const double det = a00 * (a11 * a22 - a12 * a21) - a01 * (a10 * a22 - a12 * a20) + a02 * (a10 * a21 - a11 * a20);

	return (det >= 0.0) ? NATIVE_GFX_CULL_BACK : NATIVE_GFX_CULL_FRONT;
}

internal BlendMode Engine_RenderBlendMode(u8 blend)
{
	switch (blend)
	{
	case ENGINE_BLEND_PSX_AVERAGE:
		return BM_AVERAGE;
	case ENGINE_BLEND_PSX_ADD:
		return BM_ADD;
	case ENGINE_BLEND_PSX_SUBTRACT:
		return BM_SUBTRACT;
	case ENGINE_BLEND_PSX_ADD_QUARTER:
		return BM_ADD_QUATER_SOURCE;
	case ENGINE_BLEND_MULTIPLY:
		return BM_MULTIPLY;
	case ENGINE_BLEND_ALPHA:
		return BM_ALPHA;
	default:
		return BM_NONE;
	}
}

// The two tints of a draw of "ns" (include/platform/engine.h, ENGINE_PROGRAM_NS).
// The material tint is a linear value (glTF baseColorFactor): "ns" puts it on
// the display curve. The draw tint is the game's colour of the draw (the depth
// cue and fade of a driver, EngCmdl_Tints): a factor on display values, the way
// "ne" multiplies it for the classic driver (v_color = a_color * tint). So it
// goes apart, in vramClut ("ns" reads no CLUT), and is not put on the curve.
// Alpha is the product, as for every program. Pure (the selftest calls it).
void Engine_NsTints(const float materialTint[4], const float drawTint[4], float tint[4], float displayTint[4])
{
	int i;

	for (i = 0; i < 3; i++)
	{
		tint[i] = materialTint[i];
		displayTint[i] = drawTint[i];
	}
	tint[3] = materialTint[3] * drawTint[3];
	displayTint[3] = 1.0f;
}

// One range (or the one dynamic range): uniforms, texture, blend, draw state and
// the draw call(s). first/count are indices of the bound index buffer when
// `indexed`, else vertices of the bound dynamic buffer from `vertexOffset`.
// Returns 1 if something was drawn. *outTexture is set when slot 0 changed,
// *outVram when the VRAM unit changed (both restored by the caller).
// *boundProgram is the program bound now (the caller binds "ne" first; a range
// of another program binds its own, and the next range rebinds as needed).
internal int Engine_RenderDrawRange(const EngineMaterial *material, const EngineRenderDraw *draw, const float *clipFromModel, NativeGfxCull cull, int indexed,
                                    u32 first, u32 count, int vertexOffset, int *outTexture, int *outVram, u32 *boundProgram)
{
	const u32 program = Engine_ProgramOf((int)material->program);
	EngineUniforms u;
	NativeGfxDrawState state;
	const int table = (material->texSource == ENGINE_TEX_VRAM_TABLE);
	const int vram = (material->texSource == ENGINE_TEX_VRAM) || table;
	const u32 tableTexture = table ? Engine_TrackMosaicTexture() : NATIVE_GFX_INVALID;
	const int native = (material->texSource == ENGINE_TEX_NATIVE) && (material->nativeTexture != NATIVE_GFX_INVALID);
	const BlendMode blend = Engine_RenderBlendMode(material->blend);
	const int transparent = (draw->flags & ENGINE_DRAW_TRANSPARENT) != 0u;
	// The PS1 rule: in a semi-transparent VRAM face only texels with the STP bit
	// blend, the others stay opaque. Two passes, the opaque one first.
	const int twoPass = vram && (blend != BM_NONE) && (blend != BM_MULTIPLY) && ((material->flags & ENGINE_MATERIAL_SEMI_STP) != 0u);

	if (count == 0u)
	{
		return 0;
	}
	if (program == 0xffffffffu)
	{
		s_engCount.refusedNoProgram++;
		Engine_RenderFirstRefusal(8, "no program for the material");
		return 0;
	}
	if (table && (tableTexture == NATIVE_GFX_INVALID))
	{
		// A table face without its data texture would read the fallback texture.
		s_engCount.refusedBadMaterial++;
		Engine_RenderFirstRefusal(9, "mosaic table range without its data texture");
		return 0;
	}
	if (program != *boundProgram)
	{
		NativeGfx_BindProgram((ShaderID)program);
		*boundProgram = program;
	}

	memset(&u, 0, sizeof(u));
	memcpy(u.clipFromModel, clipFromModel, sizeof(u.clipFromModel));
	for (int i = 0; i < 4; i++)
	{
		u.tint[i] = material->tint[i] * draw->tint[i];
	}
	u.params[0] = table ? 3.0f : (vram ? 2.0f : (native ? 1.0f : 0.0f));
	u.params[1] = (native && ((material->flags & ENGINE_MATERIAL_SRGB) != 0u)) ? 1.0f : 0.0f;
	u.params[2] = (material->blend == ENGINE_BLEND_MASK) ? material->alphaCutoff : 0.0f;
	u.params[3] = 0.0f;
	u.params2[0] = vram ? 2.0f : 1.0f;
	// splitKeep 0 (every draw that does not set it) keeps everything.
	u.params2[1] = (draw->splitKeep > 0) ? 1.0f : ((draw->splitKeep < 0) ? -1.0f : 0.0f);
	// The material's depth offset (track decals): the vertex stage multiplies clip z by 1 + this. 0 for every material made without one.
	u.params2[2] = material->depthBias;
	// Table flags: 1 = a cell texel smaller than a pixel falls back to the group's levels 2, 1, 0.
	u.params2[3] = table ? 1.0f : 0.0f;
	memcpy(u.split, draw->split, sizeof(u.split));
	u.params3[0] = (float)material->program;
	u.params3[1] = draw->time;
	u.params3[2] = material->programParam[0];
	u.params3[3] = material->programParam[1];
	memcpy(u.params4, draw->user, sizeof(u.params4));
	// "ns" reads no VRAM page: its STYLE travels in that vec4 (engine.h, ENGINE_PROGRAM_NS);
	// nor a CLUT: the draw tint travels in that one, apart from the material tint (Engine_NsTints).
	// A material of "ns" has a native texture or none, never VRAM, so the block below leaves both.
	if (material->program == ENGINE_PROGRAM_NS)
	{
		memcpy(u.vramPage, draw->user2, sizeof(u.vramPage));
		Engine_NsTints(material->tint, draw->tint, u.tint, u.vramClut);
	}
	if (vram)
	{
		const EngineTpage tpage = Engine_DecodeTpage(material->tpage);
		int clutX = 0;
		int clutY = 0;

		Engine_DecodeClut(material->clut, &clutX, &clutY);
		u.vramPage[0] = (float)tpage.baseX;
		u.vramPage[1] = (float)tpage.baseY;
		u.vramPage[2] = (float)tpage.depth;
		u.vramClut[0] = (float)clutX;
		u.vramClut[1] = (float)clutY;

		NativeGfx_BindTexture(ENGINE_VRAM_UNIT, NativeRenderer_GetVRAMTexture(), NATIVE_GFX_FILTER_NEAREST);
		*outVram = 1;
		if (table)
		{
			NativeGfx_BindTexture(ENGINE_TABLE_UNIT, (TextureID)tableTexture, NATIVE_GFX_FILTER_NEAREST);
			s_engCount.tableRanges++;
		}
	}
	else if (native)
	{
		NativeGfx_BindTexture(0, (TextureID)material->nativeTexture, NATIVE_GFX_FILTER_KEEP);
		*outTexture = 1;
		// "ns": the normal map in slot 3 (ENGINE_TABLE_UNIT). Nothing of the PSX path reads
		// slot 3, so there is nothing to restore afterwards.
		if ((material->program == ENGINE_PROGRAM_NS) && (material->nativeTexture2 != 0u) && (material->nativeTexture2 != 0xffffffffu))
		{
			NativeGfx_BindTexture(ENGINE_TABLE_UNIT, (TextureID)material->nativeTexture2, NATIVE_GFX_FILTER_KEEP);
		}
	}

	state.depthTest = ((draw->flags & ENGINE_DRAW_NO_DEPTH) == 0u) ? 1 : 0;
	state.depthWrite = (state.depthTest && !transparent && ((material->flags & ENGINE_MATERIAL_NO_DEPTH_WRITE) == 0u)) ? 1 : 0;
	state.depthCompare = NATIVE_GFX_COMPARE_GREATER_OR_EQUAL;
	state.cull = ((material->flags & ENGINE_MATERIAL_DOUBLE_SIDED) != 0u) ? NATIVE_GFX_CULL_NONE : cull;
	state.colorWriteOff = NATIVE_GFX_COLOR_A;
	NativeGfx_SetDrawState(&state);

	for (int pass = 0; pass < (twoPass ? 2 : 1); pass++)
	{
		if (twoPass)
		{
			u.params[3] = (pass == 0) ? 1.0f : 2.0f;
			NativeGfx_SetBlendMode((pass == 0) ? BM_NONE : blend);
		}
		else
		{
			NativeGfx_SetBlendMode(blend);
		}
		NativeGfx_UpdateUniforms((ShaderID)program, &u);
		if (indexed)
		{
			NativeGfx_DrawIndexed((int)first, (int)count, vertexOffset);
		}
		else
		{
			NativeGfx_Draw(vertexOffset + (int)first, (int)count);
		}
	}
	return 1;
}

// The material a range is drawn with. W3 of the oracle (--engine-oracle-fault,
// measuring only): the one faulted material is drawn from its copy with the
// wrong clut (Engine_OracleFaultMaterial); otherwise the table's entry.
internal const EngineMaterial *Engine_RenderMaterialAt(int index)
{
	if ((g_engineOracleFaultMaterial >= 0) && (index == g_engineOracleFaultMaterial))
	{
		return Engine_OracleFaultMaterial();
	}
	return Engine_Material(index);
}

internal const EngineMaterial *Engine_RenderMaterialFor(const EngineRenderDraw *draw, int rangeMaterial)
{
	return Engine_RenderMaterialAt((draw->materialOverride >= 0) ? draw->materialOverride : rangeMaterial);
}

// Once-only diagnostics: the first refusal of each kind and the first marker drawn.
internal void Engine_RenderFirstRefusal(int kind, const char *reason)
{
	global_variable int s_seen[16];

	if ((kind >= 0) && (kind < 16) && !s_seen[kind])
	{
		s_seen[kind] = 1;
		Platform_Log("[CTR Engine] first refusal: %s\n", reason);
	}
}

int Engine_DrawMarker(u32 item, const void *clipArg, const void *dispenvArg, int onScreen, int ofsX, int ofsY)
{
	const RECT16 *clip = (const RECT16 *)clipArg;
	const DISPENV *dispenv = (const DISPENV *)dispenvArg;
	const int bank = (int)((item >> 20) & 1u);
	const int view = (int)((item >> 16) & 3u);
	const int index = (int)(item & 0xFFFFu);
	const EngineRenderDraw *draw;
	const EngineMeshDesc *mesh = NULL;
	EngineMat4 clipFromModel;
	NativeGfxCull cull;
	int textureBound = 0;
	int vramBound = 0;
	int drawn = 0;
	u32 boundProgram;
	u32 vb = 0xffffffffu;
	u32 ib = 0xffffffffu;
	EngineRenderStream *stream = NULL;
	int streamOffset = 0;

	s_engCount.markersSeen++;

	// Item layout (ENGINE_MARKER_ITEM): index bits 0-15, view bits 16-17, bank bit 20,
	// flag bit 22. Reserved, never set: bits 18, 19, 21, 23 (mask 0xAC0000).
	if (((item & ENGINE_MARKER_FLAG) == 0u) || ((item & 0xAC0000u) != 0u) || (index >= s_engDrawCount[bank][view]))
	{
		s_engCount.refusedBadItem++;
		Engine_RenderFirstRefusal(0, "bad item (flag, bits or index >= draw count)");
		return 0;
	}
	if (Engine_Program() == 0xffffffffu)
	{
		s_engCount.refusedNoProgram++;
		Engine_RenderFirstRefusal(1, "no program");
		return 0;
	}
	if ((clip == NULL) || (clip->w <= 0) || (clip->h <= 0))
	{
		s_engCount.refusedClip++;
		Engine_RenderFirstRefusal(2, "bad clip rect");
		return 0;
	}
	if (!onScreen || !NativeGfx_TargetDepth(s_mainRenderTarget))
	{
		s_engCount.refusedOffscreenOrNoDepth++;
		Engine_RenderFirstRefusal(3, "offscreen or target has no depth");
		return 0;
	}

	draw = &s_engDraws[bank][view][index];
	if (draw->dynCount > 0u)
	{
		if (!Engine_RenderBufferValid(s_engDynBuffer))
		{
			s_engCount.refusedBadMesh++;
			Engine_RenderFirstRefusal(4, "bad mesh/frame/buffer");
			return 0;
		}
	}
	else
	{
		mesh = Engine_MeshDesc(draw->mesh);
		if (mesh != NULL)
		{
			Engine_MeshGetGpu(draw->mesh, &vb, &ib);
		}
		if ((mesh == NULL) || (vb == 0xffffffffu) || (ib == 0xffffffffu) || (draw->frame >= mesh->frames) || (mesh->rangeCount == 0u))
		{
			s_engCount.refusedBadMesh++;
			Engine_RenderFirstRefusal(4, "bad mesh/frame/buffer");
			return 0;
		}
		// A streamed buffer (engine.h, RENDER): its array of this frame or no draw.
		stream = (s_engStreamCount > 0) ? Engine_RenderStreamOf(vb) : NULL;
		if ((stream != NULL) && !Engine_RenderStreamReady(stream, bank, 0, &streamOffset))
		{
			s_engCount.refusedBadMesh++;
			Engine_RenderFirstRefusal(10, "streamed buffer without its array of this frame");
			return 0;
		}
	}

	NativeRenderer_SetupClipMode(clip, dispenv, onScreen);
	NativeRenderer_SetOffscreenState(clip, !onScreen);
	NativeRenderer_SetProjection(clip, dispenv, !onScreen);

	// The PSX ortho projection in front of the draw's screen-from-model matrix:
	// the same product as NativeRenderer_DrawNativeMesh (x and y scale and move,
	// z and w pass through).
	{
		const float *p = s_psxUniforms.projection;
		const float *s = draw->clipFromModel;

		for (int column = 0; column < 4; column++)
		{
			const double s0 = (double)s[column * 4 + 0];
			const double s1 = (double)s[column * 4 + 1];
			const double s2 = (double)s[column * 4 + 2];
			const double s3 = (double)s[column * 4 + 3];

			// The split's draw offset, as NativeRenderLayer_ScreenFromView adds it:
			// screen.x = H*x + (rectW/2 + ofsX)*z, so the offset multiplies the w
			// row. The matrix already holds rectW/2 and rectH/2.
			const double t0 = s0 + (double)ofsX * s3;
			const double t1 = s1 + (double)ofsY * s3;

			clipFromModel.m[column * 4 + 0] = (float)((double)p[0] * t0 + (double)p[12] * s3);
			clipFromModel.m[column * 4 + 1] = (float)((double)p[5] * t1 + (double)p[13] * s3);
			clipFromModel.m[column * 4 + 2] = (float)s2;
			clipFromModel.m[column * 4 + 3] = (float)s3;
		}
	}
	cull = Engine_RenderCullOfMatrix(draw->clipFromModel);

	// One depth clear per view and frame, at the first engine draw of the view:
	// the scissor box the clip just set up.
	if (!s_engDepthCleared[bank][view])
	{
		int boxX = 0;
		int boxY = 0;
		int boxW = 0;
		int boxH = 0;

		NativeRenderer_ScissorBoxInForce(&boxX, &boxY, &boxW, &boxH);
		NativeGfx_ClearDepth(boxX, boxY, boxW, boxH);
		s_engDepthCleared[bank][view] = 1;
	}

	NativeGfx_SetBlendMode(BM_NONE);
	NativeGfx_BindProgram((ShaderID)Engine_Program());
	boundProgram = Engine_Program();

	if (draw->dynCount > 0u)
	{
		const EngineMaterial *material = Engine_RenderMaterialAt(draw->dynMaterial);
		const u32 firstVertex = s_engRegion[bank] * (u32)ENGINE_DYN_EXPANDED + draw->dynFirst;

		if (material == NULL)
		{
			s_engCount.refusedBadMaterial++;
			Engine_RenderFirstRefusal(5, "bad material (dynamic)");
		}
		else
		{
			NativeGfx_UpdateVertexBuffer(s_engDynBuffer, (int)(firstVertex * sizeof(EngineVertex)), (int)(draw->dynCount * sizeof(EngineVertex)),
			                             &s_engDynCpu[bank][draw->dynFirst]);
			NativeGfx_BindVertexBuffer(s_engDynBuffer);
			// The slice just written starts at firstVertex of the buffer.
			drawn += Engine_RenderDrawRange(material, draw, clipFromModel.m, cull, 0, 0, draw->dynCount, (int)firstVertex, &textureBound, &vramBound,
			                                &boundProgram);
		}
	}
	else
	{
		if (stream != NULL)
		{
			// The copy of this frame's array into its region, once per frame (checked above).
			(void)Engine_RenderStreamReady(stream, bank, 1, &streamOffset);
		}
		NativeGfx_BindVertexBuffer((NativeGfxBuffer)vb);
		NativeGfx_BindIndexBuffer((NativeGfxBuffer)ib);

		for (u32 r = 0; r < mesh->rangeCount; r++)
		{
			const EngineRange *range = &mesh->ranges[r];
			const EngineMaterial *material = Engine_RenderMaterialFor(draw, range->material);

			// A range the draw leaves out (the other layouts of an animated slot, Engine_ModelRangeSkip).
			if (Engine_RangeSkipped(draw->rangeSkip, r))
			{
				continue;
			}
			if (material == NULL)
			{
				s_engCount.refusedBadMaterial++;
				Engine_RenderFirstRefusal(6, "bad material");
				continue;
			}
			if ((range->indexCount == 0u) || (range->firstIndex > mesh->indexCount) || (range->indexCount > (mesh->indexCount - range->firstIndex)))
			{
				s_engCount.refusedBadRange++;
				Engine_RenderFirstRefusal(7, "bad range");
				continue;
			}
			drawn += Engine_RenderDrawRange(material, draw, clipFromModel.m, cull, 1, range->firstIndex, range->indexCount,
			                                (stream != NULL) ? streamOffset : (int)(draw->frame * mesh->verticesPerFrame), &textureBound, &vramBound,
			                                &boundProgram);
		}
	}

	// Back to what the PSX path relies on, exactly as NativeRenderer_DrawNativeMesh
	// leaves it: draw state NULL, the PSX vertex buffer rebound, program unknown,
	// blend none, PSX uniforms dirty, slot 0 restored when a range changed it.
	NativeGfx_SetDrawState(NULL);
	if (s_boundVertexBuffer >= 0)
	{
		NativeGfx_BindVertexBuffer(s_vertexBuffer[s_boundVertexBuffer]);
	}
	else
	{
		NativeGfx_BindVertexBuffer(NATIVE_GFX_INVALID);
	}
	s_previousShader = (ShaderID)-1;
	s_previousBlendMode = BM_NONE;
	NativeGfx_SetBlendMode(BM_NONE);
	NativeRenderer_MarkPSXUniformsDirty();

	if (vramBound)
	{
		// Unit 1 is the immutable PSX colour lookup table.
		NativeGfx_BindTexture(ENGINE_LUT_UNIT, s_rgLutTexture, NATIVE_GFX_FILTER_NEAREST);
	}
	if (textureBound)
	{
		NativeGfx_BindTexture(0, (s_lastBoundTexture == (TextureID)-1) ? 0 : s_lastBoundTexture, NATIVE_GFX_FILTER_KEEP);
	}

	s_engCount.rangesDrawn += (u32)drawn;
	if (drawn > 0)
	{
		if (s_engCount.drawn == 0u)
		{
			Platform_Log("[CTR Engine] first marker drawn: bank %d view %d index %d, ofs %d,%d\n", bank, view, index, ofsX, ofsY);
		}
		s_engCount.drawn++;
	}
	return (drawn > 0) ? 1 : 0;
}

void Engine_RenderCountersGet(EngineRenderCounters *out)
{
	if (out == NULL)
	{
		return;
	}
	out->drawn = s_engCount.drawn;
	out->markers = s_engCount.markersSeen;
	out->ranges = s_engCount.rangesDrawn;
	out->submitted = s_engCount.submitted;
	out->refusedNotReady = s_engCount.submitRefusedNotReady;
	out->refusedScene = s_engCount.submitRefusedScene;
	out->refusedCell = s_engCount.submitRefusedCell;
	out->refusedArena = s_engCount.submitRefusedArena;
	out->refusedDyn = s_engCount.submitRefusedDyn;
	out->refusedAtDraw = s_engCount.refusedBadItem + s_engCount.refusedNoProgram + s_engCount.refusedClip + s_engCount.refusedOffscreenOrNoDepth +
	                     s_engCount.refusedBadMesh + s_engCount.refusedBadMaterial + s_engCount.refusedBadRange;
	out->uploadFailed = s_engCount.uploadFailed;
}

void Engine_RenderReport(void)
{
	// After a fallback as well: the counters say what the engine drew before it.
	if (!g_cfg_engine && !Engine_FellBack())
	{
		return;
	}

	Platform_Log("[CTR Engine] at exit: %u draw(s), %u marker(s), %u range(s); submitted %u; "
	             "refused at submit: not ready %u, scene %u, cell/args %u, arena %u, dynamic %u; "
	             "refused at draw: item %u, program %u, clip %u, offscreen/no depth %u, mesh %u, material %u, range %u; upload failed %u\n",
	             s_engCount.drawn, s_engCount.markersSeen, s_engCount.rangesDrawn, s_engCount.submitted, s_engCount.submitRefusedNotReady,
	             s_engCount.submitRefusedScene, s_engCount.submitRefusedCell, s_engCount.submitRefusedArena, s_engCount.submitRefusedDyn,
	             s_engCount.refusedBadItem, s_engCount.refusedNoProgram, s_engCount.refusedClip, s_engCount.refusedOffscreenOrNoDepth,
	             s_engCount.refusedBadMesh, s_engCount.refusedBadMaterial, s_engCount.refusedBadRange, s_engCount.uploadFailed);
	Platform_Log("[CTR Engine] submits per view: %u %u %u %u\n", s_engCount.submittedPerView[0], s_engCount.submittedPerView[1],
	             s_engCount.submittedPerView[2], s_engCount.submittedPerView[3]);
	Platform_Log("[CTR Engine] track mosaic: table ranges drawn %u\n", s_engCount.tableRanges);
}
