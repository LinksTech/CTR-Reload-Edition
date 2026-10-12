#include <platform/engine.h>

// ENGINE MATERIAL - see include/platform/engine.h. Skeleton: every function is a stub.

EngineTpage Engine_DecodeTpage(u16 tpage)
{
	(void)tpage;
	return (EngineTpage){0, 0, 0, 0};
}

void Engine_DecodeClut(u16 clut, int *x, int *y)
{
	(void)clut;
	(void)x;
	(void)y;
}

EngineBlend Engine_BlendFromAbr(int abr)
{
	(void)abr;
	return ENGINE_BLEND_OPAQUE;
}

u8 Engine_BlendChannel(EngineBlend blend, u8 background, u8 foreground)
{
	(void)blend;
	(void)background;
	(void)foreground;
	return 0;
}

int Engine_MaterialRegister(const EngineMaterial *m)
{
	(void)m;
	return 0;
}

const EngineMaterial *Engine_Material(int index)
{
	(void)index;
	return NULL;
}

void Engine_MaterialsReset(void)
{
}

void Engine_SetLight(const EngineLight *light)
{
	(void)light;
}

const EngineLight *Engine_GetLight(void)
{
	return NULL;
}

u8 Engine_NightChannel(int nightPercent, u8 value)
{
	(void)nightPercent;
	(void)value;
	return 0;
}

int Engine_SelfTestMaterial(int *checks)
{
	(void)checks;
	return 0;
}
