#include "EditorTestPreferences.h"
#define NOMINMAX
#include "core/Application.h"
#include "core/Logger.h"
#include "core/AssetPaths.h"
#include "ecs/ComponentRegistry.h"
#include "ecs/MutationQueue.h"
#include "ecs/components/SceneIdentityComponent.h"
#include "editor/EditorPreferences.h"
#include "scene/SceneSerializer.h"
#include "resources/ResourceManager.h"
#include "editor/InputRouting.h"
#include "editor/LightGizmoMath.h"
#include <chrono>
#if defined(ENABLE_DX12)
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include "platform/GlfwWindow.h"
#include <GLFW/glfw3.h>
#endif
#include <fstream>
#include <iostream>
#include <thread>
static void Check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
struct RegressionExtension {float gain=1;};
static void Extension(){
    ecs::ComponentRegistry registry;ecs::ComponentDescriptor d;d.key="RegressionExtension";d.label="Extension";d.id=ecs::ComponentId(d.key.c_str());
    d.has=[](const ecs::World& w,ecs::Entity e){return w.HasComponent<RegressionExtension>(e);};d.add=[](ecs::World& w,ecs::Entity e){w.AddComponent<RegressionExtension>(e);};d.remove=[](ecs::World& w,ecs::Entity e){w.RemoveComponent<RegressionExtension>(e);};d.reset=[](ecs::World& w,ecs::Entity e){*w.GetComponent<RegressionExtension>(e)={};};
    d.serialize=[](const ecs::World& w,ecs::Entity e){return nlohmann::json{{"gain",w.GetComponent<RegressionExtension>(e)->gain}};};d.deserialize=[](ecs::World& w,ecs::Entity e,const nlohmann::json& j){w.GetComponent<RegressionExtension>(e)->gain=j.at("gain").get<float>();};
    ecs::PropertyDescriptor p;p.key=p.label="gain";p.kind=ecs::PropertyKind::Number;p.minimum=0;p.maximum=10;p.get=[](const ecs::World& w,ecs::Entity e)->ecs::PropertyValue{return w.GetComponent<RegressionExtension>(e)->gain;};p.set=[](ecs::World& w,ecs::Entity e,const ecs::PropertyValue& v){w.GetComponent<RegressionExtension>(e)->gain=std::get<float>(v);};d.properties.push_back(p);registry.Register(d);
    bool duplicate=false;try{registry.Register(d);}catch(const std::exception&){duplicate=true;}Check(duplicate,"duplicate stable registry ID");
    ecs::World world;auto entity=world.CreateEntity();Check(registry.Add(world,entity,d.id)&&registry.Set(world,entity,d.id,"gain",4.f),"registered type properties");auto data=registry.Capture(world,entity);registry.Reset(world,entity,d.id);registry.Restore(world,entity,data);ecs::PropertyValue value;Check(registry.Get(world,entity,d.id,"gain",value)&&std::get<float>(value)==4.f,"registered serialization/property extension hooks");
}
static void Cpu(){
    Extension();
    const auto& registry=ecs::ComponentRegistry::Builtins();Check(registry.Components().size()==17,"builtin component coverage including Fog, LuaScript and player components");
    for(const char* key:{"Fog","PlayerStart","Player","CharacterMovement"}){const auto* component=registry.Find(key);Check(component&&component->id==ecs::ComponentId(key),"player component stable descriptor");}
    const auto* script=registry.Find("LuaScript");Check(script&&script->id==ecs::ComponentId("LuaScript")&&script->properties.size()==4,"LuaScript stable descriptor, asset/enabled and explicit input/camera permissions");
    ecs::World a,b;auto e=a.CreateEntity(),other=b.CreateEntity();Check(e.index==other.index&&e.generation==other.generation&&e!=other,"World identity in Entity equality");Check(!a.IsAlive(other)&&!b.IsAlive(e),"foreign handles cannot alias");
    std::string error;Check(registry.Add(a,e,ecs::ComponentId("Transform"),&error),"registry add");Check(!registry.Add(a,other,ecs::ComponentId("Transform"),&error),"registry foreign add");
    ecs::PropertyValue property;Check(!registry.Get(a,other,ecs::ComponentId("Transform"),"position",property,&error),"foreign property read");Check(registry.Get(a,e,ecs::ComponentId("Transform"),"position",property,&error),"safe property read");
    Check(editor::RouteInput({true,true,true,false,false,false,false,true,false,true})==editor::InputOwner::UI,"text owns input before camera/game");
    Check(editor::RouteInput({true,true,false,false,true,false,false,true,false,true})==editor::InputOwner::TransformGizmo,"gizmo input priority");
    Check(editor::RouteInput({true,true,false,false,false,false,false,true,false,true})==editor::InputOwner::Camera,"camera input priority");
    Check(editor::RouteInput({false,true,false,false,false,false,false,false,false,true})==editor::InputOwner::UI,"unfocused runtime input");
    const auto camera=editor::GizmoCamera({0,2,-3},0,-.2f,1.f,1.f,.01f,100);Check(editor::ProjectEditorLine({-20,0,0},{20,0,0},camera).has_value(),"grid segments crossing viewport boundaries");
    Check(!registry.Set(a,e,ecs::ComponentId("Transform"),"position",ecs::Vec3{NAN,0,0},&error),"NaN property rejection");Check(registry.Set(a,e,ecs::ComponentId("Transform"),"scale",ecs::Vec3{-2,3,.5f},&error),"negative/nonuniform scale");
    Check(registry.Add(a,e,ecs::ComponentId("SpotLight"),&error),"add light");Check(!registry.Add(a,e,ecs::ComponentId("PointLight"),&error),"one light type per entity");
    Check(!registry.Remove(a,e,ecs::ComponentId("Transform"),&error),"dependency validation");Check(registry.Remove(a,e,ecs::ComponentId("SpotLight"),&error),"remove component");
    bool protectedIteration=false;a.ForEach<ecs::TransformComponent>([&](auto,auto&){try{a.CreateEntity();}catch(const std::exception&){protectedIteration=true;}});Check(protectedIteration,"iterator mutation guard");
    ecs::MutationQueue commands;a.ForEach<ecs::TransformComponent>([&](auto entity,auto&){commands.Add<ecs::PointLightComponent>(entity);});Check(commands.Apply(a)==1&&a.HasComponent<ecs::PointLightComponent>(e),"deferred component mutation");commands.Destroy(e);Check(commands.Apply(b)==0&&a.IsAlive(e),"queue world validation");
    bool workerRejected=false,workerApplyRejected=false,workerCloneRejected=false;
    std::thread producer([&]{try{a.DestroyEntity(e);}catch(const std::exception&){workerRejected=true;}commands.Add<ecs::MaterialComponent>(e);try{commands.Apply(a);}catch(const std::exception&){workerApplyRejected=true;}try{a.CloneData();}catch(const std::exception&){workerCloneRejected=true;}});
    producer.join();Check(workerRejected&&workerApplyRejected&&workerCloneRejected&&commands.Apply(a)==1,"producer queue/owner thread boundary without lost commands");
    ecs::Entity queued;commands.Enqueue(a.GetIdentity(),[&](ecs::World& target){queued=target.CreateEntity();target.AddComponent<ecs::TransformComponent>(queued);});Check(commands.Apply(a)==1&&a.IsAlive(queued),"deferred entity creation initializer");commands.Destroy(queued);Check(commands.Apply(a)==1&&!a.IsAlive(queued),"deferred entity destruction");
    bool selfReplaceRejected=false;try{a.ReplaceData(a);}catch(const std::exception&){selfReplaceRejected=true;}Check(selfReplaceRejected&&a.IsAlive(e),"self data replacement rejected");
    ecs::World::CloneMap mapping;auto clone=a.CloneData(&mapping);Check(clone->GetIdentity()!=a.GetIdentity()&&clone->IsAlive(mapping.at(e))&&!clone->IsAlive(e),"clone references");clone->GetComponent<ecs::TransformComponent>(mapping.at(e))->position.x=99;Check(a.GetComponent<ecs::TransformComponent>(e)->position.x==0,"component value isolation");

    Application app;auto& editor=app.GetEditorLayer();auto entity=editor.CreateEntity(app,"Original");auto& world=app.GetWorld();
    Check(editor.AddComponent(app,entity,ecs::ComponentId("MeshRenderer")),"editor AddComponent");
    Check(editor.SetProperty(app,entity,ecs::ComponentId("MeshRenderer"),"meshPath",std::string("models/validation_cube.obj")),"mesh asset reference");
    Check(editor.AddComponent(app,entity,ecs::ComponentId("Material")),"material component");Check(editor.SetProperty(app,entity,ecs::ComponentId("Material"),"materialPath",std::string("materials/stress.material.json")),"material reference");
    Check(editor.AssignAsset(app,entity,ecs::AssetKind::Texture,"textures/stress/cube.dds"),"typed texture assignment");editor.UndoEdit(app);Check(world.GetComponent<ecs::MaterialComponent>(entity)->texturePath.empty(),"asset assignment Undo");
    Check(editor.ResetComponent(app,entity,ecs::ComponentId("Material")),"generic Reset values");editor.UndoEdit(app);Check(world.GetComponent<ecs::MaterialComponent>(entity)->materialPath=="materials/stress.material.json","Reset Undo");
    Check(!editor.SetProperty(app,entity,ecs::ComponentId("SceneIdentity"),"value",std::string("forged")),"readonly scene identity");
    Check(editor.AddComponent(app,entity,ecs::ComponentId("PointLight")),"point component");Check(editor.AddComponent(app,entity,ecs::ComponentId("Rigidbody"))&&editor.AddComponent(app,entity,ecs::ComponentId("Collider")),"physics components");
    auto disposable=editor.CreateEntity(app,"Disposable");auto duplicate=editor.DuplicateEntity(app,entity);Check(world.IsAlive(duplicate)&&world.GetComponent<ecs::SceneIdentityComponent>(duplicate)->value!=world.GetComponent<ecs::SceneIdentityComponent>(entity)->value,"duplicate identity");
    Check(editor.RenameEntity(app,duplicate,"Renamed"),"rename");editor.UndoEdit(app);Check(world.GetComponent<ecs::TagComponent>(duplicate)->name=="Original Copy","rename Undo");editor.RedoEdit(app);Check(world.GetComponent<ecs::TagComponent>(duplicate)->name=="Renamed","rename Redo");
    Check(editor.DeleteEntity(app,duplicate),"generic delete");editor.UndoEdit(app);auto restored=editor.GetSelectedEntity();Check(restored!=duplicate&&world.IsAlive(restored),"delete Undo generation");editor.UndoEdit(app);Check(world.GetComponent<ecs::TagComponent>(restored)->name=="Original Copy","history remap after delete");editor.RedoEdit(app);editor.RedoEdit(app);Check(!world.IsAlive(restored),"redo remapped delete");
    editor.SelectEntity(entity);editor.MarkSaved();auto count=editor.UndoCount();editor.RecordEdit(app,"Drag position");for(int i=0;i<100;++i)world.GetComponent<ecs::TransformComponent>(entity)->position.x=i*.01f;editor.FinishTransaction(app);Check(editor.UndoCount()==count+1&&editor.IsSceneDirty(),"drag coalescing/dirty");editor.UndoEdit(app);Check(!editor.IsSceneDirty()&&world.GetComponent<ecs::TransformComponent>(entity)->position.x==0,"Undo savepoint");editor.RedoEdit(app);
    editor.RecordEdit(app,"Cancelled drag");world.GetComponent<ecs::TransformComponent>(entity)->position.x=18;editor.CancelTransaction(app);Check(std::abs(world.GetComponent<ecs::TransformComponent>(entity)->position.x-.99f)<1e-6,"cancel rollback");
    auto settings=app.GetSceneSettings();settings.lighting.ambientColor={.2f,.3f,.4f};settings.lighting.ambientIntensity=.17f;settings.environment.enabled=true;settings.environment.exposure=1.5f;settings.shadows.resolution=1024;settings.shadows.normalBias=.3f;
    editor.BeginSceneTransaction(app);Check(app.SetSceneSettings(settings,&error),"scene settings validation");editor.FinishTransaction(app);auto settingsJson=SceneSettingsToJson(settings);editor.UndoEdit(app);Check(SceneSettingsToJson(app.GetSceneSettings())!=settingsJson,"scene settings Undo");editor.RedoEdit(app);Check(SceneSettingsToJson(app.GetSceneSettings())==settingsJson,"scene settings Redo");
    auto original=SceneSerializer::CaptureWorld(world,app.GetSceneSettings());auto editorIdentity=world.GetIdentity();editor.SelectEntity(entity);const auto selected=editor.GetSelectedEntity();
    app.SetEditorPlayMode(true);Check(app.GetWorld().GetIdentity()!=editorIdentity&&app.GetEditorWorld().GetIdentity()==editorIdentity,"separate Runtime World");auto runtimeEntity=app.MapToRuntime(entity);auto runtimeDisposable=app.MapToRuntime(disposable);
    Check(!app.GetWorld().IsAlive(entity)&&app.GetWorld().IsAlive(runtimeEntity),"editor reference rejected in runtime");
    app.GetWorld().GetComponent<ecs::RigidbodyComponent>(runtimeEntity)->velocity={1,0,0};ecs::PhysicsSystem physics;physics.Update(app.GetWorld(),1.f/60);Check(app.GetWorld().GetComponent<ecs::TransformComponent>(runtimeEntity)->position.x!=world.GetComponent<ecs::TransformComponent>(entity)->position.x,"actual physics runtime Transform");physics.ResetState();
    app.GetWorld().GetComponent<ecs::TransformComponent>(runtimeEntity)->position={50,60,70};app.GetWorld().GetComponent<ecs::PointLightComponent>(runtimeEntity)->intensity=40;
    auto created=editor.CreateLight(app,LightType::Spot);Check(app.DestroySceneEntity(runtimeDisposable),"delete during Play");app.GetEnvironmentSettings().exposure=7;
    app.SetEditorPaused(true);auto pausedWorld=app.GetWorld().GetIdentity();Check(app.IsEditorPlayMode()&&app.IsEditorPaused()&&!app.IsSimulationRunning(),"Pause simulation routing");app.SetEditorPaused(false);Check(app.IsSimulationRunning()&&app.GetWorld().GetIdentity()==pausedWorld,"Resume retains runtime");
    app.SetEditorPlayMode(false);Check(app.GetWorld().GetIdentity()==editorIdentity&&SceneSerializer::CaptureWorld(world,app.GetSceneSettings())==original,"CRITICAL Stop restores exact authoring data/settings");Check(world.IsAlive(disposable)&&!world.IsAlive(created)&&!world.IsAlive(runtimeEntity)&&editor.GetSelectedEntity()==selected,"runtime references/selection after Stop");
    for(int i=0;i<20;++i){app.SetEditorPlayMode(true);app.SetEditorPaused(true);app.SetEditorPaused(false);app.SetEditorPlayMode(false);}Check(SceneSerializer::CaptureWorld(world,app.GetSceneSettings())==original,"repeated Play/Stop isolation");
    std::filesystem::create_directories("editor2-readback");auto path=std::filesystem::path("editor2-readback/scene.json");Check(SceneSerializer::SaveWorld(path,world,&error,&settings),"v4 atomic save");ecs::World reopened;SceneSettings reopenedSettings;Check(SceneSerializer::LoadWorld(path,reopened,reopenedSettings,&error)&&SceneSerializer::CaptureWorld(reopened,reopenedSettings)==original,"Close/Reopen equivalent scene");
    auto unchanged=SceneSerializer::CaptureWorld(reopened,reopenedSettings);auto invalid=unchanged;invalid["settings"]["environment"]["asset"]="../escape.hdr";Check(!SceneSerializer::RestoreWorld(invalid,reopened,reopenedSettings,&error)&&SceneSerializer::CaptureWorld(reopened,reopenedSettings)==unchanged,"invalid load is transactional");
    invalid=unchanged;invalid["entities"][0]["components"]["Transform"]["position"]={nullptr,0,0};Check(!SceneSerializer::RestoreWorld(invalid,reopened,reopenedSettings,&error),"invalid numeric scene error");
    Check(!SceneSerializer::WriteAtomic(path,original,&error,false),"Save As overwrite protection");Check(!ecs::ComponentRegistry::ValidateAssetKey("C:/private/file.obj",ecs::AssetKind::Mesh,&error)&&!ecs::ComponentRegistry::ValidateAssetKey("../bad.obj",ecs::AssetKind::Mesh,&error),"asset path validation");
    editor::EditorPreferences prefs;prefs.operation=2;prefs.grid=false;prefs.cameraSpeed=4;prefs.snapping=true;prefs.rotationSnap=30;Check(editor::SavePreferences("editor2-readback/preferences.json",prefs,&error),"preferences save");editor::EditorPreferences loaded;Check(editor::LoadPreferences("editor2-readback/preferences.json",loaded,&error)&&loaded==prefs,"preferences separate roundtrip");Check(!original.contains("panels")&&!original["settings"]["shadows"].contains("debugMode"),"no editor/debug pollution");
    Logger::Get().ClearMessages();Logger::Get().Warn("Editor2 property validation diagnostic");Check(Logger::Get().Messages().size()==1,"Console logger source");Logger::Get().ClearMessages();Check(Logger::Get().Messages().empty(),"Console clear");
    app.NewScene();Check(app.GetWorld().GetAliveCount()==0&&!editor.IsSceneDirty()&&editor.UndoCount()==0,"New Scene/history invalidation");app.Shutdown();
    std::cout<<"Editor2 CPU registry, transactions, v4 settings, references, mutation phases and real Play/Stop isolation passed\n";
}
static void Benchmark(const char* path){
    std::ofstream output(path);Check(output.good(),"editor performance output");output<<"entities,iteration,serializeMs,cloneMs,stopMs,transactionMs\n";
    using Clock=std::chrono::steady_clock;auto ms=[](auto t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();};
    for(int count:{100,500,1000}){
        Application app;auto& ui=app.GetEditorLayer();for(int i=0;i<count;++i){auto e=app.CreateEmptyEntity("Benchmark entity");app.GetWorld().AddComponent<ecs::MeshRendererComponent>(e).meshPath="models/validation_cube.obj";app.GetWorld().AddComponent<ecs::MaterialComponent>(e);}
        auto entity=app.GetWorld().Entities().front();ui.SelectEntity(entity);
        for(int i=0;i<25;++i){auto start=Clock::now();auto document=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());auto serialization=ms(start);Check(int(document["entities"].size())==count,"serialization entity count");
            app.SetEditorPlayMode(true);double clone=app.GetLastWorldCloneMs();start=Clock::now();app.SetEditorPlayMode(false);auto stop=ms(start);
            start=Clock::now();ui.RecordEdit(app,"Coalesced drag");for(int step=0;step<100;++step)ui.SetProperty(app,entity,ecs::ComponentId("Transform"),"position",ecs::Vec3{float(i*100+step),0,0},false);ui.FinishTransaction(app);auto transaction=ms(start);
            output<<count<<','<<i<<','<<serialization<<','<<clone<<','<<stop<<','<<transaction<<'\n';
        }app.Shutdown();
    }
}
#if defined(ENABLE_DX12)
static void Gpu(){
    Application app;Check(app.Initialize(),"real DX12 Application initialization");auto* renderer=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());Check(renderer,"DX12 active renderer");
    auto* window=dynamic_cast<GlfwWindow*>(app.GetWindow());Check(window,"GLFW window");glfwHideWindow(window->GetGlfwHandle());
    app.SetupPhysicsStressScene(500);app.SetEditorPlayMode(false);app.SetGpuInstancingEnabled(true);app.GetEditorLayer().SetLightGizmosVisible(false);
    app.SetFrameLimit(120);Check(app.Run()==0,"Edit rendering/UI lifecycle");Check(app.GetRenderStatistics().submitted.renderedInstances==501&&app.GetRenderStatistics().submitted.instancedDrawCalls>0,"500 cube batching through editor");
    auto pixels=renderer->ReadBackViewportPixels();Check(pixels.size()>40000,"DX12 viewport GPU readback");unsigned varied=0;for(std::size_t i=4;i<pixels.size();i+=4)if(pixels[i]!=pixels[0]||pixels[i+1]!=pixels[1])++varied;Check(varied>100,"actual scene pixels in editor viewport");
    auto mesh=app.GetResourceManager()->Get<MeshResource>("models/validation_cube.obj");Check(mesh&&mesh->IsLoaded(),"editor cached mesh");auto version=mesh->GetVersion();auto reloaded=app.GetResourceManager()->Reload<MeshResource>("models/validation_cube.obj");Check(reloaded==mesh&&mesh->GetVersion()>version,"Resource reload shared handle/version");app.SetFrameLimit(20);Check(app.Run()==0,"render after mesh GPU cache reload");
    auto invalid=app.GetEditorLayer().CreateEntity(app,"Missing asset");Check(app.GetEditorLayer().AddComponent(app,invalid,ecs::ComponentId("MeshRenderer")),"missing asset entity");Check(app.GetEditorLayer().SetProperty(app,invalid,ecs::ComponentId("MeshRenderer"),"meshPath",std::string("models/editor2_missing.obj")),"missing relative asset accepted for async resolution");app.SetFrameLimit(40);Check(app.Run()==0,"missing asset fallback UI/render stability");auto missing=app.GetResourceManager()->Get<MeshResource>("models/editor2_missing.obj");Check(missing&&missing->IsFailed(),"missing asset reports failure");app.GetEditorLayer().DeleteEntity(app,invalid);
    auto& editor=app.GetEditorLayer();auto entity=app.GetWorld().Entities().at(1);editor.SelectEntity(entity);auto before=SceneSerializer::CaptureWorld(app.GetEditorWorld(),app.GetSceneSettings());auto identity=app.GetWorld().GetIdentity();
    app.SetEditorPlayMode(true);auto runtimeEntity=app.MapToRuntime(entity);app.GetWorld().GetComponent<ecs::RigidbodyComponent>(runtimeEntity)->velocity={3,0,0};auto start=app.GetWorld().GetComponent<ecs::TransformComponent>(runtimeEntity)->position;
    app.SetDiagnosticSlowFrame(1,60);app.SetFrameLimit(80);Check(app.Run()==0,"owned physics + Runtime rendering");auto moved=app.GetWorld().GetComponent<ecs::TransformComponent>(runtimeEntity)->position;Check(moved.x!=start.x||moved.y!=start.y,"owned PhysicsSystem changes Runtime Transform");
    editor.CreateLight(app,LightType::Spot);auto extra=editor.CreateEntity(app,"Runtime-only");editor.DeleteEntity(app,runtimeEntity);editor.UndoEdit(app);auto restored=editor.GetSelectedEntity();Check(app.GetWorld().IsAlive(restored)&&restored!=runtimeEntity,"GPU runtime Delete/Undo generation");
    app.SetEditorPaused(true);auto paused=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());app.SetFrameLimit(40);Check(app.Run()==0&&SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==paused,"Pause freezes owned physics while rendering");
    app.SetEditorPaused(false);app.SetEditorPlayMode(false);Check(app.GetWorld().GetIdentity()==identity&&SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==before&&!app.GetWorld().IsAlive(extra),"DX12 CRITICAL authoring scene isolation");
    std::string error;const std::string key="scenes/editor2_integration.json";Check(app.SaveSceneAs(key,true,&error),"Application atomic Save As");auto path=AssetPaths::ResolveAssetOutputPath(key);
    Check(!app.SaveSceneAs(key,false,&error)&&!app.SaveSceneAs("../escape.json",false,&error),"Application overwrite/path rejection");
    for(int cycle=0;cycle<3;++cycle){app.SetEditorPlayMode(true);app.SetFrameLimit(20);Check(app.Run()==0,"repeated Runtime GPU frames");app.SetEditorPaused(true);app.SetEditorPlayMode(false);Check(app.OpenScene(key,&error),"scene reload replaces references");glfwSetWindowSize(window->GetGlfwHandle(),960+cycle*80,640);app.SetFrameLimit(20);Check(app.Run()==0,"viewport resize after scene reload");}
    Check(!app.GetWorld().IsAlive(entity),"reload invalidates old scene handle");
    renderer->BeginFrame();renderer->Clear(0,0,0,1);app.GetEditorWorld().UpdateFrameSystemsOn(app.GetWorld(),0);app.NewScene();renderer->EndFrame();renderer->Present();
    app.SetFrameLimit(20);Check(app.Run()==0&&app.GetRenderStatistics().submitted.drawCalls==0,"destroy before submit / empty scene after Clear/New");
    Check(renderer->GetValidationErrorCount()==0,"DX12 debug ERROR/CORRUPTION");app.Shutdown();std::filesystem::remove(path);
    std::cout<<"Editor2 DX12 viewport readback, owned physics isolation, batching, Pause/Resume, reload/resize, empty scene and debug validation passed\n";
}
#endif
int main(int argc,char** argv){try{ScopedEditorPreferenceRestore preservePreferences;if(argc>2&&std::string(argv[1])=="--benchmark"){Benchmark(argv[2]);return 0;}Cpu();
#if defined(ENABLE_DX12)
    if(argc>1&&std::string(argv[1])=="--dx12")Gpu();
#endif
    return 0;}catch(const std::exception& e){std::cerr<<"EDITOR2 FAILED: "<<e.what()<<'\n';return 1;}}
