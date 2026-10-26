return {
  fields = {
    handle = { type = "entity" },
    openAngle = { type = "number", default = 1.2, min = 0, max = 3.14 }
  },
  on_start = function(self) self.open = false end,
  on_event = function(self, event)
    if event.type == "toggle" then
      self.open = not self.open
      local rotation = World.get_local_rotation(self.entity)
      rotation.y = self.open and self.openAngle or 0
      World.set_local_rotation(self.entity, rotation)
    end
  end
}
