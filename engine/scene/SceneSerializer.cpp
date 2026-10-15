#define NOMINMAX
#include "SceneSerializer.h"
#include "../ecs/ComponentRegistry.h"
#include "../ecs/components/SceneIdentityComponent.h"
#include <set>
#ifdef _WIN32
#include <windows.h>
#endif
#include "LightSerialization.h"
#include "../ecs/components/LightComponent.h"
#include "../resources/MaterialSerialization.h"

#include "../core/Logger.h"
#include "../ecs/World.h"
#include "../ecs/components/BoundsBounceComponent.h"
#include "../ecs/components/ColliderComponent.h"
#include "../ecs/components/MaterialComponent.h"
#include "../ecs/components/MeshRendererComponent.h"
#include "../ecs/components/RigidbodyComponent.h"
#include "../ecs/components/TagComponent.h"
#include "../ecs/components/TransformComponent.h"
#include "../ecs/components/VelocityComponent.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace
{
void SetError(std::string* outError, const std::string& message)
{
    if (outError != nullptr)
        *outError = message;
}

nlohmann::json Vec3ToJson(const ecs::Vec3& v)
{
    return nlohmann::json::array({ v.x, v.y, v.z });
}

nlohmann::json ColorToJson(const std::array<float, 4>& color)
{
    return nlohmann::json::array({ color[0], color[1], color[2], color[3] });
}

void ReadVec3(const nlohmann::json& json, const char* key, ecs::Vec3& out)
{
    if (!json.contains(key) || !json[key].is_array())
        return;

    const auto& value = json[key];
    if (value.size() > 0) out.x = value[0].get<float>();
    if (value.size() > 1) out.y = value[1].get<float>();
    if (value.size() > 2) out.z = value[2].get<float>();
}

nlohmann::json EntityToJson(const EcsDemoEntityConfig& entity)
{
    nlohmann::json json;
    if(entity.light)json["light"]=LightToJson(*entity.light);
    json["tag"] = entity.tag;
    json["meshPath"] = entity.meshPath;
    json["texturePath"] = entity.texturePath;
    json["shaderPath"] = entity.shaderPath;
    json["materialPath"] = entity.materialPath;
    json["slotMaterials"]=entity.slotMaterials;json["materialOverrides"]=MaterialOverridesToJson(entity.materialOverrides);
    json["color"] = ColorToJson(entity.materialTint);
    json["visible"] = entity.visible;
    json["bounce"] = entity.bounce;
    json["position"] = Vec3ToJson(entity.position);
    json["rotation"] = Vec3ToJson(entity.rotation);
    json["scale"] = Vec3ToJson(entity.scale);
    json["linearVelocity"] = Vec3ToJson(entity.linearVelocity);
    json["angularVelocity"] = Vec3ToJson(entity.angularVelocity);
    json["colliderManual"] = entity.colliderManual;
    json["colliderType"] = entity.colliderType;
    json["simulatePhysics"] = entity.simulatePhysics;
    json["isStatic"] = entity.isStatic;
    json["useGravity"] = entity.useGravity;
    if (entity.colliderManual)
    {
        json["colliderHalfExtents"] = Vec3ToJson(entity.colliderHalfExtents);
        json["colliderOffset"] = Vec3ToJson(entity.colliderOffset);
    }
    return json;
}

EcsDemoEntityConfig EntityFromJson(const nlohmann::json& json)
{
    EcsDemoEntityConfig entity;
    if(json.contains("light"))entity.light=LightFromJson(json["light"]);
    entity.tag = json.value("tag", std::string());
    entity.meshPath = json.value("meshPath", std::string());
    entity.texturePath = json.value("texturePath", std::string());
    entity.shaderPath = json.value("shaderPath", std::string());
    if(json.contains("materialOverrides"))entity.materialOverrides=MaterialOverridesFromJson(json.at("materialOverrides"));
    if(json.contains("slotMaterials"))entity.slotMaterials=json.at("slotMaterials").get<std::vector<std::string>>();
    entity.materialPath = json.value("materialPath", std::string());
    if (json.contains("color") && json["color"].is_array())
    {
        const auto& color = json["color"];
        for (std::size_t i = 0; i < color.size() && i < entity.materialTint.size(); ++i)
            entity.materialTint[i] = color[i].get<float>();
    }
    entity.visible = json.value("visible", true);
    entity.bounce = json.value("bounce", true);
    ReadVec3(json, "position", entity.position);
    ReadVec3(json, "rotation", entity.rotation);
    ReadVec3(json, "scale", entity.scale);
    ReadVec3(json, "linearVelocity", entity.linearVelocity);
    ReadVec3(json, "angularVelocity", entity.angularVelocity);
    ReadVec3(json, "colliderHalfExtents", entity.colliderHalfExtents);
    ReadVec3(json, "colliderOffset", entity.colliderOffset);
    entity.colliderType = json.value("colliderType", std::string("box"));
    entity.colliderManual = json.value("colliderManual", false) ||
        json.contains("colliderHalfExtents") || json.contains("colliderOffset");
    entity.simulatePhysics = json.value("simulatePhysics", true);
    entity.isStatic = json.value("isStatic", false);
    entity.useGravity = json.value("useGravity", true);
    return entity;
}
}

bool SceneSerializer::LoadEntityConfigs(
    const std::filesystem::path& path,
    std::vector<EcsDemoEntityConfig>& outEntities,
    std::string* outError)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        SetError(outError, "SceneSerializer: cannot open scene file: " + path.string());
        return false;
    }

    nlohmann::json scene;
    try
    {
        file >> scene;
    }
    catch (const std::exception& e)
    {
        SetError(outError, std::string("SceneSerializer: JSON parse failed: ") + e.what());
        return false;
    }

    if(scene.is_object()&&scene.value("version",1)==4){
        ecs::World world;SceneSettings settings;if(!RestoreWorld(scene,world,settings,outError))return false;
        std::vector<EcsDemoEntityConfig> loaded;
        for(auto entity:world.Entities()){
            EcsDemoEntityConfig c;c.scale={1,1,1};c.bounce=false;c.simulatePhysics=false;c.useGravity=false;
            if(auto* t=world.GetComponent<ecs::TransformComponent>(entity)){c.position=t->position;c.rotation=t->rotation;c.scale=t->scale;}
            if(auto* tag=world.GetComponent<ecs::TagComponent>(entity))c.tag=tag->name;
            if(auto* m=world.GetComponent<ecs::MeshRendererComponent>(entity)){c.meshPath=m->meshPath;c.texturePath=m->texturePath;c.shaderPath=m->shaderPath;c.visible=m->visible;}
            if(auto* m=world.GetComponent<ecs::MaterialComponent>(entity)){c.materialPath=m->materialPath;c.slotMaterials=m->slotMaterials;c.materialOverrides=m->overrides;std::copy(m->tint,m->tint+4,c.materialTint.begin());if(!m->shaderPath.empty())c.shaderPath=m->shaderPath;if(!m->texturePath.empty())c.texturePath=m->texturePath;}
            if(auto* b=world.GetComponent<ecs::RigidbodyComponent>(entity)){c.linearVelocity=b->velocity;c.angularVelocity=b->angularVelocity;c.isStatic=b->isStatic;c.simulatePhysics=b->simulatePhysics;c.useGravity=b->useGravity;}
            if(auto* v=world.GetComponent<ecs::VelocityComponent>(entity)){c.linearVelocity=v->linear;c.angularVelocity=v->angular;}
            if(auto* collider=world.GetComponent<ecs::ColliderComponent>(entity)){c.colliderHalfExtents=collider->halfExtents;c.colliderOffset=collider->offset;c.colliderType=collider->type==ecs::ColliderType::Sphere?"sphere":"box";c.colliderManual=!collider->autoFitFromMesh;}
            c.bounce=world.HasComponent<ecs::BoundsBounceComponent>(entity);LightType type;
            if(auto* light=ecs::GetLightParameters(world,entity,&type)){c.light=SceneLightDescription{type,*light,false};if(auto* d=world.GetComponent<ecs::DirectionalLightComponent>(entity))c.light->primary=d->primary;}
            loaded.push_back(std::move(c));
        }
        outEntities=std::move(loaded);return true;
    }
    const auto& entities = scene.contains("entities") ? scene["entities"] : scene;
    if (!entities.is_array())
    {
        SetError(outError, "SceneSerializer: scene must contain an entities array");
        return false;
    }

    try {
        if(scene.is_object()&&scene.value("version",1)!=1&&scene.value("version",1)!=2&&scene.value("version",1)!=3)throw std::runtime_error("unsupported scene version");
        std::vector<EcsDemoEntityConfig> loaded;
        for(const auto& entityJson:entities)loaded.push_back(EntityFromJson(entityJson));
        outEntities=std::move(loaded);
    } catch(const std::exception& e) {SetError(outError,e.what());return false;}

    Logger::Get().Info(
        "SceneSerializer: loaded scene=" + path.string() +
        " entities=" + std::to_string(outEntities.size()));
    return true;
}

bool SceneSerializer::SaveEntityConfigs(
    const std::filesystem::path& path,
    const std::vector<EcsDemoEntityConfig>& entities,
    std::string* outError)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    nlohmann::json scene;
    scene["version"] = 3;
    scene["entities"] = nlohmann::json::array();
    for (const auto& entity : entities)
        scene["entities"].push_back(EntityToJson(entity));

    return WriteAtomic(path,scene,outError);
}

bool SceneSerializer::SaveWorld(const std::filesystem::path& path,ecs::World& world,std::string* error,const SceneSettings* settings){
    try{return WriteAtomic(path,CaptureWorld(world,settings?*settings:SceneSettings{}),error);}
    catch(const std::exception& e){SetError(error,std::string("Scene save: ")+e.what());return false;}
}
std::string SceneSerializer::EntitySceneId(const ecs::World& world,ecs::Entity e){
    if(auto* id=world.GetComponent<ecs::SceneIdentityComponent>(e))return id->value;
    return "local-"+std::to_string(e.index)+"-"+std::to_string(e.generation);
}
std::string SceneSerializer::PrimaryDirectionalId(ecs::World& world){
    ecs::Entity selected;bool preferred=false;
    for(auto e:world.Entities())if(auto* d=world.GetComponent<ecs::DirectionalLightComponent>(e)){
        if(d->enabled&&(!selected.IsValid()||(!preferred&&d->primary))){selected=e;preferred=d->primary;}
    }
    return selected.IsValid()?EntitySceneId(world,selected):std::string();
}
nlohmann::json SceneSerializer::CaptureWorld(ecs::World& world,const SceneSettings& settings){
    auto resolved=settings;resolved.primaryDirectional=PrimaryDirectionalId(world);
    nlohmann::json scene={{"version",4},{"settings",SceneSettingsToJson(resolved)},{"entities",nlohmann::json::array()}};
    std::set<std::string> ids;const auto& registry=ecs::ComponentRegistry::Builtins();
    ecs::World check;auto test=check.CreateEntity();
    for(auto e:world.Entities()){
        auto components=registry.Capture(world,e);components.erase("SceneIdentity");const auto id=EntitySceneId(world,e);
        if(id.empty()||id.size()>128||!ids.insert(id).second)throw std::runtime_error("Duplicate/invalid scene entity ID");
        // Validate a value copy before writing. No changes to the source scene.
        registry.Restore(check,test,components);
        scene["entities"].push_back({{"id",id},{"components",std::move(components)}});
    }
    return scene;
}
bool SceneSerializer::RestoreWorld(const nlohmann::json& scene,ecs::World& world,SceneSettings& settings,std::string* error){
    try{
        if(!scene.is_object()||scene.value("version",0)!=4||!scene.contains("entities")||!scene.at("entities").is_array())throw std::runtime_error("Expected scene schema v4");
        ecs::World staged;auto next=scene.contains("settings")?SceneSettingsFromJson(scene.at("settings")):SceneSettings{};
        std::set<std::string> ids;const auto& registry=ecs::ComponentRegistry::Builtins();
        for(const auto& node:scene.at("entities")){
            const auto id=node.at("id").get<std::string>();if(id.empty()||id.size()>128||!ids.insert(id).second)throw std::runtime_error("Duplicate/invalid scene entity ID: "+id);
            auto e=staged.CreateEntity();try{registry.Restore(staged,e,node.at("components"));}catch(const std::exception& failure){throw std::runtime_error("Entity "+id+": "+failure.what());}
            if(staged.HasComponent<ecs::SceneIdentityComponent>(e))throw std::runtime_error("SceneIdentity belongs in entity.id, not components");
            staged.AddComponent<ecs::SceneIdentityComponent>(e).value=id;
        }
        if(!next.primaryDirectional.empty()&&next.primaryDirectional!=PrimaryDirectionalId(staged))throw std::runtime_error("Primary directional reference conflicts with ECS preferences/order");
        world.ReplaceData(staged);settings=std::move(next);return true;
    }catch(const std::exception& e){SetError(error,std::string("Scene load: ")+e.what());return false;}
}
bool SceneSerializer::LoadWorld(const std::filesystem::path& path,ecs::World& world,SceneSettings& settings,std::string* error){
    try{
        std::ifstream file(path);if(!file)throw std::runtime_error("Cannot open scene: "+path.string());nlohmann::json scene;file>>scene;
        if(scene.is_object()&&scene.value("version",1)==4)return RestoreWorld(scene,world,settings,error);
        std::vector<EcsDemoEntityConfig> configs;if(!LoadEntityConfigs(path,configs,error))return false;
        ecs::World staged;for(const auto& c:configs){
            auto e=staged.CreateEntity();staged.AddComponent<ecs::SceneIdentityComponent>(e);staged.AddComponent<ecs::TagComponent>(e).name=c.tag;
            auto& t=staged.AddComponent<ecs::TransformComponent>(e);t.position=c.position;t.rotation=c.rotation;t.scale=c.scale;
            if(!c.meshPath.empty()){auto& m=staged.AddComponent<ecs::MeshRendererComponent>(e);m.meshPath=c.meshPath;m.texturePath=c.texturePath;m.shaderPath=c.shaderPath;m.visible=c.visible;}
            if(!c.meshPath.empty()||!c.materialPath.empty()){auto& m=staged.AddComponent<ecs::MaterialComponent>(e);m.materialPath=c.materialPath;m.slotMaterials=c.slotMaterials;m.overrides=c.materialOverrides;m.shaderPath=c.shaderPath;m.texturePath=c.texturePath;std::copy(c.materialTint.begin(),c.materialTint.end(),m.tint);}
            if(c.light)ecs::AddSceneLight(staged,e,*c.light);
            else if(!c.meshPath.empty()){
                auto& b=staged.AddComponent<ecs::RigidbodyComponent>(e);b.velocity=c.linearVelocity;b.angularVelocity=c.angularVelocity;b.simulatePhysics=c.simulatePhysics;b.useGravity=c.useGravity;b.isStatic=c.isStatic;
                auto& collider=staged.AddComponent<ecs::ColliderComponent>(e);collider.type=c.colliderType=="sphere"?ecs::ColliderType::Sphere:ecs::ColliderType::Box;collider.autoFitFromMesh=!c.colliderManual;
                if(c.colliderManual){collider.halfExtents=c.colliderHalfExtents;collider.offset=c.colliderOffset;}
                auto& velocity=staged.AddComponent<ecs::VelocityComponent>(e);velocity.linear=c.linearVelocity;velocity.angular=c.angularVelocity;if(c.bounce)staged.AddComponent<ecs::BoundsBounceComponent>(e);
            }
        }
        const auto migrated=CaptureWorld(staged,SceneSettings{});return RestoreWorld(migrated,world,settings,error);
    }catch(const std::exception& e){SetError(error,std::string("Scene load: ")+e.what());return false;}
}
bool SceneSerializer::WriteAtomic(const std::filesystem::path& path,const nlohmann::json& document,std::string* error,bool replace){
    std::filesystem::path temporary;
    try{
        if(path.empty()||path.filename().empty())throw std::runtime_error("Empty scene output path");
        if(!replace&&std::filesystem::exists(path))throw std::runtime_error("Destination already exists; explicit overwrite confirmation required");
        if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());
        temporary=path;temporary+=std::string(".tmp-")+ecs::NewSceneIdentity();
        {std::ofstream file(temporary,std::ios::binary|std::ios::trunc);if(!file)throw std::runtime_error("Cannot create atomic scene temporary file");file<<document.dump(2);file.flush();if(!file)throw std::runtime_error("Scene write failed");file.close();if(!file)throw std::runtime_error("Scene close failed");}
#ifdef _WIN32
        if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH|(replace?MOVEFILE_REPLACE_EXISTING:0)))throw std::runtime_error("Atomic scene replacement failed, Windows error "+std::to_string(GetLastError()));
#else
        std::filesystem::rename(temporary,path);
#endif
        return true;
    }catch(const std::exception& e){std::error_code ignored;if(!temporary.empty())std::filesystem::remove(temporary,ignored);SetError(error,e.what());return false;}
}
