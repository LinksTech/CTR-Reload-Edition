// rldpack_anim.inc - char-poses: the poses of a character, checked against the
// model and placed beside it for the 3D view of Reload Studio
//
// Included by tools/rldpack.c after tools/rldpack_char.inc: it uses the PLY
// reader and the chain of make-char and changes neither.
//
// A PREVIEW, NOT A BUILD. A driver has four animations in the game: turn (21
// frames), reverse (7), bump (15) and jump (4); make-char computes all 47
// frames itself from the model (the automatic poses). Later an author will
// give poses of their own instead: the same mesh exported once more per pose,
// only its vertices moved. This command is the first step of that: it checks
// such files against the model and places them where the model stands in the
// view. It never builds a character and refuses --out - no pose reaches a
// container yet, and make-char knows nothing of them.
//
// THE POSES: one PLY per pose in one folder (not its subfolders), named after
// the pose, upper or lower case alike:
//   turn_left.ply    turn, frame 0: full lock to the left
//   turn_right.ply   turn, frame 20: full lock to the right
//   reverse.ply      reverse, frame 6: the pose it ends in
//   bump.ply         bump, the pose held through its 15 frames
//   jump.ply         jump, frame 3: the pose it ends in
//   idle.ply         no animation of the game has it: checked, not used
// Any other .ply is named once (pose-unknown) and left alone; the model file
// itself, when it lies in the folder, silently.
//
// THE RULES are checked on the PLY data as read - before welding, repair and
// reduction, which change the indices. The first one a file breaks decides:
//   pose-read          the file cannot be opened or read as a PLY
//   pose-vertex-count  another number of vertices than the model
//   pose-face-count    another number of faces than the model
//   pose-faces         a face with other corners (number, indices, order)
//   pose-split         vertices the model welds into one point lie apart
//                      (more than RLDANIM_JOINED units of the PLY further
//                      apart than in the model)
// and then, without refusing the pose:
//   pose-kart-moved    the kart moved: it stays as in the model (warning)
//   pose-far           a vertex moved more than RLDANIM_FAR units (warning)
//   pose-colors        the colors differ: the model's are used (note)
// Over all poses: only one of turn_left and turn_right is usable -> pose-turn-
// pair (warning), the turn stays automatic; idle -> pose-idle-unused (note).
//
// THE PLACE: the model is built as make-char builds it, with the same
// switches, and the transformation of that build is applied to every pose: the
// axes (--up, --forward), the factor of --fit (or --scale), the shift onto the
// reference dummy and --size about the seat. A pose is never fitted on its
// own - a bent pose would be scaled differently. The kart keeps the model's
// place in every pose, as it does in every frame of the game. The model's own
// build is quiet: its findings are those of make-char --check.
//
// THE FILE (--preview), little endian:
//   "RLDPS1\0\0"               8 bytes
//   u32 count                  RLDANIM_POSE_COUNT = 6, always all six in the
//                              order turn_left, turn_right, reverse, bump,
//                              jump, idle
//   per pose: u32 triangles    0 for a pose that is missing or refused
//     per triangle 3 corners of
//       s16 x, s16 y, s16 z    whole game units in the model space of RLDPV1
//                              (make-char --preview): +Y up, +Z forward, +X the
//                              driver's left
//       u8 r, u8 g, u8 b       the model's vertex color
//       u8 pad                 1: drawn from both sides - the poses are shown
//                              before the repair turns faces outward
// The triangles are the model's faces as read, split into triangles as
// make-char splits them (before repair and reduction): the same list in every
// pose, the corners in the order of the PLY.
//
// MACHINE LINES (protocol 1, tools/reloadstudio/reloadstudio.h):
//   @rldpack  1 char-poses
//   @value    pose-dir <folder> switch, then size, fit, wheels, repair, up and
//             forward as make-char says them
//   @file     ply ok|missing <model> <bytes>
//   @msg      the pose-* above, pose-model, pose-remesh, pose-dir, pose-folder,
//             usage, and the ply-* of the reader for the model (a pose that
//             cannot be read gets one pose-read with the reader's finding)
//   @char     pose <name> <state> <file> <rule>   one per pose, in the order
//             of the file; state ok | missing | bad | unused (idle) |
//             automatic (a turn pose without its partner); file "" when
//             missing; rule the id that decided, or "-"
//   @char     pose-unknown <file>                 per .ply that is no pose
//   @char     poses <found> <shown>               files of a pose name, and
//             the poses the preview shows (ok, unused, automatic)
//   @file     preview ok|failed <file> <bytes>
//   @end      <exit code>: 0 = no error, 1 = an error (a pose, the model or
//             the folder), 2 = the command line
// There is no @result line: nothing is built.
//
// Deterministic as the rest of rldpack: no time, no random numbers, files in
// the order of their names, the arithmetic of make-char (RldMk_Axes,
// RldMk_Round, RldMk_Sqrt).

#define RLDANIM_POSE_COUNT 6
#define RLDANIM_FILES_MAX 64      // .ply files of the folder that are looked at
#define RLDANIM_JOINED 0.001      // PLY units: pose-split and pose-kart-moved
#define RLDANIM_FAR 2.0           // PLY units: pose-far (128 game units at scale 1)
#define RLDANIM_CORNER_BYTES 10u  // as RLDMK_PREVIEW_CORNER_BYTES

enum
{
	RLDANIM_TURN_LEFT = 0,
	RLDANIM_TURN_RIGHT,
	RLDANIM_REVERSE,
	RLDANIM_BUMP,
	RLDANIM_JUMP,
	RLDANIM_IDLE
};

enum
{
	RLDANIM_MISSING = 0,
	RLDANIM_OK,
	RLDANIM_BAD,
	RLDANIM_UNUSED,
	RLDANIM_AUTOMATIC,
	RLDANIM_STATES
};

static const char *const s_rldAnimNames[RLDANIM_POSE_COUNT] = {"turn_left", "turn_right", "reverse", "bump", "jump", "idle"};
static const char *const s_rldAnimStates[RLDANIM_STATES] = {"missing", "ok", "bad", "unused", "automatic"};

// The model as make-char builds it, and what the poses need of that build.
struct RldAnimModel
{
	const struct RldMkPly *ply;   // as read
	struct RldMkOptions opt;      // the switches, with the scale the build used
	struct RldMkBuild build;      // with the map of --diagnose: diagPlyPosition, diagPositionGroup
	int built;                    // the build went through: the poses can be placed
	double shift[3];              // onto the reference dummy (0 without)
	double pivot[3];              // --size scales about this point
	double size;                  // --size as a factor; 1 = nothing is scaled
	u32 *joined;                  // per PLY vertex: the first vertex welded with it (itself when none)
	u32 triangleCount;            // the faces as read, split into triangles (RldMk_StepTriangles)
	u32 *triangle;                // 3 PLY vertices each
};

// One pose: its file and what the rules said.
struct RldAnimPose
{
	int state;                    // RLDANIM_MISSING ...
	char file[RLD_NAME_MAX];      // the file name in the folder, "" when missing
	const char *rule;             // the id that decided, "-" for none
	struct RldMkPly ply;          // as read; empty when missing or unreadable
};

// The pose a file name stands for: 0..5, -1 for none. Only .ply files count.
static int RldAnim_PoseIndex(const char *name)
{
	char lower[RLD_NAME_MAX];
	size_t length = strlen(name);
	size_t i;
	int p;

	if ((length <= 4u) || (length >= sizeof(lower)) || !Rld_EndsWithNoCase(name, ".ply"))
	{
		return -1;
	}

	length -= 4u;
	for (i = 0; i < length; i++)
	{
		const char c = name[i];

		lower[i] = ((c >= 'A') && (c <= 'Z')) ? (char)(c - 'A' + 'a') : c;
	}
	lower[length] = '\0';

	for (p = 0; p < RLDANIM_POSE_COUNT; p++)
	{
		if (strcmp(lower, s_rldAnimNames[p]) == 0)
		{
			return p;
		}
	}

	return -1;
}

// One character without regard to case, '\' as '/'.
static char RldAnim_Folded(char c)
{
	return ((c >= 'A') && (c <= 'Z')) ? (char)(c - 'A' + 'a') : ((c == '\\') ? '/' : c);
}

// Two paths of the same file as far as the text tells: case and the kind of
// separator do not count.
static int RldAnim_SamePath(const char *a, const char *b)
{
	for (;; a++, b++)
	{
		const char x = RldAnim_Folded(*a);

		if (x != RldAnim_Folded(*b))
		{
			return 0;
		}
		if (x == '\0')
		{
			return 1;
		}
	}
}

// The last code said since `said` (an index into s_rldMkCodes), for a message
// that names why; "not determined" when none.
static void RldAnim_LastCode(size_t said, char *out, size_t size)
{
	const char *codes = &s_rldMkCodes[said];
	const char *last = strrchr(codes, ' ');

	last = (last != NULL) ? (last + 1) : codes;
	snprintf(out, size, "%s", (last[0] != '\0') ? last : "not determined");
}

static void RldAnim_FreeModel(struct RldAnimModel *m)
{
	RldMk_FreeBuild(&m->build);
	free(m->joined);
	free(m->triangle);
	memset(m, 0, sizeof(*m));
}

// The group of a PLY vertex in the model's build (RLDMK_GROUP_*), -1 when no
// triangle uses it or the model did not build.
static int RldAnim_Group(const struct RldAnimModel *m, u32 vertex)
{
	u32 p;

	if (!m->built)
	{
		return -1;
	}
	p = m->build.diagPlyPosition[vertex];
	return ((p == RLDMK_NONE) || (p >= m->build.diagPositionCount)) ? -1 : (int)m->build.diagPositionGroup[p];
}

// Builds the model quietly, as make-char would with `opt`, and keeps what the
// poses need. 1 = the poses can be placed; 0 = the model does not build (the
// vertex and face rules still work), *why names the last finding.
static int RldAnim_PrepareModel(struct RldAnimModel *m, const struct RldMkPly *ply, const struct RldMkOptions *opt, char *why, size_t whySize)
{
	const int quiet = s_rldMkQuiet;
	const int errors = s_rldMkErrors;
	const size_t said = strlen(s_rldMkCodes);
	struct RldMkOptions at = *opt;
	struct RldMkBuild probe;
	u32 *first;
	u32 i;
	int a;

	memset(m, 0, sizeof(*m));
	m->ply = ply;
	m->size = 1.0;
	why[0] = '\0';

	// --diagnose keeps the map from PLY vertex to welded position and the
	// groups (RldMk_DiagKeep); the limits it lifts move nothing.
	at.diagnose = 1;
	s_rldMkQuiet = 1;
	m->built = RldMk_BuildModel(ply, &at, &m->build) && (m->build.diagPlyPosition != NULL) && (m->build.diagPositionGroup != NULL);
	if (!m->built)
	{
		RldAnim_LastCode(said, why, whySize);
	}
	else
	{
		// The transformation of the build (RldMk_BuildOnce): the scale --fit
		// found, the dummy's shift, --size about the pivot of RldMk_SizeHull.
		m->opt = *opt;
		m->opt.scale = m->build.fitted ? m->build.fitFactor : opt->scale;
		for (a = 0; a < 3; a++)
		{
			m->shift[a] = m->build.dummy ? m->build.dumFit.shift[a] : 0.0;
			m->pivot[a] = m->build.dummy ? m->build.dumFit100.seat[a] : ((a == 1) ? RLDMK_HIP_Y : 0.0);
		}
		if ((opt->sizePercent != 0u) && (opt->sizePercent != 100u))
		{
			m->size = (double)opt->sizePercent / 100.0;
		}

		// The faces split as step 2 splits them, before the repair: the
		// triangles of the preview, the same for every pose.
		memset(&probe, 0, sizeof(probe));
		if (RldMk_FitProbe(ply, &m->opt, &probe, 0))
		{
			m->triangleCount = probe.triangleCount;
			m->triangle = probe.triangle;
			probe.triangle = NULL;
		}
		else
		{
			m->built = 0;
			RldAnim_LastCode(said, why, whySize);
		}
		RldMk_FreeBuild(&probe);
	}
	s_rldMkQuiet = quiet;
	s_rldMkErrors = errors;
	s_rldMkCodes[said] = '\0';

	if (!m->built)
	{
		return 0;
	}

	// Vertices welded into one point: the first of each position stands for it.
	m->joined = (u32 *)malloc(((size_t)ply->vertexCount + 1u) * sizeof(u32));
	first = (u32 *)malloc(((size_t)m->build.diagPositionCount + 1u) * sizeof(u32));
	if ((m->joined == NULL) || (first == NULL))
	{
		free(first);
		m->built = 0;
		snprintf(why, whySize, "memory");
		return 0;
	}
	for (i = 0; i < m->build.diagPositionCount; i++)
	{
		first[i] = RLDMK_NONE;
	}
	for (i = 0; i < ply->vertexCount; i++)
	{
		const u32 p = m->build.diagPlyPosition[i];

		m->joined[i] = i;
		if ((p != RLDMK_NONE) && (p < m->build.diagPositionCount))
		{
			if (first[p] == RLDMK_NONE)
			{
				first[p] = i;
			}
			m->joined[i] = first[p];
		}
	}
	free(first);
	return 1;
}

// One PLY point -> the model space of the build, game units: the grid of the
// welding, the axes and scale, the dummy's shift, and --size for the driver
// and the steering wheel.
static void RldAnim_Place(const struct RldAnimModel *m, const double xyz[3], int group, double out[3])
{
	double grid[3];
	int a;

	for (a = 0; a < 3; a++)
	{
		grid[a] = (double)RldMk_Round(xyz[a] * 100000.0) / 100000.0;
	}
	RldMk_Axes(&m->opt, grid, out);
	for (a = 0; a < 3; a++)
	{
		out[a] += m->shift[a];
	}
	if ((m->size != 1.0) && (group != RLDMK_GROUP_KART))
	{
		for (a = 0; a < 3; a++)
		{
			out[a] = m->pivot[a] + m->size * (out[a] - m->pivot[a]);
		}
	}
}

// Every vertex of a pose in the model space, 3 per vertex; the kart takes the
// model's place. NULL = out of memory.
static double *RldAnim_PlacePose(const struct RldAnimModel *m, const struct RldMkPly *pose)
{
	const struct RldMkPly *model = m->ply;
	double *out = (double *)malloc(((size_t)model->vertexCount + 1u) * 3u * sizeof(double));
	u32 i;

	if (out == NULL)
	{
		return NULL;
	}

	for (i = 0; i < model->vertexCount; i++)
	{
		const int group = RldAnim_Group(m, i);
		const struct RldMkPly *from = (group == RLDMK_GROUP_KART) ? model : pose;

		RldAnim_Place(m, &from->xyz[3u * i], group, &out[3u * i]);
	}

	return out;
}

// The corners of face f, n of them.
static const u32 *RldAnim_FaceCorners(const struct RldMkPly *ply, u32 f, u32 *n)
{
	*n = ply->cornerCount[f];
	return (*n > 4u) ? &ply->more[ply->moreStart[f]] : &ply->corners[4u * f];
}

// "a b c" (at most five, then "...") for the text of pose-faces.
static void RldAnim_CornerText(const u32 *c, u32 n, char *out, size_t size)
{
	size_t used = 0;
	u32 k;

	out[0] = '\0';
	for (k = 0; (k < n) && (k < 5u) && (used < size); k++)
	{
		used += (size_t)snprintf(&out[used], size - used, "%s%u", (k != 0u) ? " " : "", c[k]);
	}
	if ((n > 5u) && (used < size))
	{
		snprintf(&out[used], size - used, " ...");
	}
}

// The rules on one pose that was read. Returns RLDANIM_OK or RLDANIM_BAD;
// *rule gets the id that decided ("-" when none). Says its findings.
static int RldAnim_CheckPose(const struct RldAnimModel *m, const struct RldMkPly *pose, const char *file, const char **rule)
{
	const struct RldMkPly *model = m->ply;
	u32 f;
	u32 i;

	*rule = "-";
	if (pose->vertexCount != model->vertexCount)
	{
		*rule = "pose-vertex-count";
		RldMk_Say("error", *rule, file,
		          "%s has %u vertices, the model has %u. A pose must be the model's own mesh with moved vertices: the same vertices in the same order.", file,
		          pose->vertexCount, model->vertexCount);
		return RLDANIM_BAD;
	}

	if (pose->faceCount != model->faceCount)
	{
		*rule = "pose-face-count";
		RldMk_Say("error", *rule, file, "%s has %u faces, the model has %u. A pose must keep every face of the model.", file, pose->faceCount,
		          model->faceCount);
		return RLDANIM_BAD;
	}

	for (f = 0; f < model->faceCount; f++)
	{
		u32 n;
		u32 poseN;
		u32 k;
		const u32 *c = RldAnim_FaceCorners(model, f, &n);
		const u32 *poseC = RldAnim_FaceCorners(pose, f, &poseN);
		int same = (n == poseN);

		for (k = 0; same && (k < n); k++)
		{
			same = (c[k] == poseC[k]);
		}
		if (!same)
		{
			char got[64];
			char want[64];

			RldAnim_CornerText(poseC, poseN, got, sizeof(got));
			RldAnim_CornerText(c, n, want, sizeof(want));
			*rule = "pose-faces";
			RldMk_Say("error", *rule, file,
			          "Face %u of %s uses the vertices %s, face %u of the model uses %s. A pose must keep the faces exactly as in the model - pose the mesh, do not edit it.",
			          f, file, got, f, want);
			return RLDANIM_BAD;
		}
	}

	// Welded in the model, apart in the pose. Without a build there is no
	// welding to compare with: the rule waits for the model.
	for (i = 0; m->built && (i < model->vertexCount); i++)
	{
		const u32 j = m->joined[i];

		if (j != i)
		{
			const double inModel = RldMk_Sqrt(RldMk_Distance2(&model->xyz[3u * i], &model->xyz[3u * j]));
			const double inPose = RldMk_Sqrt(RldMk_Distance2(&pose->xyz[3u * i], &pose->xyz[3u * j]));

			if (inPose > (inModel + RLDANIM_JOINED))
			{
				*rule = "pose-split";
				RldMk_Say("error", *rule, file,
				          "Vertices %u and %u are one point in the model but %.3f units apart in %s. Points that are joined in the model must stay together in every pose.",
				          j, i, inPose, file);
				return RLDANIM_BAD;
			}
		}
	}

	// Usable. What it moved: the kart, and how far at most.
	if (m->built)
	{
		double kartMost = 0.0;
		double most = 0.0;
		u32 mostVertex = 0;

		for (i = 0; i < model->vertexCount; i++)
		{
			const int group = RldAnim_Group(m, i);
			const double d = RldMk_Sqrt(RldMk_Distance2(&model->xyz[3u * i], &pose->xyz[3u * i]));

			if (group < 0)
			{
				continue;
			}
			if ((group == RLDMK_GROUP_KART) && (d > kartMost))
			{
				kartMost = d;
			}
			if (d > most)
			{
				most = d;
				mostVertex = i;
			}
		}

		if (kartMost > RLDANIM_JOINED)
		{
			RldMk_Say("warning", "pose-kart-moved", file,
			          "The kart moves in %s by up to %.3f units. The kart stays as in the model in every frame; only the driver and the steering wheel take the pose.",
			          file, kartMost);
		}
		if (most > RLDANIM_FAR)
		{
			RldMk_Say("warning", "pose-far", file, "%s moves vertex %u by %.3f units, more than %.3f. Check the axes and the scale of the export.", file,
			          mostVertex, most, RLDANIM_FAR);
		}
	}

	if (pose->hasColors && model->hasColors && (memcmp(pose->rgb, model->rgb, (size_t)model->vertexCount * 3u) != 0))
	{
		RldMk_Say("note", "pose-colors", file, "The colors of %s differ from the model and are ignored: every pose uses the model's colors.", file);
	}

	return RLDANIM_OK;
}

// The states over all poses: a turn pose needs its partner, idle is not used.
static void RldAnim_Settle(struct RldAnimPose poses[RLDANIM_POSE_COUNT])
{
	const int left = (poses[RLDANIM_TURN_LEFT].state == RLDANIM_OK);
	const int right = (poses[RLDANIM_TURN_RIGHT].state == RLDANIM_OK);

	if (left != right)
	{
		struct RldAnimPose *alone = &poses[left ? RLDANIM_TURN_LEFT : RLDANIM_TURN_RIGHT];

		alone->state = RLDANIM_AUTOMATIC;
		alone->rule = "pose-turn-pair";
		RldMk_Say("warning", "pose-turn-pair", alone->file,
		          "%s has no partner %s.ply: steering uses the automatic poses. Steering needs both turn_left.ply and turn_right.ply.", alone->file,
		          s_rldAnimNames[left ? RLDANIM_TURN_RIGHT : RLDANIM_TURN_LEFT]);
	}

	if (poses[RLDANIM_IDLE].state == RLDANIM_OK)
	{
		poses[RLDANIM_IDLE].state = RLDANIM_UNUSED;
		poses[RLDANIM_IDLE].rule = "pose-idle-unused";
		RldMk_Say("note", "pose-idle-unused", poses[RLDANIM_IDLE].file, "idle.ply was checked but is not used yet: the game has no idle animation.");
	}
}

// A pose the preview draws.
static int RldAnim_Shown(int state)
{
	return (state == RLDANIM_OK) || (state == RLDANIM_UNUSED) || (state == RLDANIM_AUTOMATIC);
}

// The preview file in memory (the format at the head of this file). NULL =
// out of memory, or the model did not build.
static u8 *RldAnim_Preview(const struct RldAnimModel *m, const struct RldAnimPose poses[RLDANIM_POSE_COUNT], size_t *sizeOut)
{
	size_t size = 12u;
	size_t at = 12u;
	u8 *out;
	int p;

	*sizeOut = 0;
	if (!m->built)
	{
		return NULL;
	}

	for (p = 0; p < RLDANIM_POSE_COUNT; p++)
	{
		size += 4u + (RldAnim_Shown(poses[p].state) ? (size_t)m->triangleCount * 3u * RLDANIM_CORNER_BYTES : 0u);
	}

	out = (u8 *)calloc(1, size);
	if (out == NULL)
	{
		return NULL;
	}
	memcpy(out, "RLDPS1\0\0", 8u);
	Rld_WriteLE32(&out[8], RLDANIM_POSE_COUNT);

	for (p = 0; p < RLDANIM_POSE_COUNT; p++)
	{
		double *placed;
		u32 t;

		if (!RldAnim_Shown(poses[p].state))
		{
			Rld_WriteLE32(&out[at], 0u);
			at += 4u;
			continue;
		}

		placed = RldAnim_PlacePose(m, &poses[p].ply);
		if (placed == NULL)
		{
			free(out);
			return NULL;
		}

		Rld_WriteLE32(&out[at], m->triangleCount);
		at += 4u;
		for (t = 0; t < (3u * m->triangleCount); t++)
		{
			const u32 v = m->triangle[t];
			int a;

			for (a = 0; a < 3; a++)
			{
				s64 unit = RldMk_Round(placed[3u * v + (u32)a]);

				unit = (unit < -32768) ? -32768 : ((unit > 32767) ? 32767 : unit);
				Rld_WriteLE16(&out[at + 2u * (u32)a], (u32)unit & 0xFFFFu);
			}
			memcpy(&out[at + 6u], &m->ply->rgb[3u * v], 3u);
			out[at + 9u] = 1u;
			at += RLDANIM_CORNER_BYTES;
		}
		free(placed);
	}

	*sizeOut = size;
	return out;
}

// The .ply files of the pose folder, sorted by name without regard to case
// (then as written), the first RLDANIM_FILES_MAX of them.
struct RldAnimFolder
{
	char names[RLDANIM_FILES_MAX][RLD_NAME_MAX];
	u32 count;
	u32 seen;
};

static int RldAnim_CompareName(const char *a, const char *b)
{
	const char *x = a;
	const char *y = b;

	for (;; x++, y++)
	{
		const char cx = ((*x >= 'A') && (*x <= 'Z')) ? (char)(*x - 'A' + 'a') : *x;
		const char cy = ((*y >= 'A') && (*y <= 'Z')) ? (char)(*y - 'A' + 'a') : *y;

		if (cx != cy)
		{
			return ((unsigned char)cx < (unsigned char)cy) ? -1 : 1;
		}
		if (cx == '\0')
		{
			return strcmp(a, b);
		}
	}
}

static void RldAnim_FolderVisit(void *context, const char *name)
{
	struct RldAnimFolder *folder = (struct RldAnimFolder *)context;
	u32 at;

	if (!Rld_EndsWithNoCase(name, ".ply") || (strlen(name) >= RLD_NAME_MAX))
	{
		return;
	}

	folder->seen++;
	at = folder->count;
	while ((at > 0u) && (RldAnim_CompareName(name, folder->names[at - 1u]) < 0))
	{
		at--;
	}
	if (at >= RLDANIM_FILES_MAX)
	{
		return;
	}
	if (folder->count < RLDANIM_FILES_MAX)
	{
		folder->count++;
	}
	memmove(&folder->names[at + 1u], &folder->names[at], (size_t)(folder->count - 1u - at) * sizeof(folder->names[0]));
	snprintf(folder->names[at], sizeof(folder->names[at]), "%s", name);
}

// dir and file joined, every separator the system's own (the folder may be
// given with '/' on Windows).
static void RldAnim_JoinPath(char *out, size_t size, const char *dir, const char *file)
{
	size_t i;

	snprintf(out, size, "%s" RLD_SEP "%s", dir, file);
	for (i = 0; out[i] != '\0'; i++)
	{
		if ((out[i] == '/') || (out[i] == '\\'))
		{
			out[i] = RLD_SEP[0];
		}
	}
}

// What the PLY reader found (its code), worded for a pose file.
static const char *RldAnim_ReaderText(const char *code)
{
	static const char *const texts[][2] = {
		{"ply-format", "this is not a PLY file rldpack can read. Export the pose as the model is exported (PLY, ASCII or binary)."},
		{"ply-truncated", "the file ends early - it was not saved completely."},
		{"ply-index", "a face points to a vertex the file does not have."},
		{"ply-position", "the file has no vertex positions (x, y, z)."},
		{"ply-no-faces", "the file has no faces. Export the mesh, not only its points."},
		{"ply-big", "the file has more vertices or faces than rldpack reads."},
		{"memory", "rldpack ran out of memory while reading it."},
	};
	int i;

	for (i = 0; i < RLD_ARRAY_COUNT(texts); i++)
	{
		if (strcmp(code, texts[i][0]) == 0)
		{
			return texts[i][1];
		}
	}
	return "the PLY reader cannot read it. Export the pose as the model is exported.";
}

// Reads one pose file. 1 = read; 0 = said why (pose-read).
static int RldAnim_ReadPose(const char *dir, const char *file, struct RldMkPly *ply)
{
	char path[RLD_PATH_MAX + RLD_NAME_MAX];
	u8 *data = NULL;
	size_t size = 0;
	int read;

	memset(ply, 0, sizeof(*ply));
	RldAnim_JoinPath(path, sizeof(path), dir, file);
	if (!Rld_ReadFile(path, &data, &size))
	{
		RldMk_Say("error", "pose-read", path, "%s: the file cannot be opened.", file);
		return 0;
	}

	// Quietly: the reader's findings are worded for the model file. The one
	// that stopped it is said here once, for this file (pose-read).
	{
		const int quiet = s_rldMkQuiet;
		const int errors = s_rldMkErrors;
		const size_t said = strlen(s_rldMkCodes);
		char code[64];

		s_rldMkQuiet = 1;
		read = RldMk_ParsePly(data, size, ply);
		s_rldMkQuiet = quiet;
		if (!read)
		{
			RldMk_FreePly(ply);
			RldAnim_LastCode(said, code, sizeof(code));
			s_rldMkCodes[said] = '\0';   // only pose-read counts, as one error
			s_rldMkErrors = errors;
			RldMk_Say("error", "pose-read", code, "%s: %s", file, RldAnim_ReaderText(code));
		}
	}
	free(data);
	return read;
}

//========================================================================================
// THE COMMAND: rldpack char-poses
//========================================================================================

static int RldAnim_PosesCommand(int argc, char **argv)
{
	static struct RldAnimFolder folder;
	static struct RldAnimPose poses[RLDANIM_POSE_COUNT];
	static char why[160];
	const char *modelArg = NULL;
	const char *dirArg = NULL;
	const char *previewArg = NULL;
	const char *upArg = NULL;
	const char *forwardArg = NULL;
	const char *fitArg = NULL;
	const char *scaleArg = NULL;
	const char *sizeArg = NULL;
	const char *wheelsArg = NULL;
	const char *repairArg = NULL;
	const char *remeshArg = NULL;
	int remesh = 0;
	struct RldMkOptions opt;
	struct RldMkPly ply;
	struct RldAnimModel model;
	struct RldMkModelInfo info;
	u32 found = 0;
	u32 shown = 0;
	u32 i;
	int p;

	RldMk_DefaultOptions(&opt);
	RldMk_ResetSaid();
	memset(&ply, 0, sizeof(ply));
	memset(&model, 0, sizeof(model));
	memset(&folder, 0, sizeof(folder));
	memset(poses, 0, sizeof(poses));
	for (p = 0; p < RLDANIM_POSE_COUNT; p++)
	{
		poses[p].rule = "-";
	}

	for (p = 0; p < argc; p++)
	{
		const char *arg = argv[p];
		const int hasValue = ((p + 1) < argc);

		if ((strcmp(arg, "--model") == 0) && hasValue)
		{
			modelArg = argv[++p];
		}
		else if ((strcmp(arg, "--pose-dir") == 0) && hasValue)
		{
			dirArg = argv[++p];
		}
		else if ((strcmp(arg, "--preview") == 0) && hasValue)
		{
			previewArg = argv[++p];
		}
		else if ((strcmp(arg, "--up") == 0) && hasValue)
		{
			upArg = argv[++p];
		}
		else if ((strcmp(arg, "--forward") == 0) && hasValue)
		{
			forwardArg = argv[++p];
		}
		else if ((strcmp(arg, "--fit") == 0) && hasValue)
		{
			fitArg = argv[++p];
		}
		else if ((strcmp(arg, "--scale") == 0) && hasValue)
		{
			scaleArg = argv[++p];
		}
		else if ((strcmp(arg, "--size") == 0) && hasValue)
		{
			sizeArg = argv[++p];
		}
		else if ((strcmp(arg, "--wheels") == 0) && hasValue)
		{
			wheelsArg = argv[++p];
		}
		else if ((strcmp(arg, "--repair") == 0) && hasValue)
		{
			repairArg = argv[++p];
		}
		else if ((strcmp(arg, "--remesh") == 0) && hasValue)
		{
			remeshArg = argv[++p];
		}
		else if (strcmp(arg, "--out") == 0)
		{
			// Never a container from here: the poses are not packed yet.
			RldMk_Say("error", "usage", arg,
			          "char-poses writes no character: it checks the poses and writes only the --preview file. Build the character with make-char.");
			return 2;
		}
		else
		{
			RldMk_Say("error", "usage", arg, "rldpack does not know the argument \"%s\".", arg);
			fprintf(stderr, "\n");
			RldAnim_Usage(stderr);
			return 2;
		}
	}

	if ((modelArg == NULL) || (dirArg == NULL))
	{
		RldMk_Say("error", "usage", NULL, "%s", (modelArg == NULL) ? "No model file was given." : "No pose folder was given.");
		fprintf(stderr, "\n");
		RldAnim_Usage(stderr);
		return 2;
	}

	// THE SWITCHES of make-char that move the model, read as make-char reads them.
	if ((upArg != NULL) && (strcmp(upArg, "y") != 0) && (strcmp(upArg, "Y") != 0))
	{
		if ((strcmp(upArg, "z") == 0) || (strcmp(upArg, "Z") == 0))
		{
			opt.upZ = 1;
		}
		else
		{
			RldMk_Say("error", "up", upArg, "The up axis must be y or z.");
		}
	}
	if ((forwardArg != NULL) && (strcmp(forwardArg, "z") != 0) && (strcmp(forwardArg, "+z") != 0))
	{
		if (strcmp(forwardArg, "-z") == 0)
		{
			opt.backwards = 1;
		}
		else
		{
			RldMk_Say("error", "forward", forwardArg, "Forward must be z or -z.");
		}
	}
	if (RldMk_FitSwitch(fitArg, scaleArg, &opt.fit) && (scaleArg != NULL))
	{
		char *end = NULL;
		const double value = strtod(scaleArg, &end);

		if ((end == scaleArg) || (*end != '\0') || !(value > 0.0) || !(value <= 1000.0))
		{
			RldMk_Say("error", "scale", scaleArg, "The scale \"%s\" must be a number above 0 (1 = meters as exported).", scaleArg);
		}
		else
		{
			opt.scale = value;
		}
	}
	if (wheelsArg != NULL)
	{
		char word[8];
		const int known = RldMk_Lower(wheelsArg, word, sizeof(word));

		if (known && (strcmp(word, "off") == 0))
		{
			opt.noWheels = 1;
		}
		else if (!known || (strcmp(word, "on") != 0))
		{
			RldMk_Say("error", "wheels", wheelsArg, "Wheels must be on or off.");
		}
	}
	if (repairArg != NULL)
	{
		char word[8];
		const int known = RldMk_Lower(repairArg, word, sizeof(word));

		if (known && (strcmp(word, "off") == 0))
		{
			opt.repair = 0;
		}
		else if (!known || (strcmp(word, "auto") != 0))
		{
			RldMk_Say("error", "repair", repairArg, "Repair must be auto or off.");
		}
	}
	if (remeshArg != NULL)
	{
		char word[8];
		const int known = RldMk_Lower(remeshArg, word, sizeof(word));

		if (known && (strcmp(word, "on") == 0))
		{
			remesh = 1;
		}
		else if (!known || (strcmp(word, "off") != 0))
		{
			RldMk_Say("error", "remesh", remeshArg, "Remesh must be on or off.");
		}
	}
	if ((sizeArg != NULL) && (!RldMk_ParseWhole(sizeArg, 100000u, &opt.sizePercent) || (opt.sizePercent < RLDMK_SIZE_MIN) || (opt.sizePercent > RLDMK_SIZE_MAX)))
	{
		RldMk_Say("error", "char-size", sizeArg, "The size \"%s\" must be a whole number from 50 to 200 (percent; %s).", sizeArg, RLDMK_SIZE_TEXT);
		opt.sizePercent = 100u;
	}

	{
		char number[24];

		Rld_Emit("value", "pose-dir", dirArg, "switch", (const char *)NULL);
		snprintf(number, sizeof(number), "%u", opt.sizePercent);
		Rld_Emit("value", "size", number, (sizeArg != NULL) ? "switch" : "default", (const char *)NULL);
		Rld_Emit("value", "fit", opt.fit ? "crash" : "none", (fitArg != NULL) ? "switch" : "default", (const char *)NULL);
		Rld_Emit("value", "wheels", opt.noWheels ? "off" : "on", (wheelsArg != NULL) ? "switch" : "default", (const char *)NULL);
		Rld_Emit("value", "repair", opt.repair ? "auto" : "off", (repairArg != NULL) ? "switch" : "default", (const char *)NULL);
		Rld_Emit("value", "up", opt.upZ ? "z" : "y", (upArg != NULL) ? "switch" : "default", (const char *)NULL);
		Rld_Emit("value", "forward", opt.backwards ? "-z" : "z", (forwardArg != NULL) ? "switch" : "default", (const char *)NULL);
		Rld_Emit("value", "remesh", remesh ? "on" : "off", (remeshArg != NULL) ? "switch" : "default", (const char *)NULL);
	}

	// A wrong value of a switch is the command line: nothing is read or written.
	if (s_rldMkErrors != 0)
	{
		return 2;
	}

	// The closed hull has vertices of its own, none of them in a pose: the
	// poses cannot follow it. Said, the rules still
	// run on the model without the hull, and no preview is written.
	if (remesh)
	{
		RldMk_Say("error", "pose-remesh", NULL,
		          "Pose files cannot be used with the closed hull (--remesh on): the hull has no vertices in common with the poses. Turn the hull off or remove the pose folder.");
	}

	// THE MODEL, read aloud: what its reader finds is said as for make-char.
	// PLY only (tools/rldpack_import.inc: an OBJ is model-ply-only) - the
	// poses must have its points in the order of the file.
	if (!RldMk_LoadModel(modelArg, &ply, &info, RLDMK_ROLE_POSE))
	{
		return 1;
	}

	if (!RldAnim_PrepareModel(&model, &ply, &opt, why, sizeof(why)))
	{
		RldMk_Say("error", "pose-model", why,
		          "The model does not build (%s), so the poses cannot be placed beside it. Check the model first; the vertex and face rules are checked all the same.",
		          why);
	}

	// THE FOLDER.
	if (!Rld_EachFile(dirArg, RldAnim_FolderVisit, &folder))
	{
		RldMk_Say("error", "pose-dir", dirArg, "The pose folder %s does not exist or cannot be read.", dirArg);
	}
	else if (folder.seen > folder.count)
	{
		RldMk_Say("warning", "pose-folder", NULL, "The pose folder holds %u PLY files; only the first %u by name are looked at. Keep only the poses in it.",
		          folder.seen, folder.count);
	}

	for (i = 0; i < folder.count; i++)
	{
		const char *name = folder.names[i];
		const int pose = RldAnim_PoseIndex(name);
		char path[RLD_PATH_MAX + RLD_NAME_MAX];

		RldAnim_JoinPath(path, sizeof(path), dirArg, name);
		if (RldAnim_SamePath(path, modelArg))
		{
			continue;
		}
		if ((pose < 0) || (poses[pose].file[0] != '\0'))
		{
			// Not a pose name - or the second file of one on a system that
			// tells cases apart: the first by name counts.
			Rld_Emit("char", "pose-unknown", name, (const char *)NULL);
			RldMk_Say("note", "pose-unknown", name, "%s is not a pose name and is ignored. Pose names: turn_left, turn_right, reverse, bump, jump, idle.",
			          name);
			continue;
		}

		found++;
		snprintf(poses[pose].file, sizeof(poses[pose].file), "%s", name);
		if (!RldAnim_ReadPose(dirArg, name, &poses[pose].ply))
		{
			poses[pose].state = RLDANIM_BAD;
			poses[pose].rule = "pose-read";
			continue;
		}
		poses[pose].state = RldAnim_CheckPose(&model, &poses[pose].ply, name, &poses[pose].rule);
	}

	RldAnim_Settle(poses);

	for (p = 0; p < RLDANIM_POSE_COUNT; p++)
	{
		const int named = (strcmp(poses[p].rule, "-") != 0);

		shown += RldAnim_Shown(poses[p].state) ? 1u : 0u;
		Rld_Emit("char", "pose", s_rldAnimNames[p], s_rldAnimStates[poses[p].state], poses[p].file, poses[p].rule, (const char *)NULL);
		printf("  %-12s  %-10s  %s%s%s\n", s_rldAnimNames[p], s_rldAnimStates[poses[p].state], (poses[p].file[0] != '\0') ? poses[p].file : "-",
		       named ? "  " : "", named ? poses[p].rule : "");
	}
	{
		char foundText[16];
		char shownText[16];

		snprintf(foundText, sizeof(foundText), "%u", found);
		snprintf(shownText, sizeof(shownText), "%u", shown);
		Rld_Emit("char", "poses", foundText, shownText, (const char *)NULL);
	}

	// THE PREVIEW: of a model that was built, whatever the poses say.
	if ((previewArg != NULL) && model.built && !remesh)
	{
		size_t size = 0;
		u8 *data = RldAnim_Preview(&model, poses, &size);
		FILE *file = (data != NULL) ? fopen(previewArg, "wb") : NULL;
		const int openError = ((data != NULL) && (file == NULL)) ? errno : 0;

		if ((file == NULL) || (fwrite(data, 1, size, file) != size) || (fclose(file) != 0))
		{
			RldMk_EmitFile("preview", "failed", previewArg, 0u);
			if (data == NULL)
			{
				(void)RldMk_OutOfMemory();
			}
			else if (!RldMk_SayMissingFolder("warning", "preview", previewArg, "The preview file", openError))
			{
				RldMk_Say("warning", "preview", previewArg, "The preview file %s cannot be written. Is the folder write-protected?", previewArg);
			}
		}
		else
		{
			RldMk_EmitFile("preview", "ok", previewArg, size);
			printf("  preview       %s, %u bytes (%u of %d poses, %u triangles each)\n", previewArg, (u32)size, shown, RLDANIM_POSE_COUNT,
			       model.triangleCount);
		}
		free(data);
	}

	for (p = 0; p < RLDANIM_POSE_COUNT; p++)
	{
		RldMk_FreePly(&poses[p].ply);
	}
	RldAnim_FreeModel(&model);
	RldMk_FreePly(&ply);
	return (s_rldMkErrors != 0) ? 1 : 0;
}

static void RldAnim_Usage(FILE *out)
{
	fprintf(out, "CHAR-POSES (A PREVIEW - THE POSES ARE NOT PACKED YET)\n");
	fprintf(out, "  rldpack char-poses --model <ply> --pose-dir <folder> [switches]\n");
	fprintf(out, "  Checks poses of your own against a model and places them where the\n");
	fprintf(out, "  model stands, for the 3D view of Reload Studio. A pose is the model's\n");
	fprintf(out, "  own mesh exported once more with moved vertices: the same vertices in\n");
	fprintf(out, "  the same order, the same faces. It writes no character and refuses\n");
	fprintf(out, "  --out; make-char does not use the poses yet.\n");
	fprintf(out, "\n");
	fprintf(out, "  --model <ply>               the model, as for make-char (required)\n");
	fprintf(out, "  --pose-dir <folder>         one PLY per pose (required): turn_left,\n");
	fprintf(out, "                              turn_right, reverse, bump, jump; idle is\n");
	fprintf(out, "                              checked but not used. Other PLY files are\n");
	fprintf(out, "                              named and left alone\n");
	fprintf(out, "  --preview <file>            write the placed poses for Reload Studio\n");
	fprintf(out, "                              (RLDPS1, see tools/rldpack_anim.inc)\n");
	fprintf(out, "  --remesh on|off             on: refused (pose-remesh) - the closed hull\n");
	fprintf(out, "                              has no vertices in common with the poses\n");
	fprintf(out, "  --up, --forward, --fit, --scale, --size, --wheels, --repair\n");
	fprintf(out, "                              as for make-char: the poses take the place\n");
	fprintf(out, "                              the model gets with them\n");
	fprintf(out, "\n");
	fprintf(out, "  rldpack char-poses --model driver.ply --pose-dir poses --size 110\n");
	fprintf(out, "\n");
}

//========================================================================================
// SELF-TEST
//========================================================================================
//
// The standard model of make-char's self-test (kart, driver and steering wheel
// as boxes) and poses made from it in memory. No file is read or written.

static int s_rldAnimTestFailed;

static void RldAnim_Test(int ok, const char *name, const char *format, ...)
{
	char text[320];
	va_list list;

	if (ok)
	{
		printf("  ok   char-poses %s\n", name);
		return;
	}

	va_start(list, format);
	vsnprintf(text, sizeof(text), format, list);
	va_end(list);
	printf("  FAIL char-poses %s: %s\n", name, text);
	s_rldAnimTestFailed++;
}

// A test mesh as a binary PLY in memory, read back. 1 = read.
static int RldAnim_TestRead(const struct RldMkTestMesh *mesh, struct RldMkPly *ply)
{
	size_t size = 0;
	u8 *data = RldMk_TestPly(mesh, 1, 1, 0, &size);
	int ok;

	memset(ply, 0, sizeof(*ply));
	if (data == NULL)
	{
		return 0;
	}
	ok = RldMk_ParsePly(data, size, ply);
	free(data);
	return ok;
}

// Checks a pose and says which rule decided; *state RLDANIM_OK or _BAD.
static const char *RldAnim_TestRule(const struct RldAnimModel *m, const struct RldMkTestMesh *pose, int *state)
{
	struct RldMkPly ply;
	const char *rule = "-";

	*state = RLDANIM_BAD;
	if (!RldAnim_TestRead(pose, &ply))
	{
		return "unread";
	}
	*state = RldAnim_CheckPose(m, &ply, "pose.ply", &rule);
	RldMk_FreePly(&ply);
	return rule;
}

// The model placed as a pose of itself lies where make-char's preview draws
// the model: the box of its triangles against the box of the neutral frame
// of RLDPV1, at --size `percent`. Leaves the model in *m (freed by the caller).
static void RldAnim_TestCopy(const struct RldMkTestMesh *mesh, u32 percent, struct RldMkPly *ply, struct RldAnimModel *m)
{
	struct RldMkOptions opt;
	char name[96];
	char why[160];
	int built[6];
	int posed[6];
	double *placed = NULL;
	size_t size = 0;
	int kart[6];
	u8 *preview;
	int delta = 32767;
	u32 t;
	int a;

	snprintf(name, sizeof(name), "a copy of the model stands where the model is drawn (--size %u)", percent);
	RldMk_DefaultOptions(&opt);
	opt.sizePercent = percent;
	memset(m, 0, sizeof(*m));
	if (!RldAnim_TestRead(mesh, ply) || !RldAnim_PrepareModel(m, ply, &opt, why, sizeof(why)))
	{
		RldAnim_Test(0, name, "the test model does not build");
		return;
	}

	for (a = 0; a < 3; a++)
	{
		built[a] = posed[a] = 32767;
		built[3 + a] = posed[3 + a] = -32768;
	}

	// pose 0 of RLDPV1: turn frame 10, the neutral one
	preview = RldMk_Preview(&m->build, &size, kart);
	if (preview != NULL)
	{
		const u32 count = Rld_ReadLE32(&preview[12]);

		for (t = 0; t < (3u * count); t++)
		{
			for (a = 0; a < 3; a++)
			{
				const int v = (int)(s16)Rld_ReadLE16(&preview[16u + (size_t)t * RLDANIM_CORNER_BYTES + 2u * (u32)a]);

				built[a] = (v < built[a]) ? v : built[a];
				built[3 + a] = (v > built[3 + a]) ? v : built[3 + a];
			}
		}
	}

	placed = RldAnim_PlacePose(m, ply);
	for (t = 0; (placed != NULL) && (t < (3u * m->triangleCount)); t++)
	{
		for (a = 0; a < 3; a++)
		{
			const int v = (int)RldMk_Round(placed[3u * m->triangle[t] + (u32)a]);

			posed[a] = (v < posed[a]) ? v : posed[a];
			posed[3 + a] = (v > posed[3 + a]) ? v : posed[3 + a];
		}
	}

	if ((preview != NULL) && (placed != NULL))
	{
		delta = 0;
		for (a = 0; a < 6; a++)
		{
			const int d = (built[a] > posed[a]) ? (built[a] - posed[a]) : (posed[a] - built[a]);

			delta = (d > delta) ? d : delta;
		}
	}
	RldAnim_Test(delta <= 2, name, "box %d %d %d %d %d %d, the model's %d %d %d %d %d %d", posed[0], posed[1], posed[2], posed[3], posed[4], posed[5],
	             built[0], built[1], built[2], built[3], built[4], built[5]);
	free(preview);
	free(placed);
}

static int RldAnim_SelfTest(void)
{
	struct RldMkTestMesh *mesh = (struct RldMkTestMesh *)calloc(1, sizeof(struct RldMkTestMesh));
	struct RldMkTestMesh *pose = (struct RldMkTestMesh *)calloc(1, sizeof(struct RldMkTestMesh));
	const int quietBefore = s_rldMkQuiet;
	struct RldMkPly ply;
	struct RldAnimModel m;
	int state = 0;
	const char *rule;
	u32 driver = 0;
	u32 kartVertex = 0;
	u32 i;
	int k;

	s_rldAnimTestFailed = 0;
	if ((mesh == NULL) || (pose == NULL))
	{
		free(mesh);
		free(pose);
		printf("  FAIL char-poses: out of memory\n");
		return 1;
	}
	s_rldMkQuiet = 1;
	RldMk_ResetSaid();

	// THE NAMES: case does not count, only .ply files, nothing else is a pose.
	RldAnim_Test((RldAnim_PoseIndex("Turn_Left.PLY") == RLDANIM_TURN_LEFT) && (RldAnim_PoseIndex("jump.ply") == RLDANIM_JUMP) &&
	                 (RldAnim_PoseIndex("idle.ply") == RLDANIM_IDLE) && (RldAnim_PoseIndex("wave.ply") < 0) && (RldAnim_PoseIndex("jump.obj") < 0) &&
	                 (RldAnim_PoseIndex(".ply") < 0),
	             "pose names", "the names are not told apart");

	// THE PLACE at --size 130: kart and driver apart; then at 100 % for the rules.
	RldMk_TestStandard(mesh, 2u, 0, 0.0, -1);
	RldAnim_TestCopy(mesh, 130u, &ply, &m);
	RldAnim_FreeModel(&m);
	RldMk_FreePly(&ply);
	RldAnim_TestCopy(mesh, 100u, &ply, &m);
	if (!m.built)
	{
		RldAnim_FreeModel(&m);
		RldMk_FreePly(&ply);
		s_rldMkQuiet = quietBefore;
		RldMk_ResetSaid();
		free(mesh);
		free(pose);
		return s_rldAnimTestFailed + 1;
	}

	// THE RULES.
	RldMk_ResetSaid();
	rule = RldAnim_TestRule(&m, mesh, &state);
	RldAnim_Test((state == RLDANIM_OK) && (strcmp(rule, "-") == 0) && (s_rldMkCodes[0] == '\0'), "a copy of the model passes silently", "%s, said: %s",
	             rule, s_rldMkCodes);

	for (i = 0; i < mesh->vertexCount; i++)
	{
		const int group = RldAnim_Group(&m, i);

		driver = ((group == RLDMK_GROUP_DRIVER) && (driver == 0u)) ? i : driver;
		kartVertex = ((group == RLDMK_GROUP_KART) && (kartVertex == 0u)) ? i : kartVertex;
	}

	// The driver leans: every driver vertex 0.1 to the side. Usable, moved by
	// the build's scale; the kart stays where it was.
	memcpy(pose, mesh, sizeof(*pose));
	for (i = 0; i < pose->vertexCount; i++)
	{
		if (RldAnim_Group(&m, i) == RLDMK_GROUP_DRIVER)
		{
			pose->xyz[i][0] = (double)(float)(pose->xyz[i][0] + 0.1);
		}
	}
	RldMk_ResetSaid();
	rule = RldAnim_TestRule(&m, pose, &state);
	RldAnim_Test((state == RLDANIM_OK) && (s_rldMkErrors == 0) && !RldMk_Said("pose-kart-moved"), "a leaning driver passes", "%s, said: %s", rule,
	             s_rldMkCodes);
	{
		struct RldMkPly posePly;
		const double expect = 0.1 * RLDMK_UNITS * m.opt.scale;
		double *a = RldAnim_PlacePose(&m, &ply);
		double *b = NULL;
		double moved = 0.0;
		double kart = 1.0;

		if (RldAnim_TestRead(pose, &posePly))
		{
			b = RldAnim_PlacePose(&m, &posePly);
			RldMk_FreePly(&posePly);
		}
		if ((a != NULL) && (b != NULL))
		{
			moved = b[3u * driver] - a[3u * driver];
			kart = RldMk_Sqrt(RldMk_Distance2(&a[3u * kartVertex], &b[3u * kartVertex]));
		}
		RldAnim_Test((moved > (expect - 0.01)) && (moved < (expect + 0.01)) && (kart == 0.0), "the pose takes the model's scale, the kart its place",
		             "moved %.3f (expected %.3f), kart %.3f", moved, expect, kart);
		free(a);
		free(b);
	}

	// One vertex more.
	memcpy(pose, mesh, sizeof(*pose));
	RldMk_TestVertex(pose, 0.0, 0.5, 0.0, 3u);
	rule = RldAnim_TestRule(&m, pose, &state);
	RldAnim_Test((state == RLDANIM_BAD) && (strcmp(rule, "pose-vertex-count") == 0), "a vertex more (pose-vertex-count)", "%s", rule);

	// One face less.
	memcpy(pose, mesh, sizeof(*pose));
	pose->faceCount--;
	rule = RldAnim_TestRule(&m, pose, &state);
	RldAnim_Test((state == RLDANIM_BAD) && (strcmp(rule, "pose-face-count") == 0), "a face less (pose-face-count)", "%s", rule);

	// A face turned round.
	memcpy(pose, mesh, sizeof(*pose));
	{
		const u32 keep = pose->corners[3][1];

		pose->corners[3][1] = pose->corners[3][3];
		pose->corners[3][3] = keep;
	}
	rule = RldAnim_TestRule(&m, pose, &state);
	RldAnim_Test((state == RLDANIM_BAD) && (strcmp(rule, "pose-faces") == 0), "a face turned round (pose-faces)", "%s", rule);

	// A welded point torn apart: the sides of a box meet at seams, every
	// corner of a box is three vertices welded into one.
	memcpy(pose, mesh, sizeof(*pose));
	for (i = 1; i < mesh->vertexCount; i++)
	{
		if (m.joined[i] != i)
		{
			pose->xyz[i][1] = (double)(float)(pose->xyz[i][1] + 0.05);
			break;
		}
	}
	rule = RldAnim_TestRule(&m, pose, &state);
	RldAnim_Test((i < mesh->vertexCount) && (state == RLDANIM_BAD) && (strcmp(rule, "pose-split") == 0), "a welded point torn apart (pose-split)", "%s",
	             rule);

	// The kart moved a little and the driver far: usable, with warnings.
	memcpy(pose, mesh, sizeof(*pose));
	for (i = 0; i < pose->vertexCount; i++)
	{
		const int group = RldAnim_Group(&m, i);

		if (group == RLDMK_GROUP_KART)
		{
			pose->xyz[i][1] = (double)(float)(pose->xyz[i][1] + 0.01);
		}
		else if (group == RLDMK_GROUP_DRIVER)
		{
			pose->xyz[i][1] = (double)(float)(pose->xyz[i][1] + 2.5);
		}
	}
	RldMk_ResetSaid();
	rule = RldAnim_TestRule(&m, pose, &state);
	RldAnim_Test((state == RLDANIM_OK) && RldMk_Said("pose-kart-moved") && RldMk_Said("pose-far") && (s_rldMkErrors == 0),
	             "kart moved and driver far (pose-kart-moved, pose-far)", "%s, said: %s", rule, s_rldMkCodes);

	// The states over all poses, and the file.
	{
		struct RldAnimPose set[RLDANIM_POSE_COUNT];
		size_t size = 0;
		size_t expect;
		u8 *data;

		memset(set, 0, sizeof(set));
		for (k = 0; k < RLDANIM_POSE_COUNT; k++)
		{
			set[k].rule = "-";
		}
		set[RLDANIM_TURN_LEFT].state = RLDANIM_OK;
		set[RLDANIM_JUMP].state = RLDANIM_OK;
		set[RLDANIM_IDLE].state = RLDANIM_OK;
		set[RLDANIM_BUMP].state = RLDANIM_BAD;
		RldMk_ResetSaid();
		RldAnim_Settle(set);
		RldAnim_Test((set[RLDANIM_TURN_LEFT].state == RLDANIM_AUTOMATIC) && (set[RLDANIM_IDLE].state == RLDANIM_UNUSED) &&
		                 (set[RLDANIM_JUMP].state == RLDANIM_OK) && RldMk_Said("pose-turn-pair") && RldMk_Said("pose-idle-unused"),
		             "a turn without its partner, idle unused (pose-turn-pair)", "said: %s", s_rldMkCodes);

		for (k = 0; k < RLDANIM_POSE_COUNT; k++)
		{
			if (RldAnim_Shown(set[k].state))
			{
				(void)RldAnim_TestRead(mesh, &set[k].ply);
			}
		}
		data = RldAnim_Preview(&m, set, &size);
		expect = 12u + (size_t)RLDANIM_POSE_COUNT * 4u + 3u * (size_t)m.triangleCount * 3u * RLDANIM_CORNER_BYTES;
		RldAnim_Test((data != NULL) && (size == expect) && (memcmp(data, "RLDPS1\0\0", 8u) == 0) && (Rld_ReadLE32(&data[8]) == RLDANIM_POSE_COUNT) &&
		                 (Rld_ReadLE32(&data[12]) == m.triangleCount) &&
		                 (Rld_ReadLE32(&data[16u + (size_t)m.triangleCount * 3u * RLDANIM_CORNER_BYTES]) == 0u),
		             "the preview file (RLDPS1, six poses, the missing ones empty)", "%u bytes, expected %u", (u32)size, (u32)expect);
		free(data);
		for (k = 0; k < RLDANIM_POSE_COUNT; k++)
		{
			RldMk_FreePly(&set[k].ply);
		}
	}

	RldAnim_FreeModel(&m);
	RldMk_FreePly(&ply);
	s_rldMkQuiet = quietBefore;
	RldMk_ResetSaid();
	free(mesh);
	free(pose);
	return s_rldAnimTestFailed;
}
