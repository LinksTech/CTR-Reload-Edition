#include <common.h>

void LOAD_Callback_Overlay_Generic(struct LoadQueueSlot *lqs)
{
	(void)lqs;
	sdata->load_inProgress = 0;
}

void LOAD_Callback_Overlay_230(void)
{
	sdata->load_inProgress = 0;
	sdata->gGT->overlayIndex_Threads = OVERLAY_INDEX_MAIN_MENU;
}

void LOAD_Callback_Overlay_231(void)
{
	sdata->load_inProgress = 0;
	sdata->gGT->overlayIndex_Threads = OVERLAY_INDEX_RACING_OR_BATTLE;
}

void LOAD_Callback_Overlay_232(void)
{
	sdata->load_inProgress = 0;
	sdata->gGT->overlayIndex_Threads = OVERLAY_INDEX_ADV_HUB;
}

void LOAD_Callback_Overlay_233(void)
{
	sdata->load_inProgress = 0;
	sdata->gGT->overlayIndex_Threads = OVERLAY_INDEX_PODIUMS;
}

void LOAD_Callback_MaskHints3D(struct LoadQueueSlot *lqs)
{
	sdata->load_inProgress = 0;
	sdata->modelMaskHints3D = (struct Model *)lqs->ptrDestination;
}

void LOAD_Callback_Podiums(struct LoadQueueSlot *lqs)
{
	sdata->load_inProgress = 0;
	data.podiumModel_podiumStands = (struct Model *)lqs->ptrDestination;
}

#if defined(CTR_NATIVE)
// How large the level structure is that `sdata->ptrLevelFile` points to.
//
// Needed by LOAD_Callback_PatchMem: there the pointer map comes as a SEPARATE
// file, so the call no longer knows how large the thing it points to is -
// unlike in the funnel in LOAD_File.c, where both are in one file.
//
// Here and nowhere else, because `ptrLevelFile` is set in exactly one place in the
// whole tree: the line below. Two setters would be two chances
// for the size to no longer match the pointer.
global_variable int s_levelFileBytes = 0;
#endif

void LOAD_Callback_LEV(struct LoadQueueSlot *lqs)
{
	if ((lqs->flags & LT_GETADDR) == 0)
	{
		sdata->load_inProgress = 0;
	}

	sdata->ptrLevelFile = (struct Level *)lqs->ptrDestination;

#if defined(CTR_NATIVE)
	// The funnel has already trimmed the allocation down to the body and
	// advanced ptrDestination by the first word. What arrives here is the
	// body, and `size_UNUSED` is the whole file including map and
	// first word - so the safe upper bound and not the exact number.
	s_levelFileBytes = (int)lqs->size_UNUSED;
#endif
}

void LOAD_Callback_PatchMem(struct LoadQueueSlot *lqs)
{
	// CTR doesn't load one lev DRAM for AdvHub,
	// it loads one ReadFile for LEV in a sub-mempack,
	// it loads one ReadFile for PtrMap with AllocHighMem

	// that's why the patch map is handled here
	struct DramPointerMap *patchMap = lqs->ptrDestination;
	int patchNum = patchMap->numBytes >> DRAM_POINTER_MAP_WORD_SHIFT;

	sdata->load_inProgress = 0;

#if defined(CTR_NATIVE)
	// Here the map comes as its own file, so it is also held against its own
	// size: `patchNum` is stored in the file and must not point beyond
	// it. The limit for the TARGET comes from LOAD_Callback_LEV.
	{
		extern int g_cfg_ptrMapChecked;
		const u32 have = (lqs->size_UNUSED > sizeof(struct DramPointerMap)) ? (u32)(lqs->size_UNUSED - sizeof(struct DramPointerMap)) : 0u;

		if (g_cfg_ptrMapChecked && (((u32)patchNum * 4u) > have))
		{
			Platform_LogError("[CTR Ptr] REFUSED: the patch file claims %d pointer(s), it holds room for %u - nothing patched\n", patchNum, have / 4u);
			patchNum = 0;
		}
	}

	LOAD_RunPtrMap((char *)sdata->ptrLevelFile, s_levelFileBytes, DRAM_GETOFFSETS(patchMap), patchNum);
#else
	LOAD_RunPtrMap((char *)sdata->ptrLevelFile, 0, DRAM_GETOFFSETS(patchMap), patchNum);
#endif

	MEMPACK_SwapPacks(0);
	MEMPACK_ClearHighMem();
	MEMPACK_SwapPacks(sdata->gGT->activeMempackIndex);
}

void LOAD_Callback_DriverModels(struct LoadQueueSlot *lqs)
{
	sdata->load_inProgress = 0;
	sdata->ptrMPK = (int)lqs->ptrDestination;
}

void LOAD_HubCallback(struct LoadQueueSlot *lqs)
{
	sdata->load_inProgress = 0;
	LOAD_Callback_PatchMem(lqs);

	sdata->gGT->level2 = sdata->ptrLevelFile;
	MEMPACK_SwapPacks(sdata->gGT->activeMempackIndex);
}
