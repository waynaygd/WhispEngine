-- The existing FPS raycast sends "interact" to this collider.
return {
  fields = { door = { type = "entity" } },
  on_start = function(self) self.open = false end,
  on_event = function(self, event)
    if event.type ~= "interact" then return end
    self.open = not self.open
    if self.door and World.is_alive(self.door) then
      World.send_event(self.door, "set_open", self.open and 1 or 0)
    end
  end
}
