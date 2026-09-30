#include "platform/native_state.h"

#include <macros.h>

#include "platform/native_audio.h"
#include "platform/native_gpu.h"
#include "platform/native_input.h"

#include <string.h>

#define NATIVE_STATE_FOURCC(a, b, c, d) ((u32)(a) | ((u32)(b) << 8) | ((u32)(c) << 16) | ((u32)(d) << 24))

// NOTE(aalhendi): Snapshot tags are stored as little-endian four-character IDs
// in native replay/state blobs. `CTRS` means CTR native State bundle.
#define NATIVE_STATE_MAGIC              NATIVE_STATE_FOURCC('C', 'T', 'R', 'S')
// 2 (2026-09-29): region HSPU, the 32-bit SPU addresses of the game logic
// (HOWL_Bank.c, 1 MB sound memory). States only load in the same build anyway.
#define NATIVE_STATE_VERSION            2u

// HOWL_Bank.c
int Howl_SpuState_GetSize(void);
int Howl_SpuState_Capture(void *dst, int dstSize);
int Howl_SpuState_Restore(const void *src, int srcSize);

enum NativeStateRegionKind
{
	NATIVE_STATE_REGION_INPUT = NATIVE_STATE_FOURCC('I', 'N', 'P', 'T'), // input snapshot region
	NATIVE_STATE_REGION_AUDIO = NATIVE_STATE_FOURCC('A', 'U', 'D', 'O'), // audio/SPU/XA snapshot region
	NATIVE_STATE_REGION_GPU = NATIVE_STATE_FOURCC('G', 'P', 'U', ' '),   // GPU/VRAM snapshot region
	NATIVE_STATE_REGION_HSPU = NATIVE_STATE_FOURCC('H', 'S', 'P', 'U'),  // 32-bit SPU addresses of the game's sound banks
};

struct NativeStateRegion
{
	u32 kind;
	u32 offset;
	u32 size;
};

struct NativeStateHeader
{
	u32 magic;
	u32 version;
	u32 size;
	u32 regionCount;
	struct NativeStateRegion regions[4];
};

internal u32 NativeState_Align4(u32 value)
{
	return (value + 3u) & ~3u;
}

internal int NativeState_GetRegionSize(u32 kind)
{
	switch (kind)
	{
	case NATIVE_STATE_REGION_INPUT:
		return Platform_InputGetStateSize();
	case NATIVE_STATE_REGION_AUDIO:
		return NativeAudio_GetStateSize();
	case NATIVE_STATE_REGION_GPU:
		return NativeGpu_GetStateSize();
	case NATIVE_STATE_REGION_HSPU:
		return Howl_SpuState_GetSize();
	}

	return 0;
}

internal int NativeState_CaptureRegion(u32 kind, void *dst, int dstSize)
{
	switch (kind)
	{
	case NATIVE_STATE_REGION_INPUT:
		return Platform_InputCaptureState(dst, dstSize);
	case NATIVE_STATE_REGION_AUDIO:
		return NativeAudio_CaptureState(dst, dstSize);
	case NATIVE_STATE_REGION_GPU:
		return NativeGpu_CaptureState(dst, dstSize);
	case NATIVE_STATE_REGION_HSPU:
		return Howl_SpuState_Capture(dst, dstSize);
	}

	return 0;
}

internal int NativeState_RestoreRegion(u32 kind, const void *src, int srcSize)
{
	switch (kind)
	{
	case NATIVE_STATE_REGION_INPUT:
		return Platform_InputRestoreState(src, srcSize);
	case NATIVE_STATE_REGION_AUDIO:
		return NativeAudio_RestoreState(src, srcSize);
	case NATIVE_STATE_REGION_GPU:
		return NativeGpu_RestoreState(src, srcSize);
	case NATIVE_STATE_REGION_HSPU:
		return Howl_SpuState_Restore(src, srcSize);
	}

	return 0;
}

internal int NativeState_InitHeader(struct NativeStateHeader *header)
{
	u32 offset = NativeState_Align4((u32)sizeof(*header));
	u32 regionCount = 0;
	local_persist const u32 regionKinds[] = {
	    NATIVE_STATE_REGION_INPUT,
	    NATIVE_STATE_REGION_AUDIO,
	    NATIVE_STATE_REGION_GPU,
	    NATIVE_STATE_REGION_HSPU,
	};

	memset(header, 0, sizeof(*header));
	header->magic = NATIVE_STATE_MAGIC;
	header->version = NATIVE_STATE_VERSION;
	header->regionCount = (u32)len(regionKinds);

	for (regionCount = 0; regionCount < header->regionCount; regionCount++)
	{
		const int regionSize = NativeState_GetRegionSize(regionKinds[regionCount]);

		if (regionSize <= 0)
		{
			return 0;
		}

		header->regions[regionCount].kind = regionKinds[regionCount];
		header->regions[regionCount].offset = offset;
		header->regions[regionCount].size = (u32)regionSize;
		offset = NativeState_Align4(offset + (u32)regionSize);
	}

	header->size = offset;
	return 1;
}

int NativeState_GetSize(void)
{
	struct NativeStateHeader header;

	if (!NativeState_InitHeader(&header))
	{
		return 0;
	}

	return (int)header.size;
}

int NativeState_Capture(void *dst, int dstSize)
{
	struct NativeStateHeader header;
	u8 *bytes = (u8 *)dst;

	if (!NativeState_InitHeader(&header))
	{
		return 0;
	}
	if ((dst == NULL) || (dstSize < (int)header.size))
	{
		return 0;
	}

	memset(dst, 0, header.size);
	memcpy(dst, &header, sizeof(header));

	for (u32 i = 0; i < header.regionCount; i++)
	{
		struct NativeStateRegion *region = &header.regions[i];

		if (!NativeState_CaptureRegion(region->kind, &bytes[region->offset], (int)region->size))
		{
			return 0;
		}
	}

	return 1;
}

int NativeState_Restore(const void *src, int srcSize)
{
	const struct NativeStateHeader *header = (const struct NativeStateHeader *)src;
	const u8 *bytes = (const u8 *)src;
	u32 i;

	if ((src == NULL) || (srcSize < (int)sizeof(*header)))
	{
		return 0;
	}
	if ((header->magic != NATIVE_STATE_MAGIC) || (header->version != NATIVE_STATE_VERSION))
	{
		return 0;
	}
	if ((header->size < sizeof(*header)) || (header->size > (u32)srcSize))
	{
		return 0;
	}
	if (header->regionCount > len(header->regions))
	{
		return 0;
	}

	for (i = 0; i < header->regionCount; i++)
	{
		const struct NativeStateRegion *region = &header->regions[i];
		const u32 end = region->offset + region->size;

		if ((region->size == 0) || (region->offset < sizeof(*header)) || (end < region->offset) || (end > header->size))
		{
			return 0;
		}
		if (NativeState_GetRegionSize(region->kind) != (int)region->size)
		{
			return 0;
		}
	}

	// NOTE(aalhendi): Restore in the same stable order as capture: input first
	// so later deterministic replay code can install frame input immediately
	// after loading a whole-machine checkpoint; audio/GPU rebuild their host
	// output from PSX-visible state.
	for (i = 0; i < header->regionCount; i++)
	{
		const struct NativeStateRegion *region = &header->regions[i];

		if (!NativeState_RestoreRegion(region->kind, &bytes[region->offset], (int)region->size))
		{
			return 0;
		}
	}

	return 1;
}
