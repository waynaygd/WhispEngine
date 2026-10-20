#include "RuntimeUI.h"
#include <algorithm>
namespace ui {
bool ManagedByLayout(const Document& d,const Widget& w){if(w.ignoreLayout)return false;for(auto& p:d.widgets)if(p.id==w.parent)return p.layout!=0;return false;}
std::map<std::string,Rect> ComputeLayout(const Document& d,Rect canvas){
 std::map<std::string,Rect> out;if(d.widgets.empty()||!Validate(d).empty())return out;
 std::map<std::string,std::vector<const Widget*>> children;std::map<std::string,Pair> measured;const Widget* root=nullptr;
 for(auto& w:d.widgets){children[w.parent].push_back(&w);if(w.id==d.root)root=&w;}
 auto measure=[&](auto&& self,const Widget& w)->Pair{
  auto size=Pair{std::max(0.f,w.size[0]),std::max(0.f,w.size[1])};Pair content{0,0};unsigned count=0;
  for(auto* c:children[w.id]){auto v=self(self,*c);if(!c->visible||c->ignoreLayout)continue;++count;
   if(w.layout==1){content[0]=std::max(content[0],v[0]);content[1]+=v[1];}else if(w.layout==2){content[0]+=v[0];content[1]=std::max(content[1],v[1]);}else{content[0]=std::max(content[0],v[0]);content[1]=std::max(content[1],v[1]);}}
  if(count>1&&(w.layout==1||w.layout==2))content[w.layout==1?1:0]+=float(count-1)*w.spacing;
  for(int a=0;a<2;++a)if(w.layout&&w.autoSize[a])size[a]=content[a]+w.padding[a]+w.padding[a+2];return measured[w.id]=size;
 };measure(measure,*root);
 auto arrange=[&](auto&& self,const Widget& w,Rect r,Pair inheritedOffset,Pair inheritedScale)->void{
  Pair origin{r.x+r.w*w.pivot[0],r.y+r.h*w.pivot[1]};
  Pair factor{std::clamp(inheritedScale[0]*w.visualScale[0],0.f,10000.f),std::clamp(inheritedScale[1]*w.visualScale[1],0.f,10000.f)};
  Pair offset{inheritedOffset[0]+inheritedScale[0]*(origin[0]*(1-w.visualScale[0])+w.visualOffset[0]),inheritedOffset[1]+inheritedScale[1]*(origin[1]*(1-w.visualScale[1])+w.visualOffset[1])};
  out[w.id]={r.x*factor[0]+offset[0],r.y*factor[1]+offset[1],r.w*factor[0],r.h*factor[1]};
  Rect inner{r.x+w.padding[0],r.y+w.padding[1],std::max(0.f,r.w-w.padding[0]-w.padding[2]),std::max(0.f,r.h-w.padding[1]-w.padding[3])};
  auto flow=children[w.id];std::erase_if(flow,[](auto* c){return !c->visible||c->ignoreLayout;});if(w.reverse)std::reverse(flow.begin(),flow.end());
  const int main=w.layout==1?1:0;float extent=main?inner.h:inner.w,total=flow.size()>1?float(flow.size()-1)*w.spacing:0;unsigned flexible=0;
  if(w.layout==1||w.layout==2)for(auto* c:flow){if(c->stretch[main])++flexible;else total+=measured[c->id][main];}
  float share=flexible?std::max(0.f,extent-total)/flexible:0;float cursor=(main?inner.y:inner.x)+std::max(0.f,extent-total-share*flexible)*w.alignment[main];
  std::map<std::string,Rect> slots;
  if(w.layout)for(auto* c:flow){auto sz=measured[c->id];for(int a=0;a<2;++a){float avail=a?inner.h:inner.w;if(w.layout==3){if(w.stretchChildren||c->stretch[a])sz[a]=avail;}else if(a==main){if(c->stretch[a])sz[a]=share;}else if(w.stretchChildren||c->stretch[a])sz[a]=avail;}
   Rect slot{inner.x+std::max(0.f,inner.w-sz[0])*w.alignment[0],inner.y+std::max(0.f,inner.h-sz[1])*w.alignment[1],sz[0],sz[1]};
   if(w.layout==1){slot.y=cursor;cursor+=sz[1]+w.spacing;}else if(w.layout==2){slot.x=cursor;cursor+=sz[0]+w.spacing;}slots[c->id]=slot;
  }
  for(auto* c:children[w.id]){auto cr=LayoutRect(*c,r);for(int a=0;a<2;++a)if(c->layout&&c->autoSize[a]){float old=a?cr.h:cr.w,next=measured[c->id][a];if(a){cr.y+=(old-next)*c->pivot[a];cr.h=next;}else{cr.x+=(old-next)*c->pivot[a];cr.w=next;}}
   if(slots.contains(c->id))cr=slots.at(c->id);self(self,*c,cr,offset,factor);}
 };
 auto r=LayoutRect(*root,canvas);if(root->layout&&root->autoSize[0])r.w=measured[root->id][0];if(root->layout&&root->autoSize[1])r.h=measured[root->id][1];arrange(arrange,*root,r,{0,0},{1,1});return out;
}
}
