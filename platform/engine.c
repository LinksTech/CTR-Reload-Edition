#include <platform/engine.h>

#include <stdio.h>
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

// --------------------------------------------------------------------------
// SELFTEST (Engine_SelfTest, ctest engine_selftest): the value reader of
// --engine-fail, the rule of the setting, and the fallback on made-up switches
// (every switch is put back afterwards).

#define ENGSW_CHECK(cond, ...)                            \
	do                                                    \
	{                                                     \
		(*checks)++;                                      \
		if (!(cond))                                      \
		{                                                 \
			printf("engine selftest: switches " __VA_ARGS__); \
			printf("\n");                                 \
			failures++;                                   \
		}                                                 \
	} while (0)

int Engine_SelfTestSwitches(int *checks)
{
	int failures = 0;

	// (1) --engine-fail
	ENGSW_CHECK(Engine_FailModeFromName("start") == ENGINE_FAIL_START, "fail: start");
	ENGSW_CHECK(Engine_FailModeFromName("track") == ENGINE_FAIL_TRACK, "fail: track");
	ENGSW_CHECK(Engine_FailModeFromName("Start") < 0, "fail: Start accepted");
	ENGSW_CHECK(Engine_FailModeFromName("starts") < 0, "fail: starts accepted");
	ENGSW_CHECK(Engine_FailModeFromName("") < 0, "fail: empty word accepted");
	ENGSW_CHECK(Engine_FailModeFromName(NULL) < 0, "fail: NULL accepted");

	// (2) the setting: 0 nothing, --no-engine first, then the command line, then --dev
	ENGSW_CHECK(Engine_SettingDecision(0, 1, 0, 0) == ENGINE_SETTING_OFF, "setting: 0 with --dev not off");
	ENGSW_CHECK(Engine_SettingDecision(0, 0, 1, 1) == ENGINE_SETTING_OFF, "setting: 0 with everything not off");
	ENGSW_CHECK(Engine_SettingDecision(1, 1, 0, 0) == ENGINE_SETTING_ON, "setting: 1 with --dev not on");
	ENGSW_CHECK(Engine_SettingDecision(1, 0, 0, 0) == ENGINE_SETTING_NEEDS_DEV, "setting: 1 without --dev not refused");
	ENGSW_CHECK(Engine_SettingDecision(1, 1, 0, 1) == ENGINE_SETTING_NO_ENGINE, "setting: --no-engine does not win");
	ENGSW_CHECK(Engine_SettingDecision(1, 0, 1, 1) == ENGINE_SETTING_NO_ENGINE, "setting: --no-engine does not win over the line");
	ENGSW_CHECK(Engine_SettingDecision(1, 1, 1, 0) == ENGINE_SETTING_LINE, "setting: --engine on the line does not decide");
	ENGSW_CHECK(Engine_SettingDecision(1, 0, 1, 0) == ENGINE_SETTING_LINE, "setting: --engine on the line without --dev");

	// (3) the fallback: every switch to 0, once, the stage kept; nothing without the engine
	{
		const int saved[6] = {g_cfg_engine, g_cfg_engineTrack, g_cfg_engineTestScene, g_cfg_engineMenuScene, g_cfg_engineHide, g_cfg_engineFail};
		const char *savedStage = s_engineFallbackStage;

		g_cfg_engine = 0;
		s_engineFallbackStage = NULL;
		Engine_Fallback("selftest", "without the engine");
		ENGSW_CHECK(!Engine_FellBack(), "fallback: acted without the engine");

		g_cfg_engine = 1;
		g_cfg_engineTrack = 1;
		g_cfg_engineTestScene = 1;
		g_cfg_engineMenuScene = 1;
		g_cfg_engineHide = ENGINE_HIDE_SKY | ENGINE_HIDE_WATER;
		Engine_Fallback("selftest", "a made-up failure (this line is expected)");
		ENGSW_CHECK(Engine_FellBack(), "fallback: not noted");
		ENGSW_CHECK(!g_cfg_engine && !g_cfg_engineTrack && !g_cfg_engineTestScene && !g_cfg_engineMenuScene && !g_cfg_engineHide,
		            "fallback: a switch stayed on (%d %d %d %d %d)", g_cfg_engine, g_cfg_engineTrack, g_cfg_engineTestScene, g_cfg_engineMenuScene,
		            g_cfg_engineHide);
		ENGSW_CHECK((s_engineFallbackStage != NULL) && (strcmp(s_engineFallbackStage, "selftest") == 0), "fallback: stage not kept");
		Engine_Fallback("second", "a second failure");
		ENGSW_CHECK((s_engineFallbackStage != NULL) && (strcmp(s_engineFallbackStage, "selftest") == 0), "fallback: a second call changed the stage");

		g_cfg_engine = saved[0];
		g_cfg_engineTrack = saved[1];
		g_cfg_engineTestScene = saved[2];
		g_cfg_engineMenuScene = saved[3];
		g_cfg_engineHide = saved[4];
		g_cfg_engineFail = saved[5];
		s_engineFallbackStage = savedStage;
	}

	return failures;
}
