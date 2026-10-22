// rldinstances.h - how many placed objects a track may have, for the game and the tools
//
// ONE NUMBER, TWO SIDES. The game sizes its instance pool from it
// (MainInit_JitPoolsNew, game/MAIN/MainInit.c), the native render layer its
// per-slot tables (platform/native_render_layer.c), and the container check
// (Rld_CheckLevTables, include/rldtrack.inc) refuses a LEV above it - in the
// packer and Reload Studio (tools/rldpack.c) as in the game when it loads a
// container. Change it here and nowhere else.
//
// WHERE THE OLD 110 CAME FROM. On the disc a race has one instance pool of
// 0x1000 >> 5 = 128 slots in the pack (MainInit.c). The 8 drivers and 2 HUD
// instances take 10 of them before the level's InstDefs, the wake 8 more if
// the LEV carries a wake model: 110 placed objects, and during the race up to
// 21 crate explosions and weapons, as measured, come on top - so from 90 on
// fewer than 28 remained. That is a PS1 memory limit, not one of the LEV
// format: Level.numInstances is a u32, the InstDef table is numInstances *
// 0x40 bytes, and the visibility lists (PVS visInstSrc) are NULL-terminated
// pointer lists.
//
// THE LARGE POOL. A level with more than RLD_LEVEL_INSTANCES_RETAIL placed
// objects gets RLD_INSTANCE_POOL_LARGE instance slots: RLD_LEVEL_INSTANCES_MAX
// for the level, and the whole retail pool on top as the reserve for drivers,
// HUD, wake, weapons and effects - never less than the disc left them. A level
// with up to RLD_LEVEL_INSTANCES_RETAIL objects (every disc track) keeps the
// retail pool as before.
//
// Plain int constants: the game compares them with int slot numbers.
#ifndef RLD_INSTANCES_H
#define RLD_INSTANCES_H

// The most placed objects (LEV InstDefs) a track may have.
#define RLD_LEVEL_INSTANCES_MAX 512

// The race instance pool on the disc: 0x1000 >> 5 (MainInit_JitPoolsNew).
#define RLD_INSTANCE_POOL_RETAIL 128

// Up to this many placed objects the retail pool keeps room for the drivers,
// the HUD, the wake and what the race births; above it a level gets the large pool.
#define RLD_LEVEL_INSTANCES_RETAIL 90

// Instance slots of the large pool: the level's objects plus the retail pool as reserve.
#define RLD_INSTANCE_POOL_LARGE (RLD_LEVEL_INSTANCES_MAX + RLD_INSTANCE_POOL_RETAIL)

// A number above as text, for a message that is a string literal:
// RLD_INSTANCES_TEXT(RLD_LEVEL_INSTANCES_MAX) is "512".
#define RLD_INSTANCES_TEXT_DIGITS(n) #n
#define RLD_INSTANCES_TEXT(n)        RLD_INSTANCES_TEXT_DIGITS(n)

#endif
