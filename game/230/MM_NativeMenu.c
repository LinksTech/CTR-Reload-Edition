#include <common.h>

#ifdef CTR_NATIVE

#include "platform/native_assets.h"

// THE MAIN MENU, REDIVIDED.
//
// The original puts the menu box to the right (0x180 of 512) and leaves the logo
// standing to its left: ring, CTR lettering, banner, Crash with trophy. Six
// rows - ADVENTURE, TIME TRIAL, ARCADE, VS., BATTLE, HIGH SCORE, and a
// seventh as soon as the scrapbook is unlocked.
//
// Here instead there is a grid of three columns, 25 / 50 / 25, and the
// menu holds the middle one. The logo goes away. Four rows remain:
// ADVENTURE, ARCADE, NITRO-PIT, QUIT.
//
// WHY A GRID AND NOT A NUMBER
//
// "Menu in the middle" would be a single constant, and the next question
// would be "and where is left". The three columns are written as numbers because the
// outer two are still empty and you cannot tell from a 256 that it is the
// middle of 128..383 and not the middle of something else.
//
// The reference space is 512 x 216 - the same area in which the declarations in
// game/native_uidecl.c lie and in which 224.c puts its centred rows at 0x100.
// The display converts the aspect ratio on top of that; here stands
// the intention, not the result.

// Whether the Nitro-Pit path was taken. Points the select screen at the
// containers from tracks/ instead of the tracks of the disc.
int g_mmNitroPit = 0;

// Which mode the list provides: NITRO RACE or CRYSTAL (2026-09-29).
// The same offer rule, a different mode (NativeTrack_WhyNotOffered).
static int s_mmListMode = NATIVE_TRACK_MODE_RACE;

void MM_NativeTracks_SetListMode(int mode)
{
	s_mmListMode = ((mode == NATIVE_TRACK_MODE_CRYSTAL) || (mode == NATIVE_TRACK_MODE_CTR)) ? mode : NATIVE_TRACK_MODE_RACE;
}

int MM_NativeTracks_ListMode(void)
{
	return s_mmListMode;
}

// THE GRID NOW LIVES IN THE DECLARATION.
//
// Column 0: 0..127, column 1: 128..383, column 2: 384..511, and the menu
// holds the middle one - x 256 is its middle. This derivation stood here as a
// chain of constants; since the file sets the position, it would stand there twice
// and the second one would have no effect. It is in menus/nitro-pit.menu.

// The donor slot.
//
// A container track is today not loaded as a level slot of its own
// but put in the place of an existing one; LOAD_VramFileCallback and
// the LEV reader reach through this ID. The debug menu lets you cycle the slot
// and stands on DINGO CANYON when nobody chooses anything else.
// Here it is not a row in the menu but a question to the container - and
// the default is the same.
#define MM_NATIVE_TRACK_DONOR DINGO_CANYON

// The second donor slot: for the CRYSTAL mode. Until 2026-09-29 it hung
// on the crystal bit in META; since then it follows the mode that is started
// (MM_NativeTracks_DonorFor).
//
// WHY A SECOND ONE AT ALL. The warp pad of the adventure hub lets exactly
// four level IDs into crystal mode - 18, 19, 21 and 23
// (AH_WarpPad.c:602). Whoever wants to land there later has to sit on one
// of these four; on DINGO CANYON (0) the question cannot even
// be asked. That is the whole purpose of this constant.
//
// WHY NITRO COURT. Of the four it is the only one with both index calculations
// at 0: D232.battleCrystalEventTime[levelID - 18] and the token table in
// 221.c. If one of the two calculations takes a wrong turn, it shows up
// most readily at this spot. And in the retail LEV it carries 20 STATIC_CRYSTAL
// (measured on ctr-u.bin, BIGFILE entry 146), just like RAMPAGE RUINS,
// SKULL ROCK and ROCKY ROAD - so a stock comparison is possible.
//
// WHAT THIS DOES NOT DO, and that belongs here and not in a promise:
// CRYSTAL_CHALLENGE is set in ONE place in the whole tree, in
// AH_WarpPad.c:612, and the Nitro-Pit path does not lead there - it is the
// arcade flow. A container on this slot drives an arcade race on
// an arena. The slot is the precondition for the mode, not the
// mode. Measured on 2026-09-01, not assumed.
#define MM_NATIVE_CRYSTAL_DONOR NITRO_COURT

// How many containers the select screen can hold. The reader in
// native_assets.c holds the same number; here it is clamped against NativeTrack_Count(),
// so that this is only an upper bound for the space.
//
// SIXTY-FOUR, AND WHY NOT MORE. An entry is about 2,400 bytes -
// path[1024], name[513], author[513], note[192], file[128] and the numbers -,
// so 77 KB at 32 and 155 KB at 64. In a process that holds double-digit
// megabytes that is no size. What speaks against 128 is not the memory but
// that nobody has that many: the folder holds 14 today. 64 is four times
// that and leaves room without paying for a case that does not exist.
//
// As long as the list could not scroll, this number was not the
// limit anyway - five were visible. Now it is.
#define MM_NATIVE_SELECT_MAX 64

// How many characters of a track name fit in a row of the select screen.
// The row is MM_NATIVE_ROW_W (216) wide, the text begins 8 px
// inside and should end 8 px before the right edge: 200 px, a character in
// FONT_BIG is 17 px, so 11. DecalFont cuts nothing off - a longer
// name would run over the box.
#define MM_NATIVE_SELECT_NAME_CHARS 11

// THE BOX ON THE RIGHT - dimensions and line length, in ONE place.
//
// For the disc the preview video with 176x75 stands there. A container has
// none, but text, and that now gets the space the video does not
// need. The derivation of the four numbers is at the assignment in
// MM_TrackSelect.c, because the neighbours it was measured against are there.
//
// 220 wide carry 208 px of text at 6 px indent on each side. FONT_SMALL is
// 13 px wide: 16 characters - exactly the length of the key, and that is why
// 220 is the lower bound for the box and not a choice.
#define MM_NATIVE_INFO_CHARS 16

// The same 208 px in FONT_BIG, which is 17 px wide: 12 characters.
#define MM_NATIVE_INFO_BIG_CHARS 12

// ROWS THAT DO NOT EXIST IN THE LANGUAGE FILE
//
// sdata->lngStrings comes from BI_LANGUAGEFILE, and the pointer array lies in the
// same buffer directly behind the strings - it cannot be
// extended without writing into foreign bytes. So a range of our own
// above all language indices (the highest is 0x24b), which RECTMENU intercepts before
// the access.
//
// The range must stay below 0x8000: struct MenuRow.stringIndex is s16 and
// 0x8000 is the lock bit.
//
// These strings are in one language. The language file has eight versions,
// these have one. That is a decision and not an oversight: a German
// version of "QUIT" would be guessed, and nothing is guessed here.
//
// SINCE STEP 1 THEY LIVE IN THE DECLARATION. Whoever renames a row
// changes that file and not this file here; the stock of indices belongs
// to the format (native_menudecl.c) and still lies in this range.
// The rows of the CHEATS box (native_menuscreen.c).
char *NativeMenuCheats_String(s16 index);

char *MM_NativeMenu_String(s16 index)
{
	char *cheat;

	// OUR OWN ROWS (native_menuscreen.c): race type box, NITRO-PIT,
	// main menu.
	//
	// Each is at most ten characters long, as long as TIME TRIAL and HIGH
	// SCORE. RECTMENU_GetWidth counts every row of every box of the chain
	// (RECTMENU.c:602-657), and the main menu stands in every chain. With eleven
	// characters every arcade box would get wider, including the one of SINGLE and CUP.
	switch (index)
	{
	case MM_NATIVE_LNG_NITRO_PIT:
		return "NITRO-PIT";
	// Open: RACE and CUP - the box is already called NITRO-PIT. Collapsed, only
	// the chosen row without title stands in the chain (RECTMENU.c:747, :781),
	// there NITRO RACE / NITRO CUP says where you are (2026-09-29). The question
	// is asked here, when drawing and when measuring the width, not when swapping the
	// rows: the box collapses and expands in the middle of a frame (in the procs), and
	// a text that only follows in the next frame would be wrong for one frame.
	case MM_NATIVE_LNG_NITRO_RACE:
		return ((D230.menuRaceType.state & ONLY_DRAW_TITLE) != 0) ? "NITRO RACE" : "RACE";
	case MM_NATIVE_LNG_NITRO_CUP:
		return ((D230.menuRaceType.state & ONLY_DRAW_TITLE) != 0) ? "NITRO CUP" : sdata->lngStrings[LNG_CUP];
	case MM_NATIVE_LNG_CRYSTAL:
		return "CRYSTAL";
	case MM_NATIVE_LNG_CTR:
		return "CTR";
	case MM_NATIVE_LNG_EXIT_GAME:
		return "EXIT GAME";
	case MM_NATIVE_LNG_CHEATS:
		return "CHEATS";
	case MM_NATIVE_LNG_GRAPHICS:
		return "GRAPHICS";
	default:
		break;
	}

	cheat = NativeMenuCheats_String(index);

	if (cheat != NULL)
	{
		return cheat;
	}

	return NativeMenuDecl_String(index);
}

// THE ROWS OF THE SELECT SCREEN
//
// The same structure that D230.arcadeTracks uses for the tracks of the disc,
// so that MM_TrackSelect does not have to know two kinds of rows:
//
//   levID        the run-time level ID of the track (65..99, assigned at the
//                scan; NativeTrack_LevelForIndex) - it becomes gGT->currLEV,
//                and only the funnel MainRaceTrack_RequestLoad turns it into
//                the donor slot (MM_NativeTracks_TranslateLevel below).
//                -1 if the track got no ID (more containers
//                than the band holds) - the row is then grey like a
//                broken one and cannot be confirmed.
//   mapTextureID -1, there is no map for a custom track
//   unlock       MM_TRACK_UNLOCK_ALWAYS, also for broken containers
//
// A container the reader rejected also stands in the list. Whoever puts down a
// file and finds it nowhere looks for the fault in the game. It
// is drawn grey and cannot be confirmed.
static struct MainMenu_LevelRow s_mmSelectRows[MM_NATIVE_SELECT_MAX];
static int s_mmSelectCount = 0;

// THE WINDOW OF THE LIST - which row stands at the top.
//
// Five rows are visible, there are up to MM_NATIVE_SELECT_MAX containers.
// This offset is the only number that separates the two: the draw loop
// starts at it, the cursor keeps running over ALL tracks.
static int s_mmSelectFirst = 0;

// Whether the select screen shows the containers and not the disc.
static int s_mmSelectScreen = 0;

// MAKE THE WINDOW FOLLOW THE CURSOR.
//
// Only as far as needed: if the cursor runs out at the top, it becomes the top
// row; if it runs out at the bottom, the bottom one. In between the window stands
// still. A window that keeps the cursor CENTRED scrolls already at the first
// step and lets the list slide away under the cursor - that is the
// movement perceived as jumping.
//
// At the end it is clamped, so that no empty rows stand below the last
// track.
void MM_NativeTracks_FollowCursor(int selected)
{
	const int count = MM_NativeTracks_Count();

	if (count <= MM_NATIVE_LIST_ROWS)
	{
		s_mmSelectFirst = 0;
		return;
	}

	if (selected < s_mmSelectFirst)
	{
		s_mmSelectFirst = selected;
	}

	if (selected > (s_mmSelectFirst + MM_NATIVE_LIST_ROWS - 1))
	{
		s_mmSelectFirst = selected - (MM_NATIVE_LIST_ROWS - 1);
	}

	if (s_mmSelectFirst > (count - MM_NATIVE_LIST_ROWS))
	{
		s_mmSelectFirst = count - MM_NATIVE_LIST_ROWS;
	}

	if (s_mmSelectFirst < 0)
	{
		s_mmSelectFirst = 0;
	}
}

int MM_NativeTracks_Window(void)
{
	return s_mmSelectFirst;
}

// HOW MANY ROWS THIS LIST SHOWS - at most five, with fewer containers
// as many as there are.
int MM_NativeTracks_WindowRows(void)
{
	const int count = MM_NativeTracks_Count();

	return (count < MM_NATIVE_LIST_ROWS) ? count : MM_NATIVE_LIST_ROWS;
}

// One buffer for the row being drawn. One is enough: DecalFont
// draws straight from the pointer, the row is finished before the next one
// is filled.
// AS LONG AS THE LONGEST CALLER, NOT AS THE FIRST.
//
// The buffer was MM_NATIVE_SELECT_NAME_CHARS + 1, so 16 bytes, and
// as long as the info panel took 13 characters that worked out. With 16 characters for the
// full key the terminating null byte writes to index 16 - one
// byte past the array. So it is built from both limits, and CopyTruncated
// clamps on its own in addition: a limit that only the caller keeps no longer holds
// for the next caller.
#define MM_NATIVE_LINE_MAX \
	((MM_NATIVE_SELECT_NAME_CHARS > MM_NATIVE_INFO_CHARS) ? MM_NATIVE_SELECT_NAME_CHARS : MM_NATIVE_INFO_CHARS)

static char s_mmLineBuffer[MM_NATIVE_LINE_MAX + 1];

// THE POSITION COUNTER, e.g. "4/14".
//
// WHY A NUMBER AND NO ARROWS. An arrow says THAT there is something more.
// The number also says how much and where you stand - with fourteen tracks
// that is the difference between "keep pressing" and "nine more". And it
// needs no place of its own: the title row is already there and occupied in the middle,
// on the right it stands empty.
//
// No snprintf: this translation unit pulls in no stdio, and setting two
// numbers below a hundred by hand is shorter than the reason not
// to.
static void MM_NativeTracks_CopyCounter(int oneBased, int total)
{
	int at = 0;

	if (oneBased >= 10)
	{
		s_mmLineBuffer[at++] = (char)('0' + ((oneBased / 10) % 10));
	}

	s_mmLineBuffer[at++] = (char)('0' + (oneBased % 10));
	s_mmLineBuffer[at++] = '/';

	if (total >= 10)
	{
		s_mmLineBuffer[at++] = (char)('0' + ((total / 10) % 10));
	}

	s_mmLineBuffer[at++] = (char)('0' + (total % 10));
	s_mmLineBuffer[at] = '\0';
}

// It is drawn on the right in the title row of the list box, in the
// small font - it is information about the list and not a row in
// it. With five or fewer containers it is not shown: then there is
// nothing you would not see.
void MM_NativeTracks_DrawCounter(RECT *listBox, int selected)
{
	const int count = MM_NativeTracks_Count();

	if (count <= MM_NATIVE_LIST_ROWS)
	{
		return;
	}

	MM_NativeTracks_CopyCounter(selected + 1, count);

	DecalFont_DrawLine(&s_mmLineBuffer[0], (s16)(listBox->x + listBox->w - MM_NATIVE_COUNTER_INSET_X),
	                   (s16)(listBox->y + MM_NATIVE_COUNTER_Y), FONT_SMALL, (JUSTIFY_RIGHT | GRAY));
}

static void MM_NativeTracks_CopyTruncated(const char *src, int maxChars)
{
	int c = 0;

	if (maxChars > MM_NATIVE_LINE_MAX)
	{
		maxChars = MM_NATIVE_LINE_MAX;
	}

	while ((c < maxChars) && (src != NULL) && (src[c] != '\0'))
	{
		s_mmLineBuffer[c] = src[c];
		c++;
	}

	s_mmLineBuffer[c] = '\0';
}

// THE SHORT FORM OF A TRACK NAME - ONE RULE, NO SPECIAL CASE.
//
// Eleven characters do not carry "Pizza Planet Crystal". Previously it was cut
// hard: "Arabian Heights" became "ARABIAN HEI", and the cut
// could not be seen - the name looked as if that was its name.
//
// THE RULE
//
//   1. If the name fits, it stays.
//   2. Otherwise the FIRST word is shortened to three letters plus a dot, then
//      the second, and so on. The LAST word always stays whole.
//   3. If that is not enough, the same round once more with ONE letter.
//   4. If that is not enough either, it is cut hard - and then there is a dot
//      at the end, so that the cut stays visible.
//
// WHY FROM THE FRONT AND NOT FROM THE BACK: the last word distinguishes the
// tracks from each other - CRYSTAL against HARD, A against B. The beginning is the
// common part and carries the least. "Inferno Island" thus becomes
// "INF. ISLAND" and not "INFERNO ISL".
#define MM_NATIVE_NAME_WORDS 8

static int MM_NativeTracks_LineLen(const int *wordLen, const int *keep, int wordCount)
{
	int total = (wordCount > 0) ? (wordCount - 1) : 0;
	int i;

	for (i = 0; i < wordCount; i++)
	{
		// A shortened word costs one dot more than its letters.
		total += keep[i] + ((keep[i] < wordLen[i]) ? 1 : 0);
	}

	return total;
}

static void MM_NativeTracks_Shorten(const char *src, char *dst, int maxChars)
{
	const char *wordAt[MM_NATIVE_NAME_WORDS];
	int wordLen[MM_NATIVE_NAME_WORDS];
	int keep[MM_NATIVE_NAME_WORDS];
	int wordCount = 0;
	int pass;
	int i;
	int at = 0;

	if (maxChars > MM_NATIVE_LINE_MAX)
	{
		maxChars = MM_NATIVE_LINE_MAX;
	}

	while ((*src != '\0') && (wordCount < MM_NATIVE_NAME_WORDS))
	{
		while (*src == ' ')
		{
			src++;
		}

		if (*src == '\0')
		{
			break;
		}

		wordAt[wordCount] = src;

		while ((*src != '\0') && (*src != ' '))
		{
			src++;
		}

		wordLen[wordCount] = (int)(src - wordAt[wordCount]);
		keep[wordCount] = wordLen[wordCount];
		wordCount++;
	}

	if (wordCount == 0)
	{
		dst[0] = '\0';
		return;
	}

	// Two rounds: first three letters per word, then one. The last word
	// stays untouched in both rounds.
	for (pass = 0; pass < 2; pass++)
	{
		const int letters = (pass == 0) ? 3 : 1;

		for (i = 0; i < (wordCount - 1); i++)
		{
			if (MM_NativeTracks_LineLen(wordLen, keep, wordCount) <= maxChars)
			{
				break;
			}

			if (keep[i] > letters)
			{
				keep[i] = letters;
			}
		}
	}

	for (i = 0; i < wordCount; i++)
	{
		int c;

		if ((i != 0) && (at < maxChars))
		{
			dst[at++] = ' ';
		}

		for (c = 0; (c < keep[i]) && (at < maxChars); c++)
		{
			dst[at++] = wordAt[i][c];
		}

		if ((keep[i] < wordLen[i]) && (at < maxChars))
		{
			dst[at++] = '.';
		}
	}

	// The hard cut, when even one letter per word is not enough. The
	// dot at the end is the only place where a name is longer than it
	// looks - and it says so.
	if (MM_NativeTracks_LineLen(wordLen, keep, wordCount) > maxChars)
	{
		dst[maxChars - 1] = '.';
		at = maxChars;
	}

	dst[at] = '\0';
}

// A ROW OF LABEL AND VALUE, clamped to the row length.
// "BY: " are four characters, so twelve remain for the name.
static void MM_NativeTracks_CopyLabelled(const char *label, const char *value)
{
	int at = 0;

	while ((label[at] != '\0') && (at < MM_NATIVE_INFO_CHARS))
	{
		s_mmLineBuffer[at] = label[at];
		at++;
	}

	while ((*value != '\0') && (at < MM_NATIVE_INFO_CHARS))
	{
		s_mmLineBuffer[at++] = *value++;
	}

	s_mmLineBuffer[at] = '\0';
}
int MM_NativeTracks_ScreenActive(void)
{
	return (s_mmSelectScreen != 0) && (s_mmSelectCount > 0);
}

struct MainMenu_LevelRow *MM_NativeTracks_Rows(void)
{
	return &s_mmSelectRows[0];
}

int MM_NativeTracks_Count(void)
{
	return s_mmSelectCount;
}

// The name for a row of the select screen, shortened to maxChars.
// The caller sets the character count, because it knows the row width.
//
// If the reader could not read META - a newer version, damage -,
// the FILE NAME stands there, without extension and with spaces instead of underscores
// (format 4.1). Until then the row was grey and empty, and nobody knew
// which file was meant.
char *MM_NativeTracks_RowName(int row, int maxChars)
{
	const struct NativeTrackEntry *entry = NativeTrack_Get(MM_NativeTracks_RowIndex(row));
	char fileName[128];
	const char *shown = "?";

	if ((entry != NULL) && (entry->name[0] != '\0'))
	{
		shown = entry->name;
	}
	else if (entry != NULL)
	{
		size_t length = strlen(entry->file);
		size_t k;

		// The extension without regard to upper/lower case - the scan takes it the same way.
		if (length > 9u)
		{
			static const char ending[] = ".rldtrack";
			size_t e;
			int same = 1;

			for (e = 0; e < 9u; e++)
			{
				char c = entry->file[length - 9u + e];

				if ((c >= 'A') && (c <= 'Z'))
				{
					c = (char)(c - 'A' + 'a');
				}
				if (c != ending[e])
				{
					same = 0;
				}
			}
			if (same)
			{
				length -= 9u;
			}
		}
		if (length >= sizeof(fileName))
		{
			length = sizeof(fileName) - 1u;
		}

		for (k = 0; k < length; k++)
		{
			fileName[k] = (entry->file[k] == '_') ? ' ' : entry->file[k];
		}
		fileName[length] = '\0';
		shown = fileName;
	}

	MM_NativeTracks_Shorten(shown, &s_mmLineBuffer[0], maxChars);

	return &s_mmLineBuffer[0];
}

// Choosable means: the row offers the mode of the list - valid, with a
// level ID, and the mode declared (offer rule, NativeTrack_WhyNotOffered).
// Otherwise grey, and the wheel shows the reason (MM_NativeTrackSelect.c).
int MM_NativeTracks_RowOk(int row)
{
	return NativeTrack_WhyNotOffered(MM_NativeTracks_RowIndex(row), s_mmListMode, NULL) == NULL;
}

// The box on the right, where the preview image and the map stand for the tracks
// of the disc. A custom track has neither: the preview video lies
// in a BIGFILE entry of its own, the map as an icon pair in the icon set. Neither
// exists for a container, and putting the picture of a foreign track there
// would be a claim.
//
// Instead there stands what the container really brings along: name, author,
// key - and whether the reader rejected it.
// An unsigned number as text. No snprintf: this tree draws
// menu text from its own buffers, and a second buffer next to s_mmLineBuffer
// would be a second place where a row can become too long.
// WHO BUILT THE TRACK - and what to do if nobody said.
//
// In eight of the fourteen containers in tracks/ the author is literally
// "unbekannt" (Arcade_Crystal, Arcade_Hard, Dinoco_Hard, Ice_Rink,
// Pizza_Planet_Crystal, Pizza_Planet_Hard, Rink_Hard, Sids_House). That is
// DATA and not code - written down at packing, not generated by rldpack
// -, but since today it stands large in the menu, and there every
// visible text is English.
//
// That is why here and not deeper: the string in the container stays as
// it is. The right fix is repacking the eight with --author; until
// then an empty field and an "unbekannt" mean the same, namely
// UNKNOWN.
static const char *MM_NativeTracks_Author(const char *author)
{
	local_persist const char placeholder[] = "unbekannt";
	int at;

	if (author[0] == '\0')
	{
		return "UNKNOWN";
	}

	for (at = 0; placeholder[at] != '\0'; at++)
	{
		const char c = author[at];
		const char lower = ((c >= 'A') && (c <= 'Z')) ? (char)(c + ('a' - 'A')) : c;

		if (lower != placeholder[at])
		{
			return author;
		}
	}

	return (author[at] == '\0') ? "UNKNOWN" : author;
}

static const char *MM_NativeTracks_Unsigned(u32 value)
{
	local_persist char digits[12];
	int at = sizeof(digits) - 1;

	digits[at] = '\0';

	do
	{
		digits[--at] = (char)('0' + (value % 10u));
		value /= 10u;
	} while ((value != 0u) && (at > 0));

	return &digits[at];
}

// NO PREVIEW BOX. The place top right stays free.
//
// The path there is measured and in place: NativeSTR_UploadNextFrame
// (platform/native_str.c:803) is in the end LoadImage(&rect, rgb555) into the
// VRAM page of icon 0x3f plus NativeRenderer_UpdateVRAM(), and the page
// is drawn as a 16-bit quad (MM_TrackSelect_Video_DrawNativePreview).
// Only the images are missing. An empty frame with "NO PREVIEW IMAGE" in it
// tells every player so again at every glance - so nothing
// stands there.

// THE WHOLE RIGHT COLUMN, in ONE place.
//
// The retail screen puts three things there one below the other: the heading
// SELECT / LEVEL, the video window and the track map in six layers. For
// a custom track none of these applies - there is no video in the BIGFILE
// and no map icon in the icon set (mapTextureID is -1, MM_NativeMenu.c:706).
//
// That is why THIS function is the copy and not the screen: list,
// input, fade-in and lap choice stay ONE code path that arcade and
// adventure use unchanged. Only the right column forks, and it
// forks in a single place.
void MM_NativeTracks_DrawRightColumn(int slideX, int slideY, int index, int lapBoxClosed)
{
	RECT info;

	// THE LAP CHOICE GETS THE SPACE. The retail screen hangs title and
	// map on the same condition; it clears the right column before the
	// lap box comes there.
	if (!lapBoxClosed)
	{
		return;
	}

	// WITH THE FADE-IN, NOT NEXT TO IT. Until 2026-09-17 the fixed
	// position without the offset stood here - so the info box jumped into the picture while
	// the list slid in.
	info.x = (s16)(slideX + MM_NATIVE_INFO_X);
	info.y = (s16)(slideY + MM_NATIVE_INFO_Y);
	info.w = MM_NATIVE_INFO_W;
	info.h = MM_NATIVE_INFO_H;

	MM_NativeTracks_DrawInfo(&info, index);
}

void MM_NativeTracks_DrawInfo(RECT *r, int index)
{
	const struct NativeTrackEntry *entry = NativeTrack_Get(index);
	const s16 textX = (s16)(r->x + 6);

	// Two rows on the size line: the first 46 points below the
	// box edge, the second 24 below that - the same numbers as in the menu.
	const s16 row0 = (s16)(r->y + MM_NATIVE_TITLE_BLOCK);
	const s16 row1 = (s16)(row0 + MM_NATIVE_ROW_PITCH);

	// SAY ONCE THAT IT WAS HERE.
	//
	// "No preview image" has two quite different causes: either this box
	// is not drawn at all, or it is drawn and is empty.
	// From the outside both look the same, and the element table of --ui-watch
	// cannot tell them apart - letters inside the box do not change its
	// bounds.
	//
	// So the drawing says it itself, a single time per run.
	{
		local_persist int said = 0;

		// IN_MENU and not at the first call: the first call falls into the
		// first frame of the fade-in, and there the box stands at y -108,
		// so above the top edge of the picture. Measured on 2026-08-29 - the probe
		// thereby proved THAT drawing happens, but not WHERE it lies
		// when it stands.
		if (!said && (D230.trackSelect.transition.state == IN_MENU))
		{
			said = 1;
			Platform_Log("[CTR Menu] preview box at %d,%d %dx%d for '%s' by '%s'\n", (int)r->x, (int)r->y, (int)r->w, (int)r->h,
			             (entry != NULL) ? entry->name : "(null)", ((entry != NULL) && (entry->author[0] != '\0')) ? entry->author : "(none)");
		}
	}

	if (entry == NULL)
	{
		RECTMENU_DrawTitledBox(r, "TRACK INFO", 1);
		return;
	}

	// ROW 1 - WHO BUILT THE TRACK.
	//
	// Here stood a branch: if the signature did not check out, "UNVERIFIED" stood in grey
	// instead of the name - nobody vouches for it. The signature was
	// taken out of the format on 2026-09-19, so the branch is gone.
	//
	// WHAT THAT MEANS, and it belongs here and not in a footnote: the
	// name in this row is a CLAIM from META. It was not proven before
	// either - the signature proved who PACKED it, not who BUILT it.
	// Now the row says exactly what it can.
	MM_NativeTracks_CopyLabelled("MAKER: ", MM_NativeTracks_Author(entry->author));
	DecalFont_DrawLine(&s_mmLineBuffer[0], textX, row0, FONT_SMALL, ORANGE);

	// ROW 2 - WHICH VERSION. trackVersion is the author's count from
	// META, not that of the format (that is metaVersion) - whoever downloads a track
	// again wants to know whether he has the newer one.
	//
	// MODES stood here until 2026-09-17 and gave way to these two fields.
	// The box carries two rows; a third would mean 128 instead of 104 high,
	// and then only 40 would remain for the preview above it.
	MM_NativeTracks_CopyLabelled("VERSION: ", MM_NativeTracks_Unsigned(entry->trackVersion));
	DecalFont_DrawLine(&s_mmLineBuffer[0], textX, row1, FONT_SMALL, GRAY);

	// THE BOX LAST, AND NECESSARILY SO.
	//
	// The ordering table draws what is entered later FURTHER BACK.
	// A box of type 1 is opaque - entered before the rows it covers
	// them completely. Measured: the box stood there and was empty. The
	// list box does not fall for this, because type 0 is translucent.
	//
	// Title and title line are drawn by RECTMENU_DrawTitledBox itself before the
	// box, so they stay visible.
	RECTMENU_DrawTitledBox(r, "TRACK INFO", 1);
}

// Which slot for this track - the ONLY place where the choice is made.
//
// Since the run-time IDs the row of the select screen no longer carries
// the slot but the ID of the track; only the loading asks for the slot
// (MM_NativeTracks_LoadRow), and passes it to NativeTrack_Load. So
// the old scissors - row and loading name different slots, and
// NativeTrack_ArmSubfiles silently does nothing - can no longer open at all.
//
// A DISC TRACK NEVER PASSES HERE. It is called only from the
// Nitro-Pit path, and that begins at a container from tracks/. Whoever chooses ARCADE
// or ADVENTURE runs past this file.
//
// THE SLOT FOLLOWS THE MODE, NOT THE CRYSTAL BIT (2026-09-29).
// A container stands in the list of every mode it declares - the same
// container can therefore start as Race, CTR or Crystal. When the
// crystal bit in META decided here, a Race+Crystal container sat on NITRO COURT (18)
// for RACE and CTR too; there a race starts like an arena (traffic light and
// sound state 10 instead of the intro, MainMain.c:188-196), and every retail
// fallback per level comes from the slot. Now: CRYSTAL -> NITRO COURT, RACE and
// CTR -> DINGO CANYON. Cups are Race.
//
// WHAT DOES NOT CHANGE. The CRYSTAL list only lets containers with the bit
// in (NativeTrack_WhyNotOffered), so a pure crystal track sits
// on 18 as before; a track without the bit on 0 as before. Only a
// container with both modes changes the slot in RACE and CTR.
static int MM_NativeTracks_DonorFor(int mode)
{
	return (mode == NATIVE_TRACK_MODE_CRYSTAL) ? MM_NATIVE_CRYSTAL_DONOR : MM_NATIVE_TRACK_DONOR;
}

// Loads the container for the chosen row onto the donor slot.
//
// Checked a SECOND time, against the file as it is now: between the
// listing and the loading any number of minutes lie. See NativeTrack_Load;
// the debug menu does the same at its spot.
int MM_NativeTracks_LoadRow(int index)
{
	return MM_NativeTracks_LoadRowFor(index, NATIVE_TRACK_MODE_RACE);
}

// The same for a mode: the CRYSTAL list loads by the rule for
// Crystal, every other caller (cup, funnel, --autoload-track) by Race.
// The mode also chooses the slot (MM_NativeTracks_DonorFor).
int MM_NativeTracks_LoadRowFor(int index, int mode)
{
	const struct NativeTrackEntry *entry = NativeTrack_Get(index);
	const char *whyNot = NativeTrack_WhyNotOffered(index, mode, NULL);

	// The offer rule here too, for every path that comes past here: the
	// wheel, the cup, the funnel, --autoload-track, --level 65..99.
	if (whyNot != NULL)
	{
		Platform_LogWarn("[CTR Menu] track %d%s%s%s is not offered - %s\n", index, (entry != NULL) ? " '" : "", (entry != NULL) ? entry->file : "",
		                 (entry != NULL) ? "'" : "", whyNot);
		return 0;
	}

	const int donor = MM_NativeTracks_DonorFor(mode);

	if (!NativeTrack_Load(index, donor))
	{
		Platform_LogWarn("[CTR Menu] '%s' did not load\n", entry->file);
		return 0;
	}

	// The line says the slot and the reason - and says explicitly that a
	// slot is not a mode. Whoever finds it later in a log should not be able to
	// read from it that something was switched on here.
	//
	// The lines for CRYSTAL and CTR read word for word as before 2026-09-29
	// (the CRYSTAL list only lets containers with the crystal bit in, so the
	// sentence stays true); the branch "Race on the crystal slot" is gone,
	// because it no longer exists.
	Platform_Log("[CTR Menu] NITRO-PIT: '%s' by %s -> donor slot %d, %s\n", entry->name,
	             (entry->author[0] != '\0') ? entry->author : "(not set)", donor,
	             (mode == NATIVE_TRACK_MODE_CRYSTAL)
	                 ? "picked for the crystal bit in META - CRYSTAL list, the Crystal Challenge is switched on at the start"
	                 : ((mode == NATIVE_TRACK_MODE_CTR) ? "CTR list, the CTR Challenge is switched on at the start" : "the standing seat"));
	return 1;
}

// THE TRANSLATION AT THE FUNNEL - run-time ID to donor slot, in ONE place.
//
// The only caller is MainRaceTrack_RequestLoad, the funnel through which EVERY
// level change goes. Everything below - Lev_ID_To_Load, LOAD_LevelFile,
// gGT->levelID, every retail table - still sees only slots from the
// retail band; a 65..99 never arrives there, and that is why
// no table needs a switch either.
//
// The NORMAL path carries nothing more here: the select screen has already
// loaded the container at the confirmation ([K6] in
// MM_NativeTrackSelect.c), so LoadedIndex is the requested index, and
// the slot the loading chose is returned. The load branch here
// catches every OTHER caller that requests a run-time ID (today
// --autoload-track, later the custom cup) - for that one the funnel is then
// the same path as for the menu.
//
// IF NOTHING STANDS BEHIND THE ID - a remembered ID after a restart,
// a typo of a later caller -, the run falls back LOUDLY to the
// bare donor slot: retail DINGO CANYON, without a container. That is
// visibly wrong instead of silently off, and it indexes no table
// outside its bounds. Release first, so that a leftover
// container of a DIFFERENT run does not jump in on the same slot.
//
// THE SWITCH BETWEEN TWO RACES (custom cup, 2026-09-28). The cup
// requests the next race from the standings (UI_CupStandings.c), and
// there the level of the RUNNING container is still going: after RequestLoad
// the finish flag first slides in, and below it drawing continues until it is
// fully in the picture (MainMain.c, LOAD_REQUESTED: "keep rendering the scene").
// A swap here would give the level that is still visible the numbers of the
// next one - sky and draw memory, intersection buffer, track mods, sound.
// So if a container is currently running, the funnel here only decides the
// slot, and the swap happens in MM_NativeTracks_StartLoad, when the loader
// begins and only the flag is drawn any more (LOAD_LevelFile). Every
// path that came past here so far loads from the menu - there no
// container is running, and for it nothing changes.
#define MM_NATIVE_PENDING_NONE (-1)
global_variable int s_mmPendingIndex = MM_NATIVE_PENDING_NONE;

internal int MM_NativeTracks_ContainerIsLive(void)
{
	return (NativeTrack_LoadedIndex() >= 0) && NativeTrack_ActiveForLevel(sdata->gGT->levelID);
}

s16 MM_NativeTracks_TranslateLevel(s16 levelID)
{
	const int band = (int)levelID - NATIVE_TRACK_LEVELID_FIRST;
	int index;

	// Every new request replaces a reserved one that did not live to see
	// its load.
	s_mmPendingIndex = MM_NATIVE_PENDING_NONE;

	if ((band < 0) || (band >= NATIVE_TRACK_LEVELID_COUNT))
	{
		return levelID;
	}

	index = NativeTrack_IndexForLevel((int)levelID);
	if (index < 0)
	{
		NativeTrack_Release();
		Platform_LogWarn("[CTR Tracks] level id %d has no container behind it - falling back to the bare donor seat %d\n", (int)levelID,
		                 MM_NATIVE_TRACK_DONOR);
		return MM_NATIVE_TRACK_DONOR;
	}

	if ((NativeTrack_LoadedIndex() != index) && MM_NativeTracks_ContainerIsLive())
	{
		const struct NativeTrackEntry *entry = NativeTrack_Get(index);

		// The cup is Race, and MM_NativeTracks_StartLoad loads with
		// MM_NativeTracks_LoadRow (Race) - the same question, the same slot, otherwise
		// the slot comparison there would trip.
		const int seat = MM_NativeTracks_DonorFor(NATIVE_TRACK_MODE_RACE);

		s_mmPendingIndex = index;

		Platform_Log("[CTR Tracks] level id %d -> seat %d ('%s') - swapped in when the loader starts, '%s' is still on screen\n", (int)levelID, seat,
		             entry->name, NativeTrack_LoadedName());
		return (s16)seat;
	}

	if (NativeTrack_LoadedIndex() != index)
	{
		if (!MM_NativeTracks_LoadRow(index))
		{
			Platform_LogWarn("[CTR Tracks] level id %d: the container did not load - falling back to the bare donor seat %d\n", (int)levelID,
			                 MM_NATIVE_TRACK_DONOR);
			return MM_NATIVE_TRACK_DONOR;
		}
	}

	{
		const int seat = NativeTrack_LoadedDonorLevel();

		Platform_Log("[CTR Tracks] level id %d -> seat %d ('%s')\n", (int)levelID, seat, NativeTrack_LoadedName());

		return (s16)seat;
	}
}

// The second part of the switch between two races (see above): from
// MainRaceTrack_StartLoad, when the flag is fully in the picture and before
// LOAD_LevelFile starts the loader. Without a reservation it does nothing.
//
// If the load goes wrong here, the slot is already assigned - then it runs
// bare, retail track without a container, and the log says so loudly. That is
// the same answer as above in the funnel.
void MM_NativeTracks_StartLoad(s16 levelID)
{
	const int index = s_mmPendingIndex;

	s_mmPendingIndex = MM_NATIVE_PENDING_NONE;

	if (index < 0)
	{
		return;
	}

	if (!MM_NativeTracks_LoadRow(index))
	{
		Platform_LogWarn("[CTR Tracks] the container for seat %d did not load as the loader started - the seat loads bare\n", (int)levelID);
		return;
	}

	if (NativeTrack_LoadedDonorLevel() != (int)levelID)
	{
		Platform_LogWarn("[CTR Tracks] '%s' took seat %d, the loader asks for %d - the seat loads bare\n", NativeTrack_LoadedName(),
		                 NativeTrack_LoadedDonorLevel(), (int)levelID);
		return;
	}

	Platform_Log("[CTR Tracks] '%s' swapped in on seat %d as the loader starts\n", NativeTrack_LoadedName(), (int)levelID);
}

// The name of the track that REALLY runs on this level. Retail takes
// data.metaDataLEV[levelID].name_LNG - for a container levelID is the
// donor slot, and there DINGO CANYON would stand (2026-09-28: the container name in both
// places). Between two cup races, until the swap, that is
// the track that is still visible - the same question as when loading.
//
// Shortened like the rows of the wheel, to MM_NATIVE_LINE_MAX: in FONT_BIG that is
// at most 272 of 512 columns, as wide as the longest names of the
// disc. A buffer of its own, because the title at race start draws the cup name and
// the track name in the same frame.
global_variable char s_mmTitleName[MM_NATIVE_LINE_MAX + 1];

const char *MM_NativeTracks_NameForLevel(int levelID)
{
	if ((NativeTrack_LoadedIndex() < 0) || !NativeTrack_ActiveForLevel(levelID))
	{
		return NULL;
	}

	MM_NativeTracks_Shorten(NativeTrack_LoadedName(), &s_mmTitleName[0], MM_NATIVE_LINE_MAX);
	return &s_mmTitleName[0];
}

// Disarming also means: throwing the container out of memory.
//
// NativeTrack_ActiveForLevel only asks whether A container is loaded and whether
// its donor ID matches the level. If it stays behind after a Nitro-Pit race,
// the next race keeps driving the custom track on ITS donor slot
// - and nobody chose anything wrong. Since there are two slots,
// that is no longer only DINGO CANYON but the one the container got;
// the cleanup is unaffected by that, because it releases the container and
// not a slot.
//
// This is called only at the title screen: on every confirmed row of the
// main menu and on every return there. During a race nothing comes
// past here.
void MM_NativeTracks_Disarm(void)
{
	s_mmSelectScreen = 0;
	g_mmNitroPit = 0;
	s_mmPendingIndex = MM_NATIVE_PENDING_NONE;

	// A custom cup ends at the title screen: after the podium, or with QUIT from
	// the pause.
	MM_NativeCup_Stop("back at the title");

	if (NativeTrack_LoadedIndex() >= 0)
	{
		Platform_Log("[CTR Menu] container '%s' released - back at the title\n", NativeTrack_LoadedName());
		NativeTrack_Release();
	}
}

// Which container stands on which row. Since the evening of 2026-09-29
// a row is no longer the scan index: containers without Race do not stand in the
// RACE list (NativeTrack_HiddenFromRace). The level IDs, track-ids.tsv, the
// cups and --autoload-track keep calculating with the scan index; only the wheel
// counts rows.
static s16 s_mmSelectScan[MM_NATIVE_SELECT_MAX];

int MM_NativeTracks_RowIndex(int row)
{
	if ((row < 0) || (row >= s_mmSelectCount))
	{
		return -1;
	}

	return s_mmSelectScan[row];
}

static void MM_NativeTracks_BuildSelectRows(void)
{
	const int count = NativeTrack_Count();
	int scan;
	int i = 0;

	for (scan = 0; (scan < count) && (i < MM_NATIVE_SELECT_MAX); scan++)
	{
		if (NativeTrack_HiddenFrom(scan, s_mmListMode))
		{
			continue;
		}

		s_mmSelectScan[i] = (s16)scan;

		// The run-time ID of the track, no longer the donor slot. Which
		// slot it gets is still decided by MM_NativeTracks_DonorFor - but
		// only at the funnel, when it is really loaded.
		s_mmSelectRows[i].levID = (s16)NativeTrack_LevelForIndex(scan);
		s_mmSelectRows[i].videoThumbnail = 0;
		s_mmSelectRows[i].mapTextureID = -1;
		s_mmSelectRows[i].unlock = MM_TRACK_UNLOCK_ALWAYS;
		s_mmSelectRows[i].previewVideoFileIndex = 0;
		s_mmSelectRows[i].previewVideoFrameCount = 0;
		i++;
	}

	s_mmSelectCount = i;
}

// For the track screen of ARCADE -> CUSTOM (MM_NativeTrackSelect.c).
// The rows are rebuilt at every entry. The folder is not re-read
// in doing so; the list from startup applies.
int MM_NativeTracks_BuildRows(void)
{
	MM_NativeTracks_BuildSelectRows();

	return s_mmSelectCount;
}

// ===========================================================================
//  THE VERBS OF THE NITRO-PIT PATH.
// ===========================================================================
//
//  They stand here and not in native_menudecl.c, because their data
//  stands here - s_mmSelectScreen, the containers, the donor slot. There only
//  the names stand under which the file calls them.
//
//  NONE OF THEM NAVIGATES. Where it goes on is said by "weiter" in the
//  file. If the target were here, the file could not move it, and
//  that is exactly what it exists for.

// Is there a container in tracks/?
int MM_NativeTracks_CondPresent(struct RectMenu *box)
{
	(void)box;

	return NativeTrack_Count() > 0;
}

// ALWAYS TRUE - and that is the whole purpose.
//
// A row behind which nothing lies yet should look like one that
// can do something later: it stands there, it is grey, it accepts no input.
// The format knows 'gesperrt-wenn <condition>' for that; what was missing was
// a condition that asks no question.
//
// Without it you would have to take the row out of the file - and then the
// next reader no longer knows that there is supposed to be OPTIONS.
int MM_NativeMenu_CondAlways(struct RectMenu *box)
{
	(void)box;

	return 1;
}

// NITRO-PIT: the arcade flow with a different track choice.
int MM_NativeTracks_ActNitroPit(struct RectMenu *box)
{
	struct GameTracker *gGT = sdata->gGT;

	(void)box;

	// DONT change, should only work in Arcade, and VS
	if ((gGT->gameMode2 & CHEAT_ONELAP) != 0)
	{
		gGT->numLaps = MM_ONE_LAP_CHEAT_COUNT;
	}

	gGT->gameMode1 |= ARCADE_MODE;

	g_mmNitroPit = 1;
	s_mmSelectScreen = 0;

	Platform_Log("[CTR Menu] NITRO-PIT: %d container(s) in tracks/\n", NativeTrack_Count());

	return 1;
}

// CUSTOM TRACK: from here the arcade path, only with the list from tracks/.
int MM_NativeTracks_ActOpenSelect(struct RectMenu *box)
{
	(void)box;

	MM_NativeTracks_BuildSelectRows();

	// The list begins at the top when the screen opens. A window that
	// stays from last time would be a state nobody has set.
	s_mmSelectFirst = 0;

	if (s_mmSelectCount <= 0)
	{
		// The lock of the row should already have caught this. The 0 tells the
		// format that the target of this row should NOT be taken -
		// otherwise the player would stand in front of an empty list.
		return 0;
	}

	s_mmSelectScreen = 1;

	// The select screen starts at the first track and not at what
	// was last chosen on the disc.
	sdata->trackSelBackup = 0;

	// Row by row as in MM_MenuProc_SingleCup: player count, then
	// difficulty, then driver select, then the select screen.
	sdata->gGT->gameMode2 &= ~(CUP_ANY_KIND);

	D230.characterSelectTransitionState = IN_MENU;

	return 1;
}

// The way back out of the box. That the parent box shows its rows again
// is done by the generic proc - here stands only what applies in addition.
int MM_NativeTracks_ActCloseSelect(struct RectMenu *box)
{
	(void)box;

	s_mmSelectScreen = 0;

	return 1;
}


// ===========================================================================
//  RULE FOR EVERY PAGE THAT OPENS WITH "oeffnen ersetzen".
//
//  It is the ROOT of the chain. With that D230.menuMainMenu drops out, and
//  with it the frame tick the whole menu system drew from it:
//  RECTMENU_ProcessState calls funcPtr only for sdata->ptrActiveMenu
//  (RECTMENU.c:1046), so only for the root. In retail that is always the
//  main menu box - even three levels deep -, which is why there MM_MenuProc_Main
//  runs in every frame and with it MM_Title_MenuUpdate (MM_MenuFlow.c:70).
//  That is the ONLY place that reads titleMenuState and desiredMenuIndex.
//
//  CONSEQUENCE IF THE PAGE DOES NOT CARRY THE TICK ITSELF: every exit of the
//  whole chain below it stays stuck - including those that stand in retail procs
//  and have nothing to do with our verbs.
//
//  MEASURED, NOT PRESUMED (2026-09-17, --menu-keys):
//
//    SINGLE RACE - 1P - EASY   MM_MenuProc_Difficulty (MM_MenuFlow.c:464)
//    VS - SINGLE - 2P          MM_MenuProc_2p3p4p     (MM_MenuFlow.c:378)
//
//  Both set EXITING and CHARACTER_SELECT, both pictures stood still.
//  Cross-check with --stock-menu, the same key sequence: driver select at
//  VBlank 450.
//
//  This is SOLVED in MM_NativeMode_PageTick, further below, together with the
//  EXECUTE_FUNCPTR that NativeMenuDecl_Confirm sets when replacing.
//
//  AND IT BREAKS SILENTLY. Twice in the same week the log looked
//  perfectly in order: TIME TRIAL set all bits correctly
//  (gameMode1 0x00022000, player 1) and the screen did not move;
//  SINGLE RACE and CUP RACE reached their target box and started no
//  race. From that the acceptance rule: "reaches the target box" is NO proof.
//  A mode only counts as proven when a run goes through from the mode choice to the
//  loading screen.
// ===========================================================================

// ---------------------------------------------------------------------------
//  THE MODE CHOICE, AS EFFECTS INSTEAD OF A TEXT INDEX CHAIN.
//
//  In retail the LANGUAGE INDEX of the chosen main menu row decides what
//  happens (MM_MenuFlow.c:125 and the if chain below it). That couples the
//  label to the effect: whoever renames the row changes the game.
//  These seven verbs undo the coupling for the arcade path - the file
//  says WHICH row calls which verb, and the text is just text again.
//
//  THE CLEARING STANDS IN EVERY VERB, NOT BEFORE IT.
//
//  MM_MenuProc_Main clears the mode mask as soon as ANY main menu
//  row is confirmed (:154) - one level above the place where the
//  mode is now chosen. If ARCADE kept setting its bit and left the
//  choice below it standing, TIME TRIAL would meet a set ARCADE_MODE, and
//  MM_MenuProc_SingleCup decides exactly on this bit. So every
//  verb first cleans up itself; afterwards exactly one mode stands.
// ---------------------------------------------------------------------------


// WHAT THE CHOICE CHANGED IN THE GAME STATE, in one line per choice.
//
// The mode bits are otherwise visible nowhere: they stand in gGT and take effect
// only screens later. Without this line "no bit stayed set" is a
// claim and not a proof - and exactly that was the problem with the text index chain,
// that nobody could say what a row had really set.
internal void MM_NativeMode_Log(const char *name, u32 vorher1, u32 vorher2)
{
	struct GameTracker *gGT = sdata->gGT;

	Platform_Log("[CTR Menu] mode '%s': gameMode1 0x%08x -> 0x%08x, gameMode2 0x%08x -> 0x%08x, players %d, laps %d\n", name, (unsigned)vorher1,
	             (unsigned)gGT->gameMode1, (unsigned)vorher2, (unsigned)gGT->gameMode2, (int)gGT->numPlyrNextGame, (int)gGT->numLaps);
}

internal void MM_NativeMode_Clear(void)
{
	struct GameTracker *gGT = sdata->gGT;

	gGT->gameMode1 &= ~(BATTLE_MODE | ADVENTURE_MODE | TIME_TRIAL | ADVENTURE_ARENA | ARCADE_MODE | ADVENTURE_CUP);
	gGT->gameMode2 &= ~(CUP_ANY_KIND);

	// As in the retail branch: the default switches off the one-lap cheat in
	// time trial and adventure.
	gGT->numLaps = MM_DEFAULT_LAP_COUNT;
}

int MM_NativeMode_ActSingle(struct RectMenu *box)
{
	struct GameTracker *gGT = sdata->gGT;
	const u32 vorher1 = gGT->gameMode1;
	const u32 vorher2 = gGT->gameMode2;

	(void)box;
	MM_NativeMode_Clear();
	gGT->gameMode1 |= ARCADE_MODE;
	MM_NativeMode_Log("SINGLE RACE", vorher1, vorher2);

	return 1;
}

int MM_NativeMode_ActCup(struct RectMenu *box)
{
	struct GameTracker *gGT = sdata->gGT;
	const u32 vorher1 = gGT->gameMode1;
	const u32 vorher2 = gGT->gameMode2;

	(void)box;
	MM_NativeMode_Clear();
	gGT->gameMode1 |= ARCADE_MODE;
	gGT->gameMode2 |= CUP_ANY_KIND;
	MM_NativeMode_Log("CUP RACE", vorher1, vorher2);

	return 1;
}

// THE TITLE SCREEN TICKS ON THE MAIN MENU BOX - AND THAT IS GONE HERE.
//
// RECTMENU_ProcessState calls funcPtr only for sdata->ptrActiveMenu
// (RECTMENU.c:1046), and on a REPLACED page that is no longer
// D230.menuMainMenu but the page itself. So MM_MenuProc_Main
// no longer runs, and with it MM_Title_MenuUpdate no longer runs - the only
// place that reads titleMenuState and desiredMenuIndex at all
// (MM_Title.c:120).
//
// WHAT GOT STUCK BECAUSE OF THAT, measured on 2026-09-17 with --menu-keys:
//
//   SINGLE RACE - 1P - EASY   MM_MenuProc_Difficulty (MM_MenuFlow.c:464)
//                             sets EXITING and CHARACTER_SELECT; the picture
//                             stays at EASY, VBlank 416 to 500
//                             unchanged.
//   VS - SINGLE - 2P          MM_MenuProc_2p3p4p (MM_MenuFlow.c:378), likewise.
//
//   Cross-check with --stock-menu, the same key sequence: the driver select
//   stands at VBlank 450. It is due to the rework and not to the game.
//
// This function gives the replaced page back the part of the frame tick that
// the main menu box carried for the whole chain. What does NOT come back is
// the demo counter: a replaced page is a choice made and not
// idle time.
void MM_NativeMode_PageTick(struct RectMenu *box)
{
	// Retail rewrites both lock tables in EVERY frame, and both
	// concern boxes hanging below this page: 1P/2P, 2P/3P/4P and the
	// three difficulty levels.
	MM_ToggleRows_Difficulty();
	MM_ToggleRows_PlayerCount();

	// Custom cup: all difficulties open (MM_NativeCupSelect.c).
	MM_NativeCup_OpenDifficulty();

	MM_Title_MenuUpdate();

	// If the exit has just fired, ptrDesiredMenu points at a
	// different box. The page is then left and not gone back to,
	// so the marker of the "ersetzen" branch has to be cleared - otherwise it still stands
	// at the next title screen.
	if ((sdata->ptrDesiredMenu != NULL) && (sdata->ptrDesiredMenu != box))
	{
		NativeMenuDecl_NoteAbandoned(box);
	}
}

// RETURNS 0, AND THAT IS THE POINT.
//
// Time trial opens no child - it LEAVES the chain through desiredMenuIndex
// and titleMenuState, like the retail branch (MM_MenuFlow.c:204). The zero tells
// NativeMenuDecl_Confirm that the row is answered and no "weiter"
// should open any more.
int MM_NativeMode_ActTimeTrial(struct RectMenu *box)
{
	struct GameTracker *gGT = sdata->gGT;
	const u32 vorher1 = gGT->gameMode1;
	const u32 vorher2 = gGT->gameMode2;
	struct RectMenu *parent = (box != NULL) ? (struct RectMenu *)box->ptrPrevBox_InHierarchy : NULL;

	MM_NativeMode_Clear();

	gGT->numPlyrNextGame = 1;
	gGT->gameMode1 |= TIME_TRIAL;

	// THE MAIN MENU MUST GO BACK TO THE SLOT OF THE ACTIVE BOX.
	//
	// The exits of the title screen - including the one to driver select - stand in
	// MM_Title_MenuUpdate (MM_Title.c:3), and that runs from MM_MenuProc_Main.
	// A proc runs only for the ACTIVE box. As long as the replaced page
	// is on top, nobody looks at the set desiredMenuIndex: measured on
	// 2026-09-17 the bits were right and the screen stayed where it was.
	//
	// ONLY_DRAW_TITLE deliberately STAYS set here. The main menu thereby fades
	// out while it only shows the chosen row - exactly
	// the picture the retail path makes. It is reset on
	// re-entry (MM_MenuFlow.c:618) and not here.
	if (parent != NULL)
	{
		sdata->ptrDesiredMenu = parent;
	}

	// This page is left and not gone back to - the marker of the
	// "ersetzen" branch has to be cleared.
	NativeMenuDecl_NoteAbandoned(box);

	D230.titleMenuState = TITLE_MENU_STATE_EXITING;
	D230.desiredMenuIndex = MM_EXIT_ROUTE_CHARACTER_SELECT;

	MM_NativeMode_Log("TIME TRIAL", vorher1, vorher2);

	return 0;
}

// VS SETS NO BIT, and that is not forgetfulness. In retail VS is the
// ABSENCE of ARCADE_MODE and BATTLE_MODE - that is how
// MM_MenuProc_SingleCup tells 1P2P from 2P3P4P (MM_MenuFlow.c:498).
int MM_NativeMode_ActVs(struct RectMenu *box)
{
	struct GameTracker *gGT = sdata->gGT;
	const u32 vorher1 = gGT->gameMode1;
	const u32 vorher2 = gGT->gameMode2;

	(void)box;
	MM_NativeMode_Clear();
	MM_NativeMode_Log("VS", vorher1, vorher2);

	return 1;
}

int MM_NativeMode_ActBattle(struct RectMenu *box)
{
	struct GameTracker *gGT = sdata->gGT;
	const u32 vorher1 = gGT->gameMode1;
	const u32 vorher2 = gGT->gameMode2;

	(void)box;
	MM_NativeMode_Clear();
	gGT->gameMode1 |= BATTLE_MODE;
	D230.characterSelectTransitionState = EXITING_MENU;
	MM_NativeMode_Log("BATTLE", vorher1, vorher2);

	return 1;
}

// THE CUP CHOICE UNDER VS, and why it needs two verbs of its own.
//
// "On entering VS, CUP_ANY_KIND is cleared anyway" is only true the
// FIRST time. Whoever goes VS -> CUP -> back -> VS -> SINGLE comes out without these
// two verbs with the cup bit set in the single race.
int MM_NativeMode_ActVsSingle(struct RectMenu *box)
{
	struct GameTracker *gGT = sdata->gGT;
	const u32 vorher1 = gGT->gameMode1;
	const u32 vorher2 = gGT->gameMode2;

	(void)box;
	gGT->gameMode2 &= ~(CUP_ANY_KIND);
	MM_NativeMode_Log("VS SINGLE", vorher1, vorher2);

	return 1;
}

int MM_NativeMode_ActVsCup(struct RectMenu *box)
{
	struct GameTracker *gGT = sdata->gGT;
	const u32 vorher1 = gGT->gameMode1;
	const u32 vorher2 = gGT->gameMode2;

	(void)box;
	gGT->gameMode2 |= CUP_ANY_KIND;
	MM_NativeMode_Log("VS CUP", vorher1, vorher2);

	return 1;
}

// QUIT. The same exit(0) as Ctrl+Q and the window close button, so that the
// reports at atexit arrive the same way on every path.
#if defined(_MSC_VER)
// Platform_QuitGame leaves through exit(0) and does not come back. The
// return behind it is therefore dead code - the compiler is right, and the
// signature demands it anyway. Only around this one function.
#pragma warning(push)
#pragma warning(disable : 4702)
#endif
int MM_NativeMenu_ActQuit(struct RectMenu *box)
{
	(void)box;

	Platform_QuitGame("QUIT in the main menu");

	return 1;
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

// Which style applies to which box: retail, plus only what a declared box
// from menus/nitro-pit.menu names explicitly. The startup no longer loads
// a declaration (main.c), so it is
// g_rectMenuStyleRetail for every box. The branch that gave the main menu scale and
// minimum width is gone together with --stock-menu and --menu-scale.
const struct RectMenuStyle *MM_NativeMenu_StyleFor(const struct RectMenu *menu)
{
	local_persist struct RectMenuStyle style;

	style = g_rectMenuStyleRetail;

	// And on top of that, what the file named explicitly.
	NativeMenuDecl_OverlayStyle(&style, menu);

	// THE MINIMUM WIDTH: THE ROOT SETS IT, A CHILD RAISES IT. The chain is drawn with
	// ONE width, and the style that clamps it is the one of this
	// box - an entry on a child would otherwise have no effect.
	{
		const int eigen = NativeMenuDecl_MinWidthChars(menu);
		const int kinder = NativeMenuDecl_ChildMinWidthChars(menu);

		if (eigen >= 0)
		{
			style.minWidthChars = (s16)eigen;
		}

		if (kinder > (int)style.minWidthChars)
		{
			style.minWidthChars = (s16)kinder;
		}
	}

	return &style;
}

#endif
