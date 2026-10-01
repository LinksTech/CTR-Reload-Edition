#ifndef NATIVE_CHARS_H
#define NATIVE_CHARS_H

// Custom characters (platform/native_chars.c): the roster the driver select
// shows after the 15 retail tiles, the pick it leaves behind, and the funnel
// that binds seat 0 to the model of the picked file in a one-player arcade
// race on its template. The engine only ever sees the templates 0..14 in
// data.characterIDs; the custom model is an overlay on seat 0. Without --char
// and --dev-grid-fill the roster is empty and nothing here opens a file or
// changes a picture.

struct Model;
struct Instance;

// The roster holds at most this many entries; the driver select numbers them
// from NATIVE_CHAR_RUNTIME_ID_FIRST on (entry e has runtime id FIRST + e).
#define NATIVE_CHAR_ROSTER_MAX 32
#define NATIVE_CHAR_RUNTIME_ID_FIRST 32

// main.c, first pass over the command line: --chars-dir and --char are only
// remembered, nothing is opened or scanned.
void NativeChar_SetFolder(const char *folder);
void NativeChar_SetFile(const char *file);

// main.c, first pass: --dev-grid-fill <n> (main.c refuses n outside
// 1..NATIVE_CHAR_ROSTER_MAX), only remembered.
void NativeChar_SetGridFill(int count);

// main.c, before any window: 0 when --char names a file that is neither in a
// --chars-dir nor an absolute path (the message is then on stderr), else 1.
int NativeChar_ArgsUsable(void);

// Called once at startup after the existing load preparation, before CTR_Main.
// With --dev it registers the exit line of the instance counter (also without
// --char). With --char: Rld_OpenAs with s_rldCharFormat, CHRI
// (RldChar_ParseInfo) and CMDL read whole (hash per chunk), RldChar_CheckModel
// on the UNRELOCATED bytes, then LOAD_RunPtrMap once on the host copy (0 ->
// refused "PTRMAP"). Logs one line "loaded" or "REFUSED"; a refused file leaves
// the game in retail state. Then the roster is built and stays fixed for the run.
void NativeChar_LoadDev(void);

// The roster: entry 0 is the --char file when it loaded, then the placeholders
// of --dev-grid-fill. At most NATIVE_CHAR_ROSTER_MAX in all (the rest is cut,
// one log line).
int NativeChar_RosterCount(void);
int NativeChar_EntryTemplate(int entry);        // 0..14; -1 outside the roster
const char *NativeChar_EntryName(int entry);    // CHRI name; placeholders "PLACEHOLDER <n>" (n = 1..); "" outside
int NativeChar_EntryIsPlaceholder(int entry);   // 1 for a placeholder, 0 for a file, 0 outside
struct Model *NativeChar_EntryModel(int entry); // the relocated host model of a file; NULL for placeholders and outside
int NativeChar_EntryMenuFrame(int entry);       // menu pose: (frames of animation 0) >> 1 of the model (21 -> 10), 0 without model

// The pick of the driver select: an entry index, or -1 (retail). Written in
// every frame of the driver select; an index outside the roster is kept as -1.
void NativeChar_SetPick(int entry);
int NativeChar_Pick(void);

// The direct start (DebugMenu_JumpToLevel: --level, --autoload-track, the
// LEVEL and TRACK pages of the debug menu): pick = entry 0 when it is a loaded
// file, else -1. So --level 0 --driver 14 --char <file> binds as it always did.
void NativeChar_PickForJump(void);

// Menu side of the mode rule: ARCADE_MODE set, none of TIME_TRIAL,
// ADVENTURE_MODE and BATTLE_MODE, numPlyrNextGame == 1, boolDemoMode == 0, and
// MM_NativeTrackSelect_Chosen() neither CRYSTAL nor CTR.
int NativeChar_ModeAllowed(void);

// The funnel, right after LOAD_GlobalModelPtrs_MPK in load stage 5. Binds
// seat 0 only when the pick is a file and data.characterIDs[0] holds its
// template; it never writes characterIDs.
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

// At every --shot and --rank-report VBlank: the steer line of seat 0 (only
// while seat 0 is bound, only in a race).
void NativeChar_NoteVBlank(int vblank);

#endif
