#include <common.h>

#ifdef CTR_NATIVE

// The world aspect, and nothing else.
//
// Taken from the reference build's game/native_view.c, which is 676 lines. What
// came across is the aspect itself and the one arithmetic operation it implies.
// What did not: the UI safe-area module (CTR_UI_BuildSafeArea, the anchor
// mapping, the full-canvas pointer ranges), the view-parameter struct with its
// frustum corners, and the reference-4:3 builder for UI push buffers. Those
// exist to keep a 4:3-authored HUD anchored inside a wider picture, which is its
// own step and needs the UI call sites with it.
//
// Hor+, not Hor-. The reference build's own note is worth keeping: raising the
// Y squash constant from 0x360 to 0x480 also produces a 16:9 picture, but it
// leaves the horizontal field of view where it was and cuts the top and bottom
// off instead. That is a vertical crop. Scaling the X row of the view-projection
// down leaves the vertical field of view untouched and widens the horizontal
// one - the same picture with more world to the left and right.
//
// 43:18 is 2.38889, which is 3440x1440 exactly. What is commonly written 21:9 is
// 2.33333 and is the aspect of no monitor anyone owns.

#define CTR_VIEW_REFERENCE_ASPECT_WIDTH  4
#define CTR_VIEW_REFERENCE_ASPECT_HEIGHT 3

global_variable int s_worldAspectWidth = CTR_VIEW_REFERENCE_ASPECT_WIDTH;
global_variable int s_worldAspectHeight = CTR_VIEW_REFERENCE_ASPECT_HEIGHT;

// Defined with the view settings further down, called by the aspect setter
// above them. The alternative is putting the setter after the settings, which
// would put the aspect - the thing this file is named for - halfway down it.
void CTR_View_UpdateActiveSettings(void);

internal int CTR_View_GCD(int a, int b)
{
	while (b != 0)
	{
		const int remainder = a % b;

		a = b;
		b = remainder;
	}

	return a;
}

// Reduced on the way in, so 16:9 and 32:18 are one setting and not two. The
// scale below is a ratio of products, and unreduced terms overflow it sooner
// than they need to.
void CTR_View_SetWorldAspect(int width, int height)
{
	int divisor;

	if ((width <= 0) || (height <= 0))
	{
		return;
	}

	divisor = CTR_View_GCD(width, height);
	if (divisor <= 0)
	{
		return;
	}

	s_worldAspectWidth = width / divisor;
	s_worldAspectHeight = height / divisor;

	// The set of view settings the render path reads follows the shape. Here,
	// because this is the one door the shape changes through - the flag, the
	// startup detection, a monitor change and the debug menu all arrive here.
	CTR_View_UpdateActiveSettings();
}

void CTR_View_GetWorldAspect(int *outWidth, int *outHeight)
{
	if (outWidth != NULL)
	{
		*outWidth = s_worldAspectWidth;
	}
	if (outHeight != NULL)
	{
		*outHeight = s_worldAspectHeight;
	}
}

//----------------------------------------------------------------------------------------
// THE THREE SHAPES, AND WHICH ONE A DISPLAY IS
//
// One list. Everything that has to enumerate the shapes - the startup detection,
// the debug menu's ASPECT row, and from here on anything that wants a setting
// per shape - walks this one. It used to be two lists: this idea lived in
// DebugMenu.c as s_aspects and the detection would have made a second copy of
// it, which is the shape of the three bugs found on 2026-08-27.
//
// There is no fourth entry and no "whatever the monitor happens to be". A shape
// costs a world projection, a UI safe area and a set of view settings that
// somebody has to look at; a 16:10 panel is nearer 16:9 than 4:3 and is driven
// as 16:9, with the presentation letterboxing the difference.
//
// Row 0 is spelled with the reference macros rather than 4 and 3, because it IS
// the reference: two places holding the same number is how one of them goes
// stale.

enum CTR_AspectMode
{
	CTR_ASPECT_MODE_4_3 = 0,
	CTR_ASPECT_MODE_16_9,
	CTR_ASPECT_MODE_43_18,
	CTR_ASPECT_MODE_COUNT,
};

struct CTR_AspectModeEntry
{
	const char *name;
	int width;
	int height;
};

global_variable const struct CTR_AspectModeEntry s_aspectModes[CTR_ASPECT_MODE_COUNT] = {
    {"4:3", CTR_VIEW_REFERENCE_ASPECT_WIDTH, CTR_VIEW_REFERENCE_ASPECT_HEIGHT},
    {"16:9", 16, 9},
    {"43:18", 43, 18},
};

//----------------------------------------------------------------------------------------
// THE CANVAS AS A DESCRIPTION INSTEAD OF AS LITERALS
//
// Up to step 1 the dimensions of the picture area stood as numbers in
// PushBuffer_Init: 0x200 wide, 0xd8 high, 0x6a for half a height, 0x6e as
// second start point, 0xfd for half a width, 0x103 as second column.
// Seven viewport cases, twenty-eight assignments, every number one of its own.
//
// Here they stand once, and the seven cases are computed.
//
// SINCE STEP 2 THE WIDTH IS REALLY WIDE. 4:3 keeps 512, 16:9 gets
// 682 and 43:18 gets 918 columns - and that is the difference between
// this rework and what used to be called widescreen. Before, the world was drawn in
// 512 columns and pulled apart by the display; from one
// column behind the projection nobody knew any more that a wide format
// was active. Now the wide picture is really wider, the GTE projects at
// 43:18 exactly as at 4:3, and what is added is world at the sides.
//
// WHAT IS FREE AND WHAT FOLLOWS. Exactly one number is free, the width. Everything
// else follows: the half height from height and gap, the second start point from
// half height and gap, the same calculation horizontally. At 512 that is
// (216-4)/2 = 106 = 0x6a, 106+4 = 110 = 0x6e, (512-6)/2 = 253 = 0xfd,
// 253+6 = 259 = 0x103 - exactly the literals that disappeared here.
//
// THE GAPS ARE THE REMAINDER, NOT A CHOICE. Four rows vertically and six
// columns horizontally are what stays free between the quarters; they
// stand here because otherwise they would be hidden in the difference of two literals.
//
// THE WIDTH IS A FRACTION, because it has to be: 512 * 4/3 is 2048/3 and
// 512 * 129/72 is 2752/3, and neither of the two is a column count. The
// table carries the exact fraction, CTR_Canvas_Width rounds it in one
// single place - see there why it rounds to EVEN.
//
// WHAT DOES NOT GROW WITH IT. The VRAM. Both frame buffers stand there at x 0..511
// (MainMain.c:522-525) and the textures lie in the right half and in
// the gap in between; 918 columns would overwrite textures. Native
// therefore means: wide in the render target and in pb->rect, 512 in VRAM. The
// pack shader already squashed before, it only gets a different factor, and
// whoever reads the picture back from VRAM - pause background, video,
// scrapbook - still sees the squashed 512 picture.
#define CTR_CANVAS_REFERENCE_WIDTH 512
#define CTR_CANVAS_HEIGHT          216

// The pixel aspect ratio of the PS1, 9/16. Not the picture aspect ratio - the
// similarity to 16:9 is a coincidence that PushBuffer.c:387 already names. From it
// and the canvas follows what the display shows: 512 * 9/16 = 288, and 288/216
// is 4/3.
#define CTR_CANVAS_PIXEL_NUM 9
#define CTR_CANVAS_PIXEL_DEN 16

#define CTR_CANVAS_SPLIT_GAP_X 6
#define CTR_CANVAS_SPLIT_GAP_Y 4

// The projection distance, and why it does NOT follow the width.
//
// It is the H register of the GTE and determines the field of view; a full picture
// gets 0x100, one half as wide 0x80. In step 1 it said here that it was open
// whether it should follow the width in step 2. It does not follow it, and that
// is the core of the method: if the ratio of width and distance stayed
// the same, a wider picture would show the same world stretched wider. With a fixed
// distance it shows MORE world at the same size - which is what Hor+ means.
//
// Deriving it from the width would only be possible with a rule that
// nobody wrote down: 253/2 is 126.5 and not 128. That is why it stands
// as what it is - two numbers per picture split.
#define CTR_CANVAS_DISTANCE_FULL  0x100
#define CTR_CANVAS_DISTANCE_SPLIT 0x80

struct CTR_CanvasFormat
{
	int widthNum;
	int widthDen;
	int height;
	int pixelNum;
	int pixelDen;
	int splitGapX;
	int splitGapY;
};

// One macro, three uses - the same form as CTR_VIEW_STOCK_SET further
// down, and for the same reason: three written-out lines would be three copies
// of one fact, and one of them would fall behind at some point.
#define CTR_CANVAS_AT(wNum, wDen) \
	{                             \
		(wNum), (wDen), CTR_CANVAS_HEIGHT, CTR_CANVAS_PIXEL_NUM, CTR_CANVAS_PIXEL_DEN, CTR_CANVAS_SPLIT_GAP_X, CTR_CANVAS_SPLIT_GAP_Y \
	}

global_variable const struct CTR_CanvasFormat s_canvasFormats[CTR_ASPECT_MODE_COUNT] = {
    CTR_CANVAS_AT(CTR_CANVAS_REFERENCE_WIDTH, 1), // 4:3   - the reference width itself
    CTR_CANVAS_AT(2048, 3),                       // 16:9  - 512 * 4/3   = 682.67
    CTR_CANVAS_AT(2752, 3),                       // 43:18 - 512 * 129/72 = 917.33
};

int CTR_View_SettingsMode(void);

// THE ROUNDING RULE: TO THE NEAREST EVEN COLUMN COUNT.
//
// The fraction above is exact, the canvas cannot be - rect.w is an
// s16, and 682.67 is not a column count. Rounding goes to EVEN, and that is
// no preference but the condition under which the split works out:
// CTR_Canvas_Viewport computes halfW = (W - 6) / 2 and puts two halves with
// a gap of 6 side by side. 2 * ((W - 6) / 2) + 6 is W again only for even W.
// At 917 the result would be 916 - a column that none of the four
// players gets, so a seam in the middle of the picture.
//
// The second reason lies in the GTE. The picture centre goes as
// CTC2(rect.w << 15, 24) into the OFX register, that is (W/2) << 16; with odd W
// the centre lies half a pixel next to the column boundary, and the same half
// shift is then contained in every projection.
//
// WHAT THE ROUNDING COSTS, measured against the display ratio:
//     4:3    512/1   ->  512   exact
//     16:9   2048/3  ->  682   0.098 % too narrow  (684 would be 0.195 % too wide)
//     43:18  2752/3  ->  918   0.073 % too wide    (916 would be 0.146 % too narrow)
// Both below a tenth of a percent. In a window 864 rows high that is
// a bar of less than one pixel - see CTR_Canvas_PresentAspect,
// which computes from the same canvas and therefore does not stretch at all.
//
// The nearest even number, in integers: 2 * round(num / (2*den)), and round(a/b)
// is (a + b/2) / b - so here (num + den) / (2*den). For 512/1 the result is 512,
// without a single bit differing from before step 2.
int CTR_Canvas_Width(int mode)
{
	int num;
	int den;

	if ((mode < 0) || (mode >= (int)CTR_ASPECT_MODE_COUNT))
	{
		return CTR_CANVAS_REFERENCE_WIDTH;
	}

	num = s_canvasFormats[mode].widthNum;
	den = s_canvasFormats[mode].widthDen;

	if ((num <= 0) || (den <= 0))
	{
		return CTR_CANVAS_REFERENCE_WIDTH;
	}

	return 2 * ((num + den) / (2 * den));
}

int CTR_Canvas_Height(int mode)
{
	if ((mode < 0) || (mode >= (int)CTR_ASPECT_MODE_COUNT))
	{
		return CTR_CANVAS_HEIGHT;
	}

	return s_canvasFormats[mode].height;
}

// The row the render path reads. Through CTR_View_SettingsMode and not
// through CTR_View_ActiveMode, because the second may return -1: --aspect takes
// any W:H, and a custom aspect ratio gets the canvas of the
// nearest of the three shapes. Since step 2 that is no longer without consequence
// - a ratio between two rows gets a canvas that does not fit it exactly,
// and is shown with a bar instead of stretched. See
// CTR_Canvas_PresentAspect.
int CTR_Canvas_ActiveWidth(void)
{
	return CTR_Canvas_Width(CTR_View_SettingsMode());
}

int CTR_Canvas_ActiveHeight(void)
{
	return CTR_Canvas_Height(CTR_View_SettingsMode());
}

// THE REFERENCE WIDTH, for the one reader that takes rect.w as a SCALE and
// not as the picture edge.
//
// Step 0 sorted 93 readers of pb->rect: 88 mean the picture edge and
// follow the canvas by themselves, exactly one is a scale - the
// LOD distance in RenderBucket_QueueExecute.c. It computes from half the
// viewport width how far away a model may stand before it loses a detail
// level. If it followed the canvas, models at 43:18 would keep their level
// 1.8 times as far - that would be a different track, not a wider picture.
//
// CONVERTED AND NOT REPLACED, for two reasons that both stand in the tree:
// with four players the viewport of one player is not the whole canvas,
// and 225.c shrinks rect.w frame by frame during the winner animation.
// A fixed 512 would throw both away. At 4:3 canvas and reference are
// the same number, so the conversion there is the identity - for every
// input value, not only for 512.
int CTR_Canvas_ToReferenceWidth(int width)
{
	const int canvasW = CTR_Canvas_ActiveWidth();

	if ((canvasW <= 0) || (canvasW == CTR_CANVAS_REFERENCE_WIDTH))
	{
		return width;
	}

	return (int)(((s64)width * CTR_CANVAS_REFERENCE_WIDTH) / canvasW);
}

// The same conversion the other way round: a length that is authored on the
// reference canvas, on the drawn one.
//
// For a layer that should fill the whole picture and has no anchor for that
// - the loading flag is the case it is built for. It is not
// shifted but stretched, just like FULL_CANVAS in the UI mapper, and for
// the same reason: what is laid over the whole picture has no edge it
// could hold on to. At 4:3 the identity, for every input value.
int CTR_Canvas_FromReferenceWidth(int width)
{
	const int canvasW = CTR_Canvas_ActiveWidth();

	if ((canvasW <= 0) || (canvasW == CTR_CANVAS_REFERENCE_WIDTH))
	{
		return width;
	}

	return (int)(((s64)width * canvasW) / CTR_CANVAS_REFERENCE_WIDTH);
}

// A VIEWPORT, COMPUTED INSTEAD OF LOOKED UP.
//
// Takes the canvas as dimensions and not as a format number, so that the same
// function answers two questions: which picture area this player gets,
// and which one he would get on the reference canvas of 512. The second is the one
// a scale reader needs - see CTR_Canvas_ToReferenceWidth.
//
// Return 0 means: for this combination there is no picture area, and the
// caller leaves the push buffer untouched. That is no new behaviour
// but the old one spelled out - PushBuffer_Init returned in exactly these cases
// without writing anything. With ONE player this expressly does NOT apply
// to the ID: total == 1 gave each of the four push buffers the whole picture, and
// that stays so.
int CTR_Canvas_Viewport(int canvasW, int canvasH, int id, int total, RECT *outRect, int *outDistance)
{
	const int halfW = (canvasW - CTR_CANVAS_SPLIT_GAP_X) / 2;
	const int halfH = (canvasH - CTR_CANVAS_SPLIT_GAP_Y) / 2;
	const int secondX = halfW + CTR_CANVAS_SPLIT_GAP_X;
	const int secondY = halfH + CTR_CANVAS_SPLIT_GAP_Y;

	int x = 0;
	int y = 0;
	int w = 0;
	int h = 0;
	int distance = 0;

	if (total == 1)
	{
		w = canvasW;
		h = canvasH;
		distance = CTR_CANVAS_DISTANCE_FULL;
	}
	else if ((total == 2) && (id >= 0) && (id <= 1))
	{
		w = canvasW;
		h = halfH;
		y = (id == 1) ? secondY : 0;
		distance = CTR_CANVAS_DISTANCE_FULL;
	}
	else if ((total >= 3) && (total <= 4) && (id >= 0) && (id <= 3))
	{
		// Bit 0 picks the column, bit 1 the row - the order in which the
		// four cases used to stand one below the other.
		w = halfW;
		h = halfH;
		x = ((id & 1) != 0) ? secondX : 0;
		y = ((id & 2) != 0) ? secondY : 0;
		distance = CTR_CANVAS_DISTANCE_SPLIT;
	}
	else
	{
		return 0;
	}

	if (outRect != NULL)
	{
		outRect->x = (s16)x;
		outRect->y = (s16)y;
		outRect->w = (s16)w;
		outRect->h = (s16)h;
	}
	if (outDistance != NULL)
	{
		*outDistance = distance;
	}

	return 1;
}

// The shape in which the finished picture is shown - computed from the canvas.
//
// Width times pixel ratio, against the height: (W * 9) : (H * 16). At 512x216
// that is 4608:3456, reduced 4:3 - the same shape as before. And not only
// the same shape but the same numbers: 4608/3456 is exactly 3/4, and the
// integer calculation in NativeRenderer_UpdatePresentationViewport therefore
// arrives for every window size at the same viewport as with 4:3.
// That is why 4:3 stays bit-identical, although the source is a different one.
//
// WHY NO LONGER THE WORLD RATIO. Because it would stretch the picture, and
// exactly that is supposed to stop. The canvas is now as wide as the format
// demands; pulled into an exactly 16:9 window it would be distorted by the 0.098 %
// the rounding leaves over. Computed from the canvas the picture
// stands undistorted, and the rounding becomes a bar of less than one
// pixel.
//
// WHAT THAT MEANS FOR A CUSTOM --aspect. The canvas comes from the table
// of the three shapes. --aspect 21:9 therefore gets the canvas of 43:18 and is
// shown with bars instead of stretched - before, every ratio was filled
// exactly. That is a change and is recorded as such for step 2;
// a canvas of its own for any ratio would be a decision of its own
// and not a derivation.
void CTR_Canvas_PresentAspect(int *outWidth, int *outHeight)
{
	const int mode = CTR_View_SettingsMode();

	if (outWidth != NULL)
	{
		*outWidth = CTR_Canvas_Width(mode) * CTR_CANVAS_PIXEL_NUM;
	}
	if (outHeight != NULL)
	{
		*outHeight = CTR_Canvas_Height(mode) * CTR_CANVAS_PIXEL_DEN;
	}
}

int CTR_View_ModeCount(void)
{
	return (int)CTR_ASPECT_MODE_COUNT;
}

const char *CTR_View_ModeName(int mode)
{
	if ((mode < 0) || (mode >= (int)CTR_ASPECT_MODE_COUNT))
	{
		return "custom";
	}

	return s_aspectModes[mode].name;
}

void CTR_View_ModeAspect(int mode, int *outWidth, int *outHeight)
{
	if ((mode < 0) || (mode >= (int)CTR_ASPECT_MODE_COUNT))
	{
		return;
	}

	if (outWidth != NULL)
	{
		*outWidth = s_aspectModes[mode].width;
	}
	if (outHeight != NULL)
	{
		*outHeight = s_aspectModes[mode].height;
	}
}

// Which row the world aspect currently is, or -1 for none of them. -1 is a real
// answer: --aspect takes any W:H, so somebody can be driving 5:4 on purpose, and
// a caller that has to name the row must be told there is none rather than being
// handed row 0.
int CTR_View_ActiveMode(void)
{
	int mode;

	for (mode = 0; mode < (int)CTR_ASPECT_MODE_COUNT; mode++)
	{
		if ((s_aspectModes[mode].width == s_worldAspectWidth) && (s_aspectModes[mode].height == s_worldAspectHeight))
		{
			return mode;
		}
	}

	return -1;
}

// Thousandths, not a float. 4:3 is 1333, 16:9 is 1777, 43:18 is 2388 - the gaps
// between them are hundreds, so the truncation is nowhere near able to move an
// answer, and it goes into the log as %d.%03d without ever becoming a float.
int CTR_View_RatioMilli(int pixelWidth, int pixelHeight)
{
	if ((pixelWidth <= 0) || (pixelHeight <= 0))
	{
		return 0;
	}

	return (int)(((s64)pixelWidth * 1000) / (s64)pixelHeight);
}

// Nearest in ratio, and nothing else. Not "wider than" or "at least" - a
// threshold has to be placed somewhere and every placement is an opinion, while
// nearest is a measurement. 1.6 comes out 16:9 (0.177 away, against 0.267 to
// 4:3) and so does 2.0 (0.222 against 0.389 to 43:18).
//
// A tie goes to the lower row, so the answer is the same every time it is asked.
int CTR_View_NearestMode(int pixelWidth, int pixelHeight)
{
	const int ratio = CTR_View_RatioMilli(pixelWidth, pixelHeight);
	int best = -1;
	int bestDistance = 0;
	int mode;

	if (ratio <= 0)
	{
		return -1;
	}

	for (mode = 0; mode < (int)CTR_ASPECT_MODE_COUNT; mode++)
	{
		int distance = ratio - CTR_View_RatioMilli(s_aspectModes[mode].width, s_aspectModes[mode].height);

		if (distance < 0)
		{
			distance = -distance;
		}

		if ((best < 0) || (distance < bestDistance))
		{
			best = mode;
			bestDistance = distance;
		}
	}

	return best;
}

// The one question every caller below actually asks. At 4:3 the scale is 12/12,
// which is exact for every input, but the callers still branch on this: an
// identity that is arrived at by multiplying and dividing is not the same
// promise as an untouched value, and 4:3 has to stay untouched.
int CTR_View_IsReferenceAspect(void)
{
	return (s_worldAspectWidth == CTR_VIEW_REFERENCE_ASPECT_WIDTH) && (s_worldAspectHeight == CTR_VIEW_REFERENCE_ASPECT_HEIGHT);
}

// HERE STOOD THE FRACTION AND ITS TWO SCALERS.
//
// CTR_View_GetAspectFraction returned referenceAspect/targetAspect - 36/48 at 16:9,
// 72/129 at 43:18 - and three consumers used it to undo the same thing:
// ScaleProjectionX squashed the X row of the projection, UnscaleProjectionX
// unsquashed the cull frustum, and CTR_UI_BuildSafeArea pulled the HUD in. All
// three cancelled the same stretch that the display applied when a 512
// column wide picture was shown in a wider box.
//
// The display no longer stretches. The canvas is as wide as the format, the
// GTE projects at 43:18 as at 4:3 and thereby shows more world instead of
// the same narrower - so the fraction is 1/1 and there is nothing left
// to cancel. Step 2 of the format description rework.
//
// CTR_View_IsReferenceAspect still stands above; after this step it has
// no reader any more. It is not removed here - that would be a second
// change, and it is the same treatment as for aspectX/aspectY.

// The HUD's push buffer is authored in 4:3 and must not be widened with the
// world. Two ways to be it, because the game reaches it by both: the member in
// the game tracker, and the pointer sdata keeps to it.
int CTR_View_IsUIPushBuffer(const struct PushBuffer *pb)
{
	if ((pb == NULL) || (sdata == NULL) || (sdata->gGT == NULL))
	{
		return 0;
	}

	if (pb == &sdata->gGT->pushBuffer_UI)
	{
		return 1;
	}

	return (sdata->ptrPushBufferUI != 0) && (pb == (const struct PushBuffer *)(uintptr_t)sdata->ptrPushBufferUI);
}

// WHICH CANVAS WIDTH THIS PUSH BUFFER GETS.
//
// The world buffer gets the canvas of the format, the HUD buffer keeps the
// reference width. That is the same separation that is already justified one line
// higher, only at the other place: the HUD is authored in 512 columns,
// its 140 slots and the menu constants stand in this
// unit, and a number that changes to 918 would be for them not a wider
// picture but a wrong reference - a right-anchored element computes
// virtualWidth - x and would lie 406 columns off.
//
// CONSEQUENCE AS LONG AS STEP 3 IS MISSING: at 16:9 and 43:18 the HUD lies on the left in the
// wide canvas instead of centred. Step 3 lets it migrate in through the anchors
// - left stays x, centre becomes x + (W-512)/2, right x + (W-512).
// At 4:3 both widths are 512 and this shift is 0, so here there is
// no difference for the reference format.
int CTR_Canvas_PushBufferWidth(const struct PushBuffer *pb)
{
	if (CTR_View_IsUIPushBuffer(pb))
	{
		return CTR_CANVAS_REFERENCE_WIDTH;
	}

	return CTR_Canvas_ActiveWidth();
}

// A length FROM THIS PUSH BUFFER converted to the frame buffer.
//
// CTR_Canvas_ToReferenceWidth always divides by the canvas. That is right for
// the world push buffer, whose rectangle stands in canvas columns - and it is
// NOT right for the UI push buffer, whose rectangle stands in the authored 512.
// That one was shrunk from 512 to 285 by it, and because its rectangle serves as
// draw area, the display clamped to the left 512 canvas columns:
// HUD gone on the right, race start picture torn at exactly this edge.
//
// The reference size is therefore not the canvas but the width in which
// EXACTLY THIS push buffer is authored. At 4:3 both are 512 and the
// conversion is the identity for every push buffer.
int CTR_Canvas_PushBufferToReferenceWidth(const struct PushBuffer *pb, int width)
{
	const int authoredW = CTR_Canvas_PushBufferWidth(pb);

	if ((authoredW <= 0) || (authoredW == CTR_CANVAS_REFERENCE_WIDTH))
	{
		return width;
	}

	return (int)(((s64)width * CTR_CANVAS_REFERENCE_WIDTH) / authoredW);
}

//----------------------------------------------------------------------------------------
// THE UI SIDE OF THE SAME FRACTION
//
// The world opens Hor+ and the presentation shows the 512-column picture in a
// wider box. Those two together are the world at its right proportions with more
// of it visible - and they are also, for anything NOT drawn through the world
// projection, a horizontal stretch by exactly the fraction above. The HUD is
// authored in a 512x216 canvas and drawn straight into that buffer, so at 16:9
// every sprite in it comes out 4/3 too wide.
//
// So the UI is pulled in by the same fraction before it is drawn, and the
// presentation stretch puts it back at its authored proportions. Square pixels,
// no distortion, and the same sprite at all three ratios. What DOES change is
// how much room there is around it, which is the whole point of a wider picture.
//
// Y is untouched. The vertical field of view does not change under Hor+ and the
// presentation does not stretch vertically, so there is nothing to undo.

// Where an element keeps still while the canvas gets wider.
//
// One factor, four fixed points. The factor is the same in all four, which is
// what makes this a move rather than a stretch; only the point that does not
// move differs:
//
//   left    x' = 0      + x * s
//   right   x' = width  - (width - x) * s
//   centre  x' = centre + (x - centre) * s
//   canvas  x' = x                              (not mapped at all)
//
// CENTRE is what a world object does. Hor+ keeps pixels per radian and only
// shows more at the sides, so a point in the world stays where it was relative
// to the picture centre - which is exactly this arithmetic. An element that has
// to stay beside something in the world takes this one.
//
// FULL_CANVAS is not an edge. A layer laid ACROSS the canvas - a fade, a dim, a
// separator between split-screen viewports - has nothing to hold on to, and
// pulling it in leaves the picture showing through on both sides.
enum CTR_UIAnchor
{
	CTR_UI_ANCHOR_CENTRE = 0,
	CTR_UI_ANCHOR_FULL_CANVAS = 1,
	CTR_UI_ANCHOR_LEFT = 2,
	CTR_UI_ANCHOR_RIGHT = 3,
};

struct CTR_UIViewParameters
{
	// The canvas in which the HUD is AUTHORED: 512 x 216, in every format.
	// The 140 HUD slots and the 76 menu constants stand in this unit,
	// and it never changes.
	int virtualWidth;
	int virtualHeight;
	int centreX;

	// The canvas that is DRAWN into: 512, 682 or 918. The difference
	// between the two is the whole task of this mapper.
	int canvasWidth;

	// THE COMPOSITION ZONE OF THE MENUS (design rule, 2026-09-28): "What belongs together
	// stays together. Additional width is background, never distance."
	//
	// The strip of the canvas at whose edges LEFT and RIGHT hold on.
	// Outside the menus it is the canvas itself, and the mapper computes
	// bit for bit as before. In the menus it is at most as wide as the
	// 16:9 canvas and stands centred - so at 918 columns 682 from column 118.
	// Whatever goes beyond that is background: the world (Hor+) and the flag.
	int zoneLeft;
	int zoneWidth;
};

// Defined with the player-menu rule further down; the safe area asks it.
int CTR_UI_MenuMode(void);

// HERE STOOD THE ROUNDED SCALING.
//
// It multiplied a distance from the anchor point by the aspect fraction and
// rounded symmetrically, so that two elements at the two picture edges do not
// drift apart by a pixel that no rule asked for. The mapper
// no longer scales - it shifts, and a shift by a whole number
// has no rounding error that would have to be argued away.

int CTR_UI_BuildSafeArea(struct CTR_UIViewParameters *view, int virtualWidth, int virtualHeight)
{
	if ((view == NULL) || (virtualWidth <= 0) || (virtualHeight <= 0))
	{
		return 0;
	}

	// TWO WIDTHS, AND THAT IS ALL OF STEP 3.
	//
	// virtualWidth comes from the caller and is the width of the HUD push buffer,
	// so the canvas in which the elements are authored - 512, unchanged
	// in every format. canvasWidth is the canvas that is drawn into.
	// Their difference is the shift that CTR_UI_MapX distributes.
	//
	// The canvas is READ here and not passed in, because otherwise it would have two
	// sources: the caller and the format description. One of them would fall
	// behind.
	//
	// A CANVAS NARROWER THAN THE AUTHORED ONE DOES NOT EXIST. It would be a
	// crop and not a narrower picture - the same consideration from which the old
	// version clamped its fraction to 1/1 as soon as the ratio was not wider
	// than the reference format. At 4:3 both widths are 512, the
	// shift is 0, and the mapper is the identity.
	view->virtualWidth = virtualWidth;
	view->virtualHeight = virtualHeight;
	view->centreX = virtualWidth / 2;
	view->canvasWidth = CTR_Canvas_ActiveWidth();

	if (view->canvasWidth < virtualWidth)
	{
		view->canvasWidth = virtualWidth;
	}

	// THE ZONE, AND WHY IT DOES NOTHING AT 4:3 AND 16:9.
	//
	// Its width is the canvas of 16:9, read from the format table and
	// not copied as 682. It only takes effect when the canvas is wider
	// than it - at 4:3 (512) and 16:9 (682) the zone is the canvas, the
	// left edge 0, and CTR_UI_MapX computes the same numbers as before. At
	// 43:18 and 64:27 (918) it lies at 118..799.
	//
	// Only in the player menus (CTR_UI_MenuMode). The race HUD, the pause, the
	// loading screen and the results screens keep the whole canvas.
	view->zoneLeft = 0;
	view->zoneWidth = view->canvasWidth;

	if (CTR_UI_MenuMode())
	{
		const int zoneWidth = CTR_Canvas_Width(CTR_ASPECT_MODE_16_9);

		if ((zoneWidth >= virtualWidth) && (zoneWidth < view->canvasWidth))
		{
			view->zoneWidth = zoneWidth;
			view->zoneLeft = (view->canvasWidth - zoneWidth) / 2;
		}
	}

	return 1;
}

// A HUD COORDINATE THAT DOES NOT GO THROUGH THE UI PUSH BUFFER.
//
// The mapper in native_gpu.c only sees what is drawn into the UI push buffer.
// Whoever puts a HUD element elsewhere for ordering reasons is not found there
// and stays put, while its icon is shifted.
//
// There is exactly one such case: the glow behind item box and
// wumpa fruit (UI_Weapon.c) draws into the WORLD push buffer, so that it stays behind
// its icon - the three other UI places that draw there
// are pointers above other vehicles and rightly stand in world coordinates.
//
// The anchor comes from the same thirds rule that the mapper applies to an
// element box, applied to the point instead of the box. It may do that
// because the element lies on its icon: measured 286 for the fruit, 209
// for the item, 232 for the icon in between - all three in the middle
// third. At 4:3 the shift is 0 and this the identity.
int CTR_UI_MapAuthoredX(int x)
{
	struct CTR_UIViewParameters view;

	if (!CTR_UI_BuildSafeArea(&view, CTR_CANVAS_REFERENCE_WIDTH, CTR_Canvas_ActiveHeight()))
	{
		return x;
	}

	return CTR_UI_MapX(&view, x, CTR_UI_AnchorForBox(x, x, view.virtualWidth));
}

// SHIFTING BY ANCHOR, AND ONE LAYER GETS STRETCHED.
//
// The anchor says which point of the authored canvas falls on which point of the
// drawn one. Three of the four cases are pure shifts by a
// whole number, and a shift changes neither width nor distance
// between two elements - it cannot distort anything.
//
//   LEFT    x                     the left picture edge is the left picture edge
//   RIGHT   x + (W - 512)         the right one stays the right one
//   CENTRE  x + (W - 512)/2       the picture centre stays the picture centre
//
// At 918 that is 0, +406 and +203; at 682 it is 0, +170 and +85.
//
// FULL_CANVAS IS THE EXCEPTION, and it is exactly the layer for which the
// anchor exists: a fade, a dimming, a separator bar between
// picture areas. It is LAID over the canvas and has nothing it
// could hold on to; shifted, it would let the picture show through on one side.
// Up to step 2 the display stretched it, because it stretched the whole
// 512 picture. The display no longer stretches, so it has to
// happen here - in ONE place, not per element.
//
// All four cases are the identity at 4:3, for every input value: the
// shift is 0 and the stretch is x * 512 / 512.
//
// LEFT AND RIGHT HOLD THE EDGES OF THE ZONE, not those of the canvas. If the
// zone is the canvas - outside the menus always, in the menus up to 16:9 -,
// those are the same numbers as above: LEFT x, RIGHT x + (W - 512). In a
// menu at 918 columns they are +118 and +288. CENTRE stays the picture centre, and
// that is at the same time the centre of the zone: (W - Z)/2 + (Z - 512)/2 = (W - 512)/2.
// FULL_CANVAS stays stretched over the whole canvas - a layer over
// the picture is background, and that may grow.
int CTR_UI_MapX(const struct CTR_UIViewParameters *view, int x, int anchor)
{
	int shift;

	if (view == NULL)
	{
		return x;
	}

	shift = view->canvasWidth - view->virtualWidth;

	if (anchor == (int)CTR_UI_ANCHOR_FULL_CANVAS)
	{
		if ((shift == 0) || (view->virtualWidth <= 0))
		{
			return x;
		}

		return (int)(((s64)x * view->canvasWidth) / view->virtualWidth);
	}

	if (anchor == (int)CTR_UI_ANCHOR_LEFT)
	{
		return x + view->zoneLeft;
	}

	if (anchor == (int)CTR_UI_ANCHOR_RIGHT)
	{
		return x + view->zoneLeft + (view->zoneWidth - view->virtualWidth);
	}

	return x + (view->canvasWidth / 2) - view->centreX;
}

//----------------------------------------------------------------------------------------
// A SET OF VIEW SETTINGS PER SHAPE
//
// Three data sets, one struct, one code path.
//
// This is the one place where three copies are RIGHT, and it is worth saying why
// it is not the mistake the UI table exists to prevent. There, one authored HUD
// was being described three times: the same fact, and two of the copies would go
// stale. Here the fact itself is three facts - how far a 43:18 picture should
// see is not the same question as how far a 4:3 one should, because the pictures
// contain different amounts of world. A shape is a physical difference, and a
// difference that is real belongs in data.
//
// What must NOT be three is the code. Nothing below branches on which shape is
// in force; it reads the set that is active and does the same arithmetic with
// it.
//
// Every value here is a number the render path already used, at the place it
// used it. All three sets hold the stock ones, so today every expression comes
// out at the same bits as before.

enum CTR_ViewSetting
{
	// The projection distance - the game's own word for field of view. Smaller
	// sees wider. Stored as a factor in 1/256 and applied where the distance is
	// READ, not where it is written: PushBuffer_Init runs once per level, and a
	// row that only bites after a level change is a row that cannot be turned
	// while driving. Applying it at the read sites also means it composes with
	// whatever the camera decided this frame - the speed zoom, a cutscene
	// distance - instead of overwriting it.
	CTR_VIEW_SETTING_FOV = 0,

	// The divisor the CULL frustum's vertical half-extent is built with. NOT a
	// field of view - the projection's own vertical scale is a second 0x360 in
	// PushBuffer_SetMatrixVP that this does not touch. See the descriptor table.
	CTR_VIEW_SETTING_VERTICAL,

	// The near clip threshold, as a factor in 1/256 of the one the render path
	// computes. A factor and not a shift: a shift has five values and this needs
	// to be turned in the same percent as everything else. 256 is one, and one
	// is bit-identical - the multiply and the shift cancel exactly.
	CTR_VIEW_SETTING_NEAR,

	// The distance past which a BSP leaf is put in the melting bucket, as the
	// multiplier of the projection distance that produces it. This is the old
	// tree's DIST: detail, not a far plane. Stock 0x1a00 is 0x1a at 1/256, so
	// the render path shifts by eight and lands on the same product.
	CTR_VIEW_SETTING_TRACK_DIST,

	// How far along its direction each frustum corner is pushed, whose only
	// consumer is the coarse cull box. Also not a far plane. See the table.
	CTR_VIEW_SETTING_FAR,

	CTR_VIEW_SETTING_COUNT,
};

struct CTR_ViewSettings
{
	// An array, not four named fields. The menu, the config file and the clamp
	// all have to walk the settings, and walking named fields means offsetof and
	// a second table that says which offset is which.
	int value[CTR_VIEW_SETTING_COUNT];
};

struct CTR_ViewSettingDesc
{
	const char *key;   // what the config file writes
	const char *label; // what the debug menu shows, at most 10 characters
	int stock;         // the value the render path used before any of this
	int minimum;
	int maximum;
	int hasOff;  // whether the row has an OFF position one step above the grid
	int percent; // whether this row is read and turned as a share of stock
};

// TURNED IN PERCENT OF STOCK, NOT IN RAW UNITS
//
// A raw 0x360 says nothing about how far it is from what the game shipped with.
// The share does, and it is the same question for all three of them, so it is
// also the same step for all three - one number here instead of a column of
// hand-picked raw steps that all mean something different.
//
// FIVE PERCENT, and the reason is not the size of the step. It is that STOCK
// SITS ON THE GRID: every value reachable from 100% is a multiple of five, so
// counting back to exactly stock always works and RESET SET is a convenience
// rather than the only way home. A step that did not divide 100 would leave the
// original unreachable after the first turn.
//
// The value stored and written to disk stays raw. The render path reads raw, the
// config file has raw in it already, and the raw number is what gets written
// into the source if a turned value is ever kept - so it is what the row shows
// beside the share.
#define CTR_VIEW_PERCENT_STEP 5

// THE GRID EVERY ROW IS TURNED ON: 5 % TO 200 %, IN FIVES.
//
// Decided after the measurement below was on the table. The earlier
// limits stopped each row where its effect stopped, and the effect of that was
// that a range which does nothing became a range that cannot be reached - which
// also means it cannot be looked at. Seeing where a picture breaks is worth
// having, and every one of these is one press away from stock.
//
// What is inside the grid and still true:
//
//   VERT CULL  below 100 % widens the cull frustum past the picture. Nothing
//              new appears, because the projection does not show it.
//   CULL BOX   above roughly 55 % the corner ray already leaves the +-0x8000
//              world box, so the clipped point - and the box built from it - is
//              bit-identical however high this goes.
//   NEAR       below 100 % the near clip drops under half the projection
//              distance, which is where the GTE's perspective divide gives up
//              and clamps. Geometry gets through that cannot be projected, and
//              a vertex out of a clamped divide is thrown across the screen.
//              Not a dead range - a range that draws wrong on purpose.
//
// FOV and TRACK DIST do something everywhere in the grid.
#define CTR_VIEW_PERCENT_MIN 5
#define CTR_VIEW_PERCENT_MAX 200

// A share of stock as a raw value, rounded the same way in the table and in the
// step - so the bound the table names is exactly the bound a row steps onto.
#define CTR_VIEW_RAW_AT(stock, percent) ((((stock) * (percent)) + 50) / 100)

// The four numbers the render path used before any of this existed, spelled
// once. The descriptor table below and the three sets further down both read
// them from here, so "all three sets are stock today" is a property of the
// source and not something that has to be checked.
// The numbers the render path used before any of this existed, spelled once.
// The descriptor table below and the three sets further down both read them from
// here, so "all three sets are stock today" is a property of the source.
//
// TWO OF THE FIVE ARE FACTORS, AND 0x100 IS ONE.
//
// FOV, VERTICAL and FAR replace a literal that stands in the source; their raw
// value IS that literal. NEAR and TRACK DIST scale a number the render path
// works out per frame from the projection distance, so there is no literal to
// store - their raw is the factor, in 1/256, and 0x100 means unchanged. At
// 0x100 the multiply and the shift cancel exactly, for every input, so 100 %
// is not "close to stock", it is stock.
#define CTR_VIEW_STOCK_FOV        0x100
#define CTR_VIEW_STOCK_VERTICAL   0x360
#define CTR_VIEW_STOCK_FAR        0x100
#define CTR_VIEW_STOCK_NEAR       0x100
#define CTR_VIEW_STOCK_TRACK_DIST 0x1a00

// The unit of a factor row, and the shift that undoes it.
#define CTR_VIEW_FACTOR_ONE   0x100
#define CTR_VIEW_FACTOR_SHIFT 8

// TRACK DIST past this never melts anything, and that is provable rather than
// generous: the distance it is compared against comes out of gte_stsz, and the
// GTE clamps SZ3 to 0xffff. A threshold above 0xffff can never be exceeded.
//
// It is also the number the LOD page's TRACK row sets, which is how the two
// pages show the same fact without storing it twice.
#define CTR_VIEW_TRACK_DIST_OFF 0x10000

// One descriptor table. Three consumers: the menu rows, the config file keys and
// the clamp. The stock column is also what a set is reset to and what a changed
// set is measured against, so one place knows what stock is.
//
// WHAT EACH ROW REACHES, AND WHERE ITS RANGE STOPS MEANING ANYTHING
//
// Measured out of the code on 2026-08-28, because a row that can be turned
// through a range where nothing happens is worse than a row that is not there:
// it gets turned, nothing changes, and the next hour goes to looking for the
// reason somewhere else. The limits are set where the effect ends, not where the
// arithmetic ends.
//
//   FOV        live over its whole range and, since it is applied at the read
//              sites, live everywhere - the title screen, the garage, cutscenes
//              and the split screens included. It composes with the camera's own
//              zoom instead of replacing it.
//
//   VERT CULL  the vertical half-extent of the CULL frustum and only that. At
//              stock it matches the picture to the unit: rect.h * 0x600 / 0x360
//              / 2 is 192, and 192 is exactly the view-space half-height that
//              fills a 216-line viewport through a Y row scaled by 0x360/0x600.
//              BELOW 100 % the cull frustum is wider than the picture and
//              nothing new appears - the whole lower half of the first range was
//              dead. Above 100 % it cuts scenery off at the top and bottom.
//              Minimum is stock, and the name no longer says field of view.
//
//   NEAR       the near clip, and it can only ever cull MORE. The GTE's divide
//              gives up once the projected depth reaches half the projection
//              distance - gte_divide returns 0xffffffff at numerator >=
//              denominator * 2 and Lm_E clamps to 0x1ffff - and the stock
//              threshold (FOV >> 1) + 1 is the smallest value that stays one
//              above that edge. So 100 % is a floor with a proof behind it, not
//              a preference. Upward it drops close geometry that would have
//              projected correctly, which is the safe direction and the only
//              one there is.
//
//   TRACK DIST the old tree's DIST. Past this distance a BSP leaf goes in the
//              melting bucket, where the midpoints of its 3x3 grid walk toward
//              the straight line between their neighbours - a quadblock going
//              flat, and the edge glitch that gets turned away by pushing the
//              distance out. Live from 25 % up to the point where nothing melts
//              any more; that point is exact and the table's maximum sits on it.
//
//   CULL BOX   was FAR, and it is not a far plane. It scales the four corner
//              RAYS whose only consumer is pb->bbox, the axis-aligned box that
//              BSP children are rejected against; the four side planes are built
//              from the same rays at unit length and never see it. The far point
//              is then clipped against the +-0x8000 world limit, and that is
//              where the upper range died - at stock the ray is 104909 units at
//              4:3 for a world 65536 across, so it leaves the box and the clip
//              puts it back on the boundary, which depends on the direction and
//              not on the length. Above roughly 55 % the box is bit-identical
//              whatever this says. Maximum is stock, so 100 % means "does not
//              cut" and every step down is a real cut. The wider the picture,
//              the longer the ray, so it starts to bite LOWER at 16:9 and lower
//              again at 43:18.
//
// The bounds are the grid, spelled from the stock column so that no row can name
// a percentage the step cannot land on. TRACK DIST keeps the OFF value as its
// raw maximum - that is a position, not a share, and the step reaches it from
// the top of the grid rather than by counting.
global_variable const struct CTR_ViewSettingDesc s_viewSettingDescs[CTR_VIEW_SETTING_COUNT] = {
    {"fov", "FOV", CTR_VIEW_STOCK_FOV, CTR_VIEW_RAW_AT(CTR_VIEW_STOCK_FOV, CTR_VIEW_PERCENT_MIN),
     CTR_VIEW_RAW_AT(CTR_VIEW_STOCK_FOV, CTR_VIEW_PERCENT_MAX), 0, 1},

    {"vertical", "VERT CULL", CTR_VIEW_STOCK_VERTICAL, CTR_VIEW_RAW_AT(CTR_VIEW_STOCK_VERTICAL, CTR_VIEW_PERCENT_MIN),
     CTR_VIEW_RAW_AT(CTR_VIEW_STOCK_VERTICAL, CTR_VIEW_PERCENT_MAX), 0, 1},

    {"near", "NEAR", CTR_VIEW_STOCK_NEAR, CTR_VIEW_RAW_AT(CTR_VIEW_STOCK_NEAR, CTR_VIEW_PERCENT_MIN),
     CTR_VIEW_RAW_AT(CTR_VIEW_STOCK_NEAR, CTR_VIEW_PERCENT_MAX), 0, 1},

    {"trackdist", "TRACK DIST", CTR_VIEW_STOCK_TRACK_DIST, CTR_VIEW_RAW_AT(CTR_VIEW_STOCK_TRACK_DIST, CTR_VIEW_PERCENT_MIN),
     CTR_VIEW_TRACK_DIST_OFF, 1, 1},

    {"far", "CULL BOX", CTR_VIEW_STOCK_FAR, CTR_VIEW_RAW_AT(CTR_VIEW_STOCK_FAR, CTR_VIEW_PERCENT_MIN),
     CTR_VIEW_RAW_AT(CTR_VIEW_STOCK_FAR, CTR_VIEW_PERCENT_MAX), 0, 1},
};

// The three sets. Not three lists of numbers - three uses of one macro, which
// is five uses of the defines above.
#define CTR_VIEW_STOCK_SET  {{CTR_VIEW_STOCK_FOV, CTR_VIEW_STOCK_VERTICAL, CTR_VIEW_STOCK_NEAR, CTR_VIEW_STOCK_TRACK_DIST, CTR_VIEW_STOCK_FAR}}

global_variable struct CTR_ViewSettings s_viewSettings[CTR_ASPECT_MODE_COUNT] = {
    CTR_VIEW_STOCK_SET,
    CTR_VIEW_STOCK_SET,
    CTR_VIEW_STOCK_SET,
};

// Which set the render path reads. Moved once, when the shape changes, rather
// than worked out per frustum corner - and it is a row index and never -1, so
// there is no per-read branch for "what if the aspect is none of the three".
global_variable int s_viewSettingsActive = (int)CTR_ASPECT_MODE_4_3;

void CTR_View_UpdateActiveSettings(void)
{
	int mode = CTR_View_ActiveMode();

	if (mode < 0)
	{
		// A custom aspect from --aspect W:H. It gets the nearest shape's set,
		// by the same arithmetic that picks a shape for a monitor - rather than
		// a fourth set nobody has ever looked at.
		mode = CTR_View_NearestMode(s_worldAspectWidth, s_worldAspectHeight);
	}

	if (mode < 0)
	{
		mode = (int)CTR_ASPECT_MODE_4_3;
	}

	s_viewSettingsActive = mode;
}

int CTR_View_SettingsMode(void)
{
	return s_viewSettingsActive;
}

int CTR_View_SettingCount(void)
{
	return (int)CTR_VIEW_SETTING_COUNT;
}

const char *CTR_View_SettingLabel(int setting)
{
	if ((setting < 0) || (setting >= (int)CTR_VIEW_SETTING_COUNT))
	{
		return "?";
	}

	return s_viewSettingDescs[setting].label;
}

const char *CTR_View_SettingKey(int setting)
{
	if ((setting < 0) || (setting >= (int)CTR_VIEW_SETTING_COUNT))
	{
		return "?";
	}

	return s_viewSettingDescs[setting].key;
}

int CTR_View_SettingValue(int mode, int setting)
{
	if ((mode < 0) || (mode >= (int)CTR_ASPECT_MODE_COUNT) || (setting < 0) || (setting >= (int)CTR_VIEW_SETTING_COUNT))
	{
		return 0;
	}

	return s_viewSettings[mode].value[setting];
}

// Clamped on the way in, and it is the only way in. A value that arrives from
// the menu, from the config file or from a flag later cannot leave a number
// behind that the render path would divide by.
void CTR_View_SetSettingValue(int mode, int setting, int value)
{
	const struct CTR_ViewSettingDesc *desc;

	if ((mode < 0) || (mode >= (int)CTR_ASPECT_MODE_COUNT) || (setting < 0) || (setting >= (int)CTR_VIEW_SETTING_COUNT))
	{
		return;
	}

	desc = &s_viewSettingDescs[setting];

	if (value < desc->minimum)
	{
		value = desc->minimum;
	}
	if (value > desc->maximum)
	{
		value = desc->maximum;
	}

	s_viewSettings[mode].value[setting] = value;
}

// Whether this row is read and turned as a share of stock.
int CTR_View_SettingIsPercent(int setting)
{
	if ((setting < 0) || (setting >= (int)CTR_VIEW_SETTING_COUNT))
	{
		return 0;
	}

	return s_viewSettingDescs[setting].percent;
}

// The stored value as a share of stock, rounded. 100 is exactly stock, and
// exactly stock is what the row shows the moment the game starts.
int CTR_View_SettingPercent(int mode, int setting)
{
	const struct CTR_ViewSettingDesc *desc;

	if ((mode < 0) || (mode >= (int)CTR_ASPECT_MODE_COUNT) || (setting < 0) || (setting >= (int)CTR_VIEW_SETTING_COUNT))
	{
		return 0;
	}

	desc = &s_viewSettingDescs[setting];

	if (desc->stock == 0)
	{
		return 0;
	}

	return ((s_viewSettings[mode].value[setting] * 100) + (desc->stock / 2)) / desc->stock;
}

// One step up or down.
//
// A percent row lands on the next multiple of the step rather than adding the
// step to wherever it happens to stand. That is what keeps the grid: a value
// that arrived from a config file written by an older build, or that sat on a
// clamped bound, joins the grid on its first turn instead of walking beside it
// forever - and 100 is on the grid, so stock is always reachable by counting.
//
// A percent row CLAMPS rather than wraps. It wrapped when this menu had no
// direction; it has one now, and one step past the bottom landing on twice the
// field of view is a picture nobody asked for and a menu nobody trusts.
void CTR_View_StepSettingValue(int mode, int setting, int direction)
{
	const struct CTR_ViewSettingDesc *desc;
	int value;

	if ((mode < 0) || (mode >= (int)CTR_ASPECT_MODE_COUNT) || (setting < 0) || (setting >= (int)CTR_VIEW_SETTING_COUNT) || (direction == 0))
	{
		return;
	}

	desc = &s_viewSettingDescs[setting];

	if (desc->percent)
	{
		const int step = CTR_VIEW_PERCENT_STEP;
		const int top = CTR_VIEW_RAW_AT(desc->stock, CTR_VIEW_PERCENT_MAX);
		int percent;
		int wanted;

		// OFF sits one step above the top of the grid and is left the same way.
		// It is a position and not a share, so it is reached by stepping rather
		// than by counting - and the row that can turn melting off entirely does
		// not need the other page to get there.
		if (desc->hasOff)
		{
			if (s_viewSettings[mode].value[setting] >= desc->maximum)
			{
				if (direction < 0)
				{
					s_viewSettings[mode].value[setting] = top;
				}

				return;
			}

			if ((direction > 0) && (s_viewSettings[mode].value[setting] >= top))
			{
				s_viewSettings[mode].value[setting] = desc->maximum;
				return;
			}
		}

		percent = CTR_View_SettingPercent(mode, setting);

		if (direction > 0)
		{
			wanted = ((percent / step) + 1) * step;
		}
		else
		{
			wanted = (((percent + step - 1) / step) - 1) * step;
		}

		// The grid, and it clamps rather than wrapping. One step past the bottom
		// landing on twice the field of view is a picture nobody asked for.
		if (wanted < CTR_VIEW_PERCENT_MIN)
		{
			wanted = CTR_VIEW_PERCENT_MIN;
		}
		if (wanted > CTR_VIEW_PERCENT_MAX)
		{
			wanted = CTR_VIEW_PERCENT_MAX;
		}

		value = CTR_VIEW_RAW_AT(desc->stock, wanted);

		if (value < desc->minimum)
		{
			value = desc->minimum;
		}
		if (value > desc->maximum)
		{
			value = desc->maximum;
		}

		s_viewSettings[mode].value[setting] = value;
		return;
	}

	// Every row is a share, so there is no second way of stepping and no branch
	// pretending there might be. A row that is not a share would need one, and
	// leaving a raw fallback standing for a case that does not exist is how a
	// dead branch survives long enough to be trusted.
	value = s_viewSettings[mode].value[setting] + direction;

	if (value < desc->minimum)
	{
		value = desc->minimum;
	}
	if (value > desc->maximum)
	{
		value = desc->maximum;
	}

	s_viewSettings[mode].value[setting] = value;
}

void CTR_View_ResetSettings(int mode)
{
	int setting;

	if ((mode < 0) || (mode >= (int)CTR_ASPECT_MODE_COUNT))
	{
		return;
	}

	for (setting = 0; setting < (int)CTR_VIEW_SETTING_COUNT; setting++)
	{
		s_viewSettings[mode].value[setting] = s_viewSettingDescs[setting].stock;
	}
}

// Whether a set still holds what the render path used before any of this
// existed. The menu marks a changed set, so "the picture looks wrong" and "I
// turned something" cannot be confused with each other.
int CTR_View_SettingsChanged(int mode)
{
	int setting;

	if ((mode < 0) || (mode >= (int)CTR_ASPECT_MODE_COUNT))
	{
		return 0;
	}

	for (setting = 0; setting < (int)CTR_VIEW_SETTING_COUNT; setting++)
	{
		if (s_viewSettings[mode].value[setting] != s_viewSettingDescs[setting].stock)
		{
			return 1;
		}
	}

	return 0;
}

//----------------------------------------------------------------------------------------
// WHAT THE RENDER PATH ASKS FOR
//
// Five rows, each asked where a literal used to sit. They read the active set
// and nothing else - no shape is named below this line.

// One multiply and one shift, spelled once, used by the three rows that scale
// something. At factor 0x100 it returns its argument unchanged for every input
// - which is what lets five rows all sit at 100 % and the picture be the picture
// the game draws without this file.
internal int CTR_View_ApplyFactor(int value, int factor)
{
	if (factor == CTR_VIEW_FACTOR_ONE)
	{
		return value;
	}

	return (int)(((s64)value * factor) >> CTR_VIEW_FACTOR_SHIFT);
}

// THE PROJECTION DISTANCE, SCALED BY THE FOV ROW.
//
// Called from the two places in CAM.c that turn distanceToScreen_CURR into
// _PREV, and from nowhere else. _PREV is what every consumer reads - the
// projection, the cull frustum, the near clip, the detail thresholds and the
// two dozen sprite paths that load it into GTE control register 26 - so one
// scaled write covers all of them and none of them can disagree.
//
// Two consequences worth naming. It is turned while driving, because CAM runs
// every frame and PushBuffer_Init does not. And it multiplies what the camera
// decided rather than replacing it, so the speed zoom survives.
//
// What it does NOT reach: the title screen, the character select, the garage,
// the podium and the cutscenes, which write _PREV themselves, and the split
// screens, whose viewport rows carry their own distance. This row is the
// one-player race.
int CTR_View_ProjectionDistance(int distanceToScreen)
{
	return CTR_View_ApplyFactor(distanceToScreen, s_viewSettings[s_viewSettingsActive].value[CTR_VIEW_SETTING_FOV]);
}

// The near clip threshold, scaled after each site has computed it its own way.
// Three places compute it and they do not agree on how - two add one afterwards
// and one does not, two shift an unsigned copy and one shifts a signed int - so
// what is shared is the scaling and not the expression. Each site keeps its own
// casts and its own plus one, and at 0x100 each is character for character the
// number it was.
//
// Below 0x100 it draws wrong, and that is reachable on purpose. The GTE's
// perspective divide returns nothing usable once the projected depth reaches
// half the projection distance, and the stock threshold is exactly one above
// that edge - so anything under 100 % lets geometry through that cannot be
// projected, and a vertex out of a clamped divide is thrown across the screen.
int CTR_View_NearClip(int threshold)
{
	return CTR_View_ApplyFactor(threshold, s_viewSettings[s_viewSettingsActive].value[CTR_VIEW_SETTING_NEAR]);
}

// THE NEAR PLANE, AND WHY IT IS TWICE AS LARGE AS ITS NAME SAYS.
//
// The track renderer has always clipped against a plane, and it does it
// right: it splits instead of discarding. That is not immediately visible when
// reading, because two tricks lie on top of each other.
//
// FIRST, IT CLIPS IN VIEW SPACE AND NOT ON THE SCREEN.
// Ovr226_800aa858_ProjectClipRecordRawVertex sends the vertex through
// LLV0BK - a transformation WITHOUT projection - and stores the result as
// pos[0..2]. What then stands there as posScreen and depth are
// view-space coordinates, not screen coordinates, and
// Ovr226_800aab00_InterpolateClipRecordVertex mixes exactly these. The new
// vertex therefore really lies on the plane and not on a line on the
// screen.
//
// SECOND, EVERYTHING IS DOUBLED. Ovr226_800aaad0 shifts x, y and z left by one
// before it stores them. A projection is scale-invariant -
// (2x, 2y, 2z) lands at the same screen position as (x, y, z) - so the point is
// not lost in the process. Two things are gained: s16 carries one bit more right in front of the
// camera, and the doubled depth comes to lie above the
// division edge H/2, where the coprocessor gives up.
//
// HENCE THE 129. The threshold is held against the DOUBLED depth, so
// the plane lies at threshold/2. (H>>1)+1 = 129 at H = 256 means: plane
// at 64.5, doubled 129, and 129 is exactly one above the edge 128 at which
// gte_divide stops dividing. The game's near plane was not chosen,
// it is glued to the limit of the coprocessor.
//
// WHAT THIS SWITCH DOES. It detaches it from that. --near-plane N sets the
// plane to the view-space depth N; the threshold becomes 2*N, so that the
// doubling keeps working out. That only makes sense together with
// --gte-near-div: without it the division saturates below 128
// doubled depth, and whatever is let in closer flies across
// the screen instead of appearing.
//
// Default 0, and 0 means: the caller keeps its own expression.
// The two places in 226_00_DrawLevelOvr1P.c do NOT compute it the same way - the
// one shifts unsigned, the other signed - and this
// detour touches neither of them, it passes through.
int g_cfg_nearPlane = 0;

int CTR_View_NearPlaneThreshold(int stockThreshold)
{
	if (g_cfg_nearPlane <= 0)
	{
		return stockThreshold;
	}

	return g_cfg_nearPlane * 2;
}

// THE COUNT OF NEAR LOSSES.
//
// Four places can lose a face that touches the near plane, and
// they do it for four different reasons. Whoever only sees that a corner is
// black does not know which of them it was; here each stands on its own.
//
// halfNear   - all vertices lie in front of the plane. The face is really
//              gone and belongs gone. No fault but the plane at
//              work.
// box        - DrawLevelOvr1P_SourceInsideClipRecordWindow. The clip record
//              is ONLY written when at least one vertex lies in a
//              box of +-0x100/+-0x180/+-0x100 around the camera position.
//              That is a saving measure of the PS1 on the clip buffer, not
//              geometry. What falls here falls without a geometric reason.
// allNear    - the clip record was there, but in the consumer all
//              corners did lie in front of the plane.
// noSpace    - the clip buffer was full.
//
// Plus the two denominators: how many faces touched the plane at all,
// and how many of them were split.
//
// SWITCH: --near-report, default off, and it changes NOTHING in the picture. It may
// therefore also run along in the comparison run.
int g_cfg_nearReport = 0;

global_variable long long s_nearTouched;
global_variable long long s_nearSplit;
global_variable long long s_nearDropHalf;
global_variable long long s_nearDropBox;
global_variable long long s_nearDropAll;
global_variable long long s_nearDropSpace;
global_variable long long s_nearMarked;

void CTR_NearClip_NoteTouched(void)
{
	if (g_cfg_nearReport)
	{
		s_nearTouched++;
	}
}

void CTR_NearClip_NoteSplit(void)
{
	if (g_cfg_nearReport)
	{
		s_nearSplit++;
	}
}

// A face on which at least one vertex lies in front of the threshold. NOT
// the same as "touches the plane" further up: that only counts the faces
// that get as far as the clip record. This one counts all, and the difference
// between the two numbers is exactly the amount that does not even reach the clip
// - because it turns off at one of the eight places in 226_00_DrawLevelOvr1P.c
// that make out of a near vertex not a split but a
// SUBDIVISION DECISION: directMask = DIRECT_QUAD instead of the selection through
// Ovr226_800a3b90/800a3c70. The threshold there is not a clip but a
// detail switch - and that is why a shifted plane changes the picture even
// where nothing is clipped at all.
void CTR_NearClip_NoteMarked(void)
{
	if (g_cfg_nearReport)
	{
		s_nearMarked++;
	}
}

void CTR_NearClip_NoteDropHalfNear(void)
{
	if (g_cfg_nearReport)
	{
		s_nearDropHalf++;
	}
}

void CTR_NearClip_NoteDropBox(void)
{
	if (g_cfg_nearReport)
	{
		s_nearDropBox++;
	}
}

void CTR_NearClip_NoteDropAllNear(void)
{
	if (g_cfg_nearReport)
	{
		s_nearDropAll++;
	}
}

void CTR_NearClip_NoteDropNoSpace(void)
{
	if (g_cfg_nearReport)
	{
		s_nearDropSpace++;
	}
}

// THE BOX NO LONGER DISCARDS, IT ONLY SLOWS DOWN.
//
// The box in DrawLevelOvr1P_ShouldWriteRenderedClippedRecord is a
// saving measure on the clip buffer of the PS1, not a geometric criterion. At
// site 8 it discards 86-93 % of the faces that go there - those are the
// holes in the lower corners. Drawing raw was no answer (built on 2026-09-04
// as --near-box-fallback, measured and removed on 2026-09-05): the vertices
// of these faces are unprojectable by construction, 96 % stand at SZ3 == 0
// (measured 2026-09-04, Crash Cove).
//
// --near-box-keep lets the face into the clip record anyway. The
// consumer splits it in view space and projects the intersection points anew -
// the only way that is right for such vertices. In exchange the
// writer Ovr226_800a898c gets the space test it never had: without the box
// nothing else stands between it and the buffer end. Default off, and off
// means: the writer takes exactly the path from before, space test
// included - that only exists with the switch.
//
// What the space test throws away and what the consumer leaves lying is
// counted. Either would be the next hole.
//
// DEFAULT ON, since the evening of 2026-09-05. Measured: the switch is the only
// fix for the open corners bottom left and right, and those are open in every
// format; every verdict of the day ("almost perfect", runs 11 to 18) was
// driven with it; the space test discards 1.1 % (4:3, 896 of 81,915) to
// 1.7 % (43:18, 1,470 of 85,251) of the records, the consumer never stalled in
// 1,800 VBlanks, the vertex peak per frame is the same (8,844 at
// 43:18 with and without). What changes at 4:3 against the previous code is exactly the
// corners. Off still means: the
// writer takes exactly the path from before; --near-box-drop is the way back.
int g_cfg_nearBoxKeep = 1;

int CTR_NearClip_BoxKeep(void)
{
	return g_cfg_nearBoxKeep;
}

global_variable long long s_keepKept[2];    // written despite the box
global_variable long long s_keepNoSpace[2]; // no room left, discarded
global_variable long long s_consumerStops;  // consumer stopped
global_variable long long s_consumerLeft;   // records left lying in the process

void CTR_NearClip_NoteDynKept(int count)
{
	if (g_cfg_nearReport)
	{
		s_keepKept[(count == 4) ? 1 : 0]++;
	}
}

void CTR_NearClip_NoteDynKeepNoSpace(int count)
{
	if (g_cfg_nearReport)
	{
		s_keepNoSpace[(count == 4) ? 1 : 0]++;
	}
}

void CTR_NearClip_NoteConsumerStopped(long long recordsLeft)
{
	if (g_cfg_nearReport)
	{
		s_consumerStops++;
		s_consumerLeft += recordsLeft;
	}
}

void CTR_NearClip_KeepReport(void)
{
	if (!g_cfg_nearReport)
	{
		return;
	}

	Platform_Log("[CTR Near] box keep: GT3 %lld kept past the box, %lld dropped for space; GT4 %lld kept, %lld dropped for space\n",
	             s_keepKept[0], s_keepNoSpace[0], s_keepKept[1], s_keepNoSpace[1]);
	Platform_Log("[CTR Near]   consumer stopped %lld time(s) for prim reserve, %lld record(s) left undrawn\n", s_consumerStops, s_consumerLeft);
}

// THE TWO SILENT EXITS OF THE WRITER OF SITE 8.
//
// Ovr226_800a898c_WriteDynamicRenderedClippedRecordAtOtEntry reports success
// twice without having written anything: at otEntry == NULL and when
// DrawLevelOvr1P_ShouldWriteRenderedClippedRecord is false. In the second case
// no vertex lay in the box of +-0x100/+-0x180/+-0x100 around projectedCenter -
// a saving measure on the clip buffer of the PS1, not a geometric criterion.
//
// The existing counter NoteDropBox does not see that: it sits in
// DrawLevelOvr1P_WriteRenderedClippedRecordAtOt, a different writer that
// nothing calls at the 2026-09-04 anchor (NoteTouched reports 0).
//
// The first exit is unreachable from the code - both callers, line 6759
// and 6782, check inheritedOtEntry beforehand and return. It is
// counted anyway: an exit that one only BELIEVES is never taken
// is not a measured exit.
//
// wrote counts the third case, so that the three numbers together add up to the calls
// and no gap remains. Separated by count, because the same writer
// serves site 7 (triangles) and site 8 (quads).
global_variable long long s_dynNoOtEntry;
global_variable long long s_dynOutsideBox[2];
global_variable long long s_dynWrote[2];

void CTR_NearClip_NoteDynNoOtEntry(void)
{
	if (g_cfg_nearReport)
	{
		s_dynNoOtEntry++;
	}
}

void CTR_NearClip_NoteDynOutsideBox(int count)
{
	if (g_cfg_nearReport)
	{
		s_dynOutsideBox[(count == 4) ? 1 : 0]++;
	}
}

void CTR_NearClip_NoteDynWrote(int count)
{
	if (g_cfg_nearReport)
	{
		s_dynWrote[(count == 4) ? 1 : 0]++;
	}
}

void CTR_NearClip_DynWriterReport(void)
{
	if (!g_cfg_nearReport)
	{
		return;
	}

	Platform_Log("[CTR Near] dynamic clip writer: %lld with no ot entry (unreachable from both callers)\n", s_dynNoOtEntry);
	Platform_Log("[CTR Near]   GT3 (site 7): %lld outside the record box, %lld written\n", s_dynOutsideBox[0], s_dynWrote[0]);
	Platform_Log("[CTR Near]   GT4 (site 8): %lld outside the record box, %lld written\n", s_dynOutsideBox[1], s_dynWrote[1]);
	CTR_NearClip_KeepReport();
}

void CTR_NearClip_CutSiteReport(void);

void CTR_NearClip_Report(void)
{
	if (!g_cfg_nearReport)
	{
		return;
	}

	Platform_Log("[CTR Near] plane %d (0 means each site keeps its own stock threshold) - %lld faces had a near vertex, %lld of them reached the clip record, %lld split\n",
	             g_cfg_nearPlane, s_nearMarked, s_nearTouched, s_nearSplit);
	Platform_Log("[CTR Near] dropped: %lld wholly in front, %lld outside the record box, %lld all-near in consumer, %lld no record space\n", s_nearDropHalf,
	             s_nearDropBox, s_nearDropAll, s_nearDropSpace);
	CTR_NearClip_DynWriterReport();
	CTR_NearClip_CutSiteReport();
	Platform_LogFlush();
}

// THE CLIP BUFFER OF THE NEAR PLANE - ONE SIZE, ALWAYS COUNTED.
//
// One rule for allocation (MainInit_JitPools) and end
// (DrawLevelOvr1P_GetClipRecordEnd): the table of the slot times
// CTR_CLIP_STOCK_FACTOR, plus for a container the track's surcharge from
// its LEV - four records per quadblock, see RLD_CLIP_RECORD_GT4_BYTES in
// rldtrack.inc. --tracks-fixed-memory and --no-tracks take both back,
// as with the draw memory: then the buffer is stock, byte for byte.
//
// Until 2026-09-15 the number came solely from MainDB_GetClipSize by the
// level ID, so for every container from the table of the slot: 12,000
// bytes, 200 GT4 records per frame. On Crash Cove at 43:18 the space was already
// missing 1,470 times in 700 frames, on a track with
// coarser BSP correspondingly more often - and what found no room was a
// face that cuts the near plane: the road under the kart.
//
// THE FACTOR FOR THE DISC TRACKS (2026-09-16). The surcharge of 2026-09-15 applied
// only to containers. A test run the same evening on Papu's Pyramid (disc,
// 43:18, 5,213 frames) says in the exit line: "clip records peak 12000 of
// 12000 bytes in a frame (200 of 200 GT4 records); 0 GT3 + 1034 GT4 record(s)
// found no room in 32 frame(s)". The table of the slot is sized for 4:3;
// at 43:18 the camera sees 918 instead of 512 columns and cuts correspondingly
// more faces at the near plane. Four instead of one: 800 GT4 records for the
// 3000-word table. The factor is CHOSEN, not measured - the true
// peak value stood behind the cap at 200 and was not in the log; the
// [CTR Clip] exit line names it from now on. Not sized by the format,
// because the format can be switched at run time (debug menu, VIDEO -> ASPECT),
// but the buffer is only allocated in MainInit: the allocation has to carry the widest
// format. The memory for it lies BEHIND the window of the pack
// (CTR_ClipStockSurchargeBytes, counted in main.c towards the container reserve);
// the window itself stays the console.
//
// COMPUTED ONCE, NOT PER RECORD. DrawLevelOvr1P_HasClipRecordSpace calls this
// function at each of the seven writers, so per clip record (Vista: 1,890 per
// frame, acceptance test 2026-09-15), and NativeTrack_ClipBytes re-read the
// LEV header each time for it (Rld_MemNeed: quadblock count, plus eight sky segments
// sorted). The value only changes with level, player count or loaded
// container - exactly that is the key of the cache. The rule
// stays one function in two places; it just no longer computes per call.
#define CTR_CLIP_STOCK_FACTOR 4

global_variable struct
{
	int valid;
	u32 levelID;
	int numPlyr;
	int track;
	int bytes;
} s_clipBytesCache;

int CTR_ClipBufferBytes(u32 levelID, int numPlyrCurrGame)
{
	extern int g_cfg_tracksFixedMemory;
	extern int g_cfg_tracks;
	int NativeTrack_ActiveForLevel(int levelID);
	int NativeTrack_LoadedIndex(void);
	u32 NativeTrack_ClipBytes(void);

	const int track = NativeTrack_LoadedIndex();
	int bytes;

	if (s_clipBytesCache.valid && (s_clipBytesCache.levelID == levelID) && (s_clipBytesCache.numPlyr == numPlyrCurrGame) && (s_clipBytesCache.track == track))
	{
		return s_clipBytesCache.bytes;
	}

	bytes = MainDB_GetClipSize(levelID, numPlyrCurrGame) << 2;

	if (!g_cfg_tracksFixedMemory && g_cfg_tracks)
	{
		bytes *= CTR_CLIP_STOCK_FACTOR;

		if (NativeTrack_ActiveForLevel((int)levelID))
		{
			bytes += (int)NativeTrack_ClipBytes();
		}
	}

	s_clipBytesCache.valid = 1;
	s_clipBytesCache.levelID = levelID;
	s_clipBytesCache.numPlyr = numPlyrCurrGame;
	s_clipBytesCache.track = track;
	s_clipBytesCache.bytes = bytes;

	return bytes;
}

// The reserve behind the window for the factor above: four slots in
// data.PtrClipBuffer, each (factor - 1) times the largest entry of the table of the
// slot. Computed BEFORE MEMPACK_Init, when nobody knows yet which level
// comes - hence the largest entry and not that of the level. The bound
// 64 lies above every case of the switch in MainDB_GetClipSize; everything above it
// delivers its default and cannot raise the maximum.
u32 CTR_ClipStockSurchargeBytes(void)
{
	u32 most = 0;
	int id;

	for (id = 0; id < 64; id++)
	{
		const u32 one = (u32)MainDB_GetClipSize((u32)id, 1) << 2;
		const u32 many = (u32)MainDB_GetClipSize((u32)id, 4) << 2;

		if (one > most)
		{
			most = one;
		}
		if (many > most)
		{
			most = many;
		}
	}

	return 4u * (u32)(CTR_CLIP_STOCK_FACTOR - 1) * most;
}

// The counters for it run WITHOUT a switch. A record without room is a face
// missing from the picture, and a fault that is only counted under --near-report
// is not counted - the same rule as for the split report of the GPU.
global_variable long long s_clipDropNoSpace[2]; // [0] GT3, [1] GT4 - all seven writers
global_variable long long s_clipFrameDiscards;   // the consumer found the pointer behind the end
global_variable int s_clipFrameHadDrop;
global_variable int s_clipFramesWithDrop;
global_variable int s_clipPeakBytes;
global_variable int s_clipCapacityBytes;
global_variable int s_clipDropSaid; // the first ten with VBlank, so that a capture finds its way there

void CTR_Clip_NoteDropNoSpace(int count)
{
	int Platform_GetVBlankCount(void);

	s_clipDropNoSpace[(count == 4) ? 1 : 0]++;
	s_clipFrameHadDrop = 1;

	if (s_clipDropSaid < 10)
	{
		s_clipDropSaid++;
		Platform_Log("[CTR Clip] vblank %d: no room for a clip record (%d vertices) - a near face is not drawn here\n", Platform_GetVBlankCount(), count);
	}
}

// The frame is over at the consumer - whether it split or discarded the records.
// Until 2026-09-16 the frame mark was only reset in the split path, and
// a discarded frame would have attached its drop to the next frame.
internal void CTR_Clip_FrameEnded(void)
{
	if (s_clipFrameHadDrop)
	{
		s_clipFramesWithDrop++;
		s_clipFrameHadDrop = 0;
	}
}

void CTR_Clip_NoteFrameDiscard(void)
{
	s_clipFrameDiscards++;
	CTR_Clip_FrameEnded();
}

void CTR_Clip_NoteFrameUse(int usedBytes, int capacityBytes)
{
	if (usedBytes > s_clipPeakBytes)
	{
		s_clipPeakBytes = usedBytes;
	}

	s_clipCapacityBytes = capacityBytes;
	CTR_Clip_FrameEnded();
}

void CTR_Clip_PrintReport(void)
{
	const int recordBytes = 0x3c;

	Platform_Log("[CTR Clip] at exit: clip records peak %d of %d bytes in a frame (%d of %d GT4 records); %lld GT3 + %lld GT4 record(s) found no room in %d frame(s); %lld frame(s) discarded whole by the consumer\n",
	             s_clipPeakBytes, s_clipCapacityBytes, s_clipPeakBytes / recordBytes, s_clipCapacityBytes / recordBytes, s_clipDropNoSpace[0], s_clipDropNoSpace[1],
	             s_clipFramesWithDrop, s_clipFrameDiscards);
}

// THE DETAIL DECISION, SEPARATE FROM THE CLIP.
//
// A near vertex triggers TWO different things in the track renderer, and
// the threshold in DrawLevelOvr1P_SetProjectedDepth feeds both. A probe
// that only moves the threshold therefore always moves both at once and
// cannot say which of the two had the effect.
//
// FIRST, THE DETAIL DECISION. Ten places make out of a near
// vertex not a split but a coarser drawing: directMask =
// DRAW_LEVEL_OVR1P_DIRECT_QUAD instead of the finer selection through
// Ovr226_800a3b90/800a3c70. No clip record is written in the process. The
// face is drawn as one quad, not as the pair of triangles that
// the selection would have chosen.
//
// SECOND, THE CLIP - thirteen other places that write a clip record.
// For that there is CTR_NearClip_CutSplits.
//
// --near-detail-off takes its branch from the first group: the caller falls
// through to the normal mask selection, as if the vertex were not near. The
// clip stays untouched, the threshold stays untouched, and
// CTR_NearClip_NoteMarked keeps counting what would have happened without the switch.
//
// Default off. Off means: the return value is the argument, so exactly the
// number the call site would have checked itself without this detour.
int g_cfg_nearDetailOff = 0;

int CTR_NearClip_DetailForcesQuad(int stockNear)
{
	if (g_cfg_nearDetailOff)
	{
		return 0;
	}

	return stockNear;
}

// THE OTHER EFFECT OF THE NEAR PLANE, ALONE.
//
// Thirteen places make a clip record out of a near vertex: they call
// one of the Write...ClippedRecord... functions and leave the face to the
// consumer, which splits it against the plane. That is the clip in the
// literal sense, and it is something different from the detail decision above.
//
// --near-cut-off takes its branch from this group: instead of going into
// the clip record the face goes unsplit to EmitPreparedProjected...RawCode..., so
// drawn as it was projected. The detail decision stays
// untouched, the threshold stays untouched.
//
// At the 2026-09-02 anchor ZERO of 572,439 marked faces reached the
// counted clip record writer. If that also holds in the driving scene,
// this switch must change nothing there - and exactly that is the measurement.
//
// Default off, and off means: the return value is the argument.
int g_cfg_nearCutOff = 0;

// THE THIRTEEN INDIVIDUALLY.
//
// --near-cut-off takes the branch from all thirteen at once, and at the
// 2026-09-04 anchor that fills the two undrawn patches in the lower corners
// - 3,365 points, left 627 at y 751..769, right 2738 at y 784..832. Which
// of the thirteen does that, the collective switch does not say. It also costs the
// bottom twelve rows, which it clears over the full width.
//
// Hence a mask instead of a switch. Bit i belongs to site i of
// s_nearCutSiteName; if it is set, the site takes its clip record as
// before, if it is zero, it falls through to EmitPreparedProjected...RawCode....
// The default is all thirteen set, and then the return value is the
// argument - exactly the number the call site would have checked itself
// without this detour.
//
// --near-cut-off stays and takes precedence: it is the same as mask 0.
//
// The two counters hang on --near-report like all others and change nothing
// in the picture. WOULD counts how often the site saw a near vertex,
// TOOK how often it really took the clip record. With the full
// mask both are equal; the difference is exactly what the mask
// took away. A site with WOULD 0 cannot be responsible for anything,
// and that is available before any picture comparison.
#define CTR_NEAR_CUT_SITES 13

unsigned int g_cfg_nearCutMask = (1u << CTR_NEAR_CUT_SITES) - 1u;

global_variable long long s_nearCutWould[CTR_NEAR_CUT_SITES];
global_variable long long s_nearCutTook[CTR_NEAR_CUT_SITES];

// The name is the line in 226_00_DrawLevelOvr1P.c and the function it
// stands in. Six groups: water direct, Ground4x1, Ground4x2, Dynamic,
// Quad4x4, water - once each for triangles (GT3) and once for quads
// (GT4), except for the first three.
local_persist const char *const s_nearCutSiteName[CTR_NEAR_CUT_SITES] = {
    "3902 water EmitSignedClipProjectedTriDirect",
    "3930 water EmitNonzeroClipProjectedQuadDirect",
    "3959 water EmitPreparedProjectedQuadDirectCode",
    "4971 Ground4x1 GT3",
    "4994 Ground4x1 GT4",
    "6295 Ground4x2 GT3",
    "6318 Ground4x2 GT4",
    "6757 Dynamic GT3",
    "6780 Dynamic GT4",
    "7185 Quad4x4 GT3",
    "7208 Quad4x4 GT4",
    "9627 Water GT3",
    "9648 Water GT4",
};

int CTR_NearClip_CutSplitsAt(int site, int stockNear)
{
	const int known = (site >= 0) && (site < CTR_NEAR_CUT_SITES);
	int take;

	if (g_cfg_nearCutOff)
	{
		take = 0;
	}
	else if (known && ((g_cfg_nearCutMask & (1u << site)) == 0u))
	{
		take = 0;
	}
	else
	{
		take = stockNear;
	}

	if (g_cfg_nearReport && known)
	{
		if (stockNear != 0)
		{
			s_nearCutWould[site]++;
		}

		if (take != 0)
		{
			s_nearCutTook[site]++;
		}
	}

	return take;
}

void CTR_NearClip_CutSiteReport(void)
{
	int i;

	if (!g_cfg_nearReport)
	{
		return;
	}

	Platform_Log("[CTR Near] cut sites, mask 0x%04x (bit i = site i takes its clip record)\n", g_cfg_nearCutMask);

	for (i = 0; i < CTR_NEAR_CUT_SITES; i++)
	{
		Platform_Log("[CTR Near]   %2d %-46s near %8lld, clipped %8lld%s\n", i, s_nearCutSiteName[i], s_nearCutWould[i], s_nearCutTook[i],
		             ((g_cfg_nearCutMask & (1u << i)) == 0u) ? "   (mask off)" : "");
	}
}

// The distance past which a BSP leaf melts, from the projection distance the
// caller already has. Retail multiplies by 0x1a; this multiplies by the stored
// number and shifts eight, and the stored number is 0x1a00, so the product is
// the same product.
int CTR_View_TrackDistance(int distanceToScreen)
{
	const int value = s_viewSettings[s_viewSettingsActive].value[CTR_VIEW_SETTING_TRACK_DIST];

	if (value >= CTR_VIEW_TRACK_DIST_OFF)
	{
		// Above the GTE's own clamp on SZ3, so no leaf can ever exceed it. Said
		// once here rather than left to a multiply that would overflow into it
		// by accident.
		return CTR_VIEW_TRACK_DIST_OFF;
	}

	return (int)(((s64)distanceToScreen * value) >> CTR_VIEW_FACTOR_SHIFT);
}

// Whether this row stands at the far end of its range, where its number stops
// being a distance and becomes "never". Only TRACK DIST has such an end, and it
// has one because the end is provable rather than large.
int CTR_View_SettingIsOff(int mode, int setting)
{
	if ((mode < 0) || (mode >= (int)CTR_ASPECT_MODE_COUNT) || (setting < 0) || (setting >= (int)CTR_VIEW_SETTING_COUNT))
	{
		return 0;
	}

	if (!s_viewSettingDescs[setting].hasOff)
	{
		return 0;
	}

	return s_viewSettings[mode].value[setting] >= s_viewSettingDescs[setting].maximum;
}

// Whether melting is off entirely. The LOD page's TRACK row is this question and
// its answer - it does not keep a bit of its own, because "leaves stop melting"
// and "the melt distance is past the far end" are one fact and this file is
// where it lives.
int CTR_View_TrackDistIsOff(void)
{
	return s_viewSettings[s_viewSettingsActive].value[CTR_VIEW_SETTING_TRACK_DIST] >= CTR_VIEW_TRACK_DIST_OFF;
}

// Set from the LOD page, and for every shape rather than the active one: the
// TRACK row is not a per-shape thought, and a bit that came out of a config file
// written by an older build has to land somewhere definite.
void CTR_View_SetTrackDistOff(int off)
{
	int mode;

	for (mode = 0; mode < (int)CTR_ASPECT_MODE_COUNT; mode++)
	{
		s_viewSettings[mode].value[CTR_VIEW_SETTING_TRACK_DIST] = (off != 0) ? CTR_VIEW_TRACK_DIST_OFF : CTR_VIEW_STOCK_TRACK_DIST;
	}
}

// The divisor of the frustum's vertical half-extent. Never zero: the clamp on
// the way in is what guarantees that, and this is divided by.
int CTR_View_VerticalSquash(void)
{
	const int value = s_viewSettings[s_viewSettingsActive].value[CTR_VIEW_SETTING_VERTICAL];

	return (value > 0) ? value : s_viewSettingDescs[CTR_VIEW_SETTING_VERTICAL].stock;
}

int CTR_View_FrustumFar(void)
{
	return s_viewSettings[s_viewSettingsActive].value[CTR_VIEW_SETTING_FAR];
}


// The fallback, for an element no row of the declaration table claims.
//
// The thirds of the canvas, applied to the element box centre: an element
// authored hard against an edge has its centre in that edge's third. It is a
// rule that answers a question nobody wrote down, and it answers it from the
// DRAWN box - so an element whose content grows can cross a boundary and change
// anchor with nothing having been decided.
//
// It stays as the fallback rather than being replaced by it, and the number of
// elements still falling back on it is logged. That number is the answer to
// "how much of this screen is still guesswork".
int CTR_UI_AnchorForBox(int x0, int x1, int virtualWidth)
{
	int centre;

	if (virtualWidth <= 0)
	{
		return (int)CTR_UI_ANCHOR_CENTRE;
	}

	centre = (x0 + x1) / 2;

	if ((centre * 3) < virtualWidth)
	{
		return (int)CTR_UI_ANCHOR_LEFT;
	}

	if ((centre * 3) >= (virtualWidth * 2))
	{
		return (int)CTR_UI_ANCHOR_RIGHT;
	}

	return (int)CTR_UI_ANCHOR_CENTRE;
}

//----------------------------------------------------------------------------------------
// THE PLAYER MENUS: ANCHOR BY THE EDGE RULE
//
// The menus have a rule of their own, and it is fixed (decided on
// 2026-09-25): an element that lies at most a tenth of the width away from an
// edge in the 4:3 picture holds that edge. If it lies at both
// edges, it does not fit the rule and stays centred. Everything else is
// centred. The thirds rule above, by contrast, pulls everything in the left or right
// third to the edge, even a box that stands 100 points away from it -
// measured, that way it tore apart rows, titles and columns in driver select,
// high score, cup select, battle, garage and memory card.
//
// ONLY IN THE MENUS, and exactly where the game says so itself:
// gameMode1 & MAIN_MENU. Set for the menu level and the garage
// (LOAD_TenStages.c:329-341), cleared in races, pause, loading and on the
// results screens. The race HUD therefore decides bit for bit as before.
int CTR_UI_MenuMode(void)
{
	return (sdata != NULL) && (sdata->gGT != NULL) && ((sdata->gGT->gameMode1 & MAIN_MENU) != 0);
}

// The edge rule itself. Distance on the left is x0, on the right virtualWidth - x1 (x1 is
// the right corner, so the edge behind the last column). A tenth of
// 512 is 51.2, so at most 51 in integers.
//
// THE SHIFT RISES WITH THE POSITION. Left 0, centre (W-512)/2, right W-512:
// if a box wanders to the right, "left" can only drop away and "right" can only
// be added, and "both edges" lies with centre in between. An element that
// crosses the boundary while flying in or out therefore jumps at most in
// its own direction of flight, never back. And what lies entirely outside on the right in 4:3
// (x0 >= 512) gets right and lies entirely outside on the right of the
// canvas; entirely outside on the left gets left and stays outside on the left.
//
// SINCE THE ZONE THE LAST SENTENCE NO LONGER HOLDS BY ITSELF. With the zone the
// sequence is left (W-Z)/2, centre (W-512)/2, right (W-Z)/2 + Z-512 - it keeps
// rising, so an element still only jumps in its direction of flight, at 918
// columns by 85 instead of 203. But left and right lie 118 columns before the
// picture edge, and what is just outside would be visible.
// CTR_UI_MenuEdgeTransit below catches that.
int CTR_UI_AnchorForBoxEdge(int x0, int x1, int virtualWidth)
{
	const int margin = virtualWidth / 10;
	int atLeft;
	int atRight;

	if (virtualWidth <= 0)
	{
		return (int)CTR_UI_ANCHOR_CENTRE;
	}

	atLeft = (x0 <= margin);
	atRight = ((virtualWidth - x1) <= margin);

	if (atLeft && !atRight)
	{
		return (int)CTR_UI_ANCHOR_LEFT;
	}

	if (atRight && !atLeft)
	{
		return (int)CTR_UI_ANCHOR_RIGHT;
	}

	return (int)CTR_UI_ANCHOR_CENTRE;
}

// The shift of an anchor in canvas columns: 0, (W-512)/2 or W-512.
// Read from CTR_UI_MapX and not written out once more.
int CTR_UI_AnchorShift(const struct CTR_UIViewParameters *view, int anchor)
{
	return CTR_UI_MapX(view, 0, anchor);
}

// AN ELEMENT WITH A FIXED ANCHOR THAT GLIDES OVER THE 4:3 EDGE.
//
// On a change the menus fly their boxes out of the picture by up to 512 points
// and back in (MM_TransitionInOut, MM_MenuFlow.c:3-40). A centre-
// anchored box that is just entirely outside in 4:3 (x0 = 512) would lie on
// the wide canvas at 512 + (W-512)/2 - in the middle of the picture, and it would vanish
// there instead of flying out.
//
// That is why the shift grows while the element crosses the edge: by
// the share that is already outside, from its anchor up to the shift
// that means "entirely outside" on the canvas - right W-512, left 0. If it is
// entirely outside, it is also entirely outside on the canvas; if it stands in the picture,
// it is exactly its anchor. The element only gets faster in the process, not wider.
//
// It is needed for elements whose anchor a screen prescribes firmly, and
// since the zone also for the edge rule (CTR_UI_MenuEdgeTransit). "Entirely outside"
// is always the edge of the CANVAS, not of the zone. At 4:3 every
// shift is 0 and this the identity.
int CTR_UI_TransitShift(const struct CTR_UIViewParameters *view, int anchorShift, int x0, int x1)
{
	const int width = x1 - x0;
	int outside;

	if ((view == NULL) || (width <= 0))
	{
		return anchorShift;
	}

	if (x1 > view->virtualWidth)
	{
		const int farShift = view->canvasWidth - view->virtualWidth;

		outside = x1 - view->virtualWidth;
		if (outside > width)
		{
			outside = width;
		}

		return anchorShift + (int)(((s64)(farShift - anchorShift) * outside) / width);
	}

	if (x0 < 0)
	{
		outside = -x0;
		if (outside > width)
		{
			outside = width;
		}

		return anchorShift - (int)(((s64)anchorShift * outside) / width);
	}

	return anchorShift;
}

// THE TRANSIT OF THE EDGE RULE, SINCE THE ZONE EXISTS.
//
// Without a zone the edge rule needed none: LEFT was the left picture edge and
// RIGHT the right one, what lay outside in 4:3 lay outside on the canvas.
// With the zone both hold its edges, and at 918 columns those lie 118
// before the picture edge. Measured on the captures of 2026-09-28:
// the band ends of the high score (-20..-17 and 529..544, outside in 4:3) would stand
// visibly in the background at 98 and 817, and a box that flies over the 4:3 edge
// would vanish at the zone edge instead of at the picture edge.
//
// So the same transit as for the screen rules: proportionally from the
// zone position up to "entirely outside" on the canvas. What comes back is the addition
// to the anchor, not the whole shift.
//
// 0 as long as the zone is the canvas - at 4:3, at 16:9 and outside the
// menus -, and 0 for CENTRE, which the zone did not change. There
// every picture therefore stays as it was.
int CTR_UI_MenuEdgeTransit(const struct CTR_UIViewParameters *view, int anchor, int x0, int x1)
{
	int shift;

	if ((view == NULL) || (view->zoneLeft == 0))
	{
		return 0;
	}

	if ((anchor != (int)CTR_UI_ANCHOR_LEFT) && (anchor != (int)CTR_UI_ANCHOR_RIGHT))
	{
		return 0;
	}

	shift = CTR_UI_AnchorShift(view, anchor);

	return CTR_UI_TransitShift(view, shift, x0, x1) - shift;
}

// THE SAME FOR A CENTRED ELEMENT OUTSIDE THE UI PUSH BUFFER.
//
// The LOADING lettering (RaceFlag_DrawLoadingString) draws at the root of the
// swapchain OT, where the UI mapper does not see it (native_gpu.c, end of the
// UI section). It computes its letters as in 4:3 in the 512 space - fly-in
// from the right, rest in the middle, fly-out to the left - and brings each onto
// the canvas with this: at rest by (W-512)/2, and whatever is outside in 4:3 is outside
// here too. Built like CTR_UI_MapAuthoredX. At 4:3 the result is 0.
int CTR_UI_CentreTransitShift(int x0, int x1)
{
	struct CTR_UIViewParameters view;

	if (!CTR_UI_BuildSafeArea(&view, CTR_CANVAS_REFERENCE_WIDTH, CTR_Canvas_ActiveHeight()))
	{
		return 0;
	}

	return CTR_UI_TransitShift(&view, CTR_UI_AnchorShift(&view, (int)CTR_UI_ANCHOR_CENTRE), x0, x1);
}

// Defined in game/native_menuscreen.c, which the unity build includes after the
// menu overlay - it needs the character select's own constants. Read by
// PushBuffer_SetDrawEnv_Normal (game/PushBuffer.c), which comes earlier.
int NativeMenuWindow_ShiftFor(const struct PushBuffer *pb);

//----------------------------------------------------------------------------------------
// THE LEVEL OF DETAIL
//
// Four separate mechanisms swap detail by distance. They sit in three different
// files, they compare three different quantities against six different
// thresholds, and none of them knows about the others. A switch that names one
// of them is a switch that half works, so this names all four:
//
//   TEX     the quadblock texture. Four TextureLayouts lie one behind the other
//           at each face pointer and the render path walks FORWARD through them
//           as the face comes closer - entry 0 is the far one, entry 3 the near
//           one. 226_00_DrawLevelOvr1P.c, two selectors.
//   TRACK   the BSP leaf's render-list slot. Past one distance a leaf is put in
//           the full-dynamic bucket, which walks the midpoints of its 3x3 grid
//           toward the straight line between their neighbours and fades the
//           colour with them - a quadblock melting into a flat quad.
//           RenderLevel/RenderLists.c.
//   MODEL   which of a model's headers is drawn. Header 0 is the finest, and
//           each header carries the distance up to which it is the right one.
//           RenderBucket/RenderBucket_QueueExecute.c.
//   SUBDIV  the near subdivision. This one is the other way round: it ADDS
//           detail close up rather than removing it far away. Forcing it means
//           subdividing the whole picture and not only the near part of it, so
//           it is the one stage that is off unless it is asked for.
//
// A mask and not one level, because the four cost wildly different amounts and
// the only way to find out which one is worth its price is to be able to turn
// them on one at a time and measure between.
//
// It lives in this file because this file is the first thing the unity build
// includes and therefore the only place all four call sites can see. It is
// deliberately NOT a fourth per-shape setting: how far a texture stays sharp has
// nothing to do with how wide the picture is, and three copies of one number is
// the shape of a bug that has not happened yet.

enum CTR_LodStage
{
	CTR_LOD_STAGE_TEXTURE = 0,
	CTR_LOD_STAGE_TRACK,
	CTR_LOD_STAGE_MODEL,
	CTR_LOD_STAGE_SUBDIV,
	CTR_LOD_STAGE_COUNT,
};

struct CTR_LodStageDesc
{
	const char *key;
	const char *label;
};

global_variable const struct CTR_LodStageDesc s_lodStageDescs[CTR_LOD_STAGE_COUNT] = {
    {"tex", "LOD TEX"},
    {"track", "LOD TRACK"},
    {"model", "LOD MODEL"},
    {"subdiv", "LOD SUBDIV"},
};

#define CTR_LOD_STAGE_BIT(stage) (1 << (stage))
#define CTR_LOD_MASK_ALL         ((1 << CTR_LOD_STAGE_COUNT) - 1)

// The three that swap CONTENT are on; the one that adds tessellation is not.
//
// TEX, TRACK and MODEL each exchange one thing for a different thing at a
// distance - a coarser texture, a flatter quadblock, another mesh - and that
// exchange is what is seen as a jump from muddy to sharp. SUBDIV exchanges
// nothing; it splits what is already there, and with the affine texture mapping
// this renderer still has, that split is what keeps near-camera textures from
// warping. Turning it on everywhere would tessellate the whole visible track
// every frame to fix a warp that is only visible in the first few metres.
#define CTR_LOD_MASK_DEFAULT (CTR_LOD_STAGE_BIT(CTR_LOD_STAGE_TEXTURE) | CTR_LOD_STAGE_BIT(CTR_LOD_STAGE_MODEL))

// TRACK IS NOT IN THE MASK, AND THAT IS THE WHOLE POINT.
//
// "Leaves stop melting" and "the melt distance is past the far end" are one
// fact, and TRACK DIST on the VIEW page is where it is kept. The bit is
// synthesised on the way out and consumed on the way in, so the LOD row, the
// config file's `video lod` number and the VIEW row cannot disagree - there is
// nothing for them to disagree about.
//
// Dropping it from the default is what puts TRACK DIST at 100 % on a fresh
// start. A config file from before this carries the bit, and reading it turns
// the distance off, so a picture that was being driven stays the picture.
#define CTR_LOD_MASK_STORED (CTR_LOD_MASK_ALL & ~CTR_LOD_STAGE_BIT(CTR_LOD_STAGE_TRACK))

global_variable int s_lodMask = CTR_LOD_MASK_DEFAULT;

//----------------------------------------------------------------------------------------
// NO LOD: THE MECHANISM OFF, NOT A DEFAULT THAT SAYS SO
//
// The mask above is a measuring instrument - four stages, turned one at a time,
// so the price of each can be found. It was also carrying the answer, and that
// is what broke: TRACK moved out of the mask and into the VIEW page's TRACK DIST
// row, which is PER SHAPE. From then on "melting is off" was three values, and
// a shape whose value was not turned kept melting while the other two did not.
// One fact in three places, and one of them fell behind - for the third time in
// this tree.
//
// So this is not a fifth bit and not a better default. It is one boolean that
// says the whole mechanism is out, and it is read at every site that chooses by
// distance. It cannot be split across shapes because it is not per shape, and it
// cannot be lost to an old config file because a file that does not mention it
// leaves the built-in value - which is on.
//
// The mask still works underneath and the old picture is one flag away, because
// a comparison that cannot be made is a claim that cannot be checked.
//
// WHAT IT DOES NOT COVER, on purpose:
//
//   SUBDIV   is not a swap. The other stages exchange one thing for a coarser
//            thing at a distance; SUBDIV splits what is already there, near the
//            camera, to keep affine texture mapping from warping. Forcing it
//            means tessellating the whole visible track every frame - a large
//            cost that fixes neither the texture pop nor the blocky karts. It
//            stays reachable by name through the mask.
//
//   The animation guard in the model walk, and the UI push buffer. Both are
//   places where a header index is a CHOICE and not a level of detail, and both
//   cost a day each to find. See RenderBucket_QueueExecute.c.
global_variable int s_lodNone = 1;

int CTR_Lod_NoneMode(void)
{
	return s_lodNone;
}

void CTR_Lod_SetNoneMode(int on)
{
	s_lodNone = (on != 0);
}

// Whether a stage is answered by the no-LOD mode rather than by its bit.
//
// Written as a list of the three rather than as "everything except SUBDIV", so
// that a fifth stage added later is not swept in by an else.
internal int CTR_Lod_StageCoveredByNoneMode(int stage)
{
	return (stage == (int)CTR_LOD_STAGE_TEXTURE) || (stage == (int)CTR_LOD_STAGE_TRACK) || (stage == (int)CTR_LOD_STAGE_MODEL);
}

// THE TIRES, which were never a stage and are the second half of the blocky
// karts.
//
// DrawTires drops a kart's wheels entirely once the instance's chosen header
// index passes a threshold - two in one or two player, ZERO above that. It is
// not a coarser wheel, it is no wheel, and it is keyed on the same header index
// the model walk writes into idpp->lodIndex.
//
// That coupling is why forcing MODEL did not fix it. Where the animation guard
// holds a coarse header - which is exactly the animated things, so exactly the
// drivers - the index stays high and the wheels stay gone. A kart body without
// wheels at distance is the block.
//
// Its own question rather than a fifth stage bit: it is not a level of detail
// with steps, it is a yes or no, and the mask is a table of stages that have
// levels.
int CTR_Lod_TiresForced(void)
{
	return s_lodNone;
}

int CTR_Lod_StageCount(void)
{
	return (int)CTR_LOD_STAGE_COUNT;
}

const char *CTR_Lod_StageKey(int stage)
{
	if ((stage < 0) || (stage >= (int)CTR_LOD_STAGE_COUNT))
	{
		return "?";
	}

	return s_lodStageDescs[stage].key;
}

const char *CTR_Lod_StageLabel(int stage)
{
	if ((stage < 0) || (stage >= (int)CTR_LOD_STAGE_COUNT))
	{
		return "?";
	}

	return s_lodStageDescs[stage].label;
}

int CTR_Lod_Mask(void)
{
	return s_lodMask | (CTR_View_TrackDistIsOff() ? CTR_LOD_STAGE_BIT(CTR_LOD_STAGE_TRACK) : 0);
}

// Masked on the way in, and it is the only way in. A config file edited by hand
// or a flag with a typo cannot leave a bit standing that no stage answers to.
//
// The TRACK bit is consumed rather than stored, and only in the direction that
// says something: a mask carrying it turns the melt distance off, a mask without
// it leaves whatever the VIEW row holds. Otherwise reading `video lod 5` out of
// a file would wipe a turned TRACK DIST two lines before the file gets to it.
void CTR_Lod_SetMask(int mask)
{
	if ((mask & CTR_LOD_STAGE_BIT(CTR_LOD_STAGE_TRACK)) != 0)
	{
		CTR_View_SetTrackDistOff(1);
	}

	s_lodMask = mask & CTR_LOD_MASK_STORED;
}

void CTR_Lod_ToggleStage(int stage)
{
	if ((stage < 0) || (stage >= (int)CTR_LOD_STAGE_COUNT))
	{
		return;
	}

	if (stage == (int)CTR_LOD_STAGE_TRACK)
	{
		// The row on this page and the row on VIEW are two controls for one
		// number. Off means the far end; on means back to stock, which is the
		// only value this control can name - a turned distance is turned on the
		// page that shows the number.
		CTR_View_SetTrackDistOff(!CTR_View_TrackDistIsOff());
		return;
	}

	s_lodMask ^= CTR_LOD_STAGE_BIT(stage);
}

// THE question, asked in four places. Inlined by any compiler and written as a
// call anyway, because a call site that reads `if (CTR_Lod_StageForced(...))`
// says what it is doing and `if (s_lodMask & 2)` does not.
int CTR_Lod_StageForced(int stage)
{
	if ((stage < 0) || (stage >= (int)CTR_LOD_STAGE_COUNT))
	{
		return 0;
	}

	// Before the mask and before TRACK DIST, because the whole point of the mode
	// is that no per-stage and no per-shape value can take it back. A stage this
	// covers is not asked about; it is answered.
	if (s_lodNone && CTR_Lod_StageCoveredByNoneMode(stage))
	{
		return 1;
	}

	if (stage == (int)CTR_LOD_STAGE_TRACK)
	{
		return CTR_View_TrackDistIsOff();
	}

	return (s_lodMask & CTR_LOD_STAGE_BIT(stage)) != 0;
}

// "stock", "alle", or a comma-separated list of stage keys. Returns 0 and
// touches nothing when a name is not a stage - a flag that silently accepts a
// typo is a run that measured something else than it says it did.
int CTR_Lod_ParseMask(const char *text, int *outMask)
{
	const char *cursor = text;
	int mask = 0;

	if ((text == NULL) || (text[0] == '\0'))
	{
		return 0;
	}

	if (strcmp(text, "stock") == 0)
	{
		if (outMask != NULL)
		{
			*outMask = 0;
		}
		return 1;
	}

	if ((strcmp(text, "alle") == 0) || (strcmp(text, "all") == 0))
	{
		if (outMask != NULL)
		{
			*outMask = CTR_LOD_MASK_ALL;
		}
		return 1;
	}

	while (*cursor != '\0')
	{
		const char *comma = strchr(cursor, ',');
		const size_t length = (comma != NULL) ? (size_t)(comma - cursor) : strlen(cursor);
		int stage;
		int found = 0;

		for (stage = 0; stage < (int)CTR_LOD_STAGE_COUNT; stage++)
		{
			const char *key = s_lodStageDescs[stage].key;

			if ((strlen(key) == length) && (strncmp(key, cursor, length) == 0))
			{
				mask |= CTR_LOD_STAGE_BIT(stage);
				found = 1;
				break;
			}
		}

		if (!found)
		{
			return 0;
		}

		if (comma == NULL)
		{
			break;
		}

		cursor = comma + 1;
	}

	if (outMask != NULL)
	{
		*outMask = mask;
	}

	return 1;
}

//----------------------------------------------------------------------------------------
// WHAT IT COSTS, COUNTED RATHER THAN ARGUED
//
// One frame is not a measurement, so this counts a window of frames and prints
// totals and per-frame averages once. What it counts is what the four
// mechanisms decide, plus the one number that says how much geometry actually
// came through: the level primitive count, incremented where the overlay itself
// increments its own.
//
// The texture counters carry a second number beside the level histogram: how
// often the level the stock path WOULD have chosen holds different bytes from
// the finest one. That is the answer to "is there anything to gain at all" -
// a face whose four layouts are identical has no better level to be forced to,
// and no amount of switching will sharpen it.

#define CTR_LOD_REPORT_DEFAULT_FRAMES 60
#define CTR_LOD_MODEL_LEVELS          8

// The window repeats rather than firing once, because one window is one corner
// of one lap. Six of them ten seconds apart cover a whole lap from a single
// drive, and six numbers next to each other say which part of the track is the
// expensive one - which is the question "where does it get tight" actually asks.
#define CTR_LOD_REPORT_WINDOWS 6
#define CTR_LOD_REPORT_GAP     600

global_variable int s_lodReportAtVBlank = 0;
global_variable int s_lodReportFrames = 0;
global_variable int s_lodCounting = 0;
global_variable int s_lodFramesCounted = 0;
global_variable int s_lodReportWindowsLeft = 0;
global_variable int s_lodWindowIndex = 0;

global_variable int s_lodTexLevel[4];
global_variable int s_lodTexDrawn[4];
global_variable int s_lodTexCoarser;
global_variable int s_lodTexCoarserDiffers;
global_variable long long s_lodTexExtent[4];
global_variable int s_lodTexExtentCount[4];
global_variable int s_lodSlot[8];
global_variable int s_lodSlotWouldFullDynamic;
global_variable int s_lodModelLevel[CTR_LOD_MODEL_LEVELS];
global_variable int s_lodModelDrawn[CTR_LOD_MODEL_LEVELS];

// Why a refinement was refused, sorted. See CTR_Lod_NoteModelHeld.
enum CTR_LodAnimCase
{
	CTR_LOD_ANIM_MORE = 0,
	CTR_LOD_ANIM_SAME,
	CTR_LOD_ANIM_FEWER,
	CTR_LOD_ANIM_UNCOMPARABLE,
	CTR_LOD_ANIM_CASE_COUNT,
};

global_variable const char *const s_lodAnimCaseNames[CTR_LOD_ANIM_CASE_COUNT] = {"more", "same", "fewer", "n/a"};
global_variable int s_lodModelHeldAnim[CTR_LOD_ANIM_CASE_COUNT];
global_variable int s_lodModelUpgraded;
global_variable int s_lodModelExhausted;
global_variable int s_lodModelHeld;
global_variable int s_lodLevelPrims;
global_variable int s_lodPrimReserveFails;
global_variable int s_lodTireSets;
global_variable int s_lodTireWouldSkip;

int CTR_Lod_Counting(void)
{
	return s_lodCounting;
}

void CTR_Lod_ArmReport(int vblank, int frames)
{
	s_lodReportAtVBlank = vblank;
	s_lodReportFrames = (frames > 0) ? frames : CTR_LOD_REPORT_DEFAULT_FRAMES;
	s_lodReportWindowsLeft = CTR_LOD_REPORT_WINDOWS;
}

internal void CTR_Lod_ZeroCounters(void)
{
	int i;

	for (i = 0; i < 4; i++)
	{
		s_lodTexLevel[i] = 0;
		s_lodTexDrawn[i] = 0;
		s_lodTexExtent[i] = 0;
		s_lodTexExtentCount[i] = 0;
	}
	for (i = 0; i < 8; i++)
	{
		s_lodSlot[i] = 0;
	}
	for (i = 0; i < CTR_LOD_MODEL_LEVELS; i++)
	{
		s_lodModelLevel[i] = 0;
		s_lodModelDrawn[i] = 0;
	}

	s_lodTexCoarser = 0;
	s_lodTexCoarserDiffers = 0;
	s_lodSlotWouldFullDynamic = 0;
	s_lodModelExhausted = 0;
	s_lodModelHeld = 0;
	s_lodModelUpgraded = 0;

	for (i = 0; i < (int)CTR_LOD_ANIM_CASE_COUNT; i++)
	{
		s_lodModelHeldAnim[i] = 0;
	}
	s_lodLevelPrims = 0;
	s_lodPrimReserveFails = 0;
	s_lodTireSets = 0;
	s_lodTireWouldSkip = 0;
	s_lodFramesCounted = 0;
}

// ASKED FOR, AND DRAWN. THEY ARE TWO NUMBERS.
//
// This took stockLevel alone and the report printed it under a heading that
// read like an outcome. It is not an outcome - it is the request, and while a
// stage is forced the two differ on every single face. A report that shows only
// the request looks identical whether the forcing works or does nothing at all,
// which is exactly the reading that cost a night: the eye saw coarse detail, the
// numbers looked unchanged, and neither could contradict the other because they
// were not answering the same question.
//
// stockLevel is what the distance thresholds asked for, drawnLevel what the
// caller is about to use, finestLevel what the data allows.
void CTR_Lod_NoteTexture(int stockLevel, int drawnLevel, int finestLevel, int differs)
{
	if (!s_lodCounting)
	{
		return;
	}

	if ((stockLevel >= 0) && (stockLevel < 4))
	{
		s_lodTexLevel[stockLevel]++;
	}

	if ((drawnLevel >= 0) && (drawnLevel < 4))
	{
		s_lodTexDrawn[drawnLevel]++;
	}

	if (stockLevel < finestLevel)
	{
		s_lodTexCoarser++;
		if (differs)
		{
			s_lodTexCoarserDiffers++;
		}
	}
}

// The slot a leaf landed in, and separately whether the distance test WOULD
// have sent it to the full-dynamic bucket. The second number is the whole cost
// of the TRACK stage: it is exactly the leaves that stopped melting.
void CTR_Lod_NoteTrackSlot(int slot, int wouldBeFullDynamic)
{
	if (!s_lodCounting)
	{
		return;
	}

	if ((slot >= 0) && (slot < 8))
	{
		s_lodSlot[slot]++;
	}

	if (wouldBeFullDynamic)
	{
		s_lodSlotWouldFullDynamic++;
	}
}

// The same two numbers, for the model headers, and for the same reason.
//
// requestedIndex is where the distance walk stopped; drawnIndex is the header
// actually handed back. Equal when nothing is forced and equal again whenever
// the forcing declined - so the gap between the two rows IS the mechanism
// working, and a report where they match while a stage says "finest" is a
// report saying the stage is not reaching the picture.
void CTR_Lod_NoteModel(int requestedIndex, int drawnIndex, int exhausted)
{
	if (!s_lodCounting)
	{
		return;
	}

	if (exhausted)
	{
		s_lodModelExhausted++;
		return;
	}

	if ((drawnIndex >= 0) && (drawnIndex < CTR_LOD_MODEL_LEVELS))
	{
		s_lodModelDrawn[drawnIndex]++;
	}

	if (drawnIndex != requestedIndex)
	{
		s_lodModelUpgraded++;
	}

	if ((requestedIndex >= 0) && (requestedIndex < CTR_LOD_MODEL_LEVELS))
	{
		s_lodModelLevel[requestedIndex]++;
	}
}

// How many texels one level's UV box covers, summed so an average can be
// printed. Four levels whose areas fall by four each step are a mip chain by
// any other name.
void CTR_Lod_NoteTextureExtent(int level, int texels)
{
	if (!s_lodCounting || (level < 0) || (level >= 4))
	{
		return;
	}

	s_lodTexExtent[level] += texels;
	s_lodTexExtentCount[level]++;
}

// A model the MODEL stage wanted to sharpen and did not, because the finer
// header's animation would have wrapped the instance's animation word somewhere
// else. That word is game state, so this number is the price of leaving game
// state alone - and if it is zero, the rule cost nothing on this track.
// AND WHY EACH REFUSAL HAPPENED - the question that was open until 2026-08-29
// and is now answered. It came out `more:0` in every window, which is the reason
// the animation guard was left exactly as it is. The reading of that result, and
// what it says the headers actually are, is written where somebody editing the
// model path will stand: RenderBucket_QueueExecute.c, at the forcing block.
//
// The sorting stays because the answer is one track's answer. Another track can
// contradict it, and then the number says so instead of the idea being had a
// second time from scratch.
//
// HOW TO READ THE REPORT'S OTHER MODEL NUMBERS, and one trap that was walked
// into: instances drawn coarse are NOT all refusals. The difference between the
// coarse columns of `model drawn` and `held` is the other case - every finer
// header carrying maxDistanceLOD 0, which is a header somebody selected on
// purpose and not a detail level at all.
//
// That difference came out as exactly 120 in three consecutive windows of 120
// frames each, and was read here as "exactly one instance per frame". It is not.
// The next lap gave 120, 79 and 0 for the same quantity. Three equal numbers
// were a property of that stretch of track, not a law. The CLASS is real; the
// constant was not.
//
// The four buckets:
//
//   more    the fine header holds MORE frames - the only recoverable share, and
//           the one that measured zero
//   same    equal counts. Should be ZERO, because equal already swaps. Anything
//           here means the refusal came from something other than the count, and
//           the assumption the whole idea rests on is wrong.
//   fewer   the fine header holds FEWER - the index would fall outside its data.
//           Not recoverable this way.
//   n/a     one of the two has no animation for this index at all. Swapping
//           would stop or start a movement, which is not a detail change.
//
// fineFrames and coarseFrames are frame counts, or -1 where the header has no
// animation to take a count from.
void CTR_Lod_NoteModelHeld(int wouldHaveChanged, int fineFrames, int coarseFrames)
{
	int which;

	if (!s_lodCounting || !wouldHaveChanged)
	{
		return;
	}

	s_lodModelHeld++;

	if ((fineFrames < 0) || (coarseFrames < 0))
	{
		which = CTR_LOD_ANIM_UNCOMPARABLE;
	}
	else if (fineFrames > coarseFrames)
	{
		which = CTR_LOD_ANIM_MORE;
	}
	else if (fineFrames == coarseFrames)
	{
		which = CTR_LOD_ANIM_SAME;
	}
	else
	{
		which = CTR_LOD_ANIM_FEWER;
	}

	s_lodModelHeldAnim[which]++;
}

void CTR_Lod_NoteLevelPrimitive(void)
{
	if (s_lodCounting)
	{
		s_lodLevelPrims++;
	}
}

// One kart's set of wheels, and whether the header-index threshold would have
// dropped it. The second number is the whole cost of the tire rule the way
// `would melt` is the whole cost of TRACK: it is exactly the wheels that are
// being drawn now and were not before.
void CTR_Lod_NoteTires(int wouldSkip)
{
	if (!s_lodCounting)
	{
		return;
	}

	s_lodTireSets++;

	if (wouldSkip)
	{
		s_lodTireWouldSkip++;
	}
}

// A bucket that could not reserve its share of the primitive buffer and gave up
// on the rest of itself. Anything above zero means the picture is missing
// geometry, and it is the one counter that is a fault rather than a cost.
void CTR_Lod_NotePrimReserveFail(void)
{
	if (s_lodCounting)
	{
		s_lodPrimReserveFails++;
	}
}

internal void CTR_Lod_PrintReport(void)
{
	const int frames = (s_lodFramesCounted > 0) ? s_lodFramesCounted : 1;
	int stage;
	int i;

	Platform_Log("[CTR Lod] ---- window %d, %d frames ----\n", s_lodWindowIndex, s_lodFramesCounted);

	// Once, in the first window. Which stages are in force cannot change during
	// a run that nobody is touching, and repeating it six times would bury the
	// six numbers that do change.
	if (s_lodWindowIndex == 0)
	{
		// The mode first, and each stage says whether the mode or its own bit is
		// answering for it. A row that only said "forced high" would not tell
		// which of the two, and the whole reason the mode exists is that the
		// per-stage answer turned out to be reachable from somewhere else.
		Platform_Log("[CTR Lod]   NO LOD     %s\n", s_lodNone ? "on - the mechanism is out" : "off - stages answer for themselves");

		for (stage = 0; stage < (int)CTR_LOD_STAGE_COUNT; stage++)
		{
			const int byMode = s_lodNone && CTR_Lod_StageCoveredByNoneMode(stage);

			Platform_Log("[CTR Lod]   %-10s %s\n", s_lodStageDescs[stage].label,
			             byMode ? "finest, by NO LOD" : (CTR_Lod_StageForced(stage) ? "forced high, by its own bit" : "stock"));
		}

		Platform_Log("[CTR Lod]   %-10s %s\n", "TIRES", CTR_Lod_TiresForced() ? "always drawn, by NO LOD" : "dropped past the header threshold");
	}

	Platform_Log("[CTR Lod]   level prims  %8d total, %6d per frame\n", s_lodLevelPrims, s_lodLevelPrims / frames);
	Platform_Log("[CTR Lod]   prim buffer  %d bucket walks gave up for want of room%s\n", s_lodPrimReserveFails,
	             (s_lodPrimReserveFails != 0) ? "   WARNING: geometry is missing from the picture" : "");

	// TWO ROWS, AND THE SECOND ONE IS THE ANSWER.
	//
	// `asked` is the distance thresholds talking; `drawn` is what went into the
	// picture. While TEX is forced the second row must be flat against the
	// finest column, and if it is not, the stage is not reaching the faces
	// whatever the header line at the top of the window claims.
	Platform_Log("[CTR Lod]   tex asked    0:%d  1:%d  2:%d  3:%d\n", s_lodTexLevel[0], s_lodTexLevel[1], s_lodTexLevel[2], s_lodTexLevel[3]);
	Platform_Log("[CTR Lod]   tex drawn    0:%d  1:%d  2:%d  3:%d   <- this is what the picture got\n", s_lodTexDrawn[0], s_lodTexDrawn[1], s_lodTexDrawn[2],
	             s_lodTexDrawn[3]);
	Platform_Log("[CTR Lod]   tex coarser  %d selections below the finest, %d of them hold different bytes\n", s_lodTexCoarser, s_lodTexCoarserDiffers);

	// The average UV box of each level, in texels. Falling by roughly four per
	// step is a mip chain drawn by hand; staying flat means the levels are four
	// different pictures and forcing the finest costs nothing but detail.
	Platform_Log("[CTR Lod]   tex texels  ");
	for (i = 0; i < 4; i++)
	{
		if (s_lodTexExtentCount[i] != 0)
		{
			Platform_Log(" %d:%lld", i, s_lodTexExtent[i] / s_lodTexExtentCount[i]);
		}
	}
	Platform_Log("%s\n", "   (average UV box per level)");

	Platform_Log("[CTR Lod]   leaf slots   4x4:%d  subdiv:%d  4x2:%d  4x1:%d  water:%d  fulldyn:%d\n", s_lodSlot[0], s_lodSlot[1], s_lodSlot[2], s_lodSlot[3],
	             s_lodSlot[4], s_lodSlot[5]);
	Platform_Log("[CTR Lod]   would melt   %d leaves past the distance threshold\n", s_lodSlotWouldFullDynamic);

	Platform_Log("[CTR Lod]   model asked ");
	for (i = 0; i < CTR_LOD_MODEL_LEVELS; i++)
	{
		if (s_lodModelLevel[i] != 0)
		{
			Platform_Log(" %d:%d", i, s_lodModelLevel[i]);
		}
	}
	Platform_Log("   beyond all levels: %d\n", s_lodModelExhausted);

	// The row that says whether the mechanism reached the picture. `upgraded` is
	// how often the two rows differed for one instance; `held` is how often a
	// finer header existed and was refused because taking it would have moved
	// the animation wrap - game state, which a detail switch may not touch.
	Platform_Log("[CTR Lod]   model drawn ");
	for (i = 0; i < CTR_LOD_MODEL_LEVELS; i++)
	{
		if (s_lodModelDrawn[i] != 0)
		{
			Platform_Log(" %d:%d", i, s_lodModelDrawn[i]);
		}
	}
	Platform_Log("   <- this is what the picture got. upgraded: %d, held back by animation: %d\n", s_lodModelUpgraded, s_lodModelHeld);

	// Why each refusal happened, and therefore what a narrower rule would buy.
	// `more` is the recoverable share and nothing else is; `same` must be zero;
	// the sum must be `held`, and it says so rather than being trusted.
	{
		int sum = 0;

		Platform_Log("[CTR Lod]   held why    ");
		for (i = 0; i < (int)CTR_LOD_ANIM_CASE_COUNT; i++)
		{
			Platform_Log(" %s:%d", s_lodAnimCaseNames[i], s_lodModelHeldAnim[i]);
			sum += s_lodModelHeldAnim[i];
		}
		Platform_Log("   (fine header vs coarse; 'more' is what a narrower rule would recover)%s\n",
		             (sum == s_lodModelHeld) ? "" : "   WARNING: the sum is not held, a case is missing");
	}

	// The second half of the blocky karts, in the same shape as `would melt`.
	// The right-hand number is the work this rule adds - and while the mode is
	// off it is instead the number of karts driving around without wheels.
	Platform_Log("[CTR Lod]   tire sets    %d reached the threshold, %d of them are past it (%d per frame)\n", s_lodTireSets, s_lodTireWouldSkip,
	             s_lodTireWouldSkip / frames);
}

// Called once per frame with the VBlank the frame ended on.
void CTR_Lod_Tick(int vblank)
{
	if (s_lodReportAtVBlank <= 0)
	{
		return;
	}

	if (!s_lodCounting)
	{
		if (vblank < s_lodReportAtVBlank)
		{
			return;
		}

		CTR_Lod_ZeroCounters();
		s_lodCounting = 1;
		return;
	}

	s_lodFramesCounted++;

	if (s_lodFramesCounted < s_lodReportFrames)
	{
		return;
	}

	CTR_Lod_PrintReport();
	s_lodCounting = 0;
	s_lodWindowIndex++;
	s_lodReportWindowsLeft--;

	if (s_lodReportWindowsLeft <= 0)
	{
		s_lodReportAtVBlank = 0;
		return;
	}

	s_lodReportAtVBlank = vblank + CTR_LOD_REPORT_GAP;
}

#endif
