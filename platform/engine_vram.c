#include <platform/engine.h>

// ENGINE VRAM - see include/platform/engine.h. Skeleton: every function is a stub.

int Engine_VramDecode(const u16 *vram, int pageX, int pageY, int depth, int clutX, int clutY, int u0, int v0, int w, int h, u8 *rgbaOut)
{
	(void)vram;
	(void)pageX;
	(void)pageY;
	(void)depth;
	(void)clutX;
	(void)clutY;
	(void)u0;
	(void)v0;
	(void)w;
	(void)h;
	(void)rgbaOut;
	return 0;
}

void Engine_VramTexel(const u16 *vram, int pageX, int pageY, int depth, int clutX, int clutY, int u, int v, u8 rgbaOut[4])
{
	(void)vram;
	(void)pageX;
	(void)pageY;
	(void)depth;
	(void)clutX;
	(void)clutY;
	(void)u;
	(void)v;
	(void)rgbaOut;
}

int Engine_SelfTestVram(int *checks)
{
	(void)checks;
	return 0;
}
