#include "GltfSceneImport.h"
#include "../resources/loaders/MeshLoader.h"
#include "../resources/loaders/TextureLoader.h"
#include "../core/AssetPaths.h"
#include <fstream>
#include <cstring>
#include <set>
#include <functional>
#include <cfloat>
#include <map>
namespace editor {
namespace {
using Json=nlohmann::json;namespace fs=std::filesystem;
std::string Read(const fs::path& p,std::uintmax_t limit){if(fs::file_size(p)>limit)throw std::runtime_error("Imported document exceeds size limit");std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("Cannot read imported document");return {std::istreambuf_iterator<char>(f),{}};}
void Write(const fs::path& p,const std::string& bytes){if(fs::exists(p))throw std::runtime_error("Generated import filename collides with source dependency");std::ofstream f(p,std::ios::binary);f.write(bytes.data(),bytes.size());f.close();if(!f)throw std::runtime_error("Cannot write generated imported asset");}
std::string Base64(const std::string& text){
 std::string result;unsigned value=0;int bits=-8;bool padded=false;const std::string alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
 for(unsigned char c:text){if(std::isspace(c))continue;if(c=='='){padded=true;continue;}auto n=alphabet.find(char(c));if(padded||n==std::string::npos)throw std::runtime_error("Invalid embedded base64 data");value=(value<<6)|unsigned(n);bits+=6;if(bits>=0){result+=char((value>>bits)&255);bits-=8;}}return result;
}
std::string DecodeUri(const std::string& input){std::string output;for(std::size_t i=0;i<input.size();++i){if(input[i]=='%'){if(i+2>=input.size())throw std::runtime_error("Invalid URI escape");auto h=input.substr(i+1,2);std::size_t used=0;auto value=std::stoul(h,&used,16);if(used!=2)throw std::runtime_error("Invalid URI escape");output+=char(value);i+=2;}else output+=input[i];}return output;}
ImportMatrix LocalMatrix(const Json& node){
 if(node.contains("matrix")){if(node.contains("translation")||node.contains("rotation")||node.contains("scale"))throw std::runtime_error("Node has both matrix and TRS");return node.at("matrix").get<ImportMatrix>();}
 auto p=node.value("translation",std::array<double,3>{0,0,0}),s=node.value("scale",std::array<double,3>{1,1,1});auto q=node.value("rotation",std::array<double,4>{0,0,0,1});
 for(auto v:p)if(!std::isfinite(v))throw std::runtime_error("Invalid glTF translation");for(auto v:s)if(!std::isfinite(v))throw std::runtime_error("Invalid glTF scale");double n=0;for(auto v:q){if(!std::isfinite(v))throw std::runtime_error("Invalid quaternion");n+=v*v;}if(n<1e-12)throw std::runtime_error("Zero quaternion");for(auto& v:q)v/=std::sqrt(n);
 auto m=IdentityMatrix();double x=q[0],y=q[1],z=q[2],w=q[3];
 m[0]=(1-2*(y*y+z*z))*s[0];m[1]=2*(x*y+w*z)*s[0];m[2]=2*(x*z-w*y)*s[0];
 m[4]=2*(x*y-w*z)*s[1];m[5]=(1-2*(x*x+z*z))*s[1];m[6]=2*(y*z+w*x)*s[1];
 m[8]=2*(x*z+w*y)*s[2];m[9]=2*(y*z-w*x)*s[2];m[10]=(1-2*(x*x+y*y))*s[2];for(int i=0;i<3;++i)m[12+i]=p[i];return m;
}
}
std::string GltfSceneImport::Prepare(const fs::path& stage,const ImportPlan& plan){
 std::uintmax_t generatedBytes=plan.bytes;
 auto emit=[&](const fs::path& path,const std::string& data){if(generatedBytes+data.size()>512ull*1024*1024)throw std::runtime_error("Generated import package exceeds 512 MiB");Write(path,data);generatedBytes+=data.size();};
 const auto source=stage/fs::u8path(plan.entry);auto bytes=Read(source,256ull*1024*1024);Json document;std::string binary;
 auto extension=source.extension().string();std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
 if(extension==".glb"){
  auto word=[&](std::size_t i){if(i+4>bytes.size())throw std::runtime_error("Truncated GLB");std::uint32_t n;std::memcpy(&n,bytes.data()+i,4);return n;};
  if(word(0)!=0x46546c67||word(4)!=2||word(8)!=bytes.size())throw std::runtime_error("Invalid GLB header");
  bool json=false;for(std::size_t at=12;at<bytes.size();){auto size=word(at),type=word(at+4);at+=8;if(size>bytes.size()-at)throw std::runtime_error("Invalid GLB chunk");if(type==0x4e4f534a){if(json||size>16*1024*1024)throw std::runtime_error("Invalid GLB JSON");document=Json::parse(bytes.substr(at,size));json=true;}else if(type==0x004e4942){if(!binary.empty())throw std::runtime_error("Multiple GLB BIN chunks");binary=bytes.substr(at,size);}at+=size;}if(!json)throw std::runtime_error("Missing GLB JSON");
 }else if(extension==".gltf"){if(bytes.size()>16*1024*1024)throw std::runtime_error("glTF JSON exceeds 16 MiB");document=Json::parse(bytes);}else throw std::runtime_error("Separate/scene import supports glTF/GLB only");
 if(document.at("asset").at("version")!="2.0")throw std::runtime_error("Expected glTF 2.0");
 if(!document.value("animations",Json::array()).empty()||!document.value("skins",Json::array()).empty()||!document.value("extensionsRequired",Json::array()).empty())throw std::runtime_error("Static separate import does not support animation, skins or mandatory extensions");
 std::function<void(const Json&)> validateUv=[&](const Json& j){if(j.is_object()){if(j.contains("texCoord")&&j.at("texCoord").get<int>()!=0)throw std::runtime_error("Separate import currently supports material UV0 only");if(j.contains("KHR_texture_transform"))throw std::runtime_error("KHR_texture_transform unsupported by current material renderer");for(const auto& v:j)validateUv(v);}else if(j.is_array())for(const auto& v:j)validateUv(v);};if(document.contains("materials"))validateUv(document.at("materials"));
 const auto& nodes=document.at("nodes");const auto& meshes=document.at("meshes");if(nodes.empty()||nodes.size()>4096||meshes.size()>1024)throw std::runtime_error("Invalid/oversized scene graph");
 for(const auto& mesh:meshes)for(const auto& primitive:mesh.at("primitives"))if(primitive.value("mode",4)!=4||primitive.contains("targets")||primitive.contains("extensions"))throw std::runtime_error("Separate import requires static triangle primitives without compression/morph extensions");
 if(!binary.empty()){
  if(!document.contains("buffers")||document.at("buffers").empty()||document["buffers"][0].contains("uri"))throw std::runtime_error("Unexpected GLB binary mapping");
  auto name=AssetPaths::ToUtf8String(source.filename())+".whisp.bin";emit(source.parent_path()/fs::u8path(name),binary);document["buffers"][0]["uri"]=name;
 }
 std::map<unsigned,std::string> buffers;
 auto bufferBytes=[&](unsigned id)->const std::string&{auto found=buffers.find(id);if(found!=buffers.end())return found->second;auto uri=document.at("buffers").at(id).at("uri").get<std::string>();std::string data;if(uri.starts_with("data:")){auto comma=uri.find(',');if(comma==std::string::npos||uri.substr(0,comma).find(";base64")==std::string::npos)throw std::runtime_error("Expected base64 buffer data URI");data=Base64(uri.substr(comma+1));}else data=Read(AssetImport::SafeTarget(stage,AssetPaths::ToUtf8String((fs::u8path(plan.entry).parent_path()/fs::u8path(DecodeUri(uri))).lexically_normal())),256ull*1024*1024);return buffers.emplace(id,std::move(data)).first->second;};
 if(document.contains("images")){auto& images=document["images"];if(images.size()>1024)throw std::runtime_error("Imported image count exceeds 1024");for(unsigned id=0;id<images.size();++id){auto& image=images[id];std::string encoded,mime=image.value("mimeType",std::string());
  if(image.contains("bufferView")){const auto& view=document.at("bufferViews").at(image.at("bufferView").get<unsigned>());const auto& data=bufferBytes(view.at("buffer").get<unsigned>());auto offset=view.value("byteOffset",std::size_t(0)),length=view.at("byteLength").get<std::size_t>();if(offset>data.size()||length>data.size()-offset)throw std::runtime_error("Invalid image bufferView");encoded=data.substr(offset,length);}
  else if(image.contains("uri")){auto uri=image.at("uri").get<std::string>();if(!uri.starts_with("data:")){auto relative=AssetPaths::ToUtf8String((fs::u8path(plan.entry).parent_path()/fs::u8path(DecodeUri(uri))).lexically_normal());auto path=AssetImport::SafeTarget(stage,relative);if(!fs::is_regular_file(path))throw std::runtime_error("Missing staged image reference");auto decoded=TextureLoader::Load(plan.package+"/"+relative,path);if(!decoded.success||decoded.data.placeholder)throw std::runtime_error("Staged image validation failed: "+decoded.errorMessage);continue;}auto comma=uri.find(','),semi=uri.find(';');if(comma==std::string::npos||semi==std::string::npos||uri.substr(semi,comma-semi)!=";base64")throw std::runtime_error("Expected base64 image data URI");mime=uri.substr(5,semi-5);encoded=Base64(uri.substr(comma+1));}
  else throw std::runtime_error("Image has no data reference");
  std::string extension=mime=="image/png"?".png":mime=="image/jpeg"||mime=="image/jpg"?".jpg":mime=="image/vnd-ms.dds"?".dds":"";if(extension.empty()||encoded.empty())throw std::runtime_error("Unsupported embedded image MIME: "+mime);
  auto name=AssetPaths::ToUtf8String(source.filename())+".image-"+std::to_string(id)+extension;auto path=source.parent_path()/fs::u8path(name);emit(path,encoded);auto texture=TextureLoader::Load(plan.package+"/"+AssetPaths::ToUtf8String(path.lexically_relative(stage)),path);if(!texture.success||texture.data.placeholder)throw std::runtime_error("Extracted image validation failed: "+texture.errorMessage);image.erase("bufferView");image["uri"]=name;
 }}
 std::vector<unsigned> roots;auto scenes=document.value("scenes",Json::array());if(!scenes.empty()){int scene=document.value("scene",0);if(scene<0||scene>=int(scenes.size()))throw std::runtime_error("Invalid default scene");roots=scenes[scene].value("nodes",std::vector<unsigned>{});}else{
  std::set<unsigned> children;for(const auto& n:nodes)for(auto child:n.value("children",std::vector<unsigned>{}))children.insert(child);for(unsigned i=0;i<nodes.size();++i)if(!children.contains(i))roots.push_back(i);
 }
 Json manifest={{"coordinateConvention","WhispMeshLoaderNative"},{"version",1},{"source",plan.package+"/"+plan.entry},{"nodes",Json::array()}};std::set<unsigned> visited,used;
 const auto meshName=[&](unsigned i){return AssetPaths::ToUtf8String(source.stem())+"_mesh_"+std::to_string(i)+".gltf";};
 std::function<void(unsigned,const ImportMatrix&,unsigned)> visit=[&](unsigned id,const ImportMatrix& parent,unsigned depth){
  if(id>=nodes.size()||depth>256||!visited.insert(id).second)throw std::runtime_error("Invalid glTF graph: cycle, duplicate parent or excessive depth");const auto& node=nodes[id];if(node.contains("skin"))throw std::runtime_error("Skinned nodes unsupported");auto world=MultiplyMatrix(parent,LocalMatrix(node));
  if(node.contains("mesh")){unsigned mesh=node.at("mesh").get<unsigned>();if(mesh>=meshes.size())throw std::runtime_error("Invalid node mesh reference");auto t=DecomposeTransform(world);if(!t)throw std::runtime_error("Node "+std::to_string(id)+" has shear, singular or unrepresentable world TRS; use Single Mesh (baked) mode");
   auto key=plan.package+"/"+AssetPaths::ToUtf8String(fs::u8path(plan.entry).parent_path()/fs::u8path(meshName(mesh)));(void)AssetImport::SafeTarget(stage,AssetPaths::ToUtf8String(fs::u8path(plan.entry).parent_path()/fs::u8path(meshName(mesh))));
   std::string name=AssetPaths::ToUtf8String(source.stem())+"_"+node.value("name",std::string("Node_"+std::to_string(id)));if(name.size()>240)throw std::runtime_error("Imported node name exceeds 240 UTF-8 bytes; rename the node or package source");
   manifest["nodes"].push_back({{"node",id},{"name",name},{"mesh",key},{"position",{t->position.x,t->position.y,t->position.z}},{"rotation",{t->rotation.x,t->rotation.y,t->rotation.z}},{"scale",{t->scale.x,t->scale.y,t->scale.z}}});used.insert(mesh);
  }
  for(auto child:node.value("children",std::vector<unsigned>{}))visit(child,world,depth+1);
 };for(auto root:roots)visit(root,IdentityMatrix(),0);if(manifest["nodes"].empty())throw std::runtime_error("Selected scene contains no mesh nodes");
 std::map<std::string,std::array<ecs::Vec3,2>> bounds;
 for(unsigned mesh:used){auto part=document;part.erase("animations");part.erase("skins");part["nodes"]=Json::array({{{"mesh",0}}});part["meshes"]=Json::array({meshes[mesh]});part["scenes"]=Json::array({{{"nodes",{0}}}});part["scene"]=0;
  auto path=source.parent_path()/fs::u8path(meshName(mesh));emit(path,part.dump(2));auto key=plan.package+"/"+AssetPaths::ToUtf8String(path.lexically_relative(stage));auto loaded=MeshLoader::Load(key,path);if(!loaded.success||loaded.data.meshData.vertices.empty())throw std::runtime_error("Separate mesh validation: "+loaded.errorMessage);
  ecs::Vec3 lo{FLT_MAX,FLT_MAX,FLT_MAX},hi{-FLT_MAX,-FLT_MAX,-FLT_MAX};for(const auto& v:loaded.data.meshData.vertices){for(float x:v.position)if(!std::isfinite(x))throw std::runtime_error("Invalid imported vertex position");lo.x=std::min(lo.x,v.position[0]);lo.y=std::min(lo.y,v.position[1]);lo.z=std::min(lo.z,v.position[2]);hi.x=std::max(hi.x,v.position[0]);hi.y=std::max(hi.y,v.position[1]);hi.z=std::max(hi.z,v.position[2]);}for(auto i:loaded.data.meshData.indices)if(i>=loaded.data.meshData.vertices.size())throw std::runtime_error("Invalid imported index");bounds[key]={lo,hi};
 }
 ecs::Vec3 lo{FLT_MAX,FLT_MAX,FLT_MAX},hi{-FLT_MAX,-FLT_MAX,-FLT_MAX};
 for(const auto& node:manifest["nodes"]){auto b=bounds.at(node.at("mesh").get<std::string>());auto p=node.at("position").get<std::array<float,3>>(),r=node.at("rotation").get<std::array<float,3>>(),s=node.at("scale").get<std::array<float,3>>();ecs::TransformComponent t;t.position={p[0],p[1],p[2]};t.rotation={r[0],r[1],r[2]};t.scale={s[0],s[1],s[2]};float m[16];ecs::BuildRenderModelMatrix(m,ecs::RenderPose::From(t));for(int i=0;i<8;++i){ecs::Vec3 v{i&1?b[1].x:b[0].x,i&2?b[1].y:b[0].y,i&4?b[1].z:b[0].z};ecs::Vec3 w{m[0]*v.x+m[4]*v.y+m[8]*v.z+m[12],m[1]*v.x+m[5]*v.y+m[9]*v.z+m[13],m[2]*v.x+m[6]*v.y+m[10]*v.z+m[14]};lo.x=std::min(lo.x,w.x);lo.y=std::min(lo.y,w.y);lo.z=std::min(lo.z,w.z);hi.x=std::max(hi.x,w.x);hi.y=std::max(hi.y,w.y);hi.z=std::max(hi.z,w.z);}}
 manifest["placementOrigin"]={(lo.x+hi.x)*.5f,lo.y,(lo.z+hi.z)*.5f};manifest["bounds"]={{lo.x,lo.y,lo.z},{hi.x,hi.y,hi.z}};
 const auto name=plan.entry+".wscene";emit(AssetImport::SafeTarget(stage,name),manifest.dump(2));return plan.package+"/"+name;
}
nlohmann::json GltfSceneImport::ReadManifest(const fs::path& path){auto j=Json::parse(Read(path,16ull*1024*1024));if(j.value("coordinateConvention",std::string("WhispMeshLoaderNative"))!="WhispMeshLoaderNative")throw std::runtime_error("Unsupported imported coordinate convention");if(j.at("version")!=1||!j.at("nodes").is_array()||j.at("nodes").empty()||j.at("nodes").size()>4096)throw std::runtime_error("Invalid imported scene manifest");return j;}
}
