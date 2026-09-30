#ifndef PLATFORM_NATIVE_GFX_H
#define PLATFORM_NATIVE_GFX_H

// The graphics device.
//
// THE RULE THIS INTERFACE IS CUT BY: a name in this file says WHAT should
// happen. It never says how a PlayStation would have done it. Semi-transparency
// passes, texture pages, CLUTs, 15-bit colour, mask bits, dither - all of that
// is PSX semantics. It lives above this line in native_renderer.c and reaches
// the device only as (a) an ordinary blend mode, (b) opaque uniform bytes whose
// meaning is a contract between the PSX layer and its shader.
//
// Ask of every field added here: does it describe what to draw, or how the PS1
// did it? The second belongs in the backend or above, never here. This is what
// keeps the 1999 look one of two presentations instead of a special case wired
// through every layer - and what made the Vulkan backend a translation rather
// than a rebuild. There is one backend now; the rule is what would make a second
// one a translation too.
//
// STILL ABOVE THE LINE, BY DESIGN, FOR NOW: device lifetime (device and
// swapchain creation), render targets, and pipelines. All three are entangled
// with native_renderer.c's own window ownership, and moving them in the same
// change as everything else would leave the tree half migrated. They land in
// their own passes, against this same rule.

#include <macros.h>
#include <platform/native_renderer_types.h>

// Opaque handles. A backend may store whatever it likes behind them; nothing
// above this line may assume they are handles, indices, or pointers.
typedef u32 NativeGfxBuffer;

#define NATIVE_GFX_INVALID ((u32) - 1)

typedef enum
{
	NATIVE_GFX_FILTER_NEAREST = 0,
	NATIVE_GFX_FILTER_LINEAR,

	// Bind without touching the texture's sampler state. Not the same as
	// picking NEAREST: several binds here deliberately inherit whatever the
	// texture was created with, and forcing a filter on them would change
	// how the image is sampled.
	NATIVE_GFX_FILTER_KEEP,
} NativeGfxFilter;

// What sampler state a texture is CURRENTLY carrying, asked of the device.
//
// Every bind that says KEEP leaves the texture's own filter standing, so the
// filter a draw samples with is the outcome of a chain of earlier binds. A
// chain is something to read back, not to reason about: the two most recent
// filters in this tree that were reasoned about - the VRAM blit and the
// bilinear shader table - were both wrong about themselves.
NativeGfxFilter NativeGfx_TextureFilter(TextureID texture);

// Deliberately not RECT16: that type arrives through psx/libgpu.h, and the
// device layer should not be including PSX headers to describe a rectangle.
typedef struct
{
	int x;
	int y;
	int w;
	int h;
} NativeGfxRect;

// Which device is underneath, for the log. There is one, and the selection that
// used to sit here went with the other one.
const char *NativeGfx_BackendName(void);

// --- Textures --------------------------------------------------------------
//
// RG8 is here as a plain two-channel 8-bit format, which every API has. That
// the PSX layer happens to use it to hold packed 16-bit pixels is the PSX
// layer's business and stays in its comments, not in this name.

typedef enum
{
	NATIVE_GFX_TEXFMT_RGBA8 = 0,
	NATIVE_GFX_TEXFMT_RG8,

	// One 8-bit channel. Carries palette indices rather than brightness, so it
	// is only ever sampled with nearest filtering - an interpolated index is a
	// different colour, not a blend of two.
	NATIVE_GFX_TEXFMT_R8,

	// Readback only, for host-side images that want the channel order the
	// window system already uses.
	NATIVE_GFX_TEXFMT_BGRA8,
} NativeGfxTextureFormat;

typedef enum
{
	NATIVE_GFX_WRAP_REPEAT = 0,
	NATIVE_GFX_WRAP_CLAMP,
} NativeGfxWrap;

typedef struct
{
	int width;
	int height;
	NativeGfxTextureFormat format;
	NativeGfxFilter filter;
	NativeGfxWrap wrap;

	// NULL allocates storage without initialising it.
	const void *pixels;
} NativeGfxTextureDesc;

TextureID NativeGfx_CreateTexture(const NativeGfxTextureDesc *desc);
void NativeGfx_DestroyTexture(TextureID texture);
void NativeGfx_BindTexture(int slot, TextureID texture, NativeGfxFilter filter);

// Upload a sub-rectangle. rowPixels is the width of the row the source is cut
// from, for uploading a window of a larger image without copying it out first;
// 0 means the source is exactly this rectangle. Row alignment follows from the
// format, so callers do not carry it.
void NativeGfx_UpdateTexture(TextureID texture, int x, int y, int width, int height, NativeGfxTextureFormat format, const void *pixels,
                             int rowPixels);

// --- Vertex buffers --------------------------------------------------------
//
// The layout is declared once at creation and owned by the handle. Describing it
// instead of issuing it is what let two backends put it where each needed it -
// vertex-array state on one, baked into a pipeline on the other - and it is why
// a handle is opaque: what stands behind it is no caller's business.

typedef enum
{
	NATIVE_GFX_ATTR_FLOAT32 = 0,
	NATIVE_GFX_ATTR_SINT16,
	NATIVE_GFX_ATTR_UINT8,

	// 8-bit integer rescaled to 0..1 when sampled by the shader.
	NATIVE_GFX_ATTR_UNORM8,
	NATIVE_GFX_ATTR_SINT8,
} NativeGfxAttribType;

typedef struct
{
	int slot;
	int components;
	NativeGfxAttribType type;
	int offset;
} NativeGfxVertexAttrib;

#define NATIVE_GFX_MAX_ATTRIBS 8

typedef struct
{
	int bytes;

	// Hint only: whether the contents are rewritten every frame.
	int dynamic;

	// NULL allocates without initialising.
	const void *initial;

	int stride;
	int attribCount;
	NativeGfxVertexAttrib attribs[NATIVE_GFX_MAX_ATTRIBS];
} NativeGfxVertexBufferDesc;

NativeGfxBuffer NativeGfx_CreateVertexBuffer(const NativeGfxVertexBufferDesc *desc);
void NativeGfx_DestroyVertexBuffer(NativeGfxBuffer buffer);
void NativeGfx_BindVertexBuffer(NativeGfxBuffer buffer);
void NativeGfx_UpdateVertexBuffer(NativeGfxBuffer buffer, int offset, int bytes, const void *source);

// --- Render targets --------------------------------------------------------
//
// An offscreen surface to draw into, plus the texture that holds the result.
// The stencil attachment is requested rather than described: whether that is a
// renderbuffer, a combined depth-stencil, or an image in a subpass is the
// backend's problem.

typedef u32 NativeGfxTarget;

// Draw to the window rather than to an offscreen target.
#define NATIVE_GFX_TARGET_DEFAULT NATIVE_GFX_INVALID

typedef struct
{
	int width;
	int height;

	// Draw into a texture that already exists instead of one the target
	// creates. A borrowed texture is not destroyed with the target and cannot
	// be resized through it - the target does not own it.
	//
	// Zero means "make your own", which is the usual case, so a desc that never
	// mentions this field does the ordinary thing. Deliberately not
	// NATIVE_GFX_INVALID: that is (u32)-1, and a designated initializer leaves
	// an unnamed field at zero, which would silently mean "borrow texture 0".
	// No backend hands out zero as a texture, so it is free to mean none.
	//
	// This exists for VRAM: the emulated framebuffer is a texture in its own
	// right that other code samples from, and sometimes has to be drawn into.
	// Vulkan builds every framebuffer this way round anyway - from an image
	// view you already hold - so borrowing is the shape, not the exception.
	TextureID texture;
} NativeGfxTargetDesc;

NativeGfxTarget NativeGfx_CreateTarget(const NativeGfxTargetDesc *desc);
void NativeGfx_ResizeTarget(NativeGfxTarget target, int width, int height);
void NativeGfx_DestroyTarget(NativeGfxTarget target);
void NativeGfx_BindTarget(NativeGfxTarget target);
TextureID NativeGfx_TargetTexture(NativeGfxTarget target);
int NativeGfx_TargetWidth(NativeGfxTarget target);
int NativeGfx_TargetHeight(NativeGfxTarget target);

// ANTI-ALIASING (2026-09-22). A target is multisampled: every
// pass then draws into a multisampled image, and the texture of the target
// (TargetTexture) only comes into being as its resolve at the end of the pass. The readers
// of the texture - pack, present, readPixels - notice nothing of it.
//
// SetTargetSamples: 1 = off, otherwise 2 or 4; if the device cannot do the count,
// the next lower one applies. Switching destroys what depends on the old count,
// and waits for the device to do so - so only call it at a frame boundary,
// not between two draws. A borrowed target is never
// multisampled. TargetSamples says what currently applies.
void NativeGfx_SetTargetSamples(NativeGfxTarget target, int samples);
int NativeGfx_TargetSamples(NativeGfxTarget target);

// Sample shading for the following draws: in a pass with more than one
// sample, programs with a sample version (psx, psx32) then draw with it
// and with minSampleShading 1.0. Otherwise without effect. State like the blend mode.
void NativeGfx_SetSampleShading(int enable);

// --- Programs --------------------------------------------------------------
//
// A program is named, not written out. Source used to arrive here as a list of
// fragments for the backend to concatenate and compile; the backend that did
// that is gone, and the one that is left cannot compile text at runtime - it
// finds a prebuilt module by name. The name is debugName below.
//
// Attribute and sampler names are data passed through to the shader, not
// concepts in this interface. A backend never has to know what any of them
// mean, which is exactly why no PSX vocabulary needs to appear here.

typedef struct
{
	int slot;
	const char *name;
} NativeGfxNamedSlot;

#define NATIVE_GFX_MAX_PROGRAM_SLOTS 8

typedef enum
{
	NATIVE_GFX_UNIFORM_INT = 0,
	NATIVE_GFX_UNIFORM_FLOAT,
	NATIVE_GFX_UNIFORM_VEC2,
	NATIVE_GFX_UNIFORM_VEC4,
	NATIVE_GFX_UNIFORM_MAT4,
} NativeGfxUniformType;

typedef struct
{
	const char *name;
	NativeGfxUniformType type;

	// Byte offset of this field inside the block passed to
	// NativeGfx_UpdateUniforms.
	int offset;
} NativeGfxUniformField;

typedef struct
{

	// Vertex attribute slots, bound before linking.
	NativeGfxNamedSlot attribs[NATIVE_GFX_MAX_PROGRAM_SLOTS];
	int attribCount;

	// Texture slots each sampler reads from, assigned once after linking.
	NativeGfxNamedSlot samplers[NATIVE_GFX_MAX_PROGRAM_SLOTS];
	int samplerCount;

	// Layout of the uniform block this program is fed. Declaring it once means
	// updates can hand over plain bytes: GL unpacks them into individual
	// uniform writes using these offsets, Vulkan copies the block wholesale
	// into a buffer. Neither needs to know what any field means, which is how
	// PSX state crosses the seam without PSX vocabulary crossing with it.
	NativeGfxUniformField uniforms[NATIVE_GFX_MAX_PROGRAM_SLOTS];
	int uniformCount;

	// The converted form, and the one everything ends up on: a named uniform
	// block backed by a real buffer. NativeGfx_UpdateUniforms then hands over
	// the block's bytes and the device writes them straight into that buffer -
	// which is what Vulkan does, rather than something translated for it.
	//
	// The C struct passed to UpdateUniforms must match std140 layout. For
	// anything past a couple of scalars that means explicit padding; get it
	// wrong and fields land on each other silently.
	//
	// uniformCount above is the older per-field form, kept only until the
	// remaining shaders are converted. A program uses one or the other.
	const char *uniformBlockName;
	int uniformBlockBytes;
	int uniformBlockBinding;

	const char *debugName;
} NativeGfxProgramDesc;

ShaderID NativeGfx_CreateProgram(const NativeGfxProgramDesc *desc);
void NativeGfx_DestroyProgram(ShaderID program);
void NativeGfx_BindProgram(ShaderID program);

// Hands the program its uniform block. The program must be bound. Fields the
// shader optimised away are skipped silently, which is why the block can stay
// one shape across shader variants that use different subsets of it.
void NativeGfx_UpdateUniforms(ShaderID program, const void *block);

// --- State -----------------------------------------------------------------

void NativeGfx_SetViewport(int x, int y, int width, int height);
void NativeGfx_SetScissorEnabled(int enable);
void NativeGfx_SetScissorRect(int x, int y, int width, int height);
// Returns what APPLIES afterwards, not what was wished for. A device without
// fillModeNonSolid refuses the wireframe mode - see the note at
// NativeGfxVK_SetWireframe -, and the switch above has to learn that,
// otherwise the same fact stands in two places and one falls behind.
int NativeGfx_SetWireframe(int enable);
void NativeGfx_SetBlendMode(BlendMode blend);

// No depth test and no stencil in this interface. Both stood
// here, both never worked - see the note at NativeRenderer_SetBlendMode in
// platform/native_renderer.c, which says what it was, why it did nothing and where
// the one attempt lies.

// --- Drawing ---------------------------------------------------------------

void NativeGfx_ClearColor(float r, float g, float b, float a);
void NativeGfx_ClearColorBuffer(void);
void NativeGfx_Draw(int firstVertex, int vertexCount);
// Reads from whatever target is currently bound. rowPixels works as it does
// for NativeGfx_UpdateTexture.
void NativeGfx_ReadPixels(int x, int y, int width, int height, NativeGfxTextureFormat format, void *dst, int rowPixels);

// --- Diagnostics -----------------------------------------------------------
//
// The GL debug channel has paid for itself several times over, so it stays
// exactly as it is - it just becomes a backend responsibility, installed by
// whichever backend is active.

void NativeGfx_PushDebugLabel(const char *label);
void NativeGfx_PopDebugLabel(void);

// Counts what a frame asked the device for, above the backend and therefore
// identically for all of them. Called once per frame from the swap, so the same
// numbers can be read off a GL run and a Vulkan run and compared.
void NativeGfx_ReportFrame(void);

// Draws the previous frame asked for. Survives the per-frame reset so an
// overlay can put it next to a frame time.
unsigned int NativeGfx_LastFrameDraws(void);

// Frame time, sampled once per frame by NativeGfx_ReportFrame and reported into
// the log as well as read by the overlay. One source, so a log and a screen
// never disagree - and so a run never has to be reported by photographing it.
double NativeGfx_FrameAverageMs(void);
double NativeGfx_FrameWorstMs(void);

// GPU timing. Whether it can be measured at all is a device property, so the
// backend answers it rather than the caller sniffing extensions. One timer is
// in flight at a time, which is why ending one takes no handle. BeginTimer says
// whether it really started one: in a frame that is not being recorded it
// cannot, and a caller that waited for it anyway would read the stamps of the
// timer's previous round as new.
typedef u32 NativeGfxTimer;

int NativeGfx_TimersSupported(void);
NativeGfxTimer NativeGfx_CreateTimer(void);
void NativeGfx_DestroyTimer(NativeGfxTimer timer);
int NativeGfx_BeginTimer(NativeGfxTimer timer);
void NativeGfx_EndTimer(void);
int NativeGfx_TimerReady(NativeGfxTimer timer);
u32 NativeGfx_TimerElapsedNanoseconds(NativeGfxTimer timer);

#endif

#if defined(CTR_INTERNAL)
// Measuring switch, off unless asked for: puts scissored clears back on the
// old path so both behaviours can be compared from one binary.
void NativeGfxVK_SetWholeAttachmentClears(int enable);
void NativeGfxVK_ReportClearCounts(void);
#endif
