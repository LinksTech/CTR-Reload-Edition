// rs_char.c - page "Character": build a glTF, OBJ or PLY model as .rldchar
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
// Model: a glTF (.glb with everything inside - the recommended format, first
// in the file dialog - or .gltf with its files), a PLY, or an OBJ with its
// material file (MTL) and the textures the MTL names (PNG, JPG, TGA). make-char
// reads a glTF into the same data as an OBJ, so the page treats it as one
// (Char_ModelIsObj: the Textures folder, vertex colours, the native model,
// the wheels); its shape keys are the driver's animations (card Animations,
// @pose). rldpack tells the format by the content and
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
// THE GUIDED FLOW (rs_charroute.h): the page asks first "What do you want to
// build?" - step 0, three large cards with an example picture (Rs_RouteImage,
// rs_cards.c; without one a neutral frame with the title), the title and one
// sentence of RsRoute_Preset. A click, Enter on a card (Tab and the arrow keys
// move between them) or the automation verb "route" chooses the route:
//   Vanilla   the classic model of make-char, as the page built it before -
//             the same commands, the same bytes
//   Low-Poly  the classic model made from the same .glb (the classic
//   Hi-Res    fallback) and the .glb itself in the chunk CHRG (THE BUILD OF A
//             CHRG ROUTE); the presets and the plain-English messages are
//             rs_charhires.c's
// THE CREATOR KIT (rs_kit.c, a module of its own embedded in this page): step
// 0 has "Creator Kit" at the top right, beside "What do you want to build?"
// (K10: the kit's icon, the title in the font of the card titles, "Templates
// to get started" small below it; as tall as that heading row), and the step
// Model of every route, while no model is chosen (the field empty), the line
// "No model yet? Start with the Creator Kit." with "Open Creator Kit" at its
// end. Either shows the kit's page in place of this one (the sidebar keeps
// Character marked): from step 0 with all three ways, from the step Model
// with the way of the route chosen (Rs_KitChooseWay, K9). Back on the kit's
// first step comes back here to step 0 (RS_WM_HOST_RETURN), every entry
// kept. The automation verb "kit" clicks the one that is shown.
// Then only the steps of the route, the tabs of before: Model, Driver, In-game
// look, Voices (optional: Next skips it), Build. What a route does not need is
// hidden, not greyed out (Char_RouteHides). "Change type" in the head of the
// steps (and Back on the step Model) goes back to step 0; every entry stays,
// and another route checks the model again with its rules. Each step has one
// main action (the filled button: Next on the steps 1 to 4, Build on the
// step Build, after a build Test in game) and Next. Old scripts that name no
// route (model x.obj; build) pick Vanilla with their first verb of the page.
//
// ADVANCED: a section of the step Model, closed at the start ("Advanced
// options"; open, it takes the place of everything below the model field,
// "Hide advanced" in its title line). Its cards, one at a time (Char_ExtrasShown):
// Vanilla - Options (Repair the model, Draw open parts from both sides,
// Closed hull, Reduce to fit, with today's defaults; not in the user mode,
// which fixes them), Import (the axes, the colours, the vertex colours and the
// native model of an OBJ - the tab Extras of before), Animations (the shape
// keys) and, only with --enable-preview-features, Wheels; a CHRG route -
// Options ("Upscale small textures", see THE UPSCALE below, the JPEG
// conversion, the vertex colours, THE CLASSIC MODEL) and Animations (THE
// CLIPS of the model). Reduce to fit (the button, over the limit) stays on the
// step itself: it is the fix of the model's error.
//
// THE UPSCALE of a CHRG route (rs_charroute.h): "Upscale small textures" is
// off at the start of Low-Poly and Hi-Res. When a model is checked the page
// suggests it - ticks the box and says so in one line below it
// (RsCharHires_UpscaleLine) - only when every base colour texture of the
// model is under 64 px on its larger edge, as the file has it (before the
// game's round-up; RsCharHires_SuggestUpscale). Otherwise it stays off and
// no line is shown. A box the author clicked stays with that model (the
// choices per model, as the vertex colour mode); "upscale auto" in
// automation forgets it.
//
// THE KART of a CHRG route (rs_charroute.h, RsHiresCheck.kartCase): three
// cases. A model with a node "kart" that has meshes (RS_KART_PART) is packed
// with --kart-part, and the check says RS_TEXT_KART_PART as a message; the
// switch below is hidden then. The kit's mesh-less kart joint (RS_KART_GAME)
// suggests "Kart: from the player's game data" (Advanced, card Options, below
// the upscale): the box is ticked, one line below it and one on the step Model
// with "Change" say so, and the pack gets --game-kart template (the game puts
// the original kart from the player's own game data under the figure; from
// 4.3 also under the classic model, make-char --figure-only: CHRI bit 5). Any
// other model (RS_KART_OWN) has the switch off and is packed as before - the
// same bytes. A box the author clicked stays with that model (the choices per
// model); "gamekart auto" in automation forgets it. A finished character
// (.rldchar) has no switch: what its CHRG says is reported ("parts:").
//
// THE JPEG TEXTURES of a CHRG route (rs_charroute.h): the game reads PNG
// only, so a model with a JPEG texture is refused (HRG-16) - the error stays
// at its place and its "How to fix" names the button "Convert to PNG" on the
// step Model (the fix of the model's error, like Reduce to fit). Never
// suggested: only the author's click (or "png convert") ticks "Convert JPEG
// textures to PNG" (Advanced, card Options, below the upscale), kept with
// the model. The page then checks the model as converted: rldpack hires-png
// in this process (Rs_HiresPngFile) into Rs_TempDir, that file checked and
// deleted again (Char_HiresEnsure) - the JPEG error goes and the upscale
// suggestion sees the sizes of the PNGs. The step Model shows one line, "Your
// JPEG textures will be converted to PNG.", with "Change" (Advanced at the
// box). Unticked there, the model is checked as it is again.
//
// THE CLIPS of a CHRG route (Advanced, card Animations; the page "Hi-Res
// (Preview)" had them before it went): the list "Clip" - Bind pose and the 12
// clips of the game, then the optional finish_win and finish_lose (4.3) -,
// Play | Pause, "< Frame" and "Frame >" (1/30 s, a tick of the game), the time
// of the frame, and the table of the 14 clips: in the file and how long, or
// the stand-in the game plays, or the rest pose; a missing finish clip is no
// warning and says what the game plays after the finish line instead (with
// a kart - its kart part or the game's kart - the seated race clips, else
// podium_1..3 by place / lose). A
// click on a row shows that clip. The preview shows the clip chosen; at the
// start idle (where the clips can play), playing - in automation paused at
// its first frame unless "hires-play on" (shots stay the same every run).
//
// THE CLASSIC MODEL of a CHRG route (Advanced, card Options, "Classic
// model"): made from the .glb by make-char (the start), or the one of a
// finished character: then no make-char runs, the build is [hires-png]
// [hires-upscale] hires-add <that .rldchar> <model> -o <out> - rldpack's bytes,
// as rldpack hires-add itself -, the character's name, look and voices stay,
// and the steps Driver, In-game look and Voices only say so. Not the start: the
// step Model has a line with "Change" for it.
//
// THE FINISHED CHARACTER: on Low-Poly and Hi-Res the model field takes a
// .rldchar too - its CHRG is checked with the same findings as a .glb and
// shown with its clips; nothing is built from it (the step Build says to
// choose the .glb; packing a model into a finished character is THE CLASSIC
// MODEL).
//
// THE MESSAGES AT THEIR PLACE: every message keeps its step (and card); the
// step shows its own below its fields, each with its detail ("How to fix" for
// a CHRG route), and nothing where it has none. Vanilla: make-char's @msg
// lines by their id (Char_TabOfCode, as before); a CHRG route: those of
// RsCharHires_Messages by their place - a model without clips or wheel nodes
// is one line each - and of the classic fallback's make-char only its errors
// about the model; its other lines about the model (the reduction, the fit,
// rig-bind-pose) stay behind "Show details" (Char_ChrgMessages). The head of
// each step keeps its number in red, amber or green.
//
// THE PREVIEW is shown from step 1 on, on every step. Vanilla: the classic
// model as before. A CHRG route: the picture of the .glb as its check drew it
// (textured, the engine's filter, the engine's light and the vertex colours
// in the mode chosen) on the view's neutral stage (RsView_SetStage: grey
// background and floor, the same in both colour schemes), with its idle clip
// playing on the page's timer (RsHires_AnimFrame; in automation its first
// frame unless "hires-play on" says otherwise, so that shots are the same every
// run; THE CLIPS: Advanced, card Animations, shows any clip or the bind pose),
// and above it the switch "Low-Poly | Classic fallback" or "Hi-Res | Classic fallback"
// (the route's model, Char_PreviewText; the fallback:
// make-char --preview, drawn as Vanilla's, without the stage).
//
// THE VERTEX COLOURS of a CHRG route (rs_charroute.h, include/rldhires_color.inc):
// a model with COLOR_0 has the switch "Vertex color blending" on its step
// Model, ON = PSX blend (grey 128 is neutral, as on the PS1), OFF = ignored,
// and below it one line that says why it is set (RsCharHires_ColourLine);
// Advanced (card Options) holds all three modes, glTF (white is neutral) the
// third - the switch then shows neither on nor off. A model without COLOR_0
// has none of it and the build passes no switch. When a model is checked the
// mode is the suggestion (RsCharHires_SuggestColours: PS1-like -> PSX, other
// colours -> glTF, not decidable -> the route's); a mode the author chose
// stays with that model (CHAR_VCOL_CHOICES models remembered). The mode is
// the picture's at once (RsHires_SetColourMode), the messages' (HRG-36 a
// warning in glTF only) and the build's: hires-add --vertex-colors <mode>.
//
// Layout: the steps as tabs on the left, the 3D preview always on the right,
// the bar with Back and Next (and while rldpack runs its progress and Cancel)
// below both, so that every step is seen whole without scrolling:
//   1 Model         the model (a CHRG route: a .glb, or a finished .rldchar to look at), the line of what
//                   its check found; Vanilla: what an OBJ brought along, its
//                   size and Show kart wheels; Advanced
//   2 Driver        name, driving style, mask
//   3 In-game look  two cards, switched in the title line: Portrait (icon,
//                   framing, transparency, frame, the game's view of it) and
//                   In the race (minimap colour; shadow and exhaust of the
//                   driver with two points and Pick - preview feature, see
//                   THE LOOK OF THE DRIVER)
//   4 Voices        the folder, its files and their events, Play, the ten
//                   events at a glance
//   5 Build         where the file goes (the field empty: the game's
//                   characters folder), the headline of the last check or
//                   build, Build - after a build Test in game (Rs_TestCharacter,
//                   rs_test.c) and Build again -, Show in folder, "Show
//                   details" (the raw output of rldpack in place of the
//                   messages of the step)
// The check runs by itself (a model chosen, a field changed); there is no
// button Check (the automation verb "check" and Enter run it). A check or
// build that runs longer than CHAR_PROGRESS_DELAY shows a bar of its progress
// (the @progress lines of rldpack, which the shell passes as
// RS_WM_JOB_PROGRESS) and Cancel in the bar: Cancel ends rldpack
// (Rs_JobCancel); a build cancelled leaves at most <out>.part, which the page
// deletes - a file <out> from before stays as it was. Every control is a child
// of the page as before, with the same ID; the controls of the other steps
// are only hidden - a message to one of them still works. Ctrl+Tab and
// Ctrl+Shift+Tab step through the steps.
//
// THE BUILD OF A CHRG ROUTE (Char_ChainStart): every step through
// Rs_RunRldpack, so the bytes are rldpack's - make-char with the page's
// switches, RsCharHires_FallbackArgs and --wheels off when the .glb has its
// own four wheels -> <out name>.classic.part in Rs_TempDir (make-char reads
// a JPEG texture itself, so it takes the model as it is); with "Convert JPEG
// textures to PNG" hires-png -> <model>-png.glb in Rs_TempDir; with "Upscale
// small textures" hires-upscale of that (or of the model) ->
// <name>-upscaled.glb in Rs_TempDir (never beside the output, never in the
// game's characters folder); hires-add <classic> <the last of them> -o <out>.
// The temp files go at the end.

#include "reloadstudio.h"
#include "rs_anim.h"
#include "rs_charroute.h"
#include "rs_hires_model.h"
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
// The file dialog of the model: glTF binary first (recommended: it brings its
// textures and its shape keys, the driver's animations), then OBJ and PLY.
#define CHAR_MODEL_FILTER  L"glTF binary (*.glb) - recommended\0*.glb\0OBJ models (*.obj)\0*.obj\0PLY models (*.ply)\0*.ply\0" \
                           L"glTF (*.gltf)\0*.gltf\0All models (*.glb;*.gltf;*.obj;*.ply)\0*.glb;*.gltf;*.obj;*.ply\0All files\0*.*\0\0"
#define CHAR_VOICE_SET_MAX 64     // files with an event of their own (--voice), at most
#define CHAR_MAX_ARGS      (64 + 2 * CHAR_VOICE_SET_MAX)
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
                                  // two would push the bar down
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
#define CHAR_ID_EXTRAS         187   // 187..190: Import | Wheels | Animations | Options in the Advanced of the step Model
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
// THE GUIDED FLOW (rs_charroute.h): step 0 and what the steps add.
#define CHAR_ID_ROUTE          203   // 203..205: the three cards of step 0, RS_ROUTE_VANILLA..RS_ROUTE_HIRES
#define CHAR_ID_ROUTE_ASK      206   // "What do you want to build?"
#define CHAR_ID_CHANGE_TYPE    207   // "Change type" in the head of the steps
#define CHAR_ID_ROUTE_LABEL    208   // the route chosen, beside it
#define CHAR_ID_ADVANCED       209   // "Advanced" on the step Model: opens and closes the section
#define CHAR_ID_UPSCALE        210   // "Upscale small textures" (Advanced of a CHRG route)
#define CHAR_ID_UPSCALE_HELP   211
#define CHAR_ID_TEST           212   // "Test in game" (step Build, after a build)
#define CHAR_ID_REBUILD        213   // "Build again" beside it
#define CHAR_ID_TEST_NOTE      214   // what Test in game did, or why not
#define CHAR_ID_PREVIEW_SWITCH 215   // 215..216: Low-Poly/Hi-Res | Classic fallback above the preview (CHRG routes)
#define CHAR_ID_OUT_NOTE       217   // where the build goes, below the output field
#define CHAR_ID_VBLEND         218   // "Vertex color blending" on the step Model (a CHRG route, a model with COLOR_0)
#define CHAR_ID_VBLEND_NOTE    219   // its line: what the mode does and why it is set
#define CHAR_ID_VMODE_LABEL    220   // Advanced, card Options of a CHRG route: "Vertex colors" PSX | glTF | Off
#define CHAR_ID_VMODE          221
#define CHAR_ID_VMODE_HELP     222
#define CHAR_ID_UPSCALE_NOTE   223   // the line below "Upscale small textures": the suggestion, or the author's choice
#define CHAR_ID_UPSCALE_STEP   224   // the line of the upscale on the step Model itself (outside Advanced)
#define CHAR_ID_UPSCALE_CHANGE 225   // "Change" at its end: opens Advanced at the box
#define CHAR_ID_PNG            226   // "Convert JPEG textures to PNG" (Advanced of a CHRG route, card Options)
#define CHAR_ID_PNG_HELP       227
#define CHAR_ID_PNG_STEP       228   // its line on the step Model (only while the box is on)
#define CHAR_ID_PNG_CHANGE     229   // "Change" at its end: opens Advanced at the box
#define CHAR_ID_PNG_FIX        230   // "Convert to PNG" on the step Model: the fix of the JPEG error
// Advanced of a CHRG route, the card Animations: the clips of the model (THE CLIPS).
#define CHAR_ID_CLIP_LABEL     231   // "Clip"
#define CHAR_ID_CLIP           232   // Bind pose | the 12 clips
#define CHAR_ID_CLIP_PLAY      233   // Play | Pause
#define CHAR_ID_CLIP_PREV      234   // one frame back (1/30 s, a tick of the game)
#define CHAR_ID_CLIP_NEXT      235   // one frame on
#define CHAR_ID_CLIP_TIME      236   // the time of the frame shown
#define CHAR_ID_CLIP_TABLE     237   // the 12 clips: in the file and how long, or what plays instead
#define CHAR_ID_CLIP_NOTE      238
// Advanced of a CHRG route, the card Options: where the classic model comes
// from (THE CLASSIC MODEL); its line on the step Model with "Change".
#define CHAR_ID_CLASSIC_LABEL  239
#define CHAR_ID_CLASSIC_MODE   240   // made from the .glb | from a finished character
#define CHAR_ID_CLASSIC_FILE   241
#define CHAR_ID_CLASSIC_BROWSE 242
#define CHAR_ID_CLASSIC_HELP   243
#define CHAR_ID_CLASSIC_STEP   244   // its line on the step Model (only while it comes from a character)
#define CHAR_ID_CLASSIC_CHANGE 245   // "Change" at its end: opens Advanced at the list
#define CHAR_ID_CHAR_NOTE      246   // the card Options with a finished character as the model: why nothing is there
#define CHAR_ID_GAMEKART       247   // "Kart: from the player's game data" (Advanced of a CHRG route, card Options)
#define CHAR_ID_GAMEKART_HELP  248
#define CHAR_ID_GAMEKART_NOTE  249   // the line below the box: the suggestion, or the author's choice
#define CHAR_ID_GAMEKART_STEP  250   // the line of the switch on the step Model itself (outside Advanced)
#define CHAR_ID_GAMEKART_CHANGE 251  // "Change" at its end: opens Advanced at the box
// THE CREATOR KIT: its button on step 0 and its line on the step Model.
#define CHAR_ID_KIT_OPEN       252   // "Creator Kit" at the top right of step 0
#define CHAR_ID_KIT_STEP       253   // "No model yet? Start with the Creator Kit." (CHRG routes, no model)
#define CHAR_ID_KIT_LINK       254   // "Open Creator Kit" at its end
#define CHAR_VCOL_CHOICES      8     // models whose vertex colour mode, upscale or PNG conversion the author chose, remembered
// The author's choices remembered per model (Char_Remembered).
#define CHAR_CHOICE_VCOL       0     // the vertex colour mode (RS_HIRES_VCOL_*)
#define CHAR_CHOICE_UPSCALE    1     // "Upscale small textures" (0 or 1)
#define CHAR_CHOICE_PNG        2     // "Convert JPEG textures to PNG" (1; off is forgotten)
#define CHAR_CHOICE_GAMEKART   3     // "Kart: from the player's game data" (0 or 1)
#define CHAR_CHOICE_KINDS      4
// The controls of the cards Wheels and Animations (WH_ID_FIRST..WH_ID_LAST in
// rs_wheels.c, AN_ID_FIRST..AN_ID_LAST in rs_anim.c): Advanced of the step Model shows them.
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
// the minimap colour and the output (Char_MetaStart). Every step of the build
// of a CHRG route is a CHAR_JOB_BUILD (g_char.chainStep says which).
enum { CHAR_JOB_NONE = 0, CHAR_JOB_CHECK, CHAR_JOB_BUILD, CHAR_JOB_META };
// The steps of the build of a CHRG route (THE BUILD OF A CHRG ROUTE).
enum { CHAR_CHAIN_NONE = 0, CHAR_CHAIN_CLASSIC, CHAR_CHAIN_PNG, CHAR_CHAIN_UPSCALE, CHAR_CHAIN_PACK };
// What a machine line is to Char_MetaDone (Char_LineKind).
enum { CHAR_LINE_MODEL = 0, CHAR_LINE_META, CHAR_LINE_END };

// The steps (the tabs of before), in their order.
enum { CHAR_TAB_MODEL = 0, CHAR_TAB_DRIVER, CHAR_TAB_LOOK, CHAR_TAB_VOICES, CHAR_TAB_BUILD, CHAR_TABS };
// What the head of a tab shows.
enum { CHAR_STEP_TODO = 0, CHAR_STEP_OPTIONAL, CHAR_STEP_DONE, CHAR_STEP_WARNING, CHAR_STEP_PROBLEM };
// The cards of the Advanced section of the step Model (the tab Extras of
// before, and the options of the model): in the order of the IDs of their
// switch (CHAR_ID_EXTRAS) and of the automation verb "extras".
enum { CHAR_EXTRAS_IMPORT = 0, CHAR_EXTRAS_WHEELS, CHAR_EXTRAS_ANIM, CHAR_EXTRAS_OPTIONS, CHAR_EXTRAS_COUNT };
// The part of the step Model a control belongs to (Char_TabOfId, *card):
// CHAR_CARD_ALL on the step whatever is open, CHAR_CARD_BASIC only while
// Advanced is closed, CHAR_EXTRAS_* only on that card of Advanced.
#define CHAR_CARD_ALL   (-1)
#define CHAR_CARD_BASIC (-2)
#define CHAR_TIMER_ANIM    3      // the idle clip of a hi-res preview (Char_HiresTick)
#define CHAR_ANIM_TICK_MS  50
#define CHAR_ANIM_TICK_CAP 200    // the longest step of one tick
#define CHAR_WIZ_MSGS      48     // messages of RsCharHires_Messages at most
#define CHAR_STEP_MSGS_MIN_H 36   // the messages of a step, at least (one entry and the line "N more below")
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
static const wchar_t *const g_charTabWords[CHAR_TABS] = { L"model", L"driver", L"look", L"voices", L"build" };
static const wchar_t *const g_charTabTexts[CHAR_TABS] = { L"Model", L"Driver", L"In-game look", L"Voices", L"Build" };
static const wchar_t *const g_charStepWords[] = { L"to do", L"optional", L"done", L"warning", L"problem" };
static const wchar_t *const g_charExtrasWords[CHAR_EXTRAS_COUNT] = { L"import", L"wheels", L"animations", L"options" };
static const wchar_t *const g_charExtrasTexts[CHAR_EXTRAS_COUNT] = { L"Import", L"Wheels", L"Animations", L"Options" };
// The order of the cards on the switch of Advanced (the IDs keep the order above).
static const int g_charExtrasOrder[CHAR_EXTRAS_COUNT] = { CHAR_EXTRAS_OPTIONS, CHAR_EXTRAS_IMPORT, CHAR_EXTRAS_ANIM, CHAR_EXTRAS_WHEELS };
// The list "Vertex colors" of Advanced (a CHRG route): its entries in this
// order, the mode of each (RS_HIRES_VCOL_*).
#define CHAR_VMODES 3
static const int g_charVModeOrder[CHAR_VMODES] = { RS_HIRES_VCOL_PSX, RS_HIRES_VCOL_GLTF, RS_HIRES_VCOL_OFF };
static const wchar_t *const g_charVModeTexts[CHAR_VMODES] = { L"PSX - grey 128 is neutral (PS1 shading)", L"glTF - white is neutral",
                                                              L"Off - vertex colours ignored" };
#define CHAR_NOT_GLB_TEXT L"Low-Poly and Hi-Res need a .glb - export as glTF Binary."
#define CHAR_NOT_GLB_FIX  L"How to fix: in Blender, File > Export > glTF 2.0, Format \"glTF Binary (.glb)\", then choose that file."
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
#define CHAR_START_TEXT L"Pick a glTF (.glb), OBJ or PLY model of driver, steering wheel and kart. rldpack converts and checks it and builds the .rldchar."
#define CHAR_START_TEXT_GLB L"Pick the .glb (glTF Binary) of driver, steering wheel and kart. It is checked with the game's rules at once."
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
    struct RsAnimPoses poses;       // @pose lines and the messages about the poses (card Animations, rs_anim.c)
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
    unsigned long nativeTriangles;  // its triangles (the native model drawn in the preview), 0 = none
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
    // THE GUIDED FLOW: the route (RS_ROUTE_*), step 0 shown, its cards.
    int route;                      // RS_ROUTE_NONE until chosen
    int choosing;                   // step 0 "What do you want to build?" is shown
    HWND routeAsk, routeCard[RS_ROUTES], routeLabel, changeType;
    HWND kitOpen, kitStep, kitLink; // THE CREATOR KIT: step 0's button, the line of the step Model
    HBITMAP routeImage[RS_ROUTES];  // the pictures of the cards (Rs_RouteImage), NULL = none
    int routeImageW, routeImageH;   // the size they were made for
    int advOpen;                    // Advanced of the step Model is open
    HWND advanced, upscale, upscaleHelp;
    // THE UPSCALE of a CHRG route: the line below the box (CHAR_ID_UPSCALE_NOTE);
    // 1 = the check of the model suggests it (RsCharHires_SuggestUpscale), the
    // largest edge of its base colour textures in the file, 1 = the box is the
    // author's choice for this model.
    HWND upscaleNote;
    // The line on the step Model (RsCharHires_UpscaleStepLine) and "Change"
    // at its end; both hidden while the line is empty.
    HWND upscaleStep, upscaleChange;
    int upSuggested, upChosen;
    unsigned upLargest;
    // THE KART of a CHRG route: the box "Kart: from the player's game data" and
    // its help, the line below it, the line on the step Model with "Change";
    // 1 = the check of the model suggests the box (the kit's kart joint), 1 =
    // the box is the author's choice for this model.
    HWND gameKart, gameKartHelp, gameKartNote, gameKartStep, gameKartChange;
    int kartSuggested, kartChosen;
    // ... and of the route Vanilla (K6): the kart case of its model, a .glb
    // (RS_KART_*, RsHires_KartCaseFile; -1 = none: not a .glb, not read), the
    // model and its stamp it was read for.
    int vanKart;
    int vanSkins;                   // its skins: one or more - make-char gets --rig bind-pose (as the CHRG fallback)
    wchar_t vanKartPath[CHAR_VAL];
    unsigned long long vanKartStamp;
    // THE JPEG TEXTURES of a CHRG route: the box of Advanced and its help,
    // the line on the step Model with "Change" (empty and hidden while the
    // box is off), the button "Convert to PNG" (shown: pngFixOn); of the
    // check shown: 1 = it is of the model as converted (hiresPng), the JPEG
    // textures the conversion found, why it failed ("" = it did not).
    HWND png, pngHelp, pngStep, pngChange, pngFix;
    int pngFixOn;
    int pngJpeg;
    wchar_t pngWhy[200];
    // THE VERTEX COLOURS of a CHRG route: the switch and its line, the three
    // modes in Advanced; the mode (RS_HIRES_VCOL_*) the picture is drawn in
    // and the build passes, -1 = the model has no COLOR_0 (none of it shown,
    // no switch passed); the suggestion and its reason (RS_VCOL_WHY_*); 1 =
    // the mode is the author's choice.
    HWND vblend, vblendNote, vmodeLabel, vmode, vmodeHelp;
    int vcolMode, vcolSuggested, vcolWhy, vcolChosen;
    // The author's choices per model (Char_Remembered): per kind
    // (CHAR_CHOICE_*) the value chosen, -1 = none of that kind.
    struct { wchar_t path[CHAR_VAL]; int value[CHAR_CHOICE_KINDS]; } choice[CHAR_VCOL_CHOICES];
    int choiceNext;
    int chainVcol;                  // the mode the running build passes to hires-add, -1 = none
    HWND test, rebuild, testNote, outNote;
    int builtFresh;                 // the last build is of the fields as they are: Test in game is offered
    // A step shows its problems in its head (red, amber) only once it was
    // shown or Build was pressed; before, it is "still to do" (Char_TabsUpdate).
    int visited[CHAR_TABS];
    int buildTried;
    int headShort;                  // the heads use their short texts (Char_LayStrip: the long ones do not fit)
    int nameDerived;                // the name in the field was made from the model's file name (Char_NameFromModel)
    // The messages of the page, each with its step (and card); the list on
    // the step shows those of the step shown (Char_MsgRefill).
    struct CharPageMsg { int tab, card, severity; wchar_t *text, *detail; } *pageMsg;
    int pageMsgCount, pageMsgCap;
    int *listMsg;                   // entry of the list -> index into pageMsg
    int listCount;
    // A CHRG route: the in-process check of the .glb (RsHires_CheckFile), the
    // preview switch Low-Poly/Hi-Res | Classic fallback, the idle clip playing.
    struct RsHiresCheck hires;
    int hiresChecked;               // hires holds the check of hiresPath
    int hiresPng;                   // ... as converted to PNG ("Convert JPEG textures to PNG" was on)
    int hiresGameKart;              // ... with the author's choice of "Kart: from the player's game data" (-1 none, 0, 1)
    wchar_t hiresPath[CHAR_VAL];
    unsigned long long hiresStamp;
    HWND previewSwitch[2];
    int previewClassic;             // 1 = the classic fallback is shown, 0 = the hi-res model
    unsigned char *classicPv;       // the classic fallback's preview file of the check shown (Rs_Free)
    size_t classicPvBytes;
    int hiresShown;                 // the view shows the hi-res picture
    float animSeconds, animWheel;
    DWORD animTick;
    // THE BUILD OF A CHRG ROUTE: make-char -> classic (temp) -> [hires-png ->
    // chainPngPath (temp)] -> [hires-upscale -> derived (temp)] -> hires-add -> out.
    wchar_t chainClassic[CHAR_VAL], chainDerived[CHAR_VAL], chainOut[CHAR_VAL], chainModel[CHAR_VAL];
    wchar_t chainPngPath[CHAR_VAL];
    int chainStep;                  // CHAR_CHAIN_* of the running build, CHAR_CHAIN_NONE = a Vanilla build
    int chainPng;                   // the running chain converts JPEG textures to PNG first
    int chainUpscale;               // the running chain upscales
    unsigned chainParts;            // the parts field the running chain packs (RsHires_PartsFor)
    int chainWritten;               // @written of the last upscale or pack step
    wchar_t chainPath[CHAR_VAL], chainSha[80];
    struct CharChainMsg { int severity; wchar_t code[16]; wchar_t text[RS_HIRES_TEXT_MAX]; } chainMsg[RS_HIRES_ROWS_MAX];
    int chainMsgCount;
    // THE CLIPS of a CHRG route (Advanced, card Animations): the clip shown
    // (0 = the bind pose, 1 + n = clip n of RsHires_ClipName, -1 = the
    // start: idle where the clips can play, else the bind pose), whether it
    // plays, and the table being filled (its clicks are no choice then).
    HWND clipLabel, clipCombo, clipPlay, clipPrev, clipNext, clipTime, clipTable, clipNote;
    int clipSel, clipPlaying;
    // THE CLASSIC MODEL of a CHRG route: 1 = it comes from a finished
    // character (the field classicFile; hires-add into it), 0 = make-char
    // makes it from the .glb (the start). chainOwnClassic: the running build
    // packs into that file (nothing made, nothing deleted); buildFromFile:
    // the build that ended did so (its headline).
    HWND classicLabel, classicMode, classicFile, classicBrowse, classicHelp, classicStep, classicChange;
    int classicFrom;
    // Without make-char (Char_NoMake) the look of the preview (shadow,
    // exhaust) is the character file's: read by Char_ChrgNoMake from
    // lookFile (RsHires_CharLook); fileLookKnown 0 = it could not be read.
    // charNote: the card Options of a finished character in the model field
    // says why it has nothing to set.
    struct RsCharLook fileLook;
    int fileLookKnown;
    wchar_t lookFile[CHAR_VAL];
    HWND charNote;
    int chainOwnClassic, buildFromFile;
    HWND importNote, vcolorsLabel, vcolors, vcolorsHelp;  // the card Import of Advanced
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
    int *msgCard;                   // and its card (Advanced on the step Model, In-game look), -1 = none
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
    unsigned long previewNativeTris;    // triangles of the native model in the preview (view-bench)
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
static int Char_Remembered(const wchar_t *path, int kind);
static void Char_ObjUpdate(HWND page);
static void Char_NativeResult(wchar_t **f, int n);
static void Char_VoiceSelShow(void);
static int Char_VoicesHeard(int *clips, int *events);
static void Char_VoiceColumns(int width);
static int Char_ExtrasShown(int c);
static int Char_WheelsCard(void);
static void Char_HiresStop(void);
static void Char_HiresShow(void);
static void Char_ClassicShow(void);
static void Char_ClassicKeep(const wchar_t *path);
static void Char_HiresEnsure(const wchar_t *model);
static void Char_HiresForget(void);
static void Char_ClipFill(void);
static void Char_ClipControls(void);
static void Char_PreviewShow(void);
static void Char_HiresTick(HWND page);
static void Char_PreviewSwitch(HWND page, int classic);
static void Char_NameFilter(void);
static void Char_FieldPath(HWND edit, wchar_t *out, int cap);
static int Char_EndsWith(const wchar_t *path, const wchar_t *ending);

// ---------------------------------------------------------------------------
// The guided flow (rs_charroute.h): which route, which cards, which controls
// ---------------------------------------------------------------------------

// 1 = the route chosen builds the chunk CHRG (Low-Poly, Hi-Res).
static int Char_Chrg(void)
{
    const struct RsRoutePreset *p = RsRoute_Preset(g_char.route);
    return p && p->chrg;
}

// The name of the model a CHRG route builds, as its card says it
// ("Low-Poly", "Hi-Res"): the first side of the switch above the preview,
// the notes below the preview and the line of the build name it.
static const wchar_t *Char_ChrgTitle(void)
{
    const struct RsRoutePreset *p = RsRoute_Preset(g_char.route);
    return p && p->chrg ? p->title : L"Hi-Res";
}

// The two sides of the switch above the preview of a CHRG route: the
// route's model | Classic fallback.
static const wchar_t *Char_PreviewText(int classic)
{
    return classic ? L"Classic fallback" : Char_ChrgTitle();
}

// The tip of "Classic fallback" on that switch, naming the route's model.
static void Char_PreviewTip(void)
{
    wchar_t tip[160];
    swprintf(tip, 160, L"The classic model made from the same file: the game draws it wherever the %ls model is not drawn.",
             Char_ChrgTitle());
    Rs_SetTip(g_char.previewSwitch[1], tip);
}

// Show kart wheels: on the step Model itself for Vanilla. A CHRG route has no
// such choice - the classic fallback gets --wheels off by itself when the
// hi-res model brings its own four wheels (Char_MakeArgs), so the control is
// hidden there (Char_RouteHides).
static int Char_WheelsCard(void)
{
    return CHAR_CARD_BASIC;
}

// THE CLASSIC MODEL of a CHRG route comes from a finished character
// (Advanced, card Options) instead of make-char.
static int Char_ClassicFromFile(void)
{
    return Char_Chrg() && g_char.classicFrom;
}

// A CHRG route whose model field holds a finished character (.rldchar): its
// CHRG is checked and shown, nothing is built from it (THE FINISHED CHARACTER).
static int Char_ModelIsCharacter(void)
{
    wchar_t model[CHAR_VAL];
    if (!Char_Chrg() || !g_char.model)
        return 0;
    Char_FieldPath(g_char.model, model, CHAR_VAL);
    return Char_EndsWith(model, L".rldchar");
}

// THE KART of the route Vanilla (K6): the kart case of its model when the box
// has a place there - a .glb read (RsHires_KartCaseFile), not with a kart part
// of its own, and the card Options of Advanced shown on Vanilla (the box lives
// there) - else -1. The same rule as a CHRG route: the kit's kart joint
// without a kart (RS_KART_GAME) suggests the box.
static int Char_VanillaKart(void)
{
    if (g_char.route != RS_ROUTE_VANILLA || g_char.vanKart < 0 || g_char.vanKart == RS_KART_PART ||
        !Char_ExtrasShown(CHAR_EXTRAS_OPTIONS))
        return -1;
    return g_char.vanKart;
}

// THE KART: the box "Kart: from the player's game data" has a place - a CHRG
// route, a .glb model, and not a model with a kart part of its own (that wins:
// the check says so as a message); on Vanilla Char_VanillaKart.
static int Char_KartBoxWanted(void)
{
    if (!Char_Chrg())
        return Char_VanillaKart() >= 0;
    if (Char_ModelIsCharacter())
        return 0;
    return !(g_char.hiresChecked && g_char.hires.read && g_char.hires.kartCase == RS_KART_PART);
}

// No make-char on this route as it is set: the steps Driver, In-game look and
// Voices have nothing to set (their values are those of the character file).
static int Char_NoMake(void)
{
    return Char_ClassicFromFile() || Char_ModelIsCharacter();
}

// The cards of Advanced a route shows. Vanilla: Options (the repair, the open
// parts, the remesh and the reduction - not in the user mode, which fixes
// them), Import, Animations, and Wheels only with --enable-preview-features
// (a preview feature of the native OBJ model). A CHRG route: Options
// ("Upscale small textures", the classic model, ...) and Animations (the
// clips of the model, THE CLIPS - not the shape keys of Vanilla's card).
static int Char_ExtrasShown(int c)
{
    if (Char_Chrg())
        return c == CHAR_EXTRAS_OPTIONS || c == CHAR_EXTRAS_ANIM;
    switch (c) {
    case CHAR_EXTRAS_OPTIONS: return !Rs_NativeForUsers();
    case CHAR_EXTRAS_WHEELS: return g_rsPreviewFeatures != 0;
    case CHAR_EXTRAS_IMPORT:
    case CHAR_EXTRAS_ANIM: return 1;
    }
    return 0;
}

static int Char_ExtrasCount(void)
{
    int c, n = 0;
    for (c = 0; c < CHAR_EXTRAS_COUNT; c++)
        n += Char_ExtrasShown(c);
    return n;
}

// The card of Advanced shown is one the route has: else its first.
static void Char_ExtrasFix(void)
{
    int i;
    if (Char_ExtrasShown(g_char.extrasCard))
        return;
    for (i = 0; i < CHAR_EXTRAS_COUNT; i++)
        if (Char_ExtrasShown(g_charExtrasOrder[i])) {
            g_char.extrasCard = g_charExtrasOrder[i];
            return;
        }
}

// THE CREATOR KIT: its line on the step Model is wanted on every route
// (Vanilla, Low-Poly, Hi-Res) while the model field is empty (spaces only
// count as empty).
static int Char_KitHint(void)
{
    wchar_t model[8];
    int i;
    if (g_char.route < 0 || g_char.route >= RS_ROUTES || !g_char.model)
        return 0;
    GetWindowTextW(g_char.model, model, 8);
    for (i = 0; model[i]; i++)
        if (model[i] != L' ' && model[i] != L'\t')
            return 0;
    return 1;
}

// The controls a route does not need: hidden (not greyed out). Vanilla has
// no "Upscale small textures" and no switch Low-Poly/Hi-Res | Classic fallback; a CHRG
// route has no size (the hi-res model keeps its own size), no repair or
// reduction choices (its fallback is made with RsCharHires_FallbackArgs), no
// Show kart wheels (above), none of the OBJ's choices (the textures folder,
// the list of what was read, the card Import), no card Animations (the shape
// keys of the classic model: the hi-res model plays its own clips) and no
// card Wheels.
static int Char_RouteHides(HWND c)
{
    int id = GetDlgCtrlID(c);
    // (Vanilla keeps "Kart: from the player's game data": Char_KartBoxWanted.)
    if (!Char_Chrg())
        return c == g_char.upscale || c == g_char.upscaleHelp || c == g_char.upscaleNote || c == g_char.upscaleStep ||
               c == g_char.upscaleChange || (id >= CHAR_ID_PNG && id <= CHAR_ID_PNG_FIX) || id == CHAR_ID_PREVIEW_SWITCH ||
               id == CHAR_ID_PREVIEW_SWITCH + 1 ||
               (id >= CHAR_ID_VBLEND && id <= CHAR_ID_VMODE_HELP) || (id >= CHAR_ID_CLIP_LABEL && id <= CHAR_ID_CHAR_NOTE);
    return (id >= CHAR_ID_SIZE_LABEL && id <= CHAR_ID_OPTIONS_LABEL) || (id >= CHAR_ID_REPAIR && id <= CHAR_ID_QUALITY) ||
           id == CHAR_ID_REDUCE_FIT || id == CHAR_ID_MODEL_IMPORT || (id >= CHAR_ID_MODEL_FILES && id <= CHAR_ID_TEXTURES_CLEAR) ||
           (id >= CHAR_ID_FALLBACK && id <= CHAR_ID_NATIVE_LINE) || (id >= CHAR_ID_IMPORT_FIRST && id <= CHAR_ID_IMPORT_LAST) ||
           (id >= CHAR_ID_ANIM_FIRST && id <= CHAR_ID_ANIM_LAST) || (id >= CHAR_ID_WHEELS_FIRST && id <= CHAR_ID_WHEELS_LAST);
}

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
        // The notes of the guided flow wrap all the same (their steps have
        // the room): "Upscale small textures", its line and its line on the
        // step Model, the same of "Convert JPEG textures to PNG", where the
        // build goes, what Test in game did.
        if (g_char.compact && lineH > 0 && label != g_char.headline && label != g_char.upscaleHelp && label != g_char.upscaleNote &&
            label != g_char.upscaleStep && label != g_char.gameKartHelp && label != g_char.gameKartNote &&
            label != g_char.gameKartStep && label != g_char.pngHelp && label != g_char.pngStep && label != g_char.outNote &&
            label != g_char.testNote) {
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
        // The model: a PLY, an OBJ (its MTL comes as @model mtl as well) or a glTF.
        if (wcscmp(Char_Field(f, n, 1), L"ply") == 0 || wcscmp(Char_Field(f, n, 1), L"obj") == 0 ||
            wcscmp(Char_Field(f, n, 1), L"gltf") == 0 || wcscmp(Char_Field(f, n, 1), L"glb") == 0) {
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
        if (wcscmp(Char_Field(f, n, 1), L"shape-key") == 0)
            CharAnim_ShapeKey(&j->poses, Char_Field(f, n, 2), Char_Field(f, n, 3));
        Char_ImportLine(f, n);
    } else if (wcscmp(kind, L"pose") == 0) {
        CharAnim_PoseLine(&j->poses, f, n);
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
        CharAnim_PoseMsg(&j->poses, Char_Field(f, n, 2), text);
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
// preview, the bar). *card: on the tab In-game look its card; on the step
// Model CHAR_CARD_ALL (the model field, Advanced), CHAR_CARD_BASIC (shown
// while Advanced is closed) or the card of Advanced (CHAR_EXTRAS_*); -1
// elsewhere. Show kart wheels moves with the route (Char_WheelsCard).
static int Char_WheelsCard(void);
static int Char_TabOfId(int id, int *card)
{
    *card = -1;
    if ((id >= CHAR_ID_MODEL_LABEL && id <= CHAR_ID_MODEL_BROWSE) || id == CHAR_ID_ADVANCED)
        return CHAR_TAB_MODEL;
    if (id == CHAR_ID_WHEELS) {
        *card = Char_WheelsCard();
        return CHAR_TAB_MODEL;
    }
    if ((id >= CHAR_ID_REPAIR && id <= CHAR_ID_QUALITY) || id == CHAR_ID_REDUCE || id == CHAR_ID_UPSCALE ||
        id == CHAR_ID_UPSCALE_HELP || id == CHAR_ID_UPSCALE_NOTE || (id >= CHAR_ID_GAMEKART && id <= CHAR_ID_GAMEKART_NOTE) ||
        (id >= CHAR_ID_VMODE_LABEL && id <= CHAR_ID_VMODE_HELP) ||
        id == CHAR_ID_PNG || id == CHAR_ID_PNG_HELP) {
        *card = CHAR_EXTRAS_OPTIONS;
        return CHAR_TAB_MODEL;
    }
    if (id >= CHAR_ID_CLIP_LABEL && id <= CHAR_ID_CLIP_NOTE) {
        *card = CHAR_EXTRAS_ANIM;
        return CHAR_TAB_MODEL;
    }
    if ((id >= CHAR_ID_CLASSIC_LABEL && id <= CHAR_ID_CLASSIC_HELP) || id == CHAR_ID_CHAR_NOTE) {
        *card = CHAR_EXTRAS_OPTIONS;
        return CHAR_TAB_MODEL;
    }
    if (id == CHAR_ID_KIT_STEP || id == CHAR_ID_KIT_LINK) {
        *card = CHAR_CARD_BASIC;
        return CHAR_TAB_MODEL;
    }
    if (id == CHAR_ID_VBLEND || id == CHAR_ID_VBLEND_NOTE || id == CHAR_ID_UPSCALE_STEP || id == CHAR_ID_UPSCALE_CHANGE ||
        id == CHAR_ID_GAMEKART_STEP || id == CHAR_ID_GAMEKART_CHANGE || (id >= CHAR_ID_PNG_STEP && id <= CHAR_ID_PNG_FIX) || id == CHAR_ID_CLASSIC_STEP || id == CHAR_ID_CLASSIC_CHANGE) {
        *card = CHAR_CARD_BASIC;
        return CHAR_TAB_MODEL;
    }
    // Reduce to fit (the button, over the limit) is the fix of the model's
    // error: on the step itself, not behind Advanced.
    if (id == CHAR_ID_MODEL_INFO || (id >= CHAR_ID_SIZE_LABEL && id <= CHAR_ID_OPTIONS_LABEL) || id == CHAR_ID_MODEL_IMPORT ||
        id == CHAR_ID_REDUCE_FIT ||
        (id >= CHAR_ID_MODEL_FILES && id <= CHAR_ID_TEXTURES_CLEAR) || (id >= CHAR_ID_FALLBACK && id <= CHAR_ID_NATIVE_LINE)) {
        *card = CHAR_CARD_BASIC;
        return CHAR_TAB_MODEL;
    }
    if ((id >= CHAR_ID_OUT_LABEL && id <= CHAR_ID_SHOW && id != CHAR_ID_MESSAGES) || id == CHAR_ID_TEST ||
        id == CHAR_ID_REBUILD || id == CHAR_ID_TEST_NOTE || id == CHAR_ID_OUT_NOTE)
        return CHAR_TAB_BUILD;
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
    if (id >= CHAR_ID_EXTRAS && id < CHAR_ID_EXTRAS + CHAR_EXTRAS_COUNT) {
        *card = CHAR_CARD_ALL;          // the switch of Advanced: Char_Wanted
        return CHAR_TAB_MODEL;
    }
    if (id >= CHAR_ID_WHEELS_FIRST && id <= CHAR_ID_WHEELS_LAST) {
        *card = CHAR_EXTRAS_WHEELS;
        return CHAR_TAB_MODEL;
    }
    if (id >= CHAR_ID_ANIM_FIRST && id <= CHAR_ID_ANIM_LAST) {
        *card = CHAR_EXTRAS_ANIM;
        return CHAR_TAB_MODEL;
    }
    if (id >= CHAR_ID_IMPORT_FIRST && id <= CHAR_ID_IMPORT_LAST) {
        *card = CHAR_EXTRAS_IMPORT;
        return CHAR_TAB_MODEL;
    }
    return -1;
}

// The card of Advanced an @msg id belongs to, -1 = none: the choices of
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
    // The poses and shape keys (a rig is refused there: use shape keys), the
    // wheels and their axles.
    if (wcsncmp(code, L"pose", 4) == 0 || wcsncmp(code, L"morph-", 6) == 0 || wcscmp(code, L"model-rig") == 0)
        return CHAR_EXTRAS_ANIM;
    if (wcsncmp(code, L"wheel-", 6) == 0 || wcsncmp(code, L"axle-", 5) == 0 || wcscmp(code, L"rear-wheel") == 0)
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
    if (!code || !code[0])
        return -1;
    if (wcscmp(code, L"write") == 0)
        return CHAR_TAB_BUILD;
    if (wcsncmp(code, L"icon", 4) == 0 || wcscmp(code, L"map-color") == 0 || wcscmp(code, L"shadow") == 0 ||
        wcscmp(code, L"exhaust") == 0)
        return CHAR_TAB_LOOK;
    if (wcsncmp(code, L"voice", 5) == 0)
        return CHAR_TAB_VOICES;
    if (wcsncmp(code, L"name", 4) == 0 || wcscmp(code, L"author") == 0 || wcscmp(code, L"class") == 0 ||
        wcscmp(code, L"template") == 0 || wcsncmp(code, L"mask", 4) == 0)
        return CHAR_TAB_DRIVER;
    return CHAR_TAB_MODEL;      // its card of Advanced, if any: Char_CardOfCode
}

// THE MESSAGES AT THEIR PLACE: every message of the page keeps its step (and
// its card); the list on a step shows only those of that step, below the
// step's fields - a message without a step (rldpack stopped, could not
// start) on the steps Model and Build. A step without messages shows no list.
// The number in the head of each step (red, amber, green) counts them as
// before (Char_CountTabs).
static int Char_MsgOnStep(int tab, int step)
{
    return tab == step || (tab < 0 && (step == CHAR_TAB_MODEL || step == CHAR_TAB_BUILD));
}

// The same for an entry of pageMsg: with Advanced open on the step Model only
// those of the card shown (the choices of Import, the poses of Animations,
// the wheels); Basic shows every message of the model.
static int Char_MsgShown(const struct CharPageMsg *m)
{
    if (!Char_MsgOnStep(m->tab, g_char.tab))
        return 0;
    if (g_char.tab == CHAR_TAB_MODEL && g_char.advOpen)
        return m->tab == CHAR_TAB_MODEL && m->card == g_char.extrasCard;
    return 1;
}

static void Char_Relayout(HWND page);

// The list of the step shown, from pageMsg.
static void Char_MsgRefill(void)
{
    int i, had = g_char.listCount;
    Rs_MsgListClear(g_char.msgs);
    g_char.listCount = 0;
    if (!g_char.listMsg && g_char.pageMsgCap)
        g_char.listMsg = Rs_Alloc((size_t)g_char.pageMsgCap * sizeof(int));
    for (i = 0; i < g_char.pageMsgCount; i++) {
        const struct CharPageMsg *m = &g_char.pageMsg[i];
        if (!Char_MsgShown(m))
            continue;
        g_char.listMsg[g_char.listCount++] = i;
        Rs_MsgListAdd(g_char.msgs, m->severity, m->text, m->detail);
    }
    // The list comes or goes with its first or last entry: laid out again.
    if ((had > 0) != (g_char.listCount > 0) && g_char.msgs)
        Char_Relayout(GetParent(g_char.msgs));
}

static void Char_MsgClear(void)
{
    int i;
    for (i = 0; i < g_char.pageMsgCount; i++) {
        Rs_Free(g_char.pageMsg[i].text);
        Rs_Free(g_char.pageMsg[i].detail);
    }
    g_char.pageMsgCount = 0;
    g_char.msgTabCount = 0;
    Char_MsgRefill();
}

// card: of Advanced (Char_CardOfCode) or of the tab In-game look, -1 = none.
static void Char_MsgAddCard(int tab, int card, int severity, const wchar_t *text, const wchar_t *detail)
{
    if (g_char.pageMsgCount == g_char.pageMsgCap) {
        int cap = g_char.pageMsgCap ? g_char.pageMsgCap * 2 : 32;
        struct CharPageMsg *m = Rs_Alloc((size_t)cap * sizeof(*m));
        if (g_char.pageMsgCount)
            memcpy(m, g_char.pageMsg, (size_t)g_char.pageMsgCount * sizeof(*m));
        Rs_Free(g_char.pageMsg);
        g_char.pageMsg = m;
        g_char.pageMsgCap = cap;
        Rs_Free(g_char.listMsg);
        g_char.listMsg = Rs_Alloc((size_t)cap * sizeof(int));
        // listMsg is filled anew below (Char_MsgRefill)
        g_char.listCount = 0;
        Rs_MsgListClear(g_char.msgs);
        {
            int i;
            for (i = 0; i < g_char.pageMsgCount; i++)
                if (Char_MsgShown(&g_char.pageMsg[i])) {
                    g_char.listMsg[g_char.listCount++] = i;
                    Rs_MsgListAdd(g_char.msgs, g_char.pageMsg[i].severity, g_char.pageMsg[i].text, g_char.pageMsg[i].detail);
                }
        }
    }
    g_char.pageMsg[g_char.pageMsgCount].tab = tab;
    g_char.pageMsg[g_char.pageMsgCount].card = (tab == CHAR_TAB_MODEL || tab == CHAR_TAB_LOOK) ? card : -1;
    g_char.pageMsg[g_char.pageMsgCount].severity = severity;
    g_char.pageMsg[g_char.pageMsgCount].text = Rs_Dup(text ? text : L"");
    g_char.pageMsg[g_char.pageMsgCount].detail = detail ? Rs_Dup(detail) : NULL;
    g_char.pageMsgCount++;
    if (Char_MsgShown(&g_char.pageMsg[g_char.pageMsgCount - 1])) {
        g_char.listMsg[g_char.listCount++] = g_char.pageMsgCount - 1;
        Rs_MsgListAdd(g_char.msgs, severity, text, detail);
        if (g_char.listCount == 1 && g_char.msgs)
            Char_Relayout(GetParent(g_char.msgs));
    }
    // The old index of tabs (the report, "problem <n>") follows pageMsg.
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
    g_char.msgCard[g_char.msgTabCount++] = (tab == CHAR_TAB_MODEL || tab == CHAR_TAB_LOOK) ? card : -1;
}

static void Char_MsgAdd(int tab, int severity, const wchar_t *text, const wchar_t *detail)
{
    Char_MsgAddCard(tab, -1, severity, text, detail);
}

// A message of the list opens its step (and its card of Advanced).
static void Char_MsgOpen(HWND page, int i);

// The errors and warnings of the last run per tab, for the heads.
static void Char_CountTabs(void)
{
    int i;
    // The messages of the page as they are shown (after Char_ShowCheckResult or
    // Char_BuildDone filled them): a message without a step counts nowhere.
    memset(g_char.tabErrors, 0, sizeof(g_char.tabErrors));
    memset(g_char.tabWarnings, 0, sizeof(g_char.tabWarnings));
    for (i = 0; i < g_char.pageMsgCount; i++) {
        int t = g_char.pageMsg[i].tab;
        if (t < 0)
            continue;
        if (g_char.pageMsg[i].severity == RS_SEV_ERROR)
            g_char.tabErrors[t]++;
        else if (g_char.pageMsg[i].severity == RS_SEV_WARNING)
            g_char.tabWarnings[t]++;
    }
}

// What each head shows: the errors and warnings of the last run first - of a
// step not shown yet (and before Build was pressed) only "to do", so that a
// name not typed yet is no red head right after the model -, then whether
// the step is filled in. Model and Driver are needed, the others optional.
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
        const int seen = g_char.visited[t] || g_char.buildTried;
        if (g_char.tabErrors[t] && !seen)
            step = CHAR_STEP_TODO;      // not shown yet: no error before the author was there
        else if (g_char.tabErrors[t])
            step = CHAR_STEP_PROBLEM;
        else if (g_char.tabWarnings[t] && seen)
            step = CHAR_STEP_WARNING;
        else if (Char_NoMake() && (t == CHAR_TAB_DRIVER || t == CHAR_TAB_LOOK || t == CHAR_TAB_VOICES))
            step = CHAR_STEP_DONE;      // the character file holds them
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
        else if (t == CHAR_TAB_BUILD)
            step = g_char.builtFresh ? CHAR_STEP_DONE : CHAR_STEP_TODO;
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
    // Back on the first step goes back to the choice of the type; the last
    // step has no Next (Char_Wanted hides it).
    EnableWindow(g_char.back, TRUE);
    EnableWindow(g_char.next, g_char.tab < CHAR_TABS - 1);
}

// The text of the head of step t: Voices says that it is optional (Next skips
// it). Where the heads do not fit with their whole texts (Char_LayStrip,
// headShort) the short ones: "Look", and Voices without "(optional)" - its
// outlined number and the line at the top of the step say it then.
static const wchar_t *Char_HeadText(int t)
{
    if (t == CHAR_TAB_VOICES)
        return g_char.headShort ? L"Voices" : L"Voices (optional)";
    if (t == CHAR_TAB_LOOK && g_char.headShort)
        return L"Look";
    return g_charTabTexts[t];
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
    DrawTextW(dc, Char_HeadText(t), -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, oldFont);
    if ((di->itemState & ODS_FOCUS) && !(di->itemState & ODS_NOFOCUSRECT)) {
        r = rc;
        InflateRect(&r, -Rs_Px(4), -Rs_Px(5));
        SetTextColor(dc, RS_COL_TEXT);
        DrawFocusRect(dc, &r);
    }
}

// Options | Import | Animations | Wheels in Advanced of the step Model, Portrait | In the race in
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

// The pictures of the three cards, made for w x h (Rs_RouteImage, rs_cards.c)
// when that size is new; NULL where none can be made (the card draws a
// neutral frame with its title instead).
static void Char_RouteImages(int w, int h)
{
    int r;
    if (w <= 0 || h <= 0 || (w == g_char.routeImageW && h == g_char.routeImageH))
        return;
    for (r = 0; r < RS_ROUTES; r++) {
        if (g_char.routeImage[r])
            DeleteObject(g_char.routeImage[r]);
        g_char.routeImage[r] = Rs_RouteImage(r, w, h);
        InvalidateRect(g_char.routeCard[r], NULL, FALSE);
    }
    g_char.routeImageW = w;
    g_char.routeImageH = h;
}

// A card of step 0: the example picture (or a neutral frame with the title),
// the title, the one sentence. The border is the accent colour where the
// focus is or for the route chosen before.
static void Char_DrawRouteCard(const DRAWITEMSTRUCT *di, int r)
{
    const struct RsRoutePreset *p = RsRoute_Preset(r);
    HDC dc = di->hDC;
    RECT rc = di->rcItem, pic, t;
    const int pad = Rs_Px(12), focus = (di->itemState & ODS_FOCUS) != 0;
    const int marked = focus || r == g_char.route || (di->itemState & ODS_SELECTED);
    HBRUSH br;
    HPEN pen;
    HGDIOBJ oldBr, oldPen, oldFont;

    br = CreateSolidBrush(RS_COL_PAGE);
    FillRect(dc, &rc, br);
    DeleteObject(br);
    br = CreateSolidBrush(RS_COL_CARD);
    pen = CreatePen(PS_SOLID, marked ? Rs_Px(2) : 1, marked ? RS_COL_ACCENT : RS_COL_BORDER);
    oldBr = SelectObject(dc, br);
    oldPen = SelectObject(dc, pen);
    RoundRect(dc, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1, Rs_Px(14), Rs_Px(14));
    SelectObject(dc, oldBr);
    SelectObject(dc, oldPen);
    DeleteObject(br);
    DeleteObject(pen);
    SetRect(&pic, rc.left + pad, rc.top + pad, rc.right - pad, rc.top + pad + (rc.bottom - rc.top) * 55 / 100);
    SetBkMode(dc, TRANSPARENT);
    if (g_char.routeImage[r]) {
        HDC mem = CreateCompatibleDC(dc);
        BITMAP bm;
        HGDIOBJ old = SelectObject(mem, g_char.routeImage[r]);
        GetObjectW(g_char.routeImage[r], sizeof(bm), &bm);
        BitBlt(dc, pic.left + ((pic.right - pic.left) - bm.bmWidth) / 2, pic.top + ((pic.bottom - pic.top) - bm.bmHeight) / 2,
               bm.bmWidth, bm.bmHeight, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteDC(mem);
    } else {
        // No picture: a neutral frame with the title, so that the card reads
        // the same without one.
        br = CreateSolidBrush(RS_COL_PAGE);
        FillRect(dc, &pic, br);
        DeleteObject(br);
        br = CreateSolidBrush(RS_COL_BORDER);
        FrameRect(dc, &pic, br);
        DeleteObject(br);
        oldFont = SelectObject(dc, Rs_Font(RS_FONT_TITLE));
        SetTextColor(dc, RS_COL_MUTED);
        DrawTextW(dc, p ? p->title : L"", -1, &pic, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, oldFont);
    }
    SetRect(&t, rc.left + pad, pic.bottom + Rs_Px(10), rc.right - pad, pic.bottom + Rs_Px(40));
    oldFont = SelectObject(dc, Rs_Font(RS_FONT_SECTION));
    SetTextColor(dc, RS_COL_TEXT);
    DrawTextW(dc, p ? p->title : L"", -1, &t, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, Rs_Font(RS_FONT_BODY));
    SetTextColor(dc, RS_COL_MUTED);
    SetRect(&t, rc.left + pad, t.bottom, rc.right - pad, rc.bottom - pad);
    DrawTextW(dc, p ? p->sentence : L"", -1, &t, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, oldFont);
    if (focus && !(di->itemState & ODS_NOFOCUSRECT)) {
        t = rc;
        InflateRect(&t, -Rs_Px(5), -Rs_Px(5));
        SetTextColor(dc, RS_COL_TEXT);
        DrawFocusRect(dc, &t);
    }
}

// THE CREATOR KIT's button of step 0 (K10): the kit's icon, "Creator Kit" in
// the font of the card titles and below it, small, "Templates to get
// started"; framed as the cards, the accent colour where the focus is.
#define CHAR_KIT_TITLE L"Creator Kit"
#define CHAR_KIT_SUB   L"Templates to get started"
#define CHAR_KIT_ICON  L"\xE7B8"            // Package (Segoe MDL2 Assets), the kit's module icon

// The height of one line of a font (RS_FONT_*), or of the large icon font (-1).
static int Char_FontLine(int font)
{
    HDC dc = GetDC(NULL);
    TEXTMETRICW tm;
    HGDIOBJ old = SelectObject(dc, font < 0 ? Rs_IconFont(1) : Rs_Font(font));
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
    return tm.tmHeight;
}

static int Char_FontWidth(int font, const wchar_t *text)
{
    HDC dc = GetDC(NULL);
    SIZE sz = { 0, 0 };
    HGDIOBJ old = SelectObject(dc, font < 0 ? Rs_IconFont(1) : Rs_Font(font));
    GetTextExtentPoint32W(dc, text, (int)wcslen(text), &sz);
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
    return sz.cx;
}

// What the button needs: padding, the icon, the wider of its two lines; the
// two lines one above the other.
static void Char_KitButtonSize(int *w, int *h)
{
    const int padX = Rs_Px(14), padY = Rs_Px(6), iconGap = Rs_Px(10);
    int tw = Char_FontWidth(RS_FONT_SECTION, CHAR_KIT_TITLE), sw = Char_FontWidth(RS_FONT_SMALL, CHAR_KIT_SUB);
    *w = padX + Char_FontWidth(-1, CHAR_KIT_ICON) + iconGap + (tw > sw ? tw : sw) + padX;
    *h = padY + Char_FontLine(RS_FONT_SECTION) + Char_FontLine(RS_FONT_SMALL) + padY;
}

static void Char_DrawKitButton(const DRAWITEMSTRUCT *di)
{
    HDC dc = di->hDC;
    RECT rc = di->rcItem, t;
    const int focus = (di->itemState & ODS_FOCUS) != 0, pressed = (di->itemState & ODS_SELECTED) != 0;
    const int padX = Rs_Px(14), iconGap = Rs_Px(10);
    const int titleH = Char_FontLine(RS_FONT_SECTION), subH = Char_FontLine(RS_FONT_SMALL);
    int iconW = Char_FontWidth(-1, CHAR_KIT_ICON), y;
    HBRUSH br;
    HPEN pen;
    HGDIOBJ oldBr, oldPen, oldFont;

    br = CreateSolidBrush(RS_COL_PAGE);
    FillRect(dc, &rc, br);
    DeleteObject(br);
    br = CreateSolidBrush(RS_COL_CARD);
    pen = CreatePen(PS_SOLID, focus || pressed ? Rs_Px(2) : 1, focus || pressed ? RS_COL_ACCENT : RS_COL_BORDER);
    oldBr = SelectObject(dc, br);
    oldPen = SelectObject(dc, pen);
    RoundRect(dc, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1, Rs_Px(14), Rs_Px(14));
    SelectObject(dc, oldBr);
    SelectObject(dc, oldPen);
    DeleteObject(br);
    DeleteObject(pen);
    SetBkMode(dc, TRANSPARENT);
    // The icon, in the middle of the button's height.
    SetRect(&t, rc.left + padX, rc.top, rc.left + padX + iconW, rc.bottom);
    oldFont = SelectObject(dc, Rs_IconFont(1));
    SetTextColor(dc, RS_COL_ACCENT);
    DrawTextW(dc, CHAR_KIT_ICON, -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    // The two lines, together in the middle of the height.
    y = rc.top + ((rc.bottom - rc.top) - titleH - subH) / 2;
    SetRect(&t, rc.left + padX + iconW + iconGap, y, rc.right - padX, y + titleH);
    SelectObject(dc, Rs_Font(RS_FONT_SECTION));
    SetTextColor(dc, RS_COL_TEXT);
    DrawTextW(dc, CHAR_KIT_TITLE, -1, &t, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SetRect(&t, t.left, t.bottom, t.right, t.bottom + subH);
    SelectObject(dc, Rs_Font(RS_FONT_SMALL));
    SetTextColor(dc, RS_COL_MUTED);
    DrawTextW(dc, CHAR_KIT_SUB, -1, &t, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, oldFont);
    if (focus && !(di->itemState & ODS_NOFOCUSRECT)) {
        t = rc;
        InflateRect(&t, -Rs_Px(4), -Rs_Px(4));
        SetTextColor(dc, RS_COL_TEXT);
        DrawFocusRect(dc, &t);
    }
}

// A head (or a switch, or a card of step 0): an owner-drawn button, so that a
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
    g_char.visited[t] = 1;
    for (i = 0; i < CHAR_TABS; i++)
        InvalidateRect(g_char.tabHead[i], NULL, FALSE);
    Char_MsgRefill();           // the messages of this step
    Char_Relayout(page);
    Char_UpdateButtons();
    f = GetFocus();
    while (f && GetParent(f) && GetParent(f) != page)
        f = GetParent(f);       // the edit field of a combo box
    if (focus || (f && GetParent(f) == page && (!Char_IsShown(f) || !IsWindowEnabled(f))))
        SetFocus(g_char.tabHead[t]);
}

// The card of Advanced (the step Model).
static void Char_SelectExtras(HWND page, int c)
{
    int i;
    if (c < 0 || c >= CHAR_EXTRAS_COUNT || !Char_ExtrasShown(c))
        return;
    g_char.extrasCard = c;
    for (i = 0; i < CHAR_EXTRAS_COUNT; i++)
        InvalidateRect(g_char.extrasSwitch[i], NULL, FALSE);
    Char_MsgRefill();
    Char_Relayout(page);
}

// i: index into pageMsg. A message of a card of Advanced opens it (a card the
// route shows: Char_ExtrasShown).
static int Char_ExtrasShown(int c);
static void Char_MsgOpen(HWND page, int i)
{
    if (i < 0 || i >= g_char.msgTabCount || g_char.msgTab[i] < 0)
        return;
    if (g_char.msgTab[i] == CHAR_TAB_LOOK && g_char.msgCard[i] >= 0) {
        int c;
        g_char.lookCard = g_char.msgCard[i];
        for (c = 0; c < CHAR_LOOK_CARDS; c++)
            InvalidateRect(g_char.lookSwitch[c], NULL, FALSE);
    } else if (g_char.msgTab[i] == CHAR_TAB_MODEL && g_char.msgCard[i] >= 0 && Char_ExtrasShown(g_char.msgCard[i])) {
        int c;
        g_char.extrasCard = g_char.msgCard[i];
        g_char.advOpen = 1;
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
    EnableWindow(g_char.rebuild, Char_CanBuild());
    EnableWindow(g_char.test, g_char.builtFresh && !g_char.jobId);
    EnableWindow(g_char.modelBrowse, !building);
    EnableWindow(g_char.iconBrowse, !building);
    EnableWindow(g_char.voicesBrowse, !building);
    EnableWindow(g_char.texturesBrowse, !building);
    EnableWindow(g_char.outBrowse, !building);
    EnableWindow(g_char.show, !building);
    // Build, or after a build of the fields as they are Test in game (the
    // one main action) with Build again beside it.
    if (g_char.build && !g_char.choosing && g_char.tab == CHAR_TAB_BUILD) {
        int fresh = g_char.builtFresh != 0;
        if (Char_IsShown(g_char.build) == fresh || Char_IsShown(g_char.test) != fresh)
            Char_Relayout(GetParent(g_char.build));
    }
    Char_VoiceSelShow();
    Char_TabsUpdate();
}

// "Show details": the raw output of rldpack in the step Build in place of its
// messages (the command first, then every line), "Hide details" back.
static void Char_ShowView(void)
{
    if (g_char.showRaw)
        Char_RawRefresh();
    Rs_SetText(g_char.rawToggle, g_char.showRaw ? L"Hide details" : L"Show details");
    if (g_char.raw)
        Char_Relayout(GetParent(g_char.raw));
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

// Height of a list (report view) for this many rows: its head, the rows, the
// border - measured, so that the last row is not cut at any DPI or font.
static int Char_ListHeight(HWND list, int rows)
{
    HWND head = ListView_GetHeader(list);
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
    if (ListView_GetItemCount(list) > 0 && ListView_GetItemRect(list, 0, &rc, LVIR_BOUNDS))
        rowH = rc.bottom - rc.top;
    if (rowH <= 0) {
        TEXTMETRICW tm;
        HDC dc = GetDC(list);
        HGDIOBJ old = SelectObject(dc, (HFONT)SendMessageW(list, WM_GETFONT, 0, 0));
        GetTextMetricsW(dc, &tm);
        SelectObject(dc, old);
        ReleaseDC(list, dc);
        rowH = tm.tmHeight + Rs_Px(4);
    }
    return headH + rows * rowH + 2 * GetSystemMetrics(SM_CYBORDER) + Rs_Px(2);
}

static int Char_ImportListHeight(int rows)
{
    return Char_ListHeight(g_char.modelFiles, rows);
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
    // A glTF is read into the same lines as an OBJ (no MTL: its materials are inside).
    int gltf = wcscmp(g_char.importFormat, L"gltf") == 0 || wcscmp(g_char.importFormat, L"glb") == 0;
    int obj = wcscmp(g_char.importFormat, L"obj") == 0 || gltf;
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
    Char_Copy(out, cap, gltf ? L"glTF" : obj ? L"OBJ" : L"PLY");
    if (obj) {
        t[0] = 0;
        if (mtl > 1)
            swprintf(t, 160, L": %d of %d MTL files found", mtlOk, mtl);
        else if (mtl == 1 && wcscmp(mtlState, L"none") == 0 && !gltf)
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

// The model is an OBJ - or a glTF, which make-char reads into the same data
// as an OBJ (its materials, textures and UVs; the same choices, the same
// native model): by what the last check of this model read, else by its name.
static int Char_ModelIsObj(void)
{
    wchar_t model[CHAR_VAL];
    Char_FieldPath(g_char.model, model, CHAR_VAL);
    if (g_char.importFormat[0] && model[0] && _wcsicmp(model, g_char.checkModel) == 0)
        return wcscmp(g_char.importFormat, L"obj") == 0 || wcscmp(g_char.importFormat, L"gltf") == 0 ||
               wcscmp(g_char.importFormat, L"glb") == 0;
    return Char_EndsWith(model, L".obj") || Char_EndsWith(model, L".glb") || Char_EndsWith(model, L".gltf");
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
#define CHAR_NATIVE_TEXT_ON  L"Preview: also writes the OBJ's own mesh, UVs and textures (CNET, CTXT; each texture a power of two from 16 to 2048) beside the classic model; needs Show kart wheels off or a wheel model (Advanced, Wheels). The game draws it with NATIVE DRIVERS set to PREVIEW (OPTIONS, GRAPHICS)."
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

// The preview follows the fields at once - without make-char (Char_NoMake)
// it shows the look of the character file instead (Char_ChrgNoMake read it).
static void Char_LookPreview(void)
{
    int quad[4], pts[2][3], count = 0, shadow = 0, i;
    if (!g_char.view)
        return;
    if (Char_NoMake()) {
        const struct RsCharLook *l = &g_char.fileLook;
        memset(quad, 0, sizeof(quad));
        memset(pts, 0, sizeof(pts));
        if (!g_char.fileLookKnown) {
            RsView_SetLook(g_char.view, 0, quad, 0, pts, 0);
            return;
        }
        if (l->shadow == RS_CHAR_LOOK_RETAIL) {
            memcpy(quad, g_charRetailQuad, sizeof(quad));
            shadow = 1;
        } else if (l->shadow == RS_CHAR_LOOK_OWN) {
            memcpy(quad, l->quad, sizeof(quad));
            shadow = 1;
        }
        if (l->exhaust == RS_CHAR_LOOK_RETAIL) {
            memcpy(pts, g_charRetailPoints, sizeof(pts));
            count = 2;
        } else if (l->exhaust == RS_CHAR_LOOK_OWN) {
            memcpy(pts, l->point, sizeof(pts));
            count = l->count;
        }
        RsView_SetLook(g_char.view, shadow, quad, count, pts, l->exhaust == RS_CHAR_LOOK_RETAIL);
        return;
    }
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
#define CHAR_NATIVE_TEXT_WHEELS L"Classic model only: the native model needs Show kart wheels off or a wheel model (Advanced, Wheels)."

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
    // win and lose are built only into the native model.
    CharAnim_NativeState(Char_NativePassed());
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
    g_charJob.nativeTriangles = wcstoul(f[3], NULL, 10);
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
        return L"Classic look, as the game draws it. Textures of your own need an OBJ model with its MTL file (Native model, Advanced, Import).";
    if (Char_NativePassed())
        return L"Classic look: the check brought no native model - see the message list.";
    if (Char_NativeOn())
        return L"Classic look: the native model needs Show kart wheels off (tab Model) or a wheel model (Advanced, Wheels).";
    if (g_char.previewTextured)
        return L"Classic look: your textures are only baked into the colours of the triangles. Tick Native model (Advanced, Import) to build them.";
    return L"Classic look, as the game draws it. Tick Native model (Advanced, Import) for the native model.";
}

// The note after a check shown, a change of the card Wheels or of the view's
// toggles. Only while a model is shown: the other notes (no model, errors)
// stay as they are.
static void Char_ChrgViewNote(void);
static void Char_ViewNoteUpdate(void)
{
    wchar_t one[384], wheelNames[256];
    int wheelMissing, native = 0, missing;

    if (Char_Chrg()) {
        Char_ChrgViewNote();
        return;
    }
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
        Char_ViewNotePart(L"Your wheels show here but are not built (Advanced, Wheels says why).");
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
    // A CHRG route keeps the classic fallback's picture for the switch
    // Low-Poly/Hi-Res | Classic fallback (Char_PreviewShow follows in Char_CheckDone).
    if (Char_Chrg())
        Char_ClassicKeep(Rs_FileExists(path) ? path : NULL);
    // The label above the model: the view's own for Vanilla; the classic
    // picture without the stage of the hi-res one.
    RsView_SetModelLabel(g_char.view, Char_Chrg() ? L"Classic fallback" : NULL);
    if (!Char_Chrg())
        RsView_SetStage(g_char.view, 0);
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
    // The animation the card Animations shows goes on in the new file.
    CharAnim_PreviewLoaded();
    // The preview shows the colours of the palette as they are - the game's
    // brightest case. In the race the ground under the kart darkens the
    // driver (game/COLL.c sets alphaScale from the track's colour there,
    // RenderBucket fades towards black): 0.25 + luma / 128 of the colour on
    // ground darker than luma 96, at most 75 % darker.
    // The note (Char_ViewNoteUpdate): the textures of the model and those
    // not found, by their file names, once each.
    g_char.previewNative = RsView_HasNative(g_char.view);
    g_char.previewNativeTris = g_char.previewNative ? g_charJob.nativeTriangles : 0ul;
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
                                         : L"press Reduce to fit, or reduce it in Blender";
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

// THE MESSAGES OF A CHRG ROUTE: those of RsCharHires_Messages at their place
// (the model, its animations, its wheels, its size on the step Model; the
// build on the step Build), each with its "How to fix" and rule ID; a line
// when the model could not be read; of the classic fallback's make-char only
// its errors about the model ("Classic fallback: ..."; its warnings, notes and
// infos about the model - the reduction, the fit, rig-bind-pose - stay behind
// Show details) and, as for Vanilla, everything it says about the other
// steps (name, icon, voices). Returns the errors.
// The part of the hi-res check alone (also without make-char, Char_ChrgNoMake).
static int Char_ChrgHiresMessages(void)
{
    struct RsWizMsg *w = Rs_Alloc(CHAR_WIZ_MSGS * sizeof(*w));
    wchar_t text[2 * RS_WIZ_TEXT_MAX + 32], detail[RS_WIZ_TEXT_MAX + 48];
    int errors = 0, n, i;

    if (!g_char.hires.read) {
        Char_MsgAdd(CHAR_TAB_MODEL, RS_SEV_ERROR, g_char.hires.why[0] ? g_char.hires.why : L"The model could not be read.",
                    Char_ModelIsCharacter() ? L"How to fix: choose a character built on the route Low-Poly or Hi-Res, or the .glb it was made from."
                                            : L"How to fix: export the model again as glTF Binary (.glb) and choose that file.");
        errors++;
    } else {
        // "Convert JPEG textures to PNG" on, and the conversion failed: its
        // reason in place of the JPEG line (whose fix is the button).
        if (g_char.hiresPng && g_char.pngWhy[0]) {
            swprintf(text, 2 * RS_WIZ_TEXT_MAX + 32, L"Your JPEG textures could not be converted to PNG: %ls.", g_char.pngWhy);
            Char_MsgAdd(CHAR_TAB_MODEL, RS_SEV_ERROR, text,
                        L"How to fix: save the textures as PNG (glTF export: Images > PNG) and export again (HRG-16).");
            errors++;
        }
        n = RsCharHires_Messages(&g_char.hires, g_char.route, w, CHAR_WIZ_MSGS);
        for (i = 0; i < n; i++) {
            if (g_char.hiresPng && g_char.pngWhy[0] && RsCharHires_IsJpegMsg(&w[i]))
                continue;
            if (w[i].fix[0])
                swprintf(detail, RS_WIZ_TEXT_MAX + 48, L"How to fix: %ls%ls%ls%ls", w[i].fix, w[i].rule[0] ? L" (" : L"", w[i].rule,
                         w[i].rule[0] ? L")" : L"");
            else
                Char_Copy(detail, RS_WIZ_TEXT_MAX + 48, w[i].rule);
            Char_MsgAdd(w[i].place == RS_WIZ_PLACE_BUILD ? CHAR_TAB_BUILD : CHAR_TAB_MODEL, w[i].severity, w[i].text,
                        detail[0] ? detail : NULL);
            errors += w[i].severity == RS_SEV_ERROR;
        }
    }
    Rs_Free(w);
    return errors;
}

static int Char_ChrgMessages(int exitCode, int makeOk)
{
    static const int order[] = { RS_SEV_ERROR, RS_SEV_WARNING, RS_SEV_NOTE, RS_SEV_INFO, RS_SEV_OK };
    wchar_t text[2 * RS_WIZ_TEXT_MAX + 32];
    int errors = Char_ChrgHiresMessages(), i, s;

    errors += Char_AddRunProblems(exitCode, makeOk);
    for (s = 0; s < (int)(sizeof(order) / sizeof(order[0])); s++) {
        for (i = 0; i < g_charJob.msgCount; i++) {
            const struct CharMsg *m = &g_charJob.msgs[i];
            int tab = Char_TabOfCode(m->code);
            if (m->severity != order[s] || (tab == CHAR_TAB_MODEL && m->severity != RS_SEV_ERROR))
                continue;
            if (tab == CHAR_TAB_MODEL)
                swprintf(text, 2 * RS_WIZ_TEXT_MAX + 32, L"Classic fallback: %ls", m->text ? m->text : L"");
            else
                Char_Copy(text, 2 * RS_WIZ_TEXT_MAX + 32, m->text ? m->text : L"");
            Char_MsgAddCard(tab, tab == CHAR_TAB_LOOK ? Char_LookCardOfCode(m->code) : -1, m->severity, text, m->detail);
            errors += m->severity == RS_SEV_ERROR;
        }
    }
    return errors;
}

// The line below the model field of a CHRG route: what the check of the
// .glb found, against the route's targets and the game's budget - the
// triangles, the textures (the largest edge, how many, their memory against
// the budget), the clips, the wheels and the skin.
static void Char_ChrgInfo(void)
{
    const struct RsHiresCheck *c = &g_char.hires;
    const struct RsRoutePreset *p = RsRoute_Preset(g_char.route);
    wchar_t t[640], tris[2][24], skin[32], finish[32];
    unsigned long long target;
    if (!c->read) {
        Char_SetInfo(c->why[0] ? c->why : L"The model could not be read.", RS_COL_ERROR);
        return;
    }
    target = p && p->triangleTarget ? p->triangleTarget : c->budgetTriangles;
    Char_Grouped(tris[0], 24, (long)c->triangles);
    Char_Grouped(tris[1], 24, (long)target);
    if (c->joints)
        swprintf(skin, 32, L"skin of %u joint%ls", c->joints, c->joints == 1 ? L"" : L"s");
    else
        Char_Copy(skin, 32, L"no skin");
    // The optional finish clips only when the file has one.
    finish[0] = 0;
    if (c->finishClips > 0)
        swprintf(finish, 32, L" (+ %d finish)", c->finishClips);
    swprintf(t, 640, L"%ls triangles of %ls%ls - textures up to %u px, %d texture%ls, %.1f of %.1f MiB - %d of %d animations%ls - %ls - %ls",
             tris[0], tris[1], p && p->triangleTarget && p->triangleTarget < c->budgetTriangles ? L" (target)" : L"", c->textureEdge,
             c->textures, c->textures == 1 ? L"" : L"s", (double)c->textureBytes / 1048576.0, (double)c->budgetTextureBytes / 1048576.0,
             c->clips, RS_HIRES_CLIPS_BASE, finish,
             c->wheels == 4 ? L"its own wheels" : c->parts == RS_HIRES_PARTS_GAME_KART ? L"the game's kart and wheels" : L"standard wheels",
             skin);
    Char_SetInfo(t, c->usable ? RS_COL_TEXT : RS_COL_ERROR);
}

static void Char_ShowCheckResult(int exitCode)
{
    struct CharJobData *j = &g_charJob;
    int ok = j->resultSeen && wcscmp(j->resultState, L"checked") == 0;
    wchar_t t[256];
    int errors, over;

    if (Char_Chrg())
        ok = ok && g_char.hires.read && g_char.hires.usable;
    g_char.checked = ok;
    Char_Copy(g_char.checkedPath, CHAR_VAL, j->resultPath);
    Char_VoiceNote();           // it says an error of the voices
    Char_MsgClear();
    if (Char_Chrg()) {
        errors = Char_ChrgMessages(exitCode, j->resultSeen && wcscmp(j->resultState, L"checked") == 0);
        Char_ChrgInfo();
    } else {
        errors = Char_AddRunProblems(exitCode, ok);
        errors += Char_CountMsgs(RS_SEV_ERROR);
        Char_AddMsgs(RS_SEV_ERROR);
        Char_AddMsgs(RS_SEV_WARNING);
        Char_AddMsgs(RS_SEV_NOTE);
        Char_AddMsgs(RS_SEV_INFO);
        Char_AddMsgs(RS_SEV_OK);
    }
    Char_CountTabs();
    Char_EmptyText(L"rldpack reported nothing.");
    over = !ok && !Char_Chrg() && Char_OverLimit(t, 256);
    if (ok) {
        Char_Headline(L"Ready to build", RS_COL_OK);
    } else if (over) {
        Char_Headline(t, RS_COL_ERROR);
    } else {
        // Where they are: the first step whose head is red.
        int s;
        for (s = 0; s < CHAR_TABS && !g_char.tabErrors[s]; s++) {
        }
        swprintf(t, 256, L"Cannot build yet - %d problem(s)%ls%ls", errors, s < CHAR_TABS ? L", see the step " : L"",
                 s < CHAR_TABS ? g_charTabTexts[s] : L"");
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
// A CHRG route: the check of the .glb and its preview
// ---------------------------------------------------------------------------

// THE CHECK OF A CHRG ROUTE is twofold and has no code of its own: the .glb
// with the game's rules in this process (RsHires_CheckFile, rs_hires_model.c,
// include/rldhires.inc - rldpack hires-check runs the same), and make-char --check
// of the classic fallback made from the same file (the page's own switches,
// then RsCharHires_FallbackArgs: Char_MakeArgs). The .glb is read again only
// when it is another file or was written since (its stamp).
//
// THE JPEG TEXTURES: with "Convert JPEG textures to PNG" on, the file checked
// is the model as rldpack hires-png converts it (Rs_HiresPngFile, the same
// code in this process) - written to Rs_TempDir as <model>-png.glb, read by
// the check and deleted again; the build converts once more (Char_ChainStart),
// so no temp file outlives the check. hiresPath stays the author's model (the
// choices per model and the view's key go by it).
static void Char_HiresEnsure(const wchar_t *model)
{
    unsigned long long stamp = Char_StampFile(CHAR_STAMP_START, model);
    const int png = Char_IsChecked(g_char.png);
    const int gameKart = Char_Remembered(model, CHAR_CHOICE_GAMEKART);
    wchar_t dir[CHAR_VAL], beside[CHAR_VAL], converted[CHAR_VAL];
    if (g_char.hiresChecked && _wcsicmp(model, g_char.hiresPath) == 0 && stamp == g_char.hiresStamp && png == g_char.hiresPng &&
        gameKart == g_char.hiresGameKart)
        return;
    Char_HiresStop();
    RsHires_CheckFree(&g_char.hires);
    memset(&g_char.hires, 0, sizeof(g_char.hires));
    g_char.pngJpeg = 0;
    g_char.pngWhy[0] = 0;
    converted[0] = 0;
    if (png) {
        int jpeg = 0, done = 0;
        Rs_TempDir(dir, CHAR_VAL);
        Rs_PathJoin(beside, CHAR_VAL, dir, L"check.rldchar");
        if (!RsHires_PngPath(model, beside, converted, CHAR_VAL) || _wcsicmp(converted, model) == 0) {
            converted[0] = 0;
            Char_Copy(g_char.pngWhy, 200, L"the path of the converted model is too long");
        } else if (!Rs_HiresPngFile(model, converted, &jpeg, &done, g_char.pngWhy, 200)) {
            converted[0] = 0;
            if (!g_char.pngWhy[0])
                Char_Copy(g_char.pngWhy, 200, L"the conversion failed");
        }
        g_char.pngJpeg = jpeg;
        if (Rs_Automating()) {
            if (converted[0])
                Rs_AutoLog(L"  png convert: %d JPEG texture(s) converted for the check (rldpack hires-png -> %ls)", done,
                           Rs_PathName(converted));
            else
                Rs_AutoLog(L"  png convert: failed - %ls", g_char.pngWhy);
        }
    }
    RsHires_CheckFile(converted[0] ? converted : model, &g_char.hires);
    // THE KART: the author's choice of "Kart: from the player's game data"
    // against what the model suggests - checked with the parts it is packed
    // with (the game's kart: HRG-42 instead of HRG-20), the case kept.
    if (gameKart >= 0 && g_char.hires.read && !g_char.hires.fromCharacter && g_char.hires.kartCase != RS_KART_PART &&
        RsHires_PartsFor(RS_KART_OWN, gameKart) != g_char.hires.parts) {
        RsHires_CheckFree(&g_char.hires);
        memset(&g_char.hires, 0, sizeof(g_char.hires));
        RsHires_CheckFileParts(converted[0] ? converted : model, RsHires_PartsFor(RS_KART_OWN, gameKart), &g_char.hires);
    }
    if (converted[0])
        DeleteFileW(converted);
    // THE CLIPS: another model starts at idle (or its bind pose); the same
    // one, checked again, keeps the clip chosen.
    if (_wcsicmp(model, g_char.hiresPath) != 0)
        g_char.clipSel = -1;
    g_char.hiresChecked = 1;
    g_char.hiresPng = png;
    g_char.hiresGameKart = gameKart;
    g_char.hiresStamp = stamp;
    Char_Copy(g_char.hiresPath, CHAR_VAL, model);
    g_char.animSeconds = 0.0f;
    g_char.animWheel = 0.0f;
    Char_ClipFill();
    if (Rs_Automating()) {
        if (g_char.hires.read)
            Rs_AutoLog(L"  hires check: %ls - %llu triangles, %d of %d clips, %d wheel nodes, %d error(s), %d warning(s)",
                       g_char.hires.usable ? L"usable" : L"refused", g_char.hires.triangles, g_char.hires.clips, RS_HIRES_CLIPS_BASE,
                       g_char.hires.wheels, g_char.hires.errors, g_char.hires.warnings);
        else
            Rs_AutoLog(L"  hires check: not read - %ls", g_char.hires.why);
    }
}

// The check of another model or none: the hi-res check goes.
static void Char_HiresForget(void)
{
    Char_HiresStop();
    RsHires_CheckFree(&g_char.hires);
    memset(&g_char.hires, 0, sizeof(g_char.hires));
    g_char.hiresChecked = 0;
    g_char.hiresPng = 0;
    g_char.pngJpeg = 0;
    g_char.pngWhy[0] = 0;
    g_char.hiresPath[0] = 0;
    g_char.hiresShown = 0;
    g_char.clipSel = -1;
    Char_ClipFill();
    Char_ClipControls();
    Rs_Free(g_char.classicPv);
    g_char.classicPv = NULL;
    g_char.classicPvBytes = 0;
}

// THE AUTHOR'S CHOICES PER MODEL (the vertex colour mode, "Upscale small
// textures"): the value of a kind (CHAR_CHOICE_*) the author chose for a
// model, -1 = none.
static int Char_Remembered(const wchar_t *path, int kind)
{
    int i;
    for (i = 0; path && path[0] && i < CHAR_VCOL_CHOICES; i++)
        if (g_char.choice[i].path[0] && _wcsicmp(g_char.choice[i].path, path) == 0)
            return g_char.choice[i].value[kind];
    return -1;
}

// Remembers a choice of a kind (value -1: forgets it; a model with no choice
// left is forgotten), the oldest model of CHAR_VCOL_CHOICES goes.
static void Char_Remember(const wchar_t *path, int kind, int value)
{
    int i, k, any = 0;
    if (!path || !path[0])
        return;
    for (i = 0; i < CHAR_VCOL_CHOICES; i++)
        if (g_char.choice[i].path[0] && _wcsicmp(g_char.choice[i].path, path) == 0)
            break;
    if (value < 0) {
        if (i == CHAR_VCOL_CHOICES)
            return;
        g_char.choice[i].value[kind] = -1;
        for (k = 0; k < CHAR_CHOICE_KINDS; k++)
            any |= g_char.choice[i].value[k] >= 0;
        if (!any)
            g_char.choice[i].path[0] = 0;
        return;
    }
    if (i == CHAR_VCOL_CHOICES) {
        i = g_char.choiceNext;
        g_char.choiceNext = (g_char.choiceNext + 1) % CHAR_VCOL_CHOICES;
        Char_Copy(g_char.choice[i].path, CHAR_VAL, path);
        for (k = 0; k < CHAR_CHOICE_KINDS; k++)
            g_char.choice[i].value[k] = -1;
    }
    g_char.choice[i].value[kind] = value;
}

// The switch, the list of Advanced and the line as the mode is.
static void Char_VcolControls(void)
{
    wchar_t line[RS_WIZ_TEXT_MAX];
    int i;
    if (g_char.vcolMode < 0)
        return;
    SendMessageW(g_char.vblend, BM_SETCHECK,
                 g_char.vcolMode == RS_HIRES_VCOL_PSX ? BST_CHECKED : g_char.vcolMode == RS_HIRES_VCOL_OFF ? BST_UNCHECKED : BST_INDETERMINATE, 0);
    for (i = 0; i < CHAR_VMODES; i++)
        if (g_charVModeOrder[i] == g_char.vcolMode)
            SendMessageW(g_char.vmode, CB_SETCURSEL, (WPARAM)i, 0);
    RsCharHires_ColourLine(g_char.vcolMode, g_char.vcolSuggested, g_char.vcolWhy, g_char.route, line, RS_WIZ_TEXT_MAX);
    Rs_SetText(g_char.vblendNote, line);
}

// After the hi-res check of a CHRG route (a new one or the one kept, also
// for another route): the suggestion, and the mode - the author's choice for
// this model, or the suggestion - for the picture and the build.
static void Char_VcolUpdate(void)
{
    const int was = g_char.vcolMode;
    int why = RS_VCOL_WHY_NONE, s = -1, chosen;
    // A finished character keeps the mode its CHRG names: no switch.
    if (Char_Chrg() && g_char.hiresChecked && g_char.hires.read && !g_char.hires.fromCharacter)
        s = RsCharHires_SuggestColours(&g_char.hires, g_char.route, &why);
    g_char.vcolSuggested = s;
    g_char.vcolWhy = why;
    if (s < 0) {
        g_char.vcolMode = -1;
        g_char.vcolChosen = 0;
    } else {
        chosen = Char_Remembered(g_char.hiresPath, CHAR_CHOICE_VCOL);
        g_char.vcolChosen = chosen >= 0;
        g_char.vcolMode = chosen >= 0 ? chosen : s;
        RsHires_SetColourMode(&g_char.hires, g_char.vcolMode);
        Char_VcolControls();
    }
    if ((was < 0) != (g_char.vcolMode < 0) && g_char.view)
        Char_Relayout(GetParent(g_char.view));
    if (Rs_Automating() && s >= 0 && was != g_char.vcolMode)
        Rs_AutoLog(L"  vertex colors: %ls (%ls; suggested %ls)", RsHires_ColourWord(g_char.vcolMode),
                   g_char.vcolChosen ? L"the author's choice" : L"the suggestion", RsHires_ColourWord(s));
}

// The author (or the automation) sets a mode: remembered for this model, the
// picture drawn again at once, the messages anew (the check's result kept:
// rldpack does not run again). Returns what Char_Check returns.
static int Char_VcolSet(HWND page, int mode)
{
    if (g_char.vcolMode < 0 || !RsHires_ColourWord(mode))
        return 0;
    Char_Remember(g_char.hiresPath, CHAR_CHOICE_VCOL, mode);
    g_char.vcolChosen = 1;
    g_char.vcolMode = mode;
    g_char.builtFresh = 0;
    if (RsHires_SetColourMode(&g_char.hires, mode) && g_char.hiresShown && !g_char.previewClassic)
        Char_HiresShow();
    Char_VcolControls();
    Char_Relayout(page);
    return Char_Check(page);
}

// THE UPSCALE: the line below the box and the line on the step Model as the
// box and the suggestion are; the page laid out again when a line came, went
// or changed (the step line's "Change" sits at the end of its text).
static void Char_UpscaleControls(void)
{
    wchar_t line[RS_WIZ_TEXT_MAX], was[RS_WIZ_TEXT_MAX], step[RS_WIZ_TEXT_MAX], wasStep[RS_WIZ_TEXT_MAX];
    const int on = Char_IsChecked(g_char.upscale);
    RsCharHires_UpscaleLine(on, g_char.upSuggested, g_char.upLargest, line, RS_WIZ_TEXT_MAX);
    RsCharHires_UpscaleStepLine(g_char.route, on, g_char.upSuggested, g_char.upLargest, step, RS_WIZ_TEXT_MAX);
    GetWindowTextW(g_char.upscaleNote, was, RS_WIZ_TEXT_MAX);
    GetWindowTextW(g_char.upscaleStep, wasStep, RS_WIZ_TEXT_MAX);
    Rs_SetText(g_char.upscaleNote, line);
    Rs_SetText(g_char.upscaleStep, step);
    if (((was[0] != 0) != (line[0] != 0) || wcscmp(wasStep, step) != 0) && g_char.view)
        Char_Relayout(GetParent(g_char.view));
}

// After the hi-res check of a CHRG route (as Char_VcolUpdate, also for
// another route): the suggestion (RsCharHires_SuggestUpscale) and the box -
// the author's choice for this model, or the suggestion. Without a check (no
// model, not a .glb) there is no suggestion and the box stays as it is (the
// route's start: off).
static void Char_UpscaleUpdate(void)
{
    const int was = Char_IsChecked(g_char.upscale);
    int chosen, on;
    unsigned largest = 0;
    if (!Char_Chrg() || !g_char.hiresChecked || !g_char.hires.read || g_char.hires.fromCharacter) {
        g_char.upSuggested = 0;
        g_char.upChosen = 0;
        g_char.upLargest = 0;
        Char_UpscaleControls();
        return;
    }
    g_char.upSuggested = RsCharHires_SuggestUpscale(&g_char.hires, g_char.route, &largest);
    g_char.upLargest = largest;
    chosen = Char_Remembered(g_char.hiresPath, CHAR_CHOICE_UPSCALE);
    g_char.upChosen = chosen >= 0;
    on = chosen >= 0 ? chosen : g_char.upSuggested;
    Char_SetChecked(g_char.upscale, on);
    Char_UpscaleControls();
    if (Rs_Automating() && on != was)
        Rs_AutoLog(L"  upscale small textures: %ls (%ls; suggested %ls, largest base colour texture %u px)", on ? L"on" : L"off",
                   g_char.upChosen ? L"the author's choice" : L"the suggestion", g_char.upSuggested ? L"on" : L"off", largest);
}

// The author (the box) or the automation sets "Upscale small textures": kept
// for this model when one is checked. It changes only the build (hires-upscale
// before packing), not the check.
static void Char_UpscaleSet(int on)
{
    Char_SetChecked(g_char.upscale, on);
    if (Char_Chrg() && g_char.hiresChecked && g_char.hiresPath[0]) {
        Char_Remember(g_char.hiresPath, CHAR_CHOICE_UPSCALE, on != 0);
        g_char.upChosen = 1;
    }
    g_char.builtFresh = 0;
    Char_UpdateButtons();
    Char_UpscaleControls();
}

// "Change" at the end of a line on the step Model: Advanced opens at its
// card Options, the box gets the keyboard focus and is scrolled into view.
static void Char_OptionsChange(HWND page, HWND box)
{
    int c;
    // A CHRG route, or Vanilla with "Kart: from the player's game data" (K6).
    if ((!Char_Chrg() && !(box == g_char.gameKart && Char_KartBoxWanted())) || !Char_ExtrasShown(CHAR_EXTRAS_OPTIONS))
        return;
    g_char.advOpen = 1;
    g_char.extrasCard = CHAR_EXTRAS_OPTIONS;
    for (c = 0; c < CHAR_EXTRAS_COUNT; c++)
        InvalidateRect(g_char.extrasSwitch[c], NULL, FALSE);
    Rs_SetText(g_char.advanced, L"Hide advanced");
    Char_MsgRefill();
    Char_Relayout(page);
    SetFocus(box);
    Rs_ScrollIntoView(box);
}

static void Char_UpscaleChange(HWND page)
{
    Char_OptionsChange(page, g_char.upscale);
}

// THE KART: the line below the box and the line on the step Model as the box
// and the suggestion are; the page laid out again when a line came, went or
// changed (as Char_UpscaleControls).
static void Char_GameKartControls(void)
{
    wchar_t line[RS_WIZ_TEXT_MAX], was[RS_WIZ_TEXT_MAX], step[RS_WIZ_TEXT_MAX], wasStep[RS_WIZ_TEXT_MAX];
    const int on = Char_IsChecked(g_char.gameKart);
    line[0] = 0;
    step[0] = 0;
    if (Char_KartBoxWanted()) {
        RsCharHires_GameKartLine(on, g_char.kartSuggested, line, RS_WIZ_TEXT_MAX);
        RsCharHires_GameKartStepLine(on, g_char.kartSuggested, g_char.route, step, RS_WIZ_TEXT_MAX);
    }
    GetWindowTextW(g_char.gameKartNote, was, RS_WIZ_TEXT_MAX);
    GetWindowTextW(g_char.gameKartStep, wasStep, RS_WIZ_TEXT_MAX);
    Rs_SetText(g_char.gameKartNote, line);
    Rs_SetText(g_char.gameKartStep, step);
    if (((was[0] != 0) != (line[0] != 0) || wcscmp(wasStep, step) != 0) && g_char.view)
        Char_Relayout(GetParent(g_char.view));
}

// THE KART of the route Vanilla (K6): the kart case of its model, read again
// when the file is another one or changed; a model that is no .glb has none.
static void Char_VanillaKartEnsure(const wchar_t *model)
{
    unsigned long long stamp;
    if (!model[0] || !Char_EndsWith(model, L".glb")) {
        g_char.vanKart = -1;
        g_char.vanSkins = 0;
        g_char.vanKartPath[0] = 0;
        g_char.vanKartStamp = 0;
        return;
    }
    stamp = Char_StampFile(CHAR_STAMP_START, model);
    if (g_char.vanKartPath[0] && _wcsicmp(model, g_char.vanKartPath) == 0 && stamp == g_char.vanKartStamp)
        return;
    g_char.vanKart = RsHires_KartCaseFile(model, &g_char.vanSkins);
    Char_Copy(g_char.vanKartPath, CHAR_VAL, model);
    g_char.vanKartStamp = stamp;
}

// After the hi-res check of a CHRG route (as Char_UpscaleUpdate), or after the
// kart case of a Vanilla model was read (Char_VanillaKartEnsure, K6): the
// suggestion (the model has the kit's kart joint, RS_KART_GAME) and the box -
// the author's choice for this model, or the suggestion. A model with a kart
// part of its own (RS_KART_PART) or without a check has no box and no
// suggestion: the box is off.
static void Char_GameKartUpdate(void)
{
    const int was = Char_IsChecked(g_char.gameKart);
    static int wasWanted = -1;      // the box had a place at the last check
    int chosen, on, wanted, kase;
    const wchar_t *path;
    if (Char_Chrg()) {
        kase = !g_char.hiresChecked || !g_char.hires.read || g_char.hires.fromCharacter ? -1 : g_char.hires.kartCase;
        path = g_char.hiresPath;
    } else {
        kase = Char_VanillaKart();
        path = g_char.vanKartPath;
    }
    if (kase < 0 || kase == RS_KART_PART) {
        g_char.kartSuggested = 0;
        g_char.kartChosen = 0;
        Char_SetChecked(g_char.gameKart, 0);
        Char_GameKartControls();
        wanted = Char_KartBoxWanted();
        if (wanted != wasWanted && g_char.view)
            Char_Relayout(GetParent(g_char.view));   // the box itself came or went
        wasWanted = wanted;
        return;
    }
    g_char.kartSuggested = kase == RS_KART_GAME;
    chosen = Char_Remembered(path, CHAR_CHOICE_GAMEKART);
    g_char.kartChosen = chosen >= 0;
    on = chosen >= 0 ? chosen : g_char.kartSuggested;
    Char_SetChecked(g_char.gameKart, on);
    Char_GameKartControls();
    wanted = Char_KartBoxWanted();
    if ((wanted != wasWanted || on != was) && g_char.view)
        Char_Relayout(GetParent(g_char.view));
    wasWanted = wanted;
    if (Rs_Automating() && on != was)
        Rs_AutoLog(L"  game kart: %ls (%ls; suggested %ls)", on ? L"on" : L"off",
                   g_char.kartChosen ? L"the author's choice" : L"the suggestion", g_char.kartSuggested ? L"on" : L"off");
}

// The author (the box) or the automation sets "Kart: from the player's game
// data": kept for this model when one is checked. It changes the build
// (--game-kart template) and the check: the model is checked again with the
// parts it is packed with (Char_HiresEnsure).
static void Char_GameKartSet(HWND page, int on)
{
    Char_SetChecked(g_char.gameKart, on);
    if (Char_Chrg() && g_char.hiresChecked && g_char.hiresPath[0]) {
        Char_Remember(g_char.hiresPath, CHAR_CHOICE_GAMEKART, on != 0);
        g_char.kartChosen = 1;
    } else if (!Char_Chrg() && Char_VanillaKart() >= 0) {
        Char_Remember(g_char.vanKartPath, CHAR_CHOICE_GAMEKART, on != 0);
        g_char.kartChosen = 1;
    }
    g_char.builtFresh = 0;
    Char_UpdateButtons();
    Char_GameKartControls();
    Char_Check(page);
}

static void Char_GameKartChange(HWND page)
{
    Char_OptionsChange(page, g_char.gameKart);
}

// What the next build packs, as the parts field: the kart part or the game's
// kart (RsHires_PartsFor) from the model checked and the box; a finished
// character: what its CHRG says.
static unsigned Char_KartParts(void)
{
    if (!g_char.hiresChecked || !g_char.hires.read)
        return 0;
    if (Char_ModelIsCharacter() || g_char.hires.fromCharacter)
        return g_char.hires.parts;
    return RsHires_PartsFor(g_char.hires.kartCase, Char_KartBoxWanted() && Char_IsChecked(g_char.gameKart) ? 1 : 0);
}

// THE JPEG TEXTURES: the line on the step Model (while the box is on) and the
// button "Convert to PNG" (the check of the model as it is found a JPEG), as
// the box and the check shown are; laid out again when either changed.
static void Char_PngControls(void)
{
    wchar_t step[RS_WIZ_TEXT_MAX], was[RS_WIZ_TEXT_MAX];
    const int checked = Char_Chrg() && g_char.hiresChecked && !g_char.hires.fromCharacter;
    const int fix = checked && RsCharHires_PngFixShown(&g_char.hires, g_char.route, g_char.hiresPng);
    RsCharHires_PngStepLine(checked ? g_char.route : RS_ROUTE_NONE, checked && g_char.hiresPng, g_char.pngJpeg, g_char.pngWhy[0] != 0, step,
                            RS_WIZ_TEXT_MAX);
    GetWindowTextW(g_char.pngStep, was, RS_WIZ_TEXT_MAX);
    Rs_SetText(g_char.pngStep, step);
    if ((wcscmp(was, step) != 0 || fix != g_char.pngFixOn) && g_char.view) {
        g_char.pngFixOn = fix;
        Char_Relayout(GetParent(g_char.view));
    }
    g_char.pngFixOn = fix;
}

// Before the check of a CHRG route: the box as the author chose it for this
// model - never suggested, so off without a choice.
static void Char_PngUpdate(const wchar_t *model)
{
    Char_SetChecked(g_char.png, Char_Remembered(model, CHAR_CHOICE_PNG) == 1);
}

// The author (the box, "Convert to PNG") or the automation sets "Convert JPEG
// textures to PNG": kept for this model, and the model checked again - as
// converted, or as it is. Returns Char_Check's result.
static int Char_PngSet(HWND page, int on)
{
    wchar_t model[CHAR_VAL];
    Char_SetChecked(g_char.png, on);
    Char_FieldPath(g_char.model, model, CHAR_VAL);
    if (model[0])
        Char_Remember(model, CHAR_CHOICE_PNG, on ? 1 : -1);
    g_char.builtFresh = 0;
    Char_UpdateButtons();
    if (Rs_Automating())
        Rs_AutoLog(L"  convert jpeg textures to png: %ls (the author's choice)", on ? L"on" : L"off");
    return Char_Check(page);
}

// THE CLASSIC MODEL of a CHRG route (Advanced, card Options): made from the
// .glb by make-char (the start, as always), or the one of a finished
// character - the build is then hires-add into that file, rldpack's bytes as
// "rldpack hires-add <character> <model> -o <out>": its classic model, name,
// driving style, mask, look and voices stay as they are, so the steps Driver,
// In-game look and Voices have nothing to set. It is not the start, so the
// step Model says it in a line with "Change".
static void Char_ClassicPath(wchar_t *out, int cap)
{
    Char_FieldPath(g_char.classicFile, out, cap);
}

// 1 = path is a character file: its first 8 bytes are RLDCHAR_MAGIC
// (include/rldchar.inc); else why says what is wrong. rldpack hires-add
// checks the rest when it builds.
static int Char_ClassicFileOk(const wchar_t *path, wchar_t *why, int cap)
{
    static const unsigned char magic[8] = { 'R', 'L', 'D', 'C', 'H', 'A', 'R', 0 };
    unsigned char head[8];
    size_t got = 0;
    FILE *f;
    why[0] = 0;
    if (!path[0]) {
        Char_Copy(why, cap, L"No character chosen for the classic model yet.");
        return 0;
    }
    f = _wfopen(path, L"rb");
    if (!f) {
        swprintf(why, (size_t)cap, L"%ls cannot be read.", Rs_PathName(path));
        return 0;
    }
    got = fread(head, 1, 8, f);
    fclose(f);
    if (got != 8 || memcmp(head, magic, 8) != 0) {
        swprintf(why, (size_t)cap, L"%ls is not a character file (.rldchar).", Rs_PathName(path));
        return 0;
    }
    return 1;
}

// The list, and the line on the step Model as the choice is (laid out again
// when the line changed).
static void Char_ClassicControls(void)
{
    wchar_t line[CHAR_VAL + 96], was[CHAR_VAL + 96], path[CHAR_VAL];
    line[0] = 0;
    if (!g_char.classicMode)
        return;
    SendMessageW(g_char.classicMode, CB_SETCURSEL, (WPARAM)(g_char.classicFrom ? 1 : 0), 0);
    if (Char_ClassicFromFile() && !Char_ModelIsCharacter()) {
        Char_ClassicPath(path, CHAR_VAL);
        if (path[0])
            swprintf(line, CHAR_VAL + 96, L"The classic model, name, look and voices come from %ls.", Rs_PathName(path));
        else
            Char_Copy(line, CHAR_VAL + 96, L"The classic model comes from a finished character - none chosen yet.");
    }
    GetWindowTextW(g_char.classicStep, was, CHAR_VAL + 96);
    Rs_SetText(g_char.classicStep, line);
    if (wcscmp(was, line) != 0 && g_char.view)
        Char_Relayout(GetParent(g_char.view));
}

// The author (the list, the automation verb "classic") sets where the
// classic model comes from; the model is checked again.
static int Char_ClassicSet(HWND page, int fromFile)
{
    g_char.classicFrom = fromFile != 0;
    g_char.builtFresh = 0;
    Char_ClassicControls();
    Char_Relayout(page);
    return Char_Check(page);
}

// A CHRG route without make-char (Char_NoMake). THE FINISHED CHARACTER in
// the model field: its CHRG checked and shown as a .glb's, nothing built
// from it (the build starts from a .glb; packing a model into a finished
// character is "Classic model: from a finished character"). THE CLASSIC
// MODEL from a character: the .glb checked, the character's file looked at.
// Neither has a classic fallback to check or show. Char_HiresEnsure ran.
static void Char_ChrgNoMake(HWND page)
{
    static const int steps[3] = { CHAR_TAB_DRIVER, CHAR_TAB_LOOK, CHAR_TAB_VOICES };
    const int character = Char_ModelIsCharacter();
    wchar_t why[CHAR_VAL + 64], path[CHAR_VAL] = L"", text[CHAR_VAL + 160];
    int errors, fileOk = 1, i, s;

    Char_JobReset();
    if (g_char.showRaw)
        Char_RawRefresh();
    Rs_Free(g_char.classicPv);
    g_char.classicPv = NULL;
    g_char.classicPvBytes = 0;
    g_char.modelRead = g_char.hires.read;
    Char_MsgClear();
    errors = Char_ChrgHiresMessages();
    if (character) {
        swprintf(text, CHAR_VAL + 160, L"A finished character: its %ls model is checked and shown. Nothing is built from it.",
                 Char_ChrgTitle());
        Char_MsgAdd(CHAR_TAB_MODEL, RS_SEV_INFO, text,
                    L"To build, choose the .glb it was made from. To pack a model into a finished character, choose the .glb "
                    L"and, under Advanced, \"Classic model: from a finished character\".");
        Char_MsgAdd(CHAR_TAB_BUILD, RS_SEV_INFO, L"Nothing to build: the model is a finished character.",
                    L"Choose the .glb it was made from on the step Model.");
        for (i = 0; i < 3; i++)
            Char_MsgAdd(steps[i], RS_SEV_INFO, L"A finished character holds the choices of this step itself.", NULL);
    } else {
        Char_ClassicPath(path, CHAR_VAL);
        fileOk = Char_ClassicFileOk(path, why, CHAR_VAL + 64);
        if (!fileOk) {
            Char_MsgAddCard(CHAR_TAB_MODEL, CHAR_EXTRAS_OPTIONS, RS_SEV_ERROR, why,
                            L"How to fix: choose a .rldchar that Reload Studio or rldpack built (Advanced, Classic model), or let "
                            L"the classic model be made from the .glb.");
            errors++;
        } else {
            swprintf(text, CHAR_VAL + 160, L"Taken from %ls: the classic model, name, driving style, mask, look and voices of that character.",
                     Rs_PathName(path));
            for (i = 0; i < 3; i++)
                Char_MsgAdd(steps[i], RS_SEV_INFO, text, L"Advanced, Classic model: \"Made from the .glb\" uses this step again.");
        }
    }
    // The look of the preview: the character file's (Char_LookPreview).
    Char_FieldPath(character ? g_char.model : g_char.classicFile, g_char.lookFile, CHAR_VAL);
    g_char.fileLookKnown = (character || fileOk) && RsHires_CharLook(g_char.lookFile, &g_char.fileLook, why, CHAR_VAL + 64);
    Char_LookPreview();
    Char_CountTabs();
    Char_ChrgInfo();
    g_char.checked = !character && fileOk && g_char.hires.read && g_char.hires.usable;
    if (g_char.checked) {
        Char_Headline(L"Ready to build", RS_COL_OK);
    } else if (character && errors == 0) {
        Char_Headline(L"Checked - a finished character: nothing to build", RS_COL_MUTED);
    } else {
        for (s = 0; s < CHAR_TABS && !g_char.tabErrors[s]; s++) {
        }
        swprintf(text, CHAR_VAL + 160, L"Cannot build yet - %d problem(s)%ls%ls", errors, s < CHAR_TABS ? L", see the step " : L"",
                 s < CHAR_TABS ? g_charTabTexts[s] : L"");
        Char_Headline(text, RS_COL_ERROR);
    }
    Char_UpdateButtons();
    Char_PreviewShow();
    if (Rs_Automating()) {
        if (character)
            Rs_AutoLog(L"  check: a finished character - its CHRG checked (%ls), nothing to build", g_char.hires.read
                       ? (g_char.hires.usable ? L"usable" : L"refused") : L"not read");
        else if (g_char.checked)
            Rs_AutoLog(L"  check: ready to build - the classic model from %ls (no make-char)", Rs_PathName(path));
        else
            Rs_AutoLog(L"  check: cannot build yet, %d problem(s)", errors);
    }
    Char_Relayout(page);
}

// The clip shown stops (another step, the classic fallback, a new check).
static void Char_HiresStop(void)
{
    HWND page = g_char.view ? GetParent(g_char.view) : NULL;
    if (page)
        KillTimer(page, CHAR_TIMER_ANIM);
}

// THE CLIPS of a CHRG route (Advanced, card Animations): the clip the preview
// shows - 0 the bind pose, 1 + n clip n. At the start (clipSel -1) idle where
// the clips can play, else the bind pose, as the preview always did.
static int Char_ClipShown(void)
{
    const struct RsHiresCheck *c = &g_char.hires;
    if (!g_char.hiresChecked || !c->read || !c->animReady)
        return 0;
    if (g_char.clipSel < 0)
        return c->clips > 0 ? 1 : 0;
    return g_char.clipSel <= RS_HIRES_CLIPS ? g_char.clipSel : 0;
}

// The note below the preview of a CHRG route.
static void Char_ChrgViewNote(void)
{
    wchar_t text[200];
    const int clip = Char_ClipShown();
    if (!g_char.viewNote)
        return;
    g_char.viewPartCount = 0;
    g_char.viewFirst = -1;
    if (g_char.previewClassic) {
        if (Char_NoMake())
            Char_ViewNotePart(Char_ModelIsCharacter() ? L"The classic model of a finished character is not shown here."
                                                      : L"The classic model comes from a finished character (Advanced) - it is not shown here.");
        else if (g_char.classicPv) {
            swprintf(text, 200, L"The classic fallback: the game draws it wherever the %ls model is not drawn.", Char_ChrgTitle());
            Char_ViewNotePart(text);
        } else {
            Char_ViewNotePart(L"No classic fallback yet - see the messages of the step Model.");
        }
        Char_ViewNotePart(CHAR_VIEW_MOUSE_TEXT);
        Char_ViewNotePart(CHAR_VIEW_SHADE_TEXT);
    } else {
        if (clip > 0)
            swprintf(text, 200,
                     g_char.clipPlaying ? L"The %ls model as the game draws it, with its textures - its clip %ls plays."
                                        : L"The %ls model as the game draws it, with its textures - its clip %ls, paused.",
                     Char_ChrgTitle(), RsHires_ClipName(clip - 1));
        else if (g_char.hires.animReady && g_char.hires.clips > 0)
            swprintf(text, 200, L"The %ls model as the game draws it, with its textures - in its bind pose.", Char_ChrgTitle());
        else
            swprintf(text, 200, L"The %ls model as the game draws it, with its textures. It has no animations: it stands still.",
                     Char_ChrgTitle());
        Char_ViewNotePart(!g_char.hires.read ? L"Nothing to draw - see the messages of the step Model." : text);
        Char_ViewNotePart(CHAR_VIEW_MOUSE_TEXT);
    }
    Char_ViewNoteShow(RS_COL_MUTED);
}

// The frame of the clip shown at animSeconds (wheels turned animWheel) in
// the view: copied over the picture shown, the textures stay. Returns its
// triangles, 0 = none.
static int Char_ClipFrame(void)
{
    const unsigned char *bytes = NULL;
    size_t size = 0;
    const int clip = Char_ClipShown();
    int n;
    if (clip <= 0)
        return 0;
    n = RsHires_AnimFrame(&g_char.hires, clip - 1, g_char.animSeconds, g_char.animWheel, &bytes, &size);
    if (n <= 0 || !bytes)
        return 0;
    if (!RsView_UpdatePreviewData(g_char.view, bytes, size, g_char.hires.previewKeepAt, g_char.hires.previewKeepBytes)) {
        RsView_SetModelKey(g_char.view, g_char.hiresPath);
        RsView_LoadPreviewData(g_char.view, bytes, size);
    }
    RsView_SetPose(g_char.view, RS_VIEW_POSE_NEUTRAL);
    return n;
}

// The line of the time below the clip (as the clip's own time runs: a steer
// clip by its share), or why the clips cannot be played.
static void Char_ClipTimeShow(void)
{
    const struct RsHiresCheck *c = &g_char.hires;
    const int clip = Char_ClipShown();
    wchar_t text[160];
    text[0] = 0;
    if (g_char.hiresChecked && c->read && !c->animReady) {
        Char_Copy(text, 160, c->animWhy[0] ? c->animWhy : L"Clips cannot be played for this model.");
    } else if (clip > 0) {
        float share = -1.0f;
        const float len = RsHires_AnimLength(c, clip - 1);
        const float shown = RsHires_AnimLocal(c, clip - 1, g_char.animSeconds, &share);
        if (len > 0.0f) {
            swprintf(text, 160, L"%.2f s of %.2f s", (double)shown, (double)len);
            if (share >= 0.0f)
                swprintf(text + wcslen(text), 160 - wcslen(text), L" - steer share %.2f", (double)share);
        } else {
            Char_Copy(text, 160, L"The rest pose - this clip and its stand-ins are not in the file.");
        }
    } else if (g_char.hiresChecked && c->read) {
        Char_Copy(text, 160, L"The bind pose.");
    }
    Rs_SetText(g_char.clipTime, text);
}

// The list "Clip" and the table as the check shown is: per clip whether it
// is in the file (and how long), or what the game plays instead.
static void Char_ClipFill(void)
{
    const struct RsHiresCheck *c = &g_char.hires;
    const int known = g_char.hiresChecked && c->read;
    int i;
    if (!g_char.clipCombo)
        return;
    SendMessageW(g_char.clipCombo, CB_RESETCONTENT, 0, 0);
    SendMessageW(g_char.clipCombo, CB_ADDSTRING, 0, (LPARAM)L"Bind pose");
    for (i = 0; i < RS_HIRES_CLIPS; i++) {
        wchar_t entry[160], state[96], length[32];
        state[0] = length[0] = 0;
        if (!known || c->clipState[i] == RS_HIRES_CLIP_PRESENT) {
            Char_Copy(entry, 160, RsHires_ClipName(i));
            if (known) {
                Char_Copy(state, 96, L"yes");
                swprintf(length, 32, L"%.2f s", (double)c->clipSeconds[i]);
            }
        } else if (c->clipState[i] == RS_HIRES_CLIP_FINISH) {
            // Optional (4.3): no warning, the game plays what it played before.
            wchar_t what[64];
            RsHires_FinishText(c, i, what, 64);
            swprintf(entry, 160, L"%ls (optional, missing - after the finish line: %ls)", RsHires_ClipName(i), what);
            swprintf(state, 96, L"no (optional) - after the finish line: %ls", what);
        } else if (c->clipState[i] == RS_HIRES_CLIP_FALLBACK) {
            swprintf(entry, 160, L"%ls (missing - plays %ls%ls)", RsHires_ClipName(i), RsHires_ClipName(c->clipVia[i]),
                     c->clipFullLock[i] ? L" at full lock" : L"");
            swprintf(state, 96, L"missing - plays %ls%ls", RsHires_ClipName(c->clipVia[i]), c->clipFullLock[i] ? L" at full lock" : L"");
        } else {
            swprintf(entry, 160, L"%ls (missing - rest pose)", RsHires_ClipName(i));
            Char_Copy(state, 96, L"missing - rest pose");
        }
        SendMessageW(g_char.clipCombo, CB_ADDSTRING, 0, (LPARAM)entry);
        ListView_SetItemText(g_char.clipTable, i, 1, state);
        ListView_SetItemText(g_char.clipTable, i, 2, length);
    }
    SendMessageW(g_char.clipCombo, CB_SETCURSEL, (WPARAM)Char_ClipShown(), 0);
}

// The controls of the card as the clip shown and the check are.
static void Char_ClipControls(void)
{
    const int can = g_char.hiresChecked && g_char.hires.read && g_char.hires.animReady;
    const int clip = Char_ClipShown();
    if (!g_char.clipCombo)
        return;
    SendMessageW(g_char.clipCombo, CB_SETCURSEL, (WPARAM)clip, 0);
    EnableWindow(g_char.clipCombo, can);
    EnableWindow(g_char.clipPlay, clip > 0);
    EnableWindow(g_char.clipPrev, clip > 0);
    EnableWindow(g_char.clipNext, clip > 0);
    Rs_SetText(g_char.clipPlay, g_char.clipPlaying && clip > 0 ? L"Pause" : L"Play");
    Char_ClipTimeShow();
}

// The hi-res picture of the check (textured, the engine's filter) in the
// view; the clip shown (THE CLIPS) plays on the page's timer while it plays.
static void Char_HiresShow(void)
{
    HWND page = GetParent(g_char.view);
    const struct RsHiresCheck *c = &g_char.hires;
    g_char.hiresShown = 0;
    Char_HiresStop();
    // The neutral stage: the colours as they look in the game (RsView_SetStage).
    RsView_SetStage(g_char.view, 1);
    if (!g_char.hiresChecked || !c->read || !c->preview) {
        wchar_t msg[RS_HIRES_TEXT_MAX + 32];
        swprintf(msg, RS_HIRES_TEXT_MAX + 32, L"Nothing to draw: %ls",
                 !g_char.hiresChecked ? L"choose a .glb" : !c->read ? c->why : L"the file has no triangle to draw");
        RsView_Clear(g_char.view, msg);
        Char_ClipControls();
        Char_ChrgViewNote();
        return;
    }
    RsView_SetModelKey(g_char.view, g_char.hiresPath);
    RsView_SetModelLabel(g_char.view, g_char.route == RS_ROUTE_LOWPOLY ? L"Low-Poly model" : L"Hi-Res model");
    RsView_LoadPreviewData(g_char.view, c->preview, c->previewBytes);
    RsView_SetPose(g_char.view, RS_VIEW_POSE_NEUTRAL);
    RsView_SetWheels(g_char.view, 0);   // the picture carries the engine's standard wheels itself
    g_char.hiresShown = 1;
    if (Char_ClipShown() > 0) {
        // The frame of the clip at its time; playing, the page's timer moves
        // it on. In automation nothing plays unless "hires-play on" says so:
        // the shots are the same every run (and the log has no line per frame).
        Char_ClipFrame();
        if (g_char.clipPlaying) {
            g_char.animTick = GetTickCount();
            SetTimer(page, CHAR_TIMER_ANIM, CHAR_ANIM_TICK_MS, NULL);
        }
    }
    Char_ClipControls();
    Char_ChrgViewNote();
}

// The classic fallback of the check shown (make-char --preview) in the view.
static void Char_ClassicShow(void)
{
    g_char.hiresShown = 0;
    Char_HiresStop();
    RsView_SetStage(g_char.view, 0);
    RsView_SetModelLabel(g_char.view, L"Classic fallback");
    if (g_char.classicPv && RsView_LoadPreviewData(g_char.view, g_char.classicPv, g_char.classicPvBytes)) {
        RsView_SetPose(g_char.view, g_charPoseView[g_char.poseNow]);
        RsView_SetWheels(g_char.view, !(g_char.hiresChecked && g_char.hires.read && g_char.hires.wheels == 4));
    } else {
        RsView_Clear(g_char.view, Char_NoMake() ? L"The classic model of the character file is not shown here."
                                                : L"No classic fallback yet - see the messages of the step Model.");
    }
    Char_ChrgViewNote();
}

// The preview of a CHRG route as the switch says.
static void Char_PreviewShow(void)
{
    if (!Char_Chrg())
        return;
    if (g_char.previewClassic)
        Char_ClassicShow();
    else
        Char_HiresShow();
}

// A tick of the clip shown: the next frame copied over the shown one (the
// textures stay).
static void Char_HiresTick(HWND page)
{
    DWORD now = GetTickCount(), dt = now - g_char.animTick;
    if (!g_char.hiresShown || !Char_Chrg() || g_char.choosing || !IsWindowVisible(page) || !g_char.hires.animReady ||
        !g_char.clipPlaying || Char_ClipShown() <= 0) {
        Char_HiresStop();
        return;
    }
    if (dt > CHAR_ANIM_TICK_CAP)
        dt = CHAR_ANIM_TICK_CAP;
    g_char.animTick = now;
    g_char.animSeconds += (float)dt / 1000.0f;
    g_char.animWheel += 180.0f * (float)dt / 1000.0f;
    if (g_char.animWheel >= 360.0f)
        g_char.animWheel -= 360.0f;
    Char_ClipFrame();
    if (Char_IsShown(g_char.clipTime))
        Char_ClipTimeShow();
}

// THE CLIPS: the author (the list, a row of the table, the automation verb
// hires-clip) chooses the clip shown - 0 the bind pose, 1 + n clip n; it
// starts at its beginning.
static void Char_ClipSelect(int sel)
{
    if (sel < 0 || sel > RS_HIRES_CLIPS)
        return;
    g_char.clipSel = sel;
    g_char.animSeconds = 0.0f;
    if (Char_Chrg() && !g_char.previewClassic)
        Char_HiresShow();
    else
        Char_ClipControls();
}

// Play or pause the clip shown (Play | Pause, hires-play).
static void Char_ClipPlay(int on)
{
    g_char.clipPlaying = on != 0;
    if (Char_Chrg() && !g_char.previewClassic && g_char.hiresShown) {
        HWND page = GetParent(g_char.view);
        Char_HiresStop();
        if (g_char.clipPlaying && Char_ClipShown() > 0) {
            g_char.animTick = GetTickCount();
            SetTimer(page, CHAR_TIMER_ANIM, CHAR_ANIM_TICK_MS, NULL);
        }
    }
    Char_ClipControls();
    Char_ChrgViewNote();
}

// A frame at a time (< Frame, Frame >, hires-time): paused at that time of
// the clip shown, the wheels turned that far. Returns the triangles of the
// picture (the bind pose's without a clip), 0 = nothing drawn.
static int Char_ClipAt(float seconds, float wheel)
{
    int n = 0;
    g_char.clipPlaying = 0;
    Char_HiresStop();
    g_char.animSeconds = seconds < 0.0f ? 0.0f : seconds;
    g_char.animWheel = wheel;
    if (Char_Chrg() && !g_char.previewClassic && g_char.hiresShown)
        n = Char_ClipShown() > 0 ? Char_ClipFrame() : g_char.hires.previewTriangles;
    Char_ClipControls();
    Char_ChrgViewNote();
    return n;
}

// The switch Low-Poly/Hi-Res | Classic fallback above the preview.
static void Char_PreviewSwitch(HWND page, int classic)
{
    int i;
    g_char.previewClassic = classic != 0;
    for (i = 0; i < 2; i++)
        InvalidateRect(g_char.previewSwitch[i], NULL, FALSE);
    Char_PreviewShow();
    Char_Relayout(page);        // the pose choice belongs to the classic fallback
}

// The classic fallback's preview file of a check, kept in memory for the
// switch (the file itself goes with the result kept).
static void Char_ClassicKeep(const wchar_t *path)
{
    FILE *f;
    long n;
    Rs_Free(g_char.classicPv);
    g_char.classicPv = NULL;
    g_char.classicPvBytes = 0;
    f = path && path[0] ? _wfopen(path, L"rb") : NULL;
    if (!f)
        return;
    if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) > 0 && n < 512L * 1024L * 1024L && fseek(f, 0, SEEK_SET) == 0) {
        g_char.classicPv = Rs_Alloc((size_t)n);
        if (fread(g_char.classicPv, 1, (size_t)n, f) == (size_t)n) {
            g_char.classicPvBytes = (size_t)n;
        } else {
            Rs_Free(g_char.classicPv);
            g_char.classicPv = NULL;
        }
    }
    fclose(f);
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
    wchar_t rearWheel[CHAR_VAL];  // --rear-wheel-model
    wchar_t axle[2][48];          // --axle-front, --axle-rear <dz>,<dy>,<dtrack>
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
    if (n > 4 && (_wcsicmp(name + n - 4, L".ply") == 0 || _wcsicmp(name + n - 4, L".obj") == 0 ||
                  _wcsicmp(name + n - 4, L".glb") == 0))
        name[n - 4] = 0;
    else if (n > 5 && _wcsicmp(name + n - 5, L".gltf") == 0)
        name[n - 5] = 0;
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
// The line below the output field (step Build): where the character goes -
// with the field empty the game's characters folder (the default), else the
// file chosen, and whether the game loads it from there. named: the output
// the last check named (rldpack's @value out), NULL = not known yet.
static void Char_OutNote(const wchar_t *named)
{
    wchar_t field[CHAR_VAL], dir[CHAR_VAL], note[2 * CHAR_VAL + 96];
    const int game = Char_GameCharDir(dir, CHAR_VAL);
    if (!g_char.outNote)
        return;
    Char_FieldPath(g_char.out, field, CHAR_VAL);
    if (field[0])
        Char_Copy(note, 2 * CHAR_VAL + 96, Char_InGameCharDir(field) ? L"Into the game's characters folder, where the game loads it."
                                           : L"Your own place - the game loads characters only from its characters folder.");
    else if (named && named[0])
        swprintf(note, 2 * CHAR_VAL + 96, L"%ls: %ls", Char_InGameCharDir(named) ? L"Into the game's characters folder" : L"Next to the model",
                 named);
    else if (game)
        swprintf(note, 2 * CHAR_VAL + 96, L"Empty: into the game's characters folder, %ls - where the game loads it.", dir);
    else
        Char_Copy(note, 2 * CHAR_VAL + 96, L"Empty: next to the model. The game is not known yet - choose it on the page Test in game.");
    Rs_SetText(g_char.outNote, note);
}

static void Char_OutCue(void)
{
    wchar_t dir[CHAR_VAL];
    const int game = Char_GameCharDir(dir, CHAR_VAL);
    SendMessageW(g_char.out, EM_SETCUEBANNER, FALSE,
                 (LPARAM)(game ? L"In the game's characters folder (.rldchar)" : L"Next to the model (.rldchar)"));
    Char_OutNote(NULL);
}

// make-char with the values of the fields. check = 1: with --check and the
// preview files of number seq. out: output path (NULL = from the field).
// Returns 0 if there is no model.
static int Char_MakeArgs(struct CharArgs *a, int check, const wchar_t *out, int seq)
{
    const int user = Rs_NativeForUsers();
    // A CHRG route passes none of the choices it hides (Char_RouteHides): the
    // classic fallback is made with the switches of RsCharHires_FallbackArgs
    // at the end (and --wheels off when the hi-res model has its own wheels).
    const int chrg = Char_Chrg();

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
    if (!user && !chrg && !Char_IsChecked(g_char.repair)) {
        Char_ArgsAdd(a, L"--repair");
        Char_ArgsAdd(a, L"off");
    }
    if (!user && !chrg && !Char_IsChecked(g_char.openParts)) {
        Char_ArgsAdd(a, L"--open-parts");
        Char_ArgsAdd(a, L"one-sided");
    }
    if (!user && !chrg && Char_RemeshOn()) {
        Char_ArgsAdd(a, L"--remesh");
        Char_ArgsAdd(a, L"on");
    }
    g_char.runReduce = user || chrg || Char_IsChecked(g_char.reduce);
    if (!g_char.runReduce) {
        Char_ArgsAdd(a, L"--reduce");
        Char_ArgsAdd(a, L"off");
    }
    if (!chrg && !Char_IsChecked(g_char.wheels)) {
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
    if (!chrg && Char_ListIndex(g_char.up, CHAR_UP_COUNT) != 0) {
        Char_ArgsAdd(a, L"--up");
        Char_ArgsAdd(a, g_charUpWords[Char_ListIndex(g_char.up, CHAR_UP_COUNT)]);
    }
    if (!chrg && Char_ListIndex(g_char.forward, CHAR_FORWARD_COUNT) != 0) {
        Char_ArgsAdd(a, L"--forward");
        Char_ArgsAdd(a, g_charForwardWords[Char_ListIndex(g_char.forward, CHAR_FORWARD_COUNT)]);
    }
    // The palette: 64 colours instead of rldpack's 128 (not in the user mode).
    if (!user && !chrg && Char_ListIndex(g_char.colors, CHAR_COLORS_COUNT) != 0) {
        Char_ArgsAdd(a, L"--colors");
        Char_ArgsAdd(a, g_charColorsWords[Char_ListIndex(g_char.colors, CHAR_COLORS_COUNT)]);
    }
    // An OBJ: the textures folder, when one is given.
    Char_FieldPath(g_char.textures, a->textures, CHAR_VAL);
    if (!chrg && g_char.objOn && a->textures[0]) {
        Char_ArgsAdd(a, L"--textures");
        Char_ArgsAdd(a, a->textures);
    }
    // An OBJ: how its vertex colours meet its textures, when not auto.
    if (!chrg && g_char.objOn && Char_VColorIndex() != 0) {
        Char_ArgsAdd(a, L"--vertex-colors");
        Char_ArgsAdd(a, g_charVColorWords[Char_VColorIndex()]);
    }
    // An OBJ: its own mesh and textures as well (Char_NativePassed): with
    // "Native model" ticked (card Import, a preview feature open to
    // everyone; not ticked at the start) or in the user mode, and Show kart
    // wheels off or a wheel model of the card Wheels (rldpack refuses it
    // beside the kart wheels without one).
    Char_NativeUpdate();
    if (!chrg && Char_NativePassed()) {
        Char_ArgsAdd(a, L"--native-model");
        Char_ArgsAdd(a, L"on");
    }
    // The card Wheels (rs_wheels.c, preview feature): an author's wheel model
    // with the native model and the kart wheels shown (make-char writes WHLS
    // version 2; the classic model keeps the game's wheels), its size only
    // when it is not 100 %.
    if (!chrg && Char_WheelPassed(a->wheel, CHAR_VAL)) {
        Char_ArgsAdd(a, L"--wheel-model");
        Char_ArgsAdd(a, a->wheel);
        if (CharWheels_SizePercent() != 100) {
            swprintf(a->wheelSize, 16, L"%d", CharWheels_SizePercent());
            Char_ArgsAdd(a, L"--wheel-size");
            Char_ArgsAdd(a, a->wheelSize);
        }
        // With it, and only when set (none set: the command of before; make-char
        // then writes WHLS version 3): the rear wheel, the axles. Never
        // --wheels-always: the game draws an author's wheels at every
        // distance, the flag changes nothing there.
        if (CharWheels_RearModelPath(a->rearWheel, CHAR_VAL)) {
            Char_ArgsAdd(a, L"--rear-wheel-model");
            Char_ArgsAdd(a, a->rearWheel);
        }
        {
            int axle, v[3];
            for (axle = 0; axle < 2; axle++) {
                if (!CharWheels_Axle(axle, v))
                    continue;
                swprintf(a->axle[axle], 48, L"%d,%d,%d", v[0], v[1], v[2]);
                Char_ArgsAdd(a, axle == 0 ? L"--axle-front" : L"--axle-rear");
                Char_ArgsAdd(a, a->axle[axle]);
            }
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
    // A CHRG route: after the page's own switches those of the classic
    // fallback (RsCharHires_FallbackArgs, e.g. --reduce auto, so that the
    // fallback always fits), then --wheels off when the hi-res check found the
    // model's own four wheels - else the fallback would have them twice (its
    // own wheel geometry and the game's kart wheels). The same for --check,
    // --preview and the build.
    if (chrg) {
        const wchar_t *fallback[RS_CHARHIRES_FALLBACK_ARGS];
        int i, n = RsCharHires_FallbackArgs(g_char.route, fallback);
        for (i = 0; i < n && i < RS_CHARHIRES_FALLBACK_ARGS; i++)
            Char_ArgsAdd(a, fallback[i]);
        if (g_char.hiresChecked && g_char.hires.read && g_char.hires.wheels == 4) {
            Char_ArgsAdd(a, L"--wheels");
            Char_ArgsAdd(a, L"off");
        }
        // THE KART: a figure that gets the game's kart (the kit's joint, or the
        // switch) - its classic model shows it on a plain stand-in kart made by
        // code (make-char --figure-only, no game data in the file), which a 4.3
        // game swaps for the original kart (CHRI bit 5, set by --figure-only).
        if (Char_KartParts() == RS_HIRES_PARTS_GAME_KART)
            Char_ArgsAdd(a, L"--figure-only");
    } else {
        // Vanilla (K6): a .glb with a skin (the kit's figure exported with its
        // skeleton) is taken in its bind pose, as the CHRG fallback takes it
        // (RsCharHires_FallbackArgs) - make-char refuses a skin without it; a
        // model without a skin gets nothing new, its bytes stay as before.
        if (g_char.route == RS_ROUTE_VANILLA && g_char.vanKart >= 0 && g_char.vanSkins > 0) {
            Char_ArgsAdd(a, L"--rig");
            Char_ArgsAdd(a, L"bind-pose");
        }
        // The game's kart, as for a CHRG route: its model is the classic one.
        if (Char_KartBoxWanted() && Char_IsChecked(g_char.gameKart))
            Char_ArgsAdd(a, L"--figure-only");
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
    Char_ViewNotePart(L"Grey: Crash with his kart - the size a model is fitted to. Choose a glTF, OBJ or PLY model.");
    Char_ViewNotePart(CHAR_VIEW_MOUSE_TEXT);
    Char_ViewNoteShow(RS_COL_MUTED);
    Char_ApplyQuality();
    memset(g_char.tabErrors, 0, sizeof(g_char.tabErrors));
    memset(g_char.tabWarnings, 0, sizeof(g_char.tabWarnings));
    g_char.modelRead = 0;
    g_char.stampKnown = 0;
    Char_SetInfo(Char_Chrg() ? CHAR_START_TEXT_GLB : CHAR_START_TEXT, RS_COL_MUTED);
    Char_ImportClear();
    Char_Headline(Char_Chrg() ? L"Choose a .glb model to start (step Model)" : L"Choose a glTF, OBJ or PLY model to start (step Model)",
                  RS_COL_MUTED);
    Char_ReduceFitShow(page, 0);
    Char_ProgressShow(page, 0);
    Char_VoicesClear();
    Char_EmptyText(L"Choose a glTF, OBJ or PLY model. What rldpack finds shows up here.");
    Char_UpdateButtons();
    g_char.checkModel[0] = 0;
    CharWheels_ModelChecked(page, L"", g_char.sizeNow, 0);
    CharAnim_ModelChecked(page, L"", L"", NULL, 0);
    Char_HiresForget();
    if (Char_Chrg())
        Char_ChrgViewNote();
}

// A CHRG route with a model that is no .glb: one line at the model, nothing
// checked (Low-Poly and Hi-Res read the .glb as the game does).
static void Char_ChrgNotGlb(HWND page)
{
    Char_NoModel(page);
    Char_MsgAdd(CHAR_TAB_MODEL, RS_SEV_ERROR, CHAR_NOT_GLB_TEXT, CHAR_NOT_GLB_FIX);
    Char_CountTabs();
    Char_SetInfo(CHAR_NOT_GLB_TEXT, RS_COL_ERROR);
    RsView_Clear(g_char.view, CHAR_NOT_GLB_TEXT);
    Char_Headline(L"Cannot build yet - 1 problem(s)", RS_COL_ERROR);
    Char_UpdateButtons();
    if (Rs_Automating())
        Rs_AutoLog(L"  check: %ls", CHAR_NOT_GLB_TEXT);
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
    g_char.builtFresh = 0;
    // A CHRG route: a .glb (or a finished character, only to look at); the
    // hi-res check of it first (in this process, again only when the file
    // changed), then make-char below - unless there is none to run
    // (Char_ChrgNoMake: a finished character, or the classic model from one).
    if (Char_Chrg()) {
        wchar_t model[CHAR_VAL];
        const int character = Char_ModelIsCharacter();
        Char_FieldPath(g_char.model, model, CHAR_VAL);
        if (model[0] && !Char_EndsWith(model, L".glb") && !character) {
            Char_ChrgNotGlb(page);
            return 0;
        }
        if (model[0]) {
            if (character)
                Char_SetChecked(g_char.png, 0);     // nothing to convert: the CHRG is checked as it is
            else
                Char_PngUpdate(model);  // the author's choice for this model, else off
            Char_HiresEnsure(model);
        }
        Char_VcolUpdate();      // the suggestion and the mode (also for another route)
        Char_UpscaleUpdate();   // the same for "Upscale small textures"
        Char_GameKartUpdate();  // and for "Kart: from the player's game data"
        Char_PngControls();     // the line and the button of "Convert JPEG textures to PNG"
        Char_ClassicControls(); // the line of the classic model
        if (model[0] && Char_NoMake()) {
            Char_ChrgNoMake(page);
            return 0;
        }
    } else {
        // Vanilla (K6): the kart case of a .glb model, and with it the
        // suggestion "Kart: from the player's game data" (make-char
        // --figure-only), before make-char checks it with that switch.
        wchar_t model[CHAR_VAL];
        Char_FieldPath(g_char.model, model, CHAR_VAL);
        Char_VanillaKartEnsure(model);
        Char_GameKartUpdate();
    }
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

// THE BUILD OF A CHRG ROUTE, every step through Rs_RunRldpack (the bytes are
// rldpack's): make-char with the switches of the check -> the classic
// fallback <out name>.classic.part in Rs_TempDir; with "Convert JPEG textures
// to PNG" hires-png <model> -o <model>-png.glb (RsHires_PngArgs); with
// "Upscale small textures" hires-upscale <model or converted> -o <derived>
// (RsHires_UpscaleArgs; the derived .glb files also in Rs_TempDir, never in
// the game's characters folder); then hires-add <classic> <the last of them>
// -o <out> (RsHires_PackArgs). The temp files go at the end, whatever
// happened. The raw output ("Show details") has every command and every line
// of them.
static void Char_ChainCleanup(void)
{
    wchar_t part[CHAR_VAL + 8];
    // The classic model from a character is the author's file: never deleted.
    if (g_char.chainClassic[0] && !g_char.chainOwnClassic) {
        DeleteFileW(g_char.chainClassic);
        swprintf(part, CHAR_VAL + 8, L"%ls.part", g_char.chainClassic);
        DeleteFileW(part);
    }
    if (g_char.chainUpscale && g_char.chainDerived[0])
        DeleteFileW(g_char.chainDerived);
    if (g_char.chainPng && g_char.chainPngPath[0])
        DeleteFileW(g_char.chainPngPath);
    if (g_char.chainOut[0]) {
        swprintf(part, CHAR_VAL + 8, L"%ls.part", g_char.chainOut);
        if (Rs_FileExists(part))
            DeleteFileW(part);
    }
    g_char.chainStep = CHAR_CHAIN_NONE;
    g_char.chainClassic[0] = 0;
    g_char.chainDerived[0] = 0;
    g_char.chainPngPath[0] = 0;
    g_char.chainOwnClassic = 0;
}

// Starts one step after make-char: its command into the raw output, its
// lines to Char_ChainLine. 1 = started.
static int Char_ChainRun(HWND page, int step, const wchar_t *const *args, int argc, const wchar_t *what)
{
    wchar_t *cmd = Rs_Alloc(CHAR_CMD_CAP * sizeof(wchar_t));
    int i;
    cmd[0] = 0;
    Rs_AppendArg(cmd, CHAR_CMD_CAP, L"ReloadStudio.exe");
    Rs_AppendArg(cmd, CHAR_CMD_CAP, L"--rldpack");
    for (i = 0; i < argc; i++)
        Rs_AppendArg(cmd, CHAR_CMD_CAP, args[i]);
    Char_RawAppend(cmd);
    Rs_Free(cmd);
    g_char.chainStep = step;
    g_char.chainWritten = 0;
    g_char.chainMsgCount = 0;
    g_char.chainPath[0] = 0;
    g_char.chainSha[0] = 0;
    g_char.jobKind = CHAR_JOB_BUILD;
    g_char.jobSeq = 0;
    g_char.jobId = Rs_RunRldpack(page, args, argc);
    if (!g_char.jobId) {
        g_char.jobKind = CHAR_JOB_NONE;
        return 0;
    }
    if (!g_char.progressOn)
        SetTimer(page, CHAR_TIMER_BUSY, CHAR_PROGRESS_DELAY, NULL);
    Char_Copy(g_char.progressText, 64, what);
    g_char.progressPos = -1;
    InvalidateRect(g_char.progress, NULL, FALSE);
    EnableWindow(g_char.cancel, TRUE);
    Char_Headline(what, RS_COL_MUTED);
    if (Rs_Automating())
        Rs_AutoLog(L"  build: %ls", what);
    Char_UpdateButtons();
    return 1;
}

// A line of hires-png, hires-upscale or hires-add: its findings and what it wrote.
static void Char_ChainLine(const wchar_t *line)
{
    wchar_t *copy = Rs_Dup(line), *f[8];
    int n = Rs_SplitMachine(copy, f, 8);
    if (n >= 4 && wcscmp(f[0], L"msg") == 0 && g_char.chainMsgCount < RS_HIRES_ROWS_MAX) {
        struct CharChainMsg *m = &g_char.chainMsg[g_char.chainMsgCount++];
        m->severity = Rs_SeverityFromText(f[1]);
        Char_Copy(m->code, 16, f[2]);
        Char_Copy(m->text, RS_HIRES_TEXT_MAX, f[3]);
    } else if (n >= 3 && wcscmp(f[0], L"written") == 0) {
        Char_Copy(g_char.chainPath, CHAR_VAL, f[1]);
        Char_Copy(g_char.chainSha, 80, f[2]);
        g_char.chainWritten = 1;
    }
    Rs_Free(copy);
}

static void Char_ChainDone(HWND page, int exitCode);

// The build of a CHRG route begins: make-char of the classic fallback (or,
// with the classic model from a finished character, its first step after).
static int Char_ChainStart(HWND page, const wchar_t *path)
{
    struct CharArgs a;
    wchar_t dir[CHAR_VAL], name[CHAR_VAL], beside[CHAR_VAL];
    int started;

    if (!path[0])
        return -1;
    Rs_TempDir(dir, CHAR_VAL);
    // THE CLASSIC MODEL: from a finished character (no make-char: hires-add
    // packs into that file), else make-char makes it into the temp folder.
    g_char.chainOwnClassic = Char_ClassicFromFile();
    g_char.buildFromFile = g_char.chainOwnClassic;
    if (g_char.chainOwnClassic) {
        Char_ClassicPath(g_char.chainClassic, CHAR_VAL);
        if (_wcsicmp(g_char.chainClassic, path) == 0) {
            Char_MsgClear();
            Char_MsgAdd(CHAR_TAB_BUILD, RS_SEV_ERROR, L"The output is the character the classic model comes from.",
                        L"Choose another output: that file stays as it is.");
            Char_CountTabs();
            Char_Headline(L"Not built - 1 problem(s)", RS_COL_ERROR);
            if (Rs_Automating())
                Rs_AutoLog(L"  build: the output is the character the classic model comes from");
            g_char.chainClassic[0] = 0;
            g_char.chainOwnClassic = 0;
            return -1;
        }
    } else {
        Char_Copy(name, CHAR_VAL - 16, Rs_PathName(path));
        Char_Append(name, CHAR_VAL, L".classic.part");
        Rs_PathJoin(g_char.chainClassic, CHAR_VAL, dir, name);
    }
    Char_Copy(g_char.chainOut, CHAR_VAL, path);
    Char_FieldPath(g_char.model, g_char.chainModel, CHAR_VAL);
    g_char.chainUpscale = Char_IsChecked(g_char.upscale);
    g_char.chainParts = RsHires_PartsFor(g_char.hiresChecked && g_char.hires.read ? g_char.hires.kartCase : RS_KART_OWN,
                                         Char_KartBoxWanted() && Char_IsChecked(g_char.gameKart) ? 1 : 0);
    g_char.chainPng = Char_IsChecked(g_char.png);
    g_char.chainVcol = g_char.vcolMode;       // -1: the model has no COLOR_0, no switch
    g_char.chainDerived[0] = 0;
    g_char.chainPngPath[0] = 0;
    // The converted and the upscaled model beside a temp "output" of the same
    // name: in Rs_TempDir. The upscale takes the converted model.
    Rs_PathJoin(beside, CHAR_VAL, dir, Rs_PathName(path));
    if (g_char.chainPng && (!RsHires_PngPath(g_char.chainModel, beside, g_char.chainPngPath, CHAR_VAL) ||
                            _wcsicmp(g_char.chainPngPath, g_char.chainModel) == 0)) {
        g_char.chainPngPath[0] = 0;
        g_char.chainPng = 0;
    }
    if (g_char.chainUpscale &&
        (!RsHires_UpscaledPath(g_char.chainPng ? g_char.chainPngPath : g_char.chainModel, beside, g_char.chainDerived, CHAR_VAL) ||
         _wcsicmp(g_char.chainDerived, g_char.chainModel) == 0)) {
        g_char.chainDerived[0] = 0;
        g_char.chainUpscale = 0;
    }
    if (g_char.chainOwnClassic) {
        // Straight on to the steps after make-char, as if it had written the
        // classic model: their findings are the build's (no make-char lines).
        Char_Copy(g_char.buildOut, CHAR_VAL, path);
        g_char.buildWheelsOff = g_char.hires.read && g_char.hires.wheels == 4;
        Char_JobReset();
        g_char.chainStep = CHAR_CHAIN_CLASSIC;
        Char_ChainDone(page, 0);
        return g_char.jobId ? 1 : -1;
    }
    if (!Char_MakeArgs(&a, 0, g_char.chainClassic, 0)) {
        Char_ArgsFree(&a);
        return -1;
    }
    Char_Copy(g_char.buildOut, CHAR_VAL, a.out);
    g_char.buildWheelsOff = g_char.hires.read && g_char.hires.wheels == 4;
    g_char.buildMask = Char_MaskIndex();
    g_char.buildMapSet = g_char.mapColorSet;
    g_char.buildMapColor = g_char.mapColor;
    g_char.chainStep = CHAR_CHAIN_CLASSIC;
    Char_Headline(L"Building the classic fallback...", RS_COL_MUTED);
    started = Char_StartJob(page, CHAR_JOB_BUILD, a.v, a.n, 0) != 0;
    Char_ArgsFree(&a);
    if (!started) {
        Char_ChainCleanup();
        Char_StartFailed(L"Not built - 1 problem(s)");
        return -1;
    }
    Char_Copy(g_char.progressText, 64, L"Building the classic fallback...");
    return 1;
}

// A step of the chain failed: its findings on the step Build.
static void Char_ChainFailed(HWND page, const wchar_t *what, int exitCode)
{
    wchar_t text[CHAR_VAL + 64];
    int i, errors = 0;
    g_char.checked = 0;
    g_char.built[0] = 0;
    Char_MsgClear();
    for (i = 0; i < g_char.chainMsgCount; i++) {
        Char_MsgAdd(CHAR_TAB_BUILD, g_char.chainMsg[i].severity, g_char.chainMsg[i].text,
                    g_char.chainMsg[i].code[0] ? g_char.chainMsg[i].code : NULL);
        errors += g_char.chainMsg[i].severity == RS_SEV_ERROR;
    }
    if (!errors) {
        swprintf(text, CHAR_VAL + 64, L"%ls failed (exit code %d).", what, exitCode);
        Char_MsgAdd(CHAR_TAB_BUILD, RS_SEV_ERROR, text, L"Click \"Show details\" to see everything rldpack printed.");
        errors = 1;
    }
    Char_CountTabs();
    swprintf(text, CHAR_VAL + 64, L"Not built - %d problem(s)", errors);
    Char_Headline(text, RS_COL_ERROR);
    if (Rs_Automating())
        Rs_AutoLog(L"  build: %ls failed (exit %d), %d error(s)", what, exitCode, errors);
    Char_ChainCleanup();
    Char_Relayout(page);
}

static void Char_BuildDone(HWND page, int exitCode);

// The end of a step of the chain: the next one, or the end of the build.
static void Char_ChainDone(HWND page, int exitCode)
{
    const wchar_t *args[RS_HIRES_PACK_ARGS > RS_HIRES_UPSCALE_ARGS ? RS_HIRES_PACK_ARGS : RS_HIRES_UPSCALE_ARGS];
    const wchar_t *model = g_char.chainPng ? g_char.chainPngPath : g_char.chainModel;   // the model after hires-png
    wchar_t what[64];
    int n, step = g_char.chainStep;

    if (step == CHAR_CHAIN_CLASSIC) {
        if (!g_char.chainOwnClassic && !(g_charJob.resultSeen && wcscmp(g_charJob.resultState, L"ok") == 0)) {
            g_char.chainStep = CHAR_CHAIN_NONE;
            Char_BuildDone(page, exitCode);     // make-char's own findings
            Char_ChainCleanup();
            return;
        }
        if (g_char.chainPng) {
            n = RsHires_PngArgs(g_char.chainModel, g_char.chainPngPath, args);
            if (Char_ChainRun(page, CHAR_CHAIN_PNG, args, n, L"Converting JPEG textures to PNG..."))
                return;
            Char_ChainFailed(page, L"Converting to PNG", -1);
            return;
        }
        step = CHAR_CHAIN_PNG;              // straight on to the upscale step
        exitCode = 0;
        g_char.chainWritten = 1;
    }
    if (step == CHAR_CHAIN_PNG) {
        if (exitCode != 0 || !g_char.chainWritten) {
            Char_ChainFailed(page, L"Converting to PNG", exitCode);
            return;
        }
        if (g_char.chainUpscale) {
            n = RsHires_UpscaleArgs(model, g_char.chainDerived, args);
            if (Char_ChainRun(page, CHAR_CHAIN_UPSCALE, args, n, L"Upscaling small textures..."))
                return;
            Char_ChainFailed(page, L"Upscaling", -1);
            return;
        }
        step = CHAR_CHAIN_UPSCALE;          // straight on to the pack step
        exitCode = 0;
        g_char.chainWritten = 1;
    }
    if (step == CHAR_CHAIN_UPSCALE) {
        if (exitCode != 0 || !g_char.chainWritten) {
            Char_ChainFailed(page, L"Upscaling", exitCode);
            return;
        }
        n = RsHires_PackArgsParts(g_char.chainClassic, g_char.chainUpscale ? g_char.chainDerived : model, g_char.chainOut,
                                  g_char.chainVcol, g_char.chainParts, args);
        swprintf(what, 64, L"Packing the %ls model...", Char_ChrgTitle());
        if (Char_ChainRun(page, CHAR_CHAIN_PACK, args, n, what))
            return;
        Char_ChainFailed(page, L"Packing", -1);
        return;
    }
    if (exitCode != 0 || !g_char.chainWritten) {
        Char_ChainFailed(page, L"Packing", exitCode);
        return;
    }
    // Done: the result is the packed file, with make-char's other findings
    // (from a finished character: there was no make-char - the pack is the result).
    if (g_char.chainOwnClassic) {
        g_charJob.resultSeen = 1;
        Char_Copy(g_charJob.resultState, 16, L"ok");
    }
    {
        WIN32_FILE_ATTRIBUTE_DATA fa;
        Char_Copy(g_charJob.resultPath, CHAR_VAL, g_char.chainPath[0] ? g_char.chainPath : g_char.chainOut);
        Char_Copy(g_charJob.resultSha, 80, g_char.chainSha);
        g_charJob.resultBytes = GetFileAttributesExW(g_charJob.resultPath, GetFileExInfoStandard, &fa)
                                    ? (long long)(((unsigned long long)fa.nFileSizeHigh << 32) | fa.nFileSizeLow)
                                    : -1;
    }
    g_char.chainStep = CHAR_CHAIN_NONE;
    Char_BuildDone(page, 0);
    {
        int i;
        for (i = 0; i < g_char.chainMsgCount; i++)
            if (g_char.chainMsg[i].severity == RS_SEV_WARNING || g_char.chainMsg[i].severity == RS_SEV_ERROR)
                Char_MsgAdd(CHAR_TAB_BUILD, g_char.chainMsg[i].severity, g_char.chainMsg[i].text,
                            g_char.chainMsg[i].code[0] ? g_char.chainMsg[i].code : NULL);
    }
    Char_ChainCleanup();
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
    g_char.buildTried = 1;      // from now on every head shows its problems
    // An empty field: the game's characters folder as it is now (the game may
    // have been chosen since the last check), else rldpack's default.
    Char_FieldPath(g_char.out, field, CHAR_VAL);
    if (!field[0]) {
        wchar_t model[CHAR_VAL];
        Char_FieldPath(g_char.model, model, CHAR_VAL);
        Char_DefaultOut(model, field, CHAR_VAL);
    }
    Char_CleanPath(path, CHAR_VAL, field[0] ? field : g_char.checkedPath);
    g_char.builtFresh = 0;
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
    if (Char_Chrg())
        return Char_ChainStart(page, path);
    g_char.chainStep = CHAR_CHAIN_NONE;
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
    g_char.builtFresh = 0;      // the fields are no longer those of the build: Build again, no Test in game
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
        Char_OutNote(j->out);
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
    Char_PreviewShow();         // a CHRG route: the hi-res picture or the classic fallback
    // The cards of the preview features follow the model just checked.
    CharWheels_ModelChecked(page, g_char.checkModel, g_char.sizeNow, g_char.checked);
    CharAnim_ModelChecked(page, g_char.checkModel, g_char.importFormat, &j->poses, g_char.checked);
    return 1;
}

static void Char_BuildDone(HWND page, int exitCode)
{
    struct CharJobData *j = &g_charJob;
    int ok = j->resultSeen && wcscmp(j->resultState, L"ok") == 0;
    wchar_t text[CHAR_VAL + 64];
    wchar_t detail[192];
    wchar_t size[32];

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
        // through (a combo turned by the mouse wheel) shows at once. The
        // classic model from a character: those are the character's.
        if (g_char.buildFromFile) {
            wchar_t from[CHAR_VAL];
            Char_ClassicPath(from, CHAR_VAL);
            swprintf(text, CHAR_VAL + 64, L"Built: %ls (%ls) - classic model, name, look and voices of %ls. Restart the game to load it.",
                     Rs_PathName(g_char.built), size, Rs_PathName(from));
            Char_Headline(text, RS_COL_OK);
            swprintf(text, CHAR_VAL + 64, L"Built %ls", g_char.built);
            swprintf(detail, 192, L"The classic model of %ls - SHA-256 %ls", Rs_PathName(from), g_char.builtSha);
        } else {
            swprintf(text, CHAR_VAL + 64, L"Built: %ls (%ls) - mask %ls, kart wheels %ls%ls. Restart the game to load it.",
                     Rs_PathName(g_char.built), size, mask, wheelsOff ? L"hidden" : L"shown", mapText);
            Char_Headline(text, RS_COL_OK);
            swprintf(text, CHAR_VAL + 64, L"Built %ls", g_char.built);
            swprintf(detail, 192, L"Kart wheels %ls, mask %ls%ls - SHA-256 %ls", wheelsOff ? L"hidden" : L"shown", mask, mapText,
                     g_char.builtSha);
        }
        Char_MsgAdd(CHAR_TAB_BUILD, RS_SEV_OK, text, detail);
        // The game loads characters only from the folder characters next to
        // it, and reads that folder once, when it starts.
        if (!Char_InGameCharDir(g_char.built)) {
            wchar_t dir[CHAR_VAL];
            if (Char_GameCharDir(dir, CHAR_VAL)) {
                swprintf(text, CHAR_VAL + 64, L"Copy it into %ls - the game loads characters only from there.", dir);
                Char_MsgAdd(CHAR_TAB_BUILD, RS_SEV_NOTE, text, NULL);
            } else {
                Char_MsgAdd(CHAR_TAB_BUILD, RS_SEV_NOTE,
                              L"Copy it into the characters folder next to ctr_native.exe - the game loads characters "
                              L"only from there.",
                              L"Reload Studio does not know the game yet: choose it on the page Test in game, then "
                              L"a build with an empty Output goes into its characters folder.");
            }
        }
        Char_MsgAdd(CHAR_TAB_BUILD, RS_SEV_INFO, L"Test in game starts it at once; a game already running loads it after a restart.",
                      L"The game reads the characters folder only when it starts.");
        Char_AddRunProblems(exitCode, 1);
        // The warnings and notes of make-char at their steps (a CHRG route:
        // those about the model stay behind Show details, as in its check).
        if (Char_Chrg()) {
            int i;
            for (i = 0; i < g_charJob.msgCount; i++) {
                const struct CharMsg *m = &g_charJob.msgs[i];
                int tab = Char_TabOfCode(m->code);
                if ((m->severity == RS_SEV_WARNING || m->severity == RS_SEV_NOTE) && tab != CHAR_TAB_MODEL)
                    Char_MsgAddCard(tab, tab == CHAR_TAB_LOOK ? Char_LookCardOfCode(m->code) : -1, m->severity, m->text, m->detail);
            }
        } else {
            Char_AddMsgs(RS_SEV_WARNING);
            Char_AddMsgs(RS_SEV_NOTE);
        }
        g_char.builtFresh = 1;
        Rs_ConfigSet(L"char.out", g_char.built);
        if (Rs_Automating()) {
            Rs_AutoLog(L"  build: ok %ls %lld bytes sha256 %ls", g_char.built, g_char.builtBytes, g_char.builtSha);
            if (g_char.buildFromFile)
                Rs_AutoLog(L"  build: the classic model, name, look and voices of the character it was packed into");
            else
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
    Char_CountTabs();
    Char_UpdateButtons();
    Char_Relayout(page);        // show or hide "Show in folder", Build or Test in game
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
        Char_ChainCleanup();        // a CHRG route: its temp files
        Char_MsgAdd(CHAR_TAB_BUILD, RS_SEV_INFO, L"The build was cancelled - nothing was written.",
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
    if (kind == CHAR_JOB_BUILD && g_char.chainStep != CHAR_CHAIN_NONE) {
        // A CHRG route: the next step of its build, or its end.
        Char_ChainDone(page, exitCode);
        if (g_char.jobId) {
            Char_UpdateButtons();
            return;
        }
        if (g_char.checkAfterBuild)
            Char_Check(page);
    } else if (kind == CHAR_JOB_BUILD) {
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
// THE NAME FROM THE FILE: a model chosen while the name field is empty (or
// still holds a name made this way) gives the name of its file - upper case,
// the characters of the game's menu font only (any other one a space, spaces
// not doubled, none at the ends), at most CHAR_NAME_FROM_FILE (15: make-char
// notes name-long above that - a name the Studio makes should not draw a note
// of its own). So the step Driver
// needs the author only to change it, and the shortest way is card, model,
// the head Build, Build. A name the author typed is never replaced
// (nameDerived goes to 0 with the first change of the field that is not this
// one). Nothing usable in the file name: the field stays as it was.
#define CHAR_NAME_FROM_FILE 15

static void Char_NameFromModel(const wchar_t *path)
{
    wchar_t name[CHAR_NAME_MAX + 1], *now;
    const wchar_t *p = Rs_PathName(path), *dot = wcsrchr(p, L'.');
    int n = 0;

    now = Rs_GetText(g_char.name);
    if (now[0] && !g_char.nameDerived) {
        Rs_Free(now);
        return;
    }
    Rs_Free(now);
    for (; *p && p != dot && n < CHAR_NAME_FROM_FILE; p++) {
        wchar_t c = *p;
        if (c >= L'a' && c <= L'z')
            c = (wchar_t)(c - L'a' + L'A');
        if (!Char_NameCharAllowed(c) || c == L' ') {
            if (n > 0 && name[n - 1] != L' ')
                name[n++] = L' ';
            continue;
        }
        name[n++] = c;
    }
    while (n > 0 && name[n - 1] == L' ')
        n--;
    name[n] = 0;
    if (!n)
        return;
    g_char.applying = 1;
    Rs_SetText(g_char.name, name);
    g_char.applying = 0;
    g_char.nameDerived = 1;
    Char_NameFilter();          // the line below the field
    if (Rs_Automating())
        Rs_AutoLog(L"  name: %ls (from the file name)", name);
}

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
        Char_NameFromModel(clean);
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
    // The steering of the card Animations follows (the same frames 10, 0, 20).
    CharAnim_SteerFromPage(pose == 1 ? -RS_VIEW_STEER_MAX : pose == 2 ? RS_VIEW_STEER_MAX : 0);
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
    // Low-Poly and Hi-Res read a .glb only (E6) - and a finished character,
    // to look at its hi-res model (THE FINISHED CHARACTER).
    if (Rs_BrowseOpenFile(Rs_MainWindow(), L"Choose the character model",
                          Char_Chrg() ? L"glTF binary (*.glb)\0*.glb\0Finished character, to look at (*.rldchar)\0*.rldchar\0\0"
                                      : CHAR_MODEL_FILTER,
                          start, pick, CHAR_VAL))
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

// A model Reload Studio reads: glTF (binary .glb or .gltf with its files), OBJ
// or PLY - by its ending, as the file dialog lists them.
static int Char_ModelFile(const wchar_t *path)
{
    return Char_EndsWith(path, L".glb") || Char_EndsWith(path, L".gltf") || Char_EndsWith(path, L".obj") ||
           Char_EndsWith(path, L".ply");
}

// A model whose textures may lie beside it as files: glTF or OBJ (a .png
// dropped with it is its texture, not the icon).
static int Char_ModelWithTextures(const wchar_t *path)
{
    return Char_EndsWith(path, L".glb") || Char_EndsWith(path, L".gltf") || Char_EndsWith(path, L".obj");
}

// A file ending of another 3D format: dropped, it goes into the model field
// all the same, and the check says what it is and which formats are read.
static int Char_OtherModelFile(const wchar_t *path)
{
    static const wchar_t *const endings[] = { L".fbx", L".stl", L".blend", L".3ds", L".dae", L".usd", L".usdz" };
    int i;
    for (i = 0; i < (int)(sizeof(endings) / sizeof(endings[0])); i++)
        if (Char_EndsWith(path, endings[i]))
            return 1;
    return 0;
}

// Files dropped onto the page (or given to the automation verb "drop"): a
// .glb, .gltf, .obj or .ply is the model, a .png the icon, a folder the voices
// folder. A file of another 3D format is the model only when none of those
// came with it. A .png that comes with a glTF or an OBJ is not the icon:
// dropped with it it is its texture (rldpack reads it through the model),
// and an icon that changes unasked would go unnoticed. With a .ply it is the
// icon as always. While the card Wheels is shown, a model file is the wheel
// model instead (the first one; the model keeps its file).
// Returns the number of paths taken.
static int Char_DropPaths(HWND page, const wchar_t *const *paths, int count)
{
    wchar_t model[CHAR_VAL], other[CHAR_VAL];
    int i, tab = -1, taken = 0, withModel = 0, withTextures = 0;

    model[0] = 0;
    other[0] = 0;
    if (g_char.tab == CHAR_TAB_MODEL && g_char.advOpen && g_char.extrasCard == CHAR_EXTRAS_WHEELS && Char_ExtrasShown(CHAR_EXTRAS_WHEELS)) {
        for (i = 0; i < count; i++)
            if (Char_ModelFile(paths[i])) {
                CharWheels_Automate(page, L"wheel-model", paths[i]);
                return 1;
            }
    }
    for (i = 0; i < count; i++) {
        if (Char_ModelWithTextures(paths[i]))
            withTextures = 1;
        if (Char_ModelFile(paths[i]))
            withModel = 1;
    }
    for (i = 0; i < count; i++) {
        const wchar_t *path = paths[i];
        if (Rs_DirExists(path)) {
            Rs_SetText(g_char.voices, path);
            if (tab < 0)
                tab = CHAR_TAB_VOICES;
        } else if (Char_EndsWith(path, L".png") && !withTextures) {
            Rs_SetText(g_char.icon, path);
            if (tab < 0 || tab == CHAR_TAB_VOICES)
                tab = CHAR_TAB_LOOK;
        } else if (Char_ModelFile(path)) {
            Char_Copy(model, CHAR_VAL, path);
            tab = CHAR_TAB_MODEL;
        } else if ((Char_OtherModelFile(path) || (Char_Chrg() && Char_EndsWith(path, L".rldchar"))) && !withModel) {
            // (A finished character on Low-Poly or Hi-Res: to look at its model.)
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

// Enter (IDOK): on a button of the page (a push button, a head, a switch, a
// card of step 0) press it, otherwise check immediately.
static void Char_Enter(HWND page)
{
    HWND focus = GetFocus();
    wchar_t cls[16];
    LONG_PTR type = focus ? (GetWindowLongPtrW(focus, GWL_STYLE) & BS_TYPEMASK) : 0;
    if (focus && GetParent(focus) == page && GetClassNameW(focus, cls, 16) && _wcsicmp(cls, L"Button") == 0 &&
        (type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON || type == BS_OWNERDRAW)) {
        SendMessageW(focus, BM_CLICK, 0, 0);
    } else if (focus && GetParent(focus) == page && !g_char.choosing) {
        Char_CheckNow(page);
    }
}

// ---------------------------------------------------------------------------
// The route: step 0, choosing, changing it
// ---------------------------------------------------------------------------

// Step 0 again ("Change type", Back on the step Model): everything entered
// stays; the card of the route chosen is marked and has the focus.
static void Char_ShowChoice(HWND page)
{
    g_char.choosing = 1;
    Char_HiresStop();
    RsView_PickBegin(g_char.view, 0);
    Char_Relayout(page);
    SetFocus(g_char.routeCard[g_char.route >= 0 && g_char.route < RS_ROUTES ? g_char.route : 0]);
}

// The texts that name the formats of the route: the cue of the model field
// and its line while no model is chosen.
static void Char_RouteTexts(void)
{
    const struct RsRoutePreset *p = RsRoute_Preset(g_char.route);
    wchar_t label[96];
    SendMessageW(g_char.model, EM_SETCUEBANNER, FALSE,
                 (LPARAM)(Char_Chrg() ? L"A .glb (glTF Binary) of driver, steering wheel and kart"
                                      : L"A glTF (.glb, recommended), OBJ or PLY of driver, steering wheel and kart"));
    swprintf(label, 96, L"Building: %ls", p ? p->title : L"-");
    Rs_SetText(g_char.routeLabel, label);
    Rs_SetText(g_char.previewSwitch[0], Char_PreviewText(0));
    Char_PreviewTip();
}

// The route chosen (a card, the automation verb "route"): the steps of that
// route. Another route than before starts at its step Model, Advanced
// closed, with its defaults (E7: "Upscale small textures" as its preset
// says - off; the check of the model then suggests it or not, THE UPSCALE),
// and checks the model again with its rules; every entry stays.
// RS_ROUTE_NONE: step 0 without a route. Returns 1 when a check started.
static int Char_SetRoute(HWND page, int route)
{
    const struct RsRoutePreset *p = RsRoute_Preset(route);
    const int was = g_char.route;
    wchar_t model[CHAR_VAL];

    if (!p) {
        g_char.route = RS_ROUTE_NONE;
        Char_ShowChoice(page);
        return 0;
    }
    g_char.choosing = 0;
    if (was == route) {
        Char_MsgRefill();
        Char_Relayout(page);
        Char_PreviewShow();
        SetFocus(g_char.tabHead[g_char.tab]);
        return 0;
    }
    g_char.route = route;
    Char_HiresStop();           // the idle clip of the route before
    if (!Char_Chrg())
        RsView_SetStage(g_char.view, 0);
    g_char.hiresShown = 0;
    g_char.advOpen = 0;
    Rs_SetText(g_char.advanced, L"Advanced options");
    g_char.previewClassic = 0;
    g_char.builtFresh = 0;
    g_char.showRaw = 0;
    Rs_SetText(g_char.rawToggle, L"Show details");
    Char_SetChecked(g_char.upscale, p->upscaleDefault);
    g_char.upSuggested = 0;     // until the check of the model on this route
    g_char.upChosen = 0;
    Rs_SetText(g_char.upscaleNote, L"");
    Rs_SetText(g_char.upscaleStep, L"");
    Char_SetChecked(g_char.gameKart, 0);
    g_char.kartSuggested = 0;   // until the check of the model on this route
    g_char.kartChosen = 0;
    Rs_SetText(g_char.gameKartNote, L"");
    Rs_SetText(g_char.gameKartStep, L"");
    Rs_SetText(g_char.pngStep, L"");    // until the check of the model on this route
    g_char.pngFixOn = 0;
    Char_ExtrasFix();
    Char_RouteTexts();
    g_char.tab = CHAR_TAB_MODEL;
    g_char.visited[CHAR_TAB_MODEL] = 1;
    Char_MsgRefill();
    Char_Relayout(page);
    SetFocus(g_char.tabHead[g_char.tab]);
    Char_FieldPath(g_char.model, model, CHAR_VAL);
    if (!model[0]) {
        Char_NoModel(page);
        return 0;
    }
    return Char_CheckNow(page) > 0;
}

// "Test in game" (rs_test.c, Rs_TestCharacter): the game with the character
// just built in a race at once; in automation only the command line is
// logged. The line below the buttons says what happened. 1 = started.
static int Char_TestInGame(HWND page)
{
    wchar_t why[512];
    int ok;
    if (!g_char.builtFresh || !g_char.built[0]) {
        Char_SetLabel(g_char.testNote, L"Build the character first.", RS_COL_WARNING, NULL);
        Char_Relayout(page);
        return 0;
    }
    why[0] = 0;
    ok = Rs_TestCharacter(g_char.built, g_char.route < 0 ? RS_ROUTE_VANILLA : g_char.route, why, 512);
    Char_SetLabel(g_char.testNote,
                  why[0] ? why : ok ? L"The game starts with your character in a race." : L"The game could not be started.",
                  ok ? RS_COL_OK : RS_COL_ERROR, NULL);
    Char_Relayout(page);
    if (Rs_Automating())
        Rs_AutoLog(L"  test-in-game: %ls%ls%ls", ok ? L"started" : L"not started", why[0] ? L" - " : L"", why);
    return ok;
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

// THE GUIDED FLOW in the report: the route, the step, Advanced, the options
// of the route, and every control shown with its text - the push buttons
// filled in the accent colour marked "primary" (one per step).
static const wchar_t *Char_SeverityWord(int severity)
{
    switch (severity) {
    case RS_SEV_ERROR: return L"error";
    case RS_SEV_WARNING: return L"warning";
    case RS_SEV_NOTE: return L"note";
    case RS_SEV_OK: return L"ok";
    }
    return L"info";
}

// The report of a CHRG route beyond "hires check:" - what the page Hi-Res,
// now gone, wrote: the model's source and the classic model (THE FINISHED CHARACTER,
// THE CLASSIC MODEL), the summary, every finding of the check one per line
// (the step Model folds the missing clips into one line; here each has its
// own), every clip, the clip shown, and the picture.
static void Char_ReportChrg(FILE *f)
{
    const struct RsHiresCheck *c = &g_char.hires;
    wchar_t line[CHAR_VAL + 96], path[CHAR_VAL];
    int i;
    const int clip = Char_ClipShown();

    Char_Put(f, L"model source: %ls", Char_ModelIsCharacter() ? L"a finished character (its CHRG checked and shown, nothing to build)"
                                                              : L"a .glb");
    Char_ClassicPath(path, CHAR_VAL);
    Char_Put(f, L"classic model: %ls%ls", g_char.classicFrom ? L"from the character " : L"made from the .glb",
             g_char.classicFrom ? (path[0] ? path : L"(none chosen)") : L"");
    GetWindowTextW(g_char.classicStep, line, CHAR_VAL + 96);
    Char_Put(f, L"classic step line: \"%ls\"%ls", line,
             !line[0] ? L"" : Char_IsShown(g_char.classicStep) && Char_IsShown(g_char.classicChange) ? L" (shown, with \"Change\")"
                                                                                                       : L" (not shown on this view)");
    if (Char_NoMake() && g_char.fileLookKnown) {
        static const wchar_t *const modes[3] = { L"retail", L"own", L"off" };
        const struct RsCharLook *l = &g_char.fileLook;
        Char_Put(f, L"look source: the character %ls - shadow %ls, exhaust %ls%ls", Rs_PathName(g_char.lookFile),
                 modes[l->shadow >= 0 && l->shadow <= 2 ? l->shadow : 0], modes[l->exhaust >= 0 && l->exhaust <= 2 ? l->exhaust : 0],
                 l->exhaust == RS_CHAR_LOOK_OWN ? (l->count == 2 ? L" (2 points)" : L" (1 point)") : L"");
    } else {
        Char_Put(f, L"look source: %ls", Char_NoMake() ? L"none (the character file could not be read)" : L"the fields of the step In-game look");
    }
    if (!g_char.hiresChecked || !c->read)
        return;
    Char_Put(f, L"hires summary: clips %d/%d wheels %d/4 joints %u triangles %llu textures %d bytes %llu of %llu edge %u of %u finish clips %d/%d",
             c->clips, RS_HIRES_CLIPS_BASE, c->wheels, c->joints, c->triangles, c->textures, c->textureBytes, c->budgetTextureBytes,
             c->textureEdge, c->budgetEdge, c->finishClips, RS_HIRES_CLIPS - RS_HIRES_CLIPS_BASE);
    for (i = 0; i < c->rowCount; i++)
        Char_Put(f, L"hires row: %ls %ls %ls", Char_SeverityWord(c->rows[i].severity), c->rows[i].rule[0] ? c->rows[i].rule : L"-",
                 c->rows[i].text);
    if (c->rowsDropped > 0)
        Char_Put(f, L"hires rows not listed: %d", c->rowsDropped);
    for (i = 0; i < RS_HIRES_CLIPS; i++) {
        if (c->clipState[i] == RS_HIRES_CLIP_PRESENT)
            Char_Put(f, L"hires clip: %ls present %.2f", RsHires_ClipName(i), (double)c->clipSeconds[i]);
        else if (c->clipState[i] == RS_HIRES_CLIP_FINISH) {
            wchar_t what[64];
            RsHires_FinishText(c, i, what, 64);
            Char_Put(f, L"hires clip: %ls optional, missing - after the finish line: %ls (shown: %ls)", RsHires_ClipName(i), what,
                     c->clipVia[i] >= 0 ? RsHires_ClipName(c->clipVia[i]) : L"rest");
        } else if (c->clipState[i] == RS_HIRES_CLIP_FALLBACK)
            Char_Put(f, L"hires clip: %ls fallback %ls%ls", RsHires_ClipName(i), RsHires_ClipName(c->clipVia[i]),
                     c->clipFullLock[i] ? L" full-lock" : L"");
        else
            Char_Put(f, L"hires clip: %ls rest", RsHires_ClipName(i));
    }
    Char_Put(f, L"clip shown: %ls %.3f %.1f %ls", clip > 0 ? RsHires_ClipName(clip - 1) : L"bind", (double)g_char.animSeconds,
             (double)g_char.animWheel, g_char.clipPlaying && clip > 0 ? L"playing" : L"paused");
    Char_Put(f, L"anim: %ls", c->animReady ? L"ready" : c->animWhy[0] ? c->animWhy : L"unavailable");
    Char_Put(f, L"hires picture: %d triangles, textured %ls%ls%ls", c->previewTriangles, c->previewTextured ? L"yes" : L"no",
             !c->previewTextured && c->previewWhy[0] ? L" - " : L"", !c->previewTextured ? c->previewWhy : L"");
}

static void Char_ReportFlow(FILE *f)
{
    const struct RsRoutePreset *p = RsRoute_Preset(g_char.route);
    HWND page = GetParent(g_char.model), c;
    int primary = 0;
    wchar_t *text;

    Char_Put(f, L"route: %ls%ls", p ? p->word : L"none", g_char.choosing ? L" (step 0 shown: What do you want to build?)" : L"");
    if (g_char.choosing)
        Char_Put(f, L"step: 0 choose");
    else
        Char_Put(f, L"step: %d %ls", g_char.tab + 1, g_charTabWords[g_char.tab]);
    // THE CREATOR KIT: its button of step 0 and its line of the step Model.
    Char_Put(f, L"kit option: %ls", Char_IsShown(g_char.kitOpen)
                                         ? L"shown (\"Creator Kit\", top right of step 0; the kit's icon, the title in the font of "
                                           L"the card titles, \"Templates to get started\" small below it)"
                                         : L"not shown");
    text = Rs_GetText(g_char.kitStep);
    Char_Put(f, L"kit step line: %ls", Char_IsShown(g_char.kitStep) && Char_IsShown(g_char.kitLink) ? text : L"(none)");
    Rs_Free(text);
    Char_Put(f, L"advanced: %ls%ls (cards on this route: %d)", g_char.advOpen ? L"open, card " : L"closed",
             g_char.advOpen ? g_charExtrasWords[g_char.extrasCard] : L"", Char_ExtrasCount());
    {
        // THE UPSCALE: the box, who set it (the author, the suggestion, or
        // the route's start before a check), the suggestion with its reason,
        // the preset and the line below the box.
        const int checked = Char_Chrg() && g_char.hiresChecked && g_char.hires.read;
        wchar_t why[96], line[RS_WIZ_TEXT_MAX];
        if (!checked)
            swprintf(why, 96, L"no model checked");
        else if (g_char.hires.baseTextures == 0)
            swprintf(why, 96, L"no base colour texture");
        else if (g_char.hires.baseUnsized > 0)
            swprintf(why, 96, L"%d of %d base colour texture(s) of unknown size", g_char.hires.baseUnsized, g_char.hires.baseTextures);
        else
            swprintf(why, 96, L"%d base colour texture(s), largest %u px in the file, suggested under %u", g_char.hires.baseTextures,
                     g_char.hires.baseEdgeFile, RS_UPSCALE_SUGGEST_EDGE);
        GetWindowTextW(g_char.upscaleNote, line, RS_WIZ_TEXT_MAX);
        Char_Put(f, L"upscale small textures: %ls (%ls), suggested %ls (%ls), preset %ls, line \"%ls\"%ls",
                 Char_IsChecked(g_char.upscale) ? L"on" : L"off",
                 g_char.upChosen ? L"chosen by the author" : checked ? L"suggested" : L"the route's start",
                 g_char.upSuggested ? L"on" : L"off", why, p && p->upscaleDefault ? L"on" : L"off", line,
                 Char_Chrg() ? L"" : L" [no CHRG route: hidden, not used]");
        // The line on the step Model (L"" = none) and whether it is shown now
        // with "Change" at its end.
        GetWindowTextW(g_char.upscaleStep, line, RS_WIZ_TEXT_MAX);
        Char_Put(f, L"upscale step line: \"%ls\"%ls", line,
                 !line[0] ? L"" : Char_IsShown(g_char.upscaleStep) && Char_IsShown(g_char.upscaleChange) ? L" (shown, with \"Change\")"
                                                                                                           : L" (not shown on this view)");
        // THE KART: the box, who set it, the suggestion, the lines, and what
        // the next build packs.
        GetWindowTextW(g_char.gameKartNote, line, RS_WIZ_TEXT_MAX);
        Char_Put(f, L"game kart: %ls (%ls), suggested %ls, line \"%ls\"%ls", Char_IsChecked(g_char.gameKart) ? L"on" : L"off",
                 g_char.kartChosen ? L"chosen" : checked || Char_VanillaKart() >= 0 ? L"suggested" : L"default",
                 g_char.kartSuggested ? L"on" : L"off", line,
                 Char_KartBoxWanted() ? L"" : Char_Chrg() ? L" [no box on this model]"
                                                          : L" [Vanilla: no box - the model is no .glb, has a kart part or is not read]");
        GetWindowTextW(g_char.gameKartStep, line, RS_WIZ_TEXT_MAX);
        Char_Put(f, L"game kart step line: \"%ls\"%ls", line,
                 !line[0] ? L"" : Char_IsShown(g_char.gameKartStep) && Char_IsShown(g_char.gameKartChange) ? L" (shown, with \"Change\")"
                                                                                                             : L" (not shown on this view)");
        // Vanilla (K6): the kart case of its model and whether make-char gets
        // --figure-only and (a skin) --rig bind-pose.
        if (!Char_Chrg())
            Char_Put(f, L"vanilla kart: %ls, make-char --figure-only: %ls, --rig bind-pose: %ls (skins %d)",
                     g_char.vanKart == RS_KART_GAME ? L"the kit's kart joint, no kart" : g_char.vanKart == RS_KART_PART ? L"a kart part"
                     : g_char.vanKart == RS_KART_OWN ? L"own (no kit joint)" : L"none (no .glb)",
                     Char_KartBoxWanted() && Char_IsChecked(g_char.gameKart) ? L"yes" : L"no",
                     g_char.route == RS_ROUTE_VANILLA && g_char.vanKart >= 0 && g_char.vanSkins > 0 ? L"yes" : L"no",
                     g_char.vanSkins);
        {
            const unsigned parts = Char_KartParts();
            Char_Put(f, L"parts: %ls", (parts & RS_HIRES_PARTS_GAME_KART) == RS_HIRES_PARTS_GAME_KART
                                       ? L"game kart (donor template), classic model: the figure on a stand-in kart that the game swaps for the original kart "
                                         L"from the player's game data (--figure-only, 4.3); the grey stand-in only in a game before 4.3 or without game data"
                                       : (parts & RS_HIRES_PARTS_KART) ? L"kart part" : L"none");
        }
        // THE JPEG TEXTURES: the box (only ever the author's), what the check
        // shown is of, the button "Convert to PNG" and the line on the step
        // Model.
        Char_Put(f, L"png convert: %ls (%ls), check of %ls, jpeg textures %d%ls%ls, button \"Convert to PNG\" %ls%ls",
                 Char_IsChecked(g_char.png) ? L"on" : L"off", Char_IsChecked(g_char.png) ? L"chosen by the author" : L"the start: never suggested",
                 !checked ? L"no model" : g_char.hiresPng ? L"the model as converted" : L"the model as it is",
                 g_char.hiresPng ? g_char.pngJpeg : g_char.hires.jpegTextures, g_char.pngWhy[0] ? L", failed: " : L"", g_char.pngWhy,
                 g_char.pngFixOn ? L"offered" : L"not offered", g_char.pngFixOn && !Char_IsShown(g_char.pngFix) ? L" (not shown on this view)" : L"",
                 Char_Chrg() ? L"" : L" [no CHRG route: hidden, not used]");
        GetWindowTextW(g_char.pngStep, line, RS_WIZ_TEXT_MAX);
        Char_Put(f, L"png step line: \"%ls\"%ls", line,
                 !line[0] ? L"" : Char_IsShown(g_char.pngStep) && Char_IsShown(g_char.pngChange) ? L" (shown, with \"Change\")"
                                                                                                   : L" (not shown on this view)");
    }
    if (p)
        Char_Put(f, L"preset: chrg %d, triangle target %llu, texture edge target %u", p->chrg, p->triangleTarget, p->textureEdgeTarget);
    Char_Put(f, L"details: %ls", g_char.showRaw ? L"shown" : L"hidden");
    Char_Put(f, L"preview: %ls", !Char_Chrg() ? L"classic (Vanilla)" : g_char.previewClassic ? L"classic fallback"
             : g_char.route == RS_ROUTE_LOWPOLY ? L"low-poly" : L"hi-res");
    Char_Put(f, L"preview stage: %ls", RsView_GetStage(g_char.view) ? L"neutral" : L"the view's own");
    if (Char_Chrg()) {
        static const wchar_t *const whyWords[] = { L"no COLOR_0", L"PS1-like", L"white is neutral", L"not decidable - the route's" };
        if (g_char.vcolMode < 0)
            Char_Put(f, L"vertex colors: none (the model has no COLOR_0: no switch, hires-add without --vertex-colors)");
        else
            Char_Put(f, L"vertex colors: %ls (%ls), suggested %ls (%ls, median %d), switch %ls", RsHires_ColourWord(g_char.vcolMode),
                     g_char.vcolChosen ? L"chosen by the author" : L"suggested", RsHires_ColourWord(g_char.vcolSuggested),
                     g_char.vcolWhy >= 0 && g_char.vcolWhy <= 3 ? whyWords[g_char.vcolWhy] : L"?", g_char.hires.vcolMedian,
                     g_char.vcolMode == RS_HIRES_VCOL_PSX ? L"on" : g_char.vcolMode == RS_HIRES_VCOL_OFF ? L"off" : L"glTF (Advanced)");
    }
    if (Char_Chrg()) {
        if (g_char.hiresChecked && g_char.hires.read)
            Char_Put(f, L"hires check: %ls, %llu triangles (budget %llu), texture edge %u (budget %u), %d of %d clips, %d of %d finish clips, %d wheel nodes, "
                        L"%d error(s), %d warning(s), animation %ls",
                     g_char.hires.usable ? L"usable" : L"refused", g_char.hires.triangles, g_char.hires.budgetTriangles,
                     g_char.hires.textureEdge, g_char.hires.budgetEdge, g_char.hires.clips, RS_HIRES_CLIPS_BASE,
                     g_char.hires.finishClips, RS_HIRES_CLIPS - RS_HIRES_CLIPS_BASE, g_char.hires.wheels,
                     g_char.hires.errors, g_char.hires.warnings, g_char.hires.animReady ? L"plays" : L"none");
        else
            Char_Put(f, L"hires check: %ls", g_char.hiresChecked ? g_char.hires.why : L"(none)");
        Char_ReportChrg(f);
    }
    text = Rs_GetText(g_char.testNote);
    Char_Put(f, L"test note: %ls", text[0] ? text : L"(none)");
    Rs_Free(text);
    for (c = page ? GetWindow(page, GW_CHILD) : NULL; c; c = GetWindow(c, GW_HWNDNEXT)) {
        wchar_t cls[32];
        LONG_PTR style = GetWindowLongPtrW(c, GWL_STYLE), type = style & BS_TYPEMASK;
        const wchar_t *kind;
        int isPrimary;
        if (!(style & WS_VISIBLE) || !GetClassNameW(c, cls, 32))
            continue;
        isPrimary = GetPropW(c, L"RsPrimary") != NULL;
        if (_wcsicmp(cls, L"Button") == 0)
            kind = type == BS_AUTOCHECKBOX || type == BS_CHECKBOX || type == BS_3STATE || type == BS_AUTO3STATE ? L"tick box"
                   : isPrimary                                                                                    ? L"primary button"
                                                                                                                  : L"button";
        else if (_wcsicmp(cls, L"Edit") == 0)
            kind = L"field";
        else if (_wcsicmp(cls, L"ComboBox") == 0)
            kind = L"list";
        else
            continue;
        primary += isPrimary;
        text = Rs_GetText(c);
        Char_Put(f, L"shown %ls: %d %ls%ls", kind, GetDlgCtrlID(c), text, IsWindowEnabled(c) ? L"" : L" [greyed out]");
        Rs_Free(text);
    }
    Char_Put(f, L"primary buttons shown: %d", primary);
    {
        int i;
        Char_Put(f, L"page messages: %d", g_char.pageMsgCount);
        for (i = 0; i < g_char.pageMsgCount; i++)
            Char_Put(f, L"page message: %ls\t%d\t%ls\t%ls", g_char.pageMsg[i].tab >= 0 ? g_charTabWords[g_char.pageMsg[i].tab] : L"-",
                     g_char.pageMsg[i].severity, g_char.pageMsg[i].text, g_char.pageMsg[i].detail ? g_char.pageMsg[i].detail : L"");
    }
}

static int Char_WriteReport(const wchar_t *path)
{
    FILE *f = _wfopen(path, L"wb");
    wchar_t *text;
    int i;

    if (!f)
        return 0;
    Char_Put(f, L"Reload Studio - Character page");
    Char_ReportFlow(f);
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

// "tab 1..5" or "tab model|driver|look|voices|build": as a click on the head
// (the verb of before; "step" is the same). "tab extras" (the tab of before)
// opens Advanced of the step Model at its card Import.
static int Char_AutoExtras(HWND page, const wchar_t *arg);
static int Char_AutoTab(HWND page, const wchar_t *arg)
{
    int t;
    if (_wcsicmp(arg, L"extras") == 0)
        return Char_AutoExtras(page, L"import");
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
    Rs_AutoLog(L"  tab: say 1 to 5, or model, driver, look, voices, build or extras");
    return RS_AUTO_FAIL;
}

// "extras import|wheels|animations|options": that card of Advanced (the tab
// Extras of before), Advanced open on the step Model. A card the route does
// not have fails ("hidden").
static int Char_AutoExtras(HWND page, const wchar_t *arg)
{
    int c;
    for (c = 0; c < CHAR_EXTRAS_COUNT; c++) {
        if (_wcsicmp(arg, g_charExtrasWords[c]) == 0) {
            if (!Char_ExtrasShown(c)) {
                Rs_AutoLog(L"  extras: %ls is hidden on this route%ls", g_charExtrasTexts[c],
                           c == CHAR_EXTRAS_WHEELS && !Char_Chrg() ? L" (Wheels: only with --enable-preview-features)" : L"");
                return RS_AUTO_FAIL;
            }
            g_char.extrasCard = c;
            g_char.advOpen = 1;
            Rs_SetText(g_char.advanced, L"Hide advanced");
            Char_SelectTab(page, CHAR_TAB_MODEL, 0);
            UpdateWindow(page);
            Rs_AutoLog(L"  extras: %ls (Advanced of the step Model)", g_charExtrasTexts[c]);
            return RS_AUTO_DONE;
        }
    }
    Rs_AutoLog(L"  extras: say import, wheels, animations or options");
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
    Rs_AutoLog(L"  problem: message %ld -> step %d %ls%ls%ls", n, g_char.tab + 1, g_charTabTexts[g_char.tab],
               g_char.tab == CHAR_TAB_MODEL && g_char.advOpen ? L", Advanced " : L"",
               g_char.tab == CHAR_TAB_MODEL && g_char.advOpen ? g_charExtrasTexts[g_char.extrasCard] : L"");
    return RS_AUTO_DONE;
}

// "step <1..5|model|driver|look|voices|build|next|back>": as a click on the
// head of the step, on Next or on Back (Back on the step Model: step 0).
static void Char_ShowChoice(HWND page);
static int Char_AutoStep(HWND page, const wchar_t *arg)
{
    int t;
    if (_wcsicmp(arg, L"next") == 0 || _wcsicmp(arg, L"back") == 0) {
        const int next = _wcsicmp(arg, L"next") == 0;
        if (next && g_char.tab >= CHAR_TABS - 1) {
            Rs_AutoLog(L"  step: Build is the last step - there is no Next");
            return RS_AUTO_FAIL;
        }
        SendMessageW(page, WM_COMMAND, MAKEWPARAM(next ? CHAR_ID_NEXT : CHAR_ID_BACK, BN_CLICKED), (LPARAM)(next ? g_char.next : g_char.back));
        UpdateWindow(page);
        if (g_char.choosing)
            Rs_AutoLog(L"  step: back to \"What do you want to build?\"");
        else
            Rs_AutoLog(L"  step: %d %ls (%ls)", g_char.tab + 1, g_charTabTexts[g_char.tab], g_charStepWords[g_char.step[g_char.tab]]);
        return RS_AUTO_DONE;
    }
    for (t = 0; t < CHAR_TABS; t++) {
        wchar_t num[4];
        swprintf(num, 4, L"%d", t + 1);
        if (wcscmp(arg, num) == 0 || _wcsicmp(arg, g_charTabWords[t]) == 0) {
            Char_SelectTab(page, t, 0);
            UpdateWindow(page);
            Rs_AutoLog(L"  step: %d %ls (%ls)", t + 1, g_charTabTexts[t], g_charStepWords[g_char.step[t]]);
            return RS_AUTO_DONE;
        }
    }
    Rs_AutoLog(L"  step: say 1 to 5, model, driver, look, voices, build, next or back");
    return RS_AUTO_FAIL;
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
        // Without a pan the pan stays: the view zooms about the middle of
        // the model (RsView_SetCamera).
        int n;
        wchar_t rest[8];
        RsView_GetCamera(g_char.view, NULL, NULL, NULL, &panX, &panY);
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
    if (wcscmp(verb, L"view-wheel") == 0) {
        // <x pixel> <y pixel> <steps>: the mouse wheel over that pixel of the
        // preview, one notch per step (+ zooms in, - out), as the real wheel.
        int px, py, steps, n, k;
        wchar_t rest[8];
        RECT rc;
        POINT pt;
        n = swscanf(arg, L"%d %d %d %7ls", &px, &py, &steps, rest);
        GetClientRect(g_char.view, &rc);
        if (n != 3 || px < 0 || py < 0 || px >= rc.right || py >= rc.bottom || steps < -20 || steps > 20 || steps == 0) {
            Rs_AutoLog(L"  view-wheel: say <x pixel> <y pixel> <steps> - a pixel of the preview (%d x %d), steps -20..20, not 0",
                       (int)rc.right, (int)rc.bottom);
            return RS_AUTO_FAIL;
        }
        pt.x = px;
        pt.y = py;
        ClientToScreen(g_char.view, &pt);
        for (k = 0; k < (steps < 0 ? -steps : steps); k++)
            SendMessageW(g_char.view, WM_MOUSEWHEEL, MAKEWPARAM(0, (WORD)(short)(steps < 0 ? -WHEEL_DELTA : WHEEL_DELTA)),
                         MAKELPARAM(pt.x, pt.y));
        RsView_GetCamera(g_char.view, &yaw, &pitch, &zoom, &panX, &panY);
        Char_AutoViewDone();
        Rs_AutoLog(L"  view-wheel: %d step(s) at %d %d: zoom %d %%, pan %d %d", steps, px, py, zoom, panX, panY);
        return RS_AUTO_DONE;
    }
    // "view-filter engine|nearest": how the hi-res picture samples its
    // textures - the engine's hi-res filter (the start) or nearest, for
    // comparison images; the model is read again and drawn so.
    if (wcscmp(verb, L"view-filter") == 0) {
        wchar_t model[CHAR_VAL];
        const int nearest = _wcsicmp(arg, L"nearest") == 0;
        if (!nearest && _wcsicmp(arg, L"engine") != 0) {
            Rs_AutoLog(L"  view-filter: say engine or nearest");
            return RS_AUTO_FAIL;
        }
        if (!Char_Chrg()) {
            Rs_AutoLog(L"  view-filter: only the picture of a Low-Poly or Hi-Res model has the engine's filter");
            return RS_AUTO_FAIL;
        }
        RsHires_SetPointSampling(nearest);
        Char_FieldPath(g_char.model, model, CHAR_VAL);
        if (g_char.hiresChecked && model[0]) {
            g_char.hiresStamp = 0;      // read again: the picture is made with the filter
            Char_HiresEnsure(model);
            Char_VcolUpdate();
            Char_PreviewShow();
        }
        Rs_AutoLog(L"  view-filter: %ls", nearest ? L"nearest" : L"engine");
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
        {
            // The triangles of the model drawn: the native one (all its
            // triangles in every frame) or the classic pose shown.
            int light, crash, shadow, exhaust, native = 0;
            const int drawsNative = RsView_GetToggles(g_char.view, &light, &crash, &shadow, &exhaust, &native) && g_char.previewNativeTris;
            Rs_AutoLog(L"  view-bench: %d pictures of %d x %d pixels, %lu triangles of the %ls model shown: picture %d us on average, "
                       L"p95 %d us, longest %d us",
                       n, w, h, drawsNative ? g_char.previewNativeTris : g_char.previewShown ? g_char.previewTris[g_char.poseNow] : 0ul,
                       drawsNative ? L"native" : L"classic", avg, p95, longest);
        }
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

    // "Model file": a label naming the formats ("Model (GLB/OBJ/PLY)") would
    // widen the label column of every tab - at 1366 x 768 the card Import
    // then needs a line more and the page scrolls. The formats are in the
    // cue, in the line below the field and in the file dialog.
    g_char.modelLabel = Rs_Label(page, CHAR_ID_MODEL_LABEL, L"Model file", RS_FONT_BOLD);
    g_char.model = Rs_Edit(page, CHAR_ID_MODEL, L"", 0);
    SendMessageW(g_char.model, EM_SETCUEBANNER, FALSE, (LPARAM)L"A glTF (.glb, recommended), OBJ or PLY of driver, steering wheel and kart");
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

    g_char.rawToggle = Rs_Button(page, CHAR_ID_RAW_TOGGLE, L"Show details");
    Rs_SetTip(g_char.rawToggle, L"Everything rldpack printed: each command, then every line it wrote.");
    g_char.outLabel = Rs_Label(page, CHAR_ID_OUT_LABEL, L"Save as", RS_FONT_BOLD);
    g_char.out = Rs_Edit(page, CHAR_ID_OUT, L"", 0);
    g_char.outNote = Rs_Label(page, CHAR_ID_OUT_NOTE, L"", RS_FONT_SMALL);
    Rs_SetTextColor(g_char.outNote, RS_COL_MUTED);
    Char_OutCue();
    g_char.outBrowse = Rs_Button(page, CHAR_ID_OUT_BROWSE, L"Browse...");
    // The check runs by itself (a model chosen, a field changed): no button.
    // The automation verb "check" and Enter still run it.
    g_char.check = Rs_Button(page, CHAR_ID_CHECK, L"Check");
    ShowWindow(g_char.check, SW_HIDE);
    g_char.build = Rs_PrimaryButton(page, CHAR_ID_BUILD, L"Build");
    g_char.test = Rs_PrimaryButton(page, CHAR_ID_TEST, L"Test in game");
    Rs_SetTip(g_char.test, L"Starts the game with this character in a race right away.");
    g_char.rebuild = Rs_Button(page, CHAR_ID_REBUILD, L"Build again");
    g_char.testNote = Rs_Label(page, CHAR_ID_TEST_NOTE, L"", RS_FONT_SMALL);
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
    g_char.next = Rs_PrimaryButton(page, CHAR_ID_NEXT, L"Next >");
    Rs_MsgListSetClickable(g_char.msgs, 1);
    Rs_MsgListSetWhole(g_char.msgs, 1);
    Rs_SetTip(g_char.msgs, L"The messages of this step. Click one about a card of Advanced to open it.");
    // THE GUIDED FLOW: step 0 with its three cards, the route in the head of
    // the steps with "Change type", Advanced of the step Model, "Upscale small
    // textures" (CHRG routes), the switch Low-Poly/Hi-Res | Classic fallback.
    g_char.route = RS_ROUTE_NONE;
    g_char.choosing = 1;
    g_char.routeAsk = Rs_Label(page, CHAR_ID_ROUTE_ASK, L"What do you want to build?", RS_FONT_SECTION);
    for (i = 0; i < RS_ROUTES; i++) {
        const struct RsRoutePreset *p = RsRoute_Preset(i);
        g_char.routeCard[i] = Char_TabButton(page, CHAR_ID_ROUTE + i, p ? p->title : L"", RS_FONT_SECTION);
        Rs_SetTip(g_char.routeCard[i], p ? p->sentence : NULL);
    }
    g_char.routeLabel = Rs_Label(page, CHAR_ID_ROUTE_LABEL, L"", RS_FONT_BOLD);
    SetWindowLongPtrW(g_char.routeLabel, GWL_STYLE, GetWindowLongPtrW(g_char.routeLabel, GWL_STYLE) | SS_RIGHT | SS_ENDELLIPSIS);
    g_char.changeType = Rs_Button(page, CHAR_ID_CHANGE_TYPE, L"Change type");
    Rs_SetTip(g_char.changeType, L"Back to \"What do you want to build?\" - everything entered stays.");
    g_char.vanKart = -1;
    // THE CREATOR KIT: step 0's button (owner-drawn, Char_DrawKitButton), the
    // line of the step Model.
    g_char.kitOpen = Char_TabButton(page, CHAR_ID_KIT_OPEN, L"Creator Kit", RS_FONT_SECTION);
    Rs_SetTip(g_char.kitOpen, L"Ready Blender templates for a driver of your own - Vanilla, Low-Poly and Hi-Res: a jointed "
                              L"puppet, its clips and the kart from your own game files.");
    g_char.kitStep = Rs_Label(page, CHAR_ID_KIT_STEP, L"No model yet? Start with the Creator Kit.", RS_FONT_SMALL);
    Rs_SetTextColor(g_char.kitStep, RS_COL_MUTED);
    g_char.kitLink = Rs_Button(page, CHAR_ID_KIT_LINK, L"Open Creator Kit");
    Rs_SetTip(g_char.kitLink, L"Opens the Creator Kit with the template of this way: a puppet to start your model from.");
    g_char.advanced = Rs_Button(page, CHAR_ID_ADVANCED, L"Advanced options");
    g_char.upscale = Rs_Check(page, CHAR_ID_UPSCALE, L"Upscale small textures");
    g_char.upscaleHelp = Rs_Label(page, CHAR_ID_UPSCALE_HELP,
                                  L"Small pixel textures are enlarged sharply before packing, so that they stay clear under the "
                                  L"game's smooth filter. The model file itself is not changed.",
                                  RS_FONT_SMALL);
    Rs_SetTextColor(g_char.upscaleHelp, RS_COL_MUTED);
    // THE UPSCALE: its line below the box, empty (not shown) unless suggested
    // or chosen against the suggestion (Char_UpscaleControls).
    g_char.upscaleNote = Rs_Label(page, CHAR_ID_UPSCALE_NOTE, L"", RS_FONT_SMALL);
    Rs_SetTextColor(g_char.upscaleNote, RS_COL_MUTED);
    // Its line on the step Model, with "Change" (a button, as "Change type").
    g_char.upscaleStep = Rs_Label(page, CHAR_ID_UPSCALE_STEP, L"", RS_FONT_SMALL);
    Rs_SetTextColor(g_char.upscaleStep, RS_COL_MUTED);
    g_char.upscaleChange = Rs_Button(page, CHAR_ID_UPSCALE_CHANGE, L"Change");
    Rs_SetTip(g_char.upscaleChange, L"Opens Advanced at \"Upscale small textures\".");
    // THE KART: the box (ticked by the check of a model with the kit's kart
    // joint), its help, its line below the box and its line on the step Model
    // with "Change" (Char_GameKartControls).
    g_char.gameKart = Rs_Check(page, CHAR_ID_GAMEKART, L"Kart: from the player's game data");
    g_char.gameKartHelp = Rs_Label(page, CHAR_ID_GAMEKART_HELP,
                                   L"For a figure without a kart: in the race the game puts the original kart from the player's own "
                                   L"game data under it. Leave it off for a model with a kart of its own.",
                                   RS_FONT_SMALL);
    Rs_SetTextColor(g_char.gameKartHelp, RS_COL_MUTED);
    g_char.gameKartNote = Rs_Label(page, CHAR_ID_GAMEKART_NOTE, L"", RS_FONT_SMALL);
    Rs_SetTextColor(g_char.gameKartNote, RS_COL_MUTED);
    g_char.gameKartStep = Rs_Label(page, CHAR_ID_GAMEKART_STEP, L"", RS_FONT_SMALL);
    Rs_SetTextColor(g_char.gameKartStep, RS_COL_MUTED);
    g_char.gameKartChange = Rs_Button(page, CHAR_ID_GAMEKART_CHANGE, L"Change");
    Rs_SetTip(g_char.gameKartChange, L"Opens Advanced at \"Kart: from the player's game data\".");
    // THE JPEG TEXTURES: the box (never ticked by the Studio itself), its
    // help, its line on the step Model with "Change", and the button of the
    // JPEG error's fix.
    g_char.png = Rs_Check(page, CHAR_ID_PNG, L"Convert JPEG textures to PNG");
    g_char.pngHelp = Rs_Label(page, CHAR_ID_PNG_HELP,
                              L"The game reads PNG textures only. Before packing, a copy of the model is written with its JPEG textures "
                              L"as PNG, pixel for pixel as decoded. The model file itself is not changed.",
                              RS_FONT_SMALL);
    Rs_SetTextColor(g_char.pngHelp, RS_COL_MUTED);
    g_char.pngStep = Rs_Label(page, CHAR_ID_PNG_STEP, L"", RS_FONT_SMALL);
    Rs_SetTextColor(g_char.pngStep, RS_COL_MUTED);
    g_char.pngChange = Rs_Button(page, CHAR_ID_PNG_CHANGE, L"Change");
    Rs_SetTip(g_char.pngChange, L"Opens Advanced at \"Convert JPEG textures to PNG\".");
    g_char.pngFix = Rs_Button(page, CHAR_ID_PNG_FIX, L"Convert to PNG");
    Rs_SetTip(g_char.pngFix, L"Converts the model's JPEG textures to PNG before packing (rldpack hires-png). Your file stays as it is.");
    // THE VERTEX COLOURS: the switch is a three-state box (the third state
    // glTF, chosen in Advanced); the page sets its state itself.
    g_char.vblend = Rs_Check(page, CHAR_ID_VBLEND, L"Vertex color blending");
    SetWindowLongPtrW(g_char.vblend, GWL_STYLE, (GetWindowLongPtrW(g_char.vblend, GWL_STYLE) & ~(LONG_PTR)BS_TYPEMASK) | BS_3STATE);
    Rs_SetTip(g_char.vblend, L"On: the vertex colours shade the textures as on the PS1 - grey 128 shows the texture unchanged, "
                             L"darker shades it, lighter brightens it. Off: the vertex colours are ignored. A third way, glTF "
                             L"(white is neutral), is under Advanced.");
    g_char.vblendNote = Rs_Label(page, CHAR_ID_VBLEND_NOTE, L"", RS_FONT_SMALL);
    Rs_SetTextColor(g_char.vblendNote, RS_COL_MUTED);
    g_char.vmodeLabel = Rs_Label(page, CHAR_ID_VMODE_LABEL, L"Vertex colors", RS_FONT_BOLD);
    g_char.vmode = Rs_Combo(page, CHAR_ID_VMODE);
    for (i = 0; i < CHAR_VMODES; i++)
        SendMessageW(g_char.vmode, CB_ADDSTRING, 0, (LPARAM)g_charVModeTexts[i]);
    SendMessageW(g_char.vmode, CB_SETCURSEL, 0, 0);
    g_char.vmodeHelp = Rs_Label(page, CHAR_ID_VMODE_HELP,
                                L"How the vertex colours meet the textures. PSX: as on the PS1, grey 128 is neutral. glTF: as the "
                                L"glTF standard says, white is neutral and the colour multiplies. Off: they are ignored.",
                                RS_FONT_SMALL);
    Rs_SetTextColor(g_char.vmodeHelp, RS_COL_MUTED);
    // THE CLASSIC MODEL (Advanced, card Options of a CHRG route): the list,
    // the character's field, its help; its line on the step Model with "Change".
    g_char.classicLabel = Rs_Label(page, CHAR_ID_CLASSIC_LABEL, L"Classic model", RS_FONT_BOLD);
    g_char.classicMode = Rs_Combo(page, CHAR_ID_CLASSIC_MODE);
    SendMessageW(g_char.classicMode, CB_ADDSTRING, 0, (LPARAM)L"Made from the .glb (default)");
    SendMessageW(g_char.classicMode, CB_ADDSTRING, 0, (LPARAM)L"From a finished character (.rldchar)");
    SendMessageW(g_char.classicMode, CB_SETCURSEL, 0, 0);
    g_char.classicFile = Rs_Edit(page, CHAR_ID_CLASSIC_FILE, L"", ES_AUTOHSCROLL);
    SendMessageW(g_char.classicFile, EM_SETCUEBANNER, FALSE, (LPARAM)L"The .rldchar the model is packed into");
    g_char.classicBrowse = Rs_Button(page, CHAR_ID_CLASSIC_BROWSE, L"Browse...");
    g_char.classicHelp = Rs_Label(page, CHAR_ID_CLASSIC_HELP,
                                  L"The game draws the classic model wherever the hi-res one is not drawn. Made from the .glb, the "
                                  L"steps Driver, In-game look and Voices set it up. From a finished character, the model is packed "
                                  L"into a copy of it: its classic model, name, look and voices stay, and its file is not changed.",
                                  RS_FONT_SMALL);
    Rs_SetTextColor(g_char.classicHelp, RS_COL_MUTED);
    g_char.classicStep = Rs_Label(page, CHAR_ID_CLASSIC_STEP, L"", RS_FONT_SMALL);
    Rs_SetTextColor(g_char.classicStep, RS_COL_MUTED);
    g_char.classicChange = Rs_Button(page, CHAR_ID_CLASSIC_CHANGE, L"Change");
    g_char.charNote = Rs_Label(page, CHAR_ID_CHAR_NOTE,
                               L"The model is a finished character: it is checked and shown as it is, nothing is built, so "
                               L"the choices of the build (Upscale small textures, Convert JPEG textures to PNG, Classic model) "
                               L"are not offered. The card Animations shows its clips.",
                               RS_FONT_SMALL);
    Rs_SetTextColor(g_char.charNote, RS_COL_MUTED);
    Rs_SetTip(g_char.classicChange, L"Opens Advanced at \"Classic model\".");
    // THE CLIPS (Advanced, card Animations of a CHRG route): the clip shown,
    // Play | Pause, a frame at a time, its time, the table of the 12 clips.
    g_char.clipLabel = Rs_Label(page, CHAR_ID_CLIP_LABEL, L"Clip", RS_FONT_BOLD);
    g_char.clipCombo = Rs_Combo(page, CHAR_ID_CLIP);
    g_char.clipPlay = Rs_Button(page, CHAR_ID_CLIP_PLAY, L"Play");
    g_char.clipPrev = Rs_Button(page, CHAR_ID_CLIP_PREV, L"< Frame");
    Rs_SetTip(g_char.clipPrev, L"One frame back (1/30 s, a tick of the game); the clip pauses.");
    g_char.clipNext = Rs_Button(page, CHAR_ID_CLIP_NEXT, L"Frame >");
    Rs_SetTip(g_char.clipNext, L"One frame on (1/30 s, a tick of the game); the clip pauses.");
    g_char.clipTime = Rs_Label(page, CHAR_ID_CLIP_TIME, L"", RS_FONT_SMALL);
    Rs_SetTextColor(g_char.clipTime, RS_COL_MUTED);
    g_char.clipTable = Rs_ListView(page, CHAR_ID_CLIP_TABLE, LVS_NOSORTHEADER);
    {
        static const wchar_t *const heads[3] = { L"Clip", L"In the file", L"Length" };
        LVCOLUMNW col;
        LVITEMW it;
        for (i = 0; i < 3; i++) {
            memset(&col, 0, sizeof(col));
            col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM | LVCF_FMT;
            col.fmt = LVCFMT_LEFT;
            col.pszText = (LPWSTR)heads[i];
            col.cx = Rs_Px(90);
            col.iSubItem = i;
            ListView_InsertColumn(g_char.clipTable, i, &col);
        }
        for (i = 0; i < RS_HIRES_CLIPS; i++) {
            memset(&it, 0, sizeof(it));
            it.mask = LVIF_TEXT;
            it.iItem = i;
            it.pszText = (LPWSTR)RsHires_ClipName(i);
            ListView_InsertItem(g_char.clipTable, &it);
        }
    }
    Rs_SetTip(g_char.clipTable, L"Click a clip to show it.");
    g_char.clipNote = Rs_Label(page, CHAR_ID_CLIP_NOTE,
                               L"The clips posed with the engine's own animation code, as the game plays them. A missing clip "
                               L"plays its stand-in (the table says which). finish_win and finish_lose are optional: a driver "
                               L"plays them after the finish line (places 1-3, from 4). The wheels turn slowly while a clip plays.",
                               RS_FONT_SMALL);
    Rs_SetTextColor(g_char.clipNote, RS_COL_MUTED);
    g_char.clipSel = -1;
    g_char.clipPlaying = !Rs_Automating();      // in automation only "hires-play on" plays: the shots stay the same
    Char_ClipFill();
    Char_ClipControls();
    g_char.vcolMode = g_char.vcolSuggested = -1;
    g_char.chainVcol = -1;
    g_char.chainParts = 0;
    for (i = 0; i < 2; i++)
        g_char.previewSwitch[i] = Char_TabButton(page, CHAR_ID_PREVIEW_SWITCH + i, Char_PreviewText(i), RS_FONT_SECTION);
    Char_PreviewTip();
    // Advanced: its switch, the card Import, then the cards of the preview
    // features.
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
                             L"character, beside the classic model; it needs Show kart wheels off or a wheel model of your own (Advanced, "
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
        HWND last[] = { g_char.advanced, g_char.outLabel, g_char.out, g_char.outBrowse, g_char.outNote, g_char.headline,
                        g_char.build, g_char.test, g_char.rebuild, g_char.show, g_char.rawToggle, g_char.testNote,
                        g_char.msgs, g_char.raw, g_char.changeType, g_char.previewSwitch[0], g_char.previewSwitch[1],
                        g_char.view, g_char.pose, g_char.viewNote, g_char.progress, g_char.cancel, g_char.back,
                        g_char.next, g_char.check };
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
    // Step 0 first: the heads of the steps are its first stops of Tab.
    SetWindowPos(g_char.routeAsk, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    for (i = RS_ROUTES - 1; i >= 0; i--)
        SetWindowPos(g_char.routeCard[i], g_char.routeAsk, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    // "Creator Kit" right after the three cards.
    SetWindowPos(g_char.kitOpen, g_char.routeCard[RS_ROUTES - 1], 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

// Label left, field right of it.
static void Char_PlaceField(HWND label, HWND field, int x, int labelW, int y, int fieldW)
{
    MoveWindow(label, x, y + Rs_Px(4), labelW, Rs_Px(20), TRUE);
    MoveWindow(field, x + labelW + Rs_Px(8), y, fieldW, Rs_Px(28), TRUE);
}

// Sizes of the layout (96 dpi). The page is laid out for the window it gets:
// the steps on the left, the preview on the right as tall as they are, the
// bar (Back, Next, the progress) below both, CHAR_BAR_H high. The room above
// it comes from the tallest step (Advanced only while it is open); the
// messages of the step shown take what is left below its fields, at least
// CHAR_STEP_MSGS_MIN_H. Only where that does not fit, the bar goes lower than
// the page and the shell scrolls it (reloadstudio.h, "Pages").
// THE COMPACT LAYOUT: a page narrower than CHAR_FULL_W or lower than
// CHAR_FULL_H (the start window on 1366 x 768 at 150 %: 1344 x 640 pixels
// of client, about 836 x 426 left for the page) gets a
// one-line heading (Rs_PageSetCompactHead), lower tab heads, notes on one line
// with their whole text as the tooltip (Char_TextHeight), a lower bar and
// the preview without its title line. Measured with the automation verb
// "controls" (--screen simulates the start window with the frame of the
// simulated dpi, Rs_PlaceWindow): at 1366 x 768 with 150 % and 1920 x 1080
// with 100 %, every step of every route is whole without scrolling, Advanced
// open on each of its cards as well.
#define CHAR_FULL_W      920    // the layout as always from here on
#define CHAR_FULL_H      560
#define CHAR_PAGE_MIN_W  800    // below: the view scrolls (the compact layout needs about this)
#define CHAR_PAGE_MIN_H  400
#define CHAR_COMPACT_STRIP_H 28
#define CHAR_COMPACT_BAR_H   34 // Back, Next, the progress and Cancel
#define CHAR_COMPACT_VIEW_MIN_W 200
#define CHAR_LEFT_MIN_W  540    // the column of the tabs, at least
#define CHAR_LEFT_MAX_W  760    // and at most: the rest goes to the preview
#define CHAR_VIEW_MIN_W  300    // the column of the preview, at least
#define CHAR_STRIP_H     36     // the heads of the tabs
#define CHAR_BAR_H       50
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

    // The whole texts first, the short ones where those do not fit.
    for (g_char.headShort = 0; g_char.headShort < 2; g_char.headShort++) {
        total = 0;
        for (t = 0; t < CHAR_TABS; t++) {
            widths[t] = Rs_Px(12 + 20 + 8 + 16) + Char_TextWidth(g_char.tabHead[t], Char_HeadText(t));
            total += widths[t] + (t ? Rs_Px(4) : 0);
        }
        if (total <= room)
            break;
    }
    if (g_char.headShort > 1)
        g_char.headShort = 1;
    for (t = 0; t < CHAR_TABS; t++)
        InvalidateRect(g_char.tabHead[t], NULL, FALSE);
    // Too wide: the widest heads give way first (down to the next widest),
    // so that a short name is not cut while a long one has room to spare.
    while (total > room) {
        int widest = 0, next = 0, count = 0, cut;
        for (t = 0; t < CHAR_TABS; t++)
            if (widths[t] > widest)
                widest = widths[t];
        for (t = 0; t < CHAR_TABS; t++) {
            if (widths[t] == widest)
                count++;
            else if (widths[t] > next)
                next = widths[t];
        }
        cut = (total - room + count - 1) / count;
        if (next > 0 && widest - cut < next)
            cut = widest - next;
        if (cut <= 0 || widest - cut < Rs_Px(48))
            cut = widest - Rs_Px(48);
        if (cut <= 0)
            break;
        for (t = 0; t < CHAR_TABS; t++)
            if (widths[t] == widest) {
                widths[t] -= cut;
                total -= cut;
            }
    }
    for (t = 0; t < CHAR_TABS; t++) {
        MoveWindow(g_char.tabHead[t], x, top, widths[t], h + 1, TRUE);
        x += widths[t] + Rs_Px(4);
    }
    return top + h;
}

// Step 1 Model: the model field and Browse; below it, while Advanced is
// closed, the line of what was read (and for an OBJ what it brought along and
// the textures folder), the size and Show kart wheels (Vanilla), Reduce to
// fit over the limit, and the button that opens Advanced. A CHRG route has
// only the model and the line of its check (Char_RouteHides). Returns the
// bottom of what it placed.
// A line on the step Model with "Change" right after its text (at the right
// of the field when the text wraps); returns the y below it.
static int Char_LayStepLine(HWND label, HWND change, int x, int y, int fieldW)
{
    int bh = Rs_Px(g_char.compact ? 24 : 28), cw, lw, tw, off, rowH, noteH;
    wchar_t *text = Rs_GetText(change);
    cw = Char_TextWidth(change, text) + Rs_Px(28);
    lw = fieldW - cw - Rs_Px(8);
    Rs_Free(text);
    text = Rs_GetText(label);
    tw = Char_TextWidth(label, text) + Rs_Px(4);
    Rs_Free(text);
    if (tw < lw)
        lw = tw;
    if (lw < Rs_Px(40))
        lw = Rs_Px(40);
    noteH = Char_NoteHeight(label, lw);
    off = (bh - Rs_Px(18)) / 2;
    MoveWindow(label, x, y + off, lw, noteH, TRUE);
    MoveWindow(change, x + lw + Rs_Px(8), y, cw, bh, TRUE);
    rowH = off + noteH > bh ? off + noteH : bh;
    return y + rowH + Rs_Px(g_char.compact ? 2 : 6);
}

static int Char_LayModelAt(const struct CharLay *k, const RECT *in, int filesH)
{
    HWND options[2];
    const int chrg = Char_Chrg();
    int x = in->left + k->labelW + Rs_Px(8), fieldW = in->right - x, y = in->top, infoH, noteH, i, count;

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
    // THE CREATOR KIT: every route without a model.
    if (Char_KitHint())
        y = Char_LayStepLine(g_char.kitStep, g_char.kitLink, x, y, fieldW);
    // A CHRG route, a model with COLOR_0: "Vertex color blending" and its line.
    if (chrg && g_char.vcolMode >= 0) {
        int bw = Char_CheckWidth(g_char.vblend);
        MoveWindow(g_char.vblend, x, y, bw < fieldW ? bw : fieldW, Rs_Px(24), TRUE);
        y += Rs_Px(26);
        noteH = Char_NoteHeight(g_char.vblendNote, fieldW);
        MoveWindow(g_char.vblendNote, x, y, fieldW, noteH, TRUE);
        y += noteH + Rs_Px(g_char.compact ? 2 : 6);
    }
    // A CHRG route: the line of the PNG conversion and the line of the
    // upscale (in the order of the build), each with "Change" right after its
    // text (at the right of the field when the text wraps), only while it
    // says something (Char_PngControls, Char_UpscaleControls).
    // (A finished character as the model: none of them - Char_Wanted.)
    if (chrg && !Char_ModelIsCharacter() && GetWindowTextLengthW(g_char.classicStep) > 0)
        y = Char_LayStepLine(g_char.classicStep, g_char.classicChange, x, y, fieldW);
    if (chrg && !Char_ModelIsCharacter() && GetWindowTextLengthW(g_char.pngStep) > 0)
        y = Char_LayStepLine(g_char.pngStep, g_char.pngChange, x, y, fieldW);
    if (chrg && !Char_ModelIsCharacter() && GetWindowTextLengthW(g_char.upscaleStep) > 0)
        y = Char_LayStepLine(g_char.upscaleStep, g_char.upscaleChange, x, y, fieldW);
    if ((chrg || Char_KartBoxWanted()) && !Char_ModelIsCharacter() && GetWindowTextLengthW(g_char.gameKartStep) > 0)
        y = Char_LayStepLine(g_char.gameKartStep, g_char.gameKartChange, x, y, fieldW);
    // A model with JPEG textures: "Convert to PNG", the fix of its error.
    if (chrg && g_char.pngFixOn) {
        int bw = Char_TextWidth(g_char.pngFix, L"Convert to PNG") + Rs_Px(32);
        MoveWindow(g_char.pngFix, x, y + Rs_Px(2), bw < fieldW ? bw : fieldW, Rs_Px(30), TRUE);
        y += Rs_Px(g_char.compact ? 34 : 38);
    }
    if (!chrg) {
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
        // (Char_Wanted hides the line).
        Rs_SetTip(g_char.size, g_char.compact ? CHAR_SIZE_HINT_TEXT : NULL);
        if (g_char.compact) {
            y += Rs_Px(6);
        } else {
            noteH = Char_NoteHeight(g_char.sizeHint, fieldW);
            MoveWindow(g_char.sizeHint, x, y, fieldW, noteH, TRUE);
            y += Rs_Px(8) + noteH;
        }
        // Show kart wheels, and in the user mode the classic fallback (ticked,
        // greyed out, "Coming soon" right of it); the choices of rldpack's work
        // on the model are the card Options of Advanced.
        count = 0;
        options[count++] = g_char.wheels;
        if (Rs_NativeForUsers())
            options[count++] = g_char.fallback;
        MoveWindow(g_char.optionsLabel, in->left, y + Rs_Px(2), k->labelW, Rs_Px(20), TRUE);
        {
            int bx = g_char.compact ? in->left : x, bwAll = g_char.compact ? in->right - in->left : fieldW, cx = bx;
            for (i = 0; i < count; i++) {
                int bw = Char_CheckWidth(options[i]);
                int extra = options[i] == g_char.fallback ? Rs_Px(8) + Char_TextWidth(g_char.fallbackHint, L"Coming soon") + Rs_Px(4) : 0;
                if (bw > bwAll - extra)
                    bw = bwAll - extra;
                if (cx > bx && cx + bw + extra > bx + bwAll) {
                    cx = bx;
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
            y += Rs_Px(4) + noteH;
        }
        // Over the limit: the button that ticks Reduce to fit and checks again.
        if (g_char.reduceFitOn) {
            int bw = Char_TextWidth(g_char.reduceFit, L"Reduce to fit") + Rs_Px(32);
            MoveWindow(g_char.reduceFit, x, y + Rs_Px(6), bw < fieldW ? bw : fieldW, Rs_Px(30), TRUE);
            y += Rs_Px(36);
        }
    }
    // The button that opens Advanced (Char_LayAdvSwitch moves it into the
    // title line of the section while it is open).
    if (!g_char.advOpen) {
        int bw = Char_TextWidth(g_char.advanced, L"Advanced options") + Rs_Px(32);
        MoveWindow(g_char.advanced, in->left, y + Rs_Px(g_char.compact ? 4 : 8), bw, Rs_Px(28), TRUE);
        y += Rs_Px(g_char.compact ? 32 : 36);
    }
    return y;
}

// Step 1 with the list of what an OBJ brought along at least
// g_char.filesLeast rows tall, taller as far as the room down to in->bottom
// allows, never taller than its rows. Laid out with in->bottom above in->top
// it says how much it needs at least.
static int Char_LayModel(const struct CharLay *k, const RECT *in)
{
    int least, most, end;

    if (!g_char.filesOn || g_char.filesLeast == 0 || Char_Chrg())
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

// Advanced, the card Options. Vanilla: the four choices of rldpack's work on
// the model (a new row where the next does not fit) and what they did; a
// CHRG route: "Upscale small textures" with its note. Returns its bottom.
static int Char_LayOptions(const RECT *in)
{
    static const int ids[4] = { CHAR_ID_REPAIR, CHAR_ID_OPEN_PARTS, CHAR_ID_REMESH, CHAR_ID_REDUCE };
    int y = in->top, x = in->left, w = in->right - in->left, i, cx, qualityH, noteH;

    if (Char_Chrg() && Char_ModelIsCharacter()) {
        noteH = Char_TextHeight(g_char.charNote, w, CHAR_NOTE_LINES);
        MoveWindow(g_char.charNote, x, y, w, noteH, TRUE);
        return y + noteH;
    }
    if (Char_Chrg()) {
        int bw = Char_CheckWidth(g_char.upscale);
        MoveWindow(g_char.upscale, x, y, bw < w ? bw : w, Rs_Px(24), TRUE);
        if (GetWindowTextLengthW(g_char.upscaleNote) > 0) {
            // Its line (the suggestion, or the author's choice against it),
            // spaced as the line of "Vertex color blending".
            y += Rs_Px(26);
            noteH = Char_NoteHeight(g_char.upscaleNote, w);
            MoveWindow(g_char.upscaleNote, x, y, w, noteH, TRUE);
            y += noteH + Rs_Px(6);
        } else
            y += Rs_Px(28);
        noteH = Char_TextHeight(g_char.upscaleHelp, w, CHAR_NOTE_LINES);
        MoveWindow(g_char.upscaleHelp, x, y, w, noteH, TRUE);
        y += noteH;
        // THE KART: the box with its line and help, not for a model with a
        // kart part of its own.
        if (Char_KartBoxWanted()) {
            bw = Char_CheckWidth(g_char.gameKart);
            y += Rs_Px(14);
            MoveWindow(g_char.gameKart, x, y, bw < w ? bw : w, Rs_Px(24), TRUE);
            if (GetWindowTextLengthW(g_char.gameKartNote) > 0) {
                y += Rs_Px(26);
                noteH = Char_NoteHeight(g_char.gameKartNote, w);
                MoveWindow(g_char.gameKartNote, x, y, w, noteH, TRUE);
                y += noteH + Rs_Px(6);
            } else
                y += Rs_Px(28);
            noteH = Char_TextHeight(g_char.gameKartHelp, w, CHAR_NOTE_LINES);
            MoveWindow(g_char.gameKartHelp, x, y, w, noteH, TRUE);
            y += noteH;
        }
        // "Convert JPEG textures to PNG" and its help.
        bw = Char_CheckWidth(g_char.png);
        y += Rs_Px(14);
        MoveWindow(g_char.png, x, y, bw < w ? bw : w, Rs_Px(24), TRUE);
        y += Rs_Px(28);
        noteH = Char_TextHeight(g_char.pngHelp, w, CHAR_NOTE_LINES);
        MoveWindow(g_char.pngHelp, x, y, w, noteH, TRUE);
        y += noteH;
        // A model with COLOR_0: the three vertex colour modes.
        if (g_char.vcolMode >= 0) {
            int lw = Char_TextWidth(g_char.vmodeLabel, L"Vertex colors") + Rs_Px(12);
            int cw = Char_ComboTextWidth(g_char.vmode) + Rs_Px(28);
            if (lw + cw > w)
                cw = w - lw;
            y += Rs_Px(14);
            MoveWindow(g_char.vmodeLabel, x, y + Rs_Px(4), lw, Rs_Px(20), TRUE);
            MoveWindow(g_char.vmode, x + lw, y, cw, Rs_Px(300), TRUE);
            y += Rs_Px(32);
            noteH = Char_TextHeight(g_char.vmodeHelp, w, CHAR_NOTE_LINES);
            MoveWindow(g_char.vmodeHelp, x, y, w, noteH, TRUE);
            y += noteH;
        }
        // THE CLASSIC MODEL: the list, and the character's field with Browse
        // while it comes from one.
        {
            int lw = Char_TextWidth(g_char.classicLabel, L"Classic model") + Rs_Px(12);
            int cw = Char_ComboTextWidth(g_char.classicMode) + Rs_Px(28), bw2 = Rs_Px(100);
            if (lw + cw > w)
                cw = w - lw;
            y += Rs_Px(14);
            MoveWindow(g_char.classicLabel, x, y + Rs_Px(4), lw, Rs_Px(20), TRUE);
            MoveWindow(g_char.classicMode, x + lw, y, cw, Rs_Px(300), TRUE);
            y += Rs_Px(32);
            if (g_char.classicFrom) {
                MoveWindow(g_char.classicFile, x, y + Rs_Px(2), w - bw2 - Rs_Px(8), Rs_Px(28), TRUE);
                MoveWindow(g_char.classicBrowse, x + w - bw2, y, bw2, Rs_Px(32), TRUE);
                y += Rs_Px(36);
            }
            noteH = Char_TextHeight(g_char.classicHelp, w, CHAR_NOTE_LINES);
            MoveWindow(g_char.classicHelp, x, y, w, noteH, TRUE);
            y += noteH;
        }
        return y;
    }
    cx = x;
    for (i = 0; i < 4; i++) {
        HWND box = GetDlgItem(GetParent(g_char.repair), ids[i]);
        int bw = Char_CheckWidth(box);
        if (bw > w)
            bw = w;
        if (cx > x && cx + bw > x + w) {
            cx = x;
            y += Rs_Px(26);
        }
        MoveWindow(box, cx, y, bw, Rs_Px(24), TRUE);
        cx += bw + Rs_Px(12);
    }
    y += Rs_Px(24);
    // What the repair, the open parts and the remesh did (Char_ApplyQuality).
    MoveWindow(g_char.quality, x, y + Rs_Px(4), w, Rs_Px(18), FALSE);
    qualityH = Char_QualityHeight(w);
    g_char.qualityOn = qualityH > 0;
    if (qualityH > 0) {
        MoveWindow(g_char.quality, x, y + Rs_Px(4), w, qualityH, TRUE);
        y += Rs_Px(4) + qualityH;
    }
    // THE KART of Vanilla (K6): the box with its line and help, as on a CHRG
    // route, for a .glb model that may get the game's kart.
    if (Char_KartBoxWanted()) {
        int bw = Char_CheckWidth(g_char.gameKart);
        y += Rs_Px(14);
        MoveWindow(g_char.gameKart, x, y, bw < w ? bw : w, Rs_Px(24), TRUE);
        if (GetWindowTextLengthW(g_char.gameKartNote) > 0) {
            y += Rs_Px(26);
            noteH = Char_NoteHeight(g_char.gameKartNote, w);
            MoveWindow(g_char.gameKartNote, x, y, w, noteH, TRUE);
            y += noteH + Rs_Px(6);
        } else
            y += Rs_Px(28);
        noteH = Char_TextHeight(g_char.gameKartHelp, w, CHAR_NOTE_LINES);
        MoveWindow(g_char.gameKartHelp, x, y, w, noteH, TRUE);
        y += noteH;
    }
    return y;
}

// Advanced of a CHRG route, the card Animations (THE CLIPS): the list "Clip"
// with Play | Pause and a frame at a time, the time below, the table of the
// 12 clips, the note. Returns its bottom.
static int Char_LayClips(const RECT *in)
{
    int y = in->top, x = in->left, w = in->right - in->left, noteH, bx, i, bw[3], tableH, c0, c2;
    HWND buttons[3];
    const int lw = Char_TextWidth(g_char.clipLabel, L"Clip") + Rs_Px(12);
    buttons[0] = g_char.clipPlay;
    buttons[1] = g_char.clipPrev;
    buttons[2] = g_char.clipNext;
    bw[0] = Char_TextWidth(g_char.clipPlay, L"Pause") + Rs_Px(28);
    bw[1] = Char_TextWidth(g_char.clipPrev, L"< Frame") + Rs_Px(24);
    bw[2] = Char_TextWidth(g_char.clipNext, L"Frame >") + Rs_Px(24);
    MoveWindow(g_char.clipLabel, x, y + Rs_Px(4), lw, Rs_Px(20), TRUE);
    SendMessageW(g_char.clipCombo, CB_SETDROPPEDWIDTH, (WPARAM)(Char_ComboTextWidth(g_char.clipCombo) + Rs_Px(16)), 0);
    {
        int cw = w - lw - bw[0] - bw[1] - bw[2] - Rs_Px(24);
        if (cw < Rs_Px(120)) {
            // Narrow: the buttons on a row of their own.
            MoveWindow(g_char.clipCombo, x + lw, y, w - lw, Rs_Px(300), TRUE);
            y += Rs_Px(34);
            bx = x + lw;
        } else {
            MoveWindow(g_char.clipCombo, x + lw, y, cw, Rs_Px(300), TRUE);
            bx = x + lw + cw + Rs_Px(8);
        }
    }
    for (i = 0; i < 3; i++) {
        MoveWindow(buttons[i], bx, y, bw[i], Rs_Px(30), TRUE);
        bx += bw[i] + Rs_Px(8);
    }
    y += Rs_Px(34);
    MoveWindow(g_char.clipTime, x + lw, y, w - lw, Rs_Px(18), TRUE);
    y += Rs_Px(24);
    // The table: the header and every clip row, measured (compact: as many as
    // there is room for).
    tableH = g_char.compact ? Rs_Px(160) : Char_ListHeight(g_char.clipTable, RS_HIRES_CLIPS);
    MoveWindow(g_char.clipTable, x, y, w, tableH, TRUE);
    c0 = Rs_Px(100);
    c2 = Rs_Px(80);
    ListView_SetColumnWidth(g_char.clipTable, 0, c0);
    ListView_SetColumnWidth(g_char.clipTable, 1, w - c0 - c2 - Rs_Metric(g_char.clipTable, SM_CXVSCROLL) - Rs_Px(4));
    ListView_SetColumnWidth(g_char.clipTable, 2, c2);
    y += tableH + Rs_Px(8);
    noteH = Char_TextHeight(g_char.clipNote, w, CHAR_NOTE_LINES);
    MoveWindow(g_char.clipNote, x, y, w, noteH, TRUE);
    return y + noteH;
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

// Advanced of the step Model, the card Import: a note, then the choices (those of an OBJ
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
    return y + Rs_Px(g_char.compact ? 8 : 16);
}

// Advanced of the step Model, below the model field: in its title line the
// switch of the cards the route has (Char_ExtrasShown; no switch for one
// card) and at the right the button that closes it; the card chosen below
// (Char_LayImport, Char_LayOptions, the cards of rs_wheels.c and rs_anim.c).
static void Char_LayAdvSwitch(const RECT *card)
{
    int x = card->left + Rs_Px(10), i, bw;
    for (i = 0; i < CHAR_EXTRAS_COUNT; i++) {
        const int c = g_charExtrasOrder[i];
        if (!Char_ExtrasShown(c))
            continue;
        bw = Char_TextWidth(g_char.extrasSwitch[c], g_charExtrasTexts[c]) + Rs_Px(20);
        MoveWindow(g_char.extrasSwitch[c], x, card->top + Rs_Px(8), bw, Rs_Px(36), TRUE);
        x += bw + Rs_Px(4);
    }
    if (g_char.advOpen) {
        bw = Char_TextWidth(g_char.advanced, L"Hide advanced") + Rs_Px(32);
        MoveWindow(g_char.advanced, card->right - Rs_Px(18) - bw, card->top + Rs_Px(12), bw, Rs_Px(28), TRUE);
    }
}

// The card "Preview" from top to bottom: the 3D view as tall as the card
// allows. The pose choice is in the title line of the card; a CHRG route has
// the switch Low-Poly/Hi-Res | Classic fallback there (the pose then only for the
// classic fallback).
static void Char_LayPreview(HWND page, int left, int right, int top, int bottom)
{
    RECT card, in;
    int width, viewTop, viewBottom, noteH, i, x, poseW, poseRow;
    const int chrg = Char_Chrg();

    card.left = left;
    card.top = top;
    card.right = right;
    card.bottom = bottom;
    // Compact: no title line - the switch and the pose choice on a row of
    // their own, the note on one line below the view. A CHRG route: where
    // the pose choice (classic fallback only) does not fit beside the switch,
    // a row of its own below it.
    if (g_char.compact) {
        in = Rs_CardInner(&card, 0);
        width = in.right - in.left;
        poseW = Rs_Px(150) < width ? Rs_Px(150) : width;
        for (i = 0, x = in.left; i < 2; i++) {
            int bw = Char_TextWidth(g_char.previewSwitch[i], Char_PreviewText(i)) + Rs_Px(16);
            MoveWindow(g_char.previewSwitch[i], x, in.top - Rs_Px(4), bw, Rs_Px(32), TRUE);
            x += bw + Rs_Px(2);
        }
        poseRow = chrg && g_char.previewClassic && in.right - poseW < x + Rs_Px(8);
        MoveWindow(g_char.pose, in.right - poseW, in.top + (poseRow ? Rs_Px(32) : 0), poseW, Rs_Px(200), TRUE);
        viewTop = in.top + Rs_Px(poseRow ? 64 : 32);
        Char_ViewNoteFit(width);
        noteH = Char_NoteHeight(g_char.viewNote, width);
        viewBottom = in.bottom - Rs_Px(4) - noteH;
        if (viewBottom < viewTop + Rs_Px(CHAR_VIEW_MIN_H))
            viewBottom = viewTop + Rs_Px(CHAR_VIEW_MIN_H);
        MoveWindow(g_char.view, in.left, viewTop, width, viewBottom - viewTop, TRUE);
        MoveWindow(g_char.viewNote, in.left, viewBottom + Rs_Px(4), width, noteH, TRUE);
        if (card.bottom < viewBottom + Rs_Px(4) + noteH + Rs_Px(16))
            card.bottom = viewBottom + Rs_Px(4) + noteH + Rs_Px(16);
        Rs_CardAdd(page, &card, NULL);
        return;
    }
    in = Rs_CardInner(&card, 1);
    width = in.right - in.left;
    for (i = 0, x = card.left + Rs_Px(10); i < 2; i++) {
        int bw = Char_TextWidth(g_char.previewSwitch[i], Char_PreviewText(i)) + Rs_Px(20);
        MoveWindow(g_char.previewSwitch[i], x, card.top + Rs_Px(8), bw, Rs_Px(36), TRUE);
        x += bw + Rs_Px(4);
    }
    poseW = Rs_Px(168);
    poseRow = chrg && g_char.previewClassic && in.right - poseW < x + Rs_Px(8);
    MoveWindow(g_char.pose, in.right - poseW, poseRow ? in.top : card.top + Rs_Px(10), poseW, Rs_Px(200), TRUE);
    viewTop = in.top + (poseRow ? Rs_Px(34) : 0);
    Char_ViewNoteFit(width);
    noteH = Char_NoteHeight(g_char.viewNote, width);
    viewBottom = in.bottom - Rs_Px(6) - noteH;
    if (viewBottom < viewTop + Rs_Px(CHAR_VIEW_MIN_H))
        viewBottom = viewTop + Rs_Px(CHAR_VIEW_MIN_H);
    MoveWindow(g_char.view, in.left, viewTop, width, viewBottom - viewTop, TRUE);
    MoveWindow(g_char.viewNote, in.left, viewBottom + Rs_Px(6), width, noteH, TRUE);
    if (card.bottom < viewBottom + Rs_Px(6) + noteH + Rs_Px(16))
        card.bottom = viewBottom + Rs_Px(6) + noteH + Rs_Px(16);
    Rs_CardAdd(page, &card, chrg ? NULL : L"Preview");
}

// Step 5 Build: where the character goes (the game's characters folder while
// the field is empty, the line below it says so), what the last check or
// build says (the headline), then the one main action - Build; after a build
// of the fields as they are Test in game, with Build again beside it -, Show
// in folder after a build and "Show details" at the right of that row, what
// Test in game did below. Returns the bottom of what it placed.
static int Char_LayBuild(const struct CharLay *k, const RECT *in)
{
    int x = in->left + k->labelW + Rs_Px(8), fieldW = in->right - x, y = in->top, noteH, bx, bw, rawW, h;
    wchar_t *text;

    Char_PlaceField(g_char.outLabel, g_char.out, in->left, k->labelW, y + Rs_Px(2), fieldW - k->browseW - Rs_Px(8));
    MoveWindow(g_char.outBrowse, in->right - k->browseW, y, k->browseW, Rs_Px(32), TRUE);
    y += Rs_Px(36);
    noteH = Char_TextHeight(g_char.outNote, fieldW, 2);
    MoveWindow(g_char.outNote, x, y, fieldW, noteH, TRUE);
    y += noteH + Rs_Px(g_char.compact ? 6 : 14);
    MoveWindow(g_char.headline, in->left, y, in->right - in->left, Rs_Px(22), TRUE);
    y += Rs_Px(g_char.compact ? 26 : 30);
    h = Rs_Px(g_char.compact ? 30 : 34);
    bx = in->left;
    if (g_char.builtFresh) {
        bw = Char_TextWidth(g_char.test, L"Test in game") + Rs_Px(48);
        MoveWindow(g_char.test, bx, y, bw, h, TRUE);
        bx += bw + Rs_Px(8);
        bw = Char_TextWidth(g_char.rebuild, L"Build again") + Rs_Px(32);
        MoveWindow(g_char.rebuild, bx, y, bw, h, TRUE);
        bx += bw + Rs_Px(8);
    } else {
        bw = Char_TextWidth(g_char.build, L"Build") + Rs_Px(80);
        MoveWindow(g_char.build, bx, y, bw, h, TRUE);
        bx += bw + Rs_Px(8);
    }
    text = Rs_GetText(g_char.show);
    bw = Char_TextWidth(g_char.show, text) + Rs_Px(32);
    Rs_Free(text);
    MoveWindow(g_char.show, bx, y, bw, h, TRUE);
    rawW = Char_TextWidth(g_char.rawToggle, L"Show details");
    bw = Char_TextWidth(g_char.rawToggle, L"Hide details");
    rawW = (bw > rawW ? bw : rawW) + Rs_Px(32);
    MoveWindow(g_char.rawToggle, in->right - rawW, y, rawW, h, TRUE);
    y += h + Rs_Px(6);
    text = Rs_GetText(g_char.testNote);
    if (text[0]) {
        noteH = Char_TextHeight(g_char.testNote, in->right - in->left, 2);
        MoveWindow(g_char.testNote, in->left, y, in->right - in->left, noteH, TRUE);
        y += noteH + Rs_Px(4);
    }
    Rs_Free(text);
    return y;
}

// The bar at the bottom: Back and Next at the right; while rldpack runs the
// bar of its progress and Cancel at the left. Returns its bottom.
static int Char_LayBar(HWND page, int left, int right, int top, int bottom)
{
    RECT card;
    int y, h = Rs_Px(g_char.compact ? 28 : 30), navW = Rs_Px(96), x;

    card.left = left;
    card.top = top;
    card.right = right;
    card.bottom = bottom;
    y = (top + bottom - h) / 2;
    x = right - Rs_Px(18) - navW;
    MoveWindow(g_char.next, x, y, navW, h, TRUE);
    MoveWindow(g_char.back, x - Rs_Px(8) - navW, y, navW, h, TRUE);
    if (g_char.progressOn) {
        int cancelW = Char_TextWidth(g_char.cancel, L"Cancel") + Rs_Px(32);
        int progW = Rs_Px(g_char.compact ? 200 : 260);
        MoveWindow(g_char.progress, left + Rs_Px(18), y + (h - Rs_Px(18)) / 2, progW, Rs_Px(18), TRUE);
        MoveWindow(g_char.cancel, left + Rs_Px(18) + progW + Rs_Px(8), y, cancelW, h, TRUE);
    }
    Rs_CardAdd(page, &card, NULL);
    return bottom;
}

// Step 0: "What do you want to build?" and the three cards in a row, as large
// as the page allows (at most 5:4 tall), the picture of each made for its
// size (Char_RouteImages).
static void Char_RouteImages(int w, int h);
static void Char_LayChoose(HWND page, int left, int right, int top, int bottom)
{
    const int gap = Rs_Px(g_char.compact ? 12 : 20), askH = Rs_Px(g_char.compact ? 28 : 36), pad = Rs_Px(12);
    int w, h, i, rowH;

    (void)page;
    {
        // "Creator Kit" at the top right (K10): as tall as the heading row,
        // which grows to the button when the button needs more; the question
        // in the middle of the row.
        int kw, kh, th = Char_FontLine(RS_FONT_SECTION) + Rs_Px(4);
        Char_KitButtonSize(&kw, &kh);
        rowH = askH > kh ? askH : kh;
        MoveWindow(g_char.kitOpen, right - kw, top, kw, rowH, TRUE);
        MoveWindow(g_char.routeAsk, left, top + (rowH - th) / 2, right - kw - Rs_Px(12) - left, th, TRUE);
    }
    top += rowH + Rs_Px(g_char.compact ? 4 : 12);
    w = (right - left - 2 * gap) / 3;
    h = bottom - top;
    if (h > w)
        h = w;
    if (h < Rs_Px(180))
        h = Rs_Px(180);
    for (i = 0; i < RS_ROUTES; i++)
        MoveWindow(g_char.routeCard[i], left + i * (w + gap), top, w, h, TRUE);
    Char_RouteImages(w - 2 * pad, h * 55 / 100);
}

// What is shown: step 0 or the steps of the route; on a step its controls
// (and on Model those of Basic or of the card of Advanced shown, on In-game
// look those of its card), and nothing a route does not need
// (Char_RouteHides). The preview and the bar stay on every step.
static int Char_Wanted(HWND c)
{
    int id = GetDlgCtrlID(c), card, tab;
    wchar_t *text;
    int on;

    if ((id >= CHAR_ID_ROUTE && id < CHAR_ID_ROUTE + RS_ROUTES) || id == CHAR_ID_ROUTE_ASK || id == CHAR_ID_KIT_OPEN)
        return g_char.choosing;
    if (g_char.choosing || c == g_char.check || Char_RouteHides(c))
        return 0;
    tab = Char_TabOfId(id, &card);
    if (tab < 0) {
        if (c == g_char.pose)
            return !Char_Chrg() || g_char.previewClassic;
        if (c == g_char.next)
            return g_char.tab < CHAR_TABS - 1;
        if (c == g_char.cancel || c == g_char.progress)
            return g_char.progressOn;
        if (c == g_char.msgs)
            return g_char.listCount > 0 && !(g_char.tab == CHAR_TAB_BUILD && g_char.showRaw);
        return 1;
    }
    if (tab != g_char.tab || Char_UserHidden(c) || CharWheels_Hidden(c))
        return 0;
    // No make-char (THE CLASSIC MODEL from a character, THE FINISHED
    // CHARACTER): these steps show only their messages - what the file holds.
    if (Char_NoMake() && (tab == CHAR_TAB_DRIVER || tab == CHAR_TAB_LOOK || tab == CHAR_TAB_VOICES))
        return 0;
    if (tab == CHAR_TAB_LOOK)
        return card < 0 || card == g_char.lookCard;
    if (tab == CHAR_TAB_MODEL) {
        if (id >= CHAR_ID_EXTRAS && id < CHAR_ID_EXTRAS + CHAR_EXTRAS_COUNT)
            return g_char.advOpen && Char_ExtrasShown(id - CHAR_ID_EXTRAS) && Char_ExtrasCount() > 1;
        if (c == g_char.advanced)
            return Char_ExtrasCount() > 0;
        if (card == CHAR_CARD_BASIC && g_char.advOpen)
            return 0;
        if (card >= 0 && (!g_char.advOpen || card != g_char.extrasCard))
            return 0;
        // A finished character as the model: nothing to build, so the
        // choices of the build have no effect - the card Options says so
        // instead, and their lines on the step go too.
        if (c == g_char.charNote)
            return Char_ModelIsCharacter();
        if (c == g_char.kitStep || c == g_char.kitLink)
            return Char_KitHint();
        if (Char_ModelIsCharacter() &&
            (c == g_char.upscale || c == g_char.upscaleHelp || c == g_char.upscaleNote || c == g_char.upscaleStep ||
             c == g_char.upscaleChange || (id >= CHAR_ID_GAMEKART && id <= CHAR_ID_GAMEKART_CHANGE) || (id >= CHAR_ID_PNG && id <= CHAR_ID_PNG_FIX) ||
             (id >= CHAR_ID_CLASSIC_LABEL && id <= CHAR_ID_CLASSIC_CHANGE)))
            return 0;
        if (c == g_char.reduceFit)
            return g_char.reduceFitOn;
        if (c == g_char.quality)
            return g_char.qualityOn;
        if (c == g_char.sizeHint || c == g_char.optionsLabel)
            return !g_char.compact;
        if (c == g_char.modelImport)
            return g_char.importOn;
        if (c == g_char.modelFiles)
            return g_char.filesOn && g_char.filesLeast > 0;
        if (c == g_char.vcolorsLabel || c == g_char.vcolors || c == g_char.vcolorsHelp)
            return g_char.objOn;
        if (id >= CHAR_ID_VBLEND && id <= CHAR_ID_VMODE_HELP)
            return g_char.vcolMode >= 0;
        if ((id >= CHAR_ID_GAMEKART && id <= CHAR_ID_GAMEKART_CHANGE) && !Char_KartBoxWanted())
            return 0;
        if (c == g_char.gameKartNote || c == g_char.gameKartStep)
            return GetWindowTextLengthW(c) > 0;
        if (c == g_char.gameKartChange)
            return GetWindowTextLengthW(g_char.gameKartStep) > 0;
        if (c == g_char.upscaleNote || c == g_char.upscaleStep)
            return GetWindowTextLengthW(c) > 0;
        if (c == g_char.upscaleChange)
            return GetWindowTextLengthW(g_char.upscaleStep) > 0;
        if (c == g_char.pngStep)
            return GetWindowTextLengthW(c) > 0;
        if (c == g_char.pngChange)
            return GetWindowTextLengthW(g_char.pngStep) > 0;
        if (c == g_char.pngFix)
            return g_char.pngFixOn;
        if (c == g_char.classicFile || c == g_char.classicBrowse)
            return g_char.classicFrom;
        if (c == g_char.classicStep || c == g_char.classicChange)
            return GetWindowTextLengthW(g_char.classicStep) > 0;
        if (c == g_char.texturesLabel || c == g_char.textures || c == g_char.texturesBrowse || c == g_char.texturesClear)
            return g_char.texturesOn;
        return 1;
    }
    if (tab == CHAR_TAB_BUILD) {
        if (c == g_char.build)
            return !g_char.builtFresh;
        if (c == g_char.test || c == g_char.rebuild)
            return g_char.builtFresh;
        if (c == g_char.show)
            return g_char.built[0] != 0;
        if (c == g_char.raw)
            return g_char.showRaw;
        if (c == g_char.testNote) {
            text = Rs_GetText(c);
            on = text[0] != 0;
            Rs_Free(text);
            return on;
        }
    }
    return 1;
}

static void Char_TabApply(HWND page)
{
    HWND c;
    for (c = GetWindow(page, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        int want = Char_Wanted(c);
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
    int vgap = Rs_Px(compact ? 6 : 12), avail, leftW, least, comboW, contentTop, advTop, extrasBottom[CHAR_EXTRAS_COUNT];
    int barH, barTop, upperBottom, need, t, i, stepEnd, viewMinW = Rs_Px(compact ? CHAR_COMPACT_VIEW_MIN_W : CHAR_VIEW_MIN_W);
    RECT card, in, adv, advIn, inStep;
    HWND column[20];

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

    // Step 0: the three cards, nothing else.
    if (g_char.choosing) {
        Char_LayChoose(page, left, right, top, bottom);
        Char_TabApply(page);
        return;
    }

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
    column[19] = g_char.outLabel;
    for (i = 0; i < 20; i++) {
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

    // The columns: the steps about 58 % (CHAR_LEFT_MIN_W..CHAR_LEFT_MAX_W, and
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
    // The route and "Change type" in the line of the heads, over the preview.
    {
        int ctW = Char_TextWidth(g_char.changeType, L"Change type") + Rs_Px(28), px = left + leftW + k.gap;
        int rowH = contentTop - top, bh = Rs_Px(compact ? 24 : 28);
        MoveWindow(g_char.changeType, right - ctW, top + (rowH - bh) / 2, ctW, bh, TRUE);
        MoveWindow(g_char.routeLabel, px, top + (rowH - Rs_Px(20)) / 2, right - ctW - Rs_Px(8) - px, Rs_Px(20), TRUE);
    }

    // The cards of Advanced lay themselves out below the model field (those
    // of rs_wheels.c and rs_anim.c report their cards, taken back here: the
    // page reports the card of the step shown below).
    card.left = left;
    card.top = contentTop;
    card.right = left + leftW;
    card.bottom = contentTop;
    in = Rs_CardInner(&card, 0);
    advTop = in.top + Rs_Px(compact ? 30 : 34);
    adv.left = left;
    adv.right = left + leftW;
    adv.top = advTop;
    adv.bottom = advTop;
    extrasBottom[CHAR_EXTRAS_WHEELS] = CharWheels_Layout(page, left, left + leftW, advTop, k.labelW, compact);
    extrasBottom[CHAR_EXTRAS_ANIM] = CharAnim_Layout(page, left, left + leftW, advTop, k.labelW, compact);
    Rs_CardClear(page);
    extrasBottom[CHAR_EXTRAS_IMPORT] = Char_LayImport(&k, left, left + leftW, advTop);
    advIn = Rs_CardInner(&adv, 1);
    extrasBottom[CHAR_EXTRAS_OPTIONS] = Char_LayOptions(&advIn) + Rs_Px(16);
    // A CHRG route: its own card Animations (THE CLIPS) in place of the shape keys'.
    if (Char_Chrg())
        extrasBottom[CHAR_EXTRAS_ANIM] = Char_LayClips(&advIn) + Rs_Px(16);
    Char_LayAdvSwitch(&adv);
    // Advanced sets no room for the other steps: only while it is open its
    // card may push the bar lower (below).
    need = 0;

    // Every step is laid out (the hidden ones too, so that their notes have
    // their width), and the tallest sets the room above the bar for all of
    // them - In-game look and Voices last: their pictures and their list take
    // the room there is (Model and Voices here with the least height of
    // their lists).
    barH = Rs_Px(compact ? CHAR_COMPACT_BAR_H : CHAR_BAR_H);
    g_char.filesLeast = compact ? 0 : CHAR_IMPORT_MIN_ROWS;
    for (t = 0; t < CHAR_TABS; t++) {
        int end;
        if (t == CHAR_TAB_LOOK) {
            // Its card In the race has nothing that gives way (the pictures
            // of Portrait do); below the title line of its switch.
            const RECT inLook = Rs_CardInner(&card, 1);
            end = Char_LayLookRace(&k, &inLook) + Rs_Px(16);
        } else if (t == CHAR_TAB_MODEL) {
            end = Char_LayModel(&k, &in) + Rs_Px(16);
            // The list of Model shows one row less where it would push the bar
            // down; the compact layout has no room for it (the line above it
            // says what was found).
            if (g_char.filesOn && g_char.filesLeast > 0 && bottom - end - vgap < barH + Rs_Px(CHAR_STEP_MSGS_MIN_H)) {
                g_char.filesLeast = 1;
                end = Char_LayModel(&k, &in) + Rs_Px(16);
                if (bottom - end - vgap < barH + Rs_Px(CHAR_STEP_MSGS_MIN_H)) {
                    g_char.filesLeast = 0;
                    end = Char_LayModel(&k, &in) + Rs_Px(16);
                }
            }
        } else if (t == CHAR_TAB_DRIVER) {
            end = Char_LayDriver(&k, &in) + Rs_Px(16);
        } else if (t == CHAR_TAB_VOICES) {
            end = Char_LayVoices(&k, &in) + Rs_Px(16);
        } else {
            end = Char_LayBuild(&k, &in) + Rs_Px(16);
        }
        if (end > need)
            need = end;
    }
    // (The compact layout leaves a narrower margin below the card of Advanced.)
    if (g_char.tab == CHAR_TAB_MODEL && g_char.advOpen && Char_ExtrasShown(g_char.extrasCard) &&
        extrasBottom[g_char.extrasCard] - Rs_Px(compact ? 8 : 0) > need)
        need = extrasBottom[g_char.extrasCard] - Rs_Px(compact ? 8 : 0);
    barTop = bottom - barH;
    upperBottom = barTop - vgap;
    if (upperBottom < need) {
        upperBottom = need;
        barTop = upperBottom + vgap;
    }

    // The step shown with the room there is: its lists and pictures take it,
    // its messages get at least CHAR_STEP_MSGS_MIN_H below its fields.
    card.bottom = upperBottom;
    in = Rs_CardInner(&card, 0);
    inStep = in;
    if (g_char.listCount > 0 || (g_char.tab == CHAR_TAB_BUILD && g_char.showRaw))
        inStep.bottom -= Rs_Px(CHAR_STEP_MSGS_MIN_H + 8);
    stepEnd = Char_LayModel(&k, &inStep);
    {
        // The tab In-game look: its switch in the title line of the card.
        RECT inLook = Rs_CardInner(&card, 1);
        int race, portrait;
        inLook.bottom = inStep.bottom;
        race = Char_LayLookRace(&k, &inLook);
        portrait = Char_LayLookPortrait(&k, &inLook);
        Char_LayLookSwitch(&card);
        if (g_char.tab == CHAR_TAB_LOOK)
            stepEnd = g_char.lookCard == CHAR_LOOK_RACE ? race : portrait;
    }
    t = Char_LayVoices(&k, &inStep);
    if (g_char.tab == CHAR_TAB_VOICES)
        stepEnd = t;
    t = Char_LayBuild(&k, &inStep);
    if (g_char.tab == CHAR_TAB_BUILD)
        stepEnd = t;
    if (g_char.tab == CHAR_TAB_DRIVER)
        stepEnd = Char_LayDriver(&k, &inStep);
    if (g_char.tab == CHAR_TAB_MODEL && g_char.advOpen)
        stepEnd = extrasBottom[g_char.extrasCard] - Rs_Px(16);
    // No make-char: Driver, In-game look and Voices show their messages alone (Char_Wanted).
    if (Char_NoMake() && (g_char.tab == CHAR_TAB_DRIVER || g_char.tab == CHAR_TAB_LOOK || g_char.tab == CHAR_TAB_VOICES))
        stepEnd = inStep.top;
    // The messages of the step (or the raw output on Build) below its fields,
    // down to the bottom of the card.
    if (g_char.listCount > 0 || (g_char.tab == CHAR_TAB_BUILD && g_char.showRaw)) {
        int y = stepEnd + Rs_Px(8), mh = upperBottom - Rs_Px(16) - y;
        if (mh < Rs_Px(CHAR_STEP_MSGS_MIN_H)) {
            mh = Rs_Px(CHAR_STEP_MSGS_MIN_H);
            upperBottom = y + mh + Rs_Px(16);
            barTop = upperBottom + vgap;
        }
        MoveWindow(g_char.msgs, in.left, y, in.right - in.left, mh, TRUE);
        MoveWindow(g_char.raw, in.left, y, in.right - in.left, mh, TRUE);
    } else if (stepEnd + Rs_Px(16) > upperBottom) {
        upperBottom = stepEnd + Rs_Px(16);
        barTop = upperBottom + vgap;
    }
    card.bottom = upperBottom;
    Rs_CardAdd(page, &card, NULL);

    Char_LayPreview(page, left + leftW + k.gap, right, contentTop, upperBottom);
    Char_LayBar(page, left, right, barTop, barTop + barH);
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
    if (id >= CHAR_ID_ROUTE && id < CHAR_ID_ROUTE + RS_ROUTES) {
        if (code == BN_CLICKED)
            Char_SetRoute(page, id - CHAR_ID_ROUTE);
        return 0;
    }
    if (id >= CHAR_ID_PREVIEW_SWITCH && id < CHAR_ID_PREVIEW_SWITCH + 2) {
        if (code == BN_CLICKED)
            Char_PreviewSwitch(page, id - CHAR_ID_PREVIEW_SWITCH);
        return 0;
    }
    switch (id) {
    case CHAR_ID_BACK:
    case CHAR_ID_NEXT:
        if (code == BN_CLICKED) {
            if (id == CHAR_ID_BACK && g_char.tab == CHAR_TAB_MODEL)
                Char_ShowChoice(page);
            else
                Char_SelectTab(page, g_char.tab + (id == CHAR_ID_NEXT ? 1 : -1), 0);
        }
        break;
    case CHAR_ID_CHANGE_TYPE:
        if (code == BN_CLICKED)
            Char_ShowChoice(page);
        break;
    case CHAR_ID_KIT_OPEN:
    case CHAR_ID_KIT_LINK:
        // Step 0: the kit with all three ways; the step Model of a route: with
        // that route's way chosen (K9) - the author can change it there.
        if (code == BN_CLICKED) {
            Rs_KitChooseWay(id == CHAR_ID_KIT_LINK && g_char.route >= 0 && g_char.route < RS_ROUTES ? g_char.route
                                                                                                    : RS_KIT_WAYS_ALL);
            Rs_ShowPage(RS_PAGE_KIT);
        }
        break;
    case CHAR_ID_ADVANCED:
        if (code == BN_CLICKED) {
            g_char.advOpen = !g_char.advOpen;
            Char_ExtrasFix();
            Rs_SetText(g_char.advanced, g_char.advOpen ? L"Hide advanced" : L"Advanced options");
            Char_MsgRefill();
            Char_Relayout(page);
            SetFocus(g_char.advanced);
        }
        break;
    case CHAR_ID_UPSCALE:
        // The author's choice, kept for this model; it changes only the
        // build (hires-upscale before packing), not the check.
        if (code == BN_CLICKED)
            Char_UpscaleSet(Char_IsChecked(g_char.upscale));
        break;
    case CHAR_ID_UPSCALE_CHANGE:
        if (code == BN_CLICKED)
            Char_UpscaleChange(page);
        break;
    case CHAR_ID_GAMEKART:
        // The author's choice, kept for this model; it changes only the
        // build (--game-kart template), not the check.
        if (code == BN_CLICKED)
            Char_GameKartSet(page, Char_IsChecked(g_char.gameKart));
        break;
    case CHAR_ID_GAMEKART_CHANGE:
        if (code == BN_CLICKED)
            Char_GameKartChange(page);
        break;
    case CHAR_ID_PNG:
        // The author's choice, kept for this model; the model is checked
        // again, as converted or as it is.
        if (code == BN_CLICKED)
            Char_PngSet(page, Char_IsChecked(g_char.png));
        break;
    case CHAR_ID_PNG_FIX:
        // The deliberate step: the box on, the model checked as converted.
        if (code == BN_CLICKED && g_char.pngFixOn)
            Char_PngSet(page, 1);
        break;
    case CHAR_ID_PNG_CHANGE:
        if (code == BN_CLICKED)
            Char_OptionsChange(page, g_char.png);
        break;
    case CHAR_ID_VBLEND:
        // On (PSX) <-> off; from glTF (Advanced) a click turns it on.
        if (code == BN_CLICKED && g_char.vcolMode >= 0)
            Char_VcolSet(page, g_char.vcolMode == RS_HIRES_VCOL_PSX ? RS_HIRES_VCOL_OFF : RS_HIRES_VCOL_PSX);
        break;
    case CHAR_ID_CLASSIC_MODE:
        if (code == CBN_SELCHANGE) {
            LRESULT sel = SendMessageW(g_char.classicMode, CB_GETCURSEL, 0, 0);
            if (sel == 0 || sel == 1)
                Char_ClassicSet(page, (int)sel);
        }
        break;
    case CHAR_ID_CLASSIC_FILE:
        if (code == EN_CHANGE && !g_char.applying) {
            Char_ClassicControls();
            Char_Changed(page);
        }
        break;
    case CHAR_ID_CLASSIC_BROWSE:
        if (code == BN_CLICKED) {
            wchar_t start[CHAR_VAL], pick[CHAR_VAL];
            Char_DialogStart(g_char.classicFile, start, CHAR_VAL);
            if (Rs_BrowseOpenFile(Rs_MainWindow(), L"Choose the character the model is packed into",
                                  L"Characters (*.rldchar)\0*.rldchar\0All files\0*.*\0\0", start, pick, CHAR_VAL))
                Rs_SetText(g_char.classicFile, pick);
        }
        break;
    case CHAR_ID_CLASSIC_CHANGE:
        if (code == BN_CLICKED)
            Char_OptionsChange(page, g_char.classicMode);
        break;
    case CHAR_ID_CLIP:
        if (code == CBN_SELCHANGE) {
            LRESULT sel = SendMessageW(g_char.clipCombo, CB_GETCURSEL, 0, 0);
            if (sel >= 0)
                Char_ClipSelect((int)sel);
        }
        break;
    case CHAR_ID_CLIP_PLAY:
        if (code == BN_CLICKED)
            Char_ClipPlay(!g_char.clipPlaying);
        break;
    case CHAR_ID_CLIP_PREV:
    case CHAR_ID_CLIP_NEXT:
        if (code == BN_CLICKED)
            Char_ClipAt(g_char.animSeconds + (id == CHAR_ID_CLIP_NEXT ? 1.0f : -1.0f) / 30.0f, g_char.animWheel);
        break;
    case CHAR_ID_VMODE:
        if (code == CBN_SELCHANGE && g_char.vcolMode >= 0) {
            LRESULT sel = SendMessageW(g_char.vmode, CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < CHAR_VMODES)
                Char_VcolSet(page, g_charVModeOrder[sel]);
        }
        break;
    case CHAR_ID_TEST:
        if (code == BN_CLICKED)
            Char_TestInGame(page);
        break;
    case CHAR_ID_REBUILD:
        if (code == BN_CLICKED)
            Char_Build(page);
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
        if (code == EN_CHANGE) {
            const int hint = Char_IsShown(g_char.kitStep);
            Char_ModelChanged(page);
            // The line of the Creator Kit comes and goes with an empty field.
            if (!g_char.choosing && g_char.tab == CHAR_TAB_MODEL && hint != Char_KitHint())
                Char_Relayout(page);
        }
        break;
    case CHAR_ID_NAME:
        if (code == EN_CHANGE && !g_char.applying) {
            g_char.nameDerived = 0;     // the author's own: never replaced by a model's file name
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
        if (code == EN_CHANGE) {
            if (id == CHAR_ID_OUT)
                Char_OutNote(NULL);
            Char_Changed(page);
        }
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
    // THE CLIPS: a click on a row of the table shows that clip.
    if (hdr && hdr->idFrom == CHAR_ID_CLIP_TABLE && hdr->hwndFrom == g_char.clipTable) {
        if (hdr->code == NM_CLICK && g_char.hires.animReady) {
            const NMITEMACTIVATE *a = (const NMITEMACTIVATE *)hdr;
            if (a->iItem >= 0 && a->iItem < RS_HIRES_CLIPS)
                Char_ClipSelect(a->iItem + 1);
        }
        return 0;
    }
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
        else if (id >= CHAR_ID_ROUTE && id < CHAR_ID_ROUTE + RS_ROUTES)
            Char_DrawRouteCard(di, id - CHAR_ID_ROUTE);
        else if (id == CHAR_ID_KIT_OPEN)
            Char_DrawKitButton(di);
        else if (id >= CHAR_ID_PREVIEW_SWITCH && id < CHAR_ID_PREVIEW_SWITCH + 2)
            Char_DrawSwitch(di, Char_PreviewText(id - CHAR_ID_PREVIEW_SWITCH), (id - CHAR_ID_PREVIEW_SWITCH) == g_char.previewClassic);
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
        if (!g_char.choosing)
            Char_SelectTab(page, (g_char.tab + ((int)(INT_PTR)wParam < 0 ? CHAR_TABS - 1 : 1)) % CHAR_TABS, 1);
        *handled = 1;
        return 1;
    case RS_WM_JOB_LINE: {
        wchar_t *line = (wchar_t *)lParam;
        if (line && g_char.jobId && (int)wParam == g_char.jobId) {
            Char_RawAppend(line);
            // The lines of a CHAR_JOB_META run are read when it ends (Char_MetaDone);
            // those of hires-png, hires-upscale and hires-add by the chain (Char_ChainLine).
            if (g_char.jobKind == CHAR_JOB_BUILD && g_char.chainStep >= CHAR_CHAIN_PNG)
                Char_ChainLine(line);
            else if (g_char.jobKind != CHAR_JOB_META)
                Char_Feed(line);
            if (g_char.showRaw)
                Char_RawRefresh();
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
        if (wParam == CHAR_TIMER_ANIM) {
            Char_HiresTick(page);
            *handled = 1;
            return 0;
        }
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
    case RS_WM_HOST_RETURN:
        // Back from the Creator Kit: step 0, every entry kept.
        Char_ShowChoice(page);
        *handled = 1;
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
        // The idle clip of a hi-res preview goes on (it stops while the page is hidden).
        if (Char_Chrg() && !g_char.choosing && !g_char.previewClassic && g_char.hiresChecked)
            Char_HiresShow();
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
        // The messages without Char_MsgClear: no list to fill, no layout now.
        for (i = 0; i < g_char.pageMsgCount; i++) {
            Rs_Free(g_char.pageMsg[i].text);
            Rs_Free(g_char.pageMsg[i].detail);
        }
        g_char.pageMsgCount = 0;
        g_char.listCount = 0;
        Rs_Free(g_char.pageMsg);
        g_char.pageMsg = NULL;
        Rs_Free(g_char.listMsg);
        g_char.listMsg = NULL;
        g_char.pageMsgCap = 0;
        Char_HiresForget();
        Char_ChainCleanup();
        for (i = 0; i < RS_ROUTES; i++) {
            if (g_char.routeImage[i])
                DeleteObject(g_char.routeImage[i]);
            g_char.routeImage[i] = NULL;
        }
        return 0;
    }
    }
    return 0;
}

// "upscale change", "png change": a click on "Change" at the end of a line on
// the step Model - FAIL when it is not shown, or when Advanced did not open
// at its card Options with the box shown, in view and focused.
static int Char_AutoChange(HWND page, const wchar_t *word, int id, HWND change, HWND box, const wchar_t *boxName)
{
    HWND host = GetParent(page);
    RECT at, view;
    int inView;
    if (!Char_IsShown(change)) {
        Rs_AutoLog(L"  %ls change: no line with \"Change\" on the step Model shown", word);
        return RS_AUTO_FAIL;
    }
    SendMessageW(page, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), (LPARAM)change);
    UpdateWindow(page);
    GetWindowRect(box, &at);
    GetClientRect(host, &view);
    MapWindowPoints(host, NULL, (POINT *)&view, 2);
    inView = at.left >= view.left && at.right <= view.right && at.top >= view.top && at.bottom <= view.bottom;
    Rs_AutoLog(L"  %ls change: advanced %ls%ls, \"%ls\" %ls, %ls, focus %ls", word, g_char.advOpen ? L"open, card " : L"closed",
               g_char.advOpen ? g_charExtrasTexts[g_char.extrasCard] : L"", boxName, Char_IsShown(box) ? L"shown" : L"hidden",
               inView ? L"in view" : L"out of view", GetFocus() == box ? L"on the box" : L"elsewhere");
    return g_char.advOpen && g_char.extrasCard == CHAR_EXTRAS_OPTIONS && Char_IsShown(box) && inView && GetFocus() == box ? RS_AUTO_DONE
                                                                                                                        : RS_AUTO_FAIL;
}

static int Char_Automate(HWND page, const wchar_t *verb, const wchar_t *arg)
{
    int r;

    // THE GUIDED FLOW. "route vanilla|lowpoly|hires|none" as a click on a card
    // of step 0 (none: step 0 again).
    if (wcscmp(verb, L"route") == 0) {
        int route = _wcsicmp(arg, L"none") == 0 ? RS_ROUTE_NONE : RsRoute_FromWord(arg);
        if (route == RS_ROUTE_NONE && _wcsicmp(arg, L"none") != 0) {
            Rs_AutoLog(L"  route: say vanilla, lowpoly, hires or none");
            return RS_AUTO_FAIL;
        }
        r = Char_SetRoute(page, route);
        UpdateWindow(page);
        Rs_AutoLog(L"  route: %ls", route == RS_ROUTE_NONE ? L"none - step 0" : RsRoute_Preset(route)->title);
        return r ? RS_AUTO_WAIT : RS_AUTO_DONE;
    }
    if (wcscmp(verb, L"report") == 0) {
        if (!arg[0] || !Char_WriteReport(arg)) {
            Rs_AutoLog(L"  report: could not write '%ls'", arg);
            return RS_AUTO_FAIL;
        }
        Rs_AutoLog(L"  report: %ls", arg);
        return RS_AUTO_DONE;
    }
    // THE CREATOR KIT: "kit" as a click on the way shown - "Creator Kit" of
    // step 0, or "Open Creator Kit" of the step Model; neither shown: refused.
    if (wcscmp(verb, L"kit") == 0) {
        if (!Char_IsShown(g_char.kitOpen) && !Char_IsShown(g_char.kitLink)) {
            Rs_AutoLog(L"  kit: not offered here - \"Creator Kit\" is on Build a character, \"Open Creator Kit\" on "
                       L"the step Model of every route while no model is chosen");
            return RS_AUTO_FAIL;
        }
        if (Char_IsShown(g_char.kitOpen)) {
            Rs_AutoLog(L"  kit: Creator Kit (Build a character) - all three ways");
            SendMessageW(page, WM_COMMAND, MAKEWPARAM(CHAR_ID_KIT_OPEN, BN_CLICKED), (LPARAM)g_char.kitOpen);
        } else {
            const struct RsRoutePreset *p = RsRoute_Preset(g_char.route);
            Rs_AutoLog(L"  kit: Open Creator Kit (step Model) - the way %ls", p ? p->title : L"?");
            SendMessageW(page, WM_COMMAND, MAKEWPARAM(CHAR_ID_KIT_LINK, BN_CLICKED), (LPARAM)g_char.kitLink);
        }
        return RS_AUTO_DONE;
    }
    // Old scripts name no route ("model x.obj; build"): their first verb of
    // the page picks Vanilla, the page as it was. On step 0 with a route
    // chosen before ("Change type"), a verb goes back to its steps.
    if (g_char.choosing) {
        if (g_char.route == RS_ROUTE_NONE)
            Rs_AutoLog(L"  route: vanilla (implicit - the script names no route)");
        Char_SetRoute(page, g_char.route == RS_ROUTE_NONE ? RS_ROUTE_VANILLA : g_char.route);
    }
    if (wcscmp(verb, L"step") == 0)
        return Char_AutoStep(page, arg);
    // "advanced on|off": the section Advanced of the step Model (it opens the step).
    if (wcscmp(verb, L"advanced") == 0) {
        int on = _wcsicmp(arg, L"on") == 0;
        if (!on && _wcsicmp(arg, L"off") != 0) {
            Rs_AutoLog(L"  advanced: say on or off");
            return RS_AUTO_FAIL;
        }
        if (on && Char_ExtrasCount() == 0) {
            Rs_AutoLog(L"  advanced: this route has no advanced options");
            return RS_AUTO_FAIL;
        }
        g_char.advOpen = on;
        Char_ExtrasFix();
        Rs_SetText(g_char.advanced, on ? L"Hide advanced" : L"Advanced options");
        Char_SelectTab(page, CHAR_TAB_MODEL, 0);
        UpdateWindow(page);
        Rs_AutoLog(L"  advanced: %ls%ls%ls", on ? L"open, card " : L"closed", on ? g_charExtrasTexts[g_char.extrasCard] : L"", L"");
        return RS_AUTO_DONE;
    }
    // "details on|off": as "Show details" / "Hide details" (it opens the step Build).
    if (wcscmp(verb, L"details") == 0) {
        int on = _wcsicmp(arg, L"on") == 0;
        if (!on && _wcsicmp(arg, L"off") != 0) {
            Rs_AutoLog(L"  details: say on or off");
            return RS_AUTO_FAIL;
        }
        g_char.showRaw = on;
        Char_SelectTab(page, CHAR_TAB_BUILD, 0);
        Char_ShowView();
        UpdateWindow(page);
        Rs_AutoLog(L"  details: %ls (%d line(s))", on ? L"shown" : L"hidden", g_char.rawLines);
        return RS_AUTO_DONE;
    }
    // "test-in-game": as the button (only after a build of the fields as they are).
    if (wcscmp(verb, L"test-in-game") == 0) {
        if (!g_char.builtFresh) {
            Rs_AutoLog(L"  test-in-game: not offered - build first");
            return RS_AUTO_FAIL;
        }
        return Char_TestInGame(page) ? RS_AUTO_DONE : RS_AUTO_FAIL;
    }
    // "preview hires|lowpoly|classic": the switch above the preview of a CHRG
    // route (hires and lowpoly both mean its first side, the route's model).
    if (wcscmp(verb, L"preview") == 0) {
        int classic = _wcsicmp(arg, L"classic") == 0;
        if (!Char_Chrg()) {
            Rs_AutoLog(L"  preview: only Low-Poly and Hi-Res have a hi-res picture");
            return RS_AUTO_FAIL;
        }
        if (!classic && _wcsicmp(arg, L"hires") != 0 && _wcsicmp(arg, L"lowpoly") != 0) {
            Rs_AutoLog(L"  preview: say hires, lowpoly or classic");
            return RS_AUTO_FAIL;
        }
        Char_PreviewSwitch(page, classic);
        UpdateWindow(page);
        Rs_AutoLog(L"  preview: %ls", Char_PreviewText(classic));
        return RS_AUTO_DONE;
    }
    // "vcolor on|off|psx|gltf|auto": the vertex colours of a CHRG route's
    // model - on and off as the switch "Vertex color blending" (on = psx),
    // psx, gltf and off as the list of Advanced, auto: the author's choice
    // forgotten, the suggestion again. FAIL without a model with COLOR_0.
    if (wcscmp(verb, L"vcolor") == 0) {
        int mode = _wcsicmp(arg, L"on") == 0 ? RS_HIRES_VCOL_PSX : RsHires_ColourModeOf(arg);
        if (!Char_Chrg() || g_char.vcolMode < 0) {
            Rs_AutoLog(L"  vcolor: only a Low-Poly or Hi-Res model with vertex colours (COLOR_0) has the switch");
            return RS_AUTO_FAIL;
        }
        if (_wcsicmp(arg, L"auto") == 0) {
            Char_Remember(g_char.hiresPath, CHAR_CHOICE_VCOL, -1);
            mode = g_char.vcolSuggested;
        } else if (mode < 0) {
            Rs_AutoLog(L"  vcolor: say on, off, psx, gltf or auto");
            return RS_AUTO_FAIL;
        }
        r = Char_VcolSet(page, mode);
        if (_wcsicmp(arg, L"auto") == 0) {
            Char_Remember(g_char.hiresPath, CHAR_CHOICE_VCOL, -1);
            g_char.vcolChosen = 0;
            Char_VcolControls();
        }
        UpdateWindow(page);
        Rs_AutoLog(L"  vcolor: %ls (%ls; suggested %ls)", RsHires_ColourWord(g_char.vcolMode),
                   g_char.vcolChosen ? L"the author's choice" : L"the suggestion", RsHires_ColourWord(g_char.vcolSuggested));
        return r > 0 ? RS_AUTO_WAIT : RS_AUTO_DONE;
    }
    // "upscale on|off|auto": "Upscale small textures" of a CHRG route, as a
    // click on the box (on, off: the author's choice, kept for the model
    // checked) - auto: the choice forgotten, the suggestion again. "upscale
    // change": as a click on "Change" at the end of the line on the step Model.
    if (wcscmp(verb, L"upscale") == 0) {
        int on = _wcsicmp(arg, L"on") == 0;
        if (!Char_Chrg()) {
            Rs_AutoLog(L"  upscale: only Low-Poly and Hi-Res have \"Upscale small textures\"");
            return RS_AUTO_FAIL;
        }
        if (_wcsicmp(arg, L"change") == 0)
            return Char_AutoChange(page, L"upscale", CHAR_ID_UPSCALE_CHANGE, g_char.upscaleChange, g_char.upscale, L"Upscale small textures");
        if (_wcsicmp(arg, L"auto") == 0) {
            Char_Remember(g_char.hiresPath, CHAR_CHOICE_UPSCALE, -1);
            Char_UpscaleUpdate();
            g_char.builtFresh = 0;
            Char_UpdateButtons();
        } else if (on || _wcsicmp(arg, L"off") == 0) {
            Char_UpscaleSet(on);
        } else {
            Rs_AutoLog(L"  upscale: say on, off, auto or change");
            return RS_AUTO_FAIL;
        }
        UpdateWindow(page);
        Rs_AutoLog(L"  upscale: %ls (%ls; suggested %ls, largest base colour texture %u px)", Char_IsChecked(g_char.upscale) ? L"on" : L"off",
                   g_char.upChosen ? L"the author's choice" : L"the suggestion", g_char.upSuggested ? L"on" : L"off", g_char.upLargest);
        return RS_AUTO_DONE;
    }
    // "gamekart on|off|auto|change": "Kart: from the player's game data" of a
    // CHRG route, or of Vanilla with a .glb model (K6), exactly like "upscale"
    // (on, off: the author's choice, kept for the model checked; auto: the
    // choice forgotten, the suggestion again; change: a click on "Change" at
    // the end of the line on the step Model).
    if (wcscmp(verb, L"gamekart") == 0) {
        int on = _wcsicmp(arg, L"on") == 0;
        if (!Char_Chrg() && !Char_KartBoxWanted()) {
            Rs_AutoLog(L"  gamekart: \"Kart: from the player's game data\" is on Low-Poly and Hi-Res, and on Vanilla for a "
                       L".glb model without a kart part");
            return RS_AUTO_FAIL;
        }
        if (_wcsicmp(arg, L"change") == 0)
            return Char_AutoChange(page, L"gamekart", CHAR_ID_GAMEKART_CHANGE, g_char.gameKartChange, g_char.gameKart,
                                   L"Kart: from the player's game data");
        if (_wcsicmp(arg, L"auto") == 0) {
            Char_Remember(Char_Chrg() ? g_char.hiresPath : g_char.vanKartPath, CHAR_CHOICE_GAMEKART, -1);
            Char_GameKartUpdate();
            g_char.builtFresh = 0;
            Char_UpdateButtons();
            Char_Check(page);
        } else if (on || _wcsicmp(arg, L"off") == 0) {
            Char_GameKartSet(page, on);
        } else {
            Rs_AutoLog(L"  gamekart: say on, off, auto or change");
            return RS_AUTO_FAIL;
        }
        UpdateWindow(page);
        Rs_AutoLog(L"  game kart: %ls (%ls; suggested %ls)", Char_IsChecked(g_char.gameKart) ? L"on" : L"off",
                   g_char.kartChosen ? L"the author's choice" : L"the suggestion", g_char.kartSuggested ? L"on" : L"off");
        return RS_AUTO_DONE;
    }
    // "png convert|on|off|change": "Convert JPEG textures to PNG" of a CHRG
    // route - convert as a click on the button "Convert to PNG" of the JPEG
    // error (FAIL when it is not offered), on and off as a click on the box of
    // Advanced (the author's choice, kept for the model; the model is checked
    // again), change as a click on "Change" at the end of its line on the step
    // Model. Never suggested: nothing else ticks it.
    if (wcscmp(verb, L"png") == 0) {
        int on = _wcsicmp(arg, L"on") == 0;
        if (!Char_Chrg()) {
            Rs_AutoLog(L"  png: only Low-Poly and Hi-Res have \"Convert JPEG textures to PNG\"");
            return RS_AUTO_FAIL;
        }
        if (_wcsicmp(arg, L"change") == 0)
            return Char_AutoChange(page, L"png", CHAR_ID_PNG_CHANGE, g_char.pngChange, g_char.png, L"Convert JPEG textures to PNG");
        if (_wcsicmp(arg, L"convert") == 0) {
            if (!g_char.pngFixOn || !Char_IsShown(g_char.pngFix)) {
                Rs_AutoLog(L"  png convert: no button \"Convert to PNG\" shown (no JPEG texture found, or already converted)");
                return RS_AUTO_FAIL;
            }
            Rs_AutoLog(L"  png convert: a click on \"Convert to PNG\"");
            r = Char_PngSet(page, 1);
        } else if (on || _wcsicmp(arg, L"off") == 0) {
            Char_SetChecked(g_char.png, on);
            r = Char_PngSet(page, on);
        } else {
            Rs_AutoLog(L"  png: say convert, on, off or change");
            return RS_AUTO_FAIL;
        }
        UpdateWindow(page);
        return r > 0 ? RS_AUTO_WAIT : RS_AUTO_DONE;
    }
    // THE CLIPS (Advanced, card Animations of a CHRG route), the verbs the
    // page Hi-Res had: "hires-clip bind|<clip>" as the list "Clip",
    // "hires-play on|off" as Play | Pause, "hires-time <seconds> [<wheel
    // degrees>]" paused at that frame (a frame at a time).
    if (wcscmp(verb, L"hires-clip") == 0 || wcscmp(verb, L"hires-play") == 0 || wcscmp(verb, L"hires-time") == 0) {
        const struct RsHiresCheck *c = &g_char.hires;
        if (!Char_Chrg() || !g_char.hiresChecked || !c->read || !c->animReady) {
            Rs_AutoLog(L"  %ls: clips cannot be played (%ls)", verb,
                       !Char_Chrg() ? L"only Low-Poly and Hi-Res have them" : c->animWhy[0] ? c->animWhy : L"no usable model checked");
            return RS_AUTO_FAIL;
        }
        if (wcscmp(verb, L"hires-clip") == 0) {
            int sel = -1, i;
            if (_wcsicmp(arg, L"bind") == 0)
                sel = 0;
            for (i = 0; i < RS_HIRES_CLIPS && sel < 0; i++)
                if (_wcsicmp(arg, RsHires_ClipName(i)) == 0)
                    sel = i + 1;
            if (sel < 0) {
                Rs_AutoLog(L"  hires-clip: unknown clip '%ls' (bind, idle .. menu, finish_win, finish_lose)", arg);
                return RS_AUTO_FAIL;
            }
            Char_ClipSelect(sel);
            UpdateWindow(page);
            Rs_AutoLog(L"  hires-clip: %ls", sel > 0 ? RsHires_ClipName(sel - 1) : L"bind");
            return RS_AUTO_DONE;
        }
        if (wcscmp(verb, L"hires-play") == 0) {
            const int on = _wcsicmp(arg, L"on") == 0;
            if (!on && _wcsicmp(arg, L"off") != 0) {
                Rs_AutoLog(L"  hires-play: say on or off");
                return RS_AUTO_FAIL;
            }
            if (on && Char_ClipShown() <= 0) {
                Rs_AutoLog(L"  hires-play: nothing to play (choose a clip first)");
                return RS_AUTO_FAIL;
            }
            Char_ClipPlay(on);
            Rs_AutoLog(L"  hires-play: %ls", on ? L"on" : L"off");
            return RS_AUTO_DONE;
        }
        {
            wchar_t *end = NULL;
            const double secs = wcstod(arg, &end);
            double deg;
            int n;
            if (end == arg || secs < 0.0) {
                Rs_AutoLog(L"  hires-time: say <seconds> [<wheel degrees>]");
                return RS_AUTO_FAIL;
            }
            deg = wcstod(end, &end);
            n = Char_ClipAt((float)secs, (float)deg);
            UpdateWindow(page);
            Rs_AutoLog(L"  hires-time: %ls %.3f -> %d triangles", Char_ClipShown() > 0 ? RsHires_ClipName(Char_ClipShown() - 1) : L"bind",
                       secs, n);
            return RS_AUTO_DONE;
        }
    }
    // THE CLASSIC MODEL of a CHRG route: "classic glb" made from the .glb
    // (the start), "classic <file.rldchar>" from that finished character,
    // "classic change" as a click on "Change" at the end of its line on the
    // step Model.
    if (wcscmp(verb, L"classic") == 0) {
        if (!Char_Chrg()) {
            Rs_AutoLog(L"  classic: only Low-Poly and Hi-Res have a classic model to choose");
            return RS_AUTO_FAIL;
        }
        if (_wcsicmp(arg, L"change") == 0)
            return Char_AutoChange(page, L"classic", CHAR_ID_CLASSIC_CHANGE, g_char.classicChange, g_char.classicMode, L"Classic model");
        if (!arg[0]) {
            Rs_AutoLog(L"  classic: say glb, a .rldchar or change");
            return RS_AUTO_FAIL;
        }
        if (_wcsicmp(arg, L"glb") == 0) {
            r = Char_ClassicSet(page, 0);
            Rs_AutoLog(L"  classic: made from the .glb");
        } else {
            wchar_t path[CHAR_VAL];
            Char_CleanPath(path, CHAR_VAL, arg);
            g_char.applying = 1;
            Rs_SetText(g_char.classicFile, path);
            g_char.applying = 0;
            Rs_AutoLog(L"  classic: from %ls", path);
            r = Char_ClassicSet(page, 1);
        }
        UpdateWindow(page);
        return r > 0 ? RS_AUTO_WAIT : RS_AUTO_DONE;
    }
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
        // Low-Poly and Hi-Res with another file than a .glb (the one line at
        // the model says so, or a finished character to look at), or with the
        // classic model from a character: no rldpack to start.
        if (Char_Chrg() && (!Char_EndsWith(arg, L".glb") || Char_ClassicFromFile()))
            return RS_AUTO_DONE;
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
