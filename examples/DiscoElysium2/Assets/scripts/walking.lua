local M={}
function M.distance(a,b) return math.sqrt((a.x-b.x)^2+(a.z-b.z)^2) end
function M.route(points)
 local result={}
 for i,p in ipairs(points) do
  local a,b=points[i-1],points[i+1]
  if not a or not b or math.abs((p.x-a.x)*(b.z-p.z)-(p.z-a.z)*(b.x-p.x))>.001 then result[#result+1]=p end
 end
 return result
end
function M.cancel(self) self.route={};self.step=1;self.target=nil end
function M.tick(self,dt)
 local p=World.get_position(self.entity);local dest=self.route[self.step]
 while dest and M.distance(p,dest)<.13 do self.step=self.step+1;dest=self.route[self.step] end
 local tx,tz=0,0
 if dest and not self.dialogue then
  local dx,dz=dest.x-p.x,dest.z-p.z;local length=math.sqrt(dx*dx+dz*dz);local remaining=length
  for i=self.step+1,#self.route do remaining=remaining+M.distance(self.route[i-1],self.route[i]) end
  local speed=math.min(self.walk_speed,math.sqrt(2*self.braking*math.max(0,remaining-.06)))
  if self.route[self.step+1] then speed=math.min(speed,math.sqrt(2*self.braking*math.max(0,length-.12)+.75*.75)) end
  tx,tz=dx/length*speed,dz/length*speed
 end
 local dx,dz=tx-self.vx,tz-self.vz;local delta=math.sqrt(dx*dx+dz*dz);local step=(math.sqrt(tx*tx+tz*tz)<self.speed and self.braking or self.acceleration)*dt
 if delta>step then dx,dz=dx/delta*step,dz/delta*step end
 self.vx,self.vz=self.vx+dx,self.vz+dz;self.speed=math.sqrt(self.vx*self.vx+self.vz*self.vz)
 if self.speed<.025 and not dest then self.vx,self.vz,self.speed=0,0,0;Character.stop(self.entity)
 else
  Character.move_world(self.entity,Vec3.new(self.vx/self.walk_speed,0,self.vz/self.walk_speed))
  if self.speed>.08 then
   local yaw=math.atan(self.vx,self.vz);local difference=(yaw-self.yaw+math.pi)%(2*math.pi)-math.pi
   self.yaw=self.yaw+difference*(1-math.exp(-self.turn_response*dt));World.set_rotation(self.entity,Vec3.new(0,self.yaw,0))
  end
 end
end
return M
