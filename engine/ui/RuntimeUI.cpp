#include "RuntimeUI.h"
#include "../ecs/components/UICanvasComponent.h"
#include "../ecs/components/SceneIdentityComponent.h"
#include "../resources/ResourceManager.h"
#include <set>
#include <tuple>
namespace ui {
Rect Intersect(Rect a,Rect b){float x=std::max(a.x,b.x),y=std::max(a.y,b.y);return {x,y,std::max(0.f,std::min(a.x+a.w,b.x+b.w)-x),std::max(0.f,std::min(a.y+a.h,b.y+b.h)-y)};}
Rect LayoutRect(const Widget& w,Rect parent){Rect rect;float* start[]={&rect.x,&rect.y};float* extent[]={&rect.w,&rect.h};const float pstart[]={parent.x,parent.y},psize[]={parent.w,parent.h};for(int a=0;a<2;++a){float span=psize[a]*(w.anchorMax[a]-w.anchorMin[a]);*extent[a]=std::max(0.f,span+w.size[a]);*start[a]=pstart[a]+psize[a]*w.anchorMin[a]+span*w.pivot[a]+w.position[a]-*extent[a]*w.pivot[a];}return rect;}
DrawList LayoutDocument(const Document& document,float width,float height,const FontAtlas& atlas,RenderTextureHandle font,const TextureResolver& resolve){
 DrawList out;out.width=width;out.height=height;auto error=Validate(document);if(!error.empty()){out.errors.push_back(error);return out;}if(!document.enabled||width<1||height<1||document.widgets.empty())return out;
 const float scale=std::min(width/document.referenceResolution[0],height/document.referenceResolution[1]);
 const Rect screen{0,0,width,height};const std::array<float,4> solid{.5f/atlas.texture.width,.5f/atlas.texture.height,.5f/atlas.texture.width,.5f/atlas.texture.height};
 std::map<std::string,const Widget*> widgets;std::map<std::string,std::vector<const Widget*>> children;
 for(auto& w:document.widgets){widgets[w.id]=&w;children[w.parent].push_back(&w);}for(auto& [id,list]:children)std::sort(list.begin(),list.end(),[](auto a,auto b){return std::tie(a->zOrder,a->id)<std::tie(b->zOrder,b->id);});
 auto quad=[&](Rect r,const std::array<float,4>& uv,Color color,Rect clip,RenderTextureHandle texture){if(r.w<=0||r.h<=0||clip.w<=0||clip.h<=0||color[3]<=0||out.vertices.size()+6>131072)return;if(Intersect(r,clip).w<=0||Intersect(r,clip).h<=0)return;unsigned first=unsigned(out.vertices.size());out.vertices.insert(out.vertices.end(),{{r.x,r.y,uv[0],uv[1],color},{r.x+r.w,r.y,uv[2],uv[1],color},{r.x+r.w,r.y+r.h,uv[2],uv[3],color},{r.x,r.y,uv[0],uv[1],color},{r.x+r.w,r.y+r.h,uv[2],uv[3],color},{r.x,r.y+r.h,uv[0],uv[3],color}});out.commands.push_back({first,6,clip,texture});};
 auto visit=[&](auto&& self,const Widget& w,Rect parent,Rect clip,float opacity)->void{
  if(!w.visible)return;Rect logical=LayoutRect(w,parent);Rect r{logical.x*scale,logical.y*scale,logical.w*scale,logical.h*scale};opacity*=w.opacity;auto color=w.color;color[3]*=opacity;
  const unsigned firstCommand=unsigned(out.commands.size());
  if(w.type=="Panel")quad(r,solid,color,clip,font);
  else if(w.type=="ProgressBar"){quad(r,solid,color,clip,font);auto fill=w.fillColor;fill[3]*=opacity;Rect bar=r;bar.w*=w.value;quad(bar,solid,fill,clip,font);}
  else if(w.type=="Image"){auto texture=resolve(w.texture);if(texture.IsValid())quad(r,w.uv,color,clip,texture);else {auto fallback=Color{1,0,1,opacity};quad(r,solid,fallback,clip,font);out.errors.push_back("Missing or loading Image texture: "+w.texture);}}
  else if(w.type=="Text"){
   auto codepoints=DecodeUTF8(w.text);float unit=w.fontSize/atlas.height*scale;float lineY=r.y;
   for(size_t start=0;start<codepoints.size();){size_t end=start;float advance=0;for(;end<codepoints.size()&&codepoints[end]!='\n';++end){if(codepoints[end]=='\r')continue;auto glyph=atlas.glyphs.find(codepoints[end]);if(glyph==atlas.glyphs.end())glyph=atlas.glyphs.find('?');if(glyph!=atlas.glyphs.end())advance+=glyph->second.advance*unit;}
    float x=r.x+(w.textAlignment==1?(r.w-advance)*.5f:w.textAlignment==2?r.w-advance:0);for(size_t i=start;i<end;++i){if(codepoints[i]=='\r')continue;auto glyph=atlas.glyphs.find(codepoints[i]);if(glyph==atlas.glyphs.end())glyph=atlas.glyphs.find('?');if(glyph==atlas.glyphs.end())continue;auto& g=glyph->second;quad({x+g.pixels.x*unit,lineY+(atlas.ascent+g.pixels.y)*unit,g.pixels.w*unit,g.pixels.h*unit},g.uv,color,Intersect(clip,r),font);x+=g.advance*unit;}
    lineY+=atlas.lineHeight*unit;start=end+1;
   }
  }
  out.widgets.push_back({w.id,firstCommand,unsigned(out.commands.size())-firstCommand});
  auto childClip=w.clipChildren?Intersect(clip,r):clip;for(auto* child:children[w.id])self(self,*child,logical,childClip,opacity);
 };
 visit(visit,*widgets[document.root],{0,0,width/scale,height/scale},screen,1);return out;
}
DrawList RuntimeUI::Build(const ecs::World& world,ResourceManager& resources,float width,float height,const FontAtlas& atlas,RenderTextureHandle font,const TextureResolver& textures){
 DrawList out;out.width=width;out.height=height;struct Canvas {ecs::Entity entity;std::string stable;int order;};std::vector<Canvas> canvases;std::unordered_set<ecs::Entity,ecs::EntityHash> live;
 for(auto e:world.Entities())if(auto* c=world.GetComponent<ecs::UICanvasComponent>(e);c&&c->enabled&&!c->document.empty()){
  live.insert(e);auto asset=resources.Get<UIResource>(c->document);if(!asset){asset=resources.Load<UIResource>(c->document);resources.WatchForHotReload<UIResource>(c->document);}auto& instance=instances[e];auto definition=asset->GetData().document;if(!definition){out.errors.push_back(asset->GetErrorMessage());continue;}
  if(instance.resource!=c->document||instance.version!=asset->GetVersion()){instance.resource=c->document;instance.version=asset->GetVersion();instance.state=*definition;}
  auto* id=world.GetComponent<ecs::SceneIdentityComponent>(e);canvases.push_back({e,id?id->value:std::to_string(e.index),std::clamp(c->sortOrder,-1000000,1000000)+instance.state.sortOrder});
 }
 std::erase_if(instances,[&](auto& pair){return !live.contains(pair.first);});std::sort(canvases.begin(),canvases.end(),[](auto& a,auto& b){return std::tie(a.order,a.stable,a.entity.index)<std::tie(b.order,b.stable,b.entity.index);});
 for(auto& canvas:canvases){auto draw=LayoutDocument(instances.at(canvas.entity).state,width,height,atlas,font,textures);unsigned offset=unsigned(out.vertices.size());if(offset+draw.vertices.size()>131072){out.errors.push_back("UI vertex budget exceeded");break;}out.vertices.insert(out.vertices.end(),draw.vertices.begin(),draw.vertices.end());for(auto command:draw.commands){command.first+=offset;out.commands.push_back(command);}out.errors.insert(out.errors.end(),draw.errors.begin(),draw.errors.end());}return out;
}
}
