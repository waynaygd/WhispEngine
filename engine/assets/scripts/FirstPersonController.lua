-- Behaviour belongs to this Entity. Physics and camera interpolation stay in C++.
return {
    fields = {
        move_speed = {type="number", default=4, min=0, max=30, label="Move Speed"},
        jump_speed = {type="number", default=4.5, min=0, max=20, label="Jump Speed"},
        sensitivity = {type="number", default=0.0025, min=0.0001, max=0.02, label="Mouse Sensitivity"},
        interact_distance = {type="number", default=3.0, min=0.5, max=10.0, label="Interact Distance"}
    },
    on_start = function(self)
        Input.bind("MoveForward", "W"); Input.bind("MoveBackward", "S")
        Input.bind("MoveLeft", "A"); Input.bind("MoveRight", "D")
        Input.bind("Jump", "Space"); Input.bind("CaptureCursor", "MouseLeft")
        Input.bind("ReleaseCursor", "Tab"); Input.bind("Interact", "E")
        Character.set_move_speed(self.entity, self.move_speed)
        Character.set_jump_speed(self.entity, self.jump_speed)
        if Camera.acquire() then Camera.follow(self.entity, Character.eye_offset(self.entity)) end
    end,
    on_update = function(self, dt)
        if not Camera.acquire() then Character.stop(self.entity); return end
        Camera.follow(self.entity, Character.eye_offset(self.entity))
        if Input.was_pressed("ReleaseCursor") then
            Input.capture_cursor(false); Character.stop(self.entity); return
        end
        if Input.was_pressed("CaptureCursor") then Input.capture_cursor(true) end
        if not Input.cursor_captured() then Character.stop(self.entity); return end
        local dx, dy = Input.mouse_delta()
        Camera.rotate(dx * self.sensitivity, -dy * self.sensitivity)
        local forward, right = Camera.flat_forward(), Camera.right()
        local x, z = 0, 0
        if Input.is_down("MoveForward") then z = z + 1 end
        if Input.is_down("MoveBackward") then z = z - 1 end
        if Input.is_down("MoveRight") then x = x + 1 end
        if Input.is_down("MoveLeft") then x = x - 1 end
        Character.move_world(self.entity, Vec3.new(forward.x*z + right.x*x, 0, forward.z*z + right.z*x))
        if Input.was_pressed("Jump") then Character.jump(self.entity) end
        if Input.was_pressed("Interact") then
            Log.info("E pressed")

            local hit = Physics.raycast(
                Camera.position(),
                Camera.forward(),
                self.interact_distance,
                self.entity
            )

            if hit then
                Log.info("Raycast hit")
                World.send_event(hit.entity, "interact", 0)
            else
                Log.info("Raycast missed")
            end
        end
    end,
    on_stop = function(self)
        if World.is_alive(self.entity) then Character.stop(self.entity) end
        Input.capture_cursor(false); Camera.release()
    end
}
