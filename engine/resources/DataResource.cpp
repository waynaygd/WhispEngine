#include "DataResource.h"
#include "../core/ProjectContext.h"
#include "../core/AssetPaths.h"
#include "../ui/UIDocument.h"
#include "../scene/SceneSerializer.h"
#include <fstream>
#include <set>
#include <cmath>
using J=nlohmann::json;
namespace {
void Require(bool yes,const std::string& message){if(!yes)throw std::runtime_error("Data: "+message);}
void Text(const J& v,const std::string& at){Require(v.is_string()&&ui::ValidUTF8(v.get<std::string>())&&v.get_ref<const std::string&>().size()<=65536,at+" must be UTF-8 text");}
}
void DataLoader::ValidateSchema(const J& s,unsigned depth){
 Require(depth<24&&s.is_object()&&s.contains("type")&&s.at("type").is_string(),"invalid schema");auto t=s.at("type").get<std::string>();
 if(t=="Object"){Require(s.contains("fields")&&s.at("fields").is_object()&&s.at("fields").size()<=64,"schema fields (maximum 64)");for(auto it=s.at("fields").begin();it!=s.at("fields").end();++it){Require(!it.key().empty()&&it.key().size()<=128&&ui::ValidUTF8(it.key()),"schema field name");ValidateSchema(it.value(),depth+1);}}
 else if(t=="Array")ValidateSchema(s.at("items"),depth+1);
 else if(t=="Enum"){Require(s.contains("values")&&s.at("values").is_array()&&!s.at("values").empty()&&s.at("values").size()<=256,"enum schema values");std::set<std::string> seen;for(auto& v:s.at("values")){Text(v,"enum");Require(seen.insert(v.get<std::string>()).second,"duplicate enum option");}}
 else if(t=="Number"){double lo=s.value("min",-1e100),hi=s.value("max",1e100);Require(std::isfinite(lo)&&std::isfinite(hi)&&lo<=hi,"number schema range");}
 else Require(t=="String"||t=="Boolean"||t=="Vec3"||t=="Color"||t=="Resource","unknown schema type: "+t);
 if(s.contains("default"))ValidateValue(s,s.at("default"),"schema default",depth);
}
std::filesystem::path DataLoader::SafePath(const std::string& key){
 ProjectContext::ValidateRelative(key);Require(key.ends_with(".data.json")||key.ends_with(".table.json"),"expected .data.json or .table.json");
 auto p=ProjectManager::Active();auto path=p?p->ResolveAsset(key):AssetPaths::ResolveAssetPath(std::filesystem::u8path(key));ProjectContext::CheckNoLinks(path);return path;
}
void DataLoader::ValidateValue(const J& s,const J& v,const std::string& at,unsigned depth){
 Require(depth<24&&s.is_object(),at+" schema depth/shape");auto t=s.at("type").get<std::string>();
 if(t=="String")Text(v,at);
 else if(t=="Number")Require(v.is_number()&&std::isfinite(v.get<double>())&&v.get<double>()>=s.value("min",-1e100)&&v.get<double>()<=s.value("max",1e100),at+" number/range");
 else if(t=="Boolean")Require(v.is_boolean(),at+" boolean");
 else if(t=="Vec3"||t=="Color"){
  Require(v.is_array()&&v.size()==(t=="Vec3"?3:4),at+" vector size");for(auto& n:v)Require(n.is_number()&&std::isfinite(n.get<double>())&&(t!="Color"||(n.get<double>()>=0&&n.get<double>()<=1)),at+" vector channel");
 }else if(t=="Enum"){Text(v,at);Require(s.at("values").is_array()&&std::find(s.at("values").begin(),s.at("values").end(),v)!=s.at("values").end(),at+" enum value");}
 else if(t=="Resource"){
  Text(v,at);auto key=v.get<std::string>();if(key.empty()){Require(s.value("optional",false),at+" missing resource");return;}
  ProjectContext::ValidateRelative(key);auto p=ProjectManager::Active();auto path=p?p->ResolveAsset(key):AssetPaths::ResolveAssetPath(std::filesystem::u8path(key));ProjectContext::CheckNoLinks(path);
  Require(std::filesystem::is_regular_file(path),at+" resource missing: "+key);if(s.contains("suffix"))Require(key.ends_with(s.at("suffix").get<std::string>()),at+" resource type");
 }else if(t=="Array"){Require(v.is_array()&&v.size()<=2048,at+" array size");for(std::size_t i=0;i<v.size();++i)ValidateValue(s.at("items"),v[i],at+"["+std::to_string(i)+"]",depth+1);}
 else if(t=="Object"){
  Require(v.is_object()&&s.at("fields").is_object(),at+" object");auto& fields=s.at("fields");for(auto it=fields.begin();it!=fields.end();++it){Require(v.contains(it.key()),at+" missing "+it.key());ValidateValue(it.value(),v.at(it.key()),at+"."+it.key(),depth+1);}for(auto it=v.begin();it!=v.end();++it)Require(fields.contains(it.key()),at+" unknown "+it.key());
 }else throw std::runtime_error("Data: unknown schema type "+t);
}
void DataLoader::Validate(const J& d){
 Require(d.is_object()&&d.at("version")==1,"version must be 1");Text(d.at("type"),"type name");Require(!d.at("type").get_ref<const std::string&>().empty(),"empty definition type");auto kind=d.at("kind").get<std::string>();ValidateSchema(d.at("schema"));Require(d.at("schema").at("type")=="Object","root schema must be Object");
 if(kind=="DataAsset")ValidateValue(d.at("schema"),d.at("value"));
 else if(kind=="DataTable"){
  auto& rows=d.at("rows");Require(rows.is_array()&&rows.size()<=2048,"rows limit");Require(d.at("schema").at("fields").at("id").at("type")=="String","String id field required");std::set<std::string> ids;
  for(auto& row:rows){ValidateValue(d.at("schema"),row);auto id=row.at("id").get<std::string>();Require(!id.empty()&&ids.insert(id).second,"empty/duplicate row id: "+id);}
 }else throw std::runtime_error("Data: unknown kind");
}
ResourceLoadResult<DataResource> DataLoader::Load(const std::string& key,const std::filesystem::path&){
 ResourceLoadResult<DataResource> r;try{auto path=SafePath(key);Require(std::filesystem::file_size(path)<=4*1024*1024,"file exceeds 4 MiB");std::ifstream f(path,std::ios::binary);Require(bool(f),"cannot open "+key);J d=J::parse(f);Validate(d);Require((d.at("kind")=="DataTable")==key.ends_with(".table.json"),"suffix/kind mismatch");r.data.cpuBytes=d.dump().size();r.data.document=std::make_shared<const J>(std::move(d));r.success=true;}catch(const std::exception& e){r.errorMessage=e.what();}return r;
}
bool DataLoader::Save(const std::string& key,const J& d,std::string* error){try{
 auto path=SafePath(key);Validate(d);Require(d.dump().size()<=4*1024*1024,"file exceeds 4 MiB");Require((d.at("kind")=="DataTable")==key.ends_with(".table.json"),"suffix/kind mismatch");std::filesystem::create_directories(path.parent_path());return SceneSerializer::WriteAtomic(path,d,error,true);
 }catch(const std::exception& e){if(error)*error=e.what();return false;}}
J DataLoader::Default(const J& s){auto t=s.at("type").get<std::string>();if(s.contains("default"))return s.at("default");if(t=="Object"){J v=J::object();for(auto it=s.at("fields").begin();it!=s.at("fields").end();++it)v[it.key()]=Default(it.value());return v;}if(t=="Array")return J::array();if(t=="Number")return s.value("min",0.);if(t=="Boolean")return false;if(t=="Vec3")return J::array({0,0,0});if(t=="Color")return J::array({1,1,1,1});if(t=="Enum")return s.at("values").at(0);return "";}
std::string DataLoader::ExportCSV(const J& d){
 Validate(d);Require(d.at("kind")=="DataTable","CSV requires DataTable");auto quote=[](std::string v){std::string out="\"";for(char c:v){if(c=='\"')out+='\"';out+=c;}return out+"\"";};std::string out;auto& fields=d.at("schema").at("fields");
 for(auto it=fields.begin();it!=fields.end();++it){if(it!=fields.begin())out+=',';out+=quote(it.key());}out+="\r\n";
 for(auto& row:d.at("rows")){for(auto it=fields.begin();it!=fields.end();++it){if(it!=fields.begin())out+=',';auto& v=row.at(it.key());out+=quote(v.is_string()?v.get<std::string>():v.dump());}out+="\r\n";}return out;
}
J DataLoader::ImportCSV(J d,const std::string& text){
 Require(text.size()<=4*1024*1024&&ui::ValidUTF8(text),"CSV size/encoding");std::vector<std::vector<std::string>> records;std::vector<std::string> row;std::string cell;bool quoted=false,closed=false;
 for(std::size_t i=0;i<text.size();++i){char c=text[i];if(quoted){if(c=='\"'){if(i+1<text.size()&&text[i+1]=='\"'){cell+='\"';++i;}else{quoted=false;closed=true;}}else cell+=c;}else if(c=='\"'){Require(cell.empty()&&!closed,"CSV quote");quoted=true;}else if(c==','||c=='\n'||c=='\r'){row.push_back(cell);cell.clear();closed=false;if(c!=','){if(c=='\r'&&i+1<text.size()&&text[i+1]=='\n')++i;records.push_back(row);row.clear();}}else{Require(!closed,"CSV trailing text");cell+=c;}}
 Require(!quoted,"CSV unterminated quote");if(!cell.empty()||!row.empty()||closed){row.push_back(cell);records.push_back(row);}Require(!records.empty(),"CSV empty");auto& fields=d.at("schema").at("fields");std::set<std::string> names;for(auto& n:records[0])Require(fields.contains(n)&&names.insert(n).second,"CSV unknown/duplicate column");Require(names.size()==fields.size(),"CSV missing columns");d["rows"]=J::array();
 for(std::size_t i=1;i<records.size();++i){Require(records[i].size()==records[0].size(),"CSV column count");J v=J::object();for(std::size_t j=0;j<records[i].size();++j){auto n=records[0][j];auto t=fields.at(n).at("type");v[n]=(t=="String"||t=="Enum"||t=="Resource")?J(records[i][j]):J::parse(records[i][j]);}d["rows"].push_back(v);}Validate(d);return d;
}
