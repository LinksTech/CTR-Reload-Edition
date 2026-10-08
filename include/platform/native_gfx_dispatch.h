#ifndef PLATFORM_NATIVE_GFX_DISPATCH_H
#define PLATFORM_NATIVE_GFX_DISPATCH_H

// How a backend is plugged in.
//
// One table of function pointers per backend and a forwarder per interface
// function in native_gfx.c. Every call above the device goes through a
// forwarder, so nothing outside a backend ever names a backend.
//
// WHAT THE TABLE ACTUALLY GIVES, AND WHAT IT USED TO CLAIM.
//
// It used to say here that a table has to be filled in entry for entry, so a
// function the backend forgot to implement is a hole the initialiser shows,
// where direct calls would simply have compiled. That was the wrong way round,
// both halves of it:
//
//   - the table is filled with DESIGNATED initialisers (.createTexture = ...),
//     and C zero-fills anything not named. A forgotten entry is a silent null
//     that crashes on the first call that needs it, at some point during a race.
//   - a direct call to a function that does not exist is a LINK error. Earlier,
//     and louder, than anything a table can do by itself.
//
// So the table made that failure later and quieter, not earlier. What it really
// gives is the seam: one place where a second backend goes back in, with the
// interface above untouched either way, and one place where a call can be
// counted or traced for every backend at once.
//
// The claim is now made true rather than deleted. NativeGfx_CheckDispatch walks
// the table at start-up and names every null entry before a single frame is
// drawn - see native_gfx.c. That check is part of what the table is for; without
// it the seam costs an indirection and buys nothing but the seam.
//
// ONE LIST, USED THREE TIMES.
//
// The entries are written once, below, and expanded into the struct fields and
// into the check. Two lists - a struct here and a list of names in the checker -
// would be the same fact in two places, which is a shape this tree has been
// caught by more than once. Add an entry here and the struct grows, the check covers
// it, and neither can be forgotten.
//
// X takes the return type, the field name, and the parameter list in brackets.

#include <platform/native_gfx.h>

#define NATIVE_GFX_DISPATCH_ENTRIES(X)                                                                                                             \
	X(TextureID, createTexture, (const NativeGfxTextureDesc *desc))                                                                                \
	X(void, updateTexture,                                                                                                                         \
	  (TextureID texture, int x, int y, int width, int height, NativeGfxTextureFormat format, const void *pixels, int rowPixels))                   \
	X(void, destroyTexture, (TextureID texture))                                                                                                   \
	X(void, bindTexture, (int slot, TextureID texture, NativeGfxFilter filter))                                                                     \
	X(NativeGfxFilter, textureFilter, (TextureID texture))                                                                                         \
	X(TextureID, createTextureLevels, (const NativeGfxTextureLevelsDesc *desc))                                                                    \
	X(void, setTextureSampling, (TextureID texture, const NativeGfxSampling *sampling))                                                           \
	X(void, textureLimits, (NativeGfxTextureLimits *out))                                                                                          \
	X(u32, shrinkStaging, (u32 keepBytes))                                                                                                         \
	X(u32, stagingBytes, (void))                                                                                                                   \
                                                                                                                                                   \
	X(NativeGfxBuffer, createVertexBuffer, (const NativeGfxVertexBufferDesc *desc))                                                                 \
	X(void, destroyVertexBuffer, (NativeGfxBuffer buffer))                                                                                          \
	X(void, bindVertexBuffer, (NativeGfxBuffer buffer))                                                                                             \
	X(void, updateVertexBuffer, (NativeGfxBuffer buffer, int offset, int bytes, const void *source))                                                \
	X(NativeGfxBuffer, createIndexBuffer, (const NativeGfxIndexBufferDesc *desc))                                                                   \
	X(void, destroyIndexBuffer, (NativeGfxBuffer buffer))                                                                                           \
	X(void, bindIndexBuffer, (NativeGfxBuffer buffer))                                                                                              \
                                                                                                                                                   \
	X(NativeGfxTarget, createTarget, (const NativeGfxTargetDesc *desc))                                                                             \
	X(void, resizeTarget, (NativeGfxTarget target, int width, int height))                                                                          \
	X(void, destroyTarget, (NativeGfxTarget target))                                                                                                \
	X(void, bindTarget, (NativeGfxTarget target))                                                                                                   \
	X(TextureID, targetTexture, (NativeGfxTarget target))                                                                                           \
	X(int, targetWidth, (NativeGfxTarget target))                                                                                                   \
	X(int, targetHeight, (NativeGfxTarget target))                                                                                                  \
	X(void, setTargetSamples, (NativeGfxTarget target, int samples))                                                                                \
	X(int, targetSamples, (NativeGfxTarget target))                                                                                                 \
	X(void, setTargetDepth, (NativeGfxTarget target, int enable))                                                                                   \
	X(int, targetDepth, (NativeGfxTarget target))                                                                                                   \
	X(void, setSampleShading, (int enable))                                                                                                         \
                                                                                                                                                   \
	X(ShaderID, createProgram, (const NativeGfxProgramDesc *desc))                                                                                  \
	X(void, destroyProgram, (ShaderID program))                                                                                                     \
	X(void, bindProgram, (ShaderID program))                                                                                                        \
	X(void, updateUniforms, (ShaderID program, const void *block))                                                                                  \
                                                                                                                                                   \
	X(void, setViewport, (int x, int y, int width, int height))                                                                                     \
	X(void, setScissorEnabled, (int enable))                                                                                                        \
	X(void, setScissorRect, (int x, int y, int width, int height))                                                                                  \
	X(int, setWireframe, (int enable))                                                                                                              \
                                                                                                                                                   \
	X(void, setBlendMode, (BlendMode blend))                                                                                                        \
	X(void, setDrawState, (const NativeGfxDrawState *state))                                                                                        \
	X(void, clearColor, (float r, float g, float b, float a))                                                                                       \
	X(void, clearColorBuffer, (void))                                                                                                               \
	X(void, clearDepth, (int x, int y, int width, int height))                                                                                      \
	X(void, draw, (int firstVertex, int vertexCount))                                                                                               \
	X(void, drawIndexed, (int firstIndex, int indexCount, int vertexOffset))                                                                        \
	X(void, readPixels, (int x, int y, int width, int height, NativeGfxTextureFormat format, void *dst, int rowPixels))                             \
                                                                                                                                                   \
	X(int, timersSupported, (void))                                                                                                                 \
	X(NativeGfxTimer, createTimer, (void))                                                                                                          \
	X(void, destroyTimer, (NativeGfxTimer timer))                                                                                                   \
	X(int, beginTimer, (NativeGfxTimer timer))                                                                                                      \
	X(void, endTimer, (void))                                                                                                                       \
	X(int, timerReady, (NativeGfxTimer timer))                                                                                                      \
	X(u32, timerElapsed, (NativeGfxTimer timer))                                                                                                    \
                                                                                                                                                   \
	X(void, pushDebugLabel, (const char *label))                                                                                                    \
	X(void, popDebugLabel, (void))

struct NativeGfxDispatch
{
	// Not an entry, because it is not a function and the check has nothing to
	// say about it. A backend without a name would be found by reading a log.
	const char *name;

#define NATIVE_GFX_DISPATCH_FIELD(ret, fieldName, params) ret (*fieldName) params;
	NATIVE_GFX_DISPATCH_ENTRIES(NATIVE_GFX_DISPATCH_FIELD)
#undef NATIVE_GFX_DISPATCH_FIELD
};

// Walks the active table and names every entry the backend did not fill in.
// Returns the number of holes, zero being the only acceptable answer. Called
// once, before the first frame - see NativeRenderer_InitialiseDevice.
int NativeGfx_CheckDispatch(void);

#endif
