#pragma once
#include <string>
#include <stdexcept>
namespace scripting {
inline std::string LuaModuleKey(const std::string& name) {
  if(name.empty()||name.size()>128)throw std::runtime_error("Invalid module name: "+name);
  bool first=true;std::string key="scripts/";
  for(unsigned char c:name){
    if(c=='.'){if(first)throw std::runtime_error("Empty module segment: "+name);key+='/';first=true;continue;}
    if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'||(!first&&c>='0'&&c<='9')))
      throw std::runtime_error("Unsafe module name: "+name);
    key+=char(c);first=false;
  }
  if(first)throw std::runtime_error("Empty module segment: "+name);
  return key+".lua";
}
}
