#include <common.h>

#ifdef CTR_NATIVE

#include <stdio.h>

// The built-in stock. Generated from menus/nitro-pit.menu at configure time,
// so that the declaration exists exactly ONCE and not once as a file and
// once as a string, which drift apart.
#include "native_menudefault.h"

// ===========================================================================
//  MENUS AS DATA - the load path, the pool, the verb tables, the proc.
// ===========================================================================
//
//  TWO PHASES. First the whole file is parsed and every name resolved,
//  into a staging area. Only when there are zero errors does the result go
//  into the pool. A typo in line 40 therefore does not let lines 1 to
//  39 take effect and drop the rest - either everything or nothing.
//
//  WHY A FIXED POOL. sdata->ptrActiveMenu is a pointer to a
//  struct RectMenu, and it keeps pointing there while a reload happens.
//  A box therefore needs a place that does not move, and the
//  place hangs on the NAME and not on the order in the file.
//
//  WHAT A RELOAD TOUCHES: declaration fields - rows, text, position, style.
//  What it does NOT touch: rowSelected, ptrNextBox_InHierarchy,
//  ptrPrevBox_InHierarchy, NEEDS_TO_CLOSE, ONLY_DRAW_TITLE. That is the
//  state the player is currently in, and it does not belong to the file.

#define NATIVE_MENU_DECL_PATH "menus/nitro-pit.menu"
#define NATIVE_MENU_DECL_RESULT "menus/nitro-pit.ergebnis"
#define NATIVE_MENU_DECL_PICTURE "menus/nitro-pit.bild.ppm"
#define NATIVE_MENU_DECL_LINE 256
#define NATIVE_MENU_DECL_TOKENS 24

// How often to look, in VBlanks. Once per second is enough for a
// hand at the text editor, and more often would mean looking at the disk every frame.
#define NATIVE_MENU_DECL_POLL_VBLANKS 60

// How much play the mapping group->box gets, in authored columns.
// The frame is the outer rectangle, the shadow hangs next to it; without
// some air the check would not find its own box again.
#define NATIVE_MENU_DECL_FIT 16

// RELOAD AT RUN TIME, DEFAULT OFF.
//
// In normal operation the disk is not looked at all the time. When this flag
// is on, the menu turns into the loop "change, see": text editor on one
// screen, game on the other. No command-line switch sets it at present (the
// former --menu-reload is gone), so the file is not read at run time.
int g_cfg_menuReload = 0;

// WHY A CHECKSUM AND NOT THE FILE TIME.
//
// The time needs a platform switch - stat versus _stat versus
// GetFileAttributesEx - and it lies with every editor that keeps the time
// or writes twice in one second. The file is four kilobytes; reading it
// once per second and summing it costs nothing and is a statement
// about the CONTENT.
global_variable u32 s_fileHash;
global_variable int s_fileHashValid;
global_variable int s_nextPollVBlank;
global_variable int s_reloadPending;
global_variable int s_picturePending;

// Defined in platform/native_renderer.c. The unity translation unit
// includes this file BEFORE the platform files, so the prototype stands
// here instead of coming from platform/native_renderer.h - the same solution as
// for DebugMenu_IsOpen in native_uidecl.c.
void NativeRenderer_GetMainTargetSize(int *outWidth, int *outHeight);
int NativeRenderer_ReadMainTarget(void *dst, int dstBytes, int *outWidth, int *outHeight);

// The last error, verbatim. It also stands in the picture, so that while
// editing you do not have to look into the log to notice that nothing arrived.
// AS LONG AS ONE LINE FITS INTO THE PICTURE, AND NOT ONE CHARACTER LONGER.
//
// The canvas is 512 columns wide, FONT_SMALL is 13 px: 39 characters. Whatever
// went beyond that would stand outside the picture at 4:3 and would be at
// 43:18 a group in the right third that the mapper pushes to the right.
// The full wording is in the log; here stands what can be seen.
#define NATIVE_MENU_DECL_ERROR_CHARS 39

global_variable char s_lastError[NATIVE_MENU_DECL_ERROR_CHARS + 1];

// What was last really drawn, per box. Filled by
// RECTMENU_DrawSelf - width and height are computed there, not declared.
struct NativeMenuDeclDrawn
{
	int valid;
	int x0;
	int y0;
	int x1;
	int y1;
};

global_variable struct NativeMenuDeclDrawn s_drawn[NATIVE_MENU_DECL_BOXES];

// ---------------------------------------------------------------------------
//  The three verb tables.
//
//  The functions stand where their data stands - the Nitro-Pit verbs in
//  MM_NativeMenu.c, next to the containers. Here stand only
//  the names. A verb that is missing here is a load error with a line number
//  and not a silent nothing.
// ---------------------------------------------------------------------------

struct NativeMenuDeclConditionRow
{
	const char *name;
	NativeMenuDeclCondition fn;
};

struct NativeMenuDeclActionRow
{
	const char *name;
	NativeMenuDeclAction fn;
};

struct NativeMenuDeclSourceRow
{
	const char *name;
	NativeMenuDeclSource fn;
};

global_variable const struct NativeMenuDeclConditionRow s_conditions[] = {
    {"tracks-vorhanden", MM_NativeTracks_CondPresent},
    {"immer", MM_NativeMenu_CondAlways},
};

global_variable const struct NativeMenuDeclActionRow s_actions[] = {
    {"nitro-pit", MM_NativeTracks_ActNitroPit},
    {"nitro-auswahl-auf", MM_NativeTracks_ActOpenSelect},
    {"nitro-auswahl-zu", MM_NativeTracks_ActCloseSelect},
    {"quit", MM_NativeMenu_ActQuit},

    // The arcade path. The names are modes, not buttons.
    {"modus-einzel", MM_NativeMode_ActSingle},
    {"modus-cup", MM_NativeMode_ActCup},
    {"modus-zeitfahren", MM_NativeMode_ActTimeTrial},
    {"modus-vs", MM_NativeMode_ActVs},
    {"modus-battle", MM_NativeMode_ActBattle},
    {"vs-einzel", MM_NativeMode_ActVsSingle},
    {"vs-cup", MM_NativeMode_ActVsCup},
};

// Still empty, and that is stated here explicitly: nothing reads this table
// yet. A dynamic list (the containers, later the profiles) would hook in here.
// tools/menued reads the names from this table.
global_variable const struct NativeMenuDeclSourceRow s_sources[] = {
    {NULL, NULL},
};

// ---------------------------------------------------------------------------
//  The pool and the slots.
// ---------------------------------------------------------------------------

global_variable struct RectMenu s_pool[NATIVE_MENU_DECL_POOL];

// WAS THIS BOX OPENED BY REPLACING?
//
// The difference can only be seen on the WAY BACK, and there it is
// complete: a stacked child clears the bits of the parent and
// disappears - the parent box was the active one all along. A replaced
// box IS the active one; its parent must be put back in, otherwise
// no menu at all remains after the way back.
//
// The marker stands here and not in the state of the box: there no bit
// is free that does not already mean something else.
global_variable u8 s_openedReplaced[NATIVE_MENU_DECL_BOXES];

struct NativeMenuDeclSlot
{
	const char *name;

	// The fixed place. Either a pool entry or a box that already
	// exists.
	struct RectMenu *box;

	// 1: the file writes the declaration fields of this box.
	// 0: the box is only a TARGET that "weiter" may name. The file
	//    does not touch it - that way the retail menus stay as they are.
	int declared;

	// 1: a pure format box. It gets the generic proc, and on
	//    opening its state is reset to the declared bits.
	// 0: the box already has a proc that belongs to it.
	int generic;

	// THE RESTING POSITION DOES NOT ALWAYS LIE IN THE BOX.
	//
	// For the main menu MM_Title_MenuUpdate rewrites posX_curr in every frame,
	// from D230.titleMainMenuPos plus the value of the fade-in. Whoever
	// sets posX_curr is overwritten one frame later. The
	// resting position of this box is therefore titleMainMenuPos, and this field
	// says that it lies there - instead of knowing it at some place in the code.
	int posInTitleAnchor;

	// THE POSITION IS AN OFFSET IN THE CASCADE AND NOT A PLACE IN THE PICTURE.
	//
	// RECTMENU_DrawSelf hands a child posX + posX_prev of the parent
	// and adds its own posX_curr to that. For a box of the
	// cascade x and y are therefore a shift against the place the
	// drawer sets anyway - not a spot in the reference space. The editor would draw
	// them at the wrong spot otherwise, and it should be able to read this fact here
	// instead of knowing it.
	int posIsCascadeOffset;
};

// hauptmenue is declared, but NOT generic, and that is the one place
// where a box of the file keeps a foreign proc: on
// D230.menuMainMenu hang the fade-in of the title, the demo counter and
// the cheat codes. The file sets its rows and its position - its
// behaviour still belongs to MM_MenuProc_Main. A format box GETS no
// proc of its own; this one already had one.
global_variable const struct NativeMenuDeclSlot s_slots[] = {
    {"hauptmenue", &D230.menuMainMenu, 1, 0, 1, 0},
    {"custom-track", &s_pool[0], 1, 1, 0, 1},

    // THE FOUR RETAIL BOXES OF THE CHAIN. Declarable, but without rows - and that
    // is no half measure but the limit that was measured.
    //
    // Their rows carry their meaning in the INDEX: MM_MenuProc_1p2p computes
    // numPlyrNextGame = rowSelected + 1, MM_MenuProc_Difficulty reads
    // D230.cupDifficulty.speed[rowSelected], MM_MenuProc_SingleCup asks
    // rowSelected != 0. A file that may add, delete and
    // reorder rows would thereby silently mean something else - "5 LAPS"
    // drives three. Besides, MM_ToggleRows_PlayerCount writes in EVERY frame
    // the lock bit into the same row fields, for adventure, arcade,
    // VS and battle at once; the conditions of the format are evaluated at loading and
    // at opening, not per frame.
    //
    // What the file may set here, it sets completely: position, title, the bits
    // it speaks about, and the minimum width of the chain.
    {"spieler-1p2p", &D230.menuPlayers1P2P, 1, 0, 0, 1},
    {"spieler-2p3p4p", &D230.menuPlayers2P3P4P, 1, 0, 0, 1},
    {"renntyp", &D230.menuRaceType, 1, 0, 0, 1},
    {"schwierigkeit", &D230.menuDifficulty, 1, 0, 0, 1},
    {"abenteuer", &D230.menuAdventure, 0, 0, 0, 1},

    // TWO PAGES THAT REPLACE INSTEAD OF STACKING, and are therefore roots of their own.
    // A root gets its own style (RECTMENU_ProcessState) and its own
    // middle - that is why its position here carries a place in the reference space and
    // not an offset in the cascade.
    {"renn-modus", &s_pool[1], 1, 1, 0, 0},
    {"vs-renntyp", &s_pool[2], 1, 1, 0, 0},
};

#define NATIVE_MENU_DECL_SLOTS ((int)len(s_slots))
#define NATIVE_MENU_DECL_STYLE_FIELDS_MAX 32

// How many characters a declared minimum width may demand at most. 64
// characters in FONT_BIG are 1088 points - twice as wide as the reference space.
// The limit catches a typo and is no statement about the picture.
#define NATIVE_MENU_DECL_MIN_WIDTH_MAX 64

CTR_STATIC_ASSERT(len(s_slots) <= NATIVE_MENU_DECL_BOXES);

// ---------------------------------------------------------------------------
//  What a parsed declaration carries.
// ---------------------------------------------------------------------------

struct NativeMenuDeclRowSpec
{
	s16 text;
	s16 textLocked; // -1: the same text as in the open case
	s8 condition;   // index into s_conditions, -1
	s8 negate;      // gesperrt-wenn-nicht
	s8 action;      // index into s_actions, -1
	s8 target;      // index into s_slots, -1
	s8 open;        // enum NativeMenuDeclOpen
};

struct NativeMenuDeclBox
{
	int declared;
	s16 x;
	s16 y;
	int hasPos;

	// TWO WORDS, NOT ONE.
	//
	// layoutBits says which bits are set. layoutMask says which ones
	// the file speaks about at all - and only those may be reset.
	//
	// The difference only shows with a box that already belongs to someone.
	// D230.menuMainMenu.state carries EXECUTE_FUNCPTR and
	// DISABLE_INPUT_ALLOW_FUNCPTRS, which the title screen toggles in every frame;
	// a plain assignment would tear them away. A mere adding on the other hand can
	// never switch anything OFF - "schrift gross" would be a key that only works in
	// one direction and silently does nothing in the other.
	u32 layoutBits;
	u32 layoutMask;
	s16 titleText;
	int hasTitle;
	s8 backAction;

	// The declared anchor, or -1. It is NOT a state bit: RECTMENU does not know it,
	// only the widescreen mapper reads it.
	s8 anchor;

	// THE DECLARED STYLE AS AN INDEX AND NOT AS A POINTER. Every style
	// has a mask - which of its fields the file named at all
	// -, and from a pointer there is no way back to it.
	s8 styleIndex;

	// THE MINIMUM WIDTH IN CHARACTERS, or -1. It is no state bit and no
	// style field but a statement about THIS box - that is why it stands here
	// and not in s_styleFields, where it would need a whole style.
	s16 minWidth;

	int rowCount;
	struct NativeMenuDeclRowSpec rows[NATIVE_MENU_DECL_ROWS];
};

// The staging area - phase one writes here, phase two takes it over.
global_variable struct NativeMenuDeclBox s_staging[NATIVE_MENU_DECL_BOXES];

// The result, and the rows that RECTMENU really reads. Both are
// fixed, because menu->rows is a pointer to here.
global_variable struct NativeMenuDeclBox s_boxes[NATIVE_MENU_DECL_BOXES];
global_variable struct MenuRow s_rows[NATIVE_MENU_DECL_BOXES][NATIVE_MENU_DECL_ROWS + 1];

// Own strings, index MM_NATIVE_LNG_BASE + n.
global_variable char s_stringPool[NATIVE_MENU_DECL_STRINGS][NATIVE_MENU_DECL_STRING_LEN];
global_variable int s_stringCount;
global_variable char s_stagingStrings[NATIVE_MENU_DECL_STRINGS][NATIVE_MENU_DECL_STRING_LEN];
global_variable int s_stagingStringCount;

// Own styles, derived from retail.
global_variable struct RectMenuStyle s_styles[NATIVE_MENU_DECL_POOL];
global_variable char s_styleNames[NATIVE_MENU_DECL_POOL][NATIVE_MENU_DECL_NAME_LEN];
global_variable int s_styleCount;

// WHICH FIELDS THE FILE NAMED, bit for bit in the order of
// s_styleFields. The same pair as layoutBits and layoutMask, and for the
// same reason: a style that only says "rahmen-x" should also only change rahmen-x.
// Without the mask it carries all 22 fields of retail with it and
// thereby silently overwrites what it is laid onto.
global_variable u32 s_styleMask[NATIVE_MENU_DECL_POOL];

// ---------------------------------------------------------------------------
//  The fields a "stil" block may set.
//
//  Only the distances. Scale and minimum width belong to the style the code
//  keeps (scalePercent, minWidthChars), and two places that set the same
//  number drift apart.
// ---------------------------------------------------------------------------

struct NativeMenuDeclStyleField
{
	const char *name;
	u32 offset;
};

// The mask is a u32.
CTR_STATIC_ASSERT(NATIVE_MENU_DECL_STYLE_FIELDS_MAX <= 32);

global_variable const struct NativeMenuDeclStyleField s_styleFields[] = {
    {"rahmen-x", OFFSETOF(struct RectMenuStyle, borderX)},
    {"rahmen-y", OFFSETOF(struct RectMenuStyle, borderY)},
    {"rahmen-einzug-w", OFFSETOF(struct RectMenuStyle, borderInsetW)},
    {"rahmen-einzug-h", OFFSETOF(struct RectMenuStyle, borderInsetH)},
    {"rahmen-lichtkante", OFFSETOF(struct RectMenuStyle, frameBevelPercent)},
    {"schatten-w-schmal", OFFSETOF(struct RectMenuStyle, shadowWNarrow)},
    {"schatten-w-breit", OFFSETOF(struct RectMenuStyle, shadowWWide)},
    {"schatten-h-schmal", OFFSETOF(struct RectMenuStyle, shadowHNarrow)},
    {"schatten-h-breit", OFFSETOF(struct RectMenuStyle, shadowHWide)},
    {"titel-linie-y", OFFSETOF(struct RectMenuStyle, titleRuleY)},
    {"titel-linie-y-gross", OFFSETOF(struct RectMenuStyle, titleRuleYBig)},
    {"titel-linie-h", OFFSETOF(struct RectMenuStyle, titleRuleH)},
    {"titel-vorschub", OFFSETOF(struct RectMenuStyle, titleAdvance)},
    {"titel-hoehe-gross", OFFSETOF(struct RectMenuStyle, titleHeightBig)},
    {"nur-titel-abzug", OFFSETOF(struct RectMenuStyle, onlyTitleShrink)},
    {"zeile-oben-gross", OFFSETOF(struct RectMenuStyle, rowTopBig)},
    {"zeile-zuschlag-gross", OFFSETOF(struct RectMenuStyle, rowExtraBig)},
    {"hervorhebung-abzug-gross", OFFSETOF(struct RectMenuStyle, highlightShrinkBig)},
    {"kind-abstand", OFFSETOF(struct RectMenuStyle, childGapY)},
    {"rahmen-versatz-x", OFFSETOF(struct RectMenuStyle, frameOffsetX)},
    {"rahmen-versatz-y", OFFSETOF(struct RectMenuStyle, frameOffsetY)},
    {"rahmen-zuschlag-w", OFFSETOF(struct RectMenuStyle, frameExtraW)},
    {"rahmen-zuschlag-h", OFFSETOF(struct RectMenuStyle, frameExtraH)},
    {"rahmen-zuschlag-h-eingeklappt", OFFSETOF(struct RectMenuStyle, frameExtraHCollapsed)},
};

// ---------------------------------------------------------------------------
//  Splitting a line.
//
//  Whitespace separates, "..." holds together, # starts a comment. The
//  line is cut up in the process - the caller does not need it afterwards.
// ---------------------------------------------------------------------------

internal int NativeMenuDecl_Tokenize(char *line, char **tok, int max)
{
	int count = 0;
	char *p = line;

	while ((*p != '\0') && (count < max))
	{
		while ((*p == ' ') || (*p == '\t') || (*p == '\r') || (*p == '\n'))
		{
			p++;
		}

		if ((*p == '\0') || (*p == '#'))
		{
			break;
		}

		if (*p == '"')
		{
			p++;
			tok[count++] = p;

			while ((*p != '\0') && (*p != '"'))
			{
				p++;
			}

			if (*p == '"')
			{
				*p = '\0';
				p++;
			}

			continue;
		}

		tok[count++] = p;

		while ((*p != '\0') && (*p != ' ') && (*p != '\t') && (*p != '\r') && (*p != '\n'))
		{
			p++;
		}

		if (*p != '\0')
		{
			*p = '\0';
			p++;
		}
	}

	return count;
}

internal int NativeMenuDecl_ParseNumber(const char *s, int *out)
{
	int value = 0;
	int digits = 0;
	int base = 10;
	int negative = 0;

	if (*s == '-')
	{
		negative = 1;
		s++;
	}

	if ((s[0] == '0') && ((s[1] == 'x') || (s[1] == 'X')))
	{
		base = 16;
		s += 2;
	}

	while (*s != '\0')
	{
		int digit;

		if ((*s >= '0') && (*s <= '9'))
		{
			digit = *s - '0';
		}
		else if ((base == 16) && (*s >= 'a') && (*s <= 'f'))
		{
			digit = (*s - 'a') + 10;
		}
		else if ((base == 16) && (*s >= 'A') && (*s <= 'F'))
		{
			digit = (*s - 'A') + 10;
		}
		else
		{
			return 0;
		}

		value = (value * base) + digit;
		digits++;
		s++;
	}

	if (digits == 0)
	{
		return 0;
	}

	*out = negative ? -value : value;
	return 1;
}

internal int NativeMenuDecl_SlotByName(const char *name)
{
	int i;

	for (i = 0; i < NATIVE_MENU_DECL_SLOTS; i++)
	{
		if (strcmp(s_slots[i].name, name) == 0)
		{
			return i;
		}
	}

	return -1;
}

internal int NativeMenuDecl_ConditionByName(const char *name)
{
	int i;

	for (i = 0; i < (int)len(s_conditions); i++)
	{
		if (strcmp(s_conditions[i].name, name) == 0)
		{
			return i;
		}
	}

	return -1;
}

internal int NativeMenuDecl_ActionByName(const char *name)
{
	int i;

	for (i = 0; i < (int)len(s_actions); i++)
	{
		if (strcmp(s_actions[i].name, name) == 0)
		{
			return i;
		}
	}

	return -1;
}

internal int NativeMenuDecl_StyleByName(const char *name)
{
	int i;

	for (i = 0; i < s_styleCount; i++)
	{
		if (strcmp(s_styleNames[i], name) == 0)
		{
			return i;
		}
	}

	return -1;
}

// Stores an own string and returns its text index, or -1.
internal s16 NativeMenuDecl_AddString(const char *text)
{
	int i = 0;

	if (s_stagingStringCount >= NATIVE_MENU_DECL_STRINGS)
	{
		return -1;
	}

	while ((i < (NATIVE_MENU_DECL_STRING_LEN - 1)) && (text[i] != '\0'))
	{
		s_stagingStrings[s_stagingStringCount][i] = text[i];
		i++;
	}

	s_stagingStrings[s_stagingStringCount][i] = '\0';

	return (s16)(MM_NATIVE_LNG_BASE + s_stagingStringCount++);
}

char *NativeMenuDecl_String(s16 index)
{
	const int slot = (int)index - MM_NATIVE_LNG_BASE;

	if ((slot >= 0) && (slot < s_stringCount))
	{
		return &s_stringPool[slot][0];
	}

	return NULL;
}

// ---------------------------------------------------------------------------
//  Phase one: parse and resolve.
// ---------------------------------------------------------------------------

struct NativeMenuDeclParse
{
	const char *source;
	int errors;
	int line;

	// 0 no block, 1 kasten, 2 stil
	int blockKind;
	int blockIndex;
};

internal void NativeMenuDecl_Error(struct NativeMenuDeclParse *ps, const char *what, const char *detail)
{
	ps->errors++;

	// The FIRST error stays, not the last: the later ones are often
	// consequences of the first, and whoever edits the file wants to know the spot
	// where it first went wrong.
	if (ps->errors == 1)
	{
		// Without the path: it stands in the log line directly below and would cost
		// two thirds of the space the statement needs here.
		snprintf(s_lastError, sizeof(s_lastError), "%d: %s '%s'", ps->line, what, detail);
	}

	Platform_LogWarn("[CTR Menu] %s:%d: %s '%s'\n", ps->source, ps->line, what, detail);
}

// A "text" value: either "lng <number>" (two fields) or an own
// string (one field). Returns the number of fields used.
internal int NativeMenuDecl_ParseText(struct NativeMenuDeclParse *ps, char **tok, int count, int at, s16 *out)
{
	if (at >= count)
	{
		NativeMenuDecl_Error(ps, "without a value", tok[at - 1]);
		return 0;
	}

	if (strcmp(tok[at], "lng") == 0)
	{
		int value = 0;

		if (((at + 1) >= count) || !NativeMenuDecl_ParseNumber(tok[at + 1], &value))
		{
			NativeMenuDecl_Error(ps, "not a number after lng", tok[at]);
			return 0;
		}

		*out = (s16)value;
		return 2;
	}

	*out = NativeMenuDecl_AddString(tok[at]);

	if (*out < 0)
	{
		NativeMenuDecl_Error(ps, "too many own strings", tok[at]);
		return 0;
	}

	return 1;
}

internal void NativeMenuDecl_ParseRow(struct NativeMenuDeclParse *ps, char **tok, int count)
{
	struct NativeMenuDeclBox *box = &s_staging[ps->blockIndex];
	struct NativeMenuDeclRowSpec *row;
	int at = 2;

	if (count < 2)
	{
		NativeMenuDecl_Error(ps, "zeile without a name", tok[0]);
		return;
	}

	if (box->rowCount >= NATIVE_MENU_DECL_ROWS)
	{
		NativeMenuDecl_Error(ps, "too many rows", tok[1]);
		return;
	}

	row = &box->rows[box->rowCount];
	row->text = RECTMENU_STRING_NONE;
	row->textLocked = -1;
	row->condition = -1;
	row->negate = 0;
	row->action = -1;
	row->target = -1;
	row->open = (s8)NATIVE_MENU_DECL_OPEN_STACKED;

	while (at < count)
	{
		const char *key = tok[at];

		if (strcmp(key, "text") == 0)
		{
			const int used = NativeMenuDecl_ParseText(ps, tok, count, at + 1, &row->text);

			if (used == 0)
			{
				return;
			}

			at += 1 + used;
			continue;
		}

		if (strcmp(key, "text-gesperrt") == 0)
		{
			const int used = NativeMenuDecl_ParseText(ps, tok, count, at + 1, &row->textLocked);

			if (used == 0)
			{
				return;
			}

			at += 1 + used;
			continue;
		}

		if ((strcmp(key, "gesperrt-wenn") == 0) || (strcmp(key, "gesperrt-wenn-nicht") == 0))
		{
			int found;

			if ((at + 1) >= count)
			{
				NativeMenuDecl_Error(ps, "without a value", key);
				return;
			}

			found = NativeMenuDecl_ConditionByName(tok[at + 1]);

			if (found < 0)
			{
				NativeMenuDecl_Error(ps, "no such condition", tok[at + 1]);
				return;
			}

			row->condition = (s8)found;
			row->negate = (strcmp(key, "gesperrt-wenn-nicht") == 0) ? 1 : 0;
			at += 2;
			continue;
		}

		if (strcmp(key, "wirkung") == 0)
		{
			int found;

			if ((at + 1) >= count)
			{
				NativeMenuDecl_Error(ps, "without a value", key);
				return;
			}

			found = NativeMenuDecl_ActionByName(tok[at + 1]);

			if (found < 0)
			{
				NativeMenuDecl_Error(ps, "no such action", tok[at + 1]);
				return;
			}

			row->action = (s8)found;
			at += 2;
			continue;
		}

		if (strcmp(key, "weiter") == 0)
		{
			int found;

			if ((at + 1) >= count)
			{
				NativeMenuDecl_Error(ps, "without a value", key);
				return;
			}

			found = NativeMenuDecl_SlotByName(tok[at + 1]);

			if (found < 0)
			{
				NativeMenuDecl_Error(ps, "no such kasten", tok[at + 1]);
				return;
			}

			row->target = (s8)found;
			at += 2;
			continue;
		}

		if (strcmp(key, "oeffnen") == 0)
		{
			if ((at + 1) >= count)
			{
				NativeMenuDecl_Error(ps, "without a value", key);
				return;
			}

			if (strcmp(tok[at + 1], "gestapelt") == 0)
			{
				row->open = (s8)NATIVE_MENU_DECL_OPEN_STACKED;
			}
			else if (strcmp(tok[at + 1], "ersetzen") == 0)
			{
				row->open = (s8)NATIVE_MENU_DECL_OPEN_REPLACE;
			}
			else
			{
				NativeMenuDecl_Error(ps, "neither gestapelt nor ersetzen", tok[at + 1]);
				return;
			}

			at += 2;
			continue;
		}

		NativeMenuDecl_Error(ps, "not a zeile key", key);
		return;
	}

	if (row->text == RECTMENU_STRING_NONE)
	{
		NativeMenuDecl_Error(ps, "zeile without text", tok[1]);
		return;
	}

	box->rowCount++;
}

internal void NativeMenuDecl_ParseBoxKey(struct NativeMenuDeclParse *ps, char **tok, int count)
{
	struct NativeMenuDeclBox *box = &s_staging[ps->blockIndex];
	const char *key = tok[0];
	int value = 0;

	if (strcmp(key, "zeile") == 0)
	{
		NativeMenuDecl_ParseRow(ps, tok, count);
		return;
	}

	if (count < 2)
	{
		NativeMenuDecl_Error(ps, "without a value", key);
		return;
	}

	if (strcmp(key, "x") == 0)
	{
		if (!NativeMenuDecl_ParseNumber(tok[1], &value))
		{
			NativeMenuDecl_Error(ps, "not a number", tok[1]);
			return;
		}

		box->x = (s16)value;
		box->hasPos = 1;
		return;
	}

	if (strcmp(key, "y") == 0)
	{
		if (!NativeMenuDecl_ParseNumber(tok[1], &value))
		{
			NativeMenuDecl_Error(ps, "not a number", tok[1]);
			return;
		}

		box->y = (s16)value;
		box->hasPos = 1;
		return;
	}

	if (strcmp(key, "bezug") == 0)
	{
		if (strcmp(tok[1], "mitte") == 0)
		{
			box->layoutBits |= CENTER_ON_COORDS;
		}
		else if (strcmp(tok[1], "mitte-x") == 0)
		{
			box->layoutBits |= CENTER_ON_X;
		}
		else if (strcmp(tok[1], "mitte-y") == 0)
		{
			box->layoutBits |= CENTER_ON_Y;
		}
		else if (strcmp(tok[1], "ecke") != 0)
		{
			NativeMenuDecl_Error(ps, "no such bezug", tok[1]);
			return;
		}

		// "ecke" also owns the bits - otherwise it would be the only value of the
		// key that says nothing.
		box->layoutMask |= (u32)CENTER_ON_COORDS;
		return;
	}

	// THE FONT OF THE ROWS.
	//
	// USE_SMALL_FONT is read by RECTMENU in exactly one place and picks
	// FONT_SMALL instead of FONT_BIG for title and rows. The style carries the
	// additions next to it, but not the font itself: the row height comes
	// from data.font_charPixHeight, and which of the two fonts applies is
	// a statement about the box and not about its dimensions.
	if (strcmp(key, "schrift") == 0)
	{
		if (strcmp(tok[1], "klein") == 0)
		{
			box->layoutBits |= (u32)USE_SMALL_FONT;
		}
		else if (strcmp(tok[1], "gross") != 0)
		{
			NativeMenuDecl_Error(ps, "neither klein nor gross", tok[1]);
			return;
		}

		box->layoutMask |= (u32)USE_SMALL_FONT;
		return;
	}

	// WHERE THE TEXT STANDS IN ITS ROW.
	//
	// CENTER_MENU_TEXT centres title and row text in the box width,
	// instead of letting both begin at the left inner edge. It is a statement
	// about the box and not about the single row - RECTMENU knows
	// no alignment per row, and a key on the row would be a
	// promise the drawer cannot keep.
	if (strcmp(key, "ausrichtung") == 0)
	{
		if (strcmp(tok[1], "mitte") == 0)
		{
			box->layoutBits |= (u32)CENTER_MENU_TEXT;
		}
		else if (strcmp(tok[1], "links") != 0)
		{
			NativeMenuDecl_Error(ps, "neither links nor mitte", tok[1]);
			return;
		}

		box->layoutMask |= (u32)CENTER_MENU_TEXT;
		return;
	}

	// AT WHICH EDGE THE BOX HANGS WHEN THE PICTURE GETS WIDER.
	//
	// Without this key the mapper guesses: CTR_UI_AnchorForBox divides the
	// canvas into thirds and reads the middle of the box. The rule is right
	// most of the time and answers a question nobody wrote down - a
	// box whose width grows with its rows can push its middle across
	// a thirds boundary and change the anchor without anybody having
	// decided anything. Exactly that is what the main menu does during its
	// fade-in: it slides in from the right, and on the way the rule answers
	// "right", at rest "centre".
	//
	// "leinwand" is not an edge but the statement "do not map" - for
	// a layer that is laid over the whole picture.
	if (strcmp(key, "anker") == 0)
	{
		if (strcmp(tok[1], "mitte") == 0)
		{
			box->anchor = (s8)CTR_UI_ANCHOR_CENTRE;
		}
		else if (strcmp(tok[1], "links") == 0)
		{
			box->anchor = (s8)CTR_UI_ANCHOR_LEFT;
		}
		else if (strcmp(tok[1], "rechts") == 0)
		{
			box->anchor = (s8)CTR_UI_ANCHOR_RIGHT;
		}
		else if (strcmp(tok[1], "leinwand") == 0)
		{
			box->anchor = (s8)CTR_UI_ANCHOR_FULL_CANVAS;
		}
		else
		{
			NativeMenuDecl_Error(ps, "no such anker", tok[1]);
		}

		return;
	}

	if (strcmp(key, "layout") == 0)
	{
		// One column, clamped at top and bottom, left and right onto
		// itself - exactly what the four numbers per row used to write down by hand.
		// There is nothing more yet, and what does not exist is
		// an error and not a silent default.
		if (strcmp(tok[1], "spalte") != 0)
		{
			NativeMenuDecl_Error(ps, "no such layout", tok[1]);
		}

		return;
	}

	if (strcmp(key, "titel") == 0)
	{
		if (strcmp(tok[1], "keiner") == 0)
		{
			box->titleText = RECTMENU_STRING_NONE;
			box->hasTitle = 1;
			return;
		}

		if (NativeMenuDecl_ParseText(ps, tok, count, 1, &box->titleText) == 0)
		{
			return;
		}

		box->hasTitle = 1;
		return;
	}

	// THE MINIMUM WIDTH, IN CHARACTERS AND NOT IN POINTS.
	//
	// The drawer computes it in RECTMENU_ProcessState from the font width
	// and the scale: N * FP_Mult(charPixWidth[FONT_BIG], scale) + 1. The
	// point count therefore hangs on the menu scale, and a number in points in the
	// file would miss as soon as the scale is not 100.
	//
	// "breite mindestens N" and not "mindestbreite N": the word in between
	// leaves room for "breite fest N", should it ever be needed, without
	// a second key standing next to it.
	if (strcmp(key, "breite") == 0)
	{
		if (count < 3)
		{
			NativeMenuDecl_Error(ps, "breite needs 'mindestens' and a number", key);
			return;
		}

		if (strcmp(tok[1], "mindestens") != 0)
		{
			NativeMenuDecl_Error(ps, "no word after breite", tok[1]);
			return;
		}

		if (!NativeMenuDecl_ParseNumber(tok[2], &value))
		{
			NativeMenuDecl_Error(ps, "not a number", tok[2]);
			return;
		}

		if ((value < 0) || (value > NATIVE_MENU_DECL_MIN_WIDTH_MAX))
		{
			NativeMenuDecl_Error(ps, "outside 0..64 characters", tok[2]);
			return;
		}

		box->minWidth = (s16)value;
		return;
	}

	if (strcmp(key, "nimmt-stil") == 0)
	{
		const int found = NativeMenuDecl_StyleByName(tok[1]);

		if (found < 0)
		{
			NativeMenuDecl_Error(ps, "no such stil", tok[1]);
			return;
		}

		box->styleIndex = (s8)found;
		return;
	}

	if (strcmp(key, "zurueck") == 0)
	{
		const int found = NativeMenuDecl_ActionByName(tok[1]);

		if (found < 0)
		{
			NativeMenuDecl_Error(ps, "no such action", tok[1]);
			return;
		}

		box->backAction = (s8)found;
		return;
	}

	NativeMenuDecl_Error(ps, "not a kasten key", key);
}

internal void NativeMenuDecl_ParseStyleKey(struct NativeMenuDeclParse *ps, char **tok, int count)
{
	int i;
	int value = 0;

	if (count < 2)
	{
		NativeMenuDecl_Error(ps, "without a value", tok[0]);
		return;
	}

	for (i = 0; i < (int)len(s_styleFields); i++)
	{
		if (strcmp(s_styleFields[i].name, tok[0]) == 0)
		{
			if (!NativeMenuDecl_ParseNumber(tok[1], &value))
			{
				NativeMenuDecl_Error(ps, "not a number", tok[1]);
				return;
			}

			*(s16 *)((char *)&s_styles[ps->blockIndex] + s_styleFields[i].offset) = (s16)value;
			s_styleMask[ps->blockIndex] |= (u32)1 << i;
			return;
		}
	}

	NativeMenuDecl_Error(ps, "not a stil key", tok[0]);
}

internal void NativeMenuDecl_ParseLine(struct NativeMenuDeclParse *ps, char *line)
{
	char *tok[NATIVE_MENU_DECL_TOKENS];
	const int count = NativeMenuDecl_Tokenize(line, tok, NATIVE_MENU_DECL_TOKENS);

	if (count == 0)
	{
		return;
	}

	if (strcmp(tok[0], "kasten") == 0)
	{
		int found;

		if (count < 2)
		{
			NativeMenuDecl_Error(ps, "kasten without a name", tok[0]);
			ps->blockKind = 0;
			return;
		}

		found = NativeMenuDecl_SlotByName(tok[1]);

		if (found < 0)
		{
			NativeMenuDecl_Error(ps, "no such kasten", tok[1]);
			ps->blockKind = 0;
			return;
		}

		if (!s_slots[found].declared)
		{
			NativeMenuDecl_Error(ps, "is a target only, never declared", tok[1]);
			ps->blockKind = 0;
			return;
		}

		if (s_staging[found].declared)
		{
			NativeMenuDecl_Error(ps, "declared twice", tok[1]);
			ps->blockKind = 0;
			return;
		}

		s_staging[found].declared = 1;
		s_staging[found].titleText = RECTMENU_STRING_NONE;
		s_staging[found].backAction = -1;

		// Explicitly, because 0 is a valid anchor (the centre). A
		// zeroed field would mean "centre declared" instead of "nothing said".
		s_staging[found].anchor = -1;
		s_staging[found].styleIndex = -1;
		s_staging[found].minWidth = -1;
		ps->blockKind = 1;
		ps->blockIndex = found;
		return;
	}

	if (strcmp(tok[0], "stil") == 0)
	{
		if (count < 2)
		{
			NativeMenuDecl_Error(ps, "stil without a name", tok[0]);
			ps->blockKind = 0;
			return;
		}

		if (NativeMenuDecl_StyleByName(tok[1]) >= 0)
		{
			NativeMenuDecl_Error(ps, "declared twice", tok[1]);
			ps->blockKind = 0;
			return;
		}

		if (s_styleCount >= NATIVE_MENU_DECL_POOL)
		{
			NativeMenuDecl_Error(ps, "too many styles", tok[1]);
			ps->blockKind = 0;
			return;
		}

		// Derived and not copied: colors, scale and everything
		// the file does not name come from retail.
		s_styles[s_styleCount] = g_rectMenuStyleRetail;
		s_styleMask[s_styleCount] = 0;
		snprintf(&s_styleNames[s_styleCount][0], NATIVE_MENU_DECL_NAME_LEN, "%s", tok[1]);
		ps->blockKind = 2;
		ps->blockIndex = s_styleCount;
		s_styleCount++;
		return;
	}

	if (ps->blockKind == 1)
	{
		NativeMenuDecl_ParseBoxKey(ps, tok, count);
		return;
	}

	if (ps->blockKind == 2)
	{
		NativeMenuDecl_ParseStyleKey(ps, tok, count);
		return;
	}

	NativeMenuDecl_Error(ps, "outside any block", tok[0]);
}

// ---------------------------------------------------------------------------
//  Phase two: take over.
// ---------------------------------------------------------------------------

// Text and lock bit of a row, from the condition. Separate, because it is needed at
// loading AND at opening a box.
internal void NativeMenuDecl_BuildRows(int slot)
{
	const struct NativeMenuDeclBox *box = &s_boxes[slot];
	int i;

	for (i = 0; i < box->rowCount; i++)
	{
		const struct NativeMenuDeclRowSpec *spec = &box->rows[i];
		struct MenuRow *out = &s_rows[slot][i];
		int locked = 0;

		if (spec->condition >= 0)
		{
			const int met = s_conditions[spec->condition].fn(s_slots[slot].box) != 0;

			locked = spec->negate ? !met : met;
		}

		out->stringIndex = (s16)((locked && (spec->textLocked >= 0)) ? spec->textLocked : spec->text);

		if (locked)
		{
			out->stringIndex |= MENU_ROW_LOCKED;
		}

		// layout spalte: clamped at top and bottom, left and right onto
		// itself.
		out->rowOnPressUp = (char)((i > 0) ? (i - 1) : 0);
		out->rowOnPressDown = (char)((i < (box->rowCount - 1)) ? (i + 1) : (box->rowCount - 1));
		out->rowOnPressLeft = (char)i;
		out->rowOnPressRight = (char)i;
	}

	s_rows[slot][box->rowCount].stringIndex = RECTMENU_STRING_NONE;
}

internal void NativeMenuDecl_Commit(void)
{
	int i;

	// The strings first, because the rows point at them.
	memcpy(s_stringPool, s_stagingStrings, sizeof(s_stringPool));
	s_stringCount = s_stagingStringCount;

	for (i = 0; i < NATIVE_MENU_DECL_SLOTS; i++)
	{
		const struct NativeMenuDeclSlot *slot = &s_slots[i];
		struct RectMenu *box = slot->box;

		if (!s_staging[i].declared)
		{
			continue;
		}

		s_boxes[i] = s_staging[i];
		NativeMenuDecl_BuildRows(i);

		if (slot->generic)
		{
			// A pure format box: the declaration fields get a
			// known state. A zeroed slot would have stringIndexTitle 0,
			// and 0 is a valid language index - "no title" must be set
			// explicitly.
			box->stringIndexTitle = s_boxes[i].hasTitle ? s_boxes[i].titleText : (s16)RECTMENU_STRING_NONE;
			box->drawStyle = 0;
			box->funcPtr = NativeMenuDecl_Proc;

			// THE RUN-TIME BITS SURVIVE.
			//
			// At the FIRST load the slot is zeroed, there assigning and
			// adding are the same. At a RELOAD they are not: ONLY_DRAW_TITLE,
			// DRAW_NEXT_MENU_IN_HIERARCHY and NEEDS_TO_CLOSE say where the
			// player currently stands, and that does not belong to the file. Simply
			// overwriting them would mean pulling the cascade out from under his feet
			// while he types.
			{
				const u32 runtime =
				    box->state & (u32)(ONLY_DRAW_TITLE | DRAW_NEXT_MENU_IN_HIERARCHY | NEEDS_TO_CLOSE | RECTMENU_CLOSE_TRANSIENT);

				box->state = s_boxes[i].layoutBits | runtime;
			}

			box->posX_curr = (u16)s_boxes[i].x;
			box->posY_curr = (u16)s_boxes[i].y;
		}
		else
		{
			// A box that already belongs to someone. Only the bits the
			// file speaks about are set AND reset; everything
			// else stays untouched. On D230.menuMainMenu.state hang
			// EXECUTE_FUNCPTR and DISABLE_INPUT_ALLOW_FUNCPTRS, which the
			// title screen toggles in every frame - they stand in no mask.
			box->state = (box->state & ~s_boxes[i].layoutMask) | (s_boxes[i].layoutBits & s_boxes[i].layoutMask);

			if (s_boxes[i].hasTitle)
			{
				box->stringIndexTitle = s_boxes[i].titleText;
			}
		}

		if (s_boxes[i].hasPos)
		{
			if (slot->posInTitleAnchor)
			{
				D230.titleMainMenuPos.x = s_boxes[i].x;
				D230.titleMainMenuPos.y = s_boxes[i].y;
			}
			else
			{
				box->posX_curr = (u16)s_boxes[i].x;
				box->posY_curr = (u16)s_boxes[i].y;
			}
		}

		// THE ROWS ONLY IF THE FILE NAMES SOME.
		//
		// A box that already belongs to someone may be declared for its position, its
		// title and its minimum width without the file taking over
		// its rows. Rehanging them blindly would mean pulling away the
		// row fields into which MM_ToggleRows_PlayerCount and
		// MM_ToggleRows_Difficulty write their lock bit in every frame - the
		// locking would then silently no longer happen.
		//
		// A pure format box has no rows of its own, there the empty
		// declaration is a load error (see NativeMenuDecl_ParseBuffer).
		if (s_boxes[i].rowCount > 0)
		{
			box->rows = &s_rows[i][0];
		}
	}
}

// ---------------------------------------------------------------------------
//  Loading.
// ---------------------------------------------------------------------------

internal int NativeMenuDecl_ParseBuffer(const char *source, const char *text)
{
	struct NativeMenuDeclParse ps;
	char line[NATIVE_MENU_DECL_LINE];
	const char *p = text;

	memset(&ps, 0, sizeof(ps));
	ps.source = source;

	memset(s_staging, 0, sizeof(s_staging));
	memset(s_stagingStrings, 0, sizeof(s_stagingStrings));
	s_stagingStringCount = 0;
	s_styleCount = 0;

	while (*p != '\0')
	{
		int n = 0;

		while ((*p != '\0') && (*p != '\n') && (n < (NATIVE_MENU_DECL_LINE - 1)))
		{
			line[n++] = *p++;
		}

		// An overlong line must not silently fall apart into two.
		if ((*p != '\0') && (*p != '\n'))
		{
			ps.line++;
			NativeMenuDecl_Error(&ps, "line too long", source);

			while ((*p != '\0') && (*p != '\n'))
			{
				p++;
			}
		}
		else
		{
			ps.line++;
		}

		if (*p == '\n')
		{
			p++;
		}

		line[n] = '\0';
		NativeMenuDecl_ParseLine(&ps, line);
	}

	// A FORMAT BOX WITHOUT ROWS IS AN ERROR AND NOT AN EMPTY BOX.
	//
	// Since a declared box does not have to bring its rows along, there are
	// two meanings of "no zeile": for a box that already belongs to someone
	// it means "the rows stay with the code". For a pure
	// format box there is no code they could belong to - its slot
	// in the pool is zeroed, and menu->rows would point at nothing.
	{
		int i;

		for (i = 0; i < NATIVE_MENU_DECL_SLOTS; i++)
		{
			if (s_staging[i].declared && s_slots[i].generic && (s_staging[i].rowCount == 0))
			{
				ps.line = 0;
				NativeMenuDecl_Error(&ps, "format box without a zeile", s_slots[i].name);
			}
		}
	}

	return ps.errors;
}

// The file in one piece. NULL if it does not exist.
internal char *NativeMenuDecl_ReadFile(const char *path, long *sizeOut)
{
	FILE *file = fopen(path, "rb");
	char *buffer;
	long size;

	if (file == NULL)
	{
		return NULL;
	}

	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);

	if ((size < 0) || (size > (1 << 20)))
	{
		fclose(file);
		return NULL;
	}

	buffer = (char *)malloc((size_t)size + 1);

	if (buffer == NULL)
	{
		fclose(file);
		return NULL;
	}

	size = (long)fread(buffer, 1, (size_t)size, file);
	buffer[size] = '\0';
	fclose(file);

	if (sizeOut != NULL)
	{
		*sizeOut = size;
	}

	return buffer;
}

// FNV-1a over the file content. No cryptographic value - only the question "is this still
// the same file", and for that it is enough.
internal u32 NativeMenuDecl_Hash(const char *text, long size)
{
	u32 h = 2166136261u;
	long i;

	for (i = 0; i < size; i++)
	{
		h ^= (u32)(u8)text[i];
		h *= 16777619u;
	}

	return h;
}

// Was anything ever taken over at all? At the FIRST load the
// built-in stock is the fallback, at every later one it is the state that already stands -
// replacing that with the stock would be a change and not a fallback.
global_variable int s_haveCommitted;

void NativeMenuDecl_Load(void)
{
	long fileSize = 0;
	char *fileText = NativeMenuDecl_ReadFile(NATIVE_MENU_DECL_PATH, &fileSize);
	const char *source = NATIVE_MENU_DECL_PATH;
	const char *text = fileText;
	int errors;

	if (text != NULL)
	{
		s_fileHash = NativeMenuDecl_Hash(fileText, fileSize);
		s_fileHashValid = 1;
	}
	else
	{
		s_fileHashValid = 0;
		Platform_Log("[CTR Menu] '%s' is missing - the built-in set applies\n", NATIVE_MENU_DECL_PATH);
		source = "(built-in)";
		text = NATIVE_MENU_DEFAULT_TEXT;
	}

	s_lastError[0] = '\0';

	errors = NativeMenuDecl_ParseBuffer(source, text);

	if ((errors != 0) && (fileText != NULL))
	{
		if (s_haveCommitted)
		{
			// RELOAD WITH ERRORS: take nothing over. The state that stands on the
			// screen stays - that is exactly why there are the two phases.
			Platform_LogWarn("[CTR Menu] '%s': %d error(s), nothing taken - the previous state stands\n", NATIVE_MENU_DECL_PATH,
			                 errors);
			free(fileText);
			return;
		}

		// FIRST LOAD WITH ERRORS: there is no old state yet, so the
		// built-in stock. The game always starts.
		Platform_LogWarn("[CTR Menu] '%s': %d error(s), nothing taken - the built-in set applies\n", NATIVE_MENU_DECL_PATH,
		                 errors);
		source = "(built-in)";
		errors = NativeMenuDecl_ParseBuffer(source, NATIVE_MENU_DEFAULT_TEXT);
	}

	if (errors != 0)
	{
		// The built-in stock itself is broken. That is a build error and not
		// user input, so say it loudly and take nothing over.
		Platform_LogWarn("[CTR Menu] the built-in set has %d error(s) - the boxes in code stand\n", errors);
	}
	else
	{
		int boxes = 0;
		int rows = 0;
		int i;

		NativeMenuDecl_Commit();
		s_haveCommitted = 1;

		// The result is not written here: what a box REALLY
		// draws is only settled once it is drawn. The request
		// is remembered and redeemed at the next finished frame.
		//
		// Only with the menu reload on (g_cfg_menuReload). The file belongs
		// to the loop "change, see"; without it an ordinary run writes nothing
		// to the disk that it does not have to.
		s_reloadPending = (g_cfg_menuReload != 0);
		s_picturePending = (g_cfg_menuReload != 0);

		for (i = 0; i < NATIVE_MENU_DECL_SLOTS; i++)
		{
			if (s_boxes[i].declared)
			{
				boxes++;
				rows += s_boxes[i].rowCount;
			}
		}

		Platform_Log("[CTR Menu] %s: %d box(es), %d row(s), %d string(s), %d style(s)\n", source, boxes, rows, s_stringCount,
		             s_styleCount);

		// WHAT CAME OUT OF IT, ROW BY ROW.
		//
		// The navigation - up, down, left, right - only acts on input.
		// A picture comparison does not see it, not even a bit-identical one. So
		// the load path states it, otherwise it would be the only property of the
		// format that nobody can check.
		for (i = 0; i < NATIVE_MENU_DECL_SLOTS; i++)
		{
			int r;

			if (!s_boxes[i].declared)
			{
				continue;
			}

			// THE RUN-TIME STATE, THE SAME BEFORE AND AFTER THE RELOAD.
			//
			// rowSelected is the cursor, and the three bits say where in the
			// cascade the player stands. A reload must not touch them -
			// they are shown here so that you can check it instead of
			// believing it.
			Platform_Log("[CTR Menu]   %s row=%d state=%s%s%s declared=%s%s anchor=%d mask=%06x width=%d\n", s_slots[i].name,
			             (int)s_slots[i].box->rowSelected, ((s_slots[i].box->state & ONLY_DRAW_TITLE) != 0) ? " titleOnly" : "",
			             ((s_slots[i].box->state & DRAW_NEXT_MENU_IN_HIERARCHY) != 0) ? " child" : "",
			             ((s_slots[i].box->state & NEEDS_TO_CLOSE) != 0) ? " closing" : "",
			             ((s_slots[i].box->state & USE_SMALL_FONT) != 0) ? " small" : " big",
			             ((s_slots[i].box->state & CENTER_MENU_TEXT) != 0) ? " centre" : " left",
			             (int)s_boxes[i].anchor,
			             (unsigned)s_boxes[i].layoutMask, (int)s_boxes[i].minWidth);

			for (r = 0; r < s_boxes[i].rowCount; r++)
			{
				const struct MenuRow *built = &s_rows[i][r];

				Platform_Log("[CTR Menu]   %s[%d] text 0x%04x%s  up %d down %d left %d right %d\n", s_slots[i].name, r,
				             (unsigned)(built->stringIndex & MENU_ROW_LNG_MASK),
				             ((built->stringIndex & MENU_ROW_LOCKED) != 0) ? " locked" : "", (int)built->rowOnPressUp,
				             (int)built->rowOnPressDown, (int)built->rowOnPressLeft, (int)built->rowOnPressRight);
			}
		}
	}

	if (fileText != NULL)
	{
		free(fileText);
	}
}

// ---------------------------------------------------------------------------
//  At run time.
// ---------------------------------------------------------------------------

internal int NativeMenuDecl_SlotOfBox(const struct RectMenu *box)
{
	int i;

	for (i = 0; i < NATIVE_MENU_DECL_SLOTS; i++)
	{
		if (s_slots[i].box == box)
		{
			return i;
		}
	}

	return -1;
}

// THE DECLARED MINIMUM WIDTH OF THIS BOX, or -1.
int NativeMenuDecl_MinWidthChars(const struct RectMenu *box)
{
	const int slot = NativeMenuDecl_SlotOfBox(box);

	if ((slot < 0) || !s_boxes[slot].declared)
	{
		return -1;
	}

	return s_boxes[slot].minWidth;
}

// THE LARGEST DECLARED MINIMUM WIDTH AMONG THE CHILDREN, or -1.
//
// WHY THROUGH THE CHAIN AT ALL. RECTMENU_GetWidth walks down the cascade
// and returns ONE width; RECTMENU_ProcessState puts the
// minimum width of the style on top of it, and the style is that of the active box - so of the
// ROOT. A minimum width on a child would thereby be a key that
// silently does nothing.
//
// So the minimum width follows the same rule as the text width, which
// the drawer already treats that way: the chain is as wide as its
// widest member. The root SETS it, a child can only RAISE it.
//
// The walk takes exactly the path that RECTMENU_GetWidth takes - the same
// condition, so that no box counts that is not drawn at all.
int NativeMenuDecl_ChildMinWidthChars(const struct RectMenu *box)
{
	int best = -1;
	int guard;

	if (box == NULL)
	{
		return -1;
	}

	for (guard = 0; guard < NATIVE_MENU_DECL_BOXES; guard++)
	{
		int wert;

		if ((box->state & DRAW_NEXT_MENU_IN_HIERARCHY) == 0)
		{
			break;
		}

		box = (const struct RectMenu *)box->ptrNextBox_InHierarchy;

		if (box == NULL)
		{
			break;
		}

		wert = NativeMenuDecl_MinWidthChars(box);

		if (wert > best)
		{
			best = wert;
		}
	}

	return best;
}

// LAY THE DECLARED DEVIATIONS ONTO A FINISHED STYLE.
//
// This function used to return a whole style, and MM_NativeMenu_StyleFor
// passed it on blindly. With that the main menu lost the two fields in
// which it differs from retail - scale and minimum width -, as soon as
// it said "nimmt-stil": the lines that set them stood BEHIND the return
// and were never reached. Measured on a box with short content: 93 instead of
// 229 points wide.
//
// Now the caller says what it is laid onto, and here only what
// the file really named is laid on. 1 if something was laid on.
int NativeMenuDecl_OverlayStyle(struct RectMenuStyle *dst, const struct RectMenu *box)
{
	const int slot = NativeMenuDecl_SlotOfBox(box);
	const struct RectMenuStyle *src;
	u32 mask;
	int i;
	int getan = 0;

	if ((dst == NULL) || (slot < 0) || !s_boxes[slot].declared)
	{
		return 0;
	}

	if (s_boxes[slot].styleIndex < 0)
	{
		return 0;
	}

	src = &s_styles[s_boxes[slot].styleIndex];
	mask = s_styleMask[s_boxes[slot].styleIndex];

	for (i = 0; i < (int)len(s_styleFields); i++)
	{
		if ((mask & ((u32)1 << i)) == 0)
		{
			continue;
		}

		*(s16 *)((char *)dst + s_styleFields[i].offset) = *(const s16 *)((const char *)src + s_styleFields[i].offset);
		getan = 1;
	}

	return getan;
}

// A CHILD THAT FRESHLY OPENS MUST NOT BRING AN OLD CHAIN ALONG.
//
// Retail clears its boxes on re-entry into the title screen:
// MM_ResetAllMenus (MM_MenuFlow.c) clears ONLY_DRAW_TITLE and
// DRAW_NEXT_MENU_IN_HIERARCHY and sets ptrNextBox to 0. It reaches its boxes in
// two ways - through the list D230.arrayMenuPtrs (nine entries, D230.c)
// and through the CHAIN from every list entry.
//
// A REPLACED PAGE STANDS ON NEITHER OF THE TWO WAYS. It is not in the list,
// and the chain from D230.menuMainMenu ends at once, because replacing does
// not even set its ptrNextBox. So it keeps its chain from the last
// visit.
//
// WHAT THAT CAUSES (measured): on the SECOND entry into RACE MODE
// renn-modus, spieler-1p2p and schwierigkeit are all still collapsed. A
// collapsed box accepts no input (RECTMENU_ProcessInput checks
// ONLY_DRAW_TITLE twice, even), so NONE accepts it - the picture shows
// SINGLE RACE / 1P / EASY and no longer reacts to anything. The first visit
// runs through flawlessly, which is why the fault only appears "now and then".
// Cross-check with the retail menu and the same key sequence: the second
// visit gets as far as the track choice.
//
// So on opening we give the child the same fresh state that
// MM_ResetAllMenus would give it - and its old chain along with it.
// ptrPrevBox stays: the caller sets it right afterwards for the
// way back. The depth limit catches a ring that a half-cleared
// pointer could leave behind; eight is more than the canvas can ever carry.
internal void NativeMenuDecl_FreshChain(struct RectMenu *box)
{
	int depth;

	for (depth = 0; (box != NULL) && (depth < 8); depth++)
	{
		struct RectMenu *next = (struct RectMenu *)box->ptrNextBox_InHierarchy;

		box->state &= ~(ONLY_DRAW_TITLE | DRAW_NEXT_MENU_IN_HIERARCHY);
		box->ptrNextBox_InHierarchy = 0;

		box = next;
	}
}

// A child is opened: its rows are evaluated afresh, and a
// format box gets its declared state bits back.
internal void NativeMenuDecl_OpenChild(int slot)
{
	struct RectMenu *box = s_slots[slot].box;

	if (!s_boxes[slot].declared)
	{
		return;
	}

	NativeMenuDecl_BuildRows(slot);

	if (s_slots[slot].generic)
	{
		box->state = s_boxes[slot].layoutBits;
		box->rowSelected = 0;
	}
}

// THIS PAGE IS LEFT WITHOUT ANYONE GOING BACK.
//
// The marker from the "ersetzen" branch tells the way back that the parent box
// has to be put back in. Whoever leaves the page through a title screen exit
// - TIME TRIAL does that - never takes this way back, and the marker
// would stay set. It does no harm there, because the next
// opening sets it anew anyway; but a marker that stands longer than the
// thing it marks is a trap for the next reader.
void NativeMenuDecl_NoteAbandoned(const struct RectMenu *box)
{
	const int slot = NativeMenuDecl_SlotOfBox(box);

	if (slot >= 0)
	{
		s_openedReplaced[slot] = 0;
	}
}

int NativeMenuDecl_Confirm(struct RectMenu *box, int row)
{
	const int slot = NativeMenuDecl_SlotOfBox(box);
	const struct NativeMenuDeclRowSpec *spec;

	if ((slot < 0) || !s_boxes[slot].declared)
	{
		return 0;
	}

	if ((row < 0) || (row >= s_boxes[slot].rowCount))
	{
		return 0;
	}

	spec = &s_boxes[slot].rows[row];

	// The file says nothing about this row. Exactly like this ADVENTURE falls through into the
	// retail path, without its effect standing here a second time.
	if ((spec->action < 0) && (spec->target < 0))
	{
		return 0;
	}

	if (spec->action >= 0)
	{
		if (s_actions[spec->action].fn(box) == 0)
		{
			// The effect discarded the target. The row is answered
			// anyway - the caller should not go the retail path as well.
			return 1;
		}
	}

	if (spec->target >= 0)
	{
		struct RectMenu *child = s_slots[spec->target].box;

		NativeMenuDecl_FreshChain(child);
		NativeMenuDecl_OpenChild(spec->target);

		Platform_Log("[CTR Menu] '%s' row %d -> '%s' (%s)\n", s_slots[slot].name, row, s_slots[spec->target].name,
		             (spec->open == (s8)NATIVE_MENU_DECL_OPEN_REPLACE) ? "ersetzen" : "gestapelt");

		if (spec->open == (s8)NATIVE_MENU_DECL_OPEN_REPLACE)
		{
			// THE WAY BACK IS LAID HERE, NOT SEARCHED FOR LATER.
			//
			// In retail ptrPrevBox_InHierarchy is the field from which every proc
			// fetches its parent box. When stacking, RECTMENU writes it; when
			// replacing, nobody wrote it so far, and the way back ran into
			// nothing. The same pointer, the same meaning - only from here.
			child->ptrPrevBox_InHierarchy = box;
			s_openedReplaced[spec->target] = 1;

			// THE PAGE NEEDS ITS OWN FRAME TICK. A declared box
			// does not carry EXECUTE_FUNCPTR (mask 0x000003 in the load report), so its
			// proc only runs on a key press. As the ROOT of the chain it has to
			// carry the tick that the main menu carried so far - why
			// is explained at MM_NativeMode_PageTick.
			child->state |= EXECUTE_FUNCPTR;

			sdata->ptrDesiredMenu = child;
		}
		else
		{
			box->ptrNextBox_InHierarchy = child;
			box->state |= ONLY_DRAW_TITLE | DRAW_NEXT_MENU_IN_HIERARCHY;
			s_openedReplaced[spec->target] = 0;
		}
	}

	return 1;
}

void NativeMenuDecl_Proc(struct RectMenu *box)
{
	const s16 row = box->rowSelected;

	// THE FRAME TICK OF THE REPLACED PAGE, and only of it. RECTMENU sets funcState
	// to UPDATE before the frame tick (RECTMENU_ProcessState) and to INPUT before a
	// key press (RECTMENU_ProcessInput). A STACKED box never arrives here with UPDATE
	// - it is never ptrActiveMenu, and without EXECUTE_FUNCPTR it is not
	// ticked anyway.
	if (box->funcState == RECTMENU_FUNC_STATE_UPDATE)
	{
		MM_NativeMode_PageTick(box);
		return;
	}

	// THE WAY BACK, ONCE INSTEAD OF IN EVERY PROC.
	//
	// Thirty retail procs each write these four lines themselves.
	// A format box gets them from here.
	if (row == -1)
	{
		const int slot = NativeMenuDecl_SlotOfBox(box);
		struct RectMenu *parent = (struct RectMenu *)box->ptrPrevBox_InHierarchy;

		if (parent != NULL)
		{
			// Both cases clear the same two bits. For the REPLACED
			// box that is not cleaning up after a child but a
			// repair: MM_MenuProc_Main set ONLY_DRAW_TITLE BEFORE
			// the row was answered at all (MM_MenuFlow.c), and
			// nobody has taken it back since.
			parent->state &= ~(ONLY_DRAW_TITLE | DRAW_NEXT_MENU_IN_HIERARCHY);

			if ((slot >= 0) && s_openedReplaced[slot])
			{
				s_openedReplaced[slot] = 0;

				// The parent box becomes the active one again. Without this line
				// no menu is left after the way back.
				sdata->ptrDesiredMenu = parent;
			}
		}

		if ((slot >= 0) && s_boxes[slot].declared && (s_boxes[slot].backAction >= 0))
		{
			s_actions[s_boxes[slot].backAction].fn(box);
		}

		return;
	}

	NativeMenuDecl_Confirm(box, (int)row);
}

// ---------------------------------------------------------------------------
//  Reload at run time.
// ---------------------------------------------------------------------------

// Defined in game/native_menuscreen.c, which the unity build includes after the
// menu overlay.
void NativeMenuLock_Tick(void);

void NativeMenuDecl_Tick(void)
{
	long size = 0;
	char *text;
	u32 hash;
	const int now = Platform_GetVBlankCount();

	// The multiplayer lock hangs here, because this is the first place of every
	// menu frame, before the proc of the active box - and it always applies,
	// not only with the menu reload on.
	NativeMenuLock_Tick();

	if (!g_cfg_menuReload)
	{
		return;
	}

	if (now < s_nextPollVBlank)
	{
		return;
	}

	s_nextPollVBlank = now + NATIVE_MENU_DECL_POLL_VBLANKS;

	text = NativeMenuDecl_ReadFile(NATIVE_MENU_DECL_PATH, &size);

	if (text == NULL)
	{
		// The file is gone. That is no reason to throw away the state that
		// it left behind - only when it comes back is it read.
		s_fileHashValid = 0;
		return;
	}

	hash = NativeMenuDecl_Hash(text, size);
	free(text);

	if (s_fileHashValid && (hash == s_fileHash))
	{
		return;
	}

	Platform_Log("[CTR Menu] '%s' has changed - reloading\n", NATIVE_MENU_DECL_PATH);
	NativeMenuDecl_Load();
}

const char *NativeMenuDecl_LastError(void)
{
	return (s_lastError[0] != '\0') ? &s_lastError[0] : NULL;
}

// ---------------------------------------------------------------------------
//  What was really drawn.
// ---------------------------------------------------------------------------

// The reason below a grey box entry (native_menuscreen.c).
void NativeMenuReason_NoteDrawn(const struct RectMenu *box, const RECT *frame);

void NativeMenuDecl_NoteDrawn(const struct RectMenu *box, const RECT *frame)
{
	const int slot = NativeMenuDecl_SlotOfBox(box);

	NativeMenuReason_NoteDrawn(box, frame);

	if ((slot < 0) || !s_boxes[slot].declared || (frame == NULL))
	{
		return;
	}

	s_drawn[slot].valid = 1;
	s_drawn[slot].x0 = frame->x;
	s_drawn[slot].y0 = frame->y;
	s_drawn[slot].x1 = frame->x + frame->w;
	s_drawn[slot].y1 = frame->y + frame->h;
}

// Which box drew this rectangle? -1 if none.
//
// ONE finder for both questions - the name and the anchor. Two searches with
// the same rule would be the same statement in two places, and one would
// deviate from the other at the first tweak of the rule.
internal int NativeMenuDecl_SlotForDrawnBox(const int *authoredBox)
{
	int i;

	if (authoredBox == NULL)
	{
		return -1;
	}

	for (i = 0; i < NATIVE_MENU_DECL_SLOTS; i++)
	{
		if (!s_drawn[i].valid)
		{
			continue;
		}

		// DOES THE GROUP LIE IN WHAT THIS BOX DREW.
		//
		// Not equality: the group is the union of the primitives, the
		// frame is the rectangle around it, and the shadow hangs on the outside.
		// Being contained is the statement that holds - and because two
		// menu boxes of the cascade do not overlap, it is unambiguous.
		if ((authoredBox[0] >= (s_drawn[i].x0 - NATIVE_MENU_DECL_FIT)) &&
		    (authoredBox[1] >= (s_drawn[i].y0 - NATIVE_MENU_DECL_FIT)) &&
		    (authoredBox[2] <= (s_drawn[i].x1 + NATIVE_MENU_DECL_FIT)) &&
		    (authoredBox[3] <= (s_drawn[i].y1 + NATIVE_MENU_DECL_FIT)))
		{
			return i;
		}
	}

	return -1;
}

const char *NativeMenuDecl_NameForDrawnBox(const int *authoredBox)
{
	const int slot = NativeMenuDecl_SlotForDrawnBox(authoredBox);

	return (slot >= 0) ? s_slots[slot].name : NULL;
}

int NativeMenuDecl_AnchorForBox(const int *authoredBox)
{
	const int slot = NativeMenuDecl_SlotForDrawnBox(authoredBox);

	if ((slot < 0) || !s_boxes[slot].declared)
	{
		return -1;
	}

	return (int)s_boxes[slot].anchor;
}

void NativeMenuDecl_FrameConsumed(void)
{
	int i;

	for (i = 0; i < NATIVE_MENU_DECL_BOXES; i++)
	{
		s_drawn[i].valid = 0;
	}
}

// A request that is redeemed exactly once. The drawer queries it
// as soon as its decisions for the frame are settled.
int NativeMenuDecl_TakeResultRequest(void)
{
	const int pending = s_reloadPending;

	s_reloadPending = 0;

	return pending;
}

const char *NativeMenuDecl_ResultPath(void)
{
	return NATIVE_MENU_DECL_RESULT;
}

// THE PICTURE, IN THE CANVAS AND NOT IN THE INTERNAL RESOLUTION.
//
// The main target is as large as the internal resolution makes it - at factor
// four and 43:18 that is 3672 by 864. The editor needs it in the canvas
// in which the boxes lie (512, 682 or 918 by 216), otherwise the
// background does not match the rectangles. So it is sampled while writing.
//
// PPM (P6) and not BMP or PNG: Tkinter reads PPM without any additional
// dependency, and the format is so plain that channel order
// and row direction cannot silently go wrong - exactly the two faults that
// --shot had for years. The file is a measurement capture of this machine and,
// like the result file, is not under version control.
void NativeMenuDecl_PictureIfDue(void)
{
	int targetW = 0;
	int targetH = 0;
	int width = 0;
	int height = 0;
	int canvasW;
	int canvasH;
	int bytes;
	u8 *pixels;
	u8 *row;
	FILE *out;
	int y;

	if (!s_picturePending)
	{
		return;
	}

	s_picturePending = 0;

	NativeRenderer_GetMainTargetSize(&targetW, &targetH);
	canvasW = CTR_Canvas_ActiveWidth();
	canvasH = CTR_Canvas_ActiveHeight();
	bytes = targetW * targetH * 4;

	if ((bytes <= 0) || (canvasW <= 0) || (canvasH <= 0))
	{
		return;
	}

	pixels = (u8 *)malloc((size_t)bytes);
	row = (u8 *)malloc((size_t)canvasW * 3);

	if ((pixels == NULL) || (row == NULL))
	{
		free(pixels);
		free(row);
		Platform_LogWarn("[CTR Menu] picture: %d bytes refused\n", bytes);
		return;
	}

	if (!NativeRenderer_ReadMainTarget(pixels, bytes, &width, &height))
	{
		free(pixels);
		free(row);
		Platform_LogWarn("[CTR Menu] picture: nothing read, no file written\n");
		return;
	}

	out = fopen(NATIVE_MENU_DECL_PICTURE, "wb");

	if (out == NULL)
	{
		free(pixels);
		free(row);
		Platform_LogWarn("[CTR Menu] '%s' cannot be written\n", NATIVE_MENU_DECL_PICTURE);
		return;
	}

	fprintf(out, "P6\n%d %d\n255\n", canvasW, canvasH);

	for (y = 0; y < canvasH; y++)
	{
		// FLIPPED BACK. NativeRenderer_ReadMainTarget returns the bottom
		// picture row first; PPM wants the top one first.
		const int sy = (height - 1) - ((y * height) / canvasH);
		int x;

		for (x = 0; x < canvasW; x++)
		{
			const int sx = (x * width) / canvasW;
			const u8 *p = pixels + (((size_t)sy * (size_t)width + (size_t)sx) * 4);

			// The read-back delivers B, G, R, A.
			row[(x * 3) + 0] = p[2];
			row[(x * 3) + 1] = p[1];
			row[(x * 3) + 2] = p[0];
		}

		fwrite(row, 1, (size_t)canvasW * 3, out);
	}

	fclose(out);
	free(pixels);
	free(row);

	Platform_Log("[CTR Menu] '%s' written, %dx%d from %dx%d\n", NATIVE_MENU_DECL_PICTURE, canvasW, canvasH, width,
	             height);
}

#endif
