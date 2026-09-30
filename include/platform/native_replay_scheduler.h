#ifndef PLATFORM_NATIVE_REPLAY_SCHEDULER_H
#define PLATFORM_NATIVE_REPLAY_SCHEDULER_H

#include <macros.h>

#if defined(CTR_INTERNAL)
struct NativeReplaySchedulerFrameInfo
{
	s32 frameTimer;
	s32 frameCounter;
	s32 timer;
	s32 framesInThisLEV;
	s32 elapsedTimeMS;
	s32 msInThisLEV;
	s32 elapsedEventTime;
	s32 mainGameState;
	s32 loadingStage;
	s32 levelID;
	u32 mixRandomNumber;
	u32 audioRNG;
	u32 deadcoed0;
	u32 deadcoed1;
	u32 advRng0;
	u32 advRng1;
};

int NativeReplayScheduler_PrepareReportFromArgs(int argc, char **argv);
int NativeReplayScheduler_ConfigureFromArgs(int argc, char **argv);
void NativeReplayScheduler_Shutdown(void);
int NativeReplayScheduler_RequestStart(void);
int NativeReplayScheduler_RequestStop(void);
int NativeReplayScheduler_BeginFrame(const struct NativeReplaySchedulerFrameInfo *info);
int NativeReplayScheduler_ConsumeVSyncPacket(int requestedVBlanks, int *emittedVBlanks);
int NativeReplayScheduler_ConsumeFrameElapsedTimeMS(int *elapsedTimeMS);
int NativeReplayScheduler_EndFrame(const struct NativeReplaySchedulerFrameInfo *info);
void NativeReplayScheduler_RecordVSyncPacket(int emittedVBlanks);

// 1 while a replay is prepared, recording or playing. Then
// nothing outside the input may change the game flow - such as the pause on
// minimise or focus loss (native_platform.c, Platform_TakeFocusPauseWish).
int NativeReplayScheduler_IsActive(void);
#endif

#endif
