#include <platform/engine.h>

// ENGINE PROGRAMS - see include/platform/engine.h, section PROGRAMS.
//
// The engine's own GPU program, "ne". It is the engine twin of the native layer
// program "nr" (native_shaders.inc, ctr_native_layer_shader) and of the retail
// twin "nrt": the same VERTEX / FRAGMENT sections in one source, the same
// macros (VARYING_LOC0, UBO_QUALIFIER, SAMPLER_SLOTn, fragColor), the same
// vertex layout (location 0 position, 1 texcoord, 2 colour - struct
// EngineVertex has the layout of struct NativeProbeVertex). What "ne" adds is
// one fragment path that reads the PSX VRAM texture directly, so that a mesh of
// the engine can wear the retail textures of the game without a copy of them.
//
// THE BLOCK. "NeBlock" is struct EngineUniforms (include/platform/engine.h)
// field for field, 192 bytes std140: a mat4 at 0, then eight vec4 at 64, 80,
// 96, 112, 128, 144, 160, 176. Both stages of every engine program declare it
// with the same text (ENGINE_UNIFORM_BLOCK_GLSL in native_shaders.inc). Only
// vec4 after the matrix means std140 and the C struct agree without padding
// rules to get wrong; the static assert below holds the size.
//
// THE PROGRAMS. "ne" (ENGINE_PROGRAM_NE) and the water program "nw"
// (ENGINE_PROGRAM_NW, platform/engine_water_shader.inc) share the vertex
// layout, the samplers and the block; a material names its program
// (EngineMaterial.program) and the renderer binds it per range.
//
// THE SAMPLERS. Three slots, all constant for the lifetime of the program:
//   slot 0  "s_texture"  the native texture of texSource 1 (RGBA8, sRGB or not,
//                        exactly as "nr" reads it)
//   slot 1  "s_vram"     the PSX VRAM texture (RG8, 1024 x 512, the texture
//                        NativeRenderer_GetVRAMTexture returns), read with
//                        texelFetch - nearest only, never filtered
//   slot 3  "s_table"    the track mosaic table of texSource 3 (RGBA8, nearest,
//                        platform/engine_track_mosaic.c), read with texelFetch
// Slot 2 is the PSX page atlas: the layout has a binding for it (the bindings
// run from 0 to the highest slot) but "ne" never declares or moves it.
// The PSX program has the VRAM in slot 0 and its colour lookup table in slot 1;
// "ne" has no lookup table (see FIVE-FIVE-FIVE below), so a draw binds the two
// textures it needs and nothing else.
//
// WHERE THE PROGRAM IS CREATED. Not through
// NativeRenderer_Shader_CompileWithUniforms: that helper only identifies the
// SPIR-V module of a body by comparing the pointer against the bodies in
// native_shaders.inc, and its sampler list names the PSX layout (VRAM in slot
// 0, lookup table in slot 1). "ne" is not in native_shaders.inc, so the helper
// would name a wrong module. Instead NativeGfx_CreateProgram is called with the
// same descriptor the helper builds, with the sampler list of "ne" and
// debugName "ne". Under Vulkan that name is what finds the prebuilt modules
// ne.vert / ne.frag (NativeGfxVK_CreateProgram).
//
// THE SPIR-V. Vulkan cannot compile GLSL at runtime: the source of "ne" is
// ctr_native_engine_shader in native_shaders.inc, listed in CtrBlitShader_Get as "ne",
// so tools/shader_spirv_probe.c emits ne.vert / ne.frag, the build compiles them
// into native_shaders_spv.h and the ctest shader_spirv_dialect checks them.

// The block must be exactly what the GLSL declares: mat4 (64) + 8 x vec4 (128), ENGINE_UNIFORM_BLOCK_GLSL.
_Static_assert(sizeof(EngineUniforms) == 192, "EngineUniforms must be 192 bytes (std140 NeBlock)");

// The GLSL is ctr_native_engine_shader in native_shaders.inc (with its explanation),
// so the SPIR-V probe and the dialect test see it.

global_variable int s_engineProgramsTried = 0;
global_variable ShaderID s_engineProgram = NATIVE_GFX_INVALID;
global_variable ShaderID s_engineWaterProgram = NATIVE_GFX_INVALID;

int Engine_ProgramsInit(void)
{
	// Only with --engine; without it nothing is created and nothing is logged.
	if (!g_cfg_engine)
	{
		return 0;
	}

	// Once. A second call answers from the first (also when it failed: a
	// missing program does not become ready by asking again).
	if (s_engineProgramsTried)
	{
		return (s_engineProgram != NATIVE_GFX_INVALID) ? 1 : 0;
	}
	s_engineProgramsTried = 1;

	// --engine-fail start: neither program is made, as when the device refuses
	// them; Engine_RenderInit then falls back (FALLBACK, engine.h).
	if (g_cfg_engineFail == ENGINE_FAIL_START)
	{
		Platform_Log("[CTR Engine] programs 'ne' and 'nw' not made: --engine-fail start\n");
		return 0;
	}

	// The descriptor of NativeRenderer_Shader_CompileWithUniforms, with the
	// samplers of "ne" (see the header comment for why not the helper).
	NativeGfxProgramDesc desc = {
	    .attribs = {
	        {.slot = a_position, .name = "a_position"},
	        {.slot = a_texcoord, .name = "a_texcoord"},
	        {.slot = a_color, .name = "a_color"},
	    },
	    .attribCount = 3,
	    .samplers = {
	        {.slot = 0, .name = "s_texture"},
	        {.slot = 1, .name = "s_vram"},
	        {.slot = 3, .name = "s_table"},
	    },
	    .samplerCount = 3,
	    .uniformBlockName = "NeBlock",
	    .uniformBlockBytes = (int)sizeof(EngineUniforms),
	    .uniformBlockBinding = NATIVE_UNIFORM_BLOCK_BINDING,
	    .debugName = "ne",
	};

	s_engineProgram = NativeGfx_CreateProgram(&desc);

	Platform_Log("[CTR Engine] program 'ne' %s\n", (s_engineProgram != NATIVE_GFX_INVALID) ? "ready" : "missing");

	// "nw": the same descriptor under its own name (its SPIR-V modules are nw.vert / nw.frag).
	desc.debugName = "nw";
	s_engineWaterProgram = NativeGfx_CreateProgram(&desc);

	Platform_Log("[CTR Engine] program 'nw' %s\n", (s_engineWaterProgram != NATIVE_GFX_INVALID) ? "ready" : "missing");

	return (s_engineProgram != NATIVE_GFX_INVALID) ? 1 : 0;
}

u32 Engine_Program(void)
{
	return (s_engineProgram != NATIVE_GFX_INVALID) ? (u32)s_engineProgram : 0xffffffffu;
}

u32 Engine_ProgramOf(int program)
{
	ShaderID id = NATIVE_GFX_INVALID;

	if (program == ENGINE_PROGRAM_NE)
	{
		id = s_engineProgram;
	}
	else if (program == ENGINE_PROGRAM_NW)
	{
		id = s_engineWaterProgram;
	}
	return (id != NATIVE_GFX_INVALID) ? (u32)id : 0xffffffffu;
}

// The reason Engine_RenderInit hands to Engine_Fallback when a program is missing.
const char *Engine_ProgramsFailReason(void)
{
	const int ne = (s_engineProgram == NATIVE_GFX_INVALID);
	const int nw = (s_engineWaterProgram == NATIVE_GFX_INVALID);

	if (ne && nw)
	{
		return (g_cfg_engineFail == ENGINE_FAIL_START) ? "the programs 'ne' and 'nw' were not made (forced by --engine-fail start)"
		                                               : "the programs 'ne' and 'nw' could not be created";
	}
	return ne ? "the program 'ne' could not be created" : (nw ? "the program 'nw' could not be created" : "no program missing");
}
