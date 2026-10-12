#include <platform/engine.h>

// ENGINE FX - see include/platform/engine.h. Skeleton: every function is a stub.

int Engine_FxBillboard(const EngineCamera *cam, EngineVec3 centre, float halfW, float halfH, s16 roll, const u8 colour[4], EngineVertex *outV, u16 *outI, u16 baseIndex)
{
	(void)cam;
	(void)centre;
	(void)halfW;
	(void)halfH;
	(void)roll;
	(void)colour;
	(void)outV;
	(void)outI;
	(void)baseIndex;
	return 0;
}

int Engine_FxGroundQuad(EngineVec3 centre, EngineVec3 normal, EngineVec3 forward, float halfLength, float halfWidth, const u8 colour[4], EngineVertex *outV, u16 *outI, u16 baseIndex)
{
	(void)centre;
	(void)normal;
	(void)forward;
	(void)halfLength;
	(void)halfWidth;
	(void)colour;
	(void)outV;
	(void)outI;
	(void)baseIndex;
	return 0;
}

int Engine_FxRibbon(const EngineVec3 *left, const EngineVec3 *right, const u8 (*colour)[4], int points, float uScale, EngineVertex *outV, int maxVertices, u16 *outI, int maxIndices, u16 baseIndex)
{
	(void)left;
	(void)right;
	(void)colour;
	(void)points;
	(void)uScale;
	(void)outV;
	(void)maxVertices;
	(void)outI;
	(void)maxIndices;
	(void)baseIndex;
	return 0;
}

int Engine_SelfTestFx(int *checks)
{
	(void)checks;
	return 0;
}
