#define NOMINMAX
#include "render/Shadows.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include "platform/GlfwWindow.h"
#include "resources/ResourceManager.h"
#include "ecs/RenderInterpolation.h"
#include <GLFW/glfw3.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <stdexcept>
using namespace ecs;
namespace {
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
ShadowMatrix Model(Vec3 p,Vec3 scale,Vec3 rotation={}){
    TransformComponent t;t.position=p;t.scale=scale;t.rotation=rotation;
    ShadowMatrix m;BuildRenderModelMatrix(m.data(),RenderPose::From(t));return m;
}
struct Object {RenderMeshHandle mesh;RenderInstanceData data;ShadowCasterBounds bounds;};
struct Fixture {
    unsigned width=640,height=480;GlfwWindow window;Dx12RenderAdapter gpu;
    RenderMeshHandle cube,sphere,plane;RenderShaderHandle shader;RenderTextureHandle white;
    std::filesystem::path output;std::vector<Object> objects;ShadowMatrix vp;
    LightingSettings lighting;ShadowSettings settings;ShadowFrame lastFrame;
    double prepareMs=0;std::ofstream timings;
    Fixture(std::filesystem::path dest):output(dest){
        std::filesystem::create_directories(output);Check(window.Create(width,height,"Shadow depth range"),"window");glfwHideWindow(window.GetGlfwHandle());Start();
        timings.open(output/"timings.csv");timings<<"case,cpu_prepare_ms,gpu_shadow_ms,gpu_frame_ms,draws,resolution,map_payload_bytes,range\n";
    }
    void Start(){
        Check(gpu.Initialize(&window),"DX12 initialize");ResourceManager resources(nullptr);
        auto source=resources.Load<ShaderResource>("dx12/textured.hlsl");Check(source->IsLoaded()&&!source->GetData().placeholder,"real shader");shader=gpu.CreateShaderProgram(source->GetData());
        auto load=[&](const char* name){auto asset=resources.Load<MeshResource>(name);Check(asset->IsLoaded()&&!asset->GetData().placeholder,"real mesh");return gpu.UploadMesh(asset->GetData().meshData);};
        cube=load("models/validation_cube.obj");sphere=load("models/validation_sphere.obj");
        MeshData data;for(auto p:{Vec3{-9,0,-7},Vec3{-9,0,7},Vec3{9,0,7},Vec3{9,0,-7}}){MeshVertex v;v.position[0]=p.x;v.position[1]=p.y;v.position[2]=p.z;v.normal[1]=1;data.vertices.push_back(v);}data.indices={0,1,2,0,2,3};plane=gpu.UploadMesh(data);
        TextureData pixel;pixel.width=pixel.height=1;pixel.pixels={255,255,255,255};white=gpu.CreateTexture2D(pixel);
    }
    void Stop(){Check(gpu.GetValidationErrorCount()==0,"DX12 validation errors");gpu.DestroyMesh(cube);gpu.DestroyMesh(sphere);gpu.DestroyMesh(plane);gpu.DestroyTexture(white);gpu.DestroyShader(shader);gpu.Shutdown();}
    void Camera(ShadowVector eye,ShadowVector target,bool perspective=false){
        auto forward=ShadowUnit({target[0]-eye[0],target[1]-eye[1],target[2]-eye[2]});auto right=ShadowUnit(ShadowCross({0,0,1},forward)),up=ShadowCross(forward,right);
        ShadowMatrix view{right[0],up[0],forward[0],0,right[1],up[1],forward[1],0,right[2],up[2],forward[2],0,-ShadowDot(right,eye),-ShadowDot(up,eye),-ShadowDot(forward,eye),1};
        ShadowMatrix projection{1.f/9,0,0,0,0,1.f/6.75f,0,0,0,0,1.f/100,0,0,0,0,1};
        if(perspective){float y=1/std::tan(.5235987756f);projection={y*float(height)/width,0,0,0,0,y,0,0,0,0,100.f/99.9f,1,0,0,-10.f/99.9f,0};}
        vp=ShadowMultiply(projection,view);gpu.SetCameraPosition({eye[0],eye[1],eye[2],1});
    }
    void Add(RenderMeshHandle mesh,Vec3 p,Vec3 size,std::array<float,4> color,Vec3 rotation={}){
        Object object;object.mesh=mesh;object.data.model=Model(p,size,rotation);object.data.normal=BuildNormalMatrix(object.data.model);object.data.tint=color;
        object.bounds={{INFINITY,INFINITY,INFINITY},{-INFINITY,-INFINITY,-INFINITY}};
        // Cube/sphere unit mesh AABB; plane is not an upstream caster.
        for(int corner=0;corner<8;++corner)for(int row=0;row<3;++row){float v=object.data.model[12+row];for(int k=0;k<3;++k)v+=object.data.model[k*4+row]*(corner&(1<<k)?.5f:-.5f);object.bounds.minimum[row]=std::min(object.bounds.minimum[row],v);object.bounds.maximum[row]=std::max(object.bounds.maximum[row],v);}
        objects.push_back(object);
    }
    void Scene(bool remote,float gap=0){
        objects.clear();Add(plane,{0,0,0},{1,1,1},{.2f,.45f,.7f,1});
        Add(cube,{0,.5f+gap,0},{1,1,1},{.7f,.4f,.15f,1});
        if(remote){const auto direction=ShadowUnit(lighting.directional.direction),right=ShadowUnit(ShadowCross({0,1,0},direction));Vec3 p{-direction[0]*3000+right[0]*7,-direction[1]*3000+right[1]*7,-direction[2]*3000+right[2]*7};Add(cube,p,{1,1,1},{.5f,.5f,.5f,1});}
    }
    std::vector<std::uint8_t> Frame(const std::string& name,bool capture){
        window.PollEvents();gpu.BeginFrame();gpu.Clear(.025f,.03f,.04f,1);auto light=BuildLightingConstants(lighting);gpu.SetLighting(light);
        auto start=std::chrono::steady_clock::now();lastFrame=BuildDirectionalShadowFrame(light,settings,{});std::vector<ShadowCasterBounds> bounds;for(size_t i=1;i<objects.size();++i)bounds.push_back(objects[i].bounds);ExtendDirectionalCasterDepth(lastFrame,bounds);prepareMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        if(gpu.BeginShadowPass(lastFrame)){for(auto& o:objects){gpu.SetMaterialState({true,true});gpu.DrawShadowMesh(o.mesh,o.data);}gpu.EndShadowPass();}
        gpu.BeginMainPass();gpu.BindShader(shader);gpu.BindTexture(0,white);
        for(auto& o:objects){o.data.mvp=ShadowMultiply(vp,o.data.model);gpu.SetMaterialState({true,true});gpu.SetModelMatrix(o.data.model);gpu.SetNormalMatrix(o.data.normal);gpu.SetTestTransform(o.data.mvp.data());gpu.SetTestColor(o.data.tint[0],o.data.tint[1],o.data.tint[2],1);gpu.DrawMesh(o.mesh);}
        gpu.EndMainPass();gpu.EndFrame();std::vector<std::uint8_t> image;if(capture){image=gpu.ReadBackFramePixels();Check(image.size()==size_t(width)*height*4,"GPU color size");std::ofstream f(output/(name+".ppm"),std::ios::binary);f<<"P6\n"<<width<<' '<<height<<"\n255\n";for(size_t i=0;i<image.size();i+=4)f.write(reinterpret_cast<char*>(image.data()+i),3);Check(f.good(),"write image");}
        auto gpuTime=gpu.GetGpuTimings();if(gpuTime.valid&&!capture){auto s=gpu.GetSubmissionStatistics();timings<<name<<','<<prepareMs<<','<<gpuTime.shadowMs<<','<<gpuTime.frameMs<<','<<s.shadowDrawCalls<<','<<s.shadowResolution<<','<<ShadowMapBytes(settings)<<','<<lastFrame.settings.depthRange<<'\n';}
        gpu.Present();return image;
    }
    std::vector<std::uint8_t> Capture(const std::string& name){for(int i=0;i<4;++i)Frame(name,true);return Frame(name,true);}
};
}
int main(int argc,char** argv){try{
    const bool baseline=argc>1&&std::string(argv[1])=="--baseline";Fixture f(argc>2?argv[2]:"shadow-depth-range-readback");
    f.lighting.directional.direction={1,-.15f,.3f};f.lighting.directional.color={1,1,1};f.lighting.directional.intensity=.9f;f.lighting.ambientIntensity=.12f;
    f.settings.coverage=24;f.settings.depthRange=40;f.settings.resolution=1024;f.settings.cameraFitted=true;f.settings.pcf=false;f.settings.depthBias=0;f.settings.slopeBias=0;f.settings.normalBias=0;f.settings.receiverBias=0;f.settings.edgeFade=0;f.settings.debugMode=1;
    f.Camera({0,18,0},{0,0,0});f.Scene(false);auto nearImage=f.Capture("contact-near-range");auto normalFrame=f.lastFrame;
    f.Scene(true);auto remote=f.Capture("contact-extended-range");auto extended=f.lastFrame;
    int lost=0,shadow=0,acne=0;for(unsigned y=35;y<f.height-35;++y)for(unsigned x=35;x<f.width-35;++x){auto at=(y*f.width+x)*4;if(nearImage[at]<30){++shadow;if(remote[at]>200)++lost;}}
    // Left upper floor is lit in both scenarios, outside both projected casters.
    for(unsigned y=35;y<300;++y)for(unsigned x=35;x<200;++x){auto at=(y*f.width+x)*4;Check(nearImage[at]>250,"lit floor ROI contaminated");acne+=remote[at]<250;}
    std::cout<<"contact shadow pixels="<<shadow<<" lost_after_extension="<<lost<<" lit_roi_acne="<<acne<<" range="<<extended.settings.depthRange<<" world_bias_before="<<normalFrame.constants.sampling[1]*normalFrame.settings.depthRange<<" world_bias_after="<<extended.constants.sampling[1]*extended.settings.depthRange<<'\n';
    Check(shadow>100,"shadow oracle empty");if(!baseline){Check(lost<4,"depth extension detaches contact shadow");Check(acne==0,"receiver bias fix introduces acne");}
    if(!baseline){
        Check(std::abs(normalFrame.constants.sampling[1]*normalFrame.settings.depthRange-.0002f)<1e-8f,"roundoff floor world size");
        Check(std::abs(extended.constants.sampling[1]*extended.settings.depthRange-.0002f)<1e-8f,"extension changed roundoff floor world size");
    }
    for(int angle=0;angle<2;++angle)for(float gap:{0.f,.02f,.15f}){
        f.lighting.directional.direction=angle?ShadowVector{.2f,-1,.1f}:ShadowVector{1,-.15f,.3f};f.Scene(true,gap);auto image=f.Capture("gap-"+std::to_string(gap)+"-angle-"+std::to_string(angle));
        int noise=0;for(unsigned y=35;y<300;++y)for(unsigned x=35;x<200;++x)noise+=image[(y*f.width+x)*4]<250;
        std::cout<<"gap="<<gap<<" angle="<<angle<<" lit_roi_acne="<<noise<<'\n';if(!baseline)Check(noise==0,"gap/angle floor acne");
    }
    // Real reference geometry; identical objects/settings for before and after.
    f.settings.debugMode=0;f.Scene(true);f.Add(f.cube,{-3,1,1},{1,2,1},{.65f,.65f,.65f,1});f.Add(f.cube,{3,1.5f,-2},{1,3,1},{.3f,.55f,.4f,1});f.Add(f.sphere,{-2,1,-3},{2,2,2},{.6f,.55f,.45f,1});f.Add(f.cube,{2,.45f,2},{3,.1f,2},{.5f,.5f,.5f,1},{0,0,.35f});f.Add(f.cube,{-4,.6f,-1},{.04f,1.2f,2},{.7f,.6f,.4f,1});
    for(int angle=0;angle<2;++angle){f.lighting.directional.direction=angle?ShadowVector{.2f,-1,.1f}:ShadowVector{1,-.15f,.3f};for(int filter:{0,3,5}){f.settings.pcf=filter!=0;f.settings.filterTaps=filter==3?3:5;f.Camera({9,10,-12},{0,0,0},true);f.Capture("reference-angle-"+std::to_string(angle)+"-filter-"+std::to_string(filter));}}
    for(int i=0;i<9;++i){f.Camera({9+i*.002f,10,-12},{float(i)*.002f,0,0},true);f.Capture("motion-"+std::to_string(i));}
    f.Camera({18,20,-24},{0,0,0},true);f.Capture("reference-far");
    for(int i=0;i<130;++i)f.Frame(i<30?"warmup":"measured",false);
    auto depth=f.gpu.ReadBackShadowDepth();size_t occupied=0;for(float d:depth){Check(std::isfinite(d)&&d>=0&&d<=1,"finite GPU shadow depth");occupied+=d<1;}Check(occupied>100,"GPU shadow map empty");std::ofstream raw(f.output/"depth.f32",std::ios::binary);raw.write(reinterpret_cast<const char*>(depth.data()),depth.size()*sizeof(float));
    glfwSetWindowSize(f.window.GetGlfwHandle(),800,600);f.width=800;f.height=600;f.Capture("resize");f.Stop();f.Start();f.objects.clear();f.Scene(true);f.Camera({0,18,0},{0,0,0});f.Capture("restart");f.Stop();
    std::cout<<"Shadow depth range GPU regression PASS validation=0\n";return 0;
}catch(const std::exception& e){std::cerr<<"Shadow depth range FAILED: "<<e.what()<<'\n';return 1;}}
