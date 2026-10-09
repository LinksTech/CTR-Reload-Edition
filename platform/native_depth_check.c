// ===========================================================================
// THE DEPTH ACROSS VIEW Z 0x1000 - A SELF-TEST WITHOUT A GAME RUN.
//
// --native-depth-selftest (ctest native_depth_selftest). The queue scales an
// instance nearer than view z 0x1000 up by 4 - its m3x3 (RenderBucket_BuildM3x3)
// and its mvp.t (RenderBucket_AdjustViewPositionForMvp) - and quarters the
// translation of a DRAW_HUGE one, and keeps no note of either. The render layer
// works the shift out again (NativeRenderLayer_FillItem) and divides the view
// point by 2^shift (NativeRenderLayer_ItemMatrix). If the two did not agree,
// the body would jump in depth by a factor of 4 where an object crosses view z
// 0x1000.
//
// THE INSTANCE. Turned about y and scaled (instance scale 1.25, model scale
// 1.5), seen by a camera that is turned about y as well and stands away from
// the origin - so the view matrix times m3x3 (the light-matrix multiply of the
// queue) and the view z the layer computes again both have real work to do.
// The depth of the instance is hit exactly: its z is searched until the
// queue's own RenderBucket_GetViewPosition gives view z 0x0FFF, and again
// 0x1000. Every step is the queue's, in the order of RenderBucket_QueueDraw:
// GetViewPosition, AdjustViewPositionForMvp, StoreMvpTranslation, BuildM3x3,
// StoreMatrixWords, BuildMvp (handler NORMAL); then the item and the matrix of
// the render layer.
//
// WHAT IS COMPARED, for the pair 0x0FFF / 0x1000, once as a normal instance
// (shift +2 / 0) and once with DRAW_HUGE (shift 0 / -2):
//   step         the true depth w of the middle of the body (u = (0, 0.5, 0)),
//                relative change across the seam; one unit of view z is 0.024
//                percent of it. Below 0.1 percent.
//   rest         the same change without the unit the instance moved: what is
//                left is what the seam itself adds. Below 0.1 percent.
//   matrix part  w of the corner u = (1, 0.5, 1) minus w of u = (-1, 0.5, -1):
//                the translation drops out, so this is the m3x3 part alone,
//                once scaled by 4 and rounded, once not. Below 0.5 percent.
//   wrong shift  each side with the shift of the other side, for the middle and
//                for the matrix part: about 300 and 75 percent - the size of
//                the seam a wrong shift leaves, and the proof that the test can
//                fail.
//
// THE CUSTOM CHARACTER (step 4c). The same seam once more on the way of a
// native custom character: the item filled the same way, its units the model's
// (unitScale = 16384 / mh->scale per axis, NativeRenderLayer_RouteChar) and its
// matrix NativeRenderLayer_CharItemMatrix, for two model scales - 0x1000 and an
// odd one, 0x0ccd. Compared at a point of the model (0, 32, 0) and, for the
// matrix part, the corners (+-20, 32, +-20) model units: step, rest and matrix
// part below 0.1, 0.1 and 0.5 percent, a wrong shift about 300 and 75 percent.
// Appended to the line; the part before it is word for word the old one.
//
// THE WATER LINE (step 4e, stage a), appended after that. Three made-up
// instances with SPLIT_LINE go through the queue's own split steps
// (RenderBucket_BuildSplitState, RenderBucket_SelectRetailHandlers,
// RenderBucket_WriteInstanceCallbackLabels): turned about z and scaled, once
// near (view z 0x0800) and once far (0x2000) - the branch R - and unturned at
// unit scale near - the branch P. The plane lies through the middle of a hull
// of 2400 x 2400 x 3200 input units. For 9 x 9 x 9 points of that hull the
// side of the retail splitDist (the corner through the GTE as
// RenderBucket_TransformSplitDecodedVertex does it, MVMVA sf = 1 with idpp->m3x3
// as the light matrix, and splitLine minus its height as
// RenderBucket_InitWaterSplitVertex) is compared with the side of the native
// plane (NativeRenderLayer_SplitPlane, model units) wherever |d| > 1: 0
// points may differ. Per case also: the handler is SPLIT, the branch the
// expected one, the selector both-mask, and the composed model-view matrix of
// the body equals the one RenderBucket_BuildMvp gives the same instance. A
// level plane (row 1 without its x and z parts) must differ at some points of
// the turned instances - the proof that the comparison can fail.
//
// THE SELECTORS. The retail side selector itself
// (RenderBucket_ApplyWaterSplitSideSelector) for the four labels and an unknown
// one, specLightX 1 and -1, a corner below (+5) and above (-5): drawn or not,
// colour changed or not, SXY moved or not must be what
// NativeRenderLayer_SplitSides says - 20 cases, 0 may differ. The colour
// factor of the native side against the queue's own colour for every channel
// value 0..255: shift and mask (specLightX 1, 0x7f7f7f) within 0.5, the 3/4 of
// dim-xor within 1.25 (it rounds down twice). The labels of the instance flags
// 0, REFLECTION_FUNC23, WATER_SPLIT_WHITE and both, as the queue writes them,
// must be both-mask, negative, dim-xor and xor.
//
// THE FACTOR RULE (D1, NativeRenderLayer_RuleFactor): the factor in force 0, 1,
// 2 and 4 gives 1, 1, 2, 4; at the native position a canvas 918 wide on a
// display 216 high in a target of 1600 x 400, 1836 x 432 and 3440 x 1440 gives
// 1, 2 and 3, and a display height of 0 gives 1.
//
// Its own file because it runs the queue's matrix steps, which use the
// coprocessor: the render layer itself must not. Nothing of this runs in a
// game; the switch returns before the window, like --gte-selftest. One line on
// stdout, 0 = passed. The values are deterministic, so the ctest pins the line.
// ===========================================================================

#include <math.h>
#include <stdio.h>
#include <string.h>

struct NativeDepthCheckPoint
{
	struct NrDrawItem item;
	double w;       // true depth of the middle of the body
	double matrix;  // w(1, 0.5, 1) - w(-1, 0.5, -1)
	double originZ; // mvp.t[2] / 2^shift: the view z of the origin
	int shiftHeld;  // the layer's own check of the shift (mvp.t[2]) held
	int found;      // the depth was hit
};

// w of a unit point through the matrix of an item, with the shift given.
static double NativeDepthCheck_W(const struct NrDrawItem *item, int shift, double ux, double uy, double uz)
{
	struct NrDrawItem copy = *item;
	double S[4][4];

	copy.mvpShift = (s8)shift;
	NativeRenderLayer_ItemMatrix(&copy, 0.0, 0.0, S);
	return (S[3][0] * ux) + (S[3][1] * uy) + (S[3][2] * uz) + S[3][3];
}

// The matrix part (corner minus opposite corner) of an item, with the shift given.
static double NativeDepthCheck_Matrix(const struct NrDrawItem *item, int shift)
{
	return NativeDepthCheck_W(item, shift, 1.0, 0.5, 1.0) - NativeDepthCheck_W(item, shift, -1.0, 0.5, -1.0);
}

static void NativeDepthCheck_Place(int viewZ, int huge, int modelScale, struct NativeDepthCheckPoint *out)
{
	static struct GameTracker tracker;
	static struct Instance inst;
	static struct PushBuffer pb;
	static struct InstDrawPerPlayer idpp;
	static struct ModelHeader mh;
	static struct ModelFrame frame;
	struct RenderBucketMatrixState matrixState;
	MATRIX projectionMvp;
	VECTOR viewPos;
	int notes;
	int z;

	memset(out, 0, sizeof(*out));
	memset(&tracker, 0, sizeof(tracker));
	memset(&inst, 0, sizeof(inst));
	memset(&pb, 0, sizeof(pb));
	memset(&idpp, 0, sizeof(idpp));
	memset(&mh, 0, sizeof(mh));
	memset(&frame, 0, sizeof(frame));
	memset(&matrixState, 0, sizeof(matrixState));
	memset(&viewPos, 0, sizeof(viewPos));

	// The camera: turned about y by 30 degrees (0.866 and 0.5 in 4.12), at
	// (1000, 200, -3000). A view of 512 x 240 at a screen distance of 256.
	pb.matrix_ViewProj.m[0][0] = 3547;
	pb.matrix_ViewProj.m[0][2] = -2048;
	pb.matrix_ViewProj.m[1][1] = 0x1000;
	pb.matrix_ViewProj.m[2][0] = 2048;
	pb.matrix_ViewProj.m[2][2] = 3547;
	pb.pos.x = 1000;
	pb.pos.y = 200;
	pb.pos.z = -3000;
	pb.distanceToScreen_PREV = 256;
	pb.rect.w = 512;
	pb.rect.h = 240;

	// The instance: turned about y by about 37 degrees (0.8 and 0.6), instance
	// scale 1.25, model scale 1.5.
	inst.matrix.m[0][0] = 3277;
	inst.matrix.m[0][2] = 2458;
	inst.matrix.m[1][1] = 0x1000;
	inst.matrix.m[2][0] = -2458;
	inst.matrix.m[2][2] = 3277;
	inst.matrix.t[0] = 1300;
	inst.matrix.t[1] = 150;
	inst.scale.x = 0x1400;
	inst.scale.y = 0x1400;
	inst.scale.z = 0x1400;
	inst.flags = huge ? DRAW_HUGE : 0;
	mh.scale.x = (s16)modelScale;
	mh.scale.y = (s16)modelScale;
	mh.scale.z = (s16)modelScale;

	idpp.ptrCurrFrame = &frame;
	idpp.ptrNextFrame = NULL;

	// The z that gives exactly this view z through the queue's own transform.
	for (z = -6000; z <= 12000; z++)
	{
		inst.matrix.t[2] = z;
		RenderBucket_GetViewPosition(&inst, &pb, &viewPos);
		if (viewPos.vz == viewZ)
		{
			out->found = 1;
			break;
		}
	}
	if (!out->found)
	{
		return;
	}

	// The queue, in the order of RenderBucket_QueueDraw.
	{
		const int viewDepth = viewPos.vz;

		RenderBucket_AdjustViewPositionForMvp(&inst, &viewPos);
		RenderBucket_StoreMvpTranslation(&idpp, &viewPos);
		RenderBucket_BuildM3x3(&inst, &mh, viewDepth, &matrixState);
		RenderBucket_StoreMatrixWords(&idpp.m3x3, matrixState.m0, matrixState.m1, matrixState.m2, matrixState.m3, matrixState.m4);
		RenderBucket_BuildMvp(&pb, &idpp, &projectionMvp);
	}

	// The render layer, as for a native draw in view 0 without a draw offset;
	// the units of a char item as NativeRenderLayer_RouteChar sets them (a
	// probe item never reads them).
	notes = NativeRenderLayer_FillItem(&out->item, &tracker, &inst, &idpp, &pb, 0);
	out->item.unitScale[0] = 16384.0 / (double)mh.scale.x;
	out->item.unitScale[1] = 16384.0 / (double)mh.scale.y;
	out->item.unitScale[2] = 16384.0 / (double)(u16)mh.scale.z;
	out->shiftHeld = (notes & NR_ITEM_SHIFT_OFF) == 0;
	out->w = NativeDepthCheck_W(&out->item, out->item.mvpShift, 0.0, 0.5, 0.0);
	out->matrix = NativeDepthCheck_Matrix(&out->item, out->item.mvpShift);
	out->originZ = ldexp((double)out->item.mvpT[2], -(int)out->item.mvpShift);
}

struct NativeDepthCheckPair
{
	struct NativeDepthCheckPoint nearPoint;
	struct NativeDepthCheckPoint farPoint;
	double step;
	double rest;
	double matrixPart;
	double wrongNear;
	double wrongFar;
	double matrixWrongNear;
	double matrixWrongFar;
};

static void NativeDepthCheck_Pair(int huge, struct NativeDepthCheckPair *pair)
{
	const int nearShift = huge ? 0 : 2;
	const int farShift = huge ? -2 : 0;
	const struct NrDrawItem *nearItem;
	const struct NrDrawItem *farItem;
	double nearW;
	double farW;

	NativeDepthCheck_Place(0x0FFF, huge, 0x1800, &pair->nearPoint);
	NativeDepthCheck_Place(0x1000, huge, 0x1800, &pair->farPoint);
	nearItem = &pair->nearPoint.item;
	farItem = &pair->farPoint.item;
	nearW = pair->nearPoint.w;
	farW = pair->farPoint.w;

	pair->step = (fabs(farW - nearW) / nearW) * 100.0;
	pair->rest = (fabs((farW - nearW) - (pair->farPoint.originZ - pair->nearPoint.originZ)) / nearW) * 100.0;
	pair->matrixPart = (fabs(pair->farPoint.matrix - pair->nearPoint.matrix) / fabs(pair->nearPoint.matrix)) * 100.0;

	// Each side with the shift of the other side.
	pair->wrongNear = (fabs(NativeDepthCheck_W(nearItem, farShift, 0.0, 0.5, 0.0) - farW) / farW) * 100.0;
	pair->wrongFar = (fabs(NativeDepthCheck_W(farItem, nearShift, 0.0, 0.5, 0.0) - nearW) / nearW) * 100.0;
	pair->matrixWrongNear = (fabs(NativeDepthCheck_Matrix(nearItem, farShift) - pair->farPoint.matrix) / fabs(pair->farPoint.matrix)) * 100.0;
	pair->matrixWrongFar = (fabs(NativeDepthCheck_Matrix(farItem, nearShift) - pair->nearPoint.matrix) / fabs(pair->nearPoint.matrix)) * 100.0;
}

// w of a point of the model (model units) through the matrix of a char item,
// with the shift given.
static double NativeDepthCheck_CharW(const struct NrDrawItem *item, int shift, double mx, double my, double mz)
{
	struct NrDrawItem copy = *item;
	double S[4][4];

	copy.mvpShift = (s8)shift;
	NativeRenderLayer_CharItemMatrix(&copy, 0.0, 0.0, S);
	return (S[3][0] * mx) + (S[3][1] * my) + (S[3][2] * mz) + S[3][3];
}

static double NativeDepthCheck_CharMatrix(const struct NrDrawItem *item, int shift)
{
	return NativeDepthCheck_CharW(item, shift, 20.0, 32.0, 20.0) - NativeDepthCheck_CharW(item, shift, -20.0, 32.0, -20.0);
}

// The pair 0x0FFF / 0x1000 of a normal instance on the char way, model scale given.
static void NativeDepthCheck_CharPair(int modelScale, struct NativeDepthCheckPair *pair)
{
	const struct NrDrawItem *nearItem;
	const struct NrDrawItem *farItem;
	double nearW;
	double farW;
	double nearM;
	double farM;

	NativeDepthCheck_Place(0x0FFF, 0, modelScale, &pair->nearPoint);
	NativeDepthCheck_Place(0x1000, 0, modelScale, &pair->farPoint);
	nearItem = &pair->nearPoint.item;
	farItem = &pair->farPoint.item;
	nearW = NativeDepthCheck_CharW(nearItem, nearItem->mvpShift, 0.0, 32.0, 0.0);
	farW = NativeDepthCheck_CharW(farItem, farItem->mvpShift, 0.0, 32.0, 0.0);
	nearM = NativeDepthCheck_CharMatrix(nearItem, nearItem->mvpShift);
	farM = NativeDepthCheck_CharMatrix(farItem, farItem->mvpShift);
	pair->nearPoint.w = nearW;
	pair->farPoint.w = farW;
	pair->nearPoint.matrix = nearM;
	pair->farPoint.matrix = farM;

	pair->step = (fabs(farW - nearW) / nearW) * 100.0;
	pair->rest = (fabs((farW - nearW) - (pair->farPoint.originZ - pair->nearPoint.originZ)) / nearW) * 100.0;
	pair->matrixPart = (fabs(farM - nearM) / fabs(nearM)) * 100.0;
	pair->wrongNear = (fabs(NativeDepthCheck_CharW(nearItem, 0, 0.0, 32.0, 0.0) - farW) / farW) * 100.0;
	pair->wrongFar = (fabs(NativeDepthCheck_CharW(farItem, 2, 0.0, 32.0, 0.0) - nearW) / nearW) * 100.0;
	pair->matrixWrongNear = (fabs(NativeDepthCheck_CharMatrix(nearItem, 0) - farM) / fabs(farM)) * 100.0;
	pair->matrixWrongFar = (fabs(NativeDepthCheck_CharMatrix(farItem, 2) - nearM) / fabs(nearM)) * 100.0;
}

static int NativeDepthCheck_PairPassed(const struct NativeDepthCheckPair *pair, int nearShift, int farShift)
{
	return pair->nearPoint.found && pair->farPoint.found && pair->nearPoint.shiftHeld && pair->farPoint.shiftHeld &&
	       ((int)pair->nearPoint.item.mvpShift == nearShift) && ((int)pair->farPoint.item.mvpShift == farShift) && (pair->step < 0.1) &&
	       (pair->rest < 0.1) && (pair->matrixPart < 0.5) && (pair->wrongNear > 50.0) && (pair->wrongFar > 50.0) && (pair->matrixWrongNear > 50.0) &&
	       (pair->matrixWrongFar > 50.0);
}

// --- Step 4e, stage a: the water line ----------------------------------------

struct NativeDepthCheckSplit
{
	int found;
	int handlerSplit;
	int branch;
	int selectorBoth;
	int composedSame;
	int splitLine;
	int nearView;
	int compared;
	int differ;
	int below;
	int above;
	int levelDiffer; // a level plane (row 1 without its x and z parts) - the proof the check can fail
	// Step 4e, stage b (a REFLECTIVE instance, or a plane far below): the
	// handler and selector the queue chose, the mirror against the queue's own
	// mirrored corners in view space, and whether the mirror turns the
	// winding (det < 0 against det > 0 of the body).
	int drawFunc;
	int selectorIndex;
	int mirrorCompared;
	double mirrorError;
	int cullFlipped;
};

static double NativeDepthCheck_Det(const double m[3][3])
{
	return (m[0][0] * ((m[1][1] * m[2][2]) - (m[1][2] * m[2][1]))) - (m[0][1] * ((m[1][0] * m[2][2]) - (m[1][2] * m[2][0]))) +
	       (m[0][2] * ((m[1][0] * m[2][1]) - (m[1][1] * m[2][0])));
}

static void NativeDepthCheck_Split(int viewZ, int turned, int modelScale, int instScale, u32 instFlags, int planeBelow, struct NativeDepthCheckSplit *out)
{
	static struct GameTracker tracker;
	static struct Instance inst;
	static struct PushBuffer pb;
	static struct InstDrawPerPlayer idpp;
	static struct InstDrawPerPlayer idppNormal;
	static struct ModelHeader mh;
	static struct ModelFrame frame;
	static struct NrDrawItem item;
	struct RenderBucketMatrixState matrixState;
	struct RenderBucketSplitState split;
	MATRIX projectionMvp;
	MATRIX normalMvp;
	VECTOR viewPos;
	double plane[4];
	u32 flags;
	int drawFunc = 0;
	int uncompressFunc = 0;
	int viewDepth;
	int z;
	int ix;
	int iy;
	int iz;
	int r;
	int c;

	memset(out, 0, sizeof(*out));
	memset(&tracker, 0, sizeof(tracker));
	memset(&inst, 0, sizeof(inst));
	memset(&pb, 0, sizeof(pb));
	memset(&idpp, 0, sizeof(idpp));
	memset(&mh, 0, sizeof(mh));
	memset(&frame, 0, sizeof(frame));
	memset(&item, 0, sizeof(item));
	memset(&matrixState, 0, sizeof(matrixState));
	memset(&viewPos, 0, sizeof(viewPos));

	// The camera of NativeDepthCheck_Place.
	pb.matrix_ViewProj.m[0][0] = 3547;
	pb.matrix_ViewProj.m[0][2] = -2048;
	pb.matrix_ViewProj.m[1][1] = 0x1000;
	pb.matrix_ViewProj.m[2][0] = 2048;
	pb.matrix_ViewProj.m[2][2] = 3547;
	pb.pos.x = 1000;
	pb.pos.y = 200;
	pb.pos.z = -3000;
	pb.distanceToScreen_PREV = 256;
	pb.rect.w = 512;
	pb.rect.h = 240;

	// turned 1: about z by about 20 degrees (0.94 and 0.34), the height in
	// split space then takes x as well; turned 2: about x (a kart nodding on a
	// slope), the height takes z; 0: the identity.
	if (turned == 1)
	{
		inst.matrix.m[0][0] = 3849;
		inst.matrix.m[0][1] = -1401;
		inst.matrix.m[1][0] = 1401;
		inst.matrix.m[1][1] = 3849;
		inst.matrix.m[2][2] = 0x1000;
	}
	else if (turned == 2)
	{
		inst.matrix.m[0][0] = 0x1000;
		inst.matrix.m[1][1] = 3849;
		inst.matrix.m[1][2] = -1401;
		inst.matrix.m[2][1] = 1401;
		inst.matrix.m[2][2] = 3849;
	}
	else
	{
		inst.matrix.m[0][0] = 0x1000;
		inst.matrix.m[1][1] = 0x1000;
		inst.matrix.m[2][2] = 0x1000;
	}
	inst.matrix.t[0] = 1300;
	inst.matrix.t[1] = 150;
	inst.scale.x = (s16)instScale;
	inst.scale.y = (s16)instScale;
	inst.scale.z = (s16)instScale;
	inst.flags = instFlags;
	mh.scale.x = (s16)modelScale;
	mh.scale.y = (s16)modelScale;
	mh.scale.z = (s16)modelScale;
	idpp.ptrCurrFrame = &frame;
	idpp.ptrNextFrame = NULL;

	for (z = -6000; z <= 12000; z++)
	{
		inst.matrix.t[2] = z;
		RenderBucket_GetViewPosition(&inst, &pb, &viewPos);
		if (viewPos.vz == viewZ)
		{
			out->found = 1;
			break;
		}
	}
	if (!out->found)
	{
		return;
	}

	// The queue, in the order of RenderBucket_QueueDraw.
	viewDepth = viewPos.vz;
	RenderBucket_AdjustViewPositionForMvp(&inst, &viewPos);
	RenderBucket_StoreMvpTranslation(&idpp, &viewPos);
	RenderBucket_BuildM3x3(&inst, &mh, viewDepth, &matrixState);
	RenderBucket_StoreMatrixWords(&idpp.m3x3, matrixState.m0, matrixState.m1, matrixState.m2, matrixState.m3, matrixState.m4);

	// The water through the middle of the hull (input y 0..2400): its height in
	// split space is row 1 of m3x3 at y 1200, and splitLine = 4 x (vertSplit -
	// t.y) in both branches here (the branch P divides by a scale of 0x1000).
	// planeBelow (stage b): 2000 world units below the origin - beyond the 362
	// split units of the branch R, so a plain SPLIT_LINE gets no split output
	// and the handler SPECIAL.
	inst.vertSplit = (s16)(planeBelow ? (inst.matrix.t[1] - 2000) : (inst.matrix.t[1] + ((((int)idpp.m3x3.m[1][1] * 1200) / 4096) / 4)));
	idppNormal = idpp;

	flags = inst.flags;
	split = RenderBucket_BuildSplitState(&inst, &mh, &frame, NULL, &pb, &idpp, viewDepth, &flags, &matrixState, &projectionMvp);
	RenderBucket_SelectRetailHandlers(&flags, &split, &drawFunc, &uncompressFunc);
	idpp.unkEC = drawFunc;
	idpp.unkF0 = uncompressFunc;
	RenderBucket_CopyDispatchTables();
	RenderBucket_WriteInstanceCallbackLabels(&inst, flags);

	out->handlerSplit = (drawFunc == RB_RETAIL_DRAWFUNC_SPLIT);
	out->drawFunc = drawFunc;
	out->selectorIndex = NativeRenderLayer_SelectorIndex((u32)(size_t)inst.funcPtr[2]);
	out->branch = NativeRenderLayer_SplitBranch(&idpp);
	out->selectorBoth = (NativeRenderLayer_SelectorIndex((u32)(size_t)inst.funcPtr[2]) == NR_SELECTOR_BOTH_MASK);
	out->splitLine = (int)idpp.splitLine;

	// The render layer, as NativeRenderLayer_RouteCharView fills the item.
	(void)NativeRenderLayer_FillItem(&item, &tracker, &inst, &idpp, &pb, 0);
	out->nearView = item.nearView;
	item.unitScale[0] = 16384.0 / (double)mh.scale.x;
	item.unitScale[1] = 16384.0 / (double)mh.scale.y;
	item.unitScale[2] = 16384.0 / (double)(u16)mh.scale.z;
	if (out->branch == 1)
	{
		NativeRenderLayer_ComposeModelView(&pb.matrix_ViewProj, &idpp.m3x3, item.mvp);
	}
	RenderBucket_BuildMvp(&pb, &idppNormal, &normalMvp);
	out->composedSame = 1;
	for (r = 0; r < 3; r++)
	{
		for (c = 0; c < 3; c++)
		{
			if (item.mvp[r][c] != normalMvp.m[r][c])
			{
				out->composedSame = 0;
			}
		}
	}
	NativeRenderLayer_SplitPlane(&idpp, out->branch == 1, item.unitScale, plane);

	// Both sides of every point.
	RenderBucket_GteLoadLightMatrixWords(&idpp.m3x3);
	for (ix = 0; ix < 9; ix++)
	{
		for (iy = 0; iy < 9; iy++)
		{
			for (iz = 0; iz < 9; iz++)
			{
				const int gx = -1200 + (ix * 300);
				const int gy = iy * 300;
				const int gz = -1600 + (iz * 400);
				double nativeD;
				int y;
				int retailD;

				if (out->branch == 1)
				{
					MTC2(CTR_PackS16Pair(gx, gy), 0);
					MTC2(CTR_PackS16Pair(gz, 0), 1);
					doCOP2(0x04a6012);
					y = (s16)MFC2(10);
				}
				else
				{
					y = gy;
				}
				retailD = (s16)((int)(s16)idpp.splitLine - y);
				nativeD = plane[3] - ((plane[0] * ((double)gx / item.unitScale[0])) + (plane[1] * ((double)gy / item.unitScale[1])) +
				                      (plane[2] * ((double)gz / item.unitScale[2])));
				if (fabs(nativeD) <= 1.0)
				{
					continue;
				}
				out->compared++;
				if ((retailD >= 0) != (nativeD >= 0.0))
				{
					out->differ++;
				}
				{
					const double levelD = plane[3] - (plane[1] * ((double)gy / item.unitScale[1]));

					if ((fabs(levelD) > 1.0) && ((retailD >= 0) != (levelD >= 0.0)))
					{
						out->levelDiffer++;
					}
				}
				if (nativeD >= 0.0)
				{
					out->below++;
				}
				else
				{
					out->above++;
				}
			}
		}
	}

	// Stage b: the mirror of REFLECTION (split space in the branch R) or of
	// SPECIAL (the input units), corner by corner as the queue mirrors it
	// (RenderBucket_MirrorSpecialPackedXY on the packed corner) and turns it
	// with the matrix of the view (idpp->mvp: the view matrix in the branch R,
	// the model-view matrix otherwise), against the native mirror map.
	if ((drawFunc == RB_RETAIL_DRAWFUNC_REFLECTION) || (drawFunc == RB_RETAIL_DRAWFUNC_SPECIAL))
	{
		static struct RenderBucketDrawContext ctx;
		const int splitSpace = (drawFunc == RB_RETAIL_DRAWFUNC_REFLECTION) && (out->branch == 1);
		const double scale = ldexp(1.0, (int)item.mvpShift);
		double body[3][3];

		memset(&ctx, 0, sizeof(ctx));
		ctx.idpp = &idpp;
		NativeRenderLayer_MirrorMatrix(&item, &idpp, &pb, splitSpace);
		for (r = 0; r < 3; r++)
		{
			for (c = 0; c < 3; c++)
			{
				body[r][c] = (double)item.mvp[r][c];
			}
		}
		out->cullFlipped = (NativeDepthCheck_Det((const double(*)[3])item.mirrorA) < 0.0) && (NativeDepthCheck_Det((const double(*)[3])body) > 0.0);

		RenderBucket_GteLoadLightMatrixWords(&idpp.m3x3);
		for (ix = 0; ix < 5; ix++)
		{
			for (iy = 0; iy < 5; iy++)
			{
				for (iz = 0; iz < 5; iz++)
				{
					const int gx = -1200 + (ix * 600);
					const int gy = iy * 600;
					const int gz = -1600 + (iz * 800);
					const double p[3] = {(double)gx / item.unitScale[0], (double)gy / item.unitScale[1], (double)gz / item.unitScale[2]};
					u32 packed;
					int sigma[3];

					if (splitSpace)
					{
						MTC2(CTR_PackS16Pair(gx, gy), 0);
						MTC2(CTR_PackS16Pair(gz, 0), 1);
						doCOP2(0x04a6012);
						sigma[0] = (s16)MFC2(9);
						sigma[1] = (s16)MFC2(10);
						sigma[2] = (s16)MFC2(11);
					}
					else
					{
						sigma[0] = gx;
						sigma[1] = gy;
						sigma[2] = gz;
					}
					packed = RenderBucket_MirrorSpecialPackedXY(&ctx, CTR_PackS16Pair(sigma[0], sigma[1]));
					sigma[0] = (s16)(packed & 0xffffu);
					sigma[1] = (s16)(packed >> 16);
					for (r = 0; r < 3; r++)
					{
						const double retail = ((((double)idpp.mvp.m[r][0] * sigma[0]) + ((double)idpp.mvp.m[r][1] * sigma[1]) +
						                        ((double)idpp.mvp.m[r][2] * sigma[2])) /
						                       4096.0) +
						                      (double)idpp.mvp.t[r];
						const double native =
						    ((item.mirrorA[r][0] * p[0]) + (item.mirrorA[r][1] * p[1]) + (item.mirrorA[r][2] * p[2]) + item.mirrorB[r]) * scale;
						const double error = fabs(retail - native);

						out->mirrorError = (error > out->mirrorError) ? error : out->mirrorError;
					}
					out->mirrorCompared++;
				}
			}
		}
	}
}

// THE ITEMS OF A SPLIT VIEW (G4ea S2): NativeRenderLayer_SetUpSplit on a filled
// item of a kart nodding nose down (about x, so the front wheels lie lower), the
// plane through its origin, selector both-mask: the item above keeps d < 0
// (-1) in otRangeNormal, untinted, with the rear wheels; the item below keeps
// d >= 0 (+1) in otRangeSecondary, halved, with the front wheels; both bodies
// on, both the same plane, the masks disjoint and together all four. 1 = held.
static int NativeDepthCheck_SplitItems(void)
{
	static struct Instance inst;
	static struct InstDrawPerPlayer idpp;
	static struct NativeCharGpu gpu;
	static struct NrDrawItem above;
	static struct NrDrawItem below;
	double plane[4];
	int held = 1;
	int k;

	memset(&inst, 0, sizeof(inst));
	memset(&idpp, 0, sizeof(idpp));
	memset(&gpu, 0, sizeof(gpu));
	memset(&above, 0, sizeof(above));
	memset(&below, 0, sizeof(below));
	inst.matrix.m[0][0] = 0x1000;
	inst.matrix.m[1][1] = 3849;
	inst.matrix.m[1][2] = -1401;
	inst.matrix.m[2][1] = 1401;
	inst.matrix.m[2][2] = 3849;
	inst.matrix.t[1] = 150;
	inst.vertSplit = 150;
	inst.scale.x = 0x1000;
	inst.scale.y = 0x1000;
	inst.scale.z = 0x1000;
	inst.funcPtr[2] = (void *)(size_t)RB_RETAIL_INST_FUNC2_SPLIT_BOTH_MASK;
	inst.specLightX = 1;
	inst.reflectionRGBA = 0x7f7f7fu;
	idpp.m3x3 = inst.matrix;
	idpp.splitLine = 0;
	gpu.wheelFront[0] = 30.0f;
	gpu.wheelFront[1] = 20.0f;
	gpu.wheelFront[2] = 60.0f;
	gpu.wheelRear[0] = 30.0f;
	gpu.wheelRear[1] = 20.0f;
	gpu.wheelRear[2] = -60.0f;
	above.kind = NR_ITEM_CHAR;
	above.gpu = &gpu;
	above.nativeWheels = 1;
	above.primary = 1;
	above.bodyOn = 1;
	above.wheelMask = NR_WHEEL_MASK_ALL;
	above.unitScale[0] = 4.0;
	above.unitScale[1] = 4.0;
	above.unitScale[2] = 4.0;
	above.tintScale[0] = 1.0f;
	above.tintScale[1] = 1.0f;
	above.tintScale[2] = 1.0f;

	NativeRenderLayer_SetUpSplit(&above, &below, &inst, &idpp, 1);
	NativeRenderLayer_SplitPlane(&idpp, 1, above.unitScale, plane);

	held = held && (above.splitKeep == -1) && (below.splitKeep == 1) && (above.part == NR_PART_ABOVE) && (below.part == NR_PART_BELOW);
	held = held && (above.primary == 1) && (below.primary == 0) && (above.secondRange == 0) && (below.secondRange == 1);
	held = held && above.bodyOn && below.bodyOn && (above.ofsXExtra == 0) && (below.ofsXExtra == 0);
	for (k = 0; k < 3; k++)
	{
		held = held && (above.tintScale[k] == 1.0f) && (below.tintScale[k] == 0.5f);
	}
	for (k = 0; k < 4; k++)
	{
		held = held && (above.split[k] == plane[k]) && (below.split[k] == plane[k]);
	}
	held = held && (below.wheelMask == 0x03u) && (above.wheelMask == 0x0Cu) && ((above.wheelMask & below.wheelMask) == 0u) &&
	       ((above.wheelMask | below.wheelMask) == NR_WHEEL_MASK_ALL);
	return held;
}

// NativeRenderLayer_WheelBelow against game/DrawTires.c's rule in its own
// integer units (G4ea S2): the middle in four-times units (local = 4 m, as
// NativeRenderLayer_FillNativeWheels has it), scaled by inst->scale >> 12,
// turned by the rotation >> 12, plus (t.y - pos.y) << 2, against
// splitCameraY = (vertSplit - pos.y) << 2 (DrawTiresSolid_SetupGteState,
// DrawTiresSolid_SelectProjectedWheel: below when splitCameraY - y >= 0). For
// the four wheels of a nodding kart and the plane at every height from 80
// below to 80 above its origin; points within one four-times unit of the plane
// are left out (the integer cuts). Returns the number that differ.
static int NativeDepthCheck_WheelBelow(int *compared)
{
	static struct Instance inst;
	static const double middles[4][3] = {{30.0, 20.0, 60.0}, {-30.0, 20.0, 60.0}, {30.0, 20.0, -60.0}, {-30.0, 20.0, -60.0}};
	int differ = 0;
	int h;
	int wheel;

	*compared = 0;
	memset(&inst, 0, sizeof(inst));
	inst.matrix.m[0][0] = 0x1000;
	inst.matrix.m[1][1] = 3849;
	inst.matrix.m[1][2] = -1401;
	inst.matrix.m[2][1] = 1401;
	inst.matrix.m[2][2] = 3849;
	inst.matrix.t[1] = 150;
	inst.scale.x = 0x1400;
	inst.scale.y = 0x1400;
	inst.scale.z = 0x1400;

	for (h = -80; h <= 80; h++)
	{
		inst.vertSplit = (s16)(inst.matrix.t[1] + h);
		for (wheel = 0; wheel < 4; wheel++)
		{
			const int splitCameraY = (int)inst.vertSplit << 2;
			int local[3];
			int y = (int)inst.matrix.t[1] << 2;
			int c;
			int retail;

			for (c = 0; c < 3; c++)
			{
				const int scale = (c == 0) ? inst.scale.x : ((c == 1) ? inst.scale.y : inst.scale.z);

				local[c] = ((int)(4.0 * middles[wheel][c]) * scale) >> 12;
			}
			y += (((int)inst.matrix.m[1][0] * local[0]) + ((int)inst.matrix.m[1][1] * local[1]) + ((int)inst.matrix.m[1][2] * local[2])) >> 12;
			if (abs(splitCameraY - y) <= 1)
			{
				continue;
			}
			retail = (splitCameraY - y) >= 0;
			(*compared)++;
			if (retail != NativeRenderLayer_WheelBelow(&inst, middles[wheel]))
			{
				differ++;
			}
		}
	}
	return differ;
}

// The classes of the colour factor (G4ea S3): exact only when every channel
// mask keeps all or none of the bits of 255 >> s and none above. Returns the
// number of cases that differ.
static int NativeDepthCheck_ShiftMaskClasses(int *cases)
{
	static const struct
	{
		int shift;
		u32 mask;
		int exact;
	} table[] = {
	    {1, 0x7f7f7fu, 1}, {1, 0xffffffu, 0}, {0, 0xffffffu, 1}, {1, 0x7f007fu, 1}, {2, 0x3f3f3fu, 1}, {2, 0x7f7f7fu, 0}, {1, 0x3f3f3fu, 0}, {8, 0x000000u, 1},
	};
	float factor[3];
	int differ = 0;
	int i;

	*cases = (int)(sizeof(table) / sizeof(table[0]));
	for (i = 0; i < *cases; i++)
	{
		if (NativeRenderLayer_ShiftMaskFactor(table[i].shift, table[i].mask, factor) != table[i].exact)
		{
			differ++;
		}
	}
	return differ;
}

// Stage b: the mirrored native wheels (NativeRenderLayer_MirrorWheels) against
// game/DrawTires.c's mirror in world units (the middle at 2 splitCameraY - y,
// the rim with y negated): taken back to world units by the inverse view
// matrix, x and z must stay and y must turn about the plane, the axes as well.
// Returns the largest deviation.
static double NativeDepthCheck_MirrorWheels(void)
{
	static struct Instance inst;
	static struct PushBuffer pb;
	static struct NrDrawItem item;
	double Vd[3][3];
	double Vi[3][3];
	double before[4][3];
	double beforeA[4][3][3];
	const double rel[3] = {120.0, 40.0, 2600.0}; // t - pos, world units
	double plane;
	double worst = 0.0;
	int wheel;
	int r;
	int c;
	int k;

	memset(&inst, 0, sizeof(inst));
	memset(&pb, 0, sizeof(pb));
	memset(&item, 0, sizeof(item));
	pb.matrix_ViewProj.m[0][0] = 3547;
	pb.matrix_ViewProj.m[0][2] = -2048;
	pb.matrix_ViewProj.m[1][1] = 0x1000;
	pb.matrix_ViewProj.m[2][0] = 2048;
	pb.matrix_ViewProj.m[2][2] = 3547;
	inst.matrix.t[1] = 150;
	inst.vertSplit = 110; // the plane 40 units below the origin
	for (r = 0; r < 3; r++)
	{
		for (c = 0; c < 3; c++)
		{
			Vd[r][c] = (double)pb.matrix_ViewProj.m[r][c] / 4096.0;
		}
	}
	if (!NativeRenderLayer_Invert3((const double(*)[3])Vd, Vi))
	{
		return 1e9;
	}
	for (r = 0; r < 3; r++)
	{
		double t = 0.0;

		for (k = 0; k < 3; k++)
		{
			t += Vd[r][k] * rel[k];
		}
		item.mvpT[r] = (s32)floor(t + 0.5);
	}
	item.mvpShift = 0;
	for (wheel = 0; wheel < NATIVE_WHEELS_COUNT; wheel++)
	{
		for (r = 0; r < 3; r++)
		{
			item.wheelB[wheel][r] = (double)item.mvpT[r] + (Vd[r][0] * (30.0 * (double)(wheel + 1))) + (Vd[r][1] * (10.0 - (15.0 * (double)wheel))) +
			                        (Vd[r][2] * (-20.0 * (double)wheel));
			for (c = 0; c < 3; c++)
			{
				item.wheelA[wheel][r][c] = (Vd[r][c] * (1.0 + (0.25 * (double)c))) + ((r == c) ? 0.0 : (0.01 * (double)(wheel + 1)));
			}
		}
	}
	memcpy(before, item.wheelB, sizeof(before));
	memcpy(beforeA, item.wheelA, sizeof(beforeA));
	if (!NativeRenderLayer_MirrorWheels(&item, &inst, &pb))
	{
		return 1e9;
	}

	// The plane in world units relative to the camera, as the native map has
	// it: the origin of the item (mvp.t, the view of t - pos) plus h.
	plane = (double)(inst.vertSplit - inst.matrix.t[1]);
	{
		double origin = 0.0;

		for (k = 0; k < 3; k++)
		{
			origin += Vi[1][k] * (double)item.mvpT[k];
		}
		plane += origin;
	}
	for (wheel = 0; wheel < NATIVE_WHEELS_COUNT; wheel++)
	{
		double w0[3];
		double w1[3];

		for (r = 0; r < 3; r++)
		{
			w0[r] = 0.0;
			w1[r] = 0.0;
			for (k = 0; k < 3; k++)
			{
				w0[r] += Vi[r][k] * before[wheel][k];
				w1[r] += Vi[r][k] * item.wheelB[wheel][k];
			}
		}
		worst = fmax(worst, fabs(w1[0] - w0[0]));
		worst = fmax(worst, fabs(w1[2] - w0[2]));
		worst = fmax(worst, fabs(w1[1] - ((2.0 * plane) - w0[1])));
		for (c = 0; c < 3; c++)
		{
			double a0[3];
			double a1[3];

			for (r = 0; r < 3; r++)
			{
				a0[r] = 0.0;
				a1[r] = 0.0;
				for (k = 0; k < 3; k++)
				{
					a0[r] += Vi[r][k] * beforeA[wheel][k][c];
					a1[r] += Vi[r][k] * item.wheelA[wheel][k][c];
				}
			}
			worst = fmax(worst, fabs(a1[0] - a0[0]));
			worst = fmax(worst, fabs(a1[1] + a0[1]));
			worst = fmax(worst, fabs(a1[2] - a0[2]));
		}
	}
	return worst;
}

// Stage b: the queue picked the handler and selector expected, the mirror lies
// within 2 view units of the queue's mirrored corners (the GTE rounds the
// split space down by up to one unit), and the mirror turns the winding.
static int NativeDepthCheck_MirrorPassed(const struct NativeDepthCheckSplit *sp, int drawFunc, int selectorIndex)
{
	return sp->found && (sp->drawFunc == drawFunc) && (sp->selectorIndex == selectorIndex) && sp->composedSame && (sp->mirrorCompared == 125) &&
	       (sp->mirrorError <= 2.0) && sp->cullFlipped;
}

static int NativeDepthCheck_SplitPassed(const struct NativeDepthCheckSplit *sp, int branch, int nearView)
{
	return sp->found && sp->handlerSplit && (sp->branch == branch) && sp->selectorBoth && sp->composedSame && (sp->nearView == nearView) &&
	       (sp->compared > 100) && (sp->differ == 0) && (sp->below > 0) && (sp->above > 0) && ((branch == 0) || (sp->levelDiffer > 0));
}

struct NativeDepthCheckSides
{
	int cases;
	int differ;
	double shiftMaskError;
	double dimError;
	int labelsHeld;
	int reflectionCases;
	int reflectionDiffer;
};

static void NativeDepthCheck_Sides(struct NativeDepthCheckSides *out)
{
	static struct Instance inst;
	static struct RenderBucketDrawContext ctx;
	static const u32 labels[5] = {RB_RETAIL_INST_FUNC2_SPLIT_BOTH_MASK, RB_RETAIL_INST_FUNC2_SPLIT_NEGATIVE, RB_RETAIL_INST_FUNC2_SPLIT_XOR,
	                              RB_RETAIL_INST_FUNC2_SPLIT_DIM_XOR, 0x8006d000u};
	static const int lights[2] = {1, -1};
	static const u32 flagSets[4] = {0, REFLECTION_FUNC23, WATER_SPLIT_WHITE, REFLECTION_FUNC23 | WATER_SPLIT_WHITE};
	static const int expectedIndex[4] = {NR_SELECTOR_BOTH_MASK, NR_SELECTOR_NEGATIVE, NR_SELECTOR_DIM_XOR, NR_SELECTOR_XOR};
	const u32 color = 0x00c08041u;
	const u32 sxy = 0x00200010u;
	int label;
	int light;
	int side;
	int value;
	int i;

	memset(out, 0, sizeof(*out));

	for (label = 0; label < 5; label++)
	{
		for (light = 0; light < 2; light++)
		{
			for (side = 0; side < 2; side++)
			{
				const int guard = (side == 0) ? 5 : -5; // 0: below, 1: above
				struct RenderBucketSplitVertex v[3];
				struct NrSplitSide above;
				struct NrSplitSide below;
				const struct NrSplitSide *mine;
				int drawn;

				memset(&inst, 0, sizeof(inst));
				memset(&ctx, 0, sizeof(ctx));
				memset(v, 0, sizeof(v));
				inst.funcPtr[2] = (void *)(size_t)labels[label];
				inst.specLightX = (s8)lights[light];
				inst.reflectionRGBA = 0x7f7f7fu;
				ctx.inst = &inst;
				ctx.waterSplitSide = -1; // the handler SPLIT
				for (i = 0; i < 3; i++)
				{
					v[i].color = color;
					v[i].sxy = sxy;
				}

				drawn = RenderBucket_ApplyWaterSplitSideSelector(&ctx, guard, &v[0], &v[1], &v[2]);
				(void)NativeRenderLayer_SplitSides(labels[label], lights[light], -1, &above, &below);
				mine = (guard >= 0) ? &below : &above;
				out->cases++;
				if ((drawn != 0) != (mine->body != 0))
				{
					out->differ++;
				}
				else if (drawn && (((v[0].color != color) != (mine->dimmed != 0)) || ((v[0].sxy != sxy) != (mine->offset != 0))))
				{
					out->differ++;
				}
			}
		}
	}

	// The colour factor against the queue's colour, every channel value.
	memset(&inst, 0, sizeof(inst));
	memset(&ctx, 0, sizeof(ctx));
	inst.specLightX = 1;
	inst.reflectionRGBA = 0x7f7f7fu;
	ctx.inst = &inst;
	{
		float factor[3];

		(void)NativeRenderLayer_ShiftMaskFactor(1, 0x7f7f7fu, factor);
		for (value = 0; value < 256; value++)
		{
			const u32 word = (u32)value | ((u32)value << 8) | ((u32)value << 16);
			const u32 halved = RenderBucket_WaterSplitShiftMaskColor(&ctx, word);
			const u32 dimmed = RenderBucket_WaterSplitDimColor(word);
			int ch;

			for (ch = 0; ch < 3; ch++)
			{
				const double a = fabs(((double)factor[ch] * (double)value) - (double)((halved >> (8 * ch)) & 0xffu));
				const double b = fabs((0.75 * (double)value) - (double)((dimmed >> (8 * ch)) & 0xffu));

				out->shiftMaskError = (a > out->shiftMaskError) ? a : out->shiftMaskError;
				out->dimError = (b > out->dimError) ? b : out->dimError;
			}
		}
	}

	// Stage b: the selectors a REFLECTION view can draw (the part above in both
	// passes): negative (the mirror pass halved), an unknown label (neither);
	// both-mask, xor and dim-xor are left to the CMDL.
	{
		static const int accepted[5] = {0, 1, 0, 0, 1};
		static const int mirrorDim[5] = {0, 1, 0, 0, 0};

		for (i = 0; i < 5; i++)
		{
			int originalDimmed = -1;
			int mirrorDimmed = -1;
			const int ok = NativeRenderLayer_ReflectionSides(labels[i], 1, &originalDimmed, &mirrorDimmed);

			out->reflectionCases++;
			if ((ok != accepted[i]) || (ok && ((originalDimmed != 0) || (mirrorDimmed != mirrorDim[i]))))
			{
				out->reflectionDiffer++;
			}
		}
	}

	// The labels as the queue writes them from the instance flags.
	out->labelsHeld = 1;
	for (i = 0; i < 4; i++)
	{
		memset(&inst, 0, sizeof(inst));
		RenderBucket_CopyDispatchTables();
		RenderBucket_WriteInstanceCallbackLabels(&inst, flagSets[i]);
		if (NativeRenderLayer_SelectorIndex((u32)(size_t)inst.funcPtr[2]) != expectedIndex[i])
		{
			out->labelsHeld = 0;
		}
	}
}

// The factor rule (D1): the cases, 0 = all held.
static int NativeDepthCheck_RuleFactor(int *cases)
{
	struct
	{
		int atNative;
		int effective;
		int targetW;
		int targetH;
		int canvasW;
		int displayH;
		int expected;
	} const table[] = {
	    {0, 0, 0, 0, 0, 0, 1},       {0, 1, 0, 0, 0, 0, 1},         {0, 2, 0, 0, 0, 0, 2},         {0, 4, 0, 0, 0, 0, 4},
	    {1, 0, 1600, 400, 918, 216, 1}, {1, 0, 1836, 432, 918, 216, 2}, {1, 0, 3440, 1440, 918, 216, 3}, {1, 0, 3440, 1440, 918, 0, 1},
	};
	int failed = 0;
	int i;

	*cases = (int)(sizeof(table) / sizeof(table[0]));
	for (i = 0; i < *cases; i++)
	{
		if (NativeRenderLayer_RuleFactor(table[i].atNative, table[i].effective, table[i].targetW, table[i].targetH, table[i].canvasW, table[i].displayH) !=
		    table[i].expected)
		{
			failed++;
		}
	}
	return failed;
}

// THE BIN MARKERS OF THE TWIN (one marker per occupied bin). Seven keys with
// the bins 40 33 33 25 12 9 2 (the later command first, as a paint order has
// them) against the cells 10..31 of a depth range: the runs are the cells 31 (3
// keys, two of them held down from 33 and one from 40), 25, 12 and 10 (2 keys,
// held up from 9 and 2), 5 keys held. Linked into a made-up range of those
// cells (a guard word on each side): every slice heads the cell of its run and
// no other word changes, its marker carries the slice bit and the slice index,
// the first slice alone is the lead, and the keys of the slices follow one
// another from the item's first key. The limits: 384 bin markers fit an empty
// arena of 512 (128 kept for the items), 385 do not, nor one more after 384;
// NativeTwin_PaintRuns reports 65 cells as runMax + 1 with runMax 64
// (NR_TWIN_BINS_MAX), and 64 cells as 64. That the route then falls back to
// one marker per item needs a paint order, so a twin source, and is not run
// here (NativeDepthCheck_TwinView runs the fallbacks that need none). Returns
// 1 when all held; cells and *held for the line.
static int NativeDepthCheck_TwinBins(int cells[4], int *held)
{
	static const int bins[7] = {40, 33, 33, 25, 12, 9, 2};
	static const int wantCell[4] = {31, 25, 12, 10};
	static const u32 wantCount[4] = {3, 1, 1, 2};
	static u32 table[24];
	static u64 many[NR_TWIN_BINS_MAX + 1];
	u64 keys[7];
	u32 runFirst[NR_TWIN_BINS_MAX];
	u32 runCount[NR_TWIN_BINS_MAX];
	s16 runCell[NR_TWIN_BINS_MAX];
	u32 heldKeys = 0;
	u32 runs;
	const int dbBefore = s_nrMarkerDb;
	const int markersBefore = s_nrMarkerCount[0];
	const int slicesBefore = s_nrSliceCount[0];
	int ok = 1;
	int written;
	int range;
	int i;
	u32 r;

	for (i = 0; i < 7; i++)
	{
		keys[i] = ((u64)(u32)bins[i] << 40) | ((u64)(u32)(6 - i) << 20) | (u64)(u32)i;
	}
	runs = NativeTwin_PaintRuns(keys, 7, 10, 31, runFirst, runCount, runCell, NR_TWIN_BINS_MAX, &heldKeys);
	ok = (runs == 4u) && (heldKeys == 5u);
	for (r = 0; ok && (r < runs); r++)
	{
		ok = ((int)runCell[r] == wantCell[r]) && (runCount[r] == wantCount[r]);
		cells[r] = (int)runCell[r];
	}
	*held = (int)heldKeys;
	if (!ok)
	{
		return 0;
	}

	// Into the made-up range: table[1 + (cell - 10)] is the cell, table[0] and
	// table[23] the guards.
	if (!NativeGpuLinks_IsRegisteredHostRange(s_nrMarkers[0], sizeof(s_nrMarkers[0])))
	{
		NativeGpuLinks_RegisterRangeChecked("selftest native markers", s_nrMarkers[0], sizeof(s_nrMarkers[0]));
	}
	for (i = 0; i < 24; i++)
	{
		table[i] = 0x00ABC000u + (u32)i;
	}
	s_nrMarkerDb = 0;
	s_nrMarkerCount[0] = 0;
	s_nrSliceCount[0] = 0;
	range = (int)(size_t)&table[1] - (10 * 4);
	written = NativeRenderLayer_LinkTwinRuns(range, 5, 100u, runFirst, runCount, runCell, runs);
	ok = (written == 4) && (s_nrSliceCount[0] == 4) && (s_nrMarkerCount[0] == 4);
	for (r = 0; ok && (r < runs); r++)
	{
		const struct NrTwinSlice *slice = &s_nrSlices[0][r];
		const DR_PSYX_NATIVE *m = &s_nrMarkers[0][r];

		ok = (slice->item == 5u) && (slice->lead == ((r == 0u) ? 1u : 0u)) && ((int)slice->cell == wantCell[r]) && (slice->keyFirst == (100u + runFirst[r])) &&
		     (slice->keyCount == wantCount[r]) && ((r == 0u) || (slice->keyFirst == (s_nrSlices[0][r - 1u].keyFirst + s_nrSlices[0][r - 1u].keyCount))) &&
		     ((m->code[0] >> 24) == (u32)PSYX_NATIVE_CODE) && ((m->code[0] & 0xFFFFFFu) == (NR_SLICE_MARKER | r)) && (m->code[1] == 0u) &&
		     (table[1 + (wantCell[r] - 10)] == CtrGpu_PrimToOTLink24(m));
	}
	for (i = 0; ok && (i < 24); i++)
	{
		const int cell = i - 1 + 10;
		const int linked = (cell == 31) || (cell == 25) || (cell == 12) || (cell == 10);

		ok = linked || (table[i] == (0x00ABC000u + (u32)i));
	}
	s_nrMarkerDb = dbBefore;
	s_nrMarkerCount[0] = markersBefore;
	s_nrSliceCount[0] = slicesBefore;

	// The limits.
	for (i = 0; i <= NR_TWIN_BINS_MAX; i++)
	{
		many[i] = ((u64)(u32)(200 - i) << 40) | (u64)(u32)i;
	}
	ok = ok && NativeRenderLayer_TwinBinsFit(0, 384) && !NativeRenderLayer_TwinBinsFit(0, 385) && !NativeRenderLayer_TwinBinsFit(384, 1) &&
	     NativeRenderLayer_TwinBinsFit(383, 1) &&
	     (NativeTwin_PaintRuns(many, NR_TWIN_BINS_MAX + 1, 0, 1000, runFirst, runCount, runCell, NR_TWIN_BINS_MAX, NULL) == (u32)(NR_TWIN_BINS_MAX + 1)) &&
	     (NativeTwin_PaintRuns(many, NR_TWIN_BINS_MAX, 0, 1000, runFirst, runCount, runCell, NR_TWIN_BINS_MAX, NULL) == (u32)NR_TWIN_BINS_MAX);
	return ok;
}

// THE KEYS AT THE ROUTE ARE THE KEYS OF THE DRAW. The route works the paint
// order of a twin item out with the screen matrix without a draw offset
// (NativeRenderLayer_TwinItemOrder: CharItemMatrix(it, 0, 0)); the draw builds
// it with ofsX + ofsXExtra - 0.5 and ofsY - 0.5 (NativeRenderLayer_DrawCharItem).
// NativeTwin_PaintOrder reads only row w of that matrix (elements 3, 7, 11 and
// 15 of the float matrix of the draw), so the keys are the same when those four
// floats are the same bit for bit. Checked for a near item (shift 2), a far one
// (shift 0) and a mirror item (its own map), each with an odd draw offset;
// rows x and y must move with the offset, or the check would say nothing.
static int NativeDepthCheck_TwinRowW(void)
{
	static struct NrDrawItem it;
	static const s16 mvp[3][3] = {{3812, -410, 1490}, {260, 4021, -705}, {-1530, 640, 3790}};
	const double ofsX = 37.0;
	const double ofsY = -11.0;
	int ok = 1;
	int round;

	for (round = 0; round < 3; round++)
	{
		double S0[4][4];
		double S1[4][4];
		float s0[16];
		float s1[16];
		int r;
		int c;

		memset(&it, 0, sizeof(it));
		memcpy(it.mvp, mvp, sizeof(mvp));
		it.mvpT[0] = -903;
		it.mvpT[1] = 412;
		it.mvpT[2] = (round == 0) ? 0x3a40 : 0x5c21;
		it.mvpShift = (s8)((round == 0) ? 2 : 0);
		it.unitScale[0] = 16384.0 / 5100.0;
		it.unitScale[1] = 16384.0 / 4400.0;
		it.unitScale[2] = 16384.0 / 6300.0;
		it.H = 0x140;
		it.rectW = 512;
		it.rectH = 216;
		it.ofsXExtra = 3;
		it.twin = 1;
		if (round == 2)
		{
			it.mirror = 1;
			for (r = 0; r < 3; r++)
			{
				for (c = 0; c < 3; c++)
				{
					it.mirrorA[r][c] = ((double)mvp[r][c] / 4096.0) * ((c == 1) ? -1.0 : 1.0) * it.unitScale[c];
				}
			}
			it.mirrorB[0] = -903.0;
			it.mirrorB[1] = 1210.5;
			it.mirrorB[2] = 5911.25;
		}

		NativeRenderLayer_CharItemMatrix(&it, 0.0, 0.0, S0);
		NativeRenderLayer_CharItemMatrix(&it, (ofsX + (double)it.ofsXExtra) - 0.5, ofsY - 0.5, S1);
		NativeRenderLayer_MatrixToDraw(S0, s0);
		NativeRenderLayer_MatrixToDraw(S1, s1);
		ok = ok && (memcmp(&s0[3], &s1[3], sizeof(float)) == 0) && (memcmp(&s0[7], &s1[7], sizeof(float)) == 0) &&
		     (memcmp(&s0[11], &s1[11], sizeof(float)) == 0) && (memcmp(&s0[15], &s1[15], sizeof(float)) == 0) && (s0[12] != s1[12]) && (s0[13] != s1[13]);
	}
	return ok;
}

// THE MARKERS OF A TWIN VIEW (NativeRenderLayer_TwinBinMarkers) where no twin
// source is needed - a selftest has none, so NativeRenderLayer_TwinItemOrder
// finds no paint order. Two made-up ranges of the cells 10..31 (a guard word on
// each side), depthOffset 10..31:
// (a) two items without a body (the sides of a water line a selector leaves
//     out): 1, one marker each in the middle cell 20, the first item in
//     otRangeNormal, the second in otRangeSecondary, item fields 0 and 1, no
//     slice, no other word changed;
// (b) an item with a body and no paint order: 0, nothing linked, the key pool
//     at its level before, counted as "no order";
// (c) one item without a body when 384 markers are in use: 0, nothing linked,
//     counted as "arena"; with 383 in use it gets its marker.
// The fallback of an item over NR_TWIN_BINS_MAX cells needs a paint order and
// is not run here. Every state of the layer it touches is put back.
static int NativeDepthCheck_TwinView(void)
{
	static u32 normal[24];
	static u32 second[24];
	static struct InstDrawPerPlayer idpp;
	const int dbBefore = s_nrMarkerDb;
	const int markersBefore = s_nrMarkerCount[0];
	const int slicesBefore = s_nrSliceCount[0];
	const u32 keysBefore = s_nrTwinKeyCount[0];
	const struct NrDrawItem itemsBefore[2] = {s_nrItems[0][0], s_nrItems[0][1]};
	const unsigned long long noOrderBefore = s_nrTwinBins.viewsNoOrder;
	const unsigned long long arenaBefore = s_nrTwinBins.viewsArena;
	const unsigned long long countsBefore = s_nrCount.markersWritten;
	int ok = (NativeCharGpu_TwinSource() == NULL);
	int i;

	if (!NativeGpuLinks_IsRegisteredHostRange(s_nrMarkers[0], sizeof(s_nrMarkers[0])))
	{
		NativeGpuLinks_RegisterRangeChecked("selftest native markers", s_nrMarkers[0], sizeof(s_nrMarkers[0]));
	}
	memset(&idpp, 0, sizeof(idpp));
	idpp.depthOffset[0] = 10;
	idpp.depthOffset[1] = 31;
	idpp.otRangeNormal = (int)(size_t)&normal[1] - (10 * 4);
	idpp.otRangeSecondary = (int)(size_t)&second[1] - (10 * 4);
	s_nrMarkerDb = 0;
	s_nrSliceCount[0] = 0;
	s_nrTwinKeyCount[0] = 7;
	memset(&s_nrItems[0][0], 0, sizeof(s_nrItems[0][0]) * 2u);
	s_nrItems[0][0].twin = 1;
	s_nrItems[0][1].twin = 1;

	// (a)
	for (i = 0; i < 24; i++)
	{
		normal[i] = 0x00ABD000u + (u32)i;
		second[i] = 0x00ABE000u + (u32)i;
	}
	s_nrMarkerCount[0] = 0;
	ok = ok && (NativeRenderLayer_TwinBinMarkers(&idpp, 0, 2) == 1) && (s_nrMarkerCount[0] == 2) && (s_nrSliceCount[0] == 0) &&
	     ((s_nrMarkers[0][0].code[0] & 0xFFFFFFu) == 0u) && ((s_nrMarkers[0][1].code[0] & 0xFFFFFFu) == 1u) &&
	     (normal[1 + (20 - 10)] == CtrGpu_PrimToOTLink24(&s_nrMarkers[0][0])) && (second[1 + (20 - 10)] == CtrGpu_PrimToOTLink24(&s_nrMarkers[0][1]));
	for (i = 0; ok && (i < 24); i++)
	{
		ok = (i == (1 + (20 - 10))) || ((normal[i] == (0x00ABD000u + (u32)i)) && (second[i] == (0x00ABE000u + (u32)i)));
	}

	// (b)
	for (i = 0; i < 24; i++)
	{
		normal[i] = 0x00ABD000u + (u32)i;
		second[i] = 0x00ABE000u + (u32)i;
	}
	s_nrMarkerCount[0] = 0;
	s_nrItems[0][0].bodyOn = 1;
	ok = ok && (NativeRenderLayer_TwinBinMarkers(&idpp, 0, 1) == 0) && (s_nrMarkerCount[0] == 0) && (s_nrSliceCount[0] == 0) && (s_nrTwinKeyCount[0] == 7u) &&
	     (s_nrTwinBins.viewsNoOrder == (noOrderBefore + 1u)) && (normal[1 + (20 - 10)] == (0x00ABD000u + 11u));

	// (c)
	s_nrItems[0][0].bodyOn = 0;
	s_nrMarkerCount[0] = 384;
	ok = ok && (NativeRenderLayer_TwinBinMarkers(&idpp, 0, 1) == 0) && (s_nrMarkerCount[0] == 384) && (s_nrTwinBins.viewsArena == (arenaBefore + 1u)) &&
	     (normal[1 + (20 - 10)] == (0x00ABD000u + 11u));
	s_nrMarkerCount[0] = 383;
	ok = ok && (NativeRenderLayer_TwinBinMarkers(&idpp, 0, 1) == 1) && (s_nrMarkerCount[0] == 384) &&
	     (normal[1 + (20 - 10)] == CtrGpu_PrimToOTLink24(&s_nrMarkers[0][383]));

	s_nrMarkerDb = dbBefore;
	s_nrMarkerCount[0] = markersBefore;
	s_nrSliceCount[0] = slicesBefore;
	s_nrTwinKeyCount[0] = keysBefore;
	s_nrItems[0][0] = itemsBefore[0];
	s_nrItems[0][1] = itemsBefore[1];
	s_nrTwinBins.viewsNoOrder = noOrderBefore;
	s_nrTwinBins.viewsArena = arenaBefore;
	s_nrCount.markersWritten = countsBefore;
	return ok;
}

// THE OWN WHEELS (WHLS version 2, NativeRenderLayer_FillCharWheels), on a fifth
// line. A made-up set (an author's wheel of radius 16 model units at the
// retail points) and a made-up driver: the pose through NativeWheels_Pull (two
// ticks, so it rolls), the item with the view and instance matrices 4096 x I,
// so a map A of a wheel is its frame in kart space times the mesh scale.
//   mirror     steer 0: the -X wheels (the mirrored mesh, P u with P = diag(-1,
//              1, 1)) are the mirror image of the +X ones, A1 = P A0 P and B1 =
//              P B0, up to the wobble of retail (its phases tilt the two sides
//              by up to 2 / 512, DrawTires.c:378-388): within 0.01. The mesh
//              merely turned (the frame of the pose as it is) misses by 2.
//   steer      wheelRotation 64 (22.5 degrees): both front wheels turn the same
//              way - the outer axles of the two are opposite, and the left one
//              is (cos, 0, -sin): within 0.01. A mirrored steering misses by 0.77.
//   size       the rim of a wheel: 16 x wheelSize / 4096 world units.
//   roll       roll step x roll radius = the way of the tick (speed x 32 ms /
//              8192), and with radius 16 the roll step of the retail size.
//   turn       every A a rotation times the scale (determinant > 0): the cull of
//              the body holds for all four.
//   size 0     wheelSize 0 draws no wheel and still hides the retail ones.
// Leaves the pose table empty again.
static int NativeDepthCheck_OwnWheels(double *mirrorError, double *steerError, double *radius, double *rollError, int *proper, int *zeroHeld)
{
	static struct Driver driver;
	static struct Instance inst;
	static struct PushBuffer pb;
	static struct NativeCharGpu gpu;
	static struct NrDrawItem item;
	static const double P[3] = {-1.0, 1.0, 1.0};
	static struct NativeWheelOwn own;
	const double size = (double)0x0ccc / 4096.0;
	const struct NativeWheelPose *pose;
	int pass;
	int r;
	int c;

	*mirrorError = 0.0;
	*steerError = 0.0;
	*radius = 0.0;
	*rollError = 1.0;
	*proper = 0;
	*zeroHeld = 0;

	memset(&driver, 0, sizeof(driver));
	memset(&inst, 0, sizeof(inst));
	memset(&pb, 0, sizeof(pb));
	memset(&gpu, 0, sizeof(gpu));
	for (r = 0; r < 3; r++)
	{
		pb.matrix_ViewProj.m[r][r] = 0x1000;
		inst.matrix.m[r][r] = 0x1000;
	}
	inst.scale.x = 0x0ccc;
	inst.scale.y = 0x0ccc;
	inst.scale.z = 0x0ccc;
	gpu.hasWheels = 1;
	gpu.wheelOwn = 1;
	gpu.wheelRadius = 16.0f;
	gpu.wheelHalfWidth = 6.0f;
	gpu.wheelFront[0] = 36.0f;
	gpu.wheelFront[1] = 16.0f;
	gpu.wheelFront[2] = 49.75f;
	gpu.wheelRear[0] = 36.0f;
	gpu.wheelRear[1] = 16.0f;
	gpu.wheelRear[2] = -24.0f;

	for (pass = 0; pass < 3; pass++)
	{
		NativeWheels_Forget();
		driver.wheelSize = (pass == 2) ? 0 : 0x0ccc;
		driver.wheelRotation = (pass == 1) ? 64 : 0;
		driver.speedApprox = (pass == 0) ? 3000 : 0;
		driver.hazardTimer = 0;
		own.radius[0] = (double)gpu.wheelRadius;
		own.radius[1] = (double)gpu.wheelRadius;
		own.treads[0] = 0;
		own.treads[1] = 0;
		own.rearOwn = 0;
		NativeWheels_Pull(0, &driver, &inst, 1, 1u, 32, 0, &own);
		NativeWheels_Pull(0, &driver, &inst, 1, 2u, 32, 0, &own);
		pose = NativeWheels_PoseOf(&inst);
		if (pose == NULL)
		{
			NativeWheels_Forget();
			return 0;
		}

		memset(&item, 0, sizeof(item));
		item.gpu = &gpu;
		item.mvpShift = 0;
		item.mvpT[2] = 2000;
		item.wheelMask = NR_WHEEL_MASK_ALL;
		for (r = 0; r < 3; r++)
		{
			item.mvp[r][r] = 0x1000;
			item.unitScale[r] = 1.0;
		}
		NativeRenderLayer_FillCharWheels(&item, &inst, &pb);

		if (pass == 2)
		{
			*zeroHeld = item.nativeWheels && (item.wheelMask == 0);
			continue;
		}
		if (!item.nativeWheels)
		{
			NativeWheels_Forget();
			return 0;
		}

		if (pass == 0)
		{
			int pair;
			int wheel;

			// The mirror: A1 = P A0 P, B1 = P B0 (front and rear pair).
			for (pair = 0; pair < 2; pair++)
			{
				const int left = pair * 2;
				const int right = left + 1;

				for (r = 0; r < 3; r++)
				{
					const double eb = fabs(item.wheelB[right][r] - (P[r] * item.wheelB[left][r]));

					*mirrorError = (eb > *mirrorError) ? eb : *mirrorError;
					for (c = 0; c < 3; c++)
					{
						const double ea = fabs(item.wheelA[right][r][c] - (P[r] * item.wheelA[left][r][c] * P[c])) / size;

						*mirrorError = (ea > *mirrorError) ? ea : *mirrorError;
					}
				}
			}

			// The size and the roll.
			*radius = 16.0 * sqrt((item.wheelA[0][0][1] * item.wheelA[0][0][1]) + (item.wheelA[0][1][1] * item.wheelA[0][1][1]) +
			                      (item.wheelA[0][2][1] * item.wheelA[0][2][1]));
			*rollError = fabs((pose->rollStep * NativeWheels_RollRadius(pose)) - ((3000.0 * 32.0) / 8192.0)) +
			             fabs(pose->rollStep - ((3000.0 * 32.0) / (32.0 * (double)0x0ccc)));

			// Every map a rotation times the scale.
			for (wheel = 0; wheel < NATIVE_WHEELS_COUNT; wheel++)
			{
				double(*A)[3] = item.wheelA[wheel];
				const double det = (A[0][0] * ((A[1][1] * A[2][2]) - (A[1][2] * A[2][1]))) - (A[0][1] * ((A[1][0] * A[2][2]) - (A[1][2] * A[2][0]))) +
				                   (A[0][2] * ((A[1][0] * A[2][1]) - (A[1][1] * A[2][0])));

				*proper += (det > 0.0) ? 1 : 0;
			}
		}
		else
		{
			// The steer: the outer axle of wheel 0 is A0 (1, 0, 0), of wheel 1
			// A1 (-1, 0, 0) (the outside of the mirrored mesh); opposite, and
			// the left one yawed by 22.5 degrees about +y.
			const double s = (64.0 * 4.0) * (6.283185307179586 / 4096.0);
			const double expect[3] = {cos(s), 0.0, -sin(s)};
			int wheel;

			for (r = 0; r < 3; r++)
			{
				const double outLeft = item.wheelA[0][r][0] / size;
				const double outRight = -item.wheelA[1][r][0] / size;
				const double e1 = fabs(outLeft + outRight);
				const double e2 = fabs(outLeft - expect[r]);

				*steerError = (e1 > *steerError) ? e1 : *steerError;
				*steerError = (e2 > *steerError) ? e2 : *steerError;
			}
			for (wheel = 0; wheel < NATIVE_WHEELS_COUNT; wheel++)
			{
				double(*A)[3] = item.wheelA[wheel];
				const double det = (A[0][0] * ((A[1][1] * A[2][2]) - (A[1][2] * A[2][1]))) - (A[0][1] * ((A[1][0] * A[2][2]) - (A[1][2] * A[2][0]))) +
				                   (A[0][2] * ((A[1][0] * A[2][1]) - (A[1][1] * A[2][0])));

				*proper += (det > 0.0) ? 1 : 0;
			}
		}
	}

	NativeWheels_Forget();
	return 1;
}

// WIN AND LOSE (render plan B3, NATIVE_CHAR_GPU_FINISH_STAGES), on a seventh
// line. A made-up native part of 4 points and 47 poses with an MRPH section
// (version 1 layout: win and lose, no normals, base pose 10) in memory:
//   layout   win and lose get 8 stages each behind the 47 poses (first 47
//            and 55, 63 poses in all); without MRPH, or for the preview and
//            the twin (withFinish 0), none.
//   stages   every point of stage s of a target is pose 10 + (s + 1) / 8 x
//            its delta (float), its UV and colour those of pose 10; pose 10
//            itself as before.
//   rule     ranks 0..2 win, 3..7 lose, -1 and 8 none; ticks 0, 1, 2, 15, 16,
//            1000 give the stages 0, 0, 1, 7, 7, 7; a set with win alone has
//            no pose for lose.
// Returns the failures (0 = passed).
static void NativeDepthCheck_PutF32(u8 *at, float value)
{
	u32 bits;

	memcpy(&bits, &value, sizeof(bits));
	at[0] = (u8)(bits & 0xffu);
	at[1] = (u8)((bits >> 8) & 0xffu);
	at[2] = (u8)((bits >> 16) & 0xffu);
	at[3] = (u8)((bits >> 24) & 0xffu);
}

static int NativeDepthCheck_Finish(int *checks, double *errorMax)
{
	enum
	{
		FN = 4,
		FP = 47
	};
	static u8 poses[FP * FN * 24];
	static u8 uv[FN * 8];
	static u8 colors[FN * 4];
	static u8 morph[0x10 + (2 * FN * 12)];
	static struct RldCharNative n;
	static struct NativeCharGpu set;
	static struct NativeProbeVertex out[FN];
	static struct NativeProbeVertex base[FN];
	static const int rankTarget[10] = {-1, 0, 0, 0, 1, 1, 1, 1, 1, -1}; // ranks -1..8
	static const u32 ticks[6] = {0u, 1u, 2u, 15u, 16u, 1000u};
	static const int ticksStage[6] = {0, 0, 1, 7, 7, 7};
	u32 targets = 0;
	u32 first[NATIVE_CHAR_GPU_FINISH_TARGETS];
	u32 total = 0;
	int failures = 0;
	u32 p;
	u32 v;
	int c;
	int target;
	int stage;

	*checks = 0;
	*errorMax = 0.0;
	memset(&n, 0, sizeof(n));
	memset(&set, 0, sizeof(set));
	for (p = 0; p < FP; p++)
	{
		for (v = 0; v < FN; v++)
		{
			u8 *at = &poses[((p * FN) + v) * 24u];

			for (c = 0; c < 3; c++)
			{
				NativeDepthCheck_PutF32(&at[c * 4], (float)((int)(p * 10u) + (int)(v * 3u) + c) * 0.25f);
				NativeDepthCheck_PutF32(&at[12 + (c * 4)], (c == 1) ? 1.0f : 0.0f);
			}
		}
	}
	for (v = 0; v < FN; v++)
	{
		NativeDepthCheck_PutF32(&uv[v * 8u], 0.125f * (float)v);
		NativeDepthCheck_PutF32(&uv[(v * 8u) + 4u], 1.0f - (0.125f * (float)v));
		colors[(v * 4u) + 0u] = (u8)(10u + v);
		colors[(v * 4u) + 1u] = (u8)(20u + v);
		colors[(v * 4u) + 2u] = (u8)(30u + v);
		colors[(v * 4u) + 3u] = 255u;
	}
	memset(morph, 0, sizeof(morph));
	morph[0x00] = 3u;       // win and lose
	morph[0x04] = (u8)FN;   // vertexCount
	morph[0x08] = 10u;      // basePose
	for (target = 0; target < 2; target++)
	{
		for (v = 0; v < FN; v++)
		{
			for (c = 0; c < 3; c++)
			{
				NativeDepthCheck_PutF32(&morph[0x10 + (((u32)target * FN) + v) * 12u + ((u32)c * 4u)],
				                        (target == 0) ? (2.0f + (float)v + (float)c) : -(1.0f + (0.5f * (float)c)));
			}
		}
	}
	n.state = RLDCHAR_NATIVE_READY;
	n.vertexCount = FN;
	n.poseCount = FP;
	n.poses = poses;
	n.uv = uv;
	n.colors = colors;

	// Without MRPH: no stages.
	NativeCharGpu_FinishLayout(&n, FP, 1, &targets, first, &total);
	failures += ((targets == 0u) && (total == FP)) ? 0 : 1;
	(*checks)++;

	n.morph = morph;
	n.morphTargets = 3u;
	n.morphNormals = 0u;
	n.morphBasePose = 10u;

	// The preview and the twin: none.
	NativeCharGpu_FinishLayout(&n, FP, 0, &targets, first, &total);
	failures += ((targets == 0u) && (total == FP)) ? 0 : 1;
	(*checks)++;

	// A seat.
	NativeCharGpu_FinishLayout(&n, FP, 1, &targets, first, &total);
	failures += ((targets == 3u) && (first[0] == 47u) && (first[1] == 55u) && (total == 63u)) ? 0 : 1;
	(*checks)++;

	// The stages.
	NativeCharGpu_PoseVertices(&n, 10u, 0u, FN, base);
	NativeCharGpu_BufferVertices(&n, FP, first, targets, 10u, 0u, FN, out);
	failures += (memcmp(out, base, sizeof(out)) == 0) ? 0 : 1;
	(*checks)++;
	for (target = 0; target < 2; target++)
	{
		for (stage = 0; stage < NATIVE_CHAR_GPU_FINISH_STAGES; stage++)
		{
			const double t = (double)(stage + 1) / (double)NATIVE_CHAR_GPU_FINISH_STAGES;
			int ok = 1;

			NativeCharGpu_BufferVertices(&n, FP, first, targets, first[target] + (u32)stage, 0u, FN, out);
			for (v = 0; v < FN; v++)
			{
				for (c = 0; c < 3; c++)
				{
					const double delta = (target == 0) ? (2.0 + (double)v + (double)c) : -(1.0 + (0.5 * (double)c));
					const double e = fabs((double)out[v].position[c] - ((double)base[v].position[c] + (t * delta)));

					*errorMax = (e > *errorMax) ? e : *errorMax;
					ok = ok && (e < 1e-4);
				}
				ok = ok && (out[v].texcoord[0] == base[v].texcoord[0]) && (out[v].texcoord[1] == base[v].texcoord[1]) &&
				     (memcmp(out[v].color, base[v].color, sizeof(out[v].color)) == 0);
			}
			failures += ok ? 0 : 1;
			(*checks)++;
		}
	}

	// The rule.
	for (c = 0; c < 10; c++)
	{
		failures += (NativeCharGpu_FinishTarget(c - 1) == rankTarget[c]) ? 0 : 1;
		(*checks)++;
	}
	for (c = 0; c < 6; c++)
	{
		failures += (NativeCharGpu_FinishStage(ticks[c]) == ticksStage[c]) ? 0 : 1;
		(*checks)++;
	}
	// The races with a placing: arcade and versus; battle, crystal challenge,
	// boss, adventure, relic, token race and time trial not.
	{
		static const u32 modes1[9] = {ARCADE_MODE, 0u, BATTLE_MODE, ARCADE_MODE | CRYSTAL_CHALLENGE, ADVENTURE_MODE | ADVENTURE_BOSS, ADVENTURE_MODE,
		                              RELIC_RACE, ADVENTURE_MODE, TIME_TRIAL};
		static const u32 modes2[9] = {0u, 0u, 0u, 0u, 0u, 0u, 0u, TOKEN_RACE, 0u};
		static const int allowed[9] = {1, 1, 0, 0, 0, 0, 0, 0, 0};

		for (c = 0; c < 9; c++)
		{
			failures += ((NativeCharGpu_FinishModeRefusal(modes1[c], modes2[c]) == NULL) == (allowed[c] != 0)) ? 0 : 1;
			(*checks)++;
		}
	}
	set.finishTargets = 1u << NATIVE_CHAR_GPU_FINISH_WIN;
	set.finishFirst[NATIVE_CHAR_GPU_FINISH_WIN] = 47u;
	failures += ((NativeCharGpu_FinishPose(&set, NATIVE_CHAR_GPU_FINISH_WIN, 7) == 54) && (NativeCharGpu_FinishPose(&set, NATIVE_CHAR_GPU_FINISH_LOSE, 0) == -1) &&
	             (NativeCharGpu_FinishPose(&set, NATIVE_CHAR_GPU_FINISH_WIN, 8) == -1))
	                ? 0
	                : 1;
	(*checks)++;
	return failures;
}

// THE OWN WHEELS OF WHLS VERSION 3 (render plan A1, A2, A4), on an eighth
// line. A made-up native part (3 points, 1 triangle, 2 materials) with a
// front wheel of 8 points at radius 16 (6 triangles) and, as version 3, a
// rear wheel of its own: 10 points evenly round at radius 24 (8 triangles,
// material 1) behind the WHLS head (n->wheelExtra), built without a device:
//   v2       the wheel as before: 2 x 8 points, 6 x 6 indices, the mirror at
//            18, no rear mesh, not always drawn.
//   v3 rear  the rear mesh behind: points 16..35 (its mirror image x negated),
//            its range at 36 and its mirror at 60, 24 indices each, all of
//            them at 16 or more; material 1, radius 24, treads 10; ALWAYS_DRAW
//            read. Without the rear mesh flag: the front mesh as in v2.
//   roll     front radius 16, rear 24: the rear roll step is 16 / 24 of the
//            front's; with the rear the front's, the same.
//   level    the retail tyre threshold: 1 player lodIndex 2 shown, 3 hidden;
//            3 players lodIndex 0 shown, 1 hidden; ALWAYS_DRAW shown at
//            lodIndex 5 - with NO LOD off; with it on (the default, which
//            forces the retail tyres too) all five shown.
// Returns the failures.
static void NativeDepthCheck_PutWheelVertex(u8 *at, double radius, double angle, double x)
{
	NativeDepthCheck_PutF32(&at[0], (float)x);
	NativeDepthCheck_PutF32(&at[4], (float)(radius * cos(angle)));
	NativeDepthCheck_PutF32(&at[8], (float)(radius * sin(angle)));
	NativeDepthCheck_PutF32(&at[12], (x > 0.0) ? 1.0f : -1.0f);
	NativeDepthCheck_PutF32(&at[16], 0.0f);
	NativeDepthCheck_PutF32(&at[20], 0.0f);
	NativeDepthCheck_PutF32(&at[24], (float)(angle / 6.283185307179586));
	NativeDepthCheck_PutF32(&at[28], (x > 0.0) ? 0.0f : 1.0f);
}

static int NativeDepthCheck_WheelsV3(int *checks)
{
	enum
	{
		NW = 8,
		TW = 6,
		NR = 10,
		TR = 8
	};
	static u8 poses[3 * 24];
	static u8 materials[2 * 16];
	static u8 indices[3 * 2];
	static u8 triangleMaterials[2];
	static u8 wheel[0x30 + (NW * 32) + (TW * 6) + 2];
	static u8 extra[0x30 + (NR * 32) + (TR * 6)];
	static struct RldCharNative n;
	static struct NativeCharGpuCpu cpu;
	static struct NativeCharGpu set;
	static struct NrDrawItem item;
	static struct InstDrawPerPlayer idpp;
	static struct GameTracker tracker;
	static struct Driver driver;
	static struct Instance inst;
	struct NativeWheelOwn own;
	const struct NativeWheelPose *pose;
	int failures = 0;
	int pass;
	u32 i;

	*checks = 0;
	memset(poses, 0, sizeof(poses));
	memset(materials, 0, sizeof(materials));
	memset(wheel, 0, sizeof(wheel));
	memset(extra, 0, sizeof(extra));
	for (i = 0; i < 2u; i++)
	{
		materials[(i * 16u) + 0u] = 200u;
		materials[(i * 16u) + 1u] = 200u;
		materials[(i * 16u) + 2u] = 200u;
		materials[(i * 16u) + 3u] = 255u;
		materials[(i * 16u) + 4u] = 0xffu; // texture -1
		materials[(i * 16u) + 5u] = 0xffu;
	}
	indices[0] = 0u;
	indices[2] = 1u;
	indices[4] = 2u;
	triangleMaterials[0] = 0u;
	triangleMaterials[1] = 0u;
	NativeDepthCheck_PutF32(&poses[24], 1.0f);
	NativeDepthCheck_PutF32(&poses[48 + 4], 1.0f);

	// WHLS head: the front material 0, radius 16, half width 6, the middles.
	NativeDepthCheck_PutF32(&wheel[0x0C], 16.0f);
	NativeDepthCheck_PutF32(&wheel[0x10], 6.0f);
	NativeDepthCheck_PutF32(&wheel[0x14], 40.0f);
	NativeDepthCheck_PutF32(&wheel[0x18], 18.0f);
	NativeDepthCheck_PutF32(&wheel[0x1C], 52.0f);
	NativeDepthCheck_PutF32(&wheel[0x20], 44.0f);
	NativeDepthCheck_PutF32(&wheel[0x24], 24.0f);
	NativeDepthCheck_PutF32(&wheel[0x28], -30.0f);
	for (i = 0; i < (u32)NW; i++)
	{
		NativeDepthCheck_PutWheelVertex(&wheel[0x30 + (i * 32u)], 16.0, (6.283185307179586 * (double)(i / 2u)) / 4.0, ((i & 1u) != 0u) ? -6.0 : 6.0);
	}
	for (i = 0; i < (u32)(TW * 3); i++)
	{
		wheel[0x30 + (NW * 32) + (i * 2u)] = (u8)((i * 3u) % (u32)NW);
	}
	// The rear mesh: material 1, radius 24, half width 8.
	extra[0x20] = 1u;
	NativeDepthCheck_PutF32(&extra[0x24], 24.0f);
	NativeDepthCheck_PutF32(&extra[0x28], 8.0f);
	for (i = 0; i < (u32)NR; i++)
	{
		NativeDepthCheck_PutWheelVertex(&extra[0x30 + (i * 32u)], 24.0, (6.283185307179586 * (double)i) / (double)NR, 6.0);
	}
	for (i = 0; i < (u32)(TR * 3); i++)
	{
		extra[0x30 + (NR * 32) + (i * 2u)] = (u8)((i * 7u) % (u32)NR);
	}

	memset(&n, 0, sizeof(n));
	n.state = RLDCHAR_NATIVE_READY;
	n.vertexCount = 3u;
	n.triangleCount = 1u;
	n.indexSize = 2u;
	n.materialCount = 2u;
	n.poseCount = 0u;
	n.poses = poses;
	n.indices = indices;
	n.triangleMaterials = triangleMaterials;
	n.materials = materials;
	n.wheel = wheel;
	n.wheelVertexCount = NW;
	n.wheelTriangleCount = TW;

	for (pass = 0; pass < 3; pass++)
	{
		n.wheelVersion = (pass == 0) ? 2u : 3u;
		n.wheelFlags = (pass == 0) ? 0u : ((pass == 1) ? (RLDCHAR_WHEEL_ALWAYS_DRAW | RLDCHAR_WHEEL_REAR_MESH) : RLDCHAR_WHEEL_ALWAYS_DRAW);
		n.wheelExtra = (pass == 1) ? extra : NULL;
		n.wheelRearVertexCount = (pass == 1) ? (u32)NR : 0u;
		n.wheelRearTriangleCount = (pass == 1) ? (u32)TR : 0u;
		if (!NativeCharGpu_Build(&n, &cpu))
		{
			NativeCharGpu_FreeCpu(&cpu);
			failures++;
			(*checks)++;
			continue;
		}
		failures += ((cpu.wheelOwn == 1u) && (cpu.wheelMirrorFirst == (u32)(TW * 3)) && (cpu.wheelIndexCount == (u32)(TW * 3)) &&
		             (cpu.wheelAlways == ((pass == 0) ? 0u : 1u)))
		                ? 0
		                : 1;
		(*checks)++;
		if (pass != 1)
		{
			failures += ((cpu.wheelRearOwn == 0u) && (cpu.wheelVertexCount == (u32)(2 * NW)) && (cpu.wheelIndexTotal == (u32)(6 * TW))) ? 0 : 1;
			(*checks)++;
		}
		else
		{
			int ok = (cpu.wheelRearOwn == 1u) && (cpu.wheelVertexCount == (u32)((2 * NW) + (2 * NR))) &&
			         (cpu.wheelIndexTotal == (u32)((6 * TW) + (6 * TR))) && (cpu.wheelRearFirst == (u32)(6 * TW)) &&
			         (cpu.wheelRearMirrorFirst == (u32)((6 * TW) + (3 * TR))) && (cpu.wheelRearIndexCount == (u32)(3 * TR)) &&
			         (cpu.wheelRearMaterial == 1u) && (cpu.wheelRearRadius == 24.0f) && (cpu.wheelRearHalfWidth == 8.0f) && (cpu.wheelRearTreads == 10) &&
			         cpu.wheelRearTreadsEstimated;

			(*checks)++;
			for (i = 0; ok && (i < (u32)(6 * TR)); i++)
			{
				ok = cpu.wheelIndices[(6 * TW) + i] >= (u16)(2 * NW);
			}
			for (i = 0; ok && (i < (u32)NR); i++)
			{
				const struct NativeProbeVertex *a = &cpu.wheelVertices[(2 * NW) + i];
				const struct NativeProbeVertex *b = &cpu.wheelVertices[(2 * NW) + NR + i];

				ok = (a->position[0] == 6.0f) && (b->position[0] == -6.0f) && (a->position[1] == b->position[1]) && (a->position[2] == b->position[2]);
			}
			failures += ok ? 0 : 1;
		}
		NativeCharGpu_FreeCpu(&cpu);
	}

	// The roll per axle.
	memset(&driver, 0, sizeof(driver));
	memset(&inst, 0, sizeof(inst));
	driver.wheelSize = 0x0ccc;
	driver.speedApprox = 1000;
	for (pass = 0; pass < 2; pass++)
	{
		NativeWheels_Forget();
		own.radius[0] = 16.0;
		own.radius[1] = (pass == 0) ? 24.0 : 16.0;
		own.treads[0] = 0;
		own.treads[1] = 0;
		own.rearOwn = (pass == 0);
		NativeWheels_Pull(0, &driver, &inst, 1, 1u, 32, 0, &own);
		NativeWheels_Pull(0, &driver, &inst, 1, 2u, 32, 0, &own);
		pose = NativeWheels_PoseOf(&inst);
		failures += ((pose != NULL) && (pose->rollStep != 0.0) &&
		             (fabs(pose->rollStepRear - (pose->rollStep * (own.radius[0] / own.radius[1]))) < 1e-12) &&
		             ((pass == 0) || ((pose->rollStepRear == pose->rollStep) && (pose->rollRear == pose->roll))))
		                ? 0
		                : 1;
		(*checks)++;
	}
	NativeWheels_Forget();

	// The level of detail.
	{
		static const int players[5] = {1, 1, 3, 3, 1};
		static const int lod[5] = {2, 3, 0, 1, 5};
		static const int always[5] = {0, 0, 0, 0, 1};
		static const int past[5] = {0, 1, 0, 1, 0};
		int c;

		const int noneBefore = CTR_Lod_NoneMode();

		memset(&set, 0, sizeof(set));
		set.hasWheels = 1;
		set.wheelOwn = 1;
		// Both settings of NO LOD (its tyre answer is the mode), the one of the
		// run restored after.
		for (c = 0; c < 10; c++)
		{
			int shown;

			CTR_Lod_SetNoneMode(c >= 5);
			shown = !past[c % 5] || CTR_Lod_TiresForced();

			memset(&item, 0, sizeof(item));
			memset(&idpp, 0, sizeof(idpp));
			memset(&tracker, 0, sizeof(tracker));
			set.wheelAlways = (u8)always[c % 5];
			item.gpu = &set;
			item.nativeWheels = 1;
			item.wheelMask = NR_WHEEL_MASK_ALL;
			idpp.lodIndex = lod[c % 5];
			tracker.numPlyrCurrGame = (u8)players[c % 5];
			NativeRenderLayer_CharWheelLod(&item, &idpp, &tracker);
			failures += ((item.wheelMask == (shown ? NR_WHEEL_MASK_ALL : 0)) && (item.nativeWheels == 1)) ? 0 : 1;
			(*checks)++;
		}
		CTR_Lod_SetNoneMode(noneBefore);
	}
	return failures;
}

int NativeDepthCheck_Run(void)
{
	static struct NativeDepthCheckPair normal;
	static struct NativeDepthCheckPair huge;
	static struct NativeDepthCheckPair charEven;
	static struct NativeDepthCheckPair charOdd;
	static struct NativeDepthCheckSplit splitNear;
	static struct NativeDepthCheckSplit splitFar;
	static struct NativeDepthCheckSplit splitP;
	static struct NativeDepthCheckSides sides;
	static struct NativeDepthCheckSplit mirrorNear;
	static struct NativeDepthCheckSplit mirrorFar;
	static struct NativeDepthCheckSplit mirrorSpecial;
	double wheelError;
	int mirrorPassed;
	static struct NativeDepthCheckSplit splitPitched;
	int itemsHeld;
	int wheelsCompared = 0;
	int wheelsDiffer;
	int classCases = 0;
	int classesDiffer;
	int itemsPassed;
	int binMiddle;
	int binNearest;
	int twinCells[4] = {0, 0, 0, 0};
	int twinHeld = 0;
	int twinPassed;
	int twinRowW;
	int twinView;
	int held;
	int passed;
	int charPassed;
	int splitPassed;
	int ruleCases = 0;
	int ruleFailed;
	double ownMirror;
	double ownSteer;
	double ownRadius;
	double ownRoll;
	int ownProper;
	int ownZero;
	int ownRan;
	int ownPassed;
	int strobePassed;
	char strobeLine[1024];
	int finishChecks;
	double finishError;
	int finishFailures;
	int v3Checks;
	int v3Failures;
	unsigned long long lodBefore[3];

	// The queue writes its scratch words; outside a game run the scratchpad
	// has to be set up first.
	Platform_InitScratchpad();

	NativeDepthCheck_Pair(0, &normal);
	NativeDepthCheck_Pair(1, &huge);
	NativeDepthCheck_CharPair(0x1000, &charEven);
	NativeDepthCheck_CharPair(0x0ccd, &charOdd);
	NativeDepthCheck_Split(0x0800, 1, 0x1800, 0x1400, SPLIT_LINE, 0, &splitNear);
	NativeDepthCheck_Split(0x2000, 1, 0x1800, 0x1400, SPLIT_LINE, 0, &splitFar);
	NativeDepthCheck_Split(0x0800, 0, 0x1000, 0x1000, SPLIT_LINE, 0, &splitP);
	NativeDepthCheck_Split(0x0800, 1, 0x1800, 0x1400, REFLECTIVE, 0, &mirrorNear);
	NativeDepthCheck_Split(0x2000, 1, 0x1800, 0x1400, REFLECTIVE, 0, &mirrorFar);
	NativeDepthCheck_Split(0x0800, 1, 0x1800, 0x1400, SPLIT_LINE, 1, &mirrorSpecial);
	wheelError = NativeDepthCheck_MirrorWheels();
	NativeDepthCheck_Split(0x0800, 2, 0x1800, 0x1400, SPLIT_LINE, 0, &splitPitched);
	itemsHeld = NativeDepthCheck_SplitItems();
	wheelsDiffer = NativeDepthCheck_WheelBelow(&wheelsCompared);
	classesDiffer = NativeDepthCheck_ShiftMaskClasses(&classCases);
	// The bin of a marker (K-A): the middle of depthOffset, or its nearest end
	// for the mirror of the twin.
	{
		static struct InstDrawPerPlayer binView;

		memset(&binView, 0, sizeof(binView));
		binView.depthOffset[0] = 10;
		binView.depthOffset[1] = 31;
		binMiddle = NativeRenderLayer_MarkerBin(&binView, 0);
		binNearest = NativeRenderLayer_MarkerBin(&binView, 1);
	}
	twinPassed = NativeDepthCheck_TwinBins(twinCells, &twinHeld);
	twinRowW = NativeDepthCheck_TwinRowW();
	twinView = NativeDepthCheck_TwinView();
	twinPassed = twinPassed && twinRowW && twinView;
	NativeDepthCheck_Sides(&sides);
	ruleFailed = NativeDepthCheck_RuleFactor(&ruleCases);
	ownRan = NativeDepthCheck_OwnWheels(&ownMirror, &ownSteer, &ownRadius, &ownRoll, &ownProper, &ownZero);
	ownPassed = ownRan && (ownMirror < 0.01) && (ownSteer < 0.01) && (fabs(ownRadius - (16.0 * (double)0x0ccc / 4096.0)) < 1e-9) && (ownRoll < 1e-9) &&
	            (ownProper == 8) && ownZero;
	strobePassed = NativeWheels_StrobeSelfTest(strobeLine, sizeof(strobeLine));
	finishFailures = NativeDepthCheck_Finish(&finishChecks, &finishError);
	lodBefore[0] = s_nrCharCnt.lodShown;
	lodBefore[1] = s_nrCharCnt.lodHidden;
	lodBefore[2] = s_nrCharCnt.lodAlways;
	v3Failures = NativeDepthCheck_WheelsV3(&v3Checks);
	s_nrCharCnt.lodShown = lodBefore[0];
	s_nrCharCnt.lodHidden = lodBefore[1];
	s_nrCharCnt.lodAlways = lodBefore[2];

	held = normal.nearPoint.shiftHeld && normal.farPoint.shiftHeld && huge.nearPoint.shiftHeld && huge.farPoint.shiftHeld;
	charPassed = NativeDepthCheck_PairPassed(&charEven, 2, 0) && NativeDepthCheck_PairPassed(&charOdd, 2, 0);
	splitPassed = NativeDepthCheck_SplitPassed(&splitNear, 1, 1) && NativeDepthCheck_SplitPassed(&splitFar, 1, 0) &&
	              NativeDepthCheck_SplitPassed(&splitP, 0, 1) && (sides.differ == 0) && (sides.cases == 20) && (sides.shiftMaskError <= 0.5) &&
	              (sides.dimError <= 1.25) && sides.labelsHeld && (ruleFailed == 0);
	mirrorPassed = NativeDepthCheck_MirrorPassed(&mirrorNear, RB_RETAIL_DRAWFUNC_REFLECTION, NR_SELECTOR_NEGATIVE) &&
	               NativeDepthCheck_MirrorPassed(&mirrorFar, RB_RETAIL_DRAWFUNC_REFLECTION, NR_SELECTOR_NEGATIVE) &&
	               NativeDepthCheck_MirrorPassed(&mirrorSpecial, RB_RETAIL_DRAWFUNC_SPECIAL, NR_SELECTOR_BOTH_MASK) && (wheelError < 1e-6) &&
	               (sides.reflectionDiffer == 0) && (sides.reflectionCases == 5);
	itemsPassed = NativeDepthCheck_SplitPassed(&splitPitched, 1, 1) && itemsHeld && (wheelsCompared >= 600) && (wheelsDiffer == 0) && (classesDiffer == 0) &&
	              (binMiddle == 20) && (binNearest == 10);
	passed = NativeDepthCheck_PairPassed(&normal, 2, 0) && NativeDepthCheck_PairPassed(&huge, 0, -2) && charPassed && splitPassed && mirrorPassed &&
	         itemsPassed && twinPassed && ownPassed && strobePassed && (finishFailures == 0) && (v3Failures == 0);

	printf("native depth selftest %s: normal shift %d to %d, w %.4f to %.4f, step %.4f percent, rest %.4f percent, matrix part %.4f percent, "
	       "wrong shift %.1f and %.1f percent, matrix %.1f and %.1f percent, huge shift %d to %d, w %.4f to %.4f, step %.4f percent, rest %.4f percent, "
	       "matrix part %.4f percent, wrong shift %.1f and %.1f percent, matrix %.1f and %.1f percent, shift checks %s",
	       passed ? "passed" : "differs", (int)normal.nearPoint.item.mvpShift, (int)normal.farPoint.item.mvpShift, normal.nearPoint.w, normal.farPoint.w,
	       normal.step, normal.rest, normal.matrixPart, normal.wrongNear, normal.wrongFar, normal.matrixWrongNear, normal.matrixWrongFar,
	       (int)huge.nearPoint.item.mvpShift, (int)huge.farPoint.item.mvpShift, huge.nearPoint.w, huge.farPoint.w, huge.step, huge.rest,
	       huge.matrixPart, huge.wrongNear, huge.wrongFar, huge.matrixWrongNear, huge.matrixWrongFar, held ? "held" : "off");

	// The char way (step 4c), appended: the part above is the line of before.
	{
		const struct NativeDepthCheckPair *pairs[2] = {&charEven, &charOdd};
		const int scales[2] = {0x1000, 0x0ccd};
		int i;

		for (i = 0; i < 2; i++)
		{
			const struct NativeDepthCheckPair *c = pairs[i];

			printf(", char scale 0x%04x shift %d to %d, w %.4f to %.4f, step %.4f percent, rest %.4f percent, matrix part %.4f percent, "
			       "wrong shift %.1f and %.1f percent, matrix %.1f and %.1f percent",
			       scales[i], (int)c->nearPoint.item.mvpShift, (int)c->farPoint.item.mvpShift, c->nearPoint.w, c->farPoint.w, c->step, c->rest,
			       c->matrixPart, c->wrongNear, c->wrongFar, c->matrixWrongNear, c->matrixWrongFar);
		}
		printf(", char %s", charPassed ? "passed" : "differs");
	}

	// The water line (step 4e, stage a), appended: the part above is the line
	// of before.
	{
		const struct NativeDepthCheckSplit *cases[3] = {&splitNear, &splitFar, &splitP};
		const char *names[3] = {"R near", "R far", "P near"};
		int i;

		for (i = 0; i < 3; i++)
		{
			const struct NativeDepthCheckSplit *sp = cases[i];

			printf(", split %s: handler %s, selector %s, composed %s, splitLine %d, %d points (%d below, %d above), %d differ, level plane %d differ",
			       names[i], sp->handlerSplit ? "split" : "other", sp->selectorBoth ? "both-mask" : "other", sp->composedSame ? "same" : "off",
			       sp->splitLine, sp->compared, sp->below, sp->above, sp->differ, sp->levelDiffer);
		}
		printf(", selector sides %d cases %d differ, shift/mask error max %.2f, dim error max %.2f, labels %s, factor rule %d cases %d differ, "
		       "split %s\n",
		       sides.cases, sides.differ, sides.shiftMaskError, sides.dimError, sides.labelsHeld ? "held" : "off", ruleCases, ruleFailed,
		       splitPassed ? "passed" : "differs");
	}

	// The mirror (step 4e, stage b), on a line of its own.
	{
		const struct NativeDepthCheckSplit *cases[3] = {&mirrorNear, &mirrorFar, &mirrorSpecial};
		const char *names[3] = {"reflection R near", "reflection R far", "special"};
		int i;

		printf("native depth selftest mirror");
		for (i = 0; i < 3; i++)
		{
			const struct NativeDepthCheckSplit *sp = cases[i];

			printf(", %s: handler %s, selector %s, splitLine %d, %d corners, error max %.3f, winding %s", names[i],
			       (sp->drawFunc == RB_RETAIL_DRAWFUNC_REFLECTION) ? "reflection" : ((sp->drawFunc == RB_RETAIL_DRAWFUNC_SPECIAL) ? "special" : "other"),
			       NativeRenderLayer_SelectorName(sp->selectorIndex), sp->splitLine, sp->mirrorCompared, sp->mirrorError,
			       sp->cullFlipped ? "turned" : "kept");
		}
		printf(", wheels error max %.9f, reflection selectors %d cases %d differ, mirror %s\n", wheelError, sides.reflectionCases, sides.reflectionDiffer,
		       mirrorPassed ? "passed" : "differs");
	}

	// The checks of the review of stage a (G4ea S1-S3), on a third line.
	printf("native depth selftest split items, split R near nodding: handler %s, splitLine %d, %d points (%d below, %d above), %d differ, level plane %d "
	       "differ, items of a split view %s, wheels against DrawTires %d cases %d differ, shift/mask classes %d cases %d differ, "
	       "marker bin of depth range 10..31 middle %d nearest %d, items %s\n",
	       splitPitched.handlerSplit ? "split" : "other", splitPitched.splitLine, splitPitched.compared, splitPitched.below, splitPitched.above,
	       splitPitched.differ, splitPitched.levelDiffer, itemsHeld ? "held" : "off", wheelsCompared, wheelsDiffer, classCases, classesDiffer,
	       binMiddle, binNearest, itemsPassed ? "passed" : "differs");

	// The bin markers of the twin (one marker per occupied bin), on a fourth line.
	printf("native depth selftest twin bin markers: bins 40 33 33 25 12 9 2 in the cells 10..31 give the cells %d %d %d %d, %d keys held, "
	       "linked into those cells only, arena keeps %d, paint runs report %d cells as over %d, row w %s with the draw offset (near, far, mirror), "
	       "view: items without a body one middle marker each in their ranges, no paint order and a full arena link nothing (%s), bins %s\n",
	       twinCells[0], twinCells[1], twinCells[2], twinCells[3], twinHeld, NR_TWIN_ARENA_KEEP, NR_TWIN_BINS_MAX + 1, NR_TWIN_BINS_MAX,
	       twinRowW ? "same" : "moved", twinView ? "held" : "off", twinPassed ? "passed" : "differ");

	// The own wheels (WHLS version 2), on a fifth line.
	printf("native depth selftest own wheels: mirror error max %.6f, steer error max %.6f, rim %.6f world units (16 model units x 0x0ccc / 4096), "
	       "roll error %.3g, rotations %d of 8, wheelSize 0 %s, own wheels %s\n",
	       ownMirror, ownSteer, ownRadius, ownRoll, ownProper, ownZero ? "held" : "off", ownPassed ? "passed" : "differ");

	// The stroboscope of the own wheels (render plan A3), on a sixth line.
	printf("native depth selftest wheel strobe: %s, strobe %s\n", strobeLine, strobePassed ? "passed" : "differ");

	// Win and lose (render plan B3), on a seventh line.
	printf("native depth selftest win lose: %d stages per target behind the poses, %d ticks per stage, %d checks, %d failures, "
	       "stage error max %.6f, win lose %s\n",
	       NATIVE_CHAR_GPU_FINISH_STAGES, NATIVE_CHAR_GPU_FINISH_TICKS, finishChecks, finishFailures, finishError,
	       (finishFailures == 0) ? "passed" : "differ");

	// The own wheels of WHLS version 3 (render plan A1, A2, A4), on an eighth line.
	printf("native depth selftest own wheels v3: rear mesh, axle roll, level of detail with NO LOD off and on, %d checks, %d failures, wheels v3 %s\n",
	       v3Checks, v3Failures, (v3Failures == 0) ? "passed" : "differ");

	return passed ? 0 : 1;
}
