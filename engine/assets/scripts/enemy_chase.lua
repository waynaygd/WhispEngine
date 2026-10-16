return {
    fields = {
        speed = {type="number", default=1.0, min=0.1, max=20, label="Speed"},
        damage = {type="integer", default=1, min=1, max=100, label="Damage"},
        hit_radius = {type="number", default=0.65, min=0.1, max=3, label="Hit Radius"}
    },
    on_start = function(self) self.age=0 end,
    on_update = function(self,dt)
        self.age=self.age+dt
        local core=World.find_by_tag("Core")[1]
        if not core or not World.is_alive(core) then return end
        local p=World.get_position(self.entity); local goal=World.get_position(core)
        local x=goal.x-p.x; local z=goal.z-p.z
        local distance=math.sqrt(x*x+z*z)
        if distance<=self.hit_radius then
            local controller=World.find_by_tag("WaveController")[1]
            if controller then World.send_event(controller,"damage",self.damage) end
            World.destroy(self.entity)
        else
            local step=math.min(self.speed*dt,distance)
            World.set_position(self.entity,Vec3.new(p.x+x/distance*step,p.y,p.z+z/distance*step))
        end
    end,
    on_event = function(self,event)
        if event.type=="damage" then World.destroy(self.entity) end
    end,
    on_destroy = function(self) end
}
