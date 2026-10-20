#pragma once
#include <nlohmann/json.hpp>
#include <array>
#include <string>
#include <vector>
#include <filesystem>
#include <memory>
#include "../resources/ResourceLoader.h"
namespace ui {
using Pair=std::array<float,2>;using Color=std::array<float,4>;
struct Widget {
 std::string id,parent,type="Panel",text,texture;
 Pair anchorMin{0,0},anchorMax{0,0},pivot{0,0},position{0,0},size{200,60};
 Color color{1,1,1,1},fillColor{.1f,.8f,.2f,1};
 Color hoverColor{.3f,.5f,.8f,1},pressedColor{.1f,.25f,.5f,1},disabledColor{.3f,.3f,.3f,.5f};
 bool enabled=true;int interaction=0; // interaction is transient, never serialized.
 float opacity=1,fontSize=24,value=1;int zOrder=0,textAlignment=0;
 bool visible=true,clipChildren=false;std::array<float,4> uv{0,0,1,1};
};
struct Document {std::string name="Canvas",root="Root";bool enabled=true;Pair referenceResolution{1920,1080};int scaleMode=0,sortOrder=0;std::vector<Widget> widgets;std::vector<std::string> requiredWidgets;};
void to_json(nlohmann::json&,const Widget&);void from_json(const nlohmann::json&,Widget&);
void to_json(nlohmann::json&,const Document&);void from_json(const nlohmann::json&,Document&);
bool ValidUTF8(const std::string&);
std::string Validate(const Document&);
std::filesystem::path SafeDocumentPath(const std::string&);
Document EmptyDocument();Document ExampleHUD();
}
struct UIResource {std::shared_ptr<const ui::Document> document;};
class UILoader {
public:
 static ResourceLoadResult<UIResource> Load(const std::string&,const std::filesystem::path&);
 static bool Save(const std::string&,const ui::Document&,std::string* error=nullptr);
};
