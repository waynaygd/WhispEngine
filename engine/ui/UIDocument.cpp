#include "UIDocument.h"
#include "../resources/PostFxResource.h"
#include "../ecs/ComponentRegistry.h"
#include "../scene/SceneSerializer.h"
#include <fstream>
#include <map>
#include <set>
namespace ui {
using Json=nlohmann::json;
void to_json(Json& j,const Widget& v){j={{"id",v.id},{"parent",v.parent},{"type",v.type},{"text",v.text},{"texture",v.texture},{"anchorMin",v.anchorMin},{"anchorMax",v.anchorMax},{"pivot",v.pivot},{"position",v.position},{"size",v.size},{"color",v.color},{"fillColor",v.fillColor},{"opacity",v.opacity},{"fontSize",v.fontSize},{"value",v.value},{"zOrder",v.zOrder},{"textAlignment",v.textAlignment},{"visible",v.visible},{"clipChildren",v.clipChildren},{"uv",v.uv}};j["layout"]=v.layout;j["spacing"]=v.spacing;j["padding"]=v.padding;j["alignment"]=v.alignment;j["autoSize"]=v.autoSize;j["stretch"]=v.stretch;j["stretchChildren"]=v.stretchChildren;j["ignoreLayout"]=v.ignoreLayout;j["reverse"]=v.reverse;j["styleAsset"]=v.styleAsset;j["styleId"]=v.styleId;j["styleOverrides"]=v.styleOverrides;j["transitionDuration"]=v.transitionDuration;if(v.type=="Button"){j["enabled"]=v.enabled;j["hoverColor"]=v.hoverColor;j["pressedColor"]=v.pressedColor;j["disabledColor"]=v.disabledColor;}}
void from_json(const Json& j,Widget& v){v.id=j.at("id").get<std::string>();v.type=j.at("type").get<std::string>();
#define UI_READ(field) if(j.contains(#field))j.at(#field).get_to(v.field);
 UI_READ(layout) UI_READ(spacing) UI_READ(padding) UI_READ(alignment) UI_READ(autoSize) UI_READ(stretch) UI_READ(stretchChildren) UI_READ(ignoreLayout) UI_READ(reverse) UI_READ(styleAsset) UI_READ(styleId) UI_READ(styleOverrides) UI_READ(transitionDuration) UI_READ(parent) UI_READ(text) UI_READ(texture) UI_READ(anchorMin) UI_READ(anchorMax) UI_READ(pivot) UI_READ(position) UI_READ(size) UI_READ(color) UI_READ(fillColor) UI_READ(opacity) UI_READ(fontSize) UI_READ(value) UI_READ(zOrder) UI_READ(textAlignment) UI_READ(visible) UI_READ(clipChildren) UI_READ(uv) UI_READ(enabled) UI_READ(hoverColor) UI_READ(pressedColor) UI_READ(disabledColor)
#undef UI_READ
}
void to_json(Json& j,const Document& d){j={{"version",1},{"name",d.name},{"enabled",d.enabled},{"referenceResolution",d.referenceResolution},{"scaleMode",d.scaleMode},{"sortOrder",d.sortOrder},{"root",d.root},{"widgets",d.widgets}};if(!d.animations.empty())j["animations"]=d.animations;if(!d.requiredWidgets.empty())j["requiredWidgets"]=d.requiredWidgets;}
void from_json(const Json& j,Document& d){if(j.at("version")!=1)throw std::runtime_error("Unsupported UI document version");d.name=j.value("name",std::string("Canvas"));d.enabled=j.value("enabled",true);d.referenceResolution=j.value("referenceResolution",Pair{1920,1080});d.scaleMode=j.value("scaleMode",0);d.sortOrder=j.value("sortOrder",0);d.root=j.at("root").get<std::string>();if(!j.at("widgets").is_array()||j.at("widgets").size()>2048)throw std::runtime_error("UI document supports at most 2048 widgets");d.widgets=j.at("widgets").get<std::vector<Widget>>();if(j.contains("requiredWidgets"))d.requiredWidgets=j.at("requiredWidgets").get<std::vector<std::string>>();if(j.contains("animations"))d.animations=j.at("animations").get<std::vector<AnimationClip>>();auto error=Validate(d);if(!error.empty())throw std::runtime_error(error);}
bool ValidUTF8(const std::string& text){
 for(size_t i=0;i<text.size();){unsigned c=(unsigned char)text[i++];if(c<128){if(c==0)return false;continue;}unsigned n=0,value=0,minimum=0;
  if(c>=0xc2&&c<=0xdf){n=1;value=c&31;minimum=128;}else if(c>=0xe0&&c<=0xef){n=2;value=c&15;minimum=2048;}else if(c>=0xf0&&c<=0xf4){n=3;value=c&7;minimum=65536;}else return false;
  if(i+n>text.size())return false;while(n--){unsigned b=(unsigned char)text[i++];if((b&192)!=128)return false;value=(value<<6)|(b&63);}if(value<minimum||value>0x10ffff||(value>=0xd800&&value<=0xdfff))return false;
 }return true;
}
std::string Validate(const Document& d){
 auto range=[](float x,float lo,float hi){return std::isfinite(x)&&x>=lo&&x<=hi;};
 if(d.scaleMode!=0||(!ValidUTF8(d.name)||d.name.size()>256)||d.widgets.size()>2048||d.requiredWidgets.size()>2048)return "Unsupported UI scale mode or document size";
 for(float n:d.referenceResolution)if(!range(n,1,16384))return "Invalid Canvas reference resolution";
 if(d.sortOrder<-1000000||d.sortOrder>1000000)return "Invalid Canvas Sort Order";
 std::map<std::string,const Widget*> index;
 for(auto& w:d.widgets){
  if(w.layout<0||w.layout>3||(w.layout&&w.type!="Panel")||!range(w.spacing,0,10000)||!range(w.transitionDuration,0,10))return "Invalid layout/transition: "+w.id;
  for(float v:w.padding)if(!range(v,0,10000))return "Invalid layout padding: "+w.id;
  for(int a=0;a<2;++a){if(!range(w.alignment[a],0,1))return "Invalid layout alignment: "+w.id;if(w.layout&&w.autoSize[a]&&w.anchorMin[a]!=w.anchorMax[a])return "Auto Size requires fixed anchors and a container: "+w.id;}
  if(w.styleOverrides.size()>StyleProperties().size()||w.styleId.size()>128||!ValidUTF8(w.styleId)||w.styleAsset.size()>1024||!ValidUTF8(w.styleAsset))return "Invalid Style reference";
  if(!w.styleAsset.empty()&&(!w.styleAsset.ends_with(".uistyle.json")||(w.styleAsset.find("..")!=std::string::npos||w.styleAsset.find(':')!=std::string::npos||w.styleAsset.front()=='/'||w.styleAsset.find('\\')!=std::string::npos)))return "Invalid Style Asset key: "+w.id;
  for(auto& key:w.styleOverrides)if(std::find(StyleProperties().begin(),StyleProperties().end(),key)==StyleProperties().end())return "Unknown Style override: "+key;
  if(w.id.empty()||!ValidUTF8(w.id)||!ValidUTF8(w.text)||w.id.size()>128||!index.emplace(w.id,&w).second)return "Invalid or duplicate Widget ID";
  if(w.type!="Panel"&&w.type!="Text"&&w.type!="Image"&&w.type!="ProgressBar"&&w.type!="Button")return "Unknown Widget Type: "+w.type;
  if(w.text.size()>16384||!range(w.opacity,0,1)||!range(w.fontSize,1,256)||!range(w.value,0,1)||w.textAlignment<0||w.textAlignment>2||w.zOrder<-1000000||w.zOrder>1000000)return "Invalid widget opacity/font/progress/alignment/order: "+w.id;
  for(int axis=0;axis<2;++axis){if(!range(w.anchorMin[axis],0,1)||!range(w.anchorMax[axis],0,1)||w.anchorMin[axis]>w.anchorMax[axis]||!range(w.pivot[axis],0,1)||!range(w.position[axis],-100000,100000)||!range(w.size[axis],-100000,100000)||(w.anchorMin[axis]==w.anchorMax[axis]&&w.size[axis]<0))return "Invalid widget anchors/position/size: "+w.id;}
  for(auto& color:{w.color,w.fillColor,w.hoverColor,w.pressedColor,w.disabledColor})for(float n:color)if(!range(n,0,1))return "Invalid widget color: "+w.id;
  for(float n:w.uv)if(!range(n,0,1))return "Invalid image UV rectangle: "+w.id;
  if(w.uv[0]>w.uv[2]||w.uv[1]>w.uv[3])return "Reversed image UV rectangle: "+w.id;
  if(!w.texture.empty()&&!ecs::ComponentRegistry::ValidateAssetKey(w.texture,ecs::AssetKind::Texture))return "Invalid Image texture path: "+w.id;
 }
 for(auto& child:d.widgets)if(!child.ignoreLayout&&index.contains(child.parent)){auto& parent=*index.at(child.parent);if(parent.layout)for(int a=0;a<2;++a)if(parent.autoSize[a]&&(child.stretch[a]||(parent.stretchChildren&&(parent.layout==3||a!=(parent.layout==1?1:0)))))return "Cyclic Auto Size / Stretch dependency: "+parent.id;}
 if(d.animations.size()>512)return "Animation clip budget exceeded";
 std::set<std::string> clips;
 for(auto& c:d.animations){
  if(c.id.empty()||!ValidUTF8(c.id)||c.id.size()>128||!clips.insert(c.id).second||!index.contains(c.widget))return "Invalid/duplicate Animation ID or missing Widget: "+c.id;
  if(!range(c.duration,.0001f,3600)||!range(c.delay,0,3600)||!range(c.speed,.001f,100)|| (c.pingPong&&!c.loop))return "Invalid animation timing/loop: "+c.id;
  if(c.easing!="linear"&&c.easing!="easeIn"&&c.easing!="easeOut"&&c.easing!="easeInOut")return "Unsupported animation easing: "+c.id;
  int n=c.property=="color"?4:(c.property=="opacity"||c.property=="progress"||c.property=="offsetX"||c.property=="offsetY"||c.property=="scaleX"||c.property=="scaleY"?1:0);
  if(!n||(c.property=="progress"&&index[c.widget]->type!="ProgressBar"))return "Invalid animation property: "+c.id;
  for(auto v:{c.from,c.to}){if(n==4){if(!v.is_array()||v.size()!=4)return "Invalid animation color";for(auto& x:v)if(!x.is_number()||!range(x.get<float>(),0,1))return "Invalid animation color";}else{if(!v.is_number())return "Invalid animation value";float lo=c.property.starts_with("offset")?-100000:0,hi=c.property=="opacity"||c.property=="progress"?1:c.property.starts_with("scale")?100:100000;if(!range(v.get<float>(),lo,hi))return "Invalid animation value";}}
 }
 for(const auto& id:d.requiredWidgets)if(!index.contains(id))return "Missing required Widget ID: "+id;
 if(d.widgets.empty())return d.root.empty()?std::string():"Empty Canvas must have an empty root";
 if(!index.contains(d.root)||!index[d.root]->parent.empty()||index[d.root]->type!="Panel")return "UI root must be a parentless Panel";
 for(auto& w:d.widgets){const Widget* node=&w;std::set<std::string> seen;while(node->id!=d.root){if(!seen.insert(node->id).second||seen.size()>64)return "Cycle or excessive UI nesting";auto parent=index.find(node->parent);if(parent==index.end()||(parent->second->type!="Panel"&&parent->second->type!="Button"))return "Widget requires a Panel parent: "+node->id;node=parent->second;}}
 return {};
}
std::filesystem::path SafeDocumentPath(const std::string& key){if(!key.ends_with(".ui.json")&&!key.ends_with(".uistyle.json"))throw std::runtime_error("Expected .ui.json document");return PostFxLoader::SafePath(key);}
Document EmptyDocument(){Document d;Widget root;root.id="Root";root.anchorMax={1,1};root.size={0,0};root.color={0,0,0,0};d.widgets.push_back(root);return d;}
Document ExampleHUD(){auto d=EmptyDocument();d.name="PlayerHUD";Widget panel;panel.id="StatusPanel";panel.parent="Root";panel.position={32,32};panel.size={440,200};panel.color={.03f,.04f,.06f,.85f};panel.clipChildren=true;d.widgets.push_back(panel);
 Widget text;text.id="HealthLabel";text.parent=panel.id;text.type="Text";text.position={20,12};text.size={400,40};text.text="Health / Здоровье";text.fontSize=28;d.widgets.push_back(text);
 Widget bar;bar.id="HealthBar";bar.parent=panel.id;bar.type="ProgressBar";bar.position={20,58};bar.size={400,28};bar.color={.08f,.09f,.1f,1};bar.fillColor={.15f,.85f,.28f,1};bar.value=.8f;d.widgets.push_back(bar);
 text.id="SanityLabel";text.position={20,96};text.text="Sanity / Рассудок";d.widgets.push_back(text);bar.id="SanityBar";bar.position={20,142};bar.fillColor={.24f,.5f,1,1};bar.value=.65f;d.widgets.push_back(bar);
 text.id="InteractPrompt";text.parent="Root";text.anchorMin={.5f,1};text.anchorMax=text.anchorMin;text.pivot={.5f,1};text.position={0,-48};text.size={900,48};text.text="[E] Interact / Нажмите E";text.textAlignment=1;text.fontSize=32;d.widgets.push_back(text);return d;}
}
ResourceLoadResult<UIResource> UILoader::Load(const std::string& key,const std::filesystem::path&){ResourceLoadResult<UIResource> result;try{std::ifstream input(ui::SafeDocumentPath(key));if(!input)throw std::runtime_error("Missing UI Document: "+key);nlohmann::json j;input>>j;if(key.ends_with(".uistyle.json"))result.data.styles=std::make_shared<ui::StyleSheet>(j.get<ui::StyleSheet>());else result.data.document=std::make_shared<ui::Document>(j.get<ui::Document>());result.success=true;}catch(const std::exception& e){result.errorMessage=e.what();}return result;}
bool UILoader::Save(const std::string& key,const ui::Document& d,std::string* error){try{if(!key.ends_with(".ui.json"))throw std::runtime_error("Expected .ui.json document");auto invalid=ui::Validate(d);if(!invalid.empty())throw std::runtime_error(invalid);return SceneSerializer::WriteAtomic(ui::SafeDocumentPath(key),nlohmann::json(d),error,true);}catch(const std::exception& e){if(error)*error=e.what();return false;}}
