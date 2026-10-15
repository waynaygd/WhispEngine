#include "ecs/RenderInterpolation.h"
#include "ecs/PhysicsStressScene.h"
#include "ecs/systems/RenderSystem.h"
#include "core/FixedStepClock.h"
#include "resources/ResourceManager.h"
#include "render/IRenderAdapter.h"
#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <cstring>
#include <limits>
#if !defined(TRACY_ENABLE)
#define ZoneScopedN(name) ((void)0)
#endif

using namespace ecs;
static void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static bool Near(float a,float b,float eps=2e-5f){return std::abs(a-b)<eps;}
static Entity Body(World& w,Vec3 p={}) {
    auto e=w.CreateEntity();w.AddComponent<TransformComponent>(e).position=p;
    w.AddComponent<RigidbodyComponent>(e);w.AddComponent<ColliderComponent>(e);return e;
}
static void MathAndLifecycle() {
    World w;auto e=Body(w,{2,3,4});RenderInterpolation i;
    i.SetFrame(w,true,0);i.BeginTick(w);
    auto& t=*w.GetComponent<TransformComponent>(e);t.position={6,7,8};t.rotation={0,0,3.14159265359f};
    i.EndTick(w);
    for(float a:{0.f,.25f,.5f,.75f,.99999f}) {
        i.SetFrame(w,true,a);auto p=i.Resolve(e,t);float m[16];BuildRenderModelMatrix(m,p);
        Check(Near(p.position.x,2+4*a)&&Near(p.position.y,3+4*a),"position lerp");
        // Pi is ambiguous at float precision; compare absolute half-circle angle.
        Check(Near(m[0],std::cos(3.14159265359f*a))&&Near(std::abs(m[1]),std::sin(3.14159265359f*a)),"rotation slerp");
        Check(t.position.x==6&&t.rotation.z==3.14159265359f,"render wrote physics Transform");
    }
    auto q=RenderRotation({.2f,.4f,-.7f}),neg=RenderQuaternion{-q.x,-q.y,-q.z,-q.w};
    auto s=RenderSlerp(q,neg,.5f);Check(Near(std::abs(q.x*s.x+q.y*s.y+q.z*s.z+q.w*s.w),1),"antipodal quaternion");
    s=RenderSlerp(q,q,.75f);Check(Near(s.w,q.w),"equal/near quaternion");
    {
        World scaledWorld;auto scaled=Body(scaledWorld);RenderInterpolation scaleHistory;
        scaleHistory.BeginTick(scaledWorld);auto& scaleTransform=*scaledWorld.GetComponent<TransformComponent>(scaled);
        scaleTransform.scale={2,3,4};scaleHistory.EndTick(scaledWorld);scaleHistory.SetFrame(scaledWorld,true,.5);
        auto pose=scaleHistory.Resolve(scaled,scaleTransform);
        Check(pose.scale.x==1.5f&&pose.scale.y==2&&pose.scale.z==2.5f&&scaleTransform.scale.z==4,"scale lerp wrote physics scale");
    }
    // Independent legacy Euler Rz*Ry*Rx basis, including non-uniform scale.
    TransformComponent legacy;legacy.rotation={.2f,.4f,-.7f};legacy.scale={2,3,4};float m[16];BuildRenderModelMatrix(m,RenderPose::From(legacy));
    const auto r=legacy.rotation;const float cx=std::cos(r.x),sx=std::sin(r.x),cy=std::cos(r.y),sy=std::sin(r.y),cz=std::cos(r.z),sz=std::sin(r.z);
    const float expected[9]={cz*cy,sz*cy,-sy,cz*sy*sx-sz*cx,sz*sy*sx+cz*cx,cy*sx,cz*sy*cx+sz*sx,sz*sy*cx-cz*sx,cy*cx};
    for(int col=0;col<3;++col)for(int row=0;row<3;++row)Check(Near(m[col*4+row],expected[col*3+row]*(col==0?2:col==1?3:4)),"matrix convention changed");
    // Wrap across +/-Pi follows the short path, not Euler lerp through zero.
    t.rotation={0,0,3.12413936f};i.ResetEntity(w,e);i.BeginTick(w);t.rotation.z=-3.12413936f;i.EndTick(w);i.SetFrame(w,true,.5);
    BuildRenderModelMatrix(m,i.Resolve(e,t));Check(m[0]<-.999f,"Euler wrap long path");
    t.position={100,0,0};i.SetFrame(w,true,.25);Check(i.Resolve(e,t).position.x==100,"Inspector edit smeared");
    i.BeginTick(w);t.position.x=101;i.EndTick(w);i.ResetEntity(w,e);i.SetFrame(w,true,.25);Check(i.Resolve(e,t).position.x==101,"explicit teleport history");
    i.SetFrame(w,false,.5);Check(i.Resolve(e,t).position.x==101&&i.Count()==0,"Stop residual interpolation");
    i.Clear();i.SetFrame(w,true,.7);Check(i.Resolve(e,t).position.x==101,"Resume/Restart stale pose");
    auto old=e;w.DestroyEntity(e);i.SetFrame(w,true,.5);Check(i.Count()==0,"destroyed snapshot retained");
    e=Body(w,{55,0,0});Check(e.index==old.index&&e.generation!=old.generation,"generation setup");
    i.SetFrame(w,true,.2);Check(i.Resolve(e,*w.GetComponent<TransformComponent>(e)).position.x==55,"recycled/new entity flew from old pose");
    w.GetComponent<RigidbodyComponent>(e)->isStatic=true;i.SetFrame(w,true,.5);Check(i.Count()==0,"static interpolated");
    w.GetComponent<RigidbodyComponent>(e)->isStatic=false;w.GetComponent<RigidbodyComponent>(e)->sleeping=true;i.SetFrame(w,true,.5);
    i.BeginTick(w);i.EndTick(w);Check(i.Resolve(e,*w.GetComponent<TransformComponent>(e)).position.x==55,"stationary pose");
    w.RemoveComponent<RigidbodyComponent>(e);i.SetFrame(w,true,.5);Check(i.Count()==0,"removed Rigidbody history");
    w.Clear();i.SetFrame(w,true,.5);Check(i.Count()==0,"World.Clear history");
    e=Body(w,{77,0,0});i.SetFrame(w,true,.5);Check(i.Resolve(e,*w.GetComponent<TransformComponent>(e)).position.x==77,"World.Clear generation reuse");
    for(double alpha:{-1.,1.,2.,std::numeric_limits<double>::quiet_NaN()}) {i.SetFrame(w,true,alpha);Check(i.Alpha()>=0&&i.Alpha()<1,"alpha bound");}
    FixedStepClock clock;for(double dt:{.003,.025,.006,.05,2.,.004}) {
        const auto plan=clock.Advance(dt);Check(plan.ticks<=4,"tick limit");
        for(int tick=0;tick<plan.ticks;++tick){i.BeginTick(w);w.GetComponent<TransformComponent>(e)->position.x+=1;i.EndTick(w);}
        i.SetFrame(w,true,clock.Accumulator()/FixedStepClock::TickSeconds);
        Check(i.Alpha()>=0&&i.Alpha()<1,"irregular/stall alpha");
    }
    std::cout<<"Interpolation math, lifecycle, edit, generation and stall passed\n";
}

struct RecordingAdapter:IRenderAdapter {
    std::array<float,16> last{},mesh{},collider{};int meshes=0,colliders=0;
    bool Initialize(IWindow*)override{return true;}void BeginFrame()override{}void Clear(float,float,float,float)override{}
    void SetTestTransform(const float* p)override{std::copy(p,p+16,last.begin());}void SetTestColor(float,float,float,float)override{}
    RenderMeshHandle UploadMesh(const MeshData&)override{return {1};}RenderTextureHandle CreateTexture2D(const TextureData&)override{return {1};}
    RenderShaderHandle CreateShaderProgram(const ShaderResource&)override{return {1};}
    void DrawMesh(RenderMeshHandle)override{mesh=last;++meshes;}void DrawTestTriangle()override{}void DrawTestLine()override{}void DrawTestQuad()override{}
    void DrawTestCube()override{collider=last;++colliders;}void EndFrame()override{}void Present()override{}void Shutdown()override{}
};
static void RenderPathAndBenchmark() {
    RecordingAdapter adapter;ResourceManager resources(nullptr);
    resources.Load<MeshResource>("models/validation_cube.obj");resources.Load<ShaderResource>("dx12/textured.hlsl");resources.Load<TextureResource>("defaults/texture");
    RenderSystem render;render.SetResourceManager(&resources);render.SetRenderAdapter(&adapter);
    RenderInterpolation history;render.SetInterpolation(&history);
    World w;std::vector<Entity> entities;CreatePhysicsStressScene(w,entities,1);w.DestroyEntity(entities[0]);auto e=entities[1];
    auto& t=*w.GetComponent<TransformComponent>(e);
    history.BeginTick(w);t.position.x+=2;history.EndTick(w);history.SetFrame(w,true,.5);render.SetDebugCollidersEnabled(true);
    for(int frame=0;frame<10;++frame)render.Update(w,0);
    Check(adapter.meshes>0&&adapter.colliders>0,"render resources/packet not submitted");
    for(int n=0;n<16;++n)Check(Near(adapter.mesh[n],adapter.collider[n]),"debug and mesh render pose mismatch");
    const auto interpolated=adapter.mesh;history.SetEnabled(false);render.Update(w,0);const auto physical=adapter.mesh;
    Check(!Near(interpolated[12],physical[12]),"ON/OFF did not reach submitted MVP");
    history.SetEnabled(true);render.SetPhysicsDebugPose(true);render.Update(w,0);
    for(int n=0;n<16;++n)Check(Near(adapter.collider[n],physical[n]),"Physics Debug Pose mismatch");
    // Two camera/window submissions consume the same history without advancing it.
    const TransformComponent physicalBefore=t;
    const auto poseBefore=history.Resolve(e,t);
    render.SetCameraProjection(1.047f,1.5f,.01f,100);render.Update(w,0);render.SetCameraProjection(1.047f,2.f,.01f,100);render.Update(w,0);
    Check(t.position.x==physicalBefore.position.x&&t.position.y==physicalBefore.position.y&&t.position.z==physicalBefore.position.z&&
          t.rotation.x==physicalBefore.rotation.x&&t.rotation.y==physicalBefore.rotation.y&&t.rotation.z==physicalBefore.rotation.z&&
          t.scale.x==physicalBefore.scale.x&&t.scale.y==physicalBefore.scale.y&&t.scale.z==physicalBefore.scale.z,"render changed transform");
    Check(history.Resolve(e,t).position.x==poseBefore.position.x,"second window advanced pose history");
    w.DestroyEntity(e);history.SetFrame(w,true,.5);adapter.meshes=0;adapter.colliders=0;render.Update(w,0);Check(adapter.meshes==0&&adapter.colliders==0,"deleted object submitted");
    render.SetDebugCollidersEnabled(false);
    for(int count:{100,250,500,1000}) {
        CreatePhysicsStressScene(w,entities,count);history.Clear();history.BeginTick(w);
        for(auto entity:entities)if(!w.GetComponent<RigidbodyComponent>(entity)->isStatic) {
            w.GetComponent<TransformComponent>(entity)->position.x+=.1f;
            w.GetComponent<TransformComponent>(entity)->rotation={.2f,.3f,.4f};
        }
        history.EndTick(w);history.SetFrame(w,true,.5);
        Check(history.Count()==std::size_t(count),"stress interpolated count");
        for(int warm=0;warm<10;++warm)render.Update(w,0);
        const auto growth=history.BufferGrowths();
        for(bool enabled:{false,true,false,true}) {
            history.SetEnabled(enabled);double resolveNs=0,renderNs=0,syncNs=0;
            for(int sample=0;sample<300;++sample) {
                auto measure=[&] {
                auto start=std::chrono::steady_clock::now();history.SetFrame(w,true,.5);auto synced=std::chrono::steady_clock::now();
                float checksum=0;
                {ZoneScopedN("RenderInterpolationBenchmark");for(auto entity:entities)checksum+=history.Resolve(entity,*w.GetComponent<TransformComponent>(entity)).position.x;}
                auto resolved=std::chrono::steady_clock::now();render.Update(w,0);auto end=std::chrono::steady_clock::now();
                Check(std::isfinite(checksum),"resolve checksum");
                syncNs+=std::chrono::duration<double,std::nano>(synced-start).count();resolveNs+=std::chrono::duration<double,std::nano>(resolved-synced).count();renderNs+=std::chrono::duration<double,std::nano>(end-resolved).count();
                };
                if(enabled) { ZoneScopedN("RenderComparisonOn");measure(); }
                else { ZoneScopedN("RenderComparisonOff");measure(); }
            }
            std::cout<<"render benchmark count="<<count<<" enabled="<<enabled<<" sync_us="<<syncNs/300000<<" resolve_us="<<resolveNs/300000<<" render_us="<<renderNs/300000<<'\n';
        }
        Check(history.BufferGrowths()==growth,"steady visual frames grew snapshot storage");
        ClearPhysicsStressScene(w,entities);history.SetFrame(w,true,.5);Check(history.Count()==0,"stress cleanup history");
    }
    render.ReleaseGpuResources();render.SetRenderAdapter(nullptr);
    std::cout<<"Shared render MVP/debug/resource/lifecycle path passed\n";
}
int main(int argc,char** argv){try{
#if defined(TRACY_ENABLE)
    if(argc>1&&std::strcmp(argv[1],"--wait-tracy")==0) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
        while(!tracy::GetProfiler().IsConnected()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(20));
        Check(tracy::GetProfiler().IsConnected(),"Tracy connection timeout");
    }
#endif
    MathAndLifecycle();RenderPathAndBenchmark();return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
