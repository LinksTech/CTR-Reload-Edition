// rs_char.c - page "Character": build a PLY or OBJ model as .rldchar
//
// The page checks nothing itself. It starts rldpack make-char (with --check to
// check, without it to build), reads its machine lines (protocol in
// reloadstudio.h) and shows them. The Studio asks for no template: it always
// passes --template 14 (Fake Crash). The driving style is the --class switch.
//
// Flow: typing in a text field starts a new check after 600 ms, a click (a
// choice in a list, a tick, a colour) after 100 ms; choosing a model checks at
// once. A model exported again (or its MTL, a texture, the icon, a file of the
// voices folder) is checked again when Reload Studio becomes the active
// program again or the page is shown again: the page keeps a stamp (size and
// time of writing) of every file the last check read (Char_StampFields,
// Char_ImportFiles) and compares it then (Char_FilesChanged).
// The results of the last CHAR_CACHE checks are kept (their lines and their
// temp files): a check with the same command (but for its temp files) while
// the stamps of its files are the same shows the result kept again, without
// rldpack (Char_CacheFind) - a tick set and taken back, a name typed and
// taken back. Check, Enter and choosing a model always run rldpack.
// When only the name, the driving style, the mask, the minimap colour or the
// output differ from a result kept (the same model, options and files),
// rldpack checks only them - with a model file that is not there, so that it
// stops right after them - and the model part of the result kept stands
// (Char_MetaStart, Char_MetaDone): no second reading, repair, fit, reduction
// and preview of the same model. A new check makes the running one outdated: it is
// ended, its lines are only freed. A check also asks rldpack for the converted
// model (--preview, shown by the RsModelView control of rs_view.c) and, with an
// icon, for the decoded and the converted picture (--icon-preview). These go to
// files in the folder of Rs_TempDir (%TEMP%\Reload Studio, with --settings the
// folder of the settings file) that are deleted once they are shown.
//
// Size: rldpack fits every model to the size of Crash with his kart (--fit
// crash, its default) and reports the factor (@char fit); 100 % on the slider
// is that size. It reports the sizes this model allows (@value size-range);
// the slider stays inside them. It is a visual size only - physics and
// collision follow the driving style. The preview draws the model beside the
// reference dummy (rs_view.c): the kart it was fitted onto with a driver of
// Crash's size (@value crash-box), in the same scale.
//
// Options, each a make-char switch; only the switches that differ from
// rldpack's default are passed:
//   "Repair the model" (default on, --repair auto; off = --repair off): split
//      corners welded, faces turned outward, small holes closed; @char
//      repaired says what changed, shown below the options when anything did
//   "Draw open parts from both sides" (default on, --open-parts two-sided;
//      off = --open-parts one-sided): parts still open after the repair are
//      drawn from both sides at no extra triangles; @char two-sided counts them
//   "Closed hull (remesh)" (default OFF, only on request: --remesh on): every
//      part becomes a closed hull that the reduction then brings back under
//      the limit; @char remeshed gives the counts. The preview shows the
//      result like every other build, so the author sees what it does. It
//      needs "Reduce to fit": greyed out (and not passed) while that is off
//   "Reduce to fit" (default OFF, --reduce off; on = rldpack's default
//      --reduce auto): only a model over the limit of triangles a driver may
//      draw loses triangles until it fits, @char reduced says how many; a
//      model under the limit is never reduced. Off and over the limit the
//      check fails, the headline gives the triangles and the limit (@char
//      budget) and the button "Reduce to fit" below it ticks the box and
//      checks again
//   "Show kart wheels" (default on, --wheels; off = the game draws no kart
//      wheels for this driver, for models with wheels of their own; the
//      preview's dummy follows at once)
// THE NATIVE MODEL (PREVIEW, open to everyone): an OBJ gets its own mesh,
// UVs and textures beside the classic model (--native-model on) when
// "Native model" on the card Import is ticked and Show kart wheels is off -
// or on, with a wheel model of the card Wheels (rs_wheels.c): then
// --wheel-model <file> [--wheel-size <percent>] as well, and the native model
// drives on the author's wheels while the classic model keeps the game's
// (Char_NativePassed, Char_WheelPassed). Not ticked (the start) the command
// and the bytes are those of before. The look (tab In-game look), the native
// model in the preview and the card Wheels are open as well; each is marked
// "Preview feature".
// THE USER MODE (Rs_NativeForUsers: today --enable-preview-features, later
// perhaps the release of the native model): the first four options and Colors (card
// Import) are hidden and stay rldpack's defaults - nothing of them is passed,
// so a model over the limit is reduced for the classic model (CMDL) while
// the native model keeps every face. An OBJ with Show kart wheels off is
// built with --native-model on (no tick box). The game draws a native model
// only with the kart wheels hidden, so with the kart wheels an OBJ is built
// as the classic model only (no CNET, no test wheel, no dialog), and so is a
// PLY, which names no texture file; the line below the options says which
// and follows Show kart wheels at once (Char_NativeUpdate). Beside Show kart
// wheels "Include classic fallback model", ticked and greyed out, "Coming
// soon": the classic model is always built. The preview shows the native
// model with its textures when one is built (else the classic model with the
// game's wheels, as without the switch); an error of the native part stops
// the check and the build as every error does (its message names the cause).
//
// THE PREVIEW (rs_view.c): its camera (drag, right drag, the mouse wheel, a
// double click, the bar and the View menu in the view) and its display
// toggles (background, Crash size, shadow, exhaust, Native or Classic) belong
// to the view - nothing of them is built or stored, every start begins with
// the start view; the verbs view-* set them (Char_AutoView). The note below
// it says which look it shows, why, the textures not found and how to steer
// it (Char_ViewNoteUpdate).
//
// Mask: Aku Aku or Uka Uka, the mask the driver wears (the mask item, the
// rescue after a fall, its sound and music, the HUD icon). The choice starts
// at the mask of the template (Fake Crash: Uka Uka); --mask aku|uka is always
// passed, so the file carries the choice of its author (the game then says
// "from the file", not "from the template").
//
// Minimap colour: like the template (Fake Crash: grey, nothing passed, the
// file keeps its bytes) or a colour of your own from the colour dialog,
// passed as --map-color RRGGBB. The swatch shows it; clicking it chooses.
//
// Icon: a PNG of any size; rldpack turns it into the game's 43 x 25 portrait
// of 16 colours and keeps the PNG's transparency. "Framing" (--icon-fit): "Fit
// like the game's heads" (the Studio's default, fit: the subject whole in the
// head box of the game's portraits), "Fill the frame" (fill: cut at the sides
// or the bottom) or "As is" (none, rldpack's default: cut to 43:25 in the
// middle, as before);
// never stretched. "Make background transparent" (default off, --icon-background
// corners) takes away the colour that touches the corners - in any PNG, one
// with transparency keeps a thin outline (@char icon_background says how many
// pixels and which colour); "Retail frame" (default off, --icon-frame retail)
// puts the frame and the dark box of the game's portraits behind it. Without
// an icon none of them is passed. The second picture shows the portrait as the
// game draws it, on a colour of the race, beside the template's portrait that
// rldpack reads from the game's data; without them the measured frame stands
// in as lines.
//
// Model: a PLY, or an OBJ with its material file (MTL) and the textures the
// MTL names (PNG, JPG, TGA). rldpack tells the format by the content and
// says what it read in @model lines: the format, the MTL, every texture with
// its material, the groups (o, g) and where the colours came from. For an OBJ
// the tab Model shows them below the model field: one line with the summary
// and a small list (MTL, textures, groups) that scrolls in itself - where the
// tab has no room for it, no list (Char_Layout) - and below them the field
// "Textures folder" (--textures: a folder rldpack looks in first for a
// texture that is not at its path; empty = not passed), shown when the last
// check missed a texture or a folder is set (Char_TexturesUpdate): an OBJ
// whose textures were all found needs none, and the room is the list's. A
// PLY shows
// the line only when the file is not named .ply. What went wrong (mtl-*,
// tex-*, obj-*, model-*) is in the message list like every message about the
// model; an error there stops the build as always.
//
// Voices (tab 4): a folder of WAV or VAG files, --voices. rldpack names every
// file of it (@voice: length, size, the event it fills or none) and counts the
// clips of the ten events (@voiceevent, in the order of s_rldCharVoiceEvents in
// include/rldchar.inc); the file names pick the event (boost1..boost4 ... fire4,
// yes, hit). The list shows the files; the choice "Event" below it gives the
// file chosen an event of its own or none (--voice <file>=<event|none>, kept
// until the folder changes), "Play" plays the file as the game will hear it
// (the WAV of --voice-preview, also for a VAG). "Normalize volume" (default on,
// --voice-normalize) brings every clip to the same peak. Without a folder none
// of these switches is passed: the command is the one of a driver without
// voices, and the driver is silent in the game.
//
// After a build the headline names the mask and the kart wheels in the file,
// where a wrong choice cannot be missed, and says to restart the game: it
// reads its characters folder only when it starts. The options are not
// remembered - every start of the Studio begins with the defaults.
//
// Layout: the steps as tabs on the left, the 3D preview always on the right,
// the build below both, so that every tab is seen whole without scrolling:
//   1 Model         the PLY or OBJ, its size, the options and what rldpack did
//                   to it
//   2 Driver        name, driving style, mask
//   3 In-game look  two cards, switched in the title line: Portrait (icon,
//                   framing, transparency, frame, the game's view of it) and
//                   In the race (minimap colour; shadow and exhaust of the
//                   driver with two points and Pick - preview feature, see
//                   THE LOOK OF THE DRIVER)
//   4 Voices        the folder, its files and their events, Play, the ten
//                   events at a glance
//   5 Extras        the cards Import (how rldpack reads the model: its up and
//                   forward axes, the colours of its palette; for an OBJ how
//                   its vertex colours meet its textures), Wheels and
//                   Animations (rs_wheels.c, rs_anim.c), one at a time;
//                   Wheels a preview feature open to everyone, Animations
//                   locked without --enable-preview-features. Import
//                   lives here, not on the tab Model: there an OBJ leaves no
//                   room at 1366 x 768 and at 1920 x 1080 with 150 % (the bar
//                   is at its least height), and its choices are rarely
//                   needed - rldpack names the one to change when it is.
// The head of a tab shows its number in a circle: green done, red problem (an
// error of the last run belongs to it), amber warning, an outline when it is
// optional or still to do. The bar at the bottom: headline, the message list
// (a click on a message opens the tab it belongs to), output, Check, Build,
// Back and Next. A check or build that runs longer than CHAR_PROGRESS_DELAY
// shows a bar of its progress (the @progress lines of rldpack, which the
// shell passes as RS_WM_JOB_PROGRESS) and Cancel beside the headline: Cancel
// ends rldpack (Rs_JobCancel); a build cancelled leaves at most <out>.part,
// which the page deletes - a file <out> from before stays as it was. Every
// control is a child of the page as before, with the same ID; the controls of
// the other tabs are only hidden - a message to one of them still works. Ctrl+Tab and Ctrl+Shift+Tab step through the tabs.

#include "reloadstudio.h"
#include "rs_anim.h"
#include "rs_view.h"
#include "rs_wheels.h"
#include <commdlg.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define CHAR_TIMER_CHECK   1
#define CHAR_TIMER_BUSY    2      // the progress bar and Cancel show after CHAR_PROGRESS_DELAY
#define CHAR_PROGRESS_DELAY 300
#define CHAR_PROGRESS_CLASS L"RsCharProgress"
#define CHAR_CHECK_DELAY   600    // after typing in a text field
// After a click on a list, a tick or a colour: short, but not at once - the
// arrow keys and the mouse wheel step through a closed list one CBN_SELCHANGE
// at a time, and each would start and end a run of rldpack.
#define CHAR_CLICK_DELAY   100
#define CHAR_VAL           1024   // length of a value or path
#define CHAR_VOICE_SET_MAX 64     // files with an event of their own (--voice), at most
#define CHAR_MAX_ARGS      (56 + 2 * CHAR_VOICE_SET_MAX)
#define CHAR_CMD_CAP       32768  // the command in the raw output, at most (the shell's RS_CMD_CAP)
#define CHAR_NAME_MAX      17     // RLDCHAR_NAME_MAX in include/rldchar.inc
#define CHAR_SIZE_MIN      50     // range of the --size switch
#define CHAR_SIZE_MAX      200
#define CHAR_SIZE_DEFAULT  100
#define CHAR_TEMPLATE      L"14"  // Fake Crash: the Studio asks for no template
// The mask of CHAR_TEMPLATE, an index into g_charMasks: Fake Crash wears Uka Uka
// (bit 14 is clear in RLDCHAR_TEMPLATE_AKU_BITS, include/rldchar.inc). It has
// to change with CHAR_TEMPLATE; rldpack says the template's mask in @value mask.
#define CHAR_TEMPLATE_MASK 1
#define CHAR_PENDING       16     // outdated checks whose temp files wait for their end
#define CHAR_CACHE         4      // checks whose results are kept (Char_Cache*)
#define CHAR_POSES         3      // poses in a --preview file
#define CHAR_IMAGE_CLASS   L"RsCharImage"
#define CHAR_SWATCH_CLASS  L"RsCharSwatch"
#define CHAR_TEMPLATE_MAP_COLOR RGB(0x80, 0x80, 0x80)   // Fake Crash's minimap colour (data.colors, 0x808080)
#define CHAR_IMAGE_MAX     16384  // largest edge of a picture the page shows
#define CHAR_ICON_W        43     // the part of the icon the menu tile shows (--icon-preview)
#define CHAR_ICON_H        25
// The icon in the game (the second picture): a colour of the race (the mean of
// the race beside the HUD ranking, measured once - only the numbers are here),
// the template's portrait on the left, the own on the right. Without the game's
// data the measured frame of the retail portraits (x 2..38, y 1..23) and their
// head box (x 0..40, y 0..24, dotted) stand in as lines.
#define CHAR_GAME_BG       RGB(107, 87, 78)
#define CHAR_GAME_LINE     200    // grey of those lines, all three channels
#define CHAR_GAME_GAP      4      // picture pixels around and between the portraits
#define CHAR_GAME_W        (2 * CHAR_ICON_W + 3 * CHAR_GAME_GAP)
#define CHAR_GAME_H        (CHAR_ICON_H + 2 * CHAR_GAME_GAP)
#define CHAR_INFO_LINES    3      // lines of the model info at most
#define CHAR_QUALITY_LINES 6      // lines of the repair / remesh note at most
#define CHAR_NOTE_LINES    3      // lines of a note below a field at most (they wrap)
#define CHAR_VIEW_NOTE_LINES 5    // lines of the note below the preview at most (Char_ViewNoteFit)
#define CHAR_VIEW_PARTS    6      // its sentences at most
#define CHAR_REPAIR_FIELDS 10     // numbers of @char repaired
#define CHAR_REMESH_FIELDS 5      // numbers of @char remeshed
#define CHAR_VOICE_EVENTS  10     // RLDCHAR_VOICE_EVENTS in include/rldchar.inc
#define CHAR_VOICE_NONE    CHAR_VOICE_EVENTS   // index of "Unassigned" in g_charVoiceEvents
#define CHAR_VOICE_PASSING 6      // the event never heard (g_charVoiceEvents)
#define CHAR_VOICE_PER_EVENT 4    // RLDCHAR_VOICE_PER_EVENT: more is an error of rldpack
#define CHAR_VOICE_NAME    260    // length of a file name of the voices folder
#define CHAR_VOICE_CLASS   L"RsCharVoiceEvents"
#define CHAR_VOICE_CELL_H  22     // a row of the overview of the events
#define CHAR_VOICE_LIST_MIN_H 96  // the file list, at least: its head and three rows
#define CHAR_IMPORT_ROWS_MAX 1024 // rows of each kind (MTL, texture, group) the list keeps, at most
#define CHAR_IMPORT_MIN_ROWS 2    // rows that list shows at least (it scrolls in itself), 1 where
                                  // two would push the bar below CHAR_BAR_MIN_H
// Posted to the page by its subclass of the main window when Reload Studio
// becomes the active program again (WM_ACTIVATEAPP goes to the main window
// only). WM_APP + 64 and up are the page's own (reloadstudio.h).
#define CHAR_WM_ACTIVATED  (WM_APP + 64)
#define CHAR_SUBCLASS_MAIN 0x43484152  // the ID of that subclass ("CHAR")

// Controls
#define CHAR_ID_MODEL_LABEL    100
#define CHAR_ID_MODEL          101
#define CHAR_ID_MODEL_BROWSE   102
#define CHAR_ID_MODEL_INFO     103
#define CHAR_ID_NAME_LABEL     104
#define CHAR_ID_NAME           105
#define CHAR_ID_NAME_NOTE      106
#define CHAR_ID_CLASS_LABEL    107
#define CHAR_ID_CLASS          108
#define CHAR_ID_CLASS_HELP     109
#define CHAR_ID_SIZE_LABEL     110
#define CHAR_ID_SIZE           111
#define CHAR_ID_SIZE_VALUE     112
#define CHAR_ID_SIZE_NOTE      113
#define CHAR_ID_SIZE_HINT      114
#define CHAR_ID_REDUCE         115
#define CHAR_ID_WHEELS         116
#define CHAR_ID_SIZE_FIT       117
#define CHAR_ID_SIZE_CRASH     118
#define CHAR_ID_OPTIONS_LABEL  119
#define CHAR_ID_ICON_LABEL     120
#define CHAR_ID_ICON           121
#define CHAR_ID_ICON_BROWSE    122
#define CHAR_ID_ICON_CLEAR     123
#define CHAR_ID_ICON_CAPTION   124   // 124..125
#define CHAR_ID_ICON_IMAGE     126   // 126..127
#define CHAR_ID_VOICES_LABEL   130
#define CHAR_ID_VOICES         131
#define CHAR_ID_VOICES_BROWSE  132
#define CHAR_ID_VOICES_CLEAR   133
#define CHAR_ID_VOICES_NOTE    134
#define CHAR_ID_VOICE_LIST     135   // the files of the folder
#define CHAR_ID_VOICE_EVENT_LABEL 136
#define CHAR_ID_VOICE_EVENT    137   // the event of the file chosen
#define CHAR_ID_VOICE_PLAY     138
#define CHAR_ID_VOICE_NORMALIZE 139
#define CHAR_ID_VIEW           140
#define CHAR_ID_POSE           141
#define CHAR_ID_VIEW_NOTE      142
#define CHAR_ID_OUT_LABEL      150
#define CHAR_ID_OUT            151
#define CHAR_ID_OUT_BROWSE     152
#define CHAR_ID_CHECK          153
#define CHAR_ID_BUILD          154
#define CHAR_ID_HEADLINE       155
#define CHAR_ID_MESSAGES       156
#define CHAR_ID_RAW            157
#define CHAR_ID_RAW_TOGGLE     158
#define CHAR_ID_SHOW           159
#define CHAR_ID_REPAIR         160
#define CHAR_ID_OPEN_PARTS     161
#define CHAR_ID_REMESH         162
#define CHAR_ID_QUALITY        163
#define CHAR_ID_MASK_LABEL     164
#define CHAR_ID_MASK           165
#define CHAR_ID_MASK_HELP      166
#define CHAR_ID_MAPCOLOR_LABEL 167
#define CHAR_ID_MAPCOLOR       168   // the swatch
#define CHAR_ID_MAPCOLOR_PICK  169
#define CHAR_ID_MAPCOLOR_LIKE  170
#define CHAR_ID_MAPCOLOR_HELP  171
#define CHAR_ID_ICON_FIT_LABEL 172
#define CHAR_ID_ICON_FIT       173
#define CHAR_ID_ICON_CORNERS   174   // "Make background transparent"
#define CHAR_ID_ICON_FRAME     175   // "Retail frame"
#define CHAR_ID_REDUCE_FIT     176   // the button "Reduce to fit" below the options (tab Model)
#define CHAR_ID_CANCEL         177   // Cancel beside the headline, while rldpack runs
#define CHAR_ID_PROGRESS       178   // the progress bar beside it
#define CHAR_ID_TAB            180   // 180..184: the heads of the tabs 1..5
#define CHAR_ID_BACK           185
#define CHAR_ID_NEXT           186
#define CHAR_ID_EXTRAS         187   // 187..189: Import | Wheels | Animations in the tab Extras
#define CHAR_ID_VOICE_RULE     191   // the line with the file names (tab Voices)
#define CHAR_ID_VOICE_EVENTS_LABEL 192
#define CHAR_ID_VOICE_EVENTS   193   // the ten events at a glance
#define CHAR_ID_MODEL_IMPORT   194   // the line of what rldpack read (format, MTL, textures, colours)
#define CHAR_ID_MODEL_FILES    195   // the list of the MTL, the textures and the groups of an OBJ
#define CHAR_ID_TEXTURES_LABEL 196   // Textures folder (an OBJ only)
#define CHAR_ID_TEXTURES       197
#define CHAR_ID_TEXTURES_BROWSE 198
#define CHAR_ID_TEXTURES_CLEAR 199
#define CHAR_ID_FALLBACK       200   // "Include classic fallback model" (tab Model, Rs_NativeForUsers only)
#define CHAR_ID_FALLBACK_HINT  201   // its "Coming soon"
#define CHAR_ID_NATIVE_LINE    202   // the native model's result or why there is none (tab Model, Rs_NativeForUsers only)
// The controls of the cards Wheels and Animations (WH_ID_FIRST..WH_ID_LAST in
// rs_wheels.c, AN_ID_FIRST..AN_ID_LAST in rs_anim.c): the tab Extras shows them.
// The card Import (380..399) is the page's own.
#define CHAR_ID_WHEELS_FIRST   300
#define CHAR_ID_WHEELS_LAST    339
#define CHAR_ID_ANIM_FIRST     340
#define CHAR_ID_ANIM_LAST      379
#define CHAR_ID_IMPORT_FIRST   380
#define CHAR_ID_IMPORT_NOTE    380   // the note at the top of the card Import
#define CHAR_ID_UP_LABEL       381   // Up: Y | Z
#define CHAR_ID_UP             382
#define CHAR_ID_UP_HELP        383
#define CHAR_ID_FORWARD_LABEL  384   // Forward: +Z | -Z
#define CHAR_ID_FORWARD        385
#define CHAR_ID_FORWARD_HELP   386
#define CHAR_ID_COLORS_LABEL   387   // Colors: 128 | 64
#define CHAR_ID_COLORS         388
#define CHAR_ID_COLORS_HELP    389
#define CHAR_ID_VCOLORS_LABEL  390   // Vertex colors (an OBJ only)
#define CHAR_ID_VCOLORS        391
#define CHAR_ID_VCOLORS_HELP   392
#define CHAR_ID_NATIVE_LABEL   393   // Native model (preview feature): the tick box, the hint, the note
#define CHAR_ID_NATIVE         394
#define CHAR_ID_NATIVE_HINT    395
#define CHAR_ID_NATIVE_HELP    396
#define CHAR_ID_IMPORT_LAST    399
#define CHAR_ID_LOOK_SWITCH    400   // 400..401: Portrait | In the race in the title line of the tab In-game look
#define CHAR_ID_LOOK_HINT      402   // "Preview feature" right in that line (card In the race)
#define CHAR_ID_SHADOW_LABEL   403   // the card In the race: shadow and exhaust of the driver (renderer package A)
#define CHAR_ID_SHADOW         404
#define CHAR_ID_SHADOW_HELP    405
#define CHAR_ID_EXHAUST_LABEL  406
#define CHAR_ID_EXHAUST        407
#define CHAR_ID_EXHAUST_HELP   408
#define CHAR_ID_POINT          409   // 409..418: per exhaust point its label, x, y, z and Pick (CHAR_POINT_IDS each)
#define CHAR_ID_LOOK_NOTE      419
#define CHAR_POINT_IDS         5
#define CHAR_LOOK_PORTRAIT     0     // the cards of the tab In-game look
#define CHAR_LOOK_RACE         1
#define CHAR_LOOK_CARDS        2

// CHAR_JOB_META: rldpack checks only the name, the driving style, the mask,
// the minimap colour and the output (Char_MetaStart).
enum { CHAR_JOB_NONE = 0, CHAR_JOB_CHECK, CHAR_JOB_BUILD, CHAR_JOB_META };
// What a machine line is to Char_MetaDone (Char_LineKind).
enum { CHAR_LINE_MODEL = 0, CHAR_LINE_META, CHAR_LINE_END };

// The tabs, in their order.
enum { CHAR_TAB_MODEL = 0, CHAR_TAB_DRIVER, CHAR_TAB_LOOK, CHAR_TAB_VOICES, CHAR_TAB_EXTRAS, CHAR_TABS };
// What the head of a tab shows.
enum { CHAR_STEP_TODO = 0, CHAR_STEP_OPTIONAL, CHAR_STEP_DONE, CHAR_STEP_WARNING, CHAR_STEP_PROBLEM };
// The card the tab Extras shows.
enum { CHAR_EXTRAS_IMPORT = 0, CHAR_EXTRAS_WHEELS, CHAR_EXTRAS_ANIM, CHAR_EXTRAS_COUNT };
enum { CHAR_IMG_ORIGINAL = 0, CHAR_IMG_ICON, CHAR_IMG_COUNT };

// ---------------------------------------------------------------------------
// Tables
// ---------------------------------------------------------------------------

struct CharClass {
    const wchar_t *word;    // this is how the value goes to rldpack (--class)
    const wchar_t *text;
};

// The four engine classes with their retail drivers (enum EngineClass,
// include/namespace_Vehicle.h; engineID in data.MetaDataCharacters).
static const struct CharClass g_charClasses[] = {
    { L"balanced",     L"Balanced - like Crash, Cortex, Komodo Joe, Fake Crash" },
    { L"acceleration", L"Acceleration - like Coco, N. Gin, Pinstripe" },
    { L"speed",        L"Speed - like Tiny, Dingodile, Papu Papu, N. Tropy" },
    { L"turning",      L"Turning - like Polar, Pura, Ripper Roo, Penta Penguin" },
};
#define CHAR_CLASS_COUNT ((int)(sizeof(g_charClasses) / sizeof(g_charClasses[0])))

struct CharMask {
    const wchar_t *word;    // this is how the value goes to rldpack (--mask)
    const wchar_t *name;    // for the build message and the report
    const wchar_t *text;
};

// The two masks, in the order of RLDCHAR_MASK_AKU, RLDCHAR_MASK_UKA
// (include/rldchar.inc) less one. In retail Crash, Coco, Polar, Pura and Penta
// wear Aku Aku (RLDCHAR_TEMPLATE_AKU_BITS), every other driver Uka Uka.
static const struct CharMask g_charMasks[] = {
    { L"aku", L"Aku Aku", L"Aku Aku - like Crash, Coco, Polar, Pura, Penta" },
    { L"uka", L"Uka Uka", L"Uka Uka - like the template (Fake Crash), Cortex, Tiny" },
};
#define CHAR_MASK_COUNT ((int)(sizeof(g_charMasks) / sizeof(g_charMasks[0])))

struct CharFit {
    const wchar_t *word;    // this is how the value goes to rldpack (--icon-fit)
    const wchar_t *text;
};

// The framing of the icon, in the order of the combo box.
static const struct CharFit g_charIconFits[] = {
    { L"fit",  L"Fit like the game's heads" },
    { L"fill", L"Fill the frame" },
    { L"none", L"As is" },
};
#define CHAR_ICON_FIT_COUNT   ((int)(sizeof(g_charIconFits) / sizeof(g_charIconFits[0])))
#define CHAR_ICON_FIT_DEFAULT 0   // what the page starts with
#define CHAR_ICON_FIT_RLDPACK 2   // rldpack's default, not passed

// The axes of the model as the lists of the card Import offer them (--up,
// --forward); only what is not the default (the first) is passed. The texts
// are the ones rldpack's messages name ("Up Z").
static const wchar_t *const g_charUpWords[] = { L"y", L"z" };
static const wchar_t *const g_charUpTexts[] = { L"Y (default)", L"Z" };
static const wchar_t *const g_charForwardWords[] = { L"z", L"-z" };
static const wchar_t *const g_charForwardTexts[] = { L"+Z (default)", L"-Z" };
#define CHAR_UP_COUNT      2
#define CHAR_FORWARD_COUNT 2
// The colours of the model's palette (--colors): 128, rldpack's default, is
// not passed.
static const wchar_t *const g_charColorsWords[] = { L"128", L"64" };
static const wchar_t *const g_charColorsTexts[] = { L"128 (default)", L"64" };
#define CHAR_COLORS_COUNT  2

// How the vertex colours of an OBJ meet its textures (--vertex-colors), in the
// order of the list on the card Import; only what is not auto is passed. The
// texts are the ones rldpack's messages name ("Vertex colors: Plain color").
static const wchar_t *const g_charVColorWords[] = { L"auto", L"modulate", L"color" };
static const wchar_t *const g_charVColorTexts[] = { L"Auto (default)", L"Texture modulation (PS1)", L"Plain color" };
#define CHAR_VCOLORS_COUNT ((int)(sizeof(g_charVColorWords) / sizeof(g_charVColorWords[0])))

struct CharVoiceEvent {
    const wchar_t *word;    // this is how the value goes to rldpack (--voice <file>=<word>)
    const wchar_t *text;
};

// The events of the voices in the order of the CVOI event table
// (s_rldCharVoiceEvents in include/rldchar.inc): 0..7 the retail voice sets,
// 8 and 9 the two short sounds; then "none", a file that fills no event.
// Passing is said by the driver who has just passed the player
// (game/PlayLevel.c): the player's own driver never says it, and a custom
// driver is only ever the player's (one-player ARCADE) - its passing clips
// are packed, but never heard.
// Fire is also the warp orb and the clock (game/Vehicle/VehPickupItem.c).
static const struct CharVoiceEvent g_charVoiceEvents[CHAR_VOICE_EVENTS + 1] = {
    { L"boost", L"Boost" },          { L"hit", L"Hit" },         { L"spin", L"Spin out" },
    { L"bigair", L"Big air" },       { L"drop", L"Drop" },       { L"shield", L"Shield" },
    { L"passing", L"Passing - never heard" },      { L"fire", L"Fire" },       { L"short-yes", L"Short: yes" },
    { L"short-hit", L"Short: hit" }, { L"none", L"Unassigned" },
};

// Poses of the preview, in the order of the --preview file: anim 0 frame 10
// (neutral), frame 0 (full steer left) and frame 20 (full steer right) - the
// game maps a left steer to frame 0 (game/Vehicle/VehFrame.c, VehPhysProc.c).
// The tabs: the word of the automation verb "tab", the text of the head.
static const wchar_t *const g_charTabWords[CHAR_TABS] = { L"model", L"driver", L"look", L"voices", L"extras" };
static const wchar_t *const g_charTabTexts[CHAR_TABS] = { L"Model", L"Driver", L"In-game look", L"Voices", L"Extras" };
static const wchar_t *const g_charStepWords[] = { L"to do", L"optional", L"done", L"warning", L"problem" };
static const wchar_t *const g_charExtrasWords[CHAR_EXTRAS_COUNT] = { L"import", L"wheels", L"animations" };
static const wchar_t *const g_charExtrasTexts[CHAR_EXTRAS_COUNT] = { L"Import", L"Wheels", L"Animations" };
static const wchar_t *const g_charLookTexts[CHAR_LOOK_CARDS] = { L"Portrait", L"In the race" };
static const wchar_t *const g_charLookWords[CHAR_LOOK_CARDS] = { L"portrait", L"race" };
// The look of the driver (make-char --shadow, --exhaust; the order of the
// modes in CHRI: retail 0, auto/custom 1, off 2).
static const wchar_t *const g_charShadowTexts[3] = { L"Retail", L"Auto", L"Off" };
static const wchar_t *const g_charShadowWords[3] = { L"retail", L"auto", L"off" };
static const wchar_t *const g_charExhaustTexts[3] = { L"Retail", L"Custom", L"Off" };
static const wchar_t *const g_charExhaustWords[3] = { L"retail", L"custom", L"off" };
// The retail look in 1/16 game units (as rldpack's @value retail-shadow and
// retail-exhaust): the shadow quad x0 x1 z0 z1 and the two smoke sources.
static const int g_charRetailQuad[4] = { -800, 800, -820, 1040 };
static const int g_charRetailPoints[2][3] = { { 288, 896, -896 }, { -288, 896, -896 } };

static const wchar_t *const g_charPoseWords[CHAR_POSES] = { L"neutral", L"left", L"right" };
static const wchar_t *const g_charPoseTexts[CHAR_POSES] = { L"Neutral", L"Steering left", L"Steering right" };
static const int g_charPoseView[CHAR_POSES] = { RS_VIEW_POSE_NEUTRAL, RS_VIEW_POSE_FRAME0, RS_VIEW_POSE_FRAME20 };
// The fixed views of the preview by the words of the verb view-preset and the
// report, in the order of RS_VIEW_PRESET_* (rs_view.h).
static const wchar_t *const g_charPresetWords[RS_VIEW_PRESET_COUNT] = { L"front", L"side", L"back", L"top", L"34", L"race" };

// Before a model is chosen, below its field (the page has no subtitle).
#define CHAR_START_TEXT L"Pick a PLY or OBJ model of driver, steering wheel and kart. rldpack converts and checks it and builds the .rldchar."
#define CHAR_SIZE_HINT_TEXT L"Visual size only - physics and collision follow the driving style."
#define CHAR_NAME_RULE_TEXT L"1 to 17 characters: A-Z 0-9 space ! % ' + , - . / : < = > ? _"
#define CHAR_VOICES_TEXT L"Optional - without voices the driver is silent in the game."
#define CHAR_VOICE_RULE_TEXT L"File names: boost1-4 hit1-4 spin1-4 bigair1-4 drop1-4 shield1-4 passing1-4 fire1-4, yes, hit (.wav, .vag)"
#define CHAR_FIT_WAIT_TEXT L"The model is fitted to Crash size when it is checked."
// (No line break between 75 and %: a no-break space.)
#define CHAR_VIEW_SHADE_TEXT L"Colours as on a bright road. On dark ground the game shades every driver, by up to 75\u00A0%."
#define CHAR_REMESH_NEEDS_TEXT L"Closed hull needs Reduce to fit: the hulls have far more triangles than a driver may draw."

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

struct CharMsg {
    int severity;
    wchar_t *code;
    wchar_t *text;
    wchar_t *detail;
};

// A picture as 32-bit BGRA, top row first.
struct CharImage {
    int w, h;
    unsigned char *px;
};

// A file of the voices folder, one @voice line.
struct CharVoice {
    wchar_t name[CHAR_VOICE_NAME];  // the file name, as --voice takes it
    wchar_t state[16];              // ok | bad | unknown | ignored | unused | cut
    int event;                      // index into g_charVoiceEvents, CHAR_VOICE_NONE = none
    long ms;                        // length of the sound in the file, -1 = not reported
    long long bytes;                // size of the file, -1 = not reported
    long rate;
    int channels;
    int peak;                       // per mille of full scale, -1 = not reported
    wchar_t *preview;               // the --voice-preview WAV (Rs_Free), NULL = none
};

// A row of the list of what an OBJ brought along: its MTL, a texture (one
// @model texture line, per material) or a group (@model group).
enum { CHAR_IMPORT_MTL = 0, CHAR_IMPORT_TEXTURE, CHAR_IMPORT_GROUP };
struct CharImport {
    int kind;                       // CHAR_IMPORT_*
    wchar_t state[16];              // ok | missing | unreadable | unsupported | bad | none ("" for a group)
    wchar_t material[128];          // the material of a texture
    wchar_t path[CHAR_VAL];         // the file as rldpack names it, or the name of the group
    long long faces;                // faces of a group
};

// What the running (or last) rldpack run reported.
struct CharJobData {
    int protocolSeen, protocol;
    int modelSeen;                  // @file ply | obj: the model file
    wchar_t modelState[16];
    long long modelBytes;
    wchar_t modelFormat[8];         // @model format: ply | obj, "" = not reported
    wchar_t modelColors[16];        // @model colors: vertex | material | texture | grey | mixed
    struct CharImport *import;      // @model mtl, texture and group lines, in their order
    int importCount, importCap;
    int importLines[3];             // the lines of each kind CHAR_IMPORT_*, kept or not
    long groupsMore;                // groups rldpack did not list (@msg obj-groups "<n> more groups ...")
    int outSeen;
    wchar_t out[CHAR_VAL];
    int rangeSeen, rangeLo, rangeHi;
    int rangeNone;                  // "size-range 0 0": no size fits this model
    int kartSeen;
    int kart[6];                    // @value kart-box: the model's own kart
    int retailSeen;
    int retail[6];                  // @value retail-kart: the retail kart, the size reference
    int crashSeen;
    int crash[6];                   // @value crash-box: Crash with his kart, in tenths of game units
    wchar_t reduce[48];             // @value reduce: "<auto|off> <origin>"
    wchar_t fit[48];                // @value fit: "<crash|none> <origin>"
    wchar_t wheels[48];             // @value wheels: "<on|off> <origin>"
    wchar_t repair[48];             // @value repair: "<auto|off> <origin>"
    wchar_t openParts[48];          // @value open-parts: "<two-sided|one-sided> <origin>"
    wchar_t remesh[48];             // @value remesh: "<on|off> <origin>"
    wchar_t mask[48];               // @value mask: "<aku|uka|none> <origin>"
    wchar_t mapColor[48];           // @value map-color: "<RRGGBB|template> <origin>"
    int repairedSeen;
    long repaired[CHAR_REPAIR_FIELDS];  // @char repaired: welded, degenerate, duplicate, flipped, holes
                                        // closed, hole triangles, holes left, open edges before, after,
                                        // cracks split
    int twoSidedSeen;
    long twoSided;                  // @char two-sided: triangles of open parts drawn from both sides
    int remeshedSeen;
    long remeshed[CHAR_REMESH_FIELDS];  // @char remeshed: triangles of the source, of the hulls, after;
                                        // open edges before, after
    int reducedSeen;
    int budgetSeen;
    int iconRetailOk;               // @file icon-retail ok: the template's portrait was written
    wchar_t iconBackground[48];     // @char icon_background: "<pixels cleared> <RRGGBB>"
    wchar_t iconPlace[48];          // @char icon_place: "<x> <y> <w> <h>" in the 43 x 25, 0 0 0 0 without fit
    long budget[4];                 // @char budget: triangles, their limit, draw bytes, their limit
    long reduced[4];                // @char reduced: triangles before, after; draw bytes before, after
    int fitSeen;
    wchar_t fitFactor[16];          // @char fit: factor, length before/after, height before/after
    wchar_t fitLength[2][16];
    wchar_t fitHeight[2][16];
    wchar_t fitBasis[16];           // kart | model: what was matched to Crash's length
    wchar_t triangles[32];          // @char triangles (or faces)
    wchar_t parts[160];             // @char parts: "kart 1, driver 2, steering wheel 3"
    int voicesSeen;                 // @char voices <clips> <events filled>
    int voiceClips, voiceFilled;
    int voiceEventSeen;             // @voiceevent <event> <clips>, all ten
    int voiceEvent[CHAR_VOICE_EVENTS];
    struct CharVoice *voice;        // @voice, one per file of the folder
    int voiceCount, voiceCap;
    struct CharMsg *msgs;
    int msgCount, msgCap;
    int resultSeen;
    wchar_t resultState[16];
    wchar_t resultPath[CHAR_VAL];
    long long resultBytes;
    wchar_t resultSha[80];
    int endSeen, endCode;
    wchar_t nativeText[256];        // @char native-model as the note says it, "" = none (Char_NativeResult)
    int lookQuadSeen;               // @value shadow-quad x0 x1 z0 z1 (game units) -> 1/16
    int lookQuad[4];
};

static struct {
    HWND modelLabel, model, modelBrowse, modelInfo;
    HWND modelImport, modelFiles;   // what rldpack read (Char_ApplyImport)
    int importOn;                   // the line below the model info is shown (on the tab Model)
    int filesOn;                    // and the list below it
    // What the last check read, taken over from g_charJob when it ends.
    struct CharImport *importRow;
    int importRowCount;
    int importTotal[3];             // MTL files, textures, groups of the model (also those not in the list)
    wchar_t importFormat[8], importColors[16];
    COLORREF importColor;
    int filesW;                     // width of the list, for its columns
    int filesLeast;                 // rows the list shows at least (Char_Layout), 0 = no list
    HWND texturesLabel, textures, texturesBrowse, texturesClear;  // an OBJ only
    int texturesOn;                 // they are shown (Char_TexturesUpdate)
    HWND nameLabel, name, nameNote;
    HWND classLabel, cls, classHelp;
    HWND maskLabel, mask, maskHelp;
    HWND mapLabel, mapSwatch, mapPick, mapLike, mapHelp;
    int mapColorSet;                // 0 = like the template
    COLORREF mapColor;              // the chosen colour when mapColorSet
    COLORREF mapCustom[16];         // the custom colours of the colour dialog
    HWND sizeLabel, size, sizeValue, sizeNote, sizeHint, sizeFit, sizeCrash;
    HWND optionsLabel, repair, openParts, remesh, reduce, wheels, quality;
    HWND iconLabel, icon, iconBrowse, iconClear, iconCaption[CHAR_IMG_COUNT], iconImage[CHAR_IMG_COUNT];
    HWND iconFitLabel, iconFit, iconCorners, iconFrame;
    HWND voicesLabel, voices, voicesBrowse, voicesClear, voicesNote;
    HWND voiceList, voiceEventLabel, voiceEvent, voicePlay, voiceNorm, voiceRule, voiceEventsLabel, voiceEvents;
    // The voices of the last check (taken over from g_charJob when it ends).
    struct CharVoice *voiceRow;
    int voiceRowCount;
    int voiceKnown;                 // the last check reported the voices (@char voices)
    int voiceClips, voiceFilled;
    int voiceEventCount[CHAR_VOICE_EVENTS];
    int voiceSeq;                   // temp number of the --voice-preview files shown, 0 = none
    wchar_t voiceSel[CHAR_VOICE_NAME];  // the file chosen in the list, "" = none
    // Files given an event of their own (--voice), for the folder voiceSetDir:
    // event = what goes to rldpack, named = the event rldpack reported for the
    // file before (by its name; CHAR_VOICE_NONE for a file it leaves out).
    struct { wchar_t name[CHAR_VOICE_NAME]; int event, named; } voiceSet[CHAR_VOICE_SET_MAX];
    int voiceSetCount;
    wchar_t voiceSetDir[CHAR_VAL];
    unsigned char *voiceSound;      // the WAV playing (PlaySound, SND_MEMORY), Rs_Free
    int voiceFilling;               // the list is being filled: its changes are no choice
    int voiceListW;                 // width of the list, for its columns
    COLORREF voicesColor;
    HWND view, pose, viewNote;
    HWND outLabel, out, outBrowse, check, build, headline, msgs, raw, rawToggle, show;
    HWND reduceFit;                 // "Reduce to fit" below the options, only over the limit
    HWND cancel, progress;          // beside the headline while rldpack runs (Char_ProgressShow)
    int progressOn;                 // they are shown
    int progressPos;                // 0..1000, -1 = not known
    wchar_t progressText[64];       // what the bar says
    wchar_t slowText[48];           // "about N s" of the info reduce-slow of the running job, "" = none
    wchar_t buildOut[CHAR_VAL];     // the output of the running build: its <out>.part goes after a cancel
    int reduceFitOn;                // it is shown (on the tab Model)
    int qualityOn;                  // the note below the options has text (shown on the tab Model)
    HWND tabHead[CHAR_TABS], back, next, extrasSwitch[CHAR_EXTRAS_COUNT];
    HWND importNote, vcolorsLabel, vcolors, vcolorsHelp;  // the card Import of the tab Extras
    HWND nativeLabel, native, nativeHint, nativeHelp;    // its row "Native model" (preview feature, Char_NativeUpdate)
    HWND fallback, fallbackHint, nativeLine;  // tab Model, Rs_NativeForUsers only (Char_UserHidden)
    // The tab In-game look: Portrait | In the race (lookCard), and on the card
    // In the race the shadow and the exhaust (preview feature, open to everyone).
    HWND lookSwitch[CHAR_LOOK_CARDS], lookHint, lookNote;
    HWND shadowLabel, shadow, shadowHelp, exhaustLabel, exhaust, exhaustHelp;
    HWND pointLabel[2], point[2][3], pointPick[2];
    int lookCard;                   // CHAR_LOOK_*
    int shadowSet, exhaustSet;      // the author chose them (else they follow the wheels: Char_LookDefaults)
    int lookQuadKnown;              // the last check said the auto quad (@value shadow-quad)
    int lookQuad[4];                // x0 x1 z0 z1, 1/16 game units
    HWND upLabel, up, upHelp, forwardLabel, forward, forwardHelp, colorsLabel, colors, colorsHelp;
    int objOn;                      // the model is an OBJ: its own choices are shown (Char_ObjUpdate)
    int tab;                        // CHAR_TAB_*
    int extrasCard;                 // CHAR_EXTRAS_*
    int step[CHAR_TABS];            // CHAR_STEP_* of each head
    int tabErrors[CHAR_TABS], tabWarnings[CHAR_TABS];   // of the last run, per tab
    int *msgTab;                    // the tab of every entry of the message list, -1 = none
    int *msgCard;                   // and its card in the tab Extras, -1 = none
    int msgTabCount, msgTabCap;
    int modelRead;                  // the last run read the model in the field
    int compact;                    // the compact layout (Char_Layout, CHAR_FULL_W/H): notes on one line
    int stepShown;                  // the heads were drawn once (Char_TabsUpdate)

    // colours of the labels, for the report
    COLORREF headColor, nameColor, sizeColor, infoColor, viewColor, fitColor, qualityColor;
    wchar_t infoFull[CHAR_VAL];     // the model info before Char_FitLines

    // Stamps (Char_Stamp*) of the files the check shown read: stampRun of the
    // model, the icon and the voices folder when the running check started,
    // stampFields of those when the check shown started, stampImport of the
    // MTL and texture files it named (stampFiles, "a\0b\0\0", Rs_Free).
    unsigned long long stampRun, stampFields, stampImport;
    wchar_t *stampFiles;
    int stampKnown;                 // the stamps belong to the check shown
    // The results kept, [0] the newest (Char_Cache*): the command without its
    // temp files (Char_ArgsKey), the same without the name, driving style,
    // mask, minimap colour and output (modelKey), the raw output shown (the
    // command, then the lines of rldpack), that of the run that read the
    // model (modelRaw), the lines the result was read from (feed, one per
    // line), the output rldpack named, the exit code, the temp files and the
    // stamps of the files.
    struct {
        wchar_t *key, *modelKey, *raw, *modelRaw, *feed, *out, *stampFiles;
        int exitCode, seq;
        unsigned long long stampFields, stampImport;
    } cache[CHAR_CACHE];
    wchar_t *runKey, *runModelKey;  // the keys of the running check (Rs_Free)
    // The result kept that a running CHAR_JOB_META completes: copies of its
    // modelRaw, feed and out, its temp files.
    wchar_t *metaModelRaw, *metaFeed;
    int metaSeq;
    wchar_t *feedText;              // the lines of the result shown, as Char_Feed got them
    size_t feedLen, feedCap;

    int applying;                   // fields are being set: trigger no check
    int timer;                      // check waits for the timer
    int jobId, jobKind;             // running job, 0 = none
    int jobSeq;                     // temp files of the running check
    int seq;                        // last handed out temp number
    struct { int id, seq; } pending[CHAR_PENDING];   // ended checks, files still to delete
    int sizeNow;                    // slider position, CHAR_SIZE_MIN..CHAR_SIZE_MAX
    int rangeKnown, rangeLo, rangeHi;
    int rangeNone;                  // rldpack: no size fits this model
    wchar_t sizeWhy[2][512];        // reason of char-size: [0] below the range, [1] above it
    int sizeLockHigh;               // the slider went back to the upper bound
    int sizeLocked;                 // the slider was put back into the range
    int kartKnown;
    int kart[6];
    int retailKnown;
    int retail[6];
    int crashKnown;
    int crash[6];                   // tenths of game units
    int poseNow;
    int yawNow;
    int previewShown;               // the view shows a model
    int previewPoses;
    unsigned long previewTris[CHAR_POSES];
    // What the note below the preview says (Char_ViewNoteUpdate), from the
    // check shown: its file carries the native model (RsView_HasNative), the
    // OBJ names textures, the textures it did not find.
    int previewNative, previewTextured, previewMissing;
    wchar_t previewMissingNames[256];
    wchar_t previewModel[CHAR_VAL]; // the model of the preview shown
    // The note's sentences, the most important first; the layout shows as
    // many as fit (Char_ViewNoteFit).
    int viewPartCount;
    int viewFirst;                  // the sentence the compact layout shows first (the textures not found), -1 = in order
    wchar_t viewPart[CHAR_VIEW_PARTS][384];
    struct CharImage image[CHAR_IMG_COUNT];
    struct CharImage game;          // the second picture: both portraits on the race (Char_GameCompose)
    int gameRetail;                 // 1 = the template's portrait came from the game's data
    int checked;                    // last check gave "checked"
    int checkAfterBuild;            // a check was asked for while building
    wchar_t checkedPath[CHAR_VAL];
    wchar_t checkModel[CHAR_VAL];   // the model the running check was started with
    wchar_t built[CHAR_VAL];        // last built character
    long long builtBytes;
    wchar_t builtSha[80];
    wchar_t checkDefault[CHAR_VAL]; // the game's default output the last check used, "" = none
    int buildWheelsOff;             // what the running build passed, for its message
    int buildMask;                  // index into g_charMasks
    int runReduce;                  // the running job got Reduce to fit (Char_MakeArgs)
    int buildMapSet;                // the minimap colour the running build passed, if any
    COLORREF buildMapColor;
    int showRaw;
    wchar_t *rawText;               // all lines of the last run
    size_t rawLen, rawCap;
    int rawLines;
} g_char;

static struct CharJobData g_charJob;

static void Char_Layout(HWND page, int w, int h);
static int Char_Check(HWND page);
static void Char_ObjUpdate(HWND page);
static void Char_NativeResult(wchar_t **f, int n);
static void Char_VoiceSelShow(void);
static int Char_VoicesHeard(int *clips, int *events);
static void Char_VoiceColumns(int width);

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static void Char_Copy(wchar_t *dst, int cap, const wchar_t *src)
{
    size_t n;
    if (cap <= 0)
        return;
    if (!src)
        src = L"";
    n = wcslen(src);
    if (n >= (size_t)cap)
        n = (size_t)cap - 1;
    memmove(dst, src, n * sizeof(wchar_t));
    dst[n] = 0;
}

static void Char_Append(wchar_t *dst, int cap, const wchar_t *src)
{
    int n = (int)wcslen(dst);
    if (n < cap - 1)
        Char_Copy(dst + n, cap - n, src);
}

static const wchar_t *Char_Field(wchar_t **fields, int count, int i)
{
    return (i < count && fields[i]) ? fields[i] : L"";
}

static int Char_IsShown(HWND h)
{
    return (GetWindowLongPtrW(h, GWL_STYLE) & WS_VISIBLE) != 0;
}

// Kilobytes of 1000 bytes, rounded; small files in bytes.
static void Char_SizeText(wchar_t *out, int cap, long long bytes)
{
    if (bytes < 1000)
        swprintf(out, cap, L"%lld bytes", bytes);
    else
        swprintf(out, cap, L"%lld KB", (bytes + 500) / 1000);
}

static const wchar_t *Char_ColorName(COLORREF c)
{
    if (c == RS_COL_OK) return L"green";
    if (c == RS_COL_ERROR) return L"red";
    if (c == RS_COL_WARNING) return L"amber";
    if (c == RS_COL_NOTE) return L"blue";
    if (c == RS_COL_MUTED) return L"grey";
    return L"normal";
}

static int Char_TextHeight(HWND label, int width, int maxLines);
static void Char_Relayout(HWND page);

// The notes below the fields whose text changes. They wrap instead of ending
// in "...", so a narrow page loses none of their words.
static int Char_IsNote(HWND label)
{
    return label && (label == g_char.nameNote || label == g_char.sizeFit || label == g_char.sizeNote ||
                     label == g_char.viewNote || label == g_char.voicesNote || label == g_char.voiceRule ||
                     label == g_char.modelImport);
}

// Height of a note (or the headline) at this width: its lines, at least one,
// at most CHAR_NOTE_LINES.
static int Char_NoteHeight(HWND label, int width)
{
    int least = label == g_char.headline ? Rs_Px(22) : Rs_Px(18);
    int h = Char_TextHeight(label, width, label == g_char.viewNote ? CHAR_VIEW_NOTE_LINES : CHAR_NOTE_LINES);
    return h > least ? h : least;
}

static void Char_SetLabel(HWND label, const wchar_t *text, COLORREF color, COLORREF *store)
{
    Rs_SetText(label, text);
    Rs_SetTextColor(label, color);
    if (store)
        *store = color;
    // A note that now needs more or fewer lines: the page is laid out again.
    if (Char_IsNote(label)) {
        RECT rc;
        GetWindowRect(label, &rc);
        if (rc.right > rc.left && Char_NoteHeight(label, rc.right - rc.left) != rc.bottom - rc.top)
            Char_Relayout(GetParent(label));
    }
}

// The headline is one line, cut with "..." where it is longer (SS_ENDELLIPSIS:
// the control keeps the whole text); the whole text is also its tooltip.
static void Char_Headline(const wchar_t *text, COLORREF color)
{
    Char_SetLabel(g_char.headline, text, color, &g_char.headColor);
    Rs_SetTip(g_char.headline, text);
}

// Text the message list shows as long as it is empty.
static void Char_EmptyText(const wchar_t *text)
{
    Rs_SetText(g_char.msgs, text);
}

// Spaces and quotation marks away, no slash at the end.
static void Char_CleanPath(wchar_t *out, int cap, const wchar_t *in)
{
    size_t n;
    if (!in)
        in = L"";
    while (*in == L' ' || *in == L'\t' || *in == L'"')
        in++;
    Char_Copy(out, cap, in);
    n = wcslen(out);
    while (n > 0 && (out[n - 1] == L' ' || out[n - 1] == L'\t' || out[n - 1] == L'"'))
        out[--n] = 0;
    while (n > 3 && (out[n - 1] == L'\\' || out[n - 1] == L'/'))
        out[--n] = 0;
}

// Text of an edit field without quotation marks and spaces at the edges.
static void Char_FieldPath(HWND edit, wchar_t *out, int cap)
{
    wchar_t *text = Rs_GetText(edit);
    Char_CleanPath(out, cap, text);
    Rs_Free(text);
}

static int Char_EndsWith(const wchar_t *path, const wchar_t *ending)
{
    size_t n = wcslen(path), e = wcslen(ending);
    return n >= e && _wcsicmp(path + n - e, ending) == 0;
}

// Up to max numbers out of the text (separated by anything else), times
// scale, rounded to whole numbers and held to -32768..32767 times scale.
static int Char_NumbersScaled(const wchar_t *s, int *out, int max, int scale)
{
    int n = 0;
    while (*s && n < max) {
        wchar_t *end;
        double v;
        if (!((*s >= L'0' && *s <= L'9') || ((*s == L'-' || *s == L'+') && s[1] >= L'0' && s[1] <= L'9'))) {
            s++;
            continue;
        }
        v = wcstod(s, &end);
        if (end == s) {
            s++;
            continue;
        }
        v *= scale;
        if (v > 32767.0 * scale)
            v = 32767.0 * scale;
        if (v < -32768.0 * scale)
            v = -32768.0 * scale;
        out[n++] = (int)(v < 0 ? v - 0.5 : v + 0.5);
        s = end;
    }
    return n;
}

// Up to max whole numbers out of the text, rounded and held to -32768..32767.
static int Char_Numbers(const wchar_t *s, int *out, int max)
{
    return Char_NumbersScaled(s, out, max, 1);
}

// A whole number with a space between groups of three digits: "27 636".
static void Char_Grouped(wchar_t *out, int cap, long v)
{
    wchar_t digits[24];
    int n, i, o = 0;
    swprintf(digits, 24, L"%ld", v < 0 ? -v : v);
    n = (int)wcslen(digits);
    if (v < 0 && o < cap - 1)
        out[o++] = L'-';
    for (i = 0; i < n && o < cap - 1; i++) {
        if (i > 0 && (n - i) % 3 == 0 && o < cap - 1)
            out[o++] = L' ';
        if (o < cap - 1)
            out[o++] = digits[i];
    }
    out[o] = 0;
}

static int Char_IsChecked(HWND box)
{
    return SendMessageW(box, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static void Char_SetChecked(HWND box, int on)
{
    SendMessageW(box, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
}

// Height the text of a label takes wrapped at width, at most maxLines lines
// (the rest is cut off at a line boundary).
static int Char_TextHeight(HWND label, int width, int maxLines)
{
    wchar_t *text = Rs_GetText(label);
    HDC dc = GetDC(label);
    HFONT font = (HFONT)SendMessageW(label, WM_GETFONT, 0, 0);
    HGDIOBJ old;
    TEXTMETRICW tm;
    RECT rc;
    int h = Rs_Px(18), lineH;

    if (dc) {
        old = SelectObject(dc, font ? font : Rs_Font(RS_FONT_SMALL));
        GetTextMetricsW(dc, &tm);
        lineH = tm.tmHeight;
        rc.left = 0;
        rc.top = 0;
        rc.right = width;
        rc.bottom = 0;
        DrawTextW(dc, text, -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EXPANDTABS);
        h = rc.bottom;
        // The compact layout: one line, cut with "...", the whole text as
        // the tooltip (Rs_LabelOneLine) - no word is lost, it is a hover away.
        if (g_char.compact && lineH > 0 && label != g_char.headline) {
            Rs_LabelOneLine(label, h > lineH);
            if (h > lineH)
                h = lineH;
        } else {
            Rs_LabelOneLine(label, 0);
        }
        if (lineH > 0 && h > lineH * maxLines)
            h = lineH * maxLines;
        SelectObject(dc, old);
        ReleaseDC(label, dc);
    }
    Rs_Free(text);
    return h;
}

// Puts full into label so that it takes at most maxLines lines at width: as
// it is when it fits, else cut after a word and ended with "..." - what does
// not fit is cut off visibly, never silently.
static void Char_FitLines(HWND label, const wchar_t *full, int width, int maxLines)
{
    HDC dc = GetDC(label);
    HFONT font = (HFONT)SendMessageW(label, WM_GETFONT, 0, 0);
    HGDIOBJ old;
    TEXTMETRICW tm;
    wchar_t buf[CHAR_VAL + 4];
    size_t n = wcslen(full);

    Char_Copy(buf, CHAR_VAL, full);
    if (dc && width > 0) {
        old = SelectObject(dc, font ? font : Rs_Font(RS_FONT_SMALL));
        GetTextMetricsW(dc, &tm);
        for (;;) {
            RECT rc = { 0, 0, width, 0 };
            DrawTextW(dc, buf, -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EXPANDTABS);
            if (rc.bottom <= tm.tmHeight * maxLines)
                break;
            // One word less (and the spaces and dashes before it).
            while (n > 0 && full[n - 1] != L' ')
                n--;
            while (n > 0 && (full[n - 1] == L' ' || full[n - 1] == L'-' || full[n - 1] == L','))
                n--;
            if (n == 0)
                break;
            memcpy(buf, full, n * sizeof(wchar_t));
            wcscpy(buf + n, L"...");
        }
        SelectObject(dc, old);
    }
    if (dc)
        ReleaseDC(label, dc);
    Rs_SetText(label, buf);
}

// Width a check box needs for its text in its font: box, gap, text, a margin.
static int Char_CheckWidth(HWND box)
{
    wchar_t text[128];
    HDC dc = GetDC(box);
    HFONT font = (HFONT)SendMessageW(box, WM_GETFONT, 0, 0);
    HGDIOBJ old;
    SIZE ext;
    int n = GetWindowTextW(box, text, 128);

    ext.cx = n * Rs_Px(8);
    if (dc) {
        old = SelectObject(dc, font ? font : Rs_Font(RS_FONT_BODY));
        GetTextExtentPoint32W(dc, text, n, &ext);
        SelectObject(dc, old);
        ReleaseDC(box, dc);
    }
    return Rs_Px(13 + 4 + 6) + ext.cx;
}

// Width of a text in the font of control h.
static int Char_TextWidth(HWND h, const wchar_t *text)
{
    HDC dc = GetDC(h);
    HFONT font = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0);
    HGDIOBJ old;
    SIZE ext;
    int n = (int)wcslen(text);

    ext.cx = n * Rs_Px(8);
    if (dc) {
        old = SelectObject(dc, font ? font : Rs_Font(RS_FONT_BODY));
        GetTextExtentPoint32W(dc, text, n, &ext);
        SelectObject(dc, old);
        ReleaseDC(h, dc);
    }
    return ext.cx;
}

// Width of the longest entry of a combo box, without margins and arrow.
static int Char_ComboTextWidth(HWND combo)
{
    wchar_t text[256];
    int i, n = (int)SendMessageW(combo, CB_GETCOUNT, 0, 0), most = 0;

    for (i = 0; i < n; i++) {
        if (SendMessageW(combo, CB_GETLBTEXTLEN, (WPARAM)i, 0) >= 256)
            continue;
        SendMessageW(combo, CB_GETLBTEXT, (WPARAM)i, (LPARAM)text);
        {
            int tw = Char_TextWidth(combo, text);
            if (tw > most)
                most = tw;
        }
    }
    return most;
}

// The menu font of the game: A-Z, 0-9, space and ! % ' + , - . / : < = > ? _
// (RldChar_NameCharAllowed in include/rldchar.inc). Only for typing - rldpack
// checks the name.
static int Char_NameCharAllowed(wchar_t c)
{
    if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9'))
        return 1;
    return c != 0 && wcschr(L" !%'+,-./:<=>?_", c) != NULL;
}

// ---------------------------------------------------------------------------
// Pictures: icon PNG as decoded and as converted (BMP files of --icon-preview)
// ---------------------------------------------------------------------------

static void Char_ImageFree(struct CharImage *img)
{
    Rs_Free(img->px);
    img->px = NULL;
    img->w = 0;
    img->h = 0;
}

static unsigned Char_Le16(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned long Char_Le32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) |
           ((unsigned long)p[3] << 24);
}

// A channel through a bit mask, scaled to 0..255. mask 0 = def.
static unsigned Char_MaskChannel(unsigned long v, unsigned long mask, unsigned def)
{
    int shift = 0, bits = 0;
    unsigned long m;
    if (!mask)
        return def;
    while (!((mask >> shift) & 1))
        shift++;
    for (m = mask >> shift; m & 1; m >>= 1)
        bits++;
    v = (v & mask) >> shift;
    if (bits >= 8)
        return (unsigned)(v >> (bits - 8));
    return (unsigned)(v * 255 / ((1ul << bits) - 1));
}

// Reads a BMP (1/4/8 bit with palette, 16/24/32 bit, BI_RGB or bit fields,
// top-down or bottom-up). 32-bit pictures keep their alpha as written. 1 = read.
static int Char_LoadBmp(const wchar_t *path, struct CharImage *img)
{
    FILE *f = _wfopen(path, L"rb");
    unsigned char *data = NULL, *px = NULL;
    long size;
    unsigned long off, hsz, comp, used, masks[4] = { 0, 0, 0, 0 };
    long w, h;
    int bpp, topDown, y, x, ok = 0;
    size_t stride, palAt, palCount = 0;

    memset(img, 0, sizeof(*img));
    if (!f)
        return 0;
    if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) >= 54 && size <= 64L * 1024 * 1024 &&
        fseek(f, 0, SEEK_SET) == 0) {
        data = Rs_Alloc((size_t)size);
        if (fread(data, 1, (size_t)size, f) != (size_t)size) {
            Rs_Free(data);
            data = NULL;
        }
    }
    fclose(f);
    if (!data)
        return 0;

    if (data[0] != 'B' || data[1] != 'M')
        goto done;
    off = Char_Le32(data + 10);
    hsz = Char_Le32(data + 14);
    if (hsz < 40 || hsz > (unsigned long)size - 14)
        goto done;      // the header must lie inside the file (it covers the masks at 54..69)
    w = (long)Char_Le32(data + 18);
    h = (long)Char_Le32(data + 22);
    bpp = (int)Char_Le16(data + 28);
    comp = Char_Le32(data + 30);
    used = Char_Le32(data + 46);
    if (h < -CHAR_IMAGE_MAX)
        goto done;      // also keeps -h defined
    topDown = h < 0;
    if (h < 0)
        h = -h;
    if (w <= 0 || h <= 0 || w > CHAR_IMAGE_MAX || h > CHAR_IMAGE_MAX || (long long)w * h > 16LL * 1024 * 1024)
        goto done;
    if (comp != 0 && comp != 3 && comp != 6)
        goto done;      // compressed: not written by rldpack
    palAt = 14 + hsz;
    if (comp == 3 || comp == 6) {
        if (hsz >= 52) {
            masks[0] = Char_Le32(data + 54);
            masks[1] = Char_Le32(data + 58);
            masks[2] = Char_Le32(data + 62);
            if (hsz >= 56)
                masks[3] = Char_Le32(data + 66);
        } else {
            if (palAt + (comp == 6 ? 16u : 12u) > (size_t)size)
                goto done;
            masks[0] = Char_Le32(data + palAt);
            masks[1] = Char_Le32(data + palAt + 4);
            masks[2] = Char_Le32(data + palAt + 8);
            if (comp == 6)
                masks[3] = Char_Le32(data + palAt + 12);
            palAt += comp == 6 ? 16 : 12;
        }
    } else if (bpp == 16) {
        masks[0] = 0x7C00;
        masks[1] = 0x03E0;
        masks[2] = 0x001F;
    }
    if (bpp == 1 || bpp == 4 || bpp == 8) {
        palCount = used ? used : (1u << bpp);
        if (palCount > 256 || palAt + palCount * 4 > (size_t)size)
            goto done;
    } else if (bpp != 16 && bpp != 24 && bpp != 32) {
        goto done;
    }
    stride = (((size_t)w * (size_t)bpp + 31) / 32) * 4;
    if (off > (unsigned long)size || stride * (size_t)h > (size_t)size - off)
        goto done;

    px = Rs_Alloc((size_t)w * (size_t)h * 4);
    for (y = 0; y < h; y++) {
        const unsigned char *row = data + off + stride * (size_t)(topDown ? y : h - 1 - y);
        unsigned char *dst = px + (size_t)y * (size_t)w * 4;
        for (x = 0; x < w; x++, dst += 4) {
            unsigned b, g, r, a = 255;
            if (bpp <= 8) {
                unsigned bit = (unsigned)x * (unsigned)bpp;
                unsigned idx = (row[bit >> 3] >> (8 - bpp - (bit & 7))) & ((1u << bpp) - 1);
                const unsigned char *p = data + palAt + (size_t)(idx < palCount ? idx : 0) * 4;
                b = p[0];
                g = p[1];
                r = p[2];
            } else if (bpp == 24) {
                b = row[x * 3];
                g = row[x * 3 + 1];
                r = row[x * 3 + 2];
            } else {
                unsigned long v = bpp == 16 ? Char_Le16(row + x * 2) : Char_Le32(row + x * 4);
                if (bpp == 32 && comp == 0) {
                    b = v & 0xFF;
                    g = (v >> 8) & 0xFF;
                    r = (v >> 16) & 0xFF;
                    a = (v >> 24) & 0xFF;
                } else {
                    r = Char_MaskChannel(v, masks[0], 0);
                    g = Char_MaskChannel(v, masks[1], 0);
                    b = Char_MaskChannel(v, masks[2], 0);
                    a = Char_MaskChannel(v, masks[3], 255);
                }
            }
            dst[0] = (unsigned char)b;
            dst[1] = (unsigned char)g;
            dst[2] = (unsigned char)r;
            dst[3] = (unsigned char)a;
        }
    }
    // rldpack writes a valid alpha channel: a fully transparent icon
    // (icon-empty) shows as the checkerboard, not as a black block.
    img->w = (int)w;
    img->h = (int)h;
    img->px = px;
    px = NULL;
    ok = 1;
done:
    Rs_Free(px);
    Rs_Free(data);
    return ok;
}

// The picture box: the picture as large as fits, with whole steps when it is
// smaller than the box (pixels stay sharp), on a checkerboard where it is
// transparent. Without a picture a grey text (window text).
static void Char_ImagePaint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    const struct CharImage *img = (const struct CharImage *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    RECT rc;
    HBRUSH br;

    GetClientRect(hwnd, &rc);
    br = CreateSolidBrush(RS_COL_PAGE);
    FillRect(dc, &rc, br);
    DeleteObject(br);
    if (img && img->px && img->w > 0 && img->h > 0) {
        int bw = rc.right - Rs_Px(8), bh = rc.bottom - Rs_Px(8);
        int dw, dh, cell, x, y;
        unsigned char *buf;
        BITMAPINFO bi;
        if (bw * img->h <= bh * img->w) {
            dw = bw;
            dh = (int)((long long)bw * img->h / img->w);
        } else {
            dh = bh;
            dw = (int)((long long)bh * img->w / img->h);
        }
        if (dw >= img->w && dh >= img->h) {
            int k = dw / img->w;
            if (dh / img->h < k)
                k = dh / img->h;
            dw = img->w * k;
            dh = img->h * k;
        }
        if (dw < 1)
            dw = 1;
        if (dh < 1)
            dh = 1;
        // Checker cells of about 8 screen pixels, counted in picture pixels.
        cell = img->w * Rs_Px(8) / dw;
        if (cell < 1)
            cell = 1;
        buf = Rs_Alloc((size_t)img->w * (size_t)img->h * 4);
        for (y = 0; y < img->h; y++) {
            for (x = 0; x < img->w; x++) {
                const unsigned char *s = img->px + ((size_t)y * img->w + x) * 4;
                unsigned char *d = buf + ((size_t)y * img->w + x) * 4;
                unsigned bg = (((x / cell) + (y / cell)) & 1) ? 204 : 240;
                unsigned a = s[3];
                d[0] = (unsigned char)((s[0] * a + bg * (255 - a) + 127) / 255);
                d[1] = (unsigned char)((s[1] * a + bg * (255 - a) + 127) / 255);
                d[2] = (unsigned char)((s[2] * a + bg * (255 - a) + 127) / 255);
                d[3] = 0;
            }
        }
        memset(&bi, 0, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = img->w;
        bi.bmiHeader.biHeight = -img->h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchDIBits(dc, (rc.right - dw) / 2, (rc.bottom - dh) / 2, dw, dh, 0, 0, img->w, img->h,
                      buf, &bi, DIB_RGB_COLORS, SRCCOPY);
        Rs_Free(buf);
    } else {
        wchar_t text[128];
        HGDIOBJ old = SelectObject(dc, Rs_Font(RS_FONT_SMALL));
        GetWindowTextW(hwnd, text, 128);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RS_COL_MUTED);
        DrawTextW(dc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, old);
    }
    br = CreateSolidBrush(RS_COL_BORDER);
    FrameRect(dc, &rc, br);
    DeleteObject(br);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK Char_ImageProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Char_ImagePaint(hwnd);
        return 0;
    case WM_SETTEXT: {
        LRESULT r = DefWindowProcW(hwnd, msg, wParam, lParam);
        InvalidateRect(hwnd, NULL, FALSE);
        return r;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// The swatch of the minimap colour: the chosen colour, or the template's
// grey. A click chooses, like the button "Choose...".
static LRESULT CALLBACK Char_SwatchProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        HBRUSH br;
        GetClientRect(hwnd, &rc);
        br = CreateSolidBrush(g_char.mapColorSet ? g_char.mapColor : CHAR_TEMPLATE_MAP_COLOR);
        FillRect(dc, &rc, br);
        DeleteObject(br);
        br = CreateSolidBrush(RS_COL_BORDER);
        FrameRect(dc, &rc, br);
        DeleteObject(br);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_LBUTTONUP) {
        HWND page = GetParent(hwnd);
        SendMessageW(page, WM_COMMAND, MAKEWPARAM(CHAR_ID_MAPCOLOR_PICK, BN_CLICKED), (LPARAM)g_char.mapPick);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Shows the minimap colour: swatch, "Like the template" only while a colour is set.
static void Char_MapColorShow(void)
{
    InvalidateRect(g_char.mapSwatch, NULL, TRUE);
    EnableWindow(g_char.mapLike, g_char.mapColorSet);
}

// Sets the minimap colour (set = 0: like the template) and checks again.
static void Char_MapColorSet(HWND page, int set, COLORREF color);

static HWND Char_ImageBox(HWND page, int id, struct CharImage *img)
{
    HWND h = CreateWindowExW(0, CHAR_IMAGE_CLASS, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, page,
                             (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    if (h)
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)img);
    return h;
}

static void Char_ImagesClear(void)
{
    int i;
    for (i = 0; i < CHAR_IMG_COUNT; i++) {
        Char_ImageFree(&g_char.image[i]);
        InvalidateRect(g_char.iconImage[i], NULL, FALSE);
    }
    Char_ImageFree(&g_char.game);
    g_char.gameRetail = 0;
}

// ---------------------------------------------------------------------------
// Temp files of a check: <Rs_TempDir>\char-<process>-<number>... (Rs_TempDir:
// %TEMP%\Reload Studio, with --settings the folder of the settings file)
// ---------------------------------------------------------------------------

static void Char_TempBase(wchar_t *out, int cap, int seq)
{
    wchar_t dir[CHAR_VAL];
    wchar_t name[64];

    Rs_TempDir(dir, CHAR_VAL);   // created there
    swprintf(name, 64, L"char-%lu-%d", (unsigned long)GetCurrentProcessId(), seq);
    Rs_PathJoin(out, cap, dir, name);
}

// kind: L".rldpv" (model preview), L"-original.bmp", L"-icon.bmp",
// L"-retail.bmp", L"" (the --icon-preview prefix).
static void Char_TempPath(wchar_t *out, int cap, int seq, const wchar_t *kind)
{
    Char_TempBase(out, cap, seq);
    Char_Append(out, cap, kind);
}

// Every file a check leaves: the --preview file and those of --icon-preview
// (the template's portrait only when rldpack found the game's data).
#define CHAR_TEMP_KINDS 4
static const wchar_t *const g_charTempKinds[CHAR_TEMP_KINDS] = { L".rldpv", L"-original.bmp", L"-icon.bmp",
                                                                 L"-retail.bmp" };

// 1 if name is exactly one of the page's own temp files,
// "char-<digits>-<digits><kind>" with a kind of g_charTempKinds or
// "-voice-<digits>.wav" (--voice-preview); *pid = the first number, *seq the
// second. Nothing else in the folder is the page's to delete - with
// --settings it is a folder of the user.
static int Char_TempOwnName(const wchar_t *name, unsigned long *pid, int *seq)
{
    const wchar_t *p = name + 5;
    const wchar_t *digits;
    int i;

    if (wcsncmp(name, L"char-", 5) != 0)
        return 0;
    digits = p;
    while (*p >= L'0' && *p <= L'9')
        p++;
    if (p == digits || p - digits > 10 || *p != L'-')
        return 0;
    *pid = wcstoul(digits, NULL, 10);
    digits = ++p;
    while (*p >= L'0' && *p <= L'9')
        p++;
    if (p == digits || p - digits > 10)
        return 0;
    *seq = (int)wcstol(digits, NULL, 10);
    for (i = 0; i < CHAR_TEMP_KINDS; i++)
        if (wcscmp(p, g_charTempKinds[i]) == 0)
            return 1;
    if (wcsncmp(p, L"-voice-", 7) != 0)
        return 0;
    p += 7;
    digits = p;
    while (*p >= L'0' && *p <= L'9')
        p++;
    return p != digits && p - digits <= 10 && wcscmp(p, L".wav") == 0;
}

// The --voice-preview files of check seq: "<prefix>-voice-<n>.wav".
static void Char_TempDeleteVoices(int seq)
{
    wchar_t pattern[CHAR_VAL];
    wchar_t dir[CHAR_VAL];
    wchar_t path[CHAR_VAL];
    WIN32_FIND_DATAW fd;
    HANDLE find;

    if (seq <= 0)
        return;
    Char_TempPath(pattern, CHAR_VAL, seq, L"-voice-*.wav");
    Rs_PathDir(dir, CHAR_VAL, pattern);
    find = FindFirstFileW(pattern, &fd);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do {
        unsigned long pid = 0;
        int own = 0;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && Char_TempOwnName(fd.cFileName, &pid, &own) &&
            pid == GetCurrentProcessId() && own == seq) {
            Rs_PathJoin(path, CHAR_VAL, dir, fd.cFileName);
            DeleteFileW(path);
        }
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

// keepVoices = 1: the --voice-preview files stay (the list plays them).
static void Char_TempDeleteKeep(int seq, int keepVoices)
{
    wchar_t path[CHAR_VAL];
    int i;
    if (seq <= 0)
        return;
    for (i = 0; i < CHAR_TEMP_KINDS; i++) {
        Char_TempPath(path, CHAR_VAL, seq, g_charTempKinds[i]);
        DeleteFileW(path);
    }
    if (!keepVoices)
        Char_TempDeleteVoices(seq);
}

static void Char_TempDelete(int seq)
{
    Char_TempDeleteKeep(seq, 0);
}

// An outdated check is ended; its files are deleted when it has gone.
static void Char_Abandon(void)
{
    int i;
    if (!g_char.jobId)
        return;
    if (g_char.jobKind == CHAR_JOB_CHECK || g_char.jobKind == CHAR_JOB_META) {
        Rs_KillJob(g_char.jobId);
        for (i = 0; i < CHAR_PENDING; i++)
            if (!g_char.pending[i].id)
                break;
        if (i == CHAR_PENDING) {
            Char_TempDelete(g_char.pending[0].seq);
            memmove(&g_char.pending[0], &g_char.pending[1], sizeof(g_char.pending[0]) * (CHAR_PENDING - 1));
            i = CHAR_PENDING - 1;
        }
        g_char.pending[i].id = g_char.jobId;
        g_char.pending[i].seq = g_char.jobSeq;
    }
    g_char.jobId = 0;
    g_char.jobKind = CHAR_JOB_NONE;
    g_char.jobSeq = 0;
    Rs_Free(g_char.runKey);
    g_char.runKey = NULL;
    Rs_Free(g_char.runModelKey);
    g_char.runModelKey = NULL;
    Rs_Free(g_char.metaModelRaw);
    Rs_Free(g_char.metaFeed);
    g_char.metaModelRaw = g_char.metaFeed = NULL;
    g_char.metaSeq = 0;
}

// Leftovers of earlier runs: the page's own files "char-<pid>-<number><kind>"
// (Char_TempOwnName) of processes that are no longer running (a check ended
// on closing may still have held its files). Files of running Reload Studios
// and every other file stay.
static void Char_TempSweep(void)
{
    wchar_t pattern[CHAR_VAL];
    wchar_t dir[CHAR_VAL];
    wchar_t path[CHAR_VAL];
    WIN32_FIND_DATAW fd;
    HANDLE find;

    Char_TempBase(pattern, CHAR_VAL, 0);
    Rs_PathDir(dir, CHAR_VAL, pattern);
    Rs_PathJoin(pattern, CHAR_VAL, dir, L"char-*");
    find = FindFirstFileW(pattern, &fd);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do {
        unsigned long pid = 0;
        int seq = 0;
        HANDLE proc;
        int alive = 0;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        if (!Char_TempOwnName(fd.cFileName, &pid, &seq))
            continue;
        if (pid == 0 || pid == GetCurrentProcessId())
            continue;
        proc = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
        if (proc) {
            alive = WaitForSingleObject(proc, 0) == WAIT_TIMEOUT;
            CloseHandle(proc);
        } else if (GetLastError() == ERROR_ACCESS_DENIED) {
            alive = 1;      // there, but not ours to ask
        }
        if (alive)
            continue;
        Rs_PathJoin(path, CHAR_VAL, dir, fd.cFileName);
        DeleteFileW(path);
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

static void Char_PendingDone(int id)
{
    int i;
    for (i = 0; i < CHAR_PENDING; i++) {
        if (g_char.pending[i].id == id) {
            Char_TempDelete(g_char.pending[i].seq);
            g_char.pending[i].id = 0;
            g_char.pending[i].seq = 0;
        }
    }
}

// ---------------------------------------------------------------------------
// Results kept (CHAR_CACHE): a kept result owns the temp files of its check
// until it is dropped.
// ---------------------------------------------------------------------------

// 1 if a kept result (other than the one at skip) has the temp files of seq.
static int Char_CacheHasSeq(int seq, int skip)
{
    int i;
    for (i = 0; i < CHAR_CACHE; i++)
        if (i != skip && g_char.cache[i].key && g_char.cache[i].seq == seq)
            return 1;
    return 0;
}

// Drops the result at i; its temp files go unless another result has them
// (the voice previews of the list shown stay - Char_ApplyVoices deletes them
// when the list changes).
static void Char_CacheDrop(int i)
{
    int seq = g_char.cache[i].seq;
    int had = g_char.cache[i].key != NULL;
    Rs_Free(g_char.cache[i].key);
    Rs_Free(g_char.cache[i].modelKey);
    Rs_Free(g_char.cache[i].raw);
    Rs_Free(g_char.cache[i].modelRaw);
    Rs_Free(g_char.cache[i].feed);
    Rs_Free(g_char.cache[i].out);
    Rs_Free(g_char.cache[i].stampFiles);
    if (had && seq > 0 && !Char_CacheHasSeq(seq, i))
        Char_TempDeleteKeep(seq, seq == g_char.voiceSeq);
    memmove(&g_char.cache[i], &g_char.cache[i + 1], sizeof(g_char.cache[0]) * (size_t)(CHAR_CACHE - 1 - i));
    memset(&g_char.cache[CHAR_CACHE - 1], 0, sizeof(g_char.cache[0]));
}

// Moves the result at i to the front (the newest).
static void Char_CacheTouch(int i)
{
    if (i > 0) {
        unsigned char keep[sizeof(g_char.cache[0])];
        memcpy(keep, &g_char.cache[i], sizeof(keep));
        memmove(&g_char.cache[1], &g_char.cache[0], sizeof(g_char.cache[0]) * (size_t)i);
        memcpy(&g_char.cache[0], keep, sizeof(keep));
    }
}

// A copy of a list "a\0b\0\0" (NULL = none).
static wchar_t *Char_ListDup(const wchar_t *list)
{
    const wchar_t *p = list;
    wchar_t *copy;
    size_t n;
    if (!list)
        return NULL;
    while (*p)
        p += wcslen(p) + 1;
    n = (size_t)(p - list) + 1;
    copy = Rs_Alloc(n * sizeof(wchar_t));
    memcpy(copy, list, n * sizeof(wchar_t));
    return copy;
}

// ---------------------------------------------------------------------------
// The pipe from rldpack
// ---------------------------------------------------------------------------

static void Char_VoicesFree(struct CharVoice *v, int count)
{
    int i;
    for (i = 0; i < count; i++)
        Rs_Free(v[i].preview);
    Rs_Free(v);
}

static void Char_JobReset(void)
{
    int i;
    for (i = 0; i < g_charJob.msgCount; i++) {
        Rs_Free(g_charJob.msgs[i].code);
        Rs_Free(g_charJob.msgs[i].text);
        Rs_Free(g_charJob.msgs[i].detail);
    }
    Rs_Free(g_charJob.msgs);
    Char_VoicesFree(g_charJob.voice, g_charJob.voiceCount);
    Rs_Free(g_charJob.import);
    memset(&g_charJob, 0, sizeof(g_charJob));
    g_char.rawLen = 0;
    g_char.rawLines = 0;
    if (g_char.rawText)
        g_char.rawText[0] = 0;
    g_char.feedLen = 0;
    if (g_char.feedText)
        g_char.feedText[0] = 0;
}

static void Char_JobMsg(int severity, const wchar_t *code, const wchar_t *text, const wchar_t *detail)
{
    struct CharJobData *j = &g_charJob;
    if (j->msgCount == j->msgCap) {
        int cap = j->msgCap ? j->msgCap * 2 : 16;
        struct CharMsg *m = Rs_Alloc((size_t)cap * sizeof(*m));
        if (j->msgCount)
            memcpy(m, j->msgs, (size_t)j->msgCount * sizeof(*m));
        Rs_Free(j->msgs);
        j->msgs = m;
        j->msgCap = cap;
    }
    j->msgs[j->msgCount].severity = severity;
    j->msgs[j->msgCount].code = Rs_Dup(code);
    j->msgs[j->msgCount].text = Rs_Dup(text);
    j->msgs[j->msgCount].detail = Rs_Dup(detail);
    j->msgCount++;
}

static void Char_RawAppend(const wchar_t *line)
{
    size_t n = wcslen(line);
    size_t need = g_char.rawLen + n + 3;
    if (need > g_char.rawCap) {
        size_t cap = g_char.rawCap ? g_char.rawCap : 4096;
        wchar_t *p;
        while (cap < need)
            cap *= 2;
        p = Rs_Alloc(cap * sizeof(wchar_t));
        if (g_char.rawLen)
            memcpy(p, g_char.rawText, g_char.rawLen * sizeof(wchar_t));
        Rs_Free(g_char.rawText);
        g_char.rawText = p;
        g_char.rawCap = cap;
    }
    if (g_char.rawLen) {
        g_char.rawText[g_char.rawLen++] = L'\r';
        g_char.rawText[g_char.rawLen++] = L'\n';
    }
    memcpy(g_char.rawText + g_char.rawLen, line, n * sizeof(wchar_t));
    g_char.rawLen += n;
    g_char.rawText[g_char.rawLen] = 0;
    g_char.rawLines++;
}

static void Char_RawRefresh(void)
{
    Rs_SetText(g_char.raw, g_char.rawText ? g_char.rawText : L"");
}

// The value fields from index 2 on as one text "a b c" - numbers that come as
// separate fields or in one field read the same.
static void Char_JoinFields(wchar_t **f, int n, wchar_t *out, int cap)
{
    int i;
    out[0] = 0;
    for (i = 2; i < n; i++) {
        if (out[0])
            Char_Append(out, cap, L" ");
        Char_Append(out, cap, Char_Field(f, n, i));
    }
}

// Index into g_charVoiceEvents of an event word of rldpack ("-" and "none" =
// CHAR_VOICE_NONE); -1 = not an event.
static int Char_VoiceEventIndex(const wchar_t *word)
{
    int e;
    if (wcscmp(word, L"-") == 0)
        return CHAR_VOICE_NONE;
    for (e = 0; e <= CHAR_VOICE_NONE; e++)
        if (_wcsicmp(word, g_charVoiceEvents[e].word) == 0)
            return e;
    return -1;
}

// @voice <name> <state> <event|-> <ms> <bytes> <rate> <channels> <peak per mille> <preview|->
static void Char_VoiceLine(wchar_t **f, int n)
{
    struct CharJobData *j = &g_charJob;
    struct CharVoice *v;
    const wchar_t *field;
    int e;

    if (!Char_Field(f, n, 1)[0])
        return;
    if (j->voiceCount == j->voiceCap) {
        int cap = j->voiceCap ? j->voiceCap * 2 : 32;
        struct CharVoice *grown = Rs_Alloc((size_t)cap * sizeof(*grown));
        if (j->voiceCount)
            memcpy(grown, j->voice, (size_t)j->voiceCount * sizeof(*grown));
        Rs_Free(j->voice);
        j->voice = grown;
        j->voiceCap = cap;
    }
    v = &j->voice[j->voiceCount++];
    memset(v, 0, sizeof(*v));
    Char_Copy(v->name, CHAR_VOICE_NAME, Char_Field(f, n, 1));
    Char_Copy(v->state, 16, Char_Field(f, n, 2));
    e = Char_VoiceEventIndex(Char_Field(f, n, 3));
    v->event = e < 0 ? CHAR_VOICE_NONE : e;
    field = Char_Field(f, n, 4);
    v->ms = *field ? wcstol(field, NULL, 10) : -1;
    field = Char_Field(f, n, 5);
    v->bytes = *field ? wcstoll(field, NULL, 10) : -1;
    v->rate = wcstol(Char_Field(f, n, 6), NULL, 10);
    v->channels = _wtoi(Char_Field(f, n, 7));
    field = Char_Field(f, n, 8);
    v->peak = *field ? _wtoi(field) : -1;
    field = Char_Field(f, n, 9);
    v->preview = (*field && wcscmp(field, L"-") != 0) ? Rs_Dup(field) : NULL;
}

// @model format <ply|obj>, colors <vertex|material|texture|grey|mixed>,
// mtl <state> <path|->, texture <state> <material> <path>, group <faces> <name>
static void Char_ImportLine(wchar_t **f, int n)
{
    struct CharJobData *j = &g_charJob;
    const wchar_t *key = Char_Field(f, n, 1);
    struct CharImport *r;
    int kind;

    if (wcscmp(key, L"format") == 0) {
        Char_Copy(j->modelFormat, 8, Char_Field(f, n, 2));
        return;
    }
    if (wcscmp(key, L"colors") == 0) {
        Char_Copy(j->modelColors, 16, Char_Field(f, n, 2));
        return;
    }
    if (wcscmp(key, L"mtl") == 0)
        kind = CHAR_IMPORT_MTL;
    else if (wcscmp(key, L"texture") == 0)
        kind = CHAR_IMPORT_TEXTURE;
    else if (wcscmp(key, L"group") == 0)
        kind = CHAR_IMPORT_GROUP;
    else
        return;
    if (j->importLines[kind]++ >= CHAR_IMPORT_ROWS_MAX)
        return;     // only counted
    if (j->importCount == j->importCap) {
        int cap = j->importCap ? j->importCap * 2 : 16;
        struct CharImport *m = Rs_Alloc((size_t)cap * sizeof(*m));
        if (j->importCount)
            memcpy(m, j->import, (size_t)j->importCount * sizeof(*m));
        Rs_Free(j->import);
        j->import = m;
        j->importCap = cap;
    }
    r = &j->import[j->importCount++];
    memset(r, 0, sizeof(*r));
    r->kind = kind;
    if (kind == CHAR_IMPORT_GROUP) {
        // "-": a group without a name
        r->faces = wcstoll(Char_Field(f, n, 2), NULL, 10);
        Char_Copy(r->path, CHAR_VAL, wcscmp(Char_Field(f, n, 3), L"-") == 0 ? L"" : Char_Field(f, n, 3));
    } else {
        Char_Copy(r->state, 16, Char_Field(f, n, 2));
        if (kind == CHAR_IMPORT_TEXTURE) {
            Char_Copy(r->material, 128, Char_Field(f, n, 3));
            Char_Copy(r->path, CHAR_VAL, Char_Field(f, n, 4));
        } else {
            Char_Copy(r->path, CHAR_VAL, wcscmp(Char_Field(f, n, 3), L"-") == 0 ? L"" : Char_Field(f, n, 3));
        }
    }
}

// One line of the current run. Human lines and unknown kinds stay only in
// the raw output. The line is split in the process.
static void Char_ParseLine(wchar_t *line)
{
    struct CharJobData *j = &g_charJob;
    wchar_t *f[16];
    int n = Rs_SplitMachine(line, f, 16);
    const wchar_t *kind;

    if (n <= 0)
        return;
    kind = f[0];
    if (wcscmp(kind, L"rldpack") == 0) {
        j->protocolSeen = 1;
        j->protocol = _wtoi(Char_Field(f, n, 1));
    } else if (wcscmp(kind, L"file") == 0) {
        if (wcscmp(Char_Field(f, n, 1), L"icon-retail") == 0)
            j->iconRetailOk = wcscmp(Char_Field(f, n, 2), L"ok") == 0;
        // The model: a PLY, or an OBJ (its MTL comes as @model mtl as well).
        if (wcscmp(Char_Field(f, n, 1), L"ply") == 0 || wcscmp(Char_Field(f, n, 1), L"obj") == 0) {
            const wchar_t *bytes = Char_Field(f, n, 4);
            j->modelSeen = 1;
            Char_Copy(j->modelState, 16, Char_Field(f, n, 2));
            j->modelBytes = *bytes ? wcstoll(bytes, NULL, 10) : -1;
        }
    } else if (wcscmp(kind, L"value") == 0) {
        const wchar_t *key = Char_Field(f, n, 1);
        wchar_t all[256];
        int v[6];
        if (wcscmp(key, L"out") == 0) {
            j->outSeen = 1;
            Char_Copy(j->out, CHAR_VAL, Char_Field(f, n, 2));
        } else if (wcscmp(key, L"size-range") == 0) {
            Char_JoinFields(f, n, all, 256);
            if (Char_Numbers(all, v, 2) == 2 && v[0] == 0 && v[1] == 0) {
                j->rangeNone = 1;
            } else if (Char_Numbers(all, v, 2) == 2) {
                int lo = v[0] < CHAR_SIZE_MIN ? CHAR_SIZE_MIN : v[0];
                int hi = v[1] > CHAR_SIZE_MAX ? CHAR_SIZE_MAX : v[1];
                if (lo <= hi) {
                    j->rangeSeen = 1;
                    j->rangeLo = lo;
                    j->rangeHi = hi;
                } else {
                    j->rangeNone = 1;
                }
            }
        } else if (wcscmp(key, L"kart-box") == 0) {
            Char_JoinFields(f, n, all, 256);
            if (Char_Numbers(all, v, 6) == 6) {
                j->kartSeen = 1;
                memcpy(j->kart, v, sizeof(j->kart));
            }
        } else if (wcscmp(key, L"retail-kart") == 0) {
            Char_JoinFields(f, n, all, 256);
            if (Char_Numbers(all, v, 6) == 6) {
                j->retailSeen = 1;
                memcpy(j->retail, v, sizeof(j->retail));
            }
        } else if (wcscmp(key, L"crash-box") == 0) {
            // Decimals ("-33.8"): kept in tenths.
            Char_JoinFields(f, n, all, 256);
            if (Char_NumbersScaled(all, v, 6, 10) == 6) {
                j->crashSeen = 1;
                memcpy(j->crash, v, sizeof(j->crash));
            }
        } else if (wcscmp(key, L"reduce") == 0 || wcscmp(key, L"fit") == 0 || wcscmp(key, L"wheels") == 0) {
            wchar_t *dst = key[0] == L'r' ? j->reduce : key[0] == L'f' ? j->fit : j->wheels;
            Char_JoinFields(f, n, dst, 48);
        } else if (wcscmp(key, L"repair") == 0) {
            Char_JoinFields(f, n, j->repair, 48);
        } else if (wcscmp(key, L"open-parts") == 0) {
            Char_JoinFields(f, n, j->openParts, 48);
        } else if (wcscmp(key, L"remesh") == 0) {
            Char_JoinFields(f, n, j->remesh, 48);
        } else if (wcscmp(key, L"mask") == 0) {
            Char_JoinFields(f, n, j->mask, 48);
        } else if (wcscmp(key, L"shadow-quad") == 0 && n >= 6) {
            int q;
            for (q = 0; q < 4; q++) {
                const double value = wcstod(Char_Field(f, n, 2 + q), NULL) * 16.0;
                j->lookQuad[q] = (int)(value < 0.0 ? value - 0.5 : value + 0.5);
            }
            j->lookQuadSeen = 1;
        } else if (wcscmp(key, L"map-color") == 0) {
            Char_JoinFields(f, n, j->mapColor, 48);
        }
    } else if (wcscmp(kind, L"char") == 0) {
        const wchar_t *key = Char_Field(f, n, 1);
        if (wcscmp(key, L"native-model") == 0)
            Char_NativeResult(f, n);
        else if (wcscmp(key, L"triangles") == 0 || (wcscmp(key, L"faces") == 0 && !j->triangles[0]))
            Char_Copy(j->triangles, 32, Char_Field(f, n, 2));
        else if (wcscmp(key, L"parts") == 0)
            Char_Copy(j->parts, 160, Char_Field(f, n, 3));
        else if (wcscmp(key, L"voices") == 0) {
            j->voicesSeen = 1;
            j->voiceClips = _wtoi(Char_Field(f, n, 2));
            j->voiceFilled = _wtoi(Char_Field(f, n, 3));
        } else if (wcscmp(key, L"budget") == 0 && n >= 6) {
            // <triangles> <limit> <draw bytes> <limit>, before any reduction
            int i;
            for (i = 0; i < 4; i++)
                j->budget[i] = wcstol(Char_Field(f, n, 2 + i), NULL, 10);
            j->budgetSeen = 1;
            // It comes before the reduction, which takes minutes for a model
            // of a million triangles: the headline says so meanwhile (unless
            // reduce-slow said how long already).
            if (j->budget[0] > j->budget[1] && g_char.runReduce && !g_char.slowText[0]) {
                wchar_t t[160], a[24], b[24];
                Char_Grouped(a, 24, j->budget[0]);
                Char_Grouped(b, 24, j->budget[1]);
                swprintf(t, 160, L"Reducing %ls triangles to the limit of %ls - this can take minutes...", a, b);
                Char_Headline(t, RS_COL_MUTED);
            }
        } else if (wcscmp(key, L"icon_background") == 0) {
            Char_Copy(j->iconBackground, 48, Char_Field(f, n, 2));
        } else if (wcscmp(key, L"icon_place") == 0) {
            Char_Copy(j->iconPlace, 48, Char_Field(f, n, 2));
        } else if (wcscmp(key, L"reduced") == 0 && n >= 6) {
            // <triangles before> <after> <draw bytes before> <after>
            int i;
            for (i = 0; i < 4; i++)
                j->reduced[i] = wcstol(Char_Field(f, n, 2 + i), NULL, 10);
            j->reducedSeen = 1;
        } else if (wcscmp(key, L"repaired") == 0 && n >= 2 + CHAR_REPAIR_FIELDS) {
            // the ten counts of the repair (RldMk_EmitFacts in tools/rldpack_char.inc)
            int i;
            for (i = 0; i < CHAR_REPAIR_FIELDS; i++)
                j->repaired[i] = wcstol(Char_Field(f, n, 2 + i), NULL, 10);
            j->repairedSeen = 1;
        } else if (wcscmp(key, L"two-sided") == 0 && n >= 3) {
            j->twoSided = wcstol(Char_Field(f, n, 2), NULL, 10);
            j->twoSidedSeen = 1;
        } else if (wcscmp(key, L"remeshed") == 0 && n >= 2 + CHAR_REMESH_FIELDS) {
            int i;
            for (i = 0; i < CHAR_REMESH_FIELDS; i++)
                j->remeshed[i] = wcstol(Char_Field(f, n, 2 + i), NULL, 10);
            j->remeshedSeen = 1;
        } else if (wcscmp(key, L"fit") == 0 && n >= 8) {
            // <factor> <length before> <after> <height before> <after> <kart|model>
            j->fitSeen = 1;
            Char_Copy(j->fitFactor, 16, Char_Field(f, n, 2));
            Char_Copy(j->fitLength[0], 16, Char_Field(f, n, 3));
            Char_Copy(j->fitLength[1], 16, Char_Field(f, n, 4));
            Char_Copy(j->fitHeight[0], 16, Char_Field(f, n, 5));
            Char_Copy(j->fitHeight[1], 16, Char_Field(f, n, 6));
            Char_Copy(j->fitBasis, 16, Char_Field(f, n, 7));
        }
    } else if (wcscmp(kind, L"model") == 0) {
        Char_ImportLine(f, n);
    } else if (wcscmp(kind, L"voice") == 0) {
        Char_VoiceLine(f, n);
    } else if (wcscmp(kind, L"voiceevent") == 0) {
        int e = Char_VoiceEventIndex(Char_Field(f, n, 1));
        if (e >= 0 && e < CHAR_VOICE_EVENTS) {
            j->voiceEventSeen = 1;
            j->voiceEvent[e] = _wtoi(Char_Field(f, n, 2));
        }
    } else if (wcscmp(kind, L"msg") == 0) {
        const wchar_t *text = Char_Field(f, n, 3);
        const wchar_t *detail = Char_Field(f, n, 4);
        if (!*text) {
            text = detail;
            detail = L"";
        }
        // The groups beyond the ones rldpack lists: "<n> more groups (o, g) are not listed."
        if (wcscmp(Char_Field(f, n, 2), L"obj-groups") == 0)
            j->groupsMore += wcstol(text, NULL, 10);
        // How long the reduction takes ("... takes about N s"): into the
        // headline beside the bar at once, not only into the list at the end.
        if (wcscmp(Char_Field(f, n, 2), L"reduce-slow") == 0 && g_char.jobId && g_char.jobKind != CHAR_JOB_META) {
            const wchar_t *about = wcsstr(text, L"about ");
            if (about) {
                wchar_t line[96];
                size_t k = 0;
                while (about[k] && about[k] != L';' && about[k] != L'.' && k < 47)
                    k++;
                memcpy(g_char.slowText, about, k * sizeof(wchar_t));
                g_char.slowText[k] = 0;
                swprintf(line, 96, L"%ls - the reduction takes %ls", g_char.jobKind == CHAR_JOB_BUILD ? L"Building" : L"Checking",
                         g_char.slowText);
                Char_Headline(line, RS_COL_MUTED);
            }
        }
        Char_JobMsg(Rs_SeverityFromText(Char_Field(f, n, 1)), Char_Field(f, n, 2), text, detail);
    } else if (wcscmp(kind, L"result") == 0) {
        const wchar_t *bytes = Char_Field(f, n, 3);
        j->resultSeen = 1;
        Char_Copy(j->resultState, 16, Char_Field(f, n, 1));
        Char_Copy(j->resultPath, CHAR_VAL, Char_Field(f, n, 2));
        j->resultBytes = *bytes ? wcstoll(bytes, NULL, 10) : -1;
        Char_Copy(j->resultSha, 80, Char_Field(f, n, 4));
    } else if (wcscmp(kind, L"end") == 0) {
        j->endSeen = 1;
        j->endCode = _wtoi(Char_Field(f, n, 1));
    }
}

// A line of the result shown: kept in feedText (for Char_CacheStore), then
// read (Char_ParseLine on a copy).
static void Char_Feed(const wchar_t *line)
{
    size_t n = wcslen(line);
    wchar_t *copy;
    if (g_char.feedLen + n + 2 > g_char.feedCap) {
        size_t cap = g_char.feedCap ? g_char.feedCap : 4096;
        wchar_t *p;
        while (cap < g_char.feedLen + n + 2)
            cap *= 2;
        p = Rs_Alloc(cap * sizeof(wchar_t));
        if (g_char.feedLen)
            memcpy(p, g_char.feedText, g_char.feedLen * sizeof(wchar_t));
        Rs_Free(g_char.feedText);
        g_char.feedText = p;
        g_char.feedCap = cap;
    }
    memcpy(g_char.feedText + g_char.feedLen, line, n * sizeof(wchar_t));
    g_char.feedLen += n;
    g_char.feedText[g_char.feedLen++] = L'\n';
    g_char.feedText[g_char.feedLen] = 0;
    copy = Rs_Dup(line);
    Char_ParseLine(copy);
    Rs_Free(copy);
}

// Every line of text (lines ended by \n or \r\n) to fn, from line first on.
static void Char_EachLine(const wchar_t *text, int first, void (*fn)(const wchar_t *line))
{
    wchar_t *all = Rs_Dup(text ? text : L""), *line, *next;
    int i = 0;
    for (line = all; line && *line; line = next, i++) {
        next = wcschr(line, L'\n');
        if (next) {
            if (next > line && next[-1] == L'\r')
                next[-1] = 0;
            *next++ = 0;
        }
        if (i >= first)
            fn(line);
    }
    Rs_Free(all);
}

static const struct CharMsg *Char_FindMsg(const wchar_t *code)
{
    int i;
    for (i = 0; i < g_charJob.msgCount; i++)
        if (g_charJob.msgs[i].code && wcscmp(g_charJob.msgs[i].code, code) == 0)
            return &g_charJob.msgs[i];
    return NULL;
}

// ---------------------------------------------------------------------------
// Tabs
// ---------------------------------------------------------------------------

// The tab a control belongs to, by its ID; -1 = on every tab (the heads, the
// preview, the bar). *card: in the tab Extras the card of the control, -1 = both.
static int Char_TabOfId(int id, int *card)
{
    *card = -1;
    if ((id >= CHAR_ID_MODEL_LABEL && id <= CHAR_ID_MODEL_INFO) || (id >= CHAR_ID_SIZE_LABEL && id <= CHAR_ID_OPTIONS_LABEL) ||
        (id >= CHAR_ID_REPAIR && id <= CHAR_ID_QUALITY) || id == CHAR_ID_REDUCE_FIT || id == CHAR_ID_MODEL_IMPORT ||
        (id >= CHAR_ID_MODEL_FILES && id <= CHAR_ID_TEXTURES_CLEAR) || (id >= CHAR_ID_FALLBACK && id <= CHAR_ID_NATIVE_LINE))
        return CHAR_TAB_MODEL;
    if ((id >= CHAR_ID_NAME_LABEL && id <= CHAR_ID_CLASS_HELP) || (id >= CHAR_ID_MASK_LABEL && id <= CHAR_ID_MASK_HELP))
        return CHAR_TAB_DRIVER;
    if ((id >= CHAR_ID_ICON_LABEL && id <= CHAR_ID_ICON_IMAGE + 1) || (id >= CHAR_ID_ICON_FIT_LABEL && id <= CHAR_ID_ICON_FRAME)) {
        *card = CHAR_LOOK_PORTRAIT;
        return CHAR_TAB_LOOK;
    }
    if ((id >= CHAR_ID_MAPCOLOR_LABEL && id <= CHAR_ID_MAPCOLOR_HELP) || (id >= CHAR_ID_LOOK_HINT && id <= CHAR_ID_LOOK_NOTE)) {
        *card = CHAR_LOOK_RACE;
        return CHAR_TAB_LOOK;
    }
    if (id >= CHAR_ID_LOOK_SWITCH && id < CHAR_ID_LOOK_SWITCH + CHAR_LOOK_CARDS)
        return CHAR_TAB_LOOK;
    if ((id >= CHAR_ID_VOICES_LABEL && id <= CHAR_ID_VOICE_NORMALIZE) ||
        (id >= CHAR_ID_VOICE_RULE && id <= CHAR_ID_VOICE_EVENTS))
        return CHAR_TAB_VOICES;
    if (id >= CHAR_ID_EXTRAS && id < CHAR_ID_EXTRAS + CHAR_EXTRAS_COUNT)
        return CHAR_TAB_EXTRAS;
    if (id >= CHAR_ID_WHEELS_FIRST && id <= CHAR_ID_WHEELS_LAST) {
        *card = CHAR_EXTRAS_WHEELS;
        return CHAR_TAB_EXTRAS;
    }
    if (id >= CHAR_ID_ANIM_FIRST && id <= CHAR_ID_ANIM_LAST) {
        *card = CHAR_EXTRAS_ANIM;
        return CHAR_TAB_EXTRAS;
    }
    if (id >= CHAR_ID_IMPORT_FIRST && id <= CHAR_ID_IMPORT_LAST) {
        *card = CHAR_EXTRAS_IMPORT;
        return CHAR_TAB_EXTRAS;
    }
    return -1;
}

// The card of the tab Extras an @msg id belongs to, -1 = none: the choices of
// the card Import (the axes, the palette, the vertex colours of an OBJ; the
// native model, which the user mode shows on the tab Model).
static int Char_CardOfCode(const wchar_t *code)
{
    if (!code)
        return -1;
    if (wcscmp(code, L"vertex-colors") == 0 || wcscmp(code, L"obj-vertex-modulation") == 0 ||
        wcscmp(code, L"up") == 0 || wcscmp(code, L"forward") == 0 || wcscmp(code, L"ply-up-axis") == 0 ||
        wcscmp(code, L"colors") == 0 || (wcsncmp(code, L"native-", 7) == 0 && !Rs_NativeForUsers()))
        return CHAR_EXTRAS_IMPORT;
    if (wcsncmp(code, L"pose", 4) == 0)
        return CHAR_EXTRAS_ANIM;
    if (wcsncmp(code, L"wheel-", 6) == 0)
        return CHAR_EXTRAS_WHEELS;
    return -1;
}

// The card of the tab In-game look an @msg id belongs to.
static int Char_LookCardOfCode(const wchar_t *code)
{
    return (code && wcsncmp(code, L"icon", 4) == 0) ? CHAR_LOOK_PORTRAIT : CHAR_LOOK_RACE;
}

// The tab an @msg id of make-char belongs to (the ids in reloadstudio.h and
// tools/rldpack_char.inc); -1 = none (the output, or no id). Everything about
// the model and its options is the tab Model.
static int Char_TabOfCode(const wchar_t *code)
{
    if (!code || !code[0] || wcscmp(code, L"write") == 0)
        return -1;
    if (wcsncmp(code, L"icon", 4) == 0 || wcscmp(code, L"map-color") == 0 || wcscmp(code, L"shadow") == 0 ||
        wcscmp(code, L"exhaust") == 0)
        return CHAR_TAB_LOOK;
    if (wcsncmp(code, L"voice", 5) == 0)
        return CHAR_TAB_VOICES;
    if (wcsncmp(code, L"name", 4) == 0 || wcscmp(code, L"author") == 0 || wcscmp(code, L"class") == 0 ||
        wcscmp(code, L"template") == 0 || wcsncmp(code, L"mask", 4) == 0)
        return CHAR_TAB_DRIVER;
    if (wcsncmp(code, L"pose", 4) == 0 || wcsncmp(code, L"wheel-", 6) == 0)
        return CHAR_TAB_EXTRAS;
    if (Char_CardOfCode(code) == CHAR_EXTRAS_IMPORT)
        return CHAR_TAB_EXTRAS;
    return CHAR_TAB_MODEL;
}

// The message list, with the tab of every entry (a click opens it).
static void Char_MsgClear(void)
{
    Rs_MsgListClear(g_char.msgs);
    g_char.msgTabCount = 0;
}

// card: of the tab Extras (Char_CardOfCode), -1 = none.
static void Char_MsgAddCard(int tab, int card, int severity, const wchar_t *text, const wchar_t *detail)
{
    if (g_char.msgTabCount == g_char.msgTabCap) {
        int cap = g_char.msgTabCap ? g_char.msgTabCap * 2 : 32;
        int *n = Rs_Alloc((size_t)cap * sizeof(*n));
        int *c = Rs_Alloc((size_t)cap * sizeof(*c));
        if (g_char.msgTabCount) {
            memcpy(n, g_char.msgTab, (size_t)g_char.msgTabCount * sizeof(*n));
            memcpy(c, g_char.msgCard, (size_t)g_char.msgTabCount * sizeof(*c));
        }
        Rs_Free(g_char.msgTab);
        Rs_Free(g_char.msgCard);
        g_char.msgTab = n;
        g_char.msgCard = c;
        g_char.msgTabCap = cap;
    }
    g_char.msgTab[g_char.msgTabCount] = tab;
    g_char.msgCard[g_char.msgTabCount++] = (tab == CHAR_TAB_EXTRAS || tab == CHAR_TAB_LOOK) ? card : -1;
    Rs_MsgListAdd(g_char.msgs, severity, text, detail);
}

static void Char_MsgAdd(int tab, int severity, const wchar_t *text, const wchar_t *detail)
{
    Char_MsgAddCard(tab, -1, severity, text, detail);
}

// A message of the list opens its tab (and its card of the tab Extras).
static void Char_MsgOpen(HWND page, int i);

// The errors and warnings of the last run per tab, for the heads.
static void Char_CountTabs(void)
{
    int i;
    memset(g_char.tabErrors, 0, sizeof(g_char.tabErrors));
    memset(g_char.tabWarnings, 0, sizeof(g_char.tabWarnings));
    for (i = 0; i < g_charJob.msgCount; i++) {
        int t = Char_TabOfCode(g_charJob.msgs[i].code);
        if (t < 0)
            continue;
        if (g_charJob.msgs[i].severity == RS_SEV_ERROR)
            g_char.tabErrors[t]++;
        else if (g_charJob.msgs[i].severity == RS_SEV_WARNING)
            g_char.tabWarnings[t]++;
    }
}

// What each head shows: the errors and warnings of the last run first, then
// whether the step is filled in. Model and Driver are needed, the others optional.
static void Char_TabsUpdate(void)
{
    wchar_t model[CHAR_VAL], icon[CHAR_VAL], voices[CHAR_VAL];
    wchar_t *name;
    int t;

    if (!g_char.tabHead[0])
        return;
    Char_FieldPath(g_char.model, model, CHAR_VAL);
    Char_FieldPath(g_char.icon, icon, CHAR_VAL);
    Char_FieldPath(g_char.voices, voices, CHAR_VAL);
    name = Rs_GetText(g_char.name);
    for (t = 0; t < CHAR_TABS; t++) {
        int step;
        if (g_char.tabErrors[t])
            step = CHAR_STEP_PROBLEM;
        else if (g_char.tabWarnings[t])
            step = CHAR_STEP_WARNING;
        else if (t == CHAR_TAB_MODEL)
            step = model[0] && g_char.modelRead ? CHAR_STEP_DONE : CHAR_STEP_TODO;
        else if (t == CHAR_TAB_DRIVER)
            step = name[0] ? CHAR_STEP_DONE : CHAR_STEP_TODO;
        else if (t == CHAR_TAB_LOOK)
            step = (icon[0] || g_char.mapColorSet) ? CHAR_STEP_DONE : CHAR_STEP_OPTIONAL;
        else if (t == CHAR_TAB_VOICES) {
            // Done only when the driver says at least one clip; a folder not
            // read yet is still to do, one without such a clip a warning.
            int heard, events;
            if (!voices[0])
                step = CHAR_STEP_OPTIONAL;
            else if (!g_char.voiceKnown)
                step = CHAR_STEP_TODO;
            else
                step = Char_VoicesHeard(&heard, &events) > 0 ? CHAR_STEP_DONE : CHAR_STEP_WARNING;
        }
        else
            step = CHAR_STEP_OPTIONAL;
        if (step != g_char.step[t] || !g_char.stepShown) {
            wchar_t tip[128];
            g_char.step[t] = step;
            swprintf(tip, 128, L"Step %d: %ls - %ls", t + 1, g_charTabTexts[t], g_charStepWords[step]);
            Rs_SetTip(g_char.tabHead[t], tip);
            InvalidateRect(g_char.tabHead[t], NULL, FALSE);
        }
    }
    g_char.stepShown = 1;
    Rs_Free(name);
    EnableWindow(g_char.back, g_char.tab > 0);
    EnableWindow(g_char.next, g_char.tab < CHAR_TABS - 1);
}

// The head of tab t: its number in a circle that says the step (green done,
// amber warning, red problem, an outline when optional or still to do), the
// name beside it. The head shown has the colour of the card and is open
// towards it; the others lie on the page with the card's border below them.
static void Char_DrawTab(const DRAWITEMSTRUCT *di, int t)
{
    HDC dc = di->hDC;
    RECT rc = di->rcItem, r;
    int shown = t == g_char.tab, d = Rs_Px(20), cx, cy, step = g_char.step[t];
    COLORREF bg = shown ? RS_COL_CARD : RS_COL_PAGE, ring, fill, ink;
    HBRUSH br;
    HPEN pen;
    HGDIOBJ oldBr, oldPen, oldFont;
    wchar_t num[4];

    br = CreateSolidBrush(RS_COL_PAGE);
    FillRect(dc, &rc, br);
    DeleteObject(br);
    br = CreateSolidBrush(bg);
    pen = CreatePen(PS_SOLID, 1, RS_COL_BORDER);
    oldBr = SelectObject(dc, br);
    oldPen = SelectObject(dc, pen);
    if (shown) {
        // Round at the top; the bottom lies below the button and is not drawn.
        RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom + Rs_Px(16), Rs_Px(12), Rs_Px(12));
    } else {
        MoveToEx(dc, rc.left, rc.bottom - 1, NULL);
        LineTo(dc, rc.right, rc.bottom - 1);
    }
    SelectObject(dc, oldBr);
    SelectObject(dc, oldPen);
    DeleteObject(br);
    DeleteObject(pen);

    switch (step) {
    case CHAR_STEP_DONE: fill = RS_COL_OK; break;
    case CHAR_STEP_WARNING: fill = RS_COL_WARNING; break;
    case CHAR_STEP_PROBLEM: fill = RS_COL_ERROR; break;
    default: fill = bg; break;
    }
    ring = step == CHAR_STEP_TODO ? RS_COL_TEXT : step == CHAR_STEP_OPTIONAL ? RS_COL_MUTED : fill;
    ink = (step == CHAR_STEP_TODO || step == CHAR_STEP_OPTIONAL) ? ring : RS_COL_CARD;   // white, dark in the dark scheme
    cx = rc.left + Rs_Px(12);
    cy = (rc.top + rc.bottom - 1 - d) / 2;
    br = CreateSolidBrush(fill);
    pen = CreatePen(PS_SOLID, Rs_Px(1) > 1 ? Rs_Px(1) : 1, ring);
    oldBr = SelectObject(dc, br);
    oldPen = SelectObject(dc, pen);
    Ellipse(dc, cx, cy, cx + d + 1, cy + d + 1);
    SelectObject(dc, oldBr);
    SelectObject(dc, oldPen);
    DeleteObject(br);
    DeleteObject(pen);
    SetBkMode(dc, TRANSPARENT);
    swprintf(num, 4, L"%d", t + 1);
    oldFont = SelectObject(dc, Rs_Font(RS_FONT_SMALL));
    SetTextColor(dc, ink);
    SetRect(&r, cx, cy, cx + d + 1, cy + d + 1);
    DrawTextW(dc, num, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, Rs_Font(shown ? RS_FONT_BOLD : RS_FONT_BODY));
    SetTextColor(dc, shown ? RS_COL_TEXT : RS_COL_MUTED);
    SetRect(&r, cx + d + Rs_Px(8), rc.top, rc.right - Rs_Px(4), rc.bottom - 1);
    DrawTextW(dc, g_charTabTexts[t], -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, oldFont);
    if ((di->itemState & ODS_FOCUS) && !(di->itemState & ODS_NOFOCUSRECT)) {
        r = rc;
        InflateRect(&r, -Rs_Px(4), -Rs_Px(5));
        SetTextColor(dc, RS_COL_TEXT);
        DrawFocusRect(dc, &r);
    }
}

// Import | Wheels | Animations in the tab Extras, Portrait | In the race in
// the tab In-game look: the card shown in the text colour with an orange line
// below, the other muted.
static void Char_DrawSwitch(const DRAWITEMSTRUCT *di, const wchar_t *text, int shown)
{
    HDC dc = di->hDC;
    RECT rc = di->rcItem, r;
    HBRUSH br = CreateSolidBrush(RS_COL_CARD);
    HGDIOBJ oldFont;

    FillRect(dc, &rc, br);
    DeleteObject(br);
    SetBkMode(dc, TRANSPARENT);
    oldFont = SelectObject(dc, Rs_Font(RS_FONT_SECTION));
    SetTextColor(dc, shown ? RS_COL_TEXT : RS_COL_MUTED);
    DrawTextW(dc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, oldFont);
    if (shown) {
        SetRect(&r, rc.left + Rs_Px(8), rc.bottom - Rs_Px(3), rc.right - Rs_Px(8), rc.bottom);
        br = CreateSolidBrush(RS_COL_ACCENT);
        FillRect(dc, &r, br);
        DeleteObject(br);
    }
    if ((di->itemState & ODS_FOCUS) && !(di->itemState & ODS_NOFOCUSRECT)) {
        r = rc;
        InflateRect(&r, -Rs_Px(2), -Rs_Px(4));
        SetTextColor(dc, RS_COL_TEXT);
        DrawFocusRect(dc, &r);
    }
}

// A head (or a switch of the tab Extras): an owner-drawn button, so that a
// click, the keys and the automation reach it as any button.
static HWND Char_TabButton(HWND page, int id, const wchar_t *text, int font)
{
    HWND h = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, page,
                             (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    if (h) {
        SetPropW(h, L"RsFont", (HANDLE)(INT_PTR)(font + 1));    // refont after a change of dpi (rs_shell.c)
        SendMessageW(h, WM_SETFONT, (WPARAM)Rs_Font(font), FALSE);
    }
    return h;
}

static void Char_UpdateButtons(void);

// Shows tab t. focus = 1: the keyboard focus to its head (Ctrl+Tab); it also
// goes there when it was on a control that is hidden now.
static void Char_SelectTab(HWND page, int t, int focus)
{
    HWND f;
    int i;

    RsView_PickBegin(g_char.view, 0);   // a pick ends with the tab (no stray click sets a point later)

    if (t < 0 || t >= CHAR_TABS)
        return;
    g_char.tab = t;
    for (i = 0; i < CHAR_TABS; i++)
        InvalidateRect(g_char.tabHead[i], NULL, FALSE);
    Char_Relayout(page);
    Char_UpdateButtons();
    f = GetFocus();
    while (f && GetParent(f) && GetParent(f) != page)
        f = GetParent(f);       // the edit field of a combo box
    if (focus || (f && GetParent(f) == page && (!Char_IsShown(f) || !IsWindowEnabled(f))))
        SetFocus(g_char.tabHead[t]);
}

// The card of the tab Extras.
static void Char_SelectExtras(HWND page, int c)
{
    int i;
    if (c < 0 || c >= CHAR_EXTRAS_COUNT)
        return;
    g_char.extrasCard = c;
    for (i = 0; i < CHAR_EXTRAS_COUNT; i++)
        InvalidateRect(g_char.extrasSwitch[i], NULL, FALSE);
    Char_Relayout(page);
}

static void Char_MsgOpen(HWND page, int i)
{
    if (i < 0 || i >= g_char.msgTabCount || g_char.msgTab[i] < 0)
        return;
    if (g_char.msgTab[i] == CHAR_TAB_LOOK && g_char.msgCard[i] >= 0) {
        int c;
        g_char.lookCard = g_char.msgCard[i];
        for (c = 0; c < CHAR_LOOK_CARDS; c++)
            InvalidateRect(g_char.lookSwitch[c], NULL, FALSE);
    } else if (g_char.msgCard[i] >= 0 && g_char.msgCard[i] != g_char.extrasCard) {
        int c;
        g_char.extrasCard = g_char.msgCard[i];
        for (c = 0; c < CHAR_EXTRAS_COUNT; c++)
            InvalidateRect(g_char.extrasSwitch[c], NULL, FALSE);
    }
    Char_SelectTab(page, g_char.msgTab[i], 0);
}

// ---------------------------------------------------------------------------
// Size slider
// ---------------------------------------------------------------------------

static int Char_SizePos(void)
{
    return (int)SendMessageW(g_char.size, TBM_GETPOS, 0, 0);
}

static void Char_SizeShow(void)
{
    wchar_t t[16];
    swprintf(t, 16, L"%d %%", g_char.sizeNow);
    Rs_SetText(g_char.sizeValue, t);
}

// The line below the slider: the allowed range, or why the slider went back.
static void Char_SizeNote(void)
{
    wchar_t t[768];
    if (g_char.rangeNone) {
        Char_SetLabel(g_char.sizeNote, L"No size fits this model - make the model itself smaller (see the messages).",
                      RS_COL_ERROR, &g_char.sizeColor);
        return;
    }
    if (!g_char.rangeKnown) {
        Char_SetLabel(g_char.sizeNote, L"The sizes this model allows show up after the first check.",
                      RS_COL_MUTED, &g_char.sizeColor);
        return;
    }
    if (g_char.sizeLocked) {
        const wchar_t *why = g_char.sizeWhy[g_char.sizeLockHigh ? 1 : 0];
        swprintf(t, 768, L"Locked at %d %% - this model allows %d..%d %%%ls%ls", g_char.sizeNow,
                 g_char.rangeLo, g_char.rangeHi, why[0] ? L": " : L".", why);
        Char_SetLabel(g_char.sizeNote, t, RS_COL_WARNING, &g_char.sizeColor);
        return;
    }
    if (g_char.rangeLo > CHAR_SIZE_MIN || g_char.rangeHi < CHAR_SIZE_MAX)
        swprintf(t, 768, L"This model allows %d..%d %% - the rest of the bar is locked.", g_char.rangeLo,
                 g_char.rangeHi);
    else
        swprintf(t, 768, L"This model allows every size from %d to %d %%.", g_char.rangeLo, g_char.rangeHi);
    Char_SetLabel(g_char.sizeNote, t, RS_COL_MUTED, &g_char.sizeColor);
}

// Marks the allowed range on the bar (or nothing while it is unknown).
static void Char_SizeRangeShow(void)
{
    if (g_char.rangeKnown)
        SendMessageW(g_char.size, TBM_SETSEL, TRUE, MAKELPARAM(g_char.rangeLo, g_char.rangeHi));
    else
        SendMessageW(g_char.size, TBM_CLEARSEL, TRUE, 0);
}

// Puts the slider to value, inside the allowed range. Returns 1 if the value
// had to be moved into the range.
static int Char_SizeSet(int value)
{
    int locked = 0;
    if (value < CHAR_SIZE_MIN)
        value = CHAR_SIZE_MIN;
    if (value > CHAR_SIZE_MAX)
        value = CHAR_SIZE_MAX;
    if (g_char.rangeKnown) {
        if (value < g_char.rangeLo) {
            value = g_char.rangeLo;
            locked = 1;
            g_char.sizeLockHigh = 0;
        } else if (value > g_char.rangeHi) {
            value = g_char.rangeHi;
            locked = 1;
            g_char.sizeLockHigh = 1;
        }
    }
    if (Char_SizePos() != value)
        SendMessageW(g_char.size, TBM_SETPOS, TRUE, value);
    g_char.sizeNow = value;
    g_char.sizeLocked = locked;
    Char_SizeShow();
    Char_SizeNote();
    return locked;
}

// The model changed: its range is no longer known, the slider is free.
static void Char_SizeForget(void)
{
    g_char.rangeKnown = 0;
    g_char.rangeNone = 0;
    g_char.sizeWhy[0][0] = 0;
    g_char.sizeWhy[1][0] = 0;
    g_char.sizeLocked = 0;
    Char_SizeRangeShow();
    Char_SizeNote();
    Char_SetLabel(g_char.sizeFit, CHAR_FIT_WAIT_TEXT, RS_COL_MUTED, &g_char.fitColor);
}

// ---------------------------------------------------------------------------
// Display of the results
// ---------------------------------------------------------------------------

static int Char_CanBuild(void)
{
    return g_char.checked && !g_char.jobId && !g_char.timer;
}

static void Char_UpdateButtons(void)
{
    int building = g_char.jobId && g_char.jobKind == CHAR_JOB_BUILD;
    wchar_t model[CHAR_VAL];

    Char_FieldPath(g_char.model, model, CHAR_VAL);
    EnableWindow(g_char.check, model[0] && !building);
    EnableWindow(g_char.build, Char_CanBuild());
    EnableWindow(g_char.modelBrowse, !building);
    EnableWindow(g_char.iconBrowse, !building);
    EnableWindow(g_char.voicesBrowse, !building);
    EnableWindow(g_char.texturesBrowse, !building);
    EnableWindow(g_char.outBrowse, !building);
    EnableWindow(g_char.show, !building);
    Char_VoiceSelShow();
    Char_TabsUpdate();
}

static void Char_ShowView(void)
{
    if (g_char.showRaw)
        Char_RawRefresh();
    ShowWindow(g_char.raw, g_char.showRaw ? SW_SHOW : SW_HIDE);
    ShowWindow(g_char.msgs, g_char.showRaw ? SW_HIDE : SW_SHOW);
    Rs_SetText(g_char.rawToggle, g_char.showRaw ? L"Show messages" : L"Show rldpack output");
}

static void Char_Relayout(HWND page)
{
    RECT rc;
    GetClientRect(page, &rc);
    if (rc.right > 0 && rc.bottom > 0) {
        Char_Layout(page, rc.right, rc.bottom);
        InvalidateRect(page, NULL, TRUE);
    }
}

// The model info wraps to at most CHAR_INFO_LINES lines (Char_FitLines); the
// page is laid out again only when its height changes.
static void Char_SetInfo(const wchar_t *text, COLORREF color)
{
    HWND page = GetParent(g_char.modelInfo);
    RECT rc;
    int need;

    Char_Copy(g_char.infoFull, CHAR_VAL, text);
    Char_SetLabel(g_char.modelInfo, text, color, &g_char.infoColor);
    GetClientRect(g_char.modelInfo, &rc);
    if (rc.right <= 0)
        return;
    Char_FitLines(g_char.modelInfo, g_char.infoFull, rc.right, CHAR_INFO_LINES);
    need = Char_TextHeight(g_char.modelInfo, rc.right, CHAR_INFO_LINES);
    if (need < Rs_Px(18))
        need = Rs_Px(18);
    if (need != rc.bottom)
        Char_Relayout(page);
}

static int Char_CountMsgs(int severity)
{
    int i, n = 0;
    for (i = 0; i < g_charJob.msgCount; i++)
        if (g_charJob.msgs[i].severity == severity)
            n++;
    return n;
}

// All @msg of one severity, in rldpack's order.
static void Char_AddMsgs(int severity)
{
    int i;
    for (i = 0; i < g_charJob.msgCount; i++)
        if (g_charJob.msgs[i].severity == severity)
            Char_MsgAddCard(Char_TabOfCode(g_charJob.msgs[i].code),
                            Char_TabOfCode(g_charJob.msgs[i].code) == CHAR_TAB_LOOK ? Char_LookCardOfCode(g_charJob.msgs[i].code)
                                                                                     : Char_CardOfCode(g_charJob.msgs[i].code),
                            severity,
                            g_charJob.msgs[i].text, g_charJob.msgs[i].detail);
}

// What rldpack could not report itself: no result, failure without a
// reason, foreign protocol. Return: number of errors that were added.
static int Char_AddRunProblems(int exitCode, int ok)
{
    struct CharJobData *j = &g_charJob;
    wchar_t t[256];

    if (j->protocolSeen && j->protocol != RS_PROTOCOL) {
        swprintf(t, 256, L"rldpack reports in format %d, but this Reload Studio reads format %d.",
                 j->protocol, RS_PROTOCOL);
        Char_MsgAdd(-1, RS_SEV_WARNING, t,
                      L"Some results may be missing. rldpack is built into this Reload Studio, so both should always "
                      L"match - this build looks inconsistent.");
    }
    if (!j->resultSeen) {
        swprintf(t, 256, L"rldpack stopped without a result (exit code %d).", exitCode);
        Char_MsgAdd(-1, RS_SEV_ERROR, t,
                      L"Click \"Show rldpack output\" to see everything it printed.");
        return 1;
    }
    if (!ok && Char_CountMsgs(RS_SEV_ERROR) == 0) {
        swprintf(t, 256, L"rldpack did not accept the character, but gave no reason (exit code %d).", exitCode);
        Char_MsgAdd(-1, RS_SEV_ERROR, t,
                      L"Click \"Show rldpack output\" to see everything it printed.");
        return 1;
    }
    return 0;
}

// The line below the model field: what rldpack found in the model.
static void Char_ApplyModelInfo(void)
{
    struct CharJobData *j = &g_charJob;
    wchar_t t[512];
    wchar_t size[32];

    if (j->modelSeen && wcscmp(j->modelState, L"ok") != 0) {
        Char_SetInfo(L"The model file cannot be opened - check the path.", RS_COL_ERROR);
        return;
    }
    if (!j->modelSeen) {
        Char_SetInfo(L"-", RS_COL_MUTED);
        return;
    }
    Char_SizeText(size, 32, j->modelBytes < 0 ? 0 : j->modelBytes);
    if (j->reducedSeen) {
        // Reduced: what it changed first, then the parts as without it.
        wchar_t n[4][24];
        int i;
        for (i = 0; i < 4; i++)
            Char_Grouped(n[i], 24, j->reduced[i]);
        swprintf(t, 512, L"Triangles %ls -> %ls, draw memory %ls -> %ls bytes - reduced to fit%ls%ls (%ls)",
                 n[0], n[1], n[2], n[3], j->parts[0] ? L" - " : L"", j->parts, size);
    } else if (j->triangles[0] && j->parts[0])
        swprintf(t, 512, L"%ls triangles drawn - %ls  (%ls)", j->triangles, j->parts, size);
    else if (j->triangles[0])
        swprintf(t, 512, L"%ls triangles drawn  (%ls)", j->triangles, size);
    else
        swprintf(t, 512, L"read, but not converted - see the messages  (%ls)", size);
    Char_SetInfo(t, j->parts[0] ? RS_COL_TEXT : RS_COL_WARNING);
}

// ---------------------------------------------------------------------------
// What rldpack read of the model (@model): the line below the model info and
// the list of the MTL, the textures and the groups of an OBJ
// ---------------------------------------------------------------------------

static const wchar_t *const g_charImportKinds[] = { L"MTL", L"Texture", L"Group" };

// The words of a state of @model mtl or texture, for the list.
static const wchar_t *Char_ImportStateText(const wchar_t *state)
{
    if (wcscmp(state, L"ok") == 0)
        return L"found";
    if (wcscmp(state, L"unreadable") == 0 || wcscmp(state, L"bad") == 0)
        return L"cannot be read";
    if (wcscmp(state, L"unsupported") == 0)
        return L"not a PNG, JPG or TGA";
    return state;   // missing, none
}

// 1 = the row says that something was not found or not read (amber).
static int Char_ImportProblem(const struct CharImport *r)
{
    return r->kind != CHAR_IMPORT_GROUP && wcscmp(r->state, L"ok") != 0 && wcscmp(r->state, L"none") != 0;
}

// The texts of a row in the columns "File or group" and "State".
static void Char_ImportTexts(const struct CharImport *r, wchar_t *name, int nameCap, wchar_t *state, int stateCap)
{
    if (r->kind == CHAR_IMPORT_GROUP) {
        Char_Copy(name, nameCap, r->path[0] ? r->path : L"(no name)");
        swprintf(state, (size_t)stateCap, L"%lld face%ls", r->faces, r->faces == 1 ? L"" : L"s");
        return;
    }
    if (r->path[0])
        Char_Copy(name, nameCap, Rs_PathName(r->path));
    else
        Char_Copy(name, nameCap, r->kind == CHAR_IMPORT_MTL ? L"(no mtllib line)" : L"-");
    Char_Copy(state, stateCap, Char_ImportStateText(r->state));
}

static void Char_ImportColumns(int width)
{
    int w = width - Rs_Metric(g_char.modelFiles, SM_CXVSCROLL) - Rs_Px(4);
    int item = Char_TextWidth(g_char.modelFiles, L"Texture") + Rs_Px(16);
    int state = Char_TextWidth(g_char.modelFiles, L"State") + Rs_Px(16);
    int material = Char_TextWidth(g_char.modelFiles, L"Material") + Rs_Px(16);
    int name, i;

    g_char.filesW = width;
    for (i = 0; i < g_char.importRowCount; i++) {
        const struct CharImport *r = &g_char.importRow[i];
        wchar_t text[CHAR_VAL], st[48];
        int tw;
        Char_ImportTexts(r, text, CHAR_VAL, st, 48);
        tw = Char_TextWidth(g_char.modelFiles, st) + Rs_Px(16);
        if (tw > state)
            state = tw;
        tw = Char_TextWidth(g_char.modelFiles, r->material) + Rs_Px(16);
        if (tw > material)
            material = tw;
    }
    // Material takes at most a third of the room Item and State leave.
    if (material > (w - item - state) / 3)
        material = (w - item - state) / 3;
    name = w - item - state - material;
    if (name < Rs_Px(80))
        name = Rs_Px(80);
    ListView_SetColumnWidth(g_char.modelFiles, 0, item);
    ListView_SetColumnWidth(g_char.modelFiles, 1, name);
    ListView_SetColumnWidth(g_char.modelFiles, 2, material);
    ListView_SetColumnWidth(g_char.modelFiles, 3, state);
}

// Height of the list for this many rows: its head, the rows, the border.
static int Char_ImportListHeight(int rows)
{
    HWND head = ListView_GetHeader(g_char.modelFiles);
    int headH = Rs_Px(24), rowH = 0;
    RECT rc;

    if (head) {
        HDLAYOUT hl;
        WINDOWPOS wp;
        RECT all = { 0, 0, 1000, 1000 };
        memset(&wp, 0, sizeof(wp));
        hl.prc = &all;
        hl.pwpos = &wp;
        if (Header_Layout(head, &hl) && wp.cy > 0)
            headH = wp.cy;
    }
    if (ListView_GetItemCount(g_char.modelFiles) > 0 && ListView_GetItemRect(g_char.modelFiles, 0, &rc, LVIR_BOUNDS))
        rowH = rc.bottom - rc.top;
    if (rowH <= 0) {
        TEXTMETRICW tm;
        HDC dc = GetDC(g_char.modelFiles);
        HGDIOBJ old = SelectObject(dc, (HFONT)SendMessageW(g_char.modelFiles, WM_GETFONT, 0, 0));
        GetTextMetricsW(dc, &tm);
        SelectObject(dc, old);
        ReleaseDC(g_char.modelFiles, dc);
        rowH = tm.tmHeight + Rs_Px(4);
    }
    return headH + rows * rowH + 2 * GetSystemMetrics(SM_CYBORDER) + Rs_Px(2);
}

// The list anew from g_char.importRow.
static void Char_ImportFill(void)
{
    int i;
    SendMessageW(g_char.modelFiles, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_char.modelFiles);
    for (i = 0; i < g_char.importRowCount; i++) {
        const struct CharImport *r = &g_char.importRow[i];
        wchar_t name[CHAR_VAL], state[48];
        LVITEMW it;
        Char_ImportTexts(r, name, CHAR_VAL, state, 48);
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = i;
        it.pszText = (LPWSTR)g_charImportKinds[r->kind];
        it.lParam = i;
        ListView_InsertItem(g_char.modelFiles, &it);
        ListView_SetItemText(g_char.modelFiles, i, 1, name);
        ListView_SetItemText(g_char.modelFiles, i, 2, (LPWSTR)r->material);
        ListView_SetItemText(g_char.modelFiles, i, 3, state);
    }
    if (g_char.filesW > 0)
        Char_ImportColumns(g_char.filesW);
    SendMessageW(g_char.modelFiles, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_char.modelFiles, NULL, TRUE);
}

// The line: the format, for an OBJ its MTL, textures and groups, and where
// the colours came from. *problem = 1 when something was not found or read
// or the model has no colours (the line is amber then).
static void Char_ImportSummary(wchar_t *out, int cap, int *problem)
{
    int obj = wcscmp(g_char.importFormat, L"obj") == 0;
    int mtl = g_char.importTotal[CHAR_IMPORT_MTL], tex = g_char.importTotal[CHAR_IMPORT_TEXTURE];
    int groups = g_char.importTotal[CHAR_IMPORT_GROUP], mtlOk = 0, texOk = 0, listed = 0, i;
    const wchar_t *mtlState = L"";
    const wchar_t *c = g_char.importColors;
    wchar_t t[160];

    *problem = 0;
    for (i = 0; i < g_char.importRowCount; i++) {
        const struct CharImport *r = &g_char.importRow[i];
        if (Char_ImportProblem(r))
            *problem = 1;
        if (r->kind == CHAR_IMPORT_MTL) {
            if (!mtlState[0])
                mtlState = r->state;
            mtlOk += wcscmp(r->state, L"ok") == 0;
        } else if (r->kind == CHAR_IMPORT_TEXTURE) {
            texOk += wcscmp(r->state, L"ok") == 0;
        } else {
            listed++;
        }
    }
    Char_Copy(out, cap, obj ? L"OBJ" : L"PLY");
    if (obj) {
        t[0] = 0;
        if (mtl > 1)
            swprintf(t, 160, L": %d of %d MTL files found", mtlOk, mtl);
        else if (mtl == 1 && wcscmp(mtlState, L"none") == 0)
            Char_Copy(t, 160, L": no MTL");
        else if (mtl == 1)
            swprintf(t, 160, L": MTL %ls", Char_ImportStateText(mtlState));
        Char_Append(out, cap, t);
        if (tex) {
            swprintf(t, 160, L"%ls %d of %d texture%ls found", mtl ? L"," : L":", texOk, tex, tex == 1 ? L"" : L"s");
            Char_Append(out, cap, t);
        }
        if (groups) {
            swprintf(t, 160, L"%ls %d group%ls", (mtl || tex) ? L"," : L":", groups, groups == 1 ? L"" : L"s");
            Char_Append(out, cap, t);
        }
        if (listed < groups) {
            swprintf(t, 160, L" (%d listed)", listed);
            Char_Append(out, cap, t);
        }
    } else {
        Char_Append(out, cap, L", read by its content");
    }
    // Where the colours came from (mixed: from more than one of the three).
    if (wcscmp(c, L"vertex") == 0)
        Char_Append(out, cap, L" - colours: vertices");
    else if (wcscmp(c, L"material") == 0)
        Char_Append(out, cap, L" - colours: materials");
    else if (wcscmp(c, L"texture") == 0)
        Char_Append(out, cap, L" - colours: textures");
    else if (wcscmp(c, L"mixed") == 0)
        Char_Append(out, cap, L" - colours: several sources");
    else if (wcscmp(c, L"grey") == 0) {
        Char_Append(out, cap, L" - no colours: grey");
        *problem = 1;
    }
}

// After a check: takes over what it read and shows it - the line for every
// OBJ and for a model whose file is not named .ply, the list where there are
// rows (MTL, textures, groups).
static void Char_ApplyImport(void)
{
    struct CharJobData *j = &g_charJob;
    wchar_t model[CHAR_VAL], t[CHAR_VAL];
    int importOn, filesOn, problem = 0, i;

    Rs_Free(g_char.importRow);
    g_char.importRow = j->import;
    g_char.importRowCount = j->importCount;
    memcpy(g_char.importTotal, j->importLines, sizeof(g_char.importTotal));
    // rldpack lists at most 1024 groups and counts the rest in obj-groups.
    g_char.importTotal[CHAR_IMPORT_GROUP] += (int)j->groupsMore;
    j->import = NULL;
    j->importCount = j->importCap = 0;
    Char_Copy(g_char.importFormat, 8, j->modelFormat);
    Char_Copy(g_char.importColors, 16, j->modelColors);

    Char_FieldPath(g_char.model, model, CHAR_VAL);
    importOn = g_char.importFormat[0] &&
               (wcscmp(g_char.importFormat, L"ply") != 0 || !Char_EndsWith(model, L".ply"));
    // The list only with something the line does not say already: a file
    // (MTL, texture) or a group - not for an OBJ that names no MTL and has
    // no groups (its one row would be "no mtllib line").
    filesOn = 0;
    for (i = 0; importOn && i < g_char.importRowCount && !filesOn; i++)
        filesOn = g_char.importRow[i].kind != CHAR_IMPORT_MTL || wcscmp(g_char.importRow[i].state, L"none") != 0;
    if (importOn) {
        Char_ImportSummary(t, CHAR_VAL, &problem);
        g_char.importColor = problem ? RS_COL_WARNING : RS_COL_TEXT;
        Rs_SetText(g_char.modelImport, t);
        Rs_SetTextColor(g_char.modelImport, g_char.importColor);
        Rs_SetTip(g_char.modelImport, t);
    } else {
        Rs_SetText(g_char.modelImport, L"");
    }
    Char_ImportFill();
    // Laid out again whenever there is something to show: the list takes the
    // height its rows need, as far as the tab has room.
    if (importOn || importOn != g_char.importOn || filesOn != g_char.filesOn) {
        g_char.importOn = importOn;
        g_char.filesOn = filesOn;
        Char_Relayout(GetParent(g_char.modelImport));
    }
    Char_ObjUpdate(GetParent(g_char.modelImport));
}

// No model: nothing read.
static void Char_ImportClear(void)
{
    Rs_Free(g_char.importRow);
    g_char.importRow = NULL;
    g_char.importRowCount = 0;
    memset(g_char.importTotal, 0, sizeof(g_char.importTotal));
    g_char.importFormat[0] = 0;
    g_char.importColors[0] = 0;
    Rs_SetText(g_char.modelImport, L"");
    Char_ImportFill();
    if (g_char.importOn || g_char.filesOn) {
        g_char.importOn = g_char.filesOn = 0;
        Char_Relayout(GetParent(g_char.modelImport));
    }
}

// The model is an OBJ: by what the last check of this model read, else by its
// name.
static int Char_ModelIsObj(void)
{
    wchar_t model[CHAR_VAL];
    Char_FieldPath(g_char.model, model, CHAR_VAL);
    if (g_char.importFormat[0] && model[0] && _wcsicmp(model, g_char.checkModel) == 0)
        return wcscmp(g_char.importFormat, L"obj") == 0;
    return Char_EndsWith(model, L".obj");
}

// The entry chosen in a list of the card Import, 0 (the default) when none.
static int Char_ListIndex(HWND combo, int count)
{
    LRESULT sel = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    return (sel < 0 || sel >= count) ? 0 : (int)sel;
}

static int Char_VColorIndex(void)
{
    LRESULT sel = SendMessageW(g_char.vcolors, CB_GETCURSEL, 0, 0);
    return (sel < 0 || sel >= CHAR_VCOLORS_COUNT) ? 0 : (int)sel;
}

static void Char_ChangedSoon(HWND page);

// The field Textures folder: for an OBJ whose last check missed a texture,
// or with a folder set (a field being typed in is never taken away: this
// runs when the model changes and when a check ends).
static void Char_TexturesUpdate(HWND page)
{
    wchar_t dir[CHAR_VAL];
    int on = 0, i;

    Char_FieldPath(g_char.textures, dir, CHAR_VAL);
    if (g_char.objOn) {
        on = dir[0] != 0;
        for (i = 0; i < g_char.importRowCount && !on; i++)
            on = g_char.importRow[i].kind == CHAR_IMPORT_TEXTURE && wcscmp(g_char.importRow[i].state, L"missing") == 0;
    }
    if (on != g_char.texturesOn) {
        g_char.texturesOn = on;
        Char_Relayout(page);
    }
}

// The row "Native model" of the card Import. PREVIEW, open to everyone
// ("Preview feature" at the right, with and without --enable-preview-features;
// the user mode has no tick box, Char_UserHidden): enabled for an OBJ, empty
// at the start - not ticked, the command and the bytes of the character are
// those of before. Ticked, Char_MakeArgs passes --native-model on for an OBJ
// with Show kart wheels off (rldpack refuses it beside the kart wheels); the
// note says what the last check made of it (@char native-model,
// Char_NativeResult) or why it is not passed. Never stored in the settings.
#define CHAR_NATIVE_TEXT_ON  L"Preview: also writes the OBJ's own mesh, UVs and textures (CNET, CTXT; each texture a power of two from 16 to 2048) beside the classic model; needs Show kart wheels off or a wheel model (Extras, Wheels). The game draws it with NATIVE DRIVERS set to PREVIEW (OPTIONS, GRAPHICS)."
#define CHAR_NATIVE_TEXT_OBJ L"Only for an OBJ model with its materials."
// ---------------------------------------------------------------------------
// THE LOOK OF THE DRIVER (tab In-game look, card In the race; renderer package A)
// ---------------------------------------------------------------------------
//
// make-char --shadow retail|auto|off and --exhaust retail|off|x,y,z[;x,y,z]
// (tools/rldpack_char.inc, THE LOOK). PREVIEW, open to everyone ("Preview
// feature" in the title line of the card, with and without
// --enable-preview-features). rldpack's defaults follow the wheels
// (retail/retail with them, auto/off without them); the page passes what
// differs from them. Without the user mode the choices start at retail/retail
// whatever the wheels, so with the wheels hidden the page passes --shadow
// retail --exhaust retail until the author chooses: a character keeps the
// command and the bytes it had before the look. In the user mode
// (Rs_NativeForUsers) the choices start at rldpack's defaults and follow
// "Show kart wheels" until the author changes them. The
// points are game units of the built model, as rldpack info and @value
// exhaust-point give them; Pick takes the surface point under a click in
// the preview (pose Neutral, RsView_PickBegin). The preview draws the shadow
// quad (auto: @value shadow-quad of the last check; retail: the retail quad)
// and the points (retail: grey) at once from the fields; rldpack checks them.

#define CHAR_LOOK_NOTE_TEXT L"Points in game units of the built model (x left, y up, z forward), as rldpack info shows them. Pick: click the model in the preview."

static void Char_Changed(HWND page);

// The choices the author has not set: retail/retail; in the user mode
// rldpack's defaults (retail/retail, without the wheels auto/off).
static void Char_LookDefaults(void)
{
    const int follow = Rs_NativeForUsers() && !Char_IsChecked(g_char.wheels);
    if (!g_char.shadow)
        return;
    if (!g_char.shadowSet)
        SendMessageW(g_char.shadow, CB_SETCURSEL, follow ? 1 : 0, 0);
    if (!g_char.exhaustSet)
        SendMessageW(g_char.exhaust, CB_SETCURSEL, follow ? 2 : 0, 0);
}

// The point fields only for custom points.
static void Char_LookEnable(void)
{
    const int custom = Char_ListIndex(g_char.exhaust, 3) == 1;
    int i, a;
    for (i = 0; i < 2; i++) {
        Rs_SetTextColor(g_char.pointLabel[i], custom ? RS_COL_TEXT : RS_COL_MUTED);
        for (a = 0; a < 3; a++)
            EnableWindow(g_char.point[i][a], custom);
        EnableWindow(g_char.pointPick[i], custom);
    }
}

// A point of the fields in 1/16 game units: 1 = all three are numbers.
static int Char_LookPointValue(int i, int out[3])
{
    int a;
    for (a = 0; a < 3; a++) {
        wchar_t *text = Rs_GetText(g_char.point[i][a]);
        wchar_t *end = NULL;
        const double value = wcstod(text, &end);
        const int ok = end != text && *end == 0;
        Rs_Free(text);
        if (!ok)
            return 0;
        out[a] = (int)(value * 16.0 < 0.0 ? value * 16.0 - 0.5 : value * 16.0 + 0.5);
    }
    return 1;
}

// A point's fields are empty.
static int Char_LookPointEmpty(int i)
{
    int a, empty = 1;
    for (a = 0; a < 3; a++) {
        wchar_t *text = Rs_GetText(g_char.point[i][a]);
        empty = empty && !text[0];
        Rs_Free(text);
    }
    return empty;
}

// The fields of point i, from 1/16 game units (two decimals), as typed.
static void Char_LookPointSet(int i, const int v[3])
{
    wchar_t text[32];
    int a;
    g_char.applying = 1;
    for (a = 0; a < 3; a++) {
        swprintf(text, 32, L"%.2f", (double)v[a] / 16.0);
        Rs_SetText(g_char.point[i][a], text);
    }
    g_char.applying = 0;
}

// The preview follows the fields at once.
static void Char_LookPreview(void)
{
    int quad[4], pts[2][3], count = 0, shadow = 0, i;
    if (!g_char.view)
        return;
    switch (Char_ListIndex(g_char.shadow, 3)) {
    case 0:
        memcpy(quad, g_charRetailQuad, sizeof(quad));
        shadow = 1;
        break;
    case 1:
        memcpy(quad, g_char.lookQuad, sizeof(quad));
        shadow = g_char.lookQuadKnown;
        break;
    default:
        break;
    }
    switch (Char_ListIndex(g_char.exhaust, 3)) {
    case 0:
        memcpy(pts, g_charRetailPoints, sizeof(pts));
        count = 2;
        break;
    case 1:
        for (i = 0; i < 2; i++)
            if (Char_LookPointValue(i, pts[count]))
                count++;
        break;
    default:
        break;
    }
    RsView_SetLook(g_char.view, shadow, quad, count, pts, Char_ListIndex(g_char.exhaust, 3) == 0);
}

// Custom points for the first time: the fields start at the retail points.
static void Char_LookCustomStart(void)
{
    int i;
    for (i = 0; i < 2; i++)
        if (Char_LookPointEmpty(i))
            Char_LookPointSet(i, g_charRetailPoints[i]);
}

// The --exhaust value of the custom points: "x,y,z" or "x,y,z;x,y,z" as typed
// (point 2 only when it is not empty); rldpack says what is wrong with it.
static void Char_LookExhaustArg(wchar_t *out, int cap)
{
    int i, a;
    out[0] = 0;
    for (i = 0; i < 2; i++) {
        if (i == 1 && Char_LookPointEmpty(1))
            break;
        if (i == 1)
            Char_Append(out, cap, L";");
        for (a = 0; a < 3; a++) {
            wchar_t *text = Rs_GetText(g_char.point[i][a]);
            wchar_t clean[48];
            int n = 0, c;
            for (c = 0; text[c] && n < 47; c++)
                if (text[c] != L' ')
                    clean[n++] = text[c];
            clean[n] = 0;
            Rs_Free(text);
            if (a)
                Char_Append(out, cap, L",");
            Char_Append(out, cap, clean);
        }
    }
}

// The card of the tab In-game look.
static void Char_SelectLook(HWND page, int c)
{
    int i;
    if (c < 0 || c >= CHAR_LOOK_CARDS)
        return;
    g_char.lookCard = c;
    RsView_PickBegin(g_char.view, 0);
    for (i = 0; i < CHAR_LOOK_CARDS; i++)
        InvalidateRect(g_char.lookSwitch[i], NULL, FALSE);
    Char_Relayout(page);
}

// Pick point n (1 or 2) in the preview: pose Neutral, the note says how.
static void Char_LookPickStart(int n)
{
    wchar_t text[160];
    if (Char_ListIndex(g_char.exhaust, 3) != 1)
        return;
    SendMessageW(g_char.pose, CB_SETCURSEL, RS_VIEW_POSE_NEUTRAL, 0);
    g_char.poseNow = RS_VIEW_POSE_NEUTRAL;
    RsView_SetPose(g_char.view, RS_VIEW_POSE_NEUTRAL);
    RsView_PickBegin(g_char.view, n);
    swprintf(text, 160, L"Click the model in the preview where point %d goes (a right click cancels).", n);
    Rs_SetText(g_char.lookNote, text);
}

// The end of a pick (RS_VIEW_N_PICK): the point into its fields.
static void Char_LookPicked(HWND page)
{
    int v[3], n = 0;
    const int hit = RsView_PickResult(g_char.view, &n, v);
    if (n == 1 || n == 2) {
        if (hit) {
            Char_LookPointSet(n - 1, v);
            Rs_SetText(g_char.lookNote, CHAR_LOOK_NOTE_TEXT);
            if (Rs_Automating())
                Rs_AutoLog(L"  exhaust-pick: point %d at %.2f %.2f %.2f", n, (double)v[0] / 16.0, (double)v[1] / 16.0,
                           (double)v[2] / 16.0);
            Char_LookPreview();
            Char_Changed(page);
        } else {
            Rs_SetText(g_char.lookNote, L"The click missed the model - the point stays as it was. Pick again to try once more.");
            if (Rs_Automating())
                Rs_AutoLog(L"  exhaust-pick: point %d - the click missed the model", n);
        }
    }
}

// THE USER MODE (Rs_NativeForUsers, renderer step 6): no tick box - an OBJ
// with Show kart wheels off gets its native model beside the classic one.
// The game draws a native model only with the kart wheels hidden (rldpack
// refuses --native-model on beside them), so with the kart wheels an OBJ is
// built as the classic model only - no dialog, the line below the options
// says so; a PLY names no texture file and is the classic model only too.
#define CHAR_NATIVE_TEXT_USER   L"Native model: the OBJ's own mesh, UVs and textures, built beside the classic model (preview: the game draws it with NATIVE DRIVERS set to PREVIEW)."
#define CHAR_NATIVE_TEXT_PLY    L"Classic model only: a PLY names no texture file. Export the model as OBJ with its textures for the native model."
#define CHAR_NATIVE_TEXT_WHEELS L"Classic model only: the native model needs Show kart wheels off or a wheel model (Extras, Wheels)."

// The native model is built for an OBJ (ticked, or in the user mode) with
// Show kart wheels off, or with them and a wheel model of the card Wheels
// (its wheels then stand in for the kart wheels on the native model).
static int Char_NativeOn(void)
{
    return g_char.objOn && g_char.wheels && (Rs_NativeForUsers() || Char_IsChecked(g_char.native));
}

static int Char_NativePassed(void)
{
    return Char_NativeOn() && (!Char_IsChecked(g_char.wheels) || CharWheels_ModelPath(NULL, 0));
}

// The wheel model of the card Wheels goes to make-char (--wheel-model): with
// the native model and Show kart wheels on - off, there are no wheels at all.
static int Char_WheelPassed(wchar_t *path, int cap)
{
    return Char_NativePassed() && Char_IsChecked(g_char.wheels) && CharWheels_ModelPath(path, cap);
}

static void Char_NativeNote(const wchar_t *text)
{
    if (g_char.nativeHelp) {
        RECT rc;
        Rs_SetText(g_char.nativeHelp, text);
        // Compact: one line, cut with "..." and the whole text as the tooltip
        // when it is longer (Char_TextHeight sets both; the height stays).
        GetWindowRect(g_char.nativeHelp, &rc);
        if (g_char.compact && rc.right > rc.left)
            Char_TextHeight(g_char.nativeHelp, rc.right - rc.left, 3);
    }
    if (g_char.nativeLine && Rs_NativeForUsers())
        Rs_SetText(g_char.nativeLine, text);
}

// After every change of the model and before every run: enabled or not, and
// the note of what will be passed.
static void Char_NativeUpdate(void)
{
    const int on = g_char.objOn;
    if (!g_char.native)
        return;
    CharWheels_PageState(g_char.objOn, Char_IsChecked(g_char.wheels), Char_NativeOn(), Char_WheelPassed(NULL, 0),
                         Char_ListIndex(g_char.up, CHAR_UP_COUNT) != 0, Char_ListIndex(g_char.forward, CHAR_FORWARD_COUNT) != 0);
    // The Textures folder as Char_MakeArgs passes it (an OBJ only): char-wheel
    // looks there for the wheel's texture as make-char does.
    {
        wchar_t dir[CHAR_VAL];
        Char_FieldPath(g_char.textures, dir, CHAR_VAL);
        CharWheels_SetTextures(g_char.objOn ? dir : L"");
    }
    EnableWindow(g_char.native, on ? TRUE : FALSE);
    Rs_SetTextColor(g_char.nativeLabel, on ? RS_COL_TEXT : RS_COL_MUTED);
    if (Char_NativePassed() && g_charJob.nativeText[0])
        Char_NativeNote(g_charJob.nativeText);
    else if (Rs_NativeForUsers())
        Char_NativeNote(!g_char.objOn ? CHAR_NATIVE_TEXT_PLY : Char_NativePassed() ? CHAR_NATIVE_TEXT_USER : CHAR_NATIVE_TEXT_WHEELS);
    else if (g_char.objOn && Char_IsChecked(g_char.native) && Char_IsChecked(g_char.wheels))
        Char_NativeNote(CHAR_NATIVE_TEXT_WHEELS);
    else if (!g_char.objOn)
        Char_NativeNote(CHAR_NATIVE_TEXT_OBJ);
    else
        Char_NativeNote(CHAR_NATIVE_TEXT_ON);
}

// The user mode (Rs_NativeForUsers): the hidden choices back at rldpack's
// defaults, Reduce to fit on (nothing passed: --reduce auto).
static void Char_UserDefaults(void)
{
    if (!Rs_NativeForUsers())
        return;
    Char_SetChecked(g_char.repair, 1);
    Char_SetChecked(g_char.openParts, 1);
    Char_SetChecked(g_char.remesh, 0);
    Char_SetChecked(g_char.reduce, 1);
    Char_SetChecked(g_char.native, 0);
    SendMessageW(g_char.colors, CB_SETCURSEL, 0, 0);
}

// Controls the user mode hides (Rs_NativeForUsers): the choices it fixes to
// rldpack's defaults (repair, open parts two-sided, no remesh, reduce auto,
// 128 colours) and the tick box of the native model; and those it alone
// shows. 1 = hidden in the mode the page runs in.
static int Char_UserHidden(HWND c)
{
    if (c == g_char.fallback || c == g_char.fallbackHint || c == g_char.nativeLine)
        return !Rs_NativeForUsers();
    if (!Rs_NativeForUsers())
        return 0;
    return c == g_char.repair || c == g_char.openParts || c == g_char.remesh || c == g_char.reduce ||
           c == g_char.quality || c == g_char.reduceFit || c == g_char.colorsLabel || c == g_char.colors ||
           c == g_char.colorsHelp || c == g_char.native;
}

// @char native-model <vertices> <triangles> <poses> <materials> <textures>
// <wheels|hidden> <bytes> of the run shown (always hidden: the page passes
// --native-model on only with the kart wheels hidden).
// Kept with the run (g_charJob.nativeText): the first check of a model learns
// only at its end that the model is an OBJ (Char_ObjUpdate), and the note
// written then (Char_NativeUpdate) shows the result kept.
static void Char_NativeResult(wchar_t **f, int n)
{
    if (n < 9)
        return;
    swprintf(g_charJob.nativeText, 256, L"Native model: %ls vertices, %ls triangles, %ls poses, %ls materials, %ls textures, %ls - %ls bytes (preview).",
             f[2], f[3], f[4], f[5], f[6], wcscmp(f[7], L"wheels") == 0 ? L"with wheels" : L"no wheels", f[8]);
    if (Char_NativePassed())
        Char_NativeNote(g_charJob.nativeText);
}

// The choices of an OBJ are shown only for an OBJ; the page is laid out
// again when that changes. A choice not passed so far (the model turned out
// an OBJ by its content) is checked with.
static void Char_ObjUpdate(HWND page)
{
    int obj = Char_ModelIsObj();
    wchar_t dir[CHAR_VAL];
    if (obj == g_char.objOn) {
        Char_TexturesUpdate(page);
        return;
    }
    g_char.objOn = obj;
    Char_NativeUpdate();
    Char_TexturesUpdate(page);
    Char_Relayout(page);
    Char_FieldPath(g_char.textures, dir, CHAR_VAL);
    if (obj && (Char_VColorIndex() != 0 || dir[0]))
        Char_ChangedSoon(page);
}

// ---------------------------------------------------------------------------
// Stamps of the files a check read: a 64-bit FNV-1a hash over the path (in
// small letters), the size and the time of writing of each file - a model
// exported again changes it, a file only opened does not.
// ---------------------------------------------------------------------------

#define CHAR_STAMP_START 14695981039346656037ULL

static unsigned long long Char_StampBytes(unsigned long long h, const void *data, size_t n)
{
    const unsigned char *p = (const unsigned char *)data;
    size_t i;
    for (i = 0; i < n; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static unsigned long long Char_StampPath(unsigned long long h, const wchar_t *path)
{
    for (; *path; path++) {
        wchar_t c = towlower(*path);
        if (c == L'/')
            c = L'\\';
        h = Char_StampBytes(h, &c, sizeof(c));
    }
    return Char_StampBytes(h, L"", sizeof(wchar_t));
}

// One file: its path, and its size and time of writing (or that it is not
// there).
static unsigned long long Char_StampFile(unsigned long long h, const wchar_t *path)
{
    WIN32_FILE_ATTRIBUTE_DATA fa;
    h = Char_StampPath(h, path);
    if (path[0] && GetFileAttributesExW(path, GetFileExInfoStandard, &fa)) {
        h = Char_StampBytes(h, &fa.nFileSizeHigh, sizeof(fa.nFileSizeHigh));
        h = Char_StampBytes(h, &fa.nFileSizeLow, sizeof(fa.nFileSizeLow));
        h = Char_StampBytes(h, &fa.ftLastWriteTime, sizeof(fa.ftLastWriteTime));
    } else {
        h = Char_StampBytes(h, "none", 4);
    }
    return h;
}

// A folder: every file in it (not below it), in any order.
static unsigned long long Char_StampFolder(unsigned long long h, const wchar_t *dir)
{
    wchar_t pattern[CHAR_VAL];
    WIN32_FIND_DATAW fd;
    HANDLE find;
    unsigned long long sum = 0, count = 0;

    h = Char_StampPath(h, dir);
    if (!dir[0])
        return h;
    Rs_PathJoin(pattern, CHAR_VAL, dir, L"*");
    find = FindFirstFileW(pattern, &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            unsigned long long one;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            one = Char_StampPath(CHAR_STAMP_START, fd.cFileName);
            one = Char_StampBytes(one, &fd.nFileSizeHigh, sizeof(fd.nFileSizeHigh));
            one = Char_StampBytes(one, &fd.nFileSizeLow, sizeof(fd.nFileSizeLow));
            one = Char_StampBytes(one, &fd.ftLastWriteTime, sizeof(fd.ftLastWriteTime));
            sum += one;
            count++;
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    h = Char_StampBytes(h, &sum, sizeof(sum));
    return Char_StampBytes(h, &count, sizeof(count));
}

// The files of the fields: the model, the icon, the voices folder; and while
// the wheel model of the card Wheels is passed, how often its files were
// written since (CharWheels_Generation): a result kept is not shown again for
// a wheel exported again.
static unsigned long long Char_StampFields(const wchar_t *model, const wchar_t *icon, const wchar_t *voices)
{
    unsigned long long h = CHAR_STAMP_START;
    h = Char_StampFile(h, model);
    h = Char_StampFile(h, icon);
    if (Char_WheelPassed(NULL, 0)) {
        const int generation = CharWheels_Generation();
        h = Char_StampBytes(h, &generation, sizeof(generation));
    }
    return Char_StampFolder(h, voices);
}

// A list of files "a\0b\0\0" (NULL = none).
static unsigned long long Char_StampList(const wchar_t *list)
{
    unsigned long long h = CHAR_STAMP_START;
    for (; list && *list; list += wcslen(list) + 1)
        h = Char_StampFile(h, list);
    return h;
}

static void Char_ListAdd(wchar_t **list, size_t *len, size_t *cap, const wchar_t *path)
{
    size_t n = wcslen(path);
    if (!n)
        return;
    if (*len + n + 2 > *cap) {
        size_t grown = *cap ? *cap * 2 : 1024;
        wchar_t *p;
        while (grown < *len + n + 2)
            grown *= 2;
        p = Rs_Alloc(grown * sizeof(wchar_t));
        if (*len)
            memcpy(p, *list, *len * sizeof(wchar_t));
        Rs_Free(*list);
        *list = p;
        *cap = grown;
    }
    memcpy(*list + *len, path, n * sizeof(wchar_t));
    *len += n;
    (*list)[(*len)++] = 0;
    (*list)[*len] = 0;
}

// The places rldpack looks for a texture that is not at its path, by its
// name, into the list: in dir and its folders textures, tex, images and maps,
// with its own ending and as .png, .jpg, .jpeg, .tga and .bmp.
static void Char_ListPlaces(wchar_t **list, size_t *len, size_t *cap, const wchar_t *dir, const wchar_t *name)
{
    static const wchar_t *const subs[] = { L"", L"textures", L"tex", L"images", L"maps" };
    static const wchar_t *const ends[] = { L"", L".png", L".jpg", L".jpeg", L".tga", L".bmp" };
    wchar_t base[CHAR_VAL], folder[CHAR_VAL], file[CHAR_VAL], path[CHAR_VAL];
    wchar_t *dot;
    int s, e;

    Char_Copy(base, CHAR_VAL, name);
    dot = wcsrchr(base, L'.');
    for (s = 0; s < (int)(sizeof(subs) / sizeof(subs[0])); s++) {
        if (subs[s][0])
            Rs_PathJoin(folder, CHAR_VAL, dir, subs[s]);
        else
            Char_Copy(folder, CHAR_VAL, dir);
        for (e = 0; e < (int)(sizeof(ends) / sizeof(ends[0])); e++) {
            Char_Copy(file, CHAR_VAL, base);
            if (ends[e][0]) {
                if (dot)
                    file[dot - base] = 0;
                Char_Append(file, CHAR_VAL, ends[e]);
            }
            Rs_PathJoin(path, CHAR_VAL, folder, file);
            Char_ListAdd(list, len, cap, path);
        }
    }
}

// The MTL and texture files of the last check (g_char.importRow): each MTL,
// each texture as rldpack names it and, for a texture not found, the places
// rldpack looks for it by its name (Char_ListPlaces) - next to each MTL and
// the model, in the textures folder of the field - where a texture delivered
// later is put. "a\0b\0\0", Rs_Free; NULL = none.
static wchar_t *Char_ImportFiles(void)
{
    wchar_t *list = NULL;
    size_t len = 0, cap = 0;
    wchar_t dir[CHAR_VAL], model[CHAR_VAL], textures[CHAR_VAL];
    int i, k;

    Char_FieldPath(g_char.model, model, CHAR_VAL);
    Rs_PathDir(model, CHAR_VAL, model);
    Char_FieldPath(g_char.textures, textures, CHAR_VAL);
    for (i = 0; i < g_char.importRowCount; i++) {
        const struct CharImport *r = &g_char.importRow[i];
        if (r->kind == CHAR_IMPORT_GROUP || !r->path[0])
            continue;
        Char_ListAdd(&list, &len, &cap, r->path);
        if (r->kind != CHAR_IMPORT_TEXTURE || wcscmp(r->state, L"ok") == 0)
            continue;
        for (k = 0; k < g_char.importRowCount; k++) {
            const struct CharImport *m = &g_char.importRow[k];
            if (m->kind != CHAR_IMPORT_MTL || !m->path[0])
                continue;
            Rs_PathDir(dir, CHAR_VAL, m->path);
            Char_ListPlaces(&list, &len, &cap, dir, Rs_PathName(r->path));
        }
        if (model[0])
            Char_ListPlaces(&list, &len, &cap, model, Rs_PathName(r->path));
        if (textures[0])
            Char_ListPlaces(&list, &len, &cap, textures, Rs_PathName(r->path));
    }
    return list;
}

// The stamps of the check that has just been shown.
static void Char_StampShown(void)
{
    Rs_Free(g_char.stampFiles);
    g_char.stampFiles = Char_ImportFiles();
    g_char.stampImport = Char_StampList(g_char.stampFiles);
    g_char.stampFields = g_char.stampRun;
    g_char.stampKnown = 1;
}

static void Char_ChangedSoon(HWND page);
static void Char_WheelsFollow(HWND page);

// When Reload Studio is active again or the page is shown again: a file the
// check shown read has been written since (a model exported again, a texture
// delivered) - check again. Not while a check waits or runs: it reads the
// files anew anyway. The card Wheels compares the files of its wheel model
// first (CharWheels_FilesChanged): one written since is read again at once,
// and the page checks again as after a click. Returns 1 if a check was
// asked for.
static int Char_FilesChanged(HWND page, const wchar_t *why)
{
    wchar_t model[CHAR_VAL], icon[CHAR_VAL], voices[CHAR_VAL];

    if (CharWheels_FilesChanged(page, why))
        Char_WheelsFollow(page);
    if (!g_char.stampKnown || g_char.jobId || g_char.timer)
        return 0;
    Char_FieldPath(g_char.model, model, CHAR_VAL);
    if (!model[0])
        return 0;
    Char_FieldPath(g_char.icon, icon, CHAR_VAL);
    Char_FieldPath(g_char.voices, voices, CHAR_VAL);
    if (Char_StampFields(model, icon, voices) == g_char.stampFields &&
        Char_StampList(g_char.stampFiles) == g_char.stampImport)
        return 0;
    if (Rs_Automating())
        Rs_AutoLog(L"  files: written since the last check (%ls) - checking again", why);
    Char_ChangedSoon(page);
    return 1;
}

// The main window's WM_ACTIVATEAPP, to the page (CHAR_WM_ACTIVATED).
static LRESULT CALLBACK Char_MainSub(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data)
{
    (void)id;
    if (msg == WM_ACTIVATEAPP && wParam)
        PostMessageW((HWND)data, CHAR_WM_ACTIVATED, 0, 0);
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// The list: the state amber where a file was not found or not read, the
// whole path and the material as the tooltip of a row.
static LRESULT Char_ImportNotify(NMHDR *hdr)
{
    switch (hdr->code) {
    case NM_CUSTOMDRAW: {
        NMLVCUSTOMDRAW *cd = (NMLVCUSTOMDRAW *)hdr;
        if (cd->nmcd.dwDrawStage == CDDS_PREPAINT)
            return CDRF_NOTIFYITEMDRAW;
        if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
            return CDRF_NOTIFYSUBITEMDRAW;
        if (cd->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
            int row = (int)cd->nmcd.lItemlParam;
            if (cd->iSubItem == 3 && row >= 0 && row < g_char.importRowCount &&
                Char_ImportProblem(&g_char.importRow[row]))
                cd->clrText = RS_COL_WARNING;
            else
                cd->clrText = ListView_GetTextColor(g_char.modelFiles);
            return CDRF_NEWFONT;
        }
        return CDRF_DODEFAULT;
    }
    case LVN_GETINFOTIPW: {
        NMLVGETINFOTIPW *tip = (NMLVGETINFOTIPW *)hdr;
        LVITEMW it;
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_PARAM;
        it.iItem = tip->iItem;
        if (tip->pszText && tip->cchTextMax > 0 && ListView_GetItem(g_char.modelFiles, &it) && it.lParam >= 0 &&
            it.lParam < g_char.importRowCount) {
            const struct CharImport *r = &g_char.importRow[it.lParam];
            wchar_t name[CHAR_VAL], state[48];
            Char_ImportTexts(r, name, CHAR_VAL, state, 48);
            if (r->kind == CHAR_IMPORT_TEXTURE)
                swprintf(tip->pszText, (size_t)tip->cchTextMax, L"%ls\nTexture of the material %ls: %ls", r->path,
                         r->material, state);
            else if (r->kind == CHAR_IMPORT_MTL)
                swprintf(tip->pszText, (size_t)tip->cchTextMax, L"%ls\nMaterial file: %ls", r->path[0] ? r->path : name,
                         state);
            else
                swprintf(tip->pszText, (size_t)tip->cchTextMax, L"Group %ls: %ls (for information only)", name, state);
            tip->pszText[tip->cchTextMax - 1] = 0;
        }
        return 0;
    }
    }
    return 0;
}

// The line below the slider: what 100 % is for this model (@char fit).
static void Char_ApplyFit(void)
{
    struct CharJobData *j = &g_charJob;
    wchar_t t[256];

    if (j->fitSeen) {
        // With a kart its length was matched to Crash's (the driver may stick
        // out); without one the whole model. With --wheels off rldpack always
        // takes the whole model, so "model" means "no kart" only while the
        // wheels are on. The factor as received (4 significant digits: a
        // model in millimeters is not "x0.00").
        const int wheelsOff = wcsncmp(j->wheels, L"off", 3) == 0;
        const wchar_t *basis = (wcscmp(j->fitBasis, L"model") == 0 && !wheelsOff) ? L" - no kart" : L"";
        swprintf(t, 256, L"Fitted to Crash size: x%ls (%ls -> %ls long)%ls", j->fitFactor, j->fitLength[0],
                 j->fitLength[1], basis);
        Char_SetLabel(g_char.sizeFit, t, RS_COL_TEXT, &g_char.fitColor);
    } else if (wcsncmp(j->fit, L"none", 4) == 0) {
        Char_SetLabel(g_char.sizeFit, L"Not fitted - the model keeps the size it was exported in.", RS_COL_MUTED,
                      &g_char.fitColor);
    } else if (j->modelSeen && wcscmp(j->modelState, L"ok") == 0) {
        Char_SetLabel(g_char.sizeFit, L"Not fitted - see the messages.", RS_COL_MUTED, &g_char.fitColor);
    } else {
        Char_SetLabel(g_char.sizeFit, CHAR_FIT_WAIT_TEXT, RS_COL_MUTED, &g_char.fitColor);
    }
}

// ---------------------------------------------------------------------------
// Voices (tab 4): the files of the folder as the last check reported them
// ---------------------------------------------------------------------------

// What a state of @voice means, for the column Event; NULL = ok.
static const wchar_t *Char_VoiceStateText(const wchar_t *state)
{
    if (wcscmp(state, L"ok") == 0)
        return NULL;
    if (wcscmp(state, L"cut") == 0)
        return L"cut";
    if (wcscmp(state, L"bad") == 0)
        return L"cannot be read";
    if (wcscmp(state, L"unknown") == 0)
        return L"name not known";
    if (wcscmp(state, L"ignored") == 0)
        return L"not a voice file";
    if (wcscmp(state, L"unused") == 0)
        return L"not used";
    return state;
}

// The column Event of a row: the event (or "-"), and what is wrong with the file.
static void Char_VoiceEventText(const struct CharVoice *v, wchar_t *out, int cap)
{
    const wchar_t *state = Char_VoiceStateText(v->state);
    const wchar_t *event = v->event == CHAR_VOICE_NONE ? L"-" : g_charVoiceEvents[v->event].text;
    if (state)
        swprintf(out, cap, L"%ls (%ls)", event, state);
    else
        Char_Copy(out, cap, event);
}

static void Char_VoiceLengthText(const struct CharVoice *v, wchar_t *out, int cap)
{
    if (v->ms < 0 || v->rate <= 0)     // not read
        Char_Copy(out, cap, L"-");
    else
        swprintf(out, cap, L"%ld.%02ld s", v->ms / 1000, (v->ms % 1000) / 10);
}

// Row of the file name (case does not matter, as for rldpack), -1 = not there.
static int Char_VoiceFind(const wchar_t *name)
{
    int i;
    for (i = 0; i < g_char.voiceRowCount; i++)
        if (_wcsicmp(g_char.voiceRow[i].name, name) == 0)
            return i;
    return -1;
}

// The row chosen in the list, -1 = none.
static int Char_VoiceSelected(void)
{
    int item = ListView_GetNextItem(g_char.voiceList, -1, LVNI_SELECTED);
    LVITEMW it;
    if (item < 0)
        return -1;
    memset(&it, 0, sizeof(it));
    it.mask = LVIF_PARAM;
    it.iItem = item;
    if (!ListView_GetItem(g_char.voiceList, &it) || it.lParam < 0 || it.lParam >= g_char.voiceRowCount)
        return -1;
    return (int)it.lParam;
}

// The files given an event of their own (--voice), kept only while they are
// in the folder of the last check.
static int Char_VoiceSetFind(const wchar_t *name)
{
    int i;
    for (i = 0; i < g_char.voiceSetCount; i++)
        if (_wcsicmp(g_char.voiceSet[i].name, name) == 0)
            return i;
    return -1;
}

// The event of row as the page will pass it: its own (--voice), else the one
// rldpack reported by its name.
static int Char_VoiceOwnEvent(int row)
{
    int i = Char_VoiceSetFind(g_char.voiceRow[row].name);
    return i >= 0 ? g_char.voiceSet[i].event : g_char.voiceRow[row].event;
}

// A file rldpack does not take as a voice at all (not a .wav or .vag) gets no
// event: the choice stays grey - unless it has one of its own, to take it back.
static int Char_VoiceCanChoose(int row)
{
    return wcscmp(g_char.voiceRow[row].state, L"ignored") != 0 || Char_VoiceSetFind(g_char.voiceRow[row].name) >= 0;
}

// "Event" and "Play" follow the row chosen: greyed out without one, Play also
// without a preview file of it.
static void Char_VoiceSelShow(void)
{
    int row = Char_VoiceSelected();
    int building = g_char.jobId && g_char.jobKind == CHAR_JOB_BUILD;
    if (row >= 0) {
        Char_Copy(g_char.voiceSel, CHAR_VOICE_NAME, g_char.voiceRow[row].name);
        SendMessageW(g_char.voiceEvent, CB_SETCURSEL, (WPARAM)Char_VoiceOwnEvent(row), 0);
    } else {
        g_char.voiceSel[0] = 0;
        SendMessageW(g_char.voiceEvent, CB_SETCURSEL, (WPARAM)-1, 0);
    }
    EnableWindow(g_char.voiceEvent, row >= 0 && !building && Char_VoiceCanChoose(row));
    EnableWindow(g_char.voicePlay, row >= 0 && g_char.voiceRow[row].preview != NULL);
}

static void Char_VoiceSelect(int row)
{
    int i, n = ListView_GetItemCount(g_char.voiceList);
    ListView_SetItemState(g_char.voiceList, -1, 0, LVIS_SELECTED);
    for (i = 0; i < n; i++) {
        LVITEMW it;
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_PARAM;
        it.iItem = i;
        if (ListView_GetItem(g_char.voiceList, &it) && it.lParam == row) {
            ListView_SetItemState(g_char.voiceList, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(g_char.voiceList, i, FALSE);
            break;
        }
    }
    Char_VoiceSelShow();
}

// The list anew from g_char.voiceRow; the file chosen before stays chosen.
static void Char_VoiceFill(void)
{
    wchar_t keep[CHAR_VOICE_NAME];
    int i, again = -1;

    Char_Copy(keep, CHAR_VOICE_NAME, g_char.voiceSel);
    g_char.voiceFilling = 1;
    SendMessageW(g_char.voiceList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_char.voiceList);
    for (i = 0; i < g_char.voiceRowCount; i++) {
        const struct CharVoice *v = &g_char.voiceRow[i];
        wchar_t length[32], size[32], event[96];
        LVITEMW it;
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = i;
        it.pszText = (LPWSTR)v->name;
        it.lParam = i;
        ListView_InsertItem(g_char.voiceList, &it);
        Char_VoiceLengthText(v, length, 32);
        if (v->bytes < 0)
            Char_Copy(size, 32, L"-");
        else
            Char_SizeText(size, 32, v->bytes);
        Char_VoiceEventText(v, event, 96);
        ListView_SetItemText(g_char.voiceList, i, 1, length);
        ListView_SetItemText(g_char.voiceList, i, 2, size);
        ListView_SetItemText(g_char.voiceList, i, 3, event);
        if (keep[0] && _wcsicmp(keep, v->name) == 0)
            again = i;
    }
    if (g_char.voiceListW > 0)
        Char_VoiceColumns(g_char.voiceListW);
    SendMessageW(g_char.voiceList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_char.voiceList, NULL, TRUE);
    g_char.voiceFilling = 0;
    if (again >= 0)
        Char_VoiceSelect(again);
    else
        Char_VoiceSelShow();
}

// The overview of the ten events: its text (the controls list and the report
// read it) and its picture.
static void Char_VoiceEventsShow(void)
{
    wchar_t t[512];
    int e;
    t[0] = 0;
    for (e = 0; e < CHAR_VOICE_EVENTS; e++) {
        wchar_t one[48];
        int n = g_char.voiceKnown ? g_char.voiceEventCount[e] : 0;
        if (n > CHAR_VOICE_PER_EVENT)
            swprintf(one, 48, L"%ls%ls %d too many", e ? L", " : L"", g_charVoiceEvents[e].text, n);
        else if (n > 0)
            swprintf(one, 48, L"%ls%ls %d", e ? L", " : L"", g_charVoiceEvents[e].text, n);
        else
            swprintf(one, 48, L"%ls%ls silent", e ? L", " : L"", g_charVoiceEvents[e].text);
        Char_Append(t, 512, one);
    }
    SetWindowTextW(g_char.voiceEvents, t);
    InvalidateRect(g_char.voiceEvents, NULL, FALSE);
}

// The ten events in two rows of five: a green circle with the number of
// clips, or a grey ring and a grey name for an event that stays silent.
static void Char_VoiceEventsPaint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc, cell, r;
    HBRUSH br;
    HGDIOBJ oldFont;
    int e, c, rowH, d = Rs_Px(16), left[6], need = 0;

    GetClientRect(hwnd, &rc);
    br = CreateSolidBrush(RS_COL_CARD);
    FillRect(dc, &rc, br);
    DeleteObject(br);
    rowH = rc.bottom / 2;
    SetBkMode(dc, TRANSPARENT);
    oldFont = SelectObject(dc, Rs_Font(RS_FONT_SMALL));
    // Each column as wide as its longer name needs, the rest shared out;
    // equal columns (names cut with "...") where even that does not fit.
    for (c = 0; c < 5; c++) {
        SIZE a, b;
        GetTextExtentPoint32W(dc, g_charVoiceEvents[c].text, (int)wcslen(g_charVoiceEvents[c].text), &a);
        GetTextExtentPoint32W(dc, g_charVoiceEvents[c + 5].text, (int)wcslen(g_charVoiceEvents[c + 5].text), &b);
        left[c + 1] = d + Rs_Px(6 + 10) + (a.cx > b.cx ? a.cx : b.cx);
        need += left[c + 1];
    }
    left[0] = 0;
    for (c = 0; c < 5; c++)
        left[c + 1] = left[c] + (need <= rc.right ? left[c + 1] + (rc.right - need) / 5 : rc.right / 5);
    for (e = 0; e < CHAR_VOICE_EVENTS; e++) {
        int n = g_char.voiceKnown ? g_char.voiceEventCount[e] : 0;
        // Green: said in the game; red: more clips than an event takes (the
        // build stops); grey: silent, or Passing, never heard.
        COLORREF fill = n > CHAR_VOICE_PER_EVENT ? RS_COL_ERROR
                        : (n > 0 && e != CHAR_VOICE_PASSING) ? RS_COL_OK
                                                             : RS_COL_CARD;
        COLORREF ring = fill == RS_COL_CARD ? RS_COL_MUTED : fill;
        HPEN pen = CreatePen(PS_SOLID, Rs_Px(1) > 1 ? Rs_Px(1) : 1, ring);
        HGDIOBJ oldBr, oldPen;
        int cx, cy;
        SetRect(&cell, left[e % 5], (e / 5) * rowH, left[e % 5 + 1], (e / 5) * rowH + rowH);
        cx = cell.left;
        cy = (cell.top + cell.bottom - d) / 2;
        br = CreateSolidBrush(fill);
        oldBr = SelectObject(dc, br);
        oldPen = SelectObject(dc, pen);
        Ellipse(dc, cx, cy, cx + d + 1, cy + d + 1);
        SelectObject(dc, oldBr);
        SelectObject(dc, oldPen);
        DeleteObject(br);
        DeleteObject(pen);
        if (n > 0) {
            wchar_t num[8];
            swprintf(num, 8, L"%d", n);
            SetTextColor(dc, fill == RS_COL_CARD ? RS_COL_MUTED : RS_COL_CARD);
            SetRect(&r, cx, cy, cx + d + 1, cy + d + 1);
            DrawTextW(dc, num, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        SetTextColor(dc, fill == RS_COL_CARD ? RS_COL_MUTED : fill == RS_COL_ERROR ? RS_COL_ERROR : RS_COL_TEXT);
        SetRect(&r, cx + d + Rs_Px(6), cell.top, cell.right - Rs_Px(2), cell.bottom);
        DrawTextW(dc, g_charVoiceEvents[e].text, -1, &r,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    SelectObject(dc, oldFont);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK Char_VoiceEventsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_PAINT) {
        Char_VoiceEventsPaint(hwnd);
        return 0;
    }
    if (msg == WM_ERASEBKGND)
        return 1;
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// The clips of the last check the driver says in the game (Passing is never
// heard) and the events they fill. Returns the clips.
static int Char_VoicesHeard(int *clips, int *events)
{
    int e;
    *clips = 0;
    *events = 0;
    if (!g_char.voiceKnown)
        return 0;
    for (e = 0; e < CHAR_VOICE_EVENTS; e++) {
        if (e == CHAR_VOICE_PASSING || g_char.voiceEventCount[e] <= 0)
            continue;
        *clips += g_char.voiceEventCount[e];
        (*events)++;
    }
    return *clips;
}

// The line below the folder: what the last check found in it.
static void Char_VoiceNote(void)
{
    wchar_t voices[CHAR_VAL];
    wchar_t t[256];
    COLORREF color = RS_COL_MUTED;
    int heard, events;

    Char_FieldPath(g_char.voices, voices, CHAR_VAL);
    if (!voices[0]) {
        Char_Copy(t, 256, CHAR_VOICES_TEXT);
    } else if (!g_char.modelRead && !g_char.voiceKnown) {
        Char_Copy(t, 256, L"Choose a model on the tab Model: its check reads the voice files.");
    } else if (!g_char.voiceKnown) {
        Char_Copy(t, 256, L"The voice folder was not read - see the messages.");
        color = RS_COL_WARNING;
    } else if (g_char.tabErrors[CHAR_TAB_VOICES] > 0) {
        Char_Copy(t, 256, L"The voices cannot be packed - see the messages in red.");
        color = RS_COL_ERROR;
    } else if (Char_VoicesHeard(&heard, &events) == 0) {
        Char_Copy(t, 256, L"No clip the driver says in the game - it stays silent. See the messages.");
        color = RS_COL_WARNING;
    } else {
        swprintf(t, 256, L"%d clip%ls for %d of %d events the driver says in the game; grey ones stay silent.",
                 heard, heard == 1 ? L"" : L"s", events, CHAR_VOICE_EVENTS - 1);
    }
    Char_SetLabel(g_char.voicesNote, t, color, &g_char.voicesColor);
}

// No voices to show (no folder, no model): the list empties, the preview
// files of the last check go.
static void Char_VoicesClear(void)
{
    Char_VoicesFree(g_char.voiceRow, g_char.voiceRowCount);
    g_char.voiceRow = NULL;
    g_char.voiceRowCount = 0;
    g_char.voiceKnown = 0;
    g_char.voiceClips = g_char.voiceFilled = 0;
    memset(g_char.voiceEventCount, 0, sizeof(g_char.voiceEventCount));
    if (!Char_CacheHasSeq(g_char.voiceSeq, -1))
        Char_TempDeleteVoices(g_char.voiceSeq);
    g_char.voiceSeq = 0;
    Char_VoiceFill();
    Char_VoiceEventsShow();
    Char_VoiceNote();
}

// The end of a check: its voices into the tab. seq: its temp number - its
// preview files stay while the list shows them.
static void Char_ApplyVoices(int seq)
{
    struct CharJobData *j = &g_charJob;
    int i;

    Char_VoicesFree(g_char.voiceRow, g_char.voiceRowCount);
    g_char.voiceRow = j->voice;
    g_char.voiceRowCount = j->voiceCount;
    j->voice = NULL;
    j->voiceCount = j->voiceCap = 0;
    g_char.voiceKnown = j->voicesSeen;
    g_char.voiceClips = j->voiceClips;
    g_char.voiceFilled = j->voiceFilled;
    if (j->voiceEventSeen)
        memcpy(g_char.voiceEventCount, j->voiceEvent, sizeof(g_char.voiceEventCount));
    else
        memset(g_char.voiceEventCount, 0, sizeof(g_char.voiceEventCount));
    if (g_char.voiceSeq != seq && !Char_CacheHasSeq(g_char.voiceSeq, -1))
        Char_TempDeleteVoices(g_char.voiceSeq);
    g_char.voiceSeq = seq;
    // A file that has gone from the folder keeps no event of its own.
    if (j->voicesSeen) {
        for (i = g_char.voiceSetCount - 1; i >= 0; i--) {
            if (Char_VoiceFind(g_char.voiceSet[i].name) < 0) {
                memmove(&g_char.voiceSet[i], &g_char.voiceSet[i + 1],
                        sizeof(g_char.voiceSet[0]) * (size_t)(g_char.voiceSetCount - i - 1));
                g_char.voiceSetCount--;
            }
        }
    }
    Char_VoiceFill();
    Char_VoiceEventsShow();
    Char_VoiceNote();
}

// Gives the file of row its own event e (CHAR_VOICE_NONE: none), passed as
// --voice <file>=<event> from the next check on. Back to the event of its name
// (for a file rldpack leaves out: back to none) the --voice line goes away.
// Returns 1 = changed, 0 = no change (the event it has), -1 = not possible
// (no room for another, a file that is no voice).
static int Char_VoiceAssign(int row, int e)
{
    struct CharVoice *v;
    wchar_t event[96];
    int i, item;

    if (row < 0 || row >= g_char.voiceRowCount || e < 0 || e > CHAR_VOICE_NONE)
        return -1;
    if (e == Char_VoiceOwnEvent(row))
        return 0;
    if (!Char_VoiceCanChoose(row) || (wcscmp(g_char.voiceRow[row].state, L"ignored") == 0 && e != CHAR_VOICE_NONE))
        return -1;
    v = &g_char.voiceRow[row];
    i = Char_VoiceSetFind(v->name);
    if (i < 0) {
        if (g_char.voiceSetCount >= CHAR_VOICE_SET_MAX)
            return -1;
        i = g_char.voiceSetCount++;
        Char_Copy(g_char.voiceSet[i].name, CHAR_VOICE_NAME, v->name);
        g_char.voiceSet[i].named = v->event;
    }
    if (e == g_char.voiceSet[i].named) {
        memmove(&g_char.voiceSet[i], &g_char.voiceSet[i + 1],
                sizeof(g_char.voiceSet[0]) * (size_t)(g_char.voiceSetCount - i - 1));
        g_char.voiceSetCount--;
    } else {
        g_char.voiceSet[i].event = e;
    }
    // The list says it at once; the check that follows reports the file anew.
    v->event = e;
    Char_VoiceEventText(v, event, 96);
    for (item = ListView_GetItemCount(g_char.voiceList) - 1; item >= 0; item--) {
        LVITEMW it;
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_PARAM;
        it.iItem = item;
        if (ListView_GetItem(g_char.voiceList, &it) && it.lParam == row)
            ListView_SetItemText(g_char.voiceList, item, 3, event);
    }
    return 1;
}

// Play: PlaySoundW of winmm, loaded only here (LoadLibrary) - the exe needs no
// further library for it.
typedef BOOL (WINAPI *CharPlaySound)(LPCWSTR, HMODULE, DWORD);

static CharPlaySound Char_PlaySoundFn(void)
{
    static int tried;
    static CharPlaySound play;
    if (!tried) {
        HMODULE winmm = LoadLibraryExW(L"winmm.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        tried = 1;
        if (winmm)
            play = (CharPlaySound)GetProcAddress(winmm, "PlaySoundW");
    }
    return play;
}

// Stops what Play plays and frees its bytes.
static void Char_VoiceStop(void)
{
    if (g_char.voiceSound) {
        CharPlaySound play = Char_PlaySoundFn();
        if (play)
            play(NULL, NULL, 0);
    }
    Rs_Free(g_char.voiceSound);
    g_char.voiceSound = NULL;
}

// Play did not work: the line below the folder says why (until the next check),
// the automation log too.
static void Char_VoicePlayFailed(const struct CharVoice *v, const wchar_t *why)
{
    wchar_t t[CHAR_VOICE_NAME + 128];
    swprintf(t, CHAR_VOICE_NAME + 128, L"Cannot play %ls - %ls.", v->name, why);
    Char_SetLabel(g_char.voicesNote, t, RS_COL_WARNING, &g_char.voicesColor);
    if (Rs_Automating())
        Rs_AutoLog(L"  voiceplay: %ls", t);
}

// Plays the preview WAV of row: the file as the game will hear it (22050 Hz
// mono, normalized or not, cut to the longest length). It is read into memory,
// so the check may delete it meanwhile. In automation it is only read and
// described in the log, not played. 1 = played (or described).
static int Char_VoicePlay(int row)
{
    const struct CharVoice *v;
    FILE *f;
    long size;
    unsigned char *bytes;
    unsigned long rate = 0, data = 0, at;
    unsigned channels = 0, bits = 0;
    CharPlaySound play;

    if (row < 0 || row >= g_char.voiceRowCount)
        return 0;
    v = &g_char.voiceRow[row];
    if (!v->preview) {
        Char_VoicePlayFailed(v, L"rldpack could not read it");
        return 0;
    }
    Char_VoiceStop();
    f = _wfopen(v->preview, L"rb");
    if (!f) {
        Char_VoicePlayFailed(v, L"its preview file is gone, check again");
        return 0;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 12 || size > 16L * 1024L * 1024L) {
        fclose(f);
        Char_VoicePlayFailed(v, L"its preview file has a wrong size");
        return 0;
    }
    bytes = Rs_Alloc((size_t)size);
    if (fread(bytes, 1, (size_t)size, f) != (size_t)size || memcmp(bytes, "RIFF", 4) != 0 ||
        memcmp(bytes + 8, "WAVE", 4) != 0) {
        fclose(f);
        Rs_Free(bytes);
        Char_VoicePlayFailed(v, L"its preview is not a WAV file");
        return 0;
    }
    fclose(f);
    // The format and the length of the samples, for the log.
    for (at = 12; at + 8 <= (unsigned long)size;) {
        unsigned long len = Char_Le32(bytes + at + 4);
        if (memcmp(bytes + at, "fmt ", 4) == 0 && len >= 16 && at + 8 + 16 <= (unsigned long)size) {
            channels = Char_Le16(bytes + at + 10);
            rate = Char_Le32(bytes + at + 12);
            bits = Char_Le16(bytes + at + 22);
        } else if (memcmp(bytes + at, "data", 4) == 0) {
            data = len;
        }
        if (len > (unsigned long)size - at - 8)
            break;
        at += 8 + len + (len & 1);
    }
    if (Rs_Automating()) {
        unsigned long frames = (channels && bits >= 8) ? data / (channels * (bits / 8)) : 0;
        Rs_AutoLog(L"  voiceplay: %ls -> %ls, %ld bytes, %lu Hz, %u channel(s), %u bit, %lu frames (%lu ms) - "
                   L"not played (automation)",
                   v->name, v->preview, size, rate, channels, bits, frames, rate ? frames * 1000 / rate : 0);
        Rs_Free(bytes);
        return 1;
    }
    play = Char_PlaySoundFn();
    if (!play) {
        Rs_Free(bytes);
        Char_VoicePlayFailed(v, L"Windows offers no way to play sounds here (winmm)");
        return 0;
    }
    g_char.voiceSound = bytes;
    if (!play((LPCWSTR)bytes, NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT)) {
        Rs_Free(g_char.voiceSound);
        g_char.voiceSound = NULL;
        Char_VoicePlayFailed(v, L"Windows did not play it (no sound device?)");
        return 0;
    }
    return 1;
}

// The columns of the list: Event as wide as its longest text needs (the state
// in it is what a row has to say), the name the rest - at least 120.
static void Char_VoiceColumns(int width)
{
    int w = width - Rs_Metric(g_char.voiceList, SM_CXVSCROLL) - Rs_Px(4);
    int length = Rs_Px(64), size = Rs_Px(64), event = Char_TextWidth(g_char.voiceList, L"Event") + Rs_Px(16);
    int name, i;
    g_char.voiceListW = width;
    for (i = 0; i < g_char.voiceRowCount; i++) {
        wchar_t text[96];
        int tw;
        Char_VoiceEventText(&g_char.voiceRow[i], text, 96);
        tw = Char_TextWidth(g_char.voiceList, text) + Rs_Px(16);
        if (tw > event)
            event = tw;
    }
    name = w - length - size - event;
    if (name < Rs_Px(120)) {
        event -= Rs_Px(120) - name;
        name = Rs_Px(120);
    }
    ListView_SetColumnWidth(g_char.voiceList, 0, name);
    ListView_SetColumnWidth(g_char.voiceList, 1, length);
    ListView_SetColumnWidth(g_char.voiceList, 2, size);
    ListView_SetColumnWidth(g_char.voiceList, 3, event);
}

// "Closed hull (remesh)" works only with "Reduce automatically" (make-char:
// --remesh on needs --reduce auto). While that is off the box is greyed out
// and --remesh is not passed, whatever its tick says.
static int Char_RemeshAllowed(void)
{
    return Char_IsChecked(g_char.reduce);
}

static int Char_RemeshOn(void)
{
    return Char_RemeshAllowed() && Char_IsChecked(g_char.remesh);
}

static void Char_UpdateOptions(void)
{
    EnableWindow(g_char.remesh, Char_RemeshAllowed());
}

// "<n> <singular|plural>" with the number grouped, after ", " when out has text.
static void Char_CountPart(wchar_t *out, int cap, long n, const wchar_t *one, const wchar_t *many)
{
    wchar_t num[24];
    wchar_t part[128];
    Char_Grouped(num, 24, n);
    swprintf(part, 128, L"%ls%ls %ls", out[0] ? L", " : L"", num, n == 1 ? one : many);
    Char_Append(out, cap, part);
}

// The lines below the options: what the repair changed (only when it changed
// anything), the open parts drawn from both sides (only when there are such)
// and the remesh - each only as rldpack reported it (@char repaired,
// two-sided, remeshed) - and why "Closed hull" is greyed out. Lines are
// separated by CR LF.
static void Char_QualityText(wchar_t *out, int cap)
{
    struct CharJobData *j = &g_charJob;
    wchar_t line[512];
    wchar_t n[5][24];
    int i;

    out[0] = 0;
    if (j->repairedSeen) {
        const long *r = j->repaired;
        line[0] = 0;
        if (r[0])
            Char_CountPart(line, 512, r[0], L"corner welded", L"corners welded");
        if (r[1] + r[2])
            Char_CountPart(line, 512, r[1] + r[2], L"empty or doubled triangle dropped",
                           L"empty or doubled triangles dropped");
        if (r[3])
            Char_CountPart(line, 512, r[3], L"face turned outward", L"faces turned outward");
        if (r[9])
            Char_CountPart(line, 512, r[9], L"crack split", L"cracks split");
        if (r[4])
            Char_CountPart(line, 512, r[4], L"hole closed", L"holes closed");
        if (r[7] != r[8]) {
            wchar_t part[96];
            Char_Grouped(n[0], 24, r[7]);
            Char_Grouped(n[1], 24, r[8]);
            swprintf(part, 96, L"%lsopen edges %ls -> %ls", line[0] ? L", " : L"", n[0], n[1]);
            Char_Append(line, 512, part);
        }
        if (line[0]) {
            Char_Append(out, cap, L"Repaired: ");
            Char_Append(out, cap, line);
        }
    }
    // Only when it says something: the option on and parts drawn so.
    if (j->twoSidedSeen && j->twoSided > 0 && wcsncmp(j->openParts, L"one-sided", 9) != 0) {
        Char_Grouped(n[0], 24, j->twoSided);
        swprintf(line, 512, L"Two-sided open parts: %ls %ls (+0 triangles)", n[0],
                 j->twoSided == 1 ? L"triangle" : L"triangles");
        if (out[0])
            Char_Append(out, cap, L"\r\n");
        Char_Append(out, cap, line);
    }
    if (j->remeshedSeen) {
        for (i = 0; i < CHAR_REMESH_FIELDS; i++)
            Char_Grouped(n[i], 24, j->remeshed[i]);
        swprintf(line, 512, L"Remeshed: %ls triangles -> closed hulls of %ls -> %ls triangles, open edges %ls -> %ls",
                 n[0], n[1], n[2], n[3], n[4]);
        if (out[0])
            Char_Append(out, cap, L"\r\n");
        Char_Append(out, cap, line);
    }
    // Under the limit: the whole model, nothing taken away.
    if (j->budgetSeen && !j->reducedSeen && j->budget[0] <= j->budget[1]) {
        Char_Grouped(n[0], 24, j->budget[0]);
        Char_Grouped(n[1], 24, j->budget[1]);
        swprintf(line, 512, L"%ls triangles after the repair - within the limit of %ls for a driver. Nothing is reduced.", n[0], n[1]);
        if (out[0])
            Char_Append(out, cap, L"\r\n");
        Char_Append(out, cap, line);
    }
    // Only while it matters: Closed hull ticked but greyed out.
    if (!Char_RemeshAllowed() && Char_IsChecked(g_char.remesh)) {
        if (out[0])
            Char_Append(out, cap, L"\r\n");
        Char_Append(out, cap, CHAR_REMESH_NEEDS_TEXT);
    }
}

// Height of the note below the options: 0 while it is empty.
static int Char_QualityHeight(int width)
{
    wchar_t *text = Rs_GetText(g_char.quality);
    int empty = !text[0];
    Rs_Free(text);
    return empty ? 0 : Char_TextHeight(g_char.quality, width, CHAR_QUALITY_LINES);
}

// Into the label; the page is laid out again when its height changes.
static void Char_ApplyQuality(void)
{
    wchar_t t[CHAR_VAL];
    RECT rc;
    int shownH, need;

    GetClientRect(g_char.quality, &rc);
    shownH = g_char.qualityOn ? rc.bottom : 0;
    Char_QualityText(t, CHAR_VAL);
    Char_SetLabel(g_char.quality, t, RS_COL_TEXT, &g_char.qualityColor);
    need = Char_QualityHeight(rc.right > 0 ? rc.right : Rs_Px(300));
    if (need != shownH)
        Char_Relayout(GetParent(g_char.quality));
}

// THE NOTE BELOW THE PREVIEW. The preview shows what the character file
// holds: the native model when the check built one (NATIVE DRIVERS set to
// PREVIEW in the game), else the classic model (what the game draws with
// NATIVE DRIVERS OFF, and with every native model as its fallback); with
// both, the View menu of the preview switches between Native and Classic
// (RsView_SetShowNative; a new model starts at Native). The note says which
// look is shown, why the native one is not (no OBJ, Native model not
// ticked, the kart wheels without a wheel model), the textures not found by
// their names (@model texture of the check, the card Wheels), wheels shown
// that are not built, and how the mouse steers the view - in this order, as
// many sentences as fit in CHAR_VIEW_NOTE_LINES (the compact layout: one
// line, the whole note as its tooltip).
#define CHAR_VIEW_MOUSE_TEXT L"Drag: turn - right drag: move - wheel: zoom - double-click: reset."
#define CHAR_VIEW_NATIVE_TEXT L"Native look, as the game draws it with NATIVE DRIVERS set to PREVIEW (OPTIONS, GRAPHICS). The View menu shows the Classic fallback."
#define CHAR_VIEW_FALLBACK_TEXT L"Classic look: the fallback the game draws with NATIVE DRIVERS OFF. The View menu shows the Native look."

// Height of text in the note at this width, all its lines.
static int Char_ViewNoteHeightOf(const wchar_t *text, int width, int *lineH)
{
    HDC dc = GetDC(g_char.viewNote);
    HFONT font = (HFONT)SendMessageW(g_char.viewNote, WM_GETFONT, 0, 0);
    HGDIOBJ old;
    TEXTMETRICW tm;
    RECT rc;
    int h = 0;

    *lineH = 0;
    if (dc) {
        old = SelectObject(dc, font ? font : Rs_Font(RS_FONT_SMALL));
        GetTextMetricsW(dc, &tm);
        *lineH = tm.tmHeight;
        rc.left = 0;
        rc.top = 0;
        rc.right = width;
        rc.bottom = 0;
        DrawTextW(dc, text, -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EXPANDTABS);
        h = rc.bottom;
        SelectObject(dc, old);
        ReleaseDC(g_char.viewNote, dc);
    }
    return h;
}

// The first count sentences of the note, one space between them; first >= 0:
// that sentence before all others.
static void Char_ViewNoteJoin(wchar_t *out, int cap, int count, int first)
{
    int i;
    out[0] = 0;
    if (first >= 0 && first < g_char.viewPartCount)
        Char_Append(out, cap, g_char.viewPart[first]);
    for (i = 0; i < count && i < g_char.viewPartCount; i++) {
        if (i == first)
            continue;
        if (out[0])
            Char_Append(out, cap, L" ");
        Char_Append(out, cap, g_char.viewPart[i]);
    }
}

// The note at this width: as many sentences as fit in CHAR_VIEW_NOTE_LINES,
// at least the first; the compact layout takes all (one line and a tooltip),
// the textures not found first - its one line shows them.
static void Char_ViewNoteText(wchar_t *out, int cap, int width)
{
    int count = g_char.viewPartCount, lineH = 0;
    if (g_char.compact) {
        Char_ViewNoteJoin(out, cap, count, g_char.viewFirst);
        return;
    }
    Char_ViewNoteJoin(out, cap, count, -1);
    if (width <= 0)
        return;
    while (count > 1 && Char_ViewNoteHeightOf(out, width, &lineH) > lineH * CHAR_VIEW_NOTE_LINES && lineH > 0)
        Char_ViewNoteJoin(out, cap, --count, -1);
}

// From Char_LayPreview, before the note is measured: the sentences that fit
// its new width (no relayout from here).
static void Char_ViewNoteFit(int width)
{
    wchar_t text[CHAR_VIEW_PARTS * 384 + CHAR_VIEW_PARTS], *now;
    if (!g_char.viewNote || g_char.viewPartCount <= 0)
        return;
    Char_ViewNoteText(text, CHAR_VIEW_PARTS * 384 + CHAR_VIEW_PARTS, width);
    now = Rs_GetText(g_char.viewNote);
    if (wcscmp(now, text) != 0)
        Rs_SetText(g_char.viewNote, text);
    Rs_Free(now);
}

static void Char_ViewNoteShow(COLORREF color)
{
    wchar_t text[CHAR_VIEW_PARTS * 384 + CHAR_VIEW_PARTS];
    RECT rc = { 0, 0, 0, 0 };
    if (!g_char.viewNote || !GetWindowRect(g_char.viewNote, &rc))
        rc.left = rc.right = 0;
    Char_ViewNoteText(text, CHAR_VIEW_PARTS * 384 + CHAR_VIEW_PARTS, rc.right - rc.left);
    Char_SetLabel(g_char.viewNote, text, color, &g_char.viewColor);
}

static void Char_ViewNote(const wchar_t *text, COLORREF color)
{
    g_char.viewPartCount = 1;
    g_char.viewFirst = -1;
    Char_Copy(g_char.viewPart[0], 384, text);
    Char_ViewNoteShow(color);
}

static void Char_ViewNotePart(const wchar_t *text)
{
    if (g_char.viewPartCount < CHAR_VIEW_PARTS)
        Char_Copy(g_char.viewPart[g_char.viewPartCount++], 384, text);
}

// Which look the preview shows and why (the first sentence of the note).
static const wchar_t *Char_ViewLookText(void)
{
    int native = 0;
    if (g_char.previewNative) {
        RsView_GetToggles(g_char.view, NULL, NULL, NULL, NULL, &native);
        return native ? CHAR_VIEW_NATIVE_TEXT : CHAR_VIEW_FALLBACK_TEXT;
    }
    if (!g_char.objOn)
        return L"Classic look, as the game draws it. Textures of your own need an OBJ model with its MTL file (Native model, Extras, Import).";
    if (Char_NativePassed())
        return L"Classic look: the check brought no native model - see the message list.";
    if (Char_NativeOn())
        return L"Classic look: the native model needs Show kart wheels off (tab Model) or a wheel model (Extras, Wheels).";
    if (g_char.previewTextured)
        return L"Classic look: your textures are only baked into the colours of the triangles. Tick Native model (Extras, Import) to build them.";
    return L"Classic look, as the game draws it. Tick Native model (Extras, Import) for the native model.";
}

// The note after a check shown, a change of the card Wheels or of the view's
// toggles. Only while a model is shown: the other notes (no model, errors)
// stay as they are.
static void Char_ViewNoteUpdate(void)
{
    wchar_t one[384], wheelNames[256];
    int wheelMissing, native = 0, missing;

    if (!g_char.viewNote || !g_char.previewShown)
        return;
    RsView_GetToggles(g_char.view, NULL, NULL, NULL, NULL, &native);
    g_char.viewPartCount = 0;
    g_char.viewFirst = -1;
    Char_ViewNotePart(Char_ViewLookText());
    // The textures not found: those of the model, then the wheel's.
    wheelMissing = CharWheels_Shown() && Char_IsChecked(g_char.wheels) ? CharWheels_MissingTextures(wheelNames, 256) : 0;
    missing = g_char.previewMissing + wheelMissing;
    if (missing > 0) {
        wchar_t names[512];
        Char_Copy(names, 512, g_char.previewMissing ? g_char.previewMissingNames : L"");
        if (wheelMissing) {
            if (names[0])
                Char_Append(names, 512, L", ");
            Char_Append(names, 512, wheelNames);
        }
        // The first three names, the rest counted (the report has them all).
        {
            wchar_t *cut = names;
            int k;
            for (k = 0; k < 3 && cut; k++) {
                cut = wcsstr(cut, L", ");
                if (cut && k < 2)
                    cut += 2;
            }
            if (cut && missing > 3)
                swprintf(cut, 512 - (cut - names), L" and %d more", missing - 3);
        }
        swprintf(one, 384, L"%d texture%ls not found: %ls - drawn in %ls colour.", missing, missing == 1 ? L"" : L"s", names,
                 missing == 1 ? L"its" : L"their");
        g_char.viewFirst = g_char.viewPartCount;
        Char_ViewNotePart(one);
    }
    // Wheels of the card Wheels in the preview that the character will not have.
    if (CharWheels_Shown() && Char_IsChecked(g_char.wheels) && !Char_WheelPassed(NULL, 0))
        Char_ViewNotePart(L"Your wheels show here but are not built (Extras, Wheels says why).");
    Char_ViewNotePart(CHAR_VIEW_MOUSE_TEXT);
    // The classic model in the race: shaded on dark ground (the native one:
    // not determined).
    if (!(g_char.previewNative && native))
        Char_ViewNotePart(CHAR_VIEW_SHADE_TEXT);
    Char_ViewNoteShow(missing > 0                                                           ? RS_COL_WARNING
                      : !g_char.previewNative && g_char.objOn && g_char.previewTextured ? RS_COL_NOTE
                                                                                        : RS_COL_MUTED);
}

// The converted model of the check into the 3D view, on the dummy it was
// fitted onto. Without a preview file the view shows why instead.
static void Char_ApplyPreview(int seq)
{
    wchar_t path[CHAR_VAL];
    struct CharJobData *j = &g_charJob;
    int i;

    if (j->kartSeen) {
        g_char.kartKnown = 1;
        memcpy(g_char.kart, j->kart, sizeof(g_char.kart));
    }
    // The retail kart box is only reported (the dummy under the model is the
    // retail kart itself).
    if (j->retailSeen) {
        g_char.retailKnown = 1;
        memcpy(g_char.retail, j->retail, sizeof(g_char.retail));
    }
    // Crash's size for the dummy's driver; the game's wheels under the model
    // as the check box says.
    if (j->crashSeen) {
        g_char.crashKnown = 1;
        memcpy(g_char.crash, j->crash, sizeof(g_char.crash));
    }
    if (g_char.crashKnown)
        RsView_SetCrashBox(g_char.view, g_char.crash[0], g_char.crash[1], g_char.crash[2], g_char.crash[3],
                           g_char.crash[4], g_char.crash[5]);
    else
        RsView_SetCrashBox(g_char.view, 0, 0, 0, 0, 0, 0);
    RsView_SetWheels(g_char.view, Char_IsChecked(g_char.wheels));
    g_char.previewShown = 0;
    g_char.previewPoses = 0;
    memset(g_char.previewTris, 0, sizeof(g_char.previewTris));
    Char_TempPath(path, CHAR_VAL, seq, L".rldpv");
    if (!Rs_FileExists(path)) {
        RsView_Clear(g_char.view, L"No preview - rldpack could not convert the model.");
        Char_ViewNote(L"No preview - fix the errors in the message list first.", RS_COL_WARNING);
        return;
    }
    // Another model than the one shown: the start view, and the native look
    // when it has one (L1); the same model keeps the camera and the choice
    // of the View menu.
    if (_wcsicmp(g_char.previewModel, g_char.checkModel) != 0) {
        Char_Copy(g_char.previewModel, CHAR_VAL, g_char.checkModel);
        RsView_SetShowNative(g_char.view, 1);
    }
    RsView_SetModelKey(g_char.view, g_char.checkModel);
    // The view reads the file into memory; it is deleted right after.
    RsView_LoadPreview(g_char.view, path);
    if (!RsView_Loaded(g_char.view)) {
        Char_ViewNote(L"The preview file could not be shown - check the model again.", RS_COL_WARNING);
        return;
    }
    g_char.previewShown = 1;
    g_char.previewPoses = CHAR_POSES;
    for (i = 0; i < CHAR_POSES; i++)
        g_char.previewTris[i] = (unsigned long)RsView_TriangleCount(g_char.view, i);
    RsView_SetPose(g_char.view, g_charPoseView[g_char.poseNow]);
    // The preview shows the colours of the palette as they are - the game's
    // brightest case. In the race the ground under the kart darkens the
    // driver (game/COLL.c sets alphaScale from the track's colour there,
    // RenderBucket fades towards black): 0.25 + luma / 128 of the colour on
    // ground darker than luma 96, at most 75 % darker.
    // The note (Char_ViewNoteUpdate): the textures of the model and those
    // not found, by their file names, once each.
    g_char.previewNative = RsView_HasNative(g_char.view);
    g_char.previewTextured = 0;
    g_char.previewMissing = 0;
    g_char.previewMissingNames[0] = 0;
    for (i = 0; i < g_char.importRowCount; i++) {
        const struct CharImport *r = &g_char.importRow[i];
        const wchar_t *name = Rs_PathName(r->path);
        int k, seen = 0;
        if (r->kind != CHAR_IMPORT_TEXTURE)
            continue;
        if (wcscmp(r->state, L"ok") == 0)
            g_char.previewTextured = 1;
        if (wcscmp(r->state, L"ok") == 0 || wcscmp(r->state, L"none") == 0 || !name[0])
            continue;
        for (k = 0; k < i && !seen; k++)
            seen = g_char.importRow[k].kind == CHAR_IMPORT_TEXTURE && wcscmp(g_char.importRow[k].state, L"ok") != 0 &&
                   _wcsicmp(Rs_PathName(g_char.importRow[k].path), name) == 0;
        if (seen)
            continue;
        if (g_char.previewMissing)
            Char_Append(g_char.previewMissingNames, 256, L", ");
        Char_Append(g_char.previewMissingNames, 256, name);
        g_char.previewMissing++;
    }
    Char_ViewNoteUpdate();
}

// One portrait into the game picture at x, y, with the alpha rldpack wrote:
// 0 transparent (the race shows), 255 opaque, anything between a texel the
// game draws at half (its 50 % mode: half the race, half the texel).
static void Char_GameBlend(struct CharImage *game, const struct CharImage *icon, int at, int top)
{
    int x, y, c;
    for (y = 0; y < CHAR_ICON_H && y < icon->h; y++) {
        for (x = 0; x < CHAR_ICON_W && x < icon->w; x++) {
            const unsigned char *src = icon->px + ((size_t)y * icon->w + x) * 4;
            unsigned char *dst = game->px + ((size_t)(top + y) * game->w + at + x) * 4;
            if (src[3] == 0)
                continue;
            for (c = 0; c < 3; c++)
                dst[c] = (unsigned char)(src[3] == 255 ? src[c] : (src[c] + dst[c]) / 2);
        }
    }
}

// Without the game's data: the measured frame of the retail portraits and,
// dotted, their head box, as lines in place of the template's portrait.
static void Char_GameOutline(struct CharImage *game, int at, int top)
{
    int x, y;
    for (y = 0; y < CHAR_ICON_H; y++) {
        for (x = 0; x < CHAR_ICON_W; x++) {
            unsigned char *dst = game->px + ((size_t)(top + y) * game->w + at + x) * 4;
            int frame = ((x == 2 || x == 38) && y >= 1 && y <= 23) || ((y == 1 || y == 23) && x >= 2 && x <= 38);
            int head = x <= 40 && (x == 0 || x == 40 || y == 0 || y == 24) && ((x + y) & 1);
            if (frame || head) {
                dst[0] = CHAR_GAME_LINE;
                dst[1] = CHAR_GAME_LINE;
                dst[2] = CHAR_GAME_LINE;
            }
        }
    }
}

// The second picture: the race, the template's portrait on the left (from
// <prefix>-retail.bmp, else the lines), the converted icon on the right.
static void Char_GameCompose(int seq)
{
    struct CharImage retail = { 0, 0, NULL };
    struct CharImage *game = &g_char.game;
    wchar_t path[CHAR_VAL];
    int i;

    Char_ImageFree(game);
    g_char.gameRetail = 0;
    Rs_SetText(g_char.iconCaption[CHAR_IMG_ICON], L"In the game, beside Fake Crash");
    if (!g_char.image[CHAR_IMG_ICON].px)
        return;
    game->px = Rs_Alloc((size_t)CHAR_GAME_W * CHAR_GAME_H * 4);
    game->w = CHAR_GAME_W;
    game->h = CHAR_GAME_H;
    for (i = 0; i < CHAR_GAME_W * CHAR_GAME_H; i++) {
        game->px[i * 4 + 0] = GetBValue(CHAR_GAME_BG);
        game->px[i * 4 + 1] = GetGValue(CHAR_GAME_BG);
        game->px[i * 4 + 2] = GetRValue(CHAR_GAME_BG);
        game->px[i * 4 + 3] = 255;
    }
    Char_TempPath(path, CHAR_VAL, seq, L"-retail.bmp");
    if (g_charJob.iconRetailOk && Rs_FileExists(path) && Char_LoadBmp(path, &retail)) {
        Char_GameBlend(game, &retail, CHAR_GAME_GAP, CHAR_GAME_GAP);
        g_char.gameRetail = 1;
    } else {
        Char_GameOutline(game, CHAR_GAME_GAP, CHAR_GAME_GAP);
    }
    Char_ImageFree(&retail);
    Char_GameBlend(game, &g_char.image[CHAR_IMG_ICON], 2 * CHAR_GAME_GAP + CHAR_ICON_W, CHAR_GAME_GAP);
    if (!g_char.gameRetail)
        Rs_SetText(g_char.iconCaption[CHAR_IMG_ICON], L"Game data not found - frame only");
}

// The icon as decoded and as converted, from the --icon-preview files, and the
// converted one in the game (Char_GameCompose).
static void Char_ApplyIcon(int seq)
{
    static const wchar_t *const kinds[CHAR_IMG_COUNT] = { L"-original.bmp", L"-icon.bmp" };
    wchar_t icon[CHAR_VAL];
    wchar_t path[CHAR_VAL];
    int i;

    Char_ImagesClear();
    Char_FieldPath(g_char.icon, icon, CHAR_VAL);
    Rs_SetText(g_char.iconImage[CHAR_IMG_ORIGINAL], icon[0] ? L"cannot be read" : L"no PNG chosen");
    Rs_SetText(g_char.iconImage[CHAR_IMG_ICON], icon[0] ? L"not converted" : L"the template's icon");
    if (!icon[0])
        return;
    for (i = 0; i < CHAR_IMG_COUNT; i++) {
        Char_TempPath(path, CHAR_VAL, seq, kinds[i]);
        if (Rs_FileExists(path))
            Char_LoadBmp(path, &g_char.image[i]);
    }
    Char_GameCompose(seq);
    for (i = 0; i < CHAR_IMG_COUNT; i++)
        InvalidateRect(g_char.iconImage[i], NULL, FALSE);
}

// The framing and the two options of the icon only with an icon: without one
// they are not passed.
static void Char_UpdateIconOptions(void)
{
    wchar_t icon[CHAR_VAL];
    BOOL on;
    Char_FieldPath(g_char.icon, icon, CHAR_VAL);
    on = icon[0] != 0;
    EnableWindow(g_char.iconFit, on);
    EnableWindow(g_char.iconCorners, on);
    EnableWindow(g_char.iconFrame, on);
}

// Index into g_charIconFits of the chosen framing.
static int Char_IconFitIndex(void)
{
    LRESULT sel = SendMessageW(g_char.iconFit, CB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= CHAR_ICON_FIT_COUNT)
        sel = CHAR_ICON_FIT_DEFAULT;
    return (int)sel;
}

// Over the limit of triangles (@char budget, or rldpack's model-tris without
// it). 1 = over; the headline text then into out.
static int Char_OverLimit(wchar_t *out, int cap)
{
    struct CharJobData *j = &g_charJob;
    // budget is before the reduction: a model Reduce to fit brought under the
    // limit is no longer over it.
    int fits = j->reducedSeen && j->reduced[1] <= j->budget[1];
    int over = (j->budgetSeen && j->budget[0] > j->budget[1] && !fits) || Char_FindMsg(L"model-tris") != NULL;
    const wchar_t *what;

    if (!over)
        return 0;
    what = Char_IsChecked(g_char.reduce) ? L"Reduce to fit could not bring it under the limit"
                                         : L"tick Reduce to fit, or reduce it in Blender";
    if (j->budgetSeen) {
        wchar_t n[2][24];
        Char_Grouped(n[0], 24, j->budget[0]);
        Char_Grouped(n[1], 24, j->budget[1]);
        swprintf(out, cap, L"Model has %ls triangles, the limit is %ls - %ls.", n[0], n[1], what);
    } else {
        swprintf(out, cap, L"Model has more triangles than a driver may draw - %ls.", what);
    }
    return 1;
}

// The button "Reduce to fit" only while the model is over the limit and the
// box is off (on the tab Model: Char_TabApply).
static void Char_ReduceFitShow(HWND page, int show)
{
    if (g_char.reduceFitOn != (show != 0)) {
        g_char.reduceFitOn = show != 0;
        Char_Relayout(page);
    }
}

static void Char_ShowCheckResult(int exitCode)
{
    struct CharJobData *j = &g_charJob;
    int ok = j->resultSeen && wcscmp(j->resultState, L"checked") == 0;
    wchar_t t[256];
    int errors, over;

    g_char.checked = ok;
    Char_Copy(g_char.checkedPath, CHAR_VAL, j->resultPath);
    Char_CountTabs();
    Char_VoiceNote();           // it says an error of the voices
    Char_MsgClear();
    errors = Char_AddRunProblems(exitCode, ok);
    errors += Char_CountMsgs(RS_SEV_ERROR);
    Char_AddMsgs(RS_SEV_ERROR);
    Char_AddMsgs(RS_SEV_WARNING);
    Char_AddMsgs(RS_SEV_NOTE);
    Char_AddMsgs(RS_SEV_INFO);
    Char_AddMsgs(RS_SEV_OK);
    Char_EmptyText(L"rldpack reported nothing.");
    over = !ok && Char_OverLimit(t, 256);
    if (ok) {
        Char_Headline(L"Ready to build", RS_COL_OK);
    } else if (over) {
        Char_Headline(t, RS_COL_ERROR);
    } else {
        swprintf(t, 256, L"Cannot build yet - %d problem(s)", errors);
        Char_Headline(t, RS_COL_ERROR);
    }
    Char_ReduceFitShow(GetParent(g_char.headline), over && !Char_IsChecked(g_char.reduce));
    Char_UpdateButtons();
    if (Rs_Automating()) {
        if (ok)
            Rs_AutoLog(L"  check: ready to build, %d warning(s), %d note(s)",
                       Char_CountMsgs(RS_SEV_WARNING), Char_CountMsgs(RS_SEV_NOTE));
        else
            Rs_AutoLog(L"  check: cannot build yet, %d problem(s)", errors);
        if (over)
            Rs_AutoLog(L"  check: %ls", t);
        if (g_charJob.budgetSeen)
            Rs_AutoLog(L"  budget: %ld triangles, limit %ld; %ld bytes of draw memory, limit %ld",
                       g_charJob.budget[0], g_charJob.budget[1], g_charJob.budget[2], g_charJob.budget[3]);
        if (g_char.rangeNone)
            Rs_AutoLog(L"  size: %d %%, no size fits this model", g_char.sizeNow);
        else if (g_char.rangeKnown)
            Rs_AutoLog(L"  size: %d %%, this model allows %d..%d %%", g_char.sizeNow, g_char.rangeLo,
                       g_char.rangeHi);
        else
            Rs_AutoLog(L"  size: %d %%, allowed range not reported", g_char.sizeNow);
        if (g_charJob.fitSeen)
            Rs_AutoLog(L"  fit: x%ls, %ls -> %ls long, %ls -> %ls tall, by the %ls", g_charJob.fitFactor,
                       g_charJob.fitLength[0], g_charJob.fitLength[1], g_charJob.fitHeight[0],
                       g_charJob.fitHeight[1], g_charJob.fitBasis);
        if (g_charJob.reducedSeen)
            Rs_AutoLog(L"  reduced: %ld -> %ld triangles, draw memory %ld -> %ld bytes", g_charJob.reduced[0],
                       g_charJob.reduced[1], g_charJob.reduced[2], g_charJob.reduced[3]);
        if (g_charJob.repairedSeen)
            Rs_AutoLog(L"  repaired: %ld welded, %ld degenerate, %ld doubled, %ld turned, %ld holes closed with %ld "
                       L"triangles, %ld left, open edges %ld -> %ld, %ld cracks split",
                       g_charJob.repaired[0], g_charJob.repaired[1], g_charJob.repaired[2], g_charJob.repaired[3],
                       g_charJob.repaired[4], g_charJob.repaired[5], g_charJob.repaired[6], g_charJob.repaired[7],
                       g_charJob.repaired[8], g_charJob.repaired[9]);
        if (g_charJob.twoSidedSeen)
            Rs_AutoLog(L"  two-sided open parts: %ld triangles", g_charJob.twoSided);
        if (g_charJob.remeshedSeen)
            Rs_AutoLog(L"  remeshed: %ld -> %ld -> %ld triangles, open edges %ld -> %ld", g_charJob.remeshed[0],
                       g_charJob.remeshed[1], g_charJob.remeshed[2], g_charJob.remeshed[3], g_charJob.remeshed[4]);
        if (g_char.previewShown)
            Rs_AutoLog(L"  preview: %d pose(s), %lu/%lu/%lu triangles", g_char.previewPoses,
                       g_char.previewTris[0], g_char.previewTris[1], g_char.previewTris[2]);
        else
            Rs_AutoLog(L"  preview: none");
        if (g_char.image[CHAR_IMG_ORIGINAL].px || g_char.image[CHAR_IMG_ICON].px)
            Rs_AutoLog(L"  icon: original %dx%d, converted %dx%d", g_char.image[CHAR_IMG_ORIGINAL].w,
                       g_char.image[CHAR_IMG_ORIGINAL].h, g_char.image[CHAR_IMG_ICON].w,
                       g_char.image[CHAR_IMG_ICON].h);
        if (g_charJob.iconBackground[0] || g_charJob.iconPlace[0])
            Rs_AutoLog(L"  icon: background cleared %ls, place %ls", g_charJob.iconBackground, g_charJob.iconPlace);
        if (g_char.game.px)
            Rs_AutoLog(L"  icon in the game: beside %ls", g_char.gameRetail ? L"the template's portrait (game data)"
                                                                         : L"the frame lines (no game data)");
    }
}

static void Char_ProgressShow(HWND page, int on);

static void Char_StartFailed(const wchar_t *headline)
{
    Char_ProgressShow(GetParent(g_char.headline), 0);
    Char_MsgClear();
    Char_MsgAdd(-1, RS_SEV_ERROR, L"Reload Studio could not start rldpack.",
                  L"rldpack runs as a second copy of Reload Studio. Try again; if it keeps failing, "
                  L"check that no security program blocks it.");
    Char_Headline(headline, RS_COL_ERROR);
    Char_UpdateButtons();
    if (Rs_Automating())
        Rs_AutoLog(L"  rldpack could not be started");
}

// ---------------------------------------------------------------------------
// Progress of a long check or build
// ---------------------------------------------------------------------------

// The bar: the part done in the accent colour, the text over it.
static LRESULT CALLBACK Char_ProgressProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_ERASEBKGND)
        return 1;
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc, done;
        HBRUSH br;
        HGDIOBJ old;

        GetClientRect(hwnd, &rc);
        br = CreateSolidBrush(RS_COL_CARD);
        FillRect(dc, &rc, br);
        DeleteObject(br);
        if (g_char.progressPos > 0) {
            done = rc;
            done.right = rc.left + (int)((long long)(rc.right - rc.left) * g_char.progressPos / 1000);
            br = CreateSolidBrush(RS_COL_ACCENT);
            FillRect(dc, &done, br);
            DeleteObject(br);
        }
        br = CreateSolidBrush(RS_COL_BORDER);
        FrameRect(dc, &rc, br);
        DeleteObject(br);
        old = SelectObject(dc, Rs_Font(RS_FONT_SMALL));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RS_COL_TEXT);
        DrawTextW(dc, g_char.progressText, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, old);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// The bar and Cancel beside the headline, on or off (the bar is laid out again).
static void Char_ProgressShow(HWND page, int on)
{
    if (!on)
        KillTimer(page, CHAR_TIMER_BUSY);
    if ((on != 0) != g_char.progressOn) {
        g_char.progressOn = on != 0;
        Char_Relayout(page);
    }
}

// A @progress line of the running job: the step and how far it is.
static void Char_Progress(HWND page, const struct RsJobProgress *p)
{
    static const wchar_t *const steps[][2] = { { L"repair", L"Repairing" }, { L"remesh", L"Remeshing" },
                                               { L"reduce", L"Reducing" }, { L"write", L"Writing" } };
    const wchar_t *word = p->step;
    int i;

    for (i = 0; i < (int)(sizeof(steps) / sizeof(steps[0])); i++)
        if (wcscmp(p->step, steps[i][0]) == 0)
            word = steps[i][1];
    if (p->total > 0) {
        unsigned long done = p->done < p->total ? p->done : p->total;
        g_char.progressPos = (int)((unsigned long long)done * 1000 / p->total);
        swprintf(g_char.progressText, 64, L"%ls %lu %%", word, (unsigned long)((unsigned long long)done * 100 / p->total));
    } else {
        g_char.progressPos = -1;
        swprintf(g_char.progressText, 64, L"%ls...", word);
    }
    // A line of progress comes only from a long step: the bar shows at once.
    Char_ProgressShow(page, 1);
    InvalidateRect(g_char.progress, NULL, FALSE);
}

// Cancel: rldpack is ended; RS_WM_JOB_DONE follows with RS_JOB_CANCELLED.
static int Char_Cancel(HWND page)
{
    (void)page;
    if (!g_char.jobId)
        return 0;
    if (!Rs_JobCancel(g_char.jobId))
        return 0;
    Char_Headline(L"Cancelling...", RS_COL_MUTED);
    EnableWindow(g_char.cancel, FALSE);
    return 1;
}

// ---------------------------------------------------------------------------
// Commands to rldpack
// ---------------------------------------------------------------------------

struct CharArgs {
    const wchar_t *v[CHAR_MAX_ARGS];
    int n;
    wchar_t model[CHAR_VAL];
    wchar_t icon[CHAR_VAL];
    wchar_t voices[CHAR_VAL];
    wchar_t textures[CHAR_VAL];
    wchar_t out[CHAR_VAL];
    wchar_t preview[CHAR_VAL];
    wchar_t iconPrefix[CHAR_VAL];
    wchar_t voicePrefix[CHAR_VAL];
    wchar_t size[16];
    wchar_t mapColor[8];    // RRGGBB
    wchar_t exhaust[160];   // --exhaust x,y,z[;x,y,z]
    wchar_t wheel[CHAR_VAL];      // --wheel-model (the card Wheels)
    wchar_t wheelSize[16];        // --wheel-size
    wchar_t *name;          // Rs_Free
    wchar_t *voiceSet[CHAR_VOICE_SET_MAX];   // "<file>=<event>" of --voice, Rs_Free
};

static void Char_ArgsAdd(struct CharArgs *a, const wchar_t *s)
{
    if (a->n < CHAR_MAX_ARGS)
        a->v[a->n++] = s;
}

static void Char_ArgsFree(struct CharArgs *a)
{
    int i;
    Rs_Free(a->name);
    a->name = NULL;
    for (i = 0; i < CHAR_VOICE_SET_MAX; i++) {
        Rs_Free(a->voiceSet[i]);
        a->voiceSet[i] = NULL;
    }
}

static const wchar_t *Char_ClassWord(void)
{
    LRESULT sel = SendMessageW(g_char.cls, CB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= CHAR_CLASS_COUNT)
        sel = 0;
    return g_charClasses[sel].word;
}

// Index into g_charMasks of the chosen mask.
static int Char_MaskIndex(void)
{
    LRESULT sel = SendMessageW(g_char.mask, CB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= CHAR_MASK_COUNT)
        sel = CHAR_TEMPLATE_MASK;
    return (int)sel;
}

// The folder the game loads its characters from: "characters" next to the game
// program (the one entered on the page Test in game, else Rs_FindGameExe).
// 0 = no game program is known; out is "".
static int Char_GameCharDir(wchar_t *out, int cap)
{
    wchar_t exe[CHAR_VAL];
    wchar_t dir[CHAR_VAL];
    const wchar_t *page = Rs_TestGameExe();

    out[0] = 0;
    if (page && page[0] && Rs_FileExists(page))
        Char_Copy(exe, CHAR_VAL, page);
    else if (!Rs_FindGameExe(exe, CHAR_VAL))
        return 0;
    Rs_PathDir(dir, CHAR_VAL, exe);
    Rs_PathJoin(out, cap, dir, L"characters");
    return 1;
}

// The output while the field is empty: <game>\characters\<model name>.rldchar
// when the game program is known (the name as rldpack makes it: the model's
// file name without .ply or .obj); "" otherwise - rldpack then writes next to
// the model.
static void Char_DefaultOut(const wchar_t *model, wchar_t *out, int cap)
{
    wchar_t dir[CHAR_VAL];
    wchar_t name[CHAR_VAL];
    size_t n;

    out[0] = 0;
    if (!model[0] || !Char_GameCharDir(dir, CHAR_VAL))
        return;
    Char_Copy(name, CHAR_VAL - 8, Rs_PathName(model));
    n = wcslen(name);
    if (n > 4 && (_wcsicmp(name + n - 4, L".ply") == 0 || _wcsicmp(name + n - 4, L".obj") == 0))
        name[n - 4] = 0;
    Char_Append(name, CHAR_VAL, L".rldchar");
    Rs_PathJoin(out, cap, dir, name);
}

// 1 if path lies directly in the game's characters folder.
static int Char_InGameCharDir(const wchar_t *path)
{
    wchar_t dir[CHAR_VAL];
    wchar_t own[CHAR_VAL];

    wchar_t full[CHAR_VAL];

    if (!Char_GameCharDir(dir, CHAR_VAL))
        return 0;
    if (!GetFullPathNameW(path, CHAR_VAL, full, NULL))
        Char_Copy(full, CHAR_VAL, path);
    Rs_PathDir(own, CHAR_VAL, full);
    if (GetFullPathNameW(dir, CHAR_VAL, full, NULL))
        Char_Copy(dir, CHAR_VAL, full);
    return _wcsicmp(own, dir) == 0;
}

// The cue of the empty output field before a check names the file.
static void Char_OutCue(void)
{
    wchar_t dir[CHAR_VAL];
    SendMessageW(g_char.out, EM_SETCUEBANNER, FALSE,
                 (LPARAM)(Char_GameCharDir(dir, CHAR_VAL) ? L"In the game's characters folder (.rldchar)"
                                                          : L"Next to the model (.rldchar)"));
}

// make-char with the values of the fields. check = 1: with --check and the
// preview files of number seq. out: output path (NULL = from the field).
// Returns 0 if there is no model.
static int Char_MakeArgs(struct CharArgs *a, int check, const wchar_t *out, int seq)
{
    const int user = Rs_NativeForUsers();

    memset(a, 0, sizeof(*a));
    Char_FieldPath(g_char.model, a->model, CHAR_VAL);
    if (!a->model[0])
        return 0;
    Char_FieldPath(g_char.icon, a->icon, CHAR_VAL);
    Char_FieldPath(g_char.voices, a->voices, CHAR_VAL);
    a->name = Rs_GetText(g_char.name);
    swprintf(a->size, 16, L"%d", g_char.sizeNow);

    Char_ArgsAdd(a, L"make-char");
    Char_ArgsAdd(a, L"--machine");
    if (check)
        Char_ArgsAdd(a, L"--check");
    Char_ArgsAdd(a, L"--model");
    Char_ArgsAdd(a, a->model);
    Char_ArgsAdd(a, L"--name");
    Char_ArgsAdd(a, a->name);
    Char_ArgsAdd(a, L"--template");
    Char_ArgsAdd(a, CHAR_TEMPLATE);
    Char_ArgsAdd(a, L"--class");
    Char_ArgsAdd(a, Char_ClassWord());
    Char_ArgsAdd(a, L"--size");
    Char_ArgsAdd(a, a->size);
    // Only what differs from rldpack's defaults (--repair auto, --open-parts
    // two-sided, --remesh off, --reduce auto, --wheels on). The user mode
    // (Rs_NativeForUsers) passes none of the four: they stay rldpack's
    // defaults, hidden - a model over the limit is reduced for the classic
    // model, the native model keeps it as it is.
    if (!user && !Char_IsChecked(g_char.repair)) {
        Char_ArgsAdd(a, L"--repair");
        Char_ArgsAdd(a, L"off");
    }
    if (!user && !Char_IsChecked(g_char.openParts)) {
        Char_ArgsAdd(a, L"--open-parts");
        Char_ArgsAdd(a, L"one-sided");
    }
    if (!user && Char_RemeshOn()) {
        Char_ArgsAdd(a, L"--remesh");
        Char_ArgsAdd(a, L"on");
    }
    g_char.runReduce = user || Char_IsChecked(g_char.reduce);
    if (!g_char.runReduce) {
        Char_ArgsAdd(a, L"--reduce");
        Char_ArgsAdd(a, L"off");
    }
    if (!Char_IsChecked(g_char.wheels)) {
        Char_ArgsAdd(a, L"--wheels");
        Char_ArgsAdd(a, L"off");
    }
    // The look (tab In-game look, In the race; preview feature). rldpack's
    // defaults follow the wheels (retail/retail with them, auto/off without);
    // only what differs is passed. The choices start at retail/retail
    // (Char_LookDefaults; in the user mode at rldpack's defaults): with the
    // wheels hidden --shadow retail --exhaust retail keeps the command and the
    // bytes of a character from before the look (with them nothing is passed).
    {
        const int wheels = Char_IsChecked(g_char.wheels);
        const int sh = Char_ListIndex(g_char.shadow, 3), ex = Char_ListIndex(g_char.exhaust, 3);
        if (sh != (wheels ? 0 : 1)) {
            Char_ArgsAdd(a, L"--shadow");
            Char_ArgsAdd(a, g_charShadowWords[sh]);
        }
        if (ex == 1) {
            Char_LookExhaustArg(a->exhaust, 160);
            Char_ArgsAdd(a, L"--exhaust");
            Char_ArgsAdd(a, a->exhaust);
        } else if (ex != (wheels ? 0 : 2)) {
            Char_ArgsAdd(a, L"--exhaust");
            Char_ArgsAdd(a, g_charExhaustWords[ex]);
        }
    }
    // The axes, when not the defaults (+Y up, +Z forward).
    if (Char_ListIndex(g_char.up, CHAR_UP_COUNT) != 0) {
        Char_ArgsAdd(a, L"--up");
        Char_ArgsAdd(a, g_charUpWords[Char_ListIndex(g_char.up, CHAR_UP_COUNT)]);
    }
    if (Char_ListIndex(g_char.forward, CHAR_FORWARD_COUNT) != 0) {
        Char_ArgsAdd(a, L"--forward");
        Char_ArgsAdd(a, g_charForwardWords[Char_ListIndex(g_char.forward, CHAR_FORWARD_COUNT)]);
    }
    // The palette: 64 colours instead of rldpack's 128 (not in the user mode).
    if (!user && Char_ListIndex(g_char.colors, CHAR_COLORS_COUNT) != 0) {
        Char_ArgsAdd(a, L"--colors");
        Char_ArgsAdd(a, g_charColorsWords[Char_ListIndex(g_char.colors, CHAR_COLORS_COUNT)]);
    }
    // An OBJ: the textures folder, when one is given.
    Char_FieldPath(g_char.textures, a->textures, CHAR_VAL);
    if (g_char.objOn && a->textures[0]) {
        Char_ArgsAdd(a, L"--textures");
        Char_ArgsAdd(a, a->textures);
    }
    // An OBJ: how its vertex colours meet its textures, when not auto.
    if (g_char.objOn && Char_VColorIndex() != 0) {
        Char_ArgsAdd(a, L"--vertex-colors");
        Char_ArgsAdd(a, g_charVColorWords[Char_VColorIndex()]);
    }
    // An OBJ: its own mesh and textures as well (Char_NativePassed): with
    // "Native model" ticked (card Import, a preview feature open to
    // everyone; not ticked at the start) or in the user mode, and Show kart
    // wheels off or a wheel model of the card Wheels (rldpack refuses it
    // beside the kart wheels without one).
    Char_NativeUpdate();
    if (Char_NativePassed()) {
        Char_ArgsAdd(a, L"--native-model");
        Char_ArgsAdd(a, L"on");
    }
    // The card Wheels (rs_wheels.c, preview feature): an author's wheel model
    // with the native model and the kart wheels shown (make-char writes WHLS
    // version 2; the classic model keeps the game's wheels), its size only
    // when it is not 100 %.
    if (Char_WheelPassed(a->wheel, CHAR_VAL)) {
        Char_ArgsAdd(a, L"--wheel-model");
        Char_ArgsAdd(a, a->wheel);
        if (CharWheels_SizePercent() != 100) {
            swprintf(a->wheelSize, 16, L"%d", CharWheels_SizePercent());
            Char_ArgsAdd(a, L"--wheel-size");
            Char_ArgsAdd(a, a->wheelSize);
        }
    }
    // The mask always, also the template's: the file carries the choice.
    Char_ArgsAdd(a, L"--mask");
    Char_ArgsAdd(a, g_charMasks[Char_MaskIndex()].word);
    // The minimap colour only when one is chosen (rldpack: like the template).
    if (g_char.mapColorSet) {
        swprintf(a->mapColor, 8, L"%02X%02X%02X", GetRValue(g_char.mapColor), GetGValue(g_char.mapColor),
                 GetBValue(g_char.mapColor));
        Char_ArgsAdd(a, L"--map-color");
        Char_ArgsAdd(a, a->mapColor);
    }
    if (a->icon[0]) {
        Char_ArgsAdd(a, L"--icon");
        Char_ArgsAdd(a, a->icon);
        // Framing, background and frame only when they differ from rldpack's
        // defaults (fit none, background alpha, frame none).
        if (Char_IconFitIndex() != CHAR_ICON_FIT_RLDPACK) {
            Char_ArgsAdd(a, L"--icon-fit");
            Char_ArgsAdd(a, g_charIconFits[Char_IconFitIndex()].word);
        }
        if (Char_IsChecked(g_char.iconCorners)) {
            Char_ArgsAdd(a, L"--icon-background");
            Char_ArgsAdd(a, L"corners");
        }
        if (Char_IsChecked(g_char.iconFrame)) {
            Char_ArgsAdd(a, L"--icon-frame");
            Char_ArgsAdd(a, L"retail");
        }
        if (check) {
            Char_TempPath(a->iconPrefix, CHAR_VAL, seq, L"");
            Char_ArgsAdd(a, L"--icon-preview");
            Char_ArgsAdd(a, a->iconPrefix);
        }
    }
    if (a->voices[0]) {
        int i;
        Char_ArgsAdd(a, L"--voices");
        Char_ArgsAdd(a, a->voices);
        // Only with a folder: without one the command stays that of a driver
        // without voices.
        if (Char_IsChecked(g_char.voiceNorm))
            Char_ArgsAdd(a, L"--voice-normalize");
        for (i = 0; i < g_char.voiceSetCount; i++) {
            size_t n = wcslen(g_char.voiceSet[i].name) + 16;
            a->voiceSet[i] = Rs_Alloc(n * sizeof(wchar_t));
            swprintf(a->voiceSet[i], n, L"%ls=%ls", g_char.voiceSet[i].name,
                     g_charVoiceEvents[g_char.voiceSet[i].event].word);
            Char_ArgsAdd(a, L"--voice");
            Char_ArgsAdd(a, a->voiceSet[i]);
        }
        if (check) {
            Char_TempPath(a->voicePrefix, CHAR_VAL, seq, L"");
            Char_ArgsAdd(a, L"--voice-preview");
            Char_ArgsAdd(a, a->voicePrefix);
        }
    }
    if (check) {
        Char_TempPath(a->preview, CHAR_VAL, seq, L".rldpv");
        Char_ArgsAdd(a, L"--preview");
        Char_ArgsAdd(a, a->preview);
    }
    // A path the author chose comes first; without one the game's characters
    // folder when the game is known, else rldpack's default next to the model.
    if (out)
        Char_CleanPath(a->out, CHAR_VAL, out);
    else
        Char_FieldPath(g_char.out, a->out, CHAR_VAL);
    if (!a->out[0]) {
        Char_DefaultOut(a->model, a->out, CHAR_VAL);
        if (check)
            Char_Copy(g_char.checkDefault, CHAR_VAL, a->out);
    } else if (check) {
        g_char.checkDefault[0] = 0;
    }
    if (a->out[0]) {
        Char_ArgsAdd(a, L"--out");
        Char_ArgsAdd(a, a->out);
    }
    return 1;
}

static int Char_StartJob(HWND page, int kind, const wchar_t *const *args, int argc, int seq)
{
    wchar_t *cmd = Rs_Alloc(CHAR_CMD_CAP * sizeof(wchar_t));
    int i;

    Char_JobReset();
    // The command first in the raw output ("Show rldpack output"), quoted as
    // the shell passes it: copied to a command prompt in the folder of
    // Reload Studio it runs the same check or build.
    cmd[0] = 0;
    Rs_AppendArg(cmd, CHAR_CMD_CAP, L"ReloadStudio.exe");
    Rs_AppendArg(cmd, CHAR_CMD_CAP, L"--rldpack");
    for (i = 0; i < argc; i++)
        Rs_AppendArg(cmd, CHAR_CMD_CAP, args[i]);
    Char_RawAppend(cmd);
    Rs_Free(cmd);
    g_char.jobKind = kind;
    g_char.jobSeq = seq;
    g_char.jobId = Rs_RunRldpack(page, args, argc);
    if (!g_char.jobId) {
        g_char.jobKind = CHAR_JOB_NONE;
        g_char.jobSeq = 0;
    } else {
        // The bar and Cancel when it takes longer than a moment (a bar shown
        // for the check just ended stays for this one).
        g_char.progressPos = -1;
        g_char.slowText[0] = 0;
        Char_Copy(g_char.progressText, 64, kind == CHAR_JOB_BUILD ? L"Building..." : L"Checking...");
        InvalidateRect(g_char.progress, NULL, FALSE);
        EnableWindow(g_char.cancel, TRUE);
        if (!g_char.progressOn)
            SetTimer(page, CHAR_TIMER_BUSY, CHAR_PROGRESS_DELAY, NULL);
    }
    Char_UpdateButtons();
    return g_char.jobId;
}

// Nothing to check: no model.
static void Char_NoModel(HWND page)
{
    Char_MsgClear();
    Char_JobReset();
    if (g_char.showRaw)
        Char_RawRefresh();
    g_char.checked = 0;
    g_char.previewShown = 0;
    g_char.previewPoses = 0;
    RsView_Clear(g_char.view, NULL);
    g_char.viewPartCount = 0;
    g_char.viewFirst = -1;
    Char_ViewNotePart(L"Grey: Crash with his kart - the size a model is fitted to. Choose a PLY or OBJ model.");
    Char_ViewNotePart(CHAR_VIEW_MOUSE_TEXT);
    Char_ViewNoteShow(RS_COL_MUTED);
    Char_ApplyQuality();
    memset(g_char.tabErrors, 0, sizeof(g_char.tabErrors));
    memset(g_char.tabWarnings, 0, sizeof(g_char.tabWarnings));
    g_char.modelRead = 0;
    g_char.stampKnown = 0;
    Char_SetInfo(CHAR_START_TEXT, RS_COL_MUTED);
    Char_ImportClear();
    Char_Headline(L"Choose a PLY or OBJ model to start", RS_COL_MUTED);
    Char_ReduceFitShow(page, 0);
    Char_ProgressShow(page, 0);
    Char_VoicesClear();
    Char_EmptyText(L"Choose a PLY or OBJ model. What rldpack finds shows up here.");
    Char_UpdateButtons();
    g_char.checkModel[0] = 0;
    CharWheels_ModelChecked(page, L"", g_char.sizeNow, 0);
    CharAnim_ModelChecked(page, L"", g_char.sizeNow, 0);
}

// The switches that change nothing rldpack does with the model: name,
// driving style, mask, minimap colour, output.
static int Char_IsMetaSwitch(const wchar_t *arg)
{
    return wcscmp(arg, L"--name") == 0 || wcscmp(arg, L"--class") == 0 || wcscmp(arg, L"--mask") == 0 ||
           wcscmp(arg, L"--map-color") == 0 || wcscmp(arg, L"--out") == 0 || wcscmp(arg, L"--shadow") == 0 ||
           wcscmp(arg, L"--exhaust") == 0;
}

// The command of a check without its temp files (the values of --preview,
// --icon-preview and --voice-preview), one argument per line (Rs_Free).
// model = 1: also without the switches of Char_IsMetaSwitch and their values.
static wchar_t *Char_ArgsKey(const struct CharArgs *a, int model)
{
    size_t n = 1;
    wchar_t *key;
    int i;
    for (i = 0; i < a->n; i++)
        n += wcslen(a->v[i]) + 1;
    key = Rs_Alloc(n * sizeof(wchar_t));
    key[0] = 0;
    for (i = 0; i < a->n; i++) {
        int temp = i > 0 && (wcscmp(a->v[i - 1], L"--preview") == 0 || wcscmp(a->v[i - 1], L"--icon-preview") == 0 ||
                             wcscmp(a->v[i - 1], L"--voice-preview") == 0);
        if (model && Char_IsMetaSwitch(a->v[i])) {
            i++;        // and its value
            continue;
        }
        wcscat(key, temp ? L"-" : a->v[i]);
        wcscat(key, L"\n");
    }
    return key;
}

// The kept result of this command whose files have not been written since
// (stamp: of the model, the icon and the voices folder now), -1 = none.
// model = 1: key is a modelKey, and the result must name its output.
static int Char_CacheFindKey(const wchar_t *key, unsigned long long stamp, int model)
{
    int i;
    for (i = 0; i < CHAR_CACHE; i++) {
        const wchar_t *own = model ? g_char.cache[i].modelKey : g_char.cache[i].key;
        if (own && wcscmp(own, key) == 0 && g_char.cache[i].stampFields == stamp &&
            (!model || (g_char.cache[i].out && g_char.cache[i].out[0])) &&
            Char_StampList(g_char.cache[i].stampFiles) == g_char.cache[i].stampImport)
            return i;
    }
    return -1;
}

static int Char_CacheFind(const wchar_t *key, unsigned long long stamp)
{
    return Char_CacheFindKey(key, stamp, 0);
}

// Keeps the result of the check just shown (its keys are taken over), the
// newest first; the oldest goes when all places are taken. modelRaw: the
// raw output of the run that read the model, NULL = the one shown.
static void Char_CacheStore(wchar_t *key, wchar_t *modelKey, int exitCode, int seq, const wchar_t *modelRaw)
{
    int i;
    for (i = 0; i < CHAR_CACHE; i++)
        if (g_char.cache[i].key && wcscmp(g_char.cache[i].key, key) == 0)
            Char_CacheDrop(i--);
    if (g_char.cache[CHAR_CACHE - 1].key)
        Char_CacheDrop(CHAR_CACHE - 1);
    Char_CacheTouch(CHAR_CACHE - 1);    // the empty place to the front
    g_char.cache[0].key = key;
    g_char.cache[0].modelKey = modelKey;
    g_char.cache[0].raw = Rs_Dup(g_char.rawText ? g_char.rawText : L"");
    g_char.cache[0].modelRaw = Rs_Dup(modelRaw ? modelRaw : g_char.cache[0].raw);
    g_char.cache[0].feed = Rs_Dup(g_char.feedText ? g_char.feedText : L"");
    g_char.cache[0].out = Rs_Dup(g_charJob.outSeen ? g_charJob.out : L"");
    g_char.cache[0].exitCode = exitCode;
    g_char.cache[0].seq = seq;
    g_char.cache[0].stampFields = g_char.stampFields;
    g_char.cache[0].stampImport = g_char.stampImport;
    g_char.cache[0].stampFiles = Char_ListDup(g_char.stampFiles);
}

static int Char_CheckDone(HWND page, int exitCode, int seq);

// The result kept at i, shown again as if rldpack had just reported it: its
// lines into the raw output and the parser, then the end of a check with its
// temp files. stamp: of the model, the icon and the voices folder now.
static void Char_CacheShow(HWND page, int i, const wchar_t *model, unsigned long long stamp)
{
    Char_CacheTouch(i);
    Char_ProgressShow(page, 0);
    Char_JobReset();
    Char_RawAppend(L"Nothing has changed since this check (the same command, the same files): its result is shown "
                   L"again without running rldpack. Check runs it anew.");
    Char_EachLine(g_char.cache[0].raw, 0, Char_RawAppend);
    Char_EachLine(g_char.cache[0].feed, 0, Char_Feed);
    Char_Copy(g_char.checkModel, CHAR_VAL, model);
    g_char.stampRun = stamp;
    if (Rs_Automating())
        Rs_AutoLog(L"  result kept: nothing has changed since an earlier check - shown again, rldpack does not run");
    if (g_char.showRaw)
        Char_RawRefresh();
    Char_CheckDone(page, g_char.cache[0].exitCode, g_char.cache[0].seq);
    Char_UpdateButtons();
}

// The message codes and @value keys of the switches of Char_IsMetaSwitch, and
// of the template, the author and the version they come with.
static int Char_IsMetaCode(const wchar_t *code)
{
    static const wchar_t *const codes[] = { L"name", L"name-upper", L"name-chars", L"name-long", L"class", L"mask",
                                            L"map-color", L"template", L"author", L"version", L"shadow", L"exhaust" };
    int i;
    for (i = 0; i < (int)(sizeof(codes) / sizeof(codes[0])); i++)
        if (wcscmp(code, codes[i]) == 0)
            return 1;
    return 0;
}

static int Char_IsMetaValue(const wchar_t *key)
{
    static const wchar_t *const keys[] = { L"name", L"author", L"char_version", L"template", L"class", L"mask",
                                           L"map-color", L"out", L"shadow", L"exhaust", L"exhaust-point" };
    int i;
    for (i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])); i++)
        if (wcscmp(key, keys[i]) == 0)
            return 1;
    return 0;
}

// CHAR_LINE_META: a @msg or @value of those; CHAR_LINE_END: @result, @end;
// CHAR_LINE_MODEL: everything else (also a line that is no machine line).
// *error: a @msg error.
static int Char_LineKind(const wchar_t *line, int *error)
{
    wchar_t *copy = Rs_Dup(line);
    wchar_t *f[8];
    int n = Rs_SplitMachine(copy, f, 8), kind = CHAR_LINE_MODEL;

    *error = 0;
    if (n > 0 && wcscmp(f[0], L"msg") == 0) {
        *error = Rs_SeverityFromText(Char_Field(f, n, 1)) == RS_SEV_ERROR;
        if (Char_IsMetaCode(Char_Field(f, n, 2)))
            kind = CHAR_LINE_META;
    } else if (n > 0 && wcscmp(f[0], L"value") == 0) {
        if (Char_IsMetaValue(Char_Field(f, n, 1)))
            kind = CHAR_LINE_META;
    } else if (n > 0 && (wcscmp(f[0], L"result") == 0 || wcscmp(f[0], L"end") == 0)) {
        kind = CHAR_LINE_END;
    }
    Rs_Free(copy);
    return kind;
}

// Only the switches of Char_IsMetaSwitch differ from the result kept at
// base: rldpack checks them alone (CHAR_JOB_META), with --model a file of
// the temp folder that is never written - it reports them and stops at the
// model. Char_MetaDone puts its lines and the model part of base together.
// 1 = started.
static int Char_MetaStart(HWND page, const struct CharArgs *a, int base, unsigned long long stamp)
{
    const wchar_t *v[24];
    wchar_t none[CHAR_VAL];
    int n = 0, i, out = 0, started;

    // A result kept without the native part (a check that stopped at the name
    // leaves it out) is no base when the native model is passed: rldpack
    // checks everything, so an error of the native part is not missed.
    if (Char_NativePassed() && (!g_char.cache[base].feed || !wcsstr(g_char.cache[base].feed, L"@char\tnative-model\t")))
        return 0;
    Char_TempPath(none, CHAR_VAL, 0, L"-none.ply");
    v[n++] = L"make-char";
    v[n++] = L"--machine";
    v[n++] = L"--check";
    v[n++] = L"--model";
    v[n++] = none;
    for (i = 0; i + 1 < a->n && n + 2 <= 22; i++) {
        // --wheels too: the defaults of the look follow it (it stops at the model all the same)
        if (Char_IsMetaSwitch(a->v[i]) || wcscmp(a->v[i], L"--template") == 0 || wcscmp(a->v[i], L"--wheels") == 0) {
            out |= wcscmp(a->v[i], L"--out") == 0;
            v[n++] = a->v[i];
            v[n++] = a->v[++i];
        }
    }
    if (!out) {
        // As rldpack names it for this model: what it named for base.
        v[n++] = L"--out";
        v[n++] = g_char.cache[base].out;
    }
    g_char.metaModelRaw = Rs_Dup(g_char.cache[base].modelRaw);
    g_char.metaFeed = Rs_Dup(g_char.cache[base].feed);
    g_char.metaSeq = g_char.cache[base].seq;
    Char_CacheTouch(base);
    Char_Copy(g_char.checkModel, CHAR_VAL, a->model);
    g_char.stampRun = stamp;
    Char_Headline(L"Checking...", RS_COL_MUTED);
    if (Rs_Automating())
        Rs_AutoLog(L"  result kept: only name, driving style, mask, minimap colour or output changed - rldpack checks them, "
                   L"the model part is that of an earlier check");
    started = Char_StartJob(page, CHAR_JOB_META, v, n, 0) != 0;
    if (!started) {
        Rs_Free(g_char.metaModelRaw);
        Rs_Free(g_char.metaFeed);
        g_char.metaModelRaw = g_char.metaFeed = NULL;
    }
    return started;
}

static int Char_CheckNow(HWND page);

// The line of the run that read the model, unless it is about a switch of
// Char_IsMetaSwitch (those come from the CHAR_JOB_META run) or its end; its
// @result is noted (g_char's merge state below).
static int g_charMergeResult, g_charMergeChecked, g_charMergeErrors;

static void Char_MergeModelLine(const wchar_t *line)
{
    int error, kind = Char_LineKind(line, &error);
    if (error)
        g_charMergeErrors++;
    if (kind == CHAR_LINE_MODEL) {
        Char_Feed(line);
    } else if (kind == CHAR_LINE_END && wcsncmp(line, L"@result", 7) == 0) {
        g_charMergeResult = 1;
        g_charMergeChecked = wcsncmp(line + 7, L"\tchecked", 8) == 0;
    }
}

static void Char_MergeMetaLine(const wchar_t *line)
{
    int error;
    if (Char_LineKind(line, &error) == CHAR_LINE_META)
        Char_Feed(line);
}

// 1 if the CHAR_JOB_META run said what was asked: its protocol, the output,
// and no message but about its switches and the model it was not to find.
static int Char_MetaUsable(void)
{
    struct CharJobData *j = &g_charJob;
    int i;
    if (!j->protocolSeen || j->protocol != RS_PROTOCOL || !j->outSeen || !j->mask[0])
        return 0;
    for (i = 0; i < j->msgCount; i++) {
        const wchar_t *code = j->msgs[i].code ? j->msgs[i].code : L"";
        if (!Char_IsMetaCode(code) && !Char_EndsWith(code, L"-open") && wcsncmp(code, L"model-", 6) != 0)
            return 0;
    }
    return 1;
}

// End of a CHAR_JOB_META run: its lines about the switches, the lines of the
// result kept but for those, and a result of the two (checked when there is
// no error left) as the result of a check. Where the run said anything else,
// or the result kept has no result line, rldpack checks everything.
static void Char_MetaDone(HWND page)
{
    wchar_t *metaRaw = Rs_Dup(g_char.rawText ? g_char.rawText : L"");
    wchar_t *modelRaw = g_char.metaModelRaw, *feed = g_char.metaFeed;
    wchar_t *key = g_char.runKey, *modelKey = g_char.runModelKey;
    wchar_t line[CHAR_VAL + 64];
    int seq = g_char.metaSeq, errors, exitCode, usable;

    g_char.metaModelRaw = g_char.metaFeed = NULL;
    g_char.runKey = g_char.runModelKey = NULL;
    g_char.metaSeq = 0;
    Char_JobReset();
    Char_EachLine(metaRaw, 1, Char_Feed);
    usable = Char_MetaUsable();
    Char_JobReset();
    g_charMergeResult = g_charMergeChecked = g_charMergeErrors = 0;
    if (usable) {
        Char_RawAppend(L"Only the name, the driving style, the mask, the minimap colour or the output changed: rldpack "
                       L"checked them alone (the first command; its model file is never written, so it stops there), "
                       L"the model part is the result of the check before (the second command).");
        Char_EachLine(metaRaw, 0, Char_RawAppend);
        Char_EachLine(modelRaw, 0, Char_RawAppend);
        Char_EachLine(metaRaw, 1, Char_MergeMetaLine);
        Char_EachLine(feed, 0, Char_MergeModelLine);
    }
    if (!usable || !g_charMergeResult || (!g_charMergeChecked && g_charMergeErrors == 0)) {
        if (Rs_Automating())
            Rs_AutoLog(L"  result kept: could not be put together with the new run - rldpack checks everything");
        Rs_Free(metaRaw);
        Rs_Free(modelRaw);
        Rs_Free(feed);
        Rs_Free(key);
        Rs_Free(modelKey);
        Char_CheckNow(page);
        return;
    }
    errors = Char_CountMsgs(RS_SEV_ERROR);
    exitCode = errors ? 1 : 0;
    swprintf(line, CHAR_VAL + 64, L"@result\t%ls\t%ls\t\t", errors ? L"failed" : L"checked", g_charJob.out);
    Char_Feed(line);
    swprintf(line, CHAR_VAL + 64, L"@end\t%d", exitCode);
    Char_Feed(line);
    if (Char_CheckDone(page, exitCode, seq) && key && modelKey) {
        Char_CacheStore(key, modelKey, exitCode, seq, modelRaw);
        key = modelKey = NULL;
    }
    Rs_Free(key);
    Rs_Free(modelKey);
    Rs_Free(metaRaw);
    Rs_Free(modelRaw);
    Rs_Free(feed);
}

// Checks with the values of the fields; force = 0: a result kept for the
// same command and files is shown again instead (Char_CacheFind). Return:
// 1 = rldpack running, 0 = done without rldpack (the message is already
// there), -1 = nothing to check.
static int Char_CheckRun(HWND page, int force)
{
    struct CharArgs a;
    int started, seq, hit;
    unsigned long long stamp;

    // While building: the fields no longer match the last check. Build stays
    // locked, the check follows as soon as the build has ended (Char_JobDone).
    if (g_char.jobId && g_char.jobKind == CHAR_JOB_BUILD) {
        g_char.checked = 0;
        g_char.checkAfterBuild = 1;
        Char_UpdateButtons();
        return -1;
    }
    KillTimer(page, CHAR_TIMER_CHECK);
    g_char.timer = 0;
    g_char.checkAfterBuild = 0;
    Char_Abandon();             // a running check is thereby outdated
    g_char.checked = 0;
    seq = g_char.seq + 1;       // taken only when rldpack runs
    if (!Char_MakeArgs(&a, 1, NULL, seq)) {
        Char_ArgsFree(&a);
        Char_NoModel(page);
        return -1;
    }
    stamp = Char_StampFields(a.model, a.icon, a.voices);
    g_char.runKey = Char_ArgsKey(&a, 0);
    g_char.runModelKey = Char_ArgsKey(&a, 1);
    hit = force ? -1 : Char_CacheFind(g_char.runKey, stamp);
    if (hit >= 0) {
        Rs_Free(g_char.runKey);
        Rs_Free(g_char.runModelKey);
        g_char.runKey = g_char.runModelKey = NULL;
        Char_CacheShow(page, hit, a.model, stamp);
        Char_ArgsFree(&a);
        return 0;
    }
    hit = force ? -1 : Char_CacheFindKey(g_char.runModelKey, stamp, 1);
    if (hit >= 0 && Char_MetaStart(page, &a, hit, stamp)) {
        Char_ArgsFree(&a);
        return 1;
    }
    g_char.seq = seq;
    Char_Copy(g_char.checkModel, CHAR_VAL, a.model);
    g_char.stampRun = stamp;
    Char_Headline(L"Checking...", RS_COL_MUTED);
    started = Char_StartJob(page, CHAR_JOB_CHECK, a.v, a.n, seq) != 0;
    Char_ArgsFree(&a);
    if (!started) {
        Rs_Free(g_char.runKey);
        Rs_Free(g_char.runModelKey);
        g_char.runKey = g_char.runModelKey = NULL;
        Char_StartFailed(L"Cannot build yet - 1 problem(s)");
        return 0;
    }
    return 1;
}

// A check after a change: a result kept for it is shown again.
static int Char_Check(HWND page)
{
    return Char_CheckRun(page, 0);
}

// A check asked for (Check, Enter, a model chosen): rldpack always runs.
static int Char_CheckNow(HWND page)
{
    return Char_CheckRun(page, 1);
}

// Return: 1 = rldpack builds, 0 = refused (replacing declined), -1 = not possible.
static int Char_Build(HWND page)
{
    struct CharArgs a;
    wchar_t path[CHAR_VAL];
    wchar_t field[CHAR_VAL];
    int started;

    if (!Char_CanBuild())
        return -1;
    // An empty field: the game's characters folder as it is now (the game may
    // have been chosen since the last check), else rldpack's default.
    Char_FieldPath(g_char.out, field, CHAR_VAL);
    if (!field[0]) {
        wchar_t model[CHAR_VAL];
        Char_FieldPath(g_char.model, model, CHAR_VAL);
        Char_DefaultOut(model, field, CHAR_VAL);
    }
    Char_CleanPath(path, CHAR_VAL, field[0] ? field : g_char.checkedPath);
    if (path[0] && Rs_FileExists(path)) {
        wchar_t question[CHAR_VAL + 64];
        swprintf(question, CHAR_VAL + 64, L"%ls exists. Replace it?", path);
        if (!Rs_AskYesNo(Rs_MainWindow(), L"Replace character?", question))
            return 0;
    }
    // The game's characters folder is made when it is missing (a fresh game).
    // When that fails (a write-protected game folder) nothing is built.
    if (path[0] && Char_InGameCharDir(path)) {
        wchar_t dir[CHAR_VAL];
        Rs_PathDir(dir, CHAR_VAL, path);
        if (!Rs_DirExists(dir)) {
            if (CreateDirectoryW(dir, NULL) || GetLastError() == ERROR_ALREADY_EXISTS) {
                if (Rs_Automating())
                    Rs_AutoLog(L"  build: made the folder %ls", dir);
            } else {
                wchar_t text[CHAR_VAL + 64];
                swprintf(text, CHAR_VAL + 64, L"Cannot make the folder %ls.", dir);
                Char_MsgClear();
                Char_MsgAdd(-1, RS_SEV_ERROR, text,
                              L"Choose another output with Browse, or make the folder yourself.");
                Char_Headline(L"Not built - 1 problem(s)", RS_COL_ERROR);
                if (Rs_Automating())
                    Rs_AutoLog(L"  build: cannot make the folder %ls", dir);
                return -1;
            }
        }
    }
    if (!Char_MakeArgs(&a, 0, path[0] ? path : NULL, 0)) {
        Char_ArgsFree(&a);
        return -1;
    }
    Char_Copy(g_char.buildOut, CHAR_VAL, a.out);
    g_char.buildWheelsOff = !Char_IsChecked(g_char.wheels);
    g_char.buildMask = Char_MaskIndex();
    g_char.buildMapSet = g_char.mapColorSet;
    g_char.buildMapColor = g_char.mapColor;
    Char_Headline(L"Building...", RS_COL_MUTED);
    started = Char_StartJob(page, CHAR_JOB_BUILD, a.v, a.n, 0) != 0;
    Char_ArgsFree(&a);
    if (!started) {
        Char_StartFailed(L"Not built - 1 problem(s)");
        return -1;
    }
    return 1;
}

// A field has changed: check in delay ms (a timer already waiting is
// replaced, so the last change sets the time).
static void Char_ChangedAfter(HWND page, UINT delay)
{
    if (g_char.applying)
        return;
    g_char.checked = 0;
    SetTimer(page, CHAR_TIMER_CHECK, delay, NULL);
    g_char.timer = 1;
    Char_UpdateButtons();
}

// Typed into a text field: check in 600 ms.
static void Char_Changed(HWND page)
{
    Char_ChangedAfter(page, CHAR_CHECK_DELAY);
}

// Clicked (a list, a tick box, the minimap colour): check in 100 ms.
static void Char_ChangedSoon(HWND page)
{
    Char_ChangedAfter(page, CHAR_CLICK_DELAY);
}

// The card Wheels changed its model or its size (CharWheels_ExportChanged):
// the note of the native model, and a check like a typed field or a click.
static void Char_WheelsFollow(HWND page)
{
    const int changed = CharWheels_ExportChanged();
    if (!changed)
        return;
    Char_NativeUpdate();
    if (g_char.model) {
        wchar_t model[CHAR_VAL];
        Char_FieldPath(g_char.model, model, CHAR_VAL);
        if (model[0])
            Char_ChangedAfter(page, changed == 1 ? CHAR_CHECK_DELAY : CHAR_CLICK_DELAY);
    }
}

static void Char_MapColorSet(HWND page, int set, COLORREF color)
{
    g_char.mapColorSet = set;
    g_char.mapColor = color;
    Char_MapColorShow();
    Char_ChangedSoon(page);
}

// "Choose...": the colour dialog, starting at the colour shown.
static void Char_MapColorPick(HWND page)
{
    CHOOSECOLORW cc;
    memset(&cc, 0, sizeof(cc));
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = Rs_MainWindow();
    cc.rgbResult = g_char.mapColorSet ? g_char.mapColor : CHAR_TEMPLATE_MAP_COLOR;
    cc.lpCustColors = g_char.mapCustom;
    cc.Flags = CC_RGBINIT | CC_FULLOPEN;
    if (ChooseColorW(&cc))
        Char_MapColorSet(page, 1, cc.rgbResult);
}

// End of a check: range, model facts, preview and icon into the page. If the
// size is outside the range rldpack reported, the slider goes to the nearest
// allowed size and the check runs again at once.
static int Char_CheckDone(HWND page, int exitCode, int seq)
{
    struct CharJobData *j = &g_charJob;
    const struct CharMsg *sizeMsg = Char_FindMsg(L"char-size");
    wchar_t sizeWhy[512];

    sizeWhy[0] = 0;
    if (sizeMsg && sizeMsg->text) {
        // "Size N% is outside lo..hi% for this model: <why>. Choose a size in that range."
        const wchar_t *why = wcsstr(sizeMsg->text, L"for this model: ");
        wchar_t *advice;
        Char_Copy(sizeWhy, 512, why ? why + 16 : L"");
        advice = wcsstr(sizeWhy, L" Choose a size");
        if (advice)
            *advice = 0;
    }
    if (j->outSeen) {
        wchar_t cue[CHAR_VAL + 32];
        swprintf(cue, CHAR_VAL + 32, Char_InGameCharDir(j->out) ? L"In the game's characters folder: %ls"
                                                                : L"Next to the model: %ls",
                 Rs_PathName(j->out));
        SendMessageW(g_char.out, EM_SETCUEBANNER, FALSE, (LPARAM)cue);
    }
    g_char.rangeNone = j->rangeNone;
    if (j->rangeNone) {
        g_char.rangeKnown = 0;
        g_char.sizeLocked = 0;
        Char_SizeRangeShow();
        Char_SizeNote();
    } else if (j->rangeSeen) {
        int was = g_char.sizeNow;
        int lockedBefore = g_char.sizeLocked;
        g_char.rangeKnown = 1;
        g_char.rangeLo = j->rangeLo;
        g_char.rangeHi = j->rangeHi;
        // The reason belongs to the bound the checked size went past.
        if (sizeWhy[0] && (was < g_char.rangeLo || was > g_char.rangeHi))
            Char_Copy(g_char.sizeWhy[was > g_char.rangeHi ? 1 : 0], 512, sizeWhy);
        Char_SizeRangeShow();
        g_char.applying = 1;
        if (!Char_SizeSet(was)) {
            // In the range: an earlier "Locked at" stays until the slider moves.
            g_char.sizeLocked = lockedBefore;
            Char_SizeNote();
        } else {
            g_char.applying = 0;
            if (Rs_Automating())
                Rs_AutoLog(L"  size: %d %% is outside %d..%d %% for this model - set to %d %%", was,
                           g_char.rangeLo, g_char.rangeHi, g_char.sizeNow);
            if (!Char_CacheHasSeq(seq, -1))
                Char_TempDelete(seq);
            if (Char_CheckNow(page) >= 0) {
                Char_SizeNote();
                return 0;
            }
        }
        g_char.applying = 0;
    } else if (j->modelSeen && wcscmp(j->modelState, L"ok") != 0) {
        Char_SizeForget();
    }
    g_char.modelRead = j->modelSeen && wcscmp(j->modelState, L"ok") == 0;
    g_char.lookQuadKnown = j->lookQuadSeen;
    if (j->lookQuadSeen)
        memcpy(g_char.lookQuad, j->lookQuad, sizeof(g_char.lookQuad));
    Char_ApplyModelInfo();
    Char_ApplyImport();
    Char_ApplyFit();
    Char_ApplyVoices(seq);
    Char_ApplyQuality();
    Char_ApplyPreview(seq);
    Char_LookPreview();
    Char_ApplyIcon(seq);
    // The temp files stay with the result kept (Char_CacheStore).
    Char_StampShown();
    Char_ShowCheckResult(exitCode);
    // The cards of the preview features follow the model just checked.
    CharWheels_ModelChecked(page, g_char.checkModel, g_char.sizeNow, g_char.checked);
    CharAnim_ModelChecked(page, g_char.checkModel, g_char.sizeNow, g_char.checked);
    return 1;
}

static void Char_BuildDone(HWND page, int exitCode)
{
    struct CharJobData *j = &g_charJob;
    int ok = j->resultSeen && wcscmp(j->resultState, L"ok") == 0;
    wchar_t text[CHAR_VAL + 64];
    wchar_t detail[192];
    wchar_t size[32];

    Char_CountTabs();
    Char_MsgClear();
    Char_EmptyText(L"rldpack reported nothing.");
    if (ok) {
        // What is in the file: rldpack's @value lines, else what the build passed.
        int wheelsOff = j->wheels[0] ? wcsncmp(j->wheels, L"off", 3) == 0 : g_char.buildWheelsOff;
        const wchar_t *mask = wcsncmp(j->mask, L"aku", 3) == 0   ? g_charMasks[0].name
                              : wcsncmp(j->mask, L"uka", 3) == 0 ? g_charMasks[1].name
                                                                 : g_charMasks[g_char.buildMask].name;
        // The minimap colour only when one is in the file: rldpack's word, else what the build passed.
        wchar_t mapText[64];
        mapText[0] = 0;
        if (j->mapColor[0] && wcsncmp(j->mapColor, L"template", 8) != 0)
            swprintf(mapText, 64, L", map colour %.6ls", j->mapColor);
        else if (!j->mapColor[0] && g_char.buildMapSet)
            swprintf(mapText, 64, L", map colour %02X%02X%02X", GetRValue(g_char.buildMapColor),
                     GetGValue(g_char.buildMapColor), GetBValue(g_char.buildMapColor));
        // The voices only when a folder was given: how many clips are in the file.
        if (j->voicesSeen) {
            wchar_t voiceText[40];
            swprintf(voiceText, 40, L", %d voice clip%ls", j->voiceClips, j->voiceClips == 1 ? L"" : L"s");
            Char_Append(mapText, 64, voiceText);
        }
        Char_Copy(g_char.built, CHAR_VAL, j->resultPath);
        g_char.builtBytes = j->resultBytes;
        Char_Copy(g_char.builtSha, 80, j->resultSha);
        Char_SizeText(size, 32, j->resultBytes < 0 ? 0 : j->resultBytes);
        // The mask and the wheels in the headline: a choice that did not come
        // through (a combo turned by the mouse wheel) shows at once.
        swprintf(text, CHAR_VAL + 64, L"Built: %ls (%ls) - mask %ls, kart wheels %ls%ls. Restart the game to load it.",
                 Rs_PathName(g_char.built), size, mask, wheelsOff ? L"hidden" : L"shown", mapText);
        Char_Headline(text, RS_COL_OK);
        swprintf(text, CHAR_VAL + 64, L"Built %ls", g_char.built);
        swprintf(detail, 192, L"Kart wheels %ls, mask %ls%ls - SHA-256 %ls", wheelsOff ? L"hidden" : L"shown", mask, mapText,
                 g_char.builtSha);
        Char_MsgAdd(-1, RS_SEV_OK, text, detail);
        // The game loads characters only from the folder characters next to
        // it, and reads that folder once, when it starts.
        if (!Char_InGameCharDir(g_char.built)) {
            wchar_t dir[CHAR_VAL];
            if (Char_GameCharDir(dir, CHAR_VAL)) {
                swprintf(text, CHAR_VAL + 64, L"Copy it into %ls - the game loads characters only from there.", dir);
                Char_MsgAdd(-1, RS_SEV_NOTE, text, NULL);
            } else {
                Char_MsgAdd(-1, RS_SEV_NOTE,
                              L"Copy it into the characters folder next to ctr_native.exe - the game loads characters "
                              L"only from there.",
                              L"Reload Studio does not know the game yet: choose it on the page Test in game, then "
                              L"a build with an empty Output goes into its characters folder.");
            }
        }
        Char_MsgAdd(-1, RS_SEV_INFO, L"Restart the game to load the new file.",
                      L"The game reads the characters folder only when it starts.");
        Char_AddRunProblems(exitCode, 1);
        Char_AddMsgs(RS_SEV_WARNING);
        Char_AddMsgs(RS_SEV_NOTE);
        Rs_ConfigSet(L"char.out", g_char.built);
        if (Rs_Automating()) {
            Rs_AutoLog(L"  build: ok %ls %lld bytes sha256 %ls", g_char.built, g_char.builtBytes, g_char.builtSha);
            Rs_AutoLog(L"  build: kart wheels %ls, mask %ls%ls", wheelsOff ? L"hidden" : L"shown", mask, mapText);
        }
    } else {
        int errors;
        g_char.checked = 0;
        g_char.built[0] = 0;
        errors = Char_AddRunProblems(exitCode, 0);
        errors += Char_CountMsgs(RS_SEV_ERROR);
        Char_AddMsgs(RS_SEV_ERROR);
        Char_AddMsgs(RS_SEV_WARNING);
        Char_AddMsgs(RS_SEV_NOTE);
        swprintf(text, CHAR_VAL + 64, L"Not built - %d problem(s)", errors);
        Char_Headline(text, RS_COL_ERROR);
        if (Rs_Automating())
            Rs_AutoLog(L"  build: failed, %d error(s)", errors);
    }
    Char_Relayout(page);        // show or hide "Show in folder"
}

// "<out>.part" of the build: rldpack writes the character there first and
// renames it at the end; what a build cancelled (or ended otherwise) left
// goes.
static void Char_DeletePart(void)
{
    wchar_t part[CHAR_VAL + 8];
    if (!g_char.buildOut[0])
        return;
    swprintf(part, CHAR_VAL + 8, L"%ls.part", g_char.buildOut);
    if (Rs_FileExists(part) && DeleteFileW(part) && Rs_Automating())
        Rs_AutoLog(L"  build: deleted %ls", part);
}

// A check or build ended by Cancel: nothing of it is shown; the result shown
// before stays in the view, the headline says what happened.
static void Char_CancelDone(HWND page, int kind, int seq)
{
    Char_MsgClear();
    if (kind == CHAR_JOB_BUILD) {
        Char_MsgAdd(-1, RS_SEV_INFO, L"The build was cancelled - nothing was written.",
                    L"A character file of that name from before is left as it was.");
        Char_Headline(L"Cancelled - nothing was written", RS_COL_WARNING);
        if (Rs_Automating())
            Rs_AutoLog(L"  build: cancelled");
        // The check before the build still holds: Build can be pressed again.
        if (g_char.checkAfterBuild)
            Char_Check(page);
        return;
    }
    Rs_Free(g_char.runKey);
    Rs_Free(g_char.runModelKey);
    Rs_Free(g_char.metaModelRaw);
    Rs_Free(g_char.metaFeed);
    g_char.runKey = g_char.runModelKey = g_char.metaModelRaw = g_char.metaFeed = NULL;
    g_char.metaSeq = 0;
    if (seq > 0)
        Char_TempDelete(seq);
    g_char.checked = 0;
    Char_MsgAdd(-1, RS_SEV_INFO, L"The check was cancelled.", L"Check starts it again, and so does a change to a field.");
    if (g_char.slowText[0]) {
        // What rldpack said of the reduction stays: the next try takes as long.
        wchar_t line[128];
        swprintf(line, 128, L"Cancelled - Check starts it again (the reduction takes %ls)", g_char.slowText);
        Char_Headline(line, RS_COL_WARNING);
    } else {
        Char_Headline(L"Cancelled - Check starts it again", RS_COL_WARNING);
    }
    if (Rs_Automating())
        Rs_AutoLog(L"  check: cancelled");
}

static void Char_JobDone(HWND page, int exitCode)
{
    int kind = g_char.jobKind;
    int seq = g_char.jobSeq;

    g_char.jobId = 0;
    g_char.jobKind = CHAR_JOB_NONE;
    g_char.jobSeq = 0;
    Char_ProgressShow(page, 0);
    if (g_char.showRaw)
        Char_RawRefresh();
    if (kind == CHAR_JOB_BUILD)
        Char_DeletePart();
    if (exitCode == RS_JOB_CANCELLED) {
        Char_CancelDone(page, kind, seq);
        Char_UpdateButtons();
        return;
    }
    if (kind == CHAR_JOB_BUILD) {
        Char_BuildDone(page, exitCode);
        if (g_char.checkAfterBuild)
            Char_Check(page);
    } else if (kind == CHAR_JOB_META) {
        Char_MetaDone(page);
    } else {
        wchar_t *key = g_char.runKey, *modelKey = g_char.runModelKey;
        g_char.runKey = g_char.runModelKey = NULL;
        if (Char_CheckDone(page, exitCode, seq) && key && modelKey) {
            Char_CacheStore(key, modelKey, exitCode, seq, NULL);
        } else {
            Rs_Free(key);
            Rs_Free(modelKey);
            Char_TempDeleteKeep(seq, 1);    // the voice previews go with the next check
        }
    }
    Char_UpdateButtons();
}

// ---------------------------------------------------------------------------
// Fields
// ---------------------------------------------------------------------------

// A new model: the size range of the old one no longer counts.
static void Char_ModelChanged(HWND page)
{
    if (g_char.applying)
        return;
    Char_SizeForget();
    g_char.modelRead = 0;
    Char_ObjUpdate(page);
    Char_Changed(page);
}

// Sets the model and checks at once. Returns 1 if rldpack is running.
static int Char_SetModel(HWND page, const wchar_t *path)
{
    wchar_t clean[CHAR_VAL];
    wchar_t dir[CHAR_VAL];

    Char_CleanPath(clean, CHAR_VAL, path);
    g_char.applying = 1;
    Rs_SetText(g_char.model, clean);
    g_char.applying = 0;
    Char_SizeForget();
    g_char.modelRead = 0;
    Char_ObjUpdate(page);
    if (clean[0]) {
        Rs_PathDir(dir, CHAR_VAL, clean);
        Rs_ConfigSet(L"char.folder", dir);
    }
    return Char_CheckNow(page) > 0;
}

// Only the characters of the menu font, capitals, at most 17. rldpack checks
// the name all the same; this only keeps typing on the right path.
static void Char_NameFilter(void)
{
    wchar_t *text = Rs_GetText(g_char.name);
    wchar_t clean[CHAR_NAME_MAX + 1];
    int n = 0, dropped = 0;
    const wchar_t *p;

    for (p = text; *p; p++) {
        wchar_t c = *p;
        if (c >= L'a' && c <= L'z')
            c = (wchar_t)(c - L'a' + L'A');
        if (!Char_NameCharAllowed(c)) {
            dropped = 1;
            continue;
        }
        if (n >= CHAR_NAME_MAX) {
            dropped = 1;
            continue;
        }
        clean[n++] = c;
    }
    clean[n] = 0;
    if (wcscmp(clean, text) != 0) {
        int applying = g_char.applying;
        g_char.applying = 1;
        Rs_SetText(g_char.name, clean);
        SendMessageW(g_char.name, EM_SETSEL, (WPARAM)n, (LPARAM)n);
        g_char.applying = applying;
    }
    Rs_Free(text);
    if (dropped)
        Char_SetLabel(g_char.nameNote, L"Left out what the game cannot show - " CHAR_NAME_RULE_TEXT,
                      RS_COL_WARNING, &g_char.nameColor);
    else
        Char_SetLabel(g_char.nameNote, CHAR_NAME_RULE_TEXT, RS_COL_MUTED, &g_char.nameColor);
}

// While the slider is dragged every new size waits the 600 ms of typing; when
// it is let go (TB_ENDTRACK, also after the keys) a check still waiting starts
// at once.
static void Char_SizeScrolled(HWND page, int code)
{
    int before = g_char.sizeNow;
    Char_SizeSet(Char_SizePos());
    if (g_char.sizeNow != before)
        Char_Changed(page);
    if (code == TB_ENDTRACK && g_char.timer && !g_char.applying)
        Char_Check(page);
}

static void Char_SetPose(int pose)
{
    if (pose < 0 || pose >= CHAR_POSES)
        pose = 0;
    g_char.poseNow = pose;
    if (SendMessageW(g_char.pose, CB_GETCURSEL, 0, 0) != pose)
        SendMessageW(g_char.pose, CB_SETCURSEL, (WPARAM)pose, 0);
    RsView_SetPose(g_char.view, g_charPoseView[pose]);
}

// ---------------------------------------------------------------------------
// Buttons, dialogs, drag and drop
// ---------------------------------------------------------------------------

// Start folder of the dialogs: the field, else the folder of the last model.
static void Char_DialogStart(HWND field, wchar_t *out, int cap)
{
    Char_FieldPath(field, out, cap);
    if (!out[0])
        Rs_ConfigGet(L"char.folder", out, cap);
}

static void Char_BrowseModel(HWND page)
{
    wchar_t start[CHAR_VAL];
    wchar_t pick[CHAR_VAL];
    Char_DialogStart(g_char.model, start, CHAR_VAL);
    if (Rs_BrowseOpenFile(Rs_MainWindow(), L"Choose the character model",
                          L"3D models (*.ply;*.obj)\0*.ply;*.obj\0PLY models (*.ply)\0*.ply\0"
                          L"OBJ models (*.obj)\0*.obj\0All files\0*.*\0\0", start, pick, CHAR_VAL))
        Char_SetModel(page, pick);
}

static void Char_BrowseIcon(void)
{
    wchar_t start[CHAR_VAL];
    wchar_t pick[CHAR_VAL];
    Char_DialogStart(g_char.icon, start, CHAR_VAL);
    if (Rs_BrowseOpenFile(Rs_MainWindow(), L"Choose the icon picture",
                          L"PNG pictures (*.png)\0*.png\0All files\0*.*\0\0", start, pick, CHAR_VAL))
        Rs_SetText(g_char.icon, pick);      // EN_CHANGE schedules the check
}

static void Char_BrowseVoices(void)
{
    wchar_t start[CHAR_VAL];
    wchar_t pick[CHAR_VAL];
    Char_DialogStart(g_char.voices, start, CHAR_VAL);
    if (Rs_BrowseFolder(Rs_MainWindow(), L"Choose the voices folder", start, pick, CHAR_VAL))
        Rs_SetText(g_char.voices, pick);
}

static void Char_BrowseTextures(void)
{
    wchar_t start[CHAR_VAL];
    wchar_t pick[CHAR_VAL];
    Char_DialogStart(g_char.textures, start, CHAR_VAL);
    if (Rs_BrowseFolder(Rs_MainWindow(), L"Choose the folder of the textures", start, pick, CHAR_VAL))
        Rs_SetText(g_char.textures, pick);      // EN_CHANGE schedules the check
}

static void Char_BrowseOut(void)
{
    wchar_t start[CHAR_VAL];
    wchar_t pick[CHAR_VAL];
    Char_FieldPath(g_char.out, start, CHAR_VAL);
    if (!start[0]) {
        // The game's characters folder when the game is known (made if missing).
        wchar_t model[CHAR_VAL];
        Char_FieldPath(g_char.model, model, CHAR_VAL);
        Char_DefaultOut(model, start, CHAR_VAL);
        if (!start[0] && Char_GameCharDir(start, CHAR_VAL))
            Char_Append(start, CHAR_VAL, L"\\");
        if (start[0]) {
            wchar_t dir[CHAR_VAL];
            Rs_PathDir(dir, CHAR_VAL, start);
            if (!Rs_DirExists(dir))
                CreateDirectoryW(dir, NULL);
        }
    }
    if (!start[0])
        Rs_ConfigGet(L"char.out", start, CHAR_VAL);
    if (!start[0])
        Char_Copy(start, CHAR_VAL, g_char.checkedPath);
    if (Rs_BrowseSaveFile(Rs_MainWindow(), L"Save the character as",
                          L"Characters (*.rldchar)\0*.rldchar\0\0", L"rldchar", start, pick, CHAR_VAL))
        Rs_SetText(g_char.out, pick);
}

static void Char_ShowInFolder(void)
{
    wchar_t params[CHAR_VAL + 16];
    if (!g_char.built[0])
        return;
    swprintf(params, CHAR_VAL + 16, L"/select,\"%ls\"", g_char.built);
    ShellExecuteW(NULL, L"open", L"explorer.exe", params, NULL, SW_SHOWNORMAL);
}

// A file ending of another 3D format: dropped, it goes into the model field
// all the same, and the check says what it is and which formats are read.
static int Char_OtherModelFile(const wchar_t *path)
{
    static const wchar_t *const endings[] = { L".fbx", L".gltf", L".glb", L".stl", L".blend", L".3ds", L".dae" };
    int i;
    for (i = 0; i < (int)(sizeof(endings) / sizeof(endings[0])); i++)
        if (Char_EndsWith(path, endings[i]))
            return 1;
    return 0;
}

// Files dropped onto the page (or given to the automation verb "drop"): a
// .ply or .obj is the model, a .png the icon, a folder the voices folder. A
// file of another 3D format is the model only when no .ply or .obj came
// with it. A .png that comes with an .obj is not the icon: dropped with an
// OBJ it is its texture (rldpack reads it through the MTL), and an icon that
// changes unasked would go unnoticed. With a .ply it is the icon as always.
// Returns the number of paths taken.
static int Char_DropPaths(HWND page, const wchar_t *const *paths, int count)
{
    wchar_t model[CHAR_VAL], other[CHAR_VAL];
    int i, tab = -1, taken = 0, withModel = 0, withObj = 0;

    model[0] = 0;
    other[0] = 0;
    for (i = 0; i < count; i++) {
        if (Char_EndsWith(paths[i], L".obj"))
            withObj = 1;
        if (withObj || Char_EndsWith(paths[i], L".ply"))
            withModel = 1;
    }
    for (i = 0; i < count; i++) {
        const wchar_t *path = paths[i];
        if (Rs_DirExists(path)) {
            Rs_SetText(g_char.voices, path);
            if (tab < 0)
                tab = CHAR_TAB_VOICES;
        } else if (Char_EndsWith(path, L".png") && !withObj) {
            Rs_SetText(g_char.icon, path);
            if (tab < 0 || tab == CHAR_TAB_VOICES)
                tab = CHAR_TAB_LOOK;
        } else if (Char_EndsWith(path, L".ply") || Char_EndsWith(path, L".obj")) {
            Char_Copy(model, CHAR_VAL, path);
            tab = CHAR_TAB_MODEL;
        } else if (Char_OtherModelFile(path) && !withModel) {
            Char_Copy(other, CHAR_VAL, path);
            tab = CHAR_TAB_MODEL;
        } else {
            if (Rs_Automating())
                Rs_AutoLog(L"  drop: not taken: %ls", path);
            continue;
        }
        taken++;
    }
    if (!model[0])
        Char_Copy(model, CHAR_VAL, other);
    // The tab of what was dropped (the model first) comes to the front.
    if (tab >= 0 && tab != g_char.tab)
        Char_SelectTab(page, tab, 0);
    if (model[0])
        Char_SetModel(page, model);
    return taken;
}

static void Char_Drop(HWND page, HDROP drop)
{
    UINT count = DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0);
    wchar_t (*path)[CHAR_VAL];
    const wchar_t **list;
    UINT i;
    int n = 0;

    if (count > 64)
        count = 64;     // more than anyone drops at once
    path = Rs_Alloc((size_t)(count ? count : 1) * sizeof(*path));
    list = Rs_Alloc((size_t)(count ? count : 1) * sizeof(*list));
    for (i = 0; i < count; i++)
        if (DragQueryFileW(drop, i, path[n], CHAR_VAL)) {
            list[n] = path[n];
            n++;
        }
    DragFinish(drop);
    Char_DropPaths(page, list, n);
    Rs_Free((void *)list);
    Rs_Free(path);
}

// Enter (IDOK): on a button press it, otherwise check immediately.
static void Char_Enter(HWND page)
{
    HWND focus = GetFocus();
    if (focus == g_char.modelBrowse || focus == g_char.iconBrowse || focus == g_char.iconClear ||
        focus == g_char.voicesBrowse || focus == g_char.voicesClear || focus == g_char.voicePlay ||
        focus == g_char.texturesBrowse || focus == g_char.texturesClear || focus == g_char.cancel ||
        focus == g_char.outBrowse ||
        focus == g_char.check || focus == g_char.build || focus == g_char.rawToggle || focus == g_char.show ||
        focus == g_char.back || focus == g_char.next || (focus && GetDlgCtrlID(focus) >= CHAR_ID_TAB &&
                                                         GetDlgCtrlID(focus) < CHAR_ID_EXTRAS + CHAR_EXTRAS_COUNT)) {
        SendMessageW(focus, BM_CLICK, 0, 0);
    } else if (focus && GetParent(focus) == page) {
        Char_CheckNow(page);
    }
}

// ---------------------------------------------------------------------------
// Report (automation "report")
// ---------------------------------------------------------------------------

static void Char_Put(FILE *f, const wchar_t *fmt, ...)
{
    wchar_t line[4096];
    va_list ap;
    char *utf8;

    va_start(ap, fmt);
    vswprintf(line, 4095, fmt, ap);
    va_end(ap);
    line[4095] = 0;
    utf8 = Rs_ToUtf8(line);
    fputs(utf8, f);
    fputs("\n", f);
    Rs_Free(utf8);
}

static void Char_PutText(FILE *f, const wchar_t *what, HWND control)
{
    wchar_t *text = Rs_GetText(control);
    Char_Put(f, L"%ls: %ls", what, text);
    Rs_Free(text);
}

static void Char_PutLabel(FILE *f, const wchar_t *what, HWND control, COLORREF color)
{
    wchar_t *text = Rs_GetText(control);
    Char_Put(f, L"%ls: %ls [%ls]", what, text, Char_ColorName(color));
    Rs_Free(text);
}

static const wchar_t *Char_EnabledWord(HWND h)
{
    return IsWindowEnabled(h) ? L"enabled" : L"greyed out";
}

static int Char_WriteReport(const wchar_t *path)
{
    FILE *f = _wfopen(path, L"wb");
    wchar_t *text;
    int i;

    if (!f)
        return 0;
    Char_Put(f, L"Reload Studio - Character page");
    Char_PutText(f, L"model", g_char.model);
    Char_PutLabel(f, L"model info (shown)", g_char.modelInfo, g_char.infoColor);
    Char_Put(f, L"model info (full): %ls", g_char.infoFull);
    if (g_char.importOn)
        Char_PutLabel(f, L"model read", g_char.modelImport, g_char.importColor);
    else
        Char_Put(f, L"model read: (hidden)");
    Char_Put(f, L"model format (last check): %ls", g_char.importFormat[0] ? g_char.importFormat : L"(not reported)");
    Char_Put(f, L"model colours (last check): %ls", g_char.importColors[0] ? g_char.importColors : L"(not reported)");
    Char_Put(f, L"model files list: %ls, %d row(s); MTL %d, textures %d, groups %d in all",
             g_char.filesOn && g_char.filesLeast > 0 ? L"shown" : g_char.filesOn ? L"hidden - no room" : L"hidden",
             g_char.importRowCount, g_char.importTotal[CHAR_IMPORT_MTL], g_char.importTotal[CHAR_IMPORT_TEXTURE],
             g_char.importTotal[CHAR_IMPORT_GROUP]);
    for (i = 0; i < g_char.importRowCount; i++) {
        const struct CharImport *r = &g_char.importRow[i];
        wchar_t name[CHAR_VAL], state[48];
        Char_ImportTexts(r, name, CHAR_VAL, state, 48);
        Char_Put(f, L"model file: %ls | %ls | %ls | %ls%ls | %ls", g_charImportKinds[r->kind], name,
                 r->material[0] ? r->material : L"-", state, Char_ImportProblem(r) ? L" [amber]" : L"",
                 r->path[0] ? r->path : L"-");
    }
    {
        wchar_t dir[CHAR_VAL];
        Char_FieldPath(g_char.textures, dir, CHAR_VAL);
        Char_Put(f, L"textures folder: %ls (%ls) [%ls]", dir[0] ? dir : L"(none)",
                 !g_char.objOn ? L"not passed - no OBJ" : dir[0] ? L"passed as --textures" : L"not passed",
                 g_char.texturesOn ? L"shown" : L"hidden");
    }
    Char_PutText(f, L"name", g_char.name);
    Char_PutLabel(f, L"name note", g_char.nameNote, g_char.nameColor);
    text = Rs_GetText(g_char.cls);
    Char_Put(f, L"driving style: %ls (passed as %ls)", text, Char_ClassWord());
    Rs_Free(text);
    Char_Put(f, L"template: %ls (fixed)", CHAR_TEMPLATE);
    Char_Put(f, L"mask: %ls (passed as --mask %ls%ls)", g_charMasks[Char_MaskIndex()].name,
             g_charMasks[Char_MaskIndex()].word, Char_MaskIndex() == CHAR_TEMPLATE_MASK ? L", the template's" : L"");
    Char_Put(f, L"mask (last run): %ls", g_charJob.mask[0] ? g_charJob.mask : L"(not reported)");
    if (g_char.mapColorSet)
        Char_Put(f, L"minimap colour: %02X%02X%02X (passed as --map-color)", GetRValue(g_char.mapColor),
                 GetGValue(g_char.mapColor), GetBValue(g_char.mapColor));
    else
        Char_Put(f, L"minimap colour: like the template (not passed)");
    Char_Put(f, L"minimap colour (last run): %ls", g_charJob.mapColor[0] ? g_charJob.mapColor : L"(not reported)");
    Char_Put(f, L"size: %d %%", g_char.sizeNow);
    if (g_char.rangeNone)
        Char_Put(f, L"size range: none fits this model");
    else if (g_char.rangeKnown)
        Char_Put(f, L"size range: %d..%d %%", g_char.rangeLo, g_char.rangeHi);
    else
        Char_Put(f, L"size range: (not reported)");
    Char_PutLabel(f, L"size note", g_char.sizeNote, g_char.sizeColor);
    Char_PutText(f, L"size hint", g_char.sizeHint);
    Char_PutText(f, L"size label", g_char.sizeCrash);
    Char_PutLabel(f, L"fit line", g_char.sizeFit, g_char.fitColor);
    Char_Put(f, L"fit (last run): %ls", g_charJob.fit[0] ? g_charJob.fit : L"(not reported)");
    if (g_charJob.fitSeen)
        Char_Put(f, L"fitted: x%ls, %ls -> %ls long, %ls -> %ls tall (game units), by the %ls", g_charJob.fitFactor,
                 g_charJob.fitLength[0], g_charJob.fitLength[1], g_charJob.fitHeight[0], g_charJob.fitHeight[1],
                 g_charJob.fitBasis);
    else
        Char_Put(f, L"fitted: (not reported)");
    Char_Put(f, L"option Reduce to fit: %ls (%ls)", Char_IsChecked(g_char.reduce) ? L"on" : L"off",
             Char_IsChecked(g_char.reduce) ? L"rldpack's default" : L"passed as --reduce off");
    if (g_charJob.budgetSeen)
        Char_Put(f, L"budget: %ld triangles, limit %ld; %ld bytes of draw memory, limit %ld", g_charJob.budget[0],
                 g_charJob.budget[1], g_charJob.budget[2], g_charJob.budget[3]);
    else
        Char_Put(f, L"budget: (not reported)");
    Char_Put(f, L"reduce (last run): %ls", g_charJob.reduce[0] ? g_charJob.reduce : L"(not reported)");
    if (g_charJob.reducedSeen)
        Char_Put(f, L"reduced: %ld -> %ld triangles, draw memory %ld -> %ld bytes", g_charJob.reduced[0],
                 g_charJob.reduced[1], g_charJob.reduced[2], g_charJob.reduced[3]);
    else
        Char_Put(f, L"reduced: (not reported)");
    Char_Put(f, L"option Show kart wheels: %ls (%ls)", Char_IsChecked(g_char.wheels) ? L"on" : L"off",
             Char_IsChecked(g_char.wheels) ? L"rldpack's default" : L"passed as --wheels off");
    Char_Put(f, L"wheels (last run): %ls", g_charJob.wheels[0] ? g_charJob.wheels : L"(not reported)");
    Char_Put(f, L"option Repair the model: %ls (%ls)", Char_IsChecked(g_char.repair) ? L"on" : L"off",
             Char_IsChecked(g_char.repair) ? L"rldpack's default" : L"passed as --repair off");
    Char_Put(f, L"repair (last run): %ls", g_charJob.repair[0] ? g_charJob.repair : L"(not reported)");
    if (g_charJob.repairedSeen)
        Char_Put(f, L"repaired: %ld welded, %ld degenerate, %ld doubled, %ld turned outward, %ld holes closed with %ld "
                    L"triangles, %ld holes left, open edges %ld -> %ld, %ld cracks split",
                 g_charJob.repaired[0], g_charJob.repaired[1], g_charJob.repaired[2], g_charJob.repaired[3],
                 g_charJob.repaired[4], g_charJob.repaired[5], g_charJob.repaired[6], g_charJob.repaired[7],
                 g_charJob.repaired[8], g_charJob.repaired[9]);
    else
        Char_Put(f, L"repaired: (not reported)");
    Char_Put(f, L"option Draw open parts from both sides: %ls (%ls)", Char_IsChecked(g_char.openParts) ? L"on" : L"off",
             Char_IsChecked(g_char.openParts) ? L"rldpack's default" : L"passed as --open-parts one-sided");
    Char_Put(f, L"open parts (last run): %ls", g_charJob.openParts[0] ? g_charJob.openParts : L"(not reported)");
    if (g_charJob.twoSidedSeen)
        Char_Put(f, L"two-sided: %ld triangles", g_charJob.twoSided);
    else
        Char_Put(f, L"two-sided: (not reported)");
    Char_Put(f, L"option Closed hull (remesh): %ls (%ls)", Char_IsChecked(g_char.remesh) ? L"on" : L"off",
             !Char_RemeshAllowed() ? L"greyed out, not passed - needs Reduce to fit"
             : Char_RemeshOn()     ? L"passed as --remesh on"
                                   : L"rldpack's default");
    Char_Put(f, L"remesh (last run): %ls", g_charJob.remesh[0] ? g_charJob.remesh : L"(not reported)");
    if (g_charJob.remeshedSeen)
        Char_Put(f, L"remeshed: %ld -> %ld -> %ld triangles, open edges %ld -> %ld", g_charJob.remeshed[0],
                 g_charJob.remeshed[1], g_charJob.remeshed[2], g_charJob.remeshed[3], g_charJob.remeshed[4]);
    else
        Char_Put(f, L"remeshed: (not reported)");
    {
        // The note below the options, its lines joined by " | ".
        wchar_t *note = Rs_GetText(g_char.quality);
        wchar_t line[CHAR_VAL];
        const wchar_t *p;
        line[0] = 0;
        for (p = note; *p; p++) {
            wchar_t one[2] = { *p, 0 };
            if (*p == L'\r')
                continue;
            Char_Append(line, CHAR_VAL, *p == L'\n' ? L" | " : one);
        }
        Char_Put(f, L"quality note: %ls [%ls]", line[0] ? line : L"(none)", Char_ColorName(g_char.qualityColor));
        Rs_Free(note);
    }
    Char_PutText(f, L"icon", g_char.icon);
    Char_Put(f, L"icon framing: %ls (%ls) [%ls]", g_charIconFits[Char_IconFitIndex()].text,
             g_charIconFits[Char_IconFitIndex()].word, Char_EnabledWord(g_char.iconFit));
    Char_Put(f, L"icon make background transparent: %ls [%ls]", Char_IsChecked(g_char.iconCorners) ? L"on" : L"off",
             Char_EnabledWord(g_char.iconCorners));
    Char_Put(f, L"icon retail frame: %ls [%ls]", Char_IsChecked(g_char.iconFrame) ? L"on" : L"off",
             Char_EnabledWord(g_char.iconFrame));
    Char_Put(f, L"icon background cleared (last run): %ls", g_charJob.iconBackground[0] ? g_charJob.iconBackground
                                                                                     : L"(not reported)");
    Char_Put(f, L"icon place (last run): %ls", g_charJob.iconPlace[0] ? g_charJob.iconPlace : L"(not reported)");
    if (g_char.game.px)
        Char_Put(f, L"icon in the game: %dx%d, beside %ls", g_char.game.w, g_char.game.h,
                 g_char.gameRetail ? L"the template's portrait (game data)" : L"the frame lines (no game data)");
    else
        Char_Put(f, L"icon in the game: (none)");
    for (i = 0; i < CHAR_IMG_COUNT; i++) {
        const struct CharImage *img = &g_char.image[i];
        if (img->px)
            Char_Put(f, L"icon %ls: %dx%d", i == CHAR_IMG_ORIGINAL ? L"original" : L"converted", img->w, img->h);
        else
            Char_Put(f, L"icon %ls: (none)", i == CHAR_IMG_ORIGINAL ? L"original" : L"converted");
    }
    Char_PutText(f, L"voices", g_char.voices);
    Char_PutLabel(f, L"voices note", g_char.voicesNote, g_char.voicesColor);
    Char_Put(f, L"voices normalize: %ls (%ls)", Char_IsChecked(g_char.voiceNorm) ? L"on" : L"off",
             Char_IsChecked(g_char.voiceNorm) ? L"passed as --voice-normalize with a folder" : L"not passed");
    if (g_char.voiceKnown)
        Char_Put(f, L"voices (last check): %d clip(s), %d of %d events", g_char.voiceClips, g_char.voiceFilled,
                 CHAR_VOICE_EVENTS);
    else
        Char_Put(f, L"voices (last check): (not reported)");
    {
        wchar_t *events = Rs_GetText(g_char.voiceEvents);
        Char_Put(f, L"voice events: %ls", events);
        Rs_Free(events);
    }
    Char_Put(f, L"voice files: %d", g_char.voiceRowCount);
    for (i = 0; i < g_char.voiceRowCount; i++) {
        const struct CharVoice *v = &g_char.voiceRow[i];
        wchar_t length[32], event[96];
        Char_VoiceLengthText(v, length, 32);
        Char_VoiceEventText(v, event, 96);
        Char_Put(f, L"voice file: %ls | %ls | %ls | %lld bytes | %ld Hz | %d channel(s) | peak %d | %ls | preview %ls",
                 v->name, v->state, length, v->bytes, v->rate, v->channels, v->peak, event,
                 v->preview ? L"yes" : L"none");
    }
    Char_Put(f, L"voice chosen: %ls", g_char.voiceSel[0] ? g_char.voiceSel : L"(none)");
    {
        wchar_t *event = Rs_GetText(g_char.voiceEvent);
        Char_Put(f, L"voice event choice: %ls [%ls]", event[0] ? event : L"(none)", Char_EnabledWord(g_char.voiceEvent));
        Rs_Free(event);
    }
    Char_Put(f, L"button Play: %ls", Char_EnabledWord(g_char.voicePlay));
    {
        wchar_t line[CHAR_VAL];
        line[0] = 0;
        for (i = 0; i < g_char.voiceSetCount; i++) {
            wchar_t one[CHAR_VOICE_NAME + 32];
            swprintf(one, CHAR_VOICE_NAME + 32, L"%ls%ls=%ls", i ? L" " : L"", g_char.voiceSet[i].name,
                     g_charVoiceEvents[g_char.voiceSet[i].event].word);
            Char_Append(line, CHAR_VAL, one);
        }
        Char_Put(f, L"voice events chosen (--voice): %ls", line[0] ? line : L"(none)");
    }
    Char_PutText(f, L"voice names", g_char.voiceRule);
    Char_PutText(f, L"output", g_char.out);
    if (g_char.kartKnown)
        Char_Put(f, L"kart box: %d %d %d %d %d %d", g_char.kart[0], g_char.kart[1], g_char.kart[2],
                 g_char.kart[3], g_char.kart[4], g_char.kart[5]);
    else
        Char_Put(f, L"kart box: (not reported)");
    if (g_char.retailKnown)
        Char_Put(f, L"retail kart: %d %d %d %d %d %d", g_char.retail[0], g_char.retail[1], g_char.retail[2],
                 g_char.retail[3], g_char.retail[4], g_char.retail[5]);
    else
        Char_Put(f, L"retail kart: (not reported)");
    if (g_char.crashKnown)
        Char_Put(f, L"crash box: %.1f %.1f %.1f %.1f %.1f %.1f", g_char.crash[0] / 10.0, g_char.crash[1] / 10.0,
                 g_char.crash[2] / 10.0, g_char.crash[3] / 10.0, g_char.crash[4] / 10.0, g_char.crash[5] / 10.0);
    else
        Char_Put(f, L"crash box: (not reported)");
    Char_Put(f, L"preview dummy: shown, wheels %ls", Char_IsChecked(g_char.wheels) ? L"shown" : L"hidden");
    if (g_char.previewShown)
        Char_Put(f, L"preview: %d pose(s), triangles %lu/%lu/%lu", g_char.previewPoses, g_char.previewTris[0],
                 g_char.previewTris[1], g_char.previewTris[2]);
    else
        Char_Put(f, L"preview: (none)");
    Char_Put(f, L"pose: %ls", g_charPoseWords[g_char.poseNow]);
    g_char.yawNow = RsView_GetYaw(g_char.view);   // a new model may have reset the camera
    Char_Put(f, L"turn: %d degrees", g_char.yawNow);
    {
        // The camera and the display toggles of the preview (Char_AutoView).
        int yaw, pitch, zoom, panX, panY, preset, light, crash, shadow, exhaust, native, drawn, k;
        RsView_GetCamera(g_char.view, &yaw, &pitch, &zoom, &panX, &panY);
        preset = RsView_GetPreset(g_char.view);
        Char_Put(f, L"camera: %d %d %d %d %d %ls", yaw, pitch, zoom, panX, panY,
                 preset >= 0 && preset < RS_VIEW_PRESET_COUNT ? g_charPresetWords[preset] : L"none");
        drawn = RsView_GetToggles(g_char.view, &light, &crash, &shadow, &exhaust, &native);
        Char_Put(f, L"view-look: %ls %ls %ls %ls %ls", light ? L"light" : L"dark", crash ? L"on" : L"off", shadow ? L"on" : L"off",
                 exhaust ? L"on" : L"off", drawn ? L"native" : L"classic");
        Char_Put(f, L"preview native model: %ls%ls", RsView_HasNative(g_char.view) ? L"in the preview" : L"none",
                 RsView_HasNative(g_char.view) && !native ? L" (Classic chosen)" : L"");
        Char_Put(f, L"preview textures missing: %d%ls%ls", g_char.previewMissing, g_char.previewMissing ? L" " : L"",
                 g_char.previewMissingNames);
        Char_Put(f, L"preview note sentences: %d", g_char.viewPartCount);
        for (k = 0; k < g_char.viewPartCount; k++)
            Char_Put(f, L"preview note %d: %ls", k + 1, g_char.viewPart[k]);
    }
    Char_PutLabel(f, L"preview note", g_char.viewNote, g_char.viewColor);
    Char_PutLabel(f, L"headline", g_char.headline, g_char.headColor);
    Char_Put(f, L"button Check: %ls", Char_EnabledWord(g_char.check));
    Char_Put(f, L"button Build character: %ls", Char_EnabledWord(g_char.build));
    Char_Put(f, L"button Show in folder: %ls", Char_IsShown(g_char.show) ? L"shown" : L"hidden");
    Char_Put(f, L"button Reduce to fit: %ls", g_char.reduceFitOn ? L"shown" : L"hidden");
    Char_Put(f, L"button Cancel and progress: %ls%ls%ls", g_char.progressOn ? L"shown, " : L"hidden",
             g_char.progressOn ? g_char.progressText : L"", g_char.progressOn && !IsWindowEnabled(g_char.cancel) ? L" (cancelling)" : L"");
    if (g_char.built[0])
        Char_Put(f, L"last built: %ls, %lld bytes, SHA-256 %ls", g_char.built, g_char.builtBytes, g_char.builtSha);
    else
        Char_Put(f, L"last built: (none)");
    Char_Put(f, L"view: %ls", g_char.showRaw ? L"rldpack output" : L"messages");
    Char_Put(f, L"tab: %d %ls", g_char.tab + 1, g_charTabTexts[g_char.tab]);
    for (i = 0; i < CHAR_TABS; i++)
        Char_Put(f, L"tab %d %ls: %ls", i + 1, g_charTabTexts[i], g_charStepWords[g_char.step[i]]);
    Char_Put(f, L"extras card: %ls", g_charExtrasTexts[g_char.extrasCard]);
    Char_Put(f, L"look card: %ls", g_charLookTexts[g_char.lookCard]);
    {
        wchar_t args[160];
        int pts[3], i;
        Char_Put(f, L"look: %ls (%ls)", L"enabled (preview feature)",
                 IsWindowEnabled(g_char.shadow) ? L"enabled" : L"greyed out");
        Char_Put(f, L"shadow: %ls (%ls)", g_charShadowWords[Char_ListIndex(g_char.shadow, 3)],
                 g_char.shadowSet ? L"chosen" : L"follows the wheels");
        Char_Put(f, L"exhaust: %ls (%ls)", g_charExhaustWords[Char_ListIndex(g_char.exhaust, 3)],
                 g_char.exhaustSet ? L"chosen" : L"follows the wheels");
        for (i = 0; i < 2; i++) {
            if (Char_LookPointValue(i, pts))
                Char_Put(f, L"exhaust point %d: %.2f %.2f %.2f (%ls)", i + 1, (double)pts[0] / 16.0, (double)pts[1] / 16.0,
                         (double)pts[2] / 16.0, IsWindowEnabled(g_char.point[i][0]) ? L"enabled" : L"greyed out");
            else
                Char_Put(f, L"exhaust point %d: %ls", i + 1, Char_LookPointEmpty(i) ? L"empty" : L"not three numbers");
        }
        if (g_char.lookQuadKnown)
            Char_Put(f, L"shadow quad (auto, last check): x %.2f..%.2f, z %.2f..%.2f", (double)g_char.lookQuad[0] / 16.0,
                     (double)g_char.lookQuad[1] / 16.0, (double)g_char.lookQuad[2] / 16.0, (double)g_char.lookQuad[3] / 16.0);
        else
            Char_Put(f, L"shadow quad (auto, last check): (not reported)");
        Char_LookExhaustArg(args, 160);
        Char_Put(f, L"exhaust argument (custom): %ls", Char_ListIndex(g_char.exhaust, 3) == 1 ? args : L"(not passed)");
    }
    Char_Put(f, L"up: %ls (%ls)", g_charUpTexts[Char_ListIndex(g_char.up, CHAR_UP_COUNT)],
             Char_ListIndex(g_char.up, CHAR_UP_COUNT) ? L"passed as --up" : L"rldpack's default");
    Char_Put(f, L"forward: %ls (%ls)", g_charForwardTexts[Char_ListIndex(g_char.forward, CHAR_FORWARD_COUNT)],
             Char_ListIndex(g_char.forward, CHAR_FORWARD_COUNT) ? L"passed as --forward" : L"rldpack's default");
    Char_Put(f, L"colors: %ls (%ls)", g_charColorsTexts[Char_ListIndex(g_char.colors, CHAR_COLORS_COUNT)],
             Char_ListIndex(g_char.colors, CHAR_COLORS_COUNT) ? L"passed as --colors" : L"rldpack's default");
    Char_Put(f, L"vertex colors: %ls (%ls) [%ls]", g_charVColorTexts[Char_VColorIndex()],
             !g_char.objOn ? L"not passed - no OBJ" : Char_VColorIndex() ? L"passed as --vertex-colors" : L"rldpack's default",
             g_char.objOn ? L"shown" : L"hidden");
    {
        wchar_t *hint = Rs_GetText(g_char.nativeHint);
        wchar_t *note = Rs_GetText(g_char.nativeHelp);
        Char_Put(f, L"native model: %ls, %ls, %ls (%ls)", L"enabled (preview feature)",
                 IsWindowEnabled(g_char.native) ? L"enabled" : L"greyed out", Char_IsChecked(g_char.native) ? L"on" : L"off",
                 Char_NativePassed() ? L"passed as --native-model on" : L"not passed");
        Char_Put(f, L"native model hint: %ls", hint ? hint : L"");
        Char_Put(f, L"native model note: %ls", note ? note : L"");
        Rs_Free(hint);
        Rs_Free(note);
    }
    // The user mode only (the report without it stays as it was).
    if (Rs_NativeForUsers()) {
        wchar_t *line = Rs_GetText(g_char.nativeLine);
        Char_Put(f, L"user mode: on - Repair the model, Draw open parts, Closed hull, Reduce to fit, Colors and the native tick box "
                    L"hidden (rldpack's defaults, nothing passed)");
        Char_Put(f, L"hidden: repair %ls, open parts %ls, remesh %ls, reduce %ls, reduce button %ls, colors %ls, native box %ls",
                 Char_IsShown(g_char.repair) ? L"shown" : L"hidden", Char_IsShown(g_char.openParts) ? L"shown" : L"hidden",
                 Char_IsShown(g_char.remesh) ? L"shown" : L"hidden", Char_IsShown(g_char.reduce) ? L"shown" : L"hidden",
                 Char_IsShown(g_char.reduceFit) ? L"shown" : L"hidden", Char_IsShown(g_char.colors) ? L"shown" : L"hidden",
                 Char_IsShown(g_char.native) ? L"shown" : L"hidden");
        Char_Put(f, L"option Include classic fallback model: %ls, %ls, locked - coming soon (always built)",
                 Char_IsChecked(g_char.fallback) ? L"on" : L"off", IsWindowEnabled(g_char.fallback) ? L"enabled" : L"greyed out");
        Char_Put(f, L"native line: %ls", line ? line : L"");
        Rs_Free(line);
    }
    {
        // The tab of every message, in the order of the list ("-" = none).
        wchar_t line[CHAR_VAL];
        line[0] = 0;
        for (i = 0; i < g_char.msgTabCount; i++) {
            wchar_t one[32];
            swprintf(one, 32, L"%ls%ls%ls%ls", i ? L" " : L"", g_char.msgTab[i] >= 0 ? g_charTabWords[g_char.msgTab[i]] : L"-",
                     g_char.msgCard[i] >= 0 ? L"/" : L"", g_char.msgCard[i] >= 0 ? g_charExtrasWords[g_char.msgCard[i]] : L"");
            Char_Append(line, CHAR_VAL, one);
        }
        Char_Put(f, L"message tabs: %ls", line[0] ? line : L"(none)");
    }
    Char_Put(f, L"messages: %d", Rs_MsgListCount(g_char.msgs));
    Rs_MsgListWrite(g_char.msgs, f);
    {
        int kept = 0;
        for (i = 0; i < CHAR_CACHE; i++)
            kept += g_char.cache[i].key != NULL;
        Char_Put(f, L"results kept: %d of %d", kept, CHAR_CACHE);
    }
    Char_Put(f, L"rldpack output of the last run: %d line(s)", g_char.rawLines);
    if (g_char.rawText && g_char.rawLen) {
        char *utf8 = Rs_ToUtf8(g_char.rawText);
        const char *p;
        for (p = utf8; *p; p++)
            if (*p != '\r')
                fputc(*p, f);
        fputc('\n', f);
        Rs_Free(utf8);
    }
    CharWheels_Report(f);
    CharAnim_Report(f);
    fclose(f);
    return 1;
}

// ---------------------------------------------------------------------------
// Automation: the same paths as typing and clicking. No verb starts the game.
// ---------------------------------------------------------------------------

// allowNone: "none" empties the field (icon, voices, out).
static int Char_AutoText(HWND edit, const wchar_t *verb, const wchar_t *arg, int allowNone)
{
    const wchar_t *value = (allowNone && _wcsicmp(arg, L"none") == 0) ? L"" : arg;
    wchar_t *now;
    Rs_SetText(edit, value);    // EN_CHANGE schedules the check as when typing
    now = Rs_GetText(edit);
    Rs_AutoLog(L"  %ls: %ls", verb, now[0] ? now : L"(none)");
    Rs_Free(now);
    return RS_AUTO_WAIT;
}

static int Char_AutoClass(HWND page, const wchar_t *arg)
{
    int i;
    for (i = 0; i < CHAR_CLASS_COUNT; i++) {
        if (_wcsicmp(arg, g_charClasses[i].word) == 0) {
            SendMessageW(g_char.cls, CB_SETCURSEL, (WPARAM)i, 0);
            SendMessageW(page, WM_COMMAND, MAKEWPARAM(CHAR_ID_CLASS, CBN_SELCHANGE), (LPARAM)g_char.cls);
            Rs_AutoLog(L"  class: %ls", g_charClasses[i].text);
            return RS_AUTO_WAIT;
        }
    }
    Rs_AutoLog(L"  class: '%ls' is not a driving style - use balanced, acceleration, speed or turning", arg);
    return RS_AUTO_FAIL;
}

// "mapcolor template|RRGGBB" (a leading # is allowed): as the dialog or the
// button "Like the template", then the check at once.
static int Char_AutoMapColor(HWND page, const wchar_t *arg)
{
    const wchar_t *hex = arg[0] == L'#' ? arg + 1 : arg;
    wchar_t *end;
    unsigned long v;
    int r;

    if (_wcsicmp(arg, L"template") == 0) {
        Char_MapColorSet(page, 0, CHAR_TEMPLATE_MAP_COLOR);
        Rs_AutoLog(L"  mapcolor: like the template");
    } else {
        int digits = 0;
        while (digits < 6 && iswxdigit(hex[digits]))
            digits++;
        v = wcstoul(hex, &end, 16);
        if (digits != 6 || hex[6] || *end) {
            Rs_AutoLog(L"  mapcolor: '%ls' is not a colour - use template or RRGGBB", arg);
            return RS_AUTO_FAIL;
        }
        Char_MapColorSet(page, 1, RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF));
        Rs_AutoLog(L"  mapcolor: %06lX", v);
    }
    UpdateWindow(g_char.mapSwatch);     // painted before a following "shot"
    r = Char_Check(page);
    if (r > 0)
        return RS_AUTO_WAIT;
    if (r < 0)
        Rs_AutoLog(L"  mapcolor: %ls", g_char.jobId ? L"checked after the running build" : L"not checked - no model is chosen");
    return RS_AUTO_DONE;
}

static int Char_AutoMask(HWND page, const wchar_t *arg)
{
    int i;
    for (i = 0; i < CHAR_MASK_COUNT; i++) {
        if (_wcsicmp(arg, g_charMasks[i].word) == 0) {
            SendMessageW(g_char.mask, CB_SETCURSEL, (WPARAM)i, 0);
            SendMessageW(page, WM_COMMAND, MAKEWPARAM(CHAR_ID_MASK, CBN_SELCHANGE), (LPARAM)g_char.mask);
            Rs_AutoLog(L"  mask: %ls", g_charMasks[i].name);
            return RS_AUTO_WAIT;
        }
    }
    Rs_AutoLog(L"  mask: '%ls' is not a mask - use aku or uka", arg);
    return RS_AUTO_FAIL;
}

static int Char_AutoSize(HWND page, const wchar_t *arg)
{
    wchar_t *end;
    long v = wcstol(arg, &end, 10);
    int r;
    while (*end == L' ' || *end == L'%')
        end++;
    if (end == arg || *end || v < CHAR_SIZE_MIN || v > CHAR_SIZE_MAX) {
        Rs_AutoLog(L"  size: '%ls' is not a size - use a whole number from %d to %d", arg, CHAR_SIZE_MIN,
                   CHAR_SIZE_MAX);
        return RS_AUTO_FAIL;
    }
    if (g_char.rangeKnown && (v < g_char.rangeLo || v > g_char.rangeHi)) {
        const wchar_t *why = g_char.sizeWhy[v > g_char.rangeHi ? 1 : 0];
        Rs_AutoLog(L"  size: %ld %% is locked - this model allows %d..%d %%%ls%ls", v, g_char.rangeLo,
                   g_char.rangeHi, why[0] ? L": " : L"", why);
        return RS_AUTO_FAIL;
    }
    // Slider and value as when dragging, then the check at once (not after
    // 600 ms): a following "shot" shows the preview of this size.
    Char_SizeSet((int)v);
    Rs_AutoLog(L"  size: %d %%", g_char.sizeNow);
    r = Char_Check(page);
    if (r > 0)
        return RS_AUTO_WAIT;
    if (r < 0)
        Rs_AutoLog(L"  size: %ls", g_char.jobId ? L"checked after the running build" : L"not checked - no model is chosen");
    return RS_AUTO_DONE;
}

// "repair on|off", "open-parts on|off" (on = from both sides), "remesh on|off",
// "reduce on|off", "wheels on|off": the check box as when clicked, then the
// check at once (as for "size"), so that a following "shot" shows its result.
static int Char_AutoOption(HWND page, HWND box, const wchar_t *verb, const wchar_t *arg)
{
    int on, r;
    if (_wcsicmp(arg, L"on") == 0)
        on = 1;
    else if (_wcsicmp(arg, L"off") == 0)
        on = 0;
    else {
        Rs_AutoLog(L"  %ls: say on or off", verb);
        return RS_AUTO_FAIL;
    }
    Char_SetChecked(box, on);
    if (box == g_char.wheels) {
        RsView_SetWheels(g_char.view, on);
        Char_NativeUpdate();
        Char_LookDefaults();
        Char_LookEnable();
        Char_LookPreview();
    }
    Char_UpdateOptions();
    if (box == g_char.reduce)
        Char_ApplyQuality();
    Rs_AutoLog(L"  %ls: %ls", verb, on ? L"on" : L"off");
    if (box == g_char.remesh && on && !Char_RemeshAllowed())
        Rs_AutoLog(L"  %ls: greyed out - needs Reduce to fit, not passed", verb);
    r = Char_Check(page);
    if (r > 0)
        return RS_AUTO_WAIT;
    if (r < 0)
        Rs_AutoLog(L"  %ls: %ls", verb,
                   g_char.jobId ? L"checked after the running build" : L"not checked - no model is chosen");
    return RS_AUTO_DONE;
}

// "icon-fit fit|fill|none": the framing of the icon as when chosen, then the
// check at once.
static int Char_AutoIconFit(HWND page, const wchar_t *arg)
{
    int i, r;
    for (i = 0; i < CHAR_ICON_FIT_COUNT; i++) {
        if (_wcsicmp(arg, g_charIconFits[i].word) == 0) {
            SendMessageW(g_char.iconFit, CB_SETCURSEL, (WPARAM)i, 0);
            Rs_AutoLog(L"  icon-fit: %ls", g_charIconFits[i].text);
            r = Char_Check(page);
            if (r > 0)
                return RS_AUTO_WAIT;
            if (r < 0)
                Rs_AutoLog(L"  icon-fit: %ls",
                           g_char.jobId ? L"checked after the running build" : L"not checked - no model is chosen");
            return RS_AUTO_DONE;
        }
    }
    Rs_AutoLog(L"  icon-fit: '%ls' is not a framing - use fit, fill or none", arg);
    return RS_AUTO_FAIL;
}

static int Char_AutoPose(const wchar_t *arg)
{
    int i;
    for (i = 0; i < CHAR_POSES; i++) {
        if (_wcsicmp(arg, g_charPoseWords[i]) == 0) {
            Char_SetPose(i);
            UpdateWindow(g_char.view);      // painted before a following "shot"
            Rs_AutoLog(L"  pose: %ls", g_charPoseTexts[i]);
            return RS_AUTO_DONE;
        }
    }
    Rs_AutoLog(L"  pose: say neutral, left or right");
    return RS_AUTO_FAIL;
}

// "tab 1..5" or "tab model|driver|look|voices|extras": as a click on the head.
static int Char_AutoTab(HWND page, const wchar_t *arg)
{
    int t;
    for (t = 0; t < CHAR_TABS; t++) {
        wchar_t num[4];
        swprintf(num, 4, L"%d", t + 1);
        if (wcscmp(arg, num) == 0 || _wcsicmp(arg, g_charTabWords[t]) == 0) {
            Char_SelectTab(page, t, 0);
            UpdateWindow(page);         // painted before a following "shot"
            Rs_AutoLog(L"  tab: %d %ls (%ls)", t + 1, g_charTabTexts[t], g_charStepWords[g_char.step[t]]);
            return RS_AUTO_DONE;
        }
    }
    Rs_AutoLog(L"  tab: say 1 to 5, or model, driver, look, voices or extras");
    return RS_AUTO_FAIL;
}

// "extras import|wheels|animations": the card the tab Extras shows (it opens the tab).
static int Char_AutoExtras(HWND page, const wchar_t *arg)
{
    int c;
    for (c = 0; c < CHAR_EXTRAS_COUNT; c++) {
        if (_wcsicmp(arg, g_charExtrasWords[c]) == 0) {
            g_char.extrasCard = c;
            Char_SelectTab(page, CHAR_TAB_EXTRAS, 0);
            UpdateWindow(page);
            Rs_AutoLog(L"  extras: %ls", g_charExtrasTexts[c]);
            return RS_AUTO_DONE;
        }
    }
    Rs_AutoLog(L"  extras: say import, wheels or animations");
    return RS_AUTO_FAIL;
}

// "problem <n>": as a click on the n-th message of the list (1 = the first).
static int Char_AutoProblem(HWND page, const wchar_t *arg)
{
    wchar_t *end;
    long n = wcstol(arg, &end, 10);
    if (end == arg || *end || n < 1 || n > g_char.msgTabCount) {
        Rs_AutoLog(L"  problem: '%ls' - the list has %d message(s)", arg, g_char.msgTabCount);
        return RS_AUTO_FAIL;
    }
    if (g_char.msgTab[n - 1] < 0) {
        Rs_AutoLog(L"  problem: message %ld belongs to no tab", n);
        return RS_AUTO_DONE;
    }
    Char_MsgOpen(page, (int)n - 1);
    UpdateWindow(page);
    Rs_AutoLog(L"  problem: message %ld -> tab %d %ls%ls%ls", n, g_char.tab + 1, g_charTabTexts[g_char.tab],
               g_char.tab == CHAR_TAB_EXTRAS ? L", card " : L"", g_char.tab == CHAR_TAB_EXTRAS ? g_charExtrasTexts[g_char.extrasCard] : L"");
    return RS_AUTO_DONE;
}

// "up y|z", "forward z|-z": a list of the card Import as when chosen, then
// the check at once.
static int Char_AutoList(HWND page, HWND combo, const wchar_t *verb, const wchar_t *arg,
                         const wchar_t *const *words, const wchar_t *const *texts, int count)
{
    int i, r;
    for (i = 0; i < count; i++) {
        if (_wcsicmp(arg, words[i]) == 0) {
            SendMessageW(combo, CB_SETCURSEL, (WPARAM)i, 0);
            Rs_AutoLog(L"  %ls: %ls", verb, texts[i]);
            r = Char_Check(page);
            if (r > 0)
                return RS_AUTO_WAIT;
            if (r < 0)
                Rs_AutoLog(L"  %ls: %ls", verb,
                           g_char.jobId ? L"checked after the running build" : L"not checked - no model is chosen");
            return RS_AUTO_DONE;
        }
    }
    Rs_AutoLog(L"  %ls: '%ls' - say %ls or %ls", verb, arg, words[0], words[1]);
    return RS_AUTO_FAIL;
}

// "vertex-colors auto|modulate|color": the choice of the card Import as when
// chosen, then the check at once (an OBJ only: for another model it is not
// shown and not passed).
static int Char_AutoVColors(HWND page, const wchar_t *arg)
{
    int i, r;
    for (i = 0; i < CHAR_VCOLORS_COUNT; i++) {
        if (_wcsicmp(arg, g_charVColorWords[i]) == 0) {
            SendMessageW(g_char.vcolors, CB_SETCURSEL, (WPARAM)i, 0);
            Rs_AutoLog(L"  vertex-colors: %ls%ls", g_charVColorTexts[i], g_char.objOn ? L"" : L" (not shown and not passed - no OBJ)");
            r = Char_Check(page);
            if (r > 0)
                return RS_AUTO_WAIT;
            if (r < 0)
                Rs_AutoLog(L"  vertex-colors: %ls",
                           g_char.jobId ? L"checked after the running build" : L"not checked - no model is chosen");
            return RS_AUTO_DONE;
        }
    }
    Rs_AutoLog(L"  vertex-colors: '%ls' - say auto, modulate or color", arg);
    return RS_AUTO_FAIL;
}

// "voice <file>=<event|none>": as choosing the file in the list and the event
// below it, then the check at once. The file must be in the list of the last
// check; the event is a word of g_charVoiceEvents (boost ... short-hit, none).
static int Char_AutoVoice(HWND page, const wchar_t *arg)
{
    wchar_t name[CHAR_VOICE_NAME];
    const wchar_t *eq = wcsrchr(arg, L'=');
    int row, e, r;

    if (!eq || eq == arg || (size_t)(eq - arg) >= CHAR_VOICE_NAME) {
        Rs_AutoLog(L"  voice: say <file>=<event>, the event one of boost, hit, spin, bigair, drop, shield, passing, "
                   L"fire, short-yes, short-hit or none");
        return RS_AUTO_FAIL;
    }
    memcpy(name, arg, (size_t)(eq - arg) * sizeof(wchar_t));
    name[eq - arg] = 0;
    e = Char_VoiceEventIndex(eq + 1);
    if (e < 0 || wcscmp(eq + 1, L"-") == 0) {
        Rs_AutoLog(L"  voice: '%ls' is not an event - use boost, hit, spin, bigair, drop, shield, passing, fire, "
                   L"short-yes, short-hit or none", eq + 1);
        return RS_AUTO_FAIL;
    }
    row = Char_VoiceFind(name);
    if (row < 0) {
        Rs_AutoLog(L"  voice: %ls is not in the list (%d file(s) of the last check)", name, g_char.voiceRowCount);
        return RS_AUTO_FAIL;
    }
    Char_VoiceSelect(row);
    if (!IsWindowEnabled(g_char.voiceEvent)) {
        Rs_AutoLog(L"  voice: %ls is no voice file - the choice Event is greyed out", g_char.voiceRow[row].name);
        return RS_AUTO_FAIL;
    }
    // As the mouse: the choice, then its notification to the page.
    SendMessageW(g_char.voiceEvent, CB_SETCURSEL, (WPARAM)e, 0);
    r = Char_VoiceAssign(row, e);
    if (r < 0) {
        Rs_AutoLog(L"  voice: %ls cannot take %ls (a file that is no voice, or more than %d files given an event)",
                   g_char.voiceRow[row].name, g_charVoiceEvents[e].text, CHAR_VOICE_SET_MAX);
        Char_VoiceSelShow();
        return RS_AUTO_FAIL;
    }
    if (r == 0) {
        Rs_AutoLog(L"  voice: %ls is %ls already", g_char.voiceRow[row].name, g_charVoiceEvents[e].text);
        return RS_AUTO_DONE;
    }
    Rs_AutoLog(L"  voice: %ls -> %ls%ls", g_char.voiceRow[row].name, g_charVoiceEvents[e].text,
               Char_VoiceSetFind(g_char.voiceRow[row].name) >= 0 ? L"" : L" (by its name, no --voice)");
    r = Char_Check(page);
    if (r > 0)
        return RS_AUTO_WAIT;
    if (r < 0)
        Rs_AutoLog(L"  voice: %ls", g_char.jobId ? L"checked after the running build" : L"not checked - no model is chosen");
    return RS_AUTO_DONE;
}

// "voiceplay <file>": as choosing the file and pressing Play - in automation
// the preview is only read and described in the log, never played.
static int Char_AutoVoicePlay(const wchar_t *arg)
{
    int row = Char_VoiceFind(arg);
    if (row < 0) {
        Rs_AutoLog(L"  voiceplay: %ls is not in the list (%d file(s) of the last check)", arg, g_char.voiceRowCount);
        return RS_AUTO_FAIL;
    }
    Char_VoiceSelect(row);
    UpdateWindow(GetParent(g_char.voiceList));     // painted before a following "shot"
    return Char_VoicePlay(row) ? RS_AUTO_DONE : RS_AUTO_FAIL;
}

// "drop <path>[|<path>...]": as dropping these files together onto the page
// (Char_DropPaths). | cannot be part of a Windows path.
static int Char_AutoDrop(HWND page, const wchar_t *arg)
{
    wchar_t buf[4 * CHAR_VAL];
    const wchar_t *list[16];
    wchar_t *p = buf;
    int n = 0, taken, i;

    Char_Copy(buf, 4 * CHAR_VAL, arg);
    while (*p && n < 16) {
        wchar_t *bar = wcschr(p, L'|');
        if (bar)
            *bar = 0;
        if (*p)
            list[n++] = p;
        if (!bar)
            break;
        p = bar + 1;
    }
    if (!n) {
        Rs_AutoLog(L"  drop: no path");
        return RS_AUTO_FAIL;
    }
    for (i = 0; i < n; i++)
        Rs_AutoLog(L"  drop: %ls", list[i]);
    taken = Char_DropPaths(page, list, n);
    Rs_AutoLog(L"  drop: %d of %d taken, tab %ls", taken, n, g_charTabWords[g_char.tab]);
    if (!taken)
        return RS_AUTO_DONE;
    return RS_AUTO_WAIT;    // a model checks at once, the others after the delay
}

static int Char_AutoViewOnOff(const wchar_t *verb, const wchar_t *word, int *on)
{
    if (_wcsicmp(word, L"on") == 0)
        *on = 1;
    else if (_wcsicmp(word, L"off") == 0)
        *on = 0;
    else {
        Rs_AutoLog(L"  %ls: say on or off", verb);
        return 0;
    }
    return 1;
}

// After a step of the view: the turn and the note follow, painted before a
// following "shot".
static void Char_AutoViewDone(void)
{
    g_char.yawNow = RsView_GetYaw(g_char.view);
    Char_ViewNoteUpdate();
    UpdateWindow(g_char.view);
}

// The verbs of the preview's camera and display toggles (the mouse, the bar
// and the View menu of the view do the same; nothing of them is built or
// stored). RS_AUTO_UNKNOWN when verb is none of them.
static int Char_AutoView(const wchar_t *verb, const wchar_t *arg)
{
    int yaw, pitch, zoom, panX, panY, on, i;
    if (wcsncmp(verb, L"view-", 5) != 0)
        return RS_AUTO_UNKNOWN;
    if (wcscmp(verb, L"view-camera") == 0) {
        // <yaw> <pitch> <zoom %> [<pan x> <pan y>]
        int n;
        wchar_t rest[8];
        panX = panY = 0;
        n = swscanf(arg, L"%d %d %d %d %d %7ls", &yaw, &pitch, &zoom, &panX, &panY, rest);
        if (n != 3 && n != 5) {
            Rs_AutoLog(L"  view-camera: say <yaw> <pitch> <zoom %%> [<pan x> <pan y>] in whole numbers");
            return RS_AUTO_FAIL;
        }
        RsView_SetCamera(g_char.view, yaw, pitch, zoom, panX, panY);
        RsView_GetCamera(g_char.view, &yaw, &pitch, &zoom, &panX, &panY);
        Char_AutoViewDone();
        Rs_AutoLog(L"  view-camera: yaw %d, pitch %d, zoom %d %%, pan %d %d", yaw, pitch, zoom, panX, panY);
        return RS_AUTO_DONE;
    }
    if (wcscmp(verb, L"view-preset") == 0) {
        for (i = 0; i < RS_VIEW_PRESET_COUNT && _wcsicmp(arg, g_charPresetWords[i]) != 0; i++)
            ;
        if (i >= RS_VIEW_PRESET_COUNT) {
            Rs_AutoLog(L"  view-preset: say front, side, back, top, 34 or race");
            return RS_AUTO_FAIL;
        }
        RsView_SetPreset(g_char.view, i);
        Char_AutoViewDone();
        Rs_AutoLog(L"  view-preset: %ls", g_charPresetWords[i]);
        return RS_AUTO_DONE;
    }
    if (wcscmp(verb, L"view-reset") == 0) {
        RsView_ResetCamera(g_char.view);
        Char_AutoViewDone();
        Rs_AutoLog(L"  view-reset: the start view");
        return RS_AUTO_DONE;
    }
    if (wcscmp(verb, L"view-bg") == 0) {
        const int light = _wcsicmp(arg, L"light") == 0;
        if (!light && _wcsicmp(arg, L"dark") != 0) {
            Rs_AutoLog(L"  view-bg: say dark or light");
            return RS_AUTO_FAIL;
        }
        RsView_SetBackground(g_char.view, light);
        Char_AutoViewDone();
        Rs_AutoLog(L"  view-bg: %ls", light ? L"light" : L"dark");
        return RS_AUTO_DONE;
    }
    if (wcscmp(verb, L"view-crash") == 0) {
        if (!Char_AutoViewOnOff(verb, arg, &on))
            return RS_AUTO_FAIL;
        RsView_SetCrashRef(g_char.view, on);
        Char_AutoViewDone();
        Rs_AutoLog(L"  view-crash: %ls", on ? L"on" : L"off");
        return RS_AUTO_DONE;
    }
    if (wcscmp(verb, L"view-overlay") == 0) {
        // <shadow on|off> <exhaust on|off>: what the preview draws, not what is built
        wchar_t a[8], b[8];
        int shadow, exhaust;
        if (swscanf(arg, L"%7ls %7ls", a, b) != 2 || !Char_AutoViewOnOff(verb, a, &shadow) ||
            !Char_AutoViewOnOff(verb, b, &exhaust)) {
            Rs_AutoLog(L"  view-overlay: say <shadow on|off> <exhaust on|off>");
            return RS_AUTO_FAIL;
        }
        RsView_SetOverlays(g_char.view, shadow, exhaust);
        Char_AutoViewDone();
        Rs_AutoLog(L"  view-overlay: shadow %ls, exhaust %ls", shadow ? L"on" : L"off", exhaust ? L"on" : L"off");
        return RS_AUTO_DONE;
    }
    if (wcscmp(verb, L"view-model") == 0) {
        const int native = _wcsicmp(arg, L"native") == 0;
        if (!native && _wcsicmp(arg, L"classic") != 0) {
            Rs_AutoLog(L"  view-model: say native or classic");
            return RS_AUTO_FAIL;
        }
        if (native && !RsView_HasNative(g_char.view)) {
            Rs_AutoLog(L"  view-model: the preview has no native model - it shows the classic one");
            return RS_AUTO_FAIL;
        }
        RsView_SetShowNative(g_char.view, native);
        Char_AutoViewDone();
        Rs_AutoLog(L"  view-model: %ls", native ? L"native" : L"classic");
        return RS_AUTO_DONE;
    }
    if (wcscmp(verb, L"view-bench") == 0) {
        int avg = 0, longest = 0, p95 = 0, w = 0, h = 0, n;
        wchar_t *end;
        long v = wcstol(arg, &end, 10);
        while (*end == L' ')
            end++;
        if (end == arg || *end || v < 1 || v > 1000) {
            Rs_AutoLog(L"  view-bench: say how many pictures, 1 to 1000");
            return RS_AUTO_FAIL;
        }
        n = RsView_Bench(g_char.view, (int)v, &avg, &longest, &p95, &w, &h);
        if (n <= 0) {
            Rs_AutoLog(L"  view-bench: no picture drawn (%d x %d pixels)", w, h);
            return RS_AUTO_FAIL;
        }
        UpdateWindow(g_char.view);
        Rs_AutoLog(L"  view-bench: %d pictures of %d x %d pixels, %lu triangles in the pose shown: picture %d us on average, "
                   L"p95 %d us, longest %d us",
                   n, w, h, g_char.previewShown ? g_char.previewTris[g_char.poseNow] : 0ul, avg, p95, longest);
        return RS_AUTO_DONE;
    }
    return RS_AUTO_UNKNOWN;
}

static int Char_AutoTurn(const wchar_t *arg)
{
    wchar_t *end;
    long v = wcstol(arg, &end, 10);
    while (*end == L' ')
        end++;
    if (end == arg || *end) {
        Rs_AutoLog(L"  turn: '%ls' is not a whole number of degrees", arg);
        return RS_AUTO_FAIL;
    }
    v %= 360;
    if (v < 0)
        v += 360;
    RsView_SetYaw(g_char.view, (int)v);
    g_char.yawNow = RsView_GetYaw(g_char.view);
    UpdateWindow(g_char.view);      // painted before a following "shot"
    Rs_AutoLog(L"  turn: %d degrees", g_char.yawNow);
    return RS_AUTO_DONE;
}

// ---------------------------------------------------------------------------
// Callbacks of the page
// ---------------------------------------------------------------------------

static void Char_Create(HWND page)
{
    WNDCLASSEXW wc;
    HINSTANCE inst = GetModuleHandleW(NULL);
    int i;

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpfnWndProc = Char_ImageProc;
    wc.lpszClassName = CHAR_IMAGE_CLASS;
    RegisterClassExW(&wc);
    RsView_Register(inst);

    // The heads of the tabs first: the first stops of the key Tab.
    for (i = 0; i < CHAR_TABS; i++) {
        wchar_t text[32];
        swprintf(text, 32, L"%d %ls", i + 1, g_charTabTexts[i]);
        g_char.tabHead[i] = Char_TabButton(page, CHAR_ID_TAB + i, text, RS_FONT_BOLD);
    }
    g_char.tab = CHAR_TAB_MODEL;
    g_char.extrasCard = CHAR_EXTRAS_IMPORT;

    g_char.modelLabel = Rs_Label(page, CHAR_ID_MODEL_LABEL, L"Model (PLY or OBJ)", RS_FONT_BOLD);
    g_char.model = Rs_Edit(page, CHAR_ID_MODEL, L"", 0);
    g_char.modelBrowse = Rs_Button(page, CHAR_ID_MODEL_BROWSE, L"Browse...");
    g_char.modelInfo = Rs_Label(page, CHAR_ID_MODEL_INFO, L"-", RS_FONT_SMALL);   // wraps (Char_Layout)
    g_char.modelImport = Rs_Label(page, CHAR_ID_MODEL_IMPORT, L"", RS_FONT_SMALL); // wraps (Char_Layout)
    {
        static const wchar_t *const heads[4] = { L"Item", L"File or group", L"Material", L"State" };
        LVCOLUMNW col;
        g_char.modelFiles = Rs_ListView(page, CHAR_ID_MODEL_FILES, LVS_NOSORTHEADER);
        ListView_SetExtendedListViewStyleEx(g_char.modelFiles, LVS_EX_INFOTIP, LVS_EX_INFOTIP);
        for (i = 0; i < 4; i++) {
            memset(&col, 0, sizeof(col));
            col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM | LVCF_FMT;
            col.fmt = LVCFMT_LEFT;
            col.pszText = (LPWSTR)heads[i];
            col.cx = Rs_Px(80);
            col.iSubItem = i;
            ListView_InsertColumn(g_char.modelFiles, i, &col);
        }
        Rs_SetTip(g_char.modelFiles, L"What the OBJ brought along: its material file (MTL), the texture of each "
                                     L"material and the groups (o, g - for information only). Point at a row for "
                                     L"the whole path.");
        ShowWindow(g_char.modelImport, SW_HIDE);
        ShowWindow(g_char.modelFiles, SW_HIDE);
    }
    g_char.texturesLabel = Rs_Label(page, CHAR_ID_TEXTURES_LABEL, L"Textures folder", RS_FONT_BOLD);
    g_char.textures = Rs_Edit(page, CHAR_ID_TEXTURES, L"", 0);
    SendMessageW(g_char.textures, EM_SETCUEBANNER, FALSE,
                 (LPARAM)L"Optional - a folder with the textures");
    Rs_SetTip(g_char.textures, L"rldpack looks here first for a texture of the material file that is not at its "
                               L"path (by its name, also in the folders textures, tex, images and maps), then next "
                               L"to the material file and the model.");
    g_char.texturesBrowse = Rs_Button(page, CHAR_ID_TEXTURES_BROWSE, L"Browse...");
    g_char.texturesClear = Rs_Button(page, CHAR_ID_TEXTURES_CLEAR, L"Clear");

    g_char.nameLabel = Rs_Label(page, CHAR_ID_NAME_LABEL, L"Name", RS_FONT_BOLD);
    g_char.name = Rs_Edit(page, CHAR_ID_NAME, L"", ES_UPPERCASE);
    SendMessageW(g_char.name, EM_LIMITTEXT, CHAR_NAME_MAX, 0);
    g_char.nameNote = Rs_Label(page, CHAR_ID_NAME_NOTE, L"", RS_FONT_SMALL);

    g_char.classLabel = Rs_Label(page, CHAR_ID_CLASS_LABEL, L"Driving style", RS_FONT_BOLD);
    g_char.cls = Rs_Combo(page, CHAR_ID_CLASS);
    for (i = 0; i < CHAR_CLASS_COUNT; i++)
        SendMessageW(g_char.cls, CB_ADDSTRING, 0, (LPARAM)g_charClasses[i].text);
    SendMessageW(g_char.cls, CB_SETCURSEL, 0, 0);
    g_char.classHelp = Rs_Label(page, CHAR_ID_CLASS_HELP,
                                L"How the kart drives: speed, acceleration and turning as the drivers named.",
                                RS_FONT_SMALL);
    Rs_SetTextColor(g_char.classHelp, RS_COL_MUTED);

    g_char.maskLabel = Rs_Label(page, CHAR_ID_MASK_LABEL, L"Mask", RS_FONT_BOLD);
    g_char.mask = Rs_Combo(page, CHAR_ID_MASK);
    for (i = 0; i < CHAR_MASK_COUNT; i++)
        SendMessageW(g_char.mask, CB_ADDSTRING, 0, (LPARAM)g_charMasks[i].text);
    SendMessageW(g_char.mask, CB_SETCURSEL, CHAR_TEMPLATE_MASK, 0);
    g_char.maskHelp = Rs_Label(page, CHAR_ID_MASK_HELP,
                               L"Worn for the mask item and after a fall, with its music.",
                               RS_FONT_SMALL);
    Rs_SetTextColor(g_char.maskHelp, RS_COL_MUTED);

    g_char.mapLabel = Rs_Label(page, CHAR_ID_MAPCOLOR_LABEL, L"Minimap colour", RS_FONT_BOLD);
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_HAND);
    wc.lpfnWndProc = Char_SwatchProc;
    wc.lpszClassName = CHAR_SWATCH_CLASS;
    RegisterClassExW(&wc);
    g_char.mapSwatch = CreateWindowExW(0, CHAR_SWATCH_CLASS, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, page,
                                       (HMENU)(INT_PTR)CHAR_ID_MAPCOLOR, inst, NULL);
    g_char.mapPick = Rs_Button(page, CHAR_ID_MAPCOLOR_PICK, L"Choose...");
    g_char.mapLike = Rs_Button(page, CHAR_ID_MAPCOLOR_LIKE, L"Like the template");
    g_char.mapHelp = Rs_Label(page, CHAR_ID_MAPCOLOR_HELP, L"The driver's marker on the minimap. Like the template: as Fake Crash shows it. "
                              L"80 per channel is the icon as drawn, higher is brighter.",
                              RS_FONT_SMALL);
    Rs_SetTextColor(g_char.mapHelp, RS_COL_MUTED);
    for (i = 0; i < 16; i++)
        g_char.mapCustom[i] = RGB(255, 255, 255);
    g_char.mapColorSet = 0;
    g_char.mapColor = CHAR_TEMPLATE_MAP_COLOR;
    Char_MapColorShow();

    // The tab In-game look: Portrait | In the race, and on In the race the
    // shadow and the exhaust (preview feature, open to everyone).
    for (i = 0; i < CHAR_LOOK_CARDS; i++)
        g_char.lookSwitch[i] = Char_TabButton(page, CHAR_ID_LOOK_SWITCH + i, g_charLookTexts[i], RS_FONT_SECTION);
    g_char.lookCard = CHAR_LOOK_PORTRAIT;
    g_char.lookHint = Rs_PreviewMark(page, CHAR_ID_LOOK_HINT);
    SetWindowLongPtrW(g_char.lookHint, GWL_STYLE, GetWindowLongPtrW(g_char.lookHint, GWL_STYLE) | SS_RIGHT);
    Rs_SetTip(g_char.lookHint, L"Preview: shadow and exhaust fitted to the model, for the classic and the native model.");
    g_char.shadowLabel = Rs_Label(page, CHAR_ID_SHADOW_LABEL, L"Shadow", RS_FONT_BOLD);
    g_char.shadow = Rs_Combo(page, CHAR_ID_SHADOW);
    for (i = 0; i < 3; i++)
        SendMessageW(g_char.shadow, CB_ADDSTRING, 0, (LPARAM)g_charShadowTexts[i]);
    g_char.shadowHelp = Rs_Label(page, CHAR_ID_SHADOW_HELP, L"Auto: fitted to the model's footprint by rldpack. Retail: the kart's.", RS_FONT_SMALL);
    Rs_SetTextColor(g_char.shadowHelp, RS_COL_MUTED);
    g_char.exhaustLabel = Rs_Label(page, CHAR_ID_EXHAUST_LABEL, L"Exhaust", RS_FONT_BOLD);
    g_char.exhaust = Rs_Combo(page, CHAR_ID_EXHAUST);
    for (i = 0; i < 3; i++)
        SendMessageW(g_char.exhaust, CB_ADDSTRING, 0, (LPARAM)g_charExhaustTexts[i]);
    g_char.exhaustHelp = Rs_Label(page, CHAR_ID_EXHAUST_HELP, L"Smoke and glow. Custom: the two points below.", RS_FONT_SMALL);
    Rs_SetTextColor(g_char.exhaustHelp, RS_COL_MUTED);
    for (i = 0; i < 2; i++) {
        static const wchar_t *const cues[3] = { L"x", L"y", L"z" };
        int a;
        g_char.pointLabel[i] = Rs_Label(page, CHAR_ID_POINT + i * CHAR_POINT_IDS, i ? L"Point 2 (right)" : L"Point 1 (left)",
                                        RS_FONT_BOLD);
        for (a = 0; a < 3; a++) {
            g_char.point[i][a] = Rs_Edit(page, CHAR_ID_POINT + i * CHAR_POINT_IDS + 1 + a, L"", 0);
            SendMessageW(g_char.point[i][a], EM_SETCUEBANNER, FALSE, (LPARAM)cues[a]);
        }
        g_char.pointPick[i] = Rs_Button(page, CHAR_ID_POINT + i * CHAR_POINT_IDS + 4, L"Pick");
        Rs_SetTip(g_char.pointPick[i], L"Then click the model in the preview (pose Neutral): the point of its surface under the click.");
    }
    g_char.lookNote = Rs_Label(page, CHAR_ID_LOOK_NOTE, CHAR_LOOK_NOTE_TEXT, RS_FONT_SMALL);
    Rs_SetTextColor(g_char.lookNote, RS_COL_MUTED);

    g_char.sizeLabel = Rs_Label(page, CHAR_ID_SIZE_LABEL, L"Size", RS_FONT_BOLD);
    g_char.size = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS | TBS_ENABLESELRANGE,
                                  0, 0, 10, 10, page, (HMENU)(INT_PTR)CHAR_ID_SIZE, inst, NULL);
    SendMessageW(g_char.size, TBM_SETRANGE, FALSE, MAKELPARAM(CHAR_SIZE_MIN, CHAR_SIZE_MAX));
    SendMessageW(g_char.size, TBM_SETLINESIZE, 0, 1);
    SendMessageW(g_char.size, TBM_SETPAGESIZE, 0, 10);
    SendMessageW(g_char.size, TBM_SETPOS, TRUE, CHAR_SIZE_DEFAULT);
    g_char.sizeValue = Rs_Label(page, CHAR_ID_SIZE_VALUE, L"", RS_FONT_BODY);
    g_char.sizeNote = Rs_Label(page, CHAR_ID_SIZE_NOTE, L"", RS_FONT_SMALL);
    g_char.sizeHint = Rs_Label(page, CHAR_ID_SIZE_HINT, CHAR_SIZE_HINT_TEXT, RS_FONT_SMALL);
    Rs_SetTextColor(g_char.sizeHint, RS_COL_MUTED);
    g_char.sizeCrash = Rs_Label(page, CHAR_ID_SIZE_CRASH, L"100 % = Crash size", RS_FONT_SMALL);
    Rs_SetTextColor(g_char.sizeCrash, RS_COL_MUTED);
    g_char.sizeFit = Rs_Label(page, CHAR_ID_SIZE_FIT, CHAR_FIT_WAIT_TEXT, RS_FONT_SMALL);
    g_char.fitColor = RS_COL_MUTED;
    Rs_SetTextColor(g_char.sizeFit, g_char.fitColor);
    g_char.sizeNow = CHAR_SIZE_DEFAULT;

    g_char.optionsLabel = Rs_Label(page, CHAR_ID_OPTIONS_LABEL, L"Options", RS_FONT_BOLD);
    g_char.repair = Rs_Check(page, CHAR_ID_REPAIR, L"Repair the model");
    g_char.openParts = Rs_Check(page, CHAR_ID_OPEN_PARTS, L"Draw open parts from both sides");
    g_char.remesh = Rs_Check(page, CHAR_ID_REMESH, L"Closed hull (remesh)");
    g_char.reduce = Rs_Check(page, CHAR_ID_REDUCE, L"Reduce to fit");
    Rs_SetTip(g_char.reduce, L"Only a model over the limit of triangles a driver may draw: rldpack takes triangles "
                             L"away until it fits. A model under the limit is never reduced.");
    Rs_SetTip(g_char.remesh, L"Makes every part a closed hull. Needs Reduce to fit: the hulls have far more "
                             L"triangles than a driver may draw.");
    g_char.wheels = Rs_Check(page, CHAR_ID_WHEELS, L"Show kart wheels");
    Char_SetChecked(g_char.repair, 1);
    Char_SetChecked(g_char.openParts, 1);
    Char_SetChecked(g_char.remesh, 0);      // only on request
    Char_SetChecked(g_char.reduce, 0);      // a model under the limit is never reduced anyway
    Char_SetChecked(g_char.wheels, 1);
    Char_LookDefaults();
    Char_LookEnable();
    // The user mode (Rs_NativeForUsers): rldpack's defaults, hidden
    // (Char_UserHidden) - Reduce to fit on, so that everything that asks the
    // box sees what is passed (nothing: --reduce auto).
    if (Rs_NativeForUsers())
        Char_SetChecked(g_char.reduce, 1);
    // Only in the user mode: the classic model (CMDL) is always built - the
    // box says so, ticked and greyed out until leaving it out is possible.
    g_char.fallback = Rs_Check(page, CHAR_ID_FALLBACK, L"Include classic fallback model");
    Char_SetChecked(g_char.fallback, 1);
    EnableWindow(g_char.fallback, FALSE);
    Rs_SetTip(g_char.fallback, L"Always built: the game's own model of the driver, for every view the native model "
                               L"does not draw yet. Leaving it out comes later.");
    g_char.fallbackHint = Rs_Label(page, CHAR_ID_FALLBACK_HINT, L"Coming soon", RS_FONT_SMALL);
    Rs_SetTextColor(g_char.fallbackHint, RS_COL_MUTED);
    g_char.nativeLine = Rs_Label(page, CHAR_ID_NATIVE_LINE, L"", RS_FONT_SMALL);
    if (!Rs_NativeForUsers()) {
        ShowWindow(g_char.fallback, SW_HIDE);
        ShowWindow(g_char.fallbackHint, SW_HIDE);
        ShowWindow(g_char.nativeLine, SW_HIDE);
    }
    Char_UpdateOptions();
    g_char.quality = Rs_Label(page, CHAR_ID_QUALITY, L"", RS_FONT_SMALL);   // wraps (Char_Layout)
    g_char.qualityColor = RS_COL_TEXT;

    g_char.iconLabel = Rs_Label(page, CHAR_ID_ICON_LABEL, L"Icon (PNG)", RS_FONT_BOLD);
    g_char.icon = Rs_Edit(page, CHAR_ID_ICON, L"", 0);
    SendMessageW(g_char.icon, EM_SETCUEBANNER, FALSE, (LPARAM)L"Optional - without it the game shows the template's icon");
    g_char.iconBrowse = Rs_Button(page, CHAR_ID_ICON_BROWSE, L"Browse...");
    g_char.iconClear = Rs_Button(page, CHAR_ID_ICON_CLEAR, L"Clear");
    g_char.iconFitLabel = Rs_Label(page, CHAR_ID_ICON_FIT_LABEL, L"Framing", RS_FONT_BOLD);
    g_char.iconFit = Rs_Combo(page, CHAR_ID_ICON_FIT);
    for (i = 0; i < CHAR_ICON_FIT_COUNT; i++)
        SendMessageW(g_char.iconFit, CB_ADDSTRING, 0, (LPARAM)g_charIconFits[i].text);
    SendMessageW(g_char.iconFit, CB_SETCURSEL, CHAR_ICON_FIT_DEFAULT, 0);
    Rs_SetTip(g_char.iconFit, L"Fit: your subject whole, as large as the heads of the game's drivers. Fill: it fills "
                              L"the frame, cut at the sides or the bottom. As is: cut to 43:25 in the middle, as before. "
                              L"Never stretched.");
    g_char.iconCorners = Rs_Check(page, CHAR_ID_ICON_CORNERS, L"Make background transparent");
    Rs_SetTip(g_char.iconCorners, L"Removes the background colour that touches the corners - above all for pictures "
                                  L"without transparency. Transparency in the PNG is always kept, a thin outline too.");
    g_char.iconFrame = Rs_Check(page, CHAR_ID_ICON_FRAME, L"Retail frame");
    Rs_SetTip(g_char.iconFrame, L"Puts the frame and the dark half-transparent box of the game's portraits behind "
                                L"your picture.");
    g_char.iconCaption[CHAR_IMG_ORIGINAL] =
        Rs_Label(page, CHAR_ID_ICON_CAPTION, L"Your picture (PNG, any size)", RS_FONT_SMALL);
    g_char.iconCaption[CHAR_IMG_ICON] =
        Rs_Label(page, CHAR_ID_ICON_CAPTION + 1, L"In the game, beside Fake Crash", RS_FONT_SMALL);
    // The second box shows the converted icon in the game (g_char.game).
    for (i = 0; i < CHAR_IMG_COUNT; i++) {
        Rs_SetTextColor(g_char.iconCaption[i], RS_COL_MUTED);
        g_char.iconImage[i] = Char_ImageBox(page, CHAR_ID_ICON_IMAGE + i,
                                            i == CHAR_IMG_ICON ? &g_char.game : &g_char.image[i]);
    }
    Char_UpdateIconOptions();

    g_char.voicesLabel = Rs_Label(page, CHAR_ID_VOICES_LABEL, L"Voices", RS_FONT_BOLD);
    g_char.voices = Rs_Edit(page, CHAR_ID_VOICES, L"", 0);
    SendMessageW(g_char.voices, EM_SETCUEBANNER, FALSE, (LPARAM)L"Optional - a folder with WAV or VAG files");
    g_char.voicesBrowse = Rs_Button(page, CHAR_ID_VOICES_BROWSE, L"Browse...");
    g_char.voicesClear = Rs_Button(page, CHAR_ID_VOICES_CLEAR, L"Clear");
    g_char.voicesNote = Rs_Label(page, CHAR_ID_VOICES_NOTE, CHAR_VOICES_TEXT, RS_FONT_SMALL);
    g_char.voicesColor = RS_COL_MUTED;
    Rs_SetTextColor(g_char.voicesNote, RS_COL_MUTED);
    {
        static const wchar_t *const heads[4] = { L"File", L"Length", L"Size", L"Event" };
        LVCOLUMNW col;
        g_char.voiceList = Rs_ListView(page, CHAR_ID_VOICE_LIST, LVS_NOSORTHEADER);
        ListView_SetExtendedListViewStyleEx(g_char.voiceList, LVS_EX_INFOTIP, LVS_EX_INFOTIP);
        for (i = 0; i < 4; i++) {
            memset(&col, 0, sizeof(col));
            col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM | LVCF_FMT;
            col.fmt = (i == 1 || i == 2) ? LVCFMT_RIGHT : LVCFMT_LEFT;
            col.pszText = (LPWSTR)heads[i];
            col.cx = Rs_Px(80);
            col.iSubItem = i;
            ListView_InsertColumn(g_char.voiceList, i, &col);
        }
    }
    g_char.voiceEventLabel = Rs_Label(page, CHAR_ID_VOICE_EVENT_LABEL, L"Event", RS_FONT_BOLD);
    g_char.voiceEvent = Rs_Combo(page, CHAR_ID_VOICE_EVENT);
    for (i = 0; i <= CHAR_VOICE_NONE; i++)
        SendMessageW(g_char.voiceEvent, CB_ADDSTRING, 0, (LPARAM)g_charVoiceEvents[i].text);
    Rs_SetTip(g_char.voiceEvent, L"The event the file chosen in the list is said at. Its name chooses one by itself; "
                                 L"this choice goes over the name. Unassigned: the file is not packed.");
    g_char.voicePlay = Rs_Button(page, CHAR_ID_VOICE_PLAY, L"Play");
    Rs_SetTip(g_char.voicePlay, L"Plays the file chosen as the game will play it: 22050 Hz mono, cut where it is "
                                L"too long, normalized when that is ticked. A double click on a file plays it too.");
    g_char.voiceNorm = Rs_Check(page, CHAR_ID_VOICE_NORMALIZE, L"Normalize volume");
    Rs_SetTip(g_char.voiceNorm, L"Brings every clip to the same peak (-1 dB), so that no line is much louder or "
                                L"quieter than the others. Off: the files as recorded.");
    Char_SetChecked(g_char.voiceNorm, 1);
    g_char.voiceEventsLabel = Rs_Label(page, CHAR_ID_VOICE_EVENTS_LABEL, L"Events", RS_FONT_BOLD);
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpfnWndProc = Char_VoiceEventsProc;
    wc.lpszClassName = CHAR_VOICE_CLASS;
    RegisterClassExW(&wc);
    g_char.voiceEvents = CreateWindowExW(0, CHAR_VOICE_CLASS, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, page,
                                         (HMENU)(INT_PTR)CHAR_ID_VOICE_EVENTS, inst, NULL);
    Rs_SetTip(g_char.voiceEvents, L"The ten events of a race the driver speaks at, with the number of clips for "
                                  L"each - the game picks one of them as for the original drivers. Grey: no clip, "
                                  L"the driver stays silent there; red: more than 4, the build stops. Fire: a "
                                  L"missile, a bomb, the warp orb, the clock or a mask. Passing is said by a driver "
                                  L"who has just passed the player - a custom driver is always the player, so it is "
                                  L"never heard.");
    g_char.voiceRule = Rs_Label(page, CHAR_ID_VOICE_RULE, CHAR_VOICE_RULE_TEXT, RS_FONT_SMALL);
    Rs_SetTextColor(g_char.voiceRule, RS_COL_MUTED);
    Rs_SetTip(g_char.voiceRule, L"A file named boost1 to boost4 is said at a boost, hit1 to hit4 when hit, and so on "
                                L"(up to 4 clips each, case does not matter); yes and hit are the two short sounds. "
                                L"WAV (8 to 48 kHz) or PS1 VAG; a WAV goes before a VAG of the same name.");

    g_char.view = CreateWindowExW(0, RS_VIEW_CLASS, L"No model yet", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, page,
                                  (HMENU)(INT_PTR)CHAR_ID_VIEW, inst, NULL);
    g_char.yawNow = RS_VIEW_DEFAULT_YAW;
    RsView_SetYaw(g_char.view, g_char.yawNow);
    g_char.pose = Rs_Combo(page, CHAR_ID_POSE);
    for (i = 0; i < CHAR_POSES; i++)
        SendMessageW(g_char.pose, CB_ADDSTRING, 0, (LPARAM)g_charPoseTexts[i]);
    SendMessageW(g_char.pose, CB_SETCURSEL, 0, 0);
    g_char.viewNote = Rs_Label(page, CHAR_ID_VIEW_NOTE, L"", RS_FONT_SMALL);

    g_char.rawToggle = Rs_Button(page, CHAR_ID_RAW_TOGGLE, L"Show rldpack output");
    g_char.outLabel = Rs_Label(page, CHAR_ID_OUT_LABEL, L"Output", RS_FONT_BOLD);
    g_char.out = Rs_Edit(page, CHAR_ID_OUT, L"", 0);
    Char_OutCue();
    g_char.outBrowse = Rs_Button(page, CHAR_ID_OUT_BROWSE, L"Browse...");
    g_char.check = Rs_Button(page, CHAR_ID_CHECK, L"Check");
    g_char.build = Rs_PrimaryButton(page, CHAR_ID_BUILD, L"Build character");
    g_char.headline = Rs_Label(page, CHAR_ID_HEADLINE, L"", RS_FONT_BOLD);
    SetWindowLongPtrW(g_char.headline, GWL_STYLE, GetWindowLongPtrW(g_char.headline, GWL_STYLE) | SS_ENDELLIPSIS);
    g_char.msgs = Rs_MsgList(page, CHAR_ID_MESSAGES);
    g_char.raw = Rs_Edit(page, CHAR_ID_RAW, L"",
                         ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_HSCROLL);
    SendMessageW(g_char.raw, WM_SETFONT, (WPARAM)Rs_Font(RS_FONT_MONO), FALSE);
    SendMessageW(g_char.raw, EM_LIMITTEXT, 0, 0);
    ShowWindow(g_char.raw, SW_HIDE);
    g_char.show = Rs_Button(page, CHAR_ID_SHOW, L"Show in folder");
    ShowWindow(g_char.show, SW_HIDE);
    g_char.reduceFit = Rs_Button(page, CHAR_ID_REDUCE_FIT, L"Reduce to fit");
    ShowWindow(g_char.reduceFit, SW_HIDE);
    g_char.cancel = Rs_Button(page, CHAR_ID_CANCEL, L"Cancel");
    Rs_SetTip(g_char.cancel, L"Stops rldpack. A build cancelled writes nothing; a character file from before stays as "
                             L"it was.");
    ShowWindow(g_char.cancel, SW_HIDE);
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpfnWndProc = Char_ProgressProc;
    wc.lpszClassName = CHAR_PROGRESS_CLASS;
    RegisterClassExW(&wc);
    g_char.progress = CreateWindowExW(0, CHAR_PROGRESS_CLASS, L"", WS_CHILD, 0, 0, 10, 10, page,
                                      (HMENU)(INT_PTR)CHAR_ID_PROGRESS, inst, NULL);
    g_char.back = Rs_Button(page, CHAR_ID_BACK, L"< Back");
    g_char.next = Rs_Button(page, CHAR_ID_NEXT, L"Next >");
    Rs_MsgListSetClickable(g_char.msgs, 1);
    Rs_MsgListSetWhole(g_char.msgs, 1);
    Rs_SetTip(g_char.msgs, L"Click a message to open the tab it belongs to.");
    // The tab Extras: its switch, the card Import, then the cards of the
    // preview features.
    for (i = 0; i < CHAR_EXTRAS_COUNT; i++)
        g_char.extrasSwitch[i] = Char_TabButton(page, CHAR_ID_EXTRAS + i, g_charExtrasTexts[i], RS_FONT_SECTION);
    g_char.importNote = Rs_Label(page, CHAR_ID_IMPORT_NOTE,
                                 L"How rldpack reads the model. The defaults suit most models; a message names the choice "
                                 L"to change when one does not.",
                                 RS_FONT_SMALL);
    Rs_SetTextColor(g_char.importNote, RS_COL_MUTED);
    g_char.upLabel = Rs_Label(page, CHAR_ID_UP_LABEL, L"Up", RS_FONT_BOLD);
    g_char.up = Rs_Combo(page, CHAR_ID_UP);
    for (i = 0; i < CHAR_UP_COUNT; i++)
        SendMessageW(g_char.up, CB_ADDSTRING, 0, (LPARAM)g_charUpTexts[i]);
    SendMessageW(g_char.up, CB_SETCURSEL, 0, 0);
    g_char.upHelp = Rs_Label(page, CHAR_ID_UP_HELP, L"The axis that points up in the file. Z: Blender's own axes, the "
                             L"front at -Y.", RS_FONT_SMALL);
    Rs_SetTextColor(g_char.upHelp, RS_COL_MUTED);
    g_char.forwardLabel = Rs_Label(page, CHAR_ID_FORWARD_LABEL, L"Forward", RS_FONT_BOLD);
    g_char.forward = Rs_Combo(page, CHAR_ID_FORWARD);
    for (i = 0; i < CHAR_FORWARD_COUNT; i++)
        SendMessageW(g_char.forward, CB_ADDSTRING, 0, (LPARAM)g_charForwardTexts[i]);
    SendMessageW(g_char.forward, CB_SETCURSEL, 0, 0);
    g_char.forwardHelp = Rs_Label(page, CHAR_ID_FORWARD_HELP, L"-Z: the model looks backwards and is turned round.",
                                  RS_FONT_SMALL);
    Rs_SetTextColor(g_char.forwardHelp, RS_COL_MUTED);
    g_char.colorsLabel = Rs_Label(page, CHAR_ID_COLORS_LABEL, L"Colors", RS_FONT_BOLD);
    g_char.colors = Rs_Combo(page, CHAR_ID_COLORS);
    for (i = 0; i < CHAR_COLORS_COUNT; i++)
        SendMessageW(g_char.colors, CB_ADDSTRING, 0, (LPARAM)g_charColorsTexts[i]);
    SendMessageW(g_char.colors, CB_SETCURSEL, 0, 0);
    g_char.colorsHelp = Rs_Label(page, CHAR_ID_COLORS_HELP, L"The palette of the model: up to 128 colours, or 64 as the "
                                 L"original drivers have.", RS_FONT_SMALL);
    Rs_SetTextColor(g_char.colorsHelp, RS_COL_MUTED);
    g_char.vcolorsLabel = Rs_Label(page, CHAR_ID_VCOLORS_LABEL, L"Vertex colors", RS_FONT_BOLD);
    g_char.vcolors = Rs_Combo(page, CHAR_ID_VCOLORS);
    for (i = 0; i < CHAR_VCOLORS_COUNT; i++)
        SendMessageW(g_char.vcolors, CB_ADDSTRING, 0, (LPARAM)g_charVColorTexts[i]);
    SendMessageW(g_char.vcolors, CB_SETCURSEL, 0, 0);
    Rs_SetTip(g_char.vcolors, L"Auto: an OBJ ripped from a PS1 game (vertex colours of its textured faces around 0x80) "
                              L"lights its textures as the PS1 did, any other keeps texture times vertex colour. Texture "
                              L"modulation (PS1): always as the PS1, 0x80 shows the texture as it is. Plain color: always "
                              L"texture times vertex colour.");
    g_char.vcolorsHelp = Rs_Label(page, CHAR_ID_VCOLORS_HELP, L"OBJ: how a vertex colour meets the texture of its face.",
                                  RS_FONT_SMALL);
    Rs_SetTextColor(g_char.vcolorsHelp, RS_COL_MUTED);
    g_char.nativeLabel = Rs_Label(page, CHAR_ID_NATIVE_LABEL, L"Native model", RS_FONT_BOLD);
    g_char.native = Rs_Check(page, CHAR_ID_NATIVE, L"Also write the mesh and textures as they are");
    g_char.nativeHint = Rs_PreviewMark(page, CHAR_ID_NATIVE_HINT);
    // right-aligned in a box as wide as the longer of "Coming soon" and
    // "Preview feature" (Char_LayNative)
    SetWindowLongPtrW(g_char.nativeHint, GWL_STYLE, GetWindowLongPtrW(g_char.nativeHint, GWL_STYLE) | SS_RIGHT);
    g_char.nativeHelp = Rs_Label(page, CHAR_ID_NATIVE_HELP, CHAR_NATIVE_TEXT_OBJ, RS_FONT_SMALL);
    Rs_SetTextColor(g_char.nativeHelp, RS_COL_MUTED);
    Rs_SetTip(g_char.native, L"Preview: rldpack also writes the OBJ's own triangles, UVs, materials and texture files (CNET, CTXT) into the "
                             L"character, beside the classic model; it needs Show kart wheels off or a wheel model of your own (Extras, "
                             L"Wheels). The game draws them with NATIVE DRIVERS "
                             L"set to PREVIEW (OPTIONS, GRAPHICS); otherwise it draws the classic model as before.");
    Rs_SetTip(g_char.nativeHint, L"Preview: the game draws the native model with NATIVE DRIVERS set to PREVIEW (OPTIONS, GRAPHICS).");
    Char_NativeUpdate();
    CharWheels_Create(page, g_char.view);
    CharAnim_Create(page, g_char.view);
    // Tab order: the heads, the tab shown (Reduce to fit after the options),
    // then the preview and the bar.
    SetWindowPos(g_char.reduceFit, g_char.quality, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    {
        HWND last[] = { g_char.view, g_char.pose, g_char.viewNote, g_char.headline, g_char.progress, g_char.cancel,
                        g_char.rawToggle, g_char.msgs,
                        g_char.raw, g_char.outLabel, g_char.out, g_char.outBrowse, g_char.show, g_char.back,
                        g_char.next, g_char.check, g_char.build };
        for (i = 0; i < (int)(sizeof(last) / sizeof(last[0])); i++)
            SetWindowPos(last[i], HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    DragAcceptFiles(page, TRUE);
    SetWindowSubclass(Rs_MainWindow(), Char_MainSub, CHAR_SUBCLASS_MAIN, (DWORD_PTR)page);
    Char_TempSweep();

    g_char.applying = 1;
    Char_NameFilter();
    Char_SizeSet(CHAR_SIZE_DEFAULT);
    Char_SizeForget();
    g_char.applying = 0;
    Char_ApplyIcon(0);
    Char_NoModel(page);
}

// Label left, field right of it.
static void Char_PlaceField(HWND label, HWND field, int x, int labelW, int y, int fieldW)
{
    MoveWindow(label, x, y + Rs_Px(4), labelW, Rs_Px(20), TRUE);
    MoveWindow(field, x + labelW + Rs_Px(8), y, fieldW, Rs_Px(28), TRUE);
}

// Sizes of the layout (96 dpi). The page is laid out for the window it gets:
// the tabs on the left, the preview on the right as tall as they are, the bar
// below both. Its height comes from what is left below the tallest tab (the
// card Animations of the tab Extras), between CHAR_BAR_MIN_H and
// CHAR_BAR_MAX_H. Only where even CHAR_BAR_MIN_H does not fit below a tab, the
// bar goes lower than the page and the shell scrolls it (reloadstudio.h,
// "Pages").
// THE COMPACT LAYOUT: a page narrower than CHAR_FULL_W or lower than
// CHAR_FULL_H (the start window on 1366 x 768 at 150 %: 1344 x 640 pixels
// of client, about 836 x 426 left for the page) gets a
// one-line heading (Rs_PageSetCompactHead), lower tab heads, notes on one line
// with their whole text as the tooltip (Char_TextHeight), the bar in two rows
// without Back and Next (Ctrl+Tab and the heads step through the tabs) and
// the preview without its title line. Measured with the automation verb
// "controls" (--screen simulates the start window with the frame of the
// simulated dpi, Rs_PlaceWindow): at 1366 x 768 and 1920 x 1080, 100 % and
// 150 %, nothing of the page scrolls, with --enable-preview-features and
// without; without it a model over the limit (the button Reduce to fit) or
// with a texture missing (the field Textures folder) may still push the bar
// down at 1366 x 768 with 150 % (only up and down).
#define CHAR_FULL_W      920    // the layout as always from here on
#define CHAR_FULL_H      560
#define CHAR_PAGE_MIN_W  800    // below: the view scrolls (the compact layout needs about this)
#define CHAR_PAGE_MIN_H  400
#define CHAR_COMPACT_STRIP_H 28
#define CHAR_COMPACT_BAR_H   78 // headline and output; messages (two whole lines), Check and Build
#define CHAR_COMPACT_VIEW_MIN_W 200
#define CHAR_LEFT_MIN_W  540    // the column of the tabs, at least
#define CHAR_LEFT_MAX_W  760    // and at most: the rest goes to the preview
#define CHAR_VIEW_MIN_W  300    // the column of the preview, at least
#define CHAR_STRIP_H     36     // the heads of the tabs
#define CHAR_BAR_MIN_H   104    // headline, and two rows of buttons beside the message list
#define CHAR_BAR_MAX_H   240
#define CHAR_VIEW_MIN_H  120    // the 3D view, at least

// Measures of the layout, the same for all tabs.
struct CharLay {
    int gap, labelW, browseW, clearW;
};

// The heads of the tabs from left on the line top, as wide as their text
// needs (narrower where the column is narrow). They reach one pixel into the
// card below: the head shown is open towards it. Returns the top of the card.
static int Char_LayStrip(int left, int right, int top)
{
    int widths[CHAR_TABS];
    int x = left + Rs_Px(12), h = Rs_Px(g_char.compact ? CHAR_COMPACT_STRIP_H : CHAR_STRIP_H), total = 0, room = right - left - Rs_Px(24), t;

    for (t = 0; t < CHAR_TABS; t++) {
        widths[t] = Rs_Px(12 + 20 + 8 + 16) + Char_TextWidth(g_char.tabHead[t], g_charTabTexts[t]);
        total += widths[t] + (t ? Rs_Px(4) : 0);
    }
    if (total > room) {
        int less = (total - room + CHAR_TABS - 1) / CHAR_TABS;
        for (t = 0; t < CHAR_TABS; t++)
            widths[t] -= less;
    }
    for (t = 0; t < CHAR_TABS; t++) {
        MoveWindow(g_char.tabHead[t], x, top, widths[t], h + 1, TRUE);
        x += widths[t] + Rs_Px(4);
    }
    return top + h;
}

// Tab 1 Model: model, what rldpack read of it (the list filesH tall), size,
// options, what rldpack did. Returns the bottom of what it placed.
static int Char_LayModelAt(const struct CharLay *k, const RECT *in, int filesH)
{
    HWND options[6];
    int x = in->left + k->labelW + Rs_Px(8), fieldW = in->right - x, y = in->top, infoH, qualityH, noteH, i, count;

    Char_PlaceField(g_char.modelLabel, g_char.model, in->left, k->labelW, y + Rs_Px(2), fieldW - k->browseW - Rs_Px(8));
    MoveWindow(g_char.modelBrowse, in->right - k->browseW, y, k->browseW, Rs_Px(32), TRUE);
    y += Rs_Px(34);
    // One line, or up to CHAR_INFO_LINES when the text needs them.
    Char_FitLines(g_char.modelInfo, g_char.infoFull, fieldW, g_char.compact ? 1 : CHAR_INFO_LINES);
    infoH = Char_TextHeight(g_char.modelInfo, fieldW, CHAR_INFO_LINES);
    Rs_SetTip(g_char.modelInfo, g_char.compact && wcscmp(g_char.infoFull, L"") != 0 ? g_char.infoFull : NULL);
    if (infoH < Rs_Px(18))
        infoH = Rs_Px(18);
    MoveWindow(g_char.modelInfo, x, y, fieldW, infoH, TRUE);
    y += infoH + Rs_Px(g_char.compact ? 4 : 8);
    // An OBJ (or a model not named .ply): the line of what was read right
    // below, then the list of its MTL, textures and groups over the width
    // of the card.
    if (g_char.importOn) {
        noteH = Char_NoteHeight(g_char.modelImport, fieldW);
        MoveWindow(g_char.modelImport, x, y - Rs_Px(6), fieldW, noteH, TRUE);
        y += noteH + Rs_Px(2);
    }
    if (g_char.filesOn && g_char.filesLeast > 0) {
        MoveWindow(g_char.modelFiles, in->left, y, in->right - in->left, filesH, TRUE);
        Char_ImportColumns(in->right - in->left);
        y += filesH + Rs_Px(8);
    }
    // An OBJ: the textures folder, as the icon and the voices folder are laid out.
    if (g_char.texturesOn) {
        Char_PlaceField(g_char.texturesLabel, g_char.textures, in->left, k->labelW, y + Rs_Px(2),
                        fieldW - k->browseW - k->clearW - Rs_Px(16));
        MoveWindow(g_char.texturesBrowse, in->right - k->browseW - k->clearW - Rs_Px(8), y, k->browseW, Rs_Px(32), TRUE);
        MoveWindow(g_char.texturesClear, in->right - k->clearW, y, k->clearW, Rs_Px(32), TRUE);
        y += Rs_Px(g_char.compact ? 34 : 40);
    }
    MoveWindow(g_char.sizeLabel, in->left, y + Rs_Px(4), k->labelW, Rs_Px(20), TRUE);
    MoveWindow(g_char.size, x - Rs_Px(4), y, fieldW - Rs_Px(64), Rs_Px(30), TRUE);
    MoveWindow(g_char.sizeValue, in->right - Rs_Px(60), y + Rs_Px(4), Rs_Px(60), Rs_Px(20), TRUE);
    y += Rs_Px(g_char.compact ? 30 : 32);
    // What 100 % is: in the label column under "Size", the fit beside it.
    MoveWindow(g_char.sizeCrash, in->left, y, k->labelW, Rs_Px(18), TRUE);
    noteH = Char_NoteHeight(g_char.sizeFit, fieldW);
    MoveWindow(g_char.sizeFit, x, y, fieldW, noteH, TRUE);
    y += Rs_Px(2) + noteH;
    noteH = Char_NoteHeight(g_char.sizeNote, fieldW);
    MoveWindow(g_char.sizeNote, x, y, fieldW, noteH, TRUE);
    y += Rs_Px(2) + noteH;
    // Compact: the hint that the size is a look only is the slider's tooltip
    // (Char_TabApply hides the line).
    Rs_SetTip(g_char.size, g_char.compact ? CHAR_SIZE_HINT_TEXT : NULL);
    if (g_char.compact) {
        y += Rs_Px(6);
    } else {
        noteH = Char_NoteHeight(g_char.sizeHint, fieldW);
        MoveWindow(g_char.sizeHint, x, y, fieldW, noteH, TRUE);
        y += Rs_Px(8) + noteH;
    }
    // The check boxes left to right, a new row where the next one does not fit.
    // The user mode (Rs_NativeForUsers): only Show kart wheels and the classic
    // fallback (ticked, greyed out, "Coming soon" right of it).
    count = 0;
    if (!Rs_NativeForUsers()) {
        options[count++] = g_char.repair;
        options[count++] = g_char.openParts;
        options[count++] = g_char.remesh;
        options[count++] = g_char.reduce;
    }
    options[count++] = g_char.wheels;
    if (Rs_NativeForUsers())
        options[count++] = g_char.fallback;
    MoveWindow(g_char.optionsLabel, in->left, y + Rs_Px(2), k->labelW, Rs_Px(20), TRUE);
    // Compact: the boxes (and "Coming soon") from the left edge of the card,
    // without the label (Char_TabApply hides it) - in the user mode one row.
    if (g_char.compact) {
        x = in->left;
        fieldW = in->right - in->left;
    }
    {
        int cx = x;
        for (i = 0; i < count; i++) {
            int bw = Char_CheckWidth(options[i]);
            int extra = options[i] == g_char.fallback ? Rs_Px(8) + Char_TextWidth(g_char.fallbackHint, L"Coming soon") + Rs_Px(4) : 0;
            if (bw > fieldW - extra)
                bw = fieldW - extra;
            if (cx > x && cx + bw + extra > x + fieldW) {
                cx = x;
                y += Rs_Px(26);
            }
            MoveWindow(options[i], cx, y, bw, Rs_Px(24), TRUE);
            if (extra)
                MoveWindow(g_char.fallbackHint, cx + bw + Rs_Px(8), y + Rs_Px(4), extra - Rs_Px(8), Rs_Px(18), TRUE);
            cx += bw + extra + Rs_Px(12);
        }
    }
    y += Rs_Px(24);
    // The user mode: what the native model came to, or why there is none.
    if (Rs_NativeForUsers()) {
        noteH = Char_NoteHeight(g_char.nativeLine, fieldW);
        MoveWindow(g_char.nativeLine, x, y + Rs_Px(4), fieldW, noteH, TRUE);
        return y + Rs_Px(4) + noteH;
    }
    // What the repair, the open parts and the remesh did (Char_ApplyQuality).
    MoveWindow(g_char.quality, x, y + Rs_Px(4), fieldW, Rs_Px(18), FALSE);
    qualityH = Char_QualityHeight(fieldW);
    g_char.qualityOn = qualityH > 0;
    if (qualityH > 0) {
        MoveWindow(g_char.quality, x, y + Rs_Px(4), fieldW, qualityH, TRUE);
        y += Rs_Px(4) + qualityH;
    }
    // Over the limit: the button that ticks Reduce to fit and checks again.
    if (g_char.reduceFitOn) {
        int bw = Char_TextWidth(g_char.reduceFit, L"Reduce to fit") + Rs_Px(32);
        MoveWindow(g_char.reduceFit, x, y + Rs_Px(6), bw < fieldW ? bw : fieldW, Rs_Px(30), TRUE);
        y += Rs_Px(36);
    }
    return y;
}

// Tab 1 with the list of what an OBJ brought along at least
// g_char.filesLeast rows tall, taller as far as the room down to in->bottom
// allows, never taller than its rows. Laid out with in->bottom above in->top
// it says how much it needs at least.
static int Char_LayModel(const struct CharLay *k, const RECT *in)
{
    int least, most, end;

    if (!g_char.filesOn || g_char.filesLeast == 0)
        return Char_LayModelAt(k, in, 0);
    least = Char_ImportListHeight(g_char.importRowCount < g_char.filesLeast ? g_char.importRowCount
                                                                             : g_char.filesLeast);
    most = Char_ImportListHeight(g_char.importRowCount);
    end = Char_LayModelAt(k, in, least);
    if (most > least && in->bottom > end) {
        int more = in->bottom - end;
        end = Char_LayModelAt(k, in, least + (more < most - least ? more : most - least));
    }
    return end;
}

// Tab 2 Driver: name, driving style, mask.
static int Char_LayDriver(const struct CharLay *k, const RECT *in)
{
    int x = in->left + k->labelW + Rs_Px(8), fieldW = in->right - x, y = in->top, noteH;

    Char_PlaceField(g_char.nameLabel, g_char.name, in->left, k->labelW, y, Rs_Px(220) < fieldW ? Rs_Px(220) : fieldW);
    y += Rs_Px(30);
    noteH = Char_NoteHeight(g_char.nameNote, fieldW);
    MoveWindow(g_char.nameNote, x, y, fieldW, noteH, TRUE);
    y += Rs_Px(14) + noteH;
    MoveWindow(g_char.classLabel, in->left, y + Rs_Px(4), k->labelW, Rs_Px(20), TRUE);
    MoveWindow(g_char.cls, x, y, fieldW, Rs_Px(300), TRUE);
    y += Rs_Px(30);
    noteH = Char_NoteHeight(g_char.classHelp, fieldW);
    MoveWindow(g_char.classHelp, x, y, fieldW, noteH, TRUE);
    y += Rs_Px(14) + noteH;
    MoveWindow(g_char.maskLabel, in->left, y + Rs_Px(4), k->labelW, Rs_Px(20), TRUE);
    MoveWindow(g_char.mask, x, y, fieldW, Rs_Px(300), TRUE);
    y += Rs_Px(30);
    noteH = Char_NoteHeight(g_char.maskHelp, fieldW);
    MoveWindow(g_char.maskHelp, x, y, fieldW, noteH, TRUE);
    return y + noteH;
}

// Tab 3 In-game look: icon with its options and pictures, minimap colour. The
// pictures as large as the room allows, at most 4 times.
static int Char_LayLookPortrait(const struct CharLay *k, const RECT *in)
{
    int x = in->left + k->labelW + Rs_Px(8), fieldW = in->right - x, width = in->right - in->left, y = in->top;
    int factor, boxW, boxH, mapH = 0, i;

    Char_PlaceField(g_char.iconLabel, g_char.icon, in->left, k->labelW, y + Rs_Px(2),
                    fieldW - k->browseW - k->clearW - Rs_Px(16));
    MoveWindow(g_char.iconBrowse, in->right - k->browseW - k->clearW - Rs_Px(8), y, k->browseW, Rs_Px(32), TRUE);
    MoveWindow(g_char.iconClear, in->right - k->clearW, y, k->clearW, Rs_Px(32), TRUE);
    y += Rs_Px(40);
    // The framing below the field, the two options in a row beside it where
    // they fit, else below it.
    MoveWindow(g_char.iconFitLabel, in->left, y + Rs_Px(4), k->labelW, Rs_Px(20), TRUE);
    boxW = Char_ComboTextWidth(g_char.iconFit) + Rs_Px(28);
    if (boxW > fieldW)
        boxW = fieldW;
    MoveWindow(g_char.iconFit, x, y, boxW, Rs_Px(300), TRUE);
    {
        HWND boxes[2];
        int cx = x + boxW + Rs_Px(16);
        boxes[0] = g_char.iconCorners;
        boxes[1] = g_char.iconFrame;
        if (cx + Char_CheckWidth(boxes[0]) > x + fieldW) {
            cx = x;
            y += Rs_Px(36);
        } else {
            y += Rs_Px(3);
        }
        for (i = 0; i < 2; i++) {
            int bw = Char_CheckWidth(boxes[i]);
            if (bw > fieldW)
                bw = fieldW;
            if (cx > x && cx + bw > x + fieldW) {
                cx = x;
                y += Rs_Px(26);
            }
            MoveWindow(boxes[i], cx, y, bw, Rs_Px(24), TRUE);
            cx += bw + Rs_Px(12);
        }
    }
    y += Rs_Px(24) + Rs_Px(g_char.compact ? 6 : 14);
    // Whole steps, at most 4: your picture, then the game picture (two
    // portraits wide); the first column as wide as its caption needs.
    {
        wchar_t *text = Rs_GetText(g_char.iconCaption[CHAR_IMG_ORIGINAL]);
        int capW = Char_TextWidth(g_char.iconCaption[CHAR_IMG_ORIGINAL], text) + Rs_Px(4);
        int firstW = 0, gameW = 0, bx;
        Rs_Free(text);
        for (factor = 4; factor >= 1; factor--) {
            boxW = Rs_Px(CHAR_ICON_W * factor + 8);
            firstW = boxW > capW ? boxW : capW;
            gameW = Rs_Px(CHAR_GAME_W * factor + 8);
            boxH = Rs_Px(CHAR_GAME_H * factor + 8);
            if (factor == 1 || (firstW + k->gap + gameW <= width && y + Rs_Px(20) + boxH + mapH <= in->bottom))
                break;
        }
        MoveWindow(g_char.iconCaption[CHAR_IMG_ORIGINAL], in->left, y, firstW + k->gap, Rs_Px(18), TRUE);
        MoveWindow(g_char.iconImage[CHAR_IMG_ORIGINAL], in->left, y + Rs_Px(20), boxW, boxH, TRUE);
        // The last caption may run on to the edge of the card.
        bx = in->left + firstW + k->gap;
        MoveWindow(g_char.iconCaption[CHAR_IMG_ICON], bx, y, in->right - bx, Rs_Px(18), TRUE);
        MoveWindow(g_char.iconImage[CHAR_IMG_ICON], bx, y + Rs_Px(20), gameW, boxH, TRUE);
    }
    return y + Rs_Px(20) + boxH;
}

// Tab 3 In-game look, card In the race: the minimap colour, the shadow and
// the exhaust (preview feature), the two points with Pick, a note.
static int Char_LayLookRace(const struct CharLay *k, const RECT *in)
{
    int x = in->left + k->labelW + Rs_Px(8), fieldW = in->right - x, y = in->top, noteH, i, a;
    const int rowH = Rs_Px(g_char.compact ? 28 : 36);

    MoveWindow(g_char.mapLabel, in->left, y + Rs_Px(6), k->labelW, Rs_Px(20), TRUE);
    MoveWindow(g_char.mapSwatch, x, y, Rs_Px(48), Rs_Px(32), TRUE);
    {
        int pickW = Char_CheckWidth(g_char.mapPick) + Rs_Px(8);
        int likeW = Char_CheckWidth(g_char.mapLike) + Rs_Px(8);
        MoveWindow(g_char.mapPick, x + Rs_Px(56), y, pickW, Rs_Px(32), TRUE);
        MoveWindow(g_char.mapLike, x + Rs_Px(64) + pickW, y, likeW, Rs_Px(32), TRUE);
    }
    y += Rs_Px(g_char.compact ? 32 : 36);
    noteH = Char_NoteHeight(g_char.mapHelp, fieldW);
    MoveWindow(g_char.mapHelp, x, y, fieldW, noteH, TRUE);
    y += noteH + Rs_Px(g_char.compact ? 2 : 14);
    // "Coming soon" / "Preview feature" right on the row Shadow: it is about
    // shadow and exhaust, not about the minimap colour above, which works
    // without the switch too.
    {
        const int rowTop = y;
        int hintW = Char_TextWidth(g_char.lookHint, L"Preview feature"), h;
        h = Char_TextWidth(g_char.lookHint, L"Coming soon");
        hintW = (h > hintW ? h : hintW) + Rs_Px(4);
        y = Char_LayChoice(k, in, y, g_char.shadowLabel, g_char.shadow, g_char.shadowHelp);
        MoveWindow(g_char.lookHint, in->right - hintW, rowTop + Rs_Px(5), hintW, Rs_Px(18), TRUE);
        {
            RECT hr;
            GetWindowRect(g_char.shadowHelp, &hr);
            MapWindowPoints(NULL, GetParent(g_char.shadowHelp), (POINT *)&hr, 2);
            if (hr.right > in->right - hintW - Rs_Px(8)) {
                const int helpW = in->right - hintW - Rs_Px(8) - hr.left;
                h = Char_TextHeight(g_char.shadowHelp, helpW, 2);
                MoveWindow(g_char.shadowHelp, hr.left, hr.top, helpW, h, TRUE);
                if (hr.top + h + Rs_Px(8) > y)
                    y = hr.top + h + Rs_Px(8);
            }
        }
    }
    y = Char_LayChoice(k, in, y, g_char.exhaustLabel, g_char.exhaust, g_char.exhaustHelp);
    for (i = 0; i < 2; i++) {
        int ex = x, ew = Rs_Px(72), pw = Char_TextWidth(g_char.pointPick[i], L"Pick") + Rs_Px(32);
        MoveWindow(g_char.pointLabel[i], in->left, y + Rs_Px(4), k->labelW, Rs_Px(20), TRUE);
        for (a = 0; a < 3; a++) {
            MoveWindow(g_char.point[i][a], ex, y, ew, Rs_Px(28), TRUE);
            ex += ew + Rs_Px(6);
        }
        MoveWindow(g_char.pointPick[i], ex + Rs_Px(6), y - Rs_Px(1), pw, Rs_Px(g_char.compact ? 28 : 30), TRUE);
        y += rowH;
    }
    noteH = Char_NoteHeight(g_char.lookNote, in->right - in->left);
    MoveWindow(g_char.lookNote, in->left, y, in->right - in->left, noteH, TRUE);
    return y + noteH;
}

// Portrait | In the race in the title line of the tab In-game look.
static void Char_LayLookSwitch(const RECT *card)
{
    int x = card->left + Rs_Px(10), c;
    for (c = 0; c < CHAR_LOOK_CARDS; c++) {
        int bw = Char_TextWidth(g_char.lookSwitch[c], g_charLookTexts[c]) + Rs_Px(20);
        MoveWindow(g_char.lookSwitch[c], x, card->top + Rs_Px(8), bw, Rs_Px(36), TRUE);
        x += bw + Rs_Px(4);
    }
}

// Tab 4 Voices: the folder, the line of what was found, the list of its files
// (as tall as the room allows, at least CHAR_VOICE_LIST_MIN_H), below it the
// event of the file chosen with Play and "Normalize volume", the ten events,
// the line with the file names. Laid out with in->bottom at in->top it says
// how much it needs at least.
static int Char_LayVoices(const struct CharLay *k, const RECT *in)
{
    int x = in->left + k->labelW + Rs_Px(8), fieldW = in->right - x, width = in->right - in->left, y = in->top;
    int noteH, ruleH, listH, eventsH, comboW, playW, normW, rowH, tail;

    Char_PlaceField(g_char.voicesLabel, g_char.voices, in->left, k->labelW, y + Rs_Px(2),
                    fieldW - k->browseW - k->clearW - Rs_Px(16));
    MoveWindow(g_char.voicesBrowse, in->right - k->browseW - k->clearW - Rs_Px(8), y, k->browseW, Rs_Px(32), TRUE);
    MoveWindow(g_char.voicesClear, in->right - k->clearW, y, k->clearW, Rs_Px(32), TRUE);
    y += Rs_Px(36);
    noteH = Char_NoteHeight(g_char.voicesNote, fieldW);
    MoveWindow(g_char.voicesNote, x, y, fieldW, noteH, TRUE);
    y += noteH + Rs_Px(8);

    // The row below the list: Event, Play, Normalize volume - the check box on
    // a row of its own where it does not fit beside them.
    comboW = Char_ComboTextWidth(g_char.voiceEvent) + Rs_Px(28);
    playW = Char_TextWidth(g_char.voicePlay, L"Play") + Rs_Px(40);
    normW = Char_CheckWidth(g_char.voiceNorm);
    if (comboW > fieldW - playW - Rs_Px(8))
        comboW = fieldW - playW - Rs_Px(8);
    // Compact: the list of events narrower (its drop-down keeps its width),
    // so that Normalize volume stays on the row.
    if (g_char.compact && x + comboW + Rs_Px(8) + playW + Rs_Px(16) + normW > in->right) {
        comboW = in->right - x - Rs_Px(8) - playW - Rs_Px(16) - normW;
        if (comboW < Rs_Px(100))
            comboW = Rs_Px(100);
    }
    rowH = Rs_Px(32);
    if (x + comboW + Rs_Px(8) + playW + Rs_Px(16) + normW > in->right)
        rowH += Rs_Px(36);
    eventsH = Rs_Px(2 * CHAR_VOICE_CELL_H);
    ruleH = Char_NoteHeight(g_char.voiceRule, width);
    tail = Rs_Px(8) + rowH + Rs_Px(g_char.compact ? 6 : 10) + eventsH + Rs_Px(g_char.compact ? 4 : 8) + ruleH;

    listH = in->bottom - tail - y;
    if (listH < Rs_Px(g_char.compact ? CHAR_VOICE_LIST_MIN_H * 5 / 12 : CHAR_VOICE_LIST_MIN_H))
        listH = Rs_Px(g_char.compact ? CHAR_VOICE_LIST_MIN_H * 5 / 12 : CHAR_VOICE_LIST_MIN_H);
    MoveWindow(g_char.voiceList, in->left, y, width, listH, TRUE);
    Char_VoiceColumns(width);
    y += listH + Rs_Px(8);

    MoveWindow(g_char.voiceEventLabel, in->left, y + Rs_Px(6), k->labelW, Rs_Px(20), TRUE);
    MoveWindow(g_char.voiceEvent, x, y + Rs_Px(1), comboW, Rs_Px(300), TRUE);
    MoveWindow(g_char.voicePlay, x + comboW + Rs_Px(8), y, playW, Rs_Px(30), TRUE);
    if (rowH > Rs_Px(32))
        MoveWindow(g_char.voiceNorm, x, y + Rs_Px(42), normW < fieldW ? normW : fieldW, Rs_Px(24), TRUE);
    else
        MoveWindow(g_char.voiceNorm, in->right - normW, y + Rs_Px(3), normW, Rs_Px(24), TRUE);
    y += rowH + Rs_Px(g_char.compact ? 6 : 10);

    MoveWindow(g_char.voiceEventsLabel, in->left, y + Rs_Px(1), k->labelW, Rs_Px(20), TRUE);
    MoveWindow(g_char.voiceEvents, x, y, fieldW, eventsH, TRUE);
    y += eventsH + Rs_Px(g_char.compact ? 4 : 8);
    MoveWindow(g_char.voiceRule, in->left, y, width, ruleH, TRUE);
    return y + ruleH;
}

// One choice of the card Import: label, list as wide as its longest entry,
// the note beside it (two lines at most) or, where that leaves too little,
// below it. Returns the top of the next row.
static int Char_LayChoice(const struct CharLay *k, const RECT *in, int y, HWND label, HWND combo, HWND help)
{
    int x = in->left + k->labelW + Rs_Px(8), fieldW = in->right - x;
    int comboW = Char_ComboTextWidth(combo) + Rs_Px(28), helpX, helpW, h;

    if (comboW > fieldW)
        comboW = fieldW;
    MoveWindow(label, in->left, y + Rs_Px(4), k->labelW, Rs_Px(20), TRUE);
    MoveWindow(combo, x, y, comboW, Rs_Px(300), TRUE);
    helpX = x + comboW + Rs_Px(12);
    helpW = in->right - helpX;
    if (helpW >= Rs_Px(180)) {
        h = Char_TextHeight(help, helpW, 2);
        MoveWindow(help, helpX, y + Rs_Px(5), helpW, h, TRUE);
        h += Rs_Px(5);
        return y + (h > Rs_Px(30) ? h : Rs_Px(30)) + Rs_Px(g_char.compact ? 2 : 8);
    }
    h = Char_TextHeight(help, fieldW, 2);
    MoveWindow(help, x, y + Rs_Px(30), fieldW, h, TRUE);
    return y + Rs_Px(30) + h + Rs_Px(g_char.compact ? 2 : 8);
}

// The row "Native model" of the card Import: label, tick box, the hint at the
// right (the same box with and without the switch), the note below the box.
// Returns the top of the next row.
static int Char_LayNative(const struct CharLay *k, const RECT *in, int y)
{
    int x = in->left + k->labelW + Rs_Px(8), fieldW = in->right - x, hintW, boxW, h;

    hintW = Char_TextWidth(g_char.nativeHint, L"Coming soon");
    h = Char_TextWidth(g_char.nativeHint, L"Preview feature");
    hintW = (h > hintW ? h : hintW) + Rs_Px(4);
    boxW = Rs_CheckBoxWidth(g_char.native);
    if (boxW > fieldW - hintW - Rs_Px(8))
        boxW = fieldW - hintW - Rs_Px(8);
    if (boxW < Rs_Px(40))
        boxW = Rs_Px(40);
    MoveWindow(g_char.nativeLabel, in->left, y + Rs_Px(3), k->labelW, Rs_Px(20), TRUE);
    MoveWindow(g_char.native, x, y, boxW, Rs_Px(24), TRUE);
    MoveWindow(g_char.nativeHint, in->right - hintW, y + Rs_Px(3), hintW, Rs_Px(18), TRUE);
    // The user mode has no tick box (Char_UserHidden): the note in its place.
    if (Rs_NativeForUsers()) {
        h = Char_TextHeight(g_char.nativeHelp, fieldW - hintW - Rs_Px(8), 3);
        MoveWindow(g_char.nativeHelp, x, y + Rs_Px(4), fieldW - hintW - Rs_Px(8), h, TRUE);
        h += Rs_Px(4);
        return y + (h > Rs_Px(24) ? h : Rs_Px(24)) + Rs_Px(8);
    }
    y += Rs_Px(g_char.compact ? 26 : 28);
    // Compact: the note on one line, the whole text its tooltip (Char_TextHeight).
    h = Char_TextHeight(g_char.nativeHelp, fieldW, 3);
    MoveWindow(g_char.nativeHelp, x, y, fieldW, h, TRUE);
    return y + h + Rs_Px(g_char.compact ? 2 : 8);
}

// Tab 5 Extras, the card Import: a note, then the choices (those of an OBJ
// only for an OBJ). Returns the bottom of the card.
static int Char_LayImport(const struct CharLay *k, int left, int right, int top)
{
    RECT card, in;
    int y, noteH;

    card.left = left;
    card.top = top;
    card.right = right;
    card.bottom = top;
    in = Rs_CardInner(&card, 1);
    y = in.top;
    noteH = Char_TextHeight(g_char.importNote, in.right - in.left, CHAR_NOTE_LINES);
    MoveWindow(g_char.importNote, in.left, y, in.right - in.left, noteH, TRUE);
    y += noteH + Rs_Px(g_char.compact ? 6 : 12);
    y = Char_LayChoice(k, &in, y, g_char.upLabel, g_char.up, g_char.upHelp);
    y = Char_LayChoice(k, &in, y, g_char.forwardLabel, g_char.forward, g_char.forwardHelp);
    if (!Rs_NativeForUsers())
        y = Char_LayChoice(k, &in, y, g_char.colorsLabel, g_char.colors, g_char.colorsHelp);
    if (g_char.objOn)
        y = Char_LayChoice(k, &in, y, g_char.vcolorsLabel, g_char.vcolors, g_char.vcolorsHelp);
    y = Char_LayNative(k, &in, y);
    return y + Rs_Px(16);
}

// Tab 5 Extras: Import | Wheels | Animations in the title line of the card
// (the card itself has no title), the card Import, or that of rs_wheels.c
// or rs_anim.c, below it.
static void Char_LayExtrasSwitch(const RECT *card)
{
    int x = card->left + Rs_Px(10), c;
    for (c = 0; c < CHAR_EXTRAS_COUNT; c++) {
        int bw = Char_TextWidth(g_char.extrasSwitch[c], g_charExtrasTexts[c]) + Rs_Px(20);
        MoveWindow(g_char.extrasSwitch[c], x, card->top + Rs_Px(8), bw, Rs_Px(36), TRUE);
        x += bw + Rs_Px(4);
    }
}

// The card "Preview" from top to bottom: the 3D view as tall as the card
// allows. The pose choice is in the title line of the card.
static void Char_LayPreview(HWND page, int left, int right, int top, int bottom)
{
    RECT card, in;
    int width, viewBottom, noteH;

    card.left = left;
    card.top = top;
    card.right = right;
    card.bottom = bottom;
    // Compact: no title line - the pose choice on a row of its own, the note
    // on one line below the view.
    if (g_char.compact) {
        int poseW = Rs_Px(150);
        in = Rs_CardInner(&card, 0);
        width = in.right - in.left;
        if (poseW > width)
            poseW = width;
        MoveWindow(g_char.pose, in.right - poseW, in.top, poseW, Rs_Px(200), TRUE);
        Char_ViewNoteFit(width);
        noteH = Char_NoteHeight(g_char.viewNote, width);
        viewBottom = in.bottom - Rs_Px(4) - noteH;
        if (viewBottom < in.top + Rs_Px(32) + Rs_Px(CHAR_VIEW_MIN_H))
            viewBottom = in.top + Rs_Px(32) + Rs_Px(CHAR_VIEW_MIN_H);
        MoveWindow(g_char.view, in.left, in.top + Rs_Px(32), width, viewBottom - in.top - Rs_Px(32), TRUE);
        MoveWindow(g_char.viewNote, in.left, viewBottom + Rs_Px(4), width, noteH, TRUE);
        if (card.bottom < viewBottom + Rs_Px(4) + noteH + Rs_Px(16))
            card.bottom = viewBottom + Rs_Px(4) + noteH + Rs_Px(16);
        Rs_CardAdd(page, &card, NULL);
        return;
    }
    in = Rs_CardInner(&card, 1);
    width = in.right - in.left;
    MoveWindow(g_char.pose, in.right - Rs_Px(168), card.top + Rs_Px(10), Rs_Px(168), Rs_Px(200), TRUE);
    Char_ViewNoteFit(width);
    noteH = Char_NoteHeight(g_char.viewNote, width);
    viewBottom = in.bottom - Rs_Px(6) - noteH;
    if (viewBottom < in.top + Rs_Px(CHAR_VIEW_MIN_H))
        viewBottom = in.top + Rs_Px(CHAR_VIEW_MIN_H);
    MoveWindow(g_char.view, in.left, in.top, width, viewBottom - in.top, TRUE);
    MoveWindow(g_char.viewNote, in.left, viewBottom + Rs_Px(6), width, noteH, TRUE);
    if (card.bottom < viewBottom + Rs_Px(6) + noteH + Rs_Px(16))
        card.bottom = viewBottom + Rs_Px(6) + noteH + Rs_Px(16);
    Rs_CardAdd(page, &card, L"Preview");
}

// The compact bar (Char_Layout): row 1 the headline (one line, the rest as its
// tooltip), the progress and Cancel while rldpack runs, Output, Browse and the
// toggle of the raw output; row 2 the message list (or the raw output), Show
// in folder after a build, Check and Build. No Back and Next. Returns its bottom.
static int Char_LayBarCompact(HWND page, const struct CharLay *k, RECT *card, const RECT *in, int rawW)
{
    int y2 = in->top + Rs_Px(26), rowH = in->bottom - y2, labelW, fieldW = Rs_Px(200), x, headR, buildW = Rs_Px(140), checkW = Rs_Px(84);
    int showW = g_char.built[0] ? Rs_Px(136) : 0, listR;
    wchar_t *text;

    if (rowH < Rs_Px(28))
        rowH = Rs_Px(28);
    text = Rs_GetText(g_char.outLabel);
    labelW = Char_TextWidth(g_char.outLabel, text) + Rs_Px(4);
    Rs_Free(text);
    MoveWindow(g_char.rawToggle, in->right - rawW, in->top, rawW, Rs_Px(24), TRUE);
    x = in->right - rawW - Rs_Px(12) - k->browseW;
    MoveWindow(g_char.outBrowse, x, in->top - Rs_Px(3), k->browseW, Rs_Px(30), TRUE);
    x -= Rs_Px(8) + fieldW;
    MoveWindow(g_char.out, x, in->top - Rs_Px(2), fieldW, Rs_Px(28), TRUE);
    x -= Rs_Px(8) + labelW;
    MoveWindow(g_char.outLabel, x, in->top + Rs_Px(2), labelW, Rs_Px(20), TRUE);
    headR = x - Rs_Px(12);
    if (g_char.progressOn) {
        int cancelW = Char_TextWidth(g_char.cancel, L"Cancel") + Rs_Px(32);
        int progW = Rs_Px(120);
        MoveWindow(g_char.cancel, headR - cancelW, in->top, cancelW, Rs_Px(24), TRUE);
        MoveWindow(g_char.progress, headR - cancelW - Rs_Px(8) - progW, in->top + Rs_Px(3), progW, Rs_Px(18), TRUE);
        headR -= cancelW + progW + Rs_Px(20);
    }
    ShowWindow(g_char.cancel, g_char.progressOn ? SW_SHOWNA : SW_HIDE);
    ShowWindow(g_char.progress, g_char.progressOn ? SW_SHOWNA : SW_HIDE);
    if (headR < in->left + Rs_Px(40))
        headR = in->left + Rs_Px(40);
    MoveWindow(g_char.headline, in->left, in->top + Rs_Px(2), headR - in->left, Rs_Px(22), TRUE);

    MoveWindow(g_char.build, in->right - buildW, y2, buildW, Rs_Px(28), TRUE);
    MoveWindow(g_char.check, in->right - buildW - Rs_Px(8) - checkW, y2, checkW, Rs_Px(28), TRUE);
    listR = in->right - buildW - Rs_Px(8) - checkW - Rs_Px(12);
    if (showW) {
        MoveWindow(g_char.show, listR - showW, y2, showW, Rs_Px(28), TRUE);
        listR -= showW + Rs_Px(8);
    }
    ShowWindow(g_char.show, showW ? SW_SHOW : SW_HIDE);
    ShowWindow(g_char.back, SW_HIDE);
    ShowWindow(g_char.next, SW_HIDE);
    MoveWindow(g_char.msgs, in->left, y2, listR - in->left, rowH, TRUE);
    MoveWindow(g_char.raw, in->left, y2, listR - in->left, rowH, TRUE);
    card->bottom = y2 + rowH + Rs_Px(6);
    Rs_CardAdd(page, card, NULL);
    return card->bottom;
}

// The bar from top to bottom over the whole width: the headline with the
// toggle of the raw output at the top, below it the message list on the left
// and on the right output (Browse, Show in folder) and the buttons Back, Next,
// Check and Build. Returns its bottom.
static int Char_LayBar(HWND page, const struct CharLay *k, int left, int right, int top, int bottom)
{
    RECT card, in;
    int width, rawW, headW, y, rightW, rx, labelW, fieldW, listBottom, showW;
    wchar_t *text;

    card.left = left;
    card.top = top;
    card.right = right;
    card.bottom = bottom;
    in.left = left + Rs_Px(18);
    in.right = right - Rs_Px(18);
    in.top = top + Rs_Px(6);
    in.bottom = bottom - Rs_Px(6);
    width = in.right - in.left;
    rawW = Char_TextWidth(g_char.rawToggle, L"Show rldpack output") + Rs_Px(28);
    headW = width - rawW - Rs_Px(12);
    if (g_char.compact)
        return Char_LayBarCompact(page, k, &card, &in, rawW);
    // While rldpack runs: the bar of its progress and Cancel, left of the
    // toggle; the headline gives them its room.
    if (g_char.progressOn) {
        int cancelW = Char_TextWidth(g_char.cancel, L"Cancel") + Rs_Px(32);
        int progW = Rs_Px(160);
        int cx = in.right - rawW - Rs_Px(12) - cancelW;
        MoveWindow(g_char.cancel, cx, in.top, cancelW, Rs_Px(24), TRUE);
        MoveWindow(g_char.progress, cx - Rs_Px(8) - progW, in.top + Rs_Px(3), progW, Rs_Px(18), TRUE);
        headW -= cancelW + progW + Rs_Px(20);
    }
    ShowWindow(g_char.cancel, g_char.progressOn ? SW_SHOWNA : SW_HIDE);
    ShowWindow(g_char.progress, g_char.progressOn ? SW_SHOWNA : SW_HIDE);
    MoveWindow(g_char.headline, in.left, in.top + Rs_Px(2), headW, Rs_Px(22), TRUE);
    MoveWindow(g_char.rawToggle, in.right - rawW, in.top, rawW, Rs_Px(24), TRUE);
    y = in.top + Rs_Px(24) + Rs_Px(4);

    // Right: output and buttons.
    rightW = width * 46 / 100;
    if (rightW < Rs_Px(440))
        rightW = Rs_Px(440);
    if (rightW > Rs_Px(560))
        rightW = Rs_Px(560);
    rx = in.right - rightW;
    text = Rs_GetText(g_char.outLabel);
    labelW = Char_TextWidth(g_char.outLabel, text) + Rs_Px(4);
    Rs_Free(text);
    showW = g_char.built[0] ? Rs_Px(136) + Rs_Px(8) : 0;
    fieldW = rightW - labelW - Rs_Px(8) - k->browseW - Rs_Px(8) - showW;
    MoveWindow(g_char.outLabel, rx, y + Rs_Px(5), labelW, Rs_Px(20), TRUE);
    MoveWindow(g_char.out, rx + labelW + Rs_Px(8), y + Rs_Px(1), fieldW, Rs_Px(28), TRUE);
    MoveWindow(g_char.outBrowse, rx + labelW + Rs_Px(8) + fieldW + Rs_Px(8), y, k->browseW, Rs_Px(30), TRUE);
    MoveWindow(g_char.show, in.right - Rs_Px(136), y, Rs_Px(136), Rs_Px(30), TRUE);
    ShowWindow(g_char.show, g_char.built[0] ? SW_SHOW : SW_HIDE);
    MoveWindow(g_char.back, rx, y + Rs_Px(34), Rs_Px(84), Rs_Px(30), TRUE);
    MoveWindow(g_char.next, rx + Rs_Px(92), y + Rs_Px(34), Rs_Px(84), Rs_Px(30), TRUE);
    if (!Char_IsShown(g_char.back))
        ShowWindow(g_char.back, SW_SHOWNA);
    if (!Char_IsShown(g_char.next))
        ShowWindow(g_char.next, SW_SHOWNA);

    MoveWindow(g_char.build, in.right - Rs_Px(156), y + Rs_Px(34), Rs_Px(156), Rs_Px(30), TRUE);
    MoveWindow(g_char.check, in.right - Rs_Px(156 + 8 + 96), y + Rs_Px(34), Rs_Px(96), Rs_Px(30), TRUE);

    // Left: the messages (or the raw output), down to the bottom.
    listBottom = in.bottom;
    if (listBottom < y + Rs_Px(64))
        listBottom = y + Rs_Px(64);
    MoveWindow(g_char.msgs, in.left, y, rx - Rs_Px(16) - in.left, listBottom - y, TRUE);
    MoveWindow(g_char.raw, in.left, y, rx - Rs_Px(16) - in.left, listBottom - y, TRUE);
    card.bottom = listBottom + Rs_Px(6);
    Rs_CardAdd(page, &card, NULL);
    return card.bottom;
}

// Shows the controls of the tab (and of the card of the tab Extras) chosen,
// hides those of the others. Heads, preview and bar stay as they are.
static void Char_TabApply(HWND page)
{
    HWND c;
    for (c = GetWindow(page, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        int card, tab = Char_TabOfId(GetDlgCtrlID(c), &card), want;
        if (tab < 0)
            continue;
        want = tab == g_char.tab && (card < 0 || card == (tab == CHAR_TAB_LOOK ? g_char.lookCard : g_char.extrasCard)) &&
               !Char_UserHidden(c);
        if (c == g_char.reduceFit)
            want = want && g_char.reduceFitOn;
        else if (c == g_char.quality)
            want = want && g_char.qualityOn;
        else if (c == g_char.sizeHint || c == g_char.optionsLabel)
            want = want && !g_char.compact;
        else if (c == g_char.modelImport)
            want = want && g_char.importOn;
        else if (c == g_char.modelFiles)
            want = want && g_char.filesOn && g_char.filesLeast > 0;
        else if (c == g_char.vcolorsLabel || c == g_char.vcolors || c == g_char.vcolorsHelp)
            want = want && g_char.objOn;
        else if (c == g_char.texturesLabel || c == g_char.textures || c == g_char.texturesBrowse ||
                 c == g_char.texturesClear)
            want = want && g_char.texturesOn;
        if (want != Char_IsShown(c))
            ShowWindow(c, want ? SW_SHOWNA : SW_HIDE);
    }
}

static void Char_Layout(HWND page, int w, int h)
{
    struct CharLay k;
    // The compact layout (CHAR_FULL_W, CHAR_FULL_H) first: it decides the heading.
    const int compact = w < Rs_Px(CHAR_FULL_W) || h < Rs_Px(CHAR_FULL_H);
    int left, right = w - Rs_Px(32), top, bottom = h - Rs_Px(24);
    int vgap = Rs_Px(compact ? 6 : 12), avail, leftW, least, comboW, contentTop, extrasBottom[CHAR_EXTRAS_COUNT];
    int barH, barTop, upperBottom, need, t, i, viewMinW = Rs_Px(compact ? CHAR_COMPACT_VIEW_MIN_W : CHAR_VIEW_MIN_W);
    RECT card, in;
    HWND column[19];

    g_char.compact = compact;
    Rs_PageSetCompactHead(page, compact);
    left = Rs_Px(compact ? 16 : 32);
    // The page has no subtitle: the cards start right below its title.
    top = Rs_PageHeadBottom(page, w) + Rs_Px(compact ? 2 : 8);
    Rs_CardClear(page);
    k.gap = Rs_Px(16);
    k.browseW = Rs_Px(100);
    k.clearW = Rs_Px(72);
    k.labelW = Rs_Px(112);

    // Nothing is cut short: the label column is as wide as its widest label,
    // and the tabs hold the longest entry of either combo.
    column[0] = g_char.modelLabel;
    column[1] = g_char.nameLabel;
    column[2] = g_char.classLabel;
    column[3] = g_char.maskLabel;
    column[4] = g_char.sizeLabel;
    column[5] = g_char.sizeCrash;
    column[6] = g_char.optionsLabel;
    column[7] = g_char.iconLabel;
    column[8] = g_char.voicesLabel;
    column[9] = g_char.mapLabel;
    column[10] = g_char.iconFitLabel;
    column[11] = g_char.voiceEventLabel;
    column[12] = g_char.voiceEventsLabel;
    column[13] = g_char.vcolorsLabel;
    column[14] = g_char.upLabel;
    column[15] = g_char.forwardLabel;
    column[16] = g_char.colorsLabel;
    column[17] = g_char.texturesLabel;
    column[18] = g_char.nativeLabel;
    for (i = 0; i < 19; i++) {
        wchar_t *text = Rs_GetText(column[i]);
        int tw = Char_TextWidth(column[i], text) + Rs_Px(4);
        if (tw > k.labelW)
            k.labelW = tw;
        Rs_Free(text);
    }
    comboW = Char_ComboTextWidth(g_char.cls);
    SendMessageW(g_char.cls, CB_SETDROPPEDWIDTH, (WPARAM)(comboW + Rs_Px(16)), 0);
    i = Char_ComboTextWidth(g_char.mask);
    SendMessageW(g_char.mask, CB_SETDROPPEDWIDTH, (WPARAM)(i + Rs_Px(16)), 0);
    SendMessageW(g_char.iconFit, CB_SETDROPPEDWIDTH, (WPARAM)(Char_ComboTextWidth(g_char.iconFit) + Rs_Px(16)), 0);
    SendMessageW(g_char.voiceEvent, CB_SETDROPPEDWIDTH, (WPARAM)(Char_ComboTextWidth(g_char.voiceEvent) + Rs_Px(16)), 0);
    SendMessageW(g_char.vcolors, CB_SETDROPPEDWIDTH, (WPARAM)(Char_ComboTextWidth(g_char.vcolors) + Rs_Px(16)), 0);
    if (i > comboW)
        comboW = i;
    comboW += Rs_Px(28);        // the margins and the arrow

    // The columns: the tabs about 58 % (CHAR_LEFT_MIN_W..CHAR_LEFT_MAX_W, and
    // as wide as the label column and the combos need), the preview the rest.
    if (compact)
        k.gap = Rs_Px(12);
    avail = right - left - k.gap;
    least = 2 * Rs_Px(18) + k.labelW + Rs_Px(8) + comboW;
    if (least < Rs_Px(CHAR_LEFT_MIN_W))
        least = Rs_Px(CHAR_LEFT_MIN_W);
    leftW = avail * 58 / 100;
    if (leftW > Rs_Px(CHAR_LEFT_MAX_W))
        leftW = Rs_Px(CHAR_LEFT_MAX_W);
    if (avail - leftW < viewMinW)
        leftW = avail - viewMinW;
    if (leftW < least)
        leftW = least;
    if (right < left + leftW + k.gap + viewMinW)
        right = left + leftW + k.gap + viewMinW;    // the page grows and scrolls sideways

    contentTop = Char_LayStrip(left, left + leftW, top);

    // The cards of the tab Extras lay themselves out (and report their cards,
    // taken back here: the page reports the card of the tab shown below).
    extrasBottom[CHAR_EXTRAS_WHEELS] = CharWheels_Layout(page, left, left + leftW, contentTop, k.labelW, compact);
    extrasBottom[CHAR_EXTRAS_ANIM] = CharAnim_Layout(page, left, left + leftW, contentTop, k.labelW, compact);
    Rs_CardClear(page);
    extrasBottom[CHAR_EXTRAS_IMPORT] = Char_LayImport(&k, left, left + leftW, contentTop);
    need = 0;
    for (i = 0; i < CHAR_EXTRAS_COUNT; i++)
        if (extrasBottom[i] > need)
            need = extrasBottom[i];

    // Every tab is laid out (the hidden ones too, so that their notes have
    // their width), and the tallest sets the room above the bar for all of
    // them - In-game look and Voices last: their pictures and their list take
    // the room there is (Model and Voices here with the least height of
    // their lists).
    card.left = left;
    card.top = contentTop;
    card.right = left + leftW;
    card.bottom = contentTop;
    in = Rs_CardInner(&card, 0);
    // The list of Model shows one row less where it would push the bar
    // below its least height; the compact layout has no room for it (the
    // line above it says what was found).
    g_char.filesLeast = compact ? 0 : CHAR_IMPORT_MIN_ROWS;
    for (t = 0; t < CHAR_TAB_EXTRAS; t++) {
        int end;
        if (t == CHAR_TAB_LOOK) {
            // Its card In the race has nothing that gives way (the pictures
            // of Portrait do); below the title line of its switch.
            const RECT inLook = Rs_CardInner(&card, 1);
            end = Char_LayLookRace(&k, &inLook) + Rs_Px(16);
            if (end > need)
                need = end;
            continue;
        }
        end = (t == CHAR_TAB_MODEL ? Char_LayModel(&k, &in) : t == CHAR_TAB_DRIVER ? Char_LayDriver(&k, &in)
                                                                                 : Char_LayVoices(&k, &in)) + Rs_Px(16);
        if (t == CHAR_TAB_MODEL && g_char.filesOn && g_char.filesLeast > 0 && bottom - end - vgap < Rs_Px(CHAR_BAR_MIN_H)) {
            g_char.filesLeast = 1;
            end = Char_LayModel(&k, &in) + Rs_Px(16);
            // Still too tall (the field Textures folder below it): no list -
            // the line above it says what was found.
            if (bottom - end - vgap < Rs_Px(CHAR_BAR_MIN_H)) {
                g_char.filesLeast = 0;
                end = Char_LayModel(&k, &in) + Rs_Px(16);
            }
        }
        if (end > need)
            need = end;
    }
    barH = bottom - need - vgap;
    if (barH > Rs_Px(CHAR_BAR_MAX_H))
        barH = Rs_Px(CHAR_BAR_MAX_H);
    if (barH < Rs_Px(CHAR_BAR_MIN_H))
        barH = Rs_Px(CHAR_BAR_MIN_H);
    if (compact)
        barH = Rs_Px(CHAR_COMPACT_BAR_H);
    barTop = bottom - barH;
    if (barTop < need + vgap)
        barTop = need + vgap;
    upperBottom = barTop - vgap;

    // Model, In-game look and Voices in the room above the bar; only the
    // smallest pictures may still push the bar lower (the lists of Model and
    // Voices never do: their least height is in need above).
    card.bottom = upperBottom;
    in = Rs_CardInner(&card, 0);
    t = Char_LayModel(&k, &in) + Rs_Px(16);
    if (g_char.tab == CHAR_TAB_MODEL && t > upperBottom)
        upperBottom = t;
    {
        // The tab In-game look: its switch in the title line of the card.
        const RECT inLook = Rs_CardInner(&card, 1);
        const int race = Char_LayLookRace(&k, &inLook) + Rs_Px(16);
        t = Char_LayLookPortrait(&k, &inLook) + Rs_Px(16);
        if (g_char.lookCard == CHAR_LOOK_RACE)
            t = race;
        Char_LayLookSwitch(&card);
        if (g_char.tab == CHAR_TAB_LOOK && t > upperBottom)
            upperBottom = t;
    }
    t = Char_LayVoices(&k, &in) + Rs_Px(16);
    if (g_char.tab == CHAR_TAB_VOICES && t > upperBottom)
        upperBottom = t;
    Char_LayExtrasSwitch(&card);
    if (g_char.tab == CHAR_TAB_EXTRAS && extrasBottom[g_char.extrasCard] > upperBottom)
        upperBottom = extrasBottom[g_char.extrasCard];
    if (barTop < upperBottom + vgap) {
        barTop = upperBottom + vgap;
        barH = Rs_Px(compact ? CHAR_COMPACT_BAR_H : CHAR_BAR_MIN_H);
    }
    card.bottom = upperBottom;
    Rs_CardAdd(page, &card, NULL);

    Char_LayPreview(page, left + leftW + k.gap, right, top, upperBottom);
    Char_LayBar(page, &k, left, right, barTop, barTop + barH);
    Char_TabApply(page);

    // After a DPI change the shell sets the base font; the raw output
    // stays in a fixed-width font, though.
    if ((HFONT)SendMessageW(g_char.raw, WM_GETFONT, 0, 0) != Rs_Font(RS_FONT_MONO))
        SendMessageW(g_char.raw, WM_SETFONT, (WPARAM)Rs_Font(RS_FONT_MONO), TRUE);
}

static LRESULT Char_Command(HWND page, WPARAM wParam, LPARAM lParam)
{
    int id = LOWORD(wParam);
    int code = HIWORD(wParam);
    (void)lParam;

    // The cards of the preview features handle their own controls; a change
    // of the wheel model or its size is a change of the command (Char_WheelsFollow).
    if (CharAnim_Command(page, id, code))
        return 0;
    if (CharWheels_Command(page, id, code)) {
        Char_WheelsFollow(page);
        Char_ViewNoteUpdate();
        return 0;
    }
    // The user mode fixes these to rldpack's defaults (Char_UserHidden):
    // taken and dropped, whatever sent it; the fallback stays ticked.
    if (id == CHAR_ID_FALLBACK) {
        Char_SetChecked(g_char.fallback, 1);
        return 0;
    }
    if (Rs_NativeForUsers() && (id == CHAR_ID_REPAIR || id == CHAR_ID_OPEN_PARTS || id == CHAR_ID_REMESH || id == CHAR_ID_REDUCE ||
                                id == CHAR_ID_REDUCE_FIT || id == CHAR_ID_COLORS || id == CHAR_ID_NATIVE)) {
        Char_UserDefaults();
        return 0;
    }
    if (id >= CHAR_ID_TAB && id < CHAR_ID_TAB + CHAR_TABS) {
        if (code == BN_CLICKED)
            Char_SelectTab(page, id - CHAR_ID_TAB, 0);
        return 0;
    }
    if (id >= CHAR_ID_EXTRAS && id < CHAR_ID_EXTRAS + CHAR_EXTRAS_COUNT) {
        if (code == BN_CLICKED)
            Char_SelectExtras(page, id - CHAR_ID_EXTRAS);
        return 0;
    }
    if (id >= CHAR_ID_LOOK_SWITCH && id < CHAR_ID_LOOK_SWITCH + CHAR_LOOK_CARDS) {
        if (code == BN_CLICKED)
            Char_SelectLook(page, id - CHAR_ID_LOOK_SWITCH);
        return 0;
    }
    // The look.
    if (id >= CHAR_ID_SHADOW && id <= CHAR_ID_LOOK_NOTE) {
        if ((id == CHAR_ID_SHADOW || id == CHAR_ID_EXHAUST) && code == CBN_SELCHANGE) {
            if (id == CHAR_ID_SHADOW)
                g_char.shadowSet = 1;
            else
                g_char.exhaustSet = 1;
            if (id == CHAR_ID_EXHAUST)
                RsView_PickBegin(g_char.view, 0);
            if (id == CHAR_ID_EXHAUST && Char_ListIndex(g_char.exhaust, 3) == 1)
                Char_LookCustomStart();
            Char_LookEnable();
            Char_LookPreview();
            Char_ChangedSoon(page);
        } else if (id >= CHAR_ID_POINT && id < CHAR_ID_POINT + 2 * CHAR_POINT_IDS) {
            const int i = (id - CHAR_ID_POINT) / CHAR_POINT_IDS, k = (id - CHAR_ID_POINT) % CHAR_POINT_IDS;
            if (k == 4 && code == BN_CLICKED)
                Char_LookPickStart(i + 1);
            else if (k >= 1 && k <= 3 && code == EN_CHANGE && !g_char.applying) {
                Char_LookPreview();
                Char_Changed(page);
            }
        }
        return 0;
    }
    switch (id) {
    case CHAR_ID_BACK:
    case CHAR_ID_NEXT:
        if (code == BN_CLICKED)
            Char_SelectTab(page, g_char.tab + (id == CHAR_ID_NEXT ? 1 : -1), 0);
        break;
    case CHAR_ID_MESSAGES:
        // A click on a message: the tab it belongs to.
        if (code == RS_MSGN_CLICK)
            Char_MsgOpen(page, Rs_MsgListClicked(g_char.msgs));
        break;
    case IDOK:
        Char_Enter(page);
        break;
    case IDCANCEL:
        // Esc while rldpack runs: as Cancel.
        if (g_char.progressOn)
            Char_Cancel(page);
        break;
    case CHAR_ID_CANCEL:
        if (code == BN_CLICKED)
            Char_Cancel(page);
        break;
    case CHAR_ID_MODEL:
        if (code == EN_CHANGE)
            Char_ModelChanged(page);
        break;
    case CHAR_ID_NAME:
        if (code == EN_CHANGE && !g_char.applying) {
            Char_NameFilter();
            Char_Changed(page);
        }
        break;
    case CHAR_ID_ICON:
        if (code == EN_CHANGE) {
            Char_UpdateIconOptions();
            Char_Changed(page);
        }
        break;
    case CHAR_ID_VOICES:
        if (code == EN_CHANGE) {
            // Another folder (or Clear): the events chosen per file were for
            // the old one. The same folder written another way (a slash at
            // the end, other capitals, / for \) and a path still being typed
            // (no folder) keep them.
            wchar_t dir[CHAR_VAL], full[CHAR_VAL];
            Char_FieldPath(g_char.voices, dir, CHAR_VAL);
            if (dir[0] && GetFullPathNameW(dir, CHAR_VAL, full, NULL))
                Char_CleanPath(dir, CHAR_VAL, full);
            if ((!dir[0] || Rs_DirExists(dir)) && _wcsicmp(dir, g_char.voiceSetDir) != 0) {
                g_char.voiceSetCount = 0;
                Char_Copy(g_char.voiceSetDir, CHAR_VAL, dir);
            }
            Char_VoiceNote();
            Char_Changed(page);
        }
        break;
    case CHAR_ID_OUT:
    case CHAR_ID_TEXTURES:
        if (code == EN_CHANGE)
            Char_Changed(page);
        break;
    case CHAR_ID_TEXTURES_BROWSE:
        if (code == BN_CLICKED)
            Char_BrowseTextures();
        break;
    case CHAR_ID_TEXTURES_CLEAR:
        if (code == BN_CLICKED)
            Rs_SetText(g_char.textures, L"");
        break;
    case CHAR_ID_VOICE_EVENT:
        // The event of the file chosen, from the next check on.
        if (code == CBN_SELCHANGE) {
            LRESULT sel = SendMessageW(g_char.voiceEvent, CB_GETCURSEL, 0, 0);
            int row = Char_VoiceSelected();
            if (row >= 0 && sel >= 0 && sel <= CHAR_VOICE_NONE) {
                int r = Char_VoiceAssign(row, (int)sel);
                if (r > 0)
                    Char_ChangedSoon(page);
                else if (r < 0) {
                    MessageBeep(MB_ICONWARNING);
                    Char_VoiceSelShow();    // the choice goes back to what is passed
                }
            }
        }
        break;
    case CHAR_ID_VOICE_PLAY:
        if (code == BN_CLICKED)
            Char_VoicePlay(Char_VoiceSelected());      // a failure says why below the folder
        break;
    case CHAR_ID_VOICE_NORMALIZE:
        if (code == BN_CLICKED)
            Char_ChangedSoon(page);
        break;
    case CHAR_ID_CLASS:
    case CHAR_ID_MASK:
    case CHAR_ID_ICON_FIT:
    case CHAR_ID_VCOLORS:
    case CHAR_ID_UP:
    case CHAR_ID_FORWARD:
    case CHAR_ID_COLORS:
        if (code == CBN_SELCHANGE)
            Char_ChangedSoon(page);
        break;
    case CHAR_ID_ICON_CORNERS:
    case CHAR_ID_ICON_FRAME:
        if (code == BN_CLICKED)
            Char_ChangedSoon(page);
        break;
    case CHAR_ID_MAPCOLOR_PICK:
        if (code == BN_CLICKED)
            Char_MapColorPick(page);
        break;
    case CHAR_ID_MAPCOLOR_LIKE:
        if (code == BN_CLICKED) {
            Char_MapColorSet(page, 0, CHAR_TEMPLATE_MAP_COLOR);
            SetFocus(g_char.mapPick);   // the button just pressed is greyed out now
        }
        break;
    case CHAR_ID_REDUCE_FIT:
        // As ticking the box, then the check at once.
        if (code == BN_CLICKED) {
            Char_SetChecked(g_char.reduce, 1);
            Char_UpdateOptions();
            Char_ApplyQuality();
            SetFocus(g_char.reduce);    // the button goes away with the check
            Char_Check(page);
        }
        break;
    case CHAR_ID_REDUCE:
        if (code == BN_CLICKED) {
            Char_UpdateOptions();
            Char_ApplyQuality();
            Char_ChangedSoon(page);
        }
        break;
    case CHAR_ID_REPAIR:
    case CHAR_ID_OPEN_PARTS:
    case CHAR_ID_REMESH:
        if (code == BN_CLICKED)
            Char_ChangedSoon(page);
        break;
    case CHAR_ID_NATIVE:
        if (code == BN_CLICKED) {
            Char_NativeUpdate();
            Char_ChangedSoon(page);
        }
        break;
    case CHAR_ID_WHEELS:
        // The preview follows at once, and in the user mode the line of the
        // native model (built only with the wheels hidden); the check follows
        // as for every click.
        if (code == BN_CLICKED) {
            RsView_SetWheels(g_char.view, Char_IsChecked(g_char.wheels));
            Char_NativeUpdate();
            Char_LookDefaults();
            Char_LookEnable();
            Char_LookPreview();
            Char_ChangedSoon(page);
        }
        break;
    case CHAR_ID_VIEW:
        if (code == RS_VIEW_N_YAW || code == RS_VIEW_N_CAMERA)
            g_char.yawNow = RsView_GetYaw(g_char.view);
        else if (code == RS_VIEW_N_PICK)
            Char_LookPicked(page);
        else if (code == RS_VIEW_N_TOGGLES)
            Char_ViewNoteUpdate();      // Native or Classic chosen in the View menu
        break;
    case CHAR_ID_POSE:
        if (code == CBN_SELCHANGE) {
            LRESULT sel = SendMessageW(g_char.pose, CB_GETCURSEL, 0, 0);
            Char_SetPose(sel < 0 ? 0 : (int)sel);
        }
        break;
    case CHAR_ID_MODEL_BROWSE:
        if (code == BN_CLICKED)
            Char_BrowseModel(page);
        break;
    case CHAR_ID_ICON_BROWSE:
        if (code == BN_CLICKED)
            Char_BrowseIcon();
        break;
    case CHAR_ID_ICON_CLEAR:
        if (code == BN_CLICKED)
            Rs_SetText(g_char.icon, L"");
        break;
    case CHAR_ID_VOICES_BROWSE:
        if (code == BN_CLICKED)
            Char_BrowseVoices();
        break;
    case CHAR_ID_VOICES_CLEAR:
        if (code == BN_CLICKED)
            Rs_SetText(g_char.voices, L"");
        break;
    case CHAR_ID_OUT_BROWSE:
        if (code == BN_CLICKED)
            Char_BrowseOut();
        break;
    case CHAR_ID_CHECK:
        if (code == BN_CLICKED)
            Char_CheckNow(page);
        break;
    case CHAR_ID_BUILD:
        if (code == BN_CLICKED)
            Char_Build(page);
        break;
    case CHAR_ID_RAW_TOGGLE:
        if (code == BN_CLICKED) {
            g_char.showRaw = !g_char.showRaw;
            Char_ShowView();
        }
        break;
    case CHAR_ID_SHOW:
        if (code == BN_CLICKED)
            Char_ShowInFolder();
        break;
    }
    return 0;
}

// The list of the voice files: a choice shows its event, a double click plays
// it, a tooltip says all rldpack reported about it.
static LRESULT Char_VoiceNotify(NMHDR *hdr)
{
    switch (hdr->code) {
    case LVN_ITEMCHANGED: {
        const NMLISTVIEW *lv = (const NMLISTVIEW *)hdr;
        if (!g_char.voiceFilling && (lv->uChanged & LVIF_STATE) && ((lv->uNewState ^ lv->uOldState) & LVIS_SELECTED))
            Char_VoiceSelShow();
        return 0;
    }
    case NM_DBLCLK: {
        const NMITEMACTIVATE *act = (const NMITEMACTIVATE *)hdr;
        if (act->iItem >= 0)
            Char_VoicePlay(Char_VoiceSelected());
        return 0;
    }
    case LVN_GETINFOTIPW: {
        NMLVGETINFOTIPW *tip = (NMLVGETINFOTIPW *)hdr;
        LVITEMW it;
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_PARAM;
        it.iItem = tip->iItem;
        if (tip->pszText && tip->cchTextMax > 0 && ListView_GetItem(g_char.voiceList, &it) && it.lParam >= 0 &&
            it.lParam < g_char.voiceRowCount) {
            const struct CharVoice *v = &g_char.voiceRow[it.lParam];
            wchar_t length[32], event[96], peak[32];
            Char_VoiceLengthText(v, length, 32);
            Char_VoiceEventText(v, event, 96);
            if (v->peak >= 0 && v->rate > 0)
                swprintf(peak, 32, L", peak %d.%d %%", v->peak / 10, v->peak % 10);
            else
                peak[0] = 0;
            swprintf(tip->pszText, (size_t)tip->cchTextMax, L"%ls\n%ls, %ld Hz, %d channel(s)%ls\nEvent: %ls",
                     v->name, length, v->rate, v->channels, peak, event);
            tip->pszText[tip->cchTextMax - 1] = 0;
        }
        return 0;
    }
    case LVN_GETEMPTYMARKUP: {
        NMLVEMPTYMARKUP *em = (NMLVEMPTYMARKUP *)hdr;
        wchar_t dir[CHAR_VAL];
        Char_FieldPath(g_char.voices, dir, CHAR_VAL);
        em->dwFlags = EMF_CENTERED;
        Char_Copy(em->szMarkup, L_MAX_URL_LENGTH,
                  !dir[0]             ? L"Choose a folder with WAV or VAG files."
                  : g_char.voiceKnown ? L"No files in this folder."
                                      : L"The files show up with the next check of a model.");
        return TRUE;
    }
    }
    return 0;
}

static LRESULT Char_Notify(HWND page, NMHDR *hdr)
{
    int handled = 0;
    LRESULT r;
    if (hdr && hdr->idFrom == CHAR_ID_VOICE_LIST && hdr->hwndFrom == g_char.voiceList)
        return Char_VoiceNotify(hdr);
    if (hdr && hdr->idFrom == CHAR_ID_MODEL_FILES && hdr->hwndFrom == g_char.modelFiles)
        return Char_ImportNotify(hdr);
    r = CharAnim_Notify(page, hdr, &handled);
    if (handled)
        return r;
    return CharWheels_Notify(page, hdr, &handled);
}

static LRESULT Char_Message(HWND page, UINT msg, WPARAM wParam, LPARAM lParam, int *handled)
{
    // The cards of the preview features first: they take only their own jobs
    // and controls. WM_DESTROY goes to both and then on to the page's own.
    if (msg == WM_DESTROY) {
        int ignored = 0;
        CharWheels_Message(page, msg, wParam, lParam, &ignored);
        ignored = 0;
        CharAnim_Message(page, msg, wParam, lParam, &ignored);
    } else {
        LRESULT r = CharAnim_Message(page, msg, wParam, lParam, handled);
        if (*handled)
            return r;
        r = CharWheels_Message(page, msg, wParam, lParam, handled);
        if (*handled) {
            Char_WheelsFollow(page);
            // A wheel read (or dropped): the note below the preview follows.
            if (msg == RS_WM_JOB_DONE)
                Char_ViewNoteUpdate();
            return r;
        }
    }
    switch (msg) {
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT *di = (const DRAWITEMSTRUCT *)lParam;
        int id = (int)di->CtlID;
        if (id >= CHAR_ID_TAB && id < CHAR_ID_TAB + CHAR_TABS)
            Char_DrawTab(di, id - CHAR_ID_TAB);
        else if (id >= CHAR_ID_EXTRAS && id < CHAR_ID_EXTRAS + CHAR_EXTRAS_COUNT)
            Char_DrawSwitch(di, g_charExtrasTexts[id - CHAR_ID_EXTRAS], id - CHAR_ID_EXTRAS == g_char.extrasCard);
        else if (id >= CHAR_ID_LOOK_SWITCH && id < CHAR_ID_LOOK_SWITCH + CHAR_LOOK_CARDS)
            Char_DrawSwitch(di, g_charLookTexts[id - CHAR_ID_LOOK_SWITCH], id - CHAR_ID_LOOK_SWITCH == g_char.lookCard);
        else
            return 0;
        *handled = 1;
        return TRUE;
    }
    case RS_WM_STEP_TAB:
        Char_SelectTab(page, (g_char.tab + ((int)(INT_PTR)wParam < 0 ? CHAR_TABS - 1 : 1)) % CHAR_TABS, 1);
        *handled = 1;
        return 1;
    case RS_WM_JOB_LINE: {
        wchar_t *line = (wchar_t *)lParam;
        if (line && g_char.jobId && (int)wParam == g_char.jobId) {
            Char_RawAppend(line);
            // The lines of a CHAR_JOB_META run are read when it ends (Char_MetaDone).
            if (g_char.jobKind != CHAR_JOB_META)
                Char_Feed(line);
        }
        Rs_Free(line);
        *handled = 1;
        return 0;
    }
    case RS_WM_JOB_PROGRESS: {
        struct RsJobProgress *p = (struct RsJobProgress *)lParam;
        if (p && g_char.jobId && (int)wParam == g_char.jobId)
            Char_Progress(page, p);
        Rs_Free(p);
        *handled = 1;
        return 0;
    }
    case RS_WM_JOB_BUSY: {
        // All job slots taken: rldpack did not start (Char_StartFailed said
        // so already); the message list says why.
        wchar_t *text = (wchar_t *)lParam;
        Char_MsgClear();
        Char_MsgAdd(-1, RS_SEV_ERROR, L"rldpack could not start: Reload Studio runs as many jobs as it can.",
                    text ? text : L"Wait until a check or build has ended, then check again.");
        Rs_Free(text);
        *handled = 1;
        return 0;
    }
    case RS_WM_JOB_DONE:
        if (g_char.jobId && (int)wParam == g_char.jobId)
            Char_JobDone(page, (int)lParam);
        else
            Char_PendingDone((int)wParam);
        *handled = 1;
        return 0;
    case WM_TIMER:
        if (wParam == CHAR_TIMER_BUSY) {
            KillTimer(page, CHAR_TIMER_BUSY);
            if (g_char.jobId)
                Char_ProgressShow(page, 1);
            *handled = 1;
            return 0;
        }
        if (wParam != CHAR_TIMER_CHECK)
            return 0;
        *handled = 1;
        // Building is running: the timer keeps running and asks again later
        if (g_char.jobId && g_char.jobKind == CHAR_JOB_BUILD)
            return 0;
        Char_Check(page);
        return 0;
    case WM_HSCROLL:
        if ((HWND)lParam == g_char.size) {
            Char_SizeScrolled(page, LOWORD(wParam));
            *handled = 1;
        }
        return 0;
    case RS_WM_PAGE_SHOWN: {
        // The game may have been chosen on the page Test meanwhile: the cue
        // follows, and a check again when the default output has moved.
        wchar_t field[CHAR_VAL], model[CHAR_VAL], def[CHAR_VAL];
        Char_FieldPath(g_char.out, field, CHAR_VAL);
        Char_FieldPath(g_char.model, model, CHAR_VAL);
        Char_DefaultOut(model, def, CHAR_VAL);
        if (!model[0] || field[0])
            Char_OutCue();
        else if (_wcsicmp(def, g_char.checkDefault) != 0)
            Char_Changed(page);
        Char_FilesChanged(page, L"page shown");
        *handled = 1;
        return 0;
    }
    case CHAR_WM_ACTIVATED:
        // Reload Studio is the active program again: on this page, while it
        // is shown (another page checks again when it is shown).
        if (IsWindowVisible(page))
            Char_FilesChanged(page, L"Reload Studio active again");
        *handled = 1;
        return 0;
    case WM_DROPFILES:
        Char_Drop(page, (HDROP)wParam);
        *handled = 1;
        return 0;
    case WM_DESTROY: {
        int i;
        RemoveWindowSubclass(Rs_MainWindow(), Char_MainSub, CHAR_SUBCLASS_MAIN);
        Rs_Free(g_char.stampFiles);
        g_char.stampFiles = NULL;
        g_char.stampKnown = 0;
        Char_Abandon();
        for (i = 0; i < CHAR_PENDING; i++)
            Char_TempDelete(g_char.pending[i].seq);
        for (i = 0; i < CHAR_CACHE; i++)
            Char_TempDelete(g_char.cache[i].seq);
        while (g_char.cache[0].key)
            Char_CacheDrop(0);
        Rs_Free(g_char.feedText);
        g_char.feedText = NULL;
        g_char.feedLen = g_char.feedCap = 0;
        Char_VoiceStop();
        Char_TempDeleteVoices(g_char.voiceSeq);
        g_char.voiceSeq = 0;
        Char_VoicesFree(g_char.voiceRow, g_char.voiceRowCount);
        g_char.voiceRow = NULL;
        g_char.voiceRowCount = 0;
        Char_ImagesClear();
        Rs_Free(g_char.importRow);
        g_char.importRow = NULL;
        g_char.importRowCount = 0;
        Rs_Free(g_char.msgTab);
        g_char.msgTab = NULL;
        Rs_Free(g_char.msgCard);
        g_char.msgCard = NULL;
        g_char.msgTabCount = g_char.msgTabCap = 0;
        return 0;
    }
    }
    return 0;
}

static int Char_Automate(HWND page, const wchar_t *verb, const wchar_t *arg)
{
    int r;

    if (wcscmp(verb, L"tab") == 0)
        return Char_AutoTab(page, arg);
    if (wcscmp(verb, L"extras") == 0)
        return Char_AutoExtras(page, arg);
    if (wcscmp(verb, L"problem") == 0)
        return Char_AutoProblem(page, arg);
    if (wcscmp(verb, L"model") == 0) {
        if (!arg[0] || _wcsicmp(arg, L"none") == 0) {
            Char_SetModel(page, L"");
            Rs_AutoLog(L"  model: (none)");
            return RS_AUTO_DONE;
        }
        Rs_AutoLog(L"  model: %ls", arg);
        if (Char_SetModel(page, arg))
            return RS_AUTO_WAIT;
        Rs_AutoLog(L"  model: rldpack could not be started");
        return RS_AUTO_FAIL;
    }
    if (wcscmp(verb, L"drop") == 0)
        return Char_AutoDrop(page, arg);
    if (wcscmp(verb, L"name") == 0)
        return Char_AutoText(g_char.name, verb, arg, 0);
    if (wcscmp(verb, L"class") == 0)
        return Char_AutoClass(page, arg);
    if (wcscmp(verb, L"mask") == 0)
        return Char_AutoMask(page, arg);
    if (wcscmp(verb, L"mapcolor") == 0)
        return Char_AutoMapColor(page, arg);
    if (wcscmp(verb, L"size") == 0)
        return Char_AutoSize(page, arg);
    if (wcscmp(verb, L"icon") == 0)
        return Char_AutoText(g_char.icon, verb, arg, 1);
    if (wcscmp(verb, L"voices") == 0)
        return Char_AutoText(g_char.voices, verb, arg, 1);
    if (wcscmp(verb, L"textures") == 0)
        return Char_AutoText(g_char.textures, verb, arg, 1);
    if (wcscmp(verb, L"voice") == 0)
        return Char_AutoVoice(page, arg);
    if (wcscmp(verb, L"voicenorm") == 0)
        return Char_AutoOption(page, g_char.voiceNorm, verb, arg);
    if (wcscmp(verb, L"voiceplay") == 0)
        return Char_AutoVoicePlay(arg);
    if (wcscmp(verb, L"out") == 0)
        return Char_AutoText(g_char.out, verb, arg, 1);
    // Like the button Cancel (it shows while rldpack runs): mostly as "now
    // cancel", since a step waits until the page is idle.
    if (wcscmp(verb, L"cancel") == 0) {
        int kind = g_char.jobKind;
        if (!Char_Cancel(page)) {
            Rs_AutoLog(L"  cancel: nothing is running");
            return RS_AUTO_DONE;
        }
        Rs_AutoLog(L"  cancel: the %ls is cancelled", kind == CHAR_JOB_BUILD ? L"build" : L"check");
        return RS_AUTO_WAIT;
    }
    if (wcscmp(verb, L"check") == 0) {
        r = Char_CheckNow(page);
        if (r > 0)
            return RS_AUTO_WAIT;
        if (r == 0)
            return RS_AUTO_DONE;
        Rs_AutoLog(L"  check: %ls", g_char.jobId ? L"a build is running" : L"no model is chosen");
        return RS_AUTO_FAIL;
    }
    // Like the button "Build character". Only rldpack runs; the game does not.
    if (wcscmp(verb, L"build") == 0) {
        if (!Char_CanBuild()) {
            Rs_AutoLog(L"  build: not possible - the last check did not pass");
            return RS_AUTO_FAIL;
        }
        r = Char_Build(page);
        if (r > 0)
            return RS_AUTO_WAIT;
        Rs_AutoLog(L"  build: not started");
        return RS_AUTO_FAIL;
    }
    // The user mode: the choices it fixes are not there to set.
    if (Rs_NativeForUsers() && (wcscmp(verb, L"repair") == 0 || wcscmp(verb, L"open-parts") == 0 || wcscmp(verb, L"remesh") == 0 ||
                                wcscmp(verb, L"reduce") == 0 || wcscmp(verb, L"reduce-to-fit") == 0 || wcscmp(verb, L"colors") == 0)) {
        Rs_AutoLog(L"  %ls: hidden - the user mode keeps rldpack's default", verb);
        return RS_AUTO_FAIL;
    }
    if (wcscmp(verb, L"fallback") == 0) {
        Rs_AutoLog(L"  fallback: locked - coming soon (the classic model is always built)");
        return RS_AUTO_FAIL;
    }
    if (wcscmp(verb, L"repair") == 0)
        return Char_AutoOption(page, g_char.repair, verb, arg);
    if (wcscmp(verb, L"open-parts") == 0)
        return Char_AutoOption(page, g_char.openParts, verb, arg);
    if (wcscmp(verb, L"remesh") == 0)
        return Char_AutoOption(page, g_char.remesh, verb, arg);
    if (wcscmp(verb, L"reduce") == 0)
        return Char_AutoOption(page, g_char.reduce, verb, arg);
    // Like the button "Reduce to fit" below the headline (only while it is shown).
    if (wcscmp(verb, L"reduce-to-fit") == 0) {
        if (!g_char.reduceFitOn) {
            Rs_AutoLog(L"  reduce-to-fit: the button is not shown - the model is not over the limit");
            return RS_AUTO_FAIL;
        }
        Rs_AutoLog(L"  reduce-to-fit: Reduce to fit on, check");
        Char_SetChecked(g_char.reduce, 1);
        Char_UpdateOptions();
        Char_ApplyQuality();
        r = Char_Check(page);
        return r > 0 ? RS_AUTO_WAIT : RS_AUTO_DONE;
    }
    if (wcscmp(verb, L"wheels") == 0)
        return Char_AutoOption(page, g_char.wheels, verb, arg);
    // The tab In-game look: its card; the look (preview feature).
    if (wcscmp(verb, L"look") == 0) {
        int c;
        for (c = 0; c < CHAR_LOOK_CARDS && _wcsicmp(arg, g_charLookWords[c]) != 0; c++) {
        }
        if (c >= CHAR_LOOK_CARDS) {
            Rs_AutoLog(L"  look: say portrait or race");
            return RS_AUTO_FAIL;
        }
        Char_SelectLook(page, c);
        Rs_AutoLog(L"  look: %ls", g_charLookTexts[c]);
        return RS_AUTO_DONE;
    }
    if (wcscmp(verb, L"shadow") == 0 || wcscmp(verb, L"exhaust") == 0 || wcscmp(verb, L"exhaust-point") == 0 ||
        wcscmp(verb, L"exhaust-pick") == 0) {
        if (wcscmp(verb, L"shadow") == 0 || wcscmp(verb, L"exhaust") == 0) {
            const int isShadow = wcscmp(verb, L"shadow") == 0;
            const wchar_t *const *words = isShadow ? g_charShadowWords : g_charExhaustWords;
            int m;
            for (m = 0; m < 3 && _wcsicmp(arg, words[m]) != 0; m++) {
            }
            if (m >= 3) {
                Rs_AutoLog(L"  %ls: say %ls, %ls or %ls", verb, words[0], words[1], words[2]);
                return RS_AUTO_FAIL;
            }
            SendMessageW(isShadow ? g_char.shadow : g_char.exhaust, CB_SETCURSEL, m, 0);
            if (isShadow)
                g_char.shadowSet = 1;
            else
                g_char.exhaustSet = 1;
            if (!isShadow && m == 1)
                Char_LookCustomStart();
            Char_LookEnable();
            Char_LookPreview();
            Rs_AutoLog(L"  %ls: %ls", verb, words[m]);
            r = Char_Check(page);
            return r > 0 ? RS_AUTO_WAIT : RS_AUTO_DONE;
        }
        if (wcscmp(verb, L"exhaust-point") == 0) {
            // "<n> <x> <y> <z>" in game units, or "<n> none" (the fields emptied)
            int n = 0, a;
            double v[3];
            wchar_t rest[16];
            if (swscanf(arg, L"%d %15ls", &n, rest) == 2 && (n == 1 || n == 2) && _wcsicmp(rest, L"none") == 0) {
                g_char.applying = 1;
                for (a = 0; a < 3; a++)
                    Rs_SetText(g_char.point[n - 1][a], L"");
                g_char.applying = 0;
                Rs_AutoLog(L"  exhaust-point: %d none", n);
            } else if (swscanf(arg, L"%d %lf %lf %lf", &n, &v[0], &v[1], &v[2]) == 4 && (n == 1 || n == 2)) {
                int q[3];
                for (a = 0; a < 3; a++)
                    q[a] = (int)(v[a] * 16.0 < 0.0 ? v[a] * 16.0 - 0.5 : v[a] * 16.0 + 0.5);
                if (Char_ListIndex(g_char.exhaust, 3) != 1) {
                    SendMessageW(g_char.exhaust, CB_SETCURSEL, 1, 0);
                    g_char.exhaustSet = 1;
                    Char_LookCustomStart();
                    Char_LookEnable();
                }
                Char_LookPointSet(n - 1, q);
                Rs_AutoLog(L"  exhaust-point: %d at %.2f %.2f %.2f", n, (double)q[0] / 16.0, (double)q[1] / 16.0, (double)q[2] / 16.0);
            } else {
                Rs_AutoLog(L"  exhaust-point: say <1|2> <x> <y> <z> or <1|2> none");
                return RS_AUTO_FAIL;
            }
            Char_LookPreview();
            r = Char_Check(page);
            return r > 0 ? RS_AUTO_WAIT : RS_AUTO_DONE;
        }
        {
            // exhaust-pick <n> <px> <py>: as a click at that pixel of the view
            int n = 0, px = 0, py = 0;
            if (swscanf(arg, L"%d %d %d", &n, &px, &py) != 3 || (n != 1 && n != 2)) {
                Rs_AutoLog(L"  exhaust-pick: say <1|2> <x pixel> <y pixel> of the preview");
                return RS_AUTO_FAIL;
            }
            if (Char_ListIndex(g_char.exhaust, 3) != 1) {
                SendMessageW(g_char.exhaust, CB_SETCURSEL, 1, 0);
                g_char.exhaustSet = 1;
                Char_LookCustomStart();
                Char_LookEnable();
            }
            Char_LookPickStart(n);
            RsView_PickPixel(g_char.view, n, px, py);   // RS_VIEW_N_PICK -> Char_LookPicked
            r = Char_Check(page);
            return r > 0 ? RS_AUTO_WAIT : RS_AUTO_DONE;
        }
    }
    if (wcscmp(verb, L"icon-fit") == 0)
        return Char_AutoIconFit(page, arg);
    if (wcscmp(verb, L"vertex-colors") == 0)
        return Char_AutoVColors(page, arg);
    if (wcscmp(verb, L"native-model") == 0) {
        if (Rs_NativeForUsers()) {
            Rs_AutoLog(L"  native-model: no tick box - the user mode builds it for an OBJ with Show kart wheels off or a wheel model%ls",
                       !g_char.objOn ? L" (this model is no OBJ: classic model only)"
                       : Char_NativePassed() ? L" (passed)"
                                             : L" (Show kart wheels is on and no wheel model: classic model only, not passed)");
            return _wcsicmp(arg, L"on") == 0 ? RS_AUTO_DONE : RS_AUTO_FAIL;
        }
        r = Char_AutoOption(page, g_char.native, verb, arg);
        Char_NativeUpdate();
        if (!g_char.objOn)
            Rs_AutoLog(L"  native-model: greyed out - no OBJ, not passed");
        else if (Char_IsChecked(g_char.native) && !Char_NativePassed())
            Rs_AutoLog(L"  native-model: not passed - Show kart wheels is on and no wheel model is chosen (classic model only)");
        return r;
    }
    if (wcscmp(verb, L"up") == 0)
        return Char_AutoList(page, g_char.up, verb, arg, g_charUpWords, g_charUpTexts, CHAR_UP_COUNT);
    if (wcscmp(verb, L"forward") == 0)
        return Char_AutoList(page, g_char.forward, verb, arg, g_charForwardWords, g_charForwardTexts, CHAR_FORWARD_COUNT);
    if (wcscmp(verb, L"colors") == 0)
        return Char_AutoList(page, g_char.colors, verb, arg, g_charColorsWords, g_charColorsTexts, CHAR_COLORS_COUNT);
    if (wcscmp(verb, L"icon-transparent") == 0)
        return Char_AutoOption(page, g_char.iconCorners, verb, arg);
    if (wcscmp(verb, L"icon-frame") == 0)
        return Char_AutoOption(page, g_char.iconFrame, verb, arg);
    if (wcscmp(verb, L"pose") == 0)
        return Char_AutoPose(arg);
    if (wcscmp(verb, L"turn") == 0)
        return Char_AutoTurn(arg);
    r = Char_AutoView(verb, arg);
    if (r != RS_AUTO_UNKNOWN)
        return r;
    if (wcscmp(verb, L"report") == 0) {
        if (!arg[0] || !Char_WriteReport(arg)) {
            Rs_AutoLog(L"  report: could not write '%ls'", arg);
            return RS_AUTO_FAIL;
        }
        Rs_AutoLog(L"  report: %ls", arg);
        return RS_AUTO_DONE;
    }
    r = CharAnim_Automate(page, verb, arg);
    if (r != RS_AUTO_UNKNOWN)
        return r;
    r = CharWheels_Automate(page, verb, arg);
    Char_WheelsFollow(page);
    return r;
}

static int Char_Busy(HWND page)
{
    (void)page;
    return g_char.jobId != 0 || g_char.timer || CharAnim_Busy() || CharWheels_Busy();
}

const struct RsPageDef g_rsCharPage = {
    L"Character",
    L"Build a character",
    L"",                        // no subtitle: CHAR_START_TEXT below the model field instead
    Char_Create,
    Char_Layout,
    Char_Command,
    Char_Notify,
    Char_Message,
    Char_Automate,
    Char_Busy,
    CHAR_PAGE_MIN_W,
    CHAR_PAGE_MIN_H
};
