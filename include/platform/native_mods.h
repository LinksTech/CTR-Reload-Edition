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
// ticked - that is DEFAULT), "ALL RANDOM", "<n> SELECTED". With play mods on
// (below): "<n> MOD(S)" beside DEFAULT, else the CPU value short and the count -
// "RANDOM +<n>", "<k> SEL +<n>" (11 characters at most).
const char *NativeMods_StatusText(void);

// THE PLAY MODS, each OFF or ON, freely combined, for the same races as CPU
// DRIVERS (the rule of NativeMods_MenuOffered at the menu, of the race below):
//   MOON GRAVITY  the karts fall at 41/100 of their gravity - the game's own
//                 low gravity (QUADBLOCK_FLAG_LOW_GRAVITY, BOT_FLAG_MOON_GRAVITY);
//                 the hang-time turbo is judged by the air time times 41/100,
//                 so a jump earns what it earns at full gravity
//   ICY TRACKS    road terrain (full speed and full grip: asphalt, wood, stone,
//                 hardpack, metal and the three asphalts of the water tracks)
//                 is ice, as Polar Pass's; water, mud, grass, dirt, snow, the
//                 slow and the sideslip terrain keep theirs, and the turbo pads
//                 and ramps are not terrain at all
//   ITEM CHAOS    an item box gives every item of the four race item sets with
//                 the same chance, whatever the rank; a CPU driver, which never
//                 takes a box, attacks with its four weapons (TNT, potion, bomb,
//                 missile) with the same chance, whatever the side
// All OFF changes nothing: no other path, no other random number.
#define NATIVE_MODS_PLAY_MOON  0x1
#define NATIVE_MODS_PLAY_ICY   0x2
#define NATIVE_MODS_PLAY_CHAOS 0x4

// The page's value of one mod (bit above), and the setter (saves at once).
int NativeMods_PlayOn(int bit);
void NativeMods_SetPlayOn(int bit, int on);

// 1 while the race in progress is one the mods are for and the mod is ON.
int NativeMods_PlayActive(int bit);

// ICY TRACKS: the terrain the game takes for a ground of this terrain type -
// TERRAIN_ICE for road terrain while the mod is active, else the type itself;
// bot 1 for a CPU driver's nav frame, 0 for a kart's ground (the count at exit).
u8 NativeMods_IcyTerrain(u8 terrainType, int bot);

// MOON GRAVITY: the air time the hang-time turbo is judged by, at landing
// (the meter in ms; unchanged unless the mod is active). The meter stops at
// 2400 ms; MoonAir, while airborne, keeps a driver's air time past it, and
// the hang time is that times 41/100. given and full are the fire steps the
// turbo got and would have got from the meter (-1 = none), for the report at
// exit.
void NativeMods_MoonAir(int driverID, int airTime);
int NativeMods_MoonHangTime(int driverID, int jumpMeter);
void NativeMods_NoteLanding(int given, int full);

// MOON GRAVITY and ICY TRACKS, for the report at exit: a CPU driver put back on
// its path after a kill plane (BOTS_Killplane), counted while a play mod is
// active.
void NativeMods_NoteBotRescue(void);

// ITEM CHAOS, for the report at exit: an item a box gave (bot 0) or a CPU
// driver's attack (bot 1).
void NativeMods_NoteItem(int bot, int item);

// game/DebugMenu.c, beside the [CTR Race] line: one line with the play mods of
// this race, only when one is active.
void NativeMods_LogRace(void);

// main.c, --dev-mods-play <none|moon,icy,chaos>: the play mods of this run,
// never saved (the lines of the file do not count). 0 for another form.
int NativeMods_SetDevPlay(const char *list);

// main.c, --mods-selftest (ctest mods_selftest): the play mods without a race -
// settings lines, status line, --dev-mods-play, ice terrain, hang time. 0 = passed.
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
// <n>", "mods moon", "mods icy" and "mods chaos" <0|1>, one "mods retail <id>"
// per ticked retail driver and one "mods file <file>" per ticked file; LoadLine takes the rest of a line after "mods " and
// answers 1 when it named a mods value, else 0 (the caller counts it as
// skipped). The lines of the first MODS page ("cpuchars", "custom", "off") are
// read too and carried over once the roster is known (NativeMods_AfterRoster).
// A file without these lines keeps the defaults.
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
// against it. Nothing at DEFAULT, nothing for any other load.
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

// 1 while retail models read for the mods are held (quick states refuse,
// as for a bound custom seat).
int NativeMods_HoldsModels(void);

#endif
