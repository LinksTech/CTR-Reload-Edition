#include <platform/engine.h>

// ENGINE TRACK MOSAIC - see include/platform/engine.h, section TRACK MOSAIC.
//
// Scaffold of the round of 2026-10-15: no face is a mosaic face (the track keeps
// level 2 of its mosaic faces, as before), no data texture.

void Engine_TrackMosaicBegin(int levelId)
{
	(void)levelId;
}

int Engine_TrackMosaicFace(const struct TextureLayout *group, int mirrored, int form, const u8 *slotOf, EngineVertex *v, int vertCount, u16 *keyTpage)
{
	(void)group;
	(void)mirrored;
	(void)form;
	(void)slotOf;
	(void)v;
	(void)vertCount;
	(void)keyTpage;
	return 0;
}

int Engine_TrackMosaicEnd(void)
{
	return 0;
}

void Engine_TrackMosaicUnload(void)
{
}

u32 Engine_TrackMosaicTexture(void)
{
	return 0xffffffffu;
}

int Engine_SelfTestTrackMosaic(int *checks)
{
	(void)checks;
	return 0;
}
