# Nightly testers

Thanks for testing the nightly build. This page says what to look at and
how to report what you find.

## What this nightly is

- CTR Reload **0.7.5 Beta**, built automatically from `main` and not
  tested: it may be broken. Both programs show the version as
  `0.7.5 Beta (<commit>)`, where `<commit>` is the 12-character build ID of
  the `main` commit (the nightly release title shows the same ID).
- Feature freeze: from this nightly until the release, `main` only gets
  fixes, no new features.
- Put `ctr_native.exe` and `ReloadStudio.exe` into one folder you can
  write to, and replace both files with every new nightly. The game asks
  for your own disc image on the first start.

## What to test

1. **NATIVE DRIVERS preview.** OPTIONS -> GRAPHICS -> NATIVE DRIVERS, set
   it to PREVIEW and restart the game (until then the page says
   "NATIVE DRIVERS: AFTER RESTART"). Custom characters built with a
   high-detail model are then drawn with it: in the race, in water and
   mud, in the mirror on ice and in the driver select. Set it back to OFF
   and restart: everything must look as before. If the game does not start
   after choosing PREVIEW, delete the line `video nativedrivers 1` from
   `ctr-settings.cfg` next to the game.
2. **Resolution 2X and above.** The high-detail model is drawn from
   RESOLUTION 2X; at 1X the game draws the classic model. Try 2X, 4X and
   higher, and the switch between 1X and 2X.
3. **Custom drivers with wheels of their own.** In Reload Studio, page
   Character, tab Extras: tick "Native model" (card Import) and give the
   card Wheels a wheel model - an OBJ (with its MTL file and one texture of
   up to 1024 x 1024) or a PLY, up to 1024 triangles - and a size (50 to
   200 %). Build the character, put it into the `characters` folder and
   restart the game. Check that the wheels sit on the ground, the front
   wheels steer, all four roll, and the right side is the mirror image.
   With NATIVE DRIVERS off or at 1X the character must drive its classic
   model with the game's own wheels.
4. **Reload Studio preview.** On the page Character, turn, move and zoom
   the model with the mouse, try the fixed views (Front, Side, Back, Top,
   3/4, Race) and the View menu (right click): background, Crash size,
   Shadow, Exhaust and, for a character with a high-detail model, Native
   or Classic. Compare the Native look and the Race view with the game
   (NATIVE DRIVERS PREVIEW, 2X or higher). Check that the wheels of their
   own spin and steer in place and that missing texture files are named
   below the preview.

## Known issues

No need to report these:

- Custom drivers without their own voice lines are silent.
- Stroboscope effect: at top speed the tread of custom wheels can look
  still or as if it turns backwards.
- NATIVE DRIVERS applies at the next start, not right away.
- The high-detail model is drawn only from RESOLUTION 2X; at 1X the
  classic model is drawn.
- A character that shows the kart wheels and has no wheel model gets the
  classic model only.
- A semi-transparent (blend) material makes the game draw the classic
  model for that driver.
- The high-detail model comes only from an OBJ; a PLY body gets the
  classic model only (Reload Studio says so).
- Wheels of their own are always drawn, also where the game leaves its own
  wheels out (lower level of detail); in the mirror and under water they
  are darkened like the body.
- A wheel size other than 100 % shows only on the high-detail model.
- The wheel points do not follow the size of the driver.
- Reload Studio's preview shows no animations (only the poses Neutral,
  Steering left and Steering right); the card Animations stays
  "Coming soon". Its shadow is a soft rectangle.
- Exhaust points belong to the model as it was fitted; after a change of
  size or fit, set them again. The fitted shadow may cover a little more
  or less than the model.
- The format of the high-detail model is a preview and may change; a
  later version may ask you to build such a character again.

## How to report

Open an issue on GitHub with the "Bug report" form:
https://github.com/LinksTech/CTR-Reload-Edition/issues/new/choose

- Say that it is the nightly, and give the version line. The game writes
  it at the top of its log, `Version: CTR Reload 0.7.5 Beta (<commit>)`;
  `ctr_native.exe --version` prints it as well.
- Attach the game log: the newest file in the `logs\` folder next to the
  game, named `Crash Team Racing <date> <time>.log`.
- Say what you did, what happened and what you expected; for a custom
  character, its file name and the SHA-256 Reload Studio shows. A
  screenshot (F12 in the game) helps for anything you see.
- Do not attach game files, disc images, memory cards or character files.
  Logs contain folder paths with your Windows user name - edit them out if
  you mind.
