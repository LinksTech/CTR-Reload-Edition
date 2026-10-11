// ===========================================================================
// THE MODS OF THE ARCADE RACE (include/platform/native_mods.h): the CPU seats
// of a one-player arcade race, single race and cup, on the disc tracks and in
// NITRO-PIT.
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
// A FILE THAT CANNOT SIT ON ITS TEMPLATE: the seat's model at load stage 5 is
// the pack's when the pack holds the template, else the one read here - and
// the two need not have the same frame counts (the pack's Coco and Polar have
// others than every file rldpack writes; the bigfile's have not). So before
// the plan stands, a drawn file whose template the pack does not hold is
// checked against the model read here for it (NativeChar_FitsDonor, the check
// of load stage 5); one that does not fit leaves the pool, and the seats are
// drawn again without it. A template in the pack is checked at stage 5 alone;
// a seat that does not fit there gets, with ONLY SELECTED, a ticked retail
// driver whose model the load holds, else a free one of the pack.
//
// THE BINDING, load stage 5 (NativeChar_ArmSeats -> NativeChar_ArmModsSeats):
// the seats with a file are bound like seat 0 of a pick - the file's class,
// mask, map color and voices; a seat whose file does not fit its donor after
// all gets a free retail driver of the pack instead (NativeMods_RetailFallback).
//
// THE MEMORY: the read models live in host memory until the next plan or a
// menu load onto the main menu (NativeMods_ForgetPlan); the draw memory of
// the custom seats is reserved by NativeChar_DrawReserve (load stage 0) for
// every CPU seat at the largest file, and NativeChar_MempackExtraNeeded puts
// that behind the MEMPACK window at start whenever the roster holds a file.
//
// THE RANDOM NUMBERS are the module's own (xorshift32, seeded once from the
// performance counter at the first draw): the game's RNG never moves for a
// draw, and a race at DEFAULT never comes here.
// ===========================================================================

#include <platform/native_mods.h>

#define NATIVE_MODS_SEATS        8
#define NATIVE_MODS_MODEL_MAX    NATIVE_MODS_RETAIL_COUNT
#define NATIVE_MODS_ON_MAX       128
#define NATIVE_MODS_OLD_OFF_MAX  64
#define NATIVE_MODS_FILE_MAX     260
#define NATIVE_MODS_SECTOR_BYTES 0x800
#define NATIVE_MODS_POOL_MAX     (NATIVE_MODS_RETAIL_COUNT + NATIVE_CHAR_ROSTER_MAX)

int NativeCD_ReadSectorsAt(s32 encodedPos, s32 sectors, void *dst);

// The value, as the MODS page and ctr-settings.cfg set it.
global_variable int s_modsCpu = NATIVE_MODS_CPU_DEFAULT;

// SELECT DRIVERS: the ticked retail drivers (bit = id) and the ticked files by
// file name (letter case ignored, as the folder scan takes names). A name of a
// file that is no longer in the folder stays - it is ticked again when the
// file comes back.
global_variable u32 s_modsRetailOn;
global_variable char s_modsOn[NATIVE_MODS_ON_MAX][NATIVE_MODS_FILE_MAX];
global_variable int s_modsOnCount;

// The lines of the first MODS page (CPU CHARACTERS, CPU CUSTOM DRIVERS and the
// files unticked there), kept until the roster is known
// (NativeMods_AfterRoster). A line of this page wins over all of them.
global_variable struct
{
	int seen;
	int cpu;
	int custom;
} s_modsOld;
global_variable char s_modsOldOff[NATIVE_MODS_OLD_OFF_MAX][NATIVE_MODS_FILE_MAX];
global_variable int s_modsOldOffCount;
global_variable int s_modsNewSeen;

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

	// BOSS: a plan with the boss alone on seat 1 (bossPlan 1), his index and
	// the page's value it was drawn for (RANDOM draws once per cup).
	int bossPlan;
	int boss;
	int bossValue;
} s_modsPlan;

// The retail models read for the plan, in host memory: the retail seats the
// pack does not hold and the templates of the drawn files the pack does not
// hold.
global_variable struct
{
	int id;
	u8 *bytes;
	struct Model *model;
} s_modsModel[NATIVE_MODS_MODEL_MAX];
global_variable int s_modsModelCount;

// --dev-mods (main.c): the value of this run, never read from or written to
// the file; with drivers, exactly these are ticked.
global_variable int s_modsDevGiven;
global_variable int s_modsDevTicks;
global_variable u32 s_modsDevRetail;
global_variable char s_modsDevFile[NATIVE_CHAR_ROSTER_MAX][NATIVE_MODS_FILE_MAX];
global_variable int s_modsDevFileCount;

global_variable u32 s_modsRandom;
global_variable int s_modsDraws;
global_variable char s_modsStatus[16];

// ---------------------------------------------------------------------------
// THE VALUE AND THE TICKS
// ---------------------------------------------------------------------------

internal const char *NativeMods_ValueName(int value)
{
	return (value == NATIVE_MODS_CPU_ALL) ? "all random" : ((value == NATIVE_MODS_CPU_SELECTED) ? "only selected" : "default");
}

internal int NativeMods_Clamp(int value)
{
	return ((value == NATIVE_MODS_CPU_ALL) || (value == NATIVE_MODS_CPU_SELECTED)) ? value : NATIVE_MODS_CPU_DEFAULT;
}

int NativeMods_CpuDrivers(void)
{
	return s_modsCpu;
}

void NativeMods_SetCpuDrivers(int value)
{
	s_modsCpu = NativeMods_Clamp(value);
	Platform_SettingsSave();
	Platform_Log("[CTR Mods] cpu drivers: %s\n", NativeMods_ValueName(s_modsCpu));
}

int NativeMods_DriverCount(void)
{
	return NATIVE_MODS_RETAIL_COUNT + NativeChar_RosterFileCount();
}

const char *NativeMods_DriverName(int index)
{
	if ((index >= 0) && (index < NATIVE_MODS_RETAIL_COUNT))
	{
		return sdata->lngStrings[data.MetaDataCharacters[index].name_LNG_long];
	}

	index -= NATIVE_MODS_RETAIL_COUNT;
	return ((index >= 0) && (index < NativeChar_RosterFileCount())) ? NativeChar_EntryName(index) : "";
}

internal int NativeMods_OnIndex(const char *file)
{
	int i;

	for (i = 0; i < s_modsOnCount; i++)
	{
		if (SDL_strcasecmp(s_modsOn[i], file) == 0)
		{
			return i;
		}
	}

	return -1;
}

internal int NativeMods_DevFileIndex(const char *file)
{
	int i;

	for (i = 0; i < s_modsDevFileCount; i++)
	{
		if (SDL_strcasecmp(s_modsDevFile[i], file) == 0)
		{
			return i;
		}
	}

	return -1;
}

// A file of the roster (entry) is ticked.
internal int NativeMods_FileTicked(int entry)
{
	const char *file = NativeChar_EntryFile(entry);

	return s_modsDevTicks ? (NativeMods_DevFileIndex(file) >= 0) : (NativeMods_OnIndex(file) >= 0);
}

internal int NativeMods_RetailTicked(int id)
{
	return (((s_modsDevTicks ? s_modsDevRetail : s_modsRetailOn) >> id) & 1u) != 0;
}

int NativeMods_DriverSelected(int index)
{
	if ((index >= 0) && (index < NATIVE_MODS_RETAIL_COUNT))
	{
		return NativeMods_RetailTicked(index);
	}

	index -= NATIVE_MODS_RETAIL_COUNT;
	return ((index >= 0) && (index < NativeChar_RosterFileCount())) ? NativeMods_FileTicked(index) : 0;
}

// Only the list; the caller saves.
internal void NativeMods_StoreOn(const char *file, int on)
{
	const int at = NativeMods_OnIndex(file);

	if (on && (at < 0) && (s_modsOnCount < NATIVE_MODS_ON_MAX) && (file[0] != '\0') && (strlen(file) < NATIVE_MODS_FILE_MAX))
	{
		snprintf(s_modsOn[s_modsOnCount], NATIVE_MODS_FILE_MAX, "%s", file);
		s_modsOnCount++;
	}
	else if (!on && (at >= 0))
	{
		s_modsOnCount--;
		if (at != s_modsOnCount)
		{
			memcpy(s_modsOn[at], s_modsOn[s_modsOnCount], NATIVE_MODS_FILE_MAX);
		}
	}
}

// The ticks of --dev-mods: this run only.
internal void NativeMods_StoreDev(const char *file, int on)
{
	const int at = NativeMods_DevFileIndex(file);

	if (on && (at < 0) && (s_modsDevFileCount < NATIVE_CHAR_ROSTER_MAX) && (strlen(file) < NATIVE_MODS_FILE_MAX))
	{
		snprintf(s_modsDevFile[s_modsDevFileCount], NATIVE_MODS_FILE_MAX, "%s", file);
		s_modsDevFileCount++;
	}
	else if (!on && (at >= 0))
	{
		s_modsDevFileCount--;
		if (at != s_modsDevFileCount)
		{
			memcpy(s_modsDevFile[at], s_modsDevFile[s_modsDevFileCount], NATIVE_MODS_FILE_MAX);
		}
	}
}

void NativeMods_SetDriverSelected(int index, int on)
{
	if ((index >= 0) && (index < NATIVE_MODS_RETAIL_COUNT))
	{
		u32 *mask = s_modsDevTicks ? &s_modsDevRetail : &s_modsRetailOn;

		*mask = on ? (*mask | (1u << index)) : (*mask & ~(1u << index));
		Platform_SettingsSave();
		Platform_Log("[CTR Mods] select drivers: retail driver %d (%s) %s\n", index, data.MetaDataCharacters[index].name_Debug, on ? "on" : "off");
		return;
	}

	index -= NATIVE_MODS_RETAIL_COUNT;
	if ((index < 0) || (index >= NativeChar_RosterFileCount()))
	{
		return;
	}

	if (s_modsDevTicks)
	{
		NativeMods_StoreDev(NativeChar_EntryFile(index), on);
	}
	else
	{
		NativeMods_StoreOn(NativeChar_EntryFile(index), on);
	}

	Platform_SettingsSave();
	Platform_Log("[CTR Mods] select drivers: %s %s\n", NativeChar_EntryFile(index), on ? "on" : "off");
}

int NativeMods_SelectedCount(void)
{
	const int count = NativeMods_DriverCount();
	int selected = 0;
	int i;

	for (i = 0; i < count; i++)
	{
		selected += NativeMods_DriverSelected(i);
	}

	return selected;
}

internal int NativeMods_PlayCount(void);
internal void NativeMods_PlaySaveLines(FILE *file);
internal int NativeMods_PlayLoadLine(const char *rest);

const char *NativeMods_StatusText(void)
{
	const int play = NativeMods_PlayCount();
	const int selected = (s_modsCpu == NATIVE_MODS_CPU_SELECTED) ? NativeMods_SelectedCount() : 0;
	const int boss = NativeMods_Value(NATIVE_MODS_BOSS);

	// BOSS leaves CPU DRIVERS aside: the boss alone, or with the count of the
	// other play mods.
	if (boss != NATIVE_MODS_BOSS_OFF)
	{
		if (play > 0)
		{
			snprintf(s_modsStatus, sizeof(s_modsStatus), "BOSS +%d", play);
		}
		else if (boss == NATIVE_MODS_BOSS_RANDOM)
		{
			snprintf(s_modsStatus, sizeof(s_modsStatus), "RANDOM BOSS");
		}
		else
		{
			const char *name = NativeMods_ValueText(NATIVE_MODS_BOSS);

			snprintf(s_modsStatus, sizeof(s_modsStatus), ((strlen(name) + 6u) <= 11u) ? "BOSS: %s" : "%s", name);
		}
		return s_modsStatus;
	}

	// With play mods on, the CPU value short and the count: the box holds 11
	// characters, and "ALL RANDOM + 2" would be 14.
	if (play > 0)
	{
		if (s_modsCpu == NATIVE_MODS_CPU_ALL)
		{
			snprintf(s_modsStatus, sizeof(s_modsStatus), "RANDOM +%d", play);
		}
		else if (selected > 0)
		{
			snprintf(s_modsStatus, sizeof(s_modsStatus), "%d SEL +%d", selected, play);
		}
		else
		{
			snprintf(s_modsStatus, sizeof(s_modsStatus), (play == 1) ? "%d MOD" : "%d MODS", play);
		}
		return s_modsStatus;
	}

	if (s_modsCpu == NATIVE_MODS_CPU_ALL)
	{
		return "ALL RANDOM";
	}

	// Nothing ticked is DEFAULT, and the box says so.
	if (selected > 0)
	{
		snprintf(s_modsStatus, sizeof(s_modsStatus), "%d SELECTED", selected);
		return s_modsStatus;
	}

	return "OFF";
}

// ---------------------------------------------------------------------------
// THE SETTINGS FILE
// ---------------------------------------------------------------------------

void NativeMods_SaveLines(FILE *file)
{
	int i;

	fprintf(file, "mods cpu %d\n", s_modsCpu);
	NativeMods_PlaySaveLines(file);
	for (i = 0; i < NATIVE_MODS_RETAIL_COUNT; i++)
	{
		if (((s_modsRetailOn >> i) & 1u) != 0)
		{
			fprintf(file, "mods retail %d\n", i);
		}
	}

	for (i = 0; i < s_modsOnCount; i++)
	{
		fprintf(file, "mods file %s\n", s_modsOn[i]);
	}
}

// The rest of a line after "<key> " as a name: a file name may hold spaces.
// 0 for a name that is empty or too long.
internal int NativeMods_LineName(const char *rest, size_t keyLength, char name[NATIVE_MODS_FILE_MAX])
{
	size_t length;

	if (strlen(&rest[keyLength]) >= NATIVE_MODS_FILE_MAX)
	{
		return 0;
	}

	snprintf(name, NATIVE_MODS_FILE_MAX, "%s", &rest[keyLength]);
	length = strlen(name);
	while ((length > 0) && ((name[length - 1] == '\n') || (name[length - 1] == '\r')))
	{
		name[--length] = '\0';
	}

	return length > 0;
}

int NativeMods_LoadLine(const char *rest)
{
	char name[NATIVE_MODS_FILE_MAX];
	int value;

	// THE PLAY MODS, before --dev-mods: that switch holds the CPU lines only.
	if (NativeMods_PlayLoadLine(rest))
	{
		return 1;
	}

	// --dev-mods holds this run: the lines are known, but they do not count.
	if (s_modsDevGiven)
	{
		return 1;
	}

	if (sscanf(rest, "cpu %d", &value) == 1)
	{
		s_modsCpu = NativeMods_Clamp(value);
		s_modsNewSeen = 1;
		return 1;
	}

	if ((sscanf(rest, "retail %d", &value) == 1) && (value >= 0) && (value < NATIVE_MODS_RETAIL_COUNT))
	{
		s_modsRetailOn |= 1u << value;
		s_modsNewSeen = 1;
		return 1;
	}

	if ((strncmp(rest, "file ", 5) == 0) && NativeMods_LineName(rest, 5, name))
	{
		NativeMods_StoreOn(name, 1);
		s_modsNewSeen = 1;
		return 1;
	}

	// The first MODS page.
	if (sscanf(rest, "cpuchars %d", &value) == 1)
	{
		s_modsOld.seen = 1;
		s_modsOld.cpu = (value == 1);
		return 1;
	}

	if (sscanf(rest, "custom %d", &value) == 1)
	{
		s_modsOld.seen = 1;
		s_modsOld.custom = ((value >= 0) && (value <= 2)) ? value : 0;
		return 1;
	}

	if ((strncmp(rest, "off ", 4) == 0) && NativeMods_LineName(rest, 4, name))
	{
		if (s_modsOldOffCount < NATIVE_MODS_OLD_OFF_MAX)
		{
			snprintf(s_modsOldOff[s_modsOldOffCount], NATIVE_MODS_FILE_MAX, "%s", name);
			s_modsOldOffCount++;
		}
		s_modsOld.seen = 1;
		return 1;
	}

	return 0;
}

// THE FIRST MODS PAGE, CARRIED OVER. It had CPU CHARACTERS (DEFAULT: the
// troupe, RANDOM: every retail driver) and CPU CUSTOM DRIVERS (OFF, RANDOM:
// every file, SELECTED: the files not unticked); the pool was both together.
//   both at their default          -> DEFAULT
//   CUSTOM DRIVERS RANDOM          -> ALL RANDOM
//   CPU CHARACTERS RANDOM alone    -> ONLY SELECTED, every retail driver
//   CUSTOM DRIVERS SELECTED        -> ONLY SELECTED, every retail driver and
//                                     every file of the roster not unticked
// The troupe of DEFAULT cannot be ticked; every retail driver stands for it.
// Saved at once in the lines of this page (never under --settings-defaults).
void NativeMods_AfterRoster(void)
{
	int ticked = 0;
	int i;

	if (!s_modsOld.seen || s_modsNewSeen || s_modsDevGiven)
	{
		s_modsOld.seen = 0;
		return;
	}

	s_modsOld.seen = 0;
	if (s_modsOld.custom == 1)
	{
		s_modsCpu = NATIVE_MODS_CPU_ALL;
	}
	else if (s_modsOld.cpu || (s_modsOld.custom == 2))
	{
		s_modsCpu = NATIVE_MODS_CPU_SELECTED;
		s_modsRetailOn = (1u << NATIVE_MODS_RETAIL_COUNT) - 1u;
		ticked = NATIVE_MODS_RETAIL_COUNT;

		for (i = 0; (s_modsOld.custom == 2) && (i < NativeChar_RosterFileCount()); i++)
		{
			const char *file = NativeChar_EntryFile(i);
			int off = 0;
			int k;

			for (k = 0; k < s_modsOldOffCount; k++)
			{
				off |= (SDL_strcasecmp(s_modsOldOff[k], file) == 0);
			}

			if (!off)
			{
				NativeMods_StoreOn(file, 1);
				ticked++;
			}
		}
	}
	else
	{
		s_modsCpu = NATIVE_MODS_CPU_DEFAULT;
	}

	Platform_Log("[CTR Mods] settings of the first MODS page carried over: cpu characters %s, custom drivers %s -> cpu drivers %s, %d driver(s) ticked\n",
	             s_modsOld.cpu ? "random" : "default", (s_modsOld.custom == 1) ? "random" : ((s_modsOld.custom == 2) ? "selected" : "off"),
	             NativeMods_ValueName(s_modsCpu), ticked);
	Platform_SettingsSave();
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
	s_modsDevRetail = 0;
	for (word = copy; word != NULL; word = next, field++)
	{
		next = strchr(word, ',');
		if (next != NULL)
		{
			*next++ = '\0';
		}

		if (field == 0)
		{
			if (strcmp(word, "default") == 0)
			{
				s_modsCpu = NATIVE_MODS_CPU_DEFAULT;
			}
			else if (strcmp(word, "random") == 0)
			{
				s_modsCpu = NATIVE_MODS_CPU_ALL;
			}
			else if (strcmp(word, "selected") == 0)
			{
				s_modsCpu = NATIVE_MODS_CPU_SELECTED;
			}
			else
			{
				return 0;
			}
		}
		else if ((word[0] >= '0') && (word[0] <= '9') && (strspn(word, "0123456789") == strlen(word)))
		{
			const int id = atoi(word);

			if (id >= NATIVE_MODS_RETAIL_COUNT)
			{
				return 0;
			}
			s_modsDevRetail |= 1u << id;
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

	s_modsDevGiven = 1;
	s_modsDevTicks = (field > 1);
	Platform_Log("[CTR Mods] --dev-mods: cpu drivers %s, %s (this run only)\n", NativeMods_ValueName(s_modsCpu),
	             s_modsDevTicks ? "the drivers named ticked" : "the ticks of the file");
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
	const int chosen = MM_NativeTrackSelect_Chosen();
	u32 mode1;

	if (gGT == NULL)
	{
		return 0;
	}

	mode1 = (u32)gGT->gameMode1;
	return ((mode1 & ARCADE_MODE) != 0) && ((mode1 & (TIME_TRIAL | ADVENTURE_MODE | BATTLE_MODE)) == 0) && (gGT->numPlyrNextGame == 1) &&
	       (gGT->boolDemoMode == 0) && (chosen != MM_NATIVE_CHOSEN_CRYSTAL) && (chosen != MM_NATIVE_CHOSEN_CTR) &&
	       (chosen != MM_NATIVE_CHOSEN_TIME_TRIAL);
}

// The race being loaded is one the box was offered for: the funnel's mode
// rule (arcade, one player, no time trial, adventure, relic, battle or
// cutscene), no demo, none of the challenge bits (CRYSTAL and CTR of
// NITRO-PIT set them) and not the TIME TRIAL of NITRO-PIT (a NITRO RACE
// alone, MM_NativeTimeTrial.c: no mode bit of its own).
internal int NativeMods_RaceAllowed(const struct GameTracker *gGT)
{
	return (gGT != NULL) && ((gGT->gameMode1 & MAIN_MENU) == 0) && (gGT->boolDemoMode == 0) && (NativeChar_ModeRefusal(gGT) == NULL) &&
	       ((gGT->gameMode1 & CRYSTAL_CHALLENGE) == 0) && ((gGT->gameMode2 & TOKEN_RACE) == 0) &&
	       (MM_NativeTrackSelect_Mode() != MM_NATIVE_MODE_TIME_TRIAL);
}

internal int NativeMods_Defaults(void)
{
	return s_modsCpu == NATIVE_MODS_CPU_DEFAULT;
}

int NativeMods_CustomRace(void)
{
	int can = 0;
	int i;

	// BOSS leaves the CPU seats to the boss alone.
	if (NativeMods_Value(NATIVE_MODS_BOSS) != NATIVE_MODS_BOSS_OFF)
	{
		return 0;
	}

	if (s_modsCpu == NATIVE_MODS_CPU_ALL)
	{
		can = (NativeChar_RosterFileCount() > 0);
	}
	else if (s_modsCpu == NATIVE_MODS_CPU_SELECTED)
	{
		for (i = 0; !can && (i < NativeChar_RosterFileCount()); i++)
		{
			can = NativeMods_FileTicked(i);
		}
	}

	return can && NativeMods_RaceAllowed(sdata->gGT);
}

// ---------------------------------------------------------------------------
// THE PLAY MODS: GRAVITY, GRIP, NIGHT, ITEMS, DIFFICULTY, BOSS (native_mods.h)
// ---------------------------------------------------------------------------

// The range of each value, its normal value (= off) and whether it is a
// percentage (steps of 10 on the page).
internal const struct
{
	const char *key;
	int min;
	int max;
	int normal;
	int percent;
} s_modsRange[NATIVE_MODS_VALUES] = {
    {"gravity", 20, 200, 100, 1}, {"grip", 0, 100, 100, 1},      {"night", 0, 90, 0, 1},
    {"items", 0, 2, 0, 0},        {"difficulty", 0, 3, 1, 0},    {"boss", 0, 6, 0, 0},
};

internal const char *const s_modsItemsText[3] = {"DEFAULT", "CHAOS", "CUSTOM"};
internal const char *const s_modsDiffText[4] = {"EASY", "NORMAL", "HARD", "EXTREME"};
internal const char *const s_modsBossText[7] = {"OFF", "RANDOM", "ROO", "PAPU", "JOE", "PINSTRIPE", "OXIDE"};
internal const char *const s_modsLevelText[4] = {"OFF", "LOW", "NORMAL", "HIGH"};
internal const int s_modsLevelWeight[4] = {0, 1, 2, 4};

// DIFFICULTY: the move in the game's blend (0xF0 is the distance from its
// first table to its second), and the range a move may reach - from one step
// of the cup menu below its EASY (0x50 - 0x50 = 0, the quick start's 0 - 0x50)
// to two steps above its HARD.
internal const int s_modsDiffMove[4] = {-0x50, 0, 0x50, 0xA0};
#define NATIVE_MODS_DIFF_LOW  (-0x50)
#define NATIVE_MODS_DIFF_HIGH 0x190

// THE LOW-GRAVITY SHARE of the game (VehPhysForce_OnGravity, BOTS.c): 41 %.
#define NATIVE_MODS_LOW_GRAVITY 41

struct NativeModsPlay
{
	int value[NATIVE_MODS_VALUES];
	u8 level[NATIVE_MODS_ITEM_IDS];

	// --dev-mods-play only: the bots' difficulty before DIFFICULTY (-1 none)
	// and the race frame of a snapshot (0 none).
	int base;
	int shot;
};

internal void NativeMods_PlayDefaults(struct NativeModsPlay *play)
{
	int k;

	memset(play, 0, sizeof(*play));
	for (k = 0; k < NATIVE_MODS_VALUES; k++)
	{
		play->value[k] = s_modsRange[k].normal;
	}
	for (k = 0; k < NATIVE_MODS_ITEM_IDS; k++)
	{
		play->level[k] = NATIVE_MODS_WEIGHT_NORMAL;
	}
	play->base = -1;
}

// The page's and the file's values; --dev-mods-play's sets for this run, one
// per race load (the last one stays), which win and are never saved.
#define NATIVE_MODS_DEV_SETS 16
global_variable struct NativeModsPlay s_modsPlayFile;
global_variable int s_modsPlayFileReady;
global_variable struct NativeModsPlay s_modsPlayDev[NATIVE_MODS_DEV_SETS];
global_variable int s_modsPlayDevCount;
global_variable int s_modsPlayDevIndex;
global_variable int s_modsPlayDevRaces;

internal struct NativeModsPlay *NativeMods_FilePlay(void)
{
	if (!s_modsPlayFileReady)
	{
		NativeMods_PlayDefaults(&s_modsPlayFile);
		s_modsPlayFileReady = 1;
	}

	return &s_modsPlayFile;
}

internal struct NativeModsPlay *NativeMods_Play(void)
{
	return (s_modsPlayDevCount > 0) ? &s_modsPlayDev[s_modsPlayDevIndex] : NativeMods_FilePlay();
}

internal int NativeMods_InRange(int which, int value)
{
	return (value >= s_modsRange[which].min) && (value <= s_modsRange[which].max);
}

// --mods-selftest: the race rule is taken as met (there is no race to ask).
global_variable int s_modsSelfTestRace;

internal int NativeMods_RaceOk(void)
{
	return s_modsSelfTestRace || NativeMods_RaceAllowed(sdata->gGT);
}

int NativeMods_Value(int which)
{
	return ((which >= 0) && (which < NATIVE_MODS_VALUES)) ? NativeMods_Play()->value[which] : 0;
}

int NativeMods_NormalValue(int which)
{
	return ((which >= 0) && (which < NATIVE_MODS_VALUES)) ? s_modsRange[which].normal : 0;
}

const char *NativeMods_ValueText(int which)
{
	static char text[16];
	const int value = NativeMods_Value(which);

	switch (which)
	{
	case NATIVE_MODS_ITEMS:
		return s_modsItemsText[value];
	case NATIVE_MODS_DIFFICULTY:
		return s_modsDiffText[value];
	case NATIVE_MODS_BOSS:
		return s_modsBossText[value];
	default:
		snprintf(text, sizeof(text), "%d %%", value);
		return text;
	}
}

internal void NativeMods_PlaySaved(void)
{
	// --dev-mods-play holds this run; the page changes it, the file keeps its own.
	if (s_modsPlayDevCount == 0)
	{
		Platform_SettingsSave();
	}
}

void NativeMods_SetValue(int which, int value)
{
	if ((which < 0) || (which >= NATIVE_MODS_VALUES))
	{
		return;
	}

	value = (value < s_modsRange[which].min) ? s_modsRange[which].min : ((value > s_modsRange[which].max) ? s_modsRange[which].max : value);
	NativeMods_Play()->value[which] = value;
	NativeMods_PlaySaved();
	Platform_Log("[CTR Mods] %s: %s\n", s_modsRange[which].key, NativeMods_ValueText(which));
}

int NativeMods_Step(int which, int dir)
{
	int value;
	int next;

	if ((which < 0) || (which >= NATIVE_MODS_VALUES) || (dir == 0))
	{
		return 0;
	}

	value = NativeMods_Value(which);
	if (s_modsRange[which].percent)
	{
		next = (dir > 0) ? (((value / 10) + 1) * 10) : ((((value + 9) / 10) - 1) * 10);
	}
	else
	{
		next = value + ((dir > 0) ? 1 : -1);
	}

	next = (next < s_modsRange[which].min) ? s_modsRange[which].min : ((next > s_modsRange[which].max) ? s_modsRange[which].max : next);
	if (next == value)
	{
		return 0;
	}

	NativeMods_SetValue(which, next);
	return 1;
}

int NativeMods_ItemLevel(int item)
{
	return ((item >= 0) && (item < NATIVE_MODS_ITEM_IDS)) ? NativeMods_Play()->level[item] : NATIVE_MODS_WEIGHT_NORMAL;
}

void NativeMods_SetItemLevel(int item, int level)
{
	if ((item < 0) || (item >= NATIVE_MODS_ITEM_IDS))
	{
		return;
	}

	level = (level < NATIVE_MODS_WEIGHT_OFF) ? NATIVE_MODS_WEIGHT_OFF : ((level > NATIVE_MODS_WEIGHT_HIGH) ? NATIVE_MODS_WEIGHT_HIGH : level);
	NativeMods_Play()->level[item] = (u8)level;
	NativeMods_PlaySaved();
	Platform_Log("[CTR Mods] item weight %s: %s\n", NativeMods_ItemName(item), s_modsLevelText[level]);
}

// ctr-settings.cfg: the file's values (never --dev-mods-play's), an item
// weight only where it is not NORMAL.
internal void NativeMods_PlaySaveLines(FILE *file)
{
	const struct NativeModsPlay *play = NativeMods_FilePlay();
	int k;

	for (k = 0; k < NATIVE_MODS_VALUES; k++)
	{
		fprintf(file, "mods %s %d\n", s_modsRange[k].key, play->value[k]);
	}

	for (k = 0; k < NATIVE_MODS_ITEM_IDS; k++)
	{
		if (play->level[k] != NATIVE_MODS_WEIGHT_NORMAL)
		{
			fprintf(file, "mods weight %d %d\n", k, (int)play->level[k]);
		}
	}
}

// A value out of its range is its normal value. Round 1's lines: moon 1 is
// GRAVITY 41 % (its fixed value), icy 1 GRIP 0 %, chaos 1 ITEMS CHAOS; any
// other number the normal value.
internal int NativeMods_PlayLoadLine(const char *rest)
{
	struct NativeModsPlay *play = NativeMods_FilePlay();
	char format[32];
	int value;
	int item;
	int k;

	for (k = 0; k < NATIVE_MODS_VALUES; k++)
	{
		snprintf(format, sizeof(format), "%s %%d", s_modsRange[k].key);
		if (sscanf(rest, format, &value) == 1)
		{
			play->value[k] = NativeMods_InRange(k, value) ? value : s_modsRange[k].normal;
			return 1;
		}
	}

	if (sscanf(rest, "weight %d %d", &item, &value) == 2)
	{
		if ((item >= 0) && (item < NATIVE_MODS_ITEM_IDS))
		{
			play->level[item] = (u8)(((value >= NATIVE_MODS_WEIGHT_OFF) && (value <= NATIVE_MODS_WEIGHT_HIGH)) ? value : NATIVE_MODS_WEIGHT_NORMAL);
		}
		return 1;
	}

	if (sscanf(rest, "moon %d", &value) == 1)
	{
		play->value[NATIVE_MODS_GRAVITY] = (value == 1) ? NATIVE_MODS_LOW_GRAVITY : 100;
		return 1;
	}
	if (sscanf(rest, "icy %d", &value) == 1)
	{
		play->value[NATIVE_MODS_GRIP] = (value == 1) ? 0 : 100;
		return 1;
	}
	if (sscanf(rest, "chaos %d", &value) == 1)
	{
		play->value[NATIVE_MODS_ITEMS] = (value == 1) ? NATIVE_MODS_ITEMS_CHAOS : NATIVE_MODS_ITEMS_DEFAULT;
		return 1;
	}

	return 0;
}

int VehPhysGeneral_RaceItems(u8 items[NATIVE_MODS_ITEM_IDS]); // game/Vehicle/VehPhysGeneral.c

int NativeMods_RaceItems(u8 items[NATIVE_MODS_ITEM_IDS])
{
	return VehPhysGeneral_RaceItems(items);
}

const char *NativeMods_ItemName(int item)
{
	static const char *const names[NATIVE_MODS_ITEM_IDS] = {"TURBO",  "BOMB",     "MISSILE",     "TNT",            "POTION",       "SPRING",
	                                                        "SHIELD", "MASK",     "CLOCK",       "WARPBALL",       "TRIPLE BOMB",  "TRIPLE MISSILE",
	                                                        "INVISIBILITY", "SUPER ENGINE", "ITEM 14", "NONE"};

	return ((item >= 0) && (item < NATIVE_MODS_ITEM_IDS)) ? names[item] : "?";
}

// The weights of ITEM WEIGHTS over the race items.
internal int NativeMods_CustomTotal(const struct NativeModsPlay *play)
{
	u8 items[NATIVE_MODS_ITEM_IDS];
	const int count = NativeMods_RaceItems(items);
	int total = 0;
	int k;

	for (k = 0; k < count; k++)
	{
		total += s_modsLevelWeight[play->level[items[k]]];
	}

	return total;
}

int NativeMods_ItemsAllOff(void)
{
	const struct NativeModsPlay *play = NativeMods_Play();

	return (play->value[NATIVE_MODS_ITEMS] == NATIVE_MODS_ITEMS_CUSTOM) && (NativeMods_CustomTotal(play) == 0);
}

// Away from normal, by the page's values (no race asked): what the status
// line counts.
internal int NativeMods_PlayOn(int which)
{
	const struct NativeModsPlay *play = NativeMods_Play();

	if (which == NATIVE_MODS_ITEMS)
	{
		return (play->value[which] == NATIVE_MODS_ITEMS_CHAOS) || ((play->value[which] == NATIVE_MODS_ITEMS_CUSTOM) && !NativeMods_ItemsAllOff());
	}

	return play->value[which] != s_modsRange[which].normal;
}

internal int NativeMods_PlayCount(void)
{
	int count = 0;
	int k;

	for (k = 0; k < NATIVE_MODS_VALUES; k++)
	{
		count += (k != NATIVE_MODS_BOSS) && NativeMods_PlayOn(k);
	}

	return count;
}

// THE BOSS OF THE RACE IN PROGRESS: set by the plan (NativeMods_PlanSeats) of
// every arcade race load, -1 for a race without one.
global_variable int s_modsBossIndex = -1;

int NativeMods_Active(int which)
{
	if ((which < 0) || (which >= NATIVE_MODS_VALUES) || !NativeMods_RaceOk())
	{
		return 0;
	}

	if (which == NATIVE_MODS_BOSS)
	{
		return s_modsBossIndex >= 0;
	}

	return NativeMods_PlayOn(which);
}

// ---------------------------------------------------------------------------
// THE REPORT AT EXIT
// ---------------------------------------------------------------------------

// Hang-time turbos by fire step (0..2, small medium large), as given and as
// the meter would have given them; the road terrain driven as ice or as a
// blend; items by HELD_ITEM id, from boxes, CPU attacks and the boss.
#define NATIVE_MODS_REPORT_IDS 0x20
global_variable int s_modsReportArmed;
global_variable int s_modsLandingGiven[3];
global_variable int s_modsLandingFull[3];
global_variable int s_modsLandings;
global_variable int s_modsItems[3][NATIVE_MODS_REPORT_IDS];
global_variable u32 s_modsIcy[2];
global_variable u32 s_modsBlend[2];
global_variable int s_modsBotRescues;
global_variable int s_modsNightFrames;
global_variable int s_modsLaps;

internal void NativeMods_PlayReport(void)
{
	static const char *const step[3] = {"small", "medium", "large"};
	static const char *const from[3] = {"item(s) from boxes", "CPU attack(s)", "boss weapon(s)"};
	int k;

	if ((s_modsIcy[0] != 0) || (s_modsIcy[1] != 0))
	{
		Platform_Log("[CTR Mods] at exit: grip 0 %%, road terrain driven as ice in %u ground frame(s) of a kart and %u nav frame(s) of a CPU driver\n",
		             (unsigned)s_modsIcy[0], (unsigned)s_modsIcy[1]);
	}

	if ((s_modsBlend[0] != 0) || (s_modsBlend[1] != 0))
	{
		Platform_Log("[CTR Mods] at exit: grip between 0 and 100 %%, road terrain driven as a blend with ice in %u ground frame(s) of a kart and %u nav "
		             "frame(s) of a CPU driver\n",
		             (unsigned)s_modsBlend[0], (unsigned)s_modsBlend[1]);
	}

	if (s_modsNightFrames != 0)
	{
		Platform_Log("[CTR Mods] at exit: night, %d frame(s) darkened\n", s_modsNightFrames);
	}

	if (s_modsReportArmed)
	{
		Platform_Log("[CTR Mods] at exit: %d CPU driver(s) put back on the path after a kill plane while a play mod was on\n", s_modsBotRescues);
	}

	if (s_modsLandings != 0)
	{
		Platform_Log("[CTR Mods] at exit: gravity, %d landing(s) with a hang-time turbo by the meter\n", s_modsLandings);
		for (k = 0; k < 3; k++)
		{
			Platform_Log("[CTR Mods]   %-6s by the meter %d, given %d\n", step[k], s_modsLandingFull[k], s_modsLandingGiven[k]);
		}
	}

	for (k = 0; k < 3; k++)
	{
		int total = 0;
		int id;

		for (id = 0; id < NATIVE_MODS_REPORT_IDS; id++)
		{
			total += s_modsItems[k][id];
		}

		if (total != 0)
		{
			char line[512] = "";

			for (id = 0; id < NATIVE_MODS_REPORT_IDS; id++)
			{
				if (s_modsItems[k][id] != 0)
				{
					snprintf(&line[strlen(line)], sizeof(line) - strlen(line), " %d=%d", id, s_modsItems[k][id]);
				}
			}
			Platform_Log("[CTR Mods] at exit: %d %s (held item id=count):%s\n", total, from[k], line);
		}
	}

	if (s_modsLaps != 0)
	{
		Platform_Log("[CTR Mods] at exit: %d lap(s) logged\n", s_modsLaps);
	}
}

internal void NativeMods_ArmReport(void)
{
	if (!s_modsReportArmed && !s_modsSelfTestRace)
	{
		s_modsReportArmed = 1;
		Platform_AtExitReport(NativeMods_PlayReport);
	}
}

// ---------------------------------------------------------------------------
// GRAVITY
// ---------------------------------------------------------------------------

int NativeMods_GravityPercent(int zonePercent)
{
	int gravity;

	if (!NativeMods_Active(NATIVE_MODS_GRAVITY))
	{
		return zonePercent;
	}

	// A low-gravity stretch keeps the lower of its own share and the mod's.
	gravity = NativeMods_Value(NATIVE_MODS_GRAVITY);
	return ((zonePercent < 100) && (zonePercent < gravity)) ? zonePercent : gravity;
}

// A driver's flight in progress: the air time past the meter's 2400 ms, and
// the gravity it fell at against the game's own, each times the frame's ms.
#define NATIVE_MODS_DRIVERS 8
global_variable struct
{
	int air;
	int airborne;
	long long fell;
	long long own;
} s_modsFlight[NATIVE_MODS_DRIVERS];

void NativeMods_NoteGravity(int driverID, int percent, int ownPercent, int ms, int airborne)
{
	if ((driverID < 0) || (driverID >= NATIVE_MODS_DRIVERS) || !NativeMods_Active(NATIVE_MODS_GRAVITY))
	{
		return;
	}

	if (!airborne)
	{
		s_modsFlight[driverID].airborne = 0;
		return;
	}

	// A take-off: a new flight.
	if (!s_modsFlight[driverID].airborne)
	{
		s_modsFlight[driverID].airborne = 1;
		s_modsFlight[driverID].fell = 0;
		s_modsFlight[driverID].own = 0;
	}

	s_modsFlight[driverID].fell += (long long)percent * ms;
	s_modsFlight[driverID].own += (long long)ownPercent * ms;
}

void NativeMods_AirTime(int driverID, int airTime)
{
	if ((driverID >= 0) && (driverID < NATIVE_MODS_DRIVERS) && NativeMods_Active(NATIVE_MODS_GRAVITY))
	{
		s_modsFlight[driverID].air = airTime;
	}
}

// The air time of a jump is its upward speed over the gravity, twice: at a
// share of the gravity it lasts 1 / share as long, and the air time times the
// share is the time the jump takes at the game's own gravity. A flight over a
// low-gravity stretch is weighed by the share it fell at there against the
// game's 41 %; without a recorded flight the share is the mod's value.
int NativeMods_HangTime(int driverID, int jumpMeter)
{
	long long fell = NativeMods_Value(NATIVE_MODS_GRAVITY);
	long long own = 100;
	int air = jumpMeter;

	if (!NativeMods_Active(NATIVE_MODS_GRAVITY))
	{
		return jumpMeter;
	}

	if ((driverID >= 0) && (driverID < NATIVE_MODS_DRIVERS))
	{
		air = (s_modsFlight[driverID].air > jumpMeter) ? s_modsFlight[driverID].air : jumpMeter;
		if (s_modsFlight[driverID].own > 0)
		{
			fell = s_modsFlight[driverID].fell;
			own = s_modsFlight[driverID].own;
		}
		s_modsFlight[driverID].air = 0;
		s_modsFlight[driverID].fell = 0;
		s_modsFlight[driverID].own = 0;
	}

	return (int)(((long long)air * fell) / own);
}

void NativeMods_NoteLanding(int given, int full)
{
	if (full < 0)
	{
		return;
	}

	NativeMods_ArmReport();
	s_modsLandings++;
	s_modsLandingFull[(full > 2) ? 2 : full]++;
	if (given >= 0)
	{
		s_modsLandingGiven[(given > 2) ? 2 : given]++;
	}
}

// ---------------------------------------------------------------------------
// GRIP
// ---------------------------------------------------------------------------

// Road terrain: full speed and full grip in the game's table (speedMultiplier,
// slowUntilSpeed, counterSteerRatio and groundFrictionScale all 0x100), and
// no effect of its own. Water and fast water keep their surface and splash
// (COLL.c, the water and mud branch), mud its slowing and its spray, and
// sideslip its flag; grass, dirt, track, snow and the two slow terrains brake
// beside the road and keep doing it.
internal int NativeMods_RoadTerrain(int terrainType)
{
	switch (terrainType)
	{
	case TERRAIN_ASPHALT:
	case TERRAIN_WOOD:
	case TERRAIN_STONE:
	case TERRAIN_HARDPACK:
	case TERRAIN_METAL:
	case TERRAIN_RIVERASPHALT:
	case TERRAIN_STEAMASPHALT:
	case TERRAIN_OCEANASPHALT:
		return 1;
	default:
		return 0;
	}
}

u8 NativeMods_IcyTerrain(u8 terrainType, int bot)
{
	if (!NativeMods_RoadTerrain(terrainType) || !NativeMods_Active(NATIVE_MODS_GRIP) || (NativeMods_Value(NATIVE_MODS_GRIP) != 0))
	{
		return terrainType;
	}

	if (!s_modsSelfTestRace)
	{
		NativeMods_ArmReport();
		s_modsIcy[bot ? 1 : 0]++;
	}
	return (u8)TERRAIN_ICE;
}

// THE BLEND: road + (ice - road) * (100 - grip) / 100 for every number of the
// entry that drives (speed, slowdown, counter-steer, lean, friction, turn,
// response, the bots' target speed, acceleration and friction); the flags of
// the ice below 50 % (its skid marks, its reverse slide, no turn assist), the
// road's from 50 % on; name, sound, vibration and particles stay the road's.
#define NATIVE_MODS_TERRAIN_TYPES 21
global_variable struct Terrain s_modsGripTerrain[NATIVE_MODS_TERRAIN_TYPES];
global_variable int s_modsGripMade[NATIVE_MODS_TERRAIN_TYPES];

internal int NativeMods_Blend(int road, int ice, int iceShare)
{
	return road + (((ice - road) * iceShare) / 100);
}

internal void NativeMods_MakeGrip(struct Terrain *out, const struct Terrain *road, const struct Terrain *ice, int grip)
{
	const int share = 100 - grip;

	*out = *road;
	out->speedMultiplier = NativeMods_Blend(road->speedMultiplier, ice->speedMultiplier, share);
	out->slowUntilSpeed = NativeMods_Blend(road->slowUntilSpeed, ice->slowUntilSpeed, share);
	out->counterSteerRatio = NativeMods_Blend(road->counterSteerRatio, ice->counterSteerRatio, share);
	out->turnLeanScale = NativeMods_Blend(road->turnLeanScale, ice->turnLeanScale, share);
	out->groundFrictionScale = NativeMods_Blend(road->groundFrictionScale, ice->groundFrictionScale, share);
	out->turnAngleScale = NativeMods_Blend(road->turnAngleScale, ice->turnAngleScale, share);
	out->turnResponseScale = NativeMods_Blend(road->turnResponseScale, ice->turnResponseScale, share);
	out->bot.fields.targetSpeedScale = (s16)NativeMods_Blend(road->bot.fields.targetSpeedScale, ice->bot.fields.targetSpeedScale, share);
	out->bot.fields.accelerationScale = (s16)NativeMods_Blend(road->bot.fields.accelerationScale, ice->bot.fields.accelerationScale, share);
	out->botFrictionScale = (s16)NativeMods_Blend(road->botFrictionScale, ice->botFrictionScale, share);

	if (grip < 50)
	{
		out->flags = ice->flags;
		out->bot.fields.speedFlags = ice->bot.fields.speedFlags;
	}
}

struct Terrain *NativeMods_GripTerrain(u8 terrainType, struct Terrain *meta, int bot)
{
	int grip;

	if ((terrainType >= NATIVE_MODS_TERRAIN_TYPES) || !NativeMods_RoadTerrain(terrainType) || !NativeMods_Active(NATIVE_MODS_GRIP))
	{
		return meta;
	}

	grip = NativeMods_Value(NATIVE_MODS_GRIP);
	if (grip == 0)
	{
		return meta;
	}

	if (s_modsGripMade[terrainType] != grip)
	{
		NativeMods_MakeGrip(&s_modsGripTerrain[terrainType], &data.MetaDataTerrain[terrainType], &data.MetaDataTerrain[TERRAIN_ICE], grip);
		s_modsGripMade[terrainType] = grip;
	}

	if (!s_modsSelfTestRace)
	{
		NativeMods_ArmReport();
		s_modsBlend[bot ? 1 : 0]++;
	}
	return &s_modsGripTerrain[terrainType];
}

// ---------------------------------------------------------------------------
// NIGHT
// ---------------------------------------------------------------------------

// The colour the view is multiplied by: blue keeps 100 - night percent,
// green a sixteenth of the night less, red a quarter less (the slight blue of
// a night). 255 is "unchanged" (the renderer multiplies by colour / 255).
int NativeMods_NightColor(int percent, u8 rgb[3])
{
	int keep;

	if (percent <= 0)
	{
		return 0;
	}

	percent = (percent > 90) ? 90 : percent;
	keep = 100 - percent;
	rgb[0] = (u8)(((255 * keep * (400 - percent)) + 20000) / 40000);
	rgb[1] = (u8)(((255 * keep * (1600 - percent)) + 80000) / 160000);
	rgb[2] = (u8)(((255 * keep) + 50) / 100);
	return 1;
}

// The packet: the multiply request (DR_PSYX_BLEND, include/psx/libgpu.h) for
// the polygon after it, the draw mode (semi-transparent, drawing into the
// display area, no dither) and a flat quad over the whole view. It goes into
// slot 0 of the view's ordering table - the nearest, drawn after every other
// primitive of the world - at the end of the world's frame: what was linked
// there before (the fade of PushBuffer_FadeAllWindows) is drawn after it, the
// HUD (pushBuffer_UI, the slots before) after the world anyway.
typedef struct
{
	DR_PSYX_BLEND blend;
	u32 tag;
	u32 tpage;
	POLY_F4 f4;
} NativeModsNightPacket;

internal void NativeMods_NightQuad(struct GameTracker *gGT)
{
	struct PushBuffer *pb = &gGT->pushBuffer[0];
	struct DB *backBuffer = gGT->backBuffer;
	NativeModsNightPacket *p;
	u8 rgb[3];

	if (!NativeMods_NightColor(NativeMods_Value(NATIVE_MODS_NIGHT), rgb))
	{
		return;
	}

	p = (NativeModsNightPacket *)backBuffer->primMem.cursor;
	if ((u8 *)(p + 1) > (u8 *)backBuffer->primMem.end)
	{
		return;
	}

	SetPsyXBlend(&p->blend, NATIVE_PSYX_BLEND_MULTIPLY);
	setlen(&p->tag, 7);
	p->f4.tag = 0;
	p->f4.code = 0x2a;
	p->tpage = 0xe1000400;
	p->f4.r0 = rgb[0];
	p->f4.g0 = rgb[1];
	p->f4.b0 = rgb[2];
	p->f4.x0 = 0;
	p->f4.y0 = 0;
	p->f4.x1 = pb->rect.w;
	p->f4.y1 = 0;
	p->f4.x2 = 0;
	p->f4.y2 = pb->rect.h;
	p->f4.x3 = pb->rect.w;
	p->f4.y3 = pb->rect.h;

	// The polygon first, then the request in front of it: the parser meets the
	// request first.
	AddPrim(pb->ptrOT, &p->tag);
	AddPrim(pb->ptrOT, &p->blend);
	backBuffer->primMem.cursor = p + 1;

	if (!s_modsNightFrames++)
	{
		u8 shown[3];

		NativeMods_NightColor(NativeMods_Value(NATIVE_MODS_NIGHT), shown);
		Platform_Log("[CTR Mods] night %d %%: the view is multiplied by %d/%d/%d of 255\n", NativeMods_Value(NATIVE_MODS_NIGHT), shown[0], shown[1],
		             shown[2]);
		NativeMods_ArmReport();
	}
}

// ---------------------------------------------------------------------------
// ITEMS
// ---------------------------------------------------------------------------

int NativeMods_ItemsActive(void)
{
	return NativeMods_Active(NATIVE_MODS_ITEMS);
}

int NativeMods_ItemWeight(int item)
{
	const struct NativeModsPlay *play = NativeMods_Play();

	if ((item < 0) || (item >= NATIVE_MODS_ITEM_IDS))
	{
		return 0;
	}

	switch (play->value[NATIVE_MODS_ITEMS])
	{
	case NATIVE_MODS_ITEMS_CHAOS:
		return 1;
	case NATIVE_MODS_ITEMS_CUSTOM:
		return s_modsLevelWeight[play->level[item]];
	default:
		return 0;
	}
}

int NativeMods_CpuWeaponWeight(int weapon)
{
	if (NativeMods_Value(NATIVE_MODS_ITEMS) == NATIVE_MODS_ITEMS_CHAOS)
	{
		return 1;
	}

	switch (weapon)
	{
	case HELD_ITEM_BOMB_1X:
		return NativeMods_ItemWeight(HELD_ITEM_BOMB_1X) + NativeMods_ItemWeight(HELD_ITEM_BOMB_3X);
	case HELD_ITEM_MISSILE_1X:
		return NativeMods_ItemWeight(HELD_ITEM_MISSILE_1X) + NativeMods_ItemWeight(HELD_ITEM_MISSILE_3X);
	default:
		return NativeMods_ItemWeight(weapon);
	}
}

void NativeMods_NoteItem(int bot, int item)
{
	if ((item < 0) || (item >= NATIVE_MODS_REPORT_IDS) || (bot < 0) || (bot > 2))
	{
		return;
	}

	NativeMods_ArmReport();
	s_modsItems[bot][item]++;
}

// ---------------------------------------------------------------------------
// DIFFICULTY
// ---------------------------------------------------------------------------

s16 NativeMods_BotDifficulty(s16 difficulty, int boss)
{
	const struct NativeModsPlay *play = NativeMods_Play();
	int value = difficulty;

	if (!NativeMods_RaceOk())
	{
		return difficulty;
	}

	if (!boss && (play->base >= 0))
	{
		value = play->base;
	}

	if (play->value[NATIVE_MODS_DIFFICULTY] != NATIVE_MODS_DIFF_NORMAL)
	{
		value += s_modsDiffMove[play->value[NATIVE_MODS_DIFFICULTY]];
		value = (value < NATIVE_MODS_DIFF_LOW) ? NATIVE_MODS_DIFF_LOW : ((value > NATIVE_MODS_DIFF_HIGH) ? NATIVE_MODS_DIFF_HIGH : value);
	}

	return (s16)value;
}

// ---------------------------------------------------------------------------
// BOSS
// ---------------------------------------------------------------------------

// Per boss index (gGT->bossID's numbering): the character, the weapon table
// (data.bossWeaponMetaPtr is by hub), his home track and its checkpoints -
// cnt_restart_points of the 1P LEVs of the disc (Level+0x148), read from
// BIGFILE.BIG: Roo's Tubes 78, Papu's Pyramid 102, Dragon Mines 84, Hot Air
// Skyway 175, Oxide Station 190. The highest section start of each table lies
// below its track's count (0x46, 0x5d, 0x46, 0x46, 0x7f).
#define NATIVE_MODS_BOSSES 5
internal const struct
{
	int character;
	int hub;
	int home;
	int homeCheckpoints;
	const char *name;
} s_modsBosses[NATIVE_MODS_BOSSES] = {
    {RIPPER_ROO, 1, ROO_TUBES, 78, "roo"},          {PAPU_PAPU, 2, PAPU_PYRAMID, 102, "papu"},       {KOMODO_JOE, 3, DRAGON_MINES, 84, "joe"},
    {PINSTRIPE, 4, HOT_AIR_SKYWAY, 175, "pinstripe"}, {NITROS_OXIDE, 0, OXIDE_STATION, 190, "oxide"},
};

int NativeMods_BossIndex(void)
{
	return NativeMods_Active(NATIVE_MODS_BOSS) ? s_modsBossIndex : -1;
}

// THE WEAPON PATTERN ON ANOTHER TRACK. A table is a list of sections, each
// starting at a checkpoint of the boss's home track (trackCheckpoint, an
// index into ptr_restart_points), with what he throws there. Here every start
// is moved to the same share of this track's checkpoints (start * count /
// home count, at most count - 1), so the pattern keeps its order and its
// shares of the lap; weapon, throw, cooldown and juice stay. The table is a
// copy - PickupBots changes throwFlag and juiceFlag while it runs, and the
// adventure's own tables stay untouched. Without three nav paths of more than
// one point the boss keeps his path (pathChangeDisabled).
#define NATIVE_MODS_BOSS_SECTIONS 16
global_variable struct MetaDataBOSS s_modsBossTable[NATIVE_MODS_BOSS_SECTIONS];

internal void NativeMods_MakeBossTable(int index, int checkpoints, int paths)
{
	const struct MetaDataBOSS *source = data.bossWeaponMetaPtr[s_modsBosses[index].hub];
	int k;

	memset(s_modsBossTable, 0, sizeof(s_modsBossTable));
	for (k = 0; (k < (NATIVE_MODS_BOSS_SECTIONS - 1)) && (source[k].throwFlag != 0); k++)
	{
		int start = (checkpoints > 0) ? ((int)source[k].trackCheckpoint * checkpoints) / s_modsBosses[index].homeCheckpoints : 0;

		s_modsBossTable[k] = source[k];
		s_modsBossTable[k].trackCheckpoint = (u8)((start >= checkpoints) ? ((checkpoints > 0) ? (checkpoints - 1) : 0) : start);
		if (paths < 3)
		{
			s_modsBossTable[k].pathChangeDisabled = 1;
		}
	}
}

struct MetaDataBOSS *NativeMods_BossWeapons(int rebuild)
{
	if (rebuild && (s_modsBossIndex >= 0))
	{
		const struct GameTracker *gGT = sdata->gGT;
		const int checkpoints = ((gGT->level1 != NULL) && (gGT->level1->ptr_restart_points != NULL)) ? gGT->level1->cnt_restart_points : 0;
		int paths = 0;
		int k;

		for (k = 0; k < 3; k++)
		{
			paths += (sdata->NavPath_ptrHeader[k] != NULL) && (sdata->NavPath_ptrHeader[k]->numPoints > 1);
		}

		NativeMods_MakeBossTable(s_modsBossIndex, checkpoints, paths);
		Platform_Log("[CTR Mods] boss %s: his weapon sections spread over the %d checkpoint(s) of level %d (home %d)%s\n",
		             s_modsBosses[s_modsBossIndex].name, checkpoints, (int)gGT->levelID, s_modsBosses[s_modsBossIndex].homeCheckpoints,
		             (paths < 3) ? ", no path changes (fewer than three nav paths)" : "");
	}

	return &s_modsBossTable[0];
}

// ---------------------------------------------------------------------------
// THE RACE FRAME, THE LAPS, THE RACE LINE
// ---------------------------------------------------------------------------

global_variable int s_modsRaceFrames;
global_variable int s_modsRaceLoads;
global_variable int s_modsBossMissingSaid;

// The next race load: the next set of --dev-mods-play, the frame count anew.
internal void NativeMods_NextRace(void)
{
	s_modsRaceLoads++;
	s_modsRaceFrames = 0;
	s_modsBossMissingSaid = 0;

	if (s_modsPlayDevCount > 0)
	{
		s_modsPlayDevIndex = (s_modsPlayDevRaces < s_modsPlayDevCount) ? s_modsPlayDevRaces : (s_modsPlayDevCount - 1);
		s_modsPlayDevRaces++;
		Platform_Log("[CTR Mods] --dev-mods-play: race load %d takes set %d of %d\n", s_modsRaceLoads, s_modsPlayDevIndex + 1, s_modsPlayDevCount);
	}
}

void Platform_ShotRequest(const char *path); // platform/native_platform.c
extern char g_cfg_shotName[PLATFORM_ARG_PATH_MAX];

void NativeMods_RaceFrame(struct GameTracker *gGT)
{
	const int inRace = (gGT != NULL) && ((gGT->gameMode1 & (MAIN_MENU | LOADING)) == 0) && (sdata->Loading.stage == LOAD_IDLE);

	if (!inRace || !NativeMods_RaceAllowed(gGT))
	{
		return;
	}

	if (NativeMods_Active(NATIVE_MODS_NIGHT) && (gGT->numPlyrCurrGame == 1) && ((gGT->renderFlags & RENDER_FLAG_DRAW_LEVEL) != 0) &&
	    (gGT->level1 != NULL))
	{
		NativeMods_NightQuad(gGT);
	}

	// A boss race whose boss found no nav path (BOTS_Driver_Init): said on
	// the screen for five seconds and once in the log, the race goes on alone.
	if ((s_modsBossIndex >= 0) && (gGT->drivers[1] == NULL) && (gGT->elapsedEventTime < (5 * 960)))
	{
		DecalFont_DrawLine("BOSS CHALLENGE: NO CPU PATH", 256, 80, FONT_SMALL, (JUSTIFY_CENTER | ORANGE));
		DecalFont_DrawLine("ON THIS TRACK - NO BOSS", 256, 90, FONT_SMALL, (JUSTIFY_CENTER | ORANGE));
		if (!s_modsBossMissingSaid)
		{
			s_modsBossMissingSaid = 1;
			Platform_LogWarn("[CTR Mods] boss %s: level %d has no nav path a CPU driver can drive - the race runs without the boss\n",
			                 s_modsBosses[s_modsBossIndex].name, (int)gGT->levelID);
		}
	}

	if ((gGT->gameMode1 & PAUSE_ALL) == 0)
	{
		s_modsRaceFrames++;
	}

	if ((s_modsPlayDevCount > 0) && (NativeMods_Play()->shot > 0) && (s_modsRaceFrames == NativeMods_Play()->shot))
	{
		char base[PLATFORM_ARG_PATH_MAX];
		char path[PLATFORM_ARG_PATH_MAX + 32];
		size_t length;

		snprintf(base, sizeof(base), "%s", (g_cfg_shotName[0] != '\0') ? g_cfg_shotName : "mods");
		length = strlen(base);
		if ((length > 4) && ((strcmp(&base[length - 4], ".bmp") == 0) || (strcmp(&base[length - 4], ".BMP") == 0)))
		{
			base[length - 4] = '\0';
		}
		snprintf(path, sizeof(path), "%s-race%02d.bmp", base, s_modsRaceLoads);
		Platform_ShotRequest(path);
		Platform_Log("[CTR Mods] race load %d, level %d, race frame %d: snapshot %s\n", s_modsRaceLoads, (int)gGT->levelID, s_modsRaceFrames, path);
	}
}

void NativeMods_NoteLap(const struct Driver *driver, int lapIndex, int ticks)
{
	const int ms = (int)(((long long)ticks * 1000) / 960);

	if ((s_modsPlayDevCount == 0) || (driver == NULL) || !NativeMods_RaceAllowed(sdata->gGT))
	{
		return;
	}

	NativeMods_ArmReport();
	s_modsLaps++;
	Platform_Log("[CTR Mods] lap: race load %d, level %d, difficulty %s, driver %d (%s, %s), lap %d: %d.%03d s\n", s_modsRaceLoads,
	             (int)sdata->gGT->levelID, s_modsDiffText[NativeMods_Value(NATIVE_MODS_DIFFICULTY)], (int)driver->driverID,
	             data.MetaDataCharacters[(int)data.characterIDs[driver->driverID]].name_Debug, (driver->driverID == 0) ? "seat 0" : "cpu", lapIndex + 1,
	             ms / 1000, ms % 1000);
}

void NativeMods_NoteBotRescue(void)
{
	int k;

	for (k = 0; k < NATIVE_MODS_VALUES; k++)
	{
		if (NativeMods_Active(k))
		{
			NativeMods_ArmReport();
			s_modsBotRescues++;
			return;
		}
	}
}

void NativeMods_LogRace(void)
{
	char text[160] = "";
	int k;

	for (k = 0; k < NATIVE_MODS_VALUES; k++)
	{
		if (NativeMods_Active(k))
		{
			const char *shown = (k == NATIVE_MODS_BOSS) ? s_modsBosses[s_modsBossIndex].name : NativeMods_ValueText(k);

			snprintf(&text[strlen(text)], sizeof(text) - strlen(text), "%s%s %s", (text[0] != '\0') ? ", " : "", s_modsRange[k].key, shown);
		}
	}

	if (text[0] != '\0')
	{
		Platform_Log("[CTR Mods] play mods in this race: %s\n", text);
	}
}

// ---------------------------------------------------------------------------
// --dev-mods-play
// ---------------------------------------------------------------------------

internal int NativeMods_Word(const char *word, const char *const *names, int count)
{
	int k;

	for (k = 0; k < count; k++)
	{
		if (SDL_strcasecmp(word, names[k]) == 0)
		{
			return k;
		}
	}

	return -1;
}

internal int NativeMods_Number(const char *text, int *value)
{
	if ((text[0] == '\0') || ((text[0] != '-') && ((text[0] < '0') || (text[0] > '9'))) || (strspn(&text[1], "0123456789") != strlen(&text[1])))
	{
		return 0;
	}

	*value = atoi(text);
	return 1;
}

// One set: "none", or words of the forms above. 0 for another form.
internal int NativeMods_DevSet(char *text, struct NativeModsPlay *play)
{
	char *word;
	char *next;

	NativeMods_PlayDefaults(play);
	if (strcmp(text, "none") == 0)
	{
		return 1;
	}

	for (word = text; word != NULL; word = next)
	{
		char *equals;
		int value;
		int k;

		next = strchr(word, ',');
		if (next != NULL)
		{
			*next++ = '\0';
		}

		if (strcmp(word, "moon") == 0)
		{
			play->value[NATIVE_MODS_GRAVITY] = NATIVE_MODS_LOW_GRAVITY;
			continue;
		}
		if (strcmp(word, "icy") == 0)
		{
			play->value[NATIVE_MODS_GRIP] = 0;
			continue;
		}
		if (strcmp(word, "chaos") == 0)
		{
			play->value[NATIVE_MODS_ITEMS] = NATIVE_MODS_ITEMS_CHAOS;
			continue;
		}

		equals = strchr(word, '=');
		if ((equals == NULL) || (equals == word))
		{
			return 0;
		}
		*equals++ = '\0';

		if ((word[0] == 'w') && NativeMods_Number(&word[1], &k))
		{
			if ((k < 0) || (k >= NATIVE_MODS_ITEM_IDS) || !NativeMods_Number(equals, &value) || (value < 0) || (value > NATIVE_MODS_WEIGHT_HIGH))
			{
				return 0;
			}
			play->level[k] = (u8)value;
			continue;
		}
		if (strcmp(word, "base") == 0)
		{
			if (!NativeMods_Number(equals, &value) || (value < 0) || (value > 0x140))
			{
				return 0;
			}
			play->base = value;
			continue;
		}
		if (strcmp(word, "shot") == 0)
		{
			if (!NativeMods_Number(equals, &value) || (value < 1))
			{
				return 0;
			}
			play->shot = value;
			continue;
		}

		for (k = 0; k < NATIVE_MODS_VALUES; k++)
		{
			if (strcmp(word, s_modsRange[k].key) == 0)
			{
				break;
			}
		}

		if (k == NATIVE_MODS_VALUES)
		{
			return 0;
		}

		if (s_modsRange[k].percent)
		{
			if (!NativeMods_Number(equals, &value) || !NativeMods_InRange(k, value))
			{
				return 0;
			}
		}
		else
		{
			value = (k == NATIVE_MODS_ITEMS) ? NativeMods_Word(equals, s_modsItemsText, 3)
			                                 : ((k == NATIVE_MODS_DIFFICULTY) ? NativeMods_Word(equals, s_modsDiffText, 4) : NativeMods_Word(equals, s_modsBossText, 7));
			if (value < 0)
			{
				return 0;
			}
		}
		play->value[k] = value;
	}

	return 1;
}

int NativeMods_SetDevPlay(const char *list)
{
	static struct NativeModsPlay sets[NATIVE_MODS_DEV_SETS];
	char copy[1024];
	char *set;
	char *next;
	int count = 0;
	int k;

	if ((list == NULL) || (list[0] == '\0') || (strlen(list) >= sizeof(copy)))
	{
		return 0;
	}

	snprintf(copy, sizeof(copy), "%s", list);
	for (set = copy; set != NULL; set = next)
	{
		next = strchr(set, '/');
		if (next != NULL)
		{
			*next++ = '\0';
		}

		if ((count >= NATIVE_MODS_DEV_SETS) || (set[0] == '\0') || !NativeMods_DevSet(set, &sets[count]))
		{
			return 0;
		}
		count++;
	}

	memcpy(s_modsPlayDev, sets, sizeof(s_modsPlayDev));
	s_modsPlayDevCount = count;
	s_modsPlayDevIndex = 0;
	s_modsPlayDevRaces = 0;

	for (k = 0; k < count; k++)
	{
		const struct NativeModsPlay *play = &s_modsPlayDev[k];

		Platform_Log("[CTR Mods] --dev-mods-play set %d: gravity %d %%, grip %d %%, night %d %%, items %s, difficulty %s, boss %s%s (this run only)\n", k + 1,
		             play->value[NATIVE_MODS_GRAVITY], play->value[NATIVE_MODS_GRIP], play->value[NATIVE_MODS_NIGHT],
		             s_modsItemsText[play->value[NATIVE_MODS_ITEMS]], s_modsDiffText[play->value[NATIVE_MODS_DIFFICULTY]],
		             s_modsBossText[play->value[NATIVE_MODS_BOSS]], (play->base >= 0) ? ", measuring base set" : "");
	}
	return 1;
}

// ---------------------------------------------------------------------------
// THE SELF-TEST (--mods-selftest)
// ---------------------------------------------------------------------------

int VehPhysGeneral_ChaosSelfTest(int *checks);                      // game/Vehicle/VehPhysGeneral.c
int VehPhysGeneral_ItemsBoxSelfTest(int mode, const char *what, int *checks); // game/Vehicle/VehPhysGeneral.c
int PickupBots_WeaponSelfTest(int *checks);                         // game/PickupBots.c

#define NATIVE_MODS_EXPECT(cond, ...)                                                                                                                       \
	do                                                                                                                                                     \
	{                                                                                                                                                      \
		checks++;                                                                                                                                          \
		if (!(cond))                                                                                                                                       \
		{                                                                                                                                                  \
			printf("mods selftest: " __VA_ARGS__);                                                                                                         \
			printf("\n");                                                                                                                                  \
			failures++;                                                                                                                                    \
		}                                                                                                                                                  \
	} while (0)

int NativeMods_SelfTest(void)
{
	static const struct
	{
		int cpu;
		u32 retail;
		const char *dev;
		const char *status;
	} statusCases[] = {
	    {NATIVE_MODS_CPU_DEFAULT, 0u, "none", "OFF"},
	    {NATIVE_MODS_CPU_ALL, 0u, "none", "ALL RANDOM"},
	    {NATIVE_MODS_CPU_SELECTED, 0u, "none", "OFF"},
	    {NATIVE_MODS_CPU_SELECTED, 0x7u, "none", "3 SELECTED"},
	    {NATIVE_MODS_CPU_DEFAULT, 0u, "grip=30", "1 MOD"},
	    {NATIVE_MODS_CPU_DEFAULT, 0u, "gravity=60,grip=0,night=40", "3 MODS"},
	    {NATIVE_MODS_CPU_SELECTED, 0u, "gravity=41,items=chaos", "2 MODS"},
	    {NATIVE_MODS_CPU_ALL, 0u, "gravity=200,difficulty=hard", "RANDOM +2"},
	    {NATIVE_MODS_CPU_SELECTED, 0x7u, "items=chaos", "3 SEL +1"},
	    {NATIVE_MODS_CPU_SELECTED, 0x7fffu, "gravity=20,grip=50,night=90,items=custom,difficulty=easy", "15 SEL +5"},
	    {NATIVE_MODS_CPU_DEFAULT, 0u, "items=custom", "1 MOD"},
	    {NATIVE_MODS_CPU_DEFAULT, 0u, "items=custom,w0=0,w1=0,w2=0,w3=0,w4=0,w6=0,w7=0,w8=0,w9=0,w10=0,w11=0", "OFF"},
	    {NATIVE_MODS_CPU_DEFAULT, 0u, "gravity=100,grip=100,night=0,items=default,difficulty=normal", "OFF"},
	    {NATIVE_MODS_CPU_DEFAULT, 0u, "boss=roo", "BOSS: ROO"},
	    {NATIVE_MODS_CPU_ALL, 0u, "boss=oxide", "BOSS: OXIDE"},
	    {NATIVE_MODS_CPU_DEFAULT, 0u, "boss=pinstripe", "PINSTRIPE"},
	    {NATIVE_MODS_CPU_SELECTED, 0x7u, "boss=random", "RANDOM BOSS"},
	    {NATIVE_MODS_CPU_DEFAULT, 0u, "boss=papu,night=50,grip=0", "BOSS +2"},
	};
	static const struct
	{
		const char *line;
		int known;
		int gravity;
		int grip;
		int night;
		int items;
	} lineCases[] = {
	    {"moon 1", 1, 41, 100, 0, 0},        {"icy 1", 1, 41, 0, 0, 0},           {"chaos 1", 1, 41, 0, 0, 1},       {"icy 0", 1, 41, 100, 0, 1},
	    {"moon 2", 1, 100, 100, 0, 1},       {"moon x", 0, 100, 100, 0, 1},       {"chaos 0", 1, 100, 100, 0, 0},    {"gravity 70", 1, 70, 100, 0, 0},
	    {"gravity 210", 1, 100, 100, 0, 0},  {"gravity 20", 1, 20, 100, 0, 0},    {"grip 40", 1, 20, 40, 0, 0},      {"grip 101", 1, 20, 100, 0, 0},
	    {"night 90", 1, 20, 100, 90, 0},     {"night 95", 1, 20, 100, 0, 0},      {"items 2", 1, 20, 100, 0, 2},     {"items 3", 1, 20, 100, 0, 0},
	};
	static const struct
	{
		const char *list;
		int ok;
		int sets;
		int gravity;
		int grip;
		int items;
		int boss;
	} devCases[] = {
	    {"none", 1, 1, 100, 100, 0, 0},
	    {"moon", 1, 1, 41, 100, 0, 0},
	    {"icy,chaos", 1, 1, 100, 0, 1, 0},
	    {"gravity=20,grip=30,items=custom,boss=joe", 1, 1, 20, 30, 2, 4},
	    {"none/gravity=200/boss=random", 1, 3, 100, 100, 0, 0},
	    {"", 0, 0, 0, 0, 0, 0},
	    {"moon,", 0, 0, 0, 0, 0, 0},
	    {"gravity", 0, 0, 0, 0, 0, 0},
	    {"gravity=19", 0, 0, 0, 0, 0, 0},
	    {"grip=101", 0, 0, 0, 0, 0, 0},
	    {"night=91", 0, 0, 0, 0, 0, 0},
	    {"boss=crash", 0, 0, 0, 0, 0, 0},
	    {"w16=1", 0, 0, 0, 0, 0, 0},
	    {"none//none", 0, 0, 0, 0, 0, 0},
	};
	static const struct
	{
		int which;
		int from;
		int dir;
		int to;
	} stepCases[] = {
	    {NATIVE_MODS_GRAVITY, 100, 1, 110}, {NATIVE_MODS_GRAVITY, 100, -1, 90}, {NATIVE_MODS_GRAVITY, 41, 1, 50}, {NATIVE_MODS_GRAVITY, 41, -1, 40},
	    {NATIVE_MODS_GRAVITY, 20, -1, 20},  {NATIVE_MODS_GRAVITY, 200, 1, 200}, {NATIVE_MODS_GRIP, 0, -1, 0},     {NATIVE_MODS_GRIP, 0, 1, 10},
	    {NATIVE_MODS_GRIP, 100, 1, 100},    {NATIVE_MODS_NIGHT, 90, 1, 90},     {NATIVE_MODS_NIGHT, 0, 1, 10},    {NATIVE_MODS_BOSS, 6, 1, 6},
	    {NATIVE_MODS_BOSS, 0, 1, 1},        {NATIVE_MODS_DIFFICULTY, 0, -1, 0}, {NATIVE_MODS_ITEMS, 2, 1, 2},
	};
	static const struct
	{
		int gravity;
		int meter;
		int hang;
	} hangCases[] = {
	    {41, 0, 0}, {41, 1560, 639}, {41, 1561, 640}, {41, 2342, 960}, {41, 0x960, 984}, {20, 3200, 640}, {200, 320, 640}, {200, 720, 1440}, {100, 1000, 1000},
	};
	const int savedCpu = s_modsCpu;
	const u32 savedRetail = s_modsRetailOn;
	const struct NativeModsPlay savedFile = *NativeMods_FilePlay();
	const int savedDevCount = s_modsPlayDevCount;
	const int savedCpuDev = s_modsDevGiven;
	struct NativeModsPlay savedDev[NATIVE_MODS_DEV_SETS];
	int checks = 0;
	int failures = 0;
	size_t k;
	int t;

	memcpy(savedDev, s_modsPlayDev, sizeof(savedDev));
	s_modsDevGiven = 0;

	// THE STATUS LINE: the values as --dev-mods-play gives them.
	for (k = 0; k < sizeof(statusCases) / sizeof(statusCases[0]); k++)
	{
		const char *status;

		s_modsCpu = statusCases[k].cpu;
		s_modsRetailOn = statusCases[k].retail;
		NativeMods_SetDevPlay(statusCases[k].dev);
		status = NativeMods_StatusText();
		NATIVE_MODS_EXPECT((strcmp(status, statusCases[k].status) == 0) && (strlen(status) <= 11u), "status case %d is \"%s\", wanted \"%s\"", (int)k, status,
		                   statusCases[k].status);
	}
	s_modsPlayDevCount = 0;

	// THE SETTINGS LINES, round 1's carried over.
	NativeMods_PlayDefaults(NativeMods_FilePlay());
	for (k = 0; k < sizeof(lineCases) / sizeof(lineCases[0]); k++)
	{
		const int known = NativeMods_LoadLine(lineCases[k].line);
		const struct NativeModsPlay *play = &s_modsPlayFile;

		NATIVE_MODS_EXPECT((known == lineCases[k].known) && (play->value[NATIVE_MODS_GRAVITY] == lineCases[k].gravity) &&
		                       (play->value[NATIVE_MODS_GRIP] == lineCases[k].grip) && (play->value[NATIVE_MODS_NIGHT] == lineCases[k].night) &&
		                       (play->value[NATIVE_MODS_ITEMS] == lineCases[k].items),
		                   "line \"%s\" gives known %d, gravity %d grip %d night %d items %d", lineCases[k].line, known, play->value[NATIVE_MODS_GRAVITY],
		                   play->value[NATIVE_MODS_GRIP], play->value[NATIVE_MODS_NIGHT], play->value[NATIVE_MODS_ITEMS]);
	}
	NATIVE_MODS_EXPECT(NativeMods_LoadLine("weight 7 0") && (s_modsPlayFile.level[7] == 0), "line \"weight 7 0\"");
	NATIVE_MODS_EXPECT(NativeMods_LoadLine("weight 7 9") && (s_modsPlayFile.level[7] == NATIVE_MODS_WEIGHT_NORMAL), "line \"weight 7 9\"");
	NATIVE_MODS_EXPECT(NativeMods_LoadLine("difficulty 3") && (s_modsPlayFile.value[NATIVE_MODS_DIFFICULTY] == 3), "line \"difficulty 3\"");
	NATIVE_MODS_EXPECT(NativeMods_LoadLine("boss 7") && (s_modsPlayFile.value[NATIVE_MODS_BOSS] == 0), "line \"boss 7\"");
	NativeMods_PlayDefaults(NativeMods_FilePlay());

	// --dev-mods-play.
	for (k = 0; k < sizeof(devCases) / sizeof(devCases[0]); k++)
	{
		int ok;

		s_modsPlayDevCount = 0;
		ok = NativeMods_SetDevPlay(devCases[k].list);
		NATIVE_MODS_EXPECT((ok == devCases[k].ok) && (!ok || ((s_modsPlayDevCount == devCases[k].sets) &&
		                                                      (s_modsPlayDev[0].value[NATIVE_MODS_GRAVITY] == devCases[k].gravity) &&
		                                                      (s_modsPlayDev[0].value[NATIVE_MODS_GRIP] == devCases[k].grip) &&
		                                                      (s_modsPlayDev[0].value[NATIVE_MODS_ITEMS] == devCases[k].items) &&
		                                                      (s_modsPlayDev[0].value[NATIVE_MODS_BOSS] == devCases[k].boss))),
		                   "--dev-mods-play \"%s\" gives %d with %d set(s)", devCases[k].list, ok, s_modsPlayDevCount);
	}

	// One set per race load, the last one stays.
	NativeMods_SetDevPlay("none/gravity=200/night=30");
	for (t = 0; t < 4; t++)
	{
		static const int wanted[4] = {100, 200, 100, 100};

		NativeMods_NextRace();
		NATIVE_MODS_EXPECT(NativeMods_Value(NATIVE_MODS_GRAVITY) == wanted[t], "race load %d has gravity %d", t + 1, NativeMods_Value(NATIVE_MODS_GRAVITY));
	}
	NATIVE_MODS_EXPECT(NativeMods_Value(NATIVE_MODS_NIGHT) == 30, "the last set stays");

	// THE STEPS of the page.
	NativeMods_SetDevPlay("none");
	for (k = 0; k < sizeof(stepCases) / sizeof(stepCases[0]); k++)
	{
		int changed;

		s_modsPlayDev[0].value[stepCases[k].which] = stepCases[k].from;
		changed = NativeMods_Step(stepCases[k].which, stepCases[k].dir);
		NATIVE_MODS_EXPECT((NativeMods_Value(stepCases[k].which) == stepCases[k].to) && (changed == (stepCases[k].from != stepCases[k].to)),
		                   "step %d of %s from %d gives %d", stepCases[k].dir, s_modsRange[stepCases[k].which].key, stepCases[k].from,
		                   NativeMods_Value(stepCases[k].which));
	}

	s_modsSelfTestRace = 1;

	// GRAVITY: normal, the same share everywhere; on, the mod's share, on a
	// low-gravity stretch the lower one - the limits 20 and 200 included.
	for (t = 20; t <= 200; t += 10)
	{
		s_modsPlayDev[0].value[NATIVE_MODS_GRAVITY] = t;
		NATIVE_MODS_EXPECT((NativeMods_GravityPercent(100) == t) && (NativeMods_GravityPercent(NATIVE_MODS_LOW_GRAVITY) == ((t < 41) ? t : 41)),
		                   "gravity %d gives %d and %d on low gravity", t, NativeMods_GravityPercent(100), NativeMods_GravityPercent(NATIVE_MODS_LOW_GRAVITY));
	}
	s_modsPlayDev[0].value[NATIVE_MODS_GRAVITY] = 100;
	NATIVE_MODS_EXPECT((NativeMods_GravityPercent(100) == 100) && (NativeMods_GravityPercent(41) == 41), "gravity 100 changes nothing");

	// THE HANG TIME, without a recorded flight: the air time times the share.
	for (k = 0; k < sizeof(hangCases) / sizeof(hangCases[0]); k++)
	{
		s_modsPlayDev[0].value[NATIVE_MODS_GRAVITY] = hangCases[k].gravity;
		NATIVE_MODS_EXPECT(NativeMods_HangTime(-1, hangCases[k].meter) == hangCases[k].hang, "air time %d at gravity %d judged as %d, wanted %d",
		                   hangCases[k].meter, hangCases[k].gravity, NativeMods_HangTime(-1, hangCases[k].meter), hangCases[k].hang);
	}

	// A recorded flight: 3513 ms in the air at 41 %, the meter stopped at
	// 2400 - judged as 1440, the large step; once only.
	s_modsPlayDev[0].value[NATIVE_MODS_GRAVITY] = 41;
	NativeMods_NoteGravity(3, 41, 100, 33, 0);
	NativeMods_NoteGravity(3, 41, 100, 33, 1);
	NativeMods_AirTime(3, 3513);
	NATIVE_MODS_EXPECT(NativeMods_HangTime(3, 0x960) == 1440, "a flight past the meter is judged as %d", NativeMods_HangTime(3, 0x960));
	NATIVE_MODS_EXPECT(NativeMods_HangTime(3, 0x960) == 984, "the second landing keeps the meter");

	// A flight partly over a low-gravity stretch at 200 %: 30 ms at 200 of the
	// game's 100, 73 ms at 41 of the game's 41 - weighed by the time at each.
	s_modsPlayDev[0].value[NATIVE_MODS_GRAVITY] = 200;
	NativeMods_NoteGravity(5, 200, 100, 0, 0);
	NativeMods_NoteGravity(5, 200, 100, 30, 1);
	NativeMods_NoteGravity(5, 41, 41, 73, 1);
	NATIVE_MODS_EXPECT(NativeMods_HangTime(5, 1000) == (int)((1000LL * (200 * 30 + 41 * 73)) / (100 * 30 + 41 * 73)),
	                   "a flight over a low-gravity stretch at 200 %% is judged as %d", NativeMods_HangTime(5, 1000));
	s_modsPlayDev[0].value[NATIVE_MODS_GRAVITY] = 100;
	NativeMods_NoteGravity(5, 100, 100, 30, 1);
	NATIVE_MODS_EXPECT(NativeMods_HangTime(5, 1234) == 1234, "gravity 100 judges the meter itself");

	// GRIP: 100 changes nothing; 0 is ice for road terrain only; in between a
	// blend from road to ice, the right share and side of 50 % for the flags.
	for (t = 0; t <= TERRAIN_SLOWDIRT; t++)
	{
		struct Terrain *meta = &data.MetaDataTerrain[t];
		const int road = NativeMods_RoadTerrain(t);

		s_modsPlayDev[0].value[NATIVE_MODS_GRIP] = 100;
		NATIVE_MODS_EXPECT((NativeMods_IcyTerrain((u8)t, 0) == (u8)t) && (NativeMods_GripTerrain((u8)t, meta, 0) == meta), "grip 100 moves terrain %d", t);
		s_modsPlayDev[0].value[NATIVE_MODS_GRIP] = 0;
		NATIVE_MODS_EXPECT((NativeMods_IcyTerrain((u8)t, 1) == (road ? (u8)TERRAIN_ICE : (u8)t)) && (NativeMods_GripTerrain((u8)t, meta, 1) == meta),
		                   "grip 0 gives terrain %d as %d", t, (int)NativeMods_IcyTerrain((u8)t, 1));
		s_modsPlayDev[0].value[NATIVE_MODS_GRIP] = 60;
		NATIVE_MODS_EXPECT((NativeMods_IcyTerrain((u8)t, 0) == (u8)t) && ((NativeMods_GripTerrain((u8)t, meta, 0) != meta) == road), "grip 60 on terrain %d",
		                   t);
	}
	{
		const struct Terrain *roadMeta = &data.MetaDataTerrain[TERRAIN_ASPHALT];
		const struct Terrain *ice = &data.MetaDataTerrain[TERRAIN_ICE];
		struct Terrain blend;

		for (t = 0; t <= 100; t += 10)
		{
			NativeMods_MakeGrip(&blend, roadMeta, ice, t);
			NATIVE_MODS_EXPECT((blend.groundFrictionScale ==
			                    roadMeta->groundFrictionScale + (((ice->groundFrictionScale - roadMeta->groundFrictionScale) * (100 - t)) / 100)) &&
			                       (blend.flags == ((t < 50) ? ice->flags : roadMeta->flags)) && (blend.skidSound == roadMeta->skidSound) &&
			                       (blend.em_OddFrame == roadMeta->em_OddFrame),
			                   "the grip blend at %d %%: friction %d flags 0x%x", t, blend.groundFrictionScale, (unsigned)blend.flags);
		}
		NativeMods_MakeGrip(&blend, roadMeta, ice, 100);
		NATIVE_MODS_EXPECT(memcmp(&blend, roadMeta, sizeof(blend)) == 0, "grip 100 %% is the road entry byte for byte");
		NativeMods_MakeGrip(&blend, roadMeta, ice, 0);
		NATIVE_MODS_EXPECT((blend.speedMultiplier == ice->speedMultiplier) && (blend.counterSteerRatio == ice->counterSteerRatio) &&
		                       (blend.groundFrictionScale == ice->groundFrictionScale) && (blend.botFrictionScale == ice->botFrictionScale),
		                   "grip 0 %% has the ice's numbers");
		NativeMods_MakeGrip(&blend, roadMeta, ice, 50);
		NATIVE_MODS_EXPECT((blend.groundFrictionScale == (roadMeta->groundFrictionScale + ice->groundFrictionScale) / 2) &&
		                       (blend.counterSteerRatio == (roadMeta->counterSteerRatio + ice->counterSteerRatio) / 2),
		                   "grip 50 %% lies half way: friction %d", blend.groundFrictionScale);
	}

	// NIGHT: off at 0; then darker with every step, blue the brightest, the
	// limit 90 %% still not black.
	{
		u8 rgb[3];
		int last = 256;

		NATIVE_MODS_EXPECT(NativeMods_NightColor(0, rgb) == 0, "night 0 %% draws nothing");
		for (t = 10; t <= 90; t += 10)
		{
			NATIVE_MODS_EXPECT(NativeMods_NightColor(t, rgb) && (rgb[2] < last) && (rgb[0] < rgb[2]) && (rgb[1] < rgb[2]) && (rgb[0] <= rgb[1]) &&
			                       (rgb[2] == ((255 * (100 - t)) + 50) / 100),
			                   "night %d %%: %d/%d/%d", t, rgb[0], rgb[1], rgb[2]);
			last = rgb[2];
		}
		NativeMods_NightColor(90, rgb);
		NATIVE_MODS_EXPECT((rgb[0] >= 15) && (rgb[2] == 26), "night 90 %%: %d/%d/%d", rgb[0], rgb[1], rgb[2]);
		printf("mods selftest: night colours 30 %% ");
		NativeMods_NightColor(30, rgb);
		printf("%d/%d/%d, 60 %% ", rgb[0], rgb[1], rgb[2]);
		NativeMods_NightColor(60, rgb);
		printf("%d/%d/%d, 90 %% ", rgb[0], rgb[1], rgb[2]);
		NativeMods_NightColor(90, rgb);
		printf("%d/%d/%d of 255\n", rgb[0], rgb[1], rgb[2]);
	}

	// DIFFICULTY: NORMAL keeps the game's value, the others move it in order,
	// within the range; the measuring base.
	for (t = 0; t <= 0x140; t += 0x50)
	{
		int last = -0x1000;
		int d;

		for (d = 0; d < 4; d++)
		{
			int value;

			s_modsPlayDev[0].value[NATIVE_MODS_DIFFICULTY] = d;
			value = NativeMods_BotDifficulty((s16)t, 0);
			NATIVE_MODS_EXPECT((value > last) || ((value == last) && ((value == NATIVE_MODS_DIFF_LOW) || (value == NATIVE_MODS_DIFF_HIGH))),
			                   "difficulty %s on %d gives %d", s_modsDiffText[d], t, value);
			NATIVE_MODS_EXPECT((d != NATIVE_MODS_DIFF_NORMAL) || (value == t), "NORMAL moves %d to %d", t, value);
			last = value;
		}
	}
	s_modsPlayDev[0].value[NATIVE_MODS_DIFFICULTY] = NATIVE_MODS_DIFF_EXTREME;
	s_modsPlayDev[0].base = 160;
	NATIVE_MODS_EXPECT((NativeMods_BotDifficulty(0, 0) == 0x140) && (NativeMods_BotDifficulty(24, 1) == 24 + 0xA0), "the measuring base");
	s_modsPlayDev[0].base = -1;

	// BOSS: the sections of every boss spread over a track of 72 checkpoints
	// (Crash Cove) and of 190 - in order, inside, the first at 0; three paths
	// keep the path changes, fewer turn them off.
	for (t = 0; t < NATIVE_MODS_BOSSES; t++)
	{
		static const int counts[3] = {72, 190, 12};
		int c;

		for (c = 0; c < 3; c++)
		{
			int ordered = 1;
			int inside = 1;
			int sections = 0;
			int s;

			NativeMods_MakeBossTable(t, counts[c], (c == 2) ? 1 : 3);
			for (s = 0; s < NATIVE_MODS_BOSS_SECTIONS && (s_modsBossTable[s].throwFlag != 0); s++)
			{
				const struct MetaDataBOSS *source = &data.bossWeaponMetaPtr[s_modsBosses[t].hub][s];

				sections++;
				inside &= (s_modsBossTable[s].trackCheckpoint < counts[c]) && (s_modsBossTable[s].weaponType == source->weaponType) &&
				          (s_modsBossTable[s].weaponCooldown == source->weaponCooldown) &&
				          (s_modsBossTable[s].pathChangeDisabled == ((c == 2) ? 1 : source->pathChangeDisabled));
				ordered &= (s == 0) ? (s_modsBossTable[s].trackCheckpoint == 0) : (s_modsBossTable[s].trackCheckpoint >= s_modsBossTable[s - 1].trackCheckpoint);
			}
			NATIVE_MODS_EXPECT(ordered && inside && (sections >= 5) && (s_modsBossTable[sections].throwFlag == 0),
			                   "boss %s on %d checkpoints: %d section(s), ordered %d, inside %d", s_modsBosses[t].name, counts[c], sections, ordered, inside);
		}

		// On his home track the table is his own.
		NativeMods_MakeBossTable(t, s_modsBosses[t].homeCheckpoints, 3);
		NATIVE_MODS_EXPECT(s_modsBossTable[1].trackCheckpoint == data.bossWeaponMetaPtr[s_modsBosses[t].hub][1].trackCheckpoint,
		                   "boss %s keeps his sections at home", s_modsBosses[t].name);
	}

	// ITEMS: the chaos draw over a whole period, then the item box itself -
	// DEFAULT, CHAOS, a mixed CUSTOM and one item alone - and the CPU weapons.
	s_modsPlayDev[0].value[NATIVE_MODS_ITEMS] = NATIVE_MODS_ITEMS_CHAOS;
	failures += VehPhysGeneral_ChaosSelfTest(&checks);
	s_modsPlayDev[0].value[NATIVE_MODS_ITEMS] = NATIVE_MODS_ITEMS_DEFAULT;
	failures += VehPhysGeneral_ItemsBoxSelfTest(0, "DEFAULT", &checks);
	s_modsPlayDev[0].value[NATIVE_MODS_ITEMS] = NATIVE_MODS_ITEMS_CHAOS;
	failures += VehPhysGeneral_ItemsBoxSelfTest(1, "CHAOS", &checks);
	NativeMods_SetDevPlay("items=custom,w0=0,w1=3,w2=1,w3=0,w4=2,w6=0,w7=3,w8=0,w9=1,w10=0,w11=2");
	failures += VehPhysGeneral_ItemsBoxSelfTest(2, "CUSTOM mixed", &checks);
	NativeMods_SetDevPlay("items=custom,w0=0,w1=0,w2=0,w3=0,w4=0,w6=0,w7=0,w8=0,w9=1,w10=0,w11=0");
	failures += VehPhysGeneral_ItemsBoxSelfTest(3, "CUSTOM warpball alone", &checks);
	NativeMods_SetDevPlay("items=custom,w0=0,w1=0,w2=0,w3=0,w4=0,w6=0,w7=0,w8=0,w9=0,w10=0,w11=0");
	NATIVE_MODS_EXPECT(!NativeMods_ItemsActive() && NativeMods_ItemsAllOff(), "every item OFF is DEFAULT");
	failures += VehPhysGeneral_ItemsBoxSelfTest(0, "CUSTOM all off", &checks);
	NativeMods_SetDevPlay("items=custom,w0=3,w1=0,w2=0,w3=2,w4=1,w6=3,w7=3,w8=3,w9=3,w10=3,w11=1");
	failures += PickupBots_WeaponSelfTest(&checks);
	s_modsSelfTestRace = 0;

	s_modsCpu = savedCpu;
	s_modsRetailOn = savedRetail;
	s_modsPlayFile = savedFile;
	memcpy(s_modsPlayDev, savedDev, sizeof(savedDev));
	s_modsPlayDevCount = savedDevCount;
	s_modsPlayDevIndex = 0;
	s_modsPlayDevRaces = 0;
	s_modsRaceLoads = 0;
	s_modsDevGiven = savedCpuDev;

	printf("mods selftest %s: %d checks, %d failures\n", (failures == 0) ? "passed" : "FAILED", checks, failures);
	return (failures == 0) ? 0 : 1;
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
	// A GRIP blend entry is host memory a driver may point at, and so is the
	// boss's weapon table sdata->bossWeaponMeta points at.
	const int grip = NativeMods_Value(NATIVE_MODS_GRIP);

	return (s_modsModelCount > 0) || (NativeMods_Active(NATIVE_MODS_GRIP) && (grip > 0) && (grip < 100)) || (NativeMods_BossIndex() >= 0);
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

	if ((size < 12u) || (s_modsModelCount >= NATIVE_MODS_MODEL_MAX))
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

// The model of retail driver id as this plan sees it: read for it already, or
// read now. NULL when it cannot be read.
internal const struct Model *NativeMods_Model(struct BigHeader *bigfile, int id)
{
	const struct Model *model = NativeMods_ModelByName(data.MetaDataCharacters[id].name_Debug);

	return (model != NULL) ? model : NativeMods_ReadModel(bigfile, id);
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

// A retail id of the pack that no retail seat shows (a custom seat shows its
// file, not its template), and not seat 0's; -1 for none. With eight ids in
// the pack and at most seven other seats there is always one.
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

	Platform_Log("[CTR Mods] seats (%s, level %d, cpu drivers %s):%s\n", how, (int)sdata->gGT->levelID, NativeMods_ValueName(s_modsCpu), line);
}

// The pool of the value: retail ids (entry -1) and roster files (the id is
// the file's template), never the player's own (retail id player, file pick)
// and never a file marked unfit. The count.
internal int NativeMods_Pool(int player, int pick, const u8 *unfit, int poolId[NATIVE_MODS_POOL_MAX], int poolEntry[NATIVE_MODS_POOL_MAX])
{
	const int all = (s_modsCpu == NATIVE_MODS_CPU_ALL);
	int count = 0;
	int i;

	for (i = 0; i < NATIVE_MODS_RETAIL_COUNT; i++)
	{
		if ((i != player) && (all || NativeMods_RetailTicked(i)))
		{
			poolId[count] = i;
			poolEntry[count] = -1;
			count++;
		}
	}

	for (i = 0; (i < NativeChar_RosterFileCount()) && (count < NATIVE_MODS_POOL_MAX); i++)
	{
		if ((i != pick) && !unfit[i] && (all || NativeMods_FileTicked(i)))
		{
			poolId[count] = NativeChar_EntryTemplate(i);
			poolEntry[count] = i;
			count++;
		}
	}

	return count;
}

// The draw: the pool shuffled with the module's own numbers (Fisher-Yates),
// taken in that order onto the seats 1..7; a pool smaller than seven is
// shuffled again for the seats left. Nobody twice while the pool lasts.
internal void NativeMods_Draw(const int poolIdIn[NATIVE_MODS_POOL_MAX], const int poolEntryIn[NATIVE_MODS_POOL_MAX], int count)
{
	int poolId[NATIVE_MODS_POOL_MAX];
	int poolEntry[NATIVE_MODS_POOL_MAX];
	int seat = 1;
	int i;

	while (seat < NATIVE_MODS_SEATS)
	{
		memcpy(poolId, poolIdIn, sizeof(poolId));
		memcpy(poolEntry, poolEntryIn, sizeof(poolEntry));

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

		for (i = 0; (i < count) && (seat < NATIVE_MODS_SEATS); i++, seat++)
		{
			data.characterIDs[seat] = (s16)poolId[i];
			s_modsPlan.entry[seat] = poolEntry[i];
		}
	}
}

// BOSS: the boss alone on seat 1 - the page's boss, or with RANDOM one of the
// five drawn with the module's numbers (another than the player's own driver
// while there is one); the next races of a cup keep him. His model is read
// from the bigfile when the pack does not hold it, like a retail seat of the
// draw; without it the race has no boss challenge (said in the log) and its
// seats are the pack's. Seats 2..7 keep the pack's ids and stay empty (one
// bot, MainInit_Drivers).
internal void NativeMods_PlanBoss(struct BigHeader *bigfile)
{
	struct GameTracker *gGT = sdata->gGT;
	const int cup = ((gGT->gameMode2 & CUP_ANY_KIND) != 0);
	const int value = NativeMods_Value(NATIVE_MODS_BOSS);
	int pack[NATIVE_MODS_SEATS];
	int index;
	int seat;

	for (seat = 0; seat < NATIVE_MODS_SEATS; seat++)
	{
		pack[seat] = (int)data.characterIDs[seat];
	}

	if (cup && s_modsPlan.valid && s_modsPlan.cup && s_modsPlan.bossPlan && (s_modsPlan.player == pack[0]) && (s_modsPlan.bossValue == value))
	{
		index = s_modsPlan.boss;
		data.characterIDs[1] = (s16)s_modsBosses[index].character;
		s_modsBossIndex = index;
		Platform_Log("[CTR Mods] boss challenge: %s on level %d, the boss of the cup's first race\n", s_modsBosses[index].name, (int)gGT->levelID);
		return;
	}

	NativeMods_ReleaseModels();
	memset(&s_modsPlan, 0, sizeof(s_modsPlan));

	if (value == NATIVE_MODS_BOSS_RANDOM)
	{
		int pool[NATIVE_MODS_BOSSES];
		int count = 0;
		int k;

		for (k = 0; k < NATIVE_MODS_BOSSES; k++)
		{
			if (s_modsBosses[k].character != pack[0])
			{
				pool[count++] = k;
			}
		}
		index = pool[NativeMods_Below(count)];
	}
	else
	{
		index = value - NATIVE_MODS_BOSS_ROO;
	}

	if (!NativeMods_InPack(pack, s_modsBosses[index].character) &&
	    (NativeMods_ModelByName(data.MetaDataCharacters[s_modsBosses[index].character].name_Debug) == NULL) &&
	    (NativeMods_ReadModel(bigfile, s_modsBosses[index].character) == NULL))
	{
		Platform_LogWarn("[CTR Mods] boss %s has no model - the race runs without the boss challenge, with the pack's seats\n", s_modsBosses[index].name);
		return;
	}

	data.characterIDs[1] = (s16)s_modsBosses[index].character;
	s_modsPlan.valid = 1;
	s_modsPlan.cup = cup;
	memcpy(s_modsPlan.pack, pack, sizeof(pack));
	s_modsPlan.player = pack[0];
	s_modsPlan.pick = -1;
	for (seat = 0; seat < NATIVE_MODS_SEATS; seat++)
	{
		s_modsPlan.id[seat] = (int)data.characterIDs[seat];
		s_modsPlan.entry[seat] = -1;
	}
	s_modsPlan.bossPlan = 1;
	s_modsPlan.boss = index;
	s_modsPlan.bossValue = value;
	s_modsBossIndex = index;

	Platform_Log("[CTR Mods] boss challenge: %s on level %d, one against one (%s)\n", s_modsBosses[index].name, (int)gGT->levelID,
	             (value == NATIVE_MODS_BOSS_RANDOM) ? "drawn" : "chosen");
}

void NativeMods_PlanSeats(struct BigHeader *bigfile)
{
	struct GameTracker *gGT = sdata->gGT;
	int pack[NATIVE_MODS_SEATS];
	int poolId[NATIVE_MODS_POOL_MAX];
	int poolEntry[NATIVE_MODS_POOL_MAX];
	u8 unfit[NATIVE_CHAR_ROSTER_MAX];
	int pick = NativeChar_Pick();
	const int cup = ((gGT->gameMode2 & CUP_ANY_KIND) != 0);
	int count;
	int seat;
	int redraw;

	// Every race load: the next set of --dev-mods-play, and no boss until the
	// plan below gives one.
	NativeMods_NextRace();
	s_modsBossIndex = -1;

	if ((NativeMods_Value(NATIVE_MODS_BOSS) != NATIVE_MODS_BOSS_OFF) && NativeMods_RaceAllowed(gGT))
	{
		NativeMods_PlanBoss(bigfile);
		return;
	}

	// DEFAULT: nothing at all - not even the plan of an earlier race is
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
	if (cup && s_modsPlan.valid && s_modsPlan.cup && !s_modsPlan.bossPlan && (s_modsPlan.player == pack[0]) && (s_modsPlan.pick == pick))
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
	memset(unfit, 0, sizeof(unfit));
	s_modsDraws++;

	// Drawn until every drawn file whose template the pack does not hold sits
	// on it; a round that finds one that does not takes it out of the pool, so
	// this ends.
	do
	{
		count = NativeMods_Pool(pack[0], pick, unfit, poolId, poolEntry);
		if (count == 0)
		{
			for (seat = 1; seat < NATIVE_MODS_SEATS; seat++)
			{
				data.characterIDs[seat] = (s16)pack[seat];
			}

			Platform_Log("[CTR Mods] cpu drivers %s: no driver may drive (nothing ticked, only the player's own, or no file fits) - the retail "
			             "seats, as DEFAULT\n",
			             NativeMods_ValueName(s_modsCpu));
			NativeMods_ForgetPlan();
			return;
		}

		NativeMods_Draw(poolId, poolEntry, count);

		redraw = 0;
		for (seat = 1; seat < NATIVE_MODS_SEATS; seat++)
		{
			const int entry = s_modsPlan.entry[seat];
			const int templateId = (int)data.characterIDs[seat];

			if ((entry < 0) || unfit[entry] || NativeMods_InPack(pack, templateId))
			{
				continue;
			}

			if ((templateId < 0) || (templateId >= NATIVE_MODS_RETAIL_COUNT) || !NativeChar_FitsDonor(entry, NativeMods_Model(bigfile, templateId)))
			{
				Platform_LogWarn("[CTR Mods] %s cannot sit on its template %d (no model of it, or other frame counts) - left out, the seats are "
				                 "drawn again\n",
				                 NativeChar_EntryFile(entry), templateId);
				unfit[entry] = 1;
				redraw = 1;
			}
		}
	} while (redraw);

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

// ONLY SELECTED, load stage 5: a ticked retail driver, not the player's,
// whose model this load holds (the pack's or one read for the plan), drawn
// among them; -1 for none.
internal int NativeMods_TickedAtHand(void)
{
	int ids[NATIVE_MODS_RETAIL_COUNT];
	int count = 0;
	int i;

	for (i = 0; (s_modsCpu == NATIVE_MODS_CPU_SELECTED) && (i < NATIVE_MODS_RETAIL_COUNT); i++)
	{
		if ((i != s_modsPlan.player) && NativeMods_RetailTicked(i) &&
		    (NativeMods_InPack(s_modsPlan.pack, i) || (NativeMods_ModelByName(data.MetaDataCharacters[i].name_Debug) != NULL)))
		{
			ids[count++] = i;
		}
	}

	return (count > 0) ? ids[NativeMods_Below(count)] : -1;
}

// Load stage 5: a seat whose file could not be bound gets, with ONLY
// SELECTED, a ticked retail driver at hand (NativeMods_TickedAtHand), else a
// retail driver of the pack nobody shows (the pack is the one this load
// holds: seat 0's and LOAD_Robots1P's ids). Returns the id written.
int NativeMods_RetailFallback(int seat)
{
	int id;

	if (!s_modsPlan.valid || (seat < 1) || (seat >= NATIVE_MODS_SEATS))
	{
		return -1;
	}

	s_modsPlan.entry[seat] = -1;
	id = NativeMods_TickedAtHand();
	if (id < 0)
	{
		id = NativeMods_FreePackId(s_modsPlan.pack, seat);
	}
	if (id >= 0)
	{
		data.characterIDs[seat] = (s16)id;
		s_modsPlan.id[seat] = id;
	}

	return id;
}
