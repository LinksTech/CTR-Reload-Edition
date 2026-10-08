#ifndef NATIVE_PROBE_H
#define NATIVE_PROBE_H
#include <macros.h>

#define NATIVE_PROBE_NONE    0
#define NATIVE_PROBE_BODY    1 // --native-probe body
#define NATIVE_PROBE_TEXTURE 2 // --native-probe texture: the same body, every colour from a texture
#define NATIVE_PROBE_POSE    3 // --native-probe pose: the textured body, its top shaped by the retail animation frame

// The forms whose colours come from the probe texture.
#define NATIVE_PROBE_TEXTURED(form) (((form) == NATIVE_PROBE_TEXTURE) || ((form) == NATIVE_PROBE_POSE))

// The form, only with --dev and --native-preview. Never in ctr-settings.cfg:
// the file only knows s_videoSettings and the views.
extern int g_cfg_nativeProbe;

// The form a --native-probe value names (body, texture, pose), NATIVE_PROBE_NONE
// for any other word.
int NativeProbe_FormFromName(const char *name);

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
// calls it.
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

// --native-probe-selftest (ctest native_probe_selftest): SHA-256 of the mesh
// bytes of the forms, of the texture bytes and of the pose meshes against
// their golden values. 0 = passed.
int NativeProbe_SelfTest(void);
#endif
