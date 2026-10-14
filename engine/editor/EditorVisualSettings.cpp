#include "EditorLayer.h"
#include "EditorWidgets.h"
#include "LightGizmoMath.h"
#include "../platform/GlfwWindow.h"
#include <GLFW/glfw3.h>
#include "../core/Application.h"
#include "../resources/ResourceManager.h"
#include <misc/cpp/imgui_stdlib.h>
namespace editor {
EditorVisualFrame EditorLayer::BuildVisualFrame(const Application& app)const{
    EditorVisualFrame f;const bool viewport=m_ShowViewport&&m_ViewportPixelWidth>1&&m_ViewportPixelHeight>1;int width=m_ViewportPixelWidth,height=m_ViewportPixelHeight;
    if(!viewport)if(auto* window=dynamic_cast<const GlfwWindow*>(app.GetWindow()))glfwGetFramebufferSize(window->GetGlfwHandle(),&width,&height);
    f.active=width>1&&height>1;f.play=app.IsEditorPlayMode();f.showGrid=viewport&&m_ShowGrid;f.settings=m_VisualSettings;f.backgroundEnabled=m_SkyDiagnosticEnabled;
    if(f.play||!m_BackgroundOverride){static_cast<SceneBackground&>(f.settings)=app.GetSceneBackground();if(f.settings.useIblEnvironment){f.settings.cubemap=app.GetEnvironmentSettings().assetPath;f.settings.skyRotation=app.GetEnvironmentSettings().rotationDegrees;}}
    f.aspect=float(std::max(1,width))/std::max(1,height);f.tanHalfFov=std::tan(app.GetCameraVerticalFovRadians()*.5f);f.nearPlane=app.GetCameraNearPlane();f.farPlane=app.GetCameraFarPlane();
    const auto p=app.GetCameraPosition();f.camera={p.x,p.y,p.z};const auto c=GizmoCamera(p,app.GetCameraYaw(),app.GetCameraPitch(),app.GetCameraVerticalFovRadians(),f.aspect,f.nearPlane,f.farPlane);
    f.right={c.view[0],c.view[4],c.view[8]};f.up={c.view[1],c.view[5],c.view[9]};f.forward={c.view[2],c.view[6],c.view[10]};return f;
}
void EditorLayer::PrepareEditorSky(Application& app,IRenderAdapter* renderer){
    const auto frame=BuildVisualFrame(app);if(!renderer||!frame.active||!frame.backgroundEnabled||frame.settings.background!=EditorBackground::Cubemap)return;
    auto* resources=app.GetResourceManager();if(!resources)return;const auto& key=frame.settings.cubemap;
    if(!ecs::ComponentRegistry::ValidateAssetKey(key,ecs::AssetKind::Environment)||key.empty())return;
    if(m_RequestedSky!=key||!resources->Has<EnvironmentResource>(key)){m_RequestedSky=key;m_FailedSkyVersion=0;resources->LoadAsync<EnvironmentResource>(key);resources->WatchForHotReload<EnvironmentResource>(key);}
    auto asset=resources->Get<EnvironmentResource>(key);
    if(asset&&asset->IsLoaded()&&!asset->GetData().placeholder&&asset->GetVersion()!=m_FailedSkyVersion)
        if(!renderer->UploadEditorSky(asset->GetData(),asset->GetVersion()))m_FailedSkyVersion=asset->GetVersion();
}
void EditorLayer::DrawEditorVisualSettings(Application& app,IRenderAdapter* renderer){
    // Let DockBuilder restore its nodes before selecting the newly opened tab.
    if(m_FocusVisualSettings&&ImGui::GetFrameCount()>2){ImGui::SetNextWindowFocus();m_FocusVisualSettings=false;}
    if(!ImGui::Begin("Editor Visuals",&m_ShowVisualSettings)){ImGui::End();return;}
    ImGui::PushItemWidth(std::max(90.f,ImGui::GetContentRegionAvail().x*.48f));
    ImGui::Checkbox("Editor background override",&m_BackgroundOverride);
    auto& s=m_VisualSettings;ImGui::TextDisabled("Preferences / Edit mode only");ImGui::Checkbox("Editor visuals enabled",&s.enabled);
    if(ImGui::CollapsingHeader("Infinite grid",ImGuiTreeNodeFlags_DefaultOpen)){
        ImGui::Checkbox("Show grid",&m_ShowGrid);bool hideInPlay=true;ImGui::BeginDisabled();ImGui::Checkbox("Hide grid in Play",&hideInPlay);ImGui::EndDisabled();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Editor-only helper: always hidden in Play, including Pause.");
        ImGui::DragFloat("Minor spacing",&s.minorSpacing,.05f,.01f,1000);s.majorSpacing=std::max(s.majorSpacing,s.minorSpacing);
        ImGui::DragFloat("Major spacing",&s.majorSpacing,.5f,s.minorSpacing,10000);ImGui::SliderFloat("Line opacity",&s.gridOpacity,0,1);ImGui::DragFloat("Fade distance",&s.fadeDistance,1,1,100000);
    }
    if(ImGui::CollapsingHeader("Background",ImGuiTreeNodeFlags_DefaultOpen)){
        int mode=int(s.background);if(ImGui::Combo("Mode",&mode,"Solid clear color\0Procedural gradient\0Cubemap skybox\0"))s.background=EditorBackground(mode);
        ImGui::SliderFloat("Intensity",&s.skyIntensity,0,8);
        if(s.background==EditorBackground::Solid)LinearColorEdit("Solid color",s.solid.data(),3);
        else if(s.background==EditorBackground::Gradient){LinearColorEdit("Top",s.top.data(),3);LinearColorEdit("Horizon",s.horizon.data(),3);LinearColorEdit("Ground tint",s.bottom.data(),3);ImGui::SliderFloat("Gradient blend",&s.gradientBlend,.1f,4);}
        else{
            ImGui::InputText("HDR environment",&s.cubemap);AcceptAssetDrop(ecs::AssetKind::Environment,s.cubemap);ImGui::SliderFloat("Sky rotation",&s.skyRotation,-180,180);
            if(ImGui::Button("Use scene environment"))s.cubemap=app.GetEnvironmentSettings().assetPath;
            ImGui::TextWrapped("HDR assets are converted to a GPU cubemap by the existing Environment loader. Sky and scene IBL settings are independent.");
            if(auto* resources=app.GetResourceManager())if(auto asset=resources->Get<EnvironmentResource>(s.cubemap)){if(asset->IsLoading())ImGui::TextDisabled("Loading sky asynchronously...");if(asset->IsFailed())ImGui::TextWrapped("%s",asset->GetErrorMessage().c_str());}
        }
    }
    if(ImGui::Button("Save visual preferences"))PersistPreferences(app);
    if(renderer){auto d=renderer->GetEditorVisualDiagnostics();ImGui::Text("Helper draws: sky %u / grid %u",d.backgroundDraws,d.gridDraws);ImGui::Text("Visual submit CPU: %.3f ms",d.cpuMs);if(!d.error.empty())ImGui::TextWrapped("%s",d.error.c_str());}
    ImGui::TextWrapped("Depth-tested world helper, Y=0. No authored entities or scene draws are added.");ImGui::PopItemWidth();ImGui::End();
}
}
