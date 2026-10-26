-- lumen.gui: immediate mode widgets on top of the native `lumen.ui` drawing primitives.
--
--   local gui = require "lumen.gui"
--   function game.update(dt)
--     gui.begin_frame()
--     local sw, sh = gui.screen()
--     gui.panel(20, 20, 260, 90)
--     gui.label(36, 32, "Health", { size = 16 })
--     gui.bar(36, 58, 228, 18, hp / max_hp, { fg = "#d9534f" })
--     if gui.button("start", sw / 2 - 90, sh / 2, 180, 40, "Start game") then lumen.scene.load("scenes/level1.scene") end
--   end
--
-- Widgets are identified by the `id` string (stable between frames); colors are {r,g,b[,a]} (0..1) or "#rrggbb[aa]".
local M = {}

M.theme = {
  panel = "#14171dE6", panel_border = "#2b303b", text = "#e6e9ef", text_dim = "#8f97a6",
  accent = "#f59e40", button = "#252a34", button_hover = "#323947", button_down = "#1c2028",
  bar_bg = "#0c0e12CC", rounding = 6, font_size = 18,
}

local state = { mx = 0, my = 0, down = false, pressed = false, released = false, was_down = false, active = nil, hot = nil, slider = {} }

local function ui() return lumen.ui end

function M.screen() return ui().screen() end

-- Call once per frame before any widget: samples the mouse.
function M.begin_frame()
  if state.released then state.active = nil end -- the release of the previous frame has been consumed by now
  local x, y, down, pressed = ui().mouse()
  state.mx, state.my = x, y
  state.pressed = pressed
  state.released = state.was_down and not down
  state.down = down
  state.was_down = down
  state.hot = nil
end

function M.mouse() return state.mx, state.my, state.down end
function M.hovered(x, y, w, h) return state.mx >= x and state.mx < x + w and state.my >= y and state.my < y + h end

-- Position of a w*h box anchored to a screen edge/corner with a margin: "top_left", "top", "top_right", "left", "center", "right",
-- "bottom_left", "bottom", "bottom_right".
function M.anchor(where, w, h, margin)
  local sw, sh = M.screen()
  margin = margin or 16
  local x, y = margin, margin
  if where:find("right") then x = sw - w - margin elseif where == "top" or where == "bottom" or where == "center" then x = (sw - w) / 2 end
  if where:find("bottom") then y = sh - h - margin elseif where == "left" or where == "right" or where == "center" then y = (sh - h) / 2 end
  return x, y
end

function M.panel(x, y, w, h, opts)
  opts = opts or {}
  ui().rect(x, y, w, h, opts.color or M.theme.panel, { rounding = opts.rounding or M.theme.rounding })
  if opts.border ~= false then ui().rect(x, y, w, h, opts.border_color or M.theme.panel_border, { rounding = opts.rounding or M.theme.rounding, outline = 1 }) end
end

function M.label(x, y, text, opts)
  opts = opts or {}
  ui().text(x, y, text, { size = opts.size or M.theme.font_size, color = opts.color or M.theme.text, align = opts.align, shadow = opts.shadow })
end

-- Horizontal progress bar, value 0..1.
function M.bar(x, y, w, h, value, opts)
  opts = opts or {}
  value = math.max(0, math.min(1, value or 0))
  local r = opts.rounding or math.min(h / 2, M.theme.rounding)
  ui().rect(x, y, w, h, opts.bg or M.theme.bar_bg, { rounding = r })
  if value > 0 then ui().rect(x, y, math.max(h * 0.5, w * value), h, opts.fg or M.theme.accent, { rounding = r }) end
end

-- Returns true on the frame the button was released over itself.
function M.button(id, x, y, w, h, text, opts)
  opts = opts or {}
  local over = M.hovered(x, y, w, h)
  if over then state.hot = id end
  if over and state.pressed then state.active = id end
  local clicked = state.released and state.active == id and over
  local c = M.theme.button
  if over then c = (state.active == id and state.down) and M.theme.button_down or M.theme.button_hover end
  ui().rect(x, y, w, h, opts.color or c, { rounding = opts.rounding or M.theme.rounding })
  if over then ui().rect(x, y, w, h, M.theme.accent, { rounding = opts.rounding or M.theme.rounding, outline = 1 }) end
  if text then
    local size = opts.size or M.theme.font_size
    ui().text(x + w / 2, y + (h - size) / 2 - 1, text, { size = size, align = "center", color = opts.text_color or M.theme.text })
  end
  if clicked then state.active = nil end
  return clicked
end

-- Checkbox style toggle; returns the new value.
function M.toggle(id, x, y, label, value, opts)
  local size = (opts and opts.size) or M.theme.font_size
  local box = size + 4
  local w = box + 8 + size * #label * 0.55
  if M.hovered(x, y, w, box) and state.pressed then value = not value end
  ui().rect(x, y, box, box, M.theme.button, { rounding = 4 })
  ui().rect(x, y, box, box, M.theme.panel_border, { rounding = 4, outline = 1 })
  if value then ui().rect(x + 4, y + 4, box - 8, box - 8, M.theme.accent, { rounding = 2 }) end
  ui().text(x + box + 8, y + 1, label, { size = size, color = M.theme.text })
  return value
end

-- Horizontal slider; returns the (possibly changed) value.
function M.slider(id, x, y, w, h, value, min, max)
  local t = (value - min) / (max - min)
  local over = M.hovered(x, y, w, h)
  if over and state.pressed then state.slider[id] = true end
  if not state.down then state.slider[id] = nil end
  if state.slider[id] then
    t = math.max(0, math.min(1, (state.mx - x) / w))
    value = min + t * (max - min)
  end
  local ty = y + h / 2
  ui().rect(x, ty - 2, w, 4, M.theme.bar_bg, { rounding = 2 })
  ui().rect(x, ty - 2, w * t, 4, M.theme.accent, { rounding = 2 })
  ui().rect(x + w * t - 6, ty - 8, 12, 16, (over or state.slider[id]) and M.theme.text or M.theme.text_dim, { rounding = 4 })
  return value
end

return M
