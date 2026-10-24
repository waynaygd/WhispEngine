#define NOMINMAX
#include "EditorTestPreferences.h"
#include "SyntheticEditorInput.h"
#include "core/Application.h"
#include "core/AssetPaths.h"
#include "editor/LightGizmoMath.h"
#include "editor/ScalePlaneGeometry.h"
#include <ImGuizmo.h>
#include <imgui_internal.h>
#include "platform/GlfwWindow.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <filesystem>
#include <map>
#include <numeric>
static void Check(bool v,const char* text){if(!v)throw std::runtime_error(text);}
static void Matrix(const ecs::TransformComponent& t,float* model){float p[3]={t.position.x,t.position.y,t.position.z},r[3]={t.rotation.x*57.2957795f,t.rotation.y*57.2957795f,t.rotation.z*57.2957795f},s[3]={t.scale.x,t.scale.y,t.scale.z};ImGuizmo::RecomposeMatrixFromComponents(p,r,s,model);}
// Independent oracle: read the actual terminal circles from ImGuizmo's indexed
// draw mesh. Each AddLine/AddCircleFilled is a separate connected component.
static std::array<std::optional<ImVec2>,3> AxisCircles(ImDrawList& draw){
 std::vector<int> parent(draw.VtxBuffer.Size);std::iota(parent.begin(),parent.end(),0);
 auto root=[&](int x){while(parent[x]!=x){parent[x]=parent[parent[x]];x=parent[x];}return x;};
 for(int i=0;i<draw.IdxBuffer.Size;i+=3){int a=root(draw.IdxBuffer[i]);for(int j=1;j<3;++j)parent[root(draw.IdxBuffer[i+j])]=a;}
 std::array<std::optional<ImVec2>,3> result;
 for(int axis=0;axis<3;++axis){ImU32 color=ImGui::ColorConvertFloat4ToU32(ImGuizmo::GetStyle().Colors[ImGuizmo::DIRECTION_X+axis]);
  struct Bounds{float x=INFINITY,y=INFINITY,X=-INFINITY,Y=-INFINITY;int count=0;};std::map<int,Bounds> groups;
  for(int i=0;i<draw.VtxBuffer.Size;++i)if(draw.VtxBuffer[i].col==color){auto& b=groups[root(i)];auto p=draw.VtxBuffer[i].pos;b.x=std::min(b.x,p.x);b.y=std::min(b.y,p.y);b.X=std::max(b.X,p.x);b.Y=std::max(b.Y,p.y);++b.count;}
  int maximum=7;for(const auto& [id,b]:groups)if(b.count>maximum){maximum=b.count;result[axis]=ImVec2((b.x+b.X)*.5f,(b.y+b.Y)*.5f);}
 }return result;
}
static void GeometryOracle(){
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DeltaTime=1.f/60;io.MousePos={-10000,-10000};unsigned char* pixels;int fw,fh;io.Fonts->GetTexDataAsRGBA32(&pixels,&fw,&fh);io.Fonts->SetTexID(1);
 int cases=0,visible=0;std::array<int,3> covered{};
 for(auto size:{ImVec2(900,600),ImVec2(500,900),ImVec2(1600,900)})for(float fov:{.5f,1.f,1.8f})for(float depth:{2.f,20.f,2000.f})for(float dpi:{1.f,2.f})for(float yaw:{0.f,.7f})for(auto rotation:{ecs::Vec3{},ecs::Vec3{.785398f,.785398f,.785398f},ecs::Vec3{1.57079633f,0,0},ecs::Vec3{0,1.57079633f,0},ecs::Vec3{0,0,1.57079633f}})for(auto scale:{ecs::Vec3{1,1,1},ecs::Vec3{-2,3,-.5f}}){
  io.DisplaySize={size.x+100,size.y+100};ImGui::NewFrame();ImGuizmo::BeginFrame();auto* draw=ImGui::GetForegroundDrawList();
  float pitch=yaw==0?0.f:-.3f;auto camera=editor::GizmoCamera({-depth*std::sin(yaw)*std::cos(pitch),-depth*std::sin(pitch),-depth*std::cos(yaw)*std::cos(pitch)},yaw,pitch,fov,size.x/size.y,.01f,10000);ecs::TransformComponent t;t.rotation=rotation;t.scale=scale;float model[16];Matrix(t,model);
  ImGuizmo::Enable(true);ImGuizmo::AllowAxisFlip(true);ImGuizmo::SetDrawlist(draw);ImGuizmo::SetRect(37,29,size.x,size.y);ImGuizmo::SetGizmoSizeClipSpace(.1f);
  // Draw each real axis with a distinct inactive color: no pointer hover can
  // recolor the oracle, and suppress only the unrelated uniform center circle.
  auto style=ImGuizmo::GetStyle();ImGuizmo::GetStyle().CenterCircleSize=0;ImGuizmo::Enable(false);
  for(int axis=0;axis<3;++axis){ImGuizmo::GetStyle().Colors[ImGuizmo::INACTIVE]=style.Colors[ImGuizmo::DIRECTION_X+axis];ImGuizmo::Manipulate(camera.view.data(),camera.projection.data(),ImGuizmo::OPERATION(ImGuizmo::SCALE_X<<axis),ImGuizmo::LOCAL,model);}
  ImGuizmo::GetStyle()=style;ImGuizmo::Enable(true);
  auto actual=AxisCircles(*draw);auto geometry=editor::BuildScalePlaneGeometry(model,camera,{37,29},size,dpi);Check(geometry.valid,"finite projected scale frame");
  for(int axis=0;axis<3;++axis)if(actual[axis])Check(std::hypot(actual[axis]->x-geometry.ends[axis].x,actual[axis]->y-geometry.ends[axis].y)<.15f,"endpoint differs from actual ImGuizmo draw mesh");
  if(yaw==0&&rotation.x==0&&rotation.y==0&&rotation.z==0&&scale.x==1){Check(actual[0].has_value(),"unforeshortened X circle");Check(std::abs(std::hypot(actual[0]->x-geometry.center.x,actual[0]->y-geometry.center.y)-.05f*std::max(size.x,size.y))<.15f,"ImGuizmo visual length independent oracle");}
  for(int plane=0;plane<3;++plane){auto& polygon=geometry.planes[plane];if(!polygon.visible)continue;++visible;++covered[plane];int a=editor::ScalePlaneAxes[plane][0],b=editor::ScalePlaneAxes[plane][1];Check(actual[a]&&actual[b],"plane needs two visible axes");
   auto A=*actual[a],B=*actual[b],C=geometry.center;float ax=A.x-C.x,ay=A.y-C.y,bx=B.x-C.x,by=B.y-C.y,det=ax*by-ay*bx;Check(std::abs(det)>1,"nondegenerate sector");ImVec2 centroid{};
   for(auto p:polygon.points){float x=p.x-C.x,y=p.y-C.y,u=(x*by-y*bx)/det,v=(ax*y-ay*x)/det;Check(u>0&&v>0&&u<1.2f&&v<1.2f,"quad in correct actual axis sector");centroid.x+=p.x*.25f;centroid.y+=p.y*.25f;}
   Check(editor::HitScalePlane(polygon,centroid),"drawn polygon centroid hit");Check(!editor::HitScalePlane(polygon,C)&&!editor::HitScalePlane(polygon,A)&&!editor::HitScalePlane(polygon,B),"center and axes not plane hits");
  }
  ImGui::Render();++cases;
 }
 for(int n:covered)Check(n>0,"all three planes exercised by oracle");
 std::cout<<"Independent ImGuizmo mesh oracle: "<<cases<<" transforms, "<<visible<<" visible planes\n";ImGui::DestroyContext();
}
static editor::ScalePlaneGeometry Geometry(Application& app,const ecs::TransformComponent& t){auto bounds=app.GetEditorLayer().GetViewportBounds();ImVec2 size{bounds[2]-bounds[0],bounds[3]-bounds[1]};auto camera=editor::GizmoCamera(app.GetCameraPosition(),app.GetCameraYaw(),app.GetCameraPitch(),app.GetCameraVerticalFovRadians(),size.x/size.y,app.GetCameraNearPlane(),app.GetCameraFarPlane());float model[16];Matrix(t,model);return editor::BuildScalePlaneGeometry(model,camera,{bounds[0],bounds[1]},size,ImGui::GetStyle().FontScaleDpi);}
static void VerifyDrawnPlanes(Application& app,const ecs::TransformComponent& t){
 auto geometry=Geometry(app,t);auto* window=ImGui::FindWindowByName("Viewport");Check(window,"viewport draw data");int count=0;
 const ImU32 colors[3]={IM_COL32(225,205,60,95),IM_COL32(220,80,160,95),IM_COL32(70,200,210,95)};
 for(int plane=0;plane<3;++plane)if(geometry.planes[plane].visible){++count;for(auto point:geometry.planes[plane].points){float nearest=INFINITY;for(const auto& vertex:window->DrawList->VtxBuffer)if(vertex.col==colors[plane])nearest=std::min(nearest,std::hypot(point.x-vertex.pos.x,point.y-vertex.pos.y));Check(nearest<2,"native drawn polygon matches hit vertices");}}
 Check(count>0,"native visible scale plane");
}
static void Interact(Application& app,ecs::Entity cube){
 auto& ui=app.GetEditorLayer();auto& t=*app.GetWorld().GetComponent<ecs::TransformComponent>(cube);Check(app.SetEditorCameraPose({0,0,-6},0,0),"interaction camera");app.SetFrameCaptureOutput({});app.SetFrameLimit(1);
 for(int plane=0;plane<3;++plane){t.rotation=plane==0?ecs::Vec3{}:plane==1?ecs::Vec3{1.57079633f,0,0}:ecs::Vec3{0,1.57079633f,0};t.scale={-2,3,-.7f};ui.SelectEntity(cube);SyntheticEditorInput::Position(-1000,-1000);app.Run();VerifyDrawnPlanes(app,t);
  auto geometry=Geometry(app,t);auto polygon=geometry.planes[plane];Check(polygon.visible,"interaction plane visible");ImVec2 point{};for(auto p:polygon.points){point.x+=p.x*.25f;point.y+=p.y*.25f;}auto original=t.scale;auto history=ui.UndoCount();
  auto begin=[&]{
   // Undo/Redo can change the drawn geometry. Pick the current frame's plane,
   // after a released-input frame, rather than reusing the first drag's pixels.
   SyntheticEditorInput::Position(-1000,-1000);app.Run();geometry=Geometry(app,*app.GetWorld().GetComponent<ecs::TransformComponent>(cube));polygon=geometry.planes[plane];point={};for(auto p:polygon.points){point.x+=p.x*.25f;point.y+=p.y*.25f;}Check(polygon.visible&&editor::HitScalePlane(polygon,point),"current drawn plane has a hittable center");
   SyntheticEditorInput::Position(point.x,point.y);app.Run();SyntheticEditorInput::Button(0,true);app.Run();Check(ui.HasPlaneScaleCapture()&&ui.GetCapturedScalePlane()==plane&&!ImGuizmo::IsUsing(),"plane capture excludes ImGuizmo axis capture");};
  auto drag=[&]{float dx=point.x-geometry.center.x,dy=point.y-geometry.center.y,n=std::hypot(dx,dy);SyntheticEditorInput::Position(point.x+dx/n*35,point.y+dy/n*35);app.Run();Check(!ImGuizmo::IsUsing(),"axis remains inactive during plane drag");};
  begin();drag();auto changed=t.scale;float oldValues[3]={original.x,original.y,original.z},newValues[3]={changed.x,changed.y,changed.z};
  for(int axis=0;axis<3;++axis){bool affected=axis==editor::ScalePlaneAxes[plane][0]||axis==editor::ScalePlaneAxes[plane][1];Check(affected?std::abs(newValues[axis])>std::abs(oldValues[axis])&&std::signbit(newValues[axis])==std::signbit(oldValues[axis]):newValues[axis]==oldValues[axis],"only intended signed scale components change");}
  SyntheticEditorInput::Button(0,false);app.Run();Check(!ui.HasPlaneScaleCapture()&&ui.UndoCount()==history+1,"release commits exactly one drag");ui.UndoEdit(app);Check(t.scale.x==original.x&&t.scale.y==original.y&&t.scale.z==original.z,"Undo exact");ui.RedoEdit(app);Check(t.scale.x==changed.x&&t.scale.y==changed.y&&t.scale.z==changed.z,"Redo exact");ui.UndoEdit(app);
  begin();drag();SyntheticEditorInput::Key(ImGuiKey_Escape,true);app.Run();SyntheticEditorInput::Key(ImGuiKey_Escape,false);SyntheticEditorInput::Button(0,false);app.Run();Check(!ui.HasPlaneScaleCapture()&&t.scale.x==original.x&&t.scale.y==original.y&&t.scale.z==original.z&&ui.UndoCount()==history,"Escape cancels without undo entry");
  ui.SetSnapping(true);begin();drag();float factor=t.scale.x/original.x;if(plane==2)factor=t.scale.y/original.y;Check(std::abs(factor*10-std::round(factor*10))<1e-4f,"configured 0.1 scale snap");SyntheticEditorInput::Button(0,false);app.Run();ui.UndoEdit(app);ui.SetSnapping(false);
 }
}
static void Capture(const std::string& phase){
 Application app;Check(app.Initialize(),"DX12 app initialization");glfwHideWindow(dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle());SyntheticEditorInput input;
 app.NewScene();auto& ui=app.GetEditorLayer();ui.SetFixedDiagnosticLayout(true);ui.SetGameView(false);ui.SetLightGizmosVisible(false);ui.SetGizmoOperation(2);ui.SetSnapping(false);
 auto cube=ui.CreatePrimitive(app,"cube");auto& t=*app.GetWorld().GetComponent<ecs::TransformComponent>(cube);t.position={0,0,5};t.rotation={.5f,.6f,.3f};t.scale={-1,2,.7f};
 auto output=AssetPaths::GetExecutableDirectory().parent_path().parent_path()/"diagnostics/scale-planes";std::filesystem::create_directories(output);
 Check(app.SetEditorCameraPose({0,0,-6},0,0),"camera");app.SetFrameCaptureOutput((output/(phase+"-front.ppm")).string());app.SetFrameLimit(40);Check(app.Run()==0,"front capture");
 Check(app.SetEditorCameraPose({6,4,0},std::atan2(-6.f,5.f),-std::atan2(4.f,std::sqrt(61.f))),"oblique camera");app.SetFrameCaptureOutput((output/(phase+"-oblique.ppm")).string());app.SetFrameLimit(10);Check(app.Run()==0,"oblique capture");
 if(phase!="before"){VerifyDrawnPlanes(app,t);Interact(app,cube);}
 auto* renderer=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());Check(renderer->GetValidationErrorCount()==0,"DX12 validation");
}
int main(int argc,char** argv){ScopedEditorPreferenceRestore preferences;try{if(argc<=1)GeometryOracle();Capture(argc>1?argv[1]:"after");std::cout<<"Scale plane native regression passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
