#ifndef PLATFORM_H
#define PLATFORM_H

struct PlatformMempackArena
{
	void *base;
	void *start;
	void *endOfMemory;
	int size;
	int backingSize;
};

// width and height are the WINDOWED size, and 0 means "work it out": a window
// in the shape the display was found to be. A fullscreen window comes back the
// size of the display either way, and these two are what it returns to on F11.
void Platform_Init(const char *title, int width, int height, int fullscreen);

// Which of the three shapes the picture is driven in, decided from the display
// the window is on. Called once at startup and again whenever that display or
// its mode changes; does nothing while an override (--aspect or a saved
// GRAPHICS setting) holds the aspect. `why` goes into the log line, so a later
// detection says what woke it.
void Platform_DetectDisplayAspect(const char *why);

// The aspect, set from the command line (or the saved GRAPHICS setting) and
// held there. Detection steps aside for it: driving 16:9 on a 21:9 monitor
// has to stay possible, and a setting that a later re-detection quietly
// overwrites is not a setting.
void Platform_SetAspectOverride(int width, int height);

// The settings that survive a restart, on disk. Loaded once at startup and
// before the command line, written every time the debug menu or the GRAPHICS
// page changes one.
// Locked means neither: a measuring run must not read a file somebody left
// tuned, nor write one.
void Platform_SettingsLoad(void);
void Platform_SettingsSave(void);
void Platform_SetSettingsLocked(int locked);

// Whether the lock is in place. The consumer is the level id directory of the containers
// (platform/native_assets.c): it is persistent state like ctr-settings.cfg
// and keeps to the same lock, for the same reason.
int Platform_SettingsLocked(void);

// The internal resolution factor, asked two ways. GetResolutionScale is what
// is in force and belongs on screen; GetResolutionScaleSetting is what was
// asked for and belongs in a file and under a menu step.
int Platform_GetResolutionScaleSetting(void);
int Platform_GetResolutionScaleMax(void);
int Platform_ResolutionIsNative(void);
int Platform_GetResolutionNativePosition(void);

// When the dither matrix is added. Here as well as beside the definition,
// because the settings table names it above where it is written.
int Platform_GetDither(void);
void Platform_SetDither(int mode);
void Platform_StepDither(void);
const char *Platform_DitherName(void);

// Anti-aliasing, OFF / 2x / 4x, asked four ways: the setting (file), the level
// asked for now (--msaa or the setting), the level in force (read back from the
// device) and its name. Step is the menu's choice; SetRun asks for a level for
// this run only, like --msaa (--msaa-at). Either takes effect at the next frame
// boundary. Beside the definition as well, for the same reason as dither.
void Platform_SetMsaaSetting(int samples);
int Platform_GetMsaaSetting(void);
void Platform_SetMsaaRun(int samples);
void Platform_StepMsaa(void);
int Platform_GetMsaaRequested(void);
int Platform_GetMsaa(void);
const char *Platform_MsaaName(int samples);

// --msaa-at: how many VBlank/level pairs a run can carry.
#define PLATFORM_MSAA_AT_MAX 32

void Platform_Shutdown(void);

// A report that is meant to go into the log at exit. Platform_Shutdown calls all
// registered ones before it closes the log - on every way out. The
// alternative was atexit, and there half of them never reached the log; why is written
// at Platform_AtExitReport in native_platform.c. Registering once is enough,
// a second registration of the same function is silently ignored.
void Platform_AtExitReport(void (*report)(void));

// Ends the game on the same route as Ctrl+Q and the window closer.
// `why` stands in the line in the log and says which of them it was.
void Platform_QuitGame(const char *why);
void Platform_InitScratchpad(void);
const struct PlatformMempackArena *Platform_InitMempackArena(void);

// How much larger the window of the memory pack may be, beyond the size
// of the console. Must be set BEFORE MEMPACK_Init - after that the
// split is fixed, and moving it afterwards would mean updating every pointer
// in it. See platform/native_memory.c.
void Platform_SetMempackExtra(u32 bytes);
u32 Platform_GetMempackExtra(void);
const struct PlatformMempackArena *Platform_GetMempackArena(void);
void Platform_BeginFrame(void);
int Platform_BeginScene(void);
void Platform_EndScene(void);
void Platform_EndFrame(void);

#if defined(CTR_INTERNAL)
// Measuring trigger: writes the emulated VRAM at the given VBlank counts.
void Platform_DumpConfigure(const char *list, const char *prefix, int exitAfterLast);

// Anchors a measuring run on the game state instead of on the VBlank count
// since boot: one run writes the quick state at saveAt, later runs load it at
// loadAt and dump a fixed number of VBlanks after that. Either may be -1.
void Platform_DumpStateAt(int saveAt, int loadAt);

// One VRAM dump at the end of the current frame, <--dump-prefix>-<name>.tga,
// for a moment no VBlank list can name in advance (--level-tour).
void Platform_DumpRequest(const char *name);
#endif
void Platform_PresentVRAMDisplay(void);
void Platform_PinVRAMDisplayFrames(int frameCount);
void Platform_PinVRAMDisplayRect(int x, int y, int w, int h, int frameCount);
int Platform_GetVBlankCount(void);
void Platform_WaitUntilVBlank(int targetVBlank);
void Platform_PollHostEvents(void);

// Pause on minimise or focus loss (native_platform.c). The
// game fetches the wish per frame (MainFrame_GameLogic) and takes it on the
// BTN_START route; afterwards it says what came of it.
#define PLATFORM_FOCUS_PAUSE_REFUSED 0
#define PLATFORM_FOCUS_PAUSE_OPENED 1
#define PLATFORM_FOCUS_PAUSE_ALREADY 2
int Platform_TakeFocusPauseWish(void);
void Platform_NoteFocusPause(int outcome, int levelID, unsigned int gameMode1, int loading);
int Platform_PollInput(void);

#if defined(CTR_NATIVE)
int NikoGetEnterKey(void);
#endif

#endif
