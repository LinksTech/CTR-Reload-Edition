// Shader sources, shared between the renderer and the build-time SPIR-V check.
//
// They live in their own file because two things needed them: a renderer that
// compiled them at runtime, and tools/shader_spirv_probe.c, which assembles the
// same bodies under a Vulkan preamble and hands them to glslangValidator.
//
// The runtime compiler is gone with the backend that had one. What is left is
// the probe, so these bodies are now the single source the build compiles the
// linked modules from - and the renderer uses them only as identity, comparing
// the pointer it was handed against the objects here to find a module's name.
//
// Only the bodies are here. The preamble belongs with whoever compiles them.
//
// Included, not compiled on its own: these are C string literals that the
// including file uses directly, and the unity build has no place for another
// translation unit.

// Sampling out of the page store instead of out of VRAM.
//
// The store holds one index per pixel, so this reads an index and looks it up
// in the palette - which still lives in VRAM, exactly where the old route left
// it. The bit unpacking is gone: no nibble arithmetic, no byte selection, no
// dependence on how many texels share a 16-bit word. That is the whole point,
// and it is what lets an index live somewhere other than inside a VRAM word.
//
// The tile is worked out from the page origin the vertex stage already passes:
// v_page_clut.xy is that origin in VRAM words, so dividing gives the page, and
// the layer is the page doubled plus the depth. How many tiles fit across, how
// big one is, and how big the atlas is all arrive as psxPageGrid - derived in C
// from the one place that knows the atlas size, so there is no second copy of
// it in here to fall out of step.
//
// WHAT DID NOT GET SIMPLIFIED, AND WHY. Which texel of the page a coordinate
// names is still computed the way the VRAM route computes it, per depth, down
// to the two epsilons. That looks like the duplication this function was meant
// to remove, and it is not the same thing: the VRAM route's expression is not
// floor(tc). It rounds the word up at 0.001 of a texel before the boundary and
// the sub-word index up at 0.0004, and in the 0.0006-wide window between the
// two it names a texel four positions away. Writing floor(tc) here instead
// would be more correct and would stop this being a comparison - roughly ten
// fragments a frame would differ for a reason that has nothing to do with the
// page store. So the coordinate is copied verbatim and the packing is not.
// Straightening it is a change of its own, with its own measurement.
//
// The word is computed with the page origin still added, then the origin taken
// off again, rather than on its own: at a magnitude near 1024 a float's step is
// about 0.0001, so floor(a + origin) and origin + floor(a) are not the same
// question. Both operands here are exact integers below 1024, so the subtraction
// is exact.
//
// tc is clamped to the tile. The old route let U run past the page into the one
// next door, which the hardware allows - and the fill writes those columns from
// exactly there, so 8-bit pages that reach 128 words keep reading what they
// always read.
//
// The clamp can only bite above 255, and a coordinate above 255 cannot be
// interpolated out of u8 vertex corners. Bilinear reads pixel + 1 and is the one
// path that can, at the very last column, for one repeated column instead of one
// from elsewhere. Not an argument: 4.3 million coordinates on two real VRAM
// dumps, every reachable integer corner among them, at scale 1, 2 and 4 - both
// routes named the same palette entry every time.
//
// psxPageScale is how many atlas texels one VRAM texel occupies. At one, the
// sub-texel term is floor(fract(tc) * 1.0) clamped to 0..0, which is zero for
// every input - so the atlas coordinate is the texel index and nothing else.
#define GPU_PAGE_SAMPLE_FUNC                                                           \
	"	SAMPLER_SLOT2 uniform sampler2D s_pages;\n"                                   \
	"	vec2 samplePages(vec2 tc, float depth, float column) {\n"                     \
	"		float layer = (floor(v_page_clut.y / psxPageLayout.z) * psxPageLayout.x\n"                        \
	"		            + floor(v_page_clut.x / psxPageLayout.y)) * 2.0 + depth;\n"                 \
	"		vec2 tile = vec2(mod(layer, psxPageGrid.x),\n"                               \
	"		                 floor(layer / psxPageGrid.x)) * psxPageGrid.y;\n"           \
	"		vec2 texel = vec2(column,\n"                                                 \
	"		                  floor(tc.y + v_page_clut.y) - floor(v_page_clut.y));\n"    \
	"		vec2 within = clamp(floor(fract(tc) * psxPageScale),\n"                      \
	"		                    vec2(0.0), vec2(psxPageScale - 1.0));\n"                  \
	"		vec2 cell = clamp(texel, vec2(0.0), vec2(255.0)) * psxPageScale + within;\n" \
	"		vec2 uv = (tile + cell + 0.5) * psxPageGrid.zw;\n"                           \
	"		float index = floor(texture(s_pages, uv).r * 255.0 + 0.5);\n"                \
	"		vec2 clut_pos = v_page_clut.zw;\n"                                           \
	"		clut_pos.x += index * c_VRAMTexel.x;\n"                                      \
	"		return VRAM(clut_pos);\n"                                                    \
	"	}\n"

// The two coordinate expressions, each the VRAM route's own arithmetic for the
// same depth. Read them beside GPU_SAMPLE_TEXTURE_4BIT_FUNC and
// GPU_SAMPLE_TEXTURE_8BIT_FUNC below: word times texels-per-word, plus the
// index within the word, is exactly what those two compute before they start
// masking bits.
#define GPU_PAGE_COLUMN_4BIT                                          \
	"(floor(tc.x * 0.25 + v_page_clut.x) - floor(v_page_clut.x)) * 4.0" \
	" + floor(fract(tc.x / 4.0 + 0.0001) * 4.0)"

#define GPU_PAGE_COLUMN_8BIT                                         \
	"(floor(tc.x * 0.5 + v_page_clut.x) - floor(v_page_clut.x)) * 2.0" \
	" + floor(mod(tc.x, 2.0))"

// An indexed texture is sampled out of the page store, and only out of it.
//
// Both of these used to carry a second body: the same texel fetched straight
// out of a VRAM word, with its own bit masks - a nibble pair for 4 bit, a byte
// pair for 8 bit - and a runtime switch between the two. The switch is gone and
// so are the two bodies. What is left is one function per depth that differs
// only in which tile it reads and how a column maps to a word, which is what
// this route was for.
//
// The two roads were shown to arrive at the same bytes before one of them was
// taken out.

// ONE SHADER FOR THREE FORMATS.
//
// Until here there were three fragment shaders, one each for 4, 8 and 16 bit, and
// every change of the texture format in the ordering table was a new split,
// that is a vkCmdDraw of its own. On Sunset Vista that was 51 % of all splits
// (measured). The format is a property of the primitive, not of the draw:
// it now travels in the vertex (native_gpu.c, NativeGpu_VertexPageWord, bits 5-6
// of the page word) and arrives here as the flat varying v_format. Three routes,
// one function; the branch is uniform per triangle.
//
// The three routes are line for line the old ones: page store with depth 0
// and the 4-bit column, page store with depth 1 and the 8-bit column,
// directly from VRAM. What changes is only who decides.
#define GPU_SAMPLE_TEXTURE_FUNC                                                          \
	GPU_PAGE_SAMPLE_FUNC                                                                 \
	"	// returns 16 bit colour\n"                                                        \
	"	vec2 samplePSX(vec2 tc) {\n"                                                       \
	"		if (v_format < 0.5) { return samplePages(tc, 0.0, " GPU_PAGE_COLUMN_4BIT "); }\n" \
	"		if (v_format < 1.5) { return samplePages(tc, 1.0, " GPU_PAGE_COLUMN_8BIT "); }\n" \
	"		vec2 uv = (tc + v_page_clut.xy) * c_VRAMTexel;\n"                                \
	"		return VRAM(uv);\n"                                                              \
	"	}\n"

#define GPU_FETCH_VRAM_FUNC                                          \
	"	const vec2 c_VRAMTexel = vec2(1.0 / 1024.0, 1.0 / 512.0);\n"   \
	"	SAMPLER_SLOT0 uniform sampler2D s_texture;\n"                  \
	"	vec2 VRAM(vec2 uv) { return texture(s_texture, uv).rg; }\n"

#define GPU_STP_PASS_FUNC                                                                     \
	"	float texelVisible(vec2 rg) { return float(rg.x + rg.y > 0.0); }\n"                     \
	"	float stpWeight(vec2 rg) { return step(0.5, rg.y); }\n"                                 \
	"	bool discardForSemiTransPass(float visible, float stpVisible, float nonStpVisible) {\n" \
	"		if(visible < 0.5) { return true; }\n"                                                  \
	"		if(psxSemiTransPass == 1 && nonStpVisible < 0.5) { return true; }\n"                   \
	"		if(psxSemiTransPass == 2 && stpVisible < 0.5) { return true; }\n"                      \
	"		return false;\n"                                                                       \
	"	}\n"

// DITHER IS HALF OF AN OPERATION, AND WE ONLY ALWAYS DID THE FIRST HALF
//
// The console computed a colour finer than it could store, added this matrix to
// it, and THEN threw the low three bits away. Adding and truncating together are
// ordered dithering: the pattern buys back, in space, the precision that was
// lost in depth, and a CRT did the averaging.
//
// The truncation happens in exactly one place in this tree - the pack shader's
// `c.r >> 3` - and that runs only where a picture goes back into VRAM. Above
// internal resolution one the window is shown from the RGBA8 target instead,
// which is never truncated. There the matrix is added to eight bits and left
// there: a fixed pattern of plus or minus four in two hundred fifty-five, in
// PSX-pixel granularity, so each dot is an NxN block of real pixels.
//
// That is not dithering. It is the noise dithering was invented to hide, at
// four times the size, on a picture that no longer needs it. On a large flat
// surface, where the shading itself barely changes, it is the only thing
// varying - which is what a horizontal comb, stronger in the distance, is made
// of.
//
// psxDitherAmount is that second half, said out loud: one where the result of
// this draw will be truncated to five bits, zero where it will not. It is not a
// strength knob - the matrix means what the hardware meant or it means nothing.
//
// FOUND AND NOT CHANGED: the matrix is read transposed. GLSL fills a mat4 by
// COLUMN, so the four lines written below become columns, and c_dither[x][y]
// then picks written-line x at position y - the hardware's matrix is not
// symmetric, so this is its mirror along the diagonal. It has been that way
// since the shader was written, it is the same set of sixteen offsets in a
// different arrangement, and straightening it changes the picture in the stock
// stage. Its own change, with its own measurement.
#define GPU_DITHERING                                                              \
	"	const mat4 c_dither = mat4(\n"                                               \
	"		-4.0,  +0.0,  -3.0,  +1.0,\n"                                               \
	"		+2.0,  -2.0,  +3.0,  -1.0,\n"                                               \
	"		-3.0,  +1.0,  -4.0,  +0.0,\n"                                               \
	"		+3.0,  -1.0,  +2.0,  -2.0) / 255.0;\n"                                      \
	"	vec4 dither(vec4 color) {\n"                                                 \
	"		if (psxDitherAmount == 0.0) { return color; }\n"                            \
	"		ivec2 dc = ivec2(mod(floor(v_ditherCoord), 4.0));\n"                        \
	"		color.xyz += vec3(c_dither[dc.x][dc.y] * v_texcoord.w * psxDitherAmount);\n" \
	"		return color;\n"                                                            \
	"	}\n"


// THE TEXTURE IS READ WHERE THE FRAGMENT IS, AND THAT WAS TRIED THE OTHER WAY
//
// A snapTexcoord stood here for a day: at an internal resolution above one it
// walked the coordinate back to the centre of the native pixel the fragment sat
// in, the way an upscaling emulator rounds texture coordinates. It is out
// again, taken out by decision after it was driven.
//
// What it cost to learn is worth the four lines it takes to write down. The
// walk is measured in texels, not pixels - the distance to the pixel centre
// times the coordinate's derivative - and on a surface seen edge-on that
// derivative is enormous, so the first version left the page entirely and tore
// the picture into bands. Bounding it to where a texel is bigger than a native
// pixel fixed that, and then the second version did nothing worth keeping,
// because the comb it was aimed at is worst in the DISTANCE. Distance is
// minification: a native pixel covering several texels, no single right texel to
// walk to. Rounding is a magnification idea and cannot reach the other half.
//
// The attempt is not kept, and it is not a place to start from again without
// a new argument for why the answer would be different.

// WHAT IS FILTERED, AND IN WHICH ORDER
//
// The four taps of bilinearTextureSample each call samplePSX first, and
// samplePSX is the whole indexed lookup: it fetches the palette index out of
// the page atlas with a point sample at an exact texel centre, then reads the
// CLUT row out of VRAM at that index. What comes back is already a colour.
//
// Only then does the mixing happen, and it happens on lut(C11)..lut(C22) - four
// decoded RGBA colours. **Indices are never interpolated.** Between index 3 and
// index 7 there is no colour, only index 5, and index 5 can be anything; that
// mistake is not made here.
//
// Four index lookups and four CLUT lookups per bilinear pixel, against one and
// one for nearest. That is what the correct order costs, and it has been paid
// for as long as this function has existed.
//
// A `vec2 rg = mix(mix(C11, C21, ...), ...)` used to sit between the weights
// and the discard. It WAS an interpolation of raw VRAM words - the wrong thing,
// done on packed 5:5:5 - and it was assigned to a variable nothing read. Dead,
// but dead code that looks exactly like the bug the correct code avoids is
// worse than none, so it is gone.

// TWO THINGS THAT WERE WRONG HERE, AND ONE THAT WAS NOT WORTH KEEPING
//
// A smoothTextureSample stood beside these for a day: constant inside the texel,
// crossing over only at the boundary, over a width taken from the coordinate's
// own derivative. The idea is sound and the build was bounded; driven, it made
// no difference worth the four lookups it costs. Out again by decision, and
// not a place to start from again without a new argument for why the answer
// would come out otherwise.
//
// What it found on the way in stays, because both of those were real:
//
// FIRST, THE COLOURS WERE MIXED WITHOUT ASKING WHETHER THE TEXEL IS THERE. An
// invisible texel - index zero, the transparent one - still contributed its
// palette colour to the mix. That is a dark fringe along every transparency
// edge. blendTexels divides by the VISIBLE weight instead, so an invisible
// corner contributes nothing at all and an edge against nothing keeps its
// colour. With all four visible it is the old mix character for character.
//
// SECOND, THE SAMPLE POINT WAS HALF A TEXEL OFF. floor(P) and floor(P)+1 with
// fract(P) between them reaches the NEXT texel's colour while P is still inside
// THIS one. It was a bilinear shifted half a texel, and it always had been.
// Sampling around P - 0.5 puts the texel centres at weight 0 and 1 and the
// boundary at 0.5, where they belong. Only the bilinear debug toggle draws
// through here today, so this changes nothing that is on - but a filter that
// exists to be compared against has to be the thing it says it is.
#define GPU_FRAGMENT_SAMPLE_SHADER                                                                                                                 \
	/* Second output for the blend source SRC1 - the texel weight.             */                                                                     \
	/* Pipelines without dual source ignore it; its value does not matter then.*/                                                                     \
	"	layout(location = 0, index = 1) out vec4 fragWeight;\n"                                                                                        \
	GPU_FETCH_VRAM_FUNC                                                                                                                               \
	GPU_SAMPLE_TEXTURE_FUNC                                                                                                                           \
	    "	SAMPLER_SLOT1 uniform sampler2D s_rgLut;\n"                                                                                                 \
	    "	float sampledStp = 0.0;\n"                                                                                                                  \
	    "	const vec2 c_LUTTexel = vec2(1.0 / 256.0, 1.0 / 256.0);\n"                                                                                  \
	    "	vec4 lut(vec2 rg) { return texture(s_rgLut, rg - c_LUTTexel * 0.0001); }\n" GPU_STP_PASS_FUNC                                               \
	    "	vec4 blendTexels(vec2 pixel, vec2 w) {\n"                                                                                                   \
	    "		vec2 C11 = samplePSX(pixel);\n"                                                                                                            \
	    "		vec2 C21 = samplePSX(pixel + vec2(1.0, 0.0));\n"                                                                                           \
	    "		vec2 C12 = samplePSX(pixel + vec2(0.0, 1.0));\n"                                                                                           \
	    "		vec2 C22 = samplePSX(pixel + vec2(1.0, 1.0));\n"                                                                                           \
	    "		float v11 = texelVisible(C11);\n"                                                                                                          \
	    "		float v21 = texelVisible(C21);\n"                                                                                                          \
	    "		float v12 = texelVisible(C12);\n"                                                                                                          \
	    "		float v22 = texelVisible(C22);\n"                                                                                                          \
	    "		float s11 = v11 * stpWeight(C11);\n"                                                                                                       \
	    "		float s21 = v21 * stpWeight(C21);\n"                                                                                                       \
	    "		float s12 = v12 * stpWeight(C12);\n"                                                                                                       \
	    "		float s22 = v22 * stpWeight(C22);\n"                                                                                                       \
	    "		float n11 = v11 - s11;\n"                                                                                                                  \
	    "		float n21 = v21 - s21;\n"                                                                                                                  \
	    "		float n12 = v12 - s12;\n"                                                                                                                  \
	    "		float n22 = v22 - s22;\n"                                                                                                                  \
	    "		float ax1 = mix(v11, v21, w.x);\n"                                                                                                         \
	    "		float ax2 = mix(v12, v22, w.x);\n"                                                                                                         \
	    "		float axm = mix(ax1, ax2, w.y);\n"                                                                                                         \
	    "		float sx1 = mix(s11, s21, w.x);\n"                                                                                                         \
	    "		float sx2 = mix(s12, s22, w.x);\n"                                                                                                         \
	    "		float stp = mix(sx1, sx2, w.y);\n"                                                                                                         \
	    "		float nx1 = mix(n11, n21, w.x);\n"                                                                                                         \
	    "		float nx2 = mix(n12, n22, w.x);\n"                                                                                                         \
	    "		float nonStp = mix(nx1, nx2, w.y);\n"                                                                                                      \
	    "		sampledStp = stp;\n"                                                                                                                       \
	    "		if(discardForSemiTransPass(axm, stp, nonStp)) { discard; }\n"                                                                              \
	    "		float a11 = v11 * (1.0 - w.x) * (1.0 - w.y);\n"                                                                                            \
	    "		float a21 = v21 * w.x * (1.0 - w.y);\n"                                                                                                    \
	    "		float a12 = v12 * (1.0 - w.x) * w.y;\n"                                                                                                    \
	    "		float a22 = v22 * w.x * w.y;\n"                                                                                                            \
	    "		vec4 t = (a11 * lut(C11) + a21 * lut(C21) + a12 * lut(C12) + a22 * lut(C22))\n"                                                            \
	    "		       / max(a11 + a21 + a12 + a22, 0.0001);\n"                                                                                            \
	    "		t.w = 1.0 - t.w;\n"                                                                                                                        \
	    "		return t;\n"                                                                                                                               \
	    "	}\n"                                                                                                                                        \
	    "	vec4 bilinearTextureSample(vec2 P) {\n"                                                                                                     \
	    "		vec2 Q = P - 0.5;\n"                                                                                                                       \
	    "		vec2 pixel = floor(Q);\n"                                                                                                                  \
	    "		return blendTexels(pixel, Q - pixel);\n"                                                                                                   \
	    "	}\n"                                                                                                                                        \
	    "	vec4 nearestTextureSample(vec2 P) {\n"                                                                                                      \
	    "		vec2 rg = samplePSX(P);\n"                                                                                                                 \
	    "		float visible = texelVisible(rg);\n"                                                                                                       \
	    "		sampledStp = visible * stpWeight(rg);\n"                                                                                                   \
	    "		if(discardForSemiTransPass(visible, sampledStp, visible - sampledStp)) { discard; }\n"                                                     \
	    "		vec4 t = lut(rg);\n"                                                                                                                       \
	    "		t.w = 1.0 - t.w;\n"                                                                                                                        \
	    "		return t;\n"                                                                                                                               \
	    "	}\n"                                                                                                                                        \
	    "	void main() {\n"                                                                                                                            \
	    "		vec4 color = (bilinearFilter > 0) ? bilinearTextureSample(v_texcoord.xy) : nearestTextureSample(v_texcoord.xy);\n"                         \
	    "		fragColor = dither(color * v_color);\n"                                                                                                    \
	    "		fragColor.a = (psxDrawMaskSet != 0 || (psxTextureOutputStp != 0 && sampledStp >= 0.5)) ? 1.0 : 0.0;\n"                                     \
	    /* One pass for modes 0/1/3. psxSemiTransPass 3/4/5 says which; the weight is 0 for a texel without STP                        */             \
	    /* (opaque: F stays alone) and otherwise that of the mode. The same threshold as the two passes before (sampledStp >= 0.5).     */             \
	    "		float texelWeight = (psxSemiTransPass >= 3 && sampledStp >= 0.5) ? 1.0 : 0.0;\n"                                                          \
	    "		fragWeight = vec4(1.0, 1.0, 1.0, 0.0);\n"                                                                                                  \
	    "		if (psxSemiTransPass == 3) { fragWeight.a = 0.5 * texelWeight; }\n"                                                                        \
	    "		else if (psxSemiTransPass == 4) { fragWeight.a = texelWeight; }\n"                                                                         \
	    "		else if (psxSemiTransPass == 5) { fragWeight.a = texelWeight; fragWeight.rgb = vec3(1.0 - 0.75 * texelWeight); }\n"                        \
	    "	}\n"

// Everything both PSX stages have to agree on. This text is included in the
// vertex and the fragment source alike, which is what makes the uniform block
// safe to put here: both stages have to declare the block identically, and
// they cannot fail to match when they are the same string. The vertex stage
// reads Projection, the fragment stage the rest.
//
// ANTI-ALIASING: PSX_EDGE_INTERP stands before UV and
// vertex colour and is set per module by tools/shader_spirv_probe.c - empty
// for the modules that always existed (identical SPIR-V with one sample),
// `centroid` for draws into a multisampled image, `sample` for the
// sample shading variant. Without one of the two, with MSAA interpolation
// happens at the pixel centre,
// even if that lies outside the triangle - then an
// edge pixel reads UV beyond the edge, that is neighbouring texels or a foreign CLUT.
// v_page_clut and v_ditherCoord do not get it: the one is constant per primitive,
// the other is meant to stay on the PSX pixel grid.
global_variable const char *gpu_shader_common = "	VARYING_LOC0 PSX_EDGE_INTERP varying vec4 v_texcoord;\n"
                                                "	VARYING_LOC1 PSX_EDGE_INTERP varying vec4 v_color;\n"
                                                "	VARYING_LOC2 varying vec4 v_page_clut;\n"
                                                "	VARYING_LOC3 varying vec2 v_ditherCoord;\n"
                                                // The texture format of this primitive: 0 = 4 bit, 1 = 8 bit,
                                                // 2 = 16 bit. Flat, because it is one value per triangle and
                                                // must never be interpolated between two formats.
                                                "	VARYING_LOC5 flat varying float v_format;\n"
                                                "	UBO_QUALIFIER uniform PsxBlock {\n"
                                                "		mat4 Projection;\n"
                                                "		vec2 texelSize;\n"
                                                "		int bilinearFilter;\n"
                                                "		int psxSemiTransPass;\n"
                                                "		int psxDrawMaskSet;\n"
                                                "		int psxTextureOutputStp;\n"

                                                // How many atlas texels one VRAM texel occupies. One means the atlas is
                                                // the same picture as VRAM, differently stored.
                                                "		float psxPageScale;\n"

                                                // One where this draw's result will be truncated to five bits per
                                                // channel, zero where it will not. See GPU_DITHERING. It occupies the
                                                // slot that used to be a written-out pad, so no field below it moves.
                                                "		float psxDitherAmount;\n"

                                                // x tiles across, y tile size in atlas texels, zw one over the atlas
                                                // size. All four derived in C from the one place that knows how big
                                                // the atlas is, so the shader carries no second copy of it.
                                                "		vec4 psxPageGrid;\n"

                                                // x pages across a VRAM row, y page width in VRAM words, z page height
                                                // in VRAM rows. The VRAM route below writes these as literals - 16, 64,
                                                // 256 - and so did the page route until they were handed over instead.
                                                // They are derived in C from the page size and the VRAM size, which is
                                                // where both facts actually live.
                                                "		vec4 psxPageLayout;\n"

                                                // x: one if the CLUT value is to be read as the window origin into a
                                                // host texture. See NativePSXUniforms.
                                                "		vec4 psxUvOrigin;\n"
                                                "	};\n";

const char *gte_shader_psx = GPU_FRAGMENT_SAMPLE_SHADER;
const char *gte_shader_32_rgba = "	SAMPLER_SLOT0 uniform sampler2D s_texture;\n"
                                 "	void main() {\n"
                                 "		vec2 tc = v_texcoord.xy * texelSize + texelSize * 0.5;\n"
                                 "		vec4 color = texture(s_texture, tc);\n"
                                 "		fragColor = dither(color * v_color);\n"
                                 // THE ALPHA OF THE TEXTURE SURVIVES - and that is a change with
                                 // a price that has to be written here.
                                 //
                                 // Here stood fragColor.a = float(psxDrawMaskSet). That is right for the three
                                 // PSX formats: output alpha IS the mask bit there, which on
                                 // packing back into VRAM lands as bit 15.
                                 //
                                 // For a host texture it cannot stay. BM_ALPHA weights with
                                 // SRC_ALPHA, that is with exactly this value - with the mask bit in it
                                 // every fragment would be either entirely gone (0) or entirely there (1), and a picture
                                 // with real alpha would not exist. Both in one channel does not work.
                                 //
                                 // WHO ELSE THIS AFFECTS: nobody. This program only runs for
                                 // TF_32_BIT_RGBA (NativeRenderer_SetTexture), and that format is set only by
                                 // AddSplit for a set host texture (native_gpu.c). When this
                                 // was written no track set one, so this branch had never
                                 // run. The mask bit of a 32-bit drawing is therefore open and
                                 // belongs to the feature that builds on this proof, not here.
                                 "		fragColor.a = color.a;\n"
                                 "	}\n";

// THE FRACTIONAL DIGITS OF THE PROJECTION. a_extra.zw are the two bytes that
// used to stand here as "unused"; they now carry four bits each of
// sixteenth pixels from the mirror of the prim arena (include/ctr_subpixel.h).
// If --subpixel is off, they are zero and the addition adds a zero - that is why
// no second pipeline is needed.
//
// Only gl_Position. v_ditherCoord stays a_position.xy, because the dither matrix
// belongs on the PSX pixel grid and not in between.
#define GTE_PERSPECTIVE_CORRECTION "	gl_Position = Projection * vec4(a_position.xy + a_extra.zw * (1.0 / 16.0), 0.0, 1.0);\n"

#define GTE_VERTEX_SHADER                                                                                          \
	"	layout(location = 0) in vec4 a_position;\n"                                                                  \
	"	layout(location = 1) in vec4 a_texcoord; // uv, color multiplier, dither\n"                                  \
	"	layout(location = 2) in vec4 a_color;\n"                                                                     \
	"	layout(location = 3) in vec4 a_extra; // texcoord.xy ofs, unused.xy\n"                                       \
	"	const vec2 c_UVFudge = vec2(0.00025, 0.00025);\n"                                                            \
	"	void main() {\n"                                                                                             \
	"		v_ditherCoord = a_position.xy;\n"                                                                           \
	"		v_texcoord = a_texcoord;\n"                                                                                 \
	"		v_texcoord.xy += a_extra.xy * 0.5;\n"                                                                       \
	/* THE WINDOW ORIGIN FROM THE CLUT VALUE. Only for a host texture; psxUvOrigin.x is   */                         \
	/* otherwise zero and the two lines add zero. getClut puts x/16 into the lower       */                          \
	/* six bits and y above them (libgpu.h:208) - exactly this decomposition is here.    */                          \
	"		v_texcoord.x += psxUvOrigin.x * mod(a_position.w, 64.0) * 16.0;\n"                                          \
	"		v_texcoord.y += psxUvOrigin.x * floor(a_position.w / 64.0);\n"                                             \
	"		v_color = a_color;\n"                                                                                       \
	"		v_color.xyz *= a_texcoord.z;\n"                                                                            \
	"		// a_position.z is the page word: page 0..31 in bits 0-4, format in bits 5-6.\n"                             \
	"		float pageFormat = floor(a_position.z / 32.0);\n"                                                           \
	"		float pageBase = a_position.z - pageFormat * 32.0;\n"                                                       \
	"		v_format = pageFormat;\n"                                                                                   \
	"		v_page_clut.x = fract(pageBase / 16.0) * 1024.0;\n"                                                         \
	"		v_page_clut.y = floor(pageBase / 16.0) * 256.0;\n"                                                          \
	"		v_page_clut.z = fract(a_position.w / 64.0);\n"                                                              \
	"		v_page_clut.w = floor(a_position.w / 64.0) / 512.0;\n"                                                      \
	"		v_page_clut.xy += c_UVFudge;\n"                                                                             \
	"		v_page_clut.zw += c_UVFudge;\n" GTE_PERSPECTIVE_CORRECTION \
	"	}\n"

// The build this renderer was ported from also carries a mod-menu overlay
// shader here. Its menu is not part of this port, so the shader is not either -
// an unused body would still be compiled by the SPIR-V check and would have to
// keep working for nobody.

// NOTE(aalhendi): GPU VRAM pack. Samples an RGBA render texture and writes PS1
// 5:5:5:1 pixels into RG8 VRAM, low byte in R and high byte in G. NEAREST
// sampling and integer channel shifts preserve the packed PS1 pixel value.
// Written in the shared dialect: explicit locations and a named uniform block,
// which is what Vulkan requires. The qualifier macros and the #version line
// come from the preamble in tools/shader_spirv_probe.c.

// HOW MANY SAMPLES ONE OUTPUT PIXEL AVERAGES, at most.
//
// One number, used in four places: the loop bound in each of the two shaders
// that reduce a picture - the pack back into VRAM and the presentation resolve -
// and the clamp in the C function that fills their taps for each. They must all
// be the same or a shader would quietly average fewer samples than it was told
// to, so the shader text takes the digit from here rather than carrying its own.
//
// Four covers a reduction of four to one in each axis. That is exact for every
// internal factor up to four; past it the clamp says so rather than sampling
// less and staying quiet.
#define CTR_RESOLVE_TAPS_MAX 4
#define CTR_STRINGIFY_INNER(x) #x
#define CTR_STR(x) CTR_STRINGIFY_INNER(x)

// Packs a render target back into the 16-bit VRAM texture - AND IS THE OTHER
// PLACE THE PICTURE IS REDUCED.
//
// VRAM is never scaled, so above factor one this pass reads a target that is
// k times larger in each axis and writes one VRAM texel per k*k of them. It used
// to do that with a single tap and whatever texture filter the previous draw
// happened to leave bound, which is fifteen of every sixteen samples thrown away
// at factor four, by a rule nobody stated.
//
// That matters beyond tidiness: this VRAM copy is what the GAME reads. Screen
// copy effects sample it, the next frame loads from it when the draw environment
// does not clear, and every VRAM dump is taken from it. A decimated framebuffer
// there is a decimated input to the game's own effects.
//
// So it takes the same box as the presentation resolve, from the same block, with
// the same proof behind it: uniform weights, centred on the destination texel, and
// at an integer reduction the taps land on exactly the k*k source texel centres.
//
// ALPHA IS NOT AVERAGED. Bit 15 of a PSX word is the mask bit, not a colour
// channel - it is carried in the target alpha and packed straight back. The mean
// of sixteen mask bits is not a mask bit. It is taken from the centre tap, which
// is the single sample this pass took before the box existed, so the mask keeps
// exactly the behaviour it had.
//
// At one tap the offsets are zero and the average is over one sample, so this is
// literally the old expression - the offscreen pack, which is never scaled, is
// unchanged bit for bit.

global_variable const char *ctr_pack_shader =
    "#ifdef VERTEX\n"
    "layout(location = 0) in vec2 a_position;\n"
    "VARYING_LOC0 out vec2 v_uv;\n"
    "UBO_QUALIFIER uniform ResolveBlock {\n"
    "	int flipY;\n"
    "	vec4 taps;\n"
    "};\n"
    "void main() {\n"
    "	v_uv = a_position * 0.5 + 0.5;\n"
    "	if (flipY != 0) { v_uv.y = 1.0 - v_uv.y; }\n"
    "	gl_Position = vec4(a_position, 0.0, 1.0);\n"
    "}\n"
    "#endif\n"
    "#ifdef FRAGMENT\n"
    "VARYING_LOC0 in vec2 v_uv;\n"
    "SAMPLER_SLOT0 uniform sampler2D s_src;\n"
    "UBO_QUALIFIER uniform ResolveBlock {\n"
    "	int flipY;\n"
    "	vec4 taps;\n"
    "};\n"
    "void main() {\n"
    "	float nx = taps.x;\n"
    "	float ny = taps.y;\n"
    "	vec3 sum = vec3(0.0);\n"
    "	for (int j = 0; j < " CTR_STR(CTR_RESOLVE_TAPS_MAX) "; j++) {\n"
    "		if (float(j) >= ny) { break; }\n"
    "		for (int i = 0; i < " CTR_STR(CTR_RESOLVE_TAPS_MAX) "; i++) {\n"
    "			if (float(i) >= nx) { break; }\n"
    "			vec2 offset = vec2((float(i) + 0.5 - nx * 0.5) * taps.z,\n"
    "			                   (float(j) + 0.5 - ny * 0.5) * taps.w);\n"
    "			sum += texture(s_src, v_uv + offset).rgb;\n"
    "		}\n"
    "	}\n"
    "	vec4 src = vec4(sum / (nx * ny), texture(s_src, v_uv).a);\n"
    "	ivec4 c = ivec4(src * 255.0 + 0.5);\n"
    "	int px16 = (c.r >> 3) | ((c.g >> 3) << 5) | ((c.b >> 3) << 10) | ((c.a >> 7) << 15);\n"
    "	fragColor = vec4(float(px16 & 0xFF) / 255.0, float((px16 >> 8) & 0xFF) / 255.0, 0.0, 0.0);\n"
    "}\n"
    "#endif\n";

// NOTE(aalhendi): Expand packed VRAM without losing bit 15. Internal render
// targets carry that PS1 STP/mask bit in alpha so packing them is lossless.
global_variable const char *ctr_present_vram_shader = "#ifdef VERTEX\n"
                                                      "layout(location = 0) in vec2 a_position;\n"
                                                      "VARYING_LOC0 out vec2 v_uv;\n"
                                                      "UBO_QUALIFIER uniform VramBlock {\n"
                                                      "\tvec4 sourceRect;\n"
                                                      "};\n"
                                                      "void main() {\n"
                                                      "\tvec2 screenUV = a_position * 0.5 + 0.5;\n"
                                                      "\tvec2 sourcePixel = sourceRect.xy + vec2(screenUV.x, 1.0 - screenUV.y) * sourceRect.zw;\n"
                                                      "\tv_uv = sourcePixel / vec2(1024.0, 512.0);\n"
                                                      "\tgl_Position = vec4(a_position, 0.0, 1.0);\n"
                                                      "}\n"
                                                      "#endif\n"
                                                      "#ifdef FRAGMENT\n"
                                                      "VARYING_LOC0 in vec2 v_uv;\n"
                                                      "SAMPLER_SLOT0 uniform sampler2D s_texture;\n"
                                                      "void main() {\n"
                                                      "\tivec2 packedBytes = ivec2(texture(s_texture, v_uv).rg * 255.0 + 0.5);\n"
                                                      "\tint pixel = packedBytes.r | (packedBytes.g << 8);\n"
                                                      "\tivec3 color5 = ivec3(pixel & 31, (pixel >> 5) & 31, (pixel >> 10) & 31);\n"
                                                      "\tivec3 color8 = (color5 << 3) | (color5 >> 2);\n"
                                                      "\tfloat stp = float((pixel >> 15) & 1);\n"
                                                      "\tfragColor = vec4(vec3(color8) / 255.0, stp);\n"
                                                      "}\n"
                                                      "#endif\n";

// Shows the main render target on the window without going through VRAM.
//
// Only needed above 1x internal resolution. At 1x the picture is packed into
// VRAM and shown from there, which is what every frame has always done and what
// keeps the emulated VRAM the single account of what is on screen.
//
// flipY is 0 here, and that is the one number in this file that cannot be read
// off a single pass:
//
//   pack writes the target into the VRAM texture with flipY 1, so the VRAM
//   texture's lowest row carries target v=1. present_vram then shows the VRAM
//   texture's lowest row at the top of the window. Composed, the top of the
//   window has always been target v=1 - which is what an unflipped v samples.
//
// AND IT IS THE RESOLVE.
//
// Rendering at four times the size and then showing it through one bilinear tap
// throws most of what was rendered away: a bilinear tap reads a 2x2
// neighbourhood whatever the reduction is, so a four-to-one reduction drops
// twelve of every sixteen texels and keeps the aliasing it was supposed to
// remove. What makes supersampling supersampling is that every source sample
// reaches the output pixel with the SAME weight.
//
// So the taps are laid out as a box: nx by ny samples, evenly spaced across the
// source footprint of one output pixel, all weighted 1/(nx*ny). The offsets are
// (i + 0.5 - nx/2) * step, which is symmetric about zero - the box is centred on
// the output pixel centre and there is no half-texel drift in either axis. At
// nx = ny = 1 the offset is exactly zero and this is the single tap it was
// before, which is what happens wherever the window is larger than the internal
// picture and there is nothing to reduce.
//
// The taps sample with the linear filter still on, so each one already averages
// its own neighbourhood. At an integer reduction the tap centres land on texel
// centres and the linear filter returns those texels exactly, which makes the
// result the exact box average rather than an approximation of one.
global_variable const char *ctr_present_target_shader =
    "#ifdef VERTEX\n"
    "layout(location = 0) in vec2 a_position;\n"
    "VARYING_LOC0 out vec2 v_uv;\n"
    "UBO_QUALIFIER uniform ResolveBlock {\n"
    "	int flipY;\n"
    "	vec4 taps;\n"
    "};\n"
    "void main() {\n"
    "	v_uv = a_position * 0.5 + 0.5;\n"
    "	if (flipY != 0) { v_uv.y = 1.0 - v_uv.y; }\n"
    "	gl_Position = vec4(a_position, 0.0, 1.0);\n"
    "}\n"
    "#endif\n"
    "#ifdef FRAGMENT\n"
    "VARYING_LOC0 in vec2 v_uv;\n"
    "SAMPLER_SLOT0 uniform sampler2D s_src;\n"
    "UBO_QUALIFIER uniform ResolveBlock {\n"
    "	int flipY;\n"
    "	vec4 taps;\n"
    "};\n"
    "void main() {\n"
    "	float nx = taps.x;\n"
    "	float ny = taps.y;\n"
    "	vec3 sum = vec3(0.0);\n"
    "	for (int j = 0; j < " CTR_STR(CTR_RESOLVE_TAPS_MAX) "; j++) {\n"
    "		if (float(j) >= ny) { break; }\n"
    "		for (int i = 0; i < " CTR_STR(CTR_RESOLVE_TAPS_MAX) "; i++) {\n"
    "			if (float(i) >= nx) { break; }\n"
    "			vec2 offset = vec2((float(i) + 0.5 - nx * 0.5) * taps.z,\n"
    "			                   (float(j) + 0.5 - ny * 0.5) * taps.w);\n"
    "			sum += texture(s_src, v_uv + offset).rgb;\n"
    "		}\n"
    "	}\n"
    "	fragColor = vec4(sum / (nx * ny), 1.0);\n"
    "}\n"
    "#endif\n";

// Fills one tile of the page store from VRAM.
//
// Drawn with the viewport set to the tile, so the quad's 0..1 covers exactly
// that tile and nothing else has to be told where it is. Every number it needs
// arrives in the block; there is no constant in here that another file also
// holds - not the VRAM size, not the tile size, not the atlas size.
//
// WHICH WAY UP. None: the write and the read agree without a flip, and that is
// derived, not measured. A target pass takes its viewport unflipped
// (native_gfx_vk.c, NativeGfxVK_Draw - the flip is only for the swapchain),
// and framebuffer row y is image row y. That puts NDC y = -1 at texture row
// viewportY, so v_uv.y rising is the atlas row rising. samplePages reads the atlas with v rising as the VRAM row
// rises, and this writes it the same way. If that is wrong the failure is loud:
// every 4-bit and 8-bit texture appears mirrored within its 256-row page.
//
// The 16-bit format has no page store and needs none - it holds colour, not an
// index, so there is nothing to look up and nothing to unpack.
global_variable const char *ctr_pack_pages_shader =
    "#ifdef VERTEX\n"
    "layout(location = 0) in vec2 a_position;\n"
    "VARYING_LOC0 out vec2 v_uv;\n"
    "void main() {\n"
    "	v_uv = a_position * 0.5 + 0.5;\n"
    "	gl_Position = vec4(a_position, 0.0, 1.0);\n"
    "}\n"
    "#endif\n"
    "#ifdef FRAGMENT\n"
    "VARYING_LOC0 in vec2 v_uv;\n"
    "SAMPLER_SLOT0 uniform sampler2D s_texture;\n"
    "UBO_QUALIFIER uniform PagesBlock {\n"
    // x VRAM word origin, y VRAM row origin, z depth (0 = 4 bit, 1 = 8 bit),
    // w how many atlas texels one VRAM texel occupies.
    "	vec4 pageSource;\n"
    // x tile size in atlas texels. The rest is padding std140 wants anyway.
    "	vec4 pageShape;\n"
    // xy VRAM size in texels, zw one over it.
    "	vec4 pageVram;\n"
    "};\n"
    "void main() {\n"
    "	vec2 cell = floor(v_uv * pageShape.x);\n"
    "	vec2 texel = floor(cell / pageSource.w);\n"
    "	float perWord = (pageSource.z < 0.5) ? 4.0 : 2.0;\n"
    "	float wordOffset = floor(texel.x / perWord);\n"
    "	float inWord = texel.x - wordOffset * perWord;\n"
    // Wrapped, not clamped. An 8-bit tile reaches 128 words and the last page
    // reaches past the end of VRAM; the VRAM texture is sampled with REPEAT, so
    // that is where the old route reads and that is where this reads.
    "	float wordX = mod(pageSource.x + wordOffset, pageVram.x);\n"
    "	float wordY = mod(pageSource.y + texel.y, pageVram.y);\n"
    "	ivec2 bytes = ivec2(texture(s_texture, (vec2(wordX, wordY) + 0.5) * pageVram.zw).rg * 255.0 + 0.5);\n"
    "	int word = bytes.r | (bytes.g << 8);\n"
    "	int shift = int(inWord) * ((pageSource.z < 0.5) ? 4 : 8);\n"
    "	int mask = (pageSource.z < 0.5) ? 15 : 255;\n"
    "	fragColor = vec4(float((word >> shift) & mask) / 255.0, 0.0, 0.0, 0.0);\n"
    "}\n"
    "#endif\n";

// The native layer program, "nr". It becomes a program only with
// --native-preview (native_renderer.c, NativeRenderer_InitNativeLayer); without
// the switch the build still compiles it into a module, and nothing creates it.
//
// Float vertices in model space and one matrix for all of it. The matrix is the
// whole projection including the depth mapping, worked out by the caller in C -
// see the "DEPTH." block in include/platform/native_gfx.h. Nothing is snapped
// to fixed point or to the screen grid here.
//
// The block is 128 bytes std140 and is struct NativeLayerUniforms in
// native_renderer.c field for field: the matrix at 0, the tint at 64, the
// parameters at 80, the split plane at 96 and the split mode at 112. Both
// stages declare it the same way.
//
// The fragment stage is a placeholder: vertex colour times tint, times the
// texture in slot 0 when params.x says so. It has no second colour output, so
// this program must never draw with a blend mode that needs one.
//
// params.y > 0.5: the texture is NATIVE_GFX_TEXFMT_RGBA8_SRGB (a texture of the
// native texture manager, platform/native_tex.c). Its sample arrives in linear
// light and goes straight back to gamma with the sRGB encoding, so the
// modulation after it happens in gamma as before - the main target and the
// swapchain are UNORM (renderer plan C.6.2). For an 8-bit texel and NEAREST the
// round trip is exact: decode, this encoding and the UNORM store give the
// stored byte again for all 256 values, with almost half a step to spare
// (checked in float32 by --native-tex-selftest). params.y = 0, the value every
// existing caller writes, takes the other branch - the old statement, unchanged.
//
// params.w > 0: a masked material (step 4c) - a fragment whose alpha is below it
// is discarded. params.z > 0: --native-depth-tint - the colour becomes (1, G, 1)
// with G = params.z x depth = 1024 / w for params.z = 1024 / zNear, so a jump of
// the depth shows as a jump of G. Both 0, as every draw writes without them:
// neither branch is taken and the colour is the one of before.
//
// THE SPLIT (step 4e, the water line of a custom character): the queue cuts a
// model at a plane and draws the two sides into two OT ranges. A native view
// of such a model is two draws of the same mesh, one per side, each keeping
// only its own side. The plane is in model units: d = nrSplit.w -
// dot(nrSplit.xyz, position), the retail splitDist (splitLine minus the height
// of the point in the queue's split space), interpolated with perspective -
// the queue cuts in 3D and projects afterwards, so this is the same line.
// nrSplitMode.x > 0.5 keeps d >= 0 (below the plane), < -0.5 keeps d < 0
// (above); 0, as every other draw writes, keeps everything. d = 0 goes to the
// side below, as in the queue, so no pixel of the line is drawn twice or not
// at all. The texture is read before the cut, so its level of detail at the
// line comes from derivatives every fragment of the quad has.
global_variable const char *ctr_native_layer_shader =
    "#ifdef VERTEX\n"
    "layout(location = 0) in vec3 a_position;\n"
    "layout(location = 1) in vec2 a_texcoord;\n"
    "layout(location = 2) in vec4 a_color;\n"
    "VARYING_LOC0 out vec2 v_uv;\n"
    "VARYING_LOC1 out vec4 v_color;\n"
    "VARYING_LOC2 out float v_split;\n"
    "UBO_QUALIFIER uniform NrBlock {\n"
    "	mat4 nrClipFromModel;\n"
    "	vec4 nrTint;\n"
    "	vec4 nrParams;\n"
    "	vec4 nrSplit;\n"
    "	vec4 nrSplitMode;\n"
    "};\n"
    "void main() {\n"
    "	v_uv = a_texcoord;\n"
    "	v_color = a_color * nrTint;\n"
    "	v_split = nrSplit.w - dot(nrSplit.xyz, a_position);\n"
    "	gl_Position = nrClipFromModel * vec4(a_position, 1.0);\n"
    "}\n"
    "#endif\n"
    "#ifdef FRAGMENT\n"
    "VARYING_LOC0 in vec2 v_uv;\n"
    "VARYING_LOC1 in vec4 v_color;\n"
    "VARYING_LOC2 in float v_split;\n"
    "SAMPLER_SLOT0 uniform sampler2D s_texture;\n"
    "UBO_QUALIFIER uniform NrBlock {\n"
    "	mat4 nrClipFromModel;\n"
    "	vec4 nrTint;\n"
    "	vec4 nrParams;\n"
    "	vec4 nrSplit;\n"
    "	vec4 nrSplitMode;\n"
    "};\n"
    "void main() {\n"
    "	vec4 colour = v_color;\n"
    "	if (nrParams.x > 0.5) {\n"
    "		if (nrParams.y > 0.5) {\n"
    "			vec4 texel = texture(s_texture, v_uv);\n"
    "			vec3 encoded = mix(1.055 * pow(texel.rgb, vec3(1.0 / 2.4)) - 0.055, texel.rgb * 12.92, lessThan(texel.rgb, vec3(0.0031308)));\n"
    "			colour *= vec4(encoded, texel.a);\n"
    "		} else { colour *= texture(s_texture, v_uv); }\n"
    "	}\n"
    "	if ((nrSplitMode.x > 0.5) && (v_split < 0.0)) { discard; }\n"
    "	if ((nrSplitMode.x < -0.5) && (v_split >= 0.0)) { discard; }\n"
    "	if ((nrParams.w > 0.0) && (colour.a < nrParams.w)) { discard; }\n"
    "	if (nrParams.z > 0.0) { colour = vec4(1.0, clamp(nrParams.z * gl_FragCoord.z, 0.0, 1.0), 1.0, 1.0); }\n"
    "	fragColor = colour;\n"
    "}\n"
    "#endif\n";

// The program of the retail twin, "nrt" (step 4d, Z1: the retail look V1). It
// becomes a program only with --native-preview --native-twin
// (native_renderer.c); every other native draw keeps "nr", so the probe and
// the custom characters are untouched by it.
//
// The retail look of one twin triangle, from what the PSX path does with the
// same packet:
//   tint        the vertex colour toned toward the far colour F by a =
//               alphaScale / 4096 (the DPCT of RenderBucket_LoadPrimColors, in
//               float; nrFar, nrLook.x; a = 0: no tone), per corner;
//   modulation  textured: texel x colour x nrLook.y (2 for retail data, the
//               bright factor of the PSX path); untextured: the colour x
//               248 / 255. The PSX path draws a flat polygon through the same
//               lookup as a textured one, with the white texture bound
//               (native_gpu.c, AddSplit): the word it reads there is 0xffff,
//               and the 5:5:5 LUT turns that into 31 << 3 = 248 per channel
//               (NativeRenderer_InitRG8LUT), which then multiplies the colour
//               (bright 1). Without the factor every untextured twin surface
//               came out 255 / 248 = 2.8 % too bright (the orange of the head:
//               255,49 where retail has 248,48);
//   mask        a texel with alpha below nrParams.w (the clear word 0x0000) is
//               not drawn - before the modulation;
//   dither      the PSX matrix itself (GPU_DITHERING, the same macro, not a
//               copy), weight nrLook.z, on the PSX pixel grid: v_ditherCoord is
//               the PSX pixel the fragment lies in, from the clip position
//               through the inverse of the PSX projection (nrProj). The clip
//               position already carries the -0.5 px of the twin's geometry,
//               and the fragment's own place is what the grid needs, so
//               nothing is added (nrDither.x stays 0);
//   affine      UV, colour and the dither coordinate interpolated without
//               perspective (noperspective), as the PSX path draws with w = 1;
//   UV fudge    nrDither.zw added to the texture coordinate: the 0.00025
//               texels the PSX path adds to its page origin (c_UVFudge), in
//               units of this texture.
// nrParams.z > 0: --native-depth-tint, as in "nr".
// nrSplit, nrSplitMode: the water line of step 4e, as in "nr"; the split
// distance is interpolated with perspective here too (the queue cuts in 3D).
// The block is struct NativeTwinUniforms in native_renderer.c field for field,
// 192 bytes std140.
global_variable const char *ctr_native_twin_shader =
    "#ifdef VERTEX\n"
    "layout(location = 0) in vec3 a_position;\n"
    "layout(location = 1) in vec2 a_texcoord;\n"
    "layout(location = 2) in vec4 a_color;\n"
    "VARYING_LOC0 noperspective out vec2 v_uv;\n"
    "VARYING_LOC1 noperspective out vec4 v_color;\n"
    "VARYING_LOC2 noperspective out vec2 v_ditherCoord;\n"
    "VARYING_LOC3 out float v_split;\n"
    "UBO_QUALIFIER uniform NrtBlock {\n"
    "	mat4 nrClipFromModel;\n"
    "	vec4 nrTint;\n"
    "	vec4 nrParams;\n"
    "	vec4 nrFar;\n"
    "	vec4 nrLook;\n"
    "	vec4 nrDither;\n"
    "	vec4 nrProj;\n"
    "	vec4 nrSplit;\n"
    "	vec4 nrSplitMode;\n"
    "};\n"
    "void main() {\n"
    "	v_split = nrSplit.w - dot(nrSplit.xyz, a_position);\n"
    "	vec4 c = a_color * nrTint;\n"
    "	c.rgb = c.rgb + nrLook.x * (nrFar.rgb - c.rgb);\n"
    "	v_color = c;\n"
    "	v_uv = a_texcoord + nrDither.zw;\n"
    "	gl_Position = nrClipFromModel * vec4(a_position, 1.0);\n"
    "	vec2 ndc = gl_Position.xy / gl_Position.w;\n"
    "	v_ditherCoord = vec2((ndc.x - nrProj.y) / nrProj.x, (ndc.y - nrProj.w) / nrProj.z);\n"
    "}\n"
    "#endif\n"
    "#ifdef FRAGMENT\n"
    "VARYING_LOC0 noperspective in vec2 v_uv;\n"
    "VARYING_LOC1 noperspective in vec4 v_color;\n"
    "VARYING_LOC2 noperspective in vec2 v_ditherCoord;\n"
    "VARYING_LOC3 in float v_split;\n"
    "SAMPLER_SLOT0 uniform sampler2D s_texture;\n"
    "UBO_QUALIFIER uniform NrtBlock {\n"
    "	mat4 nrClipFromModel;\n"
    "	vec4 nrTint;\n"
    "	vec4 nrParams;\n"
    "	vec4 nrFar;\n"
    "	vec4 nrLook;\n"
    "	vec4 nrDither;\n"
    "	vec4 nrProj;\n"
    "	vec4 nrSplit;\n"
    "	vec4 nrSplitMode;\n"
    "};\n"
    "vec4 v_texcoord;\n"
    "float psxDitherAmount;\n"
    GPU_DITHERING
    "void main() {\n"
    "	if ((nrSplitMode.x > 0.5) && (v_split < 0.0)) { discard; }\n"
    "	if ((nrSplitMode.x < -0.5) && (v_split >= 0.0)) { discard; }\n"
    "	vec4 colour = v_color;\n"
    "	if (nrParams.x > 0.5) {\n"
    "		vec4 texel = texture(s_texture, v_uv);\n"
    "		if ((nrParams.w > 0.0) && (texel.a < nrParams.w)) { discard; }\n"
    "		colour = vec4(texel.rgb * v_color.rgb * nrLook.y, texel.a * v_color.a);\n"
    "	} else {\n"
    "		if ((nrParams.w > 0.0) && (colour.a < nrParams.w)) { discard; }\n"
    "		colour.rgb *= 248.0 / 255.0;\n"
    "	}\n"
    "	v_texcoord = vec4(0.0, 0.0, 0.0, 1.0);\n"
    "	psxDitherAmount = nrLook.z;\n"
    "	colour = dither(colour);\n"
    "	if (nrParams.z > 0.0) { colour = vec4(1.0, clamp(nrParams.z * gl_FragCoord.z, 0.0, 1.0), 1.0, 1.0); }\n"
    "	fragColor = colour;\n"
    "}\n"
    "#endif\n";

// The engine program, "ne" (platform/engine_programs.c creates it; the native
// engine, include/platform/engine.h section PROGRAMS). Its source lives here so
// that tools/shader_spirv_probe.c emits ne.vert / ne.frag with the others and the
// ctest shader_spirv_dialect compiles it. The block "NeBlock" is struct
// EngineUniforms field for field (192 bytes std140, ENGINE_UNIFORM_BLOCK_GLSL).
//
//
// texSource (params.x), the three ways to colour a fragment:
//   0  the vertex colour times the tint, nothing else.
//   1  the native texture of slot 0 times it. params.y > 0.5 marks an sRGB
//      texture (NATIVE_GFX_TEXFMT_RGBA8_SRGB): its sample arrives in linear
//      light and goes straight back to gamma with the sRGB encoding, so the
//      modulation happens in gamma as in "nr" (the main target and the
//      swapchain are UNORM). The statement is the one of "nr", character for
//      character.
//   2  the PSX VRAM, below.
//   3  the PSX VRAM through the track mosaic table (slot 3, platform/
//      engine_track_mosaic.c, which explains the row and the vertex): v_uv
//      carries the face's row and its own position (s, t); the cell under
//      (s, t) gives the UVs, the tpage and the clut, and the texel is then
//      read as in 2. With params2.w 1 a cell texel smaller than a pixel gives
//      way to the group's level 2, then 1, then 0 (the screen derivative of
//      the world position over the face size the vertex alpha carries, like
//      mip levels; not the PS1's distance thresholds).
//
// VRAM (texSource 2). vramPage.xy is the page origin in VRAM words, vramPage.z
// the depth (0 = 4 bit, 1 = 8 bit, 2 = 15 bit), vramClut.xy the CLUT origin in
// words. v_uv is in TEXELS of the page, 0..255, as a PS1 primitive carries
// them, and wraps inside the 256 x 256 page. The texel is found like this:
//   4 bit   word (page.x + u / 4, page.y + v), nibble u mod 4 is the index
//   8 bit   word (page.x + u / 2, page.y + v), byte u mod 2 is the index
//   15 bit  word (page.x + u, page.y + v) is the colour
// and for 4 and 8 bit the colour word is (clut.x + index, clut.y). This is the
// arithmetic of GPU_PAGE_COLUMN_4BIT / _8BIT and of the 15-bit route in
// native_shaders.inc (GPU_SAMPLE_TEXTURE_FUNC) without the page store: that
// store holds one index per pixel and is a copy of what these words say. Word
// x and y wrap on the VRAM (1024 x 512), as the PSX program's VRAM sampler
// (REPEAT) does for the last page.
//
// THE UV BOX. Before that arithmetic floor(v_uv) is clamped to the triangle's
// box, the flat v_uvBox (EngineVertex.uvBox, include/platform/engine.h): the
// PS1 never reads a texel outside the box of a primitive's corners, while the
// rounding of the interpolation here reaches one texel before the smallest
// corner and one past the largest (255 wrapping to 0 of the page) - a foreign
// texel or the transparent word along the border of a magnified small box.
// The box arrives as (umin, vmin, 255 - umax, 255 - vmax), so a vertex without
// one (all zero) clamps to the whole page, which changes nothing.
// The mosaic table (texSource 3) clamps to its cell instead and ignores the box.
//
// FIVE-FIVE-FIVE. A word is the two bytes of the RG8 texel: .r the low byte,
// .g the high byte. Bits 0-4 red, 5-9 green, 10-14 blue, bit 15 the STP bit.
// The PSX program turns the word into a colour through the RG8 lookup table
// (NativeRenderer_InitRG8LUT): channel = (value & 31) << 3, so 31 becomes 248,
// not 255. This shader does the same shift directly instead of a table fetch,
// which gives the same 8-bit values and keeps the retail look, including the
// 248 that the bright factor then doubles.
//
// THE MASK. The word 0x0000 (after the CLUT for indexed pages) is transparent
// and discarded, as texelVisible() in GPU_STP_PASS_FUNC does (rg.x + rg.y > 0).
// The STP bit then selects by stpPass (params.w): 1 keeps only texels WITHOUT
// STP, 2 only texels WITH STP, 0 keeps all - the PSX program's
// discardForSemiTransPass for its passes 1 and 2.
//
// ALPHA. The PSX program multiplies the colour by the vertex colour and then
// OVERWRITES alpha with its mask bit (1 when the draw sets the mask or an STP
// texel is written with output STP, else 0); it never carries a texel alpha.
// An engine draw has no mask bit to write, so a VRAM texel is opaque: alpha is
// the vertex alpha times the tint alpha (1.0 for every ordinary draw), and
// alphaCutoff (params.z) applies to that alpha like on every other path.
//
// MODULATION. colour = texel * vertex colour * params2.x. 2.0 is the PSX bright
// factor of retail data (128 is the neutral colour on the console), 1.0 native
// data - the nrLook.y of "nrt". The result is not clamped here; the UNORM
// target clamps.
// The uniform block of every engine program ("ne" here, "nw" in
// engine_water_shader.inc), both stages: struct EngineUniforms field for field
// (include/platform/engine.h, 192 bytes std140: a mat4, then eight vec4). One
// text, so the programs cannot drift apart.
#define ENGINE_UNIFORM_BLOCK_GLSL \
	"UBO_QUALIFIER uniform NeBlock {\n" \
	"	mat4 clipFromModel;\n" \
	"	vec4 tint;\n" \
	"	vec4 params;\n" \
	"	vec4 params2;\n" \
	"	vec4 vramPage;\n" \
	"	vec4 vramClut;\n" \
	"	vec4 split;\n" \
	"	vec4 params3;\n" \
	"	vec4 params4;\n" \
	"};\n"

global_variable const char *ctr_native_engine_shader =
    "#ifdef VERTEX\n"
    "layout(location = 0) in vec3 a_position;\n"
    "layout(location = 1) in vec2 a_texcoord;\n"
    "layout(location = 2) in vec4 a_color;\n"
    // The UV box of the triangle (EngineVertex.uvBox: u8 x 4 as floats, umin, vmin, 255 - umax, 255 - vmax in page texels).
    "layout(location = 3) in vec4 a_uvBox;\n"
    "VARYING_LOC0 out vec2 v_uv;\n"
    "VARYING_LOC1 out vec4 v_color;\n"
    "VARYING_LOC2 out float v_split;\n"
    "VARYING_LOC3 out vec3 v_world;\n"
    "VARYING_LOC4 flat out vec4 v_uvBox;\n"
    ENGINE_UNIFORM_BLOCK_GLSL
    "void main() {\n"
    "	v_uv = a_texcoord;\n"
    "	v_uvBox = a_uvBox;\n"
    "	v_color = a_color * tint;\n"
    // Model units (the track's are world units): the table mode's pixel size (faceFootprint).
    "	v_world = a_position;\n"
    // The water line, in model units before the matrix: the "nr" rule (nrSplit).
    "	v_split = split.w - dot(split.xyz, a_position);\n"
    "	gl_Position = clipFromModel * vec4(a_position, 1.0);\n"
    // The material's depth offset (params2.z, 0 = none): clip z times (1 + offset). With the reverse Z of the
    // engine a positive value pulls the vertex nearer, a negative one farther; it layers coplanar track decals.
    "	gl_Position.z *= (1.0 + params2.z);\n"
    // A track decal's lift (params3.z world units, 0 = none; platform/engine_track.c DECALS): its depth is that of
    // its plane moved the lift toward the camera. params4 = (plane normal, normal . camera), so h is the camera's
    // distance to the plane; scaling the view vector by s = (h - lift) / h lands on the moved plane with the same
    // screen point, and with clip z = zNear the depth is z / s. s stays >= 0.5 when the camera is near the plane.
    "	if (params3.z > 0.0) {\n"
    "		float h = abs(params4.w - dot(params4.xyz, a_position));\n"
    "		if (h > 0.0) { gl_Position.z /= max((h - params3.z) / h, 0.5); }\n"
    "	}\n"
    "}\n"
    "#endif\n"
    "#ifdef FRAGMENT\n"
    // ANTI-ALIASING, as the PSX program does it (gpu_shader_common, PSX_EDGE_INTERP): with MSAA the fragment of an
    // edge pixel is evaluated at the pixel centre, also when that lies outside the triangle, and the UV there
    // reaches beyond the face - a foreign texel, or the transparent word 0x0000, which discards the samples the
    // face covers in that pixel and lets what is behind show through a crack along the edge. Centroid keeps the
    // UV inside the covered part; with one sample it is the centre as before. The fragment stage's decoration is
    // the one that counts (tools/shader_spirv_probe.c). v_world stays at the centre: its derivatives pick the level.
    "VARYING_LOC0 centroid in vec2 v_uv;\n"
    "VARYING_LOC1 in vec4 v_color;\n"
    "VARYING_LOC2 in float v_split;\n"
    "VARYING_LOC3 in vec3 v_world;\n"
    "VARYING_LOC4 flat in vec4 v_uvBox;\n"
    "SAMPLER_SLOT0 uniform sampler2D s_texture;\n"
    "SAMPLER_SLOT1 uniform sampler2D s_vram;\n"
    "SAMPLER_SLOT3 uniform sampler2D s_table;\n"
    ENGINE_UNIFORM_BLOCK_GLSL
    // One VRAM word: the two bytes of the RG8 texel, low byte first. Wrapped on the VRAM.
    "int vramWord(ivec2 p) {\n"
    "	ivec2 w = ivec2(p.x & 1023, p.y & 511);\n"
    "	ivec2 b = ivec2(texelFetch(s_vram, w, 0).rg * 255.0 + 0.5);\n"
    "	return b.x | (b.y << 8);\n"
    "}\n"
    // The colour word of texel t of a page (t wraps inside the 256 x 256 page).
    "int pageWord(ivec2 t, ivec2 page, int depth, ivec2 clut) {\n"
    "	t = t & 255;\n"
    "	if (depth == 0) {\n"
    "		int index = (vramWord(page + ivec2(t.x >> 2, t.y)) >> ((t.x & 3) * 4)) & 15;\n"
    "		return vramWord(clut + ivec2(index, 0));\n"
    "	}\n"
    "	if (depth == 1) {\n"
    "		int index = (vramWord(page + ivec2(t.x >> 1, t.y)) >> ((t.x & 1) * 8)) & 255;\n"
    "		return vramWord(clut + ivec2(index, 0));\n"
    "	}\n"
    "	return vramWord(page + t);\n"
    "}\n"
    // A colour word as the fragment: 0x0000 is transparent, the STP passes, 5:5:5
    // as the RG8 lookup table does it: (v & 31) << 3.
    "vec4 wordColour(int word) {\n"
    "	if (word == 0) { discard; }\n"
    "	bool stp = (word & 0x8000) != 0;\n"
    "	int pass = int(params.w + 0.5);\n"
    "	if ((pass == 1) && stp) { discard; }\n"
    "	if ((pass == 2) && !stp) { discard; }\n"
    "	vec3 texel = vec3(float((word & 31) << 3), float(((word >> 5) & 31) << 3), float(((word >> 10) & 31) << 3)) / 255.0;\n"
    "	return vec4(texel * v_color.rgb * params2.x, v_color.a);\n"
    "}\n"
    // One texel of the mosaic table, its four bytes.
    "ivec4 tableTexel(int row, int x) {\n"
    "	return ivec4(texelFetch(s_table, ivec2(x, row), 0) * 255.0 + 0.5);\n"
    "}\n"
    // The UV box of a layout (its three texels) in texels, per axis.
    "vec2 layoutSize(ivec4 a, ivec4 b, ivec4 c) {\n"
    "	vec2 lo = vec2(min(min(a.x, b.x), min(c.x, c.z)), min(min(a.y, b.y), min(c.y, c.w)));\n"
    "	vec2 hi = vec2(max(max(a.x, b.x), max(c.x, c.z)), max(max(a.y, b.y), max(c.y, c.w)));\n"
    "	return hi - lo + 1.0;\n"
    "}\n"
    // How much of the face one pixel covers: the world size of the pixel over the
    // face's size. The world position is continuous across faces, (s, t) is not
    // (it starts again at every border, where its derivative would send the
    // border pixels to the coarsest level). The face size comes in the vertex
    // alpha as 16 * log2 of the side length (engine_track_mosaic.c,
    // EngMosaic_FaceSizeCode; the same on all vertices of a face). The SHORTER
    // of the two screen directions counts, as an anisotropic filter would take
    // it: a face seen at a grazing angle near the camera is long in one
    // direction only, and the longer one would send it to a coarse level
    // where the PS1 (by depth) still draws its finest. Capped at 4 to 1 like an
    // anisotropic filter: at the flattest angles the longer direction counts
    // again (divided by 4), or the finest tiles sparkle along the road.
    "vec2 faceFootprint() {\n"
    "	float minor = min(length(dFdx(v_world)), length(dFdy(v_world)));\n"
    "	float major = max(length(dFdx(v_world)), length(dFdy(v_world)));\n"
    "	float world = max(minor, major * 0.25);\n"
    "	float size = exp2(floor(v_color.a / max(tint.a, 1e-6) * 255.0 + 0.5) / 16.0);\n"
    "	return vec2(world / size);\n"
    "}\n"
    "void main() {\n"
    // params2.y: 1 keeps v_split >= 0 (below the plane), -1 keeps v_split < 0, 0 keeps all.
    "	if ((params2.y > 0.5) && (v_split < 0.0)) { discard; }\n"
    "	if ((params2.y < -0.5) && (v_split >= 0.0)) { discard; }\n"
    "	vec4 colour = v_color;\n"
    "	if (params.x > 2.5) {\n"
    // The face's row and its own position (s, t): texcoord = (s + 2 + 4a, t + 2 + 4b), row = a + 64 b.
    "		vec2 ab = floor(v_uv * 0.25);\n"
    "		int row = int(ab.x) + 64 * int(ab.y);\n"
    "		vec2 st = v_uv - 4.0 * ab - 2.0;\n"
    // Before any discard: the derivatives need the whole 2 x 2 quad.
    "		vec2 face = faceFootprint();\n"
    "		int side = max(tableTexel(row, 0).r, 1);\n"
    "		vec2 g = st * float(side);\n"
    "		ivec2 cr = clamp(ivec2(floor(g)), ivec2(0), ivec2(side - 1));\n"
    // Inside the cell, also for a centroid a rounding step off the face's border: never a texel beyond the tile.
    "		vec2 f = clamp(g - vec2(cr), 0.0, 1.0);\n"
    "		int at = 1 + 3 * (4 * cr.y + cr.x);\n"
    "		ivec4 a = tableTexel(row, at);\n"
    "		ivec4 b = tableTexel(row, at + 1);\n"
    "		ivec4 c = tableTexel(row, at + 2);\n"
    // params2.w 1: a cell texel smaller than a pixel gives way to level 2, then 1, then 0 (texels 49, 52, 55).
    "		if (params2.w > 0.5) {\n"
    "			vec2 size = layoutSize(a, b, c) * float(side);\n"
    "			float perPixel = max(face.x * size.x, face.y * size.y);\n"
    "			for (int k = 0; (k < 3) && (perPixel > 1.0); k++) {\n"
    "				at = 49 + 3 * k;\n"
    "				a = tableTexel(row, at);\n"
    "				b = tableTexel(row, at + 1);\n"
    "				c = tableTexel(row, at + 2);\n"
    "				f = clamp(st, 0.0, 1.0);\n"
    "				size = layoutSize(a, b, c);\n"
    "				perPixel = max(face.x * size.x, face.y * size.y);\n"
    "			}\n"
    "		}\n"
    // The UV inside the cell: bilinear over its corners u0v0 (0, 0), u1v1 (1, 0), u2v2 (0, 1), u3v3 (1, 1).
    "		vec2 uv = mix(mix(vec2(a.xy), vec2(b.xy), f.x), mix(vec2(c.xy), vec2(c.zw), f.x), f.y);\n"
    "		int tpage = b.z | (b.w << 8);\n"
    "		int clut = a.z | (a.w << 8);\n"
    "		colour = wordColour(pageWord(ivec2(floor(uv)), ivec2((tpage & 15) * 64, ((tpage >> 4) & 1) * 256), (tpage >> 7) & 3,\n"
    "		                             ivec2((clut & 63) * 16, clut >> 6)));\n"
    // The vertex alpha held the face size: a table face is as opaque as any VRAM texel (the tint's alpha only).
    "		colour.a = tint.a;\n"
    "	} else if (params.x > 1.5) {\n"
    // Never a texel outside the triangle's UV box (EngineVertex.uvBox): the rounding of the interpolation reaches
    // one texel before the smallest corner, and one past the largest (255 + e wraps to texel 0 of the page).
    // The maxima arrive as 255 - max: a zeroed box is the whole page (no clamp).
    "		ivec2 texel = clamp(ivec2(floor(v_uv)), ivec2(v_uvBox.xy + 0.5), ivec2(255.5 - v_uvBox.zw));\n"
    "		colour = wordColour(pageWord(texel, ivec2(vramPage.xy + 0.5), int(vramPage.z + 0.5), ivec2(vramClut.xy + 0.5)));\n"
    "	} else if (params.x > 0.5) {\n"
    "		if (params.y > 0.5) {\n"
    "			vec4 texel = texture(s_texture, v_uv);\n"
    "			vec3 encoded = mix(1.055 * pow(texel.rgb, vec3(1.0 / 2.4)) - 0.055, texel.rgb * 12.92, lessThan(texel.rgb, vec3(0.0031308)));\n"
    "			colour *= vec4(encoded, texel.a);\n"
    "		} else { colour *= texture(s_texture, v_uv); }\n"
    "	}\n"
    "	if ((params.z > 0.0) && (colour.a < params.z)) { discard; }\n"
    "	fragColor = colour;\n"
    "}\n"
    "#endif\n";

// The water program "nw" (platform/engine_water.c draws with it): its own file.
#include "engine_water_shader.inc"

// The blit shaders by name, in one place.
//
// Two readers: native_renderer.c, to find the prebuilt SPIR-V module that goes
// with a body, and tools/shader_spirv_probe.c, to decide which modules to build.
//
// It is one list because it was two. A shader added to only the renderer's copy
// compiled, linked, ran correctly under OpenGL and passed the SPIR-V dialect
// test - the test builds what the probe writes, and the probe had never heard of
// it - and then under Vulkan fell back to a name with no module behind it and
// dropped its draw every frame. The log said so once at startup and once per
// frame afterwards, which is exactly as much use as it sounds. A single list
// cannot be half-updated.
//
// The PSX shaders are not in here: they are assembled from several parts and
// built in three interpolation variants each, so their table lives with the
// assembly (tools/shader_spirv_probe.c, NativeRenderer_Shader_CompileWithUniforms).
//
// A function rather than a table, for two reasons. The bodies are pointer
// variables and not compile-time constants, so a table of them will not
// initialise at file scope in C. And a function that answers "is there an entry
// number i" carries no count beside it - the loop ends when the list does, so
// there is no second number that can go out of step with the first.
internal int CtrBlitShader_Get(int index, const char **outName, const char **outBody)
{
	const char *name = NULL;
	const char *body = NULL;

	switch (index)
	{
	case 0:
		name = "pack";
		body = ctr_pack_shader;
		break;
	case 1:
		name = "present_vram";
		body = ctr_present_vram_shader;
		break;
	case 2:
		name = "present_target";
		body = ctr_present_target_shader;
		break;
	case 3:
		name = "pack_pages";
		body = ctr_pack_pages_shader;
		break;
	case 4:
		name = "nr";
		body = ctr_native_layer_shader;
		break;
	case 5:
		name = "nrt";
		body = ctr_native_twin_shader;
		break;
	case 6:
		name = "ne";
		body = ctr_native_engine_shader;
		break;
	case 7:
		name = "nw";
		body = ctr_native_water_shader;
		break;
	default:
		return 0;
	}

	if (outName != NULL)
	{
		*outName = name;
	}
	if (outBody != NULL)
	{
		*outBody = body;
	}

	return 1;
}

// The build this renderer was ported from also has a present-pass shader (CRT
// filter, scanline mask) here. That is presentation, not rendering, and not
// part of this port - the base presents its render target without a shader pass.


