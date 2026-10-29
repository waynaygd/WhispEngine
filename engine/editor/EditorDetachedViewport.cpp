#include "EditorLayer.h"
#include "../core/Application.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cstring>
#include <cmath>
namespace editor {
bool BeginEditorPanel(const char* name,bool* open,int flags){
    if((ImGui::GetIO().ConfigFlags&ImGuiConfigFlags_ViewportsEnable)&&std::strncmp(name,"Viewport",8)!=0){
        auto* main=ImGui::GetMainViewport();ImGui::SetNextWindowViewport(main->ID);
        // B exposes only the scene Viewport. Keep other floating panel titles
        // reachable in the main native window without rebuilding their docks.
        if(auto* w=ImGui::FindWindowByName(name);w&&!w->DockId&&!(flags&ImGuiWindowFlags_NoSavedSettings)){
            ImVec2 p{std::clamp(w->Pos.x,main->WorkPos.x,main->WorkPos.x+std::max(0.f,main->WorkSize.x-80)),std::clamp(w->Pos.y,main->WorkPos.y,main->WorkPos.y+std::max(0.f,main->WorkSize.y-60))};
            if(p.x!=w->Pos.x||p.y!=w->Pos.y)ImGui::SetNextWindowPos(p);
        }
    }
    return ImGui::Begin(name,open,flags);
}
void* EditorLayer::ViewportPlatformHandle()const{
    if(!m_ShowViewport||!ImGui::GetCurrentContext())return nullptr;
    auto* v=ImGui::FindViewportByID(m_ViewportPlatformId);return v?v->PlatformHandle:nullptr;
}
bool EditorLayer::DetachViewport(Application& app){
    if(!ImGui::GetCurrentContext()||!app.GetRenderAdapter()||!app.GetRenderAdapter()->SupportsEditorPlatformWindows()){m_EditorError="Detached Viewport is unavailable on this render backend";return false;}
    if(m_ViewportMaximized||m_WorkspaceFocus){m_EditorError="Leave workspace focus/F11 before detaching Viewport";return false;}
    app.ReleaseEditorCameraCapture();
    if(auto* w=ImGui::FindWindowByName("Viewport")){m_ViewportReturnDock=w->DockId;m_ViewportPlacement.width=w->SizeFull.x;m_ViewportPlacement.height=w->SizeFull.y;}
    auto* main=ImGui::GetMainViewport();m_ViewportPlacement.x=main->Pos.x+main->Size.x+32;m_ViewportPlacement.y=main->Pos.y+80;
    m_ShowViewport=true;m_ViewportPlacement.detached=true;m_ViewportPlacementPending=true;app.GetRenderAdapter()->EnableEditorPlatformWindows();return true;
}
void EditorLayer::DockViewport(Application& app){
    app.ReleaseEditorCameraCapture();m_ViewportPlacement.detached=false;m_ViewportPlacementPending=false;m_ViewportDockPending=true;m_ShowViewport=true;m_FocusViewportWindow=true;
}
void EditorLayer::RestoreViewportPlacement(Application& app){
    m_ViewportPlacement={};if(auto it=m_Workspaces.find(WorkspaceKeys[int(m_Workspace)]);it!=m_Workspaces.end())m_ViewportPlacement=it->second.viewport;
    if(m_ViewportPlacement.detached&&app.GetRenderAdapter()&&app.GetRenderAdapter()->SupportsEditorPlatformWindows()){
        app.GetRenderAdapter()->EnableEditorPlatformWindows();m_ViewportPlacementPending=true;
    }else {m_ViewportPlacement.detached=false;m_ViewportPlacementPending=false;}
}
void EditorLayer::PrepareDetachedViewport(Application& app,IRenderAdapter* renderer){
    if(m_ViewportPlacement.detached&&m_WorkspaceFocus)DockViewport(app);
    if(m_ViewportPlacement.detached){
        if(!renderer||!renderer->SupportsEditorPlatformWindows()){m_ViewportPlacement.detached=false;return;}
        renderer->EnableEditorPlatformWindows();
        if(!(ImGui::GetIO().ConfigFlags&ImGuiConfigFlags_ViewportsEnable))return;
        if(m_ViewportPlacementPending){
            auto& p=m_ViewportPlacement;bool visible=false;
            for(const auto& monitor:ImGui::GetPlatformIO().Monitors){
                if(p.x+std::min(p.width,80.f)>monitor.WorkPos.x&&p.x<monitor.WorkPos.x+monitor.WorkSize.x&&p.y+30>monitor.WorkPos.y&&p.y<monitor.WorkPos.y+monitor.WorkSize.y-30){visible=true;break;}
            }
            if(!visible){auto* main=ImGui::GetMainViewport();p.x=main->WorkPos.x+48;p.y=main->WorkPos.y+80;p.width=std::clamp(p.width,420.f,std::max(420.f,main->WorkSize.x-96));p.height=std::clamp(p.height,280.f,std::max(280.f,main->WorkSize.y-160));}
            ImGui::SetNextWindowDockID(0,ImGuiCond_Always);ImGui::SetNextWindowPos({p.x,p.y});ImGui::SetNextWindowSize({std::clamp(p.width,420.f,8192.f),std::clamp(p.height,280.f,8192.f)});m_ViewportPlacementPending=false;
        }
        ImGuiWindowClass windowClass;windowClass.ViewportFlagsOverrideSet=ImGuiViewportFlags_NoAutoMerge;ImGui::SetNextWindowClass(&windowClass);
    }else {
        ImGuiWindowClass normal;ImGui::SetNextWindowClass(&normal);
        if(ImGui::GetIO().ConfigFlags&ImGuiConfigFlags_ViewportsEnable)ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
        if(!m_ViewportDockPending)return;
        auto* root=ImGui::DockBuilderGetNode(ImHashStr("WhispEngine.Editor.Dockspace"));auto* target=ImGui::DockBuilderGetNode(m_ViewportReturnDock);
        if(!target&&root){target=root->CentralNode?root->CentralNode:root;while(target->ChildNodes[0])target=target->ChildNodes[0];}
        if(target)ImGui::SetNextWindowDockID(target->ID,ImGuiCond_Always);
        ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);m_ViewportDockPending=false;
    }
}
}
