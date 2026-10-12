#include <platform/engine.h>

// ENGINE MODEL - retail (PS1 format) models to engine meshes (step 3). See
// include/platform/engine.h. Skeleton: every function is a stub.

int Engine_ModelImport(const struct Model *model, const char *name, EngineModelImport *out)
{
	(void)model;
	(void)name;
	if (out != NULL)
	{
		out->mesh = -1;
	}
	return 0;
}

int Engine_ModelFrame(const EngineModelImport *imp, int anim, int frame)
{
	(void)imp;
	(void)anim;
	(void)frame;
	return -1;
}

int Engine_SelfTestModel(int *checks)
{
	(void)checks;
	return 0;
}
