#define _CRT_SECURE_NO_WARNINGS
#define SDL_MAIN_HANDLED

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#include "platform/native_win32.h"
// _chdir is what the POSIX name chdir maps to in the MSVC runtime (oldnames).
#define NATIVE_CHDIR _chdir
#else
#include <unistd.h>
#define NATIVE_CHDIR chdir
#endif

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#define _EnterCriticalSection(x)
#define EnterCriticalSection(x)
#define ExitCriticalSection()

#include "platform/native_assets.h"
#include "platform/native_gpu.h"
#include "platform/native_log.h"
#include "platform/native_memory.h"
#include "platform/native_perf.h"
#include "platform/native_replay_scheduler.h"
#include "platform/native_savestate.h"

#include <platform.h>

#include "game/game_unity.h"

#include "game/zGlobal_RDATA.c"
#include "game/zGlobal_DATA.c"
#include "game/zGlobal_SDATA.c"

#undef RECT

#include "platform/native_disc_image.c"
#include "platform/native_assets.c"
#include "platform/native_audio.c"
#include "platform/native_memory.c"
#include "platform/native_checkpoint.c"
#include "platform/native_checkpoint_file.c"
#include "platform/native_cd.c"
#include "platform/native_gpu_links.c"
#include "platform/native_gpu.c"
#include "platform/native_gte_core.c"
#include "platform/native_gte_alt.c"
#include "platform/native_gte_check.c"
#include "platform/native_subpixel.c"
#include "platform/native_gte_flags.c"

#include "platform/native_input.c"
#include "platform/native_inline_c.c"
#include "platform/native_libapi.c"
#include "platform/native_libetc.c"
#include "platform/native_libgte.c"
#include "platform/native_libgpu.c"
#include "platform/native_libpad.c"
#include "platform/native_libspu.c"
#include "platform/native_log.c"
#include "platform/native_memcard.c"
#include "platform/native_memcard_adapter.c"
#include "platform/native_perf.c"
#include "platform/native_platform.c"
#include "platform/native_replay_scheduler.c"
#include "platform/native_gfx_vk.c"
#include "platform/native_gfx.c"
#include "platform/native_tex.c"
#include "platform/native_renderer.c"
#include "platform/native_savestate.c"
#include "platform/native_state.c"
#include "platform/native_str.c"
#include "platform/native_preview.c"
#include "platform/native_testfiles.c"
#include "platform/native_chars.c"
#include "platform/native_mods.c"
#include "platform/native_char_gpu.c"
#include "platform/native_twin.c"
#include "platform/native_wheels.c"
#include "platform/native_render_layer.c"
#include "platform/native_probe.c"
#include "platform/native_depth_check.c"

#ifndef CC
#if defined(__GNUC__)
#if _WIN32
#ifndef __clang__
#define CC "MINGW-GCC"
#else
#define CC "MINGW-CLANG"
#endif
#else
#ifndef __clang__
#define CC "GCC"
#else
#define CC "CLANG"
#endif
#endif
#elif defined(_MSC_VER)
#define CC "MSVC"
#else
#define CC "Unknown"
#endif
#endif

#ifndef CTR_NATIVE_VERSION
#define CTR_NATIVE_VERSION "0.0.0-dev"
#endif

// Build id from generated/ctr_build_id.h, generated on every build
// (cmake/CtrBuildId.cmake).
#include "ctr_build_id.h"
#ifndef CTR_NATIVE_BUILD_ID
#define CTR_NATIVE_BUILD_ID "unknown"
#endif

static int NativeConsole_ShouldPauseOnError(void)
{
#if defined(_WIN32)
	DWORD consoleProcesses[2];
	DWORD consoleProcessCount;

	if (GetConsoleWindow() == NULL)
		return 0;

	consoleProcessCount = GetConsoleProcessList(consoleProcesses, (DWORD)(sizeof(consoleProcesses) / sizeof(consoleProcesses[0])));
	return (consoleProcessCount == 1) && (consoleProcesses[0] == GetCurrentProcessId());
#else
	return 0;
#endif
}

static s32 NativeConsole_Return(const u32 result)
{
	if ((result != 0) && NativeConsole_ShouldPauseOnError())
	{
		fflush(stdout);
		fflush(stderr);
		fprintf(stderr, "\n[CTR Native] Press Enter to close this window...");
		fflush(stderr);

		while (getchar() != '\n' && !feof(stdin))
		{
		}
	}

	return (s32)result;
}

// TODO(aalhendi): just make an argparser?
static int NativeArg_IsVersion(const char *arg)
{
	return (arg != NULL) && ((strcmp(arg, "--version") == 0) || (strcmp(arg, "-v") == 0));
}

//----------------------------------------------------------------------------------------
// DEVELOPER SWITCHES ONLY WITH --dev
//
// Two classes of switches. PLAYER switches are the display and graphics
// settings somebody needs to play (window, fullscreen, resolution, aspect
// ratio, anti-aliasing), plus --version and --help, --log, so that a player
// can log a run for a bug report, and --image, the emergency route the
// first-start screen names. EVERYTHING ELSE - measuring, diagnostics, script
// control, experiments - is a DEVELOPER switch and only runs with --dev.
// Without --dev the start aborts at once, with the name of the switch and the
// fixed exit code NATIVE_EXIT_DEV_REQUIRED. Never pass over one silently: a
// switch that silently drops out is a run that does something other than what
// its command line says.
//
// The check comes BEFORE any evaluation, as the first thing in main. The
// evaluation itself is spread over seven places (NativeArgs_ReadDisplayFlags,
// NativeSetup_EnsureData, three loops in main, NativePerf_ConfigureFromArgs,
// NativeReplayScheduler_*FromArgs), and each of them acts while reading -
// --log fixes the log file, --crash-test arms the crash. A check "after parsing"
// would therefore be one after the effect.
//
// Both classes are in the tables below. Anything that starts with "--" and is
// in neither also aborts: without --dev as "requires --dev" (the message does
// not reveal which switches exist), with --dev as "unknown switch" - a typo in
// a measuring call that runs through anyway gives a result that is not one.
// Whoever adds a switch enters it here, otherwise the exe rejects it even with
// --dev: a switch without a row is an undocumented switch. --dev --help prints
// s_devSwitches; --help prints the player switches from NativeArgs_PrintHelp,
// which has to be kept in step with s_playerSwitches by hand.
//
// VALUES. The value form says whether the reader always takes the next word
// ("<...>", required value, argv[++argIndex]) or only when it does not start
// with "-" ("[...]"; for --windowed only the form WxH). Only a required value
// can start with "--" (--log --today.log); the check skips it, a value is never
// a switch. But if the value is itself a switch name (--log --dev, --log
// --perf), the start aborts, and likewise when it is missing (--log as the last
// word; the readers would then pass over the switch silently). The readers each
// run over all words on their own and do not know the values of the others -
// NativePerf takes --perf even behind --log, and so does the big loop with
// --deterministic. Skipped, that would be a developer switch without --dev. The
// value form therefore has to match the reader.
//
// --dev is a run value like --msaa and is never in ctr-settings.cfg: the
// file only knows s_videoSettings and the forms (native_platform.c,
// Platform_SettingsSave). No #ifdef: the same exe plays and measures.

#define NATIVE_EXIT_DEV_REQUIRED 64 // EX_USAGE from sysexits.h: wrong command line

int g_cfg_dev = 0;

typedef struct
{
	const char *name;
	const char *value; // "" no value, "<...>" required value, "[...]" only a matching next word
	const char *help;  // one sentence for --dev --help; NativeArgs_PrintHelp describes the player switches
} NativeSwitch;

static const NativeSwitch s_playerSwitches[] = {
    {"--windowed", "[WxH]", NULL},
    {"--fullscreen", "", NULL},
    {"--res-scale", "<1..max|native>", NULL},
    {"--aspect", "<W:H>", NULL},
    {"--msaa", "<1|2|4>", NULL},
    {"--log", "<file>", NULL},
    {"--image", "<path>", NULL},
    {"--version", "", NULL}, // also -v
    {"--help", "", NULL},
    {"--dev", "", NULL},
};

static const NativeSwitch s_devSwitches[] = {
    {"--deterministic", "", "measuring mode: VSync emits only the VBlanks the game asks for and never catches up by wall clock, lateness is dropped; audio is rendered per VBlank; keyboard, mouse, pads (buttons, triggers and sticks) and window focus do not reach the game, what is kept away is counted at exit except pad axes (closing the window still ends the run)"},
    {"--inject-delay", "<seed>", "disturbs the clock: about every 8th frame and boot VSync call is held 20..80 ms, which ones purely from the seed (0 = off)"},
    {"--frame-log", "", "frame log, one line per frame"},
    {"--shot", "<vblanks>", "snapshot of the internal frame at this VBlank, or an ascending list 131,133,140 (up to 128, more ends the start)"},
    {"--shot-name", "<file>", "file name of the snapshot (default SCREENSHOT.BMP); with a list, <name>-<vblank>.bmp per snapshot"},
    {"--menu-keys", "<sequence>", "scripted pad steps, e.g. down,cross or l1+r1+down (up to 256; keys up down left right cross circle square triangle start select l1 r1 l2 r2 none)"},
    {"--menu-keys-quit", "<vblanks>", "end this many VBlanks after the last step (default 120, 0 = do not end)"},
    {"--menu-keys-every", "<vblanks>", "length of one step of the sequence (default 24)"},
    {"--menu-keys-from", "<vblank>", "first step of the sequence at this VBlank (default 300)"},
    {"--menu-pads", "<n>", "the key sequence reports this many connected pads, 1 to 4 (default 1)"},
    {"--vk-validation", "", "Vulkan validation layer on"},
    {"--native-preview", "", "native render layer preview for the whole run: the native program, and depth on the main target while a native object is bound, never saved; custom characters are drawn natively only at an internal resolution of x2 or more (at x1 the retail path draws them); the same run state as NATIVE DRIVERS PREVIEW on the GRAPHICS page (saved, from the next start), on whatever is chosen there; the measuring switches that need it want it on the command line"},
    {"--native-empty-markers", "", "with --native-preview: an empty native marker at every driver instance, retail keeps drawing (self-test of the marker channel)"},
    {"--dump-vram", "<vblanks>", "VRAM dumps <prefix>-<vblank>.tga at these VBlanks (comma list, up to 8)"},
    {"--dump-prefix", "<name>", "file prefix of the VRAM dumps (default dump)"},
    {"--dump-exit", "", "end after the last VRAM dump"},
    {"--native-probe", "<form>", "with --native-preview: seat 0 of a one-player arcade race draws a generated test body natively (body; texture: the same body coloured by a texture; pose: textured, its top shaped by the retail animation frame; wheels: pose with four generated wheels in place of the retail wheels; mips: pose coloured by an sRGB texture with 9 levels, one colour each, uploaded while the race loads)"},
    {"--native-filter", "<nearest|linear>", "with --native-preview: how textures of the native texture manager are sampled - nearest (pixelated, the default) or linear (trilinear, with anisotropy up to the device limit); never saved"},
    {"--native-wheel-report", "", "only with --native-preview --native-probe: per tick the float wheel middles, roll phase and speed of the probe seat beside the middles of its retail wheels, which stay on for that seat (measuring the wheel poses)"},
    {"--native-seam-report", "", "only with --native-preview: one line per frame and native item of view 0 (probe or custom character) with its view z, shift, depth and box - the seam at view z 0x1000 (measuring only)"},
    {"--native-depth-tint", "", "only with --native-preview: every native draw is coloured by its depth (red and blue full, green = 1024 / w) instead of its colours - the seam at view z 0x1000 made visible (measuring only)"},
    {"--native-char-gpu-selftest", "<dir>", "the good .rldchar files of dir (written by rldpack make-native-tests) through the native read and the GPU set build of a custom character without a device, then end (ctest native_char_gpu_selftest)"},
    {"--native-split-report", "", "measuring only: per water line view of seat 0 drawn as a custom character (SPLIT, and SPECIAL with the split line), the raw values the queue split it with - the first four, then every 30th VBlank"},
    {"--native-twin-selftest", "<dir>", "the retail twin (step 4d, platform/native_twin.c) without a device: a retail model and a VRAM made in memory, then old_plain.rldchar of dir (written by rldpack make-native-tests), through the converter, then end (ctest native_twin_selftest)"},
    {"--native-twin", "", "only with --native-preview, measuring only: seat 0's retail model drawn natively as its retail twin (made at the race's loading screen from the model and the VRAM mirror), its retail wheels on"},
    {"--native-hide-exhaust", "", "measuring only: the exhaust particles of every seat (and the burn smoke, same icons) are moved and counted as always but not drawn, with or without --native-preview; the count comes at exit"},
    {"--native-probe-selftest", "", "check the bytes of the generated probe mesh against their hash (ctest native_probe_selftest)"},
    {"--native-depth-selftest", "", "check that a native draw keeps its depth across view z 0x1000 (ctest native_depth_selftest)"},
    {"--native-tex-selftest", "", "check the native texture manager: tables, the shader round trip, the edge rule and the levels of a fixed image (ctest native_tex_selftest)"},
    {"--native-probe-seat", "<n>", "only with --native-probe: the probe takes the model of seat n (0 to 7) instead of seat 0 (default 0)"},
    {"--native-depth-d24", "", "with --native-preview: depth images as X8_D24_UNORM_PACK32 instead of D32_SFLOAT (measuring the two)"},
    {"--perf", "", "perf recording per frame into debug/perf/perf-latest"},
    {"--perf-dir", "<folder>", "target folder of the perf recording, switches it on"},
    {"--autoload-demo", "", "with --autoload-track or --level: demo race, the bots drive every seat (HUD off, no time limit)"},
    {"--autopilot", "", "the player's seat drives as a bot, in a real race with HUD, finish and points"},
    {"--record-preview", "", "with --autoload-track records the track preview (10 s from the driver camera, the AI drives invisibly, 150 frames, game window hidden) and writes tracks/vorschau/<container>.rldprev"},
    {"--weapon-pool-empty", "", "empties the pool for missiles, bombs, shields and warpballs at race start (crash probe for the weapon fix)"},
    {"--unlock-scrapbook", "", "unlocks the Scrapbook for this session; a save made meanwhile keeps it"},
    {"--focus-pause", "", "pause on minimise/focus loss even with --dev (test of that pause); never with --deterministic"},
    {"--autoload-track", "<file>", "load this track container (file name in tracks/) as soon as the main menu is up"},
    {"--exit-after-frames", "<n>", "end after n frames in the race"},
    {"--instance-pool", "<n>", "instance pool in a race at n slots, only below the retail size (probe for a full pool)"},
    {"--crystal-grab", "<n>", "NITRO-PIT -> CRYSTAL: collects n crystals through their collision path (test probe)"},
    {"--ctr-grab", "<n>", "NITRO-PIT -> CTR: collects n letters through their collision path (test probe, sets no place)"},
    {"--settings-defaults", "", "built-in defaults, ctr-settings.cfg and track-ids.tsv neither read nor written"},
    {"--msaa-at", "<V:L,V:L,...>", "switch anti-aliasing to level L (1, 2 or 4) at VBlank V, for this run only (up to 32 pairs)"},
    {"--sample-shading", "<off|textured>", "sample shading of the anti-aliasing for comparison (default textured)"},
    {"--level", "<n>", "jump straight into level id n as soon as the main menu is up"},
    {"--driver", "<n>", "driver for the jump with --level or --autoload-track (default 0, Crash)"},
    {"--level-tour", "<list>", "drive the listed tracks one after another, each started from the main menu like --level / --autoload-track and left after --level-tour-frames race frames like QUIT, then end; list: races (level ids 0..17), containers (every container of the track folder), level ids, container file names, comma-separated"},
    {"--level-tour-frames", "<n>", "race frames per track of --level-tour (default 600)"},
    {"--setup", "", "first-start screen even when data is there"},
    {"--record", "", "record a replay report under debug/reports/ from boot (the log goes there too)"},
    {"--replay", "<file>", "play a replay"},
    {"--replay-bypass-header", "", "with --replay: play it without checking the header"},
    {"--toggle", "", "with --record: only arm the report, F9 starts and F10 stops it"},
    {"--detailed", "", "with --record: rolling checkpoints instead of only the first one"},
    {"--crash-test", "", "deliberate crash before SDL_Init (self-test of the crash report)"},
    {"--slow-boot", "", "whole boot with announcer, logos, copyright, crates and title"},
    {"--legacy-copyright-hold", "", "old hold of the copyright picture (comparison)"},
    {"--legacy-boot-logos", "", "old boot logos (comparison)"},
    {"--keep-intro-level", "", "keep the intro track (comparison)"},
    {"--disc-report", "", "disc read counters, report at exit"},
    {"--native-layer-report", "", "render layer counters, report at exit"},
    {"--near-report", "", "report of the near-plane probe"},
    {"--gte-report", "[n]", "GTE count, every n frames, without a number only the total at the end"},
    {"--subpixel-report", "[n]", "count vertices with a fractional part, every n frames, without a number at the end"},
    {"--lod-report", "<vblank> [frames]", "LOD count over a window of frames from this VBlank (default 60 frames)"},
    {"--present-report", "<vblank>", "present report at this VBlank"},
    {"--feedback-report", "", "feedback report of the renderer"},
    {"--rank-report", "[n]", "inputs of the race position per driver, every n VBlanks (default 60)"},
    {"--split-audit", "", "check of the split table in every 30th frame"},
    {"--clip-trace", "", "trace of the clip buffers"},
    {"--ui-watch", "", "UI watch"},
    {"--ui-watch-from", "<vblank>", "UI watch from this VBlank on (switches the watch on)"},
    {"--ui-elements", "<vblank>", "print the UI elements of the frame at this VBlank"},
    {"--hole-probe", "<V,X,Y>", "log primitives covering pixel X,Y at VBlanks V to V+4 (repeat for up to four points)"},
    {"--show-platform-frames", "", "make platform frames visible"},
    {"--texture-filter", "", "bilinear filtering for an A/B run (in game F3)"},
    {"--dither", "<off|packed|always>", "dither method for this run"},
    {"--lod", "<stock|all|tex,track,model,subdiv>", "force these detail stages to their highest level"},
    {"--lod-keep", "", "distance mechanism on (comparison run)"},
    {"--lod-none", "", "distance mechanism off (the default)"},
    {"--no-sky", "", "sky gone (measuring switch)"},
    {"--fill-rect-as-draw", "", "fill rectangles as draw (the default)"},
    {"--fill-rect-as-clear", "", "fill rectangles as clear (the old path)"},
    {"--page-scale", "<n>", "atlas texels per VRAM texel, 1 to 4"},
    {"--vram-copy-unclamped", "", "VRAM copy without clamp"},
    {"--clip-window-512", "", "clip window 512"},
    {"--size-rule-canvas", "", "PS1 size rule on the canvas"},
    {"--vertex-ring-off", "", "vertex ring off"},
    {"--page-rows-off", "", "page rows off"},
    {"--page-preload-off", "", "page preloading off"},
    {"--bind-vertex-implicit", "", "implicit vertex binding"},
    {"--semi-two-pass", "", "semi-transparency in two passes"},
    {"--skip-null-tex", "", "do not draw polygons with an empty texture reference"},
    {"--whole-attachment-clears", "", "clear the whole attachment, old path (measuring switch)"},
    {"--ptr-map-unchecked", "", "pointer map without check"},
    {"--tracks-fixed-memory", "", "a fixed 1 MiB behind the mempack window instead of what the containers need"},
    {"--tracks", "", "no effect, only accepted for old measuring command lines"},
    {"--no-tracks", "", "do not open tracks/"},
    {"--tracks-dir", "<folder>", "containers from this folder instead of tracks/ (reference runs)"},
    {"--chars-dir", "<folder>", "custom characters from every .rldchar of this folder instead of characters/, also with --settings-defaults (test runs); with --char only the folder of that file"},
    {"--char", "<file>", "only this .rldchar (in --chars-dir, or an absolute path) and no folder: a tile in the one-player arcade driver select right after the retail drivers, or with --level and --driver <its template> straight on seat 0"},
    {"--dev-grid-fill", "<n>", "n placeholder tiles, 1 to 32, after the custom character tiles in the one-player arcade driver select (rows and scrolling with many entries); a placeholder cannot be chosen"},
    {"--dev-char-seats", "<all|cycle>", "every seat of a one-player arcade race drives a custom model, the bots put on the file's template (give --driver <that template> for seat 0), with the template's class: all =the first file of the roster, cycle = the next file at every race load (several models measured in one run); draw memory and mempack grow by eight models"},
    {"--dev-char-seat-files", "<f0,f1,...>", "as --dev-char-seats all, but seat 0, 1, ... drives the roster file f0, f1, ... (1 to 8 file names of the folder, commas between), every further seat the first file of the roster, each bot on its own file's template; not with --dev-char-seats or --char"},
    {"--dev-mods", "<value[,driver...]>", "the MODS page for this run, never saved: value = default|random|selected (CPU DRIVERS: DEFAULT, ALL RANDOM, ONLY SELECTED), the drivers after it the ticked ones - a number 0..14 a retail driver, anything else a file name of the folder; without any, the ticks of the file"},
    {"--dev-mods-seed", "<n>", "the draw of the MODS page starts from this number (1 to 4294967295) instead of the clock: the same seats again in the same run order"},
    {"--dev-vk-pending-cap", "<n>", "the Vulkan retire lists of textures and framebuffers hold only n entries (1 to 1024) instead of 1024: a measuring run in which a full list is drained at once"},
    {"--ui-safe-area-off", "", "UI safe area off"},
    {"--ui-declarations-off", "", "UI declarations off"},
    {"--ui-floor-off", "", "UI floor off"},
    {"--ui-anchor-per-group", "", "UI anchor per group"},
    {"--ui-anchor-legacy", "", "old UI anchors"},
    {"--selftest-disc", "<dir>", "unpack the test images in <dir> (good-*/bad-*.bin), then end"},
    {"--gte-selftest", "", "both GTE paths against each other, then end"},
    {"--char-grid-selftest", "", "layout, navigation and scrolling of the driver select grid for 0 to 32 custom entries, then end; no window, no data needed"},
    {"--char-native-selftest", "<dir>", "every .rldchar of dir (written by rldpack make-native-tests) through the roster read and the native read (CNET, CTXT) without and with --native-preview, each against the expectation its name gives, then end; no window, no data needed"},
    {"--selftest-containers", "<dir>", "every check the game runs on a container, on every *.rldtrack in dir (good-* must load, bad-* must be refused), then end; no window, no data needed"},
    {"--make-test-containers", "<dir>", "write the synthetic good-*/bad-* containers of the container self-test into dir, then end"},
    {"--make-test-disc", "<dir>", "write the synthetic good-*/bad-* disc images of the disc self-test into dir, then end"},
    {"--gte-alt", "", "alternative GTE arithmetic"},
    {"--gte-near-div", "", "GTE division route that does not saturate (acts only with --gte-alt)"},
    {"--near-plane", "[n]", "near plane n (clip threshold 2n); without a number or with 0 the stock values"},
    {"--near-detail-off", "", "near detail off"},
    {"--near-cut-off", "", "near cut off"},
    {"--near-cut-sites", "<mask>", "bit mask of the 13 near cut sites that keep their clip record (default 0x1fff, all)"},
    {"--near-box-drop", "", "drop the near box"},
    {"--near-box-keep", "", "keep the near box (the default)"},
    {"--subpixel", "", "subpixel shadow in the GTE"},
    {"--save-state-at", "<vblank>", "save a quick state at this VBlank"},
    {"--load-state-at", "<vblank>", "load the quick state at this VBlank"},
};

// Looks a word up in both tables; NULL if this exe does not know it.
// *outDev says which one it came from.
static const NativeSwitch *NativeArgs_FindSwitch(const char *arg, int *outDev)
{
	if (strcmp(arg, "-v") == 0)
	{
		arg = "--version";
	}

	for (size_t i = 0; i < sizeof(s_playerSwitches) / sizeof(s_playerSwitches[0]); i++)
	{
		if (strcmp(arg, s_playerSwitches[i].name) == 0)
		{
			*outDev = 0;
			return &s_playerSwitches[i];
		}
	}

	for (size_t i = 0; i < sizeof(s_devSwitches) / sizeof(s_devSwitches[0]); i++)
	{
		if (strcmp(arg, s_devSwitches[i].name) == 0)
		{
			*outDev = 1;
			return &s_devSwitches[i];
		}
	}

	return NULL;
}

static void NativeArgs_PrintHelp(void)
{
	printf("CTR Reload %s (%s)\n", CTR_NATIVE_VERSION, CTR_NATIVE_BUILD_ID);
	printf("Usage: ctr_native.exe [switches]\n");
	printf("\n");
	printf("Player switches:\n");
	printf("  --windowed [WxH]          window instead of fullscreen, optionally with a fixed size (e.g. 1280x720);\n");
	printf("                            in game, F11 and Alt+Enter toggle it\n");
	printf("  --fullscreen              fullscreen (the default, unless the GRAPHICS page saved windowed)\n");
	printf("  --res-scale <1..%d|native> internal resolution as a multiple of the PS1 resolution, native = the\n", Platform_GetResolutionScaleMax());
	printf("                            window's own pixels; default: the saved setting, otherwise 4\n");
	printf("  --aspect <W:H>            aspect ratio of the canvas, e.g. 4:3, 16:9, 43:18; default: the saved\n");
	printf("                            setting, otherwise detected from the display\n");
	printf("  --msaa <1|2|4>            anti-aliasing for this run (1 = off); otherwise the saved setting applies\n");
	printf("                            (GRAPHICS page, default 4)\n");
	printf("  --log <file>              log file of this run; default: a new file in logs/, the last 5 are kept\n");
	printf("  --image <path>            unpack the game data from this disc image without asking\n");
	printf("  --version, -v             version and build\n");
	printf("  --help                    this overview\n");
	printf("  --dev                     unlock the developer switches\n");
	printf("\n");
	printf("--windowed, --fullscreen, --res-scale, --aspect and --msaa apply to this run and are not saved.\n");
	printf("Relative paths (--log, --image) are taken relative to the game folder, not the current directory.\n");
	printf("All other switches (measuring, diagnostics, script control) need --dev, otherwise\n");
	printf("the start aborts with exit code %d. --dev --help shows them.\n", NATIVE_EXIT_DEV_REQUIRED);
}

// --dev --help: one line per developer switch, from the same table the
// check reads.
static void NativeArgs_PrintDevHelp(void)
{
	printf("CTR Reload %s (%s)\n", CTR_NATIVE_VERSION, CTR_NATIVE_BUILD_ID);
	printf("Developer switches, only with --dev (--help shows the player switches):\n");

	for (size_t i = 0; i < sizeof(s_devSwitches) / sizeof(s_devSwitches[0]); i++)
	{
		char form[64];

		snprintf(form, sizeof(form), "%s%s%s", s_devSwitches[i].name, (s_devSwitches[i].value[0] != '\0') ? " " : "", s_devSwitches[i].value);
		printf("  %-32s %s\n", form, s_devSwitches[i].help);
	}

	printf("<value> always takes the next word, [value] only if it does not start with -.\n");
	printf("The player switches (--help) work with --dev as well. Relative paths are taken relative to the\n");
	printf("game folder, not the current directory.\n");
}

// The bytes of a list switch that is cut into its entries in a copy (--msaa-at).
#define NATIVE_ARG_LIST_MAX 512

// The values that are kept in a buffer of fixed size, or counted into a table:
// at most bytes - 1 characters (0 = any length) and at most entries
// comma-separated entries (0 = any number). A value past either ends the start
// in the gate below, before the first window, and is never cut: a cut path
// names another file or folder, a cut list other moments, and the run would
// measure something else than its command line says.
typedef struct
{
	const char *name;
	size_t bytes;
	int entries;
} NativeValueLimit;

static const NativeValueLimit s_valueLimits[] = {
    {"--log", sizeof(s_logPath), 0},
    {"--shot", 0, PLATFORM_SHOT_MAX},
    {"--shot-name", PLATFORM_ARG_PATH_MAX, 0},
    {"--dump-prefix", PLATFORM_ARG_PATH_MAX, 0},
    {"--msaa-at", NATIVE_ARG_LIST_MAX, 0},
    {"--perf-dir", NATIVE_PERF_PATH_MAX, 0},
    {"--tracks-dir", sizeof(s_nativeTrackFolder), 0},
    {"--chars-dir", NATIVE_CHAR_PATH_MAX, 0},
    {"--char", NATIVE_CHAR_PATH_MAX, 0},
    {"--autoload-track", sizeof(g_cfg_autoloadTrack), 0},
    {"--level-tour", sizeof(g_cfg_levelTour), 0},
};

// The entries of a comma list as the readers count them: empty ones (",,")
// are skipped.
static int NativeArgs_ListEntries(const char *value)
{
	int entries = 0;
	int inEntry = 0;

	for (; *value != '\0'; value++)
	{
		if (*value == ',')
		{
			inEntry = 0;
		}
		else if (!inEntry)
		{
			inEntry = 1;
			entries++;
		}
	}

	return entries;
}

// 0 = the value fits (or the switch keeps no fixed buffer). Otherwise
// NATIVE_EXIT_DEV_REQUIRED, with the message on stderr.
static int NativeArgs_ValueFits(const char *arg, const char *value)
{
	for (size_t i = 0; i < sizeof(s_valueLimits) / sizeof(s_valueLimits[0]); i++)
	{
		const NativeValueLimit *limit = &s_valueLimits[i];
		size_t length;
		int entries;

		if (strcmp(arg, limit->name) != 0)
		{
			continue;
		}

		length = strlen(value);
		if ((limit->bytes > 0) && (length >= limit->bytes))
		{
			fflush(stdout);
			fprintf(stderr, "switch %s takes at most %u characters, got %u - refused, never cut\n", arg, (unsigned)(limit->bytes - 1), (unsigned)length);
			fflush(stderr);
			return NATIVE_EXIT_DEV_REQUIRED;
		}

		entries = (limit->entries > 0) ? NativeArgs_ListEntries(value) : 0;
		if (entries > limit->entries)
		{
			fflush(stdout);
			fprintf(stderr, "switch %s takes at most %d entries, got %d - refused, never cut\n", arg, limit->entries, entries);
			fflush(stderr);
			return NATIVE_EXIT_DEV_REQUIRED;
		}

		return 0;
	}

	return 0;
}

// 0 = go on. Otherwise the exit code with which main ends at once; the message
// is then already on stderr. Called before everything else, see above.
static int NativeArgs_GateDevSwitches(int argc, char *argv[])
{
	const NativeSwitch *known;
	int dev = 0;

	// First pass: is --dev in the position of a switch? As a value
	// (--log --dev) it unlocks nothing; the second pass rejects that.
	for (int argIndex = 1; argIndex < argc; argIndex++)
	{
		known = NativeArgs_FindSwitch(argv[argIndex], &dev);

		if (known == NULL)
		{
			continue;
		}

		if (strcmp(argv[argIndex], "--dev") == 0)
		{
			g_cfg_dev = 1;
		}

		if (known->value[0] == '<')
		{
			argIndex++;
		}
	}

	// Second pass, in command-line order: the first finding
	// aborts. Words without "--" (the [..] values, -v) are not looked at.
	for (int argIndex = 1; argIndex < argc; argIndex++)
	{
		const char *arg = argv[argIndex];

		if (strncmp(arg, "--", 2) != 0)
		{
			continue;
		}

		known = NativeArgs_FindSwitch(arg, &dev);

		if ((known == NULL) && g_cfg_dev)
		{
			fflush(stdout);
			fprintf(stderr, "unknown switch %s\n", arg);
			fprintf(stderr, "[CTR Native] --dev --help shows the developer switches\n");
			fflush(stderr);
			return NATIVE_EXIT_DEV_REQUIRED;
		}

		if ((known == NULL) || (dev && !g_cfg_dev))
		{
			fflush(stdout);
			fprintf(stderr, "switch %s requires --dev\n", arg);
			fprintf(stderr, "[CTR Native] --help shows the player switches; measuring and diagnostic calls need --dev\n");
			fflush(stderr);
			return NATIVE_EXIT_DEV_REQUIRED;
		}

		if ((known->value[0] == '<') && ((argIndex + 1) >= argc))
		{
			// Last word: every reader checks "(argIndex + 1) < argc" and
			// otherwise passes over the switch silently.
			fflush(stdout);
			fprintf(stderr, "switch %s expects a value %s\n", arg, known->value);
			fflush(stderr);
			return NATIVE_EXIT_DEV_REQUIRED;
		}

		if (known->value[0] == '<')
		{
			const char *value = argv[++argIndex];

			if (NativeArgs_FindSwitch(value, &dev) != NULL)
			{
				fflush(stdout);
				fprintf(stderr, "switch %s expects a value %s, got the switch %s\n", arg, known->value, value);
				fprintf(stderr, "[CTR Native] a switch name is never a value\n");
				fflush(stderr);
				return NATIVE_EXIT_DEV_REQUIRED;
			}

			// A value too long for its buffer, or a list with too many entries.
			{
				const int fits = NativeArgs_ValueFits(arg, value);

				if (fits != 0)
				{
					return fits;
				}
			}
		}
	}

	if (g_cfg_dev)
	{
		printf("[CTR Native] --dev: developer switches unlocked\n");
	}

	return 0;
}

// The flags that have to be read BEFORE anything exists, and why they are not
// down in the big loop with the rest.
//
// Every other flag acts on something that is already there. These decide what
// comes into being: where the log is written, what shape the picture is driven
// in, whether the window comes up fullscreen (or hidden), and what the Vulkan
// instance and the main target are created with. Read after the fact
// they would be second answers to questions already answered - and --log was
// exactly that. It sat in the loop below, which runs after Platform_Init has
// already opened the default log file, and Platform_LogSetPath refuses a path
// once the stream is open. It printed the name it was given and wrote to the
// old file, which is how two runs that were meant to be compared ended up in
// one file a second time.
//
// --aspect is deliberately outside the CTR_INTERNAL block the other flags live
// in. It is a setting somebody drives with, not a measuring tool.
// --windowed/--fullscreen apply to the session and beat "video fullscreen"
// from ctr-settings.cfg (Platform_SettingsPreloadDisplay).
int g_cfg_fullscreenFromFlag = 0;

static int NativeArgs_ReadDisplayFlags(int argc, char *argv[], int *outWidth, int *outHeight)
{
	int Platform_LogSetPath(const char *path);
	int fullscreen = 1;

	// Zero means "work it out from the shape". Only --windowed WxH replaces it.
	*outWidth = 0;
	*outHeight = 0;

	for (int argIndex = 1; argIndex < argc; argIndex++)
	{
		if (strcmp(argv[argIndex], "--crash-test") == 0)
		{
			// Up here and not in the big loop: the crash is meant to happen before
			// SDL_Init, that is before there is a window. What is in the log
			// afterwards is the self-test of the crash report.
			extern int g_cfg_crashTest;

			g_cfg_crashTest = 1;
			printf("[CTR Native] crash test armed - the game will fault on purpose\n");
		}
		else if (strcmp(argv[argIndex], "--vk-validation") == 0)
		{
			// UP HERE, NOT ONLY IN THE BIG LOOP.
			// The instance comes into being in Platform_Init (NativeVk_CreateInstance),
			// the big loop runs afterwards - set only there, the switch came
			// too late and every log said "validation layer off", even with
			// --vk-validation. The late place stays and sets the same value
			// once more.
			extern int g_cfg_vkValidation;

			g_cfg_vkValidation = 1;
		}
		else if (strcmp(argv[argIndex], "--native-preview") == 0)
		{
			// UP HERE, NOT ONLY IN THE BIG LOOP.
			// The native program, the probe mesh and the probe texture come
			// into being in Platform_Init (NativeRenderer_InitialisePSX), the
			// big loop runs afterwards - set only there, none of them would be
			// made. Nothing is printed here, the log is not open yet; the late
			// place sets the same value once more and says so. NATIVE DRIVERS
			// on the GRAPHICS page sets the same value from ctr-settings.cfg,
			// also before the window (Platform_SettingsPreloadDisplay); it never
			// turns off what this set.
			extern int g_cfg_nativePreview;

			g_cfg_nativePreview = 1;
		}
		else if ((strcmp(argv[argIndex], "--native-probe") == 0) && ((argIndex + 1) < argc))
		{
			// UP HERE as well: the probe mesh buffers come into being with the
			// native program in Platform_Init (NativeRenderer_InitialisePSX).
			// Silent like --native-preview; the late place checks the value and
			// the pairing with --native-preview and says so.
			extern int g_cfg_nativeProbe;
			const char *form = argv[++argIndex];

			g_cfg_nativeProbe = NativeProbe_FormFromName(form);

			// The form wheels is the form pose plus the wheels; its texture
			// carries the wheel texel and is made in Platform_Init as well.
			g_cfg_nativeProbeWheels = (g_cfg_nativeProbe != NATIVE_PROBE_NONE) && NativeProbe_FormHasWheels(form);

			// The form mips is the form pose with the mips texture, made at the
			// race's loading screen rather than in Platform_Init.
			g_cfg_nativeProbeMips = (g_cfg_nativeProbe != NATIVE_PROBE_NONE) && NativeProbe_FormHasMips(form);
		}
		else if ((strcmp(argv[argIndex], "--msaa") == 0) && ((argIndex + 1) < argc))
		{
			// ANTI-ALIASING: samples of the main target, 1 2 4 - off, 2x, 4x.
			// Beats the saved level for this run and is never written back; a
			// choice in the menu (GRAPHICS page or debug menu) cancels it
			// (native_renderer.c, NativeRenderer_GetMsaaRequested). Up
			// here, because the main target comes into being in Platform_Init and
			// the level is meant to be fixed there already.
			extern int g_cfg_msaaSamples;
			const int samples = atoi(argv[++argIndex]);

			if ((samples == 1) || (samples == 2) || (samples == 4))
			{
				g_cfg_msaaSamples = samples;
				printf("[CTR Native] --msaa %d\n", samples);
			}
			else
			{
				printf("[CTR Native] --msaa wants 1, 2 or 4 - '%s' ignored\n", argv[argIndex]);
			}
		}
		else if ((strcmp(argv[argIndex], "--sample-shading") == 0) && ((argIndex + 1) < argc))
		{
			// A measuring switch. Sample shading belongs firmly to anti-aliasing:
			// without the switch, with more than one sample every textured
			// draw gets sample shading (textured), with one sample none does. off
			// takes it out for the comparison.
			extern int g_cfg_sampleShading;
			const char *mode = argv[++argIndex];

			if (strcmp(mode, "off") == 0) { g_cfg_sampleShading = 0; }
			else if (strcmp(mode, "textured") == 0) { g_cfg_sampleShading = 1; }
			else
			{
				printf("[CTR Native] --sample-shading wants off or textured - '%s' ignored\n", mode);
			}
		}
		else if ((strcmp(argv[argIndex], "--log") == 0) && ((argIndex + 1) < argc))
		{
			// Two runs that are going to be compared need two files. The default is
			// one name for every run, so the second run erases the first - which
			// costs exactly the comparison the two runs existed for.
			Platform_LogSetPath(argv[++argIndex]);
			printf("[CTR Native] log: %s\n", argv[argIndex]);
		}
		else if ((strcmp(argv[argIndex], "--aspect") == 0) && ((argIndex + 1) < argc))
		{
			// "4:3", "16:9", "43:18" - or any W:H, so two shapes can be put side by
			// side without a new flag each time.
			//
			// 43:18 is 2.38889, which is 3440x1440 exactly. What is commonly written
			// 21:9 is 2.33333 and is the aspect of no monitor.
			//
			// Given here, the display is not asked at all - 16:9 has to stay drivable
			// on a 21:9 monitor, and a setting that the next re-detection overwrites
			// is not a setting.
			const char *spec = argv[++argIndex];
			int aspectW = 0;
			int aspectH = 0;

			if ((sscanf(spec, "%d:%d", &aspectW, &aspectH) == 2) && (aspectW > 0) && (aspectH > 0))
			{
				Platform_SetAspectOverride(aspectW, aspectH);
				printf("[CTR Native] aspect %d:%d (from the command line)\n", aspectW, aspectH);
			}
			else
			{
				printf("[CTR Native] --aspect wants W:H, for example 16:9 - '%s' ignored\n", spec);
			}
		}
		else if (strcmp(argv[argIndex], "--windowed") == 0)
		{
			g_cfg_fullscreenFromFlag = 1;

			// A window instead of the whole screen. F11 and Alt+Enter reach the
			// same two states from either end.
			//
			// The size is worked out from the shape that was detected, so a 43:18
			// picture arrives in a 43:18 window instead of a 4:3 one with the
			// picture as a strip across the middle. An optional WxH after the flag
			// says a size instead - and then it is a size, not a suggestion, and a
			// later change of shape does not move it.
			fullscreen = 0;

			if ((argIndex + 1) < argc)
			{
				int wantWidth = 0;
				int wantHeight = 0;

				if ((sscanf(argv[argIndex + 1], "%dx%d", &wantWidth, &wantHeight) == 2) && (wantWidth > 0) && (wantHeight > 0))
				{
					*outWidth = wantWidth;
					*outHeight = wantHeight;
					argIndex++;
					printf("[CTR Native] window %dx%d (from the command line)\n", wantWidth, wantHeight);
				}
			}
		}
		else if (strcmp(argv[argIndex], "--fullscreen") == 0)
		{
			g_cfg_fullscreenFromFlag = 1;
			fullscreen = 1;
		}
		else if (strcmp(argv[argIndex], "--settings-defaults") == 0)
		{
			// Everything at its built-in default, and ctr-settings.cfg neither read
			// nor written. What a measuring run wants: a tool that reads a file
			// somebody left tuned is measuring the file.
			Platform_SetSettingsLocked(1);
			printf("[CTR Native] settings: defaults, file locked\n");
		}
		else if (strcmp(argv[argIndex], "--record-preview") == 0)
		{
			// Only the invisible window here (native_renderer.c,
			// g_cfg_windowHidden): it comes into being in Platform_Init, before the big
			// loop. The recording itself is switched on by the big loop, which reads
			// the switch again afterwards. The check has already demanded
			// --dev.
			extern int g_cfg_windowHidden;

			g_cfg_windowHidden = 1;
		}
	}

	return fullscreen;
}


//----------------------------------------------------------------------------------------
// THE FIRST START
//
// The first thing a stranger sees. It runs before the renderer, before the
// window the game uses, before anything is validated - because at this point
// there may be nothing on disk at all except this executable.
//
// WHAT IT IS NOT: it is not a copy of the disc, it does not move the image, it
// does not delete it. The user brings their own disc image, it is read once, and
// it stays exactly where they keep it. Nothing about the game data is shipped.
//
// The screen is drawn with SDL's own 2D renderer and its built-in debug font,
// not with the game's Vulkan path. That is deliberate: the game's renderer needs
// the very data this screen exists to fetch, so using it here would be a circle.
// The debug font needs no asset at all, which is the only kind of font that can
// be relied on at this moment.
//
// The text is English. The game is English, the disc is English, and the person
// reading this screen is somebody who has just downloaded the build.

#define NATIVE_SETUP_MARKER_PATH  "ctr-data.cfg"
#define NATIVE_SETUP_MARKER_MAGIC "# CTR Reload - what was unpacked from the disc image"
#define NATIVE_SETUP_WINDOW_W     880
#define NATIVE_SETUP_WINDOW_H     560
#define NATIVE_SETUP_REDRAW_MS    33

// The built-in debug font is 8x8. All sizes here are integer multiples
// of it - a bitmap font scaled to 1.5 looks like a mistake, and this
// screen is the first thing a stranger sees of us.
#define NATIVE_SETUP_GLYPH        8

#define NATIVE_SETUP_SCALE_TITLE  5
#define NATIVE_SETUP_SCALE_SUB    2
#define NATIVE_SETUP_SCALE_BIG    3
#define NATIVE_SETUP_SCALE_TEXT   2
#define NATIVE_SETUP_SCALE_FINE   1

// FRAME AND FOOTER, FROM ONE CALCULATION.
//
// They collided, and to the exact pixel: the footer stood 46 above the
// window edge, its block is 36 high, the frame sits 10 inside - 46 minus 36
// is 10, so the last line ended exactly ON the frame line. Two numbers,
// chosen independently of each other, that happened to meet exactly.
//
// That is why the footer is no longer placed at the window edge but at the
// frame: frame inside, its thickest version (4 instead of 1 while dragging),
// a gap, then the block. Whoever changes one of the numbers moves the
// other with it - the collision cannot come back.
#define NATIVE_SETUP_FRAME_INSET  10
#define NATIVE_SETUP_FRAME_THICK  4
#define NATIVE_SETUP_FOOTER_LINES 3
#define NATIVE_SETUP_FOOTER_LEAD  6
#define NATIVE_SETUP_FOOTER_GAP   10
#define NATIVE_SETUP_FOOTER_BLOCK ((NATIVE_SETUP_FOOTER_LINES * NATIVE_SETUP_GLYPH) + ((NATIVE_SETUP_FOOTER_LINES - 1) * NATIVE_SETUP_FOOTER_LEAD))
#define NATIVE_SETUP_FOOTER_TOP   (NATIVE_SETUP_FRAME_INSET + NATIVE_SETUP_FRAME_THICK + NATIVE_SETUP_FOOTER_GAP + NATIVE_SETUP_FOOTER_BLOCK)

#define NATIVE_SETUP_MESSAGE_MAX  512
#define NATIVE_SETUP_PATH_MAX     1024

// THE ONLY DISC THIS BUILD ACCEPTS, AND THE TWO IT NAMES BY NAME.
//
// Region is not a preference here. The overlays, the string table offsets and
// the memory card names in this tree are all NTSC-U; a PAL disc has the same
// directory layout, so nothing short of the serial would catch it and the game
// would fail somewhere deep inside a level load instead - which is the failure
// nobody can debug from a screenshot.
//
// The other two are listed so the message can say what the user actually has
// rather than "unsupported".
#define NATIVE_SETUP_SERIAL_NTSC_U "SCUS-94426"

struct NativeSetupKnownDisc
{
	const char *serial;
	const char *region;
};

static const struct NativeSetupKnownDisc s_setupKnownDiscs[] = {
    {NATIVE_SETUP_SERIAL_NTSC_U, "NTSC-U (USA)"},
    {"SCES-02105", "PAL (Europe)"},
    {"SCPS-10118", "NTSC-J (Japan)"},
};

#define NATIVE_SETUP_KNOWN_DISC_COUNT ((int)(sizeof(s_setupKnownDiscs) / sizeof(s_setupKnownDiscs[0])))

struct NativeSetupState
{
	SDL_Window *window;
	SDL_Renderer *renderer;

	// What the user is being told right now. Two lines: what happened, and what
	// to do about it. A message that says only the first is a message that gets
	// posted as a screenshot with "help?" under it.
	char message[NATIVE_SETUP_MESSAGE_MAX];
	char hint[NATIVE_SETUP_MESSAGE_MAX];

	// Set by the file dialog callback, consumed by the loop. The callback fires
	// on the event thread, so it may not do the work itself.
	char pickedPath[NATIVE_SETUP_PATH_MAX];
	int hasPicked;

	int dialogOpen;
	int busy;
	int cancelled;
	int done;

	// An image found beside the game, offered by name rather than used behind
	// the user's back.
	char foundPath[NATIVE_SETUP_PATH_MAX];

	char busyPath[NATIVE_SETUP_PATH_MAX];
	u32 busyFileIndex;
	u32 busyFileCount;
	u64 busyBytesDone;
	u64 busyBytesTotal;
	u64 lastDrawTicks;

	// Opened only for this screen and closed again before
	// returning. The game opens its pads itself later, and two
	// open handles on the same device are exactly the bug that once gave one
	// pad two slots (native_input.c, NativeInput_SlotForInstance).
	SDL_Gamepad *pads[4];
	int padCount;

	// Is something being dragged over the window right now. Only for the frame - but dragging
	// is the main route here, and a target that does not say that it is one
	// makes the user put the file next to it.
	int dragHover;
};

static struct NativeSetupState s_setup;

static void NativeSetup_SetMessage(const char *message, const char *hint)
{
	SDL_strlcpy(s_setup.message, (message != NULL) ? message : "", sizeof(s_setup.message));
	SDL_strlcpy(s_setup.hint, (hint != NULL) ? hint : "", sizeof(s_setup.hint));

	if (s_setup.message[0] != '\0')
	{
		printf("[CTR Setup] %s\n", s_setup.message);
		if (s_setup.hint[0] != '\0')
		{
			printf("[CTR Setup] %s\n", s_setup.hint);
		}
		fflush(stdout);
	}
}

// The marker, and what it is for.
//
// "Only on the first start" needs a definition of first, and "are the files
// there" is not it: a half-finished unpack leaves files there too. So the marker
// is written LAST, after the final byte of the final file, and it is deleted
// FIRST, before the first one is written. Between those two moments there is no
// marker, and a run that dies in the middle - power cut, killed process, full
// disc - comes back to the same screen rather than to a game missing a file it
// will only notice in the third menu.
static int NativeSetup_MarkerPath(char *dst, size_t dstSize)
{
	return NativePath_Join(dst, dstSize, NativeStr8_FromCString(NativeAssets_GetBaseDir()), NativeStr8_FromCString(NATIVE_SETUP_MARKER_PATH));
}

static int NativeSetup_MarkerExists(void)
{
	char path[NATIVE_SETUP_PATH_MAX];
	FILE *file;

	if (!NativeSetup_MarkerPath(path, sizeof(path)))
	{
		return 0;
	}

	file = fopen(path, "rb");
	if (file == NULL)
	{
		return 0;
	}

	fclose(file);
	return 1;
}

static void NativeSetup_MarkerRemove(void)
{
	char path[NATIVE_SETUP_PATH_MAX];

	if (NativeSetup_MarkerPath(path, sizeof(path)))
	{
		remove(path);
	}
}

static int NativeSetup_MarkerWrite(const char *serial, const char *source, u32 files, u64 bytes)
{
	char path[NATIVE_SETUP_PATH_MAX];
	FILE *file;

	if (!NativeSetup_MarkerPath(path, sizeof(path)))
	{
		return 0;
	}

	file = fopen(path, "w");
	if (file == NULL)
	{
		return 0;
	}

	fprintf(file, "%s\n", NATIVE_SETUP_MARKER_MAGIC);
	fprintf(file, "# Deleting this file means the next start asks again.\n");
	fprintf(file, "version 1\n");
	fprintf(file, "serial %s\n", serial);
	fprintf(file, "source %s\n", source);
	fprintf(file, "files %u\n", (unsigned int)files);
	fprintf(file, "bytes %llu\n", (unsigned long long)bytes);

	return (fclose(file) == 0);
}

// THE DRAWING
//
// Everything here goes through SDL_RenderDebugText, the 8x8 font built into SDL.
// That is a decision, not a stopgap: this screen runs BEFORE
// any game data is there, and a font file would be yet another
// file that can be missing. The debug font needs none.
//
// Scaled up it looks like a pixel font, and that is exactly how this
// screen is meant to look: belonging to our game, not like a system dialog.

struct NativeSetupColor
{
	u8 r, g, b;
};

// THE PALETTE OF THIS SCREEN, in ONE place.
//
// It was purple on dark blue. That is the colour world of another game;
// Crash Team Racing is orange, black and warm. What changed are the
// text colours here and the four area colours, which used to stand as bare
// triples in the middle of the drawing - they are here now too, so that the
// question "where are the colours" has one answer and not four.
//
// The title was already orange and stays as it is. Everything else
// follows it: lighter for body text, muted for
// second lines, warm brownish grey for the small print.
static const struct NativeSetupColor s_setupInk = {248, 166, 74};       // body text, light orange
static const struct NativeSetupColor s_setupTitle = {240, 116, 30};     // the title - unchanged
static const struct NativeSetupColor s_setupSub = {198, 138, 86};       // second lines, muted
static const struct NativeSetupColor s_setupDim = {150, 116, 92};       // small print, warm grey
static const struct NativeSetupColor s_setupWarn = {255, 138, 96};      // error message
static const struct NativeSetupColor s_setupGood = {156, 204, 96};      // image found

// The areas. The background is almost black and not quite - a hint of red
// in it takes away the coldness without one seeing a colour.
static const struct NativeSetupColor s_setupBack = {12, 9, 8};          // background
static const struct NativeSetupColor s_setupFrame = {150, 72, 20};      // frame at rest
static const struct NativeSetupColor s_setupBarBack = {48, 26, 12};     // bar background
static const struct NativeSetupColor s_setupErrBack = {58, 24, 20};     // background of the error line

// THE BUILD ON THE SCREEN.
//
// CTR_NATIVE_VERSION comes from CMakeLists.txt and is in the log at start;
// the screen is the only thing somebody else is sure to get to see, so the
// build is named here too.
//
// TWO ITEMS, BECAUSE THEY ANSWER TWO QUESTIONS: the status line under the
// subtitle says how reliable this build is; the build id in the footer
// (CTR_NATIVE_BUILD_ID, generated per build) says WHICH build it is.
//
// The same name everywhere - start screen, --version, Reload Studio, log:
// "CTR Reload <version>" plus the build id. CTR_RELOAD_VERSION is the
// upper-case form of CTR_NATIVE_VERSION for the debug font and is NOT taken
// from the build - it has to be changed together with CMakeLists.txt.
#define CTR_RELOAD_VERSION "0.7.5 BETA"

// One line, horizontally centred, at the largest integer scale up to
// `scale` that still fits into the window. Stepping down is not a luxury: the
// messages below carry file paths, and a path that runs over the edge
// is exactly the line the user needs.
static float NativeSetup_DrawCentered(float y, int scale, struct NativeSetupColor color, const char *text)
{
	int width = 0;
	int height = 0;
	size_t length = SDL_strlen(text);

	SDL_GetRenderOutputSize(s_setup.renderer, &width, &height);

	while ((scale > 1) && (((int)length * NATIVE_SETUP_GLYPH * scale) > (width - 16)))
	{
		scale--;
	}

	{
		const float textWidth = (float)((int)length * NATIVE_SETUP_GLYPH * scale);
		const float x = ((float)width - textWidth) * 0.5f;

		SDL_SetRenderScale(s_setup.renderer, (float)scale, (float)scale);
		SDL_SetRenderDrawColor(s_setup.renderer, color.r, color.g, color.b, 255);
		SDL_RenderDebugText(s_setup.renderer, x / (float)scale, y / (float)scale, text);
		SDL_SetRenderScale(s_setup.renderer, 1.0f, 1.0f);
	}

	return y + (float)(NATIVE_SETUP_GLYPH * scale);
}

// Cuts out of the middle instead of at the end: in a path the beginning and
// the end are both interesting, the middle rarely.
static void NativeSetup_ShortenPath(char *dst, size_t dstSize, const char *path, size_t maxChars)
{
	size_t length = SDL_strlen(path);

	if ((maxChars < 12u) || (length <= maxChars) || (dstSize <= maxChars))
	{
		SDL_strlcpy(dst, path, dstSize);
		return;
	}

	{
		const size_t head = (maxChars - 3u) / 2u;
		const size_t tail = (maxChars - 3u) - head;

		SDL_memcpy(dst, path, head);
		dst[head] = '.';
		dst[head + 1u] = '.';
		dst[head + 2u] = '.';
		SDL_memcpy(&dst[head + 3u], &path[length - tail], tail);
		dst[head + 3u + tail] = '\0';
	}
}

static void NativeSetup_DrawProgress(float y)
{
	int width = 0;
	int height = 0;
	const int percent = (s_setup.busyBytesTotal != 0) ? (int)((s_setup.busyBytesDone * 100u) / s_setup.busyBytesTotal) : 0;
	char line[NATIVE_SETUP_MESSAGE_MAX];
	char shortened[NATIVE_SETUP_PATH_MAX];
	SDL_FRect frame;
	SDL_FRect fill;

	SDL_GetRenderOutputSize(s_setup.renderer, &width, &height);

	y = NativeSetup_DrawCentered(y, NATIVE_SETUP_SCALE_BIG, s_setupInk, "UNPACKING YOUR DISC") + 12.0f;
	y = NativeSetup_DrawCentered(y, NATIVE_SETUP_SCALE_TEXT, s_setupDim, "THIS HAPPENS ONCE") + 34.0f;

	SDL_snprintf(line, sizeof(line), "%d%%", percent);
	y = NativeSetup_DrawCentered(y, NATIVE_SETUP_SCALE_TITLE, s_setupTitle, line) + 24.0f;

	// A drawn bar instead of a row of hash signs. Costs two
	// rectangles and does not look like a text console.
	frame.w = (float)width * 0.7f;
	frame.h = 18.0f;
	frame.x = ((float)width - frame.w) * 0.5f;
	frame.y = y;

	SDL_SetRenderDrawColor(s_setup.renderer, s_setupBarBack.r, s_setupBarBack.g, s_setupBarBack.b, 255);
	SDL_RenderRect(s_setup.renderer, &frame);

	fill = frame;
	fill.x += 3.0f;
	fill.y += 3.0f;
	fill.h -= 6.0f;
	fill.w = (frame.w - 6.0f) * ((float)percent / 100.0f);
	SDL_SetRenderDrawColor(s_setup.renderer, s_setupTitle.r, s_setupTitle.g, s_setupTitle.b, 255);
	SDL_RenderFillRect(s_setup.renderer, &fill);

	y += frame.h + 22.0f;

	SDL_snprintf(line, sizeof(line), "%u OF %u FILES", (unsigned int)s_setup.busyFileIndex, (unsigned int)s_setup.busyFileCount);
	y = NativeSetup_DrawCentered(y, NATIVE_SETUP_SCALE_TEXT, s_setupInk, line) + 12.0f;

	NativeSetup_ShortenPath(shortened, sizeof(shortened), s_setup.busyPath, (size_t)((width - 32) / NATIVE_SETUP_GLYPH));
	y = NativeSetup_DrawCentered(y, NATIVE_SETUP_SCALE_FINE, s_setupDim, shortened) + 30.0f;

	y = NativeSetup_DrawCentered(y, NATIVE_SETUP_SCALE_FINE, s_setupDim, "THE IMAGE IS ONLY READ. IT IS NOT MOVED OR DELETED.") + 22.0f;
	NativeSetup_DrawCentered(y, NATIVE_SETUP_SCALE_TEXT, s_setupSub, "ESC / (B) TO CANCEL");
}

static void NativeSetup_DrawInvite(float y)
{
	int width = 0;
	int height = 0;

	SDL_GetRenderOutputSize(s_setup.renderer, &width, &height);

	y = NativeSetup_DrawCentered(y, NATIVE_SETUP_SCALE_BIG, s_setupInk, "DRAG YOUR CRASH TEAM RACING") + 10.0f;
	y = NativeSetup_DrawCentered(y, NATIVE_SETUP_SCALE_BIG, s_setupInk, "DISC IMAGE (.CUE / .BIN) HERE") + 34.0f;

	y = NativeSetup_DrawCentered(y, NATIVE_SETUP_SCALE_TEXT, s_setupSub, "OR PRESS SPACE / (A) TO BROWSE") + 20.0f;

	// An image lying beside the game is offered, not used. Unpacking
	// writes several hundred megabytes, and an installation that so far
	// ran straight out of the image did not ask for that.
	if (s_setup.foundPath[0] != '\0')
	{
		char shortened[NATIVE_SETUP_PATH_MAX];

		y = NativeSetup_DrawCentered(y, NATIVE_SETUP_SCALE_TEXT, s_setupGood, "OR PRESS ENTER / (X) TO USE THE IMAGE FOUND HERE:") + 8.0f;
		NativeSetup_ShortenPath(shortened, sizeof(shortened), s_setup.foundPath, (size_t)((width - 32) / NATIVE_SETUP_GLYPH));
		y = NativeSetup_DrawCentered(y, NATIVE_SETUP_SCALE_FINE, s_setupGood, shortened) + 20.0f;
	}

	if (s_setup.message[0] != '\0')
	{
		SDL_FRect box;
		char shortened[NATIVE_SETUP_MESSAGE_MAX];

		box.w = (float)width - 80.0f;
		box.h = (s_setup.hint[0] != '\0') ? 62.0f : 40.0f;
		box.x = 40.0f;
		box.y = y - 8.0f;

		SDL_SetRenderDrawColor(s_setup.renderer, s_setupErrBack.r, s_setupErrBack.g, s_setupErrBack.b, 255);
		SDL_RenderFillRect(s_setup.renderer, &box);
		SDL_SetRenderDrawColor(s_setup.renderer, s_setupWarn.r, s_setupWarn.g, s_setupWarn.b, 255);
		SDL_RenderRect(s_setup.renderer, &box);

		y += 4.0f;
		NativeSetup_ShortenPath(shortened, sizeof(shortened), s_setup.message, (size_t)((width - 96) / NATIVE_SETUP_GLYPH));
		y = NativeSetup_DrawCentered(y, NATIVE_SETUP_SCALE_TEXT, s_setupWarn, shortened) + 6.0f;

		if (s_setup.hint[0] != '\0')
		{
			NativeSetup_ShortenPath(shortened, sizeof(shortened), s_setup.hint, (size_t)((width - 96) / NATIVE_SETUP_GLYPH));
			y = NativeSetup_DrawCentered(y, NATIVE_SETUP_SCALE_FINE, s_setupInk, shortened);
		}

		y += 24.0f;
	}

	y = NativeSetup_DrawCentered(y, NATIVE_SETUP_SCALE_FINE, s_setupDim, "YOUR OWN DISC IS REQUIRED. NOTHING IS DOWNLOADED.") + 8.0f;
	NativeSetup_DrawCentered(y, NATIVE_SETUP_SCALE_FINE, s_setupDim, "NTSC-U ONLY, SERIAL " NATIVE_SETUP_SERIAL_NTSC_U ".");
}

static void NativeSetup_Draw(void)
{
	int width = 0;
	int height = 0;
	float y;

	if (s_setup.renderer == NULL)
	{
		return;
	}

	SDL_GetRenderOutputSize(s_setup.renderer, &width, &height);

	SDL_SetRenderDrawColor(s_setup.renderer, s_setupBack.r, s_setupBack.g, s_setupBack.b, 255);
	SDL_RenderClear(s_setup.renderer);

	// The frame says that this window is a target. Dashed and pale
	// while nothing is over it; solid and orange as soon as something comes.
	{
		SDL_FRect edge;
		int i;
		const int thickness = s_setup.dragHover ? NATIVE_SETUP_FRAME_THICK : 1;

		if (s_setup.dragHover)
		{
			SDL_SetRenderDrawColor(s_setup.renderer, s_setupTitle.r, s_setupTitle.g, s_setupTitle.b, 255);
		}
		else
		{
			SDL_SetRenderDrawColor(s_setup.renderer, s_setupFrame.r, s_setupFrame.g, s_setupFrame.b, 255);
		}

		for (i = 0; i < thickness; i++)
		{
			edge.x = (float)NATIVE_SETUP_FRAME_INSET + (float)i;
			edge.y = (float)NATIVE_SETUP_FRAME_INSET + (float)i;
			edge.w = (float)width - (float)(2 * NATIVE_SETUP_FRAME_INSET) - (float)(2 * i);
			edge.h = (float)height - (float)(2 * NATIVE_SETUP_FRAME_INSET) - (float)(2 * i);
			SDL_RenderRect(s_setup.renderer, &edge);
		}
	}

	y = 54.0f;
	y = NativeSetup_DrawCentered(y, NATIVE_SETUP_SCALE_TITLE, s_setupTitle, "CTR RELOAD") + 12.0f;
	y = NativeSetup_DrawCentered(y, NATIVE_SETUP_SCALE_SUB, s_setupSub, "CRASH TEAM RACING PC REBUILD") + 10.0f;

	// The build status directly under the subtitle and not in the footer: the footer
	// holds four legal sentences, and a warning between them is read by
	// nobody. Here it stands where the eye looks after the name anyway.
	y = NativeSetup_DrawCentered(y, NATIVE_SETUP_SCALE_FINE, s_setupWarn, CTR_RELOAD_VERSION) + 52.0f;

	if (s_setup.busy)
	{
		NativeSetup_DrawProgress(y);
	}
	else
	{
		NativeSetup_DrawInvite(y);
	}

	// THE FOOTER. Four statements, and none of them is decoration: what this
	// builds on, that nobody at the rights holder has anything to do with it, under
	// which licence it is, and that nothing comes from the net. Therefore it stands
	// on EVERY state of this screen, also while
	// unpacking - it is the only screen some people ever see.
	{
		const float footer = (float)height - (float)NATIVE_SETUP_FOOTER_TOP;
		float fy = footer;

		// While unpacking, the cancel line is already at the top, right by the
		// progress - that is where someone who changes their mind looks for it.
		if (!s_setup.busy)
		{
			(void)NativeSetup_DrawCentered(footer - 30.0f, NATIVE_SETUP_SCALE_TEXT, s_setupSub, "ESC / (B) TO QUIT");
		}

		// Which build this is, small and at the bottom. CTR_NATIVE_BUILD_ID is
		// generated per build (cmake/CtrBuildId.cmake) - not written here by hand.
		fy = NativeSetup_DrawCentered(fy, NATIVE_SETUP_SCALE_FINE, s_setupDim, "BUILD " CTR_NATIVE_BUILD_ID) +
		     (float)NATIVE_SETUP_FOOTER_LEAD;

		fy = NativeSetup_DrawCentered(fy, NATIVE_SETUP_SCALE_FINE, s_setupDim,
		                              "BASED ON CTR-NATIVE. NOT AFFILIATED WITH OR ENDORSED BY ACTIVISION OR NAUGHTY DOG.") +
		     (float)NATIVE_SETUP_FOOTER_LEAD;
		NativeSetup_DrawCentered(fy, NATIVE_SETUP_SCALE_FINE, s_setupDim,
		                         "LICENSED UNDER GPLV3. NOTHING IS DOWNLOADED - YOUR OWN DISC IS READ WHERE IT LIES.");
	}

	SDL_RenderPresent(s_setup.renderer);
	s_setup.lastDrawTicks = SDL_GetTicks();
}

// Called once per file, from inside the extraction walk. It pumps events so the
// window keeps answering the system, and it is where a closed window turns into
// a cancelled unpack - between two files, never inside one.
static int NativeSetup_Progress(void *user, const char *path, u32 fileIndex, u32 fileCount, u64 bytesDone, u64 bytesTotal)
{
	SDL_Event event;

	(void)user;

	s_setup.busyFileIndex = fileIndex;
	s_setup.busyFileCount = fileCount;
	s_setup.busyBytesDone = bytesDone;
	s_setup.busyBytesTotal = bytesTotal;
	SDL_strlcpy(s_setup.busyPath, (path != NULL) ? path : "", sizeof(s_setup.busyPath));

	// Cancelling works here with the same keys as on the start screen. Before,
	// only the window's close button was wired, while the screen next to it
	// offered "ESC / (B) TO QUIT" - a key that stops working halfway
	// through is worse than none at all.
	//
	// The cancel ALWAYS arrives between two files, never in the middle of one:
	// this is the progress call, and it is called between the files.
	// What has already been written stays, but the marker is not
	// set - half unpacked never counts as finished.
	while (SDL_PollEvent(&event))
	{
		if ((event.type == SDL_EVENT_QUIT) || (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED))
		{
			s_setup.cancelled = 1;
			return 0;
		}

		if ((event.type == SDL_EVENT_KEY_DOWN) && (event.key.key == SDLK_ESCAPE))
		{
			s_setup.cancelled = 1;
			return 0;
		}

		if ((event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) && (event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST))
		{
			s_setup.cancelled = 1;
			return 0;
		}
	}

	// Throttled, because redrawing per file would spend more time on the screen
	// than on the disc for the several hundred small files this disc carries.
	if ((SDL_GetTicks() - s_setup.lastDrawTicks) >= NATIVE_SETUP_REDRAW_MS)
	{
		NativeSetup_Draw();
	}

	return 1;
}

static void SDLCALL NativeSetup_DialogCallback(void *userdata, const char *const *filelist, int filter)
{
	(void)userdata;
	(void)filter;

	s_setup.dialogOpen = 0;

	if ((filelist == NULL) || (filelist[0] == NULL))
	{
		// NULL list is an error from the platform, an empty one is the user
		// pressing cancel. Neither is worth a message: they already know.
		return;
	}

	SDL_strlcpy(s_setup.pickedPath, filelist[0], sizeof(s_setup.pickedPath));
	s_setup.hasPicked = 1;

	// The setup loop sleeps in SDL_WaitEvent. The dialog answers from its own
	// thread, so without an event of its own the pick would wait for the next
	// mouse move or window event. SDL_PushEvent is safe from any thread.
	{
		SDL_Event wake;

		SDL_zero(wake);
		wake.type = SDL_EVENT_USER;
		SDL_PushEvent(&wake);
	}
}

static const char *NativeSetup_RegionForSerial(const char *serial)
{
	int i;

	for (i = 0; i < NATIVE_SETUP_KNOWN_DISC_COUNT; i++)
	{
		if (SDL_strcmp(serial, s_setupKnownDiscs[i].serial) == 0)
		{
			return s_setupKnownDiscs[i].region;
		}
	}

	return NULL;
}

// ONE IMAGE, ALL THE WAYS IT CAN GO WRONG.
//
// Every branch here ends in a message that names the case and says what to do.
// The order is the order in which the answers become knowable: a file that will
// not open cannot be asked for its serial, and a disc of the wrong game cannot
// be asked how much space it needs.
static int NativeSetup_TryImage(const char *path)
{
	char serial[32];
	char failedPath[NATIVE_SETUP_PATH_MAX];
	char cueImage[NATIVE_SETUP_PATH_MAX];
	char cueName[256];
	const char *region;
	u32 files = 0;
	u64 bytes = 0;
	int result = NATIVE_DISC_IMAGE_OK;
	int fromCue = 0;

	NativeSetup_SetMessage("", "");

	// A .cue is read for the image it names; from there on everything is the
	// same as for an image handed over directly.
	if (NativeDiscImage_IsCuePath(path))
	{
		switch (NativeDiscImage_CueImagePath(path, cueImage, sizeof(cueImage), cueName, sizeof(cueName)))
		{
		case NATIVE_DISC_IMAGE_CUE_OK:
			printf("[CTR Setup] %s names the image %s\n", path, cueImage);
			fflush(stdout);
			path = cueImage;
			fromCue = 1;
			break;

		case NATIVE_DISC_IMAGE_CUE_UNREADABLE:
			NativeSetup_SetMessage("That .cue file cannot be read.", "Check that it still exists and that no other program holds it open.");
			return 0;

		case NATIVE_DISC_IMAGE_CUE_NO_FILE:
			NativeSetup_SetMessage("That .cue file names no disc image: it has no FILE line.",
			                       "Drag the .bin itself onto this window, or use a complete .cue.");
			return 0;

		case NATIVE_DISC_IMAGE_CUE_MISSING:
			SDL_snprintf(s_setup.message, sizeof(s_setup.message), "The .cue names \"%s\", but that file is not next to it.", cueName);
			SDL_snprintf(s_setup.hint, sizeof(s_setup.hint), "Keep the .cue and its .bin in one folder, under the name the .cue gives.");
			NativeSetup_SetMessage(s_setup.message, s_setup.hint);
			return 0;

		default:
			NativeSetup_SetMessage("The path of the image that the .cue names is too long.", "Move the .cue and its .bin to a folder with a shorter path.");
			return 0;
		}
	}

	if (!NativeDiscImage_OpenImagePath(path))
	{
		if (fromCue)
		{
			SDL_snprintf(s_setup.message, sizeof(s_setup.message), "The .cue names \"%s\", but that is not a readable PlayStation disc image.",
			             cueName);
			SDL_snprintf(s_setup.hint, sizeof(s_setup.hint), "CTR Reload reads BIN tracks in MODE2/2352 form.");
			NativeSetup_SetMessage(s_setup.message, s_setup.hint);
			return 0;
		}

		NativeSetup_SetMessage("That file is not a readable PlayStation disc image.",
		                       "CTR Reload reads BIN/CUE and ISO tracks in MODE2/2352 form.");
		return 0;
	}

	if (!NativeDiscImage_ReadBootSerial(serial, sizeof(serial)))
	{
		// The root read fine or the open would have failed, so the disc is a
		// disc - it just has no boot record where one belongs. Truncated rips
		// land here far more often than genuinely foreign discs do.
		NativeSetup_SetMessage("The image has no boot record - it looks damaged or incomplete.",
		                       "Re-dump or re-download it; a partial file is the usual cause.");
		return 0;
	}

	if (SDL_strcmp(serial, NATIVE_SETUP_SERIAL_NTSC_U) != 0)
	{
		region = NativeSetup_RegionForSerial(serial);

		if (region != NULL)
		{
			SDL_snprintf(s_setup.message, sizeof(s_setup.message), "That is Crash Team Racing %s, serial %s.", region, serial);
			SDL_snprintf(s_setup.hint, sizeof(s_setup.hint), "CTR Reload needs NTSC-U, serial %s. Other regions come later.", NATIVE_SETUP_SERIAL_NTSC_U);
		}
		else
		{
			SDL_snprintf(s_setup.message, sizeof(s_setup.message), "That is a PlayStation disc, but not this game: serial %s.", serial);
			SDL_snprintf(s_setup.hint, sizeof(s_setup.hint), "Needed is Crash Team Racing NTSC-U, serial %s.", NATIVE_SETUP_SERIAL_NTSC_U);
		}

		NativeSetup_SetMessage(s_setup.message, s_setup.hint);
		return 0;
	}

	if (!NativeDiscImage_Measure(&files, &bytes) || (files == 0))
	{
		NativeSetup_SetMessage("The image is the right game but its file table cannot be read.",
		                       "The dump is damaged. Re-dump or re-download it.");
		return 0;
	}

	// The marker goes away BEFORE the first byte is written, so there is no
	// window in which a half-unpacked directory counts as finished.
	NativeSetup_MarkerRemove();

	s_setup.busy = 1;
	s_setup.busyFileCount = files;
	s_setup.busyBytesTotal = bytes;
	s_setup.busyBytesDone = 0;
	s_setup.busyFileIndex = 0;
	s_setup.busyPath[0] = '\0';
	s_setup.lastDrawTicks = 0;
	NativeSetup_Draw();

	if (!NativeDiscImage_Extract(NativeAssets_GetAssetDir(), NativeSetup_Progress, NULL, &result, failedPath, sizeof(failedPath), &files, &bytes))
	{
		s_setup.busy = 0;

		switch (result)
		{
		case NATIVE_DISC_IMAGE_ERR_CREATE:
			SDL_snprintf(s_setup.message, sizeof(s_setup.message), "Cannot write into the game folder (stopped at %s).", failedPath);
			SDL_snprintf(s_setup.hint, sizeof(s_setup.hint), "Move CTR Reload out of Program Files, or run it from a folder you own.");
			break;

		case NATIVE_DISC_IMAGE_ERR_WRITE:
			SDL_snprintf(s_setup.message, sizeof(s_setup.message), "A write failed at %s.", failedPath);
			SDL_snprintf(s_setup.hint, sizeof(s_setup.hint), "The disc is most likely full - about %llu MB are needed.",
			             (unsigned long long)(s_setup.busyBytesTotal / (1024u * 1024u)));
			break;

		case NATIVE_DISC_IMAGE_ERR_CANCELLED:
			SDL_snprintf(s_setup.message, sizeof(s_setup.message), "Unpacking was cancelled. Nothing is finished.");
			SDL_snprintf(s_setup.hint, sizeof(s_setup.hint), "Start again - it picks up from the beginning, which is safe.");
			break;

		case NATIVE_DISC_IMAGE_ERR_MEMORY:
			SDL_snprintf(s_setup.message, sizeof(s_setup.message), "Out of memory while unpacking.");
			SDL_snprintf(s_setup.hint, sizeof(s_setup.hint), "Close other programs and try again.");
			break;

		default:
			SDL_snprintf(s_setup.message, sizeof(s_setup.message), "The image stopped being readable at %s.", failedPath);
			SDL_snprintf(s_setup.hint, sizeof(s_setup.hint), "The dump is damaged or incomplete. Re-dump or re-download it.");
			break;
		}

		NativeSetup_SetMessage(s_setup.message, s_setup.hint);
		return 0;
	}

	s_setup.busy = 0;

	if (!NativeSetup_MarkerWrite(serial, NativeDiscImage_GetPath(), files, bytes))
	{
		NativeSetup_SetMessage("Everything unpacked, but the note saying so could not be written.",
		                       "Check that the game folder is writable, then start again.");
		return 0;
	}

	printf("[CTR Setup] %u files, %llu bytes from %s\n", (unsigned int)files, (unsigned long long)bytes, NativeDiscImage_GetPath());
	fflush(stdout);
	return 1;
}

// The file dialog, needed in two places: space bar and controller button A.
static void NativeSetup_Browse(void)
{
	// One filter that names the forms this reader accepts, and one
	// that does not filter at all - whoever named their dump .img should not
	// have to rename it first to find out that it works.
	static const SDL_DialogFileFilter filters[] = {
	    {"Disc image", "cue;bin;img;iso"},
	    {"All files", "*"},
	};

	if (s_setup.dialogOpen)
	{
		return;
	}

	s_setup.dialogOpen = 1;
	SDL_ShowOpenFileDialog(NativeSetup_DialogCallback, NULL, s_setup.window, filters, 2, NULL, false);
}

static void NativeSetup_Pick(const char *path)
{
	SDL_strlcpy(s_setup.pickedPath, path, sizeof(s_setup.pickedPath));
	s_setup.hasPicked = 1;
}

// Pads only for this screen. SDL delivers button events exclusively
// for opened gamepads, so they have to be opened here - and closed again
// before returning. The game opens its own in Platform_InputInit, and two
// handles on the same device are the bug that once gave one pad two slots
// (native_input.c, NativeInput_SlotForInstance).
static void NativeSetup_OpenPads(void)
{
	int count = 0;
	SDL_JoystickID *ids;
	int i;

	if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
	{
		// No reason to abort - keyboard and dragging still work.
		return;
	}

	ids = SDL_GetGamepads(&count);
	if (ids == NULL)
	{
		return;
	}

	for (i = 0; (i < count) && (s_setup.padCount < (int)SDL_arraysize(s_setup.pads)); i++)
	{
		SDL_Gamepad *pad = SDL_OpenGamepad(ids[i]);

		if (pad != NULL)
		{
			s_setup.pads[s_setup.padCount++] = pad;
		}
	}

	SDL_free(ids);
}

static void NativeSetup_ClosePads(void)
{
	int i;

	for (i = 0; i < s_setup.padCount; i++)
	{
		if (s_setup.pads[i] != NULL)
		{
			SDL_CloseGamepad(s_setup.pads[i]);
			s_setup.pads[i] = NULL;
		}
	}

	s_setup.padCount = 0;
	SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

static int NativeSetup_RunWindow(void)
{
	SDL_Event event;

	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		fprintf(stderr, "[CTR Setup] SDL video init failed: %s\n", SDL_GetError());
		return 0;
	}

	// MUST come before SDL_CreateWindow. SDL registers the Windows drop target
	// when the window is created and asks at that point whether DROP_FILE is
	// switched on at all. Switched on afterwards would be too late, and the window
	// would silently accept nothing - on a screen whose main route is dragging,
	// that would be the whole screen broken.
	SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, true);

	s_setup.window = SDL_CreateWindow("CTR Reload", NATIVE_SETUP_WINDOW_W, NATIVE_SETUP_WINDOW_H, 0);
	if (s_setup.window == NULL)
	{
		fprintf(stderr, "[CTR Setup] cannot open the setup window: %s\n", SDL_GetError());
		fprintf(stderr, "[CTR Setup] Unpack from the command line instead: ctr_native --image <path to your disc image>\n");
		return 0;
	}

	// The software renderer first, and that is on purpose.
	//
	// This screen runs before anything is set up - it is the
	// screen for the machine on which nothing works yet. A
	// graphics card path that fails here takes away from the user the only place
	// at which they would be told what to do. And it would find
	// nothing to do here anyway: static text, thirty times a second.
	//
	// On top of that the game brings up Vulkan right afterwards. Not opening a
	// D3D device before that and throwing it away again is one worry less.
	s_setup.renderer = SDL_CreateRenderer(s_setup.window, "software");
	if (s_setup.renderer == NULL)
	{
		s_setup.renderer = SDL_CreateRenderer(s_setup.window, NULL);
	}

	if (s_setup.renderer == NULL)
	{
		fprintf(stderr, "[CTR Setup] cannot draw the setup window: %s\n", SDL_GetError());
		fprintf(stderr, "[CTR Setup] Unpack from the command line instead: ctr_native --image <path to your disc image>\n");
		SDL_DestroyWindow(s_setup.window);
		s_setup.window = NULL;
		return 0;
	}

	NativeSetup_OpenPads();
	NativeSetup_Draw();

	while (!s_setup.done)
	{
		if (!SDL_WaitEvent(&event))
		{
			break;
		}

		do
		{
			switch (event.type)
			{
			case SDL_EVENT_QUIT:
			case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
				s_setup.cancelled = 1;
				s_setup.done = 1;
				break;

			// A pad that is only plugged in now should also be able to
			// operate the screen - otherwise the screen depends on the order in which
			// someone plugs in their cables.
			case SDL_EVENT_GAMEPAD_ADDED:
				if (s_setup.padCount < (int)SDL_arraysize(s_setup.pads))
				{
					SDL_Gamepad *pad = SDL_OpenGamepad(event.gdevice.which);

					if (pad != NULL)
					{
						s_setup.pads[s_setup.padCount++] = pad;
					}
				}
				break;

			case SDL_EVENT_DROP_BEGIN:
				s_setup.dragHover = 1;
				break;

			case SDL_EVENT_DROP_COMPLETE:
				s_setup.dragHover = 0;
				break;

			// No keypress needed and no confirmation: released means
			// taken. That is the main route of this screen.
			case SDL_EVENT_DROP_FILE:
				s_setup.dragHover = 0;
				if (event.drop.data != NULL)
				{
					NativeSetup_Pick(event.drop.data);
				}
				break;

			case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
				if (event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH)
				{
					NativeSetup_Browse();
				}
				else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST)
				{
					s_setup.cancelled = 1;
					s_setup.done = 1;
				}
				else if ((event.gbutton.button == SDL_GAMEPAD_BUTTON_WEST) && (s_setup.foundPath[0] != '\0'))
				{
					NativeSetup_Pick(s_setup.foundPath);
				}
				break;

			case SDL_EVENT_KEY_DOWN:
				if ((event.key.key == SDLK_RETURN) && (s_setup.foundPath[0] != '\0'))
				{
					NativeSetup_Pick(s_setup.foundPath);
				}
				else if ((event.key.key == SDLK_SPACE) || (event.key.key == SDLK_RETURN))
				{
					NativeSetup_Browse();
				}
				else if (event.key.key == SDLK_ESCAPE)
				{
					s_setup.cancelled = 1;
					s_setup.done = 1;
				}
				break;

			default:
				break;
			}
		} while (!s_setup.done && SDL_PollEvent(&event));

		if (s_setup.hasPicked && !s_setup.done)
		{
			s_setup.hasPicked = 0;

			if (NativeSetup_TryImage(s_setup.pickedPath))
			{
				s_setup.done = 1;
			}
			else if (s_setup.cancelled)
			{
				s_setup.done = 1;
			}
		}

		if (!s_setup.done)
		{
			NativeSetup_Draw();
		}
	}

	NativeSetup_ClosePads();

	if (s_setup.renderer != NULL)
	{
		SDL_DestroyRenderer(s_setup.renderer);
		s_setup.renderer = NULL;
	}
	if (s_setup.window != NULL)
	{
		SDL_DestroyWindow(s_setup.window);
		s_setup.window = NULL;
	}

	return !s_setup.cancelled;
}

//----------------------------------------------------------------------------------------
// THE FOLDERS FOR OWN CONTENT
//
// A decision, not a detail: what track authors get used to cannot be changed
// later without breaking every package already in circulation. The short
// version of the reasoning is below, because the next person to want a second
// folder will read this.
//
// ONE FOLDER PER KIND OF FILE, AND THE COUNT IS THE POINT.
//
//   tracks/       every .rldtrack, flat, next to track-ids.tsv and cups.txt
//   characters/   every .rldchar, flat
//
// The kind of file decides the folder, nothing else: a track and a character
// are read by different code, at different times, under different rules, and
// neither stands in for the other. characters/ was struck once, with
// container format 4.1, while .rldchar had no specification yet - a README
// promising a format nobody was building promised too much. Now the format is
// specified (docs/CONTAINER_FORMAT.md), Reload Studio writes it and the game
// reads the folder at every start (NativeChar_LoadRoster,
// platform/native_chars.c), so the folder is created again.
//
// NOT one folder per game mode. "Time Trial" and "Adventure" are modes a track
// is PLAYED in, not kinds of file: the same container is a race, a time trial
// and an adventure round, and a folder per mode would mean either the same file
// copied four times or an author guessing which single folder their track
// "belongs" in. Both break the one promise the container makes - put the file in
// the folder, that is all.
//
// NOT a folder for challenges either: a Crystal Challenge is the same LEV and the
// same VRM with different rules. Its crystals live in the LEV, and whether a
// track offers the mode is a bit in META (CHAL, the chunk once reserved for
// this, was struck with format 4.1). A "Custom Challenges" folder would need
// either a second copy of the track or a second format.
//
// NOT a folder for ghosts or saves. Those already have homes - memcards/ and
// debug/states/ - and a second home for one fact is the shape of bug this tree
// keeps finding.
//
// Created at every start rather than only while unpacking. Unpacking happens
// once and may never happen at all on a copy that reads straight out of the
// image, and a folder that only exists on some installs is a folder authors
// cannot be told about. One mkdir call that almost always fails with EEXIST.

#define NATIVE_CONTENT_TRACKS_DIR "tracks"

struct NativeContentFolder
{
	const char *name;
	const char *readme;
};

static const struct NativeContentFolder s_contentFolders[] = {
    {NATIVE_CONTENT_TRACKS_DIR,
     "Put .rldtrack files here. One file per track, nothing to install.\n"
     "\n"
     "They are read where they lie - no patching, no unpacking, no ISO surgery.\n"
     "Remove a file and the track is gone again.\n"
     "\n"
     "The file name is how the game knows a track: its level id\n"
     "(track-ids.tsv) and cups.txt use it. Renaming a file makes it a new\n"
     "track. The track's name and its author live inside the container. They\n"
     "are what the author wrote there - nothing proves them.\n"},
    {NATIVE_CHAR_DIR_NAME,
     "Put .rldchar files here. One file per character, nothing to install.\n"
     "\n"
     "Make them with Reload Studio (the Character page). They are read once\n"
     "at start, where they lie. Remove a file and the character is gone again.\n"
     "\n"
     "A character gets a tile in the one-player arcade driver select, after\n"
     "the retail drivers. A broken file is skipped - the game starts anyway,\n"
     "and the log says why (the lines starting with [CTR Char]).\n"
     "\n"
     "The file name is how the game knows a character; the tiles are sorted\n"
     "by it, and at most 32 characters get one. Renaming a file makes it a\n"
     "new character. The name shown in the menu lives inside the file.\n"},
};

#define NATIVE_CONTENT_FOLDER_COUNT ((int)(sizeof(s_contentFolders) / sizeof(s_contentFolders[0])))

static void NativeContent_EnsureFolders(void)
{
	char path[1024];
	char readmePath[1024];
	int i;

	for (i = 0; i < NATIVE_CONTENT_FOLDER_COUNT; i++)
	{
		FILE *file;

		if (!NativePath_Join(path, sizeof(path), NativeStr8_FromCString(NativeAssets_GetBaseDir()), NativeStr8_FromCString(s_contentFolders[i].name)))
		{
			continue;
		}

		if (!NativeDiscImage_EnsureDirectory(path))
		{
			// Not fatal. A read-only install can still play the retail tracks,
			// and refusing to start over a folder nobody has used yet would be
			// the wrong trade.
			Platform_LogWarn("[CTR Content] cannot create %s\n", path);
			continue;
		}

		if (!NativePath_Join(readmePath, sizeof(readmePath), NativeStr8_FromCString(path), NATIVE_STR8_LIT("README.txt")))
		{
			continue;
		}

		// Written once. Rewriting it every start would stamp on a note somebody
		// left themselves in a folder they own.
		file = fopen(readmePath, "rb");
		if (file != NULL)
		{
			fclose(file);
			continue;
		}

		file = fopen(readmePath, "w");
		if (file != NULL)
		{
			fputs(s_contentFolders[i].readme, file);
			fclose(file);
		}
	}
}

// IS THERE ANYTHING TO PLAY, ASKED QUIETLY.
//
// The marker first, because it is one fopen and it answers for every start after
// a successful unpack. If it is not there the four files the validator insists
// on are looked for directly - and "found" includes finding them inside a disc
// image sitting beside the game, because that is a copy that runs.
//
// THAT LAST PART IS THE WHOLE RULE: the screen appears when the game CANNOT RUN,
// not when some particular layout is missing. A tree that has always read
// straight out of ctr-u.bin keeps doing exactly that and is never asked anything
// - unpacking is an offer, not a toll.
static int NativeSetup_DataPresent(void)
{
	static const char *const required[] = {
	    "BIGFILE.BIG",
	    "SOUNDS/KART.HWL",
	    "TEST.STR",
	    "XA/ENG.XNF",
	};
	char resolved[NATIVE_SETUP_PATH_MAX];
	struct NativeDiscImageFile discFile;
	int i;

	if (NativeSetup_MarkerExists())
	{
		return 1;
	}

	for (i = 0; i < (int)(sizeof(required) / sizeof(required[0])); i++)
	{
		if (!NativeAssets_ResolvePath(required[i], resolved, sizeof(resolved)) && !NativeDiscImage_FindFile(required[i], &discFile))
		{
			return 0;
		}
	}

	return 1;
}

// THE WHOLE DECISION, IN ONE PLACE.
//
// Called before the asset validator, because a first start has nothing for the
// validator to look at. Returns 1 when the game may go on.
//
// --setup forces the screen even when there is data. That is the switch: the
// first start can be repeated without deleting anything, which is the only way
// to test it more than once.
//
// --image <path> unpacks without asking. It is what a scripted run needs, and
// the route the setup window names when it cannot open.
static int NativeSetup_EnsureData(int argc, char *argv[])
{
	const char *imageArg = NULL;
	int forced = 0;
	int i;

	memset(&s_setup, 0, sizeof(s_setup));

	for (i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "--setup") == 0)
		{
			forced = 1;
		}
		else if ((strcmp(argv[i], "--image") == 0) && ((i + 1) < argc))
		{
			imageArg = argv[++i];
		}
	}

	if (imageArg != NULL)
	{
		printf("[CTR Setup] unpacking from %s\n", imageArg);
		fflush(stdout);
		return NativeSetup_TryImage(imageArg);
	}

	if (!forced && NativeSetup_DataPresent())
	{
		// The normal case, every start after the first: nothing shown, nothing
		// read, nothing asked.
		return 1;
	}

	// An image already lying beside the game is offered by name. It is still one
	// keypress away rather than automatic: unpacking writes several hundred
	// megabytes, and a copy that has always run straight out of the image did
	// not ask for that to happen behind its back.
	if (NativeDiscImage_IsAvailable())
	{
		SDL_strlcpy(s_setup.foundPath, NativeDiscImage_GetPath(), sizeof(s_setup.foundPath));
	}

	return NativeSetup_RunWindow();
}

int main(int argc, char *argv[])
{
	// First, before any switch has an effect: developer switches only with --dev.
	{
		const int gate = NativeArgs_GateDevSwitches(argc, argv);

		if (gate != 0)
		{
			return NativeConsole_Return((u32)gate);
		}
	}

	for (int argIndex = 1; argIndex < argc; argIndex++)
	{
		if (NativeArg_IsVersion(argv[argIndex]))
		{
			printf("CTR Reload %s (%s)\n", CTR_NATIVE_VERSION, CTR_NATIVE_BUILD_ID);
			return 0;
		}

		if (strcmp(argv[argIndex], "--help") == 0)
		{
			if (g_cfg_dev)
			{
				NativeArgs_PrintDevHelp();
			}
			else
			{
				NativeArgs_PrintHelp();
			}

			return 0;
		}

		// The unpacker against made-up images, see NativeDiscImage_SelfTestDir.
		// Up here for the same reason as --gte-selftest below: no window, no
		// assets folder, no disc image, no graphics card.
		if ((strcmp(argv[argIndex], "--selftest-disc") == 0) && ((argIndex + 1) < argc))
		{
			return NativeDiscImage_SelfTestDir(argv[argIndex + 1]);
		}

		// Runs both GTE paths against each other and leaves again. Up here, because
		// it needs nothing from the game: it computes on gteRegs and nothing else,
		// and a self-test that only runs after the window needs data,
		// a graphics card and luck.
		if (strcmp(argv[argIndex], "--gte-selftest") == 0)
		{
			return NativeGteCheck_Run();
		}

		// The bytes of the native probe mesh against their hash
		// (platform/native_probe.c, ctest native_probe_selftest). Up here for the
		// same reason: it only hashes a table in the exe, no window, no data.
		if (strcmp(argv[argIndex], "--native-probe-selftest") == 0)
		{
			return NativeProbe_SelfTest();
		}

		// The depth of a native draw across view z 0x1000, through the queue's
		// own matrix steps and the render layer (platform/native_depth_check.c,
		// ctest native_depth_selftest). Up here for the same reason: it computes
		// on one made-up instance, no window, no data.
		if (strcmp(argv[argIndex], "--native-depth-selftest") == 0)
		{
			return NativeDepthCheck_Run();
		}

		// The native texture manager without a device (platform/native_tex.c,
		// ctest native_tex_selftest): tables, the shader round trip, the edge
		// rule and the levels of a fixed image against their golden. Up here for
		// the same reason: it computes on tables in the exe, no window, no data.
		if (strcmp(argv[argIndex], "--native-tex-selftest") == 0)
		{
			return NativeTex_SelfTest();
		}

		// The driver select grid (game/230/MM_NativeCharGrid.c) for every count of
		// custom entries: layout, navigation and scrolling against the D230
		// tables. Up here for the same reason: it computes on tables that are in
		// the exe and needs no window, data or graphics card (ctest
		// char_grid_selftest).
		if (strcmp(argv[argIndex], "--char-grid-selftest") == 0)
		{
			return MM_NativeCharGrid_SelfTest();
		}

		// The native part of custom characters (CNET, CTXT), up here for the
		// same reason: it only reads the files rldpack wrote into a build
		// folder (ctest char_native_selftest, platform/native_chars.c).
		if ((strcmp(argv[argIndex], "--char-native-selftest") == 0) && ((argIndex + 1) < argc))
		{
			return NativeChar_NativeSelfTest(argv[argIndex + 1]);
		}

		// The GPU set of a custom character without a device (step 4c,
		// platform/native_char_gpu.c, ctest native_char_gpu_selftest): the
		// same files, through the native read and the set build. Up here for
		// the same reason: it only reads files.
		if ((strcmp(argv[argIndex], "--native-char-gpu-selftest") == 0) && ((argIndex + 1) < argc))
		{
			return NativeCharGpu_SelfTest(argv[argIndex + 1]);
		}

		// The retail twin without a device (step 4d, platform/native_twin.c,
		// ctest native_twin_selftest). Up here for the same reason: it only
		// reads files and memory it makes itself.
		if ((strcmp(argv[argIndex], "--native-twin-selftest") == 0) && ((argIndex + 1) < argc))
		{
			return NativeTwin_SelfTest(argv[argIndex + 1]);
		}

		// The container self-test, up here for the same reason: it only reads
		// files - no platform, window, audio or asset folder, no disc image,
		// no GPU - so it runs on CI (ctest selftest_bad_containers).
		if ((strcmp(argv[argIndex], "--selftest-containers") == 0) && ((argIndex + 1) < argc))
		{
			return NativeTrack_SelfTestFolder(argv[argIndex + 1]);
		}

		// The made-up input of the two self-tests above (platform/native_testfiles.c),
		// written by the game itself and not by a small tool of its own: a
		// reputation-based scanner quarantined such a tool on its first start. Up
		// here for the same reason: no window, no assets folder, no GPU.
		if ((strcmp(argv[argIndex], "--make-test-containers") == 0) && ((argIndex + 1) < argc))
		{
			return NativeTestFiles_MakeContainers(argv[argIndex + 1]);
		}

		if ((strcmp(argv[argIndex], "--make-test-disc") == 0) && ((argIndex + 1) < argc))
		{
			return NativeTestFiles_MakeDisc(argv[argIndex + 1]);
		}

		// Which GTE computes. Up here and not in the big loop
		// further down, because that only runs after Platform_Init - a switch
		// about the arithmetic has to apply from the FIRST operation, not from the
		// first one after start-up.
		if (strcmp(argv[argIndex], "--gte-alt") == 0)
		{
			g_cfg_gteAlt = 1;
			printf("[CTR GTE] second path active (--gte-alt)\n");
		}

		// The division route that does not saturate. Separate from --gte-alt, so that
		// the second path can still be run as a mere zero point;
		// it only acts together with it, because it lives in its file.
		// Up here for the same reason as --gte-alt.
		if (strcmp(argv[argIndex], "--gte-near-div") == 0)
		{
			NativeGteNearDiv_Arm();
		}

		// The near plane of the track renderer. --near-plane N sets it to the
		// view-space depth N; without a number or with 0 each of the two
		// places stays at its own expression. Up here, because the first
		// frame drawn already needs it. Without --gte-near-div it is
		// a bad idea, and the header of game/native_view.c says so
		// too - nothing is forced here anyway: whoever wants to run both
		// switches separately, to see what each one does alone, should
		// be able to.
		if (strcmp(argv[argIndex], "--near-plane") == 0)
		{
			int plane = 0;

			if (((argIndex + 1) < argc) && (argv[argIndex + 1][0] != '-'))
			{
				plane = atoi(argv[++argIndex]);
			}

			g_cfg_nearPlane = plane;
			printf("[CTR Near] near plane %d (clip threshold %d)\n", plane, plane * 2);
		}

		// ONE OF THE TWO EFFECTS OF THE NEAR PLANE, ALONE.
		//
		// A probe with near threshold 1 made the picture fault in the lower
		// corners disappear, but it changed two things at once: the
		// detail decision and the cut. --near-detail-off takes out only the
		// first - nine places no longer draw a near group of four
		// as a coarse quad, but let the normal mask selection run.
		// The cut and the threshold stay as they are.
		//
		// The counterpart is --near-cut-off. Whoever sets both has rebuilt the probe
		// from back then; whoever sets one sees which of the two it
		// was. Default off, and off is bit-identical.
		if (strcmp(argv[argIndex], "--near-detail-off") == 0)
		{
			g_cfg_nearDetailOff = 1;
			printf("[CTR Near] detail branch off (--near-detail-off): a near vertex no longer forces DIRECT_QUAD\n");
		}

		// The counterpart: only the cut, the detail decision stays.
		// Thirteen places write a clip record from a near
		// vertex; with this switch the face instead goes
		// unsplit into the draw buffer.
		if (strcmp(argv[argIndex], "--near-cut-off") == 0)
		{
			g_cfg_nearCutOff = 1;
			printf("[CTR Near] cut branch off (--near-cut-off): a near vertex no longer writes a clip record\n");
		}

		// The same thirteen places, but one by one. --near-cut-sites takes a
		// mask: bit i set means "place i takes its clip record as
		// before", bit i zero means "it falls through". Default 0x1fff, that is
		// all thirteen on, and that is bit-identical to the behaviour without this
		// switch. The numbers are in the report of --near-report.
		//
		// 0x1fff without a single bit measures "which place do I miss",
		// a single bit alone measures "which place is enough for me".
		if ((strcmp(argv[argIndex], "--near-cut-sites") == 0) && ((argIndex + 1) < argc))
		{
			// Base 0, so that 0x1fff and 8191 both work.
			g_cfg_nearCutMask = (unsigned int)strtoul(argv[++argIndex], NULL, 0);
			printf("[CTR Near] cut site mask 0x%04x (--near-cut-sites)\n", g_cfg_nearCutMask);
		}

		// Box keep: the clip record box no longer drops, it only slows down - a
		// face that it rejects goes into the clip record anyway, and the writer
		// gets the space test for it instead. This is the default (g_cfg_nearBoxKeep
		// in game/native_view.c); --near-box-keep stays accepted because older
		// measuring command lines carry it. --near-box-drop is the way back for an
		// A/B: the box drops again as in stock.
		if (strcmp(argv[argIndex], "--near-box-drop") == 0)
		{
			g_cfg_nearBoxKeep = 0;
			printf("[CTR Near] box keep off (--near-box-drop): the clip record box drops what it rejects, as stock does\n");
		}

		if (strcmp(argv[argIndex], "--near-box-keep") == 0)
		{
			g_cfg_nearBoxKeep = 1;
			printf("[CTR Near] box keep on (--near-box-keep, the default): a face the clip record box rejects is clipped anyway, space permitting\n");
		}

		// The count of near losses. Changes NOTHING in the picture - it may
		// therefore run along in the comparison run as well.
		if (strcmp(argv[argIndex], "--near-report") == 0)
		{
			g_cfg_nearReport = 1;
			Platform_AtExitReport(CTR_NearClip_Report);
			printf("[CTR Near] near-clip census on (--near-report)\n");
		}

		// Counts how often the projection clamps its screen coordinate to +-1024.
		// Up here for the same reason as --gte-alt: a count
		// that only starts after start-up has not seen the intro.
		// --gte-report [every N frames], without a number only the total at the end.
		if (strcmp(argv[argIndex], "--gte-report") == 0)
		{
			int everyFrames = 0;

			if (((argIndex + 1) < argc) && (argv[argIndex + 1][0] != '-'))
			{
				everyFrames = atoi(argv[++argIndex]);
			}

			NativeGteFlags_Arm(everyFrames);
		}

		// The fractional digits of the projection travel on to the vertex. Up
		// here for the same reason as --gte-alt: the shadow stack in the GTE
		// has to run along from the FIRST operation, not from the first one after
		// start-up.
		if (strcmp(argv[argIndex], "--subpixel") == 0)
		{
			g_cfg_subpixel = 1;
			printf("[CTR Sub] subpixel on (--subpixel)\n");
		}

		// Counts how many vertices could carry a fractional part from the
		// projection AT ALL, and how many the drawer sends
		// in total. Up here for the same reason as --gte-alt: a
		// count that only starts after start-up has not seen the
		// intro. --subpixel-report [every N frames].
		if (strcmp(argv[argIndex], "--subpixel-report") == 0)
		{
			int everyFrames = 0;

			if (((argIndex + 1) < argc) && (argv[argIndex + 1][0] != '-'))
			{
				everyFrames = atoi(argv[++argIndex]);
			}

			NativeSubpixel_Arm(everyFrames);
		}

		// The test ways of the custom characters and the placeholders of the
		// grid (platform/native_chars.c). Only remembered up here: a --char
		// that names no folder has to end the start before the first window
		// (the setup screen below is one); the folder or the file is read and
		// the roster built just before CTR_Main.
		if ((strcmp(argv[argIndex], "--chars-dir") == 0) && ((argIndex + 1) < argc))
		{
			NativeChar_SetFolder(argv[++argIndex]);
		}
		else if ((strcmp(argv[argIndex], "--char") == 0) && ((argIndex + 1) < argc))
		{
			NativeChar_SetFile(argv[++argIndex]);
		}
		else if ((strcmp(argv[argIndex], "--dev-grid-fill") == 0) && ((argIndex + 1) < argc))
		{
			// Placeholder tiles for the driver select grid. A count outside
			// 1..NATIVE_CHAR_ROSTER_MAX ends the start here, like a --char without
			// a folder: a measuring call that silently ran with another count
			// would measure another grid. Digits only, from the first character on:
			// strtol alone would also take " 5" and "+5".
			const char *value = argv[++argIndex];
			int digitsOnly = (value[0] >= '0') && (value[0] <= '9');
			long count = 0;

			for (size_t i = 1; digitsOnly && (value[i] != '\0'); i++)
			{
				digitsOnly = (value[i] >= '0') && (value[i] <= '9');
			}

			// Too many digits saturate at LONG_MAX, which is out of range as well.
			if (digitsOnly)
			{
				count = strtol(value, NULL, 10);
			}

			if (!digitsOnly || (count < 1) || (count > NATIVE_CHAR_ROSTER_MAX))
			{
				fflush(stdout);
				fprintf(stderr, "switch --dev-grid-fill expects a count from 1 to %d, got %s\n", NATIVE_CHAR_ROSTER_MAX, value);
				fflush(stderr);
				return NativeConsole_Return((u32)NATIVE_EXIT_DEV_REQUIRED);
			}

			NativeChar_SetGridFill((int)count);
		}
		else if ((strcmp(argv[argIndex], "--dev-char-seats") == 0) && ((argIndex + 1) < argc))
		{
			// A measuring switch: a word it does not know ends the start, like
			// --dev-grid-fill - a run that silently bound nothing would measure retail.
			const char *value = argv[++argIndex];

			if (strcmp(value, "all") == 0)
			{
				NativeChar_SetDevSeats(NATIVE_CHAR_DEV_SEATS_ALL);
			}
			else if (strcmp(value, "cycle") == 0)
			{
				NativeChar_SetDevSeats(NATIVE_CHAR_DEV_SEATS_CYCLE);
			}
			else
			{
				fflush(stdout);
				fprintf(stderr, "switch --dev-char-seats expects all or cycle, got %s\n", value);
				fflush(stderr);
				return NativeConsole_Return((u32)NATIVE_EXIT_DEV_REQUIRED);
			}
		}
		else if ((strcmp(argv[argIndex], "--dev-char-seat-files") == 0) && ((argIndex + 1) < argc))
		{
			// A measuring switch (several custom models in one race,
			// platform/native_chars.c): given twice, together with
			// --dev-char-seats (two answers to who sits where) or with --char
			// (which reads no folder, so the roster holds one file), or with a
			// list not of the form, it ends the start here, before the first
			// window. A name that is not a loaded file of the roster ends it right
			// after the roster is read (NativeChar_DevSeatFilesResolve).
			const char *value = argv[++argIndex];
			int filesGiven = 0;
			int seatsGiven = 0;
			int charGiven = 0;

			for (int scanIndex = 1; scanIndex < argc; scanIndex++)
			{
				if (strcmp(argv[scanIndex], "--dev-char-seat-files") == 0)
				{
					filesGiven++;
				}
				else if (strcmp(argv[scanIndex], "--dev-char-seats") == 0)
				{
					seatsGiven = 1;
				}
				else if (strcmp(argv[scanIndex], "--char") == 0)
				{
					charGiven = 1;
				}
			}

			if (filesGiven > 1)
			{
				fflush(stdout);
				fprintf(stderr, "switch --dev-char-seat-files is given %d times - once only\n", filesGiven);
				fflush(stderr);
				return NativeConsole_Return((u32)NATIVE_EXIT_DEV_REQUIRED);
			}

			if (seatsGiven || charGiven)
			{
				fflush(stdout);
				fprintf(stderr, "switch --dev-char-seat-files does not go with %s\n", seatsGiven ? "--dev-char-seats" : "--char");
				fflush(stderr);
				return NativeConsole_Return((u32)NATIVE_EXIT_DEV_REQUIRED);
			}

			if (!NativeChar_SetDevSeatFiles(value))
			{
				fflush(stdout);
				fprintf(stderr, "switch --dev-char-seat-files expects 1 to %d file names of the folder, commas between, got %s\n",
				        NATIVE_CHAR_DEV_SEAT_FILES_MAX, value);
				fflush(stderr);
				return NativeConsole_Return((u32)NATIVE_EXIT_DEV_REQUIRED);
			}
		}
		else if ((strcmp(argv[argIndex], "--dev-mods") == 0) && ((argIndex + 1) < argc))
		{
			// A measuring switch, like --dev-char-seats: a word it does not
			// know ends the start.
			const char *value = argv[++argIndex];

			if (!NativeMods_SetDev(value))
			{
				fflush(stdout);
				fprintf(stderr, "switch --dev-mods expects default|random,off|random|selected[,file,...], got %s\n", value);
				fflush(stderr);
				return NativeConsole_Return((u32)NATIVE_EXIT_DEV_REQUIRED);
			}
		}
		else if ((strcmp(argv[argIndex], "--dev-mods-seed") == 0) && ((argIndex + 1) < argc))
		{
			const char *value = argv[++argIndex];
			char *end = NULL;
			const unsigned long seed = strtoul(value, &end, 10);

			if ((value[0] < '0') || (value[0] > '9') || (end == NULL) || (*end != '\0') || (seed == 0ul) || (seed > 0xfffffffful))
			{
				fflush(stdout);
				fprintf(stderr, "switch --dev-mods-seed expects a number from 1 to 4294967295, got %s\n", value);
				fflush(stderr);
				return NativeConsole_Return((u32)NATIVE_EXIT_DEV_REQUIRED);
			}

			NativeMods_SetSeed((u32)seed);
		}
		else if ((strcmp(argv[argIndex], "--dev-vk-pending-cap") == 0) && ((argIndex + 1) < argc))
		{
			const char *value = argv[++argIndex];
			char *end = NULL;
			const long cap = strtol(value, &end, 10);

			if ((end == NULL) || (*end != '\0') || (cap < 1) || (cap > 1024))
			{
				fflush(stdout);
				fprintf(stderr, "switch --dev-vk-pending-cap expects a number from 1 to 1024, got %s\n", value);
				fflush(stderr);
				return NativeConsole_Return((u32)NATIVE_EXIT_DEV_REQUIRED);
			}

			NativeVk_SetPendingCap((int)cap);
		}
		else if ((strcmp(argv[argIndex], "--native-probe-seat") == 0) && ((argIndex + 1) < argc))
		{
			// The seat whose model the native probe takes (platform/
			// native_render_layer.c binds it every frame). A measuring switch:
			// a seat outside 0..NATIVE_PROBE_SEATS - 1, a word that is not a
			// number, the switch given twice, or a probe that will not be on
			// (no --native-probe with a known form, or no --native-preview) ends the start
			// here, before the first window - a run in which the seat silently
			// does nothing would measure something else. Digits only, as for
			// --dev-grid-fill. The probe is on exactly when the late reader of
			// --native-probe says so: the last --native-probe has the form
			// body, texture or pose and --native-preview is given (a value is never a switch
			// name, the gate saw to that, so a plain scan finds the switches).
			// Nothing is needed before Platform_Init, so there is no early
			// reader; the late reader of --native-probe names the seat in the log.
			const char *value = argv[++argIndex];
			int digitsOnly = (value[0] >= '0') && (value[0] <= '9');
			const char *probeForm = NULL;
			int previewGiven = 0;
			int seatGiven = 0;
			long seat = -1;

			for (size_t i = 1; digitsOnly && (value[i] != '\0'); i++)
			{
				digitsOnly = (value[i] >= '0') && (value[i] <= '9');
			}

			// Too many digits saturate at LONG_MAX, which is out of range as well.
			if (digitsOnly)
			{
				seat = strtol(value, NULL, 10);
			}

			for (int scanIndex = 1; scanIndex < argc; scanIndex++)
			{
				if (strcmp(argv[scanIndex], "--native-probe-seat") == 0)
				{
					seatGiven++;
				}
				else if (strcmp(argv[scanIndex], "--native-preview") == 0)
				{
					previewGiven = 1;
				}
				else if ((strcmp(argv[scanIndex], "--native-probe") == 0) && ((scanIndex + 1) < argc))
				{
					probeForm = argv[++scanIndex];
				}
			}

			if (!digitsOnly || (seat < 0) || (seat >= NATIVE_PROBE_SEATS))
			{
				fflush(stdout);
				fprintf(stderr, "switch --native-probe-seat expects a seat from 0 to %d, got %s\n", NATIVE_PROBE_SEATS - 1, value);
				fflush(stderr);
				return NativeConsole_Return((u32)NATIVE_EXIT_DEV_REQUIRED);
			}

			if (seatGiven > 1)
			{
				fflush(stdout);
				fprintf(stderr, "switch --native-probe-seat is given %d times - once only\n", seatGiven);
				fflush(stderr);
				return NativeConsole_Return((u32)NATIVE_EXIT_DEV_REQUIRED);
			}

			if ((NativeProbe_FormFromName(probeForm) == NATIVE_PROBE_NONE) || !previewGiven)
			{
				fflush(stdout);
				fprintf(stderr, "switch --native-probe-seat only acts with the probe on: --native-preview --native-probe body|texture|pose|wheels|mips\n");
				fflush(stderr);
				return NativeConsole_Return((u32)NATIVE_EXIT_DEV_REQUIRED);
			}

			g_cfg_nativeProbeSeat = (int)seat;
		}
		else if (strcmp(argv[argIndex], "--native-wheel-report") == 0)
		{
			// The wheel report of the native probe (platform/native_wheels.c,
			// platform/native_render_layer.c). A measuring switch: without the
			// probe on - the last --native-probe with a known form and
			// --native-preview, as for --native-probe-seat - it would measure
			// nothing, so the start ends here, before the first window; so does
			// the switch given more than once (as --native-probe-seat). Nothing
			// is needed before Platform_Init; the big loop below names it in the
			// log. Never in ctr-settings.cfg.
			const char *probeForm = NULL;
			int previewGiven = 0;
			int reportGiven = 0;

			for (int scanIndex = 1; scanIndex < argc; scanIndex++)
			{
				if (strcmp(argv[scanIndex], "--native-wheel-report") == 0)
				{
					reportGiven++;
				}
				else if (strcmp(argv[scanIndex], "--native-preview") == 0)
				{
					previewGiven = 1;
				}
				else if ((strcmp(argv[scanIndex], "--native-probe") == 0) && ((scanIndex + 1) < argc))
				{
					probeForm = argv[++scanIndex];
				}
			}

			if (reportGiven > 1)
			{
				fflush(stdout);
				fprintf(stderr, "switch --native-wheel-report is given %d times - once only\n", reportGiven);
				fflush(stderr);
				return NativeConsole_Return((u32)NATIVE_EXIT_DEV_REQUIRED);
			}

			if ((NativeProbe_FormFromName(probeForm) == NATIVE_PROBE_NONE) || !previewGiven)
			{
				fflush(stdout);
				fprintf(stderr, "switch --native-wheel-report only acts with the probe on: --native-preview --native-probe body|texture|pose|wheels|mips\n");
				fflush(stderr);
				return NativeConsole_Return((u32)NATIVE_EXIT_DEV_REQUIRED);
			}

			g_cfg_nativeWheelReport = 1;
		}
		else if ((strcmp(argv[argIndex], "--native-seam-report") == 0) || (strcmp(argv[argIndex], "--native-depth-tint") == 0) ||
		         (strcmp(argv[argIndex], "--native-twin") == 0))
		{
			// The two measuring switches of the seam at view z 0x1000 (step 4c,
			// platform/native_render_layer.c). Without --native-preview there is
			// no native draw to report or colour, so the start ends here, before
			// the first window; so does a switch given twice (as
			// --native-hide-exhaust). The big loop below names them in the log.
			// Never in ctr-settings.cfg.
			const char *name = argv[argIndex];
			int given = 0;
			int previewGiven = 0;

			for (int scanIndex = 1; scanIndex < argc; scanIndex++)
			{
				if (strcmp(argv[scanIndex], name) == 0)
				{
					given++;
				}
				else if (strcmp(argv[scanIndex], "--native-preview") == 0)
				{
					previewGiven = 1;
				}
			}

			if (given > 1)
			{
				fflush(stdout);
				fprintf(stderr, "switch %s is given %d times - once only\n", name, given);
				fflush(stderr);
				return NativeConsole_Return((u32)NATIVE_EXIT_DEV_REQUIRED);
			}

			if (!previewGiven)
			{
				fflush(stdout);
				fprintf(stderr, "switch %s only acts together with --native-preview\n", name);
				fflush(stderr);
				return NativeConsole_Return((u32)NATIVE_EXIT_DEV_REQUIRED);
			}

			if (strcmp(name, "--native-seam-report") == 0)
			{
				g_cfg_nativeSeamReport = 1;
			}
			else if (strcmp(name, "--native-twin") == 0)
			{
				g_cfg_nativeTwin = 1;
			}
			else
			{
				g_cfg_nativeDepthTint = 1;
			}
		}
		else if (strcmp(argv[argIndex], "--native-split-report") == 0)
		{
			// The raw values of the water line (step 4e-1b,
			// platform/native_render_layer.c). Given twice it ends the start here,
			// before the first window (as --native-hide-exhaust). The big loop
			// below names it in the log. Never in ctr-settings.cfg.
			int given = 0;

			for (int scanIndex = 1; scanIndex < argc; scanIndex++)
			{
				if (strcmp(argv[scanIndex], "--native-split-report") == 0)
				{
					given++;
				}
			}

			if (given > 1)
			{
				fflush(stdout);
				fprintf(stderr, "switch --native-split-report is given %d times - once only\n", given);
				fflush(stderr);
				return NativeConsole_Return((u32)NATIVE_EXIT_DEV_REQUIRED);
			}

			g_cfg_nativeSplitReport = 1;
		}
		else if (strcmp(argv[argIndex], "--native-hide-exhaust") == 0)
		{
			// Leaves the exhaust quads out of the ordering table
			// (platform/native_render_layer.c, NativeRenderLayer_HideExhaustQuad;
			// game/Particle.c, Particle_RenderList), for the colour checks of the
			// native probe without the exhaust glow. A measuring switch that also
			// acts without --native-preview: the reference run without the probe
			// needs it as much as the probe run. Given more than once it ends the
			// start here, before the first window (as --native-wheel-report). The
			// big loop below names it in the log. Never in ctr-settings.cfg.
			int hideGiven = 0;

			for (int scanIndex = 1; scanIndex < argc; scanIndex++)
			{
				if (strcmp(argv[scanIndex], "--native-hide-exhaust") == 0)
				{
					hideGiven++;
				}
			}

			if (hideGiven > 1)
			{
				fflush(stdout);
				fprintf(stderr, "switch --native-hide-exhaust is given %d times - once only\n", hideGiven);
				fflush(stderr);
				return NativeConsole_Return((u32)NATIVE_EXIT_DEV_REQUIRED);
			}

			g_cfg_nativeHideExhaust = 1;
		}
		else if ((strcmp(argv[argIndex], "--native-filter") == 0) && ((argIndex + 1) < argc))
		{
			// The filter option of the native texture manager for this run
			// (platform/native_tex.c, renderer plan C.6.3): nearest (pixelated,
			// the default) or linear (trilinear, anisotropy up to the device
			// limit, granted only because --native-preview requested it when
			// the device was made). A measuring switch: a value other than
			// nearest or linear, the switch given twice, or no --native-preview
			// - without which the manager makes nothing to sample - ends the
			// start here, before the first window, as --native-probe-seat does.
			// Read before the first texture of the manager; the big loop below
			// names it in the log. Never in ctr-settings.cfg.
			const char *value = argv[++argIndex];
			const int filter = NativeTex_FilterFromName(value);
			int previewGiven = 0;
			int filterGiven = 0;

			for (int scanIndex = 1; scanIndex < argc; scanIndex++)
			{
				if (strcmp(argv[scanIndex], "--native-filter") == 0)
				{
					filterGiven++;
				}
				else if (strcmp(argv[scanIndex], "--native-preview") == 0)
				{
					previewGiven = 1;
				}
			}

			if (filter < 0)
			{
				fflush(stdout);
				fprintf(stderr, "switch --native-filter expects nearest or linear, got %s\n", value);
				fflush(stderr);
				return NativeConsole_Return((u32)NATIVE_EXIT_DEV_REQUIRED);
			}

			if (filterGiven > 1)
			{
				fflush(stdout);
				fprintf(stderr, "switch --native-filter is given %d times - once only\n", filterGiven);
				fflush(stderr);
				return NativeConsole_Return((u32)NATIVE_EXIT_DEV_REQUIRED);
			}

			if (!previewGiven)
			{
				fflush(stdout);
				fprintf(stderr, "switch --native-filter only acts together with --native-preview\n");
				fflush(stderr);
				return NativeConsole_Return((u32)NATIVE_EXIT_DEV_REQUIRED);
			}

			g_cfg_nativeFilter = filter;
		}
		else if (strcmp(argv[argIndex], "--native-depth-d24") == 0)
		{
			// The depth format of the native layer for this run: X8_D24_UNORM_PACK32
			// instead of D32_SFLOAT, to measure the two against each other. Only
			// with --native-preview, which alone makes depth images - without it
			// the switch would do nothing, so the start ends here instead
			// (platform/native_gfx_vk.c, NativeVk_PickDepthFormat, reads it).
			extern int g_cfg_nativeDepthD24;
			int previewGiven = 0;

			for (int scanIndex = 1; scanIndex < argc; scanIndex++)
			{
				if (strcmp(argv[scanIndex], "--native-preview") == 0)
				{
					previewGiven = 1;
				}
			}

			if (!previewGiven)
			{
				fflush(stdout);
				fprintf(stderr, "switch --native-depth-d24 only acts together with --native-preview\n");
				fflush(stderr);
				return NativeConsole_Return((u32)NATIVE_EXIT_DEV_REQUIRED);
			}

			g_cfg_nativeDepthD24 = 1;
		}
	}

	if (!NativeChar_ArgsUsable())
	{
		return NativeConsole_Return((u32)NATIVE_EXIT_DEV_REQUIRED);
	}

	printf("[CTR Native] Starting...\n");
	fflush(stdout);

	const char *sdlBasePath = SDL_GetBasePath();
	printf("[CTR Native] SDL base path: %s\n", sdlBasePath ? sdlBasePath : "(null)");
	fflush(stdout);

	if (!NativeAssets_Init(sdlBasePath))
	{
		fprintf(stderr, "[CTR Native] Failed to initialize asset paths.\n");
		return NativeConsole_Return(1);
	}

	printf("[CTR Native] Version: CTR Reload %s (%s)\n", CTR_NATIVE_VERSION, CTR_NATIVE_BUILD_ID);
	printf("[CTR Native] Built with: " CC "\n");
	printf("[CTR Native] Base: %s\n", NativeAssets_GetBaseDir());
	printf("[CTR Native] Assets: %s\n", NativeAssets_GetAssetDir());
	fflush(stdout);

	if (NATIVE_CHDIR(NativeAssets_GetBaseDir()) != 0)
	{
		fprintf(stderr, "[CTR Native] Failed to enter base directory: %s\n", NativeAssets_GetBaseDir());
		return NativeConsole_Return(1);
	}

	// The disc-read census, armed before anything reads a single sector. It has
	// to be on before NativeAssets_Validate runs, or the boot reads it does are
	// missing from the count.
	for (int argIndex = 1; argIndex < argc; argIndex++)
	{
		if (strcmp(argv[argIndex], "--disc-report") == 0)
		{
			NativeDiscImage_SetReport(1);

			// The report runs at exit and NOT at the end of main.
			//
			// It first stood at the end of main, and it never got there: Ctrl+Q,
			// the window closer and SDL_EVENT_QUIT all go through exit(0)
			// in native_platform.c and jump past everything that comes
			// after it. So the counter ran along a whole evening and kept its
			// result to itself - measured on a first test run, 7,273
			// lines without a single report line.
			//
			// Through Platform_AtExitReport and not through atexit: atexit calls in
			// reverse order, Platform_Shutdown is hooked in in Platform_Init
			// - that is AFTER this switch - and therefore closed the
			// log before the report came. "The order does not matter"
			// stood here; it did matter, the report was only on the
			// console. Now it goes through the one door before closing.
			Platform_AtExitReport(NativeDiscImage_PrintReport);

			printf("[CTR Disc] read counters on. The report comes at exit.\n");
			fflush(stdout);
		}
		else if (strcmp(argv[argIndex], "--native-layer-report") == 0)
		{
			// The counters of platform/native_render_layer.c run in every run;
			// the switch only lets them into the log at exit, through the same
			// door as the disc report for the same reason. A run value only:
			// never in ctr-settings.cfg, the file only knows s_videoSettings
			// and the views.
			g_cfg_nativeLayerReport = 1;
			Platform_AtExitReport(NativeRenderLayer_Report);

			printf("[CTR RenderLayer] render layer counters on. The report comes at exit.\n");
			fflush(stdout);
		}
	}

	// Before the validator, because a first start has nothing for it to look at.
	// This is where a stranger's copy gets its data, and where it says why not.
	if (!NativeSetup_EnsureData(argc, argv))
	{
		return NativeConsole_Return(1);
	}

	NativeContent_EnsureFolders();

	if (!NativeAssets_Validate())
	{
		return NativeConsole_Return(1);
	}

#if defined(CTR_INTERNAL)
	if (NativeReplayScheduler_PrepareReportFromArgs(argc, argv) != 0)
	{
		return NativeConsole_Return(1);
	}
#endif

	{
		// No window size here any more. It was 800x600, which is 4:3, and so was
		// the default world aspect - two independent values that agreed, which is
		// why it was never obvious that the window size did not decide the shape of
		// the picture. The shape decides the window now, which is the dependency
		// the right way round.
		//
		// What was here was a USE_16BY9 branch that no build defined. It printed
		// "Widescreen" and opened a 1280x720 window - and never touched the world
		// aspect, so it would have shown a 4:3 picture letterboxed inside a 16:9
		// window. A window size that names an aspect it does not set is the same
		// mistake in the other direction.
		int windowWidth = 0;
		int windowHeight = 0;
		const int fullscreen = NativeArgs_ReadDisplayFlags(argc, argv, &windowWidth, &windowHeight);

		Platform_Init("Crash Team Racing", windowWidth, windowHeight, fullscreen);

		// The version in the log too - on stdout alone it would be lost, and a
		// tester sends the log, not the console.
		Platform_Log("[CTR Native] Version: CTR Reload %s (%s)\n", CTR_NATIVE_VERSION, CTR_NATIVE_BUILD_ID);
	}

	// And append the total of the lost splits at the end.
	//
	// Unconditionally, unlike the disc report: an overflowed
	// split is a fault in the picture, and a fault that is only counted under a
	// switch is not counted. The function stays silent
	// by itself when nothing happened.
	Platform_AtExitReport(NativeGpu_PrintSplitReport);

	// Plus the size limit of the PS1 GPU, which this renderer does not have, and the final total
	// of the renderer - vertex ring, fill quads, page store. The
	// size report stays silent when there was nothing; the final total always comes.
	Platform_AtExitReport(NativeGpu_PrintSizeReport);
	Platform_AtExitReport(NativeRenderer_PrintExitSummary);

	// And the clip buffer of the near plane: peak per frame, records without
	// space, whole frames dropped. Unconditionally, for the same reason as
	// the split report - a hole in the road that is only counted under --near-report
	// is not counted.
	{
		void CTR_Clip_PrintReport(void);
		void CTR_Tires_PrintReport(void);

		Platform_AtExitReport(CTR_Clip_PrintReport);

		// And the wheels of distant karts that NO LOD forces behind the header
		// threshold: how many records, how many of them left out as a line,
		// and whether a stock wheel would ever have triggered the same test.
		Platform_AtExitReport(CTR_Tires_PrintReport);
	}

#if defined(CTR_INTERNAL)
	{
		// The boot flags live in platform/native_renderer.c.
		extern int g_cfg_fastBoot;
		extern int g_cfg_legacyCopyrightHold;
		extern int g_cfg_legacyBootLogos;
		extern int g_cfg_skipIntroLevel;

		// Measuring trigger, off unless asked for. See Platform_DumpConfigure.
		const char *dumpList = NULL;
		const char *dumpPrefix = NULL;
		int dumpExit = 0;
		int saveStateAt = -1;
		int loadStateAt = -1;

		for (int argIndex = 1; argIndex < argc; argIndex++)
		{
			if ((strcmp(argv[argIndex], "--dump-vram") == 0) && ((argIndex + 1) < argc))
			{
				dumpList = argv[++argIndex];
			}
			else if ((strcmp(argv[argIndex], "--dump-prefix") == 0) && ((argIndex + 1) < argc))
			{
				dumpPrefix = argv[++argIndex];
			}
			else if (strcmp(argv[argIndex], "--dump-exit") == 0)
			{
				dumpExit = 1;
			}
			else if ((strcmp(argv[argIndex], "--save-state-at") == 0) && ((argIndex + 1) < argc))
			{
				saveStateAt = atoi(argv[++argIndex]);
			}
			else if ((strcmp(argv[argIndex], "--load-state-at") == 0) && ((argIndex + 1) < argc))
			{
				loadStateAt = atoi(argv[++argIndex]);
			}
			else if (strcmp(argv[argIndex], "--fill-rect-as-draw") == 0)
			{
				// The default since the sky and the water were fixed by it.
				// Still accepted, because a flag that says what it does is
				// worth more than one that silently disappeared.
				NativeRenderer_SetFillRectAsDraw(1);
			}
			else if (strcmp(argv[argIndex], "--fill-rect-as-clear") == 0)
			{
				// Back to the old route, for putting the two side by side.
				NativeRenderer_SetFillRectAsDraw(0);
			}
			else if ((strcmp(argv[argIndex], "--page-scale") == 0) && ((argIndex + 1) < argc))
			{
				// How many atlas texels one VRAM texel occupies, 1 to 4. With
				// the atlas still filled from VRAM this changes nothing that can
				// be seen - every atlas texel of a block holds the same index -
				// which is exactly what makes it a container worth checking
				// before anything is put in it.
				const int requested = atoi(argv[++argIndex]);

				NativeRenderer_SetPageScale(requested);
				printf("[CTR Native] page store x%d\n", NativeRenderer_GetPageScale());
			}
			else if (strcmp(argv[argIndex], "--vram-copy-unclamped") == 0)
			{
				// The old route: a VRAM copy is written where it says, even
				// when that is outside VRAM. On the disc's own data the two are
				// the same picture and the same bytes; this exists so that can
				// be shown rather than assumed.
				NativeRenderer_SetVRAMCopyClamp(0);
			}
			else if (strcmp(argv[argIndex], "--ui-safe-area-off") == 0)
			{
				// The route before the UI was mapped at all: the HUD is drawn
				// into the 512-column buffer and the presentation stretches it
				// with everything else. At 4:3 the two are the same picture and
				// the same bytes; this exists so that can be shown rather than
				// assumed.
				extern int g_cfg_uiSafeArea;

				g_cfg_uiSafeArea = 0;
			}
			else if (strcmp(argv[argIndex], "--ui-declarations-off") == 0)
			{
				// No table. Every element falls back to the derived thirds rule,
				// which is the answer the mapping gives on its own.
				extern int g_cfg_uiDeclOff;

				g_cfg_uiDeclOff = 1;
			}
			else if (strcmp(argv[argIndex], "--ui-floor-off") == 0)
			{
				// No clamp. Margins and gaps come out at whatever the mapping's
				// own arithmetic makes of them.
				extern int g_cfg_uiFloorOff;

				g_cfg_uiFloorOff = 1;
			}
			else if (strcmp(argv[argIndex], "--ui-anchor-per-group") == 0)
			{
				// A declaration is only ever looked up for a whole element, the
				// way it was before the fruit counter flickered. Keeps the old
				// answer reachable so the flicker can be shown rather than
				// described.
				extern int g_cfg_uiAnchorPerGroup;

				g_cfg_uiAnchorPerGroup = 1;
			}
			else if ((strcmp(argv[argIndex], "--ui-watch-from") == 0) && ((argIndex + 1) < argc))
			{
				// Where the watch starts. A boot sequence walks through a dozen
				// screens and every one decides different things, which is not a
				// flicker - it is a different screen. Without this the watch
				// spends its whole budget before the run reaches what it was
				// pointed at.
				extern int g_cfg_uiWatch;
				extern int g_cfg_uiWatchFrom;

				g_cfg_uiWatch = 1;
				g_cfg_uiWatchFrom = atoi(argv[++argIndex]);
				printf("[CTR Native] ui watch from vblank %d\n", g_cfg_uiWatchFrom);
			}
			else if (strcmp(argv[argIndex], "--ui-watch") == 0)
			{
				// Print the UI decision table whenever a decision differs from
				// the frame before. Silent while nothing decides differently -
				// which is the whole point, because a flicker is by definition a
				// difference between frames and a one-frame table cannot see one.
				extern int g_cfg_uiWatch;

				g_cfg_uiWatch = 1;
			}
			else if (strcmp(argv[argIndex], "--ui-anchor-legacy") == 0)
			{
				// Every undeclared element centred, which is the one rule the
				// port had before the thirds rule existed. Kept as the A/B
				// partner for the fallback itself.
				extern int g_cfg_uiAnchorLegacy;

				g_cfg_uiAnchorLegacy = 1;
			}
			else if ((strcmp(argv[argIndex], "--ui-elements") == 0) && ((argIndex + 1) < argc))
			{
				// Print one frame worth of UI elements at that VBlank: authored
				// box, anchor, declaration, floor shift, mapped box. The numbers
				// UI placement is checked against.
				extern int g_cfg_uiElementsAt;

				g_cfg_uiElementsAt = atoi(argv[++argIndex]);
				printf("[CTR Native] ui elements at vblank %d\n", g_cfg_uiElementsAt);
			}
			else if (strcmp(argv[argIndex], "--texture-filter") == 0)
			{
				// Textures sampled bilinearly instead of nearest. The same setting F3
				// moves at runtime; here so an A/B is two runs rather than a hand on a
				// key, and so both runs catch the same moment.
				extern int g_cfg_bilinearFiltering;

				g_cfg_bilinearFiltering = 1;
				printf("[CTR Native] texture filter: bilinear\n");
			}
			else if ((strcmp(argv[argIndex], "--shot") == 0) && ((argIndex + 1) < argc))
			{
				// A picture of the frame at that VBlank - the main render target at
				// its full internal size, because the window (swapchain) cannot be
				// read back (native_platform.c, Platform_TakeScreenshot). The VRAM
				// dumps cannot answer a question about the upscaled picture - the
				// displayed VRAM area is 512x216 at factor one.
				extern int g_cfg_shotAt;
				extern int g_cfg_shotList[PLATFORM_SHOT_MAX];
				extern int g_cfg_shotCount;

				// Also a list "131,133,140" - one run, many snapshots, each
				// as <name>-<vblank>.bmp. A single value writes <name> as given.
				// The list is taken in the order given, so it has to ascend.
				// Read where it stands, not from a copy: a copy of 512 bytes cut
				// a long list after 511 characters without a word. More than
				// PLATFORM_SHOT_MAX entries never get here (NativeArgs_ValueFits).
				{
					const char *spec = argv[++argIndex];
					const char *cursor = spec;

					g_cfg_shotCount = 0;
					while ((*cursor != '\0') && (g_cfg_shotCount < PLATFORM_SHOT_MAX))
					{
						// Empty entries (",,") are skipped, as strtok skipped them.
						if (*cursor == ',')
						{
							cursor++;
							continue;
						}

						g_cfg_shotList[g_cfg_shotCount++] = atoi(cursor);
						while ((*cursor != '\0') && (*cursor != ','))
						{
							cursor++;
						}
					}
					g_cfg_shotAt = (g_cfg_shotCount > 0) ? g_cfg_shotList[0] : atoi(spec);
					if (g_cfg_shotCount > 1)
					{
						printf("[CTR Native] window shots at %d vblanks, first %d, last %d\n", g_cfg_shotCount, g_cfg_shotList[0], g_cfg_shotList[g_cfg_shotCount - 1]);
					}
					else
					{
						printf("[CTR Native] window shot at vblank %d\n", g_cfg_shotAt);
					}
				}
			}
			else if (strcmp(argv[argIndex], "--clip-window-512") == 0)
			{
				// The clip record window 0x100 wide again as on the 512
				// canvas (the state before this change), in the same build. Default is the reference width.
				extern int g_cfg_clipWindow512;

				g_cfg_clipWindow512 = 1;
				printf("[CTR Native] --clip-window-512: clip record window 0x100 wide regardless of the canvas (old path)\n");
			}
			else if (strcmp(argv[argIndex], "--clip-trace") == 0)
			{
				// One line per frame, how many clip records were consumed and how
				// many polygons were drawn from them - to find the place at
				// which a clip record added later changes the picture.
				extern int g_cfg_clipTrace;

				g_cfg_clipTrace = 1;
				printf("[CTR Native] --clip-trace: one line per frame with clip records consumed and polygons emitted\n");
			}
			else if (strcmp(argv[argIndex], "--size-rule-canvas") == 0)
			{
				// The PS1 size rule in canvas pixels again (the earlier
				// state), in the same build - for comparing both rules on
				// the same pictures. Default is the reference width.
				extern int g_cfg_sizeRuleCanvas;

				g_cfg_sizeRuleCanvas = 1;
				printf("[CTR Native] --size-rule-canvas: PS1 size rule in canvas pixels (old path)\n");
			}
			else if ((strcmp(argv[argIndex], "--shot-name") == 0) && ((argIndex + 1) < argc))
			{
				// Its own file per run. SCREENSHOT.BMP was one name for every shot.
				extern char g_cfg_shotName[PLATFORM_ARG_PATH_MAX];

				snprintf(g_cfg_shotName, sizeof(g_cfg_shotName), "%s", argv[++argIndex]);
				printf("[CTR Native] window shot: %s\n", g_cfg_shotName);
			}
			else if ((strcmp(argv[argIndex], "--dither") == 0) && ((argIndex + 1) < argc))
			{
				// off | packed | always. Named rather than numbered, because a
				// run that says "--dither 2" in a bat file says nothing.
				const char *spec = argv[++argIndex];

				if (strcmp(spec, "off") == 0)
				{
					NativeRenderer_SetDither(0);
				}
				else if (strcmp(spec, "always") == 0)
				{
					NativeRenderer_SetDither(2);
				}
				else if (strcmp(spec, "packed") == 0)
				{
					NativeRenderer_SetDither(1);
				}
				else
				{
					printf("[CTR Native] --dither wants off, packed or always - '%s' is none of them\n", spec);
					return NativeConsole_Return(1);
				}

				printf("[CTR Native] dither: %s\n", NativeRenderer_DitherName());
			}
			else if (strcmp(argv[argIndex], "--vertex-ring-off") == 0)
			{
				// Back to handing each upload the front of the other buffer,
				// turn and turn about - which lets a frame with many batches
				// write over geometry its own recorded draws still need. The old
				// road, so the two can be compared rather than argued.
				extern int g_cfg_vertexRing;

				g_cfg_vertexRing = 0;
				printf("[CTR Native] vertex ring: off, every upload starts at the front\n");
			}
			else if (strcmp(argv[argIndex], "--vk-validation") == 0)
			{
				// The Khronos validation layer including synchronization validation,
				// if the SDK has it (native_gfx_vk.c, NativeVk_CreateInstance).
				// It used to be ON as soon as it was installed - on a development
				// machine that meant in EVERY run, test drives and perf measurements
				// included. It checks every command and every submit; that is a tool
				// for hunting renderer bugs, not a game state. Default off.
				extern int g_cfg_vkValidation;

				g_cfg_vkValidation = 1;
				printf("[CTR Native] Vulkan validation layer wanted (with synchronization validation)\n");
			}
			else if (strcmp(argv[argIndex], "--native-preview") == 0)
			{
				// A run value like --msaa, fixed for the whole run and never
				// written to ctr-settings.cfg. NativeArgs_ReadDisplayFlags already set
				// it before Platform_Init; this is where the log learns of it.
				// NATIVE DRIVERS (GRAPHICS page) sets the same value from the file,
				// and this switch wins over its OFF; a run with both says this line
				// only.
				extern int g_cfg_nativePreview;

				g_cfg_nativePreview = 1;
				Platform_Log("[CTR Native] native preview on for this run: native program 'nr', depth on the main target only while a native object is bound - never saved\n");

				// The marker channel reports at exit only in a run that has it.
				Platform_AtExitReport(NativeRenderLayer_MarkerReport);
			}
			else if (strcmp(argv[argIndex], "--native-wheel-report") == 0)
			{
				// Set in the first loop of main, which also refused it without the
				// probe on; this is where the log learns of it.
				Platform_Log("[CTR Native] native wheel report on: per tick the float, the exact and the retail wheel middles of seat %d, "
				             "whose retail wheels stay on; sign check from |move| >= %.4f world units\n",
				             g_cfg_nativeProbeSeat, NATIVE_WHEELS_MOVING);
			}
			else if (strcmp(argv[argIndex], "--native-twin") == 0)
			{
				// Set in the first loop of main, which also refused it without
				// --native-preview or given twice.
				Platform_Log("[CTR Native] native twin on (measurement only): seat 0's retail model drawn natively as its retail twin, its retail wheels on\n");
			}
			else if (strcmp(argv[argIndex], "--native-split-report") == 0)
			{
				// Set in the first loop of main, which also refused it given twice.
				Platform_Log("[CTR Native] native split report on (measurement only): the raw water line values of seat 0, the first four, then every 30th VBlank\n");
			}
			else if (strcmp(argv[argIndex], "--native-seam-report") == 0)
			{
				// Set in the first loop of main, which also refused it without
				// --native-preview or given twice; this is where the log learns of it.
				Platform_Log("[CTR Native] native seam report on (measurement only): one line per frame and native item of view 0\n");
			}
			else if (strcmp(argv[argIndex], "--native-depth-tint") == 0)
			{
				// As above.
				Platform_Log("[CTR Native] native depth tint on (measurement only): every native draw is coloured by its depth, green = 1024 / w\n");
			}
			else if (strcmp(argv[argIndex], "--native-hide-exhaust") == 0)
			{
				// Set in the first loop of main, which also refused it given twice;
				// this is where the log learns of it, and the count comes at exit.
				Platform_Log("[CTR Native] exhaust particles hidden (measurement only): the exhaust and burn smoke quads of every seat are not drawn, "
				             "the particles live as always\n");
				Platform_AtExitReport(NativeRenderLayer_HideExhaustReport);
			}
			else if ((strcmp(argv[argIndex], "--native-filter") == 0) && ((argIndex + 1) < argc))
			{
				// Set in the first loop of main, which also refused a wrong value,
				// a second one and a run without --native-preview; this is where
				// the log learns of it.
				argIndex++;
				Platform_Log("[CTR Native] native texture filter for this run: %s (--native-filter) - never saved\n",
				             NativeTex_FilterName(g_cfg_nativeFilter));
			}
			else if (strcmp(argv[argIndex], "--native-depth-d24") == 0)
			{
				// Set in the first loop of main, which also refused it without
				// --native-preview; this is where the log learns of it.
				Platform_Log("[CTR Native] native depth format for this run: X8_D24_UNORM_PACK32 where usable (--native-depth-d24)\n");
			}
			else if (strcmp(argv[argIndex], "--native-empty-markers") == 0)
			{
				// A self-test of the marker channel: an empty marker at every driver
				// instance, retail keeps drawing. Only with the native preview, which the
				// early reader (NativeArgs_ReadDisplayFlags) or NATIVE DRIVERS PREVIEW
				// (Platform_SettingsPreloadDisplay) set before the window -
				// so the order of the two switches on the command line does not matter.
				// Not needed before the first frame, so there is no early reader.
				// Never in ctr-settings.cfg, like --native-preview.
				extern int g_cfg_nativePreview;
				extern int g_cfg_nativeEmptyMarkers;

				if (g_cfg_nativePreview)
				{
					g_cfg_nativeEmptyMarkers = 1;
					Platform_Log("[CTR Native] empty native markers on: one at every driver instance, retail keeps drawing\n");
				}
				else
				{
					Platform_Log("[CTR Native] --native-empty-markers needs --native-preview - off for this run\n");
				}
			}
			else if (strcmp(argv[argIndex], "--split-audit") == 0)
			{
				// The box check (native_gpu.c, NativeGpu_SplitAuditBatch):
				// in every 30th frame read all vertices of every batch once more and
				// compare up to 512 boxes per split. It used to run without a switch;
				// its question has been answered, so it is opt-in now.
				extern int g_cfg_splitAudit;

				g_cfg_splitAudit = 1;
				printf("[CTR Native] split audit on: boxes checked in every 30th frame\n");
			}
			else if ((strcmp(argv[argIndex], "--native-probe") == 0) && ((argIndex + 1) < argc))
			{
				// The early reader (NativeArgs_ReadDisplayFlags) already set the
				// value before Platform_Init; this is where the log learns of it.
				// Only with the native preview, which the early reader (or NATIVE
				// DRIVERS PREVIEW, Platform_SettingsPreloadDisplay) set as well -
				// so the order of the two switches does not matter. Never in
				// ctr-settings.cfg, like --native-preview.
				extern int g_cfg_nativePreview;
				extern int g_cfg_nativeProbe;
				const char *form = argv[++argIndex];

				if (NativeProbe_FormFromName(form) == NATIVE_PROBE_NONE)
				{
					g_cfg_nativeProbe = NATIVE_PROBE_NONE;
					g_cfg_nativeProbeWheels = 0;
					g_cfg_nativeProbeMips = 0;
					Platform_Log("[CTR Native] --native-probe: unknown form %s (known: body, texture, pose, wheels, mips) - off\n", form);
				}
				else if (!g_cfg_nativePreview)
				{
					g_cfg_nativeProbe = NATIVE_PROBE_NONE;
					g_cfg_nativeProbeWheels = 0;
					g_cfg_nativeProbeMips = 0;
					Platform_Log("[CTR Native] --native-probe needs --native-preview - off for this run\n");
				}
				else
				{
					// The seat came from --native-probe-seat in the first loop of
					// main (default 0, the line as before).
					g_cfg_nativeProbe = NativeProbe_FormFromName(form);
					if (g_cfg_nativeProbeMips)
					{
						Platform_Log("[CTR Native] native probe on: seat %d of a one-player arcade race draws a generated test body natively, "
						             "its top shaped by the retail animation frame, coloured by an sRGB texture with 9 levels of one colour each, "
						             "made while the race loads, sampled %s\n",
						             g_cfg_nativeProbeSeat, NativeTex_FilterName(g_cfg_nativeFilter));
					}
					else if (g_cfg_nativeProbeWheels)
					{
						Platform_Log("[CTR Native] native probe on: seat %d of a one-player arcade race draws a generated test body natively, "
						             "coloured by a generated texture, its top shaped by the retail animation frame, with four generated wheels "
						             "in place of the retail wheels\n",
						             g_cfg_nativeProbeSeat);
					}
					else if (g_cfg_nativeProbe == NATIVE_PROBE_POSE)
					{
						Platform_Log("[CTR Native] native probe on: seat %d of a one-player arcade race draws a generated test body natively, "
						             "coloured by a generated texture, its top shaped by the retail animation frame\n",
						             g_cfg_nativeProbeSeat);
					}
					else if (g_cfg_nativeProbe == NATIVE_PROBE_TEXTURE)
					{
						Platform_Log("[CTR Native] native probe on: seat %d of a one-player arcade race draws a generated test body natively, "
						             "coloured by a generated texture\n",
						             g_cfg_nativeProbeSeat);
					}
					else
					{
						Platform_Log("[CTR Native] native probe on: seat %d of a one-player arcade race draws a generated test body natively\n",
						             g_cfg_nativeProbeSeat);
					}
				}
			}
			else if (strcmp(argv[argIndex], "--rank-report") == 0)
			{
				// Every input the race position is made of, one line per driver,
				// every N vblanks. Reads nothing back and changes nothing - it is
				// there because a frozen position has four possible causes and
				// guessing between them costs more runs than reading them does.
				extern int g_cfg_rankReport;
				int everyVBlanks = 60;

				if (((argIndex + 1) < argc) && (argv[argIndex + 1][0] != '-'))
				{
					everyVBlanks = atoi(argv[++argIndex]);
				}

				if (everyVBlanks < 1)
				{
					everyVBlanks = 1;
				}

				g_cfg_rankReport = everyVBlanks;
				printf("[CTR Native] rank report: every %d vblank(s)\n", everyVBlanks);
			}
			else if (strcmp(argv[argIndex], "--page-rows-off") == 0)
			{
				// Back to dirtying every tile a VRAM write touches, whether or
				// not the rows it touched are rows that tile is read at. The
				// old road, so the two can be compared rather than argued.
				extern int g_cfg_pageRowRange;

				g_cfg_pageRowRange = 0;
				printf("[CTR Native] page rows: off, every overlap refills\n");
			}
			else if (strcmp(argv[argIndex], "--page-preload-off") == 0)
			{
				// Back to a tile becoming resident when a draw first names its
				// page, and the atlas growing under the race.
				extern int g_cfg_pagePreload;

				g_cfg_pagePreload = 0;
				printf("[CTR Native] page preload: off, tiles arrive on first use\n");
			}
			else if ((strcmp(argv[argIndex], "--lod") == 0) && ((argIndex + 1) < argc))
			{
				// Which of the four detail stages are forced to their highest
				// level. "stock" is the game as it shipped, "all" (also accepted:
				// "alle") is all four, and a comma list names them:
				// tex,track,model,subdiv.
				//
				// A name that is not a stage aborts rather than being ignored.
				// An unreadable switch that runs anyway is a measuring run that
				// says it measured something it did not.
				int mask = 0;

				if (!CTR_Lod_ParseMask(argv[++argIndex], &mask))
				{
					fprintf(stderr, "[CTR Native] --lod: '%s' names no stage. stock | all | tex,track,model,subdiv\n", argv[argIndex]);
					return 1;
				}

				CTR_Lod_SetMask(mask);
				printf("[CTR Native] lod mask 0x%x\n", CTR_Lod_Mask());
			}
			else if (strcmp(argv[argIndex], "--bind-vertex-implicit") == 0)
			{
				// The old route: the upload binds along the way, the renderer
				// does not bind. That is how it ran while the back end rebuilt GL's
				// version. Both routes must give the same picture; this
				// switch is there so that it is run and not believed.
				extern int g_cfg_bindVertexExplicit;

				g_cfg_bindVertexExplicit = 0;
				printf("[CTR Native] vertex bind: implicit, the upload binds (old path)\n");
			}
			else if (strcmp(argv[argIndex], "--lod-keep") == 0)
			{
				// The distance mechanism back on: textures step down as a face
				// recedes, leaves melt past the threshold, models fall to a
				// coarser header, karts lose their wheels. The picture the game
				// shipped with, and the only way to put the two side by side.
				//
				// The mask underneath is untouched, so --lod-keep --lod tex is a
				// run with one stage forced and the rest stock, exactly as
				// before this mode existed.
				CTR_Lod_SetNoneMode(0);
				printf("[CTR Native] lod: the distance mechanism is ON, mask 0x%x\n", CTR_Lod_Mask());
			}
			else if (strcmp(argv[argIndex], "--lod-none") == 0)
			{
				// The built-in value, said out loud. Exists so a run can state
				// what it measured instead of relying on what the default
				// happened to be that week - which is how the last one was lost.
				CTR_Lod_SetNoneMode(1);
				printf("[CTR Native] lod: the distance mechanism is OFF\n");
			}
			else if ((strcmp(argv[argIndex], "--lod-report") == 0) && ((argIndex + 1) < argc))
			{
				// Count a window of frames starting at that VBlank and print the
				// totals once. A window and not a frame: one frame is not a
				// measurement, and the same corner of the same lap is not the
				// same frame twice.
				const int at = atoi(argv[++argIndex]);
				int frames = 60;

				if (((argIndex + 1) < argc) && (argv[argIndex + 1][0] != '-'))
				{
					frames = atoi(argv[++argIndex]);
				}

				CTR_Lod_ArmReport(at, frames);
				printf("[CTR Native] lod report at vblank %d over %d frames\n", at, frames);
			}
			else if ((strcmp(argv[argIndex], "--present-report") == 0) && ((argIndex + 1) < argc))
			{
				// Print one frame's whole chain at that VBlank and then stop. Nothing
				// is changed by it - it reads back what each stage already decided.
				extern int g_cfg_presentReportAt;

				g_cfg_presentReportAt = atoi(argv[++argIndex]);
				printf("[CTR Native] present report at vblank %d\n", g_cfg_presentReportAt);
			}
			else if (strcmp(argv[argIndex], "--feedback-report") == 0)
			{
				// One log line per framebuffer feedback pack - VBlank,
				// rectangle, tpage - so a shot can show that its frame had one.
				// Nothing is changed by it (native_gpu.c,
				// NativeGpu_PrepareFramebufferFeedback).
				extern int g_cfg_feedbackReport;

				g_cfg_feedbackReport = 1;
				printf("[CTR Native] feedback report: one line per feedback pack, at most 500\n");
			}
			else if ((strcmp(argv[argIndex], "--msaa-at") == 0) && ((argIndex + 1) < argc))
			{
				// "V:L,V:L,..." - at VBlank V ask for anti-aliasing level L (1, 2
				// or 4) as a run value like --msaa, switched at the next frame
				// boundary the way the debug menu row is, so a switch in the
				// middle of a race can be repeated and measured.
				extern int g_cfg_msaaAtCount;
				extern int g_cfg_msaaAtVBlank[PLATFORM_MSAA_AT_MAX];
				extern int g_cfg_msaaAtLevel[PLATFORM_MSAA_AT_MAX];
				// A longer value never gets here (NativeArgs_ValueFits).
				char buffer[NATIVE_ARG_LIST_MAX];
				char *cursor;
				char *token;

				snprintf(buffer, sizeof(buffer), "%s", argv[++argIndex]);
				g_cfg_msaaAtCount = 0;
				cursor = buffer;
				while (((token = strtok(cursor, ",")) != NULL) && (g_cfg_msaaAtCount < PLATFORM_MSAA_AT_MAX))
				{
					int vblank = 0;
					int level = 0;

					cursor = NULL;
					if ((sscanf(token, "%d:%d", &vblank, &level) == 2) && ((level == 1) || (level == 2) || (level == 4)))
					{
						g_cfg_msaaAtVBlank[g_cfg_msaaAtCount] = vblank;
						g_cfg_msaaAtLevel[g_cfg_msaaAtCount] = level;
						g_cfg_msaaAtCount++;
					}
					else
					{
						printf("[CTR Native] --msaa-at wants V:L with L 1, 2 or 4 - '%s' ignored\n", token);
					}
				}
				if (token != NULL)
				{
					printf("[CTR Native] --msaa-at: more than %d pairs - '%s' and the rest ignored\n", PLATFORM_MSAA_AT_MAX, token);
				}

				// Sorted by VBlank, stable: the hook only ever looks at the next
				// pair, so a smaller VBlank behind a larger one would wait for it
				// and then undo it a frame later.
				for (int i = 1; i < g_cfg_msaaAtCount; i++)
				{
					const int vblank = g_cfg_msaaAtVBlank[i];
					const int level = g_cfg_msaaAtLevel[i];
					int j = i;

					while ((j > 0) && (g_cfg_msaaAtVBlank[j - 1] > vblank))
					{
						g_cfg_msaaAtVBlank[j] = g_cfg_msaaAtVBlank[j - 1];
						g_cfg_msaaAtLevel[j] = g_cfg_msaaAtLevel[j - 1];
						j--;
					}

					g_cfg_msaaAtVBlank[j] = vblank;
					g_cfg_msaaAtLevel[j] = level;
				}
				printf("[CTR Native] --msaa-at: %d switch(es)\n", g_cfg_msaaAtCount);
			}
			else if ((strcmp(argv[argIndex], "--res-scale") == 0) && ((argIndex + 1) < argc))
			{
				// The internal resolution factor. A setting, never a derivation - a
				// resize or a fullscreen toggle does not move it - and x4 is the
				// built-in default, so this flag exists to leave it rather than to
				// reach it.
				//
				// Read after the settings file, so a flag beats a saved value. It does
				// not write one back: a flag says what this run does, and a run that
				// silently rewrote the saved setting would change the next one.
				//
				// The range is asked for rather than printed from memory. It was
				// written here as prose - "1 to 8" - which is a third place the
				// maximum was recorded, and prose does not get recompiled.
				// "native" is a word and not a number, because it is not a
				// factor: the scene is rasterised on the window's own pixel grid
				// and the last step of the frame is one texel per pixel. Written
				// as a word so that nobody has to know which integer it is - the
				// value it maps to is asked for, not spelled here.
				const char *requested = argv[++argIndex];
				void Platform_NoteResolutionScaleFlag(void);

				// Not written back: the file keeps the value from before the flag
				// (native_platform.c, Platform_NoteResolutionScaleFlag), even when a
				// menu saves the other settings.
				Platform_NoteResolutionScaleFlag();

				if (strcmp(requested, "native") == 0)
				{
					Platform_SetResolutionScale(Platform_GetResolutionNativePosition());
				}
				else
				{
					Platform_SetResolutionScale(atoi(requested));
				}

				if (Platform_ResolutionIsNative())
				{
					printf("[CTR Native] internal resolution NATIVE - the window's own grid\n");
				}
				else
				{
					printf("[CTR Native] internal resolution x%d (1 to %d, or native)\n", Platform_GetResolutionScale(),
					       Platform_GetResolutionScaleMax());
				}
			}
			else if (strcmp(argv[argIndex], "--slow-boot") == 0)
			{
				// The whole boot back: announcer, logo pages, copyright hold,
				// crate cutscene, title cinematic.
				g_cfg_fastBoot = 0;
			}
			else if (strcmp(argv[argIndex], "--deterministic") == 0)
			{
				// MEASURING MODE: the game flow depends only on the number
				// of frames, not on their duration. VSync does not catch up VBlanks that
				// are due by wall clock, from the first call at boot on
				// (native_platform.c, g_cfg_deterministic). Audio renders as in a
				// replay per VBlank on the game thread, so that the XA state
				// that CDSYS reads also depends on VBlanks and not on the audio thread.
				// And the run belongs to its script alone: keyboard, mouse, pads and
				// window focus are sealed off (native_platform.c, the measuring mode
				// seal). The line goes to the log, which is open by now.
				// A run value like --msaa: never in ctr-settings.cfg.
				extern int g_cfg_deterministic;

				g_cfg_deterministic = 1;
				NativeAudio_SetDeterministicRenderMode(1);
				printf("[CTR Native] deterministic: VBlanks only by frames, no catching up by wall clock\n");
				Platform_Log("[CTR Input] measuring mode: keyboard, mouse, pads and window focus do not reach the game - only the script "
				             "drives it (--menu-keys, --level, --autoload-demo, --autopilot, a replay); what is kept away is counted at exit\n");
			}
			else if (strcmp(argv[argIndex], "--frame-log") == 0)
			{
				// One line "[CTR Frame]" per frame in the log: frame number, VBlank
				// counter, real frame time, VBlanks emitted; plus the
				// VSync calls of the boot phase. The header line explains the columns.
				extern int g_cfg_frameLog;

				g_cfg_frameLog = 1;
				printf("[CTR Native] frame log on\n");
			}
			else if ((strcmp(argv[argIndex], "--inject-delay") == 0) && ((argIndex + 1) < argc))
			{
				// DISTURBING SWITCH for measurements: every eighth frame and every eighth
				// VSync call of the boot phase is held for 20..80 ms; which ones is
				// decided by the seed (native_platform.c, Native_InjectHash). In
				// measuring mode this must change nothing, in classic mode it must.
				extern int g_cfg_injectDelaySeed;

				g_cfg_injectDelaySeed = atoi(argv[++argIndex]);
				printf("[CTR Native] inject delay: seed %d, every 8th frame 20..80 ms\n", g_cfg_injectDelaySeed);
			}
			else if (strcmp(argv[argIndex], "--legacy-copyright-hold") == 0)
			{
				g_cfg_legacyCopyrightHold = 1;
			}
			else if (strcmp(argv[argIndex], "--legacy-boot-logos") == 0)
			{
				g_cfg_legacyBootLogos = 1;
			}
			else if (strcmp(argv[argIndex], "--keep-intro-level") == 0)
			{
				g_cfg_skipIntroLevel = 0;
			}
			else if (strcmp(argv[argIndex], "--tracks-fixed-memory") == 0)
			{
				// The old route: a fixed megabyte behind the window, instead of the
				// number that is in the containers. Read by the tracks/ scan after
				// this loop.
				extern int g_cfg_tracksFixedMemory;

				g_cfg_tracksFixedMemory = 1;
				printf("[CTR Native] mempack extra fixed at 1 MiB - the old way\n");
			}
			else if (strcmp(argv[argIndex], "--ptr-map-unchecked") == 0)
			{
				// The old route: the pointer map is applied without any
				// reference being held against the file size.
				//
				// Default is checked. Measured on 2,459,871 pointers - the whole
				// BIGFILE, all 25 disc LEVs and 21 foreign track files -
				// the check rejects nothing that loads today. The switch is
				// there in case it does fire somewhere, so that this does not cost the whole
				// evening.
				extern int g_cfg_ptrMapChecked;

				g_cfg_ptrMapChecked = 0;
				printf("[CTR Native] pointer maps applied UNCHECKED - a bad file can write anywhere\n");
			}
			else if ((strcmp(argv[argIndex], "--menu-keys") == 0) && ((argIndex + 1) < argc))
			{
				// A KEY SEQUENCE FOR THE MENU, so that a run can open a screen
				// that otherwise only a hand opens.
				//
				// The names are those of the keys and not those of the effects:
				// "cross" and not "confirm". What a cross triggers at this
				// point is decided by the menu, and the switch is not meant to claim
				// anything about it.
				extern int g_cfg_menuKeysCount;
				extern u16 g_cfg_menuKeys[];
				extern int g_cfg_menuKeysFrom;

				static const struct
				{
					const char *name;
					u16 bit;
				} keyTable[] = {
				    {"up", 0x0010},   {"down", 0x0040},  {"left", 0x0080},     {"right", 0x0020}, {"cross", 0x4000},
				    {"circle", 0x2000}, {"square", 0x8000}, {"triangle", 0x1000}, {"start", 0x0008}, {"select", 0x0001},
				    {"l1", 0x0400},   {"r1", 0x0800},    {"l2", 0x0100},       {"r2", 0x0200},    {"none", 0x0000},
				};

				// Large enough for 256 steps with the longest name ("triangle,"):
				// 256 times 9 characters is 2304. At 1024 a long sequence ended in
				// a cut-off name, and the run aborted with "is not a
				// key" instead of running shorter.
				char keyList[4096];
				char *stepText;
				int badKey = 0;

				snprintf(keyList, sizeof(keyList), "%s", argv[++argIndex]);
				g_cfg_menuKeysCount = 0;

				for (stepText = strtok(keyList, ","); stepText != NULL; stepText = strtok(NULL, ","))
				{
					size_t i;
					int keyFound = 0;

					if (g_cfg_menuKeysCount >= 256)
					{
						printf("[CTR Native] --menu-keys: more than 256 steps, the rest is left out\n");
						break;
					}

					// Several keys at once with "+", e.g. "l1+r1+down" for the
					// retail cheat codes (L1 and R1 held, MM_CheatCodes.c:134).
					{
						u16 pressedBits = 0;
						char *rest = stepText;

						while (rest != NULL)
						{
							char *plus = strchr(rest, '+');

							if (plus != NULL)
							{
								*plus = 0;
							}

							keyFound = 0;

							for (i = 0; i < (sizeof(keyTable) / sizeof(keyTable[0])); i++)
							{
								if (strcmp(rest, keyTable[i].name) == 0)
								{
									pressedBits |= keyTable[i].bit;
									keyFound = 1;
									break;
								}
							}

							if (!keyFound)
							{
								printf("[CTR Native] --menu-keys: '%s' is not a key\n", rest);
								badKey = 1;
							}

							rest = (plus != NULL) ? (plus + 1) : NULL;
						}

						// Active low: a pressed bit is a ZERO.
						g_cfg_menuKeys[g_cfg_menuKeysCount++] = (u16)(0xffff & ~pressedBits);
					}
				}

				if (badKey)
				{
					printf("[CTR Native] --menu-keys knows: up down left right cross circle square triangle start select l1 r1 l2 r2 none, together with +\n");
					return 1;
				}

				printf("[CTR Native] menu keys: %d step(s) from vblank %d, one every 24 vblanks\n", g_cfg_menuKeysCount, g_cfg_menuKeysFrom);
			}

			else if ((strcmp(argv[argIndex], "--menu-keys-quit") == 0) && ((argIndex + 1) < argc))
			{
				// How many VBlanks after the last step the run ends.
				// Default 120. ZERO means: not at all - for long menu runs, in
				// which the question is not the menu but what the memory
				// does over three thousand frames.
				extern int g_cfg_menuKeysQuit;

				g_cfg_menuKeysQuit = atoi(argv[++argIndex]);
				printf("[CTR Native] menu keys quit: %d\n", g_cfg_menuKeysQuit);
			}
			else if ((strcmp(argv[argIndex], "--menu-keys-every") == 0) && ((argIndex + 1) < argc))
			{
				// How many VBlanks one step of the sequence lasts. Default 24.
				//
				// Measuring a pause of 25 seconds means waiting 1500 VBlanks between
				// two Start presses, and with 24 per step that would be 62 steps
				// "none". The waiting time belongs in the step length, not in the
				// number of steps.
				extern int g_cfg_menuKeysEvery;

				g_cfg_menuKeysEvery = atoi(argv[++argIndex]);

				if (g_cfg_menuKeysEvery < 1)
				{
					g_cfg_menuKeysEvery = 1;
				}

				printf("[CTR Native] menu keys: one step every %d vblank(s)\n", g_cfg_menuKeysEvery);
			}
			else if ((strcmp(argv[argIndex], "--menu-pads") == 0) && ((argIndex + 1) < argc))
			{
				// How many pads the key sequence reports, 1 to 4. Without it the
				// multiplayer routes cannot be measured, and what can be measured depends on
				// whether a controller has just fallen asleep.
				extern int g_cfg_menuPads;

				g_cfg_menuPads = atoi(argv[++argIndex]);

				if ((g_cfg_menuPads < 1) || (g_cfg_menuPads > 4))
				{
					printf("[CTR Native] --menu-pads: 1 to 4, not %d\n", g_cfg_menuPads);
					return 1;
				}

				printf("[CTR Native] menu pads: %d\n", g_cfg_menuPads);
			}

			else if ((strcmp(argv[argIndex], "--menu-keys-from") == 0) && ((argIndex + 1) < argc))
			{
				extern int g_cfg_menuKeysFrom;

				g_cfg_menuKeysFrom = atoi(argv[++argIndex]);

				if (g_cfg_menuKeysFrom < 1)
				{
					g_cfg_menuKeysFrom = 1;
				}

				printf("[CTR Native] menu keys start at vblank %d\n", g_cfg_menuKeysFrom);
			}

			else if (strcmp(argv[argIndex], "--no-sky") == 0)
			{
				// The sky stays away. A measuring switch: it answers the
				// question whether a picture fault is the sky or something else.
				extern int g_cfg_noSky;

				g_cfg_noSky = 1;
				printf("[CTR Native] sky off\n");
			}
			else if (strcmp(argv[argIndex], "--tracks") == 0)
			{
				// Stays, but does nothing any more: the folder is now read on EVERY
				// start. The switch is still accepted, because an
				// unknown switch aborts the start and the existing
				// measuring runs have it in their command line.
				printf("[CTR Native] --tracks: tracks/ is read on every start anyway\n");
			}
			else if (strcmp(argv[argIndex], "--no-tracks") == 0)
			{
				// The way back. Without it the folder is not opened, the
				// TRACKS page in the debug menu says on its first line that it is
				// off, and behind NITRO-PIT there is an empty list.
				//
				// It is there because reading the folder now puts memory behind the
				// window of the pack - and a measurement that is meant to run without this memory
				// needs a way there.
				extern int g_cfg_tracks;

				g_cfg_tracks = 0;
				printf("[CTR Native] tracks/ stays shut\n");
			}
			else if ((strcmp(argv[argIndex], "--tracks-dir") == 0) && ((argIndex + 1) < argc))
			{
				// Another container folder instead of tracks/. For the menu
				// reference: its picture depends on the whole list, not only on the
				// chosen track (platform/native_assets.c, s_nativeTrackFolder).
				NativeTrack_SetFolder(argv[++argIndex]);
				printf("[CTR Native] containers come from '%s' instead of tracks/\n", argv[argIndex]);
			}
			else if ((strcmp(argv[argIndex], "--level") == 0) && ((argIndex + 1) < argc))
			{
				// Straight into a track, hands-off, as soon as the main menu is
				// up. 0 is Dingo Canyon; the whole list is enum LevelID in
				// include/namespace_Level.h.
				//
				// This is what makes two runs comparable. A track reached by
				// hand puts the two dumps at different moments, and two
				// different moments differ almost everywhere - which looks like
				// a broken renderer and is only a broken measurement.
				extern int g_cfg_jumpLevel;

				g_cfg_jumpLevel = atoi(argv[++argIndex]);
				printf("[CTR Native] jumping to level %d\n", g_cfg_jumpLevel);
			}
			else if ((strcmp(argv[argIndex], "--level-tour") == 0) && ((argIndex + 1) < argc))
			{
				// Many tracks in one run, each one the way --level or
				// --autoload-track starts it. The list is only kept here; it is
				// read once the main menu is up, when the track folder has been
				// scanned. Run by game/DebugMenu.c, beside --level.
				extern char g_cfg_levelTour[1024];

				snprintf(g_cfg_levelTour, sizeof(g_cfg_levelTour), "%s", argv[++argIndex]);
				printf("[CTR Native] level tour: '%s'\n", g_cfg_levelTour);
			}
			else if ((strcmp(argv[argIndex], "--level-tour-frames") == 0) && ((argIndex + 1) < argc))
			{
				extern int g_cfg_levelTourFrames;

				g_cfg_levelTourFrames = atoi(argv[++argIndex]);
				if (g_cfg_levelTourFrames < 1)
				{
					g_cfg_levelTourFrames = 1;
				}
				printf("[CTR Native] level tour: %d race frame(s) each\n", g_cfg_levelTourFrames);
			}
			else if ((strcmp(argv[argIndex], "--autoload-track") == 0) && ((argIndex + 1) < argc))
			{
				// Straight into a CONTAINER track, hands-off - the NITRO-PIT row
				// without anyone pressing it. A test road, not a feature: the
				// container is chosen by file name out of tracks/, taken through
				// MM_NativeTracks_LoadRow exactly as the menu takes it, and the
				// seat it was given is then started the way --level starts a
				// disc track. Read by game/DebugMenu.c, next to --level.
				extern char g_cfg_autoloadTrack[128];

				snprintf(g_cfg_autoloadTrack, sizeof(g_cfg_autoloadTrack), "%s", argv[++argIndex]);
				printf("[CTR Native] autoloading container '%s'\n", g_cfg_autoloadTrack);
			}
			else if (strcmp(argv[argIndex], "--autoload-demo") == 0)
			{
				// The track from --autoload-track, DRIVEN: the game's own demo mode
				// (boolDemoMode) turns every seat into a bot, the player's included,
				// with no time limit. A MODEL of driving - bot line, bot items, HUD
				// off, demo camera - not a race. Read by game/DebugMenu.c.
				extern int g_cfg_autoloadDemo;

				g_cfg_autoloadDemo = 1;
				printf("[CTR Native] autoload: demo mode, the bots drive every seat\n");
			}
			else if (strcmp(argv[argIndex], "--autopilot") == 0)
			{
				// Custom Cup: a cup run without a hand.
				// Unlike --autoload-demo NOT a demo mode - that switches off HUD and
				// score. Here only the player's seat becomes a bot at race start,
				// as retail does after the finish (PlayLevel.c,
				// BOTS_Driver_Convert). The race still ends as soon as it reaches the
				// finish: counting goes by the player model, not by the
				// bot bit. Read in game/MAIN/MainInit.c.
				extern int g_cfg_autopilot;

				g_cfg_autopilot = 1;
				printf("[CTR Native] autopilot: the player's seat drives as a bot in every race\n");
			}
			else if (strcmp(argv[argIndex], "--record-preview") == 0)
			{
				// Track preview: the AI drives the invisible player seat alone, without
				// opponents, filmed from the driver camera; without a driving path the
				// path camera drives from the respawn points. platform/native_preview.c
				// records every second frame and ends the game. Read in
				// game/native_flyin.c, which also switches on --autopilot for it. Without
				// sound and without a visible window: the recording runs alongside
				// other work (Reload Studio starts it by itself after "Build
				// container"). The window is already hidden by
				// NativeArgs_ReadDisplayFlags, because it comes into being before this
				// loop.
				g_cfg_recordPreview = 1;
				SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
				printf("[CTR Native] record-preview: the AI drives the track with an invisible kart, the preview is written, then the game ends\n");
			}
			else if (strcmp(argv[argIndex], "--weapon-pool-empty") == 0)
			{
				// Weapon fix from upstream bf2ed389c: missiles, bombs,
				// shields and warpballs come from the medium stack pool (32 in
				// a race, MainInit.c). Empty at race start, every one of these
				// births fails - the route the fix protects. Read in
				// game/MAIN/MainInit.c.
				extern int g_cfg_weaponPoolEmpty;

				g_cfg_weaponPoolEmpty = 1;
				printf("[CTR Native] weapon-pool-empty: missiles, bombs, shields and warpballs cannot be born in a race\n");
			}
			else if (strcmp(argv[argIndex], "--unlock-scrapbook") == 0)
			{
				// SCRAPBOOK only appears in the
				// OPTIONS box once it is unlocked - for testing without a
				// save. The bit is set in the game progress, in every
				// menu frame (game/native_menuscreen.c), because loading the memory card rewrites the
				// progress. Whoever saves while doing so
				// unlocks it permanently (SelectProfile.c:640-648).
				extern int g_cfg_unlockScrapbook;

				g_cfg_unlockScrapbook = 1;
				printf("[CTR Native] unlock-scrapbook: SCRAPBOOK is unlocked for this session\n");
			}
			else if (strcmp(argv[argIndex], "--focus-pause") == 0)
			{
				// The pause on minimise or focus loss is off with --dev, so that
				// no window event changes a measuring or replay run
				// (native_platform.c, Platform_TakeFocusPauseWish). This switch
				// switches it on anyway - to test that it works.
				extern int g_cfg_focusPause;

				g_cfg_focusPause = 1;
				printf("[CTR Native] pause on minimise or focus loss: on, despite --dev\n");
			}
			else if ((strcmp(argv[argIndex], "--hole-probe") == 0) && ((argIndex + 1) < argc))
			{
				// MEASUREMENT: "V,X,Y" - at VBlank V (to V+4) log every primitive
				// whose box covers the pixel (X,Y) in
				// PSX coordinates. Up to four points, repeat the switch; a value
				// that is not three numbers is skipped without a message.
				extern int g_cfg_holeProbeCount;
				extern int g_cfg_holeProbe[4][3];

				if (g_cfg_holeProbeCount < 4)
				{
					int *pr = g_cfg_holeProbe[g_cfg_holeProbeCount];

					if (sscanf(argv[argIndex + 1], "%d,%d,%d", &pr[0], &pr[1], &pr[2]) == 3)
					{
						g_cfg_holeProbeCount++;
						printf("[CTR Native] hole probe at vblank %d, point (%d,%d)\n", pr[0], pr[1], pr[2]);
					}
				}
				argIndex++;
			}
			else if (strcmp(argv[argIndex], "--skip-null-tex") == 0)
			{
				// MEASURING SWITCH, default off: textured polygons whose
				// texture reference is empty (page 0, CLUT 0, all UV 0 - Sunset
				// Vista has 85 such collision quads) are not drawn
				// and are counted. Not a fix: the quads belong to the track; the
				// switch only proves that the dark floor spots come from
				// them. Read in ParsePrimitive (native_gpu.c).
				extern int g_cfg_skipNullTex;

				g_cfg_skipNullTex = 1;
				printf("[CTR Native] measuring: textured polygons with an empty texture reference are skipped\n");
			}
			else if (strcmp(argv[argIndex], "--show-platform-frames") == 0)
			{
				// MEASURING SWITCH, default off. The 123 pre-baked
				// collision frames of the Sunset Vista platforms are
				// normally DELETED from the visibility list; with
				// this switch they are SET instead. Not a fix:
				// the switch shows what the draw path would do with them
				// if they were visible - and only thereby that the
				// deleting decides anything at all. Read in
				// NativeTrackMod_HideLevelFaces (game/native_trackmod.c).
				extern int g_cfg_showPlatformFrames;

				g_cfg_showPlatformFrames = 1;
				printf("[CTR Native] measuring: the platform collision frames are made VISIBLE\n");
			}
			else if (strcmp(argv[argIndex], "--semi-two-pass") == 0)
			{
				// THE BIT-IDENTICAL COUNTERPART. Textured semi-transparency
				// of the PSX modes 0/1/3 draws in one pass since the one-pass change, with the
				// texel weight as a second blend source - and mode 0's factor 0.5
				// then travels as an 8-bit fragment output instead of a float
				// blend constant, which moves 0.3 % of the pixels by 1/255 in one
				// channel. This switch restores the two passes per primitive for
				// every mode, the form that is pixel-identical to the state before
				// that change: the hard reference for the picture comparison
				// (tools/pixelgate.py). Costs the whole gain of the one-pass change, so it is a
				// measuring switch, not a setting. Read by AddSplit (native_gpu.c).
				extern int g_cfg_semiTwoPass;

				g_cfg_semiTwoPass = 1;
				printf("[CTR Native] textured semi-transparency: two passes per primitive (the older, pixel-identical form)\n");
			}
			else if ((strcmp(argv[argIndex], "--ctr-grab") == 0) && ((argIndex + 1) < argc))
			{
				// NITRO-PIT -> CTR: like --crystal-grab for C, T, R through
				// RB_CtrLetter_LInC. The place is driven by the race (MM_NativeCtr.c).
				extern int g_cfg_ctrGrab;

				g_cfg_ctrGrab = atoi(argv[++argIndex]);
				printf("[CTR Native] ctr-grab: %d letter(s) through their collision path\n", g_cfg_ctrGrab);
			}
			else if ((strcmp(argv[argIndex], "--crystal-grab") == 0) && ((argIndex + 1) < argc))
			{
				// NITRO-PIT -> CRYSTAL: without driving paths no
				// autopilot drives. The probe takes n crystals through RB_Crystal_LInC,
				// counting and the end screen stay game code (MM_NativeCrystal.c).
				extern int g_cfg_crystalGrab;

				g_cfg_crystalGrab = atoi(argv[++argIndex]);
				printf("[CTR Native] crystal-grab: %d crystal(s) through their collision path\n", g_cfg_crystalGrab);
			}
			else if ((strcmp(argv[argIndex], "--instance-pool") == 0) && ((argIndex + 1) < argc))
			{
				// The instance pool in a race (128, MainInit_JitPoolsNew) at n
				// slots, only ever smaller - that way a full pool can be triggered
				// on purpose while loading and in a race.
				extern int g_cfg_instancePool;

				g_cfg_instancePool = atoi(argv[++argIndex]);
				printf("[CTR Native] instance pool in a race: %d\n", g_cfg_instancePool);
			}
			else if ((strcmp(argv[argIndex], "--exit-after-frames") == 0) && ((argIndex + 1) < argc))
			{
				// End the run by itself, N game frames after the race is up
				// (main menu off, loading done). A run that has to be closed by
				// hand is a run whose end nobody can reproduce.
				extern int g_cfg_exitAfterFrames;

				g_cfg_exitAfterFrames = atoi(argv[++argIndex]);
				printf("[CTR Native] exiting %d frame(s) into the race\n", g_cfg_exitAfterFrames);
			}
			else if ((strcmp(argv[argIndex], "--driver") == 0) && ((argIndex + 1) < argc))
			{
				// Who drives it. 0 is Crash, which is what the debug menu starts
				// on, so leaving this out and picking nothing in the menu are
				// the same race.
				extern int g_cfg_jumpDriver;

				g_cfg_jumpDriver = atoi(argv[++argIndex]);
				printf("[CTR Native] driver %d\n", g_cfg_jumpDriver);
			}
			else if (strcmp(argv[argIndex], "--whole-attachment-clears") == 0)
			{
				// Measuring switch: puts scissored clears back on the path they
				// took before the scissored clear, so both behaviours can be compared from
				// one binary on one anchor.
				NativeGfxVK_SetWholeAttachmentClears(1);
			}
		}

		if ((saveStateAt >= 0) || (loadStateAt >= 0))
		{
			Platform_DumpStateAt(saveStateAt, loadStateAt);
		}

		// The prefix alone as well: --level-tour names its dumps itself
		// (Platform_DumpRequest) and has no VBlank list. Without a list the
		// call sets only the prefix.
		if ((dumpList != NULL) || (dumpPrefix != NULL))
		{
			Platform_DumpConfigure(dumpList, dumpPrefix, dumpExit);
		}
	}

	if (NativePerf_ConfigureFromArgs(argc, argv) != 0)
	{
		Platform_LogFlush();
		Platform_Shutdown();
		return NativeConsole_Return(1);
	}
#endif

	// NATIVE DRIVERS on the GRAPHICS page turned the native preview on before
	// the window without --native-preview (Platform_SettingsPreloadDisplay):
	// its marker channel reports at exit as in a run with the switch. With the
	// switch the report is already registered above and this adds nothing
	// (Platform_AtExitReport ignores a second registration); without either
	// nothing is registered.
	{
		extern int g_cfg_nativePreview;

		if (g_cfg_nativePreview)
		{
			Platform_AtExitReport(NativeRenderLayer_MarkerReport);
		}
	}

	// THE FOLDER IS READ ON EVERY START (unless --no-tracks).
	//
	// This used to hang on --tracks. That worked as long as the containers could only be reached
	// through the debug menu - whoever found their way there had set the
	// switch. Now the list is in the main menu behind NITRO-PIT, and a
	// menu item that is empty depending on the command line is a broken menu item.
	//
	// Here and not later: the number of how much memory has to be put behind the window of the
	// pack comes from the containers and has to be fixed BEFORE MEMPACK_Init.
	// After that the split is fixed, and moving it
	// afterwards would mean updating every pointer in it. The player chooses the
	// track only long after that, so it cannot be the one chosen,
	// only the largest one present.
	{
		extern int g_cfg_tracks;

		if (g_cfg_tracks)
		{
			extern int g_cfg_tracksFixedMemory;
			u32 CTR_ClipStockSurchargeBytes(void);
			const int found = NativeTrack_Scan();
			// The containers plus the factor of the clip buffers that every level
			// gets (CTR_ClipBufferBytes in game/native_view.c) - both
			// lie behind the window, both drop out with --tracks-fixed-memory.
			const u32 needed = g_cfg_tracksFixedMemory ? 0x100000u : (NativeTrack_MempackExtraNeeded() + CTR_ClipStockSurchargeBytes());

			Platform_SetMempackExtra(needed);

			printf("[CTR Native] tracks/: %d container(s), mempack extra %u bytes wanted, %u in force%s\n", found, needed,
			       Platform_GetMempackExtra(), g_cfg_tracksFixedMemory ? "   (fixed, --tracks-fixed-memory)" : "   (from the containers)");
		}
	}

	Platform_InitScratchpad();
	Platform_RepairResidentPointers(0);

#if defined(CTR_INTERNAL)
	if (NativeReplayScheduler_ConfigureFromArgs(argc, argv) != 0)
	{
		Platform_LogFlush();
		Platform_Shutdown();
		return NativeConsole_Return(1);
	}
#else
	(void)argc;
	(void)argv;
#endif

	// THE CHARACTERS FOLDER IS READ ON EVERY START, independent of --no-tracks.
	//
	// Every .rldchar of characters/ (or of --chars-dir; only the one file with
	// --char) is read, checked and relocated here, in host memory and after
	// every other start step; the models never enter the MEMPACK, only the
	// draw memory they may take is added behind its window (below).
	// Under --settings-defaults no folder is read unless --chars-dir names one.
	// A broken file is skipped with a line and the start goes on. With --dev
	// the exit line of the instance counter is registered either way. Then the
	// roster of the driver select is built from the files and --dev-grid-fill.
	NativeChar_LoadRoster();

	// --dev-char-seat-files: a name that is not a loaded file of the roster
	// ends the start here (exit code 64, the message on stderr and in the log)
	// - a measuring run that left a seat retail would measure another race.
	// Without the switch this does nothing.
	if (!NativeChar_DevSeatFilesResolve())
	{
		Platform_LogFlush();
		Platform_Shutdown();
		return NativeConsole_Return((u32)NATIVE_EXIT_DEV_REQUIRED);
	}

	// The draw memory of custom models (NativeChar_DrawReserve) lies in the
	// MEMPACK like the rest of the draw memory, so the window grows by what
	// the largest load of them can take - here, after the roster and before
	// MEMPACK_Init in CTR_Main. Without a file nothing is added and nothing
	// said: the split above stays as it is.
	{
		const u32 charExtra = NativeChar_MempackExtraNeeded();

		if (charExtra != 0u)
		{
			const u32 before = Platform_GetMempackExtra();

			Platform_SetMempackExtra(before + charExtra);
			Platform_Log("[CTR Native] characters/: mempack extra +%u bytes for the draw memory of custom models, %u in force\n", charExtra,
			             Platform_GetMempackExtra());
		}
	}

	const int result = CTR_Main();

	Platform_Shutdown();

	// No call of the disc report at this place any more - it hangs on atexit,
	// because most exits do not pass by here at all. Two call routes
	// would again be the same fact in two places.

	return NativeConsole_Return(result);
}
