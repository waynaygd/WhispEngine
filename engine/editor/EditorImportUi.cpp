#define NOMINMAX
#include "EditorLayer.h"
#include "AssetImport.h"
#include "../core/Application.h"
#include "../core/AssetPaths.h"
#include "../resources/ResourceManager.h"
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#ifdef _WIN32
#include <Windows.h>
#include <commdlg.h>
#endif
namespace editor {
bool EditorLayer::OpenSceneGuarded(Application& app,const std::string& key,OpenDecision decision,std::string* error){
 if(decision==OpenDecision::Cancel)return false;
 if(IsSceneDirty()&&decision==OpenDecision::Save&&!app.SaveCurrentScene(error))return false;
 return app.OpenScene(key,error);
}
void EditorLayer::RememberScene(const std::string& key){
 if(!key.starts_with("scenes/")||!ecs::ComponentRegistry::ValidateAssetKey(key,ecs::AssetKind::None)||std::filesystem::u8path(key).extension()!=".json")return;
 std::erase(m_RecentScenes,key);m_RecentScenes.insert(m_RecentScenes.begin(),key);if(m_RecentScenes.size()>12)m_RecentScenes.resize(12);
}
void EditorLayer::DrawImportDialogs(Application& app){
 if(m_FolderPopup){ImGui::OpenPopup("New Folder");m_FolderPopup=false;}
 if(ImGui::BeginPopupModal("New Folder",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
  ImGui::Text("Parent: %s",m_FolderParent.empty()?"Project":m_FolderParent.c_str());ImGui::InputText("Folder name",&m_NewFolderName);
  if(ImGui::Button("Create folder")){if(AssetImport::NewFolder(AssetImport::ProjectRoot(),m_FolderParent,m_NewFolderName,&m_EditorError)){m_AssetListsDirty=true;m_AssetDirectory=m_FolderParent.empty()?m_NewFolderName:m_FolderParent+"/"+m_NewFolderName;ImGui::CloseCurrentPopup();}}
  ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();if(!m_EditorError.empty())ImGui::TextWrapped("%s",m_EditorError.c_str());ImGui::EndPopup();
 }
 if(m_ImportPopup){ImGui::OpenPopup("Import Asset");m_ImportPopup=false;}
 if(ImGui::BeginPopupModal("Import Asset",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
  if(ImGui::InputText("Source file",&m_ImportSource))m_ImportPreview.clear();
#ifdef _WIN32
  ImGui::SameLine();if(ImGui::Button("Browse...")){
   wchar_t file[32768]{};OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.lpstrFile=file;dialog.nMaxFile=32768;dialog.lpstrFilter=L"Supported assets\0*.obj;*.gltf;*.glb;*.png;*.jpg;*.jpeg;*.dds;*.hdr\0All files\0*.*\0";dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
   if(GetOpenFileNameW(&dialog)){m_ImportSource=AssetPaths::ToUtf8String(std::filesystem::path(file));auto path=std::filesystem::path(file);auto ext=path.extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});auto folder=(ext==".hdr"?"environments/":ext==".obj"||ext==".gltf"||ext==".glb"?"models/":"textures/");m_ImportTarget=std::string(folder)+"imported/"+AssetPaths::ToUtf8String(path.stem());m_ImportRoot=AssetPaths::ToUtf8String(path.parent_path());m_ImportPreview.clear();}
  }
#endif
  if(ImGui::InputText("Dependency root (optional)",&m_ImportRoot))m_ImportPreview.clear();
  if(ImGui::IsItemHovered())ImGui::SetTooltip("Choose the common source folder when models reference ../textures or ../buffers. Only referenced files inside this root are copied.");
  if(ImGui::InputText("Target package / name",&m_ImportTarget))m_ImportPreview.clear();
  ImGui::TextWrapped("Import preserves dependency paths inside a new package. Existing packages are protected: choose a new name on collision. Models: models/, images: textures/, HDR: environments/.");
  if(ImGui::Button("Scan dependencies")){ImportPlan plan;if(AssetImport::Scan(std::filesystem::u8path(m_ImportSource),m_ImportTarget,plan,&m_EditorError,std::filesystem::u8path(m_ImportRoot))){
   m_ImportPreview=plan.package+"/"+plan.entry+"\n"+std::to_string(plan.files.size())+" files, "+std::to_string(plan.bytes/1024)+" KiB";for(const auto& f:plan.files)m_ImportPreview+="\n  "+f.relative;m_EditorError.clear();}}
  if(!m_ImportPreview.empty())ImGui::TextWrapped("%s",m_ImportPreview.c_str());
  ImGui::BeginDisabled(app.IsEditorPlayMode()||m_ImportPreview.empty());if(ImGui::Button("Import (no overwrite)")){
   ImportPlan plan;if(AssetImport::Scan(std::filesystem::u8path(m_ImportSource),m_ImportTarget,plan,&m_EditorError,std::filesystem::u8path(m_ImportRoot))&&AssetImport::Publish(AssetImport::ProjectRoot(),plan,&m_EditorError)){
    m_SelectedAsset=plan.package+"/"+plan.entry;m_AssetDirectory=plan.package;m_AssetFilter.clear();m_AssetKindFilter=0;m_AssetListsDirty=true;
    if(auto* r=app.GetResourceManager()){if(plan.package.starts_with("models/")){m_SelectedAssetKind="Mesh";r->LoadAsync<MeshResource>(m_SelectedAsset);}else if(plan.package.starts_with("textures/")){m_SelectedAssetKind="Texture";r->LoadAsync<TextureResource>(m_SelectedAsset);}else{m_SelectedAssetKind="Environment";r->LoadAsync<EnvironmentResource>(m_SelectedAsset);}}
    ImGui::CloseCurrentPopup();
   }
  }ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();if(!m_EditorError.empty())ImGui::TextWrapped("%s",m_EditorError.c_str());ImGui::EndPopup();
 }
}
}
