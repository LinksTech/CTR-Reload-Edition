#ifndef NATIVE_PREVIEW_H
#define NATIVE_PREVIEW_H

// Track preview for container tracks (platform/native_preview.c).
// File tracks\vorschau\<container without .rldtrack>.rldprev ("vorschau" is
// the folder name on disk), see there.

#define NATIVE_PREVIEW_WIDTH 170
#define NATIVE_PREVIEW_HEIGHT 71
#define NATIVE_PREVIEW_FRAMES 150
#define NATIVE_PREVIEW_FPS 15

// --record-preview (only with --dev), read in game/native_flyin.c.
extern int g_cfg_recordPreview;

// Recording: the next finished frame becomes preview frame frameIndex.
void NativePreview_RecordFrameDue(int frameIndex);
void NativePreview_RecordFail(const char *why);

// Playback in the track screen. Open returns 1 if a valid preview exists for the
// container (header and LEVD hash match).
int NativePreview_Open(int trackIndex);
int NativePreview_Upload(int frameIndex, int dstX, int dstY);
void NativePreview_Close(void);

#endif
