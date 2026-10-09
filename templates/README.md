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

`make_driver_template.py` builds the file from scratch, the same bytes on
every run of the same Blender version:

    blender --background --factory-startup --python templates/make_driver_template.py

`--glb <file>` also exports the .glb. The template, its texture and the
script are released under CC0 1.0 Universal ([LICENSE](LICENSE)): use them
for anything, no credit needed.
