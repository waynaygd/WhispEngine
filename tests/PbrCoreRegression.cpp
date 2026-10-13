#define NOMINMAX
#include "render/ColorPipeline.h"
#include "render/Lighting.h"
#include "resources/ResourceManager.h"
#include "resources/loaders/MaterialLoader.h"
#include "resources/MaterialSerialization.h"
#include "ecs/PhysicsStressScene.h"
#include "ecs/systems/RenderSystem.h"
#include "render/IRenderAdapter.h"
#include "scene/SceneSerializer.h"
#include "core/AssetPaths.h"
#include <limits>
#include <iostream>
#include <stdexcept>
#include <numbers>
#include <fstream>
#include <filesystem>
#if defined(ENABLE_DX12)
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include "platform/GlfwWindow.h"
#include <GLFW/glfw3.h>
#endif
static void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
using V=std::array<double,3>;
static double Dot(V a,V b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static V Unit(V v){double q=std::sqrt(Dot(v,v));if(!std::isfinite(q)||q<=0)return {};for(auto& x:v)x/=q;return v;}
// Independent double oracle: Smith lambda from tan(theta), not the shader's
// algebraic G1 expression. BRDF values are evaluated before output transfer.
static V Oracle(V base,double metal,double rough,V N,V view,V light){
    const double nl=std::max(0.,Dot(N,light)),nv=std::max(0.,Dot(N,view));
    if(nl<=0||nv<=0)return {};
    V h=Unit({view[0]+light[0],view[1]+light[1],view[2]+light[2]});
    if(Dot(h,h)==0)return {};
    double nh=std::max(0.,Dot(N,h)),vh=std::max(0.,Dot(view,h));
    double a=std::pow(std::max(.04,rough),2),a2=a*a;
    double d=a2/(std::numbers::pi*std::pow((1-nh*nh)+a2*nh*nh,2));
    auto lambda=[&](double cosine){return (std::sqrt(1+a2*(1/(cosine*cosine)-1))-1)/2;};
    double g=1/((1+lambda(nv))*(1+lambda(nl)));
    V result;for(int i=0;i<3;++i){double f0=.04*(1-metal)+base[i]*metal;
        double fresnel=f0+(1-f0)*std::pow(1-vh,5);
        result[i]=nl*((1-fresnel)*(1-metal)*base[i]/std::numbers::pi+d*g*fresnel/(4*nv*nl));}
    return result;
}
static double Decode(double x){return x<=.04045?x/12.92:std::pow((x+.055)/1.055,2.4);}
static double Encode(double x){x=std::clamp(x,0.,1.);return x<=.0031308?12.92*x:1.055*std::pow(x,1/2.4)-.055;}
struct Recorder:IRenderAdapter {
    RenderInstanceData pending;
    std::vector<RenderInstanceData> main,shadow;
    RenderSubmissionStatistics stats;
    bool fallback=false;std::uint64_t next=1;
    bool Initialize(IWindow*)override{return true;}
    void BeginFrame()override{main.clear();shadow.clear();stats={};}
    void Clear(float,float,float,float)override{}void EndFrame()override{}void Present()override{}void Shutdown()override{}
    void SetTestTransform(const float* p)override{std::copy_n(p,16,pending.mvp.begin());}
    void SetTestColor(float r,float g,float b,float a)override{pending.tint={r,g,b,a};}
    void SetModelMatrix(const std::array<float,16>& p)override{pending.model=p;}
    void SetNormalMatrix(const std::array<float,12>& p)override{pending.normal=p;}
    void SetBaseColor(const std::array<float,4>& p)override{pending.baseColor=p;}
    void SetSurfaceParameters(const std::array<float,4>& p)override{pending.surface=p;}
    RenderMeshHandle UploadMesh(const MeshData&)override{return {next++};}
    RenderTextureHandle CreateTexture2D(const TextureData&)override{return {next++};}
    RenderShaderHandle CreateShaderProgram(const ShaderResource&)override{return {next++};}
    bool SupportsInstancing(RenderShaderHandle)const override{return true;}
    void DrawMesh(RenderMeshHandle)override{main.push_back(pending);++stats.drawCalls;++stats.renderedInstances;}
    bool DrawMeshInstanced(RenderMeshHandle,std::span<const RenderInstanceData> p)override{if(fallback)return false;main.insert(main.end(),p.begin(),p.end());++stats.drawCalls;stats.renderedInstances+=p.size();return true;}
    bool BeginShadowPass(const ShadowFrame& f)override{return f.constants.parameters[0]>.5f;}
    void DrawShadowMesh(RenderMeshHandle,const RenderInstanceData& d)override{shadow.push_back(d);++stats.shadowDrawCalls;++stats.shadowCasters;}
    bool DrawShadowMeshInstanced(RenderMeshHandle,std::span<const RenderInstanceData> p)override{if(fallback)return false;shadow.insert(shadow.end(),p.begin(),p.end());++stats.shadowDrawCalls;stats.shadowCasters+=p.size();return true;}
    RenderSubmissionStatistics GetSubmissionStatistics()const override{return stats;}
    void DrawTestTriangle()override{}void DrawTestLine()override{}void DrawTestQuad()override{}void DrawTestCube()override{}
};
static void CpuScene(){
    using namespace ecs;
    ResourceManager resources(nullptr);Recorder recorder;RenderSystem render;World world;
    for(const char* key:{"models/material_slots.obj","models/material_slots.gltf"}){
        auto imported=resources.Load<MeshResource>(key);Require(imported->IsUsable()&&imported->GetData().meshData.submeshes.size()==2,"PBR imported material slots");
        for(const auto& submesh:imported->GetData().meshData.submeshes){const auto& material=imported->GetData().meshData.materials[submesh.materialSlot];Require(std::string(key).ends_with(".gltf")?(material.shadingModel==ShadingModel::PBR&&material.metallic==0&&material.roughness==.5f):(material.shadingModel==ShadingModel::Lambert&&material.metallic==0&&material.roughness==.5f),"OBJ legacy / glTF MR defaults");}
    }
    render.SetResourceManager(&resources);render.SetRenderAdapter(&recorder);ShadowSettings shadows;shadows.enabled=true;render.SetShadowSettings(&shadows);
    std::vector<Entity> entities;CreatePhysicsStressScene(world,entities,500);
    resources.Load<MeshResource>("models/validation_cube.obj");resources.Load<ShaderResource>("dx12/textured.hlsl");resources.Load<TextureResource>("defaults/texture");resources.Load<MaterialResource>("materials/stress.material.json");
    auto frame=[&](int warm=1){for(int i=0;i<warm;++i){recorder.BeginFrame();render.Update(world,0);}};
    for(std::size_t i=0;i<entities.size();++i){auto& m=*world.GetComponent<MaterialComponent>(entities[i]);m.overrides.shadingModel=i%2?ShadingModel::PBR:ShadingModel::Lambert;m.overrides.metallic=float(i%3)*.5f;m.overrides.roughness=float(i%5)*.2f;}
    frame(12);Require(recorder.stats.drawCalls==1&&recorder.stats.shadowDrawCalls==1&&recorder.main.size()==501,"mixed 500 ECS batching");
    auto prepared=recorder.main;render.SetInstancingEnabled(false);frame();Require(recorder.main.size()==prepared.size(),"ordinary packet count");
    for(std::size_t i=0;i<prepared.size();++i){const auto& a=prepared[i];const auto& b=recorder.main[i];Require(a.model==b.model&&a.normal==b.normal&&a.mvp==b.mvp&&a.surface==b.surface&&a.baseColor==b.baseColor&&a.tint==b.tint,"ordinary/instance CPU data contract");}
    render.SetInstancingEnabled(true);RenderInterpolation interpolation;render.SetInterpolation(&interpolation);
    auto& physical=*world.GetComponent<TransformComponent>(entities[1]);interpolation.BeginTick(world);physical.position.x+=2;physical.rotation={.3f,.5f,.2f};physical.scale={-2,.5f,3};interpolation.EndTick(world);interpolation.SetFrame(world,true,.4f);
    const auto pose=interpolation.Resolve(entities[1],physical);std::array<float,16> expected;BuildRenderModelMatrix(expected.data(),pose);
    const auto position=physical.position;frame();Require(physical.position.x==position.x&&physical.position.y==position.y&&physical.position.z==position.z,"render changed physics transform");
    Require(std::any_of(recorder.main.begin(),recorder.main.end(),[&](const auto& d){return d.model==expected&&d.normal==BuildNormalMatrix(expected)&&d.surface[2]==1;}),"interpolated PBR pose/normal");
    const auto normals=BuildNormalMatrix(expected);std::array<double,3> n{},t{};for(int i=0;i<3;++i){n[i]=normals[i*4]+normals[i*4+1]+normals[i*4+2];t[i]=expected[i]-expected[4+i];}Require(std::abs(Dot(n,t))<1e-5,"normal/tangent perpendicular");
    auto& m=*world.GetComponent<MaterialComponent>(entities[1]);m.overrides.castShadows=false;frame();Require(recorder.shadow.size()==500&&recorder.main.size()==501,"PBR cast flag");
    recorder.fallback=true;frame();Require(recorder.stats.drawCalls==501,"PBR instance fallback");recorder.fallback=false;
    world.DestroyEntity(entities[1]);frame();Require(recorder.main.size()==500,"destroy stale PBR entity");world.Clear();interpolation.Clear();frame();Require(recorder.main.empty()&&recorder.shadow.empty(),"clear PBR packets");CreatePhysicsStressScene(world,entities,500);frame();Require(recorder.stats.drawCalls==1,"restart batching");
    auto shared=resources.Load<MaterialResource>("materials/material_red.material.json");auto same=resources.Load<MaterialResource>(shared->GetKey());Require(shared==same,"shared PBR material identity");
    auto changed=shared->GetData();changed.shadingModel=ShadingModel::PBR;changed.metallic=.6f;changed.roughness=.3f;shared->ReplaceData(changed,ResourceLoadState::Loaded);
    Require(same->GetData().metallic==.6f&&same->GetData().shadingModel==ShadingModel::PBR,"shared live PBR edit");resources.Reload<MaterialResource>(shared->GetKey());Require(shared==same&&same->GetData().shadingModel==ShadingModel::Lambert,"PBR revert/reload");
    auto missing=resources.Load<TextureResource>("textures/pbr-regression-missing.png");Require(missing->IsUsable()&&missing->GetData().textureData.pixels==std::vector<std::uint8_t>({255,255,255,255}),"missing texture white fallback");
    EcsDemoEntityConfig config;config.materialOverrides.metallic=0.f;config.materialOverrides.roughness=.8f;config.materialOverrides.shadingModel=ShadingModel::PBR;
    std::string error;const std::filesystem::path path="pbr-regression.scene.json";Require(SceneSerializer::SaveEntityConfigs(path,{config},&error),"PBR scene save");std::vector<EcsDemoEntityConfig> loaded;
    Require(SceneSerializer::LoadEntityConfigs(path,loaded,&error)&&loaded.size()==1&&loaded[0].materialOverrides.metallic==0&&loaded[0].materialOverrides.roughness==.8f&&loaded[0].materialOverrides.shadingModel==ShadingModel::PBR,"PBR scene roundtrip");std::filesystem::remove(path);
    render.ReleaseGpuResources();render.SetRenderAdapter(nullptr);
    std::cout<<"PBR ECS mixed batching, overrides, poses/normals, transform invariance, shared edit/reload, fallback, lifecycle and scene persistence passed\n";
}
static void Cpu(){
    for(int version:{1,2,3}){auto m=MaterialLoader::Parse("materials/test.json",{{"version",version}});
        Require(m.success&&m.data.shadingModel==ShadingModel::Lambert&&m.data.metallic==0&&m.data.roughness==.5f,"legacy migration");}
    MaterialResource material;material.metallic=.7f;material.roughness=0;material.shadingModel=ShadingModel::PBR;
    auto json=MaterialLoader::Serialize(material);Require(json["version"]==4,"schema version");
    auto roundtrip=MaterialLoader::Parse("materials/test.json",json);Require(roundtrip.success&&roundtrip.data.roughness==0&&roundtrip.data.shadingModel==ShadingModel::PBR,"v3 roundtrip");
    for(auto bad:{nlohmann::json{{"version",5}},nlohmann::json{{"metallic",-.1}},nlohmann::json{{"roughness",1.1}},nlohmann::json{{"roughness","bad"}},nlohmann::json{{"shadingModel","other"}}})Require(!MaterialLoader::Parse("a.json",bad).success,"invalid PBR asset accepted");
    const std::string saveKey="materials/pbr-regression-temporary.material.json";const auto savePath=AssetPaths::ResolveAssetOutputPath(saveKey);
    Require(!std::filesystem::exists(savePath),"PBR temporary material already exists; preserving it");
    std::string saveError;Require(MaterialLoader::Save(saveKey,material,&saveError),"v3 atomic save");
    auto disk=MaterialLoader::Load(saveKey,savePath);Require(disk.success&&disk.data.metallic==.7f&&disk.data.roughness==0&&disk.data.shadingModel==ShadingModel::PBR,"v3 disk roundtrip");
    auto invalid=material;invalid.roughness=std::numeric_limits<float>::infinity();Require(!MaterialLoader::Save(saveKey,invalid,&saveError),"nonfinite PBR save accepted");
    Require(MaterialLoader::Load(saveKey,savePath).data.roughness==0,"failed PBR save clobbered destination");std::filesystem::remove(savePath);
    for(auto bad:{nlohmann::json{{"metallic",std::numeric_limits<double>::infinity()}},nlohmann::json{{"roughness",std::numeric_limits<double>::quiet_NaN()}}})Require(!MaterialLoader::Parse("a.json",bad).success,"nonfinite MR accepted");
    auto overrides=MaterialOverridesFromJson({{"metallic",0},{"roughness",1},{"shadingModel","Lambert"}});
    ApplyMaterialOverrides(material,overrides);Require(material.metallic==0&&material.roughness==1&&material.shadingModel==ShadingModel::Lambert,"override priority / zero values");
    Require(MaterialOverridesToJson(MaterialOverridesFromJson(MaterialOverridesToJson(overrides)))==MaterialOverridesToJson(overrides),"override persistence");
    for(auto bad:{nlohmann::json{{"metallic",2}},nlohmann::json{{"roughness",false}},nlohmann::json{{"shadingModel",0}}}){bool rejected=false;try{MaterialOverridesFromJson(bad);}catch(...){rejected=true;}Require(rejected,"invalid override accepted");}
    for(int value=0;value<=255;++value){double x=value/255.;Require(std::abs(SrgbToLinear(float(x))-Decode(x))<1e-6,"decode transfer");Require(std::abs(LinearToSrgb(SrgbToLinear(float(x)))-x)<1e-6,"transfer roundtrip");}
    for(double m:{0.,.5,1.})for(double r:{0.,.04,.2,.8,1.})for(double angle:{0.,.2,1.,1.56}){
        auto result=Oracle({.2,.5,.8},m,r,{0,0,1},Unit({std::sin(angle),0,std::cos(angle)}),{0,0,1});
        for(auto x:result)Require(std::isfinite(x)&&x>=0,"BRDF finite");}
    const auto normalIncidence=Oracle({.2,.5,.8},1,.8,{0,0,1},{0,0,1},{0,0,1});
    Require(std::abs(normalIncidence[0]-.2/(4*std::numbers::pi*std::pow(.8,4)))<1e-12,"analytic metallic GGX normal incidence");
    std::cout<<"PBR schema v3, migration, strict overrides, transfer and independent BRDF oracles passed\n";
}
#if defined(ENABLE_DX12)
static void Gpu(){
    GlfwWindow window;Require(window.Create(320,240,"PBR regression"),"window");
    Dx12RenderAdapter adapter;Require(adapter.Initialize(&window),"DX12");ResourceManager resources(nullptr);
    auto source=resources.Load<ShaderResource>("dx12/textured.hlsl");auto shader=adapter.CreateShaderProgram(source->GetData());
    Require(shader.IsValid()&&adapter.SupportsInstancing(shader),"PBR PSOs/reflection");
    TextureData texture;texture.pixels={128,192,64,255};auto tex=adapter.CreateTexture2D(texture);
    MeshData quad;quad.vertices.resize(4);quad.indices={0,1,2,0,2,3};
    float positions[4][3]={{-.8f,.8f,.5f},{.8f,.8f,.5f},{.8f,-.8f,.5f},{-.8f,-.8f,.5f}};
    for(int i=0;i<4;++i){std::copy_n(positions[i],3,quad.vertices[i].position);quad.vertices[i].normal[2]=-1;}
    auto mesh=adapter.UploadMesh(quad);RenderInstanceData data;
    data.model=data.mvp={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};data.normal=BuildNormalMatrix(data.model);
    data.tint={.6f,.4f,.8f,1};data.baseColor={.8f,.7f,.6f,1};data.surface={0,.2f,1,0};
    LightingSettings settings;settings.directional.direction={0,0,1};settings.directional.color={1,1,1};settings.directional.intensity=.7f;settings.ambientIntensity=.15f;
    V camera{0,0,-2};
    auto frame=[&](bool instanced){window.PollEvents();adapter.BeginFrame();adapter.Clear(.03f,.04f,.05f,1);
        adapter.BindShader(shader);adapter.BindTexture(0,tex);adapter.SetLighting(BuildLightingConstants(settings));
        adapter.SetCameraPosition({float(camera[0]),float(camera[1]),float(camera[2]),0});
        if(instanced)Require(adapter.DrawMeshInstanced(mesh,std::span(&data,1)),"instance");
        else{adapter.SetModelMatrix(data.model);adapter.SetTestTransform(data.mvp.data());adapter.SetNormalMatrix(data.normal);adapter.SetTestColor(data.tint[0],data.tint[1],data.tint[2],1);adapter.SetSurfaceParameters(data.surface);adapter.SetBaseColor(data.baseColor);adapter.DrawMesh(mesh);}
        adapter.EndFrame();auto p=adapter.ReadBackFramePixels();adapter.Present();return p;};
    auto pair=[&](){auto ordinary=frame(false);Require(ordinary==frame(true),"PBR ordinary/instance mismatch");
        V base;for(int i=0;i<3;++i)base[i]=Decode(texture.pixels[i]/255.)*Decode(data.tint[i])*data.baseColor[i];
        // Pixel-center location in the identity MVP plane; model is independent.
        for(int y:{60,120,180})for(int x:{80,120,160,200,240}) {
        V local{(2*x+1)/320.-1,1-(2*y+1)/240.,.5},world{},normal{};
        for(int i=0;i<3;++i){world[i]=data.model[i]*local[0]+data.model[4+i]*local[1]+data.model[8+i]*local[2]+data.model[12+i];normal[i]=-data.normal[i*4+2];}
        normal=Unit(normal);V light=Unit({-settings.directional.direction[0],-settings.directional.direction[1],-settings.directional.direction[2]});
        V view=Unit({camera[0]-world[0],camera[1]-world[1],camera[2]-world[2]});
        double metal=std::isfinite(data.surface[0])?std::clamp(double(data.surface[0]),0.,1.):0;
        double rough=std::isfinite(data.surface[1])?std::clamp(double(data.surface[1]),0.,1.):.5;
        auto direct=Oracle(base,metal,rough,normal,view,light);
        for(int i=0;i<3;++i){double linear=base[i];if(settings.enabled&&Dot(normal,normal)>.5){
            double intensity=settings.directional.enabled?settings.directional.intensity:0;
            if(data.surface[2]>.5)linear=base[i]*(1-metal)*settings.ambientIntensity+direct[i]*intensity;
            else linear=base[i]*(settings.ambientIntensity+std::max(0.,Dot(normal,light))*intensity);}
            int expected=int(std::lround(255*Encode(linear))),actual=ordinary[(y*320+x)*4+i];
            if(std::abs(expected-actual)>2){std::cerr<<"m="<<data.surface[0]<<" r="<<data.surface[1]<<" channel="<<i<<" expected="<<expected<<" actual="<<actual<<'\n';throw std::runtime_error("GPU BRDF/transfer oracle");}}
        }
        return ordinary;};
    for(float metal:{0.f,.5f,1.f})for(float rough:{0.f,.2f,.8f,1.f})for(V c:{V{0,0,-2},V{1,0,-2},V{0,0,2}}){data.surface={metal,rough,1,0};camera=c;pair();}
    camera={1./320,-1./240,.5};pair();
    camera={std::numeric_limits<double>::quiet_NaN(),0,-2};pair();
    camera={0,0,-2};data.surface={0,.8f,1,0};
    for(std::array<float,3> ray:{std::array<float,3>{0,0,1},{1,0,1},{0,0,-1}}){settings.directional.direction=ray;pair();}
    settings.directional.direction={0,0,1};settings.directional.intensity=0;pair();
    settings.ambientIntensity=0;pair();settings.directional.intensity=.7f;pair();
    settings.enabled=false;pair();settings.enabled=true;settings.ambientIntensity=.15f;
    data.surface[2]=0;pair();data.surface[2]=1;
    data.model[0]=2;data.model[5]=.5f;data.model[10]=3;data.normal=BuildNormalMatrix(data.model);pair();
    const float co=std::cos(.6f),si=std::sin(.6f);
    data.model[0]=2*co;data.model[2]=-2*si;data.model[8]=3*si;data.model[10]=3*co;data.normal=BuildNormalMatrix(data.model);pair();
    data.model[2]=data.model[8]=0;data.model[10]=3;
    data.model[0]=-2;data.normal=BuildNormalMatrix(data.model);pair();
    data.model[0]=1e20f;data.model[5]=1e-20f;data.model[10]=1e12f;data.normal=BuildNormalMatrix(data.model);pair();
    data.model[0]=2;data.model[5]=.5f;data.model[10]=3;data.normal=BuildNormalMatrix(data.model);
    data.surface={std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),1,0};pair();data.surface={0,.8f,1,0};
    data.normal={};pair();
    data.normal=BuildNormalMatrix(data.model);
    const auto originalTexture=tex;const auto originalTint=data.tint,originalBase=data.baseColor;
    TextureData blackWhite;blackWhite.width=2;blackWhite.height=1;blackWhite.pixels={0,0,0,255,255,255,255,255};tex=adapter.CreateTexture2D(blackWhite);
    settings.enabled=false;data.tint=data.baseColor={1,1,1,1};auto filtered=frame(false);
    Require(filtered==frame(true),"linear texture filtering paths differ");
    for(int c=0;c<3;++c)Require(std::abs(int(filtered[(120*320+160)*4+c])-188)<=1,"sRGB texels must decode before filtering");
    adapter.DestroyTexture(tex);tex=originalTexture;data.tint=originalTint;data.baseColor=originalBase;settings.enabled=true;
    std::array<RenderInstanceData,3> palette;
    for(int i=0;i<3;++i){palette[i]=data;palette[i].mvp={.25f,0,0,0,0,.8f,0,0,0,0,1,0,(i-1)*.65f,0,0,1};palette[i].surface={i*.5f,.2f+i*.3f,i==0?0.f:1.f,0};}
    auto paletteFrame=[&](bool instanced,int count) {
        adapter.BeginFrame();adapter.Clear(0,0,0,1);adapter.BindShader(shader);adapter.BindTexture(0,tex);adapter.SetLighting(BuildLightingConstants(settings));adapter.SetCameraPosition({0,0,-2,0});
        std::vector<RenderInstanceData> payload;for(int i=0;i<count;++i)payload.push_back(palette[i%3]);
        if(instanced)Require(adapter.DrawMeshInstanced(mesh,payload),"mixed palette batch");
        else for(const auto& d:payload){adapter.SetTestTransform(d.mvp.data());adapter.SetModelMatrix(d.model);adapter.SetNormalMatrix(d.normal);adapter.SetTestColor(d.tint[0],d.tint[1],d.tint[2],1);adapter.SetBaseColor(d.baseColor);adapter.SetSurfaceParameters(d.surface);adapter.DrawMesh(mesh);}
        Require(adapter.GetSubmissionStatistics().drawCalls==std::uint64_t(instanced?1:count),"PBR palette draw count");
        adapter.EndFrame();auto pixels=adapter.ReadBackFramePixels();adapter.Present();return pixels;
    };
    Require(paletteFrame(false,3)==paletteFrame(true,3),"mixed Lambert/PBR parameters differ");
    Require(paletteFrame(false,501)==paletteFrame(true,501),"501 mixed PBR instances differ");
    std::cout<<"DX12 analytic RGB readback, MR endpoints/intermediate, camera/light, Lambert/unlit, scale and ordinary/instanced equality passed\n";
    adapter.DestroyMesh(mesh);
    // Real loaded smooth sphere: no procedural GPU primitive bypass.
    std::filesystem::create_directories("pbr-readback");
    auto sphere=adapter.UploadMesh(resources.Load<MeshResource>("models/pbr_sphere.obj")->GetData().meshData);
    TextureData white;white.pixels={255,255,255,255};auto sphereTexture=adapter.CreateTexture2D(white);
    RenderInstanceData ball;ball.model={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};ball.normal=BuildNormalMatrix(ball.model);
    ball.mvp={.525f,0,0,0,0,.7f,0,0,0,0,.3f,0,0,0,.5f,1};ball.tint={.8f,.4f,.2f,1};
    settings.directional.direction={0,0,1};settings.directional.intensity=1;settings.ambientIntensity=0;
    auto sphereFrame=[&](float metal,float rough,bool instanced) {
        ball.surface={metal,rough,1,0};adapter.BeginFrame();adapter.Clear(0,0,0,1);adapter.BindShader(shader);adapter.BindTexture(0,sphereTexture);
        adapter.SetLighting(BuildLightingConstants(settings));adapter.SetCameraPosition({0,0,-3,0});
        if(instanced)Require(adapter.DrawMeshInstanced(sphere,{&ball,1}),"sphere instancing");
        else {adapter.SetSurfaceParameters(ball.surface);adapter.SetBaseColor(ball.baseColor);adapter.SetModelMatrix(ball.model);adapter.SetNormalMatrix(ball.normal);adapter.SetTestTransform(ball.mvp.data());adapter.SetTestColor(.8f,.4f,.2f,1);adapter.DrawMesh(sphere);}
        adapter.EndFrame();auto pixels=adapter.ReadBackFramePixels();adapter.Present();return pixels;
    };
    int sharpArea=0,broadArea=0;
    for(float m:{0.f,1.f})for(float r:{.2f,.8f}){
        auto pixels=sphereFrame(m,r,true);Require(pixels==sphereFrame(m,r,false),"loaded PBR sphere paths differ");
        std::ofstream image("pbr-readback/sphere-m"+std::to_string(m)+"-r"+std::to_string(r)+".ppm",std::ios::binary);image<<"P6\n320 240\n255\n";
        int area=0,peak=0;for(std::size_t at=0;at<pixels.size();at+=4){image.write(reinterpret_cast<const char*>(pixels.data()+at),3);if(pixels[at]>32)++area;peak=std::max(peak,int(pixels[at]));}
        if(m==1){if(r<.5f)sharpArea=area;else broadArea=area;}
        std::cout<<"Loaded sphere m="<<m<<" r="<<r<<" red>32 area="<<area<<" peak="<<peak<<'\n';
    }
    Require(sharpArea>0&&broadArea>sharpArea*2,"roughness does not broaden metallic highlight");
    const auto beforeReload=sphereFrame(1,.8f,true);
    source=resources.Reload<ShaderResource>("dx12/textured.hlsl");adapter.DestroyShader(shader);shader=adapter.CreateShaderProgram(source->GetData());
    Require(shader.IsValid()&&adapter.SupportsInstancing(shader)&&sphereFrame(1,.8f,true)==beforeReload,"PBR shader reload/reflection recreation");
    // Preserved explicit unlit shader ignores BRDF and outputs legacy tint.
    auto unlit=adapter.CreateShaderProgram(resources.Load<ShaderResource>("dx12/textured_unlit.hlsl")->GetData());
    Require(unlit.IsValid()&&adapter.SupportsInstancing(unlit),"unlit fallback PSOs");
    adapter.BeginFrame();adapter.Clear(0,0,0,1);adapter.BindShader(unlit);adapter.BindTexture(0,sphereTexture);
    Require(adapter.DrawMeshInstanced(sphere,{&ball,1}),"unlit fallback instance");adapter.EndFrame();auto unlitPixels=adapter.ReadBackFramePixels();adapter.Present();
    Require(std::abs(int(unlitPixels[(120*320+160)*4])-204)<=1,"legacy unlit fallback changed");
    adapter.DestroyShader(unlit);adapter.DestroyMesh(sphere);adapter.DestroyTexture(sphereTexture);
    auto cube=resources.Load<MeshResource>("models/validation_cube.obj");mesh=adapter.UploadMesh(cube->GetData().meshData);
    const ShadowMatrix view{1,0,0,0,0,0,-1,0,0,1,0,0,0,0,10,1},projection{1.f/8,0,0,0,0,1.f/6,0,0,0,0,1.f/20,0,0,0,0,1};
    auto vp=ShadowMultiply(projection,view);std::array<RenderInstanceData,2> scene;
    scene[0].model={14,0,0,0,0,.2f,0,0,0,0,10,0,0,-.1f,0,1};scene[1].model={1,0,0,0,0,1,0,0,0,0,1,0,0,2,0,1};
    settings.directional.direction={1,-1,0};settings.directional.intensity=.8f;settings.ambientIntensity=.2f;
    ShadowSettings shadows;shadows.coverage=16;shadows.depthRange=30;shadows.resolution=512;shadows.pcf=false;
    auto shadowFrame=[&](bool instanced,bool receive,float metallic,bool retire=false){
        for(auto& d:scene){d.mvp=ShadowMultiply(vp,d.model);d.normal=BuildNormalMatrix(d.model);d.tint={.8f,.7f,.6f,1};d.surface={metallic,.8f,1,0};}
        adapter.BeginFrame();adapter.Clear(0,0,0,1);adapter.SetMaterialState({false,true});adapter.SetLighting(BuildLightingConstants(settings));
        Require(adapter.BeginShadowPass(BuildDirectionalShadowFrame(BuildLightingConstants(settings),shadows,{})),"PBR shadow pass");
        if(instanced)Require(adapter.DrawShadowMeshInstanced(mesh,scene),"shadow instance");else for(auto& d:scene)adapter.DrawShadowMesh(mesh,d);
        adapter.EndShadowPass();adapter.BeginMainPass();adapter.BindShader(shader);adapter.BindTexture(0,tex);adapter.SetCameraPosition({0,10,0,0});adapter.SetMaterialState({false,receive});
        if(instanced)Require(adapter.DrawMeshInstanced(mesh,scene),"PBR receiver instance");
        else for(auto& d:scene){adapter.SetModelMatrix(d.model);adapter.SetTestTransform(d.mvp.data());adapter.SetNormalMatrix(d.normal);adapter.SetTestColor(d.tint[0],d.tint[1],d.tint[2],1);adapter.SetBaseColor(d.baseColor);adapter.SetSurfaceParameters(d.surface);adapter.DrawMesh(mesh);}
        adapter.EndMainPass();Require(adapter.GetSubmissionStatistics().shadowDrawCalls==std::uint64_t(instanced?1:2),"shadow batching");
        if(retire){adapter.DestroyMesh(mesh);adapter.DestroyShader(shader);adapter.DestroyTexture(tex);}
        adapter.EndFrame();auto p=adapter.ReadBackFramePixels();adapter.Present();return p;
    };
    for(float m:{0.f,.5f,1.f}){
        auto hard=shadowFrame(false,true,m);Require(hard==shadowFrame(true,true,m),"PBR shadow paths differ");
        const auto depth=adapter.ReadBackShadowDepth();auto unshadowed=shadowFrame(true,false,m);
        Require(depth==adapter.ReadBackShadowDepth(),"depth readback");
        for(int c=0;c<3;++c){double base=Decode(texture.pixels[c]/255.)*Decode(scene[0].tint[c]);
            int expected=int(std::lround(255*Encode(base*(1-m)*.2)));
            int at=(120*320+200)*4+c;Require(std::abs(int(hard[at])-expected)<=2,"PBR shadow ambient/metal oracle");
            Require(unshadowed[at]>hard[at]+3,"shadow must reduce direct light");}
        shadows.pcf=true;auto soft=shadowFrame(true,true,m);Require(soft==shadowFrame(false,true,m),"PBR PCF paths differ");shadows.pcf=false;
    }
    glfwSetWindowSize(window.GetGlfwHandle(),400,280);window.PollEvents();
    Require(shadowFrame(true,true,0).size()==400*280*4,"PBR resize readback dimensions");
    shadowFrame(true,true,0,true);Require(adapter.GetValidationErrorCount()==0,"DX12 ERROR/CORRUPTION");adapter.Shutdown();
    std::cout<<"DX12 PBR shadows affect direct diffuse/specular once, ambient approximation preserved, mixed 501 batch and resource retirement passed\n";
}
#endif
int main(int argc,char** argv){try{Cpu();CpuScene();
#if defined(ENABLE_DX12)
    if(argc>1&&std::string(argv[1])=="--dx12")Gpu();
#endif
    return 0;}catch(const std::exception& e){std::cerr<<"PBR regression FAILED: "<<e.what()<<'\n';return 1;}}
