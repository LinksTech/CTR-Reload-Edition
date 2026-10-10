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

	// RGBA8 whose colour channels are sRGB encoded: a sample hands the shader
	// LINEAR light, and filtering happens in linear light. Appended after the
	// others so that no existing value moves. Only the native texture manager
	// (platform/native_tex.c) asks for it; a shader that samples it converts
	// back to gamma itself (the "nr" program does when its block says so).
	NATIVE_GFX_TEXFMT_RGBA8_SRGB,
} NativeGfxTextureFormat;

typedef enum
{
	NATIVE_GFX_WRAP_REPEAT = 0,
	NATIVE_GFX_WRAP_CLAMP,

	// Appended. Only the sampling of a texture with levels
	// (NativeGfx_CreateTextureLevels) understands it; the four shared samplers
	// of the other textures know REPEAT and CLAMP alone, as before.
	NATIVE_GFX_WRAP_MIRROR,
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

	// 1 = a texture of the native layer. It is sampled through a sampler of its
	// own (nearest, clamped to the edge, one level), never through one of the
	// samplers the other textures share, and its pixels go up through a
	// transfer the device waits for, never into an open frame. Zero, the value
	// of every desc that does not name the field, is every texture as before.
	int nativeLayer;
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

// --- Textures with levels --------------------------------------------------
//
// A second road beside the one above, for the native texture manager
// (platform/native_tex.c) only. Everything above stays a texture of one level,
// and every VRAM upload keeps going through it unchanged; nothing here is used
// by a texture made there.
//
// A texture made here: every level is given at creation, as finished bytes -
// the device computes no level (no blit, no driver mip generation), so the
// picture is the same on every machine. Level n is max(1, width >> n) by
// max(1, height >> n) texels, tightly packed. The pixels go up through a
// transfer the device waits for, never into the ring of an open frame: if a
// frame is open with work recorded, that work is submitted and waited for
// first (the pass is stored and the next draw loads it again), so creating one
// at a loading screen is legal and changes no pixel already drawn. Sampled
// through a sampler of its own sampling, never one the other textures share.
// The limits (edge length, maxImageDimension2D) are the manager's business and
// are not checked here.

#define NATIVE_GFX_MAX_TEXTURE_LEVELS 12 // 2048 down to 1

typedef enum
{
	NATIVE_GFX_MIP_NONE = 0, // level 0 only (maxLod 0)
	NATIVE_GFX_MIP_NEAREST,
	NATIVE_GFX_MIP_LINEAR,
} NativeGfxMipMode;

// Zero is nearest, nearest, level 0 only, no anisotropy, repeat.
typedef struct
{
	NativeGfxFilter magFilter; // NEAREST or LINEAR; KEEP counts as NEAREST
	NativeGfxFilter minFilter;
	NativeGfxMipMode mipMode;

	// 0 or 1 = off. More is a wish: the device grants it only when anisotropy
	// was requested at device creation (--native-preview at start) and clamps it
	// to its own limit (NativeGfxTextureLimits).
	int anisotropy;

	NativeGfxWrap wrapU;
	NativeGfxWrap wrapV;
} NativeGfxSampling;

typedef struct
{
	int width; // of level 0
	int height;

	// NATIVE_GFX_TEXFMT_RGBA8 or NATIVE_GFX_TEXFMT_RGBA8_SRGB.
	NativeGfxTextureFormat format;

	// 1 .. NATIVE_GFX_MAX_TEXTURE_LEVELS, at most the full chain down to 1x1.
	int levelCount;
	const void *levels[NATIVE_GFX_MAX_TEXTURE_LEVELS];

	NativeGfxSampling sampling;
} NativeGfxTextureLevelsDesc;

// NATIVE_GFX_INVALID when the desc is not usable or the device refuses.
TextureID NativeGfx_CreateTextureLevels(const NativeGfxTextureLevelsDesc *desc);

// Changes how a texture made by NativeGfx_CreateTextureLevels is sampled (the
// filter option). Ignored for any other texture. Applies from the next draw.
void NativeGfx_SetTextureSampling(TextureID texture, const NativeGfxSampling *sampling);

typedef struct
{
	int maxImageDimension2D;
	int anisotropyEnabled;      // 1 = requested at device creation and granted
	float maxSamplerAnisotropy; // the device limit, 0 when anisotropy is off
} NativeGfxTextureLimits;

void NativeGfx_TextureLimits(NativeGfxTextureLimits *out);

// THE STAGING BUFFER BACK. Every one-shot transfer goes through one shared,
// mapped staging buffer that only ever grows (a 2048x2048 texture with its
// levels leaves 32 MB behind). When it is larger than keepBytes it is made
// again at keepBytes (0: freed outright) and the bytes given back are
// returned. The native texture manager passes NativeGfx_StagingBytes() from
// before its upload, so only the growth of that upload goes back and a buffer
// grown for VRAM fallbacks stays standing. Safe between any two calls: every
// user of the buffer waits for its transfer.
u32 NativeGfx_ShrinkStaging(u32 keepBytes);

// The staging buffer's size right now, 0 when there is none.
u32 NativeGfx_StagingBytes(void);

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

// --- Index buffers ---------------------------------------------------------
//
// For indexed draws (NativeGfx_DrawIndexed). The index type is declared once at
// creation and owned by the handle, as the layout is for a vertex buffer.

typedef enum
{
	NATIVE_GFX_INDEX_U16 = 0,
	NATIVE_GFX_INDEX_U32,
} NativeGfxIndexType;

typedef struct
{
	int bytes;
	NativeGfxIndexType type;

	// Required. An index buffer is static: device-local, filled once here.
	const void *initial;
} NativeGfxIndexBufferDesc;

// Handles of their own: an index buffer handle is never a vertex buffer handle.
// Bind only remembers the buffer; the next indexed draw uses it.
NativeGfxBuffer NativeGfx_CreateIndexBuffer(const NativeGfxIndexBufferDesc *desc);
void NativeGfx_DestroyIndexBuffer(NativeGfxBuffer buffer);
void NativeGfx_BindIndexBuffer(NativeGfxBuffer buffer);

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

// ANTI-ALIASING. A target is multisampled: every
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

// DEPTH ON A TARGET. SetTargetDepth(target, 1) gives the target a depth image
// of the same size and the same sample count, and the image follows every
// later size and sample change; 0 takes it away again. Between two draws of a
// frame as well: a pass open on the target ends there (its colour stored, as at
// any target switch) and the next draw opens it again with LOAD and the new set
// of attachments; the image is made and cleared by a transfer of its own. A
// borrowed target never gets one. Without --native-preview this does nothing at all, so a stray call
// cannot put depth into a run that did not ask for it. TargetDepth is 1 when the
// target has a depth image right now - a device without a usable depth format
// leaves the wish standing and the target without one.
void NativeGfx_SetTargetDepth(NativeGfxTarget target, int enable);
int NativeGfx_TargetDepth(NativeGfxTarget target);

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
	// updates can hand over plain bytes and the backend places them using these
	// offsets. It never needs to know what any field means, which is how
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

// No stencil in this interface, and no depth for the PSX draws. An older depth
// test and stencil mode stood here and never worked - see the note at
// NativeRenderer_SetBlendMode in platform/native_renderer.c, which says what
// they were, why they did nothing and where the one attempt lies. The depth
// below is a different thing: it exists for native draws only, on a target that
// asked for it (NativeGfx_SetTargetDepth), and the PSX draws keep test and write
// off.
//
// DEPTH. The convention every user of depth here follows:
//
//   format       D32_SFLOAT; X8_D24_UNORM_PACK32 where D32 is not usable at the
//                target's sample count; with neither, the target draws without
//                depth and says so in the log. --native-depth-d24 turns the
//                order round, to measure the two. The colour sample count is
//                never lowered for the sake of depth. Reverse Z needs no float
//                format: depth = zNear / zView lies in 0..1, which UNORM holds,
//                only with even steps of 2^-24 instead of the finer steps a
//                float has near 0 (far away).
//   direction    reverse Z. Cleared to 0.0 = infinitely far, nearer = larger,
//                compared with NATIVE_GFX_COMPARE_GREATER_OR_EQUAL.
//   projection   far plane at infinity: clip.z = zNear and clip.w = zView, so
//                depth = zNear / zView: 1.0 on the near plane, towards 0 in the
//                distance. The viewport keeps minDepth 0 and maxDepth 1.
//   near plane   zNear = H / 8 in view units, H being the view's distance to
//                its screen plane. A choice, not a measurement: whether the original
//                ever draws nearer than H / 8 is not known yet.
//   mvpShift     view xyz is divided by 2^mvpShift before the mapping - near
//                instances are queued scaled up by 4, huge ones down by 4.
//   colour mask  native draws write RGB only (colorWriteOff =
//                NATIVE_GFX_COLOR_A): the alpha of the main target is the mask
//                bit and becomes bit 15 when the target is packed.
//
// The mapping is the caller's arithmetic in C; a native program takes one
// finished matrix and does no depth arithmetic of its own.

typedef enum
{
	NATIVE_GFX_COMPARE_ALWAYS = 0,
	NATIVE_GFX_COMPARE_GREATER_OR_EQUAL,
	NATIVE_GFX_COMPARE_GREATER,
	NATIVE_GFX_COMPARE_LESS_OR_EQUAL,
	NATIVE_GFX_COMPARE_LESS,
	NATIVE_GFX_COMPARE_EQUAL,
} NativeGfxCompare;

// Front = counter-clockwise in normalised device coordinates with y up - the
// GL convention this whole interface speaks, whichever way up a backend stores
// its targets.
typedef enum
{
	NATIVE_GFX_CULL_NONE = 0,
	NATIVE_GFX_CULL_BACK,
	NATIVE_GFX_CULL_FRONT,
} NativeGfxCull;

#define NATIVE_GFX_COLOR_R 1u
#define NATIVE_GFX_COLOR_G 2u
#define NATIVE_GFX_COLOR_B 4u
#define NATIVE_GFX_COLOR_A 8u

typedef struct
{
	int depthTest;                // 0 = off
	int depthWrite;               // only with depthTest
	NativeGfxCompare depthCompare;
	NativeGfxCull cull;
	u32 colorWriteOff;            // channels NOT written; 0 = all four
} NativeGfxDrawState;

// NULL = all zero = the state every PSX draw has always had.
//
// State like the blend mode: it applies to every following draw, PSX and blit
// draws included, until it is set again. Whoever sets it sets it back with
// NativeGfx_SetDrawState(NULL). depthTest only has an effect in a pass whose
// target has a depth image.
void NativeGfx_SetDrawState(const NativeGfxDrawState *state);

// --- Drawing ---------------------------------------------------------------

void NativeGfx_ClearColor(float r, float g, float b, float a);
void NativeGfx_ClearColorBuffer(void);
void NativeGfx_Draw(int firstVertex, int vertexCount);

// Clears a rectangle of the bound target's depth image to far (0.0). The
// rectangle follows the convention of NativeGfx_SetScissorRect (GL, rows
// counted from the bottom) and is clipped to the pass. Without a depth image on
// the bound target, or with width or height <= 0, nothing happens.
void NativeGfx_ClearDepth(int x, int y, int width, int height);

// Like NativeGfx_Draw, through the bound index buffer; vertexOffset is added to
// every index.
void NativeGfx_DrawIndexed(int firstIndex, int indexCount, int vertexOffset);
// Reads from whatever target is currently bound. rowPixels works as it does
// for NativeGfx_UpdateTexture.
void NativeGfx_ReadPixels(int x, int y, int width, int height, NativeGfxTextureFormat format, void *dst, int rowPixels);

// --- Diagnostics -----------------------------------------------------------
//
// Debug labels are a backend responsibility, installed by whichever backend
// is active.

void NativeGfx_PushDebugLabel(const char *label);
void NativeGfx_PopDebugLabel(void);

// Counts what a frame asked the device for, above the backend and therefore
// identically for any of them. Called once per frame from the swap.
// It also samples the frame time (average and worst frame of the last second)
// and writes it into the log with the counts.
void NativeGfx_ReportFrame(void);

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

// --- Backend-specific ------------------------------------------------------
//
// Vulkan only and named so on purpose, like the measuring switches below: what
// it does is a property of this backend's pipeline cache, not of the
// interface. PIPELINES AHEAD OF THE RACE. For the target that has a depth
// image right now (SetTargetDepth), builds the pipelines its draws would
// otherwise build at their first use, in the first race frame: every pipeline
// the target drew with before it had depth, the PSX programs over every blend
// mode of the scene, and the native layer's own draws (program "nr") with the
// vertex layout of nativeVertexBuffer and the DEPTH convention above. Records
// nothing and opens no pass; a pipeline already known costs a lookup. Without
// --native-preview, or without such a target, nothing happens at all. Returns
// the number built; one log line says so.
int NativeGfxVK_WarmPipelines(NativeGfxBuffer nativeVertexBuffer);

#endif

#if defined(CTR_INTERNAL)
// Measuring switch, off unless asked for: puts scissored clears back on the
// old path so both behaviours can be compared from one binary.
void NativeGfxVK_SetWholeAttachmentClears(int enable);
void NativeGfxVK_ReportClearCounts(void);
#endif
