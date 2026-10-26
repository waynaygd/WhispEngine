#pragma once
#include "../scripting/LuaModulePath.h"
#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>
namespace editor::detail {
struct LuaToken {std::string text;bool literal=false;unsigned line=1;};
// Tokenize only; never evaluate an asset script during a dependency audit.
inline std::vector<LuaToken> LuaTokens(const std::string& source){
    std::vector<LuaToken> tokens;std::size_t at=0;unsigned line=1;
    auto bracket=[&](std::size_t pos)->std::size_t{if(pos>=source.size()||source[pos]!='[')return 0;std::size_t end=pos+1;while(end<source.size()&&source[end]=='=')++end;return end<source.size()&&source[end]=='['?end-pos+1:0;};
    auto longText=[&](std::size_t size){auto start=at+size;auto close="]"+std::string(size-2,'=')+"]";auto end=source.find(close,start);if(end==std::string::npos)throw std::runtime_error("Unterminated Lua long string/comment");auto value=source.substr(start,end-start);for(std::size_t i=at;i<end+size;++i)line+=source[i]=='\n';at=end+size;if(value.starts_with("\r\n"))value.erase(0,2);else if(!value.empty()&&(value[0]=='\n'||value[0]=='\r'))value.erase(0,1);return value;};
    auto hex=[](char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};
    while(at<source.size()){
        unsigned tokenLine=line;char c=source[at];if(std::isspace(static_cast<unsigned char>(c))){line+=c=='\n';++at;continue;}
        if(c=='-'&&at+1<source.size()&&source[at+1]=='-'){at+=2;if(auto size=bracket(at))longText(size);else while(at<source.size()&&source[at]!='\n')++at;continue;}
        if(auto size=bracket(at)){tokens.push_back({longText(size),true,tokenLine});continue;}
        if(c=='\''||c=='"'){
            ++at;std::string value;bool closed=false;
            while(at<source.size()){
                char ch=source[at++];if(ch==c){closed=true;break;}if(ch=='\n'||ch=='\r')throw std::runtime_error("Unterminated Lua string");if(ch!='\\'){value+=ch;continue;}
                if(at==source.size())break;char escape=source[at++];
                const std::string codes="abfnrtv",values="\a\b\f\n\r\t\v";auto code=codes.find(escape);
                if(code!=std::string::npos)value+=values[code];
                else if(escape=='\\'||escape=='\''||escape=='"')value+=escape;
                else if(escape=='\n'||escape=='\r'){if(escape=='\r'&&at<source.size()&&source[at]=='\n')++at;++line;value+='\n';}
                else if(escape=='z'){while(at<source.size()&&std::isspace(static_cast<unsigned char>(source[at]))){line+=source[at]=='\n';++at;}}
                else if(escape>='0'&&escape<='9'){unsigned n=escape-'0';for(int i=0;i<2&&at<source.size()&&std::isdigit(static_cast<unsigned char>(source[at]));++i)n=n*10+source[at++]-'0';if(n>255)throw std::runtime_error("Invalid Lua decimal escape");value+=char(n);}
                else if(escape=='x'){if(at+2>source.size()||hex(source[at])<0||hex(source[at+1])<0)throw std::runtime_error("Invalid Lua hex escape");value+=char(hex(source[at])*16+hex(source[at+1]));at+=2;}
                else if(escape=='u'){
                    if(at>=source.size()||source[at++]!='{')throw std::runtime_error("Invalid Lua Unicode escape");unsigned n=0,digits=0;while(at<source.size()&&source[at]!='}'){int h=hex(source[at++]);if(h<0||n>0x10ffff/16)throw std::runtime_error("Invalid Lua Unicode escape");n=n*16+h;++digits;}
                    if(!digits||at==source.size()||n>0x10ffff||(n>=0xd800&&n<=0xdfff))throw std::runtime_error("Invalid Lua Unicode escape");++at;
                    if(n<0x80)value+=char(n);else if(n<0x800){value+=char(0xc0|(n>>6));value+=char(0x80|(n&63));}else if(n<0x10000){value+=char(0xe0|(n>>12));value+=char(0x80|((n>>6)&63));value+=char(0x80|(n&63));}else{value+=char(0xf0|(n>>18));value+=char(0x80|((n>>12)&63));value+=char(0x80|((n>>6)&63));value+=char(0x80|(n&63));}
                }else throw std::runtime_error("Unsupported Lua escape in dependency audit");
            }
            if(!closed)throw std::runtime_error("Unterminated Lua string");tokens.push_back({std::move(value),true,tokenLine});continue;
        }
        if(std::isalnum(static_cast<unsigned char>(c))||c=='_'){auto start=at++;while(at<source.size()&&(std::isalnum(static_cast<unsigned char>(source[at]))||source[at]=='_'))++at;tokens.push_back({source.substr(start,at-start),false,tokenLine});}
        else if(c=='.'&&at+1<source.size()&&source[at+1]=='.'){tokens.push_back({"..",false,tokenLine});at+=2;}
        else {tokens.push_back({std::string(1,c),false,tokenLine});++at;}
    }return tokens;
}
struct LuaDependencies {std::vector<std::string> values,dynamic,moduleDynamic;};
inline LuaDependencies AnalyzeLuaDependencies(const std::string& source){
    auto tokens=LuaTokens(source);LuaDependencies result;
    // Resolve only proven literal imports; never execute source during file operations.
    for(std::size_t i=2;i<tokens.size();++i){
        const bool bracket=i+1<tokens.size()&&tokens[i-2].text=="Script"&&tokens[i-1].text=="["&&tokens[i].literal&&tokens[i+1].text=="]";
        if(tokens[i].text!="require"||tokens[i-2].text!="Script"||(!bracket&&tokens[i-1].text!="."))continue;
        auto open=i+(bracket?2:1);
        if(open+2<tokens.size()&&tokens[open].text=="("&&tokens[open+1].literal&&tokens[open+2].text==")"){
            try{result.values.push_back(scripting::LuaModuleKey(tokens[open+1].text));}catch(...){}
        }else result.moduleDynamic.push_back("line "+std::to_string(tokens[i].line)+": dynamic Script.require dependency cannot be proven; imports are not automatically rewritten by Rename.");
    }
    // Includes direct paths, schema defaults, and other literal assignments.
    for(std::size_t i=0;i<tokens.size();++i)if(tokens[i].literal){std::string value=tokens[i].text;result.values.push_back(value);std::size_t end=i;while(end+2<tokens.size()&&!tokens[end+1].literal&&tokens[end+1].text==".."&&tokens[end+2].literal){end+=2;value+=tokens[end].text;result.values.push_back(tokens[end].text);}if(end!=i)result.values.push_back(value);i=end;}
    for(std::size_t i=0;i<tokens.size();++i){
        const bool bracket=i>=2&&i+1<tokens.size()&&tokens[i-2].text=="World"&&tokens[i-1].text=="["&&tokens[i].literal&&tokens[i].text=="spawn_prefab"&&tokens[i+1].text=="]";
        if(tokens[i].text!="spawn_prefab"||(tokens[i].literal&&!bracket))continue;
        bool constant=false;std::string expression;
        const auto open=i+(bracket?2:1);
        if(i>=2&&tokens[i-2].text=="World"&&(bracket||tokens[i-1].text==".")&&open<tokens.size()&&tokens[open].text=="("){
            std::size_t end=open+1;int depth=0;for(;end<tokens.size();++end){const auto& t=tokens[end];if(!t.literal){if((t.text==","||t.text==")")&&depth==0)break;if(t.text=="("||t.text=="["||t.text=="{")++depth;if(t.text==")"||t.text=="]"||t.text=="}")--depth;}}
            auto begin=open+1;while(begin<end&&!tokens[begin].literal&&!tokens[end-1].literal&&tokens[begin].text=="("&&tokens[end-1].text==")"){++begin;--end;}
            constant=begin<end;bool stringNext=true;for(auto at=begin;at<end;++at){const auto& t=tokens[at];if(stringNext?!t.literal:t.literal||t.text!="..")constant=false;stringNext=!stringNext;if(expression.size()<160)expression+=t.literal?"\""+t.text+"\"":t.text;}if(stringNext)constant=false;
        }
        if(!constant)result.dynamic.push_back("line "+std::to_string(tokens[i].line)+": dynamic spawn_prefab("+(expression.empty()?"unknown/aliased call":expression)+"). Literal defaults and saved overrides are audited, but runtime-computed paths cannot be proven; unknown paths will NOT be updated by Rename.");
    }return result;
}
}
