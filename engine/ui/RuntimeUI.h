#pragma once
#include "UIDocument.h"
#include "../ecs/World.h"
#include "../render/RenderResourceHandles.h"
#include "../resources/TextureData.h"
#include "../resources/Resource.h"
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
struct CanvasHandle {ecs::Entity entity{};std::uint64_t session=0,serial=0,revision=0;std::string document;};
struct PointerEvent {CanvasHandle canvas;std::string widget,type;};
enum class InputMode {GameOnly,UIOnly,GameAndUI};
struct Instance {
 std::string resource;std::uint64_t version=0,serial=0;Document state;
 ResourceHandle<UIResource> asset;std::map<std::string,unsigned> overrides;
};
class RuntimeUI {
 std::unordered_map<ecs::Entity,Instance,ecs::EntityHash> instances;
 const ecs::World* world=nullptr;ResourceManager* manager=nullptr;
 std::uint64_t session=1,nextSerial=1,epoch=0;
 std::unordered_map<std::string,ResourceHandle<UIResource>> documents;
 std::vector<ecs::Entity> order;
 CanvasHandle hovered,pressed;std::string hoverWidget,pressWidget;bool pointerDown=false,gestureOwned=false;
 std::vector<PointerEvent> events;
 Instance& Resolve(const CanvasHandle&);const Instance& Resolve(const CanvasHandle&)const;
 void Emit(const CanvasHandle&,const std::string&,const std::string&);
public:
 const auto& Instances()const{return instances;}
 void Clear();void CancelPointer();
 void Sync(const ecs::World&,ResourceManager&);
 CanvasHandle Get(const std::string&)const;CanvasHandle Get(ecs::Entity)const;
 bool Valid(const CanvasHandle&)const;
 const Widget& Read(const CanvasHandle&,const std::string&)const;
 void Set(const CanvasHandle&,const std::string&,const std::string& property,const nlohmann::json&);
 // Coordinates are render-target pixels. Disabled routing cancels the gesture.
 bool Pointer(float x,float y,float width,float height,bool down,bool available);
 std::vector<PointerEvent> TakeEvents(){auto out=std::move(events);events.clear();return out;}
 DrawList Build(const ecs::World&,ResourceManager&,float width,float height,const FontAtlas&,RenderTextureHandle,const TextureResolver&);
};
}
