-- ${PROJECT_NAME}: game entry script.
-- start() runs once after the startup scene is loaded, update(dt) every frame,
-- fixed_update(dt) at the physics rate and shutdown() when the game closes.
local game = {}

local score = 0

function game.start()
  lumen.log.info("${PROJECT_NAME} started (engine ${ENGINE_VERSION})")
  game.pond = lumen.scene.find("Pond")
  game.player = lumen.scene.find("Player")
  lumen.events.on("collect", function(n) score = score + (n or 1) end)
end

function game.update(dt)
  if lumen.input.pressed("escape") then
    lumen.input.set_cursor_locked(not lumen.input.cursor_locked())
  end
  -- Respawn when falling out of the world.
  if game.player and game.player:position().y < -30 then
    game.player:set_position(lumen.vec3(0, 3, 0))
    game.player:set_velocity(lumen.vec3(0, 0, 0))
  end
  lumen.debug.text(10, 10, string.format("fps %.0f   score %d", 1 / math.max(dt, 1e-5), score))
end

function game.fixed_update(dt) end
function game.shutdown() end

return game
