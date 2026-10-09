# Animations for custom characters

A custom character can bring its own poses: steering, reverse, crash, jump,
and a win and a lose pose after the finish line. You make them as **shape
keys** in Blender and export the model as **glTF 2.0 binary (.glb)**. Reload
Studio reads the .glb on the Character page like an OBJ, plus the shape keys.

A ready-made start is the template [`templates/driver-template.blend`](../templates/driver-template.blend)
(CC0, free for any use): a simple placeholder driver with kart, at the right
size and orientation, with all seven shape keys and the export settings
already stored in the file.

## 1. Prepare the model

1. **Orientation:** the nose points toward **-Y**, **+Z** is up, the ground
   is at z = 0. The glTF exporter turns this into the +Y up, +Z forward the
   game uses (keep "+Y Up" ticked, the default).
2. **Size:** 1 Blender unit = 1 meter = 64 game units. Reload Studio fits the
   model to Crash: it is scaled so that its kart is as long as Crash's
   (112.4 game units = 1.756 m) and the whole model at most as tall as the
   tallest original driver (142.5 game units = 2.23 m). Model at about that
   size and the factor stays close to 1. The template's kart is exactly
   1.756 m long; the empty "CrashSize" in it shows Crash with his kart.
3. **Parts:** the kart is one connected piece at the bottom, about as long as
   a kart. The driver is the largest other piece; small separate pieces in
   front of him are taken as the steering wheel. So keep the driver's arms,
   legs and face connected to his body (the template welds each limb to the
   body at one corner). The game draws the wheels; leave them out, or use the
   Wheels card.
4. **Materials:** use the **Principled BSDF** with an **Image Texture** on
   Base Color (or just a Base Color). That is what the glTF exporter writes
   and what Reload Studio reads. The model needs UVs for its texture.

## 2. Add the shape keys

Add the shape keys to the **driver mesh** (Object Data properties > Shape
Keys). The first key, "Basis", is the neutral pose. Name the others exactly
as below (upper or lower case does not matter). **All seven are optional.**

| Shape key | Used for | When it is missing |
|---|---|---|
| `steer_left` | steering, leaning toward the driver's left (+X) at full lock | the mirror image of `steer_right`, or the automatic lean |
| `steer_right` | steering, leaning toward the driver's right (-X) at full lock | the mirror image of `steer_left`, or the automatic lean |
| `reverse` | driving backwards | the automatic pose |
| `crash` | the hit and bounce after a crash (strongest point) | the automatic pose |
| `jump` | in the air | the automatic pose |
| `win` | after the finish line, places 1-3 | the neutral pose |
| `lose` | after the finish line, places 4-8 | the neutral pose |

- Model each key as the **full** pose; the game blends from the neutral pose
  to it along the curve of the original drivers.
- Keep the values of all keys at 0 when you export, and do not mute a key you
  want to use: a muted key is not exported. In the template `jump`, `crash`
  and `reverse` are empty and muted, so the game moves those poses itself;
  unmute a key once you have shaped it.
- An empty but exported key means "this pose does not move".
- Other shape keys are ignored; Reload Studio lists them as a note.
- The kart stays still in every pose.

## 3. Export to glTF

In the template: Collection properties of "Driver" > **Exporters** > glTF 2.0
> **Export** (or File > Export All Collections). It writes
`driver-template.glb` next to the .blend.

For your own file, File > Export > glTF 2.0 (.glb/.gltf) with:

- **On:** Format glTF Binary (.glb) - the textures are embedded; +Y Up;
  UVs, Normals; Shape Keys (with Shape Key Normals); Materials: Export,
  Images: Automatic.
- **Off:** Skinning, Animations (no animation sampling), Cameras, Punctual
  Lights, Apply Modifiers (modifiers and shape keys do not mix: apply your
  modifiers before you add the shape keys). The template also turns "Use
  Sparse Accessor if Better" off.

Then choose the .glb as the model on the Character page of Reload Studio
("glTF binary (*.glb) - recommended" in the file dialog). The card
Animations lists each pose as from file, automatic, mirrored or error.

## 4. What Reload Studio adds for you

- **Mirror rule:** with only one of `steer_left` and `steer_right`, the other
  side is its mirror image (x -> -x) when at least 98 % of the points of the
  model find a mirror partner. Otherwise the automatic lean is used, with the
  warning "model not symmetric, using automatic lean".
- Every pose without a shape key falls back as in the table above.
- Steering, reverse, crash and jump go into the 47 frames of the classic
  model, so every version of the game shows them, and the native model shows
  the same.

## 5. Where you see win and lose

After the finish line, only on the **native model**: build the character with
"Native model" ticked (tab Extras, card Import; it needs "Show kart wheels"
off or a wheel model on the Wheels card) and set OPTIONS -> GRAPHICS ->
NATIVE DRIVERS to Preview, at a RESOLUTION of 2x or more. The classic model
stays in the neutral pose after the finish line.

## 6. Limits

- The same budgets as for an OBJ: a driver has 201,600 bytes of draw memory
  per frame, about 7200 untextured triangles; a larger model is refused, or
  reduced with "Reduce to fit". See section 4 of the
  [Reload Studio guide](../tools/package/README.txt).
- Shape keys only. **Rigs are not supported yet, please use shape keys** - a
  .glb with an armature (skin) is refused.
- OBJ and PLY stay supported for models without poses, exactly as before.
