#include "ScriptEditor.h"
#include "../scripting/Prefab.h"
#include "../core/AssetPaths.h"
#include "../core/Logger.h"
#include <stdexcept>
#include <vector>
#include <algorithm>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>
#include <winver.h>

namespace {
struct Handle {
    HANDLE value=nullptr;
    ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle()=default;
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
};
std::wstring Environment(const wchar_t* name){
    const DWORD size=GetEnvironmentVariableW(name,nullptr,0);if(!size)return {};
    std::wstring value(size,L'\0');auto written=GetEnvironmentVariableW(name,value.data(),size);
    if(!written||written>=size)return {};value.resize(written);return value;
}
std::filesystem::path OnPath(const wchar_t* name){
    const auto paths=Environment(L"PATH");if(paths.empty())return {};
    const auto size=SearchPathW(paths.c_str(),name,nullptr,0,nullptr,nullptr);if(!size)return {};
    std::wstring path(size+1,L'\0');auto written=SearchPathW(paths.c_str(),name,nullptr,DWORD(path.size()),path.data(),nullptr);
    if(!written||written>=path.size())return {};path.resize(written);return path;
}
std::wstring Quote(const std::wstring& argument){
    std::wstring result=L"\"";unsigned slashes=0;
    for(wchar_t ch:argument){
        if(ch==L'\\'){++slashes;continue;}
        result.append(ch==L'"'?slashes*2+1:slashes,L'\\');slashes=0;result+=ch;
    }
    result.append(slashes*2,L'\\');return result+L'"';
}
bool IsVisualStudio2022(const std::filesystem::path& path){
    if(path.empty()||!std::filesystem::is_regular_file(path))return false;
    DWORD ignored=0;const DWORD size=GetFileVersionInfoSizeW(path.c_str(),&ignored);if(!size)return false;
    std::vector<BYTE> data(size);if(!GetFileVersionInfoW(path.c_str(),0,size,data.data()))return false;
    VS_FIXEDFILEINFO* version=nullptr;UINT length=0;
    return VerQueryValueW(data.data(),L"\\",reinterpret_cast<void**>(&version),&length)
        &&length>=sizeof(*version)&&version->dwSignature==0xfeef04bd&&HIWORD(version->dwFileVersionMS)==17;
}
std::filesystem::path QueryVswhere(const std::filesystem::path& executable){
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};Handle read,write;
    if(!CreatePipe(&read.value,&write.value,&attributes,0)||!SetHandleInformation(read.value,HANDLE_FLAG_INHERIT,0))return {};
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESTDHANDLES;
    startup.hStdOutput=write.value;startup.hStdError=write.value;
    // Limit to VS 2022 IDE installations, excluding Build Tools and later IDE versions.
    auto command=Quote(executable.wstring())+L" -latest -version [17.0,18.0) -products * -requires Microsoft.VisualStudio.Component.CoreEditor -find Common7\\IDE\\devenv.exe -utf8";
    PROCESS_INFORMATION process{};
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))return {};
    Handle child,thread;child.value=process.hProcess;thread.value=process.hThread;
    CloseHandle(write.value);write.value=nullptr;
    std::string output;const auto deadline=GetTickCount64()+5000;
    for(;;){
        DWORD available=0;
        while(PeekNamedPipe(read.value,nullptr,0,nullptr,&available,nullptr)&&available){
            char buffer[4096];DWORD count=0;
            if(!ReadFile(read.value,buffer,(std::min)(available,DWORD(sizeof(buffer))),&count,nullptr)||!count)break;
            output.append(buffer,count);
            if(output.size()>128*1024){TerminateProcess(child.value,1);WaitForSingleObject(child.value,1000);return {};}
        }
        DWORD code=0;if(GetExitCodeProcess(child.value,&code)&&code!=STILL_ACTIVE){
            // Drain stdout after observing process exit (the last write can race the poll).
            char buffer[4096];DWORD count=0;
            while(ReadFile(read.value,buffer,sizeof(buffer),&count,nullptr)&&count){output.append(buffer,count);if(output.size()>128*1024)return {};}
            if(code!=0)return {};break;
        }
        if(GetTickCount64()>=deadline){TerminateProcess(child.value,1);WaitForSingleObject(child.value,1000);return {};}
        WaitForSingleObject(child.value,10);
    }
    const int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,output.data(),int(output.size()),nullptr,0);if(!size)return {};
    std::wstring path(size,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,output.data(),int(output.size()),path.data(),size);
    const auto begin=path.find_first_not_of(L"\ufeff\r\n \t");if(begin==std::wstring::npos)return {};
    auto end=path.find_first_of(L"\r\n",begin);path=path.substr(begin,end==std::wstring::npos?end:end-begin);
    const auto last=path.find_last_not_of(L" \t");if(last!=std::wstring::npos)path.resize(last+1);
    return path;
}
std::filesystem::path FindVisualStudio(){
    std::vector<std::filesystem::path> locators;
    for(const auto* variable:{L"ProgramFiles(x86)",L"ProgramFiles"}){
        auto root=Environment(variable);if(!root.empty())locators.push_back(std::filesystem::path(root)/L"Microsoft Visual Studio"/L"Installer"/L"vswhere.exe");
    }
    locators.push_back(OnPath(L"vswhere.exe"));
    for(const auto& locator:locators)if(!locator.empty()&&std::filesystem::is_regular_file(locator)){
        auto ide=QueryVswhere(locator);if(IsVisualStudio2022(ide))return ide;
    }
    auto ide=OnPath(L"devenv.exe");if(IsVisualStudio2022(ide))return ide;
    throw std::runtime_error("Microsoft Visual Studio 2022 IDE was not found. Check its installation (including Core Editor) and vswhere.exe or devenv.exe on PATH.");
}
}
#endif

namespace editor {
std::filesystem::path ResolveEditableLuaScript(const std::string& key){
    const auto path=scripting::SafeAsset(key,".lua");
    if(!std::filesystem::is_regular_file(path))throw std::runtime_error("Lua script is missing from the active Asset Root: "+AssetPaths::ToUtf8String(path));
    return path;
}
bool OpenLuaScriptInVisualStudio(const std::string& key,std::string* error){
    try{
        const auto script=ResolveEditableLuaScript(key);
#if defined(_WIN32)
        const auto ide=FindVisualStudio();const auto parameters=L"/Edit "+Quote(script.wstring());
        const auto directory=script.parent_path().wstring();
        SHELLEXECUTEINFOW launch{};launch.cbSize=sizeof(launch);launch.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_FLAG_NO_UI;
        launch.lpVerb=L"open";launch.lpFile=ide.c_str();launch.lpParameters=parameters.c_str();launch.lpDirectory=directory.c_str();launch.nShow=SW_SHOWNORMAL;
        if(!ShellExecuteExW(&launch))throw std::runtime_error("Could not open Lua script in Visual Studio 2022 (Windows error "+std::to_string(GetLastError())+"): "+AssetPaths::ToUtf8String(script));
        Handle process;process.value=launch.hProcess;
        Logger::Get().Info("Open Script: Visual Studio 2022 /Edit "+AssetPaths::ToUtf8String(script));
        if(error)error->clear();return true;
#else
        throw std::runtime_error("Opening scripts in Microsoft Visual Studio 2022 requires Windows.");
#endif
    }catch(const std::exception& failure){if(error)*error=failure.what();Logger::Get().Error("Open Script: "+std::string(failure.what()));return false;}
}
}
