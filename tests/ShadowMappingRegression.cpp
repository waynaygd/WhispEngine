#include "render/ColorPipeline.h"
#define NOMINMAX
#include "render/Shadows.h"
#include "core/AssetPaths.h"
#include "ecs/PhysicsStressScene.h"
#include "ecs/RenderInterpolation.h"
#include "ecs/systems/RenderSystem.h"
#include "resources/ResourceManager.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <limits>
#include <cstring>
#if defined(ENABLE_DX12)
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include "platform/GlfwWindow.h"
#include <GLFW/glfw3.h>
#include "../external/imgui/imgui.h"
#endif
using namespace ecs;
static void Check(bool v,const char* message) {if(!v)throw std::runtime_error(message);}
static ShadowMatrix Model(Vec3 position={},Vec3 scale={1,1,1},Vec3 rotation={}) {
    TransformComponent t;t.position=position;t.scale=scale;t.rotation=rotation;
    ShadowMatrix m;BuildRenderModelMatrix(m.data(),RenderPose::From(t));return m;
}
static void Math() {
    LightingSettings light;light.directional.direction={0,-9,0};ShadowSettings s;
    auto f=BuildDirectionalShadowFrame(BuildLightingConstants(light),s,{0,0,0});
    Check(std::abs(f.constants.lightVP[14]-.5f)<1e-6f,"light center depth is not DX [0,1]");
    const auto subTexel=BuildDirectionalShadowFrame(BuildLightingConstants(light),s,{s.coverage/s.resolution*.2f,0,0});
    Check(f.constants.lightVP==subTexel.constants.lightVP,"sub-texel camera translation changes XY projection");
    const auto large=BuildDirectionalShadowFrame(BuildLightingConstants(light),s,{s.coverage/s.resolution*1.2f,0,0});
    Check(std::abs(std::abs(large.constants.lightVP[12]-f.constants.lightVP[12])-2.f/s.resolution)<1e-6f,"texel snap step");
    for(ShadowVector direction:{ShadowVector{0,0,0},ShadowVector{0,1,0},ShadowVector{1,-1,2}}) {
        light.directional.direction=direction;f=BuildDirectionalShadowFrame(BuildLightingConstants(light),s,{2,3,4});
        for(float c:f.constants.lightVP)Check(std::isfinite(c),"pole/zero direction generates invalid matrix");
    }
    light.directional.enabled=false;Check(BuildDirectionalShadowFrame(BuildLightingConstants(light),s,{}).constants.parameters[0]==0,"disabled directional shadow fallback");
    s.coverage=std::numeric_limits<float>::quiet_NaN();s.resolution=19;s.depthBias=-1;
    auto clean=SanitizeShadowSettings(s);Check(clean.coverage==48&&clean.resolution==1024&&clean.depthBias==0,"shadow sanitation");
    s=ShadowSettings{};light.directional.enabled=true;light.directional.direction={.8f,-1,.3f};
    const auto constants=BuildLightingConstants(light);
    const auto fitted=BuildCameraDirectionalShadowFrame(constants,s,{2,5,-8},0,0,1.04719755f,16.f/9,.01f,150);
    for(float yaw:{0.f,.7f,2.f,3.14f})for(float pitch:{-.9f,0.f,.8f}) {
        const auto frame=BuildCameraDirectionalShadowFrame(constants,s,{2,5,-8},yaw,pitch,1.04719755f,16.f/9,.01f,150);
        Check(frame.projection==fitted.projection,"camera rotation changes fitted projection scale");
        ShadowVector forward{std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
        auto right=ShadowUnit(ShadowCross({0,1,0},forward));auto up=ShadowCross(forward,right);
        for(float distance:{.01f,32.f})for(float x:{-1.f,1.f})for(float y:{-1.f,1.f}) {
            ShadowVector corner{2,5,-8};
            for(int i=0;i<3;++i)corner[i]+=forward[i]*distance+right[i]*x*distance*std::tan(1.04719755f/2)*16.f/9+up[i]*y*distance*std::tan(1.04719755f/2);
            for(int row=0;row<3;++row) {
                float clip=frame.constants.lightVP[12+row];for(int i=0;i<3;++i)clip+=frame.constants.lightVP[i*4+row]*corner[i];
                Check(row==2?(clip>=0&&clip<=1):std::abs(clip)<=1,"fitted frustum loses camera receiver corner");
            }
        }
    }
    s.cameraFitted=false;
    auto legacy=BuildCameraDirectionalShadowFrame(constants,s,{2,5,-8},0,0,1,1,.01f,100);
    Check(legacy.constants.lightVP==BuildDirectionalShadowFrame(constants,s,{2,5,24}).constants.lightVP,"fixed projection compatibility");
    std::cout<<"Light matrix, zero/pole direction, depth convention, texel snapping, sanitation passed\n";
}
struct Recorder:IRenderAdapter {
    std::vector<RenderInstanceData> main,shadow;RenderInstanceData pending;
    RenderSubmissionStatistics stats;std::uint64_t next=1;bool enabled=true,failInstance=false;
    bool Initialize(IWindow*)override{return true;}void BeginFrame()override{main.clear();shadow.clear();stats={};}
    void Clear(float,float,float,float)override{}void SetTestTransform(const float* m)override{std::copy(m,m+16,pending.mvp.begin());}
    void SetModelMatrix(const ShadowMatrix& m)override{pending.model=m;}void SetNormalMatrix(const std::array<float,12>& n)override{pending.normal=n;}
    void SetTestColor(float r,float g,float b,float a)override{pending.tint={r,g,b,a};}
    RenderMeshHandle UploadMesh(const MeshData&)override{return {next++};}
    RenderTextureHandle CreateTexture2D(const TextureData&)override{return {next++};}
    RenderShaderHandle CreateShaderProgram(const ShaderResource&)override{return {next++};}
    bool SupportsInstancing(RenderShaderHandle)const override{return true;}
    bool DrawMeshInstanced(RenderMeshHandle,std::span<const RenderInstanceData> d)override{main.insert(main.end(),d.begin(),d.end());++stats.drawCalls;++stats.instancedDrawCalls;stats.renderedInstances+=d.size();return true;}
    void DrawMesh(RenderMeshHandle)override{main.push_back(pending);++stats.drawCalls;++stats.renderedInstances;}
    bool BeginShadowPass(const ShadowFrame& f)override{return enabled&&f.constants.parameters[0]>.5f;}
    void DrawShadowMesh(RenderMeshHandle,const RenderInstanceData& d)override{shadow.push_back(d);++stats.shadowDrawCalls;++stats.shadowCasters;}
    bool DrawShadowMeshInstanced(RenderMeshHandle,std::span<const RenderInstanceData> d)override{
        if(failInstance)return false;shadow.insert(shadow.end(),d.begin(),d.end());++stats.shadowDrawCalls;++stats.shadowInstancedDrawCalls;stats.shadowCasters+=d.size();return true;
    }
    RenderSubmissionStatistics GetSubmissionStatistics()const override{return stats;}
    void DrawTestTriangle()override{}void DrawTestLine()override{}void DrawTestQuad()override{}void DrawTestCube()override{}
    void EndFrame()override{}void Present()override{}void Shutdown()override{}
};
static void Shared() {
    Recorder adapter;ResourceManager resources(nullptr);resources.Load<MaterialResource>("materials/stress.material.json");RenderSystem render;RenderInterpolation interpolation;
    ShadowSettings shadows;LightingSettings lighting;
    render.SetResourceManager(&resources);render.SetRenderAdapter(&adapter);render.SetInterpolation(&interpolation);
    render.SetShadowSettings(&shadows);render.SetLightingSettings(&lighting);
    resources.Load<MeshResource>("models/validation_cube.obj");resources.Load<ShaderResource>("dx12/textured.hlsl");resources.Load<TextureResource>("defaults/texture");
    World world;std::vector<Entity> entities;
    auto frame=[&] {
        adapter.BeginFrame();render.Update(world,0);
        Check(adapter.shadow.size()==adapter.main.size(),"caster count differs from main prepared poses");
        for(std::size_t i=0;i<adapter.main.size();++i)Check(adapter.shadow[i].model==adapter.main[i].model&&adapter.shadow[i].normal==adapter.main[i].normal,"shadow/main interpolation poses differ");
    };
    for(int count:{100,250,500,1000}) {
        CreatePhysicsStressScene(world,entities,count);interpolation.Clear();interpolation.BeginTick(world);
        auto& t=*world.GetComponent<TransformComponent>(entities.back());t.position.x+=1;t.rotation={.3f,.8f,.1f};t.scale={2,.7f,1};
        interpolation.EndTick(world);
        for(int warm=0;warm<10;++warm){adapter.BeginFrame();render.Update(world,0);}
        for(bool interpolated:{false,true})for(float alpha:{0.f,.5f,.999f})for(bool instancing:{false,true}) {
            interpolation.SetEnabled(interpolated);
            interpolation.SetFrame(world,true,alpha);render.SetInstancingEnabled(instancing);frame();
            Check(adapter.main.size()==std::size_t(count+1),"stress objects missing");
            Check(adapter.stats.shadowDrawCalls==std::uint64_t(instancing?1:count+1),"shadow batching lost");
        }
        interpolation.SetEnabled(true);
        t.position={10,20,30};interpolation.ResetEntity(world,entities.back());interpolation.SetFrame(world,true,.5f);frame();
        interpolation.SetFrame(world,false,.5f);frame();interpolation.SetFrame(world,true,.5f);frame();
        adapter.failInstance=true;frame();Check(adapter.stats.shadowDrawCalls==std::uint64_t(count+1),"shadow instance fallback missing");adapter.failInstance=false;
        world.DestroyEntity(entities.back());entities.pop_back();frame();
        world.Clear();interpolation.Clear();frame();Check(adapter.main.empty(),"clear retains casters");
    }
    adapter.enabled=false;CreatePhysicsStressScene(world,entities,500);adapter.BeginFrame();render.Update(world,0);
    Check(adapter.shadow.empty()&&adapter.main.size()==501,"unsupported shadow backend breaks main rendering");
    shadows.enabled=false;adapter.enabled=true;adapter.BeginFrame();render.Update(world,0);Check(adapter.shadow.empty(),"disabled shadows submitted");
    render.ReleaseGpuResources();
    std::cout<<"100/250/500/1000 batching, matching interpolated poses, rotation/scale, pause/teleport/restart/clear/delete/reuse and fallback passed\n";
}
#if defined(ENABLE_DX12)
static constexpr int Width=320,Height=240;
static void SavePpm(const std::filesystem::path& path,const std::vector<std::uint8_t>& pixels) {
    std::ofstream f(path,std::ios::binary);f<<"P6\n"<<Width<<' '<<Height<<"\n255\n";
    for(std::size_t i=0;i<pixels.size();i+=4)f.write(reinterpret_cast<const char*>(pixels.data()+i),3);
}
static std::size_t Pixel(float x,float z) {return std::size_t(int((.5f-z/12)*Height)*Width+int((.5f+x/16)*Width))*4;}
static void Gpu() {
    GlfwWindow window;Check(window.Create(Width,Height,"DX12 Shadow Regression"),"window");
    Dx12RenderAdapter adapter;Check(adapter.Initialize(&window),"DX12 initialize");ResourceManager resources(nullptr);resources.Load<MaterialResource>("materials/stress.material.json");
    // These analytic Lambert/texture oracles test identity SDR output. HDR
    // Output 2.0 applies the default tone mapping even when IBL is disabled.
    EnvironmentSettings output;output.toneMapping=false;output.exposure=0;
    adapter.SetEnvironmentSettings(output);
    const auto sr=resources.Load<ShaderResource>("dx12/textured.hlsl");auto shader=adapter.CreateShaderProgram(sr->GetData());
    Check(shader.IsValid()&&adapter.SupportsInstancing(shader),"shadow-aware material PSOs");
    const auto cubeResource=resources.Load<MeshResource>("models/validation_cube.obj");auto cube=adapter.UploadMesh(cubeResource->GetData().meshData);
    TextureData texture;texture.width=texture.height=1;texture.pixels={128,192,64,255};auto tex=adapter.CreateTexture2D(texture);
    LightingSettings lighting;lighting.directional.direction={1,-1,0};lighting.directional.color={1,1,1};lighting.directional.intensity=.8f;lighting.ambientIntensity=.2f;
    ShadowSettings shadows;shadows.coverage=16;shadows.depthRange=30;shadows.pcf=false;shadows.resolution=512;
    // Top-down LH camera: x maps horizontally, z vertically; DX orthographic depth [0,1].
    const ShadowMatrix view{1,0,0,0,0,0,-1,0,0,1,0,0,0,0,10,1};
    const ShadowMatrix projection{1.f/8,0,0,0,0,1.f/6,0,0,0,0,1.f/20,0,0,0,0,1};
    const auto vp=ShadowMultiply(projection,view);
    std::vector<RenderInstanceData> instances(2);
    auto set=[&](int i,ShadowMatrix m) {instances[i].model=m;instances[i].mvp=ShadowMultiply(vp,m);instances[i].normal=BuildNormalMatrix(m);instances[i].tint={.8f,.7f,.6f,1};};
    set(0,Model({0,-.1f,0},{14,.2f,10}));set(1,Model({0,2,0}));
    auto frame=[&](bool instanced,bool release=false) {
        window.PollEvents();adapter.BeginFrame();adapter.Clear(.03f,.04f,.05f,1);adapter.SetLighting(BuildLightingConstants(lighting));
        const auto f=BuildDirectionalShadowFrame(BuildLightingConstants(lighting),shadows,{});
        const bool active=adapter.BeginShadowPass(f);
        if(active) {
            if(instanced)Check(adapter.DrawShadowMeshInstanced(cube,instances),"depth instance draw");
            else for(const auto& d:instances)adapter.DrawShadowMesh(cube,d);
            adapter.EndShadowPass();
        }
        adapter.BeginMainPass();adapter.BindShader(shader);adapter.BindTexture(0,tex);
        if(instanced)Check(adapter.DrawMeshInstanced(cube,instances),"main instance draw");
        else for(const auto& d:instances){adapter.SetModelMatrix(d.model);adapter.SetTestTransform(d.mvp.data());adapter.SetTestColor(d.tint[0],d.tint[1],d.tint[2],1);adapter.SetNormalMatrix(d.normal);adapter.DrawMesh(cube);}
        adapter.EndMainPass();const auto stats=adapter.GetSubmissionStatistics();
        Check(stats.drawCalls==std::uint64_t(instanced?1:instances.size()),"main draw counts");
        Check(stats.shadowDrawCalls==std::uint64_t(active?(instanced?1:instances.size()):0),"shadow draw counts");
        if(instanced)Check(stats.uploadBytes==instances.size()*sizeof(RenderInstanceData),"shadow/main instance upload was duplicated");
        if(release){adapter.ReleaseShadowResources();adapter.DestroyMesh(cube);adapter.DestroyShader(shader);adapter.DestroyTexture(tex);} // already recorded commands must retain resources
        adapter.EndFrame();auto pixels=adapter.ReadBackFramePixels();adapter.Present();
        Check(adapter.GetGpuTimings().valid,"timestamp readback invalid");return pixels;
    };
    auto pair=[&] {auto a=frame(false);auto b=frame(true);int maxError=0;for(std::size_t i=0;i<a.size();++i)maxError=std::max(maxError,std::abs(int(a[i])-int(b[i])));Check(maxError<=1,"ordinary/instanced shadows differ");return b;};
    std::filesystem::create_directories("shadow-readback");
    std::vector<std::uint8_t> fault;
    {
        struct RestoreSource {
            std::filesystem::path path=AssetPaths::ResolveShaderPath("dx12/shadow_depth.hlsl");
            std::string original;
            RestoreSource(){std::ifstream in(path,std::ios::binary);original.assign(std::istreambuf_iterator<char>(in),{});Check(!original.empty(),"shadow source fixture missing");std::ofstream(path)<<"invalid HLSL regression fixture";}
            ~RestoreSource(){std::ofstream out(path,std::ios::binary);out<<original;}
        } restore;
        fault=frame(true);Check(adapter.GetSubmissionStatistics().shadowDrawCalls==0,"failed shadow shader must use lit fallback");
    }
    shadows.enabled=false;auto off=pair();Check(off==fault,"failed shadow shader changes Lambert rendering");SavePpm("shadow-readback/off.ppm",off);
    shadows.enabled=true;auto hard=pair();SavePpm("shadow-readback/hard.ppm",hard);
    auto bright=Pixel(-3,0),dark=Pixel(2,0);
    std::cout<<"Floor red off="<<int(off[dark])<<" hard="<<int(hard[dark])<<" bright="<<int(hard[bright])<<'\n';
    Check(std::abs(int(hard[bright])-int(off[bright]))<=1,"clear floor incorrectly shadowed (acne)");
    Check(int(hard[dark])+30<int(off[dark]),"caster does not shadow floor");
    const float base[]={128*.8f,192*.7f,64*.6f};
    for(int i=0;i<3;++i)Check(std::abs(int(hard[dark+i])-int(std::lround(255*LinearToSrgb(SrgbToLinear(base[i]/((i==0)?.8f:(i==1)?.7f:.6f)/255)*SrgbToLinear((i==0)?.8f:(i==1)?.7f:.6f)*.2f))))<=2,"shadow must preserve ambient, texture and tint");
    auto depth=adapter.ReadBackShadowDepth();Check(depth.size()==512*512,"depth map readback dimensions");
    const auto minimum=*std::min_element(depth.begin(),depth.end());Check(minimum>0&&minimum<.6f&&*std::max_element(depth.begin(),depth.end())==1,"depth map is empty or invalid");
    for(float d:depth)Check(std::isfinite(d)&&d>=0&&d<=1,"depth outside [0,1]");
    {std::ofstream f("shadow-readback/depth.pgm",std::ios::binary);f<<"P5\n512 512\n255\n";for(float d:depth){unsigned char c=static_cast<unsigned char>(d*255);f.write(reinterpret_cast<char*>(&c),1);}}
    shadows.pcf=true;auto pcf=pair();SavePpm("shadow-readback/pcf.ppm",pcf);
    int intermediate=0;for(std::size_t p=0;p<pcf.size();p+=4)if(off[p]==off[bright]&&hard[p]<off[p]-30&&pcf[p]>hard[p]+2&&pcf[p]<off[p]-2)++intermediate;
    Check(intermediate>5,"3x3 PCF produces no fractional edge samples");
    shadows.strength=.5f;auto half=pair();Check(half[dark]>pcf[dark]+10&&half[dark]<off[dark]-10,"shadow strength");shadows.strength=1;
    set(1,Model({-3,2,0},{1.4f,.8f,.6f},{.1f,.4f,.2f}));auto moved=pair();Check(moved[dark]>hard[dark]+30,"moving rotated nonuniform caster leaves stale shadow");SavePpm("shadow-readback/moved.ppm",moved);
    set(1,Model({0,2,0}));set(0,Model({0,1.3f,0},{14,.2f,10}));auto receiverMoved=pair();
    Check(receiverMoved[dark]>hard[dark]+30,"moving receiver leaves stale shadow coordinates");
    set(0,Model({0,-.1f,0},{14,.2f,10}));lighting.directional.direction={-1,-1,0};auto reversed=pair();Check(reversed[dark]>hard[dark]+30,"light direction does not move shadow");
    lighting.directional.direction={1,-1,0};lighting.directional.enabled=false;auto ambient=pair();Check(std::abs(int(ambient[bright])-int(hard[dark]))<=2,"ambient-only changed by shadow");
    lighting.directional.enabled=true;lighting.directional.intensity=0;auto zeroIntensity=pair();Check(zeroIntensity==ambient,"zero intensity must disable shadows");lighting.directional.intensity=.8f;
    lighting.directional.direction={0,0,0};pair();lighting.directional.direction={0,-1,0};auto zeroDirection=pair();Check(frame(true)==zeroDirection,"zero direction unstable");lighting.directional.direction={1,-1,0};
    lighting.enabled=false;auto unlit=pair();Check(std::abs(int(unlit[dark])-int(std::lround(base[0])))<=2,"unlit fallback");lighting.enabled=true;
    shadows.coverage=2;auto outside=pair();Check(std::abs(int(outside[bright])-int(off[bright]))<=1,"outside shadow frustum darkens surface");shadows.coverage=16;
    for(unsigned resolution:{1024,2048,4096,512}) {shadows.resolution=resolution;shadows.depthBias+=1;shadows.slopeBias+=.1f;pair();}
    const auto initialInstances=instances;
    instances.resize(501);
    for(int i=1;i<=500;++i)set(i,Model({(i%25-12)*.45f,1.5f+(i/125)*.7f,((i/25)%5-2)*.6f},{.3f,.4f,.25f},{.1f,.03f*i,.2f}));
    pair();const auto batchStats=adapter.GetSubmissionStatistics();
    Check(batchStats.drawCalls==1&&batchStats.shadowDrawCalls==1&&batchStats.shadowInstancedDrawCalls==1&&batchStats.shadowCasters==501,"DX12 500 cubes + floor shadow batching lost");
    instances=initialInstances;
    frame(true,true);cube=adapter.UploadMesh(cubeResource->GetData().meshData);shader=adapter.CreateShaderProgram(sr->GetData());tex=adapter.CreateTexture2D(texture);pair();Check(adapter.GetValidationErrorCount()==0,"DX12 debug validation errors");
    // A complex imported mesh casts onto the floor; then receives a shadow from a larger elevated caster.
    for(const char* key:{"models/african_head.obj","models/lighting_sphere.gltf"}) {
        auto resource=resources.Load<MeshResource>(key);Check(resource&&resource->IsUsable(),"import model");
        auto mesh=resource->GetData().meshData;float center[3]{},minimum[3]={1e9f,1e9f,1e9f},maximum[3]={-1e9f,-1e9f,-1e9f};
        for(const auto& v:mesh.vertices)for(int i=0;i<3;++i){minimum[i]=std::min(minimum[i],v.position[i]);maximum[i]=std::max(maximum[i],v.position[i]);}
        float extent=0;for(int i=0;i<3;++i){center[i]=(minimum[i]+maximum[i])*.5f;extent=std::max(extent,maximum[i]-minimum[i]);}
        for(auto& v:mesh.vertices)for(int i=0;i<3;++i)v.position[i]=(v.position[i]-center[i])*2/extent;
        const auto complex=adapter.UploadMesh(mesh);RenderInstanceData model;model.model=Model({0,1.2f,0});model.mvp=ShadowMultiply(vp,model.model);model.normal=BuildNormalMatrix(model.model);model.tint={.8f,.7f,.6f,1};
        auto complexFrame=[&](bool shadowEnabled,bool elevated,bool instanced=false) {
            shadows.enabled=shadowEnabled;window.PollEvents();adapter.BeginFrame();adapter.Clear(.03f,.04f,.05f,1);adapter.SetLighting(BuildLightingConstants(lighting));
            if(adapter.BeginShadowPass(BuildDirectionalShadowFrame(BuildLightingConstants(lighting),shadows,{}))) {
                adapter.DrawShadowMesh(cube,instances[0]);
                if(instanced)Check(adapter.DrawShadowMeshInstanced(complex,std::span<const RenderInstanceData>(&model,1)),"complex shadow instance");
                else adapter.DrawShadowMesh(complex,model);
                if(elevated) {auto caster=instances[1];caster.model=Model({-3,4.2f,0},{3,1,3});adapter.DrawShadowMesh(cube,caster);}
                adapter.EndShadowPass();
            }
            adapter.BeginMainPass();adapter.BindShader(shader);adapter.BindTexture(0,tex);
            auto draw=[&](RenderMeshHandle handle,const RenderInstanceData& d){adapter.SetModelMatrix(d.model);adapter.SetTestTransform(d.mvp.data());adapter.SetTestColor(d.tint[0],d.tint[1],d.tint[2],1);adapter.SetNormalMatrix(d.normal);adapter.DrawMesh(handle);};
            draw(cube,instances[0]);
            if(instanced)Check(adapter.DrawMeshInstanced(complex,std::span<const RenderInstanceData>(&model,1)),"complex main instance");else draw(complex,model);
            if(elevated){auto caster=instances[1];caster.model=Model({-3,4.2f,0},{3,1,3});caster.mvp=ShadowMultiply(vp,caster.model);caster.normal=BuildNormalMatrix(caster.model);draw(cube,caster);}
            adapter.EndMainPass();adapter.EndFrame();auto image=adapter.ReadBackFramePixels();adapter.Present();return image;
        };
        auto noShadow=complexFrame(false,false),cast=complexFrame(true,false),receive=complexFrame(true,true);
        Check(cast==complexFrame(true,false,true)&&receive==complexFrame(true,true,true),"complex model ordinary/instanced shadow mismatch");
        int floorChanges=0,modelChanges=0;
        for(int y=0;y<Height;++y)for(int x=0;x<Width;++x) {std::size_t p=(y*Width+x)*4;
            if(x>Width/2+25&&cast[p]+10<noShadow[p])++floorChanges;
            if(std::abs(x-Width/2)<20&&std::abs(y-Height/2)<20&&receive[p]+8<cast[p])++modelChanges;
        }
        std::cout<<key<<": floor shadow pixels="<<floorChanges<<", receiver pixels="<<modelChanges<<'\n';
        Check(floorChanges>10&&modelChanges>10,"complex OBJ/glTF does not cast and receive shadows");
        const auto name=std::filesystem::path(key).stem().string();SavePpm("shadow-readback/"+name+"-cast.ppm",cast);SavePpm("shadow-readback/"+name+"-receive.ppm",receive);
        adapter.DestroyMesh(complex);
    }
    adapter.ReleaseShadowResources();std::vector<RenderTextureHandle> allocations;
    for(int i=0;i<128;++i){auto t=adapter.CreateTexture2D(texture);if(!t.IsValid())break;allocations.push_back(t);}
    Check(!allocations.empty(),"descriptor exhaustion fixture");shadows.enabled=true;auto exhausted=frame(true);
    Check(adapter.GetSubmissionStatistics().shadowDrawCalls==0,"shadow descriptor exhaustion does not fallback");
    shadows.enabled=false;Check(exhausted==frame(true),"resource failure changes main rendering");
    for(auto t:allocations)adapter.DestroyTexture(t);shadows.enabled=true;pair();
    Check(adapter.GetValidationErrorCount()==0,"DX12 debug validation errors after complex models/resource fallback");
    Check(adapter.InitializeEditorUi(&window),"shadow preview ImGui initialization");
    for(unsigned resolution:{1024,512,2048}) {
        window.PollEvents();adapter.BeginFrame();adapter.Clear(.03f,.04f,.05f,1);adapter.BeginEditorUiFrame();
        ImGui::SetNextWindowPos(ImVec2(5,5));ImGui::SetNextWindowSize(ImVec2(180,210));ImGui::Begin("Shadow preview regression");
        const auto preview=adapter.GetShadowPreviewTextureId();Check(preview!=0,"shadow preview SRV missing");
        ImGui::Image((ImTextureID)preview,ImVec2(128,128));ImGui::End();
        shadows.resolution=resolution;
        if(adapter.BeginShadowPass(BuildDirectionalShadowFrame(BuildLightingConstants(lighting),shadows,{}))) {
            Check(adapter.DrawShadowMeshInstanced(cube,instances),"preview depth draw");adapter.EndShadowPass();
        }
        adapter.BeginMainPass();adapter.EndMainPass();adapter.RenderEditorUiFrame();
        adapter.ReleaseShadowResources();adapter.EndFrame();adapter.Present();
        // Recreate a map for the next frame's previous-map preview.
        pair();
    }
    Check(adapter.GetValidationErrorCount()==0,"preview resize/retirement validation errors");adapter.ShutdownEditorUi();
    adapter.DestroyMesh(cube);adapter.DestroyShader(shader);adapter.DestroyTexture(tex);adapter.Shutdown();
    std::cout<<"DX12 depth and color readback, ambient/tint/texture, hard/PCF, strength, moving caster/light, frustum fallback, resolution/resource lifetime, OBJ/glTF cast/receive passed\n";
}
#endif
int main(int argc,char** argv) {
    try {Math();Shared();
#if defined(ENABLE_DX12)
        if(argc>1&&std::strcmp(argv[1],"--dx12")==0)Gpu();
#endif
        return 0;
    }catch(const std::exception& e){std::cerr<<"Shadow regression FAILED: "<<e.what()<<'\n';return 1;}
}

