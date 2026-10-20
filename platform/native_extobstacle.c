#include <platform/native_extobstacle.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

// THE EXTERNAL TRACK'S MOVING OBSTACLES - see include/platform/native_extobstacle.h.
//
// The converter puts the moving parts (their meshes) into the track's file as
// dynamic nodes with node animations, so the engine already draws them; this
// part gives them their effect on the karts. Its boxes ride on the very matrices
// the parts are drawn with (the node_anims entries, ExtTrack_MatAt) at the very
// time the drawing uses (the logic timer / 30, Engine_FrameTime): what the
// driver sees is what hits him. The hit is the game's own hazard hit
// (RB_Hazard_HurtDriver: the player through VehPickState_NewState, a bot
// through BOTS_ChangeState), once per entry into a box.
//
// Included after platform/engine_exttrack.c (main.c): it reads the drawing's
// node list (s_extDraw) to log which of its nodes the drawing would submit -
// read only.

enum
{
	EXT_OBST_ITEMS_MAX = 4,
	EXT_OBST_BOXES_MAX = 8,
	EXT_OBST_ANIMS_MAX = 8,
	EXT_OBST_KEYS_MAX = 2048, // samples of one node animation (the converter's train: 630)
	EXT_OBST_NODES_MAX = 64,
	EXT_OBST_NAME_MAX = 48,
	EXT_OBST_SEATS = 8,
	EXT_OBST_KART_MIDDLE = 25, // the kart's middle over posCurr (VehPhysForce_OnApplyForces: half-radius 25)
	EXT_OBST_PLACE_AHEAD_FRAMES = 30,
};

typedef struct
{
	int anim; // index into s_extObst.anim
	float center[3];
	float half[3];
	float axes[3][3]; // rows: the box's unit axes in the rest pose
	char phantom[EXT_OBST_NAME_MAX];
} ExtObstBox;

typedef struct
{
	int entry; // the node_anims entry
	int count;
	float period;
	float times[EXT_OBST_KEYS_MAX];
	float mats[EXT_OBST_KEYS_MAX * 12];
} ExtObstAnim;

typedef struct
{
	char name[EXT_OBST_NAME_MAX];
	int damage;
	float kartRadius;
	int boxCount;
	ExtObstBox box[EXT_OBST_BOXES_MAX];
	int planeAnim;    // index into s_extObst.anim, -1 none
	float planePoint[3];
	float planeNormal[3];
	float planeRef[3]; // rest point of the front part that crosses the plane
	float planeSide;   // last signed distance (0 = not yet measured)
	int planeSideSet;
	int nodeCount;
	char nodes[EXT_OBST_NODES_MAX][EXT_OBST_NAME_MAX + 16];
	int exits;
	int hits;
} ExtObstItem;

int g_cfg_extObstaclePlace = -1;

global_variable struct
{
	int ready;
	int itemCount;
	ExtObstItem item[EXT_OBST_ITEMS_MAX];
	int animCount;
	ExtObstAnim anim[EXT_OBST_ANIMS_MAX];
	u8 inside[EXT_OBST_ITEMS_MAX][EXT_OBST_SEATS]; // seat is in a box of the item (hit given)
	int hitFrame[EXT_OBST_ITEMS_MAX][EXT_OBST_SEATS]; // the frame of the last hit (for the line one frame later), 0 none
	int frame;
	int placed;
} s_extObst;

// ---------------------------------------------------------------------------
// maths (also the selftest's)

// The pose of a rest point at time t: p' = M p (3x4 rows, ExtTrack_MatAt).
internal void ExtObst_Apply(const float m[12], const float p[3], float out[3])
{
	for (int r = 0; r < 3; r++)
	{
		out[r] = (m[4 * r] * p[0]) + (m[4 * r + 1] * p[1]) + (m[4 * r + 2] * p[2]) + m[4 * r + 3];
	}
}

internal void ExtObst_Rotate(const float m[12], const float v[3], float out[3])
{
	for (int r = 0; r < 3; r++)
	{
		out[r] = (m[4 * r] * v[0]) + (m[4 * r + 1] * v[1]) + (m[4 * r + 2] * v[2]);
	}
}

// 1 = point q lies in the box (rest pose moved by m), grown by grow on every side.
internal int ExtObst_InBox(const ExtObstBox *b, const float m[12], const float q[3], float grow)
{
	float c[3];

	ExtObst_Apply(m, b->center, c);
	for (int a = 0; a < 3; a++)
	{
		float ax[3];
		float len;
		float along;

		ExtObst_Rotate(m, b->axes[a], ax);
		len = sqrtf((ax[0] * ax[0]) + (ax[1] * ax[1]) + (ax[2] * ax[2]));
		if (len < 1e-6f)
		{
			return 0;
		}
		along = (((q[0] - c[0]) * ax[0]) + ((q[1] - c[1]) * ax[1]) + ((q[2] - c[2]) * ax[2])) / len;
		if (fabsf(along) > (b->half[a] + grow))
		{
			return 0;
		}
	}
	return 1;
}

internal float ExtObst_Side(const ExtObstItem *it, const float p[3])
{
	return ((p[0] - it->planePoint[0]) * it->planeNormal[0]) + ((p[1] - it->planePoint[1]) * it->planeNormal[1]) +
	       ((p[2] - it->planePoint[2]) * it->planeNormal[2]);
}

internal void ExtObst_Mat(const ExtObstAnim *a, float t, float m[12])
{
	ExtTrack_MatAt(a->times, a->mats, a->count, a->period, t, m);
}

// ---------------------------------------------------------------------------
// reading the section

internal int ExtObst_Numbers(const ExtJsonDoc *doc, int node, float *out, int n)
{
	double v[9];

	if ((n > 9) || (ExtJson_Numbers(doc, node, v, n) != n))
	{
		return 0;
	}
	for (int i = 0; i < n; i++)
	{
		out[i] = (float)v[i];
	}
	return 1;
}

// The node_anims entry `entry` as an animation of this part (shared by the boxes
// of the same part); -1 when it does not read.
internal int ExtObst_Anim(const ExtJsonDoc *doc, int entry, const char **why)
{
	const int list = ExtJson_Get(doc, 0, "node_anims");
	int node;
	int times, mats, n;
	ExtObstAnim *a;

	for (int i = 0; i < s_extObst.animCount; i++)
	{
		if (s_extObst.anim[i].entry == entry)
		{
			return i;
		}
	}
	if (s_extObst.animCount >= EXT_OBST_ANIMS_MAX)
	{
		*why = "more moving parts than the part holds";
		return -1;
	}
	node = ExtJson_At(doc, list, entry);
	times = ExtJson_Get(doc, node, "times");
	mats = ExtJson_Get(doc, node, "matrices");
	n = ExtJson_Count(doc, times);
	if ((node < 0) || (n < 2) || (n > EXT_OBST_KEYS_MAX) || (ExtJson_Count(doc, mats) != n))
	{
		*why = "a box's node_anims entry is missing or has no keys that fit";
		return -1;
	}
	a = &s_extObst.anim[s_extObst.animCount];
	a->entry = entry;
	a->count = n;
	a->period = (float)ExtJson_Number(doc, ExtJson_Get(doc, node, "period"), 0.0);
	for (int k = 0; k < n; k++)
	{
		double v12[12];
		const int row = ExtJson_At(doc, mats, k);

		a->times[k] = (float)ExtJson_Number(doc, ExtJson_At(doc, times, k), 0.0);
		if ((ExtJson_Count(doc, row) != 12) || (ExtJson_Numbers(doc, row, v12, 12) != 12))
		{
			*why = "a node_anims matrix is not 12 numbers";
			return -1;
		}
		for (int j = 0; j < 12; j++)
		{
			a->mats[12 * k + j] = (float)v12[j];
		}
	}
	return s_extObst.animCount++;
}

internal const char *ExtObst_ReadItem(const ExtJsonDoc *doc, int node, ExtObstItem *it)
{
	const char *why = NULL;
	const int boxes = ExtJson_Get(doc, node, "boxes");
	const int plane = ExtJson_Get(doc, node, "exit_plane");
	const int nodes = ExtJson_Get(doc, node, "nodes");
	int b;

	memset(it, 0, sizeof(*it));
	snprintf(it->name, sizeof(it->name), "%s", ExtJson_String(doc, ExtJson_Get(doc, node, "name"), "obstacle"));
	it->damage = (int)ExtJson_Number(doc, ExtJson_Get(doc, node, "damage"), -1.0);
	it->kartRadius = (float)ExtJson_Number(doc, ExtJson_Get(doc, node, "kart_radius"), 25.0);
	it->planeAnim = -1;
	if (strcmp(ExtJson_String(doc, ExtJson_Get(doc, node, "kind"), ""), "animated_boxes") != 0)
	{
		return "kind is not animated_boxes";
	}
	if (strcmp(ExtJson_String(doc, ExtJson_Get(doc, node, "clock"), ""), "timer/30") != 0)
	{
		return "clock is not timer/30 (the clock of the drawn node animations)";
	}
	if ((it->damage < 1) || (it->damage > 5))
	{
		return "damage outside 1..5 (the game's hazard hits)";
	}
	if ((ExtJson_Count(doc, boxes) < 1) || (ExtJson_Count(doc, boxes) > EXT_OBST_BOXES_MAX))
	{
		return "no boxes, or more than the part holds";
	}
	b = 0;
	for (int bn = ExtJson_First(doc, boxes); bn >= 0; bn = ExtJson_Next(doc, bn), b++)
	{
		ExtObstBox *box = &it->box[b];
		float ax[9];
		const int entry = (int)ExtJson_Number(doc, ExtJson_Get(doc, bn, "anim"), -1.0);
		const int axes = ExtJson_Get(doc, bn, "axes");

		if (!ExtObst_Numbers(doc, ExtJson_Get(doc, bn, "center"), box->center, 3) || !ExtObst_Numbers(doc, ExtJson_Get(doc, bn, "half"), box->half, 3) ||
		    (ExtJson_Count(doc, axes) != 3) || !ExtObst_Numbers(doc, ExtJson_At(doc, axes, 0), &ax[0], 3) ||
		    !ExtObst_Numbers(doc, ExtJson_At(doc, axes, 1), &ax[3], 3) || !ExtObst_Numbers(doc, ExtJson_At(doc, axes, 2), &ax[6], 3))
		{
			return "a box without centre, half sizes or three axes";
		}
		for (int a = 0; a < 3; a++)
		{
			box->axes[a][0] = ax[3 * a];
			box->axes[a][1] = ax[3 * a + 1];
			box->axes[a][2] = ax[3 * a + 2];
			if (box->half[a] <= 0.0f)
			{
				return "a half size <= 0";
			}
		}
		snprintf(box->phantom, sizeof(box->phantom), "%s", ExtJson_String(doc, ExtJson_Get(doc, bn, "phantom"), ""));
		box->anim = (entry >= 0) ? ExtObst_Anim(doc, entry, &why) : -1;
		if (box->anim < 0)
		{
			return (why != NULL) ? why : "a box without its node_anims entry";
		}
	}
	it->boxCount = b;
	if (plane >= 0)
	{
		const int entry = (int)ExtJson_Number(doc, ExtJson_Get(doc, plane, "anim"), -1.0);

		if (!ExtObst_Numbers(doc, ExtJson_Get(doc, plane, "point"), it->planePoint, 3) || !ExtObst_Numbers(doc, ExtJson_Get(doc, plane, "normal"), it->planeNormal, 3) ||
		    !ExtObst_Numbers(doc, ExtJson_Get(doc, plane, "ref"), it->planeRef, 3) || (entry < 0))
		{
			return "exit_plane without point, normal, ref or anim";
		}
		it->planeAnim = ExtObst_Anim(doc, entry, &why);
		if (it->planeAnim < 0)
		{
			return (why != NULL) ? why : "exit_plane: its node_anims entry";
		}
	}
	for (int nn = ExtJson_First(doc, nodes); (nn >= 0) && (it->nodeCount < EXT_OBST_NODES_MAX); nn = ExtJson_Next(doc, nn))
	{
		snprintf(it->nodes[it->nodeCount++], sizeof(it->nodes[0]), "%s", ExtJson_String(doc, nn, ""));
	}
	return NULL;
}

// The obstacles of a document into s_extObst; 0 = none (no section or refused, logged).
internal int ExtObst_Read(const ExtJsonDoc *doc, int section)
{
	const int items = ExtJson_Get(doc, section, "items");

	memset(&s_extObst, 0, sizeof(s_extObst));
	if ((doc == NULL) || (section < 0))
	{
		return 0;
	}
	if ((int)ExtJson_Number(doc, ExtJson_Get(doc, section, "version"), 0.0) != 1)
	{
		Platform_Log("[CTR ExtObstacle] REFUSED: obstacles version is not 1; no obstacle\n");
		return 0;
	}
	for (int n = ExtJson_First(doc, items); n >= 0; n = ExtJson_Next(doc, n))
	{
		const char *why;

		if (s_extObst.itemCount >= EXT_OBST_ITEMS_MAX)
		{
			Platform_Log("[CTR ExtObstacle] REFUSED: more than %d obstacles; no obstacle\n", EXT_OBST_ITEMS_MAX);
			memset(&s_extObst, 0, sizeof(s_extObst));
			return 0;
		}
		why = ExtObst_ReadItem(doc, n, &s_extObst.item[s_extObst.itemCount]);
		if (why != NULL)
		{
			Platform_Log("[CTR ExtObstacle] REFUSED: obstacle %d: %s; no obstacle\n", s_extObst.itemCount, why);
			memset(&s_extObst, 0, sizeof(s_extObst));
			return 0;
		}
		s_extObst.itemCount++;
	}
	s_extObst.ready = (s_extObst.itemCount > 0);
	return s_extObst.ready;
}

// How many of the item's nodes the drawing holds (registered), and how many of
// them pass the drawing's own view test with the drawn camera (cam may be NULL).
internal void ExtObst_NodeState(const ExtObstItem *it, const EngineCamera *cam, int *registered, int *drawable)
{
	*registered = 0;
	*drawable = 0;
	for (int i = 0; i < s_extDraw.nodeCount; i++)
	{
		const ExtDrawNode *n = &s_extDraw.nodes[i];

		for (int k = 0; k < it->nodeCount; k++)
		{
			if (strcmp(n->name, it->nodes[k]) == 0)
			{
				(*registered)++;
				if ((cam != NULL) && !n->sky && Engine_StaticBoxVisible(cam, n->boxMin, n->boxMax))
				{
					(*drawable)++;
				}
				break;
			}
		}
	}
}

void ExtObstacle_OnLevelLoaded(void)
{
	int registered = 0, drawable = 0;

	if (!ExtObst_Read(ExtTrack_Json(), ExtTrack_Section("obstacles")))
	{
		return;
	}
	for (int i = 0; i < s_extObst.itemCount; i++)
	{
		const ExtObstItem *it = &s_extObst.item[i];

		ExtObst_NodeState(it, NULL, &registered, &drawable);
		Platform_Log("[CTR ExtObstacle] %s: %d box(es), %d node(s) (%d drawn by the track), period %.1f s, damage %d\n", it->name, it->boxCount,
		             it->nodeCount, registered, (it->planeAnim >= 0) ? s_extObst.anim[it->planeAnim].period : 0.0f, it->damage);
	}
}

void ExtObstacle_OnRestart(void)
{
	memset(s_extObst.inside, 0, sizeof(s_extObst.inside));
	memset(s_extObst.hitFrame, 0, sizeof(s_extObst.hitFrame));
	for (int i = 0; i < s_extObst.itemCount; i++)
	{
		s_extObst.item[i].planeSideSet = 0;
	}
}

// --dev-ext-obstacle-place: seat 0 onto the first obstacle's path, where its
// front part is EXT_OBST_PLACE_AHEAD_FRAMES later, stopped (a measuring aid).
internal void ExtObst_Place(struct GameTracker *gGT, float t)
{
	const ExtObstItem *it = &s_extObst.item[0];
	struct Driver *d = gGT->drivers[0];
	float m[12];
	float p[3];

	if ((d == NULL) || (it->planeAnim < 0))
	{
		return;
	}
	ExtObst_Mat(&s_extObst.anim[it->planeAnim], t + (EXT_OBST_PLACE_AHEAD_FRAMES / 30.0f), m);
	ExtObst_Apply(m, it->planeRef, p);
	d->posCurr.x = (s32)lrintf(p[0] * 256.0f);
	d->posCurr.y = (s32)lrintf(p[1] * 256.0f);
	d->posCurr.z = (s32)lrintf(p[2] * 256.0f);
	d->posPrev = d->posCurr;
	d->velocity.x = 0;
	d->velocity.y = 0;
	d->velocity.z = 0;
	d->speed = 0;
	d->speedApprox = 0;
	Platform_Log("[CTR ExtObstacle] place seat 0 frame %d timer %d at %.0f %.0f %.0f (--dev-ext-obstacle-place)\n", s_extObst.frame, (int)gGT->timer, p[0],
	             p[1], p[2]);
}

void ExtObstacle_OnLogicFrame(void)
{
	struct GameTracker *gGT = sdata->gGT;
	float t;

	if (!s_extObst.ready || (gGT == NULL) || ((gGT->gameMode1 & (MAIN_MENU | LOADING)) != 0) || (sdata->Loading.stage != LOAD_IDLE))
	{
		return;
	}
	s_extObst.frame++;
	t = (float)gGT->timer / 30.0f; // the clock of the drawn node animations (Engine_FrameTime)
	if ((g_cfg_extObstaclePlace >= 0) && !s_extObst.placed && ((int)gGT->timer >= g_cfg_extObstaclePlace))
	{
		s_extObst.placed = 1;
		ExtObst_Place(gGT, t);
	}
	for (int i = 0; i < s_extObst.itemCount; i++)
	{
		ExtObstItem *it = &s_extObst.item[i];
		float mats[EXT_OBST_BOXES_MAX][12];

		for (int b = 0; b < it->boxCount; b++)
		{
			ExtObst_Mat(&s_extObst.anim[it->box[b].anim], t, mats[b]);
		}
		// the exit: the front part's point crosses the plane outward
		if (it->planeAnim >= 0)
		{
			float m[12], p[3], side;

			ExtObst_Mat(&s_extObst.anim[it->planeAnim], t, m);
			ExtObst_Apply(m, it->planeRef, p);
			side = ExtObst_Side(it, p);
			if (it->planeSideSet && (it->planeSide <= 0.0f) && (side > 0.0f))
			{
				const EngineCamera *cam = Engine_FrameCamera(0);
				const float lo[3] = {it->planePoint[0] - 1.0f, it->planePoint[1] - 1.0f, it->planePoint[2] - 1.0f};
				const float hi[3] = {it->planePoint[0] + 1.0f, it->planePoint[1] + 1.0f, it->planePoint[2] + 1.0f};
				const float period = s_extObst.anim[it->planeAnim].period;
				int registered = 0, drawable = 0;

				ExtObst_NodeState(it, cam, &registered, &drawable);
				it->exits++;
				Platform_Log("[CTR ExtObstacle] exit %s frame %d timer %d loop %.3f s view %d nodes drawable %d/%d (exit %d)\n", it->name, s_extObst.frame,
				             (int)gGT->timer, (period > 0.0f) ? fmodf(t, period) : t, ((cam != NULL) && Engine_StaticBoxVisible(cam, lo, hi)) ? 1 : 0,
				             drawable, registered, it->exits);
			}
			it->planeSide = side;
			it->planeSideSet = 1;
		}
		// one frame after a hit: the state the hit left (a bot's damage state shows
		// in kartState only on its next tick)
		for (int seat = 0; seat < EXT_OBST_SEATS; seat++)
		{
			const struct Driver *d = gGT->drivers[seat];

			if ((s_extObst.hitFrame[i][seat] != 0) && (s_extObst.hitFrame[i][seat] == s_extObst.frame - 1) && (d != NULL))
			{
				Platform_Log("[CTR ExtObstacle] hit+1 %s seat %d frame %d timer %d kartState %d botDamage %d\n", it->name, seat, s_extObst.frame, (int)gGT->timer,
				             (int)d->kartState, ((d->actionsFlagSet & ACTION_BOT) != 0) ? (((d->botData.botFlags & BOT_FLAG_DAMAGE_ACTIVE) != 0) ? 1 : 0) : -1);
				s_extObst.hitFrame[i][seat] = 0;
			}
		}
		// the hits
		for (int seat = 0; seat < EXT_OBST_SEATS; seat++)
		{
			struct Driver *d = gGT->drivers[seat];
			float q[3];
			int box = -1;

			if ((d == NULL) || (d->kartState == KS_MASK_GRABBED))
			{
				s_extObst.inside[i][seat] = 0;
				continue;
			}
			q[0] = d->posCurr.x / 256.0f;
			q[1] = (d->posCurr.y / 256.0f) + EXT_OBST_KART_MIDDLE;
			q[2] = d->posCurr.z / 256.0f;
			for (int b = 0; (b < it->boxCount) && (box < 0); b++)
			{
				if (ExtObst_InBox(&it->box[b], mats[b], q, it->kartRadius))
				{
					box = b;
				}
			}
			if (box < 0)
			{
				s_extObst.inside[i][seat] = 0;
				continue;
			}
			if (!s_extObst.inside[i][seat])
			{
				const int before = d->kartState;
				const int result = RB_Hazard_HurtDriver(d, it->damage, NULL, 0);

				s_extObst.inside[i][seat] = 1;
				s_extObst.hitFrame[i][seat] = s_extObst.frame;
				it->hits++;
				Platform_Log("[CTR ExtObstacle] hit %s seat %d frame %d timer %d box %d (%s) kartState %d -> %d result %d\n", it->name, seat, s_extObst.frame,
				             (int)gGT->timer, box, it->box[box].phantom, before, (int)d->kartState, result);
			}
		}
	}
}

// ---------------------------------------------------------------------------
// --ext-selftest obstacle: a made-up section on a made-up node_anims list, no
// window, no game data

internal void ExtObst_TestCheck(int ok, const char *what, int *n, int *failures)
{
	(*n)++;
	if (!ok)
	{
		(*failures)++;
		printf("ext selftest obstacle: FAILED %s\n", what);
	}
}

int ExtObstacle_Selftest(void)
{
	// part 0 moves +x by 1000 units per second for 4 s (period 4), part 1 stands still
	static const char json[] =
	    "{\"node_anims\": [{\"times\": [0, 4], \"period\": 4, \"matrices\": [[1,0,0,0, 0,1,0,0, 0,0,1,0], [1,0,0,4000, 0,1,0,0, 0,0,1,0]]},"
	    " {\"times\": [0, 1], \"period\": 0, \"matrices\": [[1,0,0,0, 0,1,0,0, 0,0,1,0], [1,0,0,0, 0,1,0,0, 0,0,1,0]]}],"
	    " \"obstacles\": {\"version\": 1, \"items\": [{\"name\": \"t\", \"kind\": \"animated_boxes\", \"clock\": \"timer/30\", \"period\": 4,"
	    " \"damage\": 3, \"kart_radius\": 25, \"nodes\": [\"uvna_obst_t_0_m000_a\"],"
	    " \"boxes\": [{\"anim\": 0, \"center\": [0, 100, 0], \"half\": [500, 100, 200], \"axes\": [[1,0,0],[0,1,0],[0,0,1]], \"phantom\": \"p0\"},"
	    "            {\"anim\": 1, \"center\": [0, 100, 5000], \"half\": [100, 100, 100], \"axes\": [[0,0,-1],[0,1,0],[1,0,0]], \"phantom\": \"p1\"}],"
	    " \"exit_plane\": {\"point\": [2000, 0, 0], \"normal\": [1, 0, 0], \"anim\": 0, \"ref\": [500, 0, 0]}}]}}";
	static const char bad[] = "{\"node_anims\": [], \"obstacles\": {\"version\": 1, \"items\": [{\"name\": \"t\", \"kind\": \"animated_boxes\", \"clock\": "
	                          "\"timer/30\", \"damage\": 3, \"boxes\": [{\"anim\": 0, \"center\": [0,0,0], \"half\": [1,1,1], \"axes\": [[1,0,0],[0,1,0],[0,0,1]]}]}]}}";
	ExtJsonDoc doc;
	char why[160];
	int n = 0, failures = 0;
	float m[12], p[3];

	ExtObst_TestCheck(ExtJson_Parse(&doc, json, sizeof(json) - 1, why, (int)sizeof(why)) == 1, "the made-up document parses", &n, &failures);
	ExtObst_TestCheck(ExtObst_Read(&doc, ExtJson_Get(&doc, 0, "obstacles")) == 1, "the made-up section reads", &n, &failures);
	ExtObst_TestCheck((s_extObst.itemCount == 1) && (s_extObst.item[0].boxCount == 2) && (s_extObst.animCount == 2) && (s_extObst.item[0].damage == 3) &&
	                      (s_extObst.item[0].planeAnim == 0) && (s_extObst.item[0].nodeCount == 1),
	                  "1 item, 2 boxes on 2 animations (the plane shares part 0's), damage 3, 1 node", &n, &failures);
	{
		const ExtObstItem *it = &s_extObst.item[0];
		const float in[3] = {1000.0f, 150.0f, 0.0f}; // part 0 at t = 1 s: centre (1000, 100, 0)
		const float edge[3] = {1520.0f, 150.0f, 0.0f}; // 20 past its end, within the kart radius 25
		const float out[3] = {1530.0f, 150.0f, 0.0f};
		const float still[3] = {0.0f, 150.0f, 5180.0f}; // part 1: rotated axes, half 100 along each
		const float stillIn[3] = {0.0f, 150.0f, 5120.0f};

		ExtObst_Mat(&s_extObst.anim[0], 1.0f, m);
		ExtObst_TestCheck(fabsf(m[3] - 1000.0f) < 0.01f, "part 0 at 1 s: moved 1000 along x (ExtTrack_MatAt, linear between keys)", &n, &failures);
		ExtObst_TestCheck(ExtObst_InBox(&it->box[0], m, in, 25.0f) && ExtObst_InBox(&it->box[0], m, edge, 25.0f) && !ExtObst_InBox(&it->box[0], m, out, 25.0f),
		                  "the moving box: inside, inside within the kart radius, outside past it", &n, &failures);
		ExtObst_Mat(&s_extObst.anim[0], 5.0f, m);
		ExtObst_TestCheck(fabsf(m[3] - 1000.0f) < 0.01f, "the period repeats: 5 s = 1 s", &n, &failures);
		ExtObst_Mat(&s_extObst.anim[1], 0.5f, m);
		ExtObst_TestCheck(!ExtObst_InBox(&it->box[1], m, still, 25.0f) && ExtObst_InBox(&it->box[1], m, stillIn, 25.0f),
		                  "the still box with turned axes: 180 off is out, 120 off is in (half 100 + 25)", &n, &failures);
		// the exit: ref 500 moved by part 0 crosses x = 2000 at t = 1.5 s
		ExtObst_Mat(&s_extObst.anim[0], 1.4f, m);
		ExtObst_Apply(m, it->planeRef, p);
		ExtObst_TestCheck(ExtObst_Side(it, p) < 0.0f, "the front part before the plane at 1.4 s", &n, &failures);
		ExtObst_Mat(&s_extObst.anim[0], 1.6f, m);
		ExtObst_Apply(m, it->planeRef, p);
		ExtObst_TestCheck(ExtObst_Side(it, p) > 0.0f, "the front part past the plane at 1.6 s", &n, &failures);
	}
	ExtJson_Free(&doc);
	ExtObst_TestCheck((ExtJson_Parse(&doc, bad, sizeof(bad) - 1, why, (int)sizeof(why)) == 1) && !ExtObst_Read(&doc, ExtJson_Get(&doc, 0, "obstacles")) &&
	                      !s_extObst.ready,
	                  "a box whose node_anims entry is missing: refused, no obstacle", &n, &failures);
	ExtJson_Free(&doc);
	ExtObst_TestCheck(!ExtObst_Read(NULL, -1) && !s_extObst.ready, "no section: no obstacle", &n, &failures);
	memset(&s_extObst, 0, sizeof(s_extObst));
	if (failures)
	{
		printf("ext selftest obstacle FAILED: %d checks, %d failures\n", n, failures);
		return 1;
	}
	printf("ext selftest obstacle passed: %d checks, 0 failures\n", n);
	return 0;
}
