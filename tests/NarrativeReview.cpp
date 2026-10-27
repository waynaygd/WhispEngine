#define NOMINMAX
#include "core/Application.h"
#include "core/AssetPaths.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <iostream>
// A visible, ordinary-input review window. Does not inject gameplay actions.
// Keep the user's executable-local preferences/workspace exact on exit.
struct ReviewPreferences{
 struct File{std::filesystem::path path;bool existed;std::vector<char> bytes;};std::vector<File> files;
 ReviewPreferences(){for(auto name:{"editor-preferences.json","imgui.ini"}){auto path=AssetPaths::GetExecutableDirectory()/name;File f{path,std::filesystem::exists(path),{}};if(f.existed){std::ifstream in(path,std::ios::binary);f.bytes.assign(std::istreambuf_iterator<char>(in),{});}files.push_back(std::move(f));}}
 ~ReviewPreferences(){for(auto& f:files){if(f.existed){std::ofstream out(f.path,std::ios::binary|std::ios::trunc);out.write(f.bytes.data(),f.bytes.size());}else{std::error_code e;std::filesystem::remove(f.path,e);}}}
};
int main(){std::filesystem::current_path(AssetPaths::GetExecutableDirectory());ReviewPreferences preferences;_putenv_s("WHISP_DX12_DEBUG","1");try{Application app;if(!app.Initialize(Application::StartupMode::ProjectBrowser))return 1;std::string error;if(!app.OpenProject("D:/WhispEngineProjects/DiscoElysium2/DiscoElysium2.whispproject",Application::ProjectDecision::Discard,&error))throw std::runtime_error(error);app.GetEditorLayer().SetGameView(true);app.SetEditorPlayMode(true);if(!app.IsEditorPlayMode())throw std::runtime_error(app.GetPlayError());app.Run();if(auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter())){std::ofstream report("overhaul-manual-validation.txt");report<<gpu->GetValidationErrorCount();}app.Shutdown();return 0;}catch(const std::exception& e){std::cerr<<e.what();return 1;}}
