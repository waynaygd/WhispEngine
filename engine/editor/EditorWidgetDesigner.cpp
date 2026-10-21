#include "EditorLayer.h"
#include "EditorWidgets.h"
#include "WidgetDesignerTools.h"
#include "../core/Application.h"
#include "../ecs/components/SceneIdentityComponent.h"
#include "../resources/ResourceManager.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <cmath>
#include <set>
#include <tuple>
#include <cctype>

namespace editor {
namespace {
constexpr const char* Anchors[]{"Top Left","Top Center","Top Right","Middle Left","Center","Middle Right","Bottom Left","Bottom Center","Bottom Right","Stretch Horizontal","Stretch Vertical","Stretch Both"};
ImU32 Tint(const ui::Color& c){return ImGui::ColorConvertFloat4ToU32({c[0],c[1],c[2],c[3]});}
bool Contains(ui::Rect r,ImVec2 p){return p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.w&&p.y<r.y+r.h;}
}
const ui::Document& EditorLayer::GetUIDraft(Application& app,const std::string& key){return UIDocumentSession(app,key).draft;}
bool EditorLayer::CancelUIDocumentEdit(Application& app,const std::string& key){
 try{auto& s=UIDocumentSession(app,key);if(!s.pending)return true;auto original=s.pending->get<ui::Document>();s.pending.reset();s.draft=original;
  s.dirty=nlohmann::json(original)!=s.disk;auto* resources=app.GetResourceManager();auto asset=resources->Get<UIResource>(key);if(asset)asset->ReplaceData(UIResource{std::make_shared<ui::Document>(original)},ResourceLoadState::Loaded,false);resources->SetUIDirty(key,s.dirty);return true;
 }catch(const std::exception& e){m_EditorError=e.what();return false;}
}
bool EditorLayer::OpenUIDesigner(Application& app,const std::string& key){
 try{SyncSceneDocument(app);UIDocumentSession(app,key);if(m_UIWidgetDrag){CancelUIDocumentEdit(app,m_UIWidgetDrag->key);m_UIWidgetDrag.reset();}
  if(!m_DesignDocument.empty()){auto& old=UIDocumentSession(app,m_DesignDocument);if(old.pending)EditUIDocument(app,m_DesignDocument,old.draft,true);old.preview=m_UIPreviewSize;old.zoom=m_UIZoom;old.panX=m_UIPan.x;old.panY=m_UIPan.y;old.fit=m_UIFit;old.viewSaved=true;}
  CaptureCurrentWorkspace();m_ActiveDocument=RegisterDocument(app,DocumentType::UI,key).id;
  m_DesignDocument=key;m_WidgetRenameId.clear();auto& view=UIDocumentSession(app,key);m_UIPreviewSize=view.preview;m_UIZoom=view.zoom;m_UIPan={view.panX,view.panY};m_UIFit=view.viewSaved?view.fit:true;m_ShowCanvasDesigner=m_ShowWidgetHierarchy=m_ShowWidgetInspector=true;
  if(std::find(m_UIDesignerKeys.begin(),m_UIDesignerKeys.end(),key)==m_UIDesignerKeys.end())m_UIDesignerKeys.push_back(key);
  if(auto saved=m_Workspaces.find("ui");saved!=m_Workspaces.end())for(auto* panel:{"Canvas Designer","Widget Hierarchy","Widget Inspector"})saved->second.visible[panel]=true;
  if(m_Workspace!=EditorWorkspace::UIDesigner)SwitchWorkspace(EditorWorkspace::UIDesigner);return true;
 }catch(const std::exception& e){m_EditorError=e.what();return false;}
}
bool EditorLayer::UIDesignerOwnsKeyboard()const{
 auto* focused=ImGui::GetCurrentContext()->NavWindow;if(!focused)return false;
 for(auto* name:{"Canvas Designer","Widget Hierarchy","Widget Inspector"})if(auto* window=ImGui::FindWindowByName(name);window&&focused->RootWindow==window->RootWindow)return true;return false;
}
bool EditorLayer::WidgetCommand(Application& app,const std::string& command,const std::string& argument){
 try{if(app.IsEditorPlayMode()||m_DesignDocument.empty()||m_UIWidgetDrag)return false;auto& s=UIDocumentSession(app,m_DesignDocument);if(s.pending)EditUIDocument(app,m_DesignDocument,s.draft,true);auto next=s.draft;auto id=s.selected;auto selected=id;
  if(command=="Add"){
   if(next.widgets.empty()){auto root=ui::EmptyDocument().widgets[0];next.root=root.id;next.widgets.push_back(root);}ui::Widget w;w.id=argument;w.type=argument;w.position={16,16};w.text=argument=="Text"?"Text":"";
   while(std::any_of(next.widgets.begin(),next.widgets.end(),[&](auto& n){return n.id==w.id;}))w.id+="_";
   auto p=std::find_if(next.widgets.begin(),next.widgets.end(),[&](auto& n){return n.id==id&&(n.type=="Panel"||n.type=="Button");});w.parent=p==next.widgets.end()?next.root:p->id;selected=w.id;if(w.type=="Button")w.color={.15f,.25f,.4f,1};next.widgets.push_back(w);
   if(w.type=="Button"){ui::Widget text;text.id=w.id+"_Label";while(std::any_of(next.widgets.begin(),next.widgets.end(),[&](auto& n){return n.id==text.id;}))text.id+="_";text.type="Text";text.parent=w.id;text.anchorMax={1,0};text.position={0,16};text.size={0,36};text.text="Button";text.textAlignment=1;next.widgets.push_back(text);}
  }else if(command=="Delete"){widgets::Delete(next,id);selected=next.root;}
  else if(command=="Duplicate")selected=widgets::Duplicate(next,id);
  else if(command=="Rename"){widgets::Rename(next,id,argument);selected=argument;}
  else if(command=="Reparent")widgets::Reparent(next,id,argument,widgets::Canvas(next,m_UIPreviewSize));
  else if(command=="Anchor"||command=="Pivot"){if(ui::ManagedByLayout(next,widgets::Find(next,id)))throw std::runtime_error("Parent Layout controls this Widget; enable Ignore Parent Layout first");widgets::Preset(next,id,command=="Anchor"?std::stoi(argument):-1,command=="Pivot"?std::stoi(argument):-1,widgets::Canvas(next,m_UIPreviewSize));}
  else if(command=="Earlier"||command=="Later"){
   auto parent=widgets::Find(next,id).parent;std::vector<ui::Widget*> siblings;for(auto& w:next.widgets)if(w.parent==parent)siblings.push_back(&w);
   std::sort(siblings.begin(),siblings.end(),[](auto a,auto b){return std::tie(a->zOrder,a->id)<std::tie(b->zOrder,b->id);});auto it=std::find_if(siblings.begin(),siblings.end(),[&](auto* w){return w->id==id;});auto index=int(it-siblings.begin()),target=index+(command=="Earlier"?-1:1);if(target<0||target>=int(siblings.size()))return false;std::swap(siblings[index],siblings[target]);for(int i=0;i<int(siblings.size());++i)siblings[i]->zOrder=i;
  }else return false;
  if(!EditUIDocument(app,m_DesignDocument,next))return false;s.selected=selected;m_EditorError.clear();return true;
 }catch(const std::exception& e){m_EditorError=e.what();return false;}
}
void EditorLayer::DrawUIDesigner(Application& app){
 if(m_UIWidgetDrag&&(!m_ShowCanvasDesigner||app.IsEditorPlayMode()||ImGui::GetIO().AppFocusLost)){CancelUIDocumentEdit(app,m_UIWidgetDrag->key);m_UIWidgetDrag.reset();}
 auto newDocument=[&](){auto key="ui/Canvas-"+ecs::NewSceneIdentity()+".ui.json";if(UILoader::Save(key,ui::EmptyDocument(),&m_EditorError)){m_AssetListsDirty=true;OpenUIDesigner(app,key);}};
 if(m_ShowCanvasDesigner){
  PrepareWorkspacePrimaryWindow();
  if(ImGui::Begin("Canvas Designer",&m_ShowCanvasDesigner,m_WorkspaceFocus?ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings:0)){
   ImGui::BeginDisabled(app.IsEditorPlayMode());if(ImGui::Button("New UI Document"))newDocument();ImGui::EndDisabled();ImGui::SameLine();
   if(ImGui::BeginCombo("Document",m_DesignDocument.empty()?"Choose .ui.json":m_DesignDocument.c_str())){
    std::set<std::string> keys(m_UIDesignerKeys.begin(),m_UIDesignerKeys.end());for(auto& e:m_AssetCatalog.Entries())if(e.key.ends_with(".ui.json"))keys.insert(e.key);
    for(auto& key:keys)if(ImGui::Selectable(key.c_str(),key==m_DesignDocument))OpenUIDesigner(app,key);ImGui::EndCombo();
   }
   std::string dropped;if(AcceptAssetDrop(ecs::AssetKind::UIDocument,dropped))OpenUIDesigner(app,dropped);
   if(app.IsEditorPlayMode())ImGui::TextWrapped("Preview only during Play. Stop Play to edit authored UI.");
   if(!m_DesignDocument.empty())try{
    auto& s=UIDocumentSession(app,m_DesignDocument);ImGui::TextUnformatted(s.dirty?"Unsaved UI draft (Save UI / Ctrl+S)":"UI document saved");
    ImGui::BeginDisabled(bool(m_UIWidgetDrag));bool optionsRequested=false;
    auto previewLabel="Preview: "+std::to_string(int(m_UIPreviewSize[0]))+" x "+std::to_string(int(m_UIPreviewSize[1]));
    ImGui::SetNextItemWidth(210);
    if(ImGui::BeginCombo("##Preview Resolution",previewLabel.c_str())){
     for(auto size:{ui::Pair{1920,1080},ui::Pair{1600,900},ui::Pair{1280,720},ui::Pair{2560,1440},ui::Pair{1080,1920}})if(ImGui::Selectable((std::to_string(int(size[0]))+" x "+std::to_string(int(size[1]))).c_str())){m_UIPreviewSize=size;m_UIFit=true;}
     if(ImGui::Selectable("Custom Resolution..."))optionsRequested=true;ImGui::EndCombo();
    }
    ImGui::SameLine();if(ImGui::Button("Fit"))m_UIFit=true;ImGui::SameLine();ImGui::SetNextItemWidth(115);ImGui::SliderFloat("Zoom",&m_UIZoom,.02f,4,"%.2fx");
    ImGui::SameLine();if(ImGui::Button("Canvas Options"))optionsRequested=true;if(optionsRequested)ImGui::OpenPopup("Canvas Options");
    if(ImGui::BeginPopup("Canvas Options")){
     ImGui::TextUnformatted("Preview only; Reference Resolution is in Inspector.");
     if(ImGui::DragFloat2("Custom Preview",m_UIPreviewSize.data(),1,64,8192,"%.0f",ImGuiSliderFlags_AlwaysClamp))m_UIFit=true;
     ImGui::Checkbox("Show Grid",&m_UIGrid);ImGui::Checkbox("Snap to Grid",&m_UISnap);ImGui::DragFloat("Grid Step",&m_UIGridSize,1,1,512,"%.0f",ImGuiSliderFlags_AlwaysClamp);ImGui::EndPopup();
    }
    ImGui::EndDisabled();ImGui::TextUnformatted("Wheel: zoom | MMB: pan | Alt: bypass snap | Escape: cancel drag");
    ImGui::BeginChild("Canvas Surface",{0,0},ImGuiChildFlags_Borders,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    struct EndCanvasSurface {~EndCanvasSurface(){ImGui::EndChild();}} endCanvasSurface;
    auto area=ImGui::GetContentRegionAvail();area.x=std::max(1.f,area.x);area.y=std::max(1.f,area.y);auto start=ImGui::GetCursorScreenPos();
    if(m_UIFit){m_UIZoom=std::clamp(std::min((area.x-32)/m_UIPreviewSize[0],(area.y-32)/m_UIPreviewSize[1]),.02f,4.f);m_UIPan={(area.x-m_UIPreviewSize[0]*m_UIZoom)*.5f,(area.y-m_UIPreviewSize[1]*m_UIZoom)*.5f};m_UIFit=false;}
    ImGui::InvisibleButton("UI Canvas",area,ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonMiddle);auto& io=ImGui::GetIO();bool hovered=ImGui::IsItemHovered();
    if(hovered&&!m_UIWidgetDrag){if(io.MouseWheel!=0){float old=m_UIZoom;m_UIZoom=std::clamp(old*std::pow(1.15f,io.MouseWheel),.02f,4.f);float ratio=m_UIZoom/old;m_UIPan={io.MousePos.x-start.x-(io.MousePos.x-start.x-m_UIPan.x)*ratio,io.MousePos.y-start.y-(io.MousePos.y-start.y-m_UIPan.y)*ratio};}if(ImGui::IsMouseDragging(ImGuiMouseButton_Middle,0)){m_UIPan.x+=io.MouseDelta.x;m_UIPan.y+=io.MouseDelta.y;}}
    m_UICanvasOrigin={start.x+m_UIPan.x,start.y+m_UIPan.y};auto toScreen=[&](float x,float y){return ImVec2{m_UICanvasOrigin.x+x*m_UIZoom,m_UICanvasOrigin.y+y*m_UIZoom};};
    app.GetRuntimeUI().PreviewAdvance(io.DeltaTime);auto draw=app.BuildUIDesignerPreview(s.draft,m_UIPreviewSize);const auto& displayed=app.GetRuntimeUI().Preview(s.draft,*app.GetResourceManager());auto* list=ImGui::GetWindowDrawList();list->PushClipRect(start,{start.x+area.x,start.y+area.y},true);
    auto end=toScreen(m_UIPreviewSize[0],m_UIPreviewSize[1]);list->AddRectFilled({m_UICanvasOrigin.x,m_UICanvasOrigin.y},end,IM_COL32(35,37,41,255));
    // Present the exact runtime quads/UVs/clip rects with the editor's existing GPU backend.
    // No widgets, text layout, font rasterization or shaders are implemented here.
    for(auto& cmd:draw.commands){auto texture=app.GetRenderAdapter()->GetTexturePreviewId(cmd.texture);if(!texture)continue;auto& a=draw.vertices[cmd.first];auto& b=draw.vertices[cmd.first+1];auto& c=draw.vertices[cmd.first+2];auto& d=draw.vertices[cmd.first+5];
     list->PushClipRect(toScreen(cmd.clip.x,cmd.clip.y),toScreen(cmd.clip.x+cmd.clip.w,cmd.clip.y+cmd.clip.h),true);
     list->AddImageQuad((ImTextureID)texture,toScreen(a.x,a.y),toScreen(b.x,b.y),toScreen(c.x,c.y),toScreen(d.x,d.y),{a.u,a.v},{b.u,b.v},{c.u,c.v},{d.u,d.v},Tint(a.color));list->PopClipRect();
    }
    const float runtimeScale=std::min(m_UIPreviewSize[0]/s.draft.referenceResolution[0],m_UIPreviewSize[1]/s.draft.referenceResolution[1]);const float logicalScale=runtimeScale*m_UIZoom;
    auto logicalScreen=[&](float x,float y){return toScreen(x*runtimeScale,y*runtimeScale);};auto canvas=widgets::Canvas(displayed,m_UIPreviewSize);
    if(m_UIGrid&&m_UIGridSize*logicalScale>=5){for(float x=0;x<=canvas.w;x+=m_UIGridSize)list->AddLine(logicalScreen(x,0),logicalScreen(x,canvas.h),IM_COL32(160,170,180,35));for(float y=0;y<=canvas.h;y+=m_UIGridSize)list->AddLine(logicalScreen(0,y),logicalScreen(canvas.w,y),IM_COL32(160,170,180,35));}
    list->AddRect({m_UICanvasOrigin.x,m_UICanvasOrigin.y},end,IM_COL32(160,170,180,255));list->AddRect({m_UICanvasOrigin.x,m_UICanvasOrigin.y},logicalScreen(s.draft.referenceResolution[0],s.draft.referenceResolution[1]),IM_COL32(90,140,210,130));
    auto selected=std::find_if(s.draft.widgets.begin(),s.draft.widgets.end(),[&](auto& w){return w.id==s.selected;});int hoveredHandle=-1;
    if(selected!=s.draft.widgets.end()){
     auto r=widgets::Rectangle(displayed,selected->id,canvas);auto p=widgets::ParentRectangle(displayed,*selected,canvas);auto lo=logicalScreen(r.x,r.y),hi=logicalScreen(r.x+r.w,r.y+r.h);list->AddRect(lo,hi,IM_COL32(255,190,50,255),0.f,ImDrawFlags_None,2.f);
     float hx[]{0,.5f,1,1,1,.5f,0,0},hy[]{0,0,0,.5f,1,1,1,.5f};
     for(int i=0;i<8;++i){auto point=logicalScreen(r.x+r.w*hx[i],r.y+r.h*hy[i]);list->AddRectFilled({point.x-4,point.y-4},{point.x+4,point.y+4},IM_COL32(255,190,50,255));if(Contains({point.x-6,point.y-6,12,12},io.MousePos))hoveredHandle=i;}
     auto pivot=logicalScreen(r.x+r.w*selected->pivot[0],r.y+r.h*selected->pivot[1]);list->AddCircle(pivot,5,IM_COL32(255,80,130,255),12,2);
     for(auto anchor:{selected->anchorMin,selected->anchorMax}){auto a=logicalScreen(p.x+p.w*anchor[0],p.y+p.h*anchor[1]);list->AddCircle(a,6,IM_COL32(70,220,240,255),4,2);}
    }
    list->PopClipRect();
    if(hovered&&ImGui::IsMouseClicked(0)&&!app.IsEditorPlayMode()){
     if(hoveredHandle<0)s.selected=widgets::Pick(draw,(io.MousePos.x-m_UICanvasOrigin.x)/m_UIZoom,(io.MousePos.y-m_UICanvasOrigin.y)/m_UIZoom);
     app.GetRuntimeUI().PreviewStop();app.GetRuntimeUI().Preview(s.draft,*app.GetResourceManager());
     if(!s.selected.empty()&&!ui::ManagedByLayout(s.draft,widgets::Find(s.draft,s.selected))){if(s.pending)EditUIDocument(app,m_DesignDocument,s.draft,true);auto w=widgets::Find(s.draft,s.selected);m_UIWidgetDrag=UIWidgetDrag{m_DesignDocument,s.selected,w,widgets::Rectangle(displayed,s.selected,canvas),widgets::ParentRectangle(displayed,w,canvas),{io.MousePos.x,io.MousePos.y},hoveredHandle,logicalScale};}
    }
    if(m_UIWidgetDrag){
     if(ImGui::IsKeyPressed(ImGuiKey_Escape,false)){CancelUIDocumentEdit(app,m_UIWidgetDrag->key);m_UIWidgetDrag.reset();m_EscapeConsumed=true;}
     else {auto drag=*m_UIWidgetDrag;float dx=(io.MousePos.x-drag.mouse.x)/drag.screenScale,dy=(io.MousePos.y-drag.mouse.y)/drag.screenScale;
      if(ImGui::IsMouseDragging(0)||s.pending){auto edited=s.draft;auto& w=const_cast<ui::Widget&>(widgets::Find(edited,drag.id));w=drag.initial;widgets::SetRectangle(w,widgets::Drag(drag.start,dx,dy,drag.handle,m_UISnap&&!io.KeyAlt?m_UIGridSize:0),drag.parent);EditUIDocument(app,drag.key,edited,false);}
      if(!ImGui::IsMouseDown(0)){if(s.pending)EditUIDocument(app,drag.key,s.draft,true);m_UIWidgetDrag.reset();}
     }
    }
    for(auto& error:draw.errors)if(hovered)ImGui::SetTooltip("%s",error.c_str());
   }catch(const std::exception& e){ImGui::TextWrapped("UI preview: %s",e.what());}
  }ImGui::End();
 }
 if(m_ShowWidgetHierarchy&&!m_WorkspaceFocus){
  if(ImGui::Begin("Widget Hierarchy",&m_ShowWidgetHierarchy)&&!m_DesignDocument.empty())try{
   auto& s=UIDocumentSession(app,m_DesignDocument);ImGui::SetNextItemWidth(-FLT_MIN);ImGui::InputTextWithHint("##Widget Search","Search ID / type",&m_WidgetSearch);
   ImGui::BeginDisabled(app.IsEditorPlayMode()||bool(m_UIWidgetDrag));ImGui::SetNextItemWidth(-FLT_MIN);if(ImGui::BeginCombo("##Add Widget","Add Widget...")){for(auto* type:{"Panel","Text","Image","ProgressBar","Button"})if(ImGui::Selectable(type))WidgetCommand(app,"Add",type);ImGui::EndCombo();}
   if(ImGui::Button("Duplicate"))WidgetCommand(app,"Duplicate");ImGui::SameLine();if(ImGui::Button("Delete"))WidgetCommand(app,"Delete");
   if(ImGui::Button("Earlier"))WidgetCommand(app,"Earlier");ImGui::SameLine();if(ImGui::Button("Later"))WidgetCommand(app,"Later");
   // Traverse a stable copy: commands can replace the draft while a tree is being drawn.
   const auto document=s.draft;std::optional<std::pair<std::string,std::string>> reparent;
   auto matches=[&](const ui::Widget& w){auto value=w.id+" "+w.type,needle=m_WidgetSearch;std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return char(std::tolower(c));});std::transform(needle.begin(),needle.end(),needle.begin(),[](unsigned char c){return char(std::tolower(c));});return value.find(needle)!=std::string::npos;};
   auto hasMatch=[&](auto&& self,const ui::Widget& w)->bool{if(matches(w))return true;for(auto& child:document.widgets)if(child.parent==w.id&&self(self,child))return true;return false;};
   auto tree=[&](auto&& self,const std::string& parent)->void{
    std::vector<const ui::Widget*> children;for(auto& w:document.widgets)if(w.parent==parent)children.push_back(&w);std::sort(children.begin(),children.end(),[](auto a,auto b){return std::tie(a->zOrder,a->id)<std::tie(b->zOrder,b->id);});
    for(auto* w:children){if(!hasMatch(hasMatch,*w))continue;bool leaf=std::none_of(document.widgets.begin(),document.widgets.end(),[&](auto& child){return child.parent==w->id;});ImGui::PushID(w->id.c_str());if(!m_WidgetSearch.empty())ImGui::SetNextItemOpen(true);auto flags=ImGuiTreeNodeFlags_OpenOnArrow|ImGuiTreeNodeFlags_DefaultOpen|ImGuiTreeNodeFlags_SpanAvailWidth|(s.selected==w->id?ImGuiTreeNodeFlags_Selected:0)|(leaf?ImGuiTreeNodeFlags_Leaf:0);bool open=ImGui::TreeNodeEx("Widget",flags,"%s (%s)%s",w->id.c_str(),w->type.c_str(),w->visible?"":" [hidden]");if(ImGui::IsItemClicked()&&!ImGui::IsItemToggledOpen())s.selected=w->id;if(ImGui::IsItemHovered())ImGui::SetTooltip("%s (%s)",w->id.c_str(),w->type.c_str());
     if(w->id!=document.root&&ImGui::BeginDragDropSource()){ImGui::SetDragDropPayload("WHISP_UI_WIDGET",w->id.c_str(),w->id.size()+1);ImGui::TextUnformatted(w->id.c_str());ImGui::EndDragDropSource();}
     if(ImGui::BeginDragDropTarget()){if(auto* payload=ImGui::AcceptDragDropPayload("WHISP_UI_WIDGET"))reparent={{static_cast<const char*>(payload->Data),w->id}};ImGui::EndDragDropTarget();}
     if(open){self(self,w->id);ImGui::TreePop();}ImGui::PopID();
    }
   };tree(tree,"");if(reparent){s.selected=reparent->first;WidgetCommand(app,"Reparent",reparent->second);}ImGui::EndDisabled();
  }catch(const std::exception& e){ImGui::TextWrapped("UI hierarchy: %s",e.what());}ImGui::End();
 }
 if(m_ShowWidgetInspector&&!m_WorkspaceFocus){
  if(ImGui::Begin("Widget Inspector",&m_ShowWidgetInspector)&&!m_DesignDocument.empty())try{
   auto& s=UIDocumentSession(app,m_DesignDocument);if(!m_EditorError.empty())ImGui::TextWrapped("%s",m_EditorError.c_str());ImGui::BeginDisabled(app.IsEditorPlayMode()||bool(m_UIWidgetDrag));
   if(m_WidgetRenameId!=s.selected){m_WidgetRenameId=s.selected;m_WidgetRename=s.selected;}
   ImGui::TextUnformatted("Widget ID");ImGui::SetNextItemWidth(-FLT_MIN);ImGui::InputText("##Widget ID",&m_WidgetRename);if(ImGui::Button("Rename ID"))WidgetCommand(app,"Rename",m_WidgetRename);
   if(!s.selected.empty()){
    ImGui::TextUnformatted("Anchor Preset");ImGui::SetNextItemWidth(-FLT_MIN);if(ImGui::BeginCombo("##Anchor Preset","Preserve rectangle")){for(int i=0;i<12;++i)if(ImGui::Selectable(Anchors[i]))WidgetCommand(app,"Anchor",std::to_string(i));ImGui::EndCombo();}
    ImGui::TextUnformatted("Pivot Preset");ImGui::SetNextItemWidth(-FLT_MIN);if(ImGui::BeginCombo("##Pivot Preset","Preserve rectangle")){for(int i=0;i<9;++i)if(ImGui::Selectable(Anchors[i]))WidgetCommand(app,"Pivot",std::to_string(i));ImGui::EndCombo();}
   }
   // The UI.1 property editor is shared with the UICanvas scene inspector.
   DrawUIDocumentProperties(app,m_DesignDocument,false);ImGui::EndDisabled();
  }catch(const std::exception& e){ImGui::TextWrapped("UI inspector: %s",e.what());}ImGui::End();
 }
 if(UIDesignerOwnsKeyboard()&&!m_DesignDocument.empty()&&!app.IsEditorPlayMode()&&!ImGui::GetIO().WantTextInput&&!ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel)&&!m_UIWidgetDrag){
  auto& io=ImGui::GetIO();if(io.KeyCtrl){if(ImGui::IsKeyPressed(ImGuiKey_Z,false))UndoUIDocument(app,m_DesignDocument,io.KeyShift);if(ImGui::IsKeyPressed(ImGuiKey_Y,false))UndoUIDocument(app,m_DesignDocument,true);if(ImGui::IsKeyPressed(ImGuiKey_S,false))SaveUIDocument(app,m_DesignDocument);if(ImGui::IsKeyPressed(ImGuiKey_D,false))WidgetCommand(app,"Duplicate");}if(ImGui::IsKeyPressed(ImGuiKey_Delete,false))WidgetCommand(app,"Delete");
 }
}
}
