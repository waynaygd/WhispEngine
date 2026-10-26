#pragma once
#include "ResourceLoader.h"
#include <filesystem>
#include <memory>
#include <cstdint>
namespace ui {struct FontAtlas;}
struct FontResource {std::shared_ptr<const ui::FontAtlas> atlas;std::uint64_t cpuBytes=0;double loadMs=0,rasterMs=0;};
struct FontLoadStats {std::uint64_t loads=0,rasterizations=0;};
class FontLoader {
public:
 static bool ValidKey(const std::string&);
 static std::filesystem::path SafePath(const std::string&);
 static ResourceLoadResult<FontResource> Load(const std::string&,const std::filesystem::path&);
 static FontLoadStats Stats();
};
