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
// only the own mask). One line "loaded" or "REFUSED" per file, after "loaded"
// one line "portrait" and, for a file with CMSK, one line "mask"; a
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
// template's portrait as before. Only the grid shows the own portrait: the
// race HUD, the results and the cup standings read characterIDs and keep the
// template's portrait (game/UI/UI_Rank.c, game/222.c, game/UI/UI_CupStandings.c).
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
// file's template; it never writes characterIDs.
void NativeChar_ArmSeats(void);

// Load stage 0, right after MEMPACK_PopToState: the seats are emptied with the
// level they were armed for.
void NativeChar_ClearSeats(void);

// game/Vehicle/VehBirth.c: the model of a bound seat while data.characterIDs
// still holds the id it was armed for, else NULL (then the retail lookup).
struct Model *NativeChar_SeatModel(int index);

// The class of a seat, for every reader of MetaDataCharacters[...].engineID
// (physics and engine sound): the CHRI class of a bound seat whose guard
// holds (NativeChar_SeatModel != NULL), else retailClass - the caller's
// unchanged retail expression.
int NativeChar_SeatEngineClass(int seat, int retailClass);

// 1 for a bound custom seat (voices are not packed yet, the seat stays
// silent), else 0.
int NativeChar_SeatSilent(int seat);

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

// game/DrawTires.c, the solid wheels and their reflection: 1 when model is the
// model of a loaded file whose CHRI flags set RLDCHAR_FLAG_NO_WHEELS (the race
// seat and the driver select preview alike), else 0 - also for NULL, retail
// models and an empty roster.
int NativeChar_ModelHidesWheels(const struct Model *model);

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
