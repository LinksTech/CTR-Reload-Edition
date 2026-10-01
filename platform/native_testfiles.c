// ===========================================================================
// SYNTHETIC TEST FILES FOR THE TWO INPUT SELF-TESTS
//
//   ctr_native --dev --make-test-containers <folder>
//   ctr_native --dev --make-test-disc <folder>
//
// A track container (.rldtrack) and a disc image come from strangers. The
// ctests selftest_bad_containers and selftest_bad_disc (CMakeLists.txt,
// cmake/run_selftest.cmake) prove that the game refuses broken ones cleanly:
// they write made-up files with one of these switches and then hand the folder
// to --selftest-containers / --selftest-disc, which must accept every good-*
// file and refuse every bad-* file.
//
// WHY INSIDE THE GAME AND NOT A SMALL TOOL OF ITS OWN. The generators were
// first built as separate console programs. On a build machine with a
// reputation-based scanner every freshly built small unsigned exe of that kind
// was quarantined on its first start, before it ran a single line - so the test
// could not run at all. ctr_native.exe is started by every other test anyway.
//
// NO GAME DATA. Every byte written here is made up on the spot.
//
// Both entry points run before any window, audio or asset initialisation
// (main.c) and return 0 on success, 1 on failure.
// ===========================================================================

int NativeTestFiles_MakeContainers(const char *dir);
int NativeTestFiles_MakeDisc(const char *dir);

// ===========================================================================
// PART 1 - TRACK CONTAINERS
//
// Writes into <folder>:
//   good-*.rldtrack   small containers that are valid in every respect the
//                     game checks - the game must ACCEPT them
//   bad-*.rldtrack    containers that each break exactly one thing relative to
//                     good-synthetic.rldtrack - the game must REFUSE them
//
// THE CHUNK HASHES ARE VALID. The envelope is built with the SHA-256 and the
// field writers of include/rldtrack.inc, so a refusal of a bad-c* or bad-s1*
// file has to come from a CONTENT check and not from the hash. The chunk hash
// is plain SHA-256 without a key - anyone can recompute it, which is exactly
// why these files exist. Only the bad-hash-* files break a hash on purpose.
//
// The reader side of rldtrack.inc is already in this translation unit through
// platform/native_assets.c. Its RLDTRACK_WITH_BUILDER part (the packer's
// writers) is not compiled into the game, and it is not needed here: the one
// 32-bit writer and the six bytes of PARM are written below, and every chunk
// is read back with the game's own parsers before the program says "good".
//
// NAMES. bad-<case>-<what>.rldtrack. The cases:
//   s1   the bank header inside SNDB (numSamples, spuIndexArr[]) that
//        HOWL_Bank.c Bank_AssignSpuAddrs writes through
//   c1   the icon table offsets Rld_LevMap adds up (32-bit wrap)
//   c2   the mesh_info / skybox offsets Rld_MemNeed and
//        NativeTrack_SkyPrimBytes add up (32-bit wrap)
//   c3   the VRM TIM chain LOAD_VramFileCallback walks
//   c4   a LEVD shorter than struct Level
//   c5   the LEV pointer map LOAD_RunPtrMap relocates (repeats, ranges)
//   c6   Model.id, the index into gGT->modelPtr[0xe3] (LibraryOfModels.c)
//   c7   the spawn table CAM.c reads without a check
//   c10  META memTotal / primBytes past what any LEVD can need - the sum in
//        NativeTrack_MempackExtraNeeded wrapped (MEMPACK red screen)
//   c12  the end-of-race cameras (spawn slot 2): a mode outside
//        data.EndOfRace_Camera_Size (CAM.c steps by it), a respawn point or
//        path start past the restart points, a count past the body
//   c13  the map table (spawn slot 0) with a width or height of 0 -
//        UI_Map_GetIconPos divides by it
//   c14  the restart points: a quadblock checkpointIndex or a node link past
//        them (VehStuckProc, VehLap, RB_Warpball index with them), a count
//        outside 0..255, a table past the body
//   c15  distToFinish 0 on restart point 0 - VehLap and RB_Warpball take the
//        remainder by it
// and for the envelope: hdr (header), cnt (counts), off (offsets), len
// (lengths), dir (directory), hash, meta, mem (memory need), trunc / truncfix
// (the good file cut short, file_size left as it was / patched to the cut).
//
// SELF-CHECK. Every file is read back and run through a reference model: the
// real reader from rldtrack.inc (Rld_Open, Rld_ReadChunk, Rld_ParseMeta,
// Rld_ParseSndb, Rld_ParseParm, Rld_LevMap) plus the content rules written
// out below in 64-bit arithmetic (NativeTestModel_*). One line per file says
// what differs from the good container and the model's verdict; the switch
// fails if a good file is refused or a bad file accepted by the model - then a
// variant no longer triggers what its name says. The model is NOT the game's
// check; the game's answer comes from --selftest-containers.
// ===========================================================================

// ---------------------------------------------------------------------------
// THE SYNTHETIC LEV
//
// A LEV file is: word 0 = ptrMapOffset, then the BODY (ptrMapOffset bytes,
// struct Level at body offset 0), then the pointer map (u32 numBytes, then
// numBytes/4 body offsets of words that LOAD_RunPtrMap relocates). The game
// relocates in LOAD_DramFileCallback (LOAD_File.c) with originBytes =
// ptrMapOffset: every patch site must be a word inside the body, every value
// stored there must lie in 0..ptrMapOffset. A field that is not in the map is
// not a pointer.
//
// Body layout (offsets body-relative; every pointer is a body offset):
//
//   0x000  struct Level (sizeof 0x1f4, include/namespace_Level.h), padded to 0x200
//            0x000 ptr_mesh_info       -> 0x200   mapped
//            0x004 ptr_skybox          -> 0x220   mapped
//            0x00c numInstances        0 (no InstDefs, ptrInstDefs 0)
//            0x014 numModels           1
//            0x018 ptrModelsPtrArray   -> 0x3a0   mapped
//            0x03c levTexLookup        -> 0x350   mapped (the minimap)
//            0x040 ptr_named_tex_array -> 0x360   mapped (the icons)
//            0x06c DriverSpawn[8]      eight distinct start spots
//            0x134 ptrSpawnType1       -> 0x3d0   mapped
//            0x148 cnt_restart_points  1
//            0x14c ptr_restart_points  -> 0x430   mapped
//   0x200  mesh_info (0x20): 1 quadblock, 9 vertices, no BSP
//            +0x0c ptrQuadBlockArray   -> 0x260   mapped (0x20c)
//            +0x10 ptrVertexArray      -> 0x2c0   mapped (0x210)
//   0x220  Skybox (0x38): no vertices, eight segments of 0 faces
//   0x260  QuadBlock (0x5c): index[0..8] = 0..8, everything else 0
//   0x2c0  LevVertex[9] (0x10 each): a 3 x 3 grid
//   0x350  LevTexLookup: numIcon 2, firstIcon -> 0x360   mapped (0x354)
//   0x360  Icon[2] (0x20 each): "map_top" index 3, "map_bottom" index 4 - the
//          two map halves Rld_LevMap looks for: 4-bit, tpage 0x0008 (page x
//          512), clut 0x0420 (x 512, y 16), 16 x 8 texels each, upright
//   0x3a0  Model *[1]          -> 0x3b0              mapped (0x3a0)
//   0x3b0  Model "pipe1", id 0x57 (STATIC_PIPE1, inside 0..0xe2),
//          numHeaders 0, headers NULL
//   0x3d0  SpawnType1: count 3, room for four slots
//            slot 0 -> 0x3f0 map metadata (UIMapSpawnMetadata)  mapped (0x3d4)
//            slot 1 -> 0x410 driver spawn (SpawnPosRot)         mapped (0x3d8)
//            slot 2 -> 0x420 end-of-race cameras, count 1       mapped (0x3dc)
//            slot 3    0 (unused at count 3; bad-c7-count4-* raises the count)
//   0x3f0  UIMapSpawnMetadata: world -2000..2000 in X and Y (non-zero ranges -
//          UI_Map_GetIconPos divides by them), mode 0
//   0x410  SpawnPosRot
//   0x420  end-of-race cameras: s16 count 1, then one camera - s16 respawn
//          point 0, s16 mode 4 (look at, EndOfRace_Camera_Size 6), 6 bytes
//          of position; ends at 0x42c
//   0x430  CheckpointNode (restart point 0): distToFinish 1000 (the lap
//          divides by it), forward to itself, no branches (0xff)
//   0x440  end of the body = ptrMapOffset
//
// The quadblock's checkpointIndex is 0, restart point 0.
//
// Pointer map: the 14 mapped words above, ascending, no repeats.
// LEV size 4 + 0x440 + 4 + 14 * 4 = 0x480 (1152) bytes.
//
// Rld_MemNeed of this LEV: 1 quadblock -> primBytes 4 * 0x34 = 208, sky 0,
// total 1152 + 2 * 208 = 1568. META declares primBytes 208 and memTotal
// 1568 + NTC_MEM_HEADROOM: the game refuses only a META that declares TOO
// LITTLE, and the headroom keeps the c5 variants that append a map entry
// (+4 bytes) from being refused for memory instead of for their map.
// ---------------------------------------------------------------------------

#define NTC_LEV_BODY 4u
#define NTC_LEVEL_SIZE 0x1f4u // sizeof(struct Level)
#define NTC_MESH 0x200u
#define NTC_SKY 0x220u
#define NTC_QUAD 0x260u
#define NTC_VERT 0x2c0u
#define NTC_LOOKUP 0x350u
#define NTC_ICONS 0x360u
#define NTC_MODELARR 0x3a0u
#define NTC_MODEL 0x3b0u
#define NTC_SPAWN1 0x3d0u
#define NTC_UIMAP 0x3f0u
#define NTC_SPAWNPOS 0x410u
#define NTC_EORCAM 0x420u
#define NTC_RESTART 0x430u
#define NTC_BODY_SIZE 0x440u
#define NTC_MAP_AT (NTC_LEV_BODY + NTC_BODY_SIZE) // file offset of numBytes

#define NTC_MEM_HEADROOM 256u

// struct Level fields (namespace_Level.h)
#define NTC_LVL_MESH 0x000u
#define NTC_LVL_SKY 0x004u
#define NTC_LVL_NUM_MODELS 0x014u
#define NTC_LVL_MODELS 0x018u
#define NTC_LVL_TEXLOOKUP 0x03cu
#define NTC_LVL_NAMEDTEX 0x040u
#define NTC_LVL_DRIVERSPAWN 0x06cu
#define NTC_LVL_SPAWN1 0x134u
#define NTC_LVL_NUM_RESTART 0x148u
#define NTC_LVL_RESTART 0x14cu

// The race tables (c12..c15)
#define NTC_NODE_BYTES 12u        // struct CheckpointNode
#define NTC_NODE_DISTANCE 6u      // distToFinish, u16
#define NTC_NODE_FORWARD 8u       // nextIndex_forward, then _left, _backward, _right
#define NTC_NODE_NONE 0xffu
#define NTC_NODE_GOOD_DISTANCE 1000u
#define NTC_QUAD_CHECKPOINT 0x3eu // QuadBlock.checkpointIndex, u8
#define NTC_EOR_RESPAWN (NTC_EORCAM + 2u)
#define NTC_EOR_MODE (NTC_EORCAM + 4u)
#define NTC_EOR_DATA (NTC_EORCAM + 6u)
#define NTC_EOR_MODE_LOOKAT 4u    // EndOfRace_Camera_Size[4] = 6
#define NTC_UIMAP_BYTES 0x14u     // struct UIMapSpawnMetadata

#define NTC_MODEL_ID_OFFSET 0x10u
#define NTC_MODEL_ID 0x57u       // STATIC_PIPE1
#define NTC_MODEL_ID_COUNT 0xe3u // gGT->modelPtr[0xe3]
#define NTC_SPAWN_SLOT(k) (NTC_SPAWN1 + 4u + 4u * (u32)(k))

global_variable const u32 s_testLevMap[] = {
    NTC_LVL_MESH,       NTC_LVL_SKY,         NTC_LVL_MODELS,    NTC_LVL_TEXLOOKUP, NTC_LVL_NAMEDTEX,      NTC_LVL_SPAWN1,        NTC_LVL_RESTART,
    NTC_MESH + 0xc,     NTC_MESH + 0x10,     NTC_LOOKUP + 4,    NTC_MODELARR,      NTC_SPAWN_SLOT(0),     NTC_SPAWN_SLOT(1),     NTC_SPAWN_SLOT(2),
};
#define NTC_MAP_COUNT ((u32)(sizeof(s_testLevMap) / sizeof(s_testLevMap[0])))

// ---------------------------------------------------------------------------
// THE SYNTHETIC VRM
//
// The chained form LOAD_VramFileCallback walks: word 0 = 0x20, then per block
// a u32 size and a TIM of that size, the chain ends with a size of 0. A TIM is
// struct VramHeader: 0x00 magic 0x10, 0x04 flags 2 (16-bit direct), 0x08 bnum
// (12 + pixel bytes), 0x0c RECT x y w h (s16), 0x14 pixels, w * h halfwords.
// Block size = 0x14 + w * h * 2, as on the disc.
//
//   0x000  0x20
//   0x004  size 564   block 1: x 512 y 0 w 16 h 17 - rows 0..15 the map
//   0x008  TIM header        texels (4-bit, 4 halfwords used per row), row 16
//   0x01c  pixels            the 16-entry colour table at x 512
//   0x23c  size 36    block 2: x 528 y 0 w 4 h 2, a small pattern
//   0x240  TIM header
//   0x254  pixels
//   0x264  0          end of the chain
//   0x268  = 616 bytes
//
// good-vrm-single.rldtrack carries block 1 alone as a single TIM (word 0 is
// the TIM magic, not 0x20) - the other form LOAD_VramFileCallback takes.
// ---------------------------------------------------------------------------

#define NTC_VRM_PACKED 0x20u
#define NTC_VRM_B1 0x004u
#define NTC_VRM_H1 0x008u
#define NTC_VRM_B1_W 16u
#define NTC_VRM_B1_H 17u
#define NTC_VRM_B2 (NTC_VRM_H1 + 0x14u + NTC_VRM_B1_W * NTC_VRM_B1_H * 2u)
#define NTC_VRM_H2 (NTC_VRM_B2 + 4u)
#define NTC_VRM_B2_W 4u
#define NTC_VRM_B2_H 2u
#define NTC_VRM_END (NTC_VRM_H2 + 0x14u + NTC_VRM_B2_W * NTC_VRM_B2_H * 2u)
#define NTC_VRM_SIZE (NTC_VRM_END + 4u)

#define NTC_MAP_TPAGE 0x0008u // 4-bit, page x 512, y 0
#define NTC_MAP_CLUT 0x0420u  // x (0x20 * 16) = 512, y 16

// ---------------------------------------------------------------------------
// THE SYNTHETIC SNDB
//
// Layout as Rld_ParseSndb reads it (rldtrack.inc, version 2):
//
//   0x00  'SNDB', version 2, entryCount 2, spuFixupCount 1, reserved 0,
//         payloadOffset 52, playBank 4, playSong 3
//   0x14  entry 0: bank 4, payload offset 0x0000, size 0x1000
//   0x20  entry 1: sequence 3, payload offset 0x1000, size 0x0800
//   0x2c  SPU row fixup: row 300, spuAddr 0, spuSize 0x100 (x 8 = 2048 bytes)
//   0x34  payload
//         bank 4, sector 0: the bank header Bank_AssignSpuAddrs reads
//                (struct SampleBlockHeader): s16 numSamples 1, s16
//                spuIndexArr[1] = {300}; sector 1: 2048 bytes of sample data
//                (silence), exactly spuSize * 8 of row 300
//         sequence 3: struct CseqHeader songSize 8, no samples, no songs
//
// Row 300 is below the host's 528 SPU rows (RLD_HOWL_SPUADDR_COUNT) and the
// fixup gives it its size, so the whole bank is checkable without KART.HWL.
// ---------------------------------------------------------------------------

#define NTC_SNDB_PLAY_BANK 4u
#define NTC_SNDB_PLAY_SONG 3u
#define NTC_SNDB_ROW 300u
#define NTC_SNDB_ROW_SIZE 0x100u
#define NTC_SNDB_ENTRIES 2u
#define NTC_SNDB_FIXUPS 1u
#define NTC_SNDB_FIXUP_AT (RLD_SNDB_HEADER_SIZE + NTC_SNDB_ENTRIES * RLD_SNDB_ENTRY_SIZE)
#define NTC_SNDB_PAYLOAD (NTC_SNDB_FIXUP_AT + NTC_SNDB_FIXUPS * RLD_SNDB_SPUFIXUP_SIZE)
#define NTC_SNDB_BANK_SIZE 0x1000u
#define NTC_SNDB_SONG_SIZE 0x0800u
#define NTC_SNDB_SIZE (NTC_SNDB_PAYLOAD + NTC_SNDB_BANK_SIZE + NTC_SNDB_SONG_SIZE)
#define NTC_SNDB_BANK_AT NTC_SNDB_PAYLOAD // numSamples; spuIndexArr from +2

// META as the packer writes it: 0x00 metaVersion, 0x04..0x23 reserved zero,
// 0x24 trackVersion, 0x28 modes, 0x2c primBytes, 0x30 memTotal, 0x34
// stringCount, from 0x38 per string u32 length + bytes.
#define NTC_META_PRIM 0x2cu
#define NTC_META_TOTAL 0x30u
#define NTC_META_COUNT 0x34u
#define NTC_META_STR0 0x38u

// ---------------------------------------------------------------------------
// Byte buffers
// ---------------------------------------------------------------------------

struct NativeTestBlob
{
	u8 *data;
	size_t size;
};

internal void NativeTestCont_Fatal(const char *what)
{
	fprintf(stderr, "[selftest] test container generator: %s\n", what);
	exit(1);
}

internal void NativeTestBlob_Alloc(struct NativeTestBlob *b, size_t size)
{
	b->data = (u8 *)calloc(1, size ? size : 1u);
	b->size = size;
	if (b->data == NULL)
	{
		NativeTestCont_Fatal("out of memory");
	}
}

internal void NativeTestBlob_Copy(struct NativeTestBlob *dst, const struct NativeTestBlob *src)
{
	NativeTestBlob_Alloc(dst, src->size);
	if (src->size != 0u)
	{
		memcpy(dst->data, src->data, src->size);
	}
}

internal void NativeTestBlob_Free(struct NativeTestBlob *b)
{
	free(b->data);
	b->data = NULL;
	b->size = 0;
}

internal void NativeTestBlob_Grow(struct NativeTestBlob *b, size_t size)
{
	u8 *grown = (u8 *)realloc(b->data, size);

	if (grown == NULL)
	{
		NativeTestCont_Fatal("out of memory");
	}
	memset(&grown[b->size], 0, size - b->size);
	b->data = grown;
	b->size = size;
}

internal void NativeTestBlob_Put16(struct NativeTestBlob *b, size_t at, u32 value)
{
	if ((at + 2u) > b->size)
	{
		NativeTestCont_Fatal("16-bit write past the end");
	}
	Rld_WriteLE16(&b->data[at], value);
}

// Rld_WriteLE32 belongs to the packer side of rldtrack.inc, which the game
// does not compile.
internal void NativeTestBlob_Put32(struct NativeTestBlob *b, size_t at, u32 value)
{
	if ((at + 4u) > b->size)
	{
		NativeTestCont_Fatal("32-bit write past the end");
	}
	b->data[at + 0u] = (u8)(value);
	b->data[at + 1u] = (u8)(value >> 8);
	b->data[at + 2u] = (u8)(value >> 16);
	b->data[at + 3u] = (u8)(value >> 24);
}

internal void NativeTestBlob_Put64(struct NativeTestBlob *b, size_t at, u64 value)
{
	if ((at + 8u) > b->size)
	{
		NativeTestCont_Fatal("64-bit write past the end");
	}
	Rld_WriteLE64(&b->data[at], value);
}

// ---------------------------------------------------------------------------
// Building the parts
// ---------------------------------------------------------------------------

#define NTC_PART_META 0
#define NTC_PART_LEVD 1
#define NTC_PART_VRMD 2
#define NTC_PART_SNDB 3
#define NTC_PART_PARM 4
#define NTC_PART_COUNT 5

global_variable const char *const s_testPartNames[NTC_PART_COUNT] = {"META", "LEVD", "VRMD", "SNDB", "PARM"};

struct NativeTestParts
{
	struct NativeTestBlob meta;
	struct NativeTestBlob lev;
	struct NativeTestBlob vrm;
	struct NativeTestBlob sndb; // only with hasSndb
	struct NativeTestBlob parm; // only with hasParm
	int hasSndb;
	int hasParm;
};

internal void NativeTestParts_Copy(struct NativeTestParts *dst, const struct NativeTestParts *src)
{
	memset(dst, 0, sizeof(*dst));
	NativeTestBlob_Copy(&dst->meta, &src->meta);
	NativeTestBlob_Copy(&dst->lev, &src->lev);
	NativeTestBlob_Copy(&dst->vrm, &src->vrm);
	dst->hasSndb = src->hasSndb;
	dst->hasParm = src->hasParm;
	if (src->hasSndb)
	{
		NativeTestBlob_Copy(&dst->sndb, &src->sndb);
	}
	if (src->hasParm)
	{
		NativeTestBlob_Copy(&dst->parm, &src->parm);
	}
}

internal void NativeTestParts_Free(struct NativeTestParts *p)
{
	NativeTestBlob_Free(&p->meta);
	NativeTestBlob_Free(&p->lev);
	NativeTestBlob_Free(&p->vrm);
	NativeTestBlob_Free(&p->sndb);
	NativeTestBlob_Free(&p->parm);
}

internal const struct NativeTestBlob *NativeTestParts_Get(const struct NativeTestParts *p, int part)
{
	switch (part)
	{
	case NTC_PART_META:
		return &p->meta;
	case NTC_PART_LEVD:
		return &p->lev;
	case NTC_PART_VRMD:
		return &p->vrm;
	case NTC_PART_SNDB:
		return p->hasSndb ? &p->sndb : NULL;
	default:
		return p->hasParm ? &p->parm : NULL;
	}
}

internal void NativeTestLev_Set32(struct NativeTestBlob *lev, u32 bodyOffset, u32 value)
{
	NativeTestBlob_Put32(lev, NTC_LEV_BODY + bodyOffset, value);
}

internal void NativeTestLev_Set16(struct NativeTestBlob *lev, u32 bodyOffset, u32 value)
{
	NativeTestBlob_Put16(lev, NTC_LEV_BODY + bodyOffset, value);
}

internal void NativeTestLev_Build(struct NativeTestBlob *lev)
{
	u32 i;

	NativeTestBlob_Alloc(lev, NTC_LEV_BODY + NTC_BODY_SIZE + 4u + NTC_MAP_COUNT * 4u);
	NativeTestBlob_Put32(lev, 0, NTC_BODY_SIZE);

	// struct Level
	NativeTestLev_Set32(lev, NTC_LVL_MESH, NTC_MESH);
	NativeTestLev_Set32(lev, NTC_LVL_SKY, NTC_SKY);
	NativeTestLev_Set32(lev, NTC_LVL_NUM_MODELS, 1u);
	NativeTestLev_Set32(lev, NTC_LVL_MODELS, NTC_MODELARR);
	NativeTestLev_Set32(lev, NTC_LVL_TEXLOOKUP, NTC_LOOKUP);
	NativeTestLev_Set32(lev, NTC_LVL_NAMEDTEX, NTC_ICONS);
	for (i = 0; i < 8u; i++)
	{
		// DriverSpawn[i].pos = (100 + 100 i, 0, 0), rot 0
		NativeTestLev_Set16(lev, NTC_LVL_DRIVERSPAWN + 12u * i, 100u + 100u * i);
	}
	NativeTestLev_Set32(lev, NTC_LVL_SPAWN1, NTC_SPAWN1);
	NativeTestLev_Set32(lev, NTC_LVL_NUM_RESTART, 1u);
	NativeTestLev_Set32(lev, NTC_LVL_RESTART, NTC_RESTART);

	// mesh_info: numQuadBlock, numVertex, the two arrays
	NativeTestLev_Set32(lev, NTC_MESH + 0x00u, 1u);
	NativeTestLev_Set32(lev, NTC_MESH + 0x04u, 9u);
	NativeTestLev_Set32(lev, NTC_MESH + 0x0cu, NTC_QUAD);
	NativeTestLev_Set32(lev, NTC_MESH + 0x10u, NTC_VERT);

	// Skybox: all zero.

	// QuadBlock: the nine vertex indices
	for (i = 0; i < 9u; i++)
	{
		NativeTestLev_Set16(lev, NTC_QUAD + 2u * i, i);
	}

	// LevVertex[9]: a 3 x 3 grid, 256 units apart, grey
	for (i = 0; i < 9u; i++)
	{
		const u32 vertex = NTC_VERT + 0x10u * i;

		NativeTestLev_Set16(lev, vertex + 0x0u, (i % 3u) * 256u);
		NativeTestLev_Set16(lev, vertex + 0x4u, (i / 3u) * 256u);
		NativeTestLev_Set32(lev, vertex + 0x8u, 0x00808080u);
		NativeTestLev_Set32(lev, vertex + 0xcu, 0x00808080u);
	}

	// LevTexLookup and the two map icons
	NativeTestLev_Set32(lev, NTC_LOOKUP + 0x0u, 2u);
	NativeTestLev_Set32(lev, NTC_LOOKUP + 0x4u, NTC_ICONS);
	for (i = 0; i < 2u; i++)
	{
		const size_t icon = NTC_LEV_BODY + NTC_ICONS + 0x20u * i;
		const u32 v0 = 8u * i;
		u8 *layout = &lev->data[icon + 0x14u];

		memcpy(&lev->data[icon], (i == 0u) ? "map_top" : "map_bottom", (i == 0u) ? 7u : 10u);
		NativeTestBlob_Put32(lev, icon + 0x10u, 3u + i); // global_IconArray_Index 3 / 4
		// TextureLayout u0 v0 clut u1 v1 tpage u2 v2 u3 v3: upright 16 x 8
		layout[0] = 0;
		layout[1] = (u8)v0;
		Rld_WriteLE16(&layout[2], NTC_MAP_CLUT);
		layout[4] = 15;
		layout[5] = (u8)v0;
		Rld_WriteLE16(&layout[6], NTC_MAP_TPAGE);
		layout[8] = 0;
		layout[9] = (u8)(v0 + 7u);
		layout[10] = 15;
		layout[11] = (u8)(v0 + 7u);
	}

	// One model, pointed to from the model array
	NativeTestLev_Set32(lev, NTC_MODELARR, NTC_MODEL);
	memcpy(&lev->data[NTC_LEV_BODY + NTC_MODEL], "pipe1", 5);
	NativeTestLev_Set16(lev, NTC_MODEL + NTC_MODEL_ID_OFFSET, NTC_MODEL_ID);

	// SpawnType1: count 3, slots 0..2
	NativeTestLev_Set32(lev, NTC_SPAWN1, 3u);
	NativeTestLev_Set32(lev, NTC_SPAWN_SLOT(0), NTC_UIMAP);
	NativeTestLev_Set32(lev, NTC_SPAWN_SLOT(1), NTC_SPAWNPOS);
	NativeTestLev_Set32(lev, NTC_SPAWN_SLOT(2), NTC_EORCAM);

	// struct UIMap: worldEndX/Y, worldStartX/Y, iconSizeX/Y, iconStartX/Y, mode
	NativeTestLev_Set16(lev, NTC_UIMAP + 0x0u, 2000u);
	NativeTestLev_Set16(lev, NTC_UIMAP + 0x2u, 2000u);
	NativeTestLev_Set16(lev, NTC_UIMAP + 0x4u, 0x10000u - 2000u);
	NativeTestLev_Set16(lev, NTC_UIMAP + 0x6u, 0x10000u - 2000u);
	NativeTestLev_Set16(lev, NTC_UIMAP + 0x8u, 64u);
	NativeTestLev_Set16(lev, NTC_UIMAP + 0xau, 32u);
	NativeTestLev_Set16(lev, NTC_UIMAP + 0xcu, 400u);
	NativeTestLev_Set16(lev, NTC_UIMAP + 0xeu, 180u);

	// SpawnPosRot and the restart point: at the first start spot
	NativeTestLev_Set16(lev, NTC_SPAWNPOS, 100u);
	NativeTestLev_Set16(lev, NTC_RESTART, 100u);

	// The restart point: a track length, forward to itself, no branches.
	NativeTestLev_Set16(lev, NTC_RESTART + NTC_NODE_DISTANCE, NTC_NODE_GOOD_DISTANCE);
	lev->data[NTC_LEV_BODY + NTC_RESTART + NTC_NODE_FORWARD + 0u] = 0u;
	lev->data[NTC_LEV_BODY + NTC_RESTART + NTC_NODE_FORWARD + 1u] = (u8)NTC_NODE_NONE;
	lev->data[NTC_LEV_BODY + NTC_RESTART + NTC_NODE_FORWARD + 2u] = (u8)NTC_NODE_NONE;
	lev->data[NTC_LEV_BODY + NTC_RESTART + NTC_NODE_FORWARD + 3u] = (u8)NTC_NODE_NONE;

	// One end-of-race camera: respawn point 0, mode 4, a position.
	NativeTestLev_Set16(lev, NTC_EORCAM, 1u);
	NativeTestLev_Set16(lev, NTC_EOR_RESPAWN, 0u);
	NativeTestLev_Set16(lev, NTC_EOR_MODE, NTC_EOR_MODE_LOOKAT);
	NativeTestLev_Set16(lev, NTC_EOR_DATA + 0u, 100u);
	NativeTestLev_Set16(lev, NTC_EOR_DATA + 2u, 200u);
	NativeTestLev_Set16(lev, NTC_EOR_DATA + 4u, 0u);

	// The pointer map
	NativeTestBlob_Put32(lev, NTC_MAP_AT, NTC_MAP_COUNT * 4u);
	for (i = 0; i < NTC_MAP_COUNT; i++)
	{
		NativeTestBlob_Put32(lev, NTC_MAP_AT + 4u + 4u * i, s_testLevMap[i]);
	}
}

// The map sits at the end of the LEV, so an entry removed or appended only
// moves what follows it inside the map.
internal u32 NativeTestLev_MapCount(const struct NativeTestBlob *lev)
{
	return Rld_ReadLE32(&lev->data[NTC_MAP_AT]) / 4u;
}

internal void NativeTestLev_MapRemove(struct NativeTestBlob *lev, u32 site)
{
	const u32 count = NativeTestLev_MapCount(lev);
	u32 i;

	for (i = 0; i < count; i++)
	{
		const size_t at = NTC_MAP_AT + 4u + 4u * i;

		if (Rld_ReadLE32(&lev->data[at]) == site)
		{
			memmove(&lev->data[at], &lev->data[at + 4u], lev->size - (at + 4u));
			lev->size -= 4u;
			NativeTestBlob_Put32(lev, NTC_MAP_AT, (count - 1u) * 4u);
			return;
		}
	}

	NativeTestCont_Fatal("a map entry to remove is not in the map");
}

internal void NativeTestLev_MapAppend(struct NativeTestBlob *lev, u32 site)
{
	const u32 count = NativeTestLev_MapCount(lev);

	NativeTestBlob_Grow(lev, lev->size + 4u);
	NativeTestBlob_Put32(lev, lev->size - 4u, site);
	NativeTestBlob_Put32(lev, NTC_MAP_AT, (count + 1u) * 4u);
}

// A LEV that is consistent in itself - word 0, an empty map right behind the
// body - but whose body is shorter than struct Level.
internal void NativeTestLev_BuildShort(struct NativeTestBlob *lev, u32 bodySize)
{
	NativeTestBlob_Alloc(lev, NTC_LEV_BODY + bodySize + 4u);
	NativeTestBlob_Put32(lev, 0, bodySize);
}

internal void NativeTestVrm_PutTim(struct NativeTestBlob *vrm, size_t at, u32 x, u32 y, u32 w, u32 h)
{
	NativeTestBlob_Put32(vrm, at + 0x00u, 0x10u);
	NativeTestBlob_Put32(vrm, at + 0x04u, 0x02u);
	NativeTestBlob_Put32(vrm, at + 0x08u, 12u + w * h * 2u);
	NativeTestBlob_Put16(vrm, at + 0x0cu, x);
	NativeTestBlob_Put16(vrm, at + 0x0eu, y);
	NativeTestBlob_Put16(vrm, at + 0x10u, w);
	NativeTestBlob_Put16(vrm, at + 0x12u, h);
}

// Block 1's pixels: rows 0..15 the map texels, row 16 the colour table.
internal void NativeTestVrm_PutMapPixels(struct NativeTestBlob *vrm, size_t pixels)
{
	u32 row;
	u32 col;

	for (row = 0; row < 16u; row++)
	{
		for (col = 0; col < 4u; col++)
		{
			// Four 4-bit texels per halfword: index 1 on the border of each
			// 16 x 8 half, index 2 inside.
			u32 word = 0;
			u32 t;

			for (t = 0; t < 4u; t++)
			{
				const u32 u = col * 4u + t;
				const u32 v = row % 8u;
				const u32 index = ((u == 0u) || (u == 15u) || (v == 0u) || (v == 7u)) ? 1u : 2u;

				word |= index << (t * 4u);
			}

			NativeTestBlob_Put16(vrm, pixels + (row * NTC_VRM_B1_W + col) * 2u, word);
		}
	}

	NativeTestBlob_Put16(vrm, pixels + (16u * NTC_VRM_B1_W + 0u) * 2u, 0x0000u); // 0: transparent
	NativeTestBlob_Put16(vrm, pixels + (16u * NTC_VRM_B1_W + 1u) * 2u, 0x7fffu); // 1: white
	NativeTestBlob_Put16(vrm, pixels + (16u * NTC_VRM_B1_W + 2u) * 2u, 0x03e0u); // 2: green
	for (col = 3; col < 16u; col++)
	{
		NativeTestBlob_Put16(vrm, pixels + (16u * NTC_VRM_B1_W + col) * 2u, 0x001fu);
	}
}

internal void NativeTestVrm_Build(struct NativeTestBlob *vrm)
{
	u32 i;

	NativeTestBlob_Alloc(vrm, NTC_VRM_SIZE);
	NativeTestBlob_Put32(vrm, 0, NTC_VRM_PACKED);

	NativeTestBlob_Put32(vrm, NTC_VRM_B1, 0x14u + NTC_VRM_B1_W * NTC_VRM_B1_H * 2u);
	NativeTestVrm_PutTim(vrm, NTC_VRM_H1, 512u, 0u, NTC_VRM_B1_W, NTC_VRM_B1_H);
	NativeTestVrm_PutMapPixels(vrm, NTC_VRM_H1 + 0x14u);

	NativeTestBlob_Put32(vrm, NTC_VRM_B2, 0x14u + NTC_VRM_B2_W * NTC_VRM_B2_H * 2u);
	NativeTestVrm_PutTim(vrm, NTC_VRM_H2, 512u + NTC_VRM_B1_W, 0u, NTC_VRM_B2_W, NTC_VRM_B2_H);
	for (i = 0; i < NTC_VRM_B2_W * NTC_VRM_B2_H; i++)
	{
		NativeTestBlob_Put16(vrm, NTC_VRM_H2 + 0x14u + 2u * i, 0x4210u + i);
	}

	NativeTestBlob_Put32(vrm, NTC_VRM_END, 0u);
}

internal void NativeTestVrm_BuildSingle(struct NativeTestBlob *vrm)
{
	NativeTestBlob_Alloc(vrm, 0x14u + NTC_VRM_B1_W * NTC_VRM_B1_H * 2u);
	NativeTestVrm_PutTim(vrm, 0, 512u, 0u, NTC_VRM_B1_W, NTC_VRM_B1_H);
	NativeTestVrm_PutMapPixels(vrm, 0x14u);
}

internal void NativeTestSndb_Build(struct NativeTestBlob *sndb)
{
	NativeTestBlob_Alloc(sndb, NTC_SNDB_SIZE);
	memcpy(sndb->data, "SNDB", 4);
	NativeTestBlob_Put16(sndb, 0x04, RLD_SNDB_VERSION);
	NativeTestBlob_Put16(sndb, 0x06, NTC_SNDB_ENTRIES);
	NativeTestBlob_Put16(sndb, 0x08, NTC_SNDB_FIXUPS);
	NativeTestBlob_Put32(sndb, 0x0c, NTC_SNDB_PAYLOAD);
	NativeTestBlob_Put16(sndb, 0x10, NTC_SNDB_PLAY_BANK);
	NativeTestBlob_Put16(sndb, 0x12, NTC_SNDB_PLAY_SONG);

	// entries: kind u8, reserved u8, index u16, offset u32, size u32
	sndb->data[0x14] = (u8)RLD_SNDB_KIND_BANK;
	NativeTestBlob_Put16(sndb, 0x16, NTC_SNDB_PLAY_BANK);
	NativeTestBlob_Put32(sndb, 0x18, 0u);
	NativeTestBlob_Put32(sndb, 0x1c, NTC_SNDB_BANK_SIZE);
	sndb->data[0x20] = (u8)RLD_SNDB_KIND_SONG;
	NativeTestBlob_Put16(sndb, 0x22, NTC_SNDB_PLAY_SONG);
	NativeTestBlob_Put32(sndb, 0x24, NTC_SNDB_BANK_SIZE);
	NativeTestBlob_Put32(sndb, 0x28, NTC_SNDB_SONG_SIZE);

	// SPU row fixup: index, spuAddr, spuSize, reserved
	NativeTestBlob_Put16(sndb, NTC_SNDB_FIXUP_AT + 0u, NTC_SNDB_ROW);
	NativeTestBlob_Put16(sndb, NTC_SNDB_FIXUP_AT + 4u, NTC_SNDB_ROW_SIZE);

	// bank header: numSamples 1, spuIndexArr {row}; sector 1 is silence
	NativeTestBlob_Put16(sndb, NTC_SNDB_BANK_AT + 0u, 1u);
	NativeTestBlob_Put16(sndb, NTC_SNDB_BANK_AT + 2u, NTC_SNDB_ROW);

	// sequence: CseqHeader songSize 8, 0 long, 0 short samples, 0 songs
	NativeTestBlob_Put32(sndb, NTC_SNDB_PAYLOAD + NTC_SNDB_BANK_SIZE, 8u);
}

// PARM as Rld_ParseParm reads it: version 1, count 2, then {key, length,
// value}: reverb level 2, bot row 0 - the order the packer's writer uses.
internal void NativeTestParm_Build(struct NativeTestBlob *parm)
{
	NativeTestBlob_Alloc(parm, RLD_PARM_HEADER_SIZE + 2u * (RLD_PARM_ENTRY_HEADER + 1u));
	NativeTestBlob_Put16(parm, 0x00, RLD_PARM_VERSION);
	NativeTestBlob_Put16(parm, 0x02, 2u);
	NativeTestBlob_Put16(parm, 0x04, RLD_PARM_KEY_REVERB);
	NativeTestBlob_Put16(parm, 0x06, 1u);
	parm->data[0x08] = 2u;
	NativeTestBlob_Put16(parm, 0x09, RLD_PARM_KEY_BOTS);
	NativeTestBlob_Put16(parm, 0x0b, 1u);
	parm->data[0x0d] = 0u;
}

internal void NativeTestMeta_Build(struct NativeTestBlob *meta, const struct NativeTestBlob *lev, const char *name)
{
	static const char author[] = "CTR Reload self-test";
	struct RldMemNeed need;
	const size_t nameLen = strlen(name);
	const size_t authorLen = strlen(author);

	Rld_MemNeed(&need, lev->data, lev->size);

	NativeTestBlob_Alloc(meta, RLD_META_FIXED_SIZE + 4u + nameLen + 4u + authorLen);
	NativeTestBlob_Put32(meta, 0x00, RLD_META_VERSION);
	NativeTestBlob_Put32(meta, 0x24, 1u);
	NativeTestBlob_Put32(meta, 0x28, RLD_MODE_RACE);
	NativeTestBlob_Put32(meta, NTC_META_PRIM, need.primBytes);
	NativeTestBlob_Put32(meta, NTC_META_TOTAL, need.total + NTC_MEM_HEADROOM);
	NativeTestBlob_Put32(meta, NTC_META_COUNT, RLD_STRING_COUNT);
	NativeTestBlob_Put32(meta, NTC_META_STR0, (u32)nameLen);
	memcpy(&meta->data[NTC_META_STR0 + 4u], name, nameLen);
	NativeTestBlob_Put32(meta, NTC_META_STR0 + 4u + nameLen, (u32)authorLen);
	memcpy(&meta->data[NTC_META_STR0 + 8u + nameLen], author, authorLen);
}

// ---------------------------------------------------------------------------
// The envelope, in the layout the packer writes
//
//   0x00 magic ("RLDTRACK"), 0x08 major u16, 0x0a minor u16, 0x0c flags u32,
//   0x10 chunk_count u32, 0x14 reserved u32, 0x18 dir_offset u64,
//   0x20 file_size u64; the chunks from 0x28 in directory order; the
//   directory last, 64 bytes per entry: 0x00 type, 0x04 flags (compression),
//   0x08 offset u64, 0x10 size_stored u64, 0x18 size_raw u64, 0x20 SHA-256.
//
// Magic, major and minor come from the container type (struct RldFormat in
// rldtrack.inc; the track files here use s_rldTrackFormat), the way the
// packer's Rld_WriteEnvelope takes them.
// ---------------------------------------------------------------------------

struct NativeTestLayout
{
	int present[NTC_PART_COUNT];
	int dirIndex[NTC_PART_COUNT];
	u64 offset[NTC_PART_COUNT];
	u64 size[NTC_PART_COUNT];
	int chunkCount;
	u64 dirOffset;
	u64 fileSize;
};

internal void NativeTestCont_Assemble(const struct RldFormat *format, const struct NativeTestParts *p, struct NativeTestBlob *out,
                                      struct NativeTestLayout *layout)
{
	u64 offset = RLD_HEADER_SIZE;
	int part;
	int index = 0;

	memset(layout, 0, sizeof(*layout));

	for (part = 0; part < NTC_PART_COUNT; part++)
	{
		const struct NativeTestBlob *b = NativeTestParts_Get(p, part);

		layout->dirIndex[part] = -1;
		if (b == NULL)
		{
			continue;
		}
		layout->present[part] = 1;
		layout->dirIndex[part] = index++;
		layout->offset[part] = offset;
		layout->size[part] = b->size;
		offset += b->size;
	}

	layout->chunkCount = index;
	layout->dirOffset = offset;
	layout->fileSize = offset + (u64)index * RLD_DIR_ENTRY_SIZE;

	NativeTestBlob_Alloc(out, (size_t)layout->fileSize);
	memcpy(out->data, format->magic, 8);
	NativeTestBlob_Put16(out, 0x08, format->major);
	NativeTestBlob_Put16(out, 0x0a, format->minorWritten);
	NativeTestBlob_Put32(out, 0x0c, 0u);
	NativeTestBlob_Put32(out, 0x10, (u32)layout->chunkCount);
	NativeTestBlob_Put32(out, 0x14, 0u);
	NativeTestBlob_Put64(out, 0x18, layout->dirOffset);
	NativeTestBlob_Put64(out, 0x20, layout->fileSize);

	for (part = 0; part < NTC_PART_COUNT; part++)
	{
		const struct NativeTestBlob *b = NativeTestParts_Get(p, part);
		size_t entry;

		if (b == NULL)
		{
			continue;
		}

		entry = (size_t)layout->dirOffset + (size_t)layout->dirIndex[part] * RLD_DIR_ENTRY_SIZE;
		if (b->size != 0u)
		{
			memcpy(&out->data[(size_t)layout->offset[part]], b->data, b->size);
		}
		memcpy(&out->data[entry], s_testPartNames[part], 4);
		NativeTestBlob_Put32(out, entry + 0x04u, RLD_COMPRESSION_NONE);
		NativeTestBlob_Put64(out, entry + 0x08u, layout->offset[part]);
		NativeTestBlob_Put64(out, entry + 0x10u, b->size);
		NativeTestBlob_Put64(out, entry + 0x18u, b->size);
		Sha256(b->data, b->size, &out->data[entry + RLD_DIR_HASH_OFFSET]);
	}
}

internal size_t NativeTestCont_DirField(const struct NativeTestLayout *layout, int part, u32 field)
{
	return (size_t)layout->dirOffset + (size_t)layout->dirIndex[part] * RLD_DIR_ENTRY_SIZE + field;
}

// ---------------------------------------------------------------------------
// THE REFERENCE MODEL
//
// Existing checks run through the real reader. The content rules are written
// out here from what the game must guarantee, in 64-bit arithmetic, so that
// the model itself cannot wrap. NULL = accepted.
// ---------------------------------------------------------------------------

global_variable char s_testModelWhy[256];

internal const char *NativeTestModel_Why(const char *format, u32 a, u32 b)
{
	snprintf(s_testModelWhy, sizeof(s_testModelWhy), format, a, b);
	return s_testModelWhy;
}

internal u32 NativeTestModel_Get32(const u8 *bytes, u64 at)
{
	return Rld_ReadLE32(&bytes[(size_t)at]);
}

internal int NativeTestModel_Get16s(const u8 *bytes, u64 at)
{
	const u32 raw = Rld_ReadLE16(&bytes[(size_t)at]);

	return (raw >= 0x8000u) ? ((int)raw - 0x10000) : (int)raw;
}

// c4 and c5: the LEV and its pointer map, by the rule LOAD_DramFileCallback
// and LOAD_RunPtrMap apply - checked before, not refused at run time.
internal const char *NativeTestModel_Lev(const u8 *lev, size_t levSize, u32 *bodyOut, u32 **sitesOut, u32 *siteCountOut)
{
	u32 body;
	u32 numBytes;
	u32 count;
	u32 *sites;
	u32 i;
	u32 j;

	*sitesOut = NULL;
	*siteCountOut = 0;

	if ((u64)levSize < ((u64)NTC_LEV_BODY + NTC_LEVEL_SIZE))
	{
		return NativeTestModel_Why("c4 LEVD is %u bytes, shorter than struct Level (%u) plus the first word", (u32)levSize, NTC_LEVEL_SIZE);
	}

	body = NativeTestModel_Get32(lev, 0);
	if ((body & 0x80000000u) != 0u)
	{
		return NativeTestModel_Why("c5 ptrMapOffset 0x%x is negative%.0u", body, 0u);
	}
	if (body < NTC_LEVEL_SIZE)
	{
		return NativeTestModel_Why("c4 the LEV body is %u bytes, shorter than struct Level (%u)", body, NTC_LEVEL_SIZE);
	}
	if (((u64)NTC_LEV_BODY + body + 4u) > levSize)
	{
		return NativeTestModel_Why("c5 the pointer map at %u lies outside the LEV (%u bytes)", body, (u32)levSize);
	}

	numBytes = NativeTestModel_Get32(lev, (u64)NTC_LEV_BODY + body);
	if (((numBytes & 3u) != 0u) || (((u64)NTC_LEV_BODY + body + 4u + numBytes) > levSize))
	{
		return NativeTestModel_Why("c5 the pointer map claims %u bytes, the LEV is %u", numBytes, (u32)levSize);
	}

	count = numBytes / 4u;
	sites = (u32 *)calloc(count ? count : 1u, sizeof(u32));
	if (sites == NULL)
	{
		NativeTestCont_Fatal("out of memory");
	}

	for (i = 0; i < count; i++)
	{
		const u32 raw = NativeTestModel_Get32(lev, (u64)NTC_LEV_BODY + body + 4u + 4u * (u64)i);
		const s64 site = (s64)(int)raw & ~(s64)3;
		int value;

		if ((site < 0) || (site > ((s64)body - 4)))
		{
			free(sites);
			return NativeTestModel_Why("c5 pointer map entry %u patches at 0x%x, outside the body", i, raw);
		}

		value = (int)NativeTestModel_Get32(lev, (u64)NTC_LEV_BODY + (u64)site);
		if ((value < 0) || ((u32)value > body))
		{
			free(sites);
			return NativeTestModel_Why("c5 pointer map entry %u holds 0x%x, outside the body", i, (u32)value);
		}

		for (j = 0; j < i; j++)
		{
			if (sites[j] == (u32)site)
			{
				free(sites);
				return NativeTestModel_Why("c5 the pointer map repeats offset 0x%x (entry %u)", (u32)site, i);
			}
		}
		sites[i] = (u32)site;
	}

	*bodyOut = body;
	*sitesOut = sites;
	*siteCountOut = count;
	return NULL;
}

internal int NativeTestModel_Mapped(const u32 *sites, u32 count, u32 site)
{
	u32 i;

	for (i = 0; i < count; i++)
	{
		if (sites[i] == site)
		{
			return 1;
		}
	}
	return 0;
}

// c1, c2, c6, c7 on a LEV whose map passed NativeTestModel_Lev. Everything
// the engine reads must lie inside the body.
internal const char *NativeTestModel_LevContent(const u8 *lev, u32 body, const u32 *sites, u32 siteCount)
{
	const u64 end = (u64)body;
	u32 at;

	// c2: mesh_info and the sky, the two offsets Rld_MemNeed adds to.
	at = NativeTestModel_Get32(lev, NTC_LEV_BODY + NTC_LVL_MESH);
	if ((at != 0u) && (((u64)at + 0x20u) > end))
	{
		return NativeTestModel_Why("c2 mesh_info at 0x%x lies outside the body (%u)", at, body);
	}
	at = NativeTestModel_Get32(lev, NTC_LEV_BODY + NTC_LVL_SKY);
	if ((at != 0u) && (((u64)at + 0x38u) > end))
	{
		return NativeTestModel_Why("c2 the skybox at 0x%x lies outside the body (%u)", at, body);
	}

	// c1: the icon table Rld_LevMap walks.
	at = NativeTestModel_Get32(lev, NTC_LEV_BODY + NTC_LVL_TEXLOOKUP);
	if (at != 0u)
	{
		u32 numIcon;
		u32 firstIcon;

		if (((u64)at + 8u) > end)
		{
			return NativeTestModel_Why("c1 levTexLookup 0x%x lies outside the body (%u)", at, body);
		}
		numIcon = NativeTestModel_Get32(lev, (u64)NTC_LEV_BODY + at);
		firstIcon = NativeTestModel_Get32(lev, (u64)NTC_LEV_BODY + at + 4u);
		if ((numIcon > 4096u) || (((u64)firstIcon + (u64)numIcon * 0x20u) > end))
		{
			return NativeTestModel_Why("c1 the icon table (%u icons at 0x%x) lies outside the body", numIcon, firstIcon);
		}
	}

	// c6: the model table LibraryOfModels_Store walks until NULL.
	{
		const u32 numModels = NativeTestModel_Get32(lev, NTC_LEV_BODY + NTC_LVL_NUM_MODELS);
		const u32 array = NativeTestModel_Get32(lev, NTC_LEV_BODY + NTC_LVL_MODELS);
		u32 i;

		for (i = 0; (array != 0u) && (i < numModels); i++)
		{
			u32 model;
			int id;

			if (((u64)array + 4u * (u64)i + 4u) > end)
			{
				return NativeTestModel_Why("c6 the model table runs past the body (model %u of %u)", i, numModels);
			}
			model = NativeTestModel_Get32(lev, (u64)NTC_LEV_BODY + array + 4u * (u64)i);
			if (model == 0u)
			{
				break;
			}
			if (((u64)model + 0x18u) > end)
			{
				return NativeTestModel_Why("c6 model %u lies outside the body (at 0x%x)", i, model);
			}
			id = NativeTestModel_Get16s(lev, (u64)NTC_LEV_BODY + model + NTC_MODEL_ID_OFFSET);
			if ((id != -1) && ((id < 0) || (id >= (int)NTC_MODEL_ID_COUNT)))
			{
				return NativeTestModel_Why("c6 model %u has id 0x%x, outside -1 and 0..0xe2", i, (u32)id & 0xffffu);
			}
		}
	}

	// c7: the spawn table CAM.c reads without a check.
	{
		const u32 table = NativeTestModel_Get32(lev, NTC_LEV_BODY + NTC_LVL_SPAWN1);
		int spawnCount;
		int k;

		if (table == 0u)
		{
			return "c7 ptrSpawnType1 is NULL - CAM.c reads it in every race";
		}
		if (!NativeTestModel_Mapped(sites, siteCount, NTC_LVL_SPAWN1))
		{
			return NativeTestModel_Why("c7 ptrSpawnType1 (0x%x) is not in the pointer map - it would stay a raw offset%.0u", table, 0u);
		}
		if (((u64)table + 4u) > end)
		{
			return NativeTestModel_Why("c7 the spawn table at 0x%x lies outside the body (%u)", table, body);
		}
		spawnCount = (int)NativeTestModel_Get32(lev, (u64)NTC_LEV_BODY + table);
		if ((spawnCount < 0) || (spawnCount > 16) || (((u64)table + 4u + 4u * (u64)spawnCount) > end))
		{
			return NativeTestModel_Why("c7 the spawn table count 0x%x is out of range (table at 0x%x)", (u32)spawnCount, table);
		}
		for (k = 2; k <= 3; k++)
		{
			if ((spawnCount > k) && (NativeTestModel_Get32(lev, (u64)NTC_LEV_BODY + table + 4u + 4u * (u64)k) == 0u))
			{
				return NativeTestModel_Why("c7 spawn table count %u, slot %u is NULL", (u32)spawnCount, (u32)k);
			}
		}
	}

	return NULL;
}

// c12..c15 on a LEV that passed NativeTestModel_LevContent: the numbers the
// race uses as indices and divisors. The spawn table is known to be inside.
internal const char *NativeTestModel_LevRace(const u8 *lev, u32 body, const u32 *sites, u32 siteCount)
{
	const u64 end = (u64)body;
	const int nodes = (int)NativeTestModel_Get32(lev, NTC_LEV_BODY + NTC_LVL_NUM_RESTART);
	const u32 spawn = NativeTestModel_Get32(lev, NTC_LEV_BODY + NTC_LVL_SPAWN1);
	const u32 spawnCount = NativeTestModel_Get32(lev, (u64)NTC_LEV_BODY + spawn);
	const u32 mesh = NativeTestModel_Get32(lev, NTC_LEV_BODY + NTC_LVL_MESH);
	int i;

	// c14: the restart points, c15: the track length.
	if ((nodes < 0) || (nodes > 255))
	{
		return NativeTestModel_Why("c14 cnt_restart_points 0x%x is outside 0..255%.0u", (u32)nodes, 0u);
	}
	if (nodes > 0)
	{
		const u32 table = NativeTestModel_Get32(lev, NTC_LEV_BODY + NTC_LVL_RESTART);

		if (!NativeTestModel_Mapped(sites, siteCount, NTC_LVL_RESTART) || (((u64)table + (u64)nodes * NTC_NODE_BYTES) > end))
		{
			return NativeTestModel_Why("c14 %u restart points at 0x%x do not lie inside the body", (u32)nodes, table);
		}
		for (i = 0; i < nodes; i++)
		{
			const u64 node = (u64)NTC_LEV_BODY + table + (u64)i * NTC_NODE_BYTES;
			int k;

			for (k = 0; k < 4; k++)
			{
				const u32 next = lev[(size_t)node + NTC_NODE_FORWARD + (u32)k];

				if (((k == 0) || (next != NTC_NODE_NONE)) && (next >= (u32)nodes))
				{
					return NativeTestModel_Why("c14 restart point %u links to %u, past the table", (u32)i, next);
				}
			}
		}
		if (Rld_ReadLE16(&lev[(size_t)NTC_LEV_BODY + table + NTC_NODE_DISTANCE]) == 0u)
		{
			return "c15 restart point 0 has distToFinish 0";
		}
	}

	// c14: the checkpoint of every quadblock.
	if ((mesh != 0u) && (((u64)mesh + 0x20u) <= end))
	{
		const int quads = (int)NativeTestModel_Get32(lev, (u64)NTC_LEV_BODY + mesh);
		const u32 array = NativeTestModel_Get32(lev, (u64)NTC_LEV_BODY + mesh + 0x0cu);

		if ((quads > 0) && (!NativeTestModel_Mapped(sites, siteCount, mesh + 0x0cu) || (((u64)array + (u64)quads * RLD_QUADBLOCK_BYTES) > end)))
		{
			return NativeTestModel_Why("c14 %u quadblocks at 0x%x do not lie inside the body", (u32)quads, array);
		}
		for (i = 0; i < quads; i++)
		{
			const u32 checkpoint = lev[(size_t)NTC_LEV_BODY + array + (size_t)i * RLD_QUADBLOCK_BYTES + NTC_QUAD_CHECKPOINT];

			if ((checkpoint != NTC_NODE_NONE) && (checkpoint >= (u32)nodes))
			{
				return NativeTestModel_Why("c14 quadblock %u names restart point %u, past the table", (u32)i, checkpoint);
			}
		}
	}

	// c13: the map table.
	if (spawnCount > 0u)
	{
		const u32 map = NativeTestModel_Get32(lev, (u64)NTC_LEV_BODY + spawn + 4u);

		if (map != 0u)
		{
			if (((u64)map + NTC_UIMAP_BYTES) > end)
			{
				return NativeTestModel_Why("c13 the map table at 0x%x runs past the body (%u)", map, body);
			}
			if ((NativeTestModel_Get16s(lev, (u64)NTC_LEV_BODY + map + 0u) == NativeTestModel_Get16s(lev, (u64)NTC_LEV_BODY + map + 4u)) ||
			    (NativeTestModel_Get16s(lev, (u64)NTC_LEV_BODY + map + 2u) == NativeTestModel_Get16s(lev, (u64)NTC_LEV_BODY + map + 6u)))
			{
				return NativeTestModel_Why("c13 the map table at 0x%x has a width or height of 0%.0u", map, 0u);
			}
		}
	}

	// c12: the end-of-race cameras.
	if (spawnCount > 2u)
	{
		const int sizes = (int)(sizeof(data.EndOfRace_Camera_Size) / sizeof(data.EndOfRace_Camera_Size[0]));
		const u32 cameras = NativeTestModel_Get32(lev, (u64)NTC_LEV_BODY + spawn + 12u);
		u64 at = (u64)cameras + 2u;
		int count;

		if (at > end)
		{
			return NativeTestModel_Why("c12 the camera table at 0x%x runs past the body (%u)", cameras, body);
		}
		count = NativeTestModel_Get16s(lev, (u64)NTC_LEV_BODY + cameras);
		if (count < 0)
		{
			return NativeTestModel_Why("c12 the camera count 0x%x is negative%.0u", (u32)count & 0xffffu, 0u);
		}
		for (i = 0; i < count; i++)
		{
			int respawn;
			int mode;

			if ((at + 4u) > end)
			{
				return NativeTestModel_Why("c12 camera %u of %u runs past the body", (u32)i, (u32)count);
			}
			respawn = NativeTestModel_Get16s(lev, (u64)NTC_LEV_BODY + at);
			mode = NativeTestModel_Get16s(lev, (u64)NTC_LEV_BODY + at + 2u);
			mode = (mode < 0) ? -mode : mode;
			if ((mode >= sizes) || (data.EndOfRace_Camera_Size[mode] < 0))
			{
				return NativeTestModel_Why("c12 camera %u has mode 0x%x, not in EndOfRace_Camera_Size", (u32)i, (u32)mode);
			}
			if ((at + 4u + (u64)data.EndOfRace_Camera_Size[mode]) > end)
			{
				return NativeTestModel_Why("c12 camera %u of %u runs past the body", (u32)i, (u32)count);
			}
			if ((respawn < 0) || (respawn >= nodes))
			{
				return NativeTestModel_Why("c12 camera %u names restart point 0x%x, past the table", (u32)i, (u32)respawn & 0xffffu);
			}
			if ((mode == 9) || (mode == 13))
			{
				const int path = NativeTestModel_Get16s(lev, (u64)NTC_LEV_BODY + at + 4u);

				if ((path < 0) || (path >= nodes))
				{
					return NativeTestModel_Why("c12 camera %u follows the path from restart point 0x%x, past the table", (u32)i, (u32)path & 0xffffu);
				}
			}
			at += 4u + (u64)data.EndOfRace_Camera_Size[mode];
		}
	}

	return NULL;
}

// c3: the VRM as LOAD_VramFileCallback walks it.
internal const char *NativeTestModel_Vrm(const u8 *vrm, size_t vrmSize)
{
	u64 at;
	u32 blocks = 0;

	if (vrmSize < 4u)
	{
		return NativeTestModel_Why("c3 VRMD is %u bytes%.0u", (u32)vrmSize, 0u);
	}

	if (NativeTestModel_Get32(vrm, 0) != NTC_VRM_PACKED)
	{
		u32 w;
		u32 h;

		if (vrmSize < 0x14u)
		{
			return "c3 the single TIM header runs past VRMD";
		}
		w = Rld_ReadLE16(&vrm[0x10]);
		h = Rld_ReadLE16(&vrm[0x12]);
		if ((0x14u + (u64)w * h * 2u) > vrmSize)
		{
			return NativeTestModel_Why("c3 the single TIM (%u x %u) is larger than VRMD", w, h);
		}
		return NULL;
	}

	at = 4u;
	for (;;)
	{
		u32 blockSize;
		u32 w;
		u32 h;
		u64 head;

		if ((at + 4u) > vrmSize)
		{
			return NativeTestModel_Why("c3 the chain is not terminated inside VRMD (after %u blocks)%.0u", blocks, 0u);
		}
		blockSize = NativeTestModel_Get32(vrm, at);
		if (blockSize == 0u)
		{
			return NULL;
		}
		head = at + 4u;
		if ((head + 0x14u) > vrmSize)
		{
			return NativeTestModel_Why("c3 block %u: the TIM header runs past VRMD%.0u", blocks, 0u);
		}
		w = Rld_ReadLE16(&vrm[(size_t)head + 0x10u]);
		h = Rld_ReadLE16(&vrm[(size_t)head + 0x12u]);
		if ((head + 0x14u + (u64)w * h * 2u) > vrmSize)
		{
			return NativeTestModel_Why("c3 block %u: the rectangle needs more pixels than VRMD holds (w %u)", blocks, w);
		}
		if ((head + (blockSize & ~3u)) > vrmSize)
		{
			return NativeTestModel_Why("c3 block %u: size 0x%x runs past VRMD", blocks, blockSize);
		}
		at = head + (blockSize & ~3u);
		blocks++;
	}
}

// s1: every bank header in a parsed SNDB.
internal const char *NativeTestModel_Sndb(const struct RldSndb *sndb)
{
	u32 i;

	for (i = 0; i < sndb->entryCount; i++)
	{
		struct RldSndbEntry entry;
		const u8 *bank;
		int numSamples;
		u64 sum = 0;
		int s;

		Rld_SndbEntry(sndb, i, &entry);
		if (entry.kind != RLD_SNDB_KIND_BANK)
		{
			continue;
		}

		bank = &sndb->payload[entry.offset];
		numSamples = NativeTestModel_Get16s(bank, 0);
		if ((numSamples < 0) || (numSamples > 1023))
		{
			return NativeTestModel_Why("s1 bank %u: numSamples 0x%x outside 0..1023", entry.index, (u32)numSamples & 0xffffu);
		}

		for (s = 0; s < numSamples; s++)
		{
			const int row = NativeTestModel_Get16s(bank, 2u + 2u * (u64)s);
			u32 f;

			if ((row < 0) || (row >= (int)RLD_HOWL_SPUADDR_COUNT))
			{
				return NativeTestModel_Why("s1 bank %u: spuIndex 0x%x outside the SPU rows", entry.index, (u32)row & 0xffffu);
			}

			for (f = 0; f < sndb->spuFixupCount; f++)
			{
				struct RldSndbSpuFixup fixup;

				Rld_SndbSpuFixup(sndb, f, &fixup);
				if (fixup.index == (u32)row)
				{
					sum += fixup.spuSize;
				}
			}
		}

		if ((sum * 8u) > ((u64)entry.size - RLD_HOWL_SECTOR))
		{
			return NativeTestModel_Why("s1 bank %u: the samples need %u bytes, the entry holds less", entry.index, (u32)(sum * 8u));
		}
	}

	return NULL;
}

// The whole path: open, META, every chunk, then the content rules.
internal const char *NativeTestModel_File(const char *path)
{
	struct RldReader reader;
	struct RldMeta meta;
	const char *error;
	u8 *chunk[NTC_PART_COUNT];
	size_t chunkSize[NTC_PART_COUNT];
	int part;
	u32 body = 0;
	u32 *sites = NULL;
	u32 siteCount = 0;

	memset(chunk, 0, sizeof(chunk));
	memset(chunkSize, 0, sizeof(chunkSize));
	memset(&meta, 0, sizeof(meta));

	error = Rld_Open(&reader, path);
	if (error != NULL)
	{
		return error;
	}

	// META, LEVD and VRMD must read; a SNDB or PARM that does not read (hash)
	// costs only the sound or the values in NativeTrack_Load, not the track.
	for (part = 0; (part < NTC_PART_COUNT) && (error == NULL); part++)
	{
		int index = -1;

		if (Rld_FindEntry(&reader, s_testPartNames[part], &index) != NULL)
		{
			const char *partError = NULL;

			chunk[part] = Rld_ReadChunk(&reader, index, &chunkSize[part], &partError);
			if ((partError != NULL) && (part <= NTC_PART_VRMD))
			{
				error = partError;
			}
		}
	}
	Rld_Close(&reader);

	if (error == NULL)
	{
		error = Rld_ParseMeta(&meta, chunk[NTC_PART_META], chunkSize[NTC_PART_META]);
	}

	if (error == NULL)
	{
		error = NativeTestModel_Lev(chunk[NTC_PART_LEVD], chunkSize[NTC_PART_LEVD], &body, &sites, &siteCount);
	}

	if (error == NULL)
	{
		error = NativeTestModel_LevContent(chunk[NTC_PART_LEVD], body, sites, siteCount);
	}

	if (error == NULL)
	{
		error = NativeTestModel_LevRace(chunk[NTC_PART_LEVD], body, sites, siteCount);
	}

	// The memory need - safe to compute now that both offsets are inside.
	if (error == NULL)
	{
		struct RldMemNeed need;

		Rld_MemNeed(&need, chunk[NTC_PART_LEVD], chunkSize[NTC_PART_LEVD]);
		if ((need.total > meta.memTotal) || (need.primBytes > meta.primBytes))
		{
			error = NativeTestModel_Why("mem META understates the memory need (the LEV needs %u, META says %u)", need.total, meta.memTotal);
		}
	}

	if (error == NULL)
	{
		error = NativeTestModel_Vrm(chunk[NTC_PART_VRMD], chunkSize[NTC_PART_VRMD]);
	}

	// The map for the track wheel. "No map" is not a refusal; the icon table
	// was range-checked above, so Rld_LevMap cannot wrap here.
	if (error == NULL)
	{
		struct RldMapHalf half[2];

		if (Rld_LevMap(chunk[NTC_PART_LEVD], chunkSize[NTC_PART_LEVD], chunk[NTC_PART_VRMD], chunkSize[NTC_PART_VRMD], half) == NULL)
		{
			Rld_FreeMap(half);
		}
	}

	if ((error == NULL) && (chunk[NTC_PART_SNDB] != NULL))
	{
		struct RldSndb sndb;

		// A malformed SNDB costs only the sound, as in NativeTrack_Load; the
		// bank headers of a well-formed one are case s1.
		if (Rld_ParseSndb(&sndb, chunk[NTC_PART_SNDB], chunkSize[NTC_PART_SNDB]) == NULL)
		{
			error = NativeTestModel_Sndb(&sndb);
		}
	}

	free(sites);
	for (part = 0; part < NTC_PART_COUNT; part++)
	{
		free(chunk[part]);
	}

	return error;
}

// For the good files only: the checks of today's reader the model does not
// repeat, each of which must pass - otherwise "good" is not good.
internal const char *NativeTestModel_GoodExtras(const struct NativeTestParts *p)
{
	struct RldMapHalf half[2];
	struct RldParm parm;
	struct RldSndb sndb;
	const char *why;

	memset(&parm, 0, sizeof(parm));

	why = Rld_LevMap(p->lev.data, p->lev.size, p->vrm.data, p->vrm.size, half);
	if (why != NULL)
	{
		return why;
	}
	why = Rld_MapWhyNotMenu(half);
	Rld_FreeMap(half);
	if (why != NULL)
	{
		return why;
	}
	if (p->hasSndb)
	{
		why = Rld_ParseSndb(&sndb, p->sndb.data, p->sndb.size);
		if (why != NULL)
		{
			return why;
		}
	}
	if (p->hasParm)
	{
		why = Rld_ParseParm(&parm, p->parm.data, p->parm.size);
		if (why != NULL)
		{
			return why;
		}
		if ((parm.reverbState != RLD_PARM_SET) || (parm.botsState != RLD_PARM_SET) || (parm.unknownCount != 0u))
		{
			return "PARM values are not all valid";
		}
	}
	return NULL;
}

// ---------------------------------------------------------------------------
// Writing and reporting
// ---------------------------------------------------------------------------

global_variable const char *s_testContDir;
global_variable struct NativeTestParts s_testGood;
global_variable struct NativeTestBlob s_testGoodFile;
global_variable struct NativeTestLayout s_testGoodLayout;
global_variable int s_testWritten;
global_variable int s_testMismatches;
global_variable int s_testWriteFailed;

internal void NativeTestCont_PartDiff(char *out, size_t outSize, const struct NativeTestBlob *a, const struct NativeTestBlob *b)
{
	size_t n;
	size_t i;
	u32 differ = 0;

	if ((a == NULL) || (b == NULL))
	{
		snprintf(out, outSize, "%s", (a == b) ? "-" : ((a == NULL) ? "added" : "gone"));
		return;
	}

	n = (a->size < b->size) ? a->size : b->size;
	for (i = 0; i < n; i++)
	{
		differ += (a->data[i] != b->data[i]) ? 1u : 0u;
	}

	if ((differ == 0u) && (a->size == b->size))
	{
		snprintf(out, outSize, "=");
	}
	else if (a->size == b->size)
	{
		snprintf(out, outSize, "%uB", differ);
	}
	else
	{
		snprintf(out, outSize, "%uB%+d", differ, (int)b->size - (int)a->size);
	}
}

// Which region of the GOOD file an offset lies in.
internal const char *NativeTestCont_Region(u64 at)
{
	int part;

	if (at < RLD_HEADER_SIZE)
	{
		return "header";
	}
	for (part = 0; part < NTC_PART_COUNT; part++)
	{
		if (s_testGoodLayout.present[part] && (at >= s_testGoodLayout.offset[part]) &&
		    (at < (s_testGoodLayout.offset[part] + s_testGoodLayout.size[part])))
		{
			return s_testPartNames[part];
		}
	}
	return (at < s_testGoodLayout.fileSize) ? "directory" : "past the end";
}

internal void NativeTestCont_Write(const char *name, const struct NativeTestBlob *file)
{
	char path[1024];
	FILE *out;

	snprintf(path, sizeof(path), "%s/%s.rldtrack", s_testContDir, name);
	out = fopen(path, "wb");
	if ((out == NULL) || ((file->size != 0u) && (fwrite(file->data, 1, file->size, out) != file->size)) || (fclose(out) != 0))
	{
		fprintf(stderr, "[selftest] test container generator: cannot write %s\n", path);
		s_testWriteFailed = 1;
		return;
	}
	s_testWritten++;
}

// One line per file: name, what differs, the model's verdict.
internal void NativeTestCont_Report(const char *name, const char *diff, const struct NativeTestParts *parts)
{
	char path[1024];
	const char *verdict;
	const int good = (strncmp(name, "good-", 5) == 0);
	int ok;

	snprintf(path, sizeof(path), "%s/%s.rldtrack", s_testContDir, name);
	verdict = NativeTestModel_File(path);
	if ((verdict == NULL) && good && (parts != NULL))
	{
		verdict = NativeTestModel_GoodExtras(parts);
	}

	ok = good ? (verdict == NULL) : (verdict != NULL);
	if (!ok)
	{
		s_testMismatches++;
	}

	printf("  %-38s %-50s %s%s%s\n", name, diff, (verdict == NULL) ? "accepted" : "refused: ", (verdict == NULL) ? "" : verdict,
	       ok ? "" : "   <-- MODEL MISMATCH");
}

// A variant built from parts: assembled with valid hashes, written, reported
// with the per-chunk difference to the good parts. Frees the parts.
internal void NativeTestCont_EmitParts(const char *name, struct NativeTestParts *p, int freeParts)
{
	struct NativeTestBlob file;
	struct NativeTestLayout layout;
	char diff[160];
	char cell[NTC_PART_COUNT][24];
	int part;

	NativeTestCont_Assemble(&s_rldTrackFormat, p, &file, &layout);
	NativeTestCont_Write(name, &file);

	for (part = 0; part < NTC_PART_COUNT; part++)
	{
		NativeTestCont_PartDiff(cell[part], sizeof(cell[part]), NativeTestParts_Get(&s_testGood, part), NativeTestParts_Get(p, part));
	}
	snprintf(diff, sizeof(diff), "META %s LEVD %s VRMD %s SNDB %s PARM %s", cell[0], cell[1], cell[2], cell[3], cell[4]);

	NativeTestCont_Report(name, diff, p);
	NativeTestBlob_Free(&file);

	if (freeParts)
	{
		NativeTestParts_Free(p);
	}
}

// A variant of the good FILE: only envelope bytes change, the chunks stay.
// Frees the file.
internal void NativeTestCont_EmitFile(const char *name, struct NativeTestBlob *file)
{
	char diff[160];
	const size_t n = (file->size < s_testGoodFile.size) ? file->size : s_testGoodFile.size;
	size_t first = (size_t)-1;
	size_t i;
	u32 differ = 0;

	NativeTestCont_Write(name, file);

	for (i = 0; i < n; i++)
	{
		if (file->data[i] != s_testGoodFile.data[i])
		{
			if (first == (size_t)-1)
			{
				first = i;
			}
			differ++;
		}
	}

	if (file->size != s_testGoodFile.size)
	{
		snprintf(diff, sizeof(diff), "cut at %u (%s), %u bytes changed before", (u32)file->size, NativeTestCont_Region(file->size), differ);
	}
	else
	{
		snprintf(diff, sizeof(diff), "%u bytes changed, first at 0x%x (%s)", differ, (u32)first, NativeTestCont_Region(first));
	}

	NativeTestCont_Report(name, diff, NULL);
	NativeTestBlob_Free(file);
}

// ---------------------------------------------------------------------------
// The variants
// ---------------------------------------------------------------------------

internal void NativeTestCont_Goods(void)
{
	struct NativeTestParts p;

	// The good container: all five chunk types.
	NativeTestCont_EmitParts("good-synthetic", &s_testGood, 0);

	// The minimum: META, LEVD, VRMD - SNDB and PARM are optional.
	NativeTestParts_Copy(&p, &s_testGood);
	NativeTestBlob_Free(&p.sndb);
	NativeTestBlob_Free(&p.parm);
	p.hasSndb = 0;
	p.hasParm = 0;
	NativeTestCont_EmitParts("good-minimal", &p, 1);

	// The VRM as a single TIM instead of a chain.
	NativeTestParts_Copy(&p, &s_testGood);
	NativeTestBlob_Free(&p.vrm);
	NativeTestVrm_BuildSingle(&p.vrm);
	NativeTestCont_EmitParts("good-vrm-single", &p, 1);

	// A race track without restart points, like the arenas some real
	// containers declare Race for: no table, every quadblock checkpoint 0xff,
	// no end-of-race camera. Accepted - the race handles it (VehLap,
	// VehStuckProc, the warpball in VehPickupItem).
	NativeTestParts_Copy(&p, &s_testGood);
	NativeTestLev_Set32(&p.lev, NTC_LVL_NUM_RESTART, 0u);
	NativeTestLev_Set32(&p.lev, NTC_LVL_RESTART, 0u);
	NativeTestLev_MapRemove(&p.lev, NTC_LVL_RESTART);
	p.lev.data[NTC_LEV_BODY + NTC_QUAD + NTC_QUAD_CHECKPOINT] = (u8)NTC_NODE_NONE;
	NativeTestLev_Set16(&p.lev, NTC_EORCAM, 0u);
	NativeTestCont_EmitParts("good-race-no-restart-points", &p, 1);
}

// s1 - Bank_AssignSpuAddrs uses numSamples and spuIndexArr[] of the bank
// header unchecked: an out-of-bounds write into howl_spuAddrs.
internal void NativeTestCont_S1(void)
{
	static const struct
	{
		const char *name;
		u32 at;
		u32 value;
	} edits[] = {
	    {"bad-s1-numsamples-1024", NTC_SNDB_BANK_AT, 1024u},
	    {"bad-s1-numsamples-7fff", NTC_SNDB_BANK_AT, 0x7fffu},
	    {"bad-s1-numsamples-negative", NTC_SNDB_BANK_AT, 0xffffu},
	    {"bad-s1-spuindex-528", NTC_SNDB_BANK_AT + 2u, RLD_HOWL_SPUADDR_COUNT},
	    {"bad-s1-spuindex-7fff", NTC_SNDB_BANK_AT + 2u, 0x7fffu},
	    {"bad-s1-spuindex-negative", NTC_SNDB_BANK_AT + 2u, 0xffffu},
	    // spuSize of the row: 0x1000 * 8 = 32 KiB of samples in a 2 KiB entry
	    {"bad-s1-spusize-past-entry", NTC_SNDB_FIXUP_AT + 4u, 0x1000u},
	};
	struct NativeTestParts p;
	size_t i;

	for (i = 0; i < sizeof(edits) / sizeof(edits[0]); i++)
	{
		NativeTestParts_Copy(&p, &s_testGood);
		NativeTestBlob_Put16(&p.sndb, edits[i].at, edits[i].value);
		NativeTestCont_EmitParts(edits[i].name, &p, 1);
	}
}

// c1 - Rld_LevMap: body + lookup + 8 and body + firstIcon + numIcon * 32 wrap
// in 32 bits and read before the LEV buffer.
// c2 - Rld_MemNeed / NativeTrack_SkyPrimBytes: body + at + 8 / + 56 wrap.
//
// Each trigger comes twice: as a real attack would have it, with the field
// still in the pointer map (there the map's value range refuses it too), and
// "-unmapped", with the field taken out of the map, so that only the offset
// arithmetic stands between the value and the read.
internal void NativeTestCont_C1C2(void)
{
	static const struct
	{
		const char *name;
		u32 site;
		u32 value;
	} edits[] = {
	    {"bad-c1-texlookup-wrap", NTC_LVL_TEXLOOKUP, 0xfffffff8u},
	    {"bad-c1-firsticon-wrap", NTC_LOOKUP + 4u, 0xffffff00u},
	    {"bad-c2-meshinfo-wrap", NTC_LVL_MESH, 0xfffffff8u},
	    {"bad-c2-sky-wrap", NTC_LVL_SKY, 0xffffffd0u},
	};
	struct NativeTestParts p;
	char name[96];
	size_t i;

	for (i = 0; i < sizeof(edits) / sizeof(edits[0]); i++)
	{
		NativeTestParts_Copy(&p, &s_testGood);
		NativeTestLev_Set32(&p.lev, edits[i].site, edits[i].value);
		NativeTestCont_EmitParts(edits[i].name, &p, 1);

		NativeTestParts_Copy(&p, &s_testGood);
		NativeTestLev_Set32(&p.lev, edits[i].site, edits[i].value);
		NativeTestLev_MapRemove(&p.lev, edits[i].site);
		snprintf(name, sizeof(name), "%s-unmapped", edits[i].name);
		NativeTestCont_EmitParts(name, &p, 1);
	}

	// A huge icon count - no wrap, just far past the LEV.
	NativeTestParts_Copy(&p, &s_testGood);
	NativeTestLev_Set32(&p.lev, NTC_LOOKUP, 0xffffffffu);
	NativeTestCont_EmitParts("bad-c1-numicon-huge", &p, 1);
}

// c3 - LOAD_VramFileCallback trusts VRMD.
internal void NativeTestCont_C3(void)
{
	struct NativeTestParts p;

	NativeTestParts_Copy(&p, &s_testGood);
	p.vrm.size = 0u;
	NativeTestCont_EmitParts("bad-c3-vrm-empty", &p, 1);

	NativeTestParts_Copy(&p, &s_testGood);
	p.vrm.size = 3u;
	NativeTestCont_EmitParts("bad-c3-vrm-3-bytes", &p, 1);

	// Only the 0x20 word, no block size.
	NativeTestParts_Copy(&p, &s_testGood);
	p.vrm.size = 4u;
	NativeTestCont_EmitParts("bad-c3-vrm-chain-word-only", &p, 1);

	// The terminating 0 is cut off: the chain runs off the end.
	NativeTestParts_Copy(&p, &s_testGood);
	p.vrm.size = NTC_VRM_END;
	NativeTestCont_EmitParts("bad-c3-vrm-no-terminator", &p, 1);

	// The terminator replaced by a block size - a block that is not there.
	NativeTestParts_Copy(&p, &s_testGood);
	NativeTestBlob_Put32(&p.vrm, NTC_VRM_END, 0x24u);
	NativeTestCont_EmitParts("bad-c3-vrm-terminator-nonzero", &p, 1);

	// Block 1's size far past the chunk.
	NativeTestParts_Copy(&p, &s_testGood);
	NativeTestBlob_Put32(&p.vrm, NTC_VRM_B1, 0x7ffffffcu);
	NativeTestCont_EmitParts("bad-c3-vrm-blocksize-past-end", &p, 1);

	// Block 1's size wraps a 32-bit pointer back to before the buffer.
	NativeTestParts_Copy(&p, &s_testGood);
	NativeTestBlob_Put32(&p.vrm, NTC_VRM_B1, 0xfffffff0u);
	NativeTestCont_EmitParts("bad-c3-vrm-blocksize-wrap", &p, 1);

	// The last block's rectangle needs more pixels than the chunk has.
	NativeTestParts_Copy(&p, &s_testGood);
	NativeTestBlob_Put16(&p.vrm, NTC_VRM_H2 + 0x12u, 64u);
	NativeTestCont_EmitParts("bad-c3-vrm-rect-past-end", &p, 1);

	// Block 1's rectangle is the whole VRAM: 1 MiB of pixels from 616 bytes.
	NativeTestParts_Copy(&p, &s_testGood);
	NativeTestBlob_Put16(&p.vrm, NTC_VRM_H1 + 0x10u, 1024u);
	NativeTestBlob_Put16(&p.vrm, NTC_VRM_H1 + 0x12u, 512u);
	NativeTestCont_EmitParts("bad-c3-vrm-rect-huge", &p, 1);

	// The single-TIM form with a rectangle larger than the data.
	NativeTestParts_Copy(&p, &s_testGood);
	NativeTestBlob_Free(&p.vrm);
	NativeTestVrm_BuildSingle(&p.vrm);
	NativeTestBlob_Put16(&p.vrm, 0x12u, NTC_VRM_B1_H + 1u);
	NativeTestCont_EmitParts("bad-c3-vrm-single-rect-past-end", &p, 1);
}

// c4 - a LEVD shorter than struct Level.
internal void NativeTestCont_C4(void)
{
	struct NativeTestParts p;

	NativeTestParts_Copy(&p, &s_testGood);
	p.lev.size = 0u;
	NativeTestCont_EmitParts("bad-c4-levd-empty", &p, 1);

	NativeTestParts_Copy(&p, &s_testGood);
	p.lev.size = 3u;
	NativeTestCont_EmitParts("bad-c4-levd-3-bytes", &p, 1);

	NativeTestParts_Copy(&p, &s_testGood);
	NativeTestBlob_Free(&p.lev);
	NativeTestLev_BuildShort(&p.lev, 0u);
	NativeTestCont_EmitParts("bad-c4-levd-body-0", &p, 1);

	// Consistent in itself: word 0, a body of 0x1ec bytes, an empty map.
	NativeTestParts_Copy(&p, &s_testGood);
	NativeTestBlob_Free(&p.lev);
	NativeTestLev_BuildShort(&p.lev, NTC_LEVEL_SIZE - 8u);
	NativeTestCont_EmitParts("bad-c4-levd-body-short", &p, 1);

	// The good LEV cut in the middle of struct Level.
	NativeTestParts_Copy(&p, &s_testGood);
	p.lev.size = NTC_LEV_BODY + 0x100u;
	NativeTestCont_EmitParts("bad-c4-levd-cut-in-level", &p, 1);
}

// c5 - LOAD_RunPtrMap relocates a repeated offset twice; and a map or an
// entry that leaves the LEV.
internal void NativeTestCont_C5(void)
{
	static const struct
	{
		const char *name;
		u32 at;    // file offset inside the LEV, or 0xffffffff: append `value` to the map
		u32 value;
	} edits[] = {
	    {"bad-c5-ptrmap-dup-append", 0xffffffffu, NTC_LVL_SPAWN1},
	    // 0x135 rounds down to 0x134 in LOAD_RunPtrMap: the same word twice.
	    {"bad-c5-ptrmap-dup-lowbits", 0xffffffffu, NTC_LVL_SPAWN1 | 1u},
	    // The last entry replaced by the first: same size, one word twice.
	    {"bad-c5-ptrmap-dup-replace", NTC_MAP_AT + 4u * NTC_MAP_COUNT, NTC_LVL_MESH},
	    // A patch site at the end of the body (originBytes - 4 is the last word).
	    {"bad-c5-ptrmap-site-at-body-end", 0xffffffffu, NTC_BODY_SIZE},
	    {"bad-c5-ptrmap-site-huge", 0xffffffffu, 0x7ffffff0u},
	    {"bad-c5-ptrmap-site-negative", 0xffffffffu, 0x80000000u},
	    // A mapped word whose value points far outside the body.
	    {"bad-c5-ptrmap-value-outside", NTC_LEV_BODY + NTC_SPAWN_SLOT(1), 0x7fff0000u},
	    // Word 0 - where the map starts.
	    {"bad-c5-mapoffset-huge", 0u, 0x7ffffff0u},
	    {"bad-c5-mapoffset-negative", 0u, 0xfffffffcu},
	    // numBytes of the map.
	    {"bad-c5-mapbytes-huge", NTC_MAP_AT, 0x7ffffff0u},
	    {"bad-c5-mapbytes-negative", NTC_MAP_AT, 0xfffffffcu},
	    {"bad-c5-mapbytes-one-past", NTC_MAP_AT, NTC_MAP_COUNT * 4u + 4u},
	    {"bad-c5-mapbytes-unaligned", NTC_MAP_AT, NTC_MAP_COUNT * 4u - 2u},
	};
	struct NativeTestParts p;
	size_t i;

	for (i = 0; i < sizeof(edits) / sizeof(edits[0]); i++)
	{
		NativeTestParts_Copy(&p, &s_testGood);
		if (edits[i].at == 0xffffffffu)
		{
			NativeTestLev_MapAppend(&p.lev, edits[i].value);
		}
		else
		{
			NativeTestBlob_Put32(&p.lev, edits[i].at, edits[i].value);
		}
		NativeTestCont_EmitParts(edits[i].name, &p, 1);
	}
}

// c6 - LibraryOfModels.c: gGT->modelPtr[m->id], s16 id, table 0xe3.
internal void NativeTestCont_C6(void)
{
	static const struct
	{
		const char *name;
		u32 id;
	} ids[] = {
	    {"bad-c6-modelid-e3", 0xe3u},
	    {"bad-c6-modelid-7fff", 0x7fffu},
	    {"bad-c6-modelid-minus2", 0xfffeu},
	    {"bad-c6-modelid-8000", 0x8000u},
	};
	struct NativeTestParts p;
	size_t i;

	for (i = 0; i < sizeof(ids) / sizeof(ids[0]); i++)
	{
		NativeTestParts_Copy(&p, &s_testGood);
		NativeTestLev_Set16(&p.lev, NTC_MODEL + NTC_MODEL_ID_OFFSET, ids[i].id);
		NativeTestCont_EmitParts(ids[i].name, &p, 1);
	}
}

// c7 - CAM.c: ptrSpawnType1 NULL, and slots 2 / 3 NULL at count >= 3 / 4.
internal void NativeTestCont_C7(void)
{
	struct NativeTestParts p;

	// NULL: 0 and out of the map (a mapped 0 would become the LEV's origin).
	NativeTestParts_Copy(&p, &s_testGood);
	NativeTestLev_Set32(&p.lev, NTC_LVL_SPAWN1, 0u);
	NativeTestLev_MapRemove(&p.lev, NTC_LVL_SPAWN1);
	NativeTestCont_EmitParts("bad-c7-spawn-null", &p, 1);

	// Set, but not in the map: it would stay a raw offset in the race.
	NativeTestParts_Copy(&p, &s_testGood);
	NativeTestLev_MapRemove(&p.lev, NTC_LVL_SPAWN1);
	NativeTestCont_EmitParts("bad-c7-spawn-unmapped", &p, 1);

	NativeTestParts_Copy(&p, &s_testGood);
	NativeTestLev_Set32(&p.lev, NTC_SPAWN_SLOT(2), 0u);
	NativeTestLev_MapRemove(&p.lev, NTC_SPAWN_SLOT(2));
	NativeTestCont_EmitParts("bad-c7-count3-slot2-null", &p, 1);

	// Slot 3 is 0 in the good LEV; count 4 makes CAM.c read it.
	NativeTestParts_Copy(&p, &s_testGood);
	NativeTestLev_Set32(&p.lev, NTC_SPAWN1, 4u);
	NativeTestCont_EmitParts("bad-c7-count4-slot3-null", &p, 1);

	NativeTestParts_Copy(&p, &s_testGood);
	NativeTestLev_Set32(&p.lev, NTC_SPAWN1, 0x7fffffffu);
	NativeTestCont_EmitParts("bad-c7-count-huge", &p, 1);

	NativeTestParts_Copy(&p, &s_testGood);
	NativeTestLev_Set32(&p.lev, NTC_SPAWN1, 0xffffffffu);
	NativeTestCont_EmitParts("bad-c7-count-negative", &p, 1);
}

// c10 - NativeTrack_MempackExtraNeeded added memTotal and four clip buffers
// in 32 bits: 0xffffff00 wrapped to a few hundred bytes of reserve.
internal void NativeTestCont_C10(void)
{
	static const struct
	{
		const char *name;
		u32 at;
		u32 value;
	} edits[] = {
	    {"bad-c10-meta-total-ffffff00", NTC_META_TOTAL, 0xffffff00u},
	    {"bad-c10-meta-total-one-past-bound", NTC_META_TOTAL, RLD_META_TOTAL_MAX + 1u},
	    {"bad-c10-meta-prim-one-past-bound", NTC_META_PRIM, RLD_META_PRIM_MAX + 1u},
	    {"bad-c10-meta-prim-ffffffff", NTC_META_PRIM, 0xffffffffu},
	};
	struct NativeTestParts p;
	size_t i;

	for (i = 0; i < sizeof(edits) / sizeof(edits[0]); i++)
	{
		NativeTestParts_Copy(&p, &s_testGood);
		NativeTestBlob_Put32(&p.meta, edits[i].at, edits[i].value);
		NativeTestCont_EmitParts(edits[i].name, &p, 1);
	}
}

// c12 - CAM.c walks the end-of-race cameras by data.EndOfRace_Camera_Size[mode]
// and indexes the restart points with each camera's respawn point.
internal void NativeTestCont_C12(void)
{
	static const struct
	{
		const char *name;
		u32 at;
		u32 value;
	} edits[] = {
	    // Mode 1 is marked missing (-1): a stride of one byte.
	    {"bad-c12-eorcam-mode-missing", NTC_EOR_MODE, 1u},
	    {"bad-c12-eorcam-mode-past-table", NTC_EOR_MODE, 0x12u},
	    {"bad-c12-eorcam-mode-7fff", NTC_EOR_MODE, 0x7fffu},
	    // -32768: its absolute value is 32768 again.
	    {"bad-c12-eorcam-mode-8000", NTC_EOR_MODE, 0x8000u},
	    {"bad-c12-eorcam-respawn-past-points", NTC_EOR_RESPAWN, 1u},
	    {"bad-c12-eorcam-respawn-negative", NTC_EOR_RESPAWN, 0xffffu},
	    // The second camera starts where the first ends, and what follows is
	    // no camera: refused somewhere along the walk, never read past it.
	    {"bad-c12-eorcam-count-7fff", NTC_EORCAM, 0x7fffu},
	    {"bad-c12-eorcam-count-negative", NTC_EORCAM, 0xffffu},
	};
	struct NativeTestParts p;
	size_t i;

	for (i = 0; i < sizeof(edits) / sizeof(edits[0]); i++)
	{
		NativeTestParts_Copy(&p, &s_testGood);
		NativeTestLev_Set16(&p.lev, edits[i].at, edits[i].value);
		NativeTestCont_EmitParts(edits[i].name, &p, 1);
	}

	// Mode 9 follows the track path from the restart point in its first data
	// word; 16 bytes of data still end inside the body (0x436 of 0x440).
	NativeTestParts_Copy(&p, &s_testGood);
	NativeTestLev_Set16(&p.lev, NTC_EOR_MODE, 9u);
	NativeTestLev_Set16(&p.lev, NTC_EOR_DATA, 5u);
	NativeTestCont_EmitParts("bad-c12-eorcam-path-past-points", &p, 1);

	// The table moved to the last halfword of the body: its count says one
	// camera, and the camera would start at the end of the body.
	NativeTestParts_Copy(&p, &s_testGood);
	NativeTestLev_Set32(&p.lev, NTC_SPAWN_SLOT(2), NTC_BODY_SIZE - 2u);
	NativeTestLev_Set16(&p.lev, NTC_BODY_SIZE - 2u, 1u);
	NativeTestCont_EmitParts("bad-c12-eorcam-table-at-body-end", &p, 1);
}

// c13 - UI_Map_GetIconPos divides by worldEnd - worldStart.
internal void NativeTestCont_C13(void)
{
	struct NativeTestParts p;

	NativeTestParts_Copy(&p, &s_testGood);
	NativeTestLev_Set16(&p.lev, NTC_UIMAP + 0x0u, 0x10000u - 2000u);
	NativeTestCont_EmitParts("bad-c13-map-width-0", &p, 1);

	NativeTestParts_Copy(&p, &s_testGood);
	NativeTestLev_Set16(&p.lev, NTC_UIMAP + 0x2u, 0x10000u - 2000u);
	NativeTestCont_EmitParts("bad-c13-map-height-0", &p, 1);
}

// c14 - the restart points: VehStuckProc, VehLap and RB_Warpball index
// them with a quadblock's checkpointIndex and with the links of the nodes.
internal void NativeTestCont_C14(void)
{
	static const struct
	{
		const char *name;
		u32 at;    // body offset
		u32 value; // one byte
	} bytes[] = {
	    {"bad-c14-quad-checkpoint-past-points", NTC_QUAD + NTC_QUAD_CHECKPOINT, 1u},
	    {"bad-c14-quad-checkpoint-fe", NTC_QUAD + NTC_QUAD_CHECKPOINT, 0xfeu},
	    {"bad-c14-restart-forward-past-points", NTC_RESTART + NTC_NODE_FORWARD, 1u},
	    // Forward has no "none": every point leads on.
	    {"bad-c14-restart-forward-none", NTC_RESTART + NTC_NODE_FORWARD, NTC_NODE_NONE},
	    {"bad-c14-restart-left-past-points", NTC_RESTART + NTC_NODE_FORWARD + 1u, 5u},
	    {"bad-c14-restart-right-past-points", NTC_RESTART + NTC_NODE_FORWARD + 3u, 0xfeu},
	};
	struct NativeTestParts p;
	size_t i;

	for (i = 0; i < sizeof(bytes) / sizeof(bytes[0]); i++)
	{
		NativeTestParts_Copy(&p, &s_testGood);
		p.lev.data[NTC_LEV_BODY + bytes[i].at] = (u8)bytes[i].value;
		NativeTestCont_EmitParts(bytes[i].name, &p, 1);
	}

	// Two points where the body has room for one.
	NativeTestParts_Copy(&p, &s_testGood);
	NativeTestLev_Set32(&p.lev, NTC_LVL_NUM_RESTART, 2u);
	NativeTestCont_EmitParts("bad-c14-restart-count-past-body", &p, 1);

	NativeTestParts_Copy(&p, &s_testGood);
	NativeTestLev_Set32(&p.lev, NTC_LVL_NUM_RESTART, 256u);
	NativeTestCont_EmitParts("bad-c14-restart-count-256", &p, 1);

	NativeTestParts_Copy(&p, &s_testGood);
	NativeTestLev_Set32(&p.lev, NTC_LVL_NUM_RESTART, 0xffffffffu);
	NativeTestCont_EmitParts("bad-c14-restart-count-negative", &p, 1);

	// No restart points, but the quadblock still names point 0.
	NativeTestParts_Copy(&p, &s_testGood);
	NativeTestLev_Set32(&p.lev, NTC_LVL_NUM_RESTART, 0u);
	NativeTestLev_Set16(&p.lev, NTC_EORCAM, 0u);
	NativeTestCont_EmitParts("bad-c14-quad-checkpoint-without-points", &p, 1);
}

// c15 - VehLap and RB_Warpball take the remainder by the track length,
// distToFinish of restart point 0.
internal void NativeTestCont_C15(void)
{
	struct NativeTestParts p;

	NativeTestParts_Copy(&p, &s_testGood);
	NativeTestLev_Set16(&p.lev, NTC_RESTART + NTC_NODE_DISTANCE, 0u);
	NativeTestCont_EmitParts("bad-c15-restart-disttofinish-0", &p, 1);
}

// META and the memory need.
internal void NativeTestCont_Meta(void)
{
	static const struct
	{
		const char *name;
		u32 at;
		u32 value;
	} edits[] = {
	    {"bad-meta-stringcount-huge", NTC_META_COUNT, 0xffffffffu},
	    {"bad-meta-stringlength-huge", NTC_META_STR0, 0xffffffffu},
	    {"bad-meta-stringlength-wrap", NTC_META_STR0, 0xfffffff0u},
	    {"bad-mem-total-understated", NTC_META_TOTAL, 0u},
	    {"bad-mem-prim-understated", NTC_META_PRIM, 0u},
	};
	struct NativeTestParts p;
	size_t i;

	for (i = 0; i < sizeof(edits) / sizeof(edits[0]); i++)
	{
		NativeTestParts_Copy(&p, &s_testGood);
		NativeTestBlob_Put32(&p.meta, edits[i].at, edits[i].value);
		NativeTestCont_EmitParts(edits[i].name, &p, 1);
	}

	NativeTestParts_Copy(&p, &s_testGood);
	p.meta.size = RLD_META_FIXED_SIZE - 1u;
	NativeTestCont_EmitParts("bad-meta-short", &p, 1);
}

// The envelope: header, directory, offsets, lengths, counts, hashes. Each
// edit writes a 16-, 32- or 64-bit value, four type bytes, or flips a bit.
#define NTC_E16 1
#define NTC_E32 2
#define NTC_E64 3
#define NTC_ETYPE 4
#define NTC_EFLIP 5

internal void NativeTestCont_Edit(const char *name, int kind, size_t at, u64 value, const char *type)
{
	struct NativeTestBlob f;

	NativeTestBlob_Copy(&f, &s_testGoodFile);
	switch (kind)
	{
	case NTC_E16:
		NativeTestBlob_Put16(&f, at, (u32)value);
		break;
	case NTC_E32:
		NativeTestBlob_Put32(&f, at, (u32)value);
		break;
	case NTC_E64:
		NativeTestBlob_Put64(&f, at, value);
		break;
	case NTC_ETYPE:
		memcpy(&f.data[at], type, 4);
		break;
	default:
		f.data[at] ^= (u8)value;
		break;
	}
	NativeTestCont_EmitFile(name, &f);
}

internal void NativeTestCont_Envelope(void)
{
	const struct NativeTestLayout *l = &s_testGoodLayout;
	struct NativeTestBlob f;

	NativeTestBlob_Copy(&f, &s_testGoodFile);
	f.data[0] = 'X';
	NativeTestCont_EmitFile("bad-hdr-magic", &f);

	NativeTestCont_Edit("bad-hdr-major-older", NTC_E16, 0x08, RLD_VERSION_MAJOR - 1u, NULL);
	NativeTestCont_Edit("bad-hdr-major-newer", NTC_E16, 0x08, RLD_VERSION_MAJOR + 1u, NULL);
	NativeTestCont_Edit("bad-hdr-unknown-feature", NTC_E32, 0x0c, 0x80000000u, NULL);
	NativeTestCont_Edit("bad-hdr-reserved", NTC_E32, 0x14, 1u, NULL);
	NativeTestCont_Edit("bad-hdr-filesize-plus-1", NTC_E64, 0x20, l->fileSize + 1u, NULL);
	NativeTestCont_Edit("bad-hdr-filesize-huge", NTC_E64, 0x20, 0xffffffffffffffffull, NULL);

	NativeTestCont_Edit("bad-cnt-chunks-0", NTC_E32, 0x10, 0u, NULL);
	NativeTestCont_Edit("bad-cnt-chunks-2", NTC_E32, 0x10, 2u, NULL);
	NativeTestCont_Edit("bad-cnt-chunks-one-more", NTC_E32, 0x10, (u64)l->chunkCount + 1u, NULL);
	NativeTestCont_Edit("bad-cnt-chunks-65", NTC_E32, 0x10, RLD_CHUNK_MAX + 1u, NULL);
	NativeTestCont_Edit("bad-cnt-chunks-huge", NTC_E32, 0x10, 0xffffffffu, NULL);

	NativeTestCont_Edit("bad-off-dir-in-header", NTC_E64, 0x18, 8u, NULL);
	NativeTestCont_Edit("bad-off-dir-at-end", NTC_E64, 0x18, l->fileSize, NULL);
	NativeTestCont_Edit("bad-off-dir-plus-1", NTC_E64, 0x18, l->dirOffset + 1u, NULL);
	NativeTestCont_Edit("bad-off-dir-wrap", NTC_E64, 0x18, 0xffffffffffffffc0ull, NULL);
	NativeTestCont_Edit("bad-off-levd-zero", NTC_E64, NativeTestCont_DirField(l, NTC_PART_LEVD, 0x08), 0u, NULL);
	NativeTestCont_Edit("bad-off-levd-at-end", NTC_E64, NativeTestCont_DirField(l, NTC_PART_LEVD, 0x08), l->fileSize, NULL);
	NativeTestCont_Edit("bad-off-levd-wrap", NTC_E64, NativeTestCont_DirField(l, NTC_PART_LEVD, 0x08), 0xffffffffffffff00ull, NULL);
	NativeTestCont_Edit("bad-off-vrmd-overlaps-levd", NTC_E64, NativeTestCont_DirField(l, NTC_PART_VRMD, 0x08), l->offset[NTC_PART_LEVD], NULL);
	NativeTestCont_Edit("bad-off-parm-into-directory", NTC_E64, NativeTestCont_DirField(l, NTC_PART_PARM, 0x08),
	                    l->dirOffset - l->size[NTC_PART_PARM] + 1u, NULL);
	// 4 GiB further: the same offset for a reader that truncates to 32 bits.
	NativeTestCont_Edit("bad-off-sndb-plus-4g", NTC_E64, NativeTestCont_DirField(l, NTC_PART_SNDB, 0x08), 0x100000000ull + l->offset[NTC_PART_SNDB],
	                    NULL);

	// Stored and raw size together, so the two stay equal.
	NativeTestBlob_Copy(&f, &s_testGoodFile);
	NativeTestBlob_Put64(&f, NativeTestCont_DirField(l, NTC_PART_LEVD, 0x10), 0xffffffffffffffffull);
	NativeTestBlob_Put64(&f, NativeTestCont_DirField(l, NTC_PART_LEVD, 0x18), 0xffffffffffffffffull);
	NativeTestCont_EmitFile("bad-len-levd-huge", &f);

	NativeTestBlob_Copy(&f, &s_testGoodFile);
	NativeTestBlob_Put64(&f, NativeTestCont_DirField(l, NTC_PART_LEVD, 0x10), 0x100000000ull + l->size[NTC_PART_LEVD]);
	NativeTestBlob_Put64(&f, NativeTestCont_DirField(l, NTC_PART_LEVD, 0x18), 0x100000000ull + l->size[NTC_PART_LEVD]);
	NativeTestCont_EmitFile("bad-len-levd-plus-4g", &f);

	NativeTestBlob_Copy(&f, &s_testGoodFile);
	NativeTestBlob_Put64(&f, NativeTestCont_DirField(l, NTC_PART_META, 0x10), RLD_LIMIT_META + 1u);
	NativeTestBlob_Put64(&f, NativeTestCont_DirField(l, NTC_PART_META, 0x18), RLD_LIMIT_META + 1u);
	NativeTestCont_EmitFile("bad-len-meta-over-limit", &f);

	NativeTestCont_Edit("bad-len-levd-raw-differs", NTC_E64, NativeTestCont_DirField(l, NTC_PART_LEVD, 0x18), l->size[NTC_PART_LEVD] + 1u, NULL);

	NativeTestCont_Edit("bad-dir-levd-compressed", NTC_E32, NativeTestCont_DirField(l, NTC_PART_LEVD, 0x04), 1u, NULL);
	NativeTestCont_Edit("bad-dir-type-twice", NTC_ETYPE, NativeTestCont_DirField(l, NTC_PART_SNDB, 0), 0u, "LEVD");
	NativeTestCont_Edit("bad-dir-vrmd-missing", NTC_ETYPE, NativeTestCont_DirField(l, NTC_PART_VRMD, 0), 0u, "XXXX");
	NativeTestCont_Edit("bad-dir-meta-missing", NTC_ETYPE, NativeTestCont_DirField(l, NTC_PART_META, 0), 0u, "XXXX");

	NativeTestCont_Edit("bad-hash-levd", NTC_EFLIP, (size_t)l->offset[NTC_PART_LEVD] + NTC_LEV_BODY + NTC_SPAWN1, 0x01u, NULL);
	NativeTestCont_Edit("bad-hash-vrmd", NTC_EFLIP, (size_t)l->offset[NTC_PART_VRMD] + NTC_VRM_B1, 0x04u, NULL);
	NativeTestCont_Edit("bad-hash-meta", NTC_EFLIP, (size_t)l->offset[NTC_PART_META] + NTC_META_TOTAL, 0x01u, NULL);
	NativeTestCont_Edit("bad-hash-levd-directory", NTC_EFLIP, NativeTestCont_DirField(l, NTC_PART_LEVD, RLD_DIR_HASH_OFFSET), 0x80u, NULL);

	// NOT here: a broken SNDB or PARM hash. NativeTrack_Load reads both as
	// optional - the track loads without its sound or with default values -
	// so such a file is not "bad" by the game's rules.
}

// The good file cut short at many points: as a download that stopped
// ("trunc", file_size still says the full length) and with file_size patched
// to the cut ("truncfix"), so that the directory and chunk bounds have to
// catch it instead of the size check.
internal void NativeTestCont_Truncations(void)
{
	const struct NativeTestLayout *l = &s_testGoodLayout;
	u64 cuts[32];
	int n = 0;
	int i;

	cuts[n++] = 0;
	cuts[n++] = 1;
	cuts[n++] = 7;
	cuts[n++] = 8;
	cuts[n++] = 16;
	cuts[n++] = 24;
	cuts[n++] = 32;
	cuts[n++] = RLD_HEADER_SIZE - 1u;
	cuts[n++] = RLD_HEADER_SIZE;
	cuts[n++] = RLD_HEADER_SIZE + 1u;
	cuts[n++] = l->offset[NTC_PART_META] + l->size[NTC_PART_META] / 2u;
	cuts[n++] = l->offset[NTC_PART_LEVD];
	cuts[n++] = l->offset[NTC_PART_LEVD] + 4u;
	cuts[n++] = l->offset[NTC_PART_LEVD] + l->size[NTC_PART_LEVD] / 2u;
	cuts[n++] = l->offset[NTC_PART_LEVD] + l->size[NTC_PART_LEVD] - 1u;
	cuts[n++] = l->offset[NTC_PART_VRMD] + l->size[NTC_PART_VRMD] / 2u;
	cuts[n++] = l->offset[NTC_PART_SNDB] + 20u;
	cuts[n++] = l->offset[NTC_PART_SNDB] + l->size[NTC_PART_SNDB] / 2u;
	cuts[n++] = l->offset[NTC_PART_PARM] + 1u;
	cuts[n++] = l->dirOffset;
	cuts[n++] = l->dirOffset + 1u;
	cuts[n++] = l->dirOffset + RLD_DIR_ENTRY_SIZE;
	cuts[n++] = l->dirOffset + RLD_DIR_ENTRY_SIZE * 2u + RLD_DIR_HASH_OFFSET;
	cuts[n++] = l->fileSize - 1u;

	for (i = 0; i < n; i++)
	{
		struct NativeTestBlob f;
		char name[64];

		NativeTestBlob_Copy(&f, &s_testGoodFile);
		f.size = (size_t)cuts[i];
		snprintf(name, sizeof(name), "bad-trunc-at-%05u", (u32)cuts[i]);
		NativeTestCont_EmitFile(name, &f);

		if (cuts[i] >= RLD_HEADER_SIZE)
		{
			NativeTestBlob_Copy(&f, &s_testGoodFile);
			f.size = (size_t)cuts[i];
			NativeTestBlob_Put64(&f, 0x20, cuts[i]);
			snprintf(name, sizeof(name), "bad-truncfix-at-%05u", (u32)cuts[i]);
			NativeTestCont_EmitFile(name, &f);
		}
	}
}

int NativeTestFiles_MakeContainers(const char *dir)
{
	s_testContDir = dir;
	s_testWritten = 0;
	s_testMismatches = 0;
	s_testWriteFailed = 0;

#if defined(_WIN32)
	(void)_mkdir(dir); // an existing folder is fine; a failure shows when writing
#else
	(void)mkdir(dir, 0777);
#endif

	memset(&s_testGood, 0, sizeof(s_testGood));
	NativeTestLev_Build(&s_testGood.lev);
	NativeTestVrm_Build(&s_testGood.vrm);
	NativeTestSndb_Build(&s_testGood.sndb);
	NativeTestParm_Build(&s_testGood.parm);
	NativeTestMeta_Build(&s_testGood.meta, &s_testGood.lev, "Self-test Synthetic");
	s_testGood.hasSndb = 1;
	s_testGood.hasParm = 1;
	NativeTestCont_Assemble(&s_rldTrackFormat, &s_testGood, &s_testGoodFile, &s_testGoodLayout);

	printf("[selftest] test containers into %s\n", dir);
	printf("[selftest] good-synthetic: %u bytes - META %u, LEVD %u, VRMD %u, SNDB %u, PARM %u\n", (u32)s_testGoodFile.size, (u32)s_testGood.meta.size,
	       (u32)s_testGood.lev.size, (u32)s_testGood.vrm.size, (u32)s_testGood.sndb.size, (u32)s_testGood.parm.size);
	printf("  %-38s %-50s %s\n", "file", "differs from good-synthetic", "reference model");

	NativeTestCont_Goods();
	NativeTestCont_S1();
	NativeTestCont_C1C2();
	NativeTestCont_C3();
	NativeTestCont_C4();
	NativeTestCont_C5();
	NativeTestCont_C6();
	NativeTestCont_C7();
	NativeTestCont_C10();
	NativeTestCont_C12();
	NativeTestCont_C13();
	NativeTestCont_C14();
	NativeTestCont_C15();
	NativeTestCont_Meta();
	NativeTestCont_Envelope();
	NativeTestCont_Truncations();

	NativeTestParts_Free(&s_testGood);
	NativeTestBlob_Free(&s_testGoodFile);

	printf("[selftest] %d test containers written", s_testWritten);
	if (s_testWriteFailed)
	{
		printf(" - WRITE FAILED\n");
		return 1;
	}
	if (s_testMismatches != 0)
	{
		printf(", %d MODEL MISMATCH(ES) - a variant no longer triggers what its name says\n", s_testMismatches);
		return 1;
	}
	printf(", the reference model agrees with every name\n");
	return 0;
}

// ===========================================================================
// PART 2 - DISC IMAGES
//
// Writes good-*.bin and bad-*.bin into <folder>. Every image is generated
// here, byte by byte: a few kilobytes of raw MODE2/2352 sectors with an ISO
// 9660 volume, a SYSTEM.CNF naming SCUS_944.26 and tiny text files. No game
// data, nothing read from anywhere.
//
// WHAT THE READER NEEDS (platform/native_disc_image.c), AND ONLY THAT:
//   - raw 2352-byte sectors from byte 0, each with the sync pattern and mode 2
//     (no .cue is read);
//   - the primary volume descriptor at sector 16 ("CD001", version 1) with the
//     root directory record at byte 156;
//   - Form 1 payload at byte 24 of a sector; a file is Form 2 when bit 5 of the
//     submode byte (byte 18) of its first sector is set;
//   - SYSTEM.CNF in the root, whose text contains the serial SCUS_944.26.
// EDC/ECC are not checked by the reader and stay zero.
//
// good-* images must be extracted, bad-* must be refused - each bad image
// breaks exactly one thing, named where it is built. The payload that tries
// to escape is always called SELFTEST_ESCAPED.TXT, so a broken unpacker leaves
// a file with that name beside the assets folder or in <folder>, where the
// self-test's listing finds it.
// ===========================================================================

#define NTD_SECTOR_BYTES 2352u
#define NTD_DATA_OFFSET 24u
#define NTD_DATA_BYTES 2048u
#define NTD_USER_OFFSET 16u
#define NTD_USER_BYTES 2336u
#define NTD_PVD_LBA 16u
#define NTD_FIRST_FREE_LBA 18u
#define NTD_MAX_SECTORS 512u
#define NTD_MAX_NODES 512
#define NTD_MAX_KIDS 96
#define NTD_SUBMODE_DATA 0x08u
#define NTD_SUBMODE_FORM2 0x20u
#define NTD_DIRECTORY_FLAG 0x02u
#define NTD_DIR_BUFFER_BYTES (16u * NTD_DATA_BYTES)

#define NTD_NAME(s) (s), (unsigned)(sizeof(s) - 1u)

struct NativeTestDiscNode
{
	char name[256];
	unsigned nameLen;
	int isDir;

	const char *text; // Form 1 file contents
	unsigned form2Sectors;

	struct NativeTestDiscNode *kids[NTD_MAX_KIDS];
	int kidCount;
	struct NativeTestDiscNode *parent;

	// The record points at this node's extent instead of one of its own: the
	// loop records. Not laid out, only referenced.
	struct NativeTestDiscNode *alias;

	// Nonzero: the record claims this size instead of the real one.
	unsigned claimSize;

	// Nonzero: the record's length byte is written as this (a broken record).
	unsigned brokenLength;

	unsigned lba;
	unsigned size;
};

// Allocated for the run of NativeTestFiles_MakeDisc only, not for every game.
global_variable struct NativeTestDiscNode *s_testDiscNodes;
global_variable int s_testDiscNodeCount;
global_variable unsigned char *s_testDiscImage;
global_variable unsigned char *s_testDiscDirBuffer;
global_variable unsigned s_testDiscSectorCount;

global_variable const char s_testDiscEscaped[] = "written by the disc self-test - if this file is outside an assets folder, the unpacker let a name escape\n";

internal void NativeTestDisc_Fatal(const char *what)
{
	fprintf(stderr, "[selftest] test disc generator: %s\n", what);
	exit(1);
}

internal struct NativeTestDiscNode *NativeTestDisc_NewNode(struct NativeTestDiscNode *parent, const char *name, unsigned nameLen, int isDir)
{
	struct NativeTestDiscNode *node;

	if ((s_testDiscNodeCount >= NTD_MAX_NODES) || (nameLen > 200u) || ((parent != NULL) && (parent->kidCount >= NTD_MAX_KIDS)))
	{
		NativeTestDisc_Fatal("image description too large");
	}

	node = &s_testDiscNodes[s_testDiscNodeCount++];
	memset(node, 0, sizeof(*node));
	memcpy(node->name, name, nameLen);
	node->nameLen = nameLen;
	node->isDir = isDir;
	node->parent = parent;

	if (parent != NULL)
	{
		parent->kids[parent->kidCount++] = node;
	}

	return node;
}

internal struct NativeTestDiscNode *NativeTestDisc_Dir(struct NativeTestDiscNode *parent, const char *name, unsigned nameLen)
{
	return NativeTestDisc_NewNode(parent, name, nameLen, 1);
}

internal struct NativeTestDiscNode *NativeTestDisc_File(struct NativeTestDiscNode *parent, const char *name, unsigned nameLen, const char *text)
{
	struct NativeTestDiscNode *node = NativeTestDisc_NewNode(parent, name, nameLen, 0);

	node->text = text;
	return node;
}

internal struct NativeTestDiscNode *NativeTestDisc_Form2File(struct NativeTestDiscNode *parent, const char *name, unsigned nameLen, unsigned sectors)
{
	struct NativeTestDiscNode *node = NativeTestDisc_NewNode(parent, name, nameLen, 0);

	node->form2Sectors = sectors;
	return node;
}

// A directory record that points at an existing directory.
internal struct NativeTestDiscNode *NativeTestDisc_Alias(struct NativeTestDiscNode *parent, const char *name, unsigned nameLen,
                                                         struct NativeTestDiscNode *target)
{
	struct NativeTestDiscNode *node = NativeTestDisc_NewNode(parent, name, nameLen, 1);

	node->alias = target;
	return node;
}

internal unsigned NativeTestDisc_RecordLength(unsigned nameLen)
{
	return 33u + nameLen + (((nameLen & 1u) == 0u) ? 1u : 0u);
}

internal unsigned NativeTestDisc_DirectoryBytes(const struct NativeTestDiscNode *dir)
{
	unsigned at = 0;
	int i;

	for (i = -2; i < dir->kidCount; i++)
	{
		const unsigned length = NativeTestDisc_RecordLength((i < 0) ? 1u : dir->kids[i]->nameLen);

		if (((at % NTD_DATA_BYTES) + length) > NTD_DATA_BYTES)
		{
			at = ((at / NTD_DATA_BYTES) + 1u) * NTD_DATA_BYTES;
		}

		at += length;
	}

	return ((at + NTD_DATA_BYTES - 1u) / NTD_DATA_BYTES) * NTD_DATA_BYTES;
}

internal void NativeTestDisc_Layout(struct NativeTestDiscNode *node, unsigned *next)
{
	int i;

	if (node->alias != NULL)
	{
		return;
	}

	node->lba = *next;

	if (node->isDir)
	{
		node->size = NativeTestDisc_DirectoryBytes(node);
		*next += node->size / NTD_DATA_BYTES;

		for (i = 0; i < node->kidCount; i++)
		{
			NativeTestDisc_Layout(node->kids[i], next);
		}
	}
	else if (node->form2Sectors != 0)
	{
		node->size = node->form2Sectors * NTD_DATA_BYTES;
		*next += node->form2Sectors;
	}
	else
	{
		node->size = (unsigned)strlen(node->text);
		*next += (node->size + NTD_DATA_BYTES - 1u) / NTD_DATA_BYTES;
	}

	if (*next > NTD_MAX_SECTORS)
	{
		NativeTestDisc_Fatal("image larger than the sector budget");
	}
}

internal unsigned char *NativeTestDisc_Sector(unsigned lba)
{
	return &s_testDiscImage[(size_t)lba * NTD_SECTOR_BYTES];
}

internal void NativeTestDisc_Le32(unsigned char *at, unsigned value)
{
	at[0] = (unsigned char)(value & 0xffu);
	at[1] = (unsigned char)((value >> 8) & 0xffu);
	at[2] = (unsigned char)((value >> 16) & 0xffu);
	at[3] = (unsigned char)((value >> 24) & 0xffu);
}

internal void NativeTestDisc_Be32(unsigned char *at, unsigned value)
{
	at[0] = (unsigned char)((value >> 24) & 0xffu);
	at[1] = (unsigned char)((value >> 16) & 0xffu);
	at[2] = (unsigned char)((value >> 8) & 0xffu);
	at[3] = (unsigned char)(value & 0xffu);
}

internal unsigned char NativeTestDisc_Bcd(unsigned value)
{
	return (unsigned char)(((value / 10u) << 4) | (value % 10u));
}

// Sync, header, mode 2, and a subheader - on every sector of the image.
internal void NativeTestDisc_FrameSectors(void)
{
	unsigned lba;

	for (lba = 0; lba < s_testDiscSectorCount; lba++)
	{
		unsigned char *sector = NativeTestDisc_Sector(lba);
		const unsigned address = lba + 150u;

		sector[0] = 0x00;
		memset(&sector[1], 0xff, 10);
		sector[11] = 0x00;
		sector[12] = NativeTestDisc_Bcd(address / (60u * 75u));
		sector[13] = NativeTestDisc_Bcd((address / 75u) % 60u);
		sector[14] = NativeTestDisc_Bcd(address % 75u);
		sector[15] = 0x02;

		if ((sector[18] & NTD_SUBMODE_FORM2) == 0)
		{
			sector[18] = NTD_SUBMODE_DATA;
			sector[22] = NTD_SUBMODE_DATA;
		}
	}
}

internal void NativeTestDisc_PutRecord(unsigned char *at, unsigned lba, unsigned size, int isDir, const char *name, unsigned nameLen,
                                       unsigned brokenLength)
{
	const unsigned length = NativeTestDisc_RecordLength(nameLen);

	at[0] = (unsigned char)((brokenLength != 0) ? brokenLength : length);
	NativeTestDisc_Le32(&at[2], lba);
	NativeTestDisc_Be32(&at[6], lba);
	NativeTestDisc_Le32(&at[10], size);
	NativeTestDisc_Be32(&at[14], size);
	at[18] = 99; // 1999-10-01
	at[19] = 10;
	at[20] = 1;
	at[25] = (unsigned char)(isDir ? NTD_DIRECTORY_FLAG : 0u);
	at[28] = 1;
	at[31] = 1;
	at[32] = (unsigned char)nameLen;
	memcpy(&at[33], name, nameLen);
}

internal void NativeTestDisc_WriteData(unsigned lba, const unsigned char *bytes, unsigned size)
{
	unsigned done = 0;

	while (done < size)
	{
		const unsigned chunk = ((size - done) < NTD_DATA_BYTES) ? (size - done) : NTD_DATA_BYTES;

		memcpy(&NativeTestDisc_Sector(lba)[NTD_DATA_OFFSET], &bytes[done], chunk);
		done += chunk;
		lba++;
	}
}

internal void NativeTestDisc_WriteNode(const struct NativeTestDiscNode *node)
{
	int i;

	if (node->alias != NULL)
	{
		return;
	}

	if (node->isDir)
	{
		// One shared buffer: it is written to the image before the recursion
		// below reuses it.
		unsigned char *buffer = s_testDiscDirBuffer;
		unsigned at = 0;
		const struct NativeTestDiscNode *parent = (node->parent != NULL) ? node->parent : node;

		if (node->size > NTD_DIR_BUFFER_BYTES)
		{
			NativeTestDisc_Fatal("directory too large");
		}

		memset(buffer, 0, NTD_DIR_BUFFER_BYTES);

		for (i = -2; i < node->kidCount; i++)
		{
			const struct NativeTestDiscNode *kid = (i < 0) ? NULL : node->kids[i];
			const struct NativeTestDiscNode *target = (kid == NULL) ? ((i == -2) ? node : parent) : ((kid->alias != NULL) ? kid->alias : kid);
			const char self = (i == -2) ? '\0' : '\1';
			const unsigned nameLen = (kid == NULL) ? 1u : kid->nameLen;
			const unsigned length = NativeTestDisc_RecordLength(nameLen);
			const unsigned size = ((kid != NULL) && (kid->claimSize != 0)) ? kid->claimSize : target->size;

			if (((at % NTD_DATA_BYTES) + length) > NTD_DATA_BYTES)
			{
				at = ((at / NTD_DATA_BYTES) + 1u) * NTD_DATA_BYTES;
			}

			NativeTestDisc_PutRecord(&buffer[at], target->lba, size, (kid == NULL) ? 1 : kid->isDir, (kid == NULL) ? &self : kid->name, nameLen,
			                         (kid != NULL) ? kid->brokenLength : 0u);
			at += length;
		}

		NativeTestDisc_WriteData(node->lba, buffer, node->size);

		for (i = 0; i < node->kidCount; i++)
		{
			NativeTestDisc_WriteNode(node->kids[i]);
		}
	}
	else if (node->form2Sectors != 0)
	{
		unsigned s;

		for (s = 0; s < node->form2Sectors; s++)
		{
			unsigned char *sector = NativeTestDisc_Sector(node->lba + s);

			sector[NTD_USER_OFFSET + 2u] = NTD_SUBMODE_FORM2;
			sector[NTD_USER_OFFSET + 6u] = NTD_SUBMODE_FORM2;
			memset(&sector[NTD_DATA_OFFSET], (int)('a' + (s % 26u)), NTD_USER_BYTES - 8u);
		}
	}
	else
	{
		NativeTestDisc_WriteData(node->lba, (const unsigned char *)node->text, node->size);
	}
}

internal void NativeTestDisc_WriteVolume(const struct NativeTestDiscNode *root)
{
	unsigned char *pvd = &NativeTestDisc_Sector(NTD_PVD_LBA)[NTD_DATA_OFFSET];
	unsigned char *end = &NativeTestDisc_Sector(NTD_PVD_LBA + 1u)[NTD_DATA_OFFSET];

	pvd[0] = 1;
	memcpy(&pvd[1], "CD001", 5);
	pvd[6] = 1;
	memset(&pvd[8], ' ', 32);
	memcpy(&pvd[8], "PLAYSTATION", 11);
	memset(&pvd[40], ' ', 32);
	memcpy(&pvd[40], "SELFTEST", 8);
	NativeTestDisc_Le32(&pvd[80], s_testDiscSectorCount);
	NativeTestDisc_Be32(&pvd[84], s_testDiscSectorCount);
	pvd[128] = 0x00; // logical block size 2048, both byte orders
	pvd[129] = 0x08;
	pvd[130] = 0x08;
	pvd[131] = 0x00;
	NativeTestDisc_PutRecord(&pvd[156], root->lba, root->size, 1, "", 1, 0);

	end[0] = 0xff;
	memcpy(&end[1], "CD001", 5);
	end[6] = 1;
}

global_variable const char s_testDiscSystemCnf[] = "BOOT = cdrom:\\SCUS_944.26;1\r\nTCB = 4\r\nEVENT = 10\r\nSTACK = 801FFF00\r\n";
global_variable const char s_testDiscWrongCnf[] = "BOOT = cdrom:\\SLES_021.05;1\r\nTCB = 4\r\nEVENT = 10\r\nSTACK = 801FFF00\r\n";
global_variable const char s_testDiscDummyExe[] = "not an executable - a made-up file of the disc self-test\n";
global_variable const char s_testDiscNote[] = "a made-up file of the disc self-test\n";

internal struct NativeTestDiscNode *NativeTestDisc_NewRoot(int withBoot)
{
	struct NativeTestDiscNode *root;

	s_testDiscNodeCount = 0;
	root = NativeTestDisc_Dir(NULL, "", 0);

	if (withBoot)
	{
		NativeTestDisc_File(root, NTD_NAME("SYSTEM.CNF;1"), s_testDiscSystemCnf);
		NativeTestDisc_File(root, NTD_NAME("SCUS_944.26;1"), s_testDiscDummyExe);
	}

	return root;
}

// Lays out, frames and writes one image. cutSectors != 0 keeps only the first
// cutSectors sectors (a truncated download).
internal int NativeTestDisc_WriteImage(const char *folder, const char *name, struct NativeTestDiscNode *root, unsigned cutSectors)
{
	char path[1024];
	unsigned next = NTD_FIRST_FREE_LBA;
	size_t bytes;
	FILE *out;

	NativeTestDisc_Layout(root, &next);
	s_testDiscSectorCount = next;
	memset(s_testDiscImage, 0, (size_t)NTD_MAX_SECTORS * NTD_SECTOR_BYTES);
	NativeTestDisc_WriteNode(root);
	NativeTestDisc_WriteVolume(root);
	NativeTestDisc_FrameSectors();

	bytes = (size_t)(((cutSectors != 0) && (cutSectors < s_testDiscSectorCount)) ? cutSectors : s_testDiscSectorCount) * NTD_SECTOR_BYTES;

	if (snprintf(path, sizeof(path), "%s/%s.bin", folder, name) >= (int)sizeof(path))
	{
		fprintf(stderr, "[selftest] test disc generator: path too long\n");
		return 0;
	}

	out = fopen(path, "wb");
	if (out == NULL)
	{
		fprintf(stderr, "[selftest] test disc generator: cannot write %s\n", path);
		return 0;
	}
	if ((fwrite(s_testDiscImage, 1, bytes, out) != bytes) | (fclose(out) != 0))
	{
		fprintf(stderr, "[selftest] test disc generator: cannot write %s\n", path);
		return 0;
	}

	printf("  %s.bin (%u sectors)\n", name, (unsigned)(bytes / NTD_SECTOR_BYTES));
	return 1;
}

int NativeTestFiles_MakeDisc(const char *dir)
{
	const char *folder = dir;
	struct NativeTestDiscNode *root;
	struct NativeTestDiscNode *a;
	struct NativeTestDiscNode *b;
	char name[32];
	int ok = 1;
	int i;

	// The folder itself, if it is not there yet (one level; an existing one is
	// fine). Old images in it are overwritten.
#if defined(_WIN32)
	(void)_mkdir(folder);
#else
	(void)mkdir(folder, 0777);
#endif

	s_testDiscNodes = (struct NativeTestDiscNode *)calloc(NTD_MAX_NODES, sizeof(*s_testDiscNodes));
	s_testDiscImage = (unsigned char *)malloc((size_t)NTD_MAX_SECTORS * NTD_SECTOR_BYTES);
	s_testDiscDirBuffer = (unsigned char *)malloc(NTD_DIR_BUFFER_BYTES);
	if ((s_testDiscNodes == NULL) || (s_testDiscImage == NULL) || (s_testDiscDirBuffer == NULL))
	{
		NativeTestDisc_Fatal("out of memory");
	}

	printf("[selftest] test disc images into %s\n", folder);

	// good-basic: Form 1 files, an empty file, a Form 2 file, two directories.
	root = NativeTestDisc_NewRoot(1);
	NativeTestDisc_File(root, NTD_NAME("README.TXT;1"), s_testDiscNote);
	NativeTestDisc_File(root, NTD_NAME("EMPTY.TXT;1"), "");
	a = NativeTestDisc_Dir(root, NTD_NAME("DATA"));
	NativeTestDisc_File(a, NTD_NAME("NOTE.TXT;1"), s_testDiscNote);
	a = NativeTestDisc_Dir(root, NTD_NAME("XA"));
	NativeTestDisc_Form2File(a, NTD_NAME("MUSIC.XA;1"), 2);
	ok &= NativeTestDisc_WriteImage(folder, "good-basic", root, 0);

	// good-nested: three levels, and a directory of 60 entries that spans two
	// sectors (records never straddle one; the reader skips the padding).
	root = NativeTestDisc_NewRoot(1);
	a = NativeTestDisc_Dir(root, NTD_NAME("A"));
	b = NativeTestDisc_Dir(a, NTD_NAME("B"));
	b = NativeTestDisc_Dir(b, NTD_NAME("C"));
	NativeTestDisc_File(b, NTD_NAME("DEEP.TXT;1"), s_testDiscNote);
	a = NativeTestDisc_Dir(root, NTD_NAME("MANY"));
	for (i = 0; i < 60; i++)
	{
		snprintf(name, sizeof(name), "FILE%02d.TXT;1", i);
		NativeTestDisc_File(a, name, (unsigned)strlen(name), s_testDiscNote);
	}
	ok &= NativeTestDisc_WriteImage(folder, "good-nested", root, 0);

	// bad-dotdot: a directory named ".." (the two characters, not the 0x01
	// parent record) - its file would land beside the assets folder.
	root = NativeTestDisc_NewRoot(1);
	a = NativeTestDisc_Dir(root, NTD_NAME(".."));
	NativeTestDisc_File(a, NTD_NAME("SELFTEST_ESCAPED.TXT;1"), s_testDiscEscaped);
	ok &= NativeTestDisc_WriteImage(folder, "bad-dotdot", root, 0);

	// bad-dotdot-deep: "../.." - its file would land in <folder> itself.
	root = NativeTestDisc_NewRoot(1);
	a = NativeTestDisc_Dir(root, NTD_NAME(".."));
	a = NativeTestDisc_Dir(a, NTD_NAME(".."));
	NativeTestDisc_File(a, NTD_NAME("SELFTEST_ESCAPED.TXT;1"), s_testDiscEscaped);
	ok &= NativeTestDisc_WriteImage(folder, "bad-dotdot-deep", root, 0);

	// bad-dotdot-file: a file named "..;1".
	root = NativeTestDisc_NewRoot(1);
	NativeTestDisc_File(root, NTD_NAME("..;1"), s_testDiscEscaped);
	ok &= NativeTestDisc_WriteImage(folder, "bad-dotdot-file", root, 0);

	// bad-dot: a directory named ".".
	root = NativeTestDisc_NewRoot(1);
	a = NativeTestDisc_Dir(root, NTD_NAME("."));
	NativeTestDisc_File(a, NTD_NAME("FILE.TXT;1"), s_testDiscNote);
	ok &= NativeTestDisc_WriteImage(folder, "bad-dot", root, 0);

	// bad-dots: "..." - Windows folds trailing dots away.
	root = NativeTestDisc_NewRoot(1);
	a = NativeTestDisc_Dir(root, NTD_NAME("..."));
	NativeTestDisc_File(a, NTD_NAME("FILE.TXT;1"), s_testDiscNote);
	ok &= NativeTestDisc_WriteImage(folder, "bad-dots", root, 0);

	// bad-backslash: "\x" in the root.
	root = NativeTestDisc_NewRoot(1);
	NativeTestDisc_File(root, NTD_NAME("\\x"), s_testDiscEscaped);
	ok &= NativeTestDisc_WriteImage(folder, "bad-backslash", root, 0);

	// bad-backslash-escape: "..\..\SELFTEST_ESCAPED.TXT" one level down.
	root = NativeTestDisc_NewRoot(1);
	a = NativeTestDisc_Dir(root, NTD_NAME("D"));
	NativeTestDisc_File(a, NTD_NAME("..\\..\\SELFTEST_ESCAPED.TXT;1"), s_testDiscEscaped);
	ok &= NativeTestDisc_WriteImage(folder, "bad-backslash-escape", root, 0);

	// bad-slash: "a/.." in the root.
	root = NativeTestDisc_NewRoot(1);
	NativeTestDisc_File(root, NTD_NAME("a/.."), s_testDiscEscaped);
	ok &= NativeTestDisc_WriteImage(folder, "bad-slash", root, 0);

	// bad-slash-escape: "../../SELFTEST_ESCAPED.TXT" one level down.
	root = NativeTestDisc_NewRoot(1);
	a = NativeTestDisc_Dir(root, NTD_NAME("D"));
	NativeTestDisc_File(a, NTD_NAME("../../SELFTEST_ESCAPED.TXT;1"), s_testDiscEscaped);
	ok &= NativeTestDisc_WriteImage(folder, "bad-slash-escape", root, 0);

	// bad-drive: "C:x" - a drive-relative path, or a stream on Windows.
	root = NativeTestDisc_NewRoot(1);
	NativeTestDisc_File(root, NTD_NAME("C:x"), s_testDiscEscaped);
	ok &= NativeTestDisc_WriteImage(folder, "bad-drive", root, 0);

	// bad-absolute-drive: "C:\SELFTEST_ESCAPED.TXT".
	root = NativeTestDisc_NewRoot(1);
	NativeTestDisc_File(root, NTD_NAME("C:\\SELFTEST_ESCAPED.TXT;1"), s_testDiscEscaped);
	ok &= NativeTestDisc_WriteImage(folder, "bad-absolute-drive", root, 0);

	// bad-absolute-root: "/SELFTEST_ESCAPED.TXT" one level down.
	root = NativeTestDisc_NewRoot(1);
	a = NativeTestDisc_Dir(root, NTD_NAME("D"));
	NativeTestDisc_File(a, NTD_NAME("/SELFTEST_ESCAPED.TXT;1"), s_testDiscEscaped);
	ok &= NativeTestDisc_WriteImage(folder, "bad-absolute-root", root, 0);

	// bad-absolute-unc: "\\host\share\x".
	root = NativeTestDisc_NewRoot(1);
	NativeTestDisc_File(root, NTD_NAME("\\\\host\\share\\x"), s_testDiscEscaped);
	ok &= NativeTestDisc_WriteImage(folder, "bad-absolute-unc", root, 0);

	// bad-empty: a file record with a name of length 0.
	root = NativeTestDisc_NewRoot(1);
	NativeTestDisc_File(root, "", 0, s_testDiscEscaped);
	ok &= NativeTestDisc_WriteImage(folder, "bad-empty", root, 0);

	// bad-empty-space: a directory named " " - the reader trims trailing
	// spaces, so what is left is an empty name.
	root = NativeTestDisc_NewRoot(1);
	a = NativeTestDisc_Dir(root, NTD_NAME(" "));
	NativeTestDisc_File(a, NTD_NAME("FILE.TXT;1"), s_testDiscNote);
	ok &= NativeTestDisc_WriteImage(folder, "bad-empty-space", root, 0);

	// bad-nul: a NUL byte inside the name.
	root = NativeTestDisc_NewRoot(1);
	NativeTestDisc_File(root, NTD_NAME("A\0B.TXT;1"), s_testDiscEscaped);
	ok &= NativeTestDisc_WriteImage(folder, "bad-nul", root, 0);

	// bad-control: a control character inside the name.
	root = NativeTestDisc_NewRoot(1);
	NativeTestDisc_File(root, NTD_NAME("A\aB.TXT;1"), s_testDiscEscaped);
	ok &= NativeTestDisc_WriteImage(folder, "bad-control", root, 0);

	// bad-device: "NUL.TXT" is the NUL device in every Windows folder.
	root = NativeTestDisc_NewRoot(1);
	NativeTestDisc_File(root, NTD_NAME("NUL.TXT;1"), s_testDiscEscaped);
	ok &= NativeTestDisc_WriteImage(folder, "bad-device", root, 0);

	// bad-loop-root: sixteen directory records pointing back at the root. With
	// only a depth limit that is 16^9 paths - the walk would never end.
	root = NativeTestDisc_NewRoot(1);
	for (i = 0; i < 16; i++)
	{
		snprintf(name, sizeof(name), "LOOP%02d", i);
		NativeTestDisc_Alias(root, name, (unsigned)strlen(name), root);
	}
	ok &= NativeTestDisc_WriteImage(folder, "bad-loop-root", root, 0);

	// bad-loop-parent: A/B/UP points back at A.
	root = NativeTestDisc_NewRoot(1);
	a = NativeTestDisc_Dir(root, NTD_NAME("A"));
	b = NativeTestDisc_Dir(a, NTD_NAME("B"));
	NativeTestDisc_File(b, NTD_NAME("FILE.TXT;1"), s_testDiscNote);
	NativeTestDisc_Alias(b, NTD_NAME("UP"), a);
	ok &= NativeTestDisc_WriteImage(folder, "bad-loop-parent", root, 0);

	// bad-loop-self: A/SELF points at A itself.
	root = NativeTestDisc_NewRoot(1);
	a = NativeTestDisc_Dir(root, NTD_NAME("A"));
	NativeTestDisc_Alias(a, NTD_NAME("SELF"), a);
	ok &= NativeTestDisc_WriteImage(folder, "bad-loop-self", root, 0);

	// bad-deep: ten nested directories, deeper than any disc goes.
	root = NativeTestDisc_NewRoot(1);
	a = root;
	for (i = 0; i < 10; i++)
	{
		snprintf(name, sizeof(name), "L%d", i);
		a = NativeTestDisc_Dir(a, name, (unsigned)strlen(name));
	}
	NativeTestDisc_File(a, NTD_NAME("FILE.TXT;1"), s_testDiscNote);
	ok &= NativeTestDisc_WriteImage(folder, "bad-deep", root, 0);

	// bad-huge-dir: a directory record claiming almost 2 GB.
	root = NativeTestDisc_NewRoot(1);
	a = NativeTestDisc_Dir(root, NTD_NAME("HUGE"));
	a->claimSize = 0x7fff0000u;
	ok &= NativeTestDisc_WriteImage(folder, "bad-huge-dir", root, 0);

	// bad-broken-record: a record whose length byte is below the 34-byte
	// minimum, after the boot files (so the serial check still passes).
	root = NativeTestDisc_NewRoot(1);
	a = NativeTestDisc_File(root, NTD_NAME("BROKEN.TXT;1"), s_testDiscNote);
	a->brokenLength = 20;
	ok &= NativeTestDisc_WriteImage(folder, "bad-broken-record", root, 0);

	// bad-wrong-serial: a PAL boot record. Not a path case - the identity
	// check the first start runs before it walks anything.
	root = NativeTestDisc_NewRoot(0);
	NativeTestDisc_File(root, NTD_NAME("SYSTEM.CNF;1"), s_testDiscWrongCnf);
	NativeTestDisc_File(root, NTD_NAME("README.TXT;1"), s_testDiscNote);
	ok &= NativeTestDisc_WriteImage(folder, "bad-wrong-serial", root, 0);

	// bad-no-boot: no SYSTEM.CNF at all.
	root = NativeTestDisc_NewRoot(0);
	NativeTestDisc_File(root, NTD_NAME("README.TXT;1"), s_testDiscNote);
	ok &= NativeTestDisc_WriteImage(folder, "bad-no-boot", root, 0);

	// bad-truncated: good-basic cut after the volume descriptors.
	root = NativeTestDisc_NewRoot(1);
	NativeTestDisc_File(root, NTD_NAME("README.TXT;1"), s_testDiscNote);
	ok &= NativeTestDisc_WriteImage(folder, "bad-truncated", root, NTD_FIRST_FREE_LBA);

	// bad-not-an-image: only the first sector, no volume descriptor.
	root = NativeTestDisc_NewRoot(1);
	ok &= NativeTestDisc_WriteImage(folder, "bad-not-an-image", root, 1);

	free(s_testDiscNodes);
	free(s_testDiscImage);
	free(s_testDiscDirBuffer);
	s_testDiscNodes = NULL;
	s_testDiscImage = NULL;
	s_testDiscDirBuffer = NULL;

	printf("[selftest] test disc images %s\n", ok ? "written" : "NOT all written");
	return ok ? 0 : 1;
}
