#include <common.h>
#include <platform/native_memcard.h>

static u8 s_memcardNativeInfoSeen[2];

// ALL DRIVERS ARE UNLOCKED, THE SAVE STAYS.
//
// The seven drivers that the original only unlocks later - N. Tropy, Penta,
// Roo, Papu, Joe, Pinstripe, Fake Crash (UNLOCK_CHARACTERS,
// namespace_Main.h:232-239, bits 5..11 in gameProgress.unlocks[0]) - can be
// chosen from the start. The character select reads the bits directly
// (MM_Characters.c:127, :402, :550, :722; the icon loops :1036 and :1150 via
// MM_NativeCharGrid_TileDrawn -> MM_NativeCharGrid.c:602), so they are set in the
// game: NativeUnlock_ApplyToGame sets them in every menu frame
// (native_menuscreen.c, NativeMenuLock_Tick). Adventure chooses in the
// garage from a fixed list 0..7 (D233.c:31) and does not read the bits -
// there it stays as in the original. The bots do not depend on it
// (LOAD_Robots1P, LOAD_Assets.c:151-166: fixed 0..7 without the player).
//
// They do NOT go onto the card. Every save of a game state goes through
// MEMCARD_Save with the file SLOTS (0x1680, RefreshCard.c:293-297), and there,
// BEFORE the checksum, every one of these bits is removed that the player does not
// have themselves:
// - it was already on the card at the last read of the card (NativeUnlock_NoteCard),
// - or it is earned: the boss driver i with the gem 0x6a+i in an
//   adventure save, just as UI_CupStandings.c:699-707 sets both together;
//   N. Tropy with TT_NTROPY_BEATEN on all 18 tracks (224.c:50-54,
//   GAMEPROG.c:157-179). Penta cannot be earned in the NTSC-U code, only by
//   the retail cheat code.
// A save that is loaded and saved thus stays byte-identical. What
// is lost: a retail unlock code for a driver is no longer
// saved - the driver is unlocked anyway.
//
// The same applies to SCRAPBOOK (bit 36), if --unlock-scrapbook set it:
// only what was on the card comes back.
#define NATIVE_UNLOCK_SLOTS_SIZE 0x1680

extern int g_cfg_unlockScrapbook;

static u32 s_nativeUnlockCard[GAME_PROGRESS_UNLOCK_WORD_COUNT];

void NativeUnlock_ApplyToGame(void)
{
	sdata->gameProgress.unlocks[0] |= UNLOCK_CHARACTERS;
}

// When reading the file SLOTS: what is on the card?
static void NativeUnlock_NoteCard(const u8 *buffer, int size)
{
	const struct MemcardProfile *profile = (const struct MemcardProfile *)buffer;
	int i;

	if ((buffer == NULL) || (size != NATIVE_UNLOCK_SLOTS_SIZE))
	{
		return;
	}

	for (i = 0; i < GAME_PROGRESS_UNLOCK_WORD_COUNT; i++)
	{
		s_nativeUnlockCard[i] = profile->gameProgress.unlocks[i];
	}
}

// The driver bits that the player has earned in this save.
static u32 NativeUnlock_Earned(const struct MemcardProfile *profile)
{
	u32 earned = 0;
	int boss;
	int k;
	int track;
	int tropy = 1;

	for (boss = 0; boss < 5; boss++)
	{
		const int gem = ADV_REWARD_FIRST_GEM + boss;
		int has = CHECK_ADV_BIT(sdata->advProgress.rewards, gem) != 0;

		for (k = 0; k < MEMCARD_ADV_PROFILE_COUNT; k++)
		{
			if ((profile->advProgress[k].characterID >= 0) && CHECK_ADV_BIT(profile->advProgress[k].rewards, gem))
			{
				has = 1;
			}
		}

		if (has)
		{
			earned |= MEMCARD_BIT_MASK(GAME_UNLOCK_BIT_BOSS_CHARACTER_FIRST + boss);
		}
	}

	for (track = 0; track < MEMCARD_HIGH_SCORE_TRACK_COUNT; track++)
	{
		if ((profile->gameProgress.highScoreTracks[track].timeTrialFlags & TT_NTROPY_BEATEN) == 0)
		{
			tropy = 0;
		}
	}

	if (tropy)
	{
		earned |= UNLOCK_TROPY;
	}

	return earned;
}

// Before the checksum of the file SLOTS: only what the player has themselves.
static void NativeUnlock_MaskForSave(u8 *buffer, int size)
{
	struct MemcardProfile *profile = (struct MemcardProfile *)buffer;
	u32 keep;
	u32 before;

	if ((buffer == NULL) || (size != NATIVE_UNLOCK_SLOTS_SIZE))
	{
		return;
	}

	before = profile->gameProgress.unlocks[0];
	keep = (s_nativeUnlockCard[0] & UNLOCK_CHARACTERS) | NativeUnlock_Earned(profile);
	profile->gameProgress.unlocks[0] = (before & ~UNLOCK_CHARACTERS) | (before & keep);

	if (g_cfg_unlockScrapbook)
	{
		const u32 bit = MEMCARD_BIT_MASK(GAME_UNLOCK_BIT_SCRAPBOOK);
		const int word = MEMCARD_BIT_WORD(GAME_UNLOCK_BIT_SCRAPBOOK);

		profile->gameProgress.unlocks[word] = (profile->gameProgress.unlocks[word] & ~bit) | (s_nativeUnlockCard[word] & bit);
	}

	if (profile->gameProgress.unlocks[0] != before)
	{
		Platform_Log("[CTR Card] save: driver unlock bits 0x%03x -> 0x%03x (the game has all drivers; the card keeps only its own)\n",
		             (unsigned)(before & UNLOCK_CHARACTERS), (unsigned)(profile->gameProgress.unlocks[0] & UNLOCK_CHARACTERS));
	}
}

static u8 *MEMCARD_NativePrepareIcon(char *iconHeader, int memcardFileSize, u32 saveFlags)
{
	u8 *icon = (u8 *)&data.memcardIcon_PsyqHand[0];

	sdata->memcardIconSize = 0x100;

	if (((saveFlags & MEMCARD_SAVE_FORCE_BACKUP_COPY) == 0) && (((sdata->memcardIconSize + memcardFileSize * 2 + 0x1fff) >> 13) >= 2))
	{
		icon[3] = (sdata->memcardIconSize + memcardFileSize + 0x1fff) >> 13;
		sdata->memcardStatusFlags |= MEMCARD_STATUS_NO_BACKUP_COPY;
	}
	else
	{
		sdata->memcardStatusFlags &= ~MEMCARD_STATUS_NO_BACKUP_COPY;
		icon[3] = (sdata->memcardIconSize + memcardFileSize * 2 + 0x1fff) >> 13;
	}

	for (int i = 0; i < 0x40; i += 2)
	{
		icon[i + 4] = 0x81;
		icon[i + 5] = 0x40;
	}

	if ((iconHeader != NULL) && (iconHeader[0] != '\0'))
	{
		for (int i = 0; (i < 0x40) && (iconHeader[i] != '\0'); i++)
		{
			icon[i + 4] = iconHeader[i];
		}
	}

	return icon;
}

// NOTE(aalhendi): ctr-native adapts host-backed card operations here; the
// retail implementations stay in the retail MEMCARD domain files for non-native
// builds.
void MEMCARD_GetFreeBytes(int slotIdx)
{
	(void)slotIdx;
	sdata->memoryCard_SizeRemaining = 0x1e000;
}

u8 MEMCARD_GetInfo(int slotIdx)
{
	// NOTE(aalhendi): Native treats the host save directory as an inserted card;
	// PSX updates free space while handling the async info event that native skips.
	MEMCARD_GetFreeBytes(slotIdx);

	// NOTE(aalhendi): Report the host directory as a new card once; repeated
	// NEWCARD results make RefreshCard reload the profile forever.
	if (s_memcardNativeInfoSeen[slotIdx & 1] == 0)
	{
		s_memcardNativeInfoSeen[slotIdx & 1] = 1;
		return MC_RETURN_NEWCARD;
	}

	return MC_RETURN_IOE;
}

u8 MEMCARD_Format(int slotIdx)
{
	(void)slotIdx;
	return MC_RETURN_IOE;
}

int MEMCARD_IsFile(int slotIdx, char *save_name)
{
	char nativeName[64];

	MEMCARD_StringSet(nativeName, slotIdx, save_name);
	return NativeMemcard_FileExists(nativeName) ? MC_RETURN_IOE : MC_RETURN_NODATA;
}

char *MEMCARD_FindFirstGhost(int slotIdx, char *srcString)
{
	if (sdata->memcard_stage != MC_STAGE_IDLE)
	{
		return NULL;
	}

	MEMCARD_StringSet(sdata->s_memcardFileCurr, slotIdx, srcString);

	if (NativeMemcard_FindFirstFile(sdata->s_memcardFileCurr, &sdata->s_memcardFindGhostFile[0], sizeof(sdata->s_memcardFindGhostFile)) == 0)
	{
		return NULL;
	}

	sdata->memcard_stage = MC_STAGE_GHOST_FOUND;
	return &sdata->s_memcardFindGhostFile[0];
}

char *MEMCARD_FindNextGhost(void)
{
	if (sdata->memcard_stage != MC_STAGE_GHOST_FOUND)
	{
		return NULL;
	}

	if (NativeMemcard_FindNextFile(&sdata->s_memcardFindGhostFile[0], sizeof(sdata->s_memcardFindGhostFile)) == 0)
	{
		sdata->memcard_stage = MC_STAGE_IDLE;
		return NULL;
	}

	return &sdata->s_memcardFindGhostFile[0];
}

u8 MEMCARD_EraseFile(int slotIdx, char *srcString)
{
	char nativeName[64];

	MEMCARD_StringSet(nativeName, slotIdx, srcString);
	return NativeMemcard_RemoveFile(nativeName) == NATIVE_MEMCARD_OK ? MC_RETURN_IOE : MC_RETURN_NODATA;
}

int MEMCARD_HandleEvent(void)
{
	// Native MEMCARD operations complete synchronously in this adapter. If the
	// retail polling path reaches here, report a timeout instead of touching PSX
	// card event APIs.
	return MC_RETURN_TIMEOUT;
}

u8 MEMCARD_Load(int slotIdx, char *name, u8 *ptrMemcard, int memcardFileSize, u32 loadFlags)
{
	char nativeName[64];
	int checksumResult;

	(void)loadFlags;

	MEMCARD_StringSet(nativeName, slotIdx, name);
	enum NativeMemcardResult nativeResult = NativeMemcard_ReadSaveData(nativeName, ptrMemcard, memcardFileSize, 0x100);
	if (nativeResult == NATIVE_MEMCARD_NOT_FOUND)
	{
		return MC_RETURN_NODATA;
	}

	if (nativeResult != NATIVE_MEMCARD_OK)
	{
		return MC_RETURN_TIMEOUT;
	}

	sdata->crc16_checkpoint_byteIndex = 0;
	sdata->crc16_checkpoint_status = 0;
	do
	{
		checksumResult = MEMCARD_ChecksumLoad(ptrMemcard, memcardFileSize);
	} while (checksumResult == MC_RETURN_PENDING);

	NativeUnlock_NoteCard(ptrMemcard, memcardFileSize);

	return checksumResult == MC_RETURN_IOE ? MC_RETURN_IOE : MC_RETURN_TIMEOUT;
}

u8 MEMCARD_Save(int slotIdx, char *name, char *icon, u8 *ptrMemcard, int memcardFileSize, u32 saveFlags)
{
	char nativeName[64];

	NativeUnlock_MaskForSave(ptrMemcard, memcardFileSize);

	sdata->crc16_checkpoint_byteIndex = 0;
	sdata->crc16_checkpoint_status = 0;
	MEMCARD_ChecksumSave(ptrMemcard, memcardFileSize);

	u8 *cardIcon = MEMCARD_NativePrepareIcon(icon, memcardFileSize, saveFlags);
	MEMCARD_StringSet(nativeName, slotIdx, name);
	enum NativeMemcardResult nativeResult = NativeMemcard_WriteSaveData(nativeName, cardIcon, sdata->memcardIconSize, ptrMemcard, memcardFileSize);
	if (nativeResult == NATIVE_MEMCARD_OPEN_FAILED)
	{
		return MC_RETURN_FULL;
	}

	if (nativeResult != NATIVE_MEMCARD_OK)
	{
		return MC_RETURN_TIMEOUT;
	}

	return MC_RETURN_IOE;
}
