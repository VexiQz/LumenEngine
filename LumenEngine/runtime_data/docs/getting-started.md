# Getting started

1. Start Lumen. The hub shows your recent projects. Press **New project**, choose a name, a folder and a template.
   * `default` - a playable scene (ground, water pond, third person character, physics crates, flickering light, C++ module).
   * `empty` - a camera and a main script.
2. The editor opens the main scene. The layout is: **Hierarchy** (left), **Viewport** (centre), **Inspector** (right),
   **Assets / Console / Code** (bottom).
3. Select an entity and edit it in the Inspector. Add a **Script** component to attach `scripts/player.lua`.
4. Press **Play** (F5). The scene runs with physics and scripts; **Esc** returns to the editor. Scene state is restored when you stop.
5. Edit scripts in the **Code** panel (Ctrl+S saves; scripts hot reload while playing).

Everything the game needs lives in the project folder: scenes, scripts, assets, `native/` (C++), `packages/` (downloaded libraries)
and the `.lum` project file.

## Making a game

* Write behaviour in Lua (see [scripting](scripting.md)). Use C++ in `native/module.cpp` for heavy code (see [native-modules](native-modules.md)).
* Add libraries in **Packages**: Lua (lume, classic, flux, hump ...) and C++ (glm, entt, fmt ...) are downloaded into `packages/`.
* Tune the look in **Environment** (sun, sky softness, fog, clouds, ocean) and **Project settings > Graphics** (tonemap, exposure, bloom).
* Build a release from **Build**.
