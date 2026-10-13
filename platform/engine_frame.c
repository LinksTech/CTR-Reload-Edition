#include <platform/engine.h>

// ENGINE FRAME - see include/platform/engine.h.
//
// One camera per view, taken once per frame from the game's push buffers
// (MainFrame_RenderFrame, right after NativeRenderLayer_Pull) and read by the
// collectors and the draw side of the same frame.

internal EngineCamera s_engineCam[ENGINE_VIEWS];

void Engine_FrameCollect(struct GameTracker *gGT)
{
	int views = gGT->numPlyrCurrGame;
	int view;

	if (views > ENGINE_VIEWS)
	{
		views = ENGINE_VIEWS;
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
		Engine_TrackCollect(gGT);
	}
}

const EngineCamera *Engine_FrameCamera(int view)
{
	if ((view < 0) || (view >= ENGINE_VIEWS) || !s_engineCam[view].valid)
	{
		return NULL;
	}
	return &s_engineCam[view];
}
