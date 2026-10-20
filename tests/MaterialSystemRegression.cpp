#include "render/ColorPipeline.h"
#define NOMINMAX
#include "resources/ResourceManager.h"
#include "resources/MaterialSerialization.h"
#include "ecs/systems/RenderSystem.h"
#include "render/IRenderAdapter.h"
#include "ecs/PhysicsStressScene.h"
#include "scene/SceneSerializer.h"
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <cstring>
#if defined(ENABLE_DX12)
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include "platform/GlfwWindow.h"
#endif
using namespace ecs;
static void Check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
static void Passed(const char* message) {std::cout<<"PASS: "<<message<<'\n';}
struct Recorder:IRenderAdapter {
    struct Draw {RenderMeshRange range;MaterialRenderState state;std::vector<RenderInstanceData> instances;};
    std::vector<Draw> main,shadow;RenderInstanceData pending;RenderMeshRange range;MaterialRenderState state;
    RenderSubmissionStatistics stats;std::uint64_t next=1;bool failInstance=false;
    bool Initialize(IWindow*)override{return true;}void BeginFrame()override{main.clear();shadow.clear();stats={};}
    void Clear(float,float,float,float)override{}void EndFrame()override{}void Present()override{}void Shutdown()override{}
    void SetTestTransform(const float* m)override{std::copy_n(m,16,pending.mvp.begin());}
    void SetModelMatrix(const std::array<float,16>& m)override{pending.model=m;}
    void SetBaseColor(const std::array<float,4>& c)override{pending.baseColor=c;}
    void SetSurfaceParameters(const std::array<float,4>& c)override{pending.surface=c;}
    void SetNormalMatrix(const std::array<float,12>& m)override{pending.normal=m;}
    void SetTestColor(float r,float g,float b,float a)override{pending.tint={r,g,b,a};}
    void SetMeshRange(const RenderMeshRange& r)override{range=r;}void SetMaterialState(const MaterialRenderState& s)override{state=s;}
    RenderMeshHandle UploadMesh(const MeshData&)override{return {next++};}
    RenderTextureHandle CreateTexture2D(const TextureData&)override{return {next++};}
    RenderShaderHandle CreateShaderProgram(const ShaderResource&)override{return {next++};}
    void BindShader(RenderShaderHandle)override{}void BindTexture(std::uint32_t,RenderTextureHandle)override{}
    bool SupportsInstancing(RenderShaderHandle)const override{return true;}
    void DrawMesh(RenderMeshHandle)override{main.push_back({range,state,{pending}});++stats.drawCalls;++stats.renderedInstances;}
    bool DrawMeshInstanced(RenderMeshHandle,std::span<const RenderInstanceData> d)override{if(failInstance)return false;main.push_back({range,state,{d.begin(),d.end()}});++stats.drawCalls;++stats.instancedDrawCalls;stats.renderedInstances+=d.size();return true;}
    bool BeginShadowPass(const ShadowFrame& f)override{return f.constants.parameters[0]>.5f;}
    void DrawShadowMesh(RenderMeshHandle,const RenderInstanceData& d)override{shadow.push_back({range,state,{d}});++stats.shadowDrawCalls;++stats.shadowCasters;}
    bool DrawShadowMeshInstanced(RenderMeshHandle,std::span<const RenderInstanceData> d)override{if(failInstance)return false;shadow.push_back({range,state,{d.begin(),d.end()}});++stats.shadowDrawCalls;++stats.shadowInstancedDrawCalls;stats.shadowCasters+=d.size();return true;}
    RenderSubmissionStatistics GetSubmissionStatistics()const override{return stats;}
    void EndShadowPass()override{}void DrawTestTriangle()override{}void DrawTestLine()override{}void DrawTestQuad()override{}void DrawTestCube()override{}
};
static void Schema() {
    auto legacy=MaterialLoader::Parse("materials/a.json",{{"baseColor",{.5,.6,.7,1}}});Check(legacy.success&&legacy.data.castShadows&&!legacy.data.doubleSided,"legacy defaults");Passed("legacy schema and defaults");
    for(auto bad:{nlohmann::json{{"version",5}},nlohmann::json{{"baseColor",{"bad",0,0,1}}},nlohmann::json{{"baseColor",{2,0,0,1}}},nlohmann::json{{"doubleSided",4}},nlohmann::json{{"alphaCutoff",-1}},nlohmann::json{{"surfaceMode","unknown"}}})Check(!MaterialLoader::Parse("materials/a.json",bad).success,"invalid schema accepted");Passed("malformed types, values and future versions rejected");
    for(const char* path:{"../escape.png","C:/escape.png","https://example.com/texture.png"})Check(!MaterialLoader::Parse("materials/a.json",{{"texturePath",path}}).success,"unsafe path accepted");Passed("resource path validation");
    auto unknown=MaterialLoader::Parse("materials/a.json",{{"future",1}});Check(unknown.success&&!unknown.data.diagnostics.empty(),"unknown field diagnostic");Passed("unknown fields diagnosed");
    for(const char* mode:{"Masked","Transparent"}) {auto m=MaterialLoader::Parse("materials/a.json",{{"surfaceMode",mode}});Check(m.success&&m.data.surfaceMode!=MaterialSurfaceMode::Opaque&&!m.data.diagnostics.empty(),"unsupported mode silently opaque");}Passed("unsupported alpha modes preserved and diagnosed");
    MaterialResource m;m.name="Roundtrip";m.baseColor[0]=.37f;m.doubleSided=true;m.castShadows=false;m.receiveShadows=false;m.placeholder=false;
    const std::string key="materials/material-regression-temporary.material.json";std::string error;
    if(!MaterialLoader::Save(key,m,&error))throw std::runtime_error(error);ResourceManager resources(nullptr);resources.Load<MaterialResource>("materials/stress.material.json");auto a=resources.Load<MaterialResource>(key);Check(a->IsLoaded()&&a->GetData().baseColor[0]==.37f&&a->GetData().doubleSided&&!a->GetData().castShadows,"asset roundtrip");Passed("create, atomic save and load");
    Check(a==resources.Load<MaterialResource>(key),"material cache sharing");Passed("shared material cache identity");
    m.baseColor[0]=.71f;Check(MaterialLoader::Save(key,m,&error),"replace save");const auto version=a->GetVersion();Check(resources.Reload<MaterialResource>(key)==a&&a->GetVersion()>version&&a->GetData().baseColor[0]==.71f,"reload sharing");Passed("shared asset reload updates existing handles");
    resources.SetMaterialDirty(key,true);m.baseColor[0]=.12f;Check(MaterialLoader::Save(key,m,&error),"conflict save");resources.Reload<MaterialResource>(key);Check(a->GetData().baseColor[0]==.71f,"unsaved material discarded");resources.SetMaterialDirty(key,false);resources.Reload<MaterialResource>(key);Check(a->GetData().baseColor[0]==.12f,"revert blocked");Passed("unsaved edit guard and revert");
    resources.WatchForHotReload<MaterialResource>(key);const auto hotVersion=a->GetVersion();
    m.baseColor[0]=.6251f;Check(MaterialLoader::Save(key,m,&error),"external owned material edit");resources.PollHotReload();
    Check(a->GetVersion()>hotVersion&&a->GetData().baseColor[0]==.6251f,"file watcher updates shared material data");
    m.baseColor[0]=.12f;Check(MaterialLoader::Save(key,m,&error),"restore owned material");resources.Reload<MaterialResource>(key);
    Passed("real file edit and material hot reload");
    auto path=AssetPaths::ResolveAssetPath(key);std::ofstream(path)<<"invalid JSON";resources.Reload<MaterialResource>(key);Check(a->HasError()&&a->IsUsable()&&a->GetData().baseColor[0]==.12f,"failed reload discards last good asset");Passed("failed reload retains last good values with diagnostic");
    m.baseColor[0]=2;Check(!MaterialLoader::Save(key,m,&error),"invalid asset saved");std::ifstream in(path);std::string contents;std::getline(in,contents);Check(contents=="invalid JSON","failed save clobbered file");in.close();std::filesystem::remove(path);Passed("failed save preserves destination");
    const std::string nested="materials/material-regression-directory/test.material.json";
    const auto nestedPath=AssetPaths::ResolveAssetOutputPath(nested);
    Check(!MaterialLoader::Save(nested,m,&error)&&!std::filesystem::exists(nestedPath.parent_path()),"invalid save created directories");
    m.baseColor[0]=.5f;Check(MaterialLoader::Save(nested,m,&error),"new material subdirectory save");
    Check(MaterialLoader::Load(nested,nestedPath).success,"nested asset roundtrip");
    std::filesystem::remove(nestedPath);std::filesystem::remove(nestedPath.parent_path());Passed("nested save validates before creating directories");
    MaterialOverrides o;o.castShadows=false;o.texturePath="";auto copy=MaterialOverridesFromJson(MaterialOverridesToJson(o));MaterialResource effective;effective.texturePath="old";ApplyMaterialOverrides(effective,copy);Check(!effective.castShadows&&effective.texturePath.empty()&&effective.receiveShadows,"optional override inheritance");Passed("explicit empty texture and false overrides");
    EcsDemoEntityConfig entity;entity.materialPath="materials/material_red.material.json";entity.slotMaterials={"","materials/material_green.material.json"};entity.materialOverrides=o;entity.materialTint={.5f,.6f,.7f,1};
    const auto scene=std::filesystem::path("material-regression.scene.json");Check(SceneSerializer::SaveEntityConfigs(scene,{entity},&error),"scene save");std::vector<EcsDemoEntityConfig> loaded;Check(SceneSerializer::LoadEntityConfigs(scene,loaded,&error)&&loaded.size()==1&&loaded[0].slotMaterials==entity.slotMaterials&&loaded[0].materialOverrides.castShadows==false&&loaded[0].materialTint==entity.materialTint,"scene material roundtrip");std::filesystem::remove(scene);Passed("scene v2 references, slots, tint and overrides roundtrip");
    JobSystem jobs;Check(jobs.Initialize(),"jobs");{ResourceManager async(&jobs);auto material=async.LoadAsync<MaterialResource>("materials/material_red.material.json").get();Check(material==async.LoadAsync<MaterialResource>("materials/material_red.material.json").get(),"async shared identity");jobs.WaitAll();async.PumpFinalization(100);jobs.WaitAll();async.PumpFinalization(100);Check(material->IsLoaded(),"async material failed");auto texture=async.Get<TextureResource>("textures/material_test.png");Check(texture&&texture->IsLoaded(),"async material texture dependency");async.Shutdown();}jobs.Shutdown();Passed("asynchronous shared material and texture dependency finalization");
}
static void Cpu() {
    Schema();ResourceManager resources(nullptr);resources.Load<MaterialResource>("materials/stress.material.json");
    resources.Load<ShaderResource>("dx12/textured.hlsl");resources.Load<TextureResource>("defaults/texture");resources.Load<TextureResource>("textures/material_test.png");
    for(const char* key:{"models/material_slots.obj","models/material_slots.gltf"}) {
        auto mesh=resources.Load<MeshResource>(key);Check(mesh->IsLoaded(),"multi material import");const auto& data=mesh->GetData().meshData;
        Check(data.submeshes.size()==2&&data.materials.size()>=2,"import lost submesh materials");
        Check(data.submeshes[0].firstIndex==0&&data.submeshes[1].firstIndex==6&&data.submeshes[0].indexCount==6&&data.submeshes[1].indexCount==6,"incorrect global index ranges");
        Check(data.submeshes[0].materialSlot!=data.submeshes[1].materialSlot,"slots collapsed");
        for(const auto& sub:data.submeshes) {const auto& m=data.materials[sub.materialSlot];Check(!m.texturePath.empty(),"import texture missing");auto texture=resources.Load<TextureResource>(m.texturePath);Check(texture->IsLoaded()&&texture->GetData().textureData.pixels[0]==128,"external/embedded decode");}
    }Passed("OBJ/MTL and glTF multiple material slots, ranges and external/embedded textures");
    Recorder adapter;RenderSystem render;render.SetResourceManager(&resources);render.SetRenderAdapter(&adapter);ShadowSettings shadows;shadows.enabled=true;render.SetShadowSettings(&shadows);
    World world;std::vector<Entity> entities;RenderInterpolation interpolation;render.SetInterpolation(&interpolation);
    CreatePhysicsStressScene(world,entities,500);resources.Load<MeshResource>("models/validation_cube.obj");
    auto frame=[&](int warm=1){for(int i=0;i<warm;++i){adapter.BeginFrame();render.Update(world,0);}};
    frame(12);Check(adapter.stats.drawCalls==1&&adapter.stats.shadowDrawCalls==1&&adapter.stats.renderedInstances==501,"500 batching lost");Passed("500 cubes + floor main/shadow batching");
    for(std::size_t i=0;i<entities.size();++i){auto& c=*world.GetComponent<MaterialComponent>(entities[i]);c.overrides.metallic=float(i%3)*.5f;c.overrides.roughness=float(i%5)*.2f;c.overrides.shadingModel=i%2?ShadingModel::PBR:ShadingModel::Lambert;}
    frame();Check(adapter.stats.drawCalls==1&&adapter.stats.shadowDrawCalls==1,"PBR parameters broke batching");
    Check(std::any_of(adapter.main[0].instances.begin(),adapter.main[0].instances.end(),[](const auto& d){return d.surface[0]==1&&d.surface[2]==1;}),"per-instance PBR data absent");
    for(auto e:entities)world.GetComponent<MaterialComponent>(e)->overrides={};
    Passed("500 mixed Lambert/PBR metallic/roughness values retain main1 + shadow1");
    render.SetInstancingEnabled(false);frame();Check(adapter.stats.drawCalls==501&&adapter.stats.shadowDrawCalls==501,"ordinary draws");render.SetInstancingEnabled(true);Passed("instancing ON/OFF equivalent object counts");
    auto& component=*world.GetComponent<MaterialComponent>(entities[1]);component.overrides.castShadows=false;frame();Check(adapter.stats.shadowCasters==500&&adapter.stats.renderedInstances==501,"cast false");Passed("Cast Shadows omits caster only");
    component.overrides.receiveShadows=false;component.overrides.doubleSided=true;frame();Check(adapter.main.size()==2&&std::any_of(adapter.main.begin(),adapter.main.end(),[](const auto& d){return d.state.doubleSided&&!d.state.receiveShadows;}),"state batch key");Passed("receive/double sided state separates compatible batches");
    const auto asset=resources.Load<MaterialResource>("materials/material_red.material.json");component.materialPath=asset->GetKey();std::fill_n(component.tint,4,1.f);component.tint[0]=.5f;component.overrides={};frame(8);Check(std::any_of(adapter.main.begin(),adapter.main.end(),[](const auto& d){return std::any_of(d.instances.begin(),d.instances.end(),[](const auto& p){return p.tint[0]==.5f&&p.baseColor[1]==.25f;});}),"base color tint multiplication");Passed("shared asset color multiplied by individual tint");
    component.materialPath="materials/material_green.material.json";resources.Load<MaterialResource>(component.materialPath);frame(6);Check(render.GetStatistics().materialCacheMisses==0&&render.GetStatistics().materialCacheHits==501,"material cache metrics");Passed("runtime reference replacement and cache statistics");
    auto alpha=asset->GetData();alpha.surfaceMode=MaterialSurfaceMode::Masked;asset->ReplaceData(alpha,ResourceLoadState::Loaded);component.materialPath=asset->GetKey();frame();Check(adapter.stats.renderedInstances==500&&adapter.stats.shadowCasters==500,"masked fake rendering");alpha.surfaceMode=MaterialSurfaceMode::Transparent;asset->ReplaceData(alpha,ResourceLoadState::Loaded);frame();Check(adapter.stats.renderedInstances==500,"transparent silently opaque");component.materialPath.clear();Passed("unsupported surfaces omitted from both passes");
    auto& t=*world.GetComponent<TransformComponent>(entities[1]);interpolation.BeginTick(world);t.rotation={.2f,.4f,.6f};t.scale={2,.5f,1};t.position.x+=1;interpolation.EndTick(world);interpolation.SetFrame(world,true,.5f);frame();auto pose=interpolation.Resolve(entities[1],t);std::array<float,16> expected;BuildRenderModelMatrix(expected.data(),pose);Check(std::any_of(adapter.main[0].instances.begin(),adapter.main[0].instances.end(),[&](const auto& d){return d.model==expected&&d.normal==BuildNormalMatrix(expected);}),"interpolation/material pose");Passed("render interpolation, rotation and nonuniform normals preserved");
    adapter.failInstance=true;frame();Check(adapter.stats.drawCalls==501,"instance fallback");adapter.failInstance=false;world.DestroyEntity(entities[1]);frame();Check(adapter.stats.renderedInstances==500,"destroyed material entity retained");world.Clear();interpolation.Clear();frame();Check(adapter.main.empty()&&adapter.shadow.empty(),"clear stale draws");CreatePhysicsStressScene(world,entities,500);frame();Check(adapter.stats.drawCalls==1,"restart batching");Passed("fallback, create/delete, clear and restart");
    world.Clear();for(int i=0;i<2;++i){auto e=world.CreateEntity();world.AddComponent<TransformComponent>(e);world.AddComponent<MeshRendererComponent>(e).meshPath="models/material_slots.obj";}
    frame(10);Check(adapter.main.size()==2&&adapter.stats.instancedDrawCalls==2&&adapter.stats.renderedInstances==4&&adapter.main[0].range.firstIndex!=adapter.main[1].range.firstIndex,"multi-submesh batch grouping");Passed("multiple entities batch separately per submesh range");
    render.ReleaseGpuResources();render.SetRenderAdapter(nullptr);
}
#if defined(ENABLE_DX12)
static void ShadowFlags(Dx12RenderAdapter& adapter,GlfwWindow& window,ResourceManager& resources) {
    adapter.SetBaseColor({1,1,1,1});adapter.SetSurfaceParameters({0,.5f,0,0});
    auto cubeData=resources.Load<MeshResource>("models/validation_cube.obj");const auto cube=adapter.UploadMesh(cubeData->GetData().meshData);
    auto shaderData=resources.Load<ShaderResource>("dx12/textured.hlsl");const auto shader=adapter.CreateShaderProgram(shaderData->GetData());
    TextureData texture;texture.pixels={128,192,64,255};const auto tex=adapter.CreateTexture2D(texture);
    LightingSettings light;light.directional.direction={1,-1,0};light.directional.color={1,1,1};light.directional.intensity=.8f;light.ambientColor={1,1,1};light.ambientIntensity=.2f;
    ShadowSettings shadows;shadows.coverage=16;shadows.depthRange=30;shadows.resolution=512;shadows.pcf=false;
    const ShadowMatrix view{1,0,0,0,0,0,-1,0,0,1,0,0,0,0,10,1},projection{1.f/8,0,0,0,0,1.f/6,0,0,0,0,1.f/20,0,0,0,0,1};
    const auto vp=ShadowMultiply(projection,view);std::array<RenderInstanceData,2> objects;
    auto model=[](Vec3 pos,Vec3 scale) {TransformComponent t;t.position=pos;t.scale=scale;std::array<float,16> m;BuildRenderModelMatrix(m.data(),RenderPose::From(t));return m;};
    objects[0].model=model({0,-.1f,0},{14,.2f,10});objects[1].model=model({0,2,0},{1,1,1});
    for(auto& o:objects){o.mvp=ShadowMultiply(vp,o.model);o.normal=BuildNormalMatrix(o.model);o.tint={.8f,.7f,.6f,1};}
    auto frame=[&](bool cast,bool receive,bool instances,bool retire=false) {
        window.PollEvents();adapter.BeginFrame();adapter.Clear(.03f,.04f,.05f,1);adapter.SetLighting(BuildLightingConstants(light));adapter.SetMeshRange({});adapter.SetMaterialState({false,true});
        Check(adapter.BeginShadowPass(BuildDirectionalShadowFrame(BuildLightingConstants(light),shadows,{})),"material shadow pass");
        const auto casters=std::span<const RenderInstanceData>(objects.data(),cast?2:1);
        if(instances)Check(adapter.DrawShadowMeshInstanced(cube,casters),"material caster instance");else for(const auto& o:casters)adapter.DrawShadowMesh(cube,o);
        adapter.EndShadowPass();adapter.BeginMainPass();adapter.BindShader(shader);adapter.BindTexture(0,tex);adapter.SetMaterialState({false,receive});
        if(instances)Check(adapter.DrawMeshInstanced(cube,objects),"material receiver instance");
        else for(const auto& o:objects){adapter.SetModelMatrix(o.model);adapter.SetTestTransform(o.mvp.data());adapter.SetNormalMatrix(o.normal);adapter.SetTestColor(o.tint[0],o.tint[1],o.tint[2],1);adapter.DrawMesh(cube);}
        adapter.EndMainPass();if(retire){adapter.DestroyMesh(cube);adapter.DestroyShader(shader);adapter.DestroyTexture(tex);}adapter.EndFrame();auto pixels=adapter.ReadBackFramePixels();adapter.Present();return pixels;
    };
    const auto receiving=frame(true,true,true);Check(frame(true,true,false)==receiving,"material shadow ordinary/instance mismatch");const auto depth=adapter.ReadBackShadowDepth();
    const auto noReceive=frame(true,false,true);Check(depth==adapter.ReadBackShadowDepth(),"receive flag changed casting depth");const auto noCast=frame(false,true,true);
    const auto dark=std::size_t(120*320+200)*4; // x=2,z=0: inside the displaced caster shadow
    Check(int(receiving[dark])+30<int(noReceive[dark])&&std::abs(int(noCast[dark])-int(noReceive[dark]))<=1,"cast/receive flags did not change expected receiver pixels");
    const float base[3]={128*.8f,192*.7f,64*.6f};for(int c=0;c<3;++c)Check(std::abs(int(receiving[dark+c])-int(std::lround(255*LinearToSrgb(SrgbToLinear(base[c]/((c==0)?.8f:(c==1)?.7f:.6f)/255)*SrgbToLinear((c==0)?.8f:(c==1)?.7f:.6f)*.2f))))<=2,"material shadows removed ambient or texture/tint");
    // Reflected single-sided plane must be present in the depth map only with CullNone.
    MeshData plane;plane.vertices.resize(4);plane.indices={0,1,2,0,2,3};const float vertices[4][3]={{-1,0,1},{1,0,1},{1,0,-1},{-1,0,-1}};
    for(int i=0;i<4;++i)std::copy_n(vertices[i],3,plane.vertices[i].position);const auto p=adapter.UploadMesh(plane);
    RenderInstanceData reflected;reflected.model=model({0,2,0},{-1,1,1});
    auto depthFrame=[&](bool doubleSided){adapter.BeginFrame();adapter.Clear(0,0,0,1);adapter.SetMaterialState({doubleSided,true});adapter.SetMeshRange({});Check(adapter.BeginShadowPass(BuildDirectionalShadowFrame(BuildLightingConstants(light),shadows,{})),"double sided shadow pass");adapter.DrawShadowMesh(p,reflected);adapter.EndShadowPass();adapter.EndFrame();adapter.Present();return adapter.ReadBackShadowDepth();};
    const auto back=depthFrame(false),both=depthFrame(true);Check(*std::min_element(back.begin(),back.end())==1&&*std::min_element(both.begin(),both.end())<1,"double sided shadow PSO culling");
    adapter.DestroyMesh(p);frame(true,true,true,true); // recorded main/shadow commands retain retired resources
    adapter.SetMaterialState({});adapter.SetMeshRange({});Check(adapter.GetValidationErrorCount()==0,"material GPU resource retirement validation");
    Passed("DX12 cast/receive flags, ambient preservation, double sided depth and frames in flight retirement");
}
static void Gpu() {
    GlfwWindow window;Check(window.Create(320,240,"DX12 material regression"),"window");Dx12RenderAdapter adapter;Check(adapter.Initialize(&window),"DX12 initialize");
    ResourceManager resources(nullptr);resources.Load<MaterialResource>("materials/stress.material.json");RenderSystem render;render.SetResourceManager(&resources);render.SetRenderAdapter(&adapter);render.SetCameraTransform({},0,0);render.SetCameraProjection(1.04719755f,320.f/240,.1f,20);
    LightingSettings light;light.directional.enabled=false;light.ambientColor={1,1,1};light.ambientIntensity=1;render.SetLightingSettings(&light);
    resources.Load<ShaderResource>("dx12/textured.hlsl");resources.Load<TextureResource>("defaults/texture");resources.Load<TextureResource>("textures/material_test.png");
    World world;
    auto frame=[&](bool instances,int warm=1){render.SetInstancingEnabled(instances);std::vector<std::uint8_t> pixels;for(int i=0;i<warm;++i){window.PollEvents();adapter.BeginFrame();adapter.Clear(.03f,.04f,.05f,1);render.Update(world,0);adapter.EndFrame();pixels=adapter.ReadBackFramePixels();adapter.Present();}return pixels;};
    auto save=[](const char* path,const auto& pixels){std::ofstream f(path,std::ios::binary);f<<"P6\n320 240\n255\n";for(std::size_t i=0;i<pixels.size();i+=4)f.write(reinterpret_cast<const char*>(pixels.data()+i),3);};
    std::filesystem::create_directories("material-readback");
    for(const char* key:{"models/material_slots.obj","models/material_slots.gltf"}) {
        world.Clear();auto mesh=resources.Load<MeshResource>(key);for(const auto& material:mesh->GetData().meshData.materials)if(!material.texturePath.empty())resources.Load<TextureResource>(material.texturePath);
        std::vector<Entity> entities;
        for(int i=0;i<2;++i){auto e=world.CreateEntity();entities.push_back(e);world.AddComponent<TransformComponent>(e).scale={1,1,1};world.AddComponent<MeshRendererComponent>(e).meshPath=key;auto& c=world.AddComponent<MaterialComponent>(e);c.tint[0]=.5f;c.tint[1]=.8f;c.tint[2]=.6f;}
        auto ordinary=frame(false,12);auto instanced=frame(true);
        Check(render.GetStatistics().materialResolves==4,"each imported instance/submesh resolves its own material");
        Check(render.GetStatistics().materialPathChecks==0,"warm imported texture bindings must not revalidate filesystem paths");
        auto texture=resources.Get<TextureResource>(mesh->GetData().meshData.materials[mesh->GetData().meshData.submeshes.front().materialSlot].texturePath);
        Check(bool(texture),"import texture cached");auto textureData=texture->GetData();const auto textureVersion=texture->GetVersion();
        texture->ReplaceData(textureData,ResourceLoadState::Loaded,false);frame(true,4);
        Check(texture->GetVersion()>textureVersion&&frame(true)==instanced,"warm key reuse must honor texture version changes");
        const auto textureKey=texture->GetKey();resources.Unload<TextureResource>(textureKey);resources.Load<TextureResource>(textureKey);
        Check(frame(true,6)==instanced,"cache epoch replaces retired texture owner without stale descriptors");
        Check(render.GetStatistics().materialPathChecks==0,"replacement texture reaches warm path cache again");
        const std::string missingKey="materials/p31-deliberately-missing.material.json";
        auto missing=resources.Load<MaterialResource>(missingKey);Check(missing&&missing->HasError(),"missing material diagnostic");
        world.GetComponent<MaterialComponent>(entities[0])->materialPath=missingKey;frame(true,6);
        world.GetComponent<MaterialComponent>(entities[0])->materialPath.clear();
        Check(frame(true,4)==instanced,"missing material can be replaced without stale bindings");Check(ordinary==instanced,"import ordinary/instance pixels differ");Check(adapter.GetSubmissionStatistics().drawCalls==2&&adapter.GetSubmissionStatistics().instancedDrawCalls==2,"GPU submesh batching");
        const float factors[2][3]={{1,.25f,.125f},{.125f,1,.25f}};const float base[3]={128*.5f,192*.8f,64*.6f};
        for(int slot=0;slot<2;++slot)for(int channel=0;channel<3;++channel)Check(std::abs(int(instanced[(120*320+(slot?240:80))*4+channel])-int(std::lround(255*LinearToSrgb(SrgbToLinear((channel==0?128.f:channel==1?192.f:64.f)/255)*SrgbToLinear(channel==0?.5f:channel==1?.8f:.6f)*factors[slot][channel]))))<=2,"import material/texture/tint color oracle");
        save(std::string(key).ends_with("obj")?"material-readback/obj.ppm":"material-readback/gltf.ppm",instanced);
        for(auto e:entities){auto& c=*world.GetComponent<MaterialComponent>(e);c.overrides.shadingModel=ShadingModel::PBR;c.overrides.metallic=.5f;c.overrides.roughness=.2f;}
        auto pbr=frame(true);Check(pbr==frame(false),"PBR imported slots ordinary/instance mismatch");
        Check(pbr!=instanced,"PBR material override ignored");
        for(int slot=0;slot<2;++slot)for(int channel=0;channel<3;++channel)Check(std::abs(int(pbr[(120*320+(slot?240:80))*4+channel])-int(std::lround(255*LinearToSrgb(SrgbToLinear((channel==0?128.f:channel==1?192.f:64.f)/255)*SrgbToLinear(channel==0?.5f:channel==1?.8f:.6f)*factors[slot][channel]*.5f))))<=2,"PBR imported ambient/metallic oracle");
        for(auto e:entities)world.GetComponent<MaterialComponent>(e)->overrides={};
        for(auto e:entities) {auto& c=*world.GetComponent<MaterialComponent>(e);c.slotMaterials.resize(mesh->GetData().meshData.materials.size());for(auto& slot:c.slotMaterials)slot="materials/material_red.material.json";}
        resources.Load<MaterialResource>("materials/material_red.material.json");frame(true,6);auto assigned=frame(true);Check(assigned[(120*320+80)*4]==assigned[(120*320+240)*4],"slot asset assignment failed");
        auto shared=resources.Get<MaterialResource>("materials/material_red.material.json");auto modified=shared->GetData();modified.baseColor[0]=.4f;shared->ReplaceData(modified,ResourceLoadState::Loaded);auto edited=frame(true);Check(int(edited[(120*320+80)*4])+20<int(assigned[(120*320+80)*4]),"live shared asset color not reflected");resources.Reload<MaterialResource>(shared->GetKey());
        for(auto e:entities){auto* c=world.GetComponent<MaterialComponent>(e);c->slotMaterials.clear();world.GetComponent<TransformComponent>(e)->scale.x=-1;}
        auto mirrored=frame(true);Check(mirrored[(120*320+80)*4+1]>30,"negative scale must preserve front face winding");Check(frame(false)==mirrored,"mirrored ordinary/instance differ");
        for(auto e:entities){auto* t=world.GetComponent<TransformComponent>(e);t->scale.x=1;t->rotation.y=3.14159265f;t->position.z=4;}
        auto culled=frame(true);Check(culled[(120*320+80)*4+1]==10,"true backface culling not applied");for(auto e:entities)world.GetComponent<MaterialComponent>(e)->overrides.doubleSided=true;auto doubleSided=frame(true);Check(doubleSided[(120*320+80)*4+1]>30,"double sided GPU PSO missing");Check(frame(false)==doubleSided,"double sided ordinary/instance differ");
        world.DestroyEntity(entities[0]);frame(true);world.Clear();auto clear=frame(true);Check(clear[(120*320+80)*4]==8,"GPU clear retains material draw");
        Passed(std::string(key).ends_with("obj")?"DX12 OBJ multi-material colors, assignment, editing, culling and lifecycle":"DX12 glTF embedded texture colors, assignment, editing, culling and lifecycle");
    }
    ShadowFlags(adapter,window,resources);
    Check(adapter.GetValidationErrorCount()==0,"DX12 ERROR/CORRUPTION");render.ReleaseGpuResources();render.SetRenderAdapter(nullptr);adapter.Shutdown();Passed("DX12 material readback and Debug Layer");
}
#endif
int main(int argc,char** argv) {
    try {Cpu();
#if defined(ENABLE_DX12)
        if(argc>1&&std::strcmp(argv[1],"--dx12")==0)Gpu();
#endif
        std::cout<<"Material system regression passed\n";return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
