# Lumen Engine — Changelog

Format: newest first within a release, grouped as **Added / Changed / Fixed / Removed**.

> **Note on versions.** Lumen was developed continuously before any tagged release. The numbers below were assigned
> retroactively to group the work into coherent milestones (0.1.0 = first deliverable, 1.9.2 = current state). Dates were
> not recorded. Verification status is stated per release: **core/tools/Lua** are compiled and tested on Linux (CI-style
> `ctest`); **engine/** (editor, player, Wicked bridge) has only been syntax-checked against the Wicked headers in the
> development sandbox (no GPU, no Windows) and compiled/run by the project owner on Windows (VS, DX12).

---

## Unreleased (after 1.9.2)

### Added
- **Navigation / AI**: navmesh bake (`core/Navigation`, unit-tested), `navmesh`/`navagent` scene components, agents with
  `entity:agent_move_to/stop/...`, `lumen.nav.*`, editor preview overlay.
- **Terrain**: heightfield core (unit-tested), `terrain` component, in-viewport sculpt brushes, layers in the inspector.
- **Particles**: `particles` component (Wicked emitter) with inspector, shader types and presets fields.
- **Animation timeline**: `.lanim` keyframe clips (`core/Animation`, unit-tested), Timeline panel (scrub, keys, interpolation,
  events, capture from scene, save), `ClipSystem` runtime, `lumen.anim.play/stop/set/time`, `entity:play_clip`,
  script callbacks `on_anim_event` / `on_anim_end`, Animator `autoplayClip`.
- Editor: blue accent theme, gradient colour sky, idle throttle for a backgrounded editor, graphics `resolutionScale` and
  meshlet occlusion culling are now actually applied.

### Verification
- core: 42 tests pass. engine/: syntax-checked only (never run in the sandbox).

---

## 1.9.2 — Managed toolchain, in-editor LSP, Lua debugger

### Added
- **Lua debugger** (`core/LuaDebug`): line hook with breakpoints (optional Lua condition, hit counters), pause, step in /
  over / out, call stack, locals, upvalues, globals, expandable tables, expression evaluation in any frame, `Terminate()`.
  The hook is removed completely when no breakpoint/step/pause is active (zero cost).
- **Debugger panel** in the editor: Continue `F5`, Step over `F10`, Step into `F11`, Step out `Shift+F11`, Pause, Stop, call
  stack with jump-to-source, Locals/Globals trees, Watch list, evaluation console, breakpoint list with conditions.
- Breakpoints via gutter click in the code editor (red dot), current-line highlight and arrow while paused, automatic jump
  to the paused line; breakpoints and watches persist per project in `.lumen/breakpoints.json`.
- While paused the game world, physics and scripts are frozen but the editor stays interactive (nested UI frame driven by a
  platform event pump on Win32 and SDL2).
- `lumen.debug.breakpoint()` to pause from Lua.
- **In-editor LSP**: the code editor now talks to an in-process `lsp::Server` — typed completion with documentation,
  hover info, Ctrl+Click go to definition, diagnostics from the real analyser (replaces the old heuristic checker).
- Scene format: `animator`, `particles`, `terrain`, `navmesh`, `navagent` components with JSON round-trip and tests.
- Core tests: `LuaDebug.*` (2), `SceneDoc.GameplayComponentsRoundTrip`.

### Changed
- **Toolchain is now managed-only**: Lumen always uses its own cmake, ninja and compiler (LLVM-MinGW on Windows) downloaded
  into `lumen_runtime/toolchain`; system tools and Visual Studio are ignored unless `toolchain.useSystemTools` is `true`.
- Missing tools are downloaded automatically in the background on first launch (`toolchain.autoDownload`, default on) and
  again right before a native build. The Build panel shows status instead of requiring a manual download step.
- Linux/macOS: cmake and ninja are managed; the compiler still comes from the system (no pinned, checksum-verified compiler
  recipe is available for those hosts yet).

### Fixed
- Test build no longer fails to find Lua headers (`tests/CMakeLists.txt`).

---

## 1.9.1 — Sky, preferences, prefabs

### Fixed
- **Sky/clouds smeared while the camera moved**: `RenderPath3D::PreUpdate` must run before the camera is changed so the
  previous camera is stored for temporal reprojection; moved to the start of the editor update.
- Lua GUI (`lumen.gui`): button clicks were lost because `active` was cleared before the release frame.

### Added
- Editor preferences (UI scale, autosave, camera speed/FOV, mouse sensitivity, invert Y), applied live.
- Numpad views (1/3/7, Ctrl for opposite side), Home = frame all, Esc = deselect.
- Prefabs: save selection as prefab, drag from Assets into the viewport, instantiate at runtime (`lumen.scene.instantiate`).
- Clipboard: copy / cut / paste / select all across entities with hierarchy and id remapping.
- Image thumbnails in the Assets panel.

---

## 1.9.0 — Standalone export and player

### Added
- **Game export** (`GameBuilder`): packs game + runtime into an LPAK, writes `lumen-player` next to it; Export section in the
  Build panel (strip debug data, output folder, last result).
- `lumen-player` executable (Win32 and SDL2 entry points) that extracts to `.lumen-data/<key>` beside the exe (fallback to
  the user data dir) and runs the game without the editor.
- Tests: `GameBuilder.BuildAndExtract`.

### Fixed
- Builder compile errors (`fs::` qualification, `Log` ambiguity), native module pak path (`.lumen/bin/`).
- Player no longer runs the render path twice (Application already updates the active path).

---

## 1.8.0 — Animation, in-game UI, serious gameplay features

### Added
- Lua animation API (`play/pause/stop/loop/speed/weight`, cross-fades) and `lumen.animator` state machine library.
- In-game UI: `lumen.ui` immediate primitives (rect/text/image/line, DPI aware) and `lumen.gui` widget library.
- `lumen.scene.instantiate`, scene loading, fades, pointer/mouse helpers.
- Lua test suite expanded to 15 gameplay tests.

---

## 1.7.0 — Physics (Jolt) and code editor languages

### Added
- Jolt physics through Wicked: rigid body options (damping, gravity factor, lock 2D, sensors/triggers, continuous
  detection, start deactivated), **vehicles** (car, motorcycle, wheel entities, suspension), **joints** (fixed, point,
  distance, hinge, cone, six-DoF, swing-twist, slider, motors, break distance).
- Trigger volumes with `trigger_enter/exit` callbacks; Lua physics API (raycast, overlaps, forces, impulses, velocities).
- Syntax highlighting for **C/C++, HLSL/GLSL, JSON, Markdown, CMake** in the code editor (Lua unchanged); language-aware
  comment toggling.
- Viewport toolbar (gizmo mode, snapping, physics debug), Physics create menu, Vehicle/Joint inspector sections.

---

## 1.6.0 — Editor usability pass

### Changed
- Hierarchy with icons and visibility toggles, drag & drop reparenting, context menus.
- Runtime folder is populated on first launch (Lua stdlib, API stubs, docs, shaders, templates) — no more empty folders.
- Payload (`runtime_data`) is shipped beside the executable.

### Fixed
- Sky looked too harsh: added `skySoftness`, `haze` and gentler aerial perspective defaults.

---

## 1.5.0 — ImGui editor

### Added
- Dockable ImGui editor in `engine/` (Hierarchy, Inspector, Viewport, Assets, Console, Code, Build, Packages, Libraries,
  Shaders, Project settings), ImGuizmo gizmos, undo/redo, project hub with recent projects.
- Wicked bridge: `.lum` scene → Wicked scene (meshes, models, materials, lights, cameras, audio, environment).
- Dark, restrained theme.

### Changed
- **Editor moved from Qt to ImGui** and into the `engine/` folder (no separate `editor/`).

---

## 1.4.0 — Built-in shader library

### Added
- HLSL shaders under `runtime_data/shaders/lumen`: PBR, toon, unlit, glass, ice, dissolve, foliage, force field, terrain,
  stylised water with reflections, post-grade compute. Compiled with DXC → SPIR-V in verification.
- Built-in shaders on by default; projects can disable them and use their own.
- `lumen-shaderc` tool.

### Not verified
- Shaders were compiled but never executed on a GPU in the development sandbox.

---

## 1.3.0 — Native C++ modules and managed tools

### Added
- **Toolchain manager**: pinned, SHA-256 verified downloads of cmake, ninja and LLVM-MinGW; detection order
  config → managed → system.
- Native game modules: per-project `native/` C++ sources compiled automatically (`BuildNativeModule`), stable C ABI
  (`lumen_c.h`), diagnostics parsed into the Build panel.
- `NativeLibs` for render/audio libraries fetched on demand.

---

## 1.2.0 — Packages and assets

### Added
- **Package manager**: dependencies declared in the `.lum` file are downloaded into the project (`packages/`), lock file,
  offline mode, registry config.
- Asset database: every project file listed with type detection, watched for changes.
- LPAK archive format (`Pak.h`) for shipping.

---

## 1.1.0 — Lua standard library

### Added
- Native Lua modules: `json`, `hash`, `compress`, `zip`, `fs`, `http`, `process`, `time`, `log`, `system`, `mathx`
  (vec/quat/mat4/easing), `random` (PCG32), `noise`, `geom`, `color`, `utf8x`, `net` (TCP/UDP/select), `regex`.
- `lumen-lua` CLI and `lumen.test` test framework; 64 Lua tests.
- Own DEFLATE/ZIP/TAR implementation (`Archive`), HTTP client (`Http`), SHA-256.

---

## 1.0.0 — Core foundation

### Added
- Non-throwing `Json` (key order kept, 64-bit ints), `Fs`, `Strings`, `Log`, `Process`, `Platform`, `Status`.
- `Runtime` bootstrap: `lumen_runtime/` created beside the executable on first launch with config, logs, libs, toolchain
  and docs folders.
- `Project` model (`.lum`), `SceneDoc` (entities, components, prefabs, JSON round-trip).

---

## 0.9.0 — Lua language server

### Added
- `lumen-lsp` (stdio and TCP): diagnostics, completion, hover, definition, references, rename, signature help, document and
  workspace symbols, highlights; aware of the Lumen API stubs (`runtime_data/lua/api`).
- Lua analyser (`LuaAnalysis`): tokens, scopes, types, module resolution.

---

## 0.8.0 — Lua compiler and sandbox

### Added
- `LuaCompiler` (bytecode, strip debug info), library policy (`fsRoots`, network/process switches), `require` paths for the
  project, packages and the engine Lua library.

---

## 0.5.0 – 0.7.0 — Scripting API groundwork

### Added
- `lumen` Lua API v1: entities (transform, hierarchy, tags), scene queries, input, events, logging, game lifecycle
  (`init/update/fixed_update/shutdown`), script instances with `props`, hot reload of scripts.
- EmmyLua API stubs (`lumen.lua`, `wicked.lua`) for the language server.

---

## 0.1.0 — First deliverable (phase 1)

### Added
- `.lum` project format and templates, runtime folder bootstrap, embedded Lua 5.4, first LSP prototype.
- CMake build (FetchContent for Wicked Engine, pinned to commit `4f93f3fd4f337aeb38dbfe410701b83024ccdd42`).
