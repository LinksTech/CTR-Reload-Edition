// THE PROOF THAT BOTH GTE PATHS COMPUTE THE SAME.
//
// platform/native_gte_alt.c is a copy of platform/native_gte_core.c today.
// That is a claim as long as nobody recomputes it - and "it is just
// the same file with other names" is exactly the kind of claim this
// tree has already paid dearly for twice. So it is recomputed, and
// not on one example but on millions.
//
// WHAT IS COMPARED: ALL 64 REGISTERS, not the result.
//
// This is the point where a more convenient comparison would be wrong. Several
// places in the game deliberately read registers that a COMPLETELY DIFFERENT caller
// left behind - VehPhysForce_CounterSteer computes with the matrix that the
// ground slope step loaded, and COLL.c loads no matrix at all before its rtv0.
// A comparison that only looks at the registers an operation
// writes according to the manual does not find a deviation in IR0, in the SXY stack or
// in the FLAG register - and exactly that one then tips a
// collision result three calls later. That is why CP2D[0..31] and CP2C[0..31]
// are compared in full, plus the return value.
//
// HOW THE STATES ARISE. Two phases, because they find two different
// bugs:
//
//   Phase 1 - breadth: every opcode variant against many mutually independent
//   random states. Finds what has been mistyped in a single
//   expression.
//
//   Phase 2 - depth: long chains in which the input state of every operation
//   is the result of the previous one. Finds what is remembered between two
//   calls. The three file-level states of the core computation (m_sf, m_mac0, m_mac3)
//   have copies of their own in the alt path; that none of them is read across a call
//   boundary is a claim about the code - and phase 2 is
//   the measurement for it.
//
// WHAT THE SWITCH DOES NOT COVER, and that deserves to be said: what is switched is
// GTE_operator, nothing else. MTC2, MFC2, CTC2, CFC2 and
// gte_leadingzerocount in platform/native_inline_c.c and
// platform/native_gte_core.c apply to BOTH paths. The square root
// SquareRoot0_stub in the game code runs via MTC2(30)/MFC2(31) and thus
// also through shared code. Whoever changes something there changes it for
// both - this self-test would not notice, because it would shift both sides
// at the same time.
//
// RUN BY: --gte-selftest, and as ctest 'gte_paths_identical'.

#include <stdio.h>

#include <macros.h>
#include <psx/gtereg.h>

#define NATIVE_GTE_CHECK_REPORT_MAX 8

struct NativeGteSnapshot
{
	u32 data[32];
	u32 ctrl[32];
	int result;
};

// A random generator of its own, not rand(). A self-test that takes different
// numbers on every run can find a bug and stay silent the next time -
// and then it can no longer be determined whether the fix worked or the
// randomness went somewhere else. Xorshift32, fixed seed, identical on
// every machine.
global_variable u32 s_gteCheckRng;

internal u32 NativeGteCheck_Next(void)
{
	u32 x = s_gteCheckRng;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	s_gteCheckRng = x;

	return x;
}

internal void NativeGteCheck_Seed(u32 seed)
{
	s_gteCheckRng = (seed != 0) ? seed : 0x9e3779b9u;
}

// A register set full of random values, but not uniformly distributed over 32 bits.
//
// The interesting places of the GTE are its limits: 44-bit overflow of the
// accumulators, the clamp of IR to +-0x8000, that of SX/SY to +-1024,
// the division when numerator and denominator approach each other. Uniformly distributed
// 32-bit random numbers almost always lie far outside and therefore always hit
// the same saturation branch. That is why the order of magnitude itself is
// rolled: a random number, trimmed to a random bit width of 1 to 32.
// That way small values, values right at the limits and full
// 32-bit values all occur regularly.
internal u32 NativeGteCheck_Value(void)
{
	const u32 bits = (NativeGteCheck_Next() % 32u) + 1u;
	const u32 raw = NativeGteCheck_Next();

	if (bits >= 32u)
	{
		return raw;
	}

	return raw & ((1u << bits) - 1u);
}

internal void NativeGteCheck_Randomise(struct NativeGteSnapshot *state)
{
	for (int i = 0; i < 32; i++)
	{
		state->data[i] = NativeGteCheck_Value();
		state->ctrl[i] = NativeGteCheck_Value();
	}

	state->result = 0;
}

internal void NativeGteCheck_Load(const struct NativeGteSnapshot *state)
{
	for (int i = 0; i < 32; i++)
	{
		gteRegs.CP2D.r[i] = state->data[i];
		gteRegs.CP2C.r[i] = state->ctrl[i];
	}
}

internal void NativeGteCheck_Store(struct NativeGteSnapshot *state, int result)
{
	for (int i = 0; i < 32; i++)
	{
		state->data[i] = gteRegs.CP2D.r[i];
		state->ctrl[i] = gteRegs.CP2C.r[i];
	}

	state->result = result;
}

// Says WHERE it diverges, not only THAT it does. A self-test that reports "unequal"
// has only moved the work elsewhere.
internal int NativeGteCheck_Compare(const struct NativeGteSnapshot *core, const struct NativeGteSnapshot *alt, int op, const char *phase, int *reported)
{
	int differences = 0;

	if (core->result != alt->result)
	{
		differences++;
		if (*reported < NATIVE_GTE_CHECK_REPORT_MAX)
		{
			(*reported)++;
			printf("[CTR GTE] %s op %08x: return core %d alt %d\n", phase, (unsigned)op, core->result, alt->result);
		}
	}

	for (int i = 0; i < 32; i++)
	{
		if (core->data[i] != alt->data[i])
		{
			differences++;
			if (*reported < NATIVE_GTE_CHECK_REPORT_MAX)
			{
				(*reported)++;
				printf("[CTR GTE] %s op %08x: CP2D[%d] core %08x alt %08x\n", phase, (unsigned)op, i, core->data[i], alt->data[i]);
			}
		}

		if (core->ctrl[i] != alt->ctrl[i])
		{
			differences++;
			if (*reported < NATIVE_GTE_CHECK_REPORT_MAX)
			{
				(*reported)++;
				printf("[CTR GTE] %s op %08x: CP2C[%d] core %08x alt %08x\n", phase, (unsigned)op, i, core->ctrl[i], alt->ctrl[i]);
			}
		}
	}

	return differences;
}

// One operation, both paths, the same input state.
//
// Explicitly, because of the order: first the core, then the input state
// is loaded AGAIN, then the second path. Not "the second
// computes on from the result of the first" - that would not be a comparison
// but a chaining.
internal int NativeGteCheck_One(const struct NativeGteSnapshot *input, int op, const char *phase, int *reported, struct NativeGteSnapshot *outCore)
{
	struct NativeGteSnapshot core;
	struct NativeGteSnapshot alt;
	int result;

	NativeGteCheck_Load(input);
	result = GTE_operator(op);
	NativeGteCheck_Store(&core, result);

	NativeGteCheck_Load(input);
	result = GTE_operatorAlt(op);
	NativeGteCheck_Store(&alt, result);

	if (outCore != NULL)
	{
		*outCore = core;
	}

	return NativeGteCheck_Compare(&core, &alt, op, phase, reported);
}

// The opcodes this game actually issues - copied from
// include/psx/inline_c.h, not newly invented. They come first, because a
// bug in exactly these would be the only one that affects anybody today.
global_variable const int s_gteCheckGameOps[] = {
    0x0180001,  // rtps
    0x0280030,  // rtpt
    0x0480012,  // rt / rtv0tr
    0x0486012,  // rtv0
    0x048E012,  // rtv1
    0x0496012,  // rtv2
    0x049E012,  // rtir
    0x041E012,  // rtir_sf0
    0x04A6012,  // llv0
    0x04AE012,  // llv1
    0x04B6012,  // llv2
    0x04A2012,  // llv0bk
    0x04C0012,  // lcv0tr
    0x04CE012,  // lcv1
    0x04DE012,  // lcir
    0x0780010,  // dpcs
    0x0F8002A,  // dpct
    0x0A00428,  // sqr0
    0x0A80428,  // sqr12
    0x01400006, // nclip
    0x0158002D, // avsz3
    0x0168002E, // avsz4
    0x0178000C, // op12
    0x0170000C, // op0
    0x01A8003E, // gpl12
    0x0198003D, // gpf12
    // The four that the game code passes to doCOP2 as a raw number instead of via a
    // macro - COLL.c and RenderBucket_QueueExecute.c build their own
    // wrapper functions. They are all MVMVA and thus covered by the full sweep of the
    // second phase anyway; they are here so that the
    // CHAINS also run through them, and because a list called "what the game
    // issues" must be complete.
    0x0406012,  // COLL.c:240
    0x04C6012,  // RenderBucket_QueueExecute.c:711, :1557
    0x04BE012,  // llir, RenderBucket_QueueExecute.c:1243
};

// Every function number the core knows, plus four it does not know.
//
// The four unknown ones are not decoration: the branch that does nothing is also
// a branch, and it sets C2_FLAG to zero beforehand. A second path that did
// something else there would be silent.
global_variable const int s_gteCheckFuncts[] = {
    0x00, 0x01, 0x06, 0x0c, 0x10, 0x11, 0x12, 0x13, 0x14, 0x16, 0x1b, 0x1c, 0x1e, 0x20,
    0x28, 0x29, 0x2a, 0x2d, 0x2e, 0x30, 0x3d, 0x3e, 0x3f,
    0x02, 0x07, 0x21, 0x3c, // not assigned
};

int NativeGteCheck_Run(void)
{
	struct NativeGteSnapshot state;
	long long operations = 0;
	long long differences = 0;
	int reported = 0;

	const int gameOpCount = (int)(sizeof(s_gteCheckGameOps) / sizeof(s_gteCheckGameOps[0]));
	const int functCount = (int)(sizeof(s_gteCheckFuncts) / sizeof(s_gteCheckFuncts[0]));

	printf("[CTR GTE] selftest: core against alt, all 64 registers plus return value\n");

	// --- Phase 1a: the game's opcodes -----------------------------------
	NativeGteCheck_Seed(0x12345678u);

	for (int round = 0; round < 20000; round++)
	{
		NativeGteCheck_Randomise(&state);

		for (int i = 0; i < gameOpCount; i++)
		{
			differences += NativeGteCheck_One(&state, s_gteCheckGameOps[i], "game", &reported, NULL);
			operations++;
		}
	}

	printf("[CTR GTE]   %d game opcodes x 20000 states\n", gameOpCount);

	// --- Phase 1b: every function number in every variant --------------------
	//
	// sf and lm are the two bits that almost every branch reads, so they are
	// enumerated completely. For MVMVA mx, v and cv are added - there
	// it is 256 variants, and the cv==2 special path of the core is one of them.
	for (int round = 0; round < 8000; round++)
	{
		NativeGteCheck_Randomise(&state);

		for (int i = 0; i < functCount; i++)
		{
			const int funct = s_gteCheckFuncts[i];

			for (int sf = 0; sf < 2; sf++)
			{
				for (int lm = 0; lm < 2; lm++)
				{
					if (funct == 0x12)
					{
						for (int mx = 0; mx < 4; mx++)
						{
							for (int v = 0; v < 4; v++)
							{
								for (int cv = 0; cv < 4; cv++)
								{
									const int op = 0x0400000 | (sf << 19) | (mx << 17) | (v << 15) | (cv << 13) | (lm << 10) | funct;

									differences += NativeGteCheck_One(&state, op, "funct", &reported, NULL);
									operations++;
								}
							}
						}
					}
					else
					{
						const int op = 0x0400000 | (sf << 19) | (lm << 10) | funct;

						differences += NativeGteCheck_One(&state, op, "funct", &reported, NULL);
						operations++;
					}
				}
			}
		}
	}

	printf("[CTR GTE]   %d functions in every sf/lm variant, MVMVA in all 256, x 8000 states\n", functCount);

	// --- Phase 2: chains ----------------------------------------------------
	//
	// The input state of every operation is the result of the previous one, and the
	// chain only continues via the core. So both paths get the same input
	// in every step, and the inputs are the states that
	// a real computation actually passes through - no longer random numbers.
	for (int chain = 0; chain < 2000; chain++)
	{
		NativeGteCheck_Randomise(&state);

		for (int step = 0; step < 1000; step++)
		{
			const int pick = (int)(NativeGteCheck_Next() % (u32)gameOpCount);
			struct NativeGteSnapshot next;

			differences += NativeGteCheck_One(&state, s_gteCheckGameOps[pick], "chain", &reported, &next);
			operations++;
			state = next;
		}
	}

	printf("[CTR GTE]   2000 chains of 1000 operations, each starting from the previous result\n");

	if (differences != 0)
	{
		printf("[CTR GTE] SELFTEST FAILED: %lld differing values over %lld operations\n", differences, operations);
		if (reported >= NATIVE_GTE_CHECK_REPORT_MAX)
		{
			printf("[CTR GTE] (only the first %d are printed)\n", NATIVE_GTE_CHECK_REPORT_MAX);
		}
		return 1;
	}

	printf("[CTR GTE] selftest passed: %lld operations, 0 differences\n", operations);
	return 0;
}
