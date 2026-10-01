// rldpack_dummy.inc - the reference dummy of make-char: Crash's kart, seat,
// steering wheel and the kart wheels the game draws
//
// Part of the packer, never of the game. Self-contained: it needs only the C
// standard library (string, stdio) and the types u8 and u32 of tools/rldpack.c,
// so it can be included anywhere after those.
//
// WHAT IT IS FOR. Every custom character is built onto one fixed reference,
// the dummy: the retail kart at Crash's size with his seat, his steering wheel
// and the four wheel points the game draws its wheel sprites at. The numbers
// below were measured once on the retail driver models - the racer model HI of
// each character in BIGFILE (entry 242 + character id), animation 0 (the turn,
// 21 frames, frame 10 neutral), decoded like RB:2443-2473 and its delta frames
// like RB:2588-2637, its triangles replayed like RB:4600-4700. Only the numbers
// are here, no bytes of the models. "RB" is
// game/RenderBucket/RenderBucket_QueueExecute.c.
//
// UNITS. Model units, (frame origin + byte) x header scale / 4096, y up, z
// forward, WITHOUT the instance scale 0.8 the game draws a driver with
// (game/Vehicle/VehBirth.c:23, :384-386) - the game units of make-char and of
// RLDMK_CRASH_BOX. Values of Crash (character 0) unless said otherwise; "the
// 15 drivers" are the 16 HI models without N. Oxide.
//
// WHAT IT GIVES:
//   RldDum_Fit         where a model fitted by --fit crash goes: a shift only
//                      (the factor stays that of --fit crash)
//   RldDum_Place       shift and --size (about the seat) for one position;
//                      --size is relative to Crash's size: 100 % is the model
//                      as --fit crash fitted it, 50..200 % scale its driver and
//                      steering wheel about the seat, the kart never
//   RldDum_PoseFrame   the automatic pose of one of the 47 frames, about the
//   RldDum_PoseFit     dummy's seat and steering column (PoseFit: the model's
//   RldDum_ApplyPose   own steering wheel)
//   RldDum_Mesh        a simple mesh of the dummy for Reload Studio's preview
//   RldDum_SelfTest
//
// DETERMINISM. No time, no random numbers, no library sine, cosine or square
// root: the ones below are written out (as in tools/rldpack_char.inc), so the
// same input gives the same doubles under every runtime.

//========================================================================================
// THE DUMMY, AS MEASURED
//========================================================================================
//
// Kart and driver: a corner that is equal in all 21 frames of the turn is the
// kart, every other corner moves (Crash: 118 of 321 records still; the
// smallest motion of a moving one is 1.3 units - his feet - while one step
// along z is 0.47). 38 of his 46 still positions are the same in at least 13
// of the 14 other drivers (within 2.0): the common kart.

#define RLDDUM_GROUP_KART 0      // = RLDMK_GROUP_KART: stays still
#define RLDDUM_GROUP_DRIVER 1    // = RLDMK_GROUP_DRIVER: leans about the seat
#define RLDDUM_GROUP_WHEEL 2     // = RLDMK_GROUP_WHEEL: the steering wheel turns about its column

// THE KART, the same for all 15 drivers: their common-kart boxes agree in x
// and z within 0.7, top 51.0..52.1, bottom 5.0..6.1 (Papu 7.2 - his lowest
// floor corners are not among his common ones). 112.4 long, 56.2 wide.
#define RLDDUM_KART_BOX {-27.9, 5.6, -54.1, 28.3, 52.1, 58.3}   // x0 y0 z0 x1 y1 z1
#define RLDDUM_KART_BOTTOM 5.6
#define RLDDUM_KART_MID_Z 2.1    // (z0 + z1) / 2: where the fit puts the middle of a kart

// Its outline from above: the convex hull of the common kart in the (x, z)
// plane, counter-clockwise with x the first and z the second axis; 9 corners,
// area 6064 (the 15 drivers: 5982..6064).
#define RLDDUM_OUTLINE_COUNT 9
static const double s_rldDumOutline[RLDDUM_OUTLINE_COUNT][2] = {
	{-27.9, -27.5}, {-25.4, -49.0}, {-18.6, -54.1}, {19.0, -54.1}, {25.8, -49.0}, {28.3, -27.5}, {28.3, 53.2}, {0.0, 58.3}, {-27.9, 53.2},
};

// Its bottom line: the lowest point of the kart surface in the plane z =
// const, {z, y}. The floor plate runs from 10.9 at z -42 down to 5.7 at z 0
// and up to 7.4 at the nose; behind z -44 the kart is the engine block, its
// underside 17.9 and more above the ground. Along the center line the cockpit
// has only this one surface (y 5.6..10.7): the retail kart has no seat of its
// own, the driver sits on the plate.
#define RLDDUM_BOTTOM_COUNT 16
static const double s_rldDumBottom[RLDDUM_BOTTOM_COUNT][2] = {
	{-53.0, 36.3}, {-50.0, 33.9}, {-46.0, 17.9}, {-42.0, 10.9}, {-40.0, 10.7}, {-30.0, 9.4}, {-20.0, 8.2}, {-10.0, 6.9},
	{0.0, 5.7},    {10.0, 5.9},   {20.0, 6.2},   {30.0, 6.5},   {40.0, 6.8},   {50.0, 7.2},  {55.0, 7.3},  {58.0, 7.4},
};
#define RLDDUM_FLOOR_FROM_Z (-42.0)   // the floor plate; behind it the engine block

// THE SEAT: the center of the lowest driver corners in the seat zone (z < 15,
// behind the feet and the steering wheel ring), taking in the kart corners the
// driver's own triangles are fastened to - for Crash (+-6.3, 11.2, -7.5) and
// (+-7.2, 9.3, 9.3). Measured x 0.1 (an x step is 0.42); the dummy is
// symmetric, so 0. The kart floor under it is at y 5.8. The 15 drivers: median
// (0.1, 9.9, 0.8), 11 of them at y 9.6..11.8, z -7.7..3.7. It is a definition
// (where the driver rests on the kart), not a joint.
#define RLDDUM_SEAT_Y 10.2
#define RLDDUM_SEAT {0.0, RLDDUM_SEAT_Y, 0.9}
#define RLDDUM_SEAT_FLOOR_Y 5.8
#define RLDDUM_SEAT_HALF_X 7.2        // the seat contact: x +-6.3..7.2, z -7.5..9.3
#define RLDDUM_SEAT_Z0 (-7.5)
#define RLDDUM_SEAT_Z1 9.3

// THE STEERING WHEEL: the corners one rigid motion per extreme frame explains
// (Crash: 12 - a ring of 6 and 6 of the gloves on it; residual 0.6, at most
// 1.1). Retail turns it about its own column, a line along +z through the
// center (measured axis (-0.002, -0.011, 1.000), 0.6 degrees off z - the
// turns below use +z itself, RldDum_Turn), and NOT with the driver: it stays
// where it is. The ring leans: its plane normal is
// 18.4 degrees off the column (the top 7 units further forward than the
// bottom). The still hub the column ends in lies 4.1 in front of the center.
// Measured center x 0.2, symmetric dummy 0. The 15 drivers: center y
// 37.4..38.0, z 20.0..35.1 (median 24.4), ring radius median 10.2.
#define RLDDUM_WHEEL_CENTER {0.0, 38.0, 24.0}
#define RLDDUM_WHEEL_NORMAL {0.0, -0.316, 0.949}  // the ring plane, facing forward and down
#define RLDDUM_WHEEL_RADIUS 10.5                  // ring corners 9.2..11.3 from the column
#define RLDDUM_WHEEL_GRIP_RADIUS 19.1             // measured, for reference only: Crash's glove corners that turn with it
#define RLDDUM_WHEEL_HUB {0.1, 37.2, 28.1}
#define RLDDUM_WHEEL_FAR 21.0                     // two ring radii: farther, a part is no steering wheel (RldDum_Fit)

// The retail turn of the steering wheel per frame of animation 0, degrees, +
// = its top toward +X (as RldMkPose.roll): 45.2 at the stops, eased in between
// (13 of the 15 drivers turn it 43.6..46.1, Tiny 33.6, Penta 27.2). Crash's
// frames 0..10 measured 45.2 44.7 42.3 38.3 33.2 28.0 22.3 16.8 10.4 5.1 0,
// frames 20..10 45.2 44.6 42.3 38.3 33.2 28.2 22.2 16.8 10.5 5.1 0; the table
// is the mean of both sides, so that frame 20 mirrors frame 0 exactly.
#define RLDDUM_RETAIL_WHEEL_TURN 45.2
static const double s_rldDumRetailTurn[11] = {45.2, 44.65, 42.3, 38.3, 33.2, 28.1, 22.25, 16.8, 10.45, 5.1, 0.0};

// The retail lean of the driver (frame 0, a rigid fit to the moving corners
// without the steering wheel, signs as RldMkPose): roll 27.1, yaw 16.0, pitch
// -2.2 about a pivot (-0.3, 24.2, 9.2). The driver is NOT rigid (residual 8.9,
// at most 19.7): the hips stay (roll 8 in the band 0..15 above the seat), the
// arms turn with the wheel (44 in the band 30..45), the head swings (34).
// The 15 drivers: roll 14.0..46.6 (median 31.8), yaw 6.7..21.2 (median 13.4).
// Measured, for reference only: the poses below use their own angles.
#define RLDDUM_RETAIL_LEAN_ROLL 27.1
#define RLDDUM_RETAIL_LEAN_YAW 16.0

// THE KART WHEELS THE GAME DRAWS (game/DrawTires.c:336-370): centers at
// (+-0x90, 0x40, 0xc7) in front and (+-0x90, 0x40, -0x60) behind, each times
// the instance scale >> 12, through gte_lcv0tr with inst->matrix and the
// translation (t - camera) << 2 (DrawTires.c:391-420, :437-441) - world units
// x 4. A model corner lands at model units x instance scale / 4096 in world
// units (RB:1560-1632, RB:1281-1300), so in model units the centers are the
// numbers / 4, whatever the instance scale. Half the wheel quad: wheelSize /
// 64 world units x 4 (DrawTires.c:483-510; wheelSize 0xccc,
// game/Vehicle/VehBirth.c:31, :624) = 16 model units: a wheel reaches the
// ground (y 0). With the integer shifts of the game at instance scale 0xccc:
// x 35.95, y 15.94, z 49.70 / -24.07, half size 15.94. They sit 7.7..8.1
// outside Crash's kart sides (x +-28), level with its axle patches (front y
// 14.9..21.4, rear y 12.1..20.5).
#define RLDDUM_TIRE_X 36.0
#define RLDDUM_TIRE_Y 16.0
#define RLDDUM_TIRE_FRONT_Z 49.75
#define RLDDUM_TIRE_REAR_Z (-24.0)
#define RLDDUM_TIRE_HALF_SIZE 16.0
#define RLDDUM_AXLE_PATCH_TOP 21.4    // the highest kart corner at the wheel points

// Crash with his kart in the birth pose (frame 10), = RLDMK_CRASH_BOX: x
// -33.8..34.3, y 5.6..88.3, z -54.1..58.3. Measured, for reference only: the
// size itself comes from --fit crash (tools/rldpack_char.inc).
#define RLDDUM_CRASH_BOX {-33.8, 5.6, -54.1, 34.3, 88.3, 58.3}

//========================================================================================
// THE POSES OF A CUSTOM DRIVER
//========================================================================================
//
// THE TURN (21 frames): the steering wheel turns about the dummy's column, the
// driver leans about the dummy's seat, and the driver corners near the ring
// (the hands) go with the wheel. Retail does it with drawn frames: the wheel
// turns 45.2 degrees in place, the upper body leans ~31 degrees, the arms
// bend. A custom driver is one rigid body, and a rigid lean moves the hands
// sideways while a turning wheel moves its grips up and down - they never meet.
// Measured on four test models fitted onto the dummy - three with the same
// kart (two of them with a steering wheel of their own), and a motorbike model
// without kart wheels ("hands" = driver corners within 5 of their own ring):
//   * driver roll r about the seat and the wheel turned t about its column,
//     rigidly: the hands leave their grip by 3.2..4.8 at r 8 / t 12, 9.6..11.9
//     at r 8 / t 45, 12.3..15.4 with the retail lean and turn;
//   * the old pose (roll 12 about the hip, the wheel rolling with the driver):
//     0 - but the steering wheel itself swings 4.1 sideways (the motorbike's
//     handlebar 6.9), where retail keeps it in place;
//   * the hands carried with the wheel (below, GRIP): they stay on the ring
//     (distance as in frame 10); the arms stretch. Longest stretch of a driver
//     edge at r 8: t 25 x1.49..1.64, t 30 x1.59..1.69, t 45 x1.97..2.22; at
//     t 30: r 12 x1.60..2.00, r 8 x1.59..1.69 (the motorbike model, whose
//     handlebar sits close to the rider's chest, decides). Built by make-char
//     at r 8 / t 30 with the bend below: x1.59 to x1.98; beyond x1.5 at most 2
//     edges of a kart model, 11 of the motorbike model (whose fork counts as
//     its driver).
// So: the wheel turns 30 degrees (two thirds of retail, clearly visible), the
// driver rolls 8 degrees about the seat (his head still moves ~10 sideways),
// both along the retail curve, and the hands follow the wheel.
#define RLDDUM_TURN_ROLL 8.0
#define RLDDUM_TURN_WHEEL 30.0

// THE GRIP: a driver corner at distance d from the steering ring (a circle of
// the model's wheel radius about the column, in the plane of
// RLDDUM_WHEEL_NORMAL) takes w = (FAR - d) / (FAR - NEAR) of the wheel's turn
// and 1 - w of the driver's lean, w within 0..1. NEAR 4: the hands of the test
// models lie 1.2..5 from their ring. FAR 10: the chins of the test models
// start 10 from it (a FAR of 14 drags one model's chin half along).
// Only in the turn: in reverse, bump and jump the steering wheel stands still,
// and carrying the hands with it would stretch the motorbike rider's arms up
// to x3.6 at the deepest nod (bump pitch -20); there the hands leave the wheel
// as before.
#define RLDDUM_GRIP_NEAR 4.0
#define RLDDUM_GRIP_FAR 10.0

// THE LEAN BENDS: a driver corner h above the seat turns by the pose's angles
// x min(h / H, 1); corners at or below the seat only stretch. Retail keeps the
// lower body almost still as well (the bands 0..15 and 15..30 above the seat
// roll 8.3 and 6.6 degrees while the head rolls 34). A rigid lean swung the
// low corners in front or beside the seat down: one test kart model's feet
// went to y 3.4 at 100 % and -3.3 at --size 200 (bump frame 8, leaning
// forward), the motorbike rider's shoes to -1.25 / -1.8 in frames 0 / 20.
// No corner above the seat goes below it in any frame when tan(angle) <= H / d
// for its distance d from the seat across (roll) or along (pitch): tan is
// convex, so tan(angle x h / H) <= h / H x tan(angle) <= h / d. RldDum_Fit
// takes H = RLDDUM_BEND_HEIGHT or, for a wider or longer driver, the H that
// rule needs with the largest roll and pitch of the poses and the smallest
// stretch. 24 is about the legs of a seated driver.
// A DRIVER WITHOUT KART WHEELS (--wheels off) brings a vehicle of its own,
// standing on the ground, and the dummy's seat is not where it sits (the
// motorbike model's rider has his shoes at y 0.4 beside the bike, 10 below
// the dummy's seat; scaled about that seat they went to y -18 at --size 200).
// There the seat is the bottom of the driver: the middle of its box in x and
// z at its lowest y. Nothing of the driver is below it, so no driver corner
// goes below the ground in any frame at any --size.
// A kart driver keeps the dummy's seat; only corners below the seat can sink
// there, by their stretch (jump 1.05) and --size: at size s the lowest is
// seat + s x 1.05 x (its neutral y - seat) - at 200 % above the ground for
// every corner at least 5.3 above it at 100 % (the kart's own bottom is 5.6).
#define RLDDUM_BEND_HEIGHT 24.0

// REVERSE, BUMP, JUMP: the values of RldMk_FramePose (tools/rldpack_char.inc),
// now about the seat instead of the hip (0, 16, 0). The seat is where the
// driver rests on the kart: turning and stretching about it keeps that contact
// (about the hip, the seat contact 5.8 below it would swing and sink).
static const double s_rldDumBumpPitch[15] = {-9.0, -14.0, -20.0, -20.0, -19.0, -10.0, -2.0, 3.0, 8.0, -1.0, -10.0, -13.0, -16.0, -11.0, -6.0};
static const double s_rldDumBumpStretch[15] = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.965, 0.93, 0.965, 1.0, 1.0, 1.0, 1.0, 1.0};
static const double s_rldDumJumpPitch[4] = {0.0, -5.0, -12.0, -15.0};
static const double s_rldDumJumpStretch[4] = {1.0, 1.0 + 0.05 / 3.0, 1.0 + 0.1 / 3.0, 1.05};

// The frames per animation: turn, reverse, bump, jump (= s_rldCharFrames,
// include/rldchar.inc).
#define RLDDUM_ANIM_COUNT 4
#define RLDDUM_FRAMES_TOTAL 47
#define RLDDUM_NEUTRAL_FRAME 10
static const u32 s_rldDumFrames[RLDDUM_ANIM_COUNT] = {21u, 7u, 15u, 4u};

//========================================================================================
// ARITHMETIC THAT ROUNDS THE SAME EVERYWHERE
//========================================================================================

// Newton's method from above.
static double RldDum_Sqrt(double value)
{
	double x;
	int i;

	if (!(value > 0.0))
	{
		return 0.0;
	}

	x = (value > 1.0) ? value : 1.0;
	for (i = 0; i < 80; i++)
	{
		x = 0.5 * (x + value / x);
	}

	return x;
}

// Taylor series. The poses use at most 30 degrees, the mesh full circles: after
// the reduction to -180..180 the 21 terms reach far below the precision of a
// double even at pi.
static void RldDum_SinCos(double degrees, double *sine, double *cosine)
{
	double x;
	double x2;
	double term;
	double s = 0.0;
	double c = 0.0;
	int k;

	while (degrees > 180.0)
	{
		degrees -= 360.0;
	}
	while (degrees < -180.0)
	{
		degrees += 360.0;
	}

	x = degrees * (3.14159265358979323846 / 180.0);
	x2 = x * x;
	term = x;
	for (k = 1; k <= 41; k += 2)
	{
		s += term;
		term = -term * x2 / (double)((k + 1) * (k + 2));
	}

	term = 1.0;
	for (k = 0; k <= 40; k += 2)
	{
		c += term;
		term = -term * x2 / (double)((k + 1) * (k + 2));
	}

	*sine = s;
	*cosine = c;
}

// The bottom line at z (s_rldDumBottom), linear between the measured planes,
// the ends held.
static double RldDum_BottomAt(double z)
{
	int i;

	if (z <= s_rldDumBottom[0][0])
	{
		return s_rldDumBottom[0][1];
	}

	for (i = 1; i < RLDDUM_BOTTOM_COUNT; i++)
	{
		if (z <= s_rldDumBottom[i][0])
		{
			const double t = (z - s_rldDumBottom[i - 1][0]) / (s_rldDumBottom[i][0] - s_rldDumBottom[i - 1][0]);

			return s_rldDumBottom[i - 1][1] + t * (s_rldDumBottom[i][1] - s_rldDumBottom[i - 1][1]);
		}
	}

	return s_rldDumBottom[RLDDUM_BOTTOM_COUNT - 1][1];
}

//========================================================================================
// THE FIT: A FITTED MODEL ONTO THE DUMMY
//========================================================================================
//
// --fit crash scales the model (the factor is not changed here): with wheels
// its kart to 112.4 long, with --wheels off the whole model. RldDum_Fit then
// moves it - one shift for every position of every group:
//   x  the middle of the kart (with --wheels off too, when step 4 found one;
//      else of the whole model) onto x = 0, the dummy's center line. Not the
//      middle of everything: a driver may sit askew (one test model: the
//      middle of its driver's box at x -1.65, of its kart at 0.00).
//   y  with wheels: the bottom of the kart onto the dummy kart's bottom, 5.6
//      (the game's wheel sprites reach from y 0 to 32 at fixed points, so the
//      kart has to sit where Crash's does). With --wheels off: the bottom of
//      the whole model onto the ground, y 0 - its own wheels touch it.
//   z  the middle of the box the fit measured (the kart with wheels, the whole
//      model with --wheels off) onto the middle of the dummy kart, z 2.1. That
//      box is 112.4 long by the fit, so it lands on z -54.1..58.3.
// The seat is not used to place a model: it cannot be found reliably. With the
// rule of the dummy's seat (the lowest driver corners in z < 15), the three
// test models that share one CTR kart give seats 12.1 / 11.1 / 9.0 high and at
// z -3.3 / -4.1 / +8.5, the motorbike model one 2.0 high. Placed by their seats, the
// same kart would land 12.6 units apart along z; placed by the
// kart, all three get the same shift (0.00, -0.15, -0.34) and their karts lie
// within 0.4 of the dummy kart.

#define RLDDUM_FIT_MODEL 0   // the whole model was placed (--wheels off, or no kart)
#define RLDDUM_FIT_KART 1    // the kart was placed on the dummy kart

struct RldDumBox
{
	int valid;          // 0 = no such group
	double lo[3];       // game units, after --fit crash, before --size
	double hi[3];
};

struct RldDumFitInput
{
	struct RldDumBox kart;    // the kart group (RLDMK_GROUP_KART), positions the triangles use
	struct RldDumBox driver;  // the driver group
	struct RldDumBox wheel;   // the steering wheel group; valid 0 = the model has none
	struct RldDumBox model;   // every position a triangle uses
	int wheels;               // 1 = the game draws its kart wheels (default), 0 = --wheels off
	u32 sizePercent;          // --size (0 counts as 100)
};

struct RldDumFitResult
{
	int basis;                // RLDDUM_FIT_KART or RLDDUM_FIT_MODEL
	double shift[3];          // added to every position of every group (RldDum_Place)
	double size;              // --size as a factor, about the seat, driver and steering wheel only
	double seat[3];           // the pivot of --size and of the driver's poses: RLDDUM_SEAT, or
	                          // without kart wheels the bottom of the driver
	int hasWheel;             // the model has a steering wheel: it turns, the hands follow it
	int wheelAsDriver;        // its steering wheel group leans with the driver instead (RldDum_Fit)
	double bendHeight;        // > 0: the driver bends over this height above the seat (at 100 %)
	double wheelCenter[3];    // its column passes through here (after shift and size)
	double wheelRadius;       // its ring: half its x extent (after size)
	// For people and Reload Studio:
	double placed[6];         // the box that was placed (kart or whole model), after the shift: x0 y0 z0 x1 y1 z1
	double lengthDelta;       // its length minus the dummy kart's 112.4
	double widthDelta;        // its width minus the dummy kart's 56.2
	double wheelOffset[3];    // the model's steering wheel center minus the dummy's (0 without one)
	double driverLo[3];       // the driver after shift and size (0 without one)
	double driverHi[3];
};

// 1 = placed; 0 = neither a kart nor a model box (nothing to place).
static int RldDum_Fit(const struct RldDumFitInput *in, struct RldDumFitResult *out)
{
	static const double kartBox[6] = RLDDUM_KART_BOX;
	static const double seat[3] = RLDDUM_SEAT;
	static const double dummyWheel[3] = RLDDUM_WHEEL_CENTER;
	const struct RldDumBox *placed;
	const struct RldDumBox *middle;
	int a;

	memset(out, 0, sizeof(*out));
	out->size = ((in->sizePercent == 0u) ? 100.0 : (double)in->sizePercent) / 100.0;
	memcpy(out->seat, seat, sizeof(seat));

	if (in->wheels && in->kart.valid)
	{
		placed = &in->kart;
		out->basis = RLDDUM_FIT_KART;
	}
	else if (in->model.valid)
	{
		placed = &in->model;
		out->basis = RLDDUM_FIT_MODEL;
	}
	else
	{
		return 0;
	}
	// With kart wheels but no kart, make-char refuses the model (ply-no-kart);
	// for the preview the whole model then stands where a kart would, its
	// bottom on 5.6, and leans about the dummy's seat.
	middle = in->kart.valid ? &in->kart : &in->model;

	out->shift[0] = -0.5 * (middle->lo[0] + middle->hi[0]);
	out->shift[1] = (in->wheels ? RLDDUM_KART_BOTTOM : 0.0) - placed->lo[1];
	out->shift[2] = RLDDUM_KART_MID_Z - 0.5 * (placed->lo[2] + placed->hi[2]);

	for (a = 0; a < 3; a++)
	{
		out->placed[a] = placed->lo[a] + out->shift[a];
		out->placed[3 + a] = placed->hi[a] + out->shift[a];
	}
	out->lengthDelta = (placed->hi[2] - placed->lo[2]) - (kartBox[5] - kartBox[2]);
	out->widthDelta = (placed->hi[0] - placed->lo[0]) - (kartBox[3] - kartBox[0]);

	// Without kart wheels the seat is the bottom of the driver; for both the
	// height the lean bends over (RLDDUM_BEND_HEIGHT).
	{
		const struct RldDumBox *body = in->driver.valid ? &in->driver : &in->model;
		double across;
		double along;
		double roll = RLDDUM_TURN_ROLL;
		double pitch = 8.0;   // reverse
		double stretch = 1.0;
		double sine;
		double cosine;
		double need;
		int k;

		for (k = 0; k < 15; k++)
		{
			pitch = (s_rldDumBumpPitch[k] > pitch) ? s_rldDumBumpPitch[k] : ((-s_rldDumBumpPitch[k] > pitch) ? -s_rldDumBumpPitch[k] : pitch);
			stretch = (s_rldDumBumpStretch[k] < stretch) ? s_rldDumBumpStretch[k] : stretch;
		}
		for (k = 0; k < 4; k++)
		{
			pitch = (-s_rldDumJumpPitch[k] > pitch) ? -s_rldDumJumpPitch[k] : pitch;
			stretch = (s_rldDumJumpStretch[k] < stretch) ? s_rldDumJumpStretch[k] : stretch;
		}

		if (!in->wheels)
		{
			out->seat[0] = 0.5 * (body->lo[0] + body->hi[0]) + out->shift[0];
			out->seat[1] = body->lo[1] + out->shift[1];
			out->seat[2] = 0.5 * (body->lo[2] + body->hi[2]) + out->shift[2];
		}

		// the farthest a corner of the driver lies from the seat, across and along
		across = body->lo[0] + out->shift[0] - out->seat[0];
		across = (across < 0.0) ? -across : across;
		across = ((body->hi[0] + out->shift[0] - out->seat[0]) > across) ? (body->hi[0] + out->shift[0] - out->seat[0]) : across;
		along = body->lo[2] + out->shift[2] - out->seat[2];
		along = (along < 0.0) ? -along : along;
		along = ((body->hi[2] + out->shift[2] - out->seat[2]) > along) ? (body->hi[2] + out->shift[2] - out->seat[2]) : along;

		RldDum_SinCos(roll, &sine, &cosine);
		need = across * sine / cosine;
		RldDum_SinCos(pitch, &sine, &cosine);
		need = (along * sine / cosine > need) ? (along * sine / cosine) : need;
		need /= stretch;
		out->bendHeight = (need > RLDDUM_BEND_HEIGHT) ? need : RLDDUM_BEND_HEIGHT;
	}

	// The steering column: the dummy's direction (+z), through the model's own
	// wheel. Turned about the dummy's column, a wheel off it would swing round
	// it: the test karts' wheels sit 2.6 off it (moved 1.4 at 30 degrees), the
	// motorbike's handlebar 11.0 (5.7).
	// A steering wheel group without width has no ring to turn, and on a kart
	// one whose middle lies RLDDUM_WHEEL_FAR or more from the dummy's wheel is
	// not held where a kart driver holds the wheel (the test karts' wheels lie
	// 3.8 from it): turned about its own middle, such a part would spin on the
	// spot. Both lean with the driver instead, as the parts they hang on (and
	// as make-char moved its steering wheels before). Without kart wheels the
	// vehicle is the model's own: its steering part turns wherever it is.
	if (in->wheel.valid)
	{
		double c[3];
		double far2 = 0.0;

		for (a = 0; a < 3; a++)
		{
			c[a] = 0.5 * (in->wheel.lo[a] + in->wheel.hi[a]) + out->shift[a];
			far2 += (c[a] - dummyWheel[a]) * (c[a] - dummyWheel[a]);
		}

		if ((in->wheel.hi[0] > in->wheel.lo[0]) && (!in->wheels || (far2 < (RLDDUM_WHEEL_FAR * RLDDUM_WHEEL_FAR))))
		{
			out->hasWheel = 1;
			for (a = 0; a < 3; a++)
			{
				out->wheelCenter[a] = out->seat[a] + out->size * (c[a] - out->seat[a]);
				out->wheelOffset[a] = out->wheelCenter[a] - dummyWheel[a];
			}
			out->wheelRadius = out->size * 0.5 * (in->wheel.hi[0] - in->wheel.lo[0]);
		}
		else
		{
			out->wheelAsDriver = 1;
		}
	}
	if (!out->hasWheel)
	{
		memcpy(out->wheelCenter, dummyWheel, sizeof(dummyWheel));
		out->wheelRadius = RLDDUM_WHEEL_RADIUS;
	}

	if (in->driver.valid)
	{
		for (a = 0; a < 3; a++)
		{
			out->driverLo[a] = out->seat[a] + out->size * (in->driver.lo[a] + out->shift[a] - out->seat[a]);
			out->driverHi[a] = out->seat[a] + out->size * (in->driver.hi[a] + out->shift[a] - out->seat[a]);
		}
	}

	return 1;
}

// One position onto the dummy: the shift, then --size about the seat for the
// driver and the steering wheel (the kart keeps its size: the game draws the
// wheel sprites at the fixed points of a retail kart). About the seat, not the
// hip: the seat contact stays where it is at any size (about the hip (0, 16,
// 0) a corner on the seat, 5.8 below it, would sink 2.9 into the floor at
// 150 % and float 2.9 above it at 50 %). Seat and pose pivot are the same
// point, so a posed corner at size s is seat + s x (its pose at 100 % - seat)
// (RldDum_PoseFit scales the steering column and the grip with the size).
static void RldDum_Place(const struct RldDumFitResult *fit, int group, const double in[3], double out[3])
{
	int a;

	for (a = 0; a < 3; a++)
	{
		const double v = in[a] + fit->shift[a];

		out[a] = (group == RLDDUM_GROUP_KART) ? v : (fit->seat[a] + fit->size * (v - fit->seat[a]));
	}
}

//========================================================================================
// THE POSES
//========================================================================================

struct RldDumPose
{
	double roll;            // the driver, degrees about Z through the seat: + moves the top toward +X
	double yaw;             // degrees about Y: + turns the front (+Z) toward +X
	double pitch;           // degrees about X: + moves the top forward (+Z)
	double stretch;         // height factor about the seat
	double wheelTurn;       // the steering wheel, degrees about its column: + moves its top toward +X
	int grip;               // this frame carries the hands with the steering wheel (the turn)
	double seat[3];         // the driver's pivot
	double wheelCenter[3];  // the column (along +Z) passes through here
	double gripRadius;      // the ring the hands hold; 0 = none (no grip)
	double gripNear;        // full grip up to this distance from the ring
	double gripFar;         // no grip from this distance on
	double bendHeight;      // > 0: the lean bends over this height above the seat (RLDDUM_BEND_HEIGHT)
	int wheelAsDriver;      // the steering wheel group leans with the driver
	double trig[8];         // sine and cosine of pitch, yaw, roll, wheelTurn
};

// The pose of frame 0..46 (turn 0..20, reverse 21..27, bump 28..42, jump
// 43..46) about the dummy's own seat and steering wheel; RldDum_PoseFit moves
// the pivots onto a placed model.
//   turn 0..20     frame 0 fully toward +X, 10 neutral, 20 mirrored, along
//                  the retail curve (s_rldDumRetailTurn): driver roll
//                  RLDDUM_TURN_ROLL, the steering wheel RLDDUM_TURN_WHEEL,
//                  the hands with the wheel
//   reverse 0..6   the driver alone turns up to 10 degrees toward +X and leans
//                  8 back; the steering wheel stays
//   bump 0..14     the driver nods along the retail course at about 2/3 of
//                  its swing, squashed to 0.93 at frame 8
//   jump 0..3      the driver leans back 0, 5, 12, 15 degrees and stretches to 1.05
static void RldDum_PoseFrame(u32 frame, struct RldDumPose *pose)
{
	static const double seat[3] = RLDDUM_SEAT;
	static const double wheel[3] = RLDDUM_WHEEL_CENTER;
	u32 first = 0;
	int anim;

	memset(pose, 0, sizeof(*pose));
	pose->stretch = 1.0;
	memcpy(pose->seat, seat, sizeof(seat));
	memcpy(pose->wheelCenter, wheel, sizeof(wheel));
	pose->gripRadius = 0.0;
	pose->gripNear = RLDDUM_GRIP_NEAR;
	pose->gripFar = RLDDUM_GRIP_FAR;

	for (anim = 0; anim < RLDDUM_ANIM_COUNT; anim++)
	{
		const u32 count = s_rldDumFrames[anim];

		if (frame < (first + count))
		{
			const u32 k = frame - first;

			if (anim == 0)
			{
				// frames 0..10 from the table, 11..20 mirrored
				const double turn = (k <= (u32)RLDDUM_NEUTRAL_FRAME) ? s_rldDumRetailTurn[k] : -s_rldDumRetailTurn[2u * RLDDUM_NEUTRAL_FRAME - k];

				pose->roll = RLDDUM_TURN_ROLL * (turn / RLDDUM_RETAIL_WHEEL_TURN);
				pose->wheelTurn = RLDDUM_TURN_WHEEL * (turn / RLDDUM_RETAIL_WHEEL_TURN);
				pose->grip = 1;
			}
			else if (anim == 1)
			{
				pose->yaw = 10.0 * (double)k / 6.0;
				pose->pitch = -8.0 * (double)k / 6.0;
			}
			else if (anim == 2)
			{
				pose->pitch = s_rldDumBumpPitch[(k < 15u) ? k : 14u];
				pose->stretch = s_rldDumBumpStretch[(k < 15u) ? k : 14u];
			}
			else
			{
				pose->pitch = s_rldDumJumpPitch[(k < 4u) ? k : 3u];
				pose->stretch = s_rldDumJumpStretch[(k < 4u) ? k : 3u];
			}
			break;
		}

		first += count;
	}

	RldDum_SinCos(pose->pitch, &pose->trig[0], &pose->trig[1]);
	RldDum_SinCos(pose->yaw, &pose->trig[2], &pose->trig[3]);
	RldDum_SinCos(pose->roll, &pose->trig[4], &pose->trig[5]);
	RldDum_SinCos(pose->wheelTurn, &pose->trig[6], &pose->trig[7]);
}

// The pivots of a placed model (after RldDum_PoseFrame): the seat, the column
// through the model's own steering wheel, the grip on its ring, the bend - all
// at the model's --size. Without a steering wheel there is no grip.
static void RldDum_PoseFit(struct RldDumPose *pose, const struct RldDumFitResult *fit)
{
	memcpy(pose->seat, fit->seat, sizeof(pose->seat));
	memcpy(pose->wheelCenter, fit->wheelCenter, sizeof(pose->wheelCenter));
	pose->gripRadius = fit->hasWheel ? fit->wheelRadius : 0.0;
	pose->gripNear = RLDDUM_GRIP_NEAR * fit->size;
	pose->gripFar = RLDDUM_GRIP_FAR * fit->size;
	pose->bendHeight = fit->bendHeight * fit->size;
	pose->wheelAsDriver = fit->wheelAsDriver;
}

// The driver's lean about the seat: pitch, yaw, roll as RldMk_ApplyPoseTrig.
// With a bend height, a corner h above the seat turns by the angles x
// min(h / bendHeight, 1) (none at or below the seat).
static void RldDum_Lean(const struct RldDumPose *pose, const double in[3], double out[3])
{
	double sp = pose->trig[0];
	double cp = pose->trig[1];
	double sy = pose->trig[2];
	double cy = pose->trig[3];
	double sr = pose->trig[4];
	double cr = pose->trig[5];
	double x = in[0] - pose->seat[0];
	double y = (in[1] - pose->seat[1]) * pose->stretch;
	double z = in[2] - pose->seat[2];
	double t;

	if ((pose->bendHeight > 0.0) && ((in[1] - pose->seat[1]) < pose->bendHeight))
	{
		const double h = in[1] - pose->seat[1];
		const double k = (h > 0.0) ? (h / pose->bendHeight) : 0.0;

		RldDum_SinCos(pose->pitch * k, &sp, &cp);
		RldDum_SinCos(pose->yaw * k, &sy, &cy);
		RldDum_SinCos(pose->roll * k, &sr, &cr);
	}

	// pitch about X: the top toward +Z
	t = y * cp - z * sp;
	z = y * sp + z * cp;
	y = t;

	// yaw about Y: the front toward +X
	t = x * cy + z * sy;
	z = -x * sy + z * cy;
	x = t;

	// roll about Z: the top toward +X
	t = x * cr + y * sr;
	y = -x * sr + y * cr;
	x = t;

	out[0] = x + pose->seat[0];
	out[1] = y + pose->seat[1];
	out[2] = z + pose->seat[2];
}

// The steering wheel's turn about its column (along +Z through wheelCenter):
// the top toward +X, as the roll.
static void RldDum_Turn(const struct RldDumPose *pose, const double in[3], double out[3])
{
	const double s = pose->trig[6];
	const double c = pose->trig[7];
	const double x = in[0] - pose->wheelCenter[0];
	const double y = in[1] - pose->wheelCenter[1];

	out[0] = x * c + y * s + pose->wheelCenter[0];
	out[1] = -x * s + y * c + pose->wheelCenter[1];
	out[2] = in[2];
}

// How much of the wheel's turn a driver corner takes (RLDDUM_GRIP_NEAR/FAR):
// its distance from the ring, a circle of gripRadius about the column in the
// plane of RLDDUM_WHEEL_NORMAL.
static double RldDum_GripWeight(const struct RldDumPose *pose, const double in[3])
{
	static const double normal[3] = RLDDUM_WHEEL_NORMAL;
	const double length = RldDum_Sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
	double q[3];
	double h = 0.0;
	double r2 = 0.0;
	double d;
	int a;

	if (!pose->grip || !(pose->gripRadius > 0.0) || !(pose->gripFar > pose->gripNear))
	{
		return 0.0;
	}

	for (a = 0; a < 3; a++)
	{
		q[a] = in[a] - pose->wheelCenter[a];
		h += q[a] * normal[a] / length;
	}
	for (a = 0; a < 3; a++)
	{
		const double r = q[a] - h * normal[a] / length;

		r2 += r * r;
	}

	d = RldDum_Sqrt(r2) - pose->gripRadius;
	d = RldDum_Sqrt(h * h + d * d);
	if (d <= pose->gripNear)
	{
		return 1.0;
	}
	if (d >= pose->gripFar)
	{
		return 0.0;
	}
	return (pose->gripFar - d) / (pose->gripFar - pose->gripNear);
}

// One position of `group` (RLDDUM_GROUP_*) in this pose. The kart stays; a
// part that does not move in this frame is copied bit for bit (frame 10 is
// the input itself, the steering wheel outside the turn too). A steering
// wheel group that is none (RldDumFitResult.wheelAsDriver) leans as the driver.
static void RldDum_ApplyPose(const struct RldDumPose *pose, int group, const double in[3], double out[3])
{
	const int leans = (pose->roll != 0.0) || (pose->yaw != 0.0) || (pose->pitch != 0.0) || (pose->stretch != 1.0);
	const int turns = (pose->wheelTurn != 0.0);

	if ((group == RLDDUM_GROUP_WHEEL) && pose->wheelAsDriver)
	{
		group = RLDDUM_GROUP_DRIVER;
	}

	if (group == RLDDUM_GROUP_WHEEL)
	{
		if (turns)
		{
			RldDum_Turn(pose, in, out);
		}
		else
		{
			memcpy(out, in, 3u * sizeof(double));
		}
		return;
	}

	if (group != RLDDUM_GROUP_DRIVER)
	{
		memcpy(out, in, 3u * sizeof(double));
		return;
	}

	if (leans)
	{
		RldDum_Lean(pose, in, out);
	}
	else
	{
		memcpy(out, in, 3u * sizeof(double));
	}

	if (turns || leans)
	{
		const double w = RldDum_GripWeight(pose, in);

		if (w > 0.0)
		{
			double held[3];
			int a;

			RldDum_Turn(pose, in, held);
			for (a = 0; a < 3; a++)
			{
				out[a] += w * (held[a] - out[a]);
			}
		}
	}
}

//========================================================================================
// THE PREVIEW MESH
//========================================================================================
//
// A simple, readable dummy for Reload Studio, in game units, to be drawn
// without culling like the other boxes of the preview. Every part is a closed
// solid wound outward (right-hand normal = outside):
//   ground     a plate under everything, y -0.5..0
//   floor      the outline on the bottom line, 1.5 thick: its long sides cut
//              at z -42, -20, 0, 20, 40 so that it follows the line (5.7 at z
//              0, 10.9 at z -42; behind -42 the engine block, whose underside
//              is not drawn - the floor keeps the height of z -42 there)
//   sides      a wall 2 thick around that outline, from the floor up to the
//              top of the axle patches, y 21.4 - the kart's own height is not
//              measured (only its box: up to 52.1, the engine and wing)
//   seat       a cushion at the seat contact (x +-7.2, z -7.5..9.3), its top
//              at the seat point, its bottom in the floor
//   wheels     with wheels = 1: the four the game draws, cylinders of radius
//              16 at the wheel points, 10 wide (the game draws flat sprites)
//   steering   last, so that a preview can turn it alone
//   wheel      (RldDum_MeshSteeringFirst): a column from the center to the hub
//              and the ring, radius 10.5 in its leaning plane
// Positions and triangles come in this fixed order; with position or triangle
// NULL only the counts are given.

#define RLDDUM_MESH_TRIANGLES_MAX 600u
#define RLDDUM_MESH_TIRE_SEGMENTS 10
#define RLDDUM_MESH_RING_SEGMENTS 12
#define RLDDUM_MESH_OUTLINE_MAX 32
static const double s_rldDumFloorCuts[5] = {-42.0, -20.0, 0.0, 20.0, 40.0};

#define RLDDUM_RGB_GROUND 0x6E6E6Eu
#define RLDDUM_RGB_FLOOR 0x5A5A5Au
#define RLDDUM_RGB_SIDES 0x8C8C8Cu
#define RLDDUM_RGB_SEAT 0x787878u
#define RLDDUM_RGB_TIRE 0x2E2E2Eu
#define RLDDUM_RGB_RIM 0x4A4A4Au
#define RLDDUM_RGB_COLUMN 0x464646u
#define RLDDUM_RGB_RING 0x363636u

struct RldDumMeshOut
{
	double *position;
	u32 positionMax;
	u32 *triangle;
	u32 *color;
	u32 triangleMax;
	u32 positions;
	u32 triangles;
	int full;   // a buffer was too small
};

static u32 RldDum_MeshPoint(struct RldDumMeshOut *m, double x, double y, double z)
{
	const u32 index = m->positions++;

	if (m->position != NULL)
	{
		if (index >= m->positionMax)
		{
			m->full = 1;
		}
		else
		{
			m->position[3u * index + 0u] = x;
			m->position[3u * index + 1u] = y;
			m->position[3u * index + 2u] = z;
		}
	}
	return index;
}

static void RldDum_MeshTri(struct RldDumMeshOut *m, u32 a, u32 b, u32 c, u32 rgb)
{
	const u32 index = m->triangles++;

	if (m->triangle != NULL)
	{
		if (index >= m->triangleMax)
		{
			m->full = 1;
		}
		else
		{
			m->triangle[3u * index + 0u] = a;
			m->triangle[3u * index + 1u] = b;
			m->triangle[3u * index + 2u] = c;
			if (m->color != NULL)
			{
				m->color[index] = rgb;
			}
		}
	}
}

// a b c d counter-clockwise seen from outside
static void RldDum_MeshQuad(struct RldDumMeshOut *m, u32 a, u32 b, u32 c, u32 d, u32 rgb)
{
	RldDum_MeshTri(m, a, b, c, rgb);
	RldDum_MeshTri(m, a, c, d, rgb);
}

static void RldDum_MeshBox(struct RldDumMeshOut *m, double x0, double y0, double z0, double x1, double y1, double z1, u32 rgb)
{
	const u32 p = RldDum_MeshPoint(m, x0, y0, z0);

	RldDum_MeshPoint(m, x1, y0, z0);
	RldDum_MeshPoint(m, x1, y1, z0);
	RldDum_MeshPoint(m, x0, y1, z0);
	RldDum_MeshPoint(m, x0, y0, z1);
	RldDum_MeshPoint(m, x1, y0, z1);
	RldDum_MeshPoint(m, x1, y1, z1);
	RldDum_MeshPoint(m, x0, y1, z1);

	RldDum_MeshQuad(m, p + 0u, p + 3u, p + 2u, p + 1u, rgb);   // -z
	RldDum_MeshQuad(m, p + 4u, p + 5u, p + 6u, p + 7u, rgb);   // +z
	RldDum_MeshQuad(m, p + 0u, p + 4u, p + 7u, p + 3u, rgb);   // -x
	RldDum_MeshQuad(m, p + 1u, p + 2u, p + 6u, p + 5u, rgb);   // +x
	RldDum_MeshQuad(m, p + 0u, p + 1u, p + 5u, p + 4u, rgb);   // -y
	RldDum_MeshQuad(m, p + 3u, p + 7u, p + 6u, p + 2u, rgb);   // +y
}

// A prism over a convex polygon in (x, z), counter-clockwise; per corner its
// bottom and top height.
static void RldDum_MeshPrism(struct RldDumMeshOut *m, const double (*xz)[2], const double *bottom, const double *top, int count, double thick, u32 rgb)
{
	double cx = 0.0;
	double cz = 0.0;
	double cb;
	u32 p;
	u32 center;
	int i;

	for (i = 0; i < count; i++)
	{
		cx += xz[i][0];
		cz += xz[i][1];
	}
	cx /= (double)count;
	cz /= (double)count;
	cb = RldDum_BottomAt((cz < RLDDUM_FLOOR_FROM_Z) ? RLDDUM_FLOOR_FROM_Z : cz);

	p = m->positions;
	for (i = 0; i < count; i++)
	{
		RldDum_MeshPoint(m, xz[i][0], bottom[i], xz[i][1]);
		RldDum_MeshPoint(m, xz[i][0], top[i], xz[i][1]);
	}
	center = RldDum_MeshPoint(m, cx, cb, cz);
	RldDum_MeshPoint(m, cx, cb + thick, cz);

	for (i = 0; i < count; i++)
	{
		const u32 a = p + 2u * (u32)i;
		const u32 b = p + 2u * (u32)((i + 1) % count);

		// counter-clockwise in (x, z) is clockwise seen from above (+y)
		RldDum_MeshTri(m, center + 1u, b + 1u, a + 1u, rgb);   // top
		RldDum_MeshTri(m, center, a, b, rgb);                  // bottom
		RldDum_MeshQuad(m, a, a + 1u, b + 1u, b, rgb);         // side
	}
}

// A wall around a convex polygon (counter-clockwise in (x, z)), `thick` inward,
// from bottom[i] to top.
static void RldDum_MeshWall(struct RldDumMeshOut *m, const double (*xz)[2], const double *bottom, double top, double thick, int count, u32 rgb)
{
	const u32 p = m->positions;
	int i;

	for (i = 0; i < count; i++)
	{
		const double *prev = xz[(i + count - 1) % count];
		const double *here = xz[i];
		const double *next = xz[(i + 1) % count];
		double n1[2];
		double n2[2];
		double l1;
		double l2;
		double k;

		// inward normals of the two edges (counter-clockwise: inward is to the left)
		n1[0] = -(here[1] - prev[1]);
		n1[1] = here[0] - prev[0];
		n2[0] = -(next[1] - here[1]);
		n2[1] = next[0] - here[0];
		l1 = RldDum_Sqrt(n1[0] * n1[0] + n1[1] * n1[1]);
		l2 = RldDum_Sqrt(n2[0] * n2[0] + n2[1] * n2[1]);
		n1[0] /= l1;
		n1[1] /= l1;
		n2[0] /= l2;
		n2[1] /= l2;
		k = thick / (1.0 + n1[0] * n2[0] + n1[1] * n2[1]);

		RldDum_MeshPoint(m, here[0], bottom[i], here[1]);                                               // outer bottom
		RldDum_MeshPoint(m, here[0], top, here[1]);                                                     // outer top
		RldDum_MeshPoint(m, here[0] + k * (n1[0] + n2[0]), bottom[i], here[1] + k * (n1[1] + n2[1]));  // inner bottom
		RldDum_MeshPoint(m, here[0] + k * (n1[0] + n2[0]), top, here[1] + k * (n1[1] + n2[1]));        // inner top
	}

	for (i = 0; i < count; i++)
	{
		const u32 a = p + 4u * (u32)i;
		const u32 b = p + 4u * (u32)((i + 1) % count);

		RldDum_MeshQuad(m, a, a + 1u, b + 1u, b, rgb);              // outside
		RldDum_MeshQuad(m, a + 2u, b + 2u, b + 3u, a + 3u, rgb);    // inside
		RldDum_MeshQuad(m, a + 1u, a + 3u, b + 3u, b + 1u, rgb);    // top
		RldDum_MeshQuad(m, a, b, b + 2u, a + 2u, rgb);              // bottom
	}
}

// A cylinder along x: center, radius, half width.
static void RldDum_MeshTire(struct RldDumMeshOut *m, double x, double y, double z, double radius, double half)
{
	const u32 p = m->positions;
	u32 left;
	u32 right;
	int i;

	for (i = 0; i < RLDDUM_MESH_TIRE_SEGMENTS; i++)
	{
		double s;
		double c;

		RldDum_SinCos(360.0 * (double)i / (double)RLDDUM_MESH_TIRE_SEGMENTS, &s, &c);
		RldDum_MeshPoint(m, x - half, y + radius * s, z + radius * c);
		RldDum_MeshPoint(m, x + half, y + radius * s, z + radius * c);
	}
	left = RldDum_MeshPoint(m, x - half, y, z);
	right = RldDum_MeshPoint(m, x + half, y, z);

	for (i = 0; i < RLDDUM_MESH_TIRE_SEGMENTS; i++)
	{
		const u32 a = p + 2u * (u32)i;
		const u32 b = p + 2u * (u32)((i + 1) % RLDDUM_MESH_TIRE_SEGMENTS);

		// the angle runs from +z toward +y: clockwise seen from +x
		RldDum_MeshQuad(m, a, a + 1u, b + 1u, b, RLDDUM_RGB_TIRE);
		RldDum_MeshTri(m, right, b + 1u, a + 1u, RLDDUM_RGB_RIM);
		RldDum_MeshTri(m, left, a, b, RLDDUM_RGB_RIM);
	}
}

// The steering ring: a tube of square section (half size `half`) about the
// circle of `radius` round `center` in the plane of RLDDUM_WHEEL_NORMAL.
static void RldDum_MeshRing(struct RldDumMeshOut *m, const double center[3], double radius, double half)
{
	static const double normal[3] = RLDDUM_WHEEL_NORMAL;
	const double length = RldDum_Sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
	const double n[3] = {normal[0] / length, normal[1] / length, normal[2] / length};
	// u = +x lies in the plane (the normal has no x); v = n x u points up
	const double v[3] = {0.0, n[2], -n[1]};
	const u32 p = m->positions;
	int i;
	int k;

	for (i = 0; i < RLDDUM_MESH_RING_SEGMENTS; i++)
	{
		double s;
		double c;
		double r[3];

		RldDum_SinCos(360.0 * (double)i / (double)RLDDUM_MESH_RING_SEGMENTS, &s, &c);
		r[0] = c;
		r[1] = s * v[1];
		r[2] = s * v[2];

		// the section: (radial, normal) = (+,+) (-,+) (-,-) (+,-)
		for (k = 0; k < 4; k++)
		{
			const double dr = ((k == 0) || (k == 3)) ? half : -half;
			const double dn = (k < 2) ? half : -half;

			RldDum_MeshPoint(m, center[0] + (radius + dr) * r[0] + dn * n[0], center[1] + (radius + dr) * r[1] + dn * n[1],
			                 center[2] + (radius + dr) * r[2] + dn * n[2]);
		}
	}

	for (i = 0; i < RLDDUM_MESH_RING_SEGMENTS; i++)
	{
		const u32 a = p + 4u * (u32)i;
		const u32 b = p + 4u * (u32)((i + 1) % RLDDUM_MESH_RING_SEGMENTS);

		for (k = 0; k < 4; k++)
		{
			const u32 k1 = (u32)((k + 1) % 4);

			// the section runs clockwise seen along the ring (+x toward v)
			RldDum_MeshQuad(m, a + (u32)k, b + (u32)k, b + k1, a + k1, RLDDUM_RGB_RING);
		}
	}
}

static void RldDum_MeshBuild(int wheels, struct RldDumMeshOut *m)
{
	static const double kartBox[6] = RLDDUM_KART_BOX;
	static const double wheel[3] = RLDDUM_WHEEL_CENTER;
	static const double hub[3] = RLDDUM_WHEEL_HUB;
	double outline[RLDDUM_MESH_OUTLINE_MAX][2];
	double floorBottom[RLDDUM_MESH_OUTLINE_MAX];
	double floorTop[RLDDUM_MESH_OUTLINE_MAX];
	int count = 0;
	int i;
	int k;

	// ground
	RldDum_MeshBox(m, -(RLDDUM_TIRE_X + 12.0), -0.5, kartBox[2] - 6.0, RLDDUM_TIRE_X + 12.0, 0.0, RLDDUM_TIRE_FRONT_Z + RLDDUM_TIRE_HALF_SIZE + 6.0,
	               RLDDUM_RGB_GROUND);

	// floor and sides: the outline, its edges cut where they cross the floor
	// cuts (in the order the edge runs)
	for (i = 0; i < RLDDUM_OUTLINE_COUNT; i++)
	{
		const double *a = s_rldDumOutline[i];
		const double *b = s_rldDumOutline[(i + 1) % RLDDUM_OUTLINE_COUNT];

		outline[count][0] = a[0];
		outline[count][1] = a[1];
		count++;
		for (k = 0; k < 5; k++)
		{
			const double z = (b[1] > a[1]) ? s_rldDumFloorCuts[k] : s_rldDumFloorCuts[4 - k];

			if ((count < RLDDUM_MESH_OUTLINE_MAX) && (((z > a[1]) && (z < b[1])) || ((z < a[1]) && (z > b[1]))))
			{
				const double t = (z - a[1]) / (b[1] - a[1]);

				outline[count][0] = a[0] + t * (b[0] - a[0]);
				outline[count][1] = z;
				count++;
			}
		}
	}
	for (i = 0; i < count; i++)
	{
		const double z = outline[i][1];

		floorBottom[i] = RldDum_BottomAt((z < RLDDUM_FLOOR_FROM_Z) ? RLDDUM_FLOOR_FROM_Z : z);
		floorTop[i] = floorBottom[i] + 1.5;
	}
	RldDum_MeshPrism(m, (const double (*)[2])outline, floorBottom, floorTop, count, 1.5, RLDDUM_RGB_FLOOR);
	RldDum_MeshWall(m, (const double (*)[2])outline, floorTop, RLDDUM_AXLE_PATCH_TOP, 2.0, count, RLDDUM_RGB_SIDES);

	// seat: its top is the seat point
	RldDum_MeshBox(m, -RLDDUM_SEAT_HALF_X, RLDDUM_SEAT_FLOOR_Y + 1.5, RLDDUM_SEAT_Z0, RLDDUM_SEAT_HALF_X, RLDDUM_SEAT_Y, RLDDUM_SEAT_Z1, RLDDUM_RGB_SEAT);

	if (wheels)
	{
		RldDum_MeshTire(m, RLDDUM_TIRE_X, RLDDUM_TIRE_Y, RLDDUM_TIRE_FRONT_Z, RLDDUM_TIRE_HALF_SIZE, 5.0);
		RldDum_MeshTire(m, -RLDDUM_TIRE_X, RLDDUM_TIRE_Y, RLDDUM_TIRE_FRONT_Z, RLDDUM_TIRE_HALF_SIZE, 5.0);
		RldDum_MeshTire(m, RLDDUM_TIRE_X, RLDDUM_TIRE_Y, RLDDUM_TIRE_REAR_Z, RLDDUM_TIRE_HALF_SIZE, 5.0);
		RldDum_MeshTire(m, -RLDDUM_TIRE_X, RLDDUM_TIRE_Y, RLDDUM_TIRE_REAR_Z, RLDDUM_TIRE_HALF_SIZE, 5.0);
	}

	// steering wheel: the column to the hub, then the ring
	RldDum_MeshBox(m, wheel[0] - 1.0, wheel[1] - 1.0, wheel[2], wheel[0] + 1.0, wheel[1] + 1.0, hub[2], RLDDUM_RGB_COLUMN);
	RldDum_MeshRing(m, wheel, RLDDUM_WHEEL_RADIUS, 1.0);
}

// The dummy mesh: positions (3 doubles each) and triangles (3 position
// indices each, outward) with a color 0xRRGGBB per triangle. Returns the
// triangle count, *positionCount the position count; 0 when a buffer is too
// small. position or triangle NULL: only the counts (triangleColor may be NULL
// in any case).
static u32 RldDum_Mesh(int wheels, double *position, u32 positionMax, u32 *triangle, u32 *triangleColor, u32 triangleMax, u32 *positionCount)
{
	struct RldDumMeshOut m;

	memset(&m, 0, sizeof(m));
	if ((position != NULL) && (triangle != NULL))
	{
		m.position = position;
		m.positionMax = positionMax;
		m.triangle = triangle;
		m.color = triangleColor;
		m.triangleMax = triangleMax;
	}
	RldDum_MeshBuild(wheels, &m);

	if (positionCount != NULL)
	{
		*positionCount = m.full ? 0u : m.positions;
	}
	return m.full ? 0u : m.triangles;
}

// The first position of the steering wheel in RldDum_Mesh: it and all after
// it are RLDDUM_GROUP_WHEEL (to turn with RldDum_ApplyPose), all before it
// the kart.
static u32 RldDum_MeshSteeringFirst(int wheels)
{
	u32 all = 0;
	struct RldDumMeshOut m;
	static const double wheel[3] = RLDDUM_WHEEL_CENTER;

	RldDum_Mesh(wheels, NULL, 0u, NULL, NULL, 0u, &all);
	memset(&m, 0, sizeof(m));
	RldDum_MeshBox(&m, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 0u);
	RldDum_MeshRing(&m, wheel, RLDDUM_WHEEL_RADIUS, 1.0);
	return all - m.positions;
}

//========================================================================================
// SELF-TEST
//========================================================================================

static int RldDum_TestFailed;

static void RldDum_Check(void (*report)(const char *line), int ok, const char *what)
{
	char line[256];

	snprintf(line, sizeof(line), "  %s dummy: %s", ok ? "ok  " : "FAIL", what);
	if (report != NULL)
	{
		report(line);
	}
	if (!ok)
	{
		RldDum_TestFailed++;
	}
}

static int RldDum_Near(const double *a, const double *b, int count, double tolerance)
{
	int i;

	for (i = 0; i < count; i++)
	{
		const double d = a[i] - b[i];

		if (!(d <= tolerance) || !(d >= -tolerance))
		{
			return 0;
		}
	}
	return 1;
}

// Union-find root for the mesh check.
static u32 RldDum_TestRoot(u32 *parent, u32 p)
{
	while (parent[p] != p)
	{
		parent[p] = parent[parent[p]];
		p = parent[p];
	}
	return p;
}

// The mesh: in budget, no degenerate triangle, every part a closed surface
// wound outward (every edge twice, once each way; every part's volume > 0).
static int RldDum_TestMesh(int wheels)
{
	enum { POSITIONS = 512, TRIANGLES = 1024 };
	static double position[3 * POSITIONS];
	static u32 triangle[3 * TRIANGLES];
	static u32 color[TRIANGLES];
	static u32 parent[POSITIONS];
	static double volume[POSITIONS];
	u32 positions = 0;
	u32 needed = 0;
	const u32 count = RldDum_Mesh(wheels, position, POSITIONS, triangle, color, TRIANGLES, &positions);
	const u32 counted = RldDum_Mesh(wheels, NULL, 0u, NULL, NULL, 0u, &needed);
	u32 t;
	u32 u;
	u32 p;

	if ((count == 0u) || (count >= RLDDUM_MESH_TRIANGLES_MAX) || (count != counted) || (positions != needed) || (positions > POSITIONS))
	{
		return 0;
	}
	if (RldDum_Mesh(wheels, position, positions - 1u, triangle, color, TRIANGLES, &needed) != 0u)
	{
		return 0;   // too small a buffer has to be refused
	}
	RldDum_Mesh(wheels, position, POSITIONS, triangle, color, TRIANGLES, &positions);

	for (p = 0; p < positions; p++)
	{
		parent[p] = p;
		volume[p] = 0.0;
	}

	for (t = 0; t < count; t++)
	{
		const u32 *c = &triangle[3u * t];
		const double *a = &position[3u * c[0]];
		const double *b = &position[3u * c[1]];
		const double *d = &position[3u * c[2]];
		const double e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
		const double e2[3] = {d[0] - a[0], d[1] - a[1], d[2] - a[2]};
		const double n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
		u32 k;

		if ((c[0] >= positions) || (c[1] >= positions) || (c[2] >= positions) || (c[0] == c[1]) || (c[1] == c[2]) || (c[0] == c[2]) ||
		    ((n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) < 1.0e-6) || (color[t] > 0xFFFFFFu))
		{
			return 0;
		}

		// every directed edge once, and its reverse once
		for (k = 0; k < 3u; k++)
		{
			const u32 from = c[k];
			const u32 to = c[(k + 1u) % 3u];
			u32 same = 0;
			u32 back = 0;

			for (u = 0; u < count; u++)
			{
				u32 j;

				for (j = 0; j < 3u; j++)
				{
					same += (triangle[3u * u + j] == from) && (triangle[3u * u + (j + 1u) % 3u] == to);
					back += (triangle[3u * u + j] == to) && (triangle[3u * u + (j + 1u) % 3u] == from);
				}
			}
			if ((same != 1u) || (back != 1u))
			{
				return 0;
			}
		}

		parent[RldDum_TestRoot(parent, c[1])] = RldDum_TestRoot(parent, c[0]);
		parent[RldDum_TestRoot(parent, c[2])] = RldDum_TestRoot(parent, c[0]);
	}

	for (t = 0; t < count; t++)
	{
		const u32 *c = &triangle[3u * t];
		const double *a = &position[3u * c[0]];
		const double *b = &position[3u * c[1]];
		const double *d = &position[3u * c[2]];

		volume[RldDum_TestRoot(parent, c[0])] += (a[0] * (b[1] * d[2] - b[2] * d[1]) - a[1] * (b[0] * d[2] - b[2] * d[0]) + a[2] * (b[0] * d[1] - b[1] * d[0])) / 6.0;
	}
	for (p = 0; p < positions; p++)
	{
		if ((RldDum_TestRoot(parent, p) == p) && !(volume[p] > 0.0))
		{
			return 0;
		}
	}

	return 1;
}

// The number of failures; report gets one line per check (may be NULL).
static int RldDum_SelfTest(void (*report)(const char *line))
{
	static const double kartBox[6] = RLDDUM_KART_BOX;
	static const double seat[3] = RLDDUM_SEAT;
	static const double dummyWheel[3] = RLDDUM_WHEEL_CENTER;
	struct RldDumFitInput in;
	struct RldDumFitResult fit;
	struct RldDumFitResult again;
	struct RldDumPose pose;
	struct RldDumPose pose20;
	double s;
	double c;
	int ok;
	int a;
	u32 f;

	RldDum_TestFailed = 0;

	// ARITHMETIC
	RldDum_SinCos(30.0, &s, &c);
	ok = (s > 0.5 - 1.0e-15) && (s < 0.5 + 1.0e-15);
	RldDum_SinCos(-150.0, &s, &c);
	ok = ok && (s > -0.5 - 1.0e-14) && (s < -0.5 + 1.0e-14) && (c < -0.866025403784438) && (c > -0.866025403784439);
	RldDum_SinCos(390.0, &s, &c);
	ok = ok && (s > 0.5 - 1.0e-14) && (s < 0.5 + 1.0e-14);
	ok = ok && (RldDum_Sqrt(2.25) == 1.5) && (RldDum_Sqrt(0.0) == 0.0);
	ok = ok && (RldDum_BottomAt(-60.0) == 36.3) && (RldDum_BottomAt(5.0) > 5.79) && (RldDum_BottomAt(5.0) < 5.81) && (RldDum_BottomAt(99.0) == 7.4);
	RldDum_Check(report, ok, "sine, cosine, square root and the bottom line");

	// THE FIT: a test kart moved off the dummy and a driver above it
	memset(&in, 0, sizeof(in));
	in.kart.valid = 1;
	in.driver.valid = 1;
	in.wheel.valid = 1;
	in.model.valid = 1;
	in.wheels = 1;
	for (a = 0; a < 3; a++)
	{
		const double off[3] = {3.0, 2.0, -5.0};

		in.kart.lo[a] = kartBox[a] + off[a];
		in.kart.hi[a] = kartBox[3 + a] + off[a];
		in.wheel.lo[a] = dummyWheel[a] + off[a] - 10.0;
		in.wheel.hi[a] = dummyWheel[a] + off[a] + 10.0;
		in.driver.lo[a] = in.kart.lo[a] + 10.0;
		in.driver.hi[a] = in.kart.hi[a] + 30.0;
		in.model.lo[a] = in.kart.lo[a];
		in.model.hi[a] = in.driver.hi[a];
	}
	ok = RldDum_Fit(&in, &fit) && (fit.basis == RLDDUM_FIT_KART);
	{
		// the dummy kart, moved to x 0 (the measured box is 0.2 off-center)
		const double want[6] = {kartBox[0] - 0.2, kartBox[1], kartBox[2], kartBox[3] - 0.2, kartBox[4], kartBox[5]};
		const double shift[3] = {-3.2, -2.0, 5.0};
		const double wheelAt[3] = {-0.2, 38.0, 24.0};

		ok = ok && RldDum_Near(fit.placed, want, 6, 1.0e-9) && RldDum_Near(fit.shift, shift, 3, 1.0e-9);
		ok = ok && (fit.lengthDelta < 1.0e-9) && (fit.lengthDelta > -1.0e-9) && RldDum_Near(fit.wheelCenter, wheelAt, 3, 1.0e-9);
		ok = ok && (fit.hasWheel == 1) && (fit.wheelRadius == 10.0) && (fit.size == 1.0);
	}
	RldDum_Check(report, ok, "fit: a moved kart lands on the dummy kart (x centered, bottom 5.6, z middle 2.1)");

	in.wheels = 0;
	ok = RldDum_Fit(&in, &fit) && (fit.basis == RLDDUM_FIT_MODEL) && (fit.placed[1] == 0.0) &&
	     ((0.5 * (fit.placed[2] + fit.placed[5])) > (RLDDUM_KART_MID_Z - 1.0e-9)) && ((0.5 * (fit.placed[2] + fit.placed[5])) < (RLDDUM_KART_MID_Z + 1.0e-9)) &&
	     (fit.shift[0] > -3.2 - 1.0e-9) && (fit.shift[0] < -3.2 + 1.0e-9);
	in.kart.valid = 0;
	in.wheel.valid = 0;
	ok = ok && RldDum_Fit(&in, &fit) && (fit.basis == RLDDUM_FIT_MODEL) && !fit.hasWheel && RldDum_Near(fit.wheelCenter, dummyWheel, 3, 0.0);
	in.model.valid = 0;
	ok = ok && !RldDum_Fit(&in, &fit);
	RldDum_Check(report, ok, "fit: --wheels off puts the model on the ground; no box, no fit");

	// --size about the seat: the seat stays, the kart keeps its size
	in.kart.valid = 1;
	in.wheel.valid = 1;
	in.model.valid = 1;
	in.wheels = 1;
	in.sizePercent = 150u;
	ok = RldDum_Fit(&in, &fit) && (fit.size == 1.5);
	{
		const double onSeat[3] = {seat[0] - fit.shift[0], seat[1] - fit.shift[1], seat[2] - fit.shift[2]};
		const double kartCorner[3] = {in.kart.lo[0], in.kart.lo[1], in.kart.lo[2]};
		double out[3];
		double want[3];

		RldDum_Place(&fit, RLDDUM_GROUP_DRIVER, onSeat, out);
		ok = ok && RldDum_Near(out, seat, 3, 1.0e-9);
		RldDum_Place(&fit, RLDDUM_GROUP_KART, kartCorner, out);
		want[0] = fit.placed[0];
		want[1] = fit.placed[1];
		want[2] = fit.placed[2];
		ok = ok && RldDum_Near(out, want, 3, 1.0e-9);
	}
	RldDum_Check(report, ok, "place: --size about the seat, the kart unscaled");

	// THE POSES
	{
		const double test[5][3] = {{-12.0, 38.0, 30.0}, {9.0, 61.0, 4.0}, {0.0, RLDDUM_SEAT_Y, 0.9}, {25.0, 7.0, -40.0}, {-10.5, 38.0, 24.0}};
		struct RldDumFitResult held;
		int group;
		int i;

		memset(&held, 0, sizeof(held));
		held.size = 1.0;
		memcpy(held.seat, seat, sizeof(seat));
		memcpy(held.wheelCenter, dummyWheel, sizeof(dummyWheel));
		held.hasWheel = 1;
		held.wheelRadius = RLDDUM_WHEEL_RADIUS;

		// frame 10: the input itself, bit for bit
		RldDum_PoseFrame(RLDDUM_NEUTRAL_FRAME, &pose);
		RldDum_PoseFit(&pose, &held);
		ok = 1;
		for (group = 0; group < 3; group++)
		{
			for (i = 0; i < 5; i++)
			{
				double out[3];

				RldDum_ApplyPose(&pose, group, test[i], out);
				ok = ok && (memcmp(out, test[i], sizeof(out)) == 0);
			}
		}
		RldDum_Check(report, ok, "pose: frame 10 is the identity");

		// frame 20 mirrors frame 0 about x = 0
		RldDum_PoseFrame(0u, &pose);
		RldDum_PoseFit(&pose, &held);
		RldDum_PoseFrame(20u, &pose20);
		RldDum_PoseFit(&pose20, &held);
		ok = (pose.roll == RLDDUM_TURN_ROLL) && (pose.wheelTurn == RLDDUM_TURN_WHEEL) && (pose20.roll == -RLDDUM_TURN_ROLL) && pose.grip;
		for (group = 0; group < 3; group++)
		{
			for (i = 0; i < 5; i++)
			{
				const double mirrored[3] = {-test[i][0], test[i][1], test[i][2]};
				double out[3];
				double out20[3];

				RldDum_ApplyPose(&pose, group, test[i], out);
				RldDum_ApplyPose(&pose20, group, mirrored, out20);
				out20[0] = -out20[0];
				ok = ok && RldDum_Near(out, out20, 3, 1.0e-12);
			}
		}
		RldDum_Check(report, ok, "pose: frame 20 mirrors frame 0");

		// the steering wheel's center and the seat stay in every frame; the
		// steering wheel stands still outside the turn; the turn eases toward 10
		ok = 1;
		for (f = 0; f < RLDDUM_FRAMES_TOTAL; f++)
		{
			double out[3];
			const double away[3] = {5.0, 30.0, 26.0};

			RldDum_PoseFrame(f, &pose);
			RldDum_PoseFit(&pose, &held);
			RldDum_ApplyPose(&pose, RLDDUM_GROUP_WHEEL, dummyWheel, out);
			ok = ok && (memcmp(out, dummyWheel, sizeof(out)) == 0);
			RldDum_ApplyPose(&pose, RLDDUM_GROUP_DRIVER, seat, out);
			ok = ok && (memcmp(out, seat, sizeof(out)) == 0);
			RldDum_ApplyPose(&pose, RLDDUM_GROUP_WHEEL, away, out);
			ok = ok && ((f < 21u) || (memcmp(out, away, sizeof(out)) == 0));
			ok = ok && ((f >= 10u) || ((pose.wheelTurn > 0.0) && (pose.roll > 0.0)));
			if ((f > 0u) && (f <= 10u))
			{
				struct RldDumPose before;

				RldDum_PoseFrame(f - 1u, &before);
				ok = ok && (before.wheelTurn > pose.wheelTurn);
			}
		}
		RldDum_Check(report, ok, "pose: steering wheel center and seat stay in all 47 frames, the wheel only turns in the turn");

		// the grip: a corner on the ring goes with the wheel, one far away leans
		// with the driver
		RldDum_PoseFrame(0u, &pose);
		RldDum_PoseFit(&pose, &held);
		{
			const double onRing[3] = {-10.5, 38.0, 24.0};
			const double far[3] = {0.0, 80.0, 0.0};
			double out[3];
			double want[3];

			RldDum_ApplyPose(&pose, RLDDUM_GROUP_DRIVER, onRing, out);
			RldDum_ApplyPose(&pose, RLDDUM_GROUP_WHEEL, onRing, want);
			ok = RldDum_Near(out, want, 3, 1.0e-12);
			RldDum_ApplyPose(&pose, RLDDUM_GROUP_DRIVER, far, out);
			RldDum_Lean(&pose, far, want);
			ok = ok && (memcmp(out, want, sizeof(out)) == 0);
			held.hasWheel = 0;
			RldDum_PoseFit(&pose, &held);
			RldDum_ApplyPose(&pose, RLDDUM_GROUP_DRIVER, onRing, out);
			RldDum_Lean(&pose, onRing, want);
			ok = ok && (memcmp(out, want, sizeof(out)) == 0);
			held.hasWheel = 1;
		}
		RldDum_Check(report, ok, "pose: the hands go with the steering wheel, nothing follows a model without one");

		// at any size a posed corner is seat + s x (its pose at 100 % - seat)
		ok = RldDum_Fit(&in, &fit);
		in.sizePercent = 100u;
		ok = ok && RldDum_Fit(&in, &again);
		for (f = 0; f < RLDDUM_FRAMES_TOTAL; f++)
		{
			for (group = 1; group < 3; group++)
			{
				for (i = 0; i < 5; i++)
				{
					struct RldDumPose big;
					double unshifted[3];
					double placed[3];
					double normal[3];
					double out[3];
					double posed[3];
					double want[3];

					for (a = 0; a < 3; a++)
					{
						unshifted[a] = test[i][a] - again.shift[a];
					}
					RldDum_Place(&again, group, unshifted, normal);
					RldDum_Place(&fit, group, unshifted, placed);
					RldDum_PoseFrame(f, &pose);
					RldDum_PoseFit(&pose, &again);
					RldDum_PoseFrame(f, &big);
					RldDum_PoseFit(&big, &fit);
					RldDum_ApplyPose(&pose, group, normal, posed);
					RldDum_ApplyPose(&big, group, placed, out);
					for (a = 0; a < 3; a++)
					{
						want[a] = seat[a] + 1.5 * (posed[a] - seat[a]);
					}
					ok = ok && RldDum_Near(out, want, 3, 1.0e-9);
				}
			}
		}
		RldDum_Check(report, ok, "pose: at --size 150 every frame is the 100 % frame scaled about the seat");
	}

	// WITHOUT KART WHEELS: a motorbike-like model, its driver from y 0.35 up,
	// 46 wide and 75 long, a handlebar in front. At 50, 100 and 200 % no corner
	// of driver or handlebar goes below the ground in any frame, and frame 20
	// mirrors frame 0 with the bend as well.
	{
		static const u32 sizes[3] = {50u, 100u, 200u};
		static const double gx[3] = {-23.0, 0.0, 23.0};
		static const double gy[6] = {0.35, 3.0, 10.0, 24.0, 50.0, 92.6};
		static const double gz[3] = {-16.6, 20.0, 58.3};
		struct RldDumFitInput bike;
		double lowest = 1.0e30;
		int k;
		int ix;
		int iy;
		int iz;

		memset(&bike, 0, sizeof(bike));
		bike.kart.valid = 1;
		bike.driver.valid = 1;
		bike.wheel.valid = 1;
		bike.model.valid = 1;
		for (a = 0; a < 3; a++)
		{
			const double kartLo[3] = {-17.4, 0.0, -54.1};
			const double kartHi[3] = {17.4, 51.0, 28.7};
			const double wheelLo[3] = {-8.4, 41.5, 29.6};
			const double wheelHi[3] = {8.4, 56.5, 34.1};

			bike.kart.lo[a] = kartLo[a];
			bike.kart.hi[a] = kartHi[a];
			bike.driver.lo[a] = gx[0] * (a == 0) + gy[0] * (a == 1) + gz[0] * (a == 2);
			bike.driver.hi[a] = gx[2] * (a == 0) + gy[5] * (a == 1) + gz[2] * (a == 2);
			bike.wheel.lo[a] = wheelLo[a];
			bike.wheel.hi[a] = wheelHi[a];
			bike.model.lo[a] = (bike.driver.lo[a] < kartLo[a]) ? bike.driver.lo[a] : kartLo[a];
			bike.model.hi[a] = (bike.driver.hi[a] > kartHi[a]) ? bike.driver.hi[a] : kartHi[a];
		}
		bike.wheels = 0;

		ok = 1;
		for (k = 0; k < 3; k++)
		{
			bike.sizePercent = sizes[k];
			ok = ok && RldDum_Fit(&bike, &fit) && fit.hasWheel && (fit.bendHeight >= RLDDUM_BEND_HEIGHT) && (fit.seat[0] == 0.0) &&
			     (fit.seat[1] == 0.35) && (fit.seat[2] > 20.849) && (fit.seat[2] < 20.851);
			for (f = 0; f < RLDDUM_FRAMES_TOTAL; f++)
			{
				RldDum_PoseFrame(f, &pose);
				RldDum_PoseFit(&pose, &fit);
				RldDum_PoseFrame((f <= 20u) ? (20u - f) : f, &pose20);
				RldDum_PoseFit(&pose20, &fit);
				for (ix = 0; ix < 3; ix++)
				{
					for (iy = 0; iy < 6; iy++)
					{
						for (iz = 0; iz < 3; iz++)
						{
							const double v[3] = {gx[ix], gy[iy], gz[iz]};
							const double w[3] = {bike.wheel.lo[0] + 8.4 * (double)ix, bike.wheel.lo[1] + 3.0 * (double)iy, bike.wheel.lo[2] + 2.25 * (double)iz};
							double placed[3];
							double out[3];
							double mirrored[3];

							RldDum_Place(&fit, RLDDUM_GROUP_DRIVER, v, placed);
							RldDum_ApplyPose(&pose, RLDDUM_GROUP_DRIVER, placed, out);
							lowest = (out[1] < lowest) ? out[1] : lowest;
							if (f <= 20u)
							{
								placed[0] = -placed[0];
								RldDum_ApplyPose(&pose20, RLDDUM_GROUP_DRIVER, placed, mirrored);
								mirrored[0] = -mirrored[0];
								ok = ok && RldDum_Near(out, mirrored, 3, 1.0e-12);
							}
							RldDum_Place(&fit, RLDDUM_GROUP_WHEEL, w, placed);
							RldDum_ApplyPose(&pose, RLDDUM_GROUP_WHEEL, placed, out);
							lowest = (out[1] < lowest) ? out[1] : lowest;
						}
					}
				}
			}
		}
		ok = ok && (lowest >= 0.0);
		RldDum_Check(report, ok, "pose: without kart wheels the driver bends about its bottom, nothing below the ground at 50..200 %");
	}

	// A steering wheel group without width, or far from where a kart driver
	// holds the wheel, leans as the driver
	{
		struct RldDumFitInput odd = in;
		const double point[3] = {6.0, 70.0, 45.0};
		double asWheel[3];
		double asDriver[3];
		int k;

		ok = 1;
		for (k = 0; k < 2; k++)
		{
			odd.wheels = 1;
			odd.sizePercent = 100u;
			odd.wheel.valid = 1;
			odd.wheel.lo[0] = (k == 0) ? 3.0 : -5.0;
			odd.wheel.hi[0] = (k == 0) ? 3.0 : 5.0;
			odd.wheel.lo[1] = (k == 0) ? 35.0 : 75.0;
			odd.wheel.hi[1] = (k == 0) ? 40.0 : 85.0;
			odd.wheel.lo[2] = (k == 0) ? 20.0 : 45.0;
			odd.wheel.hi[2] = (k == 0) ? 28.0 : 55.0;
			ok = ok && RldDum_Fit(&odd, &fit) && !fit.hasWheel && fit.wheelAsDriver && (fit.bendHeight >= RLDDUM_BEND_HEIGHT);
			RldDum_PoseFrame(0u, &pose);
			RldDum_PoseFit(&pose, &fit);
			RldDum_ApplyPose(&pose, RLDDUM_GROUP_WHEEL, point, asWheel);
			RldDum_ApplyPose(&pose, RLDDUM_GROUP_DRIVER, point, asDriver);
			ok = ok && (memcmp(asWheel, asDriver, sizeof(asWheel)) == 0);
		}
		RldDum_Check(report, ok, "fit: a steering wheel without width or far off leans with the driver");
	}

	// THE MESH
	RldDum_Check(report, RldDum_TestMesh(1) && RldDum_TestMesh(0), "mesh: < 600 triangles, none degenerate, every part closed and wound outward");
	{
		u32 with = 0;
		u32 without = 0;
		const u32 t1 = RldDum_Mesh(1, NULL, 0u, NULL, NULL, 0u, &with);
		const u32 t0 = RldDum_Mesh(0, NULL, 0u, NULL, NULL, 0u, &without);

		ok = (t1 > t0) && (with > without) && ((with - RldDum_MeshSteeringFirst(1)) == (without - RldDum_MeshSteeringFirst(0))) &&
		     (RldDum_MeshSteeringFirst(0) < without);
		RldDum_Check(report, ok, "mesh: the wheels only with wheels on, the steering wheel last");
	}

	// DETERMINISM: everything twice, the same bytes
	{
		static double p1[3 * 512];
		static double p2[3 * 512];
		static u32 t1[3 * 600];
		static u32 t2[3 * 600];
		static u32 c1[600];
		static u32 c2[600];
		u32 n1 = 0;
		u32 n2 = 0;
		const u32 k1 = RldDum_Mesh(1, p1, 512u, t1, c1, 600u, &n1);
		const u32 k2 = RldDum_Mesh(1, p2, 512u, t2, c2, 600u, &n2);

		ok = (k1 == k2) && (n1 == n2) && (memcmp(p1, p2, (size_t)n1 * 3u * sizeof(double)) == 0) && (memcmp(t1, t2, (size_t)k1 * 3u * sizeof(u32)) == 0) &&
		     (memcmp(c1, c2, (size_t)k1 * sizeof(u32)) == 0);
		in.sizePercent = 120u;
		ok = ok && RldDum_Fit(&in, &fit) && RldDum_Fit(&in, &again) && (memcmp(&fit, &again, sizeof(fit)) == 0);
		for (f = 0; f < RLDDUM_FRAMES_TOTAL; f++)
		{
			RldDum_PoseFrame(f, &pose);
			RldDum_PoseFrame(f, &pose20);
			ok = ok && (memcmp(&pose, &pose20, sizeof(pose)) == 0);
		}
		RldDum_Check(report, ok, "determinism: mesh, fit and poses twice the same bytes");
	}

	return RldDum_TestFailed;
}
