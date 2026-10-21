#include "EditorLayer.h"
#include "EditorWidgets.h"
#include "../core/Application.h"
#include "../resources/ResourceManager.h"
#include <imgui.h>
#include <sstream>
#include <set>
#include <cstring>
namespace editor {
void EditorLayer::ReleaseAssetPreview(IRenderAdapter* renderer){ReleaseMaterialEditor(renderer);ReleaseAssetThumbnails(renderer);if(renderer&&m_AssetPreviewTexture.IsValid())renderer->DestroyTexture(m_AssetPreviewTexture);m_AssetPreviewTexture={};m_AssetPreviewKey.clear();m_AssetPreviewVersion=0;}
void EditorLayer::ReleaseAssetThumbnails(IRenderAdapter* renderer){
    if(renderer){
        for(auto& [key,item]:m_AssetThumbnails)if(item.texture.IsValid())renderer->DestroyTexture(item.texture);
        if(m_ThumbnailWhite.IsValid())renderer->DestroyTexture(m_ThumbnailWhite);
        if(m_ThumbnailShader.IsValid())renderer->DestroyShader(m_ThumbnailShader);
    }
    m_AssetThumbnails.clear();m_ThumbnailWhite={};m_ThumbnailShader={};
}
void EditorLayer::BeginAssetThumbnailFrame(Application& app){
    auto* resources=app.GetResourceManager();
    const auto epoch=resources?resources->GetCacheEpoch():0;
    if(m_ThumbnailRevision!=m_AssetCatalog.Revision()||m_ThumbnailEpoch!=epoch){
        ReleaseAssetThumbnails(app.GetRenderAdapter());m_ThumbnailRevision=m_AssetCatalog.Revision();m_ThumbnailEpoch=epoch;
    }
    ++m_ThumbnailFrame;m_ThumbnailRequests=m_ThumbnailUploads=0;
    // Bounded GPU cache; recently visible rows survive scrolling back.
    while(m_AssetThumbnails.size()>96){
        auto oldest=std::min_element(m_AssetThumbnails.begin(),m_AssetThumbnails.end(),[](auto& a,auto& b){return a.second.lastUsed<b.second.lastUsed;});
        if(oldest->second.texture.IsValid()&&app.GetRenderAdapter())app.GetRenderAdapter()->DestroyTexture(oldest->second.texture);
        m_AssetThumbnails.erase(oldest);
    }
}
const EditorLayer::AssetThumbnail& EditorLayer::RequestAssetThumbnail(Application& app,ecs::AssetKind kind,const std::string& key){
    ++m_BrowserStatistics.thumbnailCalls;
    if(auto cached=m_AssetThumbnails.find(key);cached!=m_AssetThumbnails.end()&&cached->second.texture.IsValid())++m_BrowserStatistics.thumbnailHits;
    auto& item=m_AssetThumbnails[key];item.kind=kind;item.lastUsed=m_ThumbnailFrame;
    auto* resources=app.GetResourceManager();auto* renderer=app.GetRenderAdapter();
    if(!resources||!renderer)return item;
    if(kind!=ecs::AssetKind::Texture&&kind!=ecs::AssetKind::Mesh){item.state="Indexed";return item;}
    const auto path=std::filesystem::u8path(key);
    auto pending=[&]{
        unsigned count=0;
        for(auto& [k,t]:m_AssetThumbnails){
            if(t.kind==ecs::AssetKind::Mesh){auto r=resources->PeekIndexed<MeshResource>(k);count+=r&&r->IsLoading();}
            if(t.kind==ecs::AssetKind::Texture){auto r=resources->PeekIndexed<TextureResource>(k);count+=r&&r->IsLoading();}
        }return count;
    };
    auto process=[&](auto resource){
        if(!resource){
            if(m_ThumbnailRequests<2&&pending()<2){
                ++m_ThumbnailRequests;
                if(kind==ecs::AssetKind::Mesh)(void)resources->LoadAsync<MeshResource>(path);
                else (void)resources->LoadAsync<TextureResource>(path);
                item.state="Loading";
            }return;
        }
        if(resource->IsLoading()){item.state="Loading";return;}
        if(resource->IsFailed()){item.state="Failed";item.error=resource->GetErrorMessage();return;}
        if(!resource->IsLoaded())return;
        if(item.version!=resource->GetVersion()){
            if(item.texture.IsValid())renderer->DestroyTexture(item.texture);
            item.texture={};item.error.clear();item.version=resource->GetVersion();item.state="Loading";
        }
        if(item.texture.IsValid()||!item.error.empty()||m_ThumbnailUploads>=1)return;
        using Data=std::decay_t<decltype(resource->GetData())>;
        if constexpr(std::is_same_v<Data,TextureResource>){
            const auto& source=resource->GetData().textureData;
            if(source.width<=0||source.height<=0||source.channelCount!=4||source.pixels.size()!=std::size_t(source.width)*source.height*4){item.error="Invalid RGBA texture data";item.state="Failed";return;}
            TextureData small;const float scale=std::min(1.f,256.f/std::max(source.width,source.height));
            small.width=std::max(1,int(source.width*scale));small.height=std::max(1,int(source.height*scale));small.pixels.resize(std::size_t(small.width)*small.height*4);
            for(int y=0;y<small.height;++y)for(int x=0;x<small.width;++x){
                const auto sx=std::min(source.width-1,int((x+.5f)*source.width/small.width));
                const auto sy=std::min(source.height-1,int((y+.5f)*source.height/small.height));
                std::memcpy(&small.pixels[(std::size_t(y)*small.width+x)*4],&source.pixels[(std::size_t(sy)*source.width+sx)*4],4);
            }
            ++m_ThumbnailUploads;item.texture=renderer->CreateThumbnailTexture(small);item.width=small.width;item.height=small.height;
        }else{
            auto shader=resources->PeekIndexed<ShaderResource>("dx12/textured.hlsl");
            if(!shader){(void)resources->LoadAsync<ShaderResource>("dx12/textured.hlsl");return;}
            if(shader->IsLoading())return;
            if(shader->IsFailed()){item.error=shader->GetErrorMessage();item.state="Failed";return;}
            ++m_ThumbnailUploads;
            if(!m_ThumbnailShader.IsValid())m_ThumbnailShader=renderer->CreateShaderProgram(shader->GetData());
            if(!m_ThumbnailWhite.IsValid()){TextureData white;white.pixels={255,255,255,255};m_ThumbnailWhite=renderer->CreateThumbnailTexture(white);}
            item.texture=renderer->RenderMeshThumbnail(resource->GetData().meshData,m_ThumbnailShader,m_ThumbnailWhite);item.width=item.height=256;
        }
        if(item.texture.IsValid())item.state="Ready";
        else{item.state="Failed";item.error="Thumbnail GPU creation failed (unsupported renderer, invalid geometry, or exhausted GPU resources)";}
    };
    if(kind==ecs::AssetKind::Texture)process(resources->PeekIndexed<TextureResource>(key));else process(resources->PeekIndexed<MeshResource>(key));
    return item;
}
void EditorLayer::DrawAssetPreview(Application& app){
 auto* resources=app.GetResourceManager();auto* renderer=app.GetRenderAdapter();if(!resources||m_SelectedAsset.empty())return;
 if(m_SelectedAssetKind=="Mesh"||m_SelectedAssetKind=="Texture"){
  const auto& preview=RequestAssetThumbnail(app,m_SelectedAssetKind=="Mesh"?ecs::AssetKind::Mesh:ecs::AssetKind::Texture,m_SelectedAsset);
  if(renderer&&preview.texture.IsValid())if(auto id=renderer->GetTexturePreviewId(preview.texture)){
   const float scale=std::min(320.f,ImGui::GetContentRegionAvail().x)/std::max(preview.width,preview.height);
   ImGui::Image(ImTextureID(id),{preview.width*scale,preview.height*scale});
  }
  if(!preview.error.empty())ImGui::TextWrapped("%s",preview.error.c_str());
 }
 if(m_SelectedAssetKind=="Mesh")if(auto resource=resources->PeekIndexed<MeshResource>(m_SelectedAsset);resource&&resource->IsLoaded()){
  if(m_AssetMetadataKey!=m_SelectedAsset||m_AssetMetadataVersion!=resource->GetVersion()){
   m_AssetMetadataKey=m_SelectedAsset;m_AssetMetadataVersion=resource->GetVersion();const auto& d=resource->GetData().meshData;std::ostringstream text;text<<"Format: "<<std::filesystem::u8path(m_SelectedAsset).extension().string()<<"\nMaterials: "<<d.materials.size()<<" / Submeshes: "<<d.submeshes.size();
   std::array<float,3> lo{FLT_MAX,FLT_MAX,FLT_MAX},hi{-FLT_MAX,-FLT_MAX,-FLT_MAX};for(const auto& v:d.vertices)for(int i=0;i<3;++i){lo[i]=std::min(lo[i],v.position[i]);hi[i]=std::max(hi[i],v.position[i]);}
   if(!d.vertices.empty())text<<"\nBounds: ["<<lo[0]<<", "<<lo[1]<<", "<<lo[2]<<"] - ["<<hi[0]<<", "<<hi[1]<<", "<<hi[2]<<"]";
   std::set<std::string> dependencies;for(const auto& m:d.materials){text<<"\nMaterial: "<<m.name;for(const auto* key:{&m.texturePath,&m.normalTexturePath,&m.metallicRoughnessTexturePath,&m.occlusionTexturePath})if(!key->empty())dependencies.insert(*key);}
   text<<"\nDependencies: "<<dependencies.size();for(const auto& key:dependencies)text<<"\n"<<key;m_AssetMetadata=text.str();
  }ImGui::TextWrapped("%s",m_AssetMetadata.c_str());
 }
 if(m_SelectedAssetKind=="Material")if(auto resource=resources->PeekIndexed<MaterialResource>(m_SelectedAsset);resource&&resource->IsLoaded()){
  const auto& m=resource->GetData();ImGui::ColorButton("Base Color",{LinearToSrgb(m.baseColor[0]),LinearToSrgb(m.baseColor[1]),LinearToSrgb(m.baseColor[2]),m.baseColor[3]});ImGui::TextWrapped("Base: %s\nNormal: %s\nMR: %s\nAO: %s",m.texturePath.c_str(),m.normalTexturePath.c_str(),m.metallicRoughnessTexturePath.c_str(),m.occlusionTexturePath.c_str());
 }
 if(m_SelectedAssetKind=="Environment"){
  if(!app.IsEditorPlayMode()){
   if(ImGui::Button("Use as Scene Skybox")){BeginSceneTransaction(app,"Set skybox");auto s=app.GetSceneSettings();s.background.background=EditorBackground::Cubemap;s.background.cubemap=m_SelectedAsset;s.background.useIblEnvironment=false;s.background.sunMetadata=false;if(s.background.sunSync==SunSyncMode::SkyDrivesLight)s.background.sunSync=SunSyncMode::Independent;app.SetSceneSettings(s);FinishTransaction(app);}
   ImGui::SameLine();if(ImGui::Button("Use as IBL"))AssignAsset(app,{},ecs::AssetKind::Environment,m_SelectedAsset);
  }
  if(auto resource=resources->PeekIndexed<EnvironmentResource>(m_SelectedAsset);resource&&resource->IsLoaded()&&!resource->GetData().placeholder){
   const auto& d=resource->GetData();
   if(renderer&&(m_AssetPreviewKey!=m_SelectedAsset||m_AssetPreviewVersion!=resource->GetVersion())&&d.width&&d.height&&d.radiance.size()==std::size_t(d.width)*d.height*4){
    TextureData thumbnail;thumbnail.width=256;thumbnail.height=128;thumbnail.pixels.resize(256*128*4);
    for(unsigned y=0;y<128;++y)for(unsigned x=0;x<256;++x){unsigned sx=std::min(d.width-1,unsigned((x+.5f)*d.width/256)),sy=std::min(d.height-1,unsigned((y+.5f)*d.height/128));for(int c=0;c<3;++c){float v=d.radiance[(std::size_t(sy)*d.width+sx)*4+c];thumbnail.pixels[(y*256+x)*4+c]=std::uint8_t(std::lround(255*LinearToSrgb(v/(1+v))));}thumbnail.pixels[(y*256+x)*4+3]=255;}
    if(m_AssetPreviewTexture.IsValid())renderer->DestroyTexture(m_AssetPreviewTexture);m_AssetPreviewTexture=renderer->CreateTexture2D(thumbnail);m_AssetPreviewKey=m_SelectedAsset;m_AssetPreviewVersion=resource->GetVersion();
   }
   if(renderer)if(auto id=renderer->GetTexturePreviewId(m_AssetPreviewTexture)){float width=std::min(256.f,ImGui::GetContentRegionAvail().x);ImGui::Image(ImTextureID(id),{width,width*.5f});ImGui::TextDisabled("Cached Reinhard preview; scene exposure is independent.");}
  }
 }
}
}
