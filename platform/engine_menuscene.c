#include <platform/engine.h>

// ENGINE MENU SCENE - a custom driver drawn by the engine in the main menu
// (--engine-menuscene, round of 2026-10-13). See include/platform/engine.h, MENU
// SCENE. Phase-1 finish line Z5: a driver outside a race, through the same API
// as the race (an engine mesh of a driver set, an EngineDraw with a matrix of
// its own), turning.
//
// LOAD (stage 5, main menu level only): NativeCharGpu_LoadMenuScene uploads the
// first roster file that has a native part into a free set slot, no seat; the
// engine registers that set like every other (Engine_DriverLoad, right after).
//
// DRAW (every frame, from Engine_FrameCollect): the body with the set's pose 0
// and, if the set has an author's wheel, the four wheels at their rest places.
// Pose 0 and not a walk through the poses: the poses are a driving animation
// (suspension, steering) and the wheels at rest would not fit a body that
// moves; a still kart that turns is what the finish line asks for.
//
// THE MATRIX. As the test scene: clip = screenFromWorld * worldFromView *
// viewModel, with viewModel built in VIEW space (x right, y down, z ahead).
// Model space of a CNET is x to the left of the kart, y up, z to the front
// (native_wheels.c: left wheels have the axle +x, z front, y up), a right-handed
// frame; the view is right-handed as well (native_render_layer.c). So
//   view = Ryaw * (-x, -y, z) * K + (xOffset, 0, distance)
// is a pure rotation and scale (no mirror: unlike the test scene's box, which
// stands up with the y flip alone, no double-sided material is needed and the
// cull the engine derives from the matrix is the game's). K comes from the hull
// of the file (set->hullMin/Max, model units): the longest side of the kart is
// a third of the view's height at the chosen distance
// (K = rectH/3 * z / (H * side)), which does not depend on the file's units.
// For orientation: at rest a kart is the model times inst->scale 0xccc / 4096 =
// 0.8 world units (VehBirth.c:23,31; NativeRenderLayer_FillCharWheels), and a
// wheel is 16 model units in radius - the hull rule above stands in for both.
//
// THE WHEELS. Rest frame of a wheel (steer 0, roll 0, no wobble): left wheels
// F = identity (axle +x, up y, front z); right wheels of an author's wheel are
// the mirrored mesh with F = identity as well (FillCharWheels: the mirrored
// mesh takes F x diag(-1,1,-1), and a right wheel's F is diag(-1,1,-1)). The
// wheel mesh has the same scale as the body at rest (wheelSize 0xccc = inst
// scale 0xccc), so a wheel is the body's matrix times a translation by its
// middle (NativeCharGpu_WheelMiddle). A set with the test wheel (WHLS version 1)
// or none gets its body only.
//
// THE HI-RES DRIVER (--engine-hires-dir, round of 2026-10-15, stage H): when
// platform/engine_hires.c loaded one, the scene shows it instead of the set -
// its body (hull from its own mesh, the same model axes) and its wheel mesh at
// the four rest middles; no roster file is needed then.
//
// TIME AND INPUT. gGT->timer runs in the menu (MainFrame_GameLogic advances it
// unless PAUSE_ALL is set, MainFrame.c:193 and 243); the yaw advances by the
// ticks that passed since the last frame. Outside --deterministic the keys Q
// and E turn the kart by hand and stop the automatic turn for 3 seconds. They
// are free: no default action of native_controls.c has them (the menu mouse
// uses the wheel and clicks only); a player who rebinds an action onto one of
// them loses that key here (it is skipped, see Engine_MenuSceneKey). Read with
// SDL_GetKeyboardState as the name entry does (NikoGetEnterKey), not through
// the pad bus: nothing the game reads changes. Under --deterministic no key is
// read (as everywhere), so a measuring run only has the automatic turn.

#define ENGINE_MENUSCENE_DISTANCE 1500.0f        // view z of the kart's middle
#define ENGINE_MENUSCENE_HEIGHT_FRACTION 0.3333f // longest side / view height
#define ENGINE_MENUSCENE_X_FRACTION 0.30f        // middle at this part of half the view width, right of centre
#define ENGINE_MENUSCENE_YAW_START 2.4f          // radians: a three-quarter view of the front at the first frame
#define ENGINE_MENUSCENE_AUTO_PER_TICK 0.03f     // radians per game tick (30 per second): a turn in about 7 s
#define ENGINE_MENUSCENE_KEY_PER_TICK 0.08f
#define ENGINE_MENUSCENE_PAUSE_TICKS 90          // 3 seconds of the 30 Hz tick
#define ENGINE_MENUSCENE_MAX_TICKS 8             // a larger step of the timer is a load or a jump: ignored

// THE ORACLE FRAME (round of 2026-10-16, W2). With --level-tour-oracle the
// scene arms one oracle frame of the material comparison per run
// (Engine_OracleArm, platform/engine_oracle.c): its ENGINE_MENUSCENE_ORACLE_FRAME-th
// drawn frame counted over the whole run, before its draw and before the retail
// instances of the frame are queued (Engine_FrameCollect runs ahead of
// RenderBucket_QueueAllInstances, MainFrame_RenderFrame.c). Counted over the run
// because a tour shows the main menu only a few frames between its entries.
#define ENGINE_MENUSCENE_ORACLE_FRAME 10u

int NativeCharGpu_LoadMenuScene(void);
extern int g_cfg_deterministic; // --deterministic, native_platform.c
void Engine_OracleArm(const char *tag);

internal int s_menuSet = -1;
internal int s_menuLastTimer = -1;
internal float s_menuYaw = ENGINE_MENUSCENE_YAW_START;
internal int s_menuPauseTicks = 0;
internal unsigned s_menuFramesDrawn = 0;
internal unsigned s_menuWheelDraws = 0;
internal int s_menuLogged = 0;
internal int s_menuReportOn = 0;
internal int s_menuOracleArmed = 0;

internal void Engine_MenuSceneReport(void)
{
	if (!g_cfg_engine || !g_cfg_engineMenuScene)
	{
		return;
	}
	Platform_Log("[CTR Engine] menu scene: %u frame(s) drawn, %u wheel draw(s)\n", s_menuFramesDrawn, s_menuWheelDraws);
}

void Engine_MenuSceneLoad(void)
{
	if (!g_cfg_engine || !g_cfg_engineMenuScene)
	{
		return;
	}
	if (!s_menuReportOn)
	{
		s_menuReportOn = 1;
		Platform_AtExitReport(Engine_MenuSceneReport);
	}
	s_menuSet = NativeCharGpu_LoadMenuScene();
	s_menuLastTimer = -1;
	s_menuYaw = ENGINE_MENUSCENE_YAW_START;
	s_menuPauseTicks = 0;
	s_menuLogged = 0;
}

// 1 when the key is down and no action of the player's layout is bound to it.
internal int Engine_MenuSceneKey(const bool *keys, int scancode)
{
	int action;

	for (action = 0; action < NATIVE_CONTROLS_ACTION_COUNT; action++)
	{
		if (s_controlsKeys[action] == scancode)
		{
			return 0;
		}
	}
	return keys[scancode] ? 1 : 0;
}

void Engine_MenuSceneCollect(struct GameTracker *gGT)
{
	const EngineCamera *cam;
	const struct NativeCharGpu *gpu;
	const struct EngineDriverSet *ds;
	EngineMat4 screenFromWorld;
	EngineMat4 worldFromView;
	EngineMat4 viewModel;
	EngineMat4 worldModel;
	EngineMat4 bodyClip;
	EngineDraw draw;
	float side;
	float centre[3];
	float k;
	float c;
	float s;
	float xOffset;
	float hullMin[3];
	float hullMax[3];
	int ticks = 0;
	int cell;
	int axis;
	int mesh;
	int hires;
	int hiresWheel = -1;

	// Only the main menu level, a camera, an engine mesh: the hi-res driver of
	// --engine-hires-dir (platform/engine_hires.c) when it is loaded, else the
	// set made for the scene.
	if ((gGT->levelID != MAIN_MENU_LEVEL) || ((gGT->gameMode1 & MAIN_MENU) == 0))
	{
		return;
	}
	cam = Engine_FrameCamera(0);
	if (cam == NULL)
	{
		return;
	}
	hires = Engine_HiresMenuReady();
	if (hires)
	{
		Engine_HiresMenuParts(&mesh, &hiresWheel, hullMin, hullMax);
		ds = NULL;
		gpu = NULL;
	}
	else
	{
		if ((s_menuSet < 0) || (s_menuSet >= NATIVE_CHAR_GPU_SETS))
		{
			return;
		}
		mesh = Engine_DriverBodyMesh(s_menuSet);
		if (mesh < 0)
		{
			return;
		}
		ds = &s_engDrvSets[s_menuSet];
		gpu = ds->gpu;
		if ((gpu == NULL) || (gpu->poseTotal == 0u))
		{
			return;
		}
		for (axis = 0; axis < 3; axis++)
		{
			hullMin[axis] = gpu->hullMin[axis];
			hullMax[axis] = gpu->hullMax[axis];
		}
	}

	// The yaw: the ticks since the last frame (the first frame and a jump
	// count none), the hand on Q and E, the automatic turn paused for 3 s.
	if (s_menuLastTimer >= 0)
	{
		ticks = (int)gGT->timer - s_menuLastTimer;
		if ((ticks < 0) || (ticks > ENGINE_MENUSCENE_MAX_TICKS))
		{
			ticks = 0;
		}
	}
	s_menuLastTimer = (int)gGT->timer;
	if (!g_cfg_deterministic)
	{
		const bool *keys = SDL_GetKeyboardState(NULL);
		int hand = 0;

		if ((keys != NULL) && !keys[SDL_SCANCODE_RALT] && !keys[SDL_SCANCODE_LALT])
		{
			hand = Engine_MenuSceneKey(keys, SDL_SCANCODE_E) - Engine_MenuSceneKey(keys, SDL_SCANCODE_Q);
		}
		if (hand != 0)
		{
			s_menuYaw += (float)hand * ENGINE_MENUSCENE_KEY_PER_TICK * (float)((ticks > 0) ? ticks : 1);
			s_menuPauseTicks = ENGINE_MENUSCENE_PAUSE_TICKS;
			ticks = 0; // the pause starts now and counts down from the next tick
		}
	}
	if (s_menuPauseTicks > 0)
	{
		s_menuPauseTicks -= ticks;
	}
	else
	{
		s_menuYaw += (float)ticks * ENGINE_MENUSCENE_AUTO_PER_TICK;
	}

	// Scale from the hull: the longest side is a third of the view's height.
	side = 0.0f;
	for (axis = 0; axis < 3; axis++)
	{
		const float extent = hullMax[axis] - hullMin[axis];

		centre[axis] = 0.5f * (hullMax[axis] + hullMin[axis]);
		if (extent > side)
		{
			side = extent;
		}
	}
	if (side < 1e-3f)
	{
		return;
	}
	k = ((float)cam->rectH * ENGINE_MENUSCENE_HEIGHT_FRACTION * ENGINE_MENUSCENE_DISTANCE) / (cam->H * side);
	xOffset = ENGINE_MENUSCENE_X_FRACTION * (float)(cam->rectW >> 1) * ENGINE_MENUSCENE_DISTANCE / cam->H;

	// viewModel columns: model x, y, z, translation (see the header comment).
	c = cosf(s_menuYaw) * k;
	s = sinf(s_menuYaw) * k;
	Engine_Mat4Identity(&viewModel);
	viewModel.m[0] = -c;
	viewModel.m[2] = s;
	viewModel.m[5] = -k;
	viewModel.m[8] = s;
	viewModel.m[10] = c;
	viewModel.m[12] = -((viewModel.m[0] * centre[0]) + (viewModel.m[4] * centre[1]) + (viewModel.m[8] * centre[2])) + xOffset;
	viewModel.m[13] = -((viewModel.m[1] * centre[0]) + (viewModel.m[5] * centre[1]) + (viewModel.m[9] * centre[2]));
	viewModel.m[14] = -((viewModel.m[2] * centre[0]) + (viewModel.m[6] * centre[1]) + (viewModel.m[10] * centre[2])) + ENGINE_MENUSCENE_DISTANCE;

	Engine_CameraScreenFromWorld(cam, &screenFromWorld);
	if (!Engine_TestSceneInvertRigid(&cam->viewFromWorld, &worldFromView))
	{
		return;
	}
	Engine_Mat4Mul(&worldFromView, &viewModel, &worldModel);
	Engine_Mat4Mul(&screenFromWorld, &worldModel, &bodyClip);

	// OT cell: the level's rule, view z >> 6, a higher cell is farther (as the
	// test scene); the menu's own sprites sit in the nearer cells and stay on top.
	cell = (int)(ENGINE_MENUSCENE_DISTANCE) >> 6;
	if (cell < 0)
	{
		cell = 0;
	}
	if (cell > 0x3fe)
	{
		cell = 0x3fe;
	}

	memset(&draw, 0, sizeof(draw));
	draw.mesh = mesh;
	draw.frame = 0u;
	draw.clipFromModel = bodyClip;
	draw.tint[0] = draw.tint[1] = draw.tint[2] = draw.tint[3] = 1.0f;
	draw.materialOverride = -1;
	draw.sortDepth = ENGINE_MENUSCENE_DISTANCE;
	draw.otCell = (u32)cell;
	draw.dynMaterial = -1;

	if (!s_menuOracleArmed && (g_cfg_levelTourOracle[0] != '\0') && (s_menuFramesDrawn == ENGINE_MENUSCENE_ORACLE_FRAME))
	{
		s_menuOracleArmed = 1;
		Engine_OracleArm("menu");
		Platform_Log("[CTR Engine] menu scene: oracle frame 'menu' armed (scene frame %u) at vblank %d\n", ENGINE_MENUSCENE_ORACLE_FRAME + 1u,
		             Platform_GetVBlankCount());
	}

	if (!Engine_RenderSubmitLinked(0, &draw, gGT->pushBuffer[0].ptrOT, cell))
	{
		return;
	}
	s_menuFramesDrawn++;
	if (!s_menuLogged)
	{
		s_menuLogged = 1;
		Platform_Log("[CTR Engine] menu scene: first draw (%s %d, mesh %d)\n", hires ? "hires driver, set" : "set", hires ? -1 : s_menuSet, mesh);
	}

	// The hi-res driver: its one wheel mesh at the four rest middles (model
	// units of the body, Engine_HiresWheelMiddle at scale 1.0), not mirrored.
	if (hires)
	{
		int wheel;

		Engine_HiresNoteMenuFrame();
		for (wheel = 0; wheel < NATIVE_WHEELS_COUNT; wheel++)
		{
			EngineMat4 place;
			float m[3];

			Engine_HiresWheelMiddle(wheel, 0x1000, 0x1000, 0x1000, m);
			Engine_Mat4Identity(&place);
			place.m[12] = m[0];
			place.m[13] = m[1];
			place.m[14] = m[2];
			Engine_Mat4Mul(&bodyClip, &place, &draw.clipFromModel);
			draw.mesh = hiresWheel;
			draw.frame = 0u;
			if (Engine_RenderSubmitLinked(0, &draw, gGT->pushBuffer[0].ptrOT, cell))
			{
				s_menuWheelDraws++;
			}
		}
		return;
	}

	// The wheels at rest: the body's matrix times the translation to the
	// middle of the wheel. The right wheels use the mirrored mesh, a mesh of
	// its own (range index: bit 0 mirrored, bit 1 rear, as DrawCharItem).
	if (gpu->hasWheels && gpu->wheelOwn)
	{
		int wheel;

		for (wheel = 0; wheel < NATIVE_WHEELS_COUNT; wheel++)
		{
			const int rangeIndex = (((wheel & 1) != 0) ? 1 : 0) + ((wheel >= 2) ? 2 : 0);
			EngineMat4 place;
			double m[3];

			if (ds->wheelMesh[rangeIndex] < 0)
			{
				continue;
			}
			NativeCharGpu_WheelMiddle(gpu->wheelFront, gpu->wheelRear, wheel, m);
			Engine_Mat4Identity(&place);
			place.m[12] = (float)m[0];
			place.m[13] = (float)m[1];
			place.m[14] = (float)m[2];
			Engine_Mat4Mul(&bodyClip, &place, &draw.clipFromModel);
			draw.mesh = ds->wheelMesh[rangeIndex];
			draw.frame = 0u;
			if (Engine_RenderSubmitLinked(0, &draw, gGT->pushBuffer[0].ptrOT, cell))
			{
				s_menuWheelDraws++;
			}
		}
	}
}
