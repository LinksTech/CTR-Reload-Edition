#include <common.h>

struct DrawSkyContext
{
	const struct ShortVertex *verts;
	u32 *ot;
	u32 screenBounds;

#if defined(CTR_NATIVE)
	// The end of the primitive memory, and how many faces failed
	// on it. See the long paragraph at DrawSky_Full.
	const u32 *primEnd;
	int dropped;

	// WHAT BECOMES OF EVERY FACE - and indeed of EVERY one, not of the first.
	//
	// Test 3 carries a complete sky: 988 points, 8 segments, D
	// 0 everywhere, every point index in range, and the primitive memory gets
	// its 113,344 bytes too. Nothing is dropped, and still there is no
	// sky in the picture. Statically the case is measured to the end.
	//
	// Between the data and the picture there are exactly two gates: the three flag bits
	// of the GTE and the edge test against pb->rect. Here we count which of
	// the two strikes - split by bit, because the three mean different things
	// and a shared counter does not answer the question.
	int looked;
	int rejMac0;   // bit 16 - the screen coordinate overflowed
	int rejDivide; // bit 17 - h/z overflowed, so z was smaller than h
	int rejDepth;  // bit 18 - z outside 0..65535
	int rejBounds; // the edge test, all three corners beyond the same edge
	int emitted;

	// Rejected by a flag bit although at least one corner lies in the picture:
	// that is the face whose absence the eye sees as a hole in the sky
	// (2026-09-22, Sunset Vista - 22 to 164 faces per segment, one face
	// spans up to a quarter circle there, and one corner behind the camera
	// takes the whole face with it). On the disc the rejected faces are
	// small and lie behind; here we count how often that is not the case.
	int rejFlagOnScreen;
#endif
};

struct DrawSkyScratch
{
	u32 baseFaceOffset;
	u32 baseCountOffset;
};

CTR_STATIC_ASSERT(sizeof(struct DrawSkyScratch) == 0x8);
CTR_STATIC_ASSERT(offsetof(struct DrawSkyScratch, baseFaceOffset) == 0x0);
CTR_STATIC_ASSERT(offsetof(struct DrawSkyScratch, baseCountOffset) == 0x4);

static u32 DrawSky_ReadWord(const void *base, int offset)
{
	return *(const u32 *)(const void *)((const char *)base + offset);
}

static int DrawSky_IsVisible(u32 gteFlag, u32 sxy0, u32 sxy1, u32 sxy2, u32 screenBounds)
{
	u32 overlap;
	u32 bounds;

	if (((gteFlag << 13) >> 29) != 0)
	{
		return 0;
	}

	overlap = sxy0 & sxy1 & sxy2;
	bounds = ~((sxy0 - screenBounds) | (sxy1 - screenBounds) | (sxy2 - screenBounds)) | overlap;
	if ((s32)bounds < 0)
	{
		return 0;
	}

	return (s32)(bounds << 16) >= 0;
}

static void DrawSky_LoadFaceVertices(struct DrawSkyContext *ctx, const struct SkyboxFace *face)
{
	const char *verts = (const char *)ctx->verts;
	const struct ShortVertex *a = (const struct ShortVertex *)(const void *)(verts + face->A);
	const struct ShortVertex *b = (const struct ShortVertex *)(const void *)(verts + face->B);
	const struct ShortVertex *c = (const struct ShortVertex *)(const void *)(verts + face->C);

	MTC2(DrawSky_ReadWord(&a->Position, 0x0), 0);
	MTC2(DrawSky_ReadWord(&a->Position, 0x4), 1);
	MTC2(DrawSky_ReadWord(&a->Color, 0x0), 20);

	MTC2(DrawSky_ReadWord(&b->Position, 0x0), 2);
	MTC2(DrawSky_ReadWord(&b->Position, 0x4), 3);
	MTC2(DrawSky_ReadWord(&b->Color, 0x0), 21);

	MTC2(DrawSky_ReadWord(&c->Position, 0x0), 4);
	MTC2(DrawSky_ReadWord(&c->Position, 0x4), 5);
	MTC2(DrawSky_ReadWord(&c->Color, 0x0), 22);
}

static void DrawSky_EmitPrimitive(u32 **primCursor, u32 *ot)
{
	POLY_G3 *poly = (POLY_G3 *)*primCursor;

	// THE SCREEN COORDINATES GO THROUGH THE HELPER, NOT AROUND THE
	// REGISTER. CTR_GteStoreSXY0/1/2 write the same four bytes as
	// CtrGpu_WritePackedXY(MFC2(12/13/14)) - and also tell the subpixel mirror
	// where they went; the primitive lies in the arena, so the
	// fraction lands directly (include/ctr_subpixel.h). Without that the sky, with
	// 241,038 of 2,373,575 vertices, was the largest single item without
	// fractional digits: it read the registers itself and packed later.
	//
	// That is the pattern for every further raw MFC2(12..14) reader: if
	// the value goes into the primitive right away, take the helper; if it first goes into a
	// local or a scratch memory, pass it on through NativeSubpixel_Forward to
	// the target place, like the track renderer. Condition in both
	// cases: between the projection and the store nobody may touch the
	// SXY stack (rtps/rtpt or MTC2 12..15).
	CtrGpu_WriteColorCode(&poly->r0, MFC2(20));
	CTR_GteStoreSXY0(&poly->x0);
	CtrGpu_WriteColorCode(&poly->r1, MFC2(21));
	CTR_GteStoreSXY1(&poly->x1);
	CtrGpu_WriteColorCode(&poly->r2, MFC2(22));
	CTR_GteStoreSXY2(&poly->x2);
	CtrGpu_LinkPacket24(ot, &poly->tag, poly, 0x06000000);

	*primCursor = (u32 *)(poly + 1);
}

static u32 *DrawSky_Piece(struct Skybox *skybox, struct DrawSkyContext *ctx, int faceIndex, int countIndex, u32 *prim)
{
	u32 numFaces = (u16)skybox->numFaces[countIndex];
	const struct SkyboxFace *face = skybox->ptrFaces[faceIndex];

	if (numFaces == 0)
	{
		return prim;
	}

	for (u32 i = 0; i < numFaces; i++, face++)
	{
		u32 *ot = (u32 *)(void *)((char *)ctx->ot + (s16)face->D);
		u32 sxy0;
		u32 sxy1;
		u32 sxy2;
		u32 gteFlag;

		DrawSky_LoadFaceVertices(ctx, face);
		gte_rtpt_b();

		sxy0 = MFC2(12);
		gteFlag = CFC2(31);
		sxy1 = MFC2(13);
		sxy2 = MFC2(14);

#if defined(CTR_NATIVE)
		ctx->looked++;

		if ((gteFlag & 0x10000u) != 0u) { ctx->rejMac0++; }
		if ((gteFlag & 0x20000u) != 0u) { ctx->rejDivide++; }
		if ((gteFlag & 0x40000u) != 0u) { ctx->rejDepth++; }

		// The edge test alone - i.e. the faces that hang on NO flag bit
		// and are still not drawn. Without this split
		// both gates would look the same.
		if ((((gteFlag << 13) >> 29) == 0u) && !DrawSky_IsVisible(gteFlag, sxy0, sxy1, sxy2, ctx->screenBounds))
		{
			ctx->rejBounds++;
		}

		// A flag bit AND a corner in the picture. The corner that triggered the bit
		// can stand anywhere after saturation; what is counted is whether
		// any of the three lies in the window - an indicator, not a proof.
		if ((((gteFlag << 13) >> 29) != 0u))
		{
			const s32 w = (s32)(ctx->screenBounds & 0xffffu);
			const s32 h = (s32)(ctx->screenBounds >> 16);
			const u32 sxy[3] = {sxy0, sxy1, sxy2};
			int corner;

			for (corner = 0; corner < 3; corner++)
			{
				const s32 x = (s16)(sxy[corner] & 0xffffu);
				const s32 y = (s16)(sxy[corner] >> 16);

				if ((x >= 0) && (x < w) && (y >= 0) && (y < h))
				{
					ctx->rejFlagOnScreen++;
					break;
				}
			}
		}
#endif

		if (DrawSky_IsVisible(gteFlag, sxy0, sxy1, sxy2, ctx->screenBounds))
		{
#if defined(CTR_NATIVE)
			// Does the next triangle still fit into the sky's share?
			//
			// Up to here the sky wrote into the primitive memory without any
			// bound. With the disc's data this cannot fire -
			// the largest sky there has 700 faces - and custom tracks
			// bring their own. The test data from 2026-08-28 has
			// 12,288, i.e. 344,064 bytes of primitives for a memory of
			// 97,280.
			//
			// What happened then: the pointer ran through the two
			// primitive memories, across both ordering tables, through the
			// swapchain tables and 24 bytes past their end out of the
			// registered range. There the GPU bridge gave up -
			// reported as "it just crashes", and everything the sky had
			// overwritten up to then was already gone.
			//
			// On real hardware it would have been the same, only quieter: the
			// PSX would simply have overwritten it. So this track is built
			// beyond the machine's budget, and the game now says so
			// instead of dying from it.
			if ((const u32 *)(prim + (sizeof(POLY_G3) / sizeof(u32))) > ctx->primEnd)
			{
				ctx->dropped++;
				continue;
			}
#endif
#if defined(CTR_NATIVE)
			ctx->emitted++;
#endif
			DrawSky_EmitPrimitive(&prim, ot);
		}
	}

	return prim;
}

void DrawSky_Full(void *skybox, struct PushBuffer *pb, struct PrimMem *primMem)
{
	struct Skybox *sky = skybox;
	u32 *prim = (u32 *)primMem->cursor;

	// NOTE(aalhendi): PSX-backfeed blocker: retail saves/restores ra and s0-s2 in scratchpad 0x00-0x0c.
	// Native C relies on the host ABI because the only retail data temporaries live in 0x10 and 0x14 and are explicit below.
#if defined(CTR_NATIVE)
	// --no-sky: leave out the sky.
	//
	// A measurement switch, not a setting. When a picture shows glitches and the
	// suspicion falls on the sky, a run without it answers the question,
	// and no amount of reasoning does. Built on 2026-08-28, when large flat
	// triangles lay across the picture and "that is the sky" was a guess.
	{
		extern int g_cfg_noSky;

		if (g_cfg_noSky)
		{
			primMem->cursor = prim;
			return;
		}
	}
#endif

	if (sky != NULL)
	{
		struct DrawSkyScratch *scratch = CTR_SCRATCHPAD_PTR(struct DrawSkyScratch, 0x10);
		struct DrawSkyContext ctx;
		u32 baseFaceOffset;
		u32 baseCountOffset;
		int faceIndex;
		int countIndex;
#if defined(CTR_NATIVE)
		u32 skyBudget = 0;
		u32 skyBytes = 0;
#endif

		CTC2(DrawSky_ReadWord(&pb->matrix_ViewProj, 0x00), 0);
		CTC2(DrawSky_ReadWord(&pb->matrix_ViewProj, 0x04), 1);
		CTC2(DrawSky_ReadWord(&pb->matrix_ViewProj, 0x08), 2);
		CTC2(DrawSky_ReadWord(&pb->matrix_ViewProj, 0x0c), 3);
		CTC2(DrawSky_ReadWord(&pb->matrix_ViewProj, 0x10), 4);
		CTC2(0, 5);
		CTC2(0, 6);
		CTC2(0, 7);

		baseFaceOffset = ((DrawSky_ReadWord(pb, 0x08) + 0x500) >> 7) & 0x1c;
		baseCountOffset = (baseFaceOffset >> 1) & 0xe;

		scratch->baseFaceOffset = baseFaceOffset;
		scratch->baseCountOffset = baseCountOffset;

		ctx.verts = sky->ptrVertex;
		ctx.ot = &pb->ptrOT[0x3ff];
		ctx.screenBounds = DrawSky_ReadWord(pb, 0x20);
#if defined(CTR_NATIVE)
		// How much primitive memory the sky may use at most.
		//
		// Not "up to the end". The sky is drawn FIRST, and if it
		// takes the whole memory, the rest of the picture writes past its
		// end - the same destruction, only one function later. A bolt
		// at the end alone would be a bolt that leaves the door next to it open.
		//
		// ONE QUARTER, and the number is measured, not chosen: of the 14
		// disc tracks with a sky, the largest (Tiger Temple) needs for its
		// four drawn segments 407 faces, i.e. 11,396 bytes or 11.7 %
		// of the 97,280. A quarter is more than twice the worst
		// real case, and the remaining three quarters are what every
		// disc track has anyway.
		ctx.primEnd = (const u32 *)(const void *)((const char *)primMem->start + primMem->capacityBytes);

		// The sky's share, computed ONCE.
		//
		// On a container track it is exactly what MainInit_PrimMem added for
		// it - then nothing is dropped and the rest of the picture
		// keeps its full budget. Otherwise a quarter, see above.
		//
		// It is outside the block because the message at the very bottom must name
		// the same number. Before, the message computed its own and, after the
		// surcharge, named a budget that was none.
		{
			int NativeTrack_ActiveForLevel(int levelID);

			skyBudget = primMem->capacityBytes / 4u;
			skyBytes = NativeTrack_ActiveForLevel(sdata->gGT->levelID) ? NativeTrack_SkyPrimBytes() : 0u;

			if (skyBytes != 0u)
			{
				skyBudget = skyBytes;
			}

			{
				const u32 *share = (const u32 *)(const void *)((const char *)prim + skyBudget);

				if (share < ctx.primEnd)
				{
					ctx.primEnd = share;
				}
			}
		}
		ctx.dropped = 0;
		ctx.looked = 0;
		ctx.rejMac0 = 0;
		ctx.rejDivide = 0;
		ctx.rejDepth = 0;
		ctx.rejFlagOnScreen = 0;
		ctx.rejBounds = 0;
		ctx.emitted = 0;
#endif

		faceIndex = (int)(baseFaceOffset >> 2);
		countIndex = (int)(baseCountOffset >> 1);
		prim = DrawSky_Piece(sky, &ctx, faceIndex, countIndex, prim);

		baseFaceOffset = (baseFaceOffset + 4) & 0x1c;
		baseCountOffset = (baseCountOffset + 2) & 0xe;
		faceIndex = (int)(baseFaceOffset >> 2);
		countIndex = (int)(baseCountOffset >> 1);
		prim = DrawSky_Piece(sky, &ctx, faceIndex, countIndex, prim);

		baseFaceOffset = (scratch->baseFaceOffset - 4) & 0x1c;
		baseCountOffset = (scratch->baseCountOffset - 2) & 0xe;
		faceIndex = (int)(baseFaceOffset >> 2);
		countIndex = (int)(baseCountOffset >> 1);
		prim = DrawSky_Piece(sky, &ctx, faceIndex, countIndex, prim);

		baseFaceOffset = (scratch->baseFaceOffset - 8) & 0x1c;
		baseCountOffset = (scratch->baseCountOffset - 4) & 0xe;
		faceIndex = (int)(baseFaceOffset >> 2);
		countIndex = (int)(baseCountOffset >> 1);
		prim = DrawSky_Piece(sky, &ctx, faceIndex, countIndex, prim);

#if defined(CTR_NATIVE)
		// THE REPORT, EVERY 300 FRAMES - and explicitly NOT a one-shot.
		//
		// Four probes of the driver selection and the line table of the page memory
		// made the same mistake on 2026-08-29: a one-shot fires
		// at the first moment it can, and the first moment is the
		// fade-in, the title screen, the wrong player. So a window as
		// for the page memory: sum up, report every 300 frames, reset to zero.
		//
		// And only if any face was looked at at all. A picture without
		// sky should not report 300 zeros.
		{
			local_persist int frames = 0;
			local_persist int looked = 0;
			local_persist int rejMac0 = 0;
			local_persist int rejDivide = 0;
			local_persist int rejDepth = 0;
			local_persist int rejBounds = 0;
			local_persist int emitted = 0;
			local_persist int dropped = 0;
			local_persist int rejFlagOnScreen = 0;

			void Platform_Log(const char *format, ...);

			frames++;
			looked += ctx.looked;
			rejMac0 += ctx.rejMac0;
			rejDivide += ctx.rejDivide;
			rejDepth += ctx.rejDepth;
			rejBounds += ctx.rejBounds;
			emitted += ctx.emitted;
			dropped += ctx.dropped;
			rejFlagOnScreen += ctx.rejFlagOnScreen;

			if ((frames >= 300) && (looked > 0))
			{
				Platform_Log("[CTR Sky] %d frame(s): %d face(s) looked at, %d drawn, %d dropped for memory\n", frames, looked, emitted, dropped);
				Platform_Log("[CTR Sky]   thrown out by the GTE flags: %d screen overflow, %d h/z overflow, %d depth out of range\n", rejMac0,
				             rejDivide, rejDepth);
				Platform_Log("[CTR Sky]   thrown out by a flag although a corner is on screen: %d\n", rejFlagOnScreen);
				Platform_Log("[CTR Sky]   thrown out by the edge test alone: %d\n", rejBounds);
				Platform_Log("[CTR Sky]   h %d, screen bounds %d x %d, segments drawn %d %d %d %d\n", (int)DrawSky_ReadWord(pb, 0x18),
				             (int)(ctx.screenBounds & 0xffffu), (int)(ctx.screenBounds >> 16), (int)(scratch->baseCountOffset >> 1),
				             (int)(((scratch->baseCountOffset + 2) & 0xe) >> 1), (int)(((scratch->baseCountOffset - 2) & 0xe) >> 1),
				             (int)(((scratch->baseCountOffset - 4) & 0xe) >> 1));

				frames = 0;
				looked = 0;
				rejMac0 = 0;
				rejDivide = 0;
				rejDepth = 0;
				rejBounds = 0;
				emitted = 0;
				dropped = 0;
				rejFlagOnScreen = 0;
			}
		}

		// Once, and then never again.
		//
		// Here it used to say "once per track" and it was a flood: the number of
		// dropped faces depends on the view angle, so it was different in almost
		// every frame and the message fired 51 times in one run.
		// A counter that changes is not a state one may compare
		// - measured on our own log from 2026-08-28, 85 KB.
		if (ctx.dropped != 0)
		{
			local_persist int alreadySaid = 0;

			if (!alreadySaid)
			{
				void Platform_Log(const char *format, ...);

				// skyBudget and not capacityBytes/4: the quarter used to be here,
				// and since a container track gets its surcharge, the
				// budget is exactly that surcharge. The message then named a
				// number that was no budget - the same fact in two places,
				// and one of them was left behind in the rework.
				alreadySaid = 1;
				Platform_Log("[CTR Sky] %d sky face(s) did not fit the sky's share of the primitive memory\n", ctx.dropped);
				Platform_Log("[CTR Sky] (%u of %u bytes) and were dropped. The sky is incomplete.\n", skyBudget, primMem->capacityBytes);
				Platform_Log("[CTR Sky] The biggest sky on the disc needs 11396 bytes. This track wants far more.\n");
			}
		}
#endif
	}

	primMem->cursor = prim;
}
