#include <platform/engine.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ENGINE FLY CAMERA (round of 2026-10-16). See include/platform/engine.h, FLY CAMERA.
//
// WHAT IT DOES: takes view 0 off the game's camera and flies it free, to look
// at the engine's scene from anywhere. Two modes, both only with --engine:
//   --engine-flycam               manual flight (keyboard and mouse), never read
//                                 under --deterministic (the camera then stays at
//                                 its start station).
//   --engine-flycam-auto <frames> a deterministic flight: a closed loop over
//                                 stations, one loop in <frames> game frames,
//                                 repeated. It wins over the manual mode.
//   --engine-flycam-path <file>   the stations of the loop: lines "x y z lx ly lz"
//                                 (camera and the point it looks at, world units,
//                                 '#' comments). Without it: a ring of
//                                 ENGINE_FLYCAM_RING stations above the box of the
//                                 static model, else above the level's vertex box.
//
// WHAT IT WRITES OF THE GAME: only pb->pos and pb->rot of view 0
// (gGT->pushBuffer[0]) and the freeze flag CAMERA_FLAG_FROZEN
// (include/namespace_Camera.h) of gGT->cameraDC[0]. The game camera reads its
// own pb->pos / pb->rot incrementally (game/CAM.c ~1565 and ~1695), so it is
// frozen instead of overwritten every frame - the way of the preview path camera
// in game/native_flyin.c. Nothing else of the game is touched.
//
// DETERMINISM OF THE AUTO FLIGHT: the pose is a pure function of the frame
// counter (Engine_FlycamAt), which counts the ticks since the level was set up -
// after the level's intro (THE INTRO in Engine_FlycamTick); no clock, no input,
// no random numbers.
//
// SMOOTH (--engine-flycam-smooth, with --engine-flycam-auto <frames>): the demo
// video flight. No holds: a centripetal Catmull-Rom spline (alpha 0.5) through the
// station positions and one through the look points (same segment and local
// parameter, so the view reaches each look point at its station), an open path
// from the first to the last station, travelled at a constant speed along the
// position curve (arc-length table) in <frames> frames; afterwards the camera
// stays at the last station. No station log lines and no snapshots: the video
// frames come from --shot-series. Engine_FlycamSmoothAt is pure and deterministic.
//
// THE STATION / SHOT RULE: each station is held for ENGINE_FLYCAM_HOLD frames
// (less on a very short loop), then a smoothstep move to the next. Halfway
// through the hold, in the first loop only, one log line names the pose and,
// with --shot-name, a snapshot <shot-name>-sNN.bmp is requested (NN the station,
// two digits).

#define ENGFLY_LIMIT 32000.0f
#define ENGFLY_PATH_FILE_MAX (1024 * 1024)
#define ENGFLY_MOUSE_PER_PIXEL 0.003f
#define ENGFLY_PITCH_MAX 1.48f
#define ENGFLY_PI 3.14159265358979323846

extern int g_cfg_deterministic;                    // --deterministic, native_platform.c
extern char g_cfg_shotName[PLATFORM_ARG_PATH_MAX]; // --shot-name, native_platform.c
void Platform_ShotRequest(const char *path);       // native_platform.c

int g_cfg_engineFlycam = 0;
int g_cfg_engineFlycamAuto = 0;
int g_cfg_engineFlycamSmooth = 0;
char g_cfg_engineFlycamPath[ENGINE_FLYCAM_PATH_MAX] = "";

global_variable EngineFlycamStation s_flyStations[ENGINE_FLYCAM_STATIONS_MAX];
global_variable int s_flyCount = 0;
global_variable const void *s_flyLevel = NULL;
global_variable int s_flyLevelID = -1;
global_variable int s_flyReady = 0; // 1 = the stations of the current level are set up
global_variable u32 s_flyFrame = 0;
global_variable float s_flyBoxMin[3];
global_variable float s_flyBoxMax[3];
global_variable float s_flyPos[3];
global_variable float s_flyYaw = 0.0f;
global_variable float s_flyPitch = 0.0f;
global_variable int s_flyPrevP = 0;
global_variable u32 s_flyIntroFrames = 0; // frames of the level's intro waited for (THE INTRO)

// --------------------------------------------------------------------------
// Smooth flight: centripetal Catmull-Rom (Barry-Goldman) + arc-length table

#define ENGFLY_SMOOTH_SAMPLES 64
#define ENGFLY_SMOOTH_EPS 1.0e-6

global_variable double s_smTab[ENGINE_FLYCAM_STATIONS_MAX * ENGFLY_SMOOTH_SAMPLES + 1];
global_variable EngineFlycamStation s_smStations[ENGINE_FLYCAM_STATIONS_MAX];
global_variable int s_smCount = 0; // 0 = no valid table
global_variable float s_smoothLength = 0.0f;

// Point j (-1..count) of the position (which 0) or look (which 1) polyline; the
// phantom points mirror the end stations.
internal double EngFly_SmCoord(const EngineFlycamStation *st, int which, int i, int k)
{
	return (which == 0) ? (double)st[i].pos[k] : (double)st[i].look[k];
}

internal void EngFly_SmPoint(const EngineFlycamStation *st, int count, int which, int j, double out[3])
{
	int k;

	for (k = 0; k < 3; k++)
	{
		if (j < 0)
		{
			out[k] = 2.0 * EngFly_SmCoord(st, which, 0, k) - EngFly_SmCoord(st, which, 1, k);
		}
		else if (j >= count)
		{
			out[k] = 2.0 * EngFly_SmCoord(st, which, count - 1, k) - EngFly_SmCoord(st, which, count - 2, k);
		}
		else
		{
			out[k] = EngFly_SmCoord(st, which, j, k);
		}
	}
}

// The curve of segment seg (between stations seg and seg+1) at local parameter u.
internal void EngFly_SmEval(const EngineFlycamStation *st, int count, int which, int seg, double u, double out[3])
{
	double p[4][3];
	double t[4];
	double tt;
	int i;
	int k;

	for (i = 0; i < 4; i++)
	{
		EngFly_SmPoint(st, count, which, seg - 1 + i, p[i]);
	}
	t[0] = 0.0;
	for (i = 1; i < 4; i++)
	{
		const double dx = p[i][0] - p[i - 1][0];
		const double dy = p[i][1] - p[i - 1][1];
		const double dz = p[i][2] - p[i - 1][2];
		double d = sqrt(sqrt(dx * dx + dy * dy + dz * dz)); // |dP|^0.5

		if (d < ENGFLY_SMOOTH_EPS)
		{
			d = ENGFLY_SMOOTH_EPS;
		}
		t[i] = t[i - 1] + d;
	}
	tt = t[1] + u * (t[2] - t[1]);
	for (k = 0; k < 3; k++)
	{
		const double a1 = (t[1] - tt) / (t[1] - t[0]) * p[0][k] + (tt - t[0]) / (t[1] - t[0]) * p[1][k];
		const double a2 = (t[2] - tt) / (t[2] - t[1]) * p[1][k] + (tt - t[1]) / (t[2] - t[1]) * p[2][k];
		const double a3 = (t[3] - tt) / (t[3] - t[2]) * p[2][k] + (tt - t[2]) / (t[3] - t[2]) * p[3][k];
		const double b1 = (t[2] - tt) / (t[2] - t[0]) * a1 + (tt - t[0]) / (t[2] - t[0]) * a2;
		const double b2 = (t[3] - tt) / (t[3] - t[1]) * a2 + (tt - t[1]) / (t[3] - t[1]) * a3;

		out[k] = (t[2] - tt) / (t[2] - t[1]) * b1 + (tt - t[1]) / (t[2] - t[1]) * b2;
	}
}

// Rebuilds the arc-length table of the position curve (count >= 2).
internal void EngFly_SmBuild(const EngineFlycamStation *st, int count)
{
	const int n = (count - 1) * ENGFLY_SMOOTH_SAMPLES + 1;
	double prev[3];
	double cur[3];
	int idx;

	EngFly_SmEval(st, count, 0, 0, 0.0, prev);
	s_smTab[0] = 0.0;
	for (idx = 1; idx < n; idx++)
	{
		const int seg = (idx - 1) / ENGFLY_SMOOTH_SAMPLES;
		const int j = idx - seg * ENGFLY_SMOOTH_SAMPLES;
		double d[3];
		int k;

		EngFly_SmEval(st, count, 0, seg, (double)j / (double)ENGFLY_SMOOTH_SAMPLES, cur);
		for (k = 0; k < 3; k++)
		{
			d[k] = cur[k] - prev[k];
			prev[k] = cur[k];
		}
		s_smTab[idx] = s_smTab[idx - 1] + sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	}
	memcpy(s_smStations, st, (size_t)count * sizeof(*st));
	s_smCount = count;
	s_smoothLength = (float)s_smTab[n - 1];
}

int Engine_FlycamSmoothAt(const EngineFlycamStation *st, int count, int frames, u32 frame, float pos[3], float look[3])
{
	const EngineFlycamStation *e = NULL;
	double s;
	double frac;
	double u;
	double p[3];
	double l[3];
	int n;
	int lo;
	int hi;
	int seg;
	int k;

	if (count < 1)
	{
		memset(pos, 0, 3 * sizeof(float));
		memset(look, 0, 3 * sizeof(float));
		return 0;
	}
	if (count > ENGINE_FLYCAM_STATIONS_MAX)
	{
		count = ENGINE_FLYCAM_STATIONS_MAX;
	}
	if (count == 1)
	{
		e = &st[0];
	}
	else
	{
		if ((s_smCount != count) || (memcmp(s_smStations, st, (size_t)count * sizeof(*st)) != 0))
		{
			EngFly_SmBuild(st, count);
		}
		if ((frames <= 1) || (frame >= (u32)(frames - 1)))
		{
			e = &st[count - 1];
		}
		else if (frame == 0)
		{
			e = &st[0];
		}
	}
	if (e != NULL)
	{
		memcpy(pos, e->pos, 3 * sizeof(float));
		memcpy(look, e->look, 3 * sizeof(float));
		return 1;
	}

	n = (count - 1) * ENGFLY_SMOOTH_SAMPLES + 1;
	s = s_smTab[n - 1] * (double)frame / (double)(frames - 1);
	lo = 0;
	hi = n - 1;
	while (hi - lo > 1)
	{
		const int mid = (lo + hi) / 2;

		if (s_smTab[mid] <= s)
		{
			lo = mid;
		}
		else
		{
			hi = mid;
		}
	}
	frac = (s_smTab[hi] > s_smTab[lo]) ? (s - s_smTab[lo]) / (s_smTab[hi] - s_smTab[lo]) : 0.0;
	seg = lo / ENGFLY_SMOOTH_SAMPLES;
	u = ((double)(lo - seg * ENGFLY_SMOOTH_SAMPLES) + frac) / (double)ENGFLY_SMOOTH_SAMPLES;
	if (u > 1.0)
	{
		u = 1.0;
	}
	EngFly_SmEval(st, count, 0, seg, u, p);
	EngFly_SmEval(st, count, 1, seg, u, l);
	for (k = 0; k < 3; k++)
	{
		pos[k] = (float)p[k];
		look[k] = (float)l[k];
	}
	return 1;
}

internal float EngFly_Clamp(float v)
{
	return (v < -ENGFLY_LIMIT) ? -ENGFLY_LIMIT : ((v > ENGFLY_LIMIT) ? ENGFLY_LIMIT : v);
}

internal s16 EngFly_RoundS16(float v)
{
	return (s16)floorf(EngFly_Clamp(v) + 0.5f);
}

// Frames of one station (hold and move) and of its hold; shared by FlycamAt and the tick.
internal int EngFly_Seg(int loopFrames, int count)
{
	const int seg = (count > 0) ? (loopFrames / count) : 1;

	return (seg < 1) ? 1 : seg;
}

internal int EngFly_Hold(int seg)
{
	int hold = seg / 2;

	if (hold < 1)
	{
		hold = 1;
	}
	return (hold > ENGINE_FLYCAM_HOLD) ? ENGINE_FLYCAM_HOLD : hold;
}

void Engine_FlycamRot(const float pos[3], const float look[3], s16 rot[3])
{
	const double dx = (double)pos[0] - (double)look[0];
	const double dy = (double)pos[1] - (double)look[1];
	const double dz = (double)pos[2] - (double)look[2];
	const double scale = 4096.0 / (2.0 * ENGFLY_PI);

	if ((dx == 0.0) && (dy == 0.0) && (dz == 0.0))
	{
		rot[0] = 0x800;
		rot[1] = 0;
		rot[2] = 0;
		return;
	}
	{
		const double yaw = atan2(dx, dz) * scale;
		const double pitch = atan2(dy, sqrt(dx * dx + dz * dz)) * scale;

		rot[0] = (s16)floor(2048.0 - pitch + 0.5);
		rot[1] = (s16)floor(yaw + 0.5);
		rot[2] = 0;
	}
}

internal int EngFly_Blank(char c)
{
	return (c == ' ') || (c == '\t');
}

int Engine_FlycamParsePath(const char *text, EngineFlycamStation *out, int max, int *badLine)
{
	int count = 0;
	int lineNo = 0;
	const char *p = text;

	*badLine = 0;
	while (*p != '\0')
	{
		const char *end = strchr(p, '\n');
		const size_t len = (end != NULL) ? (size_t)(end - p) : strlen(p);
		char line[256];
		char *s;
		char *e;
		double v[6];
		int k;

		lineNo++;
		if (len >= sizeof(line))
		{
			*badLine = lineNo;
			return -1;
		}
		memcpy(line, p, len);
		line[len] = '\0';
		p += len;
		if (*p == '\n')
		{
			p++;
		}

		s = line;
		while (EngFly_Blank(*s))
		{
			s++;
		}
		e = s + strlen(s);
		while ((e > s) && (EngFly_Blank(e[-1]) || (e[-1] == '\r')))
		{
			e--;
		}
		*e = '\0';
		if ((*s == '\0') || (*s == '#'))
		{
			continue;
		}
		for (k = 0; k < 6; k++)
		{
			char *next = NULL;

			v[k] = strtod(s, &next);
			if ((next == s) || ((*next != '\0') && !EngFly_Blank(*next)))
			{
				*badLine = lineNo;
				return -1;
			}
			s = next;
			while (EngFly_Blank(*s))
			{
				s++;
			}
		}
		if ((*s != '\0') || (count >= max))
		{
			*badLine = lineNo;
			return -1;
		}
		for (k = 0; k < 3; k++)
		{
			out[count].pos[k] = (float)v[k];
			out[count].look[k] = (float)v[3 + k];
		}
		count++;
	}
	return count;
}

int Engine_FlycamRing(const float boxMin[3], const float boxMax[3], EngineFlycamStation *out)
{
	const double cx = 0.5 * ((double)boxMin[0] + (double)boxMax[0]);
	const double cz = 0.5 * ((double)boxMin[2] + (double)boxMax[2]);
	const double ex = (double)boxMax[0] - (double)boxMin[0];
	const double ey = (double)boxMax[1] - (double)boxMin[1];
	const double ez = (double)boxMax[2] - (double)boxMin[2];
	// Low over the box (round of 2026-10-16, after the first run: the ring of
	// 0.45 / 0.25 looked at the track from far above): a ring of 0.35 of the box
	// inside it, 0.05 of the longer side above its top, each station looking past
	// the middle at a point 0.15 of the box beyond it, a quarter up the box -
	// a slant down over the middle of the track.
	const double rx = 0.35 * ex;
	const double rz = 0.35 * ez;
	const double h = (double)boxMax[1] + 0.05 * ((ex > ez) ? ex : ez);
	int k;

	for (k = 0; k < ENGINE_FLYCAM_RING; k++)
	{
		const double a = 2.0 * ENGFLY_PI * (double)k / (double)ENGINE_FLYCAM_RING;

		out[k].pos[0] = EngFly_Clamp((float)(cx + rx * cos(a)));
		out[k].pos[1] = EngFly_Clamp((float)h);
		out[k].pos[2] = EngFly_Clamp((float)(cz + rz * sin(a)));
		out[k].look[0] = EngFly_Clamp((float)(cx - 0.15 * ex * cos(a)));
		out[k].look[1] = EngFly_Clamp((float)((double)boxMin[1] + 0.25 * ey));
		out[k].look[2] = EngFly_Clamp((float)(cz - 0.15 * ez * sin(a)));
	}
	return ENGINE_FLYCAM_RING;
}

void Engine_FlycamAt(const EngineFlycamStation *st, int count, int loopFrames, u32 frame, float pos[3], float look[3], int *station, int *holdFrame)
{
	int seg;
	int hold;
	u32 f;
	int i;
	int t;
	int k;

	if (count < 1)
	{
		for (k = 0; k < 3; k++)
		{
			pos[k] = 0.0f;
			look[k] = 0.0f;
		}
		*station = -1;
		*holdFrame = 0;
		return;
	}
	seg = EngFly_Seg(loopFrames, count);
	hold = EngFly_Hold(seg);
	f = frame % (u32)(seg * count);
	i = (int)(f / (u32)seg);
	t = (int)(f % (u32)seg);
	if ((t < hold) || (count == 1))
	{
		for (k = 0; k < 3; k++)
		{
			pos[k] = st[i].pos[k];
			look[k] = st[i].look[k];
		}
		*station = i;
		*holdFrame = t;
	}
	else
	{
		const float u = (float)(t - hold) / (float)(seg - hold);
		const float s = u * u * (3.0f - 2.0f * u);
		const int j = (i + 1) % count;

		for (k = 0; k < 3; k++)
		{
			pos[k] = st[i].pos[k] + (st[j].pos[k] - st[i].pos[k]) * s;
			look[k] = st[i].look[k] + (st[j].look[k] - st[i].look[k]) * s;
		}
		*station = -1;
		*holdFrame = 0;
	}
}

// The text of a path file (max 1 MB) or NULL; the caller frees it.
internal char *EngFly_ReadFile(const char *path, const char **why)
{
	FILE *fp = fopen(path, "rb");
	char *buf;
	size_t n;

	if (fp == NULL)
	{
		*why = "cannot open it";
		return NULL;
	}
	buf = (char *)malloc(ENGFLY_PATH_FILE_MAX + 1);
	if (buf == NULL)
	{
		fclose(fp);
		*why = "out of memory";
		return NULL;
	}
	n = fread(buf, 1, ENGFLY_PATH_FILE_MAX + 1, fp);
	fclose(fp);
	if (n > ENGFLY_PATH_FILE_MAX)
	{
		free(buf);
		*why = "larger than 1 MB";
		return NULL;
	}
	buf[n] = '\0';
	return buf;
}

internal float EngFly_Comp(const SVec3 *v, int k)
{
	return (float)((k == 0) ? v->x : ((k == 1) ? v->y : v->z));
}

// The vertex box of the level; 1 when there is one.
internal int EngFly_LevelBox(const struct GameTracker *gGT, float boxMin[3], float boxMax[3])
{
	const struct mesh_info *mi = gGT->level1->ptr_mesh_info;
	int i;
	int k;

	if ((mi == NULL) || (mi->ptrVertexArray == NULL) || (mi->numVertex <= 0))
	{
		return 0;
	}
	for (k = 0; k < 3; k++)
	{
		boxMin[k] = EngFly_Comp(&mi->ptrVertexArray[0].pos, k);
		boxMax[k] = boxMin[k];
	}
	for (i = 1; i < mi->numVertex; i++)
	{
		for (k = 0; k < 3; k++)
		{
			const float v = EngFly_Comp(&mi->ptrVertexArray[i].pos, k);

			if (v < boxMin[k])
			{
				boxMin[k] = v;
			}
			if (v > boxMax[k])
			{
				boxMax[k] = v;
			}
		}
	}
	return 1;
}

// Sets up the stations of a new level; 1 when the camera can fly.
internal int EngFly_Setup(const struct GameTracker *gGT)
{
	const char *boxSource = NULL;
	const char *source = NULL;
	char mode[48];
	int k;

	s_flyCount = 0;
	s_flyFrame = 0;
	if (Engine_StaticBox(s_flyBoxMin, s_flyBoxMax))
	{
		boxSource = "static model box";
	}
	else if (EngFly_LevelBox(gGT, s_flyBoxMin, s_flyBoxMax))
	{
		boxSource = "level box";
	}

	if (g_cfg_engineFlycamPath[0] != '\0')
	{
		const char *why = NULL;
		char *text = EngFly_ReadFile(g_cfg_engineFlycamPath, &why);

		if (text == NULL)
		{
			Platform_Log("[CTR Engine] flycam: path file %s: %s, using the ring\n", g_cfg_engineFlycamPath, why);
		}
		else
		{
			int bad = 0;
			const int n = Engine_FlycamParsePath(text, s_flyStations, ENGINE_FLYCAM_STATIONS_MAX, &bad);

			free(text);
			if (n > 0)
			{
				s_flyCount = n;
				source = "path file";
			}
			else if (n < 0)
			{
				Platform_Log("[CTR Engine] flycam: path file %s: bad line %d, using the ring\n", g_cfg_engineFlycamPath, bad);
			}
			else
			{
				Platform_Log("[CTR Engine] flycam: path file %s has no stations, using the ring\n", g_cfg_engineFlycamPath);
			}
		}
	}
	if (s_flyCount == 0)
	{
		if (boxSource == NULL)
		{
			Platform_Log("[CTR Engine] flycam: no box to build a ring from, nothing to do for this level\n");
			return 0;
		}
		s_flyCount = Engine_FlycamRing(s_flyBoxMin, s_flyBoxMax, s_flyStations);
		source = boxSource;
	}
	if (boxSource == NULL)
	{
		// A path without any box: only the manual step size needs one.
		for (k = 0; k < 3; k++)
		{
			s_flyBoxMin[k] = -4000.0f;
			s_flyBoxMax[k] = 4000.0f;
		}
	}

	if ((g_cfg_engineFlycamAuto > 0) && g_cfg_engineFlycamSmooth)
	{
		snprintf(mode, sizeof(mode), "auto smooth %d frames", g_cfg_engineFlycamAuto);
	}
	else if (g_cfg_engineFlycamAuto > 0)
	{
		snprintf(mode, sizeof(mode), "auto %d frames per loop", g_cfg_engineFlycamAuto);
	}
	else
	{
		snprintf(mode, sizeof(mode), "manual");
	}
	Platform_Log("[CTR Engine] flycam: %s, %d stations from %s, box x %.0f..%.0f y %.0f..%.0f z %.0f..%.0f\n", mode, s_flyCount, source, s_flyBoxMin[0],
	             s_flyBoxMax[0], s_flyBoxMin[1], s_flyBoxMax[1], s_flyBoxMin[2], s_flyBoxMax[2]);

	// The manual start: station 0, looking from its pos to its look.
	{
		const double dx = (double)s_flyStations[0].pos[0] - (double)s_flyStations[0].look[0];
		const double dy = (double)s_flyStations[0].pos[1] - (double)s_flyStations[0].look[1];
		const double dz = (double)s_flyStations[0].pos[2] - (double)s_flyStations[0].look[2];

		for (k = 0; k < 3; k++)
		{
			s_flyPos[k] = s_flyStations[0].pos[k];
		}
		s_flyYaw = ((dx != 0.0) || (dz != 0.0)) ? (float)atan2(dx, dz) : 0.0f;
		s_flyPitch = (float)atan2(dy, sqrt(dx * dx + dz * dz));
	}
	return 1;
}

internal void EngFly_WritePose(struct GameTracker *gGT, const float pos[3], const float look[3])
{
	struct PushBuffer *pb = &gGT->pushBuffer[0];
	s16 rot[3];

	Engine_FlycamRot(pos, look, rot);
	pb->pos.x = EngFly_RoundS16(pos[0]);
	pb->pos.y = EngFly_RoundS16(pos[1]);
	pb->pos.z = EngFly_RoundS16(pos[2]);
	pb->rot.x = rot[0];
	pb->rot.y = rot[1];
	pb->rot.z = rot[2];
}

internal void EngFly_TickSmooth(struct GameTracker *gGT)
{
	float pos[3];
	float look[3];
	const u32 last = (u32)((g_cfg_engineFlycamAuto > 1) ? (g_cfg_engineFlycamAuto - 1) : 0);

	Engine_FlycamSmoothAt(s_flyStations, s_flyCount, g_cfg_engineFlycamAuto, s_flyFrame, pos, look);
	EngFly_WritePose(gGT, pos, look);
	if (s_flyFrame == last)
	{
		Platform_Log("[CTR Engine] flycam smooth: end of the flight at frame %u, length %.0f world units\n", (unsigned)s_flyFrame, (double)s_smoothLength);
	}
	if (s_flyFrame < 0xFFFFFFFFu)
	{
		s_flyFrame++;
	}
}

internal void EngFly_TickAuto(struct GameTracker *gGT)
{
	const int seg = EngFly_Seg(g_cfg_engineFlycamAuto, s_flyCount);
	const int hold = EngFly_Hold(seg);
	float pos[3];
	float look[3];
	int station;
	int holdFrame;

	Engine_FlycamAt(s_flyStations, s_flyCount, g_cfg_engineFlycamAuto, s_flyFrame, pos, look, &station, &holdFrame);
	EngFly_WritePose(gGT, pos, look);

	if ((station >= 0) && (holdFrame == hold / 2) && (s_flyFrame < (u32)(seg * s_flyCount)))
	{
		const struct PushBuffer *pb = &gGT->pushBuffer[0];

		Platform_Log("[CTR Engine] flycam station %02d frame %u pos %d %d %d look %.0f %.0f %.0f rot %d %d %d\n", station, (unsigned)s_flyFrame,
		             (int)pb->pos.x, (int)pb->pos.y, (int)pb->pos.z, look[0], look[1], look[2], (int)pb->rot.x, (int)pb->rot.y, (int)pb->rot.z);
		if (g_cfg_shotName[0] != '\0')
		{
			char base[PLATFORM_ARG_PATH_MAX];
			char path[PLATFORM_ARG_PATH_MAX + 16];
			size_t length;

			snprintf(base, sizeof(base), "%s", g_cfg_shotName);
			length = strlen(base);
			if ((length > 4) && ((strcmp(&base[length - 4], ".bmp") == 0) || (strcmp(&base[length - 4], ".BMP") == 0)))
			{
				base[length - 4] = '\0';
			}
			snprintf(path, sizeof(path), "%s-s%02d.bmp", base, station);
			Platform_ShotRequest(path);
		}
	}
	s_flyFrame++;
}

internal void EngFly_TickManual(struct GameTracker *gGT)
{
	float look[3];
	float pos[3];
	int k;

	if (!g_cfg_deterministic)
	{
		const bool *keys = SDL_GetKeyboardState(NULL);
		float mx = 0.0f;
		float my = 0.0f;
		float diag;
		float step;
		float fwd[3];
		float right[3];
		float rl;

		(void)SDL_GetRelativeMouseState(&mx, &my);
		if ((SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_RMASK) != 0)
		{
			// Mouse right turns right: yaw falls (right = (-f.z, 0, f.x)); mouse down looks down: pitch rises.
			s_flyYaw -= mx * ENGFLY_MOUSE_PER_PIXEL;
			s_flyPitch += my * ENGFLY_MOUSE_PER_PIXEL;
			if (s_flyPitch > ENGFLY_PITCH_MAX)
			{
				s_flyPitch = ENGFLY_PITCH_MAX;
			}
			if (s_flyPitch < -ENGFLY_PITCH_MAX)
			{
				s_flyPitch = -ENGFLY_PITCH_MAX;
			}
		}

		fwd[0] = -sinf(s_flyYaw) * cosf(s_flyPitch);
		fwd[1] = -sinf(s_flyPitch);
		fwd[2] = -cosf(s_flyYaw) * cosf(s_flyPitch);
		right[0] = -fwd[2];
		right[1] = 0.0f;
		right[2] = fwd[0];
		rl = sqrtf(right[0] * right[0] + right[2] * right[2]);
		if (rl > 0.0f)
		{
			right[0] /= rl;
			right[2] /= rl;
		}
		diag = sqrtf((s_flyBoxMax[0] - s_flyBoxMin[0]) * (s_flyBoxMax[0] - s_flyBoxMin[0]) + (s_flyBoxMax[1] - s_flyBoxMin[1]) * (s_flyBoxMax[1] - s_flyBoxMin[1]) +
		             (s_flyBoxMax[2] - s_flyBoxMin[2]) * (s_flyBoxMax[2] - s_flyBoxMin[2]));
		step = (0.002f * diag > 8.0f) ? 0.002f * diag : 8.0f;
		if (keys != NULL)
		{
			const float along = (float)((keys[SDL_SCANCODE_W] ? 1 : 0) - (keys[SDL_SCANCODE_S] ? 1 : 0));
			const float side = (float)((keys[SDL_SCANCODE_D] ? 1 : 0) - (keys[SDL_SCANCODE_A] ? 1 : 0));
			const float up = (float)((keys[SDL_SCANCODE_R] ? 1 : 0) - (keys[SDL_SCANCODE_F] ? 1 : 0));
			const int p = keys[SDL_SCANCODE_P] ? 1 : 0;

			if (keys[SDL_SCANCODE_LSHIFT])
			{
				step *= 4.0f;
			}
			for (k = 0; k < 3; k++)
			{
				s_flyPos[k] += step * (along * fwd[k] + side * right[k]);
			}
			s_flyPos[1] += step * up;

			if (p && !s_flyPrevP)
			{
				Platform_Log("[CTR Engine] flycam point: %.0f %.0f %.0f %.0f %.0f %.0f\n", s_flyPos[0], s_flyPos[1], s_flyPos[2], s_flyPos[0] + 1000.0f * fwd[0],
				             s_flyPos[1] + 1000.0f * fwd[1], s_flyPos[2] + 1000.0f * fwd[2]);
			}
			s_flyPrevP = p;
		}
	}

	for (k = 0; k < 3; k++)
	{
		s_flyPos[k] = EngFly_Clamp(s_flyPos[k]);
		pos[k] = s_flyPos[k];
	}
	look[0] = pos[0] - 1000.0f * sinf(s_flyYaw) * cosf(s_flyPitch);
	look[1] = pos[1] - 1000.0f * sinf(s_flyPitch);
	look[2] = pos[2] - 1000.0f * cosf(s_flyYaw) * cosf(s_flyPitch);
	EngFly_WritePose(gGT, pos, look);
}

void Engine_FlycamTick(struct GameTracker *gGT)
{
	if (!g_cfg_engine || !(g_cfg_engineFlycam || (g_cfg_engineFlycamAuto > 0)) || (gGT == NULL) || (gGT->level1 == NULL) ||
	    ((gGT->gameMode1 & (MAIN_MENU | LOADING)) != 0) || (gGT->numPlyrCurrGame < 1))
	{
		return;
	}
	// THE INTRO: the game camera's fly-in clears START_OF_RACE when it ends
	// (game/CAM.c:1686-1690); frozen before that, the level would stay in its
	// intro (letterbox, title) for good. So the fly camera takes view 0 only
	// after it, and its frames count from there.
	if ((gGT->gameMode1 & START_OF_RACE) != 0)
	{
		s_flyIntroFrames++;
		return;
	}
	if (!s_flyReady || (s_flyLevel != (const void *)gGT->level1) || (s_flyLevelID != gGT->levelID))
	{
		s_flyLevel = gGT->level1;
		s_flyLevelID = gGT->levelID;
		s_flyReady = EngFly_Setup(gGT);
		Platform_Log("[CTR Engine] flycam: the level's intro took %u frame(s); the flight starts now\n", (unsigned)s_flyIntroFrames);
		s_flyIntroFrames = 0;
	}
	if (!s_flyReady)
	{
		return;
	}

	gGT->cameraDC[0].flags |= CAMERA_FLAG_FROZEN;
	if ((g_cfg_engineFlycamAuto > 0) && g_cfg_engineFlycamSmooth)
	{
		EngFly_TickSmooth(gGT);
	}
	else if (g_cfg_engineFlycamAuto > 0)
	{
		EngFly_TickAuto(gGT);
	}
	else
	{
		EngFly_TickManual(gGT);
	}
}

// --------------------------------------------------------------------------
// Selftest

internal void EngFly_Check(int ok, const char *what, int *n, int *failures)
{
	(*n)++;
	if (!ok)
	{
		(*failures)++;
		printf("engine selftest: flycam %s\n", what);
	}
}

global_variable struct PushBuffer s_flyTestPb;
global_variable u32 s_flyTestScratch[CTR_SCRATCHPAD_SIZE / 4];

internal void EngFly_Norm(float v[3])
{
	const float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);

	if (l > 0.0f)
	{
		v[0] /= l;
		v[1] /= l;
		v[2] /= l;
	}
}

internal void EngFly_SelfTestRot(int *n, int *failures)
{
	static const float poses[8][6] = {
	    {0.0f, 800.0f, 1000.0f, 0.0f, 0.0f, 0.0f},              // along -z, down
	    {1000.0f, 600.0f, 0.0f, 0.0f, 0.0f, 0.0f},              // along -x
	    {0.0f, 500.0f, -1000.0f, 0.0f, 0.0f, 0.0f},             // along +z
	    {-1200.0f, 900.0f, 0.0f, 0.0f, 100.0f, 0.0f},           // along +x
	    {800.0f, 1200.0f, 900.0f, -300.0f, 0.0f, -200.0f},      // quadrant +x +z
	    {-900.0f, 1500.0f, -700.0f, 400.0f, 200.0f, 300.0f},    // quadrant -x -z
	    {2000.0f, 300.0f, -1500.0f, -500.0f, 300.0f, 800.0f},   // level
	    {-1500.0f, 2000.0f, 1800.0f, 300.0f, 0.0f, -400.0f},    // steep down
	};
	u8 *const savedScratch = gCTRNativeScratchpadBase;
	int i;

	// PushBuffer_SetMatrixVP works in the scratchpad, which only exists in a running game.
	if (savedScratch == NULL)
	{
		gCTRNativeScratchpadBase = (u8 *)s_flyTestScratch;
	}
	for (i = 0; i < 8; i++)
	{
		struct PushBuffer *pb = &s_flyTestPb;
		EngineCamera cam;
		EngineVec3 pt;
		EngineVec3 out;
		RECT16 rect;
		int distance = 0;
		s16 rot[3];
		float f[3];
		float right[3];
		const float up[3] = {0.0f, 1.0f, 0.0f};
		const float *pos = &poses[i][0];
		const float *look = &poses[i][3];
		int k;

		memset(pb, 0, sizeof(*pb));
		if (!CTR_Canvas_Viewport(512, 216, 0, 1, &rect, &distance))
		{
			EngFly_Check(0, "rot: no viewport", n, failures);
			break;
		}
		pb->rect = rect;
		pb->distanceToScreen_PREV = distance;
		Engine_FlycamRot(pos, look, rot);
		pb->pos.x = (s16)pos[0];
		pb->pos.y = (s16)pos[1];
		pb->pos.z = (s16)pos[2];
		pb->rot.x = rot[0];
		pb->rot.y = rot[1];
		pb->rot.z = rot[2];
		PushBuffer_SetMatrixVP(pb);
		if (!Engine_CameraFromPushBuffer(pb, 0, &cam))
		{
			EngFly_Check(0, "rot: no camera from the push buffer", n, failures);
			continue;
		}
		for (k = 0; k < 3; k++)
		{
			f[k] = look[k] - pos[k];
		}
		EngFly_Norm(f);
		right[0] = f[1] * up[2] - f[2] * up[1];
		right[1] = f[2] * up[0] - f[0] * up[2];
		right[2] = f[0] * up[1] - f[1] * up[0];
		EngFly_Norm(right);

		pt = (EngineVec3){look[0], look[1], look[2]};
		EngFly_Check(Engine_ProjectPoint(&cam, pt, &out) && fabsf(out.x - cam.offsetX) <= 2.0f && fabsf(out.y - cam.offsetY) <= 2.0f,
		             "rot: the look point is not at the screen centre", n, failures);
		pt = (EngineVec3){look[0], look[1] + 200.0f, look[2]};
		EngFly_Check(Engine_ProjectPoint(&cam, pt, &out) && out.y < cam.offsetY, "rot: world up is not above the centre", n, failures);
		pt = (EngineVec3){look[0] + 200.0f * right[0], look[1] + 200.0f * right[1], look[2] + 200.0f * right[2]};
		EngFly_Check(Engine_ProjectPoint(&cam, pt, &out) && out.x > cam.offsetX, "rot: right is not right of the centre", n, failures);
	}
	gCTRNativeScratchpadBase = savedScratch;
}

internal void EngFly_SelfTestParse(int *n, int *failures)
{
	static const char good[] = "# a path\r\n\r\n  1 2 3  4 5 6\r\n\t# indented comment\n-10.5\t20 30 0 0 0   \n100 200 300 400 500 600";
	EngineFlycamStation st[4];
	int bad = 0;
	int c;

	c = Engine_FlycamParsePath(good, st, 4, &bad);
	EngFly_Check(c == 3, "parse: three stations", n, failures);
	if (c == 3)
	{
		EngFly_Check(st[0].pos[0] == 1.0f && st[0].pos[2] == 3.0f && st[0].look[0] == 4.0f && st[0].look[2] == 6.0f, "parse: station 0 values", n, failures);
		EngFly_Check(st[1].pos[0] == -10.5f && st[1].pos[1] == 20.0f && st[1].look[1] == 0.0f, "parse: station 1 values", n, failures);
		EngFly_Check(st[2].pos[0] == 100.0f && st[2].look[2] == 600.0f, "parse: station 2 values (no final newline)", n, failures);
	}
	EngFly_Check(Engine_FlycamParsePath("", st, 4, &bad) == 0, "parse: empty text is 0 stations", n, failures);
	c = Engine_FlycamParsePath("1 2 3 4 5 6\n# x\n1 2 3 4 5\n", st, 4, &bad);
	EngFly_Check(c == -1 && bad == 3, "parse: five numbers is a bad line (3)", n, failures);
	c = Engine_FlycamParsePath("1 2 3 4 5 6 7\n", st, 4, &bad);
	EngFly_Check(c == -1 && bad == 1, "parse: seven numbers is a bad line (1)", n, failures);
	c = Engine_FlycamParsePath("\n1 2 3 4 5 6 x\n", st, 4, &bad);
	EngFly_Check(c == -1 && bad == 2, "parse: a trailing word is a bad line (2)", n, failures);
	c = Engine_FlycamParsePath("1 2 3 4 5 6\n1 2 3 4 5 6\n1 2 3 4 5 6\n", st, 2, &bad);
	EngFly_Check(c == -1 && bad == 3, "parse: more than max is a bad line (3)", n, failures);
}

internal void EngFly_SelfTestAt(int *n, int *failures)
{
	EngineFlycamStation st[4];
	float pos[3];
	float look[3];
	float pos2[3];
	float look2[3];
	int station;
	int hold;
	int station2;
	int hold2;
	int single = 1;
	int i;
	int k;

	for (i = 0; i < 4; i++)
	{
		for (k = 0; k < 3; k++)
		{
			st[i].pos[k] = (float)(i * 1000 + k * 10 + 100);
			st[i].look[k] = (float)(i * 500 - k * 7);
		}
	}
	Engine_FlycamAt(st, 4, 400, 0, pos, look, &station, &hold);
	EngFly_Check(station == 0 && hold == 0 && pos[0] == st[0].pos[0] && pos[1] == st[0].pos[1] && pos[2] == st[0].pos[2], "at: frame 0 is station 0", n, failures);
	Engine_FlycamAt(st, 4, 400, 29, pos, look, &station, &hold);
	EngFly_Check(station == 0 && hold == 29, "at: frame 29 is still station 0", n, failures);
	Engine_FlycamAt(st, 4, 400, 30, pos, look, &station, &hold);
	EngFly_Check(station == -1 && hold == 0, "at: frame 30 is moving", n, failures);
	Engine_FlycamAt(st, 4, 400, 65, pos, look, &station, &hold);
	EngFly_Check(station == -1, "at: frame 65 is moving", n, failures);
	for (k = 0; k < 3; k++)
	{
		const float lo = (st[0].pos[k] < st[1].pos[k]) ? st[0].pos[k] : st[1].pos[k];
		const float hi = (st[0].pos[k] < st[1].pos[k]) ? st[1].pos[k] : st[0].pos[k];

		if (st[0].pos[k] != st[1].pos[k])
		{
			EngFly_Check(pos[k] > lo && pos[k] < hi, "at: frame 65 pos is between stations 0 and 1", n, failures);
		}
	}
	Engine_FlycamAt(st, 4, 400, 100, pos, look, &station, &hold);
	EngFly_Check(station == 1 && hold == 0 && pos[0] == st[1].pos[0], "at: frame 100 is station 1", n, failures);
	Engine_FlycamAt(st, 4, 400, 400, pos, look, &station, &hold);
	EngFly_Check(station == 0 && hold == 0 && pos[0] == st[0].pos[0], "at: frame 400 loops to station 0", n, failures);
	for (i = 0; i < 500; i += 7)
	{
		Engine_FlycamAt(st, 1, 400, (u32)i, pos, look, &station, &hold);
		single = single && (station == 0) && (pos[0] == st[0].pos[0]);
	}
	EngFly_Check(single, "at: a single station is always station 0", n, failures);
	Engine_FlycamAt(st, 0, 400, 5, pos, look, &station, &hold);
	EngFly_Check(station == -1 && hold == 0 && pos[0] == 0.0f && look[2] == 0.0f, "at: no stations", n, failures);
	Engine_FlycamAt(st, 4, 400, 65, pos, look, &station, &hold);
	Engine_FlycamAt(st, 4, 400, 65, pos2, look2, &station2, &hold2);
	EngFly_Check(memcmp(pos, pos2, sizeof(pos)) == 0 && memcmp(look, look2, sizeof(look)) == 0 && station == station2 && hold == hold2,
	             "at: the same input gives the same output", n, failures);
}

internal void EngFly_SelfTestRing(int *n, int *failures)
{
	static const float bmin[3] = {-3000.0f, -500.0f, -2000.0f};
	static const float bmax[3] = {3000.0f, 800.0f, 2500.0f};
	static const float hmin[3] = {-32000.0f, -32000.0f, -32000.0f};
	static const float hmax[3] = {32000.0f, 32000.0f, 32000.0f};
	EngineFlycamStation ring[ENGINE_FLYCAM_RING];
	int ok = 1;
	int above = 1;
	int distinct = 1;
	int i;
	int j;
	int k;

	EngFly_Check(Engine_FlycamRing(bmin, bmax, ring) == ENGINE_FLYCAM_RING, "ring: count", n, failures);
	for (i = 0; i < ENGINE_FLYCAM_RING; i++)
	{
		for (k = 0; k < 3; k++)
		{
			ok = ok && (fabsf(ring[i].pos[k]) <= 32000.0f) && (fabsf(ring[i].look[k]) <= 32000.0f);
		}
		above = above && (ring[i].pos[1] > bmax[1]);
		for (j = i + 1; j < ENGINE_FLYCAM_RING; j++)
		{
			distinct = distinct && ((ring[i].pos[0] != ring[j].pos[0]) || (ring[i].pos[2] != ring[j].pos[2]));
		}
	}
	EngFly_Check(ok, "ring: coordinates within +-32000", n, failures);
	EngFly_Check(above, "ring: every station above the box", n, failures);
	EngFly_Check(distinct, "ring: distinct positions", n, failures);

	Engine_FlycamRing(hmin, hmax, ring);
	ok = 1;
	for (i = 0; i < ENGINE_FLYCAM_RING; i++)
	{
		for (k = 0; k < 3; k++)
		{
			ok = ok && (fabsf(ring[i].pos[k]) <= 32000.0f) && (fabsf(ring[i].look[k]) <= 32000.0f);
		}
	}
	EngFly_Check(ok, "ring: a huge box is clamped to +-32000", n, failures);
}

internal void EngFly_SelfTestSmooth(int *n, int *failures)
{
	static const float xs[6][6] = {
	    {0.0f, 200.0f, 0.0f, 100.0f, 0.0f, 500.0f},
	    {100.0f, 300.0f, 0.0f, 500.0f, 50.0f, 400.0f},
	    {1000.0f, 150.0f, 100.0f, 1500.0f, 0.0f, 600.0f},
	    {1300.0f, 700.0f, -200.0f, 1000.0f, 300.0f, -100.0f},
	    {3300.0f, 400.0f, 300.0f, 3000.0f, 100.0f, 900.0f},
	    {3350.0f, 900.0f, 320.0f, 3400.0f, 200.0f, 0.0f},
	};
	enum { FR = 600 };
	EngineFlycamStation st[6];
	EngineFlycamStation st2[6];
	static float P[FR + 1][3];
	static float L[FR + 1][3];
	float a[3];
	float b[3];
	double mean = 0.0;
	double lookMax = 0.0;
	double dev = 0.0;
	int i;
	int k;
	int ok;
	u32 f;

	for (i = 0; i < 6; i++)
	{
		for (k = 0; k < 3; k++)
		{
			st[i].pos[k] = xs[i][k];
			st[i].look[k] = xs[i][3 + k];
		}
	}
	for (f = 0; f <= FR; f++)
	{
		Engine_FlycamSmoothAt(st, 6, FR, (f == FR) ? 1000u : f, P[f], L[f]);
	}
	EngFly_Check(memcmp(P[0], st[0].pos, sizeof(a)) == 0 && memcmp(L[0], st[0].look, sizeof(a)) == 0, "smooth: frame 0 is station 0", n, failures);
	Engine_FlycamSmoothAt(st, 6, FR, FR - 1, a, b);
	EngFly_Check(memcmp(a, st[5].pos, sizeof(a)) == 0 && memcmp(b, st[5].look, sizeof(a)) == 0, "smooth: frame 599 is the last station", n, failures);
	EngFly_Check(memcmp(P[FR], st[5].pos, sizeof(a)) == 0 && memcmp(L[FR], st[5].look, sizeof(a)) == 0, "smooth: frame 1000 is the last station", n, failures);

	for (f = 0; f < FR - 1; f++)
	{
		mean += sqrt(pow((double)P[f + 1][0] - P[f][0], 2) + pow((double)P[f + 1][1] - P[f][1], 2) + pow((double)P[f + 1][2] - P[f][2], 2));
		{
			const double ls = sqrt(pow((double)L[f + 1][0] - L[f][0], 2) + pow((double)L[f + 1][1] - L[f][1], 2) + pow((double)L[f + 1][2] - L[f][2], 2));

			lookMax = (ls > lookMax) ? ls : lookMax;
		}
	}
	mean /= (double)(FR - 1);
	for (f = 0; f < FR - 1; f++)
	{
		const double d = sqrt(pow((double)P[f + 1][0] - P[f][0], 2) + pow((double)P[f + 1][1] - P[f][1], 2) + pow((double)P[f + 1][2] - P[f][2], 2));
		const double r = fabs(d - mean) / mean * 100.0;

		dev = (r > dev) ? r : dev;
	}
	EngFly_Check(dev <= 5.0, "smooth: speed is not constant (+-5 %)", n, failures);
	if (dev <= 5.0)
	{
		printf("engine selftest info: flycam smooth speed deviation %.2f %%\n", dev);
	}

	ok = 1;
	for (i = 0; i < 6; i++)
	{
		double best = 1.0e30;
		double bestL = 1.0e30;

		for (f = 0; f < FR; f++)
		{
			const double d = sqrt(pow((double)P[f][0] - st[i].pos[0], 2) + pow((double)P[f][1] - st[i].pos[1], 2) + pow((double)P[f][2] - st[i].pos[2], 2));
			const double dl = sqrt(pow((double)L[f][0] - st[i].look[0], 2) + pow((double)L[f][1] - st[i].look[1], 2) + pow((double)L[f][2] - st[i].look[2], 2));

			best = (d < best) ? d : best;
			bestL = (dl < bestL) ? dl : bestL;
		}
		ok = ok && (best < 2.0 * mean);
		if (!(bestL < lookMax))
		{
			EngFly_Check(0, "smooth: a look point is not passed", n, failures);
		}
	}
	EngFly_Check(ok, "smooth: a station is not passed", n, failures);

	Engine_FlycamSmoothAt(st, 6, FR, 250, a, b);
	Engine_FlycamSmoothAt(st, 6, FR, 250, P[0], L[0]);
	EngFly_Check(memcmp(a, P[0], sizeof(a)) == 0 && memcmp(b, L[0], sizeof(a)) == 0, "smooth: same input, same floats", n, failures);
	memcpy(st2, st, sizeof(st));
	st2[2].pos[1] += 400.0f;
	Engine_FlycamSmoothAt(st2, 6, FR, 250, P[0], L[0]);
	EngFly_Check(memcmp(a, P[0], sizeof(a)) != 0, "smooth: a changed station changes the result", n, failures);
	Engine_FlycamSmoothAt(st, 6, FR, 250, P[0], L[0]);
	EngFly_Check(memcmp(a, P[0], sizeof(a)) == 0, "smooth: the table is rebuilt back", n, failures);

	Engine_FlycamSmoothAt(st, 1, FR, 77, a, b);
	EngFly_Check(memcmp(a, st[0].pos, sizeof(a)) == 0 && memcmp(b, st[0].look, sizeof(a)) == 0, "smooth: one station", n, failures);
	EngFly_Check(Engine_FlycamSmoothAt(st, 0, FR, 77, a, b) == 0 && a[0] == 0.0f && b[2] == 0.0f, "smooth: no stations", n, failures);
}

int Engine_SelfTestFlycam(int *checks)
{
	const float zero[3] = {0.0f, 0.0f, 0.0f};
	const float a[3] = {0.0f, 0.0f, 1000.0f};
	int n = 0;
	int failures = 0;
	s16 rot[3];

	Engine_FlycamRot(zero, zero, rot);
	EngFly_Check(rot[0] == 0x800 && rot[1] == 0 && rot[2] == 0, "rot: d = 0", &n, &failures);
	Engine_FlycamRot(a, zero, rot);
	EngFly_Check(rot[0] == 0x800 && rot[1] == 0 && rot[2] == 0, "rot: level, looking along -z", &n, &failures);
	EngFly_SelfTestRot(&n, &failures);
	EngFly_SelfTestParse(&n, &failures);
	EngFly_SelfTestAt(&n, &failures);
	EngFly_SelfTestRing(&n, &failures);
	EngFly_SelfTestSmooth(&n, &failures);

	*checks += n;
	return failures;
}
