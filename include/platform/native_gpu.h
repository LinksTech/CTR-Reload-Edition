/*
 * Derived from REDRIVER2/PsyCross MIT source:
 * externals/PsyCross/src/gpu/PsyX_GPU.h
 * See THIRD_PARTY_NOTICES.md for copyright and license details.
 */

#ifndef NATIVE_GPU_H
#define NATIVE_GPU_H

#include <macros.h>
#include <psx/libgte.h>
#include <psx/libgpu.h>

extern DISPENV activeDispEnv;
extern DRAWENV activeDrawEnv;
extern int g_GPUDisabledState;

int NativeGpu_HasPendingSplits(void);
void ClearSplits(void);
void DrawAllSplits(void);
void NativeGpu_PrintSplitReport(void);
void NativeGpu_EndFrameSizeCensus(void);
void NativeGpu_PrintSizeReport(void);
void ParsePrimitivesLinkedList(u32 *p, int singlePrimitive);
int NativeGpu_GetStateSize(void);
int NativeGpu_CaptureState(void *dst, int dstSize);
int NativeGpu_RestoreState(const void *src, int srcSize);

struct NativeGpuMarkerCounts
{
	unsigned long long parsed;       // 0xB3 packets seen by the parser
	unsigned long long splits;       // native splits created
	unsigned long long refused;      // packets that found the split table full (no split, no cut)
	unsigned long long drawCalls;    // native splits reached in DrawSplit
	unsigned long long afterNative;  // PSX splits started only because the one before was native
};
void NativeGpu_MarkerCounts(struct NativeGpuMarkerCounts *out);

#endif
