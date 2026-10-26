local t = require "lumen.test"
local tween = require "lumen.tween"
local pf = require "lumen.pathfind"
local fsm = require "lumen.statemachine"
local ecs = require "lumen.ecs"
local Animator = require "lumen.animator"

t.describe("tween", function()
  t.it("reaches the goal and fires on_complete", function()
    local m, o, done = tween.new(), { x = 0 }, false
    m:to(o, { x = 10 }, 1, { ease = "quad_out", on_complete = function() done = true end })
    m:update(0.5)
    t.truthy(o.x > 5 and o.x < 10)
    m:update(0.6)
    t.eq(o.x, 10) t.truthy(done) t.eq(m:count(), 0)
  end)
  t.it("has symmetric easings", function()
    for name, f in pairs(tween.ease) do t.near(f(0), 0, 1e-9, name) t.near(f(1), 1, 1e-9, name) end
  end)
  t.it("honours delay", function()
    local m, o = tween.new(), { x = 0 }
    m:to(o, { x = 1 }, 1, { delay = 1 })
    m:update(0.5) t.eq(o.x, 0)
  end)
end)

t.describe("pathfind", function()
  local map = { "S..#....", ".#.#.##.", ".#...#..", ".####.#.", "......#G" }
  local function walk(x, y) local r = map[y + 1] return r ~= nil and x >= 0 and x < #r and r:sub(x + 1, x + 1) ~= "#" end
  t.it("finds a path around walls", function()
    local p, cost = pf.grid(walk, 0, 0, 7, 4)
    t.truthy(p) t.eq(p[1], { x = 0, y = 0 }) t.eq(p[#p], { x = 7, y = 4 }) t.truthy(cost > 0)
  end)
  t.it("returns nil when blocked", function()
    t.is_nil(pf.grid(function(x, y) return (x == 0 or x == 5) and y >= 0 and y < 5 end, 0, 0, 5, 0))
  end)
  t.it("straight line is optimal", function()
    local p, c = pf.grid(function(x, y) return x >= 0 and x < 10 and y == 0 end, 0, 0, 9, 0)
    t.eq(#p, 10) t.near(c, 9, 1e-9)
  end)
end)

t.describe("statemachine", function()
  t.it("runs hooks and guards", function()
    local log, o = {}, { hp = 1 }
    local f = fsm.new({
      alive = { enter = function() log[#log + 1] = "a+" end, exit = function() log[#log + 1] = "a-" end },
      dead = { enter = function() log[#log + 1] = "d+" end },
    }, "alive", o):when("alive", "dead", function(s) return s.hp <= 0 end)
    f:update(0.1) t.eq(f.current, "alive")
    o.hp = 0 f:update(0.1)
    t.eq(f.current, "dead") t.eq(log, { "a+", "a-", "d+" })
  end)
end)

t.describe("ecs", function()
  t.it("queries and systems", function()
    local w = ecs.new()
    local a = w:spawn({ pos = { x = 0 }, vel = { x = 2 } })
    w:spawn({ pos = { x = 5 } })
    w:system("move", function(world, dt) for _, p, v in world:query("pos", "vel") do p.x = p.x + v.x * dt end end)
    w:update(0.5)
    t.eq(w:get(a, "pos").x, 1)
    w:despawn(a)
    t.eq(w:spawn({}), a)
  end)
end)

t.describe("animator", function()
  -- fake entity: clips advance with time, play() records the request
  local function fake()
    local e = { clips = { Idle = { length = 1 }, Run = { length = 1 }, Jump = { length = 0.5 } }, log = {}, time = {} }
    function e:play(name, o) self.log[#self.log + 1] = name self.time[name] = 0 self.last = o end
    function e:animation(name) local c = self.clips[name] return c and { time = self.time[name] or 0, length = c.length } end
    function e:set_animation(name, o) self.speed = o.speed end
    function e:advance(dt) for k in pairs(self.time) do self.time[k] = self.time[k] + dt end end
    return e
  end
  local def = function() return {
    initial = "idle", params = { speed = 0 },
    states = { idle = { clip = "Idle" }, run = { clip = "Run", speed = function(p) return 1 + p.speed end },
               jump = { clip = "Jump", loop = false, next = "idle" } },
    transitions = {
      { from = "idle", to = "run", when = function(p) return p.speed > 0.2 end, fade = 0.2 },
      { from = "run", to = "idle", when = function(p) return p.speed <= 0.2 end },
      { from = "*", to = "jump", trigger = "jump", fade = 0.1 },
    } } end
  t.it("starts in the initial state and follows parameters", function()
    local e = fake() local a = Animator.new(e, def())
    t.eq(a:state(), "idle") t.eq(e.log[1], "Idle")
    a:update(0.1) t.eq(a:state(), "idle")
    a:set("speed", 3) a:update(0.1)
    t.eq(a:state(), "run") t.eq(e.last.fade, 0.2) t.eq(e.last.exclusive, true)
    a:update(0.1) t.eq(e.speed, 4)
  end)
  t.it("triggers interrupt and one shot clips return to next", function()
    local e = fake() local a = Animator.new(e, def())
    a:trigger("jump") a:update(0.1)
    t.eq(a:state(), "jump") t.eq(e.last.loop, false)
    e:advance(0.6) a:update(0.1)
    t.eq(a:state(), "idle")
  end)
  t.it("calls enter/exit listeners", function()
    local e = fake() local a = Animator.new(e, def()) local log = {}
    a:on("enter", function(_, n) log[#log + 1] = "+" .. n end)
    a:on("exit", function(_, n) log[#log + 1] = "-" .. n end)
    a:set("speed", 1) a:update(0.1)
    t.eq(log, { "-idle", "+run" })
  end)
end)

t.describe("gui", function()
  local cmds, mouse
  lumen = { ui = {
    screen = function() return 1280, 720 end,
    mouse = function() return mouse.x, mouse.y, mouse.down, mouse.pressed end,
    rect = function(...) cmds[#cmds + 1] = { "rect", ... } end,
    text = function(...) cmds[#cmds + 1] = { "text", ... } end,
  } }
  local gui = require "lumen.gui"
  local function frame(m) mouse = m cmds = {} gui.begin_frame() end
  t.it("anchors boxes to the screen", function()
    frame({ x = 0, y = 0, down = false, pressed = false })
    local x, y = gui.anchor("bottom_right", 100, 40, 10)
    t.eq(x, 1170) t.eq(y, 670)
    x, y = gui.anchor("center", 200, 100) t.eq(x, 540) t.eq(y, 310)
  end)
  t.it("button reports a click on release over it", function()
    frame({ x = 50, y = 50, down = false, pressed = false }) t.eq(gui.button("b", 10, 10, 100, 100, "ok"), false)
    frame({ x = 50, y = 50, down = true, pressed = true }) t.eq(gui.button("b", 10, 10, 100, 100, "ok"), false)
    frame({ x = 50, y = 50, down = false, pressed = false }) t.eq(gui.button("b", 10, 10, 100, 100, "ok"), true)
    frame({ x = 50, y = 50, down = false, pressed = false }) t.eq(gui.button("b", 10, 10, 100, 100, "ok"), false)
  end)
  t.it("button is not clicked when released elsewhere", function()
    frame({ x = 50, y = 50, down = true, pressed = true }) gui.button("c", 10, 10, 100, 100)
    frame({ x = 500, y = 500, down = false, pressed = false }) t.eq(gui.button("c", 10, 10, 100, 100), false)
  end)
  t.it("slider follows the mouse while dragged", function()
    frame({ x = 100, y = 10, down = true, pressed = true }) local v = gui.slider("s", 0, 0, 200, 20, 0, 0, 10)
    t.near(v, 5, 1e-6)
    frame({ x = 200, y = 10, down = true, pressed = false }) v = gui.slider("s", 0, 0, 200, 20, v, 0, 10)
    t.near(v, 10, 1e-6)
  end)
end)
os.exit(t.run() and 0 or 1)
