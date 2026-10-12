#include <platform/engine.h>

// ENGINE MATH - see include/platform/engine.h. Skeleton: every function is a stub.

void Engine_Mat4Identity(EngineMat4 *out)
{
	(void)out;
}

void Engine_Mat4Mul(const EngineMat4 *a, const EngineMat4 *b, EngineMat4 *out)
{
	(void)a;
	(void)b;
	(void)out;
}

EngineVec3 Engine_Mat4MulPoint(const EngineMat4 *m, EngineVec3 p)
{
	(void)m;
	(void)p;
	return (EngineVec3){0.0f, 0.0f, 0.0f};
}

void Engine_Mat4FromGte(const s16 r[3][3], const s32 t[3], EngineMat4 *out)
{
	(void)r;
	(void)t;
	(void)out;
}

void Engine_Mat4FromGameRot(s16 rx, s16 ry, s16 rz, EngineMat4 *out)
{
	(void)rx;
	(void)ry;
	(void)rz;
	(void)out;
}

int Engine_SelfTestMath(int *checks)
{
	(void)checks;
	return 0;
}
