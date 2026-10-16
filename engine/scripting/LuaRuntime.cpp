#include "LuaRuntime.h"
#include "Prefab.h"
#include "EntityReference.h"
#include "../ecs/ComponentRegistry.h"
#include "../ecs/PhysicsQueries.h"
#include "../core/Logger.h"
#include "../ecs/MutationQueue.h"
#include "../ecs/components/LuaScriptComponent.h"
#include "../ecs/components/TagComponent.h"
#include "../ecs/components/TransformComponent.h"
#include "../ecs/components/LightComponent.h"
#include "../ecs/PlayerSpawn.h"
#include <sol/sol.hpp>
#include <fstream>
#include <map>
#include <set>
#include <cmath>
#include <thread>
#include <limits>
#if defined(TRACY_ENABLE)
#include <tracy/Tracy.hpp>
#endif
namespace scripting {
namespace {
using Json=nlohmann::json;
struct EntityLess {bool operator()(ecs::Entity a,ecs::Entity b)const{return std::tie(a.world,a.index,a.generation)<std::tie(b.world,b.index,b.generation);}};
void Budget(lua_State* state,lua_Debug*){luaL_error(state,"Lua instruction budget exceeded");}
struct ExecutionBudget {
    lua_State* state;
    explicit ExecutionBudget(lua_State* s):state(s){lua_sethook(s,Budget,LUA_MASKCOUNT,200000);}
    ~ExecutionBudget(){lua_sethook(state,nullptr,0,0);}
};
sol::object ToLua(sol::state& lua,const Json& value){
    if(value.is_boolean())return sol::make_object(lua,value.get<bool>());
    if(value.is_number_integer())return sol::make_object(lua,value.get<lua_Integer>());
    if(value.is_number())return sol::make_object(lua,value.get<double>());
    if(value.is_string())return sol::make_object(lua,value.get<std::string>());
    if(value.is_array()&&value.size()==3)return sol::make_object(lua,ecs::Vec3{value[0].get<float>(),value[1].get<float>(),value[2].get<float>()});
    throw std::runtime_error("Unsupported Lua field value");
}
bool ValidValue(const Json& field,const Json& value){
    const auto type=field.at("type").get<std::string>();
    if(type=="entity")return ValidEntityReference(value);
    if(type=="bool")return value.is_boolean();
    if(type=="string"||type=="asset")return value.is_string()&&value.get_ref<const std::string&>().size()<=1024;
    auto number=[&](const Json& v){return v.is_number()&&std::isfinite(v.get<double>())&&v.get<double>()>=field.value("min",-1e6)&&v.get<double>()<=field.value("max",1e6);};
    if(type=="integer")return value.is_number_integer()&&number(value);
    if(type=="number")return number(value);
    if(type=="vec3")return value.is_array()&&value.size()==3&&number(value[0])&&number(value[1])&&number(value[2]);
    return false;
}
Json Schema(sol::table definition){
    Json schema=Json::object();sol::object fields=definition["fields"];
    if(!fields.valid()||fields==sol::nil)fields=sol::make_object(definition.lua_state(),sol::table::create(definition.lua_state()));
    if(fields.get_type()!=sol::type::table)throw std::runtime_error("fields must be a table");
    for(auto item:fields.as<sol::table>()){
        if(item.first.get_type()!=sol::type::string||item.second.get_type()!=sol::type::table)throw std::runtime_error("fields must map names to schemas");
        auto key=item.first.as<std::string>();auto f=item.second.as<sol::table>();
        if(key.empty()||key=="entity"||key.size()>128||schema.size()>=128)throw std::runtime_error("Invalid/oversized field schema");
        sol::object typeObject=f["type"];if(typeObject.get_type()!=sol::type::string)throw std::runtime_error("Field type must be a string: "+key);
        Json field={{"type",typeObject.as<std::string>()},{"label",key}};
        sol::object label=f["label"];if(label.valid()&&label!=sol::nil){if(label.get_type()!=sol::type::string)throw std::runtime_error("Field label must be a string");field["label"]=label.as<std::string>();}
        for(const char* bound:{"min","max"}){sol::object b=f[bound];if(b.valid()&&b!=sol::nil){if(b.get_type()!=sol::type::number)throw std::runtime_error("Field bounds must be numeric");auto value=b.as<double>();if(!std::isfinite(value))throw std::runtime_error("Nonfinite field bound");field[bound]=value;}}
        sol::object d=f["default"];const auto type=field["type"].get<std::string>();
        if(type=="entity"){if(d.valid()&&d!=sol::nil)throw std::runtime_error("Entity default must be nil; assign a scene object in Inspector");field["default"]=nullptr;}
        else if(type=="bool"){if(d.get_type()!=sol::type::boolean)throw std::runtime_error("Boolean default required");field["default"]=d.as<bool>();}
        else if(type=="integer") {if(!d.is<lua_Integer>())throw std::runtime_error("Integer default required");field["default"]=d.as<lua_Integer>();}
        else if(type=="number"){if(d.get_type()!=sol::type::number)throw std::runtime_error("Numeric default required");field["default"]=d.as<double>();}
        else if(type=="string"||type=="asset"){if(d.get_type()!=sol::type::string)throw std::runtime_error("String default required");field["default"]=d.as<std::string>();}
        else if(type=="vec3"){if(d.get_type()!=sol::type::table)throw std::runtime_error("Vec3 default must be a table");auto v=d.as<sol::table>();auto values=Json::array();for(int index=1;index<=3;++index){sol::object x=v[index];if(x.get_type()!=sol::type::number)throw std::runtime_error("Vec3 default must contain three numbers");values.push_back(x.as<double>());}field["default"]=values;}
        else throw std::runtime_error("Unknown field type: "+type);
        if(!ValidValue(field,field["default"]))throw std::runtime_error("Invalid default: "+key);
        schema[key]=field;
    }
    for(const char* callback:{"on_start","on_update","on_fixed_update","on_event","on_stop","on_destroy"}){
        sol::object fn=definition[callback];if(fn.valid()&&fn!=sol::nil&&fn.get_type()!=sol::type::function)throw std::runtime_error(std::string(callback)+" must be a function");
    }
    return schema;
}
}
struct LuaRuntime::Impl {
    // State is declared first, so all Lua references die before the VM.
    sol::state lua;
    struct Program {std::string source;Json fields;std::uint64_t revision=0;};
    struct Instance {std::string path;std::uint64_t revision;sol::environment env;sol::table definition,self;Json overrides;bool failed=false;ecs::Entity entity{};};
    std::map<std::string,Program> programs;
    std::map<std::string,std::string> errors;
    std::map<ecs::Entity,Instance,EntityLess> instances;
    std::map<ecs::Entity,std::string,EntityLess> rejected;
    struct Event {ecs::Entity target,other;std::string type;float amount=0;};
    std::vector<Event> events;
    struct Timer {ecs::Entity owner;double deadline,interval;std::string event;};
    std::map<lua_Integer,Timer> timers;
    lua_Integer nextTimer=1;
    double gameTime=0;
    ecs::MutationQueue mutations;
    std::set<ecs::Entity,EntityLess> destroying;
    ecs::World* world=nullptr;
    std::function<bool(const std::string&)> input;
    GameplayServices services;
    ecs::Entity cameraOwner{},cursorOwner{};
    bool fixedPhase=false;
    std::map<std::string,bool> held,pressed,released;
    std::map<std::string,bool> fixedPressed,fixedReleased;
    std::map<std::string,std::string> boundKeys;
    const std::thread::id owner=std::this_thread::get_id();
    bool stopping=false;
    Impl(){
        lua.open_libraries(sol::lib::base,sol::lib::math,sol::lib::string,sol::lib::table,sol::lib::utf8);
        lua.new_usertype<ecs::Entity>("Entity",sol::no_constructor);
        lua.new_usertype<ecs::Vec3>("Vec3",sol::constructors<ecs::Vec3(),ecs::Vec3(float,float,float)>(),"x",&ecs::Vec3::x,"y",&ecs::Vec3::y,"z",&ecs::Vec3::z);
    }
    void Check()const{if(owner!=std::this_thread::get_id())throw std::logic_error("Lua VM belongs to its owner thread");}
    void Fail(const std::string& path,const std::string& callback,const std::string& message){errors[path]=callback+": "+message;Logger::Get().Error("Lua ["+path+"] "+errors[path]);}
    sol::environment Environment(){
        sol::environment env(lua,sol::create);
        // Explicit allowlist: no package/io/os/debug/load/dofile/rawset/metatables/pcall.
        for(const char* key:{"assert","error","ipairs","pairs","next","select","tonumber","tostring","type"})env[key]=lua[key];
        sol::table vector=lua.create_table();vector["new"]=lua["Vec3"]["new"];env["Vec3"]=vector;
        for(const char* key:{"math","string","table","utf8"}){
            sol::table copy=lua.create_table();sol::table original=lua[key];for(auto item:original)copy[item.first]=item.second;env[key]=copy;
        }
        return env;
    }
    sol::table Evaluate(const std::string& source,const std::string& path,sol::environment& env){
        ExecutionBudget budget(lua.lua_state());
        auto loaded=lua.load(source,"@"+path,sol::load_mode::text);
        if(!loaded.valid()){sol::error error=loaded;throw std::runtime_error(error.what());}
        sol::protected_function fn=loaded;sol::set_environment(env,fn);auto result=fn();
        if(!result.valid()){sol::error error=result;throw std::runtime_error(error.what());}
        sol::object value=result;if(value.get_type()!=sol::type::table)throw std::runtime_error("Script must return a behaviour table");return value.as<sol::table>();
    }
    bool Load(const std::string& path){
        Check();try{
            auto file=SafeAsset(path,".lua");if(std::filesystem::file_size(file)>256*1024)throw std::runtime_error("Script exceeds 256 KiB");
            std::ifstream stream(file,std::ios::binary);if(!stream)throw std::runtime_error("Cannot read script");
            std::string source((std::istreambuf_iterator<char>(stream)),{});
            // Validate in a disposable VM: even math.randomseed/upvalues must not
            // perturb a running program when a candidate is rejected.
            Impl validation;
            auto env=validation.Environment();auto definition=validation.Evaluate(source,path,env);auto fields=Schema(definition);
            auto revision=programs.contains(path)?programs.at(path).revision+1:1;
            programs[path]={std::move(source),std::move(fields),revision};errors.erase(path);return true;
        }catch(const std::exception& e){Fail(path,"load",e.what());return false;}
    }
    bool Alive(ecs::Entity e)const{return world&&world->IsAlive(e)&&!destroying.contains(e);}
    lua_Integer Schedule(ecs::Entity owner,double delay,const std::string& event,bool repeat){
        auto instance=instances.find(owner);
        if(stopping||!Alive(owner)||instance==instances.end()||instance->second.failed)throw std::runtime_error("Timer owner is not an active script instance");
        if(!std::isfinite(delay)||delay<0||delay>1e6||(repeat&&delay==0)||event.empty()||event.size()>128)throw std::runtime_error("Timer requires bounded delay (every > 0) and event name (1..128 characters)");
        if(timers.size()>=1024||std::count_if(timers.begin(),timers.end(),[&](const auto& t){return t.second.owner==owner;})>=128||nextTimer==std::numeric_limits<lua_Integer>::max())throw std::runtime_error("Timer budget exceeded");
        auto id=nextTimer++;timers.emplace(id,Timer{owner,gameTime+delay,repeat?delay:0,event});return id;
    }
    void CancelTimers(ecs::Entity owner){std::erase_if(timers,[&](const auto& t){return t.second.owner==owner;});}
    void FireTimers(){
        std::vector<lua_Integer> due;for(auto& [id,t]:timers)if(t.deadline<=gameTime)due.push_back(id);
        for(auto id:due){auto at=timers.find(id);if(at==timers.end())continue;auto timer=at->second;auto instance=instances.find(timer.owner);
            if(!Alive(timer.owner)||instance==instances.end()||instance->second.failed){timers.erase(at);continue;}
            // At most one delivery per repeating timer per Update; skip missed
            // periods without building an unbounded catch-up event queue.
            if(timer.interval>0)at->second.deadline=timer.deadline+(std::floor((gameTime-timer.deadline)/timer.interval)+1)*timer.interval;
            else timers.erase(at);
            auto data=lua.create_table();data["type"]=timer.event;data["amount"]=0;data["timer"]=id;data["other"]=ecs::Entity{};Call(instance->second,"on_event",data);
        }
    }
    ecs::TransformComponent& Transform(ecs::Entity e){if(!Alive(e))throw std::runtime_error("Dead/stale/foreign entity");auto* t=world->GetComponent<ecs::TransformComponent>(e);if(!t)throw std::runtime_error("Entity has no Transform");return *t;}
    void SetTransform(ecs::Entity e,const char* field,ecs::Vec3 value){
        Transform(e);std::string error;
        if(!ecs::ComponentRegistry::Builtins().Set(*world,e,ecs::ComponentId("Transform"),field,value,&error))throw std::runtime_error("World.set_"+std::string(field)+": "+error);
    }
    std::string LightComponent(ecs::Entity e){
        if(!Alive(e))throw std::runtime_error("Light: dead/stale/foreign entity");
        if(world->HasComponent<ecs::DirectionalLightComponent>(e))return "DirectionalLight";
        if(world->HasComponent<ecs::PointLightComponent>(e))return "PointLight";
        if(world->HasComponent<ecs::SpotLightComponent>(e))return "SpotLight";
        throw std::runtime_error("Light: entity has no Directional/Point/Spot Light component");
    }
    bool InputAllowed(ecs::Entity owner)const{auto* c=Alive(owner)?world->GetComponent<ecs::LuaScriptComponent>(owner):nullptr;return !stopping&&c&&c->enabled&&c->inputEnabled&&(!services.inputAllowed||services.inputAllowed());}
    bool MotionAllowed(ecs::Entity owner)const{auto* c=Alive(owner)?world->GetComponent<ecs::LuaScriptComponent>(owner):nullptr;return !stopping&&c&&c->enabled&&(!c->inputEnabled||!services.inputAllowed||services.inputAllowed());}
    ecs::CharacterMovementComponent& Movement(ecs::Entity owner,ecs::Entity e){
        if(!Alive(e))throw std::runtime_error("Character: dead/stale/foreign entity");
        if(owner!=e)throw std::runtime_error("Character: a script controls only its own entity");
        auto* m=world->GetComponent<ecs::CharacterMovementComponent>(e);
        if(!m||!world->HasComponent<ecs::RigidbodyComponent>(e)||!world->HasComponent<ecs::ColliderComponent>(e)||!world->HasComponent<ecs::TransformComponent>(e))throw std::runtime_error("Character requires CharacterMovement, Rigidbody, Collider and Transform");return *m;
    }
    static void Vector(ecs::Vec3 v){if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z)||std::max({std::abs(v.x),std::abs(v.y),std::abs(v.z)})>1e6f)throw std::runtime_error("Expected finite bounded vector");}
    static void Number(float n,float max){if(!std::isfinite(n)||n<0||n>max)throw std::runtime_error("Gameplay parameter outside valid range");}
    void ClearControl(ecs::Entity e){
        if(world&&world->IsAlive(e))if(auto* m=world->GetComponent<ecs::CharacterMovementComponent>(e)){m->desiredDirection={};m->jumpRequested=false;if(auto* b=world->GetComponent<ecs::RigidbodyComponent>(e)){b->velocity.x=0;b->velocity.z=0;}}
        if(cameraOwner==e){if(services.camera)services.camera->following=false;cameraOwner={};}
        if(cursorOwner==e){if(services.captureCursor)services.captureCursor(false);cursorOwner={};}
    }
    bool AcquireCamera(ecs::Entity e){
        auto* c=Alive(e)?world->GetComponent<ecs::LuaScriptComponent>(e):nullptr;
        if(stopping||!c||!c->enabled||!c->cameraEnabled||!services.camera)return false;
        if(cameraOwner.IsValid()&&Alive(cameraOwner)&&cameraOwner!=e)return false;cameraOwner=e;return true;
    }
    GameplayCamera& Camera(ecs::Entity e){if(!AcquireCamera(e))throw std::runtime_error("Camera permission missing or camera owned by another script");return *services.camera;}
    void BindAction(ecs::Entity owner,const std::string& action,const std::string& key){
        auto* c=Alive(owner)?world->GetComponent<ecs::LuaScriptComponent>(owner):nullptr;
        if(stopping||!c||!c->inputEnabled||!services.bindAction||!services.bindAction(action,key))throw std::runtime_error("Input.bind: permission missing, unknown key name or action budget exceeded");
        if(!boundKeys.contains(action)||boundKeys[action]!=key){
            boundKeys[action]=key;held.erase(action);pressed.erase(action);released.erase(action);fixedPressed.erase(action);fixedReleased.erase(action);
        }
        DiscoverInput(action);
    }
    void Bind(sol::environment& env,ecs::Entity owner){
        auto api=lua.create_table();
        api.set_function("is_alive",[this](ecs::Entity e){return Alive(e);});
        api.set_function("find_by_tag",[this](const std::string& tag){auto list=lua.create_table();int i=1;for(auto e:world->Entities())if(auto* t=world->GetComponent<ecs::TagComponent>(e);Alive(e)&&t&&t->name==tag)list[i++]=e;return list;});
        api.set_function("get_position",[this](ecs::Entity e){return Transform(e).position;});
        api.set_function("get_rotation",[this](ecs::Entity e){return Transform(e).rotation;});
        api.set_function("get_scale",[this](ecs::Entity e){return Transform(e).scale;});
        api.set_function("find_in_radius",[this](ecs::Vec3 center,float radius){auto list=lua.create_table();int index=1;for(auto e:ecs::FindInRadius(*world,center,radius))if(Alive(e))list[index++]=e;return list;});
        api.set_function("set_position",[this](ecs::Entity e,ecs::Vec3 p){if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))throw std::runtime_error("Nonfinite position");Transform(e).position=p;});
        api.set_function("set_rotation",[this](ecs::Entity e,ecs::Vec3 v){SetTransform(e,"rotation",v);});
        api.set_function("set_scale",[this](ecs::Entity e,ecs::Vec3 v){SetTransform(e,"scale",v);});
        api.set_function("spawn_prefab",[this](const std::string& key,ecs::Vec3 p){if(stopping)throw std::runtime_error("Spawn during Stop");if(world->GetAliveCount()>10000)throw std::runtime_error("Lua entity budget exceeded");return SpawnPrefab(*world,key,p);});
        api.set_function("destroy",[this](ecs::Entity e){if(Alive(e)){destroying.insert(e);mutations.Destroy(e);}});
        api.set_function("send_event",[this](ecs::Entity e,const std::string& type,float amount){if(Alive(e)&&events.size()<1024&&type.size()<128&&std::isfinite(amount))events.push_back({e,{},type,amount});});
        env["World"]=api;
        auto physics=lua.create_table();
        physics.set_function("raycast",[this](ecs::Vec3 origin,ecs::Vec3 direction,float distance,sol::optional<ecs::Entity> ignore)->sol::object{
            auto hit=ecs::Raycast(*world,origin,direction,distance,ignore.value_or(ecs::Entity{}),[this](auto e){return Alive(e);});if(!hit)return sol::make_object(lua,sol::nil);
            auto result=lua.create_table();result["entity"]=hit->entity;result["position"]=hit->position;result["normal"]=hit->normal;result["distance"]=hit->distance;return sol::make_object(lua,result);
        });env["Physics"]=physics;
        auto timer=lua.create_table();timer.set_function("after",[this,owner](double delay,const std::string& event){return Schedule(owner,delay,event,false);});
        timer.set_function("every",[this,owner](double delay,const std::string& event){return Schedule(owner,delay,event,true);});
        timer.set_function("cancel",[this,owner](lua_Integer id){auto at=timers.find(id);if(at==timers.end()||at->second.owner!=owner)return false;timers.erase(at);return true;});env["Timer"]=timer;
        auto light=lua.create_table();
        light.set_function("get_intensity",[this](ecs::Entity e){ecs::PropertyValue value;std::string error;auto key=LightComponent(e);if(!ecs::ComponentRegistry::Builtins().Get(*world,e,ecs::ComponentId(key.c_str()),"intensity",value,&error))throw std::runtime_error(error);return std::get<float>(value);});
        light.set_function("set_intensity",[this](ecs::Entity e,float value){std::string error;auto key=LightComponent(e);if(!ecs::ComponentRegistry::Builtins().Set(*world,e,ecs::ComponentId(key.c_str()),"intensity",value,&error))throw std::runtime_error("Light.set_intensity: "+error);});
        env["Light"]=light;
        auto log=lua.create_table();log.set_function("info",[](const std::string& s){Logger::Get().Info("Lua: "+s);});log.set_function("warning",[](const std::string& s){Logger::Get().Warn("Lua: "+s);});log.set_function("error",[](const std::string& s){Logger::Get().Error("Lua: "+s);});env["Log"]=log;
        auto controls=lua.create_table();
        controls.set_function("is_down",[this,owner](const std::string& s){if(!InputAllowed(owner))return false;DiscoverInput(s);return held[s];});
        controls.set_function("was_pressed",[this,owner](const std::string& s){if(!InputAllowed(owner))return false;DiscoverInput(s);return fixedPhase?fixedPressed[s]:pressed[s];});
        controls.set_function("was_released",[this,owner](const std::string& s){if(!InputAllowed(owner))return false;DiscoverInput(s);return fixedPhase?fixedReleased[s]:released[s];});
        controls["is_action_held"]=controls["is_down"];controls["is_action_pressed"]=controls["was_pressed"];controls["is_action_released"]=controls["was_released"];
        controls.set_function("bind",[this,owner](const std::string& action,const std::string& key){BindAction(owner,action,key);});
        controls.set_function("mouse_delta",[this,owner](){auto delta=InputAllowed(owner)&&services.mouseDelta?services.mouseDelta():ecs::Vec3{};return std::make_tuple(delta.x,delta.y);});
        controls.set_function("cursor_captured",[this,owner](){return InputAllowed(owner)&&cursorOwner==owner&&services.cursorCaptured&&services.cursorCaptured();});
        controls.set_function("capture_cursor",[this,owner](bool capture){if(!capture){if(cursorOwner==owner){if(services.captureCursor)services.captureCursor(false);cursorOwner={};}return true;}if(!InputAllowed(owner)||!services.captureCursor)return false;if(Alive(cursorOwner)&&cursorOwner!=owner&&services.cursorCaptured&&services.cursorCaptured())return false;bool ok=services.captureCursor(true);if(ok)cursorOwner=owner;return ok;});env["Input"]=controls;
        auto character=lua.create_table();
        character.set_function("move_world",[this,owner](ecs::Entity e,ecs::Vec3 direction){Vector(direction);auto& m=Movement(owner,e);if(!MotionAllowed(owner))return;m.desiredDirection={};float n=std::hypot(direction.x,direction.z);if(n>.0001f)m.desiredDirection={direction.x/std::max(1.f,n),0,direction.z/std::max(1.f,n)};});
        character.set_function("move_local",[this,owner](ecs::Entity e,float x,float z){Vector({x,0,z});auto& m=Movement(owner,e);if(!MotionAllowed(owner))return;float yaw=Transform(e).rotation.y,n=std::max(1.f,std::hypot(x,z));m.desiredDirection={(std::cos(yaw)*x+std::sin(yaw)*z)/n,0,(-std::sin(yaw)*x+std::cos(yaw)*z)/n};});
        character.set_function("jump",[this,owner](ecs::Entity e){auto& m=Movement(owner,e);if(MotionAllowed(owner))m.jumpRequested=true;});
        character.set_function("stop",[this,owner](ecs::Entity e){auto& m=Movement(owner,e);m.desiredDirection={};m.jumpRequested=false;});
        character.set_function("is_grounded",[this,owner](ecs::Entity e){return Movement(owner,e).grounded;});
        character.set_function("get_move_speed",[this,owner](ecs::Entity e){return Movement(owner,e).moveSpeed;});
        character.set_function("get_jump_speed",[this,owner](ecs::Entity e){return Movement(owner,e).jumpSpeed;});
        character.set_function("set_move_speed",[this,owner](ecs::Entity e,float speed){Number(speed,30);Movement(owner,e).moveSpeed=speed;});
        character.set_function("set_jump_speed",[this,owner](ecs::Entity e,float speed){Number(speed,20);Movement(owner,e).jumpSpeed=speed;});
        character.set_function("velocity",[this,owner](ecs::Entity e){Movement(owner,e);auto v=world->GetComponent<ecs::RigidbodyComponent>(e)->velocity;return ecs::Vec3{v.x,0,v.z};});
        character.set_function("eye_offset",[this,owner](ecs::Entity e){Movement(owner,e);auto* p=world->GetComponent<ecs::PlayerComponent>(e);auto* c=world->GetComponent<ecs::ColliderComponent>(e);return ecs::Vec3{0,(p?p->eyeHeight:c->halfExtents.y*2)-c->halfExtents.y+c->offset.y,0};});env["Character"]=character;
        auto camera=lua.create_table();
        camera.set_function("acquire",[this,owner](){return AcquireCamera(owner);});
        camera.set_function("release",[this,owner](){if(cameraOwner==owner){if(services.camera)services.camera->following=false;cameraOwner={};}});
        camera.set_function("angles",[this,owner](){auto& c=Camera(owner);return std::make_tuple(c.yaw,c.pitch);});
        camera.set_function("set_angles",[this,owner](float yaw,float pitch){Vector({yaw,pitch,0});auto& c=Camera(owner);if(MotionAllowed(owner)){c.yaw=std::remainder(yaw,6.283185307f);c.pitch=std::clamp(pitch,-ecs::PlayerMaximumPitch,ecs::PlayerMaximumPitch);}});
        camera.set_function("rotate",[this,owner](float yaw,float pitch){Vector({yaw,pitch,0});auto& c=Camera(owner);if(MotionAllowed(owner)){c.yaw=std::remainder(c.yaw+yaw,6.283185307f);c.pitch=std::clamp(c.pitch+pitch,-ecs::PlayerMaximumPitch,ecs::PlayerMaximumPitch);}});
        camera.set_function("forward",[this,owner](){auto& c=Camera(owner);ecs::TransformComponent t;t.rotation={c.pitch,c.yaw,0};return ecs::PlayerLookDirection(t);});
        camera.set_function("flat_forward",[this,owner](){auto& c=Camera(owner);return ecs::Vec3{std::sin(c.yaw),0,std::cos(c.yaw)};});
        camera.set_function("right",[this,owner](){auto& c=Camera(owner);return ecs::Vec3{std::cos(c.yaw),0,-std::sin(c.yaw)};});
        camera.set_function("position",[this,owner](){return Camera(owner).position;});
        camera.set_function("set_position",[this,owner](ecs::Vec3 p){Vector(p);auto& c=Camera(owner);if(MotionAllowed(owner)){c.position=p;c.following=false;}});
        camera.set_function("follow",[this,owner](ecs::Entity target,ecs::Vec3 offset){Transform(target);Vector(offset);auto& c=Camera(owner);c.target=target;c.offset=offset;c.following=true;});env["Camera"]=camera;
    }
    void DiscoverInput(const std::string& action){
        if(action.empty()||action.size()>128||(held.size()>=128&&!held.contains(action)))throw std::runtime_error("Input action name/budget invalid");
        if(!held.contains(action)){bool now=input&&input(action);held[action]=now;pressed[action]=services.pressed?services.pressed(action):now;released[action]=services.released&&services.released(action);fixedPressed[action]|=pressed[action];fixedReleased[action]|=released[action];}
    }
    template<class... Args>void Call(Instance& instance,const char* callback,Args&&... args){
        if(instance.failed&&std::string_view(callback)!="on_destroy"&&std::string_view(callback)!="on_stop")return;
        try{
        sol::object fn=instance.definition[callback];if(!fn.valid()||fn==sol::nil)return;
        if(fn.get_type()!=sol::type::function)throw std::runtime_error("Callback is no longer a function");
        ExecutionBudget budget(lua.lua_state());sol::protected_function function=fn;auto result=function(instance.self,std::forward<Args>(args)...);
        if(!result.valid()){sol::error error=result;instance.failed=true;Fail(instance.path,callback,error.what());}
        }catch(const std::exception& error){instance.failed=true;Fail(instance.path,callback,error.what());}
        if(instance.failed){CancelTimers(instance.entity);ClearControl(instance.entity);}
    }
    void Retire(ecs::Entity e,Instance& instance){Call(instance,"on_stop");Call(instance,"on_destroy");CancelTimers(e);ClearControl(e);}
    void Sync(){
        if(Alive(cameraOwner)){auto* c=world->GetComponent<ecs::LuaScriptComponent>(cameraOwner);if(!c||!c->enabled||!c->cameraEnabled)ClearControl(cameraOwner);}
        if(Alive(cursorOwner)){auto* c=world->GetComponent<ecs::LuaScriptComponent>(cursorOwner);if(!c||!c->enabled||!c->inputEnabled)ClearControl(cursorOwner);}
        for(auto i=instances.begin();i!=instances.end();){auto* c=world->GetComponent<ecs::LuaScriptComponent>(i->first);
            if(!Alive(i->first)||!c||!c->enabled||c->scriptPath!=i->second.path||c->properties!=i->second.overrides||programs.at(i->second.path).revision!=i->second.revision){Retire(i->first,i->second);i=instances.erase(i);}else ++i;
        }
        // Snapshot IDs: callbacks may spawn; new entities start on the following Update.
        for(auto e:world->Entities()){
            auto* c=world->GetComponent<ecs::LuaScriptComponent>(e);if(!Alive(e)||!c||!c->enabled||c->scriptPath.empty()||instances.contains(e))continue;
            const auto path=c->scriptPath;const auto overrides=c->properties;
            if(!programs.contains(path)&&(!errors.contains(path)?!Load(path):true))continue;
            const auto fingerprint=path+":"+std::to_string(programs.at(path).revision)+":"+overrides.dump();
            if(rejected.contains(e)&&rejected.at(e)==fingerprint)continue;
            try{
                auto& program=programs.at(path);auto env=Environment();auto definition=Evaluate(program.source,path,env);Bind(env,e);
                auto self=lua.create_table();self["entity"]=e;
                for(auto it=program.fields.begin();it!=program.fields.end();++it){auto value=overrides.contains(it.key())?overrides.at(it.key()):it.value().at("default");if(!ValidValue(it.value(),value))throw std::runtime_error("Invalid override: "+it.key());
                    if(it.value().at("type")=="entity"){auto target=ResolveEntityReference(*world,value);self[it.key()]=Alive(target)?sol::make_object(lua,target):sol::make_object(lua,sol::nil);}
                    else self[it.key()]=ToLua(lua,value);
                }
                auto [it,ok]=instances.emplace(e,Instance{path,program.revision,std::move(env),definition,self,overrides,false,e});Call(it->second,"on_start");
                rejected.erase(e);
            }catch(const std::exception& error){rejected[e]=fingerprint;Fail(path,"instance",error.what());}
        }
        std::erase_if(rejected,[this](const auto& pair){return !world->IsAlive(pair.first);});
    }
};
LuaRuntime::LuaRuntime():impl(std::make_unique<Impl>()){}
LuaRuntime::~LuaRuntime(){Stop();}
void LuaRuntime::Start(ecs::World& world,std::function<bool(const std::string&)> input,GameplayServices services){Stop();impl->Check();world.AssertOwnerThread();impl->programs.clear();impl->errors.clear();impl->world=&world;impl->input=std::move(input);impl->services=std::move(services);}
void LuaRuntime::Stop(){
    impl->Check();impl->stopping=true;
    for(auto& [e,i]:impl->instances)impl->Retire(e,i);
    impl->instances.clear();impl->rejected.clear();impl->events.clear();
    impl->programs.clear();impl->errors.clear();
    impl->timers.clear();impl->gameTime=0;
    impl->mutations.Clear();impl->destroying.clear();impl->world=nullptr;
    impl->input={};impl->services={};impl->cameraOwner={};impl->cursorOwner={};
    impl->held.clear();impl->pressed.clear();impl->released.clear();
    impl->fixedPressed.clear();impl->fixedReleased.clear();impl->boundKeys.clear();
    impl->fixedPhase=false;impl->stopping=false;
    lua_gc(impl->lua.lua_state(),LUA_GCCOLLECT,0);
}
void LuaRuntime::Update(float dt){
#if defined(TRACY_ENABLE)
    ZoneScopedN("LuaGameplayUpdate");
#endif
    impl->Check();if(!impl->world||!std::isfinite(dt)||dt<0)return;
    impl->world->AssertOwnerThread();if(impl->world->IsIterating())throw std::logic_error("Lua Update during ECS iteration");
    if(impl->services.actions)for(const auto& action:impl->services.actions())if(!action.empty()&&action.size()<=128&&(impl->held.size()<128||impl->held.contains(action)))impl->DiscoverInput(action);
    for(auto& [action,previous]:impl->held){bool now=impl->input&&impl->input(action);impl->pressed[action]=impl->services.pressed?impl->services.pressed(action):now&&!previous;impl->released[action]=impl->services.released?impl->services.released(action):!now&&previous;impl->fixedPressed[action]|=impl->pressed[action];impl->fixedReleased[action]|=impl->released[action];previous=now;}
    if(impl->services.inputAllowed&&!impl->services.inputAllowed()){impl->fixedPressed.clear();impl->fixedReleased.clear();}
    impl->gameTime+=double(dt);impl->Sync();auto events=std::move(impl->events);impl->events.clear();
    for(auto& [e,i]:impl->instances)if(auto* m=impl->world->GetComponent<ecs::CharacterMovementComponent>(e)){m->desiredDirection={};if(!impl->MotionAllowed(e)||i.failed){m->jumpRequested=false;if(!impl->MotionAllowed(e)){impl->fixedPressed.clear();impl->fixedReleased.clear();}}}
    impl->FireTimers();
    for(const auto& event:events)if(impl->Alive(event.target)){auto i=impl->instances.find(event.target);if(i!=impl->instances.end()){auto data=impl->lua.create_table();data["type"]=event.type;data["amount"]=event.amount;data["other"]=event.other;impl->Call(i->second,"on_event",data);}}
    for(auto& [e,i]:impl->instances)if(impl->Alive(e))impl->Call(i,"on_update",std::min(dt,.1f));
    // Destruction callback while components still exist; apply once, after all calls.
    for(auto e:impl->destroying){auto i=impl->instances.find(e);if(i!=impl->instances.end()){impl->Retire(e,i->second);impl->instances.erase(i);}}
    impl->mutations.Apply(*impl->world);impl->destroying.clear();
}
void LuaRuntime::FixedUpdate(float dt){
    impl->Check();if(!impl->world||!std::isfinite(dt)||dt<=0)return;
    impl->world->AssertOwnerThread();if(impl->world->IsIterating())throw std::logic_error("Lua FixedUpdate during ECS iteration");
    impl->Sync();impl->fixedPhase=true;
    for(auto& [e,i]:impl->instances)if(impl->Alive(e)){
        if(!impl->MotionAllowed(e))if(auto* m=impl->world->GetComponent<ecs::CharacterMovementComponent>(e)){m->desiredDirection={};m->jumpRequested=false;}
        impl->Call(i,"on_fixed_update",dt);
    }
    impl->fixedPhase=false;impl->fixedPressed.clear();impl->fixedReleased.clear();
    for(auto e:impl->destroying){auto i=impl->instances.find(e);if(i!=impl->instances.end()){impl->Retire(e,i->second);impl->instances.erase(i);}}
    impl->mutations.Apply(*impl->world);impl->destroying.clear();
}
void LuaRuntime::SuspendInput(){
    impl->Check();impl->held.clear();impl->pressed.clear();impl->released.clear();impl->fixedPressed.clear();impl->fixedReleased.clear();
    if(impl->world)for(const auto& [e,i]:impl->instances)if(auto* m=impl->world->GetComponent<ecs::CharacterMovementComponent>(e)){m->desiredDirection={};m->jumpRequested=false;if(auto* b=impl->world->GetComponent<ecs::RigidbodyComponent>(e)){b->velocity.x=0;b->velocity.z=0;}}
    if(impl->services.captureCursor)impl->services.captureCursor(false);impl->cursorOwner={};
}
void LuaRuntime::QueueCollision(const ecs::CollisionEvent& e){impl->Check();if(impl->world&&impl->events.size()<1022){const char* type=e.kind==ecs::CollisionKind::TriggerEnter?"trigger_enter":e.kind==ecs::CollisionKind::TriggerExit?"trigger_exit":"collision";if(impl->Alive(e.a))impl->events.push_back({e.a,e.b,type});if(impl->Alive(e.b))impl->events.push_back({e.b,e.a,type});}}
bool LuaRuntime::ReloadScript(const std::string& path){return impl->Load(path);}
nlohmann::json LuaRuntime::Fields(const std::string& path){impl->Check();if(!impl->programs.contains(path)&&!impl->errors.contains(path))impl->Load(path);auto i=impl->programs.find(path);return i==impl->programs.end()?Json::object():i->second.fields;}
std::string LuaRuntime::Error(const std::string& path)const{impl->Check();auto i=impl->errors.find(path);return i==impl->errors.end()?std::string():i->second;}
std::size_t LuaRuntime::InstanceCount()const{return impl->instances.size();}
bool LuaRuntime::HasCameraOwner()const{
    impl->Check();
    auto* script=impl->Alive(impl->cameraOwner)?impl->world->GetComponent<ecs::LuaScriptComponent>(impl->cameraOwner):nullptr;
    return script&&script->enabled&&script->cameraEnabled;
}
}
