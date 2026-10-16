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
int g_cfg_engineDefault = 0; // 1 = the engine of this run comes from the default (main.c), not from --engine

// THE FALLBACK (see include/platform/engine.h, section FALLBACK). Every hook of
// the game asks g_cfg_engine first, so with the switches at 0 each one returns
// after that one compare, as in a run without --engine. g_cfg_nativePreview
// stays as it is: the device was made with it (depth-capable passes, texture
// manager), and the native layer draws the custom drivers again as it does
// with --native-preview alone.
global_variable const char *s_engineFallbackStage = NULL;

// PER LEVEL (engine.h, FALLBACK and DEFAULT): the switches a level fallback or
// the players rule of the default took off for one level only, given back by
// Engine_LevelEnd at the next level change. 0 = nothing taken.
#define ENGINE_LEVEL_OFF_NONE 0
#define ENGINE_LEVEL_OFF_FALLBACK 1
#define ENGINE_LEVEL_OFF_PLAYERS 2
global_variable int s_engineLevelOff = ENGINE_LEVEL_OFF_NONE;
global_variable int s_engineLevelSaved[5];

internal void Engine_SwitchesOff(void)
{
	g_cfg_engine = 0;
	g_cfg_engineTrack = 0;
	g_cfg_engineTestScene = 0;
	g_cfg_engineMenuScene = 0;
	g_cfg_engineHide = 0;
}

// Which failures cost the rest of the run and which only the level being
// loaded. The start (programs, rings, buffer: Engine_RenderInit) is tried once
// per run, so it stays off for the run. A level the engine cannot load or
// upload ("track load", Engine_TrackLoad; "level load",
// Engine_RenderUploadMeshes) is drawn by the PS1 path; the next level tries
// the engine again. Any other or unknown stage counts as the start: the
// conservative side.
int Engine_FallbackScope(const char *stage)
{
	if ((stage != NULL) && ((strcmp(stage, "track load") == 0) || (strcmp(stage, "level load") == 0)))
	{
		return ENGINE_FALLBACK_LEVEL;
	}
	return ENGINE_FALLBACK_RUN;
}

internal void Engine_LevelSwitchesSave(int why)
{
	s_engineLevelSaved[0] = g_cfg_engine;
	s_engineLevelSaved[1] = g_cfg_engineTrack;
	s_engineLevelSaved[2] = g_cfg_engineTestScene;
	s_engineLevelSaved[3] = g_cfg_engineMenuScene;
	s_engineLevelSaved[4] = g_cfg_engineHide;
	s_engineLevelOff = why;
	Engine_SwitchesOff();
}

void Engine_Fallback(const char *stage, const char *reason)
{
	// Once per level: a second failure after the first changes nothing.
	if (!g_cfg_engine)
	{
		return;
	}
	if (s_engineFallbackStage == NULL)
	{
		s_engineFallbackStage = (stage != NULL) ? stage : "?";
	}
	if (Engine_FallbackScope(stage) == ENGINE_FALLBACK_LEVEL)
	{
		Platform_Log("[CTR Engine] FALLBACK at %s: %s - the engine is off for this level, the PS1 path draws it; the next level tries the engine again\n",
		             (stage != NULL) ? stage : "?", (reason != NULL) ? reason : "no reason given");
		Engine_LevelSwitchesSave(ENGINE_LEVEL_OFF_FALLBACK);
		// --engine-fail track forces the first track load only (its help): the
		// next level loads as it would.
		if (g_cfg_engineFail == ENGINE_FAIL_TRACK)
		{
			g_cfg_engineFail = 0;
		}
		return;
	}
	Platform_Log("[CTR Engine] FALLBACK at %s: %s - the engine is off for the rest of this run, the PS1 path draws\n", (stage != NULL) ? stage : "?",
	             (reason != NULL) ? reason : "no reason given");
	s_engineLevelOff = ENGINE_LEVEL_OFF_NONE;
	Engine_SwitchesOff();
}

// THE DEFAULT (engine.h, DEFAULT): the engine and its track are on when the
// command line names neither --engine nor --no-engine.
int Engine_DefaultOn(int engineOnLine, int noEngine)
{
	return (!engineOnLine && !noEngine) ? 1 : 0;
}

// From game/LOAD/LOAD_TenStages.c at every level change after the first boot,
// BEFORE the engine's unload of the level that goes: the switches one level
// had off come back, so the unload clears what the engine left of it.
void Engine_LevelEnd(void)
{
	if (s_engineLevelOff == ENGINE_LEVEL_OFF_NONE)
	{
		return;
	}
	g_cfg_engine = s_engineLevelSaved[0];
	g_cfg_engineTrack = s_engineLevelSaved[1];
	g_cfg_engineTestScene = s_engineLevelSaved[2];
	g_cfg_engineMenuScene = s_engineLevelSaved[3];
	g_cfg_engineHide = s_engineLevelSaved[4];
	s_engineLevelOff = ENGINE_LEVEL_OFF_NONE;
}

// From the same place, AFTER that unload, with the number of players of the
// level about to load: the default engine draws one-player levels only; a
// level of more players (split screen, battle) goes to the PS1 path. An
// --engine named on the command line keeps the engine on, as before.
void Engine_LevelBegin(int players)
{
	if (!g_cfg_engine || !g_cfg_engineDefault || (players <= 1))
	{
		return;
	}
	Platform_Log("[CTR Engine] default: %d players - the PS1 path draws this level (the engine is the default for one player)\n", players);
	Engine_LevelSwitchesSave(ENGINE_LEVEL_OFF_PLAYERS);
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
// prepared, no menu entry). What it does in this run, from the file's value and
// the command line, in this order: 0 does nothing (it is no way out - that is
// --no-engine alone); --no-engine beats it; an --engine on the command line
// decides by itself (with or without --engine-track); with the engine on by
// default it changes nothing; without --dev it stays off (the key stays a
// developer's); else 1 is --engine --engine-track.
int Engine_SettingDecision(int setting, int dev, int engineOnLine, int noEngine, int byDefault)
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
	if (byDefault)
	{
		return ENGINE_SETTING_DEFAULT;
	}
	return dev ? ENGINE_SETTING_ON : ENGINE_SETTING_NEEDS_DEV;
}

// From Platform_SettingsPreloadDisplay, before the window (the device and the
// native groundwork come into being in Platform_Init, as for nativedrivers).
void Engine_SettingApply(int setting)
{
	extern int g_cfg_dev;
	extern int g_cfg_nativePreview;
	const int decision = Engine_SettingDecision(setting, g_cfg_dev, g_cfg_engine && !g_cfg_engineDefault, g_cfg_engineOff, g_cfg_engineDefault);

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
	else if (decision == ENGINE_SETTING_DEFAULT)
	{
		Platform_Log("[CTR Engine] settings: video engine 1 changes nothing - the engine is on by default\n");
	}
}

// --------------------------------------------------------------------------
// SELFTEST (Engine_SelfTest, ctest engine_selftest): the value reader of
// --engine-fail, the rule of the setting, the default, and the fallback per run
// and per level on made-up switches (every switch is put back afterwards).

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
	ENGSW_CHECK(Engine_SettingDecision(0, 1, 0, 0, 0) == ENGINE_SETTING_OFF, "setting: 0 with --dev not off");
	ENGSW_CHECK(Engine_SettingDecision(0, 0, 1, 1, 0) == ENGINE_SETTING_OFF, "setting: 0 with everything not off");
	ENGSW_CHECK(Engine_SettingDecision(1, 1, 0, 0, 0) == ENGINE_SETTING_ON, "setting: 1 with --dev not on");
	ENGSW_CHECK(Engine_SettingDecision(1, 0, 0, 0, 0) == ENGINE_SETTING_NEEDS_DEV, "setting: 1 without --dev not refused");
	ENGSW_CHECK(Engine_SettingDecision(1, 1, 0, 1, 0) == ENGINE_SETTING_NO_ENGINE, "setting: --no-engine does not win");
	ENGSW_CHECK(Engine_SettingDecision(1, 0, 1, 1, 0) == ENGINE_SETTING_NO_ENGINE, "setting: --no-engine does not win over the line");
	ENGSW_CHECK(Engine_SettingDecision(1, 1, 1, 0, 0) == ENGINE_SETTING_LINE, "setting: --engine on the line does not decide");
	ENGSW_CHECK(Engine_SettingDecision(1, 0, 1, 0, 0) == ENGINE_SETTING_LINE, "setting: --engine on the line without --dev");
	ENGSW_CHECK(Engine_SettingDecision(1, 0, 0, 0, 1) == ENGINE_SETTING_DEFAULT, "setting: 1 under the default not 'changes nothing'");
	ENGSW_CHECK(Engine_SettingDecision(1, 1, 0, 0, 1) == ENGINE_SETTING_DEFAULT, "setting: 1 with --dev under the default not 'changes nothing'");
	ENGSW_CHECK(Engine_SettingDecision(0, 1, 0, 0, 1) == ENGINE_SETTING_OFF, "setting: 0 under the default not 'nothing'");
	ENGSW_CHECK(Engine_SettingDecision(1, 1, 0, 1, 0) == ENGINE_SETTING_NO_ENGINE, "setting: --no-engine does not win over --dev");

	// (2b) the default: on unless the command line names --engine or --no-engine
	ENGSW_CHECK(Engine_DefaultOn(0, 0) == 1, "default: not on without switches");
	ENGSW_CHECK(Engine_DefaultOn(1, 0) == 0, "default: acted over --engine");
	ENGSW_CHECK(Engine_DefaultOn(0, 1) == 0, "default: acted over --no-engine");
	ENGSW_CHECK(Engine_DefaultOn(1, 1) == 0, "default: acted over both");

	// (2c) the scope of a fallback: the level for a load, the run for the start and anything unknown
	ENGSW_CHECK(Engine_FallbackScope("track load") == ENGINE_FALLBACK_LEVEL, "scope: track load not per level");
	ENGSW_CHECK(Engine_FallbackScope("level load") == ENGINE_FALLBACK_LEVEL, "scope: level load not per level");
	ENGSW_CHECK(Engine_FallbackScope("start") == ENGINE_FALLBACK_RUN, "scope: start not for the run");
	ENGSW_CHECK(Engine_FallbackScope("Track load") == ENGINE_FALLBACK_RUN, "scope: Track load accepted");
	ENGSW_CHECK(Engine_FallbackScope("") == ENGINE_FALLBACK_RUN, "scope: empty stage per level");
	ENGSW_CHECK(Engine_FallbackScope(NULL) == ENGINE_FALLBACK_RUN, "scope: NULL per level");

	// (3) the fallback: every switch to 0, once, the stage kept; nothing without the engine
	{
		const int saved[8] = {g_cfg_engine,     g_cfg_engineTrack, g_cfg_engineTestScene, g_cfg_engineMenuScene,
		                      g_cfg_engineHide, g_cfg_engineFail,  g_cfg_engineDefault,   s_engineLevelOff};
		int savedLevel[5];
		const char *savedStage = s_engineFallbackStage;

		memcpy(savedLevel, s_engineLevelSaved, sizeof(savedLevel));
		s_engineLevelOff = ENGINE_LEVEL_OFF_NONE;

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
		Engine_LevelEnd();
		ENGSW_CHECK(!g_cfg_engine && !g_cfg_engineTrack, "fallback: the next level brought back a run fallback");

		// (4) per level: a load failure takes the switches for this level only,
		// the next level change gives them back exactly; --engine-fail track fires once
		s_engineFallbackStage = NULL;
		g_cfg_engine = 1;
		g_cfg_engineTrack = 1;
		g_cfg_engineTestScene = 0;
		g_cfg_engineMenuScene = 1;
		g_cfg_engineHide = ENGINE_HIDE_WATER;
		g_cfg_engineFail = ENGINE_FAIL_TRACK;
		Engine_Fallback("track load", "a made-up level failure (this line is expected)");
		ENGSW_CHECK(Engine_FellBack(), "level: fallback not noted");
		ENGSW_CHECK(!g_cfg_engine && !g_cfg_engineTrack && !g_cfg_engineMenuScene && !g_cfg_engineHide, "level: a switch stayed on");
		ENGSW_CHECK(g_cfg_engineFail == 0, "level: --engine-fail track would fire again (%d)", g_cfg_engineFail);
		Engine_Fallback("level load", "a second failure in the same level");
		ENGSW_CHECK(!g_cfg_engine, "level: a second failure switched something on");
		Engine_LevelEnd();
		ENGSW_CHECK(g_cfg_engine == 1 && g_cfg_engineTrack == 1 && g_cfg_engineTestScene == 0 && g_cfg_engineMenuScene == 1 &&
		                g_cfg_engineHide == ENGINE_HIDE_WATER,
		            "level: not given back as before (%d %d %d %d %d)", g_cfg_engine, g_cfg_engineTrack, g_cfg_engineTestScene, g_cfg_engineMenuScene,
		            g_cfg_engineHide);
		ENGSW_CHECK(g_cfg_engineFail == 0, "level: --engine-fail came back");
		Engine_LevelEnd();
		ENGSW_CHECK(g_cfg_engine == 1 && g_cfg_engineTrack == 1, "level: a second level change took something");
		Engine_Fallback("level load", "a made-up level failure (this line is expected)");
		ENGSW_CHECK(!g_cfg_engine, "level: level load did not fall back");
		Engine_LevelEnd();
		ENGSW_CHECK(g_cfg_engine == 1, "level: level load not given back");
		Engine_Fallback("start", "a made-up start failure (this line is expected)");
		Engine_LevelEnd();
		ENGSW_CHECK(!g_cfg_engine && !g_cfg_engineTrack, "level: a start failure came back at the next level");

		// (5) the players rule of the default: more than one player, the PS1
		// path for that level; one player, or --engine by name, unchanged
		g_cfg_engine = 1;
		g_cfg_engineTrack = 1;
		g_cfg_engineMenuScene = 0;
		g_cfg_engineHide = 0;
		g_cfg_engineDefault = 1;
		Engine_LevelBegin(1);
		ENGSW_CHECK(g_cfg_engine == 1 && g_cfg_engineTrack == 1, "players: one player took the engine");
		Engine_LevelBegin(2);
		ENGSW_CHECK(!g_cfg_engine && !g_cfg_engineTrack, "players: two players kept the default engine");
		Engine_LevelBegin(4);
		ENGSW_CHECK(!g_cfg_engine, "players: a second call switched something on");
		Engine_LevelEnd();
		ENGSW_CHECK(g_cfg_engine == 1 && g_cfg_engineTrack == 1, "players: not given back for the next level");
		g_cfg_engineDefault = 0;
		Engine_LevelBegin(2);
		ENGSW_CHECK(g_cfg_engine == 1 && g_cfg_engineTrack == 1, "players: --engine by name lost the engine");
		g_cfg_engine = 0;
		g_cfg_engineTrack = 0;
		g_cfg_engineDefault = 1;
		Engine_LevelBegin(2);
		Engine_LevelEnd();
		ENGSW_CHECK(!g_cfg_engine && !g_cfg_engineTrack, "players: switched on without the engine");

		g_cfg_engine = saved[0];
		g_cfg_engineTrack = saved[1];
		g_cfg_engineTestScene = saved[2];
		g_cfg_engineMenuScene = saved[3];
		g_cfg_engineHide = saved[4];
		g_cfg_engineFail = saved[5];
		g_cfg_engineDefault = saved[6];
		s_engineLevelOff = saved[7];
		memcpy(s_engineLevelSaved, savedLevel, sizeof(savedLevel));
		s_engineFallbackStage = savedStage;
	}

	return failures;
}
