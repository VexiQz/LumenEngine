-- lumen.animator: parameter driven animation state machine on top of entity:play().
--
--   local Animator = require "lumen.animator"
--   local anim = Animator.new(self.entity, {
--     initial = "idle",
--     params  = { speed = 0, grounded = true },
--     states  = {
--       idle = { clip = "Idle" },
--       run  = { clip = "Run", speed = function(p) return 0.6 + p.speed / 6 end },
--       jump = { clip = "Jump", loop = false, next = "idle", on_enter = function(a) ... end },
--     },
--     transitions = {
--       { from = "idle", to = "run",  when = function(p) return p.speed > 0.2 end, fade = 0.15 },
--       { from = "run",  to = "idle", when = function(p) return p.speed <= 0.2 end, fade = 0.2 },
--       { from = "*",    to = "jump", trigger = "jump", fade = 0.1 },
--     },
--   })
--   anim:set("speed", v) ; anim:trigger("jump") ; anim:update(dt)
--
-- A state with `loop = false` and `next` returns to that state when the clip ended; `exit_time` (0..1) on a
-- transition delays it until the current clip reached that fraction. Transitions are checked in order, the first match wins.
local M = {}
local A = {}
A.__index = A

local function resolve(v, params)
  if type(v) == "function" then return v(params) end
  return v
end

function M.new(entity, def)
  assert(type(def) == "table" and def.states, "animator: definition needs `states`")
  local a = setmetatable({
    entity = entity, def = def, params = {}, triggers = {}, current = nil, time = 0, listeners = {},
  }, A)
  for k, v in pairs(def.params or {}) do a.params[k] = v end
  if def.initial then a:go(def.initial, 0) end
  return a
end

function A:set(name, value) self.params[name] = value end
function A:get(name) return self.params[name] end
function A:trigger(name) self.triggers[name] = true end
function A:state() return self.current end
function A:on(event, fn) self.listeners[event] = fn end

local function emit(self, ev, ...)
  local fn = self.listeners[ev]
  if fn then fn(self, ...) end
end

-- Switches to `name` immediately with a cross fade of `fade` seconds.
function A:go(name, fade)
  local st = assert(self.def.states[name], "animator: unknown state " .. tostring(name))
  local prev = self.current
  if prev then
    local ps = self.def.states[prev]
    if ps.on_exit then ps.on_exit(self, name) end
    emit(self, "exit", prev, name)
  end
  self.current, self.time = name, 0
  if st.clip then
    self.entity:play(st.clip, {
      loop = st.loop ~= false, speed = resolve(st.speed, self.params) or 1, weight = st.weight or 1,
      fade = fade or 0, exclusive = true, restart = true,
    })
  end
  if st.on_enter then st.on_enter(self, prev) end
  emit(self, "enter", name, prev)
end

local function progress(self, st)
  if not st.clip then return 1 end
  local info = self.entity:animation(st.clip)
  if not info or info.length <= 0 then return 1 end
  return info.time / info.length
end

function A:update(dt)
  local st = self.def.states[self.current]
  if not st then return end
  self.time = self.time + dt
  if st.speed and st.clip and type(st.speed) == "function" then
    self.entity:set_animation(st.clip, { speed = st.speed(self.params) })
  end
  if st.update then st.update(self, dt) end
  local p = progress(self, st)
  for _, tr in ipairs(self.def.transitions or {}) do
    local match = tr.from == "*" or tr.from == self.current
    if match and tr.to == self.current and not tr.allow_self then match = false end
    if match then
      local ok = true
      if tr.trigger then ok = self.triggers[tr.trigger] == true end
      if ok and tr.when then ok = tr.when(self.params, self) and true or false end
      if ok and tr.exit_time then ok = p >= tr.exit_time end
      if ok then
        if tr.trigger then self.triggers[tr.trigger] = nil end
        self:go(tr.to, tr.fade or 0.15)
        self.triggers = {}
        return
      end
    end
  end
  -- one shot clips fall through to `next`
  if st.loop == false and st.next and p >= 0.999 then
    self:go(st.next, st.next_fade or 0.15)
  end
  self.triggers = {} -- triggers live for one update
end

return M
