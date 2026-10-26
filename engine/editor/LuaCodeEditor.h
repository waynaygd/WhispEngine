#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <cstdint>
namespace editor {
enum class LuaTokenKind { Plain,Keyword,String,Number,Comment,Function,Literal };
struct LuaToken {std::size_t begin=0,end=0;LuaTokenKind kind=LuaTokenKind::Plain;};
struct LuaSyntax {bool valid=true;int line=0;std::string message;};
LuaSyntax ValidateLuaSource(const std::string& text,const std::string& key);
std::vector<LuaToken> LexLua(const std::string& text);
std::string NewLuaBehaviour();
// One existing Script Document owns one buffer. Nothing executes Lua in this class.
class LuaCodeEditor {
public:
 static constexpr std::size_t MaxBytes=4*1024*1024,MaxHistoryBytes=16*1024*1024;
 std::string key,text,disk,error,search,replacement;
 std::filesystem::path path;
 LuaSyntax syntax;
 int cursor=0,selectionStart=0,selectionEnd=0,highlightLine=0,jumpLine=0;
 bool showFind=false,showReplace=false,focusFind=false,focusCode=false,external=false,externalAcknowledged=false;
 std::uint64_t revision=0,lexPasses=0;
 void Open(const std::string& key);
 bool Save();
 bool Reload();
 bool CheckExternal();
 bool Dirty()const{return text!=disk;}
 void Edit(const std::string& value,int at=-1);
 bool Undo(bool redo=false);
 bool CanUndo(bool redo=false)const{return !(redo?redoEdits:undoEdits).empty();}
 void Indent(bool unindent);
 bool FindNext();
 bool ReplaceMatch();
 void GoTo(int line);
 void Validate(){syntax=ValidateLuaSource(text,key);}
 const std::vector<LuaToken>& Tokens();
 const std::vector<std::size_t>& Lines();
 // Draw uses ImGui's input/selection/IME and this buffer's independent bounded Undo history.
 bool Draw(bool readOnly);
private:
 struct Change {std::size_t at;std::string removed,inserted;int before=0,after=0;};
 std::vector<Change> undoEdits,redoEdits;
 std::string newline="\n";
 std::vector<LuaToken> tokens;
 std::vector<std::size_t> lines;
 std::uint64_t cachedRevision=~std::uint64_t(0);
 std::size_t historyBytes=0;
 bool refreshInput=false,selectOnActivate=false;
 std::string input;
 float horizontalOffset=0,maxLineWidth=0,metricFontSize=0;
 std::uint64_t metricRevision=~std::uint64_t(0);
 void Cache();
};
}
