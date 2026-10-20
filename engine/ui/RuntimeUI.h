#pragma once
#include "UIDocument.h"
#include "../ecs/World.h"
#include "../render/RenderResourceHandles.h"
#include "../resources/TextureData.h"
#include <functional>
#include <map>
class ResourceManager;
namespace ui {
struct Rect {float x=0,y=0,w=0,h=0;};
Rect LayoutRect(const Widget&,Rect parent);Rect Intersect(Rect,Rect);
struct Glyph {Rect pixels;std::array<float,4> uv{};float advance=0;};
struct FontAtlas {TextureData texture;std::map<unsigned,Glyph> glyphs;float ascent=0,lineHeight=0,height=32;};
FontAtlas RasterizeFont();std::vector<unsigned> DecodeUTF8(const std::string&);
struct Vertex {float x,y,u,v;Color color;};
struct DrawCommand {unsigned first=0,count=0;Rect clip;RenderTextureHandle texture;};
struct WidgetDraw {std::string id;unsigned first=0,count=0;};
struct DrawList {std::vector<WidgetDraw> widgets;float width=0,height=0;std::vector<Vertex> vertices;std::vector<DrawCommand> commands;std::vector<std::string> errors;};
using TextureResolver=std::function<RenderTextureHandle(const std::string&)>;
DrawList LayoutDocument(const Document&,float width,float height,const FontAtlas&,RenderTextureHandle font,const TextureResolver&);
struct Instance {std::string resource;std::uint64_t version=0;Document state;};
class RuntimeUI {
 std::unordered_map<ecs::Entity,Instance,ecs::EntityHash> instances;
public:
 const auto& Instances()const{return instances;}
 void Clear(){instances.clear();}
 DrawList Build(const ecs::World&,ResourceManager&,float width,float height,const FontAtlas&,RenderTextureHandle,const TextureResolver&);
};
}
