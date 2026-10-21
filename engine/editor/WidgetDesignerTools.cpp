#include "WidgetDesignerTools.h"
#include <set>
#include <cmath>
#include <stdexcept>
namespace editor::widgets {
const ui::Widget& Find(const ui::Document& d,const std::string& id){
 auto found=std::find_if(d.widgets.begin(),d.widgets.end(),[&](auto& w){return w.id==id;});
 if(found==d.widgets.end())throw std::runtime_error("Widget no longer exists: "+id);return *found;
}
ui::Rect Canvas(const ui::Document& d,ui::Pair preview){float s=std::min(preview[0]/d.referenceResolution[0],preview[1]/d.referenceResolution[1]);return {0,0,preview[0]/s,preview[1]/s};}
ui::Rect Rectangle(const ui::Document& d,const std::string& id,ui::Rect canvas){
 auto bounds=ui::ComputeLayout(d,canvas);if(!bounds.contains(id))throw std::runtime_error("Invalid Widget layout");return bounds.at(id);
}
ui::Rect ParentRectangle(const ui::Document& d,const ui::Widget& w,ui::Rect canvas){return w.parent.empty()?canvas:Rectangle(d,w.parent,canvas);}
// Inverse of UI.1 LayoutRect. SizeDelta includes the anchor span; pivot is retained.
void SetRectangle(ui::Widget& w,ui::Rect r,ui::Rect p){
 float starts[]{r.x,r.y},sizes[]{std::max(0.f,r.w),std::max(0.f,r.h)},parentStarts[]{p.x,p.y},parentSizes[]{p.w,p.h};
 for(int a=0;a<2;++a){float span=parentSizes[a]*(w.anchorMax[a]-w.anchorMin[a]);w.size[a]=sizes[a]-span;w.position[a]=starts[a]-parentStarts[a]-parentSizes[a]*w.anchorMin[a]-span*w.pivot[a]+sizes[a]*w.pivot[a];}
}
static void CheckDocument(const ui::Document& d){auto error=ui::Validate(d);if(!error.empty())throw std::runtime_error(error);}
void Reparent(ui::Document& d,const std::string& id,const std::string& parent,ui::Rect canvas){
 if(id==d.root)throw std::runtime_error("Root cannot be reparented");if((Find(d,parent).type!="Panel"&&Find(d,parent).type!="Button"))throw std::runtime_error("Only Panel or Button can contain children");
 for(std::string p=parent;!p.empty();p=Find(d,p).parent)if(p==id)throw std::runtime_error("Reparent would create a cycle");
 auto r=Rectangle(d,id,canvas),p=Rectangle(d,parent,canvas);auto next=d;auto& w=const_cast<ui::Widget&>(Find(next,id));w.parent=parent;SetRectangle(w,r,p);CheckDocument(next);d=std::move(next);
}
void Rename(ui::Document& d,const std::string& id,const std::string& name){
 if(name==id)return;if(std::any_of(d.widgets.begin(),d.widgets.end(),[&](auto& w){return w.id==name;}))throw std::runtime_error("Widget ID already exists");
 auto next=d;const_cast<ui::Widget&>(Find(next,id)).id=name;for(auto& w:next.widgets)if(w.parent==id)w.parent=name;if(next.root==id)next.root=name;for(auto& clip:next.animations)if(clip.widget==id)clip.widget=name;for(auto& required:next.requiredWidgets)if(required==id)required=name;CheckDocument(next);d=std::move(next);
}
static std::set<std::string> Subtree(const ui::Document& d,const std::string& id){Find(d,id);std::set<std::string> ids{id};for(bool more=true;more;){more=false;for(auto& w:d.widgets)if(ids.contains(w.parent)&&ids.insert(w.id).second)more=true;}return ids;}
std::string Duplicate(ui::Document& d,const std::string& id){
 if(id==d.root)throw std::runtime_error("Duplicate a child of Root");auto ids=Subtree(d,id);auto next=d;std::map<std::string,std::string> names;
 for(auto& old:ids){auto name=old+"_Copy";auto used=[&](){return std::any_of(next.widgets.begin(),next.widgets.end(),[&](auto& w){return w.id==name;})||std::any_of(names.begin(),names.end(),[&](auto& n){return n.second==name;});};while(used())name+="_";names[old]=name;}
 for(auto& w:d.widgets)if(ids.contains(w.id)){auto copy=w;copy.id=names[w.id];if(names.contains(copy.parent))copy.parent=names[copy.parent];next.widgets.push_back(std::move(copy));}CheckDocument(next);d=std::move(next);return names[id];
}
void Delete(ui::Document& d,const std::string& id){auto ids=Subtree(d,id);std::erase_if(d.widgets,[&](auto& w){return ids.contains(w.id);});std::erase_if(d.animations,[&](auto& c){return ids.contains(c.widget);});std::erase_if(d.requiredWidgets,[&](auto& id){return ids.contains(id);});if(ids.contains(d.root))d.root.clear();}
void Preset(ui::Document& d,const std::string& id,int anchor,int pivot,ui::Rect canvas){
 auto r=Rectangle(d,id,canvas);auto next=d;auto& w=const_cast<ui::Widget&>(Find(next,id));auto parent=ParentRectangle(d,w,canvas);
 if(anchor>=0&&anchor<12){if(anchor<9){w.anchorMin={float(anchor%3)*.5f,float(anchor/3)*.5f};w.anchorMax=w.anchorMin;}else{w.anchorMin={anchor==10?.5f:0,anchor==9?.5f:0};w.anchorMax={anchor==10?.5f:1,anchor==9?.5f:1};}}
 if(pivot>=0&&pivot<9)w.pivot={float(pivot%3)*.5f,float(pivot/3)*.5f};SetRectangle(w,r,parent);CheckDocument(next);d=std::move(next);
}
ui::Rect Drag(ui::Rect r,float dx,float dy,int handle,float grid){
 auto snap=[&](float f){return grid>0?std::round(f/grid)*grid:f;};
 if(handle<0){r.x=snap(r.x+dx);r.y=snap(r.y+dy);return r;}
 // Clockwise: TL, T, TR, R, BR, B, BL, L.
 float left=r.x,top=r.y,right=r.x+r.w,bottom=r.y+r.h;
 if(handle==0||handle==6||handle==7)left=std::min(right,snap(left+dx));
 if(handle==2||handle==3||handle==4)right=std::max(left,snap(right+dx));
 if(handle==0||handle==1||handle==2)top=std::min(bottom,snap(top+dy));
 if(handle==4||handle==5||handle==6)bottom=std::max(top,snap(bottom+dy));
 return {left,top,right-left,bottom-top};
}
std::string Pick(const ui::DrawList& draw,float x,float y){
 for(auto it=draw.widgets.rbegin();it!=draw.widgets.rend();++it)for(unsigned c=it->first;c<it->first+it->count;++c){auto& cmd=draw.commands[c];auto& a=draw.vertices[cmd.first];auto& b=draw.vertices[cmd.first+2];auto r=ui::Intersect({a.x,a.y,b.x-a.x,b.y-a.y},cmd.clip);if(x>=r.x&&y>=r.y&&x<r.x+r.w&&y<r.y+r.h)return it->id;}return {};
}
}
