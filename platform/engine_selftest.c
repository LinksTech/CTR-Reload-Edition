#include <platform/engine.h>

// THE ENGINE SELFTEST (--engine-selftest, ctest engine_selftest): every module
// checks itself on made-up data, without a window, a disc or a game state.

int Engine_SelfTest(void)
{
	int checks = 0;
	int failures = 0;

	failures += Engine_SelfTestMath(&checks);
	failures += Engine_SelfTestCamera(&checks);
	failures += Engine_SelfTestMaterial(&checks);
	failures += Engine_SelfTestMeshScene(&checks);
	failures += Engine_SelfTestFx(&checks);
	failures += Engine_SelfTestVram(&checks);
	failures += Engine_SelfTestModel(&checks);

	if (failures == 0)
	{
		printf("engine selftest passed: %d checks, 0 failures\n", checks);
	}
	else
	{
		printf("engine selftest FAILED: %d checks, %d failures\n", checks, failures);
	}
	fflush(stdout);
	return (failures == 0) ? 0 : 1;
}
