#pragma once
#include <filesystem>
#include <string>

namespace editor {
// Resolves exactly the active asset used by LuaRuntime, never a source fallback.
std::filesystem::path ResolveEditableLuaScript(const std::string& assetKey);
bool OpenLuaScriptInVisualStudio(const std::string& assetKey,std::string* error=nullptr);
}
