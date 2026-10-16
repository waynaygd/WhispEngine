return {
    fields = {
        globe = { type = "entity", label = "Globe" },
        spotlight = { type = "entity", label = "Spotlight" },
        rotation_speed = { type = "number", default = 0.5, min = 0, max = 10 },
        pulse_speed = { type = "number", default = 2.0, min = 0, max = 20 },
        artist = { type = "entity", label = "Concert Artist" },

        artist_rotation_speed = {
            type = "number",
            default = 0.3,
            min = -5,
            max = 5,
            label = "Artist Rotation Speed"
        },

        light_base = {
            type = "number",
            default = 100,
            min = 0,
            max = 10000,
            label = "Light Base Intensity"
        },

        light_amplitude = {
            type = "number",
            default = 80,
            min = 0,
            max = 10000,
            label = "Light Pulse Amplitude"
        }
    },
    on_start = function(self)
        self.active = false
        self.time = 0
    end,

    on_update = function(self, dt)

        if not self.active then
            return
        end

        self.time = self.time + dt
        if self.globe and World.is_alive(self.globe) then
            local r = World.get_rotation(self.globe)
            World.set_rotation(self.globe, Vec3.new(r.x, r.y + self.rotation_speed * dt, r.z))
        end
        if self.spotlight and World.is_alive(self.spotlight) then
            local intensity =
                self.light_base +
                math.sin(self.time * self.pulse_speed) * self.light_amplitude

            Light.set_intensity(
                self.spotlight,
                math.max(0, intensity)
            )
        end

        if self.artist and World.is_alive(self.artist) then
            local r = World.get_rotation(self.artist)

            World.set_rotation(
                self.artist,
                Vec3.new(r.x, r.y + self.artist_rotation_speed * dt, r.z)
            )
        end
    end,

    on_event = function(self, event)
        if event.type == "start_concert" then
            self.active = true
            self.time = 0

            Log.info("Concert started!")
        end
    end
}
