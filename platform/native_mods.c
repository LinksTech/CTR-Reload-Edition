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

// The play mods (NATIVE_MODS_PLAY_*): the page's and the file's value, and
// --dev-mods-play's for this run, which wins and is never saved.
global_variable int s_modsPlayFile;
global_variable int s_modsPlayDev;
global_variable int s_modsPlayDevGiven;

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

const char *NativeMods_StatusText(void)
{
	const int play = NativeMods_PlayCount();
	const int selected = (s_modsCpu == NATIVE_MODS_CPU_SELECTED) ? NativeMods_SelectedCount() : 0;

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
	fprintf(file, "mods moon %d\n", ((s_modsPlayFile & NATIVE_MODS_PLAY_MOON) != 0) ? 1 : 0);
	fprintf(file, "mods icy %d\n", ((s_modsPlayFile & NATIVE_MODS_PLAY_ICY) != 0) ? 1 : 0);
	fprintf(file, "mods chaos %d\n", ((s_modsPlayFile & NATIVE_MODS_PLAY_CHAOS) != 0) ? 1 : 0);
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
	// Any number but 1 is OFF, and a file without these lines keeps them OFF.
	{
		static const struct
		{
			const char *format;
			int bit;
		} play[3] = {{"moon %d", NATIVE_MODS_PLAY_MOON}, {"icy %d", NATIVE_MODS_PLAY_ICY}, {"chaos %d", NATIVE_MODS_PLAY_CHAOS}};
		int k;

		for (k = 0; k < 3; k++)
		{
			if (sscanf(rest, play[k].format, &value) == 1)
			{
				s_modsPlayFile = (value == 1) ? (s_modsPlayFile | play[k].bit) : (s_modsPlayFile & ~play[k].bit);
				return 1;
			}
		}
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
// THE PLAY MODS: MOON GRAVITY, ICY TRACKS, ITEM CHAOS (native_mods.h)
// ---------------------------------------------------------------------------

#define NATIVE_MODS_PLAY_ALL (NATIVE_MODS_PLAY_MOON | NATIVE_MODS_PLAY_ICY | NATIVE_MODS_PLAY_CHAOS)

// The hang-time turbo's air time at MOON GRAVITY: times 41/100, the factor of
// the gravity (VehPhysForce_OnGravity). A jump's air time is its upward speed
// over the gravity, twice; at 41/100 of the gravity it lasts 100/41 as long,
// and this gives it back the time it takes at full gravity.
#define NATIVE_MODS_MOON_NUM 41
#define NATIVE_MODS_MOON_DEN 100

// The report at exit: hang-time turbos by fire step (0..2, small medium
// large), as given and as the full air time would have given them; items by
// HELD_ITEM id, from boxes and from CPU attacks.
#define NATIVE_MODS_ITEM_IDS 0x20
global_variable int s_modsReportArmed;
global_variable int s_modsLandingGiven[3];
global_variable int s_modsLandingFull[3];
global_variable int s_modsLandings;
global_variable int s_modsItems[2][NATIVE_MODS_ITEM_IDS];

internal int NativeMods_PlayValue(void)
{
	return s_modsPlayDevGiven ? s_modsPlayDev : s_modsPlayFile;
}

internal int NativeMods_PlayCount(void)
{
	const int value = NativeMods_PlayValue();

	return (((value & NATIVE_MODS_PLAY_MOON) != 0) ? 1 : 0) + (((value & NATIVE_MODS_PLAY_ICY) != 0) ? 1 : 0) +
	       (((value & NATIVE_MODS_PLAY_CHAOS) != 0) ? 1 : 0);
}

internal const char *NativeMods_PlayName(int bit)
{
	return (bit == NATIVE_MODS_PLAY_MOON) ? "moon gravity" : ((bit == NATIVE_MODS_PLAY_ICY) ? "icy tracks" : "item chaos");
}

int NativeMods_PlayOn(int bit)
{
	return (NativeMods_PlayValue() & bit) != 0;
}

void NativeMods_SetPlayOn(int bit, int on)
{
	bit &= NATIVE_MODS_PLAY_ALL;

	// --dev-mods-play holds this run; the page changes it, the file keeps its own.
	if (s_modsPlayDevGiven)
	{
		s_modsPlayDev = on ? (s_modsPlayDev | bit) : (s_modsPlayDev & ~bit);
	}
	else
	{
		s_modsPlayFile = on ? (s_modsPlayFile | bit) : (s_modsPlayFile & ~bit);
		Platform_SettingsSave();
	}

	Platform_Log("[CTR Mods] %s: %s\n", NativeMods_PlayName(bit), on ? "on" : "off");
}

// --mods-selftest: the race rule is taken as met (there is no race to ask).
global_variable int s_modsSelfTestRace;

int NativeMods_PlayActive(int bit)
{
	return ((NativeMods_PlayValue() & bit) != 0) && (s_modsSelfTestRace || NativeMods_RaceAllowed(sdata->gGT));
}

global_variable u32 s_modsIcy[2];
global_variable int s_modsBotRescues;

internal void NativeMods_ArmReport(void);

u8 NativeMods_IcyTerrain(u8 terrainType, int bot)
{
	// Road terrain: full speed and full grip in the game's table (speedMultiplier,
	// slowUntilSpeed, counterSteerRatio and groundFrictionScale all 0x100), and
	// no effect of its own. Water and fast water keep their surface and splash
	// (COLL.c, the water and mud branch), mud its slowing and its spray, and
	// sideslip its flag; grass, dirt, track, snow and the two slow terrains brake
	// beside the road and keep doing it.
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
		if (!NativeMods_PlayActive(NATIVE_MODS_PLAY_ICY))
		{
			return terrainType;
		}
		if (!s_modsSelfTestRace)
		{
			NativeMods_ArmReport();
			s_modsIcy[bot ? 1 : 0]++;
		}
		return (u8)TERRAIN_ICE;
	default:
		return terrainType;
	}
}

internal void NativeMods_PlayReport(void)
{
	static const char *const step[3] = {"small", "medium", "large"};
	int k;

	if ((s_modsIcy[0] != 0) || (s_modsIcy[1] != 0))
	{
		Platform_Log("[CTR Mods] at exit: icy tracks, road terrain driven as ice in %u ground frame(s) of a kart and %u nav frame(s) of a CPU driver\n",
		             (unsigned)s_modsIcy[0], (unsigned)s_modsIcy[1]);
	}

	if (s_modsReportArmed)
	{
		Platform_Log("[CTR Mods] at exit: %d CPU driver(s) put back on the path after a kill plane while a play mod was on\n", s_modsBotRescues);
	}

	if (s_modsLandings != 0)
	{
		Platform_Log("[CTR Mods] at exit: moon gravity, %d landing(s) with a hang-time turbo by the full air time\n", s_modsLandings);
		for (k = 0; k < 3; k++)
		{
			Platform_Log("[CTR Mods]   %-6s by the full air time %d, given %d\n", step[k], s_modsLandingFull[k], s_modsLandingGiven[k]);
		}
	}

	for (k = 0; k < 2; k++)
	{
		int total = 0;
		int id;

		for (id = 0; id < NATIVE_MODS_ITEM_IDS; id++)
		{
			total += s_modsItems[k][id];
		}

		if (total != 0)
		{
			char line[512] = "";

			for (id = 0; id < NATIVE_MODS_ITEM_IDS; id++)
			{
				if (s_modsItems[k][id] != 0)
				{
					snprintf(&line[strlen(line)], sizeof(line) - strlen(line), " %d=%d", id, s_modsItems[k][id]);
				}
			}
			Platform_Log("[CTR Mods] at exit: item chaos, %d %s (held item id=count):%s\n", total, (k == 0) ? "item(s) from boxes" : "CPU attack(s)",
			             line);
		}
	}
}

internal void NativeMods_ArmReport(void)
{
	if (!s_modsReportArmed)
	{
		s_modsReportArmed = 1;
		Platform_AtExitReport(NativeMods_PlayReport);
	}
}

// A driver's air time of the jump in progress, past the meter's 2400 ms.
#define NATIVE_MODS_DRIVERS 8
global_variable int s_modsAir[NATIVE_MODS_DRIVERS];

void NativeMods_MoonAir(int driverID, int airTime)
{
	if ((driverID >= 0) && (driverID < NATIVE_MODS_DRIVERS) && NativeMods_PlayActive(NATIVE_MODS_PLAY_MOON))
	{
		s_modsAir[driverID] = airTime;
	}
}

int NativeMods_MoonHangTime(int driverID, int jumpMeter)
{
	int air = jumpMeter;

	if (!NativeMods_PlayActive(NATIVE_MODS_PLAY_MOON))
	{
		return jumpMeter;
	}

	if ((driverID >= 0) && (driverID < NATIVE_MODS_DRIVERS))
	{
		air = (s_modsAir[driverID] > jumpMeter) ? s_modsAir[driverID] : jumpMeter;
		s_modsAir[driverID] = 0;
	}

	return (air * NATIVE_MODS_MOON_NUM) / NATIVE_MODS_MOON_DEN;
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

void NativeMods_NoteBotRescue(void)
{
	if (NativeMods_PlayActive(NATIVE_MODS_PLAY_ALL))
	{
		NativeMods_ArmReport();
		s_modsBotRescues++;
	}
}

void NativeMods_NoteItem(int bot, int item)
{
	if ((item < 0) || (item >= NATIVE_MODS_ITEM_IDS))
	{
		return;
	}

	NativeMods_ArmReport();
	s_modsItems[bot ? 1 : 0][item]++;
}

void NativeMods_LogRace(void)
{
	static const int bits[3] = {NATIVE_MODS_PLAY_MOON, NATIVE_MODS_PLAY_ICY, NATIVE_MODS_PLAY_CHAOS};
	char text[64] = "";
	int k;

	for (k = 0; k < 3; k++)
	{
		if (NativeMods_PlayActive(bits[k]))
		{
			snprintf(&text[strlen(text)], sizeof(text) - strlen(text), "%s%s", (text[0] != '\0') ? ", " : "", NativeMods_PlayName(bits[k]));
		}
	}

	if (text[0] != '\0')
	{
		Platform_Log("[CTR Mods] play mods in this race: %s\n", text);
	}
}

int NativeMods_SetDevPlay(const char *list)
{
	char copy[64];
	char *word;
	char *next;
	int value = 0;

	if ((list == NULL) || (strlen(list) >= sizeof(copy)))
	{
		return 0;
	}

	snprintf(copy, sizeof(copy), "%s", list);
	if (strcmp(copy, "none") != 0)
	{
		for (word = copy; word != NULL; word = next)
		{
			next = strchr(word, ',');
			if (next != NULL)
			{
				*next++ = '\0';
			}

			if (strcmp(word, "moon") == 0)
			{
				value |= NATIVE_MODS_PLAY_MOON;
			}
			else if (strcmp(word, "icy") == 0)
			{
				value |= NATIVE_MODS_PLAY_ICY;
			}
			else if (strcmp(word, "chaos") == 0)
			{
				value |= NATIVE_MODS_PLAY_CHAOS;
			}
			else
			{
				return 0;
			}
		}
	}

	s_modsPlayDev = value;
	s_modsPlayDevGiven = 1;
	Platform_Log("[CTR Mods] --dev-mods-play: moon gravity %s, icy tracks %s, item chaos %s (this run only)\n",
	             ((value & NATIVE_MODS_PLAY_MOON) != 0) ? "on" : "off", ((value & NATIVE_MODS_PLAY_ICY) != 0) ? "on" : "off",
	             ((value & NATIVE_MODS_PLAY_CHAOS) != 0) ? "on" : "off");
	return 1;
}

// --mods-selftest (ctest mods_selftest): what the play mods compute without a
// race - the settings lines, the box's status line, --dev-mods-play, the ice
// terrain and the hang time of MOON GRAVITY. Touches no file; the module's
// values are put back at the end. 0 = passed.
int VehPhysGeneral_ChaosSelfTest(int *checks);               // game/Vehicle/VehPhysGeneral.c
int VehPhysGeneral_ChaosBoxSelfTest(int chaosOn, int *checks); // game/Vehicle/VehPhysGeneral.c

int NativeMods_SelfTest(void)
{
	static const struct
	{
		int cpu;
		u32 retail;
		int play;
		const char *status;
	} statusCases[] = {
	    {NATIVE_MODS_CPU_DEFAULT, 0u, 0, "OFF"},
	    {NATIVE_MODS_CPU_ALL, 0u, 0, "ALL RANDOM"},
	    {NATIVE_MODS_CPU_SELECTED, 0u, 0, "OFF"},
	    {NATIVE_MODS_CPU_SELECTED, 0x7u, 0, "3 SELECTED"},
	    {NATIVE_MODS_CPU_DEFAULT, 0u, NATIVE_MODS_PLAY_ICY, "1 MOD"},
	    {NATIVE_MODS_CPU_DEFAULT, 0u, NATIVE_MODS_PLAY_ALL, "3 MODS"},
	    {NATIVE_MODS_CPU_SELECTED, 0u, NATIVE_MODS_PLAY_MOON | NATIVE_MODS_PLAY_CHAOS, "2 MODS"},
	    {NATIVE_MODS_CPU_ALL, 0u, NATIVE_MODS_PLAY_MOON | NATIVE_MODS_PLAY_CHAOS, "RANDOM +2"},
	    {NATIVE_MODS_CPU_SELECTED, 0x7u, NATIVE_MODS_PLAY_CHAOS, "3 SEL +1"},
	    {NATIVE_MODS_CPU_SELECTED, 0x7fffu, NATIVE_MODS_PLAY_ALL, "15 SEL +3"},
	};
	static const struct
	{
		const char *line;
		int known;
		int play;
	} lineCases[] = {
	    {"moon 1", 1, NATIVE_MODS_PLAY_MOON},
	    {"icy 1", 1, NATIVE_MODS_PLAY_MOON | NATIVE_MODS_PLAY_ICY},
	    {"chaos 1", 1, NATIVE_MODS_PLAY_ALL},
	    {"icy 0", 1, NATIVE_MODS_PLAY_MOON | NATIVE_MODS_PLAY_CHAOS},
	    {"moon 2", 1, NATIVE_MODS_PLAY_CHAOS},
	    {"moon x", 0, NATIVE_MODS_PLAY_CHAOS},
	    {"chaos 0", 1, 0},
	};
	static const struct
	{
		const char *list;
		int ok;
		int play;
	} devCases[] = {
	    {"none", 1, 0}, {"moon", 1, NATIVE_MODS_PLAY_MOON}, {"icy,chaos", 1, NATIVE_MODS_PLAY_ICY | NATIVE_MODS_PLAY_CHAOS},
	    {"moon,icy,chaos", 1, NATIVE_MODS_PLAY_ALL}, {"", 0, 0}, {"moon,", 0, 0}, {"gravity", 0, 0},
	};
	static const struct
	{
		int meter;
		int hang;
	} moonCases[] = {{0, 0}, {1560, 639}, {1561, 640}, {2342, 960}, {0x960, 984}};
	const int savedCpu = s_modsCpu;
	const u32 savedRetail = s_modsRetailOn;
	const int savedFile = s_modsPlayFile;
	const int savedDev = s_modsPlayDev;
	const int savedDevGiven = s_modsPlayDevGiven;
	const int savedCpuDev = s_modsDevGiven;
	int checks = 0;
	int failures = 0;
	size_t k;
	int t;

	s_modsDevGiven = 0;
	s_modsPlayDevGiven = 0;

	for (k = 0; k < sizeof(statusCases) / sizeof(statusCases[0]); k++)
	{
		const char *status;

		s_modsCpu = statusCases[k].cpu;
		s_modsRetailOn = statusCases[k].retail;
		s_modsPlayFile = statusCases[k].play;
		status = NativeMods_StatusText();
		checks++;
		if ((strcmp(status, statusCases[k].status) != 0) || (strlen(status) > 11u))
		{
			printf("mods selftest: status case %d is \"%s\", wanted \"%s\"\n", (int)k, status, statusCases[k].status);
			failures++;
		}
	}

	s_modsPlayFile = 0;
	for (k = 0; k < sizeof(lineCases) / sizeof(lineCases[0]); k++)
	{
		const int known = NativeMods_LoadLine(lineCases[k].line);

		checks++;
		if ((known != lineCases[k].known) || (s_modsPlayFile != lineCases[k].play))
		{
			printf("mods selftest: line \"%s\" gives known %d play %d, wanted %d and %d\n", lineCases[k].line, known, s_modsPlayFile,
			       lineCases[k].known, lineCases[k].play);
			failures++;
		}
	}

	for (k = 0; k < sizeof(devCases) / sizeof(devCases[0]); k++)
	{
		int ok;

		s_modsPlayDev = 0;
		s_modsPlayDevGiven = 0;
		ok = NativeMods_SetDevPlay(devCases[k].list);
		checks++;
		if ((ok != devCases[k].ok) || (ok && (s_modsPlayDev != devCases[k].play)))
		{
			printf("mods selftest: --dev-mods-play \"%s\" gives %d / %d, wanted %d / %d\n", devCases[k].list, ok, s_modsPlayDev, devCases[k].ok,
			       devCases[k].play);
			failures++;
		}
	}
	s_modsPlayDevGiven = 0;

	// ICE: off, every terrain stays; on, the road terrain is ice and nothing else changes.
	s_modsSelfTestRace = 1;
	for (t = 0; t <= TERRAIN_SLOWDIRT; t++)
	{
		const int road = (t == TERRAIN_ASPHALT) || (t == TERRAIN_WOOD) || (t == TERRAIN_STONE) || (t == TERRAIN_HARDPACK) || (t == TERRAIN_METAL) ||
		                 (t == TERRAIN_RIVERASPHALT) || (t == TERRAIN_STEAMASPHALT) || (t == TERRAIN_OCEANASPHALT);

		s_modsPlayFile = 0;
		checks++;
		failures += (NativeMods_IcyTerrain((u8)t, 0) != (u8)t);
		s_modsPlayFile = NATIVE_MODS_PLAY_ALL & ~NATIVE_MODS_PLAY_ICY;
		checks++;
		failures += (NativeMods_IcyTerrain((u8)t, 0) != (u8)t);
		s_modsPlayFile = NATIVE_MODS_PLAY_ICY;
		checks++;
		if (NativeMods_IcyTerrain((u8)t, 1) != (road ? (u8)TERRAIN_ICE : (u8)t))
		{
			printf("mods selftest: terrain %d becomes %d with ICY TRACKS\n", t, (int)NativeMods_IcyTerrain((u8)t, 1));
			failures++;
		}
	}

	// MOON GRAVITY: off, the air time stays; on, times 41/100.
	for (k = 0; k < sizeof(moonCases) / sizeof(moonCases[0]); k++)
	{
		s_modsPlayFile = NATIVE_MODS_PLAY_ICY;
		checks++;
		failures += (NativeMods_MoonHangTime(-1, moonCases[k].meter) != moonCases[k].meter);
		s_modsPlayFile = NATIVE_MODS_PLAY_MOON;
		checks++;
		if (NativeMods_MoonHangTime(-1, moonCases[k].meter) != moonCases[k].hang)
		{
			printf("mods selftest: air time %d judged as %d with MOON GRAVITY, wanted %d\n", moonCases[k].meter,
			       NativeMods_MoonHangTime(-1, moonCases[k].meter), moonCases[k].hang);
			failures++;
		}
	}

	// Past the meter: 3513 ms in the air, the meter stopped at 2400 - judged as
	// 1440, the large step a jump of 1440 ms gets at full gravity; and only once.
	s_modsPlayFile = NATIVE_MODS_PLAY_MOON;
	NativeMods_MoonAir(3, 3513);
	checks += 2;
	failures += (NativeMods_MoonHangTime(3, 0x960) != 1440);
	failures += (NativeMods_MoonHangTime(3, 0x960) != 984);
	s_modsPlayFile = 0;
	NativeMods_MoonAir(3, 3513);
	s_modsPlayFile = NATIVE_MODS_PLAY_MOON;
	checks++;
	failures += (NativeMods_MoonHangTime(3, 0x960) != 984);
	s_modsSelfTestRace = 0;

	// ITEM CHAOS: the draw over one whole period of the game's random numbers,
	// then the item box itself, off and on.
	failures += VehPhysGeneral_ChaosSelfTest(&checks);
	s_modsSelfTestRace = 1;
	s_modsPlayFile = 0;
	failures += VehPhysGeneral_ChaosBoxSelfTest(0, &checks);
	s_modsPlayFile = NATIVE_MODS_PLAY_CHAOS;
	failures += VehPhysGeneral_ChaosBoxSelfTest(1, &checks);
	s_modsSelfTestRace = 0;

	s_modsCpu = savedCpu;
	s_modsRetailOn = savedRetail;
	s_modsPlayFile = savedFile;
	s_modsPlayDev = savedDev;
	s_modsPlayDevGiven = savedDevGiven;
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
