#include "RuntimeUI.h"
#include "../core/Logger.h"
#include "../ecs/components/UICanvasComponent.h"
#include "../ecs/components/SceneIdentityComponent.h"
#include "../resources/ResourceManager.h"
#include <set>
#include <tuple>
#include <utility>
#include "../ecs/ComponentRegistry.h"
namespace ui {
Rect Intersect(Rect a,Rect b){float x=std::max(a.x,b.x),y=std::max(a.y,b.y);return {x,y,std::max(0.f,std::min(a.x+a.w,b.x+b.w)-x),std::max(0.f,std::min(a.y+a.h,b.y+b.h)-y)};}
Rect LayoutRect(const Widget& w,Rect parent){Rect rect;float* start[]={&rect.x,&rect.y};float* extent[]={&rect.w,&rect.h};const float pstart[]={parent.x,parent.y},psize[]={parent.w,parent.h};for(int a=0;a<2;++a){float span=psize[a]*(w.anchorMax[a]-w.anchorMin[a]);*extent[a]=std::max(0.f,span+w.size[a]);*start[a]=pstart[a]+psize[a]*w.anchorMin[a]+span*w.pivot[a]+w.position[a]-*extent[a]*w.pivot[a];}return rect;}
namespace {
void Walk(const Document& document,float width,float height,const std::function<void(const Widget&,Rect,Rect,float)>& draw){
 if(!document.enabled||document.widgets.empty()||width<1||height<1)return;
 const float scale=std::min(width/document.referenceResolution[0],height/document.referenceResolution[1]);
 auto rectangles=ComputeLayout(document,{0,0,width/scale,height/scale});if(rectangles.empty())return;
 std::map<std::string,const Widget*> widgets;std::map<std::string,std::vector<const Widget*>> children;
 for(auto& w:document.widgets){widgets[w.id]=&w;children[w.parent].push_back(&w);}for(auto& [id,list]:children)std::sort(list.begin(),list.end(),[](auto a,auto b){return std::tie(a->zOrder,a->id)<std::tie(b->zOrder,b->id);});
 auto visit=[&](auto&& self,const Widget& w,Rect parent,Rect clip,float opacity)->void{
  if(!w.visible)return;Rect logical=rectangles.at(w.id);Rect r{logical.x*scale,logical.y*scale,logical.w*scale,logical.h*scale};opacity*=w.visualOpacity.value_or(w.opacity);
  draw(w,r,clip,opacity);auto childClip=w.clipChildren?Intersect(clip,r):clip;
  for(auto* child:children[w.id])self(self,*child,logical,childClip,opacity);
 };
 visit(visit,*widgets.at(document.root),{0,0,width/scale,height/scale},{0,0,width,height},1);
}
bool Contains(Rect r,float x,float y){return r.w>0&&r.h>0&&x>=r.x&&y>=r.y&&x<r.x+r.w&&y<r.y+r.h;}
}
DrawList LayoutDocument(const Document& document,float width,float height,const FontAtlas& atlas,RenderTextureHandle font,const TextureResolver& resolve,const FontResolver& fonts){
 DrawList out;out.width=width;out.height=height;auto error=Validate(document);if(!error.empty()){out.errors.push_back(error);return out;}if(!document.enabled||width<1||height<1||document.widgets.empty())return out;
 const float scale=std::min(width/document.referenceResolution[0],height/document.referenceResolution[1]);
 const Rect screen{0,0,width,height};const std::array<float,4> solid{.5f/atlas.texture.width,.5f/atlas.texture.height,.5f/atlas.texture.width,.5f/atlas.texture.height};
 auto quad=[&](Rect r,const std::array<float,4>& uv,Color color,Rect clip,RenderTextureHandle texture){if(r.w<=0||r.h<=0||clip.w<=0||clip.h<=0||color[3]<=0||out.vertices.size()+6>131072)return;if(Intersect(r,clip).w<=0||Intersect(r,clip).h<=0)return;unsigned first=unsigned(out.vertices.size());out.vertices.insert(out.vertices.end(),{{r.x,r.y,uv[0],uv[1],color},{r.x+r.w,r.y,uv[2],uv[1],color},{r.x+r.w,r.y+r.h,uv[2],uv[3],color},{r.x,r.y,uv[0],uv[1],color},{r.x+r.w,r.y+r.h,uv[2],uv[3],color},{r.x,r.y+r.h,uv[0],uv[3],color}});out.commands.push_back({first,6,clip,texture});};
 Walk(document,width,height,[&](const Widget& w,Rect r,Rect clip,float opacity){
  auto color=w.type=="Button"?((!w.enabled||w.interaction==3)?w.disabledColor:w.interaction==2?w.pressedColor:w.interaction==1?w.hoverColor:w.color):w.color;color[3]*=opacity;
  if(w.visualColor)color=*w.visualColor; color[3]*=w.visualColor?opacity:1;
  const unsigned firstCommand=unsigned(out.commands.size());
  if(w.type=="Panel"||w.type=="Button")quad(r,solid,color,clip,font);
  else if(w.type=="ProgressBar"){quad(r,solid,color,clip,font);auto fill=w.fillColor;fill[3]*=opacity;Rect bar=r;bar.w*=w.visualValue.value_or(w.value);quad(bar,solid,fill,clip,font);}
  else if(w.type=="Image"){auto texture=resolve(w.texture);if(texture.IsValid())quad(r,w.uv,color,clip,texture);else {auto fallback=Color{1,0,1,opacity};quad(r,solid,fallback,clip,font);out.errors.push_back("Missing or loading Image texture: "+w.texture);}}
  else if(w.type=="Text"){
   const FontAtlas* selected=&atlas;auto selectedTexture=font;
   if(!w.font.empty()){auto view=fonts?fonts(w.font):FontView{};if(view.atlas){selected=view.atlas;selectedTexture=view.texture;}else out.errors.push_back("Font: "+w.font+": "+(view.error.empty()?"unavailable; using Default Font":view.error));}
   auto codepoints=DecodeUTF8(w.text);float unit=w.fontSize/selected->height*scale;float lineY=r.y;
   struct ResolvedGlyph {const Glyph* glyph;float unit;RenderTextureHandle texture;};
   auto glyphFor=[&](unsigned c)->ResolvedGlyph{auto g=selected->glyphs.find(c);if(g!=selected->glyphs.end())return {&g->second,unit,selectedTexture};auto fallback=atlas.glyphs.find(c);if(fallback==atlas.glyphs.end())fallback=atlas.glyphs.find('?');return {fallback==atlas.glyphs.end()?nullptr:&fallback->second,w.fontSize/atlas.height*scale,font};};
   for(size_t start=0;start<codepoints.size();){size_t end=start;float advance=0;for(;end<codepoints.size()&&codepoints[end]!='\n';++end){if(codepoints[end]=='\r')continue;auto g=glyphFor(codepoints[end]);if(g.glyph)advance+=g.glyph->advance*g.unit;}
    float x=r.x+(w.textAlignment==1?(r.w-advance)*.5f:w.textAlignment==2?r.w-advance:0);for(size_t i=start;i<end;++i){if(codepoints[i]=='\r')continue;auto item=glyphFor(codepoints[i]);if(!item.glyph)continue;auto& g=*item.glyph;quad({x+g.pixels.x*item.unit,lineY+selected->ascent*unit+g.pixels.y*item.unit,g.pixels.w*item.unit,g.pixels.h*item.unit},g.uv,color,Intersect(clip,r),item.texture);x+=g.advance*item.unit;}
    lineY+=selected->lineHeight*unit;start=end+1;
   }
  }
  out.widgets.push_back({w.id,firstCommand,unsigned(out.commands.size())-firstCommand});
 });return out;
}
// Runtime state lives independently of the immutable resource document.
void RuntimeUI::CancelPointer(){
 hovered={};pressed={};hoverWidget.clear();pressWidget.clear();
 for(auto& [e,i]:instances)for(auto& w:i.state.widgets)w.interaction=0;
}
void RuntimeUI::Clear(){CancelPointer();pointerDown=gestureOwned=false;events.clear();previewStyles.Clear();previewInitialized=false;previewAnimation={};instances.clear();documents.clear();order.clear();world=nullptr;manager=nullptr;++session;}
void RuntimeUI::Sync(const ecs::World& source,ResourceManager& resources){
 if(world!=&source||manager!=&resources){Clear();world=&source;manager=&resources;epoch=resources.GetCacheEpoch();}
 const auto current=resources.GetCacheEpoch();if(epoch!=current){documents.clear();epoch=current;}
 std::unordered_set<ecs::Entity,ecs::EntityHash> live;order.clear();
 for(auto e:source.Entities())if(auto* c=source.GetComponent<ecs::UICanvasComponent>(e);c&&!c->document.empty()){
  live.insert(e);auto [at,added]=documents.try_emplace(c->document);
  if(added){at->second=resources.Get<UIResource>(c->document);if(!at->second)at->second=resources.Load<UIResource>(c->document);resources.WatchForHotReload<UIResource>(c->document);}
  auto asset=at->second;if(!asset||!asset->GetData().document){if(asset&&asset->HasError()&&Logger::Get().RuntimeSessionActive())Logger::Get().Diagnostic("ERROR","Runtime UI","Sync",c->document+": "+asset->GetErrorMessage());instances.erase(e);continue;}
  auto& i=instances[e];const bool replacement=i.asset!=asset||i.resource!=c->document;
  if(replacement||i.version!=asset->GetVersion()){
   auto next=*asset->GetData().document;
   if(!replacement)for(auto it=i.overrides.begin();it!=i.overrides.end();){
    auto old=std::find_if(i.state.widgets.begin(),i.state.widgets.end(),[&](auto& w){return w.id==it->first;});
    auto to=std::find_if(next.widgets.begin(),next.widgets.end(),[&](auto& w){return w.id==it->first;});
    if(old==i.state.widgets.end()||to==next.widgets.end()||old->type!=to->type){it=i.overrides.erase(it);continue;}
    auto mask=it->second;if(mask&1)to->text=old->text;if(mask&2)to->visible=old->visible;if(mask&4)to->opacity=old->opacity;
    if(mask&8)to->value=old->value;if(mask&16)to->texture=old->texture;if(mask&32)to->color=old->color;if(mask&64)to->enabled=old->enabled;++it;
   }else i.overrides.clear();
   i.animation={};i.styleDirty=true;i.state=std::move(next);i.resource=c->document;i.version=asset->GetVersion();i.asset=asset;i.serial=nextSerial++;
  }
  if(i.styles.Changed(*asset->GetData().document,resources,i.styleDirty)||i.styleDirty){for(size_t n=0;n<i.state.widgets.size();++n){auto& w=i.state.widgets[n];i.styles.Resolve(w,asset->GetData().document->widgets[n],i.overrides[w.id]);}i.styleDirty=false;}
  if(c->enabled&&i.state.enabled)order.push_back(e);
 }
 std::erase_if(instances,[&](auto& p){return !live.contains(p.first);});
 auto key=[&](ecs::Entity e){auto* id=source.GetComponent<ecs::SceneIdentityComponent>(e);auto* c=source.GetComponent<ecs::UICanvasComponent>(e);return std::tuple{std::clamp(c->sortOrder,-1000000,1000000)+instances.at(e).state.sortOrder,id?id->value:std::to_string(e.index),e.index};};
 std::sort(order.begin(),order.end(),[&](auto a,auto b){return key(a)<key(b);});
}
const Instance& RuntimeUI::Resolve(const CanvasHandle& h)const{
 if(!world||!world->IsAlive(h.entity)||h.session!=session)throw std::runtime_error("UI: stale Canvas session/entity");
 auto* c=world->GetComponent<ecs::UICanvasComponent>(h.entity);auto at=instances.find(h.entity);
 if(!c||at==instances.end()||c->document!=h.document||at->second.resource!=h.document||h.serial!=at->second.serial||h.revision!=at->second.version||!at->second.asset||at->second.asset->GetVersion()!=h.revision)throw std::runtime_error("UI: Canvas document changed; reacquire handle");
 return at->second;
}
Instance& RuntimeUI::Resolve(const CanvasHandle& h){return const_cast<Instance&>(std::as_const(*this).Resolve(h));}
bool RuntimeUI::Valid(const CanvasHandle& h)const{try{Resolve(h);return true;}catch(...){return false;}}
CanvasHandle RuntimeUI::Get(ecs::Entity e)const{
 auto at=instances.find(e);if(at==instances.end())throw std::runtime_error("UI: Canvas owner not found");
 auto& i=at->second;CanvasHandle h{e,session,i.serial,i.version,i.resource};Resolve(h);return h;
}
CanvasHandle RuntimeUI::Get(const std::string& name)const{
 CanvasHandle found;unsigned count=0;for(auto& [e,i]:instances)if(i.resource==name||i.state.name==name){found=Get(e);++count;}
 if(count!=1)throw std::runtime_error(count?"UI: ambiguous Canvas name; use owner Entity":"UI: Canvas not found");return found;
}
const Widget& RuntimeUI::Read(const CanvasHandle& h,const std::string& id)const{
 auto& i=Resolve(h);auto w=std::find_if(i.state.widgets.begin(),i.state.widgets.end(),[&](auto& v){return v.id==id;});if(w==i.state.widgets.end())throw std::runtime_error("UI: missing Widget ID: "+id);return *w;
}
void RuntimeUI::Set(const CanvasHandle& h,const std::string& id,const std::string& field,const nlohmann::json& value){
 auto& i=Resolve(h);auto& w=const_cast<Widget&>(Read(h,id));unsigned bit=0;
 auto type=[&](const char* t){if(w.type!=t)throw std::runtime_error("UI: widget type does not support "+field);};
 auto number=[&](){if(!value.is_number())throw std::runtime_error("UI: expected finite number in [0,1]");float n=value.get<float>();if(!std::isfinite(n)||n<0||n>1)throw std::runtime_error("UI: expected finite number in [0,1]");return n;};
 auto boolean=[&](){if(!value.is_boolean())throw std::runtime_error("UI: expected boolean");return value.get<bool>();};
 if(field=="text"){type("Text");auto text=value.get<std::string>();if(text.size()>16384||!ValidUTF8(text))throw std::runtime_error("UI: invalid UTF-8 or oversized Text");w.text=std::move(text);bit=1;}
 else if(field=="visible"){w.visible=boolean();bit=2;}
 else if(field=="opacity"){w.opacity=number();bit=4;}
 else if(field=="progress"){type("ProgressBar");w.value=number();bit=8;}
 else if(field=="image"){type("Image");auto key=value.get<std::string>();if(!ValidUTF8(key)||!ecs::ComponentRegistry::ValidateAssetKey(key,ecs::AssetKind::Texture))throw std::runtime_error("UI: unsafe Image texture key");w.texture=std::move(key);bit=16;}
 else if(field=="color"){if(!value.is_array()||value.size()!=4)throw std::runtime_error("UI: Color requires four channels");Color next;for(int n=0;n<4;++n){if(!value[n].is_number())throw std::runtime_error("UI: Color channel must be numeric");next[n]=value[n].get<float>();if(!std::isfinite(next[n])||next[n]<0||next[n]>1)throw std::runtime_error("UI: invalid Color channel");}w.color=next;bit=32;}
 else if(field=="enabled"){type("Button");w.enabled=boolean();bit=64;}
 else throw std::runtime_error("UI: unknown widget property");
 i.overrides[id]|=bit;
 if((field=="visible"&&!w.visible)||(field=="opacity"&&w.opacity==0)||(field=="enabled"&&!w.enabled)){
  auto affected=[&](const CanvasHandle& target,std::string cursor){
   if(target.entity!=h.entity||target.session!=h.session||target.serial!=h.serial)return false;
   for(unsigned depth=0;!cursor.empty()&&depth<64;++depth){if(cursor==id)return true;auto node=std::find_if(i.state.widgets.begin(),i.state.widgets.end(),[&](auto& v){return v.id==cursor;});if(node==i.state.widgets.end())break;cursor=node->parent;}return false;
  };
  if(affected(pressed,pressWidget))CancelPointer();
  else if(affected(hovered,hoverWidget)){const_cast<Widget&>(Read(h,hoverWidget)).interaction=0;hovered={};hoverWidget.clear();}
 }
}
void RuntimeUI::Emit(const CanvasHandle& h,const std::string& widget,const std::string& type){if(events.size()<1024)events.push_back({h,widget,type});}
bool RuntimeUI::Pointer(float x,float y,float width,float height,bool down,bool available){
 if(!available){CancelPointer();pointerDown=down;const bool consumed=gestureOwned;if(!down)gestureOwned=false;return consumed;}
 CanvasHandle target;std::string id;
 for(auto e:order){auto* canvas=world->GetComponent<ecs::UICanvasComponent>(e);if(!canvas||!canvas->enabled)continue;
  auto h=Get(e);Walk(instances.at(e).state,width,height,[&](const Widget& w,Rect r,Rect clip,float opacity){if(w.type=="Button"&&w.enabled&&opacity>0&&Contains(Intersect(r,clip),x,y)){target=h;id=w.id;}});
 }
 auto same=[](const CanvasHandle& a,const std::string& x,const CanvasHandle& b,const std::string& y){return a.entity==b.entity&&a.session==b.session&&a.serial==b.serial&&x==y;};

 if(!same(hovered,hoverWidget,target,id)){if(Valid(hovered))Emit(hovered,hoverWidget,"pointer_leave");hovered=target;hoverWidget=id;if(Valid(target))Emit(target,id,"pointer_enter");}
 // Hidden/disabled/deleted pressed widgets cannot retain pointer capture.
 bool pressLive=false;if(Valid(pressed))for(auto e:order)if(e==pressed.entity)Walk(instances.at(e).state,width,height,[&](const Widget& w,Rect r,Rect clip,float opacity){if(w.id==pressWidget&&w.enabled&&opacity>0&&Intersect(r,clip).w>0&&Intersect(r,clip).h>0)pressLive=true;});
 if(!pressLive){pressed={};pressWidget.clear();}
 if(down&&!pointerDown&&Valid(target)){pressed=target;pressWidget=id;gestureOwned=true;Emit(pressed,pressWidget,"pointer_down");}
 if(!down&&pointerDown&&Valid(pressed)){Emit(pressed,pressWidget,"pointer_up");if(same(pressed,pressWidget,target,id))Emit(pressed,pressWidget,"click");pressed={};pressWidget.clear();}
 for(auto& [e,i]:instances)for(auto& w:i.state.widgets)w.interaction=same(pressed,pressWidget,Get(e),w.id)?2:same(hovered,hoverWidget,Get(e),w.id)?1:0;
 pointerDown=down;const bool consumed=Valid(target)||gestureOwned;if(!down)gestureOwned=false;return consumed;
}
DrawList RuntimeUI::Build(const ecs::World& source,ResourceManager& resources,float width,float height,const FontAtlas& atlas,RenderTextureHandle font,const TextureResolver& textures,const FontResolver& fonts){
 Sync(source,resources);DrawList out;out.width=width;out.height=height;
 for(auto e:order){out.errors.insert(out.errors.end(),instances.at(e).styles.errors.begin(),instances.at(e).styles.errors.end());auto draw=LayoutDocument(instances.at(e).state,width,height,atlas,font,textures,fonts);unsigned offset=unsigned(out.vertices.size());if(offset+draw.vertices.size()>131072){out.errors.push_back("UI vertex budget exceeded");break;}out.vertices.insert(out.vertices.end(),draw.vertices.begin(),draw.vertices.end());for(auto command:draw.commands){command.first+=offset;out.commands.push_back(command);}out.errors.insert(out.errors.end(),draw.errors.begin(),draw.errors.end());}if(!out.errors.empty()&&Logger::Get().RuntimeSessionActive()){
  // Loading is a normal resource state, not a completed-session error.
  std::set<std::string> pending;
  for(auto e:order)for(auto& w:instances.at(e).state.widgets){
   if(w.type=="Image"&&!w.texture.empty()){auto asset=resources.Get<TextureResource>(w.texture);if(!asset||asset->IsLoading())pending.insert("Missing or loading Image texture: "+w.texture);}
   if(!w.styleAsset.empty()){auto asset=resources.Get<UIResource>(w.styleAsset);if(!asset||asset->IsLoading())pending.insert("Missing/invalid UI Style: "+w.styleAsset);}
  }
  for(auto& error:out.errors)if(!pending.contains(error)&&!error.starts_with("Font: "))Logger::Get().Diagnostic("WARN","Runtime UI","Build",error);
 }return out;
}
}
