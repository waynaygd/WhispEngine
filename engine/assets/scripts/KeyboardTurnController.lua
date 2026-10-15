-- Alternative: WASD relative to the camera, Q/E turn, no mouse look/capture.
return {
    fields = {
        move_speed = {type="number", default=4, min=0, max=30},
        jump_speed = {type="number", default=4.5, min=0, max=20},
        turn_speed = {type="number", default=1.8, min=0, max=10}
    },
    on_start = function(self)
        Input.bind("MoveForward", "W"); Input.bind("MoveBackward", "S")
        Input.bind("MoveLeft", "A"); Input.bind("MoveRight", "D")
        Input.bind("TurnLeft", "Q"); Input.bind("TurnRight", "E"); Input.bind("Jump", "Space")
        Character.set_move_speed(self.entity, self.move_speed)
        Character.set_jump_speed(self.entity, self.jump_speed)
        if Camera.acquire() then Camera.follow(self.entity, Character.eye_offset(self.entity)) end
    end,
    on_update = function(self, dt)
        if not Camera.acquire() then Character.stop(self.entity); return end
        Camera.follow(self.entity, Character.eye_offset(self.entity))
        local turn = 0
        if Input.is_down("TurnLeft") then turn = turn - 1 end
        if Input.is_down("TurnRight") then turn = turn + 1 end
        Camera.rotate(turn * self.turn_speed * dt, 0)
        local forward, right = Camera.flat_forward(), Camera.right()
        local x, z = 0, 0
        if Input.is_down("MoveForward") then z = z + 1 end
        if Input.is_down("MoveBackward") then z = z - 1 end
        if Input.is_down("MoveRight") then x = x + 1 end
        if Input.is_down("MoveLeft") then x = x - 1 end
        Character.move_world(self.entity, Vec3.new(forward.x*z + right.x*x, 0, forward.z*z + right.z*x))
        if Input.was_pressed("Jump") then Character.jump(self.entity) end
    end,
    on_stop = function(self)
        if World.is_alive(self.entity) then Character.stop(self.entity) end
        Camera.release()
    end
}
