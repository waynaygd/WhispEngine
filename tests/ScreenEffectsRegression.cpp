#define NOMINMAX
#include "EditorTestPreferences.h"
#include "SyntheticEditorInput.h"
#include "ApplicationTestAccess.h"
#include "core/Application.h"
#include "core/AssetPaths.h"
#include "ecs/PostProcessEvaluation.h"
#include "ecs/components/SceneIdentityComponent.h"
#include "scene/SceneSerializer.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include <imgui_internal.h>
#include <iostream>
#include <fstream>
using namespace ecs;using namespace editor;
static unsigned checks=0;
static void Check(bool value,const std::string& text){if(!value)throw std::runtime_error(text);++checks;}
static void Near(float a,float b,const char* text){Check(std::abs(a-b)<.0002f,text);}
static double Difference(const std::vector<unsigned char>& a,const std::vector<unsigned char>& b){Check(a.size()==b.size()&&!a.empty(),"comparable images");double sum=0;for(size_t i=0;i<a.size();++i)sum+=std::abs(int(a[i])-int(b[i]));return sum/a.size();}
struct Owned {std::filesystem::path path;~Owned(){std::error_code error;std::filesystem::remove(path,error);}};
static void Cpu(){
 World world;PostProcessSettings base;auto global=world.CreateEntity();world.AddComponent<TransformComponent>(global);auto& g=world.AddComponent<PostProcessVolumeComponent>(global);g.saturation=.8f;g.tint=std::array<float,4>{1,.8f,.6f,1};
 auto local=world.CreateEntity();world.AddComponent<TransformComponent>(local);auto& l=world.AddComponent<PostProcessVolumeComponent>(local);l.global=false;l.priority=1;l.extents={1,1,1};l.blendRadius=2;l.saturation=.2f;l.vignetteIntensity=1.f;
 auto result=EvaluatePostProcess({2,0,0},world,base);Near(result.screenEffects.saturation,.5f,"local/global saturation blend");Near(result.screenEffects.vignetteIntensity,.5f,"shared box weight drives independent override");Near(result.screenEffects.tint[1],.8f,"unchecked tint inherits global");Near(result.screenEffects.contrast,1,"unchecked contrast inherits neutral base");
 Check(EvaluatePostProcess({3,0,0},world,base).screenEffects.saturation==.8f,"outside local retains global");auto authored=ComponentRegistry::Builtins().Capture(world,local);for(int i=0;i<5;++i)EvaluatePostProcess({},world,base);Check(ComponentRegistry::Builtins().Capture(world,local)==authored,"screen evaluation never mutates authored values");
 l.grainScale=0;Check(!ValidatePostProcessVolume(l).empty(),"invalid grain size diagnosed");l.grainScale.reset();l.vignetteSmoothness=std::numeric_limits<float>::infinity();Check(!ValidatePostProcessVolume(l).empty(),"infinite vignette diagnosed");l.vignetteSmoothness.reset();l.aberrationFalloff=-1;Check(!ValidatePostProcessVolume(l).empty(),"invalid aberration diagnosed");l.aberrationFalloff.reset();l.tint=std::array<float,4>{1,std::numeric_limits<float>::quiet_NaN(),1,1};Check(!ValidatePostProcessVolume(l).empty(),"nonfinite tint diagnosed");
 ScreenEffects invalid;invalid.grainScale=0;invalid.brightness=10;invalid.tint[0]=std::numeric_limits<float>::quiet_NaN();auto safe=SanitizeScreenEffects(invalid);Near(safe.grainScale,.5f,"render boundary bounds grain");Near(safe.brightness,1,"render boundary bounds brightness");Near(safe.tint[0],1,"render boundary finite tint");Check(invalid.grainScale==0&&invalid.brightness==10,"sanitizer does not write authoring data");Check(!ScreenEffects{}.Active(),"neutral state bypasses effects");
}
static void Native(){
 Application app;Check(app.Initialize(),"DX12 initialize");ApplicationTestAccess::FrameDelta(app,1.f/30);SyntheticEditorInput input;auto& ui=app.GetEditorLayer();auto frame=[&](unsigned n=4){app.SetFrameLimit(n);Check(app.Run()==0,"native frames");};
 std::string error;Check(app.OpenScene("scenes/interactive_room.json",&error),error);ui.SetFixedDiagnosticLayout(true);ui.SetDiagnosticViewport(400,300);ui.SetGameView(true);ui.SetGridVisible(false);Check(app.SetEditorCameraPose({-1.85f,1.65f,-5.4f},0,0),"room camera");frame(18);
 auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());Check(gpu!=nullptr,"DX12 backend");auto image=[&](){return gpu->ReadBackViewportPixels();};auto baseline=image();
 auto capture=[&](const char* name){auto pixels=image();std::ofstream out(std::string("diagnostics/screen-effects/")+name+"-scene.ppm",std::ios::binary);out<<"P6\n400 300\n255\n";for(size_t i=0;i<pixels.size();i+=4)out.write(reinterpret_cast<const char*>(pixels.data()+i),3);Check(bool(out),"viewport pixel capture");};
 auto entity=ui.CreatePostProcessVolume(app,true);auto volume=[&]()->PostProcessVolumeComponent&{return *app.GetWorld().GetComponent<PostProcessVolumeComponent>(entity);};volume()={};frame();Check(Difference(baseline,image())==0,"neutral volume is pixel-identical to legacy scene");
 std::filesystem::create_directories("diagnostics/screen-effects");Check(gpu->SaveFrameImage("diagnostics/screen-effects/neutral.ppm"),"neutral capture");capture("neutral");
 volume().saturation=0;frame();auto gray=image();double chroma=0;for(size_t i=0;i<gray.size();i+=4)chroma+=std::abs(int(gray[i])-int(gray[i+1]))+std::abs(int(gray[i])-int(gray[i+2]));Check(chroma/gray.size()<.01,"saturation zero produces grayscale real DX12 pixels");Check(Difference(baseline,gray)>.5,"grayscale changes colored scene");Check(gpu->SaveFrameImage("diagnostics/screen-effects/grayscale.ppm"),"grayscale capture");capture("grayscale");
 volume().saturation=1;volume().contrast=1;volume().brightness=0;volume().tint=std::array<float,4>{1,1,1,1};volume().vignetteIntensity=0;volume().grainIntensity=0;volume().aberrationIntensity=0;frame();Check(Difference(baseline,image())==0,"explicit neutral overrides remain pixel-identical");
 volume().vignetteIntensity=1;frame();auto vignette=image();double center=0,edge=0;unsigned nc=0,ne=0;for(int y=0;y<300;++y)for(int x=0;x<400;++x){auto at=(y*400+x)*4;double diff=0;for(int c=0;c<3;++c)diff+=int(baseline[at+c])-int(vignette[at+c]);if(x>180&&x<220&&y>130&&y<170){center+=std::abs(diff);++nc;}if(x<30||x>370||y<20||y>280){edge+=diff;++ne;}}Check(center/nc<.1&&edge/ne>10,"vignette preserves center and darkens edges");Check(gpu->SaveFrameImage("diagnostics/screen-effects/vignette.ppm"),"vignette capture");capture("vignette");
 volume().vignetteIntensity=0;volume().grainIntensity=1;frame();auto grain=image();Check(Difference(baseline,grain)>.5,"procedural grain visible in real pixels");frame(2);Check(Difference(grain,image())>.1,"grain evolves between frames");Check(gpu->SaveFrameImage("diagnostics/screen-effects/grain.ppm"),"grain capture");capture("grain");
 volume().grainIntensity=0;volume().aberrationIntensity=1;frame();auto aberration=image();Check(Difference(baseline,aberration)>.1,"aberration separates scene channels");Check(gpu->SaveFrameImage("diagnostics/screen-effects/aberration.ppm"),"aberration capture");capture("aberration");
 volume().aberrationIntensity=0;volume().contrast=1.4f;volume().brightness=.1f;volume().tint=std::array<float,4>{1,.8f,.6f,1};frame();Check(Difference(baseline,image())>3,"contrast brightness tint visible");
 // Neutral output and grayscale also work with existing HDR exposure and both FXAA modes.
 auto originalSettings=app.GetSceneSettings();
 for(bool hdr:{false,true})for(auto aa:{AntiAliasingMode::Off,AntiAliasingMode::FxaaLow,AntiAliasingMode::FxaaHigh}){
  auto settings=originalSettings;settings.environment.enabled=hdr;settings.environment.exposure=1;settings.antiAliasing=aa;Check(app.SetSceneSettings(settings),"HDR/AA settings");volume()={};volume().enabled=false;frame();auto legacy=image();volume().enabled=true;frame();Check(Difference(legacy,image())==0,"neutral HDR/FXAA preserves existing output exactly");volume().saturation=0;frame();auto monochrome=image();unsigned chromatic=0;for(size_t i=0;i<monochrome.size();i+=4)chromatic+=monochrome[i]!=monochrome[i+1]||monochrome[i]!=monochrome[i+2];Check(chromatic==0,"grayscale follows HDR tonemap and FXAA");Check(gpu->GetSubmissionStatistics().outputDrawCalls==1,"effects use one existing fullscreen output draw");
 }
 Check(app.SetSceneSettings(originalSettings),"restore authored settings");volume().saturation=0;volume().contrast=1.4f;volume().brightness=.1f;volume().tint=std::array<float,4>{1,.8f,.6f,1};frame();
 // UI is submitted after scene output: sample the stable menu strip outside the viewport.
 frame();auto uiBefore=gpu->ReadBackFramePixels();volume().saturation=0;volume().grainIntensity=1;frame();auto uiAfter=gpu->ReadBackFramePixels();auto width=int(ImGui::GetIO().DisplaySize.x);unsigned changed=0;for(int y=5;y<20;++y)for(int x=5;x<300;++x)for(int c=0;c<3;++c)changed+=uiBefore[(y*width+x)*4+c]!=uiAfter[(y*width+x)*4+c];Check(changed==0,"postfx leaves real ImGui menu pixels unchanged");
 volume().grainIntensity=0;Check(ui.SetProperty(app,entity,ComponentId("PostProcessVolume"),"saturation",.4f),ui.GetError());ui.UndoEdit(app);Near(*volume().saturation,0,"screen override Undo");ui.RedoEdit(app);Near(*volume().saturation,.4f,"screen override Redo");
 ui.SelectEntity(entity);Check(ui.CopyEntity(app,entity),"copy screen volume");auto copy=ui.PasteEntity(app);Check(copy.IsValid(),"paste screen volume");auto* copied=app.GetWorld().GetComponent<PostProcessVolumeComponent>(copy);Check(copied&&copied->saturation==volume().saturation&&copied->tint==volume().tint,"copy preserves screen overrides");ui.UndoEdit(app);
 auto token=NewSceneIdentity();auto key="scenes/fx-"+token+".json",prefabKey="prefabs/fx-"+token+".prefab.json";Owned scene{AssetPaths::ResolveAssetOutputPath(key)},prefab{AssetPaths::ResolveAssetOutputPath(prefabKey)};
 ui.SelectEntity(entity);Check(ui.CreatePrefab(app,prefabKey),ui.GetError());Check(ui.SetProperty(app,entity,ComponentId("PostProcessVolume"),"brightness",.2f)&&ui.ApplyPrefab(app,entity),ui.GetError());Check(ui.SetProperty(app,entity,ComponentId("PostProcessVolume"),"brightness",.4f)&&ui.RevertPrefab(app,entity),ui.GetError());Near(*volume().brightness,.2f,"Prefab Apply/Revert preserves postfx");
 Check(app.SaveSceneAs(key,false,&error),error);auto saved=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());Check(app.OpenScene(key,&error),error);Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==saved,"screen overrides Save/Open exact roundtrip");
 // Same evaluated state survives runtime camera, Pause, workspace/F11 and resize.
 app.SetEditorPlayMode(true);frame(8);Near(ApplicationTestAccess::PostProcess(app).screenEffects.saturation,.4f,"runtime camera receives screen effects");app.SetEditorPaused(true);frame();Near(ApplicationTestAccess::PostProcess(app).screenEffects.brightness,.2f,"paused effects remain active");
 auto targetBytes=gpu->GetAntiAliasingDiagnostics().intermediateBytes;
 for(auto size:{std::pair{640,240},std::pair{240,400},std::pair{400,300},std::pair{640,240},std::pair{240,400},std::pair{400,300}}){ui.SetDiagnosticViewport(size.first,size.second);frame();Check(image().size()==size_t(size.first*size.second*4),"postfx resized viewport readback");}
 Check(gpu->GetAntiAliasingDiagnostics().intermediateBytes==targetBytes,"returning to same size has bounded target allocations");
 auto fullscreen=[&](){ImGui::FocusWindow(ImGui::FindWindowByName(ui.IsViewportMaximized()?"Viewport##Maximized":"Viewport"));SyntheticEditorInput::Key(ImGuiKey_F11,true);frame(1);SyntheticEditorInput::Key(ImGuiKey_F11,false);frame();};fullscreen();Check(ui.IsViewportMaximized(),"F11 enters with effects");fullscreen();Check(!ui.IsViewportMaximized(),"F11 returns with effects");ui.SwitchWorkspace(EditorWorkspace::GameplayDebugging);frame();Near(ApplicationTestAccess::PostProcess(app).screenEffects.saturation,.4f,"workspace retains effects");
 app.SetEditorPlayMode(false);frame();Check(SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings())==saved,"Play Pause Stop preserves authored effects");Check(gpu->GetValidationErrorCount()==0,"zero DX12 resource/state validation errors");app.Shutdown();
}
int main(){ScopedEditorPreferenceRestore preferences;try{Cpu();Native();std::cout<<"Screen Effects: "<<checks<<" checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<"SCREEN EFFECTS FAILED after "<<checks<<": "<<error.what()<<'\n';return 1;}}
