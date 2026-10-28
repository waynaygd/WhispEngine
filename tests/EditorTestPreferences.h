#pragma once
#include <filesystem>
#include <fstream>
#include <vector>
#include <iterator>
#include <set>
#include "editor/EditorPreferences.h"
#include "core/AssetPaths.h"
// Integration tests call Application::Run, which normally persists preferences.
// Restore the caller's exact bytes after all Application/ImGui destructors finish.
struct ScopedEditorPreferenceRestore {
    struct Saved {std::filesystem::path path;bool existed;std::vector<char> bytes;};
    std::vector<Saved> saved;
    ScopedEditorPreferenceRestore(){
        // Preferences moved next to the executable. Also protect legacy cwd
        // files and ImGui's installation-relative layout, using absolute paths.
        std::set<std::filesystem::path> paths={std::filesystem::absolute("editor-preferences.json"),std::filesystem::absolute("imgui.ini"),std::filesystem::absolute(editor::PreferencesPath()),AssetPaths::GetExecutableDirectory()/"imgui.ini"};
        for(const auto& path:paths){
            Saved file{path,std::filesystem::exists(path),{}};
            if(file.existed){std::ifstream in(path,std::ios::binary);file.bytes.assign(std::istreambuf_iterator<char>(in),{});}saved.push_back(std::move(file));
        }
    }
    ~ScopedEditorPreferenceRestore(){for(const auto& file:saved){if(file.existed){std::ofstream out(file.path,std::ios::binary|std::ios::trunc);out.write(file.bytes.data(),file.bytes.size());}else{std::error_code error;std::filesystem::remove(file.path,error);}}}
};
