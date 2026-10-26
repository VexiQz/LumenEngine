# Lumen Runtime

This folder (`lumen_runtime/`) is created next to the engine executable on the first launch and refreshed on every
start. Files you edit are never overwritten: the new version is stored beside it as `<name>.lumen-new`.

| Folder | What is inside |
|---|---|
| `lua/` | Lua libraries of the engine (`lumen.test`, `lumen.tween`, `lumen.pathfind`, `lumen.statemachine`, `lumen.ecs`). Added to `package.path` of every game. |
| `lua/api/` | Annotated API definitions (`std.lua`, `lumen.lua`, `lumen_modules.lua`). The built in language server, `lumen-lsp` and external editors (sumneko / LuaLS) read these for completion, hover and diagnostics. |
| `include/` | `lumen_c.h`, `lumen.hpp` and the Lua headers for C++ game modules. |
| `shaders/` | The Lumen shader library (HLSL) and the UI shaders. |
| `templates/` | Project templates (`default`, `empty`). Add your own folder with a `template.json`. |
| `fonts/` | UI fonts. |
| `docs/` | Documentation (this folder). |
| `lsp/` | Language server settings and editor integration files. |
| `config/` | `engine.json` (editor settings), `registry.json` (extra packages), `toolchain.json` (detected build tools). |
| `toolchain/` | Compilers and build tools downloaded by the Build panel (CMake, Ninja, llvm-mingw). |
| `packages/` | Download cache of Lua and C++ packages. |
| `cache/` | Compiled Lua (`cache/lua`), compiled shaders (`cache/shaders`) and import cache (`cache/imports`). Filled while you work. |
| `logs/` | `lumen.log` (engine) and `wicked.log` (renderer). |
| `tmp/` | Scratch space, safe to delete. |

Documents: [getting-started](getting-started.md), [scripting](scripting.md), [api-reference](api-reference.md),
[libraries](libraries.md), [native-modules](native-modules.md), [project-format](project-format.md),
[graphics](graphics.md), [editor](editor.md).
