-- lumen.pathfind: A* on grids and arbitrary graphs.
local M = {}

local Heap = {}
Heap.__index = Heap
local function heap() return setmetatable({ n = 0, k = {}, v = {} }, Heap) end
function Heap:push(val, key)
  local n = self.n + 1
  self.n = n
  local k, v = self.k, self.v
  k[n], v[n] = key, val
  while n > 1 do
    local p = n // 2
    if k[p] <= k[n] then break end
    k[p], k[n], v[p], v[n] = k[n], k[p], v[n], v[p]
    n = p
  end
end
function Heap:pop()
  local n = self.n
  if n == 0 then return nil end
  local k, v = self.k, self.v
  local top = v[1]
  k[1], v[1] = k[n], v[n]
  k[n], v[n] = nil, nil
  n = n - 1
  self.n = n
  local i = 1
  while true do
    local l, r, s = i * 2, i * 2 + 1, i
    if l <= n and k[l] < k[s] then s = l end
    if r <= n and k[r] < k[s] then s = r end
    if s == i then break end
    k[s], k[i], v[s], v[i] = k[i], k[s], v[i], v[s]
    i = s
  end
  return top
end
M.heap = heap

-- Generic A*. neighbors(node) -> iterator of (node, cost); heuristic(a, b); key(node) -> hashable.
function M.astar(start, goal, neighbors, heuristic, key)
  key = key or function(n) return n end
  local open, g, came, closed = heap(), {}, {}, {}
  local sk = key(start)
  g[sk] = 0
  open:push(start, heuristic(start, goal))
  local gk = key(goal)
  while true do
    local cur = open:pop()
    if cur == nil then return nil end
    local ck = key(cur)
    if ck == gk then
      local path = { cur }
      while came[ck] do
        cur = came[ck]
        ck = key(cur)
        table.insert(path, 1, cur)
      end
      return path, g[gk]
    end
    if not closed[ck] then
      closed[ck] = true
      for nb, cost in neighbors(cur) do
        local nk = key(nb)
        local ng = g[ck] + cost
        if not closed[nk] and (g[nk] == nil or ng < g[nk]) then
          g[nk] = ng
          came[nk] = cur
          open:push(nb, ng + heuristic(nb, goal))
        end
      end
    end
  end
end

-- Grid: walkable(x, y) -> bool; options {diagonal=true}. Returns list of {x=,y=} and cost.
function M.grid(walkable, sx, sy, gx, gy, opt)
  opt = opt or {}
  local diag = opt.diagonal ~= false
  local function key(n) return n.x .. "," .. n.y end
  local function nbrs(n)
    local list, i = {}, 0
    for dx = -1, 1 do
      for dy = -1, 1 do
        if (dx ~= 0 or dy ~= 0) and (diag or dx == 0 or dy == 0) and walkable(n.x + dx, n.y + dy) then
          -- no corner cutting
          if dx == 0 or dy == 0 or (walkable(n.x + dx, n.y) and walkable(n.x, n.y + dy)) then
            i = i + 1
            list[i] = { { x = n.x + dx, y = n.y + dy }, (dx ~= 0 and dy ~= 0) and 1.41421356 or 1 }
          end
        end
      end
    end
    local j = 0
    return function() j = j + 1 local e = list[j] if e then return e[1], e[2] end end
  end
  local function h(a, b)
    local dx, dy = math.abs(a.x - b.x), math.abs(a.y - b.y)
    if not diag then return dx + dy end
    return (dx + dy) + (1.41421356 - 2) * math.min(dx, dy)
  end
  if not walkable(gx, gy) or not walkable(sx, sy) then return nil end
  return M.astar({ x = sx, y = sy }, { x = gx, y = gy }, nbrs, h, key)
end

return M
