// ===========================================================================
// CUSTOM CHARACTERS - THE DEVELOPER PROOF OF CONCEPT.
//
// One .rldchar file stands in for the driver model of seat 0. Nothing else:
// no roster, no menu, no character select. The race is set up by the switches
// that already exist (--level, --driver <template>, --autopilot); this module
// only swaps the model the seat is born with.
//
// THE PATH OF THE FILE.
//   start    NativeChar_LoadDev, once, before CTR_Main: envelope (Rld_OpenAs
//            with s_rldCharFormat), CHRI, CMDL with its hash, RldChar_CheckModel
//            on the bytes as stored, then ONE LOAD_RunPtrMap on the host copy.
//            The model lives in host memory for the whole run - not in the
//            MEMPACK, so the memory layout of the game stays as it is.
//   stage 5  NativeChar_ArmSeats (game/LOAD/LOAD_TenStages.c): the funnel
//            decides per load whether seat 0 is bound. Its order is fixed, and
//            the first answer that is not "yes" ends it.
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
// RETAIL STAYS. Without --char: no file is opened, the funnel returns at its
// first step, the seat model is NULL and VehBirth takes the retail expression.
// The instance counter counts in every run and changes nothing; its exit line
// is there with --dev only. The log lines are read by a measuring tool and
// keep their wording.
//
// KNOWN GAP until the roster phase: a quick state holds host pointers and a
// model outside the MEMPACK. Proof-of-concept runs use no quick states.
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

// The overlay per seat. Only seat 0 is ever armed in the proof of concept.
// motorId is the character id the engine runs the seat on - the template.
global_variable struct
{
	struct Model *model;
	int motorId;
} s_seat[NATIVE_CHAR_SEATS];

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

void NativeChar_LoadDev(void)
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

	// Registered here, at start, so the line has its place in the table of exit
	// reports before the ones a race registers on its way.
	if (g_cfg_dev)
	{
		Platform_AtExitReport(NativeChar_ReportAtExit);
	}

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

void NativeChar_ClearSeats(void)
{
	memset(s_seat, 0, sizeof(s_seat));
}

// The first reason why this race is not one the proof of concept binds in,
// NULL when it is: arcade, one player, no time trial, adventure, relic, battle
// or cutscene bit. The crystal and CTR challenge bits do not refuse.
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

void NativeChar_ArmSeats(void)
{
	struct GameTracker *gGT = sdata->gGT;
	const int templateId = (int)s_char.info.templateId;
	const char *modeWhy;
	const struct Model *donor;
	int a;

	// Nothing from an earlier load survives into this one.
	NativeChar_ClearSeats();

	// 1. No file loaded (no --char, or refused): retail, silently.
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

	// 5. The seat runs on the template: --driver chose it.
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
		const u32 ours = NativeChar_AnimFrames(s_char.model, a);
		const u32 theirs = NativeChar_AnimFrames(donor, a);

		if (ours != theirs)
		{
			Platform_Log("[CTR Char] not bound: frames %d %u/%u\n", a, (unsigned)ours, (unsigned)theirs);
			return;
		}
	}

	// 7. Bound.
	s_seat[0].model = s_char.model;
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

	// Only with a loaded file: a refused one leaves the run retail.
	if ((s_char.model == NULL) || (gGT == NULL))
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
