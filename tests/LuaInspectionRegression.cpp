#define NOMINMAX
#include "core/AssetPaths.h"
#include "scripting/LuaRuntime.h"
#include "scripting/Prefab.h"
#include "ecs/components/LuaScriptComponent.h"
#include "ecs/components/SceneIdentityComponent.h"
#include "ecs/components/TagComponent.h"
#include "ecs/components/TransformComponent.h"
#include <fstream>
#include <iostream>
#include <thread>
using namespace ecs;using namespace scripting;
static unsigned checks=0;
static void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);++checks;}
struct Fixture {
 std::string key="scripts/inspection-"+NewSceneIdentity()+".lua";std::filesystem::path path=SafeAsset(key,".lua");
 void Write(const std::string& source){std::ofstream out(path);out<<source;Check(out.good(),"write owned script");}
 ~Fixture(){std::error_code ec;std::filesystem::remove(path,ec);}
};
static Entity Actor(World& world,const std::string& path){auto e=world.CreateEntity();world.AddComponent<TransformComponent>(e);world.AddComponent<SceneIdentityComponent>(e);world.AddComponent<LuaScriptComponent>(e).scriptPath=path;return e;}
static const LuaInspectedValue& Field(const LuaInstanceSnapshot& snapshot,const std::string& name){auto key='"'+name+'"';auto i=std::find_if(snapshot.fields.begin(),snapshot.fields.end(),[&](const auto& field){return field.name==key;});if(i==snapshot.fields.end())throw std::runtime_error("Missing field: "+name);return *i;}
static std::size_t Nodes(const std::vector<LuaInspectedValue>& values){std::size_t n=values.size();for(const auto& value:values)n+=Nodes(value.children);return n;}
int main(){try{
 Fixture script;script.Write(R"(
 local definition={custom={label='per instance'}}
 definition.fields={seed={type='number',default=1},target={type='entity'},optional={type='entity'}}
 definition.on_start=function(self)
   self.counter=self.seed;self.enabled=true;self.text='door state';self.callback=function() error('inspection called a function') end
   self.vector=Vec3.new(1,2,3);self.loop={};self.loop.again=self.loop
   self.nested={child={child={child={value='depth bound'}}}};self.large={}
   for n=1,400 do self.large[n]=n end
   self.long=string.rep('x',2000)
 end
 definition.on_update=function(self,dt) self.counter=self.counter+1 end
 return definition
 )");
 World world;auto a=Actor(world,script.key),b=Actor(world,script.key);
 world.GetComponent<LuaScriptComponent>(a)->properties={{"seed",2},{"target",{{"$entity",world.GetComponent<SceneIdentityComponent>(b)->value}}}};
 world.GetComponent<LuaScriptComponent>(b)->properties={{"seed",20}};
 LuaRuntime lua;Check(lua.InspectionInfo(a).state==LuaInstanceState::Stopped,"Edit has no runtime");lua.Start(world);
 Check(lua.InspectionInfo(a).state==LuaInstanceState::Pending,"inspection does not instantiate pending script");Check(lua.InstanceCount()==0,"no implicit instance creation");lua.Update(0);
 auto before=world.LocalTransform(a);const auto instances=lua.InstanceCount();auto snapshot=lua.Inspect(a);
 Check(snapshot.info.state==LuaInstanceState::Running&&snapshot.info.serial!=0,"actual running instance");
 // Large containers share the same bounded read budget, so test primitive fields
 // in a small instance separately from the adversarial container fixture.
 Check(snapshot.limited&&Nodes(snapshot.fields)+Nodes(snapshot.definition)<=256,"nested table inspection has a global node bound");Check(Field(snapshot,"long").limited&&Field(snapshot,"long").value.size()<600,"long strings are truncated");
 Check(lua.InstanceCount()==instances&&world.LocalTransform(a).position.x==before.position.x,"inspection preserves runtime and World");
 Fixture simple;simple.Write(R"(local result={custom={label='isolated'}};result.fields={seed={type='number',default=1},target={type='entity'},optional={type='entity'}}
 result.on_start=function(self) self.counter=self.seed;self.enabled=true;self.text='door state';self.callback=function() error('must not run') end;self.vector=Vec3.new(1,2,3);self.loop={};self.loop.again=self.loop end
 result.on_update=function(self,dt) self.counter=self.counter+1 end;return result)");
 world.GetComponent<LuaScriptComponent>(a)->scriptPath=simple.key;world.GetComponent<LuaScriptComponent>(b)->scriptPath=simple.key;lua.Update(0);
 snapshot=lua.Inspect(a);auto other=lua.Inspect(b);Check(Field(snapshot,"counter").value=="3"&&Field(other,"counter").value=="21","real independent state of the same script");
 Check(Field(snapshot,"enabled").type=="boolean"&&Field(snapshot,"enabled").value=="true","boolean");Check(Field(snapshot,"text").type=="string"&&Field(snapshot,"text").value=="\"door state\"","bounded string");
 Check(Field(snapshot,"optional").type=="nil"&&Field(other,"target").type=="nil","absent declared fields are actual nil");Check(Field(snapshot,"target").type=="Entity"&&Field(snapshot,"target").entity==b,"existing bound Entity userdata");Check(Field(snapshot,"vector").type=="userdata"&&Field(snapshot,"callback").type=="function","opaque values never evaluated");
 Check(Field(snapshot,"loop").children.size()==1&&Field(snapshot,"loop").children.front().value=="<cycle>","cycles are bounded");Check(!snapshot.definition.empty(),"definition data is separate from self");
 for(int n=0;n<20;++n){auto repeated=lua.Inspect(a);Check(Field(repeated,"counter").value=="3","inspection never calls update");}Check(lua.Error(simple.key).empty(),"inspecting function does not call it");
 bool wrongThread=false;std::thread thread([&]{try{lua.Inspect(a);}catch(const std::logic_error&){wrongThread=true;}});thread.join();Check(wrongThread,"owner thread guard");
 auto serial=snapshot.info.serial;simple.Write("return {on_start=function(self) self.fresh=99 end}");Check(lua.ReloadScript(simple.key),"successful reload");Check(lua.InspectionInfo(a).state==LuaInstanceState::ReloadPending&&lua.Inspect(a).fields.empty(),"pending reload cannot expose old fields as current");lua.Update(0);snapshot=lua.Inspect(a);Check(snapshot.info.serial!=serial&&Field(snapshot,"fresh").value=="99","new live instance after reload");
 serial=snapshot.info.serial;simple.Write("return syntax error");Check(!lua.ReloadScript(simple.key),"rejected reload");snapshot=lua.Inspect(a);Check(snapshot.info.serial==serial&&snapshot.info.CanRead()&&!snapshot.info.loadError.empty()&&Field(snapshot,"fresh").value=="99","failed reload keeps actual old instance with source diagnostic");
 Fixture failure;failure.Write(R"(return {fields={fail={type='bool',default=false}},on_update=function(self) if self.fail then error('only this instance failed') end;self.ok=7 end})");world.GetComponent<LuaScriptComponent>(a)->scriptPath=failure.key;world.GetComponent<LuaScriptComponent>(a)->properties={{"fail",true}};world.GetComponent<LuaScriptComponent>(b)->scriptPath=failure.key;world.GetComponent<LuaScriptComponent>(b)->properties={{"fail",false}};lua.Update(0);
 auto failed=lua.Inspect(a);other=lua.Inspect(b);Check(failed.info.state==LuaInstanceState::Failed&&failed.info.lastError.find("only this instance failed")!=std::string::npos&&failed.info.lastCallback=="on_update"&&failed.info.lastStatus==EventStatus::Error,"callback fault attributed to its instance");Check(other.info.state==LuaInstanceState::Running&&other.info.lastError.empty(),"global path error is never assigned to healthy peer");
 world.GetComponent<LuaScriptComponent>(a)->properties={{"fail","wrong"}};lua.Update(0);failed=lua.Inspect(a);Check(failed.info.state==LuaInstanceState::Rejected&&failed.info.lastError.find("Invalid override")!=std::string::npos,"instance creation rejection attributed");
 world.GetComponent<LuaScriptComponent>(b)->enabled=false;Check(lua.Inspect(b).info.state==LuaInstanceState::Disabled&&lua.Inspect(b).fields.empty(),"disabled script fields unavailable");world.DestroyEntity(a);Check(lua.Inspect(a).info.state==LuaInstanceState::NoEntity,"destroyed entity safe before next Sync");auto reused=Actor(world,failure.key);Check(reused!=a&&lua.Inspect(a).fields.empty(),"reused index cannot revive old handle");auto session=snapshot.info.session;lua.Stop();Check(lua.Inspect(b).info.state==LuaInstanceState::Stopped&&lua.Inspect(b).fields.empty(),"Stop owns no retained Lua snapshots");lua.Start(world);lua.Update(0);Check(lua.InspectionInfo(reused).session!=session,"repeat Start has new session");lua.Stop();
 Fixture rejected;rejected.Write("return invalid syntax");World broken;auto brokenEntity=Actor(broken,rejected.key);lua.Start(broken);lua.Update(0);auto rejectedInfo=lua.InspectionInfo(brokenEntity);Check(rejectedInfo.state==LuaInstanceState::Rejected&&!rejectedInfo.loadError.empty()&&rejectedInfo.lastError.empty(),"initial source load failure has explicit source scope and rejected state");lua.Stop();
 rejected.Write("local result={};result.on_update=function(self) end;result.on_start=function(self) result.on_update=false end;return result");lua.Start(broken);lua.Update(0);auto malformed=lua.InspectionInfo(brokenEntity);Check(malformed.state==LuaInstanceState::Failed&&malformed.lastCallback=="on_update"&&malformed.lastError.find("no longer a function")!=std::string::npos,"invalid callback records the attempted handler");lua.Stop();
 std::cout<<"Lua Inspection: "<<checks<<" checks passed\n";return 0;
 }catch(const std::exception& error){std::cerr<<"LUA INSPECTION FAILED after "<<checks<<": "<<error.what()<<'\n';return 1;}}
