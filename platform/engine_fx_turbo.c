#include <platform/engine.h>

// ENGINE FX TURBO - the turbo flames of custom drivers drawn by the engine (step 4).
// See include/platform/engine.h, EFFECTS OF CUSTOM DRIVERS.
//
// DRAW TIME ONLY. The game creates, moves and animates the two flame instances
// of a driver exactly as before (game/Vehicle/VehFire.c, VehTurbo.c: model
// STATIC_TURBO_EFFECT + 0..7, HIDE_MODEL from the custom look). Only their DRAW
// is taken over here, from RenderBucket_DispatchDrawFunc, for the flames of a
// driver the engine owns:
//   - which instance is a flame: its thread ticks with VehTurbo_ThTick; the
//     thread object (struct Turbo) names the driver and both flame instances
//     (thread->inst and turbo->inst; the second is born with the first one's
//     thread as parent, so both instances share that thread);
//   - the model: the retail flame model, imported once per level and Model
//     pointer by Engine_ModelImport; its animation frame is inst->animFrame of
//     animation inst->animIndex;
//   - the matrix: NativeRenderLayer_FillItem (idpp->mvp, the shift +2 near /
//     -2 DRAW_HUGE, screen H and rect) and NativeRenderLayer_CharItemMatrix
//     with unitScale = 16384 / header-0 scale, exactly as a char item;
//   - the colour: the retail DPCT fades the vertex colours toward the far
//     colour by a = idpp->alphaScale / 4096 (RenderBucket_LoadPrimColors); the
//     far colour is (colorRGBA >> 20, >> 12, >> 4) & 0xff for the colour
//     setup callbacks and 0 otherwise (a flame has colorRGBA 0: black). The
//     tint is therefore (1 - a) + far * a per channel, alpha 1; the blend is
//     the one of each imported material (semi-transparent faces of the model);
//   - the cell: the middle bin of the instance's otRangeNormal, the same cell
//     the layer links a char item into (NativeRenderLayer_MarkerBin).
// The water line (SPLIT_LINE / REFLECTIVE handlers) is not drawn here: such a
// view returns 0 and the retail handler draws the flame (counted as retail).

#define ENG_TURBO_MODELS 16

typedef struct
{
	const struct Model *model;
	EngineModelImport imp;
	int transparent;
	int failed;
} EngTurboModel;

internal EngTurboModel s_engTurboModels[ENG_TURBO_MODELS];
internal struct NrDrawItem s_engTurboItem;

// The cached import of a model: only a lookup. The meshes are made at the load
// point (EngineFx_TurboLoad); an upload inside a race frame is not allowed, so
// a model without a live mesh WITH GPU buffers is not drawn (retail draws it).
internal EngTurboModel *EngineFx_TurboModel(const struct Model *model)
{
	int i;

	for (i = 0; i < ENG_TURBO_MODELS; i++)
	{
		EngTurboModel *e = &s_engTurboModels[i];
		u32 vb = 0xffffffffu;
		u32 ib = 0xffffffffu;

		if ((e->model != model) || e->failed || (e->imp.mesh < 0) || (Engine_MeshDesc(e->imp.mesh) == NULL))
		{
			continue;
		}
		Engine_MeshGetGpu(e->imp.mesh, &vb, &ib);
		if ((vb == 0xffffffffu) || (ib == 0xffffffffu))
		{
			return NULL;
		}
		return e;
	}
	return NULL;
}

// At the load point, after Engine_DriverLoad and before Engine_RenderUploadMeshes:
// imports the eight flame models the game births and animates for the drivers
// (gGT->modelPtr[STATIC_TURBO_EFFECT + 0..7], VehFire.c / VehTurbo.c) so that
// the renderer uploads them with the other meshes. Reset by every call (the
// meshes of the last level are gone).
void EngineFx_TurboLoad(void)
{
	const struct GameTracker *gGT = sdata->gGT;
	int n;
	int i;
	int imported = 0;

	memset(s_engTurboModels, 0, sizeof(s_engTurboModels));
	if (!g_cfg_engine || (gGT == NULL))
	{
		return;
	}
	for (n = 0; n < ENG_TURBO_MODELS; n++)
	{
		const struct Model *model;
		EngTurboModel *slot = &s_engTurboModels[n];

		if (n >= 8)
		{
			break;
		}
		model = gGT->modelPtr[STATIC_TURBO_EFFECT + n];
		if (model == NULL)
		{
			continue;
		}
		slot->model = model;
		if (!Engine_ModelImport(model, "turbo flame", &slot->imp) || (slot->imp.mesh < 0))
		{
			slot->failed = 1;
			continue;
		}
		imported++;
		for (i = 0; i < slot->imp.materialCount; i++)
		{
			const EngineMaterial *m = Engine_Material(slot->imp.firstMaterial + i);

			if ((m != NULL) && (m->blend != ENGINE_BLEND_OPAQUE) && (m->blend != ENGINE_BLEND_MASK))
			{
				slot->transparent = 1;
			}
		}
	}
	Platform_Log("[CTR Engine] turbo flames: %d model(s) imported\n", imported);
}

// 1 = the engine drew this flame instance in this view (the retail handler is
// skipped); 0 = retail draws it as always.
int EngineFx_TurboRoute(struct Instance *inst, struct InstDrawPerPlayer *idpp, struct PushBuffer *pb)
{
	struct GameTracker *gGT = sdata->gGT;
	const struct Thread *thread;
	const struct Turbo *turbo;
	const struct ModelHeader *mh0;
	EngTurboModel *em;
	EngineDraw draw;
	double S[4][4];
	int view = -1;
	int v;
	int anim;
	int frame;
	float a;
	float farColor[3];

	if (!g_cfg_engine || (inst == NULL) || (idpp == NULL) || (pb == NULL) || (gGT == NULL) || (inst->model == NULL))
	{
		return 0;
	}
	thread = inst->thread;
	if ((thread == NULL) || (thread->funcThTick != VehTurbo_ThTick) || (thread->object == NULL))
	{
		return 0;
	}
	turbo = (const struct Turbo *)thread->object;
	if (((inst != thread->inst) && (inst != turbo->inst)) || !Engine_FxOwnsDriver(turbo->driver))
	{
		return 0;
	}
	// Retail never gets here for a hidden instance (the custom look sets
	// HIDE_MODEL); kept as a guard so a hidden flame is never drawn.
	if ((inst->flags & HIDE_MODEL) != 0)
	{
		return 0;
	}

	// From here on the flame belongs to an owned driver: whatever is declined
	// is drawn by the retail path and counted as such.
	for (v = 0; v < NATIVE_RENDER_LAYER_VIEWS; v++)
	{
		if (pb == &gGT->pushBuffer[v])
		{
			view = v;
			break;
		}
	}
	mh0 = (inst->model->numHeaders >= 1) ? inst->model->headers : NULL;
	if ((view < 0) || ((inst->flags & SCREENSPACE_INSTANCE) != 0) || ((u32)idpp->unkEC != (u32)RB_RETAIL_DRAWFUNC_NORMAL) ||
	    (idpp->otRangeNormal == 0) || (idpp->ptrCurrFrame == NULL) || (mh0 == NULL) || (mh0->scale.x == 0) || (mh0->scale.y == 0) ||
	    ((u16)mh0->scale.z == 0))
	{
		Engine_FxNoteRetail(ENGINE_FX_TURBO);
		return 0;
	}
	em = EngineFx_TurboModel(inst->model);
	if (em == NULL)
	{
		Engine_FxNoteRetail(ENGINE_FX_TURBO);
		return 0;
	}

	// The matrix as a char item has it (idpp->mvp, shift, H, rect), then the
	// unit scale of header 0, the units of the imported positions.
	(void)NativeRenderLayer_FillItem(&s_engTurboItem, gGT, inst, idpp, pb, view);
	s_engTurboItem.unitScale[0] = 16384.0 / (double)mh0->scale.x;
	s_engTurboItem.unitScale[1] = 16384.0 / (double)mh0->scale.y;
	s_engTurboItem.unitScale[2] = 16384.0 / (double)(u16)mh0->scale.z;
	NativeRenderLayer_CharItemMatrix(&s_engTurboItem, 0.0, 0.0, S);

	anim = (int)inst->animIndex;
	frame = (int)inst->animFrame;
	{
		const struct ModelAnim *ma = NativeRenderLayer_AnimOf(mh0, (u32)anim);

		if (ma != NULL)
		{
			frame = NativeRenderLayer_AnimFrameNumber(ma, (u16)inst->animFrame);
		}
		else
		{
			anim = 0;
			frame = 0;
		}
	}

	// DPCT toward the far colour by a = alphaScale / 4096 (held to 0..1).
	a = ((int)idpp->alphaScale <= 0) ? 0.0f : (((int)idpp->alphaScale >= 0x1000) ? 1.0f : ((float)idpp->alphaScale / 4096.0f));
	{
		const u32 setup = (u32)(size_t)inst->funcPtr[0];

		if ((setup == RB_RETAIL_INST_SETUP_LIGHT_COLOR) || (setup == RB_RETAIL_INST_SETUP_COLOR) || (setup == RB_RETAIL_INST_SETUP_FADE_COLOR))
		{
			farColor[0] = (float)((inst->colorRGBA >> 20) & 0xffu) / 255.0f;
			farColor[1] = (float)((inst->colorRGBA >> 12) & 0xffu) / 255.0f;
			farColor[2] = (float)((inst->colorRGBA >> 4) & 0xffu) / 255.0f;
		}
		else
		{
			farColor[0] = farColor[1] = farColor[2] = 0.0f;
		}
	}

	memset(&draw, 0, sizeof(draw));
	draw.materialOverride = -1;
	draw.mesh = em->imp.mesh;
	{
		const int meshFrame = Engine_ModelFrame(&em->imp, anim, frame);

		if (meshFrame < 0)
		{
			Engine_FxNoteRetail(ENGINE_FX_TURBO);
			return 0;
		}
		draw.frame = (u32)meshFrame;
	}
	Engine_DriverMatrix(S, &draw.clipFromModel);
	draw.sortDepth = (float)S[3][3];
	draw.tint[0] = (1.0f - a) + (farColor[0] * a);
	draw.tint[1] = (1.0f - a) + (farColor[1] * a);
	draw.tint[2] = (1.0f - a) + (farColor[2] * a);
	draw.tint[3] = 1.0f;
	draw.flags = em->transparent ? ENGINE_DRAW_TRANSPARENT : 0;

	if (!Engine_RenderSubmitLinked(view, &draw, (u32 *)idpp->otRangeNormal, NativeRenderLayer_MarkerBin(idpp, 0)))
	{
		Engine_FxNoteRetail(ENGINE_FX_TURBO);
		return 0;
	}
	Engine_FxNoteDrawn(ENGINE_FX_TURBO);
	return 1;
}
