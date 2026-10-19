# Driver template

`driver-template.blend` is a start for a custom character with animations:
a made-up placeholder driver with kart, built from simple shapes, at Crash's
size and in the orientation the importer expects (nose toward -Y, +Z up,
ground at z = 0, 1 unit = 1 meter). It has the seven shape keys
(`steer_left`, `steer_right`, `win`, `lose`, and empty, muted `jump`,
`crash`, `reverse`), a Principled BSDF material with a small packed texture,
a glTF exporter on the collection "Driver" and the text "HOW_TO_EXPORT".
It was made with Blender 5.2; older versions may not open it.

How to use it: [docs/ANIMATIONS.md](../docs/ANIMATIONS.md).

The .blend is a download of its own on the release page
(`driver-template.blend`, with `LICENSE-template.txt`, the guide as
`ANIMATIONS.txt` and `SHA256SUMS.txt`); it is not in the repository or a
package. To build it yourself: `make_driver_template.py` builds it from
scratch, the same bytes on every run of the same Blender version (Blender
5.2.2 makes the download itself); run it from the folder that holds
`templates` (the source code, or a package that has this folder), with
`blender` being Blender 5.2 (or its full path, e.g.
"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe"):

    blender --background --factory-startup --python templates/make_driver_template.py

It writes `templates/driver-template.blend` next to the script.

`--glb <file>` also exports the .glb. The template, its texture and the
script are released under CC0 1.0 Universal ([LICENSE](LICENSE)): use them
for anything, no credit needed.

# Creator Kit

`make_creator_kit.py` builds the Creator Kit: a template for each way of
Reload Studio's page Character, each in a folder of its own -
`vanilla\` (the classic model), `lowpoly\` and `hires\` - with its
`creator-kit-<way>.blend` and its figure export
`creator-kit-<way>-figure.glb` (the figure alone, no kart). The way Hi-Res
also exports `creator-kit-demo-local.glb`: the figure with the kart and
wheels from the user's game files, for a test in the user's own game; every
kit part in it is marked with the glTF extras `ctr_kit` and materials named
`ctrkit_...`. Each .blend stores its exporters (the export preset of its
way). `CREATOR_KIT_NOTICE.txt` is copied as `NOTICE.txt` into the kit folder
and into each way's folder. Reload Studio runs the generator from the page
Character (Build a character, Creator Kit, button Generate demo files); the
guide is [docs/CREATOR_KIT.md](../docs/CREATOR_KIT.md).

| Way | Puppet | Textures | Export |
|---|---|---|---|
| Vanilla | 543 triangles, chunky pieces, PS1 vertex colours in 9 steps | 64 x 64 colour atlas | no skeleton, no clips (the classic model poses by itself) |
| Low-Poly | 5288 triangles, PS1 vertex colours in 15 steps | 64 x 64 colour atlas | skeleton, 12 clips + 2 optional finish clips |
| Hi-Res | 14536 triangles, PS1 vertex colours in 15 steps | 1024 x 1024 atlas + a normal map made by code | skeleton, 12 clips + 2 optional finish clips |

Reload Studio also makes a classic model (the fallback) of a Low-Poly or
Hi-Res figure; both templates go through it without a warning (see the head
of `creator_kit_puppet.py`).

The kart and the wheels of the kit come from the user's own game files and
are written only into the folder the user chooses; nothing of them is in
this folder. Everything here is made by code and released under CC0 like the
driver template:

- `creator_kit_puppet.py` - the puppet in its three styles: a neutral
  articulated mannequin and its skeleton, seated in the kart in its bind pose
- `creator_kit_clips.py` - the 14 clips (idle, steer_left, steer_right,
  jump, drift_left, drift_right, hit, podium_1, podium_2, podium_3, lose,
  menu and the optional finish_win, finish_lose)
- `creator_kit_standin.py` - a made-up stand-in for the kit files, so the
  kit can be built without game data (tests, the guide's pictures)
- `creator_kit_classic.py` - an old character (the GLB of `rldpack
  char-export`) as a starting point in the collection Classic, never
  exported (`--classic <file.glb>`)
- `CREATOR_KIT_NOTICE.txt` - the notice of the kit
- `make_creator_kit.py` - the generator

Needs Blender 5.2. Without game data, from the folder that holds `templates`:

    blender --threads 1 --background --factory-startup --python-exit-code 1 --python templates/make_creator_kit.py -- --kit <folder> --stand-in --routes vanilla,lowpoly,hires

`--routes` takes any of the three ways, comma separated (default: all
three). Same Blender version and same input give the same bytes
(`--threads 1` keeps the .blend files byte-identical; with more threads only
their bytes may differ, not their content).
