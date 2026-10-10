#ifndef NATIVE_MODS_H
#define NATIVE_MODS_H

// THE MODS OF THE ARCADE RACE (platform/native_mods.c): who sits on the CPU
// seats of a one-player arcade race - single race and cup alike. Chosen on
// the MODS page (game/native_mods_page.c), reached from the MODS box of the
// track select and the cup select; saved in ctr-settings.cfg like the
// GRAPHICS page. With both values at their default (DEFAULT, OFF) nothing
// here changes a load: the bots are the retail ones LOAD_Robots1P chose.
//
//   CPU CHARACTERS     DEFAULT  the fixed retail troupe of LOAD_Robots1P
//                      RANDOM   drawn from every retail driver (ids 0..14)
//   CPU CUSTOM DRIVERS OFF      no custom driver on a CPU seat
//                      RANDOM   every custom driver of the roster joins the pool
//                      SELECTED only the ones ticked on SELECT DRIVERS
//
// THE RULE: the pool is the retail part (the troupe, or every retail driver)
// plus the custom part; each of the seven CPU seats draws from it with the
// same chance, without putting anyone back - nobody twice, and never the
// player's driver (the retail id of seat 0, or the custom file it picked). A
// pool of fewer than seven is filled up with retail drivers. A new draw for
// every race load, except inside a cup: the first race of a cup draws, the
// next three take the same seats.

struct BigHeader;
struct Model;

#define NATIVE_MODS_CPU_DEFAULT 0
#define NATIVE_MODS_CPU_RANDOM  1

#define NATIVE_MODS_CUSTOM_OFF      0
#define NATIVE_MODS_CUSTOM_RANDOM   1
#define NATIVE_MODS_CUSTOM_SELECTED 2

// The two values. A setter clamps to the values above and saves at once
// (Platform_SettingsSave; nothing is written under --settings-defaults).
int NativeMods_CpuCharacters(void);
void NativeMods_SetCpuCharacters(int value);
int NativeMods_CustomDrivers(void);
void NativeMods_SetCustomDrivers(int value);

// SELECT DRIVERS: the files of the custom roster (NativeChar_RosterFileCount,
// never the placeholders of --dev-grid-fill), in roster order. Name: the CHRI
// name; "" outside. Selected: 1 for a ticked file - every file is ticked
// until it is unticked, a file put into the folder later as well. The setter
// saves at once. SelectedCount: the ticked files.
int NativeMods_DriverCount(void);
const char *NativeMods_DriverName(int index);
int NativeMods_DriverSelected(int index);
void NativeMods_SetDriverSelected(int index, int on);
int NativeMods_SelectedCount(void);

// The status line of the MODS box, at most 11 characters of FONT_SMALL
// (the box is as wide as LAPS, 164): "OFF", "CPU: RANDOM", "CUSTOM: ALL"
// (RANDOM), "CUSTOM: <n>" (SELECTED, n ticked). With custom drivers on, the
// custom state is the one shown.
const char *NativeMods_StatusText(void);

// The menu side of the race rule: the MODS box is offered in the track select
// and the cup select of a one-player arcade race on the disc tracks - not in
// NITRO-PIT, adventure, time trial, battle or with two or more players.
int NativeMods_MenuOffered(void);

// main.c, --dev-mods <cpu,custom[,file...]>: the two values for this run
// (never saved; the lines of the file do not count), and with file names
// exactly these ticked for SELECTED. 0 when the list is not of that form.
// --dev-mods-seed <n>: the draw starts from n instead of the clock.
int NativeMods_SetDev(const char *list);
void NativeMods_SetSeed(u32 seed);

// ctr-settings.cfg (platform/native_platform.c): SaveLines writes "mods
// cpuchars <n>", "mods custom <n>" and one "mods off <file>" per unticked
// file; LoadLine takes the rest of a line after "mods " and answers 1 when it
// named a mods value, else 0 (the caller counts it as skipped). A file
// without these lines keeps the defaults.
#include <stdio.h>
void NativeMods_SaveLines(FILE *file);
int NativeMods_LoadLine(const char *rest);

// game/LOAD/LOAD_Assets.c, LOAD_DriverMPK, right after LOAD_Robots1P of a
// one-player arcade race (load stage 4, before the pack is queued): the seats
// 1..7 are drawn and written into data.characterIDs; a retail driver the pack
// of this load does not hold (it holds the player's and LOAD_Robots1P's) is
// read from the bigfile into host memory (BI_RACERMODELHI, as the pack's
// models are _hi). Nothing at the defaults, nothing for any other load.
void NativeMods_PlanSeats(struct BigHeader *bigfile);

// game/Vehicle/VehBirth.c (after the driver pack) and the donor search of the
// custom characters: a retail model this load read for the mods, by name;
// NULL when there is none.
struct Model *NativeMods_ModelByName(const char *name);

// load stage 5 (NativeChar_ArmSeats): the roster entry the plan gave seat s
// of this load, -1 for a retail seat or without a plan.
int NativeMods_SeatEntry(int seat);

// Load stage 5, a seat whose file could not be bound (no donor, other frame
// counts): it gets a retail driver of the pack no seat shows, written into
// data.characterIDs and the plan. Returns that id, -1 without a plan.
int NativeMods_RetailFallback(int seat);

// The draw memory: 1 when the race being loaded (load stage 0) may put custom
// drivers on CPU seats (custom drivers on and the race one the box is offered
// for); the reserve then takes every CPU seat at the largest file of the pool.
int NativeMods_CustomRace(void);

// A menu load onto the main menu (NativeChar_ArmSeats, step 1): the plan of a
// cup is let go, the next cup draws anew.
void NativeMods_ForgetPlan(void);

// 1 while retail models read for the mods are held (quick states refuse,
// as for a bound custom seat).
int NativeMods_HoldsModels(void);

#endif
