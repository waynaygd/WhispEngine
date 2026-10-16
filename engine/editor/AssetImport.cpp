#define NOMINMAX
#include "AssetImport.h"
#include "GltfSceneImport.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <set>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <thread>
#include <chrono>
#include "../resources/loaders/MeshLoader.h"
#include "../resources/loaders/TextureLoader.h"
#include "../core/AssetPaths.h"
#include "../core/ProjectContext.h"
#include <stb_image.h>
#ifdef _WIN32
#include <Windows.h>
#endif
namespace editor {
namespace {
namespace fs=std::filesystem;
constexpr std::uintmax_t MaxFile=256ull*1024*1024,MaxTotal=512ull*1024*1024;
std::string Utf8(const fs::path& p){auto s=p.generic_u8string();return {s.begin(),s.end()};}
std::string Ext(const fs::path& p){auto s=p.extension().string();std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return char(std::tolower(c));});return s;}
void CheckLinks(fs::path p){for(;!p.empty();p=p.parent_path()){
 if(fs::is_symlink(fs::symlink_status(p)))throw std::runtime_error("Symlinks are not allowed: "+Utf8(p));
#ifdef _WIN32
 auto a=GetFileAttributesW(p.c_str());if(a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Reparse points are not allowed: "+Utf8(p));
#endif
 if(p==p.parent_path())break;
}}
void Key(const std::string& key){
 if(key.empty()||key.size()>240||key.front()=='/'||key.back()=='/')throw std::runtime_error("Expected a portable relative path (1..240 bytes)");
 for(const auto& part:fs::u8path(key)){auto s=Utf8(part);if(s.empty()||s=="."||s==".."||s.back()=='.'||s.back()==' ')throw std::runtime_error("Invalid path component");
  for(unsigned char c:s)if(c<32||std::string(":\\*?\"<>|").find(char(c))!=std::string::npos)throw std::runtime_error("Invalid filename character");
  auto stem=s.substr(0,s.find('.'));std::transform(stem.begin(),stem.end(),stem.begin(),[](unsigned char c){return char(std::toupper(c));});
  if(stem=="CON"||stem=="PRN"||stem=="AUX"||stem=="NUL"||(stem.size()==4&&(stem.starts_with("COM")||stem.starts_with("LPT"))&&stem[3]>='0'&&stem[3]<='9'))throw std::runtime_error("Reserved filename");
 }
}
std::string Read(const fs::path& p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("Cannot read "+Utf8(p));return {std::istreambuf_iterator<char>(f),{}};}
std::string Uri(std::string s){
 if(s.starts_with("data:"))return {};
 std::string r;for(std::size_t i=0;i<s.size();++i){if(s[i]=='%'){if(i+2>=s.size())throw std::runtime_error("Invalid URI escape");auto hex=[](char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};int a=hex(s[++i]),b=hex(s[++i]);if(a<0||b<0)throw std::runtime_error("Invalid URI escape");r+=char(a*16+b);}else r+=s[i];}
 if(r.empty()||r.front()=='/'||r.find(':')!=std::string::npos||r.find('\\')!=std::string::npos)throw std::runtime_error("External/network/absolute URI is forbidden");return r;
}
}
std::filesystem::path AssetImport::ProjectRoot(){
 if(auto project=ProjectManager::Active())return project->AssetRoot();
 for(auto start:{fs::current_path(),AssetPaths::GetExecutableDirectory()})for(auto p=start;!p.empty();p=p.parent_path()){
  auto candidate=p/"engine"/"assets";if(fs::is_directory(candidate)){CheckLinks(candidate);return candidate;}if(p==p.parent_path())break;
 }
 return AssetPaths::ResolveAssetRoot();
}
std::filesystem::path AssetImport::SafeTarget(const fs::path& root,const std::string& key){
 Key(key);auto base=fs::absolute(root).lexically_normal();auto target=(base/fs::u8path(key)).lexically_normal();CheckLinks(base);CheckLinks(target);
 auto rel=target.lexically_relative(base);if(rel.empty()||*rel.begin()=="..")throw std::runtime_error("Target escapes asset root");return target;
}
bool AssetImport::NewFolder(const fs::path& root,const std::string& parent,const std::string& name,std::string* error){try{
 Key(name);if(fs::u8path(name).has_parent_path())throw std::runtime_error("Folder name must be one component");auto target=SafeTarget(root,parent.empty()?name:parent+"/"+name);
 if(!fs::is_directory(target.parent_path()))throw std::runtime_error("Parent folder does not exist");if(!fs::create_directory(target))throw std::runtime_error("Folder already exists");return true;
 }catch(const std::exception& e){if(error)*error=e.what();return false;}}
bool AssetImport::Scan(const fs::path& source,const std::string& package,ImportPlan& output,std::string* error,const fs::path& dependencyRoot){try{
 Key(package);CheckLinks(source);if(!fs::is_regular_file(source))throw std::runtime_error("Source must be a regular file");auto ext=Ext(source);
 const std::set<std::string> supported={".obj",".gltf",".glb",".png",".jpg",".jpeg",".dds",".hdr"};if(!supported.contains(ext))throw std::runtime_error("Unsupported import format: "+ext);
 if((ext==".obj"||ext==".gltf"||ext==".glb")&&!package.starts_with("models/"))throw std::runtime_error("Models must be imported into models/");
 if(ext==".hdr"&&!package.starts_with("environments/"))throw std::runtime_error("HDR must be imported into environments/");
 if((ext==".png"||ext==".jpg"||ext==".jpeg"||ext==".dds")&&!package.starts_with("textures/"))throw std::runtime_error("Textures must be imported into textures/");
 ImportPlan plan;plan.sourceRoot=fs::absolute(dependencyRoot.empty()?source.parent_path():dependencyRoot).lexically_normal();CheckLinks(plan.sourceRoot);if(!fs::is_directory(plan.sourceRoot))throw std::runtime_error("Dependency root must be an existing directory");plan.package=package;plan.entry=Utf8(fs::absolute(source).lexically_normal().lexically_relative(plan.sourceRoot));Key(plan.entry);
 const auto modelFolder=fs::u8path(plan.entry).parent_path();
 std::set<std::string> seen;
 auto add=[&](const std::string& rel){Key(rel);if(!seen.insert(rel).second)return;auto p=SafeTarget(plan.sourceRoot,rel);if(!fs::is_regular_file(p))throw std::runtime_error("Missing dependency: "+rel);auto n=fs::file_size(p);if(n>MaxFile||plan.bytes+n>MaxTotal||plan.files.size()>=2048)throw std::runtime_error("Import size/file-count limit exceeded");plan.files.push_back({p,rel,n});plan.bytes+=n;};
 add(plan.entry);
 if(ext==".gltf"||ext==".glb"){
  if(fs::file_size(source)>MaxFile)throw std::runtime_error("Model too large");auto text=Read(source);
  if(ext==".glb"){if(text.size()<20)throw std::runtime_error("Truncated GLB");auto u32=[&](std::size_t o){std::uint32_t n;std::memcpy(&n,text.data()+o,4);return n;};if(u32(0)!=0x46546c67||u32(4)!=2||u32(8)!=text.size()||u32(16)!=0x4e4f534a||u32(12)>text.size()-20)throw std::runtime_error("Invalid GLB v2 JSON chunk");text=text.substr(20,u32(12));}
  if(text.size()>16*1024*1024)throw std::runtime_error("glTF JSON exceeds 16 MiB");auto j=nlohmann::json::parse(text);
  for(const char* list:{"buffers","images"})if(j.contains(list))for(const auto& row:j.at(list))if(row.contains("uri")){auto uri=Uri(row.at("uri").get<std::string>());if(!uri.empty())add(Utf8((modelFolder/fs::u8path(uri)).lexically_normal()));}
 }
 if(ext==".obj"){
  std::istringstream obj(Read(source));std::string line;std::vector<std::string> mtls;
  while(std::getline(obj,line)){std::istringstream in(line);std::string command;in>>command;if(command=="mtllib"){std::string m;while(in>>m){if(m.starts_with("#"))break;auto rel=Utf8((modelFolder/fs::u8path(m)).lexically_normal());add(rel);mtls.push_back(rel);}}}
  for(const auto& m:mtls){std::istringstream mtl(Read(SafeTarget(plan.sourceRoot,m)));while(std::getline(mtl,line)){std::istringstream in(line);std::string command;in>>command;if(command.starts_with("map_")||command=="bump"||command=="disp"||command=="norm"||command=="refl"){
   std::string path;std::getline(in>>std::ws,path);if(path.empty()||path.front()=='-')throw std::runtime_error("MTL texture options need manual flattening before import");
   auto rel=Utf8((fs::u8path(m).parent_path()/fs::u8path(path)).lexically_normal());add(rel);
  }}}
 }
 // Reject executable/unknown dependencies; no file supplied by an asset is executed.
 const std::set<std::string> dependencies={".obj",".gltf",".glb",".bin",".mtl",".png",".jpg",".jpeg",".dds",".hdr"};
 for(const auto& f:plan.files)if(!dependencies.contains(Ext(f.source)))throw std::runtime_error("Unsupported dependency: "+f.relative);
 output=std::move(plan);return true;
 }catch(const std::exception& e){if(error)*error=e.what();return false;}}
bool AssetImport::Publish(const fs::path& root,const ImportPlan& plan,std::string* error,ModelImportMode mode){
 fs::path stage;
 bool ownsStage=false;
 std::vector<fs::path> ownedParents;
 try{
  ImportPlan verified;if(!Scan(plan.sourceRoot/fs::u8path(plan.entry),plan.package,verified,error,plan.sourceRoot))return false;
  if(verified.files.size()!=plan.files.size()||verified.bytes!=plan.bytes)throw std::runtime_error("Source changed since dependency scan; scan again");
  auto destination=SafeTarget(root,plan.package);if(fs::exists(destination))throw std::runtime_error("Destination package exists; choose a new name (no overwrite)");
  // Staged assets take precedence during loading; refuse a collision there too.
  auto authoring=ProjectRoot(),runtime=AssetPaths::ResolveAssetRoot();auto canonicalRoot=fs::weakly_canonical(root);
  if(canonicalRoot==fs::weakly_canonical(authoring)||canonicalRoot==fs::weakly_canonical(runtime))
   for(const auto& other:{authoring,runtime})if(fs::weakly_canonical(other)!=canonicalRoot&&fs::exists(SafeTarget(other,plan.package)))throw std::runtime_error("Package exists in another project/staging asset root; choose a new name");
  if(!fs::is_directory(root))throw std::runtime_error("Asset root does not exist");std::vector<fs::path> missing;
  for(auto parent=destination.parent_path();!fs::exists(parent);parent=parent.parent_path())missing.push_back(parent);
  for(auto it=missing.rbegin();it!=missing.rend();++it)if(fs::create_directory(*it))ownedParents.push_back(*it);
  static std::atomic<unsigned> sequence{0};stage=destination;stage+=".import-"+std::to_string(++sequence);
  if(!fs::create_directory(stage))throw std::runtime_error("Import staging folder already exists");
  ownsStage=true;
  for(const auto& f:verified.files){auto target=SafeTarget(stage,f.relative);fs::create_directories(target.parent_path());fs::copy_file(f.source,target,fs::copy_options::none);if(fs::file_size(target)!=f.bytes)throw std::runtime_error("Source changed while copying");}
  // Validate using existing decoders on owned staging files, never on source files.
  // Loader caches contain staging-relative material keys: remove them before publication.
  for(const auto& f:verified.files){auto path=stage/fs::u8path(f.relative);auto ext=Ext(path);
   if(ext==".png"||ext==".jpg"||ext==".jpeg"||ext==".dds"){
    if(ext!=".dds"){auto encoded=Read(path);int w=0,h=0,c=0;if(!stbi_info_from_memory(reinterpret_cast<const stbi_uc*>(encoded.data()),int(encoded.size()),&w,&h,&c)||w<=0||h<=0||std::uint64_t(w)*h>64ull*1024*1024)throw std::runtime_error("Invalid/oversized image: "+f.relative);}
    auto decoded=TextureLoader::Load(plan.package+"/"+f.relative,path);if(!decoded.success)throw std::runtime_error("Image validation failed: "+decoded.errorMessage);
   }
   if(ext==".hdr"){auto encoded=Read(path);auto* bytes=reinterpret_cast<const stbi_uc*>(encoded.data());int w=0,h=0,c=0;if(!stbi_is_hdr_from_memory(bytes,int(encoded.size()))||!stbi_info_from_memory(bytes,int(encoded.size()),&w,&h,&c)||w<=0||h<=0||std::uint64_t(w)*h>16ull*1024*1024||f.bytes>64ull*1024*1024)throw std::runtime_error("HDR must be Radiance <=64 MiB / 16 MP");}
  }
  auto ext=Ext(fs::u8path(plan.entry));if(ext==".obj"||ext==".gltf"||ext==".glb"){
   auto decoded=MeshLoader::Load(plan.package+"/"+plan.entry,stage/fs::u8path(plan.entry));if(!decoded.success||decoded.data.meshData.vertices.empty())throw std::runtime_error("Mesh validation failed: "+decoded.errorMessage);
  }
  if(mode!=ModelImportMode::SingleMesh)GltfSceneImport::Prepare(stage,verified);
  for(const auto& item:fs::recursive_directory_iterator(stage))if(item.is_regular_file()&&(item.path().extension()==".wmesh"||item.path().extension()==".wtex")){std::error_code ec;fs::remove(item.path(),ec);}
  for(const auto& f:verified.files)for(const char* suffix:{".wmesh",".wtex"}){auto cache=stage/fs::u8path(f.relative);cache+=suffix;std::error_code ec;fs::remove(cache,ec);}
  CheckLinks(destination.parent_path());
  // Windows scanners can retain a just-decoded file briefly. Keep the atomic
  // publication and no-overwrite policy; retry only transient access/sharing errors.
  for(unsigned attempt=0;;++attempt){
   std::error_code ec;fs::rename(stage,destination,ec);if(!ec)break;
#ifdef _WIN32
   if(attempt<10&&(ec.value()==ERROR_ACCESS_DENIED||ec.value()==ERROR_SHARING_VIOLATION||ec.value()==ERROR_LOCK_VIOLATION)&&!fs::exists(destination)){
    std::this_thread::sleep_for(std::chrono::milliseconds(20));CheckLinks(destination.parent_path());continue;
   }
#endif
   throw fs::filesystem_error("Import publication failed",stage,destination,ec);
  }
  stage.clear();return true;
 }catch(const std::exception& e){if(ownsStage&&!stage.empty()){std::error_code ec;fs::remove_all(stage,ec);}for(auto it=ownedParents.rbegin();it!=ownedParents.rend();++it){std::error_code ec;fs::remove(*it,ec);}if(error)*error=e.what();return false;}
}
}
