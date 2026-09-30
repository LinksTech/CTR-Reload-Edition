<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/banner-dark.svg">
    <source media="(prefers-color-scheme: light)" srcset="docs/assets/banner-light.svg">
    <img alt="CTR Reload Edition" src="docs/assets/banner-light.svg" width="100%">
  </picture>
</p>

<p align="center">
  <b>A mod of <a href="https://github.com/CTR-tools/ctr-native">ctr-native</a> focused on custom tracks</b>
</p>

<p align="center">
  <a href="https://github.com/LinksTech/CTR-Reload-Edition/releases"><img alt="Download the latest release" src="https://img.shields.io/badge/Download-latest%20release-2f9bff?style=for-the-badge&logo=github&logoColor=white"></a>
</p>

<p align="center">
  <a href="https://github.com/LinksTech/CTR-Reload-Edition/actions/workflows/build.yml"><img alt="Build" src="https://img.shields.io/github/actions/workflow/status/LinksTech/CTR-Reload-Edition/build.yml?branch=main&label=build"></a>
  <a href="https://github.com/LinksTech/CTR-Reload-Edition/releases"><img alt="Latest release" src="https://img.shields.io/github/v/release/LinksTech/CTR-Reload-Edition?include_prereleases&label=release"></a>
  <a href="LICENSE"><img alt="License: GPL-3.0" src="https://img.shields.io/badge/license-GPL--3.0-blue"></a>
  <img alt="Platform: Windows" src="https://img.shields.io/badge/platform-Windows-0078D6">
</p>

<p align="center">
  <a href="#features">Features</a> &middot;
  <a href="#getting-started">Getting started</a> &middot;
  <a href="#custom-tracks">Custom tracks</a> &middot;
  <a href="#whats-next">What's next</a> &middot;
  <a href="#building-from-source">Building</a> &middot;
  <a href="#reporting-bugs">Bugs</a> &middot;
  <a href="#credits">Credits</a> &middot;
  <a href="#license-and-disclaimer">License</a>
</p>

CTR Reload Edition runs *Crash Team Racing* (PS1, 1999) natively on Windows,
without an emulator, and plays custom tracks next to the original ones.

> [!IMPORTANT]
> You need **your own disc image** of the game, NTSC-U (SCUS-94426), as
> `.cue`/`.bin`. This repository and its releases contain **no game data**: no
> disc image, no extracted files, no models, textures, music or tracks.

> [!WARNING]
> This is a **pre-release** for testing. Expect bugs - and please
> [report them](#reporting-bugs).

## Features

| 🎨 Graphics | 🏁 Custom tracks | ✨ Quality of life |
| --- | --- | --- |
| Native Vulkan renderer, fullscreen or windowed (F11 or Alt+Enter) | Tracks as single `.rldtrack` files in a `tracks` folder | Drag the disc image onto the window once - that is the whole setup |
| Aspect ratio 4:3, 16:9 or 21:9, or Auto for your screen | ARCADE → NITRO-PIT: single race, custom cups, Crystal Challenge and CTR Challenge | All drivers available from the start |
| Internal resolution up to 8x, anti-aliasing 2x or 4x | Bots on every track that has nav paths, a preview of each track in the track selection | The race pauses when the window loses focus or is minimized |
| All of it in OPTIONS → GRAPHICS, saved for the next start | The **Alpha-Maker** builds containers and cups and starts the game on your track | The game keeps its last 5 logs, ready for a bug report |

## Getting started

1. Download **`ctr_native.exe`** (the game) and **`alphamaker.exe`** (the track
   tool) from the [latest release](https://github.com/LinksTech/CTR-Reload-Edition/releases)
   into one folder you can write to, for example `C:\Games\CTR Reload` (not
   `C:\Program Files`).
2. Start `ctr_native.exe` and drag your disc image (`.cue` or `.bin`) onto the
   window. The game unpacks what it needs into an `assets` folder next to it,
   once (about 520 MB).
3. Set up the picture in OPTIONS → GRAPHICS.
4. Create a folder `tracks` next to the game and put `.rldtrack` files into it.
5. Race them: ARCADE → NITRO-PIT → RACE, CUP, CRYSTAL or CTR.

Windows 10 (1903 or newer) or Windows 11 and a graphics driver with Vulkan 1.0.

## Custom tracks

The Alpha-Maker (`alphamaker.exe`) builds a track container from your exported
track folder (`.lev`, `.vrm` and optional music), checks it and records its
preview. On its Cups page you put up to four cups together, and "Test in game"
starts a race on your track straight away. Keep it next to `ctr_native.exe`.

<details>
<summary>What a track needs for each mode</summary>

| Mode | Needs |
| --- | --- |
| Race | restart points (the checkpoints that count the laps); bots also need nav paths |
| Crystal Challenge | at least one crystal |
| CTR Challenge | each of the letters C, T and R exactly once |
| Time Trial, Battle | not available yet |

At most 110 placed objects (crates, fruit, letters and every other placed
model); above 90 the Alpha-Maker warns.

</details>

Every page and message of the Alpha-Maker is explained in section 4 of the
[Alpha-Maker guide](tools/package/README.txt).

## What's next

Planned, in no particular order and without a date:

- Time Trial and Battle on custom tracks
- Best times on custom tracks
- Custom characters
- Skin support for the drivers
- ...and more

We build continuously; a new version comes out when enough has come together.
Until then, fixes and additions land here first.

## Building from source

Visual Studio 2022, CMake, the Vulkan SDK and Python.

<details>
<summary>Steps and developer notes</summary>

From a command prompt in the repository root:

    build-msvc.bat

This builds the Release configuration in `build-msvc-x86\` and runs the
self-tests. Requirements, results, tests and packaging are described in
[BUILDING.md](BUILDING.md).

Quick states and replays exist only behind the developer switch `--dev`. They
are raw memory snapshots of the game: load only files you made yourself.

</details>

## Reporting bugs

Open an [issue](https://github.com/LinksTech/CTR-Reload-Edition/issues) with
the bug report template, one issue per problem.

<details>
<summary>What to include - and what never to attach</summary>

- The version: `ctr_native.exe --version`
- What happened, what you expected, and the steps to get there
- The newest log from the `logs` folder next to the game
- For a custom track: its file name and the SHA-256 the Alpha-Maker shows

Issues are public: never attach disc images, the `assets` folder, memory cards
or other game data, and edit your Windows user name out of the log if you mind.

</details>

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

Other components (PsyCross, SDL3 and more), the banner font and their licenses
are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## License and disclaimer

CTR Reload Edition is licensed under the
[GNU General Public License v3.0](LICENSE).

This is a non-commercial fan project. It is not affiliated with, endorsed by or
sponsored by Activision, Naughty Dog, Sony Interactive Entertainment or any
other rights holder. *Crash Team Racing* and *Crash Bandicoot* are trademarks
of their respective owners.
