#define NOMINMAX
#include "platform/GlfwWindow.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include "resources/ResourceManager.h"
#include "ecs/CameraState.h"
#include "scene/SceneSerializer.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
constexpr unsigned Width=320, Height=240;
constexpr std::array<float,10> Inputs{0,.18f,.5f,1,2,4,8,16,32,64};
void Check(bool ok,const char* message) { if(!ok)throw std::runtime_error(message); }
float Encode(float v) {
    v=std::clamp(v,0.f,1.f);
    return v<=.0031308f ? v*12.92f : 1.055f*std::pow(v,1.f/2.4f)-.055f;
}
void Save(const std::filesystem::path& path,const std::vector<std::uint8_t>& pixels) {
    std::ofstream file(path,std::ios::binary);
    file<<"P6\n"<<Width<<' '<<Height<<"\n255\n";
    for(std::size_t i=0;i<pixels.size();i+=4)file.write(reinterpret_cast<const char*>(&pixels[i]),3);
    Check(file.good(),"capture write failed");
}
// Inject known linear radiance at the material/output boundary, without PBR,
// environment sampling, or the legacy custom-shader HDR conversion wrapper.
constexpr const char* Gradient=R"(
// HDR_SCENE_V1
struct VI { float3 p:POSITION; float3 n:NORMAL; float2 uv:TEXCOORD; };
struct VO { float4 p:SV_Position; };
VO VSMain(VI i) { VO o; o.p=float4(i.p,1); return o; }
float Value(float x) {
    float values[10]={0,.18,.5,1,2,4,8,16,32,64};
    return values[min((uint)(x/32),9)];
}
float Encode(float x) { x=saturate(x); return x<=.0031308?12.92*x:1.055*pow(x,1/2.4)-.055; }
float4 PSMain(VO i):SV_Target { return float4(Encode(Value(i.p.x)).xxx,1); }
float4 PSMainHdr(VO i):SV_Target { return float4(Value(i.p.x).xxx,1); }
)";
}

int main(int argc,char** argv) {
    try {
        const std::filesystem::path output=argc>1?std::filesystem::u8path(argv[1]):"hdr-output-readback";
        std::filesystem::create_directories(output);
        using Json=nlohmann::json;
        Check(!SceneSettingsFromJson(Json::object()).environment.toneMapping,"legacy absent settings must retain SDR");
        Check(!SceneSettingsFromJson({{"environment",{{"enabled",true},{"toneMapping",false}}}}).environment.toneMapping,"explicit tone OFF must win");
        Check(SceneSettingsFromJson({{"environment",{{"enabled",false},{"toneMapping",true}}}}).environment.toneMapping,"explicit tone ON must win without IBL");
        Check(SceneSettingsFromJson({{"environment",{{"enabled",true}}}}).environment.toneMapping,"legacy IBL defaults retain tone mapping");
        Check(SceneSettingsFromJson({{"background",{{"mode",int(EditorBackground::Cubemap)}}}}).environment.toneMapping,"legacy cubemap defaults retain tone mapping");
        ecs::World legacyWorld;SceneSettings legacySettings;std::string sceneError;
        Check(SceneSerializer::RestoreWorld({{"version",4},{"entities",Json::array()}},legacyWorld,legacySettings,&sceneError),"v4 legacy scene restore");
        Check(!legacySettings.environment.toneMapping,"v4 missing settings must retain SDR");
        const auto legacyPath=output/"legacy-v3.json";
        Check(SceneSerializer::WriteAtomic(legacyPath,{{"version",3},{"entities",Json::array()}},&sceneError),"legacy fixture save");
        Check(SceneSerializer::LoadWorld(legacyPath,legacyWorld,legacySettings,&sceneError),"v3 legacy scene load");
        Check(!legacySettings.environment.toneMapping,"v3 migration must retain SDR");
        for(bool tone:{false,true}) {
            legacySettings.environment.toneMapping=tone;
            const auto scene=SceneSerializer::CaptureWorld(legacyWorld,legacySettings);
            Check(SceneSerializer::RestoreWorld(scene,legacyWorld,legacySettings,&sceneError),"output settings scene roundtrip");
            Check(legacySettings.environment.toneMapping==tone,"scene roundtrip changed explicit tone mapping");
        }
        GlfwWindow window;
        Check(window.Create(Width,Height,"HDR output regression"),"window");
        glfwHideWindow(window.GetGlfwHandle());
        Dx12RenderAdapter gpu;
        Check(gpu.Initialize(&window),"DX12 initialize");
        ResourceManager resources(nullptr);
        auto environment=resources.Load<EnvironmentResource>("environments/uniform.hdr");
        Check(environment->IsLoaded()&&!environment->GetData().placeholder,"environment load");
        Check(gpu.UploadEnvironment(environment->GetData(),1),"environment upload");
        ShaderResource source;
        source.name="HDR output control";source.language="hlsl";
        source.vertexSource=source.fragmentSource=Gradient;
        source.vertexPath=source.fragmentPath="hdr-output-control.hlsl";
        const auto shader=gpu.CreateShaderProgram(source);
        Check(shader.IsValid(),"control shader compile");
        MeshData quad;quad.vertices.resize(4);quad.indices={0,1,2,0,2,3};
        constexpr float xy[4][2]={{-1,1},{1,1},{1,-1},{-1,-1}};
        for(unsigned i=0;i<4;++i) {
            quad.vertices[i].position[0]=xy[i][0];quad.vertices[i].position[1]=xy[i][1];
            quad.vertices[i].position[2]=.5f;
        }
        const auto mesh=gpu.UploadMesh(quad);
        TextureData white;white.pixels={255,255,255,255};
        const auto texture=gpu.CreateTexture2D(white);
        Check(mesh.IsValid()&&texture.IsValid(),"control resources");
        std::ofstream csv(output/"gradient.csv");
        csv<<"ibl,sky,fxaa,fog,exposure,tone,input,hdr_target,expected_sdr,actual_sdr,error_lsb,hdr_readback\n";
        const auto camera=ecs::BuildCameraState({},ecs::CameraComponent{},float(Width)/Height);
        FogFrame fog;fog.camera.view=camera.view;fog.camera.projection=camera.projection;
        // Execute a real fog pass while keeping the controlled input unchanged.
        fog.settings.density=0;fog.settings.mode=FogMode::Distance;
        fog.settings.startDistance=10;fog.settings.endDistance=100;
        std::array<std::array<int,10>,4> reference;
        for(auto& row:reference)row.fill(-1);
        unsigned samples=0,maxError=0;
        for(int ibl=0;ibl<2;++ibl)for(int sky=0;sky<2;++sky)
        for(int aa=0;aa<2;++aa)for(int fogOn=0;fogOn<2;++fogOn)
        for(int tone=0;tone<2;++tone)for(int ev=0;ev<2;++ev) {
            EnvironmentSettings settings;settings.enabled=ibl;settings.exposure=float(ev);settings.toneMapping=tone;
            gpu.SetEnvironmentSettings(settings);
            EditorVisualFrame visual;visual.active=sky;visual.backgroundEnabled=true;
            visual.showGrid=false;visual.settings.background=EditorBackground::Cubemap;
            gpu.SetEditorVisuals(visual);
            gpu.SetAntiAliasing(aa?AntiAliasingMode::FxaaHigh:AntiAliasingMode::Off);
            fog.active=fogOn;fog.activeCount=fogOn;gpu.SetFog(fog);gpu.SetCameraPosition({0,0,0,0});
            const bool hdrTarget=ibl||sky||aa||fogOn||tone||ev;
            for(unsigned frame=0;frame<3;++frame) {
                window.PollEvents();gpu.BeginFrame();gpu.Clear(0,0,0,1);gpu.BeginMainPass();
                gpu.SetMaterialState({true,false});gpu.BindShader(shader);gpu.BindTexture(0,texture);
                gpu.DrawMesh(mesh);gpu.EndMainPass();gpu.EndFrame();
                if(frame==2) {
                    const auto pixels=gpu.ReadBackFramePixels();
                    Check(pixels.size()==Width*Height*4,"SDR dimensions");
                    std::vector<float> hdr;if(hdrTarget)hdr=gpu.ReadBackHdrPixels();
                    if(hdrTarget)Check(hdr.size()==pixels.size(),"HDR dimensions");
                    if(fogOn)Check(gpu.GetFogDiagnostics().active&&gpu.GetFogDiagnostics().error.empty(),"fog pass");
                    const auto name=std::to_string(ibl)+std::to_string(sky)+std::to_string(aa)+
                        std::to_string(fogOn)+std::to_string(tone)+std::to_string(ev);
                    Save(output/("gradient-"+name+".ppm"),pixels);
                    for(unsigned i=0;i<Inputs.size();++i) {
                        const unsigned at=(Height/2*Width+i*32+16)*4;
                        float value=Inputs[i]*std::exp2(float(ev));
                        if(tone)value=value/(1+value);
                        const int expected=int(std::lround(255*Encode(value)));
                        const int actual=pixels[at],error=std::abs(expected-actual);
                        maxError=std::max(maxError,unsigned(error));
                        csv<<ibl<<','<<sky<<','<<aa<<','<<fogOn<<','<<ev<<','<<tone<<','<<Inputs[i]<<','
                           <<hdrTarget<<','<<expected<<','<<actual<<','<<error<<','<<(hdr.empty()?-1:hdr[at])<<'\n';
                        Check(error<=1,"SDR output oracle exceeded 1 LSB");
                        for(unsigned channel=0;channel<3;++channel) {
                            Check(std::abs(int(pixels[at+channel])-expected)<=1,"SDR RGB oracle");
                            if(hdrTarget)Check(std::abs(hdr[at+channel]-Inputs[i])<=1e-6f,"linear HDR changed before output");
                        }
                        auto& previous=reference[tone*2+ev][i];
                        if(previous>=0)Check(std::abs(previous-actual)<=1,"output depends on IBL/sky/FXAA/fog");
                        else previous=actual;
                        ++samples;
                    }
                }
                gpu.Present();
            }
        }
        Check(csv.good(),"CSV write failed");
        gpu.DestroyMesh(mesh);gpu.DestroyTexture(texture);gpu.DestroyShader(shader);
        Check(gpu.GetValidationErrorCount()==0,"DX12 validation errors");
        gpu.Shutdown();
        std::cout<<"HDR output: "<<samples<<" samples, max_error_lsb="<<maxError
                 <<", linear_hdr=preserved, invariant=PASS, validation=0\n";
        return 0;
    }catch(const std::exception& error) {std::cerr<<"HDR output FAILED: "<<error.what()<<'\n';return 1;}
}
