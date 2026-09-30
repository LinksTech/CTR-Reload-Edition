#include <common.h>


void Platform_Log(const char *format, ...);

// A PVS list slot carries either an InstDef* (that is how it comes from the LEV)
// or an Instance* (that is how the draw path needs it). The two structures
// lie in separate memories: InstDefs are in the level file, Instances
// come without exception from the instance pool (INSTANCE_LevInitAll takes them from
// gGT->JitPools.instance.free, and JitPool_Clear carves that list out of
// ptrPoolData). So whether a slot is already converted is told by the address.
static int LevInstDef_IsInstance(const struct JitPool *pool, const void *ptr)
{
	const char *lo = (const char *)pool->ptrPoolData;
	const char *hi = lo + (pool->maxItems * JITPOOL_ALIGN_ITEM_STRIDE(pool->itemSize));
	const char *p = (const char *)ptr;

	return (p >= lo) && (p < hi);
}

void LevInstDef_UnPack(struct mesh_info *ptr_mesh_info)
{
	int i;
	int numQuadBlock;
	struct QuadBlock *ptrQuadBlockArray;
	struct QuadBlock *qbCurr;
	struct InstDef **visInstSrc;
	struct Level *level1;
	const struct JitPool *pool = &sdata->gGT->JitPools.instance;
	int numConverted = 0;
	int numAlreadyDone = 0;

	numQuadBlock = ptr_mesh_info->numQuadBlock;
	ptrQuadBlockArray = ptr_mesh_info->ptrQuadBlockArray;

	// loop through all quadblocks
	for (i = 0; i < numQuadBlock; i++)
	{
		qbCurr = &ptrQuadBlockArray[i];

		if ((qbCurr->pvs != 0) && (qbCurr->pvs->visInstSrc != 0))
		{
			// loop through all instance pointers visible on quadblock
			for (visInstSrc = (struct InstDef **)qbCurr->pvs->visInstSrc; visInstSrc[0] != NULL; visInstSrc++)
			{
				// Several quadblocks may share one list (community
				// tracks do that, the disc never). Then the same slot comes by
				// several times. A slot that already points into the instance pool
				// is done - converting it a second time would go via
				// Instance+0x2c back to the InstDef, and the draw path would then read
				// InstDef+0x18 (scale.vz) as a model pointer. Sunset Vista:
				// 13120 visits per slot, crash at 0x3014.
				if (LevInstDef_IsInstance(pool, visInstSrc[0]))
				{
					numAlreadyDone++;
					continue;
				}

				visInstSrc[0] = (struct InstDef *)visInstSrc[0]->ptrInstance;
				numConverted++;
			}
		}
	}

	level1 = sdata->gGT->level1;

	if (level1->ptrInstDefPtrArray != 0)
	{
		// loop through all instDef pointers in the LEV
		for (visInstSrc = level1->ptrInstDefPtrArray; visInstSrc[0] != 0; visInstSrc++)
		{
			if (LevInstDef_IsInstance(pool, visInstSrc[0]))
			{
				numAlreadyDone++;
				continue;
			}

			visInstSrc[0] = (struct InstDef *)visInstSrc[0]->ptrInstance;
			numConverted++;
		}
	}

	Platform_Log("[CTR Lev] LevInstDef_UnPack: %d quadblock(s), %d list slot(s) converted InstDef->Instance, %d already pointed into the instance pool and were left alone\n",
	             numQuadBlock, numConverted, numAlreadyDone);
}


void LevInstDef_RePack(struct mesh_info *ptr_mesh_info, b32 boolAdvHub)
{
	int i;
	int numQuadBlock;
	struct QuadBlock *ptrQuadBlockArray;
	struct QuadBlock *qbCurr;
	struct Instance **visInstSrc;
	struct Level *level1;
	struct Thread *th;
	const struct JitPool *pool = &sdata->gGT->JitPools.instance;
	int numConverted = 0;
	int numAlreadyDone = 0;

	numQuadBlock = ptr_mesh_info->numQuadBlock;
	ptrQuadBlockArray = ptr_mesh_info->ptrQuadBlockArray;

	// loop through all quadblocks
	for (i = 0; i < numQuadBlock; i++)
	{
		qbCurr = &ptrQuadBlockArray[i];

		if ((qbCurr->pvs != 0) && (qbCurr->pvs->visInstSrc != 0))
		{
			// loop through all instance pointers visible on quadblock
			for (visInstSrc = qbCurr->pvs->visInstSrc; visInstSrc[0] != NULL; visInstSrc++)
			{
				// Mirror image of LevInstDef_UnPack: with shared lists the same slot
				// comes by as often as quadblocks use the list.
				// A slot that no longer points into the instance pool
				// is already converted back - once more would go via
				// InstDef+0x2c to the instance again. The result is read
				// on a restart of the race (MainMain.c: LOAD_RESTART -> state
				// 2 -> MainInit -> UnPack on the same LEV in memory).
				if (!LevInstDef_IsInstance(pool, visInstSrc[0]))
				{
					numAlreadyDone++;
					continue;
				}

				visInstSrc[0] = (struct Instance *)visInstSrc[0]->instDef;
				numConverted++;
			}
		}
	}

	level1 = sdata->gGT->level1;

	if (level1->ptrInstDefPtrArray != 0)
	{
		// loop through all instDef pointers in the LEV
		for (visInstSrc = (struct Instance **)level1->ptrInstDefPtrArray; visInstSrc[0] != NULL; visInstSrc++)
		{
			struct Instance *inst = visInstSrc[0];
			struct InstDef *instDef;

			if (!LevInstDef_IsInstance(pool, inst))
			{
				numAlreadyDone++;
				continue;
			}

			instDef = inst->instDef;

			// if on adv hub
			if (boolAdvHub != 0)
			{
				th = inst->thread;
				if (th != 0)
				{
					th->flags |= THREAD_FLAG_DEAD;
				}

				// erase instance in pool
				LIST_AddFront(&sdata->gGT->JitPools.instance.free, (struct Item *)inst);
			}

			// go back to instDef
			visInstSrc[0] = (struct Instance *)instDef;
			numConverted++;
		}
	}

	Platform_Log("[CTR Lev] LevInstDef_RePack: %d quadblock(s), %d list slot(s) converted Instance->InstDef, %d already pointed outside the instance pool and were left alone\n",
	             numQuadBlock, numConverted, numAlreadyDone);

	PROC_CheckAllForDead();
}
