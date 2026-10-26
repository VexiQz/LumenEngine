# Lumen Lua API reference

Generated from `lua/api/lumen.lua` (the same file the language server reads). Parameters marked `?` are optional.


## Vec3

A vector of 2, 3 or 4 numbers: `{x, y, z}` tables or `lumen.vec3(x, y, z)` values are accepted.
- field `x number`
- field `y number`
- field `z number`

## Light


- field `intensity number Light intensity.`
- field `range number Range in metres (point and spot lights).`
- field `color Vec3 Linear RGB colour.`

## Entity

A scene entity. Equality compares identity: `a == b`.

### `Entity:name()`

Entity name as shown in the hierarchy.
- returns `string`

### `Entity:id()`

Stable id stored in the scene file.
- returns `string`

### `Entity:valid()`

False once the entity has been destroyed.
- returns `boolean`

### `Entity:position()`

World position.
- returns `Vec3`

### `Entity:set_position(position)`

Moves the entity to a world position. Rigid bodies are teleported.
- param `position` : Vec3

### `Entity:local_position()`

Position relative to the parent.
- returns `Vec3`

### `Entity:set_local_position(position)`


- param `position` : Vec3

### `Entity:translate(delta)`

Moves the entity by `delta` in world space.
- param `delta` : Vec3

### `Entity:rotation()`

World rotation as euler angles in degrees (pitch X, yaw Y, roll Z).
- returns `Vec3`

### `Entity:set_rotation(euler)`


- param `euler` : Vec3 degrees (pitch, yaw, roll)

### `Entity:local_rotation()`


- returns `Vec3`

### `Entity:set_local_rotation(euler)`


- param `euler` : Vec3

### `Entity:scale()`

Local scale.
- returns `Vec3`

### `Entity:set_scale(scale)`

Sets the local scale; a single number scales uniformly.
- param `scale` : Vec3|number

### `Entity:forward()`

Unit vector along the entity's +Z axis in world space.
- returns `Vec3`

### `Entity:right()`

Unit vector along +X in world space.
- returns `Vec3`

### `Entity:up()`

Unit vector along +Y in world space.
- returns `Vec3`

### `Entity:look_at(target)`

Rotates the entity so +Z points at `target`.
- param `target` : Vec3

### `Entity:set_active(active)`

Shows or hides the entity's mesh.
- param `active` : boolean

### `Entity:parent()`

Parent entity or nil.
- returns `Entity?`

### `Entity:child(name)`

Direct child with the given name.
- param `name` : string
- returns `Entity?`

### `Entity:children()`

All direct children.
- returns `Entity[]`

### `Entity:destroy()`

Removes the entity (and its components) from the scene.

### `Entity:velocity()`

Linear velocity of the rigid body (zero without one).
- returns `Vec3`

### `Entity:set_velocity(velocity)`


- param `velocity` : Vec3

### `Entity:add_force(force)`

Applies a continuous force this step.
- param `force` : Vec3

### `Entity:add_impulse(impulse)`

Applies an instantaneous impulse.
- param `impulse` : Vec3

### `Entity:move(direction, speed, jump, control_in_air)`

Moves a character controller (capsule body with `lockRotation`).
- param `direction` : Vec3 desired movement direction (unit length)
- param `speed?` : number metres per second, default 6
- param `jump?` : number jump speed
- param `control_in_air?` : boolean allow steering while airborne

### `Entity:grounded()`

True when a character or body stands on something.
- returns `boolean`

### `Entity:light()`

Light component of the entity, or nil.
- returns `Light?`

### `Entity:set_color(color)`

Sets the base colour of the entity's material (RGB or RGBA, linear).
- param `color` : Vec3|number[]

## RayHit


- field `entity Entity`
- field `position Vec3`
- field `normal Vec3`
- field `distance number`

## Script

The base class of entity scripts: return a table with these functions. Plain (non function) fields are editable properties in the inspector.
- field `entity Entity The entity the script is attached to.`

### `Script:start()`

Called once when the game starts (or the entity is created).

### `Script:update(dt)`

Called every frame.
- param `dt` : number seconds since the last frame

### `Script:fixed_update()`

Called at the fixed physics rate.

### `Script:on_destroy()`

Called when the entity is destroyed.

### `lumen.quit()`

Stops the game.

### `lumen.elapsed()`

Seconds since the game started.
- returns `number`

### `lumen.delta_time()`

Seconds of the last frame.
- returns `number`

### `lumen.scene.find(name)`

First entity with this name.
- param `name` : string
- returns `Entity?`

### `lumen.scene.find_all(name)`

Every entity with this name.
- param `name` : string
- returns `Entity[]`

### `lumen.scene.find_by_tag(tag)`

Every entity carrying the tag.
- param `tag` : string
- returns `Entity[]`

### `lumen.scene.spawn(primitive, position, size, physics)`

Creates a primitive at runtime ("cube", "sphere", "plane", "cylinder", "cone", "capsule").
- param `primitive` : string
- param `position` : Vec3
- param `size?` : Vec3
- param `physics?` : boolean add a dynamic rigid body
- returns `Entity`

### `lumen.scene.raycast(origin, direction, max_distance)`

Casts a ray against the scene.
- param `origin` : Vec3
- param `direction` : Vec3
- param `max_distance?` : number default 1000
- returns `RayHit?`

### `lumen.scene.load(path)`

Loads another scene (project relative path) and restarts scripts.
- param `path` : string
- returns `boolean`

### `lumen.scene.gravity(gravity)`

Gets (no argument) or sets the gravity vector.
- param `gravity?` : Vec3
- returns `Vec3?`

### `lumen.input.down(key)`

True while the key / button is held. Names: letters "a".."z", digits, "space", "shift", "ctrl", "escape", "enter", "tab", arrows ("up","down","left","right"), "mouse1".."mouse3", "f1".."f12".
- param `key` : string
- returns `boolean`

### `lumen.input.pressed(key)`

True only on the frame the key was pressed.
- param `key` : string
- returns `boolean`

### `lumen.input.mouse_delta()`

Mouse movement since the last frame.
- returns `number dx, number dy`

### `lumen.input.mouse_pos()`

Mouse position in pixels.
- returns `number x, number y`

### `lumen.input.wheel()`

Wheel scroll since the last frame.
- returns `number`

### `lumen.input.set_cursor_locked(locked)`

Hides and captures the cursor (first person controls).
- param `locked` : boolean

### `lumen.input.cursor_locked()`


- returns `boolean`

### `lumen.log.info(...)`


- param `...` : any

### `lumen.log.warn(...)`


- param `...` : any

### `lumen.log.error(...)`


- param `...` : any

### `lumen.log.debug(...)`


- param `...` : any

### `lumen.debug.text(x, y, text)`

Draws text on the game view for one frame.
- param `x` : number pixels from the left
- param `y` : number pixels from the top
- param `text` : string

### `lumen.debug.line(from, to)`

Draws a debug line for one frame.
- param `from` : Vec3
- param `to` : Vec3

### `lumen.events.on(name, handler)`

Subscribes to a named event.
- param `name` : string
- param `handler` : function

### `lumen.events.emit(name, ...)`

Calls every handler of the event with the arguments.
- param `name` : string
- param `...` : any

### `lumen.vec2(x, y)`

Creates a 2D vector. Also available as `lumen.mathx.vec2`.
- param `x?` : number
- param `y?` : number
- returns `Vec2`

### `lumen.vec3(x, y, z)`

Creates a 3D vector (`v.x`, `v.y`, `v.z`; supports + - * / and unary minus).
- param `x?` : number
- param `y?` : number
- param `z?` : number
- returns `Vec3`

### `lumen.vec4(x, y, z, w)`

Creates a 4D vector.
- param `x?` : number
- param `y?` : number
- param `z?` : number
- param `w?` : number
- returns `Vec4`

### `lumen.quat(x, y, z, w)`

Creates a quaternion.
- param `x?` : number
- param `y?` : number
- param `z?` : number
- param `w?` : number
- returns `Quat`

### `lumen.mat4(...)`

Creates a 4x4 matrix (identity without arguments).
- param `...` : number
- returns `Mat4`


## Physics (Jolt)

Bodies are simulated by Jolt through Wicked. Components on an entity: **Rigid body** (static/dynamic/kinematic, shapes, character controller, 2D lock, buoyancy, sleep), **Vehicle** (car or motorcycle on a dynamic body), **Joint** (fixed, point, distance, hinge, cone, sixdof, swingtwist, slider; optional break distance).

- `Entity:set_angular_velocity(v)`, `Entity:add_torque(t)`, `Entity:add_force_at(f, p, local?)`, `Entity:add_impulse_at(i, p, local?)`
- `Entity:wake()`, `Entity:sleep()`
- `Entity:drive(throttle, steer, brake?, handbrake?)`, `Entity:speed()`
- `Entity:break_joint(broken?)`, `Entity:joint_broken()`
- `lumen.physics.raycast(origin, dir, max?)`, `overlap_sphere(center, r)`, `overlap_box(center, half)`, `gravity([v])`

**Triggers.** A rigid body with *Trigger* enabled has no collision; entities with a body that enter or leave its volume raise `on_trigger_enter(self, other)` / `on_trigger_exit(self, other)` in the scripts of both entities and the `trigger_enter` / `trigger_exit` events (`lumen.events.on("trigger_enter", function(trigger, other) end)`). Overlap is tested against world bounds (AABB).

## Animation

Clips imported with a model (glTF/FBX) are addressed by name (exact or partial) or index:
`entity:animations()`, `entity:play(clip, {loop, speed, weight, fade, exclusive, restart, time})`, `entity:stop_animation(clip?, fade?)`,
`entity:animation(clip?)`, `entity:set_animation(clip, {time, speed, weight, loop, playing})`.
`fade` cross fades with smoothstep; `exclusive = true` fades out the other clips. `require "lumen.animator"` builds a parameter driven
state machine on top of it (see the header of `lua/lumen/animator.lua`).

## Prefabs

Right click an object in the hierarchy -> *Save as prefab* writes `prefabs/<Name>.prefab` (the whole subtree; joints and vehicle wheel links stay
consistent). Drag a prefab into the viewport or double click it in the asset browser to place it. At runtime
`lumen.scene.instantiate("prefabs/Crate.prefab", position, rotation)` creates the entities, starts their scripts and returns the root.
`Ctrl+C / Ctrl+X / Ctrl+V` use the system clipboard, so objects can be pasted into other scenes and projects (`Ctrl+Shift+V` pastes as child).

## In-game UI

`lumen.ui` (rect, text, image, line, mouse, screen) is an immediate mode overlay; `require "lumen.gui"` provides a theme and widgets:
`gui.begin_frame()`, `gui.panel`, `gui.label`, `gui.bar`, `gui.button(id, x, y, w, h, text)`, `gui.toggle`, `gui.slider`, `gui.anchor(where, w, h, margin)`.

## Export

*Build > Export game* produces a standalone folder: `<Game>.exe`, `data.lpak` (Lua precompiled, scenes, assets, Lua libraries) and the runtime libraries.
The player unpacks the package once into `.lumen-data/` next to it and runs the project exactly like Play mode.
