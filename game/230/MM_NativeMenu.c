#include <common.h>

#ifdef CTR_NATIVE

#include "platform/native_assets.h"

// THE NATIVE PART OF THE MAIN MENU OVERLAY.
//
// Our own text rows (MM_NativeMenu_String), the NITRO-PIT track list (rows,
// names, loading a container onto its donor slot, the run-time level IDs at
// the funnel) and the style lookup for RECTMENU. The rows of the main menu
// itself are declared in game/native_menuscreen.c.
//
// The reference space is 512 x 216 - the same area in which the declarations in
// game/native_uidecl.c lie and in which 224.c puts its centred rows at 0x100.
// The display converts the aspect ratio on top of that.

// Whether the Nitro-Pit path was taken. Points the select screen at the
// containers from tracks/ instead of the tracks of the disc.
int g_mmNitroPit = 0;

// Which mode the list provides: NITRO RACE, CRYSTAL or CTR.
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

// The donor slot.
//
// A container track is today not loaded as a level slot of its own
// but put in the place of an existing one; LOAD_VramFileCallback and
// the LEV reader reach through this ID. The debug menu lets you cycle the slot
// and stands on DINGO CANYON when nobody chooses anything else.
// Here it is not a row in the menu but a question to the container - and
// the default is the same.
#define MM_NATIVE_TRACK_DONOR DINGO_CANYON

// The second donor slot: for the CRYSTAL mode. It follows the mode that is
// started, not the crystal bit in META (MM_NativeTracks_DonorFor).
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
// WHAT THIS DOES NOT DO: the slot is the precondition for the mode, not the
// mode. Retail sets CRYSTAL_CHALLENGE only at the warp pad
// (AH_WarpPad.c:612); for a container the NITRO-PIT -> CRYSTAL path sets it
// through MM_NativeCrystal_Arm (MM_NativeCrystal.c).
#define MM_NATIVE_CRYSTAL_DONOR NITRO_COURT

// How many containers the select screen can hold. The reader in
// native_assets.c holds the same number; here it is clamped against NativeTrack_Count(),
// so that this is only an upper bound for the space.
//
// SIXTY-FOUR, AND WHY NOT MORE. An entry is about 2,400 bytes -
// path[1024], name[513], author[513], note[192], file[128] and the numbers -,
// so 155 KB at 64. In a process that holds double-digit megabytes that is
// no size; 64 simply leaves room for more tracks than anyone has.
#define MM_NATIVE_SELECT_MAX 64

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
// The rows of our own boxes (native_menuscreen.c) are named in the switch
// below.
//
// The rows of the CHEATS box (native_menuscreen.c).
char *NativeMenuCheats_String(s16 index);

// The MODE box of the NITRO RACE track screen until 0.7.5, which the MODS box
// replaced (TIME TRIAL is a row of the NITRO-PIT box now): its texts stay in
// the table, drawn by nobody, so the numbers after them keep their places. In
// the free gap 0x8b..0x8f of the range in ovr_230.h. MODE has a colon in the
// language file (LNG_MODE), the title of the lap box has none (LNG_LAPS).
enum
{
	MM_NATIVE_LNG_MODE = MM_NATIVE_LNG_BASE + 0x8b,
	MM_NATIVE_LNG_MODE_RACE = MM_NATIVE_LNG_BASE + 0x8c,

	// Planned, not offered yet.
	MM_NATIVE_LNG_MODE_BOSS_RACE = MM_NATIVE_LNG_BASE + 0x8d,

	// The MODS box of the arcade track select (MM_NativeModsBox.c): its title
	// and its status line (platform/native_mods.c, at most 11 characters).
	MM_NATIVE_LNG_MODS = MM_NATIVE_LNG_BASE + 0x8e,
	MM_NATIVE_LNG_MODS_STATUS = MM_NATIVE_LNG_BASE + 0x8f,
};

const char *NativeMods_StatusText(void); // platform/native_mods.c

char *MM_NativeMenu_String(s16 index)
{
	char *cheat;

	// OUR OWN ROWS (native_menuscreen.c): race type box, NITRO-PIT,
	// main menu.
	//
	// Each is at most ten characters long, as long as TIME TRIAL and HIGH
	// SCORE. RECTMENU_GetWidth counts every row of every box of the chain,
	// and the main menu stands in every chain. With eleven
	// characters every arcade box would get wider, including the one of SINGLE and CUP.
	switch (index)
	{
	case MM_NATIVE_LNG_NITRO_PIT:
		return "NITRO-PIT";
	// Open: RACE and CUP - the box is already called NITRO-PIT. Collapsed, only
	// the chosen row without title stands in the chain (RECTMENU_DrawSelf),
	// there NITRO RACE / NITRO CUP says where you are. The question
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
	case MM_NATIVE_LNG_MODE:
		return "MODE";
	case MM_NATIVE_LNG_MODE_RACE:
		return "RACE";
	case MM_NATIVE_LNG_MODE_BOSS_RACE:
		return "BOSS RACE";
	case MM_NATIVE_LNG_MODS:
		return "MODS";
	case MM_NATIVE_LNG_MODS_STATUS:
		return (char *)NativeMods_StatusText();
	default:
		break;
	}

	cheat = NativeMenuCheats_String(index);

	if (cheat != NULL)
	{
		return cheat;
	}

	return NULL;
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

// The longest line any caller draws, in characters: the wheel of the track
// screen takes 14 (MM_NATIVE_WHEEL_NAME_CHARS), the cup boxes 12, the titles at
// race start and in the standings the full 16. MM_NativeTracks_Shorten clamps
// to it on its own as well: a limit that only the caller keeps would not hold
// for the next caller.
#define MM_NATIVE_LINE_MAX 16

// One buffer for the row being drawn. One is enough: DecalFont
// draws straight from the pointer, the row is finished before the next one
// is filled.
static char s_mmLineBuffer[MM_NATIVE_LINE_MAX + 1];

// THE SHORT FORM OF A TRACK NAME - ONE RULE, NO SPECIAL CASE.
//
// A row of a dozen characters does not carry "Pizza Planet Crystal". Cut
// hard, "Arabian Heights" would become "ARABIAN HEI", and the cut could not be
// seen - the name would look as if that was its name.
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
// THE SLOT FOLLOWS THE MODE, NOT THE CRYSTAL BIT.
// A container stands in the list of every mode it declares - the same
// container can therefore start as Race, CTR or Crystal. If the crystal bit
// in META decided here, a Race+Crystal container would sit on NITRO COURT (18)
// for RACE and CTR too; there a race starts like an arena (traffic light and
// sound state 10 instead of the intro, MainMain.c:188-196), and every retail
// fallback per level comes from the slot. So: CRYSTAL -> NITRO COURT, RACE and
// CTR -> DINGO CANYON. Cups are Race.
//
// The CRYSTAL list only lets containers with the bit in
// (NativeTrack_WhyNotOffered), so a pure crystal track always sits on 18 and
// a track without the bit on 0. Only a container with both modes sits on a
// different slot depending on the mode.
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
	// The CRYSTAL line names the crystal bit: the CRYSTAL list only lets
	// containers with that bit in, so the sentence is always true.
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
// catches every OTHER caller that requests a run-time ID (--autoload-track,
// the custom cup) - for those the funnel is then the same path as for the
// menu.
//
// IF NOTHING STANDS BEHIND THE ID - a remembered ID after a restart,
// a wrong number from a caller -, the run falls back LOUDLY to the
// bare donor slot: retail DINGO CANYON, without a container. That is
// visibly wrong instead of silently off, and it indexes no table
// outside its bounds. Release first, so that a leftover
// container of a DIFFERENT run does not jump in on the same slot.
//
// THE SWITCH BETWEEN TWO RACES (custom cup). The cup
// requests the next race from the standings (UI_CupStandings.c), and
// there the level of the RUNNING container is still going: after RequestLoad
// the finish flag first slides in, and below it drawing continues until it is
// fully in the picture (MainMain.c, LOAD_REQUESTED: "keep rendering the scene").
// A swap here would give the level that is still visible the numbers of the
// next one - sky and draw memory, intersection buffer, track mods, sound.
// So if a container is currently running, the funnel here only decides the
// slot, and the swap happens in MM_NativeTracks_StartLoad, when the loader
// begins and only the flag is drawn any more (LOAD_LevelFile). Every other
// path loads from the menu - there no container is running, and for it
// nothing changes.
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
// donor slot, and there DINGO CANYON would stand. The title at race start
// (UI_RaceFlow.c) and the standings (UI_CupStandings.c) ask here first.
// Between two cup races, until the swap, that is the track that is still
// visible - the same question as when loading.
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

// Which container stands on which row. A row is not the scan index:
// containers without the mode of the list do not stand in it
// (NativeTrack_HiddenFrom). The level IDs, track-ids.tsv, the
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

// For the NITRO-PIT track screen (MM_NativeTrackSelect.c).
// The rows are rebuilt at every entry. The folder is not re-read
// in doing so; the list from startup applies.
int MM_NativeTracks_BuildRows(void)
{
	MM_NativeTracks_BuildSelectRows();

	return s_mmSelectCount;
}

// Which style applies to which box: the retail style, for every box.
const struct RectMenuStyle *MM_NativeMenu_StyleFor(const struct RectMenu *menu)
{
	(void)menu;

	return &g_rectMenuStyleRetail;
}

#endif
