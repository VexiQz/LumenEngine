-- Third person controller. Attached to the "Player" entity (a capsule character body); the camera is its child.
-- Properties (speed, jump, sensitivity) are edited in the inspector.
local M = { speed = 5.5, jump = 6.5, sensitivity = 0.12 }

function M:start()
  self.yaw, self.pitch = 0, 0
  lumen.input.set_cursor_locked(true)
  self.cam = self.entity:child("Camera")
end

function M:update(dt)
  local e = self.entity
  if lumen.input.cursor_locked() then
    local dx, dy = lumen.input.mouse_delta()
    self.yaw = self.yaw + dx * self.sensitivity
    self.pitch = lumen.mathx.clamp(self.pitch + dy * self.sensitivity, -25, 60)
  end
  e:set_rotation(lumen.vec3(0, self.yaw, 0))
  if self.cam then self.cam:set_local_rotation(lumen.vec3(self.pitch, 0, 0)) end

  local f = (lumen.input.down("w") and 1 or 0) - (lumen.input.down("s") and 1 or 0)
  local r = (lumen.input.down("d") and 1 or 0) - (lumen.input.down("a") and 1 or 0)
  local run = lumen.input.down("shift") and 1.7 or 1
  local dir = e:forward() * f + e:right() * r
  if dir:length() > 0 then dir = dir:normalize() end
  local jump = (lumen.input.pressed("space") and e:grounded()) and self.jump or 0
  e:move(dir, self.speed * run, jump)
end

return M
