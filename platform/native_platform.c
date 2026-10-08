#include <platform.h>
#include <ctr_subpixel.h>

#include <macros.h>

#include "platform/native_audio.h"
#include "platform/native_gfx.h"
#include "platform/native_gpu.h"
#include "platform/native_input.h"
#include "platform/native_log.h"
#include "platform/native_perf.h"
#include "platform/native_renderer.h"
#include "platform/native_replay_scheduler.h"
#include "platform/native_savestate.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

SDL_Window *g_window = NULL;
int g_dbg_polygonSelected = 0;

extern int g_cfg_bilinearFiltering;
extern int g_dbg_emulatorPaused;
extern int g_dbg_texturelessMode;
extern int g_dbg_wireframeMode;
extern int g_windowHeight;
extern int g_windowWidth;

#define HOST_ALT_LEFT  (1 << 0)
#define HOST_ALT_RIGHT (1 << 1)
global_variable int s_hostAltKeyState = 0;
global_variable int s_platformInitialized = 0;
global_variable int s_platformBeginScene = 0;
global_variable int s_pinnedVramDisplayFrames = 0;
global_variable int s_pinnedVramDisplayCustomRect = 0;
global_variable int s_pinnedVramDisplayX = 0;
global_variable int s_pinnedVramDisplayY = 0;
global_variable int s_pinnedVramDisplayW = 0;
global_variable int s_pinnedVramDisplayH = 0;
#define NATIVE_FPS_REPORT_FRAME_WINDOW 2000
global_variable int s_fpsFrameCount = 0;
global_variable u64 s_fpsLastCounter = 0;

internal void Platform_CalcFPS(void)
{
#if defined(CTR_INTERNAL)
	const u64 freq = SDL_GetPerformanceFrequency();
	const u64 now = SDL_GetPerformanceCounter();

	if (freq == 0)
	{
		return;
	}

	if (s_fpsLastCounter == 0)
	{
		s_fpsLastCounter = now;
		s_fpsFrameCount = 0;
		return;
	}

	s_fpsFrameCount++;
	if (s_fpsFrameCount < NATIVE_FPS_REPORT_FRAME_WINDOW)
	{
		return;
	}

	if (now > s_fpsLastCounter)
	{
		const f64 elapsedSeconds = (f64)(now - s_fpsLastCounter) / (f64)freq;
		const f64 fps = (f64)s_fpsFrameCount / elapsedSeconds;

		Platform_Log("[CTR Native] FPS: %.2f (last %d frames)\n", fps, s_fpsFrameCount);
	}

	s_fpsFrameCount = 0;
	s_fpsLastCounter = now;
#endif
}

internal void Platform_GetWindowName(const char *appName, char *buffer, size_t bufferSize)
{
#ifdef CTR_INTERNAL
	snprintf(buffer, bufferSize, "%s | Internal", appName);
#else
	snprintf(buffer, bufferSize, "%s", appName);
#endif
}

internal void Platform_HandleWindowResize(void)
{
	// The event carries a size too, in logical units. Asked of the window
	// instead, so that the one place that answers this question keeps answering
	// it - the event's number and the window's number are the same today only
	// because the window has no scale factor.
	NativeRenderer_SyncWindowSize();
	NativeRenderer_ResetDevice();
}

internal void Platform_UpdateCursorVisibility(void)
{
	if (g_window == NULL)
	{
		return;
	}

	if ((SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) != 0)
	{
		SDL_HideCursor();
	}
	else
	{
		SDL_ShowCursor();
	}
}

internal void Platform_HandleFullscreenToggle(void)
{
	int fullscreen = (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) != 0;

	SDL_SetWindowFullscreen(g_window, fullscreen == 0);
	NativeRenderer_SyncWindowSize();
	Platform_UpdateCursorVisibility();
	NativeRenderer_ResetDevice();
}

// What the debug menu's VIDEO page needs, and nothing more. The toggle itself
// stays where it is - one implementation, reached by F11, by Alt+Enter and by
// the menu row, so a window that changed size has one place that noticed.
//
// Asked for here, carried out in Platform_PollHostEvents. The menu row runs
// inside the game logic, which is inside a frame the renderer has already opened
// - and the toggle ends in NativeRenderer_ResetDevice, which tears down the
// swapchain. F11 does not have this problem because the event loop is between
// frames, so the request is simply handed to the same place.
global_variable int s_fullscreenTogglePending = 0;

// The display mode the GRAPHICS page wants, or -1.
global_variable int s_fullscreenWanted = -1;

void Platform_ToggleFullscreen(void)
{
	if (g_window == NULL)
	{
		return;
	}

	s_fullscreenTogglePending = 1;
}

int Platform_IsFullscreen(void)
{
	if (g_window == NULL)
	{
		return 0;
	}

	return (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) != 0;
}

void Platform_GetWindowSize(int *outWidth, int *outHeight)
{
	if (outWidth != NULL)
	{
		*outWidth = g_windowWidth;
	}
	if (outHeight != NULL)
	{
		*outHeight = g_windowHeight;
	}
}

// The aspect, set and read as one pair. Changing it moves both halves - the
// world's horizontal projection and the shape the picture is shown in - and
// nothing else caches it, so there is no device to reset and this is safe to
// call from the game logic.
void Platform_SetAspect(int width, int height)
{
	CTR_View_SetWorldAspect(width, height);
	NativeRenderer_ApplyWorldAspectToPresentation();
}

void Platform_GetAspect(int *outWidth, int *outHeight)
{
	CTR_View_GetWorldAspect(outWidth, outHeight);
}

// WHICH SHAPE, AND WHERE THAT ANSWER COMES FROM
//
// From the display, not from the window. A window can be dragged to any size,
// and dragging it narrower must not change how much world is visible - the
// aspect decides what is rendered, the window only decides how big it is shown
// and how thick the bars around it are.
//
// The three shapes and the arithmetic that picks between them live in
// CTR_View (game/native_view.c). What is here is the SDL half: which display,
// how large it is, and when to ask again.
global_variable int s_aspectFromFlag = 0;

// Whether the windowed size was worked out from the shape or asked for by name.
// A derived one follows a later change of shape; a named one does not.
global_variable int s_windowSizeDerived = 0;

void Platform_SetAspectOverride(int width, int height)
{
	Platform_SetAspect(width, height);
	s_aspectFromFlag = 1;
}

// The display the window is on, and its size in pixels.
//
// Before the window exists there is no such display, and the primary one is the
// best available guess - but this is only called after it exists, so the guess
// is a fallback for a failed query rather than the normal path.
//
// Desktop mode, not current mode: the current mode is what a fullscreen window
// has put the display into, and reading the aspect out of that would make the
// detection depend on its own last answer.
internal int Platform_DisplayPixelSize(int *outWidth, int *outHeight)
{
	SDL_DisplayID display = 0;
	const SDL_DisplayMode *mode = NULL;

	if (g_window != NULL)
	{
		display = SDL_GetDisplayForWindow(g_window);
	}

	if (display == 0)
	{
		display = SDL_GetPrimaryDisplay();
	}

	if (display == 0)
	{
		return 0;
	}

	mode = SDL_GetDesktopDisplayMode(display);
	if (mode == NULL)
	{
		return 0;
	}

	// w and h are in screen coordinates; the pixels are that times pixel_density.
	// The density is one number for both axes, so it cancels out of a ratio and is
	// deliberately not applied - applying it would only add a rounding.
	*outWidth = mode->w;
	*outHeight = mode->h;
	return 1;
}

// A window in the shape that was detected, at about `baseWidth` across and
// inside the display it will stand on.
//
// Derived, not tabulated: three window sizes written down beside three aspects
// would be the same fact in two places again, and the ratio is the fact.
//
// A 43:18 picture in a 4:3 window is a picture made of bars, which is no use to
// test with - that is what this exists for.
#define NATIVE_WINDOW_BASE_WIDTH 1280

// How much of the display a derived window may take. A 1280x960 window does not
// fit on a 1024x768 panel, and a window larger than its screen cannot be moved
// to where its title bar is.
#define NATIVE_WINDOW_DISPLAY_NUMERATOR   8
#define NATIVE_WINDOW_DISPLAY_DENOMINATOR 10

internal void Platform_WindowedSizeForAspect(int *outWidth, int *outHeight)
{
	int aspectW = 4;
	int aspectH = 3;
	int displayW = 0;
	int displayH = 0;
	int width = NATIVE_WINDOW_BASE_WIDTH;
	int height;

	CTR_View_GetWorldAspect(&aspectW, &aspectH);

	if (Platform_DisplayPixelSize(&displayW, &displayH))
	{
		const int widthCap = (displayW * NATIVE_WINDOW_DISPLAY_NUMERATOR) / NATIVE_WINDOW_DISPLAY_DENOMINATOR;
		const int heightCap = (displayH * NATIVE_WINDOW_DISPLAY_NUMERATOR) / NATIVE_WINDOW_DISPLAY_DENOMINATOR;

		if ((widthCap > 0) && (width > widthCap))
		{
			width = widthCap;
		}

		// The height follows from the width, so a height that does not fit has to
		// come back as a narrower window rather than a squashed one.
		if ((heightCap > 0) && (((width * aspectH) / aspectW) > heightCap))
		{
			width = (heightCap * aspectW) / aspectH;
		}
	}

	if (width < 320)
	{
		width = 320;
	}

	// Rounded rather than truncated. The presentation letterboxes whatever is
	// left over, so this is a size and not a promise - one pixel out costs one
	// pixel of bar.
	height = ((width * aspectH) + (aspectW / 2)) / aspectW;
	if (height < 240)
	{
		height = 240;
	}

	*outWidth = width;
	*outHeight = height;
}

// THE SETTINGS THAT SURVIVE A RESTART
//
// One file, one reader, one writer, one lock. It was ctr-view.cfg and held the
// three sets of view settings only; the internal resolution factor wanted the
// same treatment, and a second file would have been a second everything.
//
// <section> <key> <value>, one per line, written out in full every time. A file
// that only holds what differs from stock reads well and fails badly: the day a
// stock value changes, every file already written silently means something
// else.
//
// A section is either the name of a shape - then the key is a view setting - or
// "video", for the settings that are not per shape. Both spellings come from
// the same tables the menu prints from, so a row added to either appears in the
// file without anything here being told about it. A line naming a section or a
// key that no longer exists is logged and skipped - not an error, because an
// old file must not stop the game, and not silent, because a setting that
// quietly did not arrive is worse than one that plainly did not.
#define NATIVE_SETTINGS_PATH    "ctr-settings.cfg"
#define NATIVE_SETTINGS_SECTION_VIDEO "video"

// The settings that are not per shape. A table with a getter and a setter per
// row rather than a branch per key, so saving and loading cannot drift apart -
// and so the next one costs a row.
struct NativeVideoSetting
{
	const char *key;
	void (*set)(int value);
	int (*get)(void);
};

// THE GRAPHICS PAGE: Display mode, Aspect ratio, Resolution, Anti-aliasing
// in the OPTIONS box (game/native_graphics.c).
//
// What is in the file is always the choice from the menu or from the file -
// never a command-line switch. --fullscreen/--windowed, --aspect, --res-scale
// and --msaa apply to the session and are not written.
//
// Fullscreen and aspect ratio are read BEFORE the window
// (Platform_SettingsPreloadDisplay), the rest afterwards as before.
global_variable int s_fullscreenSetting = 1; // video fullscreen 0|1
global_variable int s_aspectSetting = 0;     // video aspect 0 Auto, 1 4:3, 2 16:9, 3 21:9 (43:18)
global_variable int s_aspectPending = -1;    // chosen, applies at the next load
global_variable int s_resScaleFromFlag = 0;
global_variable int s_resScaleFile = 4;

internal void Platform_StoreFullscreenSetting(int value)
{
	s_fullscreenSetting = (value != 0);
}

int Platform_GetFullscreenSetting(void)
{
	return s_fullscreenSetting;
}

internal void Platform_StoreAspectSetting(int value)
{
	s_aspectSetting = ((value >= 0) && (value <= 3)) ? value : 0;
}

int Platform_GetAspectSetting(void)
{
	return s_aspectSetting;
}

int Platform_GetAspectPending(void)
{
	return s_aspectPending;
}

// The resolution for the file: with --res-scale the number from before.
internal int Platform_GetResolutionScaleForFile(void)
{
	return s_resScaleFromFlag ? s_resScaleFile : Platform_GetResolutionScaleSetting();
}

// main.c calls this before --res-scale: the value from the file stays the file's.
void Platform_NoteResolutionScaleFlag(void)
{
	if (!s_resScaleFromFlag)
	{
		s_resScaleFile = Platform_GetResolutionScaleSetting();
		s_resScaleFromFlag = 1;
	}
}

// The SETTING getter, not the effective one: what is written to disk has to be
// what was asked for, or a factor that was clamped on one machine comes back
// as the clamped value on the next.
global_variable const struct NativeVideoSetting s_videoSettings[] = {
    {"resscale", Platform_SetResolutionScale, Platform_GetResolutionScaleForFile},
    {"fullscreen", Platform_StoreFullscreenSetting, Platform_GetFullscreenSetting},
    {"aspect", Platform_StoreAspectSetting, Platform_GetAspectSetting},
    {"lod", CTR_Lod_SetMask, CTR_Lod_Mask},
    // A file that does not mention this key leaves the built-in value, which is
    // on - so an old config cannot switch the mechanism back on by omission.
    // That is the whole reason this is a key of its own and not a fifth bit in
    // `lod`: a bit in a mask read out of an old file arrives as a zero.
    {"nolod", CTR_Lod_SetNoneMode, CTR_Lod_NoneMode},
    {"dither", Platform_SetDither, Platform_GetDither},
    // The anti-aliasing level asked for in the menu, 1/2/4. --msaa beats it for
    // one run and is never written here, nor is --msaa-at - see
    // Platform_SetMsaaRun.
    {"msaa", Platform_SetMsaaSetting, Platform_GetMsaaSetting},
};

#define NATIVE_VIDEO_SETTING_COUNT ((int)(sizeof(s_videoSettings) / sizeof(s_videoSettings[0])))

// The lock. A measuring run must not read a file somebody left tuned, and must
// not write one either - a tool that changes what it is measuring costs the
// comparison.
global_variable int s_settingsLocked = 0;

void Platform_SetSettingsLocked(int locked)
{
	s_settingsLocked = (locked != 0);
}

int Platform_SettingsLocked(void)
{
	return s_settingsLocked;
}

void Platform_SettingsSave(void)
{
	FILE *file;
	int mode;
	int setting;

	if (s_settingsLocked != 0)
	{
		return;
	}

	file = fopen(NATIVE_SETTINGS_PATH, "w");
	if (file == NULL)
	{
		Platform_LogWarn("[CTR Native] settings: cannot write '%s'\n", NATIVE_SETTINGS_PATH);
		return;
	}

	fprintf(file, "# CTR Reload - settings that survive a restart\n");
	fprintf(file, "# <area> <name> <value>\n");

	for (setting = 0; setting < NATIVE_VIDEO_SETTING_COUNT; setting++)
	{
		fprintf(file, "%s %s %d\n", NATIVE_SETTINGS_SECTION_VIDEO, s_videoSettings[setting].key,
					s_videoSettings[setting].get());
	}

	for (mode = 0; mode < CTR_View_ModeCount(); mode++)
	{
		for (setting = 0; setting < CTR_View_SettingCount(); setting++)
		{
			fprintf(file, "%s %s %d\n", CTR_View_ModeName(mode), CTR_View_SettingKey(setting),
						CTR_View_SettingValue(mode, setting));
		}
	}

	fclose(file);
}

void Platform_SettingsLoad(void)
{
	char line[128];
	char sectionName[24];
	char keyName[24];
	FILE *file;
	int applied = 0;
	int skipped = 0;

	if (s_settingsLocked != 0)
	{
		Platform_Log("[CTR Native] settings: locked, defaults throughout\n");
		return;
	}

	file = fopen(NATIVE_SETTINGS_PATH, "r");
	if (file == NULL)
	{
		// No file is the normal first run, not a fault.
		return;
	}

	while (fgets(line, sizeof(line), file) != NULL)
	{
		int value = 0;
		int mode;
		int setting;
		int foundMode = -1;
		int foundSetting = -1;

		if ((line[0] == '#') || (line[0] == '\n') || (line[0] == '\r'))
		{
			continue;
		}

		if (sscanf(line, "%23s %23s %d", sectionName, keyName, &value) != 3)
		{
			skipped++;
			continue;
		}

		// The video section first, because it is the one that is not a shape name.
		if (strcmp(sectionName, NATIVE_SETTINGS_SECTION_VIDEO) == 0)
		{
			for (setting = 0; setting < NATIVE_VIDEO_SETTING_COUNT; setting++)
			{
				if (strcmp(keyName, s_videoSettings[setting].key) == 0)
				{
					// Clamped inside the setter, which is the only way in. A file
					// edited by hand is exactly what that clamp is for.
					s_videoSettings[setting].set(value);
					foundSetting = setting;
					break;
				}
			}

			if (foundSetting < 0)
			{
				Platform_LogWarn("[CTR Native] settings: 'video %s' names nothing, skipped\n", keyName);
				skipped++;
				continue;
			}

			applied++;
			continue;
		}

		for (mode = 0; mode < CTR_View_ModeCount(); mode++)
		{
			if (strcmp(sectionName, CTR_View_ModeName(mode)) == 0)
			{
				foundMode = mode;
				break;
			}
		}

		for (setting = 0; setting < CTR_View_SettingCount(); setting++)
		{
			if (strcmp(keyName, CTR_View_SettingKey(setting)) == 0)
			{
				foundSetting = setting;
				break;
			}
		}

		if ((foundMode < 0) || (foundSetting < 0))
		{
			Platform_LogWarn("[CTR Native] settings: '%s %s' names nothing, skipped\n", sectionName, keyName);
			skipped++;
			continue;
		}

		CTR_View_SetSettingValue(foundMode, foundSetting, value);
		applied++;
	}

	fclose(file);
	Platform_Log("[CTR Native] settings: %d value(s) from '%s', %d skipped\n", applied, NATIVE_SETTINGS_PATH, skipped);
}

internal const int s_aspectSettingShape[4][2] = {{0, 0}, {4, 3}, {16, 9}, {43, 18}};

// Before the window (Platform_Init): only "video fullscreen" and "video aspect".
// A command-line switch wins; under --settings-defaults nothing is
// read.
internal void Platform_SettingsPreloadDisplay(int *fullscreen)
{
	extern int g_cfg_fullscreenFromFlag;
	char line[128];
	char sectionName[24];
	char keyName[24];
	int value;
	FILE *file;

	if (s_settingsLocked != 0)
	{
		return;
	}

	file = fopen(NATIVE_SETTINGS_PATH, "r");
	if (file == NULL)
	{
		return;
	}

	while (fgets(line, sizeof(line), file) != NULL)
	{
		if ((sscanf(line, "%23s %23s %d", sectionName, keyName, &value) != 3) || (strcmp(sectionName, NATIVE_SETTINGS_SECTION_VIDEO) != 0))
		{
			continue;
		}

		if (strcmp(keyName, "fullscreen") == 0)
		{
			Platform_StoreFullscreenSetting(value);
		}
		else if (strcmp(keyName, "aspect") == 0)
		{
			Platform_StoreAspectSetting(value);
		}
	}

	fclose(file);

	if (!g_cfg_fullscreenFromFlag)
	{
		*fullscreen = s_fullscreenSetting;
	}

	if (!s_aspectFromFlag && (s_aspectSetting != 0))
	{
		Platform_SetAspectOverride(s_aspectSettingShape[s_aspectSetting][0], s_aspectSettingShape[s_aspectSetting][1]);
	}

	Platform_Log("[CTR Native] settings: display mode %s%s, aspect ratio %d%s\n", s_fullscreenSetting ? "fullscreen" : "windowed",
	             g_cfg_fullscreenFromFlag ? " (the command line wins this run)" : "", s_aspectSetting,
	             s_aspectFromFlag && (s_aspectSetting == 0) ? " (the command line wins this run)" : "");
}

// The four setters of the GRAPHICS page. Each one saves at once; under
// --settings-defaults Platform_SettingsSave does nothing.
void Platform_GraphicsSetFullscreen(int on)
{
	s_fullscreenSetting = (on != 0);
	s_fullscreenWanted = s_fullscreenSetting;
	Platform_SettingsSave();
	Platform_Log("[CTR Graphics] display mode: %s\n", on ? "fullscreen" : "windowed");
}

// The aspect ratio only applies at the next load of a level: the
// world view (pb->rect) and the draw memory are sized per level
// (MainInit.c), a change in the middle of a level would leave holes.
void Platform_GraphicsSetAspect(int setting)
{
	Platform_StoreAspectSetting(setting);
	s_aspectPending = s_aspectSetting;
	Platform_SettingsSave();
	Platform_Log("[CTR Graphics] aspect ratio: %d - applies when the next level loads\n", s_aspectSetting);
}

void Platform_ApplyPendingAspect(void)
{
	if (s_aspectPending < 0)
	{
		return;
	}

	if (s_aspectPending == 0)
	{
		s_aspectFromFlag = 0;
		Platform_DetectDisplayAspect("setting auto");
	}
	else
	{
		Platform_SetAspectOverride(s_aspectSettingShape[s_aspectPending][0], s_aspectSettingShape[s_aspectPending][1]);
	}

	Platform_Log("[CTR Graphics] aspect ratio %d applied at level load\n", s_aspectPending);
	s_aspectPending = -1;
}

void Platform_GraphicsSetResolution(int scale)
{
	Platform_SetResolutionScale(scale);
	s_resScaleFile = Platform_GetResolutionScaleSetting();
	s_resScaleFromFlag = 0;
	Platform_SettingsSave();
	Platform_Log("[CTR Graphics] resolution: %d\n", s_resScaleFile);
}

// Through StepMsaa, because that also cancels --msaa for the rest of the session.
void Platform_GraphicsSetMsaa(int samples)
{
	int guard;

	for (guard = 0; (guard < 3) && (Platform_GetMsaaRequested() != samples); guard++)
	{
		Platform_StepMsaa();
	}

	Platform_SettingsSave();
	Platform_Log("[CTR Graphics] anti-aliasing: %s\n", Platform_MsaaName(samples));
}

void Platform_DetectDisplayAspect(const char *why)
{
	int pixelWidth = 0;
	int pixelHeight = 0;
	int ratio;
	int mode;
	int aspectW = 0;
	int aspectH = 0;

	if (s_aspectFromFlag != 0)
	{
		Platform_Log("[CTR Native] display %s: aspect held by the command line, not detected\n", (why != NULL) ? why : "start");
		return;
	}

	if (!Platform_DisplayPixelSize(&pixelWidth, &pixelHeight))
	{
		Platform_Log("[CTR Native] display %s: SDL names no display, aspect stays %s\n",
					(why != NULL) ? why : "start", CTR_View_ModeName(CTR_View_ActiveMode()));
		return;
	}

	ratio = CTR_View_RatioMilli(pixelWidth, pixelHeight);
	mode = CTR_View_NearestMode(pixelWidth, pixelHeight);

	if (mode < 0)
	{
		Platform_Log("[CTR Native] display %s: %dx%d makes no ratio, aspect stays %s\n",
					(why != NULL) ? why : "start", pixelWidth, pixelHeight, CTR_View_ModeName(CTR_View_ActiveMode()));
		return;
	}

	// Measured, computed, chosen - the three numbers the decision was made from,
	// in the order it was made. A log line that only named the winner would leave
	// a wrong pick indistinguishable from a wrong measurement.
	Platform_Log("[CTR Native] display %s: %dx%d ratio %d.%03d -> aspect %s%s\n",
				(why != NULL) ? why : "start", pixelWidth, pixelHeight, ratio / 1000, ratio % 1000,
				CTR_View_ModeName(mode), (mode == CTR_View_ActiveMode()) ? " (unchanged)" : "");

	if (mode == CTR_View_ActiveMode())
	{
		return;
	}

	CTR_View_ModeAspect(mode, &aspectW, &aspectH);
	Platform_SetAspect(aspectW, aspectH);
}

// The internal resolution factor. TWO questions, and they are not the same:
//
//   Platform_GetResolutionScale        what is in force - what every rectangle
//                                      in the frame is multiplied by. To SHOW.
//   Platform_GetResolutionScaleSetting what was asked for. To STEP and to SAVE.
//
// They differ only where the asked-for factor would need a target larger than a
// texture may be. Today nothing reaches that - 512x216 at x8 is 4096x1728,
// half the limit - so the two are the same number, which is exactly why the
// difference is worth writing down rather than discovering later.
void Platform_SetResolutionScale(int scale)
{
	NativeRenderer_SetResolutionScale(scale);
}

int Platform_GetResolutionScale(void)
{
	return NativeRenderer_GetEffectiveResolutionScale();
}

int Platform_GetResolutionScaleSetting(void)
{
	return NativeRenderer_GetResolutionScale();
}

int Platform_GetResolutionScaleMax(void)
{
	return NativeRenderer_GetMaxResolutionScale();
}

// The position past the last factor. Two questions again, and this time they are
// "is it in force" and "what value reaches it" - a menu needs both and may
// invent neither.
int Platform_ResolutionIsNative(void)
{
	return NativeRenderer_ResolutionIsNative();
}

int Platform_GetResolutionNativePosition(void)
{
	return NativeRenderer_GetNativeResolutionPosition();
}

void Platform_GetRenderTargetSize(int *outWidth, int *outHeight)
{
	NativeRenderer_GetMainTargetSize(outWidth, outHeight);
}

// The page store, and how much of it a level actually asks for.
//
// The tile count is the answer to "how much air is in the atlas": a tile is
// only paid for once some draw names its page and its depth, and a level that
// never touches a page never pays for it.
void Platform_SetPageScale(int scale)
{
	NativeRenderer_SetPageScale(scale);
}

int Platform_GetPageScale(void)
{
	return NativeRenderer_GetPageScale();
}

void Platform_GetPageStoreUse(int *outTilesUsed, int *outTilesTotal, int *outKiB)
{
	NativeRenderer_GetPageStoreUse(outTilesUsed, outTilesTotal, outKiB);
}

int Platform_GetDither(void)
{
	return NativeRenderer_GetDither();
}

void Platform_SetDither(int mode)
{
	NativeRenderer_SetDither(mode);
}

void Platform_StepDither(void)
{
	NativeRenderer_StepDither();
}

const char *Platform_DitherName(void)
{
	return NativeRenderer_DitherName();
}

// ANTI-ALIASING, OFF / 2x / 4x - the samples of the main target. Three numbers
// again, like the resolution factor, and one more than there:
//
//   Platform_GetMsaaSetting   what the file holds and the menu chose. To SAVE.
//   Platform_GetMsaaRequested what is asked for now: --msaa if it was given,
//                             otherwise the setting. To STEP from.
//   Platform_GetMsaa          what is in force - the device may have stepped a
//                             count down. To SHOW.
//
// The flag differs from --res-scale in one way that is on purpose: it is never
// written back. A save from any menu row writes the setting, so a measuring run
// with --msaa 1 that touches DITHER does not leave the next ordinary start at
// OFF. Choosing a level in the menu is a choice, and it lifts the flag for the
// rest of the run.
//
// A new level is asked for, not done: it takes effect at the next frame
// boundary (NativeRenderer_ApplyMsaa), never in the middle of a pass.
void Platform_SetMsaaSetting(int samples)
{
	NativeRenderer_SetMsaaSetting(samples);
}

int Platform_GetMsaaSetting(void)
{
	return NativeRenderer_GetMsaaSetting();
}

void Platform_SetMsaaRun(int samples)
{
	NativeRenderer_SetMsaaRun(samples);
}

void Platform_StepMsaa(void)
{
	NativeRenderer_StepMsaa();
}

int Platform_GetMsaaRequested(void)
{
	return NativeRenderer_GetMsaaRequested();
}

int Platform_GetMsaa(void)
{
	return NativeRenderer_GetMsaa();
}

const char *Platform_MsaaName(int samples)
{
	return NativeRenderer_MsaaName(samples);
}

int Platform_GetPageRowRange(void)
{
	return NativeRenderer_GetPageRowRange();
}

void Platform_SetPageRowRange(int on)
{
	NativeRenderer_SetPageRowRange(on);
}

int Platform_GetPagePreload(void)
{
	return NativeRenderer_GetPagePreload();
}

void Platform_SetPagePreload(int on)
{
	NativeRenderer_SetPagePreload(on);
}

// The size the frame is actually rendered at - the same two numbers
// NativeRenderer_BindMainRenderTarget sizes the render target from, so what the
// VIDEO page reports cannot drift away from what the renderer does.
//
// Since the canvas became wide this is the canvas and no longer the display environment. The
// two were the same number until then; the render target is created from the canvas,
// so this line has to follow it, otherwise the VIDEO page reports at
// 43:18 512 for a picture that is computed 918 columns wide. At 4:3
// both are 512.
void Platform_GetFrameSize(int *outWidth, int *outHeight)
{
	if (outWidth != NULL)
	{
		*outWidth = CTR_Canvas_ActiveWidth();
	}
	if (outHeight != NULL)
	{
		*outHeight = activeDispEnv.disp.h;
	}
}

internal void Platform_UpdateHostAltKeyState(const s32 key, const s8 down)
{
	s32 altKeyBit = 0;

	if (key == SDL_SCANCODE_LALT)
	{
		altKeyBit = HOST_ALT_LEFT;
	}
	else if (key == SDL_SCANCODE_RALT)
	{
		altKeyBit = HOST_ALT_RIGHT;
	}

	if (altKeyBit == 0)
	{
		return;
	}

	if (down != 0)
	{
		s_hostAltKeyState |= altKeyBit;
	}
	else
	{
		s_hostAltKeyState &= ~altKeyBit;
	}
}

#if defined(CTR_INTERNAL)
// A picture of the INTERNAL frame, at whatever size the main target has - 512x216
// times the factor, or the window's own grid at NATIVE. Asked of the target
// rather than worked out, so this needs no edit when that changes again.
//
// It said "window" and it read the window, and the window cannot be read - the
// swapchain image is not one of the backend's textures, NativeGfxVK_ReadPixels
// refuses and says so in the log. This wrote the untouched buffer to disk
// anyway and announced success. Four shots that should have differed came out
// byte-identical black, and nothing about the file said which of "a black
// frame" and "no read at all" it was.
//
// So: the main target, which IS readable, and a verdict line that names the
// size and says when the picture is one single colour. A measuring tool that
// cannot tell its own failure from a result is not a measuring tool.
//
// The name is an argument. It was always SCREENSHOT.BMP - the trap --log fell
// into: two runs meant to be compared, and the second erases the first.
internal void Platform_TakeScreenshot(const char *path)
{
	const char *name = (path != NULL) ? path : "SCREENSHOT.BMP";
	int width = 0;
	int height = 0;
	int targetW = 0;
	int targetH = 0;
	int bytes;
	u8 *pixels;

	NativeRenderer_GetMainTargetSize(&targetW, &targetH);
	bytes = targetW * targetH * 4;

	if (bytes <= 0)
	{
		Platform_LogWarn("[CTR Native] shot %s: no target yet\n", name);
		return;
	}

	pixels = (u8 *)malloc((size_t)bytes);
	if (pixels == NULL)
	{
		Platform_LogWarn("[CTR Native] shot %s: %d bytes refused\n", name, bytes);
		return;
	}

	if (!NativeRenderer_ReadMainTarget(pixels, bytes, &width, &height))
	{
		Platform_LogWarn("[CTR Native] shot %s: NOTHING WAS READ, no file written\n", name);
		free(pixels);
		return;
	}

	// TWO BUGS THAT HAD ALWAYS BEEN IN EVERY CAPTURE.
	//
	// FIRST, THE CHANNELS. NativeGfx_ReadPixels returns for
	// NATIVE_GFX_TEXFMT_BGRA8 the bytes in the order B, G, R, A - that is how
	// they lie in memory. SDL_PIXELFORMAT_BGRA8888 however is a PACKED
	// format: it describes the 32-bit word from top to bottom, and on a
	// little-endian machine that means A, R, G, B in memory. So SDL read
	// every channel shifted by one place. The name that really says "these four
	// bytes in this order" is BGRX32 - SDL resolves it itself, depending on the
	// machine, to XRGB8888 or BGRX8888.
	//
	// X and not A: the alpha column of the main target is 0. Read as alpha
	// the picture would be completely transparent, and a viewer that respects alpha
	// would show nothing.
	//
	// SECOND, THE ROW ORDER. NativeRenderer_ReadMainTarget returns the
	// rows FROM BOTTOM TO TOP - row 0 is the bottom one of the picture, because
	// the main target is rendered mirrored. SDL_SaveBMP handles the
	// BMP standard correctly, it was just given an already mirrored buffer. That is why
	// it is turned back here before writing.
	//
	// Measured again and not assumed: in a picture taken with the same read
	// routine, the dark rows of the menu box began
	// at y 58 instead of at the reported 64 - and 216 minus 158 is 58. A
	// reasoning that stood here earlier blamed SDL; that was wrong, the
	// code was right by accident.
	{
		const size_t stride = (size_t)width * 4;
		u8 *swap = (u8 *)malloc(stride);

		if (swap != NULL)
		{
			int row;

			for (row = 0; row < (height / 2); row++)
			{
				u8 *a = pixels + ((size_t)row * stride);
				u8 *b = pixels + ((size_t)(height - 1 - row) * stride);

				memcpy(swap, a, stride);
				memcpy(a, b, stride);
				memcpy(b, swap, stride);
			}

			free(swap);
		}
		else
		{
			Platform_LogWarn("[CTR Native] shot %s: %d bytes refused for the flip - the picture is upside down\n",
			                 name, (int)stride);
		}
	}

	{
		SDL_Surface *surface = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_BGRX32, pixels, width * 4);

		// A file that does not open (a missing folder, or a path past MAX_PATH
		// without long paths on Windows) is said, not announced as a shot.
		const int saved = (surface != NULL) && SDL_SaveBMP(surface, name);

		if (surface != NULL)
		{
			SDL_DestroySurface(surface);
		}

		if (!saved)
		{
			Platform_LogWarn("[CTR Native] shot %s: NOT WRITTEN (%s)\n", name, SDL_GetError());
			free(pixels);
			return;
		}
	}

	{
		// One colour throughout is a legitimate picture and also what a broken
		// read looks like. Said out loud either way, so the next four identical
		// files are noticed here rather than after they were compared.
		int uniform = 1;
		int i;

		for (i = 4; i < bytes; i += 4)
		{
			if ((pixels[i] != pixels[0]) || (pixels[i + 1] != pixels[1]) || (pixels[i + 2] != pixels[2]))
			{
				uniform = 0;
				break;
			}
		}

		Platform_Log("[CTR Native] shot %dx%d (internal frame, before the last step) -> %s%s\n", width, height, name,
		             uniform ? "   WARNING: one colour throughout" : "");
	}

	free(pixels);
}

// A shot at a named VBlank, so two runs catch the same moment of an animating
// screen. A hand on F12 twice does not.
int g_cfg_shotAt = 0;
char g_cfg_shotName[PLATFORM_ARG_PATH_MAX] = {0};
// Several snapshots per run. g_cfg_shotAt is always the next one due;
// with more than one entry the file is called <name without .bmp>-<vblank>.bmp.
int g_cfg_shotList[PLATFORM_SHOT_MAX];
int g_cfg_shotCount = 0;
global_variable int s_shotNext = 0;
#endif

internal void Platform_HandleKey(int key, char down)
{
	if (down == 0)
	{
		SubmitName_UseKeyboard(0);
	}
	else
	{
		SubmitName_UseKeyboard(key);
	}

#ifdef CTR_INTERNAL
	if (!down)
	{
		// The debug keys - quick states F5/F8, replay F9/F10, VRAM F7,
		// display F1-F3, polygon choice - only with --dev. F12 (screenshot)
		// stays: testers need it for reports.
		extern int g_cfg_dev;

		if ((key != SDL_SCANCODE_F12) && !g_cfg_dev)
		{
			return;
		}

		switch (key)
		{
		case SDL_SCANCODE_F1:
			g_dbg_wireframeMode ^= 1;
			Platform_LogWarn("[CTR Native] wireframe mode: %d\n", g_dbg_wireframeMode);
			break;

		case SDL_SCANCODE_F2:
			g_dbg_texturelessMode ^= 1;
			Platform_LogWarn("[CTR Native] textureless mode: %d\n", g_dbg_texturelessMode);
			break;
		case SDL_SCANCODE_UP:
		case SDL_SCANCODE_DOWN:
			if (g_dbg_emulatorPaused)
			{
				g_dbg_polygonSelected += (key == SDL_SCANCODE_UP) ? 3 : -3;
			}
			break;
		case SDL_SCANCODE_F9:
			if (NativeReplayScheduler_RequestStart() != 0)
			{
				break;
			}
			break;
		case SDL_SCANCODE_F10:
			NativeReplayScheduler_RequestStop();
			break;
		case SDL_SCANCODE_F7:
			Platform_LogWarn("[CTR Native] saving VRAM.TGA\n");
			NativeRenderer_SaveVRAM("VRAM.TGA", 0, 0, VRAM_WIDTH, VRAM_HEIGHT, 1);
			break;
		case SDL_SCANCODE_F12:
			Platform_LogWarn("[CTR Native] Saving screenshot...\n");
			Platform_TakeScreenshot(NULL);
			break;
		case SDL_SCANCODE_F3:
			g_cfg_bilinearFiltering ^= 1;
			Platform_LogWarn("[CTR Native] filtering mode: %d\n", g_cfg_bilinearFiltering);
			break;
		case SDL_SCANCODE_F5:
			NativeSaveState_RequestSave();
			break;
		case SDL_SCANCODE_F8:
			NativeSaveState_RequestLoad();
			break;
		}
	}
#endif
}


//========================================================================================
// WHEN IT CRASHES, IT SHOULD SAY WHERE
//========================================================================================
//
// Without this a crash is only "it just crashes", and every guess at the place
// costs a run. It was built while the container loader was being hunted: the
// load path had been shown to be clean, the race afterwards died before frame
// 120, and there was no means of naming the place. This here is the means, and
// it is not limited to containers: every crash of the game writes its stack
// into the log.
//
// SetUnhandledExceptionFilter and not AddVectoredExceptionHandler: the
// vectored handler sees EVERY exception, also the ones that somebody further up catches and
// expects. What is of interest here is the one that nobody catches.
//
// The symbol names come from the .pdb that the release build writes along
// (/Zi and /DEBUG in CMakeLists.txt). If it is missing, the
// addresses remain - and an address with module and offset is still more than
// nothing.

#if defined(_WIN32)

#include <dbghelp.h>
#include <signal.h>
#include <stdlib.h>

#define NATIVE_CRASH_MAX_FRAMES 48

global_variable LONG s_crashHandled = 0;

internal const char *Platform_CrashName(DWORD code)
{
	switch (code)
	{
	case EXCEPTION_ACCESS_VIOLATION:
		return "access violation";
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
		return "array bounds exceeded";
	case EXCEPTION_DATATYPE_MISALIGNMENT:
		return "datatype misalignment";
	case EXCEPTION_FLT_DIVIDE_BY_ZERO:
		return "float divide by zero";
	case EXCEPTION_ILLEGAL_INSTRUCTION:
		return "illegal instruction";
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
		return "integer divide by zero";
	case EXCEPTION_PRIV_INSTRUCTION:
		return "privileged instruction";
	case EXCEPTION_STACK_OVERFLOW:
		return "stack overflow";
	default:
		return "exception";
	}
}

internal void Platform_CrashLogFrame(HANDLE process, DWORD64 address, int index)
{
	// SYMBOL_INFO carries the name along behind it, hence the buffer.
	u8 storage[sizeof(SYMBOL_INFO) + 512];
	SYMBOL_INFO *symbol = (SYMBOL_INFO *)storage;
	IMAGEHLP_LINE64 line;
	DWORD64 displacement = 0;
	DWORD lineDisplacement = 0;

	memset(storage, 0, sizeof(storage));
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
	symbol->MaxNameLen = 500;

	memset(&line, 0, sizeof(line));
	line.SizeOfStruct = sizeof(line);

	if (SymFromAddr(process, address, &displacement, symbol))
	{
		if (SymGetLineFromAddr64(process, address, &lineDisplacement, &line))
		{
			Platform_Log("[CTR Crash]  %2d  %s + %llu   (%s:%lu)\n", index, symbol->Name, (unsigned long long)displacement, line.FileName,
			             (unsigned long)line.LineNumber);
		}
		else
		{
			Platform_Log("[CTR Crash]  %2d  %s + %llu\n", index, symbol->Name, (unsigned long long)displacement);
		}
	}
	else
	{
		Platform_Log("[CTR Crash]  %2d  0x%08llx   (no symbol - is the .pdb next to the exe?)\n", index, (unsigned long long)address);
	}
}

// The stack from HERE, without an exception.
//
// For the deaths that are not exceptions: abort(), a rejected parameter,
// and the two places in the GPU bridge that give up. Without this the log
// only says THAT something gave up.
void Platform_CrashLogStack(const char *why)
{
	void *frames[NATIVE_CRASH_MAX_FRAMES];
	HANDLE process = GetCurrentProcess();
	USHORT count;
	USHORT i;

	SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
	SymInitialize(process, NULL, TRUE);

	count = RtlCaptureStackBackTrace(0, NATIVE_CRASH_MAX_FRAMES, frames, NULL);

	Platform_Log("[CTR Crash] %s - stack, innermost first:\n", why);

	for (i = 0; i < count; i++)
	{
		Platform_CrashLogFrame(process, (DWORD64)(DWORD_PTR)frames[i], (int)i);
	}

	Platform_Log("[CTR Crash] end of stack.\n");
}

internal LONG WINAPI Platform_CrashHandler(EXCEPTION_POINTERS *info)
{
	const EXCEPTION_RECORD *record = info->ExceptionRecord;
	HANDLE process = GetCurrentProcess();
	HANDLE thread = GetCurrentThread();
	CONTEXT context = *info->ContextRecord;
	STACKFRAME64 frame;
	int index;

	// A second crash in the handler itself would drag the log down with it.
	if (InterlockedExchange(&s_crashHandled, 1) != 0)
	{
		return EXCEPTION_EXECUTE_HANDLER;
	}

	Platform_Log("\n[CTR Crash] %s at 0x%08llx\n", Platform_CrashName(record->ExceptionCode), (unsigned long long)(DWORD_PTR)record->ExceptionAddress);

	if ((record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) && (record->NumberParameters >= 2))
	{
		// Parameter 0 is 0 for reading, 1 for writing, 8 for executing.
		const char *what = (record->ExceptionInformation[0] == 1) ? "writing" : ((record->ExceptionInformation[0] == 8) ? "executing" : "reading");

		Platform_Log("[CTR Crash] %s address 0x%08llx\n", what, (unsigned long long)record->ExceptionInformation[1]);

		if (record->ExceptionInformation[1] < 0x10000u)
		{
			Platform_Log("[CTR Crash] that is a null pointer plus a small offset - a field of a struct that was never set\n");
		}
	}

	SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
	if (!SymInitialize(process, NULL, TRUE))
	{
		Platform_Log("[CTR Crash] no symbol handler - addresses only\n");
	}

	memset(&frame, 0, sizeof(frame));
	frame.AddrPC.Mode = AddrModeFlat;
	frame.AddrFrame.Mode = AddrModeFlat;
	frame.AddrStack.Mode = AddrModeFlat;
#if defined(_M_IX86)
	frame.AddrPC.Offset = context.Eip;
	frame.AddrFrame.Offset = context.Ebp;
	frame.AddrStack.Offset = context.Esp;
#elif defined(_M_X64)
	frame.AddrPC.Offset = context.Rip;
	frame.AddrFrame.Offset = context.Rbp;
	frame.AddrStack.Offset = context.Rsp;
#endif

	Platform_Log("[CTR Crash] stack, innermost first:\n");

	for (index = 0; index < NATIVE_CRASH_MAX_FRAMES; index++)
	{
#if defined(_M_IX86)
		const DWORD machine = IMAGE_FILE_MACHINE_I386;
#else
		const DWORD machine = IMAGE_FILE_MACHINE_AMD64;
#endif

		if (!StackWalk64(machine, process, thread, &frame, &context, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL))
		{
			break;
		}

		if (frame.AddrPC.Offset == 0)
		{
			break;
		}

		Platform_CrashLogFrame(process, frame.AddrPC.Offset, index);
	}

	Platform_Log("[CTR Crash] end of stack. The log is flushed line by line, so this is complete.\n");
	Platform_LogShutdown();

	return EXCEPTION_EXECUTE_HANDLER;
}

// What the top-level filter does NOT see.
//
// SetUnhandledExceptionFilter only fires when nobody catches the exception. A
// driver or a library with its own __try swallows it before, and then
// the log says nothing - that has been seen: a crash without a single report
// line.
//
// This handler sees it FIRST and passes it on unchanged. It
// changes nothing about the flow, it only writes along. Limited to four messages,
// because some libraries use exceptions as a normal means and a log
// full of them again says nothing.
global_variable LONG s_crashFirstChanceSeen = 0;

internal LONG WINAPI Platform_CrashFirstChance(EXCEPTION_POINTERS *info)
{
	const DWORD code = info->ExceptionRecord->ExceptionCode;

	if ((code == EXCEPTION_ACCESS_VIOLATION) || (code == EXCEPTION_ILLEGAL_INSTRUCTION) || (code == EXCEPTION_INT_DIVIDE_BY_ZERO) ||
	    (code == EXCEPTION_STACK_OVERFLOW))
	{
		if (InterlockedIncrement(&s_crashFirstChanceSeen) <= 4)
		{
			Platform_Log("[CTR Crash] first chance: %s at 0x%08llx (somebody may still catch this)\n", Platform_CrashName(code),
			             (unsigned long long)(DWORD_PTR)info->ExceptionRecord->ExceptionAddress);
		}
	}

	return EXCEPTION_CONTINUE_SEARCH;
}

// The C runtime's own two deaths.
//
// An invalid parameter (a broken format, an index out of range) and
// abort() BOTH go past the top-level exception filter. Without these two here
// the program ends without a word.
internal void Platform_CrashInvalidParameter(const wchar_t *expression, const wchar_t *function, const wchar_t *file, unsigned int line,
                                             uintptr_t reserved)
{
	(void)expression;
	(void)function;
	(void)file;
	(void)reserved;

	Platform_Log("[CTR Crash] the C runtime rejected a parameter, near line %u - a bad format string\n", line);
	Platform_Log("[CTR Crash] or an index out of range.\n");
	Platform_CrashLogStack("invalid parameter");
	Platform_LogShutdown();
}

internal void Platform_CrashOnAbort(int signalNumber)
{
	(void)signalNumber;

	Platform_Log("[CTR Crash] abort() - the C runtime gave up. Not an exception, so the top-level\n");
	Platform_Log("[CTR Crash] filter never sees it - this is why a log could stay empty.\n");
	Platform_CrashLogStack("abort");
	Platform_LogShutdown();
}

internal void Platform_CrashHandlerInstall(void)
{
	SetUnhandledExceptionFilter(Platform_CrashHandler);
	AddVectoredExceptionHandler(1, Platform_CrashFirstChance);
	_set_invalid_parameter_handler(Platform_CrashInvalidParameter);
	signal(SIGABRT, Platform_CrashOnAbort);
}

// --crash-test.
//
// A crash report that has never triggered is not a report but an
// intention. This switch triggers it on purpose - BEFORE SDL_Init, that is without
// a window and without Vulkan - and writes the stack into the log. If
// Platform_Init and main are there, the tool does what it should.
//
// A null pointer and not abort(): exactly the exception this is about.
internal void Platform_CrashSelfTest(void)
{
	// volatile, so that the optimiser does not throw the access away.
	volatile int *nowhere = NULL;

	Platform_Log("[CTR Crash] --crash-test: writing to a null pointer on purpose\n");
	*nowhere = 1;
}

#else

internal void Platform_CrashHandlerInstall(void)
{
}

#endif

void Platform_Init(const char *title, int width, int height, int fullscreen)
{
	char windowName[128];

	Platform_LogInit(title);
	Platform_GetWindowName(title, windowName, sizeof(windowName));

	// Directly behind the log and before everything else: a crash DURING
	// setup should already be reported too.
	Platform_CrashHandlerInstall();

	{
		extern int g_cfg_crashTest;

		if (g_cfg_crashTest)
		{
			Platform_CrashSelfTest();
		}
	}

	Platform_Log("[CTR Native] Initialising platform\n");

	if (SDL_Init(SDL_INIT_VIDEO) == 0)
	{
		Platform_LogError("[CTR Native] Failed to initialise SDL\n");
		Platform_LogShutdown();
		return;
	}

	s_platformInitialized = 1;

	// Before the window, because the size a derived window gets comes out of the
	// shape and the shape has to be known first. There is no window yet, so this
	// reads the primary display - which is where SDL puts a window it was given
	// no position for.
	// Fullscreen and aspect ratio from the file, before the window comes into
	// being - applied afterwards they visibly jumped.
	Platform_SettingsPreloadDisplay(&fullscreen);

	Platform_DetectDisplayAspect("start");

	if ((width <= 0) || (height <= 0))
	{
		s_windowSizeDerived = 1;
		Platform_WindowedSizeForAspect(&width, &height);
	}

	if (!NativeRenderer_InitialiseRender(windowName, width, height, fullscreen))
	{
		Platform_LogError("[CTR Native] Failed to initialise window\n");
		Platform_Shutdown();
		return;
	}

	// Again, now that there is a window to stand on a display. The same function
	// and the same log line - it says "(unchanged)" when it agrees with the
	// answer above, which is the normal case. It is asked twice because SDL can
	// put the window somewhere other than the primary display, and one of the two
	// answers has to be the one the window is actually on.
	Platform_DetectDisplayAspect("window placed");

	// And if that moved the shape, the derived window is the wrong size for it.
	// Only a derived one: a size that was asked for on the command line is a
	// size, not a suggestion.
	if ((s_windowSizeDerived != 0) && (Platform_IsFullscreen() == 0))
	{
		int wantWidth = 0;
		int wantHeight = 0;

		Platform_WindowedSizeForAspect(&wantWidth, &wantHeight);

		if ((wantWidth != g_windowWidth) || (wantHeight != g_windowHeight))
		{
			SDL_SetWindowSize(g_window, wantWidth, wantHeight);
			SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
			NativeRenderer_SyncWindowSize();
			NativeRenderer_ApplyWorldAspectToPresentation();
		}
	}

	// After the log is open and before anything is drawn, and before the
	// command line is read - a flag has to be able to beat a saved value, so the
	// saved one has to be in place first. It only moves numbers that are read per
	// frame, so there is nothing to rebuild.
	Platform_SettingsLoad();

	// Which set the render path will read, and at what factor. Worth a line of
	// its own, because today all three sets hold the same numbers - so the
	// picture cannot show whether the right set was picked, and this is the only
	// place that says.
	if (Platform_ResolutionIsNative())
	{
		Platform_Log("[CTR Native] view settings: set %s active, %s - internal resolution NATIVE\n", CTR_View_ModeName(CTR_View_SettingsMode()),
		             CTR_View_SettingsChanged(CTR_View_SettingsMode()) ? "changed" : "stock");
	}
	else
	{
		Platform_Log("[CTR Native] view settings: set %s active, %s - internal resolution x%d\n", CTR_View_ModeName(CTR_View_SettingsMode()),
		             CTR_View_SettingsChanged(CTR_View_SettingsMode()) ? "changed" : "stock", Platform_GetResolutionScale());
	}

	if (!NativeRenderer_InitialisePSX())
	{
		Platform_LogError("[CTR Native] Failed to initialise PSX renderer state\n");
		Platform_Shutdown();
		return;
	}

	atexit(Platform_Shutdown);
	Platform_UpdateCursorVisibility();
	Platform_InputInit();
}

// REPORTS THAT RUN BEFORE THE LOG IS CLOSED.
//
// They hung on atexit, and there half of them never reached the log: atexit calls in
// reverse order of registration. Platform_Shutdown is hooked in in Platform_Init,
// that is AFTER the switches in main, and therefore ran BEFORE the
// reports registered there - it closes the log, and what comes afterwards
// is only on the console. Logs with --near-report carried not a single
// [CTR Near] line. The disc report and
// --gte-near-div had the same hole; the split report and the GTE flags
// only did not because they happened to be registered later.
//
// Now there is one door: whoever has something to say at exit registers
// here, and Platform_Shutdown calls all of them while the log is open. Ctrl+Q,
// the window closer, QUIT in the menu and --dump-exit all end in
// Platform_Shutdown, through atexit or directly, and s_platformInitialized makes sure
// that it happens once.
//
// 64 places, 32 and 16 before: a menu run over container tracks with
// anti-aliasing already filled all 16, and a render layer run with every
// report switch took 31 of 32 - the next steps of the render layer register
// more. Function pointers only - nothing else depends on the size: the table
// is filled in order, each report at most once, and run in that order.
#define PLATFORM_EXIT_REPORT_MAX 64

global_variable void (*s_exitReports[PLATFORM_EXIT_REPORT_MAX])(void);
global_variable int s_exitReportCount = 0;

void Platform_AtExitReport(void (*report)(void))
{
	if (report == NULL)
	{
		return;
	}

	for (int i = 0; i < s_exitReportCount; i++)
	{
		if (s_exitReports[i] == report)
		{
			return;
		}
	}

	if (s_exitReportCount >= PLATFORM_EXIT_REPORT_MAX)
	{
		Platform_LogError("[CTR Native] exit report table full (%d) - a report at exit is lost\n", PLATFORM_EXIT_REPORT_MAX);
		return;
	}

	s_exitReports[s_exitReportCount] = report;
	s_exitReportCount++;
}

internal void Platform_RunExitReports(void)
{
	for (int i = 0; i < s_exitReportCount; i++)
	{
		s_exitReports[i]();
	}

	s_exitReportCount = 0;
	Platform_LogFlush();
}

void Platform_Shutdown(void)
{
	if (s_platformInitialized == 0)
	{
		return;
	}

	s_platformInitialized = 0;

	// First the reports, while everything still stands and the log is open.
	Platform_RunExitReports();
#if defined(CTR_INTERNAL)
	NativeRenderer_FinishGpuMeasurements();
	NativePerf_Shutdown();
	NativeReplayScheduler_Shutdown();
#endif
	Platform_InputShutdown();
	NativeAudio_Shutdown();
	NativeRenderer_Shutdown();

	if (g_window != NULL)
	{
		SDL_DestroyWindow(g_window);
		g_window = NULL;
	}

	SDL_Quit();

	Platform_LogShutdown();
}

void Platform_BeginFrame(void)
{
	// NOTE(aalhendi): Normal rendering begins from DrawOTag after the current
	// draw env is installed. Starting a host scene here clears the previous env
	// and can force the host GL driver to block before the retail render-submit path.
}

int Platform_BeginScene(void)
{
	if (s_platformBeginScene)
	{
		return 0;
	}

	NativePerf_BeginScope(NATIVE_PERF_BUCKET_PLATFORM_BEGIN_SCENE);
	// NOTE(aalhendi): CTR already throttles through the retail VSync/draw-sync
	// path. Do not add a second SDL swap wait; some GL drivers charge that wait
	// to the next frame's first clear instead of the present.
	NativeRenderer_UpdateSwapIntervalState(0);

	NativeRenderer_BeginScene();

	if (activeDrawEnv.isbg)
	{
		const RECT16 clipenv = activeDrawEnv.clip;
		const u8 r = activeDrawEnv.r0;
		const u8 g = activeDrawEnv.g0;
		const u8 b = activeDrawEnv.b0;

		NativeRenderer_Clear(clipenv.x, clipenv.y, clipenv.w, clipenv.h, r, g, b);
	}

	s_platformBeginScene = 1;

	Platform_LogFlush();

	NativePerf_EndScope(NATIVE_PERF_BUCKET_PLATFORM_BEGIN_SCENE);
	return 1;
}

void Platform_EndScene(void)
{
	if (!s_platformBeginScene)
	{
		return;
	}

	NativePerf_BeginScope(NATIVE_PERF_BUCKET_PLATFORM_END_SCENE);
	s_platformBeginScene = 0;

	NativeRenderer_EndScene();

	if (s_pinnedVramDisplayFrames > 0)
	{
		if (s_pinnedVramDisplayCustomRect)
		{
			NativeRenderer_PresentVRAMRect(s_pinnedVramDisplayX, s_pinnedVramDisplayY, s_pinnedVramDisplayW, s_pinnedVramDisplayH);
		}
		else
		{
			NativeRenderer_PresentVRAMDisplay();
		}
		NativeRenderer_EndGpuFrame();
		NativeRenderer_SwapWindow();
		s_pinnedVramDisplayFrames--;
		if (s_pinnedVramDisplayFrames <= 0)
		{
			s_pinnedVramDisplayCustomRect = 0;
		}
		NativePerf_EndScope(NATIVE_PERF_BUCKET_PLATFORM_END_SCENE);
		return;
	}

	// NOTE(aalhendi): Keep the displayed VRAM region current for screen-copy
	// effects without forcing a CPU readback.
	NativeRenderer_StoreFrameBuffer(activeDispEnv.disp.x, activeDispEnv.disp.y, activeDispEnv.disp.w, activeDispEnv.disp.h);

	// The store above happens either way. VRAM stays the account of what is on
	// screen - the game reads its own framebuffer back for screen-copy effects,
	// and the next frame loads from it whenever the draw environment does not
	// clear. Only which of the two the window is shown from changes.
	//
	// At factor one this is the VRAM route, unchanged, and the other branch is
	// never taken. Asked of the sizes rather than of the factor - at NATIVE
	// there is no factor to compare against one, and the two comparisons were
	// starting to be written out in two files.
	if (NativeRenderer_PresentsViaMainTarget())
	{
		NativeRenderer_PresentMainTarget();
	}
	else
	{
		NativeRenderer_PresentVRAMRect(activeDispEnv.disp.x, activeDispEnv.disp.y, activeDispEnv.disp.w, activeDispEnv.disp.h);
	}
	// Before the frame's counts are cleared, and after both present routes, so
	// the report says which one actually ran rather than which one was expected.
	{
		extern int g_cfg_presentReportAt;
		const int vblank = Platform_GetVBlankCount();

		if ((g_cfg_presentReportAt > 0) && (vblank >= g_cfg_presentReportAt))
		{
			g_cfg_presentReportAt = 0;
			NativeRenderer_ReportPresentPath(vblank);
		}
	}
	NativeRenderer_EndPresentReportFrame();

	NativeRenderer_EndGpuFrame();
	NativeRenderer_SwapWindow();
	NativePerf_EndScope(NATIVE_PERF_BUCKET_PLATFORM_END_SCENE);
}

#if defined(CTR_INTERNAL)
// Writes the emulated VRAM at fixed VBlank counts, and can end the run after the
// last one. A measuring trigger, nothing else: off unless --dump-vram says
// otherwise.
//
// The anchor is the VBlank count, not the rendered frame number. Game state
// advances per VBlank - VSync emits them, Native_EmitVBlank runs the vsync
// callback and ticks RCNT1 - while how many frames the host managed to render
// in between depends on how fast it is. Two backends therefore reach VBlank N
// with the same game state and a different frame count, which is exactly the
// pair a backend comparison needs. Anchoring on the frame number instead
// compares two different moments of the animation and measures the host.
#define NATIVE_DUMP_MAX_POINTS 8

global_variable int s_dumpPoints[NATIVE_DUMP_MAX_POINTS];
global_variable int s_dumpDone[NATIVE_DUMP_MAX_POINTS];
global_variable int s_dumpCount;
global_variable int s_dumpExitAfterLast;
global_variable const char *s_dumpPrefix = "dump";

// One dump at the end of the current frame, named instead of anchored on a
// VBlank: --level-tour asks for one per track, at a moment that no list given
// at the start can know. <prefix>-<name>.tga, the prefix of --dump-prefix. One
// request is kept; a second one in the same frame replaces it.
global_variable char s_dumpRequestName[96];

void Platform_DumpRequest(const char *name)
{
	if ((name == NULL) || (name[0] == '\0'))
	{
		return;
	}

	snprintf(s_dumpRequestName, sizeof(s_dumpRequestName), "%s", name);
}

// A deadline for a run that drives itself (--level-tour): when the VBlank count
// passes it before the caller has moved it on, onMissed is called at the end of
// that frame, and is expected to end the run with a line that says why. Here and
// not in the game logic, because the game logic does not run while a level
// loads - a watch that sleeps exactly when a load hangs is no watch. -1 clears.
global_variable int s_deadlineVBlank = -1;
global_variable void (*s_deadlineMissed)(void);

void Platform_SetDeadline(int vblank, void (*onMissed)(void))
{
	s_deadlineVBlank = (onMissed != NULL) ? vblank : -1;
	s_deadlineMissed = onMissed;
}

internal void Platform_DeadlineIfDue(void)
{
	void (*missed)(void) = s_deadlineMissed;

	if ((s_deadlineVBlank < 0) || (missed == NULL) || (Platform_GetVBlankCount() <= s_deadlineVBlank))
	{
		return;
	}

	s_deadlineVBlank = -1;
	s_deadlineMissed = NULL;
	missed();
}

// Anchoring on the VBlank count alone turned out not to be enough.
//
// The count fixes how much game time has passed since the process started, and
// that is only the same scene in two runs if both reached the title at the same
// point - which they do not. Boot work differs between backends and between
// builds (Vulkan builds its pipelines when it first needs them, GL compiles up
// front), the attract sequence advances on that offset, and two runs arrive at
// VBlank 13000 in different demos. Measured: two runs of one build
// differed in 93.96 % of the front buffer's halfwords at the same anchor.
//
// So the anchor moves to the game state itself. One run writes a quick state at
// a chosen VBlank; every run after that loads that same state early and dumps a
// fixed number of VBlanks later. What the boot did no longer reaches the
// picture, and the run takes seconds instead of minutes.
global_variable int s_saveStateAt = -1;
global_variable int s_loadStateAt = -1;
global_variable int s_saveStateDone;
global_variable int s_loadStateDone;

void Platform_DumpConfigure(const char *list, const char *prefix, int exitAfterLast)
{
	s_dumpCount = 0;
	s_dumpExitAfterLast = exitAfterLast;

	if (prefix != NULL)
	{
		s_dumpPrefix = prefix;
	}

	while ((list != NULL) && (*list != '\0') && (s_dumpCount < NATIVE_DUMP_MAX_POINTS))
	{
		int value = 0;
		int digits = 0;

		while ((*list >= '0') && (*list <= '9'))
		{
			value = (value * 10) + (*list - '0');
			list++;
			digits++;
		}

		if (digits > 0)
		{
			s_dumpPoints[s_dumpCount] = value;
			s_dumpDone[s_dumpCount] = 0;
			s_dumpCount++;
		}

		if (*list == ',')
		{
			list++;
		}
		else
		{
			break;
		}
	}

	for (int i = 0; i < s_dumpCount; i++)
	{
		Platform_Log("[CTR Dump] VRAM at VBlank %d\n", s_dumpPoints[i]);
	}
}

void Platform_DumpStateAt(int saveAt, int loadAt)
{
	s_saveStateAt = saveAt;
	s_loadStateAt = loadAt;
	s_saveStateDone = 0;
	s_loadStateDone = 0;

	if (saveAt >= 0)
	{
		Platform_Log("[CTR Dump] quick state will be written at VBlank %d\n", saveAt);
	}

	if (loadAt >= 0)
	{
		Platform_Log("[CTR Dump] quick state will be loaded at VBlank %d\n", loadAt);
	}
}

void NativeChar_NoteVBlank(int vblank); // platform/native_chars.c, later in the same build

// The shot, at the VBlank it was asked for. Its own small function so that it
// sits beside the dump rather than inside the frame. With --char the steer
// frame of seat 0 at the same moment goes beside it (platform/native_chars.c).
internal void Platform_ShotIfDue(void)
{
	if ((g_cfg_shotAt <= 0) || (Platform_GetVBlankCount() < g_cfg_shotAt))
	{
		return;
	}

	if (g_cfg_shotCount > 1)
	{
		// Room for the whole name of --shot-name and "-<vblank>.bmp".
		char path[PLATFORM_ARG_PATH_MAX + 32];
		char base[PLATFORM_ARG_PATH_MAX];
		size_t len;

		snprintf(base, sizeof(base), "%s", (g_cfg_shotName[0] != '\0') ? g_cfg_shotName : "SCREENSHOT.BMP");
		len = strlen(base);
		if ((len > 4) && ((strcmp(base + len - 4, ".bmp") == 0) || (strcmp(base + len - 4, ".BMP") == 0)))
		{
			base[len - 4] = '\0';
		}
		snprintf(path, sizeof(path), "%s-%d.bmp", base, g_cfg_shotAt);
		Platform_TakeScreenshot(path);
		// The counter at the moment of the capture, so that a snapshot can be matched
		// to a log line from the parser (that line carries the state when the frame
		// was built).
		Platform_Log("[CTR Shot] vblank %d: %s (asked for %d)\n", Platform_GetVBlankCount(), path, g_cfg_shotAt);
		NativeChar_NoteVBlank(Platform_GetVBlankCount());

		s_shotNext++;
		g_cfg_shotAt = (s_shotNext < g_cfg_shotCount) ? g_cfg_shotList[s_shotNext] : 0;
		return;
	}

	g_cfg_shotAt = 0;
	Platform_TakeScreenshot((g_cfg_shotName[0] != '\0') ? g_cfg_shotName : NULL);
	NativeChar_NoteVBlank(Platform_GetVBlankCount());
}

// --msaa-at V:L,... (main.c): at VBlank V, ask for anti-aliasing level L as a
// run value, like --msaa - never saved, so a later save from any menu row does
// not write a measuring level into the file. From there it goes the way a hand
// on the menu does: the wait for the next frame boundary and the same switch
// (NativeRenderer_ApplyMsaa). One per frame, beside the shot and after it: a
// shot at the same VBlank still shows the level before.
int g_cfg_msaaAtCount = 0;
int g_cfg_msaaAtVBlank[PLATFORM_MSAA_AT_MAX];
int g_cfg_msaaAtLevel[PLATFORM_MSAA_AT_MAX];
global_variable int s_msaaAtNext = 0;

internal void Platform_MsaaSwitchIfDue(void)
{
	if ((s_msaaAtNext >= g_cfg_msaaAtCount) || (Platform_GetVBlankCount() < g_cfg_msaaAtVBlank[s_msaaAtNext]))
	{
		return;
	}

	Platform_Log("[CTR MSAA] --msaa-at: vblank %d (asked for %d) asks for %s\n", Platform_GetVBlankCount(), g_cfg_msaaAtVBlank[s_msaaAtNext],
	             Platform_MsaaName(g_cfg_msaaAtLevel[s_msaaAtNext]));
	Platform_SetMsaaRun(g_cfg_msaaAtLevel[s_msaaAtNext]);
	s_msaaAtNext++;
}

internal void Platform_DumpIfDue(void)
{
	// The request is served by NativeSaveState_BeginFrame at the top of the next
	// game frame, which is where saving and loading are safe. Asking for it here
	// only sets the flag.
	if ((s_saveStateAt >= 0) && !s_saveStateDone && (Platform_GetVBlankCount() >= s_saveStateAt))
	{
		s_saveStateDone = 1;
		NativeSaveState_RequestSave();
	}

	if ((s_loadStateAt >= 0) && !s_loadStateDone && (Platform_GetVBlankCount() >= s_loadStateAt))
	{
		s_loadStateDone = 1;
		NativeSaveState_RequestLoad();
	}

	// A named request (Platform_DumpRequest) is served before the VBlank list
	// and needs none: the list can end the run, the request never does.
	if (s_dumpRequestName[0] != '\0')
	{
		// Room for the whole --dump-prefix, the name and "-" ".tga".
		char path[PLATFORM_ARG_PATH_MAX + sizeof(s_dumpRequestName) + 8];

		snprintf(path, sizeof(path), "%s-%s.tga", s_dumpPrefix, s_dumpRequestName);
		s_dumpRequestName[0] = '\0';
		NativeRenderer_SaveVRAM(path, 0, 0, VRAM_WIDTH, VRAM_HEIGHT, 1);
		Platform_Log("[CTR Dump] %s at VBlank %d (requested)\n", path, Platform_GetVBlankCount());
	}

	if (s_dumpCount == 0)
	{
		return;
	}

	const int vblank = Platform_GetVBlankCount();
	int remaining = 0;

	for (int i = 0; i < s_dumpCount; i++)
	{
		if (s_dumpDone[i])
		{
			continue;
		}

		if (vblank >= s_dumpPoints[i])
		{
			// Room for the whole --dump-prefix and "-<vblank>.tga".
			char path[PLATFORM_ARG_PATH_MAX + 32];

			snprintf(path, sizeof(path), "%s-%06d.tga", s_dumpPrefix, s_dumpPoints[i]);
			NativeRenderer_SaveVRAM(path, 0, 0, VRAM_WIDTH, VRAM_HEIGHT, 1);
			Platform_Log("[CTR Dump] %s at VBlank %d (asked %d)\n", path, vblank, s_dumpPoints[i]);
			s_dumpDone[i] = 1;
		}
		else
		{
			remaining++;
		}
	}

	if ((remaining == 0) && s_dumpExitAfterLast)
	{
		// Says whether the code under test ran at all. A comparison that reports
		// no difference has to be able to tell "the change does nothing here"
		// from "the changed path was never taken here".
		NativeGfxVK_ReportClearCounts();

		Platform_Log("%s\n", "[CTR Dump] all points written, ending the run");
		Platform_LogFlush();
		Platform_Shutdown();
		exit(0);
	}
}
#endif

// ENDING THE GAME - ONE EXIT INSTEAD OF FOUR.
//
// Ctrl+Q, the QUIT row in the main menu and the measuring exits go through
// here; the window closer and SDL_EVENT_QUIT call the same exit(0) directly.
// exit(0) and not a cleanup by hand: atexit runs Platform_Shutdown, and that
// runs the exit reports (Platform_AtExitReport - disc read counter, draw
// command overflows and the rest) while the log is still open. Whoever ends
// past that loses all of them - and with them a whole evening of measuring,
// as has happened.
void Platform_QuitGame(const char *why)
{
	Platform_Log("[CTR Native] %s - quitting\n", (why != NULL) ? why : "quit");
	Platform_LogFlush();
	exit(0);
}

internal void Platform_FrameEndClock(void); // at the VBlank clock, below
void NativePreview_EndFrame(void); // platform/native_preview.c, later in the same build

// NOTE(aalhendi): Frame timing is handled by VSync() in the platform layer,
// matching PS1 hardware behavior. Platform_EndFrame does no pacing: it ends
// the scene (present and swap), counts FPS, closes the per-frame counters and
// runs the per-frame measuring hooks.
void Platform_EndFrame(void)
{
	NativePerf_BeginScope(NATIVE_PERF_BUCKET_PLATFORM_END_FRAME);
	Platform_EndScene();
	Platform_CalcFPS();

	// The GTE saturation count closes its frame here - the same place
	// where the other frame counters also close, because here a frame
	// is demonstrably over. Off as long as nobody asks.
	NativeGteFlags_Frame();
	NativeSubpixel_Frame();

	// One frame of the detail counters, closed here because this is the one
	// place a frame is known to be over. The counters cost nothing when no
	// report is armed - the arming is what switches them on.
	CTR_Lod_Tick(Platform_GetVBlankCount());
#if defined(CTR_INTERNAL)
	// Here and not inside EndScene, beside the VRAM dump and before it. The dump
	// has been reading the device from this exact point for weeks; inside
	// EndScene the frame is still open, and a read that drains the queue there
	// is a new thing to be right about. Before the dump, because the dump can
	// end the run.
	Platform_ShotIfDue();
	NativePreview_EndFrame(); // --record-preview (platform/native_preview.c)
	Platform_MsaaSwitchIfDue();
	Platform_DumpIfDue();
	Platform_DeadlineIfDue(); // after the dump: a run that ends here keeps it
#endif
	// Behind everything a frame still costs: the disturbance delay and the
	// line of the frame log (--inject-delay, --frame-log; at the VBlank clock).
	Platform_FrameEndClock();
	NativePerf_EndScope(NATIVE_PERF_BUCKET_PLATFORM_END_FRAME);
}

void Platform_PresentVRAMDisplay(void)
{
	Platform_PinVRAMDisplayFrames(1);
	Platform_BeginScene();
	Platform_EndFrame();
}

void Platform_PinVRAMDisplayFrames(int frameCount)
{
	if (frameCount > s_pinnedVramDisplayFrames)
	{
		s_pinnedVramDisplayFrames = frameCount;
		s_pinnedVramDisplayCustomRect = 0;
	}
}

void Platform_PinVRAMDisplayRect(int x, int y, int w, int h, int frameCount)
{
	if ((frameCount <= 0) || (w <= 0) || (h <= 0))
	{
		return;
	}

	s_pinnedVramDisplayX = x;
	s_pinnedVramDisplayY = y;
	s_pinnedVramDisplayW = w;
	s_pinnedVramDisplayH = h;
	s_pinnedVramDisplayFrames = frameCount;
	s_pinnedVramDisplayCustomRect = 1;
}

// PAUSE ON MINIMISE OR FOCUS LOSS. The window only reports
// here that it is gone. The pause opens the game on its own
// Start route (MainFrame_GameLogic, same condition as BTN_START, then
// MainFreeze_IfPressStart) - with all retail locks: not in the menu, not while
// loading, not in cutscenes, not in the demo, not at the end of the race.
//
// AS LONG AS THE WINDOW IS GONE (minimised or without focus), the Start route gets
// the wish offered anew in every frame, until it pauses once. Redeemed in
// the one frame of the event would be too little: in the countdown the
// finish flag is still in the picture, the Start route refuses, and the race drove on
// minimised. At most one pause per absence; once
// the window is back (focus there, not minimised), the wish expires. In the menu
// and while loading the Start route refuses in every frame - nothing happens there.
//
// Focus loss without minimising also pauses: without focus the keyboard (pad
// 0) no longer arrives, the kart would drive on without a hand. A window that never had
// focus (started in the background) reports no loss and is not gone.
//
// Only when a human plays. Off with --dev - measuring, diagnostic and replay
// calls carry --dev, and there no window event may change the game flow;
// --focus-pause switches it on anyway to test it.
// Off as long as a replay is prepared, recording or playing: a
// pause that does not come from the input would make the playback diverge.
// Never in measuring mode (--deterministic), not even with --focus-pause:
// there no window event and no real input reaches the game (the seal below).
int g_cfg_focusPause = 0;

extern int g_cfg_deterministic; // measuring mode, at the VBlank clock below

global_variable int s_awayMinimized = 0;
global_variable int s_awayUnfocused = 0;
global_variable int s_awaySinceVBlank = 0;
global_variable int s_awayPauseDone = 0;
global_variable int s_awayRefusedLogged = 0;

internal const char *Platform_FocusPauseBlocked(void)
{
	extern int g_cfg_dev;

	if (g_cfg_deterministic)
	{
		return "--deterministic (measuring mode: no window event and no real input reaches the game)";
	}

	if (g_cfg_dev && !g_cfg_focusPause)
	{
		return "--dev (measuring, diagnosis and replay runs; --focus-pause turns it on)";
	}

#if defined(CTR_INTERNAL)
	if (NativeReplayScheduler_IsActive())
	{
		return "a replay is set up, recording or playing";
	}
#endif

	return NULL;
}

internal const char *Platform_AwayWhat(void)
{
	// Minimised first: that is the stronger one, and it mostly brings the focus loss along.
	return s_awayMinimized ? "minimized" : "without focus";
}

// A window event: gone (minimised, focus lost) or back.
internal void Platform_NoteWindowAway(int minimized, int unfocused)
{
	const int wasAway = s_awayMinimized || s_awayUnfocused;

	s_awayMinimized = minimized;
	s_awayUnfocused = unfocused;

	if (!wasAway && (minimized || unfocused))
	{
		const char *blocked = Platform_FocusPauseBlocked();

		s_awaySinceVBlank = Platform_GetVBlankCount();
		s_awayPauseDone = 0;
		s_awayRefusedLogged = 0;

		if (blocked != NULL)
		{
			Platform_Log("[CTR Pause] window %s at vblank %d - no pause: %s\n", Platform_AwayWhat(), s_awaySinceVBlank, blocked);
		}
	}
	else if (wasAway && !minimized && !unfocused)
	{
		if ((Platform_FocusPauseBlocked() == NULL) && !s_awayPauseDone)
		{
			Platform_Log("[CTR Pause] window back at vblank %d, away since vblank %d - no pause in that time (the Start path refused it throughout)\n",
			             Platform_GetVBlankCount(), s_awaySinceVBlank);
		}

		s_awayPauseDone = 0;
	}
}

int Platform_TakeFocusPauseWish(void)
{
	if (!(s_awayMinimized || s_awayUnfocused) || s_awayPauseDone)
	{
		return 0;
	}

	// Asked anew every time: a replay can begin in the middle of the absence.
	return Platform_FocusPauseBlocked() == NULL;
}

void Platform_NoteFocusPause(int outcome, int levelID, unsigned int gameMode1, int loading)
{
	if (outcome == PLATFORM_FOCUS_PAUSE_REFUSED)
	{
		// Refused in every frame, as long as menu, loading or countdown last.
		// It is said once per absence.
		if (!s_awayRefusedLogged)
		{
			s_awayRefusedLogged = 1;
			Platform_Log("[CTR Pause] window %s since vblank %d, at vblank %d on level %d (gameMode1 0x%08x, loading %d): no pause yet - the Start "
			             "path refuses it (menu, loading, cutscene, demo, end of race, an open menu, the flag at the start or the cooldown); "
			             "asked again every frame while the window is away\n",
			             Platform_AwayWhat(), s_awaySinceVBlank, Platform_GetVBlankCount(), levelID, gameMode1, loading);
		}
		return;
	}

	s_awayPauseDone = 1;

	Platform_Log("[CTR Pause] window %s since vblank %d, at vblank %d on level %d (gameMode1 0x%08x, loading %d): %s\n", Platform_AwayWhat(),
	             s_awaySinceVBlank, Platform_GetVBlankCount(), levelID, gameMode1, loading,
	             (outcome == PLATFORM_FOCUS_PAUSE_OPENED) ? "the pause menu is open (the same path as Start)" : "the game was paused already");
}

// THE MEASURING MODE SEAL (--deterministic). A measuring run is driven by its
// script alone: --menu-keys, --level, --autoload-demo, --autopilot, a replay.
// Whatever happens on the desk next to it is not part of the run - and it
// reached the game: a pad button held at boot ended a demo race, and the rest
// of the run was another run. So in measuring mode the keyboard and the pads
// are not read (Platform_InputUpdate, native_input.c) - buttons and axes
// alike, the triggers L2/R2 and the sticks included -, no key event goes
// anywhere - the debug keys, F11, Alt+Enter and Ctrl+Q included - focus loss
// or minimise never pause (Platform_FocusPauseBlocked), and a display change
// does not change the aspect. The mouse is read nowhere anyway. The window
// itself still follows what is done to it: resize and minimise reach the
// renderer, which only concerns the output to the window, and the close
// button still ends the run. --shot hangs on the VBlank, not on a key.
//
// What was kept away is counted and said at exit, one line per kind and
// only for a kind that came at all; the report is registered on the first
// one, so an undisturbed run keeps its exit reports as they were. Pad axes
// (triggers, sticks) are kept away as well but not counted: they move
// without a hand on them, and a count of that would say nothing.

global_variable int s_sealedKeys = 0;
global_variable int s_sealedMouse = 0;
global_variable int s_sealedPadButtons = 0;
global_variable int s_sealedFocusLosses = 0;
global_variable int s_sealedMinimizes = 0;
global_variable int s_sealedDisplayChanges = 0;
global_variable int s_sealReportArmed = 0;

internal void Platform_ReportSealedInput(void)
{
	if (s_sealedKeys > 0)
	{
		Platform_Log("[CTR Input] at exit: measuring mode kept %d key press(es) away from the game\n", s_sealedKeys);
	}

	if (s_sealedMouse > 0)
	{
		Platform_Log("[CTR Input] at exit: measuring mode kept %d mouse click(s) and wheel turn(s) away from the game\n", s_sealedMouse);
	}

	if (s_sealedPadButtons > 0)
	{
		Platform_Log("[CTR Input] at exit: measuring mode kept %d pad button press(es) away from the game\n", s_sealedPadButtons);
	}

	if (s_sealedFocusLosses > 0)
	{
		Platform_Log("[CTR Input] at exit: measuring mode kept %d focus loss(es) away from the game (no pause)\n", s_sealedFocusLosses);
	}

	if (s_sealedMinimizes > 0)
	{
		Platform_Log("[CTR Input] at exit: measuring mode kept %d minimise(s) away from the game (no pause)\n", s_sealedMinimizes);
	}

	if (s_sealedDisplayChanges > 0)
	{
		Platform_Log("[CTR Input] at exit: measuring mode kept %d display change(s) away from the game (the aspect stays)\n",
		             s_sealedDisplayChanges);
	}
}

internal void Platform_NoteSealed(int *counter)
{
	(*counter)++;

	if (!s_sealReportArmed)
	{
		s_sealReportArmed = 1;
		Platform_AtExitReport(Platform_ReportSealedInput);
	}
}

void Platform_PollHostEvents(void)
{
	SDL_Event event;

	if (s_fullscreenTogglePending != 0)
	{
		s_fullscreenTogglePending = 0;
		Platform_HandleFullscreenToggle();
	}

	// GRAPHICS page: a target state instead of a toggle, so that
	// left/right twice before this point does not toggle twice.
	if (s_fullscreenWanted >= 0)
	{
		if (Platform_IsFullscreen() != s_fullscreenWanted)
		{
			Platform_HandleFullscreenToggle();
		}

		s_fullscreenWanted = -1;
	}

	while (SDL_PollEvent(&event))
	{
		switch (event.type)
		{
		case SDL_EVENT_GAMEPAD_ADDED:
			Platform_InputControllerAdded(event.gdevice.which);
			break;
		case SDL_EVENT_GAMEPAD_REMOVED:
			Platform_InputControllerRemoved(event.gdevice.which);
			break;
		case SDL_EVENT_QUIT:
			exit(0);
			break;
		case SDL_EVENT_WINDOW_RESIZED:
			Platform_HandleWindowResize();
			break;
		case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
			// Dragged onto another monitor. The shape of the picture follows it -
			// in measuring mode it stays the one the run started with.
			if (g_cfg_deterministic)
			{
				Platform_NoteSealed(&s_sealedDisplayChanges);
				break;
			}
			Platform_DetectDisplayAspect("window moved");
			break;
		case SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED:
		case SDL_EVENT_DISPLAY_ADDED:
		case SDL_EVENT_DISPLAY_REMOVED:
			// The desktop resolution changed under us, or the monitor the window was
			// on went away and it landed somewhere else. Not in measuring mode, as above.
			if (g_cfg_deterministic)
			{
				Platform_NoteSealed(&s_sealedDisplayChanges);
				break;
			}
			Platform_DetectDisplayAspect("display changed");
			break;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_WHEEL:
			// The game reads no mouse. Counted in measuring mode only, so that a
			// disturbance is seen to have arrived.
			if (g_cfg_deterministic)
			{
				Platform_NoteSealed(&s_sealedMouse);
			}
			break;
		case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
			// Read through the pad state (native_input.c), not through this event;
			// in measuring mode that state is not read, and the press is counted.
			// Axis motion (triggers L2/R2, sticks) is not read either, but not counted.
			if (g_cfg_deterministic)
			{
				Platform_NoteSealed(&s_sealedPadButtons);
			}
			break;
		case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
		case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
			Platform_UpdateCursorVisibility();
			break;
		case SDL_EVENT_WINDOW_MINIMIZED:
		case SDL_EVENT_WINDOW_RESTORED:
		case SDL_EVENT_WINDOW_HIDDEN:
		case SDL_EVENT_WINDOW_SHOWN:
		{
			// Only into the log: in unattended measuring runs the window
			// had area 0 several times without anybody touching it.
			// This line says next time whether SDL saw a minimise at that moment.
			// What the renderer makes of it is at NativeVk_BuildSwapchainOrPause.
			int pixelW = 0;
			int pixelH = 0;
			const char *what = (event.type == SDL_EVENT_WINDOW_MINIMIZED)  ? "minimized"
			                   : (event.type == SDL_EVENT_WINDOW_RESTORED) ? "restored"
			                   : (event.type == SDL_EVENT_WINDOW_HIDDEN)   ? "hidden"
			                                                               : "shown";

			SDL_GetWindowSizeInPixels(g_window, &pixelW, &pixelH);
			Platform_Log("[CTR Window] %s at vblank %d (t %.3f s), %dx%d pixels\n", what, Platform_GetVBlankCount(),
			             (double)SDL_GetTicks() / 1000.0, pixelW, pixelH);

			if (event.type == SDL_EVENT_WINDOW_MINIMIZED)
			{
				if (g_cfg_deterministic)
				{
					Platform_NoteSealed(&s_sealedMinimizes);
				}

				Platform_NoteWindowAway(1, s_awayUnfocused);
			}
			else if (event.type == SDL_EVENT_WINDOW_RESTORED)
			{
				Platform_NoteWindowAway(0, s_awayUnfocused);
			}
			break;
		}
		case SDL_EVENT_WINDOW_FOCUS_LOST:
		case SDL_EVENT_WINDOW_FOCUS_GAINED:
		{
			const int lost = (event.type == SDL_EVENT_WINDOW_FOCUS_LOST);

			Platform_Log("[CTR Window] focus %s at vblank %d (t %.3f s)\n", lost ? "lost" : "gained", Platform_GetVBlankCount(),
			             (double)SDL_GetTicks() / 1000.0);

			if (lost && g_cfg_deterministic)
			{
				Platform_NoteSealed(&s_sealedFocusLosses);
			}

			Platform_NoteWindowAway(s_awayMinimized, lost);
			break;
		}
		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			exit(0);
			break;
		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP:
		{
			int key = event.key.scancode;
			char down = (event.type == SDL_EVENT_KEY_UP) ? 0 : 1;

			// MEASURING MODE: no key reaches anything, see the seal above.
			if (g_cfg_deterministic)
			{
				if ((down != 0) && (event.key.repeat == 0))
				{
					Platform_NoteSealed(&s_sealedKeys);
				}
				break;
			}

			Platform_UpdateHostAltKeyState(key, down);

			// Ctrl+Q ends the game. The modifier is read from the event rather
			// than tracked like Alt above, because a modifier tracked by hand
			// stays down across a lost focus - and a quit that fires on a bare Q
			// afterwards is worse than one that needs the key held twice.
			//
			// The same exit(0) the window's close button and SDL_EVENT_QUIT use,
			// so the log is closed the same way whichever route was taken.
			if ((key == SDL_SCANCODE_Q) && (down != 0) && (event.key.repeat == 0) && ((event.key.mod & SDL_KMOD_CTRL) != 0))
			{
				Platform_QuitGame("Ctrl+Q");
			}

			if (key == SDL_SCANCODE_F11)
			{
				if ((down != 0) && (event.key.repeat == 0))
				{
					Platform_HandleFullscreenToggle();
				}
				break;
			}

			if (key == SDL_SCANCODE_RETURN)
			{
				if ((s_hostAltKeyState != 0) && (down != 0) && (event.key.repeat == 0))
				{
					Platform_HandleFullscreenToggle();
				}
				break;
			}

			if (key == SDL_SCANCODE_RSHIFT)
			{
				key = SDL_SCANCODE_LSHIFT;
			}
			else if (key == SDL_SCANCODE_RCTRL)
			{
				key = SDL_SCANCODE_LCTRL;
			}
			else if (key == SDL_SCANCODE_RALT)
			{
				key = SDL_SCANCODE_LALT;
			}

			if ((key == SDL_SCANCODE_F4) && (down == 0))
			{
#ifdef CTR_INTERNAL
				Platform_LogWarn("[CTR Native] Keyboard assigned to player %d\n", Platform_InputCycleKeyboardController());
#endif
				break;
			}

			if ((key == SDL_SCANCODE_F6) && (down == 0))
			{
#ifdef CTR_INTERNAL
				int player = Platform_InputCycleGamepadController();
				if (player == 0)
				{
					Platform_LogWarn("[CTR Native] No gamepad connected\n");
				}
				else
				{
					Platform_LogWarn("[CTR Native] Gamepad assigned to player %d\n", player);
				}
#endif
				break;
			}

			Platform_HandleKey(key, down);
			break;
		}
		}
	}
}

int Platform_PollInput(void)
{
	Platform_PollHostEvents();
	Platform_InputUpdate();
	return 1;
}

int NikoGetEnterKey(void)
{
	// MEASURING MODE: the name entry reads the host keyboard past the pad bus;
	// sealed like the rest of it.
	if (g_cfg_deterministic)
	{
		return 0;
	}

	const bool *kb = SDL_GetKeyboardState(NULL);
	return (kb && kb[SDL_SCANCODE_RETURN]) ? 1 : 0;
}

// NOTE(aalhendi): VSyncCallback uses the PSX facade, but native owns the VBlank
// clock that emits the registered callback.
// NOTE(aalhendi): Native paces VBlank from PS1 NTSC video timing instead of
// rounded 60Hz. PSX-SPX lists NTSC as 263 scanlines/frame and about 3413 video
// cycles/scanline. With the NTSC GPU clock used here, this is ~59.817Hz, making
// VSync(2) roughly 29.909 FPS. This affects host wall pacing; game state still
// advances from emitted VBlank counts and retail RCNT1 ticks.
#define NATIVE_VBLANK_GPU_CYCLES 897619ull // 3413 * 263
#define NATIVE_GPU_CLOCK_HZ      53693175ull
#define NATIVE_VSYNC_CATCHUP_MAX 8
// NOTE(aalhendi): SDL_DelayPrecise handles most of the wait; the final window
// spins against SDL's performance counter so pacing follows the VBlank target.
#define NATIVE_VSYNC_SPIN_US     200

global_variable u64 s_nextVBlankCounter = 0;
global_variable u64 s_vblankRemainder = 0;
global_variable int s_nativeVBlankCount = 0;

// MEASURING MODE (--deterministic, main.c). Without the switch
// every path below stays as it was. With it VSync does not catch up VBlanks
// that are due by wall clock (Native_CatchUpDueVBlanks is not called): every
// call emits exactly the requested VBlanks, from the first call at
// boot on. If the target of the clock already lies behind the wall clock, the clock starts
// anew at now (Native_DropLateTarget) - the lost time expires, the
// game runs slower under load instead of skipping frames. The
// game flow thus depends only on the number of frames, not on their duration.
// Never in ctr-settings.cfg: the file only knows s_videoSettings and the
// views (Platform_SettingsSave).
int g_cfg_deterministic = 0;

// FRAME LOG (--frame-log): one line "[CTR Frame] bild ..." per frame from
// Platform_EndFrame and one line "[CTR Frame] boot ..." per VSync call before
// the first frame. The columns are in the header line, which is written at the
// start of the VBlank clock. The counters always run, they cost
// nothing; written only with the switch. The column names are German words,
// kept so that old and new logs stay comparable: bild = frame, emittiert =
// emitted, nachgeholt = caught up, neu = clock restarted, verfallen = dropped
// lateness.
int g_cfg_frameLog = 0;

// DISTURBING SWITCH (--inject-delay <seed>, only for measurements): holds
// selected frames (after present and snapshot) and selected VSync calls
// of the boot phase for 20..80 ms. Which ones and for how long is a pure function
// of seed and number (Native_InjectHash): every eighth frame, duration
// 20 + (h & 0xffff) % 61 ms. Every delay is in the log as "[CTR Delay]".
int g_cfg_injectDelaySeed = 0;

global_variable int s_frameNumber = 0;         // frames since start (Platform_EndFrame)
global_variable int s_frameEmitted = 0;        // VBlanks since the last frame line, all
global_variable int s_frameCaughtUp = 0;       // of them caught up by wall clock (classic)
global_variable int s_frameRebased = 0;        // clock restarts on a backlog above 8 (classic)
global_variable double s_frameDroppedMs = 0.0; // measuring mode: dropped lateness since the last line
global_variable u64 s_frameClockStart = 0;     // QPC at the start of the VBlank clock
global_variable u64 s_frameLastLine = 0;       // QPC of the last frame line
global_variable int s_bootVSyncCalls = 0;      // VSync calls before the first frame

internal u32 Native_InjectHash(u32 n, u32 salt)
{
	u32 h = (u32)g_cfg_injectDelaySeed ^ (n * 2654435761u) ^ salt;

	h ^= h >> 16;
	h *= 0x7feb352du;
	h ^= h >> 15;
	h *= 0x846ca68bu;
	h ^= h >> 16;
	return h;
}

// what: "frame" (salt 0) or "boot vsync" (salt 0x9e3779b9), n: the number.
internal void Native_InjectDelayIfDue(const char *what, int n, u32 salt)
{
	if (g_cfg_injectDelaySeed == 0)
	{
		return;
	}

	{
		const u32 h = Native_InjectHash((u32)n, salt);

		if ((h >> 29) != 0)
		{
			return;
		}

		{
			const int ms = 20 + (int)((h & 0xffffu) % 61u);

			Platform_Log("[CTR Delay] %s %d: %d ms\n", what, n, ms);
			SDL_DelayPrecise((u64)ms * 1000000ull);
		}
	}
}

internal double Native_MsSince(u64 since)
{
	const u64 freq = SDL_GetPerformanceFrequency();
	const u64 now = SDL_GetPerformanceCounter();

	if ((freq == 0) || (since == 0) || (now < since))
	{
		return 0.0;
	}

	return (double)(now - since) * 1000.0 / (double)freq;
}

// From Platform_EndFrame, after present, snapshot and dump: the disturbance delay
// of this frame, then its line. dt includes the delay.
internal void Platform_FrameEndClock(void)
{
	s_frameNumber++;
	Native_InjectDelayIfDue("frame", s_frameNumber, 0u);

	if (g_cfg_frameLog)
	{
		Platform_Log("[CTR Frame] bild %d vblank %d t %.2f dt %.2f emittiert %d nachgeholt %d neu %d verfallen %.2f\n", s_frameNumber,
		             s_nativeVBlankCount, Native_MsSince(s_frameClockStart), Native_MsSince(s_frameLastLine), s_frameEmitted, s_frameCaughtUp,
		             s_frameRebased, s_frameDroppedMs);
	}

	s_frameLastLine = SDL_GetPerformanceCounter();
	s_frameEmitted = 0;
	s_frameCaughtUp = 0;
	s_frameRebased = 0;
	s_frameDroppedMs = 0.0;
}

internal u64 Native_CounterFromMicroseconds(u64 freq, u64 microseconds)
{
	return (freq * microseconds) / 1000000;
}

internal void Native_AdvanceVBlankTarget(void)
{
	const u64 freq = SDL_GetPerformanceFrequency();
	// counter ticks per vblank = freq * (897619 / 53693175) sec, kept exact with a
	// running remainder. freq*897619 fits u64 for any realistic QPC frequency.
	const u64 numer = freq * NATIVE_VBLANK_GPU_CYCLES;

	s_nextVBlankCounter += numer / NATIVE_GPU_CLOCK_HZ;
	s_vblankRemainder += numer % NATIVE_GPU_CLOCK_HZ;
	if (s_vblankRemainder >= NATIVE_GPU_CLOCK_HZ)
	{
		s_nextVBlankCounter++;
		s_vblankRemainder -= NATIVE_GPU_CLOCK_HZ;
	}
}

internal void Native_EnsureVBlankTarget(void)
{
	const u64 now = SDL_GetPerformanceCounter();

	if (s_nextVBlankCounter == 0)
	{
		s_nextVBlankCounter = now;
		s_vblankRemainder = 0;
		Native_AdvanceVBlankTarget();

		// The start of the clock, for column t of the frame log; plus the
		// header line that says what the columns measure. The first frame line
		// measures dt from here.
		s_frameClockStart = now;
		s_frameLastLine = now;
		if (g_cfg_frameLog)
		{
			Platform_Log("[CTR Frame] mode %s, audio render mode deterministic %d, disturbing switch seed %d\n",
			             g_cfg_deterministic ? "deterministic (VSync without catching up, the clock restarts on lateness)" : "classic (VSync catches up by wall clock)",
			             NativeAudio_IsDeterministicRenderMode(), g_cfg_injectDelaySeed);
			Platform_Log("[CTR Frame] columns: bild = frames since start (Platform_EndFrame, after present, snapshot and disturbance delay); vblank = VBlank counter at the end of the frame; "
			             "t = ms since start of the VBlank clock; dt = ms since the previous line, that is logic, drawing, present, snapshot and delay of this frame; "
			             "emittiert = VBlanks since the previous line; nachgeholt = of them caught up by wall clock without waiting (classic only); neu = clock restarts on a backlog above %d (classic only); "
			             "verfallen = lateness dropped in measuring mode in ms. boot lines: one per VSync call before the first frame, vblank = counter after catching up, before waiting\n",
			             NATIVE_VSYNC_CATCHUP_MAX);
		}
	}
}

// MEASURING MODE: if the target lies behind the wall clock, the clock starts
// anew at now. The next VBlank then comes at once, the one after that one window
// later; nothing is caught up, the lateness expires (column
// "verfallen" in the frame log).
internal void Native_DropLateTarget(void)
{
	const u64 now = SDL_GetPerformanceCounter();

	if (now > s_nextVBlankCounter)
	{
		const u64 freq = SDL_GetPerformanceFrequency();

		if (freq > 0)
		{
			s_frameDroppedMs += (double)(now - s_nextVBlankCounter) * 1000.0 / (double)freq;
		}

		s_nextVBlankCounter = now;
		s_vblankRemainder = 0;
	}
}

internal void Native_WaitUntilVBlankTarget(void)
{
	const u64 freq = SDL_GetPerformanceFrequency();
	const u64 spinWindow = Native_CounterFromMicroseconds(freq, NATIVE_VSYNC_SPIN_US);

	NativePerf_BeginScope(NATIVE_PERF_BUCKET_VSYNC_WAIT);
	while (1)
	{
		const u64 now = SDL_GetPerformanceCounter();

		if (now >= s_nextVBlankCounter)
		{
			NativePerf_EndScope(NATIVE_PERF_BUCKET_VSYNC_WAIT);
			return;
		}

		u64 remaining = s_nextVBlankCounter - now;
		if (remaining <= spinWindow)
		{
			// NOTE(penta3): OS sleeps can wake late. Sleep while safely far from
			// the VBlank target (high-res waitable timer), then spin only this
			// final small window so the native VBlank emitter is paced by our
			// clock, not the OS scheduler.
			while (SDL_GetPerformanceCounter() < s_nextVBlankCounter)
			{
			}

			NativePerf_EndScope(NATIVE_PERF_BUCKET_VSYNC_WAIT);
			return;
		}

		u64 sleepUs = ((remaining - spinWindow) * 1000000) / freq;
		if (sleepUs > 0)
		{
			// Cross-platform precise sleep: SDL_DelayPrecise uses the best per-OS
			// primitive (Win32 high-res waitable timer, Linux clock_nanosleep) and
			// yields the CPU instead of busy-waiting. Waking slightly late is safe:
			// the vblank schedule is absolute, so no drift accumulates and the loop
			// re-checks against the target.
			SDL_DelayPrecise(sleepUs * 1000ull);
		}
	}
}

internal void Native_EmitVBlank(void)
{
	NativeRCnt_EmitVBlank();

	if (vsync_callback != NULL)
	{
		vsync_callback();
	}

	NativeAudio_StepVBlank();
	s_nativeVBlankCount++;
	s_frameEmitted++;
}

internal int Native_CatchUpDueVBlanks(void)
{
	int emittedVBlanks = 0;

	Native_EnsureVBlankTarget();

	// NOTE(aalhendi): Native host stalls can be much longer than retail frame
	// stalls, for example during window dragging or a debugger break. Replay a few
	// late VBlanks normally, but rebase pathological stalls instead of bursting
	// many callbacks into one host frame.
	{
		const u64 now = SDL_GetPerformanceCounter();

		if (now >= s_nextVBlankCounter)
		{
			const u64 freq = SDL_GetPerformanceFrequency();
			const u64 step = (freq * NATIVE_VBLANK_GPU_CYCLES) / NATIVE_GPU_CLOCK_HZ;
			const u64 dueApprox = ((now - s_nextVBlankCounter) / step) + 1;

			if (dueApprox > NATIVE_VSYNC_CATCHUP_MAX)
			{
				s_nextVBlankCounter = now;
				s_vblankRemainder = 0;
				Native_AdvanceVBlankTarget();
				s_frameRebased++;
				return 0;
			}
		}
	}

	while (SDL_GetPerformanceCounter() >= s_nextVBlankCounter)
	{
		const u64 now = SDL_GetPerformanceCounter();

		Native_EmitVBlank();
		emittedVBlanks++;

		if (emittedVBlanks >= NATIVE_VSYNC_CATCHUP_MAX)
		{
			// NOTE(aalhendi): Keep normal late frames faithful, but rebase if the
			// due count grew past the cap while we were replaying.
			s_nextVBlankCounter = now;
			s_vblankRemainder = 0;
			Native_AdvanceVBlankTarget();
			s_frameRebased++;
			break;
		}

		Native_AdvanceVBlankTarget();
	}

	s_frameCaughtUp += emittedVBlanks;
	return emittedVBlanks;
}

internal void Native_WaitAndEmitVBlank(void)
{
	Native_EnsureVBlankTarget();
	if (g_cfg_deterministic)
	{
		Native_DropLateTarget();
	}
	Native_WaitUntilVBlankTarget();
	Native_EmitVBlank();
	Native_AdvanceVBlankTarget();
}

int VSync(int mode)
{
	int emittedVBlanks;

	if (mode < 0)
	{
		return s_nativeVBlankCount;
	}

	int requestedVBlanks = (mode == 0) ? 1 : mode;
	emittedVBlanks = 0;

#if defined(CTR_INTERNAL)
	if (NativeReplayScheduler_ConsumeVSyncPacket(requestedVBlanks, &emittedVBlanks))
	{
		for (s32 i = 0; i < emittedVBlanks; i++)
		{
			Native_WaitAndEmitVBlank();
		}

		return s_nativeVBlankCount;
	}
#endif

	// The boot phase: every VSync call before the first frame gets its
	// number, before it the disturbance delay (it simulates slow loading),
	// after it the boot line.
	if (s_frameNumber == 0)
	{
		s_bootVSyncCalls++;
		Native_InjectDelayIfDue("boot vsync", s_bootVSyncCalls, 0x9e3779b9u);
	}

	// MEASURING MODE: no catching up by wall clock, only the requested VBlanks.
	if (!g_cfg_deterministic)
	{
		emittedVBlanks += Native_CatchUpDueVBlanks();
	}

	if ((s_frameNumber == 0) && g_cfg_frameLog)
	{
		Platform_Log("[CTR Frame] boot vsync %d mode %d nachgeholt %d vblank %d t %.2f\n", s_bootVSyncCalls, mode, emittedVBlanks, s_nativeVBlankCount,
		             Native_MsSince(s_frameClockStart));
	}

	for (s32 i = 0; i < requestedVBlanks; i++)
	{
		Native_WaitAndEmitVBlank();
		emittedVBlanks++;
	}

#if defined(CTR_INTERNAL)
	NativeReplayScheduler_RecordVSyncPacket(emittedVBlanks);
#endif

	return s_nativeVBlankCount;
}

int Platform_GetVBlankCount(void)
{
	return s_nativeVBlankCount;
}

void Platform_WaitUntilVBlank(int targetVBlank)
{
	int emittedVBlanks = 0;
	int requestedVBlanks = targetVBlank - s_nativeVBlankCount;

	if (requestedVBlanks <= 0)
	{
		return;
	}

#if defined(CTR_INTERNAL)
	if (NativeReplayScheduler_ConsumeVSyncPacket(requestedVBlanks, &emittedVBlanks))
	{
		for (s32 i = 0; i < emittedVBlanks; i++)
		{
			Native_WaitAndEmitVBlank();
		}

		return;
	}
#endif

	// MEASURING MODE: as in VSync, no catching up.
	if (!g_cfg_deterministic)
	{
		emittedVBlanks += Native_CatchUpDueVBlanks();
	}

	while (s_nativeVBlankCount < targetVBlank)
	{
		Native_WaitAndEmitVBlank();
		emittedVBlanks++;
	}

#if defined(CTR_INTERNAL)
	NativeReplayScheduler_RecordVSyncPacket(emittedVBlanks);
#endif
}
