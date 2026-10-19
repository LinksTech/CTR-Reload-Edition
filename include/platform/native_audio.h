#ifndef PLATFORM_NATIVE_AUDIO_H
#define PLATFORM_NATIVE_AUDIO_H

#include <macros.h>
#include <psx/libspu.h>

// The emulated sound RAM: 1 MB instead of the PS1's 512 KB.
// Power of two, because the ADPCM reader masks addresses with MEMSIZE-1.
// Defined here and not in native_audio.c so that the game logic uses the
// same limit (HOWL_Bank.c, HOWL_SPU_BANK_LIMIT): if that were higher,
// NativeAudio_SpuSetTransferStartAddr would refuse, keep the old offset, and
// SpuWrite would silently write over an earlier bank.
#define NATIVE_AUDIO_SPU_MEMSIZE (1024 * 1024)

s32 NativeAudio_SpuInit(void);
u32 NativeAudio_SpuSetTransferStartAddr(u32 addr);
u32 NativeAudio_SpuWrite(const u8 *addr, u32 size);
void NativeAudio_SpuSetVoiceAttr(SpuVoiceAttr *psxAttrib);
void NativeAudio_SpuSetKey(s32 on_off, u32 voice_bit);
s32 NativeAudio_SpuSetReverb(s32 on_off);
s32 NativeAudio_SpuSetReverbModeParam(SpuReverbAttr *attr);
void NativeAudio_SpuSetReverbModeDepth(s16 left, s16 right);
u32 NativeAudio_SpuSetReverbVoice(s32 on_off, u32 voice_bit);
void NativeAudio_SpuSetCommonMasterVolume(s16 left, s16 right);
void NativeAudio_SpuSetCommonCDMix(s32 enabled);
void NativeAudio_SpuSetCommonCDVolume(s16 left, s16 right);
void NativeAudio_SpuSetCommonCDReverb(s32 enabled);

int NativeAudio_PlayXATrack(int categoryID, int xaID, int volumeLeft, int volumeRight);
int NativeAudio_PlayXAFile(const char *relativePath, int channelFilter, int volumeLeft, int volumeRight);
int NativeAudio_GetXATrackLength(int categoryID, int xaID);
// Host clips, mono s16 little endian at sampleRate (at most the output rate),
// up to the output rate by linear interpolation, times gain / 256. The samples
// stay the caller's and must outlive the clip. Never in a snapshot (a restore
// leaves both places silent). 0 when nothing plays.
// The line: a voice line on the CD channel, one place - the XA stops, the clip
// plays at volume as XA volume (faded by NativeAudio_SetXAVolume), counts in
// NativeAudio_IsXAPlaying and NativeAudio_GetXACurrOffset and stops with the
// XA (NativeAudio_StopXA, a new XA). Opens the device like PlayXATrack.
int NativeAudio_PlayPcmLine(const u8 *samples, int frameCount, int sampleRate, int gain, int volume);
// The short sound: one place of its own beside the XA and the line, at
// volumeLeft / volumeRight like an SPU voice volume; a new short sound
// replaces the one playing, nothing of the XA touches it. Only while the
// mixer runs (the device is never opened here).
int NativeAudio_PlayPcmShort(const u8 *samples, int frameCount, int sampleRate, int gain, int volumeLeft, int volumeRight);
// Both places silent at once, before the caller lets the samples go.
void NativeAudio_StopPcmClips(void);
// The music place (--ext-track, platform/native_extmusic.c): a host track, s16
// stereo interleaved at the output rate (44 100 Hz, nothing else is accepted),
// looping over the whole buffer. Not the CD channel: it does not count as XA
// playing and nothing of the XA touches it. Volume as an SPU voice volume
// (0..0x7fff) times the master volume, no reverb, never in a snapshot. The
// samples stay the caller's. Start: silent at frame 0, only on a mixer that runs
// (0 = refused). SetState once per logic frame; a state holds half a second of
// output, then the place falls silent by itself. Rewind: back to frame 0.
int NativeAudio_MusicStart(const s16 *frames, int frameCount, int sampleRate);
void NativeAudio_MusicRewind(void);
void NativeAudio_MusicSetState(int playing, int volume);
void NativeAudio_MusicStop(void);
void NativeAudio_MusicStats(u64 *framesMixed, u32 *loops, u64 *framesSilent);
void NativeAudio_MusicSelftest(int *checks, int *failures);
int NativeAudio_IsXAPlaying(void);
int NativeAudio_GetXACurrOffset(void);
int NativeAudio_GetXAMaxSample(void);
int NativeAudio_GetXAMaxSampleAtOffset(int xaCurrOffset);
void NativeAudio_SetXAVolume(int volumeLeft, int volumeRight);
void NativeAudio_StopXA(void);
void NativeAudio_StepVBlank(void);
void NativeAudio_SetDeterministicRenderMode(int enabled);
int NativeAudio_IsDeterministicRenderMode(void);
void NativeAudio_Shutdown(void);
#ifdef CTR_INTERNAL
void NativeAudio_GetOutputStats(int *underrunFrames, int *overflowFrames, int *queuedFrames);
#endif
int NativeAudio_GetStateSize(void);
int NativeAudio_CaptureState(void *dst, int dstSize);
int NativeAudio_RestoreState(const void *src, int srcSize);

#endif
