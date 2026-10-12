#include <platform/engine.h>

// ENGINE RENDER - see include/platform/engine.h. Skeleton: every function is a stub.

int Engine_RenderInit(void)
{
	return 0;
}

int Engine_RenderUploadMeshes(void)
{
	return 0;
}

void Engine_RenderLevelUnload(void)
{
}

void Engine_RenderFrameBegin(int bank)
{
	(void)bank;
}

int Engine_RenderSubmitLinked(int view, const EngineDraw *draw, u32 *ot, int cell)
{
	(void)view;
	(void)draw;
	(void)ot;
	(void)cell;
	return 0;
}

int Engine_DrawMarker(u32 item, const void *clip, const void *dispenv, int onScreen, int ofsX, int ofsY)
{
	(void)item;
	(void)clip;
	(void)dispenv;
	(void)onScreen;
	(void)ofsX;
	(void)ofsY;
	return 0;
}

void Engine_RenderReport(void)
{
}
