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

## 2. The Texture Sample node

- The node is named **Texture Sample** (`material.texture.sample2d`). Its name does not change without the
  owner's say-so.
- Workflow: place a Texture Sample node on the graph -> **double-click it** -> **a panel opens for that node**
  -> the panel lists **only the assets this node accepts**: PNG and the other image formats **already in the
  project**. No audio, no 3D models, nothing the node cannot use. Pick one and the node uses it.
- Images are recognised by what they are (image file formats), not by an asset-kind label that nothing sets.

## 3. The same rule for every node that takes an asset

Any node that consumes an asset gets its own panel on double-click, and that panel shows only the kinds of
asset that node accepts.

## 4. What the app is for (agreed plan, 2026-09-25)

Texture is the suite's surface lab, with work areas:

- **Materials** - the material node graph with a live 3D preview.
- **Image Lab** - graphs of FRust nodes that process images offline (not real time).
- **Automations** - rules that run Image Lab graphs automatically when an asset comes in.

Everything the app produces goes back into the project as assets (saved materials, processed images,
automation results), with the originals never overwritten.

Tracked as wwestlake/Creation-Texture issues #18-#30.
