#include <platform/engine.h>
#include <math.h>

// ENGINE INSTANCE - an instance of the game drawn by the engine in world space
// (round of 2026-10-13). See include/platform/engine.h, INSTANCES IN WORLD SPACE.
//
// THE PRINCIPLE: the engine shows the game state its own way. It does not
// rebuild what the PS1 queue does with it (idpp->mvp, the +2 near / -2
// DRAW_HUGE shift, GTE rounding, the per-view projected split line). It reads
// what the game keeps in WORLD units and does the rest in float:
//
//   worldFromModel  = the instance's own state:
//       world = inst->matrix.m / 4096 * (inst->scale / 4096) * model + inst->matrix.t
//     - inst->matrix: rotation 4.12 (rows as the GTE stores them) and the
//       translation in world units (s32), the very values the queue reads
//       (RenderBucket_QueueExecute.c, RenderBucket_GetViewPosition ~1299:
//       pos = inst->matrix.t - pb->pos, and BuildM3x3 ~1614-1684: the rotation
//       of inst->matrix times diag(mh->scale / 4 * inst->scale / 4096)).
//     - inst->scale: 4096 = 1, per axis (squash and stretch of a driver).
//     - the unit scale of model header 0 is NOT in this matrix: it is already
//       folded into the imported mesh. Engine_ModelImport writes positions as
//       g * mh->scale / 16384 (g = the GTE input value of the retail record),
//       and the queue's M = R * inst->scale * mh->scale / 16384 applied to g is
//       therefore exactly R * inst->scale applied to the model units. This is
//       the same statement as NativeRenderLayer_CharItemMatrix, which
//       multiplies M by unitScale = 16384 / mh->scale again.
//   clipFromModel   = Engine_CameraScreenFromWorld(camera of the view) * worldFromModel.
//
// THE WATER PLANE. The game keeps it in WORLD units per instance:
// inst->vertSplit (s16, struct Instance 0x56) is the world height of the
// plane. RenderBucket_BuildSplitState (QueueExecute.c:1862-1863) takes
// rawSplit = vertSplit - (matrix.t[1] + frame offset), i.e. vertSplit against
// the world y of the instance. Who writes it: game/COLL.c ~1504-1517 (water and
// mud quadblocks: SPLIT_LINE with vertSplit 0; reflective quadblocks:
// REFLECTIVE with level->splitLines[0/1]), game/231/RB_Instance.c:28/35,
// game/BOTS.c:1814-1821, game/Vehicle/VehTurbo.c:146-167 (flames copy it from
// the driver). The per-view idpp->splitLine of the PS1 handler is NOT used: it
// is that value moved into the camera-relative 4x space of one view.
//
// THE Y AXIS. World y is UP in the game: the driver's air gap is
// posCurr.y - quadBlockHeight (VehFrame.c:111), the ground lies at the lower y
// (VehStuckProc.c:1825 sets vertSplit from quadBlockHeight). It is the VIEW y
// that points down (the camera matrix flips it); the camera of the engine
// handles that, nothing here has to. So "above the water" = world y > L and
// "below" = world y <= L, the retail rule splitDist = L - y >= 0 (QueueExecute
// .c:2998, native_render_layer.c:4081 "splitDist >= 0 is below the plane").
// In model units the plane is d(p) = (L - t.y) - (row 1 of worldFromModel . p)
// (EngineDraw.split: split.xyz = that row, split[3] = L - t.y): d >= 0 below
// (splitKeep 1), d < 0 above (splitKeep -1).
//
// THE MIRROR. Reflect = mirror about the world plane y = L: y' = 2 L - y
// (translate, scale y by -1, translate back); clip = screenFromWorld * Reflect
// * worldFromModel. The determinant is negative, which the renderer's cull
// (Engine_RenderCullOfMatrix, from the matrix determinant) turns into the
// opposite winding by itself. The mirror draw keeps only the reflection of the
// part ABOVE the plane (the same plane in model units, splitKeep -1): the part
// below the plane is not reflected.
//
// THE HANDLERS (idpp->unkEC, chosen by RenderBucket_SelectRetailHandlers
// ~954-995) only say what the view wants:
//   NORMAL, NORMAL_ALT  one draw. (NORMAL_ALT is a normal draw whose idpp
//                       matrices the queue packs differently; the instance is
//                       where its state says.)
//   SPLIT               SPLIT_LINE, the plane crosses the instance or lies
//                       above it: the part above (tint) and, with drawBelow,
//                       the part below (belowTint), one draw each.
//   REFLECTION          REFLECTIVE, the plane crosses the instance: the part
//                       above the plane, plus its mirror image (mirrorTint).
//                       Like the layer (NativeRenderLayer_SetUpMirror) the
//                       original keeps only the part above the plane.
//   SPECIAL             flagged SPLIT_LINE or REFLECTIVE but the queue made no
//                       split state: the plane lies fully below the instance
//                       (REFLECTIVE: scratch68 < 0, QueueExecute.c ~971-981),
//                       or the instance is a SPLIT_LINE one far above it
//                       (splitLine + 362 < 0, ~1885), or the scale is zero.
//                       The retail handler cuts nothing and draws the model
//                       plus its mirror about the plane (DrawSpecialMirroredPass
//                       ~4792). Here: the whole model, and for a REFLECTIVE
//                       instance its mirror image (a non-reflective one far
//                       above the water has its mirror below the ground, where
//                       nothing can see it: one draw). SPLIT_SPECIAL (a door
//                       or garage with its own matrix path, BuildSplitState
//                       ~1771-1790) is not understood: declined, retail draws.
// Declined as well (retail draws): no camera, a screen-space instance, an
// instance with PIXEL_LOD (its scale depends on the view depth), a
// CUSTOM_MATRIX instance or a model that always points north (the queue builds
// a camera-facing matrix, QueueExecute.c ~1717-1760), a zero scale, a mesh
// without CPU description.
//
// CELLS (phase-1 transition channel): the first draw is linked into
// idpp->otRangeNormal at NativeRenderLayer_MarkerBin(idpp, 0), the below or
// mirror draw into idpp->otRangeSecondary at the same bin (into otRangeNormal
// when there is no secondary range).
//
// MEASUREMENT. The first ENG_INST_CROSS_MAX draws of the NORMAL handler are
// also worked out through the layer's matrix (NativeRenderLayer_FillItem +
// unit scale + CharItemMatrix, as engine_fx_turbo.c used to) and the eight
// corners of the mesh's bounding box compared on the screen. It changes
// nothing that is drawn; it only checks the derivation above.
//
// LIVE VERTICES (round of 2026-10-15). With d->liveVertices every draw above
// is made of the mesh's ranges as dynamic geometry from those vertices (the
// frame the caller rebuilt this frame, Engine_ModelLiveFrame), one draw per
// range with the range's material, all linked into the same cell. A model
// mesh lists every vertex once in order (indices 0..n-1, engine_model.c), so
// a range is the run of vertices it names.

#define ENG_INST_CROSS_MAX 120
#define ENG_INST_LIVE_MAX 4096 // vertices of a live frame

typedef enum
{
	ENG_INST_NORMAL = 0,
	ENG_INST_SPLIT,
	ENG_INST_REFLECTION,
	ENG_INST_SPECIAL,
	ENG_INST_KINDS
} EngInstKind;

global_variable u32 s_engInstDrawn[ENG_INST_KINDS];
global_variable u32 s_engInstDeclined;
global_variable int s_engInstReportOn = 0;

global_variable struct NrDrawItem s_engInstItem;
global_variable int s_engInstCrossCount = 0;
global_variable int s_engInstCrossLogged = 0;
global_variable float s_engInstCrossMax = 0.0f;

// 0..ENG_INST_LIVE_MAX-1: the indices of a range of a live frame.
global_variable u16 s_engInstLiveIndex[ENG_INST_LIVE_MAX];
global_variable int s_engInstLiveIndexReady = 0;

// --------------------------------------------------------------------------
// The maths on plain numbers (also the self test's subject).

// worldFromModel from the rotation (4.12, [row][col]), translation and the
// per-axis instance scale (4096 = 1): world = R/4096 * diag(scale/4096) * p + t.
internal void EngInst_BuildWorld(const s16 r[3][3], const s32 t[3], const s16 scale[3], EngineMat4 *out)
{
	int col;
	int row;

	Engine_Mat4FromGte(r, t, out);
	for (col = 0; col < 3; col++)
	{
		const float k = (float)scale[col] / 4096.0f;

		for (row = 0; row < 3; row++)
		{
			out->m[col * 4 + row] *= k;
		}
	}
}

// The plane y = L (world) in model units: d(p) = split[3] - dot(split.xyz, p),
// d >= 0 where the world y is <= L (below), d < 0 above.
internal void EngInst_Plane(const EngineMat4 *worldFromModel, float planeY, float split[4])
{
	split[0] = worldFromModel->m[1];
	split[1] = worldFromModel->m[5];
	split[2] = worldFromModel->m[9];
	split[3] = planeY - worldFromModel->m[13];
}

// Mirror about the world plane y = L.
internal void EngInst_Reflect(float planeY, EngineMat4 *out)
{
	Engine_Mat4Identity(out);
	out->m[5] = -1.0f;
	out->m[13] = 2.0f * planeY;
}

internal float EngInst_Det3(const EngineMat4 *a)
{
	const float *m = a->m;

	return m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2]) + m[8] * (m[1] * m[6] - m[5] * m[2]);
}

// --------------------------------------------------------------------------
// The instance's state.

// The model header 0 of an instance, NULL when it has none or a zero scale
// (the importer's unit scale divides by it).
internal const struct ModelHeader *EngInst_Header0(const struct Instance *inst)
{
	const struct ModelHeader *mh0;

	if ((inst == NULL) || (inst->model == NULL) || (inst->model->numHeaders < 1))
	{
		return NULL;
	}
	mh0 = inst->model->headers;
	if ((mh0 == NULL) || (mh0->scale.x == 0) || (mh0->scale.y == 0) || ((u16)mh0->scale.z == 0))
	{
		return NULL;
	}
	return mh0;
}

int Engine_InstanceWorldMatrix(const struct Instance *inst, EngineMat4 *worldFromModel)
{
	const struct ModelHeader *mh0 = EngInst_Header0(inst);
	s16 scale[3];
	s32 t[3];
	int i;

	if ((mh0 == NULL) || (worldFromModel == NULL))
	{
		return 0;
	}
	// A matrix the queue builds from the camera instead of inst->matrix.
	if (((inst->flags & CUSTOM_MATRIX) != 0) || ((mh0->flags & RB_MODEL_ALWAYS_POINT_NORTH) != 0))
	{
		return 0;
	}
	scale[0] = inst->scale.x;
	scale[1] = inst->scale.y;
	scale[2] = inst->scale.z;
	// A squashed instance (scale 0) is a flat model: retail draws it.
	if ((scale[0] == 0) || (scale[1] == 0) || (scale[2] == 0))
	{
		return 0;
	}
	for (i = 0; i < 3; i++)
	{
		t[i] = inst->matrix.t[i];
	}
	EngInst_BuildWorld(inst->matrix.m, t, scale, worldFromModel);
	return 1;
}

int Engine_InstanceViewOf(const struct PushBuffer *pb)
{
	const struct GameTracker *gGT = sdata->gGT;
	int v;

	if ((gGT == NULL) || (pb == NULL))
	{
		return -1;
	}
	for (v = 0; v < ENGINE_VIEWS; v++)
	{
		if (pb == &gGT->pushBuffer[v])
		{
			return v;
		}
	}
	return -1;
}

// --------------------------------------------------------------------------
// Counters and the layer cross-check.

internal void EngInst_CrossReport(void)
{
	if (!s_engInstCrossLogged && (s_engInstCrossCount > 0))
	{
		s_engInstCrossLogged = 1;
		Platform_Log("[CTR Engine] instance world path vs layer path: max %.2f px over %d draws\n", (double)s_engInstCrossMax, s_engInstCrossCount);
	}
}

internal void EngInst_Report(void)
{
	EngInst_CrossReport();
	Platform_Log("[CTR Engine] instances in world space: normal %u, split %u, reflection %u, special %u; declined %u\n", s_engInstDrawn[ENG_INST_NORMAL],
	             s_engInstDrawn[ENG_INST_SPLIT], s_engInstDrawn[ENG_INST_REFLECTION], s_engInstDrawn[ENG_INST_SPECIAL], s_engInstDeclined);
}

internal void EngInst_NoteReportOn(void)
{
	if (!s_engInstReportOn)
	{
		s_engInstReportOn = 1;
		Platform_AtExitReport(EngInst_Report);
	}
}

internal void EngInst_NoteDrawn(EngInstKind kind)
{
	s_engInstDrawn[kind]++;
	EngInst_NoteReportOn();
}

internal int EngInst_Decline(void)
{
	s_engInstDeclined++;
	EngInst_NoteReportOn();
	return 0;
}

// A point through a column-major clip matrix to the screen; 0 behind the camera.
internal int EngInst_ToScreen(const float *m, float x, float y, float z, float *sx, float *sy)
{
	const float w = m[3] * x + m[7] * y + m[11] * z + m[15];

	if (w <= 1.0f)
	{
		return 0;
	}
	*sx = (m[0] * x + m[4] * y + m[8] * z + m[12]) / w;
	*sy = (m[1] * x + m[5] * y + m[9] * z + m[13]) / w;
	return 1;
}

// The layer's matrix for the same instance (the char item path), the corners
// of the frame's bounding box through both, the largest screen distance.
internal void EngInst_CrossCheck(const struct Instance *inst, const struct InstDrawPerPlayer *idpp, const struct PushBuffer *pb, int view,
                                 const struct ModelHeader *mh0, const EngineInstanceDraw *d, const EngineMat4 *engineClip)
{
	const struct GameTracker *gGT = sdata->gGT;
	const EngineMeshDesc *md;
	const EngineVertex *vtx;
	EngineMat4 layerClip;
	double S[4][4];
	float lo[3];
	float hi[3];
	float worst = 0.0f;
	int valid = 0;
	u32 i;
	int k;
	int c;

	if ((s_engInstCrossCount >= ENG_INST_CROSS_MAX) || (gGT == NULL) || (idpp->ptrCurrFrame == NULL))
	{
		return;
	}
	md = Engine_MeshDesc(d->mesh);
	if ((md == NULL) || (md->vertices == NULL) || (d->frame >= md->frames) || (md->verticesPerFrame == 0))
	{
		return;
	}
	vtx = md->vertices + ((size_t)d->frame * md->verticesPerFrame);
	for (c = 0; c < 3; c++)
	{
		lo[c] = vtx[0].position[c];
		hi[c] = vtx[0].position[c];
	}
	for (i = 1; i < md->verticesPerFrame; i++)
	{
		for (c = 0; c < 3; c++)
		{
			if (vtx[i].position[c] < lo[c])
			{
				lo[c] = vtx[i].position[c];
			}
			if (vtx[i].position[c] > hi[c])
			{
				hi[c] = vtx[i].position[c];
			}
		}
	}

	(void)NativeRenderLayer_FillItem(&s_engInstItem, gGT, inst, idpp, pb, view);
	s_engInstItem.unitScale[0] = 16384.0 / (double)mh0->scale.x;
	s_engInstItem.unitScale[1] = 16384.0 / (double)mh0->scale.y;
	s_engInstItem.unitScale[2] = 16384.0 / (double)(u16)mh0->scale.z;
	NativeRenderLayer_CharItemMatrix(&s_engInstItem, 0.0, 0.0, S);
	NativeRenderLayer_MatrixToDraw(S, layerClip.m);

	for (k = 0; k < 8; k++)
	{
		const float x = (k & 1) ? hi[0] : lo[0];
		const float y = (k & 2) ? hi[1] : lo[1];
		const float z = (k & 4) ? hi[2] : lo[2];
		float ax;
		float ay;
		float bx;
		float by;

		if (EngInst_ToScreen(engineClip->m, x, y, z, &ax, &ay) && EngInst_ToScreen(layerClip.m, x, y, z, &bx, &by))
		{
			const float dist = sqrtf(((ax - bx) * (ax - bx)) + ((ay - by) * (ay - by)));

			if (dist > worst)
			{
				worst = dist;
			}
			valid++;
		}
	}
	if (valid == 0)
	{
		return;
	}
	if (worst > s_engInstCrossMax)
	{
		s_engInstCrossMax = worst;
	}
	s_engInstCrossCount++;
	EngInst_NoteReportOn();
	if (s_engInstCrossCount >= ENG_INST_CROSS_MAX)
	{
		EngInst_CrossReport();
	}
}

// The screen box (x0, y0, x1, y1) of the 8 corners through a clip matrix, as
// fractions of the view rectangle. 0 when a corner is behind the camera.
internal int EngInst_BoxOnScreen(const float *m, const float lo[3], const float hi[3], float w, float h, float box[4])
{
	int k;

	for (k = 0; k < 8; k++)
	{
		float sx;
		float sy;

		if (!EngInst_ToScreen(m, (k & 1) ? hi[0] : lo[0], (k & 2) ? hi[1] : lo[1], (k & 4) ? hi[2] : lo[2], &sx, &sy))
		{
			return 0;
		}
		sx /= w;
		sy /= h;
		box[0] = ((k == 0) || (sx < box[0])) ? sx : box[0];
		box[1] = ((k == 0) || (sy < box[1])) ? sy : box[1];
		box[2] = ((k == 0) || (sx > box[2])) ? sx : box[2];
		box[3] = ((k == 0) || (sy > box[3])) ? sy : box[3];
	}
	return 1;
}

int Engine_InstanceScreenBoxes(const struct Instance *inst, const struct InstDrawPerPlayer *idpp, const struct PushBuffer *pb, int mesh, u32 frame,
                               float engineBox[4], float retailBox[4])
{
	const struct GameTracker *gGT = sdata->gGT;
	const struct ModelHeader *mh0 = EngInst_Header0(inst);
	const EngineMeshDesc *md = Engine_MeshDesc(mesh);
	const EngineCamera *cam;
	const EngineVertex *vtx;
	EngineMat4 worldFromModel;
	EngineMat4 screenFromWorld;
	EngineMat4 clip;
	EngineMat4 layerClip;
	double S[4][4];
	float lo[3];
	float hi[3];
	int view;
	u32 i;
	int c;

	if ((gGT == NULL) || (idpp == NULL) || (pb == NULL) || (mh0 == NULL) || (md == NULL) || (md->vertices == NULL) || (frame >= md->frames) ||
	    (md->verticesPerFrame == 0) || (idpp->ptrCurrFrame == NULL) || (engineBox == NULL) || (retailBox == NULL))
	{
		return 0;
	}
	view = Engine_InstanceViewOf(pb);
	cam = (view >= 0) ? Engine_FrameCamera(view) : NULL;
	if ((cam == NULL) || (cam->rectW <= 0) || (cam->rectH <= 0) || !Engine_InstanceWorldMatrix(inst, &worldFromModel))
	{
		return 0;
	}
	vtx = md->vertices + ((size_t)frame * md->verticesPerFrame);
	for (c = 0; c < 3; c++)
	{
		lo[c] = vtx[0].position[c];
		hi[c] = vtx[0].position[c];
	}
	for (i = 1; i < md->verticesPerFrame; i++)
	{
		for (c = 0; c < 3; c++)
		{
			lo[c] = (vtx[i].position[c] < lo[c]) ? vtx[i].position[c] : lo[c];
			hi[c] = (vtx[i].position[c] > hi[c]) ? vtx[i].position[c] : hi[c];
		}
	}
	Engine_CameraScreenFromWorld(cam, &screenFromWorld);
	Engine_Mat4Mul(&screenFromWorld, &worldFromModel, &clip);

	// The layer's matrix, as EngInst_CrossCheck builds it (s_engInstItem is scratch).
	(void)NativeRenderLayer_FillItem(&s_engInstItem, gGT, inst, idpp, pb, view);
	s_engInstItem.unitScale[0] = 16384.0 / (double)mh0->scale.x;
	s_engInstItem.unitScale[1] = 16384.0 / (double)mh0->scale.y;
	s_engInstItem.unitScale[2] = 16384.0 / (double)(u16)mh0->scale.z;
	NativeRenderLayer_CharItemMatrix(&s_engInstItem, 0.0, 0.0, S);
	NativeRenderLayer_MatrixToDraw(S, layerClip.m);

	return EngInst_BoxOnScreen(clip.m, lo, hi, (float)cam->rectW, (float)cam->rectH, engineBox) &&
	       EngInst_BoxOnScreen(layerClip.m, lo, hi, (float)cam->rectW, (float)cam->rectH, retailBox);
}

// --------------------------------------------------------------------------
// The submit.

// One draw of the instance into cell bin of ot: the mesh frame, or with live
// vertices every range of the mesh as dynamic geometry (LIVE VERTICES above).
// 1 = submitted (with live vertices: at least one range).
internal int EngInst_SubmitLinked(int view, const EngineDraw *draw, const EngineInstanceDraw *d, u32 *ot, int bin)
{
	const EngineMeshDesc *desc;
	int submitted = 0;
	u32 r;
	u32 i;

	if (d->liveVertices == NULL)
	{
		return Engine_RenderSubmitLinked(view, draw, ot, bin);
	}
	desc = Engine_MeshDesc(d->mesh);
	if ((desc == NULL) || (desc->ranges == NULL) || (desc->indexCount != desc->verticesPerFrame) || (desc->verticesPerFrame > ENG_INST_LIVE_MAX))
	{
		return 0;
	}
	if (!s_engInstLiveIndexReady)
	{
		for (i = 0; i < ENG_INST_LIVE_MAX; i++)
		{
			s_engInstLiveIndex[i] = (u16)i;
		}
		s_engInstLiveIndexReady = 1;
	}
	for (r = 0; r < desc->rangeCount; r++)
	{
		const EngineRange *range = &desc->ranges[r];
		EngineDraw live = *draw;

		if ((range->indexCount == 0u) || (range->firstIndex + range->indexCount > desc->verticesPerFrame))
		{
			continue;
		}
		live.mesh = -1;
		live.frame = 0;
		live.dynVertices = d->liveVertices + range->firstIndex;
		live.dynVertexCount = range->indexCount;
		live.dynIndices = s_engInstLiveIndex;
		live.dynIndexCount = range->indexCount;
		live.dynMaterial = (int)range->material;
		if (Engine_RenderSubmitLinked(view, &live, ot, bin))
		{
			submitted = 1;
		}
	}
	return submitted;
}

int Engine_InstanceSubmit(struct Instance *inst, struct InstDrawPerPlayer *idpp, struct PushBuffer *pb, const EngineInstanceDraw *d)
{
	const EngineCamera *cam;
	const struct ModelHeader *mh0;
	EngineMat4 worldFromModel;
	EngineMat4 screenFromWorld;
	EngineMat4 clip;
	EngineDraw draw;
	EngInstKind kind;
	u32 *ot2;
	float planeY;
	float depth;
	int view;
	int bin;
	int drawn = 0;
	int handler;

	if ((inst == NULL) || (idpp == NULL) || (pb == NULL) || (d == NULL) || !g_cfg_engine)
	{
		return 0;
	}
	view = Engine_InstanceViewOf(pb);
	cam = (view >= 0) ? Engine_FrameCamera(view) : NULL;
	mh0 = EngInst_Header0(inst);
	if ((cam == NULL) || (mh0 == NULL) || (d->mesh < 0) || (Engine_MeshDesc(d->mesh) == NULL) || (idpp->otRangeNormal == 0) ||
	    ((inst->flags & (SCREENSPACE_INSTANCE | PIXEL_LOD)) != 0))
	{
		return EngInst_Decline();
	}
	handler = idpp->unkEC;
	switch ((u32)handler)
	{
	case RB_RETAIL_DRAWFUNC_NORMAL:
	case RB_RETAIL_DRAWFUNC_NORMAL_ALT:
		kind = ENG_INST_NORMAL;
		break;
	case RB_RETAIL_DRAWFUNC_SPLIT:
		kind = ENG_INST_SPLIT;
		break;
	case RB_RETAIL_DRAWFUNC_REFLECTION:
		kind = ENG_INST_REFLECTION;
		break;
	case RB_RETAIL_DRAWFUNC_SPECIAL:
		if (((u32)idpp->instFlags & SPLIT_SPECIAL) != 0)
		{
			return EngInst_Decline();
		}
		kind = ENG_INST_SPECIAL;
		break;
	default:
		return EngInst_Decline();
	}
	if (!Engine_InstanceWorldMatrix(inst, &worldFromModel))
	{
		return EngInst_Decline();
	}

	Engine_CameraScreenFromWorld(cam, &screenFromWorld);
	Engine_Mat4Mul(&screenFromWorld, &worldFromModel, &clip);
	{
		const EngineVec3 origin = {worldFromModel.m[12], worldFromModel.m[13], worldFromModel.m[14]};

		depth = Engine_Mat4MulPoint(&cam->viewFromWorld, origin).z;
	}
	planeY = (float)inst->vertSplit;
	bin = NativeRenderLayer_MarkerBin(idpp, 0);
	ot2 = (u32 *)((idpp->otRangeSecondary != 0) ? idpp->otRangeSecondary : idpp->otRangeNormal);

	memset(&draw, 0, sizeof(draw));
	draw.mesh = d->mesh;
	draw.frame = d->frame;
	draw.materialOverride = -1;
	draw.dynMaterial = -1;
	draw.clipFromModel = clip;
	draw.sortDepth = depth;
	draw.otCell = (u32)bin;
	draw.flags = d->flags;
	memcpy(draw.tint, d->tint, sizeof(draw.tint));

	// The plane in model units for every handler that cuts (the same plane
	// for the original and the mirror).
	if (kind != ENG_INST_NORMAL)
	{
		EngInst_Plane(&worldFromModel, planeY, draw.split);
	}

	// The first draw.
	if ((kind == ENG_INST_SPLIT) || (kind == ENG_INST_REFLECTION))
	{
		draw.splitKeep = -1; // the part above the plane
	}
	if (!EngInst_SubmitLinked(view, &draw, d, (u32 *)idpp->otRangeNormal, bin))
	{
		return EngInst_Decline();
	}
	drawn = 1;

	if ((u32)handler == (u32)RB_RETAIL_DRAWFUNC_NORMAL)
	{
		EngInst_CrossCheck(inst, idpp, pb, view, mh0, d, &clip);
	}

	if (kind == ENG_INST_SPLIT)
	{
		if (d->drawBelow)
		{
			draw.splitKeep = 1; // the part below the plane
			memcpy(draw.tint, d->belowTint, sizeof(draw.tint));
			if (EngInst_SubmitLinked(view, &draw, d, ot2, bin))
			{
				drawn++;
			}
		}
	}
	else if ((kind == ENG_INST_REFLECTION) || ((kind == ENG_INST_SPECIAL) && (((u32)idpp->instFlags & REFLECTIVE) != 0)))
	{
		EngineMat4 reflect;
		EngineMat4 mirrorWorld;

		EngInst_Reflect(planeY, &reflect);
		Engine_Mat4Mul(&reflect, &worldFromModel, &mirrorWorld);
		Engine_Mat4Mul(&screenFromWorld, &mirrorWorld, &draw.clipFromModel);
		draw.splitKeep = -1; // only the reflection of the part above the plane
		memcpy(draw.tint, d->mirrorTint, sizeof(draw.tint));
		if (EngInst_SubmitLinked(view, &draw, d, ot2, bin))
		{
			drawn++;
		}
	}

	EngInst_NoteDrawn(kind);
	return drawn;
}

// --------------------------------------------------------------------------
// Selftest

internal void EngInst_Check(int ok, const char *what, int *checks, int *failures)
{
	(*checks)++;
	if (!ok)
	{
		(*failures)++;
		printf("engine selftest: %s\n", what);
	}
}

internal int EngInst_Near(float a, float b, float tol)
{
	return fabsf(a - b) <= tol;
}

internal int EngInst_PointNear(EngineVec3 a, EngineVec3 b, float tol)
{
	return EngInst_Near(a.x, b.x, tol) && EngInst_Near(a.y, b.y, tol) && EngInst_Near(a.z, b.z, tol);
}

// The plane test of a model point as the shader does it.
internal float EngInst_PlaneDist(const float split[4], EngineVec3 p)
{
	return split[3] - (split[0] * p.x + split[1] * p.y + split[2] * p.z);
}

int Engine_SelfTestInstance(int *checks)
{
	// Rotation of 90 degrees about x (model y -> world z, model z -> world -y):
	// p' = (x, -z, y). Scale (0.5, 1, 2). Translation (100, 200, -300).
	static const s16 rot[3][3] = {{4096, 0, 0}, {0, 0, -4096}, {0, 4096, 0}};
	static const s16 scale[3] = {2048, 4096, 8192};
	static const s32 trans[3] = {100, 200, -300};
	const float planeY = 150.0f;
	EngineMat4 world;
	EngineMat4 reflect;
	EngineMat4 mirror;
	EngineMat4 back;
	EngineVec3 p;
	EngineVec3 w;
	EngineVec3 m;
	float split[4];
	int failures = 0;
	int n = 0;
	int i;
	u32 seed = 12345u;

	EngInst_BuildWorld(rot, trans, scale, &world);

	// The world matrix on known points: model (4, 6, 10) -> scaled (2, 6, 20)
	// -> rotated (2, -20, 6) -> moved (102, 180, -294); the origin is t.
	p = (EngineVec3){4.0f, 6.0f, 10.0f};
	w = Engine_Mat4MulPoint(&world, p);
	EngInst_Check(EngInst_PointNear(w, (EngineVec3){102.0f, 180.0f, -294.0f}, 0.001f), "instance world matrix on a known point", &n, &failures);
	w = Engine_Mat4MulPoint(&world, (EngineVec3){0.0f, 0.0f, 0.0f});
	EngInst_Check(EngInst_PointNear(w, (EngineVec3){100.0f, 200.0f, -300.0f}, 0.001f), "instance world matrix keeps the translation", &n, &failures);
	w = Engine_Mat4MulPoint(&world, (EngineVec3){1.0f, 0.0f, 0.0f});
	EngInst_Check(EngInst_PointNear(w, (EngineVec3){100.5f, 200.0f, -300.0f}, 0.001f), "instance world matrix scales the x axis by scale.x / 4096", &n, &failures);
	w = Engine_Mat4MulPoint(&world, (EngineVec3){0.0f, 0.0f, 1.0f});
	EngInst_Check(EngInst_PointNear(w, (EngineVec3){100.0f, 198.0f, -300.0f}, 0.001f), "instance world matrix: model z -> world -y, scale 2", &n, &failures);

	// The reflection about y = L: (x, L + h, z) <-> (x, L - h, z).
	EngInst_Reflect(planeY, &reflect);
	p = (EngineVec3){37.0f, planeY + 25.0f, -12.0f};
	m = Engine_Mat4MulPoint(&reflect, p);
	EngInst_Check(EngInst_PointNear(m, (EngineVec3){37.0f, planeY - 25.0f, -12.0f}, 0.001f), "reflection maps (x, L+h, z) to (x, L-h, z)", &n, &failures);
	w = Engine_Mat4MulPoint(&reflect, m);
	EngInst_Check(EngInst_PointNear(w, p, 0.001f), "reflection applied twice is the identity", &n, &failures);
	m = Engine_Mat4MulPoint(&reflect, (EngineVec3){5.0f, planeY, 6.0f});
	EngInst_Check(EngInst_PointNear(m, (EngineVec3){5.0f, planeY, 6.0f}, 0.001f), "a point on the plane stays", &n, &failures);

	// The reflection of the instance: reflect * world, then the model points
	// land where the reflected world points are.
	Engine_Mat4Mul(&reflect, &world, &mirror);
	p = (EngineVec3){4.0f, 6.0f, 10.0f};
	w = Engine_Mat4MulPoint(&world, p);
	m = Engine_Mat4MulPoint(&mirror, p);
	EngInst_Check(EngInst_PointNear(m, (EngineVec3){w.x, (2.0f * planeY) - w.y, w.z}, 0.001f), "mirror matrix = reflect * world", &n, &failures);
	Engine_Mat4Mul(&reflect, &mirror, &back);
	w = Engine_Mat4MulPoint(&back, p);
	EngInst_Check(EngInst_PointNear(w, Engine_Mat4MulPoint(&world, p), 0.001f), "mirror of the mirror is the instance", &n, &failures);

	// The determinant: the instance is a rotation with positive scales, the
	// reflection makes it negative (the renderer turns that into the other cull).
	EngInst_Check(EngInst_Det3(&world) > 0.0f, "instance world matrix has a positive determinant", &n, &failures);
	EngInst_Check(EngInst_Det3(&mirror) < 0.0f, "reflection determinant is negative", &n, &failures);

	// The plane in model units classifies model points as the world height does.
	EngInst_Plane(&world, planeY, split);
	for (i = 0; i < 64; i++)
	{
		EngineVec3 q;
		float d;

		seed = (seed * 1664525u) + 1013904223u;
		q.x = (float)((int)((seed >> 8) & 0xff) - 128) * 0.25f;
		seed = (seed * 1664525u) + 1013904223u;
		q.y = (float)((int)((seed >> 8) & 0xff) - 128) * 0.25f;
		seed = (seed * 1664525u) + 1013904223u;
		q.z = (float)((int)((seed >> 8) & 0xff) - 128) * 0.25f;
		w = Engine_Mat4MulPoint(&world, q);
		d = EngInst_PlaneDist(split, q);
		if (EngInst_Near(w.y, planeY, 0.01f))
		{
			continue; // too near the plane for a float verdict
		}
		if (((w.y < planeY) != (d >= 0.0f)) || !EngInst_Near(d, planeY - w.y, 0.01f))
		{
			break;
		}
	}
	EngInst_Check(i == 64, "model-unit plane: d >= 0 below the world plane, d = L - world y", &n, &failures);
	// Known points: the origin has world y 200 (above the plane at 150); model
	// (0, 0, 40) has world y 200 - 2 * 40 = 120 (below).
	EngInst_Check(EngInst_PlaneDist(split, (EngineVec3){0.0f, 0.0f, 0.0f}) < 0.0f, "the origin (world y 200) is above the plane at 150", &n, &failures);
	EngInst_Check(EngInst_PlaneDist(split, (EngineVec3){0.0f, 0.0f, 40.0f}) > 0.0f, "model (0,0,40) (world y 120) is below the plane at 150", &n, &failures);
	// The mirror of a point above the plane lies below it: the origin (y 200)
	// is mirrored to 2 * 150 - 200 = 100.
	m = Engine_Mat4MulPoint(&mirror, (EngineVec3){0.0f, 0.0f, 0.0f});
	EngInst_Check(EngInst_Near(m.y, 100.0f, 0.001f), "mirror of a point above the plane (y 200) lies at 2L - 200 = 100", &n, &failures);

	// The screen box of the POSITION CHECK: sx = x / z, sy = y / z (w = z); the
	// box (0..4, 0..2, 2..4) spans x 0..2 and y 0..1, in a view of 2 x 4: 0..1 x 0..0.25.
	{
		EngineMat4 proj;
		const float lo[3] = {0.0f, 0.0f, 2.0f};
		const float hi[3] = {4.0f, 2.0f, 4.0f};
		const float behind[3] = {0.0f, 0.0f, 0.5f};
		float box[4] = {0.0f, 0.0f, 0.0f, 0.0f};

		memset(&proj, 0, sizeof(proj));
		proj.m[0] = 1.0f;
		proj.m[5] = 1.0f;
		proj.m[11] = 1.0f;
		EngInst_Check(EngInst_BoxOnScreen(proj.m, lo, hi, 2.0f, 4.0f, box) && EngInst_Near(box[0], 0.0f, 1e-6f) && EngInst_Near(box[1], 0.0f, 1e-6f) &&
		                  EngInst_Near(box[2], 1.0f, 1e-6f) && EngInst_Near(box[3], 0.25f, 1e-6f),
		              "position check: screen box of a known box", &n, &failures);
		EngInst_Check(!EngInst_BoxOnScreen(proj.m, behind, hi, 2.0f, 4.0f, box), "position check: a corner behind the camera makes no box", &n, &failures);
	}

	*checks += n;
	return failures;
}
