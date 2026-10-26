#include "../ecs/components/CameraAttachmentComponent.h"
#include "LuaRuntime.h"
#include "../ecs/NavGrid.h"
#include "LuaModulePath.h"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sol/sol.hpp>
#include <sstream>
#include <thread>
#include <unordered_set>

#include "../core/Logger.h"
#include "../ecs/ComponentRegistry.h"
#include "../ecs/MutationQueue.h"
#include "../ecs/PhysicsQueries.h"
#include "../ecs/PlayerSpawn.h"
#include "../ecs/components/LightComponent.h"
#include "../ecs/components/LuaScriptComponent.h"
#include "../ecs/components/SceneIdentityComponent.h"
#include "../ecs/components/TagComponent.h"
#include "../ecs/components/TransformComponent.h"
#include "../resources/ResourceManager.h"
#include "../ui/RuntimeUI.h"
#include "EntityReference.h"
#include "Prefab.h"
#if defined(TRACY_ENABLE)
#include <tracy/Tracy.hpp>
#endif
namespace scripting {
namespace {
using Json = nlohmann::json;
struct EntityLess {
  bool operator()(ecs::Entity a, ecs::Entity b) const {
    return std::tie(a.world, a.index, a.generation) <
           std::tie(b.world, b.index, b.generation);
  }
};
void Budget(lua_State* state, lua_Debug*) {
  luaL_error(state, "Lua instruction budget exceeded");
}
struct ExecutionBudget {
  lua_State* state;
  bool installed;
  explicit ExecutionBudget(lua_State* s) : state(s), installed(lua_gethook(s)==nullptr) {
    if(installed) lua_sethook(s, Budget, LUA_MASKCOUNT, 200000);
  }
  ~ExecutionBudget() { if(installed) lua_sethook(state, nullptr, 0, 0); }
};
sol::object ToLua(sol::state& lua, const Json& value) {
  if (value.is_boolean()) return sol::make_object(lua, value.get<bool>());
  if (value.is_number_integer())
    return sol::make_object(lua, value.get<lua_Integer>());
  if (value.is_number()) return sol::make_object(lua, value.get<double>());
  if (value.is_string()) return sol::make_object(lua, value.get<std::string>());
  if (value.is_array() && value.size() == 3)
    return sol::make_object(
        lua, ecs::Vec3{value[0].get<float>(), value[1].get<float>(),
                       value[2].get<float>()});
  throw std::runtime_error("Unsupported Lua field value");
}
sol::object DataToLua(sol::state& lua,const Json& v,const Json* schema=nullptr){
 if(v.is_null())return sol::make_object(lua,sol::nil);
 if(v.is_primitive())return ToLua(lua,v);
 if(schema&&schema->at("type")=="Vec3")return sol::make_object(lua,ecs::Vec3{v[0].get<float>(),v[1].get<float>(),v[2].get<float>()});
 auto t=lua.create_table();if(v.is_array()){int i=1;for(auto& item:v)t[i++]=DataToLua(lua,item,schema&&schema->at("type")=="Array"?&schema->at("items"):schema);}else for(auto it=v.begin();it!=v.end();++it)t[it.key()]=DataToLua(lua,it.value(),schema?&schema->at("fields").at(it.key()):nullptr);return sol::make_object(lua,t);
}
bool ValidValue(const Json& field, const Json& value) {
  const auto type = field.at("type").get<std::string>();
  if (type == "entity") return ValidEntityReference(value);
  if (type == "bool") return value.is_boolean();
  if (type == "string" || type == "asset")
    return value.is_string() &&
           value.get_ref<const std::string&>().size() <= 1024;
  auto number = [&](const Json& v) {
    return v.is_number() && std::isfinite(v.get<double>()) &&
           v.get<double>() >= field.value("min", -1e6) &&
           v.get<double>() <= field.value("max", 1e6);
  };
  if (type == "integer") return value.is_number_integer() && number(value);
  if (type == "number") return number(value);
  if (type == "vec3")
    return value.is_array() && value.size() == 3 && number(value[0]) &&
           number(value[1]) && number(value[2]);
  return false;
}
Json Schema(sol::table definition) {
  Json schema = Json::object();
  sol::object fields = definition["fields"];
  if (!fields.valid() || fields == sol::nil)
    fields = sol::make_object(definition.lua_state(),
                              sol::table::create(definition.lua_state()));
  if (fields.get_type() != sol::type::table)
    throw std::runtime_error("fields must be a table");
  for (auto item : fields.as<sol::table>()) {
    if (item.first.get_type() != sol::type::string ||
        item.second.get_type() != sol::type::table)
      throw std::runtime_error("fields must map names to schemas");
    auto key = item.first.as<std::string>();
    auto f = item.second.as<sol::table>();
    if (key.empty() || key == "entity" || key.size() > 128 ||
        schema.size() >= 128)
      throw std::runtime_error("Invalid/oversized field schema");
    sol::object typeObject = f["type"];
    if (typeObject.get_type() != sol::type::string)
      throw std::runtime_error("Field type must be a string: " + key);
    Json field = {{"type", typeObject.as<std::string>()}, {"label", key}};
    sol::object label = f["label"];
    if (label.valid() && label != sol::nil) {
      if (label.get_type() != sol::type::string)
        throw std::runtime_error("Field label must be a string");
      field["label"] = label.as<std::string>();
    }
    for (const char* bound : {"min", "max"}) {
      sol::object b = f[bound];
      if (b.valid() && b != sol::nil) {
        if (b.get_type() != sol::type::number)
          throw std::runtime_error("Field bounds must be numeric");
        auto value = b.as<double>();
        if (!std::isfinite(value))
          throw std::runtime_error("Nonfinite field bound");
        field[bound] = value;
      }
    }
    sol::object d = f["default"];
    const auto type = field["type"].get<std::string>();
    if (type == "entity") {
      if (d.valid() && d != sol::nil)
        throw std::runtime_error(
            "Entity default must be nil; assign a scene object in Inspector");
      field["default"] = nullptr;
    } else if (type == "bool") {
      if (d.get_type() != sol::type::boolean)
        throw std::runtime_error("Boolean default required");
      field["default"] = d.as<bool>();
    } else if (type == "integer") {
      if (!d.is<lua_Integer>())
        throw std::runtime_error("Integer default required");
      field["default"] = d.as<lua_Integer>();
    } else if (type == "number") {
      if (d.get_type() != sol::type::number)
        throw std::runtime_error("Numeric default required");
      field["default"] = d.as<double>();
    } else if (type == "string" || type == "asset") {
      if (d.get_type() != sol::type::string)
        throw std::runtime_error("String default required");
      field["default"] = d.as<std::string>();
    } else if (type == "vec3") {
      if (d.get_type() != sol::type::table)
        throw std::runtime_error("Vec3 default must be a table");
      auto v = d.as<sol::table>();
      auto values = Json::array();
      for (int index = 1; index <= 3; ++index) {
        sol::object x = v[index];
        if (x.get_type() != sol::type::number)
          throw std::runtime_error("Vec3 default must contain three numbers");
        values.push_back(x.as<double>());
      }
      field["default"] = values;
    } else
      throw std::runtime_error("Unknown field type: " + type);
    if (!ValidValue(field, field["default"]))
      throw std::runtime_error("Invalid default: " + key);
    schema[key] = field;
  }
  for (const char* callback :
       {"on_start", "on_update", "on_fixed_update", "on_event", "on_ui_event",
        "on_stop", "on_destroy"}) {
    sol::object fn = definition[callback];
    if (fn.valid() && fn != sol::nil && fn.get_type() != sol::type::function)
      throw std::runtime_error(std::string(callback) + " must be a function");
  }
  return schema;
}
}  // namespace
struct LuaRuntime::Impl {
  // State is declared first, so all Lua references die before the VM.
  sol::state lua;
  PostFxRuntimeOverrides postfx;
  LuaRuntime* facade = nullptr;
  struct Module { std::string source, bytecode; };
  struct ModuleContext {
    sol::environment env;
    std::shared_ptr<const std::map<std::string,Module>> snapshot;
    std::map<std::string,Module> modules;
    std::map<std::string,sol::table> values;
    std::vector<std::string> stack;
    std::size_t bytes=0;
    bool collecting=false;
  };
  std::map<std::string,Module> codeCache;
  std::map<std::string,std::string> watches;
  std::map<std::string,std::set<std::string>> rootDependencies;
  std::vector<std::set<std::string>> failedGroups;
  double pollTime=0;
  struct Program {
    std::string source;
    Json fields;
    std::uint64_t revision = 0;
    std::shared_ptr<const std::map<std::string,Module>> modules;
  };
  struct Instance {
    std::string path;
    std::uint64_t revision;
    sol::environment env;
    sol::table definition, self;
    Json overrides;
    bool failed = false;
    ecs::Entity entity{};
    std::uint64_t serial = 0;
    std::string lastCallback, lastError;
    EventStatus lastStatus = EventStatus::Skipped;
    std::shared_ptr<ModuleContext> modules;
  };
  std::map<std::string, Program> programs;
  std::map<std::string, std::string> errors;
  std::map<ecs::Entity, Instance, EntityLess> instances;
  struct Rejected {
    std::string fingerprint, error;
  };
  std::map<ecs::Entity, Rejected, EntityLess> rejected;
  std::set<std::string> loadFailures;
  std::uint64_t nextInstanceSerial = 1;
  struct Event {
    ecs::Entity target, other;
    std::string type;
    float amount = 0;
    ecs::Entity source{};
    std::uint64_t trace = 0;
    std::string kind = "Lua";
    std::optional<ui::PointerEvent> uiEvent;
  };
  GameplayEventHistory history;
  std::vector<Event> events;
  struct Timer {
    ecs::Entity owner;
    double deadline, interval;
    std::string event;
  };
  std::map<lua_Integer, Timer> timers;
  lua_Integer nextTimer = 1;
  double gameTime = 0;
  ecs::MutationQueue mutations;
  std::set<ecs::Entity, EntityLess> destroying;
  ecs::ParticleSystem particles;
  ecs::World* world = nullptr;
  std::function<bool(const std::string&)> input;
  GameplayServices services;
  ecs::Entity cameraOwner{}, cursorOwner{}, uiInputOwner{};
  bool fixedPhase = false;
  std::map<std::string, bool> held, pressed, released;
  std::map<std::string, bool> fixedPressed, fixedReleased;
  std::map<std::string, std::string> boundKeys;
  const std::thread::id owner = std::this_thread::get_id();
  bool stopping = false;
  Impl() {
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string,
                       sol::lib::table, sol::lib::utf8);
    lua.new_usertype<ecs::Entity>("Entity", sol::no_constructor);
    auto canvas =
        lua.new_usertype<ui::CanvasHandle>("CanvasHandle", sol::no_constructor);
    canvas["play_animation"] = [this](const ui::CanvasHandle& h,
                                      const std::string& id,
                                      sol::optional<sol::table> options) {
      bool restart = true;
      float speed = 1;
      if (options) {
        restart = options->get_or("restart", true);
        speed = options->get_or("speed", 1.f);
      }
      UI().Play(h, id, restart, speed);
    };
    canvas["stop_animation"] = [this](const ui::CanvasHandle& h,
                                      const std::string& id) {
      UI().Stop(h, id);
    };
    canvas["is_animation_playing"] = [this](const ui::CanvasHandle& h,
                                            const std::string& id) {
      return UI().Playing(h, id);
    };
    canvas["valid"] = [this](const ui::CanvasHandle& h) {
      return world && services.runtimeUI && services.runtimeUI->Valid(h);
    };
    canvas["set_text"] = [this](const ui::CanvasHandle& h,
                                const std::string& id, const std::string& v) {
      UI().Set(h, id, "text", v);
    };
    canvas["set_visible"] = [this](const ui::CanvasHandle& h,
                                   const std::string& id,
                                   bool v) { UI().Set(h, id, "visible", v); };
    canvas["set_opacity"] = [this](const ui::CanvasHandle& h,
                                   const std::string& id,
                                   float v) { UI().Set(h, id, "opacity", v); };
    canvas["set_progress"] = [this](const ui::CanvasHandle& h,
                                    const std::string& id, float v) {
      UI().Set(h, id, "progress", v);
    };
    canvas["set_image"] = [this](const ui::CanvasHandle& h,
                                 const std::string& id, const std::string& v) {
      UI().Set(h, id, "image", v);
    };
    canvas["set_enabled"] = [this](const ui::CanvasHandle& h,
                                   const std::string& id,
                                   bool v) { UI().Set(h, id, "enabled", v); };
    canvas["set_color"] = [this](const ui::CanvasHandle& h,
                                 const std::string& id, sol::table color) {
      Json v = Json::array();
      if (color.size() != 4)
        throw std::runtime_error("UI Color requires four channels");
      for (int n = 1; n <= 4; ++n) {
        sol::object c = color[n];
        if (c.get_type() != sol::type::number)
          throw std::runtime_error("UI Color channel must be numeric");
        v.push_back(c.as<double>());
      }
      UI().Set(h, id, "color", v);
    };
    canvas["get_text"] = [this](const ui::CanvasHandle& h,
                                const std::string& id) {
      auto& w = UI().Read(h, id);
      if (w.type != "Text")
        throw std::runtime_error("UI: expected Text widget");
      return w.text;
    };
    canvas["get_progress"] = [this](const ui::CanvasHandle& h,
                                    const std::string& id) {
      auto& w = UI().Read(h, id);
      if (w.type != "ProgressBar")
        throw std::runtime_error("UI: expected ProgressBar");
      return w.value;
    };
    canvas["get_visible"] = [this](const ui::CanvasHandle& h,
                                   const std::string& id) {
      return UI().Read(h, id).visible;
    };
    canvas["get_opacity"] = [this](const ui::CanvasHandle& h,
                                   const std::string& id) {
      return UI().Read(h, id).opacity;
    };
    canvas["get_image"] = [this](const ui::CanvasHandle& h,
                                 const std::string& id) {
      auto& w = UI().Read(h, id);
      if (w.type != "Image") throw std::runtime_error("UI: expected Image");
      return w.texture;
    };
    canvas["get_enabled"] = [this](const ui::CanvasHandle& h,
                                   const std::string& id) {
      auto& w = UI().Read(h, id);
      if (w.type != "Button") throw std::runtime_error("UI: expected Button");
      return w.enabled;
    };
    canvas["get_color"] = [this](const ui::CanvasHandle& h,
                                 const std::string& id) {
      auto c = UI().Read(h, id).color;
      return std::make_tuple(c[0], c[1], c[2], c[3]);
    };
    lua.new_usertype<ecs::Vec3>(
        "Vec3",
        sol::constructors<ecs::Vec3(), ecs::Vec3(float, float, float)>(), "x",
        &ecs::Vec3::x, "y", &ecs::Vec3::y, "z", &ecs::Vec3::z);
  }
  ui::RuntimeUI& UI() {
    if (!world || !services.runtimeUI)
      throw std::runtime_error("UI requires an active runtime Canvas host");
    return *services.runtimeUI;
  }
  void Check() const {
    if (owner != std::this_thread::get_id())
      throw std::logic_error("Lua VM belongs to its owner thread");
  }
  void Fail(const std::string& path, const std::string& callback,
            const std::string& message) {
    errors[path] = callback + ": " + message;
    if (callback == "load")
      loadFailures.insert(path);
    else
      loadFailures.erase(path);
    // Lua's diagnostic format provides a real source line when present. Never infer it from arbitrary logs.
    int line=0;std::string source=path;
    auto colon=message.find(":");
    if(colon!=std::string::npos){auto end=message.find(":",colon+1);if(end!=std::string::npos&&end>colon+1){auto number=message.substr(colon+1,end-colon-1);if(number.size()<9&&std::all_of(number.begin(),number.end(),[](char c){return c>='0'&&c<='9';})){line=std::stoi(number);source=message.substr(0,colon);}}}
    Logger::Get().Diagnostic("ERROR",source,callback,"Lua [" + path + "] " + errors[path],line,message);
  }
  sol::environment Environment() {
    sol::environment env(lua, sol::create);
    // Explicit allowlist: no
    // package/io/os/debug/load/dofile/rawset/metatables/pcall.
    for (const char* key : {"assert", "error", "ipairs", "pairs", "next",
                            "select", "tonumber", "tostring", "type"})
      env[key] = lua[key];
    sol::table vector = lua.create_table();
    vector["new"] = lua["Vec3"]["new"];
    env["Vec3"] = vector;
    for (const char* key : {"math", "string", "table", "utf8"}) {
      sol::table copy = lua.create_table();
      sol::table original = lua[key];
      for (auto item : original) copy[item.first] = item.second;
      env[key] = copy;
    }
    return env;
  }
  #include "LuaModules.inl"
  sol::table Evaluate(const std::string& source, const std::string& path,
                      sol::environment& env, std::shared_ptr<ModuleContext> context={}, bool binary=false) {
    if(context&&!binary) InstallImports(env,context);
    ExecutionBudget budget(lua.lua_state());
    auto loaded = lua.load(source, "@" + path, binary?sol::load_mode::binary:sol::load_mode::text);
    if (!loaded.valid()) {
      sol::error error = loaded;
      throw std::runtime_error(error.what());
    }
    sol::protected_function fn = loaded;
    sol::set_environment(env, fn);
    auto result = fn();
    if (!result.valid()) {
      sol::error error = result;
      throw std::runtime_error(error.what());
    }
    sol::object value = result;
    if (value.get_type() != sol::type::table)
      throw std::runtime_error(binary?"Module must return a table":"Script must return a behaviour table");
    return value.as<sol::table>();
  }
  bool Load(const std::string& path) { return ReloadGraph(path); }
  bool Alive(ecs::Entity e) const {
    return world && world->IsAlive(e) && !destroying.contains(e);
  }
  lua_Integer Schedule(ecs::Entity owner, double delay,
                       const std::string& event, bool repeat) {
    auto instance = instances.find(owner);
    if (stopping || !Alive(owner) || instance == instances.end() ||
        instance->second.failed)
      throw std::runtime_error("Timer owner is not an active script instance");
    if (!std::isfinite(delay) || delay < 0 || delay > 1e6 ||
        (repeat && delay == 0) || event.empty() || event.size() > 128)
      throw std::runtime_error(
          "Timer requires bounded delay (every > 0) and event name (1..128 "
          "characters)");
    if (timers.size() >= 1024 ||
        std::count_if(timers.begin(), timers.end(),
                      [&](const auto& t) { return t.second.owner == owner; }) >=
            128 ||
        nextTimer == std::numeric_limits<lua_Integer>::max())
      throw std::runtime_error("Timer budget exceeded");
    auto id = nextTimer++;
    timers.emplace(id,
                   Timer{owner, gameTime + delay, repeat ? delay : 0, event});
    return id;
  }
  void CancelTimers(ecs::Entity owner) {
    std::erase_if(timers,
                  [&](const auto& t) { return t.second.owner == owner; });
  }
  void FireTimers() {
    std::vector<lua_Integer> due;
    for (auto& [id, t] : timers)
      if (t.deadline <= gameTime) due.push_back(id);
    for (auto id : due) {
      auto at = timers.find(id);
      if (at == timers.end()) continue;
      auto timer = at->second;
      auto instance = instances.find(timer.owner);
      if (!Alive(timer.owner) || instance == instances.end() ||
          instance->second.failed) {
        timers.erase(at);
        continue;
      }
      // At most one delivery per repeating timer per Update; skip missed
      // periods without building an unbounded catch-up event queue.
      if (timer.interval > 0)
        at->second.deadline =
            timer.deadline +
            (std::floor((gameTime - timer.deadline) / timer.interval) + 1) *
                timer.interval;
      else
        timers.erase(at);
      auto data = lua.create_table();
      data["type"] = timer.event;
      data["amount"] = 0;
      data["timer"] = id;
      data["source"] = timer.owner;
      data["other"] = ecs::Entity{};
      Event event{timer.owner, {}, timer.event, 0, timer.owner, 0, "Timer"};
      if (history.Active())
        event.trace = Trace(event, "; timer = " + std::to_string(id));
      Deliver(event, data);
    }
  }
  EventEntity Snapshot(ecs::Entity e) const {
    EventEntity out;
    out.entity = e;
    if (world && world->IsAlive(e)) {
      if (auto* t = world->GetComponent<ecs::TagComponent>(e))
        out.name = t->name;
      if (auto* id = world->GetComponent<ecs::SceneIdentityComponent>(e))
        out.stableId = id->value;
    }
    return out;
  }
  std::uint64_t Trace(const Event& event, const std::string& extra = {}) {
    if (!history.Active()) return 0;
    GameplayEvent row;
    row.time = gameTime;
    row.name = event.type;
    row.kind = event.kind;
    row.handler = event.uiEvent ? "on_ui_event" : "on_event";
    if (event.uiEvent) {
      row.canvas = event.uiEvent->canvas.document;
      row.widget = event.uiEvent->widget;
    }
    row.source = Snapshot(event.source);
    row.target = Snapshot(event.target);
    row.other = Snapshot(event.other);
    row.payload = "amount = " + std::to_string(event.amount) + extra;
    if (event.uiEvent)
      row.payload += "; canvas = " + event.uiEvent->canvas.document +
                     "; widget = " + event.uiEvent->widget +
                     "; animation = " + event.uiEvent->animation;
    return history.Add(std::move(row));
  }
  void Enqueue(Event event) {
    if (history.Active()) event.trace = Trace(event);
    events.push_back(std::move(event));
  }
  void Deliver(const Event& event, sol::table data) {
    auto id = event.trace;
    if (history.Active() && (!id || !history.Find(id))) id = Trace(event);
    auto i = instances.find(event.target);
    if (!Alive(event.target) || i == instances.end()) {
      if (history.Active())
        history.Update(
            id, EventStatus::Skipped, gameTime, {},
            "Target destroyed, disabled, or has no active script instance");
      return;
    }
    if (!history.Active()) {
      Call(i->second, event.uiEvent ? "on_ui_event" : "on_event", data);
      return;
    }
    const auto result = CallObserved(
        id, i->second, event.uiEvent ? "on_ui_event" : "on_event", data);
    history.Update(id, result, gameTime, i->second.path,
                   result == EventStatus::Error ? errors[i->second.path]
                   : result == EventStatus::Skipped
                       ? "Handler absent or script instance already failed"
                       : "");
  }
  ecs::TransformComponent& Transform(ecs::Entity e) {
    if (!Alive(e)) throw std::runtime_error("Dead/stale/foreign entity");
    auto* t = world->GetComponent<ecs::TransformComponent>(e);
    if (!t) throw std::runtime_error("Entity has no Transform");
    return *t;
  }
  void SetTransform(ecs::Entity e, const char* field, ecs::Vec3 value) {
    Transform(e);
    auto t = world->WorldTransform(e);
    if (std::string(field) == "position")
      t.position = value;
    else if (std::string(field) == "rotation")
      t.rotation = value;
    else
      t.scale = value;
    std::string error;
    if (!world->SetWorldTransform(e, t, &error))
      throw std::runtime_error("World.set_" + std::string(field) + ": " +
                               error);
  }
  std::string LightComponent(ecs::Entity e) {
    if (!Alive(e)) throw std::runtime_error("Light: dead/stale/foreign entity");
    if (world->HasComponent<ecs::DirectionalLightComponent>(e))
      return "DirectionalLight";
    if (world->HasComponent<ecs::PointLightComponent>(e)) return "PointLight";
    if (world->HasComponent<ecs::SpotLightComponent>(e)) return "SpotLight";
    throw std::runtime_error(
        "Light: entity has no Directional/Point/Spot Light component");
  }
  bool InputAllowed(ecs::Entity owner) const {
    auto* c = Alive(owner) ? world->GetComponent<ecs::LuaScriptComponent>(owner)
                           : nullptr;
    return !stopping && c && c->enabled && c->inputEnabled &&
           (!services.inputAllowed || services.inputAllowed());
  }
  bool MotionAllowed(ecs::Entity owner) const {
    auto* c = Alive(owner) ? world->GetComponent<ecs::LuaScriptComponent>(owner)
                           : nullptr;
    return !stopping && c && c->enabled &&
           (!c->inputEnabled || !services.inputAllowed ||
            services.inputAllowed());
  }
  ecs::CharacterMovementComponent& Movement(ecs::Entity owner, ecs::Entity e) {
    if (!Alive(e))
      throw std::runtime_error("Character: dead/stale/foreign entity");
    if (owner != e)
      throw std::runtime_error(
          "Character: a script controls only its own entity");
    auto* m = world->GetComponent<ecs::CharacterMovementComponent>(e);
    if (!m || !world->HasComponent<ecs::RigidbodyComponent>(e) ||
        !world->HasComponent<ecs::ColliderComponent>(e) ||
        !world->HasComponent<ecs::TransformComponent>(e))
      throw std::runtime_error(
          "Character requires CharacterMovement, Rigidbody, Collider and "
          "Transform");
    return *m;
  }
  static void Vector(ecs::Vec3 v) {
    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) ||
        std::max({std::abs(v.x), std::abs(v.y), std::abs(v.z)}) > 1e6f)
      throw std::runtime_error("Expected finite bounded vector");
  }
  static void Number(float n, float max) {
    if (!std::isfinite(n) || n < 0 || n > max)
      throw std::runtime_error("Gameplay parameter outside valid range");
  }
  void ClearControl(ecs::Entity e) {
    if (world && world->IsAlive(e))
      if (auto* m = world->GetComponent<ecs::CharacterMovementComponent>(e)) {
        m->desiredDirection = {};
        m->jumpRequested = false;
        if (auto* b = world->GetComponent<ecs::RigidbodyComponent>(e)) {
          b->velocity.x = 0;
          b->velocity.z = 0;
        }
      }
    if (uiInputOwner == e) {
      if (services.resetUIInput) services.resetUIInput();
      uiInputOwner = {};
    }
    if (cameraOwner == e) {
      if (services.camera) services.camera->following = false;
      cameraOwner = {};
    }
    if (cursorOwner == e) {
      if (services.captureCursor) services.captureCursor(false);
      cursorOwner = {};
    }
  }
  bool AcquireCamera(ecs::Entity e) {
    auto* c =
        Alive(e) ? world->GetComponent<ecs::LuaScriptComponent>(e) : nullptr;
    if (stopping || !c || !c->enabled || !c->cameraEnabled || !services.camera)
      return false;
    if (cameraOwner.IsValid() && Alive(cameraOwner) && cameraOwner != e)
      return false;
    cameraOwner = e;
    return true;
  }
  GameplayCamera& Camera(ecs::Entity e) {
    if (!AcquireCamera(e))
      throw std::runtime_error(
          "Camera permission missing or camera owned by another script");
    return *services.camera;
  }
  void BindAction(ecs::Entity owner, const std::string& action,
                  const std::string& key) {
    auto* c = Alive(owner) ? world->GetComponent<ecs::LuaScriptComponent>(owner)
                           : nullptr;
    if (stopping || !c || !c->inputEnabled || !services.bindAction ||
        !services.bindAction(action, key))
      throw std::runtime_error(
          "Input.bind: permission missing, unknown key name or action budget "
          "exceeded");
    if (!boundKeys.contains(action) || boundKeys[action] != key) {
      boundKeys[action] = key;
      held.erase(action);
      pressed.erase(action);
      released.erase(action);
      fixedPressed.erase(action);
      fixedReleased.erase(action);
    }
    DiscoverInput(action);
  }
  void Bind(sol::environment& env, ecs::Entity owner) {
    auto api = lua.create_table();
    api.set_function("is_alive", [this](ecs::Entity e) { return Alive(e); });
    api.set_function("find_by_tag", [this](const std::string& tag) {
      auto list = lua.create_table();
      int i = 1;
      for (auto e : world->Entities())
        if (auto* t = world->GetComponent<ecs::TagComponent>(e);
            Alive(e) && ((t && t->name == tag) || (tag == "Player" && world->HasComponent<ecs::PlayerComponent>(e))))
          list[i++] = e;
      return list;
    });
    api.set_function("get_position", [this](ecs::Entity e) {
      Transform(e);
      return world->WorldTransform(e).position;
    });
    api.set_function("get_rotation", [this](ecs::Entity e) {
      Transform(e);
      return world->WorldTransform(e).rotation;
    });
    api.set_function("get_scale", [this](ecs::Entity e) {
      Transform(e);
      return world->WorldTransform(e).scale;
    });
    api.set_function("find_in_radius", [this](ecs::Vec3 center, float radius) {
      auto list = lua.create_table();
      int index = 1;
      for (auto e : ecs::FindInRadius(*world, center, radius))
        if (Alive(e)) list[index++] = e;
      return list;
    });
    api.set_function("set_position", [this](ecs::Entity e, ecs::Vec3 p) {
      if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
        throw std::runtime_error("Nonfinite position");
      SetTransform(e, "position", p);
    });
    api.set_function("set_rotation", [this](ecs::Entity e, ecs::Vec3 v) {
      SetTransform(e, "rotation", v);
    });
    api.set_function("set_scale", [this](ecs::Entity e, ecs::Vec3 v) {
      SetTransform(e, "scale", v);
    });
    api.set_function("get_local_position",
                     [this](ecs::Entity e) { return Transform(e).position; });
    api.set_function("get_local_rotation",
                     [this](ecs::Entity e) { return Transform(e).rotation; });
    api.set_function("get_local_scale",
                     [this](ecs::Entity e) { return Transform(e).scale; });
    api.set_function("get_world_transform", [this](ecs::Entity e) {
      Transform(e);
      auto t = world->WorldTransform(e);
      auto value = lua.create_table();
      value["position"] = t.position;
      value["rotation"] = t.rotation;
      value["scale"] = t.scale;
      return value;
    });
    for (const char* field : {"position", "rotation", "scale"})
      api.set_function(
          std::string("set_local_") + field,
          [this, key = std::string(field)](ecs::Entity e, ecs::Vec3 value) {
            Transform(e);
            std::string error;
            if (!ecs::ComponentRegistry::Builtins().Set(
                    *world, e, ecs::ComponentId("Transform"), key, value,
                    &error))
              throw std::runtime_error(error);
          });
    api.set_function("get_parent", [this](ecs::Entity e) {
      if (!Alive(e)) throw std::runtime_error("Dead/stale/foreign entity");
      auto parent = world->Parent(e);
      return parent.IsValid() ? sol::make_object(lua, parent)
                              : sol::make_object(lua, sol::nil);
    });
    api.set_function("set_parent",
                     [this](ecs::Entity e, sol::optional<ecs::Entity> parent,
                            sol::optional<bool> keep) {
                       std::string error;
                       if (!world->SetParent(e, parent.value_or(ecs::Entity{}),
                                             keep.value_or(true), &error))
                         throw std::runtime_error(error);
                     });
    api.set_function("spawn_prefab",
                     [this](const std::string& key, ecs::Vec3 p) {
                       if (stopping)
                         throw std::runtime_error("Spawn during Stop");
                       if (world->GetAliveCount() > 10000)
                         throw std::runtime_error("Lua entity budget exceeded");
                       return SpawnPrefab(*world, key, p);
                     });
    api.set_function("destroy", [this](ecs::Entity e) {
      if (Alive(e)) {
        destroying.insert(e);
        mutations.Destroy(e);
      }
    });
    api.set_function(
        "send_event",
        [this, owner](ecs::Entity e, const std::string& type, float amount) {
          if (Alive(e) && events.size() < 1024 && type.size() < 128 &&
              std::isfinite(amount))
            Enqueue({e, {}, type, amount, owner});
        });
    api.set_function("load_scene",[this,owner](const std::string& key){
      if(stopping||!Alive(owner)||!services.loadScene)
        throw std::runtime_error("World.load_scene requires active Play");
      return services.loadScene(key);
    });
    env["World"] = api;
    auto vfx=lua.create_table();
    auto particleEntity=[this](ecs::Entity e){if(stopping||!Alive(e))throw std::runtime_error("Particles: dead/foreign Entity or stopping session");};
    vfx.set_function("play",[this,particleEntity](ecs::Entity e){particleEntity(e);particles.Play(*world,e);});
    vfx.set_function("stop",[this,particleEntity](ecs::Entity e){particleEntity(e);particles.Stop(*world,e);});
    vfx.set_function("reset",[this,particleEntity](ecs::Entity e){particleEntity(e);particles.Reset(*world,e);});
    vfx.set_function("emit",[this,particleEntity](ecs::Entity e,sol::object count){particleEntity(e);if(count.get_type()!=sol::type::number)throw std::runtime_error("Particles.emit count must be an integer in [0,10000]");double n=count.as<double>();if(!std::isfinite(n)||n<0||n>10000||std::floor(n)!=n)throw std::runtime_error("Particles.emit count must be an integer in [0,10000]");particles.Emit(*world,e,int(n));});
    vfx.set_function("is_playing",[this,particleEntity](ecs::Entity e){particleEntity(e);return particles.IsPlaying(*world,e);});
    env["Particles"]=vfx;
    auto physics = lua.create_table();
    physics.set_function(
        "raycast",
        [this](ecs::Vec3 origin, ecs::Vec3 direction, float distance,
               sol::optional<ecs::Entity> ignore) -> sol::object {
          auto hit = ecs::Raycast(*world, origin, direction, distance,
                                  ignore.value_or(ecs::Entity{}),
                                  [this](auto e) { return Alive(e); });
          if (!hit) return sol::make_object(lua, sol::nil);
          auto result = lua.create_table();
          result["entity"] = hit->entity;
          result["position"] = hit->position;
          result["normal"] = hit->normal;
          result["distance"] = hit->distance;
          return sol::make_object(lua, result);
        });
    env["Physics"] = physics;
    auto data=lua.create_table();
    auto readData=[this](const std::string& key){if(stopping||!services.postfxResources)throw std::runtime_error("Data requires resource host");auto r=services.postfxResources->Load<DataResource>(key);services.postfxResources->WatchForHotReload<DataResource>(key);if(!r||!r->GetData().document)throw std::runtime_error("Data load failed: "+key);return r->GetData().document;};
    data.set_function("load",[this,readData](const std::string& key){auto d=readData(key);if(d->at("kind")!="DataAsset")throw std::runtime_error("Data.load expects DataAsset");return DataToLua(lua,d->at("value"),&d->at("schema"));});
    data.set_function("rows",[this,readData](const std::string& key){auto d=readData(key);if(d->at("kind")!="DataTable")throw std::runtime_error("Data.rows expects DataTable");return DataToLua(lua,d->at("rows"),&d->at("schema"));});
    data.set_function("find",[this,readData](const std::string& key,const std::string& id)->sol::object{auto d=readData(key);if(d->at("kind")!="DataTable")throw std::runtime_error("Data.find expects DataTable");for(auto& row:d->at("rows"))if(row.at("id")==id)return DataToLua(lua,row,&d->at("schema"));return sol::make_object(lua,sol::nil);});env["Data"]=data;
    auto navigation=lua.create_table();navigation.set_function("find_path",[this](ecs::Vec3 start,ecs::Vec3 goal,ecs::Vec3 minimum,ecs::Vec3 maximum,float cell,float radius,sol::optional<ecs::Entity> ignore){auto path=ecs::FindGridPath(*world,start,goal,minimum,maximum,cell,radius,ignore.value_or(ecs::Entity{}));auto t=lua.create_table();int i=1;for(auto p:path)t[i++]=p;return t;});env["Navigation"]=navigation;
    auto timer = lua.create_table();
    timer.set_function("after",
                       [this, owner](double delay, const std::string& event) {
                         return Schedule(owner, delay, event, false);
                       });
    timer.set_function("every",
                       [this, owner](double delay, const std::string& event) {
                         return Schedule(owner, delay, event, true);
                       });
    timer.set_function("cancel", [this, owner](lua_Integer id) {
      auto at = timers.find(id);
      if (at == timers.end() || at->second.owner != owner) return false;
      timers.erase(at);
      return true;
    });
    env["Timer"] = timer;
    auto light = lua.create_table();
    light.set_function("get_intensity", [this](ecs::Entity e) {
      ecs::PropertyValue value;
      std::string error;
      auto key = LightComponent(e);
      if (!ecs::ComponentRegistry::Builtins().Get(*world, e,
                                                  ecs::ComponentId(key.c_str()),
                                                  "intensity", value, &error))
        throw std::runtime_error(error);
      return std::get<float>(value);
    });
    light.set_function("set_intensity", [this](ecs::Entity e, float value) {
      std::string error;
      auto key = LightComponent(e);
      if (!ecs::ComponentRegistry::Builtins().Set(*world, e,
                                                  ecs::ComponentId(key.c_str()),
                                                  "intensity", value, &error))
        throw std::runtime_error("Light.set_intensity: " + error);
    });
    env["Light"] = light;
    auto postfxApi = lua.create_table();
    postfxApi.set_function(
        "set", [this](const std::string& id, const std::string& parameter,
                      sol::object value) {
          Json j;
          if (value.get_type() == sol::type::boolean)
            j = value.as<bool>();
          else if (value.get_type() == sol::type::number) {
            double number = value.as<double>();
            if (std::isfinite(number) && std::floor(number) == number &&
                std::abs(number) <= 1000000)
              j = int(number);
            else
              j = number;
          } else if (value.get_type() == sol::type::table) {
            j = Json::array();
            auto table = value.as<sol::table>();
            if (table.size() < 2 || table.size() > 4)
              return facade->SetPostFx(id, parameter, Json());
            for (size_t i = 1; i <= table.size(); ++i) {
              sol::object item = table[i];
              if (item.get_type() != sol::type::number)
                return facade->SetPostFx(id, parameter, Json());
              j.push_back(item.as<double>());
            }
          } else
            return facade->SetPostFx(id, parameter, Json());
          return facade->SetPostFx(id, parameter, j);
        });
    postfxApi.set_function("set_weight",
                           [this](const std::string& id, float weight) {
                             return facade->SetPostFxWeight(id, weight);
                           });
    postfxApi.set_function("clear", [this](const std::string& id) {
      return facade->ClearPostFx(id);
    });
    env["postfx"] = postfxApi;
    auto uiApi = lua.create_table();
    uiApi.set_function(
        "get", sol::overload(
                   [this](const std::string& name) { return UI().Get(name); },
                   [this](ecs::Entity e) {
                     if (!Alive(e))
                       throw std::runtime_error("UI: stale Canvas owner");
                     return UI().Get(e);
                   }));
    uiApi.set_function(
        "set_input_mode", [this, owner](const std::string& mode) {
          if (!Alive(owner) || !services.setUIInputMode)
            throw std::runtime_error("UI input host unavailable");
          services.setUIInputMode(mode);
          uiInputOwner = mode == "GameOnly" ? ecs::Entity{} : owner;
          // A menu may be opened before the FPS script's first capture click.
          // Hand the existing capture permission to its current camera owner.
          if (mode == "GameOnly" && !Alive(cursorOwner) && Alive(cameraOwner) &&
              InputAllowed(cameraOwner) && services.cursorCaptured &&
              services.cursorCaptured())
            cursorOwner = cameraOwner;
        });
    uiApi.set_function("get_input_mode", [this]() {
      if (!world || !services.getUIInputMode)
        throw std::runtime_error("UI input host unavailable");
      return services.getUIInputMode();
    });
    env["ui"] = uiApi;
    auto log = lua.create_table();
    log.set_function(
        "info", [](const std::string& s) { Logger::Get().Info("Lua: " + s); });
    log.set_function("warning", [](const std::string& s) {
      Logger::Get().Warn("Lua: " + s);
    });
    log.set_function("error", [](const std::string& s) {
      Logger::Get().Error("Lua: " + s);
    });
    env["Log"] = log;
    auto controls = lua.create_table();
    controls.set_function("is_down", [this, owner](const std::string& s) {
      if (!InputAllowed(owner)) return false;
      DiscoverInput(s);
      return held[s];
    });
    controls.set_function("was_pressed", [this, owner](const std::string& s) {
      if (!InputAllowed(owner)) return false;
      DiscoverInput(s);
      return fixedPhase ? fixedPressed[s] : pressed[s];
    });
    controls.set_function("was_released", [this, owner](const std::string& s) {
      if (!InputAllowed(owner)) return false;
      DiscoverInput(s);
      return fixedPhase ? fixedReleased[s] : released[s];
    });
    controls["is_action_held"] = controls["is_down"];
    controls["is_action_pressed"] = controls["was_pressed"];
    controls["is_action_released"] = controls["was_released"];
    controls.set_function("bind", [this, owner](const std::string& action,
                                                const std::string& key) {
      BindAction(owner, action, key);
    });
    controls.set_function("mouse_delta", [this, owner]() {
      auto delta = InputAllowed(owner) && services.mouseDelta
                       ? services.mouseDelta()
                       : ecs::Vec3{};
      return std::make_tuple(delta.x, delta.y);
    });
    controls.set_function("cursor_captured", [this, owner]() {
      return InputAllowed(owner) && cursorOwner == owner &&
             services.cursorCaptured && services.cursorCaptured();
    });
    controls.set_function("capture_cursor", [this, owner](bool capture) {
      if (!capture) {
        if (cursorOwner == owner) {
          if (services.captureCursor) services.captureCursor(false);
          cursorOwner = {};
        }
        return true;
      }
      if (!InputAllowed(owner) || !services.captureCursor) return false;
      if (Alive(cursorOwner) && cursorOwner != owner &&
          services.cursorCaptured && services.cursorCaptured())
        return false;
      bool ok = services.captureCursor(true);
      if (ok) cursorOwner = owner;
      return ok;
    });
    controls.set_function("mouse_position",[this,owner](){auto p=InputAllowed(owner)&&services.mousePosition?services.mousePosition():ecs::Vec3{-1,-1,1};return std::make_tuple(p.x,p.y);});
    env["Input"] = controls;
    auto character = lua.create_table();
    character.set_function(
        "move_world", [this, owner](ecs::Entity e, ecs::Vec3 direction) {
          Vector(direction);
          auto& m = Movement(owner, e);
          if (!MotionAllowed(owner)) return;
          m.desiredDirection = {};
          float n = std::hypot(direction.x, direction.z);
          if (n > .0001f)
            m.desiredDirection = {direction.x / std::max(1.f, n), 0,
                                  direction.z / std::max(1.f, n)};
        });
    character.set_function("move_local", [this, owner](ecs::Entity e, float x,
                                                       float z) {
      Vector({x, 0, z});
      auto& m = Movement(owner, e);
      if (!MotionAllowed(owner)) return;
      float yaw = Transform(e).rotation.y, n = std::max(1.f, std::hypot(x, z));
      m.desiredDirection = {(std::cos(yaw) * x + std::sin(yaw) * z) / n, 0,
                            (-std::sin(yaw) * x + std::cos(yaw) * z) / n};
    });
    character.set_function("jump", [this, owner](ecs::Entity e) {
      auto& m = Movement(owner, e);
      if (MotionAllowed(owner)) m.jumpRequested = true;
    });
    character.set_function("stop", [this, owner](ecs::Entity e) {
      auto& m = Movement(owner, e);
      m.desiredDirection = {};
      m.jumpRequested = false;
    });
    character.set_function("is_grounded", [this, owner](ecs::Entity e) {
      return Movement(owner, e).grounded;
    });
    character.set_function("get_move_speed", [this, owner](ecs::Entity e) {
      return Movement(owner, e).moveSpeed;
    });
    character.set_function("get_jump_speed", [this, owner](ecs::Entity e) {
      return Movement(owner, e).jumpSpeed;
    });
    character.set_function("set_move_speed",
                           [this, owner](ecs::Entity e, float speed) {
                             Number(speed, 30);
                             Movement(owner, e).moveSpeed = speed;
                           });
    character.set_function("set_jump_speed",
                           [this, owner](ecs::Entity e, float speed) {
                             Number(speed, 20);
                             Movement(owner, e).jumpSpeed = speed;
                           });
    character.set_function("velocity", [this, owner](ecs::Entity e) {
      Movement(owner, e);
      auto v = world->GetComponent<ecs::RigidbodyComponent>(e)->velocity;
      return ecs::Vec3{v.x, 0, v.z};
    });
    character.set_function("eye_offset", [this, owner](ecs::Entity e) {
      Movement(owner, e);
      auto* p = world->GetComponent<ecs::PlayerComponent>(e);
      auto* c = world->GetComponent<ecs::ColliderComponent>(e);
      return ecs::Vec3{0,
                       (p ? p->eyeHeight : c->halfExtents.y * 2) -
                           c->halfExtents.y + c->offset.y,
                       0};
    });
    env["Character"] = character;
    auto camera = lua.create_table();
    // Selecting an image source is separate from the exclusive legacy control lease.
    auto selectionPermission=[this,owner](bool requireHost=true) {
      auto* c=Alive(owner)?world->GetComponent<ecs::LuaScriptComponent>(owner):nullptr;
      if(stopping||!c||!c->enabled||!c->cameraEnabled||(requireHost&&!services.camera))
        throw std::runtime_error("Camera selection permission missing");
    };
    // Logical pose, including inactive/disabled cameras. Selection and projection
    // do not affect spatial queries; interpolation belongs exclusively to render.
    auto spatial=[this,selectionPermission](ecs::Entity entity){
      selectionPermission(false);Transform(entity);
      if(!world->HasComponent<ecs::CameraComponent>(entity))throw std::runtime_error("Camera spatial query requires CameraComponent");
      return ecs::BuildCameraState(ecs::ResolveCameraPose(*world,entity),ecs::CameraComponent{},1.f,entity);
    };
    camera.set_function("screen_ray",[this,selectionPermission](ecs::Entity e,float x,float y,float aspect){selectionPermission(false);auto s=ecs::ResolveCameraState(*world,e,aspect);auto [o,d]=ecs::CameraScreenRay(s,x,y);auto t=lua.create_table();t["origin"]=o;t["direction"]=d;return t;});
    camera.set_function("pointer_ray",[this,owner,selectionPermission](ecs::Entity e)->sol::object{selectionPermission(false);auto p=InputAllowed(owner)&&services.mousePosition?services.mousePosition():ecs::Vec3{-1,-1,1};if(p.x<0||p.x>1||p.y<0||p.y>1)return sol::make_object(lua,sol::nil);auto [o,d]=ecs::CameraScreenRay(ecs::ResolveCameraState(*world,e,p.z),p.x,p.y);auto t=lua.create_table();t["origin"]=o;t["direction"]=d;return sol::make_object(lua,t);});
    camera.set_function("set_orthographic_size",[this,selectionPermission](ecs::Entity e,float size){selectionPermission(false);Transform(e);auto* c=world->GetComponent<ecs::CameraComponent>(e);if(!c)throw std::runtime_error("Camera component required");auto v=*c;v.orthographicSize=size;ecs::ValidateCamera(v);*c=v;});
    camera.set_function("get_position",[spatial](ecs::Entity e){return spatial(e).pose.position;});
    camera.set_function("get_forward",[spatial](ecs::Entity e){return spatial(e).forward;});
    camera.set_function("get_right",[spatial](ecs::Entity e){return spatial(e).right;});
    camera.set_function("get_up",[spatial](ecs::Entity e){return spatial(e).up;});
    camera.set_function("attach",[this,selectionPermission](ecs::Entity entity,ecs::Vec3 position,ecs::Vec3 rotation) {
      selectionPermission();Transform(entity);Vector(position);Vector(rotation);
      ecs::CameraAttachmentComponent value{true,position,rotation};
      if(!ecs::ValidCameraAttachment(value))throw std::runtime_error("Invalid Camera Attachment offsets");
      mutations.Enqueue(entity.world,[entity,value](ecs::World& w){
        if(!w.IsAlive(entity)||!w.HasComponent<ecs::TransformComponent>(entity))return;
        if(auto* current=w.GetComponent<ecs::CameraAttachmentComponent>(entity))*current=value;
        else w.AddComponent<ecs::CameraAttachmentComponent>(entity,value);
      });
    });
    camera.set_function("detach",[this,selectionPermission](ecs::Entity entity) {
      selectionPermission();Transform(entity);mutations.Remove<ecs::CameraAttachmentComponent>(entity);
    });
    camera.set_function("set_active",[this,selectionPermission](ecs::Entity entity) {
      selectionPermission();
      if(!services.camera->Select(*world,entity))throw std::runtime_error("Camera.set_active: dead, foreign, disabled or invalid Camera Entity");
    });
    camera.set_function("get_active",[this,selectionPermission]() -> sol::object {
      selectionPermission();auto e=services.camera->Active(*world);
      return e.IsValid()?sol::make_object(lua,e):sol::make_object(lua,sol::nil);
    });
    camera.set_function("is_active",[this,selectionPermission](ecs::Entity entity) {
      selectionPermission();return Alive(entity)&&services.camera->Active(*world)==entity;
    });
    camera.set_function("acquire",
                        [this, owner]() { return AcquireCamera(owner); });
    camera.set_function("release", [this, owner]() {
      if (cameraOwner == owner) {
        if (services.camera) services.camera->following = false;
        cameraOwner = {};
      }
    });
    camera.set_function("angles", [this, owner]() {
      auto& c = Camera(owner);
      return std::make_tuple(c.yaw, c.pitch);
    });
    camera.set_function("set_angles", [this, owner](float yaw, float pitch) {
      Vector({yaw, pitch, 0});
      auto& c = Camera(owner);
      if (MotionAllowed(owner)) {
        c.yaw = std::remainder(yaw, 6.283185307f);
        c.pitch = std::clamp(pitch, -ecs::PlayerMaximumPitch,
                             ecs::PlayerMaximumPitch);
      }
    });
    camera.set_function("rotate", [this, owner](float yaw, float pitch) {
      Vector({yaw, pitch, 0});
      auto& c = Camera(owner);
      if (MotionAllowed(owner)) {
        c.yaw = std::remainder(c.yaw + yaw, 6.283185307f);
        c.pitch = std::clamp(c.pitch + pitch, -ecs::PlayerMaximumPitch,
                             ecs::PlayerMaximumPitch);
      }
    });
    camera.set_function("forward", [this, owner]() {
      auto& c = Camera(owner);
      ecs::TransformComponent t;
      t.rotation = {c.pitch, c.yaw, 0};
      return ecs::PlayerLookDirection(t);
    });
    camera.set_function("flat_forward", [this, owner]() {
      auto& c = Camera(owner);
      return ecs::Vec3{std::sin(c.yaw), 0, std::cos(c.yaw)};
    });
    camera.set_function("right", [this, owner]() {
      auto& c = Camera(owner);
      return ecs::Vec3{std::cos(c.yaw), 0, -std::sin(c.yaw)};
    });
    camera.set_function("position",
                        [this, owner]() { return Camera(owner).position; });
    camera.set_function("set_position", [this, owner](ecs::Vec3 p) {
      Vector(p);
      auto& c = Camera(owner);
      if (MotionAllowed(owner)) {
        c.position = p;
        c.following = false;
      }
    });
    camera.set_function("follow",
                        [this, owner](ecs::Entity target, ecs::Vec3 offset) {
                          Transform(target);
                          Vector(offset);
                          auto& c = Camera(owner);
                          c.target = target;
                          c.offset = offset;
                          c.following = true;
                        });
    env["Camera"] = camera;
  }
  void DiscoverInput(const std::string& action) {
    if (action.empty() || action.size() > 128 ||
        (held.size() >= 128 && !held.contains(action)))
      throw std::runtime_error("Input action name/budget invalid");
    if (!held.contains(action)) {
      bool now = input && input(action);
      held[action] = now;
      pressed[action] = services.pressed ? services.pressed(action) : now;
      released[action] = services.released && services.released(action);
      fixedPressed[action] |= pressed[action];
      fixedReleased[action] |= released[action];
    }
  }
  template <class... Args>
  EventStatus Call(Instance& instance, const char* callback, Args&&... args) {
    return CallObserved(0, instance, callback, std::forward<Args>(args)...);
  }
  template <class... Args>
  EventStatus CallObserved(std::uint64_t trace, Instance& instance,
                           const char* callback, Args&&... args) {
    if (instance.failed && std::string_view(callback) != "on_destroy" &&
        std::string_view(callback) != "on_stop")
      return EventStatus::Skipped;
    try {
      sol::object fn = instance.definition[callback];
      if (!fn.valid() || fn == sol::nil) return EventStatus::Skipped;
      instance.lastCallback = callback;
      if (fn.get_type() != sol::type::function)
        throw std::runtime_error("Callback is no longer a function");
      if (trace)
        history.Update(trace, EventStatus::Processing, gameTime, instance.path);
      ExecutionBudget budget(lua.lua_state());
      sol::protected_function function = fn;
      auto result = function(instance.self, std::forward<Args>(args)...);
      if (!result.valid()) {
        sol::error error = result;
        instance.failed = true;
        instance.lastError = GameplayEventHistory::Bounded(
            std::string(callback) + ": " + error.what(), 4096);
        Fail(instance.path, callback, error.what());
      }
    } catch (const std::exception& error) {
      instance.failed = true;
      instance.lastError = GameplayEventHistory::Bounded(
          std::string(callback) + ": " + error.what(), 4096);
      Fail(instance.path, callback, error.what());
    }
    if (instance.failed) {
      CancelTimers(instance.entity);
      ClearControl(instance.entity);
    }
    instance.lastStatus =
        instance.failed ? EventStatus::Error : EventStatus::Success;
    return instance.lastStatus;
  }
  void Retire(ecs::Entity e, Instance& instance) {
    Call(instance, "on_stop");
    Call(instance, "on_destroy");
    CancelTimers(e);
    ClearControl(e);
  }
  void Sync() {
    if (Alive(cameraOwner)) {
      auto* c = world->GetComponent<ecs::LuaScriptComponent>(cameraOwner);
      if (!c || !c->enabled || !c->cameraEnabled) ClearControl(cameraOwner);
    }
    if (Alive(cursorOwner)) {
      auto* c = world->GetComponent<ecs::LuaScriptComponent>(cursorOwner);
      if (!c || !c->enabled || !c->inputEnabled) ClearControl(cursorOwner);
    }
    for (auto i = instances.begin(); i != instances.end();) {
      auto* c = world->GetComponent<ecs::LuaScriptComponent>(i->first);
      if (!Alive(i->first) || !c || !c->enabled ||
          c->scriptPath != i->second.path ||
          c->properties != i->second.overrides ||
          programs.at(i->second.path).revision != i->second.revision) {
        Retire(i->first, i->second);
        i = instances.erase(i);
      } else
        ++i;
    }
    // Snapshot IDs: callbacks may spawn; new entities start on the following
    // Update.
    for (auto e : world->Entities()) {
      auto* c = world->GetComponent<ecs::LuaScriptComponent>(e);
      if (!Alive(e) || !c || !c->enabled || c->scriptPath.empty() ||
          instances.contains(e))
        continue;
      const auto path = c->scriptPath;
      const auto overrides = c->properties;
      if (!programs.contains(path) &&
          (!errors.contains(path) ? !Load(path) : true))
        continue;
      const auto fingerprint = path + ":" +
                               std::to_string(programs.at(path).revision) +
                               ":" + overrides.dump();
      if (rejected.contains(e) && rejected.at(e).fingerprint == fingerprint)
        continue;
      try {
        auto& program = programs.at(path);
        auto env = Environment();
        auto context=std::make_shared<ModuleContext>();context->snapshot=program.modules;
        auto definition = Evaluate(program.source, path, env,context);
        Bind(env, e);
        auto self = lua.create_table();
        self["entity"] = e;
        for (auto it = program.fields.begin(); it != program.fields.end();
             ++it) {
          auto value = overrides.contains(it.key()) ? overrides.at(it.key())
                                                    : it.value().at("default");
          if (!ValidValue(it.value(), value))
            throw std::runtime_error("Invalid override: " + it.key());
          if (it.value().at("type") == "entity") {
            auto target = ResolveEntityReference(*world, value);
            self[it.key()] = Alive(target) ? sol::make_object(lua, target)
                                           : sol::make_object(lua, sol::nil);
          } else
            self[it.key()] = ToLua(lua, value);
        }
        auto [it, ok] = instances.emplace(
            e, Instance{path, program.revision, std::move(env), definition,
                        self, overrides, false, e, nextInstanceSerial++});
        it->second.modules=std::move(context);
        Call(it->second, "on_start");
        rejected.erase(e);
      } catch (const std::exception& error) {
        rejected[e] = {fingerprint,
                       GameplayEventHistory::Bounded(error.what(), 4096)};
        Fail(path, "instance", error.what());
      }
    }
    std::erase_if(rejected, [this](const auto& pair) {
      return !world->IsAlive(pair.first);
    });
  }
};
LuaRuntime::LuaRuntime() : impl(std::make_unique<Impl>()) {
  impl->facade = this;
}
LuaRuntime::~LuaRuntime() { Stop(); }
void LuaRuntime::Start(ecs::World& world,
                       std::function<bool(const std::string&)> input,
                       GameplayServices services) {
  Stop();
  impl->Check();
  world.AssertOwnerThread();
  impl->programs.clear();
  impl->codeCache.clear();impl->watches.clear();impl->rootDependencies.clear();impl->failedGroups.clear();impl->pollTime=0;
  impl->errors.clear();
  impl->loadFailures.clear();
  impl->world = &world;
  impl->particles.Bind(world);
  impl->input = std::move(input);
  impl->services = std::move(services);
  impl->history.BeginSession();
  if (impl->services.runtimeUI) {
    impl->services.runtimeUI->Clear();
    if (impl->services.postfxResources)
      impl->services.runtimeUI->Sync(world, *impl->services.postfxResources);
  }
}
void LuaRuntime::Stop() {
  impl->Check();
  impl->stopping = true;
  for (auto& [e, i] : impl->instances) impl->Retire(e, i);
  impl->postfx.clear();
  impl->history.EndSession();
  impl->instances.clear();
  impl->rejected.clear();
  impl->events.clear();
  impl->programs.clear();
  impl->codeCache.clear();impl->watches.clear();impl->rootDependencies.clear();impl->failedGroups.clear();impl->pollTime=0;
  impl->errors.clear();
  impl->loadFailures.clear();
  impl->timers.clear();
  impl->gameTime = 0;
  impl->mutations.Clear();
  impl->destroying.clear();
  impl->particles.Clear();
  impl->world = nullptr;
  if (impl->services.runtimeUI) impl->services.runtimeUI->Clear();
  if (impl->services.resetUIInput) impl->services.resetUIInput();
  impl->uiInputOwner = {};
  impl->input = {};
  impl->services = {};
  impl->cameraOwner = {};
  impl->cursorOwner = {};
  impl->held.clear();
  impl->pressed.clear();
  impl->released.clear();
  impl->fixedPressed.clear();
  impl->fixedReleased.clear();
  impl->boundKeys.clear();
  impl->fixedPhase = false;
  impl->stopping = false;
  lua_gc(impl->lua.lua_state(), LUA_GCCOLLECT, 0);
}
ecs::ParticleSystem& LuaRuntime::Particles(){return impl->particles;}
void LuaRuntime::Update(float dt) {
#if defined(TRACY_ENABLE)
  ZoneScopedN("LuaGameplayUpdate");
#endif
  impl->Check();
  if (!impl->world || !std::isfinite(dt) || dt < 0) return;
  impl->world->AssertOwnerThread();
  if (impl->world->IsIterating())
    throw std::logic_error("Lua Update during ECS iteration");
  if (impl->services.actions)
    for (const auto& action : impl->services.actions())
      if (!action.empty() && action.size() <= 128 &&
          (impl->held.size() < 128 || impl->held.contains(action)))
        impl->DiscoverInput(action);
  for (auto& [action, previous] : impl->held) {
    bool now = impl->input && impl->input(action);
    impl->pressed[action] = impl->services.pressed
                                ? impl->services.pressed(action)
                                : now && !previous;
    impl->released[action] = impl->services.released
                                 ? impl->services.released(action)
                                 : !now && previous;
    impl->fixedPressed[action] |= impl->pressed[action];
    impl->fixedReleased[action] |= impl->released[action];
    previous = now;
  }
  if (impl->services.inputAllowed && !impl->services.inputAllowed()) {
    impl->fixedPressed.clear();
    impl->fixedReleased.clear();
  }
  impl->PollModules(dt);
  impl->gameTime += double(dt);
  impl->Sync();
  auto events = std::move(impl->events);
  impl->events.clear();
  for (auto& [e, i] : impl->instances)
    if (auto* m =
            impl->world->GetComponent<ecs::CharacterMovementComponent>(e)) {
      m->desiredDirection = {};
      if (!impl->MotionAllowed(e) || i.failed) {
        m->jumpRequested = false;
        if (!impl->MotionAllowed(e)) {
          impl->fixedPressed.clear();
          impl->fixedReleased.clear();
        }
      }
    }
  impl->FireTimers();
  for (const auto& event : events) {
    if (!impl->Alive(event.target) || !impl->instances.contains(event.target)) {
      if (impl->history.Active()) impl->Deliver(event, sol::table{});
      continue;
    }
    auto data = impl->lua.create_table();
    data["type"] = event.type;
    data["amount"] = event.amount;
    data["other"] = event.other;
    data["source"] = impl->Alive(event.source)?sol::make_object(impl->lua,event.source):sol::make_object(impl->lua,sol::nil);
    impl->Deliver(event, data);
  }
  for (auto& [e, i] : impl->instances)
    if (impl->Alive(e)) impl->Call(i, "on_update", std::min(dt, .1f));
  // Destruction callback while components still exist; apply once, after all
  // calls.
  for (auto e : impl->destroying) {
    auto i = impl->instances.find(e);
    if (i != impl->instances.end()) {
      impl->Retire(e, i->second);
      impl->instances.erase(i);
    }
  }
  impl->mutations.Apply(*impl->world);
  impl->destroying.clear();
}
void LuaRuntime::DispatchUIEvents() {
  impl->Check();
  if (!impl->world || !impl->services.runtimeUI) return;
  impl->world->AssertOwnerThread();
  impl->Sync();
  for (auto& pointer : impl->services.runtimeUI->TakeEvents()) {
    if (impl->events.size() >= 1024) break;
    Impl::Event event{pointer.canvas.entity, {}, pointer.type, 0,
                      pointer.canvas.entity, 0,  "UI",         pointer};
    impl->Enqueue(std::move(event));
  }
  auto pending = std::move(impl->events);
  impl->events.clear();
  std::vector<Impl::Event> retained;
  for (auto& event : pending) {
    if (!event.uiEvent) {
      retained.push_back(std::move(event));
      continue;
    }
    if (!impl->services.runtimeUI->Valid(event.uiEvent->canvas)) {
      impl->history.Update(event.trace, EventStatus::Skipped, impl->gameTime,
                           {}, "Stale UI Canvas");
      continue;
    }
    auto data = impl->lua.create_table();
    data["type"] = event.type;
    data["widget"] = event.uiEvent->widget;
    data["canvas"] = event.uiEvent->canvas;
    data["animation"] = event.uiEvent->animation;
    data["canvas_key"] = event.uiEvent->canvas.document;
    data["source"] = event.source;
    data["owner"] = event.target;
    data["session"] = impl->history.Session();
    impl->Deliver(event, data);
  }
  impl->events.insert(impl->events.begin(),
                      std::make_move_iterator(retained.begin()),
                      std::make_move_iterator(retained.end()));
  for (auto e : impl->destroying) {
    auto at = impl->instances.find(e);
    if (at != impl->instances.end()) {
      impl->Retire(e, at->second);
      impl->instances.erase(at);
    }
  }
  impl->mutations.Apply(*impl->world);
  impl->destroying.clear();
}
void LuaRuntime::FixedUpdate(float dt) {
  impl->Check();
  if (!impl->world || !std::isfinite(dt) || dt <= 0) return;
  impl->world->AssertOwnerThread();
  if (impl->world->IsIterating())
    throw std::logic_error("Lua FixedUpdate during ECS iteration");
  impl->Sync();
  impl->fixedPhase = true;
  for (auto& [e, i] : impl->instances)
    if (impl->Alive(e)) {
      if (!impl->MotionAllowed(e))
        if (auto* m =
                impl->world->GetComponent<ecs::CharacterMovementComponent>(e)) {
          m->desiredDirection = {};
          m->jumpRequested = false;
        }
      impl->Call(i, "on_fixed_update", dt);
    }
  impl->fixedPhase = false;
  impl->fixedPressed.clear();
  impl->fixedReleased.clear();
  for (auto e : impl->destroying) {
    auto i = impl->instances.find(e);
    if (i != impl->instances.end()) {
      impl->Retire(e, i->second);
      impl->instances.erase(i);
    }
  }
  impl->mutations.Apply(*impl->world);
  impl->destroying.clear();
}
void LuaRuntime::SuspendInput() {
  impl->Check();
  impl->held.clear();
  impl->pressed.clear();
  impl->released.clear();
  impl->fixedPressed.clear();
  impl->fixedReleased.clear();
  if (impl->world)
    for (const auto& [e, i] : impl->instances)
      if (auto* m =
              impl->world->GetComponent<ecs::CharacterMovementComponent>(e)) {
        m->desiredDirection = {};
        m->jumpRequested = false;
        if (auto* b = impl->world->GetComponent<ecs::RigidbodyComponent>(e)) {
          b->velocity.x = 0;
          b->velocity.z = 0;
        }
      }
  if (impl->services.captureCursor) impl->services.captureCursor(false);
  impl->cursorOwner = {};
}
void LuaRuntime::QueueCollision(const ecs::CollisionEvent& e) {
  impl->Check();
  if (impl->world && impl->events.size() < 1022) {
    const char* type =
        e.kind == ecs::CollisionKind::TriggerEnter  ? "trigger_enter"
        : e.kind == ecs::CollisionKind::TriggerExit ? "trigger_exit"
                                                    : "collision";
    if (impl->Alive(e.a)) impl->Enqueue({e.a, e.b, type, 0, {}, 0, "Physics"});
    if (impl->Alive(e.b)) impl->Enqueue({e.b, e.a, type, 0, {}, 0, "Physics"});
  }
}
GameplayEventHistory& LuaRuntime::Events() {
  impl->Check();
  return impl->history;
}
bool LuaRuntime::ReloadScript(const std::string& path) {
  return impl->Load(path);
}
nlohmann::json LuaRuntime::Fields(const std::string& path) {
  impl->Check();
  if (!impl->programs.contains(path) && !impl->errors.contains(path))
    impl->Load(path);
  auto i = impl->programs.find(path);
  return i == impl->programs.end() ? Json::object() : i->second.fields;
}
std::optional<nlohmann::json> LuaRuntime::CachedFields(
    const std::string& path, const std::string& source) const {
  impl->Check();
  auto i = impl->programs.find(path);
  if (i == impl->programs.end() || i->second.source != source)
    return std::nullopt;
  return i->second.fields;
}
bool LuaRuntime::ValidFieldValue(const Json& schema, const Json& value) {
  try {
    return ValidValue(schema, value);
  } catch (...) {
    return false;
  }
}
std::string LuaRuntime::Error(const std::string& path) const {
  impl->Check();
  auto i = impl->errors.find(path);
  if(i!=impl->errors.end())return i->second;
  for(const auto& [root,deps]:impl->rootDependencies)if(deps.contains(path))
    if(auto error=impl->errors.find(root);error!=impl->errors.end())return "Dependent behaviour "+root+": "+error->second;
  return {};
}
const char* LuaInstanceStateName(LuaInstanceState state) {
  switch (state) {
    case LuaInstanceState::Stopped:
      return "Runtime unavailable (Play stopped)";
    case LuaInstanceState::NoEntity:
      return "Entity destroyed, stale or not selected";
    case LuaInstanceState::NoScript:
      return "No Lua behaviour";
    case LuaInstanceState::Disabled:
      return "Script disabled";
    case LuaInstanceState::Pending:
      return "Waiting for runtime synchronization";
    case LuaInstanceState::Running:
      return "Running";
    case LuaInstanceState::Failed:
      return "Failed";
    case LuaInstanceState::Rejected:
      return "Instance creation rejected";
    default:
      return "Reload pending (old fields unavailable)";
  }
}
LuaInstanceInfo LuaRuntime::InspectionInfo(ecs::Entity entity) const {
  impl->Check();
  LuaInstanceInfo out;
  out.entity = entity;
  out.session = impl->history.Session();
  if (!impl->world) return out;
  if (!impl->Alive(entity)) {
    out.state = LuaInstanceState::NoEntity;
    return out;
  }
  auto* component = impl->world->GetComponent<ecs::LuaScriptComponent>(entity);
  if (!component) {
    out.state = LuaInstanceState::NoScript;
    return out;
  }
  out.path = component->scriptPath;
  if (impl->loadFailures.contains(out.path))
    if (auto error = impl->errors.find(out.path); error != impl->errors.end())
      out.loadError = GameplayEventHistory::Bounded(error->second, 4096);
  if (!component->enabled) {
    out.state = LuaInstanceState::Disabled;
    return out;
  }
  const auto program = impl->programs.find(out.path);
  if (program != impl->programs.end())
    out.programRevision = program->second.revision;
  const auto found = impl->instances.find(entity);
  if (found == impl->instances.end()) {
    out.state = program == impl->programs.end() && !out.loadError.empty()
                    ? LuaInstanceState::Rejected
                    : LuaInstanceState::Pending;
    if (auto rejected = impl->rejected.find(entity);
        rejected != impl->rejected.end() &&
        rejected->second.fingerprint.starts_with(out.path + ":")) {
      out.state = LuaInstanceState::Rejected;
      out.lastError = rejected->second.error;
    }
    return out;
  }
  const auto& instance = found->second;
  out.serial = instance.serial;
  out.revision = instance.revision;
  out.lastCallback = instance.lastCallback;
  out.lastStatus = instance.lastStatus;
  out.lastError = instance.lastError;
  if (instance.path != out.path || out.revision != out.programRevision) {
    out.state = LuaInstanceState::ReloadPending;
    return out;
  }
  out.state =
      instance.failed ? LuaInstanceState::Failed : LuaInstanceState::Running;
  return out;
}
namespace {
struct LuaStackRestore {
  lua_State* state;
  int top;
  explicit LuaStackRestore(lua_State* s) : state(s), top(lua_gettop(s)) {}
  ~LuaStackRestore() { lua_settop(state, top); }
};
struct InspectionReader {
  lua_State* state;
  std::size_t remaining = 256;
  bool limited = false;
  std::unordered_set<const void*> ancestors;
  static std::string Text(const char* data, std::size_t size,
                          std::size_t maximum) {
    const auto text = Json(std::string(data, std::min(size, maximum)))
                          .dump(-1, ' ', false, Json::error_handler_t::replace);
    return text + (size > maximum ? "... (truncated)" : "");
  }
  std::string Key(int index) {
    switch (lua_type(state, index)) {
      case LUA_TSTRING: {
        std::size_t n = 0;
        auto* text = lua_tolstring(state, index, &n);
        return Text(text, n, 128);
      }
      case LUA_TNUMBER: {
        std::ostringstream value;
        value << std::setprecision(17) << lua_tonumber(state, index);
        return "[" + value.str() + "]";
      }
      case LUA_TBOOLEAN:
        return lua_toboolean(state, index) ? "[true]" : "[false]";
      default:
        return std::string("<") + luaL_typename(state, index) + " key>";
    }
  }
  LuaInspectedValue Value(int index, std::string name, unsigned depth) {
    LuaInspectedValue value;
    value.name = std::move(name);
    index = lua_absindex(state, index);
    value.type = luaL_typename(state, index);
    switch (lua_type(state, index)) {
      case LUA_TNIL:
        value.value = "nil";
        break;
      case LUA_TBOOLEAN:
        value.value = lua_toboolean(state, index) ? "true" : "false";
        break;
      case LUA_TNUMBER: {
        std::ostringstream text;
        if (lua_isinteger(state, index))
          text << lua_tointeger(state, index);
        else
          text << std::setprecision(17) << lua_tonumber(state, index);
        value.value = text.str();
        break;
      }
      case LUA_TSTRING: {
        std::size_t size = 0;
        auto* text = lua_tolstring(state, index, &size);
        value.value = Text(text, size, 512);
        value.limited = size > 512;
        limited |= value.limited;
        break;
      }
      case LUA_TUSERDATA:
        if (sol::stack::check<ecs::Entity>(state, index, sol::no_panic)) {
          value.entity = sol::stack::get<ecs::Entity>(state, index);
          value.type = "Entity";
          const auto e = value.entity;
          value.value = std::to_string(e.world) + ":" +
                        std::to_string(e.index) + ":" +
                        std::to_string(e.generation);
        } else
          value.value = "<userdata>";
        break;
      case LUA_TTABLE: {
        const auto* identity = lua_topointer(state, index);
        if (ancestors.contains(identity)) {
          value.value = "<cycle>";
          value.limited = limited = true;
        } else if (depth >= 3) {
          value.value = "<depth limit>";
          value.limited = limited = true;
        } else {
          value.children = Table(index, depth + 1, 0);
          value.value = "table";
        }
        break;
      }
      default:
        value.value = std::string("<") + value.type + ">";
        break;
    }
    return value;
  }
  // lua_next/rawget never call __pairs, __index or __tostring. Numeric keys are
  // copied with lua_tonumber, never converted in-place while iterating.
  std::vector<LuaInspectedValue> Table(int index, unsigned depth, int root) {
    LuaStackRestore restore(state);
    index = lua_absindex(state, index);
    std::vector<LuaInspectedValue> result;
    const auto* identity = lua_topointer(state, index);
    ancestors.insert(identity);
    lua_pushnil(state);
    unsigned scanned = 0;
    while (lua_next(state, index) != 0) {
      if (++scanned > 64 || !remaining) {
        limited = true;
        break;
      }
      bool skip = false;
      if (root && lua_type(state, -2) == LUA_TSTRING) {
        std::size_t size = 0;
        const auto* text = lua_tolstring(state, -2, &size);
        std::string_view key(text, size);
        skip = root == 1 ? key == "entity"
                         : key == "fields" || key == "on_start" ||
                               key == "on_update" || key == "on_fixed_update" ||
                               key == "on_event" || key == "on_ui_event" ||
                               key == "on_stop" || key == "on_destroy";
      }
      if (!skip) {
        --remaining;
        result.push_back(Value(-1, Key(-2), depth));
      }
      lua_pop(state, 1);
    }
    ancestors.erase(identity);
    std::stable_sort(
        result.begin(), result.end(),
        [](const auto& a, const auto& b) { return a.name < b.name; });
    return result;
  }
};
}  // namespace
LuaInstanceSnapshot LuaRuntime::Inspect(ecs::Entity entity) const {
  auto info = InspectionInfo(entity);
  LuaInstanceSnapshot snapshot;
  snapshot.info = info;
  if (!info.CanRead()) return snapshot;
  const auto& instance = impl->instances.at(entity);
  auto* state = impl->lua.lua_state();
  LuaStackRestore restore(state);
  InspectionReader reader{state};
  instance.self.push();
  const int self = lua_absindex(state, -1);
  snapshot.fields = reader.Table(self, 0, 1);
  // Absent declared fields really are nil in this instance; read raw values,
  // never substitute serialized overrides or schema defaults.
  const auto& schema = impl->programs.at(info.path).fields;
  for (auto it = schema.begin(); it != schema.end() && reader.remaining; ++it) {
    lua_pushlstring(state, it.key().data(), it.key().size());
    lua_rawget(state, self);
    if (lua_isnil(state, -1)) {
      --reader.remaining;
      snapshot.fields.push_back(reader.Value(
          -1, InspectionReader::Text(it.key().data(), it.key().size(), 128),
          0));
    }
    lua_pop(state, 1);
  }
  std::stable_sort(
      snapshot.fields.begin(), snapshot.fields.end(),
      [](const auto& a, const auto& b) { return a.name < b.name; });
  instance.definition.push();
  snapshot.definition = reader.Table(-1, 0, 2);
  snapshot.limited = reader.limited;
  return snapshot;
}
std::size_t LuaRuntime::InstanceCount() const { return impl->instances.size(); }
bool LuaRuntime::HasCameraOwner() const {
  impl->Check();
  auto* script = impl->Alive(impl->cameraOwner)
                     ? impl->world->GetComponent<ecs::LuaScriptComponent>(
                           impl->cameraOwner)
                     : nullptr;
  return script && script->enabled && script->cameraEnabled;
}
}  // namespace scripting

const PostFxRuntimeOverrides& scripting::LuaRuntime::PostFxOverrides() const {
  return impl->postfx;
}
bool scripting::LuaRuntime::SetPostFx(const std::string& id,
                                      const std::string& name,
                                      const nlohmann::json& value) {
  impl->Check();
  auto* resources = impl->services.postfxResources;
  if (impl->world && !impl->stopping && resources) {
    auto key = resources->FindPostFx(id);
    auto asset = key.empty() ? nullptr : resources->Get<PostFxResource>(key);
    if (asset && asset->GetData().definition)
      for (auto& p : asset->GetData().definition->parameters)
        if (p.name == name && p.Valid(value)) {
          impl->postfx[id].parameters[name] = value;
          return true;
        }
  }
  Logger::Get().Warn(
      "Lua postfx.set: unknown effect/parameter or invalid value: " + id +
      " / " + name);
  return false;
}
bool scripting::LuaRuntime::SetPostFxWeight(const std::string& id,
                                            float weight) {
  impl->Check();
  auto* r = impl->services.postfxResources;
  if (impl->world && !impl->stopping && r && std::isfinite(weight) &&
      weight >= 0 && weight <= 1 && !r->FindPostFx(id).empty()) {
    impl->postfx[id].weight = weight;
    return true;
  }
  Logger::Get().Warn(
      "Lua postfx.set_weight: unknown effect or invalid weight: " + id);
  return false;
}
bool scripting::LuaRuntime::ClearPostFx(const std::string& id) {
  impl->Check();
  auto* r = impl->services.postfxResources;
  if (!impl->world || impl->stopping || !r || r->FindPostFx(id).empty()) {
    Logger::Get().Warn("Lua postfx.clear: unknown effect: " + id);
    return false;
  }
  impl->postfx.erase(id);
  return true;
}
