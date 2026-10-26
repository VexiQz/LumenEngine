# Project format (.lum)

```
MyGame/
  MyGame.lum            project file (JSON): name, graphics, physics, scripts, shaders, native module, dependencies
  scenes/*.scene        scenes (JSON, "lumen-scene" v2): environment + entities
  scripts/*.lua         game scripts
  assets/               models (obj/gltf/fbx/ply), textures, audio
  native/               C++ module (optional)
  packages/             downloaded dependencies (managed)
  lumen.lock.json       resolved package versions
  .lumen/               caches and generated files (not part of the project)
```

A scene entity has: `id`, `name`, `parent`, `tag`, `transform`, and optional `mesh` (primitive or model), `material`, `light`,
`camera`, `body` (rigid body), `audio` and `scripts`. Environment: sun, sky (procedural softness / haze / clouds), fog, ambient,
wind and ocean.
