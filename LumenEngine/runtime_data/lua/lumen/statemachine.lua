-- lumen.statemachine: finite state machine with enter/exit/update hooks and transition guards.
local M = {}
local FSM = {}
FSM.__index = FSM

-- states = { idle = {enter=fn, exit=fn, update=fn(self, dt)}, ... }
function M.new(states, initial, owner)
  local f = setmetatable({ states = states, owner = owner, current = nil, transitions = {}, time = 0 }, FSM)
  if initial then f:set(initial) end
  return f
end

function FSM:set(name, ...)
  local nxt = assert(self.states[name], "unknown state: " .. tostring(name))
  local cur = self.current and self.states[self.current]
  if cur and cur.exit then cur.exit(self.owner, name) end
  local prev = self.current
  self.current, self.time = name, 0
  if nxt.enter then nxt.enter(self.owner, prev, ...) end
end

-- guard(owner) -> true causes a switch from `from` (or "*") to `to`
function FSM:when(from, to, guard)
  self.transitions[#self.transitions + 1] = { from = from, to = to, guard = guard }
  return self
end

function FSM:update(dt)
  self.time = self.time + dt
  for _, t in ipairs(self.transitions) do
    if (t.from == "*" or t.from == self.current) and t.to ~= self.current and t.guard(self.owner) then
      self:set(t.to)
      break
    end
  end
  local s = self.states[self.current]
  if s and s.update then s.update(self.owner, dt, self.time) end
end

return M
