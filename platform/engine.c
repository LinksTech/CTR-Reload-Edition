#include <platform/engine.h>

// THE NATIVE ENGINE - the switches. Everything else lives in the engine_*.c
// files; see include/platform/engine.h.

int g_cfg_engine = 0;
int g_cfg_engineTrack = 0;
int g_cfg_engineTestScene = 0;
int g_cfg_engineMenuScene = 0;
int g_cfg_engineHide = 0; // ENGINE_HIDE_* bits of --engine-hide

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
