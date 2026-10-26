#include "FontResource.h"
#include "../ui/UIFont.h"
#include "../scripting/Prefab.h"
#include <atomic>
#include <algorithm>
#include "../ecs/ComponentRegistry.h"
#include <chrono>
#include <fstream>
#include <stdexcept>
namespace {std::atomic<std::uint64_t> loads=0,rasters=0;}
bool FontLoader::ValidKey(const std::string& key){auto ext=std::filesystem::u8path(key).extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});return !key.empty()&&key.find('\0')==std::string::npos&&(ext==".ttf"||ext==".otf")&&ecs::ComponentRegistry::ValidateAssetKey(key,ecs::AssetKind::Font);}
std::filesystem::path FontLoader::SafePath(const std::string& key){if(!ValidKey(key))throw std::runtime_error("Expected a safe project-relative .ttf/.otf Font Asset");return scripting::SafeAsset(key,std::filesystem::u8path(key).extension().string());}
FontLoadStats FontLoader::Stats(){return {loads.load(),rasters.load()};}
ResourceLoadResult<FontResource> FontLoader::Load(const std::string& key,const std::filesystem::path&){
 ++loads;auto start=std::chrono::steady_clock::now();
 try{auto path=SafePath(key);if(!std::filesystem::is_regular_file(path)||std::filesystem::file_size(path)>16*1024*1024)throw std::runtime_error("Font is missing or exceeds 16 MiB");
  std::ifstream input(path,std::ios::binary);unsigned char signature[4]{};input.read(reinterpret_cast<char*>(signature),4);if(!input||!((signature[0]==0&&signature[1]==1&&signature[2]==0&&signature[3]==0)||(signature[0]=='O'&&signature[1]=='T'&&signature[2]=='T'&&signature[3]=='O')))throw std::runtime_error("Unsupported/corrupt SFNT font; expected TrueType or OpenType/CFF");
  auto rasterStart=std::chrono::steady_clock::now();++rasters;auto atlas=std::make_shared<ui::FontAtlas>(ui::RasterizeFontFile(path));auto end=std::chrono::steady_clock::now();FontResource data;data.cpuBytes=atlas->texture.pixels.size()+atlas->glyphs.size()*sizeof(ui::Glyph);data.atlas=std::move(atlas);data.loadMs=std::chrono::duration<double,std::milli>(end-start).count();data.rasterMs=std::chrono::duration<double,std::milli>(end-rasterStart).count();return {true,std::move(data),{}};
 }catch(const std::exception& e){return {false,{},e.what()};}
}
