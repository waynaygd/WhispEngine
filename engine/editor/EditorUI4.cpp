#include "EditorLayer.h"
#include "../core/Application.h"
#include "../core/AssetPaths.h"
#include "../resources/ResourceManager.h"
#include "../ecs/components/SceneIdentityComponent.h"
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
namespace editor {
EditorLayer::UIStyleEdit& EditorLayer::UIStyleSession(Application& app,const std::string& key){
 auto it=m_UIStyles.find(key);if(it!=m_UIStyles.end())return it->second;
 auto* rm=app.GetResourceManager();auto resource=rm->Get<UIResource>(key);if(!resource)resource=rm->Load<UIResource>(key);if(!resource||!resource->GetData().styles)throw std::runtime_error("Missing or invalid UI Style Asset");
 auto& session=m_UIStyles[key];session.draft=*resource->GetData().styles;session.disk=nlohmann::json(session.draft);return session;
}
bool EditorLayer::EditUIStyle(Application& app,const std::string& key,const ui::StyleSheet& sheet,bool finish){try{
 if(app.IsEditorPlayMode())return false;auto error=ui::ValidateStyle(sheet);if(!error.empty())throw std::runtime_error(error);auto& s=UIStyleSession(app,key);if(!s.pending)s.pending=nlohmann::json(s.draft);s.draft=sheet;
 auto current=nlohmann::json(sheet);if(finish){if(*s.pending!=current){s.undo.push_back(*s.pending);if(s.undo.size()>128)s.undo.erase(s.undo.begin());s.redo.clear();}s.pending.reset();}
 auto* rm=app.GetResourceManager();auto resource=rm->Get<UIResource>(key);if(!resource)resource=rm->Load<UIResource>(key);resource->ReplaceData(UIResource{{},std::make_shared<ui::StyleSheet>(sheet)},ResourceLoadState::Loaded,false);rm->SetUIDirty(key,current!=s.disk);if(current!=s.disk)m_DirtyUIStyles.insert(key);else m_DirtyUIStyles.erase(key);return true;
 }catch(const std::exception& e){m_EditorError=e.what();return false;}}
bool EditorLayer::SaveUIStyle(Application& app,const std::string& key){try{
 if(app.IsEditorPlayMode())return false;auto& s=UIStyleSession(app,key);if(s.pending)EditUIStyle(app,key,s.draft,true);auto disk=UILoader::Load(key,{});if(!disk.success||!disk.data.styles||nlohmann::json(*disk.data.styles)!=s.disk)throw std::runtime_error("Style changed on disk; reload/discard the draft before overwriting");if(!ui::SaveStyles(key,s.draft,&m_EditorError))return false;s.disk=nlohmann::json(s.draft);app.GetResourceManager()->SetUIDirty(key,false);m_DirtyUIStyles.erase(key);return true;
 }catch(const std::exception& e){m_EditorError=e.what();return false;}}
bool EditorLayer::UndoUIStyle(Application& app,const std::string& key,bool redo){try{
 if(app.IsEditorPlayMode())return false;auto& s=UIStyleSession(app,key);if(s.pending)EditUIStyle(app,key,s.draft,true);auto& from=redo?s.redo:s.undo;auto& to=redo?s.undo:s.redo;if(from.empty())return false;auto next=from.back().get<ui::StyleSheet>();from.pop_back();to.push_back(s.draft);s.draft=next;auto* rm=app.GetResourceManager();rm->Get<UIResource>(key)->ReplaceData(UIResource{{},std::make_shared<ui::StyleSheet>(next)},ResourceLoadState::Loaded,false);rm->SetUIDirty(key,nlohmann::json(next)!=s.disk);if(nlohmann::json(next)!=s.disk)m_DirtyUIStyles.insert(key);else m_DirtyUIStyles.erase(key);return true;
 }catch(const std::exception& e){m_EditorError=e.what();return false;}}
void EditorLayer::DrawUI4Properties(Application& app,const std::string& key,ui::Document& d,ui::Widget& w,bool& changed){
 if(ImGui::CollapsingHeader("Styles")){auto selected=key+"/"+w.id;if(m_UI4Selection!=selected){m_UI4Selection=selected;m_UIStyleKey=w.styleAsset;m_UIStyleId=w.styleId;}
 ImGui::InputText("Style Asset",&m_UIStyleKey);ImGui::InputText("Style ID",&m_UIStyleId);
 if(ImGui::Button("Apply Style"))try{auto loaded=UILoader::Load(m_UIStyleKey,{});if(!loaded.success||!loaded.data.styles||!loaded.data.styles->styles.contains(m_UIStyleId)||loaded.data.styles->styles.at(m_UIStyleId).type!=w.type)throw std::runtime_error("Style missing or incompatible");w.styleAsset=m_UIStyleKey;w.styleId=m_UIStyleId;w.styleOverrides.clear();changed=true;}catch(const std::exception& e){m_EditorError=e.what();}
 ImGui::SameLine();if(ImGui::Button("Clear Style")){w.styleAsset.clear();w.styleId.clear();w.styleOverrides.clear();m_UIStyleKey.clear();m_UIStyleId.clear();changed=true;}
 if(ImGui::Button("New Style Asset")){
  ui::StyleSheet sheet;ui::Style style;style.type=w.type;style.properties={{"color",w.color},{"opacity",w.opacity}};if(w.type=="Button"){style.properties["hoverColor"]=w.hoverColor;style.properties["pressedColor"]=w.pressedColor;style.properties["disabledColor"]=w.disabledColor;style.properties["transitionDuration"]=w.transitionDuration;}
  sheet.styles["Primary"+w.type]=style;auto path="ui/Style-"+ecs::NewSceneIdentity()+".uistyle.json";if(ui::SaveStyles(path,sheet,&m_EditorError)){w.styleAsset=m_UIStyleKey=path;w.styleId=m_UIStyleId=sheet.styles.begin()->first;w.styleOverrides.clear();changed=true;m_AssetListsDirty=true;}
 }
 if(!w.styleAsset.empty()){
  ImGui::TextWrapped("Canvas uses shared values; controls above edit authored fallback values and create explicit overrides.");
  auto overrides=w.styleOverrides;for(auto& p:overrides){ImGui::PushID(p.c_str());ImGui::Text("Override: %s",p.c_str());ImGui::SameLine();if(ImGui::SmallButton("Reset to Style")){std::erase(w.styleOverrides,p);changed=true;}ImGui::PopID();}
  try{auto& session=UIStyleSession(app,w.styleAsset);auto sheet=session.draft;
   if(!m_UIStyleId.empty()&&!sheet.styles.contains(m_UIStyleId)&&sheet.styles.contains(w.styleId)&&ImGui::Button("Create Style ID from Current")){sheet.styles[m_UIStyleId]=sheet.styles.at(w.styleId);if(EditUIStyle(app,w.styleAsset,sheet)){w.styleId=m_UIStyleId;w.styleOverrides.clear();changed=true;}}

   if(session.pending&&ImGui::IsKeyPressed(ImGuiKey_Escape,false)){auto previous=session.pending->get<ui::StyleSheet>();session.pending.reset();session.draft=previous;app.GetResourceManager()->Get<UIResource>(w.styleAsset)->ReplaceData(UIResource{{},std::make_shared<ui::StyleSheet>(previous)},ResourceLoadState::Loaded,false);app.GetResourceManager()->SetUIDirty(w.styleAsset,nlohmann::json(previous)!=session.disk);if(nlohmann::json(previous)!=session.disk)m_DirtyUIStyles.insert(w.styleAsset);else m_DirtyUIStyles.erase(w.styleAsset);m_EscapeConsumed=true;sheet=previous;}
   if(ImGui::Button("Undo Style"))UndoUIStyle(app,w.styleAsset);ImGui::SameLine();if(ImGui::Button("Redo Style"))UndoUIStyle(app,w.styleAsset,true);ImGui::SameLine();if(ImGui::Button("Save Style"))SaveUIStyle(app,w.styleAsset);
   if(ImGui::Button("Reload Style / Discard Draft")){auto loaded=UILoader::Load(w.styleAsset,{});if(loaded.success&&loaded.data.styles){session={};session.draft=*loaded.data.styles;session.disk=nlohmann::json(session.draft);app.GetResourceManager()->SetUIDirty(w.styleAsset,false);m_DirtyUIStyles.erase(w.styleAsset);app.GetResourceManager()->Reload<UIResource>(w.styleAsset);}}
   if(sheet.styles.contains(w.styleId)&&ImGui::TreeNode("Edit Shared Style")){
    auto& style=sheet.styles.at(w.styleId);ui::Widget effective;effective.type=w.type;ui::ApplyStyle(effective,effective,&style);bool edited=false;
    auto color=[&](const char* name,ui::Color& value){if(ImGui::ColorEdit4(name,value.data())){style.properties[name]=value;edited=true;}};
    color("color",effective.color);if(ImGui::SliderFloat("Style Opacity",&effective.opacity,0,1)){style.properties["opacity"]=effective.opacity;edited=true;}
    if(w.type=="Button"){color("hoverColor",effective.hoverColor);color("pressedColor",effective.pressedColor);color("disabledColor",effective.disabledColor);if(ImGui::DragFloat("Style Transition",&effective.transitionDuration,.01f,0,10)){style.properties["transitionDuration"]=effective.transitionDuration;edited=true;}}
    if(w.type=="Text"){auto fontKey=effective.font;if(ImGui::InputText("Style Font Asset",&fontKey)){style.properties["font"]=fontKey;edited=true;}if(ImGui::DragFloat("Style Font Size",&effective.fontSize,1,1,256)){style.properties["fontSize"]=effective.fontSize;edited=true;}if(ImGui::Combo("Style Text Alignment",&effective.textAlignment,"Left\0Center\0Right\0")){style.properties["textAlignment"]=effective.textAlignment;edited=true;}}
    if(w.type=="Panel"){if(ImGui::DragFloat4("Style Padding",effective.padding.data(),1,0,10000)){style.properties["padding"]=effective.padding;edited=true;}if(ImGui::DragFloat("Style Spacing",&effective.spacing,1,0,10000)){style.properties["spacing"]=effective.spacing;edited=true;}}
    if(ImGui::DragFloat2("Style Fixed Size",effective.size.data(),1,0,100000)){style.properties["size"]=effective.size;edited=true;}
    if(edited)EditUIStyle(app,w.styleAsset,sheet,!ImGui::IsAnyItemActive());else if(session.pending&&!ImGui::IsAnyItemActive())EditUIStyle(app,w.styleAsset,session.draft,true);ImGui::TreePop();
   }
  }catch(const std::exception& e){ImGui::TextWrapped("Style: %s",e.what());}
 }
 }
 if(ImGui::CollapsingHeader("Animations")){
 if(ImGui::BeginCombo("Clip",m_UIClipId.c_str())){for(auto& c:d.animations)if(ImGui::Selectable(c.id.c_str(),c.id==m_UIClipId))m_UIClipId=c.id;ImGui::EndCombo();}
 if(ImGui::Button("New Animation")){ui::AnimationClip clip;clip.id="Animation";while(std::any_of(d.animations.begin(),d.animations.end(),[&](auto& c){return c.id==clip.id;}))clip.id+="_";clip.widget=w.id;d.animations.push_back(clip);m_UIClipId=clip.id;changed=true;}
 auto at=std::find_if(d.animations.begin(),d.animations.end(),[&](auto& c){return c.id==m_UIClipId;});if(at!=d.animations.end()){
  auto& clip=*at;if(m_UIClipNameSource!=clip.id){m_UIClipNameSource=clip.id;m_UIClipNameDraft=clip.id;}
  ImGui::InputText("Animation ID",&m_UIClipNameDraft);if(ImGui::Button("Rename Clip")){
   if(!m_UIClipNameDraft.empty()&&m_UIClipNameDraft.size()<=128&&ui::ValidUTF8(m_UIClipNameDraft)&&std::none_of(d.animations.begin(),d.animations.end(),[&](auto& c){return c.id==m_UIClipNameDraft && &c!=&clip;})){clip.id=m_UIClipId=m_UIClipNameSource=m_UIClipNameDraft;changed=true;}else m_EditorError="Animation ID must be valid and unique";
  }
  if(ImGui::BeginCombo("Target Widget",clip.widget.c_str())){for(auto& target:d.widgets)if(ImGui::Selectable(target.id.c_str(),target.id==clip.widget)){clip.widget=target.id;changed=true;}ImGui::EndCombo();}
  if(ImGui::BeginCombo("Property",clip.property.c_str())){for(auto* prop:{"opacity","offsetX","offsetY","scaleX","scaleY","color","progress"})if(ImGui::Selectable(prop,clip.property==prop)){clip.property=prop;clip.from=std::string(prop)=="color"?nlohmann::json(ui::Color{1,1,1,0}):nlohmann::json(0);clip.to=std::string(prop)=="color"?nlohmann::json(ui::Color{1,1,1,1}):nlohmann::json(1);changed=true;}ImGui::EndCombo();}
  if(clip.property=="color"){auto from=clip.from.get<ui::Color>(),to=clip.to.get<ui::Color>();if(ImGui::ColorEdit4("From",from.data())){clip.from=from;changed=true;}if(ImGui::ColorEdit4("To",to.data())){clip.to=to;changed=true;}}
  else{float from=clip.from.get<float>(),to=clip.to.get<float>();if(ImGui::DragFloat("From",&from,.01f)){clip.from=from;changed=true;}if(ImGui::DragFloat("To",&to,.01f)){clip.to=to;changed=true;}}
  changed|=ImGui::DragFloat("Duration",&clip.duration,.01f,.0001f,3600,"%.3f",ImGuiSliderFlags_AlwaysClamp);changed|=ImGui::DragFloat("Delay",&clip.delay,.01f,0,3600,"%.2f",ImGuiSliderFlags_AlwaysClamp);changed|=ImGui::DragFloat("Speed",&clip.speed,.01f,.001f,100,"%.2f",ImGuiSliderFlags_AlwaysClamp);
  if(ImGui::BeginCombo("Easing",clip.easing.c_str())){for(auto* easing:{"linear","easeIn","easeOut","easeInOut"})if(ImGui::Selectable(easing,clip.easing==easing)){clip.easing=easing;changed=true;}ImGui::EndCombo();}
  changed|=ImGui::Checkbox("Loop",&clip.loop);if(!clip.loop)clip.pingPong=false;if(clip.loop)changed|=ImGui::Checkbox("Ping Pong",&clip.pingPong);
  auto id=clip.id;if(ImGui::Button("Play Preview")){app.GetRuntimeUI().Preview(d,*app.GetResourceManager());app.GetRuntimeUI().PreviewPlay(id);}ImGui::SameLine();if(ImGui::Button("Stop Preview"))app.GetRuntimeUI().PreviewStop();
  if(ImGui::Button("Delete Clip")){std::erase_if(d.animations,[&](auto& c){return c.id==id;});m_UIClipId.clear();changed=true;}
 }
 }
}
}
