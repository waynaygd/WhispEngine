#include "EditorLayer.h"
#include "LuaCodeEditor.h"
#include "ScriptEditor.h"
#include "EditorTheme.h"
#include "../core/Application.h"
#include "../core/AssetPaths.h"
#include <imgui_internal.h>
namespace editor {
bool EditorLayer::ScriptDraftsDirty()const{for(auto& [origin,s]:m_ScriptSessions)if(s&&s->Dirty())return true;return false;}
bool EditorLayer::SaveLuaDocument(Application& app,const std::string& key){
 try{if(app.IsEditorPlayMode())throw std::runtime_error("Stop Play to save Lua. Play uses saved source; drafts are retained.");auto& s=ScriptSession(app,key);if(!s.Save()){m_EditorError=s.error;return false;}m_EditorError.clear();return true;}catch(const std::exception& e){m_EditorError=e.what();return false;}
}
bool EditorLayer::SaveAllDocuments(Application& app){if(app.IsEditorPlayMode()){m_EditorError="Stop Play to Save All";return false;}return ResolveProjectEdits(app,int(Application::ProjectDecision::Save),&m_EditorError);}
bool EditorLayer::OpenScriptAtLine(Application& app,const std::string& key,int line){
 try{(void)ResolveEditableLuaScript(key);auto& s=ScriptSession(app,key);if(line>0)s.GoTo(line);if(!OpenDocument(app,key))return false;m_SelectedAsset=key;m_SelectedAssetKind="Lua Script";NavigateAssetFolder(AssetPaths::ToUtf8String(std::filesystem::u8path(key).parent_path()));return true;}catch(const std::exception& e){m_EditorError=e.what();return false;}
}
void EditorLayer::DrawScriptDocument(Application& app){
 auto it=std::find_if(m_Documents.begin(),m_Documents.end(),[&](auto& d){return d.id==m_ActiveDocument&&d.type==DocumentType::Lua;});if(it==m_Documents.end())return;
 PrepareWorkspacePrimaryWindow();
 if(ImGui::Begin("Script Document",nullptr,m_WorkspaceFocus?ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings:0))try{
  const auto key=it->key,canonical=it->canonical;auto& s=ScriptSession(app,key);ImGui::PushID(canonical.c_str());
  ImGui::Text("%s%s",key.c_str(),s.Dirty()?" *":"");
  ImGui::BeginDisabled(app.IsEditorPlayMode());if(ImGui::Button("Save Script"))SaveLuaDocument(app,key);ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button("Validate Script"))s.Validate();ImGui::SameLine();if(ImGui::Button("Reload file"))ImGui::OpenPopup("Reload Script?");ImGui::SameLine();if(ImGui::Button("Open in Visual Studio"))OpenLuaScriptInVisualStudio(key,&m_EditorError);
  if(ImGui::BeginPopupModal("Reload Script?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::TextWrapped("Reload the real file and discard this draft and its Undo history?");if(ImGui::Button("Reload")){if(s.Reload())ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();if(!s.error.empty())ErrorText(s.error.c_str());ImGui::EndPopup();}
  if(ImGui::GetTime()>=m_ScriptExternalPoll){m_ScriptExternalPoll=ImGui::GetTime()+1;s.CheckExternal();}
  if(s.external){
   ImGui::TextWrapped("File changed or became unavailable outside WhispEngine. Save will preserve the external file.");
   if(!s.externalAcknowledged&&ImGui::Button("Resolve external change"))ImGui::OpenPopup("External Lua change");
   if(ImGui::BeginPopupModal("External Lua change",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::TextWrapped("Reload replaces this draft. Keep Editing and Cancel preserve both draft and disk; Save remains protected until Reload.");if(ImGui::Button("Reload")){if(s.Reload())ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ImGui::Button("Keep Editing")){s.externalAcknowledged=true;ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ImGui::Button("Cancel")){ImGui::CloseCurrentPopup();}ImGui::EndPopup();}
  }
  if(app.IsEditorPlayMode())ImGui::TextWrapped("Read-only during Play. Saved source uses the existing runtime reload; unsaved drafts remain here.");
  if(!s.error.empty())ErrorText(s.error.c_str());if(!m_EditorError.empty())ErrorText(m_EditorError.c_str());
  if(!s.syntax.valid){ErrorText(s.syntax.message.c_str());if(s.syntax.line>0&&ImGui::SmallButton("Go to syntax error"))s.GoTo(s.syntax.line);}
  else ImGui::TextDisabled("UTF-8 | %zu lines | Ctrl+F Find / Ctrl+H Replace | Tab indent",s.Lines().size());
  static int line=1;ImGui::SetNextItemWidth(90);ImGui::InputInt("Line",&line);ImGui::SameLine();if(ImGui::SmallButton("Go to line"))s.GoTo(line);
  bool focus=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);if(focus&&ImGui::GetIO().KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_S,false)&&!ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel))SaveLuaDocument(app,key);
  ImGui::PushFont(MonoFont(),0);s.Draw(app.IsEditorPlayMode());ImGui::PopFont();ImGui::PopID();
 }catch(const std::exception& e){ErrorText(e.what());}ImGui::End();
}
}
