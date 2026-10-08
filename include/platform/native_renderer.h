#ifndef NATIVE_RENDERER_H
#define NATIVE_RENDERER_H

#include <platform/native_renderer_types.h>

int NativeRenderer_InitialiseRender(char *windowName, int width, int height, int fullscreen);
int NativeRenderer_InitialisePSX(void);
void NativeRenderer_Shutdown(void);
void NativeRenderer_ResetDevice(void);
void NativeRenderer_ApplyWorldAspectToPresentation(void);

// The window's size, taken from the window. Everything that can change it -
// creation, a resize, a fullscreen toggle - ends here, so there is one answer
// and it is measured rather than remembered.
void NativeRenderer_SyncWindowSize(void);
int NativeRenderer_GetResolutionScale(void);

// What was asked for versus what is in force. They differ only when the
// asked-for factor would need a target larger than a texture may be, and the
// second is the one every rectangle in the frame is multiplied by - so it is
// also the one a menu row has to SHOW. A row that printed the wish would be
// reporting a setting nobody is using.
//
// But a menu row STEPS the setting, and a config file writes the setting. Step
// the effective one and a factor that was clamped walks down by itself: read
// back 6, add one, store 7, and the 8 that was asked for is gone without
// anybody having asked for that.
int NativeRenderer_GetEffectiveResolutionScale(void);
void NativeRenderer_SetResolutionScale(int scale);
int NativeRenderer_ClampResolutionScale(int scale);
int NativeRenderer_GetMaxResolutionScale(void);

// The one step above the last factor, which is not a factor: the scene is
// rasterised on the window's own pixel grid and the last step of the frame is
// one texel per pixel. Asked rather than compared against a number, so the
// sentinel is spelled in exactly one place.
int NativeRenderer_ResolutionIsNative(void);
int NativeRenderer_GetNativeResolutionPosition(void);

void NativeRenderer_GetMainTargetSize(int *outWidth, int *outHeight);

// Which of the two present routes a frame takes. Derived from the sizes, not
// from the factor: the VRAM texture can only show a picture at display size.
int NativeRenderer_PresentsViaMainTarget(void);
void NativeRenderer_PresentMainTarget(void);

// One frame's whole chain, printed: what the game asked to draw at, what that
// was rendered at, which present route ran, what the last step does with the
// pixels, and how many draws went through which texture path. A measuring
// tool - it changes nothing and is silent unless asked for.
void NativeRenderer_ReportPresentPath(int vblank);

// The sums a run leaves behind - vertex ring peak and wraps, fill quads, page
// store fills, writes into the portrait strip - printed once at exit through
// Platform_AtExitReport, before the log closes. Always, even when everything is
// zero.
void NativeRenderer_PrintExitSummary(void);

// The portrait strip (VRAM x 256..511, y 266..295): every write into it is
// counted, see the note on the definition. StripArm starts the count proper at
// the first game frame (the boot clear before it is counted on its own);
// idempotent. StripOwnWrites(1) / (0) brackets the portrait uploader's own
// writes, which are then counted as "own uploads" and not as hits; it nests.
void NativeRenderer_StripArm(void);
void NativeRenderer_StripOwnWrites(int on);

// The internal picture into a caller-owned buffer, BGRA8, no padding. Returns
// 0 when nothing was read - which is a case that happens, so it is a case that
// is answered rather than assumed. See the note on the definition.
int NativeRenderer_ReadMainTarget(void *dst, int dstBytes, int *outWidth, int *outHeight);
void NativeRenderer_EndPresentReportFrame(void);
void NativeRenderer_BeginScene(void);
void NativeRenderer_EndScene(void);
void NativeRenderer_EndGpuFrame(void);
void NativeRenderer_FinishGpuMeasurements(void);
void NativeRenderer_UpdateSwapIntervalState(int swapInterval);
void NativeRenderer_SwapWindow(void);
void NativeRenderer_StoreFrameBuffer(int x, int y, int w, int h);
void NativeRenderer_PresentVRAMDisplay(void);
void NativeRenderer_PresentVRAMRect(int x, int y, int w, int h);
void NativeRenderer_SaveVRAM(const char *outputFileName, int x, int y, int width, int height, int readFromFramebuffer);
void NativeRenderer_Clear(int x, int y, int w, int h, u8 r, u8 g, u8 b);
void NativeRenderer_ClearVRAM(int x, int y, int w, int h, u8 r, u8 g, u8 b);
void NativeRenderer_CopyVRAM(u16 *src, int x, int y, int w, int h, int dstX, int dstY);
void NativeRenderer_ReadVRAM(u16 *dst, int x, int y, int dstW, int dstH);
void NativeRenderer_UpdateVRAM(void);

// A draw has named this texture page at this depth, so the page store needs a
// tile for it. Called while primitives are parsed, which is before the batch is
// drawn and therefore before the tile is sampled. A no-op while the store is
// off, and idempotent - almost every primitive names a page already named.
void NativeRenderer_UsePageTile(int pageIndex, int fourBit);

// The rows of a page a primitive reads from. The range only widens, and a
// widening marks the tile dirty, which is what makes skipping a refill safe.
void NativeRenderer_NotePageRows(int pageIndex, int fourBit, int rowLo, int rowHi);
int NativeRenderer_GetVRAMStateSize(void);
int NativeRenderer_CaptureVRAMState(void *dst, int dstSize);
int NativeRenderer_RestoreVRAMState(const void *src, int srcSize);
TextureID NativeRenderer_GetVRAMTexture(void);
TextureID NativeRenderer_GetWhiteTexture(void);
void NativeRenderer_SetBlendMode(BlendMode blendMode);
void NativeRenderer_SetOffscreenState(const RECT16 *offscreenRect, int enable);
void NativeRenderer_SetProjection(const RECT16 *drawRect, const DISPENV *displayEnv, int offscreen);
void NativeRenderer_SetupClipMode(const RECT16 *clipRect, const DISPENV *displayEnv, int enable);
void NativeRenderer_SetTexture(TextureID texture, TexFormat texFormat);
void NativeRenderer_SetOverrideTextureSize(int width, int height);
void NativeRenderer_SetPSXTextureSemiTransPass(int pass);

// The page store: every 4-bit and 8-bit texture is sampled out of an index
// atlas. There is no second route - the one that read VRAM words directly was
// removed once the two were shown to produce the same bytes.
//
// The scale is how many atlas texels one VRAM texel occupies. Read it back
// rather than remembering it - it is clamped against its own ceiling and against
// the largest edge a device will allocate.
void NativeRenderer_SetPageScale(int scale);
int NativeRenderer_GetPageScale(void);
void NativeRenderer_GetPageStoreUse(int *outTilesUsed, int *outTilesTotal, int *outKiB);
// When the dither matrix is added: OFF, PACKED, ALWAYS. The name is asked for
// rather than spelled a second time in the menu.
int NativeRenderer_GetDither(void);
void NativeRenderer_SetDither(int mode);
void NativeRenderer_StepDither(void);
const char *NativeRenderer_DitherName(void);
int NativeRenderer_GetPageRowRange(void);
void NativeRenderer_SetPageRowRange(int on);
int NativeRenderer_GetPagePreload(void);
void NativeRenderer_SetPagePreload(int on);
int NativeRenderer_GetPageRefills(void);
// Anti-aliasing, the level: 1 (off), 2 or 4 samples in the main target. Setting
// is what is saved; StepMsaa is the choice in the menu and cancels --msaa;
// SetMsaaRun is a run value like --msaa (for --msaa-at), never saved;
// Requested is what is currently asked for, GetMsaa what applies. A new level
// takes effect from the next frame boundary.
void NativeRenderer_SetMsaaSetting(int samples);
int NativeRenderer_GetMsaaSetting(void);
void NativeRenderer_SetMsaaRun(int samples);
int NativeRenderer_GetMsaaRequested(void);
int NativeRenderer_GetMsaa(void);
void NativeRenderer_StepMsaa(void);
const char *NativeRenderer_MsaaName(int samples);

#if defined(CTR_INTERNAL)
// A partial fill rectangle is drawn as a quad instead of cleared with a
// scissor. On by default; --fill-rect-as-clear puts the old route back. A
// whole-display fill stays a clear either way.
void NativeRenderer_SetFillRectAsDraw(int enable);

// A VRAM copy is cut to the VRAM it has to fit in. On by default;
// --vram-copy-unclamped puts the old route back. The count is how many
// rectangles have needed cutting, which on the disc's own data is zero.
void NativeRenderer_SetVRAMCopyClamp(int enable);
int NativeRenderer_GetVRAMCopyClippedCount(void);

#endif
void NativeRenderer_SetPSXTextureOutputSTP(int enabled);
void NativeRenderer_SetPSXDrawMaskSet(int maskSet);
// ANTI-ALIASING: sample shading for the following draws, only effective in
// a pass with more than one sample.
void NativeRenderer_SetSampleShading(int enable);
// The first vertex of the last upload inside the vertex buffer. A batch no
// longer always starts at the front - see the note on the definition - so a
// split's own start vertex is relative to this.
int NativeRenderer_VertexUploadBase(void);
void NativeRenderer_UpdateVertexBuffer(const GrVertex *vertices, int count);
void NativeRenderer_DrawTriangles(int startVertex, int triangles);
void NativeRenderer_PushDebugLabel(const char *label);
void NativeRenderer_PopDebugLabel(void);

// One draw of the native probe. screenFromModel is column-major (as a GLSL
// mat4 reads it) and maps the unit probe body to (sx*w, sy*w, zNear, w): sx, sy
// in PSX screen pixels of the split, offsets included, w = view depth.
struct NativeLayerDraw
{
	float screenFromModel[16];
	int cull;       // NativeGfxCull for this draw
	int clearDepth; // 1 = clear the depth of the split's clip rectangle first

	// The form pose: the morphed vertices of this draw (NATIVE_PROBE_VERTEX_COUNT
	// of them) and the region of the pose buffer they are written into. NULL
	// draws the static probe mesh, as every other form does.
	const struct NativeProbeVertex *vertices;
	int vertexRegion;
};

// THE POSE BUFFER of the form pose: one region of a whole probe mesh per frame
// in work and native item, NATIVE_LAYER_POSE_FRAMES x NATIVE_LAYER_POSE_SLOTS
// regions. A region is written only in its own frame and is next written
// NATIVE_LAYER_POSE_FRAMES frames later, when the frames in flight that read
// it are done; one object per slot, so no draw reads what another one wrote.
#define NATIVE_LAYER_POSE_FRAMES 3
#define NATIVE_LAYER_POSE_SLOTS  64

// Draws the probe mesh into the split's place, then puts every renderer and
// device state back that the PSX path relies on. 0 = nothing drawn.
int NativeRenderer_DrawNativeProbe(const struct NativeLayerDraw *draw, const RECT16 *clip, const DISPENV *dispenv, int onScreen);

// 1 once the probe mesh buffers exist (only with --native-preview and --native-probe),
// and for the form texture its texture as well.
int NativeRenderer_NativeProbeReady(void);

// THE DEPTH OF THE MAIN TARGET, for native draws only. The render layer asks for
// it (1) in every frame a native object is bound and lets it go (0) when none
// is; only a change does anything. Without --native-preview nothing at all.
void NativeRenderer_WantNativeDepth(int want);

// The uploads of native textures in this run, and how many of them came after
// the first frame was drawn or into a frame that already had draws.
void NativeRenderer_NativeTextureUploads(unsigned int *uploads, unsigned int *duringFrame);

#endif
