#include <common.h>
#include <platform/native_mods.h>
#include <platform/native_extcoll.h>
#include <platform/native_extphys.h>

// The session cheats from gameMode2 as text (native_menuscreen.c, CHEATS box);
// the [CTR Race] line names them, a measurement run must show "cheats none".
const char *NativeMenuCheats_Describe(void);

// From platform/native_chars.c, further down in the translation unit: the pick
// of a custom character for a start that skips the driver select.
void NativeChar_PickForJump(void);

#ifdef CTR_NATIVE

// The TRACKS page reads fields of struct NativeTrackEntry, and a struct
// cannot be forward-declared like a function. Hence a header here
// instead of the extern lines further down. The include guard in the file makes
// this a no-op, main.c pulled it in long ago.
#include <platform/native_assets.h>

// --menu-keys plays a key sequence into the pad bus. That needs the
// snapshot that replay and quick state install as well.
#include <platform/native_input.h>

// --dev-race-mode: the memory card root is moved away from memcards/ (see DebugMenu_JumpToLevel).
#include <platform/native_memcard.h>

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

// --dev-race-mode tt: the ghost tape the track select allocates before the ghost
// menu (game/230/MM_TrackSelect.c:47-48, MM_TRACK_SELECT_GHOST_TAPE_ALLOC_SIZE and
// _CLEAR_SIZE): the whole buffer, and the header that is cleared for "no ghost".
#define DBG_GHOST_TAPE_ALLOC_SIZE 0x3e00
#define DBG_GHOST_TAPE_CLEAR_SIZE 0x28

// --dev-race-mode (main.c): 0 the arcade race of every jump so far, 1 a retail
// Time Trial, 2 a plain Adventure race.
extern int g_cfg_devRaceMode;
// --dev-tt-ghost (main.c): with --dev-race-mode tt, the staff ghost races along.
extern int g_cfg_devTtGhost;
// --dev-cup (main.c): -1 off, else the retail arcade cup 0..3 the run starts
// (DebugMenu_DevCupTick).
extern int g_cfg_devCup;

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

// A stick at rest. The axes run 0..0xff with the middle at 0x80 (GAMEPAD.c
// resets them there), so 0 is not "nothing" but a stick held fully up or left:
// the right stick up is analog gas (VehPhysProc.c, stickRY) and the left stick
// left is a full turn (stickLX).
#define DBG_STICK_REST 0x80

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

	// No driver select on this way, so no pick from it: the --char file, if one
	// loaded, else none. The load funnel binds it only on its template.
	NativeChar_PickForJump();

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

	// --dev-race-mode (developer switch, measuring the engine; 0 = off = every
	// line above and below exactly as before). The chosen mode's own bit goes
	// into the set mask AND out of the clear mask (same reason as the probe:
	// MainMain.c:247 clears after it sets), and ARCADE_MODE into the clear mask:
	// retail's Time Trial and Adventure never carry it (MM_MenuFlow.c:140 clears
	// it before the choice sets TIME_TRIAL at 179 or ADVENTURE_MODE at 159).
	if (g_cfg_devRaceMode == 1)
	{
		addBits = TIME_TRIAL;
		remBits = (remBits & ~(u32)TIME_TRIAL) | ARCADE_MODE;
	}
	else if (g_cfg_devRaceMode == 2)
	{
		// ADVENTURE_ARENA stays in the clear mask: this is the race a warp pad
		// starts (AH_WarpPad.c:693 removes the arena bit), not the hub.
		addBits = ADVENTURE_MODE;
		remBits = (remBits & ~(u32)ADVENTURE_MODE) | ARCADE_MODE;
	}

	sdata->Loading.OnBegin.AddBitsConfig0 |= addBits;
	sdata->Loading.OnBegin.RemBitsConfig0 |= remBits;

	gGT->gameMode2 &= ~CUP_ANY_KIND;
	gGT->numPlyrNextGame = 1;
	gGT->currLEV = (s16)levelID;
	gGT->numLaps = ((gGT->gameMode2 & CHEAT_ONELAP) != 0) ? 1 : DBG_JUMP_LAPS;

	if (g_cfg_devRaceMode != 0)
	{
		// What the menu sets for the two modes besides the mode bit:
		// - three laps, the 1-lap cheat intentionally off (MM_MenuFlow.c:146-150
		//   MM_DEFAULT_LAP_COUNT; the arcade choice alone honours CHEAT_ONELAP);
		// - the item cheats off (MM_MenuFlow.c:160 for the adventure, :180 for the
		//   Time Trial);
		// - no CTR token race left over (the warp pad's token row sets it, and a
		//   plain race has none: AH_WarpPad.c:148-153).
		gGT->numLaps = DBG_JUMP_LAPS;
		gGT->gameMode2 &= ~(CHEAT_WUMPA | CHEAT_MASK | CHEAT_TURBO | CHEAT_ENGINE | CHEAT_BOMBS | TOKEN_RACE);
		sdata->Loading.OnBegin.RemBitsConfig8 |= TOKEN_RACE;

		// NO WRITE TO DISK. Nothing in a race writes a memory card file by itself
		// - every write is a menu choice (RefreshCard.c:295/301 through
		// SelectProfile, GAMEPROG_SaveCupProgress at SelectProfile.c:645) - and a
		// run that ends before the finish never reaches one. This is the net under
		// that: the card root moved to a folder of its own, so a choice made by
		// accident (an autopilot pressing through the end menu) lands there and
		// never in memcards/, and the saves of memcards/ are not even found.
		// The Time Trial and the Adventure race touch only the RAM copy of the
		// progress (MainGameEnd.c: highScoreTracks, advProgress losses).
		(void)NativeMemcard_SetRoot("memcards-dev-race-mode");
	}

	if (g_cfg_devRaceMode == 1)
	{
		// What the track select does when the Time Trial track is chosen
		// (MM_TrackSelect.c:461-469): the buffer for the ghost tape at the end of
		// the memory, cleared header, "no ghost in the race". The ghost menu then
		// finishes with "no ghost" (SelectProfile.c:951 clears the same 0x28 bytes)
		// and copies the tape's character into seat 1 (SelectProfile.c:1314-1317),
		// which for a cleared header is character 0. A tape of an earlier race is
		// let go first, as the race-end menu does (GhostTape_Destroy).
		GhostTape_Destroy();
		sdata->ptrGhostTapePlaying = MEMPACK_AllocHighMem(DBG_GHOST_TAPE_ALLOC_SIZE);
		memset(sdata->ptrGhostTapePlaying, 0, DBG_GHOST_TAPE_CLEAR_SIZE);
		sdata->boolReplayHumanGhost = 0;
		data.characterIDs[1] = sdata->ptrGhostTapePlaying->characterID;

		// QueueLoadTrack_MenuProc (QueueLoadTrack.c:9-14): the two staff ghosts.
		data.characterIDs[2] = N_TROPY;
		data.characterIDs[3] = NITROS_OXIDE;

		// --dev-tt-ghost: the staff ghost plays when the track's progress has
		// TT_NTROPY_OPEN (GhostReplay_Init2, GhostReplay.c:456); its tape comes
		// from the level itself (GhostReplay.c:357-361, N. Tropy until
		// TT_NTROPY_BEATEN). The bit is what beating the track's time sets
		// (MainGameEnd.c:67), here only in the RAM copy of the progress; the card
		// root is memcards-dev-race-mode (above). Disc tracks only: a container
		// in the donor slot is not known to carry a staff tape.
		if (g_cfg_devTtGhost)
		{
			if ((levelID >= 0) && (levelID < MEMCARD_HIGH_SCORE_TRACK_COUNT) && !NativeTrack_ActiveForLevel(levelID))
			{
				sdata->gameProgress.highScoreTracks[levelID].timeTrialFlags |= TT_NTROPY_OPEN;
				Platform_Log("[CTR Debug] --dev-tt-ghost: staff ghost of level %d on (%s)\n", levelID,
				             ((sdata->gameProgress.highScoreTracks[levelID].timeTrialFlags & TT_NTROPY_BEATEN) != 0) ? "Oxide" : "N. Tropy");
			}
			else
			{
				Platform_Log("[CTR Debug] --dev-tt-ghost: level %d is no disc race track - no staff ghost\n", levelID);
			}
		}
	}

	// --dev-cup (developer switch, measuring the engine; -1 = off = every line
	// above and below exactly as before): what the cup menu sets on top of the
	// arcade race this jump already is (ARCADE_MODE as MM_MenuFlow.c:195):
	// - the cup bit (MM_MenuProc_SingleCup, MM_MenuFlow.c:459) and the cup:
	//   cupID, the first track, the eight scores at 0 (MM_CupSelect.c:66-77);
	//   the level of the jump is the cup's first track (DebugMenu_DevCupTick);
	// - the speed of the EASY row (MM_MenuProc_Difficulty, MM_MenuFlow.c:427),
	//   the one row a progress without cup wins offers (MM_ToggleRows_Difficulty);
	// - one lap per race, what the ARCADE row sets with the ONE LAP cheat on
	//   (MM_MenuFlow.c:189-191), so that a measuring run reaches the podium in
	//   minutes; numLaps stays from race to race, the cup only loads the next
	//   track (UI_CupStandings.c:646);
	// - the card root of --dev-race-mode: a cup won for the first time asks on
	//   the podium to save (CUP_NEW_WIN, UI_CupStandings.c:773, CS_Camera.c:247),
	//   and a script pressing CROSS answers it there, never in memcards/.
	if (g_cfg_devCup >= 0)
	{
		int seat;

		gGT->gameMode2 |= CUP_ANY_KIND;
		gGT->cup.cupID = g_cfg_devCup;
		gGT->cup.trackIndex = 0;
		for (seat = 0; seat < (int)(sizeof(gGT->cup.points) / sizeof(gGT->cup.points[0])); seat++)
		{
			gGT->cup.points[seat] = 0;
		}
		gGT->arcadeDifficulty = D230.cupDifficulty.speed[0];
		gGT->numLaps = MM_ONE_LAP_CHEAT_COUNT;
		(void)NativeMemcard_SetRoot("memcards-dev-race-mode");
	}

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
	if (g_cfg_devRaceMode != 0)
	{
		Platform_Log("[CTR Debug] --dev-race-mode: %s, gameMode1 add 0x%08x clear 0x%08x, memory card root memcards-dev-race-mode\n",
		             (g_cfg_devRaceMode == 1) ? "retail Time Trial (TIME_TRIAL, no ghost, N Tropy and Oxide in seats 2 and 3)" : "Adventure race (ADVENTURE_MODE)",
		             (unsigned)addBits, (unsigned)remBits);
	}
	if (g_cfg_devCup >= 0)
	{
		Platform_Log("[CTR Debug] --dev-cup: retail arcade cup %d (CUP_ANY_KIND), race 1 of 4, difficulty %d (EASY), %d lap(s) per race, memory card root memcards-dev-race-mode\n",
		             g_cfg_devCup, (int)gGT->arcadeDifficulty, (int)gGT->numLaps);
	}
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

// ---------------------------------------------------------------------------
//  --dev-kart-steer: the human kart of seat 0 driven by a simple steerer,
//  with the kart's own physics (not a bot like --autopilot) - for measuring
//  what only a kart does, such as its turbo pads and what SQUARE does to the
//  reserves (RETRO FUELED of the MODS page).
//
//  HOW. In every race frame the steerer holds CROSS and steers with the
//  D-pad towards the restart point two ahead of the kart's own (one ahead with
//  --dev-kart-steer-lead 1, for tight bends where two ahead cuts the inner
//  side) (ptr_restart_points, the heading as VehStuckProc's respawn takes it:
//  ratan2(dx, dz)). Which way LEFT turns the heading is learnt from the kart
//  itself while it steers. From the n-th race of the run on (--dev-kart-steer
//  n, default 1), each time the kart gets reserves it holds SQUARE for 8
//  frames once it is off the pad and on the ground with more than 300 left -
//  every other time with DOWN, which KEEP RESERVES lets keep them, the others
//  alone, which clears them. (On a pad the game keeps the reserves anyway,
//  and SQUARE before a jump costs the speed for it - the first try braked
//  right after the pads of Hot Air Skyway and fell into its gap.) The pad bus
//  is set as the key sequence sets it (not together with --menu-keys). Every
//  change of the reserves under SQUARE is logged.
// ---------------------------------------------------------------------------

int g_cfg_kartSteer = 0;
int g_cfg_kartSteerLead = 2;

// --dev-kart-steer-unstick (only beside --dev-kart-steer; without it the helper
// above is unchanged to the last pad bit). For long measuring races, where a
// kart wedged in a wall corner stood still for the rest of the run: the target
// stays the helper's own (restart points ahead); when the kart moved less than
// KART_UNSTICK_MOVE units in KART_UNSTICK_TICKS race frames (after the start
// lights, not in a mask grab), it backs off: KART_UNSTICK_BACK frames SQUARE
// without CROSS (the brake reverses once slow, VehPhysProc.c:892-901) with the
// steering turned round. Stuck again within KART_UNSTICK_AGAIN frames after
// that: the game's own respawn (VehStuckProc_MaskGrab_Init, as COLL.c:1798-1803).
// (A target 12 m ahead instead was tried first: on the first test track it cut corners into
// the walls, 21 back-offs in 9000 frames, run U1.) Every action is one log
// line, the counts one line when the race ends.
int g_cfg_kartSteerUnstick = 0;

enum
{
	KART_UNSTICK_MOVE = 64,   // 1 m
	KART_UNSTICK_TICKS = 25,  // 0.8 s: detect + back off + detect again = 80 frames
	KART_UNSTICK_BACK = 30,
	KART_UNSTICK_AGAIN = 120,
	KART_UNSTICK_HOLD = 90,   // no new action this long after a respawn
};

internal void DebugMenu_KartSteerTick(struct GameTracker *gGT)
{
	local_persist struct PlatformInputPadSnapshot base[4];
	local_persist int haveBase = 0;
	local_persist int armed = 0;
	local_persist int leftSign = 1;
	local_persist int votes = 0;
	local_persist int lastAngle = 0;
	local_persist u16 lastWord = 0xffff;
	local_persist int hadReserves = 0;
	local_persist int pending = 0;
	local_persist int race = 0;
	local_persist int squareLeft = 0;
	local_persist int squareCount = 0;
	local_persist int squareDown = 0;
	local_persist int squareFrom = 0;
	// --dev-kart-steer-unstick
	local_persist int usTick = 0;
	local_persist int usAnchorTick = 0;
	local_persist int usAnchor[3];
	local_persist int usBackLeft = 0;
	local_persist int usBackEnd = -100000;
	local_persist int usHoldUntil = 0;
	local_persist int usBacks = 0;
	local_persist int usRespawns = 0;
	local_persist int usLastBack = 0;
	local_persist int usWaitGround = 0;

	struct PlatformInputPadSnapshot pads[4];
	struct Driver *d;
	struct Level *level;
	u16 word = 0xffff;
	int slot;
	int steerDiff = 0;

	if (!g_cfg_kartSteer)
	{
		return;
	}

	d = (gGT != NULL) ? gGT->drivers[0] : NULL;
	level = (gGT != NULL) ? gGT->level1 : NULL;

	if ((d == NULL) || (level == NULL) || ((gGT->gameMode1 & (MAIN_MENU | LOADING | END_OF_RACE)) != 0) || (sdata->Loading.stage != LOAD_IDLE) ||
	    ((d->actionsFlagSet & ACTION_BOT) != 0) || (level->ptr_restart_points == NULL) || (level->cnt_restart_points < 3))
	{
		if (armed)
		{
			armed = 0;
			hadReserves = 0;
			pending = 0;
			squareLeft = 0;
			Platform_InputClearInstalledPadSnapshots();
			if (g_cfg_kartSteerUnstick)
			{
				Platform_Log("[CTR Debug] --dev-kart-steer-unstick: race %d: %d back-off(s), %d respawn(s) in %d frame(s)\n", race, usBacks, usRespawns,
				             usTick);
				usTick = 0;
				usAnchorTick = 0;
				usBackLeft = 0;
				usBackEnd = -100000;
				usHoldUntil = 0;
				usBacks = 0;
				usRespawns = 0;
				usLastBack = 0;
				usWaitGround = 0;
			}
		}
		return;
	}

	if (!armed)
	{
		race++;
		Platform_Log("[CTR Debug] --dev-kart-steer: race %d of the run, level %d, SQUARE tests %s\n", race, (int)gGT->levelID,
		             (race >= g_cfg_kartSteer) ? "on" : "off");
	}

	if (!haveBase)
	{
		if (Platform_InputCapturePadSnapshots(base, 4) <= 0)
		{
			return;
		}
		haveBase = 1;
	}

	{
		const struct CheckpointNode *cn = level->ptr_restart_points;
		const int count = level->cnt_restart_points;
		int target = d->checkpoint.currentIndex;
		int k;

		for (k = 0; k < g_cfg_kartSteerLead; k++)
		{
			const int next = cn[(target < count) ? target : 0].nextIndex_forward;

			target = (next < count) ? next : 0;
		}
		// In an external track's anti-gravity zone the checkpoint of the ground
		// can lag behind on walls and overhangs (the point the kart drives on
		// lies far from the node of its record): a target behind the kart (more
		// than a quarter turn off its heading in the surface frame) or closer
		// than 4 m is skipped, up to four nodes on. Nothing outside a zone.
		for (k = 0; (k < 4) && (ExtPhys_DriverZone(d) != 0); k++)
		{
			const int tdx = cn[target].pos.x - (d->posCurr.x >> 8);
			const int tdy = cn[target].pos.y - (d->posCurr.y >> 8);
			const int tdz = cn[target].pos.z - (d->posCurr.z >> 8);
			int heading = 0;
			const int off = ExtPhys_ZoneHeadingTo(d, tdx, tdy, tdz, &heading) ? (((heading - (d->angle & 0xfff) + 0x800) & 0xfff) - 0x800) : 0;
			const s64 dist2 = (s64)tdx * tdx + (s64)tdy * tdy + (s64)tdz * tdz;
			const int next = cn[(target < count) ? target : 0].nextIndex_forward;

			if ((off <= 0x400) && (off >= -0x400) && (dist2 >= (s64)256 * 256))
			{
				break;
			}
			target = (next < count) ? next : 0;
		}

		{
			const int dx = cn[target].pos.x - (d->posCurr.x >> 8);
			const int dz = cn[target].pos.z - (d->posCurr.z >> 8);
			int want = ratan2(dx, dz) & 0xfff;
			const int angle = d->angle & 0xfff;

			// In an external track's anti-gravity zone d->angle is a heading
			// in the surface frame: the target's heading is taken there too
			// (platform/native_extphys.c; nothing outside a zone).
			ExtPhys_ZoneHeadingTo(d, dx, cn[target].pos.y - (d->posCurr.y >> 8), dz, &want);
			int diff = ((want - angle + 0x800) & 0xfff) - 0x800;
			int turned = ((angle - lastAngle + 0x800) & 0xfff) - 0x800;

			// Learn which way LEFT turns: the heading's change while a
			// steering key was held in the frame before.
			// (Not after a back-off frame of --dev-kart-steer-unstick: reversing
			// turns the heading the other way.)
			if (!usLastBack && ((lastWord & RAW_BTN_LEFT) == 0) && (turned != 0))
			{
				votes += (turned > 0) ? 1 : -1;
			}
			else if (!usLastBack && ((lastWord & RAW_BTN_RIGHT) == 0) && (turned != 0))
			{
				votes += (turned > 0) ? -1 : 1;
			}
			if ((votes > 8) || (votes < -8))
			{
				const int sign = (votes > 0) ? 1 : -1;

				if (sign != leftSign)
				{
					Platform_Log("[CTR Debug] --dev-kart-steer: LEFT turns the heading %s\n", (sign > 0) ? "up" : "down");
				}
				leftSign = sign;
				votes = 0;
			}
			lastAngle = angle;
			steerDiff = diff;

			word &= (u16)~RAW_BTN_CROSS;
			if ((diff > 0x40) || (diff < -0x40))
			{
				word &= (u16)~((((diff > 0) ? 1 : -1) == leftSign) ? RAW_BTN_LEFT : RAW_BTN_RIGHT);
			}
		}
	}

	// SQUARE once for each new batch of reserves, off the pad and on the
	// ground, every other time with DOWN.
	if ((d->reserves > 0) && !hadReserves)
	{
		pending = 1;
	}
	if (d->reserves <= 0)
	{
		pending = 0;
	}
	hadReserves = (d->reserves > 0);

	if (pending && (squareLeft == 0) && (race >= g_cfg_kartSteer) && (d->reserves > 300) &&
	    ((d->stepFlagSet & COLL_STEP_TRIGGER_TURBO_PAD_MASK) == 0) && ((d->actionsFlagSet & ACTION_TOUCH_GROUND) != 0))
	{
		pending = 0;
		squareLeft = 8;
		squareDown = (squareCount & 1) == 0;
		squareFrom = d->reserves;
		squareCount++;
	}

	if (squareLeft > 0)
	{
		word |= (u16)(RAW_BTN_CROSS | RAW_BTN_LEFT | RAW_BTN_RIGHT);
		word &= (u16)~(RAW_BTN_SQUARE | (squareDown ? RAW_BTN_DOWN : 0));
		squareLeft--;
		if (squareLeft == 0)
		{
			Platform_Log("[CTR Debug] --dev-kart-steer: square%s %d held 8 frames: reserves %d before, %d after\n", squareDown ? " + down" : " alone",
			             squareCount, squareFrom, (int)d->reserves);
		}
	}

	if (g_cfg_kartSteerUnstick)
	{
		const int px = d->posCurr.x >> 8;
		const int py = d->posCurr.y >> 8;
		const int pz = d->posCurr.z >> 8;
		int racing;
		long long mx;

		// No detection while the mask carries the kart or it revs up again, and
		// not until it touched the ground after that (a respawn is no stand-still).
		if ((d->kartState == KS_MASK_GRABBED) || (d->kartState == KS_ENGINE_REVVING))
		{
			usWaitGround = 1;
		}
		else if (usWaitGround && ((d->actionsFlagSet & ACTION_TOUCH_GROUND) != 0))
		{
			usWaitGround = 0;
		}
		racing = ((gGT->gameMode1 & START_OF_RACE) == 0) && (gGT->trafficLightsTimer <= 0) && !usWaitGround;
		mx = (long long)px - usAnchor[0];
		const long long my = (long long)py - usAnchor[1];
		const long long mz = (long long)pz - usAnchor[2];

		usTick++;
		if (!racing || (usBackLeft > 0) || ((mx * mx + my * my + mz * mz) > (long long)KART_UNSTICK_MOVE * KART_UNSTICK_MOVE))
		{
			usAnchor[0] = px;
			usAnchor[1] = py;
			usAnchor[2] = pz;
			usAnchorTick = usTick;
		}
		else if ((usTick - usAnchorTick >= KART_UNSTICK_TICKS) && (usTick >= usHoldUntil))
		{
			if ((usTick - usBackEnd <= KART_UNSTICK_AGAIN) && (d->lastValid != NULL) && (d->instSelf != NULL) && (d->instSelf->thread != NULL))
			{
				// The game's mask grab, as COLL.c:1798-1803 starts it (a request flag
				// set here would be cleared by VehPhysForce.c:578 before it is read).
				VehStuckProc_MaskGrab_Init(d->instSelf->thread, d);
				usRespawns++;
				usHoldUntil = usTick + KART_UNSTICK_HOLD;
				usBackEnd = -100000;
				// (The drive log names this mask grab "reason progress", its catch-all.)
				Platform_Log("[CTR Debug] --dev-kart-steer-unstick: stuck at %d %d %d frame %d -> respawn %d (reason unstick)\n", px, py, pz, usTick,
				             usRespawns);
			}
			else
			{
				usBackLeft = KART_UNSTICK_BACK;
				usBacks++;
				Platform_Log("[CTR Debug] --dev-kart-steer-unstick: stuck at %d %d %d frame %d -> back off %d\n", px, py, pz, usTick, usBacks);
			}
			usAnchorTick = usTick;
		}

		usLastBack = (usBackLeft > 0);
		if (usBackLeft > 0)
		{
			// Backwards: no gas, the brake reverses; the steering is turned round.
			word |= (u16)(RAW_BTN_CROSS | RAW_BTN_LEFT | RAW_BTN_RIGHT | RAW_BTN_DOWN);
			word &= (u16)~RAW_BTN_SQUARE;
			if ((steerDiff > 0x40) || (steerDiff < -0x40))
			{
				word &= (u16)~((((steerDiff > 0) ? 1 : -1) == leftSign) ? RAW_BTN_RIGHT : RAW_BTN_LEFT);
			}
			usBackLeft--;
			if (usBackLeft == 0)
			{
				usBackEnd = usTick;
			}
		}
	}

	memcpy(pads, base, sizeof(pads));
	for (slot = 0; slot < 4; slot++)
	{
		pads[slot].buttons[0] = (u8)(word & 0xff);
		pads[slot].buttons[1] = (u8)((word >> 8) & 0xff);
		if (slot == 0)
		{
			pads[slot].connected = 1;
			pads[slot].status = 0;
			pads[slot].id = 0x41;
		}
	}
	Platform_InputInstallPadSnapshots(pads, 4);
	armed = 1;
	lastWord = word;
}

// ---------------------------------------------------------------------------
//  --dev-drive-log [n]: what the kart of seat 0 does on the track, as log lines.
//
//  WHAT FOR. A track whose collision is generated (tools/collision_gen) is
//  judged by driving it: does the lap count, does the kart ever fall through,
//  does every mask grab happen where it should. The game keeps these facts in
//  the driver, but writes none of them down.
//
//  HOW. It only reads (the driver of seat 0, the level header) and writes
//  nothing but log lines; without the switch the tick returns at once. Lines:
//    [CTR Drive] tick T pos X Y Z cp C dist D lap L ground G state S
//        every n race frames (default 15)
//    [CTR Drive] lap A -> B tick T time MS cp C pos X Y Z ymin Y
//        when the lap counter changes (ymin: the lowest y of seat 0 since the
//        last lap line)
//    [CTR Drive] mask grab tick T reason R cp C pos X Y Z
//        when the kart enters the mask grab; R is what the game left readable:
//        kill-plane (the step flags of the collision), depth (below the BSP
//        root box - 0x40, COLL.c), else progress (a jump of a quarter lap)
//    [CTR Drive] landed tick T cp C pos X Y Z
//        when it leaves the mask grab
//  Positions in world units (posCurr >> 8), cp the restart point of lastValid
//  (255 = none), dist distanceToFinish_curr.
// ---------------------------------------------------------------------------

int g_cfg_driveLog = 0;

enum
{
	DRIVE_LOG_TICK = 0,
	DRIVE_LOG_LAP,
	DRIVE_LOG_GRAB,
	DRIVE_LOG_LANDED,
};

// One line of the drive log into out (the format the selftest holds fixed).
// v: tick, x, y, z, cp, then per kind: TICK dist, lap, ground, state;
// LAP lap before, lap after, time, ymin; GRAB reason (index into the names).
internal int DebugMenu_DriveLogFormat(char *out, size_t size, int kind, const int *v)
{
	local_persist const char *const reasons[3] = {"kill-plane", "depth", "progress"};

	switch (kind)
	{
	case DRIVE_LOG_TICK:
		return snprintf(out, size, "[CTR Drive] tick %d pos %d %d %d cp %d dist %d lap %d ground %d state %d\n", v[0], v[1], v[2], v[3], v[4],
		                v[5], v[6], v[7], v[8]);
	case DRIVE_LOG_LAP:
		return snprintf(out, size, "[CTR Drive] lap %d -> %d tick %d time %d cp %d pos %d %d %d ymin %d\n", v[5], v[6], v[0], v[7], v[4], v[1],
		                v[2], v[3], v[8]);
	case DRIVE_LOG_GRAB:
		return snprintf(out, size, "[CTR Drive] mask grab tick %d reason %s cp %d pos %d %d %d\n", v[0], reasons[((v[5] >= 0) && (v[5] < 3)) ? v[5] : 2],
		                v[4], v[1], v[2], v[3]);
	default:
		return snprintf(out, size, "[CTR Drive] landed tick %d cp %d pos %d %d %d\n", v[0], v[4], v[1], v[2], v[3]);
	}
}

internal void DebugMenu_DriveLogTick(struct GameTracker *gGT)
{
	local_persist int armed = 0;
	local_persist int tick = 0;
	local_persist int lastLap = 0;
	local_persist int grabbed = 0;
	local_persist int ymin = 0;

	struct Driver *d;
	struct Level *level;
	char line[160];
	int v[9];

	if (!g_cfg_driveLog)
	{
		return;
	}

	d = (gGT != NULL) ? gGT->drivers[0] : NULL;
	level = (gGT != NULL) ? gGT->level1 : NULL;
	if ((d == NULL) || (level == NULL) || ((gGT->gameMode1 & (MAIN_MENU | LOADING)) != 0) || (sdata->Loading.stage != LOAD_IDLE))
	{
		armed = 0;
		return;
	}

	v[0] = tick + 1;
	v[1] = d->posCurr.x >> 8;
	v[2] = d->posCurr.y >> 8;
	v[3] = d->posCurr.z >> 8;
	v[4] = (d->lastValid != NULL) ? d->lastValid->checkpointIndex : 255;
	if (!armed)
	{
		armed = 1;
		tick = 0;
		v[0] = 1;
		lastLap = d->lapIndex;
		grabbed = (d->kartState == KS_MASK_GRABBED);
		ymin = v[2];
	}
	tick = v[0];
	if (v[2] < ymin)
	{
		ymin = v[2];
	}

	if (d->lapIndex != lastLap)
	{
		v[5] = lastLap;
		v[6] = d->lapIndex;
		v[7] = gGT->elapsedEventTime;
		v[8] = ymin;
		DebugMenu_DriveLogFormat(line, sizeof(line), DRIVE_LOG_LAP, v);
		Platform_Log("%s", line);
		lastLap = d->lapIndex;
		ymin = v[2];
	}

	if ((d->kartState == KS_MASK_GRABBED) && !grabbed)
	{
		int reason = 2;

		if ((d->stepFlagSet & COLL_STEP_FLAG_KILL_PLANE) != 0)
		{
			reason = 0;
		}
		else if ((COLL_WorldMesh(level) != NULL) && (COLL_WorldMesh(level)->bspRoot != NULL) &&
		         (v[2] < (COLL_WorldMesh(level)->bspRoot->box.min.y - 0x40)))
		{
			reason = 1;
		}
		v[5] = reason;
		DebugMenu_DriveLogFormat(line, sizeof(line), DRIVE_LOG_GRAB, v);
		Platform_Log("%s", line);
	}
	else if ((d->kartState != KS_MASK_GRABBED) && grabbed)
	{
		DebugMenu_DriveLogFormat(line, sizeof(line), DRIVE_LOG_LANDED, v);
		Platform_Log("%s", line);
	}
	grabbed = (d->kartState == KS_MASK_GRABBED);

	if ((tick % g_cfg_driveLog) == 0)
	{
		v[5] = (int)d->distanceToFinish_curr;
		v[6] = d->lapIndex;
		v[7] = ((d->collisionFlags & DRIVER_COLL_FLAG_GROUNDED) != 0) ? 1 : 0;
		v[8] = d->kartState;
		DebugMenu_DriveLogFormat(line, sizeof(line), DRIVE_LOG_TICK, v);
		Platform_Log("%s", line);
	}
}

// --dev-drive-log-selftest: the line formats of --dev-drive-log, without a
// race (ctest drive_log_selftest). 0 = passed.
int DebugMenu_DriveLogSelfTest(void)
{
	static const struct
	{
		int kind;
		int v[9];
		const char *want;
	} cases[] = {
	    {DRIVE_LOG_TICK, {15, -1200, -32, 4000, 12, 30166, 0, 1, 0}, "[CTR Drive] tick 15 pos -1200 -32 4000 cp 12 dist 30166 lap 0 ground 1 state 0\n"},
	    {DRIVE_LOG_LAP, {900, 0, 0, -160, 0, 0, 1, 61234, -560}, "[CTR Drive] lap 0 -> 1 tick 900 time 61234 cp 0 pos 0 0 -160 ymin -560\n"},
	    {DRIVE_LOG_GRAB, {400, -1500, -40, 4300, 60, 0, 0, 0, 0}, "[CTR Drive] mask grab tick 400 reason kill-plane cp 60 pos -1500 -40 4300\n"},
	    {DRIVE_LOG_GRAB, {401, 10, -900, 10, 3, 1, 0, 0, 0}, "[CTR Drive] mask grab tick 401 reason depth cp 3 pos 10 -900 10\n"},
	    {DRIVE_LOG_GRAB, {402, 10, 0, 10, 3, 2, 0, 0, 0}, "[CTR Drive] mask grab tick 402 reason progress cp 3 pos 10 0 10\n"},
	    {DRIVE_LOG_GRAB, {403, 10, 0, 10, 3, 7, 0, 0, 0}, "[CTR Drive] mask grab tick 403 reason progress cp 3 pos 10 0 10\n"},
	    {DRIVE_LOG_LANDED, {445, 2000, -300, 9000, 60, 0, 0, 0, 0}, "[CTR Drive] landed tick 445 cp 60 pos 2000 -300 9000\n"},
	};
	char line[160];
	int failures = 0;
	int i;

	for (i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++)
	{
		DebugMenu_DriveLogFormat(line, sizeof(line), cases[i].kind, cases[i].v);
		if (strcmp(line, cases[i].want) != 0)
		{
			printf("drive log selftest: case %d wrote: %s", i, line);
			failures++;
		}
	}
	printf("drive log selftest %s: %d checks, %d failures\n", failures ? "FAILED" : "passed", i, failures);
	return failures ? 1 : 0;
}

// ---------------------------------------------------------------------------
//  --level-tour: many tracks in one run.
//
//  WHAT FOR. A measurement that has to hold for every track - nothing writes
//  where it must not, on any of them - is one run per track with --level, and
//  eighteen runs where one would do. The tour is that one run.
//
//  HOW. Every entry starts FROM THE MAIN MENU, with the code of --level (a
//  disc track) or of --autoload-track (a container), and after
//  --level-tour-frames race frames goes back the way pause QUIT goes back. Race
//  to race, like the cup, would be quicker, and would bring two traps along: a
//  disc track after a container on the same donor slot would keep driving the
//  container (NativeTrack_ActiveForLevel compares the slot only), and a
//  container that offers Crystal and no Race is not swapped in by the loader's
//  start (MM_NativeTracks_StartLoad loads by the rule for Race). Through the
//  menu every entry takes a road that is already walked: the title edge above
//  releases the container (MM_NativeTracks_Disarm), and the menu's VRAM is
//  loaded again each time.
//
//  Who drives is up to the caller: --autopilot makes seat 0 a bot, the tour
//  does not. One VRAM dump per entry, at its last race frame
//  (Platform_DumpRequest, <--dump-prefix>-tour-NN-<track>.tga). After the last
//  entry the run ends through Platform_QuitGame, the door --menu-keys-quit uses.
//
//  WHAT EACH ENTRY MEASURES (round of 2026-10-15, the engine's all-tracks tour).
//  Everything below only READS game state, the same with and without --engine:
//  - --level-tour-shots: bursts of 12 snapshots of consecutive race frames at
//    the listed race frames of every entry (L<level>@<frame>: that level only),
//    named <--shot-name>-tNN-L<level>-<burst>-<race frame>.bmp, one "burst" line
//    each. Race frames, not VBlanks: an entry's VBlanks move with everything
//    before it, its race frames are its game state.
//  - one "drive" line: the player's progress along the track from race frame
//    DBG_TOUR_DRIVE_FROM on (lap fractions, the longest stall) and a fingerprint
//    of every driver at the last race frame. The tour of a run without --engine
//    and the same tour with it must print the same fingerprints: the measure
//    that the engine left the game alone on every track.
//  - under --engine the per-track reports of the engine (Engine_TourNotify) at
//    the first race frame and after the last.
//  - --level-tour-oracle (round of 2026-10-16): the oracle frames of the
//    material comparison, armed before the frame is drawn - the listed race
//    frames and the entry's start, line-pre/line-post, see and weapon frames (DebugMenu_TourOracle),
//    one "oracle" line each and one per entry after its last frame.
//  - --level-tour-frames L<level>@<count>: that level drives its own count (a
//    track whose objects come late, Sunset Vista's door and platforms).
#define DBG_TOUR_MAX           64
#define DBG_TOUR_RETAIL_RACES  18 // level ids 0..17, include/namespace_Level.h
#define DBG_TOUR_SHOTS_MAX     32 // entries of --level-tour-shots (ENGINE_TOUR_SHOTS_MAX)
#define DBG_TOUR_BURST_FRAMES  12 // snapshots per burst (ENGINE_TOUR_BURST_FRAMES)
// The progress count starts after the start sequence (fly-in and countdown).
#define DBG_TOUR_DRIVE_FROM    150
// The track length of the lap count: the start node's distance, scaled as
// game/Vehicle/VehLap.c:167 scales it (VEH_LAP_TRACK_DISTANCE_SCALE_SHIFT).
#define DBG_TOUR_TRACK_LENGTH_SHIFT 3
// Under --engine: the water probe line every this many race frames (it chooses
// the race frames of the water bursts).
#define DBG_TOUR_WATER_PROBE_EVERY 30
// How long the tour may go without progress - the main menu up, a track
// started, a race frame counted - before it gives up: two minutes of VBlanks.
// A tour that hangs ends with a line that says where, rather than running until
// somebody notices. Watched at the end of every frame by the platform
// (Platform_SetDeadline), not here: this function does not run while the game
// loads (MainMain.c, the game logic is skipped while LOADING is set), and a
// watch that sleeps exactly when a load hangs is no watch.
#define DBG_TOUR_WAIT_VBLANKS  7200

enum
{
	DBG_TOUR_DISC,    // a disc track, started like --level
	DBG_TOUR_RACE,    // a container offering Race, started like --autoload-track
	DBG_TOUR_CRYSTAL, // a container offering Crystal and no Race, started like the TRACK page with the probe
};

enum
{
	DBG_TOUR_PHASE_READ,    // the list is read when the main menu is first up
	DBG_TOUR_PHASE_MENU,    // waiting for the main menu, then the next entry starts
	DBG_TOUR_PHASE_DRIVING, // the entry is loading or racing, frames are counted
	DBG_TOUR_PHASE_QUIT,    // the last dump is written at the end of the frame, the run ends in the next
	DBG_TOUR_PHASE_OFF,
};

struct DebugTourEntry
{
	int level;      // DBG_TOUR_DISC: the level id; otherwise unused
	int trackIndex; // a container's row in the track list, -1 for a disc track
	int mode;       // DBG_TOUR_*
};

global_variable struct DebugTourEntry s_tour[DBG_TOUR_MAX];
global_variable int s_tourCount = 0;
global_variable int s_tourSkipped = 0;
global_variable int s_tourDriven = 0;
global_variable int s_tourPos = 0;
global_variable int s_tourFrames = 0;
global_variable int s_tourLevel = -1;   // the level id the current entry was started on
global_variable int s_tourPhase = DBG_TOUR_PHASE_READ;
global_variable int s_tourWatched = 0; // the deadline has been set once

// --level-tour-shots, read with the list: entries for every level (-1) or one.
global_variable int s_tourShotLevels[DBG_TOUR_SHOTS_MAX];
global_variable int s_tourShotFrames[DBG_TOUR_SHOTS_MAX];
global_variable int s_tourShotCount = 0;
// The current entry: its burst starts, the burst being shot (its first race frame
// and VBlank, its last so far), and what the drive line reports.
global_variable int s_tourBursts[DBG_TOUR_SHOTS_MAX];
global_variable int s_tourBurstCount = 0;
global_variable int s_tourBurstOpen = -1;
global_variable int s_tourBurstFirstFrame = 0;
global_variable int s_tourBurstFirstVBlank = 0;
global_variable int s_tourBurstLastFrame = 0;
global_variable int s_tourBurstLastVBlank = 0;
global_variable u32 s_tourState = 0;
global_variable int s_tourLaps = 0;
global_variable int s_tourMaskGrabs = 0;

// --level-tour-oracle, --level-tour-oracle-see (round of 2026-10-16, W2: the
// material comparison over all tracks) and --level-tour-frames as given, kept
// as text by main.c and read with the list (ENGINE ORACLE (tour) in
// include/platform/engine.h, sizes ENGINE_TOUR_*).
#define DBG_TOUR_TEXT_MAX        512 // ENGINE_TOUR_TEXT_MAX
#define DBG_TOUR_ORACLE_MAX      64  // ENGINE_TOUR_ORACLE_MAX
#define DBG_TOUR_SEE_MAX         8   // ENGINE_TOUR_SEE_MAX
#define DBG_TOUR_SEE_NAMES       3   // ENGINE_TOUR_SEE_NAMES
#define DBG_TOUR_SEE_NAME_BYTES  16  // ENGINE_TOUR_SEE_NAME_BYTES
#define DBG_TOUR_SEE_FRAMES      2   // "see" oracle frames per entry
#define DBG_TOUR_SEE_GAP         5   // race frames at least between them (another animation frame of the models)
#define DBG_TOUR_LINE_BEFORE     20  // the oracle frame before the line: about this many race frames ahead of it
#define DBG_TOUR_LINE_AFTER      10  // the oracle frame after the line: this many race frames after it
char g_cfg_levelTourOracle[DBG_TOUR_TEXT_MAX] = "";
char g_cfg_levelTourOracleSee[DBG_TOUR_TEXT_MAX] = "";
char g_cfg_levelTourFramesText[DBG_TOUR_TEXT_MAX] = "";
global_variable int s_tourOracleLevels[DBG_TOUR_ORACLE_MAX];
global_variable int s_tourOracleFrames[DBG_TOUR_ORACLE_MAX];
global_variable int s_tourOracleCount = 0;
global_variable int s_tourSeeLevels[DBG_TOUR_SEE_MAX];
global_variable char s_tourSeeNames[DBG_TOUR_SEE_MAX][DBG_TOUR_SEE_NAMES][DBG_TOUR_SEE_NAME_BYTES];
global_variable int s_tourSeeCounts[DBG_TOUR_SEE_MAX];
global_variable int s_tourSeeCount = 0;
global_variable int s_tourFrameLevels[DBG_TOUR_SHOTS_MAX];
global_variable int s_tourFrameCounts[DBG_TOUR_SHOTS_MAX];
global_variable int s_tourFrameCount = 0;
global_variable int s_tourFramesThis = 0; // race frames of the current entry
// The current entry's own oracle frames: the race frames they were found in
// (0 = not yet), those found but not yet armed, the frames armed.
#define DBG_TOUR_ORACLE_START    1
#define DBG_TOUR_ORACLE_LINEPRE  2
#define DBG_TOUR_ORACLE_LINEPOST 4
#define DBG_TOUR_ORACLE_WPN      8
#define DBG_TOUR_ORACLE_SEE      16
global_variable int s_tourGoFrame = 0;
global_variable int s_tourLinePreFrame = 0;
global_variable int s_tourLineFrame = 0;
global_variable int s_tourWpnFrame = 0;
global_variable int s_tourSeeFrames[DBG_TOUR_SEE_FRAMES];
global_variable int s_tourSeeFound = 0;
global_variable int s_tourOraclePending = 0;
global_variable int s_tourOracleArmed = 0;
global_variable int s_tourOracleSkipped = 0;
global_variable s32 s_tourPrevDist = 0;
global_variable int s_tourDistPrimed = 0;
global_variable char s_tourWpnName[17];

// platform/engine_tour.c: the tour's arithmetic (include/platform/engine.h, TRACK TOUR).
int EngineTour_ParseShots(const char *text, int *levels, int *frames, int max, int *badAt);
int EngineTour_BurstsForLevel(const int *levels, const int *frames, int count, int level, int *starts, int max, int *overlapped);
int EngineTour_BurstOf(const int *starts, int n, int raceFrame);
int EngineTour_ShotName(char *out, size_t size, const char *shotName, int entry, int level, int burst, int raceFrame);
void EngineTour_DriveBegin(u32 trackLength);
void EngineTour_DriveSample(u32 distanceToFinish);
int EngineTour_DriveMilliLaps(void);
int EngineTour_DriveLongestStall(void);
u32 EngineTour_Fnv(u32 hash, const void *bytesIn, u32 bytes);
u32 EngineTour_FnvStart(void);
void Engine_TourNotify(int entry, int levelID, int phase);
int EngineTour_OracleAt(const int *levels, const int *frames, int count, int level, int raceFrame);
int EngineTour_OracleTag(char *out, size_t size, int entry, int level, const char *kind);
int EngineTour_OracleShotName(char *out, size_t size, const char *shotName, const char *tag);
int EngineTour_LineSoon(s32 remaining, s32 step, int frames);
int EngineTour_ParseSee(const char *text, int *levels, char (*names)[DBG_TOUR_SEE_NAMES][DBG_TOUR_SEE_NAME_BYTES], int *counts, int max, int *badAt);
int EngineTour_DriveForwardFolds(void);
int EngineTour_ParseFrames(const char *text, int *levels, int *frames, int max, int *badAt);
int EngineTour_FramesForLevel(const int *levels, const int *frames, int count, int level, int fallback);
// platform/engine_oracle.c: the frame being made
// now is logged by both ways under tag, its F line naming raceFrame.
void Engine_OracleArmFrame(const char *tag, int raceFrame);
// platform/engine_water.c: where the water lies in view 0, one log line (pure read); frame = the race frame.
void Engine_WaterProbeLog(const struct GameTracker *gGT, int view, int frame, const char *tag);
// platform/native_platform.c: a snapshot at the end of this frame (as native_mods.c asks for one).
void Platform_ShotRequest(const char *path);

internal void DebugMenu_TourAdd(int level, int trackIndex, int mode)
{
	if (s_tourCount >= DBG_TOUR_MAX)
	{
		s_tourSkipped++;
		Platform_Log("[CTR Tour] skip entry %d: the list holds %d\n", s_tourCount + s_tourSkipped, DBG_TOUR_MAX);
		return;
	}

	s_tour[s_tourCount].level = level;
	s_tour[s_tourCount].trackIndex = trackIndex;
	s_tour[s_tourCount].mode = mode;
	s_tourCount++;
}

// A container by the offer rule of the menu: Race if it offers Race, else
// Crystal if it offers Crystal, else a line that says why not.
internal void DebugMenu_TourAddContainer(int index)
{
	const struct NativeTrackEntry *entry = NativeTrack_Get(index);
	const char *whyNoRace = NativeTrack_WhyNoRace(index, NULL);
	const char *whyNoCrystal;

	if (entry == NULL)
	{
		s_tourSkipped++;
		Platform_Log("[CTR Tour] skip row %d: not in the track list\n", index);
		return;
	}

	if (whyNoRace == NULL)
	{
		DebugMenu_TourAdd(-1, index, DBG_TOUR_RACE);
		return;
	}

	whyNoCrystal = NativeTrack_WhyNotOffered(index, NATIVE_TRACK_MODE_CRYSTAL, NULL);

	if (whyNoCrystal == NULL)
	{
		DebugMenu_TourAdd(-1, index, DBG_TOUR_CRYSTAL);
		return;
	}

	s_tourSkipped++;
	Platform_Log("[CTR Tour] skip '%s': race - %s; crystal - %s\n", entry->file, whyNoRace, whyNoCrystal);
}

internal void DebugMenu_TourAddWord(const char *word)
{
	int value = 0;
	int digits = 0;
	int i;

	if (strcmp(word, "races") == 0)
	{
		for (i = 0; i < DBG_TOUR_RETAIL_RACES; i++)
		{
			DebugMenu_TourAdd(i, -1, DBG_TOUR_DISC);
		}
		return;
	}

	if (strcmp(word, "containers") == 0)
	{
		for (i = 0; i < NativeTrack_Count(); i++)
		{
			DebugMenu_TourAddContainer(i);
		}
		return;
	}

	// The battle arenas (level ids NITRO_COURT..LAB_BASEMENT), each started like
	// --level: a one-player arcade race on the arena map, not a battle (the
	// battle menu stays as it is); for load checks of the arenas.
	if (strcmp(word, "arenas") == 0)
	{
		for (i = NITRO_COURT; i <= LAB_BASEMENT; i++)
		{
			DebugMenu_TourAdd(i, -1, DBG_TOUR_DISC);
		}
		return;
	}

	for (i = 0; (word[i] >= '0') && (word[i] <= '9') && (digits < 6); i++)
	{
		value = (value * 10) + (word[i] - '0');
		digits++;
	}

	if ((digits > 0) && (word[i] == '\0'))
	{
		if ((value < DBG_TOUR_RETAIL_RACES) || ((value >= NITRO_COURT) && (value <= LAB_BASEMENT)))
		{
			DebugMenu_TourAdd(value, -1, DBG_TOUR_DISC);
		}
		else if ((value >= NATIVE_TRACK_LEVELID_FIRST) && (value < (NATIVE_TRACK_LEVELID_FIRST + NATIVE_TRACK_LEVELID_COUNT)))
		{
			const int index = NativeTrack_IndexForLevel(value);

			if (index < 0)
			{
				s_tourSkipped++;
				Platform_Log("[CTR Tour] skip '%s': no container has level id %d in this run\n", word, value);
			}
			else
			{
				DebugMenu_TourAddContainer(index);
			}
		}
		else
		{
			s_tourSkipped++;
			Platform_Log("[CTR Tour] skip '%s': neither a race track (0..%d), an arena (%d..%d) nor a container id (%d..%d)\n", word,
			             DBG_TOUR_RETAIL_RACES - 1, NITRO_COURT, LAB_BASEMENT, NATIVE_TRACK_LEVELID_FIRST,
			             NATIVE_TRACK_LEVELID_FIRST + NATIVE_TRACK_LEVELID_COUNT - 1);
		}
		return;
	}

	// A file name, compared the way --autoload-track compares it.
	for (i = 0; i < NativeTrack_Count(); i++)
	{
		const struct NativeTrackEntry *entry = NativeTrack_Get(i);

		if ((entry != NULL) && (strcmp(entry->file, word) == 0))
		{
			DebugMenu_TourAddContainer(i);
			return;
		}
	}

	s_tourSkipped++;
	Platform_Log("[CTR Tour] skip '%s': not in the track folder (%d container(s) listed)\n", word, NativeTrack_Count());
}

internal void DebugMenu_TourRead(void)
{
	extern char g_cfg_levelTour[1024];
	extern int g_cfg_levelTourFrames;
	const char *at = g_cfg_levelTour;

	while (*at != '\0')
	{
		char word[128];
		int length = 0;

		while (*at == ' ')
		{
			at++;
		}

		while ((*at != '\0') && (*at != ','))
		{
			if (length < (int)sizeof(word) - 1)
			{
				word[length++] = *at;
			}
			at++;
		}

		while ((length > 0) && (word[length - 1] == ' '))
		{
			length--;
		}

		word[length] = '\0';

		if (length > 0)
		{
			DebugMenu_TourAddWord(word);
		}

		if (*at == ',')
		{
			at++;
		}
	}

	Platform_Log("[CTR Tour] %d track(s), %d skipped, %d frame(s) each, list '%s'\n", s_tourCount, s_tourSkipped, g_cfg_levelTourFrames,
	             g_cfg_levelTour);

	// --level-tour-shots: a list that does not read is refused whole - a tour
	// with half its bursts would look like a complete measurement.
	{
		extern char g_cfg_levelTourShots[256];
		int badAt = 0;
		const int count = EngineTour_ParseShots(g_cfg_levelTourShots, s_tourShotLevels, s_tourShotFrames, DBG_TOUR_SHOTS_MAX, &badAt);

		s_tourShotCount = 0;
		if (count < 0)
		{
			Platform_Log("[CTR Tour] shots: '%s' refused at entry %d (race frames from 1, L<level>@<frame>, up to %d) - no snapshots\n",
			             g_cfg_levelTourShots, badAt, DBG_TOUR_SHOTS_MAX);
		}
		else if (count > 0)
		{
			s_tourShotCount = count;
			Platform_Log("[CTR Tour] shots: %d burst start(s) of %d race frames, list '%s'\n", count, DBG_TOUR_BURST_FRAMES, g_cfg_levelTourShots);
		}
	}

	// --level-tour-frames with L<level>@<count> entries, and --level-tour-oracle.
	// main.c refused either list before any window when it did not read; read
	// again here, and again refused whole.
	{
		int badAt = 0;
		int count;

		s_tourFrameCount = 0;
		count = EngineTour_ParseFrames(g_cfg_levelTourFramesText, s_tourFrameLevels, s_tourFrameCounts, DBG_TOUR_SHOTS_MAX, &badAt);
		if ((count > 1) || ((count == 1) && (s_tourFrameCounts[0] != g_cfg_levelTourFrames)))
		{
			s_tourFrameCount = count;
			Platform_Log("[CTR Tour] frames: '%s' (L<level>@<count> for that level)\n", g_cfg_levelTourFramesText);
		}

		s_tourOracleCount = 0;
		count = EngineTour_ParseShots(g_cfg_levelTourOracle, s_tourOracleLevels, s_tourOracleFrames, DBG_TOUR_ORACLE_MAX, &badAt);
		if (count < 0)
		{
			Platform_Log("[CTR Tour] oracle: '%s' refused at entry %d - no oracle frames\n", g_cfg_levelTourOracle, badAt);
		}
		else if (count > 0)
		{
			s_tourOracleCount = count;
			Platform_Log("[CTR Tour] oracle: frames '%s' of every entry, and per entry the start, %d before and %d after the line, a weapon in view\n",
			             g_cfg_levelTourOracle, DBG_TOUR_LINE_BEFORE, DBG_TOUR_LINE_AFTER);
		}

		s_tourSeeCount = 0;
		count = EngineTour_ParseSee(g_cfg_levelTourOracleSee, s_tourSeeLevels, s_tourSeeNames, s_tourSeeCounts, DBG_TOUR_SEE_MAX, &badAt);
		if (count < 0)
		{
			Platform_Log("[CTR Tour] oracle see: '%s' refused at entry %d\n", g_cfg_levelTourOracleSee, badAt);
		}
		else if ((count > 0) && (s_tourOracleCount > 0))
		{
			s_tourSeeCount = count;
			Platform_Log("[CTR Tour] oracle see: '%s' - %d frame(s) per listed level with all its models in view\n", g_cfg_levelTourOracleSee,
			             DBG_TOUR_SEE_FRAMES);
		}
	}
}

internal const char *DebugMenu_TourDiscName(int level)
{
	for (int i = 0; i < DBG_COUNT(s_levels); i++)
	{
		if (s_levels[i].id == level)
		{
			return s_levels[i].name;
		}
	}

	return "?";
}

// The dump's name: tour-NN-L<level> for a disc track, tour-NN-<file without
// .rldtrack> for a container, anything but letters, digits, '-' and '_' as '_'.
internal void DebugMenu_TourDump(const struct DebugTourEntry *entry)
{
	char name[96];
	int length;

	if (entry->trackIndex < 0)
	{
		snprintf(name, sizeof(name), "tour-%02d-L%d", s_tourPos + 1, entry->level);
	}
	else
	{
		const struct NativeTrackEntry *track = NativeTrack_Get(entry->trackIndex);
		const char *file = (track != NULL) ? track->file : "unknown";
		const char *dot = strrchr(file, '.');

		length = (dot != NULL) ? (int)(dot - file) : (int)strlen(file);
		snprintf(name, sizeof(name), "tour-%02d-%.*s", s_tourPos + 1, length, file);

		for (char *c = name + 8; *c != '\0'; c++)
		{
			const int keep = ((*c >= 'a') && (*c <= 'z')) || ((*c >= 'A') && (*c <= 'Z')) || ((*c >= '0') && (*c <= '9')) || (*c == '-') || (*c == '_');

			if (!keep)
			{
				*c = '_';
			}
		}
	}

	Platform_DumpRequest(name);
}

// Starts the entry at s_tourPos. 1 = a jump was requested, 0 = it could not
// be started (said in the log).
internal int DebugMenu_TourStart(struct GameTracker *gGT)
{
	extern int g_cfg_jumpDriver;
	const struct DebugTourEntry *entry = &s_tour[s_tourPos];

	if (entry->mode == DBG_TOUR_DISC)
	{
		s_tourLevel = entry->level;
		Platform_Log("[CTR Tour] %d/%d: level %d (%s) at vblank %d\n", s_tourPos + 1, s_tourCount, s_tourLevel, DebugMenu_TourDiscName(s_tourLevel),
		             Platform_GetVBlankCount());
		DebugMenu_JumpToLevel(gGT, s_tourLevel, g_cfg_jumpDriver);
		return 1;
	}

	{
		const struct NativeTrackEntry *track = NativeTrack_Get(entry->trackIndex);
		const char *file = (track != NULL) ? track->file : "?";

		if (entry->mode == DBG_TOUR_RACE)
		{
			// --autoload-track, word for word: the NITRO-PIT row, then the row's
			// runtime ID through the funnel.
			if (!MM_NativeTracks_LoadRow(entry->trackIndex))
			{
				Platform_Log("[CTR Tour] %d/%d: '%s' did not load - skipped\n", s_tourPos + 1, s_tourCount, file);
				return 0;
			}

			s_tourLevel = NativeTrack_LevelForIndex(entry->trackIndex);
			Platform_Log("[CTR Tour] %d/%d: level %d container '%s' race (seat %d) at vblank %d\n", s_tourPos + 1, s_tourCount, s_tourLevel, file,
			             NativeTrack_LoadedDonorLevel(), Platform_GetVBlankCount());
			DebugMenu_JumpToLevel(gGT, s_tourLevel, g_cfg_jumpDriver);
			return 1;
		}

		// Crystal: the TRACK page with the crystal probe, but loaded by the rule
		// for Crystal (MM_NativeTracks_LoadRowFor, as the CRYSTAL list does),
		// and the probe only for this one jump.
		if (!MM_NativeTracks_LoadRowFor(entry->trackIndex, NATIVE_TRACK_MODE_CRYSTAL))
		{
			Platform_Log("[CTR Tour] %d/%d: '%s' did not load - skipped\n", s_tourPos + 1, s_tourCount, file);
			return 0;
		}

		s_tourLevel = NativeTrack_LoadedDonorLevel();
		Platform_Log("[CTR Tour] %d/%d: level %d container '%s' crystal at vblank %d\n", s_tourPos + 1, s_tourCount, s_tourLevel, file,
		             Platform_GetVBlankCount());
		MM_NativeCrystal_MarkDebug();

		{
			const int probeWas = s_crystalProbe;

			s_crystalProbe = 1;
			DebugMenu_JumpToLevel(gGT, s_tourLevel, g_cfg_jumpDriver);
			s_crystalProbe = probeWas;
		}
		return 1;
	}
}

// Back to the main menu, the way pause QUIT goes (MainFreeze_MenuPtrQuit), with
// whatever box is open closed first, the way DebugMenu_JumpToLevel does it - an
// end-of-race box when the race was over before its frames were.
internal void DebugMenu_TourLeave(struct GameTracker *gGT)
{
	if (sdata->ptrActiveMenu != NULL)
	{
		RECTMENU_Hide(sdata->ptrActiveMenu);
	}
	sdata->ptrDesiredMenu = NULL;

	GhostTape_Destroy();
	sdata->Loading.OnBegin.AddBitsConfig0 |= MAIN_MENU;
	sdata->mainMenuState = MAIN_MENU_TITLE;
	sdata->Loading.OnBegin.RemBitsConfig0 |= ADVENTURE_ARENA;
	gGT->gameMode1 &= ~PAUSE_1;
	MainRaceTrack_RequestLoad(MAIN_MENU_LEVEL);
}

internal void DebugMenu_TourStop(const char *why)
{
	Platform_Log("[CTR Tour] stopped: %s; %d driven, %d skipped, %d not reached\n", why, s_tourDriven, s_tourSkipped, s_tourCount - s_tourPos);
	s_tourPhase = DBG_TOUR_PHASE_OFF;
	Platform_QuitGame("--level-tour stopped");
}

// Called by the platform at the end of a frame in which the deadline passed.
// What the tour was waiting for follows from its phase.
internal void DebugMenu_TourMissed(void)
{
	if (s_tourPhase == DBG_TOUR_PHASE_READ)
	{
		DebugMenu_TourStop("the main menu did not come up");
	}
	else if (s_tourPhase == DBG_TOUR_PHASE_MENU)
	{
		DebugMenu_TourStop("the main menu did not come back");
	}
	else if (s_tourPhase == DBG_TOUR_PHASE_DRIVING)
	{
		DebugMenu_TourStop((s_tourFrames == 0) ? "the race did not come up" : "the race did not come back");
	}
	else if (s_tourPhase == DBG_TOUR_PHASE_QUIT)
	{
		// Done, but the frame that was to end the run never came.
		s_tourPhase = DBG_TOUR_PHASE_OFF;
		Platform_QuitGame("--level-tour");
	}
}

// Progress: the watch starts over from this VBlank.
internal void DebugMenu_TourProgress(int now)
{
	Platform_SetDeadline(now + DBG_TOUR_WAIT_VBLANKS, DebugMenu_TourMissed);
}

// In the menu: start the next entry once the main menu is up. 1 = started.
internal int DebugMenu_TourMenuStep(struct GameTracker *gGT, int now, int menuUp)
{
	if (!menuUp)
	{
		return 0;
	}

	DebugMenu_TourProgress(now);

	if (!DebugMenu_TourStart(gGT))
	{
		// Tried once, in the menu it was tried from; the next entry gets the
		// next frame.
		s_tourSkipped++;
		s_tourPos++;
		return 0;
	}

	s_tourPhase = DBG_TOUR_PHASE_DRIVING;
	s_tourFrames = 0;

	// The entry's race frames: its level's L<level>@<count> of --level-tour-frames, else the count.
	{
		extern int g_cfg_levelTourFrames;

		s_tourFramesThis = EngineTour_FramesForLevel(s_tourFrameLevels, s_tourFrameCounts, s_tourFrameCount, s_tourLevel, g_cfg_levelTourFrames);
		if (s_tourFramesThis != g_cfg_levelTourFrames)
		{
			Platform_Log("[CTR Tour] %d/%d: level %d: %d race frame(s) (--level-tour-frames L%d@)\n", s_tourPos + 1, s_tourCount, s_tourLevel, s_tourFramesThis,
			             s_tourLevel);
		}
	}
	return 1;
}

// A weapon in view: a thread of the buckets of the fired weapons - MINE
// (potions, TNT, Nitro; VehPickupItem.c:751, 890) and TRACKING (missiles,
// warpballs; VehPickupItem.c:606, 1132) - whose instance was drawn in view 0 in
// the frame before (DRAW_SUCCESSFUL of its idpp[0], what the game writes while
// drawing and Particle_RenderList reads). The name of the first, NULL when
// none. Read only. In a crystal challenge on Rocky Road and Nitro Court the
// level's nitro boxes sit in MINE as well (RB_GenericMine.c:31) and count.
internal const char *DebugMenu_TourWeaponInView(const struct GameTracker *gGT)
{
	const int buckets[2] = {MINE, TRACKING};
	int b;

	for (b = 0; b < 2; b++)
	{
		const struct Thread *t;

		for (t = gGT->threadBuckets[buckets[b]].thread; t != NULL; t = t->siblingThread)
		{
			struct Instance *inst = t->inst;
			const struct InstDrawPerPlayer *idpp;

			if (((t->flags & THREAD_FLAG_DEAD) != 0) || (inst == NULL))
			{
				continue;
			}
			idpp = INST_GETIDPP(inst);
			if ((idpp[0].instFlags & DRAW_SUCCESSFUL) != 0)
			{
				return inst->name;
			}
		}
	}
	return NULL;
}

// The models of one --level-tour-oracle-see entry all in view 0: for each name
// an instance of that model (level instances and the instance pool, as
// ElimBG_ToggleInstance walks them) drawn in view 0 in the frame before
// (DRAW_SUCCESSFUL of its idpp[0]). Read only.
internal int DebugMenu_TourModelInView(struct GameTracker *gGT, const char *name)
{
	const struct Level *lev = gGT->level1;
	struct Instance *inst;
	u32 i;

	for (i = 0; (lev != NULL) && (lev->ptrInstDefs != NULL) && (i < lev->numInstances); i++)
	{
		inst = lev->ptrInstDefs[i].ptrInstance;
		if ((inst != NULL) && (inst->model != NULL) && (strncmp(inst->model->name, name, DBG_TOUR_SEE_NAME_BYTES) == 0))
		{
			const struct InstDrawPerPlayer *idpp = INST_GETIDPP(inst);

			if ((idpp[0].instFlags & DRAW_SUCCESSFUL) != 0)
			{
				return 1;
			}
		}
	}
	for (inst = (struct Instance *)gGT->JitPools.instance.taken.first; inst != NULL; inst = inst->next)
	{
		if ((inst->model != NULL) && (strncmp(inst->model->name, name, DBG_TOUR_SEE_NAME_BYTES) == 0))
		{
			const struct InstDrawPerPlayer *idpp = INST_GETIDPP(inst);

			if ((idpp[0].instFlags & DRAW_SUCCESSFUL) != 0)
			{
				return 1;
			}
		}
	}
	return 0;
}

internal int DebugMenu_TourSeeAll(struct GameTracker *gGT, int see)
{
	int k;

	for (k = 0; k < s_tourSeeCounts[see]; k++)
	{
		if (!DebugMenu_TourModelInView(gGT, s_tourSeeNames[see][k]))
		{
			return 0;
		}
	}
	return 1;
}

// --level-tour-oracle: the oracle frames of one race frame of an entry, armed in
// the game logic before the frame is drawn (Engine_OracleArmFrame; nothing else
// is touched; called after the drive sample of the frame). Besides the listed
// race frames, the entry's own, each found by reading game state:
// - start: the first race frame after the countdown (fly-in over, START_OF_RACE
//   clear, trafficLightsTimer down to 0; MainMain.c:362) - the grid with the
//   start banner ahead.
// - line-pre and line-post, around the start/finish line: the line is the race
//   frame in which the drive progress first folded a step forward
//   (EngineTour_DriveForwardFolds, EngineTour_DriveSample - the grid lies
//   behind the line, so it is crossed shortly after the start; the crossing
//   after lap 1 lies far behind the tour's frames). line-post is
//   DBG_TOUR_LINE_AFTER race frames after it, exactly. line-pre cannot look
//   back: it is the first race frame after the start in which the player, at
//   the speed of its last frame, reaches the line within DBG_TOUR_LINE_BEFORE
//   frames (EngineTour_LineSoon); the line's log line says how far ahead it
//   really was.
// - see1, see2 (--level-tour-oracle-see, that level only): the first race frame
//   after the start with all the entry's models in view, and the next one at
//   least DBG_TOUR_SEE_GAP frames later (another animation frame).
// - wpn: the first race frame after the start with a fired weapon in view
//   (DebugMenu_TourWeaponInView).
// One oracle frame per race frame, in this order: listed, start, line-pre,
// line-post, see, wpn; one that has to wait is armed in the next race frame.
// None on the entry's last race frame: the tour's VRAM dump of the entry is
// asked for there, and a frame holds one dump request (platform/engine_oracle.c). One
// snapshot of the frame (<--shot-name>-<tag>.bmp) when --shot-name is given and
// no burst shoots this frame.
internal void DebugMenu_TourOracle(struct GameTracker *gGT, int burst)
{
	extern char g_cfg_shotName[PLATFORM_ARG_PATH_MAX];
	const struct Driver *player = gGT->drivers[0];
	char kind[16];
	char tag[64];
	int see = -1;
	int i;

	for (i = 0; i < s_tourSeeCount; i++)
	{
		if (s_tourSeeLevels[i] == s_tourLevel)
		{
			see = i;
			break;
		}
	}

	if ((s_tourGoFrame == 0) && ((gGT->gameMode1 & START_OF_RACE) == 0) && (gGT->trafficLightsTimer <= 0))
	{
		s_tourGoFrame = s_tourFrames;
		s_tourOraclePending |= DBG_TOUR_ORACLE_START;
	}
	if ((s_tourGoFrame != 0) && (player != NULL))
	{
		const s32 dist = (s32)player->distanceToFinish_curr;

		if ((s_tourLineFrame == 0) && (EngineTour_DriveForwardFolds() > 0))
		{
			s_tourLineFrame = s_tourFrames;
			Platform_Log("[CTR Tour] %d/%d: level %d line at race frame %d; line-pre at %d (%d frame(s) ahead)\n", s_tourPos + 1, s_tourCount, s_tourLevel,
			             s_tourLineFrame, s_tourLinePreFrame, (s_tourLinePreFrame > 0) ? (s_tourLineFrame - s_tourLinePreFrame) : 0);
		}
		if ((s_tourLineFrame == 0) && (s_tourLinePreFrame == 0) && s_tourDistPrimed && EngineTour_LineSoon(dist, s_tourPrevDist - dist, DBG_TOUR_LINE_BEFORE))
		{
			s_tourLinePreFrame = s_tourFrames;
			s_tourOraclePending |= DBG_TOUR_ORACLE_LINEPRE;
		}
		s_tourPrevDist = dist;
		s_tourDistPrimed = 1;
	}
	if ((s_tourLineFrame != 0) && (s_tourFrames == s_tourLineFrame + DBG_TOUR_LINE_AFTER))
	{
		s_tourOraclePending |= DBG_TOUR_ORACLE_LINEPOST;
	}
	if ((s_tourGoFrame != 0) && (see >= 0) && (s_tourSeeFound < DBG_TOUR_SEE_FRAMES) && ((s_tourOraclePending & DBG_TOUR_ORACLE_SEE) == 0) &&
	    ((s_tourSeeFound == 0) || (s_tourFrames >= s_tourSeeFrames[s_tourSeeFound - 1] + DBG_TOUR_SEE_GAP)) && DebugMenu_TourSeeAll(gGT, see))
	{
		s_tourSeeFrames[s_tourSeeFound++] = s_tourFrames;
		s_tourOraclePending |= DBG_TOUR_ORACLE_SEE;
	}
	if ((s_tourGoFrame != 0) && (s_tourWpnFrame == 0))
	{
		const char *name = DebugMenu_TourWeaponInView(gGT);

		if (name != NULL)
		{
			s_tourWpnFrame = s_tourFrames;
			snprintf(s_tourWpnName, sizeof(s_tourWpnName), "%.16s", name);
			s_tourOraclePending |= DBG_TOUR_ORACLE_WPN;
		}
	}

	kind[0] = '\0';
	if (EngineTour_OracleAt(s_tourOracleLevels, s_tourOracleFrames, s_tourOracleCount, s_tourLevel, s_tourFrames))
	{
		snprintf(kind, sizeof(kind), "f%d", s_tourFrames);
	}
	else if ((s_tourOraclePending & DBG_TOUR_ORACLE_START) != 0)
	{
		s_tourOraclePending &= ~DBG_TOUR_ORACLE_START;
		snprintf(kind, sizeof(kind), "start");
	}
	else if ((s_tourOraclePending & DBG_TOUR_ORACLE_LINEPRE) != 0)
	{
		s_tourOraclePending &= ~DBG_TOUR_ORACLE_LINEPRE;
		snprintf(kind, sizeof(kind), "line-pre");
	}
	else if ((s_tourOraclePending & DBG_TOUR_ORACLE_LINEPOST) != 0)
	{
		s_tourOraclePending &= ~DBG_TOUR_ORACLE_LINEPOST;
		snprintf(kind, sizeof(kind), "line-post");
	}
	else if ((s_tourOraclePending & DBG_TOUR_ORACLE_SEE) != 0)
	{
		s_tourOraclePending &= ~DBG_TOUR_ORACLE_SEE;
		snprintf(kind, sizeof(kind), "see%d", s_tourSeeFound);
	}
	else if ((s_tourOraclePending & DBG_TOUR_ORACLE_WPN) != 0)
	{
		s_tourOraclePending &= ~DBG_TOUR_ORACLE_WPN;
		snprintf(kind, sizeof(kind), "wpn");
	}
	if ((kind[0] == '\0') || !EngineTour_OracleTag(tag, sizeof(tag), s_tourPos + 1, s_tourLevel, kind))
	{
		return;
	}
	if (s_tourFrames >= s_tourFramesThis)
	{
		s_tourOracleSkipped++;
		Platform_Log("[CTR Tour] %d/%d: level %d oracle %s NOT armed: race frame %d is the entry's last (its VRAM dump)\n", s_tourPos + 1, s_tourCount,
		             s_tourLevel, tag, s_tourFrames);
		return;
	}

	Engine_OracleArmFrame(tag, s_tourFrames);
	s_tourOracleArmed++;
	Platform_Log("[CTR Tour] %d/%d: level %d oracle %s at race frame %d, vblank %d%s%s\n", s_tourPos + 1, s_tourCount, s_tourLevel, tag, s_tourFrames,
	             Platform_GetVBlankCount(), (strcmp(kind, "wpn") == 0) ? ", weapon " : "", (strcmp(kind, "wpn") == 0) ? s_tourWpnName : "");
	if ((g_cfg_shotName[0] != '\0') && (burst < 0))
	{
		char path[PLATFORM_ARG_PATH_MAX + 64];

		if (EngineTour_OracleShotName(path, sizeof(path), g_cfg_shotName, tag))
		{
			Platform_ShotRequest(path);
		}
	}
}

// The burst being shot is over (its last frame, or the entry ended inside it):
// its line, with the VBlanks its snapshots were asked for in.
internal void DebugMenu_TourBurstClose(void)
{
	if (s_tourBurstOpen < 0)
	{
		return;
	}
	Platform_Log("[CTR Tour] %d/%d: level %d burst %d: race frames %d..%d at vblanks %d..%d\n", s_tourPos + 1, s_tourCount, s_tourLevel,
	             s_tourBurstOpen + 1, s_tourBurstFirstFrame, s_tourBurstLastFrame, s_tourBurstFirstVBlank, s_tourBurstLastVBlank);
	s_tourBurstOpen = -1;
}

// What one race frame of an entry measures (see the head of this section). Read
// only: the drivers, the level's start node and the frame counter.
internal void DebugMenu_TourMeasure(struct GameTracker *gGT, int now)
{
	extern char g_cfg_shotName[PLATFORM_ARG_PATH_MAX];
	extern int g_cfg_engine;
	const struct Driver *player = gGT->drivers[0];
	int burst;

	if (s_tourFrames == 1)
	{
		const struct Level *level = gGT->level1;
		u32 trackLength = 0;
		int overlapped = 0;

		s_tourBurstCount = EngineTour_BurstsForLevel(s_tourShotLevels, s_tourShotFrames, s_tourShotCount, s_tourLevel, s_tourBursts, DBG_TOUR_SHOTS_MAX,
		                                             &overlapped);
		s_tourBurstOpen = -1;
		if (overlapped > 0)
		{
			Platform_Log("[CTR Tour] %d/%d: level %d: %d burst start(s) inside an earlier burst left out\n", s_tourPos + 1, s_tourCount, s_tourLevel,
			             overlapped);
		}
		if ((level != NULL) && (level->ptr_restart_points != NULL) && (level->cnt_restart_points > 0))
		{
			trackLength = (u32)level->ptr_restart_points[0].distToFinish << DBG_TOUR_TRACK_LENGTH_SHIFT;
		}
		EngineTour_DriveBegin(trackLength);
		s_tourState = 0;
		s_tourLaps = 0;
		s_tourMaskGrabs = 0;
		s_tourGoFrame = 0;
		s_tourLinePreFrame = 0;
		s_tourLineFrame = 0;
		s_tourWpnFrame = 0;
		s_tourSeeFound = 0;
		s_tourOraclePending = 0;
		s_tourOracleArmed = 0;
		s_tourOracleSkipped = 0;
		s_tourDistPrimed = 0;
		s_tourWpnName[0] = '\0';
		Engine_TourNotify(s_tourPos + 1, s_tourLevel, 0);
	}

	// Snapshots: one per race frame inside a burst, taken at the end of this frame.
	burst = EngineTour_BurstOf(s_tourBursts, s_tourBurstCount, s_tourFrames);
	if (burst != s_tourBurstOpen)
	{
		DebugMenu_TourBurstClose();
	}
	if (burst >= 0)
	{
		char path[PLATFORM_ARG_PATH_MAX + 64];

		if (burst != s_tourBurstOpen)
		{
			s_tourBurstOpen = burst;
			s_tourBurstFirstFrame = s_tourFrames;
			s_tourBurstFirstVBlank = now;
		}
		s_tourBurstLastFrame = s_tourFrames;
		s_tourBurstLastVBlank = now;
		if (EngineTour_ShotName(path, sizeof(path), g_cfg_shotName, s_tourPos + 1, s_tourLevel, burst + 1, s_tourFrames))
		{
			Platform_ShotRequest(path);
		}
	}

	// Under --engine: where the water is on screen (a log line, nothing drawn or kept).
	if (g_cfg_engine && ((s_tourFrames % DBG_TOUR_WATER_PROBE_EVERY) == 0))
	{
		char tag[64];

		snprintf(tag, sizeof(tag), "tour %d level %d race frame %d", s_tourPos + 1, s_tourLevel, s_tourFrames);
		Engine_WaterProbeLog(gGT, 0, s_tourFrames, tag);
	}

	// The player's progress, from the end of the start sequence on.
	if ((player != NULL) && (s_tourFrames >= DBG_TOUR_DRIVE_FROM))
	{
		EngineTour_DriveSample(player->distanceToFinish_curr);
		s_tourLaps = (int)player->lapIndex;
		s_tourMaskGrabs = (int)player->numTimesMaskGrab;
	}

	// The fingerprint of this frame; the last race frame's is the entry's. Field by
	// field, never a whole struct (no padding bytes in it).
	{
		u32 hash = EngineTour_FnvStart();
		int seat;

		for (seat = 0; seat < 8; seat++)
		{
			const struct Driver *d = gGT->drivers[seat];
			s32 values[7];

			if (d == NULL)
			{
				continue;
			}
			values[0] = seat;
			values[1] = d->posCurr.x;
			values[2] = d->posCurr.y;
			values[3] = d->posCurr.z;
			values[4] = (s32)d->distanceToFinish_curr;
			values[5] = (s32)d->lapIndex;
			values[6] = (s32)d->numTimesMaskGrab;
			hash = EngineTour_Fnv(hash, values, (u32)sizeof(values));
		}
		s_tourState = hash;
	}

	// --level-tour-oracle: the oracle frames, after the drive sample (its line
	// event); game state read only.
	if (s_tourOracleCount > 0)
	{
		DebugMenu_TourOracle(gGT, burst);
	}
}

// The entry's measurement lines and the engine's reports after its last frame.
internal void DebugMenu_TourMeasureEnd(void)
{
	const int milli = EngineTour_DriveMilliLaps();
	const int magnitude = (milli < 0) ? -milli : milli;

	DebugMenu_TourBurstClose();
	Platform_Log("[CTR Tour] %d/%d: level %d drive: progress %s%d.%03d lap(s) from race frame %d, laps %d, longest stall %d frame(s), mask grabs %d, "
	             "state %08x\n",
	             s_tourPos + 1, s_tourCount, s_tourLevel, (milli < 0) ? "-" : "", magnitude / 1000, magnitude % 1000, DBG_TOUR_DRIVE_FROM, s_tourLaps,
	             EngineTour_DriveLongestStall(), s_tourMaskGrabs, (unsigned)s_tourState);
	if (s_tourOracleCount > 0)
	{
		// The entry's own oracle frames as found (0 = not reached), what was armed.
		Platform_Log("[CTR Tour] %d/%d: level %d oracle: %d frame(s) armed, %d not; start %d, line-pre %d, line %d, see %d/%d, wpn %d%s%s\n", s_tourPos + 1,
		             s_tourCount, s_tourLevel, s_tourOracleArmed, s_tourOracleSkipped, s_tourGoFrame, s_tourLinePreFrame, s_tourLineFrame,
		             (s_tourSeeFound > 0) ? s_tourSeeFrames[0] : 0, (s_tourSeeFound > 1) ? s_tourSeeFrames[1] : 0, s_tourWpnFrame,
		             (s_tourOraclePending != 0) ? " (found too late to be armed)" : "", (s_tourWpnFrame == 0) ? "; no weapon in view" : "");
	}
	Engine_TourNotify(s_tourPos + 1, s_tourLevel, 1);
}

// The entry is over - its frames are driven, or the game left the race by
// itself. Dump, then back to the menu or, after the last entry, to the end.
// leftByItself: the game is already on its way to the menu (or in a demo), and
// only a demo still needs the QUIT road.
internal void DebugMenu_TourFinish(struct GameTracker *gGT, int now, int leftByItself)
{
	DebugMenu_TourDump(&s_tour[s_tourPos]);
	DebugMenu_TourMeasureEnd();

	if (leftByItself)
	{
		Platform_Log("[CTR Tour] %d/%d: level %d ended early after %d race frame(s), at vblank %d\n", s_tourPos + 1, s_tourCount, s_tourLevel,
		             s_tourFrames, now);
	}
	else
	{
		Platform_Log("[CTR Tour] %d/%d: level %d done, %d race frame(s), at vblank %d\n", s_tourPos + 1, s_tourCount, s_tourLevel, s_tourFrames, now);
	}

	s_tourDriven++;
	s_tourPos++;

	if (s_tourPos >= s_tourCount)
	{
		// The dump is written at the end of this frame; the run ends in the
		// next one, so the last track's dump is not lost.
		Platform_Log("[CTR Tour] done: %d driven, %d skipped at vblank %d\n", s_tourDriven, s_tourSkipped, now);
		s_tourPhase = DBG_TOUR_PHASE_QUIT;
		return;
	}

	if ((gGT->gameMode1 & MAIN_MENU) == 0)
	{
		DebugMenu_TourLeave(gGT);
	}

	s_tourPhase = DBG_TOUR_PHASE_MENU;
	DebugMenu_TourProgress(now);
}

// On the road: count race frames, and after the last one finish the entry.
//
// No "seen out of the race first" gate. It was meant to keep frames of the old
// level under the flag from counting, but this function never sees those: the
// game logic, and with it DebugMenu_Frame, is skipped while LOADING is set
// (MainMain.c, `if ((gGT->gameMode1 & LOADING) == 0)` around
// MainFrame_GameLogic), and when the flag already covers the menu at the start,
// the next call is the first race frame - the gate never opened and nothing
// was counted. It is not needed either: every entry starts from the main menu,
// and until the new level is loaded either MAIN_MENU is set or Loading.stage is
// not idle (MainRaceTrack_RequestLoad sets LOAD_REQUESTED in the frame of the
// start), so inRace cannot be true for the old level. A demo is not a race.
//
// Once counting has begun, the main menu or a demo means the game left the race
// by itself (a crystal time limit, an end box that went on): the entry ends
// there, with its own line. Any other gap - a load, a restart - is waited for;
// the deadline runs from the last race frame.
internal void DebugMenu_TourDriveStep(struct GameTracker *gGT, int now, int inRace)
{
	if (!inRace)
	{
		if ((s_tourFrames > 0) && (((gGT->gameMode1 & MAIN_MENU) != 0) || (gGT->boolDemoMode != 0)))
		{
			DebugMenu_TourFinish(gGT, now, 1);
		}
		return;
	}

	s_tourFrames++;
	DebugMenu_TourProgress(now);
	DebugMenu_TourMeasure(gGT, now);

	// The entry's count (DebugMenu_TourMenuStep: --level-tour-frames, or its L<level>@ count).
	if (s_tourFrames >= s_tourFramesThis)
	{
		DebugMenu_TourFinish(gGT, now, 0);
	}
}

// One step per game frame. 1 = a track was started this frame (the caller
// ends the frame's debug work there, as --level does). Without --level-tour it
// only reads two words and returns 0.
internal int DebugMenu_TourTick(struct GameTracker *gGT)
{
	extern char g_cfg_levelTour[1024];
	const int now = Platform_GetVBlankCount();
	const int menuUp = ((gGT->gameMode1 & MAIN_MENU) != 0) && (sdata->ptrActiveMenu != NULL) && (sdata->Loading.stage == LOAD_IDLE);
	const int inRace = ((gGT->gameMode1 & MAIN_MENU) == 0) && (gGT->levelID != MAIN_MENU_LEVEL) && (gGT->boolDemoMode == 0) &&
	                   (sdata->Loading.stage == LOAD_IDLE) && (sdata->load_inProgress == 0);
	int started = 0;

	if ((g_cfg_levelTour[0] == '\0') || (s_tourPhase == DBG_TOUR_PHASE_OFF))
	{
		return 0;
	}

	if (s_tourPhase == DBG_TOUR_PHASE_READ)
	{
		// The watch starts with the first game frame of the run.
		if (!s_tourWatched)
		{
			s_tourWatched = 1;
			DebugMenu_TourProgress(now);
		}

		if (!menuUp)
		{
			return 0;
		}

		DebugMenu_TourRead();
		s_tourPhase = DBG_TOUR_PHASE_MENU;
		DebugMenu_TourProgress(now);
	}

	if ((s_tourPhase == DBG_TOUR_PHASE_MENU) && (s_tourPos >= s_tourCount))
	{
		// An empty list, or the entries left could not be started: no dump is
		// waiting, the run ends in this frame.
		Platform_Log("[CTR Tour] done: %d driven, %d skipped at vblank %d\n", s_tourDriven, s_tourSkipped, now);
		s_tourPhase = DBG_TOUR_PHASE_QUIT;
	}

	if (s_tourPhase == DBG_TOUR_PHASE_QUIT)
	{
		s_tourPhase = DBG_TOUR_PHASE_OFF;
		Platform_SetDeadline(-1, NULL);
		Platform_QuitGame("--level-tour");
	}
	else if (s_tourPhase == DBG_TOUR_PHASE_MENU)
	{
		started = DebugMenu_TourMenuStep(gGT, now, menuUp);
	}
	else
	{
		DebugMenu_TourDriveStep(gGT, now, inRace);
	}

	return started;
}

// --dev-cup: the run's cup, from the main menu to the podium and back.
//
// THE START: the moment of --level (main menu up, a menu on screen) and its
// funnel, DebugMenu_JumpToLevel, which sets the cup up for --dev-cup, with the
// cup's first track (data.ArcadeCups, the table MM_CupSelect.c:83 reads). From
// there the game goes its own way: UI_CupStandings loads races 2 to 4
// (UI_CupStandings.c:646) and then the podium (GEM_STONE_VALLEY, :810), and
// the CROSS on the podium loads the main menu (CS_Camera.c:328). After the
// jump nothing here writes game state: it reads, logs, asks for snapshots and
// ends the run once the main menu is back.
//
// THE SCREENS of a race, from the game's own state: race (the player's seat has
// not finished), finish (finished, the standings not yet up), results (the cup
// standings are drawn - HUD_FLAG_CUP_STANDINGS without the race HUD,
// MainFrame_RenderFrame.c:546-550 - first page, the finishing order), points
// (its page bit UI_CUP_STANDINGS_PAGE_TRACK_POINTS: this race's points and the
// cup's; after race 4 the final standings), then the podium. A screen never
// goes back to an earlier one within its race. Per screen one line when it
// begins and one when it ends: its frames, in how many of them the level was
// drawn (RENDER_FLAG_DRAW_LEVEL) and in how many the engine drew the track
// (Engine_TrackDrawnThisFrame - read in the game logic, so for the frame made
// last): which path drew the track behind each screen.
#define DBG_CUP_RACES           4   // UI_CUP_STANDINGS_TRACKS_PER_CUP
#define DBG_CUP_PAGE_POINTS     0x4 // UI_CUP_STANDINGS_PAGE_TRACK_POINTS (UI_CupStandings.c:11)
#define DBG_CUP_SHOT_PAGE_FRAME 48  // framesSinceRaceEnded: the page is up; a CROSS before 60 only skips to 60 (UI_CupStandings.c:209-215)
#define DBG_CUP_SHOT_RACE_FRAME 300 // race frames of the race snapshot, past the countdown
#define DBG_CUP_SHOT_FINISH     30  // frames after the player's finish
enum
{
	DBG_CUP_NONE,
	DBG_CUP_RACE,
	DBG_CUP_FINISH,
	DBG_CUP_RESULTS,
	DBG_CUP_POINTS,
	DBG_CUP_PODIUM,
	DBG_CUP_PHASES
};
global_variable const char *const s_devCupPhaseNames[DBG_CUP_PHASES] = {"none", "race", "finish", "results", "points", "podium"};
// Podium frames of the snapshots: the camera pan, where the CROSS of the script
// is not yet heard (CS_Camera.c:314), and after it.
global_variable const int s_devCupPodiumShots[] = {30, 90, 180};
global_variable int s_devCupStarted = 0;
global_variable int s_devCupRace = 0;      // races begun (1..4)
global_variable int s_devCupLevel = -1;    // level of the race being counted
global_variable int s_devCupPhase = DBG_CUP_NONE;
global_variable int s_devCupPhaseFrames = 0;
global_variable int s_devCupPhaseLevelDrawn = 0;
global_variable int s_devCupPhaseEngine = 0;
global_variable int s_devCupPhaseVBlank = 0;
global_variable int s_devCupShotsTaken = 0; // bits: race, finish, results, points of the race being counted
global_variable int s_devCupPodiumFrames = 0;
global_variable int s_devCupPodiumEngine = 0;
// platform/engine_track.c: 1 = the engine submitted the track in the frame made last.
int Engine_TrackDrawnThisFrame(void);

internal void DebugMenu_DevCupShot(const char *tag)
{
	extern char g_cfg_shotName[PLATFORM_ARG_PATH_MAX];
	char path[PLATFORM_ARG_PATH_MAX + 64];

	if ((g_cfg_shotName[0] != '\0') && EngineTour_OracleShotName(path, sizeof(path), g_cfg_shotName, tag))
	{
		Platform_ShotRequest(path);
		Platform_Log("[CTR Cup] dev: snapshot %s asked for at vblank %d\n", tag, Platform_GetVBlankCount());
	}
}

internal void DebugMenu_DevCupPhaseEnd(void)
{
	if (s_devCupPhase == DBG_CUP_NONE)
	{
		return;
	}
	if (s_devCupPhase == DBG_CUP_PODIUM)
	{
		Platform_Log("[CTR Cup] dev: podium ends: %d frame(s) from vblank %d, level drawn in %d, the engine drew the track in %d\n",
		             s_devCupPhaseFrames, s_devCupPhaseVBlank, s_devCupPhaseLevelDrawn, s_devCupPhaseEngine);
	}
	else
	{
		Platform_Log("[CTR Cup] dev: race %d %s ends: %d frame(s) from vblank %d, level drawn in %d, the engine drew the track in %d\n",
		             s_devCupRace, s_devCupPhaseNames[s_devCupPhase], s_devCupPhaseFrames, s_devCupPhaseVBlank, s_devCupPhaseLevelDrawn,
		             s_devCupPhaseEngine);
	}
	s_devCupPhase = DBG_CUP_NONE;
}

// 1 = the cup was started this frame (the caller returns, as after --level).
internal int DebugMenu_DevCupTick(struct GameTracker *gGT)
{
	extern int g_cfg_jumpDriver;
	struct Driver *seat;
	int phase;

	if (g_cfg_devCup < 0)
	{
		return 0;
	}

	if (!s_devCupStarted)
	{
		if (((gGT->gameMode1 & MAIN_MENU) != 0) && (sdata->ptrActiveMenu != NULL))
		{
			const int levelID = data.ArcadeCups[g_cfg_devCup].CupTrack[0].trackID;

			s_devCupStarted = 1;
			Platform_Log("[CTR Cup] dev: --dev-cup %d jumps to race 1 on level %d (cup levels %d %d %d %d) at vblank %d\n", g_cfg_devCup, levelID,
			             (int)data.ArcadeCups[g_cfg_devCup].CupTrack[0].trackID, (int)data.ArcadeCups[g_cfg_devCup].CupTrack[1].trackID,
			             (int)data.ArcadeCups[g_cfg_devCup].CupTrack[2].trackID, (int)data.ArcadeCups[g_cfg_devCup].CupTrack[3].trackID,
			             Platform_GetVBlankCount());
			DebugMenu_JumpToLevel(gGT, levelID, g_cfg_jumpDriver);
			return 1;
		}
		return 0;
	}

	if (sdata->load_inProgress != 0)
	{
		return 0;
	}

	// Back at the main menu after a race: the cup is over, the run too.
	if ((gGT->gameMode1 & MAIN_MENU) != 0)
	{
		if ((s_devCupRace > 0) && (sdata->ptrActiveMenu != NULL))
		{
			DebugMenu_DevCupPhaseEnd();
			Platform_Log("[CTR Cup] dev: back at the main menu at vblank %d - %d race(s), podium %d frame(s) (the engine drew the track in %d)\n",
			             Platform_GetVBlankCount(), s_devCupRace, s_devCupPodiumFrames, s_devCupPodiumEngine);
			Platform_QuitGame("--dev-cup");
		}
		return 0;
	}

	seat = gGT->drivers[0];
	if (gGT->levelID == GEM_STONE_VALLEY)
	{
		phase = DBG_CUP_PODIUM;
	}
	else
	{
		if (gGT->levelID != s_devCupLevel)
		{
			// A new race: the cup has loaded its next track.
			DebugMenu_DevCupPhaseEnd();
			s_devCupLevel = gGT->levelID;
			s_devCupRace++;
			s_devCupShotsTaken = 0;
			Platform_Log("[CTR Cup] dev: race %d of %d on level %d: cup %d track index %d, laps %d, difficulty %d, cup bit %s, at vblank %d\n",
			             s_devCupRace, DBG_CUP_RACES, (int)gGT->levelID, gGT->cup.cupID, gGT->cup.trackIndex, (int)gGT->numLaps, gGT->arcadeDifficulty,
			             ((gGT->gameMode2 & CUP_ANY_KIND) != 0) ? "on" : "OFF", Platform_GetVBlankCount());
		}
		if (((gGT->hudFlags & HUD_FLAG_CUP_STANDINGS) != 0) && ((gGT->hudFlags & HUD_FLAG_RACE_HUD) == 0))
		{
			phase = ((sdata->menuReadyToPass & DBG_CUP_PAGE_POINTS) != 0) ? DBG_CUP_POINTS : DBG_CUP_RESULTS;
		}
		else if ((seat != NULL) && ((seat->actionsFlagSet & ACTION_RACE_FINISHED) != 0))
		{
			phase = DBG_CUP_FINISH;
		}
		else
		{
			phase = DBG_CUP_RACE;
		}
		if (phase < s_devCupPhase)
		{
			phase = s_devCupPhase;
		}
	}

	if (phase != s_devCupPhase)
	{
		DebugMenu_DevCupPhaseEnd();
		s_devCupPhase = phase;
		s_devCupPhaseFrames = 0;
		s_devCupPhaseLevelDrawn = 0;
		s_devCupPhaseEngine = 0;
		s_devCupPhaseVBlank = Platform_GetVBlankCount();
		if (phase == DBG_CUP_PODIUM)
		{
			// The final order as UI_CupStandings_FinalizeCupRanks left it (rank ->
			// seat, UI_CupStandings.c:672-688); the drivers of the races are gone.
			int rank = 0;
			int i;

			for (i = 0; i < (int)(sizeof(data.cupPositionPerPlayer) / sizeof(data.cupPositionPerPlayer[0])); i++)
			{
				if (data.cupPositionPerPlayer[i] == 0)
				{
					rank = i + 1;
					break;
				}
			}
			Platform_Log("[CTR Cup] dev: podium on level %d at vblank %d: player's cup rank %d, reward model %d, save prompt %s\n", (int)gGT->levelID,
			             s_devCupPhaseVBlank, rank, (int)gGT->podiumRewardID, ((gGT->gameMode2 & CUP_NEW_WIN) != 0) ? "pending (a new cup win)" : "none");
		}
		else
		{
			Platform_Log("[CTR Cup] dev: race %d %s begins at vblank %d (player's place %d)\n", s_devCupRace, s_devCupPhaseNames[phase], s_devCupPhaseVBlank,
			             (seat != NULL) ? (int)seat->driverRank + 1 : 0);
		}
	}

	s_devCupPhaseFrames++;
	if ((gGT->renderFlags & RENDER_FLAG_DRAW_LEVEL) != 0)
	{
		s_devCupPhaseLevelDrawn++;
	}
	if (Engine_TrackDrawnThisFrame())
	{
		s_devCupPhaseEngine++;
	}

	if (phase == DBG_CUP_PODIUM)
	{
		int i;

		s_devCupPodiumFrames = s_devCupPhaseFrames;
		s_devCupPodiumEngine = s_devCupPhaseEngine;
		for (i = 0; i < (int)(sizeof(s_devCupPodiumShots) / sizeof(s_devCupPodiumShots[0])); i++)
		{
			if (s_devCupPhaseFrames == s_devCupPodiumShots[i])
			{
				char tag[32];

				snprintf(tag, sizeof(tag), "cup-podium-f%03d", s_devCupPodiumShots[i]);
				DebugMenu_DevCupShot(tag);
			}
		}
	}
	else
	{
		const int bit = 1 << phase;
		int due = 0;

		if ((phase == DBG_CUP_RACE) && (s_devCupPhaseFrames == DBG_CUP_SHOT_RACE_FRAME))
		{
			due = 1;
		}
		else if ((phase == DBG_CUP_FINISH) && (s_devCupPhaseFrames == DBG_CUP_SHOT_FINISH))
		{
			due = 1;
		}
		else if (((phase == DBG_CUP_RESULTS) || (phase == DBG_CUP_POINTS)) && (sdata->framesSinceRaceEnded >= DBG_CUP_SHOT_PAGE_FRAME))
		{
			due = 1;
		}
		if (due && ((s_devCupShotsTaken & bit) == 0))
		{
			char tag[32];

			s_devCupShotsTaken |= bit;
			snprintf(tag, sizeof(tag), "cup-r%d-%s", s_devCupRace, s_devCupPhaseNames[phase]);
			DebugMenu_DevCupShot(tag);
		}
	}

	return 0;
}

// The portrait strip counter (platform/native_renderer.c) starts at the first
// game frame; the boot clear before it is counted on its own.
void NativeRenderer_StripArm(void);

void DebugMenu_Frame(struct GameTracker *gGT, struct GamepadSystem *gGamepads)
{
	NativeRenderer_StripArm();

	// The key sequence first: it must have set the pad bus before anything
	// in this frame reads it.
	DebugMenu_MenuKeysTick();
	DebugMenu_KartSteerTick(gGT);
	DebugMenu_DriveLogTick(gGT);

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

			// The play mods of the MODS page, one line, only when one is active.
			NativeMods_LogRace();
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

		// --dev-tt-ghost: one line when a ghost really drives (GhostReplay_Init2
		// sets the flag only for a tape it starts), so the log proves the race had it.
		if (g_cfg_devTtGhost && ((gGT->gameMode1 & MAIN_MENU) == 0) && (sdata->boolGhostsDrawing != 0))
		{
			local_persist int ghostSeen = 0;

			if (!ghostSeen)
			{
				ghostSeen = 1;
				Platform_Log("[CTR Debug] --dev-tt-ghost: a ghost drives on level %d (from vblank %d)\n", gGT->levelID, Platform_GetVBlankCount());
			}
		}

		// --dev-cup N: the cup from the main menu to the podium, see DebugMenu_DevCupTick.
		if (DebugMenu_DevCupTick(gGT))
		{
			return;
		}

		// --level-tour LIST: one track after another, see DebugMenu_TourTick.
		if (DebugMenu_TourTick(gGT))
		{
			return;
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
	// Cleared means at rest, DBG_STICK_REST, not 0: with 0 the kart under an
	// open menu got full gas and a full left turn and drove off on its own.
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

			seat->stickLX = DBG_STICK_REST;
			seat->stickLY = DBG_STICK_REST;
			seat->stickLX_dontUse1 = DBG_STICK_REST;
			seat->stickLY_dontUse1 = DBG_STICK_REST;
			seat->stickRX = DBG_STICK_REST;
			seat->stickRY = DBG_STICK_REST;
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
