#include "core/ProjectContext.h"
#include <fstream>
#include <iostream>
#include <chrono>
#include <source_location>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <winioctl.h>
#endif
namespace fs=std::filesystem;
void Check(bool b,const char* msg){if(!b)throw std::runtime_error(msg);}
template<class F>void Reject(F fn,std::source_location loc=std::source_location::current()){bool failed=false;try{fn();}catch(...){failed=true;}if(!failed)throw std::runtime_error("Expected rejection at line "+std::to_string(loc.line()));}
#ifdef _WIN32
// Junctions do not require the symbolic-link privilege. Exercise the actual
// reparse-point guard even on machines where the symlink fixture is unavailable.
void Junction(const fs::path& link,const fs::path& target){
    Check(fs::create_directory(link),"junction fixture directory");
    const auto substitute=L"\\??\\"+fs::absolute(target).wstring();
    const auto print=fs::absolute(target).wstring();
    struct MountPoint {DWORD tag;WORD length,reserved,subOffset,subLength,printOffset,printLength;WCHAR paths[4096];} data{};
    const auto pathLength=substitute.size()+1+print.size()+1;
    Check(pathLength<std::size(data.paths),"junction fixture path length");
    data.tag=IO_REPARSE_TAG_MOUNT_POINT;data.subLength=WORD(substitute.size()*sizeof(WCHAR));
    data.printOffset=data.subLength+sizeof(WCHAR);data.printLength=WORD(print.size()*sizeof(WCHAR));
    std::copy(substitute.begin(),substitute.end(),data.paths);
    std::copy(print.begin(),print.end(),data.paths+substitute.size()+1);
    data.length=WORD(8+pathLength*sizeof(WCHAR));
    HANDLE handle=CreateFileW(link.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    Check(handle!=INVALID_HANDLE_VALUE,"open junction fixture");DWORD returned=0;
    const bool ok=DeviceIoControl(handle,FSCTL_SET_REPARSE_POINT,&data,data.length+8,nullptr,0,&returned,nullptr)!=0;
    CloseHandle(handle);Check(ok,"create junction fixture reparse data");
}
#endif
int main(){
    auto root=fs::current_path()/("WhispProjectCore-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{
        fs::create_directory(root);const auto cwd=fs::current_path();
        auto p=ProjectManager::Create(root,"HospitalDemo");
        Check(fs::is_directory(p->AssetRoot()/"scripts"),"scripts directory");
        Check(fs::exists(p->ResolveScene(p->startupScene)),"startup scene");
        Check(fs::current_path()==cwd,"working directory changed");
        Reject([&]{ProjectManager::Create(root,"HospitalDemo");});
        for(auto key:{"../outside","C:/outside","C:outside","//server/share/file","bad\\file","a/../b","CON","textures/x:stream","/absolute"})Reject([&]{p->ResolveAsset(key);});
        fs::copy_file(p->ResolveScene(p->startupScene),p->ResolveScene("Scenes/Second.json"));
        auto updated=ProjectManager::WithStartupScene(*p,"Scenes/Second.json");
        Check(ProjectManager::Prepare(p->descriptorPath)->startupScene=="Scenes/Second.json","startup descriptor atomically published");
        Reject([&]{ProjectManager::WithStartupScene(*p,p->startupScene);});
        Reject([&]{ProjectManager::WithStartupScene(*updated,"Scenes/Missing.json");});
        Check(ProjectManager::Prepare(p->descriptorPath)->descriptor==updated->descriptor,"rejected startup update preserves descriptor");
        for(const auto& entry:fs::directory_iterator(p->root))Check(entry.path().filename().string().find(".tmp-")==std::string::npos,"descriptor temporary file retired");
        p=updated;
        auto b=ProjectManager::Create(root,"WhispTest");Check(b->ResolveAsset("textures/wall.png")!=p->ResolveAsset("textures/wall.png"),"project isolation");
        Reject([&]{p->ResolveScene("Assets/Main.json");});
        auto moved=root/fs::u8path("Перенос с пробелами");fs::rename(p->root,moved);p=ProjectManager::Prepare(moved/"HospitalDemo.whispproject");
        Check(p->root==moved,"moved project");
        {std::ofstream f(root/"bad.whispproject");f<<"{broken";}Reject([&]{ProjectManager::Prepare(root/"bad.whispproject");});
        auto j=p->descriptor;j["formatVersion"]=99;{std::ofstream f(root/"bad.whispproject");f<<j;}Reject([&]{ProjectManager::Prepare(root/"bad.whispproject");});
        j=p->descriptor;j["formatVersion"]=4294967297ull;{std::ofstream f(p->descriptorPath);f<<j;}Reject([&]{ProjectManager::Prepare(p->descriptorPath);});
#ifdef _WIN32
        j=p->descriptor;j["directories"]["scenes"]="assets";j["startupScene"]="assets/Main.json";
        {std::ofstream f(p->descriptorPath);f<<j;}Reject([&]{ProjectManager::Prepare(p->descriptorPath);});
#endif
        j=p->descriptor;j["directories"]["assets"]="../outside";{std::ofstream f(p->descriptorPath);f<<j;}Reject([&]{ProjectManager::Prepare(p->descriptorPath);});
        j=p->descriptor;{std::ofstream f(p->descriptorPath);f<<j;}
        std::error_code linkError;fs::create_directory_symlink(b->AssetRoot(),p->AssetRoot()/"linked",linkError);
        if(!linkError){Reject([&]{p->ResolveAsset("linked/anything.lua");});fs::remove(p->AssetRoot()/"linked");}
        else std::cout<<"Symlink fixture unavailable: "<<linkError.message()<<'\n';
#ifdef _WIN32
        auto junction=p->AssetRoot()/"junction";Junction(junction,b->AssetRoot());
        Reject([&]{p->ResolveAsset("junction/anything.lua");});
        Reject([&]{ProjectManager::Create(junction,"Escaped");});
        Check(fs::remove(junction),"remove owned junction without touching target");
        Check(fs::is_directory(b->AssetRoot()/"scripts"),"junction target preserved");
#endif
        fs::remove(p->ResolveScene(p->startupScene));Reject([&]{ProjectManager::Prepare(p->descriptorPath);});
        ProjectManager::Activate(b);Check(ProjectManager::Active()==b,"active context");ProjectManager::Activate({});
        fs::remove_all(root);std::cout<<"Project core passed\n";return 0;
    }catch(const std::exception& e){ProjectManager::Activate({});std::cerr<<e.what()<<'\n';std::error_code ec;fs::remove_all(root,ec);return 1;}
}
