# Editor

| Shortcut | Action |
|---|---|
| F5 / Esc | play / stop |
| Ctrl+S | save scene and scripts |
| Ctrl+Z / Ctrl+Y | undo / redo |
| Ctrl+D | duplicate |
| Del | delete selection |
| W / E / R | move / rotate / scale gizmo |
| F | focus selection |
| Right mouse + WASD | fly camera (Shift = faster) |
| Middle mouse / Alt+left | pan / orbit |
| Ctrl+P | command palette |

Drag models, scripts and audio from **Assets** into the viewport or onto an entity. In the code editor: Ctrl+F find / replace, Ctrl+/ comment, Ctrl+S save, Ctrl+Z / Ctrl+Y undo and redo. Lua files get completion and
diagnostics; C++, C, HLSL, GLSL, JSON and Markdown files are syntax highlighted.

## Viewport toolbar

Top-right of the viewport: grid, selection bounds, wireframe, physics debug (Jolt shapes/joints), statistics, and the fly-camera speed slider. The hierarchy has an eye toggle per row and a **Physics** submenu (box, trigger volume, car, hinge joint). Code editor highlights Lua, C/C++, HLSL/GLSL, JSON, Markdown and CMake; `Ctrl+/` toggles the comment of the current language.

## Lua debugger

Click the gutter of a Lua file to set a breakpoint (red dot); breakpoints persist per project
(`.lumen/breakpoints.json`) and can carry a Lua condition (Debugger panel → Breakpoints). Press Play: the game
stops at the line, the code editor jumps there and highlights it. The **Debugger** panel offers
Continue `F5`, Step over `F10`, Step into `F11`, Step out `Shift+F11`, Pause, Stop, the call stack (click a
frame to jump), Locals/Upvalues/Globals with expandable tables, Watch expressions and an evaluation console that
runs in the selected frame. `lumen.debug.breakpoint()` pauses from code. While paused the world, physics and
scripts are frozen but the editor stays responsive. Disable via Project settings → Scripts → "Allow debugger"
(removes the line hook completely, no runtime cost).

## Timeline (animation clips)
Open **View → Timeline** or double-click a `.lanim` file. Select the entity the clip plays on, add tracks
(position / rotation / scale of the entity or a child path), double-click a lane to add a key, drag keys to move them,
double-click the Events row to add an event. Preview poses the scene; use **Reset pose** before editing the scene.
In Lua: `entity:play_clip("anim/door.lanim", {loop=false})`, `on_anim_event(self, name)`, `on_anim_end(self, path)`.
