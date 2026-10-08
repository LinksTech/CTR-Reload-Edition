#ifndef PLATFORM_NATIVE_CHAR_GPU_H
#define PLATFORM_NATIVE_CHAR_GPU_H

// THE GPU SET OF A CUSTOM CHARACTER (renderer plan C.5, step 4c;
// platform/native_char_gpu.c).
//
// The native part of a bound file (CNET, CTXT; NativeChar_SeatNative, held only
// with --native-preview) made into device objects once per race load, in load
// stage 5 right after the seats are armed (game/LOAD/LOAD_TenStages.c) - a
// loading screen, never a race frame:
//   body     every pose of the file (max(1, poseCount) x N vertices of the "nr"
//            layout: position, texture coordinate, colour) in one static
//            vertex buffer, filled in pieces of at most 1 MiB; a draw picks a
//            pose by its vertex offset (pose x N). No morph on the CPU, no
//            buffer written in a frame.
//   indices  u16, the triangles sorted by material: first the opaque
//            materials in ascending order, then the masked ones; one range
//            {firstIndex, count} per material that has triangles.
//   wheels   the WHLS mesh (model units, centre at the origin, axle along X,
//            outside +X) in a static buffer of its own, white vertices. An
//            author's wheel (WHLS version 2) holds it twice: the mesh as it
//            is for the +X wheels, then its mirror image for the -X wheels (x
//            negated, the winding turned, the same UV - a tread runs
//            mirrored), indices from wheelMirrorFirst on. The test wheel
//            (version 1) holds it once; its -X wheels are the mesh turned.
//   textures every CTXT entry through the native texture manager
//            (platform/native_tex.c) with its levels.
// A blend material refuses the whole set: the seat draws its CMDL (renderer
// plan C.12, all or nothing). Released with the seats (NativeChar_ClearSeats,
// load stage 0 and the start of NativeChar_ArmSeats) - always at a loading
// screen as well.
//
// Without --native-preview nothing here runs: no set, no device object, no
// line.

#include <macros.h>
#include <platform/native_gfx.h>

#define NATIVE_CHAR_GPU_SETS 8
#define NATIVE_CHAR_GPU_MATERIALS 64 // RLDCHAR_NET_MATERIALS_MAX
#define NATIVE_CHAR_GPU_TEXTURES 16  // RLDCHAR_TEX_COUNT_MAX
#define NATIVE_CHAR_GPU_POSES 47     // RLDCHAR_NET_POSES: 21 + 7 + 15 + 4

enum
{
	NATIVE_CHAR_GPU_NONE = 0,
	NATIVE_CHAR_GPU_READY = 1,
	NATIVE_CHAR_GPU_REFUSED = 2,
};

struct RldCharNative;
struct NativeProbeVertex;
struct Model;

// One material range of the sorted index buffer.
struct NativeCharRange
{
	u32 firstIndex;
	u32 indexCount;
	u16 material;
	u8 mask; // 1 = alpha mode MASK: drawn with a discard at alpha 0.5
};

struct NativeCharGpu
{
	const struct RldCharNative *key;
	int state; // NATIVE_CHAR_GPU_*
	char why[96];
	int seat;  // the first seat it was made for

	NativeGfxBuffer bodyVB;
	NativeGfxBuffer bodyIB;
	NativeGfxBuffer wheelVB;
	NativeGfxBuffer wheelIB;

	u32 vertexCount;   // N
	u32 poseCount;     // max(1, the CNET pose count)
	u32 netPoseCount;  // the CNET pose count: 0 or 47
	u32 triangleCount;
	u32 rangeCount;
	struct NativeCharRange range[NATIVE_CHAR_GPU_MATERIALS];

	int hasWheels;
	u8 wheelOwn;           // 1 = an author's wheel (WHLS version 2), 0 = the test wheel or none
	u32 wheelVertexCount;  // in the buffer: Nw, or 2 Nw for an author's wheel
	u32 wheelIndexCount;   // of one mesh: 3 Tw
	u32 wheelIndexTotal;   // in the buffer: 3 Tw, or 6 Tw for an author's wheel
	u32 wheelMirrorFirst;  // the first index of the mirrored mesh (an author's wheel), else 0
	u16 wheelMaterial;
	float wheelRadius;
	float wheelHalfWidth;
	float wheelFront[3];
	float wheelRear[3];

	u32 textureCount;
	TextureID texture[NATIVE_CHAR_GPU_TEXTURES];
	u8 textureSrgb[NATIVE_CHAR_GPU_TEXTURES];

	u32 materialCount;
	float materialTint[NATIVE_CHAR_GPU_MATERIALS][4];
	s16 materialTexture[NATIVE_CHAR_GPU_MATERIALS];
	u8 materialMask[NATIVE_CHAR_GPU_MATERIALS];

	float hullMin[3]; // model units, from the CNET head
	float hullMax[3];

	u32 vertexBytes;
	u32 indexBytes;
	u32 textureBytes; // with every level
};

// The CPU half of a set (no device): what NativeCharGpu_Build works out from a
// checked native part. The vertices of a pose are made on demand
// (NativeCharGpu_PoseVertices), never all at once.
struct NativeCharGpuCpu
{
	char why[96];      // refused: English
	u32 vertexCount;
	u32 poseCount;     // max(1, P)
	u32 netPoseCount;  // P
	u32 triangleCount;
	u16 *indices;      // triangleCount x 3, in range order (owned)
	u32 *triangleOrder; // triangleCount: the CNET triangle at each sorted place (owned)
	u32 rangeCount;
	struct NativeCharRange range[NATIVE_CHAR_GPU_MATERIALS];
	u32 materialCount;
	float materialTint[NATIVE_CHAR_GPU_MATERIALS][4];
	s16 materialTexture[NATIVE_CHAR_GPU_MATERIALS];
	u8 materialMask[NATIVE_CHAR_GPU_MATERIALS];
	u8 textureNearest[NATIVE_CHAR_GPU_TEXTURES]; // a material with the nearest flag uses it, or CTXT says so
	u8 textureNearestMixed[NATIVE_CHAR_GPU_TEXTURES]; // used with and without the material flag
	int hasWheels;
	u8 wheelOwn;
	u32 wheelVertexCount;
	u32 wheelIndexCount;
	u32 wheelIndexTotal;
	u32 wheelMirrorFirst;
	struct NativeProbeVertex *wheelVertices; // owned
	u16 *wheelIndices;                       // owned
	u16 wheelMaterial;
	float wheelRadius;
	float wheelHalfWidth;
	float wheelFront[3];
	float wheelRear[3];
	float hullMin[3];
	float hullMax[3];
};

// Load stage 5, right after NativeChar_ArmSeats: a set for every seat whose
// native part is held (seats that share a part share its set). Returns at once
// without --native-preview or without a held part. One line per seat.
void NativeCharGpu_LoadSeats(int levelID);

// Every set let go (NativeChar_ClearSeats, before the native parts are freed);
// the preview's set with them.
void NativeCharGpu_ReleaseAll(void);

// STEP 5A, the set of the driver select preview: one place of its own beside
// the eight of the seats. LoadPreview at a change of the wanted tile (from the
// pull, in the menu, never in a race frame): the set of the part
// NativeChar_PreviewNative(entry) holds, with one line "preview: ... took %u
// ms" - the time since started (SDL_GetPerformanceCounter before the read of
// the part; 0 = from here). ReleasePreview lets it go (its buffers wait as at
// a load). ForPreview: the ready set of entry, else NULL.
void NativeCharGpu_LoadPreview(int entry, u64 started);
void NativeCharGpu_ReleasePreview(void);
const struct NativeCharGpu *NativeCharGpu_ForPreview(int entry);

// STEP 4D, the retail twin (--native-twin, only with --dev and
// --native-preview): a place of its own beside the seats and the preview.
// LoadTwin in load stage 5 after LoadSeats (game/LOAD/LOAD_TenStages.c): seat
// 0's retail model through platform/native_twin.c and its pages from the VRAM
// mirror, uploaded with NativeTwin_TextureFlags. ReleaseTwin with the seats.
// ForTwin: the ready set, TwinSource: its source (pose table), TwinModel: the
// retail model it was made from - NULL while there is none.
struct NativeTwinSource;
extern int g_cfg_nativeTwin;
void NativeCharGpu_LoadTwin(int levelID);
void NativeCharGpu_ReleaseTwin(void);
const struct NativeCharGpu *NativeCharGpu_ForTwin(void);
const struct NativeTwinSource *NativeCharGpu_TwinSource(void);
const struct Model *NativeCharGpu_TwinModel(void);

// The ready set of a seat whose native part is still the one it was made from,
// else NULL.
const struct NativeCharGpu *NativeCharGpu_ForSeat(int seat);

// The pose of a set for an animation frame: base[anim] + frame with base
// {0, 21, 28, 43}; 0 for a still set (poseCount 0); -1 for an animation or a
// frame outside 21/7/15/4.
int NativeCharGpu_PoseIndex(u32 netPoseCount, int anim, int frame);

// The middle of wheel w (0 front +X, 1 front -X, 2 rear +X, 3 rear -X, the
// order of DrawTires wheelLocal) in model units.
void NativeCharGpu_WheelMiddle(const float front[3], const float rear[3], int wheel, double out[3]);

// CPU only: 1 and *out filled, or 0 with out->why (a blend material). Free
// with NativeCharGpu_FreeCpu either way.
int NativeCharGpu_Build(const struct RldCharNative *n, struct NativeCharGpuCpu *out);
void NativeCharGpu_FreeCpu(struct NativeCharGpuCpu *cpu);

// count vertices of pose p from vertex first on, in the "nr" layout.
void NativeCharGpu_PoseVertices(const struct RldCharNative *n, u32 pose, u32 first, u32 count, struct NativeProbeVertex *out);

// Sets made, refused, released and live, bytes, textures and uploads - the
// exit line of the render layer report (only in a run that made a set).
u32 NativeCharGpu_SetsMade(void);
void NativeCharGpu_ReportLine(void);

// --native-char-gpu-selftest <folder> (main.c, ctest native_char_gpu_selftest):
// the files rldpack make-native-tests wrote, through the native read and
// NativeCharGpu_Build without a device. 0 = passed.
int NativeCharGpu_SelfTest(const char *dir);

#endif
