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

`make_creator_kit.py` builds the Creator Kit template for a hi-res driver
with a skeleton: `creator-kit.blend` and its demo export
`creator-kit-demo.glb`. Reload Studio runs it on the page Creator Kit
(button Generate demo files); the guide is
[docs/CREATOR_KIT.md](../docs/CREATOR_KIT.md).

The kart and the wheels of the kit come from the user's own game data and
are written only into the folder the user chooses; nothing of them is in
this folder. Everything here is made by code and released under CC0 like the
driver template:

- `creator_kit_puppet.py` - the puppet: a neutral articulated mannequin and
  its skeleton, seated in the kart in its bind pose
- `creator_kit_clips.py` - the 12 clips (idle, steer_left, steer_right,
  jump, drift_left, drift_right, hit, podium_1, podium_2, podium_3, lose,
  menu)
- `creator_kit_standin.py` - a made-up stand-in for the kit files, so the
  kit can be built without game data (tests, the guide's pictures)
- `make_creator_kit.py` - the generator

Needs Blender 5.2. Without game data, from the folder that holds `templates`:

    blender --background --factory-startup --python-exit-code 1 --python templates/make_creator_kit.py -- --kit <folder> --stand-in

Same Blender version and same input give the same bytes.
