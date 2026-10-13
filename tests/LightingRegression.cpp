#define NOMINMAX
#include "render/Lighting.h"
#include "render/ColorPipeline.h"
#include "ecs/RenderInterpolation.h"
#include "resources/ResourceManager.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <limits>
#include <fstream>
#include <cstring>
#if defined(ENABLE_DX12)
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include "platform/GlfwWindow.h"
#endif
static void Check(bool v,const char* s) { if(!v)throw std::runtime_error(s); }
using V=std::array<float,3>;
static float Dot(V a,V b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static V Unit(V v){float len=std::sqrt(Dot(v,v));for(float& x:v)x/=len;return v;}
static V Normal(const std::array<float,12>& n,V v) {
    return {n[0]*v[0]+n[1]*v[1]+n[2]*v[2],n[4]*v[0]+n[5]*v[1]+n[6]*v[2],n[8]*v[0]+n[9]*v[1]+n[10]*v[2]};
}
static std::array<float,16> Model(ecs::Vec3 rotation={},ecs::Vec3 scale={1,1,1}) {
    ecs::TransformComponent t;t.rotation=rotation;t.scale=scale;auto p=ecs::RenderPose::From(t);
    std::array<float,16> m;ecs::BuildRenderModelMatrix(m.data(),p);return m;
}
static void MathAndImport() {
    LightingSettings s; s.directional.direction={0,-20,0};
    auto c=BuildLightingConstants(s);Check(c.directionEnabled[1]==-1,"direction normalization");
    s.directional.direction={0,0,0};Check(BuildLightingConstants(s).directionEnabled[1]==-1,"zero direction fallback");
    s.directional.direction[0]=std::numeric_limits<float>::infinity();Check(BuildLightingConstants(s).directionEnabled[1]==-1,"nonfinite direction fallback");
    s.directional.enabled=false;Check(BuildLightingConstants(s).colorIntensity[3]==0,"disabled directional");
    s.enabled=false;Check(BuildLightingConstants(s).directionEnabled[3]==0,"unlit flag");
    s.ambientIntensity=-2;Check(BuildLightingConstants(s).ambientIntensity[3]==0,"negative ambient");
    for(auto scale:{ecs::Vec3{1,1,1},ecs::Vec3{2,.5f,3},ecs::Vec3{-2,.5f,3}}) {
        const auto m=Model({.3f,.7f,-.2f},scale);const auto n=BuildNormalMatrix(m);
        // Independent geometric oracle: transformed normal remains perpendicular
        // to both transformed tangents, even under nonuniform/negative scale.
        V normal=Normal(n,{1,1,1});
        for(V t:{V{1,-1,0},V{0,1,-1}}) {
            V world{m[0]*t[0]+m[4]*t[1]+m[8]*t[2],m[1]*t[0]+m[5]*t[1]+m[9]*t[2],m[2]*t[0]+m[6]*t[1]+m[10]*t[2]};
            Check(std::abs(Dot(normal,world))<2e-5f,"normal not perpendicular to transformed tangent");
        }
    }
    Check(BuildNormalMatrix(Model({}, {0,1,1}))==std::array<float,12>{},"singular transform fallback");
    const auto n=BuildNormalMatrix(Model({}, {2,.5f,3}));Check(n[0]==.5f&&n[5]==2.f&&std::abs(n[10]-1.f/3)<1e-6f,"inverse scale");
    ResourceManager resources(nullptr);
    for(const char* key:{"models/african_head.obj","models/lighting_sphere.gltf"}) {
        auto r=resources.Load<MeshResource>(key);Check(r&&r->IsUsable()&&!r->GetData().placeholder,"complex model import failed");
        const auto& mesh=r->GetData().meshData;Check(mesh.vertices.size()>100,"complex geometry missing");
        for(const auto& v:mesh.vertices)Check(std::isfinite(Dot({v.normal[0],v.normal[1],v.normal[2]},{v.normal[0],v.normal[1],v.normal[2]}))&&Dot({v.normal[0],v.normal[1],v.normal[2]},{v.normal[0],v.normal[1],v.normal[2]})>.1f,"imported normal missing");
        if(std::string(key).find("gltf")!=std::string::npos) {
            float minY=1e30f,maxY=-1e30f;
            for(const auto& v:mesh.vertices){minY=std::min(minY,v.position[1]);maxY=std::max(maxY,v.position[1]);}
            Check(std::abs(minY-2.5f)<1e-5f&&std::abs(maxY-3.5f)<1e-5f,"glTF node transform not baked");
            // Fixture is a unit sphere, transformed by Ry(.4)*scale(2,.5,1.5)+translation.
            const float co=std::cos(.4f),si=std::sin(.4f);
            for(const auto& v:mesh.vertices) {
                float x=v.position[0]-2,z=v.position[2]-4;
                V local{(co*x-si*z)/2,(v.position[1]-3)/.5f,(si*x+co*z)/1.5f};
                V expected=Unit({co*local[0]/2+si*local[2]/1.5f,local[1]/.5f,-si*local[0]/2+co*local[2]/1.5f});
                Check(Dot(expected,Unit({v.normal[0],v.normal[1],v.normal[2]}))>.9999f,"glTF baked inverse-transpose normal incorrect");
            }
        }
        std::cout<<key<<": "<<mesh.vertices.size()<<" vertices, normals valid\n";
    }
    std::cout<<"Lighting direction, sanitation, rotation, inverse transpose and OBJ/glTF import passed\n";
}
#if defined(ENABLE_DX12)
static void SavePpm(const char* path,const std::vector<std::uint8_t>& pixels) {
    std::ofstream f(path,std::ios::binary);f<<"P6\n320 240\n255\n";
    for(std::size_t i=0;i<pixels.size();i+=4)f.write(reinterpret_cast<const char*>(pixels.data()+i),3);
}
static void Gpu() {
    GlfwWindow window;Check(window.Create(320,240,"WhispEngine lighting regression"),"window");
    Dx12RenderAdapter adapter;Check(adapter.Initialize(&window),"DX12 initialize");
    ResourceManager resources(nullptr);
    auto sr=resources.Load<ShaderResource>("dx12/textured.hlsl");
    auto shader=adapter.CreateShaderProgram(sr->GetData());Check(shader.IsValid()&&adapter.SupportsInstancing(shader),"lit shader PSOs");
    TextureData texture;texture.width=1;texture.height=1;texture.pixels={128,192,64,255};
    auto tex=adapter.CreateTexture2D(texture);Check(tex.IsValid(),"texture");
    MeshData quad;quad.vertices.resize(4);quad.indices={0,1,2,0,2,3};
    const float positions[4][3]={{-.8f,.8f,.5f},{.8f,.8f,.5f},{.8f,-.8f,.5f},{-.8f,-.8f,.5f}};
    for(int i=0;i<4;++i){std::copy(positions[i],positions[i]+3,quad.vertices[i].position);quad.vertices[i].normal[2]=1;quad.vertices[i].uv[0]=float(i%2);}
    auto mesh=adapter.UploadMesh(quad);
    RenderInstanceData data;data.model=Model();data.mvp=Model();data.normal=BuildNormalMatrix(data.model);data.tint={.6f,.4f,.8f,1};
    LightingSettings settings;
    auto frame=[&](bool instanced,int count=1) {
        window.PollEvents();adapter.BeginFrame();adapter.Clear(.03f,.04f,.05f,1);
        adapter.SetLighting(BuildLightingConstants(settings));adapter.BindShader(shader);adapter.BindTexture(0,tex);
        if(instanced) {std::vector<RenderInstanceData> instances(count,data);Check(adapter.DrawMeshInstanced(mesh,instances),"instance draw");}
        else for(int i=0;i<count;++i){adapter.SetTestTransform(data.mvp.data());adapter.SetTestColor(data.tint[0],data.tint[1],data.tint[2],1);adapter.SetNormalMatrix(data.normal);adapter.DrawMesh(mesh);}
        Check(adapter.GetSubmissionStatistics().drawCalls==std::uint64_t(instanced?1:count),"batch draw count");
        adapter.EndFrame();auto pixels=adapter.ReadBackFramePixels();adapter.Present();return pixels;
    };
    auto expected=[&](const std::vector<std::uint8_t>& pixels,V factor) {
        const int at=(120*320+160)*4;const float base[3]={128*.6f,192*.4f,64*.8f};
        for(int i=0;i<3;++i)Check(std::abs(int(pixels[at+i])-std::clamp(int(std::lround(255*LinearToSrgb(SrgbToLinear(base[i]/(i==0?.6f:i==1?.4f:.8f)/255)*SrgbToLinear(i==0?.6f:i==1?.4f:.8f)*factor[i]))),0,255))<=2,"GPU expected Lambert/texture/tint color mismatch");
    };
    auto pair=[&](V factor) {auto ordinary=frame(false);Check(ordinary==frame(true),"ordinary/instanced pixels differ");expected(ordinary,factor);};
    settings.directional.color={1,1,1};settings.ambientColor={1,1,1};settings.ambientIntensity=0;
    settings.directional.intensity=.7f;
    for(V ray:{V{0,0,-4},V{0,0,1},V{1,0,0},V{1,0,-1}}) {
        settings.directional.direction=ray;auto u=Unit(ray);float d=std::max(-u[2],0.f)*.7f;pair({d,d,d});
    }
    settings.directional.intensity=0;pair({0,0,0});
    settings.ambientColor={.8f,.6f,1};settings.ambientIntensity=.4f;pair({.32f,.24f,.4f});
    settings.directional.enabled=false;settings.directional.intensity=3;pair({.32f,.24f,.4f});
    settings.enabled=false;pair({1,1,1});
    settings.enabled=true;settings.ambientColor={1,1,1};settings.ambientIntensity=.2f;settings.directional.enabled=true;settings.directional.intensity=.6f;settings.directional.direction={0,0,-1};
    // Keep screen geometry stable, change its independent world normal transform.
    for(auto rotation:{ecs::Vec3{0,0,0},ecs::Vec3{0,.8f,0},ecs::Vec3{.4f,.8f,.3f}}) {
        data.model=Model(rotation,{2,.5f,3});data.normal=BuildNormalMatrix(data.model);
        auto n=Unit(Normal(data.normal,{0,0,1}));float f=.2f+.6f*std::max(n[2],0.f);pair({f,f,f});
    }
    // Oblique normals expose the error made by using model instead of inverse transpose.
    adapter.DestroyMesh(mesh);for(auto& v:quad.vertices){v.normal[0]=1;v.normal[1]=1;v.normal[2]=1;}mesh=adapter.UploadMesh(quad);
    auto n=Unit(Normal(data.normal,{1,1,1}));float f=.2f+.6f*std::max(n[2],0.f);pair({f,f,f});
    data.normal={};pair({1,1,1}); // singular model
    adapter.DestroyMesh(mesh);for(auto& v:quad.vertices)std::fill(v.normal,v.normal+3,0.f);mesh=adapter.UploadMesh(quad);data.normal=BuildNormalMatrix(Model());pair({1,1,1});
    adapter.DestroyMesh(mesh);
    // Actual imported complex geometry, lit/unlit and ordinary/instance image checks.
    for(const char* key:{"models/african_head.obj","models/lighting_sphere.gltf"}) {
        auto r=resources.Load<MeshResource>(key);const auto& md=r->GetData().meshData;mesh=adapter.UploadMesh(md);
        V lo{1e30f,1e30f,1e30f},hi{-1e30f,-1e30f,-1e30f};
        data.model=Model({.1f,3.4f,0},{1,.7f,1.2f});data.normal=BuildNormalMatrix(data.model);
        for(const auto& v:md.vertices)for(int i=0;i<3;++i) {
            const float world=data.model[i]*v.position[0]+data.model[4+i]*v.position[1]+data.model[8+i]*v.position[2];
            lo[i]=std::min(lo[i],world);hi[i]=std::max(hi[i],world);
        }
        data.mvp=data.model;
        for(int i=0;i<3;++i) {
            const float scale=(i==2?.8f:1.5f)/(hi[i]-lo[i]);
            for(int col=0;col<4;++col)data.mvp[col*4+i]*=scale;
            data.mvp[12+i]+=-(hi[i]+lo[i])*.5f*scale+(i==2?.5f:0);
        }
        settings.ambientIntensity=.25f;settings.directional.intensity=.85f;settings.directional.direction={-.4f,-.6f,1};data.tint={1,1,1,1};
        const auto solidTexture=tex;
        auto modelTexture=resources.Load<TextureResource>("textures/african_head_diffuse.dds");
        tex=adapter.CreateTexture2D(modelTexture->GetData().textureData);Check(tex.IsValid(),"model texture");
        settings.enabled=false;auto unlit=frame(false);settings.enabled=true;auto lit=frame(false);Check(lit==frame(true),"complex model paths differ");Check(lit!=unlit,"complex model lighting has no effect");
        std::size_t varied=0;for(std::size_t i=4;i<lit.size();i+=4)if(std::memcmp(lit.data(),lit.data()+i,4)!=0)++varied;Check(varied>100,"complex model invisible");
        if(std::string(key).find("head")!=std::string::npos){SavePpm("lighting-head-lit.ppm",lit);SavePpm("lighting-head-unlit.ppm",unlit);}
        adapter.DestroyTexture(tex);tex=solidTexture;
        adapter.DestroyMesh(mesh);
    }
    auto cube=resources.Load<MeshResource>("models/validation_cube.obj");mesh=adapter.UploadMesh(cube->GetData().meshData);data.mvp=Model();data.mvp[0]=data.mvp[5]=data.mvp[10]=.5f;data.mvp[14]=.5f;
    Check(frame(false,501)==frame(true,501),"501 lit objects batch mismatch");
    // Explicit retained unlit shader, even while lighting is enabled.
    auto unlitResource=resources.Load<ShaderResource>("dx12/textured_unlit.hlsl");auto unlitShader=adapter.CreateShaderProgram(unlitResource->GetData());Check(unlitShader.IsValid(),"unlit PSO");
    auto litShader=shader;settings.enabled=false;auto baseline=frame(false);settings.enabled=true;shader=unlitShader;Check(frame(false)==baseline,"unlit shader fallback");shader=litShader;
    adapter.DestroyShader(unlitShader);adapter.DestroyShader(shader);adapter.DestroyMesh(mesh);adapter.DestroyTexture(tex);adapter.Shutdown();
    std::cout<<"DX12 expected colors, Lambert directions, ambient, rotation, nonuniform scale, missing normals, imported OBJ/glTF, unlit fallback and 501 -> 1 draws passed\n";
}
#endif
int main(int argc,char** argv) {try {
    MathAndImport();
#if defined(ENABLE_DX12)
    if(argc>1&&std::string(argv[1])=="--dx12")Gpu();
#endif
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
