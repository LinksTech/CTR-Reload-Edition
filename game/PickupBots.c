#include <common.h>
#include <platform/native_mods.h>

void PickupBots_Init(void)
{
	int hub;
	int lev = sdata->gGT->levelID;

	// BOSS (MODS page): the boss's own weapon pattern, spread over this track.
	if (NativeMods_BossIndex() >= 0)
	{
		sdata->bossWeaponMeta = NativeMods_BossWeapons(1);
		return;
	}

	// get hubID of level
	hub = data.metaDataLEV[lev].hubID;

	// If Level ID is Oxide Station
	if (lev == OXIDE_STATION)
	{
		hub = 0;
	}

	if (hub > -1)
	{
		// set pointer to boss weapon meta
		sdata->bossWeaponMeta = data.bossWeaponMetaPtr[hub];
	}
	return;
}

enum
{
	PICKUPBOTS_ITEM_NONE = HELD_ITEM_NONE,
	PICKUPBOTS_ITEM_INVALID = -1,
	PICKUPBOTS_ITEM_BOMB = HELD_ITEM_BOMB_1X,
	PICKUPBOTS_ITEM_MISSILE = HELD_ITEM_MISSILE_1X,
	PICKUPBOTS_ITEM_TNT = HELD_ITEM_TNT,
	PICKUPBOTS_ITEM_POTION = HELD_ITEM_POTION,
	PICKUPBOTS_SHOOT_ID_BOMB_MISSILE = 2,
	PICKUPBOTS_SHOOT_FLAG_RANDOM = 0x1,
	PICKUPBOTS_SHOOT_FLAG_BACKWARD = 0x2,
	PICKUPBOTS_VOICELINE_BOMB = 10,
	PICKUPBOTS_VOICELINE_MISSILE = 11,
	PICKUPBOTS_VOICELINE_MINE_DROP = 0xf,
	PICKUPBOTS_CLOSE_DIST_SQ_BIAS = 0x90001,
	PICKUPBOTS_CLOSE_DIST_SQ_RANGE = 0x13affff,
	PICKUPBOTS_COOLDOWN_RANDOM_MASK = 0xff,
	PICKUPBOTS_COOLDOWN_BASE_FRAMES = 0xf0,
	PICKUPBOTS_LEADING_ATTACK_ROLL_MOD = 200,
	PICKUPBOTS_LEADING_ATTACK_POWERED_ROLL_MOD = 100,
	PICKUPBOTS_LEADING_ATTACK_POWERED_ROLL_THRESHOLD = 0x32,
	PICKUPBOTS_TRAILING_ATTACK_ROLL_MOD = 800,
	PICKUPBOTS_TRAILING_ATTACK_MISSILE_THRESHOLD = 2,
	PICKUPBOTS_TRAILING_ATTACK_BOMB_THRESHOLD = 4,
	PICKUPBOTS_TRAILING_ATTACK_DISTANCE_TO_FINISH_MIN = 16000,
	PICKUPBOTS_BOSS_PATH_REQUEST_FRAMES = 0x1e,
	PICKUPBOTS_BOSS_JUICE_COUNTER_MAX = 5,
	PICKUPBOTS_BOSS_SPEED_MIN = 0x1f41,
	PICKUPBOTS_BOSS_COOLDOWN_RANDOM_MASK = 0x10,
	PICKUPBOTS_BOSS_COOLDOWN_BASE_FRAMES = 0xc,
	PICKUPBOTS_BOSS_LOSS_COOLDOWN_STEP = 4,
	PICKUPBOTS_BOSS_CHECKPOINT_DISTANCE_SHIFT = 3,
	PICKUPBOTS_UPDATE_START_DELAY = 0x4b00,
	PICKUPBOTS_UPDATE_NEGATIVE_MODE_START_DELAY = 0x12c0,
};


static int PickupBots_IsBotWeaponReady(struct Driver *driver)
{
#if defined(CTR_NATIVE)
	// NOTE(aalhendi): Retail can read PS1 low memory when a boss-race
	// end-of-race rank slot is empty. Native treats that slot as no bot.
	if (driver == NULL)
	{
		return 0;
	}
#endif

	return ((driver->actionsFlagSet & ACTION_BOT) != 0) && ((driver->botData.botFlags & BOT_FLAG_DAMAGE_ACTIVE) == 0) &&
	       ((driver->actionsFlagSet & ACTION_RACE_FINISHED) == 0) && (driver->botData.weaponCooldown == 0) && (driver->instTntRecv == NULL) &&
	       (driver->clockReceive == 0);
}

static int PickupBots_IsCloseToPlayer(struct Driver *player, struct Driver *bot)
{
	int x = player->instSelf->matrix.t[0] - bot->instSelf->matrix.t[0];
	int z = player->instSelf->matrix.t[2] - bot->instSelf->matrix.t[2];

	return (u32)((x * x + z * z) - PICKUPBOTS_CLOSE_DIST_SQ_BIAS) < PICKUPBOTS_CLOSE_DIST_SQ_RANGE;
}

// Every CPU attack is counted for the report at exit (NativeMods_NoteItem;
// nothing of the race changes): the held item the bot used.
static void PickupBots_SetCooldown(struct Driver *bot)
{
	NativeMods_NoteItem(1, bot->heldItemID);
	bot->botData.weaponCooldown = (MixRNG_Scramble() & PICKUPBOTS_COOLDOWN_RANDOM_MASK) + PICKUPBOTS_COOLDOWN_BASE_FRAMES;

	// DIFFICULTY EXTREME (MODS page): half the time to the next attack.
	if (NativeMods_CpuExtreme())
	{
		bot->botData.weaponCooldown = (bot->botData.weaponCooldown >> 1);
	}
}

static void PickupBots_PlayVoice(u32 voiceID, struct Driver *attacker, struct Driver *victim)
{
	Voiceline_RequestPlay(voiceID, data.characterIDs[attacker->driverID], data.characterIDs[victim->driverID]);
}

// CPU ITEMS (MODS page, ITEMS CHAOS or CUSTOM, or DIFFICULTY EXTREME;
// platform/native_mods.h): the item of a CPU attack, drawn by the item box
// itself for the bot's place (VehPhysGeneral_SetHeldItem - CHAOS and CUSTOM by
// their weights, DEFAULT the set of the bot's rank, one warpball at a time),
// the stack of a triple not kept. With ITEMS at DEFAULT under EXTREME aimed at
// the player: a bot ahead of him takes no missile (it would fly ahead, a bomb
// thrown backwards instead), a bot behind no TNT or potion (dropped behind
// it, a missile instead).
static int PickupBots_ModsItem(struct Driver *bot, int ahead)
{
	int item;

	VehPhysGeneral_SetHeldItem(bot);
	item = bot->heldItemID;
	bot->numHeldItems = 0;

	if (NativeMods_CpuExtreme() && !NativeMods_ItemsActive())
	{
		if (ahead && ((item == HELD_ITEM_MISSILE_1X) || (item == HELD_ITEM_MISSILE_3X)))
		{
			item = HELD_ITEM_BOMB_1X;
		}
		else if (!ahead && ((item == PICKUPBOTS_ITEM_TNT) || (item == PICKUPBOTS_ITEM_POTION)))
		{
			item = HELD_ITEM_MISSILE_1X;
		}
	}

	return item;
}

// --mods-selftest (platform/native_mods.c): 65536 CPU attacks of a bot in
// fourth place on lap 2, at the play mods the caller set, half of them ahead
// of the player - each item within 5 percent of the share the box gives it
// (CHAOS, CUSTOM; the warpball flag cleared before each), or only items of the
// bot's set (DEFAULT under EXTREME), never one the side rule takes away. The
// random number is put back. Returns the failures.
int PickupBots_ItemSelfTest(const char *what, int *checks)
{
	static struct GameTracker race;
	static struct Driver bot;
	struct GameTracker *const savedGT = sdata->gGT;
	const int savedRandom = sdata->randomNumber;
	const int aimed = NativeMods_CpuExtreme() && !NativeMods_ItemsActive();
	int counts[2][HELD_ITEM_ROULETTE + 1] = {{0}};
	u8 items[NATIVE_MODS_ITEM_IDS];
	const int itemCount = NativeMods_RaceItems(items);
	int total = 0;
	int failures = 0;
	int i;

	for (i = 0; i < itemCount; i++)
	{
		total += NativeMods_ItemWeight(items[i]);
	}

	memset(&race, 0, sizeof(race));
	memset(&bot, 0, sizeof(bot));
	race.gameMode1 = ARCADE_MODE;
	race.numPlyrCurrGame = 1;
	race.numBotsNextGame = 7;
	bot.driverRank = 3;
	bot.lapIndex = 1;
	bot.actionsFlagSet = ACTION_BOT;
	sdata->gGT = &race;

	for (i = 0; i < 0x10000; i++)
	{
		int item;

		race.gameMode1 &= ~WARPBALL_HELD;
		item = PickupBots_ModsItem(&bot, i & 1);
		counts[i & 1][(item <= HELD_ITEM_ROULETTE) ? item : HELD_ITEM_ROULETTE]++;
	}

	sdata->gGT = savedGT;
	sdata->randomNumber = savedRandom;

	for (i = 0; i <= HELD_ITEM_ROULETTE; i++)
	{
		const int all = counts[0][i] + counts[1][i];
		int inSet = 0;
		int bad;
		int k;

		// Fourth place of eight is the second race set (SetHeldItem, 1P arcade).
		for (k = 0; k < 0x34; k++)
		{
			inSet |= (data.RNG_itemSetRace2[k] == i);
		}

		if (aimed)
		{
			const int missile = (i == HELD_ITEM_MISSILE_1X) || (i == HELD_ITEM_MISSILE_3X);
			const int dropped = (i == PICKUPBOTS_ITEM_TNT) || (i == PICKUPBOTS_ITEM_POTION);

			bad = (missile && (counts[1][i] != 0)) || (dropped && (counts[0][i] != 0)) ||
			      ((all != 0) && !inSet && (i != HELD_ITEM_BOMB_1X) && (i != HELD_ITEM_MISSILE_1X) && (i != HELD_ITEM_MISSILE_3X) && (i != HELD_ITEM_TURBO));
		}
		else
		{
			int raceItem = 0;
			int weight;
			int share;

			for (k = 0; k < itemCount; k++)
			{
				raceItem |= (items[k] == i);
			}
			weight = raceItem ? NativeMods_ItemWeight(i) : 0;
			share = (total > 0) ? ((0x10000 * weight) / total) : 0;
			const int off = all - share;

			bad = (share == 0) ? (all != 0) : (((off < 0) ? -off : off) * 20 > share);
		}

		(*checks)++;
		if (bad)
		{
			printf("mods selftest: CPU items, %s, item %d came %d times ahead and %d behind (in the bot's set %d)\n", what, i, counts[1][i], counts[0][i], inSet);
			failures++;
		}
	}

	{
		char line[200] = "";

		for (i = 0; i <= HELD_ITEM_ROULETTE; i++)
		{
			if ((counts[0][i] + counts[1][i]) != 0)
			{
				snprintf(&line[strlen(line)], sizeof(line) - strlen(line), " %d=%d/%d", i, counts[1][i], counts[0][i]);
			}
		}
		printf("mods selftest: CPU items, %s, of 65536 attacks (held item id=ahead/behind):%s\n", what, line);
	}
	return failures;
}

// CPU ITEMS: the attack with the item drawn (PickupBots_ModsItem), used at
// once - TNT and potion dropped and bomb and missile shot as the branches of
// UpdateArcade do (one of a triple; a bomb of a bot ahead backwards when the
// attack is aimed), the others as the player's item (VehPickupItem_ShootNow):
// turbo and mask on the bot, the shield without juice (a juiced one only ends
// when its owner fires it, which a bot never does), clock and warpball on the
// others. The race sets hold no invisibility and no super engine (battle
// items; their timers run in the kart's code only), so none is drawn here.
// The voice lines as the four weapons' on the disc.
static void PickupBots_ModsAttack(struct Driver *bot, struct Driver *player, int ahead)
{
	const int item = PickupBots_ModsItem(bot, ahead);
	const int human = (player->actionsFlagSet & ACTION_BOT) == 0;

	bot->heldItemID = item;

	switch (item)
	{
	case PICKUPBOTS_ITEM_TNT:
	case PICKUPBOTS_ITEM_POTION:
		if (human)
		{
			PickupBots_PlayVoice(PICKUPBOTS_VOICELINE_MINE_DROP, bot, player);
		}
		VehPickupItem_ShootNow(bot, item, 0);
		bot->numWumpas = 0;
		break;

	case HELD_ITEM_BOMB_1X:
	case HELD_ITEM_BOMB_3X:
		if (human)
		{
			PickupBots_PlayVoice(PICKUPBOTS_VOICELINE_BOMB, bot, player);
		}
		VehPickupItem_ShootNow(bot, PICKUPBOTS_SHOOT_ID_BOMB_MISSILE, (ahead && NativeMods_CpuExtreme()) ? PICKUPBOTS_SHOOT_FLAG_BACKWARD : 0);
		break;

	case HELD_ITEM_MISSILE_1X:
	case HELD_ITEM_MISSILE_3X:
		if (human)
		{
			PickupBots_PlayVoice(PICKUPBOTS_VOICELINE_MISSILE, bot, player);
		}
		VehPickupItem_ShootNow(bot, PICKUPBOTS_SHOOT_ID_BOMB_MISSILE, 0);
		break;

	case HELD_ITEM_SHIELD:
	{
		const s8 wumpas = bot->numWumpas;

		bot->numWumpas = 0;
		VehPickupItem_ShootNow(bot, item, 0);
		bot->numWumpas = wumpas;
		break;
	}

	case HELD_ITEM_TURBO:
	case HELD_ITEM_MASK:
	case HELD_ITEM_CLOCK:
	case HELD_ITEM_WARPBALL:
		VehPickupItem_ShootNow(bot, item, 0);
		break;

	default:
		bot->heldItemID = PICKUPBOTS_ITEM_NONE;
		return;
	}

	PickupBots_SetCooldown(bot);
}

// The attack roll of a bot next to the player: ahead (the game: 3 of 200, one
// to each of its branches) or behind (4 of 800); EXTREME twice that, from
// behind twice again while the player leads.
static int PickupBots_ModsRoll(int ahead, struct Driver *player)
{
	int roll = ahead ? 3 : PICKUPBOTS_TRAILING_ATTACK_BOMB_THRESHOLD;

	if (NativeMods_CpuExtreme())
	{
		roll *= 2;
		if (!ahead && (player->driverRank == 0))
		{
			roll *= 2;
		}
	}

	return roll;
}

static void PickupBots_UpdateArcade(void)
{
	struct GameTracker *gGT = sdata->gGT;
	const int modsItems = NativeMods_CpuAllItems();
	const int extreme = NativeMods_CpuExtreme();

	for (int i = 0; i < gGT->numPlyrCurrGame; i++)
	{
		struct Driver *player = gGT->drivers[i];

		if (player->driverRank != 0)
		{
			struct Driver *bot = gGT->driversInRaceOrder[player->driverRank - 1];

			if (PickupBots_IsBotWeaponReady(bot) && PickupBots_IsCloseToPlayer(player, bot))
			{
				int rng = MixRNG_Scramble() % PICKUPBOTS_LEADING_ATTACK_ROLL_MOD;

				// CPU ITEMS: as often as the three branches below (EXTREME more), any item.
				if (modsItems)
				{
					if (rng < PickupBots_ModsRoll(1, player))
					{
						PickupBots_ModsAttack(bot, player, 1);
					}
				}
				else if (rng == 0)
				{
					int weaponID;

					if ((bot->lapIndex != 0) &&
					    (MixRNG_Scramble() % PICKUPBOTS_LEADING_ATTACK_POWERED_ROLL_MOD < PICKUPBOTS_LEADING_ATTACK_POWERED_ROLL_THRESHOLD))
					{
						bot->numWumpas = DRIVER_WUMPA_JUICED_COUNT;
					}

					if ((gGT->elapsedEventTime & 1) != 0)
					{
						bot->heldItemID = PICKUPBOTS_ITEM_TNT;

						if ((player->actionsFlagSet & ACTION_BOT) == 0)
						{
							PickupBots_PlayVoice(PICKUPBOTS_VOICELINE_MINE_DROP, bot, player);
						}

						weaponID = PICKUPBOTS_ITEM_TNT;
					}
					else
					{
						bot->heldItemID = PICKUPBOTS_ITEM_POTION;

						if ((player->actionsFlagSet & ACTION_BOT) == 0)
						{
							PickupBots_PlayVoice(PICKUPBOTS_VOICELINE_MINE_DROP, bot, player);
						}

						weaponID = PICKUPBOTS_ITEM_POTION;
					}

					VehPickupItem_ShootNow(bot, weaponID, 0);
					bot->numWumpas = 0;
					PickupBots_SetCooldown(bot);
				}
				else if (rng == 1)
				{
					bot->heldItemID = PICKUPBOTS_ITEM_BOMB;

					if ((player->actionsFlagSet & ACTION_BOT) == 0)
					{
						PickupBots_PlayVoice(PICKUPBOTS_VOICELINE_BOMB, bot, player);
					}

					VehPickupItem_ShootNow(bot, PICKUPBOTS_SHOOT_ID_BOMB_MISSILE, 0);
					PickupBots_SetCooldown(bot);
				}
				else if (rng == 2)
				{
					bot->heldItemID = PICKUPBOTS_ITEM_MISSILE;

					if ((player->actionsFlagSet & ACTION_BOT) == 0)
					{
						PickupBots_PlayVoice(PICKUPBOTS_VOICELINE_MISSILE, bot, player);
					}

					VehPickupItem_ShootNow(bot, PICKUPBOTS_SHOOT_ID_BOMB_MISSILE, 0);
					PickupBots_SetCooldown(bot);
				}

				bot->heldItemID = PICKUPBOTS_ITEM_NONE;
			}
		}

		// EXTREME: the bot two places ahead too.
		if (extreme && (player->driverRank >= 2))
		{
			struct Driver *bot = gGT->driversInRaceOrder[player->driverRank - 2];

			if (PickupBots_IsBotWeaponReady(bot) && PickupBots_IsCloseToPlayer(player, bot) &&
			    ((MixRNG_Scramble() % PICKUPBOTS_LEADING_ATTACK_ROLL_MOD) < PickupBots_ModsRoll(1, player)))
			{
				PickupBots_ModsAttack(bot, player, 1);
				bot->heldItemID = PICKUPBOTS_ITEM_NONE;
			}
		}

		// EXTREME: from behind at any place, the bots one and two places behind.
		if ((player->driverRank < 3) || (extreme && (player->driverRank < 7)))
		{
			struct Driver *bot = gGT->driversInRaceOrder[player->driverRank + 1];

			if (PickupBots_IsBotWeaponReady(bot) &&
			    (((int)player->lapIndex < (int)gGT->numLaps) || (player->distanceToFinish_curr > PICKUPBOTS_TRAILING_ATTACK_DISTANCE_TO_FINISH_MIN)) &&
			    PickupBots_IsCloseToPlayer(player, bot))
			{
				int rng = MixRNG_Scramble() % PICKUPBOTS_TRAILING_ATTACK_ROLL_MOD;
				int weaponID = PICKUPBOTS_ITEM_NONE;

				// CPU ITEMS: as often as missile and bomb together (EXTREME
				// more), any item, on the last lap too.
				if (modsItems)
				{
					if (rng < PickupBots_ModsRoll(0, player))
					{
						PickupBots_ModsAttack(bot, player, 0);
					}
				}
				else if ((rng < PICKUPBOTS_TRAILING_ATTACK_MISSILE_THRESHOLD) && (bot->lapIndex != (u8)(gGT->numLaps - 1)))
				{
					weaponID = PICKUPBOTS_ITEM_MISSILE;
				}
				else if (rng < PICKUPBOTS_TRAILING_ATTACK_BOMB_THRESHOLD)
				{
					weaponID = PICKUPBOTS_ITEM_BOMB;
				}

				if (weaponID != PICKUPBOTS_ITEM_NONE)
				{
					bot->heldItemID = weaponID;

					if ((player->actionsFlagSet & ACTION_BOT) == 0)
					{
						PickupBots_PlayVoice(PICKUPBOTS_VOICELINE_MISSILE, bot, player);
					}

					VehPickupItem_ShootNow(bot, PICKUPBOTS_SHOOT_ID_BOMB_MISSILE, 0);
					PickupBots_SetCooldown(bot);
				}

				bot->heldItemID = PICKUPBOTS_ITEM_NONE;
			}
		}

		if (extreme && (player->driverRank < 6))
		{
			struct Driver *bot = gGT->driversInRaceOrder[player->driverRank + 2];

			if (PickupBots_IsBotWeaponReady(bot) && PickupBots_IsCloseToPlayer(player, bot) &&
			    ((MixRNG_Scramble() % PICKUPBOTS_TRAILING_ATTACK_ROLL_MOD) < PickupBots_ModsRoll(0, player)))
			{
				PickupBots_ModsAttack(bot, player, 0);
				bot->heldItemID = PICKUPBOTS_ITEM_NONE;
			}
		}
	}
}

static void PickupBots_SetBossCooldown(struct MetaDataBOSS *bossMeta)
{
	struct GameTracker *gGT = sdata->gGT;

	// BOSS (MODS page): as on the first try, no race lost.
	const int lost = (NativeMods_BossIndex() >= 0) ? 0 : (s8)sdata->advProgress.timesLostBossRace[gGT->bossID];

	sdata->bossWeaponCooldown = (RngDeadCoed(&sdata->advRng) & PICKUPBOTS_BOSS_COOLDOWN_RANDOM_MASK) + bossMeta->weaponCooldown +
	                            PICKUPBOTS_BOSS_COOLDOWN_BASE_FRAMES + (lost * PICKUPBOTS_BOSS_LOSS_COOLDOWN_STEP);
}

static struct MetaDataBOSS *PickupBots_GetInitialBossMeta(void)
{
	struct GameTracker *gGT = sdata->gGT;

	// BOSS (MODS page): the first section of his pattern on this track.
	if (NativeMods_BossIndex() >= 0)
	{
		return NativeMods_BossWeapons(0);
	}

	if (gGT->levelID == OXIDE_STATION)
	{
		return data.bossWeaponMetaPtr[0];
	}

	return data.bossWeaponMetaPtr[data.metaDataLEV[gGT->levelID].hubID];
}

static void PickupBots_AdvanceBossMeta(struct Driver *boss)
{
	struct GameTracker *gGT = sdata->gGT;
	struct MetaDataBOSS *bossMeta = sdata->bossWeaponMeta;
	struct MetaDataBOSS *nextMeta = &bossMeta[1];

	if (nextMeta->throwFlag == 0)
	{
		int threshold = gGT->level1->ptr_restart_points[bossMeta->trackCheckpoint].distToFinish << PICKUPBOTS_BOSS_CHECKPOINT_DISTANCE_SHIFT;

		if (threshold < (int)boss->distanceToFinish_curr)
		{
			int preservedThrow = -1;

			if (((bossMeta->weaponType == BOSS_WEAPON_ENCODED_POTION) || (bossMeta->weaponType == BOSS_WEAPON_ENCODED_TNT)) &&
			    (sdata->bossJuiceCounter == PICKUPBOTS_BOSS_JUICE_COUNTER_MAX))
			{
				preservedThrow = bossMeta->throwFlag;
			}

			bossMeta = PickupBots_GetInitialBossMeta();

			if (preservedThrow != -1)
			{
				bossMeta->throwFlag = preservedThrow;
			}
		}
	}
	else
	{
		int threshold = gGT->level1->ptr_restart_points[nextMeta->trackCheckpoint].distToFinish << PICKUPBOTS_BOSS_CHECKPOINT_DISTANCE_SHIFT;

		if ((int)boss->distanceToFinish_curr < threshold)
		{
			int preservedThrow = -1;

			if (((bossMeta->weaponType == BOSS_WEAPON_ENCODED_POTION) || (bossMeta->weaponType == BOSS_WEAPON_ENCODED_TNT)) &&
			    (sdata->bossJuiceCounter == PICKUPBOTS_BOSS_JUICE_COUNTER_MAX))
			{
				preservedThrow = bossMeta->throwFlag;
			}

			bossMeta = nextMeta;

			if (preservedThrow != -1)
			{
				bossMeta->throwFlag = preservedThrow;
			}
		}
	}

	sdata->bossWeaponMeta = bossMeta;
}

static void PickupBots_UpdateBossPathRequest(struct Driver *boss)
{
	if (sdata->bossWeaponMeta->pathChangeDisabled != 0)
	{
		return;
	}

	if (sdata->bossPathRequestTimer == PICKUPBOTS_BOSS_PATH_REQUEST_FRAMES)
	{
		if ((boss->botData.botFlags & BOT_FLAG_BOSS_PATH_ACTIVE) != 0)
		{
			return;
		}

		if (sdata->bossPathRequestPhase == 0)
		{
			if (boss->botData.botPath == 2)
			{
				boss->botData.desiredPath_BossOnly = 1;
				sdata->bossPathRequestTimer = 0;
				boss->botData.botFlags |= BOT_FLAG_BOSS_PATH_REQUESTED;
			}
			else if (boss->botData.botPath == 1)
			{
				boss->botData.desiredPath_BossOnly = 0;
				sdata->bossPathRequestTimer = 0;
				sdata->bossPathRequestPhase = boss->botData.botPath;
				boss->botData.botFlags |= BOT_FLAG_BOSS_PATH_REQUESTED;
			}
		}
		else
		{
			if (boss->botData.botPath == 0)
			{
				boss->botData.desiredPath_BossOnly = 1;
				sdata->bossPathRequestTimer = 0;
				boss->botData.botFlags |= BOT_FLAG_BOSS_PATH_REQUESTED;
			}
			else if (boss->botData.botPath == 1)
			{
				boss->botData.desiredPath_BossOnly = 2;
				sdata->bossPathRequestTimer = 0;
				sdata->bossPathRequestPhase = 0;
				boss->botData.botFlags |= BOT_FLAG_BOSS_PATH_REQUESTED;
			}
		}
	}
	else if ((boss->botData.botFlags & BOT_FLAG_BOSS_PATH_REQUESTED) == 0)
	{
		sdata->bossPathRequestTimer++;
	}
}

static int PickupBots_GetBossWeaponID(struct MetaDataBOSS *bossMeta)
{
	int weaponID = bossMeta->weaponType;

	if (weaponID == BOSS_WEAPON_ENCODED_TNT)
	{
		weaponID = PICKUPBOTS_ITEM_TNT;
	}
	else if (weaponID == BOSS_WEAPON_ENCODED_BOMB)
	{
		weaponID = PICKUPBOTS_ITEM_BOMB;
	}
	else if (weaponID == BOSS_WEAPON_ENCODED_POTION)
	{
		weaponID = PICKUPBOTS_ITEM_POTION;
	}
	else if (weaponID == BOSS_WEAPON_NONE)
	{
		weaponID = PICKUPBOTS_ITEM_INVALID;
	}

	return weaponID;
}

static int PickupBots_UpdateBossJuice(struct MetaDataBOSS *bossMeta, int weaponID)
{
	u16 juiceFlag = bossMeta->juiceFlag;

	if ((juiceFlag & BOSS_WEAPON_RANDOM_JUICE) == 0)
	{
		sdata->bossJuiceCounter = 0;
		return weaponID;
	}

	if (sdata->bossJuiceCounter < PICKUPBOTS_BOSS_JUICE_COUNTER_MAX)
	{
		sdata->bossJuiceCounter++;
		return weaponID;
	}

	if (bossMeta->weaponType == BOSS_WEAPON_ENCODED_TNT)
	{
		weaponID = PICKUPBOTS_ITEM_TNT;

		if (bossMeta->throwFlag != BOSS_WEAPON_NORMAL)
		{
			bossMeta->throwFlag = BOSS_WEAPON_NORMAL;
			sdata->bossJuiceCounter = PICKUPBOTS_BOSS_JUICE_COUNTER_MAX;
			bossMeta->juiceFlag = juiceFlag | BOSS_WEAPON_JUICED;
			return weaponID;
		}
	}
	else if (bossMeta->weaponType == BOSS_WEAPON_ENCODED_BOMB)
	{
		weaponID = PICKUPBOTS_ITEM_BOMB;

		if ((juiceFlag & BOSS_WEAPON_JUICED) == 0)
		{
			bossMeta->juiceFlag = juiceFlag | BOSS_WEAPON_JUICED;
			sdata->bossJuiceCounter = PICKUPBOTS_BOSS_JUICE_COUNTER_MAX;
			return PICKUPBOTS_ITEM_TNT;
		}

		bossMeta->juiceFlag = juiceFlag & ~BOSS_WEAPON_JUICED;
		sdata->bossJuiceCounter = 0;
		return weaponID;
	}
	else if (bossMeta->weaponType == BOSS_WEAPON_ENCODED_POTION)
	{
		weaponID = PICKUPBOTS_ITEM_POTION;

		if (bossMeta->throwFlag != BOSS_WEAPON_NORMAL)
		{
			bossMeta->throwFlag = BOSS_WEAPON_NORMAL;
			sdata->bossJuiceCounter = PICKUPBOTS_BOSS_JUICE_COUNTER_MAX;
			bossMeta->juiceFlag |= BOSS_WEAPON_JUICED;
			return weaponID;
		}
	}
	else
	{
		return weaponID;
	}

	bossMeta->throwFlag = BOSS_WEAPON_THROW;
	sdata->bossJuiceCounter = 0;
	bossMeta->juiceFlag &= ~BOSS_WEAPON_JUICED;
	return weaponID;
}

static void PickupBots_UpdateBoss(void)
{
	struct GameTracker *gGT = sdata->gGT;
	struct Driver *boss = gGT->drivers[1];
	struct Driver *player = gGT->drivers[0];
	struct MetaDataBOSS *bossMeta = sdata->bossWeaponMeta;

	if (((boss->botData.botFlags & BOT_FLAG_DAMAGE_ACTIVE) != 0) || ((boss->actionsFlagSet & ACTION_RACE_FINISHED) != 0) || (boss->instTntRecv != NULL) ||
	    (boss->clockReceive != 0) || (boss->botData.aiPhysics.speedLinear < PICKUPBOTS_BOSS_SPEED_MIN))
	{
		PickupBots_SetBossCooldown(bossMeta);
		return;
	}

	PickupBots_AdvanceBossMeta(boss);
	bossMeta = sdata->bossWeaponMeta;

	PickupBots_UpdateBossPathRequest(boss);

	if (sdata->bossWeaponCooldown > 0)
	{
		sdata->bossWeaponCooldown--;
		return;
	}

	PickupBots_SetBossCooldown(bossMeta);

	int weaponID = PickupBots_UpdateBossJuice(bossMeta, PickupBots_GetBossWeaponID(bossMeta));
	int throwFlag = bossMeta->throwFlag;

	// BOSS (MODS page): counted for the report at exit.
	if ((weaponID >= 0) && (NativeMods_BossIndex() >= 0))
	{
		NativeMods_NoteItem(2, weaponID);
	}
	int weaponFlags = (throwFlag == BOSS_WEAPON_THROW) ? PICKUPBOTS_SHOOT_FLAG_RANDOM : 0;

	if (weaponID >= 0)
	{
		u8 oldWumpa = boss->numWumpas;
		boss->numWumpas = ((bossMeta->juiceFlag & BOSS_WEAPON_JUICED) != 0) ? DRIVER_WUMPA_JUICED_COUNT : 0;
		boss->heldItemID = weaponID;

		if ((u16)(weaponID - PICKUPBOTS_ITEM_TNT) < 2)
		{
			PickupBots_PlayVoice(PICKUPBOTS_VOICELINE_MINE_DROP, boss, player);
		}
		else
		{
			weaponFlags |= PICKUPBOTS_SHOOT_FLAG_BACKWARD;
			PickupBots_PlayVoice(PICKUPBOTS_VOICELINE_BOMB, boss, player);
		}

		if (boss->heldItemID == PICKUPBOTS_ITEM_BOMB)
		{
			VehPickupItem_ShootNow(boss, PICKUPBOTS_SHOOT_ID_BOMB_MISSILE, (s16)weaponFlags);
		}
		else if ((boss->heldItemID == PICKUPBOTS_ITEM_POTION) && (weaponFlags == PICKUPBOTS_SHOOT_FLAG_RANDOM) &&
		         ((NativeMods_BossIndex() >= 0) ? (data.characterIDs[1] == NITROS_OXIDE) : (gGT->levelID == OXIDE_STATION)))
		{
			VehPickupItem_ShootNow(boss, weaponID, PICKUPBOTS_SHOOT_FLAG_RANDOM);
			VehPickupItem_ShootNow(boss, weaponID, PICKUPBOTS_SHOOT_FLAG_RANDOM);
		}
		else
		{
			VehPickupItem_ShootNow(boss, weaponID, (s16)weaponFlags);

			if ((boss->heldItemID == PICKUPBOTS_ITEM_TNT) && (bossMeta->throwFlag == BOSS_WEAPON_NORMAL) &&
			    (sdata->bossJuiceCounter != PICKUPBOTS_BOSS_JUICE_COUNTER_MAX))
			{
				sdata->bossJuiceCounter = PICKUPBOTS_BOSS_JUICE_COUNTER_MAX;
			}
		}

		boss->heldItemID = PICKUPBOTS_ITEM_NONE;
		boss->numWumpas = oldWumpa;
	}
}

void PickupBots_Update(void)
{
	struct GameTracker *gGT = sdata->gGT;

	// BOSS (MODS page): the boss race's start and its weapons; a boss race
	// whose boss found no nav path has nobody to throw anything.
	const int modsBoss = (NativeMods_BossIndex() >= 0);

	if (modsBoss && ((gGT->numBotsNextGame == 0) || (gGT->drivers[1] == NULL)))
	{
		return;
	}

	if ((gGT->numBotsNextGame == 0) || (gGT->elapsedEventTime < PICKUPBOTS_UPDATE_START_DELAY))
	{
		if ((gGT->gameMode1 >= 0) && !modsBoss)
		{
			return;
		}

		if (gGT->elapsedEventTime < PICKUPBOTS_UPDATE_NEGATIVE_MODE_START_DELAY)
		{
			return;
		}
	}

	if (((gGT->gameMode1 & (ADVENTURE_BOSS | END_OF_RACE)) != ADVENTURE_BOSS) && !(modsBoss && ((gGT->gameMode1 & END_OF_RACE) == 0)))
	{
		if (gGT->numPlyrCurrGame == 0)
		{
			return;
		}

		PickupBots_UpdateArcade();
		return;
	}

	PickupBots_UpdateBoss();
}
