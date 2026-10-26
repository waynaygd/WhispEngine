#include "EditorLayer.h"
#include "../core/Application.h"
#include "../resources/ResourceManager.h"
#include "../resources/DataResource.h"
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <fstream>
namespace editor {
namespace {
nlohmann::json Migrate(const nlohmann::json& schema,const nlohmann::json& old){
 if(schema.at("type")=="Object"){auto result=nlohmann::json::object();for(auto it=schema.at("fields").begin();it!=schema.at("fields").end();++it)result[it.key()]=Migrate(it.value(),old.is_object()&&old.contains(it.key())?old.at(it.key()):nlohmann::json());return result;}
 if(schema.at("type")=="Array"&&old.is_array()){auto result=nlohmann::json::array();for(auto& item:old)result.push_back(Migrate(schema.at("items"),item));return result;}
 try{DataLoader::ValidateValue(schema,old);return old;}catch(...){return DataLoader::Default(schema);}
}
bool DataField(const nlohmann::json& schema,nlohmann::json& value,const char* label){
 auto type=schema.at("type").get<std::string>();bool changed=false;
 if(type=="Boolean"){bool v=value.get<bool>();if(ImGui::Checkbox(label,&v)){value=v;changed=true;}}
 else if(type=="Number"){double v=value.get<double>();if(ImGui::InputDouble(label,&v)){value=v;changed=true;}}
 else if(type=="String"||type=="Resource"){auto v=value.get<std::string>();if(ImGui::InputText(label,&v)){value=v;changed=true;}}
 else if(type=="Enum"){auto v=value.get<std::string>();if(ImGui::BeginCombo(label,v.c_str())){for(auto& choice:schema.at("values")){auto s=choice.get<std::string>();if(ImGui::Selectable(s.c_str(),v==s)){value=s;changed=true;}}ImGui::EndCombo();}}
 else if(type=="Vec3"||type=="Color"){float v[4]{};for(std::size_t i=0;i<value.size();++i)v[i]=value[i].get<float>();if(type=="Vec3"?ImGui::InputFloat3(label,v):ImGui::ColorEdit4(label,v)){for(std::size_t i=0;i<value.size();++i)value[i]=v[i];changed=true;}}
 else if(type=="Object"){if(ImGui::TreeNode(label)){for(auto it=schema.at("fields").begin();it!=schema.at("fields").end();++it){ImGui::PushID(it.key().c_str());changed|=DataField(it.value(),value[it.key()],it.key().c_str());ImGui::PopID();}ImGui::TreePop();}}
 else if(type=="Array"){if(ImGui::TreeNode(label)){int remove=-1;for(std::size_t i=0;i<value.size();++i){ImGui::PushID(int(i));changed|=DataField(schema.at("items"),value[i],std::to_string(i).c_str());ImGui::SameLine();if(ImGui::SmallButton("Remove"))remove=int(i);ImGui::PopID();}if(remove>=0){value.erase(value.begin()+remove);changed=true;}if(value.size()<2048&&ImGui::SmallButton("Add element")){value.push_back(DataLoader::Default(schema.at("items")));changed=true;}ImGui::TreePop();}}
 return changed;
}
}
bool EditorLayer::SaveDataDraft(Application& app){
 if(app.IsEditorPlayMode()){m_DataMessage="Stop Play before saving data";return false;}
 if(!DataLoader::Save(m_DataKey,m_DataDraft,&m_DataMessage))return false;
 app.GetResourceManager()->Reload<DataResource>(m_DataKey);m_DataDirty=false;m_DataMessage="Saved";return true;
}
bool EditorLayer::DrawDataInspector(Application& app){
 if(m_SelectedAssetKind!="DataAsset"&&m_SelectedAssetKind!="DataTable")return false;
 auto& resources=*app.GetResourceManager();
 if(m_DataKey!=m_SelectedAsset){
  if(m_DataDirty){ImGui::TextWrapped("Unsaved data: %s. Save or discard before selecting another resource.",m_DataKey.c_str());ImGui::BeginDisabled(app.IsEditorPlayMode());if(ImGui::Button("Save draft"))SaveDataDraft(app);ImGui::SameLine();if(ImGui::Button("Discard draft"))m_DataDirty=false;ImGui::EndDisabled();return true;}
  m_DataKey=m_SelectedAsset;auto r=resources.Load<DataResource>(m_DataKey);if(!r||!r->GetData().document){m_DataMessage=r?r->GetErrorMessage():"Load failed";m_DataDraft=nullptr;}else{m_DataDraft=*r->GetData().document;m_DataSchemaText=m_DataDraft.at("schema").dump(2);m_DataMessage.clear();}
 }
 ImGui::TextWrapped("%s%s",m_DataKey.c_str(),m_DataDirty?" *":"");if(!m_DataMessage.empty())ImGui::TextWrapped("%s",m_DataMessage.c_str());
 ImGui::BeginDisabled(app.IsEditorPlayMode());
 if(ImGui::Button("Reload / Discard")){resources.Reload<DataResource>(m_DataKey);m_DataKey.clear();m_DataDirty=false;ImGui::EndDisabled();return true;}
 if(m_DataDraft.is_null()){ImGui::EndDisabled();return true;}
 ImGui::SameLine();if(ImGui::Button("Validate")){try{DataLoader::Validate(m_DataDraft);m_DataMessage="Valid";}catch(const std::exception& e){m_DataMessage=e.what();}}
 ImGui::SameLine();if(ImGui::Button("Save"))SaveDataDraft(app);
 auto name=m_DataDraft.at("type").get<std::string>();if(ImGui::InputText("Definition type",&name)){m_DataDraft["type"]=name;m_DataDirty=true;}
 if(ImGui::TreeNode("Schema (JSON)")){ImGui::InputTextMultiline("##schema",&m_DataSchemaText,ImVec2(-1,180));if(ImGui::Button("Apply schema")){try{auto schema=nlohmann::json::parse(m_DataSchemaText);DataLoader::ValidateSchema(schema);if(schema.at("type")!="Object")throw std::runtime_error("Root schema must be Object");auto candidate=m_DataDraft;candidate["schema"]=schema;if(candidate.at("kind")=="DataAsset")candidate["value"]=Migrate(schema,candidate.at("value"));else for(auto& row:candidate["rows"])row=Migrate(schema,row);m_DataDraft=std::move(candidate);m_DataDirty=true;m_DataMessage.clear();}catch(const std::exception& e){m_DataMessage=e.what();}}ImGui::TextWrapped("Apply preserves compatible values and supplies defaults for new fields. Save validates all rows and resource references.");ImGui::TreePop();}
 try{
 auto& schema=m_DataDraft.at("schema");
 if(m_DataDraft.at("kind")=="DataAsset")m_DataDirty|=DataField(schema,m_DataDraft["value"],"Values");
 else{
  auto& rows=m_DataDraft["rows"];if(ImGui::Button("Add row")){auto row=DataLoader::Default(schema);unsigned n=1;std::string id;do{id="row_"+std::to_string(n++);}while(std::any_of(rows.begin(),rows.end(),[&](const auto& r){return r.at("id")==id;}));row["id"]=id;rows.push_back(row);m_DataDirty=true;}
  ImGui::SameLine();if(ImGui::Button("Export CSV")){auto text=DataLoader::ExportCSV(m_DataDraft);auto path=DataLoader::SafePath(m_DataKey);path+=".csv";ProjectContext::CheckNoLinks(path);std::ofstream f(path,std::ios::binary);f<<text;if(!f)throw std::runtime_error("CSV write failed");m_DataMessage="Exported "+m_DataKey+".csv";}
  ImGui::SameLine();if(ImGui::Button("Import CSV")){auto path=DataLoader::SafePath(m_DataKey);path+=".csv";ProjectContext::CheckNoLinks(path);if(std::filesystem::file_size(path)>4*1024*1024)throw std::runtime_error("CSV exceeds 4 MiB");std::ifstream f(path,std::ios::binary);std::string text((std::istreambuf_iterator<char>(f)),{});m_DataDraft=DataLoader::ImportCSV(m_DataDraft,text);m_DataDirty=true;ImGui::EndDisabled();return true;}
  auto& fields=schema.at("fields");int remove=-1;
  if(ImGui::BeginTable("DataRows",int(fields.size()+1),ImGuiTableFlags_Borders|ImGuiTableFlags_Resizable|ImGuiTableFlags_ScrollX)){
   for(auto it=fields.begin();it!=fields.end();++it)ImGui::TableSetupColumn(it.key().c_str());ImGui::TableSetupColumn("Delete");ImGui::TableHeadersRow();
   for(std::size_t i=0;i<rows.size();++i){ImGui::PushID(int(i));ImGui::TableNextRow();for(auto it=fields.begin();it!=fields.end();++it){ImGui::TableNextColumn();ImGui::PushID(it.key().c_str());ImGui::SetNextItemWidth(140);m_DataDirty|=DataField(it.value(),rows[i][it.key()],"##value");ImGui::PopID();}ImGui::TableNextColumn();if(ImGui::SmallButton("Delete"))remove=int(i);ImGui::PopID();}ImGui::EndTable();
  }if(remove>=0){rows.erase(rows.begin()+remove);m_DataDirty=true;}
 }
 }catch(const std::exception& e){m_DataMessage=e.what();}
 ImGui::EndDisabled();return true;
}
}

