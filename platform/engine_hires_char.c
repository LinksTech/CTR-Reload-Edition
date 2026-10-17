#include <platform/engine.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ENGINE HIRES CHAR - the hi-res drivers of the .rldchar (round of 2026-10-17).
// See include/platform/engine.h, HI-RES DRIVERS FROM THE .rldchar.
//
// MILESTONE M1 (API + design): the API of engine.h is in place and answers
// "no hi-res character" everywhere (every lookup -1, every submit 0, the load
// and the frame hooks do nothing), so the callers (manager MC: podium, results,
// driver select, menu scene) can be written against it now. The loader, the
// skinning, the clip control and the wheels follow in M2 in this file; the
// pure parts are already in include/rldhires_anim.inc (pose maths) and
// platform/engine_hires_pose.c (clip state machine, fallback chains).
//
// THE DESIGN:
//   SKINNING ON THE CPU, per frame, once per pose slot, into a per-bank CPU
//   array that the renderer copies into a STREAMED vertex buffer (three
//   regions, the dynamic buffer's region rule: frames-in-flight safe;
//   Engine_RenderStream* in engine_render.c). Every vertex leaves the skinning
//   in WORLD units and axes (the instance matrix with inst->scale - the game's
//   matrix animations and squash act on the whole model - times 64 model units
//   per metre times the node/joint matrices of the pose): program "ns" lights
//   in world space (v_world = a_position, the normal packed into a_uvBox), and
//   the draw's matrix is the camera's alone. No vertex format, program or
//   uniform block changes; no other draw changes; the dynamic geometry budget
//   ENGINE_DYN_VERTICES_PER_FRAME of the track and the effects is not touched.
//   WHEELS are part of the same vertex buffer: four rigid palette entries whose
//   matrices come from the wheel rules of the prototype (engine_hires.c:
//   middles from inst->scale, steer from wheelRotation, roll integrated per
//   logic tick from speedApprox, wobble from hazardTimer) - the file's wheel
//   nodes when all four are there, else standard 3D wheels made in code.

int g_cfg_engineHiresChars = 0;
int g_cfg_engineHiresCharsLog = 0;

int Engine_HiresCharOfSeat(int seat)
{
	(void)seat;
	return -1;
}

int Engine_HiresCharOfFile(const char *file)
{
	(void)file;
	return -1;
}

int Engine_HiresCharOfEntry(int rosterEntry)
{
	(void)rosterEntry;
	return -1;
}

int Engine_HiresCharOfModel(const struct Model *model)
{
	(void)model;
	return -1;
}

int Engine_HiresCharOfPlace(int place)
{
	(void)place;
	return -1;
}

int Engine_HiresCharOfLastRaceSeat(int seat)
{
	(void)seat;
	return -1;
}

const char *Engine_HiresCharFile(int ch)
{
	(void)ch;
	return "";
}

int Engine_HiresCharResolveClip(int ch, int clip, int *full)
{
	(void)ch;
	if (full != NULL)
	{
		*full = 0;
	}
	(void)clip;
	return ENGINE_HIRES_CLIP_REST;
}

int Engine_HiresCharHull(int ch, float hullMin[3], float hullMax[3])
{
	(void)ch;
	(void)hullMin;
	(void)hullMax;
	return 0;
}

void Engine_HiresSeatForceClip(int seat, int clip)
{
	(void)seat;
	(void)clip;
}

int Engine_HiresCharSubmit(int ch, const EngineHiresSubmit *s)
{
	(void)ch;
	(void)s;
	return 0;
}

void Engine_HiresCharsLoad(void)
{
}

void Engine_HiresCharsCollect(struct GameTracker *gGT)
{
	(void)gGT;
}

int Engine_HiresCharOwns(const struct Driver *d)
{
	(void)d;
	return 0;
}

int Engine_HiresCharRoute(struct Instance *inst, struct InstDrawPerPlayer *idpp, struct PushBuffer *pb)
{
	(void)inst;
	(void)idpp;
	(void)pb;
	return 0;
}

// The selftests of the hi-res drivers: the GLB builder's own check, the pose
// maths (engine_hires_selftest.c), the clip state machine and its fallback
// chains (engine_hires_pose.c).
int Engine_SelfTestHiresChar(int *checks)
{
	int failures = 0;
	int n = 0;

#define HC_CHECK(cond, what)                                           \
	do                                                                 \
	{                                                                  \
		n++;                                                           \
		if (!(cond))                                                   \
		{                                                              \
			failures++;                                                \
			printf("engine selftest: hires char " what " FAILED\n"); \
		}                                                              \
	} while (0)

	failures += EngHiresTest_GlbSelfTest(checks);
	failures += EngHires_SelfTestAnim(checks);

	// Fallback chains (FORMAT section 4) with every clip missing in turn.
	{
		const u32 all = (1u << ENGINE_HIRES_CLIPS) - 1u;
		int full = -1;
		int c;
		int same = 1;

		for (c = 0; c < ENGINE_HIRES_CLIPS; c++)
		{
			same = same && (EngHc_Resolve(all, c, &full) == c) && (full == 0);
		}
		HC_CHECK(same, "every present clip is drawn as itself");
		HC_CHECK((EngHc_Resolve(0u, ENGINE_HIRES_CLIP_IDLE, &full) == ENGINE_HIRES_CLIP_REST) && (full == 0), "idle missing: the rest pose");
		HC_CHECK(EngHc_Resolve(1u << ENGINE_HIRES_CLIP_IDLE, ENGINE_HIRES_CLIP_STEER_LEFT, &full) == ENGINE_HIRES_CLIP_IDLE, "steer_left -> idle");
		HC_CHECK(EngHc_Resolve(1u << ENGINE_HIRES_CLIP_IDLE, ENGINE_HIRES_CLIP_STEER_RIGHT, &full) == ENGINE_HIRES_CLIP_IDLE, "steer_right -> idle");
		HC_CHECK(EngHc_Resolve(1u << ENGINE_HIRES_CLIP_IDLE, ENGINE_HIRES_CLIP_JUMP, &full) == ENGINE_HIRES_CLIP_IDLE, "jump -> idle");
		HC_CHECK((EngHc_Resolve((1u << ENGINE_HIRES_CLIP_IDLE) | (1u << ENGINE_HIRES_CLIP_STEER_LEFT), ENGINE_HIRES_CLIP_DRIFT_LEFT, &full) ==
		          ENGINE_HIRES_CLIP_STEER_LEFT) &&
		             (full == 1),
		         "drift_left -> steer_left at full lock");
		HC_CHECK((EngHc_Resolve((1u << ENGINE_HIRES_CLIP_IDLE) | (1u << ENGINE_HIRES_CLIP_STEER_RIGHT), ENGINE_HIRES_CLIP_DRIFT_RIGHT, &full) ==
		          ENGINE_HIRES_CLIP_STEER_RIGHT) &&
		             (full == 1),
		         "drift_right -> steer_right at full lock");
		HC_CHECK((EngHc_Resolve(1u << ENGINE_HIRES_CLIP_IDLE, ENGINE_HIRES_CLIP_DRIFT_LEFT, &full) == ENGINE_HIRES_CLIP_IDLE) && (full == 0),
		         "drift_left without steer_left -> idle (not at full lock)");
		HC_CHECK((EngHc_Resolve(1u << ENGINE_HIRES_CLIP_STEER_LEFT, ENGINE_HIRES_CLIP_DRIFT_RIGHT, &full) == ENGINE_HIRES_CLIP_REST), "drift_right ends at rest");
		HC_CHECK(EngHc_Resolve(1u << ENGINE_HIRES_CLIP_IDLE, ENGINE_HIRES_CLIP_HIT, &full) == ENGINE_HIRES_CLIP_IDLE, "hit -> idle");
		HC_CHECK(EngHc_Resolve((1u << ENGINE_HIRES_CLIP_IDLE) | (1u << ENGINE_HIRES_CLIP_PODIUM_1), ENGINE_HIRES_CLIP_PODIUM_3, &full) ==
		             ENGINE_HIRES_CLIP_PODIUM_1,
		         "podium_3 -> podium_2 -> podium_1");
		HC_CHECK(EngHc_Resolve((1u << ENGINE_HIRES_CLIP_PODIUM_2), ENGINE_HIRES_CLIP_PODIUM_3, &full) == ENGINE_HIRES_CLIP_PODIUM_2, "podium_3 -> podium_2");
		HC_CHECK(EngHc_Resolve(1u << ENGINE_HIRES_CLIP_IDLE, ENGINE_HIRES_CLIP_PODIUM_1, &full) == ENGINE_HIRES_CLIP_IDLE, "podium_1 -> idle");
		HC_CHECK(EngHc_Resolve(1u << ENGINE_HIRES_CLIP_IDLE, ENGINE_HIRES_CLIP_LOSE, &full) == ENGINE_HIRES_CLIP_IDLE, "lose -> idle");
		HC_CHECK(EngHc_Resolve(1u << ENGINE_HIRES_CLIP_IDLE, ENGINE_HIRES_CLIP_MENU, &full) == ENGINE_HIRES_CLIP_IDLE, "menu -> idle");
		HC_CHECK((EngHc_Resolve(all, 99, NULL) == ENGINE_HIRES_CLIP_REST) && (EngHc_Resolve(all, -3, NULL) == ENGINE_HIRES_CLIP_REST), "unknown clip: rest");
		HC_CHECK((strcmp(Engine_HiresClipName(ENGINE_HIRES_CLIP_PODIUM_3), "podium_3") == 0) && (strcmp(Engine_HiresClipName(ENGINE_HIRES_CLIP_REST), "rest") == 0) &&
		             (strcmp(Engine_HiresClipName(42), "?") == 0),
		         "clip names");
		HC_CHECK((EngHc_PlayOf(ENGINE_HIRES_CLIP_IDLE) == ENG_HC_PLAY_LOOP) && (EngHc_PlayOf(ENGINE_HIRES_CLIP_STEER_RIGHT) == ENG_HC_PLAY_SCRUB) &&
		             (EngHc_PlayOf(ENGINE_HIRES_CLIP_JUMP) == ENG_HC_PLAY_ONCE) && (EngHc_PlayOf(ENGINE_HIRES_CLIP_HIT) == ENG_HC_PLAY_ONCE) &&
		             (EngHc_PlayOf(ENGINE_HIRES_CLIP_DRIFT_LEFT) == ENG_HC_PLAY_LOOP) && (EngHc_PlayOf(ENGINE_HIRES_CLIP_PODIUM_2) == ENG_HC_PLAY_LOOP) &&
		             (EngHc_PlayOf(ENGINE_HIRES_CLIP_LOSE) == ENG_HC_PLAY_LOOP) && (EngHc_PlayOf(ENGINE_HIRES_CLIP_MENU) == ENG_HC_PLAY_LOOP),
		         "play modes");
	}

	// The state machine: a scripted run of ticks (33 ms each).
	{
		EngHcSeatState s;
		EngHcInput in;
		int forceClip = -1;
		int st;
		int i;
		int ok;

		memset(&in, 0, sizeof(in));
		in.touch = 1;
		in.kartState = 0;
		in.elapsedMs = 33;
		in.force = -1;
		EngHc_SeatReset(&s, -1);
		HC_CHECK((s.state == ENG_HC_STATE_BASE) && (s.driftSide == 1) && (s.forceClip == -1) && !s.fading, "reset: base, no fade");

		// Steering: no state change, the fraction and the side.
		in.wheelRotation = 32;
		EngHc_Advance(&s, in.elapsedMs);
		st = EngHc_Decide(&s, &in, &forceClip);
		HC_CHECK((st == ENG_HC_STATE_BASE) && !EngHc_Changes(&s, st, forceClip) && (s.steerRaw == 32), "steering stays in base");
		{
			int clip = -1;
			const float f = EngHc_SteerFraction(32, &clip);
			const float g = EngHc_SteerFraction(-85, &clip);

			HC_CHECK((fabsf(f - 0.5f) < 1e-6f) && (fabsf(g - 1.0f) < 1e-6f) && (clip == ENGINE_HIRES_CLIP_STEER_RIGHT), "steer fraction clamps, negative = right");
			(void)EngHc_SteerFraction(5, &clip);
			HC_CHECK(clip == ENGINE_HIRES_CLIP_STEER_LEFT, "positive wheelRotation = left");
		}

		// Air: two ticks off the ground stay base (a bump), the third is the jump.
		in.touch = 0;
		ok = 1;
		for (i = 0; i < 2; i++)
		{
			EngHc_Advance(&s, in.elapsedMs);
			st = EngHc_Decide(&s, &in, &forceClip);
			ok = ok && (st == ENG_HC_STATE_BASE);
		}
		HC_CHECK(ok, "a bump of 2 ticks stays on the ground");
		EngHc_Advance(&s, in.elapsedMs);
		st = EngHc_Decide(&s, &in, &forceClip);
		HC_CHECK((st == ENG_HC_STATE_JUMP) && EngHc_Changes(&s, st, forceClip), "the third tick in the air is the jump");
		EngHc_Enter(&s, st, forceClip);
		HC_CHECK((s.state == ENG_HC_STATE_JUMP) && (s.stateTime == 0.0f) && s.fading && (s.prevState == ENG_HC_STATE_BASE) &&
		             (EngHc_StateClip(&s) == ENGINE_HIRES_CLIP_JUMP) && (EngHc_FadeWeight(&s) == 0.0f),
		         "jump entered from time 0, fade from base starts at weight 0");
		// The fade: 33 ms steps of 150 ms - the weight rises without a step, 5 ticks.
		{
			float last = 0.0f;
			int rising = 1;

			for (i = 0; i < 4; i++)
			{
				float w;

				EngHc_Advance(&s, in.elapsedMs);
				w = EngHc_FadeWeight(&s);
				rising = rising && (w > last) && (w < 1.0f) && ((w - last) < 0.25f);
				last = w;
			}
			HC_CHECK(rising, "fade weight rises in steps below 0.25 over 4 ticks");
			EngHc_Advance(&s, in.elapsedMs);
			HC_CHECK((EngHc_FadeWeight(&s) == 1.0f) && !s.fading, "fade done after 150 ms");
		}
		// Once, held: the local time of a once clip clamps at its length.
		HC_CHECK((fabsf(EngHc_LocalTime(ENG_HC_PLAY_ONCE, 9.0f, 0.0f, 0.5f, 0) - 0.5f) < 1e-6f) &&
		             (fabsf(EngHc_LocalTime(ENG_HC_PLAY_ONCE, 0.2f, 0.0f, 0.5f, 0) - 0.2f) < 1e-6f),
		         "once clip holds its last pose");
		HC_CHECK((fabsf(EngHc_LocalTime(ENG_HC_PLAY_LOOP, 2.25f, 0.0f, 1.0f, 0) - 0.25f) < 1e-5f) &&
		             (fabsf(EngHc_LocalTime(ENG_HC_PLAY_SCRUB, 5.0f, 0.25f, 2.0f, 0) - 0.5f) < 1e-6f) &&
		             (fabsf(EngHc_LocalTime(ENG_HC_PLAY_SCRUB, 5.0f, 0.25f, 2.0f, 1) - 2.0f) < 1e-6f) && (EngHc_LocalTime(ENG_HC_PLAY_LOOP, 3.0f, 0.0f, 0.0f, 0) == 0.0f),
		         "loop wraps, scrub = fraction x length, full lock = the end");

		// Landing: back on the ground is base again (a change, a fade).
		in.touch = 1;
		EngHc_Advance(&s, in.elapsedMs);
		st = EngHc_Decide(&s, &in, &forceClip);
		HC_CHECK((st == ENG_HC_STATE_BASE) && EngHc_Changes(&s, st, forceClip) && (s.airTicks == 0), "landing: base");
		EngHc_Enter(&s, st, forceClip);

		// Drift: the side from the first tick (multDrift first), kept while it lasts.
		in.kartState = KS_DRIFTING;
		in.multDrift = -300;
		in.wheelRotation = 40;
		EngHc_Advance(&s, in.elapsedMs);
		st = EngHc_Decide(&s, &in, &forceClip);
		HC_CHECK((st == ENG_HC_STATE_DRIFT_RIGHT) && (s.driftSide == -1), "drift right by multDrift");
		EngHc_Enter(&s, st, forceClip);
		in.multDrift = 300;
		EngHc_Advance(&s, in.elapsedMs);
		st = EngHc_Decide(&s, &in, &forceClip);
		HC_CHECK((st == ENG_HC_STATE_DRIFT_RIGHT) && !EngHc_Changes(&s, st, forceClip) && (s.driftTicks == 2), "the side stays for the drift");
		HC_CHECK((EngHc_DriftSide(0, 0, 0) == 1) && (EngHc_DriftSide(0, -2, 9) == -1) && (EngHc_DriftSide(0, 0, -1) == -1) && (EngHc_DriftSide(7, -2, -9) == 1),
		         "drift side: first non-zero of multDrift, simpTurnState, wheelRotation");

		// Hit beats drift and air; entered once, the time from the entry.
		in.kartState = KS_SPINNING;
		in.touch = 0;
		EngHc_Advance(&s, in.elapsedMs);
		st = EngHc_Decide(&s, &in, &forceClip);
		HC_CHECK((st == ENG_HC_STATE_HIT) && EngHc_Changes(&s, st, forceClip) && (s.driftTicks == 0), "spinning is the hit");
		EngHc_Enter(&s, st, forceClip);
		ok = 1;
		for (i = 0; i < 5; i++)
		{
			in.kartState = (i < 2) ? KS_BLASTED : KS_CRASHING;
			EngHc_Advance(&s, in.elapsedMs);
			st = EngHc_Decide(&s, &in, &forceClip);
			ok = ok && (st == ENG_HC_STATE_HIT) && !EngHc_Changes(&s, st, forceClip);
		}
		HC_CHECK(ok && (fabsf(s.stateTime - 0.165f) < 1e-4f) && (s.airTicks >= ENG_HC_AIR_DELAY), "blasted and crashing stay the same hit (in the air too)");

		// Forced: first, a change of the forced clip is a change too; -1 gives it back.
		in.force = ENGINE_HIRES_CLIP_PODIUM_2;
		EngHc_Advance(&s, in.elapsedMs);
		st = EngHc_Decide(&s, &in, &forceClip);
		HC_CHECK((st == ENG_HC_STATE_FORCED) && (forceClip == ENGINE_HIRES_CLIP_PODIUM_2) && EngHc_Changes(&s, st, forceClip), "forced beats hit");
		EngHc_Enter(&s, st, forceClip);
		HC_CHECK(EngHc_StateClip(&s) == ENGINE_HIRES_CLIP_PODIUM_2, "forced clip asked for");
		in.force = ENGINE_HIRES_CLIP_LOSE;
		st = EngHc_Decide(&s, &in, &forceClip);
		HC_CHECK(EngHc_Changes(&s, st, forceClip), "another forced clip is a change");
		in.force = -1;
		in.kartState = 0;
		in.touch = 1;
		st = EngHc_Decide(&s, &in, &forceClip);
		HC_CHECK(st == ENG_HC_STATE_BASE, "force -1 gives the seat back");
		// Finished: podium by rank, lose from rank 3; above hit, below a force.
		in.finished = 1;
		in.rank = 1;
		in.kartState = KS_SPINNING;
		st = EngHc_Decide(&s, &in, &forceClip);
		HC_CHECK((st == ENG_HC_STATE_FINISHED) && (forceClip == ENGINE_HIRES_CLIP_PODIUM_2), "finished on place 2: podium_2, above hit");
		in.rank = 5;
		st = EngHc_Decide(&s, &in, &forceClip);
		HC_CHECK((st == ENG_HC_STATE_FINISHED) && (forceClip == ENGINE_HIRES_CLIP_LOSE), "finished on place 6: lose");
		HC_CHECK((EngHc_FinishedClip(0) == ENGINE_HIRES_CLIP_PODIUM_1) && (EngHc_FinishedClip(2) == ENGINE_HIRES_CLIP_PODIUM_3) &&
		             (EngHc_FinishedClip(3) == ENGINE_HIRES_CLIP_LOSE) && (EngHc_FinishedClip(-1) == ENGINE_HIRES_CLIP_LOSE),
		         "finished clips by rank");
		in.force = ENGINE_HIRES_CLIP_MENU;
		st = EngHc_Decide(&s, &in, &forceClip);
		HC_CHECK((st == ENG_HC_STATE_FORCED) && (forceClip == ENGINE_HIRES_CLIP_MENU), "a force beats finished");
		in.force = -1;
		in.finished = 0;
		in.kartState = 0;
		HC_CHECK((strcmp(EngHc_StateName(ENG_HC_STATE_DRIFT_LEFT), "drift_left") == 0) && (strcmp(EngHc_StateName(9), "?") == 0), "state names");

		// A pause (elapsed 0) moves no time.
		{
			const float before = s.baseTime;

			EngHc_Advance(&s, 0);
			HC_CHECK(s.baseTime == before, "no time without elapsed ms");
		}
		EngHc_SeatReset(&s, ENGINE_HIRES_CLIP_MENU);
		HC_CHECK((s.state == ENG_HC_STATE_FORCED) && (s.forceClip == ENGINE_HIRES_CLIP_MENU), "reset with a force starts forced");
	}

	// The API without a loaded character (M1): no character anywhere.
	HC_CHECK((Engine_HiresCharOfSeat(0) == -1) && (Engine_HiresCharOfFile("x.rldchar") == -1) && (Engine_HiresCharOfEntry(0) == -1) &&
	             (Engine_HiresCharSubmit(0, NULL) == 0),
	         "no character without a load");
#undef HC_CHECK
	*checks += n;
	return failures;
}
