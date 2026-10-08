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

static void NativeDepthCheck_Place(int viewZ, int huge, struct NativeDepthCheckPoint *out)
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
	mh.scale.x = 0x1800;
	mh.scale.y = 0x1800;
	mh.scale.z = 0x1800;

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

	// The render layer, as for a native draw in view 0 without a draw offset.
	notes = NativeRenderLayer_FillItem(&out->item, &tracker, &inst, &idpp, &pb, 0);
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

	NativeDepthCheck_Place(0x0FFF, huge, &pair->nearPoint);
	NativeDepthCheck_Place(0x1000, huge, &pair->farPoint);
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

static int NativeDepthCheck_PairPassed(const struct NativeDepthCheckPair *pair, int nearShift, int farShift)
{
	return pair->nearPoint.found && pair->farPoint.found && pair->nearPoint.shiftHeld && pair->farPoint.shiftHeld &&
	       ((int)pair->nearPoint.item.mvpShift == nearShift) && ((int)pair->farPoint.item.mvpShift == farShift) && (pair->step < 0.1) &&
	       (pair->rest < 0.1) && (pair->matrixPart < 0.5) && (pair->wrongNear > 50.0) && (pair->wrongFar > 50.0) && (pair->matrixWrongNear > 50.0) &&
	       (pair->matrixWrongFar > 50.0);
}

int NativeDepthCheck_Run(void)
{
	static struct NativeDepthCheckPair normal;
	static struct NativeDepthCheckPair huge;
	int held;
	int passed;

	// The queue writes its scratch words; outside a game run the scratchpad
	// has to be set up first.
	Platform_InitScratchpad();

	NativeDepthCheck_Pair(0, &normal);
	NativeDepthCheck_Pair(1, &huge);

	held = normal.nearPoint.shiftHeld && normal.farPoint.shiftHeld && huge.nearPoint.shiftHeld && huge.farPoint.shiftHeld;
	passed = NativeDepthCheck_PairPassed(&normal, 2, 0) && NativeDepthCheck_PairPassed(&huge, 0, -2);

	printf("native depth selftest %s: normal shift %d to %d, w %.4f to %.4f, step %.4f percent, rest %.4f percent, matrix part %.4f percent, "
	       "wrong shift %.1f and %.1f percent, matrix %.1f and %.1f percent, huge shift %d to %d, w %.4f to %.4f, step %.4f percent, rest %.4f percent, "
	       "matrix part %.4f percent, wrong shift %.1f and %.1f percent, matrix %.1f and %.1f percent, shift checks %s\n",
	       passed ? "passed" : "differs", (int)normal.nearPoint.item.mvpShift, (int)normal.farPoint.item.mvpShift, normal.nearPoint.w, normal.farPoint.w,
	       normal.step, normal.rest, normal.matrixPart, normal.wrongNear, normal.wrongFar, normal.matrixWrongNear, normal.matrixWrongFar,
	       (int)huge.nearPoint.item.mvpShift, (int)huge.farPoint.item.mvpShift, huge.nearPoint.w, huge.farPoint.w, huge.step, huge.rest,
	       huge.matrixPart, huge.wrongNear, huge.wrongFar, huge.matrixWrongNear, huge.matrixWrongFar, held ? "held" : "off");

	return passed ? 0 : 1;
}
