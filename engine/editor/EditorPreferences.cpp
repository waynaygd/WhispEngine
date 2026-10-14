#include "EditorPreferences.h"
#include "../scene/SceneSerializer.h"
#include <fstream>
#include <cmath>
namespace editor {
nlohmann::json PreferencesToJson(const EditorPreferences& p){
    return {{"version",1},{"panels",{{"hierarchy",p.hierarchy},{"inspector",p.inspector},{"viewport",p.viewport},{"assets",p.assets},{"material",p.material},{"settings",p.settings},{"console",p.console},{"diagnostics",p.diagnostics}}},
            {"grid",p.grid},{"lightGizmos",p.lightGizmos},{"xray",p.xray},{"snapping",p.snapping},{"operation",p.operation},{"space",p.space},
            {"translationSnap",p.translationSnap},{"rotationSnap",p.rotationSnap},{"scaleSnap",p.scaleSnap},{"cameraSpeed",p.cameraSpeed},{"cameraSensitivity",p.cameraSensitivity}};
}
EditorPreferences PreferencesFromJson(const nlohmann::json& j){
    if(!j.is_object()||j.value("version",0)!=1)throw std::runtime_error("Unsupported editor preferences schema");EditorPreferences p;
    if(j.contains("panels")){auto& a=j.at("panels");p.hierarchy=a.value("hierarchy",p.hierarchy);p.inspector=a.value("inspector",p.inspector);p.viewport=a.value("viewport",p.viewport);p.assets=a.value("assets",p.assets);p.material=a.value("material",p.material);p.settings=a.value("settings",p.settings);p.console=a.value("console",p.console);p.diagnostics=a.value("diagnostics",p.diagnostics);}
    p.grid=j.value("grid",p.grid);p.lightGizmos=j.value("lightGizmos",p.lightGizmos);p.xray=j.value("xray",p.xray);p.snapping=j.value("snapping",p.snapping);p.operation=j.value("operation",p.operation);p.space=j.value("space",p.space);
    if(p.operation<0||p.operation>2||p.space<0||p.space>1)throw std::runtime_error("Invalid editor gizmo preference");
    auto number=[&](const char* key,float fallback,float lo,float hi){float v=j.value(key,fallback);if(!std::isfinite(v)||v<lo||v>hi)throw std::runtime_error(std::string("Invalid editor preference: ")+key);return v;};
    p.translationSnap=number("translationSnap",p.translationSnap,.001f,1000);p.rotationSnap=number("rotationSnap",p.rotationSnap,.1f,180);p.scaleSnap=number("scaleSnap",p.scaleSnap,.001f,100);p.cameraSpeed=number("cameraSpeed",p.cameraSpeed,.2f,25);p.cameraSensitivity=number("cameraSensitivity",p.cameraSensitivity,.0001f,.02f);return p;
}
bool LoadPreferences(const std::filesystem::path& path,EditorPreferences& prefs,std::string* error){try{std::ifstream file(path);if(!file)return false;nlohmann::json j;file>>j;auto loaded=PreferencesFromJson(j);prefs=loaded;return true;}catch(const std::exception& e){if(error)*error=e.what();return false;}}
bool SavePreferences(const std::filesystem::path& path,const EditorPreferences& prefs,std::string* error){try{auto j=PreferencesToJson(prefs);(void)PreferencesFromJson(j);return SceneSerializer::WriteAtomic(path,j,error);}catch(const std::exception& e){if(error)*error=e.what();return false;}}
}
