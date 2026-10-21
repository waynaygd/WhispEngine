#include "PostFxResource.h"
#include "ResourceManager.h"
#include "../core/AssetPaths.h"
#include <fstream>
#include <regex>
#include <set>
using Json=nlohmann::json;
unsigned PostFxParameter::Components()const {if(type=="float2")return 2;if(type=="float3")return 3;if(type=="float4")return 4;return 1;}
bool PostFxParameter::Valid(const Json& value)const {
    auto number=[&](const Json& v){if(!v.is_number())return false;double x=v.get<double>();return std::isfinite(x)&&x>=minimum&&x<=maximum;};
    if(type=="bool")return value.is_boolean();
    if(type=="int")return value.is_number_integer()&&number(value);
    if(Components()>1){if(!value.is_array()||value.size()!=Components())return false;for(auto& v:value)if(!number(v))return false;return true;}
    return number(value);
}
void to_json(Json& j,const PostFxOverride& v){j={{"resource",v.resource},{"id",v.id},{"enabled",v.enabled},{"weight",v.weight},{"parameters",v.parameters}};}
void from_json(const Json& j,PostFxOverride& v){v.resource=j.at("resource").get<std::string>();v.id=j.at("id").get<std::string>();v.enabled=j.value("enabled",true);v.weight=j.value("weight",1.f);v.parameters=j.value("parameters",Json::object());if(!v.parameters.is_object()||v.parameters.size()>64||v.id.empty()||v.id.size()>128||!std::isfinite(v.weight)||v.weight<0||v.weight>1)throw std::runtime_error("Invalid Custom PostFX override");PostFxLoader::SafePath(v.resource);}
std::filesystem::path PostFxLoader::SafePath(const std::string& key){
    auto p=std::filesystem::u8path(key);if(key.empty()||key.size()>1024||p.is_absolute()||p.has_root_name()||key.find_first_of(":\\#")!=std::string::npos||key.find('\0')!=std::string::npos)throw std::runtime_error("PostFX paths must be relative to Project Asset Root");
    for(auto& part:p)if(part=="..")throw std::runtime_error("PostFX paths cannot contain ..");
    auto root=std::filesystem::weakly_canonical(AssetPaths::ResolveAssetRoot());auto full=std::filesystem::weakly_canonical(root/p);auto relative=full.lexically_relative(root);
    if(relative.empty()||relative.is_absolute()||*relative.begin()=="..")throw std::runtime_error("PostFX path escapes Project Asset Root");return full;
}
ResourceLoadResult<PostFxResource> PostFxLoader::Load(const std::string& key,const std::filesystem::path&){
    ResourceLoadResult<PostFxResource> result;
    try{
        auto path=SafePath(key);if(!key.ends_with(".postfx.json"))throw std::runtime_error("Expected .postfx.json");
        std::ifstream file(path);if(!file)throw std::runtime_error("Missing manifest: "+key);Json j;file>>j;
        if(j.at("version")!=1)throw std::runtime_error("Unsupported PostFX manifest version");
        auto d=std::make_shared<PostFxDefinition>();d->key=key;d->origin=AssetPaths::ToUtf8String(path);d->id=j.at("id").get<std::string>();d->name=j.at("name").get<std::string>();d->description=j.value("description",std::string());d->order=j.value("order",0);
        if(d->id.empty()||d->id.size()>128||d->name.empty()||d->order<-1000000||d->order>1000000)throw std::runtime_error("Invalid PostFX identity/order");
        if(j.at("stage")!="AfterTonemap")throw std::runtime_error("Only AfterTonemap is supported");
        auto shader=j.at("shader").get<std::string>();SafePath(shader);d->shader=AssetPaths::ToUtf8String(std::filesystem::u8path(key).parent_path()/std::filesystem::u8path(shader));std::replace(d->shader.begin(),d->shader.end(),char(92),char(47));auto shaderPath=SafePath(d->shader);
        if(shaderPath.extension()!=".hlsl")throw std::runtime_error("Expected HLSL shader");std::ifstream code(shaderPath,std::ios::binary);if(!code)throw std::runtime_error("Missing HLSL: "+d->shader);d->source.assign(std::istreambuf_iterator<char>(code),{});if(d->source.size()>1024*1024)throw std::runtime_error("HLSL source exceeds 1 MB");
        const auto& params=j.at("parameters");if(!params.is_array()||params.size()>16)throw std::runtime_error("PostFX supports at most 16 parameters");std::set<std::string> names;
        for(const auto& p:params){PostFxParameter v;v.name=p.at("name").get<std::string>();v.type=p.at("type").get<std::string>();v.initial=p.at("default");
            if(!std::regex_match(v.name,std::regex("[a-z][a-zA-Z0-9_]{0,63}"))||!names.insert(v.name).second)throw std::runtime_error("Invalid/duplicate parameter name");
            if(v.type!="float"&&v.type!="float2"&&v.type!="float3"&&v.type!="float4"&&v.type!="int"&&v.type!="bool")throw std::runtime_error("Invalid parameter type: "+v.type);
            v.minimum=p.value("min",0.f);v.maximum=p.value("max",1.f);if(!std::isfinite(v.minimum)||!std::isfinite(v.maximum)||v.minimum>v.maximum||std::abs(v.minimum)>1000000||std::abs(v.maximum)>1000000||!v.Valid(v.initial))throw std::runtime_error("Invalid parameter default/range: "+v.name);d->parameters.push_back(v);
        }
        result.data.definition=d;result.success=true;
    }catch(const std::exception& e){result.errorMessage=e.what();}return result;
}
PostFxFrame ResolvePostFx(const std::vector<PostFxLayer>& layers,ResourceManager& resources,const PostFxRuntimeOverrides* runtime,float time,float delta){
    PostFxFrame frame;frame.time=time;frame.delta=delta;std::map<std::string,PostFxPass> passes;
    auto obtain=[&](const std::string& key,const std::string& id)->PostFxPass*{
        auto asset=resources.Get<PostFxResource>(key);if(!asset){asset=resources.Load<PostFxResource>(key);resources.WatchForHotReload<PostFxResource>(key);}auto d=asset->GetData().definition;
        if(!d||d->id!=id||resources.PostFxDuplicate(id))return nullptr;
        auto& pass=passes[id];if(!pass.definition){pass.definition=d;for(auto& p:d->parameters)pass.parameters[p.name]=p.initial;}return &pass;
    };
    for(auto& layer:layers){auto* pass=obtain(layer.effect.resource,layer.effect.id);if(!pass)continue;float alpha=std::clamp(layer.influence,0.f,1.f);float target=layer.effect.enabled?std::clamp(layer.effect.weight,0.f,1.f):0;
        pass->weight+=(target-pass->weight)*alpha;
        for(auto& p:pass->definition->parameters){auto it=layer.effect.parameters.find(p.name);if(it==layer.effect.parameters.end()||!p.Valid(*it))continue;auto& value=pass->parameters[p.name];
            if(p.type=="int"||p.type=="bool"){if(alpha>=.5f)value=*it;}
            else if(p.Components()==1)value=value.get<float>()+(it->get<float>()-value.get<float>())*alpha;
            else for(unsigned c=0;c<p.Components();++c)value[c]=value[c].get<float>()+((*it)[c].get<float>()-value[c].get<float>())*alpha;
        }
    }
    if(runtime)for(const auto& [id,override]:*runtime){auto key=resources.FindPostFx(id);if(key.empty())continue;auto* pass=obtain(key,id);if(!pass)continue;if(override.weight)pass->weight=*override.weight;for(auto& p:pass->definition->parameters){auto it=override.parameters.find(p.name);if(it!=override.parameters.end()&&p.Valid(*it))pass->parameters[p.name]=*it;}}
    for(auto& [id,pass]:passes)if(std::isfinite(pass.weight)&&pass.weight>0)frame.passes.push_back(std::move(pass));
    std::sort(frame.passes.begin(),frame.passes.end(),[](auto& a,auto& b){return std::tie(a.definition->order,a.definition->id)<std::tie(b.definition->order,b.definition->id);});if(frame.passes.size()>32)frame.passes.resize(32);return frame;
}
