/*
 * Derived from REDRIVER2/PsyCross MIT source:
 * externals/PsyCross/src/render/PsyX_render.cpp
 * See THIRD_PARTY_NOTICES.md for copyright and license details.
 */

#include <macros.h>
#include "platform/native_renderer_types.h"
#include <SDL3/SDL.h>

#include "platform/native_gfx.h"
#include "platform/native_char_gpu.h"
#include "platform/native_gpu.h"
#include "platform/native_log.h"
#include "platform/native_perf.h"
#include "platform/native_probe.h"
#include "platform/native_tex.h"
#include "platform/native_twin.h"
#include "platform/native_renderer.h"

#include <assert.h>
#include <string.h>

#ifdef _WIN32
#include "platform/native_win32.h"

__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;

#endif // def WIN32

// WHY THE VRAM TEXTURE IS RG8, which is still the reason and is worth keeping.
//
// VRAM holds packed 16-bit PSX pixels as two bytes, R the low one and G the
// high one. Two bytes per texel is the faithful storage - it is the real PS1's
// one megabyte - against the eight bytes per texel an RG32F would take. With
// NEAREST sampling RG8 hands the shader byte/255 exactly, the same numbers the
// float format gave, so the CLUT and texture-page reconstruction is unchanged.
//
// The two names that used to carry this - VRAM_FORMAT and VRAM_INTERNAL_FORMAT,
// spelled GL_RG and GL_RG8 - are gone with the GL backend. They named constants
// that no longer exist anywhere this file includes, and nothing read them. The
// format is chosen at the one place it is now asked for, as
// NATIVE_GFX_TEXFMT_RG8.

extern SDL_Window *g_window;

#define NATIVE_RENDERER_LOG(fmt, ...)   Platform_Log("[CTR Renderer] " fmt, __VA_ARGS__)
#define NATIVE_RENDERER_ERROR(fmt, ...) Platform_LogError("[CTR Renderer] [%s] - " fmt, __func__, __VA_ARGS__)

#define MAX_NUM_VERTEX_BUFFERS          (2)
#define PSX_SCREEN_ASPECT               (240.0f / 320.0f) // PSX screen is mapped always to this aspect

#if defined(CTR_INTERNAL)
// The timer itself is the device layer's - this only holds the handles and
// matches results back to the frame that asked for them.
#define NATIVE_GPU_TIMER_QUERY_COUNT 8

struct NativeGpuTimerQuery
{
	NativeGfxTimer id;
	u32 frameIndex;
	b32 pending;
};

global_variable struct NativeGpuTimerQuery s_gpuTimerQueries[NATIVE_GPU_TIMER_QUERY_COUNT];
global_variable u32 s_gpuTimerFrameIndex;
global_variable s32 s_gpuTimerNextQuery;
global_variable b32 s_gpuTimerSupported;
global_variable b32 s_gpuTimerActive;
#endif

global_variable BlendMode s_previousBlendMode = BM_NONE;

global_variable int s_previousScissorState = 0;
global_variable int s_previousOffscreenState = 0;
global_variable RECT16 s_previousOffscreen = {0, 0, 0, 0};

global_variable ShaderID s_previousShader = (ShaderID)-1;

global_variable TextureID s_rgLutTexture = (TextureID)-1;
// NOTE(penta3): Single persistent VRAM texture, matching real PS1's single
// 1MB VRAM. PS1 page-flips two windows *inside* one VRAM; it never keeps two
// full copies. The old double buffer existed only to orphan the texture on the
// per-frame full re-upload, which no longer happens (we upload dirty rects).
#define NATIVE_VRAM_DIRTY_RECT_CAP 128
#define NATIVE_VRAM_TILE_SIZE      8
#define NATIVE_VRAM_TILE_COLS      (VRAM_WIDTH / NATIVE_VRAM_TILE_SIZE)
#define NATIVE_VRAM_TILE_ROWS      (VRAM_HEIGHT / NATIVE_VRAM_TILE_SIZE)
#define NATIVE_VRAM_TILE_COUNT     (NATIVE_VRAM_TILE_COLS * NATIVE_VRAM_TILE_ROWS)
#define NATIVE_VRAM_TILE_WORDS     (NATIVE_VRAM_TILE_COUNT / 32)

// NOTE(aalhendi): Native splits PS1 VRAM between a CPU mirror and one packed
// GPU texture. CPU writes are uploaded before GPU reads; GPU-newer tiles are
// resolved into the CPU mirror only when game code reads those VRAM regions.
struct NativeVramState
{
	TextureID texture;
	u16 cpuPixels[VRAM_WIDTH * VRAM_HEIGHT];
	RECT16 cpuDirtyRects[NATIVE_VRAM_DIRTY_RECT_CAP];
	u32 gpuNewerTiles[NATIVE_VRAM_TILE_WORDS];
	s32 cpuDirtyRectCount;
};

global_variable struct NativeVramState s_vram;

// --- The page store ---------------------------------------------------------
//
// The same textures VRAM already holds, stored as one palette index per pixel
// instead of as bit fields inside 16-bit words. What that buys is not speed and
// not, on its own, a different picture: it is that the resolution of a texture
// stops being tied to how many texels fit in a VRAM word.
//
// It is a cache, and VRAM stays the truth. Nothing is ever written here that
// was not read out of VRAM, so turning the store off is not a fallback - it is
// the same picture arriving by the other road, which is what makes the two
// comparable at all.
//
// SHAPE. 32 texture pages times two depths is 64 tiles, laid out 8 across.
// A tile is 256 texels square at scale one, because that is a 4-bit page: 64
// words of four texels, 256 rows. The 8-bit tiles are 256 wide too, which is
// twice their own page - deliberately, because the hardware lets U run past the
// page into the next one and the old route follows it there.
//
// A tile's number is page * 2 + depth, and the page number is the same one the
// vertex stage already encodes. Nothing has to be looked up, and there is no
// allocator: where a page goes is arithmetic.
//
// WHAT GROWS. Rows of tiles, and only as far as a page that is actually drawn
// with reaches. A level that never uses the upper pages never pays for them.
// Every growth says so with the old and the new number - a limit that takes
// effect quietly is a bug, and this one is reached by data we do not own.
// A 4-bit texture page is 256 texels across and 256 rows down. Two independent
// facts that happen to be the same number: the width is 64 words of four texels
// and the height is what the hardware gives a page. Named separately so a
// change to one cannot be mistaken for a change to both.
#define NATIVE_PAGE_TEXELS_X    256
#define NATIVE_PAGE_TEXELS_Y    256

// Everything below here is derived, not decided. A page is 64 VRAM words across
// because 256 texels at four to a word is 64; how many pages fit follows from
// that and from how big VRAM is. This is the arithmetic the vertex stage does
// with a literal 1024 that is really 16 pages times 64 words - the same two
// facts, written as one number that only happens to equal the VRAM width.
#define NATIVE_PAGE_WORDS_X     (NATIVE_PAGE_TEXELS_X / 4)
#define NATIVE_PAGE_COLS        (VRAM_WIDTH / NATIVE_PAGE_WORDS_X)
#define NATIVE_PAGE_ROWS        (VRAM_HEIGHT / NATIVE_PAGE_TEXELS_Y)
#define NATIVE_PAGE_COUNT       (NATIVE_PAGE_COLS * NATIVE_PAGE_ROWS)

// Two tiles per page, one per depth. How wide the atlas lies is the one free
// choice in here; the height follows, and both together have to hold every tile.
#define NATIVE_PAGE_TILE_COUNT  (NATIVE_PAGE_COUNT * 2)
#define NATIVE_PAGE_TILE_COLS   8
#define NATIVE_PAGE_TILE_ROWS   ((NATIVE_PAGE_TILE_COUNT + NATIVE_PAGE_TILE_COLS - 1) / NATIVE_PAGE_TILE_COLS)

// One bit per tile in a u64. Said out loud rather than assumed, because the
// assumption is silent everywhere it is made.
CTR_STATIC_ASSERT(NATIVE_PAGE_TILE_COUNT <= 64);

// The largest tile scale the atlas is allowed to ask for, and the largest edge
// any one texture may have. Eight tiles across at scale four is 8192, which is
// the edge a device is most likely to stop at.
#define NATIVE_PAGE_SCALE_MAX   4
#define NATIVE_PAGE_EDGE_MAX    8192

struct NativePageStore
{
	TextureID texture;
	NativeGfxTarget target;

	// The scale the atlas was built at, which is not always the one that was
	// asked for: an edge that will not fit is reported as the scale that does.
	int scale;
	int tileRows;

	// One bit per tile. In use means the tile is resident and kept fresh; named
	// means some draw has actually asked for that page and depth; dirty means
	// VRAM under it has moved since it was last written.
	//
	// Named used to be the same word as used, because a tile became resident by
	// being named. With the whole store held from the start those are two
	// different facts, and the interesting one - how many tiles does this level
	// actually want - is the one that would have been lost.
	u64 used;
	u64 named;
	u64 dirty;

	// The rows of each page any draw has ever sampled, 0..255, and empty when
	// min is above max.
	//
	// A tile is a whole 256-row page, but the framebuffer shares page columns
	// with the textures and only covers the top 216 rows of them. Writing the
	// presented frame back into VRAM therefore invalidated those tiles every
	// single frame, for rows nothing reads. Keeping the sampled range is what
	// lets that be decided rather than assumed.
	// Empty is min above max, which is what memset to zero does NOT give - so
	// they are set explicitly where the store is reset. A range that starts at
	// 0..0 would claim row zero is read by every page.
	u8 rowMin[NATIVE_PAGE_TILE_COUNT];
	u8 rowMax[NATIVE_PAGE_TILE_COUNT];

	int fillsSpared;
	int fillsSparedSinceReport;

	int usedCount;
	int namedCount;
	int preloaded;
	// How many tiles were named when the row table was last printed.
	// NOT a yes/no - see NativeRenderer_ReportPageStore.
	int rowsReportedAtCount;
	int refillsThisFrame;
	int refillsSinceReport;
	int framesSinceReport;
	int refillsTotal;
	int growths;
};

global_variable struct NativePageStore s_pages;

// What the atlas would be built at. A setting, never a derivation - the same
// rule the internal resolution factor is held to, and for the same reason.
int g_cfg_pageScale = 1;

// WHO BINDS THE VERTEX BUFFER, THE RENDERER OR THE UPLOAD.
//
// The renderer uploads its geometry once per frame and sets
// s_boundVertexBuffer while doing so - so it REMEMBERS which buffer is bound, and
// never binds it. After that come up to ninety draws without a single bind.
// So far the back end carried that: its UpdateVertexBuffer binds
// along the way, because GL's version did a glBindVertexArray on the way to
// glBufferSubData and never undid it.
//
// That is not GL fidelity, that is a missing bind. Four other places in the
// renderer do it right: after every detour they explicitly restore the
// buffer. Only the place that fills it in the first place does
// not.
//
// On: the renderer binds itself. Off: the old route, so that the two
// can be run side by side. Both must give the same picture -
// NativeGfxVK_BindVertexBuffer is a pure assignment, and what is assigned is
// the same handle that the upload assigns anyway.
int g_cfg_bindVertexExplicit = 1;

// Whether a tile is refilled only when the VRAM that moved is in the rows that
// tile is actually read at, and whether the whole store is held from the start
// instead of tile by tile as pages are first named.
//
// Both on, both with a flag that puts the old road back, because both change
// WHEN something is written and not what is written - and "the same picture by
// the other road" is a claim that has to be checkable.
int g_cfg_pageRowRange = 1;
int g_cfg_pagePreload = 1;

// Whether a frame's uploads take the next free stretch of one buffer instead of
// the front of the other one, turn and turn about. See the note above
// NativeRenderer_UpdateVertexBuffer. On, because the old way lets a frame
// overwrite geometry its own recorded draws still have to read; off puts it
// back so the two can be compared rather than argued.
int g_cfg_vertexRing = 1;

// WHEN THE DITHER MATRIX IS ADDED. Three settings, and they are three answers to
// one question - will this draw's colour be truncated to five bits afterwards?
//
//   0  OFF     never. The console's pattern is gone everywhere, including from
//              the pass that IS truncated, so a gradient that gets packed back
//              into VRAM bands the way five bits band.
//
//   1  PACKED  only where it is. That is internal resolution one, where the
//              window is shown from VRAM, and the offscreen pass at any factor,
//              because that is packed straight back. Above factor one the
//              visible picture keeps its full eight bits and gets nothing added.
//
//   2  ALWAYS  what this tree did until now: added to everything, truncated
//              almost nowhere.
//
// DEFAULT PACKED, and the reason is not taste. Dither and truncate are one
// operation; doing the first without the second is not a faithful console
// picture, it is the noise the operation exists to hide, at NxN the size. Where
// the truncation does happen, the matrix still runs and still means what the
// hardware meant.
//
// The one thing PACKED costs above factor one: NativeRenderer_StoreFrameBuffer
// packs the visible target back into VRAM for the game's own screen-copy
// effects, and that copy is now truncated without a pattern. A gradient read
// back that way can band. The right answer to that is to dither in the pack
// shader, where the truncation is - which is a change of its own.
int g_cfg_dither = 1;

#define NATIVE_DITHER_OFF    0
#define NATIVE_DITHER_PACKED 1
#define NATIVE_DITHER_ALWAYS 2

global_variable int s_vertexUploadBase = 0;
global_variable int s_vertexUploadCursor = 0;
global_variable int s_vertexUploadWraps = 0;
global_variable int s_vertexUploadPeak = 0;
global_variable int s_vertexUploadsThisFrame = 0;
global_variable int s_vertexUploadsPeak = 0;

// Uniform blocks for the blit shaders. These already set every uniform
// immediately before their draw, so they are block-shaped as written; the PSX
// shaders are not, and keep their per-field path until their state handling is
// reworked to accumulate.
// std140, so the block is a multiple of 16 even when one int is all it carries.
// A block declared as 16 bytes in the shader must not be backed by a 4-byte
// buffer; the device sizes its uniform buffer from this number.
struct NativeBlitVramUniforms
{
	float sourceRect[4];
};

// The resolve: how many samples one output pixel averages, and how far apart
// they sit in the source.
//
//   taps[0], taps[1]   the box, in samples, per axis
//   taps[2], taps[3]   the spacing between samples, in source uv
//
// std140 puts the vec4 at 16 even though the int before it ends at 4, so the
// padding is named rather than left to the compiler to agree with the device by
// coincidence.
struct NativeBlitResolveUniforms
{
	int flipY;
	int _pad[3];
	float taps[4];
};

CTR_STATIC_ASSERT(sizeof(struct NativeBlitResolveUniforms) == 32);
CTR_STATIC_ASSERT(offsetof(struct NativeBlitResolveUniforms, taps) == 16);

// One tile of the page store: where in VRAM it comes from, how big it is, and
// how big VRAM is. Everything the fill shader needs arrives here, so that shader
// holds no constant another file also holds.
struct NativeBlitPagesUniforms
{
	float source[4];
	float shape[4];
	float vram[4];
};

// No NativeGfxUniformField list for either of these on purpose.
//
// Both shader bodies were converted to the shared dialect and declare a block -
// "UBO_QUALIFIER uniform ResolveBlock" and "VramBlock" in native_shaders.inc - but
// their compile calls kept handing over a by-name field list, which is the other
// path and the two never combine. Under GL the field walk asks
// a block member's location, got nothing back, and skipped it in silence;
// under Vulkan the block was sized 0. So flipY and
// sourceRect were both permanently zero, and a sourceRect of zero collapses the
// present quad's UVs to a point: the whole window became VRAM texel (0,0),
// stretched. That is the flat violet screen, and it changed colour per frame
// because texel (0,0) did.
//
// The structs stay; the field lists are gone so the wrong path cannot be picked
// again by reaching for a list that is sitting right there.

global_variable NativeGfxTarget s_mainRenderTarget = NATIVE_GFX_INVALID;
global_variable NativeGfxTarget s_offscreenRenderTarget = NATIVE_GFX_INVALID;

// Which target is bound. Same reasoning as s_scissorRect: the readbacks below
// bind another target for a moment and have to put back what was there, and
// asking the driver (glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING)) is a question
// Vulkan cannot answer. Every bind goes through NativeRenderer_BindTarget.
global_variable NativeGfxTarget s_currentTarget = NATIVE_GFX_TARGET_DEFAULT;

internal void NativeRenderer_BindTarget(NativeGfxTarget target)
{
	s_currentTarget = target;
	NativeGfx_BindTarget(target);
}

global_variable TextureID s_whiteTexture = (TextureID)-1;
// Both start on the value the device starts on, so a restore that happens
// before any write still restores what is really there.
global_variable int s_scissorRect[4] = {0, 0, 0, 0};
global_variable float s_clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
// Device lifetime. Declared here rather than pulled in through a header because
// native_gfx.h deliberately does not describe it: bringing a device up is
// entangled with this file's window ownership, and everything that draws goes
// through NativeGfx_* instead.
int NativeGfxVK_Init(SDL_Window *window);
void NativeGfxVK_Shutdown(void);
const char *NativeGfxVK_DeviceName(void);
int NativeGfxVK_PresentFrame(void);
int NativeGfxVK_StartFirstFrame(void);


global_variable TextureID s_lastBoundTexture = (TextureID)-1;

TextureID NativeRenderer_GetVRAMTexture(void)
{
	return s_vram.texture;
}

TextureID NativeRenderer_GetWhiteTexture(void)
{
	return s_whiteTexture;
}

int g_windowWidth = 0;
int g_windowHeight = 0;

global_variable int s_presentAspectW = 4;
global_variable int s_presentAspectH = 3;
global_variable SDL_Rect s_presentViewport = {0, 0, 0, 0};

int g_dbg_wireframeMode = 0;
int g_dbg_texturelessMode = 0;

int g_cfg_bilinearFiltering = 0;


// THE WHOLE CHAIN OF ONE FRAME, PRINTED
//
// A picture that looks like nearest-neighbour magnification has four places it
// could have come from, and arguing about which is cheaper than measuring it
// exactly once. So this prints all four for one frame: what size the game asked
// to draw at, what size that was rendered at, which of the two present routes
// ran, what the last step actually does with the pixels, and how many draws of
// the frame sampled through which texture path.
//
// Counted rather than derived. Every number below is read back from the thing
// that owns it at the moment the frame ends.
int g_cfg_presentReportAt = 0;

global_variable int s_beginSceneClears = 0;
global_variable int s_beginSceneReloads = 0;
// Two programs since the format travels in the vertex: 0 = psx (4/8/16 bit, decided per vertex),
// 1 = the 32-bit override texture. Which FORMAT a primitive used is counted
// where the format is known, per primitive, in native_gpu.c ([CTR GPU] block).
global_variable int s_psxDrawsByFormat[2] = {0, 0};
global_variable int s_psxDrawsByFormatFrame[2] = {0, 0};
global_variable int s_psxDrawsUntextured = 0;
global_variable int s_psxDrawsUntexturedFrame = 0;

// Which of the four PSX texture formats a draw used. 4- and 8-bit are the
// indexed ones and go through the page store; 16- and 32-bit carry their own
// colour and do not. The counts are the answer to "is there still a path that
// samples some other way" - and it is a count, not a reading of the code.
//
// Untextured counted apart, and it has to be. A split's format comes from the
// current texture page whether or not the split reads a texture, so an
// untextured draw arrives here calling itself 4-bit while it samples the white
// texture and shows vertex colour. Counting those as textured would have said
// "every draw on this screen is a magnified texture" about a screen that may
// be half flat-shaded - a number that reads as measured and is not.
internal void NativeRenderer_NotePsxDraw(int formatIndex, TextureID texture)
{
	if ((formatIndex < 0) || (formatIndex > 1))
	{
		return;
	}

	if (texture == s_whiteTexture)
	{
		s_psxDrawsUntextured++;
		s_psxDrawsUntexturedFrame++;
		return;
	}

	s_psxDrawsByFormat[formatIndex]++;
	s_psxDrawsByFormatFrame[formatIndex]++;
}

// The internal resolution factor. One means the picture is rendered at the size
// the PSX display environment asks for, which is what every frame did before
// this existed, and every step below is written so that one is not merely equal
// to the old behaviour but is literally the old code path.
//
// It is a setting and never a derivation. The build this renderer was ported
// from derives its factor
// from the window height and had to fix that twice: a window that changed size
// kept the old factor, and starting fullscreen produced a different picture than
// starting windowed and then going fullscreen. Nothing here reads the window, so
// a resize or a fullscreen toggle cannot move it.
#define NATIVE_RES_SCALE_MAX     8
#define NATIVE_RES_TARGET_MAX_PX 8192

// NATIVE IS A POSITION, NOT A FACTOR.
//
// One step above the last one, and it does not mean x9. It means: the scene is
// rasterised straight onto the window's own pixel grid, so the last step of the
// frame is one texel per pixel and there is nothing left to resample.
//
// Why that is even possible here and not in an emulator: an emulator ends its
// frame holding a finished bitmap in console measurements, and a bitmap shown at
// any other size has to be resampled. We do not hold a bitmap. We hold geometry,
// and NativeRenderer_SetProjection has never known the factor - it maps 0..disp
// onto the whole viewport whatever the target's size is. So the target may be
// any size at all and the picture lands correctly. That is the whole change.
//
// Stored in the same int as the factor and saved to disk as 9, the way the view
// settings store TRACK DIST OFF: a position reached by stepping, not a share.
#define NATIVE_RES_SCALE_NATIVE (NATIVE_RES_SCALE_MAX + 1)

// Four, and what that is worth against what it costs.
//
// Counted rather than guessed - tools/frame_cost.py. A frame is four passes and
// only ONE of them grows with the factor: the scene, which is 512x216 times the
// factor squared. The pack back into VRAM draws into VRAM and VRAM is never
// scaled. The presentation pass covers the window, which the factor does not
// move. The page-store fills are driven by the PAGE scale and not by this one.
//
// On a 3440x1440 window, at an overdraw of two to four, x4 comes out at 1.55 to
// 2.05 times a x1 frame - and the frame it doubles is nine to thirteen million
// fragments, which is a small frame. x8 is where it turns: 5.7 to 7.8 times,
// because the scene passes the presentation pass and then keeps going.
//
// The other half is what it buys, and that has a limit worth knowing: at 512x216
// the internal picture is 2048x864 at x4, and a 3440-wide window is showing that
// ENLARGED. Nothing is resolved down there - the sharpening is entirely that the
// geometry is rasterised on a grid sixteen times finer, not that samples are
// averaged. Averaging starts at x7, where 512*k first passes 3440.
int g_cfg_resScale = 4;

// The initialiser is the one write to this that does not go through the setter,
// and therefore the one that is not clamped. Checked at compile time instead.
CTR_STATIC_ASSERT((4 >= 1) && (4 <= NATIVE_RES_SCALE_MAX));

// --- Fast boot ------------------------------------------------------------
//
// Skips the boot announcer and the spin that waits for it, the two logo pages,
// the copyright hold, the Naughty Dog crate cutscene, and the title cinematic -
// about half a minute per start, and every test cycle pays it.
//
// Nothing is bypassed in the sense of skipped work: the same loads run and the
// same state machine advances. What goes is waiting, and pictures that only
// stand until something draws over them.
//
// On by default, because the wait is worth nothing to anybody. --slow-boot
// puts all of it back; the three below put back one piece each, which is what
// makes it possible to say which piece a difference came from.
//
// Read by game/MAIN/MainMain.c, game/LOAD/LOAD_TenStages.c and
// game/230/MM_Title.c.
int g_cfg_fastBoot = 1;

// The copyright page keeps its full hold while everything else fast boot does
// stays on. --legacy-copyright-hold.
int g_cfg_legacyCopyrightHold = 0;

// The two logo pages are loaded and shown again. --legacy-boot-logos.
int g_cfg_legacyBootLogos = 0;

// The Naughty Dog crate is a cutscene, not a load. With this on, fast boot
// loads the menu level in its place and the cutscene never runs.
// --keep-intro-level is the way back.
int g_cfg_skipIntroLevel = 1;

// --- Jumping straight into a track ------------------------------------------
//
// -1 is "stay in the menu", which is what every run did before this existed.
// --level N asks for a track, --driver M for who drives it.
//
// This is a measuring switch before it is a convenience. A comparison between
// two builds only says something if both runs are the same run, and a track
// reached by hand is not: the two dumps come out of different moments and
// differ almost everywhere, which reads exactly like a broken renderer and is
// not one. Reached by a flag, both runs are hands-off and the game state at a
// given VBlank is the same on both sides.
//
// Read by game/DebugMenu.c, which is the one place that already knows how to
// set a track up.
int g_cfg_jumpLevel = -1;
int g_cfg_jumpDriver = 0;

// --autoload-track and --exit-after-frames, the container-track counterparts of
// --level: the file name to take out of tracks/ (empty: none), and how many
// game frames into the race the run ends by itself (-1: never). Read by
// game/DebugMenu.c in the same block as g_cfg_jumpLevel.
char g_cfg_autoloadTrack[128] = "";
int g_cfg_exitAfterFrames = -1;
int g_cfg_autoloadDemo = 0;

// --autopilot: the player's seat drives as a bot in a REAL race (HUD, finish,
// points) - the cup run without hands. Read by game/MAIN/MainInit.c.
int g_cfg_autopilot = 0;

// --weapon-pool-empty: the medium stack pool is emptied at race start, so every
// missile, bomb, shield and warpball birth fails (probe for the weapon fix,
// upstream bf2ed389c). Read by game/MAIN/MainInit.c.
int g_cfg_weaponPoolEmpty = 0;

// --instance-pool <n>: the race instance pool gets n items instead of 128
// (probe for a full pool). Read by game/MAIN/MainInit.c.
int g_cfg_instancePool = 0;

// --level-tour <list> and --level-tour-frames <n>: several tracks in one run,
// each started from the main menu the way --level / --autoload-track start one,
// left after n race frames the way pause QUIT leaves, and the run ends after
// the last. Empty: no tour. Read by game/DebugMenu.c, beside --level.
char g_cfg_levelTour[1024] = "";
int g_cfg_levelTourFrames = 600;

// --- The VRAM copy window ---------------------------------------------------
//
// NativeRenderer_CopyVRAM took its rectangle on trust. Every other VRAM entry
// point cuts its rectangle to the 1024x512 it has to fit in - ClearVRAM does it
// on the way in, MarkVRAMDirty and MarkGpuVRAMNewer through ClipVRAMRect - and
// this one wrote first and marked afterwards, so the marking clipped a write
// that had already happened.
//
// With the disc's own data it cannot fire: every rectangle comes from a TIM
// header that was authored to fit. It is the data that guarantees this, not the
// code, and custom tracks bring their own data. What lies immediately behind
// s_vram.cpuPixels is the dirty-rectangle list, so an overrun does not fault -
// it rewrites the record of what has to be uploaded.
//
// On by default. --vram-copy-unclamped puts the old route back, so a rectangle
// that fires here can be looked at both ways from one binary.
global_variable int s_vramCopyClamp = 1;
global_variable int s_vramCopyClipped = 0;

// NOTE(aalhendi): Pack native RGBA render targets into the persistent RG8 VRAM
// texture on the GPU instead of a GPU-to-CPU-to-GPU round trip.
global_variable ShaderID s_packShader = 0;
global_variable ShaderID s_presentVramShader = 0;
global_variable ShaderID s_presentTargetShader = 0;
global_variable ShaderID s_packPagesShader = 0;
global_variable NativeGfxBuffer s_vramQuadBuffer = NATIVE_GFX_INVALID;

// The native program ("nr"), made only with --native-preview - see
// NativeRenderer_InitNativeLayer. Without the switch it stays INVALID and
// nothing below ever touches it.
//
// Its block, std140: one finished clip-from-model matrix (the depth mapping is
// already in it - see "DEPTH." in native_gfx.h), a tint, and parameters whose
// x > 0.5 means "sample slot 0".
//
// clipFromModel is stored column-major, the way a GLSL mat4 in a std140 block
// reads it: clipFromModel[column * 4 + row].
struct NativeLayerUniforms
{
	float clipFromModel[16];
	float tint[4];
	float params[4];
};

CTR_STATIC_ASSERT(sizeof(struct NativeLayerUniforms) == 96);
CTR_STATIC_ASSERT(offsetof(struct NativeLayerUniforms, tint) == 64);
CTR_STATIC_ASSERT(offsetof(struct NativeLayerUniforms, params) == 80);

global_variable ShaderID s_nativeLayerShader = NATIVE_GFX_INVALID;

// The program of the retail twin ("nrt", step 4d Z1), only with --native-preview
// --native-twin; its block, std140, field for field the NrtBlock of the shader.
struct NativeTwinUniforms
{
	float clipFromModel[16];
	float tint[4];
	float params[4];
	float far[4];
	float look[4];
	float dither[4];
	float proj[4];
};

CTR_STATIC_ASSERT(sizeof(struct NativeTwinUniforms) == 160);
CTR_STATIC_ASSERT(offsetof(struct NativeTwinUniforms, far) == 96);
CTR_STATIC_ASSERT(offsetof(struct NativeTwinUniforms, proj) == 144);

global_variable ShaderID s_nativeTwinShader = NATIVE_GFX_INVALID;

// The generated probe mesh, made only with --native-preview and --native-probe
// (NativeRenderer_InitNativeLayer), static from then on. Without both switches
// they stay INVALID and NativeRenderer_NativeProbeReady answers 0.
global_variable NativeGfxBuffer s_probeVertexBuffer = NATIVE_GFX_INVALID;
global_variable NativeGfxBuffer s_probeIndexBuffer = NATIVE_GFX_INVALID;

// The texture of the form texture, made with the mesh and only for that form;
// without it INVALID, and no image, memory or sampler exists for it.
global_variable TextureID s_probeTexture = NATIVE_GFX_INVALID;

// The form mips: its texture is made by the native texture manager while the
// race loads (NativeRenderer_LoadProbeMipsTexture), not here at start-up, and
// it is sRGB - the "nr" block then says so in params[1]. 0 for every other
// form, whose texture stays the one-level UNORM texture it always was.
global_variable int s_probeTextureManaged = 0;
global_variable int s_probeTextureSrgb = 0;

// The probe texture of the forms texture, pose and wheels uploaded in a race
// frame (platform/native_tex.c, NativeTex_InRaceFrame): it goes up once at
// start-up, before the game's first state, so this stays 0. Counted all the
// same, so the exit line of the render layer holds for every form.
global_variable unsigned int s_probeUploadsInRaceFrame = 0;

// The pose buffer of the form pose (see NATIVE_LAYER_POSE_FRAMES): a dynamic
// vertex buffer, host-visible and written straight into its mapping - no
// transfer, nothing that counts as a texture upload. Only for that form.
global_variable NativeGfxBuffer s_probePoseBuffer = NATIVE_GFX_INVALID;

// What NativeRenderer_WantNativeDepth was last asked: the main target has its
// depth image while this is 1 (the backend follows sizes and sample counts).
global_variable int s_nativeDepthWanted = 0;

// Experiment, off unless asked for: a partial fill rectangle becomes a drawn
// quad instead of a scissored clear.
//
// The PSX fill command is not a clear. It is a rectangle of a flat colour
// written into VRAM, and the game uses it as geometry - CAM_ClearScreen puts
// two of them in the last ordering-table slot as the sky, split at a horizon
// line that moves with the camera. The parser hands them to ClearImage
// (native_gpu.c, ParsePrimitiveBody), which is why they end up here.
//
// Routing them through a clear costs us twice. A clear has a whole-attachment
// failure mode that a draw does not have, and its rectangle is computed a
// second time inside the backend - once by this file in GL's bottom-up
// convention, once by the backend turning it over - where a drawn quad carries
// its corners with it and cannot disagree with anything.
//
// A whole-display fill stays a clear. There the two are the same thing and the
// clear is cheaper.
//
// This is the default. Both defects it was built to test - the sky and the
// milky water - were gone with it on and back with it off, on the same spot,
// driven both ways. The old route stays reachable with --fill-rect-as-clear so
// the two can still be put side by side.
//
// The room for a frame's fills starts here and grows if a frame ever wants
// more. It is a starting size, not a limit: a frame that runs out clears the
// rest - visibly, and counted - and the next frame has twice the room. A cap
// that bites in normal play would be a defect, not a safeguard.
#define NATIVE_FILL_QUAD_START    64
#define NATIVE_FILL_QUAD_CEILING  4096
#define NATIVE_FILL_QUAD_VERTICES 6

global_variable int s_fillRectAsDraw = 1;
global_variable NativeGfxBuffer s_fillQuadBuffer = NATIVE_GFX_INVALID;
global_variable int s_fillQuadCapacity = 0;
global_variable int s_fillQuadUsed = 0;
global_variable int s_fillQuadPeak = 0;
global_variable int s_fillQuadOverflows = 0;
global_variable int s_fillQuadOverflowReported = 0;

internal void NativeRenderer_BeginVertexFrame(void);
internal void NativeRenderer_EnsureFillQuadRoom(int quads);

internal int NativeRenderer_InitialiseDevice(char *windowName, int fullscreen);
internal void NativeRenderer_DestroyTexture(TextureID texture);
internal void NativeRenderer_SetScissorState(int enable);
internal void NativeRenderer_SetScissorRect(int x, int y, int width, int height);
internal void NativeRenderer_SetClearColor(float r, float g, float b, float a);
internal void NativeRenderer_SetViewPort(int x, int y, int width, int height);
internal void NativeRenderer_SetPresentationAspect(int width, int height);
internal void NativeRenderer_UpdatePresentationViewport(void);
internal void NativeRenderer_ClearPresentationBars(void);
internal int NativeRenderer_SetWireframe(int enable);
internal void NativeRenderer_InitRenderTarget(NativeGfxTarget *target);
internal void NativeRenderer_DestroyRenderTarget(NativeGfxTarget *target);
internal void NativeRenderer_EnsureRenderTarget(NativeGfxTarget target, int width, int height);
internal void NativeRenderer_BindMainRenderTarget(void);
internal void NativeRenderer_ApplyMsaa(void);
internal void NativeRenderer_GetDisplaySize(int *outWidth, int *outHeight);
internal void NativeRenderer_GetCanvasSize(int *outWidth, int *outHeight);
internal void NativeRenderer_DrawVRAMRegion(int x, int y, int width, int height);
internal void NativeRenderer_LoadRenderTargetFromVRAM(NativeGfxTarget target, int x, int y, int srcW, int srcH);
internal void NativeRenderer_RebindDrawTarget(void);
internal void NativeRenderer_MarkPagesDirty(int x, int y, int w, int h);
internal void NativeRenderer_FlushPageStore(void);
internal void NativeRenderer_DestroyPageAtlas(void);
internal void NativeRenderer_ReportPageStore(void);
internal void NativeRenderer_ClearPageRows(void);
internal void NativeRenderer_PublishPageStoreUniforms(void);
internal void NativeRenderer_MarkPSXUniformsDirty(void);
#if defined(CTR_INTERNAL)
internal void NativeRenderer_ResolveGpuMeasurements(b32 waitForResults);
#endif

global_variable NativeGfxBuffer s_vertexBuffer[MAX_NUM_VERTEX_BUFFERS];
global_variable int s_curVertexBuffer = 0;
global_variable int s_boundVertexBuffer = -1;

// A target over the VRAM texture, so the pack shader can draw into emulated
// VRAM and the readback can read out of it. It borrows the texture rather than
// owning one - VRAM is sampled from everywhere else.
global_variable NativeGfxTarget s_vramTarget = NATIVE_GFX_INVALID;

// Invisible game window: only with --record-preview, set in
// main.c (NativeArgs_ReadDisplayFlags), because the window comes into being in Platform_Init,
// before the big loop. Reload Studio records the preview after
// "Build container" without a window appearing that somebody could
// close. The frames come from the main target, not from the window. HIDDEN
// and not MINIMIZED: a minimised window has area 0, and with that
// the Vulkan bring-up aborts at the swapchain.
int g_cfg_windowHidden = 0;


// Brings the window and the device up, in that order.
//
// The window carries the backend's flag and cannot change its mind afterwards.
// There is one backend, so there is nothing to decide here any more - the choice
// used to be made from argv before anything was created, and what is left is the
// order: NativeGfxVK_Init wants a window created with SDL_WINDOW_VULKAN, and the
// game never says "begin" - it draws and then swaps - so the first frame is
// opened here and every later one by the swap, which ends the previous.
//
// native_gfx.h says device lifetime is still above the device line, because it
// is entangled with this file's own window ownership. Everything that draws goes
// through NativeGfx_*; bringing a device up does not, until the window moves.
internal int NativeRenderer_InitialiseDevice(char *windowName, int fullscreen)
{
	SDL_WindowFlags windowFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN;

	if (fullscreen)
	{
		windowFlags |= SDL_WINDOW_FULLSCREEN;
	}

	if (g_cfg_windowHidden)
	{
		windowFlags |= SDL_WINDOW_HIDDEN;
	}

	g_window = SDL_CreateWindow(windowName, g_windowWidth, g_windowHeight, windowFlags);

	if (g_window == NULL)
	{
		NATIVE_RENDERER_ERROR("%s\n", "Failed to initialise SDL window!");
		return 0;
	}

	if (g_cfg_windowHidden)
	{
		Platform_Log("[CTR Window] created hidden for --record-preview\n");
	}

	// The size asked for was a wish. A fullscreen window comes back the size of
	// the display, so from here on the window is asked rather than told.
	NativeRenderer_SyncWindowSize();

	// Before the device, because it needs no device: it reads the table that was
	// filled in at compile time. A hole here is a hole whatever Vulkan says
	// afterwards, and finding it before the bring-up keeps the two failures from
	// being reported as one.
	if (NativeGfx_CheckDispatch() != 0)
	{
		NATIVE_RENDERER_ERROR("%s\n", "the graphics dispatch table has empty entries - see the [CTR Gfx] lines above");
		return 0;
	}

	if (!NativeGfxVK_Init(g_window))
	{
		// Init tears itself down on every one of its failure paths, so there is
		// nothing to undo here and calling Shutdown again would be a second free.
		NATIVE_RENDERER_ERROR("%s\n", "Vulkan bring-up failed - see the [CTR Vk] lines above");
		return 0;
	}

	if (!NativeGfxVK_StartFirstFrame())
	{
		NATIVE_RENDERER_ERROR("%s\n", "Vulkan could not open its first frame");
		NativeGfxVK_Shutdown();
		return 0;
	}

	NATIVE_RENDERER_LOG("*Renderer: Vulkan on %s\n", NativeGfxVK_DeviceName());

	return 1;
}

int NativeRenderer_InitialiseRender(char *windowName, int width, int height, int fullscreen)
{
	g_windowWidth = width;
	g_windowHeight = height;

	// Due to debugging in fullscreen
	SDL_SetHint(SDL_HINT_WINDOW_ALLOW_TOPMOST, "0");

	if (!NativeRenderer_InitialiseDevice(windowName, fullscreen))
	{
		NATIVE_RENDERER_ERROR("%s\n", "Failed to initialise the graphics device!");
		return 0;
	}

	// From the world aspect, not from the window - and after the window, not
	// before it. The size this used to be computed from was the one the process
	// starts with, 800x600, which is 4:3: the right answer for the wrong reason.
	// The order is the same kind of thing - the viewport is the aspect fitted
	// into the window, and a fullscreen window is not the size it was asked for
	// until it exists.
	NativeRenderer_ApplyWorldAspectToPresentation();

	return 1;
}

void NativeRenderer_Shutdown(void)
{
	for (int i = 0; i < MAX_NUM_VERTEX_BUFFERS; i++)
	{
		NativeGfx_DestroyVertexBuffer(s_vertexBuffer[i]);
	}

	NativeRenderer_DestroyRenderTarget(&s_mainRenderTarget);
	NativeRenderer_DestroyRenderTarget(&s_offscreenRenderTarget);
	// Before the texture it borrows, and it does not take the texture with it.
	NativeGfx_DestroyTarget(s_vramTarget);
	s_vramTarget = NATIVE_GFX_INVALID;

	NativeRenderer_DestroyTexture(s_vram.texture);

	NativeRenderer_DestroyTexture(s_whiteTexture);
	NativeRenderer_DestroyTexture(s_rgLutTexture);
	NativeRenderer_DestroyPageAtlas();
	NativeGfx_DestroyProgram(s_packShader);
	NativeGfx_DestroyProgram(s_presentVramShader);
	NativeGfx_DestroyProgram(s_presentTargetShader);
	NativeGfx_DestroyProgram(s_packPagesShader);
	if (s_nativeLayerShader != NATIVE_GFX_INVALID)
	{
		NativeGfx_DestroyProgram(s_nativeLayerShader);
		s_nativeLayerShader = NATIVE_GFX_INVALID;
	}
	if (s_nativeTwinShader != NATIVE_GFX_INVALID)
	{
		NativeGfx_DestroyProgram(s_nativeTwinShader);
		s_nativeTwinShader = NATIVE_GFX_INVALID;
	}
	if (s_probeVertexBuffer != NATIVE_GFX_INVALID)
	{
		NativeGfx_DestroyVertexBuffer(s_probeVertexBuffer);
		s_probeVertexBuffer = NATIVE_GFX_INVALID;
	}
	if (s_probeIndexBuffer != NATIVE_GFX_INVALID)
	{
		NativeGfx_DestroyIndexBuffer(s_probeIndexBuffer);
		s_probeIndexBuffer = NATIVE_GFX_INVALID;
	}
	if (s_probeTexture != NATIVE_GFX_INVALID)
	{
		if (s_probeTextureManaged)
		{
			NativeTex_Destroy(s_probeTexture);
		}
		else
		{
			NativeGfx_DestroyTexture(s_probeTexture);
		}
		s_probeTexture = NATIVE_GFX_INVALID;
		s_probeTextureManaged = 0;
		s_probeTextureSrgb = 0;
	}
	if (s_probePoseBuffer != NATIVE_GFX_INVALID)
	{
		NativeGfx_DestroyVertexBuffer(s_probePoseBuffer);
		s_probePoseBuffer = NATIVE_GFX_INVALID;
	}
	NativeGfx_DestroyVertexBuffer(s_vramQuadBuffer);

	// Last, after every object above has been handed back. This also runs when
	// NativeRenderer_InitialiseRender failed: Platform_Init then calls
	// Platform_Shutdown, so the calls above may see objects that were never made.
	NativeGfxVK_Shutdown();
}

#if defined(CTR_INTERNAL)
internal void NativeRenderer_ResolveGpuMeasurements(b32 waitForResults)
{
	for (s32 i = 0; i < NATIVE_GPU_TIMER_QUERY_COUNT; i++)
	{
		struct NativeGpuTimerQuery *query = &s_gpuTimerQueries[i];
		if (!query->pending)
		{
			continue;
		}

		// Asked at exit too: the Vulkan side does not wait (a stamp
		// of the last frame, never submitted, would never finish) and gives 0 ns for
		// "not finished" - that would be a line with 0.000 ms in the CSV and
		// in the mean. What is not finished at exit is left out.
		const int available = NativeGfx_TimerReady(query->id);
		if (!available)
		{
			if (waitForResults)
			{
				query->pending = false;
			}
			continue;
		}

		const u32 elapsedNanoseconds = NativeGfx_TimerElapsedNanoseconds(query->id);
		NativePerf_RecordGpuFrame(query->frameIndex, (f64)elapsedNanoseconds / 1000000.0);
		query->pending = false;
	}
}
#endif

// The swap interval belonged to the GL context and no longer has an owner: a
// Vulkan swapchain decides its own present mode when it is created. Kept as a
// no-op rather than removed, because ResetDevice calls it on every window
// resize and every fullscreen toggle, and a present mode that could be changed
// from here is a swapchain rebuild, not a setter.
void NativeRenderer_UpdateSwapIntervalState(int swapInterval)
{
	(void)swapInterval;
}

void NativeRenderer_BeginScene(void)
{
#if defined(CTR_INTERNAL)
	NativeRenderer_ResolveGpuMeasurements(false);
	const u32 gpuFrameIndex = s_gpuTimerFrameIndex++;
	if (s_gpuTimerSupported && NativePerf_IsEnabled())
	{
		struct NativeGpuTimerQuery *query = &s_gpuTimerQueries[s_gpuTimerNextQuery];
		// Only a timer that is really running is waited for: in a frame without a
		// command buffer none starts, and this frame gets no GPU time
		// instead of an old one.
		if (!query->pending && NativeGfx_BeginTimer(query->id))
		{
			query->frameIndex = gpuFrameIndex;
			query->pending = true;
			s_gpuTimerActive = true;
			s_gpuTimerNextQuery = (s_gpuTimerNextQuery + 1) % NATIVE_GPU_TIMER_QUERY_COUNT;
		}
	}
#endif

	NativePerf_BeginScope(NATIVE_PERF_BUCKET_RENDERER_BEGIN_SCENE);
	s_lastBoundTexture = 0;

	// The fill quads of the previous frame have been submitted with it, so their
	// room is free again. If that frame wanted more than there was, take more
	// now - here, between frames, where freeing a buffer costs nothing that is
	// still in use.
	if (s_fillQuadOverflows > 0)
	{
		s_fillQuadOverflows = 0;
		s_fillQuadOverflowReported = 0;
		NativeRenderer_EnsureFillQuadRoom(s_fillQuadCapacity * 2);
	}

	s_fillQuadUsed = 0;
	NativeRenderer_ReportPageStore();
	NativeGpu_EndFrameSizeCensus();

	NativeRenderer_UpdatePresentationViewport();
	NativeRenderer_ClearPresentationBars();
	NativeRenderer_BindMainRenderTarget();

	NativeRenderer_UpdateVRAM();
	if (!activeDrawEnv.isbg)
	{
		int sourceW = 0;
		int sourceH = 0;

		// The frame starts from what is already in VRAM, which is the PREVIOUS
		// frame at PSX resolution - so above factor one this is a magnification
		// of a small picture into a large target, and everything not redrawn
		// this frame stays at that resolution. Counted, because how often it
		// happens decides whether it matters.
		s_beginSceneReloads++;
		NativeRenderer_GetDisplaySize(&sourceW, &sourceH);
		NativeRenderer_LoadRenderTargetFromVRAM(s_mainRenderTarget, activeDispEnv.disp.x, activeDispEnv.disp.y, sourceW, sourceH);
	}
	else
	{
		s_beginSceneClears++;
	}
	NativeRenderer_SetViewPort(0, 0, NativeGfx_TargetWidth(s_mainRenderTarget), NativeGfx_TargetHeight(s_mainRenderTarget));

	if (g_dbg_wireframeMode)
	{
		// The switch is held, the device decides. If it refuses, the
		// switch falls with it, otherwise the key keeps reporting "wireframe mode: 1" and
		// nothing happens - and the dark background would be the only thing one would see,
		// that is an effect that looks like a half-working tool.
		if (!NativeRenderer_SetWireframe(1))
		{
			g_dbg_wireframeMode = 0;
		}
		else
		{
			NativeGfx_ClearColor(0.1f, 0.1f, 0.1f, 1.0f);
			NativeGfx_ClearColorBuffer();
		}
	}
	NativePerf_EndScope(NATIVE_PERF_BUCKET_RENDERER_BEGIN_SCENE);
}

void NativeRenderer_EndGpuFrame(void)
{
#if defined(CTR_INTERNAL)
	if (s_gpuTimerActive)
	{
		NativeGfx_EndTimer();
		s_gpuTimerActive = false;
	}
#endif
}

void NativeRenderer_FinishGpuMeasurements(void)
{
#if defined(CTR_INTERNAL)
	NativeRenderer_EndGpuFrame();
	if (!s_gpuTimerSupported)
	{
		return;
	}

	NativeRenderer_ResolveGpuMeasurements(true);
	for (s32 i = 0; i < NATIVE_GPU_TIMER_QUERY_COUNT; i++)
	{
		NativeGfx_DestroyTimer(s_gpuTimerQueries[i].id);
	}
	SDL_memset(s_gpuTimerQueries, 0, sizeof(s_gpuTimerQueries));
	s_gpuTimerSupported = false;
#endif
}

void NativeRenderer_EndScene(void)
{
	if (s_previousOffscreenState)
	{
		NativeRenderer_SetOffscreenState(&s_previousOffscreen, 0);
	}

	if (g_dbg_wireframeMode)
	{
		NativeRenderer_SetWireframe(0);
	}

	NativeGfx_BindVertexBuffer(NATIVE_GFX_INVALID);
}

//----------------------------------------------------------------------------------------

global_variable u8 rgLUT[LUT_WIDTH * LUT_HEIGHT * sizeof(u32)];

internal int NativeRenderer_IntAbs(int value)
{
	return value < 0 ? -value : value;
}

internal int NativeRenderer_GCD(int a, int b)
{
	a = NativeRenderer_IntAbs(a);
	b = NativeRenderer_IntAbs(b);

	while (b != 0)
	{
		int t = a % b;
		a = b;
		b = t;
	}

	return a;
}

internal void NativeRenderer_SetPresentationAspect(int width, int height)
{
	const int divisor = NativeRenderer_GCD(width, height);

	if ((width <= 0) || (height <= 0) || (divisor <= 0))
	{
		return;
	}

	s_presentAspectW = width / divisor;
	s_presentAspectH = height / divisor;
}

// The shape the finished picture is shown in, taken from the world aspect so
// the two cannot disagree.
//
// They are two halves of one setting and they fail in opposite directions: the
// world scale alone gives a picture with more world in it, squeezed into a 4:3
// box; the presentation alone gives a stretched 4:3 picture. Only together do
// they come out as the same world at the same proportions with more of it
// visible.
//
// A window that changes size or goes fullscreen does NOT come through here. It
// calls NativeRenderer_ResetDevice, which recomputes the viewport from this
// aspect - the letterbox bars change, the aspect does not.
// Logical size or pixel size: on a display with a scale factor these are two
// different numbers, and the swapchain is built from the pixel one
// (SDL_GetWindowSizeInPixels, in NativeGfxVK). The viewport computed below is
// drawn into that swapchain, so it has to be the pixel one too. It was the
// logical one and came out right anyway, because the window is not created with
// SDL_WINDOW_HIGH_PIXEL_DENSITY and the two are equal while it is not - two
// independent values that happen to agree.
void NativeRenderer_SyncWindowSize(void)
{
	if (g_window == NULL)
	{
		return;
	}

	SDL_GetWindowSizeInPixels(g_window, &g_windowWidth, &g_windowHeight);
}

void NativeRenderer_ApplyWorldAspectToPresentation(void)
{
	int aspectW = 4;
	int aspectH = 3;

	// Through the canvas description instead of directly through the world aspect, so that
	// the display reads the same source as PushBuffer_Init. Today it passes
	// through - why is written at CTR_Canvas_PresentAspect in game/native_view.c.
	CTR_Canvas_PresentAspect(&aspectW, &aspectH);
	NativeRenderer_SetPresentationAspect(aspectW, aspectH);
	NativeRenderer_UpdatePresentationViewport();
}

internal void NativeRenderer_UpdatePresentationViewport(void)
{
	if ((g_windowWidth <= 0) || (g_windowHeight <= 0) || (s_presentAspectW <= 0) || (s_presentAspectH <= 0))
	{
		s_presentViewport.x = 0;
		s_presentViewport.y = 0;
		s_presentViewport.w = g_windowWidth;
		s_presentViewport.h = g_windowHeight;
		return;
	}

	int viewportW = g_windowWidth;
	int viewportH = (viewportW * s_presentAspectH) / s_presentAspectW;

	if (viewportH > g_windowHeight)
	{
		viewportH = g_windowHeight;
		viewportW = (viewportH * s_presentAspectW) / s_presentAspectH;
	}

	if (viewportW < 1)
	{
		viewportW = 1;
	}
	if (viewportH < 1)
	{
		viewportH = 1;
	}

	s_presentViewport.w = viewportW;
	s_presentViewport.h = viewportH;
	s_presentViewport.x = (g_windowWidth - viewportW) / 2;
	s_presentViewport.y = (g_windowHeight - viewportH) / 2;
}

// What the factor may be, in one function.
//
// It used to be clamped twice for one rule - once on the way in and once on
// the way out - with the same two constants written out in both. Two copies of
// a rule that agree are not safer than one; they are one edit away from
// disagreeing, and the reader cannot tell which is authoritative.
int NativeRenderer_ClampResolutionScale(int scale)
{
	if (scale < 1)
	{
		return 1;
	}
	// The one value above the ceiling that is not a factor and is therefore not
	// clamped down to one. Anything past it still is.
	if (scale == NATIVE_RES_SCALE_NATIVE)
	{
		return NATIVE_RES_SCALE_NATIVE;
	}
	if (scale > NATIVE_RES_SCALE_MAX)
	{
		return NATIVE_RES_SCALE_MAX;
	}

	return scale;
}

// Whether the setting is the position rather than a factor. Asked in four
// places; none of them may spell out the sentinel a second time.
int NativeRenderer_ResolutionIsNative(void)
{
	return g_cfg_resScale == NATIVE_RES_SCALE_NATIVE;
}

// The value that reaches the position. The menu steps onto it and the flag
// parses onto it, and neither of them may carry its own copy of the number -
// that is how the wrap-around and the ceiling drifted apart once already.
int NativeRenderer_GetNativeResolutionPosition(void)
{
	return NATIVE_RES_SCALE_NATIVE;
}

// The highest factor there is, asked for rather than written down again. The
// menu's wrap-around used to carry its own copy of the 8 - so raising this one
// would have left the menu wrapping at the old place, and the two would have
// been discovered apart by somebody pressing X.
int NativeRenderer_GetMaxResolutionScale(void)
{
	return NATIVE_RES_SCALE_MAX;
}

// The factor as a SETTING: what was asked for, which is what a config file
// writes and what a menu step adds to. Not what is in force - see
// NativeRenderer_GetEffectiveResolutionScale for that, and the note on
// NativeRenderer_TargetScale for why they are two questions.
int NativeRenderer_GetResolutionScale(void)
{
	return g_cfg_resScale;
}

void NativeRenderer_SetResolutionScale(int scale)
{
	// Takes effect at the next BeginScene, which resizes the target. Nothing is
	// touched here, so this is safe to call from the game logic.
	g_cfg_resScale = NativeRenderer_ClampResolutionScale(scale);
}

// The frame's size in game pixels, before the factor. Its own function because
// the loader's source window is measured in the same units and the target's size
// is derived from it: at factor one the two have to come out identical, and two
// copies of "and clamp it to at least one" is how they stop being.
internal void NativeRenderer_GetDisplaySize(int *outWidth, int *outHeight)
{
	int width = activeDispEnv.disp.w;
	int height = activeDispEnv.disp.h;

	if ((width <= 0) || (height <= 0))
	{
		width = activeDrawEnv.clip.w;
		height = activeDrawEnv.clip.h;
	}

	if (width < 1)
	{
		width = 1;
	}
	if (height < 1)
	{
		height = 1;
	}

	if (outWidth != NULL)
	{
		*outWidth = width;
	}
	if (outHeight != NULL)
	{
		*outHeight = height;
	}
}

// THE CANVAS: the coordinate system in which a frame is drawn.
//
// Before the canvas became wide this question did not exist, because it had the same answer as
// NativeRenderer_GetDisplaySize. Now they are two questions:
//
//   GetDisplaySize  is the FRAME BUFFER IN VRAM - 512x216, where the finished frame
//                   is packed and where it is read back from.
//   GetCanvasSize   is the CANVAS - 512, 682 or 918 columns wide, the
//                   coordinate system of the geometry and the size in which the
//                   render target is created.
//
// The height still comes from the display environment. It is not a question of format -
// 216 rows are 216 rows in all three formats - and it already has
// a source there; giving it a second one would be exactly the pattern that this
// rework removes.
//
// At 4:3 both functions return the same two numbers, and that is why no
// caller that was switched from one to the other can move a bit
// there.
internal void NativeRenderer_GetCanvasSize(int *outWidth, int *outHeight)
{
	int width = CTR_Canvas_ActiveWidth();
	int height = 0;

	NativeRenderer_GetDisplaySize(NULL, &height);

	if (width < 1)
	{
		width = 1;
	}

	if (outWidth != NULL)
	{
		*outWidth = width;
	}
	if (outHeight != NULL)
	{
		*outHeight = height;
	}
}

// THE factor. Not the one that was asked for - the one the target is actually
// allocated at, and therefore the one every rectangle in the frame has to be
// multiplied by.
//
// There used to be two answers to this question. NativeRenderer_GetResolutionScale
// clamped the setting to 1..8, and the target size clamped a second time against
// what a texture may be allocated at - and the scissor and the clear used the
// FIRST of those. The two agree at every size this game produces, which is
// exactly what made it dangerous: it is a value that is only right because two
// independent numbers happen to coincide. On a display where they did not, the
// picture would have been scissored to a box the target does not have, and
// nothing would have said so.
//
// A factor that would not fit is reported as the factor that does, once, and
// then used consistently. A limit that takes effect without a word is a bug.
internal int NativeRenderer_TargetScale(void)
{
	local_persist int s_reportedClampedScale = 0;

	int width = 0;
	int height = 0;
	const int asked = NativeRenderer_GetResolutionScale();
	int scale = asked;

	// The position is not a factor and must not walk down the loop below - at
	// 512x216 nine would fit under the ceiling and this would hand back x9, a
	// number no part of the frame is multiplied by. Handed back unchanged so
	// that every caller has to decide what it means, and none of them can
	// silently treat it as a multiplier.
	if (NativeRenderer_ResolutionIsNative())
	{
		return NATIVE_RES_SCALE_NATIVE;
	}

	// Against the canvas and not against the frame buffer: the cap limits
	// how large the render target may become, and that is created from the
	// canvas. At 43:18 it is 1.8 times as wide, so the cap bites there
	// earlier - which it is meant to.
	NativeRenderer_GetCanvasSize(&width, &height);

	while ((scale > 1) && (((width * scale) > NATIVE_RES_TARGET_MAX_PX) || ((height * scale) > NATIVE_RES_TARGET_MAX_PX)))
	{
		scale--;
	}

	if ((scale != asked) && (s_reportedClampedScale != asked))
	{
		s_reportedClampedScale = asked;
		Platform_Log("[CTR Res] x%d would need %dx%d, past the %d limit - using x%d\n", asked, width * asked, height * asked,
		             NATIVE_RES_TARGET_MAX_PX, scale);
	}

	return scale;
}

int NativeRenderer_GetEffectiveResolutionScale(void)
{
	return NativeRenderer_TargetScale();
}

// The size the main target is allocated at, and the size the VIDEO page reports.
// One place, because the number is asked for in three and a disagreement between
// any two of them is a silent geometry bug rather than a crash.
//
// At the NATIVE position this is the presentation viewport itself, which is what
// makes the last step one texel per pixel. BeginScene updates that viewport
// immediately before it binds this target, so the two cannot be a frame apart.
//
// NOT floored at the display size. A 400x300 window driven at NATIVE really does
// get a 400x300 picture, coarser than the console's own. That is honest and it
// keeps the promise this position exists for - source equals destination, one
// tap, always - where a floor would quietly break it on small windows and the
// report would be the only place it showed.
void NativeRenderer_GetMainTargetSize(int *outWidth, int *outHeight)
{
	int width = 0;
	int height = 0;

	if (NativeRenderer_ResolutionIsNative())
	{
		local_persist int s_reportedNativeCap = 0;

		width = s_presentViewport.w;
		height = s_presentViewport.h;

		if ((width > NATIVE_RES_TARGET_MAX_PX) || (height > NATIVE_RES_TARGET_MAX_PX))
		{
			if (!s_reportedNativeCap)
			{
				s_reportedNativeCap = 1;
				Platform_Log("[CTR Res] NATIVE wants %dx%d, past the %d limit - the last step stops being one to one\n", width, height,
				             NATIVE_RES_TARGET_MAX_PX);
			}

			if (width > NATIVE_RES_TARGET_MAX_PX) { width = NATIVE_RES_TARGET_MAX_PX; }
			if (height > NATIVE_RES_TARGET_MAX_PX) { height = NATIVE_RES_TARGET_MAX_PX; }
		}

		if (width < 1) { width = 1; }
		if (height < 1) { height = 1; }
	}
	else
	{
		const int scale = NativeRenderer_TargetScale();

		// FROM THE CANVAS, NOT FROM THE DISPLAY ENVIRONMENT. This is the place
		// where a wide format really becomes wide: the render target is at
		// 43:18 918 columns times the factor, and the geometry falls into it, because
		// NativeRenderer_SetProjection takes the same canvas as its right edge.
		// VRAM does NOT grow with it - NativeRenderer_StoreFrameBuffer still packs
		// into the 512-column frame buffer, and the pack shader
		// squeezes while doing so, as it always did above factor one.
		NativeRenderer_GetCanvasSize(&width, &height);
		width *= scale;
		height *= scale;
	}

	if (outWidth != NULL)
	{
		*outWidth = width;
	}
	if (outHeight != NULL)
	{
		*outHeight = height;
	}
}

// THE SIZE THE FRAME IS CURRENTLY DRAWING INTO, in target pixels.
//
// This replaces NativeRenderer_ActiveScale, and the replacement is the point:
// three call sites needed "how many target pixels is one game pixel", and all
// three got it by multiplying by a factor. A factor only answers that question
// while the target happens to be the display times a whole number. At NATIVE it
// is not, and every one of the three would have been wrong without a word - the
// scissor clipping to a box the target does not have, the clear filling the
// wrong rectangle, the dither matrix added where no five bits follow.
//
// So the size is asked for instead of derived. On the factor path it comes out
// as display times factor, which is literally the old expression, so nothing
// that worked before moves by a pixel.
//
// THE OFFSCREEN BRANCH KEEPS THE NUMBER IT HAD, deliberately. While that target
// is bound the frame draws at VRAM size, and this used to answer with the
// DISPLAY size times one - which is not the offscreen rectangle. Whether that is
// right is a separate question with its own measurement behind it; changing it
// here would fold an untested change into a tested one.
internal void NativeRenderer_ActiveViewportSize(int *outWidth, int *outHeight)
{
	if (s_previousOffscreenState)
	{
		NativeRenderer_GetDisplaySize(outWidth, outHeight);
		return;
	}

	NativeRenderer_GetMainTargetSize(outWidth, outHeight);
}

// One game-coordinate edge, mapped onto the target grid.
//
// Edges and not sizes: a width scaled on its own drifts against the position it
// starts at, so two rectangles that touch in game coordinates would overlap or
// leave a seam. Mapping both edges and taking the difference cannot do that -
// whatever one rectangle's right edge lands on is what the next one's left edge
// lands on.
//
// At a whole factor this is value * factor exactly, for every input, so the
// factor path keeps the rectangles it had down to the pixel.
internal int NativeRenderer_MapAxis(int value, int fromSize, int toSize)
{
	if (fromSize <= 0)
	{
		return value;
	}

	return (int)(((s64)value * (s64)toSize) / (s64)fromSize);
}

internal void NativeRenderer_InitRenderTarget(NativeGfxTarget *target)
{
	// Born at 1x1 and resized on first use, exactly as before: the real size
	// is not known until a display environment exists.
	const NativeGfxTargetDesc desc = {
	    .width = 1,
	    .height = 1,
	};

	*target = NativeGfx_CreateTarget(&desc);
}

internal void NativeRenderer_DestroyRenderTarget(NativeGfxTarget *target)
{
	NativeGfx_DestroyTarget(*target);
	*target = NATIVE_GFX_INVALID;
}

internal void NativeRenderer_EnsureRenderTarget(NativeGfxTarget target, int width, int height)
{
	if (width < 1)
	{
		width = 1;
	}
	if (height < 1)
	{
		height = 1;
	}

	if ((NativeGfx_TargetWidth(target) == width) && (NativeGfx_TargetHeight(target) == height))
	{
		return;
	}

	// MEASURING LINE: every resize destroys the target texture and creates a new one
	// (native_gfx_vk.c, ResizeTarget). How often that happens per run is written
	// nowhere else. Capped.
	{
		static int s_resizeLines = 0;
		if (s_resizeLines < 2000)
		{
			s_resizeLines++;
			Platform_Log("[CTR Target] target %u resized %dx%d -> %dx%d\n", (unsigned)target, NativeGfx_TargetWidth(target), NativeGfx_TargetHeight(target), width, height);
			if (s_resizeLines == 2000)
			{
				Platform_Log("[CTR Target] 2000 resize lines written - further resizes not logged\n");
			}
		}
	}

	NativeGfx_ResizeTarget(target, width, height);

	// Reallocating the colour texture invalidates whatever the renderer
	// thought was bound.
	s_lastBoundTexture = (TextureID)-1;
}

// Puts back whichever target the game was drawing into, at its full viewport.
//
// Every bridge that steps aside to draw somewhere else - the VRAM pack, the page
// store fill - has to come back to the same place, and "the same place" is two
// branches with a different target and a different size in each. It was written
// out twice; the second copy is what this replaces.
internal void NativeRenderer_RebindDrawTarget(void)
{
	if (s_previousOffscreenState)
	{
		NativeRenderer_BindTarget(s_offscreenRenderTarget);
		NativeGfx_SetViewport(0, 0, NativeGfx_TargetWidth(s_offscreenRenderTarget), NativeGfx_TargetHeight(s_offscreenRenderTarget));
	}
	else
	{
		NativeRenderer_BindMainRenderTarget();
		NativeGfx_SetViewport(0, 0, NativeGfx_TargetWidth(s_mainRenderTarget), NativeGfx_TargetHeight(s_mainRenderTarget));
	}
}

internal void NativeRenderer_BindMainRenderTarget(void)
{
	int width = 0;
	int height = 0;

	// Both numbers from one place, factor already in them. At factor one this is
	// the same pair of numbers the display environment gave before.
	NativeRenderer_GetMainTargetSize(&width, &height);

	NativeRenderer_EnsureRenderTarget(s_mainRenderTarget, width, height);
	NativeRenderer_BindTarget(s_mainRenderTarget);
}

internal void NativeRenderer_DrawVRAMRegion(int x, int y, int width, int height)
{
	NativeGfx_BindProgram(s_presentVramShader);
	const struct NativeBlitVramUniforms vramBlock = {.sourceRect = {(float)x, (float)y, (float)width, (float)height}};
	NativeGfx_UpdateUniforms(s_presentVramShader, &vramBlock);

	// NEAREST, said rather than inherited.
	//
	// This bound with KEEP - whatever filter the last draw left on the texture.
	// It came out NEAREST every time, because that is what the texture is created
	// with and nothing else ever binds it with anything, so this changes no
	// picture. It is said because it is not a preference: the texture is RG8 and
	// holds PACKED 16-BIT VRAM WORDS. Interpolating between two of those
	// interpolates bit patterns, not colours - a red next to a blue would come
	// out somewhere else entirely in the shader's lookup. This one has to be
	// nearest, and a value that has to be right is not a value to inherit.
	//
	// That is also why the VRAM route cannot be made to look smoother: the
	// filtering would have to happen after the words are decoded, not before.
	NativeGfx_BindTexture(0, s_vram.texture, NATIVE_GFX_FILTER_NEAREST);
	NativeGfx_BindVertexBuffer(s_vramQuadBuffer);
	NativeRenderer_DrawTriangles(0, 2);
}

// srcW/srcH in VRAM texels, which above factor one is NOT the target's size.
//
// This used to read the width and height straight off the target, which was the
// same number as long as the two could not differ. Scaling the target made them
// differ, and the failure would have been silent: a 4x target would have pulled
// four times as much VRAM in and shown a quarter of the picture, stretched.
internal void NativeRenderer_LoadRenderTargetFromVRAM(NativeGfxTarget target, int x, int y, int srcW, int srcH)
{
	const ShaderID previousShader = s_previousShader;
	const TextureID previousTexture = s_lastBoundTexture;
	const BlendMode previousBlendMode = s_previousBlendMode;
	const int previousScissorState = s_previousScissorState;

	if (srcW < 1)
	{
		srcW = 1;
	}
	if (srcH < 1)
	{
		srcH = 1;
	}

	NativeRenderer_UpdateVRAM();
	NativeRenderer_BindTarget(target);
	NativeGfx_SetBlendMode(BM_NONE);
	NativeGfx_SetScissorEnabled(0);
	NativeGfx_SetViewport(0, 0, NativeGfx_TargetWidth(target), NativeGfx_TargetHeight(target));
	NativeRenderer_DrawVRAMRegion(x, y, srcW, srcH);

	if (s_boundVertexBuffer >= 0)
	{
		NativeGfx_BindVertexBuffer(s_vertexBuffer[s_boundVertexBuffer]);
	}
	else
	{
		NativeGfx_BindVertexBuffer(NATIVE_GFX_INVALID);
	}
	NativeGfx_BindProgram(previousShader == (ShaderID)-1 ? 0 : previousShader);
	NativeGfx_BindTexture(0, previousTexture == (TextureID)-1 ? 0 : previousTexture, NATIVE_GFX_FILTER_KEEP);
	s_previousShader = previousShader;
	s_lastBoundTexture = previousTexture;
	s_previousBlendMode = BM_NONE;
	s_previousScissorState = 0;
	NativeRenderer_SetBlendMode(previousBlendMode);
	NativeRenderer_SetScissorState(previousScissorState);
}

internal void NativeRenderer_ClearHostRect(int x, int y, int width, int height)
{
	if ((width <= 0) || (height <= 0))
	{
		return;
	}

	NativeRenderer_SetScissorRect(x, y, width, height);
	NativeGfx_ClearColorBuffer();
}

internal void NativeRenderer_ClearPresentationBars(void)
{
	// Read back from the renderer's own record rather than the driver's. The
	// copies matter: the clears below overwrite both records as they run.
	const int previousScissorEnabled = s_previousScissorState;
	int previousScissorBox[4];
	float previousClearColor[4];
	const int viewportRight = s_presentViewport.x + s_presentViewport.w;
	const int viewportTop = s_presentViewport.y + s_presentViewport.h;

	if ((g_windowWidth <= 0) || (g_windowHeight <= 0))
	{
		return;
	}

	if ((s_presentViewport.x == 0) && (s_presentViewport.y == 0) && (s_presentViewport.w == g_windowWidth) && (s_presentViewport.h == g_windowHeight))
	{
		return;
	}

	memcpy(previousScissorBox, s_scissorRect, sizeof(previousScissorBox));
	memcpy(previousClearColor, s_clearColor, sizeof(previousClearColor));

	NativeRenderer_SetScissorState(1);
	NativeRenderer_SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);

	NativeRenderer_ClearHostRect(0, 0, s_presentViewport.x, g_windowHeight);
	NativeRenderer_ClearHostRect(viewportRight, 0, g_windowWidth - viewportRight, g_windowHeight);
	NativeRenderer_ClearHostRect(s_presentViewport.x, 0, s_presentViewport.w, s_presentViewport.y);
	NativeRenderer_ClearHostRect(s_presentViewport.x, viewportTop, s_presentViewport.w, g_windowHeight - viewportTop);

	NativeRenderer_SetScissorState(previousScissorEnabled);

	if (previousScissorEnabled)
	{
		NativeRenderer_SetScissorRect(previousScissorBox[0], previousScissorBox[1], previousScissorBox[2], previousScissorBox[3]);
	}

	NativeRenderer_SetClearColor(previousClearColor[0], previousClearColor[1], previousClearColor[2], previousClearColor[3]);
}

void NativeRenderer_ResetDevice(void)
{
	NativeRenderer_UpdatePresentationViewport();
	NativeRenderer_UpdateSwapIntervalState(0);
}

typedef struct
{
	// The eight uniform locations that used to sit here are gone with the
	// per-field path: every value the PSX shaders read now arrives as one
	// std140 block, so there is nothing left to look up by name.
	ShaderID shader;
} GTEShader;

internal ShaderID NativeRenderer_Shader_CompileWithUniforms(const char *source, const NativeGfxUniformField *uniforms, int uniformCount,
                                                            const char *blockName, int blockBytes, int blockBinding);
internal void NativeRenderer_GenerateCommonTextures(void);
internal void NativeRenderer_CompilePSXShader(GTEShader *sh, const char *source);
internal void NativeRenderer_InitialisePSXShaders(void);
internal void NativeRenderer_InitRG8LUT(void);
internal void NativeRenderer_Ortho2D(float left, float right, float bottom, float top, float znear, float zfar);
internal void NativeRenderer_SetShader(const ShaderID shader);
internal void NativeRenderer_SyncGpuVRAMToCPU(int x, int y, int w, int h);
internal void NativeRenderer_ResolveVRAMRead(int x, int y, int w, int h);
internal void NativeRenderer_GpuPackTextureToVRAM(TextureID sourceTexture, int x, int y, int w, int h, int sourceW, int sourceH, b32 flipY);

// One program samples 4, 8 and 16 bit - the format arrives per vertex
// (native_shaders.inc GPU_SAMPLE_TEXTURE_FUNC). The 32-bit override texture
// keeps its own program.
global_variable GTEShader s_gteShaderPsx;
global_variable GTEShader s_gteShader32Rgba;

// Every shader here puts its block on binding 0 - the preamble hard-codes it:
// "layout(std140, set = 0, binding = 0)" in tools/shader_spirv_probe.c. The name kept
// "PSX" while it was the only converted shader; the blit shaders are converted
// too and share it, so the spelling says what it is.
#define NATIVE_UNIFORM_BLOCK_BINDING 0

// std140. The mat4 takes 0..63, the vec2 64..71, and the four ints land on 72
// and then every 4 after; the two floats behind them fill up to 96, where the
// first vec4 starts. Get this wrong and fields land on each other without a
// word from anyone.
struct NativePSXUniforms
{
	float projection[16];
	float texelSize[2];
	int bilinearFilter;
	int psxSemiTransPass;
	int psxDrawMaskSet;
	int psxTextureOutputStp;

	// How many atlas texels one VRAM texel occupies, and the atlas geometry the
	// sampler needs: tiles across, tile size, and one over the atlas size. All
	// written by one function, from the one place that knows how big the atlas
	// is. See NativeRenderer_PublishPageStoreUniforms.
	float psxPageScale;

	// One where this draw's result will be truncated to five bits per channel,
	// zero where it will not. See GPU_DITHERING in native_shaders.inc.
	//
	// This slot was a written-out pad: std140 starts a vec4 on a 16-byte
	// boundary and C does not, so something had to sit at 92 or the two layouts
	// would part company - psxPageGrid at 92 on one side and 96 on the other,
	// with every field after it wrong by four bytes and nobody to say so. It
	// carries a value instead of a hole now, which costs nothing and moves no
	// field.
	float psxDitherAmount;

	float psxPageGrid[4];
	float psxPageLayout[4];

	// THE ORIGIN OF THE 256 WINDOW, when the texture is a host texture.
	//
	// One means: the vertex shader reads the CLUT value of the primitive as the
	// window origin in the texture and adds it to the uv. Zero means: as
	// before, uv 0..255 are the texels 0..255 and nothing else. No code path
	// sets it any more, so it stays zero; the field is kept because the shader
	// block declares it.
	//
	// Appended and not inserted: std140 puts a vec4 on 16 bytes,
	// psxPageLayout ends at 128, and 128 is itself a multiple of 16.
	// No field above moves, and the offset assertions below for the
	// fields before it stand unchanged.
	float psxUvOrigin[4];
};

// std140 puts a vec4 on a 16-byte boundary, which lands psxPageGrid at 96,
// psxPageLayout at 112, psxUvOrigin at 128 and makes the block 144. The compiler agrees by accident
// of ordering rather than by instruction, so it is asserted rather than trusted:
// a field added above psxPageScale without a matching change in
// gpu_shader_common would move every field after it and nothing would say a
// word - which is exactly how flipY and sourceRect were silently zero.
CTR_STATIC_ASSERT(sizeof(struct NativePSXUniforms) == 144);
CTR_STATIC_ASSERT(offsetof(struct NativePSXUniforms, psxPageScale) == 88);
CTR_STATIC_ASSERT(offsetof(struct NativePSXUniforms, psxPageGrid) == 96);
CTR_STATIC_ASSERT(offsetof(struct NativePSXUniforms, psxPageLayout) == 112);
CTR_STATIC_ASSERT(offsetof(struct NativePSXUniforms, psxUvOrigin) == 128);

// The state the PSX shaders draw with, accumulated rather than written through
// on every setter. One upload happens just before a draw - the point where
// Vulkan binds its descriptors, and where all the values are known to be
// current. s_psxShader names the program whose buffer to write into; it is
// whatever NativeRenderer_SetTexture last selected.
global_variable struct NativePSXUniforms s_psxUniforms;
global_variable b32 s_psxUniformsDirty = 0;
global_variable ShaderID s_psxShader = 0;

internal void NativeRenderer_MarkPSXUniformsDirty(void)
{
	s_psxUniformsDirty = 1;
}

#include "native_shaders.inc"
internal ShaderID NativeRenderer_Shader_CompileWithUniforms(const char *source, const NativeGfxUniformField *uniforms, int uniformCount,
                                                            const char *blockName, int blockBytes, int blockBinding)
{
	// No preamble, no source assembly. Vulkan cannot compile GLSL at runtime and
	// finds a prebuilt SPIR-V module by name, so what the device needs from this
	// function is the name and nothing else. The GL flavour of the preamble used
	// to be built here and handed over beside it; it went with the backend that
	// read it.
	//
	// The bodies stay in native_shaders.inc, and are still the single source the
	// build compiles from - tools/shader_spirv_probe.c assembles them under the
	// Vulkan preamble and glslangValidator turns them into the modules linked in.
	// What they are used for HERE is identity: the pointer the caller passed is
	// compared against the very objects in that file to work out which module it
	// means. Comparing pointers is exact where comparing text would not be.
	//
	// The identification happens here rather than in the backend: the bodies
	// are declared by native_shaders.inc, which this file includes, and the
	// device layer sits earlier in the unity build where they are not visible.
	//
	// The names are the ones tools/shader_spirv_probe.c writes.
	const char *moduleName = "blit";

	{
		// The PSX shaders, one module each.
		//
		// There were two, a plain and a "_bilinear", picked between here by the
		// filtering flag as it stood at startup. They were bit-identical SPIR-V:
		// the probe wrote the bilinear one with #define BILINEAR_FILTER in its
		// preamble and NOTHING in the shader source reads that name. What actually
		// decides is the bilinearFilter uniform, in the shader's own
		// `(bilinearFilter > 0) ? bilinear : nearest`, which the debug key moves at
		// runtime.
		//
		// So the filter had two answers: a module chosen once at startup that could
		// not change, and a uniform that could - and only one of them was ever
		// read. Eight modules compiled where four are used. The dead half is gone;
		// the uniform is the setting.
		const struct
		{
			const char *body;
			const char *name;
		} psxModules[] = {
		    {gte_shader_psx, "psx"},
		    {gte_shader_32_rgba, "psx32"},
		};

		for (int i = 0; i < (int)(sizeof(psxModules) / sizeof(psxModules[0])); i++)
		{
			if (source == psxModules[i].body)
			{
				moduleName = psxModules[i].name;
				break;
			}
		}

		// The blit shaders, from the list native_shaders.inc shares with
		// tools/shader_spirv_probe.c so that a name here and a module there
		// cannot disagree.
		{
			const char *blitName = NULL;
			const char *blitBody = NULL;

			for (int i = 0; CtrBlitShader_Get(i, &blitName, &blitBody); i++)
			{
				if (source == blitBody)
				{
					moduleName = blitName;
					break;
				}
			}
		}
	}

	const NativeGfxProgramDesc desc = {
	    .attribs = {
	        {.slot = a_position, .name = "a_position"},
	        {.slot = a_texcoord, .name = "a_texcoord"},
	        {.slot = a_color, .name = "a_color"},
	        {.slot = a_extra, .name = "a_extra"},
	    },
	    .attribCount = 4,

	    // Slot 1 holds the immutable colour lookup table, slot 0 everything
	    // else. Constant for the lifetime of the program, so set at creation.
	    //
	    // The list names every sampler any shader here declares, not just the
	    // ones this shader has - a name the program does not use links away and
	    // the backend skips it. That is cheaper than threading a per-shader
	    // list through the compile call, and it is what lets the pack shader's
	    // s_src arrive the same way as the others instead of through its own
	    // glUniform1i at the call site.
	    .samplers = {
	        {.slot = 0, .name = "s_texture"},
	        {.slot = 1, .name = "s_rgLut"},
	        {.slot = 0, .name = "s_src"},
	        {.slot = 2, .name = "s_pages"},
	    },
	    .samplerCount = 4,

	    .debugName = moduleName,
	};

	// Copied rather than pointed at: the descriptor owns its layout, so a
	// caller can build the field list on the stack.
	NativeGfxProgramDesc withUniforms = desc;

	// A converted shader declares a block and takes the buffer path; an
	// unconverted one still hands over a field list. Never both.
	if (blockName != NULL)
	{
		withUniforms.uniformBlockName = blockName;
		withUniforms.uniformBlockBytes = blockBytes;
		withUniforms.uniformBlockBinding = blockBinding;
	}
	else if ((uniforms != NULL) && (uniformCount > 0))
	{
		if (uniformCount > NATIVE_GFX_MAX_PROGRAM_SLOTS)
		{
			uniformCount = NATIVE_GFX_MAX_PROGRAM_SLOTS;
		}

		memcpy(withUniforms.uniforms, uniforms, (size_t)uniformCount * sizeof(uniforms[0]));
		withUniforms.uniformCount = uniformCount;
	}

	return NativeGfx_CreateProgram(&withUniforms);
}

//--------------------------------------------------------------------------------------------

internal void NativeRenderer_GenerateCommonTextures(void)
{
	u32 whitePixelData = 0xFFFFFFFF;

	const NativeGfxTextureDesc whiteDesc = {
	    .width = 1,
	    .height = 1,
	    .format = NATIVE_GFX_TEXFMT_RGBA8,
	    .filter = NATIVE_GFX_FILTER_NEAREST,
	    .wrap = NATIVE_GFX_WRAP_REPEAT,
	    .pixels = &whitePixelData,
	};
	s_whiteTexture = NativeGfx_CreateTexture(&whiteDesc);

	const NativeGfxTextureDesc lutDesc = {
	    .width = LUT_WIDTH,
	    .height = LUT_HEIGHT,
	    .format = NATIVE_GFX_TEXFMT_RGBA8,
	    .filter = NATIVE_GFX_FILTER_NEAREST,
	    .wrap = NATIVE_GFX_WRAP_CLAMP,
	    .pixels = &rgLUT,
	};
	s_rgLutTexture = NativeGfx_CreateTexture(&lutDesc);

	// Texture unit 1 is reserved for the immutable PSX color lookup table. The
	// bind leaves unit 0 active again on its own.
	NativeGfx_BindTexture(1, s_rgLutTexture, NATIVE_GFX_FILTER_NEAREST);
}

internal void NativeRenderer_CompilePSXShader(GTEShader *sh, const char *source)
{
	sh->shader = NativeRenderer_Shader_CompileWithUniforms(source, NULL, 0, "PsxBlock", (int)sizeof(struct NativePSXUniforms),
	                                                       NATIVE_UNIFORM_BLOCK_BINDING);
}

internal void NativeRenderer_InitialisePSXShaders(void)
{
	NativeRenderer_CompilePSXShader(&s_gteShaderPsx, gte_shader_psx);
	NativeRenderer_CompilePSXShader(&s_gteShader32Rgba, gte_shader_32_rgba);
}

internal void NativeRenderer_InitVRAMPipelines(void)
{
	local_persist const float quad[12] = {-1.f, -1.f, -1.f, 1.f, 1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f, -1.f};

	// s_src arrives through the sampler list at creation now, like every other
	// sampler, instead of through its own glUniform1i here.
	s_packShader = NativeRenderer_Shader_CompileWithUniforms(ctr_pack_shader, NULL, 0, "ResolveBlock", (int)sizeof(struct NativeBlitResolveUniforms),
	                                                        NATIVE_UNIFORM_BLOCK_BINDING);

	s_presentVramShader = NativeRenderer_Shader_CompileWithUniforms(ctr_present_vram_shader, NULL, 0, "VramBlock",
	                                                               (int)sizeof(struct NativeBlitVramUniforms), NATIVE_UNIFORM_BLOCK_BINDING);

	// Same block as the pack shader, and deliberately so - see the shader.
	s_presentTargetShader = NativeRenderer_Shader_CompileWithUniforms(ctr_present_target_shader, NULL, 0, "ResolveBlock",
	                                                                 (int)sizeof(struct NativeBlitResolveUniforms), NATIVE_UNIFORM_BLOCK_BINDING);

	s_packPagesShader = NativeRenderer_Shader_CompileWithUniforms(ctr_pack_pages_shader, NULL, 0, "PagesBlock",
	                                                             (int)sizeof(struct NativeBlitPagesUniforms), NATIVE_UNIFORM_BLOCK_BINDING);

	const NativeGfxVertexBufferDesc quadDesc = {
	    .bytes = sizeof(quad),
	    .dynamic = 0,
	    .initial = quad,
	    .stride = 2 * sizeof(float),
	    .attribCount = 1,
	    .attribs = {
	        {.slot = a_position, .components = 2, .type = NATIVE_GFX_ATTR_FLOAT32, .offset = 0},
	    },
	};
	s_vramQuadBuffer = NativeGfx_CreateVertexBuffer(&quadDesc);

	NativeRenderer_EnsureFillQuadRoom(NATIVE_FILL_QUAD_START);
}

// Its own buffer, and room for a frame's fills side by side.
//
// Not the shared vertex buffer: that one is written once per draw batch and the
// two that exist take turns, which is what keeps a write from landing on
// geometry an already-recorded draw still has to read. Appending fills to it
// would break that arithmetic. Appending them to a buffer of their own, at a
// fresh offset each time and reset once a frame, keeps every fill's corners
// standing until the frame is submitted.
//
// Only ever called between frames, never while fills are outstanding: the
// device drains the queue before it frees a buffer, and doing that in the
// middle of a frame would stall for nothing.
internal void NativeRenderer_EnsureFillQuadRoom(int quads)
{
	if (quads > NATIVE_FILL_QUAD_CEILING)
	{
		quads = NATIVE_FILL_QUAD_CEILING;
	}

	if ((s_fillQuadBuffer != NATIVE_GFX_INVALID) && (quads <= s_fillQuadCapacity))
	{
		return;
	}

	const NativeGfxVertexBufferDesc fillDesc = {
	    .bytes = (int)sizeof(GrVertex) * quads * NATIVE_FILL_QUAD_VERTICES,
	    .dynamic = 1,
	    .initial = NULL,
	    .stride = sizeof(GrVertex),
	    .attribCount = 4,
	    .attribs = {
	        {.slot = a_position, .components = 4, .type = NATIVE_GFX_ATTR_SINT16, .offset = offsetof(GrVertex, x)},
	        {.slot = a_texcoord, .components = 4, .type = NATIVE_GFX_ATTR_UINT8, .offset = offsetof(GrVertex, u)},
	        {.slot = a_color, .components = 4, .type = NATIVE_GFX_ATTR_UNORM8, .offset = offsetof(GrVertex, r)},
	        {.slot = a_extra, .components = 4, .type = NATIVE_GFX_ATTR_SINT8, .offset = offsetof(GrVertex, tcx)},
	    },
	};

	const NativeGfxBuffer grown = NativeGfx_CreateVertexBuffer(&fillDesc);

	if (grown == NATIVE_GFX_INVALID)
	{
		// Keep what we have. A smaller buffer that works beats none.
		Platform_LogError("[CTR Native] room for %d fill rectangles could not be taken, staying at %d\n", quads, s_fillQuadCapacity);
		return;
	}

	if (s_fillQuadBuffer != NATIVE_GFX_INVALID)
	{
		NativeGfx_DestroyVertexBuffer(s_fillQuadBuffer);
		Platform_Log("[CTR Native] room for fill rectangles raised from %d to %d\n", s_fillQuadCapacity, quads);
	}

	s_fillQuadBuffer = grown;
	s_fillQuadCapacity = quads;
}

internal void NativeRenderer_InitRG8LUT(void)
{
	for (u16 y = 0; y < LUT_HEIGHT; y++)
	{
		u8 *row = rgLUT + y * (LUT_WIDTH * 4);
		for (u16 x = 0; x < LUT_WIDTH; x++)
		{
			const u16 c = (y << 8) | x;
			u8 *pixel = row + x * 4;
			pixel[0] = (u8)((c & 31) << 3);
			pixel[1] = (u8)(((c >> 5) & 31) << 3);
			pixel[2] = (u8)(((c >> 10) & 31) << 3);
			pixel[3] = (u8)(((c >> 15) & 1) << 7);
		}
	}
}

// The native render layer, as far as it exists so far: with --native-preview,
// the native program, and with --native-probe as well the probe mesh that
// NativeRenderer_DrawNativeProbe draws (and for the form texture its texture).
// The switch exists so all of it can be checked to leave every PSX pixel as it
// was before anything draws with it.
//
// Without the switch this returns in its first line, so a run without it makes
// no program, no buffer, no texture and no log line it did not make before.
//
// No depth image here. The main target gets one only while a native object is
// bound - NativeRenderer_WantNativeDepth, asked by the render layer - so a
// target nothing native is drawn into (the boot target, the menus, a run with
// --native-preview but without a probe) never carries one.
//
// Called once, after NativeRenderer_ApplyMsaa, at start-up: the first frame is
// open but nothing has been drawn into it yet (see the texture below).
internal void NativeRenderer_InitNativeLayer(void)
{
	extern int g_cfg_nativePreview;
	if (!g_cfg_nativePreview)
	{
		return;
	}

	s_nativeLayerShader = NativeRenderer_Shader_CompileWithUniforms(ctr_native_layer_shader, NULL, 0, "NrBlock", (int)sizeof(struct NativeLayerUniforms),
	                                                               NATIVE_UNIFORM_BLOCK_BINDING);

	Platform_Log("[CTR Res] native preview: program 'nr' %s, main target depth on demand (only while a native object is bound)\n",
	             (s_nativeLayerShader != NATIVE_GFX_INVALID) ? "ready" : "missing");

	// The retail twin's own program, only with --native-twin (step 4d Z1).
	{
		extern int g_cfg_nativeTwin;

		if (g_cfg_nativeTwin)
		{
			s_nativeTwinShader = NativeRenderer_Shader_CompileWithUniforms(ctr_native_twin_shader, NULL, 0, "NrtBlock", (int)sizeof(struct NativeTwinUniforms),
			                                                              NATIVE_UNIFORM_BLOCK_BINDING);
			Platform_Log("[CTR Res] native twin: program 'nrt' %s (the retail look V1)\n", (s_nativeTwinShader != NATIVE_GFX_INVALID) ? "ready" : "missing");
		}
	}

	// The probe mesh, only when asked for. Made here, in the load phase, because
	// a static buffer is filled through a one-shot that waits for the queue - a
	// frame never makes one. The layout is the one nr.vert reads: position at
	// location 0, texture coordinate at 1, colour at 2. The form texture has the
	// same positions and indices, white vertices and texture coordinates.
	if (g_cfg_nativeProbe != NATIVE_PROBE_NONE)
	{
		const NativeGfxVertexBufferDesc probeVertexDesc = {
		    .bytes = NATIVE_PROBE_VERTEX_COUNT * (int)sizeof(struct NativeProbeVertex),
		    .dynamic = 0,
		    .initial = NATIVE_PROBE_TEXTURED(g_cfg_nativeProbe) ? NativeProbe_TexturedVertices() : NativeProbe_Vertices(),
		    .stride = (int)sizeof(struct NativeProbeVertex),
		    .attribCount = 3,
		    .attribs = {
		        {.slot = a_position, .components = 3, .type = NATIVE_GFX_ATTR_FLOAT32, .offset = offsetof(struct NativeProbeVertex, position)},
		        {.slot = a_texcoord, .components = 2, .type = NATIVE_GFX_ATTR_FLOAT32, .offset = offsetof(struct NativeProbeVertex, texcoord)},
		        {.slot = a_color, .components = 4, .type = NATIVE_GFX_ATTR_UNORM8, .offset = offsetof(struct NativeProbeVertex, color)},
		    },
		};
		const NativeGfxIndexBufferDesc probeIndexDesc = {
		    .bytes = NATIVE_PROBE_INDEX_COUNT * (int)sizeof(u16),
		    .type = NATIVE_GFX_INDEX_U16,
		    .initial = NativeProbe_Indices(),
		};

		s_probeVertexBuffer = NativeGfx_CreateVertexBuffer(&probeVertexDesc);
		s_probeIndexBuffer = NativeGfx_CreateIndexBuffer(&probeIndexDesc);

		// The form pose draws its morphed mesh from a dynamic buffer of the
		// same layout, one region per frame in work and item; nothing is
		// written into it before the first draw.
		if (g_cfg_nativeProbe == NATIVE_PROBE_POSE)
		{
			NativeGfxVertexBufferDesc poseDesc = probeVertexDesc;

			poseDesc.bytes = NATIVE_LAYER_POSE_FRAMES * NATIVE_LAYER_POSE_SLOTS * NATIVE_PROBE_VERTEX_COUNT * (int)sizeof(struct NativeProbeVertex);
			poseDesc.dynamic = 1;
			poseDesc.initial = NULL;
			s_probePoseBuffer = NativeGfx_CreateVertexBuffer(&poseDesc);
		}
	}

	// THE PROBE TEXTURE, only for the form texture. RGBA8 UNORM, the format the
	// main target has as well: a texel read with NEAREST is the stored byte over
	// 255, the shader multiplies it by the white vertex colour (exactly 1.0) and
	// the UNORM target stores the same byte again - no conversion anywhere in
	// between. An sRGB texture would hand the shader the linear value (128
	// becomes about 55) and the UNORM target would keep that darker value.
	//
	// nativeLayer gives it a sampler of its own (nearest, clamped, one level),
	// never one of the samplers the PSX textures share, and makes the backend
	// upload it through a one-shot transfer instead of into the open frame. That
	// happens here and only here, once per run: at start-up, in the first frame,
	// before the game has drawn anything into it. Like every one-shot it first
	// submits what the frame holds so far (here only the uploads of the
	// renderer's own start-up textures) and waits. Kept for the whole run; the
	// texture never changes. These forms stay at start-up so that their pictures
	// stay the ones measured before (round 3 and step 3b).
	//
	// The form mips is the one exception: its texture is made at the race's
	// loading screen, in stage 5 of the ten loading stages right after the
	// custom seats are armed (game/LOAD/LOAD_TenStages.c, after
	// NativeChar_ArmSeats; NativeRenderer_LoadProbeMipsTexture), as every
	// native driver texture will be. The frame is open there with the draws of
	// the loading screen recorded; the waited one-shot of the texture manager
	// first submits them and waits (NativeGfxVK_CreateTextureLevels), so the
	// pass ends in the middle of that picture and the next draw opens it again
	// with LOAD - no pixel already drawn changes, and no race frame is drawn.
	if (NATIVE_PROBE_TEXTURED(g_cfg_nativeProbe) && !g_cfg_nativeProbeMips)
	{
		const NativeGfxTextureDesc probeTextureDesc = {
		    .width = NATIVE_PROBE_TEXTURE_SIZE,
		    .height = NATIVE_PROBE_TEXTURE_SIZE,
		    .format = NATIVE_GFX_TEXFMT_RGBA8,
		    .filter = NATIVE_GFX_FILTER_NEAREST,
		    .wrap = NATIVE_GFX_WRAP_CLAMP,
		    .pixels = NativeProbe_TexturePixels(),
		    .nativeLayer = 1,
		};

		if (NativeTex_InRaceFrame())
		{
			s_probeUploadsInRaceFrame++;
		}
		s_probeTexture = NativeGfx_CreateTexture(&probeTextureDesc);
		Platform_Log("[CTR Res] native probe texture: %dx%d RGBA8 UNORM, nearest, %s\n", NATIVE_PROBE_TEXTURE_SIZE, NATIVE_PROBE_TEXTURE_SIZE,
		             (s_probeTexture != NATIVE_GFX_INVALID) ? "uploaded" : "missing");
	}
}

// THE TEXTURE OF THE FORM MIPS (renderer plan D.4, step 4a): 256x256 with its 9
// levels, each one colour (platform/native_probe.c), sRGB, through the native
// texture manager (platform/native_tex.c) - sampled as --native-filter says,
// uploaded through the waited one-shot, the staging growth given back after it.
// Called at stage 5 of the ten loading stages, right after the custom seats
// are armed (game/LOAD/LOAD_TenStages.c, after NativeChar_ArmSeats): a loading
// screen, never a race frame. Only for a race track (levelID below
// NITRO_COURT, the arcade tracks the probe binds on); the boot crate and the
// menu levels load without it. Once per run: a texture that is there already
// is not made again, and it is kept until shutdown. Until it exists,
// NativeRenderer_NativeProbeReady answers 0, so the probe is not bound and the
// render layer names "probe mesh not ready" as the reason (in the menu frames
// before the race as well, where it named "main menu" before). Returns 1 when
// the texture exists afterwards.
int NativeRenderer_LoadProbeMipsTexture(int levelID)
{
	extern int g_cfg_nativePreview;
	const u8 *levels[NATIVE_PROBE_MIPS_LEVELS];
	NativeTexDesc desc;
	NativeTexResult result = NATIVE_TEX_OK;
	int level;

	if (!g_cfg_nativePreview || !g_cfg_nativeProbeMips || (s_probeTexture != NATIVE_GFX_INVALID))
	{
		return (s_probeTexture != NATIVE_GFX_INVALID) ? 1 : 0;
	}
	if ((levelID < 0) || (levelID >= NITRO_COURT))
	{
		return 0;
	}

	for (level = 0; level < NATIVE_PROBE_MIPS_LEVELS; level++)
	{
		levels[level] = NativeProbe_MipsLevel(level);
	}

	memset(&desc, 0, sizeof(desc));
	desc.width = NATIVE_PROBE_TEXTURE_SIZE;
	desc.height = NATIVE_PROBE_TEXTURE_SIZE;
	desc.flags = 0u; // sRGB colour, levels as given
	desc.wrapU = NATIVE_GFX_WRAP_CLAMP;
	desc.wrapV = NATIVE_GFX_WRAP_CLAMP;
	desc.name = "probe mips";

	s_probeTexture = NativeTex_CreateFromLevels(&desc, levels, NATIVE_PROBE_MIPS_LEVELS, &result);
	s_probeTextureManaged = (s_probeTexture != NATIVE_GFX_INVALID) ? 1 : 0;
	s_probeTextureSrgb = s_probeTextureManaged ? NativeTex_IsSrgb(s_probeTexture) : 0;

	Platform_Log("[CTR Res] native probe mips texture: %dx%d RGBA8 SRGB, %d levels, filter %s, %s\n", NATIVE_PROBE_TEXTURE_SIZE,
	             NATIVE_PROBE_TEXTURE_SIZE, NATIVE_PROBE_MIPS_LEVELS, NativeTex_FilterName(g_cfg_nativeFilter),
	             s_probeTextureManaged ? "uploaded" : NativeTex_ResultName(result));

	return s_probeTextureManaged;
}

// Probe texture uploads in a race frame, every form: the start-up texture of
// the forms texture, pose and wheels, and every upload of the manager (the form
// mips). The render layer's exit line.
unsigned int NativeRenderer_ProbeUploadsInRaceFrame(void)
{
	return s_probeUploadsInRaceFrame + (unsigned int)NativeTex_UploadsInRaceFrame();
}

int NativeRenderer_NativeProbeReady(void)
{
	if (NATIVE_PROBE_TEXTURED(g_cfg_nativeProbe) && (s_probeTexture == NATIVE_GFX_INVALID))
	{
		return 0;
	}
	if ((g_cfg_nativeProbe == NATIVE_PROBE_POSE) && (s_probePoseBuffer == NATIVE_GFX_INVALID))
	{
		return 0;
	}

	return (s_probeVertexBuffer != NATIVE_GFX_INVALID) && (s_probeIndexBuffer != NATIVE_GFX_INVALID) && (s_nativeLayerShader != NATIVE_GFX_INVALID);
}

// The depth image of the main target, only while something native is drawn
// into it. Called by the render layer once per frame from its pull, which runs
// in the game's frame before the queue and before the ordering table of the
// frame is parsed and drawn; only a change of the wish does anything.
//
// WHEN THE IMAGE IS MADE, AND WHY NO PIXEL CHANGES: NativeGfx_SetTargetDepth
// ends a pass that is open on the main target (stored, as at every target
// switch of a frame), makes the image and clears it to far through a one-shot
// of its own that touches nothing but the new image, and the next draw opens
// the pass again with LOAD and the depth attachment beside the colour. The
// colour of everything drawn so far is loaded as it was stored, and the PSX
// draws keep depth test and write off - the same pipelines they had in every
// run with --native-preview before, in which every pass had depth from the
// start. Letting go (0) defers the destruction until no frame in flight can
// name the image.
void NativeRenderer_WantNativeDepth(int want)
{
	extern int g_cfg_nativePreview;

	want = (want != 0) ? 1 : 0;
	if (!g_cfg_nativePreview || (want == s_nativeDepthWanted))
	{
		return;
	}

	s_nativeDepthWanted = want;
	NativeGfx_SetTargetDepth(s_mainRenderTarget, want);
}

void NativeRenderer_NativeTextureUploads(unsigned int *uploads, unsigned int *duringFrame)
{
	void NativeGfxVK_NativeTextureUploads(unsigned int *uploads, unsigned int *duringFrame);

	NativeGfxVK_NativeTextureUploads(uploads, duringFrame);
}

// The scissor box in force, in the convention NativeRenderer_SetScissorRect
// takes (target pixels, rows from the bottom), or the whole main target when
// the scissor is off. Read from the record rather than computed a second time:
// right after NativeRenderer_SetupClipMode the record IS the box that function
// worked out, so the two cannot disagree by a rounding.
internal void NativeRenderer_ScissorBoxInForce(int *outX, int *outY, int *outW, int *outH)
{
	if (s_previousScissorState)
	{
		*outX = s_scissorRect[0];
		*outY = s_scissorRect[1];
		*outW = s_scissorRect[2];
		*outH = s_scissorRect[3];
		return;
	}

	*outX = 0;
	*outY = 0;
	*outW = NativeGfx_TargetWidth(s_mainRenderTarget);
	*outH = NativeGfx_TargetHeight(s_mainRenderTarget);
}

// The probe drawn as one split of its own, in the place of its marker in the
// ordering table.
//
// Target, scissor and projection come from the same three calls a PSX split
// makes, so the probe lands on the same grid as the triangles around it. Its
// matrix is the caller's screenFromModel with that projection in front:
// screenFromModel gives (sx*w, sy*w, zNear, w) with sx, sy in PSX screen pixels,
// and the PSX projection is an orthographic map of those pixels - x and y scale
// and move, z is left as zNear, w stays the view depth. So depth = zNear / w,
// the convention of the "DEPTH." block in native_gfx.h.
//
// Afterwards every state this changed behind the renderer's back is put back
// or marked unknown, so the next PSX split cannot draw with any of it:
//   draw state      NULL - depth, cull and colour mask as every PSX draw has them
//   vertex buffer   the PSX buffer bound again, as after the other detours
//   program         unknown, so the next NativeRenderer_SetShader binds again
//   blend           BM_NONE, which is what the device has now
//   PSX uniforms    dirty, so the next PSX draw uploads its block again
//   slot 0          only for the form texture, which binds its texture there:
//                   the texture of the renderer's record again
// Sample shading is not touched here. Scissor, viewport and target are the
// ones a PSX split of this clip has, and the next split sets its own anyway.
int NativeRenderer_DrawNativeProbe(const struct NativeLayerDraw *draw, const RECT16 *clip, const DISPENV *dispenv, int onScreen)
{
	if (!NativeRenderer_NativeProbeReady() || (clip->w <= 0) || (clip->h <= 0))
	{
		return 0;
	}

	// Only into the main target with its depth image. Offscreen (dfe = 0) or
	// without depth the body would be drawn without a depth test, inside out -
	// so nothing is drawn, and the caller counts it as not drawn.
	if (!onScreen || !NativeGfx_TargetDepth(s_mainRenderTarget))
	{
		return 0;
	}

	NativeRenderer_SetupClipMode(clip, dispenv, onScreen);
	NativeRenderer_SetOffscreenState(clip, !onScreen);
	NativeRenderer_SetProjection(clip, dispenv, !onScreen);

	// Both matrices column-major: element (row, column) at column * 4 + row. The
	// projection is {a,0,0,0, 0,b,0,0, 0,0,c,0, tx,ty,tz,1}; its z row is not
	// used, the depth row of screenFromModel passes through unchanged.
	struct NativeLayerUniforms block;
	{
		const float *p = s_psxUniforms.projection;
		const float *s = draw->screenFromModel;

		for (int column = 0; column < 4; column++)
		{
			const double s0 = (double)s[column * 4 + 0];
			const double s1 = (double)s[column * 4 + 1];
			const double s2 = (double)s[column * 4 + 2];
			const double s3 = (double)s[column * 4 + 3];

			block.clipFromModel[column * 4 + 0] = (float)((double)p[0] * s0 + (double)p[12] * s3);
			block.clipFromModel[column * 4 + 1] = (float)((double)p[5] * s1 + (double)p[13] * s3);
			block.clipFromModel[column * 4 + 2] = (float)s2;
			block.clipFromModel[column * 4 + 3] = (float)s3;
		}

		block.tint[0] = 1.0f;
		block.tint[1] = 1.0f;
		block.tint[2] = 1.0f;
		block.tint[3] = 1.0f;

		// params.x = 1 lets the texture in slot 0 colour the body (form
		// texture); 0 keeps the sampler out of the colour. params.y = 1 says
		// the texture is sRGB (only the form mips): the shader turns its sample
		// back to gamma first. 0, as before, for every other form.
		block.params[0] = (s_probeTexture != NATIVE_GFX_INVALID) ? 1.0f : 0.0f;
		block.params[1] = s_probeTextureSrgb ? 1.0f : 0.0f;
		// params.z > 0: --native-depth-tint (0 for every other draw).
		block.params[2] = draw->depthTint;
		block.params[3] = 0.0f;
	}

	// The first native draw of a view in a frame starts from far: what an
	// earlier view left in the depth image is not this view's. Only inside the
	// split's own box, so no other view loses its depth.
	if (draw->clearDepth)
	{
		int boxX = 0;
		int boxY = 0;
		int boxW = 0;
		int boxH = 0;

		NativeRenderer_ScissorBoxInForce(&boxX, &boxY, &boxW, &boxH);
		NativeGfx_ClearDepth(boxX, boxY, boxW, boxH);
	}

	{
		const NativeGfxDrawState state = {
		    .depthTest = 1,
		    .depthWrite = 1,
		    .depthCompare = NATIVE_GFX_COMPARE_GREATER_OR_EQUAL,
		    .cull = (NativeGfxCull)draw->cull,
		    .colorWriteOff = NATIVE_GFX_COLOR_A,
		};

		NativeGfx_SetBlendMode(BM_NONE);
		NativeGfx_SetDrawState(&state);
		if (s_probeTexture != NATIVE_GFX_INVALID)
		{
			// Its own filter stands (nearest); the sampler is the native one -
			// for the form mips the one of its sampling (--native-filter).
			NativeGfx_BindTexture(0, s_probeTexture, NATIVE_GFX_FILTER_KEEP);
		}
		NativeGfx_BindProgram(s_nativeLayerShader);
		NativeGfx_UpdateUniforms(s_nativeLayerShader, &block);

		// The form pose: the morphed mesh into its own region of the pose
		// buffer (frame in work and item), drawn from there through the same
		// indices with the region as vertex offset. Writing does not bind with
		// the explicit binding the renderer uses, and the PSX buffer is bound
		// again below in any case.
		if ((draw->vertices != NULL) && (s_probePoseBuffer != NATIVE_GFX_INVALID) && (draw->vertexRegion >= 0) &&
		    (draw->vertexRegion < (NATIVE_LAYER_POSE_FRAMES * NATIVE_LAYER_POSE_SLOTS)))
		{
			const int meshBytes = NATIVE_PROBE_VERTEX_COUNT * (int)sizeof(struct NativeProbeVertex);

			NativeGfx_UpdateVertexBuffer(s_probePoseBuffer, draw->vertexRegion * meshBytes, meshBytes, draw->vertices);
			NativeGfx_BindVertexBuffer(s_probePoseBuffer);
			NativeGfx_BindIndexBuffer(s_probeIndexBuffer);
			NativeGfx_DrawIndexed(0, NATIVE_PROBE_INDEX_COUNT, draw->vertexRegion * NATIVE_PROBE_VERTEX_COUNT);
		}
		else
		{
			NativeGfx_BindVertexBuffer(s_probeVertexBuffer);
			NativeGfx_BindIndexBuffer(s_probeIndexBuffer);
			NativeGfx_DrawIndexed(0, NATIVE_PROBE_INDEX_COUNT, 0);
		}
	}

	// Back to what the PSX path relies on - in this order, see the note above.
	NativeGfx_SetDrawState(NULL);
	if (s_boundVertexBuffer >= 0)
	{
		NativeGfx_BindVertexBuffer(s_vertexBuffer[s_boundVertexBuffer]);
	}
	else
	{
		NativeGfx_BindVertexBuffer(NATIVE_GFX_INVALID);
	}
	s_previousShader = (ShaderID)-1;
	s_previousBlendMode = BM_NONE;
	NativeRenderer_MarkPSXUniformsDirty();

	// Slot 0 back to the PSX texture of the renderer's record, as after the
	// other detours that bind a texture of their own (LoadRenderTargetFromVRAM);
	// the record itself stays, because slot 0 holds that texture again.
	if (s_probeTexture != NATIVE_GFX_INVALID)
	{
		NativeGfx_BindTexture(0, (s_lastBoundTexture == (TextureID)-1) ? 0 : s_lastBoundTexture, NATIVE_GFX_FILTER_KEEP);
	}

	return 1;
}

// What psxDitherAmount of the PSX block would be for a draw into the target in
// force now: the expression of NativeRenderer_SetTexture, asked again (it is
// not kept per draw). For the retail twin's dither weight (step 4d Z1).
float NativeRenderer_PsxDitherAmountNow(void)
{
	int ditherW = 0;
	int ditherH = 0;
	int displayW = 0;
	int displayH = 0;

	NativeRenderer_ActiveViewportSize(&ditherW, &ditherH);
	NativeRenderer_GetDisplaySize(&displayW, &displayH);
	return ((g_cfg_dither == NATIVE_DITHER_ALWAYS) || ((g_cfg_dither == NATIVE_DITHER_PACKED) && (ditherW == displayW) && (ditherH == displayH))) ? 1.0f
	                                                                                                                                         : 0.0f;
}

// The VRAM mirror (VRAM_WIDTH x VRAM_HEIGHT words), read by the retail twin
// (step 4d, platform/native_twin.c) at a loading screen. Only reading.
const u16 *NativeRenderer_VramMirror(void)
{
	return s_vram.cpuPixels;
}

// A static vertex buffer of the "nr" layout, for the native meshes of step 4c
// (platform/native_char_gpu.c). The layout is the probe's.
NativeGfxBuffer NativeRenderer_CreateNativeMeshVertexBuffer(int bytes, const void *initial)
{
	extern int g_cfg_nativePreview;
	const NativeGfxVertexBufferDesc desc = {
	    .bytes = bytes,
	    .dynamic = 0,
	    .initial = initial,
	    .stride = (int)sizeof(struct NativeProbeVertex),
	    .attribCount = 3,
	    .attribs = {
	        {.slot = a_position, .components = 3, .type = NATIVE_GFX_ATTR_FLOAT32, .offset = offsetof(struct NativeProbeVertex, position)},
	        {.slot = a_texcoord, .components = 2, .type = NATIVE_GFX_ATTR_FLOAT32, .offset = offsetof(struct NativeProbeVertex, texcoord)},
	        {.slot = a_color, .components = 4, .type = NATIVE_GFX_ATTR_UNORM8, .offset = offsetof(struct NativeProbeVertex, color)},
	    },
	};

	if (!g_cfg_nativePreview || (bytes <= 0))
	{
		return NATIVE_GFX_INVALID;
	}
	return NativeGfx_CreateVertexBuffer(&desc);
}

// THE PAINT ORDER OF THE RETAIL TWIN (step 4d, only for a draw with look = 1).
// Retail has no depth buffer: it sorts every triangle of the model into the
// ordering table by its mean depth (bins of MAC0 >> 17) and paints far bins
// first, and in one bin the later command first (NativeTwin_PaintOrder,
// include/platform/native_twin.h). Where a part of the model sticks out in
// front of another but lies in the same or a farther bin - the red zigzag on
// the back of the head of the fake driver (triangles 139-141 rooted in the fur
// triangles 46 and 112, which come first in the list) - retail paints it over,
// a depth test does not. So the twin draws its triangles one by one in that
// order with the depth compare ALWAYS: within the model the last painted wins,
// as on the PSX. Towards the scene nothing changes: the twin is the only
// native draw and clears the depth of its box first.
//
// The keys live here (grown, never shrunk); 0 = no order (no twin source, the
// ranges do not hold every triangle, a pose that is not a whole multiple), and
// the draw keeps its ranges and the depth test.
global_variable u64 *s_twinPaintKeys;
global_variable u32 s_twinPaintKeyMax;
global_variable int s_twinPaintNoted;

internal u32 NativeRenderer_TwinPaintOrder(const struct NativeMeshDraw *draw)
{
	const struct NativeTwinSource *src = NativeCharGpu_TwinSource();
	u32 indices = 0;
	u32 n;
	int i;

	if ((src == NULL) || (src->native.vertexCount == 0u) || (draw->vertexOffset < 0) || (((u32)draw->vertexOffset % src->native.vertexCount) != 0u))
	{
		return 0;
	}
	for (i = 0; i < draw->rangeCount; i++)
	{
		indices += draw->ranges[i].indexCount;
	}
	if (indices != (src->native.triangleCount * 3u))
	{
		return 0;
	}
	if (s_twinPaintKeyMax < src->native.triangleCount)
	{
		u64 *grown = (u64 *)realloc(s_twinPaintKeys, (size_t)src->native.triangleCount * sizeof(u64));

		if (grown == NULL)
		{
			return 0;
		}
		s_twinPaintKeys = grown;
		s_twinPaintKeyMax = src->native.triangleCount;
	}

	n = NativeTwin_PaintOrder(src, (u32)draw->vertexOffset / src->native.vertexCount, draw->screenFromModel, s_twinPaintKeys, s_twinPaintKeyMax);
	if (!s_twinPaintNoted)
	{
		s_twinPaintNoted = 1;
		Platform_Log("[CTR Twin] paint order: %s\n", (n > 0u) ? "retail ordering-table bins (MAC0 >> 17, far first, the later command first in a bin), depth compare always"
		                                                       : "not available, drawn by ranges with the depth test");
	}
	return n;
}

// The block of one twin range (step 4d Z1): its own program "nrt".
internal void NativeRenderer_TwinRangeBlock(const struct NativeMeshDraw *draw, const struct NativeMeshRangeDraw *range, const float clipFromModel[16],
                                            struct NativeTwinUniforms *twin)
{
	const int textured = (range->texture != NATIVE_GFX_INVALID);
	int k;

	memcpy(twin->clipFromModel, clipFromModel, sizeof(twin->clipFromModel));
	for (k = 0; k < 4; k++)
	{
		twin->tint[k] = range->tint[k];
		twin->far[k] = draw->far[k];
	}
	twin->params[0] = textured ? 1.0f : 0.0f;
	twin->params[1] = 0.0f;
	twin->params[2] = draw->depthTint;
	twin->params[3] = range->alphaCutoff;
	twin->look[0] = draw->tone;
	twin->look[1] = range->modulation;
	twin->look[2] = range->ditherWeight;
	twin->look[3] = 0.0f;
	twin->dither[0] = 0.0f;
	twin->dither[1] = 0.0f;
	twin->dither[2] = range->uvFudge[0];
	twin->dither[3] = range->uvFudge[1];
	// The inverse of the PSX projection for the dither grid: ndc x
	// = p0 * x + p12, ndc y = p5 * y + p13 (w is 1 in the PSX map).
	twin->proj[0] = s_psxUniforms.projection[0];
	twin->proj[1] = s_psxUniforms.projection[12];
	twin->proj[2] = s_psxUniforms.projection[5];
	twin->proj[3] = s_psxUniforms.projection[13];
}

// ONE NATIVE MESH (step 4c), drawn into the split's place as the probe is -
// the same calls in the same order as NativeRenderer_DrawNativeProbe (clip,
// projection, the block's matrix, the depth clear of the split's box, draw
// state, program), then one indexed draw per range with its own block (the
// block is copied per draw into the frame's ring, so every draw keeps its own
// tint and texture), and afterwards the same state reset. Slot 0 is put back
// when a range bound a texture there. The retail twin (look = 1) draws one
// triangle per draw in the retail paint order instead (above), when it has it.
int NativeRenderer_DrawNativeMesh(const struct NativeMeshDraw *draw, const RECT16 *clip, const DISPENV *dispenv, int onScreen)
{
	struct NativeLayerUniforms block;
	int textureBound = 0;
	int drawn = 0;
	int i;

	if ((draw == NULL) || (s_nativeLayerShader == NATIVE_GFX_INVALID) || (clip->w <= 0) || (clip->h <= 0) || (draw->rangeCount <= 0) ||
	    (draw->vertexBuffer == NATIVE_GFX_INVALID) || (draw->indexBuffer == NATIVE_GFX_INVALID))
	{
		return 0;
	}
	if (draw->look && (s_nativeTwinShader == NATIVE_GFX_INVALID))
	{
		return 0;
	}
	if (!onScreen || !NativeGfx_TargetDepth(s_mainRenderTarget))
	{
		return 0;
	}

	NativeRenderer_SetupClipMode(clip, dispenv, onScreen);
	NativeRenderer_SetOffscreenState(clip, !onScreen);
	NativeRenderer_SetProjection(clip, dispenv, !onScreen);

	{
		const float *p = s_psxUniforms.projection;
		const float *s = draw->screenFromModel;

		for (int column = 0; column < 4; column++)
		{
			const double s0 = (double)s[column * 4 + 0];
			const double s1 = (double)s[column * 4 + 1];
			const double s2 = (double)s[column * 4 + 2];
			const double s3 = (double)s[column * 4 + 3];

			block.clipFromModel[column * 4 + 0] = (float)((double)p[0] * s0 + (double)p[12] * s3);
			block.clipFromModel[column * 4 + 1] = (float)((double)p[5] * s1 + (double)p[13] * s3);
			block.clipFromModel[column * 4 + 2] = (float)s2;
			block.clipFromModel[column * 4 + 3] = (float)s3;
		}
	}

	if (draw->clearDepth)
	{
		int boxX = 0;
		int boxY = 0;
		int boxW = 0;
		int boxH = 0;

		NativeRenderer_ScissorBoxInForce(&boxX, &boxY, &boxW, &boxH);
		NativeGfx_ClearDepth(boxX, boxY, boxW, boxH);
	}

	{
		// The retail twin draws in the retail paint order when it has it.
		const u32 paintCount = draw->look ? NativeRenderer_TwinPaintOrder(draw) : 0u;
		const NativeGfxDrawState state = {
		    .depthTest = 1,
		    .depthWrite = 1,
		    .depthCompare = (paintCount > 0u) ? NATIVE_GFX_COMPARE_ALWAYS : NATIVE_GFX_COMPARE_GREATER_OR_EQUAL,
		    .cull = (NativeGfxCull)draw->cull,
		    .colorWriteOff = NATIVE_GFX_COLOR_A,
		};

		NativeGfx_SetBlendMode(BM_NONE);
		NativeGfx_SetDrawState(&state);
		NativeGfx_BindProgram(draw->look ? s_nativeTwinShader : s_nativeLayerShader);
		NativeGfx_BindVertexBuffer(draw->vertexBuffer);
		NativeGfx_BindIndexBuffer(draw->indexBuffer);

		// One triangle per draw; the block and texture change only when the
		// range does. drawn counts the ranges that drew a triangle.
		if (paintCount > 0u)
		{
			int current = -1;
			u64 used = 0;
			u32 k;

			for (k = 0; k < paintCount; k++)
			{
				const u32 first = NATIVE_TWIN_PAINT_PLACE(s_twinPaintKeys[k]) * 3u;
				int r = -1;

				for (i = 0; i < draw->rangeCount; i++)
				{
					if ((first >= draw->ranges[i].firstIndex) && ((first - draw->ranges[i].firstIndex) < draw->ranges[i].indexCount))
					{
						r = i;
						break;
					}
				}
				if (r < 0)
				{
					continue;
				}
				if (r != current)
				{
					const struct NativeMeshRangeDraw *range = &draw->ranges[r];
					struct NativeTwinUniforms twin;

					if (range->texture != NATIVE_GFX_INVALID)
					{
						NativeGfx_BindTexture(0, range->texture, NATIVE_GFX_FILTER_KEEP);
						textureBound = 1;
					}
					NativeRenderer_TwinRangeBlock(draw, range, block.clipFromModel, &twin);
					NativeGfx_UpdateUniforms(s_nativeTwinShader, &twin);
					current = r;
				}
				NativeGfx_DrawIndexed((int)first, 3, draw->vertexOffset);
				if ((used & ((u64)1 << (r & 63))) == 0u)
				{
					used |= (u64)1 << (r & 63);
					drawn++;
				}
			}
		}

		for (i = 0; (paintCount == 0u) && (i < draw->rangeCount); i++)
		{
			// The retail twin (step 4d Z1): its own program and block.
			if (draw->look)
			{
				const struct NativeMeshRangeDraw *range = &draw->ranges[i];
				struct NativeTwinUniforms twin;

				if ((range->indexCount == 0u) || (range->indexCount > 0x7FFFFFFFu))
				{
					continue;
				}
				if (range->texture != NATIVE_GFX_INVALID)
				{
					NativeGfx_BindTexture(0, range->texture, NATIVE_GFX_FILTER_KEEP);
					textureBound = 1;
				}

				NativeRenderer_TwinRangeBlock(draw, range, block.clipFromModel, &twin);
				NativeGfx_UpdateUniforms(s_nativeTwinShader, &twin);
				NativeGfx_DrawIndexed((int)range->firstIndex, (int)range->indexCount, draw->vertexOffset);
				drawn++;
				continue;
			}

			const struct NativeMeshRangeDraw *range = &draw->ranges[i];
			const int textured = (range->texture != NATIVE_GFX_INVALID);

			if ((range->indexCount == 0u) || (range->indexCount > 0x7FFFFFFFu))
			{
				continue;
			}
			if (textured)
			{
				NativeGfx_BindTexture(0, range->texture, NATIVE_GFX_FILTER_KEEP);
				textureBound = 1;
			}

			block.tint[0] = range->tint[0];
			block.tint[1] = range->tint[1];
			block.tint[2] = range->tint[2];
			block.tint[3] = range->tint[3];
			block.params[0] = textured ? 1.0f : 0.0f;
			block.params[1] = (textured && range->srgb) ? 1.0f : 0.0f;
			block.params[2] = draw->depthTint;
			block.params[3] = range->alphaCutoff;

			NativeGfx_UpdateUniforms(s_nativeLayerShader, &block);
			NativeGfx_DrawIndexed((int)range->firstIndex, (int)range->indexCount, draw->vertexOffset);
			drawn++;
		}
	}

	// Back to what the PSX path relies on, as after the probe.
	NativeGfx_SetDrawState(NULL);
	if (s_boundVertexBuffer >= 0)
	{
		NativeGfx_BindVertexBuffer(s_vertexBuffer[s_boundVertexBuffer]);
	}
	else
	{
		NativeGfx_BindVertexBuffer(NATIVE_GFX_INVALID);
	}
	s_previousShader = (ShaderID)-1;
	s_previousBlendMode = BM_NONE;
	NativeRenderer_MarkPSXUniformsDirty();

	if (textureBound)
	{
		NativeGfx_BindTexture(0, (s_lastBoundTexture == (TextureID)-1) ? 0 : s_lastBoundTexture, NATIVE_GFX_FILTER_KEEP);
	}

	return drawn;
}

int NativeRenderer_InitialisePSX(void)
{
	SDL_memset(s_vram.cpuPixels, 0, sizeof(s_vram.cpuPixels));
	s_vram.cpuDirtyRects[0].x = 0;
	s_vram.cpuDirtyRects[0].y = 0;
	s_vram.cpuDirtyRects[0].w = VRAM_WIDTH;
	s_vram.cpuDirtyRects[0].h = VRAM_HEIGHT;
	s_vram.cpuDirtyRectCount = 1;
	SDL_memset(s_vram.gpuNewerTiles, 0, sizeof(s_vram.gpuNewerTiles));

	// The atlas is not built here. Which pages a level draws with is not known
	// until it draws, and the flags that decide whether the store runs at all
	// are read after the renderer is up - so it is built at the first flush
	// that has something to write, and rebuilt whenever it has to grow.
	s_pages.texture = (TextureID)-1;
	s_pages.target = NATIVE_GFX_INVALID;
	s_pages.scale = 1;
	s_pages.tileRows = 0;
	s_pages.used = 0;
	s_pages.named = 0;
	s_pages.dirty = 0;
	s_pages.usedCount = 0;
	s_pages.namedCount = 0;
	s_pages.preloaded = 0;
	s_pages.rowsReportedAtCount = 0;
	NativeRenderer_ClearPageRows();

	NativeRenderer_InitRG8LUT();
	NativeRenderer_GenerateCommonTextures();
	NativeRenderer_InitialisePSXShaders();
	NativeRenderer_InitVRAMPipelines();
	NativeRenderer_PublishPageStoreUniforms();

#if defined(CTR_INTERNAL)
	// Whether the GPU can be timed at all is the device's answer, not
	// something this layer sniffs versions and extensions for.
	s_gpuTimerSupported = NativeGfx_TimersSupported();
	if (s_gpuTimerSupported)
	{
		for (s32 i = 0; i < NATIVE_GPU_TIMER_QUERY_COUNT; i++)
		{
			s_gpuTimerQueries[i].id = NativeGfx_CreateTimer();
		}
	}
#endif

	// Main and offscreen draws share one explicit render-target contract. The
	// main target's size comes from NativeRenderer_GetMainTargetSize (canvas
	// times the internal factor, or the window at NATIVE); the rest of the
	// scaling to the window happens at presentation.
	NativeRenderer_InitRenderTarget(&s_mainRenderTarget);
	NativeRenderer_InitRenderTarget(&s_offscreenRenderTarget);

	// ANTI-ALIASING: only the main target. The offscreen target
	// (dfe=0, multiplayer) stays at one sample, the VRAM target is borrowed
	// and would not get one anyway. The level is already fixed here - file and
	// --msaa have been read -, and it is applied like every later one.
	NativeRenderer_ApplyMsaa();
	NativeRenderer_InitNativeLayer();

	// gen VRAM texture (single, persistent - mirrors PS1's single 1MB VRAM)
	{
		// RG8 storage: two bytes per texel, which is the real PS1's 1MB of
		// VRAM exactly. See the note "WHY THE VRAM TEXTURE IS RG8" at the top of
		// this file for why that matters to the shader.
		const NativeGfxTextureDesc vramDesc = {
		    .width = VRAM_WIDTH,
		    .height = VRAM_HEIGHT,
		    .format = NATIVE_GFX_TEXFMT_RG8,
		    .filter = NATIVE_GFX_FILTER_NEAREST,
		    .wrap = NATIVE_GFX_WRAP_REPEAT,
		    .pixels = NULL,
		};
		s_vram.texture = NativeGfx_CreateTexture(&vramDesc);

		// Offscreen blitting to VRAM, and reading back out of it.
		const NativeGfxTargetDesc vramTargetDesc = {
		    .width = VRAM_WIDTH,
		    .height = VRAM_HEIGHT,
		    .texture = s_vram.texture,
		};
		s_vramTarget = NativeGfx_CreateTarget(&vramTargetDesc);
	}

	// gen the vertex buffers (no index buffer: every draw is a plain triangle list)
	{
		// The GrVertex layout, declared once instead of issued four times.
		// a_color is the only normalised attribute: it is the only one the
		// shader wants as 0..1 rather than as a raw integer.
		const NativeGfxVertexBufferDesc vertexDesc = {
		    .bytes = sizeof(GrVertex) * NATIVE_VERTEX_RING_SIZE,
		    .dynamic = 1,
		    .initial = NULL,
		    .stride = sizeof(GrVertex),
		    .attribCount = 4,
		    .attribs = {
		        {.slot = a_position, .components = 4, .type = NATIVE_GFX_ATTR_SINT16, .offset = offsetof(GrVertex, x)},
		        {.slot = a_texcoord, .components = 4, .type = NATIVE_GFX_ATTR_UINT8, .offset = offsetof(GrVertex, u)},
		        {.slot = a_color, .components = 4, .type = NATIVE_GFX_ATTR_UNORM8, .offset = offsetof(GrVertex, r)},
		        {.slot = a_extra, .components = 4, .type = NATIVE_GFX_ATTR_SINT8, .offset = offsetof(GrVertex, tcx)},
		    },
		};

		for (int i = 0; i < MAX_NUM_VERTEX_BUFFERS; i++)
		{
			s_vertexBuffer[i] = NativeGfx_CreateVertexBuffer(&vertexDesc);
		}
	}

	NativeRenderer_ResetDevice();

	return 1;
}

internal void NativeRenderer_Ortho2D(float left, float right, float bottom, float top, float znear, float zfar)
{
	float a = 2.0f / (right - left);
	float b = 2.0f / (top - bottom);
	float c = 2.0f / (znear - zfar);

	float x = (left + right) / (left - right);
	float y = (bottom + top) / (bottom - top);

	// -1..1
	float z = (znear + zfar) / (znear - zfar);

	float ortho[16] = {a, 0, 0, 0, 0, b, 0, 0, 0, 0, c, 0, x, y, z, 1};

	// Only when the matrix changes. The projection is set per split
	// (DrawSplit -> NativeRenderer_SetProjection), 989 to 2,284 times per frame
	// on Vista, almost always with the same canvas and the same height - and
	// every time the block got a new slot in the uniform ring ("2284
	// draw(s) ... 2284 uniform update(s)" in a measured run). A block that has
	// not changed does not need one.
	if (memcmp(s_psxUniforms.projection, ortho, sizeof(ortho)) == 0)
	{
		return;
	}

	memcpy(s_psxUniforms.projection, ortho, sizeof(ortho));
	NativeRenderer_MarkPSXUniformsDirty();
}

void NativeRenderer_SetupClipMode(const RECT16 *rect, const DISPENV *displayEnv, int enable)
{
	if ((displayEnv->disp.w <= 0) || (displayEnv->disp.h <= 0))
	{
		NativeRenderer_SetScissorState(0);
		return;
	}

	if ((rect->w <= 0) || (rect->h <= 0))
	{
		// NOTE(aalhendi): Retail draw-area commands define inclusive corners.
		// Collapsed areas clip all pixels; GL scissor rejects negative sizes.
		NativeRenderer_SetScissorState(enable != 0);
		if (enable)
		{
			NativeRenderer_SetScissorRect(0, 0, 0, 0);
		}
		return;
	}

	// [A] isinterlaced dirty hack for widescreen
	const bool scissorOn = enable && (displayEnv->isinter || (rect->x - displayEnv->disp.x > 0 || rect->y - displayEnv->disp.y > 0 ||
	                                                          rect->w < displayEnv->disp.w || rect->h < displayEnv->disp.h));

	NativeRenderer_SetScissorState(scissorOn);

	if (!scissorOn)
	{
		return;
	}

	const float emuScreenAspect = 1.0f;

	const float psxScreenWInv = 1.0f / (float)displayEnv->disp.w;
	const float psxScreenHInv = 1.0f / (float)displayEnv->disp.h;

	// first map to 0..1
	float clipRectX = (float)(rect->x - displayEnv->disp.x) * psxScreenWInv;
	float clipRectY = (float)(rect->y - displayEnv->disp.y) * psxScreenHInv;
	float clipRectW = (float)(rect->w) * psxScreenWInv;
	float clipRectH = (float)(rect->h) * psxScreenHInv;

	// then map to screen
	{
		clipRectX -= 0.5f;

		clipRectX *= emuScreenAspect;
		clipRectW *= emuScreenAspect;

		clipRectX += 0.5f;
	}

	// Normal game draws target the main framebuffer. Host-window coordinates
	// are introduced only by the final presentation pass.
	//
	// The box that comes in is in game coordinates and the box that goes out is
	// in target pixels, so it is scaled by the target's own size rather than by
	// a factor. At factor one the two are the same number and nothing moves;
	// above it, a box left unscaled would clip the picture to the top-left
	// 1/factor of itself. See NativeRenderer_ActiveViewportSize for why this
	// stopped being a multiplication.
	int clipTargetW = 0;
	int clipTargetH = 0;
	NativeRenderer_ActiveViewportSize(&clipTargetW, &clipTargetH);

	const float viewportX = 0.0f;
	const float viewportY = 0.0f;
	const float viewportW = (float)clipTargetW;
	const float viewportH = (float)clipTargetH;
	const float flipOffset = viewportY + viewportH - clipRectH * viewportH;
	const float crx = viewportX + clipRectX * viewportW;
	const float cry = clipRectY * viewportH;
	const float crw = clipRectW * viewportW;
	const float crh = clipRectH * viewportH;

	NativeRenderer_SetScissorRect((int)crx, (int)(flipOffset - cry), (int)crw, (int)crh);
}

internal void NativeRenderer_SetShader(const ShaderID shader)
{
	if (s_previousShader != shader)
	{
		NativeGfx_BindProgram(shader);

		s_previousShader = shader;
	}
}


void NativeRenderer_SetTexture(TextureID texture, TexFormat texFormat)
{
	// Which PSX program last got the block. The block is uploaded at the draw into
	// the program that s_psxShader names; if the program changes,
	// the other one still carries the state of its last upload - so the
	// change itself has to mark the block as changed, otherwise the new
	// program draws with old values.
	const ShaderID previousPsxShader = s_psxShader;

	// 4, 8 and 16 bit are one program: which of the three roads a texel takes
	// is decided per vertex (native_gpu.c, NativeGpu_VertexPageWord), so a
	// split may carry all three and the format handed in here only says
	// "VRAM" or "override texture".
	switch (texFormat)
	{
	case TF_4_BIT:
	case TF_8_BIT:
	case TF_16_BIT:
		NativeRenderer_SetShader(s_gteShaderPsx.shader);
		s_psxShader = s_gteShaderPsx.shader;
		NativeRenderer_NotePsxDraw(0, texture);
		break;
	case TF_32_BIT_RGBA:
		NativeRenderer_SetShader(s_gteShader32Rgba.shader);
		s_psxShader = s_gteShader32Rgba.shader;
		NativeRenderer_NotePsxDraw(1, texture);
		break;
	}

	if (g_dbg_texturelessMode)
	{
		texture = s_whiteTexture;
	}

	// NOTE(penta3): s_texture (unit 0) and s_rgLut (unit 1) sampler bindings are baked
	// into each program at creation (NativeRenderer_Shader_CompileWithUniforms) and uniform
	// values persist per-program, so re-setting them on every split was redundant GL
	// churn. bilinearFilter stays here because it toggles at runtime (debug key).
	//
	// Both values are first computed and then COMPARED: an earlier version
	// marked the block as changed on every call, that is every split,
	// and the ring got a new block per draw, although between two
	// splits mostly nothing had changed ("2284 draw(s) ... 2284 uniform
	// update(s)" in a measured run). What changes still marks.
	const int bilinearFilter = g_cfg_bilinearFiltering;
	float psxDitherAmount;

	// Beside bilinearFilter and for the same reason: it moves at runtime. The
	// active size is asked for rather than remembered, because the offscreen
	// target draws at VRAM size - and that pass is packed straight back into
	// VRAM at five bits, which is exactly where the matrix still belongs.
	//
	// The question is "does one target pixel become one five-bit VRAM word",
	// which used to be asked as "is the factor one". Those are the same answer
	// only while the target is the display times a whole number. At NATIVE the
	// factor is not a number at all, and a comparison against 1 would have said
	// yes to a 3440-wide target - dithering a picture that never gets truncated,
	// which is the exact bug once found in the other direction.
	{
		int ditherW = 0;
		int ditherH = 0;
		int displayW = 0;
		int displayH = 0;

		NativeRenderer_ActiveViewportSize(&ditherW, &ditherH);
		NativeRenderer_GetDisplaySize(&displayW, &displayH);

		psxDitherAmount = ((g_cfg_dither == NATIVE_DITHER_ALWAYS) ||
		                   ((g_cfg_dither == NATIVE_DITHER_PACKED) && (ditherW == displayW) && (ditherH == displayH)))
		                      ? 1.0f
		                      : 0.0f;
	}

	if ((s_psxUniforms.bilinearFilter != bilinearFilter) || (s_psxUniforms.psxDitherAmount != psxDitherAmount) || (s_psxShader != previousPsxShader))
	{
		s_psxUniforms.bilinearFilter = bilinearFilter;
		s_psxUniforms.psxDitherAmount = psxDitherAmount;
		NativeRenderer_MarkPSXUniformsDirty();
	}

	NativeRenderer_SetPSXTextureSemiTransPass(0);

	if (s_lastBoundTexture == texture)
	{
		return;
	}

	// NEAREST when this is VRAM, said out loud rather than inherited.
	//
	// It was KEEP, and KEEP was right only by luck. What slot zero holds for a
	// 4- or 8-bit draw is the CLUT row, addressed by an index the shader has
	// already looked up; for a 16-bit draw it is the packed colour word itself.
	// A LINEAR filter on either would blend two CLUT entries, or two packed
	// 5:5:5 words, and produce a colour that is in neither. Nothing in the tree
	// binds VRAM LINEAR today - every bind of it is enumerated and both say
	// NEAREST - but "nothing does yet" is not a property, and filters that were
	// inherited rather than chosen have cost long debugging sessions before.
	//
	// Override textures keep KEEP: those are real RGBA images, a linear filter
	// on them is a colour filter and means what it says, and forcing NEAREST
	// on them would be changing something that was not asked about.
	NativeGfx_BindTexture(0, texture, (texture == s_vram.texture) ? NATIVE_GFX_FILTER_NEAREST : NATIVE_GFX_FILTER_KEEP);

	s_lastBoundTexture = texture;
}

// Only the 32-bit variant reads texelSize. The guard that used to sit here was
// a location lookup failing on the other three; the block carries the field for
// all of them, and the one caller only calls this for TF_32_BIT_RGBA anyway.
// The four setters below only mark the block when the value changes
// - they run per split, and the same value twice is not a new
// block. The program change, which needs an upload even without a value change,
// is covered in NativeRenderer_SetTexture.
void NativeRenderer_SetOverrideTextureSize(int width, int height)
{
	const float texelW = 1.0f / (float)width;
	const float texelH = 1.0f / (float)height;

	if ((s_psxUniforms.texelSize[0] == texelW) && (s_psxUniforms.texelSize[1] == texelH))
	{
		return;
	}

	s_psxUniforms.texelSize[0] = texelW;
	s_psxUniforms.texelSize[1] = texelH;
	NativeRenderer_MarkPSXUniformsDirty();
}

void NativeRenderer_SetPSXTextureSemiTransPass(int pass)
{
	if (s_psxUniforms.psxSemiTransPass == pass)
	{
		return;
	}

	s_psxUniforms.psxSemiTransPass = pass;
	NativeRenderer_MarkPSXUniformsDirty();
}

void NativeRenderer_SetPSXTextureOutputSTP(int enabled)
{
	if (s_psxUniforms.psxTextureOutputStp == enabled)
	{
		return;
	}

	s_psxUniforms.psxTextureOutputStp = enabled;
	NativeRenderer_MarkPSXUniformsDirty();
}

void NativeRenderer_SetPSXDrawMaskSet(int maskSet)
{
	if (s_psxUniforms.psxDrawMaskSet == maskSet)
	{
		return;
	}

	s_psxUniforms.psxDrawMaskSet = maskSet;
	NativeRenderer_MarkPSXUniformsDirty();
}

internal void NativeRenderer_DestroyTexture(TextureID texture)
{
	// The "never created" guard moved into the backend, which owns what a
	// texture handle is; keeping a copy here would be two places to change.
	NativeGfx_DestroyTexture(texture);
}

internal u16 NativeRenderer_PackRGB24ToPSX15(u8 r, u8 g, u8 b)
{
	return (u16)(((r >> 3) & 0x1f) | (((g >> 3) & 0x1f) << 5) | (((b >> 3) & 0x1f) << 10));
}

internal float NativeRenderer_PSXColorComponentFloat(u8 value)
{
	const u8 psx8 = (u8)((value >> 3) << 3);
	return (float)psx8 / 255.0f;
}

internal int NativeRenderer_ClipVRAMRect(RECT16 *out, int x, int y, int w, int h)
{
	if ((w <= 0) || (h <= 0))
	{
		return 0;
	}

	if (x < 0)
	{
		w += x;
		x = 0;
	}
	if (y < 0)
	{
		h += y;
		y = 0;
	}
	if (x + w > VRAM_WIDTH)
	{
		w = VRAM_WIDTH - x;
	}
	if (y + h > VRAM_HEIGHT)
	{
		h = VRAM_HEIGHT - y;
	}
	if ((w <= 0) || (h <= 0))
	{
		return 0;
	}

	out->x = (s16)x;
	out->y = (s16)y;
	out->w = (s16)w;
	out->h = (s16)h;
	return 1;
}

internal int NativeRenderer_DirtyRectContains(const RECT16 *outer, const RECT16 *inner)
{
	const int outerRight = outer->x + outer->w;
	const int outerBottom = outer->y + outer->h;
	const int innerRight = inner->x + inner->w;
	const int innerBottom = inner->y + inner->h;

	return (outer->x <= inner->x) && (outer->y <= inner->y) && (outerRight >= innerRight) && (outerBottom >= innerBottom);
}

internal int NativeRenderer_TryMergeDirtyRect(RECT16 *dst, const RECT16 *src)
{
	const int dstRight = dst->x + dst->w;
	const int dstBottom = dst->y + dst->h;
	const int srcRight = src->x + src->w;
	const int srcBottom = src->y + src->h;

	if ((dst->y == src->y) && (dst->h == src->h) && (src->x <= dstRight) && (dst->x <= srcRight))
	{
		const int x0 = (src->x < dst->x) ? src->x : dst->x;
		const int x1 = (srcRight > dstRight) ? srcRight : dstRight;
		dst->x = (s16)x0;
		dst->w = (s16)(x1 - x0);
		return 1;
	}

	if ((dst->x == src->x) && (dst->w == src->w) && (src->y <= dstBottom) && (dst->y <= srcBottom))
	{
		const int y0 = (src->y < dst->y) ? src->y : dst->y;
		const int y1 = (srcBottom > dstBottom) ? srcBottom : dstBottom;
		dst->y = (s16)y0;
		dst->h = (s16)(y1 - y0);
		return 1;
	}

	return 0;
}

internal void NativeRenderer_AppendCpuDirtyRect(const RECT16 *rect)
{
	for (s32 i = 0; i < s_vram.cpuDirtyRectCount; i++)
	{
		if (NativeRenderer_DirtyRectContains(&s_vram.cpuDirtyRects[i], rect))
		{
			return;
		}

		if (NativeRenderer_DirtyRectContains(rect, &s_vram.cpuDirtyRects[i]))
		{
			s_vram.cpuDirtyRects[i] = *rect;
			return;
		}

		if (NativeRenderer_TryMergeDirtyRect(&s_vram.cpuDirtyRects[i], rect))
		{
			return;
		}
	}

	if (s_vram.cpuDirtyRectCount >= NATIVE_VRAM_DIRTY_RECT_CAP)
	{
		NativeRenderer_UpdateVRAM();
	}

	if (s_vram.cpuDirtyRectCount < NATIVE_VRAM_DIRTY_RECT_CAP)
	{
		s_vram.cpuDirtyRects[s_vram.cpuDirtyRectCount] = *rect;
		s_vram.cpuDirtyRectCount++;
	}
}

// ANTI-ALIASING: sample shading for the draws that DrawSplit
// is about to make (native_gpu.c), passed through to the gfx layer. It only takes
// effect there in a pass with more than one sample.
void NativeRenderer_SetSampleShading(int enable)
{
	NativeGfx_SetSampleShading(enable);
}

// --- ANTI-ALIASING: THE LEVEL ------------------------------------------------
//
// Off, 2x or 4x: the samples of the main target. Three numbers as with the factor of the
// internal resolution, and they are not the same:
//
//   s_msaaSetting       what is saved or was chosen in the menu - that is
//                       what ctr-settings.cfg writes ("video msaa"). Default 4x.
//   g_cfg_msaaSamples   --msaa, 0 without (main.c, early), at run time also
//                       --msaa-at (NativeRenderer_SetMsaaRun). Beats the
//                       setting for the run and is never saved;
//                       a choice in the menu cancels it for the rest of the run.
//   NativeRenderer_GetMsaa   what applies - the device can lower a number.
//
// A new level is applied at the frame boundary, in NativeRenderer_ApplyMsaa:
// when creating the main target and directly after the present, never in the middle of a
// pass. Whoever sets it - menu, settings file, --msaa-at - only requests it.
int Platform_GetVBlankCount(void);

global_variable int s_msaaSetting = 4;
int g_cfg_msaaSamples = 0;

// The level last given to the gfx layer, 0 before the first time. Once per
// wish, not per frame: if the device lowers a number or the creation
// fails, it stays that way until somebody chooses something else.
global_variable int s_msaaApplied = 0;

// 1, 2 or 4. 8x does not exist; a hand-edited file is exactly what
// the clamp is there for.
internal int NativeRenderer_ClampMsaa(int samples)
{
	return (samples >= 4) ? 4 : ((samples >= 2) ? 2 : 1);
}

void NativeRenderer_SetMsaaSetting(int samples)
{
	s_msaaSetting = NativeRenderer_ClampMsaa(samples);
}

int NativeRenderer_GetMsaaSetting(void)
{
	return s_msaaSetting;
}

// The choice in the menu: it is saved and cancels --msaa.
internal void NativeRenderer_SetMsaa(int samples)
{
	s_msaaSetting = NativeRenderer_ClampMsaa(samples);
	g_cfg_msaaSamples = 0;
}

// A run value like --msaa, for --msaa-at: the setting stays as it is,
// and a later save from any menu row writes no
// measured value into the file. It is applied at the same frame boundary as the
// choice in the menu.
void NativeRenderer_SetMsaaRun(int samples)
{
	g_cfg_msaaSamples = NativeRenderer_ClampMsaa(samples);
}

int NativeRenderer_GetMsaaRequested(void)
{
	return (g_cfg_msaaSamples > 0) ? NativeRenderer_ClampMsaa(g_cfg_msaaSamples) : s_msaaSetting;
}

int NativeRenderer_GetMsaa(void)
{
	return NativeGfx_TargetSamples(s_mainRenderTarget);
}

// Climbs and wraps like DITHER: off, 2x, 4x, off. From what was requested, not
// from what applies - otherwise a step to 4x that the device lowers to 2x
// would stay at 2x.
void NativeRenderer_StepMsaa(void)
{
	const int requested = NativeRenderer_GetMsaaRequested();

	NativeRenderer_SetMsaa((requested >= 4) ? 1 : (requested * 2));
}

const char *NativeRenderer_MsaaName(int samples)
{
	return (samples >= 4) ? "4x" : ((samples >= 2) ? "2x" : "OFF");
}

internal void NativeRenderer_ApplyMsaa(void)
{
	const int wanted = NativeRenderer_GetMsaaRequested();

	if (wanted == s_msaaApplied)
	{
		return;
	}

	s_msaaApplied = wanted;
	NativeGfx_SetTargetSamples(s_mainRenderTarget, wanted);

	Platform_Log("[CTR MSAA] anti-aliasing %s asked for at vblank %d, %s in force\n", NativeRenderer_MsaaName(wanted), Platform_GetVBlankCount(),
	             NativeRenderer_MsaaName(NativeRenderer_GetMsaa()));
}

// --- Who writes the portrait strip ------------------------------------------
//
// x 256..511, y 266..295 (VRAM halfwords) is where custom character portraits
// are meant to go: between the NITRO-PIT map tables (rows 264/265) and the
// second display buffer (from row 296). Nothing the game loads is supposed to
// land there - the disc, the containers and the code were read and found to
// leave it alone. This counts what a run actually does there, on the three ways
// a write reaches VRAM: a copy (LoadImage, or the destination of MoveImage), a
// clear, and a GPU pack. It only counts - no pixel, no order, no timing changes.
//
// Armed at the first game frame (DebugMenu_Frame). The boot clear covers all of
// VRAM and is not a stranger's write, so everything before that is counted on
// its own. Restoring a quick state copies all of VRAM back and is counted on
// its own as well: it brings back what was there, it writes nothing new.
//
// The portrait uploader brackets its own writes with
// NativeRenderer_StripOwnWrites(1) / (0); those are "own uploads", not hits.
// One line at exit (NativeRenderer_PrintExitSummary), the first hits as they
// happen.
#define NATIVE_VRAM_STRIP_X   256
#define NATIVE_VRAM_STRIP_Y   266
#define NATIVE_VRAM_STRIP_W   256
#define NATIVE_VRAM_STRIP_H   30
#define NATIVE_VRAM_STRIP_SAY 8 // hits logged when they happen

enum
{
	NATIVE_STRIP_LOAD,
	NATIVE_STRIP_MOVE,
	NATIVE_STRIP_CLEAR,
	NATIVE_STRIP_GPU,
	NATIVE_STRIP_PATHS
};

global_variable const char *const s_stripPathNames[NATIVE_STRIP_PATHS] = {"load", "move", "clear", "gpu"};
global_variable int s_stripArmed;
global_variable int s_stripOwn;
global_variable int s_stripWrites[2][NATIVE_STRIP_PATHS]; // [0] before the first game frame, [1] after
global_variable int s_stripOwnWrites;
global_variable int s_stripStateRestores;
// The first write before the first game frame that is not a clear to 0 (-1:
// none), said in full at exit; the boot clear itself is the expected one.
global_variable int s_stripBootPath = -1;
global_variable int s_stripBootValue;
global_variable int s_stripBootVBlank;
global_variable RECT16 s_stripBootRect;
global_variable int s_stripFirstPath = -1;
global_variable int s_stripFirstVBlank;
global_variable RECT16 s_stripFirstRect;

// x, y, w, h are the rectangle as it is written, after any cut to VRAM.
// clearColor is the halfword a clear writes, -1 for every other way.
internal void NativeRenderer_NoteStripWrite(int path, int x, int y, int w, int h, int clearColor)
{
	if (!((x < NATIVE_VRAM_STRIP_X + NATIVE_VRAM_STRIP_W) && (x + w > NATIVE_VRAM_STRIP_X) && (y < NATIVE_VRAM_STRIP_Y + NATIVE_VRAM_STRIP_H) &&
	      (y + h > NATIVE_VRAM_STRIP_Y)))
	{
		return;
	}

	if (s_stripOwn > 0)
	{
		s_stripOwnWrites++;
		return;
	}

	if (!s_stripArmed)
	{
		s_stripWrites[0][path]++;

		if (((path != NATIVE_STRIP_CLEAR) || (clearColor != 0)) && (s_stripBootPath < 0))
		{
			s_stripBootPath = path;
			s_stripBootValue = clearColor;
			s_stripBootVBlank = Platform_GetVBlankCount();
			s_stripBootRect.x = (s16)x;
			s_stripBootRect.y = (s16)y;
			s_stripBootRect.w = (s16)w;
			s_stripBootRect.h = (s16)h;
		}
		return;
	}

	s_stripWrites[1][path]++;

	int hits = 0;

	for (int i = 0; i < NATIVE_STRIP_PATHS; i++)
	{
		hits += s_stripWrites[1][i];
	}

	if (s_stripFirstPath < 0)
	{
		s_stripFirstPath = path;
		s_stripFirstVBlank = Platform_GetVBlankCount();
		s_stripFirstRect.x = (s16)x;
		s_stripFirstRect.y = (s16)y;
		s_stripFirstRect.w = (s16)w;
		s_stripFirstRect.h = (s16)h;
	}

	if (hits <= NATIVE_VRAM_STRIP_SAY)
	{
		char value[24] = "";

		if (clearColor >= 0)
		{
			SDL_snprintf(value, sizeof(value), " value 0x%04x", (unsigned)clearColor);
		}

		Platform_Log("[CTR VRAM] strip hit %d: %s (%d,%d %dx%d)%s at vblank %d\n", hits, s_stripPathNames[path], x, y, w, h, value,
		             Platform_GetVBlankCount());
	}
}

// From DebugMenu_Frame, its first line. Idempotent.
void NativeRenderer_StripArm(void)
{
	s_stripArmed = 1;
}

// See native_renderer.h. Nests, and never goes below zero.
void NativeRenderer_StripOwnWrites(int on)
{
	if (on)
	{
		s_stripOwn++;
	}
	else if (s_stripOwn > 0)
	{
		s_stripOwn--;
	}
}

internal void NativeRenderer_PrintStripSummary(void)
{
	const int *after = s_stripWrites[1];
	const int *before = s_stripWrites[0];
	const int hits = after[NATIVE_STRIP_LOAD] + after[NATIVE_STRIP_MOVE] + after[NATIVE_STRIP_CLEAR] + after[NATIVE_STRIP_GPU];
	const int early = before[NATIVE_STRIP_LOAD] + before[NATIVE_STRIP_MOVE] + before[NATIVE_STRIP_CLEAR] + before[NATIVE_STRIP_GPU];
	char first[96];
	char boot[192];
	char bracket[48] = "";

	if (s_stripBootPath >= 0)
	{
		char value[24] = "";

		if (s_stripBootValue >= 0)
		{
			SDL_snprintf(value, sizeof(value), " value 0x%04x", (unsigned)s_stripBootValue);
		}

		SDL_snprintf(boot, sizeof(boot), "NOT ONLY CLEAR 0: load %d, move %d, clear %d, gpu %d, first %s (%d,%d %dx%d)%s at vblank %d",
		             before[NATIVE_STRIP_LOAD], before[NATIVE_STRIP_MOVE], before[NATIVE_STRIP_CLEAR], before[NATIVE_STRIP_GPU],
		             s_stripPathNames[s_stripBootPath], s_stripBootRect.x, s_stripBootRect.y, s_stripBootRect.w, s_stripBootRect.h, value,
		             s_stripBootVBlank);
	}
	else
	{
		SDL_snprintf(boot, sizeof(boot), "clear 0x0000 only");
	}

	// A bracket of the portrait uploader that was opened and never closed:
	// every write after it was taken for its own and not counted as a hit.
	if (s_stripOwn != 0)
	{
		SDL_snprintf(bracket, sizeof(bracket), "; own bracket open (%d)", s_stripOwn);
	}

	if (s_stripFirstPath >= 0)
	{
		SDL_snprintf(first, sizeof(first), "first %s (%d,%d %dx%d) at vblank %d", s_stripPathNames[s_stripFirstPath], s_stripFirstRect.x,
		             s_stripFirstRect.y, s_stripFirstRect.w, s_stripFirstRect.h, s_stripFirstVBlank);
	}
	else
	{
		SDL_snprintf(first, sizeof(first), "first none");
	}

	Platform_Log("[CTR VRAM] strip x %d..%d y %d..%d: %d write(s) (load %d, move %d, clear %d, gpu %d), %s; own uploads %d; state restores %d; "
	             "before the first frame %d (%s)%s\n",
	             NATIVE_VRAM_STRIP_X, NATIVE_VRAM_STRIP_X + NATIVE_VRAM_STRIP_W - 1, NATIVE_VRAM_STRIP_Y, NATIVE_VRAM_STRIP_Y + NATIVE_VRAM_STRIP_H - 1,
	             hits, after[NATIVE_STRIP_LOAD], after[NATIVE_STRIP_MOVE], after[NATIVE_STRIP_CLEAR], after[NATIVE_STRIP_GPU], first, s_stripOwnWrites,
	             s_stripStateRestores, early, boot, bracket);
}

internal void NativeRenderer_MarkVRAMDirty(int x, int y, int w, int h)
{
	RECT16 rect;

	if (!NativeRenderer_ClipVRAMRect(&rect, x, y, w, h))
	{
		return;
	}

	NativeRenderer_AppendCpuDirtyRect(&rect);
	NativeRenderer_MarkPagesDirty(rect.x, rect.y, rect.w, rect.h);
}

internal void NativeRenderer_MarkGpuVRAMNewer(int x, int y, int w, int h)
{
	RECT16 rect;

	if (!NativeRenderer_ClipVRAMRect(&rect, x, y, w, h))
	{
		return;
	}

	NativeRenderer_NoteStripWrite(NATIVE_STRIP_GPU, rect.x, rect.y, rect.w, rect.h, -1);
	NativeRenderer_MarkPagesDirty(rect.x, rect.y, rect.w, rect.h);

	const int tileX0 = rect.x / NATIVE_VRAM_TILE_SIZE;
	const int tileY0 = rect.y / NATIVE_VRAM_TILE_SIZE;
	const int tileX1 = (rect.x + rect.w - 1) / NATIVE_VRAM_TILE_SIZE;
	const int tileY1 = (rect.y + rect.h - 1) / NATIVE_VRAM_TILE_SIZE;

	for (int tileY = tileY0; tileY <= tileY1; tileY++)
	{
		for (int tileX = tileX0; tileX <= tileX1; tileX++)
		{
			const int tileIndex = tileY * NATIVE_VRAM_TILE_COLS + tileX;
			s_vram.gpuNewerTiles[tileIndex >> 5] |= 1u << (tileIndex & 31);
		}
	}
}

// --- The page store, in one place -------------------------------------------
//
// Everything below knows how big the atlas is; nothing above it does. The
// shader is told, not consulted, and the numbers it is told are derived here.

// The atlas is only as tall as the tiles that are in use reach. One function
// answers what that is, so the texture, the target, the uniform the shader
// samples with and the log line cannot disagree about it.
internal int NativeRenderer_PageTileSize(void)
{
	return NATIVE_PAGE_TEXELS_X * s_pages.scale;
}

internal int NativeRenderer_PageAtlasWidth(void)
{
	return NATIVE_PAGE_TILE_COLS * NativeRenderer_PageTileSize();
}

internal int NativeRenderer_PageAtlasHeight(void)
{
	return s_pages.tileRows * NativeRenderer_PageTileSize();
}

// What the shader is handed. Derived, never stored twice: change the atlas and
// this changes with it, because it reads the same three functions the atlas was
// built from.
internal void NativeRenderer_PublishPageStoreUniforms(void)
{
	const int width = NativeRenderer_PageAtlasWidth();
	const int height = NativeRenderer_PageAtlasHeight();

	s_psxUniforms.psxPageScale = (float)s_pages.scale;
	s_psxUniforms.psxPageGrid[0] = (float)NATIVE_PAGE_TILE_COLS;
	s_psxUniforms.psxPageGrid[1] = (float)NativeRenderer_PageTileSize();
	s_psxUniforms.psxPageGrid[2] = (width > 0) ? (1.0f / (float)width) : 0.0f;
	s_psxUniforms.psxPageGrid[3] = (height > 0) ? (1.0f / (float)height) : 0.0f;
	s_psxUniforms.psxPageLayout[0] = (float)NATIVE_PAGE_COLS;
	s_psxUniforms.psxPageLayout[1] = (float)NATIVE_PAGE_WORDS_X;
	s_psxUniforms.psxPageLayout[2] = (float)NATIVE_PAGE_TEXELS_Y;
	s_psxUniforms.psxPageLayout[3] = 0.0f;

	NativeRenderer_MarkPSXUniformsDirty();
}

internal void NativeRenderer_DestroyPageAtlas(void)
{
	if (s_pages.target != NATIVE_GFX_INVALID)
	{
		NativeGfx_DestroyTarget(s_pages.target);
		s_pages.target = NATIVE_GFX_INVALID;
	}
	if (s_pages.texture != (TextureID)-1)
	{
		NativeGfx_DestroyTexture(s_pages.texture);
		s_pages.texture = (TextureID)-1;
	}

	s_pages.tileRows = 0;

	// Nothing survives the texture, so everything in use has to be written
	// again. Not "everything", though: a tile no draw has named is still not
	// worth a fill.
	s_pages.dirty = s_pages.used;
	s_lastBoundTexture = (TextureID)-1;
}

// Builds the atlas at whatever number of tile rows is now needed, and says so.
//
// The scale is clamped on the way in here rather than where it is set, so a
// value from a flag, a menu row or a future config file cannot leave a size
// behind that no device will hand out. What comes back is the scale that was
// actually built, which is what the VIDEO page reads.
internal int NativeRenderer_EnsurePageAtlas(int rowsNeeded)
{
	int scale = g_cfg_pageScale;

	if (scale < 1)
	{
		scale = 1;
	}
	if (scale > NATIVE_PAGE_SCALE_MAX)
	{
		Platform_Log("[CTR Pages] scale x%d is past the ceiling, using x%d\n", scale, NATIVE_PAGE_SCALE_MAX);
		scale = NATIVE_PAGE_SCALE_MAX;
		g_cfg_pageScale = scale;
	}

	while ((scale > 1) && ((NATIVE_PAGE_TILE_COLS * NATIVE_PAGE_TEXELS_X * scale) > NATIVE_PAGE_EDGE_MAX))
	{
		Platform_Log("[CTR Pages] atlas edge %d at scale x%d is past %d, dropping to x%d\n", NATIVE_PAGE_TILE_COLS * NATIVE_PAGE_TEXELS_X * scale, scale,
		             NATIVE_PAGE_EDGE_MAX, scale - 1);
		scale--;
	}

	if (rowsNeeded > NATIVE_PAGE_TILE_ROWS)
	{
		rowsNeeded = NATIVE_PAGE_TILE_ROWS;
	}
	if (rowsNeeded < 1)
	{
		return 0;
	}

	if ((s_pages.texture != (TextureID)-1) && (s_pages.scale == scale) && (s_pages.tileRows >= rowsNeeded))
	{
		return 1;
	}

	const int oldRows = s_pages.tileRows;
	const int oldScale = s_pages.scale;
	const int oldWidth = (oldRows > 0) ? NativeRenderer_PageAtlasWidth() : 0;
	const int oldHeight = (oldRows > 0) ? NativeRenderer_PageAtlasHeight() : 0;

	NativeRenderer_DestroyPageAtlas();

	// What a device will actually hand out is not a number this layer holds, and
	// guessing at one would be a second copy of a fact only the driver has. So
	// it is asked, and a refusal steps down a scale and asks again - out loud,
	// with both numbers, because a picture that quietly arrived at half the
	// resolution that was asked for is worse than one that says so.
	while (scale >= 1)
	{
		s_pages.scale = scale;
		s_pages.tileRows = rowsNeeded;

		const int width = NativeRenderer_PageAtlasWidth();
		const int height = NativeRenderer_PageAtlasHeight();

		// Nearest, always. The bytes here are palette indices; an interpolated
		// index is a different colour, not a blend of two.
		const NativeGfxTextureDesc atlasDesc = {
		    .width = width,
		    .height = height,
		    .format = NATIVE_GFX_TEXFMT_R8,
		    .filter = NATIVE_GFX_FILTER_NEAREST,
		    .wrap = NATIVE_GFX_WRAP_CLAMP,
		    .pixels = NULL,
		};

		s_pages.texture = NativeGfx_CreateTexture(&atlasDesc);

		if (s_pages.texture != (TextureID)-1)
		{
			const NativeGfxTargetDesc atlasTargetDesc = {
			    .width = width,
			    .height = height,
			    .texture = s_pages.texture,
			};

			s_pages.target = NativeGfx_CreateTarget(&atlasTargetDesc);

			if (s_pages.target != NATIVE_GFX_INVALID)
			{
				s_pages.growths++;
				Platform_Log("[CTR Pages] atlas from %d rows (%dx%d, %d KB, x%d) to %d rows (%dx%d, %d KB, x%d)\n", oldRows, oldWidth, oldHeight,
				             (oldWidth * oldHeight) / 1024, oldScale, s_pages.tileRows, width, height, (width * height) / 1024, s_pages.scale);

				NativeRenderer_PublishPageStoreUniforms();
				return 1;
			}

			Platform_LogError("[CTR Pages] atlas %dx%d could not be drawn into\n", width, height);
		}
		else
		{
			Platform_LogError("[CTR Pages] atlas %dx%d could not be created\n", width, height);
		}

		NativeRenderer_DestroyPageAtlas();

		if (scale <= 1)
		{
			break;
		}

		Platform_Log("[CTR Pages] scale x%d did not fit, trying x%d\n", scale, scale - 1);
		scale--;
		g_cfg_pageScale = scale;
	}

	// There is no second route left to fall back to, so this is said as loudly
	// as it can be said rather than quietly switching something off. It also
	// cannot happen in practice: the smallest atlas that holds every tile is
	// 2048x2048 of one byte each - four megabytes, and under every device's
	// limit. Said out loud anyway, because a limit that takes effect without a
	// word is a shape of bug this renderer has met often enough.
	Platform_LogError("[CTR Pages] no atlas could be built - indexed textures cannot be drawn\n");
	s_pages.scale = 1;
	s_pages.tileRows = 0;
	NativeRenderer_PublishPageStoreUniforms();
	return 0;
}

// Empty, meaning no row of any page has been read from yet. Written out rather
// than memset, because zero is a valid row and 0..0 would be a claim.
internal void NativeRenderer_ClearPageRows(void)
{
	for (int layer = 0; layer < NATIVE_PAGE_TILE_COUNT; layer++)
	{
		s_pages.rowMin[layer] = 255;
		s_pages.rowMax[layer] = 0;
	}
}

// Every tile resident from the start, rather than one at a time as pages are
// first named.
//
// What that removes is not a stale sample - there never was one, the fill runs
// in the UpdateVRAM that opens the batch and therefore before anything reads.
// It removes the atlas GROWTH: the store began at nothing, reached four tile
// rows, and later eight, and each of those destroys a texture of several
// megabytes and writes every tile again, in the middle of a race. Two of them
// in one measured race.
//
// Here and not at renderer init, because the flags that decide this are read
// after the renderer is up. Once, and it says so.
internal void NativeRenderer_PreloadPageStore(void)
{
	if (s_pages.preloaded || !g_cfg_pagePreload)
	{
		return;
	}

	s_pages.preloaded = 1;
	s_pages.used = ~(u64)0 >> (64 - NATIVE_PAGE_TILE_COUNT);
	s_pages.dirty = s_pages.used;
	s_pages.usedCount = NATIVE_PAGE_TILE_COUNT;

	Platform_Log("[CTR Pages] all %d tiles held from the start, no growth to come\n", NATIVE_PAGE_TILE_COUNT);
}

// A draw has named this page and this depth, so the tile behind it has to
// exist and has to hold what VRAM holds.
//
// Called while primitives are being parsed, which is before the batch is drawn
// and therefore before the flush - the tile is filled by the time anything
// samples it. Idempotent, because this runs once per primitive and almost every
// one of them names a page that was already named.
void NativeRenderer_UsePageTile(int pageIndex, int fourBit)
{
	// Marked whether the store is running or not. The switch decides whether
	// tiles are written, not whether it is known which ones would be needed -
	// gating this on it meant that turning the store off, driving somewhere
	// else and turning it back on left the pages seen in between unrecorded,
	// and their textures would have come back as tile zero.
	//
	// It also makes the count an answer to "how many tiles does this level
	// want" that does not require running the store to ask.
	if ((pageIndex < 0) || (pageIndex >= (NATIVE_PAGE_TILE_COUNT / 2)))
	{
		return;
	}

	const int layer = (pageIndex * 2) + (fourBit ? 0 : 1);
	const u64 bit = (u64)1 << layer;

	if ((s_pages.named & bit) == 0)
	{
		s_pages.named |= bit;
		s_pages.namedCount++;

		Platform_Log("[CTR Pages] page %d at %d bit comes into use - %d of %d tiles named\n", pageIndex, fourBit ? 4 : 8, s_pages.namedCount,
		             NATIVE_PAGE_TILE_COUNT);
	}

	if ((s_pages.used & bit) != 0)
	{
		return;
	}

	s_pages.used |= bit;
	s_pages.dirty |= bit;
	s_pages.usedCount++;
}

// The rows of a page a primitive is about to read from.
//
// The range only ever widens, and widening marks the tile dirty: rows that were
// outside it were not being kept fresh, so the moment something reads them they
// have to be written once. That is what makes skipping a refill safe rather
// than a bet - a row nobody reads is not kept, and the instant somebody reads
// it, it is.
//
// One row of slack on each side. Bilinear texture filtering reaches a texel
// past the coordinate it was given, and so does the quarter-texel nudge the
// vertex stage applies. Neither is worth being exactly right about at the cost
// of being wrong once.
void NativeRenderer_NotePageRows(int pageIndex, int fourBit, int rowLo, int rowHi)
{
	if ((pageIndex < 0) || (pageIndex >= (NATIVE_PAGE_TILE_COUNT / 2)))
	{
		return;
	}

	const int layer = (pageIndex * 2) + (fourBit ? 0 : 1);

	if (rowLo > rowHi)
	{
		return;
	}

	// Past the bottom of the page means the read wrapped round to the top of it,
	// and which rows it landed on is not something to work out from here. The
	// whole page, then - conservative in the one direction that is safe.
	if (rowHi > 255)
	{
		rowLo = 0;
		rowHi = 255;
	}

	if (rowLo < 0)
	{
		rowLo = 0;
	}

	rowLo = (rowLo > 0) ? (rowLo - 1) : 0;
	rowHi = (rowHi < 255) ? (rowHi + 1) : 255;

	const int wasEmpty = (s_pages.rowMin[layer] > s_pages.rowMax[layer]);
	const int widens = wasEmpty || (rowLo < s_pages.rowMin[layer]) || (rowHi > s_pages.rowMax[layer]);

	if (!widens)
	{
		return;
	}

	s_pages.rowMin[layer] = (u8)(wasEmpty ? rowLo : ((rowLo < s_pages.rowMin[layer]) ? rowLo : s_pages.rowMin[layer]));
	s_pages.rowMax[layer] = (u8)(wasEmpty ? rowHi : ((rowHi > s_pages.rowMax[layer]) ? rowHi : s_pages.rowMax[layer]));
	s_pages.dirty |= ((u64)1 << layer) & s_pages.used;
}

// One tile, one row span of VRAM that moved. Dirty only if the two meet.
//
// Spared is counted only for tiles a draw has NAMED, and that is the whole
// point of the word. Holding the store from the start made every one of the 64
// tiles resident, so a skip counted on residency alone would count forty tiles
// that were never candidates for a fill before any of this existed - and the
// number would read as a saving eighteen times bigger than the fill count
// actually fell by. It did read that way once.
internal void NativeRenderer_DirtyTileForRows(int layer, int rowLo, int rowHi)
{
	const u64 bit = (u64)1 << layer;

	if ((s_pages.used & bit) == 0)
	{
		return;
	}

	if (!g_cfg_pageRowRange)
	{
		s_pages.dirty |= bit;
		return;
	}

	// Never read from. Not kept fresh, and nothing is lost by that: the first
	// draw that names a row of it widens the range above and marks it then.
	const int sparedForNamed = ((s_pages.named & bit) != 0);

	if (s_pages.rowMin[layer] > s_pages.rowMax[layer])
	{
		s_pages.fillsSpared += sparedForNamed;
		s_pages.fillsSparedSinceReport += sparedForNamed;
		return;
	}

	if ((rowHi < (int)s_pages.rowMin[layer]) || (rowLo > (int)s_pages.rowMax[layer]))
	{
		s_pages.fillsSpared += sparedForNamed;
		s_pages.fillsSparedSinceReport += sparedForNamed;
		return;
	}

	s_pages.dirty |= bit;
}

// VRAM under a rectangle has moved, so every tile that reads from it is stale.
//
// A tile is stale for a page it overlaps - and, at 8 bit, for the page to its
// left as well, because an 8-bit tile is 128 words wide and reaches into the
// next page. That reach wraps at the right-hand edge of VRAM, exactly as the
// sampler's own wrap does, so the left neighbour of page 0 is page 15.
internal void NativeRenderer_MarkPagesDirty(int x, int y, int w, int h)
{
	if ((s_pages.used == 0) || (w <= 0) || (h <= 0))
	{
		return;
	}

	const int pageX0 = x / NATIVE_PAGE_WORDS_X;
	const int pageX1 = (x + w - 1) / NATIVE_PAGE_WORDS_X;
	const int pageY0 = y / NATIVE_PAGE_TEXELS_Y;
	const int pageY1 = (y + h - 1) / NATIVE_PAGE_TEXELS_Y;

	for (int pageY = pageY0; pageY <= pageY1; pageY++)
	{
		// Where the rectangle sits INSIDE this row of pages. A page is 256 rows
		// and the frame buffer is 216 of them, so this is almost never the whole
		// page - which is the entire point.
		const int pageTop = pageY * NATIVE_PAGE_TEXELS_Y;
		int rowLo = y - pageTop;
		int rowHi = (y + h - 1) - pageTop;

		if (rowLo < 0)
		{
			rowLo = 0;
		}
		if (rowHi > (NATIVE_PAGE_TEXELS_Y - 1))
		{
			rowHi = NATIVE_PAGE_TEXELS_Y - 1;
		}

		for (int pageX = pageX0; pageX <= pageX1; pageX++)
		{
			const int page = (pageY * NATIVE_PAGE_COLS) + pageX;
			const int leftNeighbour = (pageY * NATIVE_PAGE_COLS) + ((pageX + NATIVE_PAGE_COLS - 1) % NATIVE_PAGE_COLS);

			// The left neighbour is dirtied at the same rows: an 8-bit tile
			// reaches sideways into the next page, not downwards into other
			// rows of it.
			NativeRenderer_DirtyTileForRows(page * 2, rowLo, rowHi);
			NativeRenderer_DirtyTileForRows((page * 2) + 1, rowLo, rowHi);
			NativeRenderer_DirtyTileForRows((leftNeighbour * 2) + 1, rowLo, rowHi);
		}
	}

	s_pages.dirty &= s_pages.used;
}

// Writes every tile that is in use and stale, from the VRAM texture, on the GPU.
//
// No readback: the source is the VRAM texture itself, which by the time this
// runs already holds both what the CPU uploaded and what the GPU drew into it.
// That is why this hangs off NativeRenderer_UpdateVRAM - the one place every
// draw batch passes through, and the point at which the device's view of VRAM
// is current by definition.
internal void NativeRenderer_FlushPageStore(void)
{
	NativeRenderer_PreloadPageStore();

	if (s_pages.dirty == 0)
	{
		return;
	}

	int rowsNeeded = 0;

	for (int layer = 0; layer < NATIVE_PAGE_TILE_COUNT; layer++)
	{
		if ((s_pages.used & ((u64)1 << layer)) != 0)
		{
			const int row = (layer / NATIVE_PAGE_TILE_COLS) + 1;

			if (row > rowsNeeded)
			{
				rowsNeeded = row;
			}
		}
	}

	if (!NativeRenderer_EnsurePageAtlas(rowsNeeded))
	{
		s_pages.dirty = 0;
		return;
	}

	const ShaderID previousShader = s_previousShader;
	const TextureID previousTexture = s_lastBoundTexture;
	const BlendMode previousBlendMode = s_previousBlendMode;
	const int previousScissorState = s_previousScissorState;

	NativeRenderer_BindTarget(s_pages.target);
	NativeGfx_SetBlendMode(BM_NONE);
	NativeGfx_SetScissorEnabled(0);
	NativeGfx_BindProgram(s_packPagesShader);
	NativeGfx_BindTexture(0, s_vram.texture, NATIVE_GFX_FILTER_KEEP);
	NativeGfx_BindVertexBuffer(s_vramQuadBuffer);

	const int tileSize = NativeRenderer_PageTileSize();
	const u64 todo = s_pages.dirty;

	s_pages.dirty = 0;

	for (int layer = 0; layer < NATIVE_PAGE_TILE_COUNT; layer++)
	{
		if ((todo & ((u64)1 << layer)) == 0)
		{
			continue;
		}

		const int page = layer / 2;
		const int fourBit = ((layer & 1) == 0);
		const int pageX = page % NATIVE_PAGE_COLS;
		const int pageY = page / NATIVE_PAGE_COLS;

		const struct NativeBlitPagesUniforms block = {
		    .source = {(float)(pageX * NATIVE_PAGE_WORDS_X), (float)(pageY * NATIVE_PAGE_TEXELS_Y), fourBit ? 0.0f : 1.0f, (float)s_pages.scale},
		    .shape = {(float)tileSize, 0.0f, 0.0f, 0.0f},
		    .vram = {(float)VRAM_WIDTH, (float)VRAM_HEIGHT, 1.0f / (float)VRAM_WIDTH, 1.0f / (float)VRAM_HEIGHT},
		};

		NativeGfx_UpdateUniforms(s_packPagesShader, &block);
		NativeGfx_SetViewport((layer % NATIVE_PAGE_TILE_COLS) * tileSize, (layer / NATIVE_PAGE_TILE_COLS) * tileSize, tileSize, tileSize);
		NativeRenderer_DrawTriangles(0, 2);

		s_pages.refillsThisFrame++;
		s_pages.refillsTotal++;
	}

	if (s_boundVertexBuffer >= 0)
	{
		NativeGfx_BindVertexBuffer(s_vertexBuffer[s_boundVertexBuffer]);
	}
	else
	{
		NativeGfx_BindVertexBuffer(NATIVE_GFX_INVALID);
	}

	NativeRenderer_RebindDrawTarget();

	NativeGfx_BindProgram(previousShader == (ShaderID)-1 ? 0 : previousShader);
	NativeGfx_BindTexture(0, previousTexture == (TextureID)-1 ? 0 : previousTexture, NATIVE_GFX_FILTER_KEEP);
	s_previousShader = previousShader;
	s_lastBoundTexture = previousTexture;
	s_previousBlendMode = BM_NONE;
	s_previousScissorState = 0;
	NativeRenderer_SetBlendMode(previousBlendMode);
	NativeRenderer_SetScissorState(previousScissorState);

	// The atlas is bound where the PSX shaders expect it, after it has been
	// written. Slot 2 is its own the way slot 1 is the lookup table's, so
	// nothing else ever moves it and this only has to happen when the atlas
	// itself changes.
	NativeGfx_BindTexture(2, s_pages.texture, NATIVE_GFX_FILTER_NEAREST);
	NativeGfx_BindTexture(0, previousTexture == (TextureID)-1 ? 0 : previousTexture, NATIVE_GFX_FILTER_KEEP);
}

// What the store costs, said every few seconds rather than every frame.
//
// A tile is written when VRAM under it moves. The framebuffer moves every
// frame, so pages that overlap it are written every frame - and if a level ever
// samples one of those as an indexed texture, the number below is where that
// shows up. Silence means the tiles in use are the ones VRAM is not churning.
internal void NativeRenderer_ReportPageStore(void)
{
	s_pages.refillsSinceReport += s_pages.refillsThisFrame;
	s_pages.framesSinceReport++;
	s_pages.refillsThisFrame = 0;

	if (s_pages.framesSinceReport < 300)
	{
		return;
	}

	if ((s_pages.refillsSinceReport > 0) || (s_pages.fillsSparedSinceReport > 0))
	{
		// Named beside resident, because with the store held whole the resident
		// count is always 64 and would stop answering "how many tiles does this
		// level want". Spared beside filled, because that difference is the
		// whole of what the row range buys and it has to be readable without a
		// second run.
		Platform_Log("[CTR Pages] %d tile fills over %d frames, %d spared, %d of %d tiles named, %d fills in all\n", s_pages.refillsSinceReport,
		             s_pages.framesSinceReport, s_pages.fillsSparedSinceReport, s_pages.namedCount, NATIVE_PAGE_TILE_COUNT, s_pages.refillsTotal);

		// Once, and only once: which rows of each named page are actually read.
		//
		// This is the number that says WHY the saving is what it is. A page
		// whose reads sit in rows 216..255 never meets a 216-row frame buffer
		// and is spared every frame; one whose reads start at row 0 shares the
		// rows with it and cannot be. Guessing which is which from the fill
		// count is exactly the kind of reasoning this line replaces.
		// AS LONG AS NEW TILES ARE ADDED, IT IS PRINTED AGAIN.
		//
		// Here stood a one-shot, and it made itself useless in the first log it
		// was read in: the first report falls on the title screen, where
		// EXACTLY ONE tile is named. The table named page 15 and nothing
		// else, and for the race with 25 tiles it stood nowhere - so
		// exactly not for the case it was built for.
		//
		// A one-shot fires at the first moment it can, and the first
		// moment is almost never the one meant. The condition is now not
		// "never before", but "tiles have been added since". That is
		// true at most 64 times and cannot flood.
		if (s_pages.namedCount > s_pages.rowsReportedAtCount)
		{
			s_pages.rowsReportedAtCount = s_pages.namedCount;

			for (int layer = 0; layer < NATIVE_PAGE_TILE_COUNT; layer++)
			{
				if ((s_pages.named & ((u64)1 << layer)) == 0)
				{
					continue;
				}

				if (s_pages.rowMin[layer] > s_pages.rowMax[layer])
				{
					Platform_Log("[CTR Pages]   page %2d at %d bit named but no row read yet\n", layer / 2, (layer & 1) ? 8 : 4);
					continue;
				}

				Platform_Log("[CTR Pages]   page %2d at %d bit reads rows %3d..%3d\n", layer / 2, (layer & 1) ? 8 : 4, s_pages.rowMin[layer],
				             s_pages.rowMax[layer]);
			}
		}
	}

	s_pages.refillsSinceReport = 0;
	s_pages.fillsSparedSinceReport = 0;
	s_pages.framesSinceReport = 0;
}

void NativeRenderer_SetPageScale(int scale)
{
	if (scale < 1)
	{
		scale = 1;
	}
	if (scale > NATIVE_PAGE_SCALE_MAX)
	{
		scale = NATIVE_PAGE_SCALE_MAX;
	}

	if (scale == g_cfg_pageScale)
	{
		return;
	}

	g_cfg_pageScale = scale;

	// Rebuilt at the next flush, from VRAM, which is still the truth. Nothing
	// is thrown away that cannot be written again from it.
	NativeRenderer_DestroyPageAtlas();
	NativeRenderer_PublishPageStoreUniforms();
	Platform_Log("[CTR Pages] scale asked for x%d\n", g_cfg_pageScale);
}

// Read back, not remembered. The built scale is clamped twice - against its own
// ceiling and against the edge a device will hand out - so a row that printed
// what was asked for would be reporting a wish.
int NativeRenderer_GetPageScale(void)
{
	return (s_pages.tileRows > 0) ? s_pages.scale : g_cfg_pageScale;
}

// The dither setting, read back rather than remembered, like everything else on
// this page: a flag, a settings file and a menu row all reach the same variable.
int NativeRenderer_GetDither(void)
{
	return g_cfg_dither;
}

// Clamped here and nowhere else, and it climbs and wraps for the menu, which has
// no left and right. Three settings, so the wrap is the whole range.
void NativeRenderer_SetDither(int mode)
{
	if ((mode < NATIVE_DITHER_OFF) || (mode > NATIVE_DITHER_ALWAYS))
	{
		mode = NATIVE_DITHER_PACKED;
	}

	g_cfg_dither = mode;
	NativeRenderer_MarkPSXUniformsDirty();
}

void NativeRenderer_StepDither(void)
{
	NativeRenderer_SetDither((g_cfg_dither >= NATIVE_DITHER_ALWAYS) ? NATIVE_DITHER_OFF : (g_cfg_dither + 1));
}

const char *NativeRenderer_DitherName(void)
{
	switch (g_cfg_dither)
	{
	case NATIVE_DITHER_OFF:
		return "OFF";
	case NATIVE_DITHER_ALWAYS:
		return "ALWAYS";
	default:
		return "PACKED";
	}
}

// The two page-store switches, read back rather than remembered: a flag and a
// menu row both reach the same variable, so the row has to print the variable.
int NativeRenderer_GetPageRowRange(void)
{
	return g_cfg_pageRowRange;
}

void NativeRenderer_SetPageRowRange(int on)
{
	g_cfg_pageRowRange = (on != 0);
	Platform_Log("[CTR Pages] row range %s\n", g_cfg_pageRowRange ? "on" : "off");
}

int NativeRenderer_GetPagePreload(void)
{
	return g_cfg_pagePreload;
}

// Turning it on mid-run does what it says at the next flush. Turning it off
// does NOT hand the tiles back - they are already resident and there is nothing
// to be gained by dropping them; what it changes is that the next run starts
// the old way. Said here because a switch that half applies is worse than one
// that says which half.
void NativeRenderer_SetPagePreload(int on)
{
	g_cfg_pagePreload = (on != 0);
	Platform_Log("[CTR Pages] preload %s%s\n", g_cfg_pagePreload ? "on" : "off",
	             (!g_cfg_pagePreload && s_pages.preloaded) ? " - tiles already held stay held until the next run" : "");
}

void NativeRenderer_GetPageStoreUse(int *outTilesUsed, int *outTilesTotal, int *outKiB)
{
	if (outTilesUsed != NULL)
	{
		// Named, not resident. With the store held whole from the start the
		// resident count is 64 on every level and would have stopped answering
		// the question this row is for - how many tiles does THIS level want.
		*outTilesUsed = s_pages.namedCount;
	}
	if (outTilesTotal != NULL)
	{
		*outTilesTotal = NATIVE_PAGE_TILE_COUNT;
	}
	if (outKiB != NULL)
	{
		*outKiB = (s_pages.tileRows > 0) ? ((NativeRenderer_PageAtlasWidth() * NativeRenderer_PageAtlasHeight()) / 1024) : 0;
	}
}

int NativeRenderer_GetPageRefills(void)
{
	return s_pages.refillsTotal;
}

void NativeRenderer_ClearVRAM(int x, int y, int w, int h, u8 r, u8 g, u8 b)
{
	const u16 color = NativeRenderer_PackRGB24ToPSX15(r, g, b);
	RECT16 clipped;

	// The same rectangle as the dirty rectangle below, with the same
	// scissor. An earlier version computed the pointer BEFORE the check
	// and only the right and the bottom edge were cut: a negative x or
	// y wrote before the start of the row or before cpuPixels, while MarkVRAMDirty
	// cut the same rectangle cleanly - CPU mirror and dirty list had
	// two rules for one fact. Counted, because no log of that time says whether
	// that ever happens.
	if (!NativeRenderer_ClipVRAMRect(&clipped, x, y, w, h))
	{
		return;
	}

	if ((clipped.x != x) || (clipped.y != y) || (clipped.w != w) || (clipped.h != h))
	{
		local_persist int s_clearClipSaid = 0;

		if (s_clearClipSaid < 10)
		{
			s_clearClipSaid++;
			Platform_Log("[CTR Renderer] ClearVRAM (%d,%d,%d,%d) reaches outside VRAM, clipped to (%d,%d,%d,%d)\n", x, y, w, h, clipped.x, clipped.y,
			             clipped.w, clipped.h);
		}
	}

	x = clipped.x;
	y = clipped.y;
	w = clipped.w;
	h = clipped.h;

	NativeRenderer_NoteStripWrite(NATIVE_STRIP_CLEAR, x, y, w, h, color);

	u16 *dst = s_vram.cpuPixels + x + y * VRAM_WIDTH;

	// clear VRAM region with given color
	for (int i = 0; i < h; i++)
	{
		u16 *tmp = dst;

		for (int j = 0; j < w; j++)
		{
			*tmp++ = color;
		}

		dst += VRAM_WIDTH;
	}

	NativeRenderer_MarkVRAMDirty(x, y, w, h);
}

void NativeRenderer_SetFillRectAsDraw(int enable)
{
	s_fillRectAsDraw = enable ? 1 : 0;
	Platform_Log("[CTR Native] fill rectangles: %s\n", enable ? "drawn as a quad" : "cleared with a scissor");
}

// Draws the fill rectangle as two triangles, in display-relative coordinates.
//
// No new shader. An untextured surface already has a path here: the 32-bit
// program samples a texture and multiplies by the vertex colour, so the 1x1
// white texture the parser already uses for flat polygons (native_gpu.c, AddSplit)
// turns it into a flat fill. bright 1 leaves the colour alone, dither 0 keeps
// the fill flat the way a clear is flat, and the mask bit is written as zero
// because that is what the scissored clear wrote before it.
//
// Every piece of state this disturbs is put back, because a fill arrives
// between two draw batches and the batch after it has not been told anything
// changed.
internal int NativeRenderer_FillRectByDraw(int left, int top, int width, int height, u8 r, u8 g, u8 b)
{
	if ((width <= 0) || (height <= 0))
	{
		return 1;
	}

	if ((s_fillQuadUsed >= s_fillQuadCapacity) || (s_fillQuadBuffer == NATIVE_GFX_INVALID))
	{
		// Counted, said out loud, and handed back so the caller clears instead.
		// The first version of this returned without drawing and without
		// clearing, which did not fall back at all - it dropped the rectangle.
		//
		// The room is raised for the next frame, so a limit that is reached
		// stops being a limit rather than staying one.
		s_fillQuadOverflows++;

		if (!s_fillQuadOverflowReported)
		{
			s_fillQuadOverflowReported = 1;
			Platform_LogWarn("[CTR Native] more than %d fill rectangles in one frame - clearing the rest, and taking more room next frame\n",
			                 s_fillQuadCapacity);
		}

		return 0;
	}

	const int slot = s_fillQuadUsed++;

	if (s_fillQuadUsed > s_fillQuadPeak)
	{
		s_fillQuadPeak = s_fillQuadUsed;
	}
	// The fill rectangles of the PSX lie in the frame buffer, that is in 512 columns; the
	// geometry has lain in the canvas since the canvas became wide, and this quad projects through the canvas.
	// So the two edges are mapped - both
	// edges and not the width scaled, otherwise two adjoining
	// rectangles drift against each other. That is the same stretch that the
	// display used to put on the whole picture, and at 4:3 the identity, because
	// canvas and frame buffer are the same number then.
	int canvasW = 0;
	int displayW = 0;

	NativeRenderer_GetCanvasSize(&canvasW, NULL);
	NativeRenderer_GetDisplaySize(&displayW, NULL);

	const s16 x0 = (s16)NativeRenderer_MapAxis(left, displayW, canvasW);
	const s16 y0 = (s16)top;
	const s16 x1 = (s16)NativeRenderer_MapAxis(left + width, displayW, canvasW);
	const s16 y1 = (s16)(top + height);

	GrVertex corners[NATIVE_FILL_QUAD_VERTICES];
	memset(corners, 0, sizeof(corners));

	// Two triangles over the rectangle: top-left, top-right, bottom-left, then
	// top-right, bottom-right, bottom-left.
	const s16 xs[NATIVE_FILL_QUAD_VERTICES] = {x0, x1, x0, x1, x1, x0};
	const s16 ys[NATIVE_FILL_QUAD_VERTICES] = {y0, y0, y1, y0, y1, y1};

	for (int i = 0; i < NATIVE_FILL_QUAD_VERTICES; i++)
	{
		corners[i].x = xs[i];
		corners[i].y = ys[i];
		corners[i].bright = 1;
		corners[i].dither = 0;
		corners[i].r = r;
		corners[i].g = g;
		corners[i].b = b;
		corners[i].a = 255;
	}

	const ShaderID previousShader = s_previousShader;
	const ShaderID previousPsxShader = s_psxShader;
	const TextureID previousTexture = s_lastBoundTexture;
	const BlendMode previousBlendMode = s_previousBlendMode;
	const int previousScissorState = s_previousScissorState;
	const int previousMaskSet = s_psxUniforms.psxDrawMaskSet;
	const int previousBilinear = s_psxUniforms.bilinearFilter;
	float previousProjection[16];
	float previousTexelSize[2];

	memcpy(previousProjection, s_psxUniforms.projection, sizeof(previousProjection));
	memcpy(previousTexelSize, s_psxUniforms.texelSize, sizeof(previousTexelSize));

	const int firstVertex = slot * NATIVE_FILL_QUAD_VERTICES;

	NativeGfx_UpdateVertexBuffer(s_fillQuadBuffer, firstVertex * (int)sizeof(GrVertex), (int)sizeof(corners), corners);

	// The white texture is 1x1, so the sampled texel is the same wherever the
	// coordinate lands and the filter cannot matter.
	NativeRenderer_SetTexture(s_whiteTexture, TF_32_BIT_RGBA);
	NativeRenderer_SetOverrideTextureSize(1, 1);
	NativeRenderer_SetPSXTextureSemiTransPass(0);
	s_psxUniforms.psxDrawMaskSet = 0;
	s_psxUniforms.bilinearFilter = 0;
	NativeRenderer_MarkPSXUniformsDirty();

	// Set here rather than trusted: a fill can arrive before the frame's first
	// batch, and then nobody has set one yet.
	NativeRenderer_SetProjection(&activeDrawEnv.clip, &activeDispEnv, 0);

	NativeRenderer_SetBlendMode(BM_NONE);
	NativeRenderer_SetScissorState(0);

	NativeGfx_BindVertexBuffer(s_fillQuadBuffer);
	NativeRenderer_DrawTriangles(firstVertex, 2);

	if (s_boundVertexBuffer >= 0)
	{
		NativeGfx_BindVertexBuffer(s_vertexBuffer[s_boundVertexBuffer]);
	}
	else
	{
		NativeGfx_BindVertexBuffer(NATIVE_GFX_INVALID);
	}

	memcpy(s_psxUniforms.projection, previousProjection, sizeof(previousProjection));
	memcpy(s_psxUniforms.texelSize, previousTexelSize, sizeof(previousTexelSize));
	s_psxUniforms.psxDrawMaskSet = previousMaskSet;
	s_psxUniforms.bilinearFilter = previousBilinear;
	NativeRenderer_MarkPSXUniformsDirty();

	NativeGfx_BindProgram(previousShader == (ShaderID)-1 ? 0 : previousShader);
	NativeGfx_BindTexture(0, previousTexture == (TextureID)-1 ? 0 : previousTexture, NATIVE_GFX_FILTER_KEEP);
	s_previousShader = previousShader;
	s_psxShader = previousPsxShader;
	s_lastBoundTexture = previousTexture;

	s_previousBlendMode = BM_NONE;
	s_previousScissorState = 0;
	NativeRenderer_SetBlendMode(previousBlendMode);
	NativeRenderer_SetScissorState(previousScissorState);

	return 1;
}

void NativeRenderer_Clear(int x, int y, int w, int h, u8 r, u8 g, u8 b)
{
	if ((w <= 0) || (h <= 0))
	{
		return;
	}

	int displayX = activeDispEnv.disp.x;
	int displayY = activeDispEnv.disp.y;
	int displayW = activeDispEnv.disp.w;
	int displayH = activeDispEnv.disp.h;

	if ((displayW <= 0) || (displayH <= 0))
	{
		displayX = activeDrawEnv.clip.x;
		displayY = activeDrawEnv.clip.y;
		displayW = activeDrawEnv.clip.w;
		displayH = activeDrawEnv.clip.h;
	}

	if ((displayW <= 0) || (displayH <= 0))
	{
		return;
	}

	const int clearRight = x + w;
	const int clearBottom = y + h;
	const int displayRight = displayX + displayW;
	const int displayBottom = displayY + displayH;

	const int overlapX = x > displayX ? x : displayX;
	const int overlapY = y > displayY ? y : displayY;
	const int overlapRight = clearRight < displayRight ? clearRight : displayRight;
	const int overlapBottom = clearBottom < displayBottom ? clearBottom : displayBottom;

	if ((overlapRight <= overlapX) || (overlapBottom <= overlapY))
	{
		return;
	}

	const int relX = overlapX - displayX;
	const int relBottom = overlapBottom - displayY;
	int scissorX = relX;
	int scissorY = displayH - relBottom;

	// The experiment. Only the partial case: a fill that covers the whole
	// display is the one case where a clear and a quad do the same thing, and
	// the clear is the cheaper way to say it.
	if (s_fillRectAsDraw)
	{
		const int coversWholeDisplay =
		    (overlapX == displayX) && (overlapY == displayY) && ((overlapRight - overlapX) == displayW) && ((overlapBottom - overlapY) == displayH);

		if (!coversWholeDisplay)
		{
			if (NativeRenderer_FillRectByDraw(relX, overlapY - displayY, overlapRight - overlapX, overlapBottom - overlapY, r, g, b))
			{
				return;
			}

			// No room this frame. Falling through to the clear below is the
			// fallback, and it is the whole reason this returns a result.
		}
	}
	int scissorW = overlapRight - overlapX;
	int scissorH = overlapBottom - overlapY;

	if ((scissorW <= 0) || (scissorH <= 0))
	{
		return;
	}

	// From the renderer's record, not the driver's. As before, the clear colour
	// is deliberately left changed - only the scissor is put back.
	const int previousScissorEnabled = s_previousScissorState;
	int previousScissorBox[4];

	memcpy(previousScissorBox, s_scissorRect, sizeof(previousScissorBox));

	// Game coordinates again, so the same mapping as the clip box above. This is
	// the fallback the fill-rectangle draw path falls through to when it runs
	// out of room; the draw path itself needs no mapping, because it goes
	// through the projection and the viewport like any other geometry.
	//
	// Both edges mapped, then the width taken as their difference - see
	// NativeRenderer_MapAxis. At a whole factor that is the old multiplication
	// for every one of the four numbers.
	{
		int clearTargetW = 0;
		int clearTargetH = 0;
		int mapFromW = 0;
		int mapFromH = 0;
		int mappedX;
		int mappedY;

		NativeRenderer_ActiveViewportSize(&clearTargetW, &clearTargetH);
		NativeRenderer_GetDisplaySize(&mapFromW, &mapFromH);

		mappedX = NativeRenderer_MapAxis(scissorX, mapFromW, clearTargetW);
		mappedY = NativeRenderer_MapAxis(scissorY, mapFromH, clearTargetH);

		scissorW = NativeRenderer_MapAxis(scissorX + scissorW, mapFromW, clearTargetW) - mappedX;
		scissorH = NativeRenderer_MapAxis(scissorY + scissorH, mapFromH, clearTargetH) - mappedY;
		scissorX = mappedX;
		scissorY = mappedY;
	}

	NativeRenderer_SetScissorState(1);
	NativeRenderer_SetScissorRect(scissorX, scissorY, scissorW, scissorH);
	NativeRenderer_SetClearColor(NativeRenderer_PSXColorComponentFloat(r), NativeRenderer_PSXColorComponentFloat(g),
	                             NativeRenderer_PSXColorComponentFloat(b), 0.0f);
	NativeGfx_ClearColorBuffer();

	NativeRenderer_SetScissorState(previousScissorEnabled);

	if (previousScissorEnabled)
	{
		NativeRenderer_SetScissorRect(previousScissorBox[0], previousScissorBox[1], previousScissorBox[2], previousScissorBox[3]);
	}
}

void NativeRenderer_SaveVRAM(const char *outputFileName, int x, int y, int width, int height, int bReadFromFrameBuffer)
{
#define FLIP_Y (VRAM_HEIGHT - i - 1)

	(void)x;
	(void)y;
	(void)bReadFromFrameBuffer;

	NativeRenderer_SyncGpuVRAMToCPU(0, 0, VRAM_WIDTH, VRAM_HEIGHT);

	FILE *fp = fopen(outputFileName, "wb");
	if (fp == NULL)
	{
		return;
	}

	u8 TGAheader[12] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	u8 header[6];
	header[0] = (width % 256);
	header[1] = (width / 256);
	header[2] = (height % 256);
	header[3] = (height / 256);
	header[4] = 16;
	header[5] = 0;

	fwrite(TGAheader, sizeof(u8), 12, fp);
	fwrite(header, sizeof(u8), 6, fp);

	for (int i = 0; i < VRAM_HEIGHT; i++)
	{
		fwrite(s_vram.cpuPixels + VRAM_WIDTH * FLIP_Y, sizeof(u16), VRAM_WIDTH, fp);
	}

	fclose(fp);

#undef FLIP_Y
}

internal void NativeRenderer_SyncGpuVRAMToCPU(int x, int y, int w, int h)
{
	RECT16 readRect;

	if (!NativeRenderer_ClipVRAMRect(&readRect, x, y, w, h))
	{
		return;
	}

	const int tileX0 = readRect.x / NATIVE_VRAM_TILE_SIZE;
	const int tileY0 = readRect.y / NATIVE_VRAM_TILE_SIZE;
	const int tileX1 = (readRect.x + readRect.w - 1) / NATIVE_VRAM_TILE_SIZE;
	const int tileY1 = (readRect.y + readRect.h - 1) / NATIVE_VRAM_TILE_SIZE;
	b32 needsReadback = false;

	for (int tileY = tileY0; tileY <= tileY1 && !needsReadback; tileY++)
	{
		for (int tileX = tileX0; tileX <= tileX1; tileX++)
		{
			const int tileIndex = tileY * NATIVE_VRAM_TILE_COLS + tileX;
			if ((s_vram.gpuNewerTiles[tileIndex >> 5] & (1u << (tileIndex & 31))) != 0)
			{
				needsReadback = true;
				break;
			}
		}
	}

	if (!needsReadback)
	{
		return;
	}

	readRect.x = (s16)(tileX0 * NATIVE_VRAM_TILE_SIZE);
	readRect.y = (s16)(tileY0 * NATIVE_VRAM_TILE_SIZE);
	readRect.w = (s16)((tileX1 - tileX0 + 1) * NATIVE_VRAM_TILE_SIZE);
	readRect.h = (s16)((tileY1 - tileY0 + 1) * NATIVE_VRAM_TILE_SIZE);

	// CPU writes must reach the persistent texture before a GPU-authored region
	// is read back, preserving PS1 VRAM command order in the split host mirror.
	NativeRenderer_UpdateVRAM();

	NativePerf_BeginScope(NATIVE_PERF_BUCKET_FRAMEBUFFER_READBACK);
	// Pack row length and alignment used to be saved and restored here; the
	// device does that around the read itself now.
	// The old spelling bound GL_READ_FRAMEBUFFER alone, leaving the draw
	// framebuffer untouched. Binding a target moves both, so the previous one
	// is put back below. Nothing draws in between, so that is the same thing.
	const NativeGfxTarget previousTarget = s_currentTarget;

	NativeRenderer_BindTarget(s_vramTarget);
	NativeGfx_ReadPixels(readRect.x, readRect.y, readRect.w, readRect.h, NATIVE_GFX_TEXFMT_RG8,
	                     s_vram.cpuPixels + (size_t)readRect.y * VRAM_WIDTH + readRect.x, VRAM_WIDTH);

	for (int tileY = tileY0; tileY <= tileY1; tileY++)
	{
		for (int tileX = tileX0; tileX <= tileX1; tileX++)
		{
			const int tileIndex = tileY * NATIVE_VRAM_TILE_COLS + tileX;
			s_vram.gpuNewerTiles[tileIndex >> 5] &= ~(1u << (tileIndex & 31));
		}
	}

	NativeRenderer_BindTarget(previousTarget);
	NativePerf_EndScope(NATIVE_PERF_BUCKET_FRAMEBUFFER_READBACK);
}

internal void NativeRenderer_ResolveVRAMRead(int x, int y, int w, int h)
{
	NativeRenderer_SyncGpuVRAMToCPU(x, y, w, h);
}

internal int NativeRenderer_RectEquals(const RECT16 *a, const RECT16 *b)
{
	return a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h;
}

internal void NativeRenderer_FlushOffscreenToVRAM(void)
{
	if (s_previousOffscreen.w <= 0 || s_previousOffscreen.h <= 0)
	{
		return;
	}

	// NOTE(aalhendi): Native offscreen draws produce RGBA pixels. Pack them into
	// the persistent 5:5:5:1 VRAM texture instead of reading them through the CPU.
	// One texel in, one texel out, and now said by the numbers rather than by a
	// claim: the offscreen target is allocated at the rectangle's own size, so
	// the source size handed over IS the destination size and the box degenerates
	// to the single tap it always was. It used to be a hand-written 1 beside a
	// comment explaining why 1 was right.
	NativeRenderer_GpuPackTextureToVRAM(NativeGfx_TargetTexture(s_offscreenRenderTarget), s_previousOffscreen.x, s_previousOffscreen.y, s_previousOffscreen.w,
	                                    s_previousOffscreen.h, NativeGfx_TargetWidth(s_offscreenRenderTarget),
	                                    NativeGfx_TargetHeight(s_offscreenRenderTarget), true);
}

// Every scissor rect and clear colour the renderer sets goes through these two,
// so s_scissorRect and s_clearColor stay a faithful record of what the device
// was told. Nothing else may call NativeGfx_SetScissorRect or
// NativeGfx_ClearColor, or the record drifts from the device.
//
// They exist because the base asked the driver - glGetIntegerv(GL_SCISSOR_BOX),
// glGetFloatv(GL_COLOR_CLEAR_VALUE) - and that is a question Vulkan cannot
// answer. A record of what was last set is the portable form of the same thing.
internal void NativeRenderer_SetScissorRect(int x, int y, int width, int height)
{
	s_scissorRect[0] = x;
	s_scissorRect[1] = y;
	s_scissorRect[2] = width;
	s_scissorRect[3] = height;

	NativeGfx_SetScissorRect(x, y, width, height);
}

internal void NativeRenderer_SetClearColor(float r, float g, float b, float a)
{
	s_clearColor[0] = r;
	s_clearColor[1] = g;
	s_clearColor[2] = b;
	s_clearColor[3] = a;

	NativeGfx_ClearColor(r, g, b, a);
}

internal void NativeRenderer_SetScissorState(int enable)
{
	if (s_previousScissorState == enable)
	{
		return;
	}

	// The early return above means enable != s_previousScissorState here, so
	// passing enable is identical to the old "toggle away from previous"
	// spelling, and says what it means.
	NativeGfx_SetScissorEnabled(enable);
	s_previousScissorState = enable;
}

void NativeRenderer_SetOffscreenState(const RECT16 *offscreenRect, int enable)
{
	const int sameOffscreenRect = NativeRenderer_RectEquals(&s_previousOffscreen, offscreenRect);
	if (!enable && !s_previousOffscreenState)
	{
		return;
	}

	if (enable && s_previousOffscreenState && sameOffscreenRect)
	{
		return;
	}

	if (enable)
	{
		// MEASURING LINE: which rectangles outside the screen arrive here.
		// Only when the rectangle changes, capped.
		if (!sameOffscreenRect)
		{
			static int s_offscreenLines = 0;
			if (s_offscreenLines < 2000)
			{
				s_offscreenLines++;
				Platform_Log("[CTR Offscreen] rect (%d,%d,%d,%d) after (%d,%d,%d,%d)\n", offscreenRect->x, offscreenRect->y, offscreenRect->w, offscreenRect->h, s_previousOffscreen.x, s_previousOffscreen.y, s_previousOffscreen.w, s_previousOffscreen.h);
			}
		}

		if (s_previousOffscreenState)
		{
			NativeRenderer_FlushOffscreenToVRAM();
		}

		s_previousOffscreenState = 1;
		NativeRenderer_EnsureRenderTarget(s_offscreenRenderTarget, offscreenRect->w, offscreenRect->h);
		s_previousOffscreen = *offscreenRect;
		NativeRenderer_LoadRenderTargetFromVRAM(s_offscreenRenderTarget, offscreenRect->x, offscreenRect->y, offscreenRect->w, offscreenRect->h);
	}
	else
	{
		s_previousOffscreenState = 0;

		NativeRenderer_FlushOffscreenToVRAM();
		NativeRenderer_BindMainRenderTarget();
		NativeRenderer_SetViewPort(0, 0, NativeGfx_TargetWidth(s_mainRenderTarget), NativeGfx_TargetHeight(s_mainRenderTarget));
	}
}

void NativeRenderer_SetProjection(const RECT16 *drawRect, const DISPENV *displayEnv, int offscreen)
{
	if (offscreen)
	{
		NativeRenderer_Ortho2D(0, drawRect->w, drawRect->h, 0, -1.0f, 1.0f);
		return;
	}

	// THE RIGHT EDGE IS THE CANVAS, NOT THE FRAME BUFFER.
	//
	// This mapping is the reason why an arbitrarily large render target
	// works: it puts 0..canvas onto the whole viewport, whatever size
	// the target has. Before the canvas became wide it was the
	// frame buffer, that is 512 - and exactly that is why a 918-column
	// world would have run 44 percent off the picture to the right.
	//
	// The height still comes from the display environment; the canvas says nothing
	// about it.
	int canvasW = 0;
	const int displayH = displayEnv->disp.h > 0 ? displayEnv->disp.h : 1;

	NativeRenderer_GetCanvasSize(&canvasW, NULL);
	NativeRenderer_Ortho2D(0, canvasW, displayH, 0, -1.0f, 1.0f);
}

// NOTE(aalhendi): Pack an RGBA render texture straight into the RG8 VRAM texture
// on the GPU, no CPU round trip. Restore or invalidate the render-state caches
// disturbed by this native bridge before the submit run continues.
// THE BOX ONE DESTINATION PIXEL AVERAGES.
//
// Used by both places a picture is reduced in this renderer: the pack back into
// VRAM, which reduces by the internal factor, and the presentation resolve,
// which reduces by whatever the window happens to be. One function, because the
// two reductions are the same question asked about different pairs of sizes.
//
// ratio is source texels per destination pixel. At or below one there is nothing
// to reduce and the answer is a single tap with a zero offset, which is exactly
// the expression both passes had before the box existed - so anything that was
// not being reduced is bit for bit unchanged.
//
// Above one the box is ceil(ratio) samples spread over exactly ratio texels,
// weighted alike and centred on the destination pixel. At an integer ratio the
// taps land on the source texel centres, each once, and the average is the exact
// box - proven over every destination pixel in tools/resolve_check.py.
//
// The cap is CTR_RESOLVE_TAPS_MAX and it says so when it bites. A limit that
// takes effect silently would look like a clean resolve that quietly is not one.
internal void NativeRenderer_BuildResolveBox(struct NativeBlitResolveUniforms *block, int sourceW, int sourceH, int destW, int destH)
{
	local_persist int s_reportedTapCap = 0;

	float ratioX;
	float ratioY;
	int tapsX;
	int tapsY;

	block->taps[0] = 1.0f;
	block->taps[1] = 1.0f;
	block->taps[2] = 0.0f;
	block->taps[3] = 0.0f;

	if ((sourceW <= 0) || (sourceH <= 0) || (destW <= 0) || (destH <= 0))
	{
		return;
	}

	ratioX = (float)sourceW / (float)destW;
	ratioY = (float)sourceH / (float)destH;

	// ceil, in integers. The ratio is a float for the spacing below, but how many
	// samples there are must not depend on which side of a whole number a float
	// landed.
	tapsX = (sourceW > destW) ? ((sourceW + destW - 1) / destW) : 1;
	tapsY = (sourceH > destH) ? ((sourceH + destH - 1) / destH) : 1;

	if ((tapsX > CTR_RESOLVE_TAPS_MAX) || (tapsY > CTR_RESOLVE_TAPS_MAX))
	{
		if (!s_reportedTapCap)
		{
			s_reportedTapCap = 1;
			Platform_Log("[CTR Res] a resolve wants %dx%d samples for %dx%d into %dx%d, capped at %d - the box is an approximation here\n", tapsX,
			             tapsY, sourceW, sourceH, destW, destH, CTR_RESOLVE_TAPS_MAX);
		}

		if (tapsX > CTR_RESOLVE_TAPS_MAX) { tapsX = CTR_RESOLVE_TAPS_MAX; }
		if (tapsY > CTR_RESOLVE_TAPS_MAX) { tapsY = CTR_RESOLVE_TAPS_MAX; }
	}

	block->taps[0] = (float)tapsX;
	block->taps[1] = (float)tapsY;

	// The spacing between samples, in source uv. Zero at one tap, so that case
	// cannot pick up an offset from an arithmetic accident.
	block->taps[2] = (tapsX > 1) ? ((ratioX / (float)tapsX) / (float)sourceW) : 0.0f;
	block->taps[3] = (tapsY > 1) ? ((ratioY / (float)tapsY) / (float)sourceH) : 0.0f;
}

// THE SOURCE SIZE IS THE TEXTURE'S, NOT A FACTOR'S.
//
// There used to be a NativeRenderer_BuildResolveForScale here that worked the
// source size out as destination times the internal factor, and its own comment
// argued for it: the factor is what the caller knows, and reading the size back
// from the device would be a second answer to a settled question.
//
// It was the wrong way round, and this is our recurring shape. The texture's
// size is the fact; the factor is a way of predicting it that is only right
// while the target is the display times a whole number. The two agreed for as
// long as that held, which is exactly what made it dangerous - at NATIVE they
// stop agreeing, and the failure would have been silent: the box would average
// over the wrong span and pack a picture that is not the one on screen, with
// nothing anywhere saying so.
//
// So the size travels with the texture now, and BuildResolveBox is called
// directly. At every whole factor the pair of numbers is the same pair it was.
internal void NativeRenderer_GpuPackTextureToVRAM(TextureID sourceTexture, int x, int y, int w, int h, int sourceW, int sourceH, b32 flipY)
{
	const ShaderID previousShader = s_previousShader;
	const TextureID previousTexture = s_lastBoundTexture;
	const BlendMode previousBlendMode = s_previousBlendMode;
	const int previousScissorState = s_previousScissorState;

	NativeRenderer_UpdateVRAM();

	NativeRenderer_BindTarget(s_vramTarget);

	NativeGfx_SetBlendMode(BM_NONE);
	NativeGfx_SetScissorEnabled(0);
	NativeGfx_SetViewport(x, y, w, h);

	NativeGfx_BindProgram(s_packShader);

	// LINEAR, said rather than inherited. This used to bind with KEEP, which
	// means whatever filter the previous draw left behind - and at one texel in,
	// one texel out that could not be noticed, because the tap lands on a texel
	// centre either way. Above factor one it decides what fifteen of sixteen
	// samples are worth, so it is not something to leave to what came before.
	NativeGfx_BindTexture(0, sourceTexture, NATIVE_GFX_FILTER_LINEAR);

	{
		struct NativeBlitResolveUniforms packBlock = {.flipY = flipY};

		NativeRenderer_BuildResolveBox(&packBlock, sourceW, sourceH, w, h);
		NativeGfx_UpdateUniforms(s_packShader, &packBlock);
	}

	NativeGfx_BindVertexBuffer(s_vramQuadBuffer);
	NativeRenderer_DrawTriangles(0, 2);
	if (s_boundVertexBuffer >= 0)
	{
		NativeGfx_BindVertexBuffer(s_vertexBuffer[s_boundVertexBuffer]);
	}
	else
	{
		NativeGfx_BindVertexBuffer(NATIVE_GFX_INVALID);
	}

	NativeRenderer_RebindDrawTarget();

	NativeGfx_BindProgram(previousShader == (ShaderID)-1 ? 0 : previousShader);
	NativeGfx_BindTexture(0, previousTexture == (TextureID)-1 ? 0 : previousTexture, NATIVE_GFX_FILTER_KEEP);
	s_previousShader = previousShader;
	s_lastBoundTexture = previousTexture;
	s_previousBlendMode = BM_NONE;
	s_previousScissorState = 0;
	NativeRenderer_SetBlendMode(previousBlendMode);
	NativeRenderer_SetScissorState(previousScissorState);
	NativeRenderer_MarkGpuVRAMNewer(x, y, w, h);
}

// NOTE(aalhendi): PS1 draws into VRAM and can texture from that same VRAM. Native
// mirrors that by flushing pending CPU VRAM writes, then packing the presented
// framebuffer into the persistent RG8 VRAM texture. CPU-side VRAM reads pull from
// that packed texture lazily, avoiding the old per-frame GPU->CPU->GPU round trip.
void NativeRenderer_StoreFrameBuffer(int x, int y, int w, int h)
{
	NativePerf_BeginScope(NATIVE_PERF_BUCKET_FRAMEBUFFER_STORE);

	NativeRenderer_GpuPackTextureToVRAM(NativeGfx_TargetTexture(s_mainRenderTarget), x, y, w, h, NativeGfx_TargetWidth(s_mainRenderTarget),
	                                    NativeGfx_TargetHeight(s_mainRenderTarget), true);

	NativePerf_EndScope(NATIVE_PERF_BUCKET_FRAMEBUFFER_STORE);
}

internal int NativeRenderer_LargerCut(int a, int b)
{
	return (a > b) ? a : b;
}

void NativeRenderer_CopyVRAM(u16 *src, int x, int y, int w, int h, int dst_x, int dst_y)
{
	int stride = w;
	const int fromVram = (src == NULL);

	if (fromVram)
	{
		// NOTE(aalhendi): MoveImage reads exactly its PS1 VRAM source rectangle. Resolve only that
		// GPU-authored region into the CPU mirror before copying it.
		NativeRenderer_ResolveVRAMRead(x, y, w, h);
		src = s_vram.cpuPixels;
		stride = VRAM_WIDTH;
	}

	// How much of the rectangle has to come off each side for both ends of the
	// copy to stay inside what they are allowed to touch. One set of cuts, taken
	// as the larger of what each end asks for, so source and destination keep
	// the offset between them: a copy that loses its left edge loses the same
	// left edge at both ends, and the pixels that do land keep the rows and
	// columns they came from.
	//
	// The source is only cut when the source IS VRAM. A caller-supplied buffer
	// has a size this layer does not know, and inventing a bound for it would be
	// a guess; the destination cut already limits how much of it is read.
	int cutLeft = 0;
	int cutTop = 0;
	int cutRight = 0;
	int cutBottom = 0;

	if (s_vramCopyClamp)
	{
		cutLeft = NativeRenderer_LargerCut(cutLeft, -dst_x);
		cutTop = NativeRenderer_LargerCut(cutTop, -dst_y);
		cutRight = NativeRenderer_LargerCut(cutRight, (dst_x + w) - VRAM_WIDTH);
		cutBottom = NativeRenderer_LargerCut(cutBottom, (dst_y + h) - VRAM_HEIGHT);

		if (fromVram)
		{
			cutLeft = NativeRenderer_LargerCut(cutLeft, -x);
			cutTop = NativeRenderer_LargerCut(cutTop, -y);
			cutRight = NativeRenderer_LargerCut(cutRight, (x + w) - VRAM_WIDTH);
			cutBottom = NativeRenderer_LargerCut(cutBottom, (y + h) - VRAM_HEIGHT);
		}

		cutLeft = NativeRenderer_LargerCut(cutLeft, 0);
		cutTop = NativeRenderer_LargerCut(cutTop, 0);
		cutRight = NativeRenderer_LargerCut(cutRight, 0);
		cutBottom = NativeRenderer_LargerCut(cutBottom, 0);
	}

	const int copyW = w - cutLeft - cutRight;
	const int copyH = h - cutTop - cutBottom;

	if ((cutLeft | cutTop | cutRight | cutBottom) != 0)
	{
		// Said once per rectangle that needed cutting, not once per run: a count
		// that only ever goes up is what makes "it never fires on the disc's own
		// data" a measurement instead of a belief.
		s_vramCopyClipped++;
		Platform_Log("[CTR Native] VRAM copy %dx%d from (%d,%d) to (%d,%d) cut by l%d t%d r%d b%d - %d so far\n", w, h, x, y, dst_x, dst_y, cutLeft,
		             cutTop, cutRight, cutBottom, s_vramCopyClipped);
	}

	if ((copyW <= 0) || (copyH <= 0))
	{
		return;
	}

	// Only the destination: the source of a move is read, not written.
	NativeRenderer_NoteStripWrite(fromVram ? NATIVE_STRIP_MOVE : NATIVE_STRIP_LOAD, dst_x + cutLeft, dst_y + cutTop, copyW, copyH, -1);

	src += (x + cutLeft) + (y + cutTop) * stride;

	u16 *dst = s_vram.cpuPixels + (dst_x + cutLeft) + (dst_y + cutTop) * VRAM_WIDTH;

	for (int i = 0; i < copyH; i++)
	{
		SDL_memcpy(dst, src, copyW * sizeof(u16));
		dst += VRAM_WIDTH;
		src += stride;
	}

	NativeRenderer_MarkVRAMDirty(dst_x + cutLeft, dst_y + cutTop, copyW, copyH);
}

void NativeRenderer_SetVRAMCopyClamp(int enable)
{
	s_vramCopyClamp = enable ? 1 : 0;
	Platform_Log("[CTR Native] VRAM copies: %s\n", enable ? "cut to VRAM" : "written as given");
}

int NativeRenderer_GetVRAMCopyClippedCount(void)
{
	return s_vramCopyClipped;
}

void NativeRenderer_ReadVRAM(u16 *dst, int x, int y, int dst_w, int dst_h)
{
	NativeRenderer_ResolveVRAMRead(x, y, dst_w, dst_h);

	// A rectangle that reaches beyond VRAM used to read
	// behind cpuPixels (cpuDirtyRects and gpuNewerTiles lie there). The PS1
	// folds the address: x modulo 1024, y modulo 512. That is how it is read here
	// as soon as the rectangle touches the edge; the normal case (fully inside) stays
	// the row copy from before. Counted, because no log says whether it happens.
	if ((x < 0) || (y < 0) || (dst_w <= 0) || (dst_h <= 0) || ((x + dst_w) > VRAM_WIDTH) || ((y + dst_h) > VRAM_HEIGHT))
	{
		local_persist int s_readWrapSaid = 0;

		if (s_readWrapSaid < 10)
		{
			s_readWrapSaid++;
			Platform_Log("[CTR Renderer] ReadVRAM (%d,%d,%d,%d) reaches outside VRAM - read with the PS1 wrap-around\n", x, y, dst_w, dst_h);
		}

		for (int i = 0; i < dst_h; i++)
		{
			const int row = (y + i) & (VRAM_HEIGHT - 1);

			for (int j = 0; j < dst_w; j++)
			{
				dst[(size_t)i * (size_t)dst_w + (size_t)j] = s_vram.cpuPixels[row * VRAM_WIDTH + ((x + j) & (VRAM_WIDTH - 1))];
			}
		}

		return;
	}

	u16 *src = s_vram.cpuPixels + x + VRAM_WIDTH * y;

	for (int i = 0; i < dst_h; i++)
	{
		SDL_memcpy(dst, src, dst_w * sizeof(u16));
		dst += dst_w;
		src += VRAM_WIDTH;
	}
}

int NativeRenderer_GetVRAMStateSize(void)
{
	return (int)sizeof(s_vram.cpuPixels);
}

int NativeRenderer_CaptureVRAMState(void *dst, int dstSize)
{
	if ((dst == NULL) || (dstSize < (int)sizeof(s_vram.cpuPixels)))
	{
		return 0;
	}

	// NOTE(aalhendi): Save-states own the CPU-side PSX VRAM mirror, not GL
	// textures. Pull pending GPU-authored VRAM into the mirror first.
	NativeRenderer_SyncGpuVRAMToCPU(0, 0, VRAM_WIDTH, VRAM_HEIGHT);
	SDL_memcpy(dst, s_vram.cpuPixels, sizeof(s_vram.cpuPixels));
	return 1;
}

int NativeRenderer_RestoreVRAMState(const void *src, int srcSize)
{
	local_persist const RECT16 zeroRect = {0, 0, 0, 0};

	if ((src == NULL) || (srcSize < (int)sizeof(s_vram.cpuPixels)))
	{
		return 0;
	}

	SDL_memcpy(s_vram.cpuPixels, src, sizeof(s_vram.cpuPixels));
	s_stripStateRestores++; // the portrait strip counter: not a hit, see there
	// NOTE(aalhendi): Restored VRAM is authoritative PSX state. Host GL caches
	// are rebuildable, so mark all of VRAM dirty and drop stale bindings.
	s_vram.cpuDirtyRectCount = 0;
	SDL_memset(s_vram.gpuNewerTiles, 0, sizeof(s_vram.gpuNewerTiles));
	NativeRenderer_MarkVRAMDirty(0, 0, VRAM_WIDTH, VRAM_HEIGHT);
	// Force both targets to be resized on next use. Their dimensions used to
	// be zeroed directly; a size of 1x1 is the same "nothing matches" signal
	// now that the sizes live behind the handle, and is what they are born at.
	NativeGfx_ResizeTarget(s_mainRenderTarget, 1, 1);
	NativeGfx_ResizeTarget(s_offscreenRenderTarget, 1, 1);
	s_previousOffscreen = zeroRect;
	s_previousOffscreenState = 0;
	s_previousShader = (ShaderID)-1;
	s_lastBoundTexture = (TextureID)-1;
	return 1;
}

// After this returns, the device's view of VRAM is current - which is why the
// page store is refreshed here and nowhere else.
//
// DrawAllSplits calls this before every draw batch, so a tile is written before
// anything samples it. The GPU's own writes into VRAM need no upload and are
// already in the texture this reads from, so they are covered by the same call
// without a readback.
void NativeRenderer_UpdateVRAM(void)
{
	if (s_vram.cpuDirtyRectCount != 0)
	{
		NativePerf_BeginScope(NATIVE_PERF_BUCKET_RENDERER_UPDATE_VRAM);

		const s32 rectCount = s_vram.cpuDirtyRectCount;
		s_vram.cpuDirtyRectCount = 0;

		for (s32 i = 0; i < rectCount; i++)
		{
			const RECT16 r = s_vram.cpuDirtyRects[i];

			// Each dirty rect is a window into the full-width CPU mirror, so the
			// upload reads rows VRAM_WIDTH apart rather than packed.
			NativeGfx_UpdateTexture(s_vram.texture, r.x, r.y, r.w, r.h, NATIVE_GFX_TEXFMT_RG8, s_vram.cpuPixels + (size_t)r.y * VRAM_WIDTH + r.x,
			                        VRAM_WIDTH);
		}
		s_lastBoundTexture = (TextureID)-1;

		NativePerf_EndScope(NATIVE_PERF_BUCKET_RENDERER_UPDATE_VRAM);
	}

	NativeRenderer_FlushPageStore();
}

void NativeRenderer_PresentVRAMRect(int displayX, int displayY, int displayW, int displayH)
{
	if (displayW <= 0 || displayH <= 0)
	{
		return;
	}

	NativeRenderer_UpdateVRAM();

	NativeRenderer_SetViewPort(s_presentViewport.x, s_presentViewport.y, s_presentViewport.w, s_presentViewport.h);
	NativeRenderer_BindTarget(NATIVE_GFX_TARGET_DEFAULT);

	NativeRenderer_SetScissorState(0);
	NativeRenderer_SetBlendMode(BM_NONE);

	NativeRenderer_DrawVRAMRegion(displayX, displayY, displayW, displayH);
	NativeGfx_BindVertexBuffer(NATIVE_GFX_INVALID);

	s_previousShader = (ShaderID)-1;
	s_lastBoundTexture = (TextureID)-1;
}

// HOW A GAME PIXEL LANDS ON THE WINDOW ALONG ONE AXIS, exactly.
//
// dst/src is how many window pixels one game pixel is worth. When that divides
// evenly every block is the same size and there is nothing more to say. When it
// does not, the blocks take the two whole sizes either side of it, and which
// pixel gets which repeats with a period of src/gcd(src, dst).
//
// So: over `period` game pixels, `wideCount` of them are `wide` window pixels
// across and the rest are `narrow`. That is the whole of it, and it is a
// property of the two sizes - no filter, no sampler and no render path can
// change it. It is printed because "the blocks are uneven" is otherwise an
// impression, and an impression cannot be compared between two runs.
internal void NativeRenderer_BlockPattern(int srcSize, int dstSize, int *outNarrow, int *outWide, int *outWideCount, int *outPeriod)
{
	int divisor;
	int period;
	int perPeriod;

	*outNarrow = 0;
	*outWide = 0;
	*outWideCount = 0;
	*outPeriod = 0;

	if ((srcSize <= 0) || (dstSize <= 0))
	{
		return;
	}

	divisor = NativeRenderer_GCD(srcSize, dstSize);
	if (divisor <= 0)
	{
		return;
	}

	period = srcSize / divisor;
	perPeriod = dstSize / divisor;

	*outPeriod = period;
	*outNarrow = perPeriod / period;
	*outWide = (perPeriod / period) + 1;
	*outWideCount = perPeriod % period;
}

// THE FINAL TOTAL THAT CTRL+Q NEVER PRINTED.
//
// The peak of the vertex ring and its wraps were only in the report of
// --present-report, that is at one VBlank that one had to know beforehand. A
// drive that ends with Ctrl+Q left nothing of it behind - and "0 wraps"
// is exactly the claim the ring fix rests on. Here, through
// Platform_AtExitReport, before the log is closed, on every way out.
// Always comes, even if everything is zero: a total that is missing looks like
// one that was never counted.
void NativeRenderer_PrintExitSummary(void)
{
	Platform_Log("[CTR Res] at exit: vertex ring %s, peak %d of %d vertices in a frame, %d upload(s) in a frame at most (batches of %d), %d wrap(s); fill quads peak %d of %d\n",
	             g_cfg_vertexRing ? "on" : "off", s_vertexUploadPeak, (int)NATIVE_VERTEX_RING_SIZE, s_vertexUploadsPeak, (int)MAX_VERTEX_BUFFER_SIZE, s_vertexUploadWraps,
	             s_fillQuadPeak, s_fillQuadCapacity);
	Platform_Log("[CTR Pages] at exit: %d fills in all, %d spared, %d of %d tiles named\n", s_pages.refillsTotal, s_pages.fillsSpared, s_pages.namedCount,
	             NATIVE_PAGE_TILE_COUNT);
	NativeRenderer_PrintStripSummary();
}

void NativeRenderer_ReportPresentPath(int vblank)
{
	static const char *const formatNames[2] = {"psx", "32-bit"};

	struct NativeBlitResolveUniforms probe = {.flipY = 0};
	int displayW = 0;
	int displayH = 0;
	int targetW = 0;
	int targetH = 0;
	int tilesUsed = 0;
	int tilesTotal = 0;
	int tileKiB = 0;
	int i;
	const int scale = NativeRenderer_TargetScale();
	const int viaMainTarget = NativeRenderer_PresentsViaMainTarget();

	NativeRenderer_GetDisplaySize(&displayW, &displayH);
	targetW = NativeGfx_TargetWidth(s_mainRenderTarget);
	targetH = NativeGfx_TargetHeight(s_mainRenderTarget);
	NativeRenderer_GetPageStoreUse(&tilesUsed, &tilesTotal, &tileKiB);

	// The same call the present pass makes, so what is printed is what is used
	// rather than a second calculation of it.
	NativeRenderer_BuildResolveBox(&probe, targetW, targetH, s_presentViewport.w, s_presentViewport.h);

	Platform_Log("[CTR Res] present report, vblank %d\n", vblank);
	Platform_Log("[CTR Res]   draw env      clip %dx%d at %d,%d   isbg %d\n", activeDrawEnv.clip.w, activeDrawEnv.clip.h, activeDrawEnv.clip.x,
	             activeDrawEnv.clip.y, activeDrawEnv.isbg);
	Platform_Log("[CTR Res]   display env   %dx%d at %d,%d\n", activeDispEnv.disp.w, activeDispEnv.disp.h, activeDispEnv.disp.x, activeDispEnv.disp.y);
	if (NativeRenderer_ResolutionIsNative())
	{
		Platform_Log("[CTR Res]   factor        NATIVE - the target is the window's own grid, not a multiple of the display\n");
	}
	else
	{
		Platform_Log("[CTR Res]   factor        setting x%d, in force x%d\n", NativeRenderer_GetResolutionScale(), scale);
	}
	{
		int canvasW = 0;
		int canvasH = 0;

		NativeRenderer_GetCanvasSize(&canvasW, &canvasH);
		Platform_Log("[CTR Res]   canvas        %dx%d   (frame buffer %dx%d)\n", canvasW, canvasH, displayW, displayH);
	}
	Platform_Log("[CTR Res]   main target   %dx%d   (display %dx%d)\n", targetW, targetH, displayW, displayH);
	Platform_Log("[CTR Res]   window        viewport %dx%d at %d,%d of %dx%d\n", s_presentViewport.w, s_presentViewport.h, s_presentViewport.x,
	             s_presentViewport.y, g_windowWidth, g_windowHeight);

	// The scene pass is the only one of the four that grows with the target, so
	// its area IS the fill cost, and the multiple is against the same picture at
	// the console's own size. Printed rather than worked out on paper.
	if ((displayW > 0) && (displayH > 0))
	{
		const s64 scenePixels = (s64)targetW * (s64)targetH;
		const s64 basePixels = (s64)displayW * (s64)displayH;
		const int timesMilli = (int)((scenePixels * 1000) / basePixels);

		Platform_Log("[CTR Res]   scene pixels  %lld per pass, %d.%03d times the display's own\n", (long long)scenePixels, timesMilli / 1000,
		             timesMilli % 1000);
	}

	{
		const int oneToOne = (targetW == s_presentViewport.w) && (targetH == s_presentViewport.h);

		Platform_Log("[CTR Res]   present path  %s\n",
		             viaMainTarget ? (oneToOne ? "main target, NEAREST" : "main target, LINEAR") : "VRAM texture, NEAREST");
	}

	if (viaMainTarget && (targetW > 0) && (targetH > 0))
	{
		// Magnifying or reducing, and by how much. The taps say which of the two
		// the resolve thinks it is doing; the ratio says which it actually is. A
		// disagreement between those two is the bug this report exists to find.
		const int magnifyingX = (s_presentViewport.w >= targetW);
		const int magnifyingY = (s_presentViewport.h >= targetH);

		Platform_Log("[CTR Res]   last step     %s x%d.%03d, %s y%d.%03d\n", magnifyingX ? "magnify" : "reduce",
		             (s_presentViewport.w * 1000 / targetW) / 1000, (s_presentViewport.w * 1000 / targetW) % 1000, magnifyingY ? "magnify" : "reduce",
		             (s_presentViewport.h * 1000 / targetH) / 1000, (s_presentViewport.h * 1000 / targetH) % 1000);
		Platform_Log("[CTR Res]   resolve       %d x %d taps, step %.6f %.6f%s\n", (int)probe.taps[0], (int)probe.taps[1], probe.taps[2], probe.taps[3],
		             ((int)probe.taps[0] == 1 && (int)probe.taps[1] == 1) ? "  (one tap: the sampler's own bilinear)" : "");
	}

	// THE ACCEPTANCE LINE.
	//
	// One texel per pixel means three things at once and all three are checked
	// here: the target is exactly the viewport, the resolve is one tap with a
	// zero step, and the sampler bound is NEAREST. Two of the three being true
	// is not the claim.
	{
		const int sizesMatch = (targetW == s_presentViewport.w) && (targetH == s_presentViewport.h);
		const int oneTap = ((int)probe.taps[0] == 1) && ((int)probe.taps[1] == 1);
		const int zeroStep = (probe.taps[2] == 0.0f) && (probe.taps[3] == 0.0f);

		Platform_Log("[CTR Res]   LAST STEP     %s\n", (sizesMatch && oneTap && zeroStep)
		                                                   ? "1 tap, offset 0 - one texel per pixel, nothing is resampled"
		                                                   : "NOT one to one - the picture is resampled on the way to the window");
	}

	// WHERE UNEVENNESS COMES FROM, as arithmetic and not as an impression.
	//
	// A game pixel becomes window/display pixels wide. When that is not a whole
	// number the blocks cannot all be the same size - some are floor, some are
	// floor+1 - and no choice of filter changes that; a filter only decides
	// whether the difference is a hard edge or a blur. So the pattern is printed
	// per axis, together with how long it takes to repeat.
	//
	// Two axes and two different answers is the other half of it: a game pixel
	// that is not as wide as it is tall is a non-square pixel, whatever the
	// window's own pixels are. That is not a fault - a 512x216 buffer shown in a
	// 4:3 frame HAS non-square pixels, and the art was drawn knowing it - but it
	// is the number to look at when a picture looks unevenly stretched.
	if ((displayW > 0) && (displayH > 0) && (s_presentViewport.w > 0) && (s_presentViewport.h > 0))
	{
		int narrowX = 0, wideX = 0, wideCountX = 0, periodX = 0;
		int narrowY = 0, wideY = 0, wideCountY = 0, periodY = 0;
		const int perGameX = (int)(((s64)s_presentViewport.w * 1000) / displayW);
		const int perGameY = (int)(((s64)s_presentViewport.h * 1000) / displayH);
		const int squareness = (perGameY > 0) ? (int)(((s64)perGameX * 1000) / perGameY) : 0;

		NativeRenderer_BlockPattern(displayW, s_presentViewport.w, &narrowX, &wideX, &wideCountX, &periodX);
		NativeRenderer_BlockPattern(displayH, s_presentViewport.h, &narrowY, &wideY, &wideCountY, &periodY);

		Platform_Log("[CTR Res]   game pixel    %d.%03d window px wide, %d.%03d tall - shape %d.%03d (1.000 is square)\n", perGameX / 1000,
		             perGameX % 1000, perGameY / 1000, perGameY % 1000, squareness / 1000, squareness % 1000);

		if (wideCountX == 0)
		{
			Platform_Log("[CTR Res]   blocks x      every game pixel %d window px - even\n", narrowX);
		}
		else
		{
			Platform_Log("[CTR Res]   blocks x      %d of %d game px are %d window px, %d are %d - repeats every %d\n", wideCountX, periodX, wideX,
			             periodX - wideCountX, narrowX, periodX);
		}

		if (wideCountY == 0)
		{
			Platform_Log("[CTR Res]   blocks y      every game pixel %d window px - even\n", narrowY);
		}
		else
		{
			Platform_Log("[CTR Res]   blocks y      %d of %d game px are %d window px, %d are %d - repeats every %d\n", wideCountY, periodY, wideY,
			             periodY - wideCountY, narrowY, periodY);
		}
	}

	Platform_Log("[CTR Res]   begin scene   %d cleared, %d reloaded from VRAM (since start)\n", s_beginSceneClears, s_beginSceneReloads);
	// The claim the vertex fix rests on is that a frame never runs out of one
	// buffer. wraps is how often it did anyway; peak is how close the worst
	// frame came. A wrap is the only way the old clobber can still happen.
	Platform_Log("[CTR Res]   vertex ring   %s, peak %d of %d vertices in a frame, %d upload(s) in a frame at most, %d wrap(s)\n", g_cfg_vertexRing ? "on" : "off", s_vertexUploadPeak,
	             (int)NATIVE_VERTEX_RING_SIZE, s_vertexUploadsPeak, s_vertexUploadWraps);
	Platform_Log("[CTR Res]   psx filter    bilinearFilter uniform = %d (%s)\n", g_cfg_bilinearFiltering, g_cfg_bilinearFiltering ? "bilinear" : "nearest");
	// The uniform beside the setting, because at PACKED the two differ on
	// purpose: the setting is on and the visible pass still gets nothing.
	Platform_Log("[CTR Res]   dither        %s, uniform %.0f on the last draw  (matrix added only where 5 bits follow)\n",
	             NativeRenderer_DitherName(), (double)s_psxUniforms.psxDitherAmount);

	// The three samplers the indexed path reads through, asked of the device.
	//
	// This is the whole "are we filtering indices" question as a measurement
	// rather than as a reading of the code. The atlas holds palette INDICES: a
	// LINEAR filter on it would average index 3 with index 7 and look up index
	// 5, which is a different colour and not a blend of two. The same for the
	// VRAM texture, which holds the CLUT rows and the packed 16-bit words. All
	// three have to say NEAREST, every frame, or the indexed path is filtering
	// the wrong thing.
	{
		static const char *const filterNames[] = {"NEAREST", "LINEAR", "KEEP/none"};
		const NativeGfxFilter vramFilter = NativeGfx_TextureFilter(s_vram.texture);
		const NativeGfxFilter pagesFilter = (s_pages.texture != (TextureID)-1) ? NativeGfx_TextureFilter(s_pages.texture) : NATIVE_GFX_FILTER_KEEP;
		const NativeGfxFilter lutFilter = NativeGfx_TextureFilter(s_rgLutTexture);
		const int allNearest = (vramFilter == NATIVE_GFX_FILTER_NEAREST) && (pagesFilter == NATIVE_GFX_FILTER_NEAREST) &&
		                       (lutFilter == NATIVE_GFX_FILTER_NEAREST);

		Platform_Log("[CTR Res]   samplers      vram %s, pages %s, clut lut %s%s\n", filterNames[vramFilter], filterNames[pagesFilter],
		             filterNames[lutFilter], allNearest ? "   (indices are never interpolated)" : "   WARNING: an index is being filtered");
	}
	Platform_Log("[CTR Res]   page store    %d of %d tiles, %d KiB\n", tilesUsed, tilesTotal, tileKiB);

	for (i = 0; i < 2; i++)
	{
		Platform_Log("[CTR Res]   draws %-7s %6d this frame, %8d since start%s\n", formatNames[i], s_psxDrawsByFormatFrame[i], s_psxDrawsByFormat[i],
		             (i == 0) ? "  (4/8/16 bit chosen per vertex - primitives by format in [CTR GPU] block)" : "  (override texture, direct)");
	}
	Platform_Log("[CTR Res]   draws flat    %6d this frame, %8d since start  (no texture read)\n", s_psxDrawsUntexturedFrame,
	             s_psxDrawsUntextured);
}

void NativeRenderer_EndPresentReportFrame(void)
{
	int i;

	// The array has TWO slots (s_psxDrawsByFormatFrame[2]); an earlier
	// version's loop ran to 4 and wrote two ints per frame behind the
	// end of the array - into the neighbours in declaration order, that is into counters
	// that were thereby zeroed every frame. Computed from the array, not
	// copied.
	for (i = 0; i < (int)(sizeof(s_psxDrawsByFormatFrame) / sizeof(s_psxDrawsByFormatFrame[0])); i++)
	{
		s_psxDrawsByFormatFrame[i] = 0;
	}

	s_psxDrawsUntexturedFrame = 0;
}

// The internal picture, into a buffer the caller owns.
//
// The main target and NOT the window, because the window cannot be read:
// NativeGfxVK_ReadPixels says so itself and refuses - the swapchain image is
// not one of our textures. What was written before this existed was a black
// file and a log line claiming success.
//
// So a shot is the picture BEFORE the last step, at 512x216 times the factor.
// What is missing from it compared to the window is exactly one bilinear
// magnification - and that one is measured, so this is the half that is not.
//
// The buffer is poisoned before the read and checked after it. A backend that
// declines leaves it untouched, and a black picture and a read that never
// happened have to be tellable apart or this tool lies the way it just did.
int NativeRenderer_ReadMainTarget(void *dst, int dstBytes, int *outWidth, int *outHeight)
{
	const unsigned char poison = 0xCD;
	unsigned char *bytes = (unsigned char *)dst;
	int width = 0;
	int height = 0;
	int i;

	NativeRenderer_GetMainTargetSize(&width, &height);

	if ((dst == NULL) || (width <= 0) || (height <= 0) || (dstBytes < (width * height * 4)))
	{
		return 0;
	}

	for (i = 0; i < (width * height * 4); i++)
	{
		bytes[i] = poison;
	}

	// Bound, because that is what the read reads from. Restored afterwards to
	// the target the frame was using, or the next draw lands somewhere else.
	NativeRenderer_BindTarget(s_mainRenderTarget);
	NativeGfx_ReadPixels(0, 0, width, height, NATIVE_GFX_TEXFMT_BGRA8, dst, 0);
	NativeRenderer_RebindDrawTarget();

	for (i = 0; i < (width * height * 4); i++)
	{
		if (bytes[i] != poison)
		{
			if (outWidth != NULL) { *outWidth = width; }
			if (outHeight != NULL) { *outHeight = height; }
			return 1;
		}
	}

	return 0;
}

// WHICH OF THE TWO PRESENT ROUTES THIS FRAME TAKES.
//
// Asked here and nowhere else. Platform_EndScene picks the route and the present
// report says which one ran; both used to work it out from the factor, and both
// spelled the same comparison. One of those two would eventually have been
// edited and the other not - and the failure mode is a report that names the
// route the frame did not take, which is worse than no report.
int NativeRenderer_PresentsViaMainTarget(void)
{
	int targetW = 0;
	int targetH = 0;
	int displayW = 0;
	int displayH = 0;

	NativeRenderer_GetMainTargetSize(&targetW, &targetH);
	NativeRenderer_GetDisplaySize(&displayW, &displayH);

	// The VRAM route can only show what VRAM holds, which is the picture at
	// display size. Anything else has to come off the main target.
	return (targetW != displayW) || (targetH != displayH);
}

// Shows the main render target on the window, without the VRAM round trip.
//
// Only used when the target is not the display's own size. At factor one the
// VRAM path is the whole story and this is never called, so nothing about the
// picture changes when the factor is off.
//
// The VRAM copy is still written by NativeRenderer_StoreFrameBuffer before this
// runs - it has to be, because the game reads its own framebuffer back for
// screen-copy effects and the next frame loads from it whenever the draw
// environment does not clear. What changes is only which of the two the window
// is shown from.
void NativeRenderer_PresentMainTarget(void)
{
	NativeRenderer_SetViewPort(s_presentViewport.x, s_presentViewport.y, s_presentViewport.w, s_presentViewport.h);
	NativeRenderer_BindTarget(NATIVE_GFX_TARGET_DEFAULT);

	NativeRenderer_SetScissorState(0);
	NativeRenderer_SetBlendMode(BM_NONE);

	NativeGfx_BindProgram(s_presentTargetShader);
	{
		struct NativeBlitResolveUniforms targetBlock = {.flipY = 0};

		NativeRenderer_BuildResolveBox(&targetBlock, NativeGfx_TargetWidth(s_mainRenderTarget), NativeGfx_TargetHeight(s_mainRenderTarget),
		                               s_presentViewport.w, s_presentViewport.h);
		NativeGfx_UpdateUniforms(s_presentTargetShader, &targetBlock);
	}

	// NEAREST when the two sizes match, LINEAR when they do not.
	//
	// At one texel per pixel a linear tap lands on the texel centre and returns
	// it exactly, so this changes no picture that was already one to one - and
	// that is the reason to say it rather than leave it. "The filter happens not
	// to matter here" and "this pass does not filter" are different claims, and
	// only the second one is checkable. The report prints which was bound.
	//
	// Where they differ, LINEAR still: a factor that does not divide the window
	// evenly resolves instead of shimmering. The main target is the only texture
	// on this pass.
	{
		const int oneToOne =
		    (NativeGfx_TargetWidth(s_mainRenderTarget) == s_presentViewport.w) && (NativeGfx_TargetHeight(s_mainRenderTarget) == s_presentViewport.h);

		NativeGfx_BindTexture(0, NativeGfx_TargetTexture(s_mainRenderTarget), oneToOne ? NATIVE_GFX_FILTER_NEAREST : NATIVE_GFX_FILTER_LINEAR);
	}
	NativeGfx_BindVertexBuffer(s_vramQuadBuffer);
	NativeRenderer_DrawTriangles(0, 2);
	NativeGfx_BindVertexBuffer(NATIVE_GFX_INVALID);

	s_previousShader = (ShaderID)-1;
	s_lastBoundTexture = (TextureID)-1;
}

void NativeRenderer_PresentVRAMDisplay(void)
{
	// NOTE(aalhendi): ctr-native local divergence. Retail presents this boot
	// splash path by displaying VRAM directly after DR_MOVE packets; the native
	// the renderer otherwise swaps the current framebuffer and never shows
	// those VRAM-only copies.
	NativeRenderer_PresentVRAMRect(activeDispEnv.disp.x, activeDispEnv.disp.y, activeDispEnv.disp.w, activeDispEnv.disp.h);
}

void NativeRenderer_SwapWindow(void)
{
	NativePerf_BeginScope(NATIVE_PERF_BUCKET_SWAP_WINDOW);

	// Ends the frame that was opened either in the window init or by the
	// previous swap, presents it, and opens the next one.
	NativeGfxVK_PresentFrame();

	// The frame that was just closed is the last one that can still be reading
	// the buffer it wrote, so the next frame takes the other one and starts its
	// cursor at the front.
	NativeRenderer_BeginVertexFrame();

	// Anti-aliasing: a level that was requested during the frame (menu,
	// --msaa-at) applies from here - between the present of one frame and the first
	// draw of the next, where no pass is open.
	NativeRenderer_ApplyMsaa();

	NativePerf_EndScope(NATIVE_PERF_BUCKET_SWAP_WINDOW);
}

// THE PSX MASK BIT, AND WHY NOTHING STANDS HERE ANY MORE.
//
// NativeRenderer_EnableDepth and NativeRenderer_SetStencilMode stood at
// this place. Both were removed, because it was
// measured that both never did anything. Whoever wants the mask function later
// starts here and not from zero.
//
// WHAT IT WAS. The PSX can set a bit in a frame buffer pixel - bit 15 -
// and instruct later drawings not to draw over a pixel marked that way.
// SetStencilMode mapped that onto the stencil buffer: when
// drawing set the stamp, when checking only draw where it is not
// set.
//
// WHY IT NEVER WORKED, two independent reasons:
//
//   1. The Vulkan back end attached no depth/stencil attachment to its
//      render pass. pDepthStencilAttachment did not occur in the whole file.
//      Without an attachment Vulkan ignores the stencil state, and every fragment
//      passes the test. (With --native-preview the main target now carries a
//      depth attachment - see the end of this note. It has no stencil aspect,
//      and stencil stays unused.)
//
//   2. Even with an attachment it would have done nothing. The mode comes from
//      split->drawPrimMode, which stems from the singlePrimitive argument of
//      ParsePrimitivesLinkedList - and that is a 1 only for DrawPrim.
//      DrawOTag passes 0, and a race is exclusively OT walks.
//      Measured over one lap of Dingo Canyon including start and menus:
//      ZERO mode changes, in either direction. So a stamp is never
//      set, and a test without a stamp can reject nothing.
//
// The second reason is the harder one: it applies independently of the back end.
//
// WHAT HAPPENED TO THE ATTEMPT. An earlier version built the attachment and
// added two test states - and took everything back on the same day, because
// there was the suspicion that the attachment had broken the skybox. That
// suspicion later turned out to be wrong: the skybox was due to the fill
// rectangles. So the attachment was probably fine and was taken back for a
// reason that was not true.
//
// Whoever needs the mask function has to build the attachment again and must
// IN ADDITION make sure that anybody asks for the stamp mode at all.
//
// THE DEPTH is the same case in short: EnableDepth computed per blend change a
// flag that forced the back end to 0 and that would have been ignored without an attachment
// anyway. The PSX has no depth buffer; it sorts through the
// ordering table. There was nothing to test and there is nothing.
//
// WHAT STANDS NOW. With --native-preview the main target carries a depth
// attachment again while a native object is bound
// (NativeRenderer_WantNativeDepth), for native draws only.
// The PSX draws in such a pass keep depth test and depth write off - the draw
// state they have always had - so they neither read nor write it; the stencil
// stays unused. Without the switch no target has one, as before. The attempt
// above is the reason the switch exists: whether an attachment alone changes a
// PSX pixel is answered by comparing a run with it against a run without.

void NativeRenderer_SetBlendMode(BlendMode blendMode)
{
	if (s_previousBlendMode == blendMode)
	{
		return;
	}

	NativeGfx_SetBlendMode(blendMode);

	s_previousBlendMode = blendMode;
}

internal void NativeRenderer_SetViewPort(int x, int y, int width, int height)
{
	NativeGfx_SetViewport(x, y, width, height);
}

internal int NativeRenderer_SetWireframe(int enable)
{
	return NativeGfx_SetWireframe(enable);
}

// WHERE ONE FRAME'S GEOMETRY GOES, AND WHY IT USED TO LAND ON ITSELF
//
// A frame does not upload once. Every DrawAllSplits uploads what has piled up
// and then issues that batch's draws, and a frame runs as many of those as the
// ordering-table walk needs - normally four to six, and sixteen when something
// throws a lot of semi-transparent textured geometry at it, because a textured
// ABE split is drawn TWICE.
//
// This used to hand each upload to the other one of two buffers, at offset zero,
// turn and turn about. The draws are only RECORDED at draw time; they execute
// when the frame is submitted. So the third upload of a frame wrote over the
// first upload's bytes while the first upload's draws were still waiting to
// read them, and those draws then drew whatever the third batch had put there.
//
// That is the rocket: firing one adds a smoke trail of textured semi-transparent
// particles, the batch count goes up, and the frame starts overwriting its own
// geometry. The log said so plainly - "5 vertex write(s) overwrote data 742
// recorded draw(s) still need", from the frame the missile left the kart.
//
// So an upload now takes the NEXT FREE STRETCH of the frame's buffer instead of
// the front of the other one. Nothing a recorded draw still needs is written to
// twice, which is the whole property that was missing. The two buffers are now
// one per frame rather than one per upload - two frames can be in flight, so
// two is exactly the number needed for the same reason at the frame scale.
//
// The cursor can still run out, and then this does what it did before: back to
// the front, and say so out loud rather than let it look like it fitted. That
// counter belongs in the report, because "it never wrapped" is the claim this
// fix rests on.
// The first vertex of the last upload. A split's own start vertex is relative to
// the batch it was recorded in, so the two are added at the draw.
int NativeRenderer_VertexUploadBase(void)
{
	return g_cfg_vertexRing ? s_vertexUploadBase : 0;
}

// Called once at the frame boundary, from the swap. Flipping here and not at an
// upload is the point: the buffer a frame writes to must be one no frame still
// in flight is reading from.
internal void NativeRenderer_BeginVertexFrame(void)
{
	if (s_vertexUploadCursor > s_vertexUploadPeak)
	{
		s_vertexUploadPeak = s_vertexUploadCursor;
	}

	if (s_vertexUploadsThisFrame > s_vertexUploadsPeak)
	{
		s_vertexUploadsPeak = s_vertexUploadsThisFrame;
	}
	s_vertexUploadsThisFrame = 0;

	s_curVertexBuffer = (s_curVertexBuffer + 1) & 1;
	s_vertexUploadCursor = 0;
}

void NativeRenderer_UpdateVertexBuffer(const GrVertex *vertices, int num_vertices)
{
	NativePerf_BeginScope(NATIVE_PERF_BUCKET_RENDERER_VERTEX_UPLOAD);
	if ((u32)num_vertices >= MAX_VERTEX_BUFFER_SIZE)
	{
		NATIVE_RENDERER_ERROR("%s\n", "MAX_VERTEX_BUFFER_SIZE reached, expect rendering errors");
		num_vertices = MAX_VERTEX_BUFFER_SIZE;
	}

	int bufferIndex = s_curVertexBuffer;
	int offsetVertices = 0;

	if (g_cfg_vertexRing)
	{
		if ((s_vertexUploadCursor + num_vertices) > (int)NATIVE_VERTEX_RING_SIZE)
		{
			// One frame wanted more than a whole buffer holds. Back to the front,
			// which is the old road and can clobber again - counted, so a run
			// that hits this is not mistaken for one that fitted.
			s_vertexUploadCursor = 0;
			s_vertexUploadWraps++;
		}

		offsetVertices = s_vertexUploadCursor;
		s_vertexUploadCursor += num_vertices;
	}
	else
	{
		// The old road: the other buffer, at the front, every time.
		s_curVertexBuffer = (s_curVertexBuffer + 1) & 1;
	}

	s_vertexUploadBase = offsetVertices;
	s_boundVertexBuffer = bufferIndex;
	s_vertexUploadsThisFrame++;
	if (g_cfg_bindVertexExplicit)
	{
		// Before the upload, not after it: the upload today binds along itself,
		// and whoever binds after it hides exactly the state that is meant to be
		// checked. Set before, the binding is already right when the
		// upload no longer touches it at all.
		NativeGfx_BindVertexBuffer(s_vertexBuffer[bufferIndex]);
	}

	// The next draws do not bind again. With the switch above that is
	// covered, because the renderer bound itself; without it, it depends
	// on the upload in the back end binding along the way.
	NativeGfx_UpdateVertexBuffer(s_vertexBuffer[bufferIndex], offsetVertices * (int)sizeof(GrVertex), num_vertices * (int)sizeof(GrVertex), vertices);
	NativePerf_EndScope(NATIVE_PERF_BUCKET_RENDERER_VERTEX_UPLOAD);
}

void NativeRenderer_DrawTriangles(int start_vertex, int triangles)
{
	NativePerf_BeginScope(NATIVE_PERF_BUCKET_RENDERER_DRAW_TRIANGLES);

	// One upload per draw that needs one, here rather than in each setter: this
	// is where Vulkan binds its descriptors, and where every field is known to
	// be current. A blit draw leaves s_psxShader on whatever PSX program was
	// last selected, but the flag is only ever set by the PSX setters, so a
	// blit cannot make this fire. If it did it would rewrite the same bytes the
	// PSX program is already using, so it costs a write and changes nothing.
	if (s_psxUniformsDirty && (s_psxShader != 0))
	{
		NativeGfx_UpdateUniforms(s_psxShader, &s_psxUniforms);
		s_psxUniformsDirty = 0;
	}

	NativeGfx_Draw(start_vertex, triangles * 3);
	NativePerf_EndScope(NATIVE_PERF_BUCKET_RENDERER_DRAW_TRIANGLES);
}

void NativeRenderer_PushDebugLabel(const char *label)
{
	NativeGfx_PushDebugLabel(label);
}

void NativeRenderer_PopDebugLabel(void)
{
	NativeGfx_PopDebugLabel();
}
