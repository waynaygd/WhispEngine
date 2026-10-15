#include "EditorLayer.h"
#include "GltfSceneImport.h"
#include "../core/Application.h"
#include "../core/AssetPaths.h"
#include "../resources/loaders/MeshLoader.h"
#include <set>
#include "../resources/ResourceManager.h"
namespace editor {
std::vector<ecs::Entity> EditorLayer::PlaceImportedScene(Application& app,const std::string& key,ecs::Vec3 point){
 try{std::filesystem::path path;for(const auto& root:{AssetPaths::ResolveAssetRoot(),AssetImport::ProjectRoot()}){auto p=AssetImport::SafeTarget(root,key);if(std::filesystem::is_regular_file(p)){path=p;break;}}if(path.empty())throw std::runtime_error("Missing scene placement asset");auto manifest=GltfSceneImport::ReadManifest(path);auto origin=manifest.value("placementOrigin",std::array<float,3>{0,0,0});return InstantiateImportedScene(app,key,{point.x-origin[0],point.y-origin[1],point.z-origin[2]});}catch(const std::exception& e){m_EditorError=e.what();return {};}
}
std::vector<ecs::Entity> EditorLayer::InstantiateImportedScene(Application& app,const std::string& key,ecs::Vec3 offset){
 std::vector<ecs::Entity> created;if(app.IsEditorPlayMode())return created;
 const auto previous=m_SelectedEntity;
 try{
  if(!key.ends_with(".wscene")||!std::isfinite(offset.x)||!std::isfinite(offset.y)||!std::isfinite(offset.z))throw std::runtime_error("Invalid imported scene/placement");
  auto resolve=[&](const std::string& asset){for(const auto& root:{AssetPaths::ResolveAssetRoot(),AssetImport::ProjectRoot()}){auto p=AssetImport::SafeTarget(root,asset);if(std::filesystem::is_regular_file(p))return p;}throw std::runtime_error("Missing imported asset: "+asset);};
  auto manifest=GltfSceneImport::ReadManifest(resolve(key));
  struct Item{std::string name,mesh;ecs::TransformComponent transform;};std::vector<Item> items;std::set<std::string> checked,texturesChecked;
  const auto directory=AssetPaths::ToUtf8String(std::filesystem::u8path(key).parent_path())+"/";
  for(const auto& node:manifest.at("nodes")){
   Item item;item.name=node.at("name").get<std::string>();item.mesh=node.at("mesh").get<std::string>();
   if(item.name.empty()||item.name.size()>240||!item.mesh.starts_with(directory)||!ecs::ComponentRegistry::ValidateAssetKey(item.mesh,ecs::AssetKind::Mesh))throw std::runtime_error("Invalid imported node name/mesh");
   auto p=node.at("position").get<std::array<float,3>>(),r=node.at("rotation").get<std::array<float,3>>(),s=node.at("scale").get<std::array<float,3>>();
   item.transform.position={p[0]+offset.x,p[1]+offset.y,p[2]+offset.z};item.transform.rotation={r[0],r[1],r[2]};item.transform.scale={s[0],s[1],s[2]};
   for(const auto& v:{item.transform.position,item.transform.rotation,item.transform.scale})for(float x:{v.x,v.y,v.z})if(!std::isfinite(x)||std::abs(x)>10000)throw std::runtime_error("Imported TRS exceeds editor limits");
   float matrix[16];ecs::BuildRenderModelMatrix(matrix,ecs::RenderPose::From(item.transform));ImportMatrix model;std::copy(matrix,matrix+16,model.begin());if(!DecomposeTransform(model,item.transform.scale))throw std::runtime_error("Invalid imported transform");
   if(checked.insert(item.mesh).second){auto loaded=MeshLoader::Load(item.mesh,resolve(item.mesh));if(!loaded.success||loaded.data.placeholder)throw std::runtime_error("Imported mesh validation failed: "+loaded.errorMessage);
    for(const auto& material:loaded.data.meshData.materials)for(const auto* dependency:{&material.texturePath,&material.normalTexturePath,&material.metallicRoughnessTexturePath,&material.occlusionTexturePath})if(!dependency->empty()){
     auto base=dependency->substr(0,dependency->find('#'));auto path=resolve(base);if(!path.empty()&&texturesChecked.insert(*dependency).second){auto image=TextureLoader::Load(*dependency,path);if(!image.success||image.data.placeholder)throw std::runtime_error("Imported texture validation failed: "+image.errorMessage);}
    }}
   items.push_back(std::move(item));
  }
  FinishTransaction(app);EntitySnapshot before;before.world=app.GetWorld().GetIdentity();before.previousSelection=previous;
  for(const auto& item:items){auto e=app.CreateEmptyEntity(item.name);created.push_back(e);auto& world=app.GetWorld();*world.GetComponent<ecs::TransformComponent>(e)=item.transform;world.AddComponent<ecs::MeshRendererComponent>(e).meshPath=item.mesh;world.AddComponent<ecs::MaterialComponent>(e);EntitySnapshot dead;dead.entity=e;dead.world=e.world;before.related.push_back(dead);app.NotifyEditorMutation(e);}
  SelectEntityFromUser(app,created.back());CommitUndo(app,"Instantiate imported scene",std::move(before));
  if(auto* resources=app.GetResourceManager())for(const auto& mesh:checked)resources->LoadAsync<MeshResource>(mesh);
  m_EditorError.clear();return created;
 }catch(const std::exception& error){for(auto e:created)if(app.GetWorld().IsAlive(e))app.DestroySceneEntity(e);m_SelectedEntity=app.GetWorld().IsAlive(previous)?previous:ecs::Entity{};m_EditorError=error.what();return {};}
}
}
