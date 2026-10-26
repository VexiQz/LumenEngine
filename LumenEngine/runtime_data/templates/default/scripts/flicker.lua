-- Flickering fire light: noise driven intensity.
local M = { amount = 0.25 }
function M:start()
  self.base = self.entity:light().intensity
  self.t = lumen.random.range(0, 100)
end
function M:update(dt)
  self.t = self.t + dt * 6
  local n = lumen.noise.fbm("perlin", self.t, 0.5, 0, { octaves = 3 }) -- -1..1
  self.entity:light().intensity = self.base * (1 + n * self.amount)
end
return M
