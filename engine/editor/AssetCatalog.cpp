#include "AssetCatalog.h"
#include "SceneCatalog.h"
#include "AssetImport.h"
#include "LuaAssetDependencies.h"
#include "../core/AssetPaths.h"
#include "../core/ProjectContext.h"
#include "../resources/loaders/MaterialLoader.h"
#include <fstream>
#include <map>
#include <algorithm>
#include <stdexcept>
#include "../scripting/Prefab.h"
#include "../scene/SceneSerializer.h"
#include "../ecs/components/SceneIdentityComponent.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif
namespace editor {
bool AssetCatalog::PollExternalChanges(float dt){
#ifdef _WIN32
    const auto root=AssetPaths::ResolveAssetRoot();
    const bool rootChanged=root!=watchedRoot;
    if(rootChanged){directoryWatch.reset();watchedRoot=root;refreshPending=false;quietSeconds=0;}
    if(!directoryWatch&&std::filesystem::is_directory(root)){
        auto handle=FindFirstChangeNotificationW(root.c_str(),TRUE,FILE_NOTIFY_CHANGE_FILE_NAME|FILE_NOTIFY_CHANGE_DIR_NAME|FILE_NOTIFY_CHANGE_LAST_WRITE|FILE_NOTIFY_CHANGE_SIZE);
        if(handle!=INVALID_HANDLE_VALUE)directoryWatch={handle,[](void* h){FindCloseChangeNotification(h);}};
    }
    if(directoryWatch&&WaitForSingleObject(directoryWatch.get(),0)==WAIT_OBJECT_0){
        refreshPending=true;quietSeconds=0;
        if(!FindNextChangeNotification(directoryWatch.get()))directoryWatch.reset();
    }
    if(rootChanged)return true;
    if(refreshPending){quietSeconds+=std::max(0.f,dt);if(quietSeconds>=.25f){refreshPending=false;return true;}}
#endif
    return false;
}
bool AssetCatalog::CreateStarter(const std::string& key,const std::string& type,std::string* error){
    try{
        if(type!="PostFX"&&type!="Lens Distortion"&&type!="HLSL"&&type!="Empty Prefab")throw std::runtime_error("Unknown starter type");
        const auto suffix=type=="Empty Prefab"?".prefab.json":type=="HLSL"?".hlsl":".postfx.json";
        if(type=="HLSL"&&(key.starts_with("dx12/")||key.starts_with("vulkan/")))throw std::runtime_error("Backend shader namespaces are engine-owned; choose another Assets folder");
        auto path=scripting::SafeAsset(key,suffix);
        if(std::filesystem::exists(path))throw std::runtime_error("Asset already exists; nothing overwritten");
        std::filesystem::create_directories(path.parent_path());
        if(type=="Empty Prefab"){
            ecs::World world;auto e=world.CreateEntity();world.AddComponent<ecs::TransformComponent>(e);
            auto json=scripting::CapturePrefab(world,e);scripting::ValidatePrefab(json);
            if(!SceneSerializer::WriteAtomic(path,json,error,false))return false;
        }else{
            auto shader=type=="HLSL"?path:path.parent_path()/(path.filename().string().substr(0,path.filename().string().size()-12)+".hlsl");
            if(std::filesystem::exists(shader))throw std::runtime_error("Shader already exists; nothing overwritten");
            const std::string body=type=="Lens Distortion"?
                "float4 PostProcess(float2 uv) { float2 p=uv*2-1; float2 warped=(p*(1+strength*dot(p,p))+1)*0.5; return SampleSceneColor(saturate(warped)); }\n":
                "float4 PostProcess(float2 uv) { return SampleSceneColor(uv); }\n";
            std::ofstream out(shader,std::ios::binary);
            if(type=="HLSL")out<<"cbuffer Constants:register(b0){float4x4 mvp;float4 tint;}\nstruct VOut{float4 position:SV_Position;float4 color:COLOR;};\nVOut VSMain(float3 p:POSITION){VOut v;v.position=mul(mvp,float4(p,1));v.color=tint;return v;}\nfloat4 PSMain(VOut v):SV_Target{return v.color;}\n";
            else out<<"#include \"WhispPostFX.hlsli\"\n"<<body;
            out.close();
            if(!out)throw std::runtime_error("Shader write failed");
            if(type!="HLSL"){
                nlohmann::json json={{"version",1},{"id","whisp."+ecs::NewSceneIdentity()},{"name",type},{"shader",AssetPaths::ToUtf8String(shader.filename())},{"stage","AfterTonemap"},{"order",0},{"parameters",nlohmann::json::array()}};
                if(type=="Lens Distortion")json["parameters"].push_back({{"name","strength"},{"type","float"},{"default",.1f},{"min",-.5f},{"max",.5f}});
                if(!SceneSerializer::WriteAtomic(path,json,error,false)){std::filesystem::remove(shader);return false;}
            }
        }
        // Publish invalidation, not a synchronous vector replacement while the
        // browser may be iterating a card/context menu from the current snapshot.
        created.insert(key);++revision;if(error)error->clear();return true;
    }catch(const std::exception& e){if(error)*error=e.what();return false;}
}
namespace {
template<class F>bool Guard(F fn,std::string* error){try{fn();if(error)error->clear();return true;}catch(const std::exception& e){if(error)*error=e.what();return false;}}
}
AssetCatalog::AssetCatalog(){
    Register({"DataAsset","[DA]","",ecs::AssetKind::None,{".json"}});
    Register({"DataTable","[DT]","",ecs::AssetKind::None,{".json"}});
    Register({"Font","[Font]","",ecs::AssetKind::Font,{".ttf",".otf"}});
    Register({"UI Document","[UI]","",ecs::AssetKind::UIDocument,{".json"}});
    Register({"Custom PostFX","[FX]","",ecs::AssetKind::PostFx,{".json"}});
    Register({"Lua Script","[Lua]","",ecs::AssetKind::LuaScript,{".lua"}});
    Register({"Prefab","[PF]","",ecs::AssetKind::Prefab,{".json"}});
    Register({"Mesh","[M]","",ecs::AssetKind::Mesh,{".obj",".fbx",".gltf",".glb"}});
    Register({"Texture","[T]","",ecs::AssetKind::Texture,{".png",".jpg",".jpeg",".bmp",".tga",".dds"}});
    Register({"Material","[Mat]","",ecs::AssetKind::Material,{".json",".material"}});
    Register({"Shader","[S]","",ecs::AssetKind::Shader,{".hlsl"},true});
    Register({"Scene Document","[Level]","scenes",ecs::AssetKind::None,{".json"}});
    Register({"Imported Scene","[Scene]","",ecs::AssetKind::None,{".wscene"}});
    Register({"UI Style","[Style]","",ecs::AssetKind::None,{".json"}});
    Register({"Unknown","[?]","",ecs::AssetKind::None,{"*"}});
    Register({"Audio","[Audio]","",ecs::AssetKind::None,{".wav",".ogg",".mp3",".flac"}});
    Register({"Environment","[HDR]","",ecs::AssetKind::Environment,{".hdr"}});
}
void AssetCatalog::Register(AssetType type){
    if(type.name.empty()||std::any_of(types.begin(),types.end(),[&](auto& t){return t.name==type.name;}))throw std::runtime_error("Duplicate/empty browser asset type");
    if(!ecs::ComponentRegistry::ValidateAssetKey(type.folder,ecs::AssetKind::None))throw std::runtime_error("Unsafe asset type folder");
    types.push_back(std::move(type));
}
void AssetCatalog::Refresh(){
    std::vector<AssetEntry> ready;std::set<std::string> dirs;
    std::map<std::filesystem::path,bool> safeEntries;std::set<std::filesystem::path> checkedDirectories;
    const auto active=std::filesystem::weakly_canonical(AssetPaths::ResolveAssetRoot());
    for(std::size_t i=0;i<types.size();++i){auto& type=types[i];if(type.name=="Scene Document")continue;const std::set<std::filesystem::path> roots=type.shaderRoot&&!ProjectManager::Active()?std::set<std::filesystem::path>{AssetPaths::ResolveShaderRoot(),AssetPaths::ResolveAssetRoot(),AssetImport::ProjectRoot()}:std::set<std::filesystem::path>{AssetPaths::ResolveAssetRoot(),AssetImport::ProjectRoot()};for(const auto& root:roots){const auto directory=root/type.folder;
        if(!std::filesystem::exists(directory))continue;
        if(checkedDirectories.insert(directory).second)ProjectContext::CheckNoLinks(directory);
        if(!type.folder.empty())dirs.insert(type.folder);
        for(std::filesystem::recursive_directory_iterator walk(directory,std::filesystem::directory_options::skip_permission_denied),end;walk!=end;++walk){const auto& item=*walk;
            // Iterator paths already belong to the checked root. Inspect each entry
            // once per refresh; resolving every ancestor for every type made a
            // 1000-file folder block the UI for tens of seconds. Never follow links.
            auto [safe,inserted]=safeEntries.try_emplace(item.path(),true);
            if(inserted){safe->second=!std::filesystem::is_symlink(item.symlink_status());
#ifdef _WIN32
                const auto attributes=GetFileAttributesW(item.path().c_str());safe->second=safe->second&&attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT);
#endif
            }
            if(!safe->second){if(item.is_directory())walk.disable_recursion_pending();continue;}
            if(item.is_directory()&&!std::filesystem::is_symlink(item.symlink_status()))dirs.insert(DirectoryKey(item.path().lexically_relative(root)));
            if(!item.is_regular_file()||std::filesystem::is_symlink(item.symlink_status()))continue;
            if(type.kind==ecs::AssetKind::Prefab&&!item.path().filename().string().ends_with(".prefab.json"))continue;
            if(type.kind==ecs::AssetKind::UIDocument&&!item.path().filename().string().ends_with(".ui.json"))continue;
            if(type.name=="UI Style"&&!item.path().filename().string().ends_with(".uistyle.json"))continue;
            if(type.name=="DataAsset"&&!item.path().filename().string().ends_with(".data.json"))continue;
            if(type.name=="DataTable"&&!item.path().filename().string().ends_with(".table.json"))continue;
            if(type.kind==ecs::AssetKind::PostFx&&!item.path().filename().string().ends_with(".postfx.json"))continue;
            if(type.kind==ecs::AssetKind::Material){
                const auto name=item.path().filename().string();
                if(name.ends_with(".data.json")||name.ends_with(".table.json")||name.ends_with(".prefab.json")||name.ends_with(".ui.json")||name.ends_with(".uistyle.json")||name.ends_with(".postfx.json"))continue;
            }
            if(type.kind==ecs::AssetKind::Material&&!item.path().filename().string().ends_with(".material.json")&&item.path().extension()!=".material"&&!DirectoryKey(item.path().lexically_relative(root)).starts_with("materials/"))continue;
            auto ext=item.path().extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
            if(type.name=="Unknown"){
                if(ext==".wtex"||ext==".wmesh"||ext==".tmp"||item.path().filename().wstring().starts_with(L"."))continue;
            }else if(std::find(type.extensions.begin(),type.extensions.end(),ext)==type.extensions.end())continue;
            auto key=AssetPaths::ToUtf8String(item.path().lexically_relative(root));std::replace(key.begin(),key.end(),'\\','/');std::error_code sizeError;const auto bytes=item.file_size(sizeError);ready.push_back({key,i,sizeError?std::nullopt:std::optional<std::uintmax_t>(bytes)});
            if(hiddenFallbacks.contains(key)&&std::filesystem::weakly_canonical(root)!=active&&!std::filesystem::exists(AssetImport::SafeTarget(active,key))){ready.pop_back();continue;}
            auto folder=std::filesystem::u8path(key).parent_path();while(!folder.empty()){dirs.insert(DirectoryKey(folder));folder=folder.parent_path();}
        }
    }
    }
    SceneCatalog scenes;scenes.Refresh({AssetPaths::ResolveAssetRoot(),AssetImport::ProjectRoot()});
    auto sceneType=std::find_if(types.begin(),types.end(),[](const auto& t){return t.name=="Scene Document";});
    if(sceneType!=types.end())for(const auto& entry:scenes.Entries()){
        ready.push_back({entry.key,std::size_t(sceneType-types.begin())});auto folder=std::filesystem::u8path(entry.key).parent_path();
        while(!folder.empty()){dirs.insert(DirectoryKey(folder));folder=folder.parent_path();}
    }
    std::set<std::string> known;for(const auto& entry:ready)if(types[entry.type].name!="Unknown")known.insert(entry.key);
    std::erase_if(ready,[&](const auto& entry){return types[entry.type].name=="Unknown"&&known.contains(entry.key);});
    std::sort(ready.begin(),ready.end(),[](auto& a,auto& b){return a.key<b.key;});ready.erase(std::unique(ready.begin(),ready.end(),[](const auto& a,const auto& b){return a.key==b.key&&a.type==b.type;}),ready.end());entries=std::move(ready);folders=std::move(dirs);++revision;
}
std::filesystem::path AssetCatalog::SafeMaterialPath(const std::string& key){
    if(std::any_of(key.begin(),key.end(),[](unsigned char c){return c<32;})||key.empty()||(!key.starts_with("materials/")&&!key.ends_with(".material.json"))||!ecs::ComponentRegistry::ValidateAssetKey(key,ecs::AssetKind::Material)||std::filesystem::u8path(key).extension()!=".json")throw std::runtime_error("Use a portable *.material.json key (legacy materials/*.json also supported)");
    const auto root=std::filesystem::weakly_canonical(AssetPaths::ResolveAssetRoot());auto path=AssetImport::SafeTarget(root,key);
    if(path.empty()||std::filesystem::is_symlink(std::filesystem::symlink_status(path)))throw std::runtime_error("Unsafe material output path");
    auto resolved=std::filesystem::weakly_canonical(path);auto relative=resolved.lexically_relative(root);
    if(relative.empty()||relative.is_absolute()||*relative.begin()=="..")throw std::runtime_error("Material path escapes asset root");return path;
}
bool AssetCatalog::ContainsReference(const nlohmann::json& j,const std::string& key){
    if(j.is_string()){
        auto value=j.get<std::string>();value=value.substr(0,value.find('#'));std::replace(value.begin(),value.end(),'\\','/');
        auto normalized=DirectoryKey(std::filesystem::u8path(value).lexically_normal());
        auto lower=[](std::string s){for(auto& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;};
#ifdef _WIN32
        const auto a=std::filesystem::u8path(normalized).wstring(),b=std::filesystem::u8path(key).wstring();
        return CompareStringOrdinal(a.c_str(),int(a.size()),b.c_str(),int(b.size()),TRUE)==CSTR_EQUAL;
#else
        return lower(normalized)==lower(key);
#endif
    }
    if(j.is_structured())for(const auto& value:j)if(ContainsReference(value,key))return true;return false;
}
bool AssetCatalog::CreateMaterial(const std::string& key,std::string* error){return Guard([&]{
    auto path=SafeMaterialPath(key);if(std::filesystem::exists(path))throw std::runtime_error("Asset already exists; choose another name");
    MaterialResource material;material.name=AssetPaths::ToUtf8String(std::filesystem::u8path(key).stem());material.placeholder=false;material.shadingModel=ShadingModel::PBR;
    std::string message;if(!MaterialLoader::Save(key,material,&message))throw std::runtime_error(message);created.insert(key);
},error);}
bool AssetCatalog::RenameMaterial(const std::string& from,const std::string& to,std::string* error){return Guard([&]{
    (void)SafeMaterialPath(from);(void)SafeMaterialPath(to);
    std::string message;if(!RenameAssetFile(from,to,&message))throw std::runtime_error(message);
},error);}
bool AssetCatalog::DeleteMaterial(const std::string& key,std::string* error){return Guard([&]{
    (void)SafeMaterialPath(key);std::string message;if(!DeleteAssetFile(key,&message))throw std::runtime_error(message);
},error);}

std::string AssetCatalog::FileSuffix(const std::string& key){
    if(key.ends_with(".data.json"))return ".data.json";
    if(key.ends_with(".table.json"))return ".table.json";
    if(key.ends_with(".prefab.json"))return ".prefab.json";
    if(key.ends_with(".material.json"))return ".material.json";
    if(key.ends_with(".uistyle.json"))return ".uistyle.json";
    if(key.ends_with(".ui.json"))return ".ui.json";
    return AssetPaths::ToUtf8String(std::filesystem::u8path(key).extension());
}
std::filesystem::path AssetCatalog::EditablePath(const std::string& key){
    const auto root=std::filesystem::weakly_canonical(AssetPaths::ResolveAssetRoot());
    auto path=AssetImport::SafeTarget(root,key);const auto resolved=std::filesystem::weakly_canonical(path);
    auto relative=resolved.lexically_relative(root);
    if(relative.empty()||relative.is_absolute()||*relative.begin()=="..")throw std::runtime_error("File escapes active Asset Root");
    auto extension=FileSuffix(key);
    const bool supported=extension==".lua"||extension==".ui.json"||extension==".uistyle.json"||extension==".prefab.json"||extension==".material.json"||
        (key.starts_with("materials/")&&(extension==".json"||extension==".material.json"||extension==".material"))||
        std::set<std::string>{".png",".jpg",".jpeg",".bmp",".tga",".dds",".hdr",".ttf",".otf",".data.json",".table.json"}.contains(extension);
    if(!supported)throw std::runtime_error("This asset requires its owning tool: shaders/model packages/generated resources are protected; Scene rename needs session and reference migration");
    // A texture may belong to a package outside models/. Do not detach its
    // relative model dependencies just because its extension is independently editable.
    for(auto parent=path.parent_path();parent!=root&&!parent.empty();parent=parent.parent_path())
        for(const auto& sibling:std::filesystem::directory_iterator(parent)){
            auto e=sibling.path().extension().string();for(auto& c:e)c=char(std::tolower(static_cast<unsigned char>(c)));
            if(e==".obj"||e==".gltf"||e==".glb"||e==".fbx")throw std::runtime_error("Model package contents require joint dependency migration; move is blocked");
        }
    const std::set<std::string> builtins={"materials/default_primitive.material.json","materials/stress.material.json","materials/pbr_textures.material.json",
        "textures/pbr_base.png","textures/pbr_normal.png","textures/pbr_orm.png","textures/pbr_control.jpg","textures/pbr_control.dds","textures/pbr_control_bc3.dds",
        "environments/studio.hdr","environments/uniform.hdr","environments/editor24_known_sun.hdr"};
    auto folded=key;for(auto& c:folded)c=char(std::tolower(static_cast<unsigned char>(c)));
    if(builtins.contains(folded))throw std::runtime_error("Built-in engine/diagnostic asset is protected");
    if(extension==".hdr"&&std::filesystem::exists(path.parent_path()/(path.stem().wstring()+L".sun.json")))throw std::runtime_error("HDR has coupled sun metadata; manage this package with its owning tool");
    if(!std::filesystem::is_regular_file(path))throw std::runtime_error("File is missing from the active Asset Root (no source fallback)");
#ifdef _WIN32
    const auto attrs=GetFileAttributesW(path.c_str());
    if(attrs==INVALID_FILE_ATTRIBUTES||(attrs&(FILE_ATTRIBUTE_READONLY|FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_SYSTEM)))throw std::runtime_error("Read-only, system or reparse-point file cannot be changed");
    HANDLE access=CreateFileW(path.c_str(),DELETE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(access==INVALID_HANDLE_VALUE)throw std::runtime_error("File is locked or access denied (Windows error "+std::to_string(GetLastError())+")");
    CloseHandle(access);
#else
    if((std::filesystem::status(path).permissions()&std::filesystem::perms::owner_write)==std::filesystem::perms::none)throw std::runtime_error("File is not writable");
#endif
    if(extension==".lua"&&(scripting::SafeAsset(key,".lua")!=resolved))throw std::runtime_error("Script resolver disagrees with active file");
    return path;
}
std::string AssetCatalog::RenameDestination(const std::string& key,const std::string& name){
    auto suffix=FileSuffix(key);std::string stem=name;
    if(stem.ends_with(suffix))stem.resize(stem.size()-suffix.size());
    if(stem.empty()||stem=="."||stem==".."||stem.back()=='.'||stem.back()==' '||stem.find_first_of("/\\")!=std::string::npos)throw std::runtime_error("Enter a non-empty filename, without folders or a trailing dot/space");
    auto parent=std::filesystem::u8path(key).parent_path();auto destination=DirectoryKey(parent/std::filesystem::u8path(stem+suffix));
    (void)AssetImport::SafeTarget(AssetPaths::ResolveAssetRoot(),destination);return destination;
}
AssetDependencyAudit AssetCatalog::AuditDependencies(const std::string& key){
    AssetDependencyAudit found;
    const bool image=std::set<std::string>{".png",".jpg",".jpeg",".bmp",".tga",".dds"}.contains(FileSuffix(key));
    std::set<std::filesystem::path> roots={std::filesystem::weakly_canonical(AssetPaths::ResolveAssetRoot()),std::filesystem::weakly_canonical(AssetImport::ProjectRoot())};
    if(auto project=ProjectManager::Active())roots.insert(project->SceneRoot());
    const auto filename=AssetPaths::ToUtf8String(std::filesystem::u8path(key).filename());
    for(const auto& root:roots)try{
        for(const auto& item:std::filesystem::recursive_directory_iterator(root)){
            const auto relative=DirectoryKey(item.path().lexically_relative(root));
            // Do not traverse/rely on linked or unreadable dependency files.
            (void)AssetImport::SafeTarget(root,relative);
            if(!item.is_regular_file())continue;
            auto ext=item.path().extension().string();
            for(auto& c:ext)c=char(std::tolower(static_cast<unsigned char>(c)));
            const bool json=ext==".json"||ext==".material"||ext==".wscene"||ext==".gltf";
            const bool text=ext==".lua"||ext==".obj"||ext==".mtl";
            if((ext==".fbx"||ext==".glb")&&image){found.incomplete.push_back(AssetPaths::ToUtf8String(item.path())+": opaque model dependencies cannot be audited safely");continue;}
            if(!json&&!text)continue;
            if(item.file_size()>16*1024*1024)throw std::runtime_error("Dependency file exceeds audit limit: "+relative);
            std::ifstream in(item.path(),std::ios::binary);if(!in)throw std::runtime_error("Cannot read dependency file: "+relative);
            std::string data((std::istreambuf_iterator<char>(in)),{});if(in.bad())throw std::runtime_error("Dependency read failed: "+relative);
            bool referenced=false;
            if(json){
                auto document=nlohmann::json::parse(data,nullptr,false);if(document.is_discarded())throw std::runtime_error("Invalid dependency JSON: "+relative);
                auto references=[&](auto&& self,const nlohmann::json& value)->bool{
                    if(ContainsReference(value,key))return true;
                    if(value.is_string()){
                        auto s=value.get<std::string>();s=s.substr(0,s.find('#'));std::replace(s.begin(),s.end(),'\\','/');
                        if(ext==".gltf"&&image&&s.find('%')!=std::string::npos)
                            throw std::runtime_error("Encoded glTF dependency needs package-level management: "+relative);
                        auto joined=(item.path().parent_path()/std::filesystem::u8path(s)).lexically_normal().lexically_relative(root);
                        return ContainsReference(AssetPaths::ToUtf8String(joined),key);
                    }
                    if(value.is_structured())for(const auto& child:value)if(self(self,child))return true;return false;
                };referenced=references(references,document);
            }else if(ext==".lua"){
                detail::LuaDependencies lua;
                try{lua=detail::AnalyzeLuaDependencies(data);}catch(const std::exception& failure){throw std::runtime_error("Lua dependency audit failed: "+relative+": "+failure.what());}
                for(const auto& value:lua.values)if(ContainsReference(value,key)){referenced=true;break;}
                if(key.ends_with(".lua"))for(const auto& warning:lua.moduleDynamic)found.dynamic.push_back(AssetPaths::ToUtf8String(item.path())+": "+warning);
                if(key.ends_with(".prefab.json"))for(const auto& warning:lua.dynamic)found.dynamic.push_back(AssetPaths::ToUtf8String(item.path())+": "+warning);
            }else{
                // Conservative text audit, never execute Lua or rewrite source.
                std::string lower=data,target=key,base=filename;for(auto* s:{&lower,&target,&base})for(auto& c:*s)c=char(std::tolower(static_cast<unsigned char>(c)));
                referenced=lower.find(target)!=std::string::npos||lower.find(base)!=std::string::npos;
            }
            if(referenced)found.confirmed.push_back(AssetPaths::ToUtf8String(item.path()));
        }
    }catch(const std::exception& e){found.incomplete.push_back("Dependency audit incomplete: "+std::string(e.what()));}
    return found;
}
std::vector<std::string> AssetCatalog::Dependencies(const std::string& key){
    auto audit=AuditDependencies(key);auto blockers=std::move(audit.confirmed);blockers.insert(blockers.end(),audit.incomplete.begin(),audit.incomplete.end());return blockers;
}
namespace {
void RequireNoDependencies(const std::string& key,bool acceptDynamicRisk){
    auto audit=AssetCatalog::AuditDependencies(key);auto dependencies=std::move(audit.confirmed);dependencies.insert(dependencies.end(),audit.incomplete.begin(),audit.incomplete.end());
    if(dependencies.empty()){
        if(audit.dynamic.empty()||acceptDynamicRisk)return;
        std::string message="Dynamic Lua dependency risk requires separate explicit confirmation:";for(auto& warning:audit.dynamic)message+="\n- "+warning;throw std::runtime_error(message);
    }
    std::string message="Asset operation blocked. Dependencies / incomplete checks:";for(auto& source:dependencies)message+="\n- "+source;throw std::runtime_error(message);
}
}
bool AssetCatalog::RenameAssetFile(const std::string& from,const std::string& to,std::string* error,bool acceptDynamicRisk){return Guard([&]{
    auto source=EditablePath(from);
    if(RenameDestination(from,AssetPaths::ToUtf8String(std::filesystem::u8path(to).filename()))!=to)throw std::runtime_error("Invalid destination filename");
    if(std::filesystem::u8path(from).parent_path()!=std::filesystem::u8path(to).parent_path()||FileSuffix(from)!=FileSuffix(to))throw std::runtime_error("Rename must preserve folder and complete extension");
    auto destination=AssetImport::SafeTarget(AssetPaths::ResolveAssetRoot(),to);
    if(std::filesystem::exists(destination))throw std::runtime_error("Destination already exists; nothing was overwritten");
    RequireNoDependencies(from,acceptDynamicRisk);source=EditablePath(from); // recheck immediately before mutation
#ifdef _WIN32
    if(!MoveFileExW(source.c_str(),destination.c_str(),MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Rename failed (Windows error "+std::to_string(GetLastError())+")");
#else
    std::filesystem::create_hard_link(source,destination);std::filesystem::remove(source);
#endif
    created.erase(from);hiddenFallbacks.insert(from);++revision;
},error);}
bool AssetCatalog::DeleteAssetFile(const std::string& key,std::string* error,bool acceptDynamicRisk){return Guard([&]{
    auto path=EditablePath(key);RequireNoDependencies(key,acceptDynamicRisk);path=EditablePath(key);
#ifdef _WIN32
    if(!DeleteFileW(path.c_str()))throw std::runtime_error("Delete failed (Windows error "+std::to_string(GetLastError())+")");
#else
    if(!std::filesystem::remove(path))throw std::runtime_error("File no longer exists");
#endif
    created.erase(key);hiddenFallbacks.insert(key);++revision;
},error);}
AssetMovePlan AssetCatalog::PlanMove(const std::vector<std::string>& sources,const std::string& folder){
    namespace fs=std::filesystem;
    const auto base=fs::weakly_canonical(AssetPaths::ResolveAssetRoot());
    const auto target=folder.empty()?base:AssetImport::SafeTarget(base,folder);
    ProjectContext::CheckNoLinks(target);
    if(!fs::is_directory(target))throw std::runtime_error("Destination folder is missing");
    if(sources.empty()||sources.size()>1024)throw std::runtime_error("Select 1..1024 resources");
    AssetMovePlan plan;plan.root=base;plan.sources=sources;plan.folder=folder;
    auto fold=[](std::string s){for(auto& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;};
    std::set<std::string> names;
    for(const auto& key:sources){
        auto source=AssetImport::SafeTarget(base,key);ProjectContext::CheckNoLinks(source);
        for(const auto& other:sources)if(key!=other&&fold(key).starts_with(fold(other)+"/"))throw std::runtime_error("Select the parent folder alone, without its children");
        auto to=DirectoryKey((target/source.filename()).lexically_relative(base));
        if(fold(to)==fold(key)||fold(to).starts_with(fold(key)+"/"))throw std::runtime_error("Cannot move a folder into itself or its descendants");
        if(!names.insert(fold(to)).second||fs::exists(AssetImport::SafeTarget(base,to)))throw std::runtime_error("Destination conflict: "+to);
        plan.roots.emplace_back(key,to);
        auto add=[&](const fs::path& file){
            auto from=DirectoryKey(file.lexically_relative(base));
            try{(void)EditablePath(from);RequireNoDependencies(from,false);}catch(const std::exception& e){throw std::runtime_error(from+": "+e.what());}
            if(plan.files.size()>=10000)throw std::runtime_error("Move exceeds 10000 files");
            const auto dest=fs::is_directory(source)?DirectoryKey(fs::u8path(to)/file.lexically_relative(source)):to;
            plan.files.emplace_back(from,dest);plan.stamps[from]={fs::file_size(file),fs::last_write_time(file)};
        };
        if(fs::is_directory(source)){
            for(const auto& entry:fs::recursive_directory_iterator(source)){
                ProjectContext::CheckNoLinks(entry.path());
                if(entry.is_regular_file())add(entry.path());
                else if(!entry.is_directory())throw std::runtime_error("Unsupported folder entry");
            }
        }else add(source);
    }
    std::sort(plan.files.begin(),plan.files.end());return plan;
}
bool AssetCatalog::ExecuteMove(const AssetMovePlan& plan,std::string* error){return Guard([&]{
    namespace fs=std::filesystem;
    if(PlanMove(plan.sources,plan.folder)!=plan)throw std::runtime_error("Files or dependencies changed; cancel and preview the move again");
    const auto base=AssetPaths::ResolveAssetRoot();
    const auto journal=AssetImport::SafeTarget(base,".whisp-move-"+ecs::NewSceneIdentity()+".json");
    nlohmann::json recovery={{"roots",plan.roots},{"completed",0},{"state","prepared"}};
    bool firstSave=true;
    auto save=[&]{std::string message;if(!SceneSerializer::WriteAtomic(journal,recovery,&message,!firstSave))throw std::runtime_error(message);firstSave=false;};
    save();std::size_t completed=0;
    auto move=[&](const std::string& from,const std::string& to){
        auto source=AssetImport::SafeTarget(base,from),destination=AssetImport::SafeTarget(base,to);
#ifdef _WIN32
        if(!MoveFileExW(source.c_str(),destination.c_str(),MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Move failed: "+from+" -> "+to+" (Windows "+std::to_string(GetLastError())+")");
#else
        if(fs::exists(destination))throw std::runtime_error("Destination appeared: "+to);
        fs::rename(source,destination);
#endif
    };
    try{
        for(const auto& [from,to]:plan.roots){recovery["pending"]=completed;save();move(from,to);++completed;recovery["completed"]=completed;recovery.erase("pending");save();}
    }catch(const std::exception& e){
        const std::string failure=e.what();std::string rollbackFailure;
        while(completed){const auto& [from,to]=plan.roots[completed-1];try{move(to,from);--completed;}catch(const std::exception& r){rollbackFailure=r.what();break;}}
        if(!rollbackFailure.empty()){
            recovery["state"]="recovery_required";recovery["completed"]=completed;recovery["failure"]=failure;recovery["rollbackFailure"]=rollbackFailure;
            try{save();}catch(...){}
            throw std::runtime_error(failure+"\nRollback incomplete: "+rollbackFailure+"\nRecovery journal: "+DirectoryKey(journal));
        }
        std::error_code ec;fs::remove(journal,ec);throw std::runtime_error(failure+"\nAll completed moves were rolled back.");
    }
    std::error_code ec;fs::remove(journal,ec);
    for(const auto& [from,to]:plan.files){created.erase(from);hiddenFallbacks.insert(from);}
    ++revision;
},error);}
bool AssetCatalog::MoveAssetFile(const std::string& from,const std::string& to,std::string* error,bool acceptDynamicRisk){return Guard([&]{
    auto source=EditablePath(from);
    const auto slash=from.find('/');
    if(slash==std::string::npos||!to.starts_with(from.substr(0,slash+1))||FileSuffix(from)!=FileSuffix(to))throw std::runtime_error("Move must preserve asset category and complete extension");
    auto destination=AssetImport::SafeTarget(AssetPaths::ResolveAssetRoot(),to);
    if(!std::filesystem::is_directory(destination.parent_path())||std::filesystem::exists(destination))throw std::runtime_error("Choose an existing folder and a fresh filename");
    RequireNoDependencies(from,acceptDynamicRisk);source=EditablePath(from);
#ifdef _WIN32
    if(!MoveFileExW(source.c_str(),destination.c_str(),MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Move failed (Windows error "+std::to_string(GetLastError())+")");
#else
    std::filesystem::create_hard_link(source,destination);std::filesystem::remove(source);
#endif
    created.erase(from);hiddenFallbacks.insert(from);++revision;
},error);}
}
