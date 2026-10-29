#include "EditorTheme.h"
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
  ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();if(!m_EditorError.empty())ErrorText(m_EditorError.c_str());ImGui::EndPopup();
 }
 if(m_ImportPopup){ImGui::OpenPopup("Import Asset");m_ImportPopup=false;}
 if(ImGui::BeginPopupModal("Import Asset",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
  if(ImGui::InputTextMultiline("Source files (one per line)",&m_ImportSource,{460,65})){m_ImportPreview.clear();if(m_ImportPackageName.empty())m_ImportPackageName=AssetPaths::ToUtf8String(std::filesystem::u8path(m_ImportSource).stem());}
#ifdef _WIN32
  ImGui::SameLine();if(ImGui::Button("Browse...")){
   wchar_t file[32768]{};OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.lpstrFile=file;dialog.nMaxFile=32768;dialog.lpstrFilter=L"Supported assets\0*.obj;*.gltf;*.glb;*.png;*.jpg;*.jpeg;*.dds;*.hdr;*.ttf;*.otf\0All files\0*.*\0";dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_EXPLORER|OFN_ALLOWMULTISELECT;
   if(GetOpenFileNameW(&dialog)){auto path=std::filesystem::path(file);m_ImportSource.clear();const wchar_t* next=file+wcslen(file)+1;if(*next){for(;*next;next+=wcslen(next)+1){if(!m_ImportSource.empty())m_ImportSource+="\n";m_ImportSource+=AssetPaths::ToUtf8String(path/next);}path=path/(file+wcslen(file)+1);}else m_ImportSource=AssetPaths::ToUtf8String(path);auto ext=path.extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});auto folder=((ext==".ttf"||ext==".otf")?"fonts/":ext==".hdr"?"environments/":ext==".obj"||ext==".gltf"||ext==".glb"?"models/":"textures/");m_ImportPackageName=AssetPaths::ToUtf8String(path.stem());if(m_ImportTargetFolder.empty())m_ImportTargetFolder=std::string(folder)+"imported";m_ImportRoot=AssetPaths::ToUtf8String(path.parent_path());m_ImportPreview.clear();}
  }
#endif
  const auto sources=ImportSources(m_ImportSource);
  auto extOf=[](const std::filesystem::path& p){auto e=p.extension().string();for(auto& c:e)c=char(std::tolower(static_cast<unsigned char>(c)));return e;};
  const bool hasGltf=std::any_of(sources.begin(),sources.end(),[&](const auto& p){auto e=extOf(p);return e==".gltf"||e==".glb";});
  const bool hasPackage=std::any_of(sources.begin(),sources.end(),[&](const auto& p){auto e=extOf(p);return e==".obj"||e==".gltf"||e==".glb"||e==".hdr";});
  if(hasPackage){
    if(ImGui::InputText("Dependency root (optional)",&m_ImportRoot))m_ImportPreview.clear();
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Choose the common source folder when models reference ../textures or ../buffers. Only referenced files inside this root are copied.");
  }
  if(hasGltf)ImGui::Combo("Model import mode",&m_ModelImportMode,"Import as Single Mesh\0Import as Separate Mesh Assets\0Instantiate Imported Scene\0");
  if(ImGui::InputText("Target Folder",&m_ImportTargetFolder))m_ImportPreview.clear();
  if(sources.size()==1&&hasPackage&&ImGui::InputText("Package name (optional)",&m_ImportPackageName))m_ImportPreview.clear();
  ImGui::TextWrapped("Destination is relative to Assets. Images/fonts keep their filename. Models/HDR use one package per source, preserving dependency paths. Audio has no importer yet.");
  auto scan=[&](const std::filesystem::path& source){
    auto ext=extOf(source);const bool single=ext==".ttf"||ext==".otf"||ext==".png"||ext==".jpg"||ext==".jpeg"||ext==".dds";
    const auto name=single?AssetPaths::ToUtf8String(source.filename()):sources.size()==1&&!m_ImportPackageName.empty()?m_ImportPackageName:AssetPaths::ToUtf8String(source.stem());
    const auto target=(m_ImportTargetFolder.empty()?"":m_ImportTargetFolder+"/")+name;
    ImportPlan plan;std::string error;
    if(!AssetImport::Scan(source,target,plan,&error,std::filesystem::u8path(m_ImportRoot),single))throw std::runtime_error(error);
    if(std::filesystem::exists(AssetImport::SafeTarget(AssetImport::ProjectRoot(),target)))throw std::runtime_error("Destination exists; nothing overwritten: "+target);
    return plan;
  };
  if(ImGui::Button("Scan dependencies")){
    m_ImportPreview.clear();
    for(const auto& source:sources)try{
      auto plan=scan(source);m_ImportPreview+=plan.package+" ("+std::to_string(plan.bytes)+" bytes)\n";
      for(const auto& file:plan.files)m_ImportPreview+="  "+file.relative+"\n";
    }catch(const std::exception& e){m_ImportPreview+=AssetPaths::ToUtf8String(source)+": "+e.what()+"\n";}
  }
  if(!m_ImportPreview.empty()){ImGui::BeginChild("Package preview",{620,130},ImGuiChildFlags_Borders);ImGui::TextUnformatted(m_ImportPreview.c_str());ImGui::EndChild();}
  auto publish=[&](const std::vector<std::filesystem::path>& items){
    m_ImportResults.clear();m_ImportFailed.clear();m_EditorError.clear();
    for(const auto& source:items){const auto name=AssetPaths::ToUtf8String(source);try{
      auto plan=scan(source);auto ext=extOf(source);const bool gltf=ext==".gltf"||ext==".glb";
      std::string error;if(!AssetImport::Publish(AssetImport::ProjectRoot(),plan,&error,ModelImportMode(gltf?m_ModelImportMode:0)))throw std::runtime_error(error);
      m_SelectedAsset=plan.standaloneImage?plan.package:plan.package+"/"+plan.entry;
      if(gltf&&m_ModelImportMode!=0){m_SelectedAsset+=".wscene";m_SelectedAssetKind="Imported Scene";if(m_ModelImportMode==2)InstantiateImportedScene(app,m_SelectedAsset);}
      else m_SelectedAssetKind=ext==".obj"||gltf?"Mesh":ext==".hdr"?"Environment":ext==".ttf"||ext==".otf"?"Font":"Texture";
      NavigateAssetFolder(plan.standaloneImage?m_ImportTargetFolder:plan.package);m_AssetFilter.clear();m_AssetKindFilter=0;m_AssetListsDirty=true;
      m_ImportResults.emplace_back(name,"Imported: "+m_SelectedAsset);
    }catch(const std::exception& e){m_ImportResults.emplace_back(name,"FAILED: "+std::string(e.what()));m_ImportFailed.push_back(name);}}
    if(!m_ImportFailed.empty())m_EditorError=std::to_string(m_ImportFailed.size())+" import(s) failed. Successful items are retained; retry only failed items.";
  };
  ImGui::BeginDisabled(app.IsEditorPlayMode()||sources.empty());
  if(Decision("Import (no overwrite)"))publish(sources);
  ImGui::EndDisabled();
  ImGui::SameLine();ImGui::BeginDisabled(app.IsEditorPlayMode()||m_ImportFailed.empty());
  if(ImGui::Button("Retry failed")){std::vector<std::filesystem::path> failed;for(const auto& p:m_ImportFailed)failed.push_back(std::filesystem::u8path(p));publish(failed);}
  ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();
  if(!m_ImportResults.empty()){ImGui::BeginChild("Import results",{620,150},ImGuiChildFlags_Borders);for(const auto& [source,result]:m_ImportResults)ImGui::TextWrapped("%s\n%s",source.c_str(),result.c_str());ImGui::EndChild();}
  if(!m_EditorError.empty())ErrorText(m_EditorError.c_str());ImGui::EndPopup();

 }
}
}
