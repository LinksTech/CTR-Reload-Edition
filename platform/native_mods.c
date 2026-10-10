// ===========================================================================
// THE MODS OF THE ARCADE RACE (include/platform/native_mods.h): the CPU seats
// of a one-player arcade race on the disc tracks, single race and cup.
//
// THE PLAN, load stage 4 (NativeMods_PlanSeats, from LOAD_DriverMPK right
// after LOAD_Robots1P): the seats 1..7 are drawn and written into
// data.characterIDs - a custom seat gets its file's template, as the dev seats
// do. The 1P arcade pack of the load holds the _hi models of seat 0 and of the
// troupe LOAD_Robots1P chose (BI_1PARCADEPACK + characterIDs[0]: the player
// and 0..7, or 0..6 for a player from 8 on). Every other retail driver of the
// plan - a retail seat or the template of a custom one - is read here from the
// bigfile (BI_RACERMODELHI + id, the same _hi model) into host memory beside
// the load queue (NativeCD_ReadSectorsAt) and relocated once; VehBirth and the
// donor search of the custom characters find it by name
// (NativeMods_ModelByName) after the pack. The textures of every driver lie
// in shared.vrm (loaded at boot), the portraits in every pack's icon table,
// so a model is all a retail driver needs. A model that cannot be read gives
// its seat to a retail driver of the pack nobody sits on - never a seat
// without a model.
//
// THE BINDING, load stage 5 (NativeChar_ArmSeats -> NativeChar_ArmModsSeats):
// the seats with a file are bound like seat 0 of a pick - the file's class,
// mask, map color and voices; a seat whose file does not fit its donor gets a
// free retail driver of the pack instead (NativeMods_RetailFallback).
//
// THE MEMORY: the read models live in host memory until the next plan or a
// menu load onto the main menu (NativeMods_ForgetPlan); the draw memory of
// the custom seats is reserved by NativeChar_DrawReserve (load stage 0) for
// every CPU seat at the largest file, and NativeChar_MempackExtraNeeded puts
// that behind the MEMPACK window at start whenever the roster holds a file.
//
// THE RANDOM NUMBERS are the module's own (xorshift32, seeded once from the
// performance counter at the first draw): the game's RNG never moves for a
// draw, and a race at the defaults never comes here.
// ===========================================================================

#include <platform/native_mods.h>

#define NATIVE_MODS_SEATS        8
#define NATIVE_MODS_RETAIL_COUNT 15
#define NATIVE_MODS_OFF_MAX      64
#define NATIVE_MODS_FILE_MAX     260
#define NATIVE_MODS_SECTOR_BYTES 0x800

int MM_NativeCupSelect_Chosen(void);
int NativeCD_ReadSectorsAt(s32 encodedPos, s32 sectors, void *dst);

// The two values, as the MODS page and ctr-settings.cfg set them.
global_variable int s_modsCpu = NATIVE_MODS_CPU_DEFAULT;
global_variable int s_modsCustom = NATIVE_MODS_CUSTOM_OFF;

// SELECT DRIVERS: the files that are NOT ticked, by file name (letter case
// ignored, as the folder scan takes names). A name of a file that is no longer
// in the folder stays - it is ticked off again when the file comes back.
global_variable char s_modsOff[NATIVE_MODS_OFF_MAX][NATIVE_MODS_FILE_MAX];
global_variable int s_modsOffCount;

// The plan of the load in progress: the roster entry of every seat (-1 =
// retail); valid while a race of the plan loads or runs. A cup keeps it for
// its next three races (cup = 1, with the player it was drawn for).
global_variable struct
{
	int valid;
	int cup;
	int player;
	int pick;
	int pack[NATIVE_MODS_SEATS];
	int id[NATIVE_MODS_SEATS];
	int entry[NATIVE_MODS_SEATS];
} s_modsPlan;

// The retail models read for the plan, in host memory.
global_variable struct
{
	int id;
	u8 *bytes;
	struct Model *model;
} s_modsModel[NATIVE_MODS_SEATS];
global_variable int s_modsModelCount;

// --dev-mods (main.c): the values of this run, never read from or written
// to the file; with file names, exactly these are ticked.
global_variable int s_modsDevGiven;
global_variable char s_modsDevFile[NATIVE_CHAR_ROSTER_MAX][NATIVE_MODS_FILE_MAX];
global_variable int s_modsDevFileCount;

global_variable u32 s_modsRandom;
global_variable int s_modsDraws;
global_variable char s_modsStatus[16];

// ---------------------------------------------------------------------------
// THE VALUES
// ---------------------------------------------------------------------------

int NativeMods_CpuCharacters(void)
{
	return s_modsCpu;
}

void NativeMods_SetCpuCharacters(int value)
{
	s_modsCpu = (value == NATIVE_MODS_CPU_RANDOM) ? NATIVE_MODS_CPU_RANDOM : NATIVE_MODS_CPU_DEFAULT;
	Platform_SettingsSave();
	Platform_Log("[CTR Mods] cpu characters: %s\n", (s_modsCpu == NATIVE_MODS_CPU_RANDOM) ? "random" : "default");
}

int NativeMods_CustomDrivers(void)
{
	return s_modsCustom;
}

void NativeMods_SetCustomDrivers(int value)
{
	s_modsCustom = ((value >= NATIVE_MODS_CUSTOM_OFF) && (value <= NATIVE_MODS_CUSTOM_SELECTED)) ? value : NATIVE_MODS_CUSTOM_OFF;
	Platform_SettingsSave();
	Platform_Log("[CTR Mods] cpu custom drivers: %s\n",
	             (s_modsCustom == NATIVE_MODS_CUSTOM_RANDOM) ? "random" : ((s_modsCustom == NATIVE_MODS_CUSTOM_SELECTED) ? "selected" : "off"));
}

int NativeMods_DriverCount(void)
{
	return NativeChar_RosterFileCount();
}

const char *NativeMods_DriverName(int index)
{
	return ((index >= 0) && (index < NativeChar_RosterFileCount())) ? NativeChar_EntryName(index) : "";
}

internal int NativeMods_OffIndex(const char *file)
{
	int i;

	for (i = 0; i < s_modsOffCount; i++)
	{
		if (SDL_strcasecmp(s_modsOff[i], file) == 0)
		{
			return i;
		}
	}

	return -1;
}

int NativeMods_DriverSelected(int index)
{
	int i;

	if ((index < 0) || (index >= NativeChar_RosterFileCount()))
	{
		return 0;
	}

	if (s_modsDevFileCount > 0)
	{
		for (i = 0; i < s_modsDevFileCount; i++)
		{
			if (SDL_strcasecmp(s_modsDevFile[i], NativeChar_EntryFile(index)) == 0)
			{
				return 1;
			}
		}

		return 0;
	}

	return NativeMods_OffIndex(NativeChar_EntryFile(index)) < 0;
}

// Only the list; the caller saves.
internal void NativeMods_StoreOff(const char *file, int off)
{
	const int at = NativeMods_OffIndex(file);

	if (off && (at < 0) && (s_modsOffCount < NATIVE_MODS_OFF_MAX) && (file[0] != '\0') && (strlen(file) < NATIVE_MODS_FILE_MAX))
	{
		snprintf(s_modsOff[s_modsOffCount], NATIVE_MODS_FILE_MAX, "%s", file);
		s_modsOffCount++;
	}
	else if (!off && (at >= 0))
	{
		s_modsOffCount--;
		if (at != s_modsOffCount)
		{
			memcpy(s_modsOff[at], s_modsOff[s_modsOffCount], NATIVE_MODS_FILE_MAX);
		}
	}
}

void NativeMods_SetDriverSelected(int index, int on)
{
	const char *file;

	if ((index < 0) || (index >= NativeChar_RosterFileCount()))
	{
		return;
	}

	file = NativeChar_EntryFile(index);
	NativeMods_StoreOff(file, !on);
	Platform_SettingsSave();
	Platform_Log("[CTR Mods] select drivers: %s %s\n", file, on ? "on" : "off");
}

int NativeMods_SelectedCount(void)
{
	int count = 0;
	int i;

	for (i = 0; i < NativeChar_RosterFileCount(); i++)
	{
		count += NativeMods_DriverSelected(i);
	}

	return count;
}

const char *NativeMods_StatusText(void)
{
	if (s_modsCustom == NATIVE_MODS_CUSTOM_RANDOM)
	{
		return "CUSTOM: ALL";
	}

	if (s_modsCustom == NATIVE_MODS_CUSTOM_SELECTED)
	{
		snprintf(s_modsStatus, sizeof(s_modsStatus), "CUSTOM: %d", NativeMods_SelectedCount());
		return s_modsStatus;
	}

	return (s_modsCpu == NATIVE_MODS_CPU_RANDOM) ? "CPU: RANDOM" : "OFF";
}

// ---------------------------------------------------------------------------
// THE SETTINGS FILE
// ---------------------------------------------------------------------------

void NativeMods_SaveLines(FILE *file)
{
	int i;

	fprintf(file, "mods cpuchars %d\n", s_modsCpu);
	fprintf(file, "mods custom %d\n", s_modsCustom);
	for (i = 0; i < s_modsOffCount; i++)
	{
		fprintf(file, "mods off %s\n", s_modsOff[i]);
	}
}

int NativeMods_LoadLine(const char *rest)
{
	char name[NATIVE_MODS_FILE_MAX];
	size_t length;
	int value;

	// --dev-mods holds this run: the lines are known, but they do not count.
	if (s_modsDevGiven)
	{
		return 1;
	}

	if (sscanf(rest, "cpuchars %d", &value) == 1)
	{
		s_modsCpu = (value == NATIVE_MODS_CPU_RANDOM) ? NATIVE_MODS_CPU_RANDOM : NATIVE_MODS_CPU_DEFAULT;
		return 1;
	}

	if (sscanf(rest, "custom %d", &value) == 1)
	{
		s_modsCustom = ((value >= NATIVE_MODS_CUSTOM_OFF) && (value <= NATIVE_MODS_CUSTOM_SELECTED)) ? value : NATIVE_MODS_CUSTOM_OFF;
		return 1;
	}

	// The name is the rest of the line: a file name may hold spaces.
	if ((strncmp(rest, "off ", 4) == 0) && (strlen(&rest[4]) < sizeof(name)))
	{
		snprintf(name, sizeof(name), "%s", &rest[4]);
		length = strlen(name);
		while ((length > 0) && ((name[length - 1] == '\n') || (name[length - 1] == '\r')))
		{
			name[--length] = '\0';
		}

		if (length > 0)
		{
			NativeMods_StoreOff(name, 1);
			return 1;
		}
	}

	return 0;
}

int NativeMods_SetDev(const char *list)
{
	char copy[1024];
	char *word;
	char *next;
	int field = 0;

	if ((list == NULL) || (strlen(list) >= sizeof(copy)))
	{
		return 0;
	}

	snprintf(copy, sizeof(copy), "%s", list);
	s_modsDevFileCount = 0;
	for (word = copy; word != NULL; word = next, field++)
	{
		next = strchr(word, ',');
		if (next != NULL)
		{
			*next++ = '\0';
		}

		if (field == 0)
		{
			if ((strcmp(word, "default") != 0) && (strcmp(word, "random") != 0))
			{
				return 0;
			}
			s_modsCpu = (strcmp(word, "random") == 0) ? NATIVE_MODS_CPU_RANDOM : NATIVE_MODS_CPU_DEFAULT;
		}
		else if (field == 1)
		{
			if (strcmp(word, "off") == 0)
			{
				s_modsCustom = NATIVE_MODS_CUSTOM_OFF;
			}
			else if (strcmp(word, "random") == 0)
			{
				s_modsCustom = NATIVE_MODS_CUSTOM_RANDOM;
			}
			else if (strcmp(word, "selected") == 0)
			{
				s_modsCustom = NATIVE_MODS_CUSTOM_SELECTED;
			}
			else
			{
				return 0;
			}
		}
		else if ((word[0] == '\0') || (s_modsDevFileCount >= NATIVE_CHAR_ROSTER_MAX) || (strlen(word) >= NATIVE_MODS_FILE_MAX))
		{
			return 0;
		}
		else
		{
			snprintf(s_modsDevFile[s_modsDevFileCount], NATIVE_MODS_FILE_MAX, "%s", word);
			s_modsDevFileCount++;
		}
	}

	if (field < 2)
	{
		return 0;
	}

	s_modsDevGiven = 1;
	Platform_Log("[CTR Mods] --dev-mods: cpu %s, custom %s, %d file(s) ticked by name (this run only)\n", (s_modsCpu == NATIVE_MODS_CPU_RANDOM) ? "random" : "default",
	             (s_modsCustom == NATIVE_MODS_CUSTOM_RANDOM) ? "random" : ((s_modsCustom == NATIVE_MODS_CUSTOM_SELECTED) ? "selected" : "off"), s_modsDevFileCount);
	return 1;
}

void NativeMods_SetSeed(u32 seed)
{
	s_modsRandom = seed;
	Platform_Log("[CTR Mods] --dev-mods-seed: the draw starts from %u\n", (unsigned)seed);
}

// ---------------------------------------------------------------------------
// THE RULE OF THE RACE
// ---------------------------------------------------------------------------

int NativeMods_MenuOffered(void)
{
	const struct GameTracker *gGT = sdata->gGT;
	u32 mode1;

	if (gGT == NULL)
	{
		return 0;
	}

	mode1 = (u32)gGT->gameMode1;
	return ((mode1 & ARCADE_MODE) != 0) && ((mode1 & (TIME_TRIAL | ADVENTURE_MODE | BATTLE_MODE)) == 0) && (gGT->numPlyrNextGame == 1) &&
	       (gGT->boolDemoMode == 0) && (MM_NativeTrackSelect_Chosen() == 0) && (MM_NativeCupSelect_Chosen() == 0);
}

// The race being loaded is one the box was offered for: the funnel's mode
// rule (arcade, one player, no time trial, adventure, relic, battle or
// cutscene), no demo, no NITRO-PIT marker and none of the challenge bits.
internal int NativeMods_RaceAllowed(const struct GameTracker *gGT)
{
	return (gGT != NULL) && ((gGT->gameMode1 & MAIN_MENU) == 0) && (gGT->boolDemoMode == 0) && (NativeChar_ModeRefusal(gGT) == NULL) &&
	       ((gGT->gameMode1 & CRYSTAL_CHALLENGE) == 0) && ((gGT->gameMode2 & TOKEN_RACE) == 0) && (MM_NativeTrackSelect_Chosen() == 0) &&
	       (MM_NativeCupSelect_Chosen() == 0);
}

internal int NativeMods_Defaults(void)
{
	return (s_modsCpu == NATIVE_MODS_CPU_DEFAULT) && (s_modsCustom == NATIVE_MODS_CUSTOM_OFF);
}

int NativeMods_CustomRace(void)
{
	return (s_modsCustom != NATIVE_MODS_CUSTOM_OFF) && (NativeChar_RosterFileCount() > 0) && NativeMods_RaceAllowed(sdata->gGT);
}

// ---------------------------------------------------------------------------
// THE MODELS
// ---------------------------------------------------------------------------

internal void NativeMods_ReleaseModels(void)
{
	int i;

	for (i = 0; i < s_modsModelCount; i++)
	{
		free(s_modsModel[i].bytes);
	}

	memset(s_modsModel, 0, sizeof(s_modsModel));
	s_modsModelCount = 0;
}

void NativeMods_ForgetPlan(void)
{
	if (s_modsPlan.valid || (s_modsModelCount > 0))
	{
		Platform_Log("[CTR Mods] plan let go (%d retail model(s) read for it)\n", s_modsModelCount);
	}

	memset(&s_modsPlan, 0, sizeof(s_modsPlan));
	NativeMods_ReleaseModels();
}

int NativeMods_HoldsModels(void)
{
	return s_modsModelCount > 0;
}

internal int NativeMods_NameIs(const struct Model *model, const char *name)
{
	int word;

	for (word = 0; word < MODEL_NAME_WORD_COUNT; word++)
	{
		if (ModelName_ReadWord(model->name, word) != ModelName_ReadWord(name, word))
		{
			return 0;
		}
	}

	return 1;
}

struct Model *NativeMods_ModelByName(const char *name)
{
	int i;

	for (i = 0; (name != NULL) && (i < s_modsModelCount); i++)
	{
		if (NativeMods_NameIs(s_modsModel[i].model, name))
		{
			return s_modsModel[i].model;
		}
	}

	return NULL;
}

// The _hi model of a retail driver, read whole into host memory, its pointer
// map checked against the file and run once (the checks of
// LOAD_DramFileCallback). Layout: word 0 is the body's length = the offset
// of the map, the body (the model) from 4, then the map's byte count and the
// offsets. NULL, with one line, for whatever does not hold.
internal struct Model *NativeMods_ReadModel(struct BigHeader *bigfile, int id)
{
	const struct BigEntry *entries = BIG_GETENTRY(bigfile);
	const struct BigEntry *entry = &entries[BI_RACERMODELHI + id];
	const u32 size = (u32)entry->size;
	const s32 sectors = (s32)((size + NATIVE_MODS_SECTOR_BYTES - 1u) / NATIVE_MODS_SECTOR_BYTES);
	const char *why = NULL;
	u8 *bytes;
	u32 bodyBytes = 0;
	u32 mapBytes = 0;

	if ((size < 12u) || (s_modsModelCount >= NATIVE_MODS_SEATS))
	{
		Platform_LogWarn("[CTR Mods] model of driver %d not read: %s\n", id, (size < 12u) ? "the bigfile entry is too small" : "no free place");
		return NULL;
	}

	bytes = (u8 *)malloc((size_t)sectors * NATIVE_MODS_SECTOR_BYTES);
	if (bytes == NULL)
	{
		Platform_LogWarn("[CTR Mods] model of driver %d not read: out of memory\n", id);
		return NULL;
	}

	if (!NativeCD_ReadSectorsAt(bigfile->cdpos + entry->offset, sectors, bytes))
	{
		why = "the disc read failed";
	}
	else
	{
		memcpy(&bodyBytes, &bytes[0], 4);
		if ((bodyBytes > (size - 8u)) || ((bodyBytes & 3u) != 0u))
		{
			why = "the body does not fit the file";
		}
		else
		{
			memcpy(&mapBytes, &bytes[4u + bodyBytes], 4);
			if (((mapBytes & 3u) != 0u) || (mapBytes > (size - 8u - bodyBytes)))
			{
				why = "the pointer map does not fit the file";
			}
			else if (LOAD_RunPtrMap((char *)&bytes[4], (int)bodyBytes, (int *)&bytes[8u + bodyBytes], (int)(mapBytes / 4u)) == 0)
			{
				why = "LOAD_RunPtrMap refused the pointer map";
			}
			else if (!NativeMods_NameIs((const struct Model *)&bytes[4], data.MetaDataCharacters[id].name_Debug))
			{
				why = "the model has another name";
			}
		}
	}

	if (why != NULL)
	{
		free(bytes);
		Platform_LogWarn("[CTR Mods] model of driver %d not read: %s\n", id, why);
		return NULL;
	}

	s_modsModel[s_modsModelCount].id = id;
	s_modsModel[s_modsModelCount].bytes = bytes;
	s_modsModel[s_modsModelCount].model = (struct Model *)&bytes[4];
	s_modsModelCount++;
	Platform_Log("[CTR Mods] model of driver %d (%s) read from the bigfile: %u bytes\n", id, data.MetaDataCharacters[id].name_Debug, (unsigned)size);
	return (struct Model *)&bytes[4];
}

// ---------------------------------------------------------------------------
// THE DRAW
// ---------------------------------------------------------------------------

internal u32 NativeMods_Next(void)
{
	if (s_modsRandom == 0u)
	{
		const u64 counter = (u64)SDL_GetPerformanceCounter();

		s_modsRandom = (u32)(counter ^ (counter >> 32)) | 1u;
	}

	s_modsRandom ^= s_modsRandom << 13;
	s_modsRandom ^= s_modsRandom >> 17;
	s_modsRandom ^= s_modsRandom << 5;
	return s_modsRandom;
}

// 0..count - 1, without the bias of a plain modulo.
internal int NativeMods_Below(int count)
{
	const u32 limit = 0xffffffffu - (0xffffffffu % (u32)count);
	u32 value;

	do
	{
		value = NativeMods_Next();
	} while (value >= limit);

	return (int)(value % (u32)count);
}

internal int NativeMods_InPack(const int pack[NATIVE_MODS_SEATS], int id)
{
	int i;

	for (i = 0; i < NATIVE_MODS_SEATS; i++)
	{
		if (pack[i] == id)
		{
			return 1;
		}
	}

	return 0;
}

// A retail id of the pack that no seat shows (a custom seat shows its file,
// not its template), and not seat 0's; -1 for none. With eight ids in the pack
// and at most seven other seats there is always one.
internal int NativeMods_FreePackId(const int pack[NATIVE_MODS_SEATS], int seat)
{
	int i;

	for (i = 0; i < NATIVE_MODS_SEATS; i++)
	{
		int used = (pack[i] == (int)data.characterIDs[0]);
		int s;

		for (s = 1; !used && (s < NATIVE_MODS_SEATS); s++)
		{
			used = (s != seat) && (s_modsPlan.entry[s] < 0) && ((int)data.characterIDs[s] == pack[i]);
		}

		if (!used)
		{
			return pack[i];
		}
	}

	return -1;
}

internal void NativeMods_LogPlan(const char *how)
{
	char line[512];
	size_t used = 0;
	int seat;

	line[0] = '\0';
	for (seat = 1; seat < NATIVE_MODS_SEATS; seat++)
	{
		const int entry = s_modsPlan.entry[seat];
		const int written = (entry >= 0) ? snprintf(&line[used], sizeof(line) - used, " %d=%s", seat, NativeChar_EntryFile(entry))
		                                 : snprintf(&line[used], sizeof(line) - used, " %d=%s", seat,
		                                            data.MetaDataCharacters[(int)data.characterIDs[seat]].name_Debug);

		if ((written < 0) || ((size_t)written >= (sizeof(line) - used)))
		{
			break;
		}
		used += (size_t)written;
	}

	Platform_Log("[CTR Mods] seats (%s, level %d, cpu %s, custom %s):%s\n", how, (int)sdata->gGT->levelID,
	             (s_modsCpu == NATIVE_MODS_CPU_RANDOM) ? "random" : "default",
	             (s_modsCustom == NATIVE_MODS_CUSTOM_RANDOM) ? "random" : ((s_modsCustom == NATIVE_MODS_CUSTOM_SELECTED) ? "selected" : "off"), line);
}

// The draw: the pool (retail part, custom part), shuffled with the module's
// own numbers (Fisher-Yates), the first seven to the seats 1..7 in order. Too
// few: filled with retail drivers nobody has, in id order.
internal void NativeMods_Draw(const int pack[NATIVE_MODS_SEATS], int pick)
{
	int poolId[NATIVE_MODS_RETAIL_COUNT + NATIVE_CHAR_ROSTER_MAX];
	int poolEntry[NATIVE_MODS_RETAIL_COUNT + NATIVE_CHAR_ROSTER_MAX];
	const int player = (int)data.characterIDs[0];
	int count = 0;
	int seat;
	int i;

	if (s_modsCpu == NATIVE_MODS_CPU_RANDOM)
	{
		for (i = 0; i < NATIVE_MODS_RETAIL_COUNT; i++)
		{
			if (i != player)
			{
				poolId[count] = i;
				poolEntry[count] = -1;
				count++;
			}
		}
	}
	else
	{
		for (seat = 1; seat < NATIVE_MODS_SEATS; seat++)
		{
			poolId[count] = pack[seat];
			poolEntry[count] = -1;
			count++;
		}
	}

	for (i = 0; (s_modsCustom != NATIVE_MODS_CUSTOM_OFF) && (i < NativeChar_RosterFileCount()); i++)
	{
		if ((i == pick) || ((s_modsCustom == NATIVE_MODS_CUSTOM_SELECTED) && !NativeMods_DriverSelected(i)))
		{
			continue;
		}

		poolId[count] = NativeChar_EntryTemplate(i);
		poolEntry[count] = i;
		count++;
	}

	for (i = count - 1; i > 0; i--)
	{
		const int j = NativeMods_Below(i + 1);
		const int id = poolId[i];
		const int entry = poolEntry[i];

		poolId[i] = poolId[j];
		poolEntry[i] = poolEntry[j];
		poolId[j] = id;
		poolEntry[j] = entry;
	}

	for (seat = 1; seat < NATIVE_MODS_SEATS; seat++)
	{
		if ((seat - 1) < count)
		{
			data.characterIDs[seat] = (s16)poolId[seat - 1];
			s_modsPlan.entry[seat] = poolEntry[seat - 1];
			continue;
		}

		// Filled up: the first retail id no seat shows.
		for (i = 0; i < NATIVE_MODS_RETAIL_COUNT; i++)
		{
			int used = (i == player);
			int s;

			for (s = 1; !used && (s < seat); s++)
			{
				used = (s_modsPlan.entry[s] < 0) && ((int)data.characterIDs[s] == i);
			}

			if (!used)
			{
				break;
			}
		}

		data.characterIDs[seat] = (s16)((i < NATIVE_MODS_RETAIL_COUNT) ? i : pack[seat]);
		s_modsPlan.entry[seat] = -1;
	}
}

void NativeMods_PlanSeats(struct BigHeader *bigfile)
{
	struct GameTracker *gGT = sdata->gGT;
	int pack[NATIVE_MODS_SEATS];
	int pick = NativeChar_Pick();
	const int cup = ((gGT->gameMode2 & CUP_ANY_KIND) != 0);
	int seat;

	// The defaults: nothing at all - not even the plan of an earlier race is
	// touched here (a menu load lets it go).
	if (NativeMods_Defaults() && !s_modsPlan.valid && (s_modsModelCount == 0))
	{
		return;
	}

	if (NativeMods_Defaults() || !NativeMods_RaceAllowed(gGT))
	{
		NativeMods_ForgetPlan();
		return;
	}

	// The player's file only while seat 0 runs on its template (the funnel's
	// own condition, step 6 of NativeChar_ArmSeats).
	if ((pick < 0) || (pick >= NativeChar_RosterFileCount()) || (NativeChar_EntryTemplate(pick) != (int)data.characterIDs[0]))
	{
		pick = -1;
	}

	// What the pack of this load holds: LOAD_Robots1P has just written it.
	for (seat = 0; seat < NATIVE_MODS_SEATS; seat++)
	{
		pack[seat] = (int)data.characterIDs[seat];
	}

	// The next race of the same cup: the same seats.
	if (cup && s_modsPlan.valid && s_modsPlan.cup && (s_modsPlan.player == pack[0]) && (s_modsPlan.pick == pick))
	{
		for (seat = 1; seat < NATIVE_MODS_SEATS; seat++)
		{
			data.characterIDs[seat] = (s16)s_modsPlan.id[seat];
		}

		NativeMods_LogPlan("cup, the seats of its first race");
		return;
	}

	NativeMods_ReleaseModels();
	memset(&s_modsPlan, 0, sizeof(s_modsPlan));
	s_modsDraws++;
	NativeMods_Draw(pack, pick);

	// Every retail model the plan needs and the pack does not hold.
	for (seat = 1; seat < NATIVE_MODS_SEATS; seat++)
	{
		const int id = (int)data.characterIDs[seat];

		if (NativeMods_InPack(pack, id) || (NativeMods_ModelByName(data.MetaDataCharacters[id].name_Debug) != NULL))
		{
			continue;
		}

		if (NativeMods_ReadModel(bigfile, id) == NULL)
		{
			const int instead = NativeMods_FreePackId(pack, seat);

			Platform_LogWarn("[CTR Mods] seat %d: driver %d has no model - %s %d of the pack instead\n", seat, id,
			                 (s_modsPlan.entry[seat] >= 0) ? "the custom driver gives way to retail driver" : "retail driver", instead);
			data.characterIDs[seat] = (s16)instead;
			s_modsPlan.entry[seat] = -1;
		}
	}

	s_modsPlan.valid = 1;
	s_modsPlan.cup = cup;
	memcpy(s_modsPlan.pack, pack, sizeof(pack));
	s_modsPlan.player = pack[0];
	s_modsPlan.pick = pick;
	for (seat = 0; seat < NATIVE_MODS_SEATS; seat++)
	{
		s_modsPlan.id[seat] = (int)data.characterIDs[seat];
	}
	s_modsPlan.entry[0] = -1;

	NativeMods_LogPlan(cup ? "cup, drawn for its races" : "single race, drawn");
}

int NativeMods_SeatEntry(int seat)
{
	if (!s_modsPlan.valid || (seat < 1) || (seat >= NATIVE_MODS_SEATS))
	{
		return -1;
	}

	return s_modsPlan.entry[seat];
}

// Load stage 5: a seat whose file could not be bound gets a retail driver of
// the pack nobody shows (the pack is the one this load holds: seat 0's and
// LOAD_Robots1P's ids). Returns the id written.
int NativeMods_RetailFallback(int seat)
{
	int id;

	if (!s_modsPlan.valid || (seat < 1) || (seat >= NATIVE_MODS_SEATS))
	{
		return -1;
	}

	s_modsPlan.entry[seat] = -1;
	id = NativeMods_FreePackId(s_modsPlan.pack, seat);
	if (id >= 0)
	{
		data.characterIDs[seat] = (s16)id;
		s_modsPlan.id[seat] = id;
	}

	return id;
}
