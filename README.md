# LumenEngine

A C++17 3D engine with a Lua scripting API, an ImGui-based editor, and optional
native C++ game modules. Wicked Engine is fetched by CMake during configuration.

## Build

From the repository root:

```sh
cmake -S LumenEngine -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

On Linux, SDL2 and Vulkan headers are required. CMake downloads the pinned
Wicked Engine dependency, so the first configure needs network access.

To build only the core, tools, and tests, pass
`-DLUMEN_BUILD_ENGINE=OFF` during configuration.

## Documentation

- [Project and build details](LumenEngine/docs/README.md)
- [Getting started](LumenEngine/runtime_data/docs/getting-started.md)
- [Editor guide](LumenEngine/runtime_data/docs/editor.md)
- [API reference](LumenEngine/runtime_data/docs/api-reference.md)
