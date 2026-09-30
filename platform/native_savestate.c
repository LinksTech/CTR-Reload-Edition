#include "platform/native_savestate.h"

#include <macros.h>

#if defined(CTR_INTERNAL)
#include "platform/native_checkpoint.h"
#include "platform/native_checkpoint_file.h"
#include "platform/native_log.h"
#include "ctr_build_id.h" // CTR_NATIVE_BUILD_ID, generated on every build

#include <errno.h>
#include <stdlib.h>

#if defined(_WIN32)
#include <direct.h>
#include <sys/stat.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#define NATIVE_SAVESTATE_ROOT       "debug"
#define NATIVE_SAVESTATE_DIR        "debug/states"
#define NATIVE_SAVESTATE_QUICK_PATH "debug/states/quick.ctrstates"

// A quick state can only be loaded in the build that wrote it. It depends on
// code addresses (NativeCheckpoint_RelocateImagePointerSlot in
// native_checkpoint.c); loaded from another build, the game crashes. The id is FNV-1a over
// CTR_NATIVE_BUILD_ID and the link timestamp of the exe - that changes with
// every link, even if only game code was recompiled.
internal u32 NativeSaveState_BuildTag(void)
{
	u32 hash = 2166136261u;
	const char *id = CTR_NATIVE_BUILD_ID;
	u32 stamp = 0;

#if defined(_WIN32)
	{
		const u8 *image = (const u8 *)GetModuleHandleA(NULL);

		if (image != NULL)
		{
			const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)image;
			const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(image + dos->e_lfanew);

			stamp = (u32)nt->FileHeader.TimeDateStamp;
		}
	}
#endif

	for (; *id != '\0'; id++)
	{
		hash = (hash ^ (u8)*id) * 16777619u;
	}

	for (int i = 0; i < 4; i++)
	{
		hash = (hash ^ (u8)(stamp >> (i * 8))) * 16777619u;
	}

	// 0 stands for "without id" (old states, replays)
	return (hash != 0u) ? hash : 1u;
}

global_variable s32 s_quickSaveRequested;
global_variable s32 s_quickLoadRequested;
global_variable u8 *s_quickPayload;
global_variable int s_quickPayloadSize;

internal s32 NativeSaveState_PathExists(const char *path)
{
	struct stat st;

	if (path == NULL)
	{
		return 0;
	}

	return stat(path, &st) == 0;
}

internal s32 NativeSaveState_MakeDir(const char *path)
{
	if ((path == NULL) || (path[0] == '\0'))
	{
		return 0;
	}

#if defined(_WIN32)
	if (_mkdir(path) == 0)
#else
	if (mkdir(path, 0755) == 0)
#endif
	{
		return 1;
	}

	return (errno == EEXIST) && NativeSaveState_PathExists(path);
}

internal s32 NativeSaveState_PrepareDir(void)
{
	return NativeSaveState_MakeDir(NATIVE_SAVESTATE_ROOT) && NativeSaveState_MakeDir(NATIVE_SAVESTATE_DIR);
}

internal s32 NativeSaveState_PreparePayload(void)
{
	const int payloadSize = NativeCheckpoint_GetSize();

	if (payloadSize <= 0)
	{
		Platform_Log("[CTR State] invalid quick state size: %d\n", payloadSize);
		return 0;
	}

	if (payloadSize != s_quickPayloadSize)
	{
		u8 *payload = (u8 *)realloc(s_quickPayload, (size_t)payloadSize);

		if (payload == NULL)
		{
			Platform_Log("[CTR State] failed to allocate quick state buffer: %d bytes\n", payloadSize);
			return 0;
		}

		s_quickPayload = payload;
		s_quickPayloadSize = payloadSize;
	}

	return 1;
}

internal s32 NativeSaveState_SaveQuick(void)
{
	if (!NativeSaveState_PrepareDir())
	{
		Platform_Log("[CTR State] failed to create quick state directory: %s\n", NATIVE_SAVESTATE_DIR);
		return 0;
	}

	if (!NativeSaveState_PreparePayload())
	{
		return 0;
	}

	if (!NativeCheckpoint_Capture(s_quickPayload, s_quickPayloadSize))
	{
		Platform_Log("[CTR State] failed to capture quick state\n");
		return 0;
	}

	if (!NativeCheckpointFile_WriteSingle(NATIVE_SAVESTATE_QUICK_PATH, s_quickPayload, s_quickPayloadSize, 0, 0, NativeSaveState_BuildTag()))
	{
		Platform_Log("[CTR State] failed to write quick state: %s\n", NATIVE_SAVESTATE_QUICK_PATH);
		return 0;
	}

	Platform_Log("[CTR State] saved quick state: %s\n", NATIVE_SAVESTATE_QUICK_PATH);
	return 1;
}

internal s32 NativeSaveState_LoadQuick(void)
{
	struct NativeCheckpointFileRecordInfo info;

	if (!NativeSaveState_PreparePayload())
	{
		return 0;
	}

	if (!NativeCheckpointFile_ReadSingle(NATIVE_SAVESTATE_QUICK_PATH, s_quickPayload, s_quickPayloadSize, &info))
	{
		Platform_Log("[CTR State] failed to read quick state: %s\n", NATIVE_SAVESTATE_QUICK_PATH);
		return 0;
	}

	if (info.buildTag != NativeSaveState_BuildTag())
	{
		Platform_Log("[CTR State] quick state %s is from another build (tag 0x%08x, this build 0x%08x, %s) - not loaded\n", NATIVE_SAVESTATE_QUICK_PATH,
		             info.buildTag, NativeSaveState_BuildTag(), CTR_NATIVE_BUILD_ID);
		return 0;
	}

	if (!NativeCheckpoint_Restore(s_quickPayload, s_quickPayloadSize))
	{
		Platform_Log("[CTR State] failed to restore quick state: %s\n", NATIVE_SAVESTATE_QUICK_PATH);
		return 0;
	}

	Platform_Log("[CTR State] loaded quick state checksum=0x%08x: %s\n", info.checksum, NATIVE_SAVESTATE_QUICK_PATH);
	return 1;
}

void NativeSaveState_RequestSave(void)
{
	if (s_quickSaveRequested == 0)
	{
		s_quickSaveRequested = 1;
		Platform_Log("[CTR State] quick save requested\n");
	}
}

void NativeSaveState_RequestLoad(void)
{
	if (s_quickLoadRequested == 0)
	{
		s_quickLoadRequested = 1;
		Platform_Log("[CTR State] quick load requested\n");
	}
}

void NativeSaveState_BeginFrame(void)
{
	if (s_quickSaveRequested != 0)
	{
		s_quickSaveRequested = 0;
		NativeSaveState_SaveQuick();
	}

	if (s_quickLoadRequested != 0)
	{
		s_quickLoadRequested = 0;
		NativeSaveState_LoadQuick();
	}
}
#endif
