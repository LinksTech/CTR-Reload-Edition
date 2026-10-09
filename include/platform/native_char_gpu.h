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

// WIN AND LOSE (render plan B3). A seat's set whose file has the shape keys
// win or lose (CNET MRPH) holds, behind the poses of the file, K =
// NATIVE_CHAR_GPU_FINISH_STAGES poses per target, made at load: stage s is
// the neutral pose blended towards the target at weight (s + 1) / K
// (RldChar_MorphVertex), win first, then lose. After the finish of a native
// seat the drawing takes pose finishFirst[target] + stage, the stage moving on
// by one every NATIVE_CHAR_GPU_FINISH_TICKS logic ticks from the finish (16
// ticks, about half a second) and then staying at K - 1, the target itself.
// Nothing is uploaded in a race frame and nothing of the game is written.
#define NATIVE_CHAR_GPU_FINISH_STAGES 8
#define NATIVE_CHAR_GPU_FINISH_TICKS 2
#define NATIVE_CHAR_GPU_FINISH_WIN 0  // RLDCHAR_MORPH_WIN
#define NATIVE_CHAR_GPU_FINISH_LOSE 1 // RLDCHAR_MORPH_LOSE
#define NATIVE_CHAR_GPU_FINISH_TARGETS 2
#define NATIVE_CHAR_GPU_FINISH_WIN_RANKS 3 // places 1 to 3 win, 4 to 8 lose
#define NATIVE_CHAR_GPU_FINISH_RANKS 8     // the places of a race (8 drivers)

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
	u32 poseTotal;     // poses in the buffer: poseCount and the win/lose stages
	u32 finishTargets; // bit (1 << NATIVE_CHAR_GPU_FINISH_*) per target with stages; 0 = none
	u32 finishFirst[NATIVE_CHAR_GPU_FINISH_TARGETS]; // the pose of stage 0 per target
	u32 triangleCount;
	u32 rangeCount;
	struct NativeCharRange range[NATIVE_CHAR_GPU_MATERIALS];

	int hasWheels;
	u8 wheelOwn;           // 1 = an author's wheel (WHLS version 2 or 3), 0 = the test wheel or none
	u32 wheelVertexCount;  // in the buffer: Nw, or 2 Nw for an author's wheel (and 2 Nr with an own rear wheel)
	u32 wheelIndexCount;   // of one mesh: 3 Tw
	u32 wheelIndexTotal;   // in the buffer: 3 Tw, or 6 Tw for an author's wheel (and 6 Tr with an own rear wheel)
	u32 wheelMirrorFirst;  // the first index of the mirrored mesh (an author's wheel), else 0
	u16 wheelMaterial;
	float wheelRadius;
	float wheelHalfWidth;
	float wheelFront[3];
	float wheelRear[3];
	// The stroboscope of an author's wheel (render plan A3): its tread count,
	// worked out at load (NativeWheels_EstimateTreads); 0 for the test wheel
	// and none, which are drawn without a clamp.
	int wheelTreads;
	u8 wheelTreadsEstimated; // 1 = from the mesh, 0 = the default
	u32 wheelTreadOuter;     // its outermost points
	u32 wheelTreadAngles;    // their angles after merging
	int wheelTreadOpen;      // open gaps between them (NativeWheelTreads.open)
	int wheelTreadImplausible; // a count found and not believed, 0 = none
	float wheelTreadStrength; // the periodicity of the count taken
	// WHLS version 3 (render plan A1, A2, A4). The rear wheel's own mesh sits
	// behind the front one in the same buffers, laid out the same way (the
	// mesh, then its mirror image); its middles come with the front ones
	// (wheelFront, wheelRear: the axle offsets and the track are part of
	// them). wheelAlways: the file's flag ALWAYS_DRAW - the own wheels are
	// drawn past the retail tyre threshold as well.
	u8 wheelAlways;
	u8 wheelRearOwn;          // 1 = the rear wheels draw a mesh of their own
	u32 wheelRearFirst;       // its first index
	u32 wheelRearMirrorFirst; // the first index of its mirror image
	u32 wheelRearIndexCount;  // of one rear mesh: 3 Tr
	u16 wheelRearMaterial;
	float wheelRearRadius;
	float wheelRearHalfWidth;
	int wheelRearTreads;
	u8 wheelRearTreadsEstimated;

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
	u32 poseTotal;     // poseCount and the win/lose stages
	u32 finishTargets;
	u32 finishFirst[NATIVE_CHAR_GPU_FINISH_TARGETS];
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
	int wheelTreads;
	u8 wheelTreadsEstimated;
	u32 wheelTreadOuter;
	u32 wheelTreadAngles;
	int wheelTreadOpen;
	int wheelTreadImplausible;
	float wheelTreadStrength;
	u8 wheelAlways;
	u8 wheelRearOwn;
	u32 wheelRearFirst;
	u32 wheelRearMirrorFirst;
	u32 wheelRearIndexCount;
	u16 wheelRearMaterial;
	float wheelRearRadius;
	float wheelRearHalfWidth;
	int wheelRearTreads;
	u8 wheelRearTreadsEstimated;
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

// WIN AND LOSE (see NATIVE_CHAR_GPU_FINISH_STAGES). FinishLayout: the targets
// of n that get stages (none without MRPH or with withFinish 0), the pose of
// their stage 0 and the poses in all. BufferVertices: as PoseVertices for
// every pose of the buffer - below poseCount the file's pose, above it a
// stage. FinishTarget: the target of a 0-based rank (driverRank), -1 for none.
// FinishStage: the stage after that many ticks since the finish. FinishPose:
// the pose of a set for a target and stage, -1 when the set has no stages of
// that target.
void NativeCharGpu_FinishLayout(const struct RldCharNative *n, u32 poseCount, int withFinish, u32 *targets, u32 first[NATIVE_CHAR_GPU_FINISH_TARGETS],
                                u32 *total);
void NativeCharGpu_BufferVertices(const struct RldCharNative *n, u32 poseCount, const u32 first[NATIVE_CHAR_GPU_FINISH_TARGETS], u32 targets, u32 pose,
                                  u32 v0, u32 count, struct NativeProbeVertex *out);
int NativeCharGpu_FinishTarget(int rank);
// The race kinds with a real placing: NULL when win and lose are drawn, else
// the reason they are not (the drawing stays as before). gameMode1 and
// gameMode2 of the game tracker, only read.
const char *NativeCharGpu_FinishModeRefusal(u32 gameMode1, u32 gameMode2);
int NativeCharGpu_FinishStage(u32 ticksSinceFinish);
int NativeCharGpu_FinishPose(const struct NativeCharGpu *set, int target, int stage);
const char *NativeCharGpu_FinishName(int target);

// Sets made, refused, released and live, bytes, textures and uploads - the
// exit line of the render layer report (only in a run that made a set).
u32 NativeCharGpu_SetsMade(void);
void NativeCharGpu_ReportLine(void);

// --native-char-gpu-selftest <folder> (main.c, ctest native_char_gpu_selftest):
// the files rldpack make-native-tests wrote, through the native read and
// NativeCharGpu_Build without a device. 0 = passed.
int NativeCharGpu_SelfTest(const char *dir);

#endif
