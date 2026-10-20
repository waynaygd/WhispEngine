#pragma once
#include <nlohmann/json.hpp>
#include <array>
#include <string>
#include <vector>
#include <filesystem>
#include <memory>
#include <optional>
#include <map>
#include "../resources/ResourceLoader.h"
namespace ui {
using Pair=std::array<float,2>;using Color=std::array<float,4>;
struct AnimationClip {
 std::string id,widget,property="opacity",easing="linear";
 nlohmann::json from=0,to=1;float duration=.25f,delay=0,speed=1;bool loop=false,pingPong=false;
 bool operator==(const AnimationClip&)const=default;
};
void to_json(nlohmann::json&,const AnimationClip&);void from_json(const nlohmann::json&,AnimationClip&);
struct Widget {
 std::string id,parent,type="Panel",text,texture;
 Pair anchorMin{0,0},anchorMax{0,0},pivot{0,0},position{0,0},size{200,60};
 Color color{1,1,1,1},fillColor{.1f,.8f,.2f,1};
 Color hoverColor{.3f,.5f,.8f,1},pressedColor{.1f,.25f,.5f,1},disabledColor{.3f,.3f,.3f,.5f};
 int layout=0;float spacing=0;Color padding{0,0,0,0};Pair alignment{0,0};
 std::array<bool,2> autoSize{false,false},stretch{false,false};bool stretchChildren=false,ignoreLayout=false,reverse=false;
 std::string styleAsset,styleId;std::vector<std::string> styleOverrides;
 float transitionDuration=0;
 // Visual layer is transient and excluded from serialization.
 Pair visualOffset{0,0},visualScale{1,1};std::optional<float> visualOpacity,visualValue;std::optional<Color> visualColor;
 bool operator==(const Widget&)const=default;
 bool enabled=true;int interaction=0; // interaction is transient, never serialized.
 float opacity=1,fontSize=24,value=1;int zOrder=0,textAlignment=0;
 bool visible=true,clipChildren=false;std::array<float,4> uv{0,0,1,1};
};
struct Document {std::string name="Canvas",root="Root";bool enabled=true;Pair referenceResolution{1920,1080};int scaleMode=0,sortOrder=0;std::vector<Widget> widgets;std::vector<std::string> requiredWidgets;std::vector<AnimationClip> animations;bool operator==(const Document&)const=default;};
void to_json(nlohmann::json&,const Widget&);void from_json(const nlohmann::json&,Widget&);
void to_json(nlohmann::json&,const Document&);void from_json(const nlohmann::json&,Document&);
bool ValidUTF8(const std::string&);
std::string Validate(const Document&);
std::filesystem::path SafeDocumentPath(const std::string&);
Document EmptyDocument();Document ExampleHUD();
}
namespace ui {
struct Style {std::string type; nlohmann::json properties=nlohmann::json::object();};
struct StyleSheet {std::map<std::string,Style> styles;};
std::string ValidateStyle(const StyleSheet&);
void to_json(nlohmann::json&,const StyleSheet&);void from_json(const nlohmann::json&,StyleSheet&);
bool SaveStyles(const std::string&,const StyleSheet&,std::string* error=nullptr);
const std::vector<std::string>& StyleProperties();
void ApplyStyle(Widget&,const Widget& authored,const Style*,unsigned runtimeMask=0);
}
struct UIResource {std::shared_ptr<const ui::Document> document;std::shared_ptr<const ui::StyleSheet> styles;};
class UILoader {
public:
 static ResourceLoadResult<UIResource> Load(const std::string&,const std::filesystem::path&);
 static bool Save(const std::string&,const ui::Document&,std::string* error=nullptr);
};
