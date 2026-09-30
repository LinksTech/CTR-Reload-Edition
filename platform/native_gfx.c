// Backend selection and the forwarders every call above the device goes
// through. Nothing here knows what a backend is made of.
//
// Included after the backend in the unity build, because it names its dispatch
// table and that is static.

#include <platform/native_gfx_dispatch.h>
// One backend, so the pointer never moves. It is still a pointer and the calls
// still go through the table: it is the seam a second backend would go back
// into. The initialiser alone does not show a forgotten entry - see the
// header; NativeGfx_CheckDispatch does.
global_variable const struct NativeGfxDispatch *s_gfx = &s_gfxVK;

// Every entry the backend did not fill in, by name, before the first frame.
//
// A designated initialiser zero-fills what it does not name, so a forgotten entry
// is a null that waits until something calls through it - which on this table
// means a crash somewhere in a race, with a stack that points at a forwarder and
// says nothing about which entry was missing.
//
// One loop over the one list in the header, so an entry added there is covered
// here without this function being touched. Reports every hole rather than
// stopping at the first: knowing there are four is worth more than finding them
// one build at a time.
//
// It returns the count rather than aborting. The caller decides what a hole
// means, and the log carries the names either way.
int NativeGfx_CheckDispatch(void)
{
	int holes = 0;

#define NATIVE_GFX_DISPATCH_CHECK(ret, fieldName, params)                        \
	if (s_gfx->fieldName == NULL)                                                \
	{                                                                            \
		holes++;                                                                 \
		Platform_LogError("[CTR Gfx] backend '%s' leaves %s unimplemented\n",     \
		                  (s_gfx->name != NULL) ? s_gfx->name : "(unnamed)", #fieldName); \
	}
	NATIVE_GFX_DISPATCH_ENTRIES(NATIVE_GFX_DISPATCH_CHECK)
#undef NATIVE_GFX_DISPATCH_CHECK

	if (holes != 0)
	{
		Platform_LogError("[CTR Gfx] %d dispatch entries are empty - the first call through any of them ends the run\n", holes);
	}

	return holes;
}

// --- Frame counters ---------------------------------------------------------
//
// Counted here rather than inside a backend, so the count cannot drift into
// backend-specific bookkeeping and stays where a second backend would find it.
//
// Vertex upload bytes are in here because they answer the actual question:
// geometry the renderer never hands down cannot be missing further along.

struct NativeGfxFrameCounters
{
	unsigned int draws;
	unsigned int drawsToWindow;
	unsigned int vertices;
	unsigned int targetBinds;
	unsigned int programBinds;
	unsigned int uniformUpdates;
	unsigned int vertexUploads;
	unsigned int vertexUploadBytes;
	unsigned int textureUploads;
};

// Counting says how much a frame does; it says nothing about the order, and the
// order is what render passes are decided by. One frame is traced in full so
// that the sequence the renderer actually produces can be read instead of
// guessed at. A steady-state frame, not a boot frame - the two look nothing
// alike.
// Set to a frame number to write that frame's calls out in order; 0 is off.
// Kept off by default now that the picture is up - a hundred lines a run is
// noise when nothing is wrong. It is the first thing to switch back on when
// something is: the order of target binds, clears, viewports and draws is what
// render pass decisions are made of, and it is not visible anywhere else.
// Pick a running frame, never a boot frame - see the note above.
#define NATIVE_GFX_TRACE_FRAME 0

global_variable struct NativeGfxFrameCounters s_gfxFrame;
global_variable NativeGfxTarget s_gfxCountedTarget = NATIVE_GFX_TARGET_DEFAULT;
global_variable unsigned int s_gfxFrameIndex = 0;

// The first frames are the boot sequence and the steady state is what the game
// actually looks like, so report both: every one of the first few, then a
// sample often enough to see a change and rare enough to keep the log readable.
#define NATIVE_GFX_FRAME_REPORT_FIRST 4
#define NATIVE_GFX_FRAME_REPORT_EVERY 120

// --- Frame timing -----------------------------------------------------------
//
// Measured here, so the log carries it. A run that has to be reported by
// photographing the screen is a run whose numbers cannot be compared
// afterwards.
//
// An average alone answers the wrong question. A 40 ms frame once a second is
// felt plainly and moves a 120-frame average by a third of a millisecond, so
// the worst frame of the last second is kept beside it. That is the number that
// matches what someone means by stuttering.

#define NATIVE_GFX_FRAMETIME_HISTORY 120

global_variable u64 s_gfxFrameLastCounter = 0;
global_variable double s_gfxFrameHistory[NATIVE_GFX_FRAMETIME_HISTORY];
global_variable int s_gfxFrameHistoryCount = 0;
global_variable int s_gfxFrameHistoryNext = 0;

global_variable double s_gfxFrameAverageMs = 0.0;
global_variable double s_gfxFrameWorstShownMs = 0.0;
global_variable double s_gfxFrameWorstPendingMs = 0.0;
global_variable double s_gfxFrameWorstElapsedMs = 0.0;

internal void NativeGfx_SampleFrameTime(void)
{
	const u64 now = SDL_GetPerformanceCounter();
	const u64 frequency = SDL_GetPerformanceFrequency();

	if ((s_gfxFrameLastCounter == 0) || (frequency == 0))
	{
		s_gfxFrameLastCounter = now;
		return;
	}

	const double elapsedMs = (double)(now - s_gfxFrameLastCounter) * 1000.0 / (double)frequency;

	s_gfxFrameLastCounter = now;

	// A level load or a dragged window is one enormous frame that would sit in
	// the worst-frame reading for a second and mean nothing.
	if (elapsedMs > 500.0)
	{
		return;
	}

	s_gfxFrameHistory[s_gfxFrameHistoryNext] = elapsedMs;
	s_gfxFrameHistoryNext = (s_gfxFrameHistoryNext + 1) % NATIVE_GFX_FRAMETIME_HISTORY;

	if (s_gfxFrameHistoryCount < NATIVE_GFX_FRAMETIME_HISTORY)
	{
		s_gfxFrameHistoryCount++;
	}

	double total = 0.0;

	for (int i = 0; i < s_gfxFrameHistoryCount; i++)
	{
		total += s_gfxFrameHistory[i];
	}

	s_gfxFrameAverageMs = total / (double)s_gfxFrameHistoryCount;

	if (elapsedMs > s_gfxFrameWorstPendingMs)
	{
		s_gfxFrameWorstPendingMs = elapsedMs;
	}

	s_gfxFrameWorstElapsedMs += elapsedMs;

	// Held a second at a time rather than decaying, so it can be read instead of
	// chased.
	if (s_gfxFrameWorstElapsedMs >= 1000.0)
	{
		s_gfxFrameWorstShownMs = s_gfxFrameWorstPendingMs;
		s_gfxFrameWorstPendingMs = 0.0;
		s_gfxFrameWorstElapsedMs = 0.0;
	}
}

internal int NativeGfx_Tracing(void)
{
	return (NATIVE_GFX_TRACE_FRAME != 0) && (s_gfxFrameIndex == NATIVE_GFX_TRACE_FRAME);
}

// Reads part of the bound target back and says whether anything was drawn into
// it. When validation has nothing left to say, the question is no longer
// whether the calls are legal but whether the images hold pixels, and which
// one first does not.
//
// Sits above the backend rather than inside it, so it asks the same question of
// whatever is underneath.
//
// One caveat worth knowing when reading the output: this reads the image as it
// stands, and under Vulkan this frame's own draws have been recorded but not
// submitted. What comes back is therefore the previous frame's result. Since
// every frame draws the same thing that answers the question all the same.
//
// Off by default. It reads with a one-shot command buffer while the frame's
// own commands are still recording, which synchronization validation rightly
// reports on every probe. Leaving that noise on would bury the next real
// report. Set it to 1 to ask "is there anything in this target at all" again; it
// is the fastest way to cut the chain in half and it should not have to be
// written a second time to be asked a second time.
#define NATIVE_GFX_PROBE_ENABLED 0

#define NATIVE_GFX_PROBE_SIZE 256

#if NATIVE_GFX_PROBE_ENABLED

global_variable unsigned char s_gfxProbePixels[NATIVE_GFX_PROBE_SIZE * NATIVE_GFX_PROBE_SIZE * 4];

internal void NativeGfx_ProbeBoundTarget(void)
{
	if (s_gfxCountedTarget == NATIVE_GFX_TARGET_DEFAULT)
	{
		// The window is not readable this way.
		return;
	}

	const int width = NativeGfx_TargetWidth(s_gfxCountedTarget);
	const int height = NativeGfx_TargetHeight(s_gfxCountedTarget);

	if ((width < NATIVE_GFX_PROBE_SIZE) || (height < NATIVE_GFX_PROBE_SIZE))
	{
		Platform_Log("[CTR Probe] target %u is %dx%d, too small to sample\n", s_gfxCountedTarget, width, height);
		return;
	}

	const int x = (width - NATIVE_GFX_PROBE_SIZE) / 2;
	const int y = (height - NATIVE_GFX_PROBE_SIZE) / 2;

	memset(s_gfxProbePixels, 0, sizeof(s_gfxProbePixels));

	NativeGfx_ReadPixels(x, y, NATIVE_GFX_PROBE_SIZE, NATIVE_GFX_PROBE_SIZE, NATIVE_GFX_TEXFMT_RGBA8, s_gfxProbePixels, NATIVE_GFX_PROBE_SIZE);

	unsigned int lit = 0;
	unsigned int brightest = 0;

	for (int i = 0; i < NATIVE_GFX_PROBE_SIZE * NATIVE_GFX_PROBE_SIZE; i++)
	{
		const unsigned int r = s_gfxProbePixels[(i * 4) + 0];
		const unsigned int g = s_gfxProbePixels[(i * 4) + 1];
		const unsigned int b = s_gfxProbePixels[(i * 4) + 2];
		const unsigned int peak = (r > g) ? ((r > b) ? r : b) : ((g > b) ? g : b);

		if (peak > 8)
		{
			lit++;
		}

		if (peak > brightest)
		{
			brightest = peak;
		}
	}

	Platform_Log("[CTR Probe] target %u (%dx%d), %dx%d at %d,%d: %u of %u pixels lit, brightest %u\n", s_gfxCountedTarget, width, height,
	             NATIVE_GFX_PROBE_SIZE, NATIVE_GFX_PROBE_SIZE, x, y, lit, (unsigned int)(NATIVE_GFX_PROBE_SIZE * NATIVE_GFX_PROBE_SIZE),
	             brightest);
}

#endif // NATIVE_GFX_PROBE_ENABLED

void NativeGfx_ReportFrame(void)
{
	NativeGfx_SampleFrameTime();

	if (s_gfxFrameIndex < NATIVE_GFX_FRAME_REPORT_FIRST || (s_gfxFrameIndex % NATIVE_GFX_FRAME_REPORT_EVERY) == 0)
	{
		Platform_Log("[CTR Gfx] frame %u on %s: %.1f fps, %.1f ms, worst %.1f ms\n", s_gfxFrameIndex, s_gfx->name,
		             (s_gfxFrameAverageMs > 0.0) ? (1000.0 / s_gfxFrameAverageMs) : 0.0, s_gfxFrameAverageMs, s_gfxFrameWorstShownMs);

		Platform_Log("[CTR Gfx] frame %u on %s: %u draw(s) / %u vertices, %u to the window | %u target, %u program, %u uniform | upload %u vtx / %u bytes, %u tex\n",
		             s_gfxFrameIndex, s_gfx->name,
		             s_gfxFrame.draws, s_gfxFrame.vertices, s_gfxFrame.drawsToWindow,
		             s_gfxFrame.targetBinds, s_gfxFrame.programBinds, s_gfxFrame.uniformUpdates,
		             s_gfxFrame.vertexUploads, s_gfxFrame.vertexUploadBytes, s_gfxFrame.textureUploads);
	}

	{
		const struct NativeGfxFrameCounters cleared = {0};

		s_gfxFrame = cleared;
	}

	s_gfxFrameIndex++;
}

// --- Forwarders -------------------------------------------------------------

TextureID NativeGfx_CreateTexture(const NativeGfxTextureDesc *desc)
{
	return s_gfx->createTexture(desc);
}

void NativeGfx_UpdateTexture(TextureID texture, int x, int y, int width, int height, NativeGfxTextureFormat format, const void *pixels, int rowPixels)
{
	s_gfxFrame.textureUploads++;
	s_gfx->updateTexture(texture, x, y, width, height, format, pixels, rowPixels);
}

void NativeGfx_DestroyTexture(TextureID texture)
{
	s_gfx->destroyTexture(texture);
}

void NativeGfx_BindTexture(int slot, TextureID texture, NativeGfxFilter filter)
{
	if (NativeGfx_Tracing())
	{
		Platform_Log("[CTR Trace]   bindTexture slot %d -> %u\n", slot, texture);
	}

	s_gfx->bindTexture(slot, texture, filter);
}

NativeGfxFilter NativeGfx_TextureFilter(TextureID texture)
{
	return s_gfx->textureFilter(texture);
}

NativeGfxBuffer NativeGfx_CreateVertexBuffer(const NativeGfxVertexBufferDesc *desc)
{
	return s_gfx->createVertexBuffer(desc);
}

void NativeGfx_DestroyVertexBuffer(NativeGfxBuffer buffer)
{
	s_gfx->destroyVertexBuffer(buffer);
}

void NativeGfx_BindVertexBuffer(NativeGfxBuffer buffer)
{
	if (NativeGfx_Tracing())
	{
		Platform_Log("[CTR Trace]   bindVertexBuffer %u\n", buffer);
	}

	s_gfx->bindVertexBuffer(buffer);
}

void NativeGfx_UpdateVertexBuffer(NativeGfxBuffer buffer, int offset, int bytes, const void *source)
{
	s_gfxFrame.vertexUploads++;
	s_gfxFrame.vertexUploadBytes += (unsigned int)((bytes > 0) ? bytes : 0);
	s_gfx->updateVertexBuffer(buffer, offset, bytes, source);
}

NativeGfxTarget NativeGfx_CreateTarget(const NativeGfxTargetDesc *desc)
{
	return s_gfx->createTarget(desc);
}

void NativeGfx_ResizeTarget(NativeGfxTarget target, int width, int height)
{
	s_gfx->resizeTarget(target, width, height);
}

void NativeGfx_DestroyTarget(NativeGfxTarget target)
{
	s_gfx->destroyTarget(target);
}

void NativeGfx_BindTarget(NativeGfxTarget target)
{
	// Before the switch, while the target being left is still the one readPixels
	// would read.
#if NATIVE_GFX_PROBE_ENABLED
	if (NativeGfx_Tracing())
	{
		NativeGfx_ProbeBoundTarget();
	}
#endif

	s_gfxCountedTarget = target;
	s_gfxFrame.targetBinds++;

	if (NativeGfx_Tracing())
	{
		Platform_Log("[CTR Trace] bindTarget %u%s\n", target, (target == NATIVE_GFX_TARGET_DEFAULT) ? " (window)" : "");
	}

	s_gfx->bindTarget(target);
}

TextureID NativeGfx_TargetTexture(NativeGfxTarget target)
{
	return s_gfx->targetTexture(target);
}

int NativeGfx_TargetWidth(NativeGfxTarget target)
{
	return s_gfx->targetWidth(target);
}

int NativeGfx_TargetHeight(NativeGfxTarget target)
{
	return s_gfx->targetHeight(target);
}

void NativeGfx_SetTargetSamples(NativeGfxTarget target, int samples)
{
	s_gfx->setTargetSamples(target, samples);
}

int NativeGfx_TargetSamples(NativeGfxTarget target)
{
	return s_gfx->targetSamples(target);
}

void NativeGfx_SetSampleShading(int enable)
{
	s_gfx->setSampleShading(enable);
}

ShaderID NativeGfx_CreateProgram(const NativeGfxProgramDesc *desc)
{
	return s_gfx->createProgram(desc);
}

void NativeGfx_DestroyProgram(ShaderID program)
{
	s_gfx->destroyProgram(program);
}

void NativeGfx_BindProgram(ShaderID program)
{
	s_gfxFrame.programBinds++;

	if (NativeGfx_Tracing())
	{
		Platform_Log("[CTR Trace]   bindProgram %u\n", program);
	}

	s_gfx->bindProgram(program);
}

void NativeGfx_UpdateUniforms(ShaderID program, const void *block)
{
	s_gfxFrame.uniformUpdates++;
	s_gfx->updateUniforms(program, block);
}

void NativeGfx_SetViewport(int x, int y, int width, int height)
{
	if (NativeGfx_Tracing())
	{
		Platform_Log("[CTR Trace]   viewport %d,%d %dx%d\n", x, y, width, height);
	}

	s_gfx->setViewport(x, y, width, height);
}

void NativeGfx_SetScissorEnabled(int enable)
{
	if (NativeGfx_Tracing())
	{
		Platform_Log("[CTR Trace]   scissor %s\n", enable ? "on" : "off");
	}

	s_gfx->setScissorEnabled(enable);
}

void NativeGfx_SetScissorRect(int x, int y, int width, int height)
{
	if (NativeGfx_Tracing())
	{
		Platform_Log("[CTR Trace]   scissor %d,%d %dx%d\n", x, y, width, height);
	}

	s_gfx->setScissorRect(x, y, width, height);
}

int NativeGfx_SetWireframe(int enable)
{
	return s_gfx->setWireframe(enable);
}

void NativeGfx_SetBlendMode(BlendMode blend)
{
	s_gfx->setBlendMode(blend);
}

void NativeGfx_ClearColor(float r, float g, float b, float a)
{
	s_gfx->clearColor(r, g, b, a);
}

void NativeGfx_ClearColorBuffer(void)
{
	if (NativeGfx_Tracing())
	{
		Platform_Log("[CTR Trace] clearColor on target %u\n", s_gfxCountedTarget);
	}

	s_gfx->clearColorBuffer();
}

void NativeGfx_Draw(int firstVertex, int vertexCount)
{
	s_gfxFrame.draws++;
	s_gfxFrame.vertices += (unsigned int)((vertexCount > 0) ? vertexCount : 0);

	if (s_gfxCountedTarget == NATIVE_GFX_TARGET_DEFAULT)
	{
		s_gfxFrame.drawsToWindow++;
	}

	if (NativeGfx_Tracing())
	{
		Platform_Log("[CTR Trace]     draw %d verts at %d -> target %u\n", vertexCount, firstVertex, s_gfxCountedTarget);
	}

	s_gfx->draw(firstVertex, vertexCount);
}

void NativeGfx_ReadPixels(int x, int y, int width, int height, NativeGfxTextureFormat format, void *dst, int rowPixels)
{
	if (NativeGfx_Tracing())
	{
		Platform_Log("[CTR Trace] readPixels %dx%d at %d,%d from target %u\n", width, height, x, y, s_gfxCountedTarget);
	}

	s_gfx->readPixels(x, y, width, height, format, dst, rowPixels);
}

int NativeGfx_TimersSupported(void)
{
	return s_gfx->timersSupported();
}

NativeGfxTimer NativeGfx_CreateTimer(void)
{
	return s_gfx->createTimer();
}

void NativeGfx_DestroyTimer(NativeGfxTimer timer)
{
	s_gfx->destroyTimer(timer);
}

int NativeGfx_BeginTimer(NativeGfxTimer timer)
{
	return s_gfx->beginTimer(timer);
}

void NativeGfx_EndTimer(void)
{
	s_gfx->endTimer();
}

int NativeGfx_TimerReady(NativeGfxTimer timer)
{
	return s_gfx->timerReady(timer);
}

u32 NativeGfx_TimerElapsedNanoseconds(NativeGfxTimer timer)
{
	return s_gfx->timerElapsed(timer);
}

void NativeGfx_PushDebugLabel(const char *label)
{
	s_gfx->pushDebugLabel(label);
}

void NativeGfx_PopDebugLabel(void)
{
	s_gfx->popDebugLabel();
}


