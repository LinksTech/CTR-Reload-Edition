<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/banner-dark.svg">
    <source media="(prefers-color-scheme: light)" srcset="docs/assets/banner-light.svg">
    <img alt="CTR Reload Edition" src="docs/assets/banner-light.svg" width="100%">
  </picture>
</p>

<p align="center">
  <b>A PC edition of Crash Team Racing built for custom content — custom tracks today, custom characters Soon™.<br>
  Built on <a href="https://github.com/CTR-tools/ctr-native">ctr-native</a>.</b>
</p>

<p align="center">
  <!-- Points at the current version on purpose: the nightly build is newer and would come first in the release list. Update with every version tag. -->
  <a href="https://github.com/LinksTech/CTR-Reload-Edition/releases/tag/v0.0-beta0"><img alt="Download the latest release" src="https://img.shields.io/badge/Download-latest%20release-2f9bff?style=for-the-badge&logo=github&logoColor=white"></a>
  <br>
  <sub>Want the newest state? The <a href="https://github.com/LinksTech/CTR-Reload-Edition/releases/tag/nightly-builds">nightly build</a> is made from the development branch every night - untested, no guarantees.</sub>
</p>

<p align="center">
  <a href="https://github.com/LinksTech/CTR-Reload-Edition/actions/workflows/build.yml"><img alt="Build" src="https://img.shields.io/github/actions/workflow/status/LinksTech/CTR-Reload-Edition/build.yml?branch=main&label=build"></a>
  <a href="https://github.com/LinksTech/CTR-Reload-Edition/releases"><img alt="Latest release" src="https://img.shields.io/github/v/tag/LinksTech/CTR-Reload-Edition?filter=v*&label=release"></a>
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
> disc image, no extracted files, no models, textures, music or tracks. Apart
> from the bundled SDL library, they hold no 3D models, voices, sound effects
> or images made by other people either.

> [!WARNING]
> This is a **pre-release** for testing. Expect bugs - and please
> [report them](#reporting-bugs).

## Features

### 🎨 Graphics
- New Vulkan renderer
- Internal resolution up to 8x, MSAA anti-aliasing
- Real widescreen: 4:3, 16:9 and 21:9 — the view gets wider, nothing
  is stretched
- Menus stay nicely centered on ultrawide screens

### 🏁 Custom tracks — our main focus
- Drop a .rldtrack file into the tracks folder and it shows up in game
- New NITRO-PIT mode under Arcade: race custom tracks or build your own
  custom cups
- Tracks bring their own music and their own minimap, scaled
  automatically for the track screen
- Fixes for crashes found in community tracks, and broken track data
  gets caught before you race, not mid-race
- Reload Studio: turns your track data into a ready-to-play file — no
  command line, no scripts, no patching, just a window

### ⚙️ Under the hood
- Custom memory budget: big custom tracks get extra room (up to 32 MB),
  and buffers are sized per track. Original tracks keep their original
  tables, with larger draw and clip buffers
- New VRAM handling: palette textures go through a GPU atlas instead of
  being decoded from emulated PS1 VRAM for every pixel — groundwork for
  higher-res textures later
- Less emulation where it doesn't matter: several PS1 rendering steps
  now run natively, like transparency in a single pass

### ✨ Quality of life
- First-start window: just drag in your disc image
- Settings are remembered, faster boot
- Race pauses when you alt-tab or minimize
- New main menu with Options (cheats, scrapbook) and Exit
- All characters unlocked from the start — your save stays untouched

## Getting started

1. Download **`ctr_native.exe`** (the game) and **`alphamaker.exe`** (the track
   tool) from the [latest release](https://github.com/LinksTech/CTR-Reload-Edition/releases/tag/v0.0-beta0)
   into one folder you can write to, for example `C:\Games\CTR Reload` (not
   `C:\Program Files`). Beta 0 ships the tool as `alphamaker.exe`; it has been
   called Reload Studio since then.
2. Start `ctr_native.exe` and drag your disc image (`.cue` or `.bin`) onto the
   window. The game unpacks what it needs into an `assets` folder next to it,
   once (about 520 MB).
3. Set up the picture in OPTIONS → GRAPHICS.
4. Create a folder `tracks` next to the game and put `.rldtrack` files into it.
5. Race them: ARCADE → NITRO-PIT → RACE, CUP, CRYSTAL or CTR.

Windows 10 (1903 or newer) or Windows 11 and a graphics driver with Vulkan 1.0.

## Custom tracks

Reload Studio (`ReloadStudio.exe`) builds a track container from your exported
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
model); above 90 Reload Studio warns.

</details>

Every page and message of Reload Studio is explained in section 4 of the
[Reload Studio guide](tools/package/README.txt).

## What's next

Planned, in no particular order and without a date:

- Time Trial and Battle on custom tracks
- Best times on custom tracks
- Custom characters - a first version is in the development version (Reload
  Studio's Character page and a `characters` folder next to the game). It
  fits every model onto Crash's kart at his size, repairs common export
  faults, reduces a model with too many triangles by itself and can leave
  out the kart wheels for a model with its own. A character's icon shows in
  the driver select. Their voices come next
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
- For a custom track: its file name and the SHA-256 Reload Studio shows

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
- Development happens openly, in batches and without fixed dates; how to
  contribute is described in [CONTRIBUTING.md](CONTRIBUTING.md).

## Credits

- [ctr-native](https://github.com/CTR-tools/ctr-native): the native PC port
  this edition is based on
- [CTR-ModSDK](https://github.com/CTR-tools/CTR-ModSDK): the decompilation
  project that ctr-native and this edition are built on
- **Sunset Vista by Tramadoll**: the track and its extras are his work. This
  repository only contains a compatibility layer (`game/native_trackmod.c`) so
  that the track runs natively in CTR Reload. The track itself is not included.
  Sunset Vista compatibility data used with permission of Tramadoll.

Other components (PsyCross, SDL3 and more), the banner font and their licenses
are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## License and disclaimer

CTR Reload Edition is licensed under the
[GNU General Public License v3.0](LICENSE).

This is a non-commercial fan project. It is not affiliated with, endorsed by or
sponsored by Activision, Naughty Dog, Sony Interactive Entertainment or any
other rights holder. *Crash Team Racing* and *Crash Bandicoot* are trademarks
of their respective owners.
