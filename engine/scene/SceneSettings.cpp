#include "SceneSettings.h"
#include "../ecs/ComponentRegistry.h"
using Json=nlohmann::json;
namespace {
float Number(const Json& j,const char* key,float fallback,float low,float high){
    float v=j.value(key,fallback);if(!std::isfinite(v)||v<low||v>high)throw std::runtime_error(std::string("Invalid scene setting: ")+key);return v;
}
std::array<float,3> Vector(const Json& j,const char* key,std::array<float,3> fallback,bool color){
    if(!j.contains(key))return fallback;auto v=j.at(key).get<std::array<float,3>>();
    for(auto x:v)if(!std::isfinite(x)||(color&&(x<0||x>10000)))throw std::runtime_error(std::string("Invalid scene vector: ")+key);return v;
}
}
Json SceneSettingsToJson(const SceneSettings& s){
    const auto& l=s.lighting;const auto& e=s.environment;const auto& h=s.shadows;
    Json j={{"antiAliasing",int(s.antiAliasing)},{"ambient",{{"color",l.ambientColor},{"intensity",l.ambientIntensity}}},
            {"legacyDirectional",{{"direction",l.directional.direction},{"color",l.directional.color},{"intensity",l.directional.intensity},{"enabled",l.directional.enabled}}},
            {"environment",{{"asset",e.assetPath},{"enabled",e.enabled},{"diffuse",e.diffuse},{"specular",e.specular},{"toneMapping",e.toneMapping},{"intensity",e.intensity},{"rotationDegrees",e.rotationDegrees},{"exposure",e.exposure}}},
            {"shadows",{{"enabled",h.enabled},{"pcf",h.pcf},{"filterTaps",h.filterTaps},{"resolution",h.resolution},{"coverage",h.coverage},{"focusDistance",h.focusDistance},{"depthRange",h.depthRange},{"depthBias",h.depthBias},{"slopeBias",h.slopeBias},{"receiverBias",h.receiverBias},{"normalBias",h.normalBias},{"strength",h.strength},{"cameraFitted",h.cameraFitted},{"receiverPlaneBias",h.receiverPlaneBias},{"filterRadius",h.filterRadius},{"edgeFade",h.edgeFade}}},
            {"background",{{"visibleSunDisc",s.background.visibleSunDisc},{"driveLightProperties",s.background.driveLightProperties},{"nightColor",s.background.nightColor},{"horizonGlow",s.background.horizonGlow},{"sunsetWarmth",s.background.sunsetWarmth},{"nightIntensity",s.background.nightIntensity},{"dayLightIntensity",s.background.dayLightIntensity},{"sunSync",int(s.background.sunSync)},{"sunMetadata",s.background.sunMetadata},{"sunDirection",s.background.sunDirection},{"sunGlowRadius",s.background.sunGlowRadius},{"sunGlowIntensity",s.background.sunGlowIntensity},{"sunRadius",s.background.sunRadius},{"sunIntensity",s.background.sunIntensity},{"version",1},{"mode",int(s.background.background)},{"solid",s.background.solid},{"top",s.background.top},{"horizon",s.background.horizon},{"bottom",s.background.bottom},{"intensity",s.background.skyIntensity},{"blend",s.background.gradientBlend},{"rotation",s.background.skyRotation},{"asset",s.background.cubemap},{"useIbl",s.background.useIblEnvironment}}},
            {"primaryDirectional",s.primaryDirectional}};
    const auto& ao=e.ambientOcclusion;
    j["ambientOcclusion"]={{"enabled",ao.enabled},{"quality",ao.quality},{"radius",ao.radius},{"intensity",ao.intensity},{"bias",ao.bias},{"debug",ao.debug}};
    (void)SceneSettingsFromJson(j);return j;
}
SceneSettings SceneSettingsFromJson(const Json& j){
    if(!j.is_object())throw std::runtime_error("Scene settings must be an object");SceneSettings s;
    s.antiAliasing=AntiAliasingMode(j.value("antiAliasing",0));if(int(s.antiAliasing)<0||int(s.antiAliasing)>2)throw std::runtime_error("Invalid anti-aliasing mode");
    if(j.contains("ambient")){auto& a=j.at("ambient");s.lighting.ambientColor=Vector(a,"color",s.lighting.ambientColor,true);s.lighting.ambientIntensity=Number(a,"intensity",s.lighting.ambientIntensity,0,10000);}
    if(j.contains("legacyDirectional")){auto& d=j.at("legacyDirectional");auto& l=s.lighting.directional;l.direction=Vector(d,"direction",l.direction,false);l.color=Vector(d,"color",l.color,true);l.intensity=Number(d,"intensity",l.intensity,0,100000);l.enabled=d.value("enabled",l.enabled);}
    if(j.contains("environment")){auto& a=j.at("environment");auto& e=s.environment;e.assetPath=a.value("asset",e.assetPath);std::string error;if(!ecs::ComponentRegistry::ValidateAssetKey(e.assetPath,ecs::AssetKind::Environment,&error))throw std::runtime_error(error);e.enabled=a.value("enabled",e.enabled);e.diffuse=a.value("diffuse",e.diffuse);e.specular=a.value("specular",e.specular);e.toneMapping=a.value("toneMapping",e.toneMapping);e.intensity=Number(a,"intensity",e.intensity,0,10000);e.rotationDegrees=Number(a,"rotationDegrees",e.rotationDegrees,-100000,100000);e.exposure=Number(a,"exposure",e.exposure,-16,16);}
    if(j.contains("shadows")){auto& a=j.at("shadows");auto& h=s.shadows;h.enabled=a.value("enabled",h.enabled);h.pcf=a.value("pcf",h.pcf);h.filterTaps=a.value("filterTaps",3);if(h.filterTaps!=3&&h.filterTaps!=5)throw std::runtime_error("Invalid shadow filter size");h.resolution=a.value("resolution",h.resolution);if(h.resolution!=512&&h.resolution!=1024&&h.resolution!=2048&&h.resolution!=4096)throw std::runtime_error("Invalid shadow resolution");h.coverage=Number(a,"coverage",h.coverage,2,500);h.focusDistance=Number(a,"focusDistance",h.focusDistance,0,500);h.depthRange=Number(a,"depthRange",h.depthRange,2,1000);h.depthBias=a.value("depthBias",h.depthBias);if(h.depthBias<0||h.depthBias>10000)throw std::runtime_error("Invalid shadow depth bias");h.slopeBias=Number(a,"slopeBias",h.slopeBias,0,8);h.receiverBias=Number(a,"receiverBias",h.receiverBias,0,.01f);h.normalBias=Number(a,"normalBias",h.normalBias,0,4);h.strength=Number(a,"strength",h.strength,0,1);h.cameraFitted=a.value("cameraFitted",h.cameraFitted);h.receiverPlaneBias=a.value("receiverPlaneBias",h.receiverPlaneBias);h.filterRadius=Number(a,"filterRadius",h.filterRadius,0,2);h.edgeFade=Number(a,"edgeFade",h.edgeFade,0,.25f);}
    if(j.contains("background")){const auto& a=j.at("background");if(a.value("version",1)!=1)throw std::runtime_error("Unsupported background version");auto& b=s.background;
        b.visibleSunDisc=a.value("visibleSunDisc",true);b.driveLightProperties=a.value("driveLightProperties",false);b.nightColor=Vector(a,"nightColor",b.nightColor,true);b.horizonGlow=Number(a,"horizonGlow",1,0,8);b.sunsetWarmth=Number(a,"sunsetWarmth",.85f,0,4);b.nightIntensity=Number(a,"nightIntensity",.25f,0,4);b.dayLightIntensity=Number(a,"dayLightIntensity",3,0,10000);
        b.sunSync=SunSyncMode(a.value("sunSync",0));if(int(b.sunSync)<0||int(b.sunSync)>2)throw std::runtime_error("Invalid Sun Sync mode");b.sunMetadata=a.value("sunMetadata",false);b.sunDirection=Vector(a,"sunDirection",b.sunDirection,false);float length=0;for(float x:b.sunDirection)length+=x*x;if(!std::isfinite(length)||length<1e-12f)throw std::runtime_error("Sun direction must be nonzero");b.sunGlowRadius=Number(a,"sunGlowRadius",3,.01f,60);b.sunGlowIntensity=Number(a,"sunGlowIntensity",.15f,0,4);b.sunRadius=Number(a,"sunRadius",.53f,.01f,10);b.sunIntensity=Number(a,"sunIntensity",1,0,32);
        if(b.sunSync==SunSyncMode::SkyDrivesLight&&!b.sunMetadata)throw std::runtime_error("Sky Drives Light requires explicit Sun Direction metadata");
        b.background=EditorBackground(a.value("mode",int(b.background)));b.solid=Vector(a,"solid",b.solid,true);b.top=Vector(a,"top",b.top,true);b.horizon=Vector(a,"horizon",b.horizon,true);b.bottom=Vector(a,"bottom",b.bottom,true);
        b.skyIntensity=Number(a,"intensity",1,0,32);b.gradientBlend=Number(a,"blend",.65f,.1f,8);b.skyRotation=Number(a,"rotation",0,-36000,36000);b.cubemap=a.value("asset",b.cubemap);b.useIblEnvironment=a.value("useIbl",false);
        EditorVisualSettings visual;static_cast<SceneBackground&>(visual)=b;if(!ValidEditorVisualSettings(visual)||!ecs::ComponentRegistry::ValidateAssetKey(b.cubemap,ecs::AssetKind::Environment))throw std::runtime_error("Invalid scene background");
    }
    // Legacy scenes without an authored toneMapping value used SDR unless IBL
    // or a cubemap background selected the native HDR path. Preserve that look
    // on read; explicit true/false always wins and the serialized schema is unchanged.
    const bool authoredToneMapping=j.contains("environment")&&j.at("environment").contains("toneMapping");
    if(!authoredToneMapping&&!s.environment.enabled&&s.background.background!=EditorBackground::Cubemap)
        s.environment.toneMapping=false;
    if(j.contains("ambientOcclusion")){const auto& a=j.at("ambientOcclusion");auto& ao=s.environment.ambientOcclusion;
        ao.enabled=a.value("enabled",false);ao.quality=a.value("quality",1);ao.debug=a.value("debug",0);
        if(ao.quality<0||ao.quality>2||ao.debug<0||ao.debug>2)throw std::runtime_error("Invalid AO quality/debug");
        ao.radius=Number(a,"radius",.75f,.01f,10);ao.intensity=Number(a,"intensity",1,0,4);ao.bias=Number(a,"bias",.025f,0,.5f);
    }
    s.primaryDirectional=j.value("primaryDirectional",std::string());if(s.primaryDirectional.size()>128)throw std::runtime_error("Invalid primary directional reference");return s;
}
