# Native (C++) modules

A project with `native/` is built by the engine: it generates `.lumen/native/CMakeLists.txt`, configures and builds with the
detected (or downloaded) toolchain, and loads the resulting shared library when the game starts.

```cpp
#include <lumen.hpp>

static int answer(lua_State* L) { lua_pushinteger(L, 42); return 1; }
static const luaL_Reg kFunctions[] = {{"answer", answer}, {nullptr, nullptr}};

extern "C" LUMEN_EXPORT int lumen_module_open(lua_State* L, const LumenHost* host)
{
    lumen::Bind(host);
    lumen::Info("module loaded");
    lumen::RegisterModule(L, "mymod", kFunctions); // Lua: local m = require("mymod")
    return 0;
}
```

The C ABI is in `include/lumen_c.h`: export `lumen_module_open(lua_State*, const LumenHost*)` (required), `lumen_module_close(lua_State*)`,
`lumen_module_update(double dt)` and `lumen_module_fixed_update(double dt)`. The host table provides `log`, `time`,
`delta_time`, entity lookup / position / rotation access and `key_down`. The Lua headers are in `include/lua`.

Build errors are parsed into the Build panel with file and line. Add C++ packages (glm, entt, fmt ...) from the Packages panel;
their include directories are added automatically.
