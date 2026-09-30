# Building CTR Reload Edition (Windows)

The project builds from source only. Building and testing need no game data.
The game itself needs your own disc image at run time (see [README.md](README.md)).

## Requirements

- **Visual Studio 2022** (or Build Tools 2022) with the workload
  *Desktop development with C++* (MSVC x86/x64 tools and a Windows 10/11 SDK)
- **CMake 3.21 or newer** on `PATH` (the one bundled with Visual Studio works)
- **LunarG Vulkan SDK** (used for the Vulkan headers and `glslangValidator`,
  which compiles the shaders). The installer sets `VULKAN_SDK`.
  The automatic builds use version 1.3.296.0.
- **Python 3.10 or newer** on `PATH` (needed by the test suite; CMake looks
  for it when configuring)
- **Git** (optional for building; without it the build ID is `unknown`). The
  package script needs Git Bash, Git and Python.

MSVC is the only supported toolchain: the 32-bit preset `windows-msvc-x86`,
which `build-msvc.bat` uses. `CMakeLists.txt` and `CMakePresets.json` still
have MinGW branches and presets, but MinGW is untested: no MinGW build is made
or tested, and the automatic builds use MSVC only. A MinGW build would also
leave out the Alpha-Maker, which is built under MSVC only.

## Build

From a command prompt in the repository root:

    build-msvc.bat

This configures a 32-bit (Win32) build in `build-msvc-x86\`, builds the
Release configuration and runs the self-tests. It is the same command the
automatic builds on GitHub use.

The same steps with CMake presets:

    cmake --preset windows-msvc-x86
    cmake --build --preset windows-msvc-x86-release --parallel
    ctest --preset windows-msvc-x86-release

## Results

In `build-msvc-x86\Release\`:

- `ctr_native.exe` (and `ctr_native.pdb`): the game
- `alphamaker.exe`: the track authoring tool; it carries the track container
  packer built in (`alphamaker.exe --rldpack <command>`)
- `rldpack.exe`: the same packer as a command line program of its own (used by
  the self-tests; not part of the package)
- `shader_spirv_probe.exe`: a build helper that writes the shader sources

`ctr_native.exe --version` reports the version and the build ID, for example
`CTR Reload Beta 0 (a1b2c3d4e5f6)`; the Alpha-Maker shows the same in its
title bar. The build ID is the commit, with `-dirty-<hash>` appended when
tracked files differ from it.

## Tests

`build-msvc.bat` runs nine self-tests:

- `ctr_native_version`: the version string
- `rldpack_selftest` and `alphamaker_rldpack_selftest`: the rldpack self-test,
  directly and through the Alpha-Maker
- `shader_spirv_dialect`: the shader SPIR-V check
- `gte_paths_identical`: the GTE self-test
- `ctr_match_unit`: the unit tests of the matching tools
- `selftest_bad_containers`: `ctr_native --dev --make-test-containers` writes
  made-up track containers into `build-msvc-x86\selftest\containers` (valid
  `good-*` ones and `bad-*` ones that each break one thing: a length, an
  offset, a count, a table), and `ctr_native --dev --selftest-containers` must
  accept every good one and refuse every bad one
- `selftest_bad_disc`: the same for disc images - `ctr_native --dev
  --make-test-disc` writes tiny made-up images into
  `build-msvc-x86\selftest\disc`, and `ctr_native --dev --selftest-disc` must
  extract the good ones and refuse the bad ones without writing anything
  outside the output folder
- `lang_check`: everything in this repository is English -
  `tools/lang_check.py` checks the contents and the file and folder names and
  fails on German words, umlauts, internal references, local paths and
  day.month.year dates; older German names that stay for now are listed in
  `tools/lang_check_allow.txt`. It also refuses the person names and the local
  checkout folders (lines `path <folder>`) listed in the local, untracked file
  `tools/lang_check_names.local.txt`; without that file (as in the automatic
  builds) these two checks are skipped

None of them needs game data, a window or a GPU; every test file is made up
by the game itself (`platform/native_testfiles.c`).

Quick states and replays (developer switches behind `--dev`) are raw memory
snapshots of the game: load only files you made yourself.

## Package

In Git Bash, from a clean working tree (all changes committed), after
`build-msvc.bat`:

    BUILD_DIR=build-msvc-x86 bash tools/package/package.sh

`BUILD_DIR` is the build folder (default `build-msvc-x86`). The script builds
`ctr_native` and `alphamaker` there again (output in `dist-build.log`), checks
that both carry the same build ID, and writes
`dist\CTR-Reload-<version>-<build id>.zip`, for example
`CTR-Reload-Beta0-a1b2c3d4e5f6.zip`. The version is `CTR_NATIVE_VERSION` from
`CMakeLists.txt` without spaces. The zip holds one folder of the same name with:

- `ctr_native.exe`, `ctr_native.pdb`, `alphamaker.exe`
- `README.txt` and `RELEASE-NOTES.txt` (from `tools/package/`)
- `LICENSE` and `THIRD_PARTY_NOTICES.md`
- `<name>-source.zip`: the source code of the packaged commit

The script refuses a tree with uncommitted changes (`--allow-dirty` makes a
trial package anyway) and refuses to package anything that looks like game
data.

## Automatic builds

- `.github/workflows/build.yml`: every push to `main` or `dev` and every pull
  request builds and runs the self-tests; a version tag (`v*`) also makes the
  release with `ctr_native.exe`, `alphamaker.exe`, `LICENSE` and
  `THIRD_PARTY_NOTICES.md`.
- `.github/workflows/nightly.yml`: every day at 21:00 UTC (and by hand) the
  pre-release `nightly-builds` is replaced with a build of `dev` that carries
  the same four files - only when `dev` has changed since the last one. The
  tag `nightly-builds` always points at the commit it was built from.
- Both use the same build job, `.github/workflows/build-job.yml`.
- `.github/workflows/guard.yml` (check `content-guard`): every push and every
  pull request runs `tools/content_guard.py`, which checks every commit in the
  history of the checked-out commit, the whole tree of each: it fails on game
  data file types and on binary files larger than 1 MB. The same check by
  hand: `python tools/content_guard.py --history HEAD`.

## Before you commit and push

Enable the hooks once per clone:

    git config core.hooksPath tools/git-hooks

- `pre-commit` and `commit-msg` run `tools/lang_check.py` on the staged files
  and on the commit message: everything in this repository is English. For
  the person-name and local-folder checks, list the names (one per line) and
  your checkout folder (`path <folder>`) in `tools/lang_check_names.local.txt`
  (untracked, it stays on your machine).
- `pre-push` refuses pushes that contain game data, third-party content or
  files larger than 5 MB.
