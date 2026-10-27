#define NOMINMAX
#include "platform/GlfwWindow.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include "resources/ResourceManager.h"
#include "ecs/CameraState.h"
#include "ui/RuntimeUI.h"
#include <GLFW/glfw3.h>
#include <fstream>
#include <iostream>
#include <cmath>

namespace {
using RGB=std::array<double,3>;
unsigned checks=0;
void Check(bool b,const char* message){if(!b)throw std::runtime_error(message);++checks;}
double Encode(double v){v=std::clamp(v,0.,1.);return v<=.0031308?12.92*v:1.055*std::pow(v,1/2.4)-.055;}
double Decode(double v){return v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);}
struct Layer {float z=2,alpha=.5f;RGB color{1,0,0};bool additive=false,textured=false;};
struct Case {
    const char* name="distance";float opaqueZ=8;bool fog=true,firstPerson=false,hud=false,tone=false;
    float exposure=0;FogMode mode=FogMode::Distance;float density=1,falloff=0;
    RGB fogColor{0,0,0};bool volumeLighting=false;float sunIntensity=0;
    std::vector<Layer> layers{{}};
};
class Fixture {
public:
    GlfwWindow window;Dx12RenderAdapter gpu;ResourceManager resources{nullptr};
    RenderShaderHandle shader;RenderTextureHandle white,texture;RenderMeshHandle mesh;
    std::filesystem::path output;std::ofstream csv;
    Fixture(const std::filesystem::path& path):output(path),csv(path/"samples.csv") {
        Check(window.Create(320,240,"Fog transparency regression"),"window");glfwHideWindow(window.GetGlfwHandle());
        csv<<"case,width,height,particle_z,opaque_z,expected_r,expected_g,expected_b,actual_r,actual_g,actual_b,max_linear_error\n";
    }
    void Start(){
        Check(gpu.Initialize(&window),"DX12 initialize/restart");
        shader=gpu.CreateShaderProgram(resources.Load<ShaderResource>("dx12/textured.hlsl")->GetData());
        TextureData t;t.pixels={255,255,255,255};white=gpu.CreateTexture2D(t);
        t.pixels={128,255,255,128};texture=gpu.CreateTexture2D(t);
        MeshData q;q.vertices.resize(4);q.indices={0,1,2,0,2,3};
        const float xy[4][2]={{-1,1},{1,1},{1,-1},{-1,-1}};
        for(int i=0;i<4;++i){q.vertices[i].position[0]=xy[i][0];q.vertices[i].position[1]=xy[i][1];q.vertices[i].normal[2]=-1;}
        mesh=gpu.UploadMesh(q);Check(shader.IsValid()&&mesh.IsValid()&&white.IsValid()&&texture.IsValid(),"resources");
    }
    void Stop(){
        gpu.DestroyMesh(mesh);gpu.DestroyTexture(white);gpu.DestroyTexture(texture);gpu.DestroyShader(shader);
        Check(gpu.GetValidationErrorCount()==0,"DX12 ERROR/CORRUPTION");gpu.Shutdown();
    }
    // Independent closed-form reference for homogeneous height/volume and a
    // horizontal exponential height profile. Volume lighting uses uniform ambient
    // plus an unshadowed isotropic directional source, so scattering is analytic.
    std::pair<double,RGB> Medium(const Case& c,double z,double originY) {
        double T=1;RGB S{};if(!c.fog)return {T,S};
        if(c.mode==FogMode::Distance)T=1-c.density*std::clamp(z/10.,0.,1.);
        else T=std::exp(-c.density*.2*std::exp(-c.falloff*std::max(originY+(c.falloff>0?1:0),0.))*z);
        for(unsigned k=0;k<3;++k){
            double source=c.fogColor[k];
            if(c.mode==FogMode::Volumetric&&c.volumeLighting)
                source=(k==1?.2:0)+c.sunIntensity/(4*3.141592653589793);
            S[k]=source*(1-T);
        }
        return {T,S};
    }
    void Run(const Case& c){
        std::vector<unsigned char> pixels;std::vector<float> hdr;unsigned width=0,height=0;
        double originY=0;
        for(int frame=0;frame<4;++frame){
            window.PollEvents();gpu.BeginFrame();auto size=gpu.RenderAreaSize();width=size[0];height=size[1];
            ecs::CameraComponent lens;lens.projection=ecs::CameraProjection::Orthographic;lens.orthographicSize=2;lens.nearClip=.1f;lens.farClip=10;
            auto camera=ecs::BuildCameraState({},lens,float(width)/height);
            originY=(1-2*(height/2+.5)/height)/camera.projection[5];
            FogFrame f;f.active=c.fog;f.activeCount=c.fog;f.camera.view=camera.view;f.camera.projection=camera.projection;
            f.settings.mode=c.mode;f.settings.startDistance=0;f.settings.endDistance=10;f.settings.maxDistance=10;
            f.settings.density=c.density;f.settings.heightDensity=f.settings.extinction=.2f;f.settings.heightFalloff=c.falloff;
            f.settings.baseHeight=c.falloff>0?-1.f:0.f;f.settings.anisotropy=0;f.settings.scatteringColor={1,1,1,1};f.settings.sceneLighting=c.volumeLighting;
            for(unsigned k=0;k<3;++k)f.settings.color[k]=float(c.fogColor[k]);gpu.SetFog(f);
            EnvironmentSettings env;env.enabled=false;env.toneMapping=c.tone;env.exposure=c.exposure;gpu.SetEnvironmentSettings(env);
            gpu.SetAntiAliasing(AntiAliasingMode::FxaaHigh);gpu.SetCameraPosition({0,0,0,0});
            LightingSettings light;light.directional.intensity=c.sunIntensity;light.directional.direction={0,0,1};
            light.directional.color={1,1,1};
            light.ambientColor={0,1,0};light.ambientIntensity=.2f;gpu.SetLighting(BuildLightingConstants(light));
            gpu.Clear(0,0,0,1);gpu.BeginMainPass();
            auto depth=[&](float z){return camera.projection[10]*z+camera.projection[14];};
            std::array<float,16> mvp{};mvp[0]=mvp[5]=mvp[10]=mvp[15]=1;mvp[14]=depth(c.opaqueZ);
            gpu.BindShader(shader);gpu.BindTexture(0,white);gpu.SetMaterialState({true,false});gpu.SetTestTransform(mvp.data());
            gpu.SetNormalMatrix({});gpu.SetTestColor(0,0,1,1);gpu.SetBaseColor({1,1,1,1});gpu.DrawMesh(mesh);
            ParticleDrawList draw;
            for(const auto& layer:c.layers){
                unsigned first=unsigned(draw.vertices.size());
                for(auto xy:std::array<std::array<float,2>,6>{{{-.4f,.4f},{.4f,.4f},{.4f,-.4f},{-.4f,.4f},{.4f,-.4f},{-.4f,-.4f}}})
                    draw.vertices.push_back({{xy[0],xy[1],depth(layer.z),1},{.5f,.5f},{float(layer.color[0]),float(layer.color[1]),float(layer.color[2]),layer.alpha}});
                draw.commands.push_back({first,6,layer.textured?texture:white,layer.additive});
            }
            // Also exercise direct-adapter users: DrawParticles must establish the
            // opaque boundary itself if no explicit BeginWorldTransparency was sent.
            if(c.firstPerson)Check(gpu.BeginViewmodelPass(),"FirstPerson pass");
            gpu.DrawParticles(draw);if(c.firstPerson)gpu.EndViewmodelPass();
            gpu.EndMainPass();
            if(c.hud){
                ui::DrawList hud;hud.width=float(width);hud.height=float(height);float x=width*.5f,y=height*.5f;
                for(auto xy:std::array<std::array<float,2>,6>{{{-8,-8},{8,-8},{8,8},{-8,-8},{8,8},{-8,8}}})hud.vertices.push_back({x+xy[0],y+xy[1],.5f,.5f,{1,1,0,1}});
                hud.commands.push_back({0,6,{0,0,float(width),float(height)},white});gpu.DrawRuntimeUI(hud);
            }
            gpu.EndFrame();
            if(frame==3){pixels=gpu.ReadBackFramePixels();hdr=gpu.ReadBackHdrPixels();if(c.fog)Check(gpu.GetFogDiagnostics().active&&gpu.GetFogDiagnostics().error.empty(),"fog pass active");}
            gpu.Present();
        }
        Check(hdr.size()==width*height*4&&pixels.size()==hdr.size(),"readback dimensions");
        auto [T,S]=Medium(c,c.opaqueZ,originY);RGB expected{S[0],S[1],T+S[2]};
        for(const auto& layer:c.layers){
            if(!c.firstPerson&&layer.z>c.opaqueZ)continue;
            auto [trans,scatter]=Medium(c,layer.z,originY);if(c.firstPerson){trans=1;scatter={};}
            double alpha=layer.alpha*(layer.textured?128./255:1);
            for(unsigned k=0;k<3;++k){double color=Decode(layer.color[k])*(layer.textured&&k==0?Decode(128./255):1);
                expected[k]=alpha*(color*trans+(layer.additive?0:scatter[k]))+(layer.additive?1:1-alpha)*expected[k];}
        }
        const unsigned at=(height/2*width+width/2)*4;double maxError=0;
        for(unsigned k=0;k<3;++k){
            maxError=std::max(maxError,std::abs(hdr[at+k]-expected[k]));
            double value=expected[k]*std::exp2(c.exposure);if(c.tone)value=value/(1+value);
            int code=c.hud?(k<2?255:0):int(std::lround(255*Encode(value)));
            Check(std::abs(int(pixels[at+k])-code)<=2,"SDR output/UI oracle");
        }
        csv<<c.name<<','<<width<<','<<height<<','<<(c.layers.empty()?0:c.layers.back().z)<<','<<c.opaqueZ;
        for(double v:expected)csv<<','<<v;for(unsigned k=0;k<3;++k)csv<<','<<hdr[at+k];csv<<','<<maxError<<'\n';csv.flush();
        if(maxError>.005)std::cerr<<c.name<<" error="<<maxError<<" expected red="<<expected[0]<<" actual="<<hdr[at]<<'\n';
        Check(maxError<=.005,"linear particle fog oracle exceeded .005");
        std::ofstream image(output/(std::string(c.name)+".ppm"),std::ios::binary);image<<"P6\n"<<width<<' '<<height<<"\n255\n";
        for(size_t i=0;i<pixels.size();i+=4)image.write(reinterpret_cast<const char*>(&pixels[i]),3);Check(image.good(),"image save");
    }
};
}
int main(int argc,char** argv){try{
    std::filesystem::path output=argc>1?std::filesystem::u8path(argv[1]):"fog-transparency-readback";std::filesystem::create_directories(output);
    Fixture f(output);f.Start();Case c;
    c.name="Z2-Z8";f.Run(c);c.name="Z6-Z8";c.layers[0].z=6;f.Run(c);
    c.name="Z2-Z4";c.opaqueZ=4;c.layers[0].z=2;f.Run(c);c.name="Z6-Z4-occluded";c.layers[0].z=6;f.Run(c);
    c=Case{};c.name="fog-off";c.fog=false;f.Run(c);
    c=Case{};c.name="alpha-zero";c.layers[0].alpha=0;f.Run(c);c.name="alpha-one";c.layers[0].alpha=1;f.Run(c);
    c=Case{};c.name="additive-colored-medium";c.layers[0].additive=true;c.fogColor={0,.3,0};f.Run(c);
    c=Case{};c.name="two-alpha-layers";c.layers={{6,.5f,{0,1,0}},{2,.5f,{1,0,0}}};f.Run(c);
    c.name="two-alpha-layers-colored-medium";c.fogColor={0,.3,0};f.Run(c);
    c=Case{};c.name="texture-rgb-alpha";c.layers[0].textured=true;f.Run(c);
    c=Case{};c.mode=FogMode::Height;c.name="height";f.Run(c);c.falloff=.5f;c.name="height-profile";f.Run(c);
    c=Case{};c.mode=FogMode::Volumetric;c.name="volume-absorption";f.Run(c);
    c.fogColor={0,.4,0};c.name="volume-scattering";f.Run(c);c.layers[0].additive=true;c.name="volume-additive";f.Run(c);
    c.layers[0].additive=false;c.falloff=.5f;c.name="volume-height-profile";f.Run(c);c.falloff=0;
    c.layers[0].additive=false;c.volumeLighting=true;c.name="volume-ambient";f.Run(c);c.sunIntensity=2;c.name="volume-directional";f.Run(c);
    c=Case{};c.firstPerson=true;c.layers[0].z=6;c.opaqueZ=4;c.name="first-person-no-world-fog";f.Run(c);
    c=Case{};c.tone=true;c.exposure=1;c.name="hdr-tonemapping";f.Run(c);c.hud=true;c.name="hud-after-output";f.Run(c);
    glfwSetWindowSize(f.window.GetGlfwHandle(),400,280);c.name="resize";f.Run(c);
    f.Stop();f.Start();c=Case{};c.name="device-restart";f.Run(c);f.Stop();
    std::cout<<"Fog transparency: "<<checks<<" checks passed; validation=0\n";return 0;
}catch(const std::exception& e){std::cerr<<"Fog transparency FAILED: "<<e.what()<<'\n';return 1;}}
