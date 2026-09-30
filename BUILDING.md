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

`build-msvc.bat` runs six self-tests: version string, the rldpack self-test
(directly and through the Alpha-Maker), the shader SPIR-V check, the GTE
self-test and the unit tests of the matching tools. None of them needs game
data, a window or a GPU.

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

## Before you push

Enable the push guard once per clone. It refuses pushes that contain game
data, third-party content or files larger than 5 MB:

    git config core.hooksPath tools/git-hooks
