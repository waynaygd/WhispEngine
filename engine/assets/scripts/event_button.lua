-- Attach to an Entity with Allow gameplay input enabled. Assign a cabinet door
-- to target. This uses the existing Lua event queue, not an inspector-only path.
return {
  fields = { target = { type = "entity" } },
  on_start = function(self) Input.bind("ToggleDoor", "E") end,
  on_update = function(self)
    if self.target and Input.was_pressed("ToggleDoor") then
      World.send_event(self.target, "toggle", 1)
    end
  end
}
