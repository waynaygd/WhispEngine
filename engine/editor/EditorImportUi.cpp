#define NOMINMAX
#include "EditorLayer.h"
#include "AssetImport.h"
#include "GltfSceneImport.h"
#include "../core/Application.h"
#include "../core/AssetPaths.h"
#include "../resources/ResourceManager.h"
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <sstream>
#include <set>
#ifdef _WIN32
#include <Windows.h>
#include <commdlg.h>
#endif
namespace editor {
namespace {
std::vector<std::filesystem::path> ImportSources(const std::string& text){
 std::vector<std::filesystem::path> paths;std::istringstream input(text);std::string line;
 while(std::getline(input,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();if(!line.empty())paths.push_back(std::filesystem::u8path(line));}return paths;
}
}

bool EditorLayer::OpenSceneGuarded(Application& app,const std::string& key,OpenDecision decision,std::string* error){
 if(decision==OpenDecision::Cancel)return false;
 if(IsSceneDirty()&&decision==OpenDecision::Save&&!app.SaveCurrentScene(error))return false;
 if(!app.OpenScene(key,error))return false;
 return OpenDocument(app,key);
}
bool EditorLayer::RequestRecentScene(Application& app,const std::string& key){
 if(app.IsEditorPlayMode()){m_EditorError="Stop Play before opening a scene";return false;}
 if(IsSceneDirty()){m_SceneDialogKey=key;m_SceneDialogAction=4;m_SceneDialogRequested=true;return false;}
 const bool ok=OpenSceneGuarded(app,key,OpenDecision::Discard,&m_EditorError);if(ok)m_EditorError.clear();else {m_SceneDialogAction=5;m_SceneDialogRequested=true;Logger::Get().Error(m_EditorError);}return ok;
}
void EditorLayer::RememberScene(const std::string& key){
 const auto project=ProjectManager::Active();const auto prefix=project?project->sceneDirectory+"/":"scenes/";
 if(!key.starts_with(prefix)||!ecs::ComponentRegistry::ValidateAssetKey(key,ecs::AssetKind::None)||std::filesystem::u8path(key).extension()!=".json")return;
 std::erase(m_RecentScenes,key);m_RecentScenes.insert(m_RecentScenes.begin(),key);if(m_RecentScenes.size()>12)m_RecentScenes.resize(12);
}
void EditorLayer::DrawImportDialogs(Application& app){
 if(m_FolderPopup){ImGui::OpenPopup("New Folder");m_FolderPopup=false;}
 if(ImGui::BeginPopupModal("New Folder",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
  ImGui::Text("Parent: %s",m_FolderParent.empty()?"Project":m_FolderParent.c_str());ImGui::InputText("Folder name",&m_NewFolderName);
  ImGui::BeginDisabled(app.IsEditorPlayMode());
  if(ImGui::Button("Create folder")){if(AssetImport::NewFolder(AssetPaths::ResolveAssetRoot(),m_FolderParent,m_NewFolderName,&m_EditorError)){m_AssetListsDirty=true;NavigateAssetFolder(m_FolderParent.empty()?m_NewFolderName:m_FolderParent+"/"+m_NewFolderName);ImGui::CloseCurrentPopup();}}
  ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();if(!m_EditorError.empty())ImGui::TextWrapped("%s",m_EditorError.c_str());ImGui::EndPopup();
 }
 if(m_ImportPopup){ImGui::OpenPopup("Import Asset");m_ImportPopup=false;}
 if(ImGui::BeginPopupModal("Import Asset",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
  if(ImGui::InputTextMultiline("Source files (one per line)",&m_ImportSource,{460,65})){m_ImportPreview.clear();if(m_ImportPackageName.empty())m_ImportPackageName=AssetPaths::ToUtf8String(std::filesystem::u8path(m_ImportSource).stem());}
#ifdef _WIN32
  ImGui::SameLine();if(ImGui::Button("Browse...")){
   wchar_t file[32768]{};OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.lpstrFile=file;dialog.nMaxFile=32768;dialog.lpstrFilter=L"Supported assets\0*.obj;*.gltf;*.glb;*.png;*.jpg;*.jpeg;*.dds;*.hdr\0All files\0*.*\0";dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_EXPLORER|OFN_ALLOWMULTISELECT;
   if(GetOpenFileNameW(&dialog)){auto path=std::filesystem::path(file);m_ImportSource.clear();const wchar_t* next=file+wcslen(file)+1;if(*next){for(;*next;next+=wcslen(next)+1){if(!m_ImportSource.empty())m_ImportSource+="\n";m_ImportSource+=AssetPaths::ToUtf8String(path/next);}path=path/(file+wcslen(file)+1);}else m_ImportSource=AssetPaths::ToUtf8String(path);auto ext=path.extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});auto folder=(ext==".hdr"?"environments/":ext==".obj"||ext==".gltf"||ext==".glb"?"models/":"textures/");m_ImportPackageName=AssetPaths::ToUtf8String(path.stem());if(m_ImportTargetFolder.empty())m_ImportTargetFolder=std::string(folder)+"imported";m_ImportRoot=AssetPaths::ToUtf8String(path.parent_path());m_ImportPreview.clear();}
  }
#endif
  if(ImGui::InputText("Dependency root (optional)",&m_ImportRoot))m_ImportPreview.clear();
  if(ImGui::IsItemHovered())ImGui::SetTooltip("Choose the common source folder when models reference ../textures or ../buffers. Only referenced files inside this root are copied.");
  const auto sources=ImportSources(m_ImportSource);const auto first=sources.empty()?std::filesystem::path{}:sources.front();auto extension=first.extension().string();std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});const bool standaloneImage=extension==".png"||extension==".jpg"||extension==".jpeg"||extension==".dds";const bool sceneModel=extension==".gltf"||extension==".glb";ImGui::BeginDisabled(!sceneModel);
  ImGui::Combo("Model import mode",&m_ModelImportMode,"Import as Single Mesh\0Import as Separate Mesh Assets\0Instantiate Imported Scene\0");ImGui::EndDisabled();
  ImGui::TextWrapped("Separate/scene modes: static glTF/GLB, selected scene; unsupported skin/animation/shear is diagnosed. Flat ECS entities, shared independent mesh assets.");
  if(ImGui::InputText("Target Folder",&m_ImportTargetFolder))m_ImportPreview.clear();
  if(!standaloneImage&&ImGui::InputText("Package name",&m_ImportPackageName))m_ImportPreview.clear();
  m_ImportTarget=m_ImportTargetFolder+(m_ImportTargetFolder.empty()?"":"/")+(standaloneImage?AssetPaths::ToUtf8String(first.filename()):m_ImportPackageName);
  ImGui::TextWrapped(standaloneImage?"Destination file: %s":"Final package: %s",m_ImportTarget.c_str());
  const char* allowed=extension==".hdr"?"environments":extension==".obj"||sceneModel?"models":"textures";
  bool validDestination=m_ImportTargetFolder==allowed||m_ImportTargetFolder.starts_with(std::string(allowed)+"/");
  if(!validDestination){ImGui::TextWrapped("This asset requires %s/. Current folder is preserved; explicitly choose an allowed destination.",allowed);if(ImGui::Button("Use suggested destination")){m_ImportTargetFolder=std::string(allowed)+"/imported";m_ImportPreview.clear();}}
  if(!standaloneImage&&m_ImportPackageName.empty())ImGui::TextDisabled("Enter a new package name; existing files are never overwritten.");
  if(standaloneImage)ImGui::TextWrapped("Textures import directly into Target Folder with their original filename. Existing files are never overwritten.");else ImGui::TextWrapped("Import preserves dependency paths inside a new package. Existing packages are protected: choose a new name on collision. Models: models/, images: textures/, HDR: environments/.");
  auto plans=[&](){
   std::vector<ImportPlan> result;std::set<std::string> keys;
   if(sources.empty())throw std::runtime_error("Choose at least one source file");
   for(const auto& source:sources){
    if(sources.size()>1&&!standaloneImage)throw std::runtime_error("Batch import supports standalone textures; import a model with its dependencies as one package");
    ImportPlan plan;const auto target=standaloneImage?m_ImportTargetFolder+"/"+AssetPaths::ToUtf8String(source.filename()):m_ImportTarget;
    if(!AssetImport::Scan(source,target,plan,&m_EditorError,std::filesystem::u8path(m_ImportRoot),standaloneImage))throw std::runtime_error(m_EditorError);
    auto folded=target;std::transform(folded.begin(),folded.end(),folded.begin(),[](unsigned char c){return char(std::tolower(c));});
    if(!keys.insert(folded).second)throw std::runtime_error("Batch contains duplicate destination filenames: "+target);
    if(std::filesystem::exists(AssetImport::SafeTarget(AssetImport::ProjectRoot(),target)))throw std::runtime_error("Destination exists; nothing overwritten: "+target);
    result.push_back(std::move(plan));
   }return result;
  };
  if(ImGui::Button("Scan dependencies"))try{auto scanned=plans();m_ImportPreview.clear();for(const auto& plan:scanned){m_ImportPreview+=(plan.standaloneImage?plan.package:plan.package+"/"+plan.entry)+"\n"+std::to_string(plan.files.size())+" files, "+std::to_string(plan.bytes/1024)+" KiB\n";}m_EditorError.clear();}catch(const std::exception& e){m_ImportPreview.clear();m_EditorError=e.what();}
  if(!m_ImportPreview.empty())ImGui::TextWrapped("%s",m_ImportPreview.c_str());
  ImGui::BeginDisabled(app.IsEditorPlayMode()||m_ImportPreview.empty());if(ImGui::Button("Import (no overwrite)"))try{
   const auto scanned=plans();for(const auto& plan:scanned){
    if(!AssetImport::Publish(AssetImport::ProjectRoot(),plan,&m_EditorError,ModelImportMode(sceneModel?m_ModelImportMode:0)))throw std::runtime_error(m_EditorError);
    m_SelectedAsset=plan.standaloneImage?plan.package:plan.package+"/"+plan.entry;
    if(sceneModel&&m_ModelImportMode!=0){m_SelectedAsset+=".wscene";m_SelectedAssetKind="Imported Scene";if(m_ModelImportMode==2)InstantiateImportedScene(app,m_SelectedAsset);}
    NavigateAssetFolder(plan.standaloneImage?m_ImportTargetFolder:plan.package);m_AssetFilter.clear();m_AssetKindFilter=0;m_AssetListsDirty=true;
    if(auto* r=app.GetResourceManager()){if(plan.package.starts_with("models/")){if(!sceneModel||m_ModelImportMode==0){m_SelectedAssetKind="Mesh";r->LoadAsync<MeshResource>(m_SelectedAsset);}}else if(plan.package.starts_with("textures/")){m_SelectedAssetKind="Texture";r->LoadAsync<TextureResource>(m_SelectedAsset);}else{m_SelectedAssetKind="Environment";r->LoadAsync<EnvironmentResource>(m_SelectedAsset);}}
   }ImGui::CloseCurrentPopup();
  }catch(const std::exception& e){m_AssetListsDirty=true;m_EditorError=e.what();}ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();if(!m_EditorError.empty())ImGui::TextWrapped("%s",m_EditorError.c_str());ImGui::EndPopup();
 }
}
}
