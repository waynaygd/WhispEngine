return {
    fields = {
        globe = { type = "entity", label = "Globe" },
        spotlight = { type = "entity", label = "Spotlight" },
        rotation_speed = { type = "number", default = 0.5, min = 0, max = 10 },
        pulse_speed = { type = "number", default = 2.0, min = 0, max = 20 }
    },
    on_start = function(self)
        self.time = 0
    end,
    on_update = function(self, dt)
        self.time = self.time + dt
        if self.globe and World.is_alive(self.globe) then
            local r = World.get_rotation(self.globe)
            World.set_rotation(self.globe, Vec3.new(r.x, r.y + self.rotation_speed * dt, r.z))
        end
        if self.spotlight and World.is_alive(self.spotlight) then
            Light.set_intensity(self.spotlight, 5 + math.sin(self.time * self.pulse_speed) * 3)
        end
    end
}
