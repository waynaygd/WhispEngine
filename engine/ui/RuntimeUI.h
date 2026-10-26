#pragma once
#include "UIDocument.h"
#include "UIFont.h"
#include "../ecs/World.h"
#include "../render/RenderResourceHandles.h"
#include "../resources/TextureData.h"
#include "../resources/Resource.h"
#include <functional>
#include <map>
class ResourceManager;
namespace ui {
std::map<std::string,Rect> ComputeLayout(const Document&,Rect canvas);
bool ManagedByLayout(const Document&,const Widget&);
Rect LayoutRect(const Widget&,Rect parent);Rect Intersect(Rect,Rect);
struct Vertex {float x,y,u,v;Color color;};
struct DrawCommand {unsigned first=0,count=0;Rect clip;RenderTextureHandle texture;};
struct WidgetDraw {std::string id;unsigned first=0,count=0;};
struct DrawList {std::vector<WidgetDraw> widgets;float width=0,height=0;std::vector<Vertex> vertices;std::vector<DrawCommand> commands;std::vector<std::string> errors;};
using TextureResolver=std::function<RenderTextureHandle(const std::string&)>;
DrawList LayoutDocument(const Document&,float width,float height,const FontAtlas&,RenderTextureHandle font,const TextureResolver&,const FontResolver& fonts={});
struct CanvasHandle {ecs::Entity entity{};std::uint64_t session=0,serial=0,revision=0;std::string document;};
struct PointerEvent {CanvasHandle canvas;std::string widget,type,animation;};
struct Playback {AnimationClip clip;float time=0;bool playing=true;};
struct ButtonTransition {Color from{},to{},current{};float elapsed=0;};
struct AnimationState {std::map<std::string,Playback> tracks;std::map<std::string,ButtonTransition> buttonColors;};
void PlayAnimation(Document&,AnimationState&,const std::string&,bool restart=true,float speed=1);
void StopAnimation(Document&,AnimationState&,const std::string&);
std::vector<std::pair<std::string,std::string>> AdvanceAnimations(Document&,AnimationState&,float dt);
class StyleCache {
 struct Entry {ResourceHandle<UIResource> resource;std::uint64_t revision=~0ull;};
 std::map<std::string,Entry> assets;std::uint64_t epoch=~0ull;
public:
 void Clear(){assets.clear();epoch=~0ull;}
 bool Changed(const Document&,ResourceManager&,bool sourceChanged=false);
 void Resolve(Widget&,const Widget&,unsigned runtimeMask=0)const;
 std::vector<std::string> errors;
};
enum class InputMode {GameOnly,UIOnly,GameAndUI};
struct Instance {
 std::string resource;std::uint64_t version=0,serial=0;Document state;
 ResourceHandle<UIResource> asset;std::map<std::string,unsigned> overrides;StyleCache styles;AnimationState animation;bool styleDirty=true;
};
class RuntimeUI {
 std::unordered_map<ecs::Entity,Instance,ecs::EntityHash> instances;
 const ecs::World* world=nullptr;ResourceManager* manager=nullptr;
 std::uint64_t session=1,nextSerial=1,epoch=0;
 std::unordered_map<std::string,ResourceHandle<UIResource>> documents;
 std::vector<ecs::Entity> order;
 CanvasHandle hovered,pressed;std::string hoverWidget,pressWidget;bool pointerDown=false,gestureOwned=false;
 std::vector<PointerEvent> events;
 Document previewSource,preview;StyleCache previewStyles;AnimationState previewAnimation;bool previewInitialized=false;
 Instance& Resolve(const CanvasHandle&);const Instance& Resolve(const CanvasHandle&)const;
 void Emit(const CanvasHandle&,const std::string&,const std::string&);
public:
 const auto& Instances()const{return instances;}
 void Clear();void CancelPointer();
 void Advance(float dt);void Play(const CanvasHandle&,const std::string&,bool restart=true,float speed=1);
 void Stop(const CanvasHandle&,const std::string&);bool Playing(const CanvasHandle&,const std::string&)const;
 const Document& Preview(const Document&,ResourceManager&);
 void PreviewPlay(const std::string&);void PreviewStop();void PreviewAdvance(float dt);

 void Sync(const ecs::World&,ResourceManager&);
 CanvasHandle Get(const std::string&)const;CanvasHandle Get(ecs::Entity)const;
 bool Valid(const CanvasHandle&)const;
 const Widget& Read(const CanvasHandle&,const std::string&)const;
 void Set(const CanvasHandle&,const std::string&,const std::string& property,const nlohmann::json&);
 // Coordinates are render-target pixels. Disabled routing cancels the gesture.
 bool Pointer(float x,float y,float width,float height,bool down,bool available);
 std::vector<PointerEvent> TakeEvents(){auto out=std::move(events);events.clear();return out;}
 DrawList Build(const ecs::World&,ResourceManager&,float width,float height,const FontAtlas&,RenderTextureHandle,const TextureResolver&,const FontResolver& fonts={});
};
}
