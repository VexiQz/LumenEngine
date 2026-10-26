# Scripting

Games are written in Lua 5.4. Two kinds of scripts exist.

## Main script
`scripts/main.lua` (set in the project settings) returns a table:

```lua
local game = {}
function game.start() lumen.log.info("hello") end
function game.update(dt) end
function game.fixed_update() end   -- fixed physics rate
function game.shutdown() end
return game
```

## Entity scripts
Attach a script to an entity (Inspector > Add component > Script). The file returns a class table. Plain values become
inspector properties and can be overridden per entity.

```lua
local M = { speed = 5 }
function M:start() self.start_pos = self.entity:position() end
function M:update(dt) self.entity:translate(self.entity:forward() * self.speed * dt) end
function M:on_destroy() end
return M
```

`self.entity` is the entity the script is attached to (see [api-reference](api-reference.md#entity)).
A script that raises an error is paused until you save it again (hot reload); the error shows in the Console.

## Frequently used calls
* `lumen.scene.find("Name")`, `lumen.scene.find_by_tag("enemy")`, `lumen.scene.spawn("sphere", pos, size, true)`.
* `lumen.scene.raycast(origin, direction, max)` -> `{entity, position, normal, distance}`.
* Input: `lumen.input.down("w")`, `lumen.input.pressed("space")`, `lumen.input.mouse_delta()`, `lumen.input.set_cursor_locked(true)`.
* Physics: `entity:set_velocity`, `entity:add_impulse`, `entity:move(dir, speed, jump)` for character controllers.
* Events: `lumen.events.on("hit", fn)` / `lumen.events.emit("hit", info)`.
* Debug: `lumen.debug.text(x, y, "fps")`, `lumen.debug.line(a, b)`.

Angles are euler degrees (pitch X, yaw Y, roll Z). The world is left handed, +Y up, +Z forward.

## Libraries
`lumen.mathx` (vectors, quaternions, matrices, easing), `lumen.noise`, `lumen.random`, `lumen.color`, `lumen.geom`,
`lumen.json`, `lumen.regex`, `lumen.time`, `lumen.fs` and more - see [libraries](libraries.md). Pure Lua helpers:
`require "lumen.tween"`, `lumen.pathfind` (A*), `lumen.statemachine`, `lumen.ecs`.

## Editor support
The Code panel and the `lumen-lsp` language server understand the annotations in `lua/api`. Annotate your own code with
`---@param name type`, `---@return type`, `---@class Name`, `---@field name type` and `---@type T` to get typed completion and hover.
