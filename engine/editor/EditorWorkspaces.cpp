#include "EditorLayer.h"
#include "../core/Application.h"
#include "../core/AssetPaths.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <fstream>
#include <sstream>
#include <set>
#include <cstdio>
#include <cstring>
namespace editor {
namespace {
ImGuiID WorkspaceRoot(){return ImHashStr("WhispEngine.Editor.Dockspace");}
std::array<bool*,17> Panels(bool& hierarchy,bool& inspector,bool& viewport,bool& assets,bool& console,bool& material,bool& settings,bool& statistics,bool& events,bool& validation,bool& lua,bool& visuals,bool& physics,bool& project,bool& widgetHierarchy,bool& canvas,bool& widgetInspector){
    return {&hierarchy,&inspector,&viewport,&assets,&console,&material,&settings,&statistics,&events,&validation,&lua,&visuals,&physics,&project,&widgetHierarchy,&canvas,&widgetInspector};
}
}
bool ValidWorkspaceIni(const std::string& ini){
    if(ini.empty()||ini.size()>1024*1024||ini.find('\0')!=std::string::npos)return false;
    struct Node {unsigned parent=0,children=0;bool split=false,space=false,central=false;};
    std::map<unsigned,Node> nodes;std::vector<unsigned> docks;bool docking=false,window=false,viewport=false;
    std::istringstream input(ini);std::string line;
    while(std::getline(input,line)){
        if(line.size()>4096)return false;
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.starts_with("[")){
            docking=line=="[Docking][Data]";window=line.starts_with("[Window][");
            if(window&&line=="[Window][Viewport]")viewport=true;
            if(!docking&&!window&&!line.starts_with("[Table]["))return false;
            continue;
        }
        auto first=line.find_first_not_of(" \t");if(first==std::string::npos)continue;auto text=line.substr(first);
        for(const auto* key:{"Pos=","Size=","SizeRef="})if(auto p=text.find(key);p!=std::string::npos){int x=0,y=0;
            if(std::sscanf(text.c_str()+p+std::strlen(key),"%d,%d",&x,&y)!=2||x<-30000||x>30000||y<-30000||y>30000)return false;
            if(key[0]=='S'&&(x<0||y<0))return false;
        }
        if(window&&text.starts_with("DockId=")){unsigned id=0;if(std::sscanf(text.c_str(),"DockId=0x%X",&id)!=1)return false;if(id)docks.push_back(id);}
        if(docking){
            const bool space=text.starts_with("DockSpace");if(!space&&!text.starts_with("DockNode"))return false;
            const auto at=text.find("ID=0x");unsigned id=0,parent=0;
            if(at==std::string::npos||std::sscanf(text.c_str()+at,"ID=0x%X",&id)!=1||!id||nodes.contains(id)||nodes.size()>=256)return false;
            if(auto p=text.find("Parent=0x");p!=std::string::npos){if(std::sscanf(text.c_str()+p,"Parent=0x%X",&parent)!=1||!nodes.contains(parent)||space)return false;if(++nodes[parent].children>2)return false;}
            auto split=text.find("Split=");if(split!=std::string::npos&&(split+6>=text.size()||(text[split+6]!='X'&&text[split+6]!='Y')))return false;
            nodes.emplace(id,Node{parent,0,split!=std::string::npos,space,text.find("CentralNode=1")!=std::string::npos});
        }
    }
    const auto root=nodes.find(WorkspaceRoot());if(!viewport&&ini.find("[Window][Canvas Designer]")==std::string::npos)return false;
    if(root==nodes.end()||!root->second.space||root->second.parent)return false;
    std::map<unsigned,unsigned> centers;
    for(const auto& [id,node]:nodes){if(node.central&&node.children)return false;if((node.children!=0&&node.children!=2)||(node.children==2&&!node.split)||(node.split&&node.children!=2))return false;unsigned n=id,depth=0;while(nodes.at(n).parent){if(++depth>32)return false;n=nodes.at(n).parent;}if(node.central&&++centers[n]>1)return false;}
    // ImGui keeps hidden windows attached to a split ancestor after merging an empty leaf.
    for(auto id:docks)if(!nodes.contains(id))return false;
    return true;
}
std::map<std::string,bool> EditorLayer::CaptureWorkspaceVisibility(){
    auto fields=Panels(m_ShowHierarchy,m_ShowInspector,m_ShowViewport,m_ShowAssetBrowser,m_ShowConsole,m_ShowMaterialEditor,m_ShowSceneSettings,m_ShowStatistics,m_ShowGameplayEvents,m_ShowSceneValidation,m_ShowLuaRuntimeInspector,m_ShowVisualSettings,m_ShowPhysicsStress,m_ShowProjectSettings,m_ShowWidgetHierarchy,m_ShowCanvasDesigner,m_ShowWidgetInspector);
    std::map<std::string,bool> result;for(std::size_t i=0;i<fields.size();++i)result[WorkspacePanels[i]]=*fields[i];return result;
}
void EditorLayer::ApplyWorkspaceVisibility(const std::map<std::string,bool>& values){
    auto fields=Panels(m_ShowHierarchy,m_ShowInspector,m_ShowViewport,m_ShowAssetBrowser,m_ShowConsole,m_ShowMaterialEditor,m_ShowSceneSettings,m_ShowStatistics,m_ShowGameplayEvents,m_ShowSceneValidation,m_ShowLuaRuntimeInspector,m_ShowVisualSettings,m_ShowPhysicsStress,m_ShowProjectSettings,m_ShowWidgetHierarchy,m_ShowCanvasDesigner,m_ShowWidgetInspector);
    for(std::size_t i=0;i<fields.size();++i)if(auto found=values.find(WorkspacePanels[i]);found!=values.end())*fields[i]=found->second;
}
void EditorLayer::WorkspaceDefaults(){
    std::map<std::string,bool> visible;for(const auto* panel:WorkspacePanels)visible[panel]=false;
    for(const auto* panel:{"Viewport","Scene Hierarchy","Inspector"})visible[panel]=true;
    if(m_Workspace==EditorWorkspace::LevelEditing){visible["Asset Browser"]=visible["Console / Diagnostics"]=true;}
    if(m_Workspace==EditorWorkspace::Scripting){visible["Console / Diagnostics"]=visible["Lua Runtime Inspector"]=visible["Gameplay Events"]=true;}
    if(m_Workspace==EditorWorkspace::AssetManagement){visible["Asset Browser"]=true;}
    if(m_Workspace==EditorWorkspace::GameplayDebugging){visible["Gameplay Events"]=visible["Lua Runtime Inspector"]=visible["Scene Validation"]=visible["Console / Diagnostics"]=true;}
    if(m_Workspace==EditorWorkspace::UIDesigner){for(auto& [name,value]:visible)value=false;for(const auto* name:{"Widget Hierarchy","Canvas Designer","Widget Inspector","Asset Browser"})visible[name]=true;}
    ApplyWorkspaceVisibility(visible);
}
void EditorLayer::InitializeWorkspaces(const EditorPreferences& preferences){
    m_Workspaces=preferences.workspaces;m_Workspace=m_RequestedWorkspace=preferences.activeWorkspace;m_WorkspacePending=true;
    if(ImGui::GetCurrentContext())ImGui::GetIO().IniFilename=nullptr;
    if(!preferences.workspacesInitialized){
        const auto path=AssetPaths::GetExecutableDirectory()/"imgui.ini";std::error_code error;
        if(std::filesystem::is_regular_file(path,error)&&std::filesystem::file_size(path,error)<=1024*1024){std::ifstream input(path,std::ios::binary);std::string ini((std::istreambuf_iterator<char>(input)),{});
            if(ValidWorkspaceIni(ini))m_Workspaces["level"]={std::move(ini),CaptureWorkspaceVisibility()};
        }
    }
    WorkspaceDefaults();
    if(auto saved=m_Workspaces.find(WorkspaceKeys[int(m_Workspace)]);saved!=m_Workspaces.end())ApplyWorkspaceVisibility(saved->second.visible);
}
bool EditorLayer::CaptureCurrentWorkspace(){
    if(!ImGui::GetCurrentContext()||m_WorkspacePending||m_ResetLayout||m_ViewportMaximized||m_FixedDiagnosticLayout||!ImGui::DockBuilderGetNode(WorkspaceRoot()))return false;
    std::size_t size=0;const auto* data=ImGui::SaveIniSettingsToMemory(&size);std::string ini(data,size);
    if(!ValidWorkspaceIni(ini)){m_EditorError="The current layout cannot be saved; restore this workspace's default layout.";return false;}
    m_Workspaces[WorkspaceKeys[int(m_Workspace)]]={std::move(ini),CaptureWorkspaceVisibility()};ImGui::GetIO().WantSaveIniSettings=false;return true;
}
bool EditorLayer::SaveCurrentWorkspace(Application& app){
    if(!CaptureCurrentWorkspace()){if(m_ViewportMaximized)m_EditorError="Return from F11 fullscreen before saving the normal workspace layout.";return false;}
    return PersistPreferences(app);
}
bool EditorLayer::ResetCurrentWorkspace(Application& app){
    m_Workspaces.erase(WorkspaceKeys[int(m_Workspace)]);m_RequestedWorkspace=m_Workspace;m_WorkspacePending=true;m_WorkspaceFirstApply=false;return PersistPreferences(app);
}
void EditorLayer::BuildWorkspaceDocking(unsigned root,const ImVec2& size){
    ImGui::DockBuilderRemoveNode(root);ImGui::DockBuilderAddNode(root,ImGuiDockNodeFlags_DockSpace);ImGui::DockBuilderSetNodeSize(root,size);
    ImGuiID center=root,left=0,right=0,bottom=0;
    if(m_Workspace==EditorWorkspace::AssetManagement){
        ImGui::DockBuilderSplitNode(center,ImGuiDir_Right,.28f,&right,&center);
        ImGui::DockBuilderSplitNode(right,ImGuiDir_Up,.42f,&left,&right);
        bottom=center; // Assets are the primary region; the viewport occupies the upper right.
        ImGui::DockBuilderDockWindow("Viewport",left);ImGui::DockBuilderDockWindow("Asset Browser",center);
        ImGui::DockBuilderDockWindow("Inspector",right);ImGui::DockBuilderDockWindow("Scene Hierarchy",right);
    }else{
        ImGui::DockBuilderSplitNode(center,ImGuiDir_Left,.18f,&left,&center);
        ImGui::DockBuilderSplitNode(center,ImGuiDir_Right,.28f,&right,&center);
        ImGui::DockBuilderSplitNode(center,ImGuiDir_Down,m_Workspace==EditorWorkspace::LevelEditing?.25f:.32f,&bottom,&center);
        ImGui::DockBuilderDockWindow("Viewport",center);ImGui::DockBuilderDockWindow("Scene Hierarchy",left);ImGui::DockBuilderDockWindow("Inspector",right);
        ImGui::DockBuilderDockWindow("Widget Hierarchy",left);ImGui::DockBuilderDockWindow("Canvas Designer",center);ImGui::DockBuilderDockWindow("Widget Inspector",right);ImGui::DockBuilderDockWindow("Asset Browser",bottom);
    }
    for(const auto* name:{"Scene Settings","Editor Visuals","Lua Runtime Inspector","Project Settings"})ImGui::DockBuilderDockWindow(name,right);
    for(const auto* name:{"Console / Diagnostics","Gameplay Events","Scene Validation","Material Editor","Statistics","Physics Stress Test"})ImGui::DockBuilderDockWindow(name,bottom);
    ImGui::DockBuilderFinish(root);
    if(m_Workspace==EditorWorkspace::Scripting||m_Workspace==EditorWorkspace::GameplayDebugging){
        if(auto* node=ImGui::DockBuilderGetNode(right))node->SelectedTabId=ImHashStr("Lua Runtime Inspector");
        if(auto* node=ImGui::DockBuilderGetNode(bottom))node->SelectedTabId=ImHashStr(m_Workspace==EditorWorkspace::Scripting?"Console / Diagnostics":"Gameplay Events");
    }else if(auto* node=ImGui::DockBuilderGetNode(bottom))node->SelectedTabId=ImHashStr("Asset Browser");
}
void EditorLayer::ConfigureDocking(){
    auto* viewport=ImGui::GetMainViewport();ImGuiViewport dock=*viewport;
    // This is a geometry-only copy for the dockspace, not a backend owner.
    dock.PlatformUserData=nullptr;dock.RendererUserData=nullptr;const float toolbar=ImGui::GetFrameHeightWithSpacing()+8;
    dock.WorkPos.y+=toolbar;dock.WorkSize.y=std::max(1.f,dock.WorkSize.y-toolbar);const auto root=WorkspaceRoot();
    if(m_FixedDiagnosticLayout){ImGui::DockSpaceOverViewport(root,&dock,ImGuiDockNodeFlags_PassthruCentralNode);return;}
    if(m_WorkspacePending){
        m_Workspace=m_RequestedWorkspace;if(!m_WorkspaceFirstApply)WorkspaceDefaults();ImGui::ClearIniSettings();
        bool loaded=false;
        if(auto saved=m_Workspaces.find(WorkspaceKeys[int(m_Workspace)]);saved!=m_Workspaces.end()&&ValidWorkspaceIni(saved->second.ini)){
            ImGui::LoadIniSettingsFromMemory(saved->second.ini.data(),saved->second.ini.size());if(!m_WorkspaceFirstApply)ApplyWorkspaceVisibility(saved->second.visible);loaded=ImGui::DockBuilderGetNode(root)!=nullptr;
            // Keep floating panels reachable after loading on a smaller display.
            for(auto* window:ImGui::GetCurrentContext()->Windows)if(!window->DockId&&!(window->Flags&ImGuiWindowFlags_NoSavedSettings)){
                ImVec2 size{std::clamp(window->SizeFull.x,80.f,std::max(80.f,dock.WorkSize.x)),std::clamp(window->SizeFull.y,60.f,std::max(60.f,dock.WorkSize.y))};
                ImGui::SetWindowSize(window,size);ImGui::SetWindowPos(window,{std::clamp(window->Pos.x,dock.WorkPos.x,dock.WorkPos.x+std::max(0.f,dock.WorkSize.x-size.x)),std::clamp(window->Pos.y,dock.WorkPos.y,dock.WorkPos.y+std::max(0.f,dock.WorkSize.y-size.y))});
            }
            // Panels introduced by an update get an ordinary docking slot.
            for(const auto* name:WorkspacePanels)if(saved->second.ini.find(std::string("[Window][")+name+"]")==std::string::npos){auto* node=ImGui::DockBuilderGetNode(root);if(node){auto* leaf=node->CentralNode?node->CentralNode:node;while(leaf->ChildNodes[0])leaf=leaf->ChildNodes[0];ImGui::DockBuilderDockWindow(name,leaf->ID);}}
        }
        m_WorkspacePending=false;m_WorkspaceFirstApply=false;m_ResetLayout=!loaded;
    }
    if(m_ResetLayout||!ImGui::DockBuilderGetNode(root)){BuildWorkspaceDocking(root,dock.WorkSize);m_ResetLayout=false;}
    ImGui::DockSpaceOverViewport(root,&dock,ImGuiDockNodeFlags_PassthruCentralNode);
}
void EditorLayer::DrawWorkspaceMenu(Application& app){
    if(!ImGui::BeginMenu("Workspaces"))return;
    for(int i=0;i<int(WorkspaceKeys.size());++i)if(ImGui::MenuItem(WorkspaceNames[i],nullptr,m_Workspace==EditorWorkspace(i)))SwitchWorkspace(EditorWorkspace(i));
    ImGui::Separator();ImGui::BeginDisabled(m_WorkspacePending||m_ViewportMaximized);
    if(ImGui::MenuItem("Save Current Layout"))SaveCurrentWorkspace(app);ImGui::EndDisabled();
    if(ImGui::MenuItem("Reset Current Layout"))ResetCurrentWorkspace(app);
    ImGui::EndMenu();
}
}
