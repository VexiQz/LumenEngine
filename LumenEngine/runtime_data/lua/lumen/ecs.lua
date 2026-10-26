-- lumen.ecs: small archetype-less ECS (component stores + cached queries).
local M = {}
local World = {}
World.__index = World

function M.new() return setmetatable({ next = 1, alive = {}, stores = {}, systems = {}, free = {} }, World) end

function World:spawn(components)
  local id = table.remove(self.free) or self.next
  if id == self.next then self.next = self.next + 1 end
  self.alive[id] = true
  for name, c in pairs(components or {}) do self:add(id, name, c) end
  return id
end

function World:despawn(id)
  if not self.alive[id] then return end
  for _, s in pairs(self.stores) do s[id] = nil end
  self.alive[id] = nil
  self.free[#self.free + 1] = id
end

function World:add(id, name, c)
  local s = self.stores[name]
  if not s then s = {} self.stores[name] = s end
  s[id] = c == nil and true or c
end
function World:remove(id, name) local s = self.stores[name] if s then s[id] = nil end end
function World:get(id, name) local s = self.stores[name] return s and s[id] end
function World:has(id, name) return self:get(id, name) ~= nil end

-- Iterates entity ids that have all listed components; yields id, c1, c2, ...
function World:query(...)
  local names = { ... }
  local ids, n = {}, 0
  local first = self.stores[names[1]]
  if first then
    for id in pairs(first) do
      local ok = true
      for i = 2, #names do
        local s = self.stores[names[i]]
        if not s or s[id] == nil then ok = false break end
      end
      if ok then n = n + 1 ids[n] = id end
    end
  end
  table.sort(ids)
  local i = 0
  return function()
    i = i + 1
    local id = ids[i]
    if not id then return nil end
    local out = { id }
    for k = 1, #names do out[k + 1] = self.stores[names[k]][id] end
    return table.unpack(out, 1, #names + 1)
  end
end

function World:system(name, fn, order) self.systems[#self.systems + 1] = { name = name, fn = fn, order = order or #self.systems } table.sort(self.systems, function(a, b) return a.order < b.order end) end
function World:update(dt) for _, s in ipairs(self.systems) do s.fn(self, dt) end end

return M
