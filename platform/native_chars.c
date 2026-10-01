// ===========================================================================
// CUSTOM CHARACTERS - ROSTER, PICK AND SEAT 0.
//
// A custom character is an overlay, never a new character id: the engine
// only ever sees the templates 0..14 in data.characterIDs. The driver select
// shows the roster after the 15 retail tiles (game/230/MM_NativeCharGrid.c)
// and leaves its choice here as the pick; at every load the funnel decides
// from the pick whether seat 0 is born with the custom model.
//
// THE ROSTER, fixed at start. For now it comes from developer switches only:
// entry 0 is the --char file when it loaded, then the placeholders of
// --dev-grid-fill - "PLACEHOLDER <n>" on template (n - 1) % 15, without a
// model. They fill the grid so that rows and scrolling can be seen, and they
// cannot be chosen. At most NATIVE_CHAR_ROSTER_MAX entries in all. A scan of
// a characters folder will take the place of the switches; nothing that reads
// the roster has to change for it.
//
// THE PICK. An entry index, or -1 for retail. The driver select writes it in
// every frame it runs: -1 on a retail tile, while the grid is off and on the
// way back to the title. The direct start (DebugMenu_JumpToLevel: --level,
// --autoload-track, the LEVEL and TRACK pages of the debug menu) sets entry 0
// when it is a loaded file, so --level with --driver <template> --char <file>
// binds as it always did.
// NativeChar_ModeAllowed is the menu side of the mode rule; the funnel keeps
// its own checks of the race it loads.
//
// THE PATH OF THE FILE.
//   start    NativeChar_LoadDev, once, before CTR_Main: envelope (Rld_OpenAs
//            with s_rldCharFormat), CHRI, CMDL with its hash, RldChar_CheckModel
//            on the bytes as stored, then ONE LOAD_RunPtrMap on the host copy.
//            The model lives in host memory for the whole run - not in the
//            MEMPACK, so the memory layout of the game stays as it is. Then
//            the roster is built.
//   stage 5  NativeChar_ArmSeats (game/LOAD/LOAD_TenStages.c): the funnel
//            decides per load whether seat 0 is bound - only when the pick is
//            a file and characterIDs[0] holds its template. Its order is fixed,
//            and the first answer that is not "yes" ends it.
//   birth    NativeChar_SeatModel (game/Vehicle/VehBirth.c): the bound model,
//            or NULL and the retail lookup by name.
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
// RETAIL STAYS. Without --char and --dev-grid-fill: no file is opened, the
// roster is empty (the grid stays off), the pick stays -1, the funnel returns
// at its first step, the seat model is NULL and VehBirth takes the retail
// expression. The instance counter counts in every run and changes nothing;
// its exit line is there with --dev only. The log lines are read by a
// measuring tool and keep their wording.
//
// KNOWN GAP: a quick state holds host pointers and a model outside the
// MEMPACK. Runs with a custom character use no quick states.
// ===========================================================================

#include <platform/native_chars.h>
#include <platform/native_path.h>

// The envelope reader, then the character format on top of it (the same two
// files the packer compiles, tools/rldpack.c).
#include <rldtrack.inc>
#include <rldchar.inc>

#define NATIVE_CHAR_PATH_MAX 1024
#define NATIVE_CHAR_SEATS 8

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

// The loaded file: the CMDL chunk (relocated, never freed - instances point
// into it) and what CHRI said.
global_variable struct
{
	u8 *cmdl;
	struct Model *model;
	struct RldCharInfo info;
} s_char;

// The overlay per seat. Only seat 0 is ever armed so far. motorId is the
// character id the engine runs the seat on - the template.
global_variable struct
{
	struct Model *model;
	int motorId;
} s_seat[NATIVE_CHAR_SEATS];

// --dev-grid-fill, as main.c passed it on (0 = none).
global_variable int s_charGridFill;

// The roster, built once by NativeChar_LoadDev: s_charRosterFiles entries are
// files (0 or 1, the --char file), the rest up to s_charRosterCount are
// placeholders. Placeholder n (1-based) is entry s_charRosterFiles + n - 1.
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

	// A cut path could name another file: the cut copy is only for the log,
	// NativeChar_BuildPath refuses it.
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

// The file: an absolute --char as it stands, otherwise inside --chars-dir; a
// relative folder lies under the game folder, as with --tracks-dir
// (NativeTrack_Scan, platform/native_assets.c).
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

	if (s_charFolder[0] == '\0')
	{
		return 0;
	}

	if (NativeChar_IsAbsolute(s_charFolder))
	{
		snprintf(folder, sizeof(folder), "%s", s_charFolder);
	}
	else if (!NativePath_Join(folder, sizeof(folder), NativeStr8_FromCString(NativeAssets_GetBaseDir()), NativeStr8_FromCString(s_charFolder)))
	{
		return 0;
	}

	return NativePath_Join(dst, dstSize, NativeStr8_FromCString(folder), NativeStr8_FromCString(s_charFile));
}

// One line, the wording a measuring tool reads: REFUSED <file>: <WORD> (<rule>) <detail>.
internal void NativeChar_Refuse(const char *word, const char *rule, const char *format, ...)
{
	char detail[256];
	va_list args;

	va_start(args, format);
	vsnprintf(detail, sizeof(detail), format, args);
	va_end(args);
	detail[sizeof(detail) - 1] = '\0';

	Platform_Log("[CTR Char] REFUSED %s: %s (%s) %s\n", s_charFile, word, rule, detail);
}

internal void NativeChar_ReportAtExit(void)
{
	Platform_Log("[CTR Char] at exit: instances dropped %lld total, %lld seat 0\n", (long long)s_droppedTotal, (long long)s_droppedSeat0);
}

// The --char file: read, checked, relocated. Every way out but the last leaves
// s_char empty, and the file then has no entry in the roster.
internal void NativeChar_LoadFile(void)
{
	struct RldReader reader;
	struct RldCharInfo info;
	struct RldCharFinding finding;
	char path[NATIVE_CHAR_PATH_MAX];
	const char *why;
	const u8 *modelEntry;
	u8 cmdlHash[6];
	u8 *infoBytes;
	u8 *cmdl;
	size_t infoSize = 0;
	size_t cmdlSize = 0;
	int infoIndex = -1;
	int modelIndex = -1;
	u64 fileBytes;
	enum RldCharVerdict verdict;

	// Retail: without --char no file is touched.
	if (!s_charGiven)
	{
		return;
	}

	// The detail of every refusal is a fixed text: the reader's, the parser's,
	// the check's or this file's own.
	if (!NativeChar_BuildPath(path, sizeof(path)))
	{
		NativeChar_Refuse("DAMAGED", "path", "%s", "the path of the file is too long");
		return;
	}

	why = Rld_OpenAs(&reader, path, &s_rldCharFormat);
	if (why != NULL)
	{
		const char *word = (reader.refusal == RLD_REFUSAL_NEWER) ? "NEEDS NEWER" : ((reader.refusal == RLD_REFUSAL_OLDER) ? "OLD FORMAT" : "DAMAGED");

		NativeChar_Refuse(word, "envelope", "%s", why);
		return;
	}

	// Rld_OpenAs has proved both present (the required chunks of the format).
	// CICN and CPRM are not read.
	Rld_FindEntry(&reader, "CHRI", &infoIndex);
	modelEntry = Rld_FindEntry(&reader, "CMDL", &modelIndex);
	if ((infoIndex < 0) || (modelEntry == NULL))
	{
		Rld_Close(&reader);
		NativeChar_Refuse("DAMAGED", "envelope", "%s", "CHRI or CMDL is missing - required chunk");
		return;
	}

	// The CMDL hash as Rld_ReadChunk proves it below; the directory stays in the
	// reader after the file is closed.
	memcpy(cmdlHash, &modelEntry[RLD_DIR_HASH_OFFSET], sizeof(cmdlHash));
	fileBytes = reader.fileSize;

	infoBytes = Rld_ReadChunk(&reader, infoIndex, &infoSize, &why);
	if (infoBytes == NULL)
	{
		Rld_Close(&reader);
		NativeChar_Refuse("DAMAGED", "chunk", "%s", why);
		return;
	}

	cmdl = Rld_ReadChunk(&reader, modelIndex, &cmdlSize, &why);
	Rld_Close(&reader);
	if (cmdl == NULL)
	{
		free(infoBytes);
		NativeChar_Refuse("DAMAGED", "chunk", "%s", why);
		return;
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
		NativeChar_Refuse("DAMAGED", (rule[0] != '\0') ? rule : "CHRI", "%s", (colon != NULL) ? (colon + 2) : why);
		return;
	}

	// BEFORE the relocation: the pointer fields still hold body offsets, which
	// is what the check reads. The game stops at the first finding. Only an OK
	// reaches the relocation - with --ptr-map-unchecked LOAD_RunPtrMap checks
	// nothing itself, so this is the one check that always runs.
	verdict = RldChar_CheckModel(cmdl, cmdlSize, &finding, NULL, NULL, NULL);
	if (verdict != RLDCHAR_VERDICT_OK)
	{
		free(cmdl);
		NativeChar_Refuse(RldChar_VerdictWord(verdict), (finding.rule != NULL) ? finding.rule : "model", "%s", finding.detail);
		return;
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
			NativeChar_Refuse("PTRMAP", "ptrmap", "%s", "LOAD_RunPtrMap refused the pointer map - nothing patched");
			return;
		}

		s_char.cmdl = cmdl;
		s_char.model = (struct Model *)body;
		s_char.info = info;
	}

	Platform_Log("[CTR Char] loaded %s: template %u, class %u, CMDL %02x%02x%02x%02x%02x%02x, %llu bytes\n", s_charFile, (unsigned)info.templateId,
	             (unsigned)info.classId, (unsigned)cmdlHash[0], (unsigned)cmdlHash[1], (unsigned)cmdlHash[2], (unsigned)cmdlHash[3], (unsigned)cmdlHash[4],
	             (unsigned)cmdlHash[5], (unsigned long long)fileBytes);
}

// Entry 0 is the file when it loaded - a refused one has no tile - then the
// placeholders. Built once: the grid, the pick and the funnel all count on
// the same entries for the whole run. Silent while it is empty.
internal void NativeChar_BuildRoster(void)
{
	const int files = (s_char.model != NULL) ? 1 : 0;
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

	s_charRosterFiles = files;
	s_charRosterCount = count;

	if (count > 0)
	{
		Platform_Log("[CTR Char] roster: %d entr%s, %d file, %d placeholder(s)\n", count, (count == 1) ? "y" : "ies", files, count - files);
	}
}

void NativeChar_LoadDev(void)
{
	// Registered here, at start, so the line has its place in the table of exit
	// reports before the ones a race registers on its way.
	if (g_cfg_dev)
	{
		Platform_AtExitReport(NativeChar_ReportAtExit);
	}

	NativeChar_LoadFile();
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
		return (int)s_char.info.templateId;
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
		return s_char.info.name;
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
		return s_char.model;
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
	// The direct start has no driver select: it takes the file when there is
	// one. Whether it binds is still the funnel's answer - --driver has to have
	// put the template into characterIDs[0].
	NativeChar_SetPick((s_charRosterFiles > 0) ? 0 : -1);
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

	// 1. No file loaded (no --char, or refused): retail, silently. Placeholders
	//    alone never bind, so the roster has nothing to give either.
	if (s_char.model == NULL)
	{
		return;
	}

	// 2. A menu load, silently. characterIDs[0] keeps the template on the way
	//    back into the menu, and the menu births drivers too (VehBirth_NonGhost).
	if ((gGT->gameMode1 & MAIN_MENU) != 0)
	{
		return;
	}

	// 3. Demo: every seat is a bot of the attract mode or --autoload-demo.
	if (gGT->boolDemoMode != 0)
	{
		Platform_Log("[CTR Char] not bound: demo\n");
		return;
	}

	// 4. The mode.
	modeWhy = NativeChar_ModeRefusal(gGT);
	if (modeWhy != NULL)
	{
		Platform_Log("[CTR Char] not bound: mode %s (gameMode1 0x%08x, %d player(s))\n", modeWhy, (unsigned)gGT->gameMode1, (int)gGT->numPlyrCurrGame);
		return;
	}

	// 5. The pick names a file, and the seat runs on its template. Both, because
	//    the pick can outlive the choice it came from (CHANGE LEVEL, the next
	//    race of a cup), and characterIDs[0] is what the engine drives: the
	//    driver select writes the template there, --driver does on a direct start.
	if ((pick < 0) || (pick >= s_charRosterCount))
	{
		Platform_Log("[CTR Char] seat 0 empty: no custom pick\n");
		return;
	}

	// A file is in the roster only with its model, so no model means a
	// placeholder - which the driver select does not let anyone choose.
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

	// 6. The frame counts of the donor: the game logic counts frames from the
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

	// 7. Bound.
	s_seat[0].model = model;
	s_seat[0].motorId = templateId;
	Platform_Log("[CTR Char] seat 0 = %s on template %d\n", s_charFile, templateId);
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
