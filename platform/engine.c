#include <platform/engine.h>

#include <string.h>

// THE NATIVE ENGINE - the switches. Everything else lives in the engine_*.c
// files; see include/platform/engine.h.

int g_cfg_engine = 0;
int g_cfg_engineTrack = 0;
int g_cfg_engineTestScene = 0;
int g_cfg_engineMenuScene = 0;
int g_cfg_engineHide = 0; // ENGINE_HIDE_* bits of --engine-hide
int g_cfg_engineFail = 0; // ENGINE_FAIL_* of --engine-fail
int g_cfg_engineOff = 0;  // --no-engine

// THE FALLBACK (see include/platform/engine.h, section FALLBACK). Every hook of
// the game asks g_cfg_engine first, so with the switches at 0 each one returns
// after that one compare, as in a run without --engine. g_cfg_nativePreview
// stays as it is: the device was made with it (depth-capable passes, texture
// manager), and the native layer draws the custom drivers again as it does
// with --native-preview alone.
global_variable const char *s_engineFallbackStage = NULL;

internal void Engine_SwitchesOff(void)
{
	g_cfg_engine = 0;
	g_cfg_engineTrack = 0;
	g_cfg_engineTestScene = 0;
	g_cfg_engineMenuScene = 0;
	g_cfg_engineHide = 0;
}

void Engine_Fallback(const char *stage, const char *reason)
{
	// Once: a second failure after the first changes nothing.
	if (!g_cfg_engine)
	{
		return;
	}
	s_engineFallbackStage = (stage != NULL) ? stage : "?";
	Platform_Log("[CTR Engine] FALLBACK at %s: %s - the engine is off for the rest of this run, the PS1 path draws\n", s_engineFallbackStage,
	             (reason != NULL) ? reason : "no reason given");
	Engine_SwitchesOff();
}

int Engine_FellBack(void)
{
	return (s_engineFallbackStage != NULL) ? 1 : 0;
}

// The value of --engine-fail as ENGINE_FAIL_*, -1 for any other word.
int Engine_FailModeFromName(const char *name)
{
	if (name == NULL)
	{
		return -1;
	}
	if (strcmp(name, "start") == 0)
	{
		return ENGINE_FAIL_START;
	}
	if (strcmp(name, "track") == 0)
	{
		return ENGINE_FAIL_TRACK;
	}
	return -1;
}

// THE SETTING "video engine 0|1" (ctr-settings.cfg, platform/native_platform.c;
// prepared, no menu entry, off by default). What it does in this run, from the
// file's value and the command line, in this order: 0 does nothing; --no-engine
// beats it; an --engine on the command line decides by itself (with or without
// --engine-track); without --dev it stays off in this build (the engine is a
// developer feature until that is decided); else 1 is --engine --engine-track.
int Engine_SettingDecision(int setting, int dev, int engineOnLine, int noEngine)
{
	if (setting == 0)
	{
		return ENGINE_SETTING_OFF;
	}
	if (noEngine)
	{
		return ENGINE_SETTING_NO_ENGINE;
	}
	if (engineOnLine)
	{
		return ENGINE_SETTING_LINE;
	}
	return dev ? ENGINE_SETTING_ON : ENGINE_SETTING_NEEDS_DEV;
}

// From Platform_SettingsPreloadDisplay, before the window (the device and the
// native groundwork come into being in Platform_Init, as for nativedrivers).
void Engine_SettingApply(int setting)
{
	extern int g_cfg_dev;
	extern int g_cfg_nativePreview;
	const int decision = Engine_SettingDecision(setting, g_cfg_dev, g_cfg_engine, g_cfg_engineOff);

	if (decision == ENGINE_SETTING_ON)
	{
		g_cfg_engine = 1;
		g_cfg_engineTrack = 1;
		g_cfg_nativePreview = 1;
		Platform_Log("[CTR Engine] settings: video engine 1 (with --dev): the engine and its track on for this run; native groundwork on\n");
	}
	else if (decision == ENGINE_SETTING_NEEDS_DEV)
	{
		Platform_Log("[CTR Engine] settings: video engine 1 acts only with --dev in this build - the engine stays off\n");
	}
	else if (decision == ENGINE_SETTING_NO_ENGINE)
	{
		Platform_Log("[CTR Engine] settings: video engine 1 not used - --no-engine\n");
	}
	else if (decision == ENGINE_SETTING_LINE)
	{
		Platform_Log("[CTR Engine] settings: video engine 1 not used - --engine on the command line decides\n");
	}
}
