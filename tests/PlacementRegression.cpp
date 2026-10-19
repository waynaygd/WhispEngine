#define NOMINMAX
#include "EditorTestPreferences.h"
#include "core/Application.h"
#include "core/AssetPaths.h"
#include "editor/PlacementMath.h"
#include "editor/LightGizmoMath.h"
#include "resources/ResourceManager.h"
#include "ecs/components/SceneIdentityComponent.h"
#include "scene/SceneSerializer.h"
#include "platform/GlfwWindow.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include <GLFW/glfw3.h>
#include <fstream>
#include <iostream>
using namespace ecs;using namespace editor;
static unsigned checks=0;
static void Check(bool value,const char* label){if(!value)throw std::runtime_error(label);++checks;}
static void Near(float a,float b,const char* label){Check(std::abs(a-b)<.003f,label);}
static float Bottom(const PlacementBounds& b,const TransformComponent& t){float y=INFINITY;for(auto p:PlacementCorners(b,t))y=std::min(y,p.y);return y;}
static void Contact(const PlacementBounds& b,const TransformComponent& t,Vec3 point,Vec3 normal){float lowest=INFINITY;for(auto p:PlacementCorners(b,t))lowest=std::min(lowest,(p.x-point.x)*normal.x+(p.y-point.y)*normal.y+(p.z-point.z)*normal.z);Near(lowest,0,"bounds contact the surface without penetration");}
int main(){ScopedEditorPreferenceRestore preferences;try{
 EditorPreferences prefs;prefs.surfaceSnap=false;prefs.alignToNormal=true;Check(PreferencesFromJson(PreferencesToJson(prefs))==prefs,"placement preferences roundtrip");auto legacy=PreferencesToJson(prefs);legacy.erase("surfaceSnap");legacy.erase("alignToNormal");Check(PreferencesFromJson(legacy).surfaceSnap&&!PreferencesFromJson(legacy).alignToNormal,"legacy preference defaults");
 Application app;Check(app.Initialize(),"initialize editor");glfwHideWindow(dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle());app.NewScene();auto& ui=app.GetEditorLayer();auto* rm=app.GetResourceManager();auto& world=app.GetWorld();
 ui.SetDiagnosticViewport(640,480);ui.SetSurfaceSnap(true);ui.SetAlignToNormal(false);ui.SetSnapping(false);
 auto cube=rm->Load<MeshResource>("models/primitives/cube.obj");auto plane=rm->Load<MeshResource>("models/primitives/plane.obj");Check(cube->IsLoaded()&&plane->IsLoaded(),"small mesh fixtures");const auto bounds=MeshPlacementBounds(cube->GetData().meshData);
 auto floor=[&]{auto e=ui.CreatePrimitive(app,"plane");world.GetComponent<TransformComponent>(e)->scale={20,1,20};return e;};auto ground=floor();auto ramp=ui.CreatePrimitive(app,"plane");auto& rampTransform=*world.GetComponent<TransformComponent>(ramp);rampTransform.position={3,2,3};rampTransform.rotation.z=.45f;rampTransform.scale={5,1,5};
 app.SetEditorCameraPose({0,8,-10},0,-.45f);app.SetFrameLimit(5);app.Run();
 auto uv=[&](Vec3 p){auto camera=GizmoCamera(app.GetCameraPosition(),app.GetCameraYaw(),app.GetCameraPitch(),app.GetCameraVerticalFovRadians(),4.f/3,app.GetCameraNearPlane(),app.GetCameraFarPlane());auto q=ProjectGizmo(p,camera);Check(bool(q),"placement target projects");return *q;};
 auto q=uv({-3,0,1});auto original=SceneSerializer::CaptureWorld(world,app.GetSceneSettings());auto undo=ui.UndoCount();
 auto preview=ui.UpdatePlacementPreview(app,"models/primitives/cube.obj",q[0],q[1]);Check(preview.ready&&preview.surface,"cube surface preview");Contact(bounds,preview.transform,{-3,0,1},{0,1,0});Near(preview.transform.rotation.z,0,"default orientation preserved");
 Check(SceneSerializer::CaptureWorld(world,app.GetSceneSettings())==original&&ui.UndoCount()==undo,"preview is outside World and Undo");
 auto created=ui.ConfirmPlacement(app);Check(created.IsValid()&&ui.UndoCount()==undo+1,"placement creates one entity and one Undo");Near(Bottom(bounds,*world.GetComponent<TransformComponent>(created)),0,"cube rests on floor");ui.UndoEdit(app);Check(!world.IsAlive(created),"placement Undo");ui.RedoEdit(app);Near(Bottom(bounds,*world.GetComponent<TransformComponent>(ui.GetSelectedEntity())),0,"placement Redo retains final Transform");
 // An offset pivot must neither sink nor shift the visible model away from the cursor.
 const auto key="models/p03-"+NewSceneIdentity()+".obj";auto path=AssetPaths::ResolveAssetOutputPath(key);
 struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove(path,ec);}} cleanup{path};
 {std::ofstream out(path);for(auto& v:cube->GetData().meshData.vertices)out<<"v "<<v.position[0]+2<<" "<<v.position[1]+1<<" "<<v.position[2]<<"\n";auto& indices=cube->GetData().meshData.indices;for(std::size_t i=0;i+2<indices.size();i+=3)out<<"f "<<indices[i]+1<<" "<<indices[i+1]+1<<" "<<indices[i+2]+1<<"\n";}
 const auto loadingCount=world.GetAliveCount();preview=ui.UpdatePlacementPreview(app,key,.5f,.5f);Check(!preview.ready&&preview.status=="Loading model...","new preview requests async loading");Check(!ui.ConfirmPlacement(app).IsValid()&&world.GetAliveCount()==loadingCount,"loading model cannot create an empty entity");rm->DrainProjectLoads();
 auto shifted=rm->Get<MeshResource>(key);Check(shifted&&shifted->IsLoaded(),"offset-pivot model loaded");auto shiftedBounds=MeshPlacementBounds(shifted->GetData().meshData);q=uv({-5,0,1});preview=ui.UpdatePlacementPreview(app,key,q[0],q[1]);Check(preview.ready&&preview.surface,"offset preview");Contact(shiftedBounds,preview.transform,{-5,0,1},{0,1,0});Near(preview.transform.position.x+2,-5,"offset bounds centered on contact");
 q=uv({3,2,3});auto hit=ui.RaycastPlacement(app,{3,10,3},{0,-1,0},20);Check(hit&&hit->entity==ramp,"nearest triangle on rotated ramp");Near(hit->normal.x,-std::sin(.45f),"world normal includes rotation");
 preview=ui.UpdatePlacementPreview(app,"models/primitives/cube.obj",q[0],q[1]);Check(preview.ready&&preview.surface,"unaligned ramp preview");Contact(bounds,preview.transform,hit->position,hit->normal);Near(preview.transform.rotation.z,0,"ramp preserves orientation unless enabled");
 ui.SetAlignToNormal(true);preview=ui.UpdatePlacementPreview(app,"models/primitives/cube.obj",q[0],q[1]);auto matrix=TransformMatrix(preview.transform);Near(float(matrix[4]),hit->normal.x,"aligned up X");Near(float(matrix[5]),hit->normal.y,"aligned up Y");Contact(bounds,preview.transform,hit->position,hit->normal);
 auto& rt=*world.GetComponent<TransformComponent>(ramp);rt.scale.x=-5;hit=ui.RaycastPlacement(app,{3,10,3},{0,-1,0},20);Check(hit&&hit->normal.y>0,"mirrored surface normal faces ray");
 const auto triangleKey="models/p03-triangle-"+NewSceneIdentity()+".obj";auto trianglePath=AssetPaths::ResolveAssetOutputPath(triangleKey);Cleanup triangleCleanup{trianglePath};
 {std::ofstream out(trianglePath);out<<"v -1 0 -1\nv 1 0 -1\nv -1 0 1\nf 1 2 3\n";}
 Check(rm->Load<MeshResource>(triangleKey)->IsLoaded(),"triangle fixture loaded");auto triangle=ui.CreateModelEntity(app,triangleKey);world.GetComponent<TransformComponent>(triangle)->position={-5,3,4};
 auto hole=ui.RaycastPlacement(app,{-4.3f,10,4.7f},{0,-1,0},20);Check(hole&&hole->entity==ground,"surface ray ignores empty area inside triangle AABB");
 auto wall=ui.CreatePrimitive(app,"plane");auto& wallT=*world.GetComponent<TransformComponent>(wall);wallT.position={7,3,0};wallT.rotation.z=1.5707963268f;wallT.scale={4,1,4};
 auto wallHit=ui.RaycastPlacement(app,{4,3,0},{1,0,0},10);Check(wallHit&&wallHit->entity==wall,"exact query supports vertical surfaces");Near(wallHit->normal.x,-1,"wall normal faces ray");
 auto wallPose=ContactPlacement(bounds,{},wallHit->position,wallHit->normal,true);Contact(bounds,wallPose,wallHit->position,wallHit->normal);Near(float(TransformMatrix(wallPose)[4]),-1,"alignment handles vertical normal");
 ui.SetSurfaceSnap(false);preview=ui.UpdatePlacementPreview(app,"models/primitives/cube.obj",q[0],q[1]);Check(preview.ready&&!preview.surface&&preview.status.find("Ground plane")!=std::string::npos,"Surface Snap off uses explicit fallback");Near(Bottom(bounds,preview.transform),0,"ground fallback accounts for bounds");
 ui.SetSurfaceSnap(true);ui.SetSnapping(true);q=uv({-2.3f,0,1.7f});preview=ui.UpdatePlacementPreview(app,"models/primitives/cube.obj",q[0],q[1]);Near(preview.transform.position.x,std::round(preview.transform.position.x),"grid X on surface");Near(preview.transform.position.z,std::round(preview.transform.position.z),"grid Z on surface");
 preview=ui.UpdatePlacementPreview(app,"models/primitives/cube.obj",.5f,0);Check(preview.ready&&!preview.surface&&preview.status.find("Camera distance")!=std::string::npos,"empty area has finite camera-distance fallback");
 auto identity=rm->Get<MeshResource>("models/primitives/cube.obj");for(int i=0;i<5;++i)ui.UpdatePlacementPreview(app,"models/primitives/cube.obj",.5f,0);Check(identity==rm->Get<MeshResource>("models/primitives/cube.obj"),"preview reuses ResourceManager asset");ui.CancelPlacement();Check(!ui.GetPlacementPreview().active,"cancel preview");
 const auto alive=world.GetAliveCount();rm->Load<MeshResource>("models/__p03_missing__.obj");preview=ui.UpdatePlacementPreview(app,"models/__p03_missing__.obj",.5f,.5f);Check(!preview.ready&&!ui.ConfirmPlacement(app).IsValid()&&world.GetAliveCount()==alive,"failed asset never creates empty entity");
 app.NewScene();ground=floor();std::vector<Entity> selected;std::vector<TransformComponent> before;
 for(int i=0;i<3;++i){auto e=i==2?ui.CreateEntity(app,"No Mesh fallback"):ui.CreatePrimitive(app,"cube");auto& t=*world.GetComponent<TransformComponent>(e);t.position={-3,float(6+i*3),1};t.rotation={.1f,.2f,.3f};t.scale={i==0?-2.f:1.f,1.5f,1};selected.push_back(e);before.push_back(t);}
 ui.SelectViewportEntity(app,selected[0],false,false);ui.SelectViewportEntity(app,selected[1],true,false);ui.SelectViewportEntity(app,selected[2],true,false);undo=ui.UndoCount();Check(ui.DropSelectionToFloor(app)&&ui.UndoCount()==undo+1,"three selected objects drop in one operation");
 for(int i=0;i<3;++i){auto& t=*world.GetComponent<TransformComponent>(selected[i]);Near(Bottom(bounds,t),0,"group ignores other selected objects and rests on floor");Near(t.position.x,before[i].position.x,"Drop preserves horizontal X");Near(t.position.z,before[i].position.z,"Drop preserves horizontal Z");Near(t.rotation.z,before[i].rotation.z,"Drop preserves orientation");}
 ui.UndoEdit(app);for(int i=0;i<3;++i)Near(world.GetComponent<TransformComponent>(selected[i])->position.y,before[i].position.y,"group Drop Undo");ui.RedoEdit(app);for(auto e:selected)Near(Bottom(bounds,*world.GetComponent<TransformComponent>(e)),0,"group Drop Redo");
 auto tilted=ui.CreatePrimitive(app,"plane");auto& tiltedT=*world.GetComponent<TransformComponent>(tilted);tiltedT.position={3,2,3};tiltedT.rotation.z=.45f;tiltedT.scale={5,1,5};
 auto onSlope=ui.CreatePrimitive(app,"cube");world.GetComponent<TransformComponent>(onSlope)->position={3,8,3};Check(ui.DropSelectionToFloor(app),"Drop onto sloped surface");Contact(bounds,*world.GetComponent<TransformComponent>(onSlope),{3,2,3},{-std::sin(.45f),std::cos(.45f),0});Near(world.GetComponent<TransformComponent>(onSlope)->position.x,3,"sloped Drop preserves X");
 auto unsupported=ui.CreatePrimitive(app,"cube");world.GetComponent<TransformComponent>(unsupported)->position={40,8,0};auto count=ui.UndoCount();Check(!ui.DropSelectionToFloor(app)&&ui.UndoCount()==count,"no floor leaves object and history unchanged");Near(world.GetComponent<TransformComponent>(unsupported)->position.y,8,"no floor keeps height");
 auto document=SceneSerializer::CaptureWorld(world,app.GetSceneSettings());auto scene="scenes/p03-"+NewSceneIdentity()+".json";std::string error;Check(app.SaveSceneAs(scene,false,&error),"save placement scene");Cleanup sceneCleanup{AssetPaths::ResolveAssetOutputPath(scene)};app.NewScene();Check(app.OpenScene(scene,&error)&&SceneSerializer::CaptureWorld(world,app.GetSceneSettings())==document,"placement disk roundtrip");
 ui.SelectViewportEntity(app,world.Entities().back(),false,false);app.SetEditorPlayMode(true);Check(!ui.DropSelectionToFloor(app)&&!ui.ConfirmPlacement(app).IsValid(),"Play blocks authoring placement");app.SetEditorPlayMode(false);Check(SceneSerializer::CaptureWorld(world,app.GetSceneSettings())==document,"Runtime leaves authoring placements unchanged");
 Check(dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter())->GetValidationErrorCount()==0,"DX12 validation zero");app.Shutdown();std::cout<<"Placement: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"PLACEMENT FAILED: "<<error.what()<<'\n';return 1;}}
