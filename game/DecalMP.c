
#include <common.h>

struct DecalMPEntry
{
	s16 timer;
	u8 kartState;
	u8 pad3[3];
	s16 boolUpdatedThisFrame;
	struct Instance *inst;
	u8 padC[4];
	s16 renderW;
	s16 renderH;
	int lodIndex;
	struct PushBuffer pb;
};

CTR_STATIC_ASSERT(sizeof(struct DecalMPEntry) == 0x128);

static inline struct DecalMPEntry *DecalMP_GetEntry(struct GameTracker *gGT, int index)
{
	return (struct DecalMPEntry *)(void *)&gGT->DecalMP[index];
}

static inline struct InstDrawPerPlayer *DecalMP_GetIdpp(struct Instance *inst, int cameraID)
{
	return (struct InstDrawPerPlayer *)((char *)INST_GETIDPP(inst) + (cameraID * sizeof(struct InstDrawPerPlayer)));
}


void DecalMP_01(struct GameTracker *gGT)
{
	if (gGT->numPlyrCurrGame == 0)
	{
		return;
	}

	int entryIndex = 0;

	for (int cameraID = 0; cameraID < gGT->numPlyrCurrGame; cameraID++)
	{
		struct PushBuffer *pb = &gGT->pushBuffer[cameraID];

		for (int driverID = 0; driverID < 8; driverID++)
		{
			struct Driver *driver = gGT->drivers[driverID];
			if (driver == NULL)
			{
				continue;
			}

			struct Instance *inst = driver->instSelf;
			inst->flags |= PUSHBUFFER_EXISTS | PIXEL_LOD;

			if (driverID == cameraID)
			{
				continue;
			}

			struct DecalMPEntry *entry = DecalMP_GetEntry(gGT, entryIndex++);

			if (driver->kartState == KS_BLASTED)
			{
				entry->kartState = KS_BLASTED;
			}
			else if (entry->kartState == KS_BLASTED)
			{
				entry->kartState = 0;
				entry->timer = 1000;
			}

			entry->pb.matrix_ViewProj = pb->matrix_ViewProj;
			entry->pb.pos = pb->pos;
			entry->pb.distanceToScreen_PREV = pb->distanceToScreen_PREV;
			entry->pb.rect = pb->rect;
			entry->pb.ptrOT = pb->ptrOT;
			entry->pb.cameraID = pb->cameraID;

			struct InstDrawPerPlayer *idpp = DecalMP_GetIdpp(inst, cameraID);
			idpp->pushBuffer = &entry->pb;
			entry->inst = inst;
		}
	}
}


void DecalMP_02(struct GameTracker *gGT)
{
	for (int index = 0; index < 12; index++)
	{
		struct DecalMPEntry *entry = DecalMP_GetEntry(gGT, index);
		if (entry->inst == NULL)
		{
			return;
		}

		int cameraID = entry->pb.cameraID;
		struct InstDrawPerPlayer *idpp = DecalMP_GetIdpp(entry->inst, cameraID);
		s16 timer = 1000;

		if ((idpp->instFlags & 0x140) == 0x140)
		{
			timer = entry->timer;
			int minFrames = entry->pb.renderBucketOTByteOffset >> 3;
			if (minFrames < 2)
			{
				minFrames = 2;
			}

			if ((timer < 1000) && (((timer <= minFrames) && (entry->lodIndex == idpp->lodIndex)) || (((gGT->timer ^ index) & 1) == 0)))
			{
				idpp->instFlags |= 0x80;
				entry->boolUpdatedThisFrame = 0;
			}
			else
			{
				entry->boolUpdatedThisFrame = 1;

				if ((entry->pb.ptrOT != NULL) && (entry->pb.renderBucketOTRangeEnd != NULL))
				{
					u32 *cameraOT = gGT->pushBuffer[cameraID].ptrOT;
					*entry->pb.ptrOT = cameraOT[0x3ff];
					cameraOT[0x3ff] = CtrGpu_PrimToOTLink24(entry->pb.renderBucketOTRangeEnd);
				}
			}

			timer++;
			if ((gGT->gameMode1 & PAUSE_ALL) == 0)
			{
				entry->timer = timer;
			}
		}
		else
		{
			entry->timer = timer;
		}
	}
}


// MEASUREMENT LINE, no behaviour: per frame, how many DecalMP entries are alive,
// how many of them draw outside the screen this round (dfe = 0,
// PushBuffer_SetDrawEnv_DecalMP) and which rectangles those are. Capped,
// so that a run does not flood the log.
void Platform_Log(const char *format, ...);
static int s_decalLogLines = 0;
static void DecalMP_LogFrame(int numAlive, int numDrawn, const RECT *rects)
{
	char line[12 * 24 + 8];
	int used = 0;
	int i;

	if (numAlive == 0)
	{
		return;
	}

	if (s_decalLogLines >= 400)
	{
		if (s_decalLogLines == 400)
		{
			Platform_Log("[CTR Decal] 400 lines written - further frames not logged\n");
			s_decalLogLines++;
		}
		return;
	}

	line[0] = 0;
	for (i = 0; i < numDrawn && i < 12; i++)
	{
		used += snprintf(line + used, sizeof(line) - (size_t)used, " (%d,%d,%d,%d)", rects[i].x, rects[i].y, rects[i].w, rects[i].h);
	}

	Platform_Log("[CTR Decal] %d entries alive, %d drawn offscreen this frame:%s\n", numAlive, numDrawn, line);
	s_decalLogLines++;
}

void DecalMP_03(struct GameTracker *gGT)
{
	RECT viewport;
	RECT drawnRects[12];
	int numAlive = 0;
	int numDrawn = 0;
	viewport.w = 0x60;
	viewport.h = 0x40;

	int texY = (gGT->numPlyrCurrGame == 2) ? 0x180 : 0x100;
	int maxTexX = (gGT->numPlyrCurrGame > 2) ? 0x2c0 : 0x380;
	int texX = 0x1a0;

	for (int index = 0; index < 12; index++)
	{
		texX += 0x60;
		if (maxTexX < texX)
		{
			texX = 0x200;
			texY += 0x40;
		}

		struct DecalMPEntry *entry = DecalMP_GetEntry(gGT, index);
		if (entry->inst == NULL)
		{
			DecalMP_LogFrame(numAlive, numDrawn, drawnRects);
			return;
		}
		numAlive++;

		int cameraID = entry->pb.cameraID;
		struct InstDrawPerPlayer *idpp = DecalMP_GetIdpp(entry->inst, cameraID);

		if ((idpp->instFlags & 0x140) != 0x140)
		{
			continue;
		}

		if (entry->boolUpdatedThisFrame != 0)
		{
			entry->timer = 0;
			entry->renderW = (s16)entry->pb.renderBucketScreenSize;
			entry->renderH = (s16)(entry->pb.renderBucketScreenSize >> 16);
			entry->lodIndex = idpp->lodIndex;

			viewport.x = (s16)texX;
			viewport.y = (s16)texY;
			drawnRects[numDrawn] = viewport;
			numDrawn++;

			PushBuffer_SetDrawEnv_DecalMP(entry->pb.renderBucketOTRangeEnd, gGT->backBuffer, &viewport, (s16)(texX - (s16)entry->pb.renderBucketScreenPos),
			                              (s16)(texY - (s16)(entry->pb.renderBucketScreenPos >> 16)), 0, 0, 0, 0, 1);
		}

		POLY_FT4 *poly = gGT->backBuffer->primMem.cursor;
		poly->code = 0x2d;

		s16 x = (s16)entry->pb.renderBucketScreenPos;
		s16 y = (s16)(entry->pb.renderBucketScreenPos >> 16);
		s16 w = (s16)entry->pb.renderBucketScreenSize;
		s16 h = (s16)(entry->pb.renderBucketScreenSize >> 16);

		CtrGpu_WritePackedXY(&poly->x0, (u16)x | ((u32)(u16)y << 16));
		CtrGpu_WritePackedXY(&poly->x1, (u16)(x + w) | ((u32)(u16)y << 16));
		CtrGpu_WritePackedXY(&poly->x2, (u16)x | ((u32)(u16)(y + h) << 16));
		CtrGpu_WritePackedXY(&poly->x3, (u16)(x + w) | ((u32)(u16)(y + h) << 16));

		int u0 = texX & 0x3f;
		int v0 = texY & 0xff;
		int u1 = u0 + entry->renderW;
		int v1 = v0 + entry->renderH;
		if (v1 >= 0x100)
		{
			v1 = 0xff;
		}

		CtrGpu_WritePackedUV(&poly->u0, (u16)(u0 | (v0 << 8)));
		CtrGpu_WritePackedUV(&poly->u1, (u16)(u1 | (v0 << 8)));
		CtrGpu_WritePackedUV(&poly->u2, (u16)(u0 | (v1 << 8)));
		CtrGpu_WritePackedUV(&poly->u3, (u16)(u1 | (v1 << 8)));

		poly->tpage = (u16)getTPage(TEXPAGE_COLOR_15BIT, TRANS_50, (u32)texX, (u32)texY);

		u32 *ot = gGT->pushBuffer[cameraID].ptrOT + (entry->pb.renderBucketOTByteOffset >> 2);
		poly->tag = CtrGpu_PackOTTag(*ot, 0x09000000);
		CtrGpu_LinkPrimToOT(ot, poly);
		gGT->backBuffer->primMem.cursor = poly + 1;
	}

	DecalMP_LogFrame(numAlive, numDrawn, drawnRects);
}
