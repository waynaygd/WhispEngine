#include "EditorPreferences.h"
#include "../core/AssetPaths.h"
#include "../ecs/ComponentRegistry.h"
#include "../scene/SceneSerializer.h"
#include <fstream>
#include <cmath>
namespace editor {
std::filesystem::path PreferencesPath(){return AssetPaths::GetExecutableDirectory()/"editor-preferences.json";}
nlohmann::json VisualsToJson(const EditorVisualSettings& s){return {{"enabled",s.enabled},{"minorSpacing",s.minorSpacing},{"majorSpacing",s.majorSpacing},{"gridOpacity",s.gridOpacity},{"fadeDistance",s.fadeDistance},{"background",int(s.background)},{"solid",s.solid},{"top",s.top},{"horizon",s.horizon},{"bottom",s.bottom},{"skyIntensity",s.skyIntensity},{"gradientBlend",s.gradientBlend},{"skyRotation",s.skyRotation},{"cubemap",s.cubemap}};}
nlohmann::json PreferencesToJson(const EditorPreferences& p){
    nlohmann::json projects=nlohmann::json::array();for(const auto& project:p.recentProjects)projects.push_back({{"name",project.name},{"descriptor",project.descriptor}});
    return {{"recentProjects",projects},{"recentScenes",p.recentScenes},{"assetGrid",p.assetGrid},{"projectAssets",p.projectAssets},{"assetCardSize",p.assetCardSize},{"backgroundOverride",p.backgroundOverride},{"projectileTestInput",p.projectileTestInput},{"visuals",VisualsToJson(p.visuals)},{"version",1},{"panels",{{"hierarchy",p.hierarchy},{"inspector",p.inspector},{"viewport",p.viewport},{"assets",p.assets},{"material",p.material},{"settings",p.settings},{"console",p.console},{"diagnostics",p.diagnostics},{"visuals",p.visualPanel}}},
            {"physicsStress",p.physicsStress},{"rotationSnapping",p.rotationSnapping},{"transformPrecision",p.transformPrecision},{"lightXrayMode",p.lightXrayMode},{"grid",p.grid},{"lightGizmos",p.lightGizmos},{"xray",p.xray},{"snapping",p.snapping},{"operation",p.operation},{"space",p.space},
            {"translationSnap",p.translationSnap},{"rotationSnap",p.rotationSnap},{"scaleSnap",p.scaleSnap},{"cameraSpeed",p.cameraSpeed},{"cameraSensitivity",p.cameraSensitivity}};
}
EditorPreferences PreferencesFromJson(const nlohmann::json& j){
    // Unavailable recent descriptors remain visible; they never prevent startup.
    if(!j.is_object()||j.value("version",0)!=1)throw std::runtime_error("Unsupported editor preferences schema");EditorPreferences p;
    if(j.contains("panels")){auto& a=j.at("panels");p.hierarchy=a.value("hierarchy",p.hierarchy);p.inspector=a.value("inspector",p.inspector);p.viewport=a.value("viewport",p.viewport);p.assets=a.value("assets",p.assets);p.material=a.value("material",p.material);p.settings=a.value("settings",p.settings);p.console=a.value("console",p.console);p.diagnostics=a.value("diagnostics",p.diagnostics);p.visualPanel=a.value("visuals",p.visualPanel);}
    if(j.contains("recentProjects")&&j.at("recentProjects").is_array()){for(const auto& item:j.at("recentProjects")){try{auto name=item.at("name").get<std::string>(),path=item.at("descriptor").get<std::string>();if(!name.empty()&&name.size()<241&&path.size()<32768&&std::filesystem::u8path(path).is_absolute()&&p.recentProjects.size()<12)p.recentProjects.push_back({name,path});}catch(...){}}}
    if(j.contains("recentScenes")){for(const auto& item:j.at("recentScenes")){auto key=item.get<std::string>();if(!ecs::ComponentRegistry::ValidateAssetKey(key,ecs::AssetKind::None)||std::filesystem::u8path(key).extension()!=".json")throw std::runtime_error("Invalid recent scene key");if(std::find(p.recentScenes.begin(),p.recentScenes.end(),key)==p.recentScenes.end()&&p.recentScenes.size()<12)p.recentScenes.push_back(key);}}
    p.assetGrid=j.value("assetGrid",true);p.projectAssets=j.value("projectAssets",true);p.assetCardSize=j.value("assetCardSize",180.f);if(!std::isfinite(p.assetCardSize)||p.assetCardSize<120||p.assetCardSize>320)throw std::runtime_error("Invalid card size");
    p.backgroundOverride=j.value("backgroundOverride",false);p.projectileTestInput=j.value("projectileTestInput",false);
    p.physicsStress=j.value("physicsStress",false);p.rotationSnapping=j.value("rotationSnapping",j.value("snapping",false));p.transformPrecision=j.value("transformPrecision",1);p.lightXrayMode=j.value("lightXrayMode",j.value("xray",false)?2:1);
    if(p.transformPrecision<0||p.transformPrecision>2||p.lightXrayMode<0||p.lightXrayMode>2)throw std::runtime_error("Invalid precision/xray preference");
    p.grid=j.value("grid",p.grid);p.lightGizmos=j.value("lightGizmos",p.lightGizmos);p.xray=j.value("xray",p.xray);p.snapping=j.value("snapping",p.snapping);p.operation=j.value("operation",p.operation);p.space=j.value("space",p.space);
    if(p.operation<0||p.operation>2||p.space<0||p.space>1)throw std::runtime_error("Invalid editor gizmo preference");
    auto number=[&](const char* key,float fallback,float lo,float hi){float v=j.value(key,fallback);if(!std::isfinite(v)||v<lo||v>hi)throw std::runtime_error(std::string("Invalid editor preference: ")+key);return v;};
    p.translationSnap=number("translationSnap",p.translationSnap,.001f,1000);p.rotationSnap=number("rotationSnap",p.rotationSnap,.1f,180);p.scaleSnap=number("scaleSnap",p.scaleSnap,.001f,100);p.cameraSpeed=number("cameraSpeed",p.cameraSpeed,.2f,25);p.cameraSensitivity=number("cameraSensitivity",p.cameraSensitivity,.0001f,.02f);
    if(j.contains("visuals")){const auto& a=j.at("visuals");auto& s=p.visuals;s.enabled=a.value("enabled",s.enabled);s.background=EditorBackground(a.value("background",int(s.background)));s.minorSpacing=a.value("minorSpacing",s.minorSpacing);s.majorSpacing=a.value("majorSpacing",s.majorSpacing);s.gridOpacity=a.value("gridOpacity",s.gridOpacity);s.fadeDistance=a.value("fadeDistance",s.fadeDistance);s.solid=a.value("solid",s.solid);s.top=a.value("top",s.top);s.horizon=a.value("horizon",s.horizon);s.bottom=a.value("bottom",s.bottom);s.skyIntensity=a.value("skyIntensity",s.skyIntensity);s.gradientBlend=a.value("gradientBlend",s.gradientBlend);s.skyRotation=a.value("skyRotation",s.skyRotation);s.cubemap=a.value("cubemap",s.cubemap);}
    if(!ValidEditorVisualSettings(p.visuals)||!ecs::ComponentRegistry::ValidateAssetKey(p.visuals.cubemap,ecs::AssetKind::Environment))throw std::runtime_error("Invalid editor visual settings");return p;
}
bool LoadPreferences(const std::filesystem::path& path,EditorPreferences& prefs,std::string* error){try{std::ifstream file(path);if(!file)return false;nlohmann::json j;file>>j;auto loaded=PreferencesFromJson(j);prefs=loaded;return true;}catch(const std::exception& e){if(error)*error=e.what();return false;}}
bool SavePreferences(const std::filesystem::path& path,const EditorPreferences& prefs,std::string* error){try{auto j=PreferencesToJson(prefs);(void)PreferencesFromJson(j);return SceneSerializer::WriteAtomic(path,j,error);}catch(const std::exception& e){if(error)*error=e.what();return false;}}
}
