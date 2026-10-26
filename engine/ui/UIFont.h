#pragma once
#include "UIDocument.h"
#include "../resources/TextureData.h"
#include "../render/RenderResourceHandles.h"
#include <map>
#include <functional>
namespace ui {
struct Rect {float x=0,y=0,w=0,h=0;};
struct Glyph {Rect pixels;std::array<float,4> uv{};float advance=0;};
struct FontAtlas {TextureData texture;std::map<unsigned,Glyph> glyphs;float ascent=0,descent=0,lineHeight=0,height=32;};
FontAtlas RasterizeFont();
std::vector<unsigned> FontCoverage();
FontAtlas RasterizeFontFile(const std::filesystem::path&,float size=32,const std::vector<unsigned>& coverage=FontCoverage());
std::vector<unsigned> DecodeUTF8(const std::string&);
struct FontView {const FontAtlas* atlas=nullptr;RenderTextureHandle texture;std::string error;};
using FontResolver=std::function<FontView(const std::string&)>;
}
