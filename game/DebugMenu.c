#include <common.h>

// The session cheats from gameMode2 as text (native_menuscreen.c, CHEATS box);
// the [CTR Race] line names them, a measurement run must show "cheats none".
const char *NativeMenuCheats_Describe(void);

#ifdef CTR_NATIVE

// The TRACKS page reads fields of struct NativeTrackEntry, and a struct
// cannot be forward-declared like a function. Hence a header here
// instead of the extern lines further down. The include guard in the file makes
// this a no-op, main.c pulled it in long ago.
#include <platform/native_assets.h>

// --menu-keys plays a key sequence into the pad bus. That needs the
// snapshot that replay and quick state install as well.
#include <platform/native_input.h>

// The in-game debug menu.
//
// SELECT+START opens it wherever the game logic runs. While it is open the game
// underneath sees no input at all, which is also what keeps the pause menu from
// answering the same START.
//
// CHEATS calls the game's own cheat functions, LEVEL SELECT loads a track
// directly, VIDEO and VIEW hold the picture settings. Other debug pages of the
// reference build - system, dev tools - were not carried over: a page that
// ruins the picture on purpose has no business one row below a page that loads
// a level.
//
// And TRACKS, which is a different kind of row: it lists what is in tracks/ and
// shows what the packer wrote into each container. That page is behind
// --tracks, off by default, and it says so on its own first line when it is off
// rather than disappearing - a row that is missing looks like a row that never
// existed.
//
// The platform logger, so opening and closing land in the log file.
void Platform_Log(const char *format, ...);
int Platform_GetVBlankCount(void);

// What the VIDEO page acts on. Declared here rather than pulled in through a
// platform header, the way the two above already are: this file is included into
// the game unity build and sees the game's headers, not the platform's.
void Platform_ToggleFullscreen(void);
int Platform_IsFullscreen(void);
void Platform_GetWindowSize(int *outWidth, int *outHeight);
void Platform_GetFrameSize(int *outWidth, int *outHeight);
void Platform_SetAspect(int width, int height);
void Platform_GetAspect(int *outWidth, int *outHeight);
void Platform_SetResolutionScale(int scale);
int Platform_GetResolutionScale(void);
int Platform_GetResolutionScaleSetting(void);
int Platform_GetResolutionScaleMax(void);
int Platform_ResolutionIsNative(void);
int Platform_GetResolutionNativePosition(void);
void Platform_GetRenderTargetSize(int *outWidth, int *outHeight);
void Platform_SetPageScale(int scale);
int Platform_GetPageScale(void);
void Platform_GetPageStoreUse(int *outTilesUsed, int *outTilesTotal, int *outKiB);
int Platform_GetDither(void);
void Platform_StepDither(void);
const char *Platform_DitherName(void);
void Platform_StepMsaa(void);
int Platform_GetMsaa(void);
int Platform_GetMsaaRequested(void);
const char *Platform_MsaaName(int samples);
int Platform_GetPageRowRange(void);
void Platform_SetPageRowRange(int on);
int Platform_GetPagePreload(void);
void Platform_SetPagePreload(int on);

// Every menu step that is meant to survive a restart writes the file. Declared
// here with the rest of the platform side this file reaches for.
void Platform_SettingsSave(void);

// LEVEL SELECT opens DBG_DRIVER, because who races is part of asking for a
// level and not a separate errand. TRACKS opens DBG_TRACK the same way, for the
// same reason: the list has one row per container and the metadata do not fit
// beside it.
//
// The two of them sit AFTER the root rows on purpose: the root's rows map to
// pages by "row + 1", so a page that is meant to be opened from somewhere else
// cannot be reached from the root by accident.
enum
{
	DBG_ROOT = 0,
	DBG_CHEATS,
	DBG_LEVEL,
	DBG_VIDEO,
	DBG_VIEW,
	DBG_TRACKS,
	DBG_DRIVER,
	DBG_TRACK,
	DBG_PAGE_COUNT,
};

// The panel is the left half of the 512x216 canvas.
#define NATIVE_DEBUG_MENU_W   256
#define NATIVE_DEBUG_MENU_H   216
#define NATIVE_DEBUG_MENU_PAD 8
#define NATIVE_DEBUG_MENU_ROW 12

// The two track pages get a wider one.
//
// 256 was picked when every page was a list of short labels. FONT_SMALL
// advances thirteen pixels per character - data.font_charPixWidth in
// game/zGlobal_DATA.c, and DecalFont_DrawLine uses it for every character
// including the space - so 256 minus the two margins holds EIGHTEEN of them.
//
// That number was never looked up. The first TRACK page was written with
// nine-character labels and sixteen-character values, twenty-five in all, and
// every line of it ran off the panel into the picture. A fingerprint alone is
// sixteen hex digits, and a UUID is thirty-two.
//
// 384 holds twenty-eight. Only those two pages get it: a panel wider than its
// content covers more of the picture for nothing.
#define NATIVE_DEBUG_MENU_W_TRACKS 384

// Header, rule, then rows; footer two lines up from the bottom. What is left
// over is how many rows fit, so a longer list scrolls rather than running off
// the panel.
#define DBG_FIRST_ROW_Y  28
#define DBG_VISIBLE_ROWS 12

global_variable const char *const s_rootNames[] = {"CHEATS", "LEVEL SELECT", "VIDEO", "VIEW", "TRACKS"};

// The VIDEO page.
//
// One row does something and two report. That is not padding: the frame the game
// draws and the window it is stretched onto are two different numbers, and the
// gap between them is the whole reason anyone asks for a higher internal
// resolution. A page that hid them would make the missing setting invisible.
//
// The INT RES row shows the factor in force, not the one that was asked for.
// The two differ only where the asked-for target would be larger than a texture
// may be, and the one in force is what every rectangle in the frame is
// multiplied by - so it is the one worth showing.
//
// This note used to say there was no internal-resolution row because there was
// no internal resolution. That stopped being true when the factor reached the
// target, the viewport, the scissor and the present path, and the note stayed
// behind: the same fact in two places, one of them falling back.
enum
{
	DBG_VIDEO_FULLSCREEN = 0,
	DBG_VIDEO_ASPECT,
	DBG_VIDEO_RES_SCALE,
	DBG_VIDEO_RES_RESET,

	// The page store sits next to the internal resolution because that is what
	// it is for. A texture with more texels in it shows nothing extra until
	// there are more screen pixels to put them on, so the two rows are read
	// together or neither of them means anything.
	//
	// There is no row that turns the store off. It is the only route an indexed
	// texture has now, so a switch would be a switch between a picture and no
	// picture.
	DBG_VIDEO_PAGE_SCALE,
	DBG_VIDEO_PAGE_TILES,

	// The two rows that decide WHEN a tile is written rather than what is in it.
	// ROWS asks whether a tile is refilled only when the VRAM that moved lies in
	// the rows that tile is actually read at; HOLD ALL asks whether every tile is
	// resident from the start instead of arriving when a page is first named.
	//
	// Both here, beside TILES, because TILES is the number they move.
	DBG_VIDEO_PAGE_ROWS,
	DBG_VIDEO_PAGE_PRELOAD,

	// When the console's dither matrix is added. Beside the page rows because
	// this is the other thing that decides what a pixel ends up being rather
	// than where it comes from.
	DBG_VIDEO_DITHER,

	// Anti-aliasing, OFF / 2x / 4x. Beside DITHER for the same reason DITHER is
	// beside the page rows: it decides what a pixel at an edge ends up being.
	// The row shows what is in force, read back - a count the device cannot do
	// is stepped down, and a row that printed the wish would hide that.
	DBG_VIDEO_MSAA,

	// One row per detail stage, counted out of CTR_Lod's own table rather than
	// listed here, so a fifth stage appears on this page without this file being
	// told about it. Each row is a toggle: HIGH means the stage never steps down
	// with distance, STOCK means it does what the game shipped doing.
	//
	// Four rows and not one setting, because the four cost wildly different
	// amounts and the only way to find out which one is worth its price is to
	// turn them on one at a time.
	// Above the four, because it answers for three of them. The row order should
	// read the way the code does: the mode first, then what is left for the
	// stages to decide.
	DBG_VIDEO_LOD_NONE,

	DBG_VIDEO_LOD_FIRST,
	DBG_VIDEO_LOD_LAST = DBG_VIDEO_LOD_FIRST + CTR_LOD_STAGE_COUNT - 1,

	DBG_VIDEO_WINDOW,
	DBG_VIDEO_FRAME,
	DBG_VIDEO_TARGET,
	DBG_VIDEO_ROW_COUNT,
};

// The VIEW page: the set of view settings that belongs to the shape the
// picture is currently driven in.
//
// Three sets exist and only the active one is shown, because only the active
// one can be judged - the point of turning FOV is watching what it does, and
// what it does at 43:18 is not what it does at 4:3. Switching sets is
// switching aspect, one page up.
//
// The title carries the shape and a star when the set no longer holds stock
// values, so "the picture looks wrong" and "I turned something" cannot be
// confused with each other.
//
// Rows come from CTR_View's descriptor table rather than being listed here -
// the table is what the config file writes and what clamps, so a fifth
// setting appears on this page without this file being told.
enum
{
	DBG_VIEW_RESET_ROW = CTR_VIEW_SETTING_COUNT,
	DBG_VIEW_ROW_COUNT,
};

// The TRACK page: what the packer wrote into one container, and the three
// rows that do something with it.
//
// A META field on this page is shown even when it is empty. An author who left
// out a field should see an empty row rather than no row - a field that is
// simply not printed cannot be told apart from a field that does not exist.
//
// The rows that ACT come first: LOAD, SLOT and CRYSTAL. At the bottom, under
// everything they act on, they read well and do not work once the page has
// more rows than the twelve on screen: LOAD AND RACE would sit below the fold,
// and a page that can only be read looks exactly like a page whose track cannot
// be started. Everything is in the log either way.
//
// CRYSTAL sits with LOAD and SLOT because it acts: it changes what LOAD does,
// the same way SLOT does.
//
// Nine rows, twelve fit, so nothing is below the fold.
enum
{
	DBG_TRACK_LOAD = 0,
	DBG_TRACK_DONOR,
	DBG_TRACK_CRYSTAL,

	DBG_TRACK_NAME,
	DBG_TRACK_AUTHOR,
	DBG_TRACK_VERSION,
	DBG_TRACK_MODES,
	DBG_TRACK_META_VERSION,
	DBG_TRACK_FILE,
	DBG_TRACK_ROW_COUNT,
};

// Row 0 of the TRACKS list is RESCAN, the containers start under it.
#define DBG_TRACKS_RESCAN_ROW 0
#define DBG_TRACKS_FIRST_ROW  1

// The factor climbs and wraps rather than stepping with left and right: this
// menu has no left and right - '<' backs out of a page - so a row that can only
// be pressed needs somewhere to go when it runs out. RESET TO x1 is the way back
// that does not go through seven presses.
//
// Where it wraps is asked of the renderer rather than written down here. It was
// a DBG_RES_SCALE_MAX of 8 beside a NATIVE_RES_SCALE_MAX of 8: the same fact in
// two places, and raising one would have left this one wrapping at the old
// number with nothing to say so.

// The atlas is eight tiles across, so scale four is an 8192 edge - the size a
// device is most likely to stop at. Beyond that there is nothing to offer.
#define DBG_PAGE_SCALE_MAX 4

// The three shapes used to be written out again here. They live in CTR_View
// now, where the startup detection also reads them - one list, so a row added
// to it appears in both places or in neither.

struct DebugCheat
{
	const char *name;
	void (*apply)(void);
};

// The game's own cheat functions, the ones the code entry screen calls. Nothing
// is reimplemented here - this page is a second way to reach them, not a second
// version of them.
global_variable const struct DebugCheat s_cheats[] = {
    {"MAX WUMPA", MM_Cheat_MaxWumpa},
    {"INFINITE MASKS", MM_Cheat_InfiniteMasks},
    {"MAX TURBOS", MM_Cheat_MaxTurbos},
    {"MAX INVISIBILITY", MM_Cheat_MaxInvisibility},
    {"MAX ENGINE", MM_Cheat_MaxEngine},
    {"MAX BOMBS", MM_Cheat_MaxBombs},
    {"TURBO COUNTER", MM_Cheat_TurboCounter},
    {"SUPER TURBO PADS", MM_Cheat_SuperTurboPads},
    {"ICY TRACKS", MM_Cheat_IcyTracks},
    {"ADV DIFFICULTY", MM_Cheat_AdvDifficulty},
    {"SUPER HARD", MM_Cheat_SuperHard},
    {"ONE LAP", MM_Cheat_OneLap},
};

struct DebugDriver
{
	const char *name;
	int id;
};

// Who the jump races as.
//
// Without this the level would load with whatever data.characterIDs[0] happened
// to hold, which from the title screen is the demo's leftover - and every driver
// asset the level then loads is picked from it. The names are short because the
// LEVEL SELECT page prints one of them inside a 256 px panel next to the word
// DRIVER.
global_variable const struct DebugDriver s_drivers[] = {
    {"CRASH", CRASH_BANDICOOT}, {"CORTEX", NEO_CORTEX},      {"TINY", TINY_TIGER},       {"COCO", COCO_BANDICOOT},
    {"N.GIN", N_GIN},           {"DINGODILE", DINGODILE},    {"POLAR", POLAR},           {"PURA", PURA},
    {"PINSTRIPE", PINSTRIPE},   {"PAPU PAPU", PAPU_PAPU},    {"RIPPER ROO", RIPPER_ROO}, {"KOMODO JOE", KOMODO_JOE},
    {"N.TROPY", N_TROPY},       {"PENTA", PENTA_PENGUIN},    {"FAKE CRASH", FAKE_CRASH}, {"OXIDE", NITROS_OXIDE},
};

struct DebugLevel
{
	const char *name;
	int id;
};

global_variable const struct DebugLevel s_levels[] = {
    {"DINGO CANYON", DINGO_CANYON},     {"DRAGON MINES", DRAGON_MINES},
    {"BLIZZARD BLUFF", BLIZZARD_BLUFF}, {"CRASH COVE", CRASH_COVE},
    {"TIGER TEMPLE", TIGER_TEMPLE},     {"PAPU PYRAMID", PAPU_PYRAMID},
    {"ROO'S TUBES", ROO_TUBES},         {"HOT AIR SKYWAY", HOT_AIR_SKYWAY},
    {"SEWER SPEEDWAY", SEWER_SPEEDWAY}, {"MYSTERY CAVES", MYSTERY_CAVES},
    {"CORTEX CASTLE", CORTEX_CASTLE},   {"N.GIN LABS", N_GIN_LABS},
    {"POLAR PASS", POLAR_PASS},         {"OXIDE STATION", OXIDE_STATION},
    {"COCO PARK", COCO_PARK},           {"TINY ARENA", TINY_ARENA},
    {"SLIDE COLISEUM", SLIDE_COLISEUM}, {"TURBO TRACK", TURBO_TRACK},
    {"NITRO COURT", NITRO_COURT},       {"RAMPAGE RUINS", RAMPAGE_RUINS},
    {"PARKING LOT", PARKING_LOT},       {"SKULL ROCK", SKULL_ROCK},
    {"NORTH BOWL", THE_NORTH_BOWL},     {"ROCKY ROAD", ROCKY_ROAD},
    {"LAB BASEMENT", LAB_BASEMENT},
};

#define DBG_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

// Row 0 of LEVEL SELECT is the driver, row 1 the crystal probe, the levels
// start under them.
//
// CRYSTAL sits at the top next to DRIVER and not below the 25 tracks, for the same
// reason as on the TRACK page: what ACTS comes first. A row that
// closes a list of 25 entries far below would be outside the
// twelve visible rows and thus a row that does not exist for the
// user. MEASURING INSTRUMENT, see s_crystalProbe.
#define DBG_LEVEL_DRIVER_ROW      0
#define DBG_LEVEL_CRYSTAL_ROW     1
#define DBG_LEVEL_FIRST_LEVEL_ROW 2

// What a jump races over when nothing chose a lap count. The stock lap menu's
// first row is the same number.
#define DBG_JUMP_LAPS 3

// How long a held direction waits before it steps again. Ten VBlanks is a
// comfortable hand speed; without it a held button walks the whole list,
// because the input wipe further down destroys the game's own edge detection.
#define DBG_NAV_REPEAT_VBLANKS 10

// How long the fullscreen row waits before it will fire again. Longer than the
// navigation gate on purpose - see the row itself.
#define DBG_FULLSCREEN_COOLDOWN_VBLANKS 45

// Every seat the game has, not the first four. The keyboard feeds slot 0 by
// default and a controller lands wherever it connected, but the array is eight
// long and a debug tool that only hears half of it reads as "does not open" to
// whoever is sitting in the other half.
#define DBG_PAD_COUNT 8

// A word with every bit set is not a hand on a controller. Seven is generous -
// the largest chord anything here reads is two - and it keeps an unwritten
// input buffer from reading as SELECT+START and opening the menu by itself.
#define DBG_MAX_PLAUSIBLE_BUTTONS 7

global_variable int s_debugMenuOpen = 0;

// Whether the panel is on screen, asked from outside.
//
// The declaration table needs it: its row for this panel is keyed on a COVERS
// rectangle, and COVERS matches ANY element that spans that rectangle - a
// full-screen background does, and one was measured claiming the row on a boot
// screen with the panel shut. A rectangle is not an identity. This is.
int DebugMenu_IsOpen(void)
{
	return s_debugMenuOpen;
}
global_variable int s_page = DBG_ROOT;
global_variable int s_cursor[DBG_PAGE_COUNT];
global_variable int s_scroll[DBG_PAGE_COUNT];
global_variable int s_driver = 0;

// Which container the TRACK page is showing, and which level it would sit in.
//
// The donor is an index into s_levels, the same table LEVEL SELECT uses. A
// container track has no levID of its own; it borrows a seat. That was an early
// trap - two custom tracks both called "level 4" and every
// measurement taken under that name worth nothing - so the seat is on the page
// and in the log beside the name out of META, and the name is what identifies
// the track.
global_variable int s_trackSelected = 0;
global_variable int s_trackDonor = 0;

// MEASURING INSTRUMENT, NOT THE INTENDED PATH.
//
// There is no path by which a container track gets into crystal mode.
// CRYSTAL_CHALLENGE is set in ONE place in the whole tree, in
// AH_WarpPad.c:612, and only the adventure hub's warp pad leads there. The
// Nitro Pit path is the arcade flow and never passes there; the
// debug jump even clears the bit explicitly.
//
// This row bypasses that - not to provide the mode, but to see ONCE
// what the game does when a track runs with the bit set on
// an arena slot. And that without anything being prepared for it:
// at the end of the race overlay 221 unconditionally jumps back into the adventure hub
// and awards a Purple Token (221.c:332-337), the lap count stays
// at three, the time limit stays the arcade value. Exactly that is meant to become
// visible instead of being cleared away beforehand.
//
// REACHABLE ON TWO PAGES, AND IT IS THE SAME SWITCH. The probe hangs
// on the debug jump, not on a page, so it also belongs where
// disc tracks are jumped to. That is not an extra but the purpose of the
// second row: the two observed faults - a
// collected crystal comes back, the counter jumps - can only
// be attributed to a container LEV when a RETAIL arena is held next to it under the same
// probe. Hence one value and two rows, not two
// values.
//
// WHOEVER READS THIS IN THREE WEEKS: this is not a half-finished feature to
// be built on. It is a probe. Once the observation
// is written down, both rows belong out again - and the proper
// path looks different, because it has to answer the three things above.
//
// Default off. As long as it is off, DebugMenu_JumpToLevel computes the same
// two masks as before, bit for bit.
global_variable int s_crystalProbe = 0;

// Toggling and labelling in ONE place, because two pages show it.
//
// Two copies of "ON - PROBE" would be the same fact in two places,
// and one of the two falls behind - exactly the pattern this project
// has already collected three times.
internal void DebugMenu_ToggleCrystalProbe(void)
{
	s_crystalProbe = !s_crystalProbe;
	Platform_Log("[CTR Debug] PROBE crystal bit is now %s - applies to the next debug jump, from TRACK or LEVEL SELECT\n", s_crystalProbe ? "ON" : "OFF");
}

internal const char *DebugMenu_CrystalProbeText(void)
{
	// The word PROBE is on the row as well, so that it does not look like a
	// mode setting, which it is not.
	return s_crystalProbe ? "ON - PROBE" : "OFF";
}

// How many rows the TRACKS list has.
//
// Switched off it is one row, and that row says what to start with. A page that
// simply showed nothing would read as "no tracks", which is a different
// statement and a wrong one.
internal int DebugMenu_TrackRowCount(void)
{
	extern int g_cfg_tracks;
	int count;

	if (!g_cfg_tracks)
	{
		return 1;
	}

	count = NativeTrack_Count();
	return DBG_TRACKS_FIRST_ROW + ((count > 0) ? count : 1);
}

internal int DebugMenu_PanelWidth(int page)
{
	return ((page == DBG_TRACKS) || (page == DBG_TRACK)) ? NATIVE_DEBUG_MENU_W_TRACKS : NATIVE_DEBUG_MENU_W;
}

// Cuts a line down to what the box really holds.
//
// The character width is ASKED of the font and not written down here.
// Exactly that is where the first version failed: the field widths were in the
// format strings, the box width stood next to them as a constant, and neither
// of the two knew about the other.
//
internal void DebugMenu_FitLine(char *line, int page)
{
	int charWidth;
	int usable;
	int fits;

	// Only the two track pages, and that is not arbitrary: the VIEW page
	// with its 23 characters also runs past its box, but what would be
	// cut off there is the raw value in brackets. That would be a
	// change to a page nobody has complained about.
	//
	// Whoever wants it wider gives it a width in DebugMenu_PanelWidth
	// and adds it here.
	if ((page != DBG_TRACKS) && (page != DBG_TRACK))
	{
		return;
	}

	charWidth = data.font_charPixWidth[FONT_SMALL];
	usable = DebugMenu_PanelWidth(page) - 2 * NATIVE_DEBUG_MENU_PAD;
	fits = (charWidth > 0) ? (usable / charWidth) : 0;

	if ((fits > 0) && ((int)strlen(line) > fits))
	{
		line[fits] = '\0';
	}
}

internal int DebugMenu_RowCount(int page)
{
	switch (page)
	{
	case DBG_ROOT:
		return DBG_COUNT(s_rootNames);
	case DBG_CHEATS:
		return DBG_COUNT(s_cheats);
	case DBG_LEVEL:
		return DBG_LEVEL_FIRST_LEVEL_ROW + DBG_COUNT(s_levels);
	case DBG_VIDEO:
		return DBG_VIDEO_ROW_COUNT;
	case DBG_VIEW:
		return DBG_VIEW_ROW_COUNT;
	case DBG_DRIVER:
		return DBG_COUNT(s_drivers);
	case DBG_TRACKS:
		return DebugMenu_TrackRowCount();
	case DBG_TRACK:
		return DBG_TRACK_ROW_COUNT;
	}

	return 0;
}

// The page's own name for its header, and the page O returns to. DBG_DRIVER is
// not a root row, so the "row + 1" arithmetic has nothing to say about it and
// both are asked for here rather than derived.
internal const char *DebugMenu_PageTitle(int page)
{
	local_persist char viewTitle[32];

	if (page == DBG_DRIVER)
	{
		return "DRIVER";
	}

	if (page == DBG_TRACK)
	{
		return "TRACK";
	}

	if (page == DBG_VIEW)
	{
		// Which set is in force, in the one place that is on screen whatever row
		// the cursor is on.
		const int mode = CTR_View_SettingsMode();

		sprintf(viewTitle, "VIEW  %s%s", CTR_View_ModeName(mode), CTR_View_SettingsChanged(mode) ? " *" : "");
		return viewTitle;
	}

	if ((page > DBG_ROOT) && (page <= DBG_COUNT(s_rootNames)))
	{
		return s_rootNames[page - 1];
	}

	return "CTR RELOAD  DEBUG";
}

// What X does on this page. It was a nested pair of ternaries in the footer
// and a third page would not fit in it.
internal const char *DebugMenu_ActionHint(int page)
{
	if (page == DBG_ROOT)
	{
		return "X = OPEN";
	}
	if (page == DBG_VIDEO)
	{
		return "X = TOGGLE";
	}
	if (page == DBG_VIEW)
	{
		return "X = +   [] = -";
	}
	if (page == DBG_TRACKS)
	{
		return "X = OPEN";
	}
	if (page == DBG_TRACK)
	{
		return "X = USE ROW";
	}

	return "X = APPLY";
}

internal int DebugMenu_ParentPage(int page)
{
	if (page == DBG_DRIVER)
	{
		return DBG_LEVEL;
	}

	if (page == DBG_TRACK)
	{
		return DBG_TRACKS;
	}

	return DBG_ROOT;
}

internal void DebugMenu_JumpToLevel(struct GameTracker *gGT, int levelID, int driverID)
{
	// The two masks as values and not directly in the fields: the probe
	// below must take exactly ONE bit out of the clear mask, and a
	// second copy of this list would be the opportunity for the two
	// versions to drift apart.
	u32 addBits = ARCADE_MODE;
	u32 remBits = (ADVENTURE_MODE | ADVENTURE_ARENA | ADVENTURE_CUP | ADVENTURE_BOSS | RELIC_RACE | CRYSTAL_CHALLENGE | TIME_TRIAL | BATTLE_MODE |
	               POINT_LIMIT | LIFE_LIMIT | TIME_LIMIT | END_OF_RACE | GAME_CUTSCENE);

	// The chosen driver. The other seven IDs are deliberately not filled in
	// here: LOAD_DriverMPK calls LOAD_Robots1P(data.characterIDs[0]) itself once
	// MAIN_MENU is off, and a second copy of that fill would be a second source
	// for the same eight numbers.
	data.characterIDs[0] = (s16)driverID;

	// MEASURING INSTRUMENT, see s_crystalProbe. Off is the default, and then
	// the same two masks as without the probe stand below, bit for bit.
	//
	// The bit must come OUT OF THE CLEAR MASK and not only go into the set
	// mask: MainMain.c:247 computes (old | add) & ~rem, so clearing beats
	// setting. If it were in both, the row would have no effect - a probe
	// that silently measures nothing while looking as if it did something.
	//
	// In ONE place with NITRO-PIT -> CRYSTAL (MM_NativeCrystal_ModeBits):
	// both paths set the bit the same way.
	if (s_crystalProbe)
	{
		MM_NativeCrystal_ModeBits(&addBits, &remBits);
	}

	sdata->Loading.OnBegin.AddBitsConfig0 |= addBits;
	sdata->Loading.OnBegin.RemBitsConfig0 |= remBits;

	gGT->gameMode2 &= ~CUP_ANY_KIND;
	gGT->numPlyrNextGame = 1;
	gGT->currLEV = (s16)levelID;
	gGT->numLaps = ((gGT->gameMode2 & CHEAT_ONELAP) != 0) ? 1 : DBG_JUMP_LAPS;

	// What QueueLoadTrack_MenuProc clears for a race that is not a battle.
	gGT->originalEventTime = TITLE_INITIAL_EVENT_TIME;

	// Whatever menu is on screen, off. QueueLoadTrack_MenuProc ends the stock
	// chain with exactly this call.
	if (sdata->ptrActiveMenu != NULL)
	{
		RECTMENU_Hide(sdata->ptrActiveMenu);
	}
	sdata->ptrDesiredMenu = NULL;

	// The probe announces itself, and it also says what it did NOT do. Whoever
	// finds this line in a log later must not be able to conclude that
	// a crystal race was set up here.
	Platform_Log("[CTR Debug] level %d as driver %d, 1 player, %d lap(s)%s\n", levelID, driverID, (int)gGT->numLaps,
	             s_crystalProbe ? " - PROBE: CRYSTAL_CHALLENGE set instead of cleared, nothing else prepared for it" : "");
	MainRaceTrack_RequestLoad((s16)levelID);
}

// One step of one row of the VIEW page, in either direction.
//
// Written to disk on every step rather than on a SAVE row. A row that has to
// be remembered is a row that gets forgotten, and the thing being asked for is
// that a turned value survives a restart. The lock behind --settings-defaults
// is what keeps a measuring run out of it.
internal void DebugMenu_StepViewSetting(int direction)
{
	const int row = s_cursor[DBG_VIEW];
	const int mode = CTR_View_SettingsMode();

	if (row == DBG_VIEW_RESET_ROW)
	{
		// Both directions do the same thing here. There is one way back and it
		// does not have a direction.
		CTR_View_ResetSettings(mode);
		Platform_Log("[CTR Debug] view %s: back to the default\n", CTR_View_ModeName(mode));
	}
	else
	{
		CTR_View_StepSettingValue(mode, row, direction);

		if (CTR_View_SettingIsPercent(row))
		{
			Platform_Log("[CTR Debug] view %s %s = %d%% (%d)\n", CTR_View_ModeName(mode), CTR_View_SettingKey(row),
							CTR_View_SettingPercent(mode, row), CTR_View_SettingValue(mode, row));
		}
		else
		{
			Platform_Log("[CTR Debug] view %s %s = %d\n", CTR_View_ModeName(mode), CTR_View_SettingKey(row),
							CTR_View_SettingValue(mode, row));
		}
	}

	Platform_SettingsSave();
}

// Everything from META, unabridged, into the log in one go.
//
// The page itself shows every field, but the panel cuts
// long texts off. Cut off does not mean lost: what does not fit on the page
// is here in full, at the moment someone opens the
// track.
internal void DebugMenu_LogTrack(const struct NativeTrackEntry *entry)
{
	Platform_Log("[CTR Debug] container %s\n", entry->file);
	Platform_Log("[CTR Debug]   track         %s\n", entry->name);
	Platform_Log("[CTR Debug]   author        %s\n", (entry->author[0] != '\0') ? entry->author : "(not set)");
	Platform_Log("[CTR Debug]   track_version %u\n", entry->trackVersion);
	Platform_Log("[CTR Debug]   modes         %s\n", NativeTrack_ModesText(entry->modes));
	Platform_Log("[CTR Debug]   format        %u.%u\n", entry->formatMajor, entry->formatMinor);
	Platform_Log("[CTR Debug]   meta_version  %u%s\n", entry->metaVersion,
	             !entry->ok ? "" : ((entry->metaVersion == 0) ? "   DRAFT - not for public release" : "   frozen"));

	if (!entry->ok)
	{
		Platform_Log("[CTR Debug]   PROBLEM       %s\n", (entry->problem != NULL) ? entry->problem : "unknown");
	}
}

internal void DebugMenu_Activate(struct GameTracker *gGT)
{
	const int row = s_cursor[s_page];

	switch (s_page)
	{
	case DBG_ROOT:
		s_page = row + 1;
		break;

	case DBG_CHEATS:
		if ((row >= 0) && (row < DBG_COUNT(s_cheats)) && (s_cheats[row].apply != NULL))
		{
			Platform_Log("[CTR Debug] cheat '%s'\n", s_cheats[row].name);
			s_cheats[row].apply();
		}
		break;

	case DBG_LEVEL:
		if (row == DBG_LEVEL_DRIVER_ROW)
		{
			// Opens on the driver a jump would use, not on row 0 - otherwise the
			// cursor and the '<' marker say different things on the first frame.
			s_cursor[DBG_DRIVER] = s_driver;
			s_page = DBG_DRIVER;
			break;
		}

		if (row == DBG_LEVEL_CRYSTAL_ROW)
		{
			// MEASURING INSTRUMENT, see s_crystalProbe. The same switch the
			// TRACK page shows - flipped here, it is flipped there too.
			DebugMenu_ToggleCrystalProbe();
			break;
		}

		{
			const int levelRow = row - DBG_LEVEL_FIRST_LEVEL_ROW;

			if ((levelRow >= 0) && (levelRow < DBG_COUNT(s_levels)))
			{
				DebugMenu_JumpToLevel(gGT, s_levels[levelRow].id, s_drivers[s_driver].id);
				s_debugMenuOpen = 0;
			}
		}
		break;

	case DBG_VIEW:
		DebugMenu_StepViewSetting(+1);
		break;

	case DBG_VIDEO:
		if (row == DBG_VIDEO_FULLSCREEN)
		{
			// A cooldown of its own, longer than the navigation gate. A held X
			// re-activates every DBG_NAV_REPEAT_VBLANKS, and this row tears down
			// and rebuilds the swapchain - the other rows set a flag or load a
			// level and close the menu, so none of them cared.
			local_persist int lastToggleVBlank = -DBG_FULLSCREEN_COOLDOWN_VBLANKS;
			const int now = Platform_GetVBlankCount();

			if ((now - lastToggleVBlank) >= DBG_FULLSCREEN_COOLDOWN_VBLANKS)
			{
				lastToggleVBlank = now;

				// Requested, not done: the swap happens in the event loop, so
				// asking what state we are in here would still report the old one.
				Platform_Log("[CTR Debug] fullscreen: %s requested\n", Platform_IsFullscreen() ? "off" : "on");
				Platform_ToggleFullscreen();
			}
		}
		else if (row == DBG_VIDEO_ASPECT)
		{
			// Cycles from wherever it stands rather than from a remembered index:
			// the flag or the startup detection can have set it before the menu was
			// ever opened, and an index that did not know about that would jump
			// somewhere else on the first press.
			//
			// A -1 - an aspect that is none of the three, which --aspect can set -
			// cycles to row 0, because (-1 + 1) is 0. That is the wanted answer and
			// not an accident of the arithmetic: the first press off a custom shape
			// lands on the reference one.
			const int next = (CTR_View_ActiveMode() + 1) % CTR_View_ModeCount();
			int nextW = 0;
			int nextH = 0;

			CTR_View_ModeAspect(next, &nextW, &nextH);
			Platform_SetAspect(nextW, nextH);
			Platform_Log("[CTR Debug] aspect %s\n", CTR_View_ModeName(next));
		}
		else if (row == DBG_VIDEO_RES_SCALE)
		{
			// Stepping the SETTING, not what is in force. The row above SHOWS what
			// is in force, which is the right thing to show and the wrong thing to
			// add one to: a factor that had been clamped down would read back
			// clamped, gain one, and be stored - walking the setting down a step at
			// a time with nobody having asked for it.
			//
			// NATIVE sits one step past the last factor and the wrap goes home
			// from there, so the order is x1..x8, NATIVE, x1. Neither bound is
			// written down here: the ceiling and the position are both asked
			// for, because a menu that carried its own copy of the maximum is
			// how the wrap and the ceiling came apart once already.
			int scale;

			if (Platform_ResolutionIsNative())
			{
				scale = 1;
			}
			else
			{
				scale = Platform_GetResolutionScaleSetting() + 1;

				if (scale > Platform_GetResolutionScaleMax())
				{
					scale = Platform_GetResolutionNativePosition();
				}
			}

			Platform_SetResolutionScale(scale);
			Platform_SettingsSave();

			if (Platform_ResolutionIsNative())
			{
				Platform_Log("[CTR Debug] internal resolution NATIVE\n");
			}
			else
			{
				Platform_Log("[CTR Debug] internal resolution x%d\n", Platform_GetResolutionScale());
			}
		}
		else if (row == DBG_VIDEO_RES_RESET)
		{
			// The one grip back. A factor that can only be climbed out of by
			// pressing through seven more of them is not a switch.
			Platform_SetResolutionScale(1);
			Platform_SettingsSave();
			Platform_Log("[CTR Debug] internal resolution x1\n");
		}
		else if (row == DBG_VIDEO_PAGE_SCALE)
		{
			// Climbs and wraps, like INT RES and for the same reason: this menu
			// has no left and right. Doubling rather than stepping, because the
			// atlas quadruples with each one and 1, 2, 4 is the whole range.
			int scale = Platform_GetPageScale() * 2;

			if (scale > DBG_PAGE_SCALE_MAX)
			{
				scale = 1;
			}

			Platform_SetPageScale(scale);
		}
		else if (row == DBG_VIDEO_PAGE_ROWS)
		{
			Platform_SetPageRowRange(!Platform_GetPageRowRange());
		}
		else if (row == DBG_VIDEO_PAGE_PRELOAD)
		{
			Platform_SetPagePreload(!Platform_GetPagePreload());
		}
		else if (row == DBG_VIDEO_DITHER)
		{
			// Climbs and wraps, like INT RES and PAGE SCALE and for the same
			// reason: this menu has no left and right. Where it wraps is asked
			// of the renderer, beside the clamp, and not written down again
			// here.
			Platform_StepDither();
			Platform_SettingsSave();
			Platform_Log("[CTR Debug] dither %s\n", Platform_DitherName());
		}
		else if (row == DBG_VIDEO_MSAA)
		{
			// Climbs and wraps like DITHER: OFF, 2x, 4x, OFF. Stepped from what is
			// asked for, not from what is in force, so a 4x the device turned
			// into 2x does not keep a press at 2x.
			//
			// Asked for, not done - the switch waits for the next frame boundary
			// (NativeRenderer_ApplyMsaa), so the log says "requested", like
			// FULLSCREEN. And a cooldown like FULLSCREEN's: every step rebuilds the
			// multisampled image and waits for the GPU, and a held X would do that
			// every DBG_NAV_REPEAT_VBLANKS.
			local_persist int lastStepVBlank = -DBG_FULLSCREEN_COOLDOWN_VBLANKS;
			const int now = Platform_GetVBlankCount();

			if ((now - lastStepVBlank) >= DBG_FULLSCREEN_COOLDOWN_VBLANKS)
			{
				lastStepVBlank = now;
				Platform_StepMsaa();
				Platform_SettingsSave();
				Platform_Log("[CTR Debug] anti-aliasing %s requested\n", Platform_MsaaName(Platform_GetMsaaRequested()));
			}
		}
		else if (row == DBG_VIDEO_LOD_NONE)
		{
			// The mode, not a stage. Toggling it does not touch the mask, so a
			// page that had one stage turned on for a measurement still has it
			// when the mode goes off again.
			CTR_Lod_SetNoneMode(!CTR_Lod_NoneMode());
			Platform_SettingsSave();
			Platform_Log("[CTR Debug] NO LOD %s\n", CTR_Lod_NoneMode() ? "on - the mechanism is out" : "off - the stages decide for themselves");
		}
		else if ((row >= DBG_VIDEO_LOD_FIRST) && (row <= DBG_VIDEO_LOD_LAST))
		{
			const int stage = row - DBG_VIDEO_LOD_FIRST;

			CTR_Lod_ToggleStage(stage);
			Platform_SettingsSave();
			Platform_Log("[CTR Debug] %s %s\n", CTR_Lod_StageLabel(stage), CTR_Lod_StageForced(stage) ? "highest stage" : "stock");
		}
		break;

	case DBG_DRIVER:
		if ((row >= 0) && (row < DBG_COUNT(s_drivers)))
		{
			s_driver = row;
			Platform_Log("[CTR Debug] driver '%s' (%d) chosen\n", s_drivers[row].name, s_drivers[row].id);
			s_page = DBG_LEVEL;
		}
		break;

	case DBG_TRACKS:
	{
		extern int g_cfg_tracks;

		// The row that says the switch is off does nothing, on purpose. A row
		// that told you what to start with and then also acted would be two
		// different rows in one place.
		if (!g_cfg_tracks)
		{
			break;
		}

		if (row == DBG_TRACKS_RESCAN_ROW)
		{
			// Reading the folder again, from scratch. Files come and go while
			// the game runs, and a list that could only be built once would be
			// a list that is wrong by the time anybody looks at it.
			const int found = NativeTrack_Scan();

			Platform_Log("[CTR Debug] tracks rescanned: %d file(s)\n", found);

			// The loaded container may not be in the list any more, and even if
			// a file with that name is, it need not be the same file. The
			// cursor goes back to the top for the same reason.
			NativeTrack_Release();
			s_cursor[DBG_TRACKS] = 0;
			s_scroll[DBG_TRACKS] = 0;
			break;
		}

		{
			const int index = row - DBG_TRACKS_FIRST_ROW;

			if (NativeTrack_Get(index) != NULL)
			{
				s_trackSelected = index;
				s_cursor[DBG_TRACK] = 0;
				s_scroll[DBG_TRACK] = 0;
				s_page = DBG_TRACK;
				DebugMenu_LogTrack(NativeTrack_Get(index));
			}
		}
		break;
	}

	case DBG_TRACK:
	{
		const struct NativeTrackEntry *entry = NativeTrack_Get(s_trackSelected);

		if (entry == NULL)
		{
			break;
		}

		if (row == DBG_TRACK_DONOR)
		{
			// Climbs and wraps, like every other row on a menu that has no left
			// and no right.
			s_trackDonor = (s_trackDonor + 1) % DBG_COUNT(s_levels);
			Platform_Log("[CTR Debug] donor slot for '%s' is now %s (%d)\n", entry->name, s_levels[s_trackDonor].name, s_levels[s_trackDonor].id);
			break;
		}

		if (row == DBG_TRACK_CRYSTAL)
		{
			// MEASURING INSTRUMENT, see s_crystalProbe. The same switch that
			// LEVEL SELECT shows - it hangs on the debug jump, not on a
			// page, and therefore also applies to --level.
			DebugMenu_ToggleCrystalProbe();
			break;
		}

		if (row != DBG_TRACK_LOAD)
		{
			// Every other row reports. The VIDEO page has rows like that too.
			break;
		}

		// A container the list already threw out is not loaded, and the reason
		// is said again here rather than left standing on a page nobody is
		// looking at any more. Since format 4.1 the same offer rule as the menu:
		// only a container that offers a race (offer rule of container format 4.1).
		{
			// With the crystal probe by the rule for Crystal, like NITRO-PIT
			// -> CRYSTAL - otherwise no crystal container would get in.
			const char *whyNot = NativeTrack_WhyNotOffered(s_trackSelected, s_crystalProbe ? NATIVE_TRACK_MODE_CRYSTAL : NATIVE_TRACK_MODE_RACE, NULL);

			if (whyNot != NULL)
			{
				Platform_Log("[CTR Debug] '%s' NOT loaded: %s\n", entry->file, whyNot);
				break;
			}
		}

		// Checked a second time in here, against the file as it is now. See
		// NativeTrack_Load: between listing and loading lie any number of
		// minutes.
		if (!NativeTrack_Load(s_trackSelected, s_levels[s_trackDonor].id))
		{
			break;
		}

		Platform_Log("[CTR Debug] '%s' by %s -> slot %s (%d), driver %s\n", entry->name,
		             (entry->author[0] != '\0') ? entry->author : "(not set)", s_levels[s_trackDonor].name,
		             s_levels[s_trackDonor].id, s_drivers[s_driver].name);

		if (s_crystalProbe)
		{
			MM_NativeCrystal_MarkDebug();
		}

		DebugMenu_JumpToLevel(gGT, s_levels[s_trackDonor].id, s_drivers[s_driver].id);
		s_debugMenuOpen = 0;
		break;
	}
	}
}


// ---------------------------------------------------------------------------
//  --menu-keys: a key sequence through the REAL input path.
//
//  WHY AT ALL. The cascade behind ARCADE - race type, player count,
//  difficulty - is only reachable through key presses. A screen that
//  no run can open is a screen about which no report can prove
//  anything: an earlier measurement had to compute half its geometry
//  instead of measuring it, because no screenshot came about.
//
//  WHY NOT THROUGH THE WINDOW. Sending keys into the window from outside
//  depends on the foreground, the keyboard layout and on which seat the
//  keyboard currently sits - F4 rotates it, and if a pad is on slot
//  0, it starts at slot 1, where the main menu does not read it at all
//  (RECTMENU_ProcessInput only reads P1). Measured: the same command gave three
//  different seats in three runs.
//
//  WHAT HAPPENS HERE INSTEAD. The sequence is installed as PSX pad bytes on slot 0,
//  through the same door that replay and quick state use
//  (Platform_InputInstallPadSnapshots). After that the full path runs:
//  WritePadBus -> GAMEPAD.c -> buttonTapPerPlayer -> RECTMENU_ProcessInput.
//  No shortcut past a gate - whoever gets into a menu with this sequence
//  gets there by hand as well.
//
//  The base is captured ONCE, before anything is installed, and
//  after that only its button word is replaced. That keeps everything else on slot 0 -
//  ID, state, analog values - as the host sees it.
// ---------------------------------------------------------------------------

// 256, not the earlier 64. With 64 steps a kart can be driven
// for about three seconds - too little to reach a particular spot on the track.
// The sequence costs two bytes per step.
#define DBG_MENU_KEYS_MAX 256

int g_cfg_menuKeysCount = 0;
u16 g_cfg_menuKeys[DBG_MENU_KEYS_MAX];
int g_cfg_menuKeysFrom = 300;
int g_cfg_menuKeysEvery = 24;
int g_cfg_menuKeysHold = 3;
int g_cfg_menuKeysQuit = 120;

// HOW MANY PADS THE TEST BENCH SIMULATES. Default 1.
//
// The multiplayer paths cannot be measured otherwise: MM_ToggleRows_PlayerCount
// locks 2P, 3P and 4P as long as not that many pads are PLUGGED IN
// (MainFrame_HaveAllPads, MainFrame.c:574), and a locked entry does not
// accept Cross. Once a controller switched itself off after idling in the middle
// of a measurement series - the same run went through before and not afterwards.
// What the test bench measures must not depend on what happens to be lying on
// the desk.
int g_cfg_menuPads = 1;

internal void DebugMenu_MenuKeysTick(void)
{
	local_persist struct PlatformInputPadSnapshot base[4];
	local_persist int haveBase = 0;
	local_persist int armed = 0;
	local_persist int lastStep = -1;

	struct PlatformInputPadSnapshot pads[4];
	int now;
	int step;
	int phase;
	u16 word;

	if (g_cfg_menuKeysCount <= 0)
	{
		return;
	}

	now = Platform_GetVBlankCount();

	if (now < g_cfg_menuKeysFrom)
	{
		return;
	}

	step = (now - g_cfg_menuKeysFrom) / g_cfg_menuKeysEvery;

	// A MENU RUN DOES NOT END ON ITS OWN OTHERWISE.
	//
	// --exit-after-frames counts RACE FRAMES (further down, the condition
	// is MAIN_MENU off). In the menu it never counts. Until now a menu run
	// still got out, because the demo counter ran out and the title screen
	// went into the demo by itself - but EVERY key press resets the counter
	// (MM_MenuFlow.c:110). So a key sequence holds the run in place,
	// and it has to be killed: no Platform_Shutdown, no
	// final reports, no closed log.
	//
	// That is why the sequence ends through the same door as Ctrl+Q and
	// --exit-after-frames, as soon as it is done and the screenshot has been
	// written. Whoever wants to keep the run open appends "none".
	if (step >= g_cfg_menuKeysCount)
	{
		if (armed)
		{
			armed = 0;
			Platform_InputClearInstalledPadSnapshots();
			Platform_Log("[CTR Debug] --menu-keys: sequence finished at vblank %d, input handed back\n", now);
		}

		// Run-out, so that --shot still takes effect and the picture is settled.
		if ((g_cfg_menuKeysQuit != 0) && (now >= (g_cfg_menuKeysFrom + (g_cfg_menuKeysCount * g_cfg_menuKeysEvery) + g_cfg_menuKeysQuit)))
		{
			Platform_Log("[CTR Debug] --menu-keys: run-out over, exiting at vblank %d\n", now);
			Platform_QuitGame("--menu-keys");
		}

		return;
	}

	if (!haveBase)
	{
		if (Platform_InputCapturePadSnapshots(base, 4) <= 0)
		{
			return;
		}

		haveBase = 1;
	}

	int slot;

	phase = (now - g_cfg_menuKeysFrom) % g_cfg_menuKeysEvery;

	// Active-low: 0xffff means "nothing pressed". Only the first handful of
	// frames of a step is held, the rest is release - a tap
	// comes from the EDGE, and without a release there is no second one.
	word = (phase < g_cfg_menuKeysHold) ? g_cfg_menuKeys[step] : (u16)0xffff;

	memcpy(pads, base, sizeof(pads));

	// ALL FOUR SLOTS, not just the first. The menu reads P1 (RECTMENU_ProcessInput),
	// but character select in VS waits for EVERY connected player: driven with only
	// slot 1, a run stopped at two open frames,
	// and so the mode could not be verified up to the loading screen. Where a slot
	// has no pad, the word goes nowhere.
	for (slot = 0; slot < 4; slot++)
	{
		pads[slot].buttons[0] = (u8)(word & 0xff);
		pads[slot].buttons[1] = (u8)((word >> 8) & 0xff);

		// 0x41 is the ID of the digital pad, 0 the status "present"
		// (native_input.c:15 and :367). A slot beyond the default stays
		// as the capture found it.
		if (slot < g_cfg_menuPads)
		{
			pads[slot].connected = 1;
			pads[slot].status = 0;
			pads[slot].id = 0x41;
		}
	}

	Platform_InputInstallPadSnapshots(pads, 4);
	armed = 1;

	if (step != lastStep)
	{
		lastStep = step;
		Platform_Log("[CTR Debug] --menu-keys: step %d of %d, word 0x%04x, vblank %d\n", step + 1, g_cfg_menuKeysCount, (unsigned)g_cfg_menuKeys[step],
		             now);
	}
}

void DebugMenu_Frame(struct GameTracker *gGT, struct GamepadSystem *gGamepads)
{
	// The key sequence first: it must have set the pad bus before anything
	// in this frame reads it.
	DebugMenu_MenuKeysTick();

	// Big enough for the longest line that CAN come about, not for the
	// longest one wants to see. By the container format a track name may be
	// 64 bytes long, a description 512 - and the cut to the
	// box width only happens afterwards, in DebugMenu_FitLine. Before, 48 bytes
	// stood here and the cut was in the format strings; whoever had forgotten one of them
	// would have written past the end.
	char line[640];

	// A sign of life, as long as a container track is in play.
	//
	// Without it, "the log stops after loading" cannot be interpreted: the game
	// may have died in the first frame or in the ninetieth. This function
	// runs in every frame of the game logic, so its last line is the last
	// line the game lived.
	//
	// Every 30 VBlanks, so that it is a half-second pulse and not a flood.
	{
		extern int g_cfg_tracks;
		local_persist int lastBeat = -1000;

		if (g_cfg_tracks && (NativeTrack_LoadedIndex() >= 0))
		{
			const int now = Platform_GetVBlankCount();

			if ((now - lastBeat) >= 30)
			{
				lastBeat = now;
				Platform_Log("[CTR Tracks] alive at vblank %d\n", now);
			}
		}
	}

	// Two one-shot lines that say which link of the chain is alive. "hook alive"
	// proves this function runs at all and since when; "first buttons" proves
	// decoded input reaches a pad here. A chord that opens no menu is a
	// different fault depending on which of them is missing, and without them it
	// looks the same either way.
	{
		local_persist int saidAlive = 0;
		local_persist int saidButtons = 0;
		int pad;

		if (!saidAlive)
		{
			extern int g_cfg_dev;

			saidAlive = 1;

			if (g_cfg_dev)
			{
				Platform_Log("[CTR Debug] menu hook alive at vblank %d - SELECT+START opens it (keyboard: SPACE+ENTER)\n", Platform_GetVBlankCount());
			}
			else
			{
				Platform_Log("[CTR Debug] menu hook alive at vblank %d - the debug menu needs --dev\n", Platform_GetVBlankCount());
			}
		}

		for (pad = 0; (pad < DBG_PAD_COUNT) && !saidButtons; pad++)
		{
			if (gGamepads->gamepad[pad].buttonsHeldCurrFrame != 0)
			{
				saidButtons = 1;
				Platform_Log("[CTR Debug] first buttons 0x%x on pad %d at vblank %d\n", gGamepads->gamepad[pad].buttonsHeldCurrFrame, pad,
				             Platform_GetVBlankCount());
			}
		}
	}

	// THE CONTAINER'S RELEASE AT THE TITLE SCREEN, moved here.
	//
	// Earlier, game/230/MM_MenuFlow.c called MM_NativeTracks_Disarm on every
	// confirmed main menu row and in MM_JumpTo_Title_Returning. Now
	// MM_MenuFlow.c is word for word the original and knows no
	// containers. Without a release, a container that TRACKS -> LOAD or
	// --autoload-track loaded would stay active after the race, and every later
	// race on its donor slot would keep driving the custom track.
	//
	// The release happens on the EDGE from the race into the title screen, not while
	// the title screen stands: --autoload-track loads the container on the title screen
	// and only starts it afterwards.
	{
		local_persist int wasInTitle = 1;
		const int inTitle = ((gGT->gameMode1 & MAIN_MENU) != 0);

		if (inTitle && !wasInTitle)
		{
			MM_NativeTracks_Disarm();
		}

		wasInTitle = inTitle;
	}

	// WHAT ARRIVES IN THE RACE, one line per race.
	//
	// The menu says what it has set ([CTR Menu] CUSTOM start). Whether it also
	// arrives only the race can say: laps, driver and player count can be
	// overwritten by a load path (DebugMenu_JumpToLevel does it). The
	// moment is the same as for --exit-after-frames: main menu off, no
	// load pending.
	{
		local_persist int wasInRace = 0;
		const int inRace = ((gGT->gameMode1 & MAIN_MENU) == 0) && (sdata->load_inProgress == 0);

		if (inRace && !wasInRace)
		{
			const int container = (NativeTrack_LoadedIndex() >= 0) && NativeTrack_ActiveForLevel(gGT->levelID);

			// "bots" = this many opponents BOTS_Driver_Init really created; without
			// a nav path it creates none (BOTS.c:3112-3135), even in an arcade race.
			Platform_Log("[CTR Race] level %d%s%s%s, driver %d, difficulty %d, laps %d, players %d, bots %d, gameMode1 0x%08x, cheats %s at vblank %d\n",
			             (int)gGT->levelID, container ? " container '" : "", container ? NativeTrack_LoadedName() : "", container ? "'" : "",
			             (int)data.characterIDs[0], (int)gGT->arcadeDifficulty, (int)gGT->numLaps, (int)gGT->numPlyrCurrGame,
			             (int)gGT->numBotsNextGame, (unsigned)gGT->gameMode1, NativeMenuCheats_Describe(), Platform_GetVBlankCount());
		}

		// --autopilot: the clock of the player seat. A bot never goes through
		// VehPhysProc (:175-178), which otherwise sets timeElapsedInRace every frame. Up
		// to the finish the same happens here; at the finish MainGameEnd freezes the clock.
		{
			extern int g_cfg_autopilot;
			struct Driver *seat = gGT->drivers[0];

			if (g_cfg_autopilot && inRace && (seat != NULL) && ((seat->actionsFlagSet & ACTION_BOT) != 0) &&
			    ((seat->actionsFlagSet & (ACTION_RACE_TIMER_FROZEN | ACTION_RACE_FINISHED)) == 0))
			{
				seat->timeElapsedInRace = gGT->elapsedEventTime;
			}
		}

		wasInRace = inRace;
	}

	// --level N, once, without anyone touching a button.
	//
	// The trigger is the game's own state, not a count of frames: the main menu
	// is up and a menu is on screen, which is precisely the situation in which
	// LEVEL SELECT below would work. A frame count would be a different moment
	// on a slower machine, and this exists to make two runs the same run.
	//
	// It goes through DebugMenu_JumpToLevel rather than beside it, so the flag
	// and the menu row set a track up in one way instead of two.
	{
		extern int g_cfg_jumpLevel;
		extern int g_cfg_jumpDriver;
		extern char g_cfg_autoloadTrack[128];
		extern int g_cfg_exitAfterFrames;

		// --autoload-track FILE, once, at the same moment as --level: the main
		// menu is up and a menu is on screen. The container goes through
		// MM_NativeTracks_LoadRow - the NITRO-PIT row, verbatim - and the seat
		// it was given is then started through DebugMenu_JumpToLevel, which is
		// what --level and the TRACK page of this menu do. Nothing here chooses
		// a seat or loads a chunk itself.
		//
		// The name is cleared FIRST, so a container that fails to load fails
		// once and is not tried again every frame.
		if ((g_cfg_autoloadTrack[0] != '\0') && ((gGT->gameMode1 & MAIN_MENU) != 0) && (sdata->ptrActiveMenu != NULL))
		{
			char wanted[128];
			int found = -1;
			int i;

			snprintf(wanted, sizeof(wanted), "%s", g_cfg_autoloadTrack);
			g_cfg_autoloadTrack[0] = '\0';

			for (i = 0; i < NativeTrack_Count(); i++)
			{
				const struct NativeTrackEntry *entry = NativeTrack_Get(i);

				if ((entry != NULL) && (strcmp(entry->file, wanted) == 0))
				{
					found = i;
					break;
				}
			}

			if (found < 0)
			{
				Platform_Log("[CTR Debug] --autoload-track: '%s' is not in tracks/ (%d container(s) listed) - nothing loaded\n", wanted,
				             NativeTrack_Count());
			}
			else if (NativeTrack_WhyNoRace(found, NULL) != NULL)
			{
				// The same rule as in the menu (format 4.1): rejected, without
				// a level ID or without Race - the reason in one word.
				Platform_Log("[CTR Debug] --autoload-track: '%s' is not offered - %s - nothing loaded\n", wanted, NativeTrack_WhyNoRace(found, NULL));
			}
			else if (!MM_NativeTracks_LoadRow(found))
			{
				Platform_Log("[CTR Debug] --autoload-track: '%s' did not load - see the [CTR Tracks] line above\n", wanted);
			}
			else
			{
				// The row's runtime ID, no longer the slot: the jump
				// goes through the same funnel as the menu
				// (MainRaceTrack_RequestLoad -> MM_NativeTracks_TranslateLevel),
				// and only there does it become the donor slot.
				const int level = NativeTrack_LevelForIndex(found);
				extern int g_cfg_autoloadDemo;

				if (g_cfg_autoloadDemo)
				{
					// Before the load: MainInit converts every driver to a bot when
					// boolDemoMode is set (MainInit.c, BOTS_Driver_Convert). The
					// countdown that sends the title demo home is set out of reach.
					gGT->boolDemoMode = 1;
					gGT->demoCountdownTimer = 0xffffffffu;
					Platform_Log("[CTR Debug] --autoload-demo: every seat drives as a bot, the player's too; HUD off, demo camera, no time limit - a MODEL of driving, not a race\n");
				}

				Platform_Log("[CTR Debug] --autoload-track '%s' jumps to level id %d (seat %d) at vblank %d\n", wanted, level,
				             NativeTrack_LoadedDonorLevel(), Platform_GetVBlankCount());
				DebugMenu_JumpToLevel(gGT, level, g_cfg_jumpDriver);
				return;
			}

			// Only the failure branches come here. A preview recording then ends
			// right away with the reason, instead of waiting 60 s in the menu.
			{
				extern int g_cfg_recordPreview;
				void NativePreview_RecordFail(const char *why);
				char why[200];

				if (g_cfg_recordPreview)
				{
					snprintf(why, sizeof(why), "--autoload-track: '%s' was not loaded - see the [CTR Debug] line above", wanted);
					NativePreview_RecordFail(why);
				}
			}
		}

		// --exit-after-frames N: count game frames once the race is up - main
		// menu off and no load in progress - and leave through the same door
		// as Ctrl+Q. The count starts at the race, not at boot, so it means the
		// same thing on a fast machine and a slow one.
		if ((g_cfg_exitAfterFrames >= 0) && ((gGT->gameMode1 & MAIN_MENU) == 0) && (sdata->load_inProgress == 0))
		{
			local_persist int raceFrames = 0;

			raceFrames++;

			if (raceFrames >= g_cfg_exitAfterFrames)
			{
				Platform_Log("[CTR Debug] --exit-after-frames: %d race frame(s) drawn on level %d at vblank %d - leaving\n", raceFrames,
				             gGT->levelID, Platform_GetVBlankCount());
				Platform_QuitGame("--exit-after-frames");
			}
		}

		if ((g_cfg_jumpLevel >= 0) && ((gGT->gameMode1 & MAIN_MENU) != 0) && (sdata->ptrActiveMenu != NULL))
		{
			const int levelID = g_cfg_jumpLevel;
			extern int g_cfg_autoloadDemo;

			g_cfg_jumpLevel = -1;

			// Format 4.1: a runtime ID keeps the same offer rule as
			// the menu - otherwise the funnel would silently fall back to the bare
			// donor slot.
			if ((levelID >= NATIVE_TRACK_LEVELID_FIRST) && (levelID < (NATIVE_TRACK_LEVELID_FIRST + NATIVE_TRACK_LEVELID_COUNT)))
			{
				const char *whyNot = NativeTrack_WhyNoRace(NativeTrack_IndexForLevel(levelID), NULL);

				if (whyNot != NULL)
				{
					Platform_Log("[CTR Debug] --level %d is not offered - %s - nothing started\n", levelID, whyNot);
					return;
				}
			}

			// --autoload-demo used to apply only to containers
			// (the branch above), so --level never drove. Seeing a disc track
			// in motion was not possible at all - and a still at the start
			// does not show a picture error that comes about while driving. The same
			// two lines as above, at the same place before the jump.
			if (g_cfg_autoloadDemo)
			{
				gGT->boolDemoMode = 1;
				gGT->demoCountdownTimer = 0xffffffffu;
				Platform_Log("[CTR Debug] --autoload-demo on --level %d: every seat drives as a bot\n", levelID);
			}

			Platform_Log("[CTR Debug] --level jumps to %d at vblank %d\n", levelID, Platform_GetVBlankCount());
			DebugMenu_JumpToLevel(gGT, levelID, g_cfg_jumpDriver);
			return;
		}
	}

	// The two halves of the chord, each said once, on whichever seat first
	// carries them.
	//
	// A chord that opens nothing is three different faults - the seat sends
	// neither word, it sends one and not the other, or it sends both but never
	// in the same frame - and the line above cannot tell them apart. These can.
	// The keyboard path and the controller path set the same two raw bits, so
	// where they diverge is upstream of anything this file can see, and the only
	// way to find out is to have the game say what arrived.
	{
		local_persist int saidSelect = 0;
		local_persist int saidStart = 0;
		local_persist int saidBoth = 0;
		int pad;

		for (pad = 0; pad < DBG_PAD_COUNT; pad++)
		{
			const int held = gGamepads->gamepad[pad].buttonsHeldCurrFrame;

			if (!saidSelect && ((held & BTN_SELECT) != 0))
			{
				saidSelect = 1;
				Platform_Log("[CTR Debug] SELECT seen on pad %d (held 0x%x)\n", pad, held);
			}
			if (!saidStart && ((held & BTN_START) != 0))
			{
				saidStart = 1;
				Platform_Log("[CTR Debug] START seen on pad %d (held 0x%x)\n", pad, held);
			}
			if (!saidBoth && ((held & BTN_SELECT) != 0) && ((held & BTN_START) != 0))
			{
				saidBoth = 1;
				Platform_Log("[CTR Debug] SELECT+START together on pad %d (held 0x%x)\n", pad, held);
			}
		}
	}

	// Not while loading: the decal buffers this draws into are in flight.
	if ((gGT->gameMode1 & LOADING) != 0)
	{
		return;
	}

	// ONLY WITH --dev. The debug menu jumps to any level, reloads the track
	// folder and reaches the developer tools, so a normal game never opens it:
	// without --dev the chord is not even looked at, and SELECT+START reaches
	// the game untouched. s_debugMenuOpen is set nowhere else, so everything
	// below the chord stays shut as well.
	{
		extern int g_cfg_dev;

		if (!g_cfg_dev)
		{
			return;
		}
	}

	// Every pad, not pad 0: the keyboard feeds one input slot and a controller
	// lands wherever it connected, so a menu that only hears one seat reads as
	// "does not open" to everyone sitting elsewhere.
	//
	// Edge-latched rather than tap-based, because the wipe further down zeroes
	// buttonsHeldCurrFrame and thereby destroys the game's own edge detection:
	// with the previous frame's held state wiped, a button still physically down
	// reads as newly tapped every frame. The latch only ever sees the fresh
	// per-frame decode, which the wipe cannot reach.
	{
		local_persist int s_chordLatch = 0;
		int chordDown = 0;
		int pad;
		int openedBy = -1;

		for (pad = 0; pad < DBG_PAD_COUNT; pad++)
		{
			const int held = gGamepads->gamepad[pad].buttonsHeldCurrFrame;
			int bits = 0;
			int scan = held;

			while (scan != 0)
			{
				bits += (scan & 1);
				scan >>= 1;
			}

			if (bits > DBG_MAX_PLAUSIBLE_BUTTONS)
			{
				continue;
			}

			if (((held & BTN_SELECT) != 0) && ((held & BTN_START) != 0))
			{
				chordDown = 1;
				openedBy = pad;
				break;
			}
		}

		if (chordDown && !s_chordLatch)
		{
			s_debugMenuOpen = !s_debugMenuOpen;
			Platform_Log("[CTR Debug] menu %s by pad %d\n", s_debugMenuOpen ? "opened" : "closed", openedBy);
		}

		s_chordLatch = chordDown;

		if (chordDown)
		{
			// The frame that toggles must not also pause or navigate.
			for (pad = 0; pad < DBG_PAD_COUNT; pad++)
			{
				gGamepads->gamepad[pad].buttonsTapped = 0;
				gGamepads->gamepad[pad].buttonsHeldCurrFrame = 0;
			}
		}
	}

	if (!s_debugMenuOpen)
	{
		return;
	}

	// Held-state navigation with a repeat gate, for the same reason the chord is
	// latched.
	{
		local_persist int s_lastNavVBlank = 0;
		const int now = Platform_GetVBlankCount();

		if ((now - s_lastNavVBlank) >= DBG_NAV_REPEAT_VBLANKS)
		{
			const int rows = DebugMenu_RowCount(s_page);
			int held = 0;
			int pad;

			for (pad = 0; pad < DBG_PAD_COUNT; pad++)
			{
				held |= gGamepads->gamepad[pad].buttonsHeldCurrFrame;
			}

			if (((held & BTN_UP) != 0) && (rows > 0))
			{
				s_cursor[s_page] = (s_cursor[s_page] + rows - 1) % rows;
				s_lastNavVBlank = now;
			}
			if (((held & BTN_DOWN) != 0) && (rows > 0))
			{
				s_cursor[s_page] = (s_cursor[s_page] + 1) % rows;
				s_lastNavVBlank = now;
			}
			if ((held & (BTN_CROSS | BTN_RIGHT)) != 0)
			{
				DebugMenu_Activate(gGT);
				s_lastNavVBlank = now;
			}

			// The only page with a value that goes both ways. Left is taken - it
			// backs out of a page - so the down step is on square, and it exists
			// only here rather than becoming a second meaning for a button on every
			// other page.
			if (((held & BTN_SQUARE) != 0) && (s_page == DBG_VIEW))
			{
				DebugMenu_StepViewSetting(-1);
				s_lastNavVBlank = now;
			}
			if (((held & (BTN_CIRCLE | BTN_LEFT)) != 0) && (s_page != DBG_ROOT))
			{
				// To the page that opened this one, not always to the root: the
				// driver list belongs to LEVEL SELECT, and backing out of it into
				// the root would lose the place in a 25-row list.
				s_page = DebugMenu_ParentPage(s_page);
				s_lastNavVBlank = now;
			}

			// The cursor drags the window rather than the window chasing the
			// cursor: keeping it in view by moving the smallest amount is what
			// makes a wrapped selection land at the far end of the list instead
			// of scrolling all the way there.
			{
				const int visible = DBG_VISIBLE_ROWS;
				const int pageRows = DebugMenu_RowCount(s_page);
				int top = s_scroll[s_page];

				if (s_cursor[s_page] < top) { top = s_cursor[s_page]; }
				if (s_cursor[s_page] >= (top + visible)) { top = s_cursor[s_page] - visible + 1; }
				if (top > (pageRows - visible)) { top = pageRows - visible; }
				if (top < 0) { top = 0; }
				s_scroll[s_page] = top;
			}
		}
	}

	// The game under the menu is deaf while it is open. Every seat, and every
	// word the frame still has to read.
	//
	// All FOUR button words, not three: zeroing the current frame while the
	// previous one still holds a press makes the game see a release it was never
	// given, and a release is an edge that menus act on. With all four at zero
	// there is no press, no hold, no release and no edge.
	//
	// And the sticks, which is the half that button words do not cover. They are
	// filled in the VSync callback, before this runs, and nothing reads the host
	// again for the rest of the frame - so clearing them here holds. Without it
	// the menu opens over a race and the kart keeps steering underneath it.
	//
	// The menu itself has already read what it needed, above.
	{
		int pad;

		for (pad = 0; pad < DBG_PAD_COUNT; pad++)
		{
			struct GamepadBuffer *seat = &gGamepads->gamepad[pad];

			seat->buttonsTapped = 0;
			seat->buttonsHeldCurrFrame = 0;
			seat->buttonsReleased = 0;
			seat->buttonsHeldPrevFrame = 0;

			seat->stickLX = 0;
			seat->stickLY = 0;
			seat->stickLX_dontUse1 = 0;
			seat->stickLY_dontUse1 = 0;
			seat->stickRX = 0;
			seat->stickRY = 0;
		}
	}

	// What the menus read is copied out of those same words later in the frame
	// (RECTMENU_CollectInput, called from MainFrame_RenderFrame), so the copy is
	// zero too and no menu under this one answers a button either.

	// Text first, panel after, and both into the same ordering table. In this
	// table a primitive added later is drawn deeper, so the panel goes last to
	// sit behind the text. And they must share a table, or the two end up
	// anchored separately and the text walks out of its own box at a wide ratio.
	{
		const int textX = NATIVE_DEBUG_MENU_PAD;
		const int rows = DebugMenu_RowCount(s_page);
		const int top = s_scroll[s_page];
		int y = DBG_FIRST_ROW_Y;
		int i;

		DecalFont_DrawLine((char *)DebugMenu_PageTitle(s_page), (s16)textX, (s16)(NATIVE_DEBUG_MENU_PAD + 2), FONT_SMALL, WHITE);

		for (i = top; (i < rows) && (i < (top + DBG_VISIBLE_ROWS)); i++)
		{
			const int selected = (i == s_cursor[s_page]);

			switch (s_page)
			{
			case DBG_ROOT:
				// No chevron. The reference font maps both '>' and '-' to the
				// same icon, so a chevron comes out as a dash. The footer says
				// which button opens a row instead.
				sprintf(line, "%s", s_rootNames[i]);
				break;

			case DBG_LEVEL:
				if (i == DBG_LEVEL_DRIVER_ROW)
				{
					sprintf(line, "DRIVER: %s", s_drivers[s_driver].name);
				}
				else if (i == DBG_LEVEL_CRYSTAL_ROW)
				{
					// MEASURING INSTRUMENT, see s_crystalProbe. Word for word as on the
					// TRACK page, because it is the same value.
					sprintf(line, "%-9s%s", "CRYSTAL", DebugMenu_CrystalProbeText());
				}
				else
				{
					sprintf(line, "%-14s%2d", s_levels[i - DBG_LEVEL_FIRST_LEVEL_ROW].name, s_levels[i - DBG_LEVEL_FIRST_LEVEL_ROW].id);
				}
				break;

			case DBG_VIEW:
				if (i == DBG_VIEW_RESET_ROW)
				{
					sprintf(line, "%s", "RESET SET");
				}
				else
				{
					// Share first, raw in brackets. The share says how far this is
					// from what the game shipped with; the raw number is what
					// would be written into the source if a turned value is ever
					// kept, so both belong on the row.
					//
					// All five rows are shares now. Two of them scale a number
					// the render path works out per frame, so their raw is a
					// factor rather than a literal - and 0x100 is one, which is
					// why every row reads 100 % at exactly the same moment.
					const int viewMode = CTR_View_SettingsMode();
					const int rawValue = CTR_View_SettingValue(viewMode, i);

					if (CTR_View_SettingIsOff(viewMode, i))
					{
						// The far end of TRACK DIST is not a distance any more.
						// Printing 985 % there invites a step down expecting a
						// small change, and the step down is the one that turns
						// melting back on.
						sprintf(line, "%-11s%5s  (%d)", CTR_View_SettingLabel(i), "OFF", rawValue);
					}
					else if (CTR_View_SettingIsPercent(i))
					{
						sprintf(line, "%-11s%4d%%  (%d)", CTR_View_SettingLabel(i), CTR_View_SettingPercent(viewMode, i), rawValue);
					}
					else
					{
						sprintf(line, "%-11s       (%d)", CTR_View_SettingLabel(i), rawValue);
					}
				}
				break;

			case DBG_VIDEO:
				if (i == DBG_VIDEO_FULLSCREEN)
				{
					sprintf(line, "%-12s%s", "FULLSCREEN", Platform_IsFullscreen() ? "ON" : "OFF");
				}
				else if (i == DBG_VIDEO_ASPECT)
				{
					int aspectW = 0;
					int aspectH = 0;

					Platform_GetAspect(&aspectW, &aspectH);
					sprintf(line, "%-12s%d:%d", "ASPECT", aspectW, aspectH);
				}
				else if (i == DBG_VIDEO_RES_SCALE)
				{
					// Read back, not remembered: the factor is clamped twice -
					// once against its own ceiling and once against what the
					// target may be allocated at - and a row that printed what
					// it asked for would be reporting a wish.
					//
					// NATIVE is not a factor and must not be shown as one. The
					// effective value carries the position through, so a row
					// that printed x%d here would say x9, which is a number
					// nothing in the frame is multiplied by.
					if (Platform_ResolutionIsNative())
					{
						sprintf(line, "%-12s%s", "INT RES", "NATIVE");
					}
					else
					{
						sprintf(line, "%-12sx%d", "INT RES", Platform_GetResolutionScale());
					}
				}
				else if (i == DBG_VIDEO_RES_RESET)
				{
					sprintf(line, "%s", "  RESET TO x1");
				}
				else if (i == DBG_VIDEO_PAGE_SCALE)
				{
					// Read back like INT RES: the scale is clamped against its
					// own ceiling and again against the largest edge the device
					// will allocate, so what was asked for and what was built
					// are two different numbers.
					sprintf(line, "%-12sx%d", "PAGE RES", Platform_GetPageScale());
				}
				else if (i == DBG_VIDEO_PAGE_TILES)
				{
					// How many of the sixty-four tiles this level has actually
					// asked for, and what the atlas costs at that size. Both
					// read back from the renderer: a row that printed what was
					// requested would be reporting a wish, and the atlas is only
					// built as tall as the tiles in use reach.
					int used = 0;
					int total = 0;
					int kib = 0;

					Platform_GetPageStoreUse(&used, &total, &kib);
					sprintf(line, "%-12s%d/%d  %dK", "TILES", used, total, kib);
				}
				else if (i == DBG_VIDEO_PAGE_ROWS)
				{
					sprintf(line, "%-12s%s", "PAGE ROWS", Platform_GetPageRowRange() ? "ON" : "OFF");
				}
				else if (i == DBG_VIDEO_PAGE_PRELOAD)
				{
					sprintf(line, "%-12s%s", "HOLD ALL", Platform_GetPagePreload() ? "ON" : "OFF");
				}
				else if (i == DBG_VIDEO_DITHER)
				{
					sprintf(line, "%-12s%s", "DITHER", Platform_DitherName());
				}
				else if (i == DBG_VIDEO_MSAA)
				{
					sprintf(line, "%-12s%s", "ANTI-ALIAS", Platform_MsaaName(Platform_GetMsaa()));
				}
				else if (i == DBG_VIDEO_LOD_NONE)
				{
					sprintf(line, "%-12s%s", "NO LOD", CTR_Lod_NoneMode() ? "ON" : "OFF");
				}
				else if ((i >= DBG_VIDEO_LOD_FIRST) && (i <= DBG_VIDEO_LOD_LAST))
				{
					const int stage = i - DBG_VIDEO_LOD_FIRST;
					const int byMode = CTR_Lod_NoneMode() && CTR_Lod_StageCoveredByNoneMode(stage);

					// A stage the mode answers for says so instead of saying
					// HIGH. Otherwise the four rows would read the same whether
					// their own bit or the row above them decided it, and
					// turning one off would look like it did nothing.
					sprintf(line, "%-12s%s", CTR_Lod_StageLabel(stage), byMode ? "-- NO LOD" : (CTR_Lod_StageForced(stage) ? "HIGH" : "STOCK"));
				}
				else if (i == DBG_VIDEO_TARGET)
				{
					int targetW = 0;
					int targetH = 0;

					Platform_GetRenderTargetSize(&targetW, &targetH);
					sprintf(line, "%-12s%dx%d", "TARGET", targetW, targetH);
				}
				else if (i == DBG_VIDEO_WINDOW)
				{
					int windowW = 0;
					int windowH = 0;

					Platform_GetWindowSize(&windowW, &windowH);
					sprintf(line, "%-12s%dx%d", "WINDOW", windowW, windowH);
				}
				else
				{
					// What the game actually renders, asked of the renderer
					// rather than written down as a constant: it changes with the
					// number of players and would otherwise report a lie in split
					// screen.
					int frameW = 0;
					int frameH = 0;

					Platform_GetFrameSize(&frameW, &frameH);
					sprintf(line, "%-12s%dx%d", "FRAME", frameW, frameH);
				}
				break;

			case DBG_DRIVER:
				// A marker on the driver a jump would use.
				sprintf(line, "%-12s%s", s_drivers[i].name, (i == s_driver) ? " <" : "");
				break;

			case DBG_TRACKS:
			{
				extern int g_cfg_tracks;

				if (!g_cfg_tracks)
				{
					sprintf(line, "%s", "OFF - RUN --tracks");
				}
				else if (i == DBG_TRACKS_RESCAN_ROW)
				{
					sprintf(line, "%-12s%d", "RESCAN", NativeTrack_Count());
				}
				else
				{
					const int index = i - DBG_TRACKS_FIRST_ROW;
					const struct NativeTrackEntry *entry = NativeTrack_Get(index);

					if (entry == NULL)
					{
						sprintf(line, "%s", "NO .RLDTRACK FILES");
					}
					else if (!entry->ok)
					{
						// A container that will not open still gets its row. A
						// file that is in the folder and nowhere in the list
						// sends whoever put it there looking in the game.
						sprintf(line, "%-20s%s", entry->file, "BAD");
					}
					else
					{
						sprintf(line, "%-24s%s", entry->name, (NativeTrack_LoadedIndex() == index) ? " <" : "");
					}
				}
				break;
			}

			case DBG_TRACK:
			{
				const struct NativeTrackEntry *entry = NativeTrack_Get(s_trackSelected);

				// Every value is cut to what the panel holds. The whole of
				// it went into the log when this page was opened - see
				// DebugMenu_LogTrack.
				if (entry == NULL)
				{
					sprintf(line, "%s", "GONE - RESCAN");
				}
				else if (i == DBG_TRACK_NAME)
				{
					sprintf(line, "%-9s%s", "TRACK", entry->name);
				}
				else if (i == DBG_TRACK_AUTHOR)
				{
					// Empty fields are NAMED. A missing author name in a list
					// is otherwise first noticed by whoever downloads the track.
					sprintf(line, "%-9s%s", "AUTHOR", (entry->author[0] != '\0') ? entry->author : "(not set)");
				}
				else if (i == DBG_TRACK_VERSION)
				{
					sprintf(line, "%-9s%u", "VERSION", entry->trackVersion);
				}
				else if (i == DBG_TRACK_MODES)
				{
					// What the author promises, not what the game has established.
					sprintf(line, "%-9s%s", "MODES", NativeTrack_ModesText(entry->modes));
				}
				else if (i == DBG_TRACK_META_VERSION)
				{
					sprintf(line, "%-9s%u%s", "META VER", entry->metaVersion,
					        !entry->ok ? "" : ((entry->metaVersion == 0) ? "  DRAFT" : "  FROZEN"));
				}
				else if (i == DBG_TRACK_FILE)
				{
					sprintf(line, "%-9s%s", "FILE", entry->file);
				}
				else if (i == DBG_TRACK_DONOR)
				{
					// The seat, not the identity. Both on screen at once, which
					// is the whole point of the row.
					sprintf(line, "%-9s%s %d", "SLOT", s_levels[s_trackDonor].name, s_levels[s_trackDonor].id);
				}
				else if (i == DBG_TRACK_CRYSTAL)
				{
					// MEASURING INSTRUMENT, see s_crystalProbe.
					sprintf(line, "%-9s%s", "CRYSTAL", DebugMenu_CrystalProbeText());
				}
				else
				{
					sprintf(line, "%s", entry->ok ? "LOAD AND RACE" : "CANNOT LOAD - SEE LOG");
				}
				break;
			}

			default:
				sprintf(line, "%s", s_cheats[i].name);
				break;
			}

			DebugMenu_FitLine(line, s_page);
			DecalFont_DrawLine(line, (s16)textX, (s16)y, FONT_SMALL, selected ? ORANGE : WHITE);
			y += NATIVE_DEBUG_MENU_ROW;
		}

		// Says there is more list than panel. A list that simply stops looks the
		// same as a list that ended.
		if (rows > DBG_VISIBLE_ROWS)
		{
			sprintf(line, "%d/%d", s_cursor[s_page] + 1, rows);
			DecalFont_DrawLine(line, (s16)textX, (s16)(NATIVE_DEBUG_MENU_H - 3 * NATIVE_DEBUG_MENU_ROW - 4), FONT_SMALL, WHITE);
		}

		DecalFont_DrawLine((char *)DebugMenu_ActionHint(s_page), (s16)textX,
		                   (s16)(NATIVE_DEBUG_MENU_H - 2 * NATIVE_DEBUG_MENU_ROW - 4), FONT_SMALL, WHITE);
		DecalFont_DrawLine((char *)((s_page == DBG_ROOT) ? "SEL+START = CLOSE" : "O = BACK"), (s16)textX,
		                   (s16)(NATIVE_DEBUG_MENU_H - NATIVE_DEBUG_MENU_ROW - 4), FONT_SMALL, WHITE);

		{
			RECT rule = {NATIVE_DEBUG_MENU_PAD, NATIVE_DEBUG_MENU_PAD + NATIVE_DEBUG_MENU_ROW + 2,
			             (s16)(DebugMenu_PanelWidth(s_page) - 2 * NATIVE_DEBUG_MENU_PAD), 1};
			Color ruleColor;

			ColorCode_SetPacked(&ruleColor, MakeColorPacked(0x40, 0x50, 0xb0));
			CTR_Box_DrawSolidBox(&rule, ruleColor, gGT->pushBuffer_UI.ptrOT);
		}

		{
			RECT panel = {0, 0, (s16)DebugMenu_PanelWidth(s_page), NATIVE_DEBUG_MENU_H};
			Color panelColor;

			ColorCode_SetPacked(&panelColor, MakeColorPacked(0x10, 0x14, 0x58));
			CTR_Box_DrawSolidBox(&panel, panelColor, gGT->pushBuffer_UI.ptrOT);
		}
	}
}

#endif
