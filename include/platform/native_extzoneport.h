#ifndef NATIVE_EXTZONEPORT_H
#define NATIVE_EXTZONEPORT_H

#include <stdint.h>
#include <platform/native_ps2float.h>

// THE RELOAD SIDE OF THE ZONE PHYSICS AND CHASE CAMERA PORT
// (platform/native_extzoneport.c; --ext-track only).
//
// The port itself (platform/native_extzonephys.c, platform/native_extchasecam.c)
// is standalone C that computes in the source game's own world (metres, z up)
// with the second console's float arithmetic (native_ps2float.c). This part
// connects it to Reload:
//   - the track's source data, section "physics.source" of exttrack.json (the
//     converter writes it, exact float bits as "%08x" strings):
//       "source": {"version": 1, "port": 1,
//         "frame": {"origin_cnk": [cx, cy, oz], "scale_per_m": 64, "rot_deg": 0},
//         "phantoms": [{"name", "kind": "static_obb"|"dynamic_obb"|"aabb", "index",
//                       "rot": [4 bits] (obb), "centre": [3 bits], "size": [3 bits]}],
//         "maggrav_splines": [{"type", "hkindex", "nodes": [[6 bits: pos, dir]]}],
//         "magdisp_splines": [...],
//         "csv": {"<table>": [{"name", "text": [cells], "bits": [nearest float bits]}]},
//         "world": {...}}   the start world of the port (platform/native_extzoneworld.c)
//     Without the section (or with "port" 0) nothing of the port runs: the
//     track behaves exactly as before.
//   - the frame mapping between Reload world units (y up) and the source world:
//       reload = scale * (x - cx, z - oz, -(y - cy))      (rot_deg 0)
//     with rot_deg about the vertical as the converter's to_reload;
//   - the step log (--dev-ext-step-log <file>): for every step of the port the
//     state before, every query the step asked Reload (input and answer) and
//     the state after, so that an offline bench can run the same step.

#define EXT_ZP_PHANTOMS_MAX 64
#define EXT_ZP_SPLINES_MAX 16
#define EXT_ZP_NODES_MAX 1024
#define EXT_ZP_CSV_TABLES 4
#define EXT_ZP_CSV_ROWS_MAX 192
#define EXT_ZP_NAME_MAX 64 // the longest source name is 59 characters

enum
{
	EXT_ZP_KIND_STATIC_OBB = 0,
	EXT_ZP_KIND_DYNAMIC_OBB = 1,
	EXT_ZP_KIND_AABB = 2,
};

typedef struct
{
	char name[EXT_ZP_NAME_MAX];
	int kind;      // EXT_ZP_KIND_*
	int index;     // its place in the source list of its kind
	Ps2F rot[4];   // obb: axis x, y, z and angle (radians); aabb: 0
	Ps2F centre[3];
	Ps2F size[3];  // full edge lengths
} ExtZpSourcePhantom;

typedef struct
{
	int type;      // 0 plane, 1 point, 2 inverse point, -1 empty entry
	int hkIndex;
	int nodeCount;
	int firstNode; // into ExtZpSource.nodes
} ExtZpSourceSpline;

typedef struct
{
	char name[EXT_ZP_NAME_MAX];
	int count;            // number cells (1..4)
	char text[4][24];     // the cells as written
	Ps2F bits[4];         // the nearest float of each cell
} ExtZpSourceCsvRow;

typedef struct
{
	char name[16];
	int rowCount;
	ExtZpSourceCsvRow rows[EXT_ZP_CSV_ROWS_MAX];
} ExtZpSourceCsv;

typedef struct
{
	int present;   // the section was read without error
	int port;      // "port": 1
	double origin[3];
	double scale;
	double rotDeg;
	int phantomCount;
	ExtZpSourcePhantom phantoms[EXT_ZP_PHANTOMS_MAX];
	int maggravCount, magdispCount;
	ExtZpSourceSpline maggrav[EXT_ZP_SPLINES_MAX];
	ExtZpSourceSpline magdisp[EXT_ZP_SPLINES_MAX];
	int nodeCount;
	Ps2F nodes[EXT_ZP_NODES_MAX][6];
	int csvCount;
	ExtZpSourceCsv csv[EXT_ZP_CSV_TABLES];
	int badValues; // entries left out (bad bits, too many, wrong type)
} ExtZpSource;

// The reader, ExtZp_ReadSource(doc, node) / ExtZp_ClearSource(), is called
// by the physics reader (platform/native_extphys.c) on the key "source"; both
// are defined before it in the unity build (they need the JSON document type).

// The read source (never NULL; present 0 without the section).
const ExtZpSource *ExtZp_Source(void);
// 1 = the track asks for the port (section present and "port" 1).
int ExtZp_PortWanted(void);
// A CSV row by table and name (NULL when missing).
const ExtZpSourceCsvRow *ExtZp_CsvRow(const char *table, const char *name);

// Frame mapping (double, no rounding): Reload world units <-> source metres.
void ExtZp_PointToSource(const double reload[3], double source[3]);
void ExtZp_PointFromSource(const double source[3], double reload[3]);
void ExtZp_DirToSource(const double reload[3], double source[3]);
void ExtZp_DirFromSource(const double source[3], double reload[3]);
// A double to the nearest float pattern (the one rounding at the border; the
// port computes on with the second console's arithmetic).
Ps2F ExtZp_ToPs2(double v);

// THE STEP LOG (--dev-ext-step-log <file>). Binary, little endian:
//   file:   "RLZSTEP1" then records
//   record: u32 tag, u32 bytes that follow, then the body
//     'LEVL' body: u32 layout version, then blobs (the level's port data)
//     'STEP' body: u32 frame, u32 seat, u32 step kind (1 zone physics, 2 chase
//            camera), u32 func (the source function the step is), u32 delta
//            (1: 'PRE '/'POST' hold only the pages that differ from the last
//            logged image of this stay in the zone - the reader keeps the
//            image; 0: every page), then blobs
//   blob:   u32 tag, u32 length, bytes (padded to 4)
//     'PRE ' the port state before the step, 'POST' after, 'QRY ' one query:
//     u32 query kind, then its input and its answer as the port's structs.
// The structs are the port's own (include/platform/native_extzonephys.h,
// native_extchasecam.h); an offline reader compiles against the same headers.
enum
{
	EXT_ZP_LOG_STEP_ZONE = 1,
	EXT_ZP_LOG_STEP_CAMERA = 2,
};
#define EXT_ZP_TAG(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))
extern const char *g_cfg_extStepLog; // the file of --dev-ext-step-log, NULL = off
int ExtZp_LogOn(void);
void ExtZp_LogBegin(uint32_t recordTag, const uint32_t *head, int headWords);
void ExtZp_LogBlob(uint32_t tag, const void *bytes, uint32_t length);
void ExtZp_LogEnd(void);

// The port's queries answered by Reload (ray: the exact line query of the
// external collision; the other kinds: nothing yet, v0 = 0) - every query
// into the step log when it is on (needs include/platform/native_extzonemem.h).
#include <platform/native_extzonemem.h>
void ExtZp_ReloadQuery(ExtZpCtx *c, int kind, const ExtZpQueryArgs *in, ExtZpQueryResult *out);

// THE START WORLD (platform/native_extzoneworld.c; physics.source.world): the
// source's physics objects in their own layout, written into the port's
// memory; 1 = loaded (*world = the step's world object, *dt = the step time).
int ExtZp_WorldLoad(ExtZpMem *m, uint32_t *world, Ps2F *dt);
void ExtZp_ClearWorld(void);
int ExtZp_WorldPages(void);
int ExtZp_WorldWords(void);
int ExtZp_WorldBad(void);
void ExtZp_WorldSelftest(int *checks, int *failures, void (*log)(const char *line));

// THE COUPLING (--dev-ext-zone-port): the port computes seat 0's kart while
// Reload has it in an anti-gravity zone (see platform/native_extzoneport.c).
struct Driver;
struct Thread;
extern int g_cfg_extZonePort;
void ExtZp_CoupleLevel(void);                                      // once per level, after physics.source was read
int ExtZp_StageSkip(struct Thread *t, struct Driver *d, int stage); // driver stage hook: 1 = Reload skips the stage
void ExtZp_StageDone(struct Thread *t, struct Driver *d, int stage); // after a stage ran
int ExtZp_PortActive(void);                                        // the port has seat 0's kart now
// The port's memory and seat 0's objects while the port is active (NULL otherwise),
// for the chase camera's glue: read only.
const ExtZpMem *ExtZp_PortMemory(uint32_t *kart, uint32_t *body, uint32_t *entity, uint32_t *world);
// Seat 0's kart as the port has it (for the chase camera's glue). 0 and active 0
// while the port does not drive the kart (then Reload's own state holds).
typedef struct ExtZpKartView
{
	int active;            // 1 = the port drives seat 0 now (values below valid)
	uint32_t kart, body, entity, world; // the objects in the port's memory
	int state;             // K+0x3E0: 2 drive, 3 reverse, 4 air, 5/6 slide, ...
	int ground;            // K+0x402: 1 = ground contact this step
	int maglev;            // K+0x428: 1 = maglev (anti-gravity) on
	int spline;            // K+0x464: maggrav spline index, -1 none
	int node;              // K+0x468: its node
	double speed;          // K+0x42C: the kart's speed (m/s)
	double airTime;        // K+0x498: time in the air (s)
	double pos[3], vel[3]; // the body (source metres, m/s; z up)
	double right[3], forward[3], up[3]; // the body's axes (source, unit)
	double posReload[3];   // Reload world units: the kart point (posCurr / 256, 0.6 m below the body)
	double upReload[3], forwardReload[3];
} ExtZpKartView;
int ExtZp_PortKartState(ExtZpKartView *out);
void ExtZp_CoupleSelftest(int *checks, int *failures, void (*log)(const char *line)); // part of --ext-selftest phys

// The selftest part (called by --ext-selftest phys): the reader on a made-up
// section, the mapping, the log record layout.
void ExtZp_Selftest(int *checks, int *failures, void (*log)(const char *line));

#endif
