#ifndef NATIVE_PROBE_H
#define NATIVE_PROBE_H
#include <macros.h>

#define NATIVE_PROBE_NONE    0
#define NATIVE_PROBE_BODY    1 // --native-probe body
#define NATIVE_PROBE_TEXTURE 2 // --native-probe texture: the same body, every colour from a texture
#define NATIVE_PROBE_POSE    3 // --native-probe pose: the textured body, its top shaped by the retail animation frame
// --native-probe wheels is the form pose (g_cfg_nativeProbe = NATIVE_PROBE_POSE)
// with native wheels beside the body (g_cfg_nativeProbeWheels = 1), so every
// path of the form pose - its buffers, its texture, its pose - stays the same.
// --native-probe mips is the form pose as well (g_cfg_nativeProbeMips = 1):
// the same body and pose, but its texture is the mips texture below, made by
// the native texture manager while the race loads instead of at start-up.

// The forms whose colours come from the probe texture.
#define NATIVE_PROBE_TEXTURED(form) (((form) == NATIVE_PROBE_TEXTURE) || ((form) == NATIVE_PROBE_POSE))

// The form, only with --dev and --native-preview. Never in ctr-settings.cfg:
// the file only knows s_videoSettings and the views.
extern int g_cfg_nativeProbe;

// The form a --native-probe value names (body, texture, pose; wheels and
// mips give NATIVE_PROBE_POSE), NATIVE_PROBE_NONE for any other word.
int NativeProbe_FormFromName(const char *name);

// 1 when a --native-probe value asks for native wheels (wheels), else 0.
int NativeProbe_FormHasWheels(const char *name);

// 1 when a --native-probe value asks for the mips texture (mips), else 0.
int NativeProbe_FormHasMips(const char *name);

// --native-probe wheels: set with g_cfg_nativeProbe by the early reader of
// main.c, before Platform_Init (the probe texture is made there and carries the
// wheel texel only in this form). 0 in every other run.
extern int g_cfg_nativeProbeWheels;

// --native-probe mips: set with g_cfg_nativeProbe by the early reader of
// main.c. 0 in every other run.
extern int g_cfg_nativeProbeMips;

// --native-probe-seat <n>, only together with --native-probe: the probe takes
// the model of seat n (0 to NATIVE_PROBE_SEATS - 1) instead of seat 0. A run
// value like the probe itself; 0 is the default and the probe as before.
#define NATIVE_PROBE_SEATS 8
extern int g_cfg_nativeProbeSeat;

struct NativeProbeVertex
{
	float position[3]; // unit body: x -1..1 (+X left seen from behind), y 0..1 (up), z -1..1 (+Z front)
	float texcoord[2];
	u8 color[4];       // RGBA
};

#define NATIVE_PROBE_VERTEX_COUNT 36
#define NATIVE_PROBE_INDEX_COUNT  54

const struct NativeProbeVertex *NativeProbe_Vertices(void);
const u16 *NativeProbe_Indices(void);

// The form texture: the same positions and indices, white vertex colours and
// texture coordinates into one RGBA8 texture of NATIVE_PROBE_TEXTURE_SIZE
// squared, rows from the top, 4 bytes per texel (R, G, B, A). The texels are
// written into a static buffer at the first call; without the form nothing
// calls it. In the form wheels (g_cfg_nativeProbeWheels) the one texel at
// NATIVE_PROBE_WHEEL_TEXEL, NATIVE_PROBE_WHEEL_TEXEL is white instead of green: it
// lies in the margin of the green region that no face of the body samples.
#define NATIVE_PROBE_TEXTURE_SIZE 256
const struct NativeProbeVertex *NativeProbe_TexturedVertices(void);
const u8 *NativeProbe_TexturePixels(void);

// The form pose: the textured mesh with its top lowered as a wedge that points
// in one of NATIVE_PROBE_POSE_PHASES directions; the phase is the retail
// animation frame modulo the phase count. Only the height of the top corners
// changes - no x, no z, no texture coordinate - so green stays on the +X half,
// blue on the -X half and red in front, and the body stays inside its box.
#define NATIVE_PROBE_POSE_PHASES 8
void NativeProbe_PoseVertices(int phase, struct NativeProbeVertex out[NATIVE_PROBE_VERTEX_COUNT]);

// THE WHEEL MESH of the form wheels: a ten-sided cylinder of unit radius around
// the x axis, x -1 (inner side) to +1 (outer side), in two halves that each fit
// the layout of the body - NATIVE_PROBE_VERTEX_COUNT vertices, nine quads, the
// same indices (NativeProbe_Indices) - so the renderer draws a half exactly as
// it draws a pose mesh. Half 0: tread quads 0-4 and the outer cap, half 1: tread
// quads 5-9 and the inner cap. Wheel colour 255,0,128, stripes 160,0,80 (tread
// quads 0 and 5, the first quad of either cap). Every vertex samples the one
// white texel of the wheel texture (NATIVE_PROBE_WHEEL_TEXEL), so the colour on
// screen is the vertex colour. The right-hand wheels take the same mesh turned
// by 180 degrees about y, never mirrored (platform/native_render_layer.c).
#define NATIVE_PROBE_WHEEL_HALVES 2
#define NATIVE_PROBE_WHEEL_TEXEL  126 // texel x and y of the white wheel texel
const struct NativeProbeVertex *NativeProbe_WheelVertices(int half);

// THE MIPS TEXTURE of the form mips: 256x256 with all 9 levels down to 1x1,
// every level one colour, sRGB bytes, alpha 255 - so the colour on screen says
// which level the sampler took (and a blend of two neighbours, which two):
//   0 255,64,255   1 0,255,64    2 160,64,255   3 224,32,160   4 192,0,96
//   5 64,0,255     6 0,192,96    7 0,255,192    8 160,32,192
// Chosen away from every colour of the AP0 pictures of L0 (x1 and x4, at least
// 36 apart), at least 64 apart from each other and 48 from the colours already
// in use (sides, checker, wheels). Level n is max(1, 256 >> n) squared texels,
// 4 bytes each, rows from the top. Written into static memory at the first
// call; without the form nothing calls it.
#define NATIVE_PROBE_MIPS_LEVELS 9
const u8 *NativeProbe_MipsLevel(int level);

// --native-probe-selftest (ctest native_probe_selftest): SHA-256 of the mesh
// bytes of the forms, of the texture bytes, of the pose meshes, of the wheel
// mesh, of the texture of the form wheels and of the mips texture against
// their golden values.
// 0 = passed.
int NativeProbe_SelfTest(void);
#endif
