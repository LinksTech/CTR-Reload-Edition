#include <common.h>
#include "platform/native_audio.h"

void Platform_Log(const char *format, ...);

// UPPER LIMIT FOR BANKS IN SOUND RAM. Retail stopped 8 KB before the end of
// the 512 KB (0x7e000). On the PS1 the reverb work area sat at the top: libspu
// puts it at the end, as large as the mode requires - for Studio A 0x1F40 from
// 0x7E0C0, which fits 0x7e000; Room, Pipe and Studio B, which CTR also uses,
// reach down to 0x7B7C0 (sizes in native_audio.c s_reverbPresets). The
// emulation computes the reverb in its own buffer (native_audio.c,
// NativeAudioReverbState.buffer) and does not evaluate any reverb start address;
// memory[] is written only by SpuWrite, read only by the ADPCM decoder and the
// snapshot. So the whole sound RAM belongs to the samples.
//
// SOUND RAM 1 MB. Container tracks can bring larger banks: a music bank of
// 380,896 bytes left no room below 512 KB for bank 54 of the driver select
// (end 533,424). The emulated sound RAM is therefore 1 MB
// (NATIVE_AUDIO_SPU_MEMSIZE in native_audio.h), and a bank may end exactly at
// that limit. The retail fields stay as they are - SpuAddrEntry.spuAddr in the
// KART.HWL header and struct Bank min/max in sData.bank[8] are u16 in 8-byte
// units (ceiling 0x7FFF8), their layout is fixed. Alongside, this file keeps
// the same values in 32 bits (s_spuAddr32, s_bankMin32, s_bankMax32); reads
// happen only there. The retail fields keep getting the same value, saturated
// (Howl_Retail16) - up to 512 KB it is identical, above that nobody reads it
// any more. The pointer sdata->audioAllocPtr is already int.
#define HOWL_SPU_BANK_LIMIT NATIVE_AUDIO_SPU_MEMSIZE
#define HOWL_SPU_RETAIL_LIMIT 0x80000
#define HOWL_SPU_ROWS_CAP 1024
#define HOWL_BANKS 8

static u32 s_spuAddr32[HOWL_SPU_ROWS_CAP]; // start address per SPU row, 8-byte units, 0 = not loaded
static u32 s_bankMin32[HOWL_BANKS];
static u32 s_bankMax32[HOWL_BANKS];

static int Howl_BankIndex(const struct Bank *bank)
{
	const int index = (int)(bank - &sdata->bank[0]);

	return ((index >= 0) && (index < HOWL_BANKS)) ? index : 0;
}

// The retail field gets the value saturated instead of truncated: an address of
// exactly 0x10000 units would be 0 when truncated and would mean "not loaded"
// to any reader that might still exist.
static u16 Howl_Retail16(u32 value)
{
	return (value > 0xFFFFu) ? (u16)0xFFFFu : (u16)value;
}

static void Howl_SetSpuAddr(int row, u32 addr)
{
	if ((row >= 0) && (row < HOWL_SPU_ROWS_CAP))
	{
		s_spuAddr32[row] = addr;
	}

	sdata->howl_spuAddrs[row].spuAddr = Howl_Retail16(addr);
}

// The start address of an SPU row in 8-byte units, 0 = not loaded.
u32 Howl_SpuAddr(int row)
{
	if ((row >= 0) && (row < HOWL_SPU_ROWS_CAP))
	{
		return s_spuAddr32[row];
	}

	return sdata->howl_spuAddrs[row].spuAddr;
}

// When reading the KART.HWL header: in the file every address is 0.
void Howl_SpuAddrReset(int rows)
{
	memset(s_spuAddr32, 0, sizeof(s_spuAddr32));
	memset(s_bankMin32, 0, sizeof(s_bankMin32));
	memset(s_bankMax32, 0, sizeof(s_bankMax32));

	if (rows > HOWL_SPU_ROWS_CAP)
	{
		Platform_Log("[CTR Sound] KART.HWL has %d SPU rows, the 32-bit table holds %d - rows above it stay 16-bit\n", rows, HOWL_SPU_ROWS_CAP);
	}
}

// Save states (native_state.c, region HSPU): the three tables belong
// to the state like the retail fields in the KART.HWL header.
int Howl_SpuState_GetSize(void)
{
	return (int)(sizeof(s_spuAddr32) + sizeof(s_bankMin32) + sizeof(s_bankMax32));
}

int Howl_SpuState_Capture(void *dst, int dstSize)
{
	u8 *bytes = (u8 *)dst;

	if ((dst == NULL) || (dstSize != Howl_SpuState_GetSize()))
	{
		return 0;
	}

	memcpy(bytes, s_spuAddr32, sizeof(s_spuAddr32));
	memcpy(bytes + sizeof(s_spuAddr32), s_bankMin32, sizeof(s_bankMin32));
	memcpy(bytes + sizeof(s_spuAddr32) + sizeof(s_bankMin32), s_bankMax32, sizeof(s_bankMax32));
	return 1;
}

int Howl_SpuState_Restore(const void *src, int srcSize)
{
	const u8 *bytes = (const u8 *)src;

	if ((src == NULL) || (srcSize != Howl_SpuState_GetSize()))
	{
		return 0;
	}

	memcpy(s_spuAddr32, bytes, sizeof(s_spuAddr32));
	memcpy(s_bankMin32, bytes + sizeof(s_spuAddr32), sizeof(s_bankMin32));
	memcpy(s_bankMax32, bytes + sizeof(s_spuAddr32) + sizeof(s_bankMin32), sizeof(s_bankMax32));
	return 1;
}

void Bank_ResetAllocator()
{
	sdata->numAudioBanks = 0;
	sdata->audioAllocPtr = 0x202;
	sdata->bankLoadStage = 4; // Stage 4: Finished
}

int Bank_Alloc(int bankID, struct Bank *ptrBank)
{
	if (sdata->boolAudioEnabled == 0)
	{
		// Stage 4: Complete
		sdata->bankLoadStage = 4;
		return 1;
	}

	// is last bank needed for level?
	sdata->bankFlags = (ptrBank->flags & 1) != 0;

	sdata->bankSectorOffset = sdata->howl_bankOffsets[bankID & 0xffff];

	// ghidra makes this look like a pointer to stack memory,
	// game shows it's a pointer to ram bank[8], what's happening?
	sdata->ptrLastBank = ptrBank;

	// temporary for loading banks to RAM,
	// sending data to SPU, then erasing RAM
	MEMPACK_PushState();

	sdata->ptrSampleBlock2 = MEMPACK_AllocMem(0x800 /*, "SampleBlock"*/);

	if (sdata->ptrSampleBlock2 == 0)
	{
		// no data loaded, PopState
		MEMPACK_PopState();
		return 0;
	}

	// Stage 0: Start chain of events
	// to parse banks and ship to SPU
	sdata->bankLoadStage = 0;

	sdata->ptrSampleBlock1 = sdata->ptrSampleBlock2;
	return 1;
}

int Bank_AssignSpuAddrs()
{
	int i;
	int ret;
	int audioAllocPtr;

	// if Stage 4: Complete
	if (sdata->bankLoadStage == 4)
	{
		return 1;
	}

	// Stage 0: Load to RAM (1/2)
	if (sdata->bankLoadStage == 0)
	{
		ret = LOAD_HowlSectorChainStart(&sdata->KartHWL_CdFile,         // CdLoc of HOWL
		                                (void *)sdata->ptrSampleBlock2, // destination in RAM for banks
		                                sdata->bankSectorOffset,        // bank offset on disc, from CdLoc
		                                1                               // one sector
		);

		if (ret != 0)
		{
			// go to next stage
			sdata->bankLoadStage++;
		}

		return 0;
	}

	// Stage 1: Load to RAM (2/2) and assign SPU Addrs
	if (sdata->bankLoadStage == 1)
	{
		if (LOAD_HowlSectorChainEnd() == 0)
		{
			return 0;
		}

		sdata->audioAllocSize = 0;

		for (i = 0; i < sdata->ptrSampleBlock1->numSamples; i++)
		{
			s16 *spuIndexArr = SBHEADER_GETARR(sdata->ptrSampleBlock1);
			sdata->audioAllocSize += sdata->howl_spuAddrs[spuIndexArr[i]].spuSize;
		}

		// convert bit-shifted count to
		// real SPU byte count, with x8
		sdata->audioAllocSize *= 8;

		// not last bank needed for level
		if (sdata->bankFlags == 0)
		{
			sdata->ptrLastBank->max = sdata->audioAllocSize >> 3;
			s_bankMax32[Howl_BankIndex(sdata->ptrLastBank)] = (u32)sdata->audioAllocSize >> 3;
		}

		// last bank needed for level
		else
		{
			// Naughty Dog bug? No bitshift?
			if (s_bankMax32[Howl_BankIndex(sdata->ptrLastBank)] < (u32)sdata->audioAllocSize)
			{
				// Stage 4: Complete
				sdata->bankLoadStage = 4;
				return 1;
			}
		}

		// === more banks needed ===

		sdata->numAudioSectors = (sdata->audioAllocSize + 0x7ff) >> 0xb;

		MEMPACK_ReallocMem(((sdata->audioAllocSize + 0x7ff) & 0xfffff800) + 0x800);

		ret = LOAD_HowlSectorChainStart(&sdata->KartHWL_CdFile,                        // CdLoc of HOWL
		                                (void *)((int)sdata->ptrSampleBlock2 + 0x800), // destination
		                                sdata->bankSectorOffset + 1,                   // offset of howl
		                                sdata->numAudioSectors                         // number of sectors
		);

		if (ret == 0)
		{
			return 0;
		}

		// not last bank needed?
		if (sdata->bankFlags == 0)
		{
			sdata->ptrLastBank->min = Howl_Retail16((u32)sdata->audioAllocPtr);
			s_bankMin32[Howl_BankIndex(sdata->ptrLastBank)] = (u32)sdata->audioAllocPtr;
			audioAllocPtr = sdata->audioAllocPtr;
		}

		// last bank needed
		else
		{
			audioAllocPtr = (int)s_bankMin32[Howl_BankIndex(sdata->ptrLastBank)];
		}

		// === Assign SpuEntry for all "new" samples ===

		struct SpuAddrEntry *sae;

		for (i = 0; i < sdata->ptrSampleBlock1->numSamples; i++)
		{
			s16 *spuIndexArr = SBHEADER_GETARR(sdata->ptrSampleBlock1);
			sae = &sdata->howl_spuAddrs[spuIndexArr[i]];

			if (Howl_SpuAddr(spuIndexArr[i]) == 0)
			{
				Howl_SetSpuAddr(spuIndexArr[i], (u32)audioAllocPtr);
			}
			audioAllocPtr += sae->spuSize;
		}

		sdata->bankLoadStage++;

		return 0;
	}

	// Stage 2: Spu Transfer Start
	if (sdata->bankLoadStage == 2)
	{
		if (LOAD_HowlSectorChainEnd() == 0)
		{
			return 0;
		}

		int spuAddrStart = (int)(s_bankMin32[Howl_BankIndex(sdata->ptrLastBank)] * 8u);

		// Retail: 0x7e000, 8 KB before the end of the 512 KB; now up to the end of
		// 1 MB (HOWL_SPU_BANK_LIMIT, above). With 32 bits everywhere a bank may
		// end exactly at the end.
		if (spuAddrStart + sdata->audioAllocSize <= HOWL_SPU_BANK_LIMIT)
		{
			// start transfer
			SpuSetTransferStartAddr(spuAddrStart);

			SpuWrite((u8 *)((int)sdata->ptrSampleBlock2 + 0x800), (u32)sdata->audioAllocSize);

			// Above the old limit: say once that the native sound RAM
			// is needed. Retail tracks never get here.
			if (spuAddrStart + sdata->audioAllocSize > HOWL_SPU_RETAIL_LIMIT)
			{
				Platform_Log("[CTR Sound] bank %d at %d..%d ends above 512 KB - it uses the 1 MB sound memory, level %d\n",
				             (int)sdata->ptrLastBank->bankID, spuAddrStart, spuAddrStart + (int)sdata->audioAllocSize, (int)sdata->gGT->levelID);
			}
		}

		// THE OVERFLOW WAS SILENT. If a bank no longer fits below the limit,
		// retail does not transfer it - and says nothing. Its samples got
		// their addresses anyway (Stage 1) and then play whatever lies there
		// in sound RAM. Example: a container track with its own bank 14 and an
		// unlockable driver, where bank 66 ended at 519,104, above the retail
		// limit 0x7e000. What happens to a bank above the limit stays retail;
		// only this line is new.
		else
		{
			Platform_Log("[CTR Sound] bank %d does not fit the sound memory: %d byte(s) from %d end at %d, %d byte(s) over the limit of %d - "
			             "not transferred, level %d\n",
			             (int)sdata->ptrLastBank->bankID, (int)sdata->audioAllocSize, spuAddrStart, spuAddrStart + (int)sdata->audioAllocSize,
			             spuAddrStart + (int)sdata->audioAllocSize - HOWL_SPU_BANK_LIMIT, HOWL_SPU_BANK_LIMIT, (int)sdata->gGT->levelID);
		}

		sdata->bankLoadStage++;

		return 0;
	}

	// Stage 3: Spu Transfer End
	if (sdata->bankLoadStage == 3)
	{
		if (SpuIsTransferCompleted(SPU_TRANSFER_PEEK) == 0)
		{
			return 0;
		}

		if (sdata->bankFlags == 0)
		{
			sdata->audioAllocPtr += sdata->audioAllocSize >> 3;
		}

		sdata->ptrLastBank->flags |= 2;

		// SPU Transfer done, remove bank from RAM
		MEMPACK_PopState();

		sdata->bankLoadStage++;
		return 1;
	}

	return 0;
}

void Bank_Destroy(struct Bank *ptrLastBank)
{
	u16 flags;

	if (sdata->boolAudioEnabled == 0)
	{
		return;
	}

	flags = ptrLastBank->flags;

	Bank_ClearInRange(s_bankMin32[Howl_BankIndex(ptrLastBank)], s_bankMax32[Howl_BankIndex(ptrLastBank)]);

	if ((flags & 1) == 0)
	{
		// this works cause Bank_Destroy
		// is only called on the "last" bank
		sdata->audioAllocPtr = (int)s_bankMin32[Howl_BankIndex(ptrLastBank)];
	}

	ptrLastBank->flags = flags & ~(2);
}

// Retail: u16 min, max and end - end overflowed as soon as a bank reached
// past 0x7FFF8. Now 32 bits, against the 32-bit table.
void Bank_ClearInRange(u32 min, u32 max)
{
	int i;
	u32 end = min + max;

	for (i = 0; i < sdata->ptrHowlHeader->numSpuAddrs; i++)
	{
		const u32 addr = Howl_SpuAddr(i);

		if (addr < min)
		{
			continue;
		}
		if (addr >= end)
		{
			continue;
		}
		Howl_SetSpuAddr(i, 0);
	}
}

int Bank_Load(int bankID, struct Bank *ptrBank)
{
	int numBanks = sdata->numAudioBanks;

	// if out of banks, quit
	if (numBanks >= 8)
	{
		return 0;
	}

	sdata->bank[numBanks].bankID = bankID & 0xffff;

	// if bank is in use, quit
	if ((sdata->bank[numBanks].flags & 3) != 0)
	{
		return 0;
	}

	if (Bank_Alloc(bankID, &sdata->bank[numBanks]) == 0)
	{
		return 0;
	}

	// starting to think this isn't really a bank...
	ptrBank->bankID = sdata->numAudioBanks++;
	return 1;
}

int Bank_DestroyLast()
{
	if (sdata->numAudioBanks == 0)
	{
		return 0;
	}

	Bank_Destroy(&sdata->bank[--sdata->numAudioBanks]);
	return 1;
}

void Bank_DestroyUntilIndex(int index)
{
	struct Bank *ptrLastBank;
	u16 bankID = index;

	while (sdata->numAudioBanks != 0)
	{
		ptrLastBank = &sdata->bank[sdata->numAudioBanks - 1];

		if ((u16)ptrLastBank->bankID == bankID)
		{
			return;
		}

		Bank_DestroyLast();
	}
}

void Bank_DestroyAll()
{
	while (sdata->numAudioBanks != 0)
	{
		Bank_DestroyLast();
	}
}
