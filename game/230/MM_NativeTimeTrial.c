#include <common.h>

#ifdef CTR_NATIVE

#include <time.h>

#include "platform/native_assets.h"
#include "platform/native_chars.h"

// NITRO-PIT -> NITRO RACE -> MODE: TIME TRIAL. ALONE ON A CONTAINER TRACK.
//
// The MODE box below the lap box of the track screen (MM_NativeTrackSelect.c)
// calls MM_NativeTimeTrial_Arm at the start when it stands on TIME TRIAL. The
// race is an ordinary arcade race (NITRO RACE) plus this marker - the
// TIME_TRIAL bit of retail stays OFF, on purpose:
//   - in a race gGT->levelID is the donor slot (DINGO CANYON), never the
//     container's own ID. Retail time trial would write its best times, the
//     N. Tropy and N. Oxide flags and the ghost checks onto that disc track
//     (MainGameEnd.c, 224.c) and offer the memory card;
//   - its ghost code (GhostReplay.c, GhostTape.c) and the N. Tropy and Oxide
//     ghosts (QueueLoadTrack.c) read tables a container need not carry;
//   - custom characters are refused under TIME_TRIAL (native_chars.c).
// None of that runs here: no ghost, no retail best time, no memory card.
//
// WHAT IS LIKE RETAIL TIME TRIAL, each switch under CTR_NATIVE and only with
// MM_NativeTimeTrial_IsCustom():
//   - no opponents: MainInit_Drivers spawns no bot (MainInit.c, the path the
//     track preview uses as well);
//   - no item crates, no fruit crates, no wumpa fruit: the instances lose draw
//     and collision as in the TIME_TRIAL branch of INSTANCE_LevInitAll
//     (INSTANCE.c);
//   - the laps come from the lap box (gGT->numLaps, 3/5/7), not the fixed 3;
//   - the title bar says TIME TRIAL (UI_RaceFlow.c);
//   - the lap times as a list, drawn here (MM_NativeTimeTrial_DrawHud) - the
//     retail list in UI_DrawRaceClock skips them in ARCADE_MODE.
//
// WHAT IS DIFFERENT: the best times live in a text file of their own next to
// ctr-settings.cfg (MM_NATIVE_TT_FILE, never the memory card), one line per
// track and lap count. The HUD shows the stored best from the first frame on,
// the results the best and NEW RECORD. The arcade HUD keeps what has no switch
// outside UI_RenderFrame.c: the label TIME (not TIME TRIAL), the rank column
// on the left and the wumpa counter; the lap list therefore stands on the
// right, below the lap counter, where nothing else is drawn.
//
// THE MARKER: Arm sets it (ARMED), MainInit_Drivers confirms it for the race
// (RACING) and RETRY keeps it - the restart sets the race up again. Back in
// the menu level (CHANGE LEVEL, CHANGE CHARACTER, QUIT, pause menu) the
// menu tick clears it, so the next NITRO RACE has opponents and items again;
// the MODE box calls Arm again for the next start. IsCustom also asks the
// MODE box itself, so a stale marker cannot make a RACE start alone.

#define MM_NATIVE_TT_FILE      "nitro-pit-times.tsv"
#define MM_NATIVE_TT_TEMP      "~nitro-pit-times.tsv.tmp"
#define MM_NATIVE_TT_OLD       "~nitro-pit-times.tsv.old"
#define MM_NATIVE_TT_FILE_MAX  (1 << 20)
#define MM_NATIVE_TT_LINE_MAX  512
#define MM_NATIVE_TT_FIELDS    8
#define MM_NATIVE_TT_NAME_MAX  32
#define MM_NATIVE_TT_TIME_TEXT 16

// 99:59:99 in ticks, the longest time the clock shows (UI_Clock.c). A longer
// one is shown as that and is no row of the file.
#define MM_NATIVE_TT_TIME_MAX (100 * 0xe100 - 1)

enum
{
	MM_NATIVE_TT_OFF = 0,
	MM_NATIVE_TT_ARMED = 1,  // Arm ran, the race is not set up yet
	MM_NATIVE_TT_RACING = 2, // MainInit_Drivers set a race up alone
};

enum
{
	// The lap list: below the lap counter of the one-player HUD (slot
	// UI_HUD_SLOT_LAP_COUNT: label at y, "1/3" at y + 8 in the big font, 17
	// rows high), right-aligned on its x. The first row keeps the gap retail
	// keeps below its big clock (UI_Clock.c: clock at 8, first lap row at
	// 8 + 8 + 0x10, 8 rows below the big font): 8 + 0x18 = 0x20 below the
	// label. Higher up it touches the big "1/3" and the wumpa count beside it
	// (slot UI_HUD_SLOT_WUMPA_COUNT, big font down to y 32).
	MM_NATIVE_TT_HUD_FIRST_ROW_Y = 0x20,
	MM_NATIVE_TT_HUD_ROW_STEP = 8,

	// The results: below the driver icon row (y 0x60) and above the end box.
	// The box is menu222 (same rows, same proc) moved down from 170 to make room
	// for two lines in the big font.
	MM_NATIVE_TT_END_BEST_Y = 0x84,
	MM_NATIVE_TT_END_NEWS_Y = 0x95,
	MM_NATIVE_TT_END_BOX_Y = 0xBC,

	MM_NATIVE_TT_MAX_LAPS = 7, // gGT->lapTime[7]
};

// The key of a best time: all four must match. The ID is the one of
// tracks\track-ids.tsv, the file name is the identity of the registry, the
// LEVD SHA-256 its version stamp. A different stamp is a different track.
struct MMNativeTimeKey
{
	int ok;
	const char *why;
	int id;
	char file[128];
	char sha[65];
	int laps;
};

struct MMNativeTimeRow
{
	int id;
	char file[128];
	char sha[65];
	int laps;
	int total; // ticks, 960 per second
	int lap;   // ticks
	char driver[MM_NATIVE_TT_NAME_MAX];
	char date[16];
};

global_variable int s_ttState = MM_NATIVE_TT_OFF;

// Per race, set again by every setup (MM_NativeTimeTrial_RaceAlone).
global_variable struct MMNativeTimeKey s_ttKey;
global_variable struct MMNativeTimeRow s_ttBest;
global_variable int s_ttHaveBest = 0;
global_variable int s_ttItemsOff = 0;
global_variable int s_ttEndDone = 0;
global_variable int s_ttNewRecord = 0;
global_variable int s_ttNewLap = 0;

global_variable int s_ttLockLogged = 0;

global_variable char s_ttBestLabel[] = "BEST";
global_variable char s_ttNewRecordText[] = "NEW RECORD";

int MM_NativeTimeTrial_IsCustom(void)
{
	const struct GameTracker *gGT = sdata->gGT;

	// The race type as NITRO RACE starts it: arcade, one player, no other
	// mode bit, a container on the donor slot - and the MODE box still on
	// TIME TRIAL.
	return (s_ttState != MM_NATIVE_TT_OFF) && (gGT->numPlyrCurrGame == 1) && ((gGT->gameMode1 & ARCADE_MODE) != 0) &&
	       ((gGT->gameMode1 & (TIME_TRIAL | ADVENTURE_MODE | BATTLE_MODE | CRYSTAL_CHALLENGE | RELIC_RACE)) == 0) &&
	       ((gGT->gameMode2 & (TOKEN_RACE | CUP_ANY_KIND)) == 0) && (MM_NativeTrackSelect_Mode() == MM_NATIVE_MODE_TIME_TRIAL) &&
	       NativeTrack_ActiveForLevel(gGT->levelID);
}

// ---------------------------------------------------------------------------
// THE FILE.
//
// One line per key, tab separated, '#' starts a comment:
//   id  file  levd-sha256  laps  best-total  best-lap  driver  date
// Times in ticks (960 per second), at most 99:59:99. driver and date belong to
// the best total; a better lap alone changes only best-lap. A line that is
// not a complete row - a comment, an older or newer layout, a damaged line,
// one with a NUL byte or a time beyond 99:59:99 - is skipped when reading and
// written back byte for byte, as is every row of another track, including
// tracks that are not in the folder any more. Line ends stay those of the
// file (LF or CR LF), a UTF-8 byte order mark at the start stays as well.
//
// Writing: the whole file into MM_NATIVE_TT_TEMP, the old file steps aside as
// MM_NATIVE_TT_OLD, the temp file takes its name, the old one is removed (the
// C library does not rename over an existing file on every system). The
// temp file is never read: a leftover one is removed. A leftover old file
// is put back if the file itself is missing (a crash between the two
// renames), else removed (MM_NativeTimeTrial_Tidy).
//
// Under the settings lock (--settings-defaults: measurement runs) it is
// neither read nor written, as ctr-settings.cfg and track-ids.tsv.

internal int MM_NativeTimeTrial_Locked(void)
{
	if (!Platform_SettingsLocked())
	{
		return 0;
	}

	if (!s_ttLockLogged)
	{
		s_ttLockLogged = 1;
		Platform_Log("[CTR Menu] TIME TRIAL: settings are locked - %s is neither read nor written, no best time is kept\n", MM_NATIVE_TT_FILE);
	}

	return 1;
}

internal int MM_NativeTimeTrial_Lower(int c)
{
	return ((c >= 'A') && (c <= 'Z')) ? (c - 'A' + 'a') : c;
}

// File names and hashes compare without case, as in the registry.
internal int MM_NativeTimeTrial_SameText(const char *a, const char *b)
{
	while ((*a != '\0') && (MM_NativeTimeTrial_Lower((u8)*a) == MM_NativeTimeTrial_Lower((u8)*b)))
	{
		a++;
		b++;
	}

	return MM_NativeTimeTrial_Lower((u8)*a) == MM_NativeTimeTrial_Lower((u8)*b);
}

// A whole number of decimal digits, 1..9 of them; else -1.
internal int MM_NativeTimeTrial_Number(const char *text)
{
	int value = 0;
	int digits = 0;

	while ((*text >= '0') && (*text <= '9') && (digits < 9))
	{
		value = value * 10 + (*text - '0');
		text++;
		digits++;
	}

	return ((digits > 0) && (*text == '\0')) ? value : -1;
}

internal int MM_NativeTimeTrial_IsSha(const char *text)
{
	int i;

	for (i = 0; i < 64; i++)
	{
		const int c = MM_NativeTimeTrial_Lower((u8)text[i]);

		if (!(((c >= '0') && (c <= '9')) || ((c >= 'a') && (c <= 'f'))))
		{
			return 0;
		}
	}

	return text[64] == '\0';
}

// One line (without its line end) into a row. 0: not a row - kept, skipped.
internal int MM_NativeTimeTrial_ParseRow(const char *line, int length, struct MMNativeTimeRow *row)
{
	char copy[MM_NATIVE_TT_LINE_MAX];
	char *field[MM_NATIVE_TT_FIELDS];
	int count = 1;
	int i;

	// A line written on Windows ends in CR LF; the CR is not part of the row.
	if ((length > 0) && (line[length - 1] == '\r'))
	{
		length--;
	}

	// A NUL byte would end a field early and hide what follows it.
	if ((length <= 0) || (length >= (int)sizeof(copy)) || (line[0] == '#') || (memchr(line, '\0', (size_t)length) != NULL))
	{
		return 0;
	}

	memcpy(copy, line, (size_t)length);
	copy[length] = '\0';
	field[0] = copy;

	for (i = 0; i < length; i++)
	{
		if (copy[i] != '\t')
		{
			continue;
		}

		if (count == MM_NATIVE_TT_FIELDS)
		{
			return 0;
		}

		copy[i] = '\0';
		field[count++] = &copy[i + 1];
	}

	if (count != MM_NATIVE_TT_FIELDS)
	{
		return 0;
	}

	memset(row, 0, sizeof(*row));
	row->id = MM_NativeTimeTrial_Number(field[0]);
	row->laps = MM_NativeTimeTrial_Number(field[3]);
	row->total = MM_NativeTimeTrial_Number(field[4]);
	row->lap = MM_NativeTimeTrial_Number(field[5]);

	if ((row->id < NATIVE_TRACK_LEVELID_FIRST) || (row->id >= NATIVE_TRACK_LEVELID_FIRST + NATIVE_TRACK_LEVELID_COUNT) || (field[1][0] == '\0') ||
	    (strlen(field[1]) >= sizeof(row->file)) || !MM_NativeTimeTrial_IsSha(field[2]) || (row->laps < 1) || (row->laps > MM_NATIVE_TT_MAX_LAPS) ||
	    (row->total <= 0) || (row->total > MM_NATIVE_TT_TIME_MAX) || (row->lap <= 0) || (row->lap > row->total) ||
	    (strlen(field[6]) >= sizeof(row->driver)) || (strlen(field[7]) >= sizeof(row->date)))
	{
		return 0;
	}

	snprintf(row->file, sizeof(row->file), "%s", field[1]);
	snprintf(row->sha, sizeof(row->sha), "%s", field[2]);
	snprintf(row->driver, sizeof(row->driver), "%s", field[6]);
	snprintf(row->date, sizeof(row->date), "%s", field[7]);
	return 1;
}

internal int MM_NativeTimeTrial_RowMatches(const struct MMNativeTimeRow *row, const struct MMNativeTimeKey *key)
{
	return (row->id == key->id) && (row->laps == key->laps) && MM_NativeTimeTrial_SameText(row->file, key->file) &&
	       MM_NativeTimeTrial_SameText(row->sha, key->sha);
}

internal int MM_NativeTimeTrial_Exists(const char *path)
{
	FILE *file = fopen(path, "rb");

	if (file == NULL)
	{
		return 0;
	}

	fclose(file);
	return 1;
}

// What an interrupted write left behind (see THE FILE above).
internal void MM_NativeTimeTrial_Tidy(void)
{
	if (MM_NativeTimeTrial_Exists(MM_NATIVE_TT_OLD))
	{
		if (!MM_NativeTimeTrial_Exists(MM_NATIVE_TT_FILE))
		{
			if (rename(MM_NATIVE_TT_OLD, MM_NATIVE_TT_FILE) == 0)
			{
				Platform_LogWarn("[CTR Menu] TIME TRIAL: %s was missing after an interrupted write - put back from %s\n", MM_NATIVE_TT_FILE,
				                 MM_NATIVE_TT_OLD);
			}
			else
			{
				Platform_LogWarn("[CTR Menu] TIME TRIAL: %s is missing and %s cannot be renamed back - the best times stay in %s\n", MM_NATIVE_TT_FILE,
				                 MM_NATIVE_TT_OLD, MM_NATIVE_TT_OLD);
			}
		}
		else if (remove(MM_NATIVE_TT_OLD) == 0)
		{
			Platform_LogWarn("[CTR Menu] TIME TRIAL: leftover %s of an interrupted write removed\n", MM_NATIVE_TT_OLD);
		}
	}

	if (MM_NativeTimeTrial_Exists(MM_NATIVE_TT_TEMP))
	{
		if (remove(MM_NATIVE_TT_TEMP) == 0)
		{
			Platform_LogWarn("[CTR Menu] TIME TRIAL: leftover %s of an interrupted write removed, not read\n", MM_NATIVE_TT_TEMP);
		}
		else
		{
			Platform_LogWarn("[CTR Menu] TIME TRIAL: leftover %s cannot be removed - it is not read\n", MM_NATIVE_TT_TEMP);
		}
	}
}

// The whole file, byte for byte (binary: a line keeps its CR, a stray byte
// survives the rewrite). NULL when it is missing; *tooBig when it is larger
// than MM_NATIVE_TT_FILE_MAX - then it is neither used nor overwritten.
internal char *MM_NativeTimeTrial_ReadFile(int *size, int *tooBig)
{
	FILE *file;
	char *text;
	size_t got;

	*size = 0;
	*tooBig = 0;

	MM_NativeTimeTrial_Tidy();

	file = fopen(MM_NATIVE_TT_FILE, "rb");
	if (file == NULL)
	{
		return NULL;
	}

	text = (char *)malloc(MM_NATIVE_TT_FILE_MAX + 1);
	if (text == NULL)
	{
		fclose(file);
		*tooBig = 1;
		return NULL;
	}

	got = fread(text, 1, MM_NATIVE_TT_FILE_MAX + 1, file);
	fclose(file);

	if (got > MM_NATIVE_TT_FILE_MAX)
	{
		free(text);
		*tooBig = 1;
		return NULL;
	}

	text[got] = '\0';
	*size = (int)got;
	return text;
}

// The UTF-8 byte order mark an editor may put in front: 3, else 0. It
// belongs to no line and is written back unchanged.
internal int MM_NativeTimeTrial_BomLength(const char *text, int size)
{
	return ((size >= 3) && ((u8)text[0] == 0xEF) && ((u8)text[1] == 0xBB) && ((u8)text[2] == 0xBF)) ? 3 : 0;
}

// The next line of text from *pos: start and length without the '\n' (a CR
// before it stays part of the line). 0 at the end of the text.
internal int MM_NativeTimeTrial_NextLine(const char *text, int size, int *pos, int *start, int *length)
{
	int end;

	if (*pos >= size)
	{
		return 0;
	}

	*start = *pos;
	end = *pos;
	while ((end < size) && (text[end] != '\n'))
	{
		end++;
	}

	*length = end - *start;
	*pos = (end < size) ? (end + 1) : end;
	return 1;
}

// The first complete row with this key. 0: none (or no file).
internal int MM_NativeTimeTrial_Lookup(const struct MMNativeTimeKey *key, struct MMNativeTimeRow *out)
{
	char *text;
	int size;
	int tooBig;
	int pos;
	int start;
	int length;
	int found = 0;

	if (!key->ok || MM_NativeTimeTrial_Locked())
	{
		return 0;
	}

	text = MM_NativeTimeTrial_ReadFile(&size, &tooBig);
	if (text == NULL)
	{
		if (tooBig)
		{
			Platform_LogWarn("[CTR Menu] TIME TRIAL: %s is larger than %d bytes - not used\n", MM_NATIVE_TT_FILE, MM_NATIVE_TT_FILE_MAX);
		}
		return 0;
	}

	pos = MM_NativeTimeTrial_BomLength(text, size);
	while (!found && MM_NativeTimeTrial_NextLine(text, size, &pos, &start, &length))
	{
		struct MMNativeTimeRow row;

		if (MM_NativeTimeTrial_ParseRow(&text[start], length, &row) && MM_NativeTimeTrial_RowMatches(&row, key))
		{
			*out = row;
			found = 1;
		}
	}

	free(text);
	return found;
}

internal int MM_NativeTimeTrial_WriteText(FILE *file, const char *text, int length)
{
	return (length <= 0) || (fwrite(text, 1, (size_t)length, file) == (size_t)length);
}

// Writes the row: replaces the first row with its key, every other line
// stays as it was, byte for byte; without one the row is appended. Line ends
// are the file's (those of its first line; a new file: LF), a last line
// without one gets the file's. NULL = written, else why not.
internal const char *MM_NativeTimeTrial_Store(const struct MMNativeTimeKey *key, const struct MMNativeTimeRow *row)
{
	char line[MM_NATIVE_TT_LINE_MAX];
	const char *eol = "\n";
	char *text;
	int size;
	int tooBig;
	int pos;
	int start;
	int length;
	int replaced = 0;
	int ok = 1;
	int lineLength;
	FILE *file;

	lineLength = snprintf(line, sizeof(line), "%d\t%s\t%s\t%d\t%d\t%d\t%s\t%s", key->id, key->file, key->sha, key->laps, row->total, row->lap, row->driver,
	                      row->date);
	if ((lineLength <= 0) || (lineLength >= (int)sizeof(line)))
	{
		return "the row is too long";
	}

	text = MM_NativeTimeTrial_ReadFile(&size, &tooBig);
	if (tooBig)
	{
		return "the file is too large to be rewritten";
	}

	file = fopen(MM_NATIVE_TT_TEMP, "wb");
	if (file == NULL)
	{
		free(text);
		return "cannot create the temp file";
	}

	if (text == NULL)
	{
		const char *head = "# CTR Reload - NITRO-PIT time trial best times. The game maintains this file.\n"
		                   "# One row per track and lap count: id<TAB>file<TAB>levd-sha256<TAB>laps<TAB>best-total<TAB>best-lap<TAB>driver<TAB>date\n"
		                   "# Times in ticks, 960 per second. A row counts only while id, file and levd-sha256 match the track.\n"
		                   "# driver and date belong to the best total. Lines the game cannot read are kept as they are.\n";

		ok = MM_NativeTimeTrial_WriteText(file, head, (int)strlen(head));
	}
	else
	{
		const int bom = MM_NativeTimeTrial_BomLength(text, size);
		int scan = bom;

		// The line end of the file: that of its first line.
		if (MM_NativeTimeTrial_NextLine(text, size, &scan, &start, &length) && (scan > start + length) && (length > 0) &&
		    (text[start + length - 1] == '\r'))
		{
			eol = "\r\n";
		}

		ok = MM_NativeTimeTrial_WriteText(file, text, bom);
		pos = bom;

		while (ok && MM_NativeTimeTrial_NextLine(text, size, &pos, &start, &length))
		{
			struct MMNativeTimeRow old;
			const int ended = (start + length) < size;

			if (!replaced && MM_NativeTimeTrial_ParseRow(&text[start], length, &old) && MM_NativeTimeTrial_RowMatches(&old, key))
			{
				ok = MM_NativeTimeTrial_WriteText(file, line, lineLength) && MM_NativeTimeTrial_WriteText(file, eol, (int)strlen(eol));
				replaced = 1;
				continue;
			}

			// The line as it is; its CR, if any, is part of it.
			ok = MM_NativeTimeTrial_WriteText(file, &text[start], length) &&
			     MM_NativeTimeTrial_WriteText(file, ended ? "\n" : eol, ended ? 1 : (int)strlen(eol));
		}
	}

	if (ok && !replaced)
	{
		ok = MM_NativeTimeTrial_WriteText(file, line, lineLength) && MM_NativeTimeTrial_WriteText(file, eol, (int)strlen(eol));
	}

	free(text);

	ok = (fflush(file) == 0) && ok;
	ok = (fclose(file) == 0) && ok;
	if (!ok)
	{
		remove(MM_NATIVE_TT_TEMP);
		return "writing the temp file failed";
	}

	// The old file steps aside, the temp file takes its name, the old one goes.
	if (MM_NativeTimeTrial_Exists(MM_NATIVE_TT_FILE) && (rename(MM_NATIVE_TT_FILE, MM_NATIVE_TT_OLD) != 0))
	{
		remove(MM_NATIVE_TT_TEMP);
		return "the file cannot be replaced (open in another program or read-only)";
	}

	if (rename(MM_NATIVE_TT_TEMP, MM_NATIVE_TT_FILE) != 0)
	{
		rename(MM_NATIVE_TT_OLD, MM_NATIVE_TT_FILE);
		remove(MM_NATIVE_TT_TEMP);
		return "renaming the temp file failed";
	}

	remove(MM_NATIVE_TT_OLD);
	return NULL;
}

// ---------------------------------------------------------------------------
// THE RACE.

// m:ss:cc as RECTMENU_DrawTime (RECTMENU.c), into a buffer of its own.
// hudRow: the form of the retail lap rows (UI_Clock.c) - a leading blank as
// the gap after the label; oneMinuteDigit as there, for a lap. No time (-1):
// -:--:--. Beyond 99:59:99 it shows 99:59:99 (and ticks * 100 cannot overflow).
internal void MM_NativeTimeTrial_FormatTime(char *out, int outSize, int ticks, int hudRow, int oneMinuteDigit)
{
	const char *lead = hudRow ? " " : "";
	int minutes;

	ticks = (ticks > MM_NATIVE_TT_TIME_MAX) ? MM_NATIVE_TT_TIME_MAX : ticks;
	minutes = oneMinuteDigit ? ((ticks / 0xe100) % 10) : (ticks / 0xe100);

	if (ticks < 0)
	{
		snprintf(out, (size_t)outSize, "%s-:--:--", lead);
		return;
	}

	snprintf(out, (size_t)outSize, "%s%d:%d%d:%d%d", lead, minutes, (ticks / 0x2580) % 6, (ticks / 0x3c0) % 10, ((ticks * 10) / 0x3c0) % 10,
	         ((ticks * 100) / 0x3c0) % 10);
}

// The key of the loaded container and the laps of this race.
internal void MM_NativeTimeTrial_BuildKey(struct MMNativeTimeKey *key)
{
	const struct GameTracker *gGT = sdata->gGT;
	const int index = NativeTrack_LoadedIndex();
	const struct NativeTrackEntry *entry = (index >= 0) ? NativeTrack_Get(index) : NULL;
	int i;

	memset(key, 0, sizeof(*key));
	key->laps = (int)gGT->numLaps;

	if (entry == NULL)
	{
		key->why = "no container loaded";
		return;
	}

	key->id = NativeTrack_LevelForIndex(index);
	if (key->id < NATIVE_TRACK_LEVELID_FIRST)
	{
		key->why = "the track has no level id";
		return;
	}

	if (!entry->levdShaOk)
	{
		key->why = "the track has no LEVD hash";
		return;
	}

	if ((key->laps < 1) || (key->laps > MM_NATIVE_TT_MAX_LAPS))
	{
		key->why = "lap count out of range";
		return;
	}

	snprintf(key->file, sizeof(key->file), "%s", entry->file);
	for (i = 0; i < 32; i++)
	{
		snprintf(&key->sha[i * 2], 3, "%02x", (unsigned)entry->levdSha[i]);
	}

	key->ok = 1;
}

// The name of the driver in seat 0 at the finish: a bound custom character by
// its CHRI name, else the short retail name ("Crash"). Tabs and line ends
// would break the row and become blanks.
internal void MM_NativeTimeTrial_DriverName(char *out, int outSize)
{
	const int pick = NativeChar_Pick();
	const int characterID = (int)data.characterIDs[0];
	const char *name = "";
	int i;

	if ((pick >= 0) && (NativeChar_SeatModel(0) != NULL))
	{
		name = NativeChar_EntryName(pick);
	}
	else if ((characterID >= 0) && (characterID < (int)(sizeof(data.MetaDataCharacters) / sizeof(data.MetaDataCharacters[0]))))
	{
		name = sdata->lngStrings[data.MetaDataCharacters[characterID].name_LNG_short];
	}

	snprintf(out, (size_t)outSize, "%s", (name != NULL) ? name : "");
	for (i = 0; out[i] != '\0'; i++)
	{
		if ((out[i] == '\t') || (out[i] == '\r') || (out[i] == '\n'))
		{
			out[i] = ' ';
		}
	}
}

// From MM_NativeTrackSelect_MenuProc, when the container of the row is loaded
// and QueueLoadTrack is about to request the level, and the MODE box stands on
// TIME TRIAL. Sets no mode bit: the race is NITRO RACE (see above).
void MM_NativeTimeTrial_Arm(void)
{
	s_ttState = MM_NATIVE_TT_ARMED;
	s_ttEndDone = 0;

	Platform_Log("[CTR Menu] NITRO-PIT -> TIME TRIAL: '%s' - %d laps, alone, no items, no ghost, best times in %s\n", NativeTrack_LoadedName(),
	             (int)sdata->gGT->numLaps, MM_NATIVE_TT_FILE);
}

// From NativeMenuLock_Tick, only in the menu level (see MM_NativeCrystal_MenuTick,
// the same reason). After a time trial race every way back into the menu
// clears the marker; an ARMED marker (start on its way) is cleared only when
// the MODE box no longer says TIME TRIAL.
void MM_NativeTimeTrial_MenuTick(void)
{
	const struct GameTracker *gGT = sdata->gGT;

	if ((s_ttState == MM_NATIVE_TT_OFF) || ((gGT->gameMode1 & MAIN_MENU) == 0))
	{
		return;
	}

	if (s_ttState == MM_NATIVE_TT_RACING)
	{
		s_ttState = MM_NATIVE_TT_OFF;
		Platform_Log("[CTR Menu] back in the menu - TIME TRIAL marker cleared, the next race has opponents and items again\n");
	}
	else if (MM_NativeTrackSelect_Mode() != MM_NATIVE_MODE_TIME_TRIAL)
	{
		s_ttState = MM_NATIVE_TT_OFF;
		Platform_Log("[CTR Menu] TIME TRIAL start not taken - marker cleared\n");
	}
}

// From MainInit_Drivers, in every race setup (RETRY included): 1 = this race
// runs alone, no bots. Also resets the race and reads the stored best, so a
// second run shows the record of the first one from its first frame on.
int MM_NativeTimeTrial_RaceAlone(void)
{
	if (!MM_NativeTimeTrial_IsCustom())
	{
		// A start that did not become a container time trial (the load fell
		// back to the bare donor slot): the marker ends with this setup.
		if (s_ttState == MM_NATIVE_TT_ARMED)
		{
			s_ttState = MM_NATIVE_TT_OFF;
			Platform_LogWarn("[CTR Menu] TIME TRIAL: this race is no container time trial - marker cleared\n");
		}
		return 0;
	}

	s_ttState = MM_NATIVE_TT_RACING;
	s_ttItemsOff = 0;
	s_ttEndDone = 0;
	s_ttNewRecord = 0;
	s_ttNewLap = 0;

	MM_NativeTimeTrial_BuildKey(&s_ttKey);
	s_ttHaveBest = MM_NativeTimeTrial_Lookup(&s_ttKey, &s_ttBest);
	return 1;
}

// From INSTANCE_LevInitAll, for every crate or wumpa fruit switched off.
void MM_NativeTimeTrial_NoteItemOff(void)
{
	s_ttItemsOff++;
}

// From MainInit_FinalizeInit, after the instances, next to the [CTR Race] line.
void MM_NativeTimeTrial_LogRace(void)
{
	char best[MM_NATIVE_TT_TIME_TEXT];

	if (!MM_NativeTimeTrial_IsCustom())
	{
		return;
	}

	MM_NativeTimeTrial_FormatTime(best, sizeof(best), s_ttHaveBest ? s_ttBest.total : -1, 0, 0);
	Platform_Log("[CTR Menu] TIME TRIAL race: '%s' laps %d - no opponents, %d crate(s) and fruit switched off, best %s%s%s%s\n", NativeTrack_LoadedName(),
	             (int)sdata->gGT->numLaps, s_ttItemsOff, best, s_ttHaveBest ? " by " : "", s_ttHaveBest ? s_ttBest.driver : "",
	             s_ttKey.ok ? "" : " (no key - nothing is stored)");
}

// From the native block in front of UI_RenderFrame_Racing (MainFrame_RenderFrame.c),
// every racing frame: the stored best and the lap list, in the retail form of
// the time trial rows (UI_Clock.c: "Ln" red in the small font, the time
// periwinkle right after it), right-aligned below the lap counter.
void MM_NativeTimeTrial_DrawHud(void)
{
	struct GameTracker *gGT = sdata->gGT;
	const struct Driver *d = gGT->drivers[0];
	const struct UiElement2D *lapSlot;
	char timeText[MM_NATIVE_TT_TIME_TEXT];
	char label[4];
	int right;
	int y;
	int laps;
	int lapIndex;
	int n;

	if (!MM_NativeTimeTrial_IsCustom() || (d == NULL) || ((gGT->gameMode1 & START_OF_RACE) != 0))
	{
		return;
	}

	lapSlot = &data.hudStructPtr[0][UI_HUD_SLOT_LAP_COUNT];
	right = lapSlot->x;
	y = lapSlot->y + MM_NATIVE_TT_HUD_FIRST_ROW_Y;

	MM_NativeTimeTrial_FormatTime(timeText, sizeof(timeText), s_ttHaveBest ? s_ttBest.total : -1, 1, 0);
	DecalFont_DrawLine(timeText, (s16)right, (s16)y, FONT_SMALL, (s16)(JUSTIFY_RIGHT | PERIWINKLE));
	DecalFont_DrawLine(s_ttBestLabel, (s16)(right - DecalFont_GetLineWidth(timeText, FONT_SMALL)), (s16)y, FONT_SMALL, (s16)(JUSTIFY_RIGHT | RED));

	laps = (int)gGT->numLaps;
	laps = (laps > MM_NATIVE_TT_MAX_LAPS) ? MM_NATIVE_TT_MAX_LAPS : laps;
	lapIndex = (int)d->lapIndex;

	for (n = 0; (n < laps) && (n <= lapIndex); n++)
	{
		const int ticks = (n < lapIndex) ? gGT->lapTime[n] : (gGT->elapsedEventTime - d->lapTime);

		y += MM_NATIVE_TT_HUD_ROW_STEP;
		snprintf(label, sizeof(label), "L%d", n + 1);
		MM_NativeTimeTrial_FormatTime(timeText, sizeof(timeText), ticks, 1, 1);
		DecalFont_DrawLine(timeText, (s16)right, (s16)y, FONT_SMALL, (s16)(JUSTIFY_RIGHT | PERIWINKLE));
		DecalFont_DrawLine(label, (s16)(right - DecalFont_GetLineWidth(timeText, FONT_SMALL)), (s16)y, FONT_SMALL, (s16)(JUSTIFY_RIGHT | RED));
	}
}

// The finish, once per race: compare with the stored best, write the row if
// the total or the best lap is better, one log line.
internal void MM_NativeTimeTrial_Finish(const struct Driver *d)
{
	const struct GameTracker *gGT = sdata->gGT;
	const int laps = ((int)gGT->numLaps > MM_NATIVE_TT_MAX_LAPS) ? MM_NATIVE_TT_MAX_LAPS : (int)gGT->numLaps;
	const int total = d->timeElapsedInRace;
	struct MMNativeTimeRow row;
	char timeText[MM_NATIVE_TT_TIME_TEXT];
	char bestText[MM_NATIVE_TT_TIME_TEXT];
	char lapText[MM_NATIVE_TT_TIME_TEXT];
	char fileNote[320];
	const char *problem = NULL;
	int bestLap = 0;
	int n;

	for (n = 0; n < laps; n++)
	{
		if ((gGT->lapTime[n] > 0) && ((bestLap == 0) || (gGT->lapTime[n] < bestLap)))
		{
			bestLap = gGT->lapTime[n];
		}
	}

	// "best" in the log line is the best BEFORE this run.
	MM_NativeTimeTrial_FormatTime(timeText, sizeof(timeText), total, 0, 0);
	MM_NativeTimeTrial_FormatTime(bestText, sizeof(bestText), s_ttHaveBest ? s_ttBest.total : -1, 0, 0);
	MM_NativeTimeTrial_FormatTime(lapText, sizeof(lapText), bestLap, 0, 0);

	memset(&row, 0, sizeof(row));
	MM_NativeTimeTrial_DriverName(row.driver, sizeof(row.driver));

	if (!s_ttKey.ok)
	{
		snprintf(fileNote, sizeof(fileNote), "file not used: %s", s_ttKey.why);
	}
	else if (MM_NativeTimeTrial_Locked())
	{
		snprintf(fileNote, sizeof(fileNote), "file not used: settings locked");
	}
	else if ((total <= 0) || (bestLap <= 0))
	{
		snprintf(fileNote, sizeof(fileNote), "file not used: no time measured");
	}
	else
	{
		s_ttNewRecord = !s_ttHaveBest || (total < s_ttBest.total);
		s_ttNewLap = !s_ttHaveBest || (bestLap < s_ttBest.lap);

		if (s_ttNewRecord || s_ttNewLap)
		{
			row.total = s_ttNewRecord ? total : s_ttBest.total;
			row.lap = s_ttNewLap ? bestLap : s_ttBest.lap;

			if (s_ttNewRecord)
			{
				// The wall clock only here, for the date in the file.
				time_t now = time(NULL);
				struct tm *local = localtime(&now);

				if ((local == NULL) || (strftime(row.date, sizeof(row.date), "%Y-%m-%d", local) == 0))
				{
					snprintf(row.date, sizeof(row.date), "-");
				}
			}
			else
			{
				snprintf(row.driver, sizeof(row.driver), "%s", s_ttBest.driver);
				snprintf(row.date, sizeof(row.date), "%s", s_ttBest.date);
			}

			problem = MM_NativeTimeTrial_Store(&s_ttKey, &row);
			if (problem == NULL)
			{
				s_ttBest = row;
				s_ttHaveBest = 1;
			}
		}

		snprintf(fileNote, sizeof(fileNote), "file %s %s: id %d, %s, ticks %d, best lap %s ticks %d%s, driver '%s'", MM_NATIVE_TT_FILE,
		         (problem != NULL) ? "NOT written" : ((s_ttNewRecord || s_ttNewLap) ? "written" : "unchanged"), s_ttKey.id, s_ttKey.file, total, lapText,
		         bestLap, s_ttNewLap ? " NEW BEST LAP" : "", row.driver);
	}

	Platform_Log("[CTR Menu] TIME TRIAL finish: '%s' laps %d time %s best %s - %s (%s)\n", NativeTrack_LoadedName(), (int)gGT->numLaps, timeText, bestText,
	             s_ttNewRecord ? "NEW RECORD" : "no record", fileNote);

	if (problem != NULL)
	{
		Platform_LogWarn("[CTR Menu] TIME TRIAL: %s NOT written - %s; the old best times are kept\n", MM_NATIVE_TT_FILE, problem);
	}
}

// From AA_EndEvent_DrawMenu (222.c), every frame of the results: the finish
// once, then the best and NEW RECORD (or NEW BEST LAP) in the font and the
// blink of the retail time trial results (224.c).
void MM_NativeTimeTrial_EndFrame(void)
{
	const struct GameTracker *gGT = sdata->gGT;
	const struct Driver *d = gGT->drivers[0];
	char line[MM_NATIVE_TT_TIME_TEXT + 8];
	char timeText[MM_NATIVE_TT_TIME_TEXT];
	const s16 blink = (gGT->timer & 1) ? (s16)(JUSTIFY_CENTER | ORANGE) : (s16)(JUSTIFY_CENTER | WHITE);

	if (d == NULL)
	{
		return;
	}

	if (!s_ttEndDone)
	{
		s_ttEndDone = 1;
		if (((d->actionsFlagSet & ACTION_RACE_FINISHED) != 0) && ((int)d->lapIndex >= (int)gGT->numLaps))
		{
			MM_NativeTimeTrial_Finish(d);
		}
		else
		{
			Platform_Log("[CTR Menu] TIME TRIAL end without a finish (lap %d of %d) - nothing compared, nothing written\n", (int)d->lapIndex,
			             (int)gGT->numLaps);
		}
	}

	MM_NativeTimeTrial_FormatTime(timeText, sizeof(timeText), s_ttHaveBest ? s_ttBest.total : -1, 0, 0);
	snprintf(line, sizeof(line), "%s %s", s_ttBestLabel, timeText);
	DecalFont_DrawLine(line, 0x100, MM_NATIVE_TT_END_BEST_Y, FONT_BIG, (s16)(JUSTIFY_CENTER | ORANGE));

	// Retail time trial shows NEW HIGH SCORE and NEW BEST LAP one below the
	// other (224.c). Here a third line in the big font does not fit between
	// the driver icon row and the end box: a record shows NEW RECORD only, the
	// better lap is in the file and the log line.
	if (s_ttNewRecord)
	{
		DecalFont_DrawLine(s_ttNewRecordText, 0x100, MM_NATIVE_TT_END_NEWS_Y, FONT_BIG, blink);
	}
	else if (s_ttNewLap)
	{
		DecalFont_DrawLine(sdata->lngStrings[LNG_NEW_BEST_LAP], 0x100, MM_NATIVE_TT_END_NEWS_Y, FONT_BIG, blink);
	}
}

// THE END BOX: the rows and the proc of menu222 (RETRY, CHANGE LEVEL,
// CHANGE CHARACTER, QUIT), lower down. RETRY restarts the level and the
// time trial with it; the others load the menu, where the marker is cleared.
extern struct MenuRow rows222[5];

internal void MM_NativeTimeTrial_EndMenuProc(struct RectMenu *menu)
{
	if ((menu->funcState == RECTMENU_FUNC_STATE_INPUT) && (menu->rowSelected >= 0))
	{
		const s16 option = menu->rows[menu->rowSelected].stringIndex;
		const char *what = "QUIT - back to the title";

		if (option == LNG_RETRY)
		{
			what = "RETRY - the same time trial again";
		}
		else if (option == LNG_CHANGE_LEVEL)
		{
			what = "CHANGE LEVEL - back to the track screen";
		}
		else if (option == LNG_CHANGE_CHARACTER)
		{
			what = "CHANGE CHARACTER - back to the driver select";
		}

		Platform_Log("[CTR Menu] TIME TRIAL end box: %s\n", what);
	}

	UI_RaceEnd_MenuProc(menu);
}

global_variable struct RectMenu s_nativeTimeTrialEndMenu = {
    .stringIndexTitle = RECTMENU_STRING_NONE,
    .posX_curr = 0x100,
    .posY_curr = MM_NATIVE_TT_END_BOX_Y,
    .unk1 = 0,
    .state = RECTMENU_STATE_SMALL_CENTERED,
    .rows = rows222,
    .funcPtr = MM_NativeTimeTrial_EndMenuProc,
    .drawStyle = 4,
};

struct RectMenu *MM_NativeTimeTrial_EndMenu(void)
{
	return &s_nativeTimeTrialEndMenu;
}

#endif
