#include <common.h>

enum RaceFlagScratchConstants
{
	RACE_FLAG_SCREEN_ROWS = 10,
	RACE_FLAG_SCREEN_POINTS_PER_ROW = 3,
};

enum RaceFlagConstants
{
	RACE_FLAG_POSITION_ONSCREEN = 0,
	RACE_FLAG_POSITION_OFFSCREEN = 5000,
	RACE_FLAG_POSITION_OFFSCREEN_LEFT = -5000,
	RACE_FLAG_OFFSCREEN_CHECK_BIAS = 4999,
	RACE_FLAG_OFFSCREEN_CHECK_WIDTH = 9999,
	RACE_FLAG_DRAW_ORDER_AFTER_FLAG = 1,
	RACE_FLAG_DRAW_ORDER_BEFORE_FLAG = -1,
	RACE_FLAG_DRAW_ORDER_DONE = 0,
	RACE_FLAG_TRANSITION_ONSCREEN = 0,
	RACE_FLAG_TRANSITION_OFFSCREEN = 2,
	RACE_FLAG_TRANSITION_BEGIN_ONSCREEN = 1,
	RACE_FLAG_TRANSITION_BEGIN_OFFSCREEN = 2,
	RACE_FLAG_TRANSITION_ONSCREEN_SPEED = 300,
	RACE_FLAG_TRANSITION_OFFSCREEN_SPEED_MAX = 1000,
	RACE_FLAG_TRANSITION_OFFSCREEN_ACCEL_SCALE = 10,
	RACE_FLAG_TRANSITION_ONSCREEN_SNAP_DISTANCE = 8,
	RACE_FLAG_TRANSITION_ONSCREEN_POSITION_SHIFT = 3,
	RACE_FLAG_TRANSITION_OFFSCREEN_POSITION_SHIFT = 2,
	RACE_FLAG_TRANSITION_TIME_SHIFT = 5,
	RACE_FLAG_LOADING_IDLE_SLIDE_LIMIT = -1000,
	RACE_FLAG_LOADING_IDLE_SLIDE_STEP = 0x28,
	// 0x23c is 0x200 + 0x3c: the right picture edge plus one letter step,
	// i.e. the first position outside the picture. 0x100 is half the
	// picture width. The lettering computes with these numbers in every format, in
	// the 512 space as in 4:3; each letter only reaches the canvas when it is
	// drawn (RaceFlag_LoadingLetterShift further down).
	RACE_FLAG_LOADING_OFFSCREEN_X_4_3 = 0x23c,
	RACE_FLAG_LOADING_CENTER_X_4_3 = 0x100,
	RACE_FLAG_LOADING_Y = 0x6c,
	RACE_FLAG_LOADING_FONT_SIZE = 1,
	RACE_FLAG_LOADING_TEXT_FLAGS = 0,
	RACE_FLAG_LOADING_FIRST_CENTER_FRAME = 4,
	RACE_FLAG_LOADING_LAST_CENTER_FRAME = 0x4a,
	RACE_FLAG_LOADING_EXIT_BASE_FRAME = 0x4b,
	RACE_FLAG_LOADING_LAST_VISIBLE_FRAME = 0x4f,
	RACE_FLAG_LOADING_ANIM_RESET_FRAME = 0x50,
	RACE_FLAG_LOADING_LETTER_SPEED_X = 0x3c,
	RACE_FLAG_LOADING_NEXT_LETTER_START_X = 0xf0,
	RACE_FLAG_LOADING_LETTER_FRAME_STEP = 4,
	RACE_FLAG_LOADING_GLYPH_EXTENDED_MAX = 4,
	RACE_FLAG_LOADING_FRAME_TIME_SHIFT = 5,
	RACE_FLAG_LOADING_MIN_FRAME_ADVANCE = 1,
	RACE_FLAG_LOADING_REPEAT_STAGE_FIRST = 6,
	RACE_FLAG_LOADING_REPEAT_STAGE_COUNT = 2,
	RACE_FLAG_TRIG_TABLE_MASK = 0x3ff,
	RACE_FLAG_TRIG_HIGH_HALF_MASK = 0x400,
	RACE_FLAG_TRIG_NEGATE_MASK = 0x800,
};

// THE LETTERING COMPUTES IN THE 512 SPACE, NOT IN THE CANVAS.
//
// In an earlier version these two positions followed the canvas: fly-in from W+60,
// rest at W/2. The idle offset RaceFlag_Transition, however, pushes the whole
// lettering left by a fixed 40 columns per frame (down to -1000), and a
// letter that was still flying in then stood W-512 further right than in 4:3 -
// outside on the left in 4:3, in the middle of the flag in the wide picture (measured on
// the transition into the garage: an "N" at column 92 in 16:9 and 328 in 43:18).
//
// Now the lettering computes as in 4:3, and each letter reaches the canvas
// individually (RaceFlag_LoadingLetterShift): centred at rest, and what is outside
// in 4:3 stays outside. At 4:3 both positions are the same numbers as
// before (512/2 and 512+60) and the shift is 0.

// The rest position of the lettering: the picture centre.
internal int RaceFlag_LoadingCentreX(void)
{
	return RACE_FLAG_LOADING_CENTER_X_4_3;
}

// The position at which a letter counts as "outside" - one letter step
// past the right picture edge. It is also the value compared against
// below to skip drawing a letter altogether; both
// the same number, as in the original.
internal int RaceFlag_LoadingOffscreenX(void)
{
	return RACE_FLAG_LOADING_OFFSCREEN_X_4_3;
}

// By how many canvas columns a letter at its 512-space position x0 is drawn
// shifted: anchored at the centre, gliding over the 4:3 edge
// (CTR_UI_CentreTransitShift, native_view.c).
internal int RaceFlag_LoadingLetterShift(int x0, u8 *glyph, int glyphByteCount)
{
	const int width = DecalFont_GetLineWidthStrlen((char *)glyph, glyphByteCount, RACE_FLAG_LOADING_FONT_SIZE);

	return CTR_UI_CentreTransitShift(x0, x0 + width);
}


struct RaceFlagProjectedRow
{
	u32 xy[RACE_FLAG_SCREEN_POINTS_PER_ROW];
};

union RaceFlagScreenBuffer
{
	struct RaceFlagProjectedRow row[RACE_FLAG_SCREEN_ROWS];
	u32 xy[RACE_FLAG_SCREEN_ROWS * RACE_FLAG_SCREEN_POINTS_PER_ROW];
};

struct RaceFlagScratch
{
	union RaceFlagScreenBuffer screen[2];
};

CTR_STATIC_ASSERT(sizeof(union RaceFlagScreenBuffer) == 0x78);
CTR_STATIC_ASSERT(sizeof(struct RaceFlagScratch) == 0xf0);

int RaceFlag_MoveModels(int frameIndex, int numFrames)
{
	// need a better prefix than TitleFlag,
	// all this does is move the intro logo models
	// from the center of the screen, to the right

	// also used for transitioning driver models
	// on and off the screen in character selection

	int angle;
	int midpoint;
	int result;

	if (frameIndex < 0)
	{
		return 0;
	}

	if (frameIndex > numFrames)
	{
		return 0x1000;
	}

	// cut in half
	midpoint = numFrames / 2;

	// if less than half done
	if (frameIndex < midpoint)
	{
		angle = (midpoint - frameIndex) * 0x400;

		// 50% - sin(angle) / 2
		result = 0x800 - MATH_Sin(angle / midpoint) / 2;
	}
	// if more than half done
	else
	{
		angle = (frameIndex - midpoint) * 0x400;

		// sin(angle) / 2 + 50%
		result = MATH_Sin(angle / midpoint) / 2 + 0x800;
	}
	return result;
}


b32 RaceFlag_IsFullyOnScreen(void)
{
	// return true if flag is fully on screen
	// return false if flag is not fully on screen
	return (sdata->RaceFlag_Position == RACE_FLAG_POSITION_ONSCREEN);
}


b32 RaceFlag_IsFullyOffScreen(void)
{
	// return false, "not true", if flag is < 5000, partially on-screen
	// return true, "not false", if flag is >= 5000, fully off-screen
	return ((((u16)sdata->RaceFlag_Position + RACE_FLAG_OFFSCREEN_CHECK_BIAS) & 0xffff) < RACE_FLAG_OFFSCREEN_CHECK_WIDTH) ^ 1;
}


b32 RaceFlag_IsTransitioning(void)
{
	int pos = sdata->RaceFlag_Position;

	return
	    // if checkered flag is not fully on-screen and not fully off-screen
	    (pos != RACE_FLAG_POSITION_ONSCREEN) && (pos != RACE_FLAG_POSITION_OFFSCREEN_LEFT) && (pos != RACE_FLAG_POSITION_OFFSCREEN) &&

	    // is allowed to render
	    ((sdata->gGT->renderFlags & RENDER_FLAG_CHECKERED_FLAG) != 0);
}


void RaceFlag_SetDrawOrder(b32 drawAfterFlag)
{
	sdata->RaceFlag_DrawOrder = (drawAfterFlag != 0) ? RACE_FLAG_DRAW_ORDER_AFTER_FLAG : RACE_FLAG_DRAW_ORDER_BEFORE_FLAG;
}


void RaceFlag_BeginTransition(int direction)
{
	// Begin Transition on-screen
	if (direction == RACE_FLAG_TRANSITION_BEGIN_ONSCREEN)
	{
		sdata->RaceFlag_LoadingTextAnimFrame = -1;

		sdata->RaceFlag_Position = RACE_FLAG_POSITION_OFFSCREEN;

		sdata->RaceFlag_AnimationType = RACE_FLAG_TRANSITION_ONSCREEN;
	}

	// Begin Transition off-screen
	else if (direction == RACE_FLAG_TRANSITION_BEGIN_OFFSCREEN)
	{
		RaceFlag_SetDrawOrder(0);

		sdata->RaceFlag_Position = RACE_FLAG_POSITION_ONSCREEN;

		sdata->RaceFlag_AnimationType = direction;
	}

	// enable loading screen's checkered flag
	sdata->gGT->renderFlags |= RENDER_FLAG_CHECKERED_FLAG;
}


void RaceFlag_SetFullyOnScreen()
{
	sdata->RaceFlag_AnimationType = RACE_FLAG_TRANSITION_ONSCREEN;
	sdata->RaceFlag_LoadingTextAnimFrame = -1;

	// flag is now fully on-screen
	sdata->RaceFlag_Position = RACE_FLAG_POSITION_ONSCREEN;

	// enable loading screen's checkered flag
	sdata->gGT->renderFlags |= RENDER_FLAG_CHECKERED_FLAG;
}


void RaceFlag_SetFullyOffScreen()
{
	sdata->RaceFlag_AnimationType = RACE_FLAG_TRANSITION_ONSCREEN;
	sdata->RaceFlag_LoadingTextAnimFrame = -1;

	// flag is now fully off-screen
	sdata->RaceFlag_Position = RACE_FLAG_POSITION_OFFSCREEN;

	// disable loading screen's checkered flag
	sdata->gGT->renderFlags &= ~RENDER_FLAG_CHECKERED_FLAG;
}


void RaceFlag_SetCanDraw(s16 canDraw)
{
	sdata->RaceFlag_CanDraw = canDraw;
}


s16 RaceFlag_GetCanDraw(void)
{
	return sdata->RaceFlag_CanDraw;
}


u32 *RaceFlag_GetOT(void)
{
	s16 positionStep;
	int position;
	struct GameTracker *gGT = sdata->gGT;

	u32 *otDrawFirst_FarthestDepth;
	u32 *otDrawLast_ClosestDepth;

	otDrawFirst_FarthestDepth = (u32 *)&gGT->pushBuffer[0].ptrOT[0x3FF];
	otDrawLast_ClosestDepth = gGT->otSwapchainDB[gGT->swapchainIndex];

	if (sdata->RaceFlag_DrawInitialized == 0)
	{
		sdata->RaceFlag_DrawInitialized = 1;
	}

	// transitioning on-screen
	if (sdata->RaceFlag_AnimationType == RACE_FLAG_TRANSITION_ONSCREEN)
	{
		// set fully "off" to start transition "on"
		if (sdata->RaceFlag_Position < 0)
		{
			sdata->RaceFlag_Position = RACE_FLAG_POSITION_OFFSCREEN;
		}

		sdata->RaceFlag_TransitionSpeed = RACE_FLAG_TRANSITION_ONSCREEN_SPEED;

		position = sdata->RaceFlag_Position;

		// if transitioning
		if (position != RACE_FLAG_POSITION_ONSCREEN)
		{
			// skip last 8 frames to zero
			if (position < RACE_FLAG_TRANSITION_ONSCREEN_SNAP_DISTANCE)
			{
				sdata->RaceFlag_Position = RACE_FLAG_POSITION_ONSCREEN;

				// transition for frame >= 8
			}
			else
			{
				// rate of transition

				position = ((u16)sdata->RaceFlag_Position >> RACE_FLAG_TRANSITION_ONSCREEN_POSITION_SHIFT) * gGT->elapsedTimeMS;
				position = position >> RACE_FLAG_TRANSITION_TIME_SHIFT;

				positionStep = -(s16)position;
				if (position < 1)
				{
					positionStep = -1;
				}

				sdata->RaceFlag_Position += positionStep;
			}
		}

		// transition is finished
		else
		{
			if (sdata->RaceFlag_DrawOrder != RACE_FLAG_DRAW_ORDER_AFTER_FLAG)
			{
				if (sdata->RaceFlag_DrawOrder != RACE_FLAG_DRAW_ORDER_BEFORE_FLAG)
				{
					return otDrawFirst_FarthestDepth;
				}

				sdata->RaceFlag_DrawOrder = RACE_FLAG_DRAW_ORDER_DONE;
			}
		}
	}

	// transition off-screen
	if (sdata->RaceFlag_AnimationType == RACE_FLAG_TRANSITION_OFFSCREEN)
	{
		if (sdata->RaceFlag_TransitionSpeed < RACE_FLAG_TRANSITION_OFFSCREEN_SPEED_MAX)
		{
			sdata->RaceFlag_TransitionSpeed += (s16)((gGT->elapsedTimeMS * RACE_FLAG_TRANSITION_OFFSCREEN_ACCEL_SCALE) >> RACE_FLAG_TRANSITION_TIME_SHIFT);
		}

		// If transitioning "off"
		if (sdata->RaceFlag_Position > RACE_FLAG_POSITION_OFFSCREEN_LEFT)
		{
			sdata->RaceFlag_Position -= (((u32)sdata->RaceFlag_TransitionSpeed >> RACE_FLAG_TRANSITION_OFFSCREEN_POSITION_SHIFT) * gGT->elapsedTimeMS) >>
			                            RACE_FLAG_TRANSITION_TIME_SHIFT;
		}

		// finished transitioning off
		else
		{
			sdata->RaceFlag_Position = RACE_FLAG_POSITION_OFFSCREEN;
			sdata->RaceFlag_AnimationType = RACE_FLAG_TRANSITION_ONSCREEN;
			gGT->renderFlags &= ~RENDER_FLAG_CHECKERED_FLAG;
		}
	}

	return otDrawLast_ClosestDepth;
}


void RaceFlag_ResetTextAnim(void)
{
	sdata->RaceFlag_LoadingTextAnimFrame = -1;
}


void RaceFlag_DrawLoadingString(void)
{
	struct GameTracker *gGT = sdata->gGT;
	int loadingTextBytes;
	int letterAnimFrame;
	int letterX;
	int glyphByteCount;
	int textByteIndex;
	char *loadingText;
	char *nextGlyph;
	int nextLetterStartX;
	int drawX;
	u32 *oldOT;
	u8 glyph[2];

	loadingText = sdata->lngStrings[LNG_LOADING];

	// pointer to OT mem
	oldOT = (u32 *)gGT->pushBuffer_UI.ptrOT;

	// pointer to OT mem
	gGT->pushBuffer_UI.ptrOT = gGT->otSwapchainDB[gGT->swapchainIndex];

	// get length of "LOADING..." string
	loadingTextBytes = strlen(loadingText);

	int textWidth = DecalFont_GetLineWidth(loadingText, RACE_FLAG_LOADING_FONT_SIZE);

	// loop counter
	textByteIndex = 0;

	// if game is not loading
	if (sdata->Loading.stage == LOAD_IDLE)
	{
		if (RACE_FLAG_LOADING_IDLE_SLIDE_LIMIT < (int)sdata->RaceFlag_Transition)
		{
			sdata->RaceFlag_Transition -= RACE_FLAG_LOADING_IDLE_SLIDE_STEP;
		}
	}
	else
	{
		sdata->RaceFlag_Transition = 0;
	}

	drawX = (sdata->RaceFlag_Transition & 0xffff) - (textWidth >> 1);

	letterAnimFrame = sdata->RaceFlag_LoadingTextAnimFrame;

	if (0 < loadingTextBytes)
	{
		nextLetterStartX = letterAnimFrame * -RACE_FLAG_LOADING_LETTER_SPEED_X + RaceFlag_LoadingOffscreenX();

		// for each byte in the localized "LOADING..." string
		do
		{
			if (letterAnimFrame < 0)
			{
			DrawLetterOffscreen:

				// draw text off screen
				letterX = RaceFlag_LoadingOffscreenX();
			}
			else
			{
				letterX = nextLetterStartX;
				if (RACE_FLAG_LOADING_FIRST_CENTER_FRAME < letterAnimFrame)
				{
					letterX = RaceFlag_LoadingCentreX();

					if (RACE_FLAG_LOADING_LAST_CENTER_FRAME < letterAnimFrame)
					{
						// if letter is fully off-screen
						if (RACE_FLAG_LOADING_LAST_VISIBLE_FRAME < letterAnimFrame)
						{
							goto DrawLetterOffscreen;
						}

						// letter is moving off-screen
						letterX =
						    (RACE_FLAG_LOADING_EXIT_BASE_FRAME - letterAnimFrame) * RACE_FLAG_LOADING_LETTER_SPEED_X + RaceFlag_LoadingCentreX();
					}
				}
			}
			glyph[0] = *loadingText;
			nextGlyph = loadingText + 1;
			glyphByteCount = 1;
			if (glyph[0] < RACE_FLAG_LOADING_GLYPH_EXTENDED_MAX)
			{
				glyph[1] = *nextGlyph;
				nextGlyph = loadingText + 2;

				// increment loop counter
				textByteIndex = textByteIndex + 1;

				glyphByteCount = 2;
			}
			if ((s16)letterX != (s16)RaceFlag_LoadingOffscreenX())
			{
				letterX += RaceFlag_LoadingLetterShift((s16)(drawX + letterX), glyph, glyphByteCount);
				DecalFont_DrawLineStrlen((char *)glyph, glyphByteCount, (s16)(drawX + letterX), RACE_FLAG_LOADING_Y, RACE_FLAG_LOADING_FONT_SIZE,
				                         RACE_FLAG_LOADING_TEXT_FLAGS);
			}

			letterX = DecalFont_GetLineWidthStrlen((char *)glyph, glyphByteCount, RACE_FLAG_LOADING_FONT_SIZE);

			drawX = drawX + letterX;
			nextLetterStartX = nextLetterStartX + RACE_FLAG_LOADING_NEXT_LETTER_START_X;

			// increment loop counter
			textByteIndex = textByteIndex + 1;

			// treat all letters with 4 frame difference
			letterAnimFrame = letterAnimFrame - RACE_FLAG_LOADING_LETTER_FRAME_STEP;

			loadingText = nextGlyph;
		} while (textByteIndex < loadingTextBytes);
	}

	// pointer to OT mem
	gGT->pushBuffer_UI.ptrOT = (u32 *)oldOT;

	if (letterAnimFrame < RACE_FLAG_LOADING_ANIM_RESET_FRAME)
	{
		int frameAdvance = gGT->elapsedTimeMS >> RACE_FLAG_LOADING_FRAME_TIME_SHIFT;

		if (frameAdvance < RACE_FLAG_LOADING_MIN_FRAME_ADVANCE)
		{
			frameAdvance = RACE_FLAG_LOADING_MIN_FRAME_ADVANCE;
		}

		sdata->RaceFlag_LoadingTextAnimFrame += frameAdvance;
	}

	else
	{
		sdata->RaceFlag_LoadingTextAnimFrame = -1;
		if ((u32)(sdata->Loading.stage - RACE_FLAG_LOADING_REPEAT_STAGE_FIRST) < RACE_FLAG_LOADING_REPEAT_STAGE_COUNT)
		{
			sdata->RaceFlag_LoadingTextAnimFrame = 0;
		}
	}
	return;
}

static char RaceFlag_CalculateBrightness(u32 sine, u8 darkTile)
{
	if (darkTile)
	{
		return ((sine * -55 + 0x140000) >> 0xD);
	}
	return ((sine * -125 + 0x1fe000) >> 0xD);
}

static int RaceFlag_Sin(u32 angle)
{
	int sine;

	// approximate trigonometry
	sine = (s32)CTR_ReadU32LE(&data.trigApprox[angle & RACE_FLAG_TRIG_TABLE_MASK]);

	if ((angle & RACE_FLAG_TRIG_HIGH_HALF_MASK) == 0)
	{
		sine = sine << 0x10;
	}

	sine = sine >> 0x10;

	if ((angle & RACE_FLAG_TRIG_NEGATE_MASK) != 0)
	{
		// make negative
		sine = -sine;
	}
	return sine;
}

void RaceFlag_DrawSelf()
{
	int i, j;
	int column, row;
	int toggle;

	s16 flagPos;
	u32 *ot;
	u32 screenlimit;
	u32 dimensions;

	int var2;
	int var3;
	u32 var1;

	POLY_G4 *p;
	struct GameTracker *gGT = sdata->gGT;

	int time;
	int lightL;
	int lightR;

	struct RaceFlagScratch *scratch;
	s32 local[5];
	SVECTOR pos[3] = {0};

	if (sdata->RaceFlag_CanDraw == 0)
	{
		return;
	}

	if (sdata->RaceFlag_LoadingTextAnimFrame < 0)
	{
		if ((5 < sdata->Loading.stage) && (sdata->Loading.stage < 8))
		{
			sdata->RaceFlag_LoadingTextAnimFrame = 0;
		}

		if (sdata->RaceFlag_LoadingTextAnimFrame < 0)
		{
			goto SKIP_LOADING_TEXT;
		}
	}

	RaceFlag_DrawLoadingString();

SKIP_LOADING_TEXT:

	sdata->RaceFlag_CopyLoadStage = sdata->Loading.stage;
	ot = (u32 *)RaceFlag_GetOT();

	// THE FLAG FILLS THE PICTURE, AND THE PICTURE IS THE CANVAS.
	//
	// Two fixed numbers stood here: 0x100 as the picture centre and, right below,
	// 0x200 as the right picture limit of the cull check. Both were half
	// and the full width of a 512-column-wide canvas. Since the canvas became wide
	// it is 918 columns wide at 43:18, and the flag promptly
	// stopped at column 512 - the flag on the left, black on the right. Exactly that
	// is the unclean loading transition.
	//
	// THE FLAG IS ENLARGED, NOT STRETCHED.
	//
	// In an earlier version the X row of the matrix was stretched by W/512 here:
	// the flag filled the wide canvas, but at 43:18 every check became
	// 1.79 times as wide as it is high. The flag is the background of all menus
	// and of the loading screen, and a full-screen background may fill the width,
	// but must not be distorted.
	//
	// MORE FLAG IS NOT POSSIBLE. It was tried to extend it sideways by columns,
	// the way the 3D scenes show more world. But the flag hangs
	// at an angle in space (data.matrixTitleFlag): to the right it runs towards a
	// vanishing line, and its ten rows converge there as well - at the bottom right
	// a black wedge remained that no number of columns fills, and the far
	// columns turned into stripes.
	//
	// So the flag is enlarged evenly: the H register (the
	// projection distance) grows with half the canvas, 0x100 at 4:3, and
	// scales BOTH axes around the picture centre. What the flag covered at 4:3,
	// it covers that way on every canvas; the checker squares stay square and get
	// bigger, at the top and bottom some flag disappears over the edge. The
	// transition becomes correct by itself: what is outside at 4:3 also lies
	// outside the wide canvas after the enlargement around the centre.
	//
	// At 4:3 the distance is 512/2 = 0x100 and this is the original's flag.
	gte_SetRotMatrix(&data.matrixTitleFlag);
	gte_SetTransMatrix(&data.matrixTitleFlag);

	gte_SetGeomOffset(CTR_Canvas_ActiveWidth() / 2, 0x78);
	gte_SetGeomScreen(CTR_Canvas_ActiveWidth() / 2);

	p = (POLY_G4 *)gGT->backBuffer->primMem.cursor;

	scratch = CTR_SCRATCHPAD_PTR(struct RaceFlagScratch, 0);

	// Packed limit pair for the cull check further down: upper
	// half the height, lower half the width. The height stays 0xd8, the width
	// follows the canvas.
	dimensions = (0xd8u << 16) | (u32)CTR_Canvas_ActiveWidth();
	screenlimit = 0x80008000;

	toggle = 0;

	// === First Loop Iteration ===
	// Remove 36*10 branching instructions,
	// Reduces clock from ~150 to ~130
	{
		union RaceFlagScreenBuffer *writeScreen = &scratch->screen[toggle];
		toggle = toggle ^ 1;

		local[0] = data.checkerFlagVariables[0];
		local[1] = data.checkerFlagVariables[1];
		local[2] = data.checkerFlagVariables[2];
		local[3] = data.checkerFlagVariables[3];
		local[4] = data.checkerFlagVariables[4];

		// === Step 1 ===
		int stepRate = gGT->elapsedTimeMS;
		local[4] += local[3] * stepRate;
		var1 = (int)local[4] >> 5;

		// === Step 2 ===
		if (0xfff < var1)
		{
			// reset counter
			local[4] &= 0x1ffff;
			var1 = (int)local[4] >> 5;

			local[0] += 0x200;
			local[2] += 200;

			int sin0 = RaceFlag_Sin(local[0]) + 0xfff;
			int sin2 = RaceFlag_Sin(local[2]) + 0xfff;

			// reset based on trig
			local[1] = (sin0 * 0x20 >> 0xd) + 0x96;
			local[3] = (sin2 * 0x40 >> 0xd) + 0xb4;
		}

		// === Step 3 ===
		var2 = RaceFlag_Sin(var1) + 0xfff;
		var2 = var2 * local[1];
		var2 = (var2 >> 0xd) + 0x280;

		// === Step 4 ===
		var1 += 0xc80;
		lightL = RaceFlag_Sin(var1) + 0xfff;

		// === Step 5 ===
		pos[0].vy = 0xfc72;
		pos[1].vy = 0xfcd0;
		pos[2].vy = 0xfd2e;

		// === Step 6 ===
		data.checkerFlagVariables[0] = local[0];
		data.checkerFlagVariables[1] = local[1];
		data.checkerFlagVariables[2] = local[2];
		data.checkerFlagVariables[3] = local[3];
		data.checkerFlagVariables[4] = local[4];

		time = sdata->RaceFlag_ElapsedTime >> 5;
		var1 = time;

		flagPos = sdata->RaceFlag_Position;
		flagPos = -0xbbe - flagPos;
		pos[0].vx = flagPos;
		pos[1].vx = flagPos;
		pos[2].vx = flagPos;

		i = 0;
		// === Step 7 ===
		for (row = 0; row < 10; row++)
		{
			SVECTOR *vect;
			for (vect = &pos[0]; vect < &pos[3]; vect++)
			{
				// Range: [1.0, 2.0]
				var3 = RaceFlag_Sin(var1) + 0xfff;
				var1 += 300;

				// change all vector posZ
				vect->vz = (s16)var2 + (s16)(var3 * 0x20 >> 0xd);
			}

			CTR_GteLoadSV3WithPad(&pos[0], &pos[1], &pos[2]);
			gte_rtpt();

			pos[0].vy += 0x11a;
			pos[1].vy += 0x11a;
			pos[2].vy += 0x11a;

			CTR_GteStoreSXY3(&writeScreen->row[row].xy[0], &writeScreen->row[row].xy[1], &writeScreen->row[row].xy[2]);
		}

		lightR = lightL;
	}


	// === Rest of Iterations ===
	// Now executing without branching
	for (column = 1; column < 35; column++)
	{
		union RaceFlagScreenBuffer *writeScreen = &scratch->screen[toggle];
		toggle = toggle ^ 1;
		union RaceFlagScreenBuffer *readScreen = &scratch->screen[toggle];

		// === Step 1 ===
		int stepRate = 0x40;
		local[4] += local[3] * stepRate;
		var1 = (int)local[4] >> 5;

		// === Step 2 ===
		if (0xfff < var1)
		{
			// reset counter
			local[4] &= 0x1ffff;
			var1 = (int)local[4] >> 5;

			local[0] += 0x200;
			local[2] += 200;

			int sin0 = RaceFlag_Sin(local[0]) + 0xfff;
			int sin2 = RaceFlag_Sin(local[2]) + 0xfff;

			// reset based on trig
			local[1] = (sin0 * 0x20 >> 0xd) + 0x96;
			local[3] = (sin2 * 0x40 >> 0xd) + 0xb4;
		}

		// === Step 3 ===
		var2 = RaceFlag_Sin(var1) + 0xfff;
		var2 = var2 * local[1];
		var2 = (var2 >> 0xd) + 0x280;

		// === Step 4 ===
		var1 += 0xc80;
		lightL = RaceFlag_Sin(var1) + 0xfff;

		// === Step 5 ===
		pos[0].vy = 0xfc72;
		pos[1].vy = 0xfcd0;
		pos[2].vy = 0xfd2e;

		// === Step 6 ===
		time += 0x100;
		var1 = time;

		pos[0].vx += 100;
		pos[1].vx += 100;
		pos[2].vx += 100;

		i = 0;
		// === Step 7 ===
		for (row = 0; row < 10; row++)
		{
			SVECTOR *vect;
			for (vect = &pos[0]; vect < &pos[3]; vect++)
			{
				// Range: [1.0, 2.0]
				var3 = RaceFlag_Sin(var1) + 0xfff;
				var1 += 300;

				// change all vector posZ
				vect->vz = (s16)var2 + (s16)(var3 * 0x20 >> 0xd);
			}

			CTR_GteLoadSV3WithPad(&pos[0], &pos[1], &pos[2]);
			gte_rtpt();

			pos[0].vy += 0x11a;
			pos[1].vy += 0x11a;
			pos[2].vy += 0x11a;

			CTR_GteStoreSXY3(&writeScreen->row[row].xy[0], &writeScreen->row[row].xy[1], &writeScreen->row[row].xy[2]);

			int firstSegment = (row == 0) ? 1 : 0;

			for (j = firstSegment; j < RACE_FLAG_SCREEN_POINTS_PER_ROW; j++, i++)
			{
				int pointIndex = row * RACE_FLAG_SCREEN_POINTS_PER_ROW + j - 1;
				u32 read0 = readScreen->xy[pointIndex];
				u32 read1 = readScreen->xy[pointIndex + 1];
				u32 write0 = writeScreen->xy[pointIndex];
				u32 write1 = writeScreen->xy[pointIndex + 1];

				if (((read0 & read1 & write0 & write1 & screenlimit) == 0) &&
				    (((dimensions - read0) & (dimensions - read1) & (dimensions - write0) & (dimensions - write1) & screenlimit) == 0))
				{
					// TRUE for gray, FALSE for white
					u8 boolDark = ((((column >> 2) + (i >> 2)) & 1U) != 0);

					u8 colorR = RaceFlag_CalculateBrightness(lightR, boolDark);
					setRGB0(p, colorR, colorR, colorR);
					CTR_WriteU32LE(&p->r2, CTR_ReadU32LE(&p->r0));

					u8 colorL = RaceFlag_CalculateBrightness(lightL, boolDark);
					setRGB1(p, colorL, colorL, colorL);
					CTR_WriteU32LE(&p->r3, CTR_ReadU32LE(&p->r1));

					// positions
					CtrGpu_WritePackedXY(&p->x0, read0);
					CtrGpu_WritePackedXY(&p->x2, read1);
					CtrGpu_WritePackedXY(&p->x1, write0);
					CtrGpu_WritePackedXY(&p->x3, write1);

					// prim/code
					setPolyG4(p);

					// Prim/OT
					// addPrim(ot, p); works but uses more instructions.
					p->tag = CtrGpu_PackOTTag(*ot, 0x8000000);
					*ot = CtrGpu_PrimToOTLink24(p);

					p++;
				}
			}
		}

		lightR = lightL;
	}

	gGT->backBuffer->primMem.cursor = p;
	sdata->RaceFlag_ElapsedTime += gGT->elapsedTimeMS * 100;
}
