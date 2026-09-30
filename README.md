# CTR Reload Edition

CTR Reload Edition is a native Windows version of *Crash Team Racing* (PS1, 1999)
with support for custom tracks.

It is a mod of [ctr-native](https://github.com/CTR-tools/ctr-native), the native
PC port that is built on the [CTR-ModSDK](https://github.com/CTR-tools/CTR-ModSDK)
decompilation project. All credit for the decompilation and the original port
goes to those projects and their contributors.

**Status:** beta. Windows only.

## No game data included

This repository contains **source code only**. It contains no data from the
game: no disc image, no extracted files, no models, textures, sounds, music or
tracks. It also contains no custom tracks: tracks made by the community are
distributed by their authors, not here.

To play you need **your own copy** of Crash Team Racing, NTSC-U
(SCUS-94426), as a raw `.bin`/`.cue` disc image. On the first start the game
asks you to drag the disc image onto its window and extracts what it needs into
an `assets` folder next to the executable.

## What it adds

- Native Windows build (32-bit, SDL3, Vulkan renderer), no emulator needed
- Widescreen presentation
- Custom tracks as `.rldtrack` containers in a `tracks` folder, playable from
  the NITRO-PIT menu (single race, custom cups, crystal and CTR challenges)
- **Alpha-Maker** (`alphamaker.exe`): a small Windows tool for track authors that
  checks and packs a track folder into an `.rldtrack` container and starts it
  in the game
- `rldpack`: the command line packer behind the Alpha-Maker; it is built into
  `alphamaker.exe` and can also be built as a program of its own

Custom tracks are made and distributed by their authors. They are not part of
this repository.

## Download

A release carries exactly two programs, `ctr_native.exe` (the game) and
`alphamaker.exe`, plus the source code archives that GitHub attaches to every
release. Put both programs into one folder. They need Windows 10 (1903 or
newer) or Windows 11 and a graphics driver with Vulkan 1.0.

Quick states and replays exist only behind the developer switch `--dev`. They
are raw memory snapshots of the game: load only files you made yourself.

## Building

See [BUILDING.md](BUILDING.md).

## How this project is made

- Built on human-written foundations: ctr-native and the CTR-ModSDK
  decompilation.
- CTR Reload Edition is developed by two people. We write code ourselves and
  together with Claude Code (Anthropic), decide what gets built, review every
  change and test it - with measured reference runs, automated checks and
  hands-on play testing.

## License

CTR Reload Edition is licensed under the GNU General Public License v3.0, see
[LICENSE](LICENSE). Third-party components and their licenses are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Disclaimer

This is a non-commercial fan project. It is not affiliated with, endorsed by or
sponsored by Activision, Naughty Dog, Sony Interactive Entertainment or any
other rights holder. *Crash Team Racing* and *Crash Bandicoot* are trademarks
of their respective owners.

## Credits

- [ctr-native](https://github.com/CTR-tools/ctr-native): the native port this
  edition is based on
- [CTR-ModSDK](https://github.com/CTR-tools/CTR-ModSDK): the decompilation
  project that ctr-native and this edition are built on
- **Sunset Vista by Tramadoll**: the track and its extras are his work. This
  repository only contains a compatibility layer (`game/native_trackmod.c`) so
  that the track runs natively in CTR Reload. The track itself is not included.
- [PsyCross](https://github.com/OpenDriver2/PsyCross): origin of parts of the
  PS1 compatibility layer
- [SDL3](https://github.com/libsdl-org/SDL): platform layer
