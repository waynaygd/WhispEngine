#define NOMINMAX
#include "MaterialLoader.h"
#include "../../core/AssetPaths.h"
#include <cmath>
#include <fstream>
#include <set>
#ifdef _WIN32
#include <Windows.h>
#endif
namespace {
std::string SafeKey(const std::string& path,bool shader) {
    if(path.empty())return {};
    const std::filesystem::path p=std::filesystem::u8path(path);
    if(p.is_absolute()||p.has_root_name()||path.find(':')!=std::string::npos)throw std::runtime_error("absolute paths and URIs are not material resource keys");
    const auto key=shader?AssetPaths::NormalizeShaderKey(p):AssetPaths::NormalizeAssetKey(p);
    if(key.empty())throw std::runtime_error("material resource path escapes its root");
    return key;
}
}
ResourceLoadResult<MaterialResource> MaterialLoader::Parse(const std::string& key,const nlohmann::json& j) {
    ResourceLoadResult<MaterialResource> result;
    try {
        if(!j.is_object())throw std::runtime_error("material must be a JSON object");
        if(j.contains("version")&&!j.at("version").is_number_integer())throw std::runtime_error("version must be an integer");
        const int version=j.value("version",1);
        if(version!=1&&version!=2&&version!=3&&version!=4)throw std::runtime_error("unsupported material schema version");
        MaterialResource m;
        m.name=j.value("name",std::string("UnnamedMaterial"));m.sourcePath=key;
        m.shaderPath=SafeKey(j.value("shaderPath",m.shaderPath),true);
        if(m.shaderPath.empty())throw std::runtime_error("shaderPath must not be empty");
        m.texturePath=SafeKey(j.value("texturePath",std::string()),false);
        m.normalTexturePath=SafeKey(j.value("normalTexturePath",std::string()),false);
        m.metallicRoughnessTexturePath=SafeKey(j.value("metallicRoughnessTexturePath",std::string()),false);
        m.occlusionTexturePath=SafeKey(j.value("occlusionTexturePath",std::string()),false);
        m.emissiveTexturePath=SafeKey(j.value("emissiveTexturePath",std::string()),false);
        m.normalScale=j.value("normalScale",1.f);m.occlusionStrength=j.value("occlusionStrength",1.f);
        if(!std::isfinite(m.normalScale)||m.normalScale<0||m.normalScale>16||!std::isfinite(m.occlusionStrength)||m.occlusionStrength<0||m.occlusionStrength>1)throw std::runtime_error("invalid normalScale [0,16] or occlusionStrength [0,1]");
        if(j.contains("textureTexCoords")) {
            const auto& uv=j.at("textureTexCoords");
            if(!uv.is_array()||uv.size()!=4)throw std::runtime_error("textureTexCoords must contain four integers");
            for(int i=0;i<4;++i) {
                if(!uv[i].is_number_integer())throw std::runtime_error("textureTexCoords must contain integers");
                m.textureTexCoords[i]=uv[i].get<int>();
                if(m.textureTexCoords[i]<0||m.textureTexCoords[i]>7)throw std::runtime_error("invalid UV set");
                if(m.textureTexCoords[i]!=0)m.diagnostics+="Unsupported UV set: slot "+std::to_string(i)+" disabled; ";
            }
        }
        if(j.contains("baseColor")) {
            const auto& color=j.at("baseColor");
            if(!color.is_array()||color.size()!=4)throw std::runtime_error("baseColor must contain four numbers");
            for(int i=0;i<4;++i) {
                if(!color[i].is_number())throw std::runtime_error("baseColor must contain numbers");
                m.baseColor[i]=color[i].get<float>();
                if(!std::isfinite(m.baseColor[i])||m.baseColor[i]<0||m.baseColor[i]>1)throw std::runtime_error("baseColor must be finite in [0,1]");
            }
        }
        m.metallic=j.value("metallic",0.f);m.roughness=j.value("roughness",.5f);
        if(!std::isfinite(m.metallic)||m.metallic<0||m.metallic>1||!std::isfinite(m.roughness)||m.roughness<0||m.roughness>1)throw std::runtime_error("metallic/roughness must be finite in [0,1]");
        const auto shading=j.value("shadingModel",std::string("Lambert"));
        if(shading=="PBR")m.shadingModel=ShadingModel::PBR;
        else if(shading!="Lambert")throw std::runtime_error("unknown shadingModel");
        const auto mode=j.value("surfaceMode",std::string("Opaque"));
        if(mode=="Opaque")m.surfaceMode=MaterialSurfaceMode::Opaque;
        else if(mode=="Masked")m.surfaceMode=MaterialSurfaceMode::Masked;
        else if(mode=="Transparent")m.surfaceMode=MaterialSurfaceMode::Transparent;
        else throw std::runtime_error("unknown surfaceMode");
        m.alphaCutoff=j.value("alphaCutoff",.5f);
        if(!std::isfinite(m.alphaCutoff)||m.alphaCutoff<0||m.alphaCutoff>1)throw std::runtime_error("alphaCutoff must be finite in [0,1]");
        m.doubleSided=j.value("doubleSided",false);m.castShadows=j.value("castShadows",true);m.receiveShadows=j.value("receiveShadows",true);
        const std::set<std::string> fields={"normalTexturePath","metallicRoughnessTexturePath","occlusionTexturePath","emissiveTexturePath","normalScale","occlusionStrength","textureTexCoords","metallic","roughness","shadingModel","version","name","shaderPath","texturePath","baseColor","surfaceMode","alphaCutoff","doubleSided","castShadows","receiveShadows"};
        for(auto it=j.begin();it!=j.end();++it)if(!fields.count(it.key()))m.diagnostics+="Ignored unknown field: "+it.key()+"; ";
        if(m.surfaceMode!=MaterialSurfaceMode::Opaque)m.diagnostics+="Surface mode is preserved but rendering is unsupported; object will be omitted.";
        m.placeholder=false;result.success=true;result.data=std::move(m);
    } catch(const std::exception& e) {result.errorMessage=std::string("Invalid material: ")+e.what();}
    return result;
}
ResourceLoadResult<MaterialResource> MaterialLoader::Load(const std::string& key,const std::filesystem::path& path) {
    ResourceLoadResult<MaterialResource> result;
    try {
        if(key.empty())throw std::runtime_error("empty material key");
        std::ifstream stream(path);if(!stream)throw std::runtime_error("material file unavailable");
        nlohmann::json j;stream>>j;return Parse(key,j);
    } catch(const std::exception& e) {result.errorMessage=e.what();return result;}
}
nlohmann::json MaterialLoader::Serialize(const MaterialResource& m) {
    if(m.shadingModel!=ShadingModel::Lambert&&m.shadingModel!=ShadingModel::PBR)throw std::runtime_error("unknown shadingModel");
    return {{"version",4},{"normalTexturePath",m.normalTexturePath},{"metallicRoughnessTexturePath",m.metallicRoughnessTexturePath},{"occlusionTexturePath",m.occlusionTexturePath},{"emissiveTexturePath",m.emissiveTexturePath},{"normalScale",m.normalScale},{"occlusionStrength",m.occlusionStrength},{"textureTexCoords",m.textureTexCoords},{"metallic",m.metallic},{"roughness",m.roughness},{"shadingModel",m.shadingModel==ShadingModel::PBR?"PBR":"Lambert"},{"name",m.name},{"shaderPath",m.shaderPath},{"texturePath",m.texturePath},
        {"baseColor",{m.baseColor[0],m.baseColor[1],m.baseColor[2],m.baseColor[3]}},
        {"surfaceMode",m.surfaceMode==MaterialSurfaceMode::Opaque?"Opaque":m.surfaceMode==MaterialSurfaceMode::Masked?"Masked":"Transparent"},
        {"alphaCutoff",m.alphaCutoff},{"doubleSided",m.doubleSided},{"castShadows",m.castShadows},{"receiveShadows",m.receiveShadows}};
}
bool MaterialLoader::Save(const std::string& key,const MaterialResource& m,std::string* error) {
    std::filesystem::path temporary;
    try {
        const auto safe=SafeKey(key,false);if(safe.empty())throw std::runtime_error("empty output key");
        if(std::filesystem::path(safe).extension()!=".json")throw std::runtime_error("material asset output must have a .json extension");
        const auto path=AssetPaths::ResolveAssetOutputPath(std::filesystem::u8path(safe));
        if(path.empty())throw std::runtime_error("invalid asset output root");
        const auto j=Serialize(m);const auto validated=Parse(safe,j);
        if(!validated.success)throw std::runtime_error(validated.errorMessage);
        const auto root=std::filesystem::weakly_canonical(AssetPaths::ResolveAssetRoot());
        auto ancestor=path.parent_path();
        while(!std::filesystem::exists(ancestor)) {
            const auto next=ancestor.parent_path();
            if(next==ancestor||next.empty())throw std::runtime_error("output parent cannot be resolved");
            ancestor=next;
        }
        const auto parent=(std::filesystem::canonical(ancestor)/path.parent_path().lexically_relative(ancestor)).lexically_normal();
        const auto relative=(parent/path.filename()).lexically_relative(root);
        if(relative.empty()||relative.is_absolute()||*relative.begin()=="..")throw std::runtime_error("output escapes asset root (including symlinks)");
        if(std::filesystem::is_symlink(std::filesystem::symlink_status(path)))throw std::runtime_error("material output must not be a symlink");
        std::filesystem::create_directories(parent);
        temporary=path;temporary+=".saving";
        if(std::filesystem::is_symlink(std::filesystem::symlink_status(temporary))){temporary.clear();throw std::runtime_error("temporary material path must not be a symlink");}
        {std::ofstream stream(temporary,std::ios::binary|std::ios::trunc);if(!stream)throw std::runtime_error("cannot open temporary material file");stream<<j.dump(2)<<'\n';stream.flush();if(!stream)throw std::runtime_error("material write failed");}
#ifdef _WIN32
                bool replaced=false;DWORD replacementError=0;
        for(unsigned attempt=0;attempt<6;++attempt) {
            if(MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){replaced=true;break;}
            replacementError=GetLastError();
            if(replacementError!=ERROR_SHARING_VIOLATION&&replacementError!=ERROR_LOCK_VIOLATION)break;
            // A watcher/virus scanner may briefly hold the newly written file.
            // Retry the atomic rename; never fall back to truncating the asset.
            if(attempt<5)Sleep(2u<<attempt);
        }
        if(!replaced)throw std::runtime_error("atomic material replacement failed, Win32 error "+std::to_string(replacementError));
#else
        std::filesystem::rename(temporary,path);
#endif
        if(error)error->clear();return true;
    } catch(const std::exception& e) {if(error)*error=e.what();if(!temporary.empty()){std::error_code ec;std::filesystem::remove(temporary,ec);}return false;}
}
MaterialResource MaterialLoader::CreateDefault() {MaterialResource m;m.name="DefaultMaterial";m.sourcePath="defaults/material";return m;}
