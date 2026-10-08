#ifndef NATIVE_CHARS_H
#define NATIVE_CHARS_H

// Custom characters (platform/native_chars.c): the .rldchar files of the
// characters folder, the roster the driver select shows after the 15 retail
// tiles, the pick it leaves behind, and the funnel that binds seat 0 to the
// model of the picked file in a one-player arcade race on its template. The
// engine only ever sees the templates 0..14 in data.characterIDs; the custom
// model is an overlay on seat 0. Without a .rldchar in the folder and without
// --char and --dev-grid-fill the roster is empty and nothing here changes a
// picture; the start logs one summary line.

struct Model;
struct Instance;
struct Driver;
struct Icon;
struct TextureLayout;

// The roster holds at most this many entries; the driver select numbers them
// from NATIVE_CHAR_RUNTIME_ID_FIRST on (entry e has runtime id FIRST + e).
#define NATIVE_CHAR_ROSTER_MAX 32
#define NATIVE_CHAR_RUNTIME_ID_FIRST 32

// The folder under the game folder that is read at start (main.c creates it).
#define NATIVE_CHAR_DIR_NAME "characters"

// main.c, first pass over the command line, developer switches for tests:
// --chars-dir reads this folder instead of characters/ (also under
// --settings-defaults), --char reads only this one file and no folder. Only
// remembered here, nothing is opened or scanned.
void NativeChar_SetFolder(const char *folder);
void NativeChar_SetFile(const char *file);

// main.c, first pass: --dev-grid-fill <n> (main.c refuses n outside
// 1..NATIVE_CHAR_ROSTER_MAX), only remembered.
void NativeChar_SetGridFill(int count);

// --dev-char-seats (main.c, only with --dev): every seat of a one-player
// arcade race on a custom model, the bot seats put on the file's template
// (seat 0 keeps --driver's), each with the template's class - ALL the first
// file of the roster, CYCLE the next file at every race load it binds
// (several models measured in one run). Only remembered.
#define NATIVE_CHAR_DEV_SEATS_OFF 0
#define NATIVE_CHAR_DEV_SEATS_ALL 1
#define NATIVE_CHAR_DEV_SEATS_CYCLE 2
void NativeChar_SetDevSeats(int mode);

// --dev-char-seat-files <f0,f1,...> (main.c, only with --dev, never with
// --dev-char-seats or --char): like ALL, but seat s < n drives the file fs of
// the roster and every seat from n on the first file of the roster (as ALL);
// each bot seat is put on the template of its own file. 1 to
// NATIVE_CHAR_DEV_SEAT_FILES_MAX names separated by commas, each a file name
// of the folder (no path). Only remembered: 0 when the list is not of that
// form (nothing remembered), else 1.
#define NATIVE_CHAR_DEV_SEATS_FILES 3
#define NATIVE_CHAR_DEV_SEAT_FILES_MAX 8
int NativeChar_SetDevSeatFiles(const char *list);

// main.c, right after NativeChar_LoadRoster: each name of
// --dev-char-seat-files looked up among the files of the roster (letter case
// ignored, as the folder scan takes the extension). 1 when every name is a file
// of the roster (or the switch is off), with one line per seat in the log; 0
// for the first name that is not, with its message on stderr and in the log -
// main.c then ends the start with exit code 64.
int NativeChar_DevSeatFilesResolve(void);

// main.c, before any window: 0 when --char names a file that is neither in a
// --chars-dir nor an absolute path (the message is then on stderr), else 1.
int NativeChar_ArgsUsable(void);

// Called once at startup after the existing load preparation, before CTR_Main,
// independent of --no-tracks. With --dev it registers the exit line of the
// instance counter. Then, in this order of precedence:
//   --char           only that file;
//   --settings-defaults without --chars-dir: nothing is read (one line);
//   else             every *.rldchar of the folder (characters/ or
//                    --chars-dir), subfolders skipped, names sorted like the
//                    track list, then read one by one.
// Per file: Rld_OpenAs with s_rldCharFormat, CHRI (RldChar_ParseInfo), the
// CMDL size against RLDCHAR_LIMIT_CMDL before any malloc, CMDL read whole
// (hash per chunk), RldChar_CheckModel on the UNRELOCATED bytes, then
// LOAD_RunPtrMap once on the host copy (0 -> refused "PTRMAP"), then CICN when
// present (RldChar_CheckIcon; a broken CICN costs only the portrait), then CMSK
// when present (RldChar_CheckMask and one LOAD_RunPtrMap; a broken CMSK costs
// only the own mask), then CVOI when present (RldChar_CheckVoices; a broken
// CVOI costs only the voices; the bytes are read again only for a bound seat). One line "loaded" or
// "REFUSED" per file, after "loaded" one line "portrait", for a file with CMSK
// one line "mask", the line "draw bytes" and one line "voices"; a
// refused file is skipped and the game starts. From the 33rd valid file on: a
// loud "NO ID" line, no entry. Then one summary line ("characters: N loaded,
// M refused (<folder>)", or one line for a missing folder), and the roster is
// built; it stays fixed for the run.
void NativeChar_LoadRoster(void);

// The roster: the loaded files in sorted order, then the placeholders of
// --dev-grid-fill. At most NATIVE_CHAR_ROSTER_MAX in all (the rest is cut,
// one log line).
int NativeChar_RosterCount(void);
int NativeChar_EntryTemplate(int entry);        // 0..14; -1 outside the roster
const char *NativeChar_EntryName(int entry);    // CHRI name; placeholders "PLACEHOLDER <n>" (n = 1..); "" outside
int NativeChar_EntryIsPlaceholder(int entry);   // 1 for a placeholder, 0 for a file, 0 outside
struct Model *NativeChar_EntryModel(int entry); // the relocated host model of a file; NULL for placeholders and outside
int NativeChar_EntryMenuFrame(int entry);       // menu pose: (frames of animation 0) >> 1 of the model (21 -> 10), 0 without model

// THE PORTRAITS of the driver select grid (CICN, include/rldchar.inc). A file
// with a usable CICN owns portrait slot e = its entry while e is below
// NATIVE_CHAR_PORTRAIT_SLOTS; every slot has a fixed place in the VRAM strip
// x 256..511, y 266..295, which no retail or container path writes:
//   texels  x 256 + 64p + 11s, y 266, 11 x 26 halfwords (4 bit, 44 x 26
//           texels), page p = slot / 5, column s = slot % 5 - the four 4-bit
//           texture pages x 256/320/384/448 with y base 256, u0 = 44s, v0 = 10;
//   CLUT    x 256 + 16 (slot % 16), y 292 + slot / 16, 16 x 1.
// In rows 266..291 the last 9 halfwords of every page stay free (x 311..319,
// 375..383, 439..447, 503..511), and so do the rows 294..295. Entries without
// a CICN, with a broken one, from entry 20 on, and placeholders show the
// template's portrait as before. The grid shows the own portrait of an entry
// (NativeChar_EntryPortrait), the race HUD, the arcade results and the cup
// standings that of a bound seat (NativeChar_SeatPortrait; game/UI/UI_Rank.c,
// game/222.c, game/UI/UI_CupStandings.c).
#define NATIVE_CHAR_PORTRAIT_SLOTS 20

struct NativeCharPortraitSlot
{
	int texelX, texelY, texelW, texelH; // VRAM halfwords
	int clutX, clutY, clutW;            // VRAM halfwords, one row
	int u0, v0;                         // texel coordinates inside the page
	int pageBits;                       // tpage bits 0..4: page x (x / 64) and the y base 256 bit (0x10)
};

// Pure geometry of a slot, for the upload and the self-test. 0 for a slot
// outside 0..NATIVE_CHAR_PORTRAIT_SLOTS - 1 (out untouched).
int NativeChar_PortraitSlot(int slot, struct NativeCharPortraitSlot *out);

// The texture layout of slot from the template's: clut, page, y base and the
// four UV corners new; abr and everything else of tpage as in the template.
// The size is the template's (u1 - u0, v2 - v0); 0 when it does not fit into
// the 44 x 26 of a slot or the slot is outside, else 1.
int NativeChar_PortraitLayout(int slot, const struct TextureLayout *templateLayout, struct TextureLayout *out);

// The driver select grid, for a custom tile: the own portrait of entry
// (uploaded into the strip at the first call after NativeChar_PortraitsDirty)
// or templateIcon when the entry has none. Silent for a retail run: without a
// file with a CICN nothing is uploaded and nothing logged.
struct Icon *NativeChar_EntryPortrait(int entry, struct Icon *templateIcon);

// The race side: the portrait of a seat for the HUD rank list, the arcade
// results and the cup standings. A bound seat whose guard holds
// (NativeChar_SeatModel != NULL) gets NativeChar_EntryPortrait of its entry;
// every other seat gets templateIcon, the caller's unchanged retail icon. One
// line "hud portrait seat <n>: ..." per load, only for a bound seat.
struct Icon *NativeChar_SeatPortrait(int seat, struct Icon *templateIcon);

// Every entering of the driver select: the next NativeChar_EntryPortrait
// uploads all own portraits again (a race, a quick state or a video may have
// used the VRAM in between).
void NativeChar_PortraitsDirty(void);

// The pick of the driver select: an entry index, or -1 (retail). Written in
// every frame of the driver select; an index outside the roster is kept as -1.
// Dropped (-1) by a main-menu choice other than ARCADE, by a menu load onto
// the title and by the funnel in a mode it must not bind in.
void NativeChar_SetPick(int entry);
int NativeChar_Pick(void);

// The direct start (DebugMenu_JumpToLevel: --level, --autoload-track, the
// LEVEL and TRACK pages of the debug menu): pick = entry 0 when --char loaded
// its file, else -1 (a file of the folder is never picked here). So
// --level 0 --driver 14 --char <file> binds as it always did.
void NativeChar_PickForJump(void);

// Menu side of the mode rule: ARCADE_MODE set, none of TIME_TRIAL,
// ADVENTURE_MODE and BATTLE_MODE, numPlyrNextGame == 1, boolDemoMode == 0, and
// MM_NativeTrackSelect_Chosen() neither CRYSTAL nor CTR.
int NativeChar_ModeAllowed(void);

// The funnel, right after LOAD_GlobalModelPtrs_MPK in load stage 5. A menu
// load onto the title (levelID MAIN_MENU_LEVEL, mainMenuState
// MAIN_MENU_TITLE) drops the pick, silently. A race load binds seat 0 only
// when the pick is a file (silent without a pick), the mode allows it (else a
// loud line and the pick is dropped) and data.characterIDs[0] holds the
// file's template; it never writes characterIDs. With --dev-char-seats every
// seat of such a race is bound instead, pick or not - the one way that writes
// characterIDs: the bot seats 1..7 get the file's template (one "dev seats"
// line per load, and one with the instances it dropped when the next load
// arms).
void NativeChar_ArmSeats(void);

// Load stage 0, right after MEMPACK_PopToState: the seats are emptied with the
// level they were armed for, and the voices they held are let go.
void NativeChar_ClearSeats(void);

// Load stage 0, MainInit_PrimMem: the draw memory the custom models of this
// load may take, added to each of the two buffers on top of tableBytes (the
// number of the level so far, only for the log line) - the largest model of
// the roster on the main menu level (the driver select), the picked file's
// model as an invisible driver (the ghost writer) and its own mask in a race
// whose mode and characterIDs[0] let the funnel bind it, with
// --dev-char-seats seat 0 as a ghost and every other seat at its draw bytes
// (bots never turn invisible); each times two (a reflective floor draws an
// instance twice). 0 and no line without a file in the roster and for every
// other load.
u32 NativeChar_DrawReserve(int tableBytes);

// main.c, after NativeChar_LoadRoster and before MEMPACK_Init: what both
// buffers of the largest NativeChar_DrawReserve take; 0 without a file.
u32 NativeChar_MempackExtraNeeded(void);

// game/Vehicle/VehBirth.c: the model of a bound seat while data.characterIDs
// still holds the id it was armed for, else NULL (then the retail lookup).
struct Model *NativeChar_SeatModel(int index);

// The class of a seat, for every reader of MetaDataCharacters[...].engineID
// (physics and engine sound): the CHRI class of a bound seat whose guard
// holds (NativeChar_SeatModel != NULL), else retailClass - the caller's
// unchanged retail expression.
int NativeChar_SeatEngineClass(int seat, int retailClass);

// 1 for a bound custom seat whose guard holds: it never speaks with its
// template's voice - no retail voice line or short sound, and no sample of
// the voice slider in the options of the pause menu (game/HOWL/
// HOWL_Settings.c). Else 0.
int NativeChar_SeatSilent(int seat);

// THE VOICES (CVOI, include/rldchar.inc), game/HOWL/HOWL_Voiceline.c. The
// funnel reads the CVOI of the file it binds (load stage 5) and lets it go with
// the seats (NativeChar_ClearSeats, after silencing the clips). 1 for a bound
// seat whose guard holds and whose file's voices are held, else 0 - a silent
// bound seat drops every voice before the audio RNG moves, as before.
int NativeChar_SeatVoiced(int seat);

// A voiced seat speaks where its template would, decided as retail decides;
// only the sound is its own. Line: retail voice set 0..7 = event 0..7, clip
// pick % count (pick = the audio RNG value retail takes for its XA line), on
// the CD channel at volume (NativeAudio_PlayPcmLine). Short: voice type 0 =
// short-yes, 1 = short-hit, clip pick % count, beside the XA at the SPU voice
// volume the retail short sound would get (NativeAudio_PlayPcmShort). Both give the
// clip's length in the unit of CDSYS_XAGetTrackLength (disc sectors at 150 a
// second), or 0 when nothing plays (an event without clips, no audio output).
// One line "[CTR Voice] seat <s> event <key> clip <name> (<i> of <n>)" per
// clip played, "[CTR Voice] seat <s> event <key>: no clip - silent" per event
// without clips.
int NativeChar_SeatSpeakLine(int seat, u32 voiceSet, u32 pick, int volume);
int NativeChar_SeatSpeakShort(int seat, u32 voiceType, u32 pick, int volumeLeft, int volumeRight);

// game/UI/UI_Map.c, the minimap marker of a seat: the four corner colors of
// the CHRI map color (RldChar_MapColor) for a bound seat whose guard holds and
// whose file chooses one, else retail - the caller's data.ptrColor entry,
// unchanged. The blink of the player (WHITE) stays the caller's.
const u32 *NativeChar_SeatMapColor(int seat, const u32 *retail);

// The mask of a seat, for VehPickupItem_MaskBoolGoodGuy and the HUD icon
// (game/UI/UI_Weapon.c): 1 Aku Aku, 0 Uka Uka. A bound seat whose guard holds
// (NativeChar_SeatModel != NULL) and whose CHRI flags choose a mask
// (RldChar_Mask) gets that mask - but only while gGT->modelPtr holds that
// mask and its beam, else (one log line per load) and for every other seat
// retailGood, the caller's unchanged retail expression.
int NativeChar_SeatMaskGood(int seat, int retailGood);

// game/Vehicle/VehPickupItem.c, right after a mask and its beam are born: one
// line "mask seat <n>: <aku|uka> from the <file|template> (...)" for a bound
// seat, once per load; nothing for any other seat.
void NativeChar_NoteMask(const struct Driver *d, int modelID);

// game/Vehicle/VehPickupItem.c, right after the mask instance is born: the own
// mask model (CMSK, checked and relocated at start) of a bound seat whose
// guard holds, else NULL - every other seat, every bot, and a file without a
// usable CMSK keep the retail model. The caller swaps only instance->model.
struct Model *NativeChar_SeatMaskModel(int seat);

// Right after that swap: one line "mask seat <n>: own model from the file
// (<n> triangles)" per load, only for a bound seat with an own mask.
void NativeChar_NoteOwnMask(const struct Driver *d);

// The mask cases of the CHRI flags and of the seat decision, without data or
// window: adds to *checks and *failures and prints one line per failure. Part
// of --char-grid-selftest (MM_NativeCharGrid_SelfTest).
void NativeChar_MaskSelfTest(int *checks, int *failures);

// THE NATIVE MODEL (CNET, CTXT; PREVIEW). Only with the native preview
// (NATIVE DRIVERS set to PREVIEW on the GRAPHICS page, or --native-preview):
// when a seat is bound (NativeChar_ArmSeats, and --dev-char-seats), the
// file's CNET and CTXT are read again from its path, checked with
// RldChar_ReadNative (include/rldchar.inc) and held in host memory until the
// seats are cleared, with one line "native model ready: ...", "native model
// refused (<rule>), using CMDL: ..." or "native model none: ...". The upload
// and the drawing are platform/native_char_gpu.c and
// platform/native_render_layer.c. This is the held part of a bound seat whose guard
// holds (NativeChar_SeatModel != NULL), NULL for every other seat, without
// --native-preview, and for a file whose native part is missing or refused -
// the seat then draws its CMDL.
struct RldCharNative;
const struct RldCharNative *NativeChar_SeatNative(int seat);

// The file name of a bound seat whose guard holds, "" for every other seat.
const char *NativeChar_SeatFile(int seat);

// The retail model the birth of a seat will find for its character id in the
// driver pack of this load (load stage 5 on), NULL for none; the retail twin
// (step 4d) is made from it.
const struct Model *NativeChar_RetailSeatModel(int seat);

// STEP 5A, the native part of the driver select preview (platform/
// native_render_layer.c, the pull, at a change of the wanted tile; only with
// --native-preview): held for one entry at a time, in the file's own slot as a
// bound seat holds it; let go when the driver select ends and in
// NativeChar_ClearSeats. PreviewNative: the ready part held for entry, else
// NULL. PreviewEntry: the entry held, -1 for none. EntryFile: the file name of
// a roster entry, "" outside the files.
void NativeChar_HoldPreview(int entry);
void NativeChar_ReleasePreview(void);
const struct RldCharNative *NativeChar_PreviewNative(int entry);
int NativeChar_PreviewEntry(void);
const char *NativeChar_EntryFile(int entry);

// The GPU self-test (platform/native_char_gpu.c): one file through the roster
// read and the native read with the preview; 1 when the part is ready. The
// caller frees the part (RldChar_FreeNative).
int NativeChar_ReadNativeFile(const char *path, const char *name, struct RldCharNative *out);

// --char-native-selftest <folder> (main.c, ctest char_native_selftest): the
// files rldpack make-native-tests wrote, through the roster read and the
// native read without and with --native-preview, each against the
// expectation its name gives. No window, no data. 0 = passed.
int NativeChar_NativeSelfTest(const char *dir);

// game/DrawTires.c, the solid wheels and their reflection: 1 when model is the
// model of a loaded file whose CHRI flags set RLDCHAR_FLAG_NO_WHEELS (the race
// seat and the driver select preview alike), else 0 - also for NULL, retail
// models and an empty roster.
int NativeChar_ModelHidesWheels(const struct Model *model);

// The same per instance and view (both passes of game/DrawTires.c): the answer
// of the model, or 1 for a view of a custom character the render layer drew
// natively with its own wheels in this frame (step 4c; a fallback view keeps
// the retail wheels of its CMDL).
struct PushBuffer;
int NativeChar_ViewHidesWheels(const struct Instance *inst, const struct PushBuffer *pb);

// game/RenderBucket/RenderBucket_QueueExecute.c, once per instance draw: 1 when
// model is the model or the own mask of a loaded file whose CHRI flags set
// RLDCHAR_FLAG_FULL_HEIGHT - the renderer then keeps bit 0 of the height -,
// else 0: NULL, retail models, files without the bit and an empty roster.
int NativeChar_ModelFullHeight(const struct Model *model);

// THE LOOK (CHRI 0x24..0x3B, include/rldchar.inc RldChar_ParseLook): the
// ground shadow and the exhaust smoke of a custom character, keyed on the
// model like NativeChar_ModelHidesWheels - the CMDL drawing and the native
// drawing alike, at every scale factor, with or without --native-preview
// (the look is read with CHRI when the roster is read). Without a file
// whose look is not retail every function below answers at its first
// comparison and changes nothing.

// game/Vehicle/VehGroundShadow.c, once per driver and frame: RLDCHAR_LOOK_RETAIL
// (0; also NULL, retail models, an empty roster), RLDCHAR_LOOK_AUTO with
// quad = xMin, xMax, zMin, zMax in 1/16 model units, or RLDCHAR_LOOK_OFF.
int NativeChar_ModelShadow(const struct Model *model, s16 quad[4]);

// The same file, per shadow drawn auto or left out (off) in a view: only
// counts, for the exit line.
void NativeChar_NoteShadow(int mode);

// The four vectors of an auto shadow before the axis rotation, in the space
// of game/Vehicle/VehGroundShadow.c (the world times 4, scaled by the height
// factor 1..256 like the retail axes): out[0] the centre (xMid, 0, zSeam),
// out[1] the half width (x), out[2] the rear (z, zSeam - zMin), out[3] the
// front (z, zMax - zSeam). zSeam splits the quad 41 : 52 like the retail
// axes. scaleX, scaleZ: the instance scale (0x1000 = 1). Pure.
void NativeChar_ShadowAxes(const s16 quad[4], int scaleX, int scaleZ, int height, s16 out[4][3]);

// game/Particle.c, Particle_RenderList, per particle in the driver-local
// block once the instance position is added (positions in the world times
// 4): for the exhaust of a driver whose model has a look (own instance,
// driverID -1, icon group 1, 7 or 8) it moves the drawn position to the own
// point (exhaust custom) and answers 0, or answers 1 = leave the quad out of
// the ordering table (exhaust off, or custom with one point and a particle
// of retail source 1). Every other particle: 0, nothing moved. Only the
// DRAWING changes; the particle itself, its birth and the random numbers stay
// (game/Vehicle/VehEmitter.c is not touched).
struct Particle;
int NativeChar_ExhaustDraw(const struct Particle *particle, s32 *posX, s32 *posY, s32 *posZ);

// game/Vehicle/VehTurbo.c, per turbo tick: RLDCHAR_LOOK_RETAIL (0; also NULL,
// retail models, an empty roster), RLDCHAR_LOOK_CUSTOM with *count points
// (1/16 model units; the turbo flames sit exactly at them, the second flame
// is hidden with one point), or RLDCHAR_LOOK_OFF (both flames hidden).
int NativeChar_ModelExhaust(const struct Model *model, s16 point[2][3], int *count);

// The same, per tick: flames placed at own points and flames hidden while
// retail would show them. Only counts, for the exit line.
void NativeChar_NoteTurboFlames(int moved, int hidden);

// The look cases of RldChar_ParseLook, the unit probes against the retail
// constants and the pure helpers above, without data or window: adds to
// *checks and *failures, one line per failure. Part of --char-grid-selftest
// (MM_NativeCharGrid_SelfTest).
void NativeChar_LookSelfTest(int *checks, int *failures);

// Right after VehBirth_SetConsts on the birth path: one line "drive values"
// with the values just written, only for a bound seat.
void NativeChar_NoteDriveValues(const struct Driver *d, int seat);

// A custom character is in play: a seat is armed, or the pick is >= 0, or the
// driver select preview holds a custom model (MM_NativeCharGrid_PreviewCustom).
// Quick states and the start of a replay recording are refused while it is 1.
int NativeChar_Active(void);

// game/RenderBucket/RenderBucket_QueueExecute.c: an instance whose drawing
// stopped because the draw memory was full. Only counts, never changes a picture.
void NativeChar_NoteDroppedInstance(const struct Instance *inst);

// At every --shot and --rank-report VBlank: the steer line of seat 0 (only
// while seat 0 is bound, only in a race).
void NativeChar_NoteVBlank(int vblank);

#endif
