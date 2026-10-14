#include <platform/engine.h>

// ENGINE FRAME - see include/platform/engine.h.
//
// One camera per view, taken once per frame from the game's push buffers
// (MainFrame_RenderFrame, right after NativeRenderLayer_Pull) and read by the
// collectors and the draw side of the same frame.

internal EngineCamera s_engineCam[ENGINE_VIEWS];
// The game time of the frame last collected into each scene bank (Engine_FrameTime).
internal float s_engineTime[ENGINE_SCENE_BANKS];

void Engine_FrameCollect(struct GameTracker *gGT)
{
	int views = gGT->numPlyrCurrGame;
	int view;

	if (views > ENGINE_VIEWS)
	{
		views = ENGINE_VIEWS;
	}

	// The bank this frame collects into was selected by Engine_RenderFrameBegin.
	{
		const int bank = Engine_SceneBank();

		if ((bank >= 0) && (bank < ENGINE_SCENE_BANKS))
		{
			s_engineTime[bank] = (float)gGT->timer / 30.0f;
		}
	}

	for (view = 0; view < ENGINE_VIEWS; view++)
	{
		if (view < views)
		{
			Engine_CameraFromPushBuffer(&gGT->pushBuffer[view], view, &s_engineCam[view]);
		}
		else
		{
			s_engineCam[view].valid = 0;
		}
	}

	if (g_cfg_engineTestScene)
	{
		Engine_TestSceneCollect(gGT);
	}
	if (g_cfg_engineMenuScene)
	{
		Engine_MenuSceneCollect(gGT);
	}
	if (g_cfg_engineTrack)
	{
		// The sky (cell 0x3ff), the track (0x3fe, 0x3fd), then the water (0x3fc):
		// the water draws only when the track was drawn this frame.
		Engine_SkyCollect(gGT);
		Engine_TrackCollect(gGT);
		Engine_WaterCollect(gGT);
	}
}

float Engine_FrameTime(int bank)
{
	if ((bank < 0) || (bank >= ENGINE_SCENE_BANKS))
	{
		return 0.0f;
	}
	return s_engineTime[bank];
}

const EngineCamera *Engine_FrameCamera(int view)
{
	if ((view < 0) || (view >= ENGINE_VIEWS) || !s_engineCam[view].valid)
	{
		return NULL;
	}
	return &s_engineCam[view];
}
