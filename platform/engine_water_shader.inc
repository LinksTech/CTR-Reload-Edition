// The water program "nw" (platform/engine_water.c; include/platform/engine.h,
// section WATER). Included by platform/native_shaders.inc and listed there in
// CtrBlitShader_Get, so tools/shader_spirv_probe.c emits nw.vert / nw.frag and
// the ctest shader_spirv_dialect compiles it. Same vertex layout, samplers and
// uniform block as "ne" (ENGINE_UNIFORM_BLOCK_GLSL).
//
// Placeholder of the round of 2026-10-15 (scaffold): the vertex colour times
// the tint, nothing else.
global_variable const char *ctr_native_water_shader =
    "#ifdef VERTEX\n"
    "layout(location = 0) in vec3 a_position;\n"
    "layout(location = 1) in vec2 a_texcoord;\n"
    "layout(location = 2) in vec4 a_color;\n"
    "VARYING_LOC0 out vec2 v_uv;\n"
    "VARYING_LOC1 out vec4 v_color;\n"
    ENGINE_UNIFORM_BLOCK_GLSL
    "void main() {\n"
    "	v_uv = a_texcoord;\n"
    "	v_color = a_color * tint;\n"
    "	gl_Position = clipFromModel * vec4(a_position, 1.0);\n"
    "}\n"
    "#endif\n"
    "#ifdef FRAGMENT\n"
    "VARYING_LOC0 in vec2 v_uv;\n"
    "VARYING_LOC1 in vec4 v_color;\n"
    "SAMPLER_SLOT0 uniform sampler2D s_texture;\n"
    "SAMPLER_SLOT1 uniform sampler2D s_vram;\n"
    ENGINE_UNIFORM_BLOCK_GLSL
    "void main() {\n"
    "	fragColor = v_color;\n"
    "}\n"
    "#endif\n";
