#ifndef CTR_NATIVE_NAMESPACE_MENUDECL_H
#define CTR_NATIVE_NAMESPACE_MENUDECL_H

// MENUS AS DATA.
//
// A box, its rows, its texts, its targets and the names of its
// effects live in a text file. What a name means lives in the code:
// three tables {name, function} in game/native_menudecl.c.
//
// THE RULE THAT CARRIES THIS: a verb does not navigate. The target is written as
// "weiter" in the file, the side effect in the code. Otherwise the target would be
// hidden in the code again, and the file could not move it.
//
// WHAT THE FORMAT DOES NOT TOUCH: the retail menus. They work, and a
// conversion would bring no visible result at full regression risk.
// The format must be ABLE to express them - nobody converts them.

// The upper limits, each in exactly one place.
enum
{
	// Slots in the pool. A format box gets a fixed slot, because
	// sdata->ptrActiveMenu points at it while a reload happens.
	NATIVE_MENU_DECL_POOL = 8,

	// Slots in the name table - format boxes and named targets
	// together. The table itself is shorter; this number only sizes the
	// intermediate areas, and an assertion keeps them together.
	NATIVE_MENU_DECL_BOXES = 16,

	// Rows per box. Four boxes stacked leave no room for more at
	// scale 100.
	NATIVE_MENU_DECL_ROWS = 12,

	// Own strings. They lie above all language indices and
	// below MM_NATIVE_LNG_TRACK0, so the two never meet.
	NATIVE_MENU_DECL_STRINGS = 32,
	NATIVE_MENU_DECL_STRING_LEN = 24,

	NATIVE_MENU_DECL_NAME_LEN = 24,
};

// How a child is opened.
enum NativeMenuDeclOpen
{
	// The parent box only shows its title, the child hangs below it -
	// the cascade that RECTMENU_DrawSelf draws itself.
	NATIVE_MENU_DECL_OPEN_STACKED = 0,

	// The box is replaced: sdata->ptrDesiredMenu, a screen change.
	NATIVE_MENU_DECL_OPEN_REPLACE = 1,
};

// A condition answers a question about the game state: 1 or 0.
typedef int (*NativeMenuDeclCondition)(struct RectMenu *box);

// An effect changes the game state and DOES NOT NAVIGATE.
//
// It returns 0 when the row is done with that and the "weiter"
// should NOT be taken. That is no way around the rule but its
// counterpart: an effect may discard a target, it may not choose one.
// It is needed exactly once - for the case that the track list
// became empty between the lock check and the confirmation.
typedef int (*NativeMenuDeclAction)(struct RectMenu *box);

// A source fills the rows of a box at run time. Not used by anyone
// yet - the table exists so that the next list costs one row and no
// decision.
typedef int (*NativeMenuDeclSource)(struct RectMenu *box, int index, char **textOut);

// Reads the declarations. Two phases: first parse and resolve all names,
// then - and only with zero errors - take them into the pool. If the file
// fails, the built-in stock applies. The game always starts.
void NativeMenuDecl_Load(void);

// The effect of a confirmed row. 1 if the file answered this
// row; 0 if it says nothing about it and the caller should carry
// on. Exactly like this a row without "wirkung" falls through into the retail path.
int NativeMenuDecl_Confirm(struct RectMenu *box, int row);

// This page is left without a way back - clear the marker from the
// "ersetzen" branch. See the rule in the header of MM_NativeMenu.c.
void NativeMenuDecl_NoteAbandoned(const struct RectMenu *box);

// The generic proc. The only one a format box gets.
void NativeMenuDecl_Proc(struct RectMenu *box);

// The text for an own index, or NULL if the index is not one of
// ours - then sdata->lngStrings applies as before.
char *NativeMenuDecl_String(s16 index);

// Lays the style fields the file named for this box onto *dst; 1 if
// anything was laid on, 0 if the file names no style for it. The minimum
// width in characters of a box, and the largest one among its drawn
// children, or -1 each.
int NativeMenuDecl_OverlayStyle(struct RectMenuStyle *dst, const struct RectMenu *box);
int NativeMenuDecl_MinWidthChars(const struct RectMenu *box);
int NativeMenuDecl_ChildMinWidthChars(const struct RectMenu *box);

// RELOAD AT RUN TIME.
//
// Called once per frame from RECTMENU_ProcessState, so in step with the tick and never
// in the middle of drawing. Only looks when g_cfg_menuReload is on, and even
// then only once per second. If the file changes (or on the first poll), the
// two-phase load path runs: on errors the old state stays.
void NativeMenuDecl_Tick(void);

// The flag behind it. Default off; no command-line switch sets it at present.
extern int g_cfg_menuReload;

// The last load error, or NULL. So that while editing you see that the
// file is broken without looking into the log.
const char *NativeMenuDecl_LastError(void);

// What a box really drew. RECTMENU_DrawSelf reports its
// finished rectangle here - width and height are computed and not declared,
// and exactly those are what an editor must see.
void NativeMenuDecl_NoteDrawn(const struct RectMenu *box, const RECT *frame);

// THE PICTURE OF THE RESULT, ONCE PER RELOAD.
//
// The editor puts it behind its sketch. Without a background you align a
// box against nothing - the rectangles are right, but whether the box covers
// the logo, no number tells.
//
// Called at the end of the frame, not at reload: the reload happens in the tick
// of RECTMENU_ProcessState, and at that point the picture of this frame is not
// drawn yet. Without the menu reload on the call does nothing and never
// looks at the disk.
void NativeMenuDecl_PictureIfDue(void);

// THE ANCHOR OF A FORMAT BOX, OR -1.
//
// The first connection between the menu format and the widescreen mapper. The
// mapper asks for it when its own table says nothing, and thereby REPLACES
// the thirds rule - it does not supplement it.
//
// Why a question and not an entry in g_nativeUiDecls: a row there carries
// a fixed rectangle or a HUD slot, and a menu box has neither.
// Its rectangle is computed from font, row count and style, it
// changes with every row and in every frame during the fade-in. A
// copied rectangle would be wrong the moment someone adds a row
// - so the drawer says what it drew, and the mapper
// asks for it.
int NativeMenuDecl_AnchorForBox(const int *authoredBox);

// The frame has been evaluated: what RECTMENU_DrawSelf reported no longer applies.
//
// Without this the rectangle of a box that has long stopped being
// drawn would stay, and in the next frame it would claim a foreign
// item. A report is valid for exactly one mapping pass.
void NativeMenuDecl_FrameConsumed(void);

// The name of the box that drew this authored rectangle, or
// NULL. This is how the --ui-elements table gets its names, instead of a
// second path that says the same thing again.
const char *NativeMenuDecl_NameForDrawnBox(const int *authoredBox);

// A request to write the result file, exactly once per reload.
// The drawer queries it as soon as its decisions for the frame are made -
// before that it is not known what a box really drew.
int NativeMenuDecl_TakeResultRequest(void);
const char *NativeMenuDecl_ResultPath(void);

#endif
