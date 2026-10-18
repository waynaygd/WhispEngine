#define NOMINMAX
#include "EditorTestPreferences.h"
#include "core/Application.h"
#include "ecs/ComponentRegistry.h"
#include "ecs/Hierarchy.h"
#include "ecs/PhysicsQueries.h"
#include "ecs/TransformAccess.h"
#include "ecs/components/ColliderComponent.h"
#include "ecs/components/RigidbodyComponent.h"
#include "ecs/components/SceneIdentityComponent.h"
#include "ecs/components/TagComponent.h"
#include "ecs/systems/PhysicsSystem.h"
#include "scene/SceneSerializer.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <source_location>

using namespace ecs;
using Json=nlohmann::json;
namespace fs=std::filesystem;
static int checks=0;
static bool Same(Vec3 a,Vec3 b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}
static void Check(bool ok,const std::string& message,std::source_location at=std::source_location::current()){
    if(!ok)throw std::runtime_error(message+" at "+std::to_string(at.line()));++checks;
}
static Entity Object(World& world,const std::string& id){
    auto e=world.CreateEntity();world.AddComponent<SceneIdentityComponent>(e).value=id;
    world.AddComponent<TransformComponent>(e);world.AddComponent<TagComponent>(e).name=id;return e;
}
static Entity Find(const World& world,const std::string& id){
    for(auto e:world.Entities())if(SceneSerializer::EntitySceneId(world,e)==id)return e;return {};
}
static Json Read(const fs::path& path){std::ifstream file(path);Json j;file>>j;return j;}
static void Write(const fs::path& path,const Json& document){
    std::string error;Check(SceneSerializer::WriteAtomic(path,document,&error),"write fixture: "+error);
}
static Json Raw(const World& world){
    Json result={{"identity",world.GetIdentity()},{"capacity",world.GetCapacity()},{"entities",Json::array()}};
    for(auto e:world.Entities())result["entities"].push_back({{"index",e.index},{"generation",e.generation},{"components",ComponentRegistry::Builtins().Capture(world,e)}});
    return result;
}
static Json Canonical(Json document){
    auto& entities=document["entities"];
    std::sort(entities.begin(),entities.end(),[](const auto& a,const auto& b){return a.at("id").template get<std::string>()<b.at("id").template get<std::string>();});return document;
}
struct Fixture {
    fs::path root=fs::current_path()/("world-mode-"+NewSceneIdentity());
    Fixture(){Check(fs::create_directory(root),"unique fixture directory");}
    ~Fixture(){ProjectManager::Activate({});std::error_code ec;fs::remove_all(root,ec);}
};
static void RejectedFile(const fs::path& path,const Json& document,World& destination,const std::string& diagnostic){
    Write(path,document);SceneSettings settings;settings.background.solid[0]=.123f;
    const auto before=Raw(destination);const auto beforeSettings=SceneSettingsToJson(settings);std::string error;
    Check(!SceneSerializer::LoadWorld(path,destination,settings,&error),"invalid file load rejected");
    Check(error.find(diagnostic)!=std::string::npos,"specific diagnostic: "+error);
    Check(Raw(destination)==before&&SceneSettingsToJson(settings)==beforeSettings,"file failure preserves World identity/handles/components/IDs and settings");
    Check(!SceneSerializer::RestoreWorld(document,destination,settings,&error),"invalid direct restore rejected");
    Check(Raw(destination)==before&&SceneSettingsToJson(settings)==beforeSettings,"direct failure also atomic");
}
static void Roundtrip(const fs::path& path,World& source){
    std::string error;SceneSettings settings;settings.environment.exposure=1.25f;
    Check(SceneSerializer::SaveWorld(path,source,&error,&settings),"SaveWorld: "+error);
    const auto doc=Read(path);
    Check(doc.at("version")==4&&doc.at("worldMode")== (source.IsHierarchyData()?"hierarchy":"rootOnly"),"v4 explicit mode including empty/root-only hierarchy");
    World::DataMode mode=source.IsHierarchyData()?World::DataMode::RootOnly:World::DataMode::HierarchyData;
    Check(SceneSerializer::ReadWorldMode(path,mode,&error)&&mode==source.GetDataMode(),"file preflight before World construction");
    World restored(mode);SceneSettings loaded;
    Check(SceneSerializer::LoadWorld(path,restored,loaded,&error),"LoadWorld: "+error);
    Check(restored.GetDataMode()==source.GetDataMode()&&Canonical(SceneSerializer::CaptureWorld(restored,loaded))==Canonical(doc),"mode/IDs/components/settings exact roundtrip");
    auto shuffled=doc;std::reverse(shuffled["entities"].begin(),shuffled["entities"].end());Write(path,shuffled);
    Check(SceneSerializer::LoadWorld(path,restored,loaded,&error),"reverse entity file order");
    Check(Canonical(SceneSerializer::CaptureWorld(restored,loaded))==Canonical(doc),"reverse order preserves local TRS and relationships");
    for(auto e:source.Entities()){
        auto copy=Find(restored,SceneSerializer::EntitySceneId(source,e));Check(copy.IsValid(),"stable scene ID restored");
        auto before=ReadWorldMatrix(source,e),after=ReadWorldMatrix(restored,copy);
        Check(before.has_value()&&after.has_value(),"world matrices available");
        for(int i=0;i<16;++i)Check(std::abs((*before)[i]-(*after)[i])<1e-6,"affine matrix consistency including shear");
    }
    if(source.IsHierarchyData()){
        World root;Object(root,"keep");RejectedFile(path,doc,root,"worldMode mismatch");
        std::vector<EcsDemoEntityConfig> configs(1);configs[0].tag="keep";
        Check(!SceneSerializer::LoadEntityConfigs(path,configs,&error)&&configs.size()==1&&configs[0].tag=="keep","legacy flatten rejected even without Relationship");
    }
}
static void Persistence(Fixture& files){
    auto path=files.root/"scene.json";World root;auto entity=Object(root,"root");
    auto* t=root.GetComponent<TransformComponent>(entity);t->scale={2,3,4};
    auto& collider=root.AddComponent<ColliderComponent>(entity);collider.autoFitFromMesh=false;collider.halfExtents={.5f,.5f,.5f};collider.offset={1,0,0};
    auto& body=root.AddComponent<RigidbodyComponent>(entity);body.useGravity=false;body.velocity={1,0,0};
    Roundtrip(path,root);
    auto old=SceneSerializer::CaptureWorld(root);old.erase("worldMode");Write(path,old);
    World::DataMode mode=World::DataMode::HierarchyData;std::string error;
    Check(SceneSerializer::ReadWorldMode(path,mode,&error)&&mode==World::DataMode::RootOnly,"old v4 missing mode means RootOnly");
    World loaded;SceneSettings settings;Check(SceneSerializer::LoadWorld(path,loaded,settings,&error),"old v4 file load");
    Check(ComponentRegistry::Builtins().Capture(root,entity)==ComponentRegistry::Builtins().Capture(loaded,Find(loaded,"root")),"old v4 no component migration");
    auto hit=Raycast(loaded,{-10,0,0},{1,0,0},30);Check(hit&&std::abs(hit->distance-10.5f)<1e-5f,"RootOnly world-sized collider and world offset ignore Transform scale");
    PhysicsSystem physics;physics.Update(loaded,.01f);Check(ReadWorldPosition(loaded,Find(loaded,"root"))->x>0,"RootOnly nonunit scale/nonzero offset dynamic still simulated");
    Check(Same(loaded.GetComponent<TransformComponent>(Find(loaded,"root"))->scale,t->scale),"physics preserves authored scale");
    Check(SceneSerializer::SaveWorld(path,loaded,&error)&&Read(path)["worldMode"]=="rootOnly","old scene resave remains RootOnly");
    World hierarchy(World::DataMode::HierarchyData);Roundtrip(path,hierarchy);
    auto parent=Object(hierarchy,"parent");Roundtrip(path,hierarchy);
    auto child=Object(hierarchy,"child"),grandchild=Object(hierarchy,"grandchild"),sibling=Object(hierarchy,"sibling");
    Check(SetParent(hierarchy,child,parent)&&SetParent(hierarchy,grandchild,child)&&SetParent(hierarchy,sibling,parent),"hierarchy fixture");
    hierarchy.GetComponent<RelationshipComponent>(child)->order=42;hierarchy.GetComponent<RelationshipComponent>(sibling)->order=9;
    hierarchy.GetComponent<TransformComponent>(parent)->position={3,4,5};hierarchy.GetComponent<TransformComponent>(parent)->scale={2,1,1};
    hierarchy.GetComponent<TransformComponent>(child)->position={1,2,3};hierarchy.GetComponent<TransformComponent>(child)->rotation.z=.7f;
    Check(!ReadWorldTransform(hierarchy,child),"fixture includes nondecomposable affine world shear");Roundtrip(path,hierarchy);
    auto doc=SceneSerializer::CaptureWorld(hierarchy);doc.erase("worldMode");RejectedFile(path,doc,root,"Relationship");RejectedFile(path,doc,hierarchy,"Relationship");
    doc["worldMode"]="rootOnly";RejectedFile(path,doc,root,"Relationship");RejectedFile(path,doc,hierarchy,"Relationship");
    RejectedFile(path,old,hierarchy,"worldMode mismatch");
    auto flat=SceneSerializer::CaptureWorld(root);
    for(const auto& bad:std::vector<Json>{nullptr,true,4,1.25,Json::array(),Json::object(),"","RootOnly","unknown"}){
        auto invalid=flat;invalid["worldMode"]=bad;RejectedFile(path,invalid,root,"worldMode");RejectedFile(path,invalid,hierarchy,"worldMode");
        mode=World::DataMode::HierarchyData;Check(!SceneSerializer::ReadWorldMode(path,mode,&error)&&mode==World::DataMode::HierarchyData,"failed file preflight preserves output mode");
        mode=World::DataMode::RootOnly;Check(!SceneSerializer::DetermineWorldMode(invalid,mode,&error)&&mode==World::DataMode::RootOnly,"failed document preflight preserves output mode");
        std::vector<EcsDemoEntityConfig> configs(1);configs[0].tag="sentinel";Check(!SceneSerializer::LoadEntityConfigs(path,configs,&error)&&configs[0].tag=="sentinel","legacy configs reject malformed mode atomically");
    }
    {std::ofstream out(path);out<<"{broken";}
    const auto before=Raw(root);Check(!SceneSerializer::LoadWorld(path,root,settings,&error)&&Raw(root)==before,"invalid JSON leaves world unchanged");
    mode=World::DataMode::HierarchyData;Check(!SceneSerializer::ReadWorldMode(path,mode,&error)&&mode==World::DataMode::HierarchyData,"invalid JSON preflight atomic");
    Check(!SceneSerializer::ReadWorldMode(files.root/"absent.json",mode,&error)&&mode==World::DataMode::HierarchyData,"absent file preflight atomic");
}
static void Legacy(Fixture& files){
    EcsDemoEntityConfig config;config.tag="legacy";config.position={1,2,3};config.scale={2,3,4};config.meshPath="models/validation_cube.obj";
    config.colliderManual=true;config.colliderHalfExtents={.5f,.6f,.7f};config.colliderOffset={1,0,0};
    auto path=files.root/"legacy.json";std::string error;Check(SceneSerializer::SaveEntityConfigs(path,{config},&error),"legacy SaveEntityConfigs");
    const auto saved=Read(path);
    for(int version=0;version<=3;++version){
        auto doc=saved;if(version==0)doc=saved.at("entities");else doc["version"]=version;Write(path,doc);
        World::DataMode mode=World::DataMode::HierarchyData;Check(SceneSerializer::ReadWorldMode(path,mode,&error)&&mode==World::DataMode::RootOnly,"legacy array/v1/v2/v3 defaults RootOnly");
        World loaded;SceneSettings settings;Check(SceneSerializer::LoadWorld(path,loaded,settings,&error),"legacy file LoadWorld");
        auto e=loaded.Entities().at(0);Check(Same(loaded.GetComponent<TransformComponent>(e)->scale,config.scale)&&Same(loaded.GetComponent<ColliderComponent>(e)->halfExtents,config.colliderHalfExtents)&&Same(loaded.GetComponent<ColliderComponent>(e)->offset,config.colliderOffset),"legacy TRS/collider unchanged");
        World hierarchy(World::DataMode::HierarchyData);Object(hierarchy,"safe");const auto before=Raw(hierarchy);
        Check(!SceneSerializer::LoadWorld(path,hierarchy,settings,&error)&&Raw(hierarchy)==before,"legacy cannot silently upgrade to hierarchy");
    }
    for(const auto& value:std::vector<Json>{"hierarchy","future",nullptr,42}){
        auto bad=saved;bad["worldMode"]=value;World destination;Object(destination,"safe");RejectedFile(path,bad,destination,value=="hierarchy"?"schema v4":"worldMode");
        std::vector<EcsDemoEntityConfig> configs;Check(!SceneSerializer::LoadEntityConfigs(path,configs,&error),"legacy decoder never ignores mode");
    }
}
static void ApplicationBoundary(Fixture& files){
    auto first=ProjectManager::Create(files.root,"OldStartup"),second=ProjectManager::Create(files.root,"OtherStartup");
    auto old=Read(first->ResolveScene(first->startupScene));old.erase("worldMode");Write(first->ResolveScene(first->startupScene),old);
    Application app;std::string error;using Decision=Application::ProjectDecision;
    Check(app.OpenProject(first->descriptorPath,Decision::Discard,&error),"old RootOnly project startup");
    auto& ui=app.GetEditorLayer();auto e=ui.CreateEntity(app,"keep");ui.SelectEntity(e);ui.MarkDirty();
    const auto project=app.GetProject();const auto before=Raw(app.GetEditorWorld());const auto settings=SceneSettingsToJson(app.GetSceneSettings());
    const auto sceneKey=app.GetSceneKey();const auto selection=ui.GetSelectedEntities();const auto undo=ui.UndoCount();
    auto unchanged=[&]{Check(app.GetProject()==project&&Raw(app.GetEditorWorld())==before&&SceneSettingsToJson(app.GetSceneSettings())==settings&&app.GetSceneKey()==sceneKey&&ui.GetSelectedEntities()==selection&&ui.UndoCount()==undo&&ui.IsSceneDirty(),"Application failure preserves project/world/IDs/settings/selection/history/dirty state");};
    World hierarchy(World::DataMode::HierarchyData);Object(hierarchy,"root-without-relationship");
    auto hierarchyPath=first->ResolveScene("Scenes/Hierarchy.json");Check(SceneSerializer::SaveWorld(hierarchyPath,hierarchy,&error),"hierarchy fixture saved in test project");
    Check(!app.OpenScene("Scenes/Hierarchy.json",&error)&&error.find("worldMode mismatch")!=std::string::npos,"Editor rejects hierarchy even without Relationship");unchanged();
    Check(!app.SetStartupScene("Scenes/Hierarchy.json",&error),"startup setting rejects hierarchy");unchanged();
    auto descriptor=Read(first->descriptorPath);
    Check(SceneSerializer::SaveWorld(second->ResolveScene(second->startupScene),hierarchy,&error),"hierarchy startup fixture");
    Check(!app.OpenProject(second->descriptorPath,Decision::Discard,&error),"hierarchy project startup rejected before commit");unchanged();
    Check(Read(first->descriptorPath)==descriptor,"failed startup update did not change project descriptor");
    auto malformed=SceneSerializer::CaptureWorld(hierarchy);malformed["worldMode"]=false;Write(second->ResolveScene(second->startupScene),malformed);
    Check(!app.OpenProject(second->descriptorPath,Decision::Discard,&error),"corrupt startup mode rejected");unchanged();
    World root;Object(root,"explicit-root");Check(SceneSerializer::SaveWorld(second->ResolveScene(second->startupScene),root,&error),"explicit RootOnly startup saved");
    Check(app.OpenProject(second->descriptorPath,Decision::Discard,&error)&&!app.GetEditorWorld().IsHierarchyData()&&Find(app.GetEditorWorld(),"explicit-root").IsValid(),"explicit RootOnly project startup remains supported");
    Check(app.SaveCurrentScene(&error)&&Read(second->ResolveScene(second->startupScene))["worldMode"]=="rootOnly","Application save preserves RootOnly marker");
}
int main(){ScopedEditorPreferenceRestore preferences;try{Fixture files;Persistence(files);Legacy(files);ApplicationBoundary(files);std::cout<<"Scene World Mode: "<<checks<<" checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
