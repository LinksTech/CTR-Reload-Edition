#ifndef NATIVE_CHARS_H
#define NATIVE_CHARS_H

// Custom characters, developer proof of concept (platform/native_chars.c).
// One .rldchar from --chars-dir/--char stands in for the driver model of
// seat 0 in a one-player arcade race on its template; everything else stays
// retail. Without --char nothing here opens a file or changes a picture.

struct Model;
struct Instance;

// main.c, first pass over the command line: --chars-dir and --char are only
// remembered, nothing is opened or scanned.
void NativeChar_SetFolder(const char *folder);
void NativeChar_SetFile(const char *file);

// main.c, before any window: 0 when --char names a file that is neither in a
// --chars-dir nor an absolute path (the message is then on stderr), else 1.
int NativeChar_ArgsUsable(void);

// Called once at startup after the existing load preparation, before CTR_Main.
// With --dev it registers the exit line of the instance counter (also without
// --char). Without --char it does nothing else. With --char: Rld_OpenAs with
// s_rldCharFormat, CHRI (RldChar_ParseInfo) and CMDL read whole (hash per chunk),
// RldChar_CheckModel on the UNRELOCATED bytes, then LOAD_RunPtrMap once on the
// host copy (0 -> refused "PTRMAP"). Logs one line "loaded" or "REFUSED"; a
// refused file leaves the game in retail state.
void NativeChar_LoadDev(void);

// The funnel, right after LOAD_GlobalModelPtrs_MPK in load stage 5.
void NativeChar_ArmSeats(void);

// Load stage 0, right after MEMPACK_PopToState: the seats are emptied with the
// level they were armed for.
void NativeChar_ClearSeats(void);

// game/Vehicle/VehBirth.c: the model of a bound seat while data.characterIDs
// still holds the id it was armed for, else NULL (then the retail lookup).
struct Model *NativeChar_SeatModel(int index);

// game/RenderBucket/RenderBucket_QueueExecute.c: an instance whose drawing
// stopped because the draw memory was full. Only counts, never changes a picture.
void NativeChar_NoteDroppedInstance(const struct Instance *inst);

// At every --shot and --rank-report VBlank: the steer line of seat 0 (only with a
// loaded --char file, only in a race).
void NativeChar_NoteVBlank(int vblank);

#endif
