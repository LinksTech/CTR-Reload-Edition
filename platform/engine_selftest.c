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
	{
		int external = 0;

		failures += Engine_SelfTestMeshExternal(&external);
		checks += external;
	}
	failures += Engine_SelfTestFx(&checks);
	failures += Engine_SelfTestVram(&checks);
	failures += Engine_SelfTestModel(&checks);
	failures += Engine_SelfTestInstance(&checks);
	failures += Engine_SelfTestInstLight(&checks);
	failures += Engine_SelfTestExhaust(&checks);
	failures += Engine_SelfTestWeather(&checks);
	failures += Engine_SelfTestTrack(&checks);
	failures += Engine_SelfTestTrackMosaic(&checks);
	failures += Engine_SelfTestSky(&checks);
	failures += Engine_SelfTestWater(&checks);
	failures += Engine_SelfTestTour(&checks);
	failures += Engine_SelfTestOracle(&checks);
	failures += Engine_SelfTestHires(&checks);
	failures += Engine_SelfTestHiresChar(&checks);
	failures += Engine_SelfTestStaticLoad(&checks);
	failures += Engine_SelfTestStatic(&checks);
	failures += Engine_SelfTestFlycam(&checks);
	failures += Engine_SelfTestSwitches(&checks);
	failures += NativeVk_PendingRangeSelfTest(&checks);

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
