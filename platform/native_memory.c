#include <platform.h>
#include <ctr_subpixel.h>
#include "ctr_scratchpad.h"
#include "platform/native_memory.h"
#if defined(CTR_INTERNAL)
#include "platform/native_checkpoint.h"
#endif

#include <common.h>
#include <macros.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Native uses the NTSC-U 926 mempack window inside the retail 2 MiB address
// space so memory regressions fail here as they would on PSX.
#define CTR_NATIVE_MEMPACK_BUFFER_SIZE  0x200000u
#define CTR_NATIVE_MEMPACK_START_OFFSET 0xba9f0u
#define CTR_NATIVE_MEMPACK_SIZE         0x144e10u

CTR_STATIC_ASSERT(CTR_NATIVE_MEMPACK_START_OFFSET + CTR_NATIVE_MEMPACK_SIZE + MEMPACK_PS1_END_GUARD_SIZE == CTR_NATIVE_MEMPACK_BUFFER_SIZE);

// AND A PIECE BEHIND IT THAT ONLY CONTAINER TRACKS GET.
//
// The window above is the console: 1,331,216 bytes, at the same place in the
// 2 MiB address space as on the PSX, so that a memory bug shows up here as it
// does there. That stays so, byte for byte - a disc track sees nothing
// of this.
//
// A foreign track is not a PSX-faithful measurement. It may be larger than
// the one whose slot it occupies, and on 2026-08-28 it was: the sky of the
// test data needs 172,032 bytes of draw memory, where Dingo Canyon gets 97,280 for
// the whole frame.
//
// The buffer carries the room from the start - an array costs nothing as long as
// nobody writes into it. The WINDOW grows only when --tracks asks for it,
// and then the number is in the log. Without the switch every pointer and every
// limit is the same as before.
// 16 MiB. Before, this said one megabyte, and that was a fixed number that
// clamped SILENTLY.
//
// We are not a port. The PSX had 2 MB, this machine has 32 GB. A
// limit that only exists because hardware from 1999 could not do more is
// no yardstick here - the window above is, and that stays byte for byte
// the console, so that a memory bug shows up as it does there.
//
// What lies BEHIND it depends on the track. The largest test file
// asks for 5,991,028 bytes; 16 MiB is two and a half times that.
//
// 32 MiB since 2026-09-15: the reserve now also carries the
// near-plane clip buffers, four per track (NativeTrack_MempackExtraNeeded).
// Sunset Vista asked for 8,582,284 bytes before (log 'tracks/: ... wanted');
// 13,120 quadblocks x 4 sets x 0x3c bytes x 4 buffers add 12,595,200,
// together 21,177,484 - over 16 MiB, under 32.
//
// An upper limit remains anyway, because this is an array. But it says
// so now when it kicks in - see Platform_SetMempackExtra. A limit that
// clamps silently is a bug, and this one was one.
#define CTR_NATIVE_MEMPACK_EXTRA_MAX 0x2000000u

union NativeScratchpadStorage
{
	u8 bytes[CTR_SCRATCHPAD_SIZE];
	u32 words[CTR_SCRATCHPAD_SIZE / sizeof(u32)];
};

CTR_STATIC_ASSERT(sizeof(union NativeScratchpadStorage) == CTR_SCRATCHPAD_SIZE);

global_variable char s_mempackMemory[CTR_NATIVE_MEMPACK_BUFFER_SIZE + CTR_NATIVE_MEMPACK_EXTRA_MAX];
global_variable u32 s_mempackExtra = 0;
global_variable struct PlatformMempackArena s_mempackArena;
global_variable union NativeScratchpadStorage s_scratchpadMemory;
u8 *gCTRNativeScratchpadBase;

void Platform_InitScratchpad(void)
{
#if defined(CTR_NATIVE)
	gCTRNativeScratchpadBase = &s_scratchpadMemory.bytes[0];
	memset(&s_scratchpadMemory, 0, sizeof(s_scratchpadMemory));

	// The track renderer projects into scratch vertices that live here, and
	// only afterwards packs them into the primitive. Without this region the
	// track - the largest part of the frame - carries no fractional bits.
	NativeSubpixel_RegisterRegion(gCTRNativeScratchpadBase, CTR_SCRATCHPAD_SIZE, "scratchpad");
#endif
}

void Platform_SetMempackExtra(u32 bytes)
{
	const u32 wanted = bytes;

	if (bytes > CTR_NATIVE_MEMPACK_EXTRA_MAX)
	{
		bytes = CTR_NATIVE_MEMPACK_EXTRA_MAX;

		// LOUD, and not as one line among many. A fixed number used to
		// clamp silently here, and whatever was then missing looked in the frame like a
		// renderer bug.
		Platform_LogError("[CTR MEMPACK] a track asks for %u bytes behind the window, the ceiling is %u\n", wanted, CTR_NATIVE_MEMPACK_EXTRA_MAX);
		Platform_LogError("[CTR MEMPACK] clamped. That track will be missing geometry - raise CTR_NATIVE_MEMPACK_EXTRA_MAX\n");
	}

	// Rounded to four, so every boundary in the pack stays word-aligned.
	s_mempackExtra = bytes & ~3u;
}

u32 Platform_GetMempackExtra(void)
{
	return s_mempackExtra;
}

void Platform_ConfigureMempackArena(void)
{
	const u32 size = CTR_NATIVE_MEMPACK_SIZE + s_mempackExtra;

	s_mempackArena.base = &s_mempackMemory[0];
	s_mempackArena.start = &s_mempackMemory[CTR_NATIVE_MEMPACK_START_OFFSET];

	// Computed from start, size and guard, not from the buffer size.
	//
	// Before, this said &s_mempackMemory[CTR_NATIVE_MEMPACK_BUFFER_SIZE], which
	// was the same - the assertion above says so. But as soon as the buffer carries
	// room for the extra, it would no longer be the same, and the limit would
	// slide back by a megabyte even without --tracks. Exactly the mistake
	// this tree keeps making: the same number in two places.
	s_mempackArena.endOfMemory = &s_mempackMemory[CTR_NATIVE_MEMPACK_START_OFFSET + size + MEMPACK_PS1_END_GUARD_SIZE];
	s_mempackArena.size = (int)size;
	s_mempackArena.backingSize = (int)(CTR_NATIVE_MEMPACK_START_OFFSET + size + MEMPACK_PS1_END_GUARD_SIZE);
}

const struct PlatformMempackArena *Platform_InitMempackArena(void)
{
	// First the layout, then zeroing - and only as far as the pack reaches.
	//
	// Before, the memset ran over the WHOLE array. As long as that carried one
	// megabyte of reserve it did not matter; with sixteen it would be sixteen
	// megabytes touched on every level change, only for nobody to
	// use them.
	Platform_ConfigureMempackArena();
	memset(s_mempackMemory, 0, (size_t)s_mempackArena.backingSize);
#if defined(CTR_INTERNAL)
	NativeCheckpoint_OnMempackArenaReset();
#endif

	return &s_mempackArena;
}

const struct PlatformMempackArena *Platform_GetMempackArena(void)
{
	return &s_mempackArena;
}

void *Platform_GetMempackBacking(void)
{
	return &s_mempackMemory[0];
}

int Platform_GetMempackBackingSize(void)
{
	// The WINDOW, not the array.
	//
	// This used to say sizeof(s_mempackMemory), and that was the same as long as the
	// array was exactly the window. Since it carries room for the extra, it
	// no longer is - and a checkpoint would suddenly have saved one megabyte
	// more than before, even without --tracks. The same mistake as with endOfMemory,
	// two functions further up.
	return s_mempackArena.backingSize;
}

void Platform_RepairResidentPointers(s32 activeMempackIndex)
{
	if ((activeMempackIndex < 0) || (activeMempackIndex >= 4))
	{
		activeMempackIndex = 0;
	}

	// NOTE(aalhendi): Native keeps retail-shaped global data, but pointer aliases
	// must target this process's static storage. This also moves GCC's
	// initializer-only memcard helper global out of the live state graph so
	// checkpoints capture the actual memcard buffer.
	sdata = &sdata_static;
	sdata_static.gGT = &sdata_static.gameTracker;
	sdata_static.gGamepads = &sdata_static.gamepadSystem;
	sdata_static.PtrMempack = &sdata_static.mempack[activeMempackIndex];
	sdata_static.ptrToMemcardBuffer1 = &sdata_static.memcardBytes[0];
	sdata_static.ptrToMemcardBuffer2 = &sdata_static.memcardBytes[0];
}
