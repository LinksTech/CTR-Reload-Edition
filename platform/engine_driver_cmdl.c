#include <platform/engine.h>

#include <stdio.h>
#include <string.h>

// ENGINE DRIVER CMDL - custom drivers whose file has only a CMDL, drawn by the
// engine (round of 2026-10-13). See include/platform/engine.h, CUSTOM DRIVERS WITH
// ONLY A CMDL.
//
// A bound custom seat whose .rldchar has no CNET has no native set (no
// s_ncgSeatSet entry, so no engine body mesh from Engine_DriverLoad); the game
// births its driver with the file's CMDL (NativeChar_SeatModel, VehBirth.c).
// This file imports that CMDL once per level with Engine_ModelImport, so that
// Engine_FxOwnsDriver says "owned" for the seat (shadow, exhaust, flames, skid
// marks and tires of the engine) and the BODY goes through Engine_InstanceSubmit
// instead of the retail handler.
//
// DRAW TIME ONLY: the game's animation word, matrices and colours are only
// read. Decisions, in the order the code takes them:
//   - WHEN: the seat models are bound at load stage 5 before the call
//     (LOAD_TenStages.c: NativeChar_ArmSeats sets s_seat[].model, then
//     NativeCharGpu_LoadSeats, Engine_DriverLoad, this load, then the upload),
//     so NativeChar_SeatModel is final here. A seat with a set but without an
//     engine body mesh (upload refused, "using CMDL") is imported too: it has
//     no engine body, and the CMDL is what the game draws for it.
//   - WHICH INSTANCE: the body instance of a driver is gGT->drivers[seat]->
//     instSelf (VehBirth.c: d->instSelf = t->inst); the thread of that
//     instance carries the driver as its object. Both are tested, so no
//     other instance (flames, wake, tires) and no ghost is taken.
//   - THE POSE: the frame the retail handler draws with is idpp->ptrCurrFrame,
//     picked by RenderBucket_GetFrame from the animation word BEFORE
//     RenderBucket_AdvanceInstanceAnimWord moves inst->animFrame on (player 0,
//     gameMode1 == 0, RenderBucket_QueueDraw). At dispatch time the instance's
//     word may already be one frame ahead, so it is NOT read; the pointer is
//     turned back into (animation, frame) by NativeRenderLayer_FrameOf, the
//     inverse of GetFrame, exactly as the layer's poseCurrent is made
//     (NativeRenderLayer_FillPose). Frame numbers are stored frames, the same
//     as NativeRenderLayer_AnimFrameNumber gives (half-frame animations
//     halved), which is what Engine_ModelFrame takes. A half frame (odd word:
//     retail blends the current and the next frame) draws the current stored
//     frame: the engine does not rebuild the PS1 in-between pose. Only when
//     the pointer names no frame the instance's own word is the fallback.
//   - THE COLOUR: the retail body is vertex coloured (no lighting in the CTR
//     retail path), then the DPCT depth cue fades each vertex toward the far
//     colour by a = alphaScale / 4096 (RenderBucket_LoadPrimColors). The far
//     colour is set by the setup callback inst->funcPtr[0]: for the light /
//     colour / fade-colour callbacks it is colorRGBA (>> 20, >> 12, >> 4)
//     & 0xff, for the zero-colour callback 0 (RenderBucket_RunInstanceSetup
//     Callback). The tint is (1 - a) + far * a per channel, the same as the
//     turbo flames and the layer's twin (it->tone / it->far). A callback the
//     dispatch does not know never reaches the route (it returns first). The
//     light callback also loads a specular light (specLightX, halfVector)
//     that the engine does not use: its far colour is taken, the specular
//     part is not drawn.
//   - UNDER WATER: the retail water split shades the part below the line by
//     (colour >> specLightX) & reflectionRGBA per vertex
//     (RenderBucket_WaterSplitShiftMaskColor; INSTANCE.c gives a driver 1 and
//     0x7f7f7f, i.e. about half brightness). As a tint: per channel
//     ((0xff >> specLightX) & mask byte) / 255 on top of the tint, exact for a
//     white vertex, close for the others. A mask of zero in all channels is
//     taken as the instance's default (0x7f7f7f), never as black.
//   - MIRROR: mirrorTint = tint (the reflection pass retail draws with the
//     same colour setup).
//   - LOD: header 0 only (Engine_ModelImport imports only the most detailed
//     header; Engine_InstanceWorldMatrix uses its scale). The retail LOD pick
//     by distance (idpp->mh) is a PS1 drawing economy and not followed; idpp->mh
//     is only used to find the frame the retail handler drew.
//   - CULL: the importer writes a one-sided triangle for the engine's
//     back-face cull the way retail keeps it when REVERSE_CULL_DIRECTION is
//     clear, and a two-sided one into a DOUBLE_SIDED material; the world
//     matrix of an instance has a positive determinant, so the winding holds.
//     The engine has no inverted cull, so a view whose instance flags have
//     REVERSE_CULL_DIRECTION (idpp->instFlags) is declined and retail draws.
//   - DECLINED: anything the route does not draw (return 0) is drawn by the
//     retail handler, which RenderBucket_DispatchDrawFunc counts through
//     Engine_DriverNoteRetailBody. The route counts nothing in that case.

#define ENG_CMDL_SEATS 8

typedef struct
{
	const struct Model *model;
	EngineModelImport imp;
	int transparent;
	int failed;
} EngCmdlModel;

internal EngCmdlModel s_engCmdlModels[ENG_CMDL_SEATS]; // one per distinct Model pointer
internal int s_engCmdlSeat[ENG_CMDL_SEATS];            // seat -> index in s_engCmdlModels, -1 = not the engine's
internal int s_engCmdlInit = 0;

internal void EngCmdl_ResetSeats(void)
{
	int i;

	for (i = 0; i < ENG_CMDL_SEATS; i++)
	{
		s_engCmdlSeat[i] = -1;
	}
	s_engCmdlInit = 1;
}

// The driver whose body instance this is (0..7), -1 when it is none.
internal int EngCmdl_SeatOfBody(const struct GameTracker *gGT, const struct Instance *inst)
{
	int seat;

	for (seat = 0; seat < ENG_CMDL_SEATS; seat++)
	{
		const struct Driver *d = gGT->drivers[seat];

		if ((d != NULL) && (d->instSelf == inst))
		{
			return seat;
		}
	}
	return -1;
}

void Engine_DriverCmdlLoad(void)
{
	int seat;
	int drawn = 0;

	memset(s_engCmdlModels, 0, sizeof(s_engCmdlModels));
	EngCmdl_ResetSeats();
	if (!g_cfg_engine)
	{
		return;
	}
	// The hi-res driver of --engine-hires-dir (engine_hires.c): its files once,
	// its meshes and material at every load, uploaded with the others.
	Engine_HiresLoad();
	// The hi-res drivers of the .rldchar (--engine-hires-chars, engine_hires_char.c).
	Engine_HiresCharsLoad();

	for (seat = 0; seat < ENG_CMDL_SEATS; seat++)
	{
		const struct Model *model = NativeChar_SeatModel(seat);
		const int set = Engine_DriverSetOfSeat(seat);
		const char *file;
		int slot = -1;
		int i;

		if (model == NULL)
		{
			continue; // retail seat (or the guard failed): nothing to say
		}
		file = NativeChar_SeatFile(seat);
		if ((set >= 0) && (Engine_DriverBodyMesh(set) >= 0))
		{
			Platform_Log("[CTR Engine] driver cmdl seat %d: %s not imported (the engine draws its native set)\n", seat, file);
			continue;
		}

		// One import per Model pointer: seats on the same file share it.
		for (i = 0; i < ENG_CMDL_SEATS; i++)
		{
			if (s_engCmdlModels[i].model == model)
			{
				slot = i;
				break;
			}
		}
		if (slot < 0)
		{
			for (i = 0; i < ENG_CMDL_SEATS; i++)
			{
				if (s_engCmdlModels[i].model == NULL)
				{
					slot = i;
					break;
				}
			}
			if (slot < 0)
			{
				Platform_Log("[CTR Engine] driver cmdl seat %d: %s not imported (table full)\n", seat, file);
				continue;
			}
			s_engCmdlModels[slot].model = model;
			if (!Engine_ModelImport(model, "driver cmdl", &s_engCmdlModels[slot].imp) || (s_engCmdlModels[slot].imp.mesh < 0))
			{
				s_engCmdlModels[slot].failed = 1;
			}
			else
			{
				for (i = 0; i < s_engCmdlModels[slot].imp.materialCount; i++)
				{
					const EngineMaterial *m = Engine_Material(s_engCmdlModels[slot].imp.firstMaterial + i);

					if ((m != NULL) && (m->blend != ENGINE_BLEND_OPAQUE) && (m->blend != ENGINE_BLEND_MASK))
					{
						s_engCmdlModels[slot].transparent = 1;
					}
				}
			}
		}
		if (s_engCmdlModels[slot].failed)
		{
			Platform_Log("[CTR Engine] driver cmdl seat %d: %s not imported (Engine_ModelImport failed)\n", seat, file);
			continue;
		}
		s_engCmdlSeat[seat] = slot;
		drawn++;
		Platform_Log("[CTR Engine] driver cmdl seat %d: model %.15s (%s), mesh %d, %d frames, %d materials\n", seat, model->name, file,
		             s_engCmdlModels[slot].imp.mesh, s_engCmdlModels[slot].imp.frameTotal, s_engCmdlModels[slot].imp.materialCount);
	}
	Platform_Log("[CTR Engine] driver cmdl: %d seat(s) drawn by the engine\n", drawn);
}

// Cheap (a few compares): called per producer per driver per frame.
int Engine_DriverCmdlOwns(const struct Driver *d)
{
	const struct GameTracker *gGT = sdata->gGT;
	int seat;
	int slot;

	if (!g_cfg_engine || !s_engCmdlInit || (d == NULL) || (gGT == NULL))
	{
		return 0;
	}
	// The hi-res driver's seat (engine_hires.c) is the engine's, whatever its model.
	if (Engine_HiresOwns(d))
	{
		return 1;
	}
	seat = (int)d->driverID;
	if ((seat < 0) || (seat >= ENG_CMDL_SEATS) || (gGT->drivers[seat] != d))
	{
		return 0;
	}
	slot = s_engCmdlSeat[seat];
	if ((slot < 0) || (d->instSelf == NULL))
	{
		return 0;
	}
	return (d->instSelf->model == s_engCmdlModels[slot].model) ? 1 : 0;
}

// The mesh must be on the GPU: an upload inside a race frame is not allowed
// (as in engine_fx_turbo.c), so a model without buffers is left to retail.
internal int EngCmdl_MeshReady(int mesh)
{
	u32 vb = 0xffffffffu;
	u32 ib = 0xffffffffu;

	if ((mesh < 0) || (Engine_MeshDesc(mesh) == NULL))
	{
		return 0;
	}
	Engine_MeshGetGpu(mesh, &vb, &ib);
	return ((vb != 0xffffffffu) && (ib != 0xffffffffu)) ? 1 : 0;
}

// The tints of a driver body draw (header comment, THE COLOUR, UNDER WATER and
// MIRROR): tint, belowTint, mirrorTint of draw; also used by engine_hires.c.
internal void EngCmdl_Tints(const struct Instance *inst, const struct InstDrawPerPlayer *idpp, EngineInstanceDraw *draw)
{
	float a;
	float far3[3];
	float below[3];
	int c;

	// The tint: DPCT toward the far colour by alphaScale / 4096.
	a = ((int)idpp->alphaScale <= 0) ? 0.0f : (((int)idpp->alphaScale >= 0x1000) ? 1.0f : ((float)idpp->alphaScale / 4096.0f));
	{
		const u32 setup = (u32)(size_t)inst->funcPtr[0];

		if ((setup == RB_RETAIL_INST_SETUP_LIGHT_COLOR) || (setup == RB_RETAIL_INST_SETUP_COLOR) || (setup == RB_RETAIL_INST_SETUP_FADE_COLOR))
		{
			far3[0] = (float)((inst->colorRGBA >> 20) & 0xffu) / 255.0f;
			far3[1] = (float)((inst->colorRGBA >> 12) & 0xffu) / 255.0f;
			far3[2] = (float)((inst->colorRGBA >> 4) & 0xffu) / 255.0f;
		}
		else
		{
			far3[0] = far3[1] = far3[2] = 0.0f;
		}
	}

	// The part below the water line: (colour >> specLightX) & reflectionRGBA.
	{
		const u32 mask = ((inst->reflectionRGBA & 0xffffffu) != 0u) ? inst->reflectionRGBA : 0x7f7f7fu;
		const u32 white = 0xffu >> ((u32)inst->specLightX & 31u);

		below[0] = (float)(white & (mask & 0xffu)) / 255.0f;
		below[1] = (float)(white & ((mask >> 8) & 0xffu)) / 255.0f;
		below[2] = (float)(white & ((mask >> 16) & 0xffu)) / 255.0f;
	}

	for (c = 0; c < 3; c++)
	{
		draw->tint[c] = (1.0f - a) + (far3[c] * a);
		draw->belowTint[c] = draw->tint[c] * below[c];
		draw->mirrorTint[c] = draw->tint[c];
	}
	draw->tint[3] = 1.0f;
	draw->belowTint[3] = 1.0f;
	draw->mirrorTint[3] = 1.0f;
}

int Engine_DriverCmdlRoute(struct Instance *inst, struct InstDrawPerPlayer *idpp, struct PushBuffer *pb)
{
	struct GameTracker *gGT = sdata->gGT;
	const struct Driver *d;
	const EngCmdlModel *em;
	const struct ModelHeader *mh0;
	EngineInstanceDraw draw;
	int seat;
	int anim = -1;
	int frame = -1;
	int meshFrame;

	if (!g_cfg_engine || !s_engCmdlInit || (inst == NULL) || (idpp == NULL) || (pb == NULL) || (gGT == NULL) || (inst->model == NULL))
	{
		return 0;
	}
	// The hi-res driver's body (engine_hires.c, --engine-hires-dir) first.
	if (Engine_HiresRoute(inst, idpp, pb))
	{
		// The engine oracle (armed frames only): these E lines are the hi-res driver's (hires=1).
		Engine_OracleNoteHires();
		return 1;
	}
	seat = EngCmdl_SeatOfBody(gGT, inst);
	if (seat < 0)
	{
		return 0;
	}
	d = gGT->drivers[seat];
	if ((inst->thread == NULL) || (inst->thread->object != (void *)d) || !Engine_FxOwnsDriver(d) || !Engine_DriverCmdlOwns(d))
	{
		return 0;
	}
	em = &s_engCmdlModels[s_engCmdlSeat[seat]];
	mh0 = (inst->model->numHeaders >= 1) ? inst->model->headers : NULL;
	if ((mh0 == NULL) || ((inst->flags & (SCREENSPACE_INSTANCE | HIDE_MODEL)) != 0) || ((idpp->instFlags & REVERSE_CULL_DIRECTION) != 0) ||
	    (idpp->mh == NULL) || (idpp->ptrCurrFrame == NULL) || (Engine_InstanceViewOf(pb) < 0) || !EngCmdl_MeshReady(em->imp.mesh))
	{
		return 0;
	}

	// The pose: the frame retail draws with (see the header comment).
	NativeRenderLayer_FrameOf(idpp->mh, idpp->ptrCurrFrame, &anim, &frame);
	if (frame < 0)
	{
		const struct ModelAnim *ma = NativeRenderLayer_AnimOf(mh0, (u32)inst->animIndex);

		if (ma == NULL)
		{
			return 0;
		}
		anim = (int)inst->animIndex;
		frame = NativeRenderLayer_AnimFrameNumber(ma, (u16)inst->animFrame);
	}
	else if (anim < 0)
	{
		anim = 0; // a header without animations: its one frame
	}
	meshFrame = Engine_ModelFrame(&em->imp, anim, frame);
	if (meshFrame < 0)
	{
		return 0;
	}

	memset(&draw, 0, sizeof(draw));
	draw.mesh = em->imp.mesh;
	draw.frame = (u32)meshFrame;
	EngCmdl_Tints(inst, idpp, &draw);
	draw.drawBelow = 1;
	draw.flags = em->transparent ? ENGINE_DRAW_TRANSPARENT : 0;

	if (Engine_InstanceSubmit(inst, idpp, pb, &draw) > 0)
	{
		Engine_FxNoteDrawn(ENGINE_FX_BODY);
		return 1;
	}
	return 0;
}

// Called for every instance retail draws: only a few compares unless the
// instance is a driver body.
void Engine_DriverNoteRetailBody(const struct Instance *inst)
{
	const struct GameTracker *gGT = sdata->gGT;
	int seat;

	if (!g_cfg_engine || (inst == NULL) || (gGT == NULL))
	{
		return;
	}
	seat = EngCmdl_SeatOfBody(gGT, inst);
	if ((seat >= 0) && Engine_FxOwnsDriver(gGT->drivers[seat]))
	{
		Engine_FxNoteRetail(ENGINE_FX_BODY);
	}
}
