---@meta
-- Lumen Engine game API. Available in every script (main script and entity scripts).

---A vector of 2, 3 or 4 numbers: `{x, y, z}` tables or `lumen.vec3(x, y, z)` values are accepted.
---@class Vec3
---@field x number
---@field y number
---@field z number
local Vec3 = {}

---@class Light
---@field intensity number Light intensity.
---@field range number Range in metres (point and spot lights).
---@field color Vec3 Linear RGB colour.
local Light = {}

---A scene entity. Equality compares identity: `a == b`.
---@class Entity
local Entity = {}

---Entity name as shown in the hierarchy.
---@return string
function Entity:name() end
---Stable id stored in the scene file.
---@return string
function Entity:id() end
---False once the entity has been destroyed.
---@return boolean
function Entity:valid() end

---World position.
---@return Vec3
function Entity:position() end
---Moves the entity to a world position. Rigid bodies are teleported.
---@param position Vec3
function Entity:set_position(position) end
---Position relative to the parent.
---@return Vec3
function Entity:local_position() end
---@param position Vec3
function Entity:set_local_position(position) end
---Moves the entity by `delta` in world space.
---@param delta Vec3
function Entity:translate(delta) end
---World rotation as euler angles in degrees (pitch X, yaw Y, roll Z).
---@return Vec3
function Entity:rotation() end
---@param euler Vec3 degrees (pitch, yaw, roll)
function Entity:set_rotation(euler) end
---@return Vec3
function Entity:local_rotation() end
---@param euler Vec3
function Entity:set_local_rotation(euler) end
---Local scale.
---@return Vec3
function Entity:scale() end
---Sets the local scale; a single number scales uniformly.
---@param scale Vec3|number
function Entity:set_scale(scale) end
---Unit vector along the entity's +Z axis in world space.
---@return Vec3
function Entity:forward() end
---Unit vector along +X in world space.
---@return Vec3
function Entity:right() end
---Unit vector along +Y in world space.
---@return Vec3
function Entity:up() end
---Rotates the entity so +Z points at `target`.
---@param target Vec3
function Entity:look_at(target) end
---Shows or hides the entity's mesh.
---@param active boolean
function Entity:set_active(active) end

---Parent entity or nil.
---@return Entity?
function Entity:parent() end
---Direct child with the given name.
---@param name string
---@return Entity?
function Entity:child(name) end
---All direct children.
---@return Entity[]
function Entity:children() end
---Removes the entity (and its components) from the scene.
function Entity:destroy() end

---Linear velocity of the rigid body (zero without one).
---@return Vec3
function Entity:velocity() end

---Navigation agent (entity needs a `navagent` component and a baked navmesh): walks to `target` along the navmesh.
---Returns false when no path exists. Scripts receive `on_nav_arrive(self)` / `on_nav_failed(self)` (failed = only a partial path).
---@param target Vec3
---@return boolean
---Plays a `.lanim` keyframe clip on this entity. Returns a clip id (nil when the file cannot be loaded).
---Callbacks on the entity's script: `on_anim_event(self, name)` and `on_anim_end(self, path)`.
---@param path string
---@param opts? {loop:boolean, speed:number, weight:number}
---@return integer|nil
function Entity:play_clip(path, opts) end
---Stops every keyframe clip playing on this entity.
function Entity:stop_clips() end
function Entity:agent_move_to(target) end
---Stops the agent.
function Entity:agent_stop() end
---Overrides the agent speed (m/s).
---@param speed number
function Entity:agent_set_speed(speed) end
---@return boolean
function Entity:agent_moving() end
---Remaining path length (horizontal, meters).
---@return number
function Entity:agent_remaining() end
---Current agent velocity.
---@return Vec3
function Entity:agent_velocity() end
---@param velocity Vec3
function Entity:set_velocity(velocity) end
---Applies a continuous force this step.
---@param force Vec3
function Entity:add_force(force) end
---Applies an instantaneous impulse.
---@param impulse Vec3
function Entity:add_impulse(impulse) end
---Sets the angular velocity (rad/s, world space).
---@param velocity Vec3
function Entity:set_angular_velocity(velocity) end
---Applies a torque this step.
---@param torque Vec3
function Entity:add_torque(torque) end
---Applies a force at a point.
---@param force Vec3
---@param point Vec3
---@param local_space? boolean point is in the body's local space (default false = world)
function Entity:add_force_at(force, point, local_space) end
---Applies an impulse at a point.
---@param impulse Vec3
---@param point Vec3
---@param local_space? boolean
function Entity:add_impulse_at(impulse, point, local_space) end
---Wakes a sleeping body.
function Entity:wake() end
---Puts the body to sleep.
function Entity:sleep() end
---Drives a vehicle component (car / motorcycle).
---@param throttle number -1..1 (negative = reverse)
---@param steer number -1..1 (1 = right)
---@param brake? number 0..1
---@param handbrake? number 0..1
function Entity:drive(throttle, steer, brake, handbrake) end
---Signed forward speed of a vehicle, speed magnitude of any other body (m/s).
---@return number
function Entity:speed() end
---Breaks (or with `false` restores) the joint component of the entity.
---@param broken? boolean default true
function Entity:break_joint(broken) end
---True when the entity's joint has broken.
---@return boolean
function Entity:joint_broken() end
---Names of the animation clips that came with the entity's model.
---@return string[]
function Entity:animations() end
---@class PlayOptions
---@field loop? boolean default true
---@field speed? number default 1
---@field weight? number blend weight 0..1, default 1
---@field fade? number cross fade seconds
---@field restart? boolean start from the beginning even when already playing
---@field exclusive? boolean fade out every other clip of the entity
---@field time? number start time in seconds
---Plays an animation clip (name, partial name or 1-based index).
---@param clip string|integer
---@param options? PlayOptions
---@return boolean
function Entity:play(clip, options) end
---Stops one clip (or all when `clip` is nil), optionally fading out.
---@param clip? string|integer
---@param fade? number
function Entity:stop_animation(clip, fade) end
---@class AnimationInfo
---@field name string
---@field time number
---@field length number
---@field playing boolean
---@field loop boolean
---@field speed number
---@field weight number
---State of a clip, or of the first playing clip when `clip` is nil.
---@param clip? string|integer
---@return AnimationInfo?
function Entity:animation(clip) end
---Changes time, speed, weight, loop or playing of a clip.
---@param clip string|integer
---@param values {time?: number, speed?: number, weight?: number, loop?: boolean, playing?: boolean}
function Entity:set_animation(clip, values) end
---Moves a character controller (capsule body with `lockRotation`).
---@param direction Vec3 desired movement direction (unit length)
---@param speed? number metres per second, default 6
---@param jump? number jump speed
---@param control_in_air? boolean allow steering while airborne
function Entity:move(direction, speed, jump, control_in_air) end
---True when a character or body stands on something.
---@return boolean
function Entity:grounded() end
---Light component of the entity, or nil.
---@return Light?
function Entity:light() end
---Sets the base colour of the entity's material (RGB or RGBA, linear).
---@param color Vec3|number[]
function Entity:set_color(color) end

---@class RayHit
---@field entity Entity
---@field position Vec3
---@field normal Vec3
---@field distance number

---The base class of entity scripts: return a table with these functions.
---Plain (non function) fields are editable properties in the inspector.
---@class Script
---@field entity Entity The entity the script is attached to.
local Script = {}
---Called once when the game starts (or the entity is created).
function Script:start() end
---Called every frame.
---@param dt number seconds since the last frame
function Script:update(dt) end
---Called at the fixed physics rate.
function Script:fixed_update() end
---Called when the entity is destroyed.
function Script:on_destroy() end

-- lumen -------------------------------------------------------------------------------------------------
lumen = {}
---Engine version.
---@type string
lumen.version = "1.9.2"
---Stops the game.
function lumen.quit() end
---Seconds since the game started.
---@return number
function lumen.elapsed() end
---Seconds of the last frame.
---@return number
function lumen.delta_time() end

lumen.scene = {}
---First entity with this name.
---@param name string
---@return Entity?
function lumen.scene.find(name) end
---Every entity with this name.
---@param name string
---@return Entity[]
function lumen.scene.find_all(name) end
---Every entity carrying the tag.
---@param tag string
---@return Entity[]
function lumen.scene.find_by_tag(tag) end
---Creates a primitive at runtime ("cube", "sphere", "plane", "cylinder", "cone", "capsule").
---@param primitive string
---@param position Vec3
---@param size? Vec3
---@param physics? boolean add a dynamic rigid body
---@return Entity
function lumen.scene.spawn(primitive, position, size, physics) end
---Casts a ray against the scene.
---@param origin Vec3
---@param direction Vec3
---@param max_distance? number default 1000
---@return RayHit?
function lumen.scene.raycast(origin, direction, max_distance) end
---Creates the entities of a prefab asset (components, scripts, triggers) and returns its root.
---@param prefab string project relative `.prefab` path
---@param position? Vec3
---@param rotation? Vec3 euler degrees
---@return Entity
function lumen.scene.instantiate(prefab, position, rotation) end
---Loads another scene (project relative path) and restarts scripts.
---@param path string
---@return boolean
function lumen.scene.load(path) end
---Gets (no argument) or sets the gravity vector.
---@param gravity? Vec3
---@return Vec3?
function lumen.scene.gravity(gravity) end

lumen.input = {}
---True while the key / button is held. Names: letters "a".."z", digits, "space", "shift", "ctrl", "escape", "enter", "tab", arrows ("up","down","left","right"), "mouse1".."mouse3", "f1".."f12".
---@param key string
---@return boolean
function lumen.input.down(key) end
---True only on the frame the key was pressed.
---@param key string
---@return boolean
function lumen.input.pressed(key) end
---Mouse movement since the last frame.
---@return number dx, number dy
function lumen.input.mouse_delta() end
---Mouse position in pixels.
---@return number x, number y
function lumen.input.mouse_pos() end
---Wheel scroll since the last frame.
---@return number
function lumen.input.wheel() end
---Hides and captures the cursor (first person controls).
---@param locked boolean
function lumen.input.set_cursor_locked(locked) end
---@return boolean
function lumen.input.cursor_locked() end

lumen.log = {}
---@param ... any
function lumen.log.info(...) end
---@param ... any
function lumen.log.warn(...) end
---@param ... any
function lumen.log.error(...) end
---@param ... any
function lumen.log.debug(...) end

lumen.debug = {}
---Draws text on the game view for one frame.
---@param x number pixels from the left
---@param y number pixels from the top
---@param text string
function lumen.debug.text(x, y, text) end
---Draws a debug line for one frame.
---@param from Vec3
---@param to Vec3
function lumen.debug.line(from, to) end

--- Pauses the game in the editor debugger at the next executed line (no effect outside the editor or when debugging is disabled).
function lumen.debug.breakpoint() end

---@class PhysicsHit
---@field entity Entity
---@field position Vec3
---@field normal Vec3
---@field distance number
lumen.physics = {}
---Ray against Jolt bodies only (ignores meshes without a rigidbody).
---@param origin Vec3
---@param direction Vec3
---@param max_distance? number default 1000
---@return PhysicsHit?
function lumen.physics.raycast(origin, direction, max_distance) end
---Entities whose bounds touch the sphere.
---@param center Vec3
---@param radius number
---@return Entity[]
function lumen.physics.overlap_sphere(center, radius) end
---Entities whose bounds touch the axis aligned box.
---@param center Vec3
---@param half_extents Vec3
---@return Entity[]
function lumen.physics.overlap_box(center, half_extents) end
---Gets or sets the world gravity.
---@param gravity? Vec3
---@return Vec3?
function lumen.physics.gravity(gravity) end

---Events raised by the engine: `trigger_enter(trigger, other)`, `trigger_exit(trigger, other)`.
---Scripts on either entity also receive `on_trigger_enter(self, other)` / `on_trigger_exit(self, other)`.
---@alias UiColor number[]|string  {r,g,b[,a]} in 0..1 or "#rrggbb[aa]"
---Immediate mode 2D overlay: call every frame from `update`, commands are cleared each frame. Coordinates are pixels of the game
---screen, origin top left. `require "lumen.gui"` adds widgets (panel, label, bar, button, toggle, slider).
lumen.ui = {}
---@return number width
---@return number height
function lumen.ui.screen() end
---@param x number
---@param y number
---@param w number
---@param h number
---@param color UiColor
---@param options? {rounding?: number, outline?: number} outline > 0 draws a frame instead of a fill
function lumen.ui.rect(x, y, w, h, color, options) end
---@param x number
---@param y number
---@param text any
---@param options? {size?: number, color?: UiColor, align?: "left"|"center"|"right", shadow?: boolean}
function lumen.ui.text(x, y, text, options) end
---@param path string image asset
---@param x number
---@param y number
---@param w number
---@param h number
---@param tint? UiColor
function lumen.ui.image(path, x, y, w, h, tint) end
---@param x1 number
---@param y1 number
---@param x2 number
---@param y2 number
---@param color? UiColor
---@param thickness? number
function lumen.ui.line(x1, y1, x2, y2, color, thickness) end
---@return number x
---@return number y
---@return boolean left_down
---@return boolean left_pressed
---@return boolean right_down
---@return boolean right_pressed
function lumen.ui.mouse() end

lumen.events = {}
---Subscribes to a named event.
---@param name string
---@param handler function
function lumen.events.on(name, handler) end
---Calls every handler of the event with the arguments.
---@param name string
---@param ... any
function lumen.events.emit(name, ...) end

---Creates a 2D vector. Also available as `lumen.mathx.vec2`.
---@param x? number
---@param y? number
---@return Vec2
function lumen.vec2(x, y) end
---Creates a 3D vector (`v.x`, `v.y`, `v.z`; supports + - * / and unary minus).
---@param x? number
---@param y? number
---@param z? number
---@return Vec3
function lumen.vec3(x, y, z) end
---Creates a 4D vector.
---@param x? number
---@param y? number
---@param z? number
---@param w? number
---@return Vec4
function lumen.vec4(x, y, z, w) end
---Creates a quaternion.
---@param x? number
---@param y? number
---@param z? number
---@param w? number
---@return Quat
function lumen.quat(x, y, z, w) end
---Creates a 4x4 matrix (identity without arguments).
---@param ... number
---@return Mat4
function lumen.mat4(...) end

---Navigation mesh queries (the mesh is baked from static meshes inside a `navmesh` entity's volume when the game starts).
---@class lumen.nav
---Keyframe clips (`.lanim`, authored in the Timeline panel).
lumen.anim = {}
---@param entity Entity
---@param path string
---@param opts? {loop:boolean, speed:number, weight:number}
---@return integer|nil
function lumen.anim.play(entity, path, opts) end
---Stops a clip by id, or all clips of an entity.
---@param target integer|Entity
function lumen.anim.stop(target) end
---@param id integer
---@param speed? number
---@param playing? boolean
function lumen.anim.set(id, speed, playing) end
---@param id integer
---@return number|nil time, number duration
function lumen.anim.time(id) end

lumen.nav = {}
---@return boolean
function lumen.nav.is_baked() end
---Re-bakes the navmesh (e.g. after spawning static geometry). Returns true when something walkable was found.
---@return boolean
function lumen.nav.bake() end
---Shortest smoothed path between two points, or nil. With `allow_partial` the path ends at the closest reachable point.
---@param from Vec3
---@param to Vec3
---@param allow_partial? boolean
---@return Vec3[]|nil
function lumen.nav.find_path(from, to, allow_partial) end
---Nearest walkable point within `max_distance` (default 2), or nil.
---@param p Vec3
---@param max_distance? number
---@return Vec3|nil
function lumen.nav.project(p, max_distance) end
---Walks the segment on the navmesh. Returns `clear, last_point`.
---@param from Vec3
---@param to Vec3
---@return boolean clear
---@return Vec3 hit
function lumen.nav.raycast(from, to) end
---Random walkable point around `center` (default radius 10), or nil.
---@param center Vec3
---@param radius? number
---@return Vec3|nil
function lumen.nav.random_point(center, radius) end
