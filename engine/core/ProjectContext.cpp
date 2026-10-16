#include "ProjectContext.h"
#include <atomic>
#include <fstream>
#include <set>
#include <algorithm>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif
namespace fs = std::filesystem;
namespace {
std::atomic<std::shared_ptr<const ProjectContext>> active;
std::atomic_uint64_t sequence{0};
std::string Utf8(const fs::path& p) { auto s=p.generic_u8string(); return {s.begin(),s.end()}; }
void Write(const fs::path& path,const nlohmann::json& json,bool replace) {
    auto temp=path; temp += ".tmp-"+std::to_string(++sequence);
    if(fs::exists(temp))throw std::runtime_error("Project temporary file already exists");
    bool owned=false;
    try {
#ifdef _WIN32
        const auto bytes=json.dump(2);
        HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create project temporary file: "+Utf8(path));
        owned=true;DWORD written=0;
        const bool ok=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(file);
        const bool closed=CloseHandle(file)!=0;
        if(!ok||!closed)throw std::runtime_error("Project write/flush/close failed");
#else
        { std::ofstream f(temp,std::ios::binary); if(!f)throw std::runtime_error("Cannot write project: "+Utf8(path));
          owned=true;f<<json.dump(2);f.flush();if(!f)throw std::runtime_error("Project write failed");f.close();if(!f)throw std::runtime_error("Project close failed"); }
#endif
#ifdef _WIN32
        if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH|(replace?MOVEFILE_REPLACE_EXISTING:0)))
            throw std::runtime_error("Cannot publish project descriptor (Windows error "+std::to_string(GetLastError())+")");
#else
        if(!replace&&fs::exists(path))throw std::runtime_error("Destination exists");
        fs::rename(temp,path);
#endif
    } catch(...) {std::error_code ec;if(owned)fs::remove(temp,ec);throw;}
}
}
void ProjectContext::ValidateRelative(const std::string& key) {
    const auto path=fs::u8path(key);
    if(key.empty()||key.size()>1024||path.has_root_path()||key.find('\\')!=std::string::npos)throw std::runtime_error("Expected a portable relative path");
    for(const auto& part:path) {
        auto s=Utf8(part);
        if(s.empty()||s=="."||s==".."||s.back()=='.'||s.back()==' ')throw std::runtime_error("Invalid project path component");
        for(unsigned char c:s)if(c<32||std::string(":\\*?\"<>|").find(char(c))!=std::string::npos)throw std::runtime_error("Invalid project path character");
        auto stem=s.substr(0,s.find('.'));std::transform(stem.begin(),stem.end(),stem.begin(),[](unsigned char c){return char(std::toupper(c));});
        if(stem=="CON"||stem=="PRN"||stem=="AUX"||stem=="NUL"||(stem.size()==4&&(stem.starts_with("COM")||stem.starts_with("LPT"))&&stem[3]>='0'&&stem[3]<='9'))throw std::runtime_error("Reserved project path");
    }
}
void ProjectContext::CheckNoLinks(const fs::path& path) {
    for(auto p=fs::absolute(path).lexically_normal();!p.empty();p=p.parent_path()) {
        std::error_code ec;auto status=fs::symlink_status(p,ec);
        if(fs::is_symlink(status))throw std::runtime_error("Project symlinks are not supported: "+Utf8(p));
#ifdef _WIN32
        const auto flags=GetFileAttributesW(p.c_str());
        if(flags!=INVALID_FILE_ATTRIBUTES&&(flags&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Project reparse points are not supported: "+Utf8(p));
#endif
        if(p==p.parent_path())break;
    }
}
fs::path ProjectContext::Resolve(const std::string& key) const {
    ValidateRelative(key);const auto target=(root/fs::u8path(key)).lexically_normal();CheckNoLinks(target);
    const auto relative=target.lexically_relative(root);
    if(relative.empty()||relative.is_absolute()||*relative.begin()=="..")throw std::runtime_error("Path escapes project root");
    return target;
}
fs::path ProjectContext::AssetRoot()const{return Resolve(assetDirectory);}
fs::path ProjectContext::SceneRoot()const{return Resolve(sceneDirectory);}
fs::path ProjectContext::ResolveAsset(const std::string& key)const{ValidateRelative(key);return Resolve(assetDirectory+"/"+key);}
fs::path ProjectContext::ResolveScene(const std::string& key)const{
    ValidateRelative(key);
    if(!key.starts_with(sceneDirectory+"/")||fs::u8path(key).extension()!=".json")throw std::runtime_error("Scene must be a JSON file inside "+sceneDirectory);
    return Resolve(key);
}
std::shared_ptr<const ProjectContext> ProjectManager::Prepare(const fs::path& path) {
    if(path.extension()!=".whispproject")throw std::runtime_error("Select a .whispproject descriptor");
    ProjectContext::CheckNoLinks(path);
    if(!fs::is_regular_file(path)||fs::file_size(path)>1024*1024)throw std::runtime_error("Missing or oversized project descriptor");
    auto p=std::make_shared<ProjectContext>();p->descriptorPath=fs::absolute(path).lexically_normal();p->root=p->descriptorPath.parent_path();
    std::ifstream f(path);f>>p->descriptor;const auto& j=p->descriptor;
    if(!j.is_object()||!j.at("formatVersion").is_number_integer()||j.at("formatVersion")!=ProjectContext::CurrentVersion)throw std::runtime_error("Unsupported project format version");
    p->name=j.at("name").get<std::string>();if(p->name.empty()||p->name.size()>240)throw std::runtime_error("Invalid project name");
    p->assetDirectory=j.at("directories").at("assets").get<std::string>();p->sceneDirectory=j.at("directories").at("scenes").get<std::string>();
    p->startupScene=j.at("startupScene").get<std::string>();
    auto assets=p->AssetRoot(), scenes=p->SceneRoot();
    auto a=assets.lexically_relative(scenes),s=scenes.lexically_relative(assets);
    if(a.empty()||a=="."||*a.begin()!=".."||s.empty()||*s.begin()!="..")throw std::runtime_error("Asset and scene directories must be separate");
    if(!fs::is_directory(assets)||!fs::is_directory(scenes))throw std::runtime_error("Project assets/scenes directory is missing");
    // Windows aliases differing only by case must not bypass root separation.
    auto contains=[](const fs::path& parent,fs::path child){
        for(;;){if(fs::equivalent(parent,child))return true;if(child==child.parent_path())return false;child=child.parent_path();}
    };
    if(contains(assets,scenes)||contains(scenes,assets))throw std::runtime_error("Asset and scene directories overlap");
    if(!fs::is_regular_file(p->ResolveScene(p->startupScene)))throw std::runtime_error("Startup scene is missing: "+p->startupScene);
    return p;
}
std::shared_ptr<const ProjectContext> ProjectManager::Create(const fs::path& parent,const std::string& name) {
    ProjectContext::ValidateRelative(name);
    if(fs::u8path(name).has_parent_path()||name.size()>120)throw std::runtime_error("Project name must be one filename component (up to 120 bytes)");
    ProjectContext::CheckNoLinks(parent);
    if(!fs::is_directory(parent))throw std::runtime_error("Choose an existing project location");
    auto root=fs::absolute(parent)/fs::u8path(name);
    if(!fs::create_directory(root))throw std::runtime_error("Project folder already exists; choose a new name/location");
    // Only this newly created directory belongs to the transaction.
    try {
        for(auto folder:{"Assets/models","Assets/textures","Assets/materials","Assets/scripts","Assets/audio","Assets/prefabs","Assets/environments","Scenes","Settings"})fs::create_directories(root/folder);
        Write(root/"Scenes/Main.json",{{"version",4},{"entities",nlohmann::json::array()}},false);
        auto path=root/fs::u8path(name+".whispproject");
        Write(path,{{"formatVersion",1},{"name",name},{"startupScene","Scenes/Main.json"},{"directories",{{"assets","Assets"},{"scenes","Scenes"}}}},false);
        return Prepare(path);
    }catch(...){std::error_code ec;fs::remove_all(root,ec);throw;}
}
std::shared_ptr<const ProjectContext> ProjectManager::WithStartupScene(const ProjectContext& context,const std::string& key) {
    if(!fs::is_regular_file(context.ResolveScene(key)))throw std::runtime_error("Save the startup scene first");
    ProjectContext::CheckNoLinks(context.descriptorPath);
    std::ifstream current(context.descriptorPath);nlohmann::json disk;current>>disk;
    if(disk!=context.descriptor)throw std::runtime_error("Project descriptor changed outside the editor. Reopen it before changing settings.");
    current.close(); // Windows cannot atomically replace a descriptor held open by this stream.
    auto next=std::make_shared<ProjectContext>(context);next->startupScene=key;next->descriptor["startupScene"]=key;
    ProjectContext::CheckNoLinks(context.descriptorPath);Write(context.descriptorPath,next->descriptor,true);return next;
}
std::shared_ptr<const ProjectContext> ProjectManager::Active(){return active.load();}
void ProjectManager::Activate(std::shared_ptr<const ProjectContext> p){active.store(std::move(p));}
