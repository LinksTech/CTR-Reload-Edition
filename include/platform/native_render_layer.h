#ifndef NATIVE_RENDER_LAYER_H
#define NATIVE_RENDER_LAYER_H

// The native render layer (platform/native_render_layer.c): a host-side scene
// table of the instances, fed once per frame before the instance queue, and a
// route in the draw dispatch that answers "retail" unless the native probe
// (--native-preview --native-probe) is bound to the model of the instance.
// Nothing here writes game state, the GTE, the scratchpad or any game memory
// other than a marker in the own OT range of a native instance view - see the
// file for the rules.
//
// The game side (game/MAIN/MainFrame_RenderFrame.c,
// game/RenderBucket/RenderBucket_QueueExecute.c, game/INSTANCE.c) comes first
// in the translation unit and repeats these prototypes locally, word for
// word. Change a signature here and there together.

#include <macros.h>
#include <psx/libgpu.h>

struct GameTracker;
struct Instance;
struct InstDrawPerPlayer;
struct PushBuffer;
struct Model;

// --native-layer-report (only with --dev): the counters at exit. The counters
// run in every run; the switch only decides whether they are printed.
// Never in ctr-settings.cfg: the file only knows s_videoSettings and the views.
extern int g_cfg_nativeLayerReport;

// Once per frame in MainFrame_RenderFrame, right before
// RenderBucket_QueueAllInstances. Reads the living pool instances into the
// scene table (prev/curr per logic tick of gGT->timer).
void NativeRenderLayer_Pull(struct GameTracker *gGT);

// INSTANCE_Birth and INSTANCE_Death: one more generation for the pool slot of
// the instance, so a reused slot never inherits the previous state of the
// instance that lived there before. Host table only.
void NativeRenderLayer_NoteBirth(const struct Instance *inst);
void NativeRenderLayer_NoteDeath(const struct Instance *inst);

// NativeCheckpoint_Restore (platform/native_checkpoint.c), before it writes the
// first byte: the instance pool is about to be replaced as a whole, without a
// single INSTANCE_Birth or INSTANCE_Death. Every slot gets a new generation and
// the table is emptied, so the next pull starts every object and camera with a
// cut. Covers every way that restores a checkpoint (quick state, replay
// bootstrap), because all of them go through that one function.
void NativeRenderLayer_NoteRestore(void);

// RenderBucket_DispatchDrawFunc, after the setup callback and before the
// handler switch. Not 0 means "drawn natively, skip the retail handler"; that
// answer only comes for the bound probe model, everything else gets 0.
int NativeRenderLayer_Route(const struct Instance *inst, const struct InstDrawPerPlayer *idpp, const struct PushBuffer *pb);

// Right after a 0 from NativeRenderLayer_Route, with the handler selector
// the switch is about to take (idpp->unkEC). Only counts.
void NativeRenderLayer_NoteSwitchEntry(const struct Instance *inst, const struct InstDrawPerPlayer *idpp, const struct PushBuffer *pb, u32 handler);

// The exit report; registered by main.c only under --native-layer-report.
void NativeRenderLayer_Report(void);

// --native-empty-markers (only with --dev, only with --native-preview).
// Never in ctr-settings.cfg: the file only knows s_videoSettings and the views.
extern int g_cfg_nativeEmptyMarkers;

// game/MAIN/MainFrame.c, MainFrame_RegisterGpuLinkRanges, after the swapchain
// ranges: registers the two marker arenas (only with --native-preview) and
// empties the one of the back buffer.
void NativeRenderLayer_RegisterMarkerArenas(struct GameTracker *gGT);

// Exit lines of the marker channel; registered by main.c with --native-preview.
void NativeRenderLayer_MarkerReport(void);

// --native-probe in force for this run (with --native-preview): quick states
// and a recording that starts mid-run are refused, like for a custom character.
int NativeRenderLayer_ProbeActive(void);

// game/DrawTires.c through NativeChar_ModelHidesWheels: 1 for the model the
// probe is bound to - its retail wheels are off, like RLDCHAR_FLAG_NO_WHEELS -
// unless --native-wheel-report keeps them on.
int NativeRenderLayer_ModelHidesWheels(const struct Model *model);

// game/DrawTires.c, around the wheel set of one instance view (solid pass:
// reflection 0, reflection pass: 1): the projected corners of each wheel as
// DrawTires read them from the GTE, and each wheel quad it wrote. Only read and
// counted, and only for the instance the probe is bound to; with
// --native-wheel-report the solid pass of view 0 gives the retail line.
void NativeRenderLayer_TiresBegin(const struct Instance *inst, const struct PushBuffer *pb, int reflection);
void NativeRenderLayer_TiresCorners(int wheelIndex, const s32 sxy[4]);
void NativeRenderLayer_TiresPrimitive(void);
void NativeRenderLayer_TiresEnd(void);

// game/Particle.c, Particle_RenderList, after a particle quad is written and
// linked: its corners are read for the exhaust box of the probe seat
// (--native-layer-report). Nothing is written.
struct Particle;
void NativeRenderLayer_NoteParticleQuad(const struct Particle *particle, const struct PushBuffer *pb, const POLY_FT4 *poly);

// native_gpu.c, NativeGpu_DrawNativeSplit, for a native split with drawing.
// ofsX/ofsY: the draw offset of the split, computed as DrawEnvOffset does.
void NativeRenderLayer_DrawNativeItem(u32 item, const RECT16 *clip, const DISPENV *dispenv, int onScreen, float ofsX, float ofsY);

#endif
