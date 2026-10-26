return {
  fields = {
    normalIntensity = { type = "number", default = 100, min = 0, max = 10000 },
    emergencyIntensity = { type = "number", default = 0, min = 0, max = 10000 }
  },
  on_start = function(self) Light.set_intensity(self.entity, self.normalIntensity) end,
  on_event = function(self, event)
    if event.type == "emergency_mode" then
      Light.set_intensity(self.entity, event.amount > 0 and self.emergencyIntensity or self.normalIntensity)
    end
  end
}
