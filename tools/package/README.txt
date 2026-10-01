CTR Reload @VERSION@ - README for testers
==========================================
Build: @BUILD@

CTR Reload runs Crash Team Racing natively on Windows and loads custom tracks
and custom characters. This version is a test of two things: the game (CTR
Reload @VERSION@) and Reload Studio, the tool that turns your exported track
or character model into a file the game can load. Play, build your tracks and
characters, race them, and tell us what breaks.

The package holds the game (ctr_native.exe, with its debug symbols
ctr_native.pdb), Reload Studio (ReloadStudio.exe), this README.txt, the
RELEASE-NOTES.txt, the licenses (LICENSE, THIRD_PARTY_NOTICES.md) and the
source code of exactly this build:
  @NAME@-source.zip
It contains no game data. You need your own copy of Crash Team Racing
(NTSC-U, SCUS-94426) as a disc image (.cue/.bin). Nothing is downloaded.
CTR Reload is not affiliated with or endorsed by Activision or Naughty Dog.

Contents
  0. Quick start
  1. What you need
  2. First start of the game
  3. Custom tracks and characters in the game
  4. Reload Studio
  5. What a track needs for each mode
  6. What we want you to test
  7. Known issues
  8. Reporting a bug
  9. Files and folders
 10. License


0. QUICK START
--------------
1. Unpack the package folder to a place of its own, for example
   C:\Games\CTR Reload. Always keep ctr_native.exe, ctr_native.pdb and
   ReloadStudio.exe together, and always from the same package.
2. Start ctr_native.exe and drag your disc image onto the window (once).
3. Start ReloadStudio.exe from the same folder. On the "Test in game" page the
   game must show "Found: CTR Reload @VERSION@ (@BUILD@)".
4. On the "Track" page pick your exported track folder, set "Output" to the
   game's "tracks" folder, press "Build container". Wait until the headline
   turns green: "Container built, preview written".
5. Start the game: ARCADE -> NITRO-PIT -> RACE (or CRYSTAL, or CTR).

Always check the version first. Reload Studio shows it in its title bar,
the game writes it at the top of its log ("Version: CTR Reload @VERSION@ (...)").
Both must show @BUILD@. When you get a new build, replace BOTH files. An
older game or Reload Studio from another folder behaves differently - most
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


3. CUSTOM TRACKS AND CHARACTERS IN THE GAME
-------------------------------------------
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

Custom characters: the game reads every .rldchar file in its "characters"
folder at start (make them on Reload Studio's "Character" page, 4.3). Each
gets a tile in the one-player ARCADE driver select, after the original
drivers; the tiles are sorted by file name, at most 32 get one. Not in
NITRO-PIT CRYSTAL or CTR, not in Time Trial, Adventure, Battle or with two
players.
- A broken file is skipped and the game starts anyway; the log says why (a
  line starting with "[CTR Char] REFUSED").
- The driving style chosen in Reload Studio decides how the kart drives
  (speed, acceleration, turning) and its engine sound.
- A character built with "Show kart wheels" off (4.3) drives without the
  game's kart wheels - its model brings its own. Tyre dust and skid marks
  stay. The log line "[CTR Char] loaded ..." then ends in ", wheels hidden".
- A character built with an icon (4.3) shows it on its tile in the driver
  select, for the first 20 files the game loads (sorted by file name; a
  refused file does not count). Without an icon, with a broken one and from
  the 21st loaded file on, the tile shows the portrait of Fake Crash; a log
  line starting with "[CTR Char] portrait" says which. The race HUD, the
  results and the cup standings still show the portrait of Fake Crash.
- For now a custom driver is silent.


4. RELOAD STUDIO
----------------
ReloadStudio.exe builds a track container (.rldtrack) from your exported track,
makes cups, builds characters (.rldchar) from a 3D model, and starts the game
on your track. Keep it in the same folder as ctr_native.exe: it finds the
game and its data there, and your music needs the game's sound data.

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

4.3 Page "Character" - build a character
  Model (PLY) Your model of driver, steering wheel and kart in one PLY file
              with vertex colours (ASCII or binary), +Y up, +Z forward, at
              any scale: Reload Studio fits it to Crash size (see Size). The
              kart is the part at the bottom; the game draws the wheels (see
              "Show kart wheels"). Other parts of a vehicle that lie across
              the middle of the kart (a body, a fork, a seat) stay still with
              it; parts to one side (shoes, hands) move with the driver.
              Faces with more than four corners are split into triangles.
              Small faults of the export are repaired (see "Repair the
              model"), and a model with too many triangles is reduced by
              itself (see "Reduce automatically"). You can also
              drag files onto the page: a .ply is the model, a .png the
              icon, a folder the voices.
  Name        Shown in the driver select: 1 to 17 characters, capitals,
              A-Z 0-9 space ! % ' + , - . / : < = > ? _ (anything else is
              left out while you type).
  Driving style
              Balanced, Acceleration, Speed or Turning, each with the
              original drivers that drive like it (default Balanced). This
              decides how the kart drives in the game.
  Size        50 to 200 % (default 100). 100 % = Crash size: every model is
              first fitted to the size of Crash with his kart. Its kart gets
              the length of Crash's kart (112.4 game units); with "Show kart
              wheels" off the whole model gets that length. It never gets
              taller than the tallest original driver. Then it is placed
              onto Crash's kart: your kart at the height and in the middle of
              his (with "Show kart wheels" off the whole model stands on the
              ground). The page shows the factor, for example "Fitted to
              Crash size: x0.71 (159.2 -> 112.4 long)".
              The bar makes the driver and the steering wheel larger or
              smaller than that, about Crash's seat (with "Show kart wheels"
              off: about the bottom of your driver); the kart keeps its size.
              When steering, the driver leans about the same point. Visual
              size only - physics and collision follow the driving style.
              After the first check the bar shows the sizes your model
              allows and the rest of it is locked; the line below says the
              range, and why when the bar had to move back.
  Repair the model
              On by default. Fixes what exports often get wrong: split
              corners are welded, triangles without area and doubled ones
              are dropped, faces are turned outward, cracks are split and
              holes of up to 8 edges are closed. The line below the options
              says what changed, for example "Repaired: 12 corners welded,
              2 holes closed, open edges 14 -> 0". Off: the surface as
              exported.
  Draw open parts from both sides
              On by default. A part that is still open after the repair (a
              kart shell without a floor, say) would let you look into it
              from behind; its triangles are drawn from both sides instead,
              at no extra triangles. Off: drawn from one side like the rest.
  Closed hull (remesh)
              Off by default; only for a model full of holes or loose sheets
              that the repair cannot fix. Kart, driver and steering wheel are
              each replaced by a closed hull of their surface (a part of at
              most 24 triangles stays as it is), which is then reduced; the
              colours are taken over from your model. The preview shows the
              result. Needs "Reduce automatically": greyed out while that
              is off.
  Reduce automatically
              On by default. The game has draw memory for 805 triangles per
              driver (as much as N. Oxide, the most any original driver
              uses). A model with more is reduced to at most 797 triangles,
              only as far as needed: the shape and the colours stay, kart,
              driver and steering wheel are reduced each on its own, and the
              same model always gives the same result. Colours always stay
              in their place and no face ever turns to the back. Colour
              borders and sharp edges are guarded as well; if the target
              cannot be reached that way, the reduction first drops these
              guards and then, if needed, its colour cost, and says so.
              The page then shows both counts, for example "Triangles 987 ->
              797, draw memory 27 636 -> 22 316 bytes - reduced
              automatically". Off: such a model is refused; reduce it
              yourself in Blender (Decimate modifier).
  Show kart wheels
              On by default: the game draws its kart wheels on your kart, as
              for every driver. Turn it off for a model that brings its own
              wheels or its own vehicle: the game then draws no kart wheels
              (and no wheel reflections) for this driver; tyre dust and skid
              marks stay. The choice is stored in the .rldchar. An older
              game does not know it and draws the wheels.
  Icon (PNG)  Optional. Your picture and how the game will have it (cut to
              44:26 in the middle, 44 x 26 pixels, 15 colours and
              transparent) side by side. The game shows it on the
              character's tile in the driver select (see 3.); without an
              icon the tile shows the portrait of Fake Crash.
  Voices      Optional. A folder with your voice lines as .wav or .vag:
              boost1, boost2, hit1, hit2, spin1, spin2, bigair1, bigair2,
              drop1, drop2, shield1, shield2, passing1, passing2, fire1,
              fire2, yes, hit. They are checked (format, length, level, and
              which places are empty), but not packed yet - the driver is
              silent in the game.
  Preview     The model as the game will draw it. Drag to turn it; the list
              at the top right of the card picks Neutral, Steering left or
              Steering right. Under your model stands the reference: an
              original kart at Crash's size with his seat, his steering wheel
              and the kart wheels the game draws (none with "Show kart
              wheels" off). Your model is fitted onto it and always drawn in
              front of it, so the grey reference shows only where your model
              leaves a gap. The dashed box is Crash size.
  Output      Where the .rldchar is written; empty = next to the model. Best:
              the game's "characters" folder, then the game finds it at its
              next start.
  Check       Checks everything without writing (also runs by itself 0.6
              seconds after every change). The messages say what to fix.
  Build character
              Writes the .rldchar (asks before it replaces a file) and shows
              its SHA-256. "Show in folder" opens the folder.
  Show rldpack output
              The full checker log.

The file name is the character's identity in the game; the name in the menu
lives inside the file.

4.4 Page "Test in game" - start the game on your track
  Game program    ctr_native.exe. Reload Studio takes the one you chose here
                  with Browse (it remembers the choice), otherwise the one in
                  its own folder; it searches no other folder. If neither is
                  there, every page that needs the game says so. The game is
                  checked with its version; it must be from the same package
                  as Reload Studio, otherwise: "This game (...) is not from
                  the same package ...". If the game remembered from an
                  earlier session is from another package, Reload Studio
                  switches once to ctr_native.exe in its own folder and says
                  "Switched from ...".
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
  Time Trial        not available yet (greyed out)
  Battle            not available yet (greyed out)

Also good to know:
- Bots need nav paths. Without them you race alone, auto drive is off, and
  1st place in a CTR challenge is easy.
- At most 110 placed objects (crates, fruit, letters and every other placed
  model). Above 90 Reload Studio warns: explosions and weapons may run
  short of room.
- "Build container" fixes the ids of the letter models c, t and r by their
  names, so the HUD shows C T R in that order, and says so.


6. WHAT WE WANT YOU TO TEST
---------------------------
Please try these and report what does not match. Test both:
Reload Studio and the game.

Reload Studio
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
    folder name. Reload Studio must say what is wrong, never crash.
[ ] "Test in game" -> "Start game" loads your track; bots drive if it has
    nav paths; no camera fly-in before the countdown.
[ ] Cups page: make a cup with 4 of your tracks, save it, play it in
    NITRO-PIT -> CUP. Try a cup with problems and read the check list.
[ ] Character page: build a character from your model, with and without
    an icon, at a few sizes. The preview matches what you see in the game.
[ ] Character page: a model exported at any scale comes out at Crash size
    (100 %) on the grey reference kart, inside the dashed box. A model of
    more than 805 triangles is "reduced automatically" and still looks like
    itself in the game: colours in their place, no holes, no faces missing.
[ ] Character page: a model with holes, split corners or open parts -
    "Repair the model" and "Draw open parts from both sides" on and off.
    Try "Closed hull (remesh)" on a model the repair cannot fix.
[ ] Character page: a model with its own wheels or vehicle, "Show kart
    wheels" off - the game draws no kart wheels for it.
[ ] Break the character on purpose: a model without kart, a very large
    model, a broken PNG, a voice file in a wrong format. Reload Studio must
    say what is wrong, never crash.
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
[ ] Your character in ARCADE: its tile is in the driver select, with its
    icon if you gave one; the race runs, the kart drives like the driving
    style you chose.
[ ] Anything that looks wrong on your track: holes, black floors, missing
    objects, sound.


7. KNOWN ISSUES
---------------
Modes and menus
- Custom tracks play Race, Crystal Challenge and CTR Challenge. Time Trial
  and Battle are planned. Best times on custom tracks are not saved.
- Multiplayer (VS., BATTLE, 2 players) is locked.
- A track that offers both Race and Crystal Challenge is not tested yet: no
  test track has both restart points and crystals.
- Crystal Challenge has a fixed time limit of 3:00 for every track for now.
- CTR Challenge: letters that the exporter wrote without a collision hitbox
  cannot be collected, so the challenge cannot be won. Reload Studio does
  not check this yet.
- No bots and no auto drive on tracks without nav paths. In a CTR Challenge
  1st place is then easy.
- With exactly two tracks in the folder, the track wheel can show two names
  on top of each other after you scroll it.

Characters
- A custom driver is silent: voices are checked, but not packed yet.
- The character's own icon shows only in the driver select, and only for
  the first 20 loaded files; the race HUD, the results and the cup
  standings show the portrait of Fake Crash.
- Only in the one-player ARCADE driver select; not in NITRO-PIT CRYSTAL or
  CTR, Time Trial, Adventure, Battle or with two players.
- The size is visual only - physics and collision follow the driving style.
- With "Show kart wheels" off, tyre dust and skid marks still show where the
  game's wheels would be.

Previews
- The invisible kart still hits crates and fruit, so they break in the
  preview. If it falls off, the mask carries it back.
- Without nav paths the camera flies along the restart points; it does not
  avoid walls and ceilings and may pass through a tunnel roof.
- A track with only Crystal Challenge gets no preview.

Tracks and sound
- Very large tracks: flickering holes near the camera are possible when a
  track has very coarse BSP leaves. The log then contains "rendered list
  full" - please report it, with the track's file name and SHA-256.
- Tracks exported with Saphi: a hit may be missed where hitboxes are
  duplicated, and some floors may be black. Always build with "Build
  container"; it fixes wrong model ids.
- Music banks above 512 KB load, but their playback is not fully tested yet.
  The ambient sound set under "Advanced" is not tested either.

Programs
- Reload Studio starts the game in developer mode for "Start game" and for
  the preview recording. That is intended.
- The programs are not signed. Some virus scanners may warn about them or
  move them to quarantine; then allow them in your scanner.
- Retail cheat codes that unlock drivers are not saved to the memory card;
  all drivers are available from the start anyway.


8. REPORTING A BUG
------------------
Open an issue on GitHub and choose "Bug report":

  https://github.com/LinksTech/CTR-Reload-Edition/issues/new/choose

One issue per problem (issues are public - see the end of this section for
what not to attach). The form asks for:
- the version: type cmd into the Explorer address bar of this folder, press
  Enter, run "ctr_native.exe --version" and paste the line it prints
  ("CTR Reload @VERSION@ (@BUILD@)")
- what happened, what you expected, and the steps to reproduce it
- for a custom track or character: its file name and the SHA-256 Reload
  Studio shows
- the log and screenshots (see below)
- your Windows version, graphics card and driver version

Which files to attach:
- The game log: the newest file in the "logs" folder next to the game,
  named "Crash Team Racing <date> <time>.log". The game keeps the last 5.
  If the game crashed, the log ends with lines starting with [CTR Crash].
- If you used "Start game" or "Build container" (the preview): the newest
  "game-test <date>.log" or "game-preview <date>.log" in
  %TEMP%\Reload Studio (paste that path into the Explorer address
  bar; with --settings, see 9., in the folder of that file).
- A screenshot (F12 in the game) if it is about the picture; a screenshot of
  Reload Studio if it is about a message there.
- If the error shows before the game window (the disc image screen), a
  screenshot of that screen - nothing is logged at that point.

Please do NOT attach: the "assets" folder, disc images, memory cards
("memcards" folder), track or character containers (.rldtrack, .rldchar) or
their source files (.lev/.vrm/.sca/.ply), or anything else from the game
data. Logs contain folder paths with your Windows user name - edit them out
if you mind.


9. FILES AND FOLDERS
--------------------
  ctr_native.exe      the game
  ctr_native.pdb      debug symbols of the game: with them a crash report
                      names the function, keep it next to ctr_native.exe
  ReloadStudio.exe    Reload Studio (track containers, cups, characters,
                      test); the checker/packer (rldpack) is built into it
  README.txt          this file
  RELEASE-NOTES.txt   what is new, decisions, known issues
  LICENSE             the GNU General Public License version 3
  THIRD_PARTY_NOTICES.md
                      licenses of the components the programs contain
  <package name>-source.zip
                      the source code of exactly this build

Created by the game next to ctr_native.exe:
  assets\             game data from your disc image (do not share)
  tracks\             your .rldtrack files; tracks\vorschau\ the previews;
                      tracks\cups.txt your cups; tracks\track-ids.tsv is
                      written by the game - leave it alone
  characters\         your .rldchar files
  logs\               the last 5 game logs
  memcards\           memory card (your saves)
  ctr-settings.cfg    your settings

Reload Studio remembers its settings in %APPDATA%\CTR Reload\reloadstudio.ini.
For automation and tests, "ReloadStudio.exe --settings <file.ini>" keeps
its settings in that file instead, and its logs and temporary files in the
folder of that file; nothing is then written to %APPDATA% or %TEMP%.
To update, replace ctr_native.exe, ctr_native.pdb and ReloadStudio.exe; keep
the rest.


10. LICENSE
-----------
CTR Reload is free software under the GNU General Public License version 3
(https://www.gnu.org/licenses/gpl-3.0.html). The source code of exactly
this build (@BUILD@) is in this package:
  @NAME@-source.zip
BUILDING.md in it says how to build it. The licenses of the third-party
components are in THIRD_PARTY_NOTICES.md.
