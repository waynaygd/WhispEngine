#include "core/Application.h"
#include "core/AssetPaths.h"
#include "editor/SunSynchronization.h"
#include "core/Logger.h"
#include <cstring>
#include <cstdlib>
#include <algorithm>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef APIENTRY
#undef APIENTRY
#endif
#include <Windows.h>
#include <shellapi.h>
#endif


int main(int argc, char** argv)
{
    Application app;

    bool legacy=false,projectArgument=false;
    for(int i=1;i<argc;++i){
        legacy|=std::strcmp(argv[i],"--legacy")==0||std::strcmp(argv[i],"--interactive-room")==0;
        projectArgument|=std::strcmp(argv[i],"--project")==0;
    }
    if (!app.Initialize(Application::StartupMode::ProjectBrowser))
        return -1;

    if(legacy&&!projectArgument){
        bool room=false;for(int i=1;i<argc;++i)room|=std::strcmp(argv[i],"--interactive-room")==0;
        const auto relative=room?std::filesystem::path("interactive_room/InteractiveRoom.whispproject"):std::filesystem::path("legacy_workspace/LegacyWorkspace.whispproject");
        std::filesystem::path descriptor;
        for(auto base:{AssetPaths::GetExecutableDirectory(),std::filesystem::current_path()}){
            for(;!base.empty();){auto candidate=base/"test_projects"/relative;if(std::filesystem::is_regular_file(candidate)){descriptor=candidate;break;}auto parent=base.parent_path();if(parent==base)break;base=parent;}
            if(!descriptor.empty())break;
        }
        std::string error;if(descriptor.empty()||!app.OpenProject(descriptor,Application::ProjectDecision::Discard,&error)){
            Logger::Get().Error("Cannot open standalone test project: "+error);app.Shutdown();return 1;
        }
    }
    bool overlap=false;int lightObjects=100;
    for(int i=1;i<argc;++i){if(std::strcmp(argv[i],"--light-overlap")==0)overlap=true;if(std::strcmp(argv[i],"--light-objects")==0&&i+1<argc)lightObjects=std::atoi(argv[++i]);}
    // Optional repeatable visual smoke/benchmark run. Normal startup is unchanged.
    for (int i = 1; i < argc; ++i) {
        if(std::strcmp(argv[i],"--project")==0){
            std::string error;
            try{
                if(i+1>=argc)throw std::runtime_error("--project requires a .whispproject path");
                std::filesystem::path descriptor;
#ifdef _WIN32
                // main's narrow argv uses the Windows code page, not UTF-8.
                int nativeCount=0;
                std::unique_ptr<wchar_t*,decltype(&LocalFree)> nativeArguments(CommandLineToArgvW(GetCommandLineW(),&nativeCount),LocalFree);
                if(!nativeArguments||i+1>=nativeCount)throw std::runtime_error("Cannot read project command line");
                descriptor=nativeArguments.get()[++i];
#else
                descriptor=std::filesystem::u8path(argv[++i]);
#endif
                if(app.OpenProject(descriptor,Application::ProjectDecision::Discard,&error))continue;
            }catch(const std::exception& e){error=e.what();}
            Logger::Get().Error(error);app.GetEditorLayer().ShowProjectBrowser(error);
            continue;
        }
        if(std::strcmp(argv[i],"--interactive-room")==0){
            // Project startup is resolved by ProjectContext above (or explicit --project).
            app.SetEditorCameraPose({-1.85f,1.65f,-5.4f},0,0);
        }
        else
        if(std::strcmp(argv[i],"--lua-demo")==0){
            std::string error;if(!app.OpenScene("scenes/lua_wave_demo.json",&error)){Logger::Get().Error(error);app.Shutdown();return 1;}
            app.SetEditorCameraPose({0,10,-14},0,-.55f);
            auto entities=app.GetWorld().Entities();if(!entities.empty())app.GetEditorLayer().SelectEntity(entities.front());
        }
        else
        if(std::strcmp(argv[i],"--editor-benchmark")==0&&i+1<argc)app.SetRenderBenchmarkOutput(argv[++i]);
        else if(std::strcmp(argv[i],"--primitive-demo")==0){
            app.NewScene();auto& editor=app.GetEditorLayer();auto sun=editor.CreateLight(app,LightType::Directional);app.GetWorld().GetComponent<ecs::DirectionalLightComponent>(sun)->primary=true;app.GetWorld().GetComponent<ecs::TransformComponent>(sun)->rotation={.35f,3.14159265f,0};
            int index=0;for(const char* type:{"cube","sphere","cylinder","cone","plane"}){auto e=editor.CreatePrimitive(app,type);auto& t=*app.GetWorld().GetComponent<ecs::TransformComponent>(e);t.position={float(index-2)*1.4f,0,4};if(index==4){t.position={0,-.51f,4};t.scale={12,1,12};}++index;}
            auto settings=app.GetSceneSettings();settings.background.sunSync=SunSyncMode::LightDrivesSky;settings.background.sunRadius=2;settings.background.sunIntensity=4;app.SetSceneSettings(settings);editor.ClearSelection();
        }
        else if(std::strcmp(argv[i],"--aa-quality")==0&&i+1<argc){auto s=app.GetSceneSettings();s.antiAliasing=AntiAliasingMode(std::clamp(std::atoi(argv[++i]),0,2));app.SetSceneSettings(s);}
        else if(std::strcmp(argv[i],"--game-view")==0)app.GetEditorLayer().SetGameView(true);
        else if(std::strcmp(argv[i],"--known-sun-demo")==0){auto sun=editor::PrimarySun(app.GetWorld());if(!sun.IsValid())sun=app.GetEditorLayer().CreateLight(app,LightType::Directional);app.GetWorld().GetComponent<ecs::DirectionalLightComponent>(sun)->primary=true;auto s=app.GetSceneSettings();s.background.background=EditorBackground::Cubemap;s.background.cubemap="environments/editor24_known_sun.hdr";s.background.sunMetadata=true;s.background.sunDirection={0,.34202014f,.93969262f};s.background.sunSync=SunSyncMode::SkyDrivesLight;s.background.visibleSunDisc=false;s.environment.enabled=true;s.environment.assetPath=s.background.cubemap;app.SetSceneSettings(s);}
        else if(std::strcmp(argv[i],"--daynight-demo")==0&&i+1<argc){float elevation=std::clamp(float(std::atof(argv[++i])),-85.f,85.f)*.01745329252f;auto sun=editor::PrimarySun(app.GetWorld());if(!sun.IsValid())sun=app.GetEditorLayer().CreateLight(app,LightType::Directional);app.GetWorld().GetComponent<ecs::DirectionalLightComponent>(sun)->primary=true;app.GetWorld().GetComponent<ecs::TransformComponent>(sun)->rotation={elevation,3.14159265f,0};auto s=app.GetSceneSettings();s.background.background=EditorBackground::DayNight;s.background.driveLightProperties=true;s.background.sunSync=SunSyncMode::Independent;s.background.sunRadius=2;s.background.sunIntensity=4;app.SetSceneSettings(s);}
        else if(std::strcmp(argv[i],"--sky-off")==0)app.GetEditorLayer().SetSkyDiagnosticEnabled(false);
        else if(std::strcmp(argv[i],"--asset-grid")==0)app.GetEditorLayer().SetAssetGrid(true);
        else if(std::strcmp(argv[i],"--diagnostic-viewport")==0&&i+2<argc){int w=std::atoi(argv[++i]);int h=std::atoi(argv[++i]);app.GetEditorLayer().SetDiagnosticViewport(w,h);}
        else if(std::strcmp(argv[i],"--shadow-quality")==0&&i+1<argc){int q=std::clamp(std::atoi(argv[++i]),0,4);ApplyShadowQuality(app.GetShadowSettings(),ShadowQuality(q));}
        else if(std::strcmp(argv[i],"--select-first")==0){auto entities=app.GetWorld().Entities();if(!entities.empty())app.GetEditorLayer().SelectEntity(entities.front());}
        else if(std::strcmp(argv[i],"--scene-background-panel")==0)app.GetEditorLayer().ShowSceneBackground();
        else if(std::strcmp(argv[i],"--play")==0)app.SetEditorPlayMode(true);
        else if(std::strcmp(argv[i],"--no-editor-visuals")==0){app.GetEditorLayer().GetVisualSettings().enabled=false;app.GetEditorLayer().SetSkyDiagnosticEnabled(false);}
        else if(std::strcmp(argv[i],"--editor-grid-only")==0){auto settings=app.GetSceneSettings();auto& s=settings.background;s.background=EditorBackground::Solid;app.SetSceneSettings(settings);}
        else if(std::strcmp(argv[i],"--editor-gradient")==0){auto settings=app.GetSceneSettings();auto& s=settings.background;s.background=EditorBackground::Gradient;app.SetSceneSettings(settings);}
        else if(std::strcmp(argv[i],"--editor-cubemap")==0&&i+1<argc){auto settings=app.GetSceneSettings();auto& s=settings.background;s.background=EditorBackground::Cubemap;s.cubemap=argv[++i];app.SetSceneSettings(settings);}
        else if(std::strcmp(argv[i],"--editor-polish-demo")==0){app.SetupLightDemoScene();app.GetEditorLayer().ResetLayout();app.GetEditorLayer().ShowVisualSettings();app.GetEditorLayer().SetGridVisible(true);app.GetEditorLayer().GetVisualSettings().enabled=true;}
        else if(std::strcmp(argv[i],"--fixed-editor-layout")==0){app.GetEditorLayer().SetFixedDiagnosticLayout(true);auto entities=app.GetWorld().Entities();if(!entities.empty())app.GetEditorLayer().SelectEntity(entities.front());}
        else if(std::strcmp(argv[i],"--editor2-demo")==0){app.GetEditorLayer().ResetLayout();app.GetEditorLayer().SetLightGizmosVisible(true);app.SetupLightDemoScene();}
        else if(std::strcmp(argv[i],"--light-demo")==0)app.SetupLightDemoScene();
        else if(std::strcmp(argv[i],"--light-stress")==0){int count=100;if(i+1<argc&&argv[i+1][0]!='-')count=std::atoi(argv[++i]);app.SetupLightStressScene(count,overlap,lightObjects);}
        else if(std::strcmp(argv[i],"--light-benchmark")==0&&i+2<argc){app.SetupLightStressScene(std::atoi(argv[++i]),overlap,lightObjects);app.SetLightBenchmarkOutput(argv[++i]);}
        else if(std::strcmp(argv[i],"--forward-lights")==0)app.SetLightRenderingMode(LightRenderingMode::Forward);
        else if(std::strcmp(argv[i],"--tiled-lights")==0)app.SetLightRenderingMode(LightRenderingMode::Tiled);
        else if(std::strcmp(argv[i],"--no-light-gizmos")==0)app.GetEditorLayer().SetLightGizmosVisible(false);
        else if(std::strcmp(argv[i],"--pbr-textures-demo")==0){app.SetupPbrTexturesDemoScene();}
        else if(std::strcmp(argv[i],"--pbr-textures-benchmark")==0 && i+2<argc){app.SetupPbrTexturesBenchmarkScene(std::atoi(argv[++i]));app.SetPbrTexturesBenchmarkOutput(argv[++i]);}
        else if(std::strcmp(argv[i],"--ibl-benchmark")==0&&i+2<argc){app.SetupIblBenchmarkScene(std::atoi(argv[++i]));app.SetIblBenchmarkOutput(argv[++i]);}
        else if(std::strcmp(argv[i],"--ibl-demo")==0){app.SetupIblDemoScene();}
        else if(std::strcmp(argv[i],"--no-ibl")==0){app.GetEnvironmentSettings().enabled=false;}
        else if(std::strcmp(argv[i],"--ibl-diffuse")==0){app.GetEnvironmentSettings().specular=false;}
        else if(std::strcmp(argv[i],"--ibl-specular")==0){app.GetEnvironmentSettings().diffuse=false;}
        else if(std::strcmp(argv[i],"--environment")==0&&i+1<argc){app.GetEnvironmentSettings().assetPath=argv[++i];}
        else if(std::strcmp(argv[i],"--environment-rotation")==0&&i+1<argc){app.GetEnvironmentSettings().rotationDegrees=std::atof(argv[++i]);}
        else if(std::strcmp(argv[i],"--exposure")==0&&i+1<argc){app.GetEnvironmentSettings().exposure=std::atof(argv[++i]);}
        else if(std::strcmp(argv[i],"--no-tone-mapping")==0){app.GetEnvironmentSettings().toneMapping=false;}
        else if(std::strcmp(argv[i],"--no-normal-mapping")==0){app.SetPbrTextureMask(app.GetPbrTextureMask()&~1u);}
        else if(std::strcmp(argv[i],"--no-pbr-textures")==0){app.SetPbrTextureMask(0);}
        else if(std::strcmp(argv[i],"--pbr-demo")==0) {
            app.SetupPbrDemoScene();
        } else if(std::strcmp(argv[i],"--pbr")==0) {
            app.SetShadingModelOverride(1);
        } else if(std::strcmp(argv[i],"--lambert")==0) {
            app.SetShadingModelOverride(0);
        } else if(std::strcmp(argv[i],"--pbr-benchmark")==0 && i+2<argc) {
            app.SetupPhysicsStressScene(std::atoi(argv[++i]));app.SetEditorPlayMode(false);app.SetPbrBenchmarkOutput(argv[++i]);
        } else if (std::strcmp(argv[i], "--shadow-demo") == 0) {
            app.SetupShadowQualityDemoScene();
        } else if (std::strcmp(argv[i], "--material-demo") == 0) {
            app.SetupMaterialDemoScene();app.SetEditorPlayMode(false);
        } else if(std::strcmp(argv[i],"--material-benchmark")==0 && i+2<argc) {
            app.SetupMaterialDemoScene(std::atoi(argv[++i]));app.SetEditorPlayMode(false);app.SetShadowBenchmarkOutput(argv[++i]);
        } else if (std::strcmp(argv[i], "--capture-frame") == 0 && i+1<argc) {
            app.SetFrameCaptureOutput(argv[++i]);
        } else if (std::strcmp(argv[i], "--no-shadows") == 0) {
            app.GetShadowSettings().enabled=false;
        } else if (std::strcmp(argv[i], "--hard-shadows") == 0) {
            app.GetShadowSettings().pcf=false;
        } else if (std::strcmp(argv[i], "--shadow-fixed") == 0) {
            app.GetShadowSettings().cameraFitted=false;
        } else if (std::strcmp(argv[i], "--shadow-coverage") == 0 && i+1<argc) {
            app.GetShadowSettings().coverage=std::atof(argv[++i]);
        } else if (std::strcmp(argv[i], "--shadow-distance") == 0 && i+1<argc) {
            app.GetShadowSettings().focusDistance=std::atof(argv[++i]);
        } else if (std::strcmp(argv[i], "--shadow-receiver-bias") == 0 && i+1<argc) {
            app.GetShadowSettings().receiverBias=std::atof(argv[++i]);
        } else if (std::strcmp(argv[i], "--shadow-normal-bias") == 0 && i+1<argc) {
            app.GetShadowSettings().normalBias=std::atof(argv[++i]);
        } else if (std::strcmp(argv[i], "--shadow-debug") == 0 && i+1<argc) {
            app.GetShadowSettings().debugMode=std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--shadow-resolution") == 0 && i+1<argc) {
            app.GetShadowSettings().resolution=std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--shadow-benchmark") == 0 && i+2<argc) {
            app.SetupPhysicsStressScene(std::atoi(argv[++i]));app.SetEditorPlayMode(false);
            app.SetShadowBenchmarkOutput(argv[++i]);
        } else if (std::strcmp(argv[i], "--no-lighting") == 0) {
            app.GetLightingSettings().enabled=false;
        } else if (std::strcmp(argv[i], "--physics-stress-demo") == 0) {app.GetEditorLayer().SetPhysicsPanelVisible(true);app.SetupPhysicsStressScene(500);
        } else if (std::strcmp(argv[i], "--stress") == 0 && i + 1 < argc) {
            app.SetupPhysicsStressScene(std::atoi(argv[++i]));
            app.SetEditorPlayMode(true);
        } else if (std::strcmp(argv[i], "--serial") == 0) {
            app.GetPhysicsSystem()->SetParallel(false);
        } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            app.SetFrameLimit(std::max(1, std::atoi(argv[++i])));
        } else if (std::strcmp(argv[i], "--diagnostics") == 0 && i + 1 < argc) {
            app.SetDiagnosticOutput(argv[++i]);
        } else if (std::strcmp(argv[i], "--stability-scenario") == 0 && i + 1 < argc) {
            app.SetDiagnosticOutput(argv[++i], true);
        } else if (std::strcmp(argv[i], "--wait-tracy") == 0) {
            app.WaitForTracyConnection();
        } else if (std::strcmp(argv[i], "--fixed-state") == 0) {
            app.SetUpdateMode(UpdateMode::Fixed);
        } else if (std::strcmp(argv[i], "--no-interpolation") == 0) {
            app.GetRenderInterpolation().SetEnabled(false);
        } else if (std::strcmp(argv[i], "--no-instancing") == 0) {
            app.SetGpuInstancingEnabled(false);
        } else if (std::strcmp(argv[i], "--lighting-benchmark") == 0 && i+2<argc) {
            app.SetupPhysicsStressScene(std::atoi(argv[++i]));
            app.SetEditorPlayMode(false);
            app.SetLightingBenchmarkOutput(argv[++i]);
        } else if (std::strcmp(argv[i], "--render-benchmark") == 0 && i+2<argc) {
            app.SetupPhysicsStressScene(std::atoi(argv[++i]));
            app.SetEditorPlayMode(false);
            app.SetRenderBenchmarkOutput(argv[++i]);
        } else if (std::strcmp(argv[i], "--slow-frame") == 0 && i + 2 < argc) {
            const auto frame = std::max(0, std::atoi(argv[++i]));
            const auto milliseconds = std::max(0, std::atoi(argv[++i]));
            app.SetDiagnosticSlowFrame(frame, milliseconds);
        }
    }

    int result = app.Run();
    app.Shutdown();
    return result;
}
