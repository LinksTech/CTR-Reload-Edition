#ifndef NATIVE_ASSETS_H
#define NATIVE_ASSETS_H

#include <stddef.h>
#include <stdio.h>

#include "platform/native_str8.h"

enum NativeAssetReadMode
{
	NATIVE_ASSET_READ_DATA_FILE,
	NATIVE_ASSET_READ_RAW_CD_SECTORS,
};

struct NativeAssetsByteBuffer
{
	u8 *data;
	int size;
};

int NativeAssets_Init(const char *executableBasePath);
const char *NativeAssets_GetBaseDir(void);
const char *NativeAssets_GetAssetDir(void);
int NativeAssets_BuildPathStr8(NativeStr8 relativePath, char *dst, size_t dstSize);
int NativeAssets_BuildPath(const char *relativePath, char *dst, size_t dstSize);
int NativeAssets_ResolvePathStr8(NativeStr8 relativePath, char *dst, size_t dstSize);
int NativeAssets_ResolvePath(const char *relativePath, char *dst, size_t dstSize);
FILE *NativeAssets_OpenHostStr8(NativeStr8 relativePath, const char *mode);
FILE *NativeAssets_OpenHost(const char *relativePath, const char *mode);
FILE *NativeAssets_OpenHostBigfile(const char *mode);
int NativeAssets_ReadBytes(const char *path, int readMode, struct NativeAssetsByteBuffer *bytes);
void NativeAssets_FreeBytes(struct NativeAssetsByteBuffer *bytes);
int NativeAssets_Validate(void);

// --- Track containers -------------------------------------------------------
//
// The read path for .rldtrack, on by default (--no-tracks turns it off). See
// the long section in native_assets.c: nothing is unpacked, and the list does
// not touch LEVD and VRMD.

struct NativeTrackEntry
{
	char file[128];
	char path[1024];

	// 0 means: the file is there, but it is not usable. `problem` then carries
	// the reason in plain text. It does NOT drop out of the list - whoever puts
	// a file there and never finds it again blames the game.
	int ok;
	const char *problem;

	// Room for a message that only comes into being at run time - `problem`
	// then points here. A list of missing tables cannot be written down as a
	// literal.
	char note[192];

	// WHY refused, as a kind (enum NativeTrackRefusal): the list picks its
	// keyword from it. `problem` stays the exact text.
	int refusal;

	// The version from the header and unknown required flags - for the
	// log line that names both numbers. 0 if the header was not readable.
	u32 formatMajor;
	u32 formatMinor;
	u32 unknownFlags;

	// What META holds - only what the reader really needs.
	//
	// The name in META is display, not identity: the game tells two
	// containers apart by their FILE NAME (track-ids.tsv).
	char name[513];
	char author[513];
	u32 trackVersion;
	u32 metaVersion;

	// Which modes the track offers, RLD_MODE_*. The player is only offered
	// what is listed here AND is built for containers (NativeTrack_WhyNoRace).
	u32 modes;

	// How much memory this track needs, computed by the packer from the LEV.
	// Stored in META so the list knows it without touching LEVD.
	u32 primBytes;
	u32 memTotal;

	// The SHA-256 of the LEVD, from the container's chunk directory - it is
	// stored there anyway, for the integrity check at load time. Here it is
	// the track's version stamp: if it changes, the file is a new
	// version, and the level ID registry says so. 0 flag if the
	// file does not even have a readable directory with LEVD.
	u8 levdSha[32];
	int levdShaOk;
};

// The modes as a readable line, e.g. "Race, Time Trial". Never NULL; for 0 it
// does not say nothing, but that the track declares none.
//
// Here and not directly from rldtrack.inc: native_assets.c is the only
// translation unit that pulls in the format.
const char *NativeTrack_ModesText(u32 modes);

// THE OFFER RULE (format 4.1): the game offers a container only the modes
// it DECLARES and that are BUILT for containers
// (RLD_MODES_PLAYABLE in rldtrack.inc) - never an undeclared one.
// Built today: Race (custom track and cup in NITRO-PIT), Crystal Challenge
// and CTR Challenge.
//
// NULL means: this row offers a race. Otherwise the reason as log text,
// and in text the keyword (at most 12 characters, for the wheel and the cup)
// and up to four lines of at most 12 characters for the preview window.
// The order: refused, no level ID, Race not declared.
//
// Not offered does not mean refused: a container without Race stays
// valid, counts toward the memory reserve and keeps its level ID.
enum NativeTrackRefusal
{
	NATIVE_TRACK_REFUSAL_NONE = 0,
	NATIVE_TRACK_REFUSAL_NEWER,   // major version > 4 or unknown required flag
	NATIVE_TRACK_REFUSAL_OLDER,   // major version < 4
	NATIVE_TRACK_REFUSAL_DAMAGED, // anything else while reading
	NATIVE_TRACK_REFUSAL_MEMORY,  // META states less memory than the LEV needs
};

#define NATIVE_TRACK_REFUSAL_LINES 4

struct NativeTrackRefusalText
{
	const char *tag;
	const char *lines[NATIVE_TRACK_REFUSAL_LINES];
	int lineCount;
};

const char *NativeTrack_WhyNoRace(int index, struct NativeTrackRefusalText *text);
int NativeTrack_OffersRace(int index);
// 1: valid, but without Race - not in the RACE list (only in the log).
int NativeTrack_HiddenFromRace(int index);

// How many rows offer a race. 0 means: the custom track row in NITRO-PIT
// stays locked.
int NativeTrack_CountRaceOffered(void);

// THE SAME RULE FOR EVERY MODE (CRYSTAL and CTR in NITRO-PIT). The
// game side names the mode by name; which bit from rldtrack.inc that is,
// only native_assets.c knows (see NativeTrack_ModesText). The three
// Race functions above are these with NATIVE_TRACK_MODE_RACE.
enum NativeTrackMode
{
	NATIVE_TRACK_MODE_RACE = 0,
	NATIVE_TRACK_MODE_CRYSTAL = 1,
	NATIVE_TRACK_MODE_CTR = 2,
};
const char *NativeTrack_WhyNotOffered(int index, int mode, struct NativeTrackRefusalText *text);
int NativeTrack_HiddenFrom(int index, int mode);
int NativeTrack_CountOffered(int mode);

int NativeTrack_Scan(void);

// A different folder instead of tracks/ (--tracks-dir, reference runs). Relative
// to the game's base like tracks/, or absolute. Must be set before the first scan.
void NativeTrack_SetFolder(const char *folder);

// How much memory must be placed behind the pack's window so that EVERY
// track in tracks/ fits - the largest that any of the containers
// asks for. 0 means: none, or none that is usable.
//
// Must be answered before MEMPACK_Init, because after that the split is
// fixed. And the player only picks the track much later, so it cannot
// be the one chosen, only the largest present.
u32 NativeTrack_MempackExtraNeeded(void);

// The primitive memory the LOADED track needs, without the sky.
// 0 if no container is loaded. Counterpart of NativeTrack_SkyPrimBytes.
u32 NativeTrack_PrimBytes(void);

// The clip buffer per player that the LOADED track needs (near plane,
// clip sets). 0 if no container is loaded. The reserve for it
// is included in NativeTrack_MempackExtraNeeded, four times, because
// data.PtrClipBuffer has four slots.
#define NATIVE_TRACK_CLIP_BUFFERS 4u
u32 NativeTrack_ClipBytes(void);
int NativeTrack_Count(void);
const struct NativeTrackEntry *NativeTrack_Get(int index);

// Path of the track preview <folder>/vorschau/<file>.rldprev (platform/native_preview.c);
// dirPath receives the vorschau folder. 0 as long as no scan has run.
int NativeTrack_PreviewPath(int index, char *path, int pathSize, char *dirPath, int dirPathSize);
int NativeTrack_Load(int index, int donorLevelID);
int NativeTrack_LoadedIndex(void);

// --- Run-time level IDs --------------------------------------------------
//
// Every track in the list gets its own level ID at scan time from the band
// 65..99: after SCRAPBOOK (64), before the synthetic adventure cup base
// (100, namespace_Level.h). The IDs live ABOVE the loader: the menu and
// gGT->currLEV compute with them, and at the funnel MainRaceTrack_RequestLoad
// they are translated into the donor slot (MM_NativeTracks_TranslateLevel).
// The engine and every retail table still only see the donor slot - an
// ID from this band must never arrive there.
//
// They are persistent through NATIVE_TRACK_ID_FILE in the scanned track folder:
// per line ID, LEVD SHA-256 as version stamp, file name. A known
// file keeps its ID, a new one gets the smallest free one, a missing one
// releases its ID. Matching is by file name, case-
// insensitive - renaming is a new track. Under the settings lock
// (--settings-defaults, every measurement run) the file is neither read nor
// written; the assignment then happens purely in memory and, thanks to the
// sorted list, is still the same for the same folder contents.
#define NATIVE_TRACK_LEVELID_FIRST 65
#define NATIVE_TRACK_LEVELID_COUNT 35
#define NATIVE_TRACK_ID_FILE       "track-ids.tsv"

// The run-time ID of a row of the list, or -1 if it has none (more
// containers than the band holds - the scan writes the message for it).
int NativeTrack_LevelForIndex(int index);

// The list index behind a run-time ID, or -1. -1 means: no file stands behind
// this ID in this run - whoever loads it anyway has remembered it,
// and that is exactly what nobody should do (see the funnel).
int NativeTrack_IndexForLevel(int levelID);

// --- Custom cups: cups.txt -----------------------------------------------
//
// NATIVE_CUP_FILE in the scanned track folder, next to track-ids.tsv. A cup
// is a name and four containers, named by their FILE NAME - the same
// identifier as in the registry, so that renaming or repacking does not create
// a second truth. The format, human-readable, like track.txt
// of rldpack make:
//
//   # comment
//   cup   = My Cup
//   track = Baby_T_Park.rldtrack
//   track = Ice_Rink.rldtrack
//   track = Inferno_Island.rldtrack
//   track = Sids_House.rldtrack
//
// A cup starts with "cup =" and has exactly four "track =" lines. A
// broken line (unknown key, no "=", empty value, too long, too
// many or too few tracks) is reported with its line number, the cup it
// belongs to is dropped, the rest stays valid. A container that is missing,
// that the reader rejected, that got no ID or that does not declare Race
// makes its cup GRAY: it is shown on screen, cannot be selected, and the log
// says why.
//
// It is read under --settings-defaults too: the file is content of the
// folder like the containers, not a setting. It is never written.
#define NATIVE_CUP_FILE     "cups.txt"
#define NATIVE_CUP_MAX      4
#define NATIVE_CUP_TRACKS   4
#define NATIVE_CUP_NAME_MAX 48

struct NativeCup
{
	char name[NATIVE_CUP_NAME_MAX];

	// The line of "cup =" in cups.txt, for every message.
	int line;

	// Per track: the file name as in cups.txt, the list index and the
	// run-time ID (65..99) - both -1 if the container is missing.
	char file[NATIVE_CUP_TRACKS][128];
	int index[NATIVE_CUP_TRACKS];
	int levelID[NATIVE_CUP_TRACKS];

	// 1 if all four containers are there, usable and have an ID. Otherwise gray,
	// and problem says why.
	int ok;
	char problem[192];
};

// How many cups cups.txt holds (gray ones included), 0 if no file, an
// empty one or only broken ones. 0 means: the cup row in NITRO-PIT stays
// locked.
int NativeCup_Count(void);
const struct NativeCup *NativeCup_Get(int index);

// THE MINIMAP OF A CONTAINER, without loading it (track screen of
// NITRO-PIT). half[0] is icon 3 (upper half), half[1] icon 4
// (lower), like gGT->ptrIcons[3]/[4] in the race.
struct NativeTrackMinimapHalf
{
	// TextureLayout as stored in the LEV: u0 v0 clut u1 v1 tpage u2 v2 u3 v3.
	u8 layout[12];

	// 0 = 4 bit, 1 = 8 bit, 2 = 15 bit (tpage bits 7-8).
	int depth;

	// The smallest rectangle in the track's VRAM that holds all four UV corners,
	// in halfwords, and the UV that lies at its top left corner.
	int srcX, srcY, w, h;
	int texelBaseU, texelBaseV;
	u16 *texels;

	// The color table: 16 entries at 4 bit, 256 at 8 bit, none at 15 bit.
	int clutX, clutY, clutW;
	u16 clut[256];
};

struct NativeTrackMinimap
{
	struct NativeTrackMinimapHalf half[2];

	// 1: scaled down (Rld_ScaleMap) - the UVs start at the corner of its own
	// texels (texelBase 0), clut and page are assigned by the menu.
	// 0: from LEV and VRM, as in the race.
	int scaled;

	// What reading cost: file, SHA-256, map, scaling - for the
	// menu's log line (in-game timing measurement).
	u32 microseconds;
};

// 1 if the container brings both map halves. Otherwise 0, and *why says
// why; out is then empty. What was read is released by NativeTrack_FreeMinimap.
// Since 4.1: always from LEV and VRM; if the map does not fit into the menu's
// strip, Rld_ScaleMap scales it down here (whether it fits in the end is checked
// by the menu). An MMAP in old containers is skipped.
int NativeTrack_ReadMinimap(int index, struct NativeTrackMinimap *out, const char **why);
void NativeTrack_FreeMinimap(struct NativeTrackMinimap *map);

int NativeTrack_LoadedDonorLevel(void);
u32 NativeTrack_SkyPrimBytes(void);
int NativeTrack_ActiveForLevel(int levelID);
const char *NativeTrack_LoadedName(void);
void NativeTrack_Release(void);
void NativeTrack_ArmSubfiles(int levelID, int levSubfile, int vramSubfile);
const u8 *NativeTrack_SubfileData(int subfileIndex, u32 *sizeOut);

// THE CONTAINER SELF-TEST (--selftest-containers, main.c).
//
// Runs on one file every check the game runs on a container - at scan time
// (header, directory, META, the map for the track wheel, the memory numbers)
// and at load time (every chunk read, the LEVD/VRMD/SNDB content checks, PARM,
// the memory need) - without touching game memory: no MEMPACK, no engine, no
// GPU, no window, nothing in the track list. 1 = the game would list and load
// it, 0 = refused; why receives the reason, the same text the game logs after
// "REJECTED - " or "NOT LOADED - ". Never crashes and leaks nothing, whatever
// the file holds. The SNDB bank sums use the host's SPU sizes only once
// KART.HWL has been read (never in the self-test); without them a row the
// container does not correct counts 0.
int NativeTrack_SelfTestFile(const char *path, char *why, int whyBytes);

// Every *.rldtrack in dir, sorted like the track list: good-* expected
// accepted, bad-* expected refused, one "[selftest] ..." line per file on
// stdout. Returns the exit code: 0 only if every file is as expected and there
// is at least one good- and one bad- file.
int NativeTrack_SelfTestFolder(const char *dir);

// 1 and the SHA-256 of the LEVD chunk of the loaded container track, if the
// level LOAD_TenStages last armed (the one loading or running) is its seat;
// 0 when that level is not a container track.
int NativeTrack_ActiveLevdSha256(unsigned char out[32]);

// THE VALUES OF A CONTAINER TRACK (PARM, format 4.1).
//
// For each question -1 if this level is not the slot of a loaded container -
// then the caller uses its retail table, and a disc track runs
// unchanged. Otherwise the value from PARM or the default,
// with or without PARM:
//   Reverb   0..4, or 5 for off (SetReverbMode switches everything from 5 off); default 2
//   BotRow   row 0..17 of data.ArcadeDifficulty; default 0
//   Ambient  sound number for slot 0 or 1, 0 = none; default none
int NativeParm_ReverbForLevel(int levelID);
int NativeParm_BotRowForLevel(int levelID);
int NativeParm_AmbientForLevel(int levelID, int slot);

// THE SOUND OF A CONTAINER TRACK.
//
// Everything here is silent as long as no container with SNDB is loaded: the two
// ForLevel questions then answer -1, ArmForLevel switches nothing on, and
// SectorData returns NULL. A disc track therefore runs unchanged.
//
// SetHowlLayout hands over the retail layout of KART.HWL once - without
// bankOffsets no bank number translates into a sector. Called at the end
// of howl_LoadHeader, the same for every run, with or without a container.
void NativeSound_SetHowlLayout(int fileIndex, const u16 *bankOffsets, int bankCount, const u16 *songOffsets, int songCount, u16 *spuRows,
                               int spuCount);

// Which bank/sequence this level plays if it is the host slot of a
// container with sound. Otherwise -1, and the caller uses its table.
int NativeSound_BankForLevel(int levelID);
int NativeSound_SongForLevel(int levelID);

// Sector replacement on or off, depending on whether this level is the host slot.
void NativeSound_ArmForLevel(int levelID);

// The bytes that go out instead of the retail sectors, or NULL.
const u8 *NativeSound_SectorData(int fileIndex, u32 firstSector, u32 sectorCount);

#endif
