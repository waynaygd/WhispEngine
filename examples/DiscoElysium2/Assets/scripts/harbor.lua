local Dialogue=Script.require('dialogue')
local Walk=Script.require('walking')
local View=Script.require('conversation_view')
local function say(self,text) self.message=text;self.messageTime=3 end
local function panels(self) local value=ui.measure_text(self.state.quest,18,338);self.hud:set_text('Quest',value) end
local function path(self,point)
 local p=World.get_position(self.entity)
 return Navigation.find_path(Vec3.new(p.x,0,p.z),Vec3.new(point.x,0,point.z),Vec3.new(-8.5,0,-7.5),Vec3.new(8.5,0,7.5),.35,.36,self.entity)
end
local function approach(self,target)
 local q=World.get_position(target.entity);local best=nil
 for i=0,15 do
  local a=i*math.pi/8;local route=path(self,Vec3.new(q.x+math.cos(a)*1.3,0,q.z+math.sin(a)*1.3))
  if #route>0 and (not best or #route<#best) then best=route end
 end
 return best
end
local function visible(self,target)
 local p,q=World.get_position(self.entity),World.get_position(target.entity);p=Vec3.new(p.x,p.y+.6,p.z);local d=Vec3.new(q.x-p.x,q.y-p.y,q.z-p.z)
 local n=math.sqrt(d.x*d.x+d.y*d.y+d.z*d.z);local hit=Physics.raycast(p,d,n+.05,self.entity)
 return hit and hit.entity==target.entity
end
local function interact(self,target)
 Character.stop(self.entity);Walk.cancel(self);self.vx,self.vz,self.speed=0,0,0
 self.state.feedback='';self.dialogue=Dialogue.prepare(Data.load(target.definition.dialogue));View.show(self);panels(self)
end
local function select(self,point,target)
 local route=target and approach(self,target) or path(self,point)
 if not route or #route==0 then Walk.cancel(self);self.markerTime=0;self.destination=nil;say(self,'Проход перекрыт. Выберите свободное место.');self.blockedTime=1;return end
 self.route=Walk.route(route);self.step=1;self.target=target;self.destination=self.route[#self.route];self.markerTime=1
 if target then say(self,'Подойти: '..target.definition.name) end
end
local function hover(self,dt)
 local ray=Camera.pointer_ray(self.camera);self.hoverPoint=nil;self.hoverTarget=nil
 if ray then
  local hit=Physics.raycast(ray.origin,ray.direction,150,self.entity)
  if hit then
   for _,t in ipairs(self.targets) do if t.entity==hit.entity then self.hoverTarget=t end end
   if math.abs(ray.direction.y)>.0001 then
    local distance=-ray.origin.y/ray.direction.y
    if distance>0 then self.hoverPoint=Vec3.new(ray.origin.x+ray.direction.x*distance,0,ray.origin.z+ray.direction.z*distance) end
   end
   if not self.hoverPoint then self.hoverBlocked=true elseif self.hoverTarget then self.hoverBlocked=false end;self.hoverProbe=(self.hoverProbe or 0)-dt
   if self.hoverPoint and not self.hoverTarget and self.hoverProbe<=0 then self.hoverBlocked=#path(self,self.hoverPoint)==0;self.hoverProbe=.18 end
  end
 end
 if self.highlight~=self.hoverTarget then
  if self.highlight then World.set_tint(self.highlight.entity,self.highlight.tint) end
  self.highlight=self.hoverTarget
  if self.highlight then local c=self.highlight.tint;World.set_tint(self.highlight.entity,{math.min(1,c[1]+.22),math.min(1,c[2]+.22),math.min(1,c[3]+.12),c[4]}) end
 end
 local cursor='arrow';local hint=''
 if self.hoverTarget then
  local inspect=self.hoverTarget.definition.id=='receipt' or self.hoverTarget.definition.id=='ledger'
  cursor=inspect and 'crosshair' or 'hand';hint=(inspect and 'Осмотреть • ' or 'Поговорить • ')..self.hoverTarget.definition.name
 elseif self.hoverPoint then cursor=self.hoverBlocked and 'not_allowed' or 'arrow' end
 if self.blockedTime>0 then cursor='not_allowed' end
 Input.set_cursor(cursor);local status=ui.measure_text(self.messageTime>0 and self.message or hint,17,650);self.hud:set_text('Status',status)
end
return {
 fields={camera={type='entity',label='Camera'},walk_speed={type='number',default=3.4,min=.5,max=7,label='Walk speed'},
  acceleration={type='number',default=7,min=1,max=20},braking={type='number',default=9,min=1,max=25},
  turn_response={type='number',default=12,min=2,max=25},camera_response={type='number',default=5,min=1,max=15},
  camera_offset={type='vec3',default={-15,15,-15},label='Camera composition offset'},
  camera_min={type='vec3',default={-23,8,-27},label='Camera bounds minimum'},camera_max={type='vec3',default={-3,24,-7},label='Camera bounds maximum'}},
 on_start=function(self)
  assert(self.camera_min.x<=self.camera_max.x and self.camera_min.y<=self.camera_max.y and self.camera_min.z<=self.camera_max.z,'Invalid camera bounds');self.hud=ui.get(self.entity);self.state={flags={},quest=Data.load('data/quest.data.json').description,feedback=''}
  self.route={};self.step=1;self.zoom=20;self.zoomTarget=20;self.vx,self.vz,self.speed,self.yaw=0,0,0,0
  self.transcript={};self.responses={};self.targets={};self.journal=false;self.skills=false
  self.message='';self.messageTime=0;self.blockedTime=0;self.markerTime=0;self.detective=Data.load('data/stats.data.json')
  for _,key in ipairs(Data.load('data/interactables.data.json').definitions) do
   local d=Data.load(key);local entities=World.find_by_tag(d.tag);assert(entities[1],'Missing '..d.tag)
   self.targets[#self.targets+1]={entity=entities[1],definition=d,tint=World.get_tint(entities[1])}
  end
  local skills='';for _,s in ipairs(Data.rows('data/skills.table.json')) do skills=skills..s.name..'  '..s.value..'\n' end
  self.hud:set_text('SkillsText',skills)
  for action,key in pairs({HarborClick='MouseLeft',HarborJournal='J',HarborSkills='C',HarborZoomIn='Z',HarborZoomOut='X'}) do Input.bind(action,key) end
  Camera.set_active(self.camera);Character.set_move_speed(self.entity,self.walk_speed);ui.set_input_mode('GameAndUI');panels(self)
 end,
 on_update=function(self,dt)
  if not self.hud:valid() then self.hud=ui.get(self.entity);if self.dialogue then View.rebuild(self);self.hud:set_visible('Dialogue',true) end;panels(self) end
  self.messageTime=math.max(0,self.messageTime-dt);self.blockedTime=math.max(0,self.blockedTime-dt)
  if Input.was_pressed('HarborJournal') then self.journal=not self.journal;self.hud:set_visible('Journal',self.journal) end
  if Input.was_pressed('HarborSkills') then self.skills=not self.skills;self.hud:set_visible('Skills',self.skills) end
  if Input.was_pressed('HarborZoomIn') then self.zoomTarget=math.max(15,self.zoomTarget-2) end
  if Input.was_pressed('HarborZoomOut') then self.zoomTarget=math.min(29,self.zoomTarget+2) end
  self.zoomTarget=math.max(15,math.min(29,self.zoomTarget-Input.scroll_delta()*1.3))
  self.zoom=self.zoom+(self.zoomTarget-self.zoom)*(1-math.exp(-8*dt));Camera.set_orthographic_size(self.camera,self.zoom)
  hover(self,dt)
  if Input.was_pressed('HarborClick') and self.hoverPoint then
   if self.dialogue then View.close(self) end
   local p=World.get_position(self.entity);local target=self.hoverTarget
   if target and Walk.distance(p,World.get_position(target.entity))<=target.definition.range and self.speed<.1 and visible(self,target) then interact(self,target)
   else select(self,self.hoverPoint,target) end
  end
  local p,c=World.get_position(self.entity),World.get_position(self.camera);local extra=self.dialogue and 4 or 0
  local x=math.max(self.camera_min.x,math.min(self.camera_max.x,p.x+self.camera_offset.x+extra));local z=math.max(self.camera_min.z,math.min(self.camera_max.z,p.z+self.camera_offset.z-extra))
  local blend=1-math.exp(-self.camera_response*dt)
  World.set_position(self.camera,Vec3.new(c.x+(x-c.x)*blend,c.y+(math.max(self.camera_min.y,math.min(self.camera_max.y,self.camera_offset.y))-c.y)*blend,c.z+(z-c.z)*blend))
  self.markerTime=math.max(0,self.markerTime-dt);self.hud:set_opacity('Destination',self.markerTime)
  if self.markerTime>0 and self.destination then
   local w,h=ui.viewport_size();local scale=math.min(w/1280,h/800);local s=Camera.world_to_screen(self.camera,self.destination,w/h)
   self.hud:set_position('Destination',s.x*w/scale-13,s.y*h/scale-9);self.hud:set_opacity('Destination',self.markerTime)
  end
 end,
 on_fixed_update=function(self,dt)
  Walk.tick(self,dt)
  if self.target and self.speed<.25 then
   local p,q=World.get_position(self.entity),World.get_position(self.target.entity)
   if Walk.distance(p,q)<=self.target.definition.range and visible(self,self.target) then interact(self,self.target) end
  end
 end,
 on_ui_event=function(self,event)
  if event.type~='click' then return end
  if event.widget=='JournalToggle' then self.journal=not self.journal;self.hud:set_visible('Journal',self.journal);return end
  if event.widget=='SkillsToggle' then self.skills=not self.skills;self.hud:set_visible('Skills',self.skills);return end
  if event.widget=='CloseDialogue' then View.close(self);return end
  if not self.dialogue then return end
  local index=tonumber(event.widget:match('^Choice(%d+)$'));local response=index and self.responses[index]
  if response then
   View.history(self,self.detective.name,response.text,true)
   if Dialogue.choose(self.dialogue,response,self.state) then View.show(self) else View.close(self);say(self,self.state.quest) end;panels(self)
  end
 end,
 on_stop=function(self) Character.stop(self.entity);if self.highlight and World.is_alive(self.highlight.entity) then World.set_tint(self.highlight.entity,self.highlight.tint) end end
}
