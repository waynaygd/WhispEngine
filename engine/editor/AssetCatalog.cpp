#include "AssetCatalog.h"
#include "../core/AssetPaths.h"
#include "../resources/loaders/MaterialLoader.h"
#include <fstream>
#include <algorithm>
#include <stdexcept>
namespace editor {
namespace {
template<class F>bool Guard(F fn,std::string* error){try{fn();if(error)error->clear();return true;}catch(const std::exception& e){if(error)*error=e.what();return false;}}
void CheckReferences(const std::string& key){
    // Explicit destructive requests only: never performed by the per-frame UI.
    const auto root=AssetPaths::ResolveAssetRoot();
    for(const auto& item:std::filesystem::recursive_directory_iterator(root)){
        if(item.path().extension()!=".json"||!item.is_regular_file())continue;
        if(std::filesystem::is_symlink(item.symlink_status()))throw std::runtime_error("Cannot prove reference safety through JSON symlink");
        if(item.file_size()>16*1024*1024)throw std::runtime_error("Reference audit cannot read oversized JSON: "+item.path().filename().string());
        std::ifstream stream(item.path());auto j=nlohmann::json::parse(stream,nullptr,false);
        if(j.is_discarded())throw std::runtime_error("Fix invalid project JSON before renaming/deleting assets: "+item.path().filename().string());
        if(AssetCatalog::ContainsReference(j,key))throw std::runtime_error("Asset is referenced by "+item.path().filename().string()+"; reassign and save the reference first");
    }
}
}
AssetCatalog::AssetCatalog(){
    Register({"Mesh","[M]","models",ecs::AssetKind::Mesh,{".obj",".fbx",".gltf",".glb"}});
    Register({"Texture","[T]","textures",ecs::AssetKind::Texture,{".png",".jpg",".jpeg",".bmp",".tga",".dds"}});
    Register({"Material","[Mat]","materials",ecs::AssetKind::Material,{".json",".material"}});
    Register({"Shader","[S]","",ecs::AssetKind::Shader,{".hlsl"},true});
    Register({"Environment","[HDR]","environments",ecs::AssetKind::Environment,{".hdr"}});
}
void AssetCatalog::Register(AssetType type){
    if(type.name.empty()||std::any_of(types.begin(),types.end(),[&](auto& t){return t.name==type.name;}))throw std::runtime_error("Duplicate/empty browser asset type");
    if(!ecs::ComponentRegistry::ValidateAssetKey(type.folder,ecs::AssetKind::None))throw std::runtime_error("Unsafe asset type folder");
    types.push_back(std::move(type));
}
void AssetCatalog::Refresh(){
    std::vector<AssetEntry> ready;std::set<std::string> dirs;
    for(std::size_t i=0;i<types.size();++i){auto& type=types[i];const auto root=type.shaderRoot?AssetPaths::ResolveShaderRoot():AssetPaths::ResolveAssetRoot();const auto directory=root/type.folder;
        if(!std::filesystem::exists(directory))continue;
        if(!type.folder.empty())dirs.insert(type.folder);
        for(const auto& item:std::filesystem::recursive_directory_iterator(directory,std::filesystem::directory_options::skip_permission_denied)){
            if(!item.is_regular_file()||std::filesystem::is_symlink(item.symlink_status()))continue;
            auto ext=item.path().extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
            if(std::find(type.extensions.begin(),type.extensions.end(),ext)==type.extensions.end())continue;
            auto key=AssetPaths::ToUtf8String(item.path().lexically_relative(root));std::replace(key.begin(),key.end(),'\\','/');ready.push_back({key,i});
            auto folder=std::filesystem::u8path(key).parent_path();while(!folder.empty()){dirs.insert(folder.generic_string());folder=folder.parent_path();}
        }
    }
    std::sort(ready.begin(),ready.end(),[](auto& a,auto& b){return a.key<b.key;});entries=std::move(ready);folders=std::move(dirs);++revision;
}
std::filesystem::path AssetCatalog::SafeMaterialPath(const std::string& key){
    if(std::any_of(key.begin(),key.end(),[](unsigned char c){return c<32;})||key.empty()||!key.starts_with("materials/")||!ecs::ComponentRegistry::ValidateAssetKey(key,ecs::AssetKind::Material)||std::filesystem::u8path(key).extension()!=".json")throw std::runtime_error("Use a portable materials/*.json key");
    const auto root=std::filesystem::weakly_canonical(AssetPaths::ResolveAssetRoot());auto path=AssetPaths::ResolveAssetOutputPath(key);
    if(path.empty()||std::filesystem::is_symlink(std::filesystem::symlink_status(path)))throw std::runtime_error("Unsafe material output path");
    auto resolved=std::filesystem::weakly_canonical(path);auto relative=resolved.lexically_relative(root);
    if(relative.empty()||relative.is_absolute()||*relative.begin()=="..")throw std::runtime_error("Material path escapes asset root");return path;
}
bool AssetCatalog::ContainsReference(const nlohmann::json& j,const std::string& key){
    if(j.is_string())return j.get_ref<const std::string&>()==key;
    if(j.is_structured())for(const auto& value:j)if(ContainsReference(value,key))return true;return false;
}
bool AssetCatalog::CreateMaterial(const std::string& key,std::string* error){return Guard([&]{
    auto path=SafeMaterialPath(key);if(std::filesystem::exists(path))throw std::runtime_error("Asset already exists; choose another name");
    MaterialResource material;material.name=std::filesystem::u8path(key).stem().string();material.placeholder=false;material.shadingModel=ShadingModel::PBR;
    std::string message;if(!MaterialLoader::Save(key,material,&message))throw std::runtime_error(message);created.insert(key);
},error);}
bool AssetCatalog::RenameMaterial(const std::string& from,const std::string& to,std::string* error){return Guard([&]{
    if(!created.contains(from))throw std::runtime_error("Only materials created in this editor session can be renamed");
    auto source=SafeMaterialPath(from),dest=SafeMaterialPath(to);if(std::filesystem::exists(dest))throw std::runtime_error("Destination already exists");CheckReferences(from);
    std::filesystem::create_directories(dest.parent_path());std::filesystem::rename(source,dest);created.erase(from);created.insert(to);
},error);}
bool AssetCatalog::DeleteMaterial(const std::string& key,std::string* error){return Guard([&]{
    if(!created.contains(key))throw std::runtime_error("Only materials created in this editor session can be deleted");
    auto path=SafeMaterialPath(key);CheckReferences(key);if(!std::filesystem::remove(path))throw std::runtime_error("Material no longer exists");created.erase(key);
},error);}
}
