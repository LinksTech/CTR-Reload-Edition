# Third-Party Notices

This project vendors third-party software and contains modified third-party
derivatives. Keep this file with source and binary distributions of CTR Reload Edition
(based on CTR Native).

The release downloads contain two programs. `ctr_native.exe` carries SDL3
(with the components compiled into it), the PsyCross-derived platform layer
with the code that came in through PsyCross (TOMB5, MAME, PCSX, one PSn00bSDK
table and material from Sony's Psy-Q SDK), and the Sunset Vista compatibility
layer. `alphamaker.exe` carries only this project's own code and the Microsoft
C runtime.

## PsyCross / Psy-X

Source: <https://github.com/OpenDriver2/PsyCross>

PsyCross provided the starting point for parts of CTR Native's
Psy-Q-compatible PS1 hardware abstraction layer, including compatible GPU, GTE,
SPU, CD, and controller library interfaces. CTR Native vendored PsyCross from
commit `603475326dfa546cb47a6cc338c32053cca56022` and later backported
upstream fixes. The derived files are compiled into `ctr_native.exe` only;
`alphamaker.exe` contains none of this code.

Files derived from PsyCross:

- `include/psx/`: `asm.h`, `gtemac.h`, `gtereg.h`, `inline_c.h`, `kernel.h`,
  `libapi.h`, `libcd.h`, `libetc.h`, `libgpu.h`, `libgte.h`, `libpad.h`,
  `libspu.h`, `r3000.h`, `strings.h`
- `include/platform/`: `native_gpu.h`, `native_renderer_types.h`
- `platform/`: `native_gpu.c`, `native_gte_core.c`, `native_gte_alt.c`,
  `native_gte_ratan_tbl.h`, `native_gte_rcossin_tbl.h`,
  `native_gte_sqrt_tbl.h`, `native_inline_c.c`, `native_libapi.c`,
  `native_libetc.c`, `native_libgpu.c`, `native_libgte.c`, `native_libpad.c`,
  `native_libspu.c`, `native_input.c`, `native_platform.c`, `native_audio.c`,
  `native_renderer.c`, `native_shaders.inc`

Where these files carry a provenance header, it names the original as
`externals/PsyCross/<path>`; `<path>` is relative to the PsyCross repository.

Several of these headers and one of the GTE tables contain material from Sony's
Psy-Q SDK; see [Psy-Q SDK material](#psy-q-sdk-material).

The GTE tables:

- `platform/native_gte_ratan_tbl.h` comes from CTR Native, which took it from
  PsyCross `src/gte/ratan_tbl.h` (MIT, (c) 2020 REDRIVER2 Project) with only
  formatting changes. It is a Psy-Q table (see
  [Psy-Q SDK material](#psy-q-sdk-material)).
- `platform/native_gte_rcossin_tbl.h` comes from CTR Native, which took it
  from PsyCross `src/gte/rcossin_tbl.h` (MIT, (c) 2020 REDRIVER2 Project).
  PsyCross got the table via REDRIVER2 from the TOMB5 project
  (`GAME/CAMERA.C`, MIT, (c) 2017 Gh0stBlade & zdimension, see
  [TOMB5 PSX emulator layer](#tomb5-psx-emulator-layer)). Its 8192 values
  equal the `rcossin_tbl` of Sony's Psy-Q LIBGTE 4.4 and later (module CSTBL)
  and are exactly `round(4096 * sin(2 * pi * k / 4096))` and
  `round(4096 * cos(2 * pi * k / 4096))`.
- `platform/native_gte_sqrt_tbl.h` comes from CTR Native, which took it from
  PsyCross `src/gte/sqrt_tbl.h` (MIT, (c) 2020 REDRIVER2 Project). REDRIVER2
  had taken the table from PSn00bSDK `libpsn00b/psxgte/squareroot.s`
  (MPL 2.0, see [PSn00bSDK](#psn00bsdk)). Its 192 values equal the `SQRT`
  table of Sony's Psy-Q LIBGTE (module SQRTBL) and are exactly
  `floor(4096 * sqrt(n / 64))` for n = 64 to 255.

License: MIT

Copyright (c) 2020 REDRIVER2 Project

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

### TOMB5 PSX emulator layer

Source: <https://github.com/TOMB5/TOMB5> (`EMULATOR/`)

The PsyCross README credits the TOMB5 PSX emulator layer as its original
source and base. Parts of it remain in the PsyCross-derived files listed
above: GPU primitive handling, `libgte` helpers, the GTE vertex stage and the
VRAM/CLUT sampling in `platform/native_shaders.inc`. The `rcossin_tbl` table
also comes from TOMB5 (see above).

License: MIT

```
MIT License

Copyright (c) 2017 Gh0stBlade & zdimension

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### MAME PlayStation GTE emulator

Source: <https://github.com/mamedev/mame> (`src/devices/cpu/psx/gte.cpp`,
`license:BSD-3-Clause`, `copyright-holders:smf`)

The GTE in `platform/native_gte_core.c` and `platform/native_gte_alt.c`
(saturation and flag helpers, the division with its reciprocal table, the
coprocessor commands) and the register macros in `include/psx/gtereg.h`
derive from MAME's PlayStation GTE emulator, via TOMB5 and PsyCross.

License: BSD-3-Clause

```
Copyright 2003-2013 smf

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### PCSX

Source: <https://github.com/pcsxr/PCSX-Reloaded>
(`pcsxr/libpcsxcore/r3000a.h`)

The GTE register structures in `include/psx/gtereg.h` (`PAIR`, `CBGR`,
`SVector*`, `SMatrix3D`, `psxCP2Data`, `psxCP2Ctrl`, `psxCP0Regs`) derive
from PCSX.

Copyright (C) 2007 Ryan Schultz, PCSX-df Team, PCSX team

License: GNU General Public License, version 2 or (at your option) any later
version. CTR Reload Edition uses this code under version 3, like the rest of
the program; the full text is in `LICENSE`.

## Psy-Q SDK material

The Psy-Q SDK is the software development kit for the original PlayStation,
with the run-time libraries of Sony Computer Entertainment (LIBGTE, LIBGPU,
LIBCD, LIBSPU, LIBAPI, LIBETC, LIBPAD and others) and their headers. The files
below contain material from it. The MIT license of PsyCross above does not
cover the Psy-Q material in the PsyCross-derived files.

### Headers in `include/psx/`

The 14 PsyCross-derived headers in `include/psx/` (listed under
[PsyCross / Psy-X](#psycross--psy-x)) came to this project from PsyCross (MIT)
through CTR Native; the provenance header of each names its PsyCross original.
They mirror the Psy-Q run-time library headers, so that the game code, which
was written for Psy-Q, builds against them. What they contain from those
headers, by file:

- `gtemac.h`, `inline_c.h`, `kernel.h`, `libapi.h`, `libcd.h`, `libgpu.h`,
  `libgte.h`, `libspu.h`: names of macros, constants, types and functions,
  structure layouts, and comments of the corresponding Psy-Q headers.
- `libetc.h`, `libpad.h`: names of constants, macros and functions of the
  corresponding Psy-Q headers, with no Psy-Q comment text.
- `r3000.h`: MIPS R3000 definitions with their comments, as the Psy-Q headers
  carried them.

`asm.h` and `strings.h` contain only a few declarations and no Psy-Q comment
text; the content of `gtereg.h` comes from MAME and PCSX (see above) and
contains no Psy-Q comment text either. `psx_prelude.h` and `psn00b_prelude.h`
in the same folder are not derived from PsyCross.

### `platform/native_gte_ratan_tbl.h`

This file holds `ratan_tbl`, the arctangent table of 1025 values that the
`ratan2` function of Psy-Q LIBGTE uses. Its provenance header names only its
PsyCross original, `src/gte/ratan_tbl.h` (see
[PsyCross / Psy-X](#psycross--psy-x)). In the NTSC-U retail executable, which
links LIBGTE, the table lies at address `0x8008A480`, at the start of the data
that `include/regionsEXE.h` marks as Psy-Q data.

### `game/MEMCARD/MEMCARD_Card.c`

This file is decompiled retail code (see
[Origin of game data, texts and decompiled tables](#origin-of-game-data-texts-and-decompiled-tables))
and is compiled into `ctr_native.exe`. Its comments mark two parts as
"copy/pasted by Naughty Dog" from the memory card sample of the Psy-Q SDK,
`psx\sample\memcard\CARD\CARD.C`:

- in `MEMCARD_InitCard`, the block that opens and enables the eight memory
  card events (`SwCARD` and `HwCARD`, each with `EvSpIOE`, `EvSpERROR`,
  `EvSpTIMOUT` and `EvSpNEW`) from `EnterCriticalSection` to
  `ExitCriticalSection`, marked as lines 84 to 101 of the sample;
- the body of `MEMCARD_CloseCard` (`StopCARD`, then closing the same eight
  events), marked as lines 355 to 365 of the sample.

The comments mark no other part of the file.

## PSn00bSDK

Source: <https://github.com/Lameguy64/PSn00bSDK>

Path: `include/psn00bsdk`

Copyright (C) 2019-2023 Lameguy64 / Meido-Tek Productions, spicyjpeg, Soapy
and other PSn00bSDK contributors (one header also names the PSXSDK authors);
the header of each file names its authors.

License: Mozilla Public License 2.0 (MPL 2.0). A copy of the license can be
obtained at <https://mozilla.org/MPL/2.0/>.

`include/psn00bsdk/include` holds 14 headers of PSn00bSDK 0.23. They were
taken from the CTR-ModSDK project (<https://github.com/CTR-tools/CTR-ModSDK>,
`include/psn00bsdk`) and are modified: include paths, register base
addresses, some type and function declarations in `psxapi.h`, `psxgpu.h` and
`psxspu.h`, MSVC structure packing in `psxpad.h`, and code formatting.
`include/psn00bsdk/local changes.txt` lists part of these changes. The
modified files remain under the MPL 2.0 and keep their original copyright and
license notices. Their source code is in this repository at
`include/psn00bsdk` and in the source code archive of every release.

These headers are not compiled into `ctr_native.exe` or `alphamaker.exe`: no
source file of the programs includes them (`include/psx/psn00b_prelude.h`,
which would, is itself included nowhere). `include/gpu.h` (primitive helper
macros) and `include/namespace_Gamepad.h` (controller button bit values)
contain small parts modelled on PSn00bSDK's `psxgpu.h` and `psxpad.h`; their
source code is in this repository as well, and any PSn00bSDK code in them is
under the MPL 2.0.

`libpsn00b` is not part of this project, with one exception: the 192-value
square root table `SQRT` in `platform/native_gte_sqrt_tbl.h`, which is
compiled into `ctr_native.exe`, came from `libpsn00b`
(`libpsn00b/psxgte/squareroot.s`) via REDRIVER2 and PsyCross (see
[PsyCross / Psy-X](#psycross--psy-x)). It remains under the MPL 2.0; its
source code is in this repository and in the source code archive of every
release.

PSn00bSDK is not marked "Incompatible With Secondary Licenses", so section 3.3
of the MPL 2.0 permits combining it with this GPL-3.0 program; the files
themselves stay under the MPL 2.0.

`mkpsxiso` and `dumpsxiso`, which PSn00bSDK ships under GPLv2 or later, are
not part of this project.

## Russo One (README banner)

The lettering in `docs/assets/banner-dark.svg` and `banner-light.svg` is the
font Russo One, converted to outlines. The font itself is not included.

Copyright (c) 2011-2012, Jovanny Lemonad (jovanny.ru), with Reserved Font Name
"Russo". License: SIL Open Font License, Version 1.1
<https://openfontlicense.org>

## SDL3

Source: <https://github.com/libsdl-org/SDL>

Path: `externals/SDL`

Vendored version: 3.4.10 (`release-3.4.10`)

SDL3 provides windowing, input, timing and audio for `ctr_native.exe`, finds
the Vulkan loader, and draws the first-start screen with its 2D renderer and
file dialog. It is built as a static library and linked only into
`ctr_native.exe`; no `SDL3.dll` is built, shipped or needed. `alphamaker.exe`
contains no SDL code.

License: zlib

Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.

Further copyright holders of SDL files compiled into the program, under the
same zlib license: Valve Corporation, Mark Callow, Andreas Schiffler
(`src/video/SDL_rotate.c`, from SDL_gfx), Gareth McCaughan (qsort,
`src/stdlib/SDL_qsort.c`), Collabora Ltd., Katharine Chui, Simon Wood,
Michal Malý, Bernat Arlandis, Mitchell Cairns, Max Maisel and Zuiki Inc.

Some parts of SDL are in the public domain or under CC0: `strtok_r` and
`bsearch` from PDCLib, the CRC-16 and CRC-32 code, MurmurHash3, Howard
Hinnant's date algorithms and the IBM VGA debug font.

### Components compiled into SDL3

SDL3 is linked statically into `ctr_native.exe`. It carries these components,
whose notices apply to the executable as well.

#### HIDAPI

Path: `externals/SDL/src/hidapi` (HIDAPI 0.14.0)

Copyright Alan Ott, Signal 11 Software, and the libusb/hidapi Team.

HIDAPI is offered under GPLv3, a BSD-style license or the original HIDAPI
license (see `LICENSE.txt` there); CTR Reload Edition uses it under the GNU
General Public License version 3, like the rest of the program.

#### Wine DirectInput data format

Path: `externals/SDL/src/joystick/windows/SDL_dinputjoystick.c` (the
`dfDIJoystick2` table)

SDL's DirectInput joystick driver contains the `dfDIJoystick2` data format
table, marked "Taken from Wine - Thanks!". It comes from Wine
(<https://gitlab.winehq.org/wine/wine>, `dlls/dinput/data_formats.c`).

Copyright (c) 2004 Robert Reif

License: GNU Lesser General Public License, version 2.1 or (at your option)
any later version. CTR Reload Edition uses it under the GNU General Public
License version 3, as section 3 of the LGPL 2.1 permits; the full text is in
`LICENSE`.

#### stb_image

Path: `externals/SDL/src/video/stb_image.h` (stb_image 2.30), by Sean Barrett

stb_image is offered as public domain (Unlicense) or under the MIT license, at
the user's choice. CTR Reload Edition uses it under the MIT alternative:

```
Copyright (c) 2017 Sean Barrett
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

The JPEG decoder of stb_image contains an integer IDCT derived from the
Independent JPEG Group's software: This software is based in part on the work
of the Independent JPEG Group.

#### miniz

Path: `externals/SDL/src/video/miniz.h` (miniz 1.15), by Rich Geldreich

License: public domain (Unlicense). Its PNG writer is by Alex Evans and was
released into the public domain.

#### yuv2rgb

Path: `externals/SDL/src/video/yuv2rgb`, from
<https://github.com/descampsa/yuv2rgb>

License: BSD-3-Clause

```
Copyright (c) 2016, Adrien Descamps
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of yuv2rgb nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

#### Unicode case-folding data

`externals/SDL/src/stdlib/SDL_casefolding.h` is generated from the Unicode
Character Database file `CaseFolding-15.1.0.txt` (kept as
`externals/SDL/build-scripts/casefolding.txt`, "© 2023 Unicode®, Inc."), and
SDL's string functions use it.

License: Unicode License v3 (Unicode-3.0)

```
UNICODE LICENSE V3

COPYRIGHT AND PERMISSION NOTICE

Copyright © 1991-2023 Unicode, Inc.

NOTICE TO USER: Carefully read the following legal agreement. BY
DOWNLOADING, INSTALLING, COPYING OR OTHERWISE USING DATA FILES, AND/OR
SOFTWARE, YOU UNEQUIVOCALLY ACCEPT, AND AGREE TO BE BOUND BY, ALL OF THE
TERMS AND CONDITIONS OF THIS AGREEMENT. IF YOU DO NOT AGREE, DO NOT
DOWNLOAD, INSTALL, COPY, DISTRIBUTE OR USE THE DATA FILES OR SOFTWARE.

Permission is hereby granted, free of charge, to any person obtaining a
copy of data files and any associated documentation (the "Data Files") or
software and any associated documentation (the "Software") to deal in the
Data Files or Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, and/or sell
copies of the Data Files or Software, and to permit persons to whom the
Data Files or Software are furnished to do so, provided that either (a)
this copyright and permission notice appear with all copies of the Data
Files or Software, or (b) this copyright and permission notice appear in
associated Documentation.

THE DATA FILES AND SOFTWARE ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
THIRD PARTY RIGHTS.

IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS INCLUDED IN THIS NOTICE
BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES,
OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THE DATA
FILES OR SOFTWARE.

Except as contained in this notice, the name of a copyright holder shall
not be used in advertising or otherwise to promote the sale, use or other
dealings in these Data Files or Software without prior written
authorization of the copyright holder.
```

#### Khronos Vulkan headers

Path: `externals/SDL/src/video/khronos/vulkan` and
`externals/SDL/src/video/khronos/vk_video`

Copyright 2014-2024 The Khronos Group Inc.

License: Apache License, Version 2.0 (`SPDX-License-Identifier: Apache-2.0`);
the full text is in [Apache License, Version 2.0](#apache-license-version-20)
at the end of this file.

SDL's Vulkan renderer is compiled against these headers. The game's own
renderer is compiled against the headers of the Vulkan SDK 1.3.296.0, which
are under the same license. The headers contribute constant values and
extension names to the program, no functions.

#### Microsoft DirectX headers

Path: `externals/SDL/src/video/directx/d3d12.h` and `d3d12sdklayers.h`
(their headers say "Copyright (c) Microsoft Corporation" and "Licensed under
the MIT license")

Two GUID constants from these headers reach the program. License, from
<https://github.com/microsoft/DirectX-Headers>:

```
Copyright (c) Microsoft Corporation.

MIT License

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED *AS IS*, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

#### OpenGL headers

`externals/SDL/include/SDL3/SDL_opengl.h` (Copyright (C) 1999-2006 Brian
Paul, Copyright (C) 2009 VMware, Inc., MIT license, from Mesa) and
`SDL_opengl_glext.h` (Copyright 2013-2020 The Khronos Group Inc., MIT
license; its embedded `khrplatform.h` part: Copyright (c) 2008-2018 The
Khronos Group Inc., MIT license): only macro values from these headers
reach the program. Their license texts, as the headers carry them:

`SDL_opengl.h`:

```
Copyright (C) 1999-2006  Brian Paul   All Rights Reserved.
Copyright (C) 2009  VMware, Inc.  All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.
```

`SDL_opengl_glext.h` (the `khrplatform.h` part):

```
Copyright (c) 2008-2018 The Khronos Group Inc.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and/or associated documentation files (the
"Materials"), to deal in the Materials without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Materials, and to
permit persons to whom the Materials are furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Materials.

THE MATERIALS ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
MATERIALS OR THE USE OR OTHER DEALINGS IN THE MATERIALS.
```

## Sunset Vista compatibility data (Tramadoll)

Path: `game/native_trackmod.c` (compiled into `ctr_native.exe`)

Sunset Vista is a custom Crash Team Racing track by Tramadoll; the track
itself is not included in this repository or in the release downloads.
`game/native_trackmod.c` is a compatibility layer that lets the track run in
CTR Reload Edition. Its actor behavior (moving wall, platforms, bats, fire
bowls, crates) follows Tramadoll's Sunset Vista source and integration notes,
and it contains data taken from them (positions, timings and collision face
indices).

Sunset Vista compatibility data used with permission of Tramadoll.

## Origin of game data, texts and decompiled tables

The data tables taken from the retail game, the game texts and the decompiled
tables in this repository were not extracted by CTR Reload Edition. They come
from the upstream decompilation projects and are used under the GNU General
Public License, version 3:

- CTR-ModSDK <https://github.com/CTR-tools/CTR-ModSDK>, the Crash Team Racing
  decompilation. GPL-3.0 since commit `530a072e1c26` (2026-06-25).
- ctr-native <https://github.com/CTR-tools/ctr-native>, the native PC port
  built on CTR-ModSDK (its initial import is a copy of CTR-ModSDK
  `decompile/General` and `include`). GPL-3.0 since commit `12e2888c05db`
  (2026-06-25), which adopts the CTR-ModSDK license.

Some of these files were reformatted or trimmed here (for example, disabled
`#if 0` blocks were removed). The values themselves match the upstream files.

Retail data tables (executable and overlay data sections):
`game/zGlobal_DATA.c`, `game/zGlobal_SDATA.c`, `game/zGlobal_RDATA.c`,
`game/221.c`, `game/222.c`, `game/224.c`, `game/225.c`, `game/226/R226.c`,
`game/227/R227.c`, `game/228/R228.c`, `game/229/R229.c`, `game/230/R230.c`,
`game/230/D230.c`, `game/231/R231.c`, `game/231/D231.c`, `game/232/R232.c`,
`game/232/D232.c`, `game/233/R233.c`, `game/233/D233.c`,
`game/231/RB_FlameJet.c`, `game/231/RB_Plant.c`, `game/231/RB_Bubbles.c`,
`game/231/RB_Fireball.c`, `game/231/RB_Orca.c`, `game/UI/UI_Speedometer.c`.

Retail metadata (disc layout, symbol addresses, file sizes and hashes):
`metadata/retail/ntsc-u-926/disc.json`,
`metadata/retail/ntsc-u-926/symbols/syms926.txt`,
`metadata/retail/ntsc-u-926/matching.json`.

Game texts: `include/namespace_Lng.h` (English string table as comments),
`include/regionsEXE.h` (commented end-of-race comment table).

Decompiled tables: `game/DrawTires.c`, `game/DrawConfetti.c`,
`game/RenderBucket/RenderBucket_QueueExecute.c`,
`game/RenderLevel/RenderLists.c`, `game/226/226_00_DrawLevelOvr1P.c`,
`game/231/RB_TNT.c`, `game/231/RB_Explosion.c`,
`game/231/RB_MaskShieldCloud.c`, `game/231/RB_Warpball.c`,
`game/231/RB_Minecart.c`, `game/231/RB_Spider.c`, `game/233/CS_ScriptCmd.c`,
`game/Vehicle/VehGroundShadow.c`, `game/UI/UI_VsQuip.c`.

The comments in `game/MEMCARD/MEMCARD_Card.c` mark two parts as Psy-Q sample
code that the original developers copied into the game; see
[Psy-Q SDK material](#psy-q-sdk-material).

The PS1 hardware tables in `platform/native_audio.c`, `platform/native_str.c`
and `platform/native_gte_core.c` also come from ctr-native. The tables in
`platform/native_audio.c` and `platform/native_str.c` (SPU interpolation and
reverb, CD-XA resampling, MDEC) are transcribed from the psx-spx hardware
documentation <https://psx-spx.consoledev.net/>.

## Vulkan loader, shaders and C runtime

The Vulkan loader (`vulkan-1.dll`) is not part of this program; the graphics
driver installs it, and the program loads it at run time. The shaders are
compiled to SPIR-V at build time with glslangValidator, which adds no code of
its own. The Microsoft C runtime (libcmt, libvcruntime, and the Universal CRT
libucrt) is linked statically into both programs under the terms of the
Microsoft Visual Studio and Windows SDK licenses. It is a system library in
the sense of section 1 of the GNU General Public License version 3. Apart from
the Vulkan loader, the programs import only DLLs that are part of Windows.

## Apache License, Version 2.0

The Khronos Vulkan headers (see
[Khronos Vulkan headers](#khronos-vulkan-headers)) are licensed under the
Apache License, Version 2.0. Its full text, from
<https://www.apache.org/licenses/LICENSE-2.0.txt>:

```

                                 Apache License
                           Version 2.0, January 2004
                        http://www.apache.org/licenses/

   TERMS AND CONDITIONS FOR USE, REPRODUCTION, AND DISTRIBUTION

   1. Definitions.

      "License" shall mean the terms and conditions for use, reproduction,
      and distribution as defined by Sections 1 through 9 of this document.

      "Licensor" shall mean the copyright owner or entity authorized by
      the copyright owner that is granting the License.

      "Legal Entity" shall mean the union of the acting entity and all
      other entities that control, are controlled by, or are under common
      control with that entity. For the purposes of this definition,
      "control" means (i) the power, direct or indirect, to cause the
      direction or management of such entity, whether by contract or
      otherwise, or (ii) ownership of fifty percent (50%) or more of the
      outstanding shares, or (iii) beneficial ownership of such entity.

      "You" (or "Your") shall mean an individual or Legal Entity
      exercising permissions granted by this License.

      "Source" form shall mean the preferred form for making modifications,
      including but not limited to software source code, documentation
      source, and configuration files.

      "Object" form shall mean any form resulting from mechanical
      transformation or translation of a Source form, including but
      not limited to compiled object code, generated documentation,
      and conversions to other media types.

      "Work" shall mean the work of authorship, whether in Source or
      Object form, made available under the License, as indicated by a
      copyright notice that is included in or attached to the work
      (an example is provided in the Appendix below).

      "Derivative Works" shall mean any work, whether in Source or Object
      form, that is based on (or derived from) the Work and for which the
      editorial revisions, annotations, elaborations, or other modifications
      represent, as a whole, an original work of authorship. For the purposes
      of this License, Derivative Works shall not include works that remain
      separable from, or merely link (or bind by name) to the interfaces of,
      the Work and Derivative Works thereof.

      "Contribution" shall mean any work of authorship, including
      the original version of the Work and any modifications or additions
      to that Work or Derivative Works thereof, that is intentionally
      submitted to Licensor for inclusion in the Work by the copyright owner
      or by an individual or Legal Entity authorized to submit on behalf of
      the copyright owner. For the purposes of this definition, "submitted"
      means any form of electronic, verbal, or written communication sent
      to the Licensor or its representatives, including but not limited to
      communication on electronic mailing lists, source code control systems,
      and issue tracking systems that are managed by, or on behalf of, the
      Licensor for the purpose of discussing and improving the Work, but
      excluding communication that is conspicuously marked or otherwise
      designated in writing by the copyright owner as "Not a Contribution."

      "Contributor" shall mean Licensor and any individual or Legal Entity
      on behalf of whom a Contribution has been received by Licensor and
      subsequently incorporated within the Work.

   2. Grant of Copyright License. Subject to the terms and conditions of
      this License, each Contributor hereby grants to You a perpetual,
      worldwide, non-exclusive, no-charge, royalty-free, irrevocable
      copyright license to reproduce, prepare Derivative Works of,
      publicly display, publicly perform, sublicense, and distribute the
      Work and such Derivative Works in Source or Object form.

   3. Grant of Patent License. Subject to the terms and conditions of
      this License, each Contributor hereby grants to You a perpetual,
      worldwide, non-exclusive, no-charge, royalty-free, irrevocable
      (except as stated in this section) patent license to make, have made,
      use, offer to sell, sell, import, and otherwise transfer the Work,
      where such license applies only to those patent claims licensable
      by such Contributor that are necessarily infringed by their
      Contribution(s) alone or by combination of their Contribution(s)
      with the Work to which such Contribution(s) was submitted. If You
      institute patent litigation against any entity (including a
      cross-claim or counterclaim in a lawsuit) alleging that the Work
      or a Contribution incorporated within the Work constitutes direct
      or contributory patent infringement, then any patent licenses
      granted to You under this License for that Work shall terminate
      as of the date such litigation is filed.

   4. Redistribution. You may reproduce and distribute copies of the
      Work or Derivative Works thereof in any medium, with or without
      modifications, and in Source or Object form, provided that You
      meet the following conditions:

      (a) You must give any other recipients of the Work or
          Derivative Works a copy of this License; and

      (b) You must cause any modified files to carry prominent notices
          stating that You changed the files; and

      (c) You must retain, in the Source form of any Derivative Works
          that You distribute, all copyright, patent, trademark, and
          attribution notices from the Source form of the Work,
          excluding those notices that do not pertain to any part of
          the Derivative Works; and

      (d) If the Work includes a "NOTICE" text file as part of its
          distribution, then any Derivative Works that You distribute must
          include a readable copy of the attribution notices contained
          within such NOTICE file, excluding those notices that do not
          pertain to any part of the Derivative Works, in at least one
          of the following places: within a NOTICE text file distributed
          as part of the Derivative Works; within the Source form or
          documentation, if provided along with the Derivative Works; or,
          within a display generated by the Derivative Works, if and
          wherever such third-party notices normally appear. The contents
          of the NOTICE file are for informational purposes only and
          do not modify the License. You may add Your own attribution
          notices within Derivative Works that You distribute, alongside
          or as an addendum to the NOTICE text from the Work, provided
          that such additional attribution notices cannot be construed
          as modifying the License.

      You may add Your own copyright statement to Your modifications and
      may provide additional or different license terms and conditions
      for use, reproduction, or distribution of Your modifications, or
      for any such Derivative Works as a whole, provided Your use,
      reproduction, and distribution of the Work otherwise complies with
      the conditions stated in this License.

   5. Submission of Contributions. Unless You explicitly state otherwise,
      any Contribution intentionally submitted for inclusion in the Work
      by You to the Licensor shall be under the terms and conditions of
      this License, without any additional terms or conditions.
      Notwithstanding the above, nothing herein shall supersede or modify
      the terms of any separate license agreement you may have executed
      with Licensor regarding such Contributions.

   6. Trademarks. This License does not grant permission to use the trade
      names, trademarks, service marks, or product names of the Licensor,
      except as required for reasonable and customary use in describing the
      origin of the Work and reproducing the content of the NOTICE file.

   7. Disclaimer of Warranty. Unless required by applicable law or
      agreed to in writing, Licensor provides the Work (and each
      Contributor provides its Contributions) on an "AS IS" BASIS,
      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
      implied, including, without limitation, any warranties or conditions
      of TITLE, NON-INFRINGEMENT, MERCHANTABILITY, or FITNESS FOR A
      PARTICULAR PURPOSE. You are solely responsible for determining the
      appropriateness of using or redistributing the Work and assume any
      risks associated with Your exercise of permissions under this License.

   8. Limitation of Liability. In no event and under no legal theory,
      whether in tort (including negligence), contract, or otherwise,
      unless required by applicable law (such as deliberate and grossly
      negligent acts) or agreed to in writing, shall any Contributor be
      liable to You for damages, including any direct, indirect, special,
      incidental, or consequential damages of any character arising as a
      result of this License or out of the use or inability to use the
      Work (including but not limited to damages for loss of goodwill,
      work stoppage, computer failure or malfunction, or any and all
      other commercial damages or losses), even if such Contributor
      has been advised of the possibility of such damages.

   9. Accepting Warranty or Additional Liability. While redistributing
      the Work or Derivative Works thereof, You may choose to offer,
      and charge a fee for, acceptance of support, warranty, indemnity,
      or other liability obligations and/or rights consistent with this
      License. However, in accepting such obligations, You may act only
      on Your own behalf and on Your sole responsibility, not on behalf
      of any other Contributor, and only if You agree to indemnify,
      defend, and hold each Contributor harmless for any liability
      incurred by, or claims asserted against, such Contributor by reason
      of your accepting any such warranty or additional liability.

   END OF TERMS AND CONDITIONS

   APPENDIX: How to apply the Apache License to your work.

      To apply the Apache License to your work, attach the following
      boilerplate notice, with the fields enclosed by brackets "[]"
      replaced with your own identifying information. (Don't include
      the brackets!)  The text should be enclosed in the appropriate
      comment syntax for the file format. We also recommend that a
      file or class name and description of purpose be included on the
      same "printed page" as the copyright notice for easier
      identification within third-party archives.

   Copyright [yyyy] [name of copyright owner]

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
```
