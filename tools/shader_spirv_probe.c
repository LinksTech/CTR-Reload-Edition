// Build-time assembly of the shader sources that become the SPIR-V modules.
//
// The renderer cannot compile GLSL at runtime; it links prebuilt SPIR-V and
// finds each module by name. This tool assembles every shader from the Vulkan
// preamble below plus its body and writes them out, and the build then runs
// glslangValidator over the lot - so a body that no longer compiles is caught
// by the build rather than by a black picture. The preamble lives only here.
//
// It writes files rather than validating itself, so a failure names the shader
// and leaves it on disk to look at.
//
// It includes the same platform/native_shaders.inc the renderer does, so the
// two cannot drift.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The shader file expects the annotations the renderer's build supplies.
#define global_variable static
#define internal static

#include "../platform/native_shaders.inc"

// The preamble. There used to be a GL counterpart in native_renderer.c; with
// the GL backend gone this is the only one.
//
// Bindings are what native_gfx_vk.c builds its descriptor set layouts for
// (NativeGfxVK_CreateProgram): the uniform block is 0 and SAMPLER_SLOTn is
// binding n + 1.
static const char *VK_HEADER_VERT =
    "#version 450\n"
    "precision lowp  int;\n"
    "precision highp float;\n"
    "#define varying   out\n"
    "#define attribute in\n"
    "#define UBO_QUALIFIER layout(std140, set = 0, binding = 0)\n"
    "#define SAMPLER_SLOT0 layout(set = 0, binding = 1)\n"
    "#define SAMPLER_SLOT1 layout(set = 0, binding = 2)\n"
    "#define SAMPLER_SLOT2 layout(set = 0, binding = 3)\n"
    "#define SAMPLER_SLOT3 layout(set = 0, binding = 4)\n"
    "#define VARYING_LOC0 layout(location = 0)\n"
    "#define VARYING_LOC1 layout(location = 1)\n"
    "#define VARYING_LOC2 layout(location = 2)\n"
    "#define VARYING_LOC3 layout(location = 3)\n"
    "#define VARYING_LOC4 layout(location = 4)\n"
    "#define VARYING_LOC5 layout(location = 5)\n";

static const char *VK_HEADER_FRAG =
    "#version 450\n"
    "precision lowp  int;\n"
    "precision highp float;\n"
    "#define varying     in\n"
    "#define UBO_QUALIFIER layout(std140, set = 0, binding = 0)\n"
    "#define SAMPLER_SLOT0 layout(set = 0, binding = 1)\n"
    "#define SAMPLER_SLOT1 layout(set = 0, binding = 2)\n"
    "#define SAMPLER_SLOT2 layout(set = 0, binding = 3)\n"
    "#define SAMPLER_SLOT3 layout(set = 0, binding = 4)\n"
    "#define VARYING_LOC0 layout(location = 0)\n"
    "#define VARYING_LOC1 layout(location = 1)\n"
    "#define VARYING_LOC2 layout(location = 2)\n"
    "#define VARYING_LOC3 layout(location = 3)\n"
    "#define VARYING_LOC4 layout(location = 4)\n"
    "#define VARYING_LOC5 layout(location = 5)\n"
    "layout(location = 0) out vec4 fragColor;\n";

static const char *s_outputDirectory = ".";

static int WriteParts(const char *name, const char *const *parts, int count)
{
	char path[1024];
	FILE *file;

	snprintf(path, sizeof(path), "%s/%s", s_outputDirectory, name);

	file = fopen(path, "wb");
	if (file == NULL)
	{
		fprintf(stderr, "shader_spirv_probe: cannot write %s\n", path);
		return 0;
	}

	for (int i = 0; i < count; i++)
	{
		fwrite(parts[i], 1, strlen(parts[i]), file);
	}

	fclose(file);
	printf("%s\n", name);

	return 1;
}

// A shader that is not a PSX one: preamble, stage define, body - one file per
// stage.
static int WriteBlit(const char *name, const char *body)
{
	char path[1024];

	const char *vs[] = {VK_HEADER_VERT, "#define VERTEX\n", body};
	const char *fs[] = {VK_HEADER_FRAG, "#define FRAGMENT\n", body};

	snprintf(path, sizeof(path), "%s.vert", name);
	if (!WriteParts(path, vs, 3))
	{
		return 0;
	}

	snprintf(path, sizeof(path), "%s.frag", name);

	return WriteParts(path, fs, 3);
}

int main(int argc, char **argv)
{
	if (argc > 1)
	{
		s_outputDirectory = argv[1];
	}

	// The PSX vertex stage. One vertex stage serves both fragment programs.
	//
	// Also for the centroid and sample fragment variants: when matching the stages
	// Vulkan demands equal decorations EXCEPT the
	// interpolation decorations, and the one of the fragment stage is decisive.
	{
		const char *vs[] = {VK_HEADER_VERT, "#define VERTEX\n", "#define PSX_EDGE_INTERP\n", gpu_shader_common, GTE_VERTEX_SHADER};

		if (!WriteParts("psx.vert", vs, 5))
		{
			return 1;
		}
	}

	// The PSX fragment programs. Bilinear filtering is a runtime uniform
	// (bilinearFilter), so one module per program covers both settings.
	{
		const struct
		{
			const char *name;
			const char *body;
		} variants[] = {
		    // One program for 4, 8 and 16 bit: the
		    // format rides in the vertex. The 32-bit override texture keeps
		    // its own program - it samples a native RGBA texture, not VRAM.
		    {"psx", gte_shader_psx},
		    {"psx32", gte_shader_32_rgba},
		};

		// ANTI-ALIASING: three versions per program, which
		// differ only in PSX_EDGE_INTERP (native_shaders.inc at
		// gpu_shader_common). Empty is the module that always existed - so the route
		// with one sample stays the old SPIR-V -, `centroid` for
		// draws into a multisampled image, `sample` for the sample shading
		// variant. Which one is used is decided by NativeGfxVK_Draw.
		static const struct
		{
			const char *suffix;
			const char *define;
		} interps[] = {
		    {"", "#define PSX_EDGE_INTERP\n"},
		    {"_centroid", "#define PSX_EDGE_INTERP centroid\n"},
		    {"_sample", "#define PSX_EDGE_INTERP sample\n"},
		};

		for (int i = 0; i < (int)(sizeof(variants) / sizeof(variants[0])); i++)
		{
			for (int k = 0; k < (int)(sizeof(interps) / sizeof(interps[0])); k++)
			{
				char path[1024];

				// One variant. There was a second with #define BILINEAR_FILTER in the
				// preamble, and no shader source reads that name - the four pairs came
				// out bit-identical SPIR-V. Filtering is the bilinearFilter uniform.
				const char *plain[] = {VK_HEADER_FRAG, "#define FRAGMENT\n", interps[k].define, gpu_shader_common, GPU_DITHERING, variants[i].body};

				snprintf(path, sizeof(path), "%s%s.frag", variants[i].name, interps[k].suffix);
				if (!WriteParts(path, plain, 6))
				{
					return 1;
				}
			}
		}
	}

	// The blit shaders, from the shared list in native_shaders.inc, not from a
	// copy of it here.
	// A shader the renderer knows about and this tool does not is a module that
	// is never built, and under Vulkan that is a draw that never happens.
	{
		const char *name = NULL;
		const char *body = NULL;

		for (int i = 0; CtrBlitShader_Get(i, &name, &body); i++)
		{
			if (!WriteBlit(name, body))
			{
				return 1;
			}
		}
	}

	return 0;
}
