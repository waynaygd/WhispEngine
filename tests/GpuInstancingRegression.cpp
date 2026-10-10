#include "ecs/PhysicsStressScene.h"
#include "ecs/RenderInterpolation.h"
#include "ecs/systems/RenderSystem.h"
#include "resources/ResourceManager.h"
#include "render/IRenderAdapter.h"
#include <iostream>
#include <stdexcept>
#include <cstring>
#if defined(ENABLE_DX12)
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include "platform/GlfwWindow.h"
#include <GLFW/glfw3.h>
#endif
using namespace ecs;
static void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}

struct BatchRecorder:IRenderAdapter {
    struct Draw {RenderMeshHandle mesh;RenderShaderHandle shader;RenderTextureHandle texture;std::vector<RenderInstanceData> instances;};
    std::vector<Draw> draws;RenderInstanceData pending;RenderShaderHandle shader;RenderTextureHandle texture;
    std::uint64_t nextMesh=1,nextTexture=1,nextShader=1;std::vector<std::uint64_t> supported;
    RenderSubmissionStatistics stats;bool failInstanced=false;
    bool Initialize(IWindow*)override{return true;}void BeginFrame()override{draws.clear();stats={};}
    void Clear(float,float,float,float)override{}void SetTestTransform(const float* p)override{std::copy(p,p+16,pending.mvp.begin());}
    void SetTestColor(float r,float g,float b,float a)override{pending.tint={r,g,b,a};}
    RenderMeshHandle UploadMesh(const MeshData&)override{return {nextMesh++};}
    RenderTextureHandle CreateTexture2D(const TextureData&)override{return {nextTexture++};}
    RenderShaderHandle CreateShaderProgram(const ShaderResource& s)override{auto n=nextShader++;if(s.vertexSource.find("WHISP_INSTANCE_LAYOUT_V1")!=std::string::npos)supported.push_back(n);return {n};}
    void BindShader(RenderShaderHandle s)override{shader=s;}void BindTexture(std::uint32_t,RenderTextureHandle t)override{texture=t;}
    bool SupportsInstancing(RenderShaderHandle s)const override{return std::find(supported.begin(),supported.end(),s.value)!=supported.end();}
    bool DrawMeshInstanced(RenderMeshHandle mesh,std::span<const RenderInstanceData> data)override{
        if(failInstanced)return false;
        draws.push_back({mesh,shader,texture,{data.begin(),data.end()}});++stats.drawCalls;++stats.instancedDrawCalls;stats.renderedInstances+=data.size();return true;
    }
    RenderSubmissionStatistics GetSubmissionStatistics()const override{return stats;}
    void DrawMesh(RenderMeshHandle m)override{draws.push_back({m,shader,texture,{pending}});++stats.drawCalls;++stats.renderedInstances;}
    void DrawTestTriangle()override{}void DrawTestLine()override{}void DrawTestQuad()override{}void DrawTestCube()override{}
    void EndFrame()override{}void Present()override{}void Shutdown()override{}
    std::vector<RenderInstanceData> Flatten()const{std::vector<RenderInstanceData> result;for(const auto& d:draws)result.insert(result.end(),d.instances.begin(),d.instances.end());return result;}
};
static void SharedPath() {
    BatchRecorder adapter;ResourceManager resources(nullptr);RenderSystem render;
    render.SetResourceManager(&resources);render.SetRenderAdapter(&adapter);
    resources.Load<MeshResource>("models/validation_cube.obj");resources.Load<ShaderResource>("dx12/textured.hlsl");resources.Load<TextureResource>("defaults/texture");
    World world;std::vector<Entity> entities;RenderInterpolation interpolation;render.SetInterpolation(&interpolation);
    for(int count:{100,250,500,1000}) {
        CreatePhysicsStressScene(world,entities,count);interpolation.Clear();interpolation.BeginTick(world);
        for(auto e:entities)if(!world.GetComponent<RigidbodyComponent>(e)->isStatic) {
            auto& t=*world.GetComponent<TransformComponent>(e);t.position.x+=.2f;t.rotation={.1f,.3f,.2f};
        }
        interpolation.EndTick(world);interpolation.SetFrame(world,true,.5);
        for(int warm=0;warm<10;++warm){adapter.BeginFrame();render.Update(world,0);}
        auto compare=[&] {
            render.SetInstancingEnabled(false);adapter.BeginFrame();render.Update(world,0);const auto ordinary=adapter.Flatten();
            Check(ordinary.size()==std::size_t(count+1),"ordinary missing ready objects");
            render.SetInstancingEnabled(true);adapter.BeginFrame();render.Update(world,0);const auto batched=adapter.Flatten();
            Check(adapter.draws.size()==1&&adapter.stats.instancedDrawCalls==1&&batched.size()==ordinary.size(),"compatible scene not one real submission");
            for(std::size_t n=0;n<ordinary.size();++n)Check(ordinary[n].mvp==batched[n].mvp&&ordinary[n].tint==batched[n].tint,"instance MVP/tint differs from ordinary path");
            Check(render.GetStatistics().submitted.drawCalls==1&&render.GetStatistics().submitted.renderedInstances==std::size_t(count+1),"actual adapter submission stats lost");
            bool distinct=false;for(const auto& p:batched)if(p.tint!=batched[0].tint)distinct=true;
            Check(distinct,"individual tint collapsed");
        };
        auto checkModel=[&](Entity entity,const char* message) {
            std::array<float,16> expected;
            BuildRenderModelMatrix(expected.data(),interpolation.Resolve(entity,*world.GetComponent<TransformComponent>(entity)));
            const auto submitted=adapter.Flatten();
            Check(std::any_of(submitted.begin(),submitted.end(),[&](const auto& d){return d.model==expected;}),message);
        };
        compare();checkModel(entities.back(),"interpolated model lost");
        interpolation.SetEnabled(false);compare();checkModel(entities.back(),"non-interpolated model lost");interpolation.SetEnabled(true);
        auto& edited=*world.GetComponent<TransformComponent>(entities.back());
        edited.scale={1.3f,.9f,1.1f};edited.position={10,30,40};
        interpolation.ResetEntity(world,entities.back());interpolation.SetFrame(world,true,.5);
        compare();checkModel(entities.back(),"teleport or individual scale retained stale model");
        interpolation.SetFrame(world,false,.5);compare();checkModel(entities.back(),"pause retained stale model");
        interpolation.SetFrame(world,true,.5);compare();checkModel(entities.back(),"resume retained stale model");
        const auto before=*world.GetComponent<TransformComponent>(entities.back());
        render.SetCameraProjection(1.047f,1.2f,.01f,150);adapter.BeginFrame();render.Update(world,0);auto first=adapter.Flatten();
        render.SetCameraProjection(1.047f,2.f,.01f,150);adapter.BeginFrame();render.Update(world,0);auto second=adapter.Flatten();
        Check(first[0].mvp!=second[0].mvp,"second camera reused MVP");
        Check(world.GetComponent<TransformComponent>(entities.back())->position.x==before.position.x&&world.GetComponent<TransformComponent>(entities.back())->rotation.y==before.rotation.y,"instancing wrote ECS Transform");
        float physicalModel[16];BuildRenderModelMatrix(physicalModel,RenderPose::From(*world.GetComponent<TransformComponent>(entities[0])));
        const auto floorModel=std::array<float,16>{physicalModel[0],physicalModel[1],physicalModel[2],physicalModel[3],physicalModel[4],physicalModel[5],physicalModel[6],physicalModel[7],physicalModel[8],physicalModel[9],physicalModel[10],physicalModel[11],physicalModel[12],physicalModel[13],physicalModel[14],physicalModel[15]};
        Check(std::any_of(second.begin(),second.end(),[&](const auto& d){return d.model==floorModel;}),"model matrices not preserved");
        const auto old=entities.back();world.DestroyEntity(old);interpolation.SetFrame(world,true,.5);adapter.BeginFrame();render.Update(world,0);Check(adapter.stats.renderedInstances==std::size_t(count),"deleted instance retained");
        auto replacement=world.CreateEntity();Check(replacement.index==old.index&&replacement.generation!=old.generation,"reuse setup");
        world.AddComponent<TransformComponent>(replacement).position={99,98,97};world.AddComponent<RigidbodyComponent>(replacement);world.AddComponent<ColliderComponent>(replacement);
        world.AddComponent<MeshRendererComponent>(replacement).meshPath="models/validation_cube.obj";
        world.AddComponent<MaterialComponent>(replacement).shaderPath="dx12/textured.hlsl";
        interpolation.SetFrame(world,true,.5);adapter.BeginFrame();render.Update(world,0);Check(adapter.stats.renderedInstances==std::size_t(count+1),"generation replacement missing");
        checkModel(replacement,"generation reuse retained old model");
        adapter.failInstanced=true;adapter.BeginFrame();render.Update(world,0);Check(adapter.stats.instancedDrawCalls==0&&adapter.stats.drawCalls==std::size_t(count+1),"failed instance upload did not fallback");adapter.failInstanced=false;
        world.Clear();interpolation.SetFrame(world,true,.5);adapter.BeginFrame();render.Update(world,0);Check(adapter.draws.empty()&&render.GetStatistics().visibleObjects==0,"Clear retained instance data");
    }
    CreatePhysicsStressScene(world,entities,12);
    auto altered=entities[4];world.GetComponent<MeshRendererComponent>(altered)->meshPath="models/missing-instancing.obj";
    world.GetComponent<MaterialComponent>(entities[5])->texturePath="textures/validation_checker.png";
    world.GetComponent<MaterialComponent>(entities[6])->shaderPath="dx12/missing-instancing.hlsl";
    resources.Load<MeshResource>("models/missing-instancing.obj");resources.Load<TextureResource>("textures/validation_checker.png");resources.Load<ShaderResource>("dx12/missing-instancing.hlsl");
    for(int warm=0;warm<15;++warm){adapter.BeginFrame();render.Update(world,0);}
    Check(adapter.stats.renderedInstances==13&&adapter.draws.size()>1,"incompatible resources combined or missing fallback");
    for(const auto& draw:adapter.draws)if(!adapter.SupportsInstancing(draw.shader))Check(draw.instances.size()==1,"unsupported shader batched");
    std::vector<RenderMeshHandle> meshes;std::vector<RenderTextureHandle> textures;
    for(const auto& d:adapter.draws){meshes.push_back(d.mesh);textures.push_back(d.texture);}
    Check(std::any_of(meshes.begin(),meshes.end(),[&](auto h){return h!=meshes[0];})&&std::any_of(textures.begin(),textures.end(),[&](auto h){return h!=textures[0];}),"distinct mesh/texture handles lost");
    // Async resource not ready: gather must tolerate the pending state.
    auto pending=world.CreateEntity();world.AddComponent<TransformComponent>(pending);world.AddComponent<MeshRendererComponent>(pending).meshPath="models/another-pending.obj";
    world.AddComponent<MaterialComponent>(pending).shaderPath="dx12/textured.hlsl";adapter.BeginFrame();render.Update(world,0);
    auto unique=world.CreateEntity();world.AddComponent<TransformComponent>(unique);world.AddComponent<MeshRendererComponent>(unique).meshPath="models/validation_cube.obj";
    world.AddComponent<MaterialComponent>(unique).shaderPath="dx12/textured.hlsl";adapter.BeginFrame();render.Update(world,0);
    render.ReleaseGpuResources();render.SetRenderAdapter(nullptr);
    std::cout<<"Shared instancing batches/matrices/tints/cameras/generations/clear/fallback regression passed\n";
}

#if defined(ENABLE_DX12)
static void Dx12Pixels() {
    GlfwWindow window;Check(window.Create(320,240,"WhispEngine instancing regression"),"DX12 test window");
    Dx12RenderAdapter adapter;Check(adapter.Initialize(&window),"DX12 initialization");
    ResourceManager resources(nullptr);
    const auto meshResource=resources.Load<MeshResource>("models/validation_cube.obj");
    const auto shaderResource=resources.Load<ShaderResource>("dx12/textured.hlsl");
    const auto textureResource=resources.Load<TextureResource>("textures/validation_checker.png");
    auto mesh=adapter.UploadMesh(meshResource->GetData().meshData);
    auto shader=adapter.CreateShaderProgram(shaderResource->GetData());auto texture=adapter.CreateTexture2D(textureResource->GetData().textureData);
    Check(mesh.IsValid()&&shader.IsValid()&&texture.IsValid()&&adapter.SupportsInstancing(shader),"instanced DX12 shader unavailable");
    World world;std::vector<Entity> entities;RenderInterpolation history;
    CreatePhysicsStressScene(world,entities,500);history.BeginTick(world);
    for(auto e:entities) {auto& t=*world.GetComponent<TransformComponent>(e);if(!world.GetComponent<RigidbodyComponent>(e)->isStatic){t.position.y-=.1f;t.rotation={.1f,.2f,.3f};}}
    history.EndTick(world);history.SetFrame(world,true,.5);
    std::vector<RenderInstanceData> instances;
    for(auto e:entities){RenderInstanceData data;BuildRenderModelMatrix(data.model.data(),history.Resolve(e,*world.GetComponent<TransformComponent>(e)));
        // Orthographic clip transform chosen for a deterministic, visible GPU test.
        data.mvp=data.model;for(int column=0;column<4;++column){data.mvp[column*4]*=.07f;data.mvp[column*4+1]*=.07f;data.mvp[column*4+2]*=.005f;}
        data.mvp[13]-=.45f;data.tint={.2f+float(e.index%5)*.1f,.6f,.8f,1};instances.push_back(data);}
    auto frame=[&](bool enabled) {
        window.PollEvents();adapter.BeginFrame();adapter.Clear(.08f,.1f,.13f,1);adapter.BindShader(shader);adapter.BindTexture(0,texture);
        if(enabled)Check(adapter.DrawMeshInstanced(mesh,instances),"real instanced command failed");
        else for(const auto& data:instances){adapter.SetTestTransform(data.mvp.data());adapter.SetTestColor(data.tint[0],data.tint[1],data.tint[2],data.tint[3]);adapter.DrawMesh(mesh);}
        const auto stats=adapter.GetSubmissionStatistics();Check(stats.renderedInstances==instances.size()&&stats.drawCalls==(enabled?1:instances.size()),"real DX12 draw statistics");
        adapter.EndFrame();auto pixels=adapter.ReadBackFramePixels();adapter.Present();return pixels;
    };
    const auto ordinary=frame(false),instanced=frame(true);Check(ordinary==instanced,"GPU pixels differ between ordinary and instanced paths");
    std::size_t varied=0;for(std::size_t p=4;p<ordinary.size();p+=4)if(std::memcmp(ordinary.data(),ordinary.data()+p,4)!=0)++varied;
    Check(varied>100,"GPU comparison rendered only clear color");
    for(int n=0;n<6;++n)frame(true);const auto growth=adapter.GetSubmissionStatistics().instanceBufferCapacity;
    glfwSetWindowSize(window.GetGlfwHandle(),400,280);for(int n=0;n<4;++n)frame(true);
    Check(adapter.GetSubmissionStatistics().instanceBufferCapacity==growth,"Resize leaked instance pages");
    ShaderResource unsupported=shaderResource->GetData();auto marker=unsupported.vertexSource.find("WHISP_INSTANCE_LAYOUT_V1");unsupported.vertexSource.erase(marker,std::strlen("WHISP_INSTANCE_LAYOUT_V1"));
    const auto fallback=adapter.CreateShaderProgram(unsupported);Check(fallback.IsValid()&&!adapter.SupportsInstancing(fallback),"untagged shader not fallback");
    const auto resizedOrdinary=frame(false);
    adapter.BeginFrame();adapter.Clear(.08f,.1f,.13f,1);adapter.BindShader(fallback);adapter.BindTexture(0,texture);
    Check(!adapter.DrawMeshInstanced(mesh,instances),"unsupported pipeline recorded instance command");
    for(const auto& data:instances){adapter.SetTestTransform(data.mvp.data());adapter.SetTestColor(data.tint[0],data.tint[1],data.tint[2],data.tint[3]);adapter.DrawMesh(mesh);}
    adapter.EndFrame();const auto fallbackPixels=adapter.ReadBackFramePixels();adapter.Present();Check(resizedOrdinary==fallbackPixels,"untagged shader normal fallback pixels differ");
    adapter.DestroyShader(fallback);
    ShaderResource wrongContract=shaderResource->GetData();
    const auto tintRead=wrongContract.fragmentSource.find("input.tint.rgb");wrongContract.fragmentSource.replace(tintRead,std::strlen("input.tint.rgb"),"gTint.rgb");
    const auto rejected=adapter.CreateShaderProgram(wrongContract);
    Check(rejected.IsValid()&&!adapter.SupportsInstancing(rejected),"per-draw CB shader incorrectly advertised instancing");adapter.DestroyShader(rejected);
    // Grow another page after an earlier command references the first page.
    adapter.BeginFrame();adapter.Clear(.08f,.1f,.13f,1);adapter.BindShader(shader);adapter.BindTexture(0,texture);
    Check(adapter.DrawMeshInstanced(mesh,instances),"first page draw");
    std::vector<RenderInstanceData> bigger(1200,instances.front());Check(adapter.DrawMeshInstanced(mesh,bigger),"mid-frame page growth draw");
    adapter.DestroyMesh(mesh);adapter.DestroyTexture(texture);adapter.DestroyShader(shader); // Retire unsubmitted resources/descriptors.
    adapter.EndFrame();adapter.Present();for(int n=0;n<3;++n){adapter.BeginFrame();adapter.Clear(.08f,.1f,.13f,1);adapter.EndFrame();adapter.Present();}
    adapter.Shutdown();
    std::cout<<"DX12 identical GPU pixels, real 501 -> 1 draws, tint/rotation/scale, slot reuse, resize, growth and retirement passed\n";
}
#endif
int main(int argc,char** argv){try{
#if defined(ENABLE_DX12)
    if(argc>1&&std::strcmp(argv[1],"--dx12")==0){Dx12Pixels();return 0;}
#endif
    SharedPath();return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
