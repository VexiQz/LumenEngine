# Lumen Engine

C++17 3D engine on top of Wicked Engine. Games: Lua + optional C++ native modules. Editor: ImGui (docking), in `engine/`.

## Build
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j
    build/bin/lumen [project.lum]
Linux needs SDL2, Vulkan headers. Wicked Engine is fetched by CMake (pinned commit). `-DLUMEN_BUILD_ENGINE=OFF` builds only core/tools/tests (verified: ctest 3/3).
On launch `lumen_runtime/` is created beside the executable (for a Visual Studio Release build:
`build-vs/bin/Release/lumen_runtime`). The runtime creates its cache, logs, configuration and
managed-tool directories on every start. Shipped Lua, headers, shaders, fonts and project templates
are installed from `runtime_data/`; tool binaries are downloaded only when requested from the Build
panel, while detected toolchain details are saved to `config/toolchain.json`. Application logs are
written to `logs/lumen.log` and Wicked Engine backlog output to `logs/wicked.log`.

## Project layout (.lum v2)
    MyGame/MyGame.lum  scenes/*.scene  scripts/*.lua  assets/  native/ (C++ module)  .lumen/ (cache, build)

## Lua API
- `lumen.scene`: find, find_all, find_by_tag, spawn, raycast, load, gravity
- `lumen.input`: down, pressed, mouse_delta, mouse_pos, wheel, set_cursor_locked
- `lumen.log`, `lumen.debug.text/line`, `lumen.events.on/emit`, `lumen.quit/elapsed/delta_time`
- Entity: position, set_position, rotation, set_rotation, scale, forward/right/up, look_at, parent, children, destroy, velocity, add_force, add_impulse, move, grounded, light, set_color
- Libraries: mathx, random, noise, color, geom, json, hash, compress, zip, fs, http, process, time, regex, net, utf8x
- Main script returns `{start, update, fixed_update, shutdown}`; entity script returns a class table (`start, update(dt), fixed_update, on_destroy`, `self.entity`).

## Native modules (C ABI)
`runtime_data/include/lumen_c.h`, `lumen.hpp`: export `lumen_module_open/close/update/fixed_update`; host table gives log, time, entity position/rotation, key_down. Built automatically from `native/` with the downloaded toolchain.
