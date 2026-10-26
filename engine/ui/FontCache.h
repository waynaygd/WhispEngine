#pragma once
#include "UIFont.h"
#include "../resources/FontResource.h"
#include "../resources/Resource.h"
class ResourceManager;class IRenderAdapter;
namespace ui {
// CPU data is owned by ResourceManager. This cache owns only per-device GPU atlases.
class FontCache {
 struct Entry {ResourceHandle<FontResource> resource;std::uint64_t revision=~0ull;RenderTextureHandle texture;std::string error;bool reported=false;};
 std::map<std::string,Entry> entries;
 std::uint64_t epoch=~0ull;bool limitReported=false;
public:
 struct Counters {std::uint64_t uploads=0,retired=0;};
 Counters counters;
 static constexpr std::size_t MaxFonts=32;
 FontView Get(const std::string&,ResourceManager&,IRenderAdapter*);
 void Clear(IRenderAdapter*);
 std::size_t Size()const{return entries.size();}
};
}
