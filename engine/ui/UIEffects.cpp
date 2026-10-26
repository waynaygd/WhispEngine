#include "RuntimeUI.h"
#include "../resources/ResourceManager.h"
#include "../scene/SceneSerializer.h"
#include <fstream>
#include <set>
namespace ui {
using Json=nlohmann::json;
void to_json(Json& j,const AnimationClip& c){j={{"id",c.id},{"widget",c.widget},{"property",c.property},{"from",c.from},{"to",c.to},{"duration",c.duration},{"delay",c.delay},{"speed",c.speed},{"loop",c.loop},{"pingPong",c.pingPong},{"easing",c.easing}};}
void from_json(const Json& j,AnimationClip& c){c.id=j.at("id");c.widget=j.at("widget");c.property=j.at("property");c.from=j.at("from");c.to=j.at("to");c.duration=j.value("duration",.25f);c.delay=j.value("delay",0.f);c.speed=j.value("speed",1.f);c.loop=j.value("loop",false);c.pingPong=j.value("pingPong",false);c.easing=j.value("easing",std::string("linear"));}
const std::vector<std::string>& StyleProperties(){static const std::vector<std::string> p{"font","color","opacity","padding","spacing","size","fontSize","textAlignment","hoverColor","pressedColor","disabledColor","transitionDuration"};return p;}
void to_json(Json& j,const StyleSheet& sheet){j={{"version",1},{"styles",Json::object()}};for(auto& [id,s]:sheet.styles)j["styles"][id]={{"type",s.type},{"properties",s.properties}};}
void from_json(const Json& j,StyleSheet& sheet){if(j.at("version")!=1||!j.at("styles").is_object()||j.at("styles").size()>256)throw std::runtime_error("Invalid UI style sheet");for(auto& [id,value]:j.at("styles").items())sheet.styles[id]={value.at("type").get<std::string>(),value.at("properties")};auto error=ValidateStyle(sheet);if(!error.empty())throw std::runtime_error(error);}
std::string ValidateStyle(const StyleSheet& sheet){
 if(sheet.styles.size()>256)return "Too many styles";
 for(auto& [id,s]:sheet.styles){if(id.empty()||id.size()>128||!ValidUTF8(id)||!s.properties.is_object())return "Invalid Style ID/properties";
  auto d=EmptyDocument();Widget w;w.id="Styled";w.parent=d.root;w.type=s.type;auto j=Json(w);
  for(auto& [key,value]:s.properties.items()){
   if(std::find(StyleProperties().begin(),StyleProperties().end(),key)==StyleProperties().end())return "Unsupported Style property: "+key;
   if((key=="font"||key=="fontSize"||key=="textAlignment")&&s.type!="Text")return "Text Style property on incompatible type";
   if((key=="hoverColor"||key=="pressedColor"||key=="disabledColor"||key=="transitionDuration")&&s.type!="Button")return "Button Style property on incompatible type";
   if((key=="padding"||key=="spacing")&&s.type!="Panel")return "Layout Style property on incompatible type";
   j[key]=value;
  }
  try{d.widgets.push_back(j.get<Widget>());auto error=Validate(d);if(!error.empty())return error;}catch(const std::exception& e){return e.what();}
 }return {};
}
bool SaveStyles(const std::string& key,const StyleSheet& sheet,std::string* error){try{if(!key.ends_with(".uistyle.json"))throw std::runtime_error("Expected .uistyle.json");auto invalid=ValidateStyle(sheet);if(!invalid.empty())throw std::runtime_error(invalid);return SceneSerializer::WriteAtomic(SafeDocumentPath(key),Json(sheet),error,true);}catch(const std::exception& e){if(error)*error=e.what();return false;}}
void ApplyStyle(Widget& output,const Widget& authored,const Style* style,unsigned mask){
 auto values=Json(authored);if(style&&style->type==authored.type)for(auto& [key,value]:style->properties.items())if(std::find(authored.styleOverrides.begin(),authored.styleOverrides.end(),key)==authored.styleOverrides.end())values[key]=value;
 auto resolved=values.get<Widget>();
 if(!(mask&32))output.color=resolved.color;if(!(mask&4))output.opacity=resolved.opacity;
 output.padding=resolved.padding;output.spacing=resolved.spacing;output.size=resolved.size;output.font=resolved.font;output.fontSize=resolved.fontSize;output.textAlignment=resolved.textAlignment;output.hoverColor=resolved.hoverColor;output.pressedColor=resolved.pressedColor;output.disabledColor=resolved.disabledColor;output.transitionDuration=resolved.transitionDuration;
}
bool StyleCache::Changed(const Document& d,ResourceManager& rm,bool sourceChanged){
 bool changed=sourceChanged;if(epoch!=rm.GetCacheEpoch()){Clear();epoch=rm.GetCacheEpoch();changed=true;}
 std::set<std::string> used;for(auto& w:d.widgets)if(!w.styleAsset.empty())used.insert(w.styleAsset);
 std::erase_if(assets,[&](auto& v){if(used.contains(v.first))return false;changed=true;return true;});
 for(auto& key:used){auto [it,added]=assets.try_emplace(key);auto& entry=it->second;if(added){entry.resource=rm.Get<UIResource>(key);if(!entry.resource)entry.resource=rm.Load<UIResource>(key);rm.WatchForHotReload<UIResource>(key);changed=true;}
  if(entry.resource&&entry.revision!=entry.resource->GetVersion()){entry.revision=entry.resource->GetVersion();changed=true;}

 }
 if(!changed)return false;errors.clear();for(auto& [key,entry]:assets)if(!entry.resource||entry.resource->HasError()||!entry.resource->GetData().styles)errors.push_back("Missing/invalid UI Style: "+key);
 for(auto& w:d.widgets)if(!w.styleAsset.empty()){auto at=assets.find(w.styleAsset);if(at!=assets.end()&&at->second.resource&&!at->second.resource->HasError())if(auto sheet=at->second.resource->GetData().styles){auto style=sheet->styles.find(w.styleId);if(style==sheet->styles.end())errors.push_back("Missing Style ID: "+w.styleId);else if(style->second.type!=w.type)errors.push_back("Incompatible Style type: "+w.id);}}
 return changed;
}
void StyleCache::Resolve(Widget& output,const Widget& authored,unsigned mask)const{
 const Style* style=nullptr;auto asset=assets.find(authored.styleAsset);if(asset!=assets.end()&&asset->second.resource&&!asset->second.resource->HasError())if(auto sheet=asset->second.resource->GetData().styles){auto found=sheet->styles.find(authored.styleId);if(found!=sheet->styles.end())style=&found->second;}
 ApplyStyle(output,authored,style,mask);
}
namespace {
Widget& FindWidget(Document& d,const std::string& id){for(auto& w:d.widgets)if(w.id==id)return w;throw std::runtime_error("Animation Widget missing");}
void SetVisual(Widget& w,const std::string& p,const Json& value){if(p=="opacity")w.visualOpacity=value.get<float>();else if(p=="progress")w.visualValue=value.get<float>();else if(p=="color")w.visualColor=value.get<Color>();else if(p=="offsetX")w.visualOffset[0]=value.get<float>();else if(p=="offsetY")w.visualOffset[1]=value.get<float>();else if(p=="scaleX")w.visualScale[0]=value.get<float>();else if(p=="scaleY")w.visualScale[1]=value.get<float>();}
void ResetVisual(Widget& w,const std::string& p){if(p=="opacity")w.visualOpacity.reset();else if(p=="progress")w.visualValue.reset();else if(p=="color")w.visualColor.reset();else if(p=="offsetX")w.visualOffset[0]=0;else if(p=="offsetY")w.visualOffset[1]=0;else if(p=="scaleX")w.visualScale[0]=1;else if(p=="scaleY")w.visualScale[1]=1;}
}
void PlayAnimation(Document& d,AnimationState& state,const std::string& id,bool restart,float speed){
 if(!std::isfinite(speed)||speed<.001f||speed>100)throw std::runtime_error("Invalid animation playback speed");
 auto found=std::find_if(d.animations.begin(),d.animations.end(),[&](auto& c){return c.id==id;});if(found==d.animations.end())throw std::runtime_error("Missing Animation ID: "+id);
 auto& w=FindWidget(d,found->widget);auto key=found->widget+"/"+found->property;auto old=state.tracks.find(key);if(!restart&&old!=state.tracks.end()&&old->second.clip.id==id&&old->second.playing)return;
 auto clip=*found;clip.speed*=speed;if(clip.speed<.001f||clip.speed>100)throw std::runtime_error("Animation speed exceeds budget");state.tracks[key]={clip,0,true};SetVisual(w,clip.property,clip.from);
}
void StopAnimation(Document& d,AnimationState& state,const std::string& id){bool exists=false;for(auto& c:d.animations)exists|=c.id==id;if(!exists)throw std::runtime_error("Missing Animation ID: "+id);for(auto it=state.tracks.begin();it!=state.tracks.end();)if(it->second.clip.id==id){ResetVisual(FindWidget(d,it->second.clip.widget),it->second.clip.property);it=state.tracks.erase(it);}else ++it;}
std::vector<std::pair<std::string,std::string>> AdvanceAnimations(Document& d,AnimationState& state,float dt){
 std::vector<std::pair<std::string,std::string>> done;if(!std::isfinite(dt)||dt<0)return done;dt=std::min(dt,.1f);
 for(auto& [key,track]:state.tracks){auto& c=track.clip;if(!track.playing)continue;track.time+=dt*c.speed;float elapsed=std::max(0.f,track.time-c.delay);float t=elapsed/c.duration;
  if(c.loop){if(c.pingPong){t=std::fmod(t,2.f);if(t>1)t=2-t;}else t=std::fmod(t,1.f);}else if(t>=1){t=1;track.playing=false;done.emplace_back(c.widget,c.id);}
  if(c.easing=="easeIn")t*=t;else if(c.easing=="easeOut")t=1-(1-t)*(1-t);else if(c.easing=="easeInOut")t=t*t*(3-2*t);
  Json value;if(c.property=="color"){Color color;for(int a=0;a<4;++a)color[a]=c.from[a].get<float>()+(c.to[a].get<float>()-c.from[a].get<float>())*t;value=color;}else value=c.from.get<float>()+(c.to.get<float>()-c.from.get<float>())*t;
  SetVisual(FindWidget(d,c.widget),c.property,value);
 }
 for(auto& w:d.widgets)if(w.type=="Button"){
  auto target=!w.enabled?w.disabledColor:w.interaction==2?w.pressedColor:w.interaction==1?w.hoverColor:w.color;
  const bool animated=state.tracks.contains(w.id+"/color");
  if(w.transitionDuration<=0){if(!animated)w.visualColor.reset();state.buttonColors.erase(w.id);continue;}
  auto [it,added]=state.buttonColors.try_emplace(w.id);auto& transition=it->second;
  if(added)transition.from=transition.to=transition.current=target;
  if(transition.to!=target){transition.from=transition.current;transition.to=target;transition.elapsed=0;}
  transition.elapsed+=dt;float t=std::min(1.f,transition.elapsed/w.transitionDuration);for(int a=0;a<4;++a)transition.current[a]=transition.from[a]+(transition.to[a]-transition.from[a])*t;
  if(!animated)w.visualColor=transition.current;
 }return done;
}
void RuntimeUI::Advance(float dt){for(auto& [e,i]:instances)for(auto& [widget,clip]:AdvanceAnimations(i.state,i.animation,dt))if(events.size()<1024)events.push_back({Get(e),widget,"animation_complete",clip});}
void RuntimeUI::Play(const CanvasHandle& h,const std::string& id,bool restart,float speed){auto& i=Resolve(h);PlayAnimation(i.state,i.animation,id,restart,speed);}
void RuntimeUI::Stop(const CanvasHandle& h,const std::string& id){auto& i=Resolve(h);StopAnimation(i.state,i.animation,id);}
bool RuntimeUI::Playing(const CanvasHandle& h,const std::string& id)const{auto& i=Resolve(h);if(std::none_of(i.state.animations.begin(),i.state.animations.end(),[&](auto& c){return c.id==id;}))throw std::runtime_error("Missing Animation ID: "+id);for(auto& [k,v]:i.animation.tracks)if(v.clip.id==id&&v.playing)return true;return false;}
const Document& RuntimeUI::Preview(const Document& source,ResourceManager& rm){bool dirty=!previewInitialized||!(previewSource==source);if(dirty){previewSource=source;preview=source;previewAnimation={};previewInitialized=true;}if(previewStyles.Changed(source,rm,dirty)||dirty)for(size_t n=0;n<preview.widgets.size();++n)previewStyles.Resolve(preview.widgets[n],source.widgets[n]);return preview;}
void RuntimeUI::PreviewPlay(const std::string& id){PlayAnimation(preview,previewAnimation,id);}
void RuntimeUI::PreviewStop(){previewAnimation={};previewInitialized=false;}
void RuntimeUI::PreviewAdvance(float dt){AdvanceAnimations(preview,previewAnimation,dt);}
}
