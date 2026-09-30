#include <common.h>

void Platform_Log(const char *format, ...);
void Platform_AtExitReport(void (*report)(void));

enum
{
	VEH_LAP_INVALID_CHECKPOINT = 0xff,
	VEH_LAP_WORLD_POS_SHIFT = 8,
	VEH_LAP_MOVING_DIR_SHIFT = 5,
	VEH_LAP_TRACK_DISTANCE_SCALE_SHIFT = 3,
	VEH_LAP_PROJECTED_DISTANCE_SHIFT = 0xc,
	VEH_LAP_WRONG_WAY_DOT_LIMIT = 0x5a801,
};

// THE COUNTING FOR THE BOUNDS CHECK BELOW.
//
// A branch that is never executed is not tested - it is only
// compiled. That is why every rejection is counted, and next to it runs the
// largest ACCEPTED index: that is the counter-proof against the check
// being too strict. Whoever only counts what they reject cannot tell apart
// "catches the bug" and "catches everything".
global_variable s64 s_lapOutOfRange;     // how often the new branch kicked in
global_variable s64 s_lapOutOfRangeBot;  // of those, from a bot
global_variable int s_lapOutOfRangeMax;  // largest rejected index
global_variable int s_lapOutOfRangeMin;  // smallest rejected index
global_variable int s_lapAcceptedMax;    // largest accepted index
global_variable int s_lapNodes;          // cnt_restart_points at the last call
global_variable int s_lapReportArmed;

internal void VehLap_Report(void)
{
	Platform_Log("[CTR Lap] at exit: checkpoint index out of range %lld time(s) (%lld from bots), rejected range %d..%d; largest accepted "
	             "index %d of %d node(s)\n",
	             s_lapOutOfRange, s_lapOutOfRangeBot, (s_lapOutOfRange != 0) ? s_lapOutOfRangeMin : -1,
	             (s_lapOutOfRange != 0) ? s_lapOutOfRangeMax : -1, s_lapAcceptedMax, s_lapNodes);
}

internal void VehLap_ArmReport(void)
{
	if (!s_lapReportArmed)
	{
		s_lapReportArmed = 1;
		Platform_AtExitReport(VehLap_Report);
	}
}

void VehLap_UpdateProgress(struct Driver *driver)
{
	struct GameTracker *gGT = sdata->gGT;
	s16 checkpointIndex = -1;

	if (driver == NULL)
	{
		return;
	}

	if ((driver->actionsFlagSet & ACTION_BOT) == 0)
	{
		struct QuadBlock *quad = driver->lastValid;

		if ((quad != NULL) && (quad->checkpointIndex != VEH_LAP_INVALID_CHECKPOINT))
		{
			checkpointIndex = quad->checkpointIndex;
		}
	}
	else
	{
		checkpointIndex = driver->botData.ai_quadblock_checkpointIndex;
	}

	struct Level *level = gGT->level1;
	if (((u32)(level->cnt_restart_points - 1) >= VEH_LAP_INVALID_CHECKPOINT) || (checkpointIndex < 0))
	{
		return;
	}

	// THE INDEX AGAINST THE REAL NODE COUNT, not against 255.
	//
	// The line above is the check retail has, and it checks the
	// COUNT: is cnt_restart_points usable, and has anyone set an
	// index at all. What it does NOT check is whether the index fits into this count
	// - and exactly that decides whether nodes[index] below still lies in the
	// array.
	//
	// WHERE THE TOO-LARGE INDEX COMES FROM. A bot takes its checkpoint from the
	// quadblock under the kart as soon as it drives beside the navigation line
	// (BOTS.c:1997, applies with BOT_FLAG_ESTIMATE_NAV or BOT_FLAG_FREE_PHYSICS);
	// otherwise from goBackCount of the navigation data (BOTS.c:1960). The first path
	// delivers what the track author wrote onto the face, and that
	// is 255 on everything that is not road.
	//
	// MEASURED on Sunset Vista: 11,791 of the 13,134 quadblocks carry checkpoint
	// 255, of those 1,854 with the GROUND bit - and only those are seen by the bot's
	// probe, because it requires QUADBLOCK_FLAG_GROUND. The track has 149 nodes.
	// Without this line nodes[255] read from 149 entries, i.e. 106 structures of
	// 12 bytes past the array - and their fields went into distance, lap state
	// and the branching logic below.
	//
	// NO SIGN CHECK ON A BYTE. Such a check would read 128..254
	// as negative and reject them together with 255. Those are valid
	// indices: Sunset Vista has 149 nodes and uses 148 of them. So the check
	// is unsigned against the number that really applies.
	//
	// WHAT THE BRANCH DOES. It returns without touching anything - so
	// distanceToFinish_curr, the lap state, the wrong-way mark and
	// checkpoint.currentIndex stay at their last valid value. A
	// checkpoint that does not exist should not change the progress, neither
	// forwards nor backwards.
	VehLap_ArmReport();
	s_lapNodes = level->cnt_restart_points;

	if ((u32)checkpointIndex >= (u32)level->cnt_restart_points)
	{
		if ((s_lapOutOfRange == 0) || (checkpointIndex > s_lapOutOfRangeMax))
		{
			s_lapOutOfRangeMax = checkpointIndex;
		}
		if ((s_lapOutOfRange == 0) || (checkpointIndex < s_lapOutOfRangeMin))
		{
			s_lapOutOfRangeMin = checkpointIndex;
		}

		s_lapOutOfRange++;
		if ((driver->actionsFlagSet & ACTION_BOT) != 0)
		{
			s_lapOutOfRangeBot++;
		}

		return;
	}

	if (checkpointIndex > s_lapAcceptedMax)
	{
		s_lapAcceptedMax = checkpointIndex;
	}

	struct CheckpointNode *nodes = level->ptr_restart_points;
	struct CheckpointNode *checkpointNode = &nodes[checkpointIndex];
	struct CheckpointNode *progressNode = &nodes[checkpointNode->nextIndex_forward];
	struct CheckpointNode *nextNode = &nodes[progressNode->nextIndex_forward];

	SVec3Slot nodeDelta;
	nodeDelta.x = (s16)CTR_MipsSubLo((u16)progressNode->pos.x, (u16)nextNode->pos.x);
	nodeDelta.y = (s16)CTR_MipsSubLo((u16)progressNode->pos.y, (u16)nextNode->pos.y);
	nodeDelta.z = (s16)CTR_MipsSubLo((u16)progressNode->pos.z, (u16)nextNode->pos.z);
	nodeDelta.w = 0;

	MATH_VectorNormalize(SVec3Slot_AsVec3(&nodeDelta));

	s16 deltaX = (s16)CTR_MipsSubLo((u16)CTR_MipsSra(driver->posCurr.x, VEH_LAP_WORLD_POS_SHIFT), (u16)progressNode->pos.x);
	s16 deltaY = (s16)CTR_MipsSubLo((u16)CTR_MipsSra(driver->posCurr.y, VEH_LAP_WORLD_POS_SHIFT), (u16)progressNode->pos.y);
	s16 deltaZ = (s16)CTR_MipsSubLo((u16)CTR_MipsSra(driver->posCurr.z, VEH_LAP_WORLD_POS_SHIFT), (u16)progressNode->pos.z);

	CTC2(CTR_PackS16Pair(deltaX, deltaY), 0);
	CTC2(CTR_PackS16Pair(deltaZ, CTR_MipsSra(driver->matrixMovingDir.m[0][2], VEH_LAP_MOVING_DIR_SHIFT)), 1);
	CTC2(CTR_PackS16Pair(CTR_MipsSra(driver->matrixMovingDir.m[1][2], VEH_LAP_MOVING_DIR_SHIFT),
	                     CTR_MipsSra(driver->matrixMovingDir.m[2][2], VEH_LAP_MOVING_DIR_SHIFT)),
	     2);

	CTR_GteLoadSVec3SlotV0(&nodeDelta);
	gte_mvmva(0, 0, 0, 3, 0);

	s32 projection = MFC2_S(25);
	s32 wrongWayTest = MFC2_S(26);
	s32 progress = ((u32)progressNode->distToFinish << VEH_LAP_TRACK_DISTANCE_SCALE_SHIFT) + (projection >> VEH_LAP_PROJECTED_DISTANCE_SHIFT);
	s32 trackLength = (s32)nodes[0].distToFinish << VEH_LAP_TRACK_DISTANCE_SCALE_SHIFT;

	driver->distanceToFinish_curr = progress;
	// NOTE(aalhendi): Retail uses signed div/mfhi for this remainder.
	driver->distanceToFinish_curr = progress % trackLength;

	if (wrongWayTest < VEH_LAP_WRONG_WAY_DOT_LIMIT)
	{
		driver->actionsFlagSet &= ~ACTION_DRIVING_WRONG_WAY;
	}
	else
	{
		driver->actionsFlagSet |= ACTION_DRIVING_WRONG_WAY;
	}

	if (((driver->actionsFlagSet & ACTION_CHECKPOINT_BRANCH_PENDING) != 0) && (driver->checkpoint.currentIndex != (u8)checkpointIndex))
	{
		driver->checkpoint.branchChoiceIndex = checkpointIndex;
		driver->actionsFlagSet &= ~ACTION_CHECKPOINT_BRANCH_PENDING;
	}

	if (checkpointNode->nextIndex_left != VEH_LAP_INVALID_CHECKPOINT)
	{
		driver->actionsFlagSet |= ACTION_CHECKPOINT_BRANCH_PENDING;
	}

	driver->checkpoint.currentIndex = checkpointIndex;
}
