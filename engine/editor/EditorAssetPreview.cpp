#include "EditorLayer.h"
#include "EditorWidgets.h"
#include "../core/Application.h"
#include "../resources/ResourceManager.h"
#include <imgui.h>
#include <sstream>
#include <set>
namespace editor {
void EditorLayer::ReleaseAssetPreview(IRenderAdapter* renderer){if(renderer&&m_AssetPreviewTexture.IsValid())renderer->DestroyTexture(m_AssetPreviewTexture);m_AssetPreviewTexture={};m_AssetPreviewKey.clear();m_AssetPreviewVersion=0;}
void EditorLayer::DrawAssetPreview(Application& app){
 auto* resources=app.GetResourceManager();auto* renderer=app.GetRenderAdapter();if(!resources||m_SelectedAsset.empty())return;
 if(m_SelectedAssetKind=="Mesh")if(auto resource=resources->Get<MeshResource>(m_SelectedAsset);resource&&resource->IsLoaded()){
  if(m_AssetMetadataKey!=m_SelectedAsset||m_AssetMetadataVersion!=resource->GetVersion()){
   m_AssetMetadataKey=m_SelectedAsset;m_AssetMetadataVersion=resource->GetVersion();const auto& d=resource->GetData().meshData;std::ostringstream text;text<<"Format: "<<std::filesystem::u8path(m_SelectedAsset).extension().string()<<"\nMaterials: "<<d.materials.size()<<" / Submeshes: "<<d.submeshes.size();
   std::array<float,3> lo{FLT_MAX,FLT_MAX,FLT_MAX},hi{-FLT_MAX,-FLT_MAX,-FLT_MAX};for(const auto& v:d.vertices)for(int i=0;i<3;++i){lo[i]=std::min(lo[i],v.position[i]);hi[i]=std::max(hi[i],v.position[i]);}
   if(!d.vertices.empty())text<<"\nBounds: ["<<lo[0]<<", "<<lo[1]<<", "<<lo[2]<<"] - ["<<hi[0]<<", "<<hi[1]<<", "<<hi[2]<<"]";
   std::set<std::string> dependencies;for(const auto& m:d.materials){text<<"\nMaterial: "<<m.name;for(const auto* key:{&m.texturePath,&m.normalTexturePath,&m.metallicRoughnessTexturePath,&m.occlusionTexturePath})if(!key->empty())dependencies.insert(*key);}
   text<<"\nDependencies: "<<dependencies.size();for(const auto& key:dependencies)text<<"\n"<<key;m_AssetMetadata=text.str();
  }ImGui::TextWrapped("%s",m_AssetMetadata.c_str());
 }
 if(m_SelectedAssetKind=="Material")if(auto resource=resources->Get<MaterialResource>(m_SelectedAsset);resource&&resource->IsLoaded()){
  const auto& m=resource->GetData();ImGui::ColorButton("Base Color",{LinearToSrgb(m.baseColor[0]),LinearToSrgb(m.baseColor[1]),LinearToSrgb(m.baseColor[2]),m.baseColor[3]});ImGui::TextWrapped("Base: %s\nNormal: %s\nMR: %s\nAO: %s",m.texturePath.c_str(),m.normalTexturePath.c_str(),m.metallicRoughnessTexturePath.c_str(),m.occlusionTexturePath.c_str());
 }
 if(m_SelectedAssetKind=="Environment"){
  if(!app.IsEditorPlayMode()){
   if(ImGui::Button("Use as Scene Skybox")){BeginSceneTransaction(app,"Set skybox");auto s=app.GetSceneSettings();s.background.background=EditorBackground::Cubemap;s.background.cubemap=m_SelectedAsset;s.background.useIblEnvironment=false;s.background.sunMetadata=false;if(s.background.sunSync==SunSyncMode::SkyDrivesLight)s.background.sunSync=SunSyncMode::Independent;app.SetSceneSettings(s);FinishTransaction(app);}
   ImGui::SameLine();if(ImGui::Button("Use as IBL"))AssignAsset(app,{},ecs::AssetKind::Environment,m_SelectedAsset);
  }
  if(auto resource=resources->Get<EnvironmentResource>(m_SelectedAsset);resource&&resource->IsLoaded()&&!resource->GetData().placeholder){
   const auto& d=resource->GetData();
   if(renderer&&(m_AssetPreviewKey!=m_SelectedAsset||m_AssetPreviewVersion!=resource->GetVersion())&&d.width&&d.height&&d.radiance.size()==std::size_t(d.width)*d.height*4){
    TextureData thumbnail;thumbnail.width=256;thumbnail.height=128;thumbnail.pixels.resize(256*128*4);
    for(unsigned y=0;y<128;++y)for(unsigned x=0;x<256;++x){unsigned sx=std::min(d.width-1,unsigned((x+.5f)*d.width/256)),sy=std::min(d.height-1,unsigned((y+.5f)*d.height/128));for(int c=0;c<3;++c){float v=d.radiance[(std::size_t(sy)*d.width+sx)*4+c];thumbnail.pixels[(y*256+x)*4+c]=std::uint8_t(std::lround(255*LinearToSrgb(v/(1+v))));}thumbnail.pixels[(y*256+x)*4+3]=255;}
    ReleaseAssetPreview(renderer);m_AssetPreviewTexture=renderer->CreateTexture2D(thumbnail);m_AssetPreviewKey=m_SelectedAsset;m_AssetPreviewVersion=resource->GetVersion();
   }
   if(renderer)if(auto id=renderer->GetTexturePreviewId(m_AssetPreviewTexture)){float width=std::min(256.f,ImGui::GetContentRegionAvail().x);ImGui::Image(ImTextureID(id),{width,width*.5f});ImGui::TextDisabled("Cached Reinhard preview; scene exposure is independent.");}
  }
 }
}
}
