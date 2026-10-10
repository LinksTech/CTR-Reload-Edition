#ifndef NATIVE_PERF_H
#define NATIVE_PERF_H

#include <macros.h>

enum NativePerfBucket
{
	NATIVE_PERF_BUCKET_GAME_LOGIC,
	NATIVE_PERF_BUCKET_RENDER_FRAME,
	NATIVE_PERF_BUCKET_MAINFRAME_SETUP,
	NATIVE_PERF_BUCKET_MAINFRAME_EFFECTS,
	NATIVE_PERF_BUCKET_MAINFRAME_HUD,
	NATIVE_PERF_BUCKET_MAINFRAME_QUEUE_INSTANCES,
	NATIVE_PERF_BUCKET_MAINFRAME_EXECUTE_INSTANCES,
	NATIVE_PERF_BUCKET_MAINFRAME_LEVEL_GEOMETRY,
	NATIVE_PERF_BUCKET_MAINFRAME_POST_LEVEL,
	NATIVE_PERF_BUCKET_MAINFRAME_REFRESHCARD,
	NATIVE_PERF_BUCKET_MAINFRAME_CLEAR_SCREEN,
	NATIVE_PERF_BUCKET_MAINFRAME_UI,
	NATIVE_PERF_BUCKET_MAINFRAME_RENDER_VSYNC,
	NATIVE_PERF_BUCKET_PLATFORM_END_FRAME,
	NATIVE_PERF_BUCKET_PLATFORM_END_SCENE,
	NATIVE_PERF_BUCKET_RENDER_SUBMIT,
	NATIVE_PERF_BUCKET_PLATFORM_BEGIN_SCENE,
	NATIVE_PERF_BUCKET_DRAW_OTAG,
	NATIVE_PERF_BUCKET_DRAW_OTAG_PARSE,
	NATIVE_PERF_BUCKET_DRAW_ALL_SPLITS,
	NATIVE_PERF_BUCKET_RENDERER_BEGIN_SCENE,
	NATIVE_PERF_BUCKET_RENDERER_UPDATE_VRAM,
	NATIVE_PERF_BUCKET_RENDERER_VERTEX_UPLOAD,
	NATIVE_PERF_BUCKET_RENDERER_DRAW_TRIANGLES,
	NATIVE_PERF_BUCKET_FRAMEBUFFER_STORE,
	NATIVE_PERF_BUCKET_FRAMEBUFFER_READBACK,
	NATIVE_PERF_BUCKET_SWAP_WINDOW,
	NATIVE_PERF_BUCKET_VSYNC_WAIT,
	NATIVE_PERF_BUCKET_AUDIO_VBLANK,

	// The native driver path (a custom character drawn natively,
	// platform/native_render_layer.c), per frame. Each one is a part of a
	// bucket above: the pull lies in the instance queue, the draw in
	// draw_all_splits_ms. The upload and bind scopes are recorded by the
	// device backend for every draw, and the recorder only keeps them while
	// the draw scope is open (the gate in s_bucketInfo) - so they stay the
	// driver path's share, not the frame's.
	NATIVE_PERF_BUCKET_NATIVE_CHAR_PULL,
	NATIVE_PERF_BUCKET_NATIVE_CHAR_WHEELS,
	NATIVE_PERF_BUCKET_NATIVE_CHAR_UPLOAD,
	NATIVE_PERF_BUCKET_NATIVE_CHAR_BIND,
	NATIVE_PERF_BUCKET_NATIVE_CHAR_DRAW,
	NATIVE_PERF_BUCKET_NATIVE_CHAR_VIEWS,

	// Everything the device backend does before a draw command, for every
	// draw of the frame (pass, pipeline, uniform placement, descriptors,
	// binds): the retail draws' side of the same work, so the two compare.
	NATIVE_PERF_BUCKET_VK_DRAW_SETUP,
	NATIVE_PERF_BUCKET_COUNT
};

// Counts per frame, columns of frame_times.csv behind the buckets and summed
// in summary.txt. The char_ counters are gated like the scopes above: only
// what happens while the driver draw scope is open is counted (char_views is
// counted by the render layer itself and has no gate).
enum NativePerfCounter
{
	NATIVE_PERF_COUNT_DRAW_CALLS,        // vkCmdDraw and vkCmdDrawIndexed recorded in the frame
	NATIVE_PERF_COUNT_CHAR_DRAW_CALLS,   // of these, the native driver path (body and wheels, every view)
	NATIVE_PERF_COUNT_CHAR_VIEWS,        // views (screens) with a native driver drawn in the frame
	NATIVE_PERF_COUNT_UPLOAD_BYTES,      // bytes copied host to device in the frame: textures, buffers, uniform blocks
	NATIVE_PERF_COUNT_CHAR_UPLOAD_BYTES, // of these, the native driver path
	NATIVE_PERF_COUNT_CHAR_VERTICES,     // vertices (indices of an indexed draw) the native driver path drew
	NATIVE_PERF_COUNT_CHAR_ITEMS,        // char items drawn (the body of a view, every view)
	NATIVE_PERF_COUNT_CHAR_MIRROR_ITEMS, // of the second items of a view: the mirror, the side below the water line
	NATIVE_PERF_COUNT_COUNT
};

struct NativePerfFrameInfo
{
	s32 frameCounter;
	s32 timer;
	s32 levelID;
	s32 gameMode1;
	s32 loadingStage;
	s32 boolDemoMode;
	s32 numPlyrCurrGame;
	s32 elapsedTimeMS;
	s32 vsyncTillFlip;
	s32 vSync_between_drawSync;
	s32 frameTimer_VsyncCallback;
};

#if defined(CTR_INTERNAL)
int NativePerf_ConfigureFromArgs(int argc, char **argv);
int NativePerf_IsEnabled(void);
void NativePerf_Shutdown(void);
void NativePerf_BeginFrame(const struct NativePerfFrameInfo *info);
void NativePerf_EndFrame(const struct NativePerfFrameInfo *info);
void NativePerf_RecordGpuFrame(u32 frameIndex, f64 gpuMs);
void NativePerf_BeginScope(enum NativePerfBucket bucket);
void NativePerf_EndScope(enum NativePerfBucket bucket);

// Adds to a counter of the open frame. Without --perf (or outside a frame)
// one compare and return, like a scope.
void NativePerf_AddCount(enum NativePerfCounter counter, u64 amount);
#else
static inline int NativePerf_ConfigureFromArgs(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	return 0;
}

static inline void NativePerf_Shutdown(void)
{
}

static inline int NativePerf_IsEnabled(void)
{
	return 0;
}

static inline void NativePerf_BeginFrame(const struct NativePerfFrameInfo *info)
{
	(void)info;
}

static inline void NativePerf_EndFrame(const struct NativePerfFrameInfo *info)
{
	(void)info;
}

static inline void NativePerf_RecordGpuFrame(u32 frameIndex, f64 gpuMs)
{
	(void)frameIndex;
	(void)gpuMs;
}

static inline void NativePerf_BeginScope(enum NativePerfBucket bucket)
{
	(void)bucket;
}

static inline void NativePerf_EndScope(enum NativePerfBucket bucket)
{
	(void)bucket;
}

static inline void NativePerf_AddCount(enum NativePerfCounter counter, u64 amount)
{
	(void)counter;
	(void)amount;
}
#endif

#endif
