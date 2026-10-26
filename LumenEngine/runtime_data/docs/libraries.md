# Libraries

Native libraries (C++, always available as `lumen.<name>` or `require "lumen.<name>"`):

| Library | Purpose |
|---|---|
| `mathx` | vec2/vec3/vec4, quat, mat4, lerp/smoothstep/damp/spring, easing, curves |
| `random` | seeded generators, `range`, `int`, `pick`, `shuffle`, `weighted`, `normal`, `in_sphere` |
| `noise` | perlin / simplex / value / worley / curl noise, `fbm` |
| `color` | parse, hsv/hsl/oklab conversion, mixing, palettes, gradients, kelvin |
| `geom` | ray/AABB/sphere/triangle tests, polygons, convex hull, triangulation, splines |
| `json` | strict `encode` / `decode` (keeps integers and floats apart) |
| `hash`, `compress`, `zip` | sha256/md5/crc, base64/hex, deflate/gzip, zip archives |
| `regex` | PCRE-like regular expressions |
| `utf8x` | unicode aware string helpers |
| `fs`, `process`, `system`, `time` | files (sandboxed to the project), child processes, host information, clocks and dates |
| `http`, `net` | HTTP client, TCP/UDP sockets |

Pure Lua (`lua/lumen`): `tween` (30+ easings, manager with delay/loop/callbacks), `pathfind` (grid and graph A*),
`statemachine`, `animator` (parameter driven animation state machine with cross fades, triggers and one-shot clips), `gui` (widgets for `lumen.ui`), `ecs`, `test` (unit test runner used by `lumen-lua`).

Third party packages: open **Packages** in the editor. Registry: lume, classic, json, flux, middleclass, inspect (Lua),
nlohmann-json, glm, entt, fmt, stb, magic-enum (C++), plus anything you add to `config/registry.json` or a git URL.
