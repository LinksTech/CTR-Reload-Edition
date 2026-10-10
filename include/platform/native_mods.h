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
// ticked - that is DEFAULT), "ALL RANDOM", "<n> SELECTED".
const char *NativeMods_StatusText(void);

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
// <n>", one "mods retail <id>" per ticked retail driver and one "mods file
// <file>" per ticked file; LoadLine takes the rest of a line after "mods " and
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
