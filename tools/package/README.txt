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
  NATIVE DRIVERS Off / Preview                  (from the next start)
The choices are saved in ctr-settings.cfg and are there after a restart.

NATIVE DRIVERS is a preview and Off by default. Preview draws a custom
character that was built with a native model (Reload Studio, tab Extras,
card Import, "Native model") with that model and its textures, and with its
own wheels when it has them (card Wheels). It does so at a RESOLUTION of 2x
or more (Native counts when the window is at least twice the game's size);
at 1x, and for every other driver, the game draws as before. The pipelines
a race needs for these drivers are built while the race loads, so the first
frame of the race does not stall. The choice applies at the next start;
until then the page shows "NATIVE DRIVERS: AFTER RESTART". To turn it off,
set it to Off on the page and restart. If the game does not start after
choosing Preview, delete the line "video nativedrivers 1" from
ctr-settings.cfg (next to ctr_native.exe).


3. CUSTOM TRACKS AND CHARACTERS IN THE GAME
-------------------------------------------
The game loads every .rldtrack file in its "tracks" folder at start. Custom
tracks are under ARCADE -> NITRO-PIT:

  RACE         single race on a custom track (with bots if it has nav paths);
               below the laps the MODE box: RACE or TIME TRIAL
  CUP          custom cups from tracks\cups.txt (made on the Cups page)
  TIME TRIAL   grey, "COMING SOON" - the time trial is under RACE, MODE
  CRYSTAL      crystal challenge: collect every crystal in 3:00
  CTR          CTR challenge: finish 1st and collect C, T and R

A row is white when at least one track in the folder offers that mode, and
grey with a reason otherwise (for example "NO CTR TRACKS"). A track that
offers several modes is listed under each of them.

In the track list the window on the left plays the track's preview. Without a
preview it shows NO PREVIEW; that is not an error.

Time trial: in the track list press X, then DOWN past the last lap row into
the MODE box, choose TIME TRIAL with X, pick the laps and start with X. You
drive alone, without crates or fruit; the HUD lists the lap times and your
best time for this track and lap count. The best times are kept in
nitro-pit-times.tsv next to the game, one line per track and lap count with
driver and date - never on the memory card. A line of a track that is no
longer in the folder stays in the file. A rebuilt track counts as a new
track. RETRY drives the time trial again, CHANGE LEVEL goes back to the
track list.

Crystal and CTR challenges end with RETRY or back to NITRO-PIT. Nothing is
written to your adventure save.

Custom tracks start without the camera fly-in before the countdown, even if
the track has its own camera path. The original tracks keep their fly-in.

Custom characters: the game reads every .rldchar file in its "characters"
folder at start (make them on Reload Studio's "Character" page, 4.3). It
reads the folder only then: after you build or replace a character,
restart the game. Each gets a tile in the one-player ARCADE driver select,
after the original drivers; the tiles are sorted by file name, at most 32
get one. Not in NITRO-PIT CRYSTAL or CTR, not in the original Time Trial,
Adventure, Battle or with two players (the NITRO-PIT time trial takes them).
- A broken file is skipped and the game starts anyway; the log says why (a
  line starting with "[CTR Char] REFUSED").
- The driving style chosen in Reload Studio decides how the kart drives
  (speed, acceleration, turning) and its engine sound.
- A character built with "Show kart wheels" off (4.3) drives without the
  game's kart wheels - its model brings its own. Tyre dust and skid marks
  stay. The log line "[CTR Char] loaded ..." then contains ", wheels hidden".
- The mask chosen in Reload Studio (4.3) is the one the driver wears: the
  mask item, the rescue after a fall, its sound and music, its voice when
  you drive the wrong way and its icon in the race HUD. With Aku Aku the
  log line "[CTR Char] loaded ..." ends in ", mask aku".
- A character built with an icon (4.3) shows it on its tile in the driver
  select, for the first 20 files the game loads (sorted by file name; a
  refused file does not count). Without an icon, with a broken one and from
  the 21st loaded file on, the tile shows the portrait of Fake Crash; a log
  line starting with "[CTR Char] portrait" says which. In the race the
  ranking on the left, the results after the race and the cup standings
  show the same icon (log line "[CTR Char] hud portrait seat 0: ...").
  High score lists and profiles show the portrait of Fake Crash.
- A character built with a minimap colour (4.3) shows its marker on the
  minimap in that colour; without one it has the colour of Fake Crash
  (808080: the marker as drawn). Your own marker still blinks white.
- A custom driver without voices is silent: no voice in the race, and the
  voice volume slider plays no sample of Fake Crash for it. With voices
  (4.3, tab 4) it says its own clips at the same moments as the original
  drivers, picked the same way; an event without a clip stays silent -
  never the voice of Fake Crash. The game logs who sits where at the start
  of a race ("[CTR Char] seats: ...") and the clips of each file (a line
  starting with "[CTR Char] voices").
- The cup podium still shows the template (Fake Crash, for a character
  from Reload Studio) in place of the custom driver; the log line
  "[CTR Char] seat 0 empty: podium of an arcade cup, the pick stays" says
  so.

Mods - the CPU opponents: in the one-player ARCADE track select press X on a
track, then DOWN past the last lap row into the MODS box (in the cup select:
DOWN from a lower cup onto the MODS line) and X. The MODS page works like
the GRAPHICS page:
  CPU CHARACTERS      DEFAULT (the usual opponents) or RANDOM (drawn from all
                      fifteen original drivers)
  CPU CUSTOM DRIVERS  OFF, RANDOM (every custom driver of the "characters"
                      folder may be drawn) or SELECTED (only the ones ticked
                      on SELECT DRIVERS, all ticked at first)
Each CPU seat draws from one pool of original and custom drivers; nobody
twice, never your own driver. Every single race draws anew, a cup keeps its
opponents for all four races. The choice is saved in ctr-settings.cfg. The
log line "[CTR Mods] seats ..." names who sits where. Not in NITRO-PIT, Time
Trial, Adventure, Battle or with two players.


4. RELOAD STUDIO
----------------
ReloadStudio.exe builds a track container (.rldtrack) from your exported track,
makes cups, builds characters (.rldchar) from a 3D model, and starts the game
on your track. Keep it in the same folder as ctr_native.exe: it finds the
game and its data there, and your music needs the game's sound data.

Colours in all messages: green = done, amber = works, but read this,
red = stopped.

The window follows the display scale of the monitor it is on (also when you
move it to another one) and opens inside the screen. It can be made small;
a page that does not fit then scrolls (scroll bars, mouse wheel). In a
narrow window the sidebar shows only its icons.

4.1 Page "Track" - build a track container
  Track       The folder with your export: .lev (geometry), .vrm (textures),
              optional .sca or .sndb (music), optional track.txt (settings).
              The rows below show what was found, including "Minimap" and
              "Bot data" (nav paths - bots and auto drive need them).
  Name, Author, Version
              Shown in the game's track list.
  Modes       One box per mode. A mode your track has the data for is ticked
              and says "Playable". A mode without data is grey and says what
              is missing. Time Trial and Battle say "Coming soon".
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
The page goes in five steps, one tab each: 1 Model, 2 Driver, 3 In-game look,
4 Voices, 5 Extras. The preview stays on the right and the bar with the
messages and the build at the bottom; nothing has to be scrolled (from
1366 x 768 on). Each tab head shows its number in a circle: green = done,
amber = read the warnings, red = a problem to fix there, an outline =
optional or still to do. A click on a head, "< Back" and "Next >" in the bar,
or Ctrl+Tab and Ctrl+Shift+Tab move between the tabs.

Tab 1 Model
  Model file  Your model of driver, steering wheel and kart in one file: a
              glTF (.glb, recommended - see glTF below), an OBJ (see OBJ
              below) or a PLY with vertex colours (ASCII or binary). The
              file dialog lists "glTF binary (*.glb) - recommended" first.
              +Y up, +Z forward, at
              any scale: Reload Studio fits it to Crash size (see Size). The
              kart is the part at the bottom; the game draws the wheels (see
              "Show kart wheels"). Other parts of a vehicle that lie across
              the middle of the kart (a body, a fork, a seat) stay still with
              it; parts to one side (shoes, hands) move with the driver.
              Faces with more than four corners are split into triangles.
              Small faults of the export are repaired (see "Repair the
              model"); a model with more triangles than a driver may draw
              is refused or reduced (see "Reduce to fit"). You can also
              drag files onto the page: a .glb, .gltf, .obj or .ply is the
              model (on the card Wheels: the wheel model), a .png the icon,
              a folder the voices. A .png dragged together with a .glb,
              .gltf or .obj is not taken as the icon: it is mostly a
              texture, which the model names anyway.
              Export again as often as you like: when you come back to
              Reload Studio (or to this page), it checks again by itself if
              the model, its material file, a texture (also one put next to
              the material file since), the icon or a file of the voices
              folder has been written since the last check.
  glTF        The recommended format: glTF 2.0, best as one .glb file
              (Blender: File -> Export -> glTF 2.0, format "glTF Binary").
              It brings everything at once: all meshes of the scene (as one
              driver), their UVs, normals, materials (base colour and base
              colour texture) and textures - inside the .glb, or as files
              next to a .gltf. Reload Studio reads it like an OBJ: the same
              rules, limits, choices and messages, the same native model.
              Its shape keys are the driver's animations (tab Extras, card
              Animations; how to make them: ANIMATIONS.txt on the release
              page, docs/ANIMATIONS.md in the source code). A glTF with
              a rig (armature, skin) is refused: rigs are not supported yet
              - use shape keys. OBJ and PLY stay as they are for models
              without animations.
  OBJ         Reload Studio tells a PLY from an OBJ by what is in the file,
              not by its name: a PLY named .obj is read as a PLY, with a
              warning. A file of another format (FBX, STL, Blender...)
              is refused with its name; export it as glTF, OBJ or PLY.
              An OBJ may bring vertex colours ("v x y z r g b", as Blender
              writes them),
              materials (mtllib and usemtl, the colour Kd of the material
              file) and textures (map_Kd: PNG, JPG or TGA). The colour of
              each corner of a face:
              - vertex colours in the OBJ: the vertex colour, times the
                texture at the corner where the material has one. Kd is not
                used then - Blender writes Kd 0.8 for every material, which
                would make the model a fifth darker;
              - no vertex colours: Kd times the texture; a texture without
                Kd: the texture; neither: grey.
              A texture gives the colour at the corners of a face (where its
              UV points), not a picture on the face: small faces show it
              best. Its transparency is not used.
              Paths in the OBJ are relative to the OBJ, paths in the material
              file relative to the material file; a texture with a path of
              another computer is also found when it lies next to the
              material file. What is missing does not stop the build: the
              model keeps its vertex colours, Kd or grey instead, and the
              message list says what was missing (a click opens this tab).
              Below the model field a line says what was read, for example
              "OBJ: MTL found, 3 of 4 textures found, 2 groups - colours:
              textures", and the list below it names the material file,
              the texture of each material (found, missing, cannot be read)
              and the groups (o and g, for information only; the parts
              kart, driver and steering wheel are found as for a PLY). Faces
              with more than four corners are split into triangles; lines,
              points and curves are left out. A broken OBJ (a number that is
              none, a corner that does not exist, no faces) is refused with
              the line it found it in. Animations of your own come from the
              shape keys of a glTF (tab Extras, card Animations); a wheel
              model (card Wheels) may be a glTF, an OBJ or a PLY.
              Textures folder (a glTF or OBJ that missed a texture, or with a
              folder set): a folder where rldpack looks
              first for a texture that is not at the path the material file
              gives - by its name, also in its folders textures, tex, images
              and maps ("--textures <folder>"). Empty: not passed; rldpack
              then looks next to the material file and the model as above.
              Where the tab has no room for the list as well (at 1366 x 768
              or with a large text size), the line above it says what was
              found and the list is left out.
              On the command line an OBJ goes where a PLY goes:
              "ReloadStudio.exe --rldpack make-char --model <file.obj> ...";
              the material file and the textures are looked for as above.
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
              result. Needs "Reduce to fit": greyed out while that is off.
  Reduce to fit
              Off by default. A driver may draw a limited number of
              triangles; a model under that limit is never reduced, and the
              line below the options says so, with its count and the limit.
              A model over it is refused: the headline of the bar gives its
              triangles and the limit, and the button "Reduce to fit" below
              the options ticks this box and checks again. With the box
              ticked a model over the limit is reduced until it fits, only
              as far as needed: the shape and the colours stay, kart,
              driver and steering wheel are reduced each on its own, and the
              same model always gives the same result. Colours always stay
              in their place and no face ever turns to the back. Colour
              borders and sharp edges are guarded as well; if the target
              cannot be reached that way, the reduction first drops these
              guards and then, if needed, its colour cost, and says so.
              The page then shows both counts, triangles and draw memory
              before -> after, and "reduced to fit". Or reduce the model
              yourself in Blender (Decimate modifier).
  Show kart wheels
              On by default: the game draws its kart wheels on your kart, as
              for every driver. Turn it off for a model that brings its own
              wheels or its own vehicle: the game then draws no kart wheels
              (and no wheel reflections) for this driver; tyre dust and skid
              marks stay. The choice is stored in the .rldchar. An older
              game does not know it and draws the wheels.

Tab 2 Driver
  Name        Shown in the driver select: 1 to 17 characters, capitals,
              A-Z 0-9 space ! % ' + , - . / : < = > ? _ (anything else is
              left out while you type).
  Driving style
              Balanced, Acceleration, Speed or Turning, each with the
              original drivers that drive like it (default Balanced). This
              decides how the kart drives in the game.
  Mask        Aku Aku (like Crash, Coco, Polar, Pura, Penta) or Uka Uka
              (like the template Fake Crash, Cortex, Tiny and the rest);
              default Uka Uka, the mask of Fake Crash. Worn for the mask
              item and after a fall, with its sound and music, and shown as
              its icon in the race HUD. The choice is stored in the .rldchar;
              an older game does not know it and gives the driver Uka Uka.

Tab 3 In-game look
  Icon (PNG)  Optional. Any PNG, at any size. It becomes the 43 x 25
              portrait of the game (as much of a portrait as the game shows)
              of 15 colours; transparency in the PNG is always kept. The game
              shows it on the character's tile in the driver select and in
              the race: the ranking, the results and the cup standings (see
              3.); without an icon these show the portrait of Fake Crash.
              With an icon (greyed out without one):
              Framing: "Fit like the game's heads" (default) puts your
              subject whole into the head box of the original portraits,
              "Fill the frame" fills it and cuts at the sides or the bottom,
              "As is" takes the whole picture cut to 43:25 in the middle (a
              picture of exactly 43 x 25 pixel for pixel). Never stretched.
              "Make background transparent" (off by default) removes the
              background colour that touches the corners - above all for a
              picture without transparency; one with transparency keeps a
              thin outline. "Retail frame" (off by default) puts the
              frame and the dark half-transparent box of the original
              portraits behind your picture.
              Below: your picture, and the portrait as the game draws it on
              a colour of the race, beside the portrait of Fake Crash (read
              from the game's data next to Reload Studio; without them its
              frame is drawn as lines). It follows every change at once.
  Minimap colour
              The colour of your driver's marker on the minimap. "Choose..."
              (or a click on the colour field) picks one, "Like the
              template" goes back to the default, the colour of Fake Crash
              (808080: the marker as drawn). 80 per channel is neutral,
              higher values are brighter. The choice is stored in the
              .rldchar; an older game does not know it and shows the
              colour of Fake Crash.

Tab 4 Voices
  Voices      Optional. A folder with your voice clips as .wav (8 to 48 kHz,
              mono or stereo) or .vag (PS1). Without voices the driver is
              silent in the race; with them it says your clips at the same
              moments as the original drivers. The line below the folder
              says how many clips were found for how many of the ten events.
              The file names choose the event (the line at the bottom of the
              tab; case does not matter, a .wav goes before a .vag of the
              same name): boost1 to boost4 (a fast boost), hit1 to hit4 (hit
              by a weapon, squashed, a crash), spin1 to spin4 (spinning out),
              bigair1 to bigair4 (a boost on landing a jump), drop1 to drop4
              (laying a mine or a potion), shield1 to shield4 (an attack the
              mask takes), passing1 to passing4 (said by a driver who has
              just passed the player; a custom driver is always the player,
              so these are packed but never heard - the page shows "Passing
              - never heard"), fire1 to
              fire4 (firing a missile or a bomb, the warp orb, the clock,
              using a mask), and yes and hit, the two short sounds. Up to 4
              clips per event; the game picks one of them as it does for the
              original drivers. An event without a clip stays silent.
  Files       The files of the folder: name, length, size and event. A file
              whose name fits no event says "name not known" and is left out;
              one that is too long is cut (3.5 s for the events, 1 s for yes
              and hit). Choose a file and its event below the list ("Event"),
              or "Unassigned" to leave it out - the choice goes over its name
              and is kept until you choose another folder ("Clear" forgets it
              too). A file that is no .wav or .vag gets no event. "Play" (or a double
              click on the file) plays it as the game will: 22050 Hz mono, cut
              and normalized as it will be packed.
  Normalize volume
              On by default: every clip is brought to the same peak, just
              below full level, so that no line is much louder or quieter
              than the others. Off: the clips as recorded.
  Events      The ten events at a glance: a green number is the clips of the
              event, a grey one stays silent (Passing: never heard), a red
              one has more than 4 clips.
  Warnings about the files (too long, clipped, silent, a name that fits no
  event) and errors (more than 4 clips for an event - this stops the build -
  or a file that cannot be read) are in the message list of the bar; a
  click on one opens this tab. The circle of the tab is green only when the
  driver says at least one clip.
  The switches of rldpack for this, as Reload Studio passes them (for the
  command line: "ReloadStudio.exe --rldpack make-char ..."): --voices
  <folder>; --voice <file>=<event> gives a file its event (boost, hit, spin,
  bigair, drop, shield, passing, fire, short-yes, short-hit) or leaves it out
  (none), once per file; --voice-normalize; --voice-preview <prefix> writes
  every file as the game will hear it, as <prefix>-voice-<n>.wav. Without
  --voices none of them is passed and the driver is silent.

Tab 5 Extras
              Three cards, one at a time: the words Import, Wheels and
              Animations at the top of the tab switch between them.
  Import      How rldpack reads the model. The defaults suit most models;
              when one does not, a message names the choice to change (a
              click on it opens this card).
              Up: the axis that points up in the file - "Y (default)" as
              Blender exports with +Y up, "Z" for Blender's own axes with
              the front at -Y ("--up z"). Forward: "-Z" for a model that
              looks backwards; it is turned round ("--forward -z"). rldpack
              says "set Up to Z" when a model only fits that way.
              Colors: the palette of the model - "128 (default)" keeps up to
              128 colours, "64" as many as the original drivers have
              ("--colors 64").
              Vertex colors (an OBJ or glTF only): how a vertex colour meets
              the texture of its face. "Auto (default)": an OBJ ripped from a
              PS1 game (the vertex colours of its textured faces lie around
              0x80) lights its textures as the PS1 did - 0x80 shows the
              texture as it is -, any other model keeps texture times vertex
              colour. "Texture modulation (PS1)" and "Plain color" choose one
              of the two for every model ("--vertex-colors modulate|color" on
              the command line; Auto passes nothing).
              Native model (an OBJ or glTF only, marked "Preview feature"):
              tick it to write the model's own mesh, UVs and textures into the
              character beside the classic model. It needs "Show kart
              wheels" off or a wheel model (card Wheels). The game draws it
              with NATIVE DRIVERS set to Preview (OPTIONS -> GRAPHICS);
              otherwise it draws the classic model as before.
  Wheels      Wheels of your own on the native model (marked "Preview
              feature"). Wheel model: a glTF (.glb, recommended), an OBJ
              (with its MTL file and one texture of at most 1024 x 1024) or
              a PLY of one wheel - axle
              along X, outer side toward +X, in the axes of the model (Up
              and Forward on the card Import), like the body; at most 1024
              triangles, never reduced. A texture that is there but cannot
              be read or is too large stops the build; a missing one leaves
              the wheel in its colour. The right wheels are its mirror
              image. Wheel size:
              100 % is the game's wheel; the bottom stays on the ground.
              "Animate in the preview" spins and steers them in the
              preview. The wheels are built with "Native model" ticked and
              "Show kart wheels" on; the classic model keeps the game's
              wheels as the fallback. The line below the options says
              whether your wheels are used - the preview shows them even
              when they are not. The line below the field says what was
              read, and in amber a warning (a texture not found, an axle
              that is not X). Browse and Clear take effect at once; a wheel
              exported again (or its texture put in place) is read again
              when you come back to Reload Studio.
              Rear wheel model (optional): another wheel for the two rear
              wheels, with the same rules - for example a wider one. Empty:
              the rear wheels are the wheel model, as in the game. It is
              used only together with a wheel model.
              Front axle / Rear axle with Fwd, Up and Track: moves the
              wheels of that axle, in model units (the game's wheel has a
              radius of 16). Fwd -32..32 (+ toward the front), Up -16..32,
              Track -32..64 (the whole track wider; each wheel moves by
              half). 0 0 0 is the game's place. The preview follows at once.
              Each of these is passed only when you set it - left alone,
              the character is built exactly as before. The game draws your
              wheels at every distance (not left out far away like its own
              tyres). Model the tread as geometry - 4 to 32 lugs, all alike,
              with a groove between each two: when the character is built,
              the lugs of each wheel model are counted from its shape, and a
              count that is certain goes into the file, so a fast wheel
              never seems to stand still or turn backwards. A count is
              stored only when every patterned part repeats that often -
              lugs, groove floors, also spokes or bolts on the rim (8 spokes
              on 4 lugs: no count). A wheel without a count (smooth, lugs
              not all alike, spokes out of step with the lugs, a pattern
              only painted in the texture, which can still flicker) gets a
              limit that is safe for up to 32 lugs: at full speed it may
              turn slower than the kart drives, never backwards;
              the messages of the build say why there is no count. A
              character built before this version has no count; build it
              again once.
  Animations  Your driver's animations, from the shape keys of a glTF
              model (.glb). In Blender, give the driver mesh shape keys
              (Object Data -> Shape Keys) named steer_left, steer_right,
              reverse, crash, jump, win and lose - the base mesh is the
              neutral pose - and export glTF 2.0 with Shape Keys on. The
              full guide is ANIMATIONS.txt on the release page (in the
              source code: docs/ANIMATIONS.md). A Blender template to start
              from, driver-template.blend (Blender 5.2), is a download on
              the release page as well. The source code carries the script
              that makes it; run it with Blender 5.2 from the folder of the
              source code:
    blender --background --factory-startup --python templates/make_driver_template.py
              (it is written next to the script; templates/README.md).
              The list
              shows each of the seven poses and where it comes from:
              "from the file" (your shape key), "mirrored" (only one
              steering key: the other side is its mirror image when the
              model is symmetric), "automatic" (no key: the driver leans as
              before), "neutral" (win and lose without a key) or "error"
              with the reason. Shape keys with other names are ignored and
              named below the list; OBJ and PLY have no shape keys and keep
              the automatic poses. Steering, reverse, crash and jump are
              built into every frame of the character, so every version of
              the game shows them. Win and lose show only with the native
              model (NATIVE DRIVERS set to Preview): after the finish line
              the driver blends to win (places 1-3) or lose (4-8). Without
              "Native model" they are not built: the list says "used only
              with the native model" and Win and Lose are greyed out.
              The preview below the list: the slider Steering from left to
              right, the tick boxes Jump, Crash, Reverse, Win and Lose (one
              at a time; untick for steering again) and Play, which plays
              the animation shown as a loop. A click on a pose in the list
              shows it. Nothing of it is built or remembered. Rigs
              (armatures) are not supported yet - use shape keys.

On the right
  Preview     The model as the game will draw it. Drag to turn it (left
              and right) and to look from higher or lower (up and down);
              drag with the right mouse button to move it, turn the mouse
              wheel to zoom, double-click for the start view again. The bar
              in the preview has the fixed views; its button View (or a
              right click) opens the menu with all of them, Reset view, a
              dark or light background, Crash size, Shadow and Exhaust (shown
              only - what is built is chosen on the tab In-game look) and,
              when the character has both, Native or Classic. None of it is
              built or remembered. The list at the top right of the card
              picks Neutral, Steering left or Steering right (the card
              Animations shows every frame and plays them). On the left
              your model, with the kart wheels the game draws under it (none
              with "Show kart wheels" off). On the right, in grey and in the
              same scale on the same floor, "Crash size": the original kart
              at Crash's size, the size your model is fitted to, with a
              plain driver figure as tall as Crash. Both turn together.
              The line below the preview says which look you see: Native
              (NATIVE DRIVERS set to Preview) when the character has a
              native model, else Classic, and why - with what to do for the
              native look (tick "Native model", Show kart wheels off or a
              wheel model, a glTF or OBJ instead of a PLY). It names the
              textures that were not found, and says when your wheels show
              but are not built. The classic colours are those on a bright
              road: on dark ground the game shades every driver, by up to
              75 %.

The bar at the bottom
  Headline    What the last check or build says, e.g. "Ready to build" (one
              line; point at it to read a longer one whole). A check or
              build that takes longer than a moment (a large model reduced,
              a closed hull) shows a bar beside it - "Reducing 45 %" - and
              the button Cancel (or Esc): it stops rldpack. A cancelled
              build writes nothing; a character file of that name from
              before stays as it was.
  Messages    What rldpack found. A click on a message (or Enter on the one
              framed with the arrow keys) opens the tab it belongs to (one
              about the icon "In-game look", one about the name "Driver").
              The list shows whole messages; "N more below" says how many
              follow - scroll to them.
  Output      Where the .rldchar is written. Empty: the game's "characters"
              folder when Reload Studio knows the game (the one on the page
              "Test in game", else the one in its own folder), else next to
              the model. A file outside the game's "characters" folder must
              be copied there; the build message says so.
  Check       Checks everything without writing (also runs by itself 0.6
              seconds after typing in a field and 0.1 seconds after a click
              on a list, a tick box or a colour; at once when the size
              slider is let go). The messages say what to fix. Reload Studio
              keeps the results of the last four checks: a change taken back
              (a tick set and cleared again, a name typed and deleted) shows
              the result kept at once, without checking again, as long as no
              file of the model has been written since. When only the name,
              the driving style, the mask, the minimap colour or the output
              change, rldpack checks only them and the model part of the
              result kept stands - no second reading and reducing of the
              same model ("Show rldpack output" shows both commands). The
              button Check always checks anew.
  Build character
              Writes the .rldchar (asks before it replaces a file) and says
              what is in it in the green headline of the bar, e.g.
              "Built: mydriver.rldchar (55 KB) - mask Aku Aku, kart wheels
              hidden." - check the mask there. The message below adds its
              SHA-256. Restart the game to load the new file - the game
              reads its "characters" folder only when it starts. "Show in
              folder" opens the folder.
  Show rldpack output
              The full checker log, in place of the messages. Its first line
              is the command Reload Studio ran ("ReloadStudio.exe --rldpack
              make-char ..."): copied to a command prompt in the folder of
              Reload Studio it checks or builds the same.

The file name is the character's identity in the game; the name in the menu
lives inside the file. The fields and options of the page are not
remembered: every start of Reload Studio begins with the defaults (only the
folders the file dialogs open in are).

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
  Time Trial        the same as Race (in the game: RACE, MODE TIME TRIAL)
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
    With the game known, the file lands in its "characters" folder; restart
    the game to see it.
[ ] Character page: a model exported at any scale comes out at Crash size
    (100 %): the same size as the grey "Crash size" kart and driver beside
    it. A model of
    more than 805 triangles is "reduced automatically" and still looks like
    itself in the game: colours in their place, no holes, no faces missing.
[ ] Character page: a model with holes, split corners or open parts -
    "Repair the model" and "Draw open parts from both sides" on and off.
    Try "Closed hull (remesh)" on a model the repair cannot fix.
[ ] Character page: a model with its own wheels or vehicle, "Show kart
    wheels" off - the game draws no kart wheels for it.
[ ] Character page: a .glb from Blender with shape keys (ANIMATIONS.txt on
    the release page)
    - the card Animations lists them, the preview plays them, and the
    driver steers, jumps and crashes with them in the game.
[ ] Character page, card Wheels: a wheel model, a rear wheel model, the
    axles moved - the preview and the game (NATIVE DRIVERS Preview) agree.
[ ] Character page: Mask Aku Aku and Uka Uka - with OPTIONS -> CHEATS ->
    MASKS on, the driver wears the chosen mask in the race and the HUD shows
    its icon.
[ ] Character page: an icon of any size and shape (also an interlaced PNG)
    - the picture on the page matches the tile in the driver select, the
    ranking in the race, the results and the cup standings.
[ ] Character page: a minimap colour - the marker on the minimap has it.
[ ] Character page: a voices folder - every file is listed with its event,
    "Play" plays it, a file given another event counts there; in the race
    the driver says its clips. Without a folder it stays silent.
[ ] Break the character on purpose: a model without kart, a very large
    model, a broken PNG, a voice file in a wrong format. Reload Studio must
    say what is wrong, never crash.
[ ] Dark mode / light mode (bottom left), resizing the window, a display
    scale of 150 % or more, a small screen, moving the window to a monitor
    with another scale: everything stays readable, and what does not fit
    can be scrolled to.

In the game
[ ] NITRO-PIT -> RACE: your track is listed, the preview plays, the race
    runs to the finish.
[ ] NITRO-PIT -> CRYSTAL (if your track has crystals): collect all, win;
    let the time run out, lose; RETRY works.
[ ] NITRO-PIT -> CTR (if your track has C, T, R): drive through C, T and R
    yourself and finish 1st -> YOU WIN. Miss a letter -> TRY AGAIN.
[ ] NITRO-PIT -> RACE -> MODE TIME TRIAL: you drive alone, the lap times
    show, and the next run shows your best time.
[ ] Pause -> QUIT in a challenge brings you back to the menu.
[ ] Your character in ARCADE: its tile is in the driver select, with its
    icon if you gave one; the race runs, the kart drives like the driving
    style you chose.
[ ] Anything that looks wrong on your track: holes, black floors, missing
    objects, sound.


7. KNOWN ISSUES
---------------
Modes and menus
- Custom tracks play Race (also as Time Trial), Crystal Challenge and CTR
  Challenge. Battle is planned.
- Time trial: the HUD keeps TIME at the top left, the rank column and the
  wumpa counter. A new record and a new best lap in one run show only NEW
  RECORD; the best lap is in the file. There is no ghost.
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
- Custom drivers without their own voice lines are silent. With voices a
  driver says nothing at an event it has no clip for.
- Wheels of their own: when the lugs cannot be counted for certain from the
  shape of the wheel (a smooth wheel, lugs that are not all alike, spokes or
  bolts out of step with the lugs, or a tread painted only in the texture),
  the wheel seems to turn slower than the kart drives at top speed. A
  painted pattern with more repeats than the stored count, or than 32, may
  still flicker at speed.
- Drivers exported with earlier builds should be re-exported in Reload
  Studio to get correct wheel tread animation.
- Native drivers in split-screen are not performance-optimized yet.
- The character's own icon shows only for the first 20 loaded files; high
  score lists and profiles show the portrait of Fake Crash.
- The cup podium shows the template (Fake Crash, for a character from
  Reload Studio) in place of the custom driver.
- Only in the one-player ARCADE driver select; not in NITRO-PIT CRYSTAL or
  CTR, the original Time Trial, Adventure, Battle or with two players.
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
their source files (.lev/.vrm/.sca/.ply/.obj), or anything else from the game
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
  docs\ANIMATIONS.md  the guide to animations of your own (ANIMATIONS.txt
                      on the release page)
  templates\          the script that makes the Blender driver template,
                      its license and README.md (the .blend itself is
                      driver-template.blend on the release page)
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
A file named portable.ini next to ReloadStudio.exe (it may be empty) does the
same without the switch: settings, logs and temporary files stay in its folder.
"--ui-scale <percent>" (75 to 300) lays the window out at that display scale
instead of the monitor's, and "--screen <w>x<h>" as if the screen were that
size - for screenshots of other setups. "--do" plays back steps (among them
"scroll top|bottom|<x> <y>" and "controls <file>", a list of what the window
shows); "ReloadStudio.exe --help" lists every switch and step.
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
