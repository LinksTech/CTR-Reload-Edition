#include <common.h>


#ifdef CTR_NATIVE
// WHERE THE TEXT OF A ROW COMES FROM.
//
// Until now each of the five places had sdata->lngStrings[index] - five
// times the same fact, and that is exactly how one of them falls behind as soon as there is a
// second text area. Now it is here once.
//
// Defined in game/230/MM_NativeMenu.c, which comes later in the unity build.
static char *RECTMENU_String(s16 index)
{
	char *native = MM_NativeMenu_String(index);

	return (native != NULL) ? native : sdata->lngStrings[index];
}
#else
#define RECTMENU_String(index) (sdata->lngStrings[index])
#endif


#ifdef CTR_NATIVE
// THE DIMENSIONS OF THE BOX FOLLOW THE SCALE OF THE FONT.
//
// Row height, frame thickness, inner padding, the offset to the next box in
// the hierarchy: all of these are pixel numbers that were set next to characters of fixed
// size. If the font gets larger and the numbers stay,
// the font sticks out over its own box.
//
// So RM_S is not an extra but the conversion that belongs to g_decalFontScale
// - and at scale 1 FP_Mult(v, FP_ONE) returns exactly v. Every
// other box in the game is therefore drawn pixel for pixel as before, because
// the scale is not set at all outside the main menu chain.
extern int g_decalFontScale;

static s16 RM_S(int value)
{
	return (s16)FP_Mult(value, g_decalFontScale);
}
#else
#define RM_S(value) ((s16)(value))
#endif


// THE RETAIL STYLE - the same numbers that until now stood as literals in the renderer,
// and the same colour stores it used to reach for directly.
//
// The colours are there as POINTERS and not as values: menuRowHighlight_Normal
// and _Green are recomputed every frame (MainFrame_RenderFrame.c:430),
// a copy would freeze the pulsing. The address in
// sdata_static is taken, because sdata points exactly there for the whole runtime
// (psyq_start.c:45, native_memory.c:170).
const struct RectMenuStyle g_rectMenuStyleRetail = {
    .borderX = 3,
    .borderY = 2,
    .borderInsetW = 6,
    .borderInsetH = 4,

    // Zero: all four frame strips carry the same tone, as before. Whoever wants the
    // bevel edge sets it in the style of their chain - see the note at the
    // field in namespace_RectMenu.h.
    .frameBevelPercent = 0,

    .shadowWNarrow = 4,
    .shadowWWide = 0xc,
    .shadowHNarrow = 2,
    .shadowHWide = 6,

    .titleRuleY = 6,
    .titleRuleYBig = 9,
    .titleRuleH = 2,
    .titleAdvance = 6,
    .titleHeightBig = 9,
    .onlyTitleShrink = 6,

    .rowTopBig = 2,
    .rowExtraBig = 3,
    .highlightShrinkBig = 3,

    .childGapY = 0xc,
    .frameOffsetX = 6,
    .frameOffsetY = 4,
    .frameExtraW = 0xc,
    .frameExtraH = 8,
    .frameExtraHCollapsed = 8,

    .frameColor = {&sdata_static.battleSetup_Color_UI_1, &sdata_static.battleSetup_Color_UI_2},
    .fillSolid = &sdata_static.DrawSolidBoxData[0],
    .fillSpecial = &sdata_static.DrawSolidBoxData[1],
    .fillNormal = &sdata_static.DrawSolidBoxData[2],
    .shadowColor = &sdata_static.DrawSolidBoxData[0],
    .highlight = {&sdata_static.menuRowHighlight_Normal, &sdata_static.menuRowHighlight_Green},

    .textStyle = {0, 0x1d},
    .textStyleLocked = 0x17,

    .scalePercent = 100,
    .minWidthChars = 0,
};

// THE STYLE OF THE BOX CURRENTLY BEING DRAWN.
//
// Set and restored in RECTMENU_ProcessState, in the same bracket
// in which g_decalFontScale already was. Whoever calls RECTMENU_DrawInnerRect or
// RECTMENU_DrawQuip from outside - MM_NativeTracks_DrawInfo, MM_TrackSelect,
// MainFreeze - thus sees the style of the active box; today that is retail
// in each of these cases, i.e. pixel for pixel what was there before.
static const struct RectMenuStyle *s_style = &g_rectMenuStyleRetail;


void RECTMENU_DrawPolyGT4(struct Icon *icon, s16 posX, s16 posY, struct PrimMem *primMem, u32 *ot, u32 color0, u32 color1, u32 color2, u32 color3,
                          char transparency, s16 scale)
{
	if (!icon)
	{
		return;
	}

	DecalHUD_DrawPolyGT4(icon, posX, posY, primMem, ot, color0, color1, color2, color3, transparency, scale);
}


void RECTMENU_DrawOuterRect_Edge(RECT *r, Color color, u32 param_3, u32 *otMem)
{
	param_3 & 0x20 ? CTR_Box_DrawClearBox(r, &color, TRANS_50_DECAL, otMem) : CTR_Box_DrawSolidBox(r, color, otMem);
}


#if defined(CTR_NATIVE)
// NOTE(aalhendi): Native does not expose EXE rdata; this mirrors 0x80011620.
static const char s_rectMenuTimeFormat[] = "%ld:%ld%ld:%ld%ld";
#define RECTMENU_TIME_FORMAT s_rectMenuTimeFormat
#else
#define RECTMENU_TIME_FORMAT rdata.s_timeString
#endif

char *RECTMENU_DrawTime(int milliseconds)
{
	// 32 is added to milliseconds every frame,
	// 960 per second, the rest is basic math

	char *str = &sdata->ghostStrTrackTime[0];

	// build a string
	sprintf(

	    str,

	    // Format
	    // Minute:Seconds:Milliseconds
	    RECTMENU_TIME_FORMAT,

	    CTR_PRINTF_PSX_LONG(milliseconds / 0xe100),              // minutes
	    CTR_PRINTF_PSX_LONG((milliseconds / 0x2580) % 6),        // seconds / 10
	    CTR_PRINTF_PSX_LONG((milliseconds / 0x3c0) % 10),        // seconds
	    CTR_PRINTF_PSX_LONG(((milliseconds * 10) / 0x3c0) % 10), // milliseconds / 10
	    CTR_PRINTF_PSX_LONG(((milliseconds * 100) / 0x3c0) % 10) // milliseconds
	);

	return str;
}

#undef RECTMENU_TIME_FORMAT


void RECTMENU_DrawRwdBlueRect_Subset(s16 *pos, int *color, u32 *ot, struct PrimMem *primMem)
{
	POLY_G4 *p = (POLY_G4 *)primMem->cursor;

	if ((u32)p <= (u32)primMem->guardEnd)
	{
		primMem->cursor = p + 1;

		CtrGpu_WriteColorCode(&p->r0, (color[0] & 0xffffff) | 0x38000000);
		CtrGpu_WriteColorCode(&p->r1, color[1] & 0xffffff);
		CtrGpu_WriteColorCode(&p->r2, color[2] & 0xffffff);
		CtrGpu_WriteColorCode(&p->r3, color[3] & 0xffffff);

		CtrGpu_WritePackedXY(&p->x0, CTR_PackS16Pair(pos[0], pos[1]));
		CtrGpu_WritePackedXY(&p->x1, CTR_PackS16Pair(pos[0] + pos[2], pos[1]));
		CtrGpu_WritePackedXY(&p->x2, CTR_PackS16Pair(pos[0], pos[1] + pos[3]));
		CtrGpu_WritePackedXY(&p->x3, CTR_PackS16Pair(pos[0] + pos[2], pos[1] + pos[3]));

		p->tag = CtrGpu_PackOTTag(*ot, 0x8000000);
		*ot = CtrGpu_PrimToOTLink24(p);
	}
}


void RECTMENU_DrawRwdBlueRect(RECT *rect, char *metas, u32 *ot, struct PrimMem *primMem)
{
	s16 pos[4];
	int gradient[2];
	int colors[4];

	pos[0] = rect->x;
	pos[2] = rect->w;

	for (int i = 0; (u8)metas[i * 4 + 3] != 0x64; i++)
	{
		u8 *meta = (u8 *)&metas[i * 4];
		gradient[0] = *(int *)&meta[0];
		gradient[1] = *(int *)&meta[4];
		colors[0] = gradient[0];
		colors[1] = gradient[0];
		colors[2] = gradient[1];
		colors[3] = gradient[1];
		pos[1] = rect->y + (s16)(meta[3] * rect->h / 100);
		pos[3] = rect->y + (s16)(meta[7] * rect->h / 100) - pos[1] + 1;

		RECTMENU_DrawRwdBlueRect_Subset(pos, colors, ot, primMem);
	}
}


void RECTMENU_DrawRwdTriangle(s16 *position, char *color, u32 *otMem, struct PrimMem *primMem)
{
	POLY_G4 *p;
	void *primmemCurr;

	primmemCurr = primMem->cursor;
	p = 0;

	if (primmemCurr <= primMem->guardEnd)
	{
		p = primmemCurr;
		primMem->cursor = p + 1;
	}

	if (p != 0)
	{
		// RGB
		p->r0 = (u8)color[0x0];
		p->g0 = (u8)color[0x1];
		p->b0 = (u8)color[0x2];

		p->r1 = (u8)color[0x4];
		p->g1 = (u8)color[0x5];
		p->b1 = (u8)color[0x6];

		p->r2 = (u8)color[0x0];
		p->g2 = (u8)color[0x1];
		p->b2 = (u8)color[0x2];

		p->r3 = (u8)color[0x8];
		p->g3 = (u8)color[0x9];
		p->b3 = (u8)color[0xa];

		// rest of the primitive (four xy)
		p->x0 = position[0];
		p->y0 = position[1] - 1;

		p->x1 = position[2];
		p->y1 = position[3];

		p->x2 = position[0];
		p->y2 = position[1];

		p->x3 = position[4];
		p->y3 = position[5];

		setPolyG4(p);
		AddPrim(otMem, p);
	}
	return;
}


// ONE CHANNEL, SHIFTED BY THE BEVEL EDGE.
//
// Separate from the colour, because the channel is the whole computation: times
// (100 + p) or (100 - p), divided by 100, and then clamped to one byte. The
// clamp at the TOP is not caution - 0x50 times 160 divided by 100 is 128 and
// still fits, 0xC0 times 160 divided by 100 would be 307 and would wrap around without it.
static u8 RECTMENU_BevelChannel(u8 value, int percent)
{
	const int scaled = ((int)value * (100 + percent)) / 100;

	return (u8)((scaled < 0) ? 0 : ((scaled > 255) ? 255 : scaled));
}


// THE COLOUR OF A SINGLE FRAME STRIP.
//
// lit = 1 for top and left, 0 for bottom and right. With
// frameBevelPercent 0 it returns the colour unchanged, and that for
// both values of lit - that is why the caller needs no case distinction
// and why the retail path is demonstrably the same as before.
//
// code stays untouched: it holds the primitive header the caller
// sets, not colour.
static Color RECTMENU_BevelColor(Color color, int lit)
{
	const int percent = lit ? s_style->frameBevelPercent : -s_style->frameBevelPercent;

	color.r = RECTMENU_BevelChannel(color.r, percent);
	color.g = RECTMENU_BevelChannel(color.g, percent);
	color.b = RECTMENU_BevelChannel(color.b, percent);

	return color;
}


// THE FOUR STRIPS OF THE FRAME - and there were always four.
//
// The order was already here and is top, bottom, left, right. The only new thing
// is which tone goes to which: top and left carry the lighter one, bottom
// and right the darker one. That gives the frame a profile instead of a flat surface.
//
// No second draw path: these are the same four calls to the same
// edge function, with the same rectangles. Only the colour argument is a
// different one, and with frameBevelPercent 0 even that is the same.
void RECTMENU_DrawOuterRect_LowLevel(RECT *p, s16 xOffset, u16 yOffset, Color color, s16 param_5, u32 *otMem)
{
	int iVar1;
	RECT r;

	const Color lit = RECTMENU_BevelColor(color, 1);
	const Color shade = RECTMENU_BevelColor(color, 0);

	r.x = p->x;
	iVar1 = (int)param_5;
	r.y = p->y;
	r.w = p->w;
	r.h = yOffset;
	RECTMENU_DrawOuterRect_Edge(&r, lit, iVar1, otMem);

	r.y += (p->h - yOffset);
	RECTMENU_DrawOuterRect_Edge(&r, shade, iVar1, otMem);

	r.y = p->y + yOffset;
	r.h = p->h - (s16)((int)((u32)yOffset << 0x10) >> 0xf);
	r.w = xOffset;
	RECTMENU_DrawOuterRect_Edge(&r, lit, iVar1, otMem);

	r.x += (p->w - xOffset);
	RECTMENU_DrawOuterRect_Edge(&r, shade, iVar1, otMem);
	return;
}


void RECTMENU_DrawOuterRect_HighLevel(RECT *r, Color color, s16 param_3, u32 *otMem)
{
	RECTMENU_DrawOuterRect_LowLevel(r, RM_S(s_style->borderX), RM_S(s_style->borderY), color, param_3, otMem);
	return;
}


void RECTMENU_DrawQuip(char *comment, s16 startX, int startY, u32 sizeX, s16 fontType, int textFlag, s16 boxFlag)
{
	int posX = startX;
	int width;
	u32 sizeY;

	if ((sizeX & 0xffff) == 0)
	{
		width = DecalFont_GetLineWidth(comment, fontType);
		sizeX = width + 0xc;
	}

	// if text is not centered
	if ((textFlag & 0x8000) != 0)
	{
		// posX with text un-centered
		posX = startX - ((s16)sizeX / 2);
	}

	sizeY = (u32)data.PlayerCommentBoxParams[fontType];

	// Draw string
	DecalFont_DrawLine(comment, startX, (data.PlayerCommentBoxParams[4 + fontType] + startY), fontType, textFlag);

	RECT r;
	r.x = posX;
	r.y = startY;
	r.w = sizeX;
	r.h = sizeY;
	RECTMENU_DrawInnerRect(&r, boxFlag, sdata->gGT->backBuffer->otMem.uiOT);
}


// A BOX WITH A TITLE FOR A SCREEN THAT DRAWS ITS ROWS ITSELF.
//
// The track selection is NOT a cascading box. It runs under
// DISABLE_INPUT_ALLOW_FUNCPTRS, i.e. without RECTMENU_ProcessInput and without
// RECTMENU_DrawSelf (RECTMENU.c:1054), and sets its rectangles itself in
// MM_TrackSelect.c. It is supposed to look like the menu all the same.
//
// That is why it is here and not there: frame thickness, title rule and its spacing
// are in the style, and the style belongs to this file. The same three numbers a
// second time in MM_TrackSelect.c would be the same fact in two places.
//
// s_style is the retail style here: RECTMENU_ProcessState restores it after
// drawing the chain, and the track selection is an active box of its own
// (comment at RECTMENU.c:1077).
void RECTMENU_DrawTitledBox(RECT *r, char *title, int drawStyle)
{
	struct GameTracker *gGT = sdata->gGT;

	if (title != NULL)
	{
		const u32 *rgb = s_style->frameColor[((drawStyle & 0x10) != 0) ? 1 : 0];
		RECT rule;
		Color color;

		// Title centred above the box, rule below it - the same computation as
		// in RECTMENU_DrawFullRect for a chain with large row font.
		DecalFont_DrawLine(title, r->x + (r->w / 2), r->y + RM_S(s_style->titleRuleYBig), FONT_BIG, (JUSTIFY_CENTER | ORANGE));

		rule.x = (s16)(r->x + RM_S(s_style->borderX));
		rule.y = (s16)(r->y + RM_S(s_style->titleRuleYBig + data.font_charPixHeight[FONT_BIG]));
		rule.w = (s16)(r->w - RM_S(s_style->borderInsetW));
		rule.h = RM_S(s_style->titleRuleH);

		ColorCode_SetPacked(&color, *rgb);
		RECTMENU_DrawOuterRect_Edge(&rule, color, (u32)(drawStyle | 0x20), gGT->backBuffer->otMem.uiOT);
	}

	RECTMENU_DrawInnerRect(r, drawStyle, gGT->backBuffer->otMem.uiOT);
}

void RECTMENU_DrawInnerRect(RECT *r, int type, u32 *ot)
{
	const u32 *colorDataNormal;
	const u32 *colorDataSpecial;
	int drawMode;
	RECT adjustedRect;

	colorDataNormal = s_style->frameColor[((type & 0x10) != 0) ? 1 : 0];

	if ((type & 2) == 0)
	{
		Color color;
		ColorCode_SetPacked(&color, *colorDataNormal);
		RECTMENU_DrawOuterRect_HighLevel(r, color, (int)(s16)(type | 0x20), ot);
	}

	adjustedRect.x = r->x;
	adjustedRect.y = r->y;
	adjustedRect.w = r->w;
	adjustedRect.h = r->h;

	if ((type & 8) == 0)
	{
		if ((type & 2) == 0)
		{
			adjustedRect.x += RM_S(s_style->borderX);
			adjustedRect.y += RM_S(s_style->borderY);
			adjustedRect.w -= RM_S(s_style->borderInsetW);
			adjustedRect.h -= RM_S(s_style->borderInsetH);
		}

		if ((type & 1) == 0)
		{
			drawMode = ((type & 0x100) != 0) ? 2 : 0;
			colorDataSpecial = ((type & 0x100) != 0) ? s_style->fillSpecial : s_style->fillNormal;

			CTR_Box_DrawClearBox(&adjustedRect, (Color *)colorDataSpecial, drawMode, ot);
		}
		else
		{
			Color color;
			ColorCode_SetPacked(&color, *s_style->fillSolid);
			CTR_Box_DrawSolidBox(&adjustedRect, color, ot);
		}
	}

	if ((type & 4) == 0)
	{
		s16 horizontalOffset = ((type & 0x80) != 0) ? RM_S(s_style->shadowWNarrow) : RM_S(s_style->shadowWWide);
		s16 verticalOffset = ((type & 0x40) != 0) ? RM_S(s_style->shadowHNarrow) : RM_S(s_style->shadowHWide);

		adjustedRect.x = r->x + r->w;
		adjustedRect.y = r->y + verticalOffset;
		adjustedRect.w = horizontalOffset;
		adjustedRect.h = r->h;

		const u32 *color = s_style->shadowColor;
		CTR_Box_DrawClearBox(&adjustedRect, (Color *)color, 0, ot);

		adjustedRect.x = r->x + horizontalOffset;
		adjustedRect.y = r->y + r->h;
		adjustedRect.w = r->w - horizontalOffset;
		adjustedRect.h = verticalOffset;
		CTR_Box_DrawClearBox(&adjustedRect, (Color *)color, 0, ot);
	}

	return;
}


void RECTMENU_DrawFullRect(struct RectMenu *menu, RECT *inner)
{
	const u32 *rgb;
	RECT outer;
	struct GameTracker *gGT = sdata->gGT;

	// if title text exists
	if ((-1 < menu->stringIndexTitle) && ((menu->state & ONLY_DRAW_TITLE) == 0))
	{
		rgb = s_style->frameColor[((menu->drawStyle & 0x10) != 0) ? 1 : 0];

		outer.x = inner->x + RM_S(s_style->borderX);
		outer.y = inner->y + RM_S(s_style->titleRuleY);

		// pixel-height of non-title menu rows
		if ((menu->state & USE_SMALL_FONT) == 0)
		{
			outer.y = inner->y + RM_S(s_style->titleRuleYBig + data.font_charPixHeight[1]);
		}
		else if ((menu->state & BIG_TEXT_IN_TITLE) == 0)
		{
			outer.y += RM_S(data.font_charPixHeight[2]);
		}
		else
		{
			outer.y += RM_S(data.font_charPixHeight[1]);
		}

		outer.h = RM_S(s_style->titleRuleH);
		outer.w = inner->w - RM_S(s_style->borderInsetW);

		Color color;
		ColorCode_SetPacked(&color, *rgb);
		RECTMENU_DrawOuterRect_Edge(&outer, color, (menu->drawStyle | 0x20), gGT->backBuffer->otMem.uiOT);
	}
	RECTMENU_DrawInnerRect(inner, menu->drawStyle, gGT->backBuffer->otMem.uiOT);
}


void RECTMENU_GetHeight(struct RectMenu *m, s16 *height, b32 boolCheckSubmenu)
{
	int lineHeight;
	struct MenuRow *row;

	// heighth of small line
	lineHeight = RM_S(data.font_charPixHeight[FONT_SMALL]);

	// if small text disabled
	if ((m->state & USE_SMALL_FONT) == 0)
	{
		// height of big line
		lineHeight = RM_S(data.font_charPixHeight[FONT_BIG] + s_style->rowExtraBig);
	}

	// if not showing only highlighted row
	if ((m->state & SHOW_ONLY_HIGHLIT_ROW) == 0)
	{
		// if not only drawing title bar
		if ((m->state & ONLY_DRAW_TITLE) == 0)
		{
			// add rows
			for (row = m->rows; row->stringIndex != -1; row++)
			{
				*height += lineHeight;
			}
		}

		// only drawing title bar
		else
		{
			*height += lineHeight - RM_S(s_style->onlyTitleShrink);
		}
	}

	// only showing row highlighted
	else
	{
		*height += lineHeight;
	}

	// handle menu title
	if (m->stringIndexTitle >= 0)
	{
		// if not drawing title big
		if ((m->state & BIG_TEXT_IN_TITLE) == 0)
		{
			*height += lineHeight + RM_S(s_style->titleAdvance);
		}

		// if drawing title big,
		// this overrides title to big, even if rest of menu is small
		else
		{
			*height += RM_S(data.font_charPixHeight[FONT_BIG] + s_style->titleHeightBig);
		}
	}

	// if submenu needs to be drawn
	if ((m->state & DRAW_NEXT_MENU_IN_HIERARCHY) != 0)
	{
		if ((boolCheckSubmenu & 0xffff) != 0)
		{
			// recursively check height for more submenus
			RECTMENU_GetHeight(m->ptrNextBox_InHierarchy, height, 1);
		}
	}
}


void RECTMENU_GetWidth(struct RectMenu *m, s16 *width, b32 boolCheckSubmenu)
{
	int fontType;
	struct MenuRow *row;
	int lineWidth;

	fontType = FONT_BIG;

	// if menu should have tiny text
	if ((m->state & USE_SMALL_FONT) != 0)
	{
		fontType = FONT_SMALL;
	}

	// handle rows
	for (row = m->rows; row->stringIndex != -1; row++)
	{
		// width of string in each row
		lineWidth = DecalFont_GetLineWidth(RECTMENU_String(row->stringIndex & 0x7fff), fontType);

		// set new width if new max is found
		if (*width < (lineWidth + 1))
		{
			*width = lineWidth + 1;
		}
	}

	// handle menu title
	if (m->stringIndexTitle >= 0)
	{
		// if force title to be big
		if ((m->state & BIG_TEXT_IN_TITLE) != 0)
		{
			// override
			fontType = FONT_BIG;
		}

		// width of string in each row
		lineWidth = DecalFont_GetLineWidth(RECTMENU_String(m->stringIndexTitle & 0x7fff), fontType);

		// set new width if new max is found
		if (*width < (lineWidth + 1))
		{
			*width = lineWidth + 1;
		}
	}

	// if submenu needs to be drawn
	if ((m->state & DRAW_NEXT_MENU_IN_HIERARCHY) != 0)
	{
		if ((boolCheckSubmenu & 0xffff) != 0)
		{
			// recursively check height for more submenus
			RECTMENU_GetWidth(m->ptrNextBox_InHierarchy, width, 1);
		}
	}
}


void RECTMENU_DrawSelf(struct RectMenu *menu, int posX, s16 posY, s16 menuWidth)
{
	u16 textFlags;
	u32 state;
	int index;
	char *titleString;
	s16 offsetX;
	s16 sVar4;
	const Color *rgb;
	u16 uVar5;
	struct MenuRow *row;
	s16 sVar6;
	s16 sVar7;
	u16 uVar8;
	RECT background;
	RECT borders;
	s16 local_60;
	s16 menuHeight;
	s16 offsetY;
	s16 local_50;
	s16 local_48;
	s16 local_40;
	s16 local_38;
	int local_30;
	int local_2c;
	s16 posX_prev;
	s16 posY_prev;
	struct GameTracker *gGT = sdata->gGT;

	uVar8 = s_style->textStyle[((menu->drawStyle & 0x10U) != 0) ? 1 : 0];
	local_40 = 0;
	local_38 = 0;
	offsetY = posY;
	if ((menu->state & RECTMENU_DRAW_CALLBACK_FLAGS) == RECTMENU_DRAW_CALLBACK_FLAGS)
	{
		menu->funcState = RECTMENU_FUNC_STATE_DRAW;
		if (menu->funcPtr != NULL)
		{
			menu->funcPtr(menu);
		}
	}
	posX_prev = 2;
	if ((menu->state & USE_SMALL_FONT) == 0)
	{
		posX_prev = 1;
		local_50 = RM_S(s_style->rowTopBig);
		sVar7 = RM_S(data.font_charPixHeight[1] + s_style->rowExtraBig);
	}
	else
	{
		local_50 = 0;
		sVar7 = RM_S(data.font_charPixHeight[2]);
		if ((menu->state & BIG_TEXT_IN_TITLE) == 0)
		{
			local_48 = RM_S(data.font_charPixHeight[2]);
			goto LAB_80045e94;
		}
	}
	local_48 = RM_S(data.font_charPixHeight[1] + s_style->rowExtraBig);
LAB_80045e94:

	local_60 = 0;
	menu->posX_prev = menu->posX_curr;
	menu->posY_prev = menu->posY_curr;
	RECTMENU_GetHeight(menu, &local_60, 0);

	state = menu->state;

	menu->width = menuWidth;
	menu->state &= ~RECTMENU_CLOSE_TRANSIENT;
	menu->height = local_60;

	if ((state & CENTER_ON_Y) != 0)
	{
		menuHeight = 0;
		RECTMENU_GetHeight(menu, &menuHeight, 1);
		local_38 = (s16)(-menuHeight / 2);
	}
	if ((state & CENTER_ON_X) != 0)
	{
		local_40 = (s16)(-menuWidth / 2);
	}
	sVar6 = 0;
	row = &menu->rows[0];
	index = menu->stringIndexTitle;
	posY_prev = local_50 + local_38 + offsetY + menu->posY_prev;
	if ((-1 < index) && ((state & ONLY_DRAW_TITLE) == 0))
	{
		sVar4 = 1;
		if ((state & BIG_TEXT_IN_TITLE) == 0)
		{
			sVar4 = posX_prev;
		}
		if ((state & CENTER_MENU_TEXT) == 0)
		{
			offsetX = (s16)(posX + menu->posX_prev);
			uVar5 = uVar8;
			if ((state & CENTER_ON_X) != 0)
			{
				uVar5 = uVar8 | 0x8000;
			}
			titleString = RECTMENU_String(index);
		}
		else
		{
			uVar5 = uVar8 | 0x8000;
			titleString = RECTMENU_String(index);
			offsetX = (s16)(posX + menu->posX_prev + (menuWidth / 2));
		}
		DecalFont_DrawLine(titleString, offsetX, posY_prev, sVar4, uVar5);
		posY_prev = local_48 + posY_prev + RM_S(s_style->titleAdvance);
	}

	if (row->stringIndex != -1)
	{
		local_30 = (menuWidth / 2) + 1;
		local_2c = posX_prev;
		do
		{
			state = menu->state;
			if (((state & (ONLY_DRAW_TITLE | SHOW_ONLY_HIGHLIT_ROW)) == 0) || (sVar6 == menu->rowSelected))
			{
				uVar5 = row->stringIndex;
				textFlags = s_style->textStyleLocked;
				if ((uVar5 & 0x8000) == 0)
				{
					textFlags = uVar8;
				}
				if ((uVar5 & 0x7fff) != 0)
				{
					if ((state & CENTER_MENU_TEXT) == 0)
					{
						sVar4 = (s16)(posX + menu->posX_prev + 1);
						if ((state & CENTER_ON_X) != 0)
						{
							textFlags |= 0x8000;
						}
						titleString = RECTMENU_String(uVar5 & 0x7fff);
						index = local_2c;
					}
					else
					{
						textFlags |= 0x8000;
						titleString = RECTMENU_String(uVar5 & 0x7fff);
						sVar4 = (s16)(posX + menu->posX_prev + local_30);
						index = posX_prev;
					}
					DecalFont_DrawLine(titleString, sVar4, posY_prev, index, textFlags);
				}
				posY_prev += sVar7;
			}
			row++;
			sVar6++;
		} while (row->stringIndex != -1);
	}
	if ((menu->state & (HIDE_ROW_HIGHLIGHT | ONLY_DRAW_TITLE)) == 0)
	{
		background.x = local_40 + posX + menu->posX_prev;
		background.y = offsetY + menu->posY_prev + local_38;
		if ((menu->state & SHOW_ONLY_HIGHLIT_ROW) == 0)
		{
			background.y += menu->rowSelected * sVar7 + local_50 + -1;
		}
		else
		{
			background.y += local_50 + -1;
		}
		if ((menu->state & USE_SMALL_FONT) == 0)
		{
			background.h = -RM_S(s_style->highlightShrinkBig);
		}
		else
		{
			background.h = 1;
		}
		background.h = sVar7 + background.h;
		if (-1 < menu->stringIndexTitle)
		{
			background.y += local_48 + RM_S(s_style->titleAdvance);
		}
		rgb = s_style->highlight[((menu->drawStyle & 0x10U) != 0) ? 1 : 0];
		background.w = menuWidth;

		CTR_Box_DrawClearBox(&background, rgb, 1, gGT->backBuffer->otMem.uiOT);
	}
	if ((menu->state & DRAW_NEXT_MENU_IN_HIERARCHY) != 0)
	{
		RECTMENU_DrawSelf(menu->ptrNextBox_InHierarchy, posX + menu->posX_prev,
		                  local_38 + offsetY + menu->posY_prev + sVar7 + RM_S(s_style->childGapY), menuWidth);
	}
	posX_prev = menu->posX_prev;
	posY_prev = menu->posY_prev;
	if ((menu->state & ONLY_DRAW_TITLE) == 0)
	{
		borders.h = (local_60 + RM_S(s_style->frameExtraH)) - (*(u8 *)&menu->state >> 7);
	}
	else
	{
		// The collapsed box has its OWN surcharge. Retail carries
		// the same value there as frameExtraH, so stock stays at 8 and
		// draws point for point what was there before.
		borders.h = sVar7 + RM_S(s_style->frameExtraHCollapsed);
	}
	borders.w = menuWidth + RM_S(s_style->frameExtraW);
	borders.y = local_38 + offsetY + posY_prev - RM_S(s_style->frameOffsetY);
	borders.x = local_40 + posX + posX_prev - RM_S(s_style->frameOffsetX);
	RECTMENU_DrawFullRect(menu, &borders);

	// WHAT THIS BOX REALLY DREW.
	//
	// Here and nowhere else: borders is the finished rectangle, with the
	// width from RECTMENU_GetWidth and the height from RECTMENU_GetHeight. Both
	// are computed and appear in no declaration - and exactly those are what an
	// editor must see. For every box the format does not know, the
	// call does nothing.
	NativeMenuDecl_NoteDrawn(menu, &borders);
}


void RECTMENU_ClearInput()
{
	int i;

	sdata->AnyPlayerTap = 0;
	sdata->AnyPlayerHold = 0;

	for (i = 0; i < 4; i++)
	{
		sdata->buttonTapPerPlayer[i] = 0;
		sdata->buttonHeldPerPlayer[i] = 0;
	}
}


void RECTMENU_CollectInput()
{
	int i;
	int numListen;
	struct RectMenu *activeSub;

	sdata->AnyPlayerTap = 0;
	sdata->AnyPlayerHold = 0;
	activeSub = sdata->activeSubMenu;

	numListen = sdata->gGT->numPlyrNextGame;

	if ((activeSub != NULL) && ((activeSub->state & ALL_PLAYERS_USE_MENU) != 0))
	{
		numListen = 4;
	}

	struct GamepadBuffer *gb = &sdata->gGamepads->gamepad[0];

	for (i = 0; i < numListen; i++)
	{
		sdata->buttonTapPerPlayer[i] = gb->buttonsTapped;
		sdata->buttonHeldPerPlayer[i] = gb->buttonsHeldCurrFrame;
		gb++;

		sdata->AnyPlayerTap |= sdata->buttonTapPerPlayer[i];
		sdata->AnyPlayerHold |= sdata->buttonHeldPerPlayer[i];
	}
}


int RECTMENU_ProcessInput(struct RectMenu *m)
{
	struct MenuRow *currMenuRow;
	int i;
	int button;
	int oldRow;
	int newRow;

	int returnVal = 0;

	RngDeadCoed(&sdata->advRng);

	if (((m->state & ONLY_DRAW_TITLE) == 0) && ((m->state & RECTMENU_DRAW_CALLBACK_FLAGS) != RECTMENU_DRAW_CALLBACK_FLAGS))
	{
		if (sdata->activeSubMenu != m)
		{
			sdata->activeSubMenu = m;

			if ((m->state & KEEP_INPUTS_IN_SUBMENU) == 0)
			{
				RECTMENU_ClearInput();
			}
		}
	}

	// button from any player
	button = sdata->AnyPlayerTap;

	// if only P1 can use menu
	if ((m->state & ALL_PLAYERS_USE_MENU) == 0)
	{
		// get button from P1
		button = sdata->buttonTapPerPlayer[0];
	}

	if (

	    // if not drawing only title bar,
	    // therefore this is the bottom of hierarchy
	    ((m->state & ONLY_DRAW_TITLE) == 0) &&

	    // draw callbacks suppress normal input
	    ((m->state & RECTMENU_DRAW_CALLBACK_FLAGS) != RECTMENU_DRAW_CALLBACK_FLAGS) &&

	    // D-pad or menu confirm/back buttons
	    ((button & RECTMENU_INPUT_MENU) != 0) &&

	    // No cheat code entering
	    ((sdata->buttonHeldPerPlayer[0] & (BTN_L1 | BTN_R1)) == 0))
	{
		oldRow = m->rowSelected;
		newRow = oldRow;

		currMenuRow = &m->rows[oldRow];

		// optimized way to check all four button presses:
		// up, down, left, right, and get new row
		for (i = 0; i < 4; i++)
		{
			if (((button >> i) & 1) != 0)
			{
				newRow = *(char *)((char *)&currMenuRow->rowOnPressUp + i);
				break;
			}
		}

		// check if row has changed
		if (oldRow != newRow)
		{
			// if cursor moving sound is not muted
			if ((m->state & MUTE_SOUND_OF_MOVING_CURSOR) == 0)
			{
				OtherFX_Play(0, 1);
			}
		}

		if ((button & (BTN_CROSS_one | BTN_CIRCLE)) == 0)
		{
			if (
			    // if Triangle or Square
			    ((button & (BTN_TRIANGLE | BTN_SQUARE_one)) != 0) &&

			    // if this is not the top of the menu
			    ((m->state & MENU_CANT_GO_BACK) == 0))
			{
				// process GO BACK

				// if menu is not muted
				if ((m->state & MUTE_SOUND_OF_MOVING_CURSOR) == 0)
				{
					OtherFX_Play(2, 1);
				}

				returnVal = -1;

				m->funcState = RECTMENU_FUNC_STATE_INPUT;

				m->rowSelected = -1;

				if (m->funcPtr != 0)
				{
					RECTMENU_ClearInput();
					m->funcPtr(m);
				}

				// Save row
				m->rowSelected = newRow;
			}
		}

		// if Cross or Circle
		else
		{
			// unlocked row
			if ((m->rows[m->rowSelected].stringIndex & 0x8000) == 0)
			{
				if ((m->state & MUTE_SOUND_OF_MOVING_CURSOR) == 0)
				{
					OtherFX_Play(1, 1);
				}

				m->funcState = RECTMENU_FUNC_STATE_INPUT;

				// Save row BEFORE processing the Cross button,
				// this is why you can glitch into 3P VS with
				// only 2 controllers, by pressing DOWN+X same frame
				m->rowSelected = newRow;

				returnVal = 1;

				if (m->funcPtr != 0)
				{
					RECTMENU_ClearInput();
					m->funcPtr(m);
				}
			}
			else if ((m->state & MUTE_SOUND_OF_MOVING_CURSOR) == 0)
			{
				// "womp" sound for LOCKED row
				OtherFX_Play(5, 1);
			}
		}

		RECTMENU_ClearInput();

		m->rowSelected = newRow;
	}

	// if "next" hierarchy level exists
	if ((m->state & DRAW_NEXT_MENU_IN_HIERARCHY) != 0)
	{
		// store self in the next
		m->ptrNextBox_InHierarchy->ptrPrevBox_InHierarchy = m;

		// keep going till the bottom hierarchy level is hit,
		// where m->state&4==0, cause not drawing "only title"
		returnVal = RECTMENU_ProcessInput(m->ptrNextBox_InHierarchy);
	}

	return returnVal;
}


void RECTMENU_ProcessState()
{
	struct RectMenu *currMenu;
	s16 width;
	int state;

	// ON THE TICK, NEVER IN THE MIDDLE OF DRAWING.
	//
	// Up here, before anything about the boxes is read: a reload
	// swaps rows and pointers, and that must not happen between two rows
	// of the same frame. Without --menu-reload the call returns at once
	// and never looks at the disk.
	NativeMenuDecl_Tick();

	// check for curr box
	currMenu = sdata->ptrDesiredMenu;

	// unused
	if (sdata->framesRemainingInMenu != 0)
	{
		sdata->framesRemainingInMenu--;
	}

	// if you want to change the Menu
	if (currMenu != 0)
	{
		sdata->ptrDesiredMenu = 0;

		// show menu
		sdata->ptrActiveMenu = currMenu;
		currMenu->state &= ~NEEDS_TO_CLOSE;

		// get menu at end of hierarchy, if there is hierarchy
		while ((currMenu->state & DRAW_NEXT_MENU_IN_HIERARCHY) != 0)
		{
			currMenu = (struct RectMenu *)currMenu->ptrNextBox_InHierarchy;
		}

		// remove "draw only title bar" from lowest hierarchy,
		// so that rows in this menu draw properly
		currMenu->state &= ~ONLY_DRAW_TITLE;
	}

	currMenu = sdata->ptrActiveMenu;
	state = currMenu->state;

	// run funcPtr if it exists
	if ((state & (EXECUTE_FUNCPTR | DISABLE_INPUT_ALLOW_FUNCPTRS)) != 0)
	{
		currMenu->funcState = RECTMENU_FUNC_STATE_UPDATE;
		currMenu->funcPtr(currMenu);

		// check if funcPtr changed "state"
		currMenu = sdata->ptrActiveMenu;
		state = currMenu->state;
	}

	// if not character selection
	if ((state & DISABLE_INPUT_ALLOW_FUNCPTRS) == 0)
	{
		// process button input for menu
		RECTMENU_ProcessInput(currMenu);

		// check if ProcessInput changed "state"
		currMenu = sdata->ptrActiveMenu;
		state = currMenu->state;

		// if Menu border is not invisible
		if ((state & INVISIBLE) == 0)
		{
#ifdef CTR_NATIVE
			// The style applies to the CHAIN of the active box and only to it,
			// and it is restored right after. Everything else - driver selection,
			// track selection, battle setup - is an active box of its own and gets
			// retail with FP(1.0), i.e. pixel for pixel what was there before.
			const struct RectMenuStyle *const savedStyle = s_style;
			const int savedScale = g_decalFontScale;

			s_style = MM_NativeMenu_StyleFor(currMenu);
			g_decalFontScale = (FP(1.0) * s_style->scalePercent) / 100;
#endif

			// clear width, then get width
			width = 0;
			RECTMENU_GetWidth(currMenu, &width, 1);

#ifdef CTR_NATIVE
			// A minimum width instead of the pure content width. Without it
			// the box jumps to a different width on every step through the hierarchy
			// - QUIT is four characters, CUSTOM TRACK twelve -, and
			// a frame that changes its size on every key press is
			// no viewing area. It may still grow larger.
			if (s_style->minWidthChars > 0)
			{
				const s16 declared =
				    (s16)(s_style->minWidthChars * FP_Mult(data.font_charPixWidth[FONT_BIG], g_decalFontScale) + 1);

				if (width < declared)
				{
					width = declared;
				}
			}
#endif

			// draw
			RECTMENU_DrawSelf(currMenu, 0, 0, (int)width);

#ifdef CTR_NATIVE
			g_decalFontScale = savedScale;
			s_style = savedStyle;
#endif

#ifdef CTR_NATIVE
			// THE ERROR IS SHOWN IN THE PICTURE, NOT ONLY IN THE LOG.
			//
			// Whoever edits the file would otherwise see a menu that does not
			// change, and take that for a bug in the game. Only with
			// --menu-reload: without the switch there is no reload, hence
			// no load error either, and the picture stays untouched.
			if (g_cfg_menuReload)
			{
				const char *failure = NativeMenuDecl_LastError();

				if (failure != NULL)
				{
					DecalFont_DrawLine("MENU NOT LOADED", 8, 8, FONT_SMALL, ORANGE_RED);
					DecalFont_DrawLine((char *)failure, 8, 22, FONT_SMALL, ORANGE_RED);
				}
			}
#endif
		}
	}

	currMenu = sdata->ptrActiveMenu;
	state = currMenu->state;

	// not sure what this is
	if ((state & RECTMENU_UNKNOWN_0x800) == 0)
	{
		if (RaceFlag_GetCanDraw() == 0)
		{
			RaceFlag_SetCanDraw(1);
		}

		sdata->gGT->renderFlags |= RENDER_FLAG_RENDER_BUCKET;
	}

	currMenu = sdata->ptrActiveMenu;
	state = currMenu->state;

	// if menu needs to close
	if ((state & NEEDS_TO_CLOSE) != 0)
	{
		// deactivate
		sdata->ptrActiveMenu = 0;
	}
}


void RECTMENU_Show(struct RectMenu *m)
{
	RECTMENU_ClearInput();

	sdata->ptrActiveMenu = m;

	m->state &= ~NEEDS_TO_CLOSE;
}


void RECTMENU_Hide(struct RectMenu *m)
{
	m->state |= NEEDS_TO_CLOSE;
}


b32 RECTMENU_BoolHidden(struct RectMenu *m)
{
	return ((m->state & NEEDS_TO_CLOSE) != 0);
}
