#include <platform/engine.h>

// ENGINE CAMERA - see include/platform/engine.h. Skeleton: every function is a stub.

int Engine_CameraFromPushBuffer(const struct PushBuffer *pb, int view, EngineCamera *out)
{
	(void)pb;
	(void)view;
	(void)out;
	return 0;
}

int Engine_ProjectPoint(const EngineCamera *cam, EngineVec3 world, EngineVec3 *out)
{
	(void)cam;
	(void)world;
	(void)out;
	return 0;
}

void Engine_CameraScreenFromWorld(const EngineCamera *cam, EngineMat4 *out)
{
	(void)cam;
	(void)out;
}

int Engine_SelfTestCamera(int *checks)
{
	(void)checks;
	return 0;
}
