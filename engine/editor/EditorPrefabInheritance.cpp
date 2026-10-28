#include "EditorLayer.h"
#include "../core/Application.h"
#include "../scripting/Prefab.h"
#include "../scene/SceneSerializer.h"
#include "../ecs/components/PrefabInstanceComponent.h"
#include <imgui.h>

namespace editor {
using Json=nlohmann::json;
std::string EditorLayer::PrefabMember(Application& app,ecs::Entity e)const{
 auto* link=app.GetWorld().GetComponent<ecs::PrefabInstanceComponent>(m_PrefabRoot);auto identity=SceneSerializer::EntitySceneId(app.GetWorld(),e);
 if(link)for(auto& [member,reference]:link->members.items())if(reference.value("$entity",std::string())==identity)return member;
 return identity;
}
void EditorLayer::TrackPrefabChanges(Application& app){
 if(m_PrefabAuthored.is_null()||!app.IsPrefabEditing())return;
 try{auto current=scripting::CapturePrefab(app.GetWorld(),m_PrefabRoot);m_PrefabAuthored=scripting::EditPrefabDelta(m_PrefabAuthored,m_PrefabObserved,current);m_PrefabObserved=std::move(current);}
 catch(const std::exception& error){m_EditorError=std::string("Child draft is not yet a valid single-root Prefab: ")+error.what();}
}
bool EditorLayer::CreateChildPrefab(Application& app,const std::string& parent,const std::string& key){
 try{
  if(app.IsEditorPlayMode())throw std::runtime_error("Stop Play before creating a Child Prefab");
  if(parent==key)throw std::runtime_error("Choose a new Child Prefab key");
  (void)scripting::SafeAsset(key,".prefab.json");
  auto child=scripting::NewChildPrefab(parent);auto resolved=scripting::ResolvePrefabDelta(child,scripting::ReadPrefab(parent));(void)resolved;
  FinishTransaction(app);auto path=scripting::SafeAsset(key,".prefab.json");
  if(!SceneSerializer::WriteAtomic(path,child,&m_EditorError,false))throw std::runtime_error(m_EditorError);
  scripting::InvalidatePrefabCache();m_AssetListsDirty=true;OpenDocument(app,key);return true;
 }catch(const std::exception& error){m_EditorError=error.what();return false;}
}
bool EditorLayer::OverridePrefabProperty(Application& app,ecs::Entity entity,const std::string& component,const std::string& path){
 try{
  if(m_PrefabAuthored.is_null()||!app.IsPrefabEditing())throw std::runtime_error("Open a Child Prefab first");FinishTransaction(app);
  auto member=PrefabMember(app,entity);bool inherited=false;for(auto& node:m_PrefabParentBase.at("entities"))if(node.at("id")==member)inherited=node.at("components").contains(component);if(!inherited)throw std::runtime_error("Locally added components belong to Child and have no Parent property to override");auto capture=scripting::CapturePrefab(app.GetWorld(),m_PrefabRoot);Json values;
  for(auto& n:capture.at("entities"))if(n.at("id")==member)values=n.at("components").at(component);
  Json::json_pointer pointer(path);auto value=values.at(pointer);auto before=CaptureSelectedEntity(app,entity);auto next=m_PrefabAuthored;
  auto& node=next["overrides"][member];if(node.is_null())node={{"components",Json::object()}};
  auto& edit=node["components"][component];if(edit.contains("added"))throw std::runtime_error("Locally added components already belong to Child");
  edit["properties"][path]={{"value",value},{"type",value.is_number()?"number":value.type_name()}};
  if(component=="LuaScript"&&values.contains("scriptPath"))edit["script"]=values.at("scriptPath");
  scripting::ResolvePrefabDelta(next,m_PrefabParentBase);m_PrefabAuthored=next;CommitUndo(app,"Override Prefab Property",std::move(before));return true;
 }catch(const std::exception& error){m_EditorError=error.what();return false;}
}
bool EditorLayer::ResetPrefabProperty(Application& app,ecs::Entity entity,const std::string& component,const std::string& path){return ResetPrefabOverride(app,PrefabMember(app,entity),component,path);}
bool EditorLayer::ResetPrefabOverride(Application& app,const std::string& member,const std::string& component,const std::string& path){
 try{
  if(m_PrefabAuthored.is_null()||!app.IsPrefabEditing())throw std::runtime_error("Open a Child Prefab first");FinishTransaction(app);auto next=m_PrefabAuthored;
  if(component=="$parent"){}
  else if(component=="$entity"){auto& list=next["suppressedEntities"];list.erase(std::remove(list.begin(),list.end(),member),list.end());}
  else {
   if(!next["overrides"].contains(member))return true;auto& node=next["overrides"][member];auto& edits=node["components"];
   if(path.empty())edits.erase(component);else if(edits.contains(component)&&edits[component].contains("properties")){
    edits[component]["properties"].erase(path);if(edits[component]["properties"].empty())edits.erase(component);
   }
   if(edits.empty()&&!node.contains("parent"))next["overrides"].erase(member);
  }
  auto parent=scripting::ReadPrefab(next.at("parentPrefab").get<std::string>());auto resolved=scripting::ResolvePrefabDelta(next,parent);
  auto& world=app.GetWorld();auto* link=world.GetComponent<ecs::PrefabInstanceComponent>(m_PrefabRoot);auto saved=link->members;
  // Include draft-local identities for the existing instance remapper. Do not
  // publish this temporary link unless the staged replacement validates.
  link->members=Json::object();for(auto e:world.Subtree(m_PrefabRoot)){auto local=PrefabMember(app,e);for(auto& [id,ref]:saved.items())if(ref.at("$entity")==SceneSerializer::EntitySceneId(world,e))local=id;link->members[local]={{"$entity",SceneSerializer::EntitySceneId(world,e)}};}
  Json nodes;try{nodes=scripting::InstanceSceneNodes(world,m_PrefabRoot,resolved);}catch(...){link->members=saved;throw;}link->members=saved;
  for(auto& n:nodes)if(n.at("id")==SceneSerializer::EntitySceneId(world,m_PrefabRoot))for(auto& source:resolved.at("entities"))if(source.at("id")==resolved.at("root"))n["components"]["Transform"]=source.at("components").at("Transform");
  auto desired=PrefabNodesSnapshot(app,nodes);desired.prefabAuthored=next;desired.prefabParentBase=parent;auto before=CaptureLike(app,desired);
  for(auto& item:before.related)if(item.components.empty()){item.alive=false;item.parent.reset();}
  RestoreSnapshot(app,desired);m_PrefabParentBase=parent;m_PrefabParentRevision=scripting::PrefabRevision(next.at("parentPrefab").get<std::string>());CommitUndo(app,component=="$parent"?"Merge Parent Changes":"Reset to Parent",std::move(before));m_EditorError.clear();return true;
 }catch(const std::exception& error){m_EditorError=error.what();return false;}
}
bool EditorLayer::ReloadPrefabParent(Application& app){
 try{
  FinishTransaction(app);if(IsSceneDirty())throw std::runtime_error("Child has local edits. Save/discard the draft before reloading Parent.");
  auto key=m_EditingPrefab;auto raw=scripting::ReadPrefabAsset(key);auto resolved=scripting::ReadPrefab(key);const auto active=m_ActiveDocument;
  if(!ClosePrefabEditor(app))return false;if(!StartPrefabEditor(app,key,resolved,raw))return false;m_ActiveDocument=active;return true;
 }catch(const std::exception& error){m_EditorError=error.what();return false;}
}
void EditorLayer::DrawPrefabInheritance(Application& app,ecs::Entity entity){
 if(m_PrefabAuthored.is_null())return;
 ImGui::SeparatorText("Prefab Inheritance");auto parent=m_PrefabAuthored.at("parentPrefab").get<std::string>();ImGui::TextWrapped("Parent Prefab: %s",parent.c_str());
 if(ImGui::SmallButton("Open Parent"))m_PrefabActionKey=parent;
 ImGui::SameLine();ImGui::BeginDisabled(IsSceneDirty());if(ImGui::SmallButton("Reload Parent"))m_PrefabReloadRequested=true;ImGui::EndDisabled();if(IsSceneDirty()&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Save/discard Child edits before reloading Parent.");
 if(scripting::PrefabRevision(parent)!=m_PrefabParentRevision)ImGui::TextWrapped("Parent changed. Child draft is preserved; saving is blocked until the conflict is resolved.");
 auto member=PrefabMember(app,entity);bool local=false;for(auto& n:m_PrefabAuthored.at("addedEntities"))if(n.at("id")==member)local=true;ImGui::TextUnformatted(local?"Locally Added Entity":"Inherited Entity");ImGui::TextWrapped("Member: %s",member.c_str());
 if(scripting::PrefabRevision(parent)!=m_PrefabParentRevision&&ImGui::SmallButton("Merge Parent Changes"))ResetPrefabOverride(app,m_PrefabObserved.at("root").get<std::string>(),"$parent","");
 auto copy=m_PrefabAuthored;
 if(copy["overrides"].contains(member))for(auto& [component,edit]:copy["overrides"][member]["components"].items()){
  ImGui::PushID(component.c_str());ImGui::Text("%s: %s",component.c_str(),edit.contains("added")?"Added":edit.value("removed",false)?"Suppressed":"Overridden");
  if(edit.contains("properties"))for(auto& [path,value]:edit["properties"].items()){ImGui::PushID(path.c_str());ImGui::TextWrapped("%s",path.c_str());ImGui::SameLine();if(ImGui::SmallButton("Reset to Parent"))ResetPrefabProperty(app,entity,component,path);ImGui::PopID();}
  else if(ImGui::SmallButton("Reset Component to Parent"))ResetPrefabProperty(app,entity,component,"");ImGui::PopID();
 }
 for(auto& id:copy["suppressedEntities"]){auto memberID=id.get<std::string>();ImGui::PushID(memberID.c_str());ImGui::TextWrapped("Suppressed Entity: %s",memberID.c_str());if(ImGui::SmallButton("Restore Inherited Entity"))ResetPrefabOverride(app,memberID,"$entity","");ImGui::PopID();}
}
}
