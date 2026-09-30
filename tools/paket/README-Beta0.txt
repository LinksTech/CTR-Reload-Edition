CTR Reload Beta 0 - README for testers
======================================
Build: @BUILD@

CTR Reload runs Crash Team Racing natively on Windows and loads custom tracks.
Beta 0 is a closed test of two things: the game (CTR Reload Beta 0) and the
Alpha-Maker, the tool that turns your exported track into a track the game
can load. Play, build your tracks, race them, and tell us what breaks.
When everything is clean, Beta 1 goes public on GitHub.

You get three files: ctr_native.exe (the game), alphamaker.exe (the track
tool) and this README.txt. They contain no game data. You need your own
copy of Crash Team Racing (NTSC-U, SCUS-94426) as a disc image (.cue/.bin).
Nothing is downloaded.
CTR Reload is not affiliated with or endorsed by Activision or Naughty Dog.

Contents
  0. Quick start
  1. What you need
  2. First start of the game
  3. Custom tracks in the game
  4. The Alpha-Maker
  5. What a track needs for each mode
  6. What we want you to test
  7. Known issues
  8. Reporting a bug
  9. Files and folders
 10. License


0. QUICK START
--------------
1. Put ctr_native.exe and alphamaker.exe together into their own folder,
   for example C:\Games\CTR Reload. Always keep the two together, and
   always from the same delivery.
2. Start ctr_native.exe and drag your disc image onto the window (once).
3. Start alphamaker.exe from the same folder. On the "Test in game" page the
   game must show "Found: CTR Reload Beta 0 (@BUILD@)".
4. On the "Track" page pick your exported track folder, set "Output" to the
   game's "tracks" folder, press "Build container". Wait until the headline
   turns green: "Container built, preview written".
5. Start the game: ARCADE -> NITRO-PIT -> RACE (or CRYSTAL, or CTR).

Always check the version first. The Alpha-Maker shows it in its title bar,
the game writes it at the top of its log ("Version: CTR Reload Beta 0 (...)").
Both must show @BUILD@. When you get a new build, replace BOTH files. An
older game or Alpha-Maker from another folder behaves differently - most
"it is still broken" reports so far came from an old copy.


1. WHAT YOU NEED
----------------
- Windows 10 (version 1903 or newer) or Windows 11. There is no Linux or Mac
  version.
- A graphics card and driver with Vulkan 1.0. The game is a 32-bit program and
  uses the 32-bit Vulkan loader that the driver installs.
- A folder you can write to. Not "C:\Program Files".
- Your own Crash Team Racing disc image, NTSC-U.


2. FIRST START OF THE GAME
--------------------------
1. Start ctr_native.exe.
2. Drag your disc image (.cue or .bin) onto the window. The game unpacks the
   data into the "assets" folder next to it, about 520 MB. This happens once.
3. The game starts.

Keys: F11 or Alt+Enter switches between window and full screen. F12 saves a
screenshot. Everything else is in the game menus.

Graphics: OPTIONS -> GRAPHICS. Up/down picks a row, left/right changes it.
  DISPLAY MODE   Fullscreen / Windowed          (right away)
  ASPECT RATIO   Auto / 4:3 / 16:9 / 21:9       (from the next race on)
  RESOLUTION     Native / 1x ... 8x             (right away)
  ANTI-ALIASING  Off / 2x / 4x                  (right away)
The choices are saved in ctr-settings.cfg and are there after a restart.


3. CUSTOM TRACKS IN THE GAME
----------------------------
The game loads every .rldtrack file in its "tracks" folder at start. Custom
tracks are under ARCADE -> NITRO-PIT:

  RACE         single race on a custom track (with bots if it has nav paths)
  CUP          custom cups from tracks\cups.txt (made on the Cups page)
  TIME TRIAL   grey, "COMING IN BETA 2"
  CRYSTAL      crystal challenge: collect every crystal in 3:00
  CTR          CTR challenge: finish 1st and collect C, T and R

A row is white when at least one track in the folder offers that mode, and
grey with a reason otherwise (for example "NO CTR TRACKS"). A track that
offers several modes is listed under each of them.

In the track list the window on the left plays the track's preview. Without a
preview it shows NO PREVIEW; that is not an error.

Crystal and CTR challenges end with RETRY or back to NITRO-PIT. Nothing is
written to your adventure save.

Custom tracks start without the camera fly-in before the countdown, even if
the track has its own camera path. The original tracks keep their fly-in.


4. THE ALPHA-MAKER
------------------
alphamaker.exe builds a track container (.rldtrack) from your exported track,
makes cups, and starts the game on your track. Keep it in the same folder as
ctr_native.exe: it finds the game and its data there, and your music needs
the game's sound data.

Colours in all messages: green = done, amber = works, but read this,
red = stopped.

4.1 Page "Track" - build a track container
  Track       The folder with your export: .lev (geometry), .vrm (textures),
              optional .sca or .sndb (music), optional track.txt (settings).
              The rows below show what was found, including "Minimap" and
              "Bot data" (nav paths - bots and auto drive need them).
  Name, Author, Version
              Shown in the game's track list.
  Modes       One box per mode. A mode your track has the data for is ticked
              and says "Playable". A mode without data is grey and says what
              is missing. Time Trial and Battle say "Coming soon in Beta 2".
  Sound       "Use the music in the folder", and under "Advanced": reverb,
              how fast the bots drive (like an original track), and the
              background sound.
  Output      Where the .rldtrack is written. Best: the game's "tracks"
              folder, then the game finds it right away.
  Check       Checks everything without writing (also runs by itself after
              every change).
  Build container
              Builds the .rldtrack, then records the preview by itself in the
              background: about 20 seconds, no game window. The AI drives
              your track as an invisible kart, filmed from the driver's view.
              The preview goes into a "vorschau" folder next to the
              container (tracks\vorschau\<name>.rldprev).
              - "Container built, preview written" (green): all done.
              - "... preview skipped" (amber): the container is built, but no
                preview; the reason is shown (usually the game on the "Test
                in game" page is missing or from another package).
              - "... preview failed" (amber): the container is built; the
                recording went wrong. The reason and the game log are shown.
              A preview game that has not finished after 2 minutes is
              stopped. Without nav paths a camera flies along the restart
              points instead of the AI driving. A track with only Crystal
              Challenge gets no preview (the preview is recorded in a race).
  Test in game
              Opens the "Test in game" page with this container.
  Show in folder, Show rldpack output
              Open the folder of the container; show the full checker log.

Build again whenever you change the track. That also records a new preview.
An old preview that does not match the track is ignored (NO PREVIEW).

The file name is the track's identity. Renaming the file makes it a new track
for the game (and for cups).

4.2 Page "Cups" - custom cups
  Tracks folder   The folder with your containers (default: the game's).
  Up to 4 cups with 4 tracks each: "New cup", "Add selected container",
  "Move up/down", "Remove", "Delete". "Save cups.txt" writes tracks\cups.txt;
  the game reads it at its next start (ARCADE -> NITRO-PIT -> CUP).
  The check list warns about missing containers, tracks without Race, tracks
  without nav paths ("there are no bots on this track"), names longer than 12
  characters and characters the menu font does not have. The game leaves out
  a cup with problems.

4.3 Page "Test in game" - start the game on your track
  Game program    ctr_native.exe. It is checked with its version; it must be
                  from the same package as the Alpha-Maker, otherwise:
                  "This game (...) is not from the same package ...". If the
                  game remembered from an earlier session is from another
                  package, the Alpha-Maker switches once to ctr_native.exe
                  in its own folder and says "Switched from ...".
  Container       The track to test.
  Options         "Play in a window (1280 x 720)", "Driver", "Let the kart
                  drive itself (autopilot)" (needs nav paths). "Extra
                  arguments" is for developers - leave it empty.
  Start game      Starts a race on the track straight away (7 bots if the
                  track has nav paths, otherwise you drive alone). After the
                  game closes, the page sums up the log: track loaded, race
                  started, or why not. "Show game log" opens the whole log.


5. WHAT A TRACK NEEDS FOR EACH MODE
-----------------------------------
  Race              restart points (the checkpoints that count the laps)
  Crystal Challenge at least one crystal on the track
  CTR Challenge     each of the letters C, T and R exactly once
  Time Trial        Beta 2 (greyed out)
  Battle            Beta 2 (greyed out)

Also good to know:
- Bots need nav paths. Without them you race alone, auto drive is off, and
  1st place in a CTR challenge is easy.
- At most 110 placed objects (crates, fruit, letters and every other placed
  model). Above 90 the Alpha-Maker warns: explosions and weapons may run
  short of room.
- "Build container" fixes the ids of the letter models c, t and r by their
  names, so the HUD shows C T R in that order, and says so.


6. WHAT WE WANT YOU TO TEST
---------------------------
Please try these and report what does not match. Test both: the
Alpha-Maker and the game.

Alpha-Maker
[ ] The title bar and the game log both show @BUILD@.
[ ] Pick your export folder: every file is found, "Minimap" and "Bot data"
    are right for your track.
[ ] Every message is clear to you: you know what it means and what to do.
    Tell us every message you did not understand.
[ ] The modes your track has data for are ticked; the others are grey with
    a reason that is true for your track.
[ ] Build container ends green with "preview written" and no game window
    pops up. Change something and build again - the preview is new.
[ ] Break things on purpose: a folder without .lev, a second .lev, a
    missing .vrm, a renamed file, a very long name, odd characters in the
    folder name. The Alpha-Maker must say what is wrong, never crash.
[ ] "Test in game" -> "Start game" loads your track; bots drive if it has
    nav paths; no camera fly-in before the countdown.
[ ] Cups page: make a cup with 4 of your tracks, save it, play it in
    NITRO-PIT -> CUP. Try a cup with problems and read the check list.
[ ] Dark mode / light mode (bottom left), resizing the window: everything
    stays readable.

In the game
[ ] NITRO-PIT -> RACE: your track is listed, the preview plays, the race
    runs to the finish.
[ ] NITRO-PIT -> CRYSTAL (if your track has crystals): collect all, win;
    let the time run out, lose; RETRY works.
[ ] NITRO-PIT -> CTR (if your track has C, T, R): drive through C, T and R
    yourself and finish 1st -> YOU WIN. Miss a letter -> TRY AGAIN.
[ ] Pause -> QUIT in a challenge brings you back to the menu.
[ ] Anything that looks wrong on your track: holes, black floors, missing
    objects, sound.


7. KNOWN ISSUES
---------------
Modes and menus
- Custom tracks play Race, Crystal Challenge and CTR Challenge. Time Trial
  and Battle come in Beta 2. Best times on custom tracks are not saved.
- Multiplayer (VS., BATTLE, 2 players) is locked.
- A track that offers both Race and Crystal Challenge is not tested yet: no
  test track has both restart points and crystals.
- Crystal Challenge has a fixed time limit of 3:00 for every track for now.
- CTR Challenge: letters that the exporter wrote without a collision hitbox
  cannot be collected, so the challenge cannot be won. The Alpha-Maker does
  not check this yet.
- No bots and no auto drive on tracks without nav paths. In a CTR Challenge
  1st place is then easy.
- With exactly two tracks in the folder, the track wheel can show two names
  on top of each other after you scroll it.

Previews
- The invisible kart still hits crates and fruit, so they break in the
  preview. If it falls off, the mask carries it back.
- Without nav paths the camera flies along the restart points; it does not
  avoid walls and ceilings and may pass through a tunnel roof.
- A track with only Crystal Challenge gets no preview.

Tracks and sound
- Very large tracks: flickering holes near the camera are possible when a
  track has very coarse BSP leaves. The log then contains "rendered list
  full" - please report it with the track.
- Tracks exported with Saphi: a hit may be missed where hitboxes are
  duplicated, and some floors may be black. Always build with "Build
  container"; it fixes wrong model ids.
- Music banks above 512 KB load, but their playback is not fully tested yet.
  The ambient sound set under "Advanced" is not tested either.

Programs
- The Alpha-Maker starts the game in developer mode for "Start game" and for
  the preview recording. That is intended.
- The programs are not signed. Some virus scanners may warn about them or
  move them to quarantine; then allow them in your scanner.
- Retail cheat codes that unlock drivers are not saved to the memory card;
  all drivers are available from the start anyway.
- Some messages on the command line (--help) are in German.


8. REPORTING A BUG
------------------
Post in the CTR Reload Beta 0 channel on Discord (the link is in your
invitation). One post per problem, with this template:

  **Version:** CTR Reload Beta 0 (@BUILD@)
  **Logs:** (attach the files, see below)
  **What happened:**
  **What you expected:**
  **Container:** file name, and the SHA-256 the Alpha-Maker shows
  **Steps to reproduce:**
  1.
  2.
  **Windows version, graphics card, driver:**

Which files to attach:
- The game log: the newest file in the "logs" folder next to the game,
  named "Crash Team Racing <date> <time>.log". The game keeps the last 5.
  If the game crashed, the log ends with lines starting with [CTR Crash].
- If you used "Start game" or "Build container" (the preview): the newest
  "game-test <date>.log" or "game-preview <date>.log" in
  %TEMP%\CTR Reload Alpha-Maker (paste that path into the Explorer address
  bar).
- Your .rldtrack container, if you are fine with sharing it. Your source
  files (.lev/.vrm/.sca) only if you want to.
- A screenshot (F12 in the game) if it is about the picture; a screenshot of
  the Alpha-Maker if it is about a message there.
- If the error shows before the game window (the disc image screen), a
  screenshot of that screen - nothing is logged at that point.

Please do NOT attach: the "assets" folder, disc images, memory cards
("memcards" folder), or anything else from the game data. Logs contain folder
paths with your Windows user name - edit them out if you mind.


9. FILES AND FOLDERS
--------------------
  ctr_native.exe      the game
  alphamaker.exe      the Alpha-Maker (track containers, cups, test); the
                      track checker/packer is built into it
  README.txt          this file

Created by the game next to ctr_native.exe:
  assets\             game data from your disc image (do not share)
  tracks\             your .rldtrack files; tracks\vorschau\ the previews;
                      tracks\cups.txt your cups; tracks\track-ids.tsv is
                      written by the game - leave it alone
  logs\               the last 5 game logs
  memcards\           memory card (your saves)
  ctr-settings.cfg    your settings

The Alpha-Maker remembers its settings in %APPDATA%\CTR Reload\alphamaker.ini.
To update, replace ctr_native.exe and alphamaker.exe; keep the rest.


10. LICENSE
-----------
CTR Reload is free software under the GNU General Public License version 3
(https://www.gnu.org/licenses/gpl-3.0.html). Beta 0 is a closed test.
Beta 1 will be published on GitHub with its source code and automatic
builds. Until then you can ask for the source code of exactly this build
(@BUILD@) in the Beta 0 channel on Discord.
