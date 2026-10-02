#include <common.h>

#ifdef CTR_NATIVE
int NativeFlyIn_PreviewAlone(void); // game/native_flyin.c
u32 NativeChar_DrawReserve(int tableBytes); // platform/native_chars.c

static void MainInit_InitVisMemBspListNodes(struct VisMem *visMem, struct mesh_info *mesh)
{
	if (mesh == NULL || mesh->bspRoot == NULL)
	{
		return;
	}

	for (int playerIndex = 0; playerIndex < 4; playerIndex++)
	{
		struct VisMemBspListNode *bspList = visMem->bspList[playerIndex];

		if (bspList == NULL)
		{
			continue;
		}

		for (int bspIndex = 0; bspIndex < mesh->numBspNodes; bspIndex++)
		{
			// NOTE(aalhendi): Native 226 reads the retained BSP pointer; RenderLists only rewrites the link word.
			bspList[bspIndex].next = NULL;
			bspList[bspIndex].bsp = &mesh->bspRoot[bspIndex];
		}
	}
}
#endif

void MainInit_VisMem(struct GameTracker *gGT)
{
	struct VisMem *visMem = gGT->level1->visMem;
	gGT->visMem1 = visMem;

	if (visMem == NULL)
	{
		return;
	}

	for (int i = 0; i < gGT->numPlyrCurrGame; i++)
	{
		visMem->visLeafSrc[i] = NULL;
		visMem->visFaceSrc[i] = NULL;
		visMem->visOVertSrc[i] = NULL;
		visMem->visSCVertSrc[i] = NULL;
	}

#ifdef CTR_NATIVE
	MainInit_InitVisMemBspListNodes(visMem, gGT->level1->ptr_mesh_info);
#endif
}

void MainInit_RainBuffer(struct GameTracker *gGT)
{
	u8 numPlyr = gGT->numPlyrCurrGame;

	if (numPlyr == 0)
	{
		return;
	}

	for (int i = 0; i < numPlyr; i++)
	{
		struct RainBuffer *dst = &gGT->rainBuffer[i];
		const u32 *srcWords = (const u32 *)(const void *)&gGT->level1->rainBuffer;
		u32 *dstWords = (u32 *)(void *)dst;

		for (int word = 0; word < (int)(sizeof(struct RainBuffer) / sizeof(u32)); word += 4)
		{
			dstWords[word + 0] = srcWords[word + 0];
			dstWords[word + 1] = srcWords[word + 1];
			dstWords[word + 2] = srcWords[word + 2];
			dstWords[word + 3] = srcWords[word + 3];
		}

		dst->numParticles_curr /= numPlyr;
		dst->numParticles_max = (s16)((u16)dst->numParticles_max / numPlyr);
	}
}

static int MainInit_GetPrimMemSize(struct GameTracker *gGT)
{
	int levelID;

	// adv garage
	if (gGT->levelID == ADVENTURE_GARAGE)
	{
		return 0x1b800;
	}

	// main menu
	if ((gGT->gameMode1 & MAIN_MENU) != 0)
	{
		return 0x17c00;
	}

	levelID = gGT->levelID;

	switch (gGT->numPlyrCurrGame)
	{
	case 0:
		return 0x25800;

	case 1:
		if ((gGT->gameMode1 & ADVENTURE_ARENA) != 0)
		{
			return 0x1c000;
		}

		if ((u32)(levelID - INTRO_RACE_TODAY) < 9)
		{
			return 0x1e000;
		}

		if (levelID < GEM_STONE_VALLEY)
		{
			return data.primMem_SizePerLEV_1P[levelID] << 10;
		}

		return 0x17c00;

	case 2:
		if (levelID < GEM_STONE_VALLEY)
		{
			return data.primMem_SizePerLEV_2P[levelID] << 10;
		}

		return 0x1e000;

	case 3:
	case 4:
		if (levelID < GEM_STONE_VALLEY)
		{
			return data.primMem_SizePerLEV_4P[levelID] << 10;
		}

		return 0x25800;

	default:
		return 0;
	}
}

void MainInit_PrimMem(struct GameTracker *gGT)
{
	int size = MainInit_GetPrimMemSize(gGT);

#if defined(CTR_NATIVE)
	// The sum of the previous level, before its draw memory disappears:
	// in how many frames the track pass gave up for lack of space
	// (226, DrawLevelOvr1P_NoteBucketGaveUp), and in how many the list
	// quadBlocksRendered reached its end (DrawLevelOvr1P_NoteRenderedListFull).
	// Nothing counted, nothing said.
	{
		void DrawLevelOvr1P_FlushGiveUps(void);
		void DrawLevelOvr1P_FlushRenderedListFull(void);

		DrawLevelOvr1P_FlushGiveUps();
		DrawLevelOvr1P_FlushRenderedListFull();
	}
#endif

	if (size == 0)
	{
		return;
	}

#if defined(CTR_NATIVE)
	// THE DRAW MEMORY GROWS WITH THE CANVAS.
	//
	// The table above is sized for the PS1's 4:3 picture. A widescreen picture
	// draws more world with the same camera (43:18 in the demo's hole window,
	// Crash Cove: 1368 instead of 1152 track primitives per frame), and in expensive
	// frames the memory ran full. Then the bucket pass gives up in
	// DrawLevelOvr1P_HasBucketPrimReserve, Ovr226_800a0e10_DispatchBucketTable aborts ALL
	// following buckets including the clip record split, and the near ground
	// is missing as a black wedge - measured 12 give-ups in 30 frames at 43:18,
	// 0 at 4:3, congruent with the hole (up to 29 percent of the lower
	// third, VBlank 1910..2004).
	//
	// The same rule as for the clip window and for the clip buffer:
	// scale the 4:3 number by reference width. At 4:3 the factor is
	// one and every byte as before; the additions below (sky, track)
	// are self-computed worst cases per face and do not scale.
	{
		int CTR_Canvas_FromReferenceWidth(int width);

		size = CTR_Canvas_FromReferenceWidth(size);
	}

	// A container track gets as much added as its sky needs.
	//
	// The number in the table above belongs to the track whose slot it
	// occupies: Dingo Canyon gets 97,280 bytes for a whole frame. A foreign
	// track may be bigger, and one test track was -
	// its sky alone wanted 172,032.
	//
	// The addition is not guessed, it is computed: the four segments that
	// a frame draws, times 28 bytes per triangle. That way the rest of the
	// frame stays at exactly the budget the host track has, and the sky
	// gets its own on top.
	//
	// The memory pack's window only carries this when the tracks folder is
	// read (the default; off with --no-tracks) - see platform/native_memory.c.
	// Without it nothing is loaded here and the addition is zero.
	{
		int NativeTrack_ActiveForLevel(int levelID);
		void Platform_Log(const char *format, ...);

		if (NativeTrack_ActiveForLevel(gGT->levelID))
		{
			// AND THE TRACK ITS OWN BUDGET, not the host's.
			//
			// An earlier version gave a container track only the addition
			// for its sky; the rest of the frame ran on the number of the
			// track whose slot it occupies. Measured on the test data, exactly
			// that was the cause of the picture errors: not the LEV was too big
			// (they are in the disc's range) and not the VRM (it is the same
			// layout byte for byte), but the BSP tree is three to
			// five times coarser than any of the disc - 3.3 to 9.2 quadblocks per
			// node against 1.2 to 2.8. The renderer culls per leaf, so
			// a multiple remains per visible leaf: 1,868 to 3,222
			// draw commands per frame against 139 to 488 on the disc.
			//
			// If the draw memory was not enough then, the pass gave up in
			// DrawLevelOvr1P_HasBucketPrimReserve and the rest of the bundle
			// was not drawn. Silently, with a hole in the picture.
			//
			// The addition is not guessed, it is the worst case:
			// every quadblock four quads, each a POLY_GT4. If the whole
			// track is visible it is still enough - and then nothing can be
			// missing here any more, instead of rarely.
			//
			// --tracks-fixed-memory restores the old way: only the sky
			// gets an addition, the rest runs on the host's budget.
			// The switch must take back BOTH numbers - the reserve behind
			// the window and this budget here. Only one of them would be a
			// state that never existed before.
			extern int g_cfg_tracksFixedMemory;
			const u32 skyBytes = NativeTrack_SkyPrimBytes();
			const u32 trackBytes = g_cfg_tracksFixedMemory ? 0u : NativeTrack_PrimBytes();
			const int stock = size;

			size += (int)(skyBytes + trackBytes);

			Platform_Log("[CTR Tracks] draw memory: %d bytes from the slot's table, +%u for the track, +%u for the sky = %d\n", stock,
				             trackBytes, skyBytes, size);
		}
	}

	// AND THE CUSTOM CHARACTERS THEIR OWN SHARE (platform/native_chars.c, THE
	// DRAW RESERVE): a custom model may draw more than the retail model it
	// stands in for, and what it draws more must not be missing from the
	// track. Added last, so everything above keeps its bytes at the same
	// offsets and the reserve lies at the end. Without a file in characters/
	// this is 0 and every byte as before.
	size += (int)NativeChar_DrawReserve(size);
#endif

	MainDB_PrimMem(&gGT->db[0].primMem, size);
	MainDB_PrimMem(&gGT->db[1].primMem, size);
}

void MainInit_JitPoolsReset(struct GameTracker *gGT)
{
	JitPool_Clear(&gGT->JitPools.thread);
	JitPool_Clear(&gGT->JitPools.instance);
	JitPool_Clear(&gGT->JitPools.smallStack);
	JitPool_Clear(&gGT->JitPools.mediumStack);
	JitPool_Clear(&gGT->JitPools.largeStack);
	JitPool_Clear(&gGT->JitPools.particle);
	JitPool_Clear(&gGT->JitPools.oscillator);
	JitPool_Clear(&gGT->JitPools.rain);
}

void MainInit_OTMem(struct GameTracker *gGT)
{
	int size;
	u32 gameMode = gGT->gameMode1;

	if ((gameMode & MAIN_MENU) != 0)
	{
		size = 0x1800;
		goto EndFunc;
	}

	if ((gameMode & ADVENTURE_ARENA) != 0)
	{
		size = 0x2c00;
		goto EndFunc;
	}

	if ((gameMode & BATTLE_MODE) != 0)
	{
		size = 0x8000;
		goto EndFunc;
	}

	// 1P/2P mode
	if (gGT->numPlyrCurrGame < 3)
	{
		size = 0x2000;
		goto EndFunc;
	}

	// 3P/4P mode
	size = 0x3000;

EndFunc:

	MainDB_OTMem(&gGT->db[0].otMem, size);
	MainDB_OTMem(&gGT->db[1].otMem, size);

	// 0x1000 per player, plus 0x18 for linking
	size = ((gGT->numPlyrCurrGame) << 0xC) | 0x18;
	gGT->otSwapchainDB[0] = MEMPACK_AllocMem(size); // "ot1"
	gGT->otSwapchainDB[1] = MEMPACK_AllocMem(size); // "ot2"
}

void MainInit_JitPoolsNew(struct GameTracker *gGT)
{
	u32 gameMode = gGT->gameMode1;
	int poolScale = 0x800;
	if ((gameMode & ADVENTURE_ARENA) == 0)
	{
		poolScale = 0x1000;
		if ((gameMode & MAIN_MENU) != 0)
		{
			poolScale = 0x400;
		}
	}

	int renderBucketSize = 0x800;
	if ((gameMode & ADVENTURE_ARENA) == 0)
	{
		renderBucketSize = 0x1000;
		if ((gameMode & MAIN_MENU) != 0)
		{
			renderBucketSize = 0x400;
			if (gGT->levelID == ADVENTURE_GARAGE)
			{
				renderBucketSize = 0x800;
			}
		}
	}

	MEMPACK_PushState();

	JitPool_Init(&gGT->JitPools.thread, (renderBucketSize * 3) >> 7, sizeof(struct Thread), rdata.s_ThreadPool);
#if defined(CTR_NATIVE)
	{
		// --instance-pool <n> (only with --dev): probe for a full
		// instance pool in a race. Never bigger than the retail number.
		extern int g_cfg_instancePool;
		void Platform_Log(const char *format, ...);
		int numInstance = renderBucketSize >> 5;

		if ((g_cfg_instancePool > 0) && (g_cfg_instancePool < numInstance) && ((gameMode & MAIN_MENU) == 0))
		{
			Platform_Log("[CTR Debug] --instance-pool: level %d - instance pool %d instead of %d\n", (int)gGT->levelID, g_cfg_instancePool, numInstance);
			numInstance = g_cfg_instancePool;
		}

		JitPool_Init(&gGT->JitPools.instance, numInstance, sizeof(struct Instance) + (sizeof(struct InstDrawPerPlayer) * gGT->numPlyrCurrGame),
		             rdata.s_InstancePool);
	}
#else
	JitPool_Init(&gGT->JitPools.instance, renderBucketSize >> 5, sizeof(struct Instance) + (sizeof(struct InstDrawPerPlayer) * gGT->numPlyrCurrGame),
	             rdata.s_InstancePool);
#endif
	JitPool_Init(&gGT->JitPools.smallStack, (poolScale * 0x19) >> 10, 0x48, rdata.s_SmallStackPool);
	JitPool_Init(&gGT->JitPools.mediumStack, poolScale >> 7, 0x88, rdata.s_MediumStackPool);

	int numDriver = poolScale >> 9;
	if ((gameMode & MAIN_MENU) != 0)
	{
		numDriver = 4;
	}
	JitPool_Init(&gGT->JitPools.largeStack, numDriver, 0x670, rdata.s_LargeStackPool);

	int numParticle = poolScale >> 5;
	JitPool_Init(&gGT->JitPools.particle, numParticle, sizeof(struct Particle), rdata.s_ParticlePool);
	JitPool_Init(&gGT->JitPools.oscillator, numParticle, 0x18, rdata.s_OscillatorPool);
	JitPool_Init(&gGT->JitPools.rain, poolScale >> 9, sizeof(struct RainLocal), rdata.s_RainPool);

#ifndef CTR_NATIVE
	gGT->ptrRenderBucketInstance = MEMPACK_AllocMem(renderBucketSize);
#else
	// NOTE(aalhendi): Native reuses static RDATA scratch for existing PC memory headroom.
	gGT->ptrRenderBucketInstance = (void *)((u32)&rdata.s_STATIC_GNORMALZ[0] + 148);
#endif

	for (int i = 0; i < 3; i++)
	{
		struct JitPool *pool = (struct JitPool *)((char *)&gGT->JitPools.smallStack + (sizeof(struct JitPool) * i));
		int *pointer = (int *)pool->free.first;
		while (pointer != (int *)0x0)
		{
			*(int **)(pointer + 2) = pointer + 2;
			pointer = (int *)*pointer;
		}
	}

#if defined(CTR_NATIVE)
	// THE NEAR-PLANE CLIP BUFFER, AND WHY IT GROWS WITH THE TRACK.
	//
	// In an earlier version its size came solely from MainDB_GetClipSize by
	// level ID - for a container, therefore, from the table of its seat
	// (seat 0: 3000 words, 12,000 bytes, 200 GT4 records per frame). The
	// draw memory above already got the track's addition,
	// this buffer did not. Whatever found no room fell away in the track renderer
	// without a trace, and those were exactly the faces that cut the near
	// plane: the road under the kart. On Crash Cove at 43:18 that was
	// already 1,470 records in 700 frames.
	//
	// CTR_ClipBufferBytes is ONE rule for allocation and end (the
	// track renderer measures the end with the same function). The addition
	// is that of primBytes, a record instead of a quad - see
	// RLD_CLIP_RECORD_GT4_BYTES in rldtrack.inc.
	{
		int CTR_ClipBufferBytes(u32 levelID, int numPlyrCurrGame);
		int NativeTrack_ActiveForLevel(int levelID);
		u32 NativeTrack_ClipBytes(void);
		void Platform_Log(const char *format, ...);

		const int bytes = CTR_ClipBufferBytes(gGT->levelID, gGT->numPlyrCurrGame);

		for (int i = 0; i < gGT->numPlyrCurrGame; i++)
		{
			data.PtrClipBuffer[i] = MEMPACK_AllocMem(bytes);
		}

		// For every level, not only for containers: a disc track
		// carries the factor as well (CTR_CLIP_STOCK_FACTOR in native_view.c),
		// and the factor in the line is computed from the result of the rule,
		// not copied - the line cannot say anything other than
		// the allocation.
		{
			const int stock = MainDB_GetClipSize(gGT->levelID, gGT->numPlyrCurrGame) << 2;
			const u32 track = NativeTrack_ActiveForLevel(gGT->levelID) ? NativeTrack_ClipBytes() : 0u;
			const int factor = (stock > 0) ? ((bytes - (int)track) / stock) : 0;

			Platform_Log("[CTR Clip] buffer per player: %d bytes = %d from the slot's table x %d + %u for the track (%d GT4 records), %d player(s)\n", bytes, stock,
			             factor, track, bytes / 0x3c, (int)gGT->numPlyrCurrGame);
		}
	}
#else
	for (int i = 0; i < gGT->numPlyrCurrGame; i++)
	{
		data.PtrClipBuffer[i] = MEMPACK_AllocMem(MainDB_GetClipSize(gGT->levelID, gGT->numPlyrCurrGame) << 2);
	}
#endif
}

void MainInit_Drivers(struct GameTracker *gGT)
{
	u8 numPlyrCurrGame = gGT->numPlyrCurrGame;
	u8 numDrivers;
	int gameMode = gGT->gameMode1;

	for (int i = 0; i < 8; i++)
	{
		gGT->drivers[i] = NULL;
	}

	gGT->numBotsNextGame = 0;

	if ((gameMode & (GAME_CUTSCENE | ADVENTURE_ARENA | MAIN_MENU)) == 0)
	{
		BOTS_Adv_AdjustDifficulty();
	}

	GhostReplay_Init1();

	if (LOAD_IsOpen_RacingOrBattle())
	{
		RB_MinePool_Init();
	}

	// Spawn all players,
	// This MUST be in reverse order,
	// because of threadBucket linked list order
	for (int i = numPlyrCurrGame - 1; i >= 0; i--)
	{
		gGT->drivers[i] = VehBirth_Player(i);
	}

	// spawn all AIs
	if ((
	        // exclude cutscene, relic, Time Trial,
	        // Adventure Hub, Main Menu, Battle
	        ((gameMode & 0x2c122020) == 0) &&

	        // numPlyrCurrGame requires AIs
	        (numPlyrCurrGame < 3)) &&
	    (
	        // in Arcade or Adventure
	        (gameMode & (ARCADE_MODE | ADVENTURE_MODE)) != 0))
	{
		// If you're in Boss Mode
		// 0x80000000
		if (gameMode < 0)
		{
			numDrivers = numPlyrCurrGame + 1;
		}

		// Purple Gem Cup
		else if (

		    // If you are in Adventure cup
		    ((gameMode & ADVENTURE_CUP) != 0) &&

		    // purple gem cup
		    (gGT->cup.cupID == 4))
		{
			numDrivers = numPlyrCurrGame + 4;
		}

		else if (numPlyrCurrGame == 1)
		{
			numDrivers = 8;
		}

		else // if (numPlyrCurrGame == 2)
		{
			numDrivers = 6;
		}

#if defined(CTR_NATIVE)
		// Track preview (--record-preview, game/native_flyin.c): the AI
		// drives the invisible player seat alone, without opponents.
		if (NativeFlyIn_PreviewAlone())
		{
			numDrivers = numPlyrCurrGame;
		}
#endif

		// Spawn AIs
		for (int i = numPlyrCurrGame; i < numDrivers; i++)
		{
			// spawn an AI at this character index
			BOTS_Driver_Init(i);
		}
	}

	// If number of AIs is not zero
	if (gGT->numBotsNextGame != 0)
	{
		// Init AI engine sounds
		EngineAudio_InitOnce(0x10, HOWL_SFX_CENTER_NO_DISTORTION);
		EngineAudio_InitOnce(0x11, HOWL_SFX_CENTER_NO_DISTORTION);
	}

	// if this is main menu
	if ((gameMode & MAIN_MENU) != 0)
	{
		// fill up 4 players
		for (int i = numPlyrCurrGame; i < 4; i++)
		{
			gGT->drivers[i] = VehBirth_Player(i);
		}
	}

	// if you're in time trial, not main menu, not cutscene.
	// basically, if you're in time trial gameplay
	if ((gameMode & GAME_MODE_TIME_TRIAL_GAMEPLAY_MASK) == TIME_TRIAL)
	{
		GhostReplay_Init2();

		GhostTape_Start();

#if defined(CTR_NATIVE)
		struct Model **humanPlyrDriverModel = &gGT->threadBuckets[PLAYER].thread->inst->model;

		// that's characterIDs[1] from the MPK
		// humanGhost = *humanPlyrDriverModel,

		// then replace with intended P1 model
		*humanPlyrDriverModel = data.driverModelExtras[0].model;
#endif
	}
}

void MainInit_FinalizeInit(struct GameTracker *gGT)
{
	int i;
	int numPlyr;
	struct Driver *d;
	struct Level *lev1;
	struct Instance *inst;

	// === Naughty Dog Bug ===
	// Quitting a race while heldItem is warpball,
	// never resets this flag, and then the game
	// can not give warpball again until you reboot
	gGT->gameMode1 &= ~(WARPBALL_HELD);

	// enable collisions with all temporary walls
	// (adv hub doors, tiger temple teeth, etc)
	sdata->doorAccessFlags = 0;

	// add a bookmark
	MEMPACK_PushState();

	gGT->pushBuffer[0].distanceToScreen_PREV = 0x100;
	gGT->pushBuffer[0].distanceToScreen_CURR = 0x100;

	memset(gGT->threadBuckets, 0, sizeof(gGT->threadBuckets));
	gGT->threadBuckets[STATIC].boolCantPause = 1;
	gGT->threadBuckets[WARPPAD].boolCantPause = 1;
	gGT->threadBuckets[CAMERA].boolCantPause = 1;
	gGT->threadBuckets[HUD].boolCantPause = 1;

	// particles
	gGT->particleList_ordinary = NULL;
	gGT->particleList_heatWarp = NULL;
	gGT->numParticles = 0;

	// deadc0ed, FUN_8006c684
	// RNG stuff
	gGT->deadcoed_struct.state0 = 0x30215400;
	gGT->deadcoed_struct.state1 = 0x493583fe;

	for (i = 0; i < 12; i++)
	{
		gGT->DecalMP[i].inst = NULL;
		*(s16 *)&gGT->DecalMP[i].data[0] = 1000;

		gGT->DecalMP[i].ptrOT1 = 0;
		gGT->DecalMP[i].ptrOT2 = 0;
	}

	MainInit_JitPoolsReset(gGT);

	lev1 = gGT->level1;

#if defined(CTR_NATIVE)
	// NOTE(aalhendi): Native menu LEVs may publish no restart table.
	if (lev1->ptr_restart_points != NULL)
#endif
	// 0x1d7c
	{
		gGT->trackLength_x_numLaps_x_8 = lev1->ptr_restart_points[0].distToFinish * gGT->numLaps * 8;
	}

	MainInit_Drivers(gGT);

	// assume 1P fov
	numPlyr = 1;

	// if you are not in main menu
	if ((gGT->gameMode1 & MAIN_MENU) == 0)
	{
		numPlyr = gGT->numPlyrCurrGame;
	}

	// Initialize four PushBuffer, 4 main screens
	PushBuffer_Init(&gGT->pushBuffer[0], 0, numPlyr);
	PushBuffer_Init(&gGT->pushBuffer[1], 1, numPlyr);
	PushBuffer_Init(&gGT->pushBuffer[2], 2, numPlyr);
	PushBuffer_Init(&gGT->pushBuffer[3], 3, numPlyr);

	struct PushBuffer *pb;

	pb = &gGT->pushBuffer_UI;
	PushBuffer_Init(pb, 0, 1);

	pb->rot.x = 0x800;
	PushBuffer_SetPsyqGeom(pb);
	PushBuffer_SetMatrixVP(pb);

	if ((gGT->hudFlags & HUD_FLAG_INIT_UI_INSTANCES) != 0)
	{
		UI_INSTANCE_InitAll();
	}

	gGT->unk1cac[4] = 2;

	for (i = 0; i < 8; i++)
	{
		// get pointer to player structure of each driver
		d = gGT->drivers[i];

		// if pointer is not nullptr
		if (d == NULL)
		{
			continue;
		}

		inst = d->instSelf;
		if (inst != 0)
		{
			inst->scale = (SVec3){0xccc, 0xccc, 0xccc};
		}

		if (i < gGT->numPlyrCurrGame)
		{
			CAM_Init(&gGT->cameraDC[i], i, d, &gGT->pushBuffer[i]);

			// freeze camera of P1, only in main menu
			if (((gGT->gameMode1 & MAIN_MENU) == 0) || (i < 1))
			{
				// remove frozen camera flag
				gGT->cameraDC[i].flags &= ~CAMERA_FLAG_FROZEN;
			}
			else
			{
				gGT->cameraDC[i].flags |= CAMERA_FLAG_FROZEN;
			}
		}
	}

	if (gGT->levelID == MAIN_MENU_LEVEL)
	{
		// 30 seconds
		gGT->demoCountdownTimer = 900;
	}

	// copy InstDef to InstancePool
	INSTANCE_LevInitAll(lev1->ptrInstDefs, lev1->numInstances);

	// Debug_ToggleNormalSpawn == normal spawn
	if (gGT->Debug_ToggleNormalSpawn != 0)
	{
		MainGameStart_Initialize(gGT, 1);

		if (gGT->boolDemoMode != 0)
		{
			for (i = 0; i < gGT->numPlyrCurrGame; i++)
			{
				BOTS_Driver_Convert(gGT->drivers[i]);
			}
		}

#if defined(CTR_NATIVE)
		// --autopilot (only with --dev): the player seat drives as a bot, in a
		// real race. Only on tracks and arenas (below GEM_STONE_VALLEY) -
		// podium, hub and menu have no nav paths, and BOTS_Driver_Convert
		// reads them.
		else
		{
			extern int g_cfg_autopilot;
			void Platform_Log(const char *format, ...);

			if (g_cfg_autopilot && (gGT->numPlyrCurrGame == 1) && (gGT->drivers[0] != NULL) && (gGT->levelID < GEM_STONE_VALLEY) &&
			    ((gGT->gameMode1 & MAIN_MENU) == 0))
			{
				struct Driver *seat = gGT->drivers[0];

				BOTS_Driver_Convert(seat);

				// BOTS_Driver_Convert freezes the clock first thing
				// (UI_RaceEnd_GetDriverClock sets ACTION_RACE_TIMER_FROZEN, the
				// retail routine for the finish). At the start that is wrong: the clock
				// would stand at 0:00:00. The clock runs again; because a bot
				// never goes through VehPhysProc, DebugMenu_Frame updates the time every
				// frame, up to the finish.
				seat->actionsFlagSet &= ~ACTION_RACE_TIMER_FROZEN;

				if ((seat->actionsFlagSet & ACTION_BOT) != 0)
				{
					Platform_Log("[CTR Debug] --autopilot: level %d - the player's seat drives as a bot, in a real race (HUD, finish, points)\n",
					             (int)gGT->levelID);
				}
				else
				{
					// Without a nav path with more than one point Convert aborts
					// (nav path check in BOTS_Driver_Convert): the seat stays a human without input.
					Platform_Log("[CTR Debug] --autopilot: level %d - OFF, the track has no nav path - the player's seat stays a player\n",
					             (int)gGT->levelID);
				}
			}
		}
#endif
	}

#if defined(CTR_NATIVE)
	// --weapon-pool-empty (only with --dev): probe for the weapon fix (upstream
	// bf2ed389c). The free list of the medium stack pool is dropped; until
	// the next load (JitPool_Init above) every missile, bomb,
	// shield and warpball fails in VehPickupItem_ShootNow.
	{
		extern int g_cfg_weaponPoolEmpty;
		void Platform_Log(const char *format, ...);

		if (g_cfg_weaponPoolEmpty && ((gGT->gameMode1 & MAIN_MENU) == 0))
		{
			Platform_Log("[CTR Debug] --weapon-pool-empty: level %d - medium stack pool emptied (%d free item(s) dropped)\n", (int)gGT->levelID,
			             (int)gGT->JitPools.mediumStack.free.count);
			gGT->JitPools.mediumStack.free.first = NULL;
			gGT->JitPools.mediumStack.free.last = NULL;
			gGT->JitPools.mediumStack.free.count = 0;
		}
	}
#endif

	// execute all camera thread update functions
	ThTick_RunBucket(gGT->threadBuckets[CAMERA].thread);

// dont write unused variables
#if 0
    // lev -> clearColor rgb
    sdata->LevClearColorRGB[0] = (u32)(char *)(lev1->clearColorRGBA)[0];
    sdata->LevClearColorRGB[1] = (u32)(char *)(lev1->clearColorRGBA)[1];
    sdata->LevClearColorRGB[2] = (u32)(char *)(lev1->clearColorRGBA)[2];
#endif

	// Used in Coco Park, encoded as Blue
	*(int *)&gGT->db[0].drawEnv.isbg = lev1->clearColorRGBA << 8;
	*(int *)&gGT->db[1].drawEnv.isbg = lev1->clearColorRGBA << 8;

	if ((gGT->numPlyrCurrGame == 1) && (lev1->clearColor[0].enable != 0) && (lev1->clearColor[1].enable != 0))
	{
		// set isbg of both DBs to false
		gGT->db[0].drawEnv.isbg = 0;
		gGT->db[1].drawEnv.isbg = 0;
	}
	else
	{
		// set isbg of both DBs to true
		gGT->db[0].drawEnv.isbg = 1;
		gGT->db[1].drawEnv.isbg = 1;
	}

	if (lev1 != NULL)
	{
		if (lev1->ptr_mesh_info != NULL)
		{
			LevInstDef_UnPack(lev1->ptr_mesh_info);
		}
	}

	MainInit_VisMem(gGT);

	MainInit_RainBuffer(gGT);

	// animates water, 1P mode
	AnimateWater1P(gGT->timer, lev1->numWaterVertices, lev1->ptr_water, lev1->ptr_tex_waterEnvMap, lev1->visOVertSrc);

	gGT->pushBuffer_UI.fadeFromBlack_desiredResult = 0x1000;
	gGT->pushBuffer_UI.fade_step = 0x200;

	numPlyr = gGT->numPlyrCurrGame;

	// stars
	gGT->stars.numStars = (s16)(lev1->stars.numStars / numPlyr);
	gGT->stars.spread = lev1->stars.spread;
	gGT->stars.seed = lev1->stars.seed;
	gGT->stars.distance = lev1->stars.distance;

	// confetti
	gGT->confetti.numParticles_currWord = 0;
	gGT->confetti.numParticles_max = 0;
	gGT->confetti.vanishRate = 0;
	gGT->confetti.velY = -10;

	for (i = 0; i < 4; i++)
	{
		gGT->winnerIndex[i] = 0;
	}

#if 0
    BOTS_EmptyFunc();
#endif

	if ((gGT->gameMode1 & GAME_CUTSCENE) != 0)
	{
		// freecam mode
		gGT->cameraDC[0].cameraMode = CAMERA_MODE_FREECAM;

		// disable all HUD flags
		gGT->hudFlags = 0;

		CS_Cutscene_Start();
	}

	if ((gGT->gameMode1 & ADVENTURE_ARENA) != 0)
	{
		// 0
		if (gGT->podiumRewardID != NOFUNC)
		{
			CS_Podium_FullScene_Init();
		}
	}

	PickupBots_Init();
}

int MainInit_StringToLevID(char *str)
{
	for (int levelID = 0; levelID < 0x41; levelID++)
	{
		char *debugName = data.metaDataLEV[levelID].name_Debug;

		if (strncmp(debugName, str, strlen(debugName)) == 0)
		{
			return levelID;
		}
	}

	return 0;
}

void MainInit_VRAMClear()
{
	DRAWENV drawEnv;

	struct
	{
		int a;
		s16 b1, b2, c, d, e, f;
	} commands;

	SetDefDrawEnv(&drawEnv, 0, 0, 0x400, 0x200);
	drawEnv.dfe = '\x01';
	PutDrawEnv(&drawEnv);

	commands.a = 0x3ffffff;
	commands.b1 = 0;
	commands.b2 = 0x200;
	commands.c = 0;
	commands.d = 0;
	commands.e = 0x3ff;
	commands.f = 0x1ff;
	DrawOTag(&commands);

	commands.d = 0x1ff;
	commands.f = 1;
	DrawOTag(&commands);
}

// Native wraps the VRAM page moves in a platform frame for presentation.
void MainInit_VRAMDisplay()
{
	RECT r;
	DR_MOVE move;

	s16 x[2];
	s16 y[2];

	x[0] = 0;
	x[1] = 0x100;

	y[0] = 0;
	y[1] = 0x128;

	for (int i = 0; i < 2; i++)
	{
		for (int j = 0; j < 2; j++)
		{
			r.x = x[i] + 0x200;
			r.y = 0x10c;
			r.w = 0x100;
			r.h = 0xd8;

			SetDrawMove(&move, &r, x[i], y[j]);

			move.tag |= 0xffffff;

			DrawOTag(&move);
			DrawSync(0);
		}
	}

#ifdef CTR_NATIVE
	Platform_PresentVRAMDisplay();
#endif
}
