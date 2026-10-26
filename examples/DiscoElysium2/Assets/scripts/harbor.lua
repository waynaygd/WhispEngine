local Dialogue = Script.require('dialogue')
local function distance(a,b)
    local x,z=a.x-b.x,a.z-b.z
    return math.sqrt(x*x+z*z)
end
local function say(self, text) self.hud:set_text('Status',text) end
local function panels(self)
    self.hud:set_text('Quest', Dialogue.wrap(self.state.quest,30))
    self.hud:set_text('History', Dialogue.wrap(self.history or '',34))
end
local function close(self)
    self.dialogue=nil
    self.responses={}
    self.hud:set_visible('Dialogue',false)
    panels(self)
end
local function show(self)
    local n=self.dialogue.nodes[self.dialogue.current]
    self.hud:set_visible('Dialogue',true)
    self.hud:set_text('Speaker',n.speaker)
    self.hud:set_image('Portrait',n.portrait)
    self.hud:set_text('Line',Dialogue.wrap(n.text,64))
    self.hud:set_text('Feedback',self.state.feedback)
    local cut=utf8.offset(n.text,28)
    local excerpt=cut and string.sub(n.text,1,cut-1)..'…' or n.text
    self.historyLines[#self.historyLines+1]=n.speaker..': '..excerpt
    if #self.historyLines>3 then table.remove(self.historyLines,1) end
    self.history=table.concat(self.historyLines,'\n\n')
    self.responses=Dialogue.choices(self.dialogue,self.state.flags)
    for i=1,4 do
        local c=self.responses[i]
        self.hud:set_visible('Choice'..i,c~=nil)
        if c then self.hud:set_text('ChoiceText'..i,c.text) end
    end
    panels(self)
end
local function interact(self,target)
    Character.stop(self.entity)
    self.route={}
    self.target=nil
    self.state.feedback=''
    self.dialogue=Dialogue.prepare(Data.load(target.definition.dialogue))
    show(self)
end
local function navigate(self,point,target)
    local p=World.get_position(self.entity)
    local start=Vec3.new(p.x,0,p.z)
    local goal=Vec3.new(point.x,0,point.z)
    local route=Navigation.find_path(start,goal,Vec3.new(-8.5,0,-7.5),Vec3.new(8.5,0,7.5),.35,.31,self.entity)
    if #route==0 then say(self,'Туда нельзя пройти. Выберите свободное место рядом.');return end
    self.route=route;self.step=1;self.target=target
    say(self,target and ('Подойти: '..target.definition.name) or 'Идём к отмеченной точке.')
end
return {
    fields={camera={type='entity',label='Isometric Camera'}},
    on_start=function(self)
        self.hud=ui.get(self.entity)
        self.state={flags={},quest=Data.load('data/quest.data.json').description,feedback=''}
        self.route={};self.step=1;self.zoom=22;self.journal=false;self.skills=false
        self.history='';self.historyLines={};self.responses={};self.targets={}
        self.detective=Data.load('data/stats.data.json')
        for _,key in ipairs(Data.load('data/interactables.data.json').definitions) do
            local d=Data.load(key)
            local entities=World.find_by_tag(d.tag)
            assert(entities[1],'Missing interactable: '..d.tag)
            self.targets[#self.targets+1]={entity=entities[1],definition=d}
        end
        local skills=''
        for _,s in ipairs(Data.rows('data/skills.table.json')) do skills=skills..s.name..'  '..s.value..'\n' end
        self.hud:set_text('SkillsText',skills)
        Input.bind('HarborClick','MouseLeft')
        Input.bind('HarborJournal','J');Input.bind('HarborSkills','C')
        Input.bind('HarborZoomIn','Z');Input.bind('HarborZoomOut','X')
        Camera.set_active(self.camera)
        ui.set_input_mode('GameAndUI')
        panels(self)
    end,
    on_update=function(self,dt)
        if Input.was_pressed('HarborJournal') then self.journal=not self.journal;self.hud:set_visible('Journal',self.journal) end
        if Input.was_pressed('HarborSkills') then self.skills=not self.skills;self.hud:set_visible('Skills',self.skills) end
        if Input.was_pressed('HarborZoomIn') then self.zoom=math.max(14,self.zoom-2) end
        if Input.was_pressed('HarborZoomOut') then self.zoom=math.min(32,self.zoom+2) end
        Camera.set_orthographic_size(self.camera,self.zoom)
        if not self.dialogue and Input.was_pressed('HarborClick') then
            local ray=Camera.pointer_ray(self.camera)
            if ray then
                local hit=Physics.raycast(ray.origin,ray.direction,150,self.entity)
                if hit then
                    local target=nil
                    for _,t in ipairs(self.targets) do if t.entity==hit.entity then target=t end end
                    local p=World.get_position(self.entity)
                    if target then
                        local q=World.get_position(target.entity)
                        if distance(p,q)<=target.definition.range then interact(self,target)
                        else
                            -- A reachable approach point around all sides, never inside the NPC collider.
                            local best=nil;local score=1e9
                            for i=0,7 do
                                local a=i*math.pi/4
                                local dest=Vec3.new(q.x+math.cos(a)*1.35,0,q.z+math.sin(a)*1.35)
                                local path=Navigation.find_path(Vec3.new(p.x,0,p.z),dest,Vec3.new(-8.5,0,-7.5),Vec3.new(8.5,0,7.5),.35,.31,self.entity)
                                if #path>0 and #path<score then best=dest;score=#path end
                            end
                            if best then navigate(self,best,target) else say(self,'Подход перекрыт. Попробуйте с другой стороны.') end
                        end
                    else
                        -- Project every noninteractive hit onto the common walk plane.
                        if math.abs(ray.direction.y)>.0001 then
                            local t=-ray.origin.y/ray.direction.y
                            if t>0 then navigate(self,Vec3.new(ray.origin.x+ray.direction.x*t,0,ray.origin.z+ray.direction.z*t),nil) end
                        end
                    end
                end
            end
        end
        local p=World.get_position(self.entity)
        -- Frame-independent follow; camera optical direction remains fixed.
        local c=World.get_position(self.camera)
        local blend=1-math.exp(-5*dt)
        World.set_position(self.camera,Vec3.new(c.x+(p.x-13-c.x)*blend,15,c.z+(p.z-17-c.z)*blend))
    end,
    on_fixed_update=function(self,dt)
        if self.dialogue then Character.stop(self.entity);return end
        local p=World.get_position(self.entity)
        if self.target and distance(p,World.get_position(self.target.entity))<=self.target.definition.range then interact(self,self.target);return end
        local dest=self.route[self.step]
        while dest and distance(p,dest)<.12 do
            self.step=self.step+1
            dest=self.route[self.step]
        end
        if not dest then Character.stop(self.entity);return end
        local dx,dz=dest.x-p.x,dest.z-p.z
        local n=math.sqrt(dx*dx+dz*dz)
        local speed=self.step==#self.route and math.min(1,n/(3.4*math.max(dt,.001))) or 1
        Character.move_world(self.entity,Vec3.new(dx/n*speed,0,dz/n*speed))
    end,
    on_ui_event=function(self,event)
        if event.type~='click' then return end
        if event.widget=='JournalToggle' then self.journal=not self.journal;self.hud:set_visible('Journal',self.journal);return end
        if event.widget=='SkillsToggle' then self.skills=not self.skills;self.hud:set_visible('Skills',self.skills);return end
        if event.widget=='CloseDialogue' then close(self);return end
        if not self.dialogue then return end
        for i=1,4 do if event.widget=='Choice'..i then
            local keep=Dialogue.choose(self.dialogue,self.responses[i],self.state)
            if keep then show(self) else close(self);say(self,self.state.quest) end
            return
        end end
    end,
    on_stop=function(self) Character.stop(self.entity) end
}
