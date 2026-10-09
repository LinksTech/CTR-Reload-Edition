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
| known chunks and size limits | META 64 KiB, LEVD 16 MiB, VRMD 4 MiB, SNDB 4 MiB, PARM 4 KiB (`Rld_ChunkLimit`) | CHRI 4 KiB, CMDL 4 MiB, CICN 4 KiB, CMSK 256 KiB, CPRM 4 KiB, CVOI 6 MiB (`RldChar_ChunkLimit`) |

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
| 0x4 | Time Trial | can be declared, not needed: the NITRO-PIT time trial (MODE box under RACE) runs on every Race track |
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
| CMDL | required | 4 MiB | the model, in the game's native model format |
| CICN | optional, written by `rldpack make-char --icon` | 4 KiB | the portrait, exactly 612 bytes (rules CICN-1..3); the game shows it in the driver select grid, the race HUD, the results and the cup standings |
| CMSK | optional, experimental, written by `rldpack make-char --mask-model` | 256 KiB (rule CMSK-3: 16 KiB) | an own mask model, drawn in place of Aku Aku / Uka Uka (rules CMSK-1..3) |
| CPRM | optional, known type: not written by rldpack, content not interpreted yet | 4 KiB | values; layout not fixed (draft) |
| CVOI | optional, written by `rldpack make-char --voices` | 6 MiB (the rules allow at most 5293176 bytes) | the driver's voice: up to 4 clips for each of 10 events, mono 16 bit at 22050 Hz (rules CVOI-1..4) |
| CNET | optional, PREVIEW, written by `rldpack make-char --native-model on` (an OBJ) or `--native-probe` (tests) | 32 MiB, checked only by the reader of the native part (unknown to the envelope) | the native model: a mesh in floats with materials, texture indices and wheels; read only with the native preview (NATIVE DRIVERS set to PREVIEW on the GRAPHICS page, or `--native-preview`) (see "Preview, planned: CNET and CTXT") |
| CTXT | optional, PREVIEW, written with CNET | 32 MiB, as CNET | the textures of CNET as PNG, decoded to RGBA8 (see the same section) |
| CTEX | reserved name, not assigned | none (unknown, skipped) | once planned for texture data; the textures of the native model are CTXT, so the name stays unused |
| CWHL | reserved name, not assigned | none (unknown, skipped) | own wheels; the wheels of the native model are a part of CNET, so CWHL stays reserved and unassigned (its old plan below) |

`rldpack make-char` writes CHRI, CMDL, then CICN with `--icon`, CMSK with
`--mask-model` and CVOI with `--voices`, in this order. CICN, CMSK, CVOI and
CPRM are known types. When present, the envelope checks them (compression,
sizes, limit) and `rldpack verify` checks their hash. A CICN, CMSK or CVOI
that breaks an envelope rule (steps 14-19, e.g. a CICN larger than 4 KiB, a
CMSK larger than 256 KiB or a CVOI larger than 6 MiB) refuses the whole file;
a CICN that breaks CICN-1..3 only loses the portrait, a CMSK that breaks
CMSK-1..3 or a model rule only loses the own mask, a CVOI that breaks
CVOI-1..4 only loses the voices (see CICN, CMSK and CVOI below). Nothing
interprets CPRM yet. CNET and CTXT (preview) are not known types of the
envelope: it checks only their position, a doubled type and an overlap, as
for any unknown chunk; everything else about them costs only the native part
(see "Preview, planned: CNET and CTXT").

### CHRI (`RldChar_ParseInfo`)

| Offset | Type | Field | Meaning |
|---|---|---|---|
| 0x00 | u16 | fixedSize | length of the numeric part: >= 0x1C, a multiple of 4 |
| 0x02 | u8 | template | the retail character 0..14 the character runs on in the game; Reload Studio always writes 14 (Fake Crash) |
| 0x03 | u8 | class | engine class 0..3: balanced, acceleration, speed, turning; the game drives the character with it |
| 0x04 | char[20] | name | display name, see the name rule |
| 0x18 | u32 | charVersion | for humans; the CMDL hash, not this number, is meant to tell versions apart |
| 0x1C | u32 | flags | only when fixedSize >= 0x20, else 0; bit 0 (`RLDCHAR_FLAG_NO_WHEELS`): the game draws no kart wheels for this driver (tyre dust and skid marks stay: they are effects at the wheel points, not wheels); bits 1-2 (`RLDCHAR_FLAG_MASK_BITS`): the mask the driver wears - 0 like the template, 1 Aku Aku, 2 Uka Uka, 3 reserved (read as 0, no finding); bit 3 (`RLDCHAR_FLAG_MAP_COLOR`): the minimap marker has the color at 0x20; bit 4 (`RLDCHAR_FLAG_FULL_HEIGHT`): the models of the file (CMDL and CMSK) use odd heights too - the game draws them with the vertex mask 0xfffcffff instead of the retail 0xfff8ffff, which clears bit 0 of pos.y + byte, so the Y bytes can take all 256 values instead of 128 even ones; the file keeps pos.y even in every frame (the bounding box still clears bit 0 of pos.y and spans 255 from there), the load line adds `, full height`, and a game from before the bit draws an odd height one value lower; further bits are reserved |
| 0x20 | u8[4] | mapColor | only when fixedSize >= 0x24, else absent: red, green, blue of the minimap marker, then 0; used only with bit 3 set (bit 3 without the field, or the field without bit 3: the template's color, no finding) |
| 0x24 | u8 | shadowMode | the look, only when fixedSize >= 0x3C, else retail: 0 retail, 1 auto (the quad at 0x28), 2 off; any other value is read as retail |
| 0x25 | u8 | exhaustMode | 0 retail, 1 custom (the points at 0x30), 2 off; any other value is read as retail |
| 0x26 | u8 | exhaustCount | 1 or 2 with custom, else 0; custom with 0 or more than 2 is read as retail |
| 0x27 | u8 | reserved | written 0, not checked |
| 0x28 | s16[4] | shadowQuad | xMin, xMax, zMin, zMax in 1/16 game units; only with shadow auto, else 0 |
| 0x30 | s16[3] | exhaust0 | x, y, z in 1/16 game units: the exhaust in place of retail source 0 (+X, the driver's left); only with exhaust custom, else 0 |
| 0x36 | s16[3] | exhaust1 | x, y, z in 1/16 game units: in place of retail source 1 (-X, the right); only with custom and count 2, else 0 |
| fixedSize | u32 | stringCount | then per string: u32 length, bytes |
| | | string 0 | author, at most 64 bytes; "" when absent |

- `rldpack` writes fixedSize 0x1C and one string, the author (UTF-8; the packer
  checks it). Only when flags is not 0 (`rldpack make-char --wheels off`,
  `--mask aku` or `--mask uka`) does it write fixedSize 0x20 with flags at
  0x1C, so a file without flags has the same bytes as before the field existed.
  Only with `--map-color` does it write fixedSize 0x24, bit 3 and the color at
  0x20. `--y-full` sets bit 4 (see "What rldpack make-char writes" under
  CMDL); it is about every model of the file, the own mask of
  `--mask-model` included. Only when the look is not retail/retail does it
  write fixedSize 0x3C: flags at 0x1C (also when 0), the color at 0x20 only
  with `--map-color` (else 0 there, without bit 3) and the look at
  0x24..0x3B; every other file keeps the bytes it had before the look
  existed.
- `rldpack info` and `rldpack verify` name the height (`even values of the
  up axis, as retail` or `every value of the up axis (--y-full, CHRI bit
  4)`) and, for a model that passes, count its odd heights (`odd heights
  <n> of <m>`, with a hint when there are some without bit 4). With
  `--machine` they print `@value y-full <on|off> container`; make-char
  prints `@value y-full <on|off> <switch|default>`.
- A reader skips numeric bytes beyond the fields it knows and strings beyond the
  first. A reader from before flags skips it, draws the wheels and gives the
  driver the template's mask; a reader from before mapColor shows the
  template's minimap color; a reader from before the look draws the retail
  shadow and exhaust.
- Flag bits a reader does not know are ignored: they are no finding and never
  refuse the file. A reader from before the mask field ignores bits 1-2 and
  gives the driver the template's mask.
- `rldpack make-char --template` sets the template, `--class` the class
  (default: the template's own class), `--mask aku|uka|like` the mask (default
  `like`: bits 1-2 stay 0 and the driver wears the template's mask; `aku` and
  `uka` are written as chosen, even when the template wears that mask).
  Reload Studio asks for no template: it always passes `--template 14` and the
  driving style the author chose (default balanced). Its Mask choice (Aku Aku
  or Uka Uka) starts at the mask of Fake Crash, Uka Uka; only a different
  choice passes `--mask aku`, so a file built with the default has the same
  bytes as before the field existed. After a build its green line names the
  mask that is in the file (from `@value mask`), for example
  `Built: mydriver.rldchar (55 KB) - mask Aku Aku, kart wheels hidden. Restart the game to load it.`
- `rldpack info` and `rldpack verify` name the mask and whether it is the
  template's or chosen in the file; the reserved value 3 is named as read like
  the template.
- `rldpack make-char --map-color RRGGBB` (six hex digits, any case, a leading
  `#` allowed) sets the minimap color; without it the template's color stays
  and nothing is written. `rldpack info` and `rldpack verify` print
  `map color RRGGBB` or `map color the template's`; with `--machine`
  `@value map-color <RRGGBB|template> <switch|template|container>`. Reload
  Studio's field "Minimap colour" (Choose... / Like the template) starts at
  the template's colour (808080 for Fake Crash: the marker as drawn) and
  passes `--map-color` only when a colour is chosen.

The look (`RldChar_ParseLook`, one function for packer and reader): the
ground shadow and the exhaust of the driver, fitted to the model.

- Space: game units of the model (CMDL without the instance scale, the same
  units as CNET; Blender 1 m = 64), origin = the model's origin, +X the
  driver's left seen from behind, +Y up, +Z forward; stored in 1/16 game
  units. CMDL and CNET share the origin, so the values mean the same for
  the drawn CMDL and the native model. Retail in these units: the exhaust
  smoke at (+-288, 896, -896) (18, 56, -56 game units), the turbo flames at
  (+-288, 768, -832), the shadow quad x -800..800, z -820..1040.
- NO RULE: the look never refuses a file, whatever it holds. A value outside
  the limits only makes that one effect retail, with one load line
  `[CTR Char] look <file>: <why> - retail` (the first effect that falls
  back, the shadow before the exhaust):
  - shadow auto: every value within -4096..4096 (256 game units), xMin < xMax
    and zMin < zMax, each side at least 16 (1 game unit);
  - exhaust custom: count 1 or 2; per used point x and z within -4096..4096,
    y within -256..4096;
  - an unknown mode is read as retail. The reserved byte, the count without
    custom and every unused value are not checked.
- A file from before the field (fixedSize below 0x3C) is retail/retail -
  also with the wheels hidden: the defaults below are the writer's, not the
  reader's.
- `rldpack make-char --shadow retail|auto|off` and `--exhaust
  retail|off|x,y,z[;x,y,z]` (the words in any case). The points are in game
  units of the finished model - after `--fit` and the shift onto the
  reference dummy, the units `rldpack info` and the preview of Reload
  Studio show (+X left seen from behind, +Y up, +Z forward, the model's
  origin); point 1 stands in for the retail source on the left (+X), point
  2 for the one on the right; one point leaves the right source undrawn.
  They are stored in 1/16 game units, rounded. They do not follow a later
  change of the fit: a model fitted anew needs its points set anew (Reload
  Studio: Pick). Without the switches: retail/retail with the kart wheels,
  auto/off with `--wheels off`. make-char refuses an unknown word, a point
  that is not three numbers, more than two points and a value outside the
  limits (x and z -256..256, y -16..256 game units, after the rounding)
  with its reason (codes `shadow`, `exhaust`), and reads the CHRI it built
  back with the game's `RldChar_ParseLook` before it writes.
- auto: make-char computes the quad at build time from the CMDL it built
  (animation 0, frame 10, the neutral pose of the `size` line, decoded to
  game units): the XZ box of the vertices in the lower half of the height
  (y <= yMin + 0.5 x (yMax - yMin); fewer than 3 such vertices: the whole
  box), the half width times 1.25 around the same middle, z from the box as
  it is; at least 8 game units on each side and 16 long, within 256 of the
  origin, rounded to 1/16. The factors are a start value: what the shadow
  icons cover is not measured, and a better rule needs only the packer.
  The quad comes from the CMDL also when the file has a native model (a
  reduced CMDL can differ from the native mesh by a fraction of a unit).
- make-char, `rldpack info` and `rldpack verify` print the look:
  `shadow        auto: x -32.2..32.2, z -53.8..58.2 (game units)`, `retail`
  or `off - the game draws none`; `exhaust       custom: (x, y, z), (x, y, z)
  (game units)` (with one point ` - the right source drawn as none`),
  `retail` or `off - the game draws none`. A look the game reads as retail
  is named by info and verify (`look          <reason> - the game draws the
  retail look`), and verify warns about it on stderr; it is no refusal.
  With `--machine`: `@value shadow <retail|auto|off> <switch|default|container>`,
  `@value retail-shadow -50 50 -51.25 65`, `@value exhaust
  <retail|custom|off> <switch|default|container>`, `@value exhaust-point
  <1|2> x y z` (custom only), `@value retail-exhaust 18 56 -56`, all in
  game units; `@value shadow-quad x0 x1 z0 z1` - from make-char always,
  once the model is built, the quad auto writes for it (also while another
  mode is chosen, for the preview), from info only with auto; and from info
  `@value look retail <reason>` for a look the game reads as retail.
- Reload Studio (a preview feature open to everyone, marked "Preview
  feature"): the card "In the race" of the tab "In-game look" chooses Shadow
  and Exhaust and the two points (fields, or Pick on the model); it starts at
  retail and retail (in the user mode of `--enable-preview-features` at
  rldpack's defaults) and passes the switches only when the choice differs
  from rldpack's defaults. Without a choice and with "Show kart wheels" off
  it passes `--shadow retail --exhaust retail`, so the file keeps the bytes
  it had before the look.
- What the game draws: see the line "look" under "What the game does".

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

The command list and the frames as `rldpack make-char` packs them:

- Colors: a palette of at most 128 colors (`--colors 128|64`, default 128;
  `@value colors <n> <switch|default>`). The color index has 7 bits (bits
  15-9), so 128 is the most the renderer can address. A model with more
  colors is merged by median cut and two k-means rounds; an entry that
  k-means leaves unused takes the color with the largest weighted error, so
  every entry is used.
- Bit 27 is never set: every color comes from ptrColors. With bit 27 the
  renderer first copies the colors into the scratchpad, where they take the
  cache slots below ceil(N / 2); without it model-slot-color has nothing to
  check and every slot is free.
- The vertex cache uses the slots 1..87 (`RLDCHAR_SLOT_MAX`), first in,
  first out; a corner still in its slot is fetched with bit 26 and costs no
  record. Slot 0 stays unused: a command of slot 0 without flags would have
  its upper 16 bits all 0 and read as a color-only command.
- The up axis: the renderer clears bit 0 of pos.y + byte, so without CHRI
  bit 4 pos.y and every Y byte are even (128 heights). With
  `--y-full` (CHRI bit 4, `RLDCHAR_FLAG_FULL_HEIGHT`) the Y bytes take
  every value 0..255 at the same scale, half the height step; pos.y stays
  even in every frame either way. X and Z always take every value.

### The model rldpack make-char reads (PLY or OBJ)

The container does not record what the model was made from: `--model` (and
`--mask-model`) take a PLY or an OBJ file, and both end in the same chain
(`tools/rldpack_import.inc`). The content decides the format, not the name:
a file that starts with the line `ply` is PLY; text whose lines are comments
and OBJ keywords, with at least one `v`, is OBJ. A file named for the other
format is read as what it is (warning `model-misnamed`); files of other 3D
programs and pictures (FBX, glTF, GLB, STL, Blender, 3DS, PNG, JPEG, ZIP and
more) are named and refused (`model-unsupported`), anything else is
`model-unknown` - except a file named `.ply`, which the PLY reader reads and
refuses with its own message (`ply-format`), as before OBJ. The poses
(`char-poses`) read PLY only (`model-ply-only`); the wheel preview
(`char-wheel`) and `make-char --wheel-model` read PLY or OBJ (see "A wheel of
one's own").

- PLY: `format ascii 1.0` or `binary_little_endian 1.0`, the vertex colors
  `red green blue` (bytes as they are, floats x 255 rounded).
- OBJ: `v` (an optional `w`, or `x y z r g b` with vertex colors 0..1), `vt`,
  `vn`, `f` in the forms `v`, `v/vt`, `v//vn` and `v/vt/vn`, negative indices,
  faces of 3 to 1024 corners, `o` and `g` (only listed), `usemtl` and
  `mtllib`; the other keywords of the format are read past with a note.
  Anything else stops the reader with the line number (`obj-syntax`,
  `obj-index`, `obj-number`, `obj-face`, `obj-empty`).
- The color of each corner of an OBJ, as bytes floor(c x 255 + 0.5) like a
  PLY's float colors (no color space conversion), in this order
  (`tools/rldpack_obj.inc`):
  1. its vertex color, when the vertex has one - the material's Kd is then
     not used (Blender writes Kd 0.8 for every material); the rule goes by
     vertex: in a file where only some `v` have a color, the others take the
     material rule;
  2. otherwise the material: Kd times its `map_Kd` texture, the texture
     alone without Kd, Kd alone without a texture;
  3. neither: grey, 0x80 per channel (`obj-no-colors` when the whole model
     is grey).
  A material that lights itself (`Ke`) takes the brighter of Kd and Ke per
  channel, since the game draws without light (Ke without Kd: Ke) - but not
  a material with a `map_Kd` and no Kd of its own: it stays white times the
  texture (Blender writes Ke beside map_Kd; Ke in place of the missing Kd
  would only darken it), and grey 0x80 when its texture is not found. The MTL keywords are read in
  any letter case (`map_kd`, `kd`); a material that is transparent (`d`
  below 1, `Tr` above 0 or a `map_d`) is drawn opaque (`obj-transparency`).
- A vertex color on a textured face: the vertex color times the texture.
  `--vertex-colors auto|modulate|color` (default `auto`) says how:
  `modulate` as the PS1 draws it, the vertex color x 255/128 and at most 1,
  so 0x80 shows the texture as it is; `color` the vertex color as it is.
  `auto` modulates when the model is a PS1 rip: the vertex colors of the
  corners of faces whose material has a `map_Kd` (found or not; the
  brightest channel, a corner without a vertex color counts as white) have
  their median at 0x70..0x90 and at least 5 % of them are exactly 0x7F or
  0x80 grey; else it is `color`. Modulation needs the texture file: without
  it the vertex color stays as it is, and a face without a texture is never
  brightened. When modulation applies, `obj-vertex-modulation` (info) says
  on how many corners.
- Vertex colors that are only a fill: when every vertex the faces use has
  the same color, exactly white or exactly black (what an unpainted color
  attribute holds), and the materials in use have colors of their own (2
  or more different Kd, or a texture), the vertex colors are dropped and
  the materials count (`obj-vertex-colors-uniform`, a warning). Any other
  color, even one for all vertices, stays. `--colors-from vertex|material`
  decides instead of this rule (default `auto`).
- The texture (`map_Kd`): PNG (`include/rldpng.inc`), JPEG, TGA and BMP
  (`tools/rldpack_image.c`; a BMP needs a header of at least 34 bytes), at
  most 4096 pixels a side and 64 MiB a file. The options `-o` and `-s`
  place it (offset + scale x `vt`, u and v; w is not used), `-clamp on`
  clamps the coordinates into 0..1; a word that starts with `-` but is no
  option of the format starts the file name (`-face.png`). Sampling is
  bilinear with `v` = 0 at the bottom of the picture, repeated (u and v
  modulo 1), but with the edges clamped on a face whose coordinates all lie
  in 0..1 and with `-clamp on`: there the other side of the picture is
  never mixed in at the edge. Each texel counts by its alpha: transparent
  texels add nothing, and a corner on fully transparent texels keeps its
  color without the texture (`tex-alpha`, info).
- The texture of a triangle with `vt` at all three corners is fitted over
  the whole triangle: its three corner colors are the ones whose Gouraud
  mix comes closest to the texture (least squares over a grid of sample
  points, about one per texel along the longest side, 4..32). A face of
  more corners, which the chain splits later, is sampled at its corners'
  `vt`.
- Finding the files: `mtllib` and `map_Kd` paths are relative to the OBJ
  and MTL file. A texture that is not at its path is looked for at a fixed
  list of other places, never by listing a folder: first in each folder
  given with `--textures <folder>` (up to 8 times), then in the MTL file's
  folder and the OBJ file's - each time the last two parts of the path
  (`textures/a.png` of a project moved to another computer), the file
  name, the file name in a subfolder `textures`, `tex`, `images` or `maps`,
  then the same with the other file types `.png`, `.jpg`, `.jpeg`, `.tga`,
  `.bmp`. The first file found counts (`tex-found-nearby`, info, names
  it). One texture file is read once however its path is written
  (`./t.png`, `a/../t.png`, `T.PNG`).
- Every missing piece falls back with a warning: an MTL file not found
  (`mtl-missing`) or not readable (`mtl-bad`, also for a Kd line that is
  not 1 or 3 numbers and a `map_Kd` without a file name), a material it
  lacks (`mtl-material`), a texture broken or of another format
  (`tex-unreadable`, `tex-unsupported`), a textured corner without `vt`
  (`tex-no-uv`), a color value outside 0..1 (`obj-color-range`, clamped).
  The textures that were not found are said in one `tex-missing` warning:
  how many of how many, the first three names, how many faces show only
  the vertex or material color, and where to put the files; its technical
  line says where the first one was looked for. The other texture and
  material warnings are said one by one for the first 16, the rest in one
  more. Only the textures of materials that faces use are read.
- The chain keeps one color per vertex, so an OBJ becomes one vertex per
  pair of `v` and color, in the order the faces first use it. Welding by
  position joins them again where the colors meet.
- Limits, as for PLY: 1000000 vertices, texture coordinates and faces, 1024
  corners a face, 4000000 face corners (`obj-big`), a line of 256 KiB, an
  MTL file of 16 MiB. A model file larger than 512 MiB is refused before it
  is read (`file-too-large`, the only message for it).
- With `--machine` the model is described in `@model` lines (the head of
  `tools/rldpack_import.inc`): `format`, `mtl`, `texture <state> <material>
  <path> <sha256|->` (one per material in use with a `map_Kd`: the file
  that was used, found at its path or elsewhere, and the SHA-256 of its
  bytes, `-` when none was read), `vertex-colors <auto|modulate|color>
  <rip|plain>` (the `--vertex-colors` mode and what the model was taken
  for), `group` and `colors`; for `--mask-model` they follow its
  `@file mask-model` line. The switches are reported as
  `@value vertex-colors`, `@value colors-from` and `@value textures`. A
  missing `.obj` is `model-open` (a PLY keeps `ply-open`).
- Faces of four corners are split at the shorter diagonal, unless the quad
  is concave and the shorter diagonal runs outside it: then one of its
  triangles turns against the quad's normal (Newell's) while both of the
  other diagonal turn with it, and the other diagonal is taken.

### Fit, parts and poses

`rldpack make-char` places the model by its parts; the container records
none of it, only the result in CMDL and the no-wheels flag in CHRI. Sizes
are in game units (64 per model unit; Blender: meters), measured on the
retail drivers: Crash with his kart is 112.4 long, 68.1 wide and 82.7 tall,
the tallest retail driver 142.5 tall.

The fit (`--fit crash`, the default; `RldMk_StepFit`). Silent passes measure
the model on the repaired mesh (welded and cleaned as the real pass does it,
so an export with split seams does not fall apart into fragments), then the
chain runs again with the factor, rounded to 4 significant digits, as
`--scale` - `--fit crash` gives the bytes of `--fit none --scale <f>`.

- With the kart wheels (the default): the kart is brought to Crash's
  length, 112.4, and the whole model is capped at 142.5 tall. When the
  height decides, the kart comes out shorter than Crash's and the game's
  wheel sprites do not sit on it: warning `fit-capped`, which names the
  height the driver would have and how far to shrink it in the model (or
  to hide the kart wheels).
- Without them (`--wheels off`): the whole model, one factor, within
  Crash with his kart in every direction - the smallest of 112.4 / length,
  68.1 / width and 82.7 / height. The size is measured without stray
  pieces: the pieces (positions joined by triangles) are taken by their
  surface area, the largest first, until they hold 90 % of the area; any
  other piece counts only when it lies within that core's box grown by
  half its size on every side. With this fit the "far taller than long"
  check (`model-scale`) and `ply-axes` do not judge the size.

The parts (`RldMk_StepParts`):

- The kart is the longest part at the bottom of the model (after the fit,
  0.15 below to 0.3 above the ground) that is 1.25 to 2.5 model units
  long and lies under the driver: the middle of the part with the most
  triangles besides it is over the kart seen from above, within its
  extent in x and z and an eighth of it more. A long part beside or behind
  the figure (a sword, a tail) is not the kart; `ply-no-kart` names it.
  With `--wheels off` the model brings its own vehicle, which may be
  shorter: when no part is a kart long, the longest such part that is
  still a vehicle is the kart (`ply-kart-short`, info): at least 0.6 long,
  longer than it is tall, under the driver, and not the only part of the
  model. A figure standing on its feet has none - a separate shoe is too
  short, a figure or legs of one piece are taller than long. With the
  wheels on `ply-no-kart` stays; its message names such a part when there
  is one.
- The driver is the largest other part. With `--wheels off` it is the
  largest part above the vehicle (its middle in height above the
  vehicle's top) and over it seen from above (an eighth more, as for the
  kart), with at least 1 % of the triangles: a car's wheels or mirrors or
  a stray piece are no driver. The steering wheel: small parts (at most
  0.6 model units across) in front of the driver - except on a vehicle
  shorter than a kart (`ply-kart-short`), which has no retail steering
  wheel: there a small part above the vehicle leans with the driver (a
  tank driver's arms), one below its top belongs to the vehicle.
- What belongs to the vehicle stands still with the kart. With the kart
  wheels, a part that is not a steering wheel, reaches across the kart's
  middle in x, lies over the kart and reaches down to the kart's top (at
  or below it: a hull exported on its chassis starts exactly there); then,
  repeated until nothing changes, a part across the kart's middle whose
  bottom lies within 1.0 game units of the top of a vehicle part, the two
  over each other (a turret on a hull, a box on a seat). A part that
  starts deeper inside another stays with the driver. With `--wheels off`
  everything that is not the driver, not a steering wheel and not above
  the vehicle is vehicle (a motorbike's footrests and the shoes on them).
- No driver above the vehicle (`--wheels off`): the whole model is the
  vehicle and stands still in every pose, without poses (`ply-vehicle-only`,
  info; also for a vehicle of one piece). `--size` then scales the whole
  model about the point on the ground under its middle.
- Warning `ply-driver`: the driver was taken by its triangles, but a larger
  part reaching higher stays still with the kart - if that part is the
  driver, the vehicle's own parts belong into the kart's mesh. Large means
  wider than a steering wheel and not a stick (as wide and as deep as an
  eighth of its height).
- `ply-center`: the kart's middle more than 0.08 model units off x = 0
  after the fit; the message gives the distance in the units of the file.
  Fitted onto the dummy it is an info - the kart is centered for the game
  anyway; with `--fit none` a warning.
- `ply-up-axis` (note): after `ply-no-kart` or `ply-axes`, a silent build
  with the other up axis; when that one gets through without either, the
  note says to set it (`--up z` or `--up y`).

The place (`--fit crash`, `RldDum_Fit` in `tools/rldpack_dummy.inc`): with
the kart wheels the kart part alone goes onto the dummy's kart - x middle 0,
bottom 5.6, z middle 2.1; with `--wheels off` the measured box of the whole
model goes onto the ground, its x and z middle onto the same point. One
shift for every position.

The poses (the 47 frames, `RldDum_PoseFrame`):

- The turn: the steering wheel turns 30 degrees about the dummy's column,
  the driver rolls up to 16 degrees and turns into the curve up to 5
  (yaw), along the retail curve. The lean is a height profile: a corner h
  above the pivot turns by the angles x min(h / H, 1), H the driver's
  height above the pivot (at least 24): the hips barely move, the head
  takes the full angles. Driver corners near the steering ring (the hands)
  go with the wheel. Reverse, bump and jump keep their own values, about
  the same pivot.
- The pivot: for a kart driver the dummy's seat (0, 10.2, 0.9). A kart
  driver sitting higher, whose lowest corner in the seat zone (z below 15)
  lies more than 8 above the seat, turns about that point instead (x and z
  of the seat). With `--wheels off` the driver turns about the bottom of
  the driver part. Below a pivot of its own the poses leave the driver as
  it is: feet and shoes stay where they stand.
- With `--fit none` nothing is placed, and `--size` and the poses turn
  about the hip (0, 16, 0).

### The size of the driver

The format has no size field. `rldpack make-char --size <percent>` (50..200,
default 100) bakes the size into the vertices before the poses are made
(`RldMk_StepSize`):

- Only the driver and the steering wheel are scaled, about a pivot P:
  p' = P + s x (p - P). With `--fit crash` (the default) the model is first
  placed onto the reference dummy (`tools/rldpack_dummy.inc`, the retail kart
  at Crash's size) and P is the dummy's seat (0, 10.2, 0.9) game units; with
  `--wheels off` it is the middle of the driver's bottom. With `--fit none`
  P is the hip (0, 16, 0) (0.25 Blender units above the ground). The poses
  turn about the same point, or about a pivot of the driver's own (see "Fit,
  parts and poses"), which moves with the size. A model that is only a
  vehicle (`ply-vehicle-only`) scales as a whole, about the point on the
  ground under its middle.
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

### How rldpack make-char runs

Nothing of this is in the container; it is how the tool gets there.

- `--check` runs every check and builds the model in memory once, and
  writes nothing but the previews. A build without `--check` builds the
  model twice and compares the bytes (`model-roundtrip` when they differ).
- The output: `--out <file>` (or the file next to the model) is written as
  `<file>.part` and renamed at the end. An older file is first moved aside
  to `<file>.old` and deleted only once the new one is in place; when the
  rename fails it is moved back, so the old character stays. A failed
  write deletes the `.part`; a process that is stopped (Reload Studio's
  Cancel ends it) leaves at most the `.part`, never half a character under
  the real name. The output and the `@result` line name `<file>`, never
  the `.part`.
- Progress, only with `--machine`: `@progress <step> <done> <total>`
  (fields separated by tabs, like every machine line), step `repair` (7
  passes), `remesh` (6 phases per part group), `reduce` (the triangles
  removed of those to remove; a new stage starts again at 0) or `write`
  (bytes). At most one line per 250 ms, the first 250 ms after the start
  at the earliest, so short runs stay silent; the end of a step (done =
  total) is said when a line of that step with less done came before.
  stdout is flushed after each line. The clock only decides whether a line
  is written: the container, the preview and the diagnosis stay the same
  bytes.
- Before a long reduction, only with `--machine`: a reduction of more than
  60000 triangles gives the info `reduce-slow` with an estimate of the
  time from a measured curve ("Reducing <n> triangles takes about <t>;
  decimate the model in the modeling tool first to save time", or, for the
  hulls of `--remesh`, "The closed hulls give <n> triangles; reducing them
  takes about <t>"), not twice in a row for the same count.
- The reduction cache, only with `--check` (Reload Studio checks again
  after every change of a field): a reduced mesh is kept in the folder for
  temporary files (TEMP, else TMP) as `rldpack-reduce-<nn>.bin`, at most 16
  files, and taken again for the same input. Its key is the SHA-256 of the
  program's build ID, every input array and the targets; a build ID that
  does not tell states of the code apart (unknown, or a modified tree
  without its hash) means no cache. The file carries the SHA-256 of its
  content and is written through a `.part`; a file that is missing,
  damaged or not plausible, and every error of the cache, only mean that
  the reduction runs. Only reductions that reach their target are kept. A
  hit gives the bytes the reduction gives. `--no-cache` turns it off; a
  build without `--check` never uses it, and the self-test neither.
- `--preview <file>` writes the built model as the game draws it, for the
  3D view of Reload Studio (`RldMk_WritePreview`). Little-endian, no
  padding:

  | Offset | Type | Field |
  |---|---|---|
  | 0x00 | char[8] | `RLDPV2` and two NUL |
  | 0x08 | u32 | poses = 3: turn frame 10 (neutral), turn frame 0, turn frame 20 |
  | 0x0C | | per pose: u32 triangles T (the same in every pose), then T x 3 corners of 10 bytes: s16 x, y, z in 1/16 game units (model space, +Y up, +Z forward, +X the driver's left), u8 r, g, b (the vertex color as stored), u8 flags (bit 0: drawn from both sides; bits 1-7 are 0) |

  Size 12 + 3 x (4 + T x 30). The corners of a triangle run
  counter-clockwise seen from the side the game draws. The game draws a
  corner in steps of 0.2 to 0.6 units; 1/16 units keep the preview within
  1/32 of it. The older `RLDPV1` is the same with whole game units;
  Reload Studio reads both.

  Every frame of the classic model follows right behind it (`RLDPC1`,
  `RldMk_ClassicPreviewAppend` in `tools/rldpack_native.inc`), so that the
  preview plays all animations:

  | Field | Type | Content |
  |---|---|---|
  | magic | char[8] | `RLDPC1` and two NUL |
  | frames, records, triangles | u32 x 3 | 47 (turn 21, reverse 7, bump 15, jump 4, the order of CMDL), the records R of the model, T as in RLDPV2 |
  | corners | T x 3 x u16 | the record of every corner, in the order and winding of RLDPV2 (colour and flags of a corner: RLDPV2's) |
  | frames | 47 x R x 3 x s16 | every record in every frame, 1/16 game units as RLDPV2 |

  Size 20 + 6 T + 282 R. Frame 10 is RLDPV2's pose 0 again, frames 0 and
  20 its poses 1 and 2. The classic model has no win and no lose.

  With `--native-model on` the native model follows in the same file, as
  the game's rules passed it (`RLDPN3`, `RldMk_NativePreviewBlock3` in
  `tools/rldpack_native.inc`, read by `tools/reloadstudio/rs_tex.c`). It
  carries what the game's "nr" program gets, so that the preview can draw
  the native model as the game does, with all its poses and the shape keys
  win and lose:

  | Field | Type | Content |
  |---|---|---|
  | magic | char[8] | `RLDPN3` and two NUL |
  | flags | u32 | bit 0: a material blends (alpha mode 2) - the game then refuses the whole native part and draws CMDL, and so does the preview; make-char writes only opaque and mask materials. Bits 1-31 are 0 |
  | textures | u32 + entries | 0..16; per texture u32 width, u32 height (powers of two 16..2048, CTXT-4), u32 flags (CTXT), then width x height x 4 bytes RGBA: level 0 as CTXT holds it, rows from the top, never scaled. The reader builds the levels below with `include/rldmip.inc`, the game's code; CTXT-6 keeps all of them within 64 MiB |
  | materials | u32 + M x 8 | 1..64; the first 8 bytes of MATL: u8 r, g, b, a (the tint), s16 texture (-1 none), u8 alpha mode (0 opaque, 1 mask, 2 blend), u8 flags (bit 0 always nearest) |
  | counts | u32 x 5 | vertices N, triangles T, poses P (47, or 1 for a still model), neutral (the pose win and lose start from: 10, or 0), morphs (bit 0 win, bit 1 lose) |
  | poses | P x N x 3 x s16 | every pose of every vertex in 1/16 game units (rounded half away from zero) |
  | targets | per morph bit, N x 3 x s16 | win, then lose: the target itself (pose neutral + its MRPH delta), the same units; the view blends neutral + w x (target - neutral), w 0..1, as the game |
  | vertices | N x 12 | s32 u, s32 v in Q16 (v from the top as in CTXT, clamped to +-64), u8 r, g, b, a: COL0 as stored, 255 without |
  | triangles | T x 8 | u16 a, b, c (counter-clockwise seen from outside), u16 material |

  Size 8 + 4 + 4 + sum (12 + width x height x 4) + 4 + 8 M + 20 + 6 N (P +
  morphs) + 12 N + 8 T. The wheels are not in it: the view shows them from
  `char-wheel`'s own file (`RLDPW3`), one run for the front and one for a
  rear wheel model; the rear one at the size of the build when it is scaled
  by the ratio of the two `@char fit` lines' "across before". The colour of
  a pixel is the "nr" program's: texel times corner colour times material
  colour, the mask leaving out what has alpha below one half; no light.
  `RLDPN2` (the same head, then only the turn frames 10, 0 and 20 per
  triangle corner; `RldMk_NativePreviewBlock`) is no longer written by
  make-char and stays only for the readers that do not know RLDPN3 yet. The
  older `RLDPN1` is no longer read: the preview asks for a new build.
- The self-test (`rldpack selftest`, also `ReloadStudio.exe --rldpack
  selftest`) builds fixed models made in the code and compares the
  SHA-256 of what comes out with fixed values (`RLDMK_GOLDEN_*`): PLAIN,
  SIZE_ICON, FIT, NO_WHEELS (built with `--shadow retail --exhaust
  retail`, so its bytes stay those from before the look), NO_WHEELS_LOOK
  (the same with the default look of `--wheels off`, auto/off), REDUCED,
  REMESH (an open driver with `--remesh on`), MASK, OBJ and the voices; the
  native model has its own (`RLDMK_GOLDEN_NATIVE_*`, among them
  NATIVE_OBJ_REDUCED: a native model beside a reduced CMDL). A golden
  changes only with a deliberate change of what it covers, in the same
  commit, never to make the test pass; on another compiler or runtime a
  different value shows a drift.

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

612 bytes, nothing else; there is no VRAM position in the chunk. Every
portrait is drawn semi-transparent: a CLUT word with the STP bit (0x8000)
halves what lies behind it, one without is opaque.

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
shows "CICN ignored" with the hash mismatch; the game loads the file and drops
only the portrait (log line `CICN ignored`).

What `rldpack make-char --icon <png>` does (`RldMk_MakeIcon`):

1. Reads the PNG with its own reader (`include/rldpng.inc`): color types 0,
   2, 3, 4 and 6, bit depths 1 to 16, interlaced (Adam7) or not, at most
   4096 x 4096 pixels.
2. Cuts it to 43:25 in the middle; the longer side loses the same on both
   ends (one pixel more at the end when the difference is odd). The menu
   tile shows only the top left 43 x 25 of the 44 x 26 texels: it draws the
   own portrait at the size of the template's (log line
   `[CTR Char] portrait geometry`).
3. Scales it to 43 x 25 with a box filter in integers, the colors weighted by
   their alpha. A texel that is less than half covered is transparent.
   Column 43 repeats column 42 and row 25 repeats row 24. A picture of
   exactly 43 x 25 is taken texel for texel; one of exactly 44 x 26 fills
   the whole chunk texel for texel, without the cut. A smaller picture is
   enlarged (rldpack notes `icon-small`).
4. Reduces the opaque texels to at most 15 colors (the same median cut and
   k-means as the model's palette). Entry 0 is transparent; a color that
   rounds to 0x0000 is written as 0x0421.
5. Builds the chunk twice, compares the bytes and runs CICN-1..3 on it. The
   chunk stays 44 x 26 (612 bytes).

Transparency in the PNG always arrives: the alpha of RGBA and gray+alpha
pixels, the tRNS alpha per palette index, and the tRNS color key of RGB and
gray pictures. A texel less than half covered becomes index 0 (transparent);
opaque black stays opaque (0x0421). A black box that is opaque in the PNG
is not background to this rule - `--icon-background corners` removes it.

Four options, each with a default that gives the same CICN as without it:

| Option | Values (default first) | What it does |
|---|---|---|
| `--icon-background` | `alpha`, `corners`, `auto` | `alpha`: the PNG's transparency only. `corners`: before step 2 the plain color around the subject also becomes transparent, filled from the four corners; the same color inside the subject, or closed off by its outline, stays, and so does a thin outline at the alpha edge. `auto`: `corners` only for a PNG without any transparent pixel |
| `--icon-tolerance` | `24`, 0..128 | how far a color may be from the background color, per channel, and still count as it |
| `--icon-fit` | `none`, `fit`, `fill` | `none`: the whole picture cut to 43:25 as above. `fit`: the subject (the pixels at least half opaque) whole, as large as a retail head (41 x 25, standing on the bottom row, centered across). `fill`: the subject fills that box, cut at the sides or above and below the face. One factor for both axes: never stretched |
| `--icon-frame` | `none`, `retail` | `retail`: after step 3, the retail frame and its dark half-transparent box (STP) behind the subject, where the subject leaves the texels transparent; two CLUT entries, so 13 colors for the subject |

All four need `--icon`. `rldpack make-char --machine` reports each with
`@value icon-background|icon-tolerance|icon-fit|icon-frame <word> <switch|default>`,
the removed background as `@char icon_background <pixels> <RRGGBB>` and the
place of the subject as `@char icon_place <x> <y> <w> <h>` (0 0 0 0 without
`--icon-fit`). `--icon-preview <prefix>` also writes `<prefix>-retail.bmp`,
the template's portrait, when the game's data is found next to the tool.
Reload Studio passes `--icon-fit fit` by default ("Fit like the game's
heads"); its "Make background transparent" passes `--icon-background corners`
and "Retail frame" `--icon-frame retail`, both off by default.

Without `--icon` the file has no CICN, and the game shows the template's
portrait.

What the game does with it (`platform/native_chars.c`,
`NativeChar_ReadIcon`): at start it reads the CICN of every file it loads
and runs CICN-1..3; a chunk it cannot read (hash, compression) or that breaks
a rule costs only the portrait. The first 20 entries of the roster (the
loaded files in sorted order) own a portrait slot in a strip of VRAM that no
retail or track path writes (x 256..511, y 266..295; layout in
`include/platform/native_chars.h`). Each time the driver select grid is
entered, the usable portraits are uploaded there, and a custom tile draws its
own portrait in the size of the template's (43 x 25 of the 44 x 26). A tile
without a usable CICN or beyond the 20 slots shows the template's portrait.
After the load line the game logs one line per file,
`[CTR Char] portrait <file>: own (slot <n>)` or the reason followed by
`the template's` (for example `CICN ignored - <reason> - the template's`).

In a race the portraits are uploaded to the same strip again
(`[CTR Char] portraits: <n> uploaded ...`), and the bound seat shows its own
portrait in the race HUD's ranking, the arcade results and the cup standings
(`NativeChar_SeatPortrait`); without a usable CICN these show the template's.
One line per load: `[CTR Char] hud portrait seat 0: own (slot <n>, <file>, tpage 0x<template's> -> 0x<own>)` or
`[CTR Char] hud portrait seat 0: the template's (<reason>, <file>)`. High
score lists and profiles keep the template's portrait.

### CMSK (`RldChar_CheckMask`) - experimental

An own mask model, drawn in place of the Aku Aku / Uka Uka model. Experimental:
`rldpack make-char --mask-model` writes it, Reload Studio has no field for it
yet. Source: the CMSK section of `include/rldchar.inc`.

| Offset | Type | Field | 1 |
|---|---|---|---|
| 0x00 | u16 | version | 1 |
| 0x02 | u16 | flags | 0 (reserved) |
| 0x04 | | model | the model in the frame of CMDL: u32 G, the body, u32 mapBytes, the pointer map |

Rules, in order:

| Rule | What must hold |
|---|---|
| CMSK-1 | at least 4 bytes and version 1 (a reader that does not know the version drops CMSK) |
| CMSK-2 | flags 0 |
| CMSK-3 | at most 16 KiB (verdict MODEL LIMIT) |
| then | every model rule of CMDL (section 4) on the model at 0x04, with these differences: exactly 1 animation of 1 frame, and at most `RLDCHAR_MASK_DRAW_BYTES_MAX` bytes of draw memory (see Limits) |

The retail mask is rigid in a race, so the own mask has one frame and no
poses. It has vertex colors only, no texture.

A CMSK that breaks a rule, has a wrong hash or a pointer map the game cannot
apply never refuses the file: only the own mask is dropped, and the retail
mask is shown (as for a broken CICN). The envelope bound of 256 KiB is wider
than CMSK-3 on purpose: a later, larger version is dropped as a mask, not as
a file. A CMSK that breaks an envelope rule (larger than 256 KiB, compressed,
size_stored not size_raw) refuses the whole file.

What `rldpack make-char --mask-model <model>` does (`RldMk_BuildMaskOnce`,
`RldMk_MaskStep`): the steps of a character, with these differences. The model
(PLY or OBJ as for `--model`, `--mask-up y|z` and `--mask-forward z|-z` as
`--up` and `--forward`) is one part: no kart, driver or steering wheel, no dummy, no
`--remesh`, no poses. It is fitted to the height of the retail mask (86.1
game units) times `--mask-size` (50..150 percent, default 100), reduced to at
most the triangles `RLDCHAR_MASK_DRAW_BYTES_MAX` allows (see Limits) and
centered where the retail mask's center lies. The chunk is built twice,
compared and checked with `RldChar_CheckMask`. `--mask` still decides which
of the two masks it is. `--mask-size`, `--mask-up` and
`--mask-forward` need `--mask-model`. Without `--mask-model` the file has no
CMSK and the same bytes as before.

`rldpack info` names it: `own mask <n> triangles, ... (CMSK)`, `none - the
retail mask (no CMSK)` or `the retail mask - CMSK ignored: <rule> <detail>`;
with `--machine` `@value own-mask`. `rldpack verify` prints `IGNORED CMSK`
for a rule finding; a CMSK whose hash does not match fails the file in
`rldpack verify`, while the game drops only the own mask.

What the game does with it (`platform/native_chars.c`, `NativeChar_ReadMask`):
at start it reads the CMSK of every file it loads, checks it with
`RldChar_CheckMask` and applies its pointer map; after the portrait line it
logs `[CTR Char] mask <file>: own (<n> triangles)` or
`[CTR Char] mask <file>: CMSK ignored - <reason> - retail` (only for a file
with CMSK). In a race only the model of the bound custom seat's mask is
swapped (`game/Vehicle/VehPickupItem.c`): the mask keeps its model index
(Aku Aku or Uka Uka, CHRI flags bits 1-2), so the beam, the sound, the music
and the invincibility stay retail, and the bots wear the retail masks. The
first own mask of each load logs
`[CTR Char] mask seat 0: own model from the file (<n> triangles)`.

### CVOI (`RldChar_CheckVoices`)

The driver's own voice: short clips for the moments a retail driver speaks.
Optional; without it the driver is silent. Source: the CVOI section of
`include/rldchar.inc`. All numbers little-endian, as in CHRI.

| Offset | Type | Field | 1 |
|---|---|---|---|
| 0x00 | u32 | version | 1 |
| 0x04 | u32 | rate | 22050, the sample rate of every clip (mono, signed 16 bit) |
| 0x08 | u32 | clipCount | 1..40 |
| 0x0C | u32 | reserved | 0 |
| 0x10 | 10 x 4 bytes | events | per event, in the order of the table below: u8 first, u8 count, u16 reserved 0 - the event's clips are [first, first + count) |
| 0x38 | clipCount x 28 bytes | clips | per clip: char[16] name, u32 offset, u32 frames, u16 volume, u16 reserved 0 |
| | | samples | signed 16-bit little-endian, clip after clip in table order |

A clip: `name` is printable ASCII (0x20..0x7E), 1..15 characters, then NUL up
to byte 15 - the file name without its extension, for the log only. `offset`
counts from the start of the chunk, `frames` is the number of samples, and
`volume` scales the clip (256 = as recorded; `rldpack` writes 256).

The events. 0..7 are the retail voice sets by index (`data.voiceID[]`,
`game/zGlobal_DATA.c`); 8 and 9 the two short sounds the retail driver plays
at once instead of a voice line (`game/HOWL/HOWL_Voiceline.c`):

| Event | Key | Retail | File names by default |
|---|---|---|---|
| 0 | boost | set 0: a fast boost | boost1..boost4 |
| 1 | hit | set 1: hit by a weapon, squashed, crashing into a driver or a wall | hit1..hit4 |
| 2 | spin | set 2: spinning out | spin1..spin4 |
| 3 | bigair | set 3: a boost on landing a jump | bigair1..bigair4 |
| 4 | drop | set 4: laying a mine or a potion | drop1..drop4 |
| 5 | shield | set 5: an attack the mask or shield takes | shield1..shield4 |
| 6 | passing | set 6: said by the driver who passes the player | passing1..passing4 |
| 7 | fire | set 7: firing a bomb, a missile or a warp orb, using a mask or the clock | fire1..fire4 |
| 8 | short-yes | the short sound in place of set 0 | yes |
| 9 | short-hit | the short sound in place of set 1 | hit |

Rules, in order (the first finding is the reason given):

| Rule | What must hold |
|---|---|
| CVOI-1 | at least 16 bytes; version 1; rate 22050; clipCount 1..40; reserved 0 |
| CVOI-2 | the event table lies in the chunk; per event count 0..4, reserved 0, and first is the sum of the counts before it (an empty event too) - so the events share the clips 0..clipCount-1 in order, each clip in exactly one event, and the counts add up to clipCount |
| CVOI-3 | the clip table lies in the chunk; per clip the name as above; offset even and not inside the tables; frames 1..77175 (3.5 s) for events 0..7 and 1..22050 (1.0 s) for events 8 and 9; volume 0..256; reserved 0 |
| CVOI-4 | the first clip starts at the end of the clip table, each next one where the one before ends, and the last one ends at the end of the chunk |

A CVOI that breaks a rule or has a wrong hash never refuses the file: only the
voices are dropped, and the driver is silent (as for a broken CICN). A CVOI
that breaks an envelope rule (larger than 6 MiB, compressed, size_stored not
size_raw) refuses the whole file. A new layout comes as version 2, which an
older reader drops the same way.

The bounds and why:

- 22050 Hz, mono, 16 bit: the retail voice lines are XA audio at 37800 Hz
  mono, 4-bit ADPCM (all 314 tracks of the game's voice folder measured);
  22050 Hz keeps speech clear at a little more than half the bytes of 44100 Hz,
  and the game mixes at 44100 Hz, an exact multiple.
- 3.5 s for a voice line: the longest retail voice line of sets 0..7 is
  2.99 s (256 lines of 16 drivers measured in whole XA sectors of 0.107 s; the
  median is 1.28 s, none is longer than 3.5 s; the longest line of the other
  retail sets is 3.41 s). The bound is the one rldpack already checked against.
- 1.0 s for a short sound: the bound rldpack already checked against; the
  length of the retail short sounds is not determined.
- 4 clips per event: retail has 2 per set; 4 give room for variety, and an
  event picks a clip the way retail picks a line of the set.
- Size: the rules allow at most 5293176 bytes (32 clips of 3.5 s, 8 of 1.0 s,
  the tables); the envelope bound of 6 MiB even holds 40 clips of 3.5 s
  (6175176 bytes). The game holds the bytes only for a bound seat (see
  below), at most one chunk per bound file. Reading and checking such a chunk
  (SHA-256 and CVOI-1..4) took about 15 ms on the measuring PC (`rldpack
  verify` of a file with the largest CVOI, warm file cache: 70 ms against 42
  ms without voices, the chunk read twice); the game pays it at start and
  again when it binds the driver, in the load screen.

What `rldpack make-char --voices <folder>` does (`RldMk_PackVoices`): every
file of the folder (the first 64 by name) is listed; a `.wav` (PCM 8/16/24
bit or float 32, 1 or 2 channels, 8000..48000 Hz) or `.vag` (PS1 ADPCM,
decoded, never encoded) goes to the event its name says (case does not
matter; a WAV wins over a VAG of the same name). `--voice
<file>=<event|none>` (up to 64 times; the last one for a file wins) puts one
file into an event by its key, or takes it out; it is split at the last `=`,
so a file name may hold one. `voice-double` is said only when two files still
share a name after that: neither of them got an event of its own. `--voice
<file>=none` on the file that wins such a name leaves the other file unused
too: it gets an event only by a `--voice` of its own. The clips of an event are ordered by file name (in lower
case, then as written); more than 4 stop the build (`voice-too-many`). Each
clip is made into what the game plays: 16 bit, stereo mixed to mono (the
mean, rounded down), converted to 22050 Hz by a windowed-sinc low-pass
(a file at 22050 Hz keeps every sample), cut after 3.5 s or 1.0 s
(`voice-length-line`, `voice-length-short`), and with `--voice-normalize` its
peak set to -1 dBFS (29204) - unless the peak is below -40 dBFS (328): such a
clip stays as it is, lifting near silence or noise to full level helps no
one (`voice-quiet`). Integers throughout but the filter taps, which
are rounded once: the same files give the same bytes. The chunk is checked
with `RldChar_CheckVoices` before it is written. A file of an event that
rldpack cannot read stops the build (`voice-wav`, `voice-vag`), and so do a
wrong `--voice` (`voice-assign`), a voice folder that cannot be opened
(`voice-folder`; more than 64 files there are a warning with the same code),
more than 4 clips in an event (`voice-too-many`) and packed voices that do not
pass the game's check (`voice-pack`, an internal error). The other findings
are warnings or notes: `voice-silent`, `voice-quiet`, `voice-clip`,
`voice-double`, `voice-unknown`, `voice-other`, `voice-stereo`,
`voice-same`, `voice-source`, `voice-missing`, and `voice-preview` when a
preview cannot be written. `voice-clip` counts the samples at full level of
the clip as the game plays it, before normalizing (which keeps the
distortion), the ones the resampler had to clamp included; it is a warning
from 16 such samples on (audible), a note below - a few lone samples at full
level are common in exports and not heard. `--voice-preview <prefix>` writes every
readable file of the folder as the game plays it, `<prefix>-voice-<n>.wav`
(n counts the folder's files from 1 by name). Without a file in an event no
CVOI is written; without `--voices` the file has no CVOI and the same bytes
as before.

With `--machine`, per file of the folder `@file voice <state> <name> <bytes>`
and `@voice <name> <state> <event|-> <ms> <bytes> <rate> <channels>
<peak_permille> <preview|->` (state ok, cut, bad, unknown, unused or
ignored; ms, rate, channels and the peak - per mille of full scale, before
normalizing - of the file as given), then `@voiceevent <key> <count>` for all
10 events and `@char voices <clips> <events with a clip>`.

`rldpack info` names it: `voices <n> sounds in <m> of 10 events, <s> s:
boost <n>, ... (CVOI)`, `none - the driver is silent (no CVOI)` or `none -
the driver is silent; CVOI ignored: <rule>: <detail>`; with `--machine`
`@value own-voices <ok|none|ignored> <text>`, and for usable voices the
`@voiceevent` and `@char voices` lines. `rldpack verify` prints `IGNORED
CVOI` for a rule finding; a CVOI whose hash does not match fails the file in
`rldpack verify`, while the game drops only the voices.

What the game does with it (`platform/native_chars.c`,
`game/HOWL/HOWL_Voiceline.c`, `platform/native_audio.c`):

- At start it reads the CVOI of every file it loads, checks it (hash and
  `RldChar_CheckVoices`) and lets the bytes go again; only the clip table
  stays. One line per file, after the draw bytes line:
  `[CTR Char] voices <file>: <n> clips - boost <n>, hit <n>, spin <n>, bigair <n>, drop <n>, shield <n>, passing <n>, fire <n>, short-yes <n>, short-hit <n>`
  (all 10 events, 0 included), `[CTR Char] voices <file>: none - silent` or
  `[CTR Char] voices <file>: CVOI ignored - <reason> - silent`.
- When it binds a seat (in the load screen of a race) it reads the CVOI of
  that file again, checks it again and holds the bytes until the seats are
  cleared - so memory is taken only for bound seats. A file changed or
  removed since the start costs only the voices of that race, with a second
  line `[CTR Char] voices <file>: CVOI ignored - <reason> - silent`.
- A bound seat without usable voices is silent, as before: no random number
  is drawn for it.
- A bound seat with voices keeps the retail decision: the same random
  numbers, chances, cooldowns, queue and boss race branch, and the
  template's line count for `rng % num`. Only the source changes. A voice
  line plays clip `rng % count` of its event with the same random number;
  the cooldown comes from the clip's length in the retail unit (sectors at
  150 per second, divided by 5, plus 30 frames). Set 0 at once gives
  short-yes, set 1 short-hit; a short sound plays only where retail would
  play the template's own short sound - that sound must be loaded, at the
  level retail gives it. For which templates that holds is not determined
  from the code; Reload Studio builds every character on template 14 (Fake
  Crash). An event
  without a clip is silent (a voice line then has the cooldown of 30 frames
  of a failed XA line). The template's voice is never played for the seat,
  and the sample of the voice volume slider stays silent for it.
- Two PCM places play the clips, mono 22050 Hz to 44100 Hz by linear
  interpolation, times `volume` / 256. A voice line takes the place of an XA
  voice line, at its volume: a new XA, a stop, pause, the end of a level and
  a restore end it as they end an XA line. A short sound has a place of its
  own beside it, at the level retail gives the template's short sound: a new
  short sound replaces the one playing, and it does not cut the line. Quick
  states and replays do not hold either: after a restore both are silent.
  Without a bound seat with voices nothing of this runs.
- Per clip: `[CTR Voice] seat <s> event <key> clip <name> (<i> of <n>)`;
  without a clip: `[CTR Voice] seat <s> event <key>: no clip - silent`.
- Retail facts that hold for the custom driver too: passing (set 6) is said
  by the driver who passes the player, so a custom driver on the player's
  seat in a one-player race never says it; fire and drop are said only for
  a human driver; set 8 has no caller and is not an event.

### Driver animations from shape keys (glTF)

A model read from glTF 2.0 (`.glb`, `.gltf`; `tools/rldpack_gltf.inc`) may
carry shape keys (morph targets) on the driver's meshes. Their names, case
ignored: `steer_left`, `steer_right`, `reverse`, `crash`, `jump`, `win`,
`lose`. The base mesh is the neutral pose. A name the packer does not know
is said (warning `pose-unknown-key`, `@char pose-unknown <name>`) and left
out; it changes no byte. A glTF with a skin is refused (error `model-rig`:
"Rigs are not supported yet, please use shape keys") and no file is written.
OBJ and PLY have no shape keys: their files stay byte for byte as before.

THE 47 FRAMES (make-char step 4k, `RldMk_StepKeys` in
`tools/rldpack_char.inc`): after the model is placed and before the
reduction, every key becomes a delta per position of the chain (the mean of
the deltas of the points welded into it, through the linear part of the
axes, fit and `--scale`; `--size` scales them with their positions). Step 8
then writes frame = base + w(k) x delta for a slot with its key, the
automatic pose of today for a slot without. The weights follow the retail
curves that drive the automatic poses:

| Key | Slot | Weight |
|---|---|---|
| steer_left | turn frames 0..9 (frame 0 full left) | the retail turn curve normed to 1 at full lock (`s_rldDumRetailTurn[k] / 45.2`) |
| steer_right | turn frames 11..20 (frame 20 full right) | the same, mirrored; frame 10 stays neutral |
| reverse | reverse frames 0..6 | k / 6 |
| crash | bump frames 0..14 | the retail pitch curve normed to its peak (`s_rldMkBumpPitch[k] / -20`, its sign kept) |
| jump | jump frames 0..3 | k / 3 |
| win, lose | none | they never move a frame: they go to the native model only (MRPH) |

The kart stays rigid: a key that moves kart positions is said (warning
`pose-kart-moved`) and its kart part left out, so wheels, shadow and exhaust
keep their places. CMDL (step 9, its hull taking every frame and base +
delta of win and lose) and POSN of the native model get the same frames:
the classic model and the native one move alike, and a build before these
keys reads the file as any other. A key that makes the model too large for
the hull is the error `model-scale`.

THE MIRROR RULE: with only `steer_left` (or only `steer_right`) the other side
is its mirror image (x negated) when at least 98 % of the driver's positions
find a partner at (-x, y, z) within 0.5 % of the model's width (info
`pose-mirrored`, status `mirrored`). Else the key is dropped, the turn keeps
the automatic lean and the warning `pose-not-symmetric` says "model is not
symmetric, using the automatic lean".

NOT USED: with `--poses still` (note `pose-still`), with `--remesh on`
(`pose-remesh`) or without a driver part (`pose-no-driver`) the slots keep
their automatic poses.

MACHINE LINES (make-char, also with `--check`; `char-poses --model <glb>`
prints the same): one line per name, always in this order -
`@pose <steer_left|steer_right|reverse|crash|jump|win|lose>
<from-file|automatic|mirrored|neutral|error> <rule|->`; `neutral` is win or
lose without a key, `error` names its rule (for example
`pose-not-symmetric`). With keys also the text line `shape keys ...`.

`rldpack char-poses --model <ply> --pose-dir <folder>` still checks a pose
folder of PLY files (rules `pose-*`) and writes only a preview file for
Reload Studio; `char-poses --model <glb>` reports the keys of a glTF. It
refuses `--out` and never writes a container.

### Preview, planned: CNET and CTXT, the native model

PREVIEW. Two optional chunks for the native render path of a custom
character: `CNET`, a mesh in floats (positions, normals, UV, materials,
texture indices, wheels), and `CTXT`, the table of its textures (PNG, decoded
to RGBA8). The game reads them only with the native preview: NATIVE DRIVERS
set to PREVIEW on the GRAPHICS page (OPTIONS; saved in `ctr-settings.cfg` as
`video nativedrivers 1`, default OFF, applies at the next start) or the start
switch `--native-preview` (for that run, never saved). Below, "with
`--native-preview`" means either. It reads, checks and holds them in host
memory, makes them into GPU objects at the load and draws them in place of
the CMDL where its native route allows (`platform/native_char_gpu.c`,
`platform/native_render_layer.c`), at an internal resolution (RESOLUTION) of
2X or more - at 1X the CMDL draws - and only with the kart wheels hidden or
with wheels of its own (WHLS version 2 or 3; see "What the game does with
them"). The layout is a draft and may change until the native path is
released. Source: the section CNET AND CTXT at the end of
`include/rldchar.inc`.

No feature bit, no new major, no new minor: an older reader skips both as
unknown chunks, and every `.rldchar` without them loads as before.

THE ENVELOPE DOES NOT KNOW THEM. `RldChar_ChunkLimit` does not name CNET and
CTXT, so `Rld_OpenAs` treats them like any unknown chunk (steps 14, 18, 19).
Only these findings refuse the whole file as DAMAGED - the same an older
reader checks, with or without `--native-preview`:

| Finding (envelope, every reader) | Text | Result |
|---|---|---|
| step 14: CNET or CTXT lies outside the data area | `a chunk lies outside the data area` | the file is refused (DAMAGED), as today |
| step 18: CNET or CTXT twice (or another type twice) | `a chunk type appears twice` | the file is refused (DAMAGED), as today |
| step 19: CNET or CTXT overlaps another chunk | `two chunks overlap` | the file is refused (DAMAGED), as today |

Everything else costs only the native part: the driver keeps its CMDL and
drives as before. This deviates on purpose from the earlier plan, in which a
reader that knows CNET/CTXT would refuse the whole file for a chunk over its
bound:

| Finding (only the reader of the native part, only with `--native-preview`) | Rule | Result |
|---|---|---|
| compression method not 0, size_stored != size_raw, larger than `RLDCHAR_LIMIT_CNET` / `RLDCHAR_LIMIT_CTXT` (32 MiB), unreadable, hash mismatch | `native-chunk` | native part refused, CMDL drawn |
| the file changed since the start (its CMDL hash), the file cannot be opened | `native-file` | native part refused, CMDL drawn |
| any rule CNET-1..13, CTXT-1..8 below | the rule | native part refused, CMDL drawn |

Without `--native-preview` CNET and CTXT are not read and not checked at all,
not even a broken one: the roster read at start never opens them, and its
checks, its log lines and the picture are those of a build without them.

UNITS AND AXES of CNET: the model units of CMDL without the instance scale
(game units: (pos + byte) x ModelHeader.scale / 4096), +X left seen from
behind, +Y up, +Z forward, triangles counter-clockwise seen from outside. All
numbers little-endian, floats IEEE 754 binary32. (Decision D17 of the
renderer concept: this proposal is what the reader checks today.)

#### CNET version 1

| Offset | Type | Field | Version 1 |
|---|---|---|---|
| 0x00 | u16 | version | 1 |
| 0x02 | u16 | flags | 0 |
| 0x04 | u16 | headerBytes | >= 0x40, a multiple of 4; the section table follows |
| 0x06 | u16 | sectionCount | 1..16 |
| 0x08 | u32 | vertexCount N | 3..20000 |
| 0x0C | u32 | triangleCount T | 1..30000 |
| 0x10 | u16 | indexSize | 2 or 4 |
| 0x12 | u16 | materialCount M | 1..64 |
| 0x14 | u16 | poseCount | 0 (one still pose for every frame) or 47 (the CMDL frames in order: turn 21, reverse 7, bump 15, jump 4) |
| 0x16 | u16 | poseEncoding | 1: float position and float normal per vertex |
| 0x18 | f32[3] | hullMin | every pose position lies inside hullMin..hullMax |
| 0x24 | f32[3] | hullMax | |
| 0x30 | u16 | textureCount | the entries CTXT must hold; 0 = no CTXT |
| 0x32 | u16 | reserved | 0 |
| 0x34 | u32[3] | reserved | 0 |

Section table at headerBytes, sectionCount entries of 16 bytes: char[4] tag,
u16 version, u16 stride, u32 offset (from the start of CNET, a multiple of 4),
u32 size. An unknown tag is skipped.

| Tag | Status | Stride | Content |
|---|---|---|---|
| POSN | required | 24 | max(1, poseCount) x N x {f32 position[3], f32 normal[3]} |
| TRIS | required | 3 x indexSize | T x 3 vertex indices |
| TMAT | required | 2 | T x u16 material |
| MATL | required | 16 | M x {u8 rgba[4], s16 texture (-1 = none), u8 alphaMode (0 opaque, 1 mask, 2 blend), u8 flags (bit 0: always nearest), u8 reserved[8] 0} |
| UV00 | optional; required when a material has a texture | 8 | N x f32[2] |
| COL0 | optional | 4 | N x RGBA8 |
| WHLS | required unless CHRI hides the wheels (flags bit 0) | 0 | the wheels, below |
| MRPH | optional (version 1; another version is skipped like an unknown tag) | 0 | the shape keys win and lose, below |

WHLS, the wheels (one mesh for all four). Size exactly 0x30 + 32 Nw + 6 Tw.
The version of its section entry (not a field of the section) tells two
kinds apart; the layout is the same byte for byte (`RLDCHAR_WHEEL_VERSION_*`):

| Version | Kind | The -X wheels | Written by |
|---|---|---|---|
| 1 | the test wheel (a grey cylinder of 64 triangles) | the same mesh turned 180 degrees about Y, never mirrored | the self-test's files and `--native-probe` only; `make-char` never writes it for a character |
| 2 | an author's wheel | the mirror image of the mesh: x negated, the normals mirrored (nx negated), the winding turned - the outer side (+X) is outside on both sides, and a tread runs mirrored | `make-char --wheel-model` |
| 3 | an author's wheel as 2, with a rear wheel of its own, axle offsets or "always draw" | as 2, for the front and the rear mesh | `make-char --wheel-model` with `--rear-wheel-model`, `--axle-front`, `--axle-rear` or `--wheels-always on` |

Drawing the wheels, the turn of version 1 and the mirror of version 2, is the
game's part; the reader only checks. A reader from before version 2 (before
v0.7.5) refuses a version 2 section by CNET-10 (`WHLS has version 2 - this
reader knows 1`) and keeps the CMDL: the file stays valid and safe. A reader
from before version 3 (0.7.5 and the builds before the night of 09./10.10.)
refuses a version 3 section the same way (`WHLS has version 3 - this reader
knows 1 and 2`, measured with rldpack af24de8 `verify`): CMDL with the retail
kart wheels.


| Offset | Type | Field |
|---|---|---|
| 0x00 | u32 | vertexCount Nw, 3..2048 |
| 0x04 | u32 | triangleCount Tw, 1..1024 |
| 0x08 | u16 | material (< M) |
| 0x0A | u16 | flags: 0 in versions 1 and 2; version 3: bit 0 always draw (`RLDCHAR_WHEEL_ALWAYS_DRAW`, no level of detail), bit 1 a rear mesh follows (`RLDCHAR_WHEEL_REAR_MESH`), bits 2-15 0 |
| 0x0C | f32 | radius, 0..64 model units |
| 0x10 | f32 | halfWidth, 0..radius |
| 0x14 | f32[3] | front: the center of the front wheel on +X, model units (version 3: with its axle offset) |
| 0x20 | f32[3] | rear: the center of the rear wheel on +X (version 3: with its axle offset) |
| 0x2C | u32 | reserved 0 |
| 0x30 | Nw x 32 bytes | f32 position[3], f32 normal[3], f32 uv[2]: center at the origin, axle along X, outside +X |
| then | Tw x 6 bytes | u16 a, b, c |

Version 3 goes on behind the mesh, at E = 0x30 + 32 Nw + 6 Tw rounded up to 4
(the bytes between are 0). Size exactly E + 0x30 + 32 Nr + 6 Tr:

| Offset | Type | Field |
|---|---|---|
| E+0x00 | f32[3] | axleFront {dz forward, dy up, dtrack the whole track wider}: as the author gave them, model units; the wheel points hold them already, a reader never adds them again |
| E+0x0C | f32[3] | axleRear, the same |
| E+0x18 | u32 | rearVertexCount Nr: with the rear flag 3..2048, else 0 |
| E+0x1C | u32 | rearTriangleCount Tr: with the rear flag 1..1024, else 0 |
| E+0x20 | u16 | rearMaterial (< M), else 0 |
| E+0x22 | u16 | reserved 0 |
| E+0x24 | f32 | rearRadius 0..64, else 0 |
| E+0x28 | f32 | rearHalfWidth 0..rearRadius, else 0 |
| E+0x2C | u32 | reserved 0 |
| E+0x30 | Nr x 32, then Tr x 6 | the rear mesh, as the front one |

The bounds of the offsets (`RLDCHAR_AXLE_*`, model units; the retail wheel
has radius 16): dz -32..32, dy -16..32, dtrack -32..64. Without the rear flag
the rear wheels draw the front mesh, as in version 2. The reader hands it
on: `wheelFlags`, `wheelExtra`, `wheelRearVertexCount`,
`wheelRearTriangleCount`, `wheelAxle[2][3]`; `RldChar_WheelIsOwn` (version 2
or 3) and `RldChar_WheelMesh` (front, or rear: its own mesh or the front
one) are the reads for the game and the views.

MRPH, the shape keys win and lose (version 1, stride 0; rule CNET-13):

| Offset | Type | Field |
|---|---|---|
| 0x00 | u16 | targets: bit 0 win, bit 1 lose; 1..3 |
| 0x02 | u16 | flags: bit 0 normal deltas follow; bits 1-15 0 |
| 0x04 | u32 | vertexCount: N of the head |
| 0x08 | u16 | basePose: 10 (turn frame 10, no steering) with 47 poses, 0 for a still model (`RLDCHAR_NET_NEUTRAL_POSE`) |
| 0x0A | u16 | reserved 0 |
| 0x0C | u32 | reserved 0 |
| 0x10 | | per target present, win before lose: N x f32[3] position deltas; with flags bit 0 then N x f32[3] normal deltas |

Size exactly 0x10 + targets x N x 12 x (1 + normals). The pose of a target
is POSN[basePose] + delta; its normal POSN[basePose] + the normal delta, of
length 1. `RldChar_MorphTarget` and `RldChar_MorphVertex` (the blend base +
t x delta) are the reads for the game and the views. A reader before MRPH
(rldpack and the game at af24de8, 0.7.5) skips it as an unknown tag - CNET-4
holds it to the place rules of every section (4-aligned, behind the table,
inside the chunk, no overlap, at most 16 sections) and CNET-5 does not look
at it - so such a reader shows the native model without win and lose
(measured with rldpack af24de8 `verify`: ready). Budget at N = 20000 with
both targets and normals: 16 + 2 x 20000 x 24 = 960 016 bytes; a whole CNET
at N 20000, 47 poses, T 30000 then about 24.2 MB of the 32 MiB.

| Rule | What must hold |
|---|---|
| CNET-1 | at least 0x40 bytes, version 1 (a reader that does not know the version drops the native part) |
| CNET-2 | flags and reserved fields 0; headerBytes >= 0x40, a multiple of 4, inside CNET; poseEncoding 1; indexSize 2 or 4 |
| CNET-3 | N 3..20000, T 1..30000, M 1..64, poseCount 0 or 47, textureCount <= 16, N fits indexSize |
| CNET-4 | sectionCount 1..16, the table inside CNET; every section 4-aligned, behind the table, inside CNET; no tag twice; no two sections overlap |
| CNET-5 | a known section has version 1, its stride and exactly the size its counts need; POSN, TRIS, TMAT and MATL are there |
| CNET-6 | the head's hull finite and not inside out; every pose position finite and inside it; every normal finite and of length 1 (length^2 0.98..1.02); every UV finite |
| CNET-7 | every index < N, no triangle with a vertex twice, every triangle material < M |
| CNET-8 | materials: alphaMode <= 2, flags bit 0 only, reserved bytes 0, texture -1 or < textureCount; UV00 present when a material has a texture |
| CNET-9 | every pose inside the hull of its CMDL frame (a still pose inside all 47): per axis pos..pos + 255 of the frame (pos.y with bit 0 cleared) times scale / 4096 |
| CNET-10 | CHRI hides the wheels: WHLS is not looked at. Else WHLS is there and holds: version 1, 2 or 3, stride 0, its counts and exact size, material < M, flags (0 in versions 1 and 2, bits 0-1 in 3) and reserved 0, radius and halfWidth in range, the wheel points finite, within 256 model units, on +X and front before rear, every vertex finite and inside the cylinder (with 1e-4 of room), unit normals, every index < Nw and no vertex twice. Version 3 also: the fill bytes 0, the axle offsets finite and within their bounds, the rear fields 0 and nothing behind the extension without the rear flag, with it the rear mesh as the front one (counts, exact size, material, radius, cylinder, normals, indices) |
| CNET-11 | textureCount equals the entries of CTXT; textureCount 0 needs no CTXT, and a CTXT then is a finding |
| CNET-12 | WHLS version 2 or 3 only (an author's wheel), checked after CTXT-1..8: its material (the field at 0x08; version 3 with a rear mesh also rearMaterial) has no texture - the wheel is one color - or a texture of at most 1024 x 1024 (`RLDCHAR_WHEEL_TEXTURE_EDGE_MAX`). The material and its texture may be the body's as well; the bound then holds for that texture. Version 1 is not held to it |
| CNET-13 | MRPH version 1 only (another version is skipped): stride 0, targets 1..3, flags bit 0 only, reserved 0, vertexCount N, basePose 10 (0 for a still model), the exact size; every number finite; every target position (base + delta) inside the head's hull and inside the box of the 47 CMDL frame hulls (the room the driver takes in its own model: the game shows win and lose at any CMDL frame and culls and sorts by it); with normals every base normal + delta of length 1 (length^2 0.98..1.02) |

The reader hands the version on: `struct RldCharNative.wheelVersion` is
`RLDCHAR_WHEEL_VERSION_TEST` (1), `RLDCHAR_WHEEL_VERSION_USER` (2) or
`RLDCHAR_WHEEL_VERSION_USER3` (3) when `wheel` is set, 0 when there is no
wheel (none, or CHRI hides them).

THE BUDGET OF AN AUTHOR'S WHEEL (version 2): one mesh of at most 1024
triangles and 2048 vertices (CNET-10, the same as version 1), drawn four
times; one material; a texture of at most 1024 on each side (CNET-12). The
wheel's texture is one of the 16 entries of CTXT and counts to the 64 MiB of
RGBA8 with every mip level (CTXT-6), together with the body's; a texture of
the same bytes as one of the body shares its entry. Over any bound the packer
refuses with a message and writes no file - it never reduces a wheel or
scales its texture.

A native body is never drawn with the sprite wheels: without a usable WHLS
(and without CHRI flags bit 0) the whole native part is refused. The game
goes one step further (see "What the game does with them"): it uses a native
part only when CHRI hides the kart wheels or when WHLS is an author's wheel
(version 2), so the test wheel (version 1) is never drawn on a driver of a
file.

THE FALLBACK CHAIN of the wheels (renderer wheel concept, section 6). A
player never sees the grey test wheel on a driver of a file:

| Case | Result |
|---|---|
| CHRI hides the wheels (flags bit 0) | native body without wheels; WHLS is not read |
| wheels shown, WHLS version 2 valid (CNET-10, CNET-12) | the reader hands on the native body with the author's wheels (`wheelVersion` 2); the game draws the body with them (below) |
| wheels shown, no WHLS | CNET-10 refuses the native part: CMDL with the retail kart wheels |
| wheels shown, WHLS version 1 (the test wheel; old files of the preview) | the game refuses the native part (`native-wheels`): CMDL with the retail kart wheels |
| WHLS version 2 breaks CNET-10 or CNET-12, its texture over 1024 or broken (CTXT-n) | the whole native part is refused (all or nothing): CMDL with the retail kart wheels |
| an older reader (before v0.7.5) and WHLS version 2 | CNET-10 (version): CMDL with the retail kart wheels |
| wheels shown, WHLS version 3 valid (CNET-10, CNET-12) | the reader hands on the native body with the author's wheels (`wheelVersion` 3, `RldChar_WheelIsOwn`); the game draws them with the rear mesh, the offsets in the wheel points and "always draw" once it reads version 3 - until then it refuses the part with `native-wheels`: CMDL with the retail kart wheels |
| a reader before version 3 (0.7.5) and WHLS version 3 | CNET-10 (version): CMDL with the retail kart wheels |
| MRPH broken (CNET-13) | the whole native part is refused: CMDL with the retail kart wheels |
| a reader before MRPH, or MRPH of another version | MRPH skipped: the native model without win and lose |
| `make-char` | never writes version 1 for a character; `--native-model on` with the kart wheels shown needs `--wheel-model`; a wheel over a bound is an error and no file; version 3 only when a switch of it changes something, else version 2 byte for byte |
| the game without the native preview (NATIVE DRIVERS OFF, no `--native-preview`), or at a resolution of 1X | CNET and CTXT are not used: CMDL with the retail kart wheels |
| a view that has no wheel pose for the driver (the game) | that view: CMDL with the retail kart wheels |

Old files load unchanged: a file without CNET, with CNET and the wheels
hidden, or with the test wheel (version 1) is read exactly as before.

THE GAME DRAWS an author's wheel (WHLS version 2) in the item of the native
body, right after it and with its depth: the mesh in model units scaled by
driver->wheelSize / 4096 (the size the retail wheel has, radius 16 model
units at 100 %; at rest the same scale as the body), its middles through
the body's matrix (they follow squash and stretch, the mesh keeps its size),
turned by the steering of the front wheels, the wobble and the roll of the
retail values; the roll uses the wheel's own radius. The -X wheels are drawn
from the mirror image of the mesh (x negated, winding turned), the +X ones
from the mesh. Its material, texture (with mip levels) and tint follow the
body, also on the two sides of a water line and in a mirror. A view that has
no wheel pose for the driver is drawn by the CMDL with the retail wheels; a
native body is never drawn with the sprite wheels. Without the native
preview, or at factor 1, the driver drives its CMDL with the retail wheels.
Sources: `platform/native_wheels.c`, `platform/native_char_gpu.c`
(the mirrored half), `platform/native_render_layer.c`.

Deliberate differences from the retail wheels:

- No LOD: retail leaves the tyres out past the LOD threshold
  (`game/DrawTires.c:829-850`; with three or four players already from the
  first coarser header). An author's wheels are always drawn, like the
  native body, which is not reduced either.
- Dimmed with the body: the retail tyre sprites keep their tyre colour in a
  mirror and under water (`scratch->tireColor`, `game/DrawTires.c:867`).
  An author's wheels follow the tint of the body, so in a mirror and below
  a water line they are as dark as the body there - darker than retail
  tyres would be.
- The tyre flicker of the bots (a darker tyre colour) is not rebuilt: an
  author's wheels roll.

#### CTXT version 1

| Offset | Type | Field |
|---|---|---|
| 0x00 | u16 | version 1 |
| 0x02 | u16 | flags 0 |
| 0x04 | u16 | count, 1..16 |
| 0x06 | u16 | entryBytes, 0x40 |
| 0x08 | u32[2] | reserved 0 |
| 0x10 | count x 0x40 | the entries |

| Entry offset | Type | Field |
|---|---|---|
| 0x00 | u8 | format: 1 = PNG |
| 0x01 | u8 | reserved 0 |
| 0x02 | u16 | flags: bit 0 linear (clear = sRGB); bits 1-2 wrap U, bits 3-4 wrap V (0 repeat, 1 clamp, 2 mirror); bit 5 always nearest; bits 6-7 alpha (0 opaque, 1 mask, 2 blend); bits 8-15 0 |
| 0x04 | u16 | width, a power of two 16..2048 |
| 0x06 | u16 | height, a power of two 16..2048 |
| 0x08 | u32 | offset of the PNG bytes, from the start of CTXT |
| 0x0C | u32 | size of the PNG bytes |
| 0x10 | u8[32] | SHA-256 of the PNG bytes |
| 0x30 | u8[16] | reserved 0 |

| Rule | What must hold |
|---|---|
| CTXT-1 | at least 0x10 bytes, version 1 |
| CTXT-2 | flags 0, entryBytes 0x40, reserved 0, count 1..16, the table inside CTXT |
| CTXT-3 | format 1, reserved bytes 0, no unknown flag bit, no wrap or alpha value 3 |
| CTXT-4 | width and height powers of two, 16..2048 |
| CTXT-5 | the PNG bytes behind the table, at least 8, inside CTXT; no two entries overlap |
| CTXT-6 | all textures with every mip level (max(1, w >> n) x max(1, h >> n) x 4) at most 64 MiB |
| CTXT-7 | the SHA-256 of every entry's bytes |
| CTXT-8 | the PNG starts with its IHDR, and that head says width x height, depth 8, color type 6 (RGBA) - checked before anything is inflated; then the PNG decodes (`include/rldpng.inc`) |

CTXT-3 to CTXT-6 run entry by entry, then CTXT-7 over all entries, then
CTXT-8: the budget is counted before any byte is hashed, the decoding comes
last, one texture after the other.

#### What the game does with them

| When | What |
|---|---|
| start (roster) | nothing: CNET and CTXT are not opened, with or without `--native-preview` |
| a seat is bound (load stage 5: seat 0, and every seat of `--dev-char-seats`), only with the native preview (NATIVE DRIVERS set to PREVIEW on the GRAPHICS page, or `--native-preview`) | the file is opened again from its path: its CMDL hash must still be the loaded one; CNET is read and checked (CNET-1..10 and 13) against the 47 frame hulls of the model in memory, then CTXT (CNET-11, CTXT-1..8, then CNET-12), the textures decoded to RGBA8; all of it is held in host memory until the seats are cleared (next load). In load stage 5, right after the seats are armed, a held part is made into GPU objects (`platform/native_char_gpu.c`; a blend material refuses the whole set and the seat draws its CMDL); the render layer then draws it in place of the CMDL where its native route allows |
| the driver select preview, only with the native preview | the file of the custom tile under the cursor is read and checked the same way and held for the preview window (one line with `<who>` `preview`) |
| a broken native part | one line, the driver keeps its CMDL |
| a native part that passes CNET-1..12 and CTXT-1..8 while CHRI shows the kart wheels (flags bit 0 clear) and WHLS is an author's wheel (version 2) | held with its own wheels and drawn natively with them (see "THE GAME DRAWS" above); its CMDL with the retail kart wheels stays the fallback |
| a native part that passes CNET-1..11 and CTXT-1..8 while CHRI shows the kart wheels (flags bit 0 clear) with the test wheel (WHLS version 1) | refused with the rule `native-wheels` and let go (`NativeChar_RefuseShownWheels`): the driver keeps its CMDL and the retail kart wheels. The game draws a native body only with the kart wheels hidden or with an author's wheel, so the grey test wheel is never uploaded or drawn - on a seat, on the dev seats and in the preview alike, also with `--dev`. Files from before the rule (a native part with the test wheel) drive as CMDL drivers |

The lines, exactly (`<who>` is `seat 0`, `dev seats`, `dev seat files` or
`preview`):

```
[CTR Char] native model ready: <who>, <file>, <N> vertices, <T> triangles, <P> poses, <M> materials, <K> textures (<B> bytes RGBA8), wheels hidden
[CTR Char] native model ready: <who>, <file>, <N> vertices, <T> triangles, <P> poses, <M> materials, <K> textures (<B> bytes RGBA8), wheels own v2, <Tw> triangles, <Nw> vertices, radius <r>, texture <i> <W>x<H>
[CTR Char] native model refused (native-wheels), using CMDL: <who>, <file>: the kart wheels are shown with the test wheel (WHLS version 1) - a native model is drawn only with the wheels hidden or its own wheels (WHLS version 2)
[CTR Char] native model refused (<rule>), using CMDL: <who>, <file>: <detail>
[CTR Char] native model none: <who>, <file> (no CNET), using CMDL
```

A ready line ends in `wheels hidden` or in `wheels own v2, ...` (with `one
colour` in place of the texture for a wheel without one): a part with the
test wheel is refused before its line. The generated test body of the game switch
`--native-probe` is no file and not touched by this rule (its wheels are a
measuring form of the probe).

None of them starts with `[CTR Char] REFUSED`: that line still means a file
the game did not load.

#### What rldpack writes and reads

- `rldpack make-char ... --native-probe <still|poses>` (preview, for tests)
  writes, after CVOI, a CNET with the test body of the native probe
  (`platform/native_probe.c`: 36 vertices, 18 triangles, its 256 x 256
  texture) fitted into the common room of the 47 frames of the model just
  built, with a wheel of 64 triangles (none with `--wheels off`), and a CTXT
  with the texture as PNG (8-bit RGBA, stored deflate blocks). Both are held
  to the game's rules before anything is written. Without `--wheels off` the
  game refuses the native part of such a file (`native-wheels`) and draws its
  CMDL with the kart wheels. Without the switch no byte of a container
  changes.
- `rldpack make-char ... --native-model on` (preview) writes, after CVOI, the
  native model of the author's own OBJ (below). Not with `--native-probe`.
  It needs `--wheels off` or `--wheel-model`: the game draws a native model
  only with the kart wheels hidden or with wheels of its own (WHLS version
  2, "A wheel of one's own" below). With the kart wheels shown and no
  `--wheel-model` make-char refuses the switch (error `native-model`,
  nothing written). Without the switch (or with `--native-model off`) no
  byte of a container changes: the OBJ reader hands the chain the same model
  either way, and the CMDL is the same.
- `rldpack make-native-tests <folder>` writes the test characters of the
  self-test (old_, none_, good_, bad_<rule>_, damaged_) into a folder that
  must lie inside a CMake build folder; the game judges them with
  `--dev --char-native-selftest <folder>` (ctest `char_native_selftest`).
  `rldpack make-native-tests --obj <folder>` writes the set of the self-test's
  mini OBJ instead: `old_obj-plain` and its native model with and without the
  test wheel (`good_obj-native`, `good_obj-native-wheels-hidden`), one CMDL
  for all three.
- `rldpack info` and `rldpack verify` name CNET and CTXT `(preview)` in the
  chunk list and add a line `native model` (`ready: ...`, `refused (<rule>)
  ... - the game draws the CMDL`, or `none`), only for a file that has one of
  them; `--machine` adds `@value native-model <ready|refused|none> <text>`.
  A ready line ends in `wheels hidden` or names the wheel: `wheels: WHLS
  version 2 (its own, <T> triangles, radius <r>)`, or `version 1 (the test
  wheel, ...)`.
  `verify` fails the file when a native chunk cannot be read or its hash does
  not match (as for CMSK), and says `IGNORED` for a rule finding.

#### The native model of an OBJ (`make-char --native-model on`)

Source: `tools/rldpack_native.inc`, section THE NATIVE MODEL OF AN OBJ. The
CMDL is built as without the switch (the required fallback); CNET and CTXT
are made from the same OBJ beside it and held to the game's rules before
anything is written. Only when the chain reduces the CMDL can the CMDL
differ from a build without the switch: its hull then also holds the native
frames (see below).

- Only an OBJ (a PLY has no texture coordinates): refused otherwise. A model
  the chain remeshed (`--remesh on`) is refused too - its hulls replace the
  faces, and the native vertices would not find their positions.
- A model the chain reduces (`--reduce auto` over the draw-memory limit):
  only the CMDL is reduced. The chain keeps the positions from before the
  reduction, moves them by `--size` and the poses as it moves its own, and
  takes those frames into the hull of each CMDL frame (so CNET-9 holds); the
  native model keeps the faces as written, up to its own limits below. The
  info `native-full` says so: "The game's own model was reduced (A -> B
  triangles); the native model keeps the faces as written." Without a
  reduction, or without `--native-model on`, nothing of this runs and the
  bytes are those of before.
- Place, axes, scale: every vertex sits where the chain put it - the OBJ
  vertex is found by its weld key among the points of the model, its welded
  position (after the repair) gives the neutral pose and the 47 frames: the
  axes of `--up`/`--forward`, the factor of `--fit crash` or `--scale`, the
  shift onto the reference dummy, `--size` and the automatic poses. Units are
  those of CMDL without the instance scale (game units), +X left seen from
  behind, +Y up, +Z forward (renderer concept D17, proposed). The turns are
  rotations, so the winding stays that of the OBJ (counter-clockwise seen
  from outside).
- Poses: 0 (one still pose) when every frame of the build is the neutral pose
  for every vertex (a vehicle with `--wheels off` that stands still,
  `--poses still`); 47 otherwise, frame by frame as the CMDL moves. A
  coordinate up to two steps of the CMDL (hull / 255) outside the hull of its
  frame - the CMDL rounds to those steps - is moved onto the hull (counted).
- Triangles: each face as a fan; one left out (counted) when two corners
  share a position or it has no area. The game culls back faces, so with
  `--repair auto` (the default) the native triangles go through the same
  repair as the CMDL's (`RldRep_Repair`, the chain's weld distance and hole
  limit): turned outward, the same holes closed (the lids cover the same
  loops; their diagonals may lie otherwise than the CMDL's, which was
  repaired on its own triangles), degenerate and doubled triangles dropped.
  A lid has the material of its corner 0 and, at every corner, the UV of
  corner 0 - one spot of the texture, nothing smeared across the hole (the
  CMDL colors its lids from the corner colors); position, normal and color
  stay those of each corner. A part still open afterwards - drawn two-sided
  in the CMDL - gets every triangle a second time, wound the other way with
  vertices of its own (CNET has no two-sided material); with
  `--open-parts one-sided` none. The open edges then match the CMDL's.
  `--two-sided` gives every triangle a second one, with or without the
  repair. `--repair off`: the faces as written and, but for `--two-sided`,
  one-sided - also the open parts the CMDL then draws from both sides.
  Vertices: one per (v, vt, vn, material). Normals per pose from the
  geometry, averaged over the faces that share v and vn (a hard edge of the
  OBJ keeps two vn).
- UV00: `offset + scale x vt` of the material's `map_Kd` (`-o`, `-s`,
  `-clamp on`), v turned (`1 - v`): CTXT rows run from the top.
- Colors: by the OBJ reader's rules without the texture; with vertex colors a
  COL0 (and every material white), else the material color is Kd, white with
  a texture, grey 0x80 without. Alpha 255; `d`/`Tr` are not used.
- CTXT: one entry per texture file in use (files with the same bytes share
  one), each side a power of two 16..2048,
  never scaled (refused, `native-texture-size`, with a hint). A PNG of 8-bit
  RGBA keeps its bytes; another picture (PNG of another kind, JPEG, TGA, BMP)
  is decoded and written as PNG of 8-bit RGBA. Flags: sRGB, wrap clamp when
  every UV on the texture lies in 0..1 (else repeat), alpha mode mask when a
  texel has alpha below 255 (the material too). A texture the reader could
  not use (missing, unreadable) leaves its material untextured in its color,
  as the CMDL draws it (warning `native-texture`).
- Wheels: `--native-model on` needs `--wheels off` (CHRI `NO_WHEELS`, no
  WHLS) or `--wheel-model` (the wheels shown, WHLS version 2: "A wheel of
  one's own" below). The probe's test wheel at the retail wheel points (64
  triangles, dark grey, WHLS version 1) is written only by `--native-probe`
  without `--wheels off` and by the self-test's files (`good_obj-native`),
  never for a character.
- Limits: 20000 vertices, 30000 triangles, 64 materials (with the wheel's),
  16 textures, 64 MiB RGBA8 with mip levels, 32 MiB per chunk; over one:
  `native-model` error with the numbers, nothing written.
- rldpack prints the CNET and CTXT lines, one line per texture and the counts
  of triangles left out and coordinates moved; `--machine` adds
  `@char native-model <vertices> <triangles> <poses> <materials> <textures>
  <wheels|hidden> <bytes of CNET and CTXT>`.
- Reload Studio: the card Import of the tab Extras, row "Native model", a
  preview feature open to everyone (marked "Preview feature"; a tick, empty
  at the start). Ticked, an OBJ is built with `--native-model on` while Show
  kart wheels is off or a wheel model is chosen (card Wheels, then with
  `--wheel-model`); with the kart wheels and no wheel model it is the classic
  model only, and the line below the options says so. In the user mode
  (`--enable-preview-features`) the same holds without the tick. Unticked,
  the command is the one from before, so the classic export keeps its
  bytes. The notes tell the author that the game draws the native model with
  NATIVE DRIVERS set to PREVIEW (OPTIONS, GRAPHICS).

#### A wheel of one's own (`make-char --wheel-model`, WHLS version 2)

Source: `tools/rldpack_native.inc`, section A WHEEL OF ONE'S OWN. Preview,
like the native model it belongs to.

- `--wheel-model <file.glb|file.gltf|file.obj|file.ply>`: one wheel for all four. The format by
  the content, as `--model`: a glTF (read into the same faces as an OBJ), an OBJ with its MTL file and texture through the
  same OBJ reader (its faces as written), a PLY as make-char reads one (no
  texture coordinates: one color). `--wheel-size <50..200>`: the radius in
  percent of the game's wheel (16 model units, `RLDDUM_TIRE_HALF_SIZE`;
  default 100). It lives only in WHLS (its radius): CHRI carries no wheel
  size (its layout above has none), and the game's own `wheelSize` is set
  alike for every driver (`game/Vehicle/VehBirth.c:624`,
  `VEH_BIRTH_WHEEL_SIZE` 0xccc; Oxide 0, :637).
- The switches: `--wheel-model` needs `--native-model on` and the wheels
  shown (error `wheel-model` beside `--wheels off`: a contradiction; error
  `wheel-model` without `--native-model on`); `--wheel-size` needs
  `--wheel-model` (error `usage`), a whole number 50..200 (error
  `wheel-size`). With `--wheel-model` CHRI keeps the wheels shown (no
  `NO_WHEELS`), so the CMDL - the fallback - drives with the game's kart
  wheels. Without the two switches no byte and no line of make-char changes.
- The wheel's axes (as `char-wheel`, `tools/rldpack_wheel.inc`): centre in
  the origin, the axle along X, the outer side toward +X, +Y up, +Z forward,
  after `--up` and `--forward` of the model (the same switches turn both).
  The mesh is centred on the middle of its box and scaled by one factor so
  that its farthest vertex from the axle lies at the radius r = 16 x
  `--wheel-size` / 100. It is not reduced and not repaired; a triangle
  without area is left out (counted).
- The wheel points are the retail ones (`game/DrawTires.c:338-367`, in model
  units: x 0x90 / 4 = 36, z 0xc7 / 4 = 49.75 in front and -0x60 / 4 = -24
  behind), the height r instead of 0x40 / 4 = 16: the bottom of the wheel
  stays on y 0, where the retail wheel (half size 16 at y 16) touches the
  ground - a larger wheel lifts its middle by the change of its radius
  (r 24 at 150 %: the middle at y 24, the bottom at 0). `--size` does not
  move them (it scales driver and steering wheel, never the kart), and the
  game's instance scale reaches them through the body's matrix as it reaches
  the retail points.
- The right-hand wheels (-X) are the mirror image of the mesh (WHLS version
  2): the game negates x and the normals' x and turns the winding, so the
  outer side (+X) is outside on both sides and a tread runs mirrored. The
  test wheel (version 1) stays turned about Y. Drawing is the game's part.
- One material (rule CNET-12): every face of an OBJ wheel has the same
  `usemtl` name, else error `wheel-materials`. Its texture is an entry of
  CTXT of at most 1024 on each side, a power of two from 16 (else
  `wheel-texture-size`, never scaled); a file of the same bytes as a body
  texture shares that entry. It counts to the 16 entries and the 64 MiB. Its
  color: Kd, else white with a texture, else grey 0x80. A missing texture
  leaves the wheel in that color (warning `wheel-texture`); one that is there
  but cannot be used - unreadable, not a PNG, JPEG, TGA or BMP, over 4096
  pixels on a side or 64 MiB - is an error (`wheel-texture-file`) and no file,
  never the color. A PLY wheel takes
  the mean of its vertex colors (info `wheel-colors`); vertex colors are not
  drawn. The wheel's material is the last of MATL; its texture makes UV00
  present for the body too (CNET-8).
- Limits: 1024 triangles (`wheel-triangles`), 2048 vertices - one per corner
  with its own v, vt and vn (`wheel-vertices`), vt at every corner of a
  textured wheel (`wheel-uv`), a wheel with area across and width
  (`wheel-flat`), at most as wide as across (`wheel-wide`; warning
  `wheel-off-axis` when its height and length differ by more than a fifth).
  Over a limit: the error, no file - never reduced, never scaled.
- Normals from the geometry (area-weighted, per v and vn; a PLY: per v), UV
  as the body's (`-o`, `-s`, `-clamp`, v turned).
- The wheel is built twice and compared, like the CMDL; then the whole native
  part is held to the game's rules (CNET-1..12, CTXT-1..8) before anything is
  written.
- Output: a line `wheel <file>: <T> triangles, <N> vertices, radius <r>
  (<p>% of the game's wheel), <w> wide, texture <i> (<W> x <H>) | one color
  <RRGGBB>; at x 36.00, y <r>, z 49.75 / -24.00 (the bottom on the ground),
  the right side mirrored`; `--machine`: `@value wheel-model <file> switch`,
  `@value wheel-size <p> switch|default`, `@file wheel-model ok|missing
  <file> <bytes>` and `@char native-wheel <triangles> <vertices> <radius>
  <half width> <percent> <WxH|color-RRGGBB>`. The OBJ reader's `@model`
  lines are those of the model only.

#### Rear wheel, axles and "always draw" (WHLS version 3)

Source: `tools/rldpack_native.inc`, section THE REAR WHEEL, THE AXLES AND
"ALWAYS DRAW". Preview, like the native model. Each switch needs
`--wheel-model` (error `usage`); Reload Studio passes them only when they
are set.

- `--rear-wheel-model <file.glb|file.gltf|file.obj|file.ply>`: a mesh of its
  own for the rear wheels, read, checked and fitted as `--wheel-model` (the
  same rules, budget and message codes; error `rear-wheel-model` names the
  file). It keeps its size against the front wheel as the two files have it:
  rear radius = front radius x the rear model's radius / the front model's,
  both measured as the fit measures them, at most 64 (`wheel-size`). Its
  material comes after the front wheel's, its texture is its own entry or
  shares one of the same bytes.
- `--axle-front <dz>,<dy>,<dtrack>`, `--axle-rear <dz>,<dy>,<dtrack>`: the
  axle forward, up and the track wider (the whole track: each wheel moves by
  half of it), model units rounded to 1/16, dz -32..32, dy -16..32, dtrack
  -32..64 (error `axle-range`). The wheel points become front (36 + dtrack /
  2, r + dy, 49.75 + dz) and rear (36 + dtrack / 2, r + dy, -24 + dz), r the
  radius of that wheel; the front axle stays in front of the rear one
  (`axle-order`, which the bounds already keep).
- `--wheels-always on|off`: on sets "always draw" (no level of detail);
  off, the default, leaves the wheels to the retail rule of the tyres.
- WHLS version 3 is written only when a switch changes something (a rear
  model, an offset other than 0, `--wheels-always on`); `--axle-front 0,0,0`
  or `--wheels-always off` alone keep version 2 and the same bytes.
- `--machine`, only for version 3: `@value wheels-version 3`, `@value
  axle-front <dz> <dy> <dtrack>`, `@value axle-rear ...`, `@value
  wheels-always on|off`, `@value rear-wheel <radius> <half width>`.

#### win and lose (MRPH)

When the model has the shape keys `win` or `lose` (see "Driver animations
from shape keys"), `make-char --native-model on` writes MRPH: each CNET
vertex takes the delta of its chain position from turn frame 10 (the same
weld as POSN), with the normals of the target from its smooth groups less
those of the base pose. A target is kept inside the box of the 47 frames
(the hull of step 9 already takes win and lose; a move back of more than two
quantization steps is the warning `morph-clamped`) and widens the head's
hull. `--machine`: `@value morph win|lose <vertices>`. Without the keys (OBJ,
PLY, a glTF without them, `--poses still`) there is no MRPH and CNET stays
byte for byte as before. The game blends from the pose it draws to win
(places 1-3) or lose (4-8) after the finish, natively only; the CMDL
fallback stays neutral.

### Planned once: CWHL, own wheels (reserved, not assigned)

SUPERSEDED: the wheels of the native model are the section WHLS of CNET
(floats, UV, material, texture index, wheel points in model units). CWHL
stays a reserved name and is not assigned; the plan below is kept for its
history only - its units (1/16 model unit with |x| <= 96, while the retail
wheel center lies at x 36 model units, 576 in 1/16) and its PS1 form do not
fit the native path.

`CWHL` is a reserved chunk name: no reader knows it (an older
reader skips it as an unknown chunk), and rldpack and Reload Studio do not
write it. Planned layout of version 1, little-endian, in 1/16 model units
(the units of CMDL, without the game's instance scale). The wheel lies with
its center at the origin, the axle along X, the outside towards +X, Y up,
Z forward; the right-hand wheels (-X) are the same model turned 180 degrees
about Y.

| Offset | Type | Field | Meaning |
|---|---|---|---|
| 0x00 | u16 | version | 1 |
| 0x02 | u16 | flags | bit 0 STEER (the front pair steers), bit 1 SPIN (the wheels roll); further bits 0 |
| 0x04 | u8 | wheelMask | bit 0 front +X, bit 1 front -X, bit 2 rear +X, bit 3 rear -X (the order of `DrawTires`); at least one bit |
| 0x05 | u8 | reserved | 0 |
| 0x06 | u16 | reserved | 0 |
| 0x08 | 3 x s16 | front | center of the front wheel on +X (the other one: x mirrored) |
| 0x0E | 3 x s16 | rear | center of the rear wheel on +X |
| 0x14 | u16 | radius | rolling radius |
| 0x16 | u16 | halfWidth | half the width |
| 0x18 | u16 | vertexCount N | 0 = sprite mode (the retail wheel pictures at these points and this size), else 3..384 |
| 0x1A | u16 | triangleCount T | 0 = sprite mode, else 1..128 |
| 0x1C | N x 12 bytes | vertices | s16 x, y, z; u16 0; u8 r, g, b; u8 0 |
| then | T x 8 bytes | triangles | u16 a, b, c; u16 flags (bit 0 = two-sided, further bits 0); wound counter-clockwise seen from outside |

Size = 0x1C + 12 N + 8 T, at most 5660 bytes.

| Rule | What must hold |
|---|---|
| CWHL-1 | at least 0x1C bytes, version 1 |
| CWHL-2 | only known flag bits (0x3), reserved fields 0, wheelMask 1..0xF |
| CWHL-3 | size exactly 0x1C + 12 N + 8 T, at most 16 KiB |
| CWHL-4 | N = T = 0 (sprite mode), or 3 <= N <= 384 and 1 <= T <= 128 |
| CWHL-5 | radius 128..512 (8..32 model units), halfWidth 1..radius |
| CWHL-6 | every vertex inside the declared cylinder: \|x\| <= halfWidth + 1, y^2 + z^2 <= (radius + 1)^2 |
| CWHL-7 | indices < N, no triangle with a vertex twice, the padding of vertices and triangles 0 |
| CWHL-8 | wheel points: \|x\| 0..96, y 0..64, z -128..128, front.z > rear.z |

- A finding is planned to cost only the own wheels, never the file: the
  game then draws the retail wheels (as for a broken CMSK).
- CHRI flags bit 0 (no wheels) together with a CWHL: rldpack refuses the
  combination; in the game bit 0 wins (no wheels at all).
- Bounds: 128 triangles per wheel, 512 per kart. The draw memory they cost
  is planned as an addition on top of the frame's draw memory, only while a
  file with CWHL is loaded. 128 is a starting value, to be measured like the
  model limit.

`rldpack char-wheel --model <obj|ply>` reads an author's wheel by the rules
of `make-char --wheel-model` (the same budget and message codes; never
repaired or reduced, its texture never scaled), fits it to the size of the
game's wheel and writes only a preview file for Reload Studio (`RLDPW3`: the
texture whole, up to 1024 on a side, with the CTXT flags make-char would give
it; the wheel's one material as MATL has it - colour, texture, alpha mode -
the vertex colour 255 as in the game; then the textured triangles. The layout
is at the head of `tools/rldpack_wheel.inc`; Reload Studio reads it with
`tools/reloadstudio/rs_tex.c` and builds the levels with `include/rldmip.inc`,
and no longer reads the older `RLDPW2`); it refuses `--out` and never writes
a container.
The wheel reaches the container only through `make-char --wheel-model`
(WHLS version 2 in CNET, "A wheel of one's own").

Reload Studio shows both as cards on its Character page (tab Extras).
"Animations" is greyed out and marked "Coming soon"; started with
`--enable-preview-features` (a switch of that run, never saved) it shows the
poses in the preview, unfinished, and writes nothing into the container.
Card Wheels (Extras): an OBJ or PLY of one wheel and its size; passed as
`--wheel-model`/`--wheel-size` (the size only when it is not 100 %) with the
native model and Show kart wheels on; a preview feature open to everyone
(marked "Preview feature"). The preview draws the wheel textured at the four
wheel points, the right ones mirrored, and "Animate in the preview (spin and
steer)" turns it. Without a wheel model the command is the one from before,
so a built `.rldchar` without one has the same bytes.

### What the game does

| When | What |
|---|---|
| start | every `*.rldchar` in the `characters` folder next to the game (the extension in any case, subfolders skipped): the names are sorted first, then the files are read in that order |
| per file | envelope (`Rld_OpenAs`), the CMDL size before any memory is taken, CHRI and CMDL with their hashes, `RldChar_ParseInfo` and `RldChar_ParseLook`, `RldChar_CheckModel`, then the pointer map, then CICN when present (`RldChar_CheckIcon`; a broken CICN costs only the portrait), then CMSK when present (`RldChar_CheckMask`; a broken CMSK costs only the own mask), then CVOI when present (`RldChar_CheckVoices`; a broken CVOI costs only the voices). CPRM is not read. CNET and CTXT are not read at start, with or without `--native-preview` |
| a bound seat, only with the native preview (NATIVE DRIVERS set to PREVIEW on the GRAPHICS page, or `--native-preview`) | CNET and CTXT read again from the file and checked (`RldChar_ReadNative`), held in host memory until the next load, one line `native model ready`, `native model refused (<rule>), using CMDL` or `native model none`; held only when CHRI hides the kart wheels or the file has wheels of its own (WHLS version 2), else refused with the rule `native-wheels` and the driver keeps its CMDL with the retail wheels (see "Preview, planned: CNET and CTXT") |
| a broken file | skipped with one log line `[CTR Char] REFUSED <file>: <kind> (<rule>) <detail>`; the game starts anyway |
| the first 32 valid files | a tile each in the one-player ARCADE driver select, after the retail drivers (not for CRYSTAL and CTR under NITRO-PIT); a further valid file gets the log line `NO ID` and no tile |
| menu and race | the driver select shows the own portrait (CICN, first 20 entries; else the template's, see CICN) and the name from CHRI; the race HUD, the arcade results and the cup standings show the own portrait too (see CICN). In the race seat 0 runs on the template's character id with the custom model, and the class in CHRI sets the physics values and the engine sound. Not from the template: its voice - in the race the driver speaks its own clips from CVOI (see CVOI) or is silent, and the sample of the voice volume slider is never the template's. Still the template's: the cup podium, high score lists and profiles. After the binding one line names every seat: `[CTR Char] seats: 0=<id> (<file>, map color <RRGGBB>\|map color of the template) 1=<id> ...`, ending in `... (cut at seat <n>)` when the line is full |
| draw memory | the game adds the draw memory of custom models on top of the frame's (`NativeChar_DrawReserve`), twice (the mirror floor draws a model again): on the main menu that of the largest model of the roster, in a race that of seat 0 drawn as an invisible driver (60 bytes a triangle without texture, 64 with one, instead of 28 and 40) plus its own mask; a race with a retail driver and every other menu load add nothing. One line per load when it adds something: `[CTR Char] draw memory: <n> bytes + <m> for custom models (...) = <sum>`. At start one line per file, `[CTR Char] draw bytes <file>: model <n>, own mask <m>`, and the memory window grows for it: `[CTR Native] characters/: mempack extra +<n> bytes for the draw memory of custom models, <m> in force`. Without a loaded file nothing of this changes |
| minimap | with `RLDCHAR_FLAG_MAP_COLOR` the bound seat's marker on the minimap has the color at 0x20, else the template's (808080 for Fake Crash: the marker as drawn). The color tints the marker: 80 per channel is neutral, higher values are brighter. The player's white blink stays |
| wheels | with `RLDCHAR_FLAG_NO_WHEELS` set in CHRI the game draws no kart wheels (and no wheel reflections) for the custom model - tyre dust and skid marks stay, they are effects at the wheel points, not wheels - and the load line `[CTR Char] loaded <file>: ...` adds `, wheels hidden` after the byte count; without the bit the wheels are drawn as for a retail driver |
| look | the ground shadow and the exhaust smoke of the custom model follow the look of CHRI (0x24..0x3B). The key is the model of the instance: the same for a CMDL driver and a natively drawn one, at every scale factor, with or without `--native-preview` (the look is read with CHRI at start). Shadow auto: the retail shadow (the same two icons, four quads, blend, intensity, height factor and depth rule) on the file's quad - the 3x3 grid runs x over xMin, middle, xMax and z over zMin, the seam, zMax, with the seam at 41/93 of the length (the retail rear : front); scaled with the instance scale, which the retail shadow ignores. Shadow off: no shadow. Exhaust custom: each smoke quad is drawn moved by (point - retail source) x the instance scale, turned with the driver; with one point the quads of retail source 1 are left out. Exhaust off: no smoke quads. Only the DRAWING changes: the smoke is born where retail births it, with the same random numbers (the underwater bubbles feed the random number of the item roll), so a race runs the same with and without a look. The turbo flames follow the exhaust: custom puts flame 1 exactly at point 0 and the mirrored flame 2 at point 1 (with one point flame 2 is hidden), off hides both; the turbo itself, its sound and its timers stay. A driver without a look - every retail driver, every file from before the field - is drawn by the unchanged retail code. The load line adds `, shadow auto` or `, shadow off` and `, exhaust custom <n>` or `, exhaust off` after the flags, only for what is not retail; a look that falls back logs `[CTR Char] look <file>: <why> - retail`. With `--dev`, when a file had a look, the exit adds `[CTR Char] at exit: look shadows auto <n>, off <n>; exhaust quads moved <n>, hidden <n>; turbo flames moved <n>, hidden <n>` (per driver or particle and view; flames per tick) |
| mask | CHRI flags bits 1-2 choose the mask the custom driver wears: Aku Aku or Uka Uka for the mask item and the rescue after a fall, with that mask's model, beam, sound and music, the mask's wrong-way voice in a one-player race and the mask's icon in the HUD weapon slot. 0 (or 3) keeps the template's mask: Aku Aku for Crash, Coco, Polar, Pura and Penta, Uka Uka for every other template, Fake Crash included; the HUD icon then follows the retail icon table, in which Penta shows the Uka Uka icon. When the race has not loaded the chosen mask's model or beam, the template's mask stays. With Aku Aku or Uka Uka chosen the load line ends in `, mask aku` or `, mask uka` (after `, wheels hidden` when both are set); with 0 or 3 nothing is added. The first mask born for the seat in each load logs `[CTR Char] mask seat 0: <aku\|uka> from the <file\|template> (model 0x.., beam 0x.., sound 0x.., song <aku\|uka\|none>)`; a chosen mask that is not loaded logs `[CTR Char] mask seat 0: <aku\|uka> wanted, not loaded - the template's mask stays` once per load |

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
| `RLDCHAR_DRAW_BYTES_MAX` | 201600 bytes of draw memory (7200 x 0x1C: 7200 triangles with vertex colors) |
| `RLDCHAR_MASK_BYTES_MAX` | 16 KiB of CMSK (CMSK-3) |
| `RLDCHAR_MASK_DRAW_BYTES_MAX` | 11200 bytes of draw memory of the own mask (400 x 0x1C: 400 triangles) |
| `RLDCHAR_LIMIT_CVOI` | 6 MiB of CVOI (the envelope bound) |
| `RLDCHAR_LOOK_COORD_MAX` | 4096 (256 game units): every value of the look, in 1/16 game units |
| `RLDCHAR_LOOK_Y_MIN` | -256 (16 game units below the origin): the lowest exhaust point |
| `RLDCHAR_LOOK_EDGE_MIN` | 16 (1 game unit): the shortest side of the shadow quad |
| `RLDCHAR_VOICE_BYTES_MAX` | 5293176 bytes: the largest CVOI the rules allow |
| `RLDCHAR_VOICE_RATE` | 22050 Hz |
| `RLDCHAR_VOICE_CLIPS_MAX`, `RLDCHAR_VOICE_PER_EVENT` | 40 clips, 4 per event |
| `RLDCHAR_VOICE_LINE_FRAMES`, `RLDCHAR_VOICE_SHORT_FRAMES` | 77175 samples (3.5 s) for events 0..7, 22050 (1.0 s) for events 8 and 9 |
| `RLDCHAR_LIMIT_CNET`, `RLDCHAR_LIMIT_CTXT` | 32 MiB each (preview; a larger chunk costs only the native part) |
| `RLDCHAR_NET_VERTICES_MAX`, `RLDCHAR_NET_TRIANGLES_MAX`, `RLDCHAR_NET_MATERIALS_MAX` | 20000 vertices, 30000 triangles, 64 materials (preview, start values) |
| `RLDCHAR_WHEEL_VERTICES_MAX`, `RLDCHAR_WHEEL_TRIANGLES_MAX` | 2048 vertices, 1024 triangles of WHLS (preview) |
| `RLDCHAR_TEX_COUNT_MAX`, `RLDCHAR_TEX_EDGE_MIN`..`MAX`, `RLDCHAR_TEX_GPU_BYTES_MAX` | 16 textures, sides 16..2048 (powers of two), 64 MiB of RGBA8 with every mip level (preview) |

`RLDCHAR_DRAW_BYTES_MAX` is the one place of the model limit: model-draw,
the reduction of `rldpack make-char` (`RLDMK_REDUCE_LIMIT`, and
`RLDMK_REDUCE_TARGET`, the same 7200 triangles: the game counts the triangles
as make-char does) and the messages of Reload Studio follow it. The draw-memory bounds and the CMDL limit are budgets of the PC,
not of the PS1 or of a retail model. 7200 is the most triangles at which
every mesh can be built: a frame is at most 65535 bytes, and at three records
per triangle (the worst case of the command list) 7278 triangles fit, so 7200
always fits; CMDL 4 MiB holds the 47 raw frames of such a model. A custom
model does not take draw memory from the track: the game adds what it may
draw on top (see "What the game does"). Eight drivers of 7000 triangles each
in one race, on retail tracks and on a large custom track, were measured
without a frame running out of draw memory and without a dropped instance.

- `rldpack make-char` never reduces a model at or below the limit. Above it,
  `--reduce auto` (rldpack's default) reduces it to at most
  `RLDMK_REDUCE_TARGET` triangles; `--reduce off` refuses it (`model-tris`,
  with its triangles and draw bytes and the limit). With `--machine` it
  reports the model against the limit before anything is reduced:
  `@char budget <triangles> <limit> <draw bytes> <limit in bytes>`.
- `--remesh on` (on request only) replaces each part group of more than 24
  triangles by a closed hull of its surface (`--remesh-resolution`, 32..128
  cells along its longest side, default 64) and reduces the hulls. A hull
  has no detail its source did not have, so it is reduced to twice the
  triangles of the source, at least 1000 and at most `RLDMK_REDUCE_TARGET`
  (`RldMk_RemeshTarget`), not to the whole limit.
- `rldpack make-char` reads a PLY or OBJ of at most 1000000 vertices and
  faces each (`RLDMK_PLY_MAX`, `ply-big`, `obj-big`): a guard of the tool,
  not a format limit, so that a large export is reduced instead of refused.
- Reload Studio passes `--reduce off` unless its "Reduce to fit" is ticked
  (off by default). A model over the limit shows its triangles, the limit
  and a button "Reduce to fit", which ticks the box and checks again.
- A game from before these bounds knows 22560 bytes of draw memory and a
  CMDL of 256 KiB: it refuses a larger model as MODEL LIMIT, and a CMDL
  above 256 KiB as DAMAGED.

## 4. Model rules (`RldChar_CheckModel`)

Packer and reader run the same function.

- The first finding decides the verdict.
- Without a listener the check stops at the first finding.
- `rldpack verify` goes on through all findings and prints the first 32;
  `rldpack info` shows the first finding only.

Verdicts: BAD MODEL, MODEL LIMIT, and DAMAGED for model-size only.

| Stage | Rule | What must hold | Verdict | Scope |
|---|---|---|---|---|
| before | model-size | CMDL <= 4 MiB; the envelope refuses a larger one first | DAMAGED | once |
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
| 4 | model-draw | 28 x G3 triangles + 40 x GT3 triangles <= `RLDCHAR_DRAW_BYTES_MAX` (see Limits) | MODEL LIMIT | once |

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
| a new layout inside SNDB, PARM, CICN or CVOI | the chunk's own version field: an older reader drops only that chunk |
| a new PARM key | no bump: unknown keys are skipped |
| a new string in META or CHRI | appended at the end only |
| a new field in CHRI | a longer fixedSize; an older reader skips the extra bytes. flags at 0x1C came this way (fixedSize 0x20, written only when a bit is set): an older reader skips it and draws the wheels. A new flag bit needs no bump: a reader ignores bits it does not know. Bit 4 (full height) came this way: an older game draws an odd height one value lower. The mask in bits 1-2 came this way: an older reader gives the driver the template's mask. mapColor at 0x20 came as the second field (fixedSize 0x24, written only with bit 3): an older reader shows the template's minimap color |
| a new number in META | not possible without a new major: META's numeric part has no length field. New things go into optional chunks |
| something without which the content is wrong | a required-feature bit in header field 0x0C; a reader that does not know it refuses (NEEDS NEWER) |
| a change an older reader would misread | a new major: older readers say NEEDS NEWER, newer readers say OLD FORMAT for the old files |
| a required chunk becoming optional | not possible within one major, in either format: an older reader refuses a file without one of its required chunks as DAMAGED (`Rld_OpenAs`) |
| header or directory | never: one envelope for every container type |

For `.rldchar`, a delta-coded model and a texture table INSIDE CMDL (texture
indices in its commands, refused today by model-tex) are planned as
required-feature bits; no bit value is assigned yet. The texture table of the
native model (CTXT) is NOT one: the native model (CNET, CTXT; preview) comes
as optional chunks with their own version fields, because CMDL stays
required and draws the character without them - the content is not wrong
without them.

Chunk names:

| Name | Container | Status |
|---|---|---|
| META, LEVD, VRMD | `.rldtrack` | required |
| SNDB, PARM | `.rldtrack` | optional |
| CHRI, CMDL | `.rldchar` | required |
| CICN | `.rldchar` | optional (portrait): written by `rldpack make-char --icon`, shown in the game's driver select grid and race |
| CMSK | `.rldchar` | optional, experimental (own mask): written by `rldpack make-char --mask-model`, drawn in place of the retail mask |
| CPRM | `.rldchar` | optional known type (values): not written by rldpack, content not interpreted yet |
| CVOI | `.rldchar` | optional (voices): written by `rldpack make-char --voices`, spoken by the bound custom driver |
| CNET, CTXT | `.rldchar` | optional, PREVIEW (the native model and its textures): written by `rldpack make-char --native-model on` (an OBJ) or `--native-probe` (tests), read by the game only with the native preview (NATIVE DRIVERS set to PREVIEW on the GRAPHICS page, or `--native-preview`); unknown to the envelope |
| CTEX, CWHL | `.rldchar` | reserved, not assigned (the textures of the native model are CTXT, its wheels a part of CNET) |
| SIGN | blocked | signature, taken out of the format; skipped in older files |
| MMAP | blocked | menu map, taken out with 4.1; skipped in older files |
| THMB | blocked | preview image, dropped |
| CHAL | blocked | challenges, reserved once, dropped with 4.1 |
| AUDD | blocked | music, reserved once, replaced by SNDB |

A blocked name is never assigned again, not even for something else.

## 6. Authority

The comments in include/rldtrack.inc and include/rldchar.inc are authoritative; this document changes in the same commit.
