#include "EditorTheme.h"
#include "EditorLayer.h"
#include "EditorWidgets.h"
#include "WidgetDesignerTools.h"
#include "../core/Application.h"
#include "../core/AssetPaths.h"
#include "../ecs/components/UICanvasComponent.h"
#include "../ecs/components/SceneIdentityComponent.h"
#include "../resources/ResourceManager.h"
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <set>
namespace editor {
EditorLayer::UIDocumentEdit& EditorLayer::UIDocumentSession(Application& app,const std::string& key){
 auto origin=AssetPaths::ToUtf8String(ui::SafeDocumentPath(key));auto& session=m_UIDocuments[origin];if(!session.initialized){auto* resources=app.GetResourceManager();auto asset=resources->Get<UIResource>(key);if(!asset)asset=resources->Load<UIResource>(key);if(!asset->GetData().document)throw std::runtime_error(asset->GetErrorMessage());session.resourceKey=key;session.draft=*asset->GetData().document;session.disk=session.draft;session.initialized=true;session.selected=session.draft.root;}return session;
}
bool EditorLayer::EditUIDocument(Application& app,const std::string& key,const ui::Document& document,bool finish){
 try{if(app.IsEditorPlayMode())throw std::runtime_error("Stop Play to edit UI documents");auto error=ui::Validate(document);if(!error.empty())throw std::runtime_error(error);auto& session=UIDocumentSession(app,key);if(!session.pending)session.pending=nlohmann::json(session.draft);auto next=document;
  for(auto& w:next.widgets)if(!w.styleAsset.empty()){auto old=std::find_if(session.draft.widgets.begin(),session.draft.widgets.end(),[&](auto& v){return v.id==w.id;});if(old!=session.draft.widgets.end()&&old->styleAsset==w.styleAsset&&old->styleId==w.styleId){auto before=nlohmann::json(*old),after=nlohmann::json(w);for(auto& prop:ui::StyleProperties())if(before[prop]!=after[prop]&&std::find(w.styleOverrides.begin(),w.styleOverrides.end(),prop)==w.styleOverrides.end())w.styleOverrides.push_back(prop);}}
  session.draft=std::move(next);
  // Serialize the baseline once and the final state once, never on each drag frame.
  if(finish){auto current=nlohmann::json(session.draft);if(session.pending&&*session.pending!=current){session.undo.push_back(*session.pending);if(session.undo.size()>128)session.undo.erase(session.undo.begin());session.redo.clear();}session.pending.reset();session.dirty=current!=session.disk;}else session.dirty=true;
  auto* resources=app.GetResourceManager();auto resource=resources->Get<UIResource>(key);if(!resource)resource=resources->Load<UIResource>(key);resource->ReplaceData(UIResource{std::make_shared<ui::Document>(session.draft)},ResourceLoadState::Loaded,false);resources->SetUIDirty(key,session.dirty);return true;
 }catch(const std::exception& e){m_EditorError=e.what();return false;}
}
bool EditorLayer::UndoUIDocument(Application& app,const std::string& key,bool redo){
 try{if(app.IsEditorPlayMode())return false;auto& session=UIDocumentSession(app,key);if(session.pending)EditUIDocument(app,key,session.draft,true);auto& source=redo?session.redo:session.undo;auto& destination=redo?session.undo:session.redo;if(source.empty())return false;destination.push_back(session.draft);session.draft=source.back().get<ui::Document>();source.pop_back();{
  // Recognize a pure ID rename through the shared validated operation. Editor
  // references follow it even if another widget was selected before Undo.
  const auto& priorWidgets=destination.back().at("widgets");std::optional<std::size_t> renameIndex;bool multiple=false;
  if(priorWidgets.size()==session.draft.widgets.size())for(std::size_t i=0;i<priorWidgets.size();++i)if(priorWidgets[i].at("id")!=session.draft.widgets[i].id){if(renameIndex){multiple=true;break;}renameIndex=i;}
  if(renameIndex&&!multiple)try{
   auto candidate=destination.back().get<ui::Document>();const auto previous=candidate.widgets[*renameIndex].id;const auto replacement=session.draft.widgets[*renameIndex].id;widgets::Rename(candidate,previous,replacement);
   if(nlohmann::json(candidate)==nlohmann::json(session.draft)){if(session.lockedWidgets.erase(previous))session.lockedWidgets.insert(replacement);if(session.selected==previous)session.selected=replacement;}
  }catch(const std::exception&){}
  if(std::none_of(session.draft.widgets.begin(),session.draft.widgets.end(),[&](auto& w){return w.id==session.selected;}))session.selected=session.draft.root;
 }session.dirty=nlohmann::json(session.draft)!=session.disk;auto* resources=app.GetResourceManager();auto resource=resources->Get<UIResource>(key);if(!resource)resource=resources->Load<UIResource>(key);resource->ReplaceData(UIResource{std::make_shared<ui::Document>(session.draft)},ResourceLoadState::Loaded,false);resources->SetUIDirty(key,session.dirty);return true;}catch(const std::exception& e){m_EditorError=e.what();return false;}
}
bool EditorLayer::SaveUIDocument(Application& app,const std::string& key){
 try{if(app.IsEditorPlayMode())return false;auto& session=UIDocumentSession(app,key);if(session.pending)EditUIDocument(app,key,session.draft,true);auto disk=UILoader::Load(key,{});if(!disk.success||nlohmann::json(*disk.data.document)!=session.disk)throw std::runtime_error("UI document changed on disk. Reload explicitly before overwriting external edits.");if(!UILoader::Save(key,session.draft,&m_EditorError))return false;session.disk=session.draft;session.dirty=false;app.GetResourceManager()->SetUIDirty(key,false);m_EditorError.clear();return true;}catch(const std::exception& e){m_EditorError=e.what();return false;}
}
bool EditorLayer::ReloadUIDocument(Application& app,const std::string& key){
 try{if(app.IsEditorPlayMode()||m_UIWidgetDrag)return false;auto loaded=UILoader::Load(key,{});if(!loaded.success||!loaded.data.document)throw std::runtime_error(loaded.errorMessage);auto& session=UIDocumentSession(app,key);UIDocumentEdit next;next.resourceKey=key;next.draft=*loaded.data.document;next.disk=next.draft;next.selected=next.draft.root;next.initialized=true;
 next.lockedWidgets=session.lockedWidgets;std::erase_if(next.lockedWidgets,[&](const auto& id){return std::none_of(next.draft.widgets.begin(),next.draft.widgets.end(),[&](const auto& w){return w.id==id;});});
 auto* resources=app.GetResourceManager();auto asset=resources->Get<UIResource>(key);if(!asset)asset=resources->Load<UIResource>(key);asset->ReplaceData(loaded.data,ResourceLoadState::Loaded,false);resources->SetUIDirty(key,false);session=std::move(next);m_UIFit=true;m_EditorError.clear();return true;}catch(const std::exception& e){m_EditorError=e.what();return false;}
}
ecs::Entity EditorLayer::CreateUICanvas(Application& app,const std::string& existing){
 if(app.IsEditorPlayMode())return {};std::string key=existing;try{if(key.empty()){key="ui/Canvas-"+ecs::NewSceneIdentity()+".ui.json";if(!UILoader::Save(key,ui::EmptyDocument(),&m_EditorError))return {};m_AssetListsDirty=true;}else {auto loaded=UILoader::Load(key,{});if(!loaded.success||!loaded.data.document)throw std::runtime_error("Missing or invalid UI Document");}FinishTransaction(app);auto previous=m_Selection.active;auto entity=app.CreateEmptyEntity("UI Canvas");app.GetWorld().AddComponent<ecs::UICanvasComponent>(entity).document=key;SelectEntityFromUser(app,entity);EntitySnapshot before;before.entity=entity;before.world=entity.world;before.previousSelection=previous;CommitUndo(app,"Create UI Canvas",before);return entity;}catch(const std::exception& e){m_EditorError=e.what();return {};}
}
void EditorLayer::DrawUICanvasInspector(Application& app,ecs::Entity entity){
 auto* canvas=app.GetWorld().GetComponent<ecs::UICanvasComponent>(entity);if(!canvas)return;ImGui::TextWrapped("Screen Space Overlay. UI documents have their own Undo UI / Redo UI and explicit Save UI.");
 ImGui::BeginDisabled(app.IsEditorPlayMode());
 if(ImGui::BeginCombo("Choose UI Document",canvas->document.c_str())){for(auto& entry:m_AssetCatalog.Entries())if(entry.key.ends_with(".ui.json"))if(ImGui::Selectable(entry.key.c_str(),entry.key==canvas->document))SetProperty(app,entity,ecs::ComponentId("UICanvas"),"document",entry.key);ImGui::EndCombo();}
 if(ImGui::Button("New UI Document")){auto key="ui/Canvas-"+ecs::NewSceneIdentity()+".ui.json";if(UILoader::Save(key,ui::EmptyDocument(),&m_EditorError)){SetProperty(app,entity,ecs::ComponentId("UICanvas"),"document",key);m_AssetListsDirty=true;}}
 std::string drop;if(AcceptAssetDrop(ecs::AssetKind::UIDocument,drop))SetProperty(app,entity,ecs::ComponentId("UICanvas"),"document",drop);
 if(canvas->document.empty()){ImGui::EndDisabled();return;}
 if(ImGui::Button("Open in Widget Designer"))OpenUIDesigner(app,canvas->document);
 ImGui::EndDisabled();
 DrawUIDocumentProperties(app,canvas->document,true);
}
void EditorLayer::DrawUIDocumentProperties(Application& app,const std::string& key,bool hierarchy){
 ImGui::PushItemWidth(std::max(80.f,ImGui::GetContentRegionAvail().x-150.f));
 auto pair=[&](const char* label,ui::Pair& value,float speed,float minimum,float maximum){ImGui::TextUnformatted(label);ImGui::SetNextItemWidth(-FLT_MIN);return ImGui::DragFloat2((std::string("##")+label).c_str(),value.data(),speed,minimum,maximum);};
 ImGui::BeginDisabled(app.IsEditorPlayMode());
 try{auto& session=UIDocumentSession(app,key);if(session.pending&&!m_UIWidgetDrag&&ImGui::IsKeyPressed(ImGuiKey_Escape,false)){CancelUIDocumentEdit(app,key);m_EscapeConsumed=true;}auto edited=session.draft;bool changed=false;
  if(ImGui::Button("Save UI"))SaveUIDocument(app,key);Inline("Undo UI");if(Action("Undo UI","Undo the last UI edit. No edits available when disabled.",session.pending.has_value()||!session.undo.empty()))UndoUIDocument(app,key);Inline("Redo UI");if(Action("Redo UI","Redo the last undone UI edit. No redo available when disabled.",!session.redo.empty()))UndoUIDocument(app,key,true);
  if(ImGui::Button("Reload UI from disk")){ImGui::OpenPopup("Reload UI?");}if(ImGui::BeginPopup("Reload UI?")){ImGui::TextWrapped("Discard unsaved UI changes and history?");if(ImGui::Button("Reload")){ReloadUIDocument(app,key);ImGui::CloseCurrentPopup();}ImGui::EndPopup();}
  ImGui::TextUnformatted(session.dirty?"UI Document: unsaved changes":"UI Document: saved");
  edited=session.draft;ImGui::TextUnformatted("Canvas Name");ImGui::SetNextItemWidth(-FLT_MIN);changed|=ImGui::InputText("##Canvas Name",&edited.name);changed|=ImGui::Checkbox("Canvas Enabled",&edited.enabled);changed|=pair("Reference Resolution",edited.referenceResolution,1,1,16384);ImGui::TextWrapped("Scale: With Screen Size (uniform fit)");ImGui::TextUnformatted("Document Sort Order");ImGui::SetNextItemWidth(-FLT_MIN);changed|=ImGui::DragInt("##Document Sort Order",&edited.sortOrder,1,-1000000,1000000);
  if(hierarchy&&ImGui::BeginCombo("Add Widget","Panel / Text / Image / Progress Bar / Button")){for(const char* type:{"Panel","Text","Image","ProgressBar","Button"})if(ImGui::Selectable(type)){
    ui::Widget w;w.type=type;w.id=std::string(type)+std::to_string(edited.widgets.size());while(std::any_of(edited.widgets.begin(),edited.widgets.end(),[&](auto& n){return n.id==w.id;}))w.id+="_";
    auto parent=std::find_if(edited.widgets.begin(),edited.widgets.end(),[&](auto& n){return n.id==session.selected;});w.parent=parent!=edited.widgets.end()&&(parent->type=="Panel"||parent->type=="Button")?parent->id:edited.root;w.position={16,16};if(w.type=="Text")w.text="Text / Текст";if(edited.widgets.empty()){auto root=ui::EmptyDocument().widgets[0];edited.root=root.id;edited.widgets.push_back(root);w.parent=root.id;}session.selected=w.id;edited.widgets.push_back(w);changed=true;
  }ImGui::EndCombo();}
  if(hierarchy){auto tree=[&](auto&& self,const std::string& parent,int depth)->void{if(depth>64)return;for(auto& w:edited.widgets)if(w.parent==parent){ImGui::Indent(depth?10.f:0.f);if(ImGui::Selectable((w.id+" ("+w.type+")").c_str(),session.selected==w.id))session.selected=w.id;ImGui::Unindent(depth?10.f:0.f);self(self,w.id,depth+1);}};tree(tree,"",0);}
  auto found=std::find_if(edited.widgets.begin(),edited.widgets.end(),[&](auto& w){return w.id==session.selected;});
  if(found!=edited.widgets.end()){auto& w=*found;ImGui::SeparatorText("Widget Properties");ImGui::Text("ID: %s",w.id.c_str());ImGui::Text("Type: %s",w.type.c_str());
   if(w.id!=edited.root&&ImGui::BeginCombo("Parent",w.parent.c_str())){for(auto& parent:edited.widgets)if((parent.type=="Panel"||parent.type=="Button")&&parent.id!=w.id)if(ImGui::Selectable(parent.id.c_str(),w.parent==parent.id)){try{auto next=edited;widgets::Reparent(next,w.id,parent.id,widgets::Canvas(edited,key==m_DesignDocument?m_UIPreviewSize:edited.referenceResolution));w=widgets::Find(next,w.id);changed=true;}catch(const std::exception& e){m_EditorError=e.what();}break;}ImGui::EndCombo();}
   changed|=ImGui::Checkbox("Visible",&w.visible);changed|=ImGui::SliderFloat("Opacity",&w.opacity,0,1);changed|=ImGui::DragInt("Local Z-order",&w.zOrder,1,-1000000,1000000);
   bool managed=ui::ManagedByLayout(edited,w);ImGui::BeginDisabled(managed);
   changed|=pair("Anchor Min",w.anchorMin,.01f,0,1);changed|=pair("Anchor Max",w.anchorMax,.01f,0,1);changed|=pair("Pivot / Alignment",w.pivot,.01f,0,1);changed|=pair("Anchored Position",w.position,1,-100000,100000);changed|=pair("Size Delta",w.size,1,-100000,100000);ImGui::EndDisabled();changed|=ImGui::ColorEdit4(w.type=="Button"?"Normal Color":"Color",w.color.data());
   DrawUI4Properties(app,key,edited,w,changed);
   if(w.type=="Panel"||w.type=="Button")changed|=ImGui::Checkbox("Clip Children",&w.clipChildren);
   bool required=std::find(edited.requiredWidgets.begin(),edited.requiredWidgets.end(),w.id)!=edited.requiredWidgets.end();
   if(ImGui::Checkbox("Required Widget ID",&required)){if(required)edited.requiredWidgets.push_back(w.id);else std::erase(edited.requiredWidgets,w.id);changed=true;}
   if(w.type=="Button"){changed|=ImGui::Checkbox("Enabled",&w.enabled);changed|=ImGui::ColorEdit4("Hover Color",w.hoverColor.data());changed|=ImGui::ColorEdit4("Pressed Color",w.pressedColor.data());changed|=ImGui::ColorEdit4("Disabled Color",w.disabledColor.data());}
   if(w.type=="Text"){ImGui::TextUnformatted("Text");ImGui::SetNextItemWidth(-FLT_MIN);changed|=ImGui::InputTextMultiline("##Text",&w.text,{0,80});changed|=ImGui::DragFloat("Font Size",&w.fontSize,1,1,256);changed|=ImGui::Combo("Text Alignment",&w.textAlignment,"Left\0Center\0Right\0");ImGui::TextWrapped("Font: Segoe UI, independent runtime atlas. Latin and Cyrillic; explicit newlines.");}
   if(w.type=="ProgressBar"){changed|=ImGui::SliderFloat("Value",&w.value,0,1);changed|=ImGui::ColorEdit4("Fill Color",w.fillColor.data());}
   if(w.type=="Image"){ImGui::TextUnformatted("Texture (drop an Asset Browser texture here)");ImGui::SetNextItemWidth(-FLT_MIN);changed|=ImGui::InputText("##Texture",&w.texture);changed|=AcceptAssetDrop(ecs::AssetKind::Texture,w.texture);ImGui::TextUnformatted("UV Rect");ImGui::SetNextItemWidth(-FLT_MIN);changed|=ImGui::DragFloat4("##UV Rect",w.uv.data(),.01f,0,1);if(!w.texture.empty()){auto texture=app.GetResourceManager()->Get<TextureResource>(w.texture);if(texture&&texture->HasError())ImGui::TextWrapped("Image error: %s",texture->GetErrorMessage().c_str());}}
   if(ImGui::Button("Remove Widget Subtree")){std::set<std::string> removed{w.id};for(bool more=true;more;){more=false;for(auto& node:edited.widgets)if(removed.contains(node.parent)&&removed.insert(node.id).second)more=true;}std::erase_if(edited.widgets,[&](auto& node){return removed.contains(node.id);});std::erase_if(edited.animations,[&](auto& c){return removed.contains(c.widget);});std::erase_if(edited.requiredWidgets,[&](auto& id){return removed.contains(id);});if(removed.contains(edited.root))edited.root.clear();session.selected=edited.root;changed=true;}
  }
  if(changed)EditUIDocument(app,key,edited,!ImGui::IsAnyItemActive());else if(session.pending&&!m_UIWidgetDrag&&!ImGui::IsAnyItemActive())EditUIDocument(app,key,session.draft,true);
 }catch(const std::exception& e){ImGui::TextWrapped("UI Document error: %s",e.what());}
 ImGui::EndDisabled();ImGui::PopItemWidth();
}
}
