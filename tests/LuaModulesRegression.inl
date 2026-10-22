static void Modules(){
 auto dependencies=editor::detail::AnalyzeLuaDependencies("local M=Script.require('gameplay.counter')");Check(std::find(dependencies.values.begin(),dependencies.values.end(),"scripts/gameplay/counter.lua")!=dependencies.values.end(),"editor detects literal module dependency");
 Check(!editor::detail::AnalyzeLuaDependencies("local M=Script.require(name)").moduleDynamic.empty(),"editor flags dynamic import dependency");
 Write("scripts/gameplay/nested.lua","return {step=1}");
 const char* module="local N=Script.require('gameplay.nested'); local n=0; return {next=function() n=n+N.step; return n end}";
 Write("scripts/gameplay/counter.lua",module);
 const char* behaviour="local M=Script.require('gameplay.counter'); assert(M==Script.require('gameplay.counter'));return {on_update=function(self) World.set_position(self.entity,Vec3.new(M.next(),0,0)) end}";
 Write("scripts/modules.lua",behaviour);
 ecs::World w;auto a=Actor(w,"scripts/modules.lua"),b=Actor(w,"scripts/modules.lua");scripting::LuaRuntime lua;lua.Start(w);lua.Update(0);Check(X(w,a)==1&&X(w,b)==1,"module state per instance and repeated import identity");lua.Update(0);Check(X(w,a)==2&&X(w,b)==2,"module state persists independently");
 Write("scripts/modules_other.lua",behaviour);auto c=Actor(w,"scripts/modules_other.lua");lua.Update(0);
 auto firstRevision=lua.InspectionInfo(a).revision,secondRevision=lua.InspectionInfo(c).revision;
 Write("scripts/modules_other.lua","return {fields={x={type='number',default='bad'}}}");
 Write("scripts/gameplay/nested.lua","return {step=2}");Check(!lua.ReloadScript("scripts/gameplay/nested.lua"),"one bad root rejects entire dependent transaction");
 Check(lua.InspectionInfo(a).revision==firstRevision&&lua.InspectionInfo(c).revision==secondRevision,"no partial root revision commit");
 Write("scripts/modules_other.lua",behaviour);lua.Update(.6f);Check(X(w,a)==2&&X(w,c)==2,"repairing one failed root retries entire transaction group");
 Write("scripts/gameplay/nested.lua","return {step=1}");Check(lua.ReloadScript("scripts/gameplay/nested.lua"),"transaction recovers all roots");lua.Update(0);lua.Update(0);
 auto serial=lua.InspectionInfo(a).serial;
 Write("scripts/gameplay/nested.lua","return {broken=");Check(!lua.ReloadScript("scripts/gameplay/nested.lua"),"bad module rejects graph reload");lua.Update(0);Check(lua.InspectionInfo(a).serial==serial&&X(w,a)==3,"failed reload retains live instance and upvalues");
 Write("scripts/gameplay/nested.lua","return {step=4}");Check(lua.ReloadScript("scripts/gameplay/nested.lua"),"transitive module reload");lua.Update(0);Check(X(w,a)==4&&lua.InspectionInfo(a).serial!=serial,"successful dependency reload replaces instances");
 Write("scripts/gameplay/nested.lua","return {step=7}");lua.Update(.6f);Check(X(w,a)==7,"automatic dependency reload after save");
 Write("scripts/missing_root.lua","local M=Script.require('gameplay.created_later');return {on_update=function(self) World.set_position(self.entity,Vec3.new(M.value,0,0)) end}");
 auto waiting=Actor(w,"scripts/missing_root.lua");lua.Update(0);Check(lua.InspectionInfo(waiting).state==scripting::LuaInstanceState::Rejected,"missing module instance rejected safely");
 Write("scripts/gameplay/created_later.lua","return {value=33}");lua.Update(.6f);Check(X(w,waiting)==33,"creation of missing dependency triggers recovery");
 Write("scripts/gameplay/a.lua","return Script.require('gameplay.b')");Write("scripts/gameplay/b.lua","return Script.require('gameplay.a')");
 for(const auto& name:{"gameplay.a","absent","../escape","x..y","C:/outside","x/y"}){Write("scripts/badmodule.lua",std::string("local M=Script.require('")+name+"');return {}");Check(!lua.ReloadScript("scripts/badmodule.lua"),"unsafe missing or cyclic module rejected");}
 Check(lua.Error("scripts/badmodule.lua").find("load")!=std::string::npos,"module error diagnostic retained");
 Write("scripts/budgetmodule.lua","local M=Script.require('gameplay.counter');while true do end;return {}");Check(!lua.ReloadScript("scripts/budgetmodule.lua"),"nested evaluation preserves outer budget");
 Write("scripts/gameplay/unsafe.lua","assert(os==nil and package==nil and io==nil and debug==nil and load==nil and dofile==nil);return {}");Write("scripts/safemodule.lua","local M=Script.require('gameplay.unsafe');return {}");Check(lua.ReloadScript("scripts/safemodule.lua"),"module sandbox allowlist");
 Write("scripts/gameplay/fault.lua","error('module failure')");Write("scripts/fault.lua","local M=Script.require('gameplay.fault');return {}");Check(!lua.ReloadScript("scripts/fault.lua")&&lua.Error("scripts/fault.lua").find("gameplay/fault.lua")!=std::string::npos,"module error chain");lua.Stop();
 Write("scripts/gameplay/bounds.lua","return {maximum=10}");Write("scripts/schema_module.lua","local M=Script.require('gameplay.bounds');return {fields={speed={type='number',default=1,min=0,max=M.maximum}},on_update=function(self) World.set_position(self.entity,Vec3.new(self.speed,0,0)) end}");
 ecs::World schema;auto schemaActor=Actor(schema,"scripts/schema_module.lua");schema.GetComponent<ecs::LuaScriptComponent>(schemaActor)->properties["speed"]=5;lua.Start(schema);lua.Update(0);auto schemaSerial=lua.InspectionInfo(schemaActor).serial;
 Write("scripts/gameplay/bounds.lua","return {maximum=2}");Check(!lua.ReloadScript("scripts/gameplay/bounds.lua"),"reload protects existing serialized overrides");lua.Update(0);Check(X(schema,schemaActor)==5&&lua.InspectionInfo(schemaActor).serial==schemaSerial,"invalid override candidate leaves working instance intact");lua.Stop();
 Write("scripts/gameplay/large.lua",std::string(256*1024+1,' '));Write("scripts/large_root.lua","local M=Script.require('gameplay.large');return {}");Check(!lua.ReloadScript("scripts/large_root.lua")&&lua.Error("scripts/large_root.lua").find("256 KiB")!=std::string::npos,"module source limit enforced");
 for(int depth=0;depth<18;++depth)Write("scripts/gameplay/depth"+std::to_string(depth)+".lua",depth==17?"return {}":"return Script.require('gameplay.depth"+std::to_string(depth+1)+"')");
 Write("scripts/depth_root.lua","local M=Script.require('gameplay.depth0');return {}");Check(!lua.ReloadScript("scripts/depth_root.lua"),"deep acyclic graph bounded");lua.Stop();
 // A request and its reply remain queued; a dead sender is nil, never a reused slot.
 Write("scripts/request.lua","return {fields={target={type='entity'}},on_start=function(self) World.send_event(self.target,'request',25) end,on_event=function(self,e) assert(e.type=='accepted' and e.amount==25);World.set_position(self.entity,Vec3.new(9,0,0)) end}");
 Write("scripts/reply.lua","return {on_event=function(self,e) assert(e.type=='request' and e.amount==25 and not World.is_alive(e.other));if e.source then assert(World.is_alive(e.source));World.send_event(e.source,'accepted',e.amount) else World.set_position(self.entity,Vec3.new(8,0,0)) end end}");
 ecs::World events;auto target=Actor(events,"scripts/reply.lua"),source=Actor(events,"scripts/request.lua");events.AddComponent<ecs::SceneIdentityComponent>(target);events.GetComponent<ecs::LuaScriptComponent>(source)->properties["target"]={{"$entity",events.GetComponent<ecs::SceneIdentityComponent>(target)->value}};
 lua.Start(events);lua.Update(0);Check(X(events,source)==0,"reply deferred to next Update");lua.Update(0);Check(X(events,source)==9,"reply source and amount preserved");lua.Stop();
 Write("scripts/request.lua","return {fields={target={type='entity'}},on_update=function(self) World.send_event(self.target,'request',25);World.destroy(self.entity) end}");lua.Start(events);lua.Update(0);Check(!events.IsAlive(source),"sender destroyed before delivery");events.CreateEntity();lua.Update(0);Check(X(events,target)==8,"dead source delivered as nil after slot reuse");lua.Stop();
 Write("scripts/chain.lua","return {on_start=function(self) self.n=0;World.send_event(self.entity,'chain',0) end,on_event=function(self,e) assert(e.source==self.entity);self.n=self.n+1;World.set_position(self.entity,Vec3.new(self.n,0,0));World.send_event(self.entity,'chain',0) end}");
 ecs::World chain;auto chained=Actor(chain,"scripts/chain.lua");lua.Start(chain);for(int i=0;i<20;++i){lua.Update(0);Check(X(chain,chained)==i+1,"self reply chain bounded to one asynchronous delivery per Update");}lua.Stop();
 Write("scripts/flood.lua","return {on_start=function(self) self.n=0;for i=1,2000 do World.send_event(self.entity,'event',0) end end,on_event=function(self,e) self.n=self.n+1;World.set_position(self.entity,Vec3.new(self.n,0,0)) end}");
 ecs::World flood;auto flooded=Actor(flood,"scripts/flood.lua");lua.Start(flood);lua.Update(0);Check(X(flood,flooded)==1024,"event flood respects existing queue cap");lua.Stop();

}
