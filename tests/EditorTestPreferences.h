#pragma once
#include <filesystem>
#include <fstream>
#include <vector>
#include <iterator>
// Integration tests call Application::Run, which normally persists preferences.
// Restore the caller's exact bytes after all Application/ImGui destructors finish.
struct ScopedEditorPreferenceRestore {
    struct Saved {std::filesystem::path path;bool existed;std::vector<char> bytes;};
    std::vector<Saved> saved;
    ScopedEditorPreferenceRestore(){
        for(const char* name:{"editor-preferences.json","imgui.ini"}){
            Saved file{name,std::filesystem::exists(name),{}};
            if(file.existed){std::ifstream in(name,std::ios::binary);file.bytes.assign(std::istreambuf_iterator<char>(in),{});}saved.push_back(std::move(file));
        }
    }
    ~ScopedEditorPreferenceRestore(){for(const auto& file:saved){if(file.existed){std::ofstream out(file.path,std::ios::binary|std::ios::trunc);out.write(file.bytes.data(),file.bytes.size());}else{std::error_code error;std::filesystem::remove(file.path,error);}}}
};
