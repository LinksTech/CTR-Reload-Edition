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
// no folder; --dev-grid-fill <n> adds placeholders.
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
//            comes from its CHRI (NativeChar_SeatEngineClass), its voice stays
//            silent (NativeChar_SeatSilent).
//   draw     NativeChar_ModelHidesWheels (game/DrawTires.c): a file whose
//            CHRI flags set RLDCHAR_FLAG_NO_WHEELS is drawn without the kart
//            wheels and their reflection, wherever its model is drawn.
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
// refused while NativeChar_Active() says a custom character is in play.
// ===========================================================================

#include <platform/native_chars.h>
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

// One loaded file: its name on disk (the identity, owned here for the whole
// run), the CMDL chunk (relocated, never freed - instances point into it),
// what CHRI said and the portrait of CICN as 16-bit words (word 0x04 the CLUT,
// word 0x14 the texels, include/rldchar.inc).
struct NativeCharFile
{
	char *file;
	u8 *cmdl;
	struct Model *model;
	struct RldCharInfo info;
	u8 cmdlHash[6];
	u64 fileBytes;
	int icon;
	const char *iconWhy;
	u16 iconWords[NATIVE_CHAR_ICON_WORDS];
};

// The files of the roster, in sorted order: entry e < s_charRosterFiles is
// s_charFiles[e], with runtime id NATIVE_CHAR_RUNTIME_ID_FIRST + e.
global_variable struct NativeCharFile s_charFiles[NATIVE_CHAR_ROSTER_MAX];

// The overlay per seat. Only seat 0 is ever armed so far. entry is the roster
// entry it was armed with, motorId the character id the engine runs the seat
// on - the template.
global_variable struct
{
	struct Model *model;
	int entry;
	int motorId;
} s_seat[NATIVE_CHAR_SEATS];

// --dev-grid-fill, as main.c passed it on (0 = none).
global_variable int s_charGridFill;

// The roster, built once by NativeChar_LoadRoster: s_charRosterFiles entries
// are files, the rest up to s_charRosterCount are placeholders. Placeholder n
// (1-based) is entry s_charRosterFiles + n - 1.
global_variable int s_charRosterCount;
global_variable int s_charRosterFiles;

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

internal void NativeChar_ReportAtExit(void)
{
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
		NativeChar_Refuse(file, "DAMAGED", (rule[0] != '\0') ? rule : "CHRI", "%s", (colon != NULL) ? (colon + 2) : why);
		return 0;
	}

	// BEFORE the relocation: the pointer fields still hold body offsets, which
	// is what the check reads. The game stops at the first finding. Only an OK
	// reaches the relocation - with --ptr-map-unchecked LOAD_RunPtrMap checks
	// nothing itself, so this is the one check that always runs.
	verdict = RldChar_CheckModel(cmdl, cmdlSize, &finding, NULL, NULL, NULL);
	if (verdict != RLDCHAR_VERDICT_OK)
	{
		free(cmdl);
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
			NativeChar_Refuse(file, "PTRMAP", "ptrmap", "%s", "LOAD_RunPtrMap refused the pointer map - nothing patched");
			return 0;
		}

		out->cmdl = cmdl;
		out->model = (struct Model *)body;
		out->info = info;
	}

	return 1;
}

internal void NativeChar_LogLoaded(const struct NativeCharFile *entry)
{
	// The CHRI flags only add to the line when a known bit is set; a file
	// without flags keeps the line as it always was.
	const char *wheels = ((entry->info.flags & RLDCHAR_FLAG_NO_WHEELS) != 0) ? ", wheels hidden" : "";

	Platform_Log("[CTR Char] loaded %s: template %u, class %u, CMDL %02x%02x%02x%02x%02x%02x, %llu bytes%s\n", entry->file, (unsigned)entry->info.templateId,
	             (unsigned)entry->info.classId, (unsigned)entry->cmdlHash[0], (unsigned)entry->cmdlHash[1], (unsigned)entry->cmdlHash[2],
	             (unsigned)entry->cmdlHash[3], (unsigned)entry->cmdlHash[4], (unsigned)entry->cmdlHash[5], (unsigned long long)entry->fileBytes, wheels);
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

// A valid file becomes the next entry while there is an id for it; after
// that it is named loudly and let go. 1 = it got an entry (and keeps file).
internal int NativeChar_Admit(struct NativeCharFile *loaded, char *file)
{
	if (s_charRosterFiles >= NATIVE_CHAR_ROSTER_MAX)
	{
		Platform_LogWarn("[CTR Char] NO ID %s: all %d custom character ids are taken - the file is valid but not in the driver select\n", file,
		                 NATIVE_CHAR_ROSTER_MAX);
		free(loaded->cmdl);
		return 0;
	}

	loaded->file = file;
	s_charFiles[s_charRosterFiles] = *loaded;
	s_charRosterFiles++;
	NativeChar_LogLoaded(loaded);
	NativeChar_LogPortrait(s_charRosterFiles - 1);
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
		if (!NativeChar_Admit(&loaded, name))
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
		NativeChar_Admit(&loaded, s_charFile);
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
}

void NativeChar_ClearSeats(void)
{
	memset(s_seat, 0, sizeof(s_seat));
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

// ---------------------------------------------------------------------------
// THE PORTRAITS (native_chars.h, NATIVE_CHAR_PORTRAIT_SLOTS). Slot e belongs to
// entry e; a slot is uploaded only for a file with a usable CICN, so a run
// without such a file writes nothing into VRAM and logs nothing here.
//
// The strip x 256..511, y 266..295 is written by nothing else: no container
// and no retail file that the game loads, and no code path but the clear at
// start and the restore of a quick state (whole VRAM). Every upload is
// bracketed with NativeRenderer_StripOwnWrites, so the strip counter of the
// renderer tells these writes from any foreign one.
//
// WHEN: at the first NativeChar_EntryPortrait after NativeChar_PortraitsDirty,
// which the grid calls on every entering of the driver select
// (MM_NativeCharGrid_Enter) - after a race, a track select, a quick state, a
// video, whatever used the VRAM in between. At most 20 slots x 2 small
// LoadImage.
//
// NOT HERE: the race HUD (game/UI/UI_Rank.c), the arcade results
// (game/222.c) and the cup standings (game/UI/UI_CupStandings.c) index the
// portraits by characterIDs and keep showing the template's portrait.
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

void NativeChar_ArmSeats(void)
{
	struct GameTracker *gGT = sdata->gGT;
	const int pick = s_charPick;
	struct Model *model;
	int templateId;
	const char *modeWhy;
	const struct Model *donor;
	int a;

	// Nothing from an earlier load survives into this one.
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
	Platform_Log("[CTR Char] seat 0 = %s on template %d\n", s_charFiles[pick].file, templateId);
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
	// channel alike; every reader asks here, so the two never disagree.
	if (NativeChar_SeatModel(seat) == NULL)
	{
		return retailClass;
	}

	return (int)s_charFiles[s_seat[seat].entry].info.classId;
}

int NativeChar_SeatSilent(int seat)
{
	// Voices are not packed yet: a bound custom seat says nothing rather than
	// speak with the template's voice.
	return NativeChar_SeatModel(seat) != NULL;
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
