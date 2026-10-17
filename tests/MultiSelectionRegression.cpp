#define NOMINMAX
#include "EditorTestPreferences.h"
#include "SyntheticEditorInput.h"
#include "core/Application.h"
#include "editor/LightGizmoMath.h"
#include "ecs/components/SceneIdentityComponent.h"
#include "scene/SceneSerializer.h"
#include "scripting/EntityReference.h"
#include "ecs/components/LuaScriptComponent.h"
#include "ecs/components/ColliderComponent.h"
#include <set>
#include <imgui_internal.h>
#include <ImGuizmo.h>
#include <iostream>
#include <limits>
#if defined(ENABLE_DX12)
#include "platform/GlfwWindow.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#endif
using namespace ecs;
using namespace editor;
static int checks=0;
static void Check(bool value,const char* label){if(!value)throw std::runtime_error(label);++checks;}
static void Near(float a,float b,const char* label,float epsilon=.0002f){Check(std::abs(a-b)<epsilon,label);}
static void SameTransform(const TransformComponent& a,const TransformComponent& b,const char* label){auto x=TransformMatrix(a),y=TransformMatrix(b);for(int i=0;i<16;++i)Near(float(x[i]),float(y[i]),label);}
static void DuplicateGroup(){
 for(int size:{1,2,16}){
  Application app;app.NewScene();auto& ui=app.GetEditorLayer();auto& w=app.GetWorld();
  auto external=ui.CreateEntity(app,"External");std::vector<Entity> sources;std::vector<nlohmann::json> snapshots;
  for(int i=0;i<size;++i){auto e=ui.CreateEntity(app,"Source");sources.push_back(e);auto& t=*w.GetComponent<TransformComponent>(e);t.position={float(i*2),0,0};t.rotation={.1f*i,.2f,.3f};t.scale={1,2,3};
   if(i%2==0)w.AddComponent<ColliderComponent>(e);else w.AddComponent<PointLightComponent>(e);
  }
  for(int i=0;i<size;++i){auto& lua=w.AddComponent<LuaScriptComponent>(sources[i]);auto internal=SceneSerializer::EntitySceneId(w,sources[(i+1)%size]);lua.properties={{"target",{{"$entity",internal}}},{"outside",{{"$entity",SceneSerializer::EntitySceneId(w,external)}}},{"text",internal}};snapshots.push_back(ComponentRegistry::Builtins().Capture(w,sources[i]));}
  ui.ResetHistory();for(auto e:sources)ui.SelectViewportEntity(app,e,true,false);
  const auto count=w.GetAliveCount();if(size>1)Check(ui.BeginGroupTransform(app,false),"duplicate group baseline");
  Check(ui.BeginDuplicateDrag(app).IsValid(),"begin duplicate set");auto copies=ui.GetSelectedEntities();Check(copies.size()==size&&ui.GetSelectedEntity()==copies.back(),"duplicate selection and active");
  Check(!ui.BeginDuplicateDrag(app).IsValid()&&w.GetAliveCount()==count+size,"once per gesture");
  if(size>1){TransformComponent pivot;pivot.position={float(size-1)+5,2,-3};Check(ui.UpdateGroupTransform(app,TransformMatrix(pivot)),"move cloned group");ui.EndGroupTransform(app);}else{ui.FinishTransaction(app);}
  auto verify=[&]{std::set<std::string> ids;for(auto e:w.Entities())Check(ids.insert(SceneSerializer::EntitySceneId(w,e)).second,"unique scene identities");
   copies=ui.GetSelectedEntities();for(int i=0;i<size;++i){Check(ComponentRegistry::Builtins().Capture(w,sources[i])==snapshots[i],"original components unchanged");auto& p=w.GetComponent<LuaScriptComponent>(copies[i])->properties;
    Check(scripting::ResolveEntityReference(w,p["target"])==copies[(i+1)%size],"internal references remapped");Check(scripting::ResolveEntityReference(w,p["outside"])==external,"external reference retained");Check(p["text"]==snapshots[i]["LuaScript"]["properties"]["text"],"ordinary strings untouched");
    auto expected=*w.GetComponent<TransformComponent>(sources[i]);if(size>1){expected.position.x+=5;expected.position.y+=2;expected.position.z-=3;}SameTransform(*w.GetComponent<TransformComponent>(copies[i]),expected,"clone relative transform");
   }};
  Check(ui.UndoCount()==1,"duplicate plus movement one Undo");verify();
  for(int n=0;n<4;++n){ui.UndoEdit(app);Check(w.GetAliveCount()==count&&ui.GetSelectedEntities()==sources,"Undo removes entire set and restores selection");ui.RedoEdit(app);Check(w.GetAliveCount()==count+size,"Redo restores entire set");verify();}
  auto saved=SceneSerializer::CaptureWorld(w);World loaded;SceneSettings settings;std::string error;Check(SceneSerializer::RestoreWorld(saved,loaded,settings,&error),"clone scene round trip");Check(loaded.GetAliveCount()==w.GetAliveCount(),"round trip count");
  for(auto e:loaded.Entities())if(auto* lua=loaded.GetComponent<LuaScriptComponent>(e))Check(loaded.IsAlive(scripting::ResolveEntityReference(loaded,lua->properties["target"])),"round trip references resolve");
  if(size>1){Check(ui.BeginGroupTransform(app,false),"cancel baseline");Check(ui.BeginDuplicateDrag(app).IsValid(),"cancel duplicate");ui.EndGroupTransform(app,true);Check(w.GetAliveCount()==count+size&&ui.GetSelectedEntities()==copies,"cancel removes all new copies restores group");}
  app.SetEditorPlayMode(true);auto runtimeCount=app.GetWorld().GetAliveCount();Check(!ui.BeginDuplicateDrag(app).IsValid()&&app.GetWorld().GetAliveCount()==runtimeCount,"Play prohibits authoring duplication");app.SetEditorPaused(true);Check(!ui.BeginDuplicateDrag(app).IsValid(),"Pause prohibits authoring duplication");app.SetEditorPlayMode(false);
  app.NewScene();Check(ui.GetSelectedEntities().empty()&&!ui.HasPendingTransaction(),"scene switch resets duplicate state");
 }
}
static void Cpu(){
 Application app;app.NewScene();auto& ui=app.GetEditorLayer();auto& world=app.GetWorld();
 auto a=ui.CreateEntity(app,"A"),b=ui.CreateEntity(app,"B"),c=ui.CreateEntity(app,"C"),d=ui.CreateEntity(app,"D");
 ui.SelectViewportEntity(app,a,false,false);ui.SelectViewportEntity(app,b,true,false);Check(ui.GetSelectedEntities().size()==2&&ui.GetSelectedEntity()==b,"Ctrl add and Active");
 ui.SelectViewportEntity(app,b,true,false);Check(ui.GetSelectedEntities().size()==1&&ui.GetSelectedEntity()==a,"Ctrl remove Active fallback");
 ui.SelectViewportEntity(app,c,false,true);Check(ui.GetSelectedEntities().size()==2&&ui.GetSelectedEntity()==c,"Viewport Shift add");
 ui.SelectViewportEntity(app,a,true,false);Check(ui.GetSelectedEntity()==c&&ui.GetSelectedEntities().size()==1,"remove non-active preserves Active");
 std::vector<Entity> visible{c,b,a,d};ui.SelectHierarchyEntity(app,b,false,false,visible);ui.SelectHierarchyEntity(app,d,false,true,visible);
 Check(ui.GetSelectedEntities().size()==3&&ui.IsSelected(a)&&ui.IsSelected(b)&&ui.IsSelected(d)&&!ui.IsSelected(c)&&ui.GetSelectedEntity()==d,"Hierarchy Shift follows visual order");
 ui.SelectHierarchyEntity(app,c,true,true,visible);Check(ui.GetSelectedEntities().size()==4,"Ctrl Shift extends range");
 ui.SelectViewportEntity(app,{},false,true);Check(ui.GetSelectedEntities().size()==4,"modified empty click preserves selection");ui.SelectViewportEntity(app,{},false,false);Check(ui.GetSelectedEntities().empty(),"empty click clears selection");
 World foreign;auto wrong=foreign.CreateEntity();ui.SelectViewportEntity(app,wrong,true,false);Check(ui.GetSelectedEntities().empty(),"foreign World handles rejected");
 TransformComponent originalA,originalB;originalA.position={-2,0,0};originalB.position={2,0,0};originalA.rotation={.2f,.3f,.1f};originalB.rotation={-.3f,.2f,-.4f};originalA.scale={1,2,3};originalB.scale={-2,1,1};
 auto reset=[&]{ui.ResetHistory();*world.GetComponent<TransformComponent>(a)=originalA;*world.GetComponent<TransformComponent>(b)=originalB;ui.SelectViewportEntity(app,a,false,false);ui.SelectViewportEntity(app,b,true,false);};
 reset();auto count=ui.UndoCount();Check(ui.BeginGroupTransform(app,false),"begin group move");TransformComponent pivot;pivot.position={5,1,-3};Check(ui.UpdateGroupTransform(app,TransformMatrix(pivot)),"group move update");pivot.position.x=10;
 for(int i=0;i<100;++i)Check(ui.UpdateGroupTransform(app,TransformMatrix(pivot)),"absolute drag update");
 Near(world.GetComponent<TransformComponent>(a)->position.x,8,"drag does not accumulate deltas");Near(world.GetComponent<TransformComponent>(b)->position.x,12,"same world offset");ui.EndGroupTransform(app);Check(ui.UndoCount()==count+1,"one drag one Undo");
 ui.UndoEdit(app);SameTransform(*world.GetComponent<TransformComponent>(a),originalA,"Undo group A");SameTransform(*world.GetComponent<TransformComponent>(b),originalB,"Undo group B");Check(ui.GetSelectedEntities().size()==2&&ui.GetSelectedEntity()==b,"Undo preserves group selection");ui.RedoEdit(app);Near(world.GetComponent<TransformComponent>(a)->position.x,8,"Redo whole group");
 auto movedA=*world.GetComponent<TransformComponent>(a),movedB=*world.GetComponent<TransformComponent>(b);count=ui.UndoCount();Check(ui.BeginGroupTransform(app,false),"begin cancel");pivot.position={11,2,4};Check(ui.UpdateGroupTransform(app,TransformMatrix(pivot)),"cancel update");ui.EndGroupTransform(app,true);SameTransform(*world.GetComponent<TransformComponent>(a),movedA,"cancel restores A");SameTransform(*world.GetComponent<TransformComponent>(b),movedB,"cancel restores B");Check(ui.UndoCount()==count,"cancel adds no history");
 reset();pivot={};pivot.rotation.y=1.5707963268f;Check(ui.BeginGroupTransform(app,false)&&ui.UpdateGroupTransform(app,TransformMatrix(pivot)),"world group rotation");ui.EndGroupTransform(app);Near(world.GetComponent<TransformComponent>(a)->position.z,2,"rotation orbits A around mean");Near(world.GetComponent<TransformComponent>(b)->position.z,-2,"rotation orbits B around mean");
 auto expected=MultiplyMatrix(TransformMatrix(pivot),TransformMatrix(originalA));auto actual=TransformMatrix(*world.GetComponent<TransformComponent>(a));for(int i=0;i<16;++i)Near(float(actual[i]),float(expected[i]),"rotation composes matrices, not Euler sums");ui.UndoEdit(app);SameTransform(*world.GetComponent<TransformComponent>(a),originalA,"rotate Undo");ui.RedoEdit(app);Near(world.GetComponent<TransformComponent>(a)->position.z,2,"rotate Redo");
 reset();TransformComponent local;local.rotation=originalB.rotation;TransformComponent turn;turn.rotation.x=.7f;auto target=MultiplyMatrix(TransformMatrix(local),TransformMatrix(turn));Check(ui.BeginGroupTransform(app,true)&&ui.UpdateGroupTransform(app,target),"Local rotation uses active orientation");auto delta=MultiplyMatrix(target,InverseGroupPivot(local));expected=MultiplyMatrix(delta,TransformMatrix(originalA));actual=TransformMatrix(*world.GetComponent<TransformComponent>(a));for(int i=0;i<16;++i)Near(float(actual[i]),float(expected[i]),"Local group rotation product");ui.EndGroupTransform(app);
 reset();pivot={};pivot.scale={2,2,2};Check(ui.BeginGroupTransform(app,false)&&ui.UpdateGroupTransform(app,TransformMatrix(pivot)),"uniform group scale handles mixed rotations");ui.EndGroupTransform(app);Near(world.GetComponent<TransformComponent>(a)->position.x,-4,"scale orbits position");Near(world.GetComponent<TransformComponent>(a)->scale.y,4,"scale changes own scale");Near(world.GetComponent<TransformComponent>(b)->scale.x,-4,"scale preserves negative sign");ui.UndoEdit(app);SameTransform(*world.GetComponent<TransformComponent>(b),originalB,"scale Undo");ui.RedoEdit(app);Near(world.GetComponent<TransformComponent>(b)->position.x,4,"scale Redo");
 originalA.rotation=originalB.rotation={};reset();pivot.scale={2,1,1};Check(ui.BeginGroupTransform(app,false)&&ui.UpdateGroupTransform(app,TransformMatrix(pivot)),"aligned non-uniform scale");ui.EndGroupTransform(app);Near(world.GetComponent<TransformComponent>(a)->scale.x,2,"non-uniform own scale");
 originalB.rotation.z=.785398163f;reset();count=ui.UndoCount();Check(ui.BeginGroupTransform(app,false),"begin invalid scale");Check(!ui.UpdateGroupTransform(app,TransformMatrix(pivot)),"shear rejected");SameTransform(*world.GetComponent<TransformComponent>(a),originalA,"invalid group is atomic even if first target valid");SameTransform(*world.GetComponent<TransformComponent>(b),originalB,"invalid group leaves B");auto invalid=TransformMatrix(pivot);invalid[0]=std::numeric_limits<double>::quiet_NaN();Check(!ui.UpdateGroupTransform(app,invalid),"NaN rejected");pivot.scale={0,1,1};Check(!ui.UpdateGroupTransform(app,TransformMatrix(pivot)),"zero scale rejected");ui.EndGroupTransform(app);Check(ui.UndoCount()==count,"invalid operation no Undo");
 reset();pivot={};pivot.position={3,0,0};Check(ui.BeginGroupTransform(app,false)&&ui.UpdateGroupTransform(app,TransformMatrix(pivot)),"move before Delete");ui.EndGroupTransform(app);count=ui.UndoCount();auto alive=world.GetAliveCount();Check(ui.DeleteSelection(app)&&world.GetAliveCount()==alive-2&&world.IsAlive(c)&&world.IsAlive(d),"atomic Delete leaves unselected entities");Check(ui.UndoCount()==count+1&&ui.GetSelectedEntities().empty(),"one Delete transaction");ui.UndoEdit(app);Check(world.GetAliveCount()==alive&&ui.GetSelectedEntities().size()==2,"Delete Undo restores group");for(auto e:ui.GetSelectedEntities())Check(world.IsAlive(e)&&e!=a&&e!=b,"Undo remaps generations");
 auto newA=ui.GetSelectedEntities()[0],newB=ui.GetSelectedEntity();Check(world.GetComponent<TagComponent>(newB)->name=="B","Active remapped after group restore");ui.RedoEdit(app);Check(world.GetAliveCount()==alive-2&&ui.GetSelectedEntities().empty(),"Delete Redo atomic");ui.UndoEdit(app);newA=ui.GetSelectedEntities()[0];newB=ui.GetSelectedEntity();ui.UndoEdit(app);SameTransform(*world.GetComponent<TransformComponent>(newA),originalA,"earlier group history remapped after Delete cycles");
 Check(ui.SetProperty(app,newB,ComponentId("Transform"),"position",Vec3{7,2,3}),"Inspector active property");ui.UndoEdit(app);Check(ui.GetSelectedEntities().size()==2&&ui.GetSelectedEntity()==newB,"active property Undo retains selection set");
 auto marker=ui.CreatePlayerStart(app);ui.SelectViewportEntity(app,newA,false,false);ui.SelectViewportEntity(app,marker,true,false);auto editorSelection=ui.GetSelectedEntities();app.SetEditorPlayMode(true);for(auto e:ui.GetSelectedEntities())Check(app.GetWorld().IsAlive(e)&&e.world!=world.GetIdentity(),"runtime selection uses mapped World handles");Check(ui.GetSelectedEntities().size()==1,"spawned-away marker pruned immediately");app.SetEditorPlayMode(false);Check(ui.GetSelectedEntities()==editorSelection&&ui.GetSelectedEntity()==marker,"Stop restores editor selection and Active");
 ui.SelectViewportEntity(app,newA,false,false);ui.SelectViewportEntity(app,newB,true,false);auto survivor=*world.GetComponent<TransformComponent>(newA);Check(ui.BeginGroupTransform(app,false),"begin dying group");pivot.position={5,0,0};Check(ui.UpdateGroupTransform(app,TransformMatrix(pivot)),"dying group moved");world.DestroyEntity(newB);ui.PruneSelection(app);Check(!world.IsAlive(newB)&&ui.GetSelectedEntities().size()==1&&!ui.HasPendingTransaction(),"external destruction cancels drag without resurrection");SameTransform(*world.GetComponent<TransformComponent>(newA),survivor,"survivor restored on invalidated drag");
 auto replacement=ui.CreateEntity(app,"replacement");ui.SelectViewportEntity(app,newB,true,false);Check(!ui.IsSelected(newB)&&ui.IsSelected(replacement),"recycled index does not select stale generation");app.NewScene();Check(ui.GetSelectedEntities().empty()&&!ui.UpdateGroupTransform(app,IdentityMatrix()),"scene replacement fences selection and group session");
}
#if defined(ENABLE_DX12)
static void Gpu(){
 Application app;Check(app.Initialize(),"DX12 initialize");SyntheticEditorInput input;auto* window=dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle();glfwHideWindow(window);SyntheticEditorInput::Queue([](ImGuiIO& io){io.AddFocusEvent(true);});
 app.NewScene();auto& ui=app.GetEditorLayer();ui.SetGameView(false);ui.SetGizmoOperation(0);
 std::vector<Entity> cubes;for(int i=0;i<3;++i){auto e=ui.CreatePrimitive(app,"cube");cubes.push_back(e);app.GetWorld().GetComponent<TagComponent>(e)->name="Multi_"+std::to_string(i);auto& t=*app.GetWorld().GetComponent<TransformComponent>(e);t.position={float(i-1)*2,0,5};t.scale={.8f,.8f,.8f};}
 app.SetEditorCameraPose({0,0,-7},0,0);ui.ClearSelection();
 auto frame=[&](unsigned n=2){app.SetFrameLimit(n);Check(app.Run()==0,"native frames");};frame(90);
 auto press=[&](ImGuiKey key){SyntheticEditorInput::Key(key,true);frame();SyntheticEditorInput::Key(key,false);frame();};
 auto point=[&](float x,float y){SyntheticEditorInput::Position(x,y);frame();};
 auto click=[&](float x,float y,bool ctrl=false,bool shift=false){SyntheticEditorInput::Key(ImGuiMod_Ctrl,ctrl);SyntheticEditorInput::Key(ImGuiMod_Shift,shift);point(x,y);SyntheticEditorInput::Button(0,true);frame(1);SyntheticEditorInput::Button(0,false);frame();SyntheticEditorInput::Key(ImGuiMod_Ctrl,false);SyntheticEditorInput::Key(ImGuiMod_Shift,false);frame();};
 auto chord=[&](ImGuiKey key){SyntheticEditorInput::Key(ImGuiMod_Ctrl,true);frame();press(key);SyntheticEditorInput::Key(ImGuiMod_Ctrl,false);frame();};
 ImGui::FocusWindow(ImGui::FindWindowByName("Viewport"));frame();press(ImGuiKey_F11);Check(ui.IsViewportMaximized(),"fullscreen viewport for pointer tests");
 auto screen=[&](Vec3 p){auto b=ui.GetViewportBounds();auto camera=GizmoCamera(app.GetCameraPosition(),app.GetCameraYaw(),app.GetCameraPitch(),app.GetCameraVerticalFovRadians(),(b[2]-b[0])/(b[3]-b[1]),app.GetCameraNearPlane(),app.GetCameraFarPlane());auto q=ProjectGizmo(p,camera);Check(bool(q),"test point projects");return ImVec2(b[0]+(*q)[0]*(b[2]-b[0]),b[1]+(*q)[1]*(b[3]-b[1]));};
 auto pa=screen({-2,0,5}),pb=screen({0,0,5}),pc=screen({2,0,5});click(pa.x,pa.y);Check(ui.GetSelectedEntities().size()==1&&ui.GetSelectedEntity()==cubes[0],"real viewport single pick");click(pb.x,pb.y,true);Check(ui.GetSelectedEntities().size()==2&&ui.GetSelectedEntity()==cubes[1],"real viewport Ctrl add");click(pb.x,pb.y,true);Check(ui.GetSelectedEntities().size()==1&&ui.GetSelectedEntity()==cubes[0],"real viewport Ctrl remove");click(pc.x,pc.y,false,true);Check(ui.GetSelectedEntities().size()==2&&ui.GetSelectedEntity()==cubes[2],"real viewport Shift add");
 auto center=screen({0,0,5});float grab=0;for(float dx:{15.f,20.f,25.f,30.f,35.f,45.f}){point(center.x+dx,center.y);if(ImGuizmo::IsOver(ImGuizmo::TRANSLATE_X)){grab=dx;break;}}Check(grab>0,"shared Move X handle hover");
 auto history=ui.UndoCount();auto beforeA=*app.GetWorld().GetComponent<TransformComponent>(cubes[0]),beforeC=*app.GetWorld().GetComponent<TransformComponent>(cubes[2]);
 ui.SetSnapping(true);SyntheticEditorInput::Button(0,true);frame(1);Check(ui.UndoCount()==history,"press without movement no Undo");point(center.x+grab+55,center.y);point(center.x+grab+80,center.y);SyntheticEditorInput::Button(0,false);frame();
 auto afterA=*app.GetWorld().GetComponent<TransformComponent>(cubes[0]),afterC=*app.GetWorld().GetComponent<TransformComponent>(cubes[2]);float moved=afterA.position.x-beforeA.position.x;Check(std::abs(moved)>.1f,"native group moved");Near(afterC.position.x-beforeC.position.x,moved,"native common movement");Near(moved,std::round(moved),"group Move snap");Check(ui.UndoCount()==history+1,"native Move one Undo");chord(ImGuiKey_Z);SameTransform(*app.GetWorld().GetComponent<TransformComponent>(cubes[0]),beforeA,"native Move Undo A");SameTransform(*app.GetWorld().GetComponent<TransformComponent>(cubes[2]),beforeC,"native Move Undo C");chord(ImGuiKey_Y);Near(app.GetWorld().GetComponent<TransformComponent>(cubes[0])->position.x,afterA.position.x,"native Move Redo");chord(ImGuiKey_Z);
 // P0.2: exercise the actual ImGuizmo input path, not just duplication helpers.
 auto baseCount=app.GetWorld().GetAliveCount();auto originals=ui.GetSelectedEntities();
 auto alt=[&](bool down){SyntheticEditorInput::Key(ImGuiMod_Alt,down);frame();};
 auto release=[&]{SyntheticEditorInput::Button(0,false);frame();};
 auto start=[&]{point(center.x+grab,center.y);SyntheticEditorInput::Button(0,true);frame(1);};
 alt(true);start();release();Check(app.GetWorld().GetAliveCount()==baseCount&&ui.GetSelectedEntities()==originals,"Alt click creates no copies");
 history=ui.UndoCount();start();point(center.x+grab+55,center.y);auto firstCopies=ui.GetSelectedEntities();
 Check(app.GetWorld().GetAliveCount()==baseCount+2&&firstCopies!=originals,"native Alt drag clones every selected entity");
 alt(false);point(center.x+grab+80,center.y);point(center.x+grab+90,center.y);release();
 Check(app.GetWorld().GetAliveCount()==baseCount+2&&ui.GetSelectedEntities()==firstCopies,"Alt release and continued drag create no extra copies");
 Check(ui.UndoCount()==history+1,"native duplicate move one Undo");
 SameTransform(*app.GetWorld().GetComponent<TransformComponent>(cubes[0]),beforeA,"native Alt drag preserves A");SameTransform(*app.GetWorld().GetComponent<TransformComponent>(cubes[2]),beforeC,"native Alt drag preserves C");
 chord(ImGuiKey_Z);Check(app.GetWorld().GetAliveCount()==baseCount&&ui.GetSelectedEntities()==originals,"native duplicate Undo selection");
 chord(ImGuiKey_Y);Check(app.GetWorld().GetAliveCount()==baseCount+2&&ui.GetSelectedEntities().size()==2,"native duplicate Redo");chord(ImGuiKey_Z);
 alt(true);start();point(center.x+grab+60,center.y);Check(app.GetWorld().GetAliveCount()==baseCount+2,"second drag creates one new set");
 press(ImGuiKey_Escape);Check(app.GetWorld().GetAliveCount()==baseCount&&ui.GetSelectedEntities()==originals&&!ui.HasPendingTransaction(),"Escape removes complete batch restores selection");
 release();alt(false);
 ui.SetSnapping(false);ui.SetGizmoOperation(1);frame();float radius=0;for(float r=20;r<=110;r+=5){point(center.x+r*.7071068f,center.y-r*.7071068f);if(ImGuizmo::IsOver(ImGuizmo::ROTATE_Z)){radius=r;break;}}Check(radius>0,"shared Rotate Z ring");history=ui.UndoCount();SyntheticEditorInput::Button(0,true);frame(1);point(center.x+radius*.258819f,center.y-radius*.965926f);SyntheticEditorInput::Button(0,false);frame();
 auto rotateA=*app.GetWorld().GetComponent<TransformComponent>(cubes[0]),rotateC=*app.GetWorld().GetComponent<TransformComponent>(cubes[2]);Check(std::abs(rotateA.position.y)>.1f&&std::abs(rotateA.rotation.z)>.01f,"native group rotates positions and orientation");Near(std::hypot(rotateC.position.x-rotateA.position.x,rotateC.position.y-rotateA.position.y),4,"native rotation preserves distance",.002f);Check(ui.UndoCount()==history+1,"native rotation one Undo");chord(ImGuiKey_Z);
 ui.SetGizmoOperation(2);frame();point(center.x,center.y);Check(ImGuizmo::IsOver(ImGuizmo::SCALE),"shared uniform scale handle");history=ui.UndoCount();SyntheticEditorInput::Button(0,true);frame(1);point(center.x+45,center.y-20);SyntheticEditorInput::Button(0,false);frame();
 auto scaleA=*app.GetWorld().GetComponent<TransformComponent>(cubes[0]),scaleC=*app.GetWorld().GetComponent<TransformComponent>(cubes[2]);Check(std::abs(scaleA.scale.x-.8f)>.01f&&std::abs(scaleA.position.x+2)>.01f,"native group scale changes sizes and offsets");Near(scaleC.scale.x,scaleA.scale.x,"native uniform scale shared");Check(ui.UndoCount()==history+1,"native scale one Undo");chord(ImGuiKey_Z);
 // All native Move axes and plane handles use the same duplicate transaction.
 ui.SetGizmoOperation(0);ui.SetSnapping(false);
 Check(app.SetEditorCameraPose({-8,7,-7},std::atan2(8.f,12.f),-std::atan2(7.f,std::sqrt(208.f))),"oblique move camera");frame();
 center=screen({0,0,5});std::array<ImVec2,3> directions;
 for(int axis=0;axis<3;++axis){Vec3 p{0,0,5};if(axis==0)p.x+=1;if(axis==1)p.y+=1;if(axis==2)p.z+=1;auto q=screen(p);float length=std::hypot(q.x-center.x,q.y-center.y);directions[axis]={(q.x-center.x)/length,(q.y-center.y)/length};}
 for(int handle=0;handle<6;++handle){
  const auto expected=ImGuizmo::MOVETYPE(ImGuizmo::MT_MOVE_X+handle);ImVec2 hit{};bool found=false;
  for(float radius:{16.f,24.f,32.f,40.f,48.f,56.f}){
   for(int signs=0;signs<4;++signs){float sign=(signs&1)?-1.f:1.f,other=(signs&2)?-1.f:1.f;ImVec2 offset{};
    if(handle<3){offset={directions[handle].x*radius*sign,directions[handle].y*radius*sign};}
    else{int a=(handle-3+1)%3,b=(handle-3+2)%3;offset={(directions[a].x*sign+directions[b].x*other)*radius,(directions[a].y*sign+directions[b].y*other)*radius};}
    hit={center.x+offset.x,center.y+offset.y};point(hit.x,hit.y);if(ImGuizmo::GetHoveredHandleType()==expected){found=true;break;}
   }if(found)break;
  }
  Check(found,"native Move axis/plane available");history=ui.UndoCount();alt(true);SyntheticEditorInput::Button(0,true);frame(1);Check(ImGuizmo::GetActiveHandleType()==expected,"native intended axis/plane captured");
  point(hit.x+(hit.x-center.x)*.8f,hit.y+(hit.y-center.y)*.8f);release();alt(false);
  Check(app.GetWorld().GetAliveCount()==baseCount+2&&ui.UndoCount()==history+1,"native each axis/plane clones group once");
  auto group=ui.GetSelectedEntities();auto& ta=*app.GetWorld().GetComponent<TransformComponent>(group[0]);auto& tc=*app.GetWorld().GetComponent<TransformComponent>(group[1]);
  Near(tc.position.x-ta.position.x,4,"axis/plane relative X");Near(tc.position.y-ta.position.y,0,"axis/plane relative Y");Near(tc.position.z-ta.position.z,0,"axis/plane relative Z");
  Check(std::hypot(ta.position.x-beforeA.position.x,ta.position.y-beforeA.position.y,ta.position.z-beforeA.position.z)>.01f,"axis/plane actual movement");
  chord(ImGuiKey_Z);Check(app.GetWorld().GetAliveCount()==baseCount&&ui.GetSelectedEntities()==originals,"axis/plane Undo group");
 }
 Check(app.SetEditorCameraPose({0,0,-7},0,0),"restore front camera");frame();
 // Actual Hierarchy rows exercise Ctrl/Shift routing and text-field safety.
 press(ImGuiKey_F11);ui.SetGizmoOperation(0);frame();auto* hierarchy=ImGui::FindWindowByName("Scene Hierarchy");ImGui::FocusWindow(hierarchy);frame();
 auto rows=app.GetWorld().Entities();float hx=hierarchy->WorkRect.Min.x+40,hy=hierarchy->WorkRect.Min.y+ImGui::GetFrameHeightWithSpacing()+ImGui::GetTextLineHeight()*.5f,step=ImGui::GetTextLineHeightWithSpacing();click(hx,hy);Check(ui.GetSelectedEntity()==rows[0]&&ui.GetSelectedEntities().size()==1,"Hierarchy single row");click(hx,hy+2*step,false,true);Check(ui.GetSelectedEntities().size()==3&&ui.GetSelectedEntity()==rows[2],"Hierarchy real Shift range");
 ui.ClearSelection();chord(ImGuiKey_A);Check(ui.GetSelectedEntities().size()==3,"Hierarchy Ctrl A");ui.ClearSelection();click(hierarchy->WorkRect.Min.x+25,hierarchy->WorkRect.Min.y+8);chord(ImGuiKey_A);Check(ui.GetSelectedEntities().empty(),"search text Ctrl A not stolen");press(ImGuiKey_Escape);
 ImGui::FocusWindow(hierarchy);frame();click(hx,hy);click(hx,hy+2*step,true);Check(ui.GetSelectedEntities().size()==2,"Hierarchy real Ctrl add");history=ui.UndoCount();press(ImGuiKey_Delete);Check(app.GetWorld().GetAliveCount()==1&&ui.GetSelectedEntities().empty()&&ui.UndoCount()==history+1,"native Delete group");chord(ImGuiKey_Z);Check(app.GetWorld().GetAliveCount()==3&&ui.GetSelectedEntities().size()==2,"native Delete Undo selection restored");chord(ImGuiKey_Y);Check(app.GetWorld().GetAliveCount()==1&&ui.GetSelectedEntities().empty(),"native Delete Redo");
 Check(dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter())->GetValidationErrorCount()==0,"DX12 validation zero");
}
#endif
int main(int argc,char** argv){ScopedEditorPreferenceRestore preferences;try{
#if defined(ENABLE_DX12)
 if(argc>1&&std::string(argv[1])=="--dx12")Gpu();else
#endif
 {Cpu();DuplicateGroup();}std::cout<<"Multi-selection: "<<checks<<" checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

