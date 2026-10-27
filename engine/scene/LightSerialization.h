#pragma once
#include "../render/SceneLights.h"
#include <nlohmann/json.hpp>
#include <stdexcept>
inline nlohmann::json LightToJson(const SceneLightDescription& d)
{
    const auto p=SanitizeLight(d.parameters);
    nlohmann::json result={{"type",d.type==LightType::Directional?"directional":d.type==LightType::Point?"point":"spot"},
        {"enabled",p.enabled},{"color",p.color},{"intensity",p.intensity},{"range",p.range},
        {"attenuation",p.attenuation},{"innerAngle",p.innerAngle},{"outerAngle",p.outerAngle},
        {"castShadows",p.castShadows},{"primary",d.primary}};
    if(d.type==LightType::Spot){result["shadowResolution"]=p.shadowResolution;result["shadowBias"]=p.shadowBias;result["shadowStrength"]=p.shadowStrength;result["shadowPcf"]=p.shadowPcf;}
    return result;
}
inline SceneLightDescription LightFromJson(const nlohmann::json& j)
{
    if(!j.is_object())throw std::runtime_error("Scene light must be an object");
    SceneLightDescription d;auto type=j.value("type",std::string("point"));
    if(type=="point")d.type=LightType::Point;else if(type=="spot")d.type=LightType::Spot;
    else if(type=="directional")d.type=LightType::Directional;else throw std::runtime_error("Unknown scene light type: "+type);
    auto& p=d.parameters;
    p.enabled=j.value("enabled",p.enabled);p.castShadows=j.value("castShadows",p.castShadows);
    p.intensity=j.value("intensity",p.intensity);p.range=j.value("range",p.range);p.attenuation=j.value("attenuation",p.attenuation);
    p.innerAngle=j.value("innerAngle",p.innerAngle);p.outerAngle=j.value("outerAngle",p.outerAngle);
    p.shadowResolution=j.value("shadowResolution",p.shadowResolution);p.shadowBias=j.value("shadowBias",p.shadowBias);p.shadowStrength=j.value("shadowStrength",p.shadowStrength);p.shadowPcf=j.value("shadowPcf",p.shadowPcf);
    if(j.contains("color")){if(!j["color"].is_array()||j["color"].size()!=3)throw std::runtime_error("Scene light color must have three numbers");p.color=j["color"].get<std::array<float,3>>();}
    d.primary=j.value("primary",false);p=SanitizeLight(p);return d;
}
