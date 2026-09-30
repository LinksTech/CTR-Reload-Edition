#ifndef CTR_NATIVE_NAMESPACE_RECTMENU_H
#define CTR_NATIVE_NAMESPACE_RECTMENU_H

enum MenuFlags
{
	// menu's X position will be used to center it horizontally
	CENTER_ON_X = 1,

	// menu's Y position will be used to center it vertically
	CENTER_ON_Y = 2,

	// menu will be centered on these coordinates
	CENTER_ON_COORDS = 3,

	// only draws the menu's title
	ONLY_DRAW_TITLE = 4,

	// transient close/reset marker cleared by RECTMENU_DrawSelf
	RECTMENU_CLOSE_TRANSIENT = 8,

	// allows drawing another menu box while a menu box is being drawn
	// position is automatically derived from originating menu, so cascading windows are easier
	DRAW_NEXT_MENU_IN_HIERARCHY = 0x10,
	DISABLE_INPUT_ALLOW_FUNCPTRS = 0x20,

	// menu will display a single row that changes as you press a direction
	// kind of like scrolling
	SHOW_ONLY_HIGHLIT_ROW = 0x40,

	// instead of the big font, menu will use the small font
	USE_SMALL_FONT = 0x80,

	// do not draw the selected-row highlight box
	HIDE_ROW_HIGHLIGHT = 0x100,

	// center title and row text within the menu width
	CENTER_MENU_TEXT = 0x200,
	EXECUTE_FUNCPTR = 0x400,
	RECTMENU_UNKNOWN_0x800 = 0x800,

	// needs a better name, apparently it's for when it's closed/closing
	NEEDS_TO_CLOSE = 0x1000,
	INVISIBLE = 0x2000,

	// title will use big text
	// to be used in conjunction with USE_SMALL_FONT
	BIG_TEXT_IN_TITLE = 0x4000,

	ALL_PLAYERS_USE_MENU = 0x8000,
	KEEP_INPUTS_IN_SUBMENU = 0x10000,
	// Retail only checks this pair together; standalone meanings are not proven.
	RECTMENU_DRAW_CALLBACK_FLAGS = 0x60000,
	// 0x80000
	MENU_CANT_GO_BACK = 0x100000,
	// 0x200000
	// 0x400000
	MUTE_SOUND_OF_MOVING_CURSOR = 0x800000
};

enum RectMenuState
{
	RECTMENU_STATE_CENTERED = RECTMENU_UNKNOWN_0x800 | CENTER_ON_COORDS,
	RECTMENU_STATE_SMALL_CENTERED = RECTMENU_STATE_CENTERED | USE_SMALL_FONT,
	RECTMENU_STATE_EXEC_CENTERED = RECTMENU_STATE_CENTERED | EXECUTE_FUNCPTR,
	RECTMENU_STATE_SMALL_EXEC_CENTERED = RECTMENU_STATE_EXEC_CENTERED | USE_SMALL_FONT,
	RECTMENU_STATE_CALLBACK = RECTMENU_UNKNOWN_0x800 | DISABLE_INPUT_ALLOW_FUNCPTRS,
	RECTMENU_STATE_CALLBACK_CENTERED = RECTMENU_STATE_CALLBACK | CENTER_ON_COORDS,
	RECTMENU_STATE_SMALL_CALLBACK_CENTERED = RECTMENU_STATE_CALLBACK_CENTERED | USE_SMALL_FONT,
	RECTMENU_STATE_SMALL_CALLBACK_CENTER_X = RECTMENU_STATE_CALLBACK | USE_SMALL_FONT | CENTER_ON_X,
	RECTMENU_STATE_SMALL_EXEC_CENTER_X = RECTMENU_UNKNOWN_0x800 | EXECUTE_FUNCPTR | USE_SMALL_FONT | CENTER_ON_X,
	RECTMENU_STATE_INVISIBLE_CALLBACK = RECTMENU_STATE_CALLBACK | INVISIBLE,
};

enum MenuRowFlags
{
	RECTMENU_STRING_NONE = -1,
	MENU_ROW_LNG_MASK = 0x7fff,
	MENU_ROW_LOCKED = 0x8000,
};

enum RectMenuDrawStyle
{
	RECTMENU_DRAW_STYLE_3P4P_LAYOUT = 0x100,
};

enum RectMenuFuncState
{
	RECTMENU_FUNC_STATE_INPUT = 0,
	RECTMENU_FUNC_STATE_UPDATE = 1,
	RECTMENU_FUNC_STATE_DRAW = 2,
};

enum RectMenuInputMask
{
	RECTMENU_INPUT_MENU = BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT | BTN_CROSS_one | BTN_SQUARE_one | BTN_CIRCLE | BTN_TRIANGLE,
};


// THE STYLE OF A MENU BOX.
//
// Up to here the drawer's dimensions were literals in RECTMENU.c and
// its colors fixed reaches into sdata. Both are now named and live
// in one place. The style "retail" carries exactly the previous values - this
// restructuring changes no pixel, and exactly that is its acceptance condition.
//
// TWO THINGS THAT ARE NOT HERE, AND WHY.
//
// The row height. It comes from data.font_charPixHeight and was never
// settable; the style only carries the additions next to it. A
// row height in the style would be a second source for a number the font
// already knows - and the two would drift apart at the first font change.
//
// The colors as VALUES. menuRowHighlight_Normal and _Green are recomputed in EVERY
// frame, from a sine curve (MainFrame_RenderFrame.c:430-431). A
// style that copies them freezes the pulsing. That is why pointers
// to the storage are here and not its content.
struct RectMenuStyle
{
	// THE FRAME. borderX/borderY is its thickness and at the same time the point
	// where the title line starts on the inside; borderInsetW/H is the same margin on
	// both sides together.
	//
	// Why four fields and not two: RM_S rounds BEFORE the sum. Below
	// scale 100, RM_S(6) is not the same as 2*RM_S(3), and this
	// restructuring must not move a pixel at any other scale either.
	s16 borderX;
	s16 borderY;
	s16 borderInsetW;
	s16 borderInsetH;

	// THE LIGHT EDGE OF THE FRAME, in percent.
	//
	// RECTMENU_DrawOuterRect_LowLevel draws the frame as FOUR separate
	// strips and so far gave all four the same color. This value spreads
	// them by its amount: top and left per channel times (100 + p) / 100, bottom
	// and right times (100 - p) / 100. The set tone stays the middle,
	// it is not shifted itself.
	//
	// ZERO IS THE PREVIOUS STATE, and provably so, not
	// incidentally: at p = 0, (100 + 0) / 100 is one, both factors return
	// every channel unchanged, and every strip gets byte for
	// byte the color it got before. The retail style therefore carries 0 -
	// pause menu, track info and quip keep drawing flat.
	//
	// WHAT THE VALUE CANNOT DO. The frame is drawn additively
	// (RECTMENU_DrawOuterRect_Edge takes TRANS_50_DECAL, measured as blend 2),
	// so it ADDS onto the background. "Darker" therefore means
	// "adds less" here and not "subtracts"; it cannot go below zero, the
	// channels are clamped to 0..255.
	s16 frameBevelPercent;

	// The shadow on the right and at the bottom. Narrow or wide, chosen by
	// drawStyle 0x80 (horizontal) and 0x40 (vertical).
	s16 shadowWNarrow;
	s16 shadowWWide;
	s16 shadowHNarrow;
	s16 shadowHWide;

	// THE TITLE. titleRuleY is the distance of the separator line from the upper
	// inner edge, titleRuleYBig the same distance in the case of big row font
	// (there as an addend to the row height). titleAdvance is the space the
	// title takes from the content below it - the same number in the height calculation,
	// in the text advance and in the highlight, because if the three
	// drifted apart, the glow bar would no longer sit on its row.
	s16 titleRuleY;
	s16 titleRuleYBig;
	s16 titleRuleH;
	s16 titleAdvance;
	s16 titleHeightBig;

	// What a box loses in height when only its title is drawn.
	s16 onlyTitleShrink;

	// Rows and highlight, each an addition to the row height.
	s16 rowTopBig;
	s16 rowExtraBig;
	s16 highlightShrinkBig;

	// The next box of the cascade, and the frame around everything.
	s16 childGapY;
	s16 frameOffsetX;
	s16 frameOffsetY;
	s16 frameExtraW;
	s16 frameExtraH;

	// THE FRAME HEIGHT OF A COLLAPSED BOX, separate from the full one.
	//
	// ONLY_DRAW_TITLE makes a box show only its chosen row
	// - that is how "ARCADE" stands above the chain. Up to here this
	// rest got the same addition as a full box, and those are two things:
	// for the full box it determines the HEIGHT (five rows = 176 points),
	// for the collapsed one the DEPTH BUDGET of the chain. One value cannot do
	// both. With 24 a collapsed box was 48 points high for a
	// 17-point-high glyph, and three levels no longer fit on screen.
	s16 frameExtraHCollapsed;

	// COLORS. Index 1 is the second set, chosen by drawStyle 0x10.
	const u32 *frameColor[2];
	const u32 *fillSolid;
	const u32 *fillSpecial;
	const u32 *fillNormal;
	const u32 *shadowColor;
	const Color *highlight[2];

	// Text style of a row: [0] normal, [1] second set, and the style
	// of a locked row, which overrides both.
	u16 textStyle[2];
	u16 textStyleLocked;

	// Scale in percent and minimum width in characters. Both used to be a
	// function over the menu pointer in MM_NativeMenu.c.
	int scalePercent;
	s16 minWidthChars;
};

// The style with today's values. Every box of the game gets it.
extern const struct RectMenuStyle g_rectMenuStyleRetail;

struct MenuRow
{
	// can have values above 0xFF,
	// such as 0x155 for "Controller 1C",
	// sometimes the top bit 0x8000 is used,
	// like VS 2P,3P,4P in main menu, to
	// determine if the row is "locked"

	// 0x0
	s16 stringIndex;

	// 0x2
	char rowOnPressUp;

	// 0x3
	char rowOnPressDown;

	// 0x4
	char rowOnPressLeft;

	// 0x5
	char rowOnPressRight;
};

struct RectMenu
{
	// 0x0
	s16 stringIndexTitle; // string index of title (null, with no row)

	// position for current frame
	u16 posX_curr; // X position
	u16 posY_curr; // Y position

	// 0x6
	u16 unk1;

	// 0x8
	// This is an int, see FUN_800469dc
	// & 1, centers posY
	// & 2, centers posX
	// & 4, draw only title bar
	// & 0x10, draw ptrNextBox_InHierarchy
	// & 0x20, disable menu input, allow menu funcptr
	// & 0x40, show only highlighted row
	// & 0x28, main menu character select (better meaning)?
	// & 0x80, tiny text in rows
	// & 0x100, hide row highlight
	// & 0x200, center title and row text
	// & 0xFF, row height (state>>7)
	// & 0x400, execute menu funcptr
	// & 0x800, ??? used in end-event menus
	// & 0x1000, needs to close
	// & 0x2000, invisible
	// & 0x4000, big text in title
	// & 0x8000, anyone can use menu
	// & 0x100000, top of menu hierarchy
	// & 0x800000, mute sound of moving cursor
	u32 state;

	// 0xC
	struct MenuRow *rows;

	// 0x10
	void (*funcPtr)(struct RectMenu *m);

	// 0x14
	// text color, box color, etc
	// one-byte variable with
	// two-byte alignment
	u16 drawStyle;

	// 0x16
	// position for previous frame
	s16 posX_prev;
	s16 posY_prev;

	// 0x1a
	s16 rowSelected;

	// 0x1c
	s16 unk1c;

	// 0x1e
	// tells funcPtr why RectMenu called it
	s16 funcState;

	// 0x20
	s16 width;
	s16 height;

	// 0x24
	struct RectMenu *ptrNextBox_InHierarchy;

	// 0x28
	struct RectMenu *ptrPrevBox_InHierarchy;

	// End of 0x2c-byte struct
};

CTR_STATIC_ASSERT(sizeof(struct MenuRow) == 6);
CTR_STATIC_ASSERT(sizeof(struct RectMenu) == 0x2C);

#endif
