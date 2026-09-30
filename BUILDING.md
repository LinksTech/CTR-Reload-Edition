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
- **Git** (optional; without it the build ID is `unknown`, and the package
  script refuses to run)

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
- `alphamaker.exe`: the track authoring tool
- `rldpack.exe`: the command line track container packer

## Tests

`build-msvc.bat` runs six self-tests: version string, the rldpack self-test
(directly and through the Alpha-Maker), the shader SPIR-V check, the GTE
self-test and the unit tests of the matching tools. None of them needs game
data, a window or a GPU.

## Package

In Git Bash, from a clean working tree, after `build-msvc.bat`:

    BAU=build-msvc-x86 bash tools/paket/paket-beta0.sh

This writes `dist\CTR-Reload-Beta0-<build id>.zip` (game, Alpha-Maker,
rldpack, license files, README) and a zip with the matching source code. The
script refuses to package anything that looks like game data.

## Before you push

Enable the push guard once per clone. It refuses pushes that contain game
data, third-party content or files larger than 5 MB:

    git config core.hooksPath tools/git-hooks
