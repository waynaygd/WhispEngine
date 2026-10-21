return {
  fields = {
    normalLight = { type = "entity" }, emergencyLight = { type = "entity" },
    openAngle = { type = "number", default = -1.75, min = -3.14, max = 3.14 },
    speed = { type = "number", default = 2.4, min = 0.1, max = 10 }
  },
  on_start = function(self)
    self.closed = World.get_local_rotation(self.entity)
    self.angle, self.target = 0, 0
  end,
  on_event = function(self, event)
    if event.type ~= "set_open" then return end
    local open = event.amount > 0
    self.target = open and self.openAngle or 0
    -- These messages drive actual lighting, rather than just diagnostic labels.
    if self.normalLight then World.send_event(self.normalLight, "emergency_mode", open and 1 or 0) end
    if self.emergencyLight then World.send_event(self.emergencyLight, "emergency_mode", open and 1 or 0) end
  end,
  on_update = function(self, dt)
    local step = self.speed * dt
    if self.angle < self.target then self.angle = math.min(self.target, self.angle + step)
    else self.angle = math.max(self.target, self.angle - step) end
    World.set_local_rotation(self.entity, Vec3.new(self.closed.x, self.closed.y + self.angle, self.closed.z))
  end
}
