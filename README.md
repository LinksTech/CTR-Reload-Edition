<h1 align="center">CTR Reload Edition</h1>

<p align="center">
  A mod of <a href="https://github.com/CTR-tools/ctr-native">ctr-native</a> focused on custom tracks
</p>

<p align="center">
  <a href="https://github.com/LinksTech/CTR-Reload-Edition/actions/workflows/build.yml"><img alt="Build" src="https://img.shields.io/github/actions/workflow/status/LinksTech/CTR-Reload-Edition/build.yml?branch=main&label=build"></a>
  <a href="https://github.com/LinksTech/CTR-Reload-Edition/releases"><img alt="Latest release" src="https://img.shields.io/github/v/release/LinksTech/CTR-Reload-Edition?include_prereleases&label=release"></a>
  <a href="LICENSE"><img alt="License: GPL-3.0" src="https://img.shields.io/badge/license-GPL--3.0-blue"></a>
  <img alt="Platform: Windows" src="https://img.shields.io/badge/platform-Windows-0078D6">
</p>

<p align="center">
  <a href="#features">Features</a> &middot;
  <a href="#download">Download</a> &middot;
  <a href="#getting-started">Getting started</a> &middot;
  <a href="#custom-tracks">Custom tracks</a> &middot;
  <a href="#building-from-source">Building</a> &middot;
  <a href="#reporting-bugs">Bugs</a> &middot;
  <a href="#credits">Credits</a> &middot;
  <a href="#license-and-disclaimer">License</a>
</p>

CTR Reload Edition runs *Crash Team Racing* (PS1, 1999) natively on Windows,
without an emulator, and plays custom tracks next to the original ones. You
need **your own disc image** of the game, NTSC-U (SCUS-94426), as `.cue`/`.bin`.
This repository and its releases contain **no game data**: no disc image, no
extracted files, no models, textures, music or tracks.

## Features

🎨 **Graphics**
- Native Vulkan renderer, fullscreen or windowed (F11 or Alt+Enter)
- Aspect ratio 4:3, 16:9 or 21:9, or Auto for your screen
- Internal resolution up to 8x, anti-aliasing 2x or 4x
- All of it in OPTIONS → GRAPHICS, saved for the next start

🏁 **Custom tracks**
- Tracks come as single `.rldtrack` files in a `tracks` folder
- ARCADE → NITRO-PIT: single race, custom cups, Crystal Challenge and
  CTR Challenge on custom tracks
- Bots on every track that has nav paths, a preview of each track in the
  track selection
- The **Alpha-Maker** turns an exported track into a container, makes cups
  and starts the game on your track

✨ **Quality of life**
- Drag the disc image onto the window once - that is the whole setup
- All drivers available from the start
- The race pauses when the window loses focus or is minimized
- The game keeps its last 5 logs, ready to attach to a bug report

## Download

Get **`ctr_native.exe`** (the game) and **`alphamaker.exe`** (the track tool)
from the [latest release](https://github.com/LinksTech/CTR-Reload-Edition/releases)
and put both into the same folder. They need Windows 10 (1903 or newer) or
Windows 11 and a graphics driver with Vulkan 1.0.

## Getting started

1. Put `ctr_native.exe` and `alphamaker.exe` into a folder you can write to,
   for example `C:\Games\CTR Reload` (not `C:\Program Files`).
2. Start `ctr_native.exe` and drag your disc image (`.cue` or `.bin`) onto the
   window. The game unpacks what it needs into an `assets` folder next to it,
   once (about 520 MB).
3. Set up the picture in OPTIONS → GRAPHICS.
4. Create a folder `tracks` next to the game and put `.rldtrack` files into it.
5. Race them: ARCADE → NITRO-PIT → RACE, CUP, CRYSTAL or CTR.

## Custom tracks

The Alpha-Maker (`alphamaker.exe`) builds a track container from your exported
track folder (`.lev`, `.vrm` and optional music), checks it and records its
preview. On its Cups page you put up to four cups together, and "Test in game"
starts a race on your track straight away. Keep it next to `ctr_native.exe`.
Every page and message is explained in section 4 of the
[Alpha-Maker guide](tools/package/README.txt).

## Building from source

Visual Studio 2022, CMake, the Vulkan SDK and Python - the steps are in
[BUILDING.md](BUILDING.md). Quick states and replays exist only behind the
developer switch `--dev`; they are raw memory snapshots of the game, so load
only files you made yourself.

## Reporting bugs

Open an [issue](https://github.com/LinksTech/CTR-Reload-Edition/issues) and
use the bug report template: one issue per problem, with the version
(`ctr_native.exe --version`) and the newest log from the `logs` folder next to
the game. Issues are public: never attach disc images, the `assets` folder,
memory cards or other game data, and edit your Windows user name out of the
log if you mind.

## How this project is made

- Built on human-written foundations: ctr-native and the CTR-ModSDK
  decompilation.
- CTR Reload Edition is developed by two people. We write code ourselves and
  together with Claude Code (Anthropic), decide what gets built, review every
  change and test it - with measured reference runs, automated checks and
  hands-on play testing.

## Credits

- [ctr-native](https://github.com/CTR-tools/ctr-native): the native PC port
  this edition is based on
- [CTR-ModSDK](https://github.com/CTR-tools/CTR-ModSDK): the decompilation
  project that ctr-native and this edition are built on
- **Sunset Vista by Tramadoll**: the track and its extras are his work. This
  repository only contains a compatibility layer (`game/native_trackmod.c`) so
  that the track runs natively in CTR Reload. The track itself is not included.

Other components (PsyCross, SDL3 and more) and their licenses are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## License and disclaimer

CTR Reload Edition is licensed under the
[GNU General Public License v3.0](LICENSE).

This is a non-commercial fan project. It is not affiliated with, endorsed by or
sponsored by Activision, Naughty Dog, Sony Interactive Entertainment or any
other rights holder. *Crash Team Racing* and *Crash Bandicoot* are trademarks
of their respective owners.
