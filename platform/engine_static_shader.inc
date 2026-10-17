// The engine's lit program for the static model, "ns" (ENGINE_PROGRAM_NS in
// include/platform/engine.h; platform/engine_static.c draws with it). Included by
// native_shaders.inc, so that tools/shader_spirv_probe.c emits ns.vert / ns.frag
// with the other programs and the ctest shader_spirv_dialect compiles it. The
// block "NeBlock" is struct EngineUniforms field for field
// (ENGINE_UNIFORM_BLOCK_GLSL, shared with "ne" and "nw").
//
// WHAT IT DRAWS. The base colour texture x vertex colour x tint of "ne"'s
// native path (same sRGB re-encode, same alpha test), times a light: an ambient
// term plus one sun, shaded with a normal that a tangent-space normal map may
// bend.
//
// THE UNIFORMS IT READS
//   tint        RGBA multiplier (material tint x draw tint), as in "ne"
//   params.x    1 = the base colour texture of slot 0, 0 = none (the colour is
//               vertex colour x tint), as in "ne"
//   params.y    1 = re-encode the texture (it is sampled linear) like "ne"
//   params.z    the alpha cutoff (0 = none), tested before the lighting
//   params3.z   0 = lit, 1 = lit with the normal map of slot 3 (s_normal),
//               2 = unlit (KHR_materials_unlit): base colour x vertex colour x
//               tint only, no light and no grade; plus 4 = PSX BLEND (below)
//   params3.w   the normal map's scale on x and y (glTF normalTexture.scale)
//   split.xyz   the direction towards the sun (unit, world axes)
//   split.w     the ambient light from above
//   params4.xyz the camera position (world units)
//   params4.w   the sun's diffuse strength
//   vramPage    the STYLE: x saturation, y contrast (1 = unchanged each), z 1 =
//               flat shading (the face's own normal, no vertex normal, no normal
//               map), w the ambient light from below - "ns" reads no VRAM page,
//               the renderer puts EngineDraw.user2 there
//
// THE LIGHT. A hemisphere ambient - from below (vramPage.w) to above (split.w)
// by 0.5 + 0.5 n.y - plus the sun: ambient(n) + diffuse * max(0, n . sun).
//
// THE VERTEX. a_uvBox is no UV box here: xyz = (normal * 0.5 + 0.5) * 255
// rounded, w = 255 when the vertex has a normal, 0 when not.
//
// LINEAR FACTORS. The vertex colour (glTF COLOR_0, e.g. baked ambient occlusion)
// and the tint (glTF baseColorFactor) are linear values by glTF; the texel is
// re-encoded to the display curve before it is multiplied (params.y, as "ne").
// So both factors are encoded with the same sRGB curve first: the product of
// encoded values is the encoded product (exact for a pure power curve, within a
// few levels for sRGB). "ne" multiplies them as they are (the look of the flat
// model stays as it was).
//
// PSX BLEND (the hi-res drivers, round of 2026-10-17; params3.z 4..6, set only
// by platform/engine_hires_char.c for a textured material of a file whose
// vertex colours are PSX blend, include/rldhires_color.inc). The vertex colour
// holds the PS1 byte, and grey 128 is neutral: it is read as byte / 128 on the
// display curve (a_color x 255 / 128) instead of being encoded, so the texel is
// multiplied by colour / 128 exactly as on the PS1, and the product texel x
// colour x tint is clamped to 1 before the light. Every other draw of "ns"
// (params3.z 0..2: the static model, hi-res files in glTF or off) takes the
// path it always took, with the same operations in the same order.
//
// THE NORMAL. The vertex normal when there is one, else the face's own normal
// from the screen derivatives of the world position. It is turned to the side
// the face is seen from (double-sided faces; and the derivative normal, whose
// sign depends on the screen axes). The normal map is applied in a tangent frame
// taken from the screen derivatives of the position and the texture coordinate
// (no tangents in the vertex): T along increasing u, B along increasing v. glTF
// green is up, towards smaller v, hence the minus on B.
//
// THE GRADE. After the light: the saturation mixes the colour with its luma
// (Rec. 709 weights), the contrast scales it around mid grey; both on the
// encoded colour, clamped to 0..1.
//
// DERIVATIVES. Every dFdx / dFdy and every texture fetch happens before the
// discard: the derivatives need the whole 2 x 2 quad (see "ne").

global_variable const char *ctr_native_static_shader =
    "#ifdef VERTEX\n"
    "layout(location = 0) in vec3 a_position;\n"
    "layout(location = 1) in vec2 a_texcoord;\n"
    "layout(location = 2) in vec4 a_color;\n"
    "layout(location = 3) in vec4 a_uvBox;\n"
    "VARYING_LOC0 out vec2 v_uv;\n"
    "VARYING_LOC1 out vec4 v_color;\n"
    "VARYING_LOC2 out vec3 v_world;\n"
    "VARYING_LOC3 out vec3 v_normal;\n"
    "VARYING_LOC4 out float v_hasNormal;\n"
    ENGINE_UNIFORM_BLOCK_GLSL
    "vec3 encodeSrgb(vec3 c) {\n"
    "	c = clamp(c, 0.0, 1.0);\n"
    "	return mix(1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, c * 12.92, lessThan(c, vec3(0.0031308)));\n"
    "}\n"
    "void main() {\n"
    "	v_uv = a_texcoord;\n"
    // LINEAR FACTORS: the vertex colour and the tint on the display curve, alpha as it is.
    // PSX BLEND (params3.z >= 4): the vertex colour is the PS1 byte, read as byte / 128.
    "	vec3 vertexColour = (params3.z > 3.5) ? (a_color.rgb * (255.0 / 128.0)) : encodeSrgb(a_color.rgb);\n"
    "	v_color = vec4(vertexColour * encodeSrgb(tint.rgb), a_color.a * tint.a);\n"
    "	v_world = a_position;\n"
    "	v_normal = a_uvBox.xyz / 127.5 - 1.0;\n"
    "	v_hasNormal = a_uvBox.w;\n"
    "	gl_Position = clipFromModel * vec4(a_position, 1.0);\n"
    // The material's depth offset, as in "ne" (params2.z, 0 = none).
    "	gl_Position.z *= (1.0 + params2.z);\n"
    "}\n"
    "#endif\n"
    "#ifdef FRAGMENT\n"
    "VARYING_LOC0 centroid in vec2 v_uv;\n"
    "VARYING_LOC1 in vec4 v_color;\n"
    "VARYING_LOC2 in vec3 v_world;\n"
    "VARYING_LOC3 in vec3 v_normal;\n"
    "VARYING_LOC4 in float v_hasNormal;\n"
    "SAMPLER_SLOT0 uniform sampler2D s_texture;\n"
    "SAMPLER_SLOT3 uniform sampler2D s_normal;\n"
    ENGINE_UNIFORM_BLOCK_GLSL
    "void main() {\n"
    // Before any discard or branch: the derivatives and fetches need the whole 2 x 2 quad.
    "	vec3 p = v_world;\n"
    "	vec3 dp1 = dFdx(p);\n"
    "	vec3 dp2 = dFdy(p);\n"
    "	vec2 duv1 = dFdx(v_uv);\n"
    "	vec2 duv2 = dFdy(v_uv);\n"
    "	vec4 texel = texture(s_texture, v_uv);\n"
    "	vec3 nt = texture(s_normal, v_uv).xyz * 2.0 - 1.0;\n"
    "	vec4 colour = v_color;\n"
    "	if (params.x > 0.5) {\n"
    "		if (params.y > 0.5) {\n"
    "			vec3 encoded = mix(1.055 * pow(texel.rgb, vec3(1.0 / 2.4)) - 0.055, texel.rgb * 12.92, lessThan(texel.rgb, vec3(0.0031308)));\n"
    "			colour *= vec4(encoded, texel.a);\n"
    "		} else { colour *= texel; }\n"
    "	}\n"
    // PSX BLEND: texel x colour / 128 x tint, clamped as on the PS1 (before the light);
    // shading is the mode of params3.z without the 4.
    "	bool psxBlend = (params3.z > 3.5);\n"
    "	float shading = psxBlend ? (params3.z - 4.0) : params3.z;\n"
    "	if (psxBlend) { colour.rgb = min(colour.rgb, vec3(1.0)); }\n"
    "	if ((params.z > 0.0) && (colour.a < params.z)) { discard; }\n"
    "	vec3 eyeDir = params4.xyz - p;\n"
    "	bool flatShade = (vramPage.z > 0.5);\n"
    "	vec3 N = ((v_hasNormal > 127.5) && !flatShade) ? normalize(v_normal) : normalize(cross(dp1, dp2));\n"
    // The side the face is seen from.
    "	if (dot(N, eyeDir) < 0.0) { N = -N; }\n"
    "	bool unlit = (shading > 1.5);\n"
    "	if (unlit) { fragColor = colour; return; }\n"
    "	if ((shading > 0.5) && !flatShade) {\n"
    "		nt.xy *= params3.w;\n"
    "		vec3 dp2perp = cross(dp2, N);\n"
    "		vec3 dp1perp = cross(N, dp1);\n"
    // T along increasing u, B along increasing v.
    "		vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;\n"
    "		vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;\n"
    "		float m = max(dot(T, T), dot(B, B));\n"
    "		if (m > 0.0) {\n"
    "			float inv = inversesqrt(m);\n"
    // glTF green = up = towards smaller v, hence -B.
    "			N = normalize(T * inv * nt.x - B * inv * nt.y + N * nt.z);\n"
    "		}\n"
    "	}\n"
    "	float ambient = mix(vramPage.w, split.w, 0.5 + 0.5 * N.y);\n"
    "	float lit = ambient + params4.w * max(dot(N, split.xyz), 0.0);\n"
    "	vec3 c = colour.rgb * lit;\n"
    "	float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));\n"
    "	c = mix(vec3(luma), c, vramPage.x);\n"
    "	c = (c - 0.5) * vramPage.y + 0.5;\n"
    "	fragColor = vec4(clamp(c, 0.0, 1.0), colour.a);\n"
    "}\n"
    "#endif\n";
