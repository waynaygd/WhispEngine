#define NOMINMAX
#include "core/Application.h"
#include "ecs/LightGather.h"
#include "ecs/PhysicsStressScene.h"
#include "ecs/systems/RenderSystem.h"
#include "editor/LightGizmoMath.h"
#include "scene/SceneSerializer.h"
#include "scene/LightSerialization.h"
#include "resources/ResourceManager.h"
#include <fstream>
#include <iostream>
#include <numbers>
#include <stdexcept>
#if defined(ENABLE_DX12)
#include "platform/GlfwWindow.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include <GLFW/glfw3.h>
#endif
static void Check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
static void Near(double a,double b,double t,const char* m){if(!std::isfinite(a)||std::abs(a-b)>t){std::cerr<<m<<" got "<<a<<" expected "<<b<<'\n';throw std::runtime_error(m);}}
static void Cpu()
{
    Application app;auto& editor=app.GetEditorLayer();auto& w=app.GetWorld();
    auto point=editor.CreateLight(app,LightType::Point);Check(w.IsAlive(point)&&w.HasComponent<ecs::PointLightComponent>(point),"editor create point");
    Check(!w.HasComponent<ecs::MeshRendererComponent>(point)&&!w.HasComponent<ecs::RigidbodyComponent>(point),"light cannot become physics body/caster");
    editor.RecordEdit(app,"intensity");w.GetComponent<ecs::PointLightComponent>(point)->intensity=17;
    editor.UndoEdit(app);Near(w.GetComponent<ecs::PointLightComponent>(point)->intensity,2,0,"Inspector Undo");
    editor.RedoEdit(app);Near(w.GetComponent<ecs::PointLightComponent>(point)->intensity,17,0,"Inspector Redo");
    editor.DeleteSelectedLight(app);Check(!w.IsAlive(point),"editor delete");
    editor.UndoEdit(app);auto restored=editor.GetSelectedEntity();Check(w.IsAlive(restored)&&restored!=point,"delete undo generation");
    Near(w.GetComponent<ecs::PointLightComponent>(restored)->intensity,17,0,"delete undo data");
    editor.RedoEdit(app);Check(!w.IsAlive(restored),"delete redo");
    editor.UndoEdit(app);auto live=editor.GetSelectedEntity();Check(w.IsAlive(live),"undo restore again");
    auto spot=editor.CreateLight(app,LightType::Spot);editor.UndoEdit(app);Check(!w.IsAlive(spot),"create undo");editor.RedoEdit(app);spot=editor.GetSelectedEntity();Check(w.HasComponent<ecs::SpotLightComponent>(spot),"create redo");
    auto dir=editor.CreateLight(app,LightType::Directional);auto& d=*w.GetComponent<ecs::DirectionalLightComponent>(dir);d.primary=true;d.intensity=7;
    auto& t=*w.GetComponent<ecs::TransformComponent>(dir);t.rotation={0,float(std::numbers::pi/2),0};t.scale={-9,2,4};
    auto camera=editor::GizmoCamera({0,0,-3},0,0,1.04719755f,4.f/3,.01f,100);
    LightingSettings legacy;legacy.directional.intensity=.3f;
    auto gather=ecs::GatherSceneLights(w,legacy,camera);Check(gather.diagnostics.primaryEcs&&gather.diagnostics.total==3&&gather.diagnostics.influencing==3,"light gather identities");Near(gather.lighting.directional.direction[0],1,1e-6,"Transform local +Z rays");Near(gather.lighting.directional.intensity,7,0,"ECS primary migration");
    auto second=editor.CreateLight(app,LightType::Directional);w.GetComponent<ecs::DirectionalLightComponent>(second)->primary=true;
    Check(ecs::GatherSceneLights(w,legacy,camera).diagnostics.primaryIndex==dir.index,"deterministic primary tie");
    w.GetComponent<ecs::DirectionalLightComponent>(dir)->enabled=false;
    Check(ecs::GatherSceneLights(w,legacy,camera).diagnostics.primaryIndex==second.index,"disabled primary replacement");
    w.GetComponent<ecs::DirectionalLightComponent>(second)->enabled=false;
    auto fallback=ecs::GatherSceneLights(w,legacy,camera);Check(!fallback.diagnostics.primaryEcs,"legacy fallback only without active ECS directional");Near(fallback.lighting.directional.intensity,.3,1e-7,"legacy intensity");
    LightParameters p;p.intensity=-1;p.range=NAN;p.innerAngle=INFINITY;p.outerAngle=-2;p.color={-1,NAN,INFINITY};p.attenuation=0;
    p=SanitizeLight(p);Check(p.range>0&&p.intensity==0&&p.innerAngle<=p.outerAngle&&p.attenuation>0,"numeric sanitation");
    p={};Near(LocalLightAttenuation(1,p),std::pow(1-std::pow(1./5,4),2),1e-6,"inverse square smooth range");
    Check(LocalLightAttenuation(5,p)==0&&LocalLightAttenuation(2,p)<LocalLightAttenuation(1,p),"range cutoff");
    Near(SpotLightAttenuation(1,p),1,0,"inner spot");Near(SpotLightAttenuation(.5f,p),0,0,"outer spot");
    float middle=(std::cos(20*.0174532925f)+std::cos(30*.0174532925f))*.5f;Near(SpotLightAttenuation(middle,p),.5,1e-5,"spot smoothstep");
    std::filesystem::create_directories("light-system-readback");std::string error;
    Check(SceneSerializer::SaveWorld("light-system-readback/scene.json",w,&error),"scene save lights");
    std::vector<EcsDemoEntityConfig> loaded;Check(SceneSerializer::LoadEntityConfigs("light-system-readback/scene.json",loaded,&error)&&loaded.size()==4,"scene light roundtrip count");
    for(auto& e:loaded)Check(e.light.has_value(),"light-only serialization");
    Check(loaded[0].light->type==LightType::Point&&loaded[0].light->parameters.intensity==17,"serialized point intensity");
    Check(loaded[2].light->type==LightType::Directional&&loaded[2].light->primary&&loaded[2].rotation.y==t.rotation.y,"serialized directional primary/rotation");
    Check(LightFromJson({{"type","spot"},{"range",-1},{"innerAngle",80},{"outerAngle",10}}).parameters.innerAngle==10,"JSON sanitized angles");
    bool invalid=false;try{LightFromJson({{"type","unknown"}});}catch(const std::exception&){invalid=true;}Check(invalid,"invalid light type diagnostic");
    {ecs::World animated;auto e=animated.CreateEntity();auto& pose=animated.AddComponent<ecs::TransformComponent>(e);animated.AddComponent<ecs::PointLightComponent>(e);animated.AddComponent<ecs::RigidbodyComponent>(e);animated.AddComponent<ecs::ColliderComponent>(e);ecs::RenderInterpolation history;history.BeginTick(animated);pose.position.x=2;history.EndTick(animated);history.SetFrame(animated,true,.5);auto snapshot=ecs::GatherSceneLights(animated,legacy,camera,&history);Near(snapshot.lights[0].positionRange[0],1,1e-6,"light RenderInterpolation pose");Near(pose.position.x,2,0,"light gather cannot change physics transform");}
    auto projected=editor::ProjectGizmo({0,0,0},camera);Check(projected&&editor::HitLightIcon(160,120,(*projected)[0],(*projected)[1],320,240),"gizmo projection/picking");
    Check(!editor::ProjectGizmo({0,0,-4},camera),"behind-camera icon");
    app.SetEditorPlayMode(true);app.SetEditorPlayMode(false);Check(w.IsAlive(live),"Play Stop retain scene lights");
    app.ClearPhysicsStressScene();Check(w.GetAliveCount()==0,"Clear removes lights");
    editor.UndoEdit(app);Check(w.GetAliveCount()==0,"Clear invalidates stale Undo history");
    {Application other;auto& ed=other.GetEditorLayer();auto& ow=other.GetWorld();auto e=ed.CreateLight(other,LightType::Point);ed.RecordEdit(other,"edit before deletion");ow.GetComponent<ecs::PointLightComponent>(e)->intensity=9;ed.DeleteSelectedLight(other);ed.UndoEdit(other);auto restoredId=ed.GetSelectedEntity();ed.UndoEdit(other);Check(ow.GetAliveCount()==1&&ed.GetSelectedEntity()==restoredId,"Undo remaps earlier records after entity recreation");Near(ow.GetComponent<ecs::PointLightComponent>(restoredId)->intensity,2,0,"Undo pre-delete edit");ed.RedoEdit(other);ed.RedoEdit(other);Check(ow.GetAliveCount()==0,"Redo remapped delete");}
    std::cout<<"CPU ECS lights, generation, primary/fallback, sanitation, attenuation, serialization, editor Undo/Redo/picking passed\n";
}
#if defined(ENABLE_DX12)
using V=std::array<double,3>;
static double Dot(V a,V b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static V Unit(V v){double n=std::sqrt(Dot(v,v));for(auto& x:v)x/=n;return v;}
static V Brdf(double metal,double rough,V N,V v,V l)
{
    double nl=std::max(0.,Dot(N,l)),nv=std::max(0.,Dot(N,v));if(nl<=0||nv<=0)return {};
    auto h=Unit({v[0]+l[0],v[1]+l[1],v[2]+l[2]});double nh=Dot(N,h),vh=Dot(v,h),a2=std::pow(std::max(.04,rough),4);
    double D=a2/(std::numbers::pi*std::pow(1+(a2-1)*nh*nh,2));
    auto lambda=[&](double c){return .5*(std::sqrt(1+a2*(1/(c*c)-1))-1);};
    double G=1/((1+lambda(nv))*(1+lambda(nl))),F0=.04*(1-metal)+metal,F=F0+(1-F0)*std::pow(1-vh,5);
    double value=nl*((1-F)*(1-metal)/std::numbers::pi+D*G*F/(4*nv*nl));return {value,value,value};
}
static std::array<float,16> Multiply(const std::array<float,16>& a,const std::array<float,16>& b)
{
    std::array<float,16> r{};for(int j=0;j<4;++j)for(int i=0;i<4;++i)for(int k=0;k<4;++k)r[j*4+i]+=a[k*4+i]*b[j*4+k];return r;
}
static void Compare(const std::vector<float>& a,const std::vector<float>& b,double tolerance,const char* message)
{
    Check(a.size()==b.size(),message);for(std::size_t i=0;i<a.size();++i)Near(a[i],b[i],tolerance,message);
}
static void Gpu(bool fallbackTest=false)
{
    GlfwWindow window;Check(window.Create(320,240,"Light System regression"),"window");glfwHideWindow(window.GetGlfwHandle());
    Dx12RenderAdapter invalidAdapter;Check(!invalidAdapter.Initialize(nullptr),"invalid initialization safe failure");invalidAdapter.Shutdown();
    Dx12RenderAdapter a;Check(a.Initialize(&window),"DX12 initialize");ResourceManager resources(nullptr);
    auto shader=a.CreateShaderProgram(resources.Load<ShaderResource>("dx12/textured.hlsl")->GetData());Check(shader.IsValid()&&a.SupportsInstancing(shader),"light PSO/reflection");
    TextureData white;white.pixels={255,255,255,255};auto texture=a.CreateTexture2D(white);
    MeshData quad;quad.vertices.resize(4);quad.indices={0,1,2,0,2,3};float xy[4][2]={{-2,1.5},{2,1.5},{2,-1.5},{-2,-1.5}};
    for(int i=0;i<4;++i){auto& v=quad.vertices[i];v.position[0]=xy[i][0];v.position[1]=xy[i][1];v.normal[2]=-1;v.uv[0]=i==1||i==2?1.f:0.f;v.uv[1]=i>=2?1.f:0.f;}
    auto mesh=a.UploadMesh(quad);RenderInstanceData data;data.model={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};data.normal=BuildNormalMatrix(data.model);data.tint=data.baseColor={1,1,1,1};data.surface={.4f,.6f,0,0};
    LightingSettings lighting;lighting.directional.enabled=false;lighting.ambientIntensity=0;
    EnvironmentSettings environment;environment.enabled=true;environment.toneMapping=true;a.SetEnvironmentSettings(environment);
    auto camera=editor::GizmoCamera({0,0,-3},0,0,1.04719755f,4.f/3,.01f,100);data.mvp=Multiply(camera.projection,camera.view);
    std::vector<GpuSceneLight> lights;std::vector<UINT> tiles;std::vector<std::uint8_t> pixels;
    auto frame=[&](LightRenderingMode mode,bool instanced=false){
        for(auto& l:lights)l.screenBounds=ProjectLightBounds(l,camera);
        a.SetSceneLights(lights,mode);a.BeginFrame();a.Clear(0,0,0,1);a.BeginMainPass();
        a.SetMeshRange({});a.BindShader(shader);a.BindTexture(0,texture);a.SetLighting(BuildLightingConstants(lighting));a.SetCameraPosition({0,0,-3,0});a.SetMaterialState({true,false});
        if(instanced)Check(a.DrawMeshInstanced(mesh,std::span(&data,1)),"light instanced draw");
        else {a.SetModelMatrix(data.model);a.SetNormalMatrix(data.normal);a.SetSurfaceParameters(data.surface);a.SetBaseColor(data.baseColor);a.SetTextureParameters(data.textureParameters);a.SetTestTransform(data.mvp.data());a.SetTestColor(1,1,1,1);a.DrawMesh(mesh);}
        a.EndMainPass();a.EndFrame();auto linear=a.ReadBackHdrPixels();pixels=a.ReadBackFramePixels();tiles=a.ReadBackLightTiles();a.Present();return linear;
    };
    GpuSceneLight point;point.positionRange={0,0,-1,4};point.directionType={0,0,1,1};point.colorIntensity={1,.5f,.25f,.5f};point.coneAttenuation={std::cos(20*.0174532925f),std::cos(30*.0174532925f),.1f,1};
    const std::size_t center=(120*320+160)*4;
    auto zero=frame(LightRenderingMode::Tiled);Near(zero[center],0,0,"zero lights");
    lights={point};auto nearImage=frame(LightRenderingMode::Forward);
    double x=(2*160.5/320-1)*3/camera.projection[0],y=(1-2*120.5/240)*3/camera.projection[5],distance=std::sqrt(1+x*x+y*y);
    double attenuation=std::pow(1-std::pow(distance/4,4),2)/(distance*distance);
    for(int c=0;c<3;++c)Near(nearImage[center+c],point.colorIntensity[c]*.5*attenuation/distance,2e-5,"point independent Lambert oracle");
    Compare(nearImage,frame(LightRenderingMode::Tiled),1e-5,"one point Forward/Tiled");
    if(fallbackTest){Compare(nearImage,frame(LightRenderingMode::Tiled),1e-5,"persistent compute fallback");Check(!a.GetSceneLightDiagnostics().optimized&&!a.GetSceneLightDiagnostics().error.empty(),"broken compute shader must diagnose Forward fallback");Check(a.GetValidationErrorCount()==0,"fallback DX12 validation");a.DestroyShader(shader);a.DestroyMesh(mesh);a.DestroyTexture(texture);a.Shutdown();std::cout<<"Broken compute shader Forward fallback readback passed\n";return;}
    Check(!tiles.empty(),"GPU tile readback");
    Compare(nearImage,frame(LightRenderingMode::Tiled,true),1e-5,"local ordinary/instance");
    lights[0].positionRange[2]=-2;auto farImage=frame(LightRenderingMode::Tiled);Check(farImage[center]<nearImage[center]*.3,"inverse-square farImage receiver");
    lights[0].positionRange[2]=-5;Near(frame(LightRenderingMode::Tiled)[center],0,0,"outside point range");
    lights={point,point};auto multiple=frame(LightRenderingMode::Tiled);Near(multiple[center],nearImage[center]*2,2e-5,"additive direct lights");
    lights={point};lights[0].positionRange[0]=1;Check(frame(LightRenderingMode::Tiled)[center]<nearImage[center],"point movement");
    lights={point};lights[0].directionType[3]=2;auto inner=frame(LightRenderingMode::Tiled);Near(inner[center],nearImage[center],2e-5,"spot inner cone");
    float angle=25*.0174532925f;lights[0].directionType={std::sin(angle),0,std::cos(angle),2};auto penumbra=frame(LightRenderingMode::Tiled);Check(penumbra[center]>0&&penumbra[center]<inner[center],"spot smooth penumbra");
    lights[0].directionType={1,0,0,2};Near(frame(LightRenderingMode::Tiled)[center],0,0,"spot rotated outside cone");
    lights={point};data.surface[2]=1;auto pbr=frame(LightRenderingMode::Tiled);V N{0,0,-1},view=Unit({-x,-y,-3}),L=Unit({-x,-y,-1});auto expected=Brdf(.4,.6,N,view,L);
    Near(pbr[center],expected[0]*.5*attenuation,1e-4,"point independent Cook-Torrance oracle");Check(std::abs(pbr[center]-nearImage[center])>.01,"PBR cannot be replaced with Lambert");
    for(unsigned count:{0,1,10,50,100,250,500})for(bool dense:{false,true}){
        lights.clear();for(unsigned i=0;i<count;++i){auto l=point;l.colorIntensity[3]=.002f;l.positionRange={dense?0.f:float(int(i%20)-10)*.6f,dense?0.f:float(int(i/20)-10)*.5f,-1,dense?5.f:.7f};if(i%3==0)l.directionType[3]=2;lights.push_back(l);}
        auto forward=frame(LightRenderingMode::Forward);auto tiled=frame(LightRenderingMode::Tiled);Compare(forward,tiled,2e-4,"many lights Forward/Tiled equivalence");Compare(tiled,frame(LightRenderingMode::Tiled,true),2e-4,"many lights ordinary/instance");
        if(dense&&count>256){unsigned overflow=0;for(std::size_t i=1;i<tiles.size();i+=2)overflow+=tiles[i];Check(overflow>0,"overflow diagnostic/fallback");}
        if(!dense&&count==500){unsigned maximum=0;for(std::size_t i=0;i<tiles.size();i+=2)maximum=std::max(maximum,tiles[i]);Check(a.GetSceneLightDiagnostics().maxPerTile<500,"distributed tiles must reduce candidates");}
    }
    lights={point};camera=editor::GizmoCamera({0,0,-3},.15f,.1f,1.04719755f,4.f/3,.01f,100);data.mvp=Multiply(camera.projection,camera.view);
    auto rotated=frame(LightRenderingMode::Forward);Compare(rotated,frame(LightRenderingMode::Tiled),1e-5,"camera rotation culling");
    glfwSetWindowSize(window.GetGlfwHandle(),400,280);glfwPollEvents();auto resized=frame(LightRenderingMode::Forward);Compare(resized,frame(LightRenderingMode::Tiled),1e-5,"viewport resize culling");Check(resized.size()==400*280*4,"resize target size");
    lights.resize(16385,point);auto limited=frame(LightRenderingMode::Tiled);Check(!a.GetSceneLightDiagnostics().error.empty(),"global capacity must be diagnosed");
    Near(limited[(140*400+200)*4],0,0,"capacity failure safe local fallback");
    // Two snapshots recorded before submission: buffer growth must not overwrite page zero.
    lights={point};auto expectedMulti=frame(LightRenderingMode::Forward);
    a.SetSceneLights(lights,LightRenderingMode::Tiled);a.BeginFrame();a.Clear(0,0,0,1);
    auto record=[&](){a.BeginMainPass();a.SetMeshRange({});a.BindShader(shader);a.BindTexture(0,texture);a.SetLighting(BuildLightingConstants(lighting));a.SetCameraPosition({0,0,-3,0});a.SetMaterialState({true,false});Check(a.DrawMeshInstanced(mesh,std::span(&data,1)),"recorded light pass");a.EndMainPass();};
    record();lights.resize(1000,point);for(std::size_t i=1;i<lights.size();++i)lights[i].colorIntensity[3]=0;
    for(auto& l:lights)l.screenBounds=ProjectLightBounds(l,camera);
    a.SetSceneLights(lights,LightRenderingMode::Tiled);record();a.EndFrame();auto recorded=a.ReadBackHdrPixels();a.Present();Compare(recorded,expectedMulti,2e-5,"recorded snapshots/growth before submission");
    // Production ECS/material/IBL/shadow path, not the manual quad path above.
    glfwSetWindowSize(window.GetGlfwHandle(),320,240);glfwPollEvents();
    auto env=resources.Load<EnvironmentResource>("environments/uniform.hdr");Check(env->IsLoaded()&&a.UploadEnvironment(env->GetData(),1),"production IBL upload");
    environment.intensity=.15f;a.SetEnvironmentSettings(environment);
    ecs::World world;ecs::RenderSystem render;ecs::RenderInterpolation interpolation;
    render.SetRenderAdapter(&a);render.SetResourceManager(&resources);render.SetLightingSettings(&lighting);render.SetInterpolation(&interpolation);
    ShadowSettings shadows;shadows.enabled=true;shadows.pcf=false;shadows.edgeFade=0;render.SetShadowSettings(&shadows);
    auto addLights=[&](){
        auto primary=world.CreateEntity();auto& pose=world.AddComponent<ecs::TransformComponent>(primary);pose.rotation={1.1f,-.7f,0};
        auto& d=world.AddComponent<ecs::DirectionalLightComponent>(primary);d.primary=true;d.castShadows=true;d.intensity=.85f;
        for(int i=0;i<2;++i){auto e=world.CreateEntity();auto& t=world.AddComponent<ecs::TransformComponent>(e);t.position={i?3.f:-3.f,10,20};t.rotation={1.5707963f,0,0};
            if(i){auto& p=world.AddComponent<ecs::SpotLightComponent>(e);p.range=60;p.intensity=20;p.outerAngle=70;}
            else {auto& p=world.AddComponent<ecs::PointLightComponent>(e);p.range=60;p.intensity=20;}}
        return primary;
    };
    auto sceneFrame=[&](LightRenderingMode mode,bool instanced,int warmup=1){
        render.SetLightRenderingMode(mode);render.SetInstancingEnabled(instanced);std::vector<float> result;
        for(int i=0;i<warmup;++i){a.BeginFrame();a.Clear(0,0,0,1);render.Update(world,0);a.EndFrame();result=a.ReadBackHdrPixels();pixels=a.ReadBackFramePixels();a.Present();}return result;
    };
    render.SetCameraProjection(.8f,320.f/240,.01f,100);render.SetCameraTransform({0,0,-4},0,0);
    for(const char* key:{"models/pbr_textures.gltf","models/pbr_textures_embedded.gltf","models/material_slots.obj","models/material_slots.gltf"}){
        world.Clear();auto loaded=resources.Load<MeshResource>(key);Check(loaded->IsLoaded(),"imported light model");
        for(const auto& m:loaded->GetData().meshData.materials)for(const auto* path:{&m.texturePath,&m.normalTexturePath,&m.metallicRoughnessTexturePath,&m.occlusionTexturePath})if(!path->empty())resources.Load<TextureResource>(*path);
        for(int i=0;i<2;++i){auto e=world.CreateEntity();auto& t=world.AddComponent<ecs::TransformComponent>(e);t.scale={1.2f,.8f,1};t.rotation={0,.15f,0};world.AddComponent<ecs::MeshRendererComponent>(e).meshPath=key;world.AddComponent<ecs::MaterialComponent>(e).overrides.shadingModel=ShadingModel::PBR;}
        addLights();world.ForEach<ecs::TransformComponent,ecs::PointLightComponent>([](auto,auto& t,auto&){t.position={-2,2,-2};});world.ForEach<ecs::TransformComponent,ecs::SpotLightComponent>([](auto,auto& t,auto&){t.position={2,2,-2};t.rotation={.3f,-.3f,0};});auto ordinary=sceneFrame(LightRenderingMode::Forward,false,16);Compare(ordinary,sceneFrame(LightRenderingMode::Tiled,true),.0002,"imported PBR maps/slots/IBL local equivalence");
    }
    world.Clear();std::vector<ecs::Entity> entities;ecs::CreatePhysicsStressScene(world,entities,500);resources.Load<MeshResource>("models/validation_cube.obj");
    render.SetCameraTransform({22,20,-12},std::atan2(-22.f,42.f),-.35f);render.SetCameraProjection(1.04719755f,320.f/240,.01f,150);
    auto primary=addLights();auto lit=sceneFrame(LightRenderingMode::Tiled,true,16);
    Check(a.GetSubmissionStatistics().drawCalls==1&&a.GetSubmissionStatistics().shadowDrawCalls==1,"500 cube main/shadow batching with ECS lights");
    Compare(lit,sceneFrame(LightRenderingMode::Forward,false),.0002,"500 cubes Forward/instanced Forward+ equality");
    shadows.enabled=false;auto withoutShadow=sceneFrame(LightRenderingMode::Tiled,true);
    auto& primaryLight=*world.GetComponent<ecs::DirectionalLightComponent>(primary);primaryLight.intensity=0;
    auto indirectAndLocal=sceneFrame(LightRenderingMode::Tiled,true);primaryLight.intensity=.85f;shadows.enabled=true;shadows.debugMode=1;
    sceneFrame(LightRenderingMode::Tiled,true);auto mask=pixels;shadows.debugMode=0;
    unsigned changed=0;for(std::size_t i=0;i<lit.size();i+=4)if(std::abs(lit[i]-withoutShadow[i])>1e-5){for(int c=0;c<3;++c)Near(lit[i+c],indirectAndLocal[i+c]+(withoutShadow[i+c]-indirectAndLocal[i+c])*(mask[i]/255.),.004,"shadow must affect primary only, preserve local and IBL");++changed;}
    Check(changed>20,"primary shadow receivers");
    world.DestroyEntity(entities[1]);sceneFrame(LightRenderingMode::Tiled,true);Check(a.GetSubmissionStatistics().renderedInstances==500,"delete object with lights");
    world.Clear();sceneFrame(LightRenderingMode::Tiled,true);Check(a.GetSubmissionStatistics().drawCalls==0,"Clear stale light draws");render.ReleaseGpuResources();
    Check(a.GetValidationErrorCount()==0,"DX12 ERROR/CORRUPTION");a.DestroyShader(shader);a.DestroyMesh(mesh);a.DestroyTexture(texture);a.Shutdown();
    std::cout<<"GPU point/spot/PBR float oracles, 0..500 distributed/dense Forward+ equivalence, overflow, camera/resize/instancing passed\n";
}
#endif
int main(int argc,char** argv){try{Cpu();
#if defined(ENABLE_DX12)
if(argc>1&&(std::string(argv[1])=="--dx12"||std::string(argv[1])=="--dx12-fallback"))Gpu(std::string(argv[1])=="--dx12-fallback");
#endif
return 0;}catch(const std::exception& e){std::cerr<<"LIGHT SYSTEM FAILED: "<<e.what()<<'\n';return 1;}}
