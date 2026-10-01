# Container format

CTR Reload Edition has two container types. Both share one envelope:

| Extension | Content | Written by | Read by |
|---|---|---|---|
| `.rldtrack` | a custom track, format 4.1 (frozen) | `rldpack build`, `rldpack make` | the game, `rldpack info`, `rldpack verify` |
| `.rldchar` | a custom character, format 1.0 (draft) | `rldpack make-char` (also through Reload Studio's Character page) | the game (`characters` folder), `rldpack info`, `rldpack verify` |

The game reads every `.rldchar` of its `characters` folder at start; see
"What the game does" in section 3.

All numbers are little-endian. KiB = 1024 bytes, MiB = 1024 KiB.

## 1. The envelope

Source: `Rld_OpenAs`, `Rld_ReadChunk` and `Rld_WriteEnvelope` in `include/rldtrack.inc`.

A container is one file: a 40-byte header, the chunk data, and a directory of
64-byte entries.

### Header (40 bytes)

| Offset | Type | Field | Meaning |
|---|---|---|---|
| 0x00 | char[8] | magic | `RLDTRACK`, or `RLDCHAR` and a NUL; compared as 8 bytes |
| 0x08 | u16 | major | content major of the magic |
| 0x0A | u16 | minor | never a reason to refuse |
| 0x0C | u32 | flags | required features, see below |
| 0x10 | u32 | chunk_count | number of directory entries |
| 0x14 | u32 | reserved | must be 0 |
| 0x18 | u64 | dir_offset | file offset of the directory |
| 0x20 | u64 | file_size | must equal the real file size |

Header field 0x08 is the content major of each magic; the envelope itself never changes.

### Directory entry (64 bytes)

| Offset | Type | Field | Meaning |
|---|---|---|---|
| 0x00 | char[4] | type | chunk name, e.g. `META` |
| 0x04 | u32 | flags | bits 0-3: compression method, 0 for every known chunk; bits 4-31 are not checked; the packer writes 0 for the whole field |
| 0x08 | u64 | offset | file offset of the chunk data |
| 0x10 | u64 | size_stored | bytes in the file |
| 0x18 | u64 | size_raw | must equal size_stored for a known chunk |
| 0x20 | u8[32] | sha256 | SHA-256 of the chunk bytes as they are in the file |

### Per-type values (`struct RldFormat`)

| | `.rldtrack` (`s_rldTrackFormat`) | `.rldchar` (`s_rldCharFormat`) |
|---|---|---|
| magic | `RLDTRACK` | `RLDCHAR` + NUL |
| major read and written | 4 | 1 |
| minor written | 1 (frozen) | 0 (draft) |
| known required-feature bits | none | none |
| chunk_count | 3..64 | 2..64 |
| required chunks | META, LEVD, VRMD | CHRI, CMDL |
| known chunks and size limits | META 64 KiB, LEVD 16 MiB, VRMD 4 MiB, SNDB 4 MiB, PARM 4 KiB (`Rld_ChunkLimit`) | CHRI 4 KiB, CMDL 256 KiB, CICN 4 KiB, CPRM 4 KiB (`RldChar_ChunkLimit`) |

Each type has its own table of known chunks: a CMDL inside a `.rldtrack` and a
META inside a `.rldchar` are unknown chunks and are skipped.

### Integrity

- Every chunk carries the SHA-256 of its bytes in its directory entry.
- The hash is checked whenever a chunk is fetched (`Rld_ReadChunk`), not when
  the file is opened.
- The hash proves that a chunk arrived as it was packed, not that it was packed
  sensibly: there is no key and no signature. The content checks below exist
  for that reason.

### Required features (header flags)

- A set bit means: without this feature the content is wrong.
- A reader refuses every bit it does not know (NEEDS NEWER).
- No bit is assigned in either format; the packers write 0.

### Compression

Compression is not part of the format. A known chunk must carry method 0 in
directory flags bits 0-3. For an unknown chunk the value is not looked at.

### Reader rules, in order (`Rld_OpenAs`)

Kinds: NEEDS NEWER = `RLD_REFUSAL_NEWER`, OLD FORMAT = `RLD_REFUSAL_OLDER`,
DAMAGED = `RLD_REFUSAL_DAMAGED`. The texts are fixed.

| # | Check | Kind | Text |
|---|---|---|---|
| 1 | the file opens | DAMAGED | `cannot be opened` |
| 2 | its size can be measured | DAMAGED | `cannot be measured` |
| 3 | file size >= 40 | DAMAGED | `shorter than a header` |
| 4 | the header can be read | DAMAGED | `the header cannot be read` |
| 5 | the 8 magic bytes match the type | DAMAGED | `no RLDTRACK magic - the extension says nothing, the magic does` (`RLDCHAR` for a character) |
| 6 | major > the type's major | NEEDS NEWER | `needs a newer version of CTR Reload - the container format is newer than this build` |
| 7 | major < the type's major | OLD FORMAT | `old format - pack it again with Reload Studio` |
| 8 | no flag bit outside the known bits | NEEDS NEWER | `needs a newer version of CTR Reload - the container needs a feature this build does not know` |
| 9 | reserved word 0x14 is 0 | DAMAGED | `a reserved header field is not zero` |
| 10 | file_size equals the real size | DAMAGED | `file_size does not match the real size - truncated download` |
| 11 | chunk_count inside the type's range | DAMAGED | `chunk_count is outside 3..64` (`2..64` for a character) |
| 12 | dir_offset >= 40, dir_offset <= file size, chunk_count x 64 bytes fit behind it | DAMAGED | `the directory does not lie inside the file` |
| 13 | the directory can be read | DAMAGED | `the directory cannot be read` |
| 14 | every entry: offset >= 40 and the chunk ends at or before dir_offset | DAMAGED | `a chunk lies outside the data area` |
| 15 | known type: compression method 0 | DAMAGED | `a chunk is compressed - compression is not part of the format` |
| 16 | known type: size_stored == size_raw | DAMAGED | `size_stored and size_raw of a chunk differ` |
| 17 | known type: size_raw <= the type's limit | DAMAGED | `a chunk is larger than the format allows for its type` |
| 18 | every entry: no chunk type twice | DAMAGED | `a chunk type appears twice` |
| 19 | every entry: no two non-empty chunks overlap | DAMAGED | `two chunks overlap` |
| 20 | every required chunk is present | DAMAGED | `<TYPE> is missing - required chunk` |

- Steps 14 to 19 run per directory entry, in directory order. Steps 18 and 19
  compare the entry with each earlier entry in turn: 18, then 19, for the first
  earlier entry, then for the next.
- The minor version is read and named, never checked.
- The reader demands no chunk order.
- Gaps between chunks and bytes after the directory are not refused; the packer
  writes none.

### Fetching a chunk (`Rld_ReadChunk`)

| Check | Text |
|---|---|
| the type is known | `not a chunk type of this format - skipped, not read` |
| compression method 0 | `the chunk is compressed - compression is not part of the format` |
| size_stored == size_raw | `size_stored and size_raw of the chunk differ` |
| size_raw <= limit (again, before memory is allocated) | `the chunk is larger than the format allows for its type` |
| memory for the chunk can be allocated | `out of memory` |
| the bytes can be read | `the chunk cannot be read - the file is shorter than its own directory says` |
| SHA-256 matches the directory | `hash mismatch - the chunk is altered or damaged` |

### Unknown chunks

An unknown chunk must pass steps 14, 18 and 19. Nothing else about it is
checked, and nobody reads it. An older reader therefore skips a newer chunk
instead of refusing the file.

### Writing (`Rld_WriteEnvelope`)

1. Header.
2. The chunks from offset 40, one after the other, in container order.
3. The directory, in the same order, at the end of the file:
   file_size = dir_offset + chunk_count x 64.

## 2. `.rldtrack` 4.1

Magic `RLDTRACK`, major 4, minor 1. A reader takes 4.0 and 4.1 alike.

| Chunk | Required | Limit | Content |
|---|---|---|---|
| META | yes | 64 KiB | name, author, version, modes, memory numbers |
| LEVD | yes | 16 MiB | the track's LEV file |
| VRMD | yes | 4 MiB | the track's VRM file (textures) |
| SNDB | no | 4 MiB | the track's own sound banks and sequences |
| PARM | no | 4 KiB | reverb, bot strength, ambient sound |

`rldpack` writes them in this order and only writes PARM when a value is set.

### META (`struct RldMeta`, `Rld_ParseMeta`)

| Offset | Type | Field | Meaning |
|---|---|---|---|
| 0x00 | u32 | metaVersion | 1 = frozen (written), 0 = draft; the reader does not check it |
| 0x04 | 32 bytes | reserved | written as 0, not read |
| 0x24 | u32 | trackVersion | for humans; the game tells versions apart by the LEVD hash |
| 0x28 | u32 | modes | mode bits, below |
| 0x2C | u32 | primBytes | draw memory per draw buffer, without the sky |
| 0x30 | u32 | memTotal | LEV size + 2 x (primBytes + sky bytes) |
| 0x34 | u32 | stringCount | number of strings |
| 0x38 | | strings | per string: u32 length, then the bytes; 0 = track name, 1 = author |

- Checks: META >= 0x38 bytes, every string inside META,
  primBytes <= `RLD_META_PRIM_MAX` (37,931,088), memTotal <= `RLD_META_TOTAL_MAX` (107,319,232).
- The reader keeps the first two strings, at most 512 bytes each, and ignores further strings.
- `rldpack` writes two strings of at most 64 bytes each, UTF-8.
- primBytes and memTotal are binding. `rldpack` computes them with `Rld_MemNeed`.
  The game computes them again from LEVD at load and refuses a track whose META
  declares less (MEMORY INFO).

Mode bits (`RLD_MODE_*`):

| Bit | Mode | State |
|---|---|---|
| 0x1 | Race | playable |
| 0x2 | CTR Challenge | playable |
| 0x4 | Time Trial | can be declared, not playable yet |
| 0x8 | Crystal Challenge | playable |
| 0x10 | Battle | reserved, `rldpack` refuses it |

The game offers a mode only when the track declares it and the game supports it
for containers (`RLD_MODES_PLAYABLE`). It ignores unknown bits.

### LEVD

The LEV file as the game loads it:

- u32 ptrMapOffset (body size)
- the body: `struct Level` and everything it points to
- the pointer map: u32 numBytes, then numBytes / 4 u32 body offsets

The game checks it at load, before the load path sees it:

`Rld_CheckLev` (`include/rldtrack.inc`):

- LEVD >= 4 + 0x1F4 bytes; body size >= 0x1F4 (`struct Level`).
- The pointer map lies inside LEVD; numBytes is a multiple of 4.
- Every map entry:
  - is below 0x80000000;
  - rounded down to a multiple of 4, it lies inside the body (at most body size - 4);
  - the word it names is <= the body size;
  - no place is listed twice.
- Every pointer field of `struct Level` (`s_rldLevelPointerFields`) is 0 or listed in the map.
- levTexLookup (0x3C), when set: the icon table, its icons and icon groups lie
  in the body; no negative count.
- Models (0x14 count, 0x18 table, up to the count or the first NULL) and the
  models of the instances (0x0C count, 0x10 table, 0x40 bytes each, model at +0x10):
  - every model is a mapped pointer;
  - its id (s16 at +0x10) is -1 or 0..0xE2.
- Spawn table (0x134):
  - a mapped pointer; count 0..16; the table lies in the body; every slot a pointer or NULL;
  - slot 2 set at count >= 3, slot 3 set at count >= 4.

`NativeTrack_CheckLevRace` (`platform/native_assets.c`): restart points, the
checkpoint index of every quadblock (0xFF or an existing restart point),
end-of-race cameras and the map table (spawn slot 0).

### VRMD (`Rld_CheckVrm`)

The VRM file as the game loads it:

- Word 0 is not 0x20: a single TIM.
- Word 0 is 0x20: a chain from offset 4. Each link is a u32 size and a block of
  (size rounded down to a multiple of 4) bytes. A size of 0 ends the chain.

Checks:

- VRMD is not empty and has at least 4 bytes.
- Every block lies in VRMD; the closing 0 lies in VRMD.
- Every TIM header has at least 0x14 bytes.
- Width and height (u16 at +0x10, +0x12) are below 0x8000.
- width x height x 2 <= block size - 0x14.

### SNDB (`Rld_ParseSndb`, `Rld_CheckSndbBanks`)

Header, 20 bytes:

| Offset | Type | Field |
|---|---|---|
| 0x00 | char[4] | `SNDB` |
| 0x04 | u16 | version = 2 |
| 0x06 | u16 | entryCount |
| 0x08 | u16 | spuFixupCount |
| 0x0A | u16 | reserved |
| 0x0C | u32 | payloadOffset |
| 0x10 | u16 | playBank: the bank the track plays |
| 0x12 | u16 | playSong: the sequence the track plays |

Then entryCount entries of 12 bytes, spuFixupCount rows of 8 bytes, and the
payload from payloadOffset.

| Entry offset | Type | Field |
|---|---|---|
| 0x00 | u8 | kind: 0 bank, 1 sequence |
| 0x01 | u8 | reserved |
| 0x02 | u16 | index |
| 0x04 | u32 | offset, counted from payloadOffset |
| 0x08 | u32 | size |

| SPU row offset | Type | Field |
|---|---|---|
| 0x00 | u16 | index |
| 0x02 | u16 | spuAddr |
| 0x04 | u16 | spuSize |
| 0x06 | u16 | reserved |

`Rld_ParseSndb`:

- at least 20 bytes; starts with the magic `SNDB`.
- version 2.
- entryCount 1..104; spuFixupCount <= 528.
- payloadOffset lies behind both tables and not past the end of the chunk.
- Every entry:
  - kind 0 or 1;
  - a bank < 71, a sequence < 33;
  - size a non-zero multiple of 2048, offset a multiple of 2048, inside the payload;
  - no bank or sequence twice, no two entries overlap.
- playBank < 71 and playSong < 33, and both are carried in the chunk.
- Every SPU row index < 528.

`Rld_CheckSndbBanks`, per bank entry:

- numSamples (first u16 of the bank) <= 1023.
- Every SPU row it names is inside the host table.
- 1 + ceil(sum of spuSize x 8 / 2048) sectors fit the entry. spuSize is the
  SNDB correction of the row, else the host's value; without the host table an
  uncorrected row counts 0.

### PARM (`Rld_ParseParm`)

| Offset | Type | Field |
|---|---|---|
| 0x00 | u16 | version = 1 |
| 0x02 | u16 | count, at most 1024 |
| 0x04 | | count x { u16 key, u16 length, length bytes of value } |

| Key | Name | Value | Valid |
|---|---|---|---|
| 1 | reverb | u8 | 0..4, or 0xFF = off |
| 2 | bots | u8 | 0..17, a row of the game's arcade difficulty table |
| 3 | ambient | 2 x u16 | ambient sound numbers, 0 = none |

- An unknown key is skipped.
- A known key with the wrong length or out of range is invalid; its default applies.
- The whole chunk falls back to the defaults when:
  - it is shorter than 4 bytes or longer than 4 KiB;
  - the version is not 1, or count is above 1024;
  - an entry runs past the end, or a key appears twice;
  - bytes follow the last entry.
- The defaults belong to the game, not to the format.

### What the game does

| When | Reads | Refusal |
|---|---|---|
| track list | header, directory, META (hash and `Rld_ParseMeta`); LEVD and VRMD are not touched; the LEVD hash in the directory is the version stamp | NEEDS NEWER, OLD FORMAT or DAMAGED; the track stays listed in gray with the reason |
| load | META; LEVD (hash, `Rld_CheckLev`, race tables); VRMD (hash, `Rld_CheckVrm`); the memory numbers; then SNDB and PARM | DAMAGED; MEMORY INFO when META understates the memory need |

| Optional chunk | Unreadable, or refused by its parser | Other |
|---|---|---|
| SNDB | the track loads without its own sound | `Rld_CheckSndbBanks` refuses: the track is refused (DAMAGED) |
| PARM | the defaults apply | invalid single key: its default applies |

## 3. `.rldchar` 1.0 (draft)

Magic `RLDCHAR` and a NUL, major 1, minor 0. Minor 0 means draft until the format
is frozen. Source: `include/rldchar.inc`.

| Chunk | Status | Limit | Content |
|---|---|---|---|
| CHRI | required | 4 KiB | template, class, name, version, flags, author |
| CMDL | required | 256 KiB | the model, in the game's native model format |
| CICN | optional, written by `rldpack make-char --icon` | 4 KiB | the portrait, exactly 612 bytes (rules CICN-1..3); the game does not show it yet |
| CPRM | optional, known type: not written by rldpack, content not interpreted yet | 4 KiB | values; layout not fixed (draft) |
| CVOI | reserved name, planned | none (unknown, skipped) | voices; layout not defined |
| CTEX | reserved name, planned | none (unknown, skipped) | texture data; layout not defined |

`rldpack make-char` writes CHRI, CMDL and, with `--icon`, CICN, in this order.
CICN and CPRM are known types. When present, the envelope checks them
(compression, sizes, limit) and `rldpack verify` checks their hash. A CICN
that breaks an envelope rule (steps 14-19, e.g. larger than 4 KiB) refuses the
whole file; a CICN that breaks CICN-1..3 only loses the portrait (see CICN
below). Nothing interprets CPRM yet.

### CHRI (`RldChar_ParseInfo`)

| Offset | Type | Field | Meaning |
|---|---|---|---|
| 0x00 | u16 | fixedSize | length of the numeric part: >= 0x1C, a multiple of 4 |
| 0x02 | u8 | template | the retail character 0..14 the character runs on in the game; Reload Studio always writes 14 (Fake Crash) |
| 0x03 | u8 | class | engine class 0..3: balanced, acceleration, speed, turning; the game drives the character with it |
| 0x04 | char[20] | name | display name, see the name rule |
| 0x18 | u32 | charVersion | for humans; the CMDL hash, not this number, is meant to tell versions apart |
| 0x1C | u32 | flags | only when fixedSize >= 0x20, else 0; bit 0 (`RLDCHAR_FLAG_NO_WHEELS`): the game draws no kart wheels for this driver (tyre dust and skid marks stay: they are effects at the wheel points, not wheels); further bits are reserved |
| fixedSize | u32 | stringCount | then per string: u32 length, bytes |
| | | string 0 | author, at most 64 bytes; "" when absent |

- `rldpack` writes fixedSize 0x1C and one string, the author (UTF-8; the packer
  checks it). Only when flags is not 0 (`rldpack make-char --wheels off`) does it
  write fixedSize 0x20 with flags at 0x1C, so a file without flags has the same
  bytes as before the field existed.
- A reader skips numeric bytes beyond the fields it knows and strings beyond the
  first. A reader from before flags skips it and draws the wheels.
- Flag bits a reader does not know are ignored: they are no finding and never
  refuse the file.
- `rldpack make-char --template` sets the template, `--class` the class
  (default: the template's own class). Reload Studio asks for no template: it
  always passes `--template 14` and the driving style the author chose
  (default balanced).

Name rule (`RldChar_NameValid`, one function for packer and reader):

- 1 to 17 characters of `A-Z 0-9`, space and `! % ' + , - . / : < = > ? _`.
- Then a NUL, and every further byte of the 20-byte field is 0.
- A reader refuses a CHRI that breaks it as DAMAGED; `rldpack make-char` first
  turns a-z into capitals, then stops before it writes a name that breaks it.

### CMDL

The chunk exactly as stored:

| Offset | Content |
|---|---|
| +0 | u32 G, the body size |
| +4 | G bytes of body; body offset 0 = `struct Model` |
| +4+G | u32 mapBytes, a multiple of 4 |
| +8+G | mapBytes / 4 u32 body offsets of the pointer fields (the pointer map) |

- The chunk ends right behind the map.
- Pointer fields hold body offsets; 0 is NULL.
- The pointer map lists exactly the pointer fields that are not 0, each once.

`struct Model`, 0x18 bytes:

| Offset | Type | Field | 1.0 |
|---|---|---|---|
| 0x00 | char[16] | name | 1..12 characters, NUL, then 0 up to byte 15 |
| 0x10 | s16 | id | -1 |
| 0x12 | s16 | numHeaders | 1 |
| 0x14 | ptr | headers | header 0 |

`struct ModelHeader`, 0x40 bytes (0x00..0x13 are not checked):

| Offset | Type | Field | 1.0 |
|---|---|---|---|
| 0x14 | u16 | maxDistanceLOD | >= 8192 |
| 0x16 | u16 | flags | 0 |
| 0x18 | 3 x s16 | scale | not checked |
| 0x20 | ptr | ptrCommandList | set |
| 0x24 | ptr | ptrFrameData | 0 |
| 0x28 | ptr | ptrTexLayout | 0 |
| 0x2C | ptr | ptrColors | set; vertex colors, 4 bytes each |
| 0x30 | ptr | unk3 | 0 |
| 0x34 | u32 | numAnimations | 4 |
| 0x38 | ptr | ptrAnimations | set; table of numAnimations ModelAnim pointers |
| 0x3C | ptr | animtex | 0 |

`struct ModelAnim`, 0x18 bytes (0x00..0x0F are not checked), frames follow directly:

| Offset | Type | Field | 1.0 |
|---|---|---|---|
| 0x10 | u16 | numFrames | bit 15 (half frames) clear |
| 0x12 | u16 | frameSize | the frame size B |
| 0x14 | ptr | ptrDeltaArray | 0: raw frames |

Animations and their frame counts:

| Index | Animation | Frames |
|---|---|---|
| 0 | turn | 21 |
| 1 | reverse | 7 |
| 2 | bump | 15 |
| 3 | jump | 4 |

A raw frame:

- `struct ModelFrame`, 0x1C bytes: pos as 3 x s16 at 0x00, vertexOffset as s32
  at 0x18 (must be 0x1C); bytes 0x06..0x17 are not checked.
- Then R records of 3 bytes. A vertex is (pos + byte) << 2.
- B = 0x1C + 3 x R, rounded up to a multiple of 4.
- R is the number of vertex commands without bit 26.

The command list:

- Word 0 is the color count N.
- Then the command words.
- The end word 0xFFFFFFFF.

| Bits | Meaning |
|---|---|
| 31 | new strip |
| 30 | reuse the strip's first vertex (fan) |
| 29 | cull side |
| 28 | back-face test on |
| 27 | color from the scratchpad color cache |
| 26 | vertex from its cache slot, no new record |
| 25-24 | no meaning in the check; rldpack writes 0 |
| 23-16 | cache slot |
| 15-9 | color index |
| 8-0 | texture index; 0 = untextured (G3), the only value 1.0 allows |

A word whose upper 16 bits are all 0 is a color-only command; 1.0 refuses it.

What `rldpack make-char` writes (`RldMk_WriteCmdl`):

| Body offset | Part |
|---|---|
| 0x00 | Model |
| 0x18 | the one ModelHeader |
| 0x58 | the table of 4 ModelAnim pointers |
| 0x68 | the command list |
| then | the colors (0x00BBGGRR) |
| then | per animation its ModelAnim and its raw frames |

The pointer map has 8 entries: Model.headers, ptrCommandList, ptrColors,
ptrAnimations, and the 4 table entries.

### The size of the driver

The format has no size field. `rldpack make-char --size <percent>` (50..200,
default 100) bakes the size into the vertices before the poses are made
(`RldMk_StepSize`):

- Only the driver and the steering wheel are scaled, about the hip
  H = (0, 16, 0) game units (0.25 Blender units above the ground):
  p' = H + s x (p - H).
- The kart is not scaled: the game draws the wheels as sprites at fixed
  points of the kart (unless the CHRI flags hide them, bit 0), so it stays as
  large as the model has it (about a retail kart).
- It is a visual size only - physics and collision follow the driving style
  (the class in CHRI).
- 100 gives the same bytes as no `--size` at all.
- The range a model takes depends on the model: the 16-bit model scale per
  axis (model-scale), the coordinate range of every frame (model-coords) and
  the length of the whole model. `rldpack make-char --machine` reports it as
  `@value size-range <lo> <hi>` (fields separated by tabs); `0 0` means that
  no size fits the model. A size outside is refused (`char-size`).

### CICN (`RldChar_CheckIcon`)

The portrait in the driver select, in the retail portrait format:

| Offset | Type | Field | 1.0 |
|---|---|---|---|
| 0x00 | u16 | version | 1 |
| 0x02 | u16 | depth | 4 bits per texel |
| 0x04 | u16 | width | 44 |
| 0x06 | u16 | height | 26 |
| 0x08 | u16[16] | clut | RGB555 and the STP bit 15, red in bits 0-4; the value 0x0000 is transparent, so black is 0x0421 |
| 0x28 | u16[26][11] | texels | the rows from the top; 4 texels per word, the lowest nibble is the leftmost |

612 bytes, nothing else; there is no VRAM position in the chunk.

Rules, in order. Each one gives a fixed text that starts with the rule:

| Rule | What must hold |
|---|---|
| CICN-3 | at least 8 bytes (checked first: below 8 bytes there is no header) |
| CICN-1 | version 1 and depth 4 |
| CICN-2 | width 44 and height 26 |
| CICN-3 | exactly 612 bytes |

A CICN that breaks a rule never refuses the file: only the portrait is
dropped, and the template's portrait is shown. `rldpack info` names the
portrait ("its own", "the template's (no CICN)", or "CICN ignored" with the
reason); `rldpack verify` prints `IGNORED CICN` for a rule finding. A CICN
whose hash does not match fails the file in `rldpack verify`; `rldpack info`
shows "CICN ignored" with the hash mismatch; the game does not read CICN and
loads the file.

What `rldpack make-char --icon <png>` does (`RldMk_MakeIcon`):

1. Reads the PNG with its own reader (`include/rldpng.inc`): color types 0,
   2, 3, 4 and 6, bit depths 1 to 16, not interlaced, at most 4096 x 4096
   pixels.
2. Cuts it to 44:26 in the middle; the longer side loses the same on both
   ends (one pixel more at the end when the difference is odd).
3. Scales it to 44 x 26 with a box filter in integers, the colors weighted by
   their alpha. A texel that is less than half covered is transparent.
4. Reduces the opaque texels to at most 15 colors (the same median cut and
   k-means as the model's palette). Entry 0 is transparent; a color that
   rounds to 0x0000 is written as 0x0421.
5. Builds the chunk twice, compares the bytes and runs CICN-1..3 on it.

Without `--icon` the file has no CICN, and the game shows the template's
portrait.

The game does not read CICN yet (`platform/native_chars.c`): until a later
version, the driver select shows the template's portrait for every custom
character, with or without CICN.

### Voices: checked, not packed

`rldpack make-char --voices <folder>` reads the voice files (boost1/2, hit1/2,
spin1/2, bigair1/2, drop1/2, shield1/2, passing1/2, fire1/2, yes, hit; .wav or
.vag), assigns them to their places and checks them. Nothing goes into the
file: CVOI stays a reserved name, and a custom driver is silent in the game.

### What the game does

| When | What |
|---|---|
| start | every `*.rldchar` in the `characters` folder next to the game (the extension in any case, subfolders skipped): the names are sorted first, then the files are read in that order |
| per file | envelope (`Rld_OpenAs`), the CMDL size before any memory is taken, CHRI and CMDL with their hashes, `RldChar_ParseInfo`, `RldChar_CheckModel`, then the pointer map. CICN and CPRM are not read |
| a broken file | skipped with one log line `[CTR Char] REFUSED <file>: <kind> (<rule>) <detail>`; the game starts anyway |
| the first 32 valid files | a tile each in the one-player ARCADE driver select, after the retail drivers (not for CRYSTAL and CTR under NITRO-PIT); a further valid file gets the log line `NO ID` and no tile |
| menu and race | the driver select shows the template's portrait and the name from CHRI; in the race seat 0 runs on the template's character id with the custom model, the class in CHRI sets the physics values and the engine sound, and the template's voice is not played |
| wheels | with `RLDCHAR_FLAG_NO_WHEELS` set in CHRI the game draws no kart wheels (and no wheel reflections) for the custom model - tyre dust and skid marks stay, they are effects at the wheel points, not wheels - and the load line `[CTR Char] loaded <file>: ...` ends in `, wheels hidden`; without the bit the wheels are drawn as for a retail driver |

- The model's frame counts must match those of the template's retail model;
  otherwise seat 0 stays retail (log line `not bound: frames`).
- One summary line per start: `[CTR Char] characters: N loaded, M refused (<folder>)`,
  with `K without an id` in it when files got no tile. Without a folder, under
  `--settings-defaults` and with `--char` the line has other wording.
- With `--settings-defaults` no folder is read unless `--chars-dir` names one.

### Limits

| Constant | Value |
|---|---|
| `RLDCHAR_NAME_MAX` | 17 characters |
| `RLDCHAR_AUTHOR_MAX` | 64 bytes |
| `RLDCHAR_TEMPLATE_MAX` | 14 |
| `RLDCHAR_CLASS_MAX` | 3 |
| `RLDCHAR_COLORS_MAX` | 128 colors |
| `RLDCHAR_SLOT_MAX` | slot 87 |
| `RLDCHAR_LOD_MIN` | 8192 |
| `RLDCHAR_FRAME_SIZE_MAX` | 65535 bytes per frame |
| `RLDCHAR_COORD_MIN`, `RLDCHAR_COORD_MAX` | -8192 <= pos and pos + 255 <= 8191 |
| `RLDCHAR_DRAW_BYTES_MAX` | 22560 bytes of draw memory (564 x 0x28) |

## 4. Model rules (`RldChar_CheckModel`)

Packer and reader run the same function.

- The first finding decides the verdict.
- Without a listener the check stops at the first finding.
- `rldpack verify` goes on through all findings and prints the first 32;
  `rldpack info` shows the first finding only.

Verdicts: BAD MODEL, MODEL LIMIT, and DAMAGED for model-size only.

| Stage | Rule | What must hold | Verdict | Scope |
|---|---|---|---|---|
| before | model-size | CMDL <= 256 KiB; the envelope refuses a larger one first | DAMAGED | once |
| 1 | model-bounds | see below; a finding ends the check | BAD MODEL | once |
| 1 | model-map | the pointer map is exact, see below | BAD MODEL | per entry and field |
| 1 | model-head | Model.id -1; name 1..12 characters, NUL, 0 up to byte 15; numHeaders 1; headers not NULL (else the check ends) | BAD MODEL | once |
| 1 | model-headers | ptrCommandList, ptrColors, ptrAnimations not NULL | BAD MODEL | once |
| 1 | model-zero | ptrFrameData, ptrTexLayout, unk3, animtex and flags are 0 | BAD MODEL | once |
| 1 | model-lod | maxDistanceLOD (u16) >= 8192 | BAD MODEL | once |
| 1 | model-anims | numAnimations 4; no NULL entry; ptrDeltaArray 0; numFrames bit 15 clear | BAD MODEL | per animation |
| 2 | model-term | the end word 0xFFFFFFFF lies in the body | BAD MODEL | the list |
| 2 | model-colors-max | N <= 128 | MODEL LIMIT | the list |
| 2 | model-colors-range | the N colors from ptrColors lie in the body (only when N <= 128) | BAD MODEL | the list |
| 2 | model-colors-range | color index < N (only when N <= 128) | BAD MODEL | per command |
| 2 | model-colorcmd | upper 16 bits not all 0 | BAD MODEL | per command |
| 2 | model-tex | texture index 0 | BAD MODEL | per command |
| 2 | model-cache-order | a command with bit 26 reads a slot an earlier command wrote | BAD MODEL | per command |
| 2 | model-slots | slot <= 87 | MODEL LIMIT | per command |
| 2 | model-slot-color | when any command uses bit 27: no command without bit 26 writes a slot below ceil(N / 2) | MODEL LIMIT | per command |
| 3 | model-frame-size | B <= 65535 | MODEL LIMIT | once |
| 3 | model-frame-layout | frameSize == B | BAD MODEL | per animation |
| 3 | model-anim-frames | numFrames & 0x7FFF is 21 / 7 / 15 / 4 | MODEL LIMIT | per animation |
| 3 | model-verts | all frames lie in the body; a finding skips the frame checks of that animation | BAD MODEL | per animation |
| 3 | model-verts | the 0x1C-byte frame head lies in the body | BAD MODEL | per frame |
| 3 | model-frame-layout | vertexOffset == 0x1C | BAD MODEL | per frame |
| 3 | model-verts | vertexOffset < 0x80000000 and vertexOffset + 3 x R <= frameSize | BAD MODEL | per frame |
| 3 | model-coords | per axis: pos >= -8192 and pos + 255 <= 8191 | BAD MODEL | per frame and axis |
| 4 | model-draw | 28 x G3 triangles + 40 x GT3 triangles <= 22560 | MODEL LIMIT | once |

model-bounds, in order:

- CMDL >= 8 bytes; G <= CMDL size - 8; mapBytes a multiple of 4; 8 + G + mapBytes == CMDL size.
- G >= 0x18 and a multiple of 4.
- When headers is set: it is a multiple of 4, and the 0x40-byte header lies in the body.
- ptrCommandList, ptrColors and ptrAnimations are multiples of 4.
- When ptrCommandList is set: word 0 of the command list lies in the body.
- ptrColors <= G.
- When ptrAnimations is set: the table of numAnimations entries lies in the body.
- Every ModelAnim pointer is a multiple of 4; when set, its 0x18 bytes lie in the body.

model-map:

- Every map entry:
  - is below 0x80000000, a multiple of 4, and <= G - 4;
  - is listed once and is a pointer field of a known structure;
  - the field it names is not 0 and holds a value <= G.
- Every known pointer field that is not 0 is listed.
- Known pointer fields: Model.headers; ptrCommandList, ptrFrameData, ptrTexLayout,
  ptrColors, unk3, ptrAnimations and animtex of header 0; every animation table
  entry; ModelAnim.ptrDeltaArray of every animation.

Stage notes:

- Stage 2 runs when ptrCommandList is set. Stage 3 runs when ptrAnimations is
  set, for animations 0..3 as far as the table has them.
- The per-command rules need the end word.
- Without a command list or without an end word R is unknown: model-frame-size, frameSize == B,
  vertexOffset == 0x1C, the per-frame record check and model-draw are not checked.
- frameSize == B and vertexOffset == 0x1C are also not checked when B > 65535.
- Triangles are counted as the renderer emits strips: a strip starts at bit 31
  or at the first command; its first two vertices draw nothing; each further
  vertex draws one triangle.

CHRI rules (`RldChar_ParseInfo`). A finding refuses the file as DAMAGED.

| Rule | What must hold |
|---|---|
| CHRI-1 | CHRI >= 0x20 bytes; fixedSize >= 0x1C, a multiple of 4, and <= CHRI size - 4 (so flags, when fixedSize >= 0x20, lies in CHRI) |
| CHRI-2 | template 0..14 |
| CHRI-3 | class 0..3 |
| CHRI-4 | the name rule |
| CHRI-5 | the string table and every string lie in CHRI; the author is at most 64 bytes |

## 5. Versioning and names

| Change | What it takes |
|---|---|
| a new minor version | nothing: a reader takes any minor, names it and never refuses because of it |
| a new optional chunk | no bump: an older reader checks its position, type and overlap and skips it |
| an optional chunk taken out of the format | no bump: older files keep loading, the chunk is skipped, its name is blocked |
| a new layout inside SNDB, PARM or CICN | the chunk's own version field: an older reader drops only that chunk |
| a new PARM key | no bump: unknown keys are skipped |
| a new string in META or CHRI | appended at the end only |
| a new field in CHRI | a longer fixedSize; an older reader skips the extra bytes. flags at 0x1C came this way (fixedSize 0x20, written only when a bit is set): an older reader skips it and draws the wheels. A new flag bit needs no bump: a reader ignores bits it does not know |
| a new number in META | not possible without a new major: META's numeric part has no length field. New things go into optional chunks |
| something without which the content is wrong | a required-feature bit in header field 0x0C; a reader that does not know it refuses (NEEDS NEWER) |
| a change an older reader would misread | a new major: older readers say NEEDS NEWER, newer readers say OLD FORMAT for the old files |
| a required chunk becoming optional | not possible within one major, in either format: an older reader refuses a file without one of its required chunks as DAMAGED (`Rld_OpenAs`) |
| header or directory | never: one envelope for every container type |

For `.rldchar`, a delta-coded model and a texture table are planned as
required-feature bits. No bit value is assigned yet.

Chunk names:

| Name | Container | Status |
|---|---|---|
| META, LEVD, VRMD | `.rldtrack` | required |
| SNDB, PARM | `.rldtrack` | optional |
| CHRI, CMDL | `.rldchar` | required |
| CICN | `.rldchar` | optional (portrait): written by `rldpack make-char --icon`, not shown by the game yet |
| CPRM | `.rldchar` | optional known type (values): not written by rldpack, content not interpreted yet |
| CVOI, CTEX | `.rldchar` | reserved, planned (voices, texture data) |
| SIGN | blocked | signature, taken out of the format; skipped in older files |
| MMAP | blocked | menu map, taken out with 4.1; skipped in older files |
| THMB | blocked | preview image, dropped |
| CHAL | blocked | challenges, reserved once, dropped with 4.1 |
| AUDD | blocked | music, reserved once, replaced by SNDB |

A blocked name is never assigned again, not even for something else.

## 6. Authority

The comments in include/rldtrack.inc and include/rldchar.inc are authoritative; this document changes in the same commit.
