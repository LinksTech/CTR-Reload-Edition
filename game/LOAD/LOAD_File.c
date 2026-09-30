#include <common.h>

#ifdef CTR_NATIVE
// The host side's container read path. Declared here and not pulled in via a
// platform header: this file goes into the game's unity build
// and sees its headers, not those of the host side. It is done exactly the same way in
// game/DebugMenu.c.
const u8 *NativeTrack_SubfileData(int subfileIndex, u32 *sizeOut);
void Platform_Log(const char *format, ...);
#endif

void LOAD_StringToUpper(char *path)
{
	for (u8 *letter = (u8 *)path; *letter != 0; letter++)
	{
		// if lowercase letter
		if ((u32)(*letter - 0x61) < 0x1a)
		{
			// uppercase
			*letter -= 0x20;
		}
	}
}

#ifdef CTR_NATIVE
#include <platform/native_cd.h>
#endif

int LOAD_InitCDvol(void)
{
#ifndef CTR_NATIVE
	if ((SPU_CURRENT_VOL_L == 0) && (SPU_CURRENT_VOL_R == 0))
	{
		SPU_MASTER_VOL_L = 0x3fff;
		SPU_MASTER_VOL_R = 0x3fff;
	}

	SPU_CD_VOL_L = 0x3fff;
	SPU_CD_VOL_R = 0x3fff;
	SPU_CTRL = 0xc001;

	CD_REG(0) = 2;
	CD_REG(2) = 0x80;
	CD_REG(3) = 0;
	CD_REG(0) = 3;
	CD_REG(1) = 0x80;
	CD_REG(2) = 0;
	CD_REG(3) = 0x20;
#else
	// NOTE(aalhendi): Retail CdInit calls this hook to reset SPU volume
	// state before howl_InitGlobals re-applies game defaults. Native has
	// no CdInit path (CDSYS_Init(0) skips it), so mirror the SPU CD
	// volume write through the native SPU API to keep parity with the
	// retail init ordering. Master volume defaults are already handled
	// by NativeAudio_SpuInit (0x3fff), matching retail's "only if zero"
	// guard.
	SpuSetCommonCDVolume(0x3fff, 0x3fff);
#endif

	return 0;
}

void LOAD_InitCD()
{
#ifdef CTR_NATIVE
	NativeCD_Init();
	CDSYS_Init(0);
	// NOTE(aalhendi): Retail chains LOAD_InitCD -> CDSYS_Init(1) -> CdInit
	// -> LOAD_InitCDvol. Native skips CdInit (no disc), so call the volume
	// hook explicitly to preserve the same init ordering.
	LOAD_InitCDvol();
	return;
#endif

	CDSYS_Init(1);
}

void *LOAD_ReadDirectory(char *filename)
{
	CdlFILE cdlFile;
	u8 buf[8];

	CDSYS_SetMode_StreamData();

	if (CdSearchFile(&cdlFile, filename) == NULL)
	{
		return NULL;
	}

	struct BigHeader *bh = MEMPACK_AllocMem(LOAD_BIGFILE_HEADER_ALLOC_BYTES /*, filename*/);

	// Search for file on disc
	// Set Cd laser to file position
	// Read the bigfile header
	// Wait for read to end
	CdControl(CdlSetloc, (u8 *)&cdlFile, buf);
	if (CdRead(LOAD_BIGFILE_HEADER_SECTORS, (u32 *)bh, CdlModeSpeed) == 0)
	{
		return NULL;
	}

	if (CdReadSync(0, 0) != 0)
	{
		return NULL;
	}

	// Save position
	bh->cdpos = CdPosToInt(&cdlFile.pos);

	// undo header allocation, only use "needed" size
	MEMPACK_ReallocMem(sizeof(struct BigHeader) + sizeof(struct BigEntry) * bh->numEntry);

	sdata->ptrBigfileCdPos_2 = bh;
	return bh;
}

void LOAD_DramFileCallback(struct LoadQueueSlot *lqs)
{
	char *fileBuf = lqs->ptrDestination;
	void (*callback)(struct LoadQueueSlot *) = lqs->callbackFuncPtr;

	if (fileBuf != NULL)
	{
		int ptrMapOffset = *(int *)&fileBuf[0];
		char *realFileBuf = &fileBuf[4];

		if (ptrMapOffset >= 0)
		{
			struct DramPointerMap *dpm = (struct DramPointerMap *)&realFileBuf[ptrMapOffset];

#if defined(CTR_NATIVE)
			// The map must lie inside the file BEFORE it is read.
			//
			// Layout: word 0 is ptrMapOffset, the body starts at 4 and is
			// exactly ptrMapOffset bytes long - that is what the line further down says, which
			// trims the allocation to ptrMapOffset + 4. At body +
			// ptrMapOffset lies numBytes, followed by numBytes/4 references.
			//
			// Without this check dpm->numBytes is read PAST the end of the file
			// and is then some number from foreign memory, which a loop
			// runs on right afterwards.
			//
			// And ptrMapOffset is passed on unchecked a second time below, to
			// MEMPACK_ReallocMem - an allocation that can grow to a number from the file.
			// So that one depends on this answer too.
			int mapOk = 1;
			{
				extern int g_cfg_ptrMapChecked;
				const u32 size = lqs->size_UNUSED;
				const u32 mapAt = 4u + (u32)ptrMapOffset;

				if (g_cfg_ptrMapChecked)
				{
					if ((mapAt + 4u) > size)
					{
						Platform_LogError("[CTR Ptr] REFUSED: the pointer map starts at %u, the file is %u bytes - nothing patched\n", mapAt, size);
						mapOk = 0;
					}
					else
					{
						const u32 mapBytes = (u32)dpm->numBytes;

						if (((mapBytes & 3u) != 0u) || ((mapAt + 4u + mapBytes) > size))
						{
							Platform_LogError("[CTR Ptr] REFUSED: the pointer map claims %u bytes at %u, the file is %u - nothing patched\n", mapBytes,
							                  mapAt, size);
							mapOk = 0;
						}
					}
				}
			}

			if (mapOk)
#endif
			{
				LOAD_RunPtrMap(realFileBuf, ptrMapOffset, (int *)DRAM_GETOFFSETS(dpm), dpm->numBytes >> 2);
			}

#if defined(CTR_NATIVE)
			if (mapOk && ((lqs->flags & LT_MEMPACK) != 0))
#else
			if ((lqs->flags & LT_SETADDR) != 0)
#endif
			{
				MEMPACK_ReallocMem(ptrMapOffset + 4);
			}
		}
		else
		{
			lqs->flags |= LT_GETADDR;
		}

		lqs->ptrDestination = &fileBuf[4];
	}

#if defined(CTR_NATIVE)
	// NOTE(aalhendi): CTR_NATIVE keeps host callback pointers and queue sentinels.
	if ((callback != NULL) && (callback != LOAD_DramFileCallback) && (callback != (void (*)(struct LoadQueueSlot *))-1) &&
	    (callback != LOAD_QUEUE_CALLBACK_SET_POINTER))
#else
	if ((callback != NULL) && (((u32)(u32)callback & 0xff000000) == 0x80000000))
#endif
	{
		callback(lqs);
	}

	sdata->queueReady = 1;
}

void *LOAD_DramFile(void *bigfilePtr, int subfileIndex, void *ptrDestination, u32 *sizePtr, int callbackOrFlags)
{
	struct LoadQueueSlot lqs;
	void *loadedFile;

	if (callbackOrFlags == -1)
	{
		loadedFile = LOAD_ReadFile_ex(bigfilePtr, LT_GETADDR, subfileIndex, ptrDestination, sizePtr, NULL);

		lqs.ptrBigfileCdPos_UNUSED = bigfilePtr;
		lqs.flags = 0;
		lqs.type_UNUSED = LT_DRAM;
		lqs.subfileIndex = subfileIndex;
		lqs.ptrDestination = loadedFile;
		lqs.size_UNUSED = *sizePtr;
		lqs.callbackFuncPtr = NULL;

		LOAD_DramFileCallback(&lqs);

		return loadedFile;
	}

	if (callbackOrFlags == -2)
	{
		loadedFile = LOAD_ReadFile_ex(bigfilePtr, LT_GETADDR, subfileIndex, NULL, sizePtr, LOAD_DramFileCallback);
		data.currSlot.ptrDestination = loadedFile;
		*(void **)ptrDestination = loadedFile;
		return loadedFile;
	}

	return LOAD_ReadFile_ex(bigfilePtr, LT_GETADDR, subfileIndex, ptrDestination, sizePtr, LOAD_DramFileCallback);
}

void LOAD_VramFileCallback(struct LoadQueueSlot *lqs)
{
	int *vramBuf = lqs->ptrDestination;

	struct VramHeader *vh = (struct VramHeader *)vramBuf;

	// if just one TIM
	if ((vramBuf != NULL) && (vramBuf[0] != 0x20))
	{
		LoadImage(&vh->rect, VRAMHEADER_GETPIXLES(vh));
	}

	// if multiple TIMs are packed together
	if ((vramBuf != NULL) && (vramBuf[0] == 0x20))
	{
		int size;
		vramBuf++;

		size = vramBuf[0];
		vh = (struct VramHeader *)&vramBuf[1];

		while (size != 0)
		{
			LoadImage(&vh->rect, VRAMHEADER_GETPIXLES(vh));

			// goto next
			vramBuf = (int *)((u8 *)vh + (size & ~3));

			size = vramBuf[0];
			vh = (struct VramHeader *)&vramBuf[1];
		}
	}

	// LOAD_NextQueuedFile waits 3 vsync frames before releasing the queue.
	sdata->frameFinishedVRAM = sdata->gGT->frameTimer_VsyncCallback;
}

void *LOAD_VramFile(void *bigfilePtr, int subfileIndex, void *ptrDestination, u32 *sizePtr, int callbackOrFlags)
{
	struct LoadQueueSlot lqs;
	void *loadedFile;

	if (ptrDestination == NULL)
	{
		MEMPACK_PushState();
	}

	if (callbackOrFlags == -1)
	{
		loadedFile = LOAD_ReadFile_ex(bigfilePtr, LT_VRAM, subfileIndex, ptrDestination, sizePtr, NULL);

		lqs.ptrBigfileCdPos_UNUSED = bigfilePtr;
		lqs.flags = 0;
		lqs.type_UNUSED = LT_VRAM;
		lqs.subfileIndex = subfileIndex;
		lqs.ptrDestination = loadedFile;
		lqs.size_UNUSED = *sizePtr;
		lqs.callbackFuncPtr = NULL;

		LOAD_VramFileCallback(&lqs);

		VSync(2);
		sdata->frameFinishedVRAM = 0;

		if (ptrDestination == NULL)
		{
			MEMPACK_PopState();
		}

		return loadedFile;
	}

	if (callbackOrFlags == -2)
	{
		loadedFile = LOAD_ReadFile_ex(bigfilePtr, LT_VRAM, subfileIndex, NULL, sizePtr, LOAD_VramFileCallback);
		data.currSlot.ptrDestination = loadedFile;
		*(void **)ptrDestination = loadedFile;
		return loadedFile;
	}

	return LOAD_ReadFile_ex(bigfilePtr, LT_VRAM, subfileIndex, ptrDestination, sizePtr, LOAD_VramFileCallback);
}

void LOAD_ReadFileASyncCallback(u8 result, u8 *unk)
{
	(void)unk;
	CdReadCallback(0);
	result &= 0xff;

	struct LoadQueueSlot *lqs = &data.currSlot;

	if (result == CdlComplete)
	{
#if defined(CTR_NATIVE)
		if ((lqs->flags & LT_MEMPACK) != 0)
#else
		if ((lqs->flags & LT_SETADDR) != 0)
#endif
		{
			MEMPACK_ReallocMem(lqs->size_UNUSED);
		}

		if (sdata->callbackCdReadSuccess != NULL)
		{
			sdata->callbackCdReadSuccess(lqs);
		}
	}

	// CdlDiskError
	else
	{
#if defined(CTR_NATIVE)
		if ((lqs->flags & LT_MEMPACK) != 0)
#else
		if ((lqs->flags & LT_SETADDR) != 0)
#endif
		{
			// undo allocation, try again
			MEMPACK_PopState();
		}

		sdata->queueRetry = 1;
	}
}

void *LOAD_ReadFile_ex(struct BigHeader *bigfile, u32 loadType, int subfileIndex, void *ptrDst, u32 *sizePtr, void (*callback)(struct LoadQueueSlot *))
{
	int uVar5;
	CdlLOC cdLoc;
	u8 paramOutput[8];
	void *originalDst;
	int sectorSize;
	int sectorCount;
	int readComplete;

	(void)loadType;
	CDSYS_SetMode_StreamData();

#if defined(CTR_NATIVE)
	// NOTE(aalhendi): CTR_NATIVE preserves existing queues that pass 0 for the
	// default bigfile; retail callers are expected to pass the real pointer.
	if (bigfile == NULL)
	{
		bigfile = sdata->ptrBigfile1;
	}
#endif

	// get size and offset of subfile
	struct BigEntry *entry = BIG_GETENTRY(bigfile);
	int eSize = entry[subfileIndex].size;
	int eOffs = entry[subfileIndex].offset;

	*sizePtr = eSize;

	CdIntToPos(bigfile->cdpos + eOffs, &cdLoc);

	struct LoadQueueSlot *lqs = &data.currSlot;
	originalDst = ptrDst;
	sectorCount = (eSize + LOAD_CD_DATA_SECTOR_ROUND_MASK) >> LOAD_CD_DATA_SECTOR_SHIFT;
	readComplete = 1;

#if defined(CTR_NATIVE)
	// A loaded .rldtrack replaces exactly TWO files of this load:
	// the LEV and the VRAM of the donor slot. Drivers, models, music and everything
	// else still come from the bigfile, and the rest of this function learns
	// nothing of it.
	//
	// Here and not further up, because this is the one place both
	// pass through: the LEV via LOAD_DramFile, the VRAM via LOAD_VramFile. Two
	// branches would be two chances for one of them to fall behind.
	//
	// NOTHING is unpacked. The bytes lie de-obfuscated in the host side's
	// memory and are copied from there - no temp folder, no file
	// next to the exe.
	{
		u32 containerSize = 0;
		const u8 *containerData = NativeTrack_SubfileData(subfileIndex, &containerSize);

		if (containerData != NULL)
		{
			*sizePtr = containerSize;
			sectorCount = ((int)containerSize + LOAD_CD_DATA_SECTOR_ROUND_MASK) >> LOAD_CD_DATA_SECTOR_SHIFT;

			// From here on, line by line, what the CD path below does too. The
			// memory bookkeeping is the host's: a container track that
			// allocates differently from a disc track would be a second
			// memory world and the first place where the two drift apart.
			if (ptrDst == NULL)
			{
				const int needed = sectorCount << LOAD_CD_DATA_SECTOR_SHIFT;

				// Asked beforehand whether it fits.
				//
				// MEMPACK_AllocMem checks by itself, but its answer to "does not
				// fit" is CTR_ErrorScreen followed by for(;;) - the game
				// then stalls and says nothing. A foreign track may be
				// larger than the one whose slot it occupies, and then
				// you want to see the two numbers and not guess.
				//
				// In that case we do NOT substitute. What loads is the track that sits on
				// the slot - wrong, but running, and the log says why.
				if (MEMPACK_GetFreeBytes() < needed)
				{
					Platform_Log("[CTR Tracks] NO ROOM for subfile %d: %d bytes needed, %d free in the pack\n", subfileIndex, needed,
					             MEMPACK_GetFreeBytes());
					Platform_Log("[CTR Tracks] not substituted - the stock track on this slot loads instead\n");
					goto ctrNativeNoSubstitution;
				}

				lqs->flags |= LT_MEMPACK;

				ptrDst = (void *)MEMPACK_AllocMem(needed);
			}
			else
			{
				lqs->flags &= ~LT_MEMPACK;
			}

			memcpy(ptrDst, containerData, containerSize);

			lqs->ptrDestination = ptrDst;
			lqs->size_UNUSED = containerSize;

			// The native CdRead calls its callback immediately, even before it
			// returns - see platform/native_cd.c. So copying is
			// not "synchronous instead of asynchronous", but the same sequence.
			if (callback != NULL)
			{
				if ((lqs->flags & LT_MEMPACK) != 0)
				{
					MEMPACK_ReallocMem(lqs->size_UNUSED);
				}

				callback(lqs);
			}
			else if (originalDst == NULL)
			{
				MEMPACK_ReallocMem(*sizePtr);
			}

			Platform_Log("[CTR Tracks] subfile %d copied into the pack at %p, %u bytes\n", subfileIndex, ptrDst, containerSize);
			return ptrDst;
		}
	}

ctrNativeNoSubstitution:
	// Back to the disc file's numbers, in case they were already computed above.
	*sizePtr = eSize;
	sectorCount = (eSize + LOAD_CD_DATA_SECTOR_ROUND_MASK) >> LOAD_CD_DATA_SECTOR_SHIFT;
#endif

	// If no address given, then find one.
	if (ptrDst == NULL)
	{
#if defined(CTR_NATIVE)
		lqs->flags |= LT_MEMPACK;
#else
		lqs->flags |= LT_SETADDR;
#endif

		// allocate room for all sectors,
		// remove alignment before next Read
		sectorSize = sectorCount << LOAD_CD_DATA_SECTOR_SHIFT;
		ptrDst = (void *)MEMPACK_AllocMem(sectorSize); // "FILE"
		if (ptrDst == NULL)
		{
			return NULL;
		}
	}
	else
	{
#if defined(CTR_NATIVE)
		lqs->flags &= ~LT_MEMPACK;
#else
		lqs->flags &= ~LT_SETADDR;
#endif
	}

#if defined(CTR_NATIVE)
	// NOTE(aalhendi): native CD reads can call back before wrapper callers store
	// the returned pointer back into data.currSlot.
	lqs->ptrDestination = ptrDst;
	lqs->size_UNUSED = eSize;
#endif

	while (1)
	{
		uVar5 = CdControl(CdlSetloc, (u8 *)&cdLoc, &paramOutput[0]);

		if (callback != NULL)
		{
			sdata->callbackCdReadSuccess = callback;
			CdReadCallback(LOAD_ReadFileASyncCallback);
		}
		else
		{
			sdata->callbackCdReadSuccess = NULL;
			CdReadCallback(NULL);
		}

		uVar5 &= CdRead(sectorCount, ptrDst, CdlModeSpeed);

		if (callback == NULL)
		{
			// Wait for all sectors to finish
			readComplete = CdReadSync(0, (u8 *)0x0) == 0;
		}

		// If either command failed, or sync read did not finish, retry.
		if ((uVar5 != 0) && (readComplete != 0))
		{
			break;
		}
	}

	if ((callback == NULL) && (originalDst == NULL))
	{
		MEMPACK_ReallocMem(*sizePtr);
	}

	return ptrDst;
}

// Used for XNF and only the XNF
void *LOAD_XnfFile(char *filename, void *ptrDestination, int *size)
{
	CdlFILE cdlFile;

	LOAD_StringToUpper(filename);
	CDSYS_SetMode_StreamData();

	if (CdSearchFile(&cdlFile, filename) == 0)
	{
		return ptrDestination;
	}

	*size = cdlFile.size;

	int allocated = ptrDestination == NULL;
	if (allocated)
	{
		// allocate room for all sectors,
		// remove alignment before next Read
		int sectorSize = (cdlFile.size + LOAD_CD_DATA_SECTOR_ROUND_MASK) & LOAD_CD_DATA_SECTOR_ALIGN_MASK;
		ptrDestination = MEMPACK_AllocMem(sectorSize /*, fileName*/);
		if (ptrDestination == NULL)
		{
			return NULL;
		}
	}

	u8 buf[8];
	CdControl(CdlSetloc, (u8 *)&cdlFile, buf);

	if (CdRead((cdlFile.size + LOAD_CD_DATA_SECTOR_ROUND_MASK) >> LOAD_CD_DATA_SECTOR_SHIFT, ptrDestination, CdlModeSpeed) == 0)
	{
		return 0;
	}

	if (CdReadSync(0, 0))
	{
		return 0;
	}

	if (allocated)
	{
		MEMPACK_ReallocMem(cdlFile.size);
	}

	return ptrDestination;
}

int LOAD_FindFile(char *filename, CdlFILE *cdlFile)
{
	if (filename == 0)
	{
		return 0;
	}
	if (cdlFile == 0)
	{
		return 0;
	}

	CDSYS_SetMode_StreamData();
	LOAD_StringToUpper(filename);

	return (CdSearchFile(cdlFile, filename) != 0);
}
