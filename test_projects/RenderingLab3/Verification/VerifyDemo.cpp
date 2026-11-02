#include <thread>
#define NOMINMAX
#include "EditorTestPreferences.h"
#include "SyntheticEditorInput.h"
#include "core/Application.h"
#include "core/AssetPaths.h"
#include "resources/ResourceManager.h"
#include "resources/loaders/MaterialLoader.h"
#include "scene/SceneSerializer.h"
#include "scripting/Prefab.h"
#include "ecs/components/TagComponent.h"
#include "ecs/components/SceneIdentityComponent.h"
#include "ecs/components/MaterialComponent.h"
#include "platform/GlfwWindow.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include <GLFW/glfw3.h>
#include <imgui_internal.h>
#include <fstream>
#include <iostream>
#include <map>
namespace fs=std::filesystem;
namespace editor {struct EditorDocumentTestAccess {
    static void Edit(EditorLayer& e,Application& a,const std::function<void(MaterialResource&)>& edit){auto* s=e.EnsureMaterialSession(a);if(!s)throw std::runtime_error("Material session unavailable");s->transaction=s->draft;edit(s->draft);e.FinishMaterialEdit(a);}
    static bool Save(EditorLayer& e,Application& a){return e.SaveMaterialDraft(a);}
    static auto Image(const EditorLayer& e){return e.m_MaterialPreviewImage;}
    static bool Reload(EditorLayer& e,Application& a){return e.ReloadMaterialDraft(a);}
    static float DraftRed(EditorLayer& e,Application& a){return e.EnsureMaterialSession(a)->draft.baseColor[0];}
    static auto& Draft(EditorLayer& e,Application& a){return e.EnsureMaterialSession(a)->draft;}
    static auto ResolutionError(EditorLayer& e,Application& a){return e.EnsureMaterialSession(a)->resolutionError;}
};}
static unsigned checks=0;
static void Check(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);std::cout<<++checks<<": "<<message<<'\n';}
static std::string Bytes(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static fs::path Source(){for(auto p=AssetPaths::GetExecutableDirectory();!p.empty();p=p.parent_path()){auto candidate=p/"test_projects/RenderingLab3/RenderingLab3.whispproject";if(fs::is_regular_file(candidate))return candidate.parent_path();if(p==p.parent_path())break;}throw std::runtime_error("Cannot locate RenderingLab3 fixture");}
int main(){ScopedEditorPreferenceRestore preferences;try{
    const auto source=Source(),output=AssetPaths::GetExecutableDirectory();const auto copy=output/("run-"+ecs::NewSceneIdentity());fs::create_directory(copy);
    std::map<fs::path,std::string> original;
    for(const auto& file:fs::recursive_directory_iterator(source))if(file.is_regular_file()){
        original[file.path()]=Bytes(file.path());const auto relative=fs::relative(file.path(),source);
        if(*relative.begin()=="Verification"||file.path().extension()==".wmesh"||file.path().extension()==".wtex")continue;
        fs::create_directories((copy/relative).parent_path());fs::copy_file(file.path(),copy/relative);
    }
    Application app;Check(app.Initialize(Application::StartupMode::ProjectBrowser),"native DX12 editor initializes");SyntheticEditorInput input;
    Check(app.GetRenderAdapter()->GetPipelineCacheDiagnostics().creations==0,"no mesh PSO creations before opening project");
    glfwHideWindow(dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle());auto& ui=app.GetEditorLayer();
    ui.SetFixedDiagnosticLayout(true);ui.SetDiagnosticViewport(960,540);ui.SetGameView(true);ui.SetGridVisible(false);ui.SetUIPreviewVisible(true);
    std::string error;Check(app.OpenProject(copy/"RenderingLab3.whispproject",Application::ProjectDecision::Discard,&error),"demo opens: "+error);
    Check(app.GetSceneKey()=="Scenes/MaterialSystem.json","startup scene selected");app.SetEditorCameraPose({0,0,-2.25f},0,0);
    auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());Check(gpu!=nullptr,"DX12 adapter selected");auto* rm=app.GetResourceManager();
    auto frame=[&](unsigned n=6){app.SetFrameLimit(n);Check(app.Run()==0,"editor frame loop");};
    auto named=[&](const std::string& name){for(auto e:app.GetWorld().Entities())if(auto* tag=app.GetWorld().GetComponent<ecs::TagComponent>(e);tag&&tag->name.starts_with(name))return e;throw std::runtime_error("Missing demo entity: "+name);};
    auto mat=[&](const std::string& name)->ecs::MaterialComponent&{return *app.GetWorld().GetComponent<ecs::MaterialComponent>(named(name));};
    auto sceneId=[&](){for(auto& d:ui.Documents())if(d.key=="Scenes/MaterialSystem.json")return d.id;throw std::runtime_error("Missing Scene document");};
    auto pixels=[&](){auto p=gpu->ReadBackViewportPixels();Check(p.size()==960*540*4,"actual scene GPU readback");return p;};
    auto save=[&](const char* name,const auto& p){std::ofstream f(output/name,std::ios::binary);f<<"P6\n960 540\n255\n";for(size_t i=0;i<p.size();i+=4)f.write(reinterpret_cast<const char*>(p.data()+i),3);};
    frame(30);Check(app.GetRenderStatistics().submitted.renderedInstances>=9,"four spheres, pedestals and stage submitted");
    auto baseline=pixels();save("scene.ppm",baseline);
    Check(mat("A -").materialPath==mat("B -").materialPath&&mat("A -").materialPath==mat("C -").materialPath,"A/B/C reference shared material");
    Check(mat("B -").tint[0]<.2f&&mat("C -").overrides.roughness==.85f,"authored Tint and Roughness overrides");
    const auto warm=gpu->GetPipelineCacheDiagnostics();Check(warm.creations>0,"opening demo creates mesh PSOs");std::cout<<"PSO after initial preparation: creations="<<warm.creations<<" cached="<<warm.cachedCount<<'\n';frame(10);Check(gpu->GetPipelineCacheDiagnostics().creations==warm.creations&&gpu->GetPipelineCacheDiagnostics().hits>warm.hits,"PSO creations stable and hits increase after warmup");
    ui.SetUIPreviewVisible(false);frame();const auto unlabelled=pixels();
    // Increment A: none of these materials has been opened in Material Editor.
    auto settle=[&](){frame(2);std::this_thread::sleep_for(std::chrono::milliseconds(150));frame(8);};
    const std::string baseKey="materials/demo_base.material.json";
    auto liveBase=rm->Get<MaterialResource>(baseKey);const auto originalBase=liveBase->GetData();
    auto writeBase=[&](const MaterialResource& data){Check(MaterialLoader::Save(baseKey,data,&error),"atomic external material save");};
    auto prefabInstance=scripting::SpawnPrefab(app.GetWorld(),"prefabs/demo_roughness.prefab.json",{4,0,5});
    mat("B -").slotMaterials={baseKey};
    auto external=originalBase;external.baseColor[0]=.05f;writeBase(external);settle();
    Check(liveBase->GetData().baseColor[0]==.05f&&pixels()!=unlabelled,"scene/slot/Prefab materials auto reload without opening Material Editor");
    Check(mat("C -").overrides.roughness==.85f,"entity roughness override survives automatic base reload");
    auto savedPrefabScene=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());Check(app.SaveCurrentScene(&error)&&app.OpenScene("Scenes/MaterialSystem.json",&error),"Scene/Prefab instances save and reload with hot materials");
    Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==savedPrefabScene,"Prefab material instance scene roundtrip");
    for(auto entity:app.GetWorld().Entities())if(auto* tag=app.GetWorld().GetComponent<ecs::TagComponent>(entity);tag&&tag->name.starts_with("C - Prefab"))app.GetWorld().DestroyEntity(entity);
    mat("B -").slotMaterials.clear();writeBase(originalBase);settle();Check(pixels()==unlabelled,"automatic base recovery restores GPU output");
    const auto beforeBurst=liveBase->GetVersion();
    for(int n=0;n<5;++n){external=originalBase;external.roughness=.31f+n*.01f;writeBase(external);}
    settle();Check(liveBase->GetVersion()==beforeBurst+1&&std::abs(liveBase->GetData().roughness-.35f)<.0001f,"debounce coalesces rapid atomic saves");
    {std::ofstream bad(copy/"Assets/materials/demo_base.material.json");bad<<"{malformed";}settle();
    const auto failedVersion=liveBase->GetVersion();Check(liveBase->HasError()&&liveBase->IsUsable()&&std::abs(liveBase->GetData().roughness-.35f)<.0001f,"malformed JSON preserves last-good material");
    frame(12);Check(liveBase->GetVersion()==failedVersion,"malformed JSON is not retried each frame");
    writeBase(originalBase);settle();Check(!liveBase->HasError()&&pixels()==unlabelled,"repair JSON automatically restores good material");
    unsigned char bmp[58]={0x42,0x4d,58,0,0,0,0,0,0,0,54,0,0,0,40,0,0,0,1,0,0,0,1,0,0,0,1,0,24};bmp[34]=4;bmp[54]=230;bmp[55]=80;bmp[56]=30;
    {std::ofstream image(copy/"Assets/textures/owned-blue.bmp",std::ios::binary);image.write(reinterpret_cast<char*>(bmp),58);}
    external=originalBase;external.texturePath="textures/owned-blue.bmp";writeBase(external);settle();
    Check(rm->Get<TextureResource>(external.texturePath)&&pixels()!=unlabelled,"changed texture reference loads new GPU dependency automatically");
    external.texturePath="textures/auto-missing.png";writeBase(external);settle();Check(rm->Get<TextureResource>(external.texturePath)->HasError(),"automatic missing texture diagnostic");
    writeBase(originalBase);settle();Check(gpu->GetPipelineCacheDiagnostics().creations==warm.creations,"automatic material constants/textures never rebuild PSO");
    const auto resourcePlateau=gpu->GetLiveGpuResourceCounts();
    for(int n=0;n<48;++n){external=originalBase;external.roughness=n%2?.25f:.45f;writeBase(external);settle();}
    Check(gpu->GetLiveGpuResourceCounts()==resourcePlateau&&gpu->GetPipelineCacheDiagnostics().creations==warm.creations,"48 reload cycles keep live GPU resource counts and PSO creations bounded");
    writeBase(originalBase);settle();Check(pixels()==unlabelled,"long reload sequence restores exact pixels");
    int otherOwner=0;rm->SetMaterialUsage(&otherOwner,{"materials/demo_alternate.material.json"});const auto watched=rm->MaterialWatchCount();
    app.GetWorld().GetComponent<ecs::MeshRendererComponent>(named("D -"))->visible=false;frame();Check(rm->MaterialWatchCount()==watched,"another watch owner keeps unused scene dependency alive");
    rm->ClearMaterialUsage(&otherOwner);Check(rm->MaterialWatchCount()<watched,"unused material/texture watches are released");
    app.GetWorld().GetComponent<ecs::MeshRendererComponent>(named("D -"))->visible=true;frame();
    std::cout<<"INCREMENT A: automatic hot reload, debounce, last-good and watch ownership passed\n";
    Check(ui.SetProperty(app,named("C -"),ecs::ComponentId("Material"),"override.roughness",.18f),"Inspector Roughness override edit");frame();Check(pixels()!=unlabelled,"Roughness edit changes actual scene GPU output");
    Check(ui.SetProperty(app,named("C -"),ecs::ComponentId("Material"),"override.roughness",ecs::PropertyValue{}),"Inspector Inherit clears optional override");Check(!mat("C -").overrides.roughness,"Inherit represented by nullopt");
    Check(ui.SetProperty(app,named("C -"),ecs::ComponentId("Material"),"override.roughness",.85f),"restore rough instance");
    frame();Check(gpu->GetPipelineCacheDiagnostics().creations==warm.creations,"instance constants do not create PSOs");
    const auto authored=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());Check(app.SaveCurrentScene(&error),"Scene Save: "+error);
    Check(app.OpenScene("Scenes/MaterialSystem.json",&error),"Scene Reload: "+error);frame();Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==authored,"Scene save/reload preserves overrides and entities");
    auto prefab=scripting::ReadPrefab("prefabs/demo_roughness.prefab.json");Check(prefab.at("entities").at(0).at("components").at("Material").at("overrides").at("roughness")==.85,"Prefab schema and material override resolve");
    ui.SelectEntity(named("A -"));Check(ui.OpenDocument(app,"materials/demo_base.material.json"),"Material Editor opens shared asset");frame(15);Check(editor::EditorDocumentTestAccess::Image(ui)!=0,"Material Editor renders native preview");
    editor::EditorDocumentTestAccess::Edit(ui,app,[](auto& d){d.baseColor[0]=.13f;});external=originalBase;external.roughness=.61f;writeBase(external);settle();
    Check(liveBase->GetData().roughness==originalBase.roughness&&editor::EditorDocumentTestAccess::DraftRed(ui,app)==.13f,"external reload preserves dirty editor draft and live last-good asset");
    Check(editor::EditorDocumentTestAccess::Reload(ui,app),"explicit Revert resolves external dirty conflict");writeBase(originalBase);settle();
    editor::EditorDocumentTestAccess::Edit(ui,app,[](auto& d){d.baseColor[0]=.12f;d.baseColor[1]=.8f;});Check(editor::EditorDocumentTestAccess::Save(ui,app),"Material Editor Save publishes draft");Check(ui.ActivateDocument(app,sceneId()),"return to Scene");frame();const auto changed=pixels();Check(changed!=unlabelled,"Material Save changes actual scene GPU pixels");save("base-edited.ppm",changed);
    for(unsigned object=0;object<4;++object){const int cx=int(480+(-2.7+1.8*object)*540*.8660254/7.25);unsigned differences=0;for(int y=250;y<290;++y)for(int x=cx-20;x<cx+20;++x)for(int c=0;c<3;++c)differences+=changed[(y*960+x)*4+c]!=unlabelled[(y*960+x)*4+c];Check(object<3?differences>0:differences==0,object<3?"dependent sphere GPU region updates":"D GPU region stays unchanged after base asset Save");}
    auto base=rm->Get<MaterialResource>("materials/demo_base.material.json");auto restore=MaterialLoader::Load("materials/demo_base.material.json",copy/"Recovery/demo_base.material.json");Check(restore.success,"base recovery fixture parses");Check(MaterialLoader::Save(base->GetKey(),restore.data,&error),"restore owned base material");rm->Reload<MaterialResource>(base->GetKey());frame();Check(pixels()==unlabelled,"base restore returns exact scene pixels");
    Check(ui.CreateMaterialAsset(app,"materials/demo_created.material.json",&error),"Asset Browser material creation: "+error);Check(ui.AssignAsset(app,named("D -"),ecs::AssetKind::Material,"materials/demo_created.material.json"),"Asset Browser assigns material to D");frame(12);Check(pixels()!=unlabelled,"new material assignment affects GPU output");
    Check(ui.AssignAsset(app,named("D -"),ecs::AssetKind::Material,"materials/demo_alternate.material.json"),"restore alternate asset");frame(12);
    Check(ui.OpenDocument(app,"materials/demo_alternate.material.json"),"open D material and register watches");frame(15);Check(ui.ActivateDocument(app,sceneId()),"return to Scene for error demo");frame();
    auto alternate=rm->Get<MaterialResource>("materials/demo_alternate.material.json");auto missing=alternate->GetData();missing.texturePath="textures/INTENTIONALLY_MISSING.png";Check(MaterialLoader::Save(alternate->GetKey(),missing,&error),"write missing texture reference only in owned demo copy");rm->PollHotReload();std::this_thread::sleep_for(std::chrono::milliseconds(140));rm->PollHotReload();frame(20);
    auto missingResource=rm->Get<TextureResource>(missing.texturePath);Check(missingResource&&missingResource->HasError()&&missingResource->IsUsable(),"Missing Texture has diagnostic and initialized fallback");Check(pixels()!=unlabelled,"Missing Texture visibly uses fallback");save("missing-texture.ppm",pixels());
    fs::copy_file(copy/"Recovery/demo_alternate.material.json",copy/"Assets/materials/demo_alternate.material.json",fs::copy_options::overwrite_existing);rm->PollHotReload();std::this_thread::sleep_for(std::chrono::milliseconds(140));rm->PollHotReload();frame(12);Check(pixels()==unlabelled,"texture reference recovery restores pixels");
    auto shader=rm->Get<ShaderResource>("shaders/demo_surface.hlsl");Check(shader&&shader->GetData().gpuHandle.IsValid(),"project-local shader prepared");const auto old=shader->GetData().gpuHandle;const auto beforeFailure=gpu->GetPipelineCacheDiagnostics();
    {std::ofstream file(copy/"Assets/shaders/demo_surface.hlsl");file<<"invalid HLSL";}rm->PollHotReload();std::this_thread::sleep_for(std::chrono::milliseconds(140));rm->PollHotReload();frame(10);
    Check(shader->GetData().gpuHandle==old&&!shader->GetData().gpuError.empty(),"Invalid Shader Reload retains last-good GPU shader");Check(pixels()==unlabelled,"Invalid Shader Reload preserves exact scene GPU output");Check(gpu->GetPipelineCacheDiagnostics().creations==beforeFailure.creations,"failed shader creates no PSOs");save("shader-failed.ppm",pixels());
    fs::copy_file(copy/"Recovery/demo_surface.hlsl.good",copy/"Assets/shaders/demo_surface.hlsl",fs::copy_options::overwrite_existing);rm->PollHotReload();std::this_thread::sleep_for(std::chrono::milliseconds(140));rm->PollHotReload();frame(12);
    Check(shader->GetData().gpuHandle!=old&&shader->GetData().gpuError.empty(),"repaired watched shader publishes replacement");Check(gpu->GetPipelineCacheDiagnostics().creations>beforeFailure.creations,"successful shader reload recreates affected PSOs");Check(pixels()==unlabelled,"shader repair restores same output");
    Check(app.OpenScene("Scenes/Inheritance.json",&error),"open separate inheritance demo without replacing original scene");frame(15);
    const std::string childKey="materials/inheritance_child.material.json",grandKey="materials/inheritance_grandchild.material.json";
    const auto inheritancePixels=pixels();save("inheritance.ppm",inheritancePixels);const auto inheritanceGpu=gpu->GetLiveGpuResourceCounts();const auto inheritancePso=gpu->GetPipelineCacheDiagnostics().creations;
    auto childResolved=rm->ResolveMaterial(childKey),grandResolved=rm->ResolveMaterial(grandKey);
    Check(childResolved.success&&grandResolved.success&&childResolved.data.roughness==.85f&&grandResolved.data.metallic==.65f&&grandResolved.data.baseColor[0]==.85f,"Base > Child > Grandchild resolves sparse data");
    external=originalBase;external.metallic=.1f;external.roughness=.42f;writeBase(external);settle();
    Check(rm->ResolveMaterial(grandKey).data.metallic==.1f&&rm->ResolveMaterial(grandKey).data.roughness==.85f,"base change propagates through two levels while child override blocks roughness");
    Check(pixels()!=inheritancePixels&&gpu->GetPipelineCacheDiagnostics().creations==inheritancePso,"inheritance changes real GPU pixels without PSO creation");
    Check(ui.OpenDocument(app,childKey),"open child in existing Material Editor");frame(12);Check(editor::EditorDocumentTestAccess::Image(ui)!=0,"resolved child preview renders");
    auto* properties=ImGui::FindWindowByName("Material Editor");properties->StateStorage.SetInt(ImHashStr("Surface",0,properties->ID),0);frame();
    const auto overrideId=ImHashStr("##Override",0,ImHashStr("Roughness",0,ImHashStr("Properties",0,ImHashStr("Parameters",0,properties->ID))));
    ImGui::SetWindowFocus("Material Editor");ImGui::ActivateItemByID(overrideId);frame(2);
    Check(!editor::EditorDocumentTestAccess::Draft(ui,app).parameterOverrides.contains("roughness"),"native Override checkbox resets to Inherit");
    Check(editor::EditorDocumentTestAccess::Save(ui,app),"save sparse child after native reset");
    Check(rm->ResolveMaterial(grandKey).data.roughness==.42f,"grandchild inherits parent after child reset");
    {std::ifstream file(copy/"Assets/materials/inheritance_child.material.json");nlohmann::json j;file>>j;Check(j.at("version")==5&&j.at("overrides").empty()&&!j.contains("roughness"),"saved child is not flattened");}
    editor::EditorDocumentTestAccess::Edit(ui,app,[](auto& d){d.parameterOverrides["roughness"]=.73f;});frame();Check(editor::EditorDocumentTestAccess::Save(ui,app),"edit child explicit parameter and Save");
    editor::EditorDocumentTestAccess::Edit(ui,app,[](auto& d){d.parentMaterial="materials/inheritance_grandchild.material.json";});frame();
    Check(editor::EditorDocumentTestAccess::ResolutionError(ui,app).find("cycle")!=std::string::npos&&!editor::EditorDocumentTestAccess::Save(ui,app),"editor cycle warning blocks invalid child Save");
    editor::EditorDocumentTestAccess::Edit(ui,app,[](auto& d){d.parentMaterial="materials/not-present-parent.material.json";});frame();
    Check(!editor::EditorDocumentTestAccess::ResolutionError(ui,app).empty(),"editor missing Parent warning");
    editor::EditorDocumentTestAccess::Edit(ui,app,[](auto& d){d.parentMaterial="materials/demo_alternate.material.json";});frame();Check(editor::EditorDocumentTestAccess::Save(ui,app),"Parent selector transaction publishes another valid parent");
    Check(rm->ResolveMaterial(childKey).data.texturePath=="textures/demo_surface.png"&&rm->ResolveMaterial(childKey).data.roughness==.73f,"new parent texture inherits while child roughness remains explicit");
    for(const auto& d:ui.Documents())if(d.key=="Scenes/Inheritance.json"){Check(ui.ActivateDocument(app,d.id),"return to inheritance Scene");break;}frame(12);
    const auto beforeCycle=pixels();auto childResource=rm->Get<MaterialResource>(childKey);auto cycle=childResource->GetData();cycle.parentMaterial=grandKey;Check(MaterialLoader::Save(childKey,cycle,&error),"write indirect cycle only in owned demo copy");settle();
    Check(!rm->ResolveMaterial(childKey).success&&!rm->ResolveMaterial(grandKey).success&&pixels()==beforeCycle,"on-disk cycle keeps last-good resolved GPU output");
    fs::copy_file(copy/"Recovery/inheritance_child.material.json",copy/"Assets/materials/inheritance_child.material.json",fs::copy_options::overwrite_existing);writeBase(originalBase);settle();
    Check(rm->ResolveMaterial(grandKey).success,"inheritance repair automatically clears cycle diagnostic");
    Check(ui.SetProperty(app,named("G -"),ecs::ComponentId("Material"),"override.roughness",.09f),"Entity overrides apply after asset inheritance");frame();
    auto inheritedScene=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());Check(app.SaveCurrentScene(&error)&&app.OpenScene("Scenes/Inheritance.json",&error),"save and reload inheritance Scene");frame();
    Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==inheritedScene,"inherited asset references and Entity overrides persist");
    Check(gpu->GetLiveGpuResourceCounts()==inheritanceGpu&&gpu->GetPipelineCacheDiagnostics().creations==inheritancePso,"children and grandchild reuse GPU allocations and compatible PSOs");
    // Re-upload the same valid image repeatedly, then rebuild the shader through
    // real file events. Exercise retirement, not merely constant-buffer changes.
    const auto textureBytes=Bytes(copy/"Assets/textures/demo_surface.png");
    auto trackedTexture=rm->Get<TextureResource>("textures/demo_surface.png");
    const auto allocationPlateau=gpu->GetLiveGpuResourceCounts();const auto cachedPlateau=gpu->GetPipelineCacheDiagnostics().cachedCount;
    for(int n=0;n<6;++n){const auto previous=trackedTexture->GetData().gpuHandle;{std::ofstream file(copy/"Assets/textures/demo_surface.png",std::ios::binary);file.write(textureBytes.data(),textureBytes.size());}settle();Check(trackedTexture->GetData().gpuHandle!=previous,"repeated file-event texture reload really replaces GPU allocation");Check(gpu->GetLiveGpuResourceCounts()==allocationPlateau,"texture retirement keeps live GPU allocations bounded");}
    const auto shaderBytes=Bytes(copy/"Recovery/demo_surface.hlsl.good");
    for(int n=0;n<4;++n){const auto previous=shader->GetData().gpuHandle;{std::ofstream file(copy/"Assets/shaders/demo_surface.hlsl");file<<shaderBytes<<"\n// reload stress "<<n<<'\n';}settle();Check(shader->GetData().gpuHandle!=previous,"repeated shader file events publish new GPU generations");Check(gpu->GetLiveGpuResourceCounts()==allocationPlateau&&gpu->GetPipelineCacheDiagnostics().cachedCount==cachedPlateau,"shader retirement keeps live allocations and PSO cache size bounded");}
    std::cout<<"INCREMENT B: inheritance, native editor reset, sparse Save, cycle fallback and GPU reuse passed\n";
    const auto cache=gpu->GetPipelineCacheDiagnostics();std::cout<<"PSO hits="<<cache.hits<<" misses="<<cache.misses<<" creations="<<cache.creations<<" cached="<<cache.cachedCount<<'\n';
    Check(gpu->GetValidationErrorCount()==0,"DX12 validation errors = 0");app.Shutdown();
    for(const auto& [path,bytes]:original)Check(Bytes(path)==bytes,"source demo file unchanged: "+AssetPaths::ToUtf8String(fs::relative(path,source)));
    std::cout<<"RenderingLab3: "<<checks<<" checks passed; artifacts="<<AssetPaths::ToUtf8String(output)<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"RenderingLab3 failed after "<<checks<<": "<<e.what()<<'\n';return 1;}}
