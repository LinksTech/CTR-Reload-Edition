#include <platform/engine.h>

// ENGINE CAMERA - see include/platform/engine.h.
//
// One camera per view, read from the game push buffer of that view. The game
// projects through the GTE: view = R * world / 4096 + t with R and t from
// pb->matrix_ViewProj, then
//   screen = offset + H * view.xy / view.z
// with H = pb->distanceToScreen_PREV and the offset set by
// PushBuffer_SetPsyqGeom (game/PushBuffer.c): SetGeomOffset(rect.w / 2,
// rect.h / 2), SetGeomScreen(distanceToScreen_PREV). RenderBucket_QueueExecute,
// RenderLists and VehGroundSkids set it the same way (rect.w >> 1, rect.h >> 1).
// The screen position is relative to the view rectangle, not to the canvas; the
// draw environment of the view adds rect.x / rect.y.
//
// Engine_CameraScreenFromWorld builds the matrix that
// NativeRenderLayer_ItemMatrix (platform/native_render_layer.c) builds for a
// body, with the body transform left out (mvpShift 0): x' = H * vx + offsetX * vz,
// y' = H * vy + offsetY * vz, z' = zNear, w = vz. After the divide by w that is
// the PSX screen point and the reverse-Z depth zNear / vz with the far plane at
// infinity.

int Engine_CameraFromPushBuffer(const struct PushBuffer *pb, int view, EngineCamera *out)
{
	s32 t[3];
	int i;

	if (out == NULL)
	{
		return 0;
	}
	out->valid = 0;
	if (pb == NULL)
	{
		return 0;
	}
	if ((pb->distanceToScreen_PREV <= 0) || (pb->rect.w <= 0) || (pb->rect.h <= 0))
	{
		return 0;
	}

	for (i = 0; i < 3; i++)
	{
		t[i] = pb->matrix_ViewProj.t[i];
	}
	Engine_Mat4FromGte(pb->matrix_ViewProj.m, t, &out->viewFromWorld);

	out->valid = 1;
	out->view = view;
	out->H = (float)pb->distanceToScreen_PREV;
	out->offsetX = (float)(pb->rect.w >> 1);
	out->offsetY = (float)(pb->rect.h >> 1);
	out->rectX = pb->rect.x;
	out->rectY = pb->rect.y;
	out->rectW = pb->rect.w;
	out->rectH = pb->rect.h;
	out->zNear = out->H / 8.0f;
	return 1;
}

int Engine_ProjectPoint(const EngineCamera *cam, EngineVec3 world, EngineVec3 *out)
{
	EngineVec3 v;

	if ((cam == NULL) || (out == NULL) || !cam->valid)
	{
		return 0;
	}

	v = Engine_Mat4MulPoint(&cam->viewFromWorld, world);
	if ((v.z <= 0.0f) || (v.z < cam->zNear))
	{
		return 0;
	}

	out->x = cam->offsetX + cam->H * v.x / v.z;
	out->y = cam->offsetY + cam->H * v.y / v.z;
	out->z = v.z;
	return 1;
}

void Engine_CameraScreenFromWorld(const EngineCamera *cam, EngineMat4 *out)
{
	const float *v;
	int col;

	if ((cam == NULL) || !cam->valid)
	{
		Engine_Mat4Identity(out);
		return;
	}

	v = cam->viewFromWorld.m;
	for (col = 0; col < 4; col++)
	{
		// Row 2 of the view matrix is the view depth; rows 0 and 1 scale by H
		// and add the screen centre times that depth.
		const float depth = v[col * 4 + 2];

		out->m[col * 4 + 0] = cam->H * v[col * 4 + 0] + cam->offsetX * depth;
		out->m[col * 4 + 1] = cam->H * v[col * 4 + 1] + cam->offsetY * depth;
		out->m[col * 4 + 2] = (col == 3) ? cam->zNear : 0.0f;
		out->m[col * 4 + 3] = depth;
	}
}

// --------------------------------------------------------------------------
// Selftest

global_variable struct PushBuffer s_engineCamTestPb;

internal void EngineCam_Check(int ok, const char *what, int *checks, int *failures)
{
	(*checks)++;
	if (!ok)
	{
		(*failures)++;
		printf("engine selftest: %s\n", what);
	}
}

internal u32 EngineCam_Rand(u32 *seed)
{
	*seed = *seed * 1664525u + 1013904223u;
	return *seed >> 8;
}

// A float in [lo, hi].
internal float EngineCam_RandRange(u32 *seed, float lo, float hi)
{
	return lo + (hi - lo) * ((float)(EngineCam_Rand(seed) & 0xffff) / 65535.0f);
}

internal float EngineCam_Abs(float v)
{
	return (v < 0.0f) ? -v : v;
}

int Engine_SelfTestCamera(int *checks)
{
	// The view setups the game uses: the rectangles and distances come from
	// CTR_Canvas_Viewport (PushBuffer_Init) for the 512 x 216 canvas.
	static const struct
	{
		int id;
		int total;
		const char *name;
	} setups[] = {
		{0, 1, "1P"}, {0, 2, "2P top"}, {1, 2, "2P bottom"}, {0, 4, "4P top left"}, {3, 4, "4P bottom right"},
	};
	int failures = 0;
	int n = 0;
	int setup;
	u32 seed = 0x1badc0deu;
	float maxErr = 0.0f;
	int overAllowance = 0;
	int total = 0;
	int skipped = 0;

	for (setup = 0; setup < (int)(sizeof(setups) / sizeof(setups[0])); setup++)
	{
		struct PushBuffer *pb = &s_engineCamTestPb;
		EngineCamera cam;
		EngineMat4 screen;
		RECT16 rect;
		int distance = 0;
		SVec3 rot;
		MATRIX gm;
		float rf[3][3];
		int used = 0;
		int i;
		int r;
		int c;

		memset(pb, 0, sizeof(*pb));
		if (!CTR_Canvas_Viewport(512, 216, setups[setup].id, setups[setup].total, &rect, &distance))
		{
			EngineCam_Check(0, "camera test: no viewport for a setup", &n, &failures);
			continue;
		}
		pb->rect = rect;
		pb->distanceToScreen_PREV = distance;

		rot.x = (s16)(EngineCam_Rand(&seed) & 0xfff);
		rot.y = (s16)(EngineCam_Rand(&seed) & 0xfff);
		rot.z = (s16)(EngineCam_Rand(&seed) & 0xfff);
		ConvertRotToMatrix(&gm, &rot);
		pb->matrix_ViewProj = gm;
		pb->matrix_ViewProj.t[0] = (s32)EngineCam_RandRange(&seed, -3000.0f, 3000.0f);
		pb->matrix_ViewProj.t[1] = (s32)EngineCam_RandRange(&seed, -3000.0f, 3000.0f);
		pb->matrix_ViewProj.t[2] = (s32)EngineCam_RandRange(&seed, -3000.0f, 3000.0f);

		EngineCam_Check(Engine_CameraFromPushBuffer(pb, setups[setup].id, &cam) == 1, "CameraFromPushBuffer accepts a real view", &n, &failures);
		EngineCam_Check(cam.H == (float)distance && cam.offsetX == (float)(rect.w >> 1) && cam.offsetY == (float)(rect.h >> 1) && cam.rectW == rect.w &&
		                    cam.rectH == rect.h && cam.zNear == (float)distance / 8.0f,
		                "CameraFromPushBuffer fields", &n, &failures);

		// The inverse of the (nearly orthonormal) rotation takes view points back to world.
		for (r = 0; r < 3; r++)
		{
			for (c = 0; c < 3; c++)
			{
				rf[r][c] = (float)gm.m[r][c] / 4096.0f;
			}
		}

		// The PS1 reference: the registers the game sets for this view.
		SetRotMatrix(&pb->matrix_ViewProj);
		SetTransMatrix(&pb->matrix_ViewProj);
		SetGeomOffset(rect.w / 2, rect.h / 2);
		SetGeomScreen(distance);

		for (i = 0; i < 1000; i++)
		{
			const float vz = EngineCam_RandRange(&seed, 200.0f, 20000.0f);
			const float limX = vz * (float)(rect.w >> 1) / (float)distance * 0.9f;
			const float limY = vz * (float)(rect.h >> 1) / (float)distance * 0.9f;
			const float vx = EngineCam_RandRange(&seed, -limX, limX);
			const float vy = EngineCam_RandRange(&seed, -limY, limY);
			float d[3];
			float w[3];
			SVECTOR sv;
			EngineVec3 world;
			EngineVec3 got;
			EngineVec3 sc;
			s32 sxy = 0;
			s32 p = 0;
			s32 flag = 0;
			float refX;
			float refY;
			float ex;
			float ey;
			float cx;
			float cy;
			float cw;
			float allowX;
			float allowY;

			d[0] = vx - (float)pb->matrix_ViewProj.t[0];
			d[1] = vy - (float)pb->matrix_ViewProj.t[1];
			d[2] = vz - (float)pb->matrix_ViewProj.t[2];
			for (c = 0; c < 3; c++)
			{
				w[c] = rf[0][c] * d[0] + rf[1][c] * d[1] + rf[2][c] * d[2];
			}
			if ((EngineCam_Abs(w[0]) > 32000.0f) || (EngineCam_Abs(w[1]) > 32000.0f) || (EngineCam_Abs(w[2]) > 32000.0f))
			{
				skipped++;
				continue;
			}

			sv.vx = (s16)(w[0] + (w[0] < 0.0f ? -0.5f : 0.5f));
			sv.vy = (s16)(w[1] + (w[1] < 0.0f ? -0.5f : 0.5f));
			sv.vz = (s16)(w[2] + (w[2] < 0.0f ? -0.5f : 0.5f));
			sv.pad = 0;
			world = (EngineVec3){(float)sv.vx, (float)sv.vy, (float)sv.vz};

			RotTransPers(&sv, &sxy, &p, &flag);
			if (flag != 0)
			{
				skipped++;
				continue;
			}
			refX = (float)(s16)(sxy & 0xffff);
			refY = (float)(s16)((sxy >> 16) & 0xffff);

			if (!Engine_ProjectPoint(&cam, world, &got))
			{
				skipped++;
				continue;
			}

			// The PS1 reference is an integer: the GTE floors the pixel, so
			// the engine point (a float inside that cell) is compared to the
			// cell centre. Besides that the GTE rounds three times where the
			// float engine does not, and none of it is an engine error:
			//   - it floors the view-space x, y, z to whole units before the
			//     divide, which moves a projected point by up to H / vz pixels;
			//   - its reciprocal (UNR table) is off by up to about 0.1 %, which
			//     scales with the distance of the point from the screen centre.
			// Every point must stay within 0.5 px plus those two shares.
			ex = EngineCam_Abs(got.x - (refX + 0.5f));
			ey = EngineCam_Abs(got.y - (refY + 0.5f));
			allowX = 0.5f + cam.H / got.z + 0.001f * EngineCam_Abs(refX - cam.offsetX);
			allowY = 0.5f + cam.H / got.z + 0.001f * EngineCam_Abs(refY - cam.offsetY);
			total++;
			used++;
			if ((ex > allowX) || (ey > allowY))
			{
				overAllowance++;
			}
			if (ex > maxErr)
			{
				maxErr = ex;
			}
			if (ey > maxErr)
			{
				maxErr = ey;
			}

			// The clip matrix agrees with the projection.
			Engine_CameraScreenFromWorld(&cam, &screen);
			sc = (EngineVec3){0.0f, 0.0f, 0.0f};
			cw = screen.m[3] * world.x + screen.m[7] * world.y + screen.m[11] * world.z + screen.m[15];
			cx = (screen.m[0] * world.x + screen.m[4] * world.y + screen.m[8] * world.z + screen.m[12]) / cw;
			cy = (screen.m[1] * world.x + screen.m[5] * world.y + screen.m[9] * world.z + screen.m[13]) / cw;
			sc.z = (screen.m[2] * world.x + screen.m[6] * world.y + screen.m[10] * world.z + screen.m[14]) / cw;
			if ((EngineCam_Abs(cx - got.x) > 1e-3f) || (EngineCam_Abs(cy - got.y) > 1e-3f) ||
			    (EngineCam_Abs(sc.z - cam.zNear / got.z) > 1e-6f * (cam.zNear / got.z)) || (EngineCam_Abs(cw - got.z) > 1e-3f * got.z))
			{
				EngineCam_Check(0, "CameraScreenFromWorld differs from ProjectPoint", &n, &failures);
				break;
			}
		}
		EngineCam_Check(used >= 500, setups[setup].name, &n, &failures);
	}

	EngineCam_Check(overAllowance == 0, "ProjectPoint beyond 0.5 px plus the GTE own rounding", &n, &failures);
	EngineCam_Check(total >= 2500, "camera test: too few points compared", &n, &failures);
	if (failures == 0)
	{
		printf("engine selftest info: camera max error %.2f px (%d points, %d skipped)\n", (double)maxErr, total, skipped);
	}

	// Rejections.
	{
		struct PushBuffer *pb = &s_engineCamTestPb;
		EngineCamera cam;
		EngineVec3 out;
		EngineVec3 pt;

		memset(pb, 0, sizeof(*pb));
		pb->rect.w = 512;
		pb->rect.h = 216;
		pb->distanceToScreen_PREV = 256;
		pb->matrix_ViewProj.m[0][0] = 0x1000;
		pb->matrix_ViewProj.m[1][1] = 0x1000;
		pb->matrix_ViewProj.m[2][2] = 0x1000;

		EngineCam_Check(Engine_CameraFromPushBuffer(NULL, 0, &cam) == 0, "CameraFromPushBuffer rejects NULL", &n, &failures);
		EngineCam_Check(Engine_CameraFromPushBuffer(pb, 0, &cam) == 1, "CameraFromPushBuffer identity view", &n, &failures);

		pt = (EngineVec3){0.0f, 0.0f, 1000.0f};
		EngineCam_Check(Engine_ProjectPoint(&cam, pt, &out) == 1 && out.x == 256.0f && out.y == 108.0f && out.z == 1000.0f,
		                "ProjectPoint straight ahead hits the screen centre", &n, &failures);
		pt = (EngineVec3){0.0f, 0.0f, -1000.0f};
		EngineCam_Check(Engine_ProjectPoint(&cam, pt, &out) == 0, "ProjectPoint rejects a point behind the camera", &n, &failures);
		pt = (EngineVec3){0.0f, 0.0f, 0.0f};
		EngineCam_Check(Engine_ProjectPoint(&cam, pt, &out) == 0, "ProjectPoint rejects depth 0", &n, &failures);
		pt = (EngineVec3){0.0f, 0.0f, cam.zNear - 1.0f};
		EngineCam_Check(Engine_ProjectPoint(&cam, pt, &out) == 0, "ProjectPoint rejects a point inside the near plane", &n, &failures);
		pt = (EngineVec3){0.0f, 0.0f, cam.zNear};
		EngineCam_Check(Engine_ProjectPoint(&cam, pt, &out) == 1, "ProjectPoint keeps a point on the near plane", &n, &failures);
		EngineCam_Check(Engine_ProjectPoint(NULL, pt, &out) == 0, "ProjectPoint rejects a NULL camera", &n, &failures);

		pb->distanceToScreen_PREV = 0;
		EngineCam_Check(Engine_CameraFromPushBuffer(pb, 0, &cam) == 0, "CameraFromPushBuffer rejects H 0", &n, &failures);
		pb->distanceToScreen_PREV = 256;
		pb->rect.w = 0;
		EngineCam_Check(Engine_CameraFromPushBuffer(pb, 0, &cam) == 0, "CameraFromPushBuffer rejects an empty rect", &n, &failures);
	}

	*checks += n;
	return failures;
}
