#ifndef NATIVE_MODS_H
#define NATIVE_MODS_H

// THE MODS OF THE ARCADE RACE (platform/native_mods.c): who sits on the CPU
// seats of a one-player arcade race - single race and cup, on the disc tracks
// and in NITRO-PIT (NITRO RACE and NITRO CUP). Chosen on the MODS page
// (game/native_mods_page.c), reached from the MODS box of the track select
// and the MODS line of the cup select (not in the TIME TRIAL of NITRO-PIT);
// saved in ctr-settings.cfg like the
// GRAPHICS page. At DEFAULT nothing here changes a load: the bots are the
// retail ones LOAD_Robots1P chose.
//
//   CPU DRIVERS   DEFAULT        the fixed retail troupe of LOAD_Robots1P
//                 ALL RANDOM     every installed driver, retail (ids 0..14)
//                                and custom, with the same chance
//                 ONLY SELECTED  only the drivers ticked on SELECT DRIVERS
//
// THE RULE: the pool is every driver the value allows, without the player's
// own (the retail id of seat 0, and the custom file it picked) and without a
// custom file that cannot sit on its template (other frame counts than the
// template's model; checked at the draw where it can be, else at the binding,
// where such a seat gets another driver). The seven CPU seats take the pool in a shuffled order;
// a pool smaller than seven is shuffled again for the seats left, so a driver
// appears more than once only then (one ticked driver: seven times). An empty
// pool (nothing ticked, or only the player's own) is DEFAULT for that race. A
// new draw for every race load, except inside a cup: the first race of a cup
// draws, the next three take the same seats.

struct BigHeader;
struct Model;

#define NATIVE_MODS_CPU_DEFAULT  0
#define NATIVE_MODS_CPU_ALL      1
#define NATIVE_MODS_CPU_SELECTED 2

// The first retail driver and the count of them in the driver list: index
// 0..14 is retail driver 0..14, index 15 on is roster file index - 15.
#define NATIVE_MODS_RETAIL_COUNT 15

// The value. The setter clamps to the values above and saves at once
// (Platform_SettingsSave; nothing is written under --settings-defaults).
int NativeMods_CpuDrivers(void);
void NativeMods_SetCpuDrivers(int value);

// SELECT DRIVERS: every driver, the fifteen retail ones first (id order), then
// the files of the custom roster (NativeChar_RosterFileCount, never the
// placeholders of --dev-grid-fill) in roster order. Name: the game's long
// name or the CHRI name; "" outside. Selected: 1 for a ticked driver - none
// is ticked until it is ticked. The setter saves at once. SelectedCount: the
// ticked drivers of the list.
int NativeMods_DriverCount(void);
const char *NativeMods_DriverName(int index);
int NativeMods_DriverSelected(int index);
void NativeMods_SetDriverSelected(int index, int on);
int NativeMods_SelectedCount(void);

// The status line of the MODS box, at most 11 characters of FONT_SMALL (the
// box is as wide as LAPS, 164): "OFF" (DEFAULT, or ONLY SELECTED with nothing
// ticked - that is DEFAULT), "ALL RANDOM", "<n> SELECTED". With play mods away
// from their normal value (below): "<n> MOD(S)" beside DEFAULT, else the CPU
// value short and the count - "RANDOM +<n>", "<k> SEL +<n>". With BOSS
// CHALLENGE, which leaves CPU DRIVERS aside: the boss alone ("BOSS: ROO", the
// bare name or "RANDOM BOSS" where that does not fit), or "BOSS +<n>".
const char *NativeMods_StatusText(void);

// THE PLAY MODS, freely combined, for the same races as CPU DRIVERS (the rule
// of NativeMods_MenuOffered at the menu, of the race below). Each has a normal
// value, and at its normal value it is off: no other path, no other random
// number.
//   GRAVITY     20..200 %, normal 100. The karts and the CPU drivers fall at
//               this share of their gravity; a low-gravity stretch of the
//               track (QUADBLOCK_FLAG_LOW_GRAVITY, BOT_FLAG_MOON_GRAVITY, 41 %)
//               keeps the lower of the two. The hang-time turbo is judged by
//               the air time times the gravity share the jump flew at against
//               the share the game gives there, so a jump earns what it earns
//               at the game's own gravity, on every step.
//   GRIP        0..100 %, normal 100. Road terrain (full speed and full grip:
//               asphalt, wood, stone, hardpack, metal and the three asphalts
//               of the water tracks) is blended towards ice: at 0 % it is ice,
//               as Polar Pass's (round 1's ICY TRACKS); in between its numbers
//               (speed, grip, counter-steer, friction, the bots' scales) lie
//               between the two, and its flags are the ice's below 50 %. Water,
//               mud, grass, dirt, snow, the slow and the sideslip terrain keep
//               theirs; turbo pads and ramps are not terrain at all.
//   NIGHT       0..90 %, normal 0. The 3D world (track, drivers, objects, sky)
//               is darkened by this share with a slight blue: one multiply over
//               the view after the world and before the HUD, so HUD, menus and
//               texts stay as they are, for the retail drawing and the native
//               drivers alike.
//   ITEMS       DEFAULT (the game's sets by rank), CHAOS (every race item with
//               the same chance, whatever the rank), CUSTOM (the weights of
//               ITEM WEIGHTS, whatever the rank; all OFF is DEFAULT). A CPU
//               driver, which never takes a box, attacks as often as before
//               with its four weapons - TNT, potion, bomb, missile; CHAOS each
//               with the same chance, CUSTOM by the weights (bomb with the
//               triple bomb, missile with the triple missile).
//   DIFFICULTY  EASY, NORMAL, HARD, EXTREME, normal NORMAL. The bots' place in
//               the game's own difficulty blend (BOTS_Adv_AdjustDifficulty,
//               ArcadeDifficulty / BossDifficulty) moves by -0x50, 0, +0x50,
//               +0xA0 - one and two steps of the cup difficulty menu.
//   BOSS        OFF, RANDOM, ROO, PAPU, JOE, PINSTRIPE, OXIDE. One against one
//               with the boss: his model, his strength (BossDifficulty), his
//               weapon pattern (data.BossWeapon*, its sections spread over this
//               track's checkpoints) and his path changes; CPU DRIVERS sits
//               out. Without a nav path the race has no boss and says so.
enum
{
	NATIVE_MODS_GRAVITY = 0,
	NATIVE_MODS_GRIP,
	NATIVE_MODS_NIGHT,
	NATIVE_MODS_ITEMS,
	NATIVE_MODS_DIFFICULTY,
	NATIVE_MODS_BOSS,
	NATIVE_MODS_VALUES
};

#define NATIVE_MODS_ITEMS_DEFAULT 0
#define NATIVE_MODS_ITEMS_CHAOS   1
#define NATIVE_MODS_ITEMS_CUSTOM  2

#define NATIVE_MODS_DIFF_EASY    0
#define NATIVE_MODS_DIFF_NORMAL  1
#define NATIVE_MODS_DIFF_HARD    2
#define NATIVE_MODS_DIFF_EXTREME 3

#define NATIVE_MODS_BOSS_OFF       0
#define NATIVE_MODS_BOSS_RANDOM    1
#define NATIVE_MODS_BOSS_ROO       2
#define NATIVE_MODS_BOSS_PAPU      3
#define NATIVE_MODS_BOSS_JOE       4
#define NATIVE_MODS_BOSS_PINSTRIPE 5
#define NATIVE_MODS_BOSS_OXIDE     6

// ITEM WEIGHTS: a level per held item id, weights 0 / 1 / 2 / 4.
#define NATIVE_MODS_WEIGHT_OFF    0
#define NATIVE_MODS_WEIGHT_LOW    1
#define NATIVE_MODS_WEIGHT_NORMAL 2
#define NATIVE_MODS_WEIGHT_HIGH   3
#define NATIVE_MODS_ITEM_IDS      16

// The page's value of one mod, its text ("60 %", "CUSTOM", "HARD", "ROO"),
// its normal value, and the setters (clamped, saved at once). Step: left (-1)
// or right (+1) - a percentage to the next multiple of 10 in that direction
// (41 goes to 40 or 50), a choice to the next one, no wrap; 0 when nothing
// changed. ItemLevel / SetItemLevel: ITEM WEIGHTS.
int NativeMods_Value(int which);
const char *NativeMods_ValueText(int which);
int NativeMods_NormalValue(int which);
void NativeMods_SetValue(int which, int value);
int NativeMods_Step(int which, int dir);
int NativeMods_ItemLevel(int item);
void NativeMods_SetItemLevel(int item, int level);

// The race items in id order (the items of the four race sets, without the
// spring), as ITEM WEIGHTS lists them; their names. The count.
int NativeMods_RaceItems(u8 items[NATIVE_MODS_ITEM_IDS]);
const char *NativeMods_ItemName(int item);

// 1 when ITEMS is CUSTOM and every race item is OFF (that is DEFAULT).
int NativeMods_ItemsAllOff(void);

// 1 while the race in progress is one the mods are for and the mod is away
// from its normal value (ITEMS: CHAOS, or CUSTOM with an item on; BOSS: the
// boss this race was loaded with sits on seat 1).
int NativeMods_Active(int which);

// GRAVITY, game/Vehicle/VehPhysForce.c and game/BOTS.c: the gravity share in
// percent for a stretch whose own share is zonePercent (100, or 41 on a
// low-gravity stretch) - zonePercent itself unless the mod is active.
int NativeMods_GravityPercent(int zonePercent);

// GRAVITY, the hang-time turbo (game/UI/UI_Meter.c): NoteGravity, every
// gravity frame of a kart (the share it fell at and the game's own, the
// frame's ms, airborne or not) weighs the flight; AirTime, while airborne,
// keeps the air time past the meter's 2400 ms; HangTime, at landing, is the
// air time the turbo is judged by (the meter in ms, unchanged unless the mod
// is active). given and full are the fire steps the turbo got and would have
// got from the meter (-1 = none), for the report at exit.
void NativeMods_NoteGravity(int driverID, int percent, int ownPercent, int ms, int airborne);
void NativeMods_AirTime(int driverID, int airTime);
int NativeMods_HangTime(int driverID, int jumpMeter);
void NativeMods_NoteLanding(int given, int full);

// GRIP at 0 %: the terrain the game takes for a ground of this type -
// TERRAIN_ICE for road terrain, else the type itself; bot 1 for a CPU
// driver's nav frame, 0 for a kart's ground (the count at exit).
u8 NativeMods_IcyTerrain(u8 terrainType, int bot);

// GRIP between 0 and 100 %: the terrain entry to use for terrain type
// terrainType whose entry is meta - a blend of road and ice for road terrain,
// meta itself otherwise.
struct Terrain;
struct Terrain *NativeMods_GripTerrain(u8 terrainType, struct Terrain *meta, int bot);

// NIGHT: the multiply colour for a darkening of percent (0..90): 1 and the
// colour in rgb (0..255 a channel, 255 = unchanged), 0 at 0 %.
int NativeMods_NightColor(int percent, u8 rgb[3]);

// game/MAIN/MainFrame_RenderFrame.c, once a frame after the world is queued
// and before the HUD's draw environment: the NIGHT multiply, the line of a
// boss race without a boss, and the shots of --dev-mods-play.
struct GameTracker;
void NativeMods_RaceFrame(struct GameTracker *gGT);

// ITEMS: 1 while it is active; the weight of a held item id for a box (CHAOS
// 1, CUSTOM 0/1/2/4) and of a CPU weapon (TNT, potion, bomb, missile).
int NativeMods_ItemsActive(void);
int NativeMods_ItemWeight(int item);
int NativeMods_CpuWeaponWeight(int weapon);

// DIFFICULTY, game/BOTS.c: the bots' difficulty for the game's own value
// (after its clamp at 0) - unchanged unless the mod is active. boss 1 for the
// value of a boss challenge (the measuring base of --dev-mods-play is not
// taken there).
s16 NativeMods_BotDifficulty(s16 difficulty, int boss);

// BOSS: the boss index of this race (0 Roo, 1 Papu, 2 Joe, 3 Pinstripe,
// 4 Oxide - gGT->bossID's numbering, the row of BossDifficulty) while the
// mod is active, else -1; the boss's weapon pattern for this track
// (PickupBots.c; rebuild 1 at the race's start, 0 for the first section).
struct MetaDataBOSS;
int NativeMods_BossIndex(void);
struct MetaDataBOSS *NativeMods_BossWeapons(int rebuild);

// GRAVITY and GRIP, for the report at exit: a CPU driver put back on its path
// after a kill plane (BOTS_Killplane), counted while a play mod is active.
void NativeMods_NoteBotRescue(void);

// ITEMS and BOSS, for the report at exit: an item a box gave (bot 0), a CPU
// driver's attack (bot 1) or a boss weapon (bot 2).
void NativeMods_NoteItem(int bot, int item);

// game/PlayLevel.c: a driver crossed the line after lap lapIndex (0-based)
// in ticks (960 a second); logged under --dev-mods-play (the measuring of
// DIFFICULTY).
struct Driver;
void NativeMods_NoteLap(const struct Driver *driver, int lapIndex, int ticks);

// game/DebugMenu.c, beside the [CTR Race] line: one line with the play mods of
// this race, only when one is active.
void NativeMods_LogRace(void);

// main.c, --dev-mods-play <set>[/<set>...]: the play mods of this run, never
// saved (the lines of the file do not count). A set is "none" or a comma list
// of gravity=<n>, grip=<n>, night=<n>, items=default|chaos|custom,
// w<item id>=<0..3> (ITEM WEIGHTS), difficulty=easy|normal|hard|extreme,
// boss=off|random|roo|papu|joe|pinstripe|oxide and the measuring keys
// base=<n> (the bots' difficulty before DIFFICULTY, as the cup menu's
// 80 / 160 / 240) and shot=<race frame> (a snapshot of that frame); round 1's
// words moon, icy and chaos are gravity=41, grip=0 and items=chaos. Each race
// load takes the next set, the last one stays. 0 for another form.
int NativeMods_SetDevPlay(const char *list);

// main.c, --mods-selftest (ctest mods_selftest): the play mods without a race -
// settings lines, status line, --dev-mods-play, steps, gravity, hang time,
// grip blend, night colour, difficulty, boss sections, item draws. 0 = passed.
int NativeMods_SelfTest(void);

// The menu side of the race rule: the MODS box is offered in the track select
// and the cup select of a one-player arcade race - on the disc and in
// NITRO-PIT, not in CRYSTAL, CTR or TIME TRIAL of NITRO-PIT, adventure, time
// trial, battle or with two or more players.
int NativeMods_MenuOffered(void);

// main.c, --dev-mods <default|random|selected[,driver...]>: the value for this
// run (never saved; the lines of the file do not count), and with drivers
// exactly these ticked - a number 0..14 is a retail driver, anything else a
// file name. 0 when the list is not of that form. --dev-mods-seed <n>: the
// draw starts from n instead of the clock.
int NativeMods_SetDev(const char *list);
void NativeMods_SetSeed(u32 seed);

// ctr-settings.cfg (platform/native_platform.c): SaveLines writes "mods cpu
// <n>", the play mods ("mods gravity", "grip", "night" <percent>, "mods items",
// "difficulty", "boss" <n>, "mods weight <item id> <level>" for an item not
// at NORMAL), one "mods retail <id>" per ticked retail driver and one "mods
// file <file>" per ticked file; LoadLine takes the rest of a line after "mods "
// and answers 1 when it named a mods value, else 0 (the caller counts it as
// skipped). A value out of its range is its normal value. Round 1's lines are
// carried over: "moon 1" is GRAVITY 41 %, "icy 1" GRIP 0 %, "chaos 1" ITEMS
// CHAOS, 0 the normal value. The lines of the first MODS page ("cpuchars",
// "custom", "off") are read too and carried over once the roster is known
// (NativeMods_AfterRoster). A file without these lines keeps the defaults.
#include <stdio.h>
void NativeMods_SaveLines(FILE *file);
int NativeMods_LoadLine(const char *rest);

// NativeChar_LoadRoster, at its end: the settings of the first MODS page, if
// the file held only those, become the value and the ticks of this one.
void NativeMods_AfterRoster(void);

// game/LOAD/LOAD_Assets.c, LOAD_DriverMPK, right after LOAD_Robots1P of a
// one-player arcade race (load stage 4, before the pack is queued): the seats
// 1..7 are drawn and written into data.characterIDs; a retail driver the pack
// of this load does not hold (it holds the player's and LOAD_Robots1P's) is
// read from the bigfile into host memory (BI_RACERMODELHI, as the pack's
// models are _hi), and so is the template of a custom seat, to check the file
// against it. With BOSS the boss alone takes seat 1 (his model read the same
// way), and the race gets one bot. Nothing at DEFAULT, nothing for any other
// load. Each call takes the next set of --dev-mods-play.
void NativeMods_PlanSeats(struct BigHeader *bigfile);

// game/Vehicle/VehBirth.c (after the driver pack) and the donor search of the
// custom characters: a retail model this load read for the mods, by name;
// NULL when there is none.
struct Model *NativeMods_ModelByName(const char *name);

// load stage 5 (NativeChar_ArmSeats): the roster entry the plan gave seat s
// of this load, -1 for a retail seat or without a plan.
int NativeMods_SeatEntry(int seat);

// Load stage 5, a seat whose file could not be bound after all: it gets, with
// ONLY SELECTED, a ticked retail driver whose model the load holds, else a
// retail driver of the pack no seat shows, written into data.characterIDs and
// the plan. Returns that id, -1 without a plan.
int NativeMods_RetailFallback(int seat);

// The draw memory: 1 when the race being loaded (load stage 0) may put custom
// drivers on CPU seats (a value that can draw a file, and the race one the box
// is offered for); the reserve then takes every CPU seat at the largest file.
int NativeMods_CustomRace(void);

// A menu load onto the main menu (NativeChar_ArmSeats, step 1): the plan of a
// cup is let go, the next cup draws anew.
void NativeMods_ForgetPlan(void);

// 1 while retail models read for the mods are held, while a GRIP blend entry
// (host memory) may sit in a driver, or while a BOSS race runs on the boss
// table of this module (sdata->bossWeaponMeta points at it): quick states
// refuse, as for a bound custom seat.
int NativeMods_HoldsModels(void);

#endif
