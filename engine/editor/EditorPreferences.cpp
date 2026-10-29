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
    nlohmann::json result={{"recentProjects",projects},{"recentScenes",p.recentScenes},{"assetGrid",p.assetGrid},{"projectAssets",p.projectAssets},{"assetCardSize",p.assetCardSize},{"backgroundOverride",p.backgroundOverride},{"projectileTestInput",p.projectileTestInput},{"visuals",VisualsToJson(p.visuals)},{"version",1},{"panels",{{"hierarchy",p.hierarchy},{"inspector",p.inspector},{"viewport",p.viewport},{"assets",p.assets},{"material",p.material},{"settings",p.settings},{"console",p.console},{"diagnostics",p.diagnostics},{"visuals",p.visualPanel}}},
            {"physicsStress",p.physicsStress},{"rotationSnapping",p.rotationSnapping},{"transformPrecision",p.transformPrecision},{"lightXrayMode",p.lightXrayMode},{"grid",p.grid},{"lightGizmos",p.lightGizmos},{"cameraGizmos",p.cameraGizmos},{"cameraFrustum",p.cameraFrustum},{"cameraPreview",p.cameraPreview},{"uiPreview",p.uiPreview},{"xray",p.xray},{"snapping",p.snapping},{"operation",p.operation},{"space",p.space},
            {"surfaceOffset",p.surfaceOffset},{"surfaceRotationOffset",p.surfaceRotationOffset},{"surfaceSnap",p.surfaceSnap},{"alignToNormal",p.alignToNormal},{"translationSnap",p.translationSnap},{"rotationSnap",p.rotationSnap},{"scaleSnap",p.scaleSnap},{"cameraSpeed",p.cameraSpeed},{"cameraSensitivity",p.cameraSensitivity}};
    auto bookmarks=nlohmann::json::object();
    for(const auto& [scene,slots]:p.bookmarks){auto values=nlohmann::json::array();for(const auto& slot:slots){if(slot)values.push_back({{"name",slot->name},{"position",slot->position},{"yaw",slot->yaw},{"pitch",slot->pitch}});else values.push_back(nullptr);}bookmarks[scene]=std::move(values);}
    result["bookmarks"]=std::move(bookmarks);
    result["documentSessions"]=p.documentSessions;
    result["activeWorkspace"]=WorkspaceKeys[std::clamp(int(p.activeWorkspace),0,int(WorkspaceKeys.size())-1)];
    result["workspacesInitialized"]=p.workspacesInitialized;
    auto layouts=nlohmann::json::object();for(const auto& [key,layout]:p.workspaces)layouts[key]={{"ini",layout.ini},{"visible",layout.visible}};
    result["workspaces"]=std::move(layouts);
    for(const auto& [key,layout]:p.workspaces){const auto& v=layout.viewport;result["workspaces"][key]["viewport"]={{"detached",v.detached},{"x",v.x},{"y",v.y},{"width",v.width},{"height",v.height}};}
    result["gameWindow"]={{"width",p.gameWindowWidth},{"height",p.gameWindowHeight},{"playInNewWindow",p.playInNewWindow}};
    const auto& f=p.selectionFilters;result["selectionFilters"]={{"meshes",f.meshes},{"lights",f.lights},{"playerStarts",f.playerStarts},{"fog",f.fog},{"physics",f.physics},{"other",f.other}};
    result["assetBrowser"]={{"view",p.assetView},{"tree",p.assetTree},{"treeWidth",p.assetTreeWidth},{"allAssets",p.assetSearchAll},{"details",p.assetDetails},{"sessions",p.browserSessions}};
    return result;
}
EditorPreferences PreferencesFromJson(const nlohmann::json& j){
    // Unavailable recent descriptors remain visible; they never prevent startup.
    if(!j.is_object()||j.value("version",0)!=1)throw std::runtime_error("Unsupported editor preferences schema");EditorPreferences p;
    if(j.contains("gameWindow")){const auto& w=j.at("gameWindow");p.gameWindowWidth=std::clamp(w.value("width",1280),320,7680);p.gameWindowHeight=std::clamp(w.value("height",720),200,4320);p.playInNewWindow=w.value("playInNewWindow",false);}
    // A damaged workspace never discards other editor preferences.
    if(j.contains("documentSessions")&&j["documentSessions"].is_object()&&j["documentSessions"].dump().size()<1024*1024)p.documentSessions=j["documentSessions"];
    p.workspacesInitialized=j.contains("workspacesInitialized")&&j["workspacesInitialized"].is_boolean()&&j["workspacesInitialized"].get<bool>();
    if(j.contains("activeWorkspace")&&j["activeWorkspace"].is_string())for(int i=0;i<int(WorkspaceKeys.size());++i)if(j["activeWorkspace"]==WorkspaceKeys[i])p.activeWorkspace=EditorWorkspace(i);
    if(j.contains("workspaces")&&j["workspaces"].is_object())for(const auto* key:WorkspaceKeys){
        if(!j["workspaces"].contains(key))continue;
        try{const auto& source=j["workspaces"][key];WorkspaceLayout layout;layout.ini=source.at("ini").get<std::string>();if(!ValidWorkspaceIni(layout.ini))continue;
            if(source.contains("visible")){if(!source["visible"].is_object())continue;for(const auto* panel:WorkspacePanels)if(source["visible"].contains(panel)&&source["visible"][panel].is_boolean())layout.visible[panel]=source["visible"][panel].get<bool>();}
            if(source.contains("viewport")){try{const auto& v=source.at("viewport");WorkspaceLayout::ViewportPlacement a;a.detached=v.value("detached",false);a.x=v.value("x",100.f);a.y=v.value("y",100.f);a.width=v.value("width",760.f);a.height=v.value("height",520.f);if(std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.width)&&std::isfinite(a.height)&&std::abs(a.x)<=30000&&std::abs(a.y)<=30000&&a.width>=420&&a.width<=8192&&a.height>=280&&a.height<=8192)layout.viewport=a;}catch(...){}}
            p.workspaces.emplace(key,std::move(layout));
        }catch(...){}}
    if(j.contains("selectionFilters")){const auto& f=j.at("selectionFilters");auto& v=p.selectionFilters;v.meshes=f.value("meshes",true);v.lights=f.value("lights",true);v.playerStarts=f.value("playerStarts",true);v.fog=f.value("fog",true);v.physics=f.value("physics",true);v.other=f.value("other",true);}
    // One damaged bookmark must not discard other views or preferences.
    if(j.contains("bookmarks")&&j["bookmarks"].is_object())for(const auto& [scene,values]:j["bookmarks"].items()){
        if(scene.size()>32768||!std::filesystem::u8path(scene).is_absolute()||!values.is_array())continue;
        SceneBookmarks slots{};bool any=false;
        for(std::size_t i=0;i<std::min(values.size(),slots.size());++i){if(values[i].is_null())continue;try{
            const auto& v=values[i];CameraBookmark b{v.at("name").get<std::string>(),v.at("position").get<std::array<float,3>>(),v.at("yaw").get<float>(),v.at("pitch").get<float>()};
            if(b.name.empty()||b.name.size()>128||!std::isfinite(b.yaw)||!std::isfinite(b.pitch)||std::abs(b.pitch)>1.553344f||std::any_of(b.position.begin(),b.position.end(),[](float x){return !std::isfinite(x);}))continue;
            slots[i]=std::move(b);any=true;
        }catch(...){}}
        if(any)p.bookmarks.emplace(scene,std::move(slots));
    }
    if(j.contains("panels")){auto& a=j.at("panels");p.hierarchy=a.value("hierarchy",p.hierarchy);p.inspector=a.value("inspector",p.inspector);p.viewport=a.value("viewport",p.viewport);p.assets=a.value("assets",p.assets);p.material=a.value("material",p.material);p.settings=a.value("settings",p.settings);p.console=a.value("console",p.console);p.diagnostics=a.value("diagnostics",p.diagnostics);p.visualPanel=a.value("visuals",p.visualPanel);}
    if(j.contains("recentProjects")&&j.at("recentProjects").is_array()){for(const auto& item:j.at("recentProjects")){try{auto name=item.at("name").get<std::string>(),path=item.at("descriptor").get<std::string>();if(!name.empty()&&name.size()<241&&path.size()<32768&&std::filesystem::u8path(path).is_absolute()&&p.recentProjects.size()<12)p.recentProjects.push_back({name,path});}catch(...){}}}
    if(j.contains("recentScenes")){for(const auto& item:j.at("recentScenes")){auto key=item.get<std::string>();if(!ecs::ComponentRegistry::ValidateAssetKey(key,ecs::AssetKind::None)||std::filesystem::u8path(key).extension()!=".json")throw std::runtime_error("Invalid recent scene key");if(std::find(p.recentScenes.begin(),p.recentScenes.end(),key)==p.recentScenes.end()&&p.recentScenes.size()<12)p.recentScenes.push_back(key);}}
    p.assetGrid=j.value("assetGrid",true);p.projectAssets=j.value("projectAssets",true);p.assetCardSize=j.value("assetCardSize",180.f);if(!std::isfinite(p.assetCardSize)||p.assetCardSize<120||p.assetCardSize>320)throw std::runtime_error("Invalid card size");
    p.assetView=p.assetGrid?0:1;
    if(j.contains("assetBrowser")){
        const auto& b=j.at("assetBrowser");p.assetView=std::clamp(b.value("view",p.assetView),0,2);
        p.assetTree=b.value("tree",true);p.assetSearchAll=b.value("allAssets",false);p.assetDetails=b.value("details",false);
        const float width=b.value("treeWidth",180.f);p.assetTreeWidth=std::isfinite(width)?std::clamp(width,90.f,480.f):180.f;
        if(b.contains("sessions")&&b["sessions"].is_object())p.browserSessions=b["sessions"];
    }
    p.backgroundOverride=j.value("backgroundOverride",false);p.projectileTestInput=j.value("projectileTestInput",false);
    p.surfaceSnap=j.value("surfaceSnap",true);p.alignToNormal=j.value("alignToNormal",false);
    p.physicsStress=j.value("physicsStress",false);p.rotationSnapping=j.value("rotationSnapping",j.value("snapping",false));p.transformPrecision=j.value("transformPrecision",1);p.lightXrayMode=j.value("lightXrayMode",j.value("xray",false)?2:1);
    if(p.transformPrecision<0||p.transformPrecision>2||p.lightXrayMode<0||p.lightXrayMode>2)throw std::runtime_error("Invalid precision/xray preference");
    // Legacy files never stored this switch and always started with Preview on.
    // Missing files keep EditorPreferences::uiPreview=false; explicit choices win.
    p.uiPreview=j.value("uiPreview",true);
    p.cameraGizmos=j.value("cameraGizmos",p.cameraGizmos);p.cameraFrustum=j.value("cameraFrustum",p.cameraFrustum);p.cameraPreview=j.value("cameraPreview",p.cameraPreview);
    p.grid=j.value("grid",p.grid);p.lightGizmos=j.value("lightGizmos",p.lightGizmos);p.xray=j.value("xray",p.xray);p.snapping=j.value("snapping",p.snapping);p.operation=j.value("operation",p.operation);p.space=j.value("space",p.space);
    if(p.operation<0||p.operation>2||p.space<0||p.space>1)throw std::runtime_error("Invalid editor gizmo preference");
    auto number=[&](const char* key,float fallback,float lo,float hi){float v=j.value(key,fallback);if(!std::isfinite(v)||v<lo||v>hi)throw std::runtime_error(std::string("Invalid editor preference: ")+key);return v;};
    p.surfaceOffset=number("surfaceOffset",0,-1000,1000);p.surfaceRotationOffset=number("surfaceRotationOffset",0,-180,180);
    p.translationSnap=number("translationSnap",p.translationSnap,.001f,1000);p.rotationSnap=number("rotationSnap",p.rotationSnap,.1f,180);p.scaleSnap=number("scaleSnap",p.scaleSnap,.001f,100);p.cameraSpeed=number("cameraSpeed",p.cameraSpeed,.2f,25);p.cameraSensitivity=number("cameraSensitivity",p.cameraSensitivity,.0001f,.02f);
    if(j.contains("visuals")){const auto& a=j.at("visuals");auto& s=p.visuals;s.enabled=a.value("enabled",s.enabled);s.background=EditorBackground(a.value("background",int(s.background)));s.minorSpacing=a.value("minorSpacing",s.minorSpacing);s.majorSpacing=a.value("majorSpacing",s.majorSpacing);s.gridOpacity=a.value("gridOpacity",s.gridOpacity);s.fadeDistance=a.value("fadeDistance",s.fadeDistance);s.solid=a.value("solid",s.solid);s.top=a.value("top",s.top);s.horizon=a.value("horizon",s.horizon);s.bottom=a.value("bottom",s.bottom);s.skyIntensity=a.value("skyIntensity",s.skyIntensity);s.gradientBlend=a.value("gradientBlend",s.gradientBlend);s.skyRotation=a.value("skyRotation",s.skyRotation);s.cubemap=a.value("cubemap",s.cubemap);}
    if(!ValidEditorVisualSettings(p.visuals)||!ecs::ComponentRegistry::ValidateAssetKey(p.visuals.cubemap,ecs::AssetKind::Environment))throw std::runtime_error("Invalid editor visual settings");return p;
}
bool LoadPreferences(const std::filesystem::path& path,EditorPreferences& prefs,std::string* error){try{std::ifstream file(path);if(!file)return false;nlohmann::json j;file>>j;auto loaded=PreferencesFromJson(j);prefs=loaded;return true;}catch(const std::exception& e){if(error)*error=e.what();return false;}}
bool SavePreferences(const std::filesystem::path& path,const EditorPreferences& prefs,std::string* error){try{auto j=PreferencesToJson(prefs);(void)PreferencesFromJson(j);return SceneSerializer::WriteAtomic(path,j,error);}catch(const std::exception& e){if(error)*error=e.what();return false;}}
}
