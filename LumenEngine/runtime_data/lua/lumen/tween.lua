-- lumen.tween: easing functions and a tween manager.
local M = {}
local pi, sin, cos, sqrt = math.pi, math.sin, math.cos, math.sqrt

local function out(f) return function(t) return 1 - f(1 - t) end end
local function inout(f) return function(t) if t < 0.5 then return f(t * 2) / 2 end return 1 - f((1 - t) * 2) / 2 end end

local base = {
  linear = function(t) return t end,
  quad = function(t) return t * t end,
  cubic = function(t) return t ^ 3 end,
  quart = function(t) return t ^ 4 end,
  quint = function(t) return t ^ 5 end,
  sine = function(t) return 1 - cos(t * pi / 2) end,
  expo = function(t) return t == 0 and 0 or 2 ^ (10 * (t - 1)) end,
  circ = function(t) return 1 - sqrt(1 - t * t) end,
  back = function(t) local s = 1.70158 return t * t * ((s + 1) * t - s) end,
  elastic = function(t)
    if t == 0 or t == 1 then return t end
    return -(2 ^ (10 * (t - 1))) * sin((t - 1.075) * (2 * pi) / 0.3)
  end,
  bounce = nil,
}
local function bounce_out(t)
  local n, d = 7.5625, 2.75
  if t < 1 / d then return n * t * t
  elseif t < 2 / d then t = t - 1.5 / d return n * t * t + 0.75
  elseif t < 2.5 / d then t = t - 2.25 / d return n * t * t + 0.9375 end
  t = t - 2.625 / d
  return n * t * t + 0.984375
end

M.ease = { linear = base.linear }
for name, f in pairs(base) do
  if name ~= "linear" then
    M.ease[name .. "_in"] = f
    M.ease[name .. "_out"] = out(f)
    M.ease[name .. "_inout"] = inout(f)
  end
end
M.ease.bounce_out = bounce_out
M.ease.bounce_in = function(t) return 1 - bounce_out(1 - t) end
M.ease.bounce_inout = inout(M.ease.bounce_in)

local Manager = {}
Manager.__index = Manager

function M.new() return setmetatable({ list = {} }, Manager) end

-- manager:to(target, {x=1,y=2}, duration, {ease="quad_out", delay=0, on_complete=fn, on_update=fn, loop=false})
function Manager:to(target, goals, duration, opt)
  opt = opt or {}
  local f = opt.ease or "linear"
  if type(f) == "string" then f = assert(M.ease[f], "unknown easing: " .. f) end
  local tw = { target = target, goals = goals, duration = math.max(duration, 1e-9), t = -(opt.delay or 0), ease = f,
               on_complete = opt.on_complete, on_update = opt.on_update, loop = opt.loop, from = nil, done = false }
  self.list[#self.list + 1] = tw
  return tw
end

function Manager:cancel(tw) tw.done = true end
function Manager:clear() self.list = {} end
function Manager:count() return #self.list end

function Manager:update(dt)
  local keep = {}
  for _, tw in ipairs(self.list) do
    if not tw.done then
      tw.t = tw.t + dt
      if tw.t >= 0 then
        if not tw.from then
          tw.from = {}
          for k in pairs(tw.goals) do tw.from[k] = tw.target[k] end
        end
        local p = math.min(tw.t / tw.duration, 1)
        local e = tw.ease(p)
        for k, v in pairs(tw.goals) do tw.target[k] = tw.from[k] + (v - tw.from[k]) * e end
        if tw.on_update then tw.on_update(tw.target, p) end
        if p >= 1 then
          if tw.loop then tw.t = 0 else
            tw.done = true
            if tw.on_complete then tw.on_complete(tw.target) end
          end
        end
      end
    end
    if not tw.done then keep[#keep + 1] = tw end
  end
  self.list = keep
end

return M
