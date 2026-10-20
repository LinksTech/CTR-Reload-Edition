#include <platform/native_exttrack_nav.h>

// THE EXTERNAL TRACK'S CPU PATHS - see include/platform/native_exttrack_nav.h.
//
// THE BOTS DRIVE ON RAILS. Position, height, pitch and checkpoint of a bot
// come from the frames of its nav path (game/BOTS.c:1953-1958, 2049-2052,
// 2487-2510); the level's collision is asked only in the estimate and
// free-physics states (BOTS.c:2053-2102). So the external track gets its bots
// from data alone: three NavHeader blocks (include/namespace_Bots.h:56-81)
// hung into lev->LevNavTable, which BOTS_Adv_AdjustDifficulty ->
// BOTS_InitNavPath reads in MainInit_Drivers (MainInit.c:474-477,
// BOTS.c:115-167). Nothing of the bot logic changes, nor its random numbers.
//
// The converter did the computing - resampling, floor height, angles,
// distances, the checkpoint of each frame - and checked it in the game's
// integer arithmetic. Here every value is checked against the struct's ranges
// and the level's checkpoint table, then copied.
//
// NEVER THE DONOR'S PATHS. The level struct is the donor's (its LEV loads as
// usual), and its LevNavTable points at the donor's own lines. A track whose
// data fails the check gets NULL: the race starts without bots
// ([CTR Race] ... bots 0) and the [CTR ExtTrack] nav line says why.

enum
{
	EXTNAV_MAGIC = -0x1303,      // BOTS.c:136
	EXTNAV_COORD_LIMIT = 32700,  // as tools/collision_gen/lev_build.py:66
	EXTNAV_OPCODE_NONE = 0x0C00, // no lane change (BOTS.c:84-87)
	EXTNAV_FLAGS_TERRAIN = 0xF8, // the bot's terrain is (u8)flags >> 3 (BOTS.c:1804); no trick bits
	EXTNAV_TERRAIN_MAX = 20,     // VehAfterColl_GetTerrain: 21 entries
	EXTNAV_BLOCK_WORDS = (sizeof(struct NavHeader) + (EXTNAV_FRAMES_MAX * sizeof(struct NavFrame)) + 3) / 4,
};

// One block per path: the header, the frames right behind it
// (NAVHEADER_GETFRAME). Static: no pool of the game is touched, and the paths
// outlive every reload; 3 x 20.5 KB.
global_variable u32 s_extNavBlock[EXTNAV_PATHS][EXTNAV_BLOCK_WORDS];
global_variable struct NavHeader *s_extNavTable[EXTNAV_PATHS];

// The JSON's integers, so that a source never points into the reader's nodes.
global_variable int s_extNavFields[EXTNAV_PATHS][EXTNAV_FRAMES_MAX * EXTNAV_FIELDS];
global_variable int s_extNavUpFields[EXTNAV_PATHS][EXTNAV_FRAMES_MAX * 3];
global_variable int s_extNavZoneFields[EXTNAV_PATHS][EXTNAV_FRAMES_MAX];

// The anti-gravity side tables of the installed paths (frame index = the frame's
// place behind its header). s_extNavZonesOn = 0: no zone frame at all, every
// query returns at once.
global_variable s16 s_extNavUp[EXTNAV_PATHS][EXTNAV_FRAMES_MAX][3];
global_variable u8 s_extNavZone[EXTNAV_PATHS][EXTNAV_FRAMES_MAX];
global_variable int s_extNavCount[EXTNAV_PATHS];
global_variable int s_extNavZonesOn;
global_variable int s_extNavBotZone[8];
global_variable int s_extNavBotRespawns[8];

enum
{
	EXTNAV_UP_ONE = 4096,
	EXTNAV_UP_TOLERANCE = 64, // |up| = 4096 +- 64
};

internal const char *ExtNav_CheckPath(const struct ExtNavPathSrc *p, int checkpoints, int *badFrame)
{
	int f;

	*badFrame = -1;
	if ((p->count < 2) || (p->fields == NULL))
	{
		return "fewer than 2 frames (BOTS_Driver_Init makes no bot on such a path)";
	}
	if (p->count > EXTNAV_FRAMES_MAX)
	{
		return "more than 1023 frames";
	}
	if (p->fields[11] != 0)
	{
		*badFrame = 0;
		return "frame 0 is not on checkpoint 0 (the bots start toward frame 0, BOTS.c:3110)";
	}

	for (f = 0; f < p->count; f++)
	{
		const int *a = &p->fields[f * EXTNAV_FIELDS];
		int k;

		*badFrame = f;
		for (k = 0; k < 3; k++)
		{
			if ((a[k] < -EXTNAV_COORD_LIMIT) || (a[k] > EXTNAV_COORD_LIMIT))
			{
				return "a coordinate outside +-32700";
			}
		}
		for (k = 3; k <= 6; k++)
		{
			if ((a[k] < 0) || (a[k] > 0xFF))
			{
				return "a rot byte outside 0..255";
			}
		}
		if ((a[7] < 1) || (a[7] > 0x7FFF) || (a[8] < 1) || (a[8] > 0x7FFF))
		{
			return "a distance to the next frame outside 1..32767";
		}
		if (((a[9] & ~EXTNAV_FLAGS_TERRAIN) != 0) || ((a[9] >> 3) > EXTNAV_TERRAIN_MAX))
		{
			return "flags other than a terrain 0..20";
		}
		if ((a[10] != EXTNAV_OPCODE_NONE) || (a[12] != 0))
		{
			return "a path change or special bits";
		}
		if ((a[11] < 0) || (a[11] >= checkpoints))
		{
			return "a checkpoint index past the checkpoint table";
		}
		if ((p->up == NULL) != (p->zone == NULL))
		{
			return "up and zone come only together";
		}
		if (p->zone != NULL)
		{
			const int *u = &p->up[f * 3];
			const long long len2 = ((long long)u[0] * u[0]) + ((long long)u[1] * u[1]) + ((long long)u[2] * u[2]);
			const long long lo = EXTNAV_UP_ONE - EXTNAV_UP_TOLERANCE;
			const long long hi = EXTNAV_UP_ONE + EXTNAV_UP_TOLERANCE;

			if ((p->zone[f] < 0) || (p->zone[f] > EXTNAV_ZONES_MAX))
			{
				return "a zone outside 0..8";
			}
			if ((len2 < lo * lo) || (len2 > hi * hi))
			{
				return "an up vector whose length is not 4096 +- 64";
			}
		}
	}

	*badFrame = -1;
	return NULL;
}

internal void ExtNav_WritePath(int k, const struct ExtNavPathSrc *p)
{
	struct NavHeader *h = (struct NavHeader *)&s_extNavBlock[k][0];
	struct NavFrame *fr = NAVHEADER_GETFRAME(h);
	int f;

	memset(h, 0, sizeof(*h));
	h->magicNumber = (s16)EXTNAV_MAGIC;
	h->numPoints = (s16)p->count;
	h->posY_firstNode = 0;
	h->last = NULL; // BOTS_InitNavPath sets it (BOTS.c:157-161)

	for (f = 0; f < p->count; f++)
	{
		const int *a = &p->fields[f * EXTNAV_FIELDS];

		fr[f].pos.x = (s16)a[0];
		fr[f].pos.y = (s16)a[1];
		fr[f].pos.z = (s16)a[2];
		fr[f].rot[0] = (u8)a[3];
		fr[f].rot[1] = (u8)a[4];
		fr[f].rot[2] = (u8)a[5];
		fr[f].rot[3] = (u8)a[6];
		fr[f].distToNextNavXYZ = (s16)a[7];
		fr[f].distToNextNavXZ = (s16)a[8];
		fr[f].flags = (s16)a[9];
		fr[f].pathChangeOpcode = (s16)a[10];
		fr[f].goBackCount = (u8)a[11];
		fr[f].specialBits = (u8)a[12];
		s_extNavZone[k][f] = (u8)((p->zone != NULL) ? p->zone[f] : 0);
		s_extNavUp[k][f][0] = (s16)((p->up != NULL) ? p->up[(f * 3) + 0] : 0);
		s_extNavUp[k][f][1] = (s16)((p->up != NULL) ? p->up[(f * 3) + 1] : EXTNAV_UP_ONE);
		s_extNavUp[k][f][2] = (s16)((p->up != NULL) ? p->up[(f * 3) + 2] : 0);
		if (s_extNavZone[k][f] != 0)
		{
			s_extNavZonesOn = 1;
		}
	}

	s_extNavCount[k] = p->count;
	s_extNavTable[k] = h;
}

int ExtNav_Install(struct Level *lev, const struct ExtNavSrc *src)
{
	const char *why = NULL;
	int bad = -1, badPath = -1, k;

	if (lev == NULL)
	{
		return 0;
	}

	s_extNavZonesOn = 0;
	memset(s_extNavBotZone, 0, sizeof(s_extNavBotZone));
	memset(s_extNavBotRespawns, 0, sizeof(s_extNavBotRespawns));
	if ((src == NULL) || (src->pathCount != EXTNAV_PATHS))
	{
		why = "not exactly 3 paths";
	}
	else if ((lev->ptr_restart_points == NULL) || (lev->cnt_restart_points < 1) || (lev->cnt_restart_points > 255))
	{
		why = "the level has no checkpoint table (the race part comes first)";
	}
	else if (src->checkpointCount != lev->cnt_restart_points)
	{
		why = "checkpoint_count is not the level's number of checkpoints";
	}
	else
	{
		for (k = 0; (k < EXTNAV_PATHS) && (why == NULL); k++)
		{
			badPath = k;
			why = ExtNav_CheckPath(&src->path[k], lev->cnt_restart_points, &bad);
		}
	}

	if (why != NULL)
	{
		lev->LevNavTable = NULL;
		s_extNavZonesOn = 0;
		Platform_Log("[CTR ExtTrack] nav: REFUSED - %s (path %d, frame %d); the race has no bots\n", why, badPath, bad);
		return 0;
	}

	for (k = 0; k < EXTNAV_PATHS; k++)
	{
		const struct ExtNavPathSrc *p = &src->path[k];

		ExtNav_WritePath(k, p);
		Platform_Log("[CTR ExtTrack] nav: path %d: %d frames, frame 0 at (%d, %d, %d), checkpoints %d .. %d\n", k, p->count, p->fields[0],
		             p->fields[1], p->fields[2], p->fields[11], p->fields[((p->count - 1) * EXTNAV_FIELDS) + 11]);
	}

	lev->LevNavTable = s_extNavTable;
	Platform_Log("[CTR ExtTrack] nav: 3 paths in the level against %d checkpoints\n", lev->cnt_restart_points);
	if (s_extNavZonesOn)
	{
		int zf[EXTNAV_PATHS] = {0, 0, 0};

		for (k = 0; k < EXTNAV_PATHS; k++)
		{
			int f;

			for (f = 0; f < s_extNavCount[k]; f++)
			{
				zf[k] += (s_extNavZone[k][f] != 0);
			}
		}
		Platform_Log("[CTR ExtTrack] nav: zone frames %d / %d / %d (bots stay on their rails there, up from the frames)\n", zf[0], zf[1], zf[2]);
	}
	return 1;
}

// The section "nav" into s_extNavFields. NULL = read, else why not; *badPath and
// *badFrame say where.
internal const char *ExtNav_ReadJson(struct ExtNavSrc *src, int *badPath, int *badFrame)
{
	const ExtJsonDoc *doc = ExtTrack_Json();
	const int sec = ExtTrack_Section("nav");
	int paths, path, k;

	memset(src, 0, sizeof(*src));
	*badPath = -1;
	*badFrame = -1;
	if ((doc == NULL) || (sec < 0))
	{
		return "exttrack.json has no section nav";
	}

	src->checkpointCount = (int)ExtJson_Number(doc, ExtJson_Get(doc, sec, "checkpoint_count"), -1.0);
	paths = ExtJson_Get(doc, sec, "paths");
	if ((ExtJson_Type(doc, paths) != EXT_JSON_ARRAY) || (ExtJson_Count(doc, paths) != EXTNAV_PATHS))
	{
		return "nav.paths is not an array of 3";
	}

	for (k = 0, path = ExtJson_First(doc, paths); (k < EXTNAV_PATHS) && (path >= 0); k++, path = ExtJson_Next(doc, path))
	{
		const int frames = ExtJson_Get(doc, path, "frames");
		const int magic = ExtJson_Get(doc, path, "magic");
		int f, frame;

		*badPath = k;
		if ((magic >= 0) && ((int)ExtJson_Number(doc, magic, 0.0) != EXTNAV_MAGIC))
		{
			return "a path's magic is not -0x1303";
		}
		if ((ExtJson_Type(doc, frames) != EXT_JSON_ARRAY) || (ExtJson_Count(doc, frames) > EXTNAV_FRAMES_MAX))
		{
			return "frames is not an array of at most 1023";
		}

		for (f = 0, frame = ExtJson_First(doc, frames); frame >= 0; f++, frame = ExtJson_Next(doc, frame))
		{
			double v[EXTNAV_FIELDS];
			int j;

			*badFrame = f;
			if ((ExtJson_Count(doc, frame) != EXTNAV_FIELDS) || (ExtJson_Numbers(doc, frame, v, EXTNAV_FIELDS) != EXTNAV_FIELDS))
			{
				return "a frame is not 13 numbers";
			}
			for (j = 0; j < EXTNAV_FIELDS; j++)
			{
				if ((v[j] < -2147483647.0) || (v[j] > 2147483647.0) || ((double)(int)v[j] != v[j]))
				{
					return "a frame value is not an integer";
				}
				s_extNavFields[k][(f * EXTNAV_FIELDS) + j] = (int)v[j];
			}
		}

		src->path[k].count = f;
		src->path[k].fields = s_extNavFields[k];

		{
			const int ups = ExtJson_Get(doc, path, "up");
			const int zones = ExtJson_Get(doc, path, "zone");
			int i, node;

			if ((ups >= 0) || (zones >= 0))
			{
				if ((ExtJson_Type(doc, ups) != EXT_JSON_ARRAY) || (ExtJson_Count(doc, ups) != f) || (ExtJson_Type(doc, zones) != EXT_JSON_ARRAY) ||
				    (ExtJson_Count(doc, zones) != f))
				{
					return "up and zone are not two arrays as long as frames";
				}
				for (i = 0, node = ExtJson_First(doc, ups); node >= 0; i++, node = ExtJson_Next(doc, node))
				{
					double v[3];
					int j;

					*badFrame = i;
					if ((ExtJson_Count(doc, node) != 3) || (ExtJson_Numbers(doc, node, v, 3) != 3))
					{
						return "an up entry is not 3 numbers";
					}
					for (j = 0; j < 3; j++)
					{
						if ((v[j] < -32768.0) || (v[j] > 32767.0) || ((double)(int)v[j] != v[j]))
						{
							return "an up entry is not 3 integers in s16";
						}
						s_extNavUpFields[k][(i * 3) + j] = (int)v[j];
					}
				}
				for (i = 0, node = ExtJson_First(doc, zones); node >= 0; i++, node = ExtJson_Next(doc, node))
				{
					const double z = ExtJson_Number(doc, node, -1.0);

					*badFrame = i;
					if (((double)(int)z != z) || (z < 0.0) || (z > (double)EXTNAV_ZONES_MAX))
					{
						return "a zone entry is not an integer 0..8";
					}
					s_extNavZoneFields[k][i] = (int)z;
				}
				src->path[k].up = s_extNavUpFields[k];
				src->path[k].zone = s_extNavZoneFields[k];
			}
		}
	}

	src->pathCount = k;
	*badPath = -1;
	*badFrame = -1;
	return NULL;
}

void ExtNav_OnLevelLoaded(struct Level *lev)
{
	struct ExtNavSrc src;
	int badPath, badFrame;
	const char *why;

	if (lev == NULL)
	{
		return;
	}

	why = ExtNav_ReadJson(&src, &badPath, &badFrame);
	if (why != NULL)
	{
		lev->LevNavTable = NULL;
		s_extNavZonesOn = 0;
		Platform_Log("[CTR ExtTrack] nav: REFUSED - %s (path %d, frame %d); the race has no bots\n", why, badPath, badFrame);
		return;
	}

	(void)ExtNav_Install(lev, &src);
}

//----------------------------------------------------------------------------------------
// Anti-gravity queries for BOTS.c (see the header). Every one returns at once
// without zone frames.

// Path and index of an external frame; -1 = not one of ours.
internal int ExtNav_FrameIndex(const struct NavFrame *frame, int *path)
{
	int k;

	for (k = 0; k < EXTNAV_PATHS; k++)
	{
		const struct NavFrame *base = NAVHEADER_GETFRAME((struct NavHeader *)&s_extNavBlock[k][0]);

		if ((s_extNavTable[k] != NULL) && (frame >= base) && (frame < (base + s_extNavCount[k])))
		{
			*path = k;
			return (int)(frame - base);
		}
	}
	return -1;
}

int ExtNav_FrameZone(const struct NavFrame *frame)
{
	int k = 0, i;

	if (!s_extNavZonesOn || (frame == NULL))
	{
		return 0;
	}
	i = ExtNav_FrameIndex(frame, &k);
	return (i < 0) ? 0 : s_extNavZone[k][i];
}

internal int ExtNav_Isqrt(long long v)
{
	long long r = 0, bit = 1LL << 40;

	if (v <= 0)
	{
		return 0;
	}
	while (bit > v)
	{
		bit >>= 2;
	}
	while (bit != 0)
	{
		if (v >= r + bit)
		{
			v -= r + bit;
			r = (r >> 1) + bit;
		}
		else
		{
			r >>= 1;
		}
		bit >>= 2;
	}
	return (int)r;
}

int ExtNav_FrameUpLerp(const struct NavFrame *curr, const struct NavFrame *next, int percentage, short up[3])
{
	int kc = 0, kn = 0, ic, in, j, len;
	int a[3] = {0, EXTNAV_UP_ONE, 0}, b[3] = {0, EXTNAV_UP_ONE, 0}, v[3];

	if (!s_extNavZonesOn)
	{
		return 0;
	}
	ic = ExtNav_FrameIndex(curr, &kc);
	in = ExtNav_FrameIndex(next, &kn);
	if (((ic < 0) || (s_extNavZone[kc][ic] == 0)) && ((in < 0) || (s_extNavZone[kn][in] == 0)))
	{
		return 0;
	}
	for (j = 0; j < 3; j++)
	{
		if ((ic >= 0) && (s_extNavZone[kc][ic] != 0))
		{
			a[j] = s_extNavUp[kc][ic][j];
		}
		if ((in >= 0) && (s_extNavZone[kn][in] != 0))
		{
			b[j] = s_extNavUp[kn][in][j];
		}
	}
	percentage = (percentage < 0) ? 0 : ((percentage > 4096) ? 4096 : percentage);
	for (j = 0; j < 3; j++)
	{
		v[j] = a[j] + (((b[j] - a[j]) * percentage) >> 12);
	}
	len = ExtNav_Isqrt(((long long)v[0] * v[0]) + ((long long)v[1] * v[1]) + ((long long)v[2] * v[2]));
	if (len == 0)
	{
		// opposite normals half way: keep the current frame's
		v[0] = a[0];
		v[1] = a[1];
		v[2] = a[2];
		len = EXTNAV_UP_ONE;
	}
	for (j = 0; j < 3; j++)
	{
		up[j] = (short)((v[j] * EXTNAV_UP_ONE) / len);
	}
	return 1;
}

struct NavFrame *ExtNav_ZoneEntryFrame(struct NavFrame *frame)
{
	int k = 0, i, n;

	if (!s_extNavZonesOn || (frame == NULL))
	{
		return frame;
	}
	i = ExtNav_FrameIndex(frame, &k);
	if ((i < 0) || (s_extNavZone[k][i] == 0))
	{
		return frame;
	}
	for (n = 0; (n < s_extNavCount[k]) && (s_extNavZone[k][i] != 0); n++)
	{
		i = (i == 0) ? (s_extNavCount[k] - 1) : (i - 1);
	}
	return NAVHEADER_GETFRAME((struct NavHeader *)&s_extNavBlock[k][0]) + i;
}

void ExtNav_BotTick(const struct Driver *bot, const struct NavFrame *curr)
{
	int k = 0, i, zone, id;

	if (!s_extNavZonesOn || (bot == NULL))
	{
		return;
	}
	id = bot->driverID;
	if ((id < 0) || (id >= 8))
	{
		return;
	}
	i = ExtNav_FrameIndex(curr, &k);
	zone = (i < 0) ? 0 : s_extNavZone[k][i];
	if (zone != s_extNavBotZone[id])
	{
		const struct GameTracker *gGT = sdata->gGT;
		const int tick = (gGT != NULL) ? (int)gGT->timer : -1;

		if (s_extNavBotZone[id] != 0)
		{
			Platform_Log("[CTR ExtNav] bot %d zone %d leave lap %d path %d frame %d tick %d\n", id, s_extNavBotZone[id], (int)bot->lapIndex, k, i, tick);
		}
		if (zone != 0)
		{
			Platform_Log("[CTR ExtNav] bot %d zone %d enter lap %d path %d frame %d tick %d\n", id, zone, (int)bot->lapIndex, k, i, tick);
		}
		s_extNavBotZone[id] = zone;
	}
}

void ExtNav_BotRespawn(const struct Driver *bot, int zone, int frameIndex)
{
	int id;

	if (!s_extNavZonesOn || (bot == NULL))
	{
		return;
	}
	id = bot->driverID;
	if ((id < 0) || (id >= 8))
	{
		return;
	}
	if (zone != 0)
	{
		s_extNavBotRespawns[id]++;
	}
	Platform_Log("[CTR ExtNav] bot %d respawn zone %d frame %d lap %d (respawns in zones %d)\n", id, zone, frameIndex, (int)bot->lapIndex,
	             s_extNavBotRespawns[id]);
}

int ExtNav_Active(void)
{
	const struct GameTracker *gGT = sdata->gGT;

	return (gGT != NULL) && (gGT->level1 != NULL) && (gGT->level1->LevNavTable == s_extNavTable);
}

//----------------------------------------------------------------------------------------
// --ext-selftest nav: 3 straight paths of 16 frames against 8 checkpoints, no
// track data, no window.

internal void ExtNav_TestPath(int *fields, int count, int lane, int checkpoints)
{
	int f;

	for (f = 0; f < count; f++)
	{
		int *a = &fields[f * EXTNAV_FIELDS];

		a[0] = (f * 240) + lane;
		a[1] = 0;
		a[2] = lane;
		a[3] = 0;
		a[4] = 0x40; // heading +x
		a[5] = 0;
		a[6] = 0;
		a[7] = 240;
		a[8] = 240;
		a[9] = 11 << 3; // HARDPACK
		a[10] = EXTNAV_OPCODE_NONE;
		a[11] = (f * checkpoints) / count;
		a[12] = 0;
	}
}

internal void ExtNav_Expect(int ok, const char *what, int *n, int *failures)
{
	(*n)++;
	if (!ok)
	{
		(*failures)++;
		printf("ext selftest nav: FAIL %s\n", what);
	}
}

int ExtNav_Selftest(void)
{
	static int fields[EXTNAV_PATHS][16 * EXTNAV_FIELDS];
	static struct CheckpointNode nodes[8];
	static struct Level lev;
	static struct NavHeader *donorTable[EXTNAV_PATHS]; // stands for the donor's own table
	struct ExtNavSrc src;
	int n = 0, failures = 0, k;

	memset(&lev, 0, sizeof(lev));
	lev.ptr_restart_points = nodes;
	lev.cnt_restart_points = 8;

	memset(&src, 0, sizeof(src));
	src.pathCount = EXTNAV_PATHS;
	src.checkpointCount = 8;
	for (k = 0; k < EXTNAV_PATHS; k++)
	{
		ExtNav_TestPath(fields[k], 16, (k - 1) * 200, 8);
		src.path[k].count = 16;
		src.path[k].fields = fields[k];
	}

	// 1: taken - the game's own test passes (magic, more than one point,
	// BOTS.c:128-140, 3155), the frames lie right behind the header, every field copied.
	{
		const int ok = ExtNav_Install(&lev, &src);
		const struct NavFrame *f2 = NAVHEADER_GETFRAME(s_extNavTable[2]);
		const struct NavFrame *f0 = NAVHEADER_GETFRAME(s_extNavTable[0]);

		ExtNav_Expect(ok && (lev.LevNavTable == s_extNavTable), "1 installed into LevNavTable", &n, &failures);
		ExtNav_Expect((s_extNavTable[2]->magicNumber == EXTNAV_MAGIC) && (s_extNavTable[2]->numPoints == 16) && (s_extNavTable[2]->last == NULL),
		              "1 header magic, count, last", &n, &failures);
		ExtNav_Expect((f2[15].goBackCount == 7) && (f0[3].pos.x == 520) && (f0[3].pos.z == -200) && (f0[3].rot[1] == 0x40) &&
		                  (f0[3].flags == (11 << 3)) && (f0[3].pathChangeOpcode == EXTNAV_OPCODE_NONE) && (f0[3].distToNextNavXZ == 240),
		              "1 frames copied behind the header", &n, &failures);
	}

	// 2: a checkpoint past the table -> refused, and no table at all (not the donor's)
	lev.LevNavTable = donorTable;
	fields[1][(5 * EXTNAV_FIELDS) + 11] = 8;
	ExtNav_Expect(!ExtNav_Install(&lev, &src) && (lev.LevNavTable == NULL), "2 checkpoint past the table refused, table NULL", &n, &failures);
	fields[1][(5 * EXTNAV_FIELDS) + 11] = 2;

	// 3: checkpoint_count is not the level's, and a level without a checkpoint table
	src.checkpointCount = 9;
	ExtNav_Expect(!ExtNav_Install(&lev, &src) && (lev.LevNavTable == NULL), "3 checkpoint_count differs", &n, &failures);
	src.checkpointCount = 8;
	lev.ptr_restart_points = NULL;
	ExtNav_Expect(!ExtNav_Install(&lev, &src), "3 no checkpoint table", &n, &failures);
	lev.ptr_restart_points = nodes;

	// 4: a trick bit in flags (jump 0x400), a distance of 0, frame 0 off checkpoint 0
	fields[0][9] |= 0x400;
	ExtNav_Expect(!ExtNav_Install(&lev, &src), "4 trick bit refused", &n, &failures);
	fields[0][9] = 11 << 3;
	fields[2][(7 * EXTNAV_FIELDS) + 8] = 0;
	ExtNav_Expect(!ExtNav_Install(&lev, &src), "4 distance 0 refused", &n, &failures);
	fields[2][(7 * EXTNAV_FIELDS) + 8] = 240;
	fields[1][11] = 1;
	ExtNav_Expect(!ExtNav_Install(&lev, &src), "4 frame 0 off checkpoint 0 refused", &n, &failures);
	fields[1][11] = 0;

	// 5: one frame only, and two paths only
	src.path[2].count = 1;
	ExtNav_Expect(!ExtNav_Install(&lev, &src), "5 one frame refused", &n, &failures);
	src.path[2].count = 16;
	src.pathCount = 2;
	ExtNav_Expect(!ExtNav_Install(&lev, &src), "5 two paths refused", &n, &failures);
	src.pathCount = EXTNAV_PATHS;

	// 6: whole again - taken, and without up/zone not a single zone frame
	ExtNav_Expect(ExtNav_Install(&lev, &src) && (lev.LevNavTable == s_extNavTable), "6 whole again", &n, &failures);
	{
		const struct NavFrame *f1 = NAVHEADER_GETFRAME(s_extNavTable[1]);
		short up[3] = {1, 2, 3};

		ExtNav_Expect(!s_extNavZonesOn && (ExtNav_FrameZone(f1 + 5) == 0) && !ExtNav_FrameUpLerp(f1 + 5, f1 + 6, 2048, up) && (up[0] == 1) &&
		                  (ExtNav_ZoneEntryFrame((struct NavFrame *)(f1 + 5)) == f1 + 5),
		              "6 no zone without up/zone", &n, &failures);
	}

	// 7: path 1 with a zone on frames 5..8 (up turning from +y to +x), the others without
	{
		static int ups[16 * 3];
		static int zones[16];
		const struct NavFrame *f1;
		short up[3] = {0, 0, 0};
		int f;

		for (f = 0; f < 16; f++)
		{
			const int in = (f >= 5) && (f <= 8);

			zones[f] = in ? 2 : 0;
			ups[(f * 3) + 0] = ((f >= 7) && in) ? 4096 : 0;
			ups[(f * 3) + 1] = ((f >= 7) && in) ? 0 : 4096;
			ups[(f * 3) + 2] = 0;
		}
		src.path[1].up = ups;
		src.path[1].zone = zones;
		ExtNav_Expect(ExtNav_Install(&lev, &src) && s_extNavZonesOn, "7 zone path installed", &n, &failures);
		f1 = NAVHEADER_GETFRAME(s_extNavTable[1]);
		ExtNav_Expect((ExtNav_FrameZone(f1 + 5) == 2) && (ExtNav_FrameZone(f1 + 4) == 0) && (ExtNav_FrameZone(f1 + 9) == 0) &&
		                  (ExtNav_FrameZone(NAVHEADER_GETFRAME(s_extNavTable[0]) + 6) == 0),
		              "7 zone per frame and path", &n, &failures);
		ExtNav_Expect(ExtNav_ZoneEntryFrame((struct NavFrame *)(f1 + 7)) == f1 + 4, "7 entry frame before the zone", &n, &failures);
		ExtNav_Expect(ExtNav_FrameUpLerp(f1 + 6, f1 + 7, 2048, up) && (up[0] > 2800) && (up[0] < 2950) && (up[1] > 2800) && (up[1] < 2950) &&
		                  (up[2] == 0),
		              "7 up half way between +y and +x is normalised", &n, &failures);
		ExtNav_Expect(ExtNav_FrameUpLerp(f1 + 4, f1 + 5, 0, up) && (up[1] == 4096) && !ExtNav_FrameUpLerp(f1 + 2, f1 + 3, 0, up),
		              "7 up at the zone edge, none outside", &n, &failures);

		// 8: an up vector of the wrong length, and up without zone, are refused
		ups[(6 * 3) + 1] = 3000;
		ExtNav_Expect(!ExtNav_Install(&lev, &src) && !s_extNavZonesOn, "8 short up vector refused", &n, &failures);
		ups[(6 * 3) + 1] = 4096;
		src.path[1].zone = NULL;
		ExtNav_Expect(!ExtNav_Install(&lev, &src), "8 up without zone refused", &n, &failures);
		src.path[1].up = NULL;
		ExtNav_Expect(ExtNav_Install(&lev, &src) && !s_extNavZonesOn, "8 whole again without zones", &n, &failures);
	}

	if (failures == 0)
	{
		printf("ext selftest nav passed: %d checks, 0 failures\n", n);
		return 0;
	}
	printf("ext selftest nav FAILED: %d checks, %d failures\n", n, failures);
	return 1;
}
