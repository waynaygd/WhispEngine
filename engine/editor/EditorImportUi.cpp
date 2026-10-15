#define NOMINMAX
#include "EditorLayer.h"
#include "AssetImport.h"
#include "GltfSceneImport.h"
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
bool EditorLayer::RequestRecentScene(Application& app,const std::string& key){
 if(app.IsEditorPlayMode()){m_EditorError="Stop Play before opening a scene";return false;}
 if(IsSceneDirty()){m_SceneDialogKey=key;m_SceneDialogAction=4;m_SceneDialogRequested=true;return false;}
 const bool ok=OpenSceneGuarded(app,key,OpenDecision::Discard,&m_EditorError);if(ok)m_EditorError.clear();else {m_SceneDialogAction=5;m_SceneDialogRequested=true;Logger::Get().Error(m_EditorError);}return ok;
}
void EditorLayer::RememberScene(const std::string& key){
 if(!key.starts_with("scenes/")||!ecs::ComponentRegistry::ValidateAssetKey(key,ecs::AssetKind::None)||std::filesystem::u8path(key).extension()!=".json")return;
 std::erase(m_RecentScenes,key);m_RecentScenes.insert(m_RecentScenes.begin(),key);if(m_RecentScenes.size()>12)m_RecentScenes.resize(12);
}
void EditorLayer::DrawImportDialogs(Application& app){
 if(m_FolderPopup){ImGui::OpenPopup("New Folder");m_FolderPopup=false;}
 if(ImGui::BeginPopupModal("New Folder",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
  ImGui::Text("Parent: %s",m_FolderParent.empty()?"Project":m_FolderParent.c_str());ImGui::InputText("Folder name",&m_NewFolderName);
  if(ImGui::Button("Create folder")){if(AssetImport::NewFolder(AssetImport::ProjectRoot(),m_FolderParent,m_NewFolderName,&m_EditorError)){m_AssetListsDirty=true;NavigateAssetFolder(m_FolderParent.empty()?m_NewFolderName:m_FolderParent+"/"+m_NewFolderName);ImGui::CloseCurrentPopup();}}
  ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();if(!m_EditorError.empty())ImGui::TextWrapped("%s",m_EditorError.c_str());ImGui::EndPopup();
 }
 if(m_ImportPopup){ImGui::OpenPopup("Import Asset");m_ImportPopup=false;}
 if(ImGui::BeginPopupModal("Import Asset",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
  if(ImGui::InputText("Source file",&m_ImportSource)){m_ImportPreview.clear();if(m_ImportPackageName.empty())m_ImportPackageName=AssetPaths::ToUtf8String(std::filesystem::u8path(m_ImportSource).stem());}
#ifdef _WIN32
  ImGui::SameLine();if(ImGui::Button("Browse...")){
   wchar_t file[32768]{};OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.lpstrFile=file;dialog.nMaxFile=32768;dialog.lpstrFilter=L"Supported assets\0*.obj;*.gltf;*.glb;*.png;*.jpg;*.jpeg;*.dds;*.hdr\0All files\0*.*\0";dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
   if(GetOpenFileNameW(&dialog)){m_ImportSource=AssetPaths::ToUtf8String(std::filesystem::path(file));auto path=std::filesystem::path(file);auto ext=path.extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});auto folder=(ext==".hdr"?"environments/":ext==".obj"||ext==".gltf"||ext==".glb"?"models/":"textures/");m_ImportPackageName=AssetPaths::ToUtf8String(path.stem());if(m_ImportTargetFolder.empty())m_ImportTargetFolder=std::string(folder)+"imported";m_ImportRoot=AssetPaths::ToUtf8String(path.parent_path());m_ImportPreview.clear();}
  }
#endif
  if(ImGui::InputText("Dependency root (optional)",&m_ImportRoot))m_ImportPreview.clear();
  if(ImGui::IsItemHovered())ImGui::SetTooltip("Choose the common source folder when models reference ../textures or ../buffers. Only referenced files inside this root are copied.");
  auto extension=std::filesystem::u8path(m_ImportSource).extension().string();std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});const bool sceneModel=extension==".gltf"||extension==".glb";ImGui::BeginDisabled(!sceneModel);
  ImGui::Combo("Model import mode",&m_ModelImportMode,"Import as Single Mesh\0Import as Separate Mesh Assets\0Instantiate Imported Scene\0");ImGui::EndDisabled();
  ImGui::TextWrapped("Separate/scene modes: static glTF/GLB, selected scene; unsupported skin/animation/shear is diagnosed. Flat ECS entities, shared independent mesh assets.");
  if(ImGui::InputText("Target Folder",&m_ImportTargetFolder))m_ImportPreview.clear();
  if(ImGui::InputText("Package name",&m_ImportPackageName))m_ImportPreview.clear();
  m_ImportTarget=m_ImportTargetFolder+(m_ImportTargetFolder.empty()?"":"/")+m_ImportPackageName;
  ImGui::TextWrapped("Final package: %s",m_ImportTarget.c_str());
  const char* allowed=extension==".hdr"?"environments":extension==".obj"||sceneModel?"models":"textures";
  bool validDestination=m_ImportTargetFolder==allowed||m_ImportTargetFolder.starts_with(std::string(allowed)+"/");
  if(!validDestination){ImGui::TextWrapped("This asset requires %s/. Current folder is preserved; explicitly choose an allowed destination.",allowed);if(ImGui::Button("Use suggested destination")){m_ImportTargetFolder=std::string(allowed)+"/imported";m_ImportPreview.clear();}}
  if(m_ImportPackageName.empty())ImGui::TextDisabled("Enter a new package name; existing files are never overwritten.");
  ImGui::TextWrapped("Import preserves dependency paths inside a new package. Existing packages are protected: choose a new name on collision. Models: models/, images: textures/, HDR: environments/.");
  if(ImGui::Button("Scan dependencies")){ImportPlan plan;if(AssetImport::Scan(std::filesystem::u8path(m_ImportSource),m_ImportTarget,plan,&m_EditorError,std::filesystem::u8path(m_ImportRoot))){
   m_ImportPreview=plan.package+"/"+plan.entry+"\n"+std::to_string(plan.files.size())+" files, "+std::to_string(plan.bytes/1024)+" KiB";for(const auto& f:plan.files)m_ImportPreview+="\n  "+f.relative;m_EditorError.clear();}}
  if(!m_ImportPreview.empty())ImGui::TextWrapped("%s",m_ImportPreview.c_str());
  ImGui::BeginDisabled(app.IsEditorPlayMode()||m_ImportPreview.empty());if(ImGui::Button("Import (no overwrite)")){
   ImportPlan plan;if(AssetImport::Scan(std::filesystem::u8path(m_ImportSource),m_ImportTarget,plan,&m_EditorError,std::filesystem::u8path(m_ImportRoot))&&AssetImport::Publish(AssetImport::ProjectRoot(),plan,&m_EditorError,ModelImportMode(sceneModel?m_ModelImportMode:0))){
    m_SelectedAsset=plan.package+"/"+plan.entry;
    if(sceneModel&&m_ModelImportMode!=0){m_SelectedAsset+=".wscene";m_SelectedAssetKind="Imported Scene";if(m_ModelImportMode==2)InstantiateImportedScene(app,m_SelectedAsset);}NavigateAssetFolder(plan.package);m_AssetFilter.clear();m_AssetKindFilter=0;m_AssetListsDirty=true;
    if(auto* r=app.GetResourceManager()){if(plan.package.starts_with("models/")){if(!sceneModel||m_ModelImportMode==0){m_SelectedAssetKind="Mesh";r->LoadAsync<MeshResource>(m_SelectedAsset);}}else if(plan.package.starts_with("textures/")){m_SelectedAssetKind="Texture";r->LoadAsync<TextureResource>(m_SelectedAsset);}else{m_SelectedAssetKind="Environment";r->LoadAsync<EnvironmentResource>(m_SelectedAsset);}}
    ImGui::CloseCurrentPopup();
   }
  }ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();if(!m_EditorError.empty())ImGui::TextWrapped("%s",m_EditorError.c_str());ImGui::EndPopup();
 }
}
}
