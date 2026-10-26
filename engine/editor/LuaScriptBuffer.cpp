#include "LuaCodeEditor.h"
#include "ScriptEditor.h"
#include "../ui/UIDocument.h"
#include "../ecs/components/SceneIdentityComponent.h"
#include <lua.hpp>
#include <fstream>
#include <algorithm>
#include <set>
#include <cctype>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif
namespace editor {
namespace {
std::string Read(const std::filesystem::path& path){
 if(std::filesystem::file_size(path)>LuaCodeEditor::MaxBytes)throw std::runtime_error("Lua editor supports files up to 4 MiB");
 std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("Cannot read Lua file");std::string value{std::istreambuf_iterator<char>(f),{}};
 if(!f.eof()&&f.fail())throw std::runtime_error("Lua read failed");if(!ui::ValidUTF8(value))throw std::runtime_error("Lua source must be UTF-8 without embedded NUL");return value;
}
int LongBracket(const std::string& s,std::size_t at){if(at>=s.size()||s[at]!='[')return -1;auto i=at+1;while(i<s.size()&&s[i]=='=')++i;return i<s.size()&&s[i]=='['?int(i-at-1):-1;}
}
std::string NewLuaBehaviour(){return "return {\n    fields = {},\n\n    on_start = function(self)\n    end,\n\n    on_update = function(self, dt)\n    end,\n\n    on_stop = function(self)\n    end\n}\n";}
std::vector<LuaToken> LexLua(const std::string& s){
 static const std::set<std::string> keywords{"and","break","do","else","elseif","end","for","function","goto","if","in","local","not","or","repeat","return","then","until","while"};
 std::vector<LuaToken> out;bool declaration=false;
 auto word=[](unsigned char c){return std::isalnum(c)||c=='_';};
 for(std::size_t i=0;i<s.size();){auto start=i;auto kind=LuaTokenKind::Plain;char c=s[i];
  bool comment=c=='-'&&i+1<s.size()&&s[i+1]=='-';if(comment)i+=2;
  int bracket=LongBracket(s,i);
  if(bracket>=0){auto close="]"+std::string(bracket,'=')+"]";i+=bracket+2;auto end=s.find(close,i);i=end==std::string::npos?s.size():end+close.size();kind=comment?LuaTokenKind::Comment:LuaTokenKind::String;}
  else if(comment){while(i<s.size()&&s[i]!='\n')++i;kind=LuaTokenKind::Comment;}
  else if(c=='\''||c=='"'){++i;while(i<s.size()){if(s[i]=='\\'){i=std::min(s.size(),i+2);continue;}if(s[i++]==c)break;}kind=LuaTokenKind::String;}
  else if(std::isalpha(static_cast<unsigned char>(c))||c=='_'){while(i<s.size()&&word(static_cast<unsigned char>(s[i])))++i;auto name=s.substr(start,i-start);auto next=s.find_first_not_of(" \t\r\n",i);if(name=="true"||name=="false"||name=="nil")kind=LuaTokenKind::Literal;else if(keywords.contains(name))kind=LuaTokenKind::Keyword;else if(declaration||(next!=std::string::npos&&s[next]=='('))kind=LuaTokenKind::Function;if(name=="function")declaration=true;}
  else if(c=='.'&&i+1<s.size()&&s[i+1]=='.'){i+=2;if(i<s.size()&&s[i]=='.')++i;}
  else if(std::isdigit(static_cast<unsigned char>(c))||(c=='.'&&i+1<s.size()&&std::isdigit(static_cast<unsigned char>(s[i+1])))){
   bool hex=c=='0'&&i+1<s.size()&&(s[i+1]=='x'||s[i+1]=='X');i+=hex?2:0;bool exponent=false;
   while(i<s.size()){auto n=s[i];if(std::isdigit(static_cast<unsigned char>(n))||(hex&&std::isxdigit(static_cast<unsigned char>(n)))){++i;continue;}if(n=='.'&&!(i+1<s.size()&&s[i+1]=='.')){++i;continue;}if(!exponent&&((!hex&&(n=='e'||n=='E'))||(hex&&(n=='p'||n=='P')))){exponent=true;++i;if(i<s.size()&&(s[i]=='+'||s[i]=='-'))++i;continue;}break;}kind=LuaTokenKind::Number;
  }else {++i;if(!std::isspace(static_cast<unsigned char>(c))&&c!='.'&&c!=':')declaration=false;}
  if(kind==LuaTokenKind::Plain&&!out.empty()&&out.back().kind==kind)out.back().end=i;else out.push_back({start,i,kind});
 }return out;
}
LuaSyntax ValidateLuaSource(const std::string& text,const std::string& key){
 LuaSyntax result;auto* lua=luaL_newstate();if(!lua)return {false,0,"Cannot allocate Lua compiler state"};
 auto offset=text.starts_with("\xef\xbb\xbf")?3u:0u;
 if(luaL_loadbufferx(lua,text.data()+offset,text.size()-offset,("@"+key).c_str(),"t")!=LUA_OK){result.valid=false;auto* msg=lua_tostring(lua,-1);result.message=msg?msg:"Lua compiler error";auto prefix=key+":";if(result.message.starts_with(prefix)){auto end=result.message.find(':',prefix.size());auto number=result.message.substr(prefix.size(),end-prefix.size());if(!number.empty()&&number.size()<9&&std::all_of(number.begin(),number.end(),[](char c){return c>='0'&&c<='9';}))result.line=std::stoi(number);}}
 lua_close(lua);return result;
}
void LuaCodeEditor::Open(const std::string& value){key=value;path=ResolveEditableLuaScript(key);text=disk=Read(path);newline=text.find("\r\n")!=std::string::npos?"\r\n":"\n";undoEdits.clear();redoEdits.clear();historyBytes=0;cursor=selectionStart=selectionEnd=0;highlightLine=jumpLine=0;external=externalAcknowledged=false;error.clear();++revision;refreshInput=true;Validate();}
bool LuaCodeEditor::CheckExternal(){try{auto resolved=ResolveEditableLuaScript(key);if(!std::filesystem::equivalent(resolved,path))throw std::runtime_error("Script belongs to another project");external=Read(resolved)!=disk;if(external)error="Lua file changed externally. Reload or keep editing; Save preserves the external file.";return external;}catch(const std::exception& e){error=e.what();external=true;return true;}}
bool LuaCodeEditor::Save(){
 std::filesystem::path temporary;
 try{if(text.size()>MaxBytes||!ui::ValidUTF8(text))throw std::runtime_error("Source must be UTF-8, without NUL, up to 4 MiB");if(CheckExternal())throw std::runtime_error(error.empty()?"Lua file changed externally. Reload or keep editing; Save will not overwrite it.":error);Validate();
  temporary=path;temporary+=".tmp-"+ecs::NewSceneIdentity();{std::ofstream f(temporary,std::ios::binary);if(!f)throw std::runtime_error("Cannot create Lua temporary file");f.write(text.data(),text.size());f.flush();if(!f)throw std::runtime_error("Cannot write Lua temporary file");f.close();if(!f)throw std::runtime_error("Cannot close Lua temporary file");}
  if(CheckExternal())throw std::runtime_error("Lua file changed during Save; original preserved");
#ifdef _WIN32
  if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Atomic Lua replacement failed (Windows error "+std::to_string(GetLastError())+")");
#else
  std::filesystem::rename(temporary,path);
#endif
  disk=text;external=false;error.clear();return true;
 }catch(const std::exception& e){error=e.what();std::error_code ignored;if(!temporary.empty())std::filesystem::remove(temporary,ignored);return false;}
}
bool LuaCodeEditor::Reload(){try{auto next=ResolveEditableLuaScript(key);if(!std::filesystem::equivalent(next,path))throw std::runtime_error("Script belongs to another project");auto value=Read(next);text=disk=value;cursor=selectionStart=selectionEnd=0;highlightLine=jumpLine=0;undoEdits.clear();redoEdits.clear();historyBytes=0;external=externalAcknowledged=false;error.clear();newline=text.find("\r\n")!=std::string::npos?"\r\n":"\n";++revision;refreshInput=true;Validate();return true;}catch(const std::exception& e){error=e.what();return false;}}
void LuaCodeEditor::Edit(const std::string& value,int at){
 if(value==text)return;if(value.size()>MaxBytes||!ui::ValidUTF8(value)){error="Edit exceeds 4 MiB or contains invalid UTF-8";refreshInput=true;return;}
 std::size_t first=0;while(first<text.size()&&first<value.size()&&text[first]==value[first])++first;std::size_t tail=0;while(tail<text.size()-first&&tail<value.size()-first&&text[text.size()-1-tail]==value[value.size()-1-tail])++tail;
 for(auto& old:redoEdits)historyBytes-=old.removed.size()+old.inserted.size();
 Change change{first,text.substr(first,text.size()-first-tail),value.substr(first,value.size()-first-tail),cursor,at<0?int(first+value.size()-first-tail):at};historyBytes+=change.removed.size()+change.inserted.size();undoEdits.push_back(std::move(change));redoEdits.clear();
 while(undoEdits.size()>256||historyBytes>MaxHistoryBytes){historyBytes-=undoEdits.front().removed.size()+undoEdits.front().inserted.size();undoEdits.erase(undoEdits.begin());}
 text=value;cursor=selectionStart=selectionEnd=std::clamp(at<0?int(first+value.size()-first-tail):at,0,int(text.size()));++revision;refreshInput=true;syntax={};
}
bool LuaCodeEditor::Undo(bool redo){auto& from=redo?redoEdits:undoEdits;auto& to=redo?undoEdits:redoEdits;if(from.empty())return false;auto c=std::move(from.back());from.pop_back();text.replace(c.at,(redo?c.removed:c.inserted).size(),redo?c.inserted:c.removed);cursor=selectionStart=selectionEnd=std::clamp(redo?c.after:c.before,0,int(text.size()));to.push_back(std::move(c));++revision;refreshInput=true;syntax={};return true;}
void LuaCodeEditor::Cache(){if(cachedRevision==revision)return;tokens=LexLua(text);++lexPasses;lines={0};for(std::size_t i=0;i<text.size();++i)if(text[i]=='\n')lines.push_back(i+1);cachedRevision=revision;}
const std::vector<LuaToken>& LuaCodeEditor::Tokens(){Cache();return tokens;}
const std::vector<std::size_t>& LuaCodeEditor::Lines(){Cache();return lines;}
void LuaCodeEditor::GoTo(int line){auto& rows=Lines();line=std::clamp(line,1,int(rows.size()));cursor=selectionStart=selectionEnd=int(rows[line-1]);highlightLine=line;jumpLine=line;focusCode=true;refreshInput=true;}
void LuaCodeEditor::Indent(bool out){
 auto a=std::min(selectionStart,selectionEnd),b=std::max(selectionStart,selectionEnd);auto begin=text.rfind('\n',a?std::size_t(a-1):std::string::npos);begin=a==0||begin==std::string::npos?0:begin+1;std::vector<std::size_t> starts{begin};for(auto i=begin;i<text.size()&&i<std::size_t(b);++i)if(text[i]=='\n'&&i+1<std::size_t(b))starts.push_back(i+1);
 const bool selected=a!=b;auto value=text;int delta=0;for(auto it=starts.rbegin();it!=starts.rend();++it){if(out){std::size_t count=0;if(*it<value.size()&&value[*it]=='\t')count=1;else while(count<4&&*it+count<value.size()&&value[*it+count]==' ')++count;value.erase(*it,count);delta-=int(count);}else {value.insert(*it,"    ");delta+=4;}}
 if(value==text)return;Edit(value,b+delta);if(selected){selectionStart=int(begin);selectionEnd=std::clamp(b+delta,0,int(text.size()));}refreshInput=true;
}
bool LuaCodeEditor::FindNext(){if(search.empty())return false;auto found=text.find(search,std::size_t(std::max(selectionStart,selectionEnd)));if(found==std::string::npos)found=text.find(search);if(found==std::string::npos){error="Text not found";return false;}error.clear();selectionStart=int(found);selectionEnd=cursor=int(found+search.size());jumpLine=int(std::upper_bound(Lines().begin(),Lines().end(),found)-Lines().begin());focusCode=true;refreshInput=true;return true;}
bool LuaCodeEditor::ReplaceMatch(){auto a=std::clamp(std::min(selectionStart,selectionEnd),0,int(text.size())),b=std::clamp(std::max(selectionStart,selectionEnd),0,int(text.size()));if(search.empty()||text.substr(a,b-a)!=search)return FindNext();auto value=text;value.replace(a,b-a,replacement);Edit(value,a+int(replacement.size()));return true;}
}
