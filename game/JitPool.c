#include <common.h>


void JitPool_Clear(struct JitPool *AP)
{
	u32 currSlot = (u32)AP->ptrPoolData;

	// clear list of free and taken
	LIST_Clear(&AP->free);
	LIST_Clear(&AP->taken);

	for (s32 loopIndex = 0; loopIndex < AP->maxItems; loopIndex++)
	{
		// add all pool items to the free list
		LIST_AddFront(&AP->free, (struct Item *)currSlot);

		currSlot += JITPOOL_ALIGN_ITEM_STRIDE(AP->itemSize);
	}
}


// The pool over memory the caller holds (maxItems * itemSize bytes) - the
// large pools of MainInit_JitPoolsNew, on the PC heap.
void JitPool_InitAt(struct JitPool *AP, int maxItems, int itemSize, char *name, void *poolData)
{
	(void)name;

	memset(AP, 0, sizeof(struct JitPool));
	AP->maxItems = maxItems;
	AP->itemSize = itemSize;
	AP->poolSize = maxItems * itemSize;
	AP->ptrPoolData = poolData;
	JitPool_Clear(AP);
}

void JitPool_Init(struct JitPool *AP, int maxItems, int itemSize, char *name)
{
	JitPool_InitAt(AP, maxItems, itemSize, name, MEMPACK_AllocMem(maxItems * itemSize));
}


int JitPool_Add(struct JitPool *AP)
{
	struct Item *item = AP->free.first;

	if (item != 0)
	{
		LIST_RemoveMember(&AP->free, item);
		LIST_AddFront(&AP->taken, item);
	}

	return (s32)item;
}


void JitPool_Remove(struct JitPool *AP, struct Item *item)
{
	LIST_RemoveMember(&AP->taken, item);
	LIST_AddFront(&AP->free, item);
}
