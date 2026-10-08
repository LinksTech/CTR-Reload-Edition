// ===========================================================================
// THE NATIVE RENDER LAYER - FIRST STAGE: THE RETAIL WAY ONLY.
//
// The layer that will one day draw native models next to the retail
// primitives. In this stage it has three hooks and draws nothing:
//
// THE PULL (NativeRenderLayer_Pull), once per frame in MainFrame_RenderFrame
// right before RenderBucket_QueueAllInstances. It reads every living pool
// instance with a model into a host-side scene table: position, rotation as a
// quaternion, scale kept apart, animation, alpha, color and flags, plus the
// camera of every push buffer. Per logic tick (gGT->timer) the current state
// moves to the previous one, so a later stage can blend between two ticks.
// The pull sits before the queue on purpose: the queue advances
// inst->animFrame of looping instances (RenderBucket_AdvanceInstanceAnimWord),
// so this is the last point where the frame still is the one of the tick.
//
// THE ROUTE (NativeRenderLayer_Route), in RenderBucket_DispatchDrawFunc after
// the setup callback and before the handler switch - the point where every
// visibility decision is made. It answers "retail" (0) at once while no
// native model exists, which is always the case here.
//
// THE GENERATIONS (NoteBirth/NoteDeath), from INSTANCE_Birth and
// INSTANCE_Death. The race pool has 128 slots and hands a freed slot out
// again at once (JitPool_Add takes the front of the free list), even in the
// same frame. Without a generation per slot a newborn instance would inherit
// the previous tick of whatever lived there before. A checkpoint restore
// replaces the pool without either call; NativeRenderLayer_NoteRestore, from
// NativeCheckpoint_Restore, renews every slot instead.
//
// THE RULES, because the retail picture has to stay byte-identical:
// - Only reading. Nothing here writes an Instance, an InstDrawPerPlayer, a
//   Driver, the GameTracker, the scratchpad or any game memory (primMem,
//   otMem, MEMPACK). All tables are static host memory.
// - No coprocessor access of any kind: its registers are global, and the
//   route runs between the setup callback that loads them and the handler
//   that uses them. The quaternion is plain float arithmetic in C.
// - No random numbers (the game's generator is shared state), no clock and
//   no input: the pull runs on the simulation side. The tick header has a
//   host time field for a later presentation side; here it stays 0.
// - Every counter always runs; --native-layer-report only decides whether the
//   report is printed at exit. The one departure from "at exit": under the same
//   switch a checkpoint restore writes two lines when it happens (one at the
//   restore, one at the first pull after it) - once per restore, never per
//   frame, and nothing at all without the switch.
//
// Not in this stage: the instances of the level (INSTANCE_LevInitAll takes
// them from the free list without INSTANCE_Birth and never puts them on the
// taken list), so the table only holds pool instances. NR_SOURCE_LEV_INSTANCE
// is reserved as a key source for them.
//
// THE PROBE (--native-preview --native-probe). The one native object so far: a
// generated test body (platform/native_probe.c) in place of the model of seat 0
// (or of the seat --native-probe-seat names) in a one-player arcade race. In
// such a race every seat has a model of its own: VehBirth_NonGhost looks the
// model up by the name of data.characterIDs[seat], and LOAD_Robots1P gives the
// seven bots seven ids that differ from each other and from the player's - so
// binding the model binds the one instance of that seat ("most in one frame
// and view" in the report shows it). The pull binds it per frame (or unbinds
// it, with the reason in the log), the route sends every view of that model that a
// native draw can take over (handler NORMAL, normal writer, own OT range, no
// decal view, no UI) into the native list and links a marker into the own OT
// range; any other view falls back to retail and counts its first reason. The
// parser turns the marker into a native split, and NativeGpu_DrawNativeSplit
// hands the item back here: the matrix is built from the very values the
// retail model would have been drawn with (idpp->mvp, the frame origin, the
// near/huge shift the queue applied), so the body sits where the model sat.
// The form pose shapes the body by the frame of the retail animation that the
// pull took before the queue moved it on (NativeRenderLayer_FillPose), and
// measures the position source FLOAT beside KONGRUENZ in the same frames.
// Without the switches nothing binds, the route keeps its one-comparison exit,
// and the log has no line it did not have before.
//
// THE WHEELS (step 3a/3b). While the probe is on, the pull moves the float wheel
// poses of every seat on once per tick (platform/native_wheels.c). The form
// wheels (--native-probe wheels: the form pose plus native wheels) draws four
// generated wheels in the same item and the same depth as the body, right after
// it; the retail wheels of the bound model stay off as in every probe form.
// --native-wheel-report keeps the retail wheels of the probe seat on and writes,
// per tick, the float wheel middles through the matrix of the body beside the
// middles game/DrawTires.c used (NativeRenderLayer_Tires*, which only read what
// DrawTires computed). The retail wheel quads of the probe seat are counted
// either way. Under --native-layer-report the exhaust quads of the probe seat
// (game/Particle.c) give one screen box per frame (NativeRenderLayer_NoteParticleQuad),
// and a second line with the box of every single quad (the mask of the
// measuring tools: the hull of all of them covers the whole body).
// ===========================================================================

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <common.h>
#include <macros.h>
#include <platform.h>

#include "platform/native_gfx.h"
#include "platform/native_gpu.h"
#include "platform/native_gpu_links.h"
#include "platform/native_probe.h"
#include "platform/native_render_layer.h"
#include "platform/native_renderer.h"
#include "platform/native_wheels.h"
#include "platform/native_char_gpu.h"
#include "platform/native_twin.h"

int g_cfg_nativeLayerReport = 0;

// --native-seam-report and --native-depth-tint (main.c, only with --dev and
// --native-preview; measuring only, never saved): the seam line per native
// item of view 0 and frame, and the depth colour of every native draw.
int g_cfg_nativeSeamReport = 0;
int g_cfg_nativeDepthTint = 0;

// platform/native_gfx_vk.c, set by main.c before Platform_Init.
extern int g_cfg_nativePreview;

// The race-frame count of the probe texture (platform/native_renderer.c),
// declared here rather than in its header.
unsigned int NativeRenderer_ProbeUploadsInRaceFrame(void);

// --native-empty-markers, set by main.c only together with --native-preview.
// Never in ctr-settings.cfg: the file only knows s_videoSettings and the views.
int g_cfg_nativeEmptyMarkers = 0;

// THE TABLE SIZE. One object per pool slot. The largest retail pool is the
// race pool with 0x1000 >> 5 = 128 slots (MainInit.c), and --instance-pool
// never goes above retail; 256 leaves room. A slot beyond the table is
// counted and left out, never written past the end.
#define NATIVE_RENDER_LAYER_SLOTS 256

// Push buffers of the views (gGT->pushBuffer[4]); the UI push buffer has no
// camera worth blending.
#define NATIVE_RENDER_LAYER_VIEWS 4

// Distinct models seen in the UI push buffer or with SCREENSPACE_INSTANCE.
// The menus show a handful; more than that is counted as beyond the table.
#define NATIVE_RENDER_LAYER_UI_MODELS 32

// Seats in gGT->drivers.
#define NATIVE_RENDER_LAYER_DRIVERS 8

// Bits 0x10 (ANIM_LOOP) and 0x20 (ANIM_STOP_AT_END) of Instance.flags: with
// either of them set, the queue advances the animation frame on its own.
#define NATIVE_RENDER_LAYER_ANIM_BITS (ANIM_LOOP | ANIM_STOP_AT_END)

// The draw handlers of RenderBucket_DispatchDrawFunc in the order of its
// switch, and one for a selector the switch does not know (its default).
// The selector values are the RB_RETAIL_DRAWFUNC_* defines of
// RenderBucket_QueueExecute.c, which comes earlier in the translation unit.
enum NativeRenderLayerHandler
{
	NATIVE_RENDER_LAYER_HANDLER_NORMAL,
	NATIVE_RENDER_LAYER_HANDLER_NORMAL_ALT,
	NATIVE_RENDER_LAYER_HANDLER_SPLIT,
	NATIVE_RENDER_LAYER_HANDLER_SPECIAL,
	NATIVE_RENDER_LAYER_HANDLER_REFLECTION,
	NATIVE_RENDER_LAYER_HANDLER_OTHER,
	NATIVE_RENDER_LAYER_HANDLERS
};

// Why the route gave a view of the bound probe model back to retail, in the
// order the route checks them; only the first reason that holds is counted.
// The first five are the handlers other than NORMAL.
enum NrProbeFallback
{
	NR_PROBE_FALLBACK_NORMAL_ALT,
	NR_PROBE_FALLBACK_SPLIT,
	NR_PROBE_FALLBACK_SPECIAL,
	NR_PROBE_FALLBACK_REFLECTION,
	NR_PROBE_FALLBACK_OTHER,
	NR_PROBE_FALLBACK_WRITER,
	NR_PROBE_FALLBACK_DECAL_VIEW,
	NR_PROBE_FALLBACK_BORROWED_RANGE,
	NR_PROBE_FALLBACK_UI,
	NR_PROBE_FALLBACK_NO_RANGE,
	NR_PROBE_FALLBACK_ITEM_LIST_FULL,
	NR_PROBE_FALLBACK_ARENA_FULL,
	NR_PROBE_FALLBACKS
};

// The words of the report and of the probe lines, in the order of the enum.
internal const char *const s_nrProbeFallbackNames[NR_PROBE_FALLBACKS] = {
    "normal alt", "split", "special", "reflection", "other", "writer", "decal view", "borrowed range", "ui", "no range", "item list full", "arena full",
};

// --- The scene table -------------------------------------------------------

// Where an object comes from. Only NR_SOURCE_INSTANCE is filled here.
enum NrSource
{
	NR_SOURCE_INSTANCE,
	NR_SOURCE_LEV_INSTANCE,
	NR_SOURCE_TRACK,
	NR_SOURCE_HOST
};

enum NrKind
{
	NR_KIND_INSTANCE_MODEL,
	NR_KIND_KART_WHEELS,
	NR_KIND_TRACK_PASS,
	NR_KIND_SKY
};

// Once per logic tick. elapsedTimeMS is a copy and never goes back.
// hostTimeUs belongs to a later presentation side; the pull reads no clock,
// so it stays 0.
struct NrTickHeader
{
	u32 timer;
	u16 elapsedTimeMS;
	unsigned long long hostTimeUs;
};

// An object is not just its Instance pointer: the same address comes back
// for another instance as soon as the slot is reused. id is the pool slot,
// generation the host counter of that slot.
struct NrObjectKey
{
	u8 source;
	u32 id;
	u32 generation;
};

// One logic tick of one object, as read before the queue.
struct NrTickState
{
	float pos[3];   // a driver: Driver.posCurr / 256, else inst->matrix.t
	float rot[4];   // x, y, z, w from the 4.12 rotation of inst->matrix
	float scale[3]; // inst->scale / 4096, apart from the rotation
	u16 animIndex;
	u16 animFrame;  // before the queue advances it (see the head of the file)
	u16 alphaScale; // a jump value: blend only within the same source
	u32 colorRGBA;
	u32 instFlags;  // a copy of inst->flags, never written back
	u8 modeIndex;   // (flags >> 16) & 7
	u8 cut;         // 1 = do not blend into this state
};

// The camera of one push buffer over the tick.
struct NrCameraTick
{
	s16 pos[3];
	s16 rot[3];
	s32 H; // distanceToScreen_PREV
	s16 rect[4];
	u8 cut;
};

struct NrCamera
{
	struct NrCameraTick prev;
	struct NrCameraTick curr;
	u8 hasPrev;
	u8 live;
};

struct NrObject
{
	struct NrObjectKey key;
	const struct Instance *inst;
	const struct Model *stubModel; // the retail model: stand-in and fallback
	u32 nativeModel;               // no native model exists yet: always 0
	enum NrKind kind;
	struct NrTickState prev;
	struct NrTickState curr;
	u8 hasPrev;
	u8 live; // in the table since the last pull
	u8 seen; // met by the running pull
};

// Indexed by pool slot.
internal struct NrObject s_nrObjects[NATIVE_RENDER_LAYER_SLOTS];
internal u32 s_nrGeneration[NATIVE_RENDER_LAYER_SLOTS];
internal struct NrCamera s_nrCameras[NATIVE_RENDER_LAYER_VIEWS];
internal struct NrTickHeader s_nrTick;
internal int s_nrHaveTick = 0;

// THE POOL THE TABLE BELONGS TO. A load builds the instance pool anew
// (MainInit_JitPoolsNew -> JitPool_Init, MainInit_JitPoolsReset ->
// JitPool_Clear) without a single INSTANCE_Death. The pull compares base,
// item size and slot count with the last frame and empties the table when
// one of them changed. The generations are not reset: they only ever count
// up, so a birth in the new pool is a new generation in any case.
internal const void *s_nrPoolData = NULL;
internal u32 s_nrPoolItemSize = 0;
internal s32 s_nrPoolMaxItems = 0;
internal int s_nrPoolKnown = 0;

// THE RESTORE. A checkpoint restore (quick state, replay bootstrap) writes the
// whole instance pool back as it was, usually at the same base, without a
// single birth or death: the same slot can hold an instance from another
// moment, so the pool check above sees nothing and the previous tick would
// bridge the jump. NativeRenderLayer_NoteRestore gives every slot a new
// generation and empties the table. What was there before is kept for one
// pull, only to check and report that the first pull after the restore
// really starts every object new.
internal int s_nrRestorePending = 0;
internal u32 s_nrGenerationBefore[NATIVE_RENDER_LAYER_SLOTS];
internal const struct Instance *s_nrInstBefore[NATIVE_RENDER_LAYER_SLOTS];

// THE EARLY EXIT. The route is asked for every drawn instance view, so it
// has to answer with one comparison while there is nothing native - the same
// pattern as NativeChar_ModelFullHeight in native_chars.c. Only the bound probe
// raises it (to 1, NativeRenderLayer_BindProbe); without --native-probe it
// stays 0 for the whole run.
internal int s_nativeModelCount = 0;

// The same for the custom characters drawn natively (step 4c): the number of
// their models bound this frame (NativeRenderLayer_BindChars). 0 for the whole
// run without --native-preview or without a ready GPU set.
internal int s_nrCharCount = 0;

// THE SEATS OF THIS FRAME. gGT->drivers[] is only rebuilt in the first state
// after a load, while the scene keeps being drawn through the load (see
// NativeRenderLayer_ReadSeats): a pointer in it may point into memory the load
// has already rebuilt. So no hook of this file follows one of them. The pull
// reads the table below once per frame, as its first step - empty while a load
// or a restart is under way, and a seat only with its driver in the large stack
// pool and its instance on the taken list of the instance pool. The hooks
// compare instances with this table and read a driver only through it.
enum NrSeatState
{
	NR_SEAT_NOT_READ,
	NR_SEAT_READY,
	NR_SEAT_LOADING,
	NR_SEAT_NO_DRIVER,
	NR_SEAT_DRIVER_OUTSIDE,
	NR_SEAT_NO_INSTANCE,
	NR_SEAT_INSTANCE_OUTSIDE
};

internal const struct Driver *s_nrSeatDriver[NATIVE_RENDER_LAYER_DRIVERS];
internal const struct Instance *s_nrSeatInst[NATIVE_RENDER_LAYER_DRIVERS];
internal u8 s_nrSeatState[NATIVE_RENDER_LAYER_DRIVERS];

// --- The counters ----------------------------------------------------------

struct NativeRenderLayerUiModel
{
	const struct Model *model; // only to tell the models apart, never printed
	s16 id;                    // copied when first seen
	unsigned long long entries;
};

struct NativeRenderLayerCounters
{
	// Route and switch, both counted in RenderBucket_DispatchDrawFunc.
	unsigned long long routeCalls;
	unsigned long long switchEntries;
	unsigned long long native;

	unsigned long long handlerAll[NATIVE_RENDER_LAYER_HANDLERS];
	unsigned long long handlerSeat0[NATIVE_RENDER_LAYER_HANDLERS];
	unsigned long long seat0AnimBits;

	// UI instances: in the UI push buffer, or only flagged screen space.
	unsigned long long uiEntries;
	unsigned long long uiByPushBuffer;
	unsigned long long uiByFlagOnly;
	unsigned long long uiBeyondTable;

	// The pull, for information only: it reads every living pool instance,
	// the queue draws a subset of them per view, so there is no equality.
	unsigned long long pullFrames;
	unsigned long long ticks;
	unsigned long long objects;
	int objectsPeak;
	unsigned long long cuts;
	unsigned long long tableResets;
	unsigned long long pullOutsidePool;
	unsigned long long pullBeyondTable;
	unsigned long long pullListCut;

	// The generations.
	unsigned long long births;
	unsigned long long deaths;
	unsigned long long lifeOutsidePool;
	unsigned long long lifeBeyondTable;

	// Checkpoint restores, and what the first pull after each one met.
	unsigned long long restores;
	unsigned long long restoreGenerations;
	unsigned long long restorePulls;
	unsigned long long restoreObjects;
	unsigned long long restoreRaised;
	unsigned long long restoreCut;
	unsigned long long restoreWithoutPrev;
	unsigned long long restoreSameAddress;

	// The marker channel: markers linked into an OT, the ones that found the
	// arena full, and why an empty marker was not written at a driver.
	unsigned long long markersWritten;
	unsigned long long markerArenaFull;
	unsigned long long emptySkipHandler;
	unsigned long long emptySkipDecalView;
	unsigned long long emptySkipBorrowedRange;
	unsigned long long emptySkipUi;
	unsigned long long emptySkipNoRange;

	// The native probe: frames it was bound in, what was drawn of it, why a
	// view of its model stayed retail, and the checks on the matrix.
	unsigned long long probeBoundFrames;
	unsigned long long probeDraws;
	unsigned long long probeDrawFrames;
	int probeMostPerView;
	unsigned long long probeNotDrawn;
	unsigned long long probeStale;
	unsigned long long probeFallback[NR_PROBE_FALLBACKS];
	unsigned long long probeDepthClears;
	unsigned long long probeMirrored;
	unsigned long long probeShiftNear;
	unsigned long long probeShiftHuge;
	unsigned long long probeShiftCheckOff;
	double probeDepthJumpMax;
	unsigned long long probeSeat0NoDispatch;

	// The seam at view z 0x1000 (see the depth part of DrawNativeItem): native
	// draws at view z 0x1000 or more, the changes of the shift between two
	// neighbouring native frames of view 0 (each one compared for the depth
	// jump), the changes across a frame without a native draw (not compared),
	// the largest jump with its VBlank, both depths and both shifts (a wrong
	// shift would show as a depth ratio of about 4 or 1/4), and as a yardstick
	// the largest step between neighbouring native frames without a change,
	// near the seam.
	unsigned long long probeShiftFar;
	unsigned long long probeShiftChanges;
	unsigned long long probeShiftNearToFar;
	unsigned long long probeShiftFarToNear;
	unsigned long long probeShiftChangesAfterGap;
	double probeDepthJumpMaxZ;
	double probeDepthJumpMaxZAfter;
	int probeDepthJumpMaxVBlank;
	int probeDepthJumpMaxShiftBefore;
	int probeDepthJumpMaxShiftAfter;
	double probeDepthStepMax;
	unsigned long long probeDepthStepPairs;

	// The model-view matrix composed in C from the view matrix and idpp->m3x3,
	// against the one the queue stored, in every native draw (0 expected), and
	// the fallback frames of view 0 whose probe box comes from that composition.
	unsigned long long probeComposedOff;
	unsigned long long probeFallbackBoxes;

	// The form pose: frames of view 0 drawn natively with a pose, how many drew
	// the frame idpp->ptrCurrFrame points at, how many did not, how many had no
	// value of the pull to take the pose from, and the morphed meshes written
	// into the pose buffer (vertex data, not texture uploads).
	unsigned long long poseTicks;
	unsigned long long poseMatched;
	unsigned long long poseMismatched;
	unsigned long long poseWithoutPull;
	unsigned long long poseFrameMismatched;
	unsigned long long poseAnimMismatched;
	unsigned long long poseWordDiffers;
	unsigned long long poseVertexWrites;

	// The position source FLOAT, measured beside KONGRUENZ in the same frames
	// (form pose only): the distance on the screen between the origin of the
	// object as the queue projects it and as the float state of the pull and
	// the camera project it, in pixels of the internal picture at scale 1.
	unsigned long long floatTicks;
	double floatDistanceMax;
	double floatDistanceSum;
	double floatDistanceTMax;
	double floatDistanceTSum;

	// The wheels: retail wheel quads game/DrawTires.c wrote for the seat the probe
	// is bound to (solid and reflection pass), items that got native wheels, the
	// wheel draws of the form wheels and the ones the renderer refused, and the
	// lines of --native-wheel-report.
	unsigned long long tiresSolidFt4;
	unsigned long long tiresReflectionFt4;
	unsigned long long wheelItems;
	unsigned long long wheelDraws;
	unsigned long long wheelNotDrawn;
	unsigned long long wheelFloatLines;
	unsigned long long wheelExactLines;
	unsigned long long wheelRetailLines;

	// The exhaust quads of the probe seat in view 0 (game/Particle.c): frames
	// that had any, and all of them.
	unsigned long long exhaustFrames;
	unsigned long long exhaustQuads;
};

internal struct NativeRenderLayerCounters s_nrCount;
internal struct NativeRenderLayerUiModel s_nrUiModels[NATIVE_RENDER_LAYER_UI_MODELS];
internal int s_nrUiModelCount = 0;

// --- The marker arenas -----------------------------------------------------

// THE ARENA. A marker is a packet in the ordering table, so the parser follows
// a 24-bit link to it: the memory has to be a range the link map knows. One
// arena per double buffer, static host memory - never MEMPACK and never
// primMem, where the end of the arena would cut instances off without a word.
// 512 per buffer: one queue list holds at most 512 entries (renderBucketSize
// 0x1000 / 8 bytes), and there is at most one marker per entry.
#define NATIVE_RENDER_LAYER_MARKERS 512

internal DR_PSYX_NATIVE s_nrMarkers[2][NATIVE_RENDER_LAYER_MARKERS];
internal int s_nrMarkerCount[2];
internal int s_nrMarkerDb = 0;

// --- The native list and the probe -----------------------------------------

// THE NATIVE LIST. What a marker that draws stands for: the item of the marker
// is the index into the list of the buffer it was written in, and it is drawn
// in the same frame (the OT is parsed after the queue has run). Emptied
// together with the marker arena of that buffer. The probe needs one entry per
// frame and view; 64 leave room, and a full list falls back to retail.
#define NATIVE_RENDER_LAYER_ITEMS 64

// The probe lines of the report come every 30th VBlank, the spacing of the
// shots of an autopilot run.
#define NATIVE_RENDER_LAYER_PROBE_LINE_STEP 30

// THE BODY IN THE HULL. The queue culls the retail model by its hull: the box
// [origin, origin + 0x3fc] on every axis, in the input units of the model's
// vertices (frame byte plus frame origin, times 4). The probe body is put
// inside it: half width, full height and half length below (about 0.75 m wide
// and high and 1.25 m long at 64 units per metre), its middle at the model's
// own origin as far as the hull allows, its floor a little above the hull's.
#define NATIVE_RENDER_LAYER_HULL_SIZE   0x3fc
#define NATIVE_RENDER_LAYER_BODY_HALF_X 0x60
#define NATIVE_RENDER_LAYER_BODY_HEIGHT 0xC0
#define NATIVE_RENDER_LAYER_BODY_HALF_Z 0xA0
#define NATIVE_RENDER_LAYER_BODY_FLOOR  0x10

// What FillItem found besides the item: counted only once the marker stands.
#define NR_ITEM_NEAR      0x1 // the queue scaled the instance up by 4
#define NR_ITEM_HUGE      0x2 // DRAW_HUGE: scaled down by 4
#define NR_ITEM_SHIFT_OFF 0x4 // the recomputed shift does not explain mvp.t[2]

// One native instance view, everything taken at route time from the values
// the retail handler would have drawn with.
struct NrDrawItem
{
	u16 object;        // pool slot (scene table), 0xFFFF outside the pool
	u8 view;           // index of pb in gGT->pushBuffer
	u8 cull;           // NATIVE_GFX_CULL_BACK or _FRONT, from the sign of det(mvp)
	s8 mvpShift;       // +2 near, -2 DRAW_HUGE, 0 otherwise (both: 0)
	u8 nearView;       // 1 = view z below 0x1000, the queue scaled up by 4
	// The form pose (NativeRenderLayer_FillPose): the frame the body is posed
	// with (from the pull, -1 without one), the frame idpp->ptrCurrFrame is (-1
	// when it cannot be told), whether the pull had a value, and the distance
	// FLOAT to KONGRUENZ (floatKnown 0 when it could not be measured).
	s16 poseDrawn;
	s16 poseCurrent;
	s16 poseDrawnAnim;   // animation index of the pull, -1 without
	s16 poseCurrentAnim; // animation idpp->ptrCurrFrame lies in, -1 when none
	u8 posePull;
	u8 poseWordDiffers;  // the animation word after the queue is not the pulled one
	u8 floatKnown;
	double floatDistance;  // FLOAT from the pull's state
	double floatDistanceT; // FLOAT from inst->matrix.t
	s16 mvp[3][3];     // idpp->mvp.m (4.12); in a fallback box the composed one
	s32 mvpT[3];       // idpp->mvp.t
	s32 bodyCenter[3]; // middle of the body (floor for y), vertex input units
	s32 bodyHalf[3];   // half width, height, half length
	s32 hullMin[3];    // the frame origin, as the hull of the queue has it
	s32 H;             // pb->distanceToScreen_PREV
	s16 rectW, rectH;  // pb->rect.w, pb->rect.h
	// Only for the probe line: the four retail wheels of the kart, which the
	// bound model no longer gets (see FillWheels). Middles in view space and the
	// half extent of a box around every wheel, the same for all four.
	u8 wheels;         // 1 = the two fields below are set
	float wheelView[4][3];
	float wheelExt[3];
	// The native wheels (form wheels, --native-wheel-report): per wheel the map
	// from the unit wheel mesh (platform/native_probe.c) to view space, true
	// units (the shift of the queue taken out): view = wheelA * u + wheelB.
	// wheelB is the wheel middle. Set only when nativeWheels is 1.
	u8 nativeWheels;
	double wheelA[4][3][3];
	double wheelB[4][3];
	// The wheel middles once more with the exact translation, only for the
	// report: (inst->matrix.t - pb->pos) in double, the source of the position
	// FLOAT of the form pose (NativeRenderLayer_FillPose), plus the retail middle
	// turned by inst->matrix - no whole-unit cut of mvp.t (render plan L4). View
	// space, world units, not shifted.
	double wheelExact[4][3];
	// The view z the item was recomputed with (FillItem), for the seam line.
	s32 viewZ;
	// STEP 4C, a custom character (kind NR_ITEM_CHAR; 0 for the probe, which
	// memset gives every probe item): its seat, its GPU set, the pose of the
	// body, and per axis the vertex input units of the queue per model unit
	// (16384 / mh->scale), the origin of the model units being the model's own.
	u8 kind;
	u8 seat;
	u8 twin; // the retail twin (step 4d): its own cull, pose table, no wheels
	u16 pose;
	// The retail look of the twin (step 4d Z1): the tone a = alphaScale / 4096
	// of this view (0 = none) and the far colour F (0..1) of the instance's
	// setup callback, as RenderBucket_RunInstanceSetupCallback loads it.
	float tone;
	float far[3];
	const struct NativeCharGpu *gpu;
	double unitScale[3];
};

#define NR_ITEM_PROBE 0
#define NR_ITEM_CHAR  1

internal struct NrDrawItem s_nrItems[2][NATIVE_RENDER_LAYER_ITEMS];

// THE POSE BUFFER WITH WHEELS. The form wheels draws the body and two halves of
// each of the four wheels per item, every one from a region of the pose buffer
// of its own: nine regions per item, so at most NATIVE_LAYER_POSE_SLOTS / 9
// items per frame (one is all a one-player race needs). The other forms keep
// one region per item, as before.
#define NR_WHEEL_REGIONS (1 + (NATIVE_WHEELS_COUNT * NATIVE_PROBE_WHEEL_HALVES))
CTR_STATIC_ASSERT((NATIVE_LAYER_POSE_SLOTS / NR_WHEEL_REGIONS) >= 1);
CTR_STATIC_ASSERT((NATIVE_LAYER_POSE_SLOTS / NR_WHEEL_REGIONS) <= NATIVE_RENDER_LAYER_ITEMS);

// The form pose: every item has a region of its own in the pose buffer.
CTR_STATIC_ASSERT(NATIVE_RENDER_LAYER_ITEMS <= NATIVE_LAYER_POSE_SLOTS);

// The pull hands the wheel table (platform/native_wheels.c) the seats of this
// file by the same index.
CTR_STATIC_ASSERT(NATIVE_WHEELS_SEATS == NATIVE_RENDER_LAYER_DRIVERS);

// The morphed mesh of the draw in progress; the renderer copies it into the
// pose buffer before the next one is made.
internal struct NativeProbeVertex s_nrPoseVertices[NATIVE_PROBE_VERTEX_COUNT];
internal int s_nrItemCount[2];

// The frame number of the layer, raised in RegisterMarkerArenas (with the
// probe that runs every frame), and the stamps that compare against it: the
// depth of a view is cleared before its first native draw in a frame.
internal u32 s_nrFrame = 0;
internal u32 s_nrDepthClearFrame[NATIVE_RENDER_LAYER_VIEWS];
internal u32 s_nrViewDrawFrame[NATIVE_RENDER_LAYER_VIEWS];
internal int s_nrViewDraws[NATIVE_RENDER_LAYER_VIEWS];
internal u32 s_nrDrawFrame = 0;

// THE BINDING. Pointers kept only to compare with; the model is the one the
// birth gave the instance of the probe seat (g_cfg_nativeProbeSeat, 0 unless
// --native-probe-seat names another), read anew every frame.
internal const struct Model *s_probeModel = NULL;
internal const struct Instance *s_probeInst = NULL;

// One bound frame with a queue, looked at when the next frame starts: was the
// instance of the probe seat dispatched in view 0, was anything drawn natively,
// what was the last reason to stay retail, and was a fallback box kept for the
// probe line (in the buffer fallbackDb).
struct NrProbeFrame
{
	int live;
	int dispatched;
	int routed;
	int drawn;
	const char *lastFallback;
	int fallbackBox;
	int fallbackDb;
};

internal struct NrProbeFrame s_nrProbeFrame;

// THE FALLBACK BOX. A view 0 split frame of the probe seat stays retail, but
// its probe line still gets the rectangle of the body, the hull and the wheels:
// the item is filled as for a native draw, only nothing is drawn, and the line
// is written when the frame is closed. The draw offset a native split of that
// frame would have had is the one of the last native draw in the same double
// buffer (the offset comes from the draw environment of the buffer).
internal struct NrDrawItem s_nrFallbackItem;
internal double s_nrLastOfs[2][2];
internal int s_nrLastOfsKnown[2];

// The true depth of the middle of the body in view 0 at the last native draw,
// and the shift it was queued with: a depth jump where the shift changes would
// be a seam at view z 0x1000. Valid only while the frames are neighbours; the
// last shift is kept across a gap, to count the changes no jump was taken of.
internal double s_nrDepthPrev = 0.0;
internal int s_nrDepthPrevShift = 0;
internal int s_nrDepthPrevNear = 0;
internal int s_nrDepthPrevValid = 0;
internal int s_nrDepthLastShift = 0;
internal int s_nrDepthHaveLast = 0;

// The yardstick of the depth steps without a shift change: only pairs with
// both depths from half to twice the seam (0x800 to 0x2000), where a step of
// the same motion has the size it has at the seam.
#define NR_DEPTH_STEP_LOW  2048.0
#define NR_DEPTH_STEP_HIGH 8192.0

// Text of the last reason the probe is not bound, for the reasons that name
// the seat.
internal char s_nrProbeWhy[64];

// The VBlank of the last probe line, so a VBlank never gets two.
internal int s_nrProbeLineVBlank = -1;

// --native-wheel-report: 1 in a frame whose pull saw a new tick; each of the two
// lines is written at most once in such a frame.
internal int s_nrWheelSample = 0;
internal u32 s_nrWheelSampleTimer = 0;
internal int s_nrWheelFloatDone = 0;
internal int s_nrWheelRetailDone = 0;

// One wheel set of game/DrawTires.c between NativeRenderLayer_TiresBegin and
// _TiresEnd: whether it is the probe seat, which pass, the corners it projected
// (screen x and y, the packed words of the GTE split up) and which wheels it
// emitted a quad for.
struct NrTires
{
	int active;
	int reflection;
	int view0;
	int wheel;
	int emitted[4];
	int corners[4][4][2];
};

internal struct NrTires s_nrTires;

// The exhaust quads of the probe seat in view 0 in this frame (screen pixels of
// the GTE, before the draw offset), and the buffer they were written for: the
// hull of all of them (box) and, for the quad line, the box of each of the
// first NR_EXHAUST_QUADS_LISTED (quadBox, x0 y0 x1 y1). Quads beyond that go
// into the hull and the count and are named as not listed in the line.
#define NR_EXHAUST_QUADS_LISTED 96

struct NrExhaust
{
	int quads;
	int db;
	int box[4];
	int quadBox[NR_EXHAUST_QUADS_LISTED][4];
};

internal struct NrExhaust s_nrExhaust;

// The VBlank of the last exhaust line, so a VBlank never gets two (like the
// probe lines).
internal int s_nrExhaustLineVBlank = -1;

// --- Helpers ---------------------------------------------------------------

// The pool slot of an item (an instance, or the stack object of a thread), or
// -1 when the address is not the start of an item of this pool. Plain address
// arithmetic on the host pointer, the same formula as LevInstDef_IsInstance.
internal int NativeRenderLayer_PoolSlot(const struct JitPool *pool, const void *inst)
{
	size_t base;
	size_t addr;
	size_t offset;
	size_t stride;

	if ((pool == NULL) || (inst == NULL) || (pool->ptrPoolData == NULL) || (pool->maxItems <= 0))
	{
		return -1;
	}

	stride = (size_t)JITPOOL_ALIGN_ITEM_STRIDE(pool->itemSize);
	if (stride == 0)
	{
		return -1;
	}

	base = (size_t)pool->ptrPoolData;
	addr = (size_t)inst;
	if (addr < base)
	{
		return -1;
	}

	offset = addr - base;
	if ((offset % stride) != 0)
	{
		return -1;
	}

	if ((offset / stride) >= (size_t)pool->maxItems)
	{
		return -1;
	}

	return (int)(offset / stride);
}

// The pool slot for a birth or a death, or -1 for an instance outside the
// pool. The caller checks the slot against the table size and counts the two
// cases apart.
internal int NativeRenderLayer_LifeSlot(const struct Instance *inst)
{
	const struct GameTracker *gGT = sdata->gGT;

	if (gGT == NULL)
	{
		return -1;
	}

	return NativeRenderLayer_PoolSlot(&gGT->JitPools.instance, inst);
}

// One birth or death: a new generation for the slot, or a count of why not.
internal void NativeRenderLayer_NoteLife(const struct Instance *inst, unsigned long long *counter)
{
	const int slot = NativeRenderLayer_LifeSlot(inst);

	if (slot < 0)
	{
		s_nrCount.lifeOutsidePool++;
		return;
	}
	if (slot >= NATIVE_RENDER_LAYER_SLOTS)
	{
		s_nrCount.lifeBeyondTable++;
		return;
	}

	s_nrGeneration[slot]++;
	(*counter)++;
}

internal void NativeRenderLayer_ResetTable(void)
{
	memset(s_nrObjects, 0, sizeof(s_nrObjects));
	memset(s_nrCameras, 0, sizeof(s_nrCameras));
}

// Rotation part of a 4.12 matrix to a unit quaternion (x, y, z, w). The
// matrix is not guaranteed to be orthonormal (4.12 rounding, game code that
// writes it by hand), so the result is normalized; a matrix without any
// usable rotation gives the identity.
internal void NativeRenderLayer_MatrixToQuat(const MATRIX *m, float q[4])
{
	const float k = 1.0f / 4096.0f;
	const float r00 = (float)m->m[0][0] * k;
	const float r01 = (float)m->m[0][1] * k;
	const float r02 = (float)m->m[0][2] * k;
	const float r10 = (float)m->m[1][0] * k;
	const float r11 = (float)m->m[1][1] * k;
	const float r12 = (float)m->m[1][2] * k;
	const float r20 = (float)m->m[2][0] * k;
	const float r21 = (float)m->m[2][1] * k;
	const float r22 = (float)m->m[2][2] * k;
	const float trace = r00 + r11 + r22;
	float s;
	float len;

	// The usual four cases: divide by the largest of the four terms, so the
	// square root never sees a value near zero when another one is large.
	if (trace > 0.0f)
	{
		s = sqrtf(trace + 1.0f) * 2.0f;
		q[3] = 0.25f * s;
		q[0] = (r21 - r12) / s;
		q[1] = (r02 - r20) / s;
		q[2] = (r10 - r01) / s;
	}
	else if ((r00 > r11) && (r00 > r22))
	{
		s = sqrtf(1.0f + r00 - r11 - r22) * 2.0f;
		if (s < 1e-6f)
		{
			s = 1e-6f;
		}
		q[3] = (r21 - r12) / s;
		q[0] = 0.25f * s;
		q[1] = (r01 + r10) / s;
		q[2] = (r02 + r20) / s;
	}
	else if (r11 > r22)
	{
		s = sqrtf(1.0f + r11 - r00 - r22) * 2.0f;
		if (s < 1e-6f)
		{
			s = 1e-6f;
		}
		q[3] = (r02 - r20) / s;
		q[0] = (r01 + r10) / s;
		q[1] = 0.25f * s;
		q[2] = (r12 + r21) / s;
	}
	else
	{
		// 1 + r22 - r00 - r11 can go negative for a matrix that is no
		// rotation at all (zeros, a squashed one); clamp before the root.
		const float t = 1.0f + r22 - r00 - r11;

		s = sqrtf((t > 0.0f) ? t : 0.0f) * 2.0f;
		if (s < 1e-6f)
		{
			s = 1e-6f;
		}
		q[3] = (r10 - r01) / s;
		q[0] = (r02 + r20) / s;
		q[1] = (r12 + r21) / s;
		q[2] = 0.25f * s;
	}

	len = sqrtf((q[0] * q[0]) + (q[1] * q[1]) + (q[2] * q[2]) + (q[3] * q[3]));
	if (len < 1e-6f)
	{
		q[0] = 0.0f;
		q[1] = 0.0f;
		q[2] = 0.0f;
		q[3] = 1.0f;
		return;
	}

	q[0] /= len;
	q[1] /= len;
	q[2] /= len;
	q[3] /= len;
}

// One tick state of an instance. driver is the Driver whose instSelf this
// instance is, or NULL.
internal void NativeRenderLayer_ReadState(const struct Instance *inst, const struct Driver *driver, struct NrTickState *state)
{
	const float k = 1.0f / 4096.0f;

	memset(state, 0, sizeof(*state));

	// A driver's matrix.t is the integer part of posCurr, which carries 8
	// fractional bits; the finer one is the better source for a blend.
	if (driver != NULL)
	{
		state->pos[0] = (float)driver->posCurr.x / 256.0f;
		state->pos[1] = (float)driver->posCurr.y / 256.0f;
		state->pos[2] = (float)driver->posCurr.z / 256.0f;
	}
	else
	{
		state->pos[0] = (float)inst->matrix.t[0];
		state->pos[1] = (float)inst->matrix.t[1];
		state->pos[2] = (float)inst->matrix.t[2];
	}

	NativeRenderLayer_MatrixToQuat(&inst->matrix, state->rot);

	// Apart from the rotation: a squashed driver has scale y = 0, which no
	// quaternion could carry.
	state->scale[0] = (float)inst->scale.x * k;
	state->scale[1] = (float)inst->scale.y * k;
	state->scale[2] = (float)inst->scale.z * k;

	state->animIndex = (u16)inst->animIndex;
	state->animFrame = (u16)inst->animFrame;
	state->alphaScale = (u16)inst->alphaScale;
	state->colorRGBA = inst->colorRGBA;
	state->instFlags = inst->flags;
	state->modeIndex = (u8)((inst->flags >> 16) & 7);
	state->cut = 0;
}

internal void NativeRenderLayer_ReadCamera(const struct PushBuffer *pb, struct NrCameraTick *cam)
{
	memset(cam, 0, sizeof(*cam));
	cam->pos[0] = pb->pos.x;
	cam->pos[1] = pb->pos.y;
	cam->pos[2] = pb->pos.z;
	cam->rot[0] = pb->rot.x;
	cam->rot[1] = pb->rot.y;
	cam->rot[2] = pb->rot.z;
	cam->H = (s32)pb->distanceToScreen_PREV;
	cam->rect[0] = pb->rect.x;
	cam->rect[1] = pb->rect.y;
	cam->rect[2] = pb->rect.w;
	cam->rect[3] = pb->rect.h;
	cam->cut = 0;
}

// The cameras of the views in use. A view that comes into use starts with a
// cut; a camera cut inside a running view (mode change, teleport) is not
// detected in this stage.
internal void NativeRenderLayer_PullCameras(const struct GameTracker *gGT, int tick)
{
	int views = (int)gGT->numPlyrCurrGame;
	int view;

	if (views > NATIVE_RENDER_LAYER_VIEWS)
	{
		views = NATIVE_RENDER_LAYER_VIEWS;
	}

	for (view = 0; view < NATIVE_RENDER_LAYER_VIEWS; view++)
	{
		struct NrCamera *cam = &s_nrCameras[view];
		struct NrCameraTick read;

		if (view >= views)
		{
			cam->live = 0;
			cam->hasPrev = 0;
			continue;
		}

		NativeRenderLayer_ReadCamera(&gGT->pushBuffer[view], &read);

		if (!cam->live)
		{
			read.cut = 1;
			cam->curr = read;
			cam->hasPrev = 0;
			cam->live = 1;
		}
		else if (tick)
		{
			cam->prev = cam->curr;
			cam->curr = read;
			cam->hasPrev = 1;
		}
		else
		{
			read.cut = cam->curr.cut;
			cam->curr = read;
		}
	}
}

internal int NativeRenderLayer_HandlerIndex(u32 handler)
{
	if (handler == (u32)RB_RETAIL_DRAWFUNC_NORMAL)
	{
		return NATIVE_RENDER_LAYER_HANDLER_NORMAL;
	}
	if (handler == (u32)RB_RETAIL_DRAWFUNC_NORMAL_ALT)
	{
		return NATIVE_RENDER_LAYER_HANDLER_NORMAL_ALT;
	}
	if (handler == (u32)RB_RETAIL_DRAWFUNC_SPLIT)
	{
		return NATIVE_RENDER_LAYER_HANDLER_SPLIT;
	}
	if (handler == (u32)RB_RETAIL_DRAWFUNC_SPECIAL)
	{
		return NATIVE_RENDER_LAYER_HANDLER_SPECIAL;
	}
	if (handler == (u32)RB_RETAIL_DRAWFUNC_REFLECTION)
	{
		return NATIVE_RENDER_LAYER_HANDLER_REFLECTION;
	}
	return NATIVE_RENDER_LAYER_HANDLER_OTHER;
}

// One UI entry, counted per model pointer. The id is copied when the model is
// first seen: at exit the model may long be gone with its load. The report
// prints neither the pointer (another address in every run) nor the name (game
// data, or a custom file, may hold any word), only the table number and id.
internal void NativeRenderLayer_NoteUi(const struct Instance *inst, int byPushBuffer)
{
	const struct Model *model = inst->model;
	int entry;

	s_nrCount.uiEntries++;
	if (byPushBuffer)
	{
		s_nrCount.uiByPushBuffer++;
	}
	else
	{
		s_nrCount.uiByFlagOnly++;
	}

	for (entry = 0; entry < s_nrUiModelCount; entry++)
	{
		if (s_nrUiModels[entry].model == model)
		{
			s_nrUiModels[entry].entries++;
			return;
		}
	}

	if (s_nrUiModelCount >= NATIVE_RENDER_LAYER_UI_MODELS)
	{
		s_nrCount.uiBeyondTable++;
		return;
	}

	s_nrUiModels[s_nrUiModelCount].model = model;
	s_nrUiModels[s_nrUiModelCount].id = (model != NULL) ? model->id : (s16)-1;
	s_nrUiModels[s_nrUiModelCount].entries = 1;
	s_nrUiModelCount++;
}

// --- The marker channel ----------------------------------------------------

// Links one marker into the own OT range of an instance view. 1 = written,
// 0 = not (no own range, or the arena of this buffer is full - counted).
//
// THE BIN. The middle of the depth range the queue gave the instance
// (depthOffset[0..1]), clamped to that range. This is a choice of the layer:
// the retail writers pick a bin per triangle from its depth, so there is no
// one bin of "the instance" in the game code to copy.
//
// THE RANGE. otRangeNormal is already moved back by minDepth entries
// (RenderBucket_AllocateOTRange), so an absolute bin is added as it is, just
// like RenderBucket_GetClampedOTEntry does. Writing there is writing into the
// range the instance owns in this frame - nothing else of the OT is touched.
//
// THE ORDER. Payload first, then tag and head link, exactly as
// RenderBucket_LinkPrimRaw hangs a primitive in: the parser can only reach the
// marker after its head link is written, and by then the code words stand.
internal int NativeRenderLayer_LinkMarker(const struct InstDrawPerPlayer *idpp, u32 item, u32 flags)
{
	const int db = s_nrMarkerDb;
	DR_PSYX_NATIVE *m;
	u32 *entry;
	int bin;

	if (idpp->otRangeNormal == 0)
	{
		return 0;
	}

	if (s_nrMarkerCount[db] >= NATIVE_RENDER_LAYER_MARKERS)
	{
		s_nrCount.markerArenaFull++;
		return 0;
	}

	bin = ((int)idpp->depthOffset[0] + (int)idpp->depthOffset[1]) / 2;
	if (bin < idpp->depthOffset[0])
	{
		bin = idpp->depthOffset[0];
	}
	if (bin > idpp->depthOffset[1])
	{
		bin = idpp->depthOffset[1];
	}

	entry = (u32 *)idpp->otRangeNormal + bin;
	m = &s_nrMarkers[db][s_nrMarkerCount[db]];

	m->code[1] = flags;
	m->code[0] = ((u32)PSYX_NATIVE_CODE << 24) | (item & 0xFFFFFFu);
	// len 2: the two code words after the tag.
	CTR_GPU_WriteTagWord(m, CtrGpu_PackOTTag(*entry, 0x02000000u));
	*entry = CtrGpu_PrimToOTLink24(m);

	s_nrMarkerCount[db]++;
	s_nrCount.markersWritten++;
	return 1;
}

// --native-empty-markers: one empty marker at every view of a driver instance
// that the switch of a later stage would draw natively - the same conditions,
// so that stage takes the channel this one tested. Any other driver view
// counts its first reason; an instance that is no driver counts nothing.
// item is the running number of the marker in this buffer.
internal void NativeRenderLayer_EmptyMarker(const struct Instance *inst, const struct InstDrawPerPlayer *idpp, const struct PushBuffer *pb)
{
	const struct GameTracker *gGT = sdata->gGT;
	int isDriver = 0;
	int seat;

	if ((inst == NULL) || (idpp == NULL) || (gGT == NULL))
	{
		return;
	}

	// The seats of this frame, as the pull checked them: during a load none.
	for (seat = 0; seat < NATIVE_RENDER_LAYER_DRIVERS; seat++)
	{
		if ((s_nrSeatInst[seat] != NULL) && (s_nrSeatInst[seat] == inst))
		{
			isDriver = 1;
			break;
		}
	}
	if (!isDriver)
	{
		return;
	}

	// Split, reflection and the other handlers need a second range and a clip
	// plane; they stay retail.
	if ((u32)idpp->unkEC != (u32)RB_RETAIL_DRAWFUNC_NORMAL)
	{
		s_nrCount.emptySkipHandler++;
		return;
	}

	// A multiplayer decal view draws into an offscreen OT for a VRAM tile.
	if ((idpp->instFlags & PUSHBUFFER_EXISTS) != 0)
	{
		s_nrCount.emptySkipDecalView++;
		return;
	}

	// The queue gave this view no range of its own (borrowed or stale).
	if (((u32)idpp->instFlags & RB_INSTANCE_SKIP_OT_RANGE) != 0)
	{
		s_nrCount.emptySkipBorrowedRange++;
		return;
	}

	// In the UI OT the parser groups by geometry; a marker without any has
	// no place there.
	if ((pb == &gGT->pushBuffer_UI) || ((inst->flags & SCREENSPACE_INSTANCE) != 0))
	{
		s_nrCount.emptySkipUi++;
		return;
	}

	if (!NativeRenderLayer_LinkMarker(idpp, (u32)s_nrMarkerCount[s_nrMarkerDb], PSYX_NATIVE_FLAG_EMPTY))
	{
		// A full arena is counted by LinkMarker itself.
		if (idpp->otRangeNormal == 0)
		{
			s_nrCount.emptySkipNoRange++;
		}
	}
}

// --- The probe -------------------------------------------------------------

// 1 when item is on the taken list of the pool. The walk stops at the slot
// count, so a broken list ends it instead of looping (as in the pull).
internal int NativeRenderLayer_PoolTaken(const struct JitPool *pool, const void *item)
{
	const struct Item *walk;
	int guard = 0;

	for (walk = pool->taken.first; walk != NULL; walk = walk->next)
	{
		if (guard >= pool->maxItems)
		{
			return 0;
		}
		guard++;

		if ((const void *)walk == item)
		{
			return 1;
		}
	}
	return 0;
}

// THE SEATS, read only once the race has been built, and every pointer checked
// against its pool before it is followed. Once per frame, as the first step of
// the pull, for all eight seats; the probe, the empty markers, the wheels of
// the probe line, the pull and the handler count of seat 0 all take the seats
// from this table and none of them reads gGT->drivers[] itself.
//
// Why: gGT->drivers[] is only written by MainInit_Drivers (MainInit.c:466
// clears all eight, :488 births the players), called from
// MainInit_FinalizeInit (MainInit.c:657) in the first state after a load
// (MainMain.c:128-200, mainGameState 1). Until then the array keeps the
// drivers of the level before. A level change sets sdata->Loading.stage away
// from LOAD_IDLE (MainRaceTrack.c:42; LOAD_Level.c:64), and the -4 step clears
// MAIN_MENU in gameMode1 right before MainRaceTrack_StartLoad
// (MainMain.c:285-309); the scene keeps being rendered through the whole load
// (MainMain.c:280, :318, :353, "keep rendering the scene"), while the stages rebuild
// the memory the old drivers, instances and models lived in. Only at the end
// of the load the stage goes back to LOAD_IDLE and LOADING is removed
// (MainMain.c:346-348, restart :259-275), and the next iteration rebuilds the
// pools and the drivers before anything is rendered again.
//
// So: no seat while a load or a restart is under way, and even then the driver
// of a seat must be the object of a stack item of the large pool (a thread's
// object is its stack item + 8, PROC.c:209 and :290; the players are born
// LARGE, VehBirth.c:35, the bots as well, BOTS.c:3134-3141), its instance an
// item on the taken list of the instance pool. Only then is a seat in the
// table, and only then may a hook read its driver or its model.
//
// No seat at all: without a game tracker, and right after a checkpoint
// restore until the next pull.
internal void NativeRenderLayer_ClearSeats(void)
{
	int seat;

	for (seat = 0; seat < NATIVE_RENDER_LAYER_DRIVERS; seat++)
	{
		s_nrSeatDriver[seat] = NULL;
		s_nrSeatInst[seat] = NULL;
		s_nrSeatState[seat] = NR_SEAT_NOT_READ;
	}
}

// Reads only: the loading state, gGT->drivers[], the instSelf of a driver that
// passed its pool, the two pools. Writes only the table. A run without any
// switch reads it too (the pull and the handler count run in every run); the
// table is host memory and nothing of the game is touched.
internal void NativeRenderLayer_ReadSeats(const struct GameTracker *gGT)
{
	int loading;
	int seat;

	if (gGT == NULL)
	{
		NativeRenderLayer_ClearSeats();
		return;
	}

	loading = (sdata->Loading.stage != LOAD_IDLE) || (((u32)gGT->gameMode1 & LOADING) != 0);

	for (seat = 0; seat < NATIVE_RENDER_LAYER_DRIVERS; seat++)
	{
		const struct Driver *driver;
		const struct Instance *inst;

		s_nrSeatDriver[seat] = NULL;
		s_nrSeatInst[seat] = NULL;

		if (loading)
		{
			s_nrSeatState[seat] = NR_SEAT_LOADING;
			continue;
		}

		driver = gGT->drivers[seat];
		if (driver == NULL)
		{
			s_nrSeatState[seat] = NR_SEAT_NO_DRIVER;
			continue;
		}
		if (NativeRenderLayer_PoolSlot(&gGT->JitPools.largeStack, (const char *)driver - 8) < 0)
		{
			s_nrSeatState[seat] = NR_SEAT_DRIVER_OUTSIDE;
			continue;
		}

		inst = driver->instSelf;
		if (inst == NULL)
		{
			s_nrSeatState[seat] = NR_SEAT_NO_INSTANCE;
			continue;
		}
		if ((NativeRenderLayer_PoolSlot(&gGT->JitPools.instance, inst) < 0) || !NativeRenderLayer_PoolTaken(&gGT->JitPools.instance, inst))
		{
			s_nrSeatState[seat] = NR_SEAT_INSTANCE_OUTSIDE;
			continue;
		}

		s_nrSeatDriver[seat] = driver;
		s_nrSeatInst[seat] = inst;
		s_nrSeatState[seat] = NR_SEAT_READY;
	}
}

// Why the probe seat of this frame cannot take the probe, NULL when it can.
// From the table of NativeRenderLayer_ReadSeats; the texts name the seat (for
// seat 0 they are the texts of before).
internal const char *NativeRenderLayer_ProbeSeatRefusal(void)
{
	const int seat = g_cfg_nativeProbeSeat;
	const char *before = NULL; // the words before "seat N"
	const char *after = "";    // and after it

	if ((seat < 0) || (seat >= NATIVE_RENDER_LAYER_DRIVERS))
	{
		return "no such seat";
	}

	switch (s_nrSeatState[seat])
	{
	case NR_SEAT_READY:
		break;
	case NR_SEAT_LOADING:
		return "loading";
	case NR_SEAT_NO_DRIVER:
		before = "no driver on ";
		break;
	case NR_SEAT_DRIVER_OUTSIDE:
		before = "";
		after = " driver outside its pool";
		break;
	case NR_SEAT_NO_INSTANCE:
		before = "no instance on ";
		break;
	case NR_SEAT_INSTANCE_OUTSIDE:
		before = "";
		after = " instance outside its pool";
		break;
	default:
		return "seats not read";
	}

	if ((before == NULL) && (s_nrSeatInst[seat]->model == NULL))
	{
		before = "no model on ";
	}
	if (before == NULL)
	{
		return NULL;
	}

	snprintf(s_nrProbeWhy, sizeof(s_nrProbeWhy), "%sseat %d%s", before, seat, after);
	return s_nrProbeWhy;
}

// The first reason why the probe is not bound this frame, NULL when it is.
// Only reads: the game tracker, the loading state, and two lookups of
// native_chars.c that write neither the game nor the log.
internal const char *NativeRenderLayer_ProbeRefusal(const struct GameTracker *gGT)
{
	const char *modeWhy;

	if (!g_cfg_nativePreview)
	{
		return "no native preview";
	}
	if (g_cfg_nativeProbe == NATIVE_PROBE_NONE)
	{
		return "no probe form";
	}
	if (!NativeRenderer_NativeProbeReady())
	{
		return "probe mesh not ready";
	}
	if (gGT == NULL)
	{
		return "no game tracker";
	}
	if (((u32)gGT->gameMode1 & MAIN_MENU) != 0)
	{
		return "main menu";
	}

	// One player and arcade only, the same funnel a custom character takes:
	// a second view or a decal tile would need what this stage leaves out.
	modeWhy = NativeChar_ModeRefusal(gGT);
	if (modeWhy != NULL)
	{
		return modeWhy;
	}

	// A custom character on the probe seat has a model of its own; the probe
	// does not take its place.
	if ((g_cfg_nativeProbeSeat >= 0) && (g_cfg_nativeProbeSeat < NATIVE_RENDER_LAYER_DRIVERS) && (NativeChar_SeatModel(g_cfg_nativeProbeSeat) != NULL))
	{
		snprintf(s_nrProbeWhy, sizeof(s_nrProbeWhy), "custom character on seat %d", g_cfg_nativeProbeSeat);
		return s_nrProbeWhy;
	}
	return NativeRenderLayer_ProbeSeatRefusal();
}

// Further down, with the matrix and the probe line of a native draw.
internal void NativeRenderLayer_ItemMatrix(const struct NrDrawItem *it, double ofsX, double ofsY, double S[4][4]);
internal void NativeRenderLayer_ProbeBoxLine(int vblank, const char *what, const struct NrDrawItem *it, const double S[4][4], double ofsX, double ofsY);

// THE EXHAUST LINE of the frame that is closed, only with --native-layer-report:
// the box of every exhaust quad of the probe seat in view 0, as game/Particle.c
// wrote their corners (gathered by NativeRenderLayer_NoteParticleQuad), plus the
// draw offset of the buffer the frame was drawn in - the offset the probe lines
// add as well, so both are in the same pixels of the internal picture at scale
// 1. One line per frame the probe was bound in, at most one per VBlank (a second
// frame closed at the same VBlank count gets none): "box none" without a quad,
// "box unknown" while the buffer has no draw offset yet (no native draw in it
// so far). The gathered box is emptied in any case.
//
// THE QUAD LINE, right after the hull line of the same VBlank and only then -
// the hull line stays word for word as it was:
//   [CTR RenderLayer] probe at vblank V: exhaust glow quads N: x0 y0 x1 y1; x0 y0 x1 y1; ...
// one "x0 y0 x1 y1" per quad, in the order game/Particle.c wrote them, rounded
// outward like the hull (floor of the smaller corner, ceil of the larger) in
// the same pixels with the same draw offset, so the hull is exactly the box
// around them. More than NR_EXHAUST_QUADS_LISTED quads: the first ones are
// listed, then "; (K not listed)". Without a quad "quads 0", while the offset
// is not known "quads N unknown" - each where the hull line says none or
// unknown.
internal void NativeRenderLayer_ExhaustQuadLine(int vblank, const struct NrExhaust *exhaust, int ofsKnown, double ofsX, double ofsY)
{
	char line[3072];
	size_t used;
	int listed;
	int q;

	if (exhaust->quads <= 0)
	{
		Platform_Log("[CTR RenderLayer] probe at vblank %d: exhaust glow quads 0\n", vblank);
		return;
	}
	if (!ofsKnown)
	{
		Platform_Log("[CTR RenderLayer] probe at vblank %d: exhaust glow quads %d unknown\n", vblank, exhaust->quads);
		return;
	}

	listed = (exhaust->quads < NR_EXHAUST_QUADS_LISTED) ? exhaust->quads : NR_EXHAUST_QUADS_LISTED;
	line[0] = '\0';
	used = 0;

	for (q = 0; q < listed; q++)
	{
		const int *b = exhaust->quadBox[q];
		const int written = snprintf(line + used, sizeof(line) - used, "%s%d %d %d %d", (q == 0) ? "" : "; ", (int)floor((double)b[0] + ofsX),
		                             (int)floor((double)b[1] + ofsY), (int)ceil((double)b[2] + ofsX), (int)ceil((double)b[3] + ofsY));

		if ((written < 0) || ((size_t)written >= (sizeof(line) - used)))
		{
			// Screen coordinates have at most five characters, so 96 quads take
			// at most 96 * 25 characters; should a line still run full (the log
			// takes 4096), the rest counts as not listed rather than cut in half.
			line[used] = '\0';
			listed = q;
			break;
		}
		used += (size_t)written;
	}

	if (listed < exhaust->quads)
	{
		Platform_Log("[CTR RenderLayer] probe at vblank %d: exhaust glow quads %d: %s; (%d not listed)\n", vblank, exhaust->quads, line,
		             exhaust->quads - listed);
	}
	else
	{
		Platform_Log("[CTR RenderLayer] probe at vblank %d: exhaust glow quads %d: %s\n", vblank, exhaust->quads, line);
	}
}

internal void NativeRenderLayer_ExhaustLine(void)
{
	const struct NrExhaust exhaust = s_nrExhaust;

	memset(&s_nrExhaust, 0, sizeof(s_nrExhaust));

	if (exhaust.quads > 0)
	{
		s_nrCount.exhaustFrames++;
	}

	if (!g_cfg_nativeLayerReport)
	{
		return;
	}

	{
		const int vblank = Platform_GetVBlankCount();

		if (vblank == s_nrExhaustLineVBlank)
		{
			return;
		}
		s_nrExhaustLineVBlank = vblank;

		if (exhaust.quads <= 0)
		{
			Platform_Log("[CTR RenderLayer] probe at vblank %d: exhaust glow box none\n", vblank);
			NativeRenderLayer_ExhaustQuadLine(vblank, &exhaust, 0, 0.0, 0.0);
		}
		else if ((exhaust.db < 0) || (exhaust.db > 1) || !s_nrLastOfsKnown[exhaust.db])
		{
			Platform_Log("[CTR RenderLayer] probe at vblank %d: exhaust glow box unknown\n", vblank);
			NativeRenderLayer_ExhaustQuadLine(vblank, &exhaust, 0, 0.0, 0.0);
		}
		else
		{
			const double ofsX = s_nrLastOfs[exhaust.db][0];
			const double ofsY = s_nrLastOfs[exhaust.db][1];

			Platform_Log("[CTR RenderLayer] probe at vblank %d: exhaust glow box %d %d %d %d\n", vblank,
			             (int)floor((double)exhaust.box[0] + ofsX), (int)floor((double)exhaust.box[1] + ofsY), (int)ceil((double)exhaust.box[2] + ofsX),
			             (int)ceil((double)exhaust.box[3] + ofsY));
			NativeRenderLayer_ExhaustQuadLine(vblank, &exhaust, 1, ofsX, ofsY);
		}
	}
}

// The bound frame before this one, now that it is over (its OT was parsed and
// drawn): the probe seat without a dispatch in view 0 is counted (hull cull,
// blinking, invisibility, a full OT - all decided in the queue, which this
// layer does not touch), and a frame on the line spacing that drew nothing
// native gets its probe line - with the fallback box when the route kept one.
// The VBlank count read here is the one the frame was shown at: nothing
// between the draw of the OT and the next pull counts a VBlank.
internal void NativeRenderLayer_CloseProbeFrame(void)
{
	const struct NrProbeFrame frame = s_nrProbeFrame;

	memset(&s_nrProbeFrame, 0, sizeof(s_nrProbeFrame));

	if (!frame.live)
	{
		// No queue, no exhaust line: what was gathered is dropped.
		memset(&s_nrExhaust, 0, sizeof(s_nrExhaust));
		return;
	}

	if (!frame.dispatched)
	{
		s_nrCount.probeSeat0NoDispatch++;
	}

	// The depth jump compares neighbouring native frames only: after a frame
	// without a native draw (a fallback, a respawn) a shift change would
	// compare depths frames apart.
	if (!frame.drawn)
	{
		s_nrDepthPrevValid = 0;
	}

	NativeRenderLayer_ExhaustLine();

	if (g_cfg_nativeLayerReport && !frame.drawn)
	{
		const int vblank = Platform_GetVBlankCount();
		const char *why = "no dispatch";

		if (((vblank % NATIVE_RENDER_LAYER_PROBE_LINE_STEP) != 0) || (vblank == s_nrProbeLineVBlank))
		{
			return;
		}

		if (frame.lastFallback != NULL)
		{
			why = frame.lastFallback;
		}
		else if (frame.routed)
		{
			why = "not drawn";
		}

		s_nrProbeLineVBlank = vblank;
		Platform_Log("[CTR RenderLayer] probe at vblank %d: retail (%s)\n", vblank, why);

		// A fallback with its box: a second line right after it carries the
		// rectangles a native line carries, so a check has the rectangle of
		// this very frame. The retail line above stays as it was.
		if (frame.fallbackBox && (frame.fallbackDb >= 0) && (frame.fallbackDb < 2))
		{
			if (s_nrLastOfsKnown[frame.fallbackDb])
			{
				const double ofsX = s_nrLastOfs[frame.fallbackDb][0];
				const double ofsY = s_nrLastOfs[frame.fallbackDb][1];
				double S[4][4];

				NativeRenderLayer_ItemMatrix(&s_nrFallbackItem, ofsX, ofsY, S);
				NativeRenderLayer_ProbeBoxLine(vblank, "fallback ", &s_nrFallbackItem, S, ofsX, ofsY);
			}
			else
			{
				Platform_Log("[CTR RenderLayer] probe at vblank %d: fallback box unknown\n", vblank);
			}
		}
	}
}

// The depth of the last native frame belongs to the object the probe was bound
// to: an unbinding or another model starts the comparison anew.
internal void NativeRenderLayer_ForgetDepth(void)
{
	s_nrDepthPrevValid = 0;
	s_nrDepthHaveLast = 0;
}

// Once per frame from the pull, before the queue, only with --native-probe:
// binds the probe to the model of the probe seat or unbinds it. Every change
// goes into the log once. Writes nothing but the statics of this file.
internal void NativeRenderLayer_BindProbe(const struct GameTracker *gGT)
{
	const char *why;

	NativeRenderLayer_CloseProbeFrame();

	why = NativeRenderLayer_ProbeRefusal(gGT);
	if (why != NULL)
	{
		if (s_nativeModelCount != 0)
		{
			Platform_Log("[CTR RenderLayer] probe unbound (%s)\n", why);

			// The probe lets go: the wheel poses start anew at the next binding,
			// so a restart never carries the old position or roll phase into
			// its first tick.
			NativeWheels_Forget();
		}
		s_probeModel = NULL;
		s_probeInst = NULL;
		s_nativeModelCount = 0;
		NativeRenderLayer_ForgetDepth();
		memset(&s_nrExhaust, 0, sizeof(s_nrExhaust));

		// No native object, so no native draw: the main target lets its depth
		// image go (nothing happens when it has none) - unless a custom
		// character is bound natively this frame (step 4c; 0 without one).
		NativeRenderer_WantNativeDepth(s_nrCharCount > 0);
		return;
	}

	{
		// The refusal above has passed the seat, so the table holds it.
		const struct Instance *inst = s_nrSeatInst[g_cfg_nativeProbeSeat];
		const struct Model *model = inst->model;

		if ((s_nativeModelCount == 0) || (model != s_probeModel))
		{
			Platform_Log("[CTR RenderLayer] probe bound to the seat %d model (id %d)\n", g_cfg_nativeProbeSeat, (int)model->id);
			NativeRenderLayer_ForgetDepth();
		}

		s_probeModel = model;
		s_probeInst = inst;
		s_nativeModelCount = 1;
	}

	// A native object is bound, so the main target needs its depth image -
	// asked for here, before the queue and before the ordering table of this
	// frame is drawn, so the first native draw already finds it. Without a
	// change nothing happens; see NativeRenderer_WantNativeDepth for when the
	// image is made and why that leaves every pixel as it was.
	NativeRenderer_WantNativeDepth(1);

	s_nrCount.probeBoundFrames++;

	// A frame without the queue dispatches nothing, so it is not looked at.
	s_nrProbeFrame.live = (gGT->renderFlags & RENDER_FLAG_RENDER_BUCKET) != 0;
}

// The reason a view of the probe model stays retail: counted, and kept for the
// probe line of the frame. Always 0, the answer of the route.
internal int NativeRenderLayer_ProbeFallback(enum NrProbeFallback reason)
{
	s_nrCount.probeFallback[reason]++;
	s_nrProbeFrame.lastFallback = s_nrProbeFallbackNames[reason];
	return 0;
}

internal enum NrProbeFallback NativeRenderLayer_HandlerFallback(int handlerIndex)
{
	switch (handlerIndex)
	{
	case NATIVE_RENDER_LAYER_HANDLER_NORMAL_ALT:
		return NR_PROBE_FALLBACK_NORMAL_ALT;
	case NATIVE_RENDER_LAYER_HANDLER_SPLIT:
		return NR_PROBE_FALLBACK_SPLIT;
	case NATIVE_RENDER_LAYER_HANDLER_SPECIAL:
		return NR_PROBE_FALLBACK_SPECIAL;
	case NATIVE_RENDER_LAYER_HANDLER_REFLECTION:
		return NR_PROBE_FALLBACK_REFLECTION;
	default:
		return NR_PROBE_FALLBACK_OTHER;
	}
}

internal s32 NativeRenderLayer_ClampS32(s32 value, s32 low, s32 high)
{
	if (value < low)
	{
		return low;
	}
	if (value > high)
	{
		return high;
	}
	return value;
}

// THE RETAIL WHEELS, for the probe line only: the bound model draws without
// them (NativeRenderLayer_ModelHidesWheels), the reference run with them, so
// the rectangle a check compares must hold them. Rebuilt from game/DrawTires.c
// in plain double arithmetic, as an upper bound rather than the exact quads:
// - wheel middles in kart space, DrawTiresSolid_BuildWheelLocalPairs
//   (DrawTires.c:338-366): x = +-(scale.x * 0x90) >> 12, y = (scale.y * 0x40)
//   >> 12, z = (scale.z * 0xc7) >> 12 in front and (scale.z * -0x60) >> 12 at
//   the back;
// - turned by the 4.12 rotation of inst->matrix and moved by the camera-relative
//   position times 4, DrawTiresSolid_SetupGteState (DrawTires.c:393-395, the
//   matrix words :403-407) and the first transform of
//   DrawTiresSolid_BuildWheelAxes (DrawTires.c:436-441);
// - the corners are that middle plus or minus two axes of length wheelSize / 64
//   each (unit vectors of 0x1000 times wheelSize >> 0x12, DrawTires.c:493-511;
//   wheelSize from the driver, :370), so every corner lies within
//   wheelSize / 64 * sqrt(2) of the middle; 2 units more cover the rounding;
// - projected with the view matrix of the push buffer, no translation, the
//   half view as offset and H, DrawTiresSolid_SetupProjectionState
//   (DrawTires.c:514-533) - the screen mapping of the body as well.
// A cube of that radius around each middle, carried into view space as the
// box of its image (half extent r * sum |VP[i][j]| / 4096 per axis), holds
// every corner; its eight projected corners bound the wheel on the screen.
// Steering and the hazard wobble only turn the rim inside that radius.
internal void NativeRenderLayer_FillWheels(struct NrDrawItem *it, const struct GameTracker *gGT, const struct Instance *inst, const struct PushBuffer *pb)
{
	const MATRIX *vp = &pb->matrix_ViewProj;
	const struct Driver *driver = NULL;
	double rel[3];
	double sideX;
	double upY;
	double frontZ;
	double backZ;
	double radius;
	int seat;
	int wheel;
	int r;
	int c;

	(void)gGT;

	// The driver only through the seats of this frame, which the pull checked.
	for (seat = 0; seat < NATIVE_RENDER_LAYER_DRIVERS; seat++)
	{
		if ((s_nrSeatInst[seat] != NULL) && (s_nrSeatInst[seat] == inst))
		{
			driver = s_nrSeatDriver[seat];
			break;
		}
	}
	if (driver == NULL)
	{
		it->wheels = 0;
		return;
	}

	sideX = (double)((inst->scale.x * 0x90) >> 12);
	upY = (double)((inst->scale.y * 0x40) >> 12);
	frontZ = (double)((inst->scale.z * 0xc7) >> 12);
	backZ = (double)((inst->scale.z * -0x60) >> 12);
	rel[0] = (double)RenderBucket_MipsSub(inst->matrix.t[0], pb->pos.x) * 4.0;
	rel[1] = (double)RenderBucket_MipsSub(inst->matrix.t[1], pb->pos.y) * 4.0;
	rel[2] = (double)RenderBucket_MipsSub(inst->matrix.t[2], pb->pos.z) * 4.0;
	radius = ((fabs((double)(s16)driver->wheelSize) / 64.0) * 1.4142135623730951) + 2.0;

	for (wheel = 0; wheel < 4; wheel++)
	{
		const double local[3] = {((wheel & 1) != 0) ? -sideX : sideX, upY, (wheel < 2) ? frontZ : backZ};
		double world[3];

		for (r = 0; r < 3; r++)
		{
			double sum = rel[r];

			for (c = 0; c < 3; c++)
			{
				sum += ((double)inst->matrix.m[r][c] * local[c]) / 4096.0;
			}
			world[r] = sum;
		}

		for (r = 0; r < 3; r++)
		{
			double sum = 0.0;

			for (c = 0; c < 3; c++)
			{
				sum += ((double)vp->m[r][c] * world[c]) / 4096.0;
			}
			it->wheelView[wheel][r] = (float)sum;
		}
	}

	for (r = 0; r < 3; r++)
	{
		double sum = 0.0;

		for (c = 0; c < 3; c++)
		{
			sum += fabs((double)vp->m[r][c]);
		}
		it->wheelExt[r] = (float)((radius * sum) / 4096.0);
	}

	it->wheels = 1;
}

// THE COMPOSED MODEL-VIEW MATRIX, in C: the view matrix of the push buffer
// times idpp->m3x3 (the rotation and scale of the instance, near scaling
// included, which the queue stores before it picks a handler). It is what
// RenderBucket_BuildMvp lets the GTE compute for the handler NORMAL - every
// column of m3x3 through the view matrix as the light matrix
// (Unknown_8006c558: three times MVMVA with sf = 1 and lm = 0) - done here
// with the same rounding: the sum of three products, shifted down by 12 as an
// arithmetic shift, held to the signed 16-bit range.
//
// Why it is needed: a view in a split frame keeps another matrix in idpp->mvp.
// RenderBucket_BuildSplitViewMvp stores the GTE light matrix there, which is
// the view matrix alone, and the split handler turns the vertices by m3x3
// first (the light matrix in RenderBucket_PrepareDrawContext). For the box of
// a split frame idpp->mvp is therefore not the model-view matrix, and this
// composition is. In every native draw it is compared with idpp->mvp, where
// both have to agree ("composed matrix off" in the report).
internal void NativeRenderLayer_ComposeModelView(const MATRIX *vp, const MATRIX *m3x3, s16 out[3][3])
{
	int r;
	int c;
	int k;

	for (r = 0; r < 3; r++)
	{
		for (c = 0; c < 3; c++)
		{
			s64 sum = 0;

			for (k = 0; k < 3; k++)
			{
				sum += (s64)vp->m[r][k] * (s64)m3x3->m[k][c];
			}

			// Shift by 12 rounding down, as the GTE does.
			sum = (sum >= 0) ? (sum / 4096) : -((-sum + 4095) / 4096);
			if (sum < -0x8000)
			{
				sum = -0x8000;
			}
			if (sum > 0x7fff)
			{
				sum = 0x7fff;
			}
			out[r][c] = (s16)sum;
		}
	}
}

// THE NATIVE WHEELS of one item, from the pose of the seat (platform/
// native_wheels.c) and the values the body is drawn with. Plain float
// arithmetic in C, only reading.
//
// THE MIDDLE goes through the matrix of the body (KONGRUENZ, idpp->mvp): the
// retail middle (NativeWheels_LocalMiddle, DrawTires.c:338-366, in the
// four-times units of DrawTires) in the vertex units of the model, which the
// queue maps with m3x3 = inst->matrix times the scale ((mh->scale >> 2) times
// inst->scale, RenderBucket_QueueExecute.c, the m3x3 build): retail turns
// inst->scale * D / 4096 by inst->matrix in four-times units, the queue turns
// g * (mh->scale / 16384) * (inst->scale / 4096) - so g = D * 4096 / mh->scale for
// every axis, and inst->scale (squash and stretch) moves the middles exactly as
// it moves the body. Then view = (mvp.m * g / 4096 + mvp.t) / 2^shift, the true
// view point as NativeRenderLayer_ItemMatrix has it.
//
// THE WHEEL around it keeps its size (DrawTires.c:351, 370): it is turned into
// the view by the view matrix and the rotation of the instance alone (the same
// two matrices DrawTires uses, DrawTires.c:393-407, 514-533), without the scale:
// view offset = vp * inst->matrix * (frame * size) / 4096^2, world units, with
// the frame of the wheel from the pose and size = half width, radius, radius.
internal void NativeRenderLayer_FillNativeWheels(struct NrDrawItem *it, const struct Instance *inst, const struct InstDrawPerPlayer *idpp,
                                                 const struct PushBuffer *pb)
{
	const struct NativeWheelPose *pose = NativeWheels_PoseOf(inst);
	const struct ModelHeader *mh = idpp->mh;
	const MATRIX *vp = &pb->matrix_ViewProj;
	const double s = ldexp(1.0, (int)it->mvpShift);
	double modelScale[3];
	double turn[3][3];
	double size[3];
	int wheel;
	int r;
	int c;
	int k;

	it->nativeWheels = 0;

	if ((pose == NULL) || (mh == NULL) || (pose->wheelSize == 0))
	{
		return;
	}

	modelScale[0] = (double)mh->scale.x;
	modelScale[1] = (double)mh->scale.y;
	modelScale[2] = (double)(u16)mh->scale.z;
	if ((modelScale[0] == 0.0) || (modelScale[1] == 0.0) || (modelScale[2] == 0.0))
	{
		return;
	}

	for (r = 0; r < 3; r++)
	{
		for (c = 0; c < 3; c++)
		{
			double sum = 0.0;

			for (k = 0; k < 3; k++)
			{
				sum += (double)vp->m[r][k] * (double)inst->matrix.m[k][c];
			}
			turn[r][c] = sum / (4096.0 * 4096.0);
		}
	}

	size[0] = NativeWheels_HalfWidth(pose);
	size[1] = NativeWheels_Radius(pose);
	size[2] = size[1];

	for (wheel = 0; wheel < NATIVE_WHEELS_COUNT; wheel++)
	{
		double d[3];
		double g[3];

		NativeWheels_LocalMiddle(wheel, d);
		for (c = 0; c < 3; c++)
		{
			g[c] = (d[c] * 4096.0) / modelScale[c];
		}

		// The exact middle: world offset = inst->matrix * (inst->scale * D /
		// 4096) / 4 / 4096 (DrawTires.c:338-366 and :393-397 without the
		// integer cuts), plus the exact camera-relative position, through vp.
		{
			const double scale[3] = {(double)inst->scale.x, (double)inst->scale.y, (double)inst->scale.z};
			double world[3];

			for (r = 0; r < 3; r++)
			{
				world[r] = (double)inst->matrix.t[r] - (double)((r == 0) ? pb->pos.x : ((r == 1) ? pb->pos.y : pb->pos.z));
				for (c = 0; c < 3; c++)
				{
					world[r] += ((double)inst->matrix.m[r][c] / 4096.0) * ((scale[c] * d[c]) / (4096.0 * 4.0));
				}
			}
			for (r = 0; r < 3; r++)
			{
				it->wheelExact[wheel][r] = 0.0;
				for (c = 0; c < 3; c++)
				{
					it->wheelExact[wheel][r] += ((double)vp->m[r][c] / 4096.0) * world[c];
				}
			}
		}

		for (r = 0; r < 3; r++)
		{
			double t = (double)it->mvpT[r];

			for (c = 0; c < 3; c++)
			{
				t += ((double)it->mvp[r][c] / 4096.0) * g[c];
			}
			it->wheelB[wheel][r] = t / s;

			for (c = 0; c < 3; c++)
			{
				double sum = 0.0;

				for (k = 0; k < 3; k++)
				{
					sum += turn[r][k] * pose->axis[wheel][k][c];
				}
				it->wheelA[wheel][r][c] = sum * size[c];
			}
		}
	}

	it->nativeWheels = 1;
}

// One item from the values of this instance view, as the handler NORMAL would
// have drawn it. Plain integer and float arithmetic in C; the frame helpers of
// the queue it calls only read the frame headers and shift words.
//
// THE ORIGIN, exactly as the hull of the queue has it (the bounds projection
// of RenderBucket_QueueExecute.c): the packed frame origin (averaged with the
// next frame when there is one), times 4, x and y as the two signed halves.
//
// THE SHIFT. The queue scales an instance nearer than view z 0x1000 up by 4 (in
// mvp.m and mvp.t) and quarters the translation of a DRAW_HUGE one (mvp.t
// only), and keeps no note of either. Dividing the whole view point by the one
// factor 2^shift leaves x / z and y / z as the retail model has them, so the
// screen position holds in both cases; only the depth is the scaled one.
// The view z is recomputed here the way the queue gets it - the camera-relative
// position times the third row of the view matrix, shifted by 12, held to the
// signed 16-bit range - and checked against mvp.t[2].
//
// THE CULL. det(mvp.m) >= 0 keeps the right-hand outward normal of the body
// facing out in a right-handed view space (x right, y down, z ahead); after the
// y flip of the screen mapping that is counter-clockwise = front. A negative
// determinant (a mirrored matrix) culls the other side instead.
internal int NativeRenderLayer_FillItem(struct NrDrawItem *it, const struct GameTracker *gGT, const struct Instance *inst, const struct InstDrawPerPlayer *idpp,
                                        const struct PushBuffer *pb, int view)
{
	const MATRIX *vp = &pb->matrix_ViewProj;
	const MATRIX *mvp = &idpp->mvp;
	const int slot = NativeRenderLayer_PoolSlot(&gGT->JitPools.instance, inst);
	u32 packedXY;
	u32 minXY;
	s32 minZ;
	s64 dx;
	s64 dy;
	s64 dz;
	s64 viewZ;
	s64 det;
	s32 expected;
	int notes = 0;
	int shift = 0;
	int r;
	int c;

	memset(it, 0, sizeof(*it));
	it->object = (slot >= 0) ? (u16)slot : (u16)0xFFFF;
	it->view = (u8)view;

	packedXY = RenderBucket_PackedFrameXY(idpp->ptrCurrFrame, idpp->ptrNextFrame);
	minXY = (u32)RenderBucket_MipsSll((int)packedXY, 2) & 0xfff8ffffu;
	minZ = RenderBucket_MipsSll(RenderBucket_FrameZ(idpp->ptrCurrFrame, idpp->ptrNextFrame), 2);
	it->hullMin[0] = (s16)(minXY & 0xffffu);
	it->hullMin[1] = (s16)(minXY >> 16);
	it->hullMin[2] = minZ;

	it->bodyHalf[0] = NATIVE_RENDER_LAYER_BODY_HALF_X;
	it->bodyHalf[1] = NATIVE_RENDER_LAYER_BODY_HEIGHT;
	it->bodyHalf[2] = NATIVE_RENDER_LAYER_BODY_HALF_Z;
	it->bodyCenter[0] = NativeRenderLayer_ClampS32(0, it->hullMin[0] + NATIVE_RENDER_LAYER_BODY_HALF_X,
	                                               it->hullMin[0] + NATIVE_RENDER_LAYER_HULL_SIZE - NATIVE_RENDER_LAYER_BODY_HALF_X);
	it->bodyCenter[1] = it->hullMin[1] + NATIVE_RENDER_LAYER_BODY_FLOOR;
	it->bodyCenter[2] = NativeRenderLayer_ClampS32(0, it->hullMin[2] + NATIVE_RENDER_LAYER_BODY_HALF_Z,
	                                               it->hullMin[2] + NATIVE_RENDER_LAYER_HULL_SIZE - NATIVE_RENDER_LAYER_BODY_HALF_Z);

	dx = (s64)RenderBucket_MipsSub(inst->matrix.t[0], pb->pos.x);
	dy = (s64)RenderBucket_MipsSub(inst->matrix.t[1], pb->pos.y);
	dz = (s64)RenderBucket_MipsSub(inst->matrix.t[2], pb->pos.z);
	viewZ = ((s64)vp->m[2][0] * dx) + ((s64)vp->m[2][1] * dy) + ((s64)vp->m[2][2] * dz);
	// Shift by 12 rounding down, as the arithmetic shift of the original does.
	viewZ = (viewZ >= 0) ? (viewZ / 4096) : -((-viewZ + 4095) / 4096);
	if (viewZ < -0x8000)
	{
		viewZ = -0x8000;
	}
	if (viewZ > 0x7fff)
	{
		viewZ = 0x7fff;
	}

	expected = (s32)viewZ;
	it->viewZ = (s32)viewZ;
	if (viewZ < 0x1000)
	{
		shift += 2;
		notes |= NR_ITEM_NEAR;
		it->nearView = 1;
		expected = RenderBucket_MipsSll(expected, 2);
	}
	if ((inst->flags & DRAW_HUGE) != 0)
	{
		shift -= 2;
		notes |= NR_ITEM_HUGE;
		expected >>= 2;
	}
	if (mvp->t[2] != expected)
	{
		notes |= NR_ITEM_SHIFT_OFF;
	}
	it->mvpShift = (s8)shift;

	for (r = 0; r < 3; r++)
	{
		for (c = 0; c < 3; c++)
		{
			it->mvp[r][c] = mvp->m[r][c];
		}
		it->mvpT[r] = mvp->t[r];
	}

	det = ((s64)mvp->m[0][0] * (((s64)mvp->m[1][1] * mvp->m[2][2]) - ((s64)mvp->m[1][2] * mvp->m[2][1]))) -
	      ((s64)mvp->m[0][1] * (((s64)mvp->m[1][0] * mvp->m[2][2]) - ((s64)mvp->m[1][2] * mvp->m[2][0]))) +
	      ((s64)mvp->m[0][2] * (((s64)mvp->m[1][0] * mvp->m[2][1]) - ((s64)mvp->m[1][1] * mvp->m[2][0])));
	it->cull = (u8)((det >= 0) ? NATIVE_GFX_CULL_BACK : NATIVE_GFX_CULL_FRONT);

	it->H = (s32)pb->distanceToScreen_PREV;
	it->rectW = pb->rect.w;
	it->rectH = pb->rect.h;

	NativeRenderLayer_FillWheels(it, gGT, inst, pb);

	// The native wheels only where they are drawn or measured.
	if (g_cfg_nativeProbeWheels || g_cfg_nativeWheelReport)
	{
		NativeRenderLayer_FillNativeWheels(it, inst, idpp, pb);
	}

	return notes;
}

// THE FALLBACK BOX of a split frame: the probe seat in view 0 stays retail, and
// the item is filled for the probe line only - the same values as a native draw
// would take, but with the composed model-view matrix in place of idpp->mvp,
// which in a split frame holds the view matrix alone (see
// NativeRenderLayer_ComposeModelView). Nothing is linked or drawn; the line is
// written when the frame is closed. Once per frame, writes only host statics.
internal void NativeRenderLayer_KeepFallbackBox(const struct GameTracker *gGT, const struct Instance *inst, const struct InstDrawPerPlayer *idpp,
                                                const struct PushBuffer *pb)
{
	struct NrDrawItem *it = &s_nrFallbackItem;

	if (s_nrProbeFrame.fallbackBox || (inst != s_probeInst) || (pb != &gGT->pushBuffer[0]) || (idpp->ptrCurrFrame == NULL) ||
	    ((inst->flags & SCREENSPACE_INSTANCE) != 0))
	{
		return;
	}

	(void)NativeRenderLayer_FillItem(it, gGT, inst, idpp, pb, 0);
	NativeRenderLayer_ComposeModelView(&pb->matrix_ViewProj, &idpp->m3x3, it->mvp);

	s_nrProbeFrame.fallbackBox = 1;
	s_nrProbeFrame.fallbackDb = s_nrMarkerDb;
	s_nrCount.probeFallbackBoxes++;
}

// THE POSE (form pose). The frame of an animation word as RenderBucket_GetFrame
// picks it: half-frame animations (numFrames with the top bit) halve the word,
// and a word past the last frame takes the last one.
internal int NativeRenderLayer_AnimFrameNumber(const struct ModelAnim *anim, u16 animFrame)
{
	int frameIndex = (int)animFrame;
	int lastFrame = (int)(anim->numFrames & 0x7fff) - 1;

	if ((s16)anim->numFrames < 0)
	{
		lastFrame >>= 1;
		frameIndex >>= 1;
	}
	if (frameIndex > lastFrame)
	{
		frameIndex = lastFrame;
	}
	return frameIndex;
}

internal const struct ModelAnim *NativeRenderLayer_AnimOf(const struct ModelHeader *mh, u32 animIndex)
{
	if ((mh == NULL) || (mh->ptrAnimations == NULL) || (mh->numAnimations == 0) || (animIndex >= mh->numAnimations))
	{
		return NULL;
	}
	return mh->ptrAnimations[animIndex];
}

// The frame number a frame pointer stands for, and in which animation of the
// header it lies: every animation is searched for the range of its frames
// (first frame, frameSize apart, up to the last frame RenderBucket_GetFrame may
// pick). -1 and -1 when the pointer lies in none of them; frame 0 and anim -1
// for a header without animations whose one frame it is.
internal void NativeRenderLayer_FrameOf(const struct ModelHeader *mh, const struct ModelFrame *frame, int *outAnim, int *outFrame)
{
	u32 a;

	*outAnim = -1;
	*outFrame = -1;

	if (mh == NULL)
	{
		return;
	}
	if (mh->ptrAnimations == NULL)
	{
		if (frame == mh->ptrFrameData)
		{
			*outFrame = 0;
		}
		return;
	}

	for (a = 0; a < mh->numAnimations; a++)
	{
		const struct ModelAnim *anim = mh->ptrAnimations[a];
		const u8 *first;
		u32 size;
		int lastFrame;

		if (anim == NULL)
		{
			continue;
		}

		first = (const u8 *)anim + sizeof(struct ModelAnim);
		size = (u32)(u16)anim->frameSize;
		lastFrame = NativeRenderLayer_AnimFrameNumber(anim, 0xFFFFu);
		if ((size == 0) || (lastFrame < 0) || ((const u8 *)frame < first))
		{
			continue;
		}

		{
			const u32 offset = (u32)((const u8 *)frame - first);

			if (((offset % size) == 0) && ((offset / size) <= (u32)lastFrame))
			{
				*outAnim = (int)a;
				*outFrame = (int)(offset / size);
				return;
			}
		}
	}
}

// The distance on the screen between the origin through a float position and
// through mvp.t / 2^shift, in pixels at scale 1; -1 when either lies behind the
// eye. Both projected with H; the offsets of the split are the same for both
// and drop out.
internal double NativeRenderLayer_FloatDistance(const struct NrDrawItem *it, const struct PushBuffer *pb, const double position[3])
{
	const MATRIX *vp = &pb->matrix_ViewProj;
	const double s = ldexp(1.0, (int)it->mvpShift);
	const double H = (double)it->H;
	const double rel[3] = {position[0] - (double)pb->pos.x, position[1] - (double)pb->pos.y, position[2] - (double)pb->pos.z};
	double f[3];
	double k[3];
	int r;
	int c;

	for (r = 0; r < 3; r++)
	{
		f[r] = 0.0;
		for (c = 0; c < 3; c++)
		{
			f[r] += ((double)vp->m[r][c] * rel[c]) / 4096.0;
		}
		k[r] = (double)it->mvpT[r] / s;
	}

	if ((f[2] <= 0.0) || (k[2] <= 0.0))
	{
		return -1.0;
	}

	{
		const double dx = H * ((f[0] / f[2]) - (k[0] / k[2]));
		const double dy = H * ((f[1] / f[2]) - (k[1] / k[2]));

		return sqrt((dx * dx) + (dy * dy));
	}
}

// The pose of one native view and, beside it, the FLOAT measurement. Reads only
// what the queue read or wrote for this view (idpp->mh, the frame pointer,
// mvp.t, the instance) and the table of the pull.
//
// DRAWN: animation and frame of the animation word the pull took before the
// queue - the word the queue drew from, before
// RenderBucket_AdvanceInstanceAnimWord moved it on. CURRENT: the animation and
// frame idpp->ptrCurrFrame lies in, found by its address alone. Both the same
// is what a pose from the pull has to give.
//
// WORD: whether the animation word of the instance at this point (after the
// queue) differs from the pulled one. Where it never does, the comparison above
// cannot tell a pose from the pull from one read here - the counter says
// whether this run could have caught the late read at all.
//
// FLOAT: the origin of the object through a float position, against KONGRUENZ
// (mvp.t / 2^shift, the origin as the queue placed it): once from the state of
// the pull (a driver's posCurr / 256, else matrix.t), once from inst->matrix.t,
// the integer position the queue itself started from. The second shows the
// arithmetic of the two ways alone, the first adds what the sources differ by.
internal void NativeRenderLayer_FillPose(struct NrDrawItem *it, const struct Instance *inst, const struct InstDrawPerPlayer *idpp, const struct PushBuffer *pb)
{
	const struct ModelHeader *mh = idpp->mh;
	const struct NrObject *obj = NULL;
	int currentAnim = -1;
	int currentFrame = -1;

	it->poseDrawn = -1;
	it->poseCurrent = -1;
	it->poseDrawnAnim = -1;
	it->poseCurrentAnim = -1;
	it->posePull = 0;
	it->poseWordDiffers = 0;
	it->floatKnown = 0;
	it->floatDistance = 0.0;
	it->floatDistanceT = 0.0;

	if ((it->object < NATIVE_RENDER_LAYER_SLOTS) && s_nrObjects[it->object].live && (s_nrObjects[it->object].inst == inst))
	{
		obj = &s_nrObjects[it->object];
	}

	// Current, from the frame pointer.
	NativeRenderLayer_FrameOf(mh, idpp->ptrCurrFrame, &currentAnim, &currentFrame);
	it->poseCurrent = (s16)currentFrame;
	it->poseCurrentAnim = (s16)currentAnim;

	if (obj == NULL)
	{
		return;
	}

	// Drawn, from the pull.
	{
		const struct ModelAnim *pulled = NativeRenderLayer_AnimOf(mh, (u32)obj->curr.animIndex);

		it->posePull = 1;
		if (pulled != NULL)
		{
			it->poseDrawn = (s16)NativeRenderLayer_AnimFrameNumber(pulled, obj->curr.animFrame);
			it->poseDrawnAnim = (s16)obj->curr.animIndex;
		}
		else if ((mh != NULL) && (mh->ptrAnimations == NULL))
		{
			it->poseDrawn = 0;
		}

		it->poseWordDiffers = (((u16)inst->animIndex != obj->curr.animIndex) || ((u16)inst->animFrame != obj->curr.animFrame)) ? 1 : 0;
	}

	// FLOAT against KONGRUENZ, for the origin, from both sources.
	{
		const double fromPull[3] = {(double)obj->curr.pos[0], (double)obj->curr.pos[1], (double)obj->curr.pos[2]};
		const double fromMatrix[3] = {(double)inst->matrix.t[0], (double)inst->matrix.t[1], (double)inst->matrix.t[2]};
		const double pullDistance = NativeRenderLayer_FloatDistance(it, pb, fromPull);
		const double matrixDistance = NativeRenderLayer_FloatDistance(it, pb, fromMatrix);

		if ((pullDistance >= 0.0) && (matrixDistance >= 0.0))
		{
			it->floatDistance = pullDistance;
			it->floatDistanceT = matrixDistance;
			it->floatKnown = 1;
		}
	}
}

// THE FLOAT LINE of --native-wheel-report, once in a frame whose pull saw a new
// tick, for the item of view 0: the pose of the tick and the four float wheel
// middles through the matrix of the body, projected as the GTE projects the
// retail wheel corners - half the view plus H x / z, without the draw offset of
// the split (DrawTires.c:514-533: OFX = rect.w << 15, OFY = rect.h << 15, H),
// so the line beside it (NativeRenderLayer_TiresEnd) is in the same pixels.
internal void NativeRenderLayer_WheelFloatLine(const struct NrDrawItem *it, const struct Instance *inst)
{
	const struct NativeWheelPose *pose = NativeWheels_PoseOf(inst);
	const double H = (double)it->H;
	const double ofx = (double)it->rectW / 2.0;
	const double ofy = (double)it->rectH / 2.0;
	char move[32];
	char wheels[4][48];
	char exact[4][48];
	int wheel;

	if (!g_cfg_nativeWheelReport || !s_nrWheelSample || s_nrWheelFloatDone || (pose == NULL))
	{
		return;
	}
	s_nrWheelFloatDone = 1;
	s_nrCount.wheelFloatLines++;

	if (pose->moveKnown)
	{
		snprintf(move, sizeof(move), "%.4f", pose->move);
	}
	else
	{
		snprintf(move, sizeof(move), "none");
	}

	for (wheel = 0; wheel < NATIVE_WHEELS_COUNT; wheel++)
	{
		const double *b = it->wheelB[wheel];
		const double *e = it->wheelExact[wheel];

		if (b[2] <= 0.0)
		{
			snprintf(wheels[wheel], sizeof(wheels[wheel]), "behind");
		}
		else
		{
			snprintf(wheels[wheel], sizeof(wheels[wheel]), "%.3f %.3f", ofx + ((H * b[0]) / b[2]), ofy + ((H * b[1]) / b[2]));
		}

		if (e[2] <= 0.0)
		{
			snprintf(exact[wheel], sizeof(exact[wheel]), "behind");
		}
		else
		{
			snprintf(exact[wheel], sizeof(exact[wheel]), "%.3f %.3f", ofx + ((H * e[0]) / e[2]), ofy + ((H * e[1]) / e[2]));
		}
	}

	// The line of the pose and the KONGRUENZ middles (as the wheels are drawn),
	// its last field the sign class of the tick (platform/native_wheels.h).
	Platform_Log("[CTR RenderLayer] wheel float at vblank %d tick %u: speed %d, move %s, roll step %.6f, roll %.6f, steer %d, "
	             "w0 %s, w1 %s, w2 %s, w3 %s, sign %s\n",
	             Platform_GetVBlankCount(), s_nrWheelSampleTimer, (int)pose->speed, move, pose->rollStep, pose->roll, (int)pose->wheelRotation,
	             wheels[0], wheels[1], wheels[2], wheels[3], NativeWheels_SignName(pose->sign));

	// The same middles with the exact translation, the ones DE4 is measured on.
	s_nrCount.wheelExactLines++;
	Platform_Log("[CTR RenderLayer] wheel exact at vblank %d tick %u: w0 %s, w1 %s, w2 %s, w3 %s\n", Platform_GetVBlankCount(), s_nrWheelSampleTimer,
	             exact[0], exact[1], exact[2], exact[3]);
}

// The probe part of the route, for an instance view of the bound model. 1 =
// native: the item is in the list and its marker in the own OT range, and the
// retail handler is skipped. 0 = retail, with the first reason counted.
internal int NativeRenderLayer_RouteProbe(const struct Instance *inst, const struct InstDrawPerPlayer *idpp, const struct PushBuffer *pb)
{
	const struct GameTracker *gGT = sdata->gGT;
	const int db = s_nrMarkerDb;
	struct NrDrawItem *it;
	int view = -1;
	int index;
	int notes;
	int v;

	if (gGT == NULL)
	{
		return 0;
	}

	// The probe seat in view 0 got this far: the queue did not cull it.
	if ((inst == s_probeInst) && (pb == &gGT->pushBuffer[0]))
	{
		s_nrProbeFrame.dispatched = 1;
	}

	// Split, reflection and the rest need a second range and a clip plane, and
	// their mvp is not the model-view matrix. A split frame of the probe seat
	// in view 0 still keeps its box for the probe line.
	if ((u32)idpp->unkEC != (u32)RB_RETAIL_DRAWFUNC_NORMAL)
	{
		const int handler = NativeRenderLayer_HandlerIndex((u32)idpp->unkEC);

		if (handler == NATIVE_RENDER_LAYER_HANDLER_SPLIT)
		{
			NativeRenderLayer_KeepFallbackBox(gGT, inst, idpp, pb);
		}
		return NativeRenderLayer_ProbeFallback(NativeRenderLayer_HandlerFallback(handler));
	}

	// Only the normal primitive writer; 1 (depth fade) and 6 (ghost) change
	// how retail draws.
	if (((inst->flags >> 16) & 7) != 0)
	{
		return NativeRenderLayer_ProbeFallback(NR_PROBE_FALLBACK_WRITER);
	}

	// A multiplayer decal view draws into an offscreen OT for a VRAM tile.
	if ((idpp->instFlags & PUSHBUFFER_EXISTS) != 0)
	{
		return NativeRenderLayer_ProbeFallback(NR_PROBE_FALLBACK_DECAL_VIEW);
	}

	// The queue gave this view no range of its own (borrowed or stale).
	if (((u32)idpp->instFlags & RB_INSTANCE_SKIP_OT_RANGE) != 0)
	{
		return NativeRenderLayer_ProbeFallback(NR_PROBE_FALLBACK_BORROWED_RANGE);
	}

	// The UI OT, a screen-space instance, or a push buffer that is none of the
	// four views (there is no view of it to draw into).
	for (v = 0; v < NATIVE_RENDER_LAYER_VIEWS; v++)
	{
		if (pb == &gGT->pushBuffer[v])
		{
			view = v;
			break;
		}
	}
	if ((view < 0) || ((inst->flags & SCREENSPACE_INSTANCE) != 0))
	{
		return NativeRenderLayer_ProbeFallback(NR_PROBE_FALLBACK_UI);
	}

	// No own range. A missing frame counts here as well: PrepareDrawContext
	// leaves before the dispatch without one, so it does not come up.
	if ((idpp->otRangeNormal == 0) || (idpp->ptrCurrFrame == NULL))
	{
		return NativeRenderLayer_ProbeFallback(NR_PROBE_FALLBACK_NO_RANGE);
	}

	if (s_nrItemCount[db] >= (g_cfg_nativeProbeWheels ? (NATIVE_LAYER_POSE_SLOTS / NR_WHEEL_REGIONS) : NATIVE_RENDER_LAYER_ITEMS))
	{
		return NativeRenderLayer_ProbeFallback(NR_PROBE_FALLBACK_ITEM_LIST_FULL);
	}

	// The item first, then the marker that points at it; without the marker
	// the item is taken back.
	index = s_nrItemCount[db];
	it = &s_nrItems[db][index];
	notes = NativeRenderLayer_FillItem(it, gGT, inst, idpp, pb, view);
	if (g_cfg_nativeProbe == NATIVE_PROBE_POSE)
	{
		NativeRenderLayer_FillPose(it, inst, idpp, pb);
	}
	s_nrItemCount[db]++;

	if (!NativeRenderLayer_LinkMarker(idpp, (u32)index, 0))
	{
		s_nrItemCount[db]--;
		return NativeRenderLayer_ProbeFallback(NR_PROBE_FALLBACK_ARENA_FULL);
	}

	if ((notes & NR_ITEM_NEAR) != 0)
	{
		s_nrCount.probeShiftNear++;
	}
	else
	{
		s_nrCount.probeShiftFar++;
	}
	if ((notes & NR_ITEM_HUGE) != 0)
	{
		s_nrCount.probeShiftHuge++;
	}
	if ((notes & NR_ITEM_SHIFT_OFF) != 0)
	{
		s_nrCount.probeShiftCheckOff++;
	}
	if (it->cull != (u8)NATIVE_GFX_CULL_BACK)
	{
		s_nrCount.probeMirrored++;
	}

	// The composition the fallback box relies on, checked where idpp->mvp is
	// the model-view matrix.
	{
		s16 composed[3][3];

		NativeRenderLayer_ComposeModelView(&pb->matrix_ViewProj, &idpp->m3x3, composed);
		if (memcmp(composed, it->mvp, sizeof(composed)) != 0)
		{
			s_nrCount.probeComposedOff++;
		}
	}

	if (it->nativeWheels)
	{
		s_nrCount.wheelItems++;
		if (view == 0)
		{
			NativeRenderLayer_WheelFloatLine(it, inst);
		}
	}

	s_nrProbeFrame.routed = 1;
	return 1;
}

// --- Step 4c: the custom characters drawn natively -------------------------
//
// THE BINDING (NativeRenderLayer_BindChars, every frame from the pull, before
// the probe's): every seat whose custom model is bound (NativeChar_SeatModel)
// and whose instance carries it, with a ready GPU set (platform/
// native_char_gpu.c), puts its model into s_nrChar - a model the dev seats
// share only once. The route then takes every view of such a model through
// the same gates as the probe (NativeRenderLayer_RouteChar); a view that
// cannot be taken stays retail - its CMDL with its retail wheels - with the
// first reason counted. Without --native-preview, without a ready set, or at
// factor 1 (NativeRenderLayer_CharFactorAllows), s_nrCharCount stays 0
// and every hook leaves at its first comparison.
//
// THE PROBE STAYS AS IT WAS. None of its functions is changed for this; the
// char items have their own matrix (NativeRenderLayer_CharItemMatrix), draw
// (NativeRenderLayer_DrawCharItem), lines and counters. Both share the item
// list, the markers, FillItem, FillPose and the depth clear of a view.

struct NrChar
{
	const struct Model *model;
	const struct NativeCharGpu *gpu;
	u8 twin; // the retail twin of seat 0 (step 4d), not a custom model
};

internal struct NrChar s_nrChar[NATIVE_RENDER_LAYER_DRIVERS];
internal int s_nrCharDepthWanted = 0;

// The binding as last logged, per seat.
internal u8 s_nrCharSeatBound[NATIVE_RENDER_LAYER_DRIVERS];

// THE STAMPS of the retail wheels (game/DrawTires.c, NativeChar_ViewHidesWheels):
// the frame a view of the instance in a pool slot was routed natively in, and
// whether that item drew native wheels.
internal u32 s_nrCharViewFrame[NATIVE_RENDER_LAYER_SLOTS][NATIVE_RENDER_LAYER_VIEWS];
internal u8 s_nrCharViewWheels[NATIVE_RENDER_LAYER_SLOTS][NATIVE_RENDER_LAYER_VIEWS];

// The draws per view and frame of the chars, apart from the probe's.
internal u32 s_nrCharViewDrawFrame[NATIVE_RENDER_LAYER_VIEWS];
internal int s_nrCharViewDraws[NATIVE_RENDER_LAYER_VIEWS];
internal u32 s_nrCharDrawFrame = 0;

// One bound frame per seat, looked at when the next frame starts (the retail
// line of the box report).
struct NrCharSeatFrame
{
	int live;
	int dispatched;
	int routed;
	int drawn;
	const char *lastFallback;
};

internal struct NrCharSeatFrame s_nrCharSeatFrame[NATIVE_RENDER_LAYER_DRIVERS];

// The seam per seat (view 0): as the probe's, with neighbouring native frames
// told by the layer's frame number.
struct NrCharSeam
{
	unsigned long long nearDraws;
	unsigned long long farDraws;
	unsigned long long hugeDraws;
	unsigned long long shiftOff;
	unsigned long long mirrored;
	unsigned long long composedOff;
	unsigned long long changes;
	unsigned long long nearToFar;
	unsigned long long farToNear;
	unsigned long long afterGap;
	unsigned long long stepPairs;
	unsigned long long view0Draws;
	double jumpMax;
	int jumpVBlank;
	double jumpW0;
	double jumpW1;
	double stepMax;
	u32 lastFrame;
	int haveLast;
	int lastShift;
	int lastNear;
	double lastW;
	int seamLineVBlank;
	int boxLineVBlank;
};

internal struct NrCharSeam s_nrCharSeam[NATIVE_RENDER_LAYER_DRIVERS];

struct NrCharCounters
{
	unsigned long long boundFrames;
	unsigned long long draws;
	unsigned long long drawFrames;
	int mostPerView;
	unsigned long long notDrawn;
	unsigned long long fallback[NR_PROBE_FALLBACKS];
	unsigned long long poseDrawn;
	unsigned long long poseMatched;
	unsigned long long poseMismatched;
	unsigned long long poseWithoutPull;
	unsigned long long poseStill;
	unsigned long long poseOutOfRange;
	unsigned long long wheelItems;
	unsigned long long wheelDraws;
	unsigned long long wheelNotDrawn;
	unsigned long long tiresNative;
	unsigned long long tiresFallback;
};

internal struct NrCharCounters s_nrCharCnt;

// STEP 4F, several native seats: the counters per seat, and per frame the
// native draws of view 0 against the views of view 0 the route took (handler
// NORMAL) - in every frame both are the same number.
struct NrCharSeatCounters
{
	unsigned long long dispatched;   // view 0, any handler
	unsigned long long routedNormal; // view 0, taken by the route
	unsigned long long draws;        // every view
	unsigned long long notDrawn;
	unsigned long long fallback[NR_PROBE_FALLBACKS];
};

internal struct NrCharSeatCounters s_nrCharSeatCnt[NATIVE_RENDER_LAYER_DRIVERS];

#define NR_CHAR_FRAME_DRAWS_LISTED 8

internal struct
{
	int live;
	int routed;
	int drawn;
} s_nrCharFrame0;

internal unsigned long long s_nrCharFramesByDraws[NR_CHAR_FRAME_DRAWS_LISTED + 1];
internal unsigned long long s_nrCharFramesDrawsOff;

// STEP 4E-1, the water line (SPLIT), first part: what the views of a custom
// character that fall back on the handler SPLIT carry - the side selector of
// the instance (inst->funcPtr[2], one of four retail labels,
// RenderBucket_ApplyWaterSplitSideSelector), its shift and mask
// (specLightX & 31, reflectionRGBA) and whether the queue gave the view a
// second OT range of its own. Counted in the fallback, so the first run names
// the case a native water line has to rebuild. Nothing is drawn natively for
// SPLIT yet ("accepted" stays 0).
// --native-split-report (main.c, only with --dev): the raw values of the
// water line of seat 0 (step 4e-1b), one line per SPLIT view and per SPECIAL
// view with SPLIT_LINE - the first four of the run, then on the line spacing
// of 30 VBlanks, at most one per VBlank. Only reading.
int g_cfg_nativeSplitReport = 0;
internal int s_nrSplitLines = 0;
internal int s_nrSplitLineVBlank = -1;

internal struct
{
	unsigned long long views;
	unsigned long long accepted;
	unsigned long long bothMask;
	unsigned long long negative;
	unsigned long long xorSide;
	unsigned long long dimXor;
	unsigned long long otherSelector;
	unsigned long long maskOff;
	unsigned long long shift1;
	unsigned long long shiftOther;
	unsigned long long secondaryOwn;
	unsigned long long secondaryShared;
} s_nrCharSplit;

// Step 5a: the driver select preview bound natively (NativeRenderLayer_BindPreview).
internal int s_nrPreviewBound = 0;
internal int s_nrPreviewBoundEntry = -1;

// The entry whose reason for not binding has been logged (once per tile and
// visit of the driver select), -1 for none.
internal int s_nrPreviewWhyEntry = -1;

internal struct
{
	unsigned long long frames;        // frames the preview was bound in
	unsigned long long outsideSelect; // of them while the driver select did not run (0 by construction)
	unsigned long long boxOutsideClip; // native preview draws whose body box left the split's clip
	unsigned long long raceFrames;    // frames with a bound race seat (4c)
	unsigned long long refusedAdventure;
	unsigned long long refusedCutscene;
	unsigned long long refusedOther;
} s_nrPreviewCnt;

// The VBlank of the last probe seam line (one per VBlank).
internal int s_nrProbeSeamVBlank = -1;

// The retail wheels of a char instance between TiresBegin and TiresEnd.
internal int s_nrCharTiresActive = 0;
internal int s_nrCharTiresNative = 0;

internal int NativeRenderLayer_CharViewStamp(const struct Instance *inst, const struct PushBuffer *pb, int *wheels);

// The seat of an instance among the seats of this frame, -1 for none.
internal int NativeRenderLayer_SeatOfInst(const struct Instance *inst)
{
	int seat;

	for (seat = 0; seat < NATIVE_RENDER_LAYER_DRIVERS; seat++)
	{
		if ((s_nrSeatInst[seat] != NULL) && (s_nrSeatInst[seat] == inst))
		{
			return seat;
		}
	}
	return -1;
}

internal int NativeRenderLayer_CharIndex(const struct Model *model)
{
	int k;

	for (k = 0; k < s_nrCharCount; k++)
	{
		if (s_nrChar[k].model == model)
		{
			return k;
		}
	}
	return -1;
}

// The frame before, now over: a bound seat whose frame drew nothing native
// gets its retail line on the line spacing (--native-layer-report).
internal void NativeRenderLayer_CloseCharFrames(void)
{
	const int vblank = Platform_GetVBlankCount();
	int seat;

	// View 0 of the frame before (step 4f): native draws against the views the
	// route took.
	if (s_nrCharFrame0.live)
	{
		const int drawn = s_nrCharFrame0.drawn;

		s_nrCharFramesByDraws[(drawn > NR_CHAR_FRAME_DRAWS_LISTED) ? NR_CHAR_FRAME_DRAWS_LISTED : drawn]++;
		if (drawn != s_nrCharFrame0.routed)
		{
			s_nrCharFramesDrawsOff++;
		}
	}
	memset(&s_nrCharFrame0, 0, sizeof(s_nrCharFrame0));

	for (seat = 0; seat < NATIVE_RENDER_LAYER_DRIVERS; seat++)
	{
		const struct NrCharSeatFrame frame = s_nrCharSeatFrame[seat];
		struct NrCharSeam *seam = &s_nrCharSeam[seat];

		memset(&s_nrCharSeatFrame[seat], 0, sizeof(s_nrCharSeatFrame[seat]));
		if (!frame.live || frame.drawn || !g_cfg_nativeLayerReport)
		{
			continue;
		}
		if (((vblank % NATIVE_RENDER_LAYER_PROBE_LINE_STEP) != 0) || (vblank == seam->boxLineVBlank))
		{
			continue;
		}
		seam->boxLineVBlank = vblank;
		Platform_Log("[CTR RenderLayer] native char seat %d at vblank %d: retail (%s)\n", seat, vblank,
		             (frame.lastFallback != NULL) ? frame.lastFallback : (frame.routed ? "not drawn" : (frame.dispatched ? "not routed" : "no dispatch")));
	}
}

// --- The factor rule: native characters from factor 2 on ----------------------
//
// THE RULE. A custom character (the race seats of 4c/4f and the driver select
// preview of 5a) is drawn natively only while the internal resolution is
// factor 2 or more. At factor 1 its seat is not bound: the model stays out of
// s_nrChar, so the route answers 0 for every view of it and the retail handler
// draws its CMDL with its retail wheels, exactly as without --native-preview.
// The probe (--native-probe, every form) and the twin (--native-twin) are
// measuring tools and do not ask this rule.
//
// THE FACTOR, read once per frame in BindChars (the first binding of the
// pull): the factor in force (NativeRenderer_GetEffectiveResolutionScale), the
// one the main target is allocated at. It is a setting (--res-scale,
// ctr-settings.cfg, and at run time the INT RES rows of the debug menu's video
// page), so it can change in the middle of a race; the binding follows from the
// next pull on. The uploads do not ask the rule: the seat sets are made at the
// loading screen and the preview set at the cursor step whatever the factor
// is, so a later change of the factor never needs an upload in a race frame.
//
// THE NATIVE POSITION is not a factor: the target is the window's own pixel
// grid. Its factor here is how many whole target pixels one game pixel gets -
// the target size over the canvas width and over the display height, the
// smaller of the two, rounded down, at least 1. A window less than twice the
// game's size is therefore factor 1 and draws by CMDL. The window can change
// size at run time; the binding follows like for a changed setting.
//
// THE LINES. One line whenever the factor the rule sees changes (and the first
// time it decides a binding): "native chars off at factor 1" or "on at factor
// N". Under --native-layer-report the frames in which the rule kept a seat or
// the preview off are counted.

int CTR_Canvas_ActiveWidth(void);

internal int s_nrCharFactor = 1;          // this frame's, read in BindChars
internal int s_nrCharFactorNative = 0;    // 1 at the native position
internal int s_nrCharFactorTargetW = 0;
internal int s_nrCharFactorTargetH = 0;
internal int s_nrCharFactorLoggedKey = 0; // factor of the last line (+ 100 at the native position), 0 = no line yet
internal int s_nrCharFactorHeldNow = 0;   // this frame already counted

internal struct
{
	unsigned long long frames;        // frames in which the rule kept a seat or the preview off
	unsigned long long seatFrames;    // seats kept off, summed over the frames
	unsigned long long previewFrames; // frames with the preview kept off
} s_nrCharFactorCnt;

// The factor the rule reads, from its inputs alone (no state): the factor in
// force, or at the native position the whole target pixels per game pixel.
internal int NativeRenderLayer_RuleFactor(int atNative, int effective, int targetW, int targetH, int canvasW, int displayH)
{
	int byWidth;
	int byHeight;
	int factor;

	if (!atNative)
	{
		return (effective < 1) ? 1 : effective;
	}
	if ((canvasW < 1) || (displayH < 1))
	{
		return 1;
	}
	byWidth = targetW / canvasW;
	byHeight = targetH / displayH;
	factor = (byWidth < byHeight) ? byWidth : byHeight;
	return (factor < 1) ? 1 : factor;
}

// Once per frame, before the first binding: this frame's factor.
internal void NativeRenderLayer_ReadCharFactor(void)
{
	int displayH = activeDispEnv.disp.h;

	s_nrCharFactorHeldNow = 0;
	s_nrCharFactorNative = NativeRenderer_ResolutionIsNative();
	if (!s_nrCharFactorNative)
	{
		s_nrCharFactor = NativeRenderLayer_RuleFactor(0, NativeRenderer_GetEffectiveResolutionScale(), 0, 0, 0, 0);
		s_nrCharFactorTargetW = 0;
		s_nrCharFactorTargetH = 0;
		return;
	}

	// The display height as the renderer takes it (the draw clip while no
	// display environment exists yet).
	if (displayH <= 0)
	{
		displayH = activeDrawEnv.clip.h;
	}
	NativeRenderer_GetMainTargetSize(&s_nrCharFactorTargetW, &s_nrCharFactorTargetH);
	s_nrCharFactor = NativeRenderLayer_RuleFactor(1, 0, s_nrCharFactorTargetW, s_nrCharFactorTargetH, CTR_Canvas_ActiveWidth(), displayH);
}

// Asked by a seat or the preview that passed every other gate: 1 = bind it
// natively, 0 = factor 1, leave it to the CMDL (counted).
internal int NativeRenderLayer_CharFactorAllows(int preview)
{
	const int on = (s_nrCharFactor >= 2);
	const int key = s_nrCharFactor + (s_nrCharFactorNative ? 100 : 0);

	if (key != s_nrCharFactorLoggedKey)
	{
		s_nrCharFactorLoggedKey = key;
		if (s_nrCharFactorNative)
		{
			Platform_Log("[CTR RenderLayer] native chars %s at factor %d (NATIVE, target %dx%d)%s at vblank %d\n", on ? "on" : "off", s_nrCharFactor,
			             s_nrCharFactorTargetW, s_nrCharFactorTargetH, on ? "" : " (drawn by CMDL)", Platform_GetVBlankCount());
		}
		else
		{
			Platform_Log("[CTR RenderLayer] native chars %s at factor %d%s at vblank %d\n", on ? "on" : "off", s_nrCharFactor, on ? "" : " (drawn by CMDL)",
			             Platform_GetVBlankCount());
		}
	}

	if (on)
	{
		return 1;
	}
	if (!s_nrCharFactorHeldNow)
	{
		s_nrCharFactorHeldNow = 1;
		s_nrCharFactorCnt.frames++;
	}
	if (preview)
	{
		s_nrCharFactorCnt.previewFrames++;
	}
	else
	{
		s_nrCharFactorCnt.seatFrames++;
	}
	return 0;
}

// Once per frame from the pull, before the probe's binding, only with
// --native-preview: which custom models are drawn natively this frame. Every
// change of a seat goes into the log once. Writes nothing but this file's
// statics.
internal void NativeRenderLayer_BindChars(const struct GameTracker *gGT)
{
	const char *why = NULL;
	int seat;

	NativeRenderLayer_CloseCharFrames();
	NativeRenderLayer_ReadCharFactor();
	s_nrCharCount = 0;

	if (gGT == NULL)
	{
		why = "no game tracker";
	}
	else if (((u32)gGT->gameMode1 & MAIN_MENU) != 0)
	{
		why = "main menu";
	}
	else
	{
		why = NativeChar_ModeRefusal(gGT);
	}

	for (seat = 0; seat < NATIVE_RENDER_LAYER_DRIVERS; seat++)
	{
		const char *seatWhy = why;
		const struct Model *model = NULL;
		const struct NativeCharGpu *gpu = NULL;

		if (seatWhy == NULL)
		{
			model = NativeChar_SeatModel(seat);
			gpu = NativeCharGpu_ForSeat(seat);

			if (model == NULL)
			{
				seatWhy = "no custom model";
			}
			else if (s_nrSeatState[seat] != NR_SEAT_READY)
			{
				seatWhy = (s_nrSeatState[seat] == NR_SEAT_LOADING) ? "loading" : "seat not ready";
			}
			else if (s_nrSeatInst[seat]->model != model)
			{
				seatWhy = "the instance has another model";
			}
			else if (gpu == NULL)
			{
				seatWhy = "no native set";
			}
			else if (!NativeRenderLayer_CharFactorAllows(0))
			{
				// Last, so the rule only speaks for a seat that would bind.
				seatWhy = "factor 1";
			}
		}

		if (seatWhy != NULL)
		{
			if (s_nrCharSeatBound[seat])
			{
				Platform_Log("[CTR RenderLayer] native char unbound: seat %d (%s)\n", seat, seatWhy);
				s_nrCharSeatBound[seat] = 0;
				s_nrCharSeam[seat].haveLast = 0;
			}
			continue;
		}

		if (!s_nrCharSeatBound[seat])
		{
			Platform_Log("[CTR RenderLayer] native char bound: seat %d model id %d (%s)\n", seat, (int)model->id, NativeChar_SeatFile(seat));
			s_nrCharSeatBound[seat] = 1;
			s_nrCharSeam[seat].haveLast = 0;
		}

		if (NativeRenderLayer_CharIndex(model) < 0)
		{
			s_nrChar[s_nrCharCount].model = model;
			s_nrChar[s_nrCharCount].gpu = gpu;
			s_nrChar[s_nrCharCount].twin = 0;
			s_nrCharCount++;
		}

		s_nrCharSeatFrame[seat].live = (gGT->renderFlags & RENDER_FLAG_RENDER_BUCKET) != 0;
	}

	if (s_nrCharCount > 0)
	{
		s_nrCharCnt.boundFrames++;
		s_nrPreviewCnt.raceFrames++;
		s_nrCharFrame0.live = (gGT != NULL) && ((gGT->renderFlags & RENDER_FLAG_RENDER_BUCKET) != 0);
	}
	else if ((gGT != NULL) && (why != NULL) && (((u32)gGT->gameMode1 & MAIN_MENU) == 0) && (strcmp(why, "no game tracker") != 0))
	{
		// A mode the funnel refuses, with a custom character in play: counted
		// by kind (5c - no native driver there).
		int any = 0;

		for (seat = 0; seat < NATIVE_RENDER_LAYER_DRIVERS; seat++)
		{
			any |= (NativeChar_SeatModel(seat) != NULL);
		}
		if (any)
		{
			if (strcmp(why, "adventure") == 0)
			{
				s_nrPreviewCnt.refusedAdventure++;
			}
			else if (strcmp(why, "cutscene") == 0)
			{
				s_nrPreviewCnt.refusedCutscene++;
			}
			else
			{
				s_nrPreviewCnt.refusedOther++;
			}
		}
	}
}

// --- Step 5a: the driver select preview drawn natively -------------------------
//
// THE PREVIEW (NativeRenderLayer_BindPreview, every frame from the pull right
// after BindChars, only with --native-preview): in the main menu while the
// driver select runs, a change of the wanted preview tile
// (MM_NativeCharGrid_PreviewDesiredEntry) to a custom entry loads its native
// part and its GPU set - the one loading point of the preview, at the cursor
// step, in a menu frame, never in a race frame. The entry the preview window
// shows (MM_NativeCharGrid_PreviewCurrentEntry) is bound like a seat model in
// s_nrChar when its set is ready and seat 0's instance carries its model; the
// route then takes it through the gates of 4c. Leaving the driver select (or
// the menu) lets the preview go. No menu box, no game state: the two MMG
// functions only read.

int MM_NativeCharGrid_PreviewDesiredEntry(void);
int MM_NativeCharGrid_PreviewCurrentEntry(void);


internal void NativeRenderLayer_BindPreview(const struct GameTracker *gGT)
{
	const int inMenu = (gGT != NULL) && (((u32)gGT->gameMode1 & MAIN_MENU) != 0);
	const int loading = (sdata->Loading.stage != LOAD_IDLE) || ((gGT != NULL) && (((u32)gGT->gameMode1 & LOADING) != 0));
	const char *why = NULL;
	int current = -2;

	if (!inMenu)
	{
		why = "not in the menu";
		NativeChar_ReleasePreview();
	}
	else if (loading)
	{
		// The menu overlay may be on its way; no question to the driver select.
		why = "loading";
	}
	else
	{
		const int desired = MM_NativeCharGrid_PreviewDesiredEntry();

		current = MM_NativeCharGrid_PreviewCurrentEntry();
		if (desired == -2)
		{
			why = "driver select not running";
			NativeChar_ReleasePreview();
			s_nrPreviewWhyEntry = -1;
		}
		else if ((desired >= 0) && (desired != NativeChar_PreviewEntry()))
		{
			// The cursor stepped onto another custom tile: read and upload now,
			// timed from here (the read and the upload, for the line).
			const u64 started = SDL_GetPerformanceCounter();

			NativeChar_HoldPreview(desired);
			NativeCharGpu_LoadPreview(desired, started);
		}
	}

	if (why == NULL)
	{
		const struct NativeCharGpu *gpu = NULL;
		const struct Model *model = NULL;

		if (current < 0)
		{
			why = "no custom tile";
		}
		else if ((gpu = NativeCharGpu_ForPreview(current)) == NULL)
		{
			why = "no native set";
		}
		else if ((s_nrSeatState[0] != NR_SEAT_READY) || (s_nrSeatInst[0] == NULL))
		{
			why = "seat not ready";
		}
		else if ((model = NativeChar_EntryModel(current)) != s_nrSeatInst[0]->model)
		{
			why = "the instance has another model";
		}
		else if (!NativeRenderLayer_CharFactorAllows(1))
		{
			why = "factor 1";
		}
		else
		{
			if (!s_nrPreviewBound || (s_nrPreviewBoundEntry != current))
			{
				Platform_Log("[CTR RenderLayer] native char preview bound: entry %d model id %d (%s) at vblank %d\n", current, (int)model->id,
				             NativeChar_EntryFile(current), Platform_GetVBlankCount());
				s_nrCharSeam[0].haveLast = 0;
			}
			s_nrPreviewBound = 1;
			s_nrPreviewBoundEntry = current;
			if ((NativeRenderLayer_CharIndex(model) < 0) && (s_nrCharCount < NATIVE_RENDER_LAYER_DRIVERS))
			{
				s_nrChar[s_nrCharCount].model = model;
				s_nrChar[s_nrCharCount].gpu = gpu;
				s_nrChar[s_nrCharCount].twin = 0;
				s_nrCharCount++;
			}
			s_nrPreviewCnt.frames++;
			if (MM_NativeCharGrid_PreviewCurrentEntry() == -2)
			{
				s_nrPreviewCnt.outsideSelect++;
			}
			s_nrCharSeatFrame[0].live = (gGT->renderFlags & RENDER_FLAG_RENDER_BUCKET) != 0;
			s_nrCharFrame0.live = s_nrCharSeatFrame[0].live;
			return;
		}
	}

	if (s_nrPreviewBound)
	{
		Platform_Log("[CTR RenderLayer] native char preview unbound (%s) at vblank %d\n", why, Platform_GetVBlankCount());
		s_nrPreviewBound = 0;
		s_nrPreviewBoundEntry = -1;
	}
	else if ((current >= 0) && (current != s_nrPreviewWhyEntry))
	{
		// A custom tile shown and not bound: the first reason, once per tile,
		// so a preview that never binds says why.
		Platform_Log("[CTR RenderLayer] native char preview not bound: entry %d (%s) at vblank %d\n", current, why, Platform_GetVBlankCount());
		s_nrPreviewWhyEntry = current;
	}
}

// --- Step 4d: the retail twin of seat 0 (--native-twin) --------------------------
//
// THE TWIN (NativeRenderLayer_BindTwin, every frame from the pull after the
// chars and the preview, only with --native-twin): seat 0's retail model, made
// into a GPU set at load stage 5 (NativeCharGpu_LoadTwin), is bound like a
// custom model while seat 0's instance carries the very model the set was made
// from, in a race the funnel would bind in, and the probe is not on seat 0. The
// route then takes its views through the gates of 4c (kind CHAR, twin 1) with
// three differences: the cull follows REVERSE_CULL_DIRECTION of the view, not
// the determinant (retail decides by the screen sign and bit 29 alone, the
// twin's winding is written for that, platform/native_twin.c); the pose comes
// from the twin's pose table (NativeTwin_PoseIndex with the pulled animation
// word); and the twin has no wheels and sets no wheel stamp - the retail wheels
// stay on.

internal int s_nrTwinBound = 0;

internal struct
{
	unsigned long long boundFrames;
	unsigned long long routed;
	unsigned long long draws;
	unsigned long long notDrawn;
	unsigned long long poseWithoutPull;
	unsigned long long poseOutOfRange;
	unsigned long long toned;      // draws with a tone a != 0
	unsigned long long farZero;    // routed views whose setup loads F = 0
	unsigned long long setupOther; // routed views with a setup callback the twin does not know (F = 0 taken)
	unsigned long long dithered;   // draws with a dither weight > 0 on a range
} s_nrTwinCnt;

internal void NativeRenderLayer_BindTwin(const struct GameTracker *gGT)
{
	const struct NativeCharGpu *gpu = NativeCharGpu_ForTwin();
	const struct Model *model = NativeCharGpu_TwinModel();
	const char *why = NULL;

	if (gpu == NULL)
	{
		why = "no twin set";
	}
	else if ((gGT == NULL) || (((u32)gGT->gameMode1 & MAIN_MENU) != 0))
	{
		why = "main menu";
	}
	else if ((why = NativeChar_ModeRefusal(gGT)) != NULL)
	{
	}
	else if ((s_nrSeatState[0] != NR_SEAT_READY) || (s_nrSeatInst[0] == NULL))
	{
		why = (s_nrSeatState[0] == NR_SEAT_LOADING) ? "loading" : "seat not ready";
	}
	else if (s_nrSeatInst[0]->model != model)
	{
		why = "the instance has another model";
	}
	else if ((g_cfg_nativeProbe != NATIVE_PROBE_NONE) && (g_cfg_nativeProbeSeat == 0))
	{
		why = "probe on seat 0";
	}
	else if (NativeRenderLayer_CharIndex(model) >= 0)
	{
		why = "model bound already";
	}

	if (why != NULL)
	{
		if (s_nrTwinBound)
		{
			Platform_Log("[CTR RenderLayer] native twin unbound (%s) at vblank %d\n", why, Platform_GetVBlankCount());
			s_nrTwinBound = 0;
		}
		return;
	}

	if (!s_nrTwinBound)
	{
		Platform_Log("[CTR RenderLayer] native twin bound: seat 0 model id %d at vblank %d\n", (int)model->id, Platform_GetVBlankCount());
		s_nrCharSeam[0].haveLast = 0;
		s_nrTwinBound = 1;
	}
	if (s_nrCharCount < NATIVE_RENDER_LAYER_DRIVERS)
	{
		s_nrChar[s_nrCharCount].model = model;
		s_nrChar[s_nrCharCount].gpu = gpu;
		s_nrChar[s_nrCharCount].twin = 1;
		s_nrCharCount++;
	}
	s_nrTwinCnt.boundFrames++;
	s_nrCharSeatFrame[0].live = (gGT->renderFlags & RENDER_FLAG_RENDER_BUCKET) != 0;
	s_nrCharFrame0.live = s_nrCharSeatFrame[0].live;
}

// The char part of the route, for an instance view of a bound custom model:
// the gates of NativeRenderLayer_RouteProbe in the same order, then the item
// (FillItem, FillPose, the units of the model, the pose, the native wheels)
// and its marker. 1 = native, 0 = retail with the first reason counted.
internal int NativeRenderLayer_CharFallback(int seat, enum NrProbeFallback reason)
{
	s_nrCharCnt.fallback[reason]++;
	if ((seat >= 0) && (seat < NATIVE_RENDER_LAYER_DRIVERS))
	{
		s_nrCharSeatFrame[seat].lastFallback = s_nrProbeFallbackNames[reason];
		s_nrCharSeatCnt[seat].fallback[reason]++;
	}
	return 0;
}

// The native wheels of a char item (WHLS): the middles of the file in model
// units through the matrix of the body (KONGRUENZ), the mesh turned by the view
// and the rotation of the instance and by the frame of the wheel pose
// (platform/native_wheels.c, as the probe's wheels; the mesh has its size in
// model units, no scale on it).
internal void NativeRenderLayer_FillCharWheels(struct NrDrawItem *it, const struct Instance *inst, const struct PushBuffer *pb)
{
	const struct NativeWheelPose *pose = NativeWheels_PoseOf(inst);
	const struct NativeCharGpu *gpu = it->gpu;
	const MATRIX *vp = &pb->matrix_ViewProj;
	const double s = ldexp(1.0, (int)it->mvpShift);
	double turn[3][3];
	int wheel;
	int r;
	int c;
	int k;

	it->nativeWheels = 0;
	if ((pose == NULL) || (gpu == NULL) || !gpu->hasWheels)
	{
		return;
	}

	for (r = 0; r < 3; r++)
	{
		for (c = 0; c < 3; c++)
		{
			double sum = 0.0;

			for (k = 0; k < 3; k++)
			{
				sum += (double)vp->m[r][k] * (double)inst->matrix.m[k][c];
			}
			turn[r][c] = sum / (4096.0 * 4096.0);
		}
	}

	for (wheel = 0; wheel < NATIVE_WHEELS_COUNT; wheel++)
	{
		double m[3];

		NativeCharGpu_WheelMiddle(gpu->wheelFront, gpu->wheelRear, wheel, m);
		for (r = 0; r < 3; r++)
		{
			double t = (double)it->mvpT[r];

			for (c = 0; c < 3; c++)
			{
				t += ((double)it->mvp[r][c] / 4096.0) * (m[c] * it->unitScale[c]);
			}
			it->wheelB[wheel][r] = t / s;

			for (c = 0; c < 3; c++)
			{
				double sum = 0.0;

				for (k = 0; k < 3; k++)
				{
					sum += turn[r][k] * pose->axis[wheel][k][c];
				}
				it->wheelA[wheel][r][c] = sum;
			}
		}
	}

	it->nativeWheels = 1;
}

// The line of --native-split-report (see above). frame y: the y half of the
// packed frame origin, averaged with the next frame, as the split setup of
// the queue reads it (RenderBucket_QueueExecute.c, the rawSplit setup).
internal void NativeRenderLayer_SplitLine(int handler, const struct Instance *inst, const struct InstDrawPerPlayer *idpp)
{
	const int vblank = Platform_GetVBlankCount();
	const struct ModelHeader *mh = idpp->mh;
	const u32 selector = (u32)(size_t)inst->funcPtr[2];
	const char *branch = "?";
	const char *selectorName = "other";
	int frameY = 0;

	if (s_nrSplitLines >= 4)
	{
		if (((vblank % NATIVE_RENDER_LAYER_PROBE_LINE_STEP) != 0) || (vblank == s_nrSplitLineVBlank))
		{
			return;
		}
	}
	else if (vblank == s_nrSplitLineVBlank)
	{
		return;
	}
	s_nrSplitLines++;
	s_nrSplitLineVBlank = vblank;

	if (((u32)idpp->unkF0 == (u32)RB_RETAIL_UNCOMPRESS_SPLIT) || ((u32)idpp->unkF0 == (u32)RB_RETAIL_UNCOMPRESS_REFLECT))
	{
		branch = "R";
	}
	else if (((u32)idpp->unkF0 == (u32)RB_RETAIL_UNCOMPRESS_NORMAL) || ((u32)idpp->unkF0 == (u32)RB_RETAIL_UNCOMPRESS_NEXTFRAME))
	{
		branch = "P";
	}

	if (selector == RB_RETAIL_INST_FUNC2_SPLIT_BOTH_MASK)
	{
		selectorName = "both-mask";
	}
	else if (selector == RB_RETAIL_INST_FUNC2_SPLIT_NEGATIVE)
	{
		selectorName = "negative";
	}
	else if (selector == RB_RETAIL_INST_FUNC2_SPLIT_XOR)
	{
		selectorName = "xor";
	}
	else if (selector == RB_RETAIL_INST_FUNC2_SPLIT_DIM_XOR)
	{
		selectorName = "dim-xor";
	}

	if (idpp->ptrCurrFrame != NULL)
	{
		frameY = (s16)(RenderBucket_PackedFrameXY(idpp->ptrCurrFrame, idpp->ptrNextFrame) >> 16);
	}

	Platform_Log("[CTR RenderLayer] native char split seat 0 at vblank %d: handler %s, branch %s, splitLine %d, vertSplit %d, t %d %d %d, up %d %d %d, "
	             "inst scale %d %d %d, model scale %d %d %d, frame y %d, view z %d, selector %s, ranges %s\n",
	             vblank, (handler == NATIVE_RENDER_LAYER_HANDLER_SPLIT) ? "split" : "special", branch, (int)idpp->splitLine, (int)inst->vertSplit,
	             (int)inst->matrix.t[0], (int)inst->matrix.t[1], (int)inst->matrix.t[2], (int)inst->matrix.m[0][1], (int)inst->matrix.m[1][1],
	             (int)inst->matrix.m[2][1], (int)inst->scale.x, (int)inst->scale.y, (int)inst->scale.z, (mh != NULL) ? (int)mh->scale.x : 0,
	             (mh != NULL) ? (int)mh->scale.y : 0, (mh != NULL) ? (int)(u16)mh->scale.z : 0, frameY, (int)idpp->mvp.t[2], selectorName,
	             ((idpp->otRangeSecondary != 0) && (idpp->otRangeSecondary != idpp->otRangeNormal)) ? "own" : "shared");
}

internal int NativeRenderLayer_RouteChar(int k, const struct Instance *inst, const struct InstDrawPerPlayer *idpp, const struct PushBuffer *pb)
{
	const struct GameTracker *gGT = sdata->gGT;
	const int db = s_nrMarkerDb;
	const int seat = NativeRenderLayer_SeatOfInst(inst);
	const struct ModelHeader *mh = idpp->mh;
	struct NrDrawItem *it;
	struct NrCharSeam *seam;
	int view = -1;
	int index;
	int notes;
	int v;

	// Only the instance of a bound seat; any other instance with the model
	// (none is known) stays retail without a count.
	if ((gGT == NULL) || (seat < 0) || (!s_nrCharSeatBound[seat] && !((s_nrPreviewBound || s_nrTwinBound) && (seat == 0))))
	{
		return 0;
	}

	if (pb == &gGT->pushBuffer[0])
	{
		s_nrCharSeatFrame[seat].dispatched = 1;
		s_nrCharSeatCnt[seat].dispatched++;
	}

	if ((u32)idpp->unkEC != (u32)RB_RETAIL_DRAWFUNC_NORMAL)
	{
		const int handler = NativeRenderLayer_HandlerIndex((u32)idpp->unkEC);

		// Step 4e-1b: the raw values of the water line of seat 0.
		if (g_cfg_nativeSplitReport && (seat == 0) &&
		    ((handler == NATIVE_RENDER_LAYER_HANDLER_SPLIT) || ((handler == NATIVE_RENDER_LAYER_HANDLER_SPECIAL) && ((inst->flags & SPLIT_LINE) != 0))))
		{
			NativeRenderLayer_SplitLine(handler, inst, idpp);
		}

		// Step 4e-1: the water line, counted (only read: the labels the queue
		// wrote, the instance's light words, the two ranges).
		if (handler == NATIVE_RENDER_LAYER_HANDLER_SPLIT)
		{
			const u32 selector = (u32)(size_t)inst->funcPtr[2];

			s_nrCharSplit.views++;
			if (selector == RB_RETAIL_INST_FUNC2_SPLIT_BOTH_MASK)
			{
				s_nrCharSplit.bothMask++;
			}
			else if (selector == RB_RETAIL_INST_FUNC2_SPLIT_NEGATIVE)
			{
				s_nrCharSplit.negative++;
			}
			else if (selector == RB_RETAIL_INST_FUNC2_SPLIT_XOR)
			{
				s_nrCharSplit.xorSide++;
			}
			else if (selector == RB_RETAIL_INST_FUNC2_SPLIT_DIM_XOR)
			{
				s_nrCharSplit.dimXor++;
			}
			else
			{
				s_nrCharSplit.otherSelector++;
			}
			if ((inst->reflectionRGBA & 0x00ffffffu) != 0x7f7f7fu)
			{
				s_nrCharSplit.maskOff++;
			}
			if ((inst->specLightX & 31) == 1)
			{
				s_nrCharSplit.shift1++;
			}
			else
			{
				s_nrCharSplit.shiftOther++;
			}
			if ((idpp->otRangeSecondary != 0) && (idpp->otRangeSecondary != idpp->otRangeNormal))
			{
				s_nrCharSplit.secondaryOwn++;
			}
			else
			{
				s_nrCharSplit.secondaryShared++;
			}
		}
		return NativeRenderLayer_CharFallback(seat, NativeRenderLayer_HandlerFallback(handler));
	}
	if (((inst->flags >> 16) & 7) != 0)
	{
		return NativeRenderLayer_CharFallback(seat, NR_PROBE_FALLBACK_WRITER);
	}
	if ((idpp->instFlags & PUSHBUFFER_EXISTS) != 0)
	{
		return NativeRenderLayer_CharFallback(seat, NR_PROBE_FALLBACK_DECAL_VIEW);
	}
	if (((u32)idpp->instFlags & RB_INSTANCE_SKIP_OT_RANGE) != 0)
	{
		return NativeRenderLayer_CharFallback(seat, NR_PROBE_FALLBACK_BORROWED_RANGE);
	}
	for (v = 0; v < NATIVE_RENDER_LAYER_VIEWS; v++)
	{
		if (pb == &gGT->pushBuffer[v])
		{
			view = v;
			break;
		}
	}
	if ((view < 0) || ((inst->flags & SCREENSPACE_INSTANCE) != 0))
	{
		return NativeRenderLayer_CharFallback(seat, NR_PROBE_FALLBACK_UI);
	}
	if ((idpp->otRangeNormal == 0) || (idpp->ptrCurrFrame == NULL) || (mh == NULL) || (mh->scale.x == 0) || (mh->scale.y == 0) ||
	    ((u16)mh->scale.z == 0))
	{
		return NativeRenderLayer_CharFallback(seat, NR_PROBE_FALLBACK_NO_RANGE);
	}
	if (s_nrItemCount[db] >= NATIVE_RENDER_LAYER_ITEMS)
	{
		return NativeRenderLayer_CharFallback(seat, NR_PROBE_FALLBACK_ITEM_LIST_FULL);
	}

	index = s_nrItemCount[db];
	it = &s_nrItems[db][index];
	notes = NativeRenderLayer_FillItem(it, gGT, inst, idpp, pb, view);
	NativeRenderLayer_FillPose(it, inst, idpp, pb);
	it->kind = NR_ITEM_CHAR;
	it->seat = (u8)seat;
	it->gpu = s_nrChar[k].gpu;
	it->twin = s_nrChar[k].twin;
	if (it->twin)
	{
		const u32 setup = (u32)(size_t)inst->funcPtr[0];
		const s32 alpha = (s32)idpp->alphaScale;

		// Retail keeps a one-sided triangle by its screen sign and bit 29 xor
		// bit 15 of the view's flags (REVERSE_CULL_DIRECTION); the twin's
		// winding is written for that, not for det(mvp).
		it->cull = (u8)(((idpp->instFlags & REVERSE_CULL_DIRECTION) != 0) ? NATIVE_GFX_CULL_FRONT : NATIVE_GFX_CULL_BACK);

		// The look (Z1): the DPCT tone of RenderBucket_LoadPrimColors, a =
		// idpp->alphaScale / 4096 (held to 0..1 as IR0 is), toward the far
		// colour the setup callback loads - (colorRGBA >> 16, >> 8, >> 0) &
		// 0xff0 into RFC/GFC/BFC, so F = bits 20-27, 12-19, 4-11 over 255;
		// the zero-colour setup loads 0. The OT gate of the DPCT is not
		// rebuilt (taken as always open).
		it->tone = (alpha <= 0) ? 0.0f : ((alpha >= 0x1000) ? 1.0f : ((float)alpha / 4096.0f));
		if ((setup == RB_RETAIL_INST_SETUP_LIGHT_COLOR) || (setup == RB_RETAIL_INST_SETUP_COLOR) || (setup == RB_RETAIL_INST_SETUP_FADE_COLOR))
		{
			const u32 color = inst->colorRGBA;

			it->far[0] = (float)((color >> 20) & 0xffu) / 255.0f;
			it->far[1] = (float)((color >> 12) & 0xffu) / 255.0f;
			it->far[2] = (float)((color >> 4) & 0xffu) / 255.0f;
		}
		else
		{
			it->far[0] = 0.0f;
			it->far[1] = 0.0f;
			it->far[2] = 0.0f;
			if (setup == RB_RETAIL_INST_SETUP_ZERO_COLOR)
			{
				s_nrTwinCnt.farZero++;
			}
			else
			{
				s_nrTwinCnt.setupOther++;
			}
		}
	}
	it->unitScale[0] = 16384.0 / (double)mh->scale.x;
	it->unitScale[1] = 16384.0 / (double)mh->scale.y;
	it->unitScale[2] = 16384.0 / (double)(u16)mh->scale.z;

	// The pose (renderer plan C.5.1): the frame the pull took, else the one
	// idpp->ptrCurrFrame is.
	{
		int anim = it->poseDrawnAnim;
		int frame = it->poseDrawn;
		int pose;

		if (!it->posePull || (frame < 0))
		{
			anim = it->poseCurrentAnim;
			frame = it->poseCurrent;
		}
		if (it->twin)
		{
			// The twin's own table, keyed by the animation word as the pull
			// took it before the queue (else the instance's word now).
			const struct NativeTwinSource *src = NativeCharGpu_TwinSource();
			int word = (int)(u16)inst->animFrame;

			anim = (int)inst->animIndex;
			if ((it->object < NATIVE_RENDER_LAYER_SLOTS) && s_nrObjects[it->object].live && (s_nrObjects[it->object].inst == inst))
			{
				anim = (int)s_nrObjects[it->object].curr.animIndex;
				word = (int)s_nrObjects[it->object].curr.animFrame;
			}
			else
			{
				s_nrTwinCnt.poseWithoutPull++;
			}
			pose = (src != NULL) ? NativeTwin_PoseIndex(src, anim, word) : -1;
			if (pose < 0)
			{
				pose = 0;
				s_nrTwinCnt.poseOutOfRange++;
			}
		}
		else
		{
			pose = NativeCharGpu_PoseIndex(it->gpu->netPoseCount, anim, frame);
			if (pose < 0)
			{
				pose = 0;
				s_nrCharCnt.poseOutOfRange++;
			}
		}
		it->pose = (u16)pose;
	}

	NativeRenderLayer_FillCharWheels(it, inst, pb);
	s_nrItemCount[db]++;

	if (!NativeRenderLayer_LinkMarker(idpp, (u32)index, 0))
	{
		s_nrItemCount[db]--;
		return NativeRenderLayer_CharFallback(seat, NR_PROBE_FALLBACK_ARENA_FULL);
	}

	seam = &s_nrCharSeam[seat];
	if ((notes & NR_ITEM_NEAR) != 0)
	{
		seam->nearDraws++;
	}
	else
	{
		seam->farDraws++;
	}
	if ((notes & NR_ITEM_HUGE) != 0)
	{
		seam->hugeDraws++;
	}
	if ((notes & NR_ITEM_SHIFT_OFF) != 0)
	{
		seam->shiftOff++;
	}
	if (it->cull != (u8)NATIVE_GFX_CULL_BACK)
	{
		seam->mirrored++;
	}
	{
		s16 composed[3][3];

		NativeRenderLayer_ComposeModelView(&pb->matrix_ViewProj, &idpp->m3x3, composed);
		if (memcmp(composed, it->mvp, sizeof(composed)) != 0)
		{
			seam->composedOff++;
		}
	}
	if (it->nativeWheels)
	{
		s_nrCharCnt.wheelItems++;
	}

	// The stamp the retail wheels of this view read (DrawTires runs after the
	// queue in the same frame). Not for the twin: its retail wheels stay on.
	if (it->twin)
	{
		s_nrTwinCnt.routed++;
	}
	else if (it->object < NATIVE_RENDER_LAYER_SLOTS)
	{
		s_nrCharViewFrame[it->object][view] = s_nrFrame;
		s_nrCharViewWheels[it->object][view] = it->nativeWheels;
	}

	s_nrCharSeatFrame[seat].routed = 1;
	if (view == 0)
	{
		s_nrCharSeatCnt[seat].routedNormal++;
		s_nrCharFrame0.routed++;
	}
	return 1;
}


// THE MATRIX S, from the unit body to the PSX screen, homogeneous, rows in
// S[row][column]. With view = (mvp.m / 4096 * g + mvp.t) / 2^shift for a point
// g of the body, the rows give (H x + OFX z, H y + OFY z, zNear, z): divided by
// w = z that is the screen point the retail projection gives the same vertex
// (offset plus H x / z), and zNear / z the reverse depth of native_gfx.h.
// OFX/OFY: half the view plus the draw offset of the split.
internal void NativeRenderLayer_ItemMatrix(const struct NrDrawItem *it, double ofsX, double ofsY, double S[4][4])
{
	const double s = ldexp(1.0, (int)it->mvpShift);
	const double H = (double)it->H;
	const double ofx = (double)(it->rectW >> 1) + ofsX;
	const double ofy = (double)(it->rectH >> 1) + ofsY;
	const double zNear = H / 8.0;
	double A[3][3];
	double b[3];
	int r;
	int c;

	for (r = 0; r < 3; r++)
	{
		double t = (double)it->mvpT[r];

		for (c = 0; c < 3; c++)
		{
			const double rf = (double)it->mvp[r][c] / 4096.0;

			A[r][c] = (rf * (double)it->bodyHalf[c]) / s;
			t += rf * (double)it->bodyCenter[c];
		}
		b[r] = t / s;
	}

	for (c = 0; c < 3; c++)
	{
		S[0][c] = (H * A[0][c]) + (ofx * A[2][c]);
		S[1][c] = (H * A[1][c]) + (ofy * A[2][c]);
		S[2][c] = 0.0;
		S[3][c] = A[2][c];
	}
	S[0][3] = (H * b[0]) + (ofx * b[2]);
	S[1][3] = (H * b[1]) + (ofy * b[2]);
	S[2][3] = zNear;
	S[3][3] = b[2];
}

// One point of the unit body through S: screen x, y and w. 0 when w <= 0
// (behind the eye: no screen point).
internal int NativeRenderLayer_ProjectUnit(const double S[4][4], double ux, double uy, double uz, double *sx, double *sy)
{
	const double x = (S[0][0] * ux) + (S[0][1] * uy) + (S[0][2] * uz) + S[0][3];
	const double y = (S[1][0] * ux) + (S[1][1] * uy) + (S[1][2] * uz) + S[1][3];
	const double w = (S[3][0] * ux) + (S[3][1] * uy) + (S[3][2] * uz) + S[3][3];

	if (w <= 0.0)
	{
		return 0;
	}

	*sx = x / w;
	*sy = y / w;
	return 1;
}

// The eight corners given in unit coordinates (lo..hi per axis) into a box
// x0 y0 x1 y1, rounded outwards. 0 when a corner has no screen point.
internal int NativeRenderLayer_ProjectBox(const double S[4][4], const double lo[3], const double hi[3], int box[4])
{
	double minX = 0.0;
	double minY = 0.0;
	double maxX = 0.0;
	double maxY = 0.0;
	int corner;

	for (corner = 0; corner < 8; corner++)
	{
		const double ux = ((corner & 1) != 0) ? hi[0] : lo[0];
		const double uy = ((corner & 2) != 0) ? hi[1] : lo[1];
		const double uz = ((corner & 4) != 0) ? hi[2] : lo[2];
		double sx;
		double sy;

		if (!NativeRenderLayer_ProjectUnit(S, ux, uy, uz, &sx, &sy))
		{
			return 0;
		}

		if ((corner == 0) || (sx < minX))
		{
			minX = sx;
		}
		if ((corner == 0) || (sx > maxX))
		{
			maxX = sx;
		}
		if ((corner == 0) || (sy < minY))
		{
			minY = sy;
		}
		if ((corner == 0) || (sy > maxY))
		{
			maxY = sy;
		}
	}

	box[0] = (int)floor(minX);
	box[1] = (int)floor(minY);
	box[2] = (int)ceil(maxX);
	box[3] = (int)ceil(maxY);
	return 1;
}

// THE PROBE LINE of a frame with a native draw, on the line spacing and only
// with --native-layer-report: where the body, the hull of the retail model and
// the retail wheels of the reference run land on the screen of this split
// (pixels of the internal picture at scale 1), their union - the rectangle a
// check compares - and the middle of the back of the body. what comes right
// before "box": "native, " for a native draw, "fallback " for a fallback box.
internal void NativeRenderLayer_ProbeBoxLine(int vblank, const char *what, const struct NrDrawItem *it, const double S[4][4], double ofsX, double ofsY)
{
	const double bodyLo[3] = {-1.0, 0.0, -1.0};
	const double bodyHi[3] = {1.0, 1.0, 1.0};
	const double H = (double)it->H;
	const double ofx = (double)(it->rectW >> 1) + ofsX;
	const double ofy = (double)(it->rectH >> 1) + ofsY;
	double hullLo[3];
	double hullHi[3];
	double wheelMin[2] = {0.0, 0.0};
	double wheelMax[2] = {0.0, 0.0};
	int box[4];
	int hull[4];
	int wheels[4];
	int all[4];
	double centerX = 0.0;
	double centerY = 0.0;
	int known;
	int axis;
	int wheel;
	int corner;
	int i;

	// The hull corners in unit coordinates of the body, so the same matrix
	// takes them: u = (g - center) / half per axis.
	for (axis = 0; axis < 3; axis++)
	{
		const double center = (double)it->bodyCenter[axis];
		const double half = (double)it->bodyHalf[axis];

		hullLo[axis] = ((double)it->hullMin[axis] - center) / half;
		hullHi[axis] = ((double)it->hullMin[axis] + (double)NATIVE_RENDER_LAYER_HULL_SIZE - center) / half;
	}

	known = NativeRenderLayer_ProjectBox(S, bodyLo, bodyHi, box) && NativeRenderLayer_ProjectBox(S, hullLo, hullHi, hull) &&
	        NativeRenderLayer_ProjectUnit(S, 0.0, 0.5, -1.0, &centerX, &centerY) && (it->wheels != 0);

	// The wheel boxes in view space, through the screen mapping of the split.
	for (wheel = 0; known && (wheel < 4); wheel++)
	{
		for (corner = 0; corner < 8; corner++)
		{
			const double x = (double)it->wheelView[wheel][0] + ((((corner & 1) != 0) ? 1.0 : -1.0) * (double)it->wheelExt[0]);
			const double y = (double)it->wheelView[wheel][1] + ((((corner & 2) != 0) ? 1.0 : -1.0) * (double)it->wheelExt[1]);
			const double z = (double)it->wheelView[wheel][2] + ((((corner & 4) != 0) ? 1.0 : -1.0) * (double)it->wheelExt[2]);
			const int first = (wheel == 0) && (corner == 0);
			double sx;
			double sy;

			if (z <= 0.0)
			{
				known = 0;
				break;
			}

			sx = ofx + ((H * x) / z);
			sy = ofy + ((H * y) / z);
			if (first || (sx < wheelMin[0]))
			{
				wheelMin[0] = sx;
			}
			if (first || (sy < wheelMin[1]))
			{
				wheelMin[1] = sy;
			}
			if (first || (sx > wheelMax[0]))
			{
				wheelMax[0] = sx;
			}
			if (first || (sy > wheelMax[1]))
			{
				wheelMax[1] = sy;
			}
		}
	}

	if (!known)
	{
		Platform_Log("[CTR RenderLayer] probe at vblank %d: %sbox unknown\n", vblank, what);
		return;
	}

	wheels[0] = (int)floor(wheelMin[0]);
	wheels[1] = (int)floor(wheelMin[1]);
	wheels[2] = (int)ceil(wheelMax[0]);
	wheels[3] = (int)ceil(wheelMax[1]);

	// x0 y0 the smallest, x1 y1 the largest of the three.
	for (i = 0; i < 4; i++)
	{
		int value = box[i];

		if ((i < 2) ? (hull[i] < value) : (hull[i] > value))
		{
			value = hull[i];
		}
		if ((i < 2) ? (wheels[i] < value) : (wheels[i] > value))
		{
			value = wheels[i];
		}
		all[i] = value;
	}

	Platform_Log("[CTR RenderLayer] probe at vblank %d: %sbox %d %d %d %d, hull %d %d %d %d, wheels %d %d %d %d, "
	             "union %d %d %d %d, center x %d\n",
	             vblank, what, box[0], box[1], box[2], box[3], hull[0], hull[1], hull[2], hull[3], wheels[0], wheels[1], wheels[2], wheels[3],
	             all[0], all[1], all[2], all[3], (int)floor(centerX + 0.5));
}

// The probe line of a native draw: on the line spacing, only with
// --native-layer-report, and never twice for one VBlank.
internal void NativeRenderLayer_ProbeLine(const struct NrDrawItem *it, const double S[4][4], double ofsX, double ofsY)
{
	const int vblank = Platform_GetVBlankCount();

	if (!g_cfg_nativeLayerReport || ((vblank % NATIVE_RENDER_LAYER_PROBE_LINE_STEP) != 0) || (vblank == s_nrProbeLineVBlank))
	{
		return;
	}
	s_nrProbeLineVBlank = vblank;

	NativeRenderLayer_ProbeBoxLine(vblank, "native, ", it, S, ofsX, ofsY);
}

// --- The hooks -------------------------------------------------------------

void NativeRenderLayer_NoteBirth(const struct Instance *inst)
{
	NativeRenderLayer_NoteLife(inst, &s_nrCount.births);
}

void NativeRenderLayer_NoteDeath(const struct Instance *inst)
{
	NativeRenderLayer_NoteLife(inst, &s_nrCount.deaths);
}

void NativeRenderLayer_NoteRestore(void)
{
	int slot;
	int liveBefore = 0;

	s_nrCount.restores++;

	for (slot = 0; slot < NATIVE_RENDER_LAYER_SLOTS; slot++)
	{
		const struct NrObject *obj = &s_nrObjects[slot];

		s_nrGenerationBefore[slot] = s_nrGeneration[slot];
		s_nrInstBefore[slot] = obj->live ? obj->inst : NULL;
		if (obj->live)
		{
			liveBefore++;
		}
		s_nrGeneration[slot]++;
	}
	s_nrCount.restoreGenerations += NATIVE_RENDER_LAYER_SLOTS;

	NativeRenderLayer_ResetTable();
	s_nrRestorePending = 1;

	// Once per restore, never per frame, and only under the switch: a run
	// without it keeps its log as it was.
	if (g_cfg_nativeLayerReport)
	{
		Platform_Log("[CTR RenderLayer] restore %llu: new generation for all %d slot(s), table emptied (%d live object(s) before)\n",
		             s_nrCount.restores, NATIVE_RENDER_LAYER_SLOTS, liveBefore);
	}

	// The probe lets go: the restored pool may hold another seat 0 at another
	// address. The next pull binds anew after its checks. Only with a bound
	// probe, so a run without one has no new line.
	if (s_nativeModelCount != 0)
	{
		Platform_Log("[CTR RenderLayer] probe unbound (checkpoint restore)\n");
		s_probeModel = NULL;
		s_probeInst = NULL;
		s_nativeModelCount = 0;
		NativeRenderLayer_ForgetDepth();
	}

	// The custom characters let go as well (step 4c); the next pull binds
	// anew. Only with one bound, so a run without one has no new line.
	if (s_nrCharCount != 0)
	{
		Platform_Log("[CTR RenderLayer] native chars unbound (checkpoint restore)\n");
		s_nrCharCount = 0;
		memset(s_nrCharSeatBound, 0, sizeof(s_nrCharSeatBound));
		s_nrPreviewBound = 0;
		s_nrPreviewBoundEntry = -1;
		s_nrTwinBound = 0;
	}

	// The seats of the pool before the restore are none of the restored pool:
	// until the next pull reads them anew, no hook finds a seat.
	NativeRenderLayer_ClearSeats();

	// The wheel poses jump with the restore: every seat starts anew.
	NativeWheels_Forget();
}

// The first pull after a restore: every object it meets has to be new - a
// generation other than the one before the restore (compared with !=, so a
// counter that wraps around still counts), a cut, no previous state - and
// every camera in use has to start with a cut as well.
// "same address" counts the objects whose slot held the same instance address
// before the restore: exactly the ones the table would have bridged without
// the new generation.
internal void NativeRenderLayer_CheckRestorePull(void)
{
	int slot;
	int objects = 0;
	int raised = 0;
	int cut = 0;
	int withoutPrev = 0;
	int sameAddress = 0;
	int camerasCut = 0;
	int view;

	for (slot = 0; slot < NATIVE_RENDER_LAYER_SLOTS; slot++)
	{
		const struct NrObject *obj = &s_nrObjects[slot];

		if (!obj->seen)
		{
			continue;
		}

		objects++;
		if (obj->key.generation != s_nrGenerationBefore[slot])
		{
			raised++;
		}
		if (obj->curr.cut != 0)
		{
			cut++;
		}
		if (!obj->hasPrev)
		{
			withoutPrev++;
		}
		if ((s_nrInstBefore[slot] != NULL) && (obj->inst == s_nrInstBefore[slot]))
		{
			sameAddress++;
		}
	}

	for (view = 0; view < NATIVE_RENDER_LAYER_VIEWS; view++)
	{
		if (s_nrCameras[view].live && (s_nrCameras[view].curr.cut != 0))
		{
			camerasCut++;
		}
	}

	s_nrRestorePending = 0;
	s_nrCount.restorePulls++;
	s_nrCount.restoreObjects += (unsigned long long)objects;
	s_nrCount.restoreRaised += (unsigned long long)raised;
	s_nrCount.restoreCut += (unsigned long long)cut;
	s_nrCount.restoreWithoutPrev += (unsigned long long)withoutPrev;
	s_nrCount.restoreSameAddress += (unsigned long long)sameAddress;

	if (g_cfg_nativeLayerReport)
	{
		Platform_Log("[CTR RenderLayer] restore %llu, first pull: objects %d, generation raised %d, cut %d, without previous state %d, "
		             "same address as before %d, cameras cut %d\n",
		             s_nrCount.restores, objects, raised, cut, withoutPrev, sameAddress, camerasCut);
	}
}

void NativeRenderLayer_Pull(struct GameTracker *gGT)
{
	const struct JitPool *pool;
	const struct Item *item;
	int guard;
	int tick;
	int objects = 0;
	int slot;
	int seat;

	// The seats of this frame first, in every frame and before every gate:
	// the binding below, the route and the handler count of this frame read
	// them, and a loading frame has to leave the table empty.
	NativeRenderLayer_ReadSeats(gGT);

	// The lines of --native-wheel-report belong to a frame that starts a tick;
	// set further down, once the tick is known.
	s_nrWheelSample = 0;
	s_nrWheelFloatDone = 0;
	s_nrWheelRetailDone = 0;

	// The probe binding, before the gate below: the retail wheels ask for the
	// bound model in frames without the queue too, so a stale binding must not
	// outlive the race (main menu, a load). Without --native-probe never runs.
	// The custom characters drawn natively (step 4c), before the probe, whose
	// depth wish then knows about them. Without --native-preview never runs.
	if (g_cfg_nativePreview)
	{
		NativeRenderLayer_BindChars(gGT);
		NativeRenderLayer_BindPreview(gGT);
		if (g_cfg_nativeTwin)
		{
			NativeRenderLayer_BindTwin(gGT);
		}
	}

	if (g_cfg_nativeProbe != NATIVE_PROBE_NONE)
	{
		NativeRenderLayer_BindProbe(gGT);
	}
	else if ((s_nrCharCount > 0) || s_nrCharDepthWanted)
	{
		// Without the probe the depth of the main target follows the chars
		// alone; only a change does anything.
		NativeRenderer_WantNativeDepth(s_nrCharCount > 0);
	}
	s_nrCharDepthWanted = (s_nrCharCount > 0);

	// The same gate as RenderBucket_QueueAllInstances: without the flag the
	// queue does not run, and in a loading frame the pool may not be the one
	// the instances live in yet.
	if ((gGT == NULL) || ((gGT->renderFlags & RENDER_FLAG_RENDER_BUCKET) == 0))
	{
		return;
	}

	s_nrCount.pullFrames++;
	pool = &gGT->JitPools.instance;

	if (!s_nrPoolKnown || (pool->ptrPoolData != s_nrPoolData) || (pool->itemSize != s_nrPoolItemSize) || (pool->maxItems != s_nrPoolMaxItems))
	{
		if (s_nrPoolKnown)
		{
			s_nrCount.tableResets++;
		}
		NativeRenderLayer_ResetTable();
		s_nrPoolData = pool->ptrPoolData;
		s_nrPoolItemSize = pool->itemSize;
		s_nrPoolMaxItems = pool->maxItems;
		s_nrPoolKnown = 1;
	}

	// gGT->timer only counts in unpaused logic frames, so a pause is one long
	// tick: the current state is overwritten, never shifted.
	tick = !s_nrHaveTick || ((u32)gGT->timer != s_nrTick.timer);
	if (tick)
	{
		s_nrTick.timer = (u32)gGT->timer;
		s_nrTick.elapsedTimeMS = (u16)gGT->elapsedTimeMS;
		s_nrTick.hostTimeUs = 0;
		s_nrHaveTick = 1;
		s_nrCount.ticks++;
	}

	// THE WHEEL POSES, only while the probe is on: every seat of this frame,
	// moved on once per tick (platform/native_wheels.c). Reads the drivers the
	// seat table checked; writes only that file's table.
	if (NativeRenderLayer_ProbeActive() || (s_nrCharCount > 0))
	{
		for (seat = 0; seat < NATIVE_RENDER_LAYER_DRIVERS; seat++)
		{
			NativeWheels_Pull(seat, s_nrSeatDriver[seat], s_nrSeatInst[seat], tick, (u32)gGT->timer, (int)gGT->elapsedTimeMS,
			                  seat == g_cfg_nativeProbeSeat);
		}

		s_nrWheelSample = g_cfg_nativeWheelReport && tick;
		s_nrWheelSampleTimer = (u32)gGT->timer;
	}

	for (slot = 0; slot < NATIVE_RENDER_LAYER_SLOTS; slot++)
	{
		s_nrObjects[slot].seen = 0;
	}

	// The taken list, as RenderBucket_QueueNonLevInstances walks it. The guard
	// stops at the slot count, so a broken list ends the walk instead of
	// looping (as in native_checkpoint.c).
	guard = 0;
	for (item = pool->taken.first; item != NULL; item = item->next)
	{
		const struct Instance *inst = (const struct Instance *)item;
		const struct Driver *driver = NULL;
		struct NrTickState state;
		struct NrObject *obj;
		int same;

		if (guard >= pool->maxItems)
		{
			s_nrCount.pullListCut++;
			break;
		}
		guard++;

		// The address first: only an item of this pool is read as an instance.
		slot = NativeRenderLayer_PoolSlot(pool, inst);
		if (slot < 0)
		{
			s_nrCount.pullOutsidePool++;
			continue;
		}
		if (slot >= NATIVE_RENDER_LAYER_SLOTS)
		{
			s_nrCount.pullBeyondTable++;
			continue;
		}

		if (inst->model == NULL)
		{
			continue;
		}

		// A driver only through the seats of this frame (NativeRenderLayer_ReadSeats).
		for (seat = 0; seat < NATIVE_RENDER_LAYER_DRIVERS; seat++)
		{
			if ((s_nrSeatInst[seat] != NULL) && (s_nrSeatInst[seat] == inst))
			{
				driver = s_nrSeatDriver[seat];
				break;
			}
		}

		NativeRenderLayer_ReadState(inst, driver, &state);

		obj = &s_nrObjects[slot];
		same = obj->live && (obj->inst == inst) && (obj->key.generation == s_nrGeneration[slot]) && (obj->stubModel == inst->model);

		if (!same)
		{
			// New object, a new generation in the slot or another model: no
			// previous state to blend from.
			memset(obj, 0, sizeof(*obj));
			obj->key.source = NR_SOURCE_INSTANCE;
			obj->key.id = (u32)slot;
			obj->key.generation = s_nrGeneration[slot];
			obj->inst = inst;
			obj->stubModel = inst->model;
			obj->nativeModel = 0;
			obj->kind = NR_KIND_INSTANCE_MODEL;
			state.cut = 1;
			obj->curr = state;
			obj->hasPrev = 0;
			s_nrCount.cuts++;
		}
		else if (tick)
		{
			obj->prev = obj->curr;
			obj->curr = state;
			obj->hasPrev = 1;
		}
		else
		{
			// Same tick: the state of this tick is read again (the menu and
			// the effects may have written after the last pull).
			state.cut = obj->curr.cut;
			obj->curr = state;
		}

		obj->seen = 1;
		objects++;
	}

	// Whatever this pull did not meet falls out of the table.
	for (slot = 0; slot < NATIVE_RENDER_LAYER_SLOTS; slot++)
	{
		struct NrObject *obj = &s_nrObjects[slot];

		obj->live = obj->seen;
		if (!obj->live)
		{
			obj->hasPrev = 0;
		}
	}

	NativeRenderLayer_PullCameras(gGT, tick);

	if (s_nrRestorePending)
	{
		NativeRenderLayer_CheckRestorePull();
	}

	s_nrCount.objects += (unsigned long long)objects;
	if (objects > s_nrCount.objectsPeak)
	{
		s_nrCount.objectsPeak = objects;
	}
}

int NativeRenderLayer_Route(const struct Instance *inst, const struct InstDrawPerPlayer *idpp, const struct PushBuffer *pb)
{
	int answer = 0;

	s_nrCount.routeCalls++;

	// The self-test of the marker channel: an empty marker in the own OT range
	// of a driver instance, and the retail handler draws as always. Before the
	// early exit on purpose - it needs no native model.
	if (g_cfg_nativeEmptyMarkers)
	{
		NativeRenderLayer_EmptyMarker(inst, idpp, pb);
	}

	if ((s_nativeModelCount == 0) && (s_nrCharCount == 0))
	{
		return 0;
	}

	// The native models are the bound probe and the bound custom models (step
	// 4c); every other model is retail without a count. Counted on the way
	// out, so "native" in the report counts exactly the answers that skip the
	// retail handler, and route calls = switch entries + native.
	if ((inst == NULL) || (idpp == NULL) || (pb == NULL))
	{
		return 0;
	}

	if ((s_nativeModelCount != 0) && (inst->model == s_probeModel))
	{
		answer = NativeRenderLayer_RouteProbe(inst, idpp, pb);
	}
	else if (s_nrCharCount != 0)
	{
		const int k = NativeRenderLayer_CharIndex(inst->model);

		if (k < 0)
		{
			return 0;
		}
		answer = NativeRenderLayer_RouteChar(k, inst, idpp, pb);
	}
	else
	{
		return 0;
	}
	if (answer != 0)
	{
		s_nrCount.native++;
	}
	return answer;
}

void NativeRenderLayer_NoteSwitchEntry(const struct Instance *inst, const struct InstDrawPerPlayer *idpp, const struct PushBuffer *pb, u32 handler)
{
	const struct GameTracker *gGT = sdata->gGT;
	const int index = NativeRenderLayer_HandlerIndex(handler);

	(void)idpp;

	s_nrCount.switchEntries++;
	s_nrCount.handlerAll[index]++;

	if ((inst == NULL) || (gGT == NULL))
	{
		return;
	}

	// Seat 0 by its own instance, the way native_chars.c recognizes it; a
	// thread's object is only a Driver for some threads. The instance comes
	// from the seats the pull checked in this frame (none during a load).
	if ((s_nrSeatInst[0] != NULL) && (s_nrSeatInst[0] == inst))
	{
		s_nrCount.handlerSeat0[index]++;
		if ((inst->flags & NATIVE_RENDER_LAYER_ANIM_BITS) != 0)
		{
			s_nrCount.seat0AnimBits++;
		}
	}

	if ((pb == &gGT->pushBuffer_UI) || ((inst->flags & SCREENSPACE_INSTANCE) != 0))
	{
		NativeRenderLayer_NoteUi(inst, pb == &gGT->pushBuffer_UI);
	}
}

// THE REPORT. Every line starts with "[CTR RenderLayer] at exit:", the first
// one is the line a check reads: route calls = switch entries while nothing
// is native, native = 0. Always all lines, even when everything is zero: a
// total that is missing looks like one that was never counted.
void NativeRenderLayer_Report(void)
{
	const unsigned long long *all = s_nrCount.handlerAll;
	const unsigned long long *seat0 = s_nrCount.handlerSeat0;
	int entry;

	Platform_Log("[CTR RenderLayer] at exit: route calls %llu, switch entries %llu, native %llu\n", s_nrCount.routeCalls,
	             s_nrCount.switchEntries, s_nrCount.native);
	Platform_Log("[CTR RenderLayer] at exit: handlers all: normal %llu, normal alt %llu, split %llu, special %llu, reflection %llu, "
	             "other %llu\n",
	             all[NATIVE_RENDER_LAYER_HANDLER_NORMAL], all[NATIVE_RENDER_LAYER_HANDLER_NORMAL_ALT], all[NATIVE_RENDER_LAYER_HANDLER_SPLIT],
	             all[NATIVE_RENDER_LAYER_HANDLER_SPECIAL], all[NATIVE_RENDER_LAYER_HANDLER_REFLECTION], all[NATIVE_RENDER_LAYER_HANDLER_OTHER]);
	Platform_Log("[CTR RenderLayer] at exit: handlers seat 0: normal %llu, normal alt %llu, split %llu, special %llu, reflection %llu, "
	             "other %llu\n",
	             seat0[NATIVE_RENDER_LAYER_HANDLER_NORMAL], seat0[NATIVE_RENDER_LAYER_HANDLER_NORMAL_ALT],
	             seat0[NATIVE_RENDER_LAYER_HANDLER_SPLIT], seat0[NATIVE_RENDER_LAYER_HANDLER_SPECIAL],
	             seat0[NATIVE_RENDER_LAYER_HANDLER_REFLECTION], seat0[NATIVE_RENDER_LAYER_HANDLER_OTHER]);
	Platform_Log("[CTR RenderLayer] at exit: seat 0 entries with animation bits 0x30 %llu\n", s_nrCount.seat0AnimBits);
	Platform_Log("[CTR RenderLayer] at exit: UI entries %llu (UI push buffer %llu, screen space flag only %llu), %d model(s), "
	             "%llu entries beyond the table of %d\n",
	             s_nrCount.uiEntries, s_nrCount.uiByPushBuffer, s_nrCount.uiByFlagOnly, s_nrUiModelCount, s_nrCount.uiBeyondTable,
	             NATIVE_RENDER_LAYER_UI_MODELS);
	for (entry = 0; entry < s_nrUiModelCount; entry++)
	{
		Platform_Log("[CTR RenderLayer] at exit: UI model %d id %d: %llu entries\n", entry, (int)s_nrUiModels[entry].id,
		             s_nrUiModels[entry].entries);
	}
	Platform_Log("[CTR RenderLayer] at exit: pull frames %llu, ticks %llu, objects %llu, peak %d per frame, cuts %llu, "
	             "table resets %llu\n",
	             s_nrCount.pullFrames, s_nrCount.ticks, s_nrCount.objects, s_nrCount.objectsPeak, s_nrCount.cuts,
	             s_nrCount.tableResets);
	Platform_Log("[CTR RenderLayer] at exit: births %llu, deaths %llu, outside the pool %llu, beyond the table %llu; "
	             "pull outside the pool %llu, beyond the table %llu, list cut %llu\n",
	             s_nrCount.births, s_nrCount.deaths, s_nrCount.lifeOutsidePool, s_nrCount.lifeBeyondTable,
	             s_nrCount.pullOutsidePool, s_nrCount.pullBeyondTable, s_nrCount.pullListCut);
	Platform_Log("[CTR RenderLayer] at exit: restores %llu, generations renewed %llu, first pulls after a restore %llu: objects %llu, "
	             "generation raised %llu, cut %llu, without previous state %llu, same address as before %llu\n",
	             s_nrCount.restores, s_nrCount.restoreGenerations, s_nrCount.restorePulls, s_nrCount.restoreObjects,
	             s_nrCount.restoreRaised, s_nrCount.restoreCut, s_nrCount.restoreWithoutPrev, s_nrCount.restoreSameAddress);

	// The probe lines only in a run with the probe: a run without it keeps the
	// report it had.
	if (NativeRenderLayer_ProbeActive())
	{
		const unsigned long long *fb = s_nrCount.probeFallback;

		Platform_Log("[CTR RenderLayer] at exit: probe bound %llu frame(s), native draws %llu, frames with a native draw %llu, "
		             "most in one frame and view %d, not drawn %llu, stale items %llu\n",
		             s_nrCount.probeBoundFrames, s_nrCount.probeDraws, s_nrCount.probeDrawFrames, s_nrCount.probeMostPerView,
		             s_nrCount.probeNotDrawn, s_nrCount.probeStale);
		Platform_Log("[CTR RenderLayer] at exit: probe fallbacks: normal alt %llu, split %llu, special %llu, reflection %llu, other %llu, "
		             "writer %llu, decal view %llu, borrowed range %llu, ui %llu, no range %llu, item list full %llu, arena full %llu\n",
		             fb[NR_PROBE_FALLBACK_NORMAL_ALT], fb[NR_PROBE_FALLBACK_SPLIT], fb[NR_PROBE_FALLBACK_SPECIAL],
		             fb[NR_PROBE_FALLBACK_REFLECTION], fb[NR_PROBE_FALLBACK_OTHER], fb[NR_PROBE_FALLBACK_WRITER],
		             fb[NR_PROBE_FALLBACK_DECAL_VIEW], fb[NR_PROBE_FALLBACK_BORROWED_RANGE], fb[NR_PROBE_FALLBACK_UI],
		             fb[NR_PROBE_FALLBACK_NO_RANGE], fb[NR_PROBE_FALLBACK_ITEM_LIST_FULL], fb[NR_PROBE_FALLBACK_ARENA_FULL]);
		Platform_Log("[CTR RenderLayer] at exit: probe depth clears %llu, mirrored determinant %llu, mvp shift near %llu huge %llu, "
		             "shift check off %llu, depth jump at a shift change max %.2f percent\n",
		             s_nrCount.probeDepthClears, s_nrCount.probeMirrored, s_nrCount.probeShiftNear, s_nrCount.probeShiftHuge,
		             s_nrCount.probeShiftCheckOff, s_nrCount.probeDepthJumpMax);
		// The seam in numbers ("huge" above is DRAW_HUGE, not the far side):
		// how often the probe was drawn at view z 0x1000 or more, how often the
		// shift changed between neighbouring native frames of view 0 - each
		// such change is one depth jump compared - and the yardstick of plain
		// motion near the seam.
		Platform_Log("[CTR RenderLayer] at exit: probe seat %d shift: far %llu, changes between neighbouring native frames %llu "
		             "(near to far %llu, far to near %llu), changes after a frame without a native draw %llu; "
		             "depth step without a change max %.2f percent over %llu pair(s) at view z 0x800 to 0x2000\n",
		             g_cfg_nativeProbeSeat, s_nrCount.probeShiftFar, s_nrCount.probeShiftChanges, s_nrCount.probeShiftNearToFar,
		             s_nrCount.probeShiftFarToNear, s_nrCount.probeShiftChangesAfterGap, s_nrCount.probeDepthStepMax,
		             s_nrCount.probeDepthStepPairs);
		// Where the largest jump happened, to tell a seam (a ratio of about 4 or
		// 1/4 between the two depths) from plain motion. All 0 without a change.
		Platform_Log("[CTR RenderLayer] at exit: probe largest depth jump %.2f percent at vblank %d: view z %.1f with shift %d, then %.1f "
		             "with shift %d, ratio %.4f\n",
		             s_nrCount.probeDepthJumpMax, s_nrCount.probeDepthJumpMaxVBlank, s_nrCount.probeDepthJumpMaxZ,
		             s_nrCount.probeDepthJumpMaxShiftBefore, s_nrCount.probeDepthJumpMaxZAfter, s_nrCount.probeDepthJumpMaxShiftAfter,
		             (s_nrCount.probeDepthJumpMaxZ > 0.0) ? (s_nrCount.probeDepthJumpMaxZAfter / s_nrCount.probeDepthJumpMaxZ) : 0.0);
		Platform_Log("[CTR RenderLayer] at exit: probe composed matrix off %llu, split fallback boxes %llu\n", s_nrCount.probeComposedOff,
		             s_nrCount.probeFallbackBoxes);
		// The texture of the form texture: every upload, and the ones that came
		// after the first frame was drawn or into a frame with draws (0 expected:
		// it is uploaded once, before the first frame draws). Both 0 for the form body.
		{
			unsigned int uploads = 0;
			unsigned int duringFrame = 0;

			NativeRenderer_NativeTextureUploads(&uploads, &duringFrame);
			Platform_Log("[CTR RenderLayer] at exit: probe texture uploads %u, uploads during a frame %u\n", uploads, duringFrame);
		}
		// The upload rule in one number, for every form: probe texture uploads
		// that came in a race frame (platform/native_tex.c, NativeTex_InRaceFrame)
		// - the start-up texture of texture, pose and wheels and the loading
		// screen texture of mips alike. 0 expected. The line above stays as it
		// was; for mips it counts the loading-screen upload as "during a frame".
		Platform_Log("[CTR RenderLayer] at exit: probe texture uploads during a race frame %u\n", NativeRenderer_ProbeUploadsInRaceFrame());

		// The form pose only, so the report of the other forms stays as it was.
		if (g_cfg_nativeProbe == NATIVE_PROBE_POSE)
		{
			Platform_Log("[CTR RenderLayer] at exit: probe pose ticks %llu, drawn = current %llu, mismatched %llu (frame %llu, animation %llu), "
			             "without a pull value %llu, execute-time anim word differs from the pulled word %llu, pose vertex writes %llu (%llu bytes)\n",
			             s_nrCount.poseTicks, s_nrCount.poseMatched, s_nrCount.poseMismatched, s_nrCount.poseFrameMismatched, s_nrCount.poseAnimMismatched,
			             s_nrCount.poseWithoutPull, s_nrCount.poseWordDiffers, s_nrCount.poseVertexWrites,
			             s_nrCount.poseVertexWrites * (unsigned long long)(NATIVE_PROBE_VERTEX_COUNT * sizeof(struct NativeProbeVertex)));
			Platform_Log("[CTR RenderLayer] at exit: probe float position ticks %llu, from the pull state max %.3f px mean %.3f px, "
			             "from matrix.t max %.3f px mean %.3f px\n",
			             s_nrCount.floatTicks, s_nrCount.floatDistanceMax,
			             (s_nrCount.floatTicks > 0) ? (s_nrCount.floatDistanceSum / (double)s_nrCount.floatTicks) : 0.0, s_nrCount.floatDistanceTMax,
			             (s_nrCount.floatTicks > 0) ? (s_nrCount.floatDistanceTSum / (double)s_nrCount.floatTicks) : 0.0);
		}
		Platform_Log("[CTR RenderLayer] at exit: seat %d frames without a dispatch while bound %llu\n", g_cfg_nativeProbeSeat,
		             s_nrCount.probeSeat0NoDispatch);

		// The wheels and the exhaust: new lines after the ones of before.
		{
			unsigned long long ticks = 0;
			unsigned long long starts = 0;
			unsigned long long signs[NATIVE_WHEEL_SIGNS];

			NativeWheels_Counts(&ticks, &starts, signs);
			Platform_Log("[CTR RenderLayer] at exit: probe seat %d retail wheel FT4: solid %llu, reflection %llu\n", g_cfg_nativeProbeSeat,
			             s_nrCount.tiresSolidFt4, s_nrCount.tiresReflectionFt4);
			Platform_Log("[CTR RenderLayer] at exit: probe native wheels: items %llu, wheel draws %llu, not drawn %llu; report lines float %llu, "
			             "exact %llu, retail %llu\n",
			             s_nrCount.wheelItems, s_nrCount.wheelDraws, s_nrCount.wheelNotDrawn, s_nrCount.wheelFloatLines, s_nrCount.wheelExactLines,
			             s_nrCount.wheelRetailLines);
			// The sign check of the probe seat alone: the ticks with motion fall
			// into agree, disagree and zero (roll step 0); still and none are
			// the ticks it does not count.
			Platform_Log("[CTR RenderLayer] at exit: probe seat %d wheel pose ticks %llu, starts %llu; sign check |move| >= %.4f world units: "
			             "agree %llu, disagree %llu, zero %llu; not counted: still %llu, none %llu\n",
			             g_cfg_nativeProbeSeat, ticks, starts, NATIVE_WHEELS_MOVING, signs[NATIVE_WHEEL_SIGN_AGREE], signs[NATIVE_WHEEL_SIGN_DISAGREE],
			             signs[NATIVE_WHEEL_SIGN_ZERO], signs[NATIVE_WHEEL_SIGN_STILL], signs[NATIVE_WHEEL_SIGN_NONE]);
			Platform_Log("[CTR RenderLayer] at exit: probe exhaust: frames with quads %llu, quads %llu\n", s_nrCount.exhaustFrames,
			             s_nrCount.exhaustQuads);
		}
	}

	// Step 4c, only in a run that made (or refused) a GPU set of a custom
	// character: a run without one keeps the report it had.
	if (NativeCharGpu_SetsMade() > 0u)
	{
		const unsigned long long *fb = s_nrCharCnt.fallback;
		int seat;

		Platform_Log("[CTR RenderLayer] at exit: native chars bound %llu frame(s), native draws %llu, frames with a native draw %llu, "
		             "most in one frame and view %d, not drawn %llu, stale items %llu\n",
		             s_nrCharCnt.boundFrames, s_nrCharCnt.draws, s_nrCharCnt.drawFrames, s_nrCharCnt.mostPerView, s_nrCharCnt.notDrawn,
		             s_nrCount.probeStale);
		Platform_Log("[CTR RenderLayer] at exit: native char fallbacks: normal alt %llu, split %llu, special %llu, reflection %llu, other %llu, "
		             "writer %llu, decal view %llu, borrowed range %llu, ui %llu, no range %llu, item list full %llu, arena full %llu\n",
		             fb[NR_PROBE_FALLBACK_NORMAL_ALT], fb[NR_PROBE_FALLBACK_SPLIT], fb[NR_PROBE_FALLBACK_SPECIAL], fb[NR_PROBE_FALLBACK_REFLECTION],
		             fb[NR_PROBE_FALLBACK_OTHER], fb[NR_PROBE_FALLBACK_WRITER], fb[NR_PROBE_FALLBACK_DECAL_VIEW], fb[NR_PROBE_FALLBACK_BORROWED_RANGE],
		             fb[NR_PROBE_FALLBACK_UI], fb[NR_PROBE_FALLBACK_NO_RANGE], fb[NR_PROBE_FALLBACK_ITEM_LIST_FULL], fb[NR_PROBE_FALLBACK_ARENA_FULL]);
		Platform_Log("[CTR RenderLayer] at exit: native char poses: drawn %llu, drawn = current %llu, mismatched %llu, without a pull value %llu, "
		             "still pose %llu, out of range %llu\n",
		             s_nrCharCnt.poseDrawn, s_nrCharCnt.poseMatched, s_nrCharCnt.poseMismatched, s_nrCharCnt.poseWithoutPull, s_nrCharCnt.poseStill,
		             s_nrCharCnt.poseOutOfRange);
		Platform_Log("[CTR RenderLayer] at exit: native char wheels: items %llu, wheel draws %llu, not drawn %llu; retail wheel FT4 in native views %llu, "
		             "in fallback views %llu\n",
		             s_nrCharCnt.wheelItems, s_nrCharCnt.wheelDraws, s_nrCharCnt.wheelNotDrawn, s_nrCharCnt.tiresNative, s_nrCharCnt.tiresFallback);
		for (seat = 0; seat < NATIVE_RENDER_LAYER_DRIVERS; seat++)
		{
			const struct NrCharSeam *seam = &s_nrCharSeam[seat];

			if (seam->view0Draws == 0)
			{
				continue;
			}
			Platform_Log("[CTR RenderLayer] at exit: native char seat %d shift: near %llu, far %llu, huge %llu, shift check off %llu, mirrored %llu, "
			             "composed matrix off %llu, changes between neighbouring native frames %llu (near to far %llu, far to near %llu), after a gap %llu, "
			             "depth jump at a change max %.2f percent (vblank %d, w %.1f to %.1f, ratio %.4f), depth step without a change max %.2f percent "
			             "over %llu pair(s)\n",
			             seat, seam->nearDraws, seam->farDraws, seam->hugeDraws, seam->shiftOff, seam->mirrored, seam->composedOff, seam->changes,
			             seam->nearToFar, seam->farToNear, seam->afterGap, seam->jumpMax, seam->jumpVBlank, seam->jumpW0, seam->jumpW1,
			             (seam->jumpW0 > 0.0) ? (seam->jumpW1 / seam->jumpW0) : 0.0, seam->stepMax, seam->stepPairs);
		}
		// Step 4f: per seat, and the frames of view 0 by their native draws.
		for (seat = 0; seat < NATIVE_RENDER_LAYER_DRIVERS; seat++)
		{
			const struct NrCharSeatCounters *c = &s_nrCharSeatCnt[seat];
			unsigned long long other = 0;
			int f;

			if ((c->dispatched == 0) && (c->draws == 0) && (c->notDrawn == 0))
			{
				continue;
			}
			for (f = 0; f < NR_PROBE_FALLBACKS; f++)
			{
				if ((f != NR_PROBE_FALLBACK_SPLIT) && (f != NR_PROBE_FALLBACK_REFLECTION))
				{
					other += c->fallback[f];
				}
			}
			Platform_Log("[CTR RenderLayer] at exit: native char seat %d: dispatched %llu, routed normal %llu, native draws %llu, not drawn %llu, "
			             "fallbacks split %llu reflection %llu other %llu\n",
			             seat, c->dispatched, c->routedNormal, c->draws, c->notDrawn, c->fallback[NR_PROBE_FALLBACK_SPLIT],
			             c->fallback[NR_PROBE_FALLBACK_REFLECTION], other);
		}
		Platform_Log("[CTR RenderLayer] at exit: native char frames by native draws in view 0: 0 %llu, 1 %llu, 2 %llu, 3 %llu, 4 %llu, 5 %llu, 6 %llu, "
		             "7 %llu, 8 %llu; frames where draws != routed %llu\n",
		             s_nrCharFramesByDraws[0], s_nrCharFramesByDraws[1], s_nrCharFramesByDraws[2], s_nrCharFramesByDraws[3], s_nrCharFramesByDraws[4],
		             s_nrCharFramesByDraws[5], s_nrCharFramesByDraws[6], s_nrCharFramesByDraws[7], s_nrCharFramesByDraws[8], s_nrCharFramesDrawsOff);

		// Step 4e-1: the water line, counted in the fallback.
		Platform_Log("[CTR RenderLayer] at exit: native char split: frames %llu, accepted %llu, selector both-mask %llu, negative %llu, xor %llu, "
		             "dim-xor %llu, other %llu, mask not 0x7f7f7f %llu\n",
		             s_nrCharSplit.views, s_nrCharSplit.accepted, s_nrCharSplit.bothMask, s_nrCharSplit.negative, s_nrCharSplit.xorSide,
		             s_nrCharSplit.dimXor, s_nrCharSplit.otherSelector, s_nrCharSplit.maskOff);
		Platform_Log("[CTR RenderLayer] at exit: native char split shift: 1 %llu, other %llu; second range of its own %llu, shared %llu\n",
		             s_nrCharSplit.shift1, s_nrCharSplit.shiftOther, s_nrCharSplit.secondaryOwn, s_nrCharSplit.secondaryShared);

		// Step 4d: the retail twin, only in a run with --native-twin.
		if (g_cfg_nativeTwin)
		{
			Platform_Log("[CTR RenderLayer] at exit: native twin bound %llu frame(s), routed %llu, native draws %llu, not drawn %llu, "
			             "poses without a pull value %llu, out of range %llu\n",
			             s_nrTwinCnt.boundFrames, s_nrTwinCnt.routed, s_nrTwinCnt.draws, s_nrTwinCnt.notDrawn, s_nrTwinCnt.poseWithoutPull,
			             s_nrTwinCnt.poseOutOfRange);
			Platform_Log("[CTR RenderLayer] at exit: native twin look: toned draws %llu, dithered draws %llu, far colour zero %llu, setup unknown %llu\n",
			             s_nrTwinCnt.toned, s_nrTwinCnt.dithered, s_nrTwinCnt.farZero, s_nrTwinCnt.setupOther);
		}

		// Step 5a/5c: where native characters were bound.
		Platform_Log("[CTR RenderLayer] at exit: native char binds by mode: race %llu, preview %llu, refused adventure %llu, cutscene %llu, other %llu; "
		             "preview outside the driver select %llu, preview box outside the clip %llu\n",
		             s_nrPreviewCnt.raceFrames, s_nrPreviewCnt.frames, s_nrPreviewCnt.refusedAdventure, s_nrPreviewCnt.refusedCutscene,
		             s_nrPreviewCnt.refusedOther, s_nrPreviewCnt.outsideSelect, s_nrPreviewCnt.boxOutsideClip);

		// The factor rule: frames in which a seat or the preview would have
		// bound and factor 1 left it to the CMDL (none of them is in the lines
		// above - not bound, not routed, no fallback).
		Platform_Log("[CTR RenderLayer] at exit: native chars at factor 1: frames %llu (drawn by CMDL), seat frames %llu, preview frames %llu; "
		             "factor at exit %d%s\n",
		             s_nrCharFactorCnt.frames, s_nrCharFactorCnt.seatFrames, s_nrCharFactorCnt.previewFrames, s_nrCharFactor,
		             s_nrCharFactorNative ? " (NATIVE)" : "");

		NativeCharGpu_ReportLine();
	}
}

// Every frame from MainFrame_RegisterGpuLinkRanges, after the six ranges of
// the game: the link map is rebuilt per frame, and ranges registered later get
// lower tokens, so registering last leaves every existing token as it is.
// Without --native-preview nothing is registered and nothing changes.
void NativeRenderLayer_RegisterMarkerArenas(struct GameTracker *gGT)
{
	extern int g_cfg_nativePreview;

	if (!g_cfg_nativePreview)
	{
		return;
	}

	NativeGpuLinks_RegisterRangeChecked("db0 native markers", s_nrMarkers[0], sizeof(s_nrMarkers[0]));
	NativeGpuLinks_RegisterRangeChecked("db1 native markers", s_nrMarkers[1], sizeof(s_nrMarkers[1]));

	// Called right after the buffers swapped: the arena of the buffer that is
	// filled now starts empty. The other one stays as it is - its OT may
	// still be read.
	s_nrMarkerDb = (gGT->swapchainIndex != 0) ? 1 : 0;
	s_nrMarkerCount[s_nrMarkerDb] = 0;

	// The native list of that buffer goes with its arena, and a new frame
	// starts for the stamps of the native draws.
	s_nrItemCount[s_nrMarkerDb] = 0;
	s_nrFrame++;
}

// THE MARKER REPORT. What the layer wrote against what the parser met: in a
// run that ends after a drawn frame both must be equal. The counts of the
// parser side come from native_gpu.c.
void NativeRenderLayer_MarkerReport(void)
{
	struct NativeGpuMarkerCounts gpu;
	char equal[64];

	memset(&gpu, 0, sizeof(gpu));
	NativeGpu_MarkerCounts(&gpu);

	if (gpu.parsed == s_nrCount.markersWritten)
	{
		snprintf(equal, sizeof(equal), "yes");
	}
	else
	{
		snprintf(equal, sizeof(equal), "no (written - parsed = %lld)", (long long)(s_nrCount.markersWritten - gpu.parsed));
	}

	Platform_Log("[CTR RenderLayer] at exit: markers written %llu, parsed %llu, native splits %llu, reached DrawSplit %llu, "
	             "refused by a full split table %llu, arena full %llu\n",
	             s_nrCount.markersWritten, gpu.parsed, gpu.splits, gpu.drawCalls, gpu.refused, s_nrCount.markerArenaFull);
	Platform_Log("[CTR RenderLayer] at exit: markers parsed = written: %s\n", equal);
	Platform_Log("[CTR RenderLayer] at exit: empty markers skipped: handler %llu, decal view %llu, borrowed range %llu, ui %llu, "
	             "no range %llu; splits after a native one %llu\n",
	             s_nrCount.emptySkipHandler, s_nrCount.emptySkipDecalView, s_nrCount.emptySkipBorrowedRange, s_nrCount.emptySkipUi,
	             s_nrCount.emptySkipNoRange, gpu.afterNative);
}

// For the whole run, whether bound or not: a quick state or a recording that
// starts mid-run could not hold the probe.
int NativeRenderLayer_ProbeActive(void)
{
	return g_cfg_nativePreview && (g_cfg_nativeProbe != NATIVE_PROBE_NONE);
}

// Called per wheel set and frame, so one comparison while nothing is bound.
// --native-wheel-report keeps the retail wheels of the bound model on: the
// measuring set-up "probe with retail wheels" (render plan D.4, step 3a).
int NativeRenderLayer_ModelHidesWheels(const struct Model *model)
{
	return (s_nativeModelCount != 0) && (model == s_probeModel) && !g_cfg_nativeWheelReport;
}

// THE RETAIL WHEELS OF THE PROBE SEAT, from game/DrawTires.c. Begin and End
// frame one wheel set of one view; Corners hands over the four projected
// corners of a wheel (the packed screen words DrawTires read from the GTE,
// nothing computed anew), Primitive says that DrawTires wrote and linked the quad
// of that wheel. Only for the instance the probe is bound to; for every other
// one Begin leaves at its first comparison. Nothing is written but this file's
// statics.
void NativeRenderLayer_TiresBegin(const struct Instance *inst, const struct PushBuffer *pb, int reflection)
{
	const struct GameTracker *gGT = sdata->gGT;

	s_nrTires.active = 0;

	// Step 4c: the retail wheels of a custom character drawn natively,
	// counted per kind of view; 0 at once without one.
	s_nrCharTiresActive = 0;
	if ((s_nrCharCount != 0) && (inst != NULL) && (NativeRenderLayer_CharIndex(inst->model) >= 0) && !s_nrChar[NativeRenderLayer_CharIndex(inst->model)].twin &&
	    (NativeRenderLayer_SeatOfInst(inst) >= 0))
	{
		int wheels = 0;

		// "Native" by the stamp alone: a native view whose retail wheels get
		// this far (its item had no native wheels) is one the counter names.
		s_nrCharTiresActive = 1;
		s_nrCharTiresNative = NativeRenderLayer_CharViewStamp(inst, pb, &wheels);
	}

	if ((s_nativeModelCount == 0) || (inst == NULL) || (inst != s_probeInst) || (gGT == NULL))
	{
		return;
	}

	memset(&s_nrTires, 0, sizeof(s_nrTires));
	s_nrTires.active = 1;
	s_nrTires.reflection = reflection;
	s_nrTires.view0 = (pb == &gGT->pushBuffer[0]);
	s_nrTires.wheel = -1;
}

void NativeRenderLayer_TiresCorners(int wheelIndex, const s32 sxy[4])
{
	int corner;

	if (!s_nrTires.active || (wheelIndex < 0) || (wheelIndex > 3))
	{
		return;
	}

	s_nrTires.wheel = wheelIndex;
	for (corner = 0; corner < 4; corner++)
	{
		s_nrTires.corners[wheelIndex][corner][0] = (s16)((u32)sxy[corner] & 0xffffu);
		s_nrTires.corners[wheelIndex][corner][1] = (s16)((u32)sxy[corner] >> 16);
	}
}

void NativeRenderLayer_TiresPrimitive(void)
{
	if (s_nrCharTiresActive)
	{
		if (s_nrCharTiresNative)
		{
			s_nrCharCnt.tiresNative++;
		}
		else
		{
			s_nrCharCnt.tiresFallback++;
		}
	}

	if (!s_nrTires.active)
	{
		return;
	}

	if (s_nrTires.reflection)
	{
		s_nrCount.tiresReflectionFt4++;
		return;
	}

	s_nrCount.tiresSolidFt4++;
	if ((s_nrTires.wheel >= 0) && (s_nrTires.wheel <= 3))
	{
		s_nrTires.emitted[s_nrTires.wheel] = 1;
	}
}

// THE RETAIL LINE of --native-wheel-report: in the frame of the float line, for
// the solid pass of view 0, the middle of every wheel quad DrawTires emitted -
// the mean of its four projected corners (GTE screen pixels, integers, before
// the draw offset) - and "none" for a wheel without a quad.
void NativeRenderLayer_TiresEnd(void)
{
	const struct NrTires tires = s_nrTires;
	char wheels[4][48];
	int wheel;

	s_nrTires.active = 0;
	s_nrCharTiresActive = 0;

	if (!tires.active || tires.reflection || !tires.view0 || !g_cfg_nativeWheelReport || !s_nrWheelSample || s_nrWheelRetailDone)
	{
		return;
	}
	s_nrWheelRetailDone = 1;
	s_nrCount.wheelRetailLines++;

	for (wheel = 0; wheel < 4; wheel++)
	{
		if (!tires.emitted[wheel])
		{
			snprintf(wheels[wheel], sizeof(wheels[wheel]), "none");
			continue;
		}

		snprintf(wheels[wheel], sizeof(wheels[wheel]), "%.2f %.2f",
		         (double)(tires.corners[wheel][0][0] + tires.corners[wheel][1][0] + tires.corners[wheel][2][0] + tires.corners[wheel][3][0]) / 4.0,
		         (double)(tires.corners[wheel][0][1] + tires.corners[wheel][1][1] + tires.corners[wheel][2][1] + tires.corners[wheel][3][1]) / 4.0);
	}

	Platform_Log("[CTR RenderLayer] wheel retail at vblank %d tick %u: w0 %s, w1 %s, w2 %s, w3 %s\n", Platform_GetVBlankCount(), s_nrWheelSampleTimer,
	             wheels[0], wheels[1], wheels[2], wheels[3]);
}

// THE EXHAUST OF THE PROBE SEAT, from game/Particle.c, Particle_RenderList, for
// every quad it has just written and linked: only its four corners are read
// (the screen words the GTE gave, written into the quad). Kept are the quads of
// view 0 of a particle that belongs to the bound instance and draws in its
// range (PARTICLE_SET_COLOR_FLAG_DRIVER_LOCAL), with the icons of the exhaust:
// icon group 1, or 7 under water (game/Vehicle/VehEmitter.c:6-7, 102, 128,
// 132, 146). The burn smoke (game/Vehicle/VehFrame.c:88-95) has the same icon
// group and owner and is kept with it - for a mask the larger box is the safe
// side. One box per frame, written when the frame is closed.
void NativeRenderLayer_NoteParticleQuad(const struct Particle *particle, const struct PushBuffer *pb, const POLY_FT4 *poly)
{
	const struct GameTracker *gGT = sdata->gGT;
	int x[4];
	int y[4];
	int corner;

	if ((s_nativeModelCount == 0) || (particle == NULL) || (poly == NULL) || (gGT == NULL) || (particle->owner.driverInst != s_probeInst) ||
	    (pb != &gGT->pushBuffer[0]) || ((particle->flagsSetColor & PARTICLE_SET_COLOR_FLAG_DRIVER_LOCAL) == 0) ||
	    ((particle->ptrIconGroup != gGT->iconGroup[1]) && (particle->ptrIconGroup != gGT->iconGroup[7])))
	{
		return;
	}

	x[0] = poly->x0;
	y[0] = poly->y0;
	x[1] = poly->x1;
	y[1] = poly->y1;
	x[2] = poly->x2;
	y[2] = poly->y2;
	x[3] = poly->x3;
	y[3] = poly->y3;

	for (corner = 0; corner < 4; corner++)
	{
		const int first = (s_nrExhaust.quads == 0) && (corner == 0);

		if (first || (x[corner] < s_nrExhaust.box[0]))
		{
			s_nrExhaust.box[0] = x[corner];
		}
		if (first || (y[corner] < s_nrExhaust.box[1]))
		{
			s_nrExhaust.box[1] = y[corner];
		}
		if (first || (x[corner] > s_nrExhaust.box[2]))
		{
			s_nrExhaust.box[2] = x[corner];
		}
		if (first || (y[corner] > s_nrExhaust.box[3]))
		{
			s_nrExhaust.box[3] = y[corner];
		}
	}

	// The box of this quad alone, for the quad line.
	if (s_nrExhaust.quads < NR_EXHAUST_QUADS_LISTED)
	{
		int *b = s_nrExhaust.quadBox[s_nrExhaust.quads];

		b[0] = x[0];
		b[1] = y[0];
		b[2] = x[0];
		b[3] = y[0];
		for (corner = 1; corner < 4; corner++)
		{
			b[0] = (x[corner] < b[0]) ? x[corner] : b[0];
			b[1] = (y[corner] < b[1]) ? y[corner] : b[1];
			b[2] = (x[corner] > b[2]) ? x[corner] : b[2];
			b[3] = (y[corner] > b[3]) ? y[corner] : b[3];
		}
	}

	s_nrExhaust.quads++;
	s_nrExhaust.db = s_nrMarkerDb;
	s_nrCount.exhaustQuads++;
}

// --native-hide-exhaust (main.c, only with --dev; measuring only, never saved):
// the colour checks of the native probe run once without the exhaust glow, the
// reference run and the probe run alike. Set in the first loop of main.
int g_cfg_nativeHideExhaust = 0;

// Quads left out by --native-hide-exhaust, per icon group, for the exit line.
static struct
{
	unsigned long long group1;
	unsigned long long group7;
	unsigned long long group8;
} s_nrHiddenExhaust;

// HIDING THE EXHAUST, from game/Particle.c, Particle_RenderList, for every
// normal particle quad just written, before it is linked. 1 = the quad is not
// linked into the ordering table (the caller still moves its cursors past it),
// 0 = linked as always. Without the switch it is always 0 and nothing else
// happens. With it, every seat and every view: a particle that draws in the
// range of its driver (PARTICLE_SET_COLOR_FLAG_DRIVER_LOCAL, with an owner)
// with the icons of the exhaust - icon group 1, or 7 under water
// (game/Vehicle/VehEmitter.c:6-7, 102, 128, 132, 146), or 8, which an
// underwater exhaust particle takes for its last bubble pop
// (game/Particle.c, Particle_FuncPtr_ExhaustUnderwater). The burn smoke
// (game/Vehicle/VehFrame.c:12, 88-95) has the same icon group, the same flag
// and an owner and is hidden with it. Other particles of groups 1 and 7 (potion
// shatter, orca splash, warp pad dust, tube bubbles) have no
// PARTICLE_SET_COLOR_FLAG_DRIVER_LOCAL and stay. Only reads the particle and
// counts in host memory.
int NativeRenderLayer_HideExhaustQuad(const struct Particle *particle)
{
	const struct GameTracker *gGT = sdata->gGT;
	const struct IconGroup *group;

	if (!g_cfg_nativeHideExhaust || (particle == NULL) || (gGT == NULL) || ((particle->flagsSetColor & PARTICLE_SET_COLOR_FLAG_DRIVER_LOCAL) == 0) ||
	    (particle->owner.driverInst == NULL) || (particle->ptrIconGroup == NULL))
	{
		return 0;
	}

	group = particle->ptrIconGroup;
	if (group == gGT->iconGroup[1])
	{
		s_nrHiddenExhaust.group1++;
	}
	else if (group == gGT->iconGroup[7])
	{
		s_nrHiddenExhaust.group7++;
	}
	else if (group == gGT->iconGroup[8])
	{
		s_nrHiddenExhaust.group8++;
	}
	else
	{
		return 0;
	}

	return 1;
}

// At exit, only in a run with --native-hide-exhaust (main.c registers it).
void NativeRenderLayer_HideExhaustReport(void)
{
	Platform_Log("[CTR RenderLayer] at exit: exhaust quads not drawn %llu (icon group 1: %llu, 7: %llu, 8: %llu)\n",
	             s_nrHiddenExhaust.group1 + s_nrHiddenExhaust.group7 + s_nrHiddenExhaust.group8, s_nrHiddenExhaust.group1, s_nrHiddenExhaust.group7,
	             s_nrHiddenExhaust.group8);
}

internal float NativeRenderLayer_DepthTint(const struct NrDrawItem *it);

// THE WHEEL DRAWS of the form wheels: per wheel its two halves (platform/
// native_probe.c), each through the renderer's probe draw with a matrix of its
// own - the screen mapping of NativeRenderLayer_ItemMatrix (H, half the view
// plus the draw offset, zNear = H / 8) applied to view = wheelA * u + wheelB -
// and a region of the pose buffer of its own (the body's region + 1 + 2 * wheel
// + half). The right-hand wheels are the left-hand mesh turned by 180 degrees
// about y (their frame, platform/native_wheels.c), never mirrored, so the cull
// of the body holds for them.
internal void NativeRenderLayer_DrawWheels(const struct NrDrawItem *it, int bodyRegion, const RECT16 *clip, const DISPENV *dispenv, int onScreen,
                                           double ofsX, double ofsY)
{
	const double H = (double)it->H;
	const double ofx = (double)(it->rectW >> 1) + ofsX;
	const double ofy = (double)(it->rectH >> 1) + ofsY;
	const double zNear = H / 8.0;
	int wheel;
	int half;

	for (wheel = 0; wheel < NATIVE_WHEELS_COUNT; wheel++)
	{
		const double(*A)[3] = it->wheelA[wheel];
		const double *b = it->wheelB[wheel];
		double S[4][4];
		int r;
		int c;

		for (c = 0; c < 3; c++)
		{
			S[0][c] = (H * A[0][c]) + (ofx * A[2][c]);
			S[1][c] = (H * A[1][c]) + (ofy * A[2][c]);
			S[2][c] = 0.0;
			S[3][c] = A[2][c];
		}
		S[0][3] = (H * b[0]) + (ofx * b[2]);
		S[1][3] = (H * b[1]) + (ofy * b[2]);
		S[2][3] = zNear;
		S[3][3] = b[2];

		for (half = 0; half < NATIVE_PROBE_WHEEL_HALVES; half++)
		{
			struct NativeLayerDraw draw;

			memset(&draw, 0, sizeof(draw));
			for (c = 0; c < 4; c++)
			{
				for (r = 0; r < 4; r++)
				{
					draw.screenFromModel[(c * 4) + r] = (float)S[r][c];
				}
			}
			draw.cull = (int)it->cull;
			draw.clearDepth = 0;
			draw.depthTint = NativeRenderLayer_DepthTint(it);
			draw.vertices = NativeProbe_WheelVertices(half);
			draw.vertexRegion = bodyRegion + 1 + (wheel * NATIVE_PROBE_WHEEL_HALVES) + half;

			if (NativeRenderer_DrawNativeProbe(&draw, clip, dispenv, onScreen))
			{
				s_nrCount.wheelDraws++;
			}
			else
			{
				s_nrCount.wheelNotDrawn++;
			}
		}
	}
}


// --- Step 4c: drawing a char item --------------------------------------------

// The screen matrix of a map view = A u + b (true view units), as
// NativeRenderLayer_ItemMatrix builds it for the probe body.
internal void NativeRenderLayer_ScreenFromView(const struct NrDrawItem *it, const double A[3][3], const double b[3], double ofsX, double ofsY, double S[4][4])
{
	const double H = (double)it->H;
	const double ofx = (double)(it->rectW >> 1) + ofsX;
	const double ofy = (double)(it->rectH >> 1) + ofsY;
	const double zNear = H / 8.0;
	int c;

	for (c = 0; c < 3; c++)
	{
		S[0][c] = (H * A[0][c]) + (ofx * A[2][c]);
		S[1][c] = (H * A[1][c]) + (ofy * A[2][c]);
		S[2][c] = 0.0;
		S[3][c] = A[2][c];
	}
	S[0][3] = (H * b[0]) + (ofx * b[2]);
	S[1][3] = (H * b[1]) + (ofy * b[2]);
	S[2][3] = zNear;
	S[3][3] = b[2];
}

// THE MATRIX OF A CHAR ITEM: from model units (the CNET positions) to the
// screen. view = (mvp.m / 4096 * g + mvp.t) / 2^shift with g = model x 16384 /
// mh->scale per axis, the vertex input units of the queue; the origin of the
// model units is the model's own.
internal void NativeRenderLayer_CharItemMatrix(const struct NrDrawItem *it, double ofsX, double ofsY, double S[4][4])
{
	const double s = ldexp(1.0, (int)it->mvpShift);
	double A[3][3];
	double b[3];
	int r;
	int c;

	for (r = 0; r < 3; r++)
	{
		for (c = 0; c < 3; c++)
		{
			A[r][c] = (((double)it->mvp[r][c] / 4096.0) * it->unitScale[c]) / s;
		}
		b[r] = (double)it->mvpT[r] / s;
	}
	NativeRenderLayer_ScreenFromView(it, A, b, ofsX, ofsY, S);
}

internal void NativeRenderLayer_GrowBox(int box[4], const int add[4], int *have)
{
	if (!*have)
	{
		memcpy(box, add, sizeof(int) * 4u);
		*have = 1;
		return;
	}
	box[0] = (add[0] < box[0]) ? add[0] : box[0];
	box[1] = (add[1] < box[1]) ? add[1] : box[1];
	box[2] = (add[2] > box[2]) ? add[2] : box[2];
	box[3] = (add[3] > box[3]) ? add[3] : box[3];
}

// A box in view space (middle +- extent per axis) through the screen mapping
// of the split; 0 when a corner lies behind the eye.
internal int NativeRenderLayer_ViewBox(const struct NrDrawItem *it, const double middle[3], const double extent[3], double ofsX, double ofsY, int box[4])
{
	const double H = (double)it->H;
	const double ofx = (double)(it->rectW >> 1) + ofsX;
	const double ofy = (double)(it->rectH >> 1) + ofsY;
	double lo[2] = {0.0, 0.0};
	double hi[2] = {0.0, 0.0};
	int corner;

	for (corner = 0; corner < 8; corner++)
	{
		const double x = middle[0] + ((((corner & 1) != 0) ? 1.0 : -1.0) * extent[0]);
		const double y = middle[1] + ((((corner & 2) != 0) ? 1.0 : -1.0) * extent[1]);
		const double z = middle[2] + ((((corner & 4) != 0) ? 1.0 : -1.0) * extent[2]);
		double sx;
		double sy;

		if (z <= 0.0)
		{
			return 0;
		}
		sx = ofx + ((H * x) / z);
		sy = ofy + ((H * y) / z);
		if ((corner == 0) || (sx < lo[0]))
		{
			lo[0] = sx;
		}
		if ((corner == 0) || (sy < lo[1]))
		{
			lo[1] = sy;
		}
		if ((corner == 0) || (sx > hi[0]))
		{
			hi[0] = sx;
		}
		if ((corner == 0) || (sy > hi[1]))
		{
			hi[1] = sy;
		}
	}
	box[0] = (int)floor(lo[0]);
	box[1] = (int)floor(lo[1]);
	box[2] = (int)ceil(hi[0]);
	box[3] = (int)ceil(hi[1]);
	return 1;
}

// The CNET hull of a char item on the screen (model units through S).
internal int NativeRenderLayer_CharBodyBox(const struct NrDrawItem *it, const double S[4][4], int box[4])
{
	const double lo[3] = {(double)it->gpu->hullMin[0], (double)it->gpu->hullMin[1], (double)it->gpu->hullMin[2]};
	const double hi[3] = {(double)it->gpu->hullMax[0], (double)it->gpu->hullMax[1], (double)it->gpu->hullMax[2]};

	return NativeRenderLayer_ProjectBox(S, lo, hi, box);
}

// THE BOX LINE of a char item, on the line spacing, only with
// --native-layer-report, one per seat and VBlank: the body (CNET hull), the
// hull of the retail model, the retail wheels of the reference run, the native
// wheels (middle +- radius + half width + 2 per axis in view space), their
// union and the middle of the back of the body.
internal void NativeRenderLayer_CharBoxLine(const struct NrDrawItem *it, const double S[4][4], double ofsX, double ofsY)
{
	const int vblank = Platform_GetVBlankCount();
	struct NrCharSeam *seam = &s_nrCharSeam[it->seat];
	double hullLo[3];
	double hullHi[3];
	int box[4];
	int hull[4];
	int wheels[4] = {0, 0, 0, 0};
	int native[4] = {0, 0, 0, 0};
	int all[4] = {0, 0, 0, 0};
	int haveWheels = 0;
	int haveNative = 0;
	int haveAll = 0;
	double centerX = 0.0;
	double centerY = 0.0;
	int known;
	int axis;
	int wheel;
	char nativeText[64];

	if (!g_cfg_nativeLayerReport || ((vblank % NATIVE_RENDER_LAYER_PROBE_LINE_STEP) != 0) || (vblank == seam->boxLineVBlank))
	{
		return;
	}
	seam->boxLineVBlank = vblank;

	for (axis = 0; axis < 3; axis++)
	{
		hullLo[axis] = (double)it->hullMin[axis] / it->unitScale[axis];
		hullHi[axis] = ((double)it->hullMin[axis] + (double)NATIVE_RENDER_LAYER_HULL_SIZE) / it->unitScale[axis];
	}

	known = NativeRenderLayer_CharBodyBox(it, S, box) && NativeRenderLayer_ProjectBox(S, hullLo, hullHi, hull) &&
	        NativeRenderLayer_ProjectUnit(S, ((double)it->gpu->hullMin[0] + (double)it->gpu->hullMax[0]) * 0.5,
	                                      ((double)it->gpu->hullMin[1] + (double)it->gpu->hullMax[1]) * 0.5, (double)it->gpu->hullMin[2], &centerX,
	                                      &centerY) &&
	        (it->wheels != 0);

	for (wheel = 0; known && (wheel < 4); wheel++)
	{
		const double middle[3] = {(double)it->wheelView[wheel][0], (double)it->wheelView[wheel][1], (double)it->wheelView[wheel][2]};
		const double extent[3] = {(double)it->wheelExt[0], (double)it->wheelExt[1], (double)it->wheelExt[2]};
		int one[4];

		known = NativeRenderLayer_ViewBox(it, middle, extent, ofsX, ofsY, one);
		if (known)
		{
			NativeRenderLayer_GrowBox(wheels, one, &haveWheels);
		}
	}

	if (!known)
	{
		Platform_Log("[CTR RenderLayer] native char seat %d at vblank %d: native, box unknown\n", (int)it->seat, vblank);
		return;
	}

	NativeRenderLayer_GrowBox(all, box, &haveAll);
	NativeRenderLayer_GrowBox(all, hull, &haveAll);
	NativeRenderLayer_GrowBox(all, wheels, &haveAll);

	if (it->nativeWheels)
	{
		const double r = (double)it->gpu->wheelRadius + (double)it->gpu->wheelHalfWidth + 2.0;
		const double extent[3] = {r, r, r};
		int ok = 1;

		for (wheel = 0; ok && (wheel < NATIVE_WHEELS_COUNT); wheel++)
		{
			int one[4];

			ok = NativeRenderLayer_ViewBox(it, it->wheelB[wheel], extent, ofsX, ofsY, one);
			if (ok)
			{
				NativeRenderLayer_GrowBox(native, one, &haveNative);
			}
		}
		haveNative = ok && haveNative;
	}

	if (haveNative)
	{
		NativeRenderLayer_GrowBox(all, native, &haveAll);
		snprintf(nativeText, sizeof(nativeText), "%d %d %d %d", native[0], native[1], native[2], native[3]);
	}
	else
	{
		snprintf(nativeText, sizeof(nativeText), "none");
	}

	Platform_Log("[CTR RenderLayer] native char seat %d at vblank %d: native, box %d %d %d %d, hull %d %d %d %d, wheels %d %d %d %d, "
	             "native wheels %s, union %d %d %d %d, center x %d\n",
	             (int)it->seat, vblank, box[0], box[1], box[2], box[3], hull[0], hull[1], hull[2], hull[3], wheels[0], wheels[1], wheels[2], wheels[3],
	             nativeText, all[0], all[1], all[2], all[3], (int)floor(centerX + 0.5));
}

// THE SEAM LINE (--native-seam-report): per native item of view 0, at most one
// per seat and VBlank.
internal void NativeRenderLayer_SeamLine(const char *kind, int seat, const struct NrDrawItem *it, double w, int knownBox, const int box[4], int *lastVBlank)
{
	const int vblank = Platform_GetVBlankCount();
	const double zNear = (double)it->H / 8.0;
	char side[16];
	char boxText[64];

	if (!g_cfg_nativeSeamReport || (vblank == *lastVBlank))
	{
		return;
	}
	*lastVBlank = vblank;

	snprintf(side, sizeof(side), "%s%s", it->nearView ? "near" : "far", ((it->mvpShift == 0) && it->nearView) || (it->mvpShift < 0) ? " huge" : "");
	if (knownBox)
	{
		snprintf(boxText, sizeof(boxText), "%d %d %d %d", box[0], box[1], box[2], box[3]);
	}
	else
	{
		snprintf(boxText, sizeof(boxText), "unknown");
	}
	Platform_Log("[CTR RenderLayer] seam at vblank %d: %s seat %d, view z %d, shift %d (%s), w %.3f, depth %.7f, box %s\n", vblank, kind, seat, (int)it->viewZ,
	             (int)it->mvpShift, side, w, (w > 0.0) ? (zNear / w) : 0.0, boxText);
}

// The depth colour of --native-depth-tint for an item: 1024 / zNear.
internal float NativeRenderLayer_DepthTint(const struct NrDrawItem *it)
{
	if (!g_cfg_nativeDepthTint || (it->H <= 0))
	{
		return 0.0f;
	}
	return (float)(1024.0 / ((double)it->H / 8.0));
}

internal void NativeRenderLayer_MatrixToDraw(const double S[4][4], float out[16])
{
	int r;
	int c;

	for (c = 0; c < 4; c++)
	{
		for (r = 0; r < 4; r++)
		{
			out[(c * 4) + r] = (float)S[r][c];
		}
	}
}

// THE DRAW OF A CHAR ITEM: the body (every material range, the pose by vertex
// offset), then its four native wheels in the same split, the same depth (not
// cleared again) and the same cull; then the counters, the seam and the box line.
internal void NativeRenderLayer_DrawCharItem(const struct NrDrawItem *it, const RECT16 *clip, const DISPENV *dispenv, int onScreen, float ofsX, float ofsY)
{
	const struct NativeCharGpu *gpu = it->gpu;
	const int view = (int)it->view;
	const int seat = (int)it->seat;
	struct NativeMeshRangeDraw ranges[NATIVE_CHAR_GPU_MATERIALS];
	struct NativeMeshDraw mesh;
	double S[4][4];
	u32 r;
	int firstInFrame;
	double twinShift = 0.0;
	const struct NativeTwinSource *twinSrc = it->twin ? NativeCharGpu_TwinSource() : NULL;
	const float ditherAmount = it->twin ? NativeRenderer_PsxDitherAmountNow() : 0.0f;
	int dithered = 0;

	if ((gpu == NULL) || (gpu->state != NATIVE_CHAR_GPU_READY) || (seat >= NATIVE_RENDER_LAYER_DRIVERS))
	{
		s_nrCharCnt.notDrawn++;
		if (seat < NATIVE_RENDER_LAYER_DRIVERS)
		{
			s_nrCharSeatCnt[seat].notDrawn++;
		}
		return;
	}

	// The twin (Z1): moved by -0.5 px as the retail corners are cut, while
	// --subpixel is off. The dither grid is the fragment's own place (nrt), so
	// it needs nothing of this shift.
	if (it->twin && !g_cfg_subpixel)
	{
		twinShift = 0.5;
	}

	NativeRenderLayer_CharItemMatrix(it, (double)ofsX - twinShift, (double)ofsY - twinShift, S);

	memset(ranges, 0, sizeof(ranges));
	for (r = 0; r < gpu->rangeCount; r++)
	{
		const struct NativeCharRange *range = &gpu->range[r];
		const int texture = gpu->materialTexture[range->material];
		struct NativeMeshRangeDraw *out = &ranges[r];

		if (it->twin && (twinSrc != NULL))
		{
			const struct NativeTwinMaterial *look = &twinSrc->material[range->material];

			out->modulation = (float)look->modulation;
			out->ditherWeight = (look->dither || activeDrawEnv.dtd) ? ditherAmount : 0.0f;
			if ((texture >= 0) && ((u32)texture < twinSrc->native.textureCount) && (twinSrc->native.texture[texture].width > 0u) &&
			    (twinSrc->native.texture[texture].height > 0u))
			{
				out->uvFudge[0] = 0.00025f / (float)twinSrc->native.texture[texture].width;
				out->uvFudge[1] = 0.00025f / (float)twinSrc->native.texture[texture].height;
			}
			if (out->ditherWeight > 0.0f)
			{
				dithered = 1;
			}
		}

		out->firstIndex = range->firstIndex;
		out->indexCount = range->indexCount;
		out->texture = (texture >= 0) ? gpu->texture[texture] : NATIVE_GFX_INVALID;
		out->srgb = (texture >= 0) ? (int)gpu->textureSrgb[texture] : 0;
		memcpy(out->tint, gpu->materialTint[range->material], sizeof(out->tint));
		out->alphaCutoff = range->mask ? 0.5f : 0.0f;
	}

	memset(&mesh, 0, sizeof(mesh));
	NativeRenderLayer_MatrixToDraw(S, mesh.screenFromModel);
	mesh.cull = (int)it->cull;
	mesh.clearDepth = (s_nrDepthClearFrame[view] != s_nrFrame) ? 1 : 0;
	mesh.vertexBuffer = gpu->bodyVB;
	mesh.indexBuffer = gpu->bodyIB;
	mesh.vertexOffset = (int)((u32)it->pose * gpu->vertexCount);
	mesh.rangeCount = (int)gpu->rangeCount;
	mesh.ranges = ranges;
	mesh.depthTint = NativeRenderLayer_DepthTint(it);
	if (it->twin)
	{
		mesh.look = 1;
		mesh.tone = it->tone;
		mesh.far[0] = it->far[0];
		mesh.far[1] = it->far[1];
		mesh.far[2] = it->far[2];
		mesh.far[3] = 1.0f;
	}

	if (NativeRenderer_DrawNativeMesh(&mesh, clip, dispenv, onScreen) == 0)
	{
		s_nrCharCnt.notDrawn++;
		s_nrCharSeatCnt[seat].notDrawn++;
		if (it->twin)
		{
			s_nrTwinCnt.notDrawn++;
		}
		return;
	}

	s_nrCharCnt.draws++;
	s_nrCharSeatCnt[seat].draws++;
	if (it->twin)
	{
		s_nrTwinCnt.draws++;
		if (it->tone > 0.0f)
		{
			s_nrTwinCnt.toned++;
		}
		if (dithered)
		{
			s_nrTwinCnt.dithered++;
		}
	}

	// The preview window: its body box has to lie in the clip of the split
	// (step 5a), in the same pixels (draw offset included) - counted only while
	// the model rests: while D230.characterSelectModelMoveTimer[0] is not 0 the
	// model flies out or in (game/230/MM_Characters.c:319-372 moves
	// matrix.t[0] by the slide offset) and leaves the window on purpose. Only
	// read.
	if (s_nrPreviewBound && (seat == 0) && (clip != NULL) && (D230.characterSelectModelMoveTimer[0] == 0))
	{
		int box[4];

		if (!NativeRenderLayer_CharBodyBox(it, S, box) || (box[0] < clip->x) || (box[1] < clip->y) || (box[2] > (clip->x + clip->w)) ||
		    (box[3] > (clip->y + clip->h)))
		{
			s_nrPreviewCnt.boxOutsideClip++;
		}
	}
	if (view == 0)
	{
		s_nrCharFrame0.drawn++;
	}
	s_nrCharSeatFrame[seat].drawn = 1;
	if (mesh.clearDepth)
	{
		s_nrDepthClearFrame[view] = s_nrFrame;
	}

	// The wheels.
	if (it->nativeWheels && (gpu->wheelVB != NATIVE_GFX_INVALID) && (gpu->wheelIB != NATIVE_GFX_INVALID))
	{
		struct NativeMeshRangeDraw wheelRange;
		const int texture = gpu->materialTexture[gpu->wheelMaterial];
		int wheel;

		memset(&wheelRange, 0, sizeof(wheelRange));
		wheelRange.firstIndex = 0;
		wheelRange.indexCount = gpu->wheelIndexCount;
		wheelRange.texture = (texture >= 0) ? gpu->texture[texture] : NATIVE_GFX_INVALID;
		wheelRange.srgb = (texture >= 0) ? (int)gpu->textureSrgb[texture] : 0;
		memcpy(wheelRange.tint, gpu->materialTint[gpu->wheelMaterial], sizeof(wheelRange.tint));
		wheelRange.alphaCutoff = gpu->materialMask[gpu->wheelMaterial] ? 0.5f : 0.0f;

		for (wheel = 0; wheel < NATIVE_WHEELS_COUNT; wheel++)
		{
			struct NativeMeshDraw w;
			double W[4][4];

			NativeRenderLayer_ScreenFromView(it, (const double(*)[3])it->wheelA[wheel], it->wheelB[wheel], (double)ofsX, (double)ofsY, W);
			memset(&w, 0, sizeof(w));
			NativeRenderLayer_MatrixToDraw(W, w.screenFromModel);
			w.cull = (int)it->cull;
			w.clearDepth = 0;
			w.vertexBuffer = gpu->wheelVB;
			w.indexBuffer = gpu->wheelIB;
			w.vertexOffset = 0;
			w.rangeCount = 1;
			w.ranges = &wheelRange;
			w.depthTint = mesh.depthTint;

			if (NativeRenderer_DrawNativeMesh(&w, clip, dispenv, onScreen) != 0)
			{
				s_nrCharCnt.wheelDraws++;
			}
			else
			{
				s_nrCharCnt.wheelNotDrawn++;
			}
		}
	}

	if (s_nrCharViewDrawFrame[view] != s_nrFrame)
	{
		s_nrCharViewDrawFrame[view] = s_nrFrame;
		s_nrCharViewDraws[view] = 0;
	}
	s_nrCharViewDraws[view]++;
	if (s_nrCharViewDraws[view] > s_nrCharCnt.mostPerView)
	{
		s_nrCharCnt.mostPerView = s_nrCharViewDraws[view];
	}

	firstInFrame = (s_nrCharDrawFrame != s_nrFrame);
	if (firstInFrame)
	{
		s_nrCharDrawFrame = s_nrFrame;
		s_nrCharCnt.drawFrames++;
	}

	if (view == 0)
	{
		s_nrLastOfs[s_nrMarkerDb][0] = (double)ofsX;
		s_nrLastOfs[s_nrMarkerDb][1] = (double)ofsY;
		s_nrLastOfsKnown[s_nrMarkerDb] = 1;
	}

	// The poses and the seam, view 0, once per seat and frame.
	if (view == 0)
	{
		struct NrCharSeam *seam = &s_nrCharSeam[seat];
		const double mid[3] = {((double)gpu->hullMin[0] + (double)gpu->hullMax[0]) * 0.5, ((double)gpu->hullMin[1] + (double)gpu->hullMax[1]) * 0.5,
		                       ((double)gpu->hullMin[2] + (double)gpu->hullMax[2]) * 0.5};
		const double w = (S[3][0] * mid[0]) + (S[3][1] * mid[1]) + (S[3][2] * mid[2]) + S[3][3];
		const int shift = (int)it->mvpShift;
		const int neighbours = seam->haveLast && (seam->lastFrame == (s_nrFrame - 1u)) && (seam->lastW > 0.0);
		int box[4];

		if (seam->haveLast && (seam->lastFrame == s_nrFrame))
		{
			// A second item of the seat in the same frame and view: no new pair.
		}
		else
		{
			const int frameSame = (it->poseDrawn >= 0) && (it->poseDrawn == it->poseCurrent);
			const int animSame = (it->poseDrawnAnim == it->poseCurrentAnim);

			s_nrCharCnt.poseDrawn++;
			if (!it->posePull)
			{
				s_nrCharCnt.poseWithoutPull++;
			}
			if (gpu->netPoseCount == 0u)
			{
				s_nrCharCnt.poseStill++;
			}
			if (it->posePull && frameSame && animSame)
			{
				s_nrCharCnt.poseMatched++;
			}
			else
			{
				s_nrCharCnt.poseMismatched++;
			}

			seam->view0Draws++;
			if (seam->haveLast && (shift != seam->lastShift))
			{
				if (neighbours)
				{
					const double jump = (fabs(w - seam->lastW) / seam->lastW) * 100.0;

					seam->changes++;
					if (seam->lastNear && !it->nearView)
					{
						seam->nearToFar++;
					}
					else if (!seam->lastNear && it->nearView)
					{
						seam->farToNear++;
					}
					if (jump > seam->jumpMax)
					{
						seam->jumpMax = jump;
						seam->jumpVBlank = Platform_GetVBlankCount();
						seam->jumpW0 = seam->lastW;
						seam->jumpW1 = w;
					}
				}
				else
				{
					seam->afterGap++;
				}
			}
			else if (neighbours && (shift == seam->lastShift) && (seam->lastW >= NR_DEPTH_STEP_LOW) && (seam->lastW <= NR_DEPTH_STEP_HIGH) &&
			         (w >= NR_DEPTH_STEP_LOW) && (w <= NR_DEPTH_STEP_HIGH))
			{
				const double step = (fabs(w - seam->lastW) / seam->lastW) * 100.0;

				seam->stepPairs++;
				if (step > seam->stepMax)
				{
					seam->stepMax = step;
				}
			}

			seam->lastFrame = s_nrFrame;
			seam->haveLast = 1;
			seam->lastShift = shift;
			seam->lastNear = it->nearView;
			seam->lastW = w;

			NativeRenderLayer_SeamLine("char", seat, it, w, NativeRenderLayer_CharBodyBox(it, S, box), box, &seam->seamLineVBlank);
			NativeRenderLayer_CharBoxLine(it, S, (double)ofsX, (double)ofsY);
		}
	}
}

// The stamp of this instance view: 1 when it was routed natively in this frame
// (*wheels: whether that item drew native wheels), else 0. 0 at once while no
// custom character is bound natively.
internal int NativeRenderLayer_CharViewStamp(const struct Instance *inst, const struct PushBuffer *pb, int *wheels)
{
	const struct GameTracker *gGT;
	int slot;
	int view;

	*wheels = 0;
	if (s_nrCharCount == 0)
	{
		return 0;
	}

	gGT = sdata->gGT;
	if ((gGT == NULL) || (inst == NULL) || (pb == NULL))
	{
		return 0;
	}
	slot = NativeRenderLayer_PoolSlot(&gGT->JitPools.instance, inst);
	if ((slot < 0) || (slot >= NATIVE_RENDER_LAYER_SLOTS))
	{
		return 0;
	}
	for (view = 0; view < NATIVE_RENDER_LAYER_VIEWS; view++)
	{
		if (pb == &gGT->pushBuffer[view])
		{
			if (s_nrCharViewFrame[slot][view] != s_nrFrame)
			{
				return 0;
			}
			*wheels = s_nrCharViewWheels[slot][view];
			return 1;
		}
	}
	return 0;
}

// Step 4c, from game/DrawTires.c through NativeChar_ViewHidesWheels: a view
// drawn natively WITH native wheels hides the retail ones. A native view
// without native wheels (no wheel pose) keeps them, and TiresBegin counts
// them as retail wheels in a native view.
int NativeRenderLayer_CharViewNativeWheels(const struct Instance *inst, const struct PushBuffer *pb)
{
	int wheels = 0;

	return NativeRenderLayer_CharViewStamp(inst, pb, &wheels) && wheels;
}

// THE NATIVE DRAW of one item, from NativeGpu_DrawNativeSplit in the order of
// the OT. An item is only good in the frame its marker was written in; any
// other index is counted and left. The depth of a view is cleared before its
// first native draw in the frame (only inside the split's clip): what an
// earlier view or frame left there is not this view's.
void NativeRenderLayer_DrawNativeItem(u32 item, const RECT16 *clip, const DISPENV *dispenv, int onScreen, float ofsX, float ofsY)
{
	const int db = s_nrMarkerDb;
	const struct NrDrawItem *it;
	struct NativeLayerDraw draw;
	double S[4][4];
	int view;
	int firstInFrame;
	int r;
	int c;

	if (item >= (u32)s_nrItemCount[db])
	{
		s_nrCount.probeStale++;
		return;
	}

	it = &s_nrItems[db][item];
	view = (int)it->view;

	if (it->kind == NR_ITEM_CHAR)
	{
		NativeRenderLayer_DrawCharItem(it, clip, dispenv, onScreen, ofsX, ofsY);
		return;
	}

	NativeRenderLayer_ItemMatrix(it, (double)ofsX, (double)ofsY, S);

	memset(&draw, 0, sizeof(draw));
	for (c = 0; c < 4; c++)
	{
		for (r = 0; r < 4; r++)
		{
			draw.screenFromModel[(c * 4) + r] = (float)S[r][c];
		}
	}
	draw.cull = (int)it->cull;
	draw.clearDepth = (s_nrDepthClearFrame[view] != s_nrFrame) ? 1 : 0;
	draw.depthTint = NativeRenderLayer_DepthTint(it);

	// The form pose: the textured mesh shaped by the drawn frame (CPU morph into
	// a static host array, read by the renderer at once), into the region of
	// this frame in work and this item.
	if (g_cfg_nativeProbe == NATIVE_PROBE_POSE)
	{
		NativeProbe_PoseVertices((it->poseDrawn >= 0) ? (int)it->poseDrawn : 0, s_nrPoseVertices);
		draw.vertices = s_nrPoseVertices;
		draw.vertexRegion = ((int)(s_nrFrame % NATIVE_LAYER_POSE_FRAMES) * NATIVE_LAYER_POSE_SLOTS) + ((int)item * (g_cfg_nativeProbeWheels ? NR_WHEEL_REGIONS : 1));
	}

	if (!NativeRenderer_DrawNativeProbe(&draw, clip, dispenv, onScreen))
	{
		s_nrCount.probeNotDrawn++;
		return;
	}

	s_nrCount.probeDraws++;
	if (draw.vertices != NULL)
	{
		s_nrCount.poseVertexWrites++;
	}
	s_nrProbeFrame.drawn = 1;

	// The form wheels: the four wheels right after the body, in the same split,
	// the same depth (not cleared again) and the same cull.
	if (g_cfg_nativeProbeWheels && it->nativeWheels && (draw.vertices != NULL))
	{
		NativeRenderLayer_DrawWheels(it, draw.vertexRegion, clip, dispenv, onScreen, (double)ofsX, (double)ofsY);
	}

	if (draw.clearDepth)
	{
		s_nrDepthClearFrame[view] = s_nrFrame;
		s_nrCount.probeDepthClears++;
	}

	if (s_nrViewDrawFrame[view] != s_nrFrame)
	{
		s_nrViewDrawFrame[view] = s_nrFrame;
		s_nrViewDraws[view] = 0;
	}
	s_nrViewDraws[view]++;
	if (s_nrViewDraws[view] > s_nrCount.probeMostPerView)
	{
		s_nrCount.probeMostPerView = s_nrViewDraws[view];
	}

	firstInFrame = (s_nrDrawFrame != s_nrFrame);
	if (firstInFrame)
	{
		s_nrDrawFrame = s_nrFrame;
		s_nrCount.probeDrawFrames++;
	}

	// The draw offset of this buffer, for the fallback boxes of later frames.
	if (view == 0)
	{
		s_nrLastOfs[db][0] = (double)ofsX;
		s_nrLastOfs[db][1] = (double)ofsY;
		s_nrLastOfsKnown[db] = 1;
	}

	// THE SEAM. The true depth of the middle of the body (u = (0, 0.5, 0)) in
	// view 0, once per frame: the w of that point through S, which is
	// (mvp row 2 . g + mvp.t[2]) / 2^shift - the view z in world units, the
	// near or huge scaling of the queue taken out again. Where the queue changed
	// the shift since the native frame right before, the relative step
	// |z - z before| / z before in percent is the depth jump; a shift taken
	// wrongly would make it a factor of 4 (75 or 300 percent), while the motion
	// of one frame moves it by a few percent at most. Without a change the same
	// step near the seam is the yardstick. A frame without a native draw in
	// between breaks the pair (CloseProbeFrame), and a change across it is only
	// counted.
	if ((view == 0) && (s_nrViewDraws[0] == 1))
	{
		const double z = (S[3][1] * 0.5) + S[3][3];
		const int shift = (int)it->mvpShift;
		const int neighbours = s_nrDepthPrevValid && (s_nrDepthPrev > 0.0);

		if (s_nrDepthHaveLast && (shift != s_nrDepthLastShift))
		{
			if (neighbours)
			{
				const double jump = (fabs(z - s_nrDepthPrev) / s_nrDepthPrev) * 100.0;

				s_nrCount.probeShiftChanges++;
				if (s_nrDepthPrevNear && !it->nearView)
				{
					s_nrCount.probeShiftNearToFar++;
				}
				else if (!s_nrDepthPrevNear && it->nearView)
				{
					s_nrCount.probeShiftFarToNear++;
				}

				if (jump > s_nrCount.probeDepthJumpMax)
				{
					s_nrCount.probeDepthJumpMax = jump;
					s_nrCount.probeDepthJumpMaxZ = s_nrDepthPrev;
					s_nrCount.probeDepthJumpMaxZAfter = z;
					s_nrCount.probeDepthJumpMaxVBlank = Platform_GetVBlankCount();
					s_nrCount.probeDepthJumpMaxShiftBefore = s_nrDepthPrevShift;
					s_nrCount.probeDepthJumpMaxShiftAfter = shift;
				}
			}
			else
			{
				s_nrCount.probeShiftChangesAfterGap++;
			}
		}
		else if (neighbours && (shift == s_nrDepthPrevShift) && (s_nrDepthPrev >= NR_DEPTH_STEP_LOW) && (s_nrDepthPrev <= NR_DEPTH_STEP_HIGH) &&
		         (z >= NR_DEPTH_STEP_LOW) && (z <= NR_DEPTH_STEP_HIGH))
		{
			const double step = (fabs(z - s_nrDepthPrev) / s_nrDepthPrev) * 100.0;

			s_nrCount.probeDepthStepPairs++;
			if (step > s_nrCount.probeDepthStepMax)
			{
				s_nrCount.probeDepthStepMax = step;
			}
		}

		s_nrDepthPrev = z;
		s_nrDepthPrevShift = shift;
		s_nrDepthPrevNear = it->nearView;
		s_nrDepthPrevValid = 1;
		s_nrDepthLastShift = shift;
		s_nrDepthHaveLast = 1;

		// --native-seam-report: the line of this native frame of the probe.
		if (g_cfg_nativeSeamReport)
		{
			const double bodyLo[3] = {-1.0, 0.0, -1.0};
			const double bodyHi[3] = {1.0, 1.0, 1.0};
			int box[4];
			const int known = NativeRenderLayer_ProjectBox(S, bodyLo, bodyHi, box);

			NativeRenderLayer_SeamLine("probe", g_cfg_nativeProbeSeat, it, z, known, box, &s_nrProbeSeamVBlank);
		}

		// The form pose, once per frame of view 0: was the frame drawn the
		// frame of idpp->ptrCurrFrame, and how far FLOAT lies from KONGRUENZ.
		if (g_cfg_nativeProbe == NATIVE_PROBE_POSE)
		{
			const int frameSame = (it->poseDrawn >= 0) && (it->poseDrawn == it->poseCurrent);
			const int animSame = (it->poseDrawnAnim == it->poseCurrentAnim);

			s_nrCount.poseTicks++;
			if (!it->posePull)
			{
				s_nrCount.poseWithoutPull++;
			}
			if (it->poseWordDiffers)
			{
				s_nrCount.poseWordDiffers++;
			}
			if (!frameSame)
			{
				s_nrCount.poseFrameMismatched++;
			}
			if (!animSame)
			{
				s_nrCount.poseAnimMismatched++;
			}
			if (it->posePull && frameSame && animSame)
			{
				s_nrCount.poseMatched++;
			}
			else
			{
				s_nrCount.poseMismatched++;
			}

			if (it->floatKnown)
			{
				s_nrCount.floatTicks++;
				s_nrCount.floatDistanceSum += it->floatDistance;
				s_nrCount.floatDistanceTSum += it->floatDistanceT;
				if (it->floatDistance > s_nrCount.floatDistanceMax)
				{
					s_nrCount.floatDistanceMax = it->floatDistance;
				}
				if (it->floatDistanceT > s_nrCount.floatDistanceTMax)
				{
					s_nrCount.floatDistanceTMax = it->floatDistanceT;
				}
			}

			if (g_cfg_nativeLayerReport)
			{
				Platform_Log("[CTR RenderLayer] probe pose at vblank %d: drawn frame %d, current frame %d, anim %d/%d\n",
				             Platform_GetVBlankCount(), (int)it->poseDrawn, (int)it->poseCurrent, (int)it->poseDrawnAnim, (int)it->poseCurrentAnim);
			}
		}
	}

	if (firstInFrame)
	{
		NativeRenderLayer_ProbeLine(it, S, (double)ofsX, (double)ofsY);
	}
}
