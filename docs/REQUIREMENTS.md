# Djehuti Texture - Requirements

These are the owner's requirements for this app, recorded 2026-09-25 after they had been explained
verbally many times and repeatedly lost between sessions and agents. **Read this before changing anything in
this app.** Where current code disagrees with this document, the code is wrong.

Do not rename nodes, move controls, or change these workflows without the owner saying so.

## 1. Assets are reached through context, never through a general browser

- Texture has **no general asset browser**: no big list of every asset in the VFS, no list of dates, no
  asset buttons scattered around the app.
- An asset is always reached **through the thing that needs it**. The node that uses an asset is where you
  choose that asset.
- The suite header's asset window is not the route into Texture's graphs. (It currently shows a Station-era
  "add selected asset to a track" button that does not belong here - separate cleanup.)

## 2. The Properties panel - every node, driven by selection

Decided 2026-09-26, replacing an earlier double-click design:

- **No double-click.** Selecting a node shows it in a **Properties panel that can be docked** anywhere in the
  layout. Select a different node and the panel follows it.
- **This is for all nodes**, not just Texture Sample. The panel is built from whatever node is selected.
- **Every value that is editable in that situation is editable in Properties** - not just shown. Each input that
  is not wired gets an editor for its type (number, colour, vector, toggle, text, image). An input that is wired
  shows what it is wired from. It is a real editing surface, not a drop-down menu.

### Texture Sample in Properties

- The node is named **Texture Sample** (`material.texture.sample2d`). Its name does not change without the
  owner's say-so.
- Its image input is chosen in its Properties: the panel lists **only the assets this node accepts** - PNG and
  the other image formats **already in the project**. No audio, no 3D models, nothing the node cannot use.
- Images are recognised by what they are (image file formats), not by an asset-kind label that nothing sets.

## 3. The same rule for every node that takes an asset

Any node that consumes an asset chooses it in its own Properties, and the list there shows only the kinds of
asset that node accepts.

## 4. What the app is for

Texture is the suite's surface lab, with work areas:

- **Materials** - the material node graph with a live 3D preview.
- **Image Lab** - a procedural image editor built on a layer stack (section 5).
- **Automations** - rules that run procedures automatically when an asset comes in.

Everything the app produces goes back into the project as assets (saved materials, processed images,
automation results), with the originals never overwritten.

### Work areas own the whole layout (decided 2026-09-27)

The dockable windows exist so the app can be organised around what you are doing. Each work area has its own
complete window layout, like DaVinci Resolve's pages or Blender's workspaces:

- Work areas are chosen from a **Layout** menu in the main menu bar: **Materials**, **Image Lab**, **Surface Map**
  (Automations later), with a tick on the current one, and **Reset Layout**. No switcher buttons (owner, 2026-09-29).
  Choosing one swaps the whole layout; only that area's panels are shown. Panels of other areas never linger.
- **Materials:** node palette on the left, Node Graph in the centre, 3D Preview on the right with Properties
  (the selected node) below it.
- **Image Lab:** the canvas in the centre as the main thing, Layers on the right with History (the undo list)
  below it; effects and tools on the left once they exist. No empty placeholder panels.
- **Each area remembers its arrangement** (saved per work area through the suite's VFS settings store), with
  **Layout > Reset Layout** to go back to the default.
- **Menus follow the work area**: Image Lab has its own Layer menu; Materials keeps the material menus. No
  commands that do not apply to the current area.

### The general node system is research's, not Texture's (2026-09-25)

Research is building one general node language: a node-based UI that produces a JSON model, which is then
compiled to a target - FRust, GLSL, a sequence of process steps, or something else. Texture will be its first
test bed. Until it is ready:

- Texture does **not** build its own general node/graph engine or node contract (hold issue #24 and related
  Phase 1 node items).
- Texture keeps a clean seam between "the graph" and "compile it for a target", so research's system can be
  swapped in there.
- Work that carries over regardless of graph engine continues: node asset panels, the preview, storage in the
  project, getting assets in and out, and the Image Lab below.

## 4a. Surface Map: CrazyBump-style maps, saved as a texture set (decided 2026-09-30)

A layout of its own (**Layout > Surface Map**), modelled on CrazyBump: maths derives a surface's maps from one
photo.

- **Select an image asset** (the source image slot in Settings - context rule, section 1).
- **Set the things**, CrazyBump's controls: **Raised / Sunken** (its "which one looks right"), Intensity,
  Sharpen, Noise Removal, and the **Fine / Medium / Large / Very Large / Huge** detail sliders, plus normal
  convention (OpenGL / DirectX green), occlusion strength, specular level and contrast, de-light for the diffuse,
  and metallic.
- **See it on a 3D object**: a lit, rotatable sphere / cube / plane with the maps applied (right-drag rotates,
  left-drag moves the light), and a large view of each map. Maps recompute in the background as sliders move.
- **Save by menu**: **File > Save Surface Map...** makes the maps at full size and saves one **texture set**
  asset.
- Generators (procedural image generation) are a **separate part** of the tool, not Surface Map.

### The texture set (the "Enhanced Texture" pack)

One project asset that keeps all of a surface's maps together, so a game engine or other tool loads it as one
thing: `<name>.texset.json` (format `djehuti-texture-set`) names each map's role, file, colour space and bit depth,
the source image, and the exact settings that made it; the maps live beside it in `<name>.texset/`:
base colour (de-lit, sRGB), normal (convention recorded), height (16-bit), occlusion, roughness, and `orm.png`
(glTF packing: r = occlusion, g = roughness, b = metallic).

## 5. Image Lab: a procedural layer editor

Think GIMP, simpler, with **no hand/mouse drawing tools**. Everything is procedural.

- **A layer stack, worked graphically** like GIMP: each layer has a thumbnail, name, visibility, opacity, and
  blend mode; layers are reordered by dragging; the canvas shows the composite.
- **Layers start from images already in the project**, chosen in context (the rule in section 1) - never from a
  general browser.
- **Effects change the image.** An effect can modify a layer or, like GIMP, **produce a new layer** with the
  result.
- **Procedures are FRust.** Every effect and procedure is FRust code - written by hand today, generated by
  research's node system eventually. The app does not care where the FRust came from.
- Results are saved back to the project as new assets (a flattened image, or individual layers).

### How effects are applied: GIMP style, baked into the pixels (decided 2026-09-27)

Not live, not real time - it is a maths job. The owner's words: GIMP-style effects applied to images; it
updates the image on screen as it does the work, but it does not have to be live action. Modelled on GIMP's
classic filter workflow (GIMP 3 with "Merge filter" on):

1. Select a layer, choose an effect.
2. **A dialog opens** with the effect's parameters, plus the common controls: presets, blend mode and opacity for
   the result, **Preview** (on by default), and **Split view** (before/after with a draggable divider).
3. With Preview on, the canvas shows the result while parameters change - computed in the background and drawn
   as it is produced. Nothing is written to the layer yet.
4. **Apply** runs the effect on the full layer with a progress bar and writes the result **into the pixels** -
   or onto a **new layer** above, if chosen. **Cancel** discards everything.
5. **Every apply is one undo step**, listed in a readable history (the same history the LLM reads and rolls
   back).
6. **Repeat Last** and **Re-show Last**, as in GIMP.

So a layer is just pixels plus name, visibility, opacity and blend mode; effects are operations that change
pixels, not objects that live in the stack.

### Procedural brushes

Brushes are in; hand-drawn strokes are out. A brush is a procedural object:

- a **stamp** - a shape, or a **texture** (an image from the project, for texture brushes);
- **stroke settings** - spacing, rotation, scale, jitter/scatter, blend;
- a **stroke** supplied by a procedure, not the mouse - a path, a grid, a scatter, the edge of a mask, a
  noise-driven curve, a line through the image.

The owner has further thoughts on how texture brushes should work - get them before designing brushes in
detail.

### Use case (illustration, not a spec): make an image tileable

Given as an example of how the tools will be used - **do not build this as its own tool**:

1. Assess the image statistically over a grid of small squares for general evenness - uneven textures never
   tile, they always pattern out. Continue only if the squares agree within some error bars.
2. Split vertically and swap the halves; split horizontally and swap the halves (the old outer edges now meet
   in the middle, so the new outer edges wrap).
3. Run a gentle dither-smear brush down the centre both ways to blend out the mismatch lines.

It combines measuring, transforming, and a procedural brush, joined by a decision. That is the pattern: tools
are general building blocks, and procedures combine them.

## 6. The tools

General building blocks, each small and separate. A procedure is FRust calling them in order, with logic in
between.

1. **Measure** - read an image and return numbers or maps, not pixels: grid statistics, histograms, means,
   variance, edge maps. This is what lets a procedure (or the LLM) decide.
2. **Transform** - move pixels: split, offset with wrap, swap, flip, rotate, crop, resize.
3. **Filter** - pixel effects over a layer or a mask: blur, sharpen, levels, dither, noise, height-to-normal.
4. **Brush** - stamp + stroke settings + a procedurally supplied stroke (above).
5. **Layers** - add, remove, reorder, opacity, blend modes, masks, flatten.
6. **Procedure** - FRust combining 1-5 with logic.

## 7. An LLM runs the tools most of the time

An LLM will be in charge most of the time: it runs the tools, assesses the results, and decides what to do next.
So every tool is built to be used by the LLM first, not just from a button:

- **One tool registry.** Each tool is registered with a name, a plain description, typed parameters, and a typed
  result. The LLM, the UI, and FRust procedures all call the same tool through the same registry. No tool exists
  only as a button.
- **Measure tools return data the LLM can read** (numbers and short findings), not just images for a human.
- **The LLM can see the work.** A tool can return a rendered view of a layer or the composite so the model can
  check visually as well as by the numbers.
- **Every step is recorded and undoable.** The LLM will experiment; each tool call goes into a readable
  history that can be rolled back, which is also the record of what was done and why.
- This plugs into the suite's existing agent (Frusty), which already calls registered tools - not a new agent.

## 8. Foundation to build first

1. **Layer model** - layers, pixel buffers, masks, blend modes, and the graphical layer stack.
2. **FRust host interface** - what a FRust procedure can call: read/write a layer, create a layer, measure, run
   a brush stroke.
3. **Tool registry** - where every tool is described, so the LLM, UI, and FRust can call it.

Once these exist, each tool is a small FRust or C++ piece plugged into them.

Tracked as wwestlake/Creation-Texture issues #18-#30 (to be updated to match sections 4-8).
