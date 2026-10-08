// ===========================================================================
// CUSTOM CHARACTERS - THE FOLDER, THE ROSTER, THE PICK AND THE SEATS.
//
// A custom character is an overlay, never a new character id: the engine
// only ever sees the templates 0..14 in data.characterIDs. The driver select
// shows the roster after the 15 retail tiles (game/230/MM_NativeCharGrid.c)
// and leaves its choice here as the pick; at every load the funnel decides
// from the pick whether seat 0 is born with the custom model.
//
// THE FOLDER. characters/ under the game folder (NATIVE_CHAR_DIR_NAME; main.c
// creates it with a README), read once at start and independent of
// --no-tracks. Every *.rldchar in it (the extension in any case, subfolders
// skipped) is collected first, then the names are sorted the way the track
// list sorts them, and only then the files are read in that order. A broken
// file is skipped with one REFUSED line and the game starts anyway. The first
// NATIVE_CHAR_ROSTER_MAX valid files get the runtime ids
// NATIVE_CHAR_RUNTIME_ID_FIRST + rank; every valid file after them is named
// in a loud NO ID line and left out. One summary line per start, also for an
// empty folder; a missing folder is one line of its own. Under
// --settings-defaults no folder is read unless --chars-dir names one.
//
// THE TEST WAYS (developer switches, main.c): --chars-dir <folder> reads that
// folder instead of characters/; --char <file> reads only this one file and
// no folder; --dev-grid-fill <n> adds placeholders; --dev-char-seats all|cycle
// puts every seat of a one-player arcade race on a file's model (measuring,
// NativeChar_ArmDevSeats).
//
// THE DRAW RESERVE (NativeChar_DrawReserve): the draw memory of a load grows
// by what its custom models may draw, the MEMPACK window by what the largest
// such load needs (NativeChar_MempackExtraNeeded) - both 0 without a file.
//
// THE ROSTER, fixed at start: the files in sorted order, then the
// placeholders of --dev-grid-fill - "PLACEHOLDER <n>" on template
// (n - 1) % 15, without a model. They fill the grid so that rows and
// scrolling can be seen, and they cannot be chosen. At most
// NATIVE_CHAR_ROSTER_MAX entries in all.
//
// THE PICK. An entry index, or -1 for retail. The driver select writes it in
// every frame it runs: -1 on a retail tile, while the grid is off and on the
// way back to the title. A main-menu choice other than ARCADE
// (game/native_menuscreen.c) and every menu load onto the title drop it too.
// The direct start (DebugMenu_JumpToLevel: --level, --autoload-track, the
// LEVEL and TRACK pages of the debug menu) sets entry 0 only for --char, so
// --level with --driver <template> --char <file> binds as it always did.
// NativeChar_ModeAllowed is the menu side of the mode rule; the funnel keeps
// its own checks of the race it loads.
//
// THE PATH OF A FILE.
//   start    NativeChar_LoadRoster, once, before CTR_Main: envelope
//            (Rld_OpenAs with s_rldCharFormat), CHRI, the CMDL size against
//            RLDCHAR_LIMIT_CMDL, CMDL with its hash, RldChar_CheckModel on the
//            bytes as stored, then ONE LOAD_RunPtrMap on the host copy. The
//            model lives in host memory for the whole run - not in the
//            MEMPACK, so the memory layout of the game stays as it is. Then
//            the roster is built.
//   stage 5  NativeChar_ArmSeats (game/LOAD/LOAD_TenStages.c): the funnel
//            decides per load whether seat 0 is bound - only when the pick is
//            a file and characterIDs[0] holds its template. Its order is fixed,
//            and the first answer that is not "yes" ends it.
//   birth    NativeChar_SeatModel (game/Vehicle/VehBirth.c): the bound model,
//            or NULL and the retail lookup by name. The class of a bound seat
//            comes from its CHRI (NativeChar_SeatEngineClass). It never speaks
//            with its template's voice (NativeChar_SeatSilent): a file with
//            usable voices (CVOI, checked at start, NativeChar_ReadVoices, and
//            read again when a seat is bound, NativeChar_HoldVoices) speaks
//            with its own clips (NativeChar_SeatSpeak, game/HOWL/
//            HOWL_Voiceline.c), a file without is silent.
//   draw     NativeChar_ModelHidesWheels (game/DrawTires.c): a file whose
//            CHRI flags set RLDCHAR_FLAG_NO_WHEELS is drawn without the kart
//            wheels and their reflection, wherever its model is drawn.
//            NativeChar_ModelFullHeight (game/RenderBucket/
//            RenderBucket_QueueExecute.c): the models of a file whose CHRI
//            flags set RLDCHAR_FLAG_FULL_HEIGHT keep bit 0 of the height.
//   mask     NativeChar_SeatMaskGood (game/Vehicle/VehPickupItem.c and the
//            HUD icon, game/UI/UI_Weapon.c): a bound seat whose CHRI flags
//            choose Aku Aku or Uka Uka wears that mask while its model and
//            beam are loaded, else the template's. A file with a usable CMSK
//            (read at start like CMDL, NativeChar_ReadMask) gives the mask
//            instance of the bound seat its own model
//            (NativeChar_SeatMaskModel); a broken CMSK costs only that.
//   grid     NativeChar_EntryPortrait (game/230/MM_NativeCharGrid.c): the
//            CICN of a file, read at start, is uploaded into its slot of the
//            portrait strip at the first draw after every entering of the
//            driver select and drawn on its tile. Only there - see THE
//            PORTRAITS.
//   stage 0  NativeChar_ClearSeats: the next load starts with empty seats.
//
// THE MODEL IS THE NATIVE ONE. CMDL is framed like a model file of the BIGFILE
// (LOAD_DramFileCallback, game/LOAD/LOAD_File.c:116-185): u32 body size G, the
// body, u32 map bytes, the map. The relocation base is the body, 4 bytes into
// the chunk, exactly as LOAD_DramFileCallback passes &fileBuf[4]; after it every
// pointer field holds an address. The struct Model is at body offset 0, where
// the loader finds the model of a single model file too: the file start plus
// LOAD_MODEL_FILE_HEADER_BYTES (load stage 6, LOAD_DramFile hands out the file
// start, game/LOAD/LOAD_File.c:231-236).
//
// RETAIL STAYS. Without a .rldchar in the folder and without --char and
// --dev-grid-fill: the roster is empty (the grid stays off), the pick stays
// -1, the funnel binds nothing, the seat model is NULL, every seat function
// returns the retail value and VehBirth takes the retail expression. The only
// trace is the summary line. The instance counter counts in every run and
// changes nothing; its exit line is there with --dev only. The log lines are
// read by a measuring tool and keep their wording.
//
// QUICK STATES hold host pointers and a model outside the MEMPACK: they are
// refused while NativeChar_Active() says a custom character is in play. The
// own mask is one more such host pointer (in the mask instance) and needs
// nothing else.
// ===========================================================================

#include <platform/native_chars.h>
#include <platform/native_audio.h>
#include <platform/native_path.h>

#include <SDL3/SDL.h>

// The envelope reader, then the character format on top of it (the same two
// files the packer compiles, tools/rldpack.c).
#include <rldtrack.inc>
#include <rldchar.inc>

#define NATIVE_CHAR_PATH_MAX 1024
#define NATIVE_CHAR_SEATS 8
#define NATIVE_CHAR_EXTENSION ".rldchar"

// A driver pack lists a few dozen models; the bound only keeps a broken list
// from walking on forever.
#define NATIVE_CHAR_DONOR_SCAN_MAX 256

// The mask the game counts animation frames with (game/Vehicle/VehFrame.c:11).
#define NATIVE_CHAR_FRAME_MASK 0x7fffu

// The mask sound is the mask model id plus this (MASK_SOUND_ID_OFFSET_FROM_MODEL,
// game/Vehicle/VehPickupItem.c): 0x53 Aku Aku, 0x54 Uka Uka.
#define NATIVE_CHAR_MASK_SOUND_OFFSET 0x1a

// --dev, main.c. Defined further down in the same build.
extern int g_cfg_dev;

global_variable char s_charFolder[NATIVE_CHAR_PATH_MAX];
global_variable char s_charFile[NATIVE_CHAR_PATH_MAX];
global_variable int s_charGiven;
global_variable int s_charArgTooLong;

// What a file's CICN gave (NativeChar_ReadIcon).
enum
{
	NATIVE_CHAR_ICON_NONE = 0,    // no CICN: the template's portrait
	NATIVE_CHAR_ICON_OWN = 1,     // iconWords hold a checked CICN
	NATIVE_CHAR_ICON_IGNORED = 2, // CICN present but unusable (iconWhy): the template's portrait
};

#define NATIVE_CHAR_ICON_WORDS (RLDCHAR_ICON_BYTES / 2u)

// What a file's CVOI gave (NativeChar_ReadVoices).
enum
{
	NATIVE_CHAR_VOICES_NONE = 0,    // no CVOI: silent
	NATIVE_CHAR_VOICES_OWN = 1,     // cvoi and voices hold a checked CVOI
	NATIVE_CHAR_VOICES_IGNORED = 2, // CVOI present but unusable (voiceWhy): silent
};

// The unit of an XA line's length (CDSYS_XAGetTrackLength, game/CDSYS.c): the
// disc sectors it spans, read at double speed (CDSYS_CD_MODE_XA_AUDIO), so 150
// a second. Voiceline_StartPlay waits length / 5 + 0x1e game frames.
#define NATIVE_CHAR_VOICE_SECTORS_PER_SECOND 150u

// What a file's CMSK gave (NativeChar_ReadMask).
enum
{
	NATIVE_CHAR_MASK_NONE = 0,    // no CMSK: the retail mask
	NATIVE_CHAR_MASK_OWN = 1,     // cmsk and mask hold a checked, relocated model
	NATIVE_CHAR_MASK_IGNORED = 2, // CMSK present but unusable (maskWhy): the retail mask
};

// One loaded file: its name on disk (the identity, owned here for the whole
// run), the CMDL chunk (relocated, never freed - instances point into it),
// what CHRI said and the portrait of CICN as 16-bit words (word 0x04 the CLUT,
// word 0x14 the texels, include/rldchar.inc).
struct NativeCharFile
{
	char *file;
	char *path;                 // where it was read, for the CVOI of a bound seat (NativeChar_HoldVoices)
	u8 *cmdl;
	struct Model *model;
	struct RldCharInfo info;
	u8 cmdlHash[6];
	u64 fileBytes;
	u32 drawBytes;              // what one draw of the model costs at most (RldChar_CheckModel, model-draw)
	u32 triangles;              // of the model (RldChar_CheckModel), for the cost of a ghost draw
	int icon;
	const char *iconWhy;
	u16 iconWords[NATIVE_CHAR_ICON_WORDS];
	u8 *cmsk;                   // the CMSK chunk, relocated, never freed while in the roster; NULL without own mask
	struct Model *mask;         // the own mask model inside cmsk, NULL = the retail mask
	u32 maskTriangles;
	u32 maskDrawBytes;          // what one draw of the own mask costs at most (RldChar_CheckMask, model-draw)
	int maskState;              // NATIVE_CHAR_MASK_*
	char maskWhy[192];          // NATIVE_CHAR_MASK_IGNORED: the rule and its detail
	u8 *cvoi;                   // the CVOI chunk, held only while a seat is bound to the file; else NULL
	struct RldCharVoices voices; // what RldChar_CheckVoices read (at start, and again from cvoi)
	int voiceState;             // NATIVE_CHAR_VOICES_*
	const char *voiceWhy;       // NATIVE_CHAR_VOICES_IGNORED: the reader's or the check's fixed text
};

// The files of the roster, in sorted order: entry e < s_charRosterFiles is
// s_charFiles[e], with runtime id NATIVE_CHAR_RUNTIME_ID_FIRST + e.
global_variable struct NativeCharFile s_charFiles[NATIVE_CHAR_ROSTER_MAX];

// The overlay per seat. Only seat 0 is armed for the player; the developer
// switch --dev-char-seats arms every seat (devSeat). entry is the roster
// entry it was armed with, motorId the character id the engine runs the seat
// on - the template. maskNoted and maskMissingNoted keep the mask lines of
// NativeChar_NoteMask and NativeChar_SeatMaskGood to one per load; ownMask is
// the file's own mask model (NULL = retail), ownMaskNoted its line;
// portraitNoted the line of NativeChar_SeatPortrait; mapColor the four corner
// colors of the minimap marker when the file chooses one (hasMapColor).
global_variable struct
{
	struct Model *model;
	int entry;
	int motorId;
	int maskNoted;
	int maskMissingNoted;
	struct Model *ownMask;
	int ownMaskNoted;
	int portraitNoted;
	int hasMapColor;
	u32 mapColor[4];
	int devSeat;                // bound by --dev-char-seats: the class stays the template's
} s_seat[NATIVE_CHAR_SEATS];

// --dev-grid-fill, as main.c passed it on (0 = none).
global_variable int s_charGridFill;

// --dev-char-seats, as main.c passed it on, and the race loads it has bound
// (the file of the next one with NATIVE_CHAR_DEV_SEATS_CYCLE). While such a
// load is open, the instances it drops are counted on their own and named
// when the next load arms its seats (or at exit).
global_variable int s_charDevSeats;
global_variable int s_charDevLoads;
global_variable int s_charDevLoadOpen;
global_variable int s_charDevLoadLevel;
global_variable s64 s_charDevDroppedTotal;
global_variable s64 s_charDevDroppedSeat0;
global_variable s64 s_charDevDroppedBound;

// The roster, built once by NativeChar_LoadRoster: s_charRosterFiles entries
// are files, the rest up to s_charRosterCount are placeholders. Placeholder n
// (1-based) is entry s_charRosterFiles + n - 1.
global_variable int s_charRosterCount;
global_variable int s_charRosterFiles;

// The entries whose CHRI flags set RLDCHAR_FLAG_FULL_HEIGHT. 0 - every run
// without such a file - lets NativeChar_ModelFullHeight answer at once.
global_variable int s_charFullHeightFiles;

// Capitals, a space and digits: inside the CHRI name rule (RldChar_NameCheck),
// so whatever draws the name of a file draws these too.
global_variable char s_charPlaceholderName[NATIVE_CHAR_ROSTER_MAX][RLDCHAR_NAME_FIELD + 1];

// The pick of the driver select: an entry of the roster, or -1 (retail).
global_variable int s_charPick = -1;

global_variable s64 s_droppedTotal;
global_variable s64 s_droppedSeat0;

internal int NativeChar_IsAbsolute(const char *path)
{
	return (path[0] == '/') || (path[0] == '\\') || ((path[0] != '\0') && (path[1] == ':'));
}

internal void NativeChar_Remember(char *dst, const char *value)
{
	const char *text = (value != NULL) ? value : "";

	// A cut path could name another file or folder: the cut copy is only for
	// the log, NativeChar_BuildPath and NativeChar_ScanFolder refuse it.
	if (strlen(text) >= NATIVE_CHAR_PATH_MAX)
	{
		s_charArgTooLong = 1;
	}

	snprintf(dst, NATIVE_CHAR_PATH_MAX, "%s", text);
}

void NativeChar_SetFolder(const char *folder)
{
	NativeChar_Remember(s_charFolder, folder);
}

void NativeChar_SetFile(const char *file)
{
	s_charGiven = 1;
	NativeChar_Remember(s_charFile, file);
}

void NativeChar_SetDevSeats(int mode)
{
	s_charDevSeats = ((mode == NATIVE_CHAR_DEV_SEATS_ALL) || (mode == NATIVE_CHAR_DEV_SEATS_CYCLE)) ? mode : NATIVE_CHAR_DEV_SEATS_OFF;
}

void NativeChar_SetGridFill(int count)
{
	// main.c refuses a count outside 1..NATIVE_CHAR_ROSTER_MAX; the bounds here
	// only keep the roster from reading past its names.
	s_charGridFill = (count < 0) ? 0 : ((count > NATIVE_CHAR_ROSTER_MAX) ? NATIVE_CHAR_ROSTER_MAX : count);
}

int NativeChar_ArgsUsable(void)
{
	if (!s_charGiven || (s_charFolder[0] != '\0') || NativeChar_IsAbsolute(s_charFile))
	{
		return 1;
	}

	fflush(stdout);
	fprintf(stderr, "switch --char %s names no folder - give --chars-dir <folder> or an absolute path\n", s_charFile);
	fflush(stderr);
	return 0;
}

// The folder: characters/ under the game folder without --chars-dir; an
// absolute --chars-dir as it stands, a relative one under the game folder - as
// NativeTrack_Scan resolves --tracks-dir (platform/native_assets.c).
internal int NativeChar_FolderPath(char *dst, size_t dstSize)
{
	if (s_charArgTooLong)
	{
		return 0;
	}

	if (s_charFolder[0] == '\0')
	{
		return NativePath_Join(dst, dstSize, NativeStr8_FromCString(NativeAssets_GetBaseDir()), NATIVE_STR8_LIT(NATIVE_CHAR_DIR_NAME));
	}

	if (NativeChar_IsAbsolute(s_charFolder))
	{
		const int written = snprintf(dst, dstSize, "%s", s_charFolder);

		return (written >= 0) && ((size_t)written < dstSize);
	}

	return NativePath_Join(dst, dstSize, NativeStr8_FromCString(NativeAssets_GetBaseDir()), NativeStr8_FromCString(s_charFolder));
}

// The --char file: an absolute --char as it stands, otherwise inside
// --chars-dir (NativeChar_FolderPath; main.c has refused a relative --char
// without --chars-dir).
internal int NativeChar_BuildPath(char *dst, size_t dstSize)
{
	char folder[NATIVE_CHAR_PATH_MAX];

	if (s_charArgTooLong)
	{
		return 0;
	}

	if (NativeChar_IsAbsolute(s_charFile))
	{
		const int written = snprintf(dst, dstSize, "%s", s_charFile);

		return (written >= 0) && ((size_t)written < dstSize);
	}

	if ((s_charFolder[0] == '\0') || !NativeChar_FolderPath(folder, sizeof(folder)))
	{
		return 0;
	}

	return NativePath_Join(dst, dstSize, NativeStr8_FromCString(folder), NativeStr8_FromCString(s_charFile));
}

// One line, the wording a measuring tool reads: REFUSED <file>: <WORD> (<rule>) <detail>.
internal void NativeChar_Refuse(const char *file, const char *word, const char *rule, const char *format, ...)
{
	char detail[256];
	va_list args;

	va_start(args, format);
	vsnprintf(detail, sizeof(detail), format, args);
	va_end(args);
	detail[sizeof(detail) - 1] = '\0';

	Platform_Log("[CTR Char] REFUSED %s: %s (%s) %s\n", file, word, rule, detail);
}

// --dev-char-seats: the instances the load just ended dropped, one line per
// bound race load - also with none, the line is the measurement.
internal void NativeChar_FlushDevLoad(void)
{
	if (!s_charDevLoadOpen)
	{
		return;
	}

	s_charDevLoadOpen = 0;
	Platform_Log("[CTR Char] dev seats: load %d on level %d ended, instances dropped %lld total, %lld seat 0, %lld on bound seats\n", s_charDevLoads,
	             s_charDevLoadLevel, (long long)s_charDevDroppedTotal, (long long)s_charDevDroppedSeat0, (long long)s_charDevDroppedBound);
}

internal void NativeChar_ReportAtExit(void)
{
	NativeChar_FlushDevLoad();
	Platform_Log("[CTR Char] at exit: instances dropped %lld total, %lld seat 0\n", (long long)s_droppedTotal, (long long)s_droppedSeat0);
}

// The portrait (CICN, optional): checked with RldChar_CheckIcon and kept as
// words. Whatever is wrong with it - unreadable, hash, CICN-1..3 - costs only
// the portrait, never the file (docs/CONTAINER_FORMAT.md, CICN). No line here:
// NativeChar_Admit names the outcome once the entry is known.
internal void NativeChar_ReadIcon(struct RldReader *reader, struct NativeCharFile *out)
{
	const char *why = NULL;
	size_t size = 0;
	int index = -1;
	u8 *bytes;
	u32 w;

	out->icon = NATIVE_CHAR_ICON_NONE;
	out->iconWhy = NULL;

	if ((Rld_FindEntry(reader, "CICN", &index) == NULL) || (index < 0))
	{
		return;
	}

	bytes = Rld_ReadChunk(reader, index, &size, &why);
	if (bytes == NULL)
	{
		out->icon = NATIVE_CHAR_ICON_IGNORED;
		out->iconWhy = (why != NULL) ? why : "CICN cannot be read";
		return;
	}

	why = RldChar_CheckIcon(bytes, size);
	if (why != NULL)
	{
		free(bytes);
		out->icon = NATIVE_CHAR_ICON_IGNORED;
		out->iconWhy = why;
		return;
	}

	// Little endian on disk, the VRAM words as they are uploaded.
	for (w = 0; w < NATIVE_CHAR_ICON_WORDS; w++)
	{
		out->iconWords[w] = (u16)Rld_ReadLE16(&bytes[2u * w]);
	}

	free(bytes);
	out->icon = NATIVE_CHAR_ICON_OWN;
}

// CMSK as stored against RldChar_CheckMask: 1 with out->maskTriangles set,
// else 0 with out->maskState IGNORED and the rule in out->maskWhy. Pure (the
// self-test runs it); the bytes stay the caller's.
internal int NativeChar_CheckMaskBytes(struct NativeCharFile *out, const u8 *bytes, size_t size)
{
	struct RldCharFinding finding;
	struct RldCharModelFacts facts;
	enum RldCharVerdict verdict;

	verdict = RldChar_CheckMask(bytes, size, &finding, NULL, NULL, &facts);
	if (verdict != RLDCHAR_VERDICT_OK)
	{
		out->maskState = NATIVE_CHAR_MASK_IGNORED;
		snprintf(out->maskWhy, sizeof(out->maskWhy), "%s (%s) %s", RldChar_VerdictWord(verdict), (finding.rule != NULL) ? finding.rule : "model",
		         finding.detail);
		return 0;
	}

	out->maskTriangles = facts.triangles;
	out->maskDrawBytes = facts.drawBytes;
	return 1;
}

// The own mask (CMSK, optional): RldChar_CheckMask on the bytes as stored, then
// ONE LOAD_RunPtrMap on the host copy, the way CMDL goes. Whatever is wrong
// with it - unreadable, hash, CMSK-1..3, a model rule, the pointer map - costs
// only the own mask, never the file. No line here: NativeChar_Admit names the
// outcome once the entry is known. The reader is still open; the model check
// runs here, before CMDL's (both are sequential, the check is not reentrant).
internal void NativeChar_ReadMask(struct RldReader *reader, struct NativeCharFile *out)
{
	const char *why = NULL;
	size_t size = 0;
	int index = -1;
	u8 *bytes;

	out->cmsk = NULL;
	out->mask = NULL;
	out->maskTriangles = 0;
	out->maskDrawBytes = 0;
	out->maskState = NATIVE_CHAR_MASK_NONE;
	out->maskWhy[0] = '\0';

	if ((Rld_FindEntry(reader, "CMSK", &index) == NULL) || (index < 0))
	{
		return;
	}

	out->maskState = NATIVE_CHAR_MASK_IGNORED;

	bytes = Rld_ReadChunk(reader, index, &size, &why);
	if (bytes == NULL)
	{
		snprintf(out->maskWhy, sizeof(out->maskWhy), "DAMAGED (chunk) %s", (why != NULL) ? why : "CMSK cannot be read");
		return;
	}

	if (!NativeChar_CheckMaskBytes(out, bytes, size))
	{
		free(bytes);
		return;
	}

	// The model frame starts behind version and flags; its body 4 bytes later,
	// as LOAD_DramFileCallback passes it. CMSK-1 and model-bounds have held all
	// of it to the chunk and to 4-byte alignment.
	{
		u8 *frame = &bytes[RLDCHAR_MASK_HEAD_BYTES];
		const u32 bodyBytes = Rld_ReadLE32(&frame[0]);
		const u32 mapBytes = Rld_ReadLE32(&frame[4u + bodyBytes]);
		char *body = (char *)&frame[4];
		int *map = (int *)&frame[8u + bodyBytes];

		if (LOAD_RunPtrMap(body, (int)bodyBytes, map, (int)(mapBytes / 4u)) == 0)
		{
			free(bytes);
			snprintf(out->maskWhy, sizeof(out->maskWhy), "%s", "PTRMAP (ptrmap) LOAD_RunPtrMap refused the pointer map - nothing patched");
			return;
		}

		out->cmsk = bytes;
		out->mask = (struct Model *)body;
		out->maskState = NATIVE_CHAR_MASK_OWN;
	}
}

// A file that is let go after its CMSK was read.
internal void NativeChar_DropMask(struct NativeCharFile *f)
{
	free(f->cmsk);
	f->cmsk = NULL;
	f->mask = NULL;
}

// The voices (CVOI, optional): checked with RldChar_CheckVoices; the bytes are
// let go again - only a bound seat holds them (NativeChar_HoldVoices), so a
// roster of files with voices costs no memory for them. Whatever is wrong with
// it - unreadable, hash, CVOI-1..4 - costs only the voices, never the file
// (docs/CONTAINER_FORMAT.md, CVOI). No line here: NativeChar_Admit names the
// outcome once the entry is known.
internal void NativeChar_ReadVoices(struct RldReader *reader, struct NativeCharFile *out)
{
	const char *why = NULL;
	size_t size = 0;
	int index = -1;
	u8 *bytes;

	out->cvoi = NULL;
	memset(&out->voices, 0, sizeof(out->voices));
	out->voiceState = NATIVE_CHAR_VOICES_NONE;
	out->voiceWhy = NULL;

	if ((Rld_FindEntry(reader, "CVOI", &index) == NULL) || (index < 0))
	{
		return;
	}

	bytes = Rld_ReadChunk(reader, index, &size, &why);
	if (bytes == NULL)
	{
		out->voiceState = NATIVE_CHAR_VOICES_IGNORED;
		out->voiceWhy = (why != NULL) ? why : "CVOI cannot be read";
		return;
	}

	why = RldChar_CheckVoices(bytes, (u64)size, &out->voices);
	free(bytes);
	if (why != NULL)
	{
		out->voiceState = NATIVE_CHAR_VOICES_IGNORED;
		out->voiceWhy = why;
		return;
	}

	out->voiceState = NATIVE_CHAR_VOICES_OWN;
}

// One file: read, checked, relocated into *out. 0 after a REFUSED line (named
// by file), with nothing left allocated; 1 with out->cmdl and out->model set.
// out->file is the caller's.
internal int NativeChar_ReadFile(const char *path, const char *file, struct NativeCharFile *out)
{
	struct RldReader reader;
	struct RldCharInfo info;
	struct RldCharFinding finding;
	const char *why;
	const u8 *modelEntry;
	u8 *infoBytes;
	u8 *cmdl;
	size_t infoSize = 0;
	size_t cmdlSize = 0;
	int infoIndex = -1;
	int modelIndex = -1;
	u64 cmdlBytes;
	enum RldCharVerdict verdict;
	struct RldCharModelFacts facts;

	// The detail of every refusal is a fixed text: the reader's, the parser's,
	// the check's or this file's own.
	why = Rld_OpenAs(&reader, path, &s_rldCharFormat);
	if (why != NULL)
	{
		const char *word = (reader.refusal == RLD_REFUSAL_NEWER) ? "NEEDS NEWER" : ((reader.refusal == RLD_REFUSAL_OLDER) ? "OLD FORMAT" : "DAMAGED");

		NativeChar_Refuse(file, word, "envelope", "%s", why);
		return 0;
	}

	// Rld_OpenAs has proved both present (the required chunks of the format).
	// CICN is read after CMDL (NativeChar_ReadIcon), CPRM is not read.
	Rld_FindEntry(&reader, "CHRI", &infoIndex);
	modelEntry = Rld_FindEntry(&reader, "CMDL", &modelIndex);
	if ((infoIndex < 0) || (modelEntry == NULL))
	{
		Rld_Close(&reader);
		NativeChar_Refuse(file, "DAMAGED", "envelope", "%s", "CHRI or CMDL is missing - required chunk");
		return 0;
	}

	// The size of the model (P19), from the directory entry and BEFORE the
	// malloc: the file comes from a stranger, and its number must not decide
	// how much memory is taken. Rld_OpenAs and Rld_ReadChunk hold the same
	// limit; this line names it in the words of the model check.
	cmdlBytes = Rld_ReadLE64(&modelEntry[0x18]);
	if (cmdlBytes > RLDCHAR_LIMIT_CMDL)
	{
		Rld_Close(&reader);
		NativeChar_Refuse(file, "DAMAGED", "model-size", "CMDL is %llu bytes, at most %u", (unsigned long long)cmdlBytes, (unsigned)RLDCHAR_LIMIT_CMDL);
		return 0;
	}

	// The CMDL hash as Rld_ReadChunk proves it below; the directory stays in the
	// reader after the file is closed.
	memcpy(out->cmdlHash, &modelEntry[RLD_DIR_HASH_OFFSET], sizeof(out->cmdlHash));
	out->fileBytes = reader.fileSize;

	infoBytes = Rld_ReadChunk(&reader, infoIndex, &infoSize, &why);
	if (infoBytes == NULL)
	{
		Rld_Close(&reader);
		NativeChar_Refuse(file, "DAMAGED", "chunk", "%s", why);
		return 0;
	}

	cmdl = Rld_ReadChunk(&reader, modelIndex, &cmdlSize, &why);
	if (cmdl != NULL)
	{
		NativeChar_ReadIcon(&reader, out);
		NativeChar_ReadMask(&reader, out);
		NativeChar_ReadVoices(&reader, out);
	}

	Rld_Close(&reader);
	if (cmdl == NULL)
	{
		free(infoBytes);
		NativeChar_Refuse(file, "DAMAGED", "chunk", "%s", why);
		return 0;
	}

	why = RldChar_ParseInfo(&info, infoBytes, infoSize);
	free(infoBytes);
	if (why != NULL)
	{
		// The text starts with its rule ("CHRI-2: template is outside 0..14").
		const char *colon = strstr(why, ": ");
		char rule[16];

		snprintf(rule, sizeof(rule), "%.*s", (colon != NULL) ? (int)(colon - why) : 0, why);
		free(cmdl);
		NativeChar_DropMask(out);
		NativeChar_Refuse(file, "DAMAGED", (rule[0] != '\0') ? rule : "CHRI", "%s", (colon != NULL) ? (colon + 2) : why);
		return 0;
	}

	// BEFORE the relocation: the pointer fields still hold body offsets, which
	// is what the check reads. The game stops at the first finding. Only an OK
	// reaches the relocation - with --ptr-map-unchecked LOAD_RunPtrMap checks
	// nothing itself, so this is the one check that always runs.
	verdict = RldChar_CheckModel(cmdl, cmdlSize, &finding, NULL, NULL, &facts);
	if (verdict != RLDCHAR_VERDICT_OK)
	{
		free(cmdl);
		NativeChar_DropMask(out);
		NativeChar_Refuse(file, RldChar_VerdictWord(verdict), (finding.rule != NULL) ? finding.rule : "model", "%s", finding.detail);
		return 0;
	}

	// ONCE, on the host copy, through the loader's own relocation: origin is the
	// body, 4 bytes into the chunk; the map follows the body and its u32 length
	// (model-bounds has held all of it to the chunk and to 4-byte alignment).
	{
		const u32 bodyBytes = Rld_ReadLE32(&cmdl[0]);
		const u32 mapBytes = Rld_ReadLE32(&cmdl[4u + bodyBytes]);
		char *body = (char *)&cmdl[4];
		int *map = (int *)&cmdl[8u + bodyBytes];

		if (LOAD_RunPtrMap(body, (int)bodyBytes, map, (int)(mapBytes / 4u)) == 0)
		{
			free(cmdl);
			NativeChar_DropMask(out);
			NativeChar_Refuse(file, "PTRMAP", "ptrmap", "%s", "LOAD_RunPtrMap refused the pointer map - nothing patched");
			return 0;
		}

		out->cmdl = cmdl;
		out->model = (struct Model *)body;
		out->info = info;
		out->drawBytes = facts.drawBytes;
		out->triangles = facts.triangles;
	}

	return 1;
}

internal void NativeChar_LogLoaded(const struct NativeCharFile *entry)
{
	// The CHRI flags only add to the line when a known bit is set; a file
	// without flags keeps the line as it always was. The mask only when the
	// file chooses one (not for "like the template" or the reserved value).
	const char *wheels = ((entry->info.flags & RLDCHAR_FLAG_NO_WHEELS) != 0) ? ", wheels hidden" : "";
	const u32 mask = RldChar_Mask(entry->info.flags);
	const char *maskText = (mask == RLDCHAR_MASK_AKU) ? ", mask aku" : ((mask == RLDCHAR_MASK_UKA) ? ", mask uka" : "");
	const char *height = ((entry->info.flags & RLDCHAR_FLAG_FULL_HEIGHT) != 0) ? ", full height" : "";

	Platform_Log("[CTR Char] loaded %s: template %u, class %u, CMDL %02x%02x%02x%02x%02x%02x, %llu bytes%s%s%s\n", entry->file,
	             (unsigned)entry->info.templateId, (unsigned)entry->info.classId, (unsigned)entry->cmdlHash[0], (unsigned)entry->cmdlHash[1],
	             (unsigned)entry->cmdlHash[2], (unsigned)entry->cmdlHash[3], (unsigned)entry->cmdlHash[4], (unsigned)entry->cmdlHash[5],
	             (unsigned long long)entry->fileBytes, wheels, maskText, height);
}

// The portrait of an admitted entry, one line of its own after "loaded" (that
// line keeps its wording, measuring tools read it).
internal void NativeChar_LogPortrait(int entry)
{
	const struct NativeCharFile *f = &s_charFiles[entry];

	if (f->icon == NATIVE_CHAR_ICON_NONE)
	{
		Platform_Log("[CTR Char] portrait %s: the template's (no CICN)\n", f->file);
	}
	else if (f->icon == NATIVE_CHAR_ICON_IGNORED)
	{
		Platform_Log("[CTR Char] portrait %s: CICN ignored - %s - the template's\n", f->file, f->iconWhy);
	}
	else if (entry >= NATIVE_CHAR_PORTRAIT_SLOTS)
	{
		Platform_Log("[CTR Char] portrait %s: no slot (entry %d, %d slots) - the template's\n", f->file, entry, NATIVE_CHAR_PORTRAIT_SLOTS);
	}
	else
	{
		Platform_Log("[CTR Char] portrait %s: own (slot %d)\n", f->file, entry);
	}
}

// The own mask of an admitted entry, one line of its own after "portrait",
// only for a file with CMSK (a file without keeps its lines as they were).
internal void NativeChar_LogMask(int entry)
{
	const struct NativeCharFile *f = &s_charFiles[entry];

	if (f->maskState == NATIVE_CHAR_MASK_OWN)
	{
		Platform_Log("[CTR Char] mask %s: own (%u triangles)\n", f->file, (unsigned)f->maskTriangles);
	}
	else if (f->maskState == NATIVE_CHAR_MASK_IGNORED)
	{
		Platform_Log("[CTR Char] mask %s: CMSK ignored - %s - retail\n", f->file, f->maskWhy);
	}
}

// The voices of an admitted entry, one line for every file: the clips of each
// event in event order, or why it is silent.
internal void NativeChar_LogVoices(int entry)
{
	const struct NativeCharFile *f = &s_charFiles[entry];
	char line[256];
	size_t used = 0;
	int e;

	if (f->voiceState == NATIVE_CHAR_VOICES_NONE)
	{
		Platform_Log("[CTR Char] voices %s: none - silent\n", f->file);
		return;
	}

	if (f->voiceState == NATIVE_CHAR_VOICES_IGNORED)
	{
		Platform_Log("[CTR Char] voices %s: CVOI ignored - %s - silent\n", f->file, f->voiceWhy);
		return;
	}

	// Ten keys of at most 9 characters and counts of one digit: always fits.
	line[0] = '\0';
	for (e = 0; e < RLDCHAR_VOICE_EVENTS; e++)
	{
		const int written = snprintf(&line[used], sizeof(line) - used, "%s%s %u", (e == 0) ? "" : ", ", s_rldCharVoiceEvents[e], (unsigned)f->voices.count[e]);

		if ((written < 0) || ((size_t)written >= (sizeof(line) - used)))
		{
			break;
		}
		used += (size_t)written;
	}

	Platform_Log("[CTR Char] voices %s: %u clips - %s\n", f->file, (unsigned)f->voices.clipCount, line);
}

// A valid file becomes the next entry while there is an id for it; after
// that it is named loudly and let go. 1 = it got an entry (and keeps file and
// a copy of path).
internal int NativeChar_Admit(struct NativeCharFile *loaded, char *file, const char *path)
{
	if (s_charRosterFiles >= NATIVE_CHAR_ROSTER_MAX)
	{
		Platform_LogWarn("[CTR Char] NO ID %s: all %d custom character ids are taken - the file is valid but not in the driver select\n", file,
		                 NATIVE_CHAR_ROSTER_MAX);
		free(loaded->cmdl);
		NativeChar_DropMask(loaded);
		return 0;
	}

	loaded->file = file;
	loaded->path = SDL_strdup(path);
	s_charFiles[s_charRosterFiles] = *loaded;
	s_charRosterFiles++;
	if ((loaded->info.flags & RLDCHAR_FLAG_FULL_HEIGHT) != 0)
	{
		s_charFullHeightFiles++;
	}
	NativeChar_LogLoaded(loaded);
	NativeChar_LogPortrait(s_charRosterFiles - 1);
	NativeChar_LogMask(s_charRosterFiles - 1);

	// What the draw reserve counts for it (NativeChar_DrawReserve), one draw.
	Platform_Log("[CTR Char] draw bytes %s: model %u, own mask %u\n", loaded->file, (unsigned)loaded->drawBytes,
	             (unsigned)((loaded->maskState == NATIVE_CHAR_MASK_OWN) ? loaded->maskDrawBytes : 0u));
	NativeChar_LogVoices(s_charRosterFiles - 1);
	return 1;
}

//----------------------------------------------------------------------------------------
// THE FOLDER
//----------------------------------------------------------------------------------------

internal int NativeChar_HasExtension(const char *fileName)
{
	const size_t nameLength = strlen(fileName);
	const size_t extLength = sizeof(NATIVE_CHAR_EXTENSION) - 1u;
	size_t i;

	if (nameLength <= extLength)
	{
		return 0;
	}

	// The extension may be upper or lower case, as with .rldtrack
	// (NativeTrack_HasExtension, platform/native_assets.c).
	for (i = 0; i < extLength; i++)
	{
		char a = fileName[nameLength - extLength + i];

		if ((a >= 'A') && (a <= 'Z'))
		{
			a = (char)(a - 'A' + 'a');
		}

		if (a != NATIVE_CHAR_EXTENSION[i])
		{
			return 0;
		}
	}

	return 1;
}

// THE ORDER: the one of the track list - NTFS-like, character by character,
// upper cased. The same comparison as NativeTrack_UpperChar and
// NativeTrack_CompareNames (platform/native_assets.c), which are internal to
// that file; keep the two in step. Same folder contents, same ids, on every
// disk and file system.
internal int NativeChar_UpperChar(char c)
{
	if ((c >= 'a') && (c <= 'z'))
	{
		return c - ('a' - 'A');
	}

	return (int)(unsigned char)c;
}

internal int NativeChar_CompareNames(const char *a, const char *b)
{
	while ((*a != '\0') && (NativeChar_UpperChar(*a) == NativeChar_UpperChar(*b)))
	{
		a++;
		b++;
	}

	return NativeChar_UpperChar(*a) - NativeChar_UpperChar(*b);
}

internal int NativeChar_CompareNameEntries(const void *a, const void *b)
{
	const char *nameA = *(const char *const *)a;
	const char *nameB = *(const char *const *)b;
	const int order = NativeChar_CompareNames(nameA, nameB);

	// Two names that differ only in case (a case-sensitive file system) still
	// get a fixed order.
	return (order != 0) ? order : strcmp(nameA, nameB);
}

struct NativeCharNameList
{
	const char *folder;
	char **names;
	int count;
	int capacity;
	int outOfMemory;
};

// Every name is collected before anything is read: no cap and no order from
// the file system decide which file comes first.
internal SDL_EnumerationResult SDLCALL NativeChar_CollectName(void *userdata, const char *dirname, const char *fname)
{
	struct NativeCharNameList *list = (struct NativeCharNameList *)userdata;
	char path[NATIVE_CHAR_PATH_MAX];
	SDL_PathInfo info;
	size_t length;
	char *copy;

	(void)dirname;

	if (!NativeChar_HasExtension(fname))
	{
		return SDL_ENUM_CONTINUE;
	}

	// A subfolder is skipped, even one named like a file. A name whose path is
	// too long is kept: reading it refuses it with a line.
	if (NativePath_Join(path, sizeof(path), NativeStr8_FromCString(list->folder), NativeStr8_FromCString(fname)) && SDL_GetPathInfo(path, &info) &&
	    (info.type != SDL_PATHTYPE_FILE))
	{
		return SDL_ENUM_CONTINUE;
	}

	if (list->count >= list->capacity)
	{
		const int capacity = (list->capacity > 0) ? (list->capacity * 2) : 64;
		char **grown = (char **)realloc(list->names, (size_t)capacity * sizeof(char *));

		if (grown == NULL)
		{
			list->outOfMemory = 1;
			return SDL_ENUM_FAILURE;
		}

		list->names = grown;
		list->capacity = capacity;
	}

	length = strlen(fname);
	copy = (char *)malloc(length + 1u);
	if (copy == NULL)
	{
		list->outOfMemory = 1;
		return SDL_ENUM_FAILURE;
	}

	memcpy(copy, fname, length + 1u);
	list->names[list->count] = copy;
	list->count++;
	return SDL_ENUM_CONTINUE;
}

// The folder (characters/ or --chars-dir): every name, sorted, then read in
// that order. Ends with exactly one line about the folder.
internal void NativeChar_ScanFolder(void)
{
	char folder[NATIVE_CHAR_PATH_MAX];
	struct NativeCharNameList list;
	SDL_PathInfo folderInfo;
	int refused = 0;
	int noId = 0;
	int i;

	if (!NativeChar_FolderPath(folder, sizeof(folder)))
	{
		Platform_Log("[CTR Char] no characters folder - the path of the folder is too long, nothing loaded\n");
		return;
	}

	if (!SDL_GetPathInfo(folder, &folderInfo) || (folderInfo.type != SDL_PATHTYPE_DIRECTORY))
	{
		Platform_Log("[CTR Char] no characters folder - nothing loaded (%s)\n", folder);
		return;
	}

	memset(&list, 0, sizeof(list));
	list.folder = folder;

	// A list that did not run to its end reads nothing: a part of the folder
	// would give other ids than the whole of it.
	if (!SDL_EnumerateDirectory(folder, NativeChar_CollectName, &list))
	{
		if (list.outOfMemory)
		{
			Platform_LogWarn("[CTR Char] out of memory while listing the characters folder - nothing loaded (%s)\n", folder);
		}
		else
		{
			Platform_LogWarn("[CTR Char] the characters folder cannot be listed - nothing loaded (%s)\n", folder);
		}

		for (i = 0; i < list.count; i++)
		{
			free(list.names[i]);
		}

		free(list.names);
		return;
	}

	if (list.count > 1)
	{
		qsort(list.names, (size_t)list.count, sizeof(list.names[0]), NativeChar_CompareNameEntries);
	}

	for (i = 0; i < list.count; i++)
	{
		struct NativeCharFile loaded;
		char path[NATIVE_CHAR_PATH_MAX];
		char *name = list.names[i];

		memset(&loaded, 0, sizeof(loaded));

		if (!NativePath_Join(path, sizeof(path), NativeStr8_FromCString(folder), NativeStr8_FromCString(name)))
		{
			NativeChar_Refuse(name, "DAMAGED", "path", "%s", "the path of the file is too long");
			refused++;
			free(name);
			continue;
		}

		if (!NativeChar_ReadFile(path, name, &loaded))
		{
			refused++;
			free(name);
			continue;
		}

		// An entry keeps its name for the whole run.
		if (!NativeChar_Admit(&loaded, name, path))
		{
			noId++;
			free(name);
		}
	}

	free(list.names);

	if (noId > 0)
	{
		Platform_Log("[CTR Char] characters: %d loaded, %d refused, %d without an id (%s)\n", s_charRosterFiles, refused, noId, folder);
	}
	else
	{
		Platform_Log("[CTR Char] characters: %d loaded, %d refused (%s)\n", s_charRosterFiles, refused, folder);
	}
}

// --char: this one file and no folder.
internal void NativeChar_LoadGivenFile(void)
{
	struct NativeCharFile loaded;
	char path[NATIVE_CHAR_PATH_MAX];

	memset(&loaded, 0, sizeof(loaded));

	if (!NativeChar_BuildPath(path, sizeof(path)))
	{
		NativeChar_Refuse(s_charFile, "DAMAGED", "path", "%s", "the path of the file is too long");
		Platform_Log("[CTR Char] characters: 0 loaded, 1 refused (--char)\n");
		return;
	}

	if (NativeChar_ReadFile(path, s_charFile, &loaded))
	{
		NativeChar_Admit(&loaded, s_charFile, path);
	}

	Platform_Log("[CTR Char] characters: %d loaded, %d refused (--char %s)\n", s_charRosterFiles, 1 - s_charRosterFiles, path);
}

// The files first - a refused one has no tile - then the placeholders. Built
// once: the grid, the pick and the funnel all count on the same entries for
// the whole run. Silent while it is empty.
internal void NativeChar_BuildRoster(void)
{
	const int files = s_charRosterFiles;
	int count = files + s_charGridFill;
	int n;

	if (count > NATIVE_CHAR_ROSTER_MAX)
	{
		Platform_Log("[CTR Char] roster: %d entries asked for, the first %d kept - %d placeholder(s) cut\n", count, NATIVE_CHAR_ROSTER_MAX,
		             count - NATIVE_CHAR_ROSTER_MAX);
		count = NATIVE_CHAR_ROSTER_MAX;
	}

	for (n = 1; n <= (count - files); n++)
	{
		snprintf(s_charPlaceholderName[n - 1], sizeof(s_charPlaceholderName[n - 1]), "PLACEHOLDER %d", n);
	}

	s_charRosterCount = count;

	if (count > 0)
	{
		Platform_Log("[CTR Char] roster: %d entr%s, %d file, %d placeholder(s)\n", count, (count == 1) ? "y" : "ies", files, count - files);
	}
}

void NativeChar_LoadRoster(void)
{
	// Registered here, at start, so the line has its place in the table of exit
	// reports before the ones a race registers on its way.
	if (g_cfg_dev)
	{
		Platform_AtExitReport(NativeChar_ReportAtExit);
	}

	if (s_charGiven)
	{
		NativeChar_LoadGivenFile();
	}
	else if (Platform_SettingsLocked() && (s_charFolder[0] == '\0'))
	{
		// --settings-defaults: a run that reads nothing of the player's
		// own, unless --chars-dir names a folder for it.
		Platform_Log("[CTR Char] characters: not read under --settings-defaults (--chars-dir <folder> reads one)\n");
	}
	else
	{
		NativeChar_ScanFolder();
	}

	NativeChar_BuildRoster();

	// A measuring switch that binds nothing would measure retail: loud.
	if ((s_charDevSeats != NATIVE_CHAR_DEV_SEATS_OFF) && (s_charRosterFiles == 0))
	{
		Platform_LogWarn("[CTR Char] dev seats: no file in the roster - every seat stays retail\n");
	}
}

// The CVOI of a file a seat is bound to, read again from its path at the
// binding (load stage 5): the file is checked again (hash, CVOI-1..4) and the
// clip table taken from these bytes. Whatever fails now costs only the voices
// of this run, with one line. Nothing for a file without voices.
internal void NativeChar_HoldVoices(int entry)
{
	struct NativeCharFile *f = &s_charFiles[entry];
	struct RldReader reader;
	const char *why;
	size_t size = 0;
	int index = -1;
	u8 *bytes = NULL;

	if ((f->voiceState != NATIVE_CHAR_VOICES_OWN) || (f->cvoi != NULL))
	{
		return;
	}

	why = (f->path != NULL) ? Rld_OpenAs(&reader, f->path, &s_rldCharFormat) : "the path of the file was not kept";
	if (why == NULL)
	{
		if ((Rld_FindEntry(&reader, "CVOI", &index) == NULL) || (index < 0))
		{
			why = "CVOI is no longer in the file";
		}
		else
		{
			bytes = Rld_ReadChunk(&reader, index, &size, &why);
			if ((bytes == NULL) && (why == NULL))
			{
				why = "CVOI cannot be read";
			}
		}
		Rld_Close(&reader);
	}

	if (bytes != NULL)
	{
		why = RldChar_CheckVoices(bytes, (u64)size, &f->voices);
	}

	if (why != NULL)
	{
		free(bytes);
		memset(&f->voices, 0, sizeof(f->voices));
		f->voiceState = NATIVE_CHAR_VOICES_IGNORED;
		f->voiceWhy = why;
		Platform_Log("[CTR Char] voices %s: CVOI ignored - %s - silent\n", f->file, why);
		return;
	}

	f->cvoi = bytes;
}

// Every held CVOI is let go with the seats; both clip places are silenced
// first, the mixer may still read from the bytes. Nothing without a held one.
internal void NativeChar_ReleaseVoices(void)
{
	int held = 0;
	int e;

	for (e = 0; e < s_charRosterFiles; e++)
	{
		held += (s_charFiles[e].cvoi != NULL) ? 1 : 0;
	}

	if (held == 0)
	{
		return;
	}

	NativeAudio_StopPcmClips();
	for (e = 0; e < s_charRosterFiles; e++)
	{
		free(s_charFiles[e].cvoi);
		s_charFiles[e].cvoi = NULL;
	}
}

void NativeChar_ClearSeats(void)
{
	memset(s_seat, 0, sizeof(s_seat));
	NativeChar_ReleaseVoices();
}

// The first reason why this race is not one the funnel binds in, NULL when it
// is: arcade, one player, no time trial, adventure, relic, battle or cutscene
// bit. The crystal and CTR challenge bits do not refuse: the menu keeps the
// pick at -1 there (NativeChar_ModeAllowed), and the direct start may bind.
internal const char *NativeChar_ModeRefusal(const struct GameTracker *gGT)
{
	const u32 mode1 = (u32)gGT->gameMode1;

	if ((mode1 & ARCADE_MODE) == 0)
	{
		return "not arcade";
	}

	if (gGT->numPlyrCurrGame != 1)
	{
		return "not one player";
	}

	if ((mode1 & TIME_TRIAL) != 0)
	{
		return "time trial";
	}

	if ((mode1 & (ADVENTURE_MODE | ADVENTURE_ARENA | ADVENTURE_CUP | ADVENTURE_BOSS)) != 0)
	{
		return "adventure";
	}

	if ((mode1 & RELIC_RACE) != 0)
	{
		return "relic race";
	}

	if ((mode1 & BATTLE_MODE) != 0)
	{
		return "battle";
	}

	if (((mode1 & GAME_CUTSCENE) != 0) || ((gGT->gameMode2 & CREDITS) != 0))
	{
		return "cutscene";
	}

	return NULL;
}

// The load of the podium at the end of an arcade cup: UI_CupStandings sets the
// empty reward STATIC_BIG1 and loads Gem Stone Valley (game/UI/
// UI_CupStandings.c), the hub detection of the load adds ADVENTURE_ARENA to the
// arcade bits (game/LOAD/LOAD_TenStages.c). A regular way with the pick still
// set, refused as "adventure" by NativeChar_ModeRefusal. The adventure sets
// STATIC_BIG1 too (Oxide, game/222.c), but never without ADVENTURE_MODE.
internal int NativeChar_ArcadeCupPodium(const struct GameTracker *gGT)
{
	const u32 mode1 = (u32)gGT->gameMode1;

	return ((mode1 & ARCADE_MODE) != 0) && ((mode1 & ADVENTURE_ARENA) != 0) &&
	       ((mode1 & (ADVENTURE_MODE | ADVENTURE_CUP | ADVENTURE_BOSS)) == 0) && (gGT->levelID == GEM_STONE_VALLEY) &&
	       (gGT->podiumRewardID == STATIC_BIG1);
}

// The model retail gives seat 0 on this template: the one in the driver pack
// whose 16-byte name is the template's debug name - the search of
// VehBirth_GetModelByName (game/Vehicle/VehBirth.c) over sdata->PLYROBJECTLIST,
// set just before in load stage 5. Its extra model slots
// (data.driverModelExtras) are left out: in the modes the funnel lets through
// LOAD_DriverMPK queues none (game/LOAD/LOAD_Assets.c:239-245), and in stage 5
// a slot is not yet a model of this load - stage 4 empties it, stage 6 turns a
// file start into a model - so it can still hold a model from an earlier load.
internal const struct Model *NativeChar_DonorModel(int templateId)
{
	struct Model **models = (struct Model **)sdata->PLYROBJECTLIST;
	const char *name = data.MetaDataCharacters[templateId].name_Debug;
	int i;

	if ((models == NULL) || (name == NULL))
	{
		return NULL;
	}

	for (i = 0; (i < NATIVE_CHAR_DONOR_SCAN_MAX) && (models[i] != NULL); i++)
	{
		int same = 1;
		int word;

		for (word = 0; word < MODEL_NAME_WORD_COUNT; word++)
		{
			if (ModelName_ReadWord(models[i]->name, word) != ModelName_ReadWord(name, word))
			{
				same = 0;
				break;
			}
		}

		if (same)
		{
			return models[i];
		}
	}

	return NULL;
}

// Frames of animation a in header 0, counted as the game counts them
// (VehFrameInst_GetNumAnimFrames, game/Vehicle/VehFrame.c:50-84); 0 where
// there is no such animation.
internal u32 NativeChar_AnimFrames(const struct Model *model, int a)
{
	const struct ModelHeader *mh;
	const struct ModelAnim *anim;

	if ((model == NULL) || (model->numHeaders <= 0) || (model->headers == NULL))
	{
		return 0;
	}

	mh = model->headers;
	if ((a >= (int)mh->numAnimations) || (mh->ptrAnimations == NULL))
	{
		return 0;
	}

	anim = mh->ptrAnimations[a];
	if (anim == NULL)
	{
		return 0;
	}

	return anim->numFrames & NATIVE_CHAR_FRAME_MASK;
}

// ---------------------------------------------------------------------------
// THE DRAW RESERVE. A custom model is drawn into the same draw memory as
// everything else (gGT->db[].primMem, game/MAIN/MainInit.c), and the
// instances come before the track (game/MAIN/MainFrame_RenderFrame.c): what a
// larger model takes, the track misses at the end of the frame ("draw memory
// full", game/226). So the memory of a load grows by what its custom models
// may draw, and the table of the level keeps its budget for everything else.
//
// Sized in load stage 0 (MainInit_PrimMem), before the funnel of stage 5 has
// decided, from what decides it there and is already known: the main menu
// level reserves the largest model of the roster (the driver select draws one
// entry's model at a time, game/230/MM_NativeCharGrid.c); a race reserves the
// picked file for seat 0 when the mode lets the funnel bind and
// characterIDs[0] holds the file's template, and with --dev-char-seats every
// seat on the file that load takes. Only the frame counts of the donor are
// not known yet: a load the funnel then refuses for them leaves its share
// unused. Each load's bytes times NATIVE_CHAR_DRAW_PASSES - a reflective floor
// draws an instance twice (RenderBucket_DrawReflectionPrimitive). Not scaled
// with the canvas: what a model draws does not depend on the picture's width.
//
// WHAT ONE DRAW COSTS. The normal writer takes 28 bytes per triangle without
// texture and 40 with one - the draw bytes of RldChar_CheckModel, every
// triangle, no back face culled. An invisible driver (the item,
// game/Vehicle/VehPickupItem.c, and the cheat, game/Vehicle/VehBirth.c, which
// only makes the seats of players invisible) is drawn by the ghost writer
// (RenderBucket_DrawInstPrim_GhostAtRange,
// game/RenderBucket/RenderBucket_QueueExecute.c): a mask packet in front of
// every triangle, 60 bytes without texture, 64 with one. A bot never turns
// invisible in the races the funnel binds in - game/PickupBots.c fires only
// bombs, missiles, TNT and potions - so seat 0 is reserved as a ghost and
// every other seat at its draw bytes. The own mask stays on the normal writer
// while its driver is invisible (game/231/RB_MaskShieldCloud.c).
//
// NOT IN IT: a triangle that crosses the split line of an instance is cut into
// up to three (RenderBucket_DrawSplitClipped; on a reflective floor
// RenderBucket_DrawWaterSplitClipped in both passes). Only the triangles on
// that one line pay - a band of the model, not all of it - and how wide the
// band gets is not determined; a visible seat 0 has the ghost's margin (60
// against 28 bytes) for it. The measuring runs of --dev-char-seats name what
// was missing per load ("instances dropped", "draw memory full").
//
// RETAIL STAYS: no file in the roster, reserve 0, no line, and every byte of
// the draw memory and of the MEMPACK is what it was. A race on a retail tile
// reserves nothing either; only the main menu level grows once a file is in
// the roster - at its end, behind everything the table gives it.
// ---------------------------------------------------------------------------

#define NATIVE_CHAR_DRAW_PASSES 2u
#define NATIVE_CHAR_GHOST_BYTES_G3 60u  // mask packet 0x1C, draw mode 8, tagless G3 0x18 (RenderBucketGhostFlatPacket)
#define NATIVE_CHAR_GHOST_BYTES_GT3 64u // mask packet 0x1C, tagless GT3 0x24 (RenderBucketGhostTexturedPacket)

// The kinds of load the reserve knows.
enum
{
	NATIVE_CHAR_LOAD_MENU = 0,  // the driver select: one model, never a ghost
	NATIVE_CHAR_LOAD_SEAT0 = 1, // a race with the pick on seat 0: ghost and own mask
	NATIVE_CHAR_LOAD_DEV = 2,   // --dev-char-seats: seat 0 a ghost, the other seats at their draw bytes
};

// --dev-char-seats: the roster entry of the next race load.
internal int NativeChar_DevSeatsEntry(void)
{
	if ((s_charRosterFiles == 0) || (s_charDevSeats != NATIVE_CHAR_DEV_SEATS_CYCLE))
	{
		return 0;
	}

	return s_charDevLoads % s_charRosterFiles;
}

// One ghost draw of a file's model: drawBytes = 28 x G3 + 40 x GT3 and
// triangles = G3 + GT3, so GT3 = (drawBytes - 28 x triangles) / 12.
internal u32 NativeChar_FileGhostBytes(const struct NativeCharFile *f)
{
	const u32 plain = f->triangles * RLDCHAR_DRAW_BYTES_G3;
	const u32 gt3 = (f->drawBytes > plain) ? ((f->drawBytes - plain) / (RLDCHAR_DRAW_BYTES_GT3 - RLDCHAR_DRAW_BYTES_G3)) : 0u;
	const u32 g3 = (f->triangles > gt3) ? (f->triangles - gt3) : 0u;

	return (g3 * NATIVE_CHAR_GHOST_BYTES_G3) + (gt3 * NATIVE_CHAR_GHOST_BYTES_GT3);
}

// One buffer, one pass: what a load of this kind may draw with entry's model;
// *seat0 the share of seat 0 (with the own mask in a race of the pick),
// *other that of every other seat. 0 outside the files.
internal u32 NativeChar_LoadBytes(int kind, int entry, u32 *seat0, u32 *other)
{
	const struct NativeCharFile *f;

	*seat0 = 0;
	*other = 0;

	if ((entry < 0) || (entry >= s_charRosterFiles))
	{
		return 0;
	}

	f = &s_charFiles[entry];
	if (kind == NATIVE_CHAR_LOAD_MENU)
	{
		*seat0 = f->drawBytes;
		return *seat0;
	}

	*seat0 = NativeChar_FileGhostBytes(f);
	if (kind == NATIVE_CHAR_LOAD_SEAT0)
	{
		*seat0 += (f->maskState == NATIVE_CHAR_MASK_OWN) ? f->maskDrawBytes : 0u;
		return *seat0;
	}

	*other = f->drawBytes;
	return *seat0 + ((u32)(NATIVE_CHAR_SEATS - 1) * *other);
}

// The entry whose load of this kind costs the most (the first of equal ones),
// -1 without a file.
internal int NativeChar_LargestEntry(int kind)
{
	u32 largest = 0;
	int found = -1;
	int entry;

	for (entry = 0; entry < s_charRosterFiles; entry++)
	{
		u32 seat0;
		u32 other;
		const u32 bytes = NativeChar_LoadBytes(kind, entry, &seat0, &other);

		if ((found < 0) || (bytes > largest))
		{
			largest = bytes;
			found = entry;
		}
	}

	return found;
}

u32 NativeChar_DrawReserve(int tableBytes)
{
	const struct GameTracker *gGT = sdata->gGT;
	char detail[192];
	u32 seat0;
	u32 other;
	u32 bytes;
	int kind;
	int entry;

	if ((s_charRosterFiles == 0) || (gGT == NULL))
	{
		return 0;
	}

	if ((gGT->gameMode1 & MAIN_MENU) != 0)
	{
		// Only the level of the driver select; the garage and the other menu
		// loads never draw a file's model.
		if (gGT->levelID != MAIN_MENU_LEVEL)
		{
			return 0;
		}

		kind = NATIVE_CHAR_LOAD_MENU;
		entry = NativeChar_LargestEntry(kind);
	}
	else if (NativeChar_ModeRefusal(gGT) != NULL)
	{
		return 0;
	}
	else if (s_charDevSeats != NATIVE_CHAR_DEV_SEATS_OFF)
	{
		// Every seat on the file the funnel will take (NativeChar_ArmDevSeats);
		// a dev seat wears the retail mask.
		kind = NATIVE_CHAR_LOAD_DEV;
		entry = NativeChar_DevSeatsEntry();
	}
	else
	{
		// Seat 0, when the pick is a file on the template the seat runs on.
		kind = NATIVE_CHAR_LOAD_SEAT0;
		entry = s_charPick;
		if ((entry < 0) || (entry >= s_charRosterFiles) || ((int)data.characterIDs[0] != NativeChar_EntryTemplate(entry)))
		{
			return 0;
		}
	}

	bytes = NativeChar_LoadBytes(kind, entry, &seat0, &other) * NATIVE_CHAR_DRAW_PASSES;
	if (bytes == 0u)
	{
		return 0;
	}

	if (kind == NATIVE_CHAR_LOAD_MENU)
	{
		snprintf(detail, sizeof(detail), "main menu, the largest file: 1 model x %u bytes", (unsigned)seat0);
	}
	else if (kind == NATIVE_CHAR_LOAD_SEAT0)
	{
		snprintf(detail, sizeof(detail), "race, seat 0: %u bytes as a ghost with the own mask", (unsigned)seat0);
	}
	else
	{
		snprintf(detail, sizeof(detail), "race, --dev-char-seats: seat 0 %u bytes as a ghost + %d x %u bytes", (unsigned)seat0, NATIVE_CHAR_SEATS - 1,
		         (unsigned)other);
	}

	Platform_Log("[CTR Char] draw memory: %d bytes + %u for custom models (%s, x %u passes, %s) = %d\n", tableBytes, (unsigned)bytes, detail,
	             (unsigned)NATIVE_CHAR_DRAW_PASSES, s_charFiles[entry].file, tableBytes + (int)bytes);
	return bytes;
}

u32 NativeChar_MempackExtraNeeded(void)
{
	// The largest reserve any load can ask for - the main menu's, a race's of
	// seat 0, and with --dev-char-seats every seat's - for both buffers
	// (db[0], db[1]).
	u32 largest = 0;
	int kind;

	for (kind = NATIVE_CHAR_LOAD_MENU; kind <= NATIVE_CHAR_LOAD_DEV; kind++)
	{
		u32 seat0;
		u32 other;
		u32 bytes;

		if ((kind == NATIVE_CHAR_LOAD_DEV) && (s_charDevSeats == NATIVE_CHAR_DEV_SEATS_OFF))
		{
			continue;
		}

		bytes = NativeChar_LoadBytes(kind, NativeChar_LargestEntry(kind), &seat0, &other);
		largest = (bytes > largest) ? bytes : largest;
	}

	return 2u * NATIVE_CHAR_DRAW_PASSES * largest;
}

// ---------------------------------------------------------------------------
// THE ROSTER, as the driver select reads it. Every function answers for any
// index: outside the roster -1, "", 0 or NULL.
// ---------------------------------------------------------------------------

int NativeChar_RosterCount(void)
{
	return s_charRosterCount;
}

// The number n of "PLACEHOLDER <n>" (1-based) for a placeholder entry, else 0.
internal int NativeChar_PlaceholderNumber(int entry)
{
	if ((entry < s_charRosterFiles) || (entry >= s_charRosterCount))
	{
		return 0;
	}

	return entry - s_charRosterFiles + 1;
}

int NativeChar_EntryTemplate(int entry)
{
	const int n = NativeChar_PlaceholderNumber(entry);

	// Placeholders walk through the templates 0..14 in order.
	if (n > 0)
	{
		return (n - 1) % (RLDCHAR_TEMPLATE_MAX + 1);
	}

	if ((entry >= 0) && (entry < s_charRosterFiles))
	{
		return (int)s_charFiles[entry].info.templateId;
	}

	return -1;
}

const char *NativeChar_EntryName(int entry)
{
	const int n = NativeChar_PlaceholderNumber(entry);

	if (n > 0)
	{
		return s_charPlaceholderName[n - 1];
	}

	if ((entry >= 0) && (entry < s_charRosterFiles))
	{
		return s_charFiles[entry].info.name;
	}

	return "";
}

int NativeChar_EntryIsPlaceholder(int entry)
{
	return NativeChar_PlaceholderNumber(entry) > 0;
}

struct Model *NativeChar_EntryModel(int entry)
{
	// A file is in the roster only once its model is loaded and relocated.
	if ((entry >= 0) && (entry < s_charRosterFiles))
	{
		return s_charFiles[entry].model;
	}

	return NULL;
}

// The pose of the menu preview. A file carries a race model, not a menu model:
// its frame 0 of animation 0 - what the menu shows a retail menu model in - is
// one end of the steering animation. The middle frame is the one a race starts
// the drive animation on (VehFrameInst_GetStartFrame, game/Vehicle/VehFrame.c):
// 21 frames -> 10.
int NativeChar_EntryMenuFrame(int entry)
{
	const struct Model *model = NativeChar_EntryModel(entry);

	if (model == NULL)
	{
		return 0;
	}

	return (int)(NativeChar_AnimFrames(model, 0) >> 1);
}

// Keyed on the model, not on a seat: whatever instance draws the model of a
// file asks here - seat 0 in a race and the driver instance of the driver
// select preview alike. Called per instance and frame, so no log line. An
// empty roster never enters the loop: 0, the retail wheels.
int NativeChar_ModelHidesWheels(const struct Model *model)
{
	int entry;

	if (model == NULL)
	{
		return 0;
	}

	for (entry = 0; (entry < s_charRosterFiles) && (entry < NATIVE_CHAR_ROSTER_MAX); entry++)
	{
		if (s_charFiles[entry].model == model)
		{
			return (s_charFiles[entry].info.flags & RLDCHAR_FLAG_NO_WHEELS) != 0;
		}
	}

	return 0;
}

// Keyed on the model like NativeChar_ModelHidesWheels, and the own mask of a
// file counts as its model: the bit is about every model in the file. Called
// once per instance draw, not per vertex. Without a file that sets the bit -
// a retail run, or custom files from before the bit - it answers 0 at once.
int NativeChar_ModelFullHeight(const struct Model *model)
{
	int entry;

	if ((s_charFullHeightFiles == 0) || (model == NULL))
	{
		return 0;
	}

	for (entry = 0; (entry < s_charRosterFiles) && (entry < NATIVE_CHAR_ROSTER_MAX); entry++)
	{
		if ((s_charFiles[entry].model == model) || (s_charFiles[entry].mask == model))
		{
			return (s_charFiles[entry].info.flags & RLDCHAR_FLAG_FULL_HEIGHT) != 0;
		}
	}

	return 0;
}

// ---------------------------------------------------------------------------
// THE PORTRAITS (native_chars.h, NATIVE_CHAR_PORTRAIT_SLOTS). Slot e belongs to
// entry e; a slot is uploaded only for a file with a usable CICN, so a run
// without such a file writes nothing into VRAM and logs nothing here.
//
// The strip x 256..511, y 266..295 is written by nothing else that is known:
// no retail file that the game loads and none of the track containers
// measured so far, and no code path but the clear at start and the restore of
// a quick state (whole VRAM). The VRMD of a foreign container is not checked
// for it (Rld_CheckVrm holds length and chain, not the place). Every upload is
// bracketed with NativeRenderer_StripOwnWrites, so the strip counter of the
// renderer tells these writes from any foreign one.
//
// WHEN: at the first NativeChar_EntryPortrait after NativeChar_PortraitsDirty,
// which the grid calls on every entering of the driver select
// (MM_NativeCharGrid_Enter) - after a race, a track select, a quick state, a
// video, whatever used the VRAM in between. At most 20 slots x 2 small
// LoadImage.
//
// IN A RACE: the race HUD (game/UI/UI_Rank.c), the arcade results
// (game/222.c) and the cup standings (game/UI/UI_CupStandings.c) ask
// NativeChar_SeatPortrait for the portrait of a seat: a bound seat gets the
// portrait of its file, every other seat the template's icon unchanged. The
// race icons (the driver pack, 43 x 25 like the menu's) lie elsewhere in VRAM;
// no retail track, podium or pack file and no container measured so far
// writes the strip, and binding a seat marks the portraits dirty, so the first HUD draw of a race uploads them - also on a
// direct start that never saw the driver select.
// ---------------------------------------------------------------------------

#define NATIVE_CHAR_PORTRAIT_STRIP_X 256
#define NATIVE_CHAR_PORTRAIT_STRIP_Y 266
#define NATIVE_CHAR_PORTRAIT_PAGE_Y 256        // y base of a page with tpage bit 0x10
#define NATIVE_CHAR_PORTRAIT_PAGE_W 64         // halfwords of a texture page
#define NATIVE_CHAR_PORTRAIT_PAGE_BIT_Y 0x10   // tpage: y base 256
#define NATIVE_CHAR_PORTRAIT_PER_PAGE 5        // 5 x 11 halfwords = 55 of 64
#define NATIVE_CHAR_PORTRAIT_CLUT_Y 292
#define NATIVE_CHAR_PORTRAIT_CLUTS_PER_ROW 16  // 16 x 16 halfwords = the width of the strip
#define NATIVE_CHAR_PORTRAIT_TEXELS_PER_WORD 4 // 4 bit
#define NATIVE_CHAR_PORTRAIT_CLUT_WORD (0x08u / 2u)                  // CICN byte 0x08
#define NATIVE_CHAR_PORTRAIT_TEXEL_WORD (RLDCHAR_ICON_WORDS_AT / 2u) // CICN byte 0x28

// The tpage bits a slot sets anew: page x (0..3), y base (4), depth (7..8) and
// bit 11; abr (5..6) stays the template's - TRANS_50_DECAL sets it anyway.
#define NATIVE_CHAR_PORTRAIT_TPAGE_MASK 0x099fu

global_variable struct Icon s_portraitIcon[NATIVE_CHAR_PORTRAIT_SLOTS];
global_variable int s_portraitsDirty = 1;
global_variable int s_portraitGeometryLogged;
global_variable u8 s_portraitSizeLogged[NATIVE_CHAR_PORTRAIT_SLOTS];

int NativeChar_PortraitSlot(int slot, struct NativeCharPortraitSlot *out)
{
	int page;
	int pageX;

	if ((out == NULL) || (slot < 0) || (slot >= NATIVE_CHAR_PORTRAIT_SLOTS))
	{
		return 0;
	}

	page = slot / NATIVE_CHAR_PORTRAIT_PER_PAGE;
	pageX = NATIVE_CHAR_PORTRAIT_STRIP_X + (NATIVE_CHAR_PORTRAIT_PAGE_W * page);

	out->texelX = pageX + ((int)RLDCHAR_ICON_ROW_WORDS * (slot % NATIVE_CHAR_PORTRAIT_PER_PAGE));
	out->texelY = NATIVE_CHAR_PORTRAIT_STRIP_Y;
	out->texelW = (int)RLDCHAR_ICON_ROW_WORDS;
	out->texelH = (int)RLDCHAR_ICON_HEIGHT;
	out->clutX = NATIVE_CHAR_PORTRAIT_STRIP_X + ((int)RLDCHAR_ICON_COLORS * (slot % NATIVE_CHAR_PORTRAIT_CLUTS_PER_ROW));
	out->clutY = NATIVE_CHAR_PORTRAIT_CLUT_Y + (slot / NATIVE_CHAR_PORTRAIT_CLUTS_PER_ROW);
	out->clutW = (int)RLDCHAR_ICON_COLORS;
	out->u0 = (out->texelX - pageX) * NATIVE_CHAR_PORTRAIT_TEXELS_PER_WORD;
	out->v0 = NATIVE_CHAR_PORTRAIT_STRIP_Y - NATIVE_CHAR_PORTRAIT_PAGE_Y;
	out->pageBits = ((pageX / NATIVE_CHAR_PORTRAIT_PAGE_W) & 0xf) | NATIVE_CHAR_PORTRAIT_PAGE_BIT_Y;
	return 1;
}

int NativeChar_PortraitLayout(int slot, const struct TextureLayout *templateLayout, struct TextureLayout *out)
{
	struct NativeCharPortraitSlot place;
	int w;
	int h;

	if ((templateLayout == NULL) || (out == NULL) || !NativeChar_PortraitSlot(slot, &place))
	{
		return 0;
	}

	// The size the template's portrait is drawn with (DecalHUD_DrawPolyGT4,
	// game/DecalHUD.c: u1 - u0 by v2 - v0). The retail size is not written in
	// the code; up to 44 x 26 the tile shows that part of the own portrait, a
	// larger one would sample beyond the slot.
	w = (int)templateLayout->u1 - (int)templateLayout->u0;
	h = (int)templateLayout->v2 - (int)templateLayout->v0;
	if ((w < 1) || (w > (int)RLDCHAR_ICON_WIDTH) || (h < 1) || (h > (int)RLDCHAR_ICON_HEIGHT))
	{
		return 0;
	}

	*out = *templateLayout;
	out->u0 = (u8)place.u0;
	out->u2 = (u8)place.u0;
	out->u1 = (u8)(place.u0 + w);
	out->u3 = (u8)(place.u0 + w);
	out->v0 = (u8)place.v0;
	out->v1 = (u8)place.v0;
	out->v2 = (u8)(place.v0 + h);
	out->v3 = (u8)(place.v0 + h);
	out->clut = (u16)((place.clutY << 6) | (place.clutX >> 4));
	out->tpage = (u16)((templateLayout->tpage & ~(u16)NATIVE_CHAR_PORTRAIT_TPAGE_MASK) | (u16)place.pageBits);
	return 1;
}

void NativeChar_PortraitsDirty(void)
{
	s_portraitsDirty = 1;
}

// Every own portrait into its slot: texels and CLUT, then one VRAM update.
internal void NativeChar_PortraitsUpload(void)
{
	int uploaded = 0;
	int entry;

	s_portraitsDirty = 0;

	for (entry = 0; (entry < s_charRosterFiles) && (entry < NATIVE_CHAR_PORTRAIT_SLOTS); entry++)
	{
		struct NativeCharFile *f = &s_charFiles[entry];
		struct NativeCharPortraitSlot place;
		RECT16 rect;

		if ((f->icon != NATIVE_CHAR_ICON_OWN) || !NativeChar_PortraitSlot(entry, &place))
		{
			continue;
		}

		if (uploaded == 0)
		{
			NativeRenderer_StripOwnWrites(1);
		}

		rect.x = (s16)place.texelX;
		rect.y = (s16)place.texelY;
		rect.w = (s16)place.texelW;
		rect.h = (s16)place.texelH;
		LoadImage(&rect, &f->iconWords[NATIVE_CHAR_PORTRAIT_TEXEL_WORD]);

		rect.x = (s16)place.clutX;
		rect.y = (s16)place.clutY;
		rect.w = (s16)place.clutW;
		rect.h = 1;
		LoadImage(&rect, &f->iconWords[NATIVE_CHAR_PORTRAIT_CLUT_WORD]);

		uploaded++;
	}

	if (uploaded == 0)
	{
		return;
	}

	NativeRenderer_StripOwnWrites(0);

	// Visible at once, as LoadImage2 makes the minimap
	// (game/230/MM_NativeTrackSelect.c, MM_NativeTrackSelect_MapUpload).
	NativeRenderer_UpdateVRAM();
	Platform_Log("[CTR Char] portraits: %d uploaded to the strip at vblank %d\n", uploaded, Platform_GetVBlankCount());
}

struct Icon *NativeChar_SeatPortrait(int seat, struct Icon *templateIcon)
{
	struct Icon *icon;
	int entry;

	if (NativeChar_SeatModel(seat) == NULL)
	{
		return templateIcon;
	}

	entry = s_seat[seat].entry;
	icon = NativeChar_EntryPortrait(entry, templateIcon);

	// One line per load, at the first portrait the race draws for the seat.
	if (s_seat[seat].portraitNoted == 0)
	{
		const struct NativeCharFile *f = &s_charFiles[entry];

		s_seat[seat].portraitNoted = 1;
		if (icon != templateIcon)
		{
			Platform_Log("[CTR Char] hud portrait seat %d: own (slot %d, %s, tpage 0x%04x -> 0x%04x)\n", seat, entry, f->file,
			             (unsigned)templateIcon->texLayout.tpage, (unsigned)icon->texLayout.tpage);
		}
		else
		{
			const char *why = (f->icon == NATIVE_CHAR_ICON_NONE) ? "no CICN" : ((f->icon == NATIVE_CHAR_ICON_IGNORED) ? "CICN ignored" : "no slot or size");

			Platform_Log("[CTR Char] hud portrait seat %d: the template's (%s, %s)\n", seat, why, f->file);
		}
	}

	return icon;
}

struct Icon *NativeChar_EntryPortrait(int entry, struct Icon *templateIcon)
{
	struct TextureLayout layout;
	const struct NativeCharFile *f;
	struct Icon *own;

	if ((templateIcon == NULL) || (entry < 0) || (entry >= s_charRosterFiles) || (entry >= NATIVE_CHAR_PORTRAIT_SLOTS))
	{
		return templateIcon;
	}

	f = &s_charFiles[entry];
	if (f->icon != NATIVE_CHAR_ICON_OWN)
	{
		return templateIcon;
	}

	if (!NativeChar_PortraitLayout(entry, &templateIcon->texLayout, &layout))
	{
		if (!s_portraitSizeLogged[entry])
		{
			s_portraitSizeLogged[entry] = 1;
			Platform_Log("[CTR Char] portrait %s: the template's portrait is %d x %d, a slot holds %u x %u - the template's\n", f->file,
			             (int)templateIcon->texLayout.u1 - (int)templateIcon->texLayout.u0, (int)templateIcon->texLayout.v2 - (int)templateIcon->texLayout.v0,
			             (unsigned)RLDCHAR_ICON_WIDTH, (unsigned)RLDCHAR_ICON_HEIGHT);
		}

		return templateIcon;
	}

	if (!s_portraitGeometryLogged)
	{
		s_portraitGeometryLogged = 1;
		Platform_Log("[CTR Char] portrait geometry: template %d x %d, tpage 0x%04x\n", (int)templateIcon->texLayout.u1 - (int)templateIcon->texLayout.u0,
		             (int)templateIcon->texLayout.v2 - (int)templateIcon->texLayout.v0, (unsigned)templateIcon->texLayout.tpage);
	}

	if (s_portraitsDirty)
	{
		NativeChar_PortraitsUpload();
	}

	own = &s_portraitIcon[entry];
	*own = *templateIcon;
	own->texLayout = layout;
	return own;
}

// ---------------------------------------------------------------------------
// THE PICK
// ---------------------------------------------------------------------------

void NativeChar_SetPick(int entry)
{
	// Only an entry of the roster is kept: whatever else arrives means retail.
	s_charPick = ((entry >= 0) && (entry < s_charRosterCount)) ? entry : -1;
}

int NativeChar_Pick(void)
{
	return s_charPick;
}

void NativeChar_PickForJump(void)
{
	// The direct start has no driver select: it takes the --char file when it
	// loaded. A file from the folder is never picked here - a --level start
	// stays retail unless --char asks otherwise. Whether it binds is still the
	// funnel's answer - --driver has to have put the template into
	// characterIDs[0].
	NativeChar_SetPick((s_charGiven && (s_charRosterFiles > 0)) ? 0 : -1);
}

// The modes a custom character may be picked in, read before the race exists:
// the main menu has set ARCADE_MODE and cleared the others
// (game/230/MM_MenuFlow.c), the race type box has set the NITRO-PIT marker
// (game/native_menuscreen.c). Not the CRYSTAL_CHALLENGE or TOKEN_RACE bits -
// the track select sets those only after the driver select.
int NativeChar_ModeAllowed(void)
{
	const struct GameTracker *gGT = sdata->gGT;
	u32 mode1;
	int chosen;

	if (gGT == NULL)
	{
		return 0;
	}

	mode1 = (u32)gGT->gameMode1;
	chosen = MM_NativeTrackSelect_Chosen();

	return ((mode1 & ARCADE_MODE) != 0) && ((mode1 & (TIME_TRIAL | ADVENTURE_MODE | BATTLE_MODE)) == 0) && (gGT->numPlyrNextGame == 1) &&
	       (gGT->boolDemoMode == 0) && (chosen != MM_NATIVE_CHOSEN_CRYSTAL) && (chosen != MM_NATIVE_CHOSEN_CTR);
}

// After a binding: who sits where in this race - the character id of every
// seat, the file of a bound one, and the minimap color it gets. Only then: a
// race without a custom character keeps its log as it was.
internal void NativeChar_LogSeats(void)
{
	char line[512];
	size_t used = 0;
	int seat;

	line[0] = '\0';
	for (seat = 0; seat < NATIVE_CHAR_SEATS; seat++)
	{
		int written;

		if (s_seat[seat].model != NULL)
		{
			if (s_seat[seat].hasMapColor)
			{
				// RRGGBB as rldpack and Reload Studio show it; the color word is 0x00BBGGRR.
				const u32 c = s_seat[seat].mapColor[0];

				written = snprintf(&line[used], sizeof(line) - used, " %d=%d (%s, map color %02X%02X%02X)", seat, (int)data.characterIDs[seat],
				                   s_charFiles[s_seat[seat].entry].file, (unsigned)(c & 0xffu), (unsigned)((c >> 8) & 0xffu), (unsigned)((c >> 16) & 0xffu));
			}
			else
			{
				written = snprintf(&line[used], sizeof(line) - used, " %d=%d (%s, map color of the template)", seat, (int)data.characterIDs[seat],
				                   s_charFiles[s_seat[seat].entry].file);
			}
		}
		else
		{
			written = snprintf(&line[used], sizeof(line) - used, " %d=%d", seat, (int)data.characterIDs[seat]);
		}

		// Too long (a very long file name): the seat that does not fit is cut
		// whole, and the line says so.
		if ((written < 0) || ((size_t)written >= (sizeof(line) - used)))
		{
			line[used] = '\0';
			Platform_Log("[CTR Char] seats:%s ... (cut at seat %d)\n", line, seat);
			return;
		}
		used += (size_t)written;
	}

	Platform_Log("[CTR Char] seats:%s\n", line);
}

// --dev-char-seats: every seat of a one-player arcade race on the model of one
// file. The bot seats 1..7 are first put on the file's template - THE ONE PLACE
// that writes data.characterIDs, and only on this developer way: the arcade
// pack of the load (BI_1PARCADEPACK + characterIDs[0], game/LOAD/LOAD_Assets.c)
// holds seat 0's driver and the bots LOAD_Robots1P chose in stage 4, so a bot
// on another template has no donor of the file's frame counts. Run with
// --driver <the file's template> and seat 0 too finds its donor in the pack;
// every seat then shares the one host model (one instance each). Class and
// drive values are those of the template on every seat (the bots all run on
// it), the own mask is not worn (the retail one is), the map color is the
// template's. A seat without a donor or whose donor's frame counts differ
// stays retail. A load the funnel would refuse (demo, mode) binds nothing and
// writes nothing.
internal void NativeChar_ArmDevSeats(struct GameTracker *gGT)
{
	const char *modeWhy = NativeChar_ModeRefusal(gGT);
	const int entry = NativeChar_DevSeatsEntry();
	const int fileTemplate = (int)s_charFiles[entry].info.templateId;
	int bound = 0;
	int seat;

	if ((gGT->boolDemoMode != 0) || (modeWhy != NULL))
	{
		Platform_Log("[CTR Char] dev seats: not bound (%s)\n", (modeWhy != NULL) ? modeWhy : "demo");
		return;
	}

	s_charDevLoads++;

	// Stage 5: LOAD_Robots1P has filled the bot seats in stage 4, the drivers
	// are born after the load (VehBirth reads characterIDs[seat]).
	for (seat = 1; seat < NATIVE_CHAR_SEATS; seat++)
	{
		data.characterIDs[seat] = (s16)fileTemplate;
	}

	for (seat = 0; seat < NATIVE_CHAR_SEATS; seat++)
	{
		const int templateId = (int)data.characterIDs[seat];
		const struct Model *donor = ((templateId >= 0) && (templateId <= RLDCHAR_TEMPLATE_MAX)) ? NativeChar_DonorModel(templateId) : NULL;
		int same = (donor != NULL);
		int a;

		for (a = 0; same && (a < RLDCHAR_ANIM_COUNT); a++)
		{
			same = (NativeChar_AnimFrames(s_charFiles[entry].model, a) == NativeChar_AnimFrames(donor, a));
		}

		if (!same)
		{
			Platform_Log("[CTR Char] dev seats: seat %d stays retail (template %d, %s)\n", seat, templateId,
			             (donor == NULL) ? "no donor in the driver pack" : "other frame counts");
			continue;
		}

		s_seat[seat].model = s_charFiles[entry].model;
		s_seat[seat].entry = entry;
		s_seat[seat].motorId = templateId;
		s_seat[seat].devSeat = 1;
		bound++;
	}

	if (bound > 0)
	{
		NativeChar_HoldVoices(entry);
	}

	// From here until the next load arms its seats, drops count for this one.
	s_charDevLoadOpen = 1;
	s_charDevLoadLevel = gGT->levelID;
	s_charDevDroppedTotal = 0;
	s_charDevDroppedSeat0 = 0;
	s_charDevDroppedBound = 0;

	NativeChar_PortraitsDirty();
	Platform_Log("[CTR Char] dev seats: %s, load %d on level %d, %d of %d seats = %s (%u draw bytes), bots on template %d\n",
	             (s_charDevSeats == NATIVE_CHAR_DEV_SEATS_CYCLE) ? "cycle" : "all", s_charDevLoads, gGT->levelID, bound, NATIVE_CHAR_SEATS,
	             s_charFiles[entry].file, (unsigned)s_charFiles[entry].drawBytes, fileTemplate);
	NativeChar_LogSeats();
}

void NativeChar_ArmSeats(void)
{
	struct GameTracker *gGT = sdata->gGT;
	const int pick = s_charPick;
	struct Model *model;
	int templateId;
	const char *modeWhy;
	const struct Model *donor;
	int a;

	// Nothing from an earlier load survives into this one; a load of
	// --dev-char-seats names what it dropped first.
	NativeChar_FlushDevLoad();
	NativeChar_ClearSeats();

	// 1. A menu load, silently. characterIDs[0] keeps the template on the way
	//    back into the menu, and the menu births drivers too
	//    (VehBirth_NonGhost). A load of the main menu onto the title (quit,
	//    race end, cup end, demo end) drops the pick - not the garage, which
	//    is a menu load as well.
	if ((gGT->gameMode1 & MAIN_MENU) != 0)
	{
		if ((gGT->levelID == MAIN_MENU_LEVEL) && (sdata->mainMenuState == MAIN_MENU_TITLE))
		{
			s_charPick = -1;
		}

		return;
	}

	// 2. No file in the roster: retail, silently. Placeholders alone never
	//    bind, so the roster has nothing to give either.
	if (s_charRosterFiles == 0)
	{
		return;
	}

	// 2b. --dev-char-seats (developer switch, measuring): every seat, the pick
	//     does not count.
	if (s_charDevSeats != NATIVE_CHAR_DEV_SEATS_OFF)
	{
		NativeChar_ArmDevSeats(gGT);
		return;
	}

	// 3. No custom pick: retail, silently.
	if ((pick < 0) || (pick >= s_charRosterCount))
	{
		return;
	}

	// 4. Demo: every seat is a bot of the attract mode or --autoload-demo.
	if (gGT->boolDemoMode != 0)
	{
		Platform_Log("[CTR Char] not bound: demo\n");
		return;
	}

	// 4b. The podium at the end of an arcade cup: nothing to bind - the podium
	//     shows the dance models of the templates (game/Podium.c) - and no gap
	//     in the mode rule, so not loud, and the pick stays. The way on from the
	//     podium loads the title (game/233/CS_Camera.c), and step 1 drops the
	//     pick there as on every way back to the title.
	if (NativeChar_ArcadeCupPodium(gGT))
	{
		Platform_Log("[CTR Char] seat 0 empty: podium of an arcade cup, the pick stays\n");
		return;
	}

	// 5. The mode. The driver select and the main menu keep the pick at -1
	//    outside the modes it may be made in, so a pick here is a gap in that
	//    rule: loud, and the pick is dropped.
	modeWhy = NativeChar_ModeRefusal(gGT);
	if (modeWhy != NULL)
	{
		Platform_LogWarn("[CTR Char] not bound: mode %s (gameMode1 0x%08x, %d player(s)) - custom pick in a forbidden mode, must not happen - pick dropped\n",
		                 modeWhy, (unsigned)gGT->gameMode1, (int)gGT->numPlyrCurrGame);
		s_charPick = -1;
		return;
	}

	// 6. The pick names a file, and the seat runs on its template. Both, because
	//    the pick can outlive the choice it came from (CHANGE LEVEL, the next
	//    race of a cup), and characterIDs[0] is what the engine drives: the
	//    driver select writes the template there, --driver does on a direct start.
	//    A file is in the roster only with its model, so no model means a
	//    placeholder - which the driver select does not let anyone choose.
	model = NativeChar_EntryModel(pick);
	if (model == NULL)
	{
		Platform_Log("[CTR Char] seat 0 empty: entry %d is a placeholder\n", pick);
		return;
	}

	templateId = NativeChar_EntryTemplate(pick);
	if ((int)data.characterIDs[0] != templateId)
	{
		Platform_Log("[CTR Char] seat 0 empty: template %d is not %d\n", (int)data.characterIDs[0], templateId);
		return;
	}

	// 7. The frame counts of the donor: the game logic counts frames from the
	//    model it draws, and the penalties hang on two of them.
	donor = NativeChar_DonorModel(templateId);
	for (a = 0; a < RLDCHAR_ANIM_COUNT; a++)
	{
		const u32 ours = NativeChar_AnimFrames(model, a);
		const u32 theirs = NativeChar_AnimFrames(donor, a);

		if (ours != theirs)
		{
			Platform_Log("[CTR Char] not bound: frames %d %u/%u\n", a, (unsigned)ours, (unsigned)theirs);
			return;
		}
	}

	// 8. Bound.
	s_seat[0].model = model;
	s_seat[0].entry = pick;
	s_seat[0].motorId = templateId;
	s_seat[0].ownMask = s_charFiles[pick].mask;

	// The race draws the seat's portrait from the strip (NativeChar_SeatPortrait):
	// upload it anew at the first draw, whatever happened since the driver select.
	NativeChar_PortraitsDirty();
	Platform_Log("[CTR Char] seat 0 = %s on template %d\n", s_charFiles[pick].file, templateId);
	NativeChar_HoldVoices(pick);

	// The marker of the minimap: one flat color like the retail driver colors
	// (data.colors, ALL4), or the template's.
	{
		u32 color = 0;

		if (RldChar_MapColor(&s_charFiles[pick].info, &color))
		{
			s_seat[0].hasMapColor = 1;
			s_seat[0].mapColor[0] = color;
			s_seat[0].mapColor[1] = color;
			s_seat[0].mapColor[2] = color;
			s_seat[0].mapColor[3] = color;
		}
	}

	NativeChar_LogSeats();
}

struct Model *NativeChar_SeatModel(int index)
{
	if ((index < 0) || (index >= NATIVE_CHAR_SEATS) || (s_seat[index].model == NULL))
	{
		return NULL;
	}

	// The guard: only while the seat still runs on the id it was armed for.
	if ((int)data.characterIDs[index] != s_seat[index].motorId)
	{
		return NULL;
	}

	return s_seat[index].model;
}

// ---------------------------------------------------------------------------
// THE SEAT VALUES. Every function gives the retail value for a seat that is
// not bound or whose guard fails (NativeChar_SeatModel NULL).
// ---------------------------------------------------------------------------

int NativeChar_SeatEngineClass(int seat, int retailClass)
{
	// The class decides the physics (data.metaPhys) and the engine sound
	// channel alike; every reader asks here, so the two never disagree. A seat
	// of --dev-char-seats keeps the class of its template.
	if ((NativeChar_SeatModel(seat) == NULL) || s_seat[seat].devSeat)
	{
		return retailClass;
	}

	return (int)s_charFiles[s_seat[seat].entry].info.classId;
}

const u32 *NativeChar_SeatMapColor(int seat, const u32 *retail)
{
	if ((NativeChar_SeatModel(seat) == NULL) || !s_seat[seat].hasMapColor)
	{
		return retail;
	}

	return s_seat[seat].mapColor;
}

int NativeChar_SeatSilent(int seat)
{
	// A bound custom seat never speaks with the template's voice - with its
	// own clips (NativeChar_SeatVoiced) or not at all.
	return NativeChar_SeatModel(seat) != NULL;
}

int NativeChar_SeatVoiced(int seat)
{
	return (NativeChar_SeatModel(seat) != NULL) && (s_charFiles[s_seat[seat].entry].voiceState == NATIVE_CHAR_VOICES_OWN) &&
	       (s_charFiles[s_seat[seat].entry].cvoi != NULL);
}

// Clip pick % count of the event, started on the mixer (a line at volume on the
// CD channel, a short sound at volumeLeft / volumeRight beside it); its length
// in the unit of CDSYS_XAGetTrackLength (whole sectors at
// NATIVE_CHAR_VOICE_SECTORS_PER_SECOND), or 0 when nothing plays: an event
// without clips (one line), or no audio output (as an XA line that does not
// start, no line).
internal int NativeChar_SeatSpeak(int seat, int event, u32 pick, int volumeLeft, int volumeRight)
{
	const struct NativeCharFile *f;
	const struct RldCharVoiceClip *clip;
	u32 count;
	u32 index;
	int played;

	if (!NativeChar_SeatVoiced(seat) || (event < 0) || (event >= RLDCHAR_VOICE_EVENTS))
	{
		return 0;
	}

	f = &s_charFiles[s_seat[seat].entry];
	count = f->voices.count[event];
	if (count == 0)
	{
		Platform_Log("[CTR Voice] seat %d event %s: no clip - silent\n", seat, s_rldCharVoiceEvents[event]);
		return 0;
	}

	index = pick % count;
	clip = &f->voices.clip[f->voices.first[event] + index];
	if (event < RLDCHAR_VOICE_SHORT_FIRST)
	{
		played = NativeAudio_PlayPcmLine(&f->cvoi[clip->offset], (int)clip->frames, (int)RLDCHAR_VOICE_RATE, (int)clip->volume, volumeLeft);
	}
	else
	{
		played = NativeAudio_PlayPcmShort(&f->cvoi[clip->offset], (int)clip->frames, (int)RLDCHAR_VOICE_RATE, (int)clip->volume, volumeLeft,
		                                  volumeRight);
	}

	if (!played)
	{
		return 0;
	}

	Platform_Log("[CTR Voice] seat %d event %s clip %s (%u of %u)\n", seat, s_rldCharVoiceEvents[event], clip->name, (unsigned)(index + 1u),
	             (unsigned)count);
	return (int)(((clip->frames * NATIVE_CHAR_VOICE_SECTORS_PER_SECOND) + (RLDCHAR_VOICE_RATE - 1u)) / RLDCHAR_VOICE_RATE);
}

int NativeChar_SeatSpeakLine(int seat, u32 voiceSet, u32 pick, int volume)
{
	// The retail sets 0..7 are the events 0..7; set 8 (voice ids 21..23) has
	// no event and no caller.
	if (voiceSet >= (u32)RLDCHAR_VOICE_SHORT_FIRST)
	{
		return 0;
	}

	return NativeChar_SeatSpeak(seat, (int)voiceSet, pick, volume, volume);
}

int NativeChar_SeatSpeakShort(int seat, u32 voiceType, u32 pick, int volumeLeft, int volumeRight)
{
	// The short sound of set 0 (OtherFX char + 0x1c) and of set 1 (char + 0x2c).
	if (voiceType > 1u)
	{
		return 0;
	}

	return NativeChar_SeatSpeak(seat, RLDCHAR_VOICE_SHORT_FIRST + (int)voiceType, pick, volumeLeft, volumeRight);
}

// Whether the level holds the mask (1 Aku Aku, 0 Uka Uka): its model and its
// beam, the two VehPickupItem_MaskUseWeapon births. INSTANCE_BirthWithThread
// gives NULL for a missing model, INSTANCE_Birth3D does not check at all.
internal int NativeChar_MaskLoaded(int good)
{
	const struct GameTracker *gGT = sdata->gGT;

	if (gGT == NULL)
	{
		return 0;
	}

	return (gGT->modelPtr[good ? STATIC_AKUAKU : STATIC_UKAUKA] != NULL) && (gGT->modelPtr[good ? STATIC_AKUBEAM : STATIC_UKABEAM] != NULL);
}

// The mask decision itself, pure: the CHRI flags of a bound seat, the
// caller's retail value and whether each mask is loaded. 1 Aku Aku, 0 Uka
// Uka. *missing = 1 when the file chooses a mask the level does not hold -
// then the template's, retailGood, as for "like the template".
internal int NativeChar_MaskGoodOf(u32 flags, int retailGood, int akuLoaded, int ukaLoaded, int *missing)
{
	const u32 mask = RldChar_Mask(flags);
	int want;

	*missing = 0;

	if (mask == RLDCHAR_MASK_LIKE)
	{
		return retailGood;
	}

	want = (mask == RLDCHAR_MASK_AKU) ? 1 : 0;
	if ((want ? akuLoaded : ukaLoaded) == 0)
	{
		*missing = 1;
		return retailGood;
	}

	return want;
}

int NativeChar_SeatMaskGood(int seat, int retailGood)
{
	u32 flags;
	int missing;
	int good;

	if (NativeChar_SeatModel(seat) == NULL)
	{
		return retailGood;
	}

	flags = s_charFiles[s_seat[seat].entry].info.flags;
	good = NativeChar_MaskGoodOf(flags, retailGood, NativeChar_MaskLoaded(1), NativeChar_MaskLoaded(0), &missing);

	// The HUD asks in every frame with the mask item: one line per load.
	if ((missing != 0) && (s_seat[seat].maskMissingNoted == 0))
	{
		s_seat[seat].maskMissingNoted = 1;
		Platform_Log("[CTR Char] mask seat %d: %s wanted, not loaded - the template's mask stays\n", seat,
		             (RldChar_Mask(flags) == RLDCHAR_MASK_AKU) ? "aku" : "uka");
	}

	return good;
}

void NativeChar_NoteMask(const struct Driver *d, int modelID)
{
	const struct GameTracker *gGT = sdata->gGT;
	const char *song = "none";
	u32 mask;
	int seat;
	int good;
	int missing;

	if ((d == NULL) || (gGT == NULL))
	{
		return;
	}

	seat = (int)d->driverID;
	if ((NativeChar_SeatModel(seat) == NULL) || (s_seat[seat].maskNoted != 0))
	{
		return;
	}
	s_seat[seat].maskNoted = 1;

	// The mask VehPickupItem_MaskUseWeapon has just created, and where the
	// choice came from: the file, or the template (like the template, or the file's
	// mask is not loaded).
	good = (modelID == STATIC_AKUAKU) ? 1 : 0;
	mask = RldChar_Mask(s_charFiles[s_seat[seat].entry].info.flags);
	(void)NativeChar_MaskGoodOf(s_charFiles[s_seat[seat].entry].info.flags, good, NativeChar_MaskLoaded(1), NativeChar_MaskLoaded(0), &missing);

	// The song is only set for a human outside KS_ENGINE_REVVING and
	// KS_MASK_GRABBED (VehPickupItem_MaskUseWeapon).
	if (((d->actionsFlagSet & ACTION_BOT) == 0) && (d->kartState != KS_ENGINE_REVVING) && (d->kartState != KS_MASK_GRABBED))
	{
		if ((gGT->gameMode1 & AKU_SONG) != 0)
		{
			song = "aku";
		}
		else if ((gGT->gameMode1 & UKA_SONG) != 0)
		{
			song = "uka";
		}
	}

	Platform_Log("[CTR Char] mask seat %d: %s from the %s%s (model 0x%02x, beam 0x%02x, sound 0x%02x, song %s)\n", seat, good ? "aku" : "uka",
	             ((mask != RLDCHAR_MASK_LIKE) && (missing == 0)) ? "file" : "template",
	             (missing != 0) ? ((mask == RLDCHAR_MASK_AKU) ? " - aku not loaded" : " - uka not loaded") : "", (unsigned)modelID,
	             (unsigned)(good ? STATIC_AKUBEAM : STATIC_UKABEAM), (unsigned)(modelID + NATIVE_CHAR_MASK_SOUND_OFFSET), song);
}

struct Model *NativeChar_SeatMaskModel(int seat)
{
	// The guard of NativeChar_SeatModel; a file without a usable CMSK has none.
	if (NativeChar_SeatModel(seat) == NULL)
	{
		return NULL;
	}

	return s_seat[seat].ownMask;
}

void NativeChar_NoteOwnMask(const struct Driver *d)
{
	int seat;

	if (d == NULL)
	{
		return;
	}

	seat = (int)d->driverID;
	if ((NativeChar_SeatMaskModel(seat) == NULL) || (s_seat[seat].ownMaskNoted != 0))
	{
		return;
	}
	s_seat[seat].ownMaskNoted = 1;

	Platform_Log("[CTR Char] mask seat %d: own model from the file (%u triangles)\n", seat, (unsigned)s_charFiles[s_seat[seat].entry].maskTriangles);
}

void NativeChar_NoteDriveValues(const struct Driver *d, int seat)
{
	int classId;
	int templateId;

	if ((d == NULL) || (NativeChar_SeatModel(seat) == NULL))
	{
		return;
	}

	templateId = s_seat[seat].motorId;
	classId = NativeChar_SeatEngineClass(seat, data.MetaDataCharacters[templateId].engineID);

	// The values VehBirth_SetConsts has just written from data.metaPhys, and
	// the engine sound index of VehBirth_EngineAudio_AllPlayers (class * 4 +
	// driverID).
	Platform_Log("[CTR Char] drive values seat %d: class %d (template %d is class %d), speed %d accelSpeed %d accel %d turn %d turnDecrease %d "
	             "inputDelay %d preTurbo %d driftBase %d weight %d, engine audio %d\n",
	             seat, classId, templateId, data.MetaDataCharacters[templateId].engineID, (int)d->const_Speed_ClassStat, (int)d->const_AccelSpeed_ClassStat,
	             (int)d->const_Accel_ClassStat, (int)d->const_TurnRate, (int)d->const_TurnDecreaseRate, (int)d->const_TurnInputDelay, (int)d->const_PreTurbo,
	             (int)d->const_DriftTurnBase, (int)d->const_CollisionWeight, (classId * 4) + (int)d->driverID);
}

int NativeChar_Active(void)
{
	int seat;

	// A seat counts while it is armed, guard or not: the question is whether
	// host pointers are in play.
	for (seat = 0; seat < NATIVE_CHAR_SEATS; seat++)
	{
		if (s_seat[seat].model != NULL)
		{
			return 1;
		}
	}

	// The pick counts too: a restore puts characterIDs[0] back, not the pick,
	// and on the same template the pick would then bind to a restored retail
	// state.
	if (s_charPick >= 0)
	{
		return 1;
	}

	return MM_NativeCharGrid_PreviewCustom();
}

void NativeChar_NoteDroppedInstance(const struct Instance *inst)
{
	const struct GameTracker *gGT = sdata->gGT;

	s_droppedTotal++;

	if ((inst != NULL) && (gGT != NULL) && (gGT->drivers[0] != NULL) && (gGT->drivers[0]->instSelf == inst))
	{
		s_droppedSeat0++;
	}

	// --dev-char-seats: the same per load, and which drops were bound seats.
	if (s_charDevLoadOpen)
	{
		int seat;

		s_charDevDroppedTotal++;

		for (seat = 0; (inst != NULL) && (gGT != NULL) && (seat < NATIVE_CHAR_SEATS); seat++)
		{
			if ((gGT->drivers[seat] != NULL) && (gGT->drivers[seat]->instSelf == inst))
			{
				s_charDevDroppedSeat0 += (seat == 0) ? 1 : 0;
				s_charDevDroppedBound += (s_seat[seat].model != NULL) ? 1 : 0;
				break;
			}
		}
	}
}

void NativeChar_NoteVBlank(int vblank)
{
	const struct GameTracker *gGT = sdata->gGT;
	const struct Instance *inst;

	// Only while seat 0 is bound and still runs on its template (the guard of
	// NativeChar_SeatModel): a refused file, an unbound race and a race on a
	// retail tile stay as quiet as a retail run.
	if ((NativeChar_SeatModel(0) == NULL) || (gGT == NULL))
	{
		return;
	}

	// In a race only: not in a menu, not while a level loads.
	if ((((u32)gGT->gameMode1) & (MAIN_MENU | LOADING)) != 0)
	{
		return;
	}

	if ((gGT->drivers[0] == NULL) || (gGT->drivers[0]->instSelf == NULL))
	{
		return;
	}

	// animIndex and animFrame of the instance (include/namespace_Instance.h:590,
	// :594), the ones the renderer picks the frame by.
	inst = gGT->drivers[0]->instSelf;
	Platform_Log("[CTR Char] vblank %d: seat 0 anim %d frame %d\n", vblank, (int)inst->animIndex, (int)inst->animFrame);
}

// ---------------------------------------------------------------------------
// THE MASK SELF-TEST, part of --char-grid-selftest (MM_NativeCharGrid_SelfTest).
// Pure like the rest of that test: CHRI bytes built here, read back with
// RldChar_ParseInfo, and the mask decision of a bound seat with each mask
// loaded or not. No roster, no seat is armed, no sdata is read.
// ---------------------------------------------------------------------------

// A CHRI chunk on template 14: fixedSize 0x1C (a file from before the flags)
// when hasFlags is 0, else 0x20 with flags; no strings. Returns its size.
internal size_t NativeChar_TestChri(u8 *bytes, int hasFlags, u32 flags)
{
	const u32 fixedSize = hasFlags ? RLDCHAR_CHRI_SIZE_FLAGS : RLDCHAR_INFO_FIXED_SIZE;

	memset(bytes, 0, RLDCHAR_CHRI_SIZE_FLAGS + 4u);
	bytes[0x00] = (u8)fixedSize;
	bytes[0x02] = RLDCHAR_TEMPLATE_MAX;
	memcpy(&bytes[0x04], "MASK", 4);
	bytes[0x18] = 1;
	if (hasFlags)
	{
		bytes[RLDCHAR_CHRI_FLAGS_OFFSET + 0] = (u8)(flags & 0xffu);
		bytes[RLDCHAR_CHRI_FLAGS_OFFSET + 1] = (u8)((flags >> 8) & 0xffu);
		bytes[RLDCHAR_CHRI_FLAGS_OFFSET + 2] = (u8)((flags >> 16) & 0xffu);
		bytes[RLDCHAR_CHRI_FLAGS_OFFSET + 3] = (u8)((flags >> 24) & 0xffu);
	}

	return (size_t)fixedSize + 4u;
}

internal void NativeChar_MaskExpect(int *checks, int *failures, int ok, const char *what)
{
	(*checks)++;

	if (!ok)
	{
		(*failures)++;
		printf("char mask selftest FAILED: %s\n", what);
	}
}

// A CMSK with a rigid mask model of 'triangles' triangles in one strip of
// vertex-color commands, built the way the rules of include/rldchar.inc read
// it: Model 0x00, header 0x18, animation table 0x58, ModelAnim 0x5C with its
// one frame at 0x74, then the command list and one color. 'damage' breaks one
// thing on purpose (NATIVE_CHAR_TEST_MASK_*). Returns the size, 0 when buf
// (bufSize bytes) is too small.
enum
{
	NATIVE_CHAR_TEST_MASK_GOOD = 0,
	NATIVE_CHAR_TEST_MASK_VERSION,   // version 2
	NATIVE_CHAR_TEST_MASK_FLAGS,     // flags 1
	NATIVE_CHAR_TEST_MASK_FRAMES,    // 2 frames
	NATIVE_CHAR_TEST_MASK_ANIMS,     // numAnimations 2
	NATIVE_CHAR_TEST_MASK_MAP,       // a map entry on the model name
	NATIVE_CHAR_TEST_MASK_TEXTURE,   // a command with texture 1
};

internal void NativeChar_TestPut32(u8 *at, u32 value)
{
	at[0] = (u8)(value & 0xffu);
	at[1] = (u8)((value >> 8) & 0xffu);
	at[2] = (u8)((value >> 16) & 0xffu);
	at[3] = (u8)((value >> 24) & 0xffu);
}

internal size_t NativeChar_TestMask(u8 *buf, size_t bufSize, u32 triangles, int damage)
{
	const u32 records = triangles + 2u;
	const u32 frameSize = (RLDCHAR_FRAME_BYTES + (records * RLDCHAR_RECORD_BYTES) + 3u) & ~3u;
	const u32 header = 0x18u;
	const u32 animTable = 0x58u;
	const u32 anim = 0x5cu;
	const u32 frame = anim + RLDCHAR_ANIM_BYTES;
	const u32 commands = frame + frameSize;
	const u32 colors = commands + 4u + (records * 4u) + 4u;
	const u32 bodyBytes = colors + 4u;
	const u32 mapCount = 5u;
	const size_t size = RLDCHAR_MASK_HEAD_BYTES + 4u + bodyBytes + 4u + (mapCount * 4u);
	u8 *body;
	u8 *map;
	u32 i;

	if (size > bufSize)
	{
		return 0;
	}
	memset(buf, 0, size);

	buf[0x00] = (u8)((damage == NATIVE_CHAR_TEST_MASK_VERSION) ? 2u : RLDCHAR_MASK_VERSION);
	buf[0x02] = (u8)((damage == NATIVE_CHAR_TEST_MASK_FLAGS) ? 1u : 0u);
	NativeChar_TestPut32(&buf[RLDCHAR_MASK_HEAD_BYTES], bodyBytes);
	body = &buf[RLDCHAR_MASK_HEAD_BYTES + 4u];
	map = &body[bodyBytes + 4u];
	NativeChar_TestPut32(&body[bodyBytes], mapCount * 4u);

	// Model: name, id -1, one header.
	memcpy(body, "ownmask", 7);
	body[RLDCHAR_MODEL_ID] = 0xff;
	body[RLDCHAR_MODEL_ID + 1u] = 0xff;
	body[RLDCHAR_MODEL_NUM_HEADERS] = 1;
	NativeChar_TestPut32(&body[RLDCHAR_MODEL_HEADERS], header);

	// The header: LOD 20000 like akumouth, the scale 0x1000.
	body[header + RLDCHAR_HEADER_LOD] = (u8)(20000u & 0xffu);
	body[header + RLDCHAR_HEADER_LOD + 1u] = (u8)(20000u >> 8);
	for (i = 0; i < 3u; i++)
	{
		body[header + RLDCHAR_HEADER_SCALE + (i * 2u) + 1u] = 0x10;
	}
	NativeChar_TestPut32(&body[header + RLDCHAR_HEADER_COMMANDS], commands);
	NativeChar_TestPut32(&body[header + RLDCHAR_HEADER_COLORS], colors);
	NativeChar_TestPut32(&body[header + RLDCHAR_HEADER_NUM_ANIMS], (damage == NATIVE_CHAR_TEST_MASK_ANIMS) ? 2u : 1u);
	NativeChar_TestPut32(&body[header + RLDCHAR_HEADER_ANIMS], animTable);
	NativeChar_TestPut32(&body[animTable], anim);

	// The animation: 1 raw frame (2 with the damage, which still lie in the
	// body - the command list follows), records right behind the frame head.
	body[anim + RLDCHAR_ANIM_NUM_FRAMES] = (u8)((damage == NATIVE_CHAR_TEST_MASK_FRAMES) ? 2u : 1u);
	body[anim + RLDCHAR_ANIM_FRAME_SIZE] = (u8)(frameSize & 0xffu);
	body[anim + RLDCHAR_ANIM_FRAME_SIZE + 1u] = (u8)(frameSize >> 8);
	NativeChar_TestPut32(&body[frame + RLDCHAR_FRAME_VERTEX_OFF], RLDCHAR_FRAME_BYTES);

	// One color, one strip: every command a new record in slot 1..80, color 0,
	// texture 0 (G3); the first one starts the strip.
	NativeChar_TestPut32(&body[commands], 1u);
	for (i = 0; i < records; i++)
	{
		u32 command = ((i % 80u) + 1u) << 16;

		if (i == 0u)
		{
			command |= RLDCHAR_CMD_NEW_STRIP;
		}
		if ((damage == NATIVE_CHAR_TEST_MASK_TEXTURE) && (i == 2u))
		{
			command |= 1u;
		}
		NativeChar_TestPut32(&body[commands + 4u + (i * 4u)], command);
	}
	NativeChar_TestPut32(&body[commands + 4u + (records * 4u)], RLDCHAR_CMD_END);
	NativeChar_TestPut32(&body[colors], 0x00808080u);

	// The pointer map: the five pointer fields that are set.
	NativeChar_TestPut32(&map[0], (damage == NATIVE_CHAR_TEST_MASK_MAP) ? 0x04u : RLDCHAR_MODEL_HEADERS);
	NativeChar_TestPut32(&map[4], header + RLDCHAR_HEADER_COMMANDS);
	NativeChar_TestPut32(&map[8], header + RLDCHAR_HEADER_COLORS);
	NativeChar_TestPut32(&map[12], header + RLDCHAR_HEADER_ANIMS);
	NativeChar_TestPut32(&map[16], animTable);

	return size;
}

// One CMSK through the game's check: usable or not, and the rule that said no.
internal void NativeChar_TestMaskCase(int *checks, int *failures, const char *name, const u8 *bytes, size_t size, int usable, const char *rule,
                                      u32 triangles)
{
	struct NativeCharFile f;
	char what[256];
	int got;

	memset(&f, 0, sizeof(f));
	got = NativeChar_CheckMaskBytes(&f, bytes, size);

	snprintf(what, sizeof(what), "own mask %s: usable %d, not %d (%s)", name, got, usable, f.maskWhy);
	NativeChar_MaskExpect(checks, failures, got == usable, what);

	if (usable)
	{
		snprintf(what, sizeof(what), "own mask %s: %u triangles, not %u", name, (unsigned)f.maskTriangles, (unsigned)triangles);
		NativeChar_MaskExpect(checks, failures, f.maskTriangles == triangles, what);
	}
	else
	{
		snprintf(what, sizeof(what), "own mask %s: ignored as '%s', not by %s", name, f.maskWhy, rule);
		NativeChar_MaskExpect(checks, failures, (f.maskState == NATIVE_CHAR_MASK_IGNORED) && (strstr(f.maskWhy, rule) != NULL), what);
	}
}

// The own mask (CMSK): good ones, too large ones and broken ones. A broken
// one is only "not usable" - the file around it is not part of the check, so
// it cannot be refused by it.
internal void NativeChar_OwnMaskSelfTest(int *checks, int *failures)
{
	static u8 buf[RLDCHAR_MASK_BYTES_MAX + 64u];
	struct RldCharFinding finding;
	size_t size;
	int seat;

	size = NativeChar_TestMask(buf, sizeof(buf), 56u, NATIVE_CHAR_TEST_MASK_GOOD);
	NativeChar_TestMaskCase(checks, failures, "56 triangles", buf, size, 1, NULL, 56u);

	size = NativeChar_TestMask(buf, sizeof(buf), RLDCHAR_MASK_DRAW_BYTES_MAX / RLDCHAR_DRAW_BYTES_G3, NATIVE_CHAR_TEST_MASK_GOOD);
	NativeChar_TestMaskCase(checks, failures, "the most triangles (RLDCHAR_MASK_DRAW_BYTES_MAX)", buf, size, 1, NULL,
	                        RLDCHAR_MASK_DRAW_BYTES_MAX / RLDCHAR_DRAW_BYTES_G3);

	// The same model is no character: the profiles stay apart, and the
	// character texts are the ones they were.
	(void)RldChar_CheckModel(&buf[RLDCHAR_MASK_HEAD_BYTES], size - RLDCHAR_MASK_HEAD_BYTES, &finding, NULL, NULL, NULL);
	NativeChar_MaskExpect(checks, failures,
	                      (finding.rule != NULL) && (strcmp(finding.detail, "ModelHeader.numAnimations is 1 - a character has exactly 4") == 0),
	                      "own mask as CMDL: not 'a character has exactly 4'");

	size = NativeChar_TestMask(buf, sizeof(buf), (RLDCHAR_MASK_DRAW_BYTES_MAX / RLDCHAR_DRAW_BYTES_G3) + 1u, NATIVE_CHAR_TEST_MASK_GOOD);
	NativeChar_TestMaskCase(checks, failures, "one triangle more", buf, size, 0, "model-draw", 0u);

	// CMSK-3 before any model rule: a good head, 16 KiB + 4 bytes.
	size = NativeChar_TestMask(buf, sizeof(buf), 56u, NATIVE_CHAR_TEST_MASK_GOOD);
	NativeChar_TestMaskCase(checks, failures, "over 16 KiB", buf, RLDCHAR_MASK_BYTES_MAX + 4u, 0, "CMSK-3", 0u);

	size = NativeChar_TestMask(buf, sizeof(buf), 56u, NATIVE_CHAR_TEST_MASK_VERSION);
	NativeChar_TestMaskCase(checks, failures, "version 2", buf, size, 0, "CMSK-1", 0u);

	NativeChar_TestMaskCase(checks, failures, "3 bytes", buf, 3u, 0, "CMSK-1", 0u);

	size = NativeChar_TestMask(buf, sizeof(buf), 56u, NATIVE_CHAR_TEST_MASK_FLAGS);
	NativeChar_TestMaskCase(checks, failures, "flags 1", buf, size, 0, "CMSK-2", 0u);

	size = NativeChar_TestMask(buf, sizeof(buf), 56u, NATIVE_CHAR_TEST_MASK_FRAMES);
	NativeChar_TestMaskCase(checks, failures, "2 frames", buf, size, 0, "model-anim-frames", 0u);

	size = NativeChar_TestMask(buf, sizeof(buf), 56u, NATIVE_CHAR_TEST_MASK_ANIMS);
	NativeChar_TestMaskCase(checks, failures, "2 animations", buf, size, 0, "model-anims", 0u);

	size = NativeChar_TestMask(buf, sizeof(buf), 56u, NATIVE_CHAR_TEST_MASK_MAP);
	NativeChar_TestMaskCase(checks, failures, "map on the name", buf, size, 0, "model-map", 0u);

	size = NativeChar_TestMask(buf, sizeof(buf), 56u, NATIVE_CHAR_TEST_MASK_TEXTURE);
	NativeChar_TestMaskCase(checks, failures, "texture 1", buf, size, 0, "model-tex", 0u);

	// The body cut short: the frame says more than the chunk holds.
	size = NativeChar_TestMask(buf, sizeof(buf), 56u, NATIVE_CHAR_TEST_MASK_GOOD);
	NativeChar_TestMaskCase(checks, failures, "cut short", buf, size - 8u, 0, "model-bounds", 0u);

	// No seat is armed: no own mask anywhere.
	for (seat = -1; seat <= NATIVE_CHAR_SEATS; seat++)
	{
		char what[96];

		snprintf(what, sizeof(what), "unbound seat %d: has an own mask", seat);
		NativeChar_MaskExpect(checks, failures, NativeChar_SeatMaskModel(seat) == NULL, what);
	}
}

// The map color of CHRI (RldChar_MapColor): a CHRI of fixedSize bytes on
// template 14 with flags and the 4 color bytes where they fit, read back.
internal void NativeChar_MapColorSelfTest(int *checks, int *failures)
{
	static const struct
	{
		const char *name;
		u32 fixedSize;
		u32 flags;
		u8 color[4];
		int has;
		u32 expect;
	} cases[] = {
	    {"old file 0x1C", 0x1Cu, 0u, {0, 0, 0, 0}, 0, 0u},
	    {"flags only, bit 3 without the field", 0x20u, RLDCHAR_FLAG_MAP_COLOR, {0, 0, 0, 0}, 0, 0u},
	    {"field without bit 3", 0x24u, 0u, {0x12, 0x34, 0x56, 0}, 0, 0u},
	    {"color 12 34 56", 0x24u, RLDCHAR_FLAG_MAP_COLOR, {0x12, 0x34, 0x56, 0}, 1, 0x563412u},
	    {"black is a color", 0x24u, RLDCHAR_FLAG_MAP_COLOR, {0, 0, 0, 0}, 1, 0u},
	    {"byte 3 is not read", 0x24u, RLDCHAR_FLAG_MAP_COLOR, {0x80, 0x80, 0x80, 0xff}, 1, 0x808080u},
	    {"with wheels off and uka", 0x24u, RLDCHAR_FLAG_MAP_COLOR | 0x5u, {0xff, 0, 0, 0}, 1, 0x0000ffu},
	    {"longer fixedSize 0x28", 0x28u, RLDCHAR_FLAG_MAP_COLOR, {0, 0xff, 0, 0}, 1, 0x00ff00u},
	};
	u8 bytes[0x2Cu];
	struct RldCharInfo info;
	char what[160];
	u32 color;
	int c;
	int seat;

	for (c = 0; c < (int)(sizeof(cases) / sizeof(cases[0])); c++)
	{
		const u32 fixedSize = cases[c].fixedSize;
		int has;

		memset(bytes, 0, sizeof(bytes));
		bytes[0x00] = (u8)fixedSize;
		bytes[0x02] = RLDCHAR_TEMPLATE_MAX;
		memcpy(&bytes[0x04], "MAP", 3);
		if (fixedSize >= RLDCHAR_CHRI_SIZE_FLAGS)
		{
			NativeChar_TestPut32(&bytes[RLDCHAR_CHRI_FLAGS_OFFSET], cases[c].flags);
		}
		if (fixedSize >= RLDCHAR_CHRI_SIZE_MAP_COLOR)
		{
			memcpy(&bytes[RLDCHAR_CHRI_MAP_COLOR_OFFSET], cases[c].color, 4);
		}

		snprintf(what, sizeof(what), "map color %s: CHRI refused", cases[c].name);
		NativeChar_MaskExpect(checks, failures, RldChar_ParseInfo(&info, bytes, (size_t)fixedSize + 4u) == NULL, what);

		color = 0xdeadbeefu;
		has = RldChar_MapColor(&info, &color);
		snprintf(what, sizeof(what), "map color %s: %d 0x%08x, not %d 0x%06x", cases[c].name, has, (unsigned)color, cases[c].has, (unsigned)cases[c].expect);
		NativeChar_MaskExpect(checks, failures, (has == cases[c].has) && (has ? (color == cases[c].expect) : (color == 0xdeadbeefu)), what);

		snprintf(what, sizeof(what), "map color %s: the mask moved", cases[c].name);
		NativeChar_MaskExpect(checks, failures, RldChar_Mask(info.flags) == RldChar_Mask(cases[c].flags), what);
	}

	// No seat is bound: every seat keeps the retail color words.
	for (seat = -1; seat <= NATIVE_CHAR_SEATS; seat++)
	{
		static const u32 retail[4] = {1, 2, 3, 4};

		snprintf(what, sizeof(what), "unbound seat %d: not the retail map color", seat);
		NativeChar_MaskExpect(checks, failures, NativeChar_SeatMapColor(seat, retail) == retail, what);
	}
}

void NativeChar_MaskSelfTest(int *checks, int *failures)
{
	// One case per file: hasFlags, flags, the mask RldChar_Mask must give, and
	// what a bound seat wears with both masks loaded for a retail value of 0
	// (Uka Uka) and of 1 (Aku Aku).
	static const struct
	{
		const char *name;
		int hasFlags;
		u32 flags;
		u32 mask;
		int good0;
		int good1;
	} cases[] = {
	    {"old file without flags", 0, 0u, RLDCHAR_MASK_LIKE, 0, 1},
	    {"flags 0 (like the template)", 1, 0u, RLDCHAR_MASK_LIKE, 0, 1},
	    {"aku", 1, 0x2u, RLDCHAR_MASK_AKU, 1, 1},
	    {"uka", 1, 0x4u, RLDCHAR_MASK_UKA, 0, 0},
	    {"reserved 3", 1, 0x6u, RLDCHAR_MASK_LIKE, 0, 1},
	    {"wheels off and uka", 1, 0x5u, RLDCHAR_MASK_UKA, 0, 0},
	    {"wheels off and aku", 1, 0x3u, RLDCHAR_MASK_AKU, 1, 1},
	    {"map color bit 3 without the field and aku", 1, 0xau, RLDCHAR_MASK_AKU, 1, 1},
	    {"full height bit 4 and aku", 1, 0x12u, RLDCHAR_MASK_AKU, 1, 1},
	    {"unknown bit 5 and uka", 1, 0x24u, RLDCHAR_MASK_UKA, 0, 0},
	};
	u8 bytes[RLDCHAR_CHRI_SIZE_FLAGS + 4u];
	struct RldCharInfo info;
	char what[160];
	size_t size;
	int missing;
	int c;
	int t;

	for (c = 0; c < (int)(sizeof(cases) / sizeof(cases[0])); c++)
	{
		size = NativeChar_TestChri(bytes, cases[c].hasFlags, cases[c].flags);

		snprintf(what, sizeof(what), "%s: CHRI refused", cases[c].name);
		NativeChar_MaskExpect(checks, failures, RldChar_ParseInfo(&info, bytes, size) == NULL, what);

		snprintf(what, sizeof(what), "%s: flags 0x%x read back", cases[c].name, (unsigned)info.flags);
		NativeChar_MaskExpect(checks, failures, info.flags == cases[c].flags, what);

		snprintf(what, sizeof(what), "%s: mask %u, not %u", cases[c].name, (unsigned)RldChar_Mask(info.flags), (unsigned)cases[c].mask);
		NativeChar_MaskExpect(checks, failures, RldChar_Mask(info.flags) == cases[c].mask, what);

		snprintf(what, sizeof(what), "%s: wheels bit lost", cases[c].name);
		NativeChar_MaskExpect(checks, failures, (info.flags & RLDCHAR_FLAG_NO_WHEELS) == (cases[c].flags & RLDCHAR_FLAG_NO_WHEELS), what);

		snprintf(what, sizeof(what), "%s: full height bit lost", cases[c].name);
		NativeChar_MaskExpect(checks, failures, (info.flags & RLDCHAR_FLAG_FULL_HEIGHT) == (cases[c].flags & RLDCHAR_FLAG_FULL_HEIGHT), what);

		snprintf(what, sizeof(what), "%s: wears %d on a template with retail 0", cases[c].name, NativeChar_MaskGoodOf(info.flags, 0, 1, 1, &missing));
		NativeChar_MaskExpect(checks, failures, (NativeChar_MaskGoodOf(info.flags, 0, 1, 1, &missing) == cases[c].good0) && (missing == 0), what);

		snprintf(what, sizeof(what), "%s: wears %d on a template with retail 1", cases[c].name, NativeChar_MaskGoodOf(info.flags, 1, 1, 1, &missing));
		NativeChar_MaskExpect(checks, failures, (NativeChar_MaskGoodOf(info.flags, 1, 1, 1, &missing) == cases[c].good1) && (missing == 0), what);
	}

	// The chosen mask not loaded: the template's, and missing says so. The
	// other mask missing changes nothing.
	NativeChar_MaskExpect(checks, failures, (NativeChar_MaskGoodOf(0x2u, 0, 0, 1, &missing) == 0) && (missing == 1), "aku not loaded: not the template's");
	NativeChar_MaskExpect(checks, failures, (NativeChar_MaskGoodOf(0x4u, 1, 1, 0, &missing) == 1) && (missing == 1), "uka not loaded: not the template's");
	NativeChar_MaskExpect(checks, failures, (NativeChar_MaskGoodOf(0x2u, 0, 1, 0, &missing) == 1) && (missing == 0), "aku with uka not loaded: not aku");
	NativeChar_MaskExpect(checks, failures, (NativeChar_MaskGoodOf(0x0u, 1, 0, 0, &missing) == 1) && (missing == 0),
	                      "like the template with nothing loaded: not the retail value");

	// No seat is armed: every seat, also outside 0..7, gives the retail value.
	for (t = -1; t <= NATIVE_CHAR_SEATS; t++)
	{
		snprintf(what, sizeof(what), "unbound seat %d: not the retail value", t);
		NativeChar_MaskExpect(checks, failures, (NativeChar_SeatMaskGood(t, 0) == 0) && (NativeChar_SeatMaskGood(t, 1) == 1), what);
	}

	// Only a model of the roster has full height: never NULL, never a model
	// that no file holds (bytes stands in for one, it is no roster model).
	NativeChar_MaskExpect(checks, failures, NativeChar_ModelFullHeight(NULL) == 0, "full height for NULL");
	NativeChar_MaskExpect(checks, failures, NativeChar_ModelFullHeight((const struct Model *)(const void *)bytes) == 0, "full height for a model of no file");

	// The templates that wear Aku Aku: Crash, Coco, Polar, Pura, Penta; 15 is
	// never a template.
	for (t = 0; t <= RLDCHAR_TEMPLATE_MAX + 1; t++)
	{
		const int aku = (t == 0) || (t == 3) || (t == 6) || (t == 7) || (t == 13);

		snprintf(what, sizeof(what), "RLDCHAR_TEMPLATE_WEARS_AKU(%d) is not %d", t, aku);
		NativeChar_MaskExpect(checks, failures, (RLDCHAR_TEMPLATE_WEARS_AKU(t) ? 1 : 0) == aku, what);
	}

	NativeChar_OwnMaskSelfTest(checks, failures);
	NativeChar_MapColorSelfTest(checks, failures);
}
