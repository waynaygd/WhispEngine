#include "EditorTestPreferences.h"
#define NOMINMAX
#include "core/Application.h"
#include "core/AssetPaths.h"
#include "editor/EditorPreferences.h"
#include "render/ColorPipeline.h"
#include "resources/ResourceManager.h"
#include "scene/SceneSerializer.h"
#include <fstream>
#include <iostream>
#include <cmath>
#include <chrono>
#if defined(ENABLE_DX12)
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include "platform/GlfwWindow.h"
#include <GLFW/glfw3.h>
#endif
static void Check(bool result,const char* text){if(!result)throw std::runtime_error(text);}
static void Cpu(){
    EditorVisualFrame frame;frame.active=true;Check(EditorGridVisible(frame),"Edit grid default");frame.play=true;Check(!EditorGridVisible(frame),"Play and Pause suppress grid");frame.play=false;frame.showGrid=false;Check(!EditorGridVisible(frame),"show grid setting");frame.showGrid=true;frame.settings.enabled=false;Check(!EditorGridVisible(frame),"all editor visuals disabled");
    EditorVisualSettings settings;Check(ValidEditorVisualSettings(settings),"valid defaults");settings.minorSpacing=0;Check(!ValidEditorVisualSettings(settings),"zero spacing");settings={};settings.majorSpacing=.1f;Check(!ValidEditorVisualSettings(settings),"major smaller than minor");settings={};settings.fadeDistance=NAN;Check(!ValidEditorVisualSettings(settings),"NaN fade");
    editor::EditorPreferences p;p.grid=false;p.visualPanel=true;p.visuals.background=EditorBackground::Cubemap;p.visuals.skyRotation=73;p.visuals.fadeDistance=800;p.visuals.cubemap="environments/studio.hdr";
    auto document=editor::PreferencesToJson(p);Check(editor::PreferencesFromJson(document)==p,"visual preference roundtrip");auto legacy=document;legacy.erase("visuals");Check(editor::PreferencesFromJson(legacy).visuals==EditorVisualSettings{},"Editor 2.0 preferences migration");
    document["visuals"]["cubemap"]="../escape.hdr";bool rejected=false;try{editor::PreferencesFromJson(document);}catch(const std::exception&){rejected=true;}Check(rejected,"unsafe cubemap key rejection");
    Application app;auto e=app.GetEditorLayer().CreateEntity(app);app.GetEditorLayer().MarkSaved();auto scene=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());app.GetEditorLayer().GetVisualSettings().skyIntensity=2;Check(!app.GetEditorLayer().IsSceneDirty()&&scene==SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings()),"visual preferences separate from scene and Undo domain");
    app.SetEditorPlayMode(true);Check(!EditorGridVisible(app.GetEditorLayer().BuildVisualFrame(app)),"Application Play routing");app.SetEditorPaused(true);Check(!EditorGridVisible(app.GetEditorLayer().BuildVisualFrame(app)),"Application Pause routing");app.SetEditorPlayMode(false);Check(app.GetWorld().IsAlive(e),"Runtime isolation retained");app.Shutdown();std::cout<<"Editor visual settings / migration / Edit-Play policy / scene separation passed\n";
}
#if defined(ENABLE_DX12)
static std::array<float,16> Multiply(const std::array<float,16>& a,const std::array<float,16>& b){std::array<float,16> r{};for(int j=0;j<4;++j)for(int i=0;i<4;++i)for(int k=0;k<4;++k)r[j*4+i]+=a[k*4+i]*b[j*4+k];return r;}
static std::array<float,16> ViewProjection(const EditorVisualFrame& f){
    auto dot=[&](const auto& v){return v[0]*f.camera[0]+v[1]*f.camera[1]+v[2]*f.camera[2];};
    std::array<float,16> view{f.right[0],f.up[0],f.forward[0],0,f.right[1],f.up[1],f.forward[1],0,f.right[2],f.up[2],f.forward[2],0,-dot(f.right),-dot(f.up),-dot(f.forward),1};
    std::array<float,16> projection{1/(f.tanHalfFov*f.aspect),0,0,0,0,1/f.tanHalfFov,0,0,0,0,f.farPlane/(f.farPlane-f.nearPlane),1,0,0,-f.farPlane*f.nearPlane/(f.farPlane-f.nearPlane),0};return Multiply(projection,view);
}
static void Gpu(bool brokenShader){
    GlfwWindow window;Check(window.Create(320,240,"Editor Visual Polish regression"),"window");glfwHideWindow(window.GetGlfwHandle());
    Dx12RenderAdapter a;Check(a.Initialize(&window),"DX12 initialize");ResourceManager resources(nullptr);
    auto shader=a.CreateShaderProgram(resources.Load<ShaderResource>("dx12/textured_unlit.hlsl")->GetData());Check(shader.IsValid(),"retained scene unlit shader");
    TextureData white;white.pixels={255,255,255,255};auto texture=a.CreateTexture2D(white);MeshData quad;quad.vertices.resize(4);quad.indices={0,1,2,0,2,3};float xz[4][2]={{-1,-1},{-1,1},{1,1},{1,-1}};for(int i=0;i<4;++i){quad.vertices[i].position[0]=xz[i][0];quad.vertices[i].position[1]=1;quad.vertices[i].position[2]=xz[i][1];quad.vertices[i].normal[1]=1;}auto mesh=a.UploadMesh(quad);
    EditorVisualFrame f;f.active=true;f.camera={0,5,0};f.forward={0,-1,0};f.up={0,0,1};f.right={1,0,0};f.aspect=320.f/240;f.tanHalfFov=1;f.nearPlane=.1f;f.farPlane=1000;f.settings.background=EditorBackground::Solid;f.settings.gridOpacity=.8f;f.settings.fadeDistance=200;
    std::vector<float> lastLinear;
    auto draw=[&](bool geometry,bool instance=false,float height=1){
        a.BeginFrame();a.SetEditorVisuals(f);a.Clear(.02f,.03f,.04f,1);float clear[4]={.02f,.03f,.04f,1};Check(a.BeginViewportRender(320,240,clear),"viewport");a.BeginMainPass();
        if(geometry){RenderInstanceData data;data.model={1,0,0,0,0,1,0,0,0,0,1,0,0,height-1,0,1};data.mvp=Multiply(ViewProjection(f),data.model);data.tint={.75f,.07f,.03f,1};a.BindShader(shader);a.BindTexture(0,texture);a.SetMeshRange({});a.SetMaterialState({true,false});
            if(instance)Check(a.DrawMeshInstanced(mesh,std::span(&data,1)),"instanced geometry");else{a.SetTestTransform(data.mvp.data());a.SetTestColor(.75f,.07f,.03f,1);a.DrawMesh(mesh);}}
        a.EndMainPass();a.EndViewportRender();a.EndFrame();auto pixels=a.ReadBackViewportPixels();lastLinear=a.ReadBackHdrPixels();Check(pixels.size()==320*240*4,"viewport readback dimensions");a.Present();return pixels;
    };
    if(brokenShader){
        struct HiddenShader {std::filesystem::path path,backup;HiddenShader(){path=AssetPaths::ResolveShaderPath("dx12/editor_visuals.hlsl");backup=path;backup+=".regression-backup";Check(!std::filesystem::exists(backup),"stale shader backup");std::filesystem::rename(path,backup);}~HiddenShader(){std::error_code error;std::filesystem::rename(backup,path,error);}};
        std::vector<std::uint8_t> image;{HiddenShader hidden;image=draw(true);}Check(!a.GetEditorVisualDiagnostics().error.empty()&&a.GetSubmissionStatistics().drawCalls==1,"visual shader fallback retains scene draw");f.active=false;Check(image==draw(true),"fallback scene pixels equal disabled visuals");Check(a.GetValidationErrorCount()==0,"fallback debug errors");a.DestroyMesh(mesh);a.DestroyShader(shader);a.DestroyTexture(texture);a.Shutdown();return;
    }
    f.showGrid=false;auto empty=draw(false),object=draw(true);Check(a.GetEditorVisualDiagnostics().backgroundDraws==1,"solid background draw");
    f.showGrid=true;auto grid=draw(false),occluded=draw(true);Check(a.GetEditorVisualDiagnostics().gridDraws==1,"Edit grid GPU pass");unsigned mask=0,visibleGrid=0;for(std::size_t i=0;i<object.size();i+=4){if(object[i]!=empty[i]||object[i+1]!=empty[i+1]||object[i+2]!=empty[i+2]){++mask;for(int c=0;c<4;++c)Check(occluded[i+c]==object[i+c],"grid must be occluded by geometry above Y=0");}else if(grid[i]!=empty[i]||grid[i+1]!=empty[i+1])++visibleGrid;}
    Check(mask>1000&&visibleGrid>500,"nontrivial depth/line samples");Check(grid!=empty,"visible infinite grid pixels");
    f.showGrid=false;auto below=draw(true,false,-1);f.showGrid=true;auto above=draw(true,false,-1);Check(below!=above,"grid in front of geometry below plane");
    Check(occluded==draw(true,true),"grid depth / ordinary vs instanced equivalence");
    f.play=true;auto play=draw(true);Check(a.GetEditorVisualDiagnostics().gridDraws==0&&a.GetEditorVisualDiagnostics().backgroundDraws==1,"Play retains scene background and suppresses grid");f.showGrid=false;Check(play==draw(true),"Play image unchanged by show-grid flag");f.play=false;
    f.settings.background=EditorBackground::Gradient;auto gradient=draw(false);Check(gradient!=empty,"gradient background pixels");
    f.forward={0,0,1};f.up={0,1,0};f.camera={0,3,0};auto sky=draw(false);std::size_t top=(10*320+160)*4,bottom=(230*320+160)*4;Check(sky[top]!=sky[bottom]||sky[top+1]!=sky[bottom+1],"top and bottom sky gradient differ");
    {float x=(2*160.5f/320-1)*f.aspect,y=1-2*10.5f/240;float directionY=y/std::sqrt(x*x+y*y+1),blend=std::pow(directionY,f.settings.gradientBlend);for(int c=0;c<3;++c){float expected=f.settings.horizon[c]+(f.settings.top[c]-f.settings.horizon[c])*blend;Check(std::abs(int(sky[top+c])-int(std::lround(255*LinearToSrgb(expected))))<=2,"procedural gradient numerical color oracle");}}
    f.showGrid=true;auto level=draw(false);Check(a.GetEditorVisualDiagnostics().gridDraws==1&&level.size()==sky.size(),"horizon rays stable");f.camera={0,-5,0};f.forward={0,1,0};f.up={0,0,1};f.right={-1,0,0};Check(draw(false)!=empty,"below-plane camera stable");
    EnvironmentSettings outputSettings;outputSettings.toneMapping=false;a.SetEnvironmentSettings(outputSettings); // Numeric SDR oracle without Reinhard; HDR sky activation is independent of IBL.
    f.showGrid=false;f.camera={0,0,0};f.forward={0,0,1};f.up={0,1,0};f.right={1,0,0};f.settings.background=EditorBackground::Cubemap;f.settings.cubemap="environments/visual-regression.hdr";
    EnvironmentResource cube;cube.sourcePath=f.settings.cubemap;cube.placeholder=false;cube.environment.resolution=4;cube.environment.mipCount=1;std::array<std::array<float,3>,6> colors{{{.8f,.1f,.05f},{.1f,.7f,.05f},{.05f,.1f,.8f},{.3f,.45f,.7f},{.2f,.4f,.6f},{.6f,.4f,.2f}}};for(auto color:colors){std::vector<float> face;for(int i=0;i<16;++i)face.insert(face.end(),{color[0],color[1],color[2],1});cube.environment.subresources.push_back(std::move(face));}
    a.BeginFrame();a.Clear(.02f,.03f,.04f,1);Check(a.UploadEditorSky(cube,1),"GPU cubemap upload");a.EndFrame();a.Present();
    auto cubeImage=draw(false);Check(a.GetEditorVisualDiagnostics().skyReady,"cubemap selected");auto colorCheck=[&](const auto& image,int face){std::size_t center=(120*320+160)*4;for(int c=0;c<3;++c)Check(std::abs(int(image[center+c])-int(std::lround(255*LinearToSrgb(colors[face][c]))))<=2,"GPU cubemap face/color oracle");};colorCheck(cubeImage,4);
    f.camera={10000,6000,-2000};Check(cubeImage==draw(false),"sky translation independence");f.settings.skyRotation=90;auto rotated=draw(false);colorCheck(rotated,0);Check(rotated!=cubeImage,"cubemap rotation");
    EnvironmentSettings environment;environment.enabled=true;environment.toneMapping=false;a.SetEnvironmentSettings(environment);draw(false);Check(!lastLinear.empty(),"HDR visual target readback");std::size_t center=(120*320+160)*4;for(int c=0;c<3;++c)if(std::abs(lastLinear[center+c]-colors[0][c])>=.0001f){std::cerr<<"HDR channel "<<c<<" actual "<<lastLinear[center+c]<<" expected "<<colors[0][c]<<"\n";Check(false,"linear HDR cubemap color preserved");};environment.enabled=false;a.SetEnvironmentSettings(environment);
    {
        EnvironmentResource source=cube;source.sky={256,1,{}};
        for(int face=0;face<6;++face){std::vector<float> pixels(256*256*4);for(std::size_t i=0;i<pixels.size();i+=4){pixels[i]=.125f;pixels[i+1]=.25f;pixels[i+2]=.5f;pixels[i+3]=1;}source.sky.subresources.push_back(std::move(pixels));}
        const auto start=std::chrono::steady_clock::now();a.BeginFrame();a.Clear(0,0,0,1);Check(a.UploadEditorSky(source,99),"high resolution source-radiance cube upload");a.EndFrame();a.Present();
        const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        environment.enabled=true;a.SetEnvironmentSettings(environment);draw(false);const float expected[]={.125f,.25f,.5f};for(int c=0;c<3;++c)Check(std::abs(lastLinear[center+c]-expected[c])<.0001f,"visible sky uses high resolution source, not low-resolution IBL cube");
        std::cout<<"Source sky 256: raw GPU payload "<<6*256*256*16<<" bytes; upload submit wall "<<ms<<" ms (includes Present, not isolated GPU timing)\n";
        a.BeginFrame();a.Clear(0,0,0,1);Check(a.UploadEditorSky(cube,100),"source cube replacement retirement");a.EndFrame();a.Present();environment.enabled=false;a.SetEnvironmentSettings(environment);
    }
    f.settings.cubemap="environments/not-loaded.hdr";auto missing=draw(false);Check(!a.GetEditorVisualDiagnostics().skyReady&&missing!=cubeImage,"unavailable cubemap gradient fallback");
    for(unsigned version=2;version<12;++version){a.BeginFrame();a.Clear(.02f,.03f,.04f,1);cube.environment.subresources[0][0]=float(version)*.01f;Check(a.UploadEditorSky(cube,version),"cubemap replacement while frames in flight");a.EndFrame();a.Present();}f.settings.cubemap=cube.sourcePath;draw(false);
    f.play=true;f.showGrid=true;a.BeginFrame();a.Clear(.02f,.03f,.04f,1);a.SetEditorVisuals(f);a.BeginMainPass();a.EndMainPass();a.EndFrame();auto runtimeWindow=a.ReadBackFramePixels();Check(runtimeWindow.size()==320*240*4,"full-window sky readback");colorCheck(runtimeWindow,0);Check(a.GetEditorVisualDiagnostics().backgroundDraws==1&&a.GetEditorVisualDiagnostics().gridDraws==0,"full-window Runtime sky without editor helpers");a.Present();f.play=false;f.showGrid=false;
    a.BeginFrame();a.Clear(.02f,.03f,.04f,1);a.SetEditorVisuals(f);float clear[4]={};Check(a.BeginViewportRender(480,270,clear),"resize");a.BeginMainPass();a.EndMainPass();a.EndViewportRender();a.EndFrame();Check(a.ReadBackViewportPixels().size()==480*270*4,"resize readback");a.Present();
    Check(a.GetValidationErrorCount()==0,"DX12 ERROR/CORRUPTION");a.DestroyMesh(mesh);a.DestroyShader(shader);a.DestroyTexture(texture);a.Shutdown();std::cout<<"Grid depth occlusion, Play policy, gradient, cubemap color/rotation/HDR/retirement, resize: GPU readback passed\n";
    Application app;Check(app.Initialize(),"Application editor initialization");auto* native=dynamic_cast<GlfwWindow*>(app.GetWindow());glfwHideWindow(native->GetGlfwHandle());app.SetupPhysicsStressScene(500);app.SetEditorPlayMode(false);app.SetGpuInstancingEnabled(true);app.GetEnvironmentSettings().enabled=false;app.GetEditorLayer().SetGridVisible(true);
    auto& visual=app.GetEditorLayer().GetVisualSettings();visual.enabled=true;visual.background=EditorBackground::Cubemap;visual.cubemap="environments/uniform.hdr";auto authored=app.GetSceneSettings();authored.background=static_cast<const SceneBackground&>(visual);Check(app.SetSceneSettings(authored),"authored cubemap settings");app.SetFrameLimit(180);Check(app.Run()==0,"editor cubemap async asset workflow");auto* adapter=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());auto diagnostics=adapter->GetEditorVisualDiagnostics();
    if(!diagnostics.skyReady){app.SetFrameLimit(300);Check(app.Run()==0,"sky async warmup");diagnostics=adapter->GetEditorVisualDiagnostics();}
    Check(diagnostics.skyReady&&diagnostics.gridDraws==1,"actual HDR loader to editor cubemap plus grid");Check(!app.GetEnvironmentSettings().enabled,"background does not enable scene IBL");Check(app.GetRenderStatistics().submitted.outputDrawCalls==1,"HDR sky reuses output pass independently of IBL");Check(app.GetRenderStatistics().submitted.drawCalls==1&&app.GetRenderStatistics().submitted.renderedInstances==501,"500 cube batching preserved");
    auto original=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());app.SetEditorPlayMode(true);app.SetFrameLimit(30);Check(app.Run()==0&&adapter->GetEditorVisualDiagnostics().gridDraws==0&&adapter->GetEditorVisualDiagnostics().backgroundDraws==1,"Application Play retains scene sky and hides grid");app.SetEditorPaused(true);app.SetFrameLimit(10);Check(app.Run()==0&&adapter->GetEditorVisualDiagnostics().gridDraws==0,"Pause stays Runtime mode");app.SetEditorPlayMode(false);app.SetFrameLimit(20);Check(app.Run()==0&&adapter->GetEditorVisualDiagnostics().gridDraws==1,"Stop restores Edit grid");Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==original,"Runtime isolation with new editor visuals");Check(adapter->GetValidationErrorCount()==0,"Application DX12 validation");app.Shutdown();
}
#endif
int main(int argc,char** argv){try{ScopedEditorPreferenceRestore preservePreferences;Cpu();
#if defined(ENABLE_DX12)
    if(argc>1)Gpu(std::string(argv[1])=="--shader-fallback");
#endif
    return 0;}catch(const std::exception& error){std::cerr<<"EDITOR VISUAL POLISH FAILED: "<<error.what()<<'\n';return 1;}}
