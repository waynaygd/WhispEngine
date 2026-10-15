return {
    fields = {
        wave_size = {type="integer", default=5, min=1, max=30, label="Wave Size"},
        wave_count = {type="integer", default=3, min=1, max=20, label="Waves"},
        spawn_interval = {type="number", default=0.8, min=0.1, max=10, label="Spawn Interval"},
        lives = {type="integer", default=10, min=1, max=100, label="Core Lives"},
        attack_radius = {type="number", default=8, min=1, max=30, label="Attack Radius"},
        enemy_prefab = {type="asset", default="prefabs/wave_enemy.prefab.json", label="Enemy Prefab"}
    },
    on_start = function(self)
        self.wave=1; self.spawned=0; self.timer=0; self.finished=false
        Log.info("Wave Survival: F attacks nearest enemy. Lives: "..self.lives)
    end,
    on_event = function(self,event)
        if event.type=="damage" and not self.finished then
            self.lives=self.lives-event.amount
            Log.warning("Core lives: "..self.lives)
        end
    end,
    on_update = function(self,dt)
        if self.finished then return end
        local targets=World.find_by_tag("Core")
        local core=targets[1]
        if not core then return end
        if self.lives<=0 then
            self.finished=true
            World.set_position(core,Vec3.new(0,-2,0))
            Log.error("DEFEAT: Core destroyed")
            for _,enemy in ipairs(World.find_by_tag("Enemy")) do World.destroy(enemy) end
            return
        end
        self.timer=self.timer+dt
        if self.spawned<self.wave_size and self.timer>=self.spawn_interval then
            self.timer=0; self.spawned=self.spawned+1
            local angle=self.spawned*2.399+self.wave
            World.spawn_prefab(self.enemy_prefab,Vec3.new(math.cos(angle)*7,0,math.sin(angle)*7))
            Log.info("Wave "..self.wave..": spawned "..self.spawned.."/"..self.wave_size)
        end
        local enemies=World.find_by_tag("Enemy")
        if Input.is_action_pressed("LuaAttack") then
            local p=World.get_position(core); local nearest=nil; local distance=self.attack_radius
            for _,enemy in ipairs(enemies) do
                local q=World.get_position(enemy)
                local d=math.sqrt((q.x-p.x)^2+(q.z-p.z)^2)
                if d<distance then nearest=enemy; distance=d end
            end
            if nearest then World.destroy(nearest); Log.info("Attack: enemy destroyed") end
        end
        if self.spawned==self.wave_size and #World.find_by_tag("Enemy")==0 then
            if self.wave==self.wave_count then
                self.finished=true; Log.info("VICTORY: all waves cleared")
                World.set_position(core,Vec3.new(0,1.5,0))
            else
                self.wave=self.wave+1; self.spawned=0; self.timer=-1
                Log.info("Starting wave "..self.wave)
            end
        end
    end,
    on_destroy = function(self) Log.info("Wave Survival stopped") end
}
