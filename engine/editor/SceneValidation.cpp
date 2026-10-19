#include "SceneValidation.h"
#include "../core/AssetPaths.h"
#include "../ecs/ComponentRegistry.h"
#include "../ecs/components/SceneIdentityComponent.h"
#include "../ecs/components/TagComponent.h"
#include "../ecs/components/PrefabInstanceComponent.h"
#include "../ecs/components/LuaScriptComponent.h"
#include "../ecs/components/PlayerStartComponent.h"
#include "../scripting/EntityReference.h"
#include "../scripting/LuaRuntime.h"
#include "../scripting/Prefab.h"
#include "../resources/ResourceManager.h"
#include <fstream>
#include <map>
#include <set>
#include <algorithm>
#include <cctype>

namespace editor {
using namespace ecs;
using Json=nlohmann::json;
const char* ValidationSeverityName(ValidationSeverity severity){
    switch(severity){case ValidationSeverity::Error:return "Error";case ValidationSeverity::Warning:return "Warning";default:return "Info";}
}
bool SceneValidationIssue::Matches(const std::string& query,int filter)const{
    if(filter>=0&&int(severity)!=filter)return false;
    const auto text=entityName+" "+stableId+" "+resource+" "+title+" "+description+" "+component+" "+field;
    return std::search(text.begin(),text.end(),query.begin(),query.end(),[](unsigned char a,unsigned char b){return std::tolower(a)==std::tolower(b);})!=text.end();
}
std::size_t SceneValidationReport::Count(ValidationSeverity severity)const{
    return std::count_if(issues.begin(),issues.end(),[&](const auto& issue){return issue.severity==severity;});
}
namespace {
struct ResourceCheck {
    std::filesystem::path path;
    std::string title,error;
    ValidationSeverity severity=ValidationSeverity::Error;
    Json prefab;
    std::vector<std::pair<AssetKind,std::string>> dependencies;
};
struct Validator {
    const World& world;
    scripting::LuaRuntime* lua;
    ResourceManager* resources;
    SceneValidationReport report;
    std::map<std::string,std::vector<Entity>> ids;
    std::map<std::pair<AssetKind,std::string>,ResourceCheck> files;
    Entity entity{};
    std::string component;
    void Add(ValidationSeverity severity,const std::string& title,const std::string& message,
             const std::string& field={},const std::string& resource={},const std::string& fix={}){
        SceneValidationIssue issue;issue.severity=severity;issue.title=title;issue.description=message;
        issue.entity=entity;issue.component=component;issue.field=field;issue.resource=resource;issue.recommendation=fix;
        if(auto* id=world.GetComponent<SceneIdentityComponent>(entity))issue.stableId=id->value;
        if(auto* tag=world.GetComponent<TagComponent>(entity))issue.entityName=tag->name;
        report.issues.push_back(std::move(issue));
    }
    void Error(const std::string& title,const std::string& message,const std::string& field={}){
        Add(ValidationSeverity::Error,title,message,field,{},"Repair the reference or clear an optional field, then Validate Scene again.");
    }
    Entity Reference(const Json& value,const std::string& field,bool required=false){
        if(value.is_null()&&!required)return {};
        if(!scripting::ValidEntityReference(value)||value.is_null()){
            Error("Invalid Entity Reference","Expected null or a single $entity persistent ID.",field);return {};
        }
        auto i=ids.find(value.at("$entity").get<std::string>());
        if(i==ids.end()||i->second.empty()){Error("Broken Entity Reference","The referenced persistent ID does not exist in this World: "+value.at("$entity").get<std::string>(),field);return {};}
        if(i->second.size()!=1){Error("Ambiguous Entity Reference","Several entities share the referenced persistent ID.",field);return {};}
        return i->second.front();
    }
    void References(const Json& value,const std::string& field,unsigned depth,std::size_t& remaining){
        if(!remaining||depth>32){Error("Reference inspection limit","Serialized fields exceed the supported inspection depth or size.",field);return;}
        --remaining;
        if(value.is_object()&&value.contains("$entity")){Reference(value,field);return;}
        if(value.is_object()||value.is_array())for(auto it=value.begin();it!=value.end();++it){
            if(!remaining){Error("Reference inspection limit","Too many serialized values to inspect safely.",field);break;}
            References(it.value(),field+"."+(value.is_object()?it.key():std::to_string(it-value.begin())),depth+1,remaining);
        }
    }
    template<class T> std::string CachedError(const std::string& key){
        if(!resources)return {};
        auto cached=resources->Get<T>(key);
        return cached&&cached->HasError()?cached->GetErrorMessage():std::string();
    }
    ResourceCheck CheckFile(AssetKind kind,const std::string& key){
        ResourceCheck result;
        if((kind==AssetKind::Mesh&&key=="defaults/mesh")||(kind==AssetKind::Material&&key=="defaults/material")||
           (kind==AssetKind::Shader&&key=="defaults/shader")||(kind==AssetKind::Texture&&key=="defaults/texture"))return result;
        std::string error;
        if(key.find('\0')!=std::string::npos||!ComponentRegistry::ValidateAssetKey(key,AssetKind::None,&error)){
            result.title="Invalid Resource Path";result.error=error.empty()?"Resource path contains a null character.":error;return result;
        }
        auto fileKey=key;
        const auto embedded=key.find("#embedded/");
        if(embedded!=std::string::npos){
            fileKey=key.substr(0,embedded);auto index=key.substr(embedded+10);
            if(kind!=AssetKind::Texture||index.empty()||index.find_first_not_of("0123456789")!=std::string::npos){result.title="Invalid Embedded Texture";result.error="Expected a model asset followed by #embedded/<index>.";return result;}
        }
        result.path=kind==AssetKind::Shader?AssetPaths::ResolveShaderPath(fileKey):AssetPaths::ResolveAssetPath(fileKey);
        if(result.path.empty()||!std::filesystem::is_regular_file(result.path)){
            result.title=kind==AssetKind::Prefab?"Missing Prefab Source":kind==AssetKind::Mesh?"Missing Mesh":"Missing Resource";
            result.error="File is absent or outside the active resource root.";return result;
        }
        std::ifstream stream(result.path,std::ios::binary);
        if(!stream){result.title="Unavailable Resource";result.error="File exists but cannot be opened for reading.";return result;}
        if(embedded==std::string::npos&&!ComponentRegistry::ValidateAssetKey(key,kind,&error)){
            result.title="Unsupported Resource Format";result.error="File exists, but its extension is not supported for this field.";result.severity=ValidationSeverity::Warning;return result;
        }
        if(kind==AssetKind::Prefab)result.prefab=scripting::ReadPrefab(key);
        if(kind==AssetKind::Material){
            if(std::filesystem::file_size(result.path)>1024*1024)throw std::runtime_error("Material exceeds the 1 MiB inspection limit.");
            Json data;stream>>data;auto parsed=MaterialLoader::Parse(key,data);
            if(!parsed.success)throw std::runtime_error(parsed.errorMessage);
            const auto& m=parsed.data;
            result.dependencies.emplace_back(AssetKind::Shader,m.shaderPath);
            for(const auto* texture:{&m.texturePath,&m.normalTexturePath,&m.metallicRoughnessTexturePath,&m.occlusionTexturePath,&m.emissiveTexturePath})
                if(!texture->empty())result.dependencies.emplace_back(AssetKind::Texture,*texture);
        }
        std::string cached;
        switch(kind){
        case AssetKind::Mesh:cached=CachedError<MeshResource>(key);break;
        case AssetKind::Texture:cached=CachedError<TextureResource>(key);break;
        case AssetKind::Material:cached=CachedError<MaterialResource>(key);break;
        case AssetKind::Shader:cached=CachedError<ShaderResource>(key);break;
        case AssetKind::Environment:cached=CachedError<EnvironmentResource>(key);break;
        default:break;
        }
        // Unrequested/loading resources are healthy. A cached failure may predate a file repair.
        if(!cached.empty()){result.title="Cached Resource Load Failed";result.error=cached;result.severity=ValidationSeverity::Warning;}
        return result;
    }
    const ResourceCheck& File(AssetKind kind,const std::string& key){
        auto [it,inserted]=files.try_emplace({kind,key});
        if(inserted)try{it->second=CheckFile(kind,key);}catch(const std::exception& e){it->second.title=kind==AssetKind::Prefab?"Invalid Prefab Source":"Invalid Resource";it->second.error=e.what();}
        return it->second;
    }
    void Asset(AssetKind kind,const std::string& key,const std::string& field,bool required=false){
        if(key.empty()){if(required)Error("Missing Resource Reference","This field requires a resource path.",field);return;}
        const auto& check=File(kind,key);
        if(!check.error.empty())Add(check.severity,check.title,check.error,field,key,"Check the path and source file; reload a failed cached resource, then Validate Scene again.");
        for(const auto& [dependency,path]:check.dependencies)Asset(dependency,path,field+" -> "+key);
    }
    void Script(const LuaScriptComponent& script){
        if(!script.properties.is_object()||script.properties.size()>128){Error("Invalid Lua Fields","Script fields must be an object containing at most 128 overrides.","properties");return;}
        const auto& file=script.scriptPath.empty()?ResourceCheck{}:File(AssetKind::LuaScript,script.scriptPath);
        if(file.path.empty()||!file.error.empty())return;
        if(std::filesystem::file_size(file.path)>256*1024){Error("Lua Script Too Large","The script exceeds LuaRuntime's 256 KiB limit.","scriptPath");return;}
        std::ifstream stream(file.path,std::ios::binary);std::string source((std::istreambuf_iterator<char>(stream)),{});
        auto schema=lua?lua->CachedFields(script.scriptPath,source):std::nullopt;
        if(!schema){Add(ValidationSeverity::Info,"Lua Metadata Unavailable","Field types cannot be checked without executing this script. Entity references were checked. Open the script normally to obtain metadata, then revalidate.","properties",script.scriptPath);return;}
        for(auto it=script.properties.begin();it!=script.properties.end();++it){
            const auto field="properties."+it.key();
            if(!schema->contains(it.key()))Add(ValidationSeverity::Warning,"Unknown Lua Field","This override is not declared by the current script metadata.",field,script.scriptPath,"Remove the unused override or restore its field declaration.");
            else {
                const auto& type=schema->at(it.key());
                if(!scripting::LuaRuntime::ValidFieldValue(type,it.value()))Add(ValidationSeverity::Error,"Invalid Lua Field Value","Value does not match the declared type or range: "+type.value("type",std::string()),field,script.scriptPath,"Assign a value matching the script's declared field type and bounds.");
                else if(type.value("type",std::string())=="asset")Asset(AssetKind::None,it.value().get<std::string>(),field);
            }
        }
    }
    void Prefab(const PrefabInstanceComponent& link){
        if(!link.members.is_object()||link.members.empty()){Error("Missing Prefab Local IDs","The instance has no valid local-to-scene member mapping.","members");return;}
        const auto& source=File(AssetKind::Prefab,link.asset);
        std::set<std::string> expected,used;
        if(!source.prefab.is_null())for(const auto& node:source.prefab.at("entities"))expected.insert(node.at("id").get<std::string>());
        if(!source.prefab.is_null()&&!link.members.contains(source.prefab.at("root").get<std::string>()))
            Error("Missing Prefab Root ID","The source root local ID is missing from this instance mapping.","members");
        for(auto it=link.members.begin();it!=link.members.end();++it){
            const auto field="members."+it.key();
            if(it.key().empty())Error("Missing Prefab Local ID","A prefab member has an empty local ID.",field);
            const auto member=Reference(it.value(),field,true);
            if(!member.IsValid())continue;
            const auto id=it.value().at("$entity").get<std::string>();
            if(!used.insert(id).second)Error("Duplicate Prefab Member","Several local IDs map to the same scene entity.",field);
            std::set<std::uint32_t> visited;auto parent=member;
            while(world.IsAlive(parent)&&parent!=entity&&visited.insert(parent.index).second)parent=world.Parent(parent);
            if(parent!=entity)Error("Foreign Prefab Member","Mapped entity is outside this instance's hierarchy.",field);
            if(!source.prefab.is_null()&&it.key()==source.prefab.at("root").get<std::string>()&&member!=entity)Error("Prefab Root Mismatch","The source root local ID must map to this instance root.",field);
            if(!source.prefab.is_null()&&!expected.erase(it.key()))Add(ValidationSeverity::Warning,"Prefab Member Not In Source","The local ID is absent from the source; this may be a local structural change.",field,link.asset,"Review the instance and its source; validation never applies or reverts changes.");
        }
        for(const auto& local:expected)Add(ValidationSeverity::Warning,"Prefab Source Member Not Mapped","Source local ID is absent from the instance: "+local,"members",link.asset,"Review possible source updates or intentional local removals.");
    }
    SceneValidationReport Run(const SceneSettings& settings){
        report.validated=true;report.world=world.GetIdentity();
        for(auto e:world.Entities())if(auto* id=world.GetComponent<SceneIdentityComponent>(e);id&&!id->value.empty())ids[id->value].push_back(e);
        try{world.ValidateHierarchy();}catch(const std::exception& e){component="Hierarchy";Error("Invalid Hierarchy",e.what());}
        for(auto e:world.Entities()){
            entity=e;component="SceneIdentity";
            auto* id=world.GetComponent<SceneIdentityComponent>(e);
            if(!id||id->value.empty())Add(ValidationSeverity::Warning,"Missing Persistent ID","Entity has no persistent scene ID; serialized references cannot target it.");
            else if(id->value.size()>128||id->value.find('\0')!=std::string::npos)Error("Invalid Persistent ID","Persistent IDs must contain 1 to 128 characters and no null character.","value");
            else if(ids.at(id->value).size()>1)Error("Duplicate Persistent ID","Persistent ID is shared by several entities.","value");
            for(const auto& descriptor:ComponentRegistry::Builtins().Components())if(descriptor.has(world,e)){
                component=descriptor.key;
                try {
                    for(const auto& property:descriptor.properties){
                        if(property.kind!=PropertyKind::Asset&&property.kind!=PropertyKind::AssetList)continue;
                        auto value=property.get(world,e);
                        if(auto* key=std::get_if<std::string>(&value))Asset(property.asset,*key,property.key,
                            (component=="MeshRenderer"&&property.key=="meshPath")||(component=="PrefabInstance"&&property.key=="asset")||(component=="LuaScript"&&property.key=="scriptPath"&&world.GetComponent<LuaScriptComponent>(e)->enabled)||(component=="PlayerStart"&&property.key=="controllerScript"&&world.GetComponent<PlayerStartComponent>(e)->enabled));
                        else if(auto* keys=std::get_if<std::vector<std::string>>(&value))for(std::size_t n=0;n<keys->size();++n)Asset(property.asset,(*keys)[n],property.key+"["+std::to_string(n)+"]");
                    }
                    auto serialized=descriptor.serialize(world,e);
                    if(component=="PrefabInstance")serialized.erase("members"); // Specialized local-to-scene mapping check below.
                    std::size_t remaining=8192;References(serialized,"",0,remaining);
                }catch(const std::exception& error){Error("Invalid Component Data",error.what());}
            }
            if(auto* script=world.GetComponent<LuaScriptComponent>(e)){
                component="LuaScript";try{Script(*script);}catch(const std::exception& error){Error("Invalid Lua Fields",error.what());}
            }
            if(auto* prefab=world.GetComponent<PrefabInstanceComponent>(e)){
                component="PrefabInstance";try{Prefab(*prefab);}catch(const std::exception& error){Error("Invalid Prefab Link",error.what());}
            }
        }
        entity={};component="Scene Settings";
        if(settings.environment.enabled)Asset(AssetKind::Environment,settings.environment.assetPath,"environment.assetPath",true);
        if(settings.background.background==EditorBackground::Cubemap)Asset(AssetKind::Environment,settings.background.useIblEnvironment?settings.environment.assetPath:settings.background.cubemap,"background.cubemap",true);
        return std::move(report);
    }
};
}
SceneValidationReport ValidateScene(const World& world,const SceneSettings& settings,scripting::LuaRuntime* metadata,ResourceManager* resources){
    return Validator{world,metadata,resources}.Run(settings);
}
}
