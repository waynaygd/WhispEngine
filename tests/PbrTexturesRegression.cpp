#define NOMINMAX
#include "resources/TangentSpace.h"
#include "resources/ResourceManager.h"
#include "resources/loaders/MaterialLoader.h"
#include "resources/MaterialSerialization.h"
#include "ecs/systems/RenderSystem.h"
#include "ecs/PhysicsStressScene.h"
#include "scene/SceneSerializer.h"
#include "core/AssetPaths.h"
#include <iostream>
#include <fstream>
#include <numbers>
#include <limits>
#if defined(ENABLE_DX12)
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include "platform/GlfwWindow.h"
#include <GLFW/glfw3.h>
#endif
static void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
using V=std::array<double,3>;
static double Dot(V a,V b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static V Cross(V a,V b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
static V Unit(V a){double l=std::sqrt(Dot(a,a));if(!std::isfinite(l)||l<1e-20)return {};for(auto& x:a)x/=l;return a;}
static double Decode(double x){return x<=.04045?x/12.92:std::pow((x+.055)/1.055,2.4);}
static double Encode(double x){x=std::clamp(x,0.,1.);return x<=.0031308?x*12.92:1.055*std::pow(x,1/2.4)-.055;}
// Double oracle uses Smith lambda, independently of shader G1 algebra.
static V BRDF(V base,double m,double r,V n,V v,V l){
    double nl=std::max(0.,Dot(n,l)),nv=std::max(0.,Dot(n,v));if(nl<=0||nv<=0)return {};
    V h=Unit({v[0]+l[0],v[1]+l[1],v[2]+l[2]});if(Dot(h,h)<.5)return {};
    double nh=std::max(0.,Dot(n,h)),vh=std::max(0.,Dot(v,h)),a2=std::pow(std::max(.04,r),4);
    double d=a2/(std::numbers::pi*std::pow(1-nh*nh+a2*nh*nh,2));
    auto lambda=[&](double c){return .5*(std::sqrt(1+a2*(1/(c*c)-1))-1);};
    double g=1/((1+lambda(nl))*(1+lambda(nv)));V result{};
    for(int i=0;i<3;++i){double f0=.04*(1-m)+base[i]*m,f=f0+(1-f0)*std::pow(1-vh,5);result[i]=nl*((1-f)*(1-m)*base[i]/std::numbers::pi+d*g*f/(4*nl*nv));}return result;
}
static MeshData Quad(){MeshData mesh;mesh.vertices.resize(4);mesh.indices={0,1,2,0,2,3};
    float p[4][3]={{-.8f,.8f,.5f},{.8f,.8f,.5f},{.8f,-.8f,.5f},{-.8f,-.8f,.5f}};
    for(int i=0;i<4;++i){auto& v=mesh.vertices[i];std::copy_n(p[i],3,v.position);v.normal[2]=-1;v.uv[0]=i==1||i==2?1.f:0.f;v.uv[1]=i>=2?1.f:0.f;}return mesh;
}
struct Recorder:IRenderAdapter {
    RenderInstanceData pending;std::vector<RenderInstanceData> main,shadow;
    RenderSubmissionStatistics stats;std::array<RenderTextureHandle,4> bound{};
    bool fallback=false;std::uint64_t next=1;
    bool Initialize(IWindow*)override{return true;}void BeginFrame()override{main.clear();shadow.clear();stats={};}
    void Clear(float,float,float,float)override{}void EndFrame()override{}void Present()override{}void Shutdown()override{}
    void SetTestTransform(const float* p)override{std::copy_n(p,16,pending.mvp.begin());}
    void SetTestColor(float r,float g,float b,float a)override{pending.tint={r,g,b,a};}
    void SetModelMatrix(const std::array<float,16>& p)override{pending.model=p;}
    void SetNormalMatrix(const std::array<float,12>& p)override{pending.normal=p;}
    void SetBaseColor(const std::array<float,4>& p)override{pending.baseColor=p;}
    void SetSurfaceParameters(const std::array<float,4>& p)override{pending.surface=p;}
    void SetTextureParameters(const std::array<float,4>& p)override{pending.textureParameters=p;}
    void BindTexture(std::uint32_t slot,RenderTextureHandle h)override{bound.at(slot)=h;}
    RenderMeshHandle UploadMesh(const MeshData&)override{return {next++};}
    RenderTextureHandle CreateTexture2D(const TextureData&)override{return {next++};}
    RenderShaderHandle CreateShaderProgram(const ShaderResource&)override{return {next++};}
    bool SupportsInstancing(RenderShaderHandle)const override{return true;}
    void DrawMesh(RenderMeshHandle)override{main.push_back(pending);++stats.drawCalls;++stats.renderedInstances;}
    bool DrawMeshInstanced(RenderMeshHandle,std::span<const RenderInstanceData> p)override{if(fallback)return false;main.insert(main.end(),p.begin(),p.end());++stats.drawCalls;++stats.instancedDrawCalls;stats.renderedInstances+=p.size();return true;}
    bool BeginShadowPass(const ShadowFrame& f)override{return f.constants.parameters[0]>.5f;}
    void DrawShadowMesh(RenderMeshHandle,const RenderInstanceData& p)override{shadow.push_back(p);++stats.shadowDrawCalls;}
    bool DrawShadowMeshInstanced(RenderMeshHandle,std::span<const RenderInstanceData> p)override{if(fallback)return false;shadow.insert(shadow.end(),p.begin(),p.end());++stats.shadowDrawCalls;return true;}
    RenderSubmissionStatistics GetSubmissionStatistics()const override{return stats;}
    void DrawTestTriangle()override{}void DrawTestLine()override{}void DrawTestQuad()override{}void DrawTestCube()override{}
};
static void CpuScene(){
    using namespace ecs;ResourceManager resources(nullptr);Recorder recorder;RenderSystem render;World world;
    render.SetRenderAdapter(&recorder);render.SetResourceManager(&resources);ShadowSettings shadows;shadows.enabled=true;render.SetShadowSettings(&shadows);
    std::vector<Entity> entities;CreatePhysicsStressScene(world,entities,500);
    resources.Load<MeshResource>("models/validation_cube.obj");resources.Load<ShaderResource>("dx12/textured.hlsl");resources.Load<MaterialResource>("materials/stress.material.json");
    resources.Load<TextureResource>("textures/pbr_normal.png");resources.Load<TextureResource>("textures/pbr_orm.png");resources.Load<TextureResource>("defaults/texture");
    const auto assign=[&](){for(std::size_t i=0;i<entities.size();++i){auto& m=*world.GetComponent<MaterialComponent>(entities[i]);m.overrides.normalTexturePath="textures/pbr_normal.png";m.overrides.metallicRoughnessTexturePath=m.overrides.occlusionTexturePath="textures/pbr_orm.png";m.overrides.normalScale=float(i%3);m.overrides.occlusionStrength=float(i%5)*.25f;m.overrides.shadingModel=ShadingModel::PBR;}};assign();
    const auto frame=[&](int count=1){for(int i=0;i<count;++i){recorder.BeginFrame();render.Update(world,0);}};
    frame(15);Check(recorder.main.size()==501&&recorder.stats.drawCalls==1&&recorder.stats.shadowDrawCalls==1,"500 mapped objects must retain main/shadow batch");for(const auto& d:recorder.main)Check(d.textureParameters[2]==7,"maps not resolved through ECS");
    auto prepared=recorder.main;render.SetInstancingEnabled(false);frame();Check(recorder.stats.drawCalls==501,"ordinary 501 draws");for(std::size_t i=0;i<prepared.size();++i)Check(prepared[i].textureParameters==recorder.main[i].textureParameters&&prepared[i].normal==recorder.main[i].normal&&prepared[i].model==recorder.main[i].model,"ordinary/instance map payload");
    render.SetInstancingEnabled(true);auto& first=*world.GetComponent<MaterialComponent>(entities[1]);first.overrides.normalTexturePath="textures/pbr_orm.png";frame(5);Check(recorder.stats.drawCalls==2&&recorder.stats.shadowDrawCalls==1,"distinct normal bindings must split main batches");first.overrides.normalTexturePath="textures/pbr_normal.png";
    first.overrides.normalTexturePath="textures/pbr-regression-missing-normal.png";resources.Load<TextureResource>(*first.overrides.normalTexturePath);frame(5);Check(std::any_of(recorder.main.begin(),recorder.main.end(),[](const auto& d){return d.textureParameters[2]==6;}),"missing normal must preserve MR/AO independently");first.overrides.normalTexturePath="textures/pbr_normal.png";
    first.overrides.textureTexCoords=std::array<int,4>{0,1,0,0};frame();Check(std::any_of(recorder.main.begin(),recorder.main.end(),[](const auto& d){return d.textureParameters[2]==6;}),"unsupported normal UV set must disable only normal map");first.overrides.textureTexCoords.reset();
    RenderInterpolation interpolation;render.SetInterpolation(&interpolation);auto& physical=*world.GetComponent<TransformComponent>(entities[1]);interpolation.BeginTick(world);physical.position.x+=2;physical.rotation={.3f,.5f,.2f};physical.scale={-2,.5f,3};interpolation.EndTick(world);interpolation.SetFrame(world,true,.4f);std::array<float,16> expected;BuildRenderModelMatrix(expected.data(),interpolation.Resolve(entities[1],physical));auto before=physical.position;frame();Check(physical.position.x==before.x,"render mutated physics pose");Check(std::any_of(recorder.main.begin(),recorder.main.end(),[&](const auto& d){return d.model==expected&&d.normal==BuildNormalMatrix(expected)&&d.textureParameters[3]==-1;}),"interpolated mirrored mapped payload");
    first.overrides.castShadows=false;frame();Check(recorder.shadow.size()==500&&recorder.main.size()==501,"mapped cast flag");recorder.fallback=true;frame();Check(recorder.stats.drawCalls==501,"mapped shader/instance fallback");recorder.fallback=false;
    world.DestroyEntity(entities[1]);frame();Check(recorder.main.size()==500,"mapped delete entity");world.Clear();interpolation.Clear();frame();Check(recorder.main.empty()&&recorder.shadow.empty(),"mapped Clear");CreatePhysicsStressScene(world,entities,500);assign();frame(5);Check(recorder.stats.drawCalls==1,"mapped Restart batching");
    render.ReleaseGpuResources();render.SetRenderAdapter(nullptr);std::cout<<"CPU 500 mapped batching/slot splits/fallback/interpolation/negative scale/delete/Clear/Restart passed\n";
}
static void Cpu(){
    MaterialResource material;material.placeholder=false;material.normalTexturePath="textures/pbr_normal.png";material.metallicRoughnessTexturePath=material.occlusionTexturePath="textures/pbr_orm.png";material.normalScale=2;material.occlusionStrength=.3f;
    auto j=MaterialLoader::Serialize(material);Check(j["version"]==4,"v4 serializer");
    auto parsed=MaterialLoader::Parse("materials/test.json",j);Check(parsed.success&&parsed.data.normalScale==2&&parsed.data.occlusionTexturePath==material.occlusionTexturePath,"v4 roundtrip");
    for(int version:{1,2,3}){auto old=MaterialLoader::Parse("materials/test.json",{{"version",version}});Check(old.success&&old.data.normalTexturePath.empty()&&old.data.normalScale==1&&old.data.occlusionStrength==1&&old.data.shadingModel==ShadingModel::Lambert,"legacy defaults");}
    for(auto bad:{nlohmann::json{{"version",5}},nlohmann::json{{"normalScale",-1}},nlohmann::json{{"normalScale",17}},nlohmann::json{{"occlusionStrength",2}},nlohmann::json{{"normalTexturePath","../escape.png"}},nlohmann::json{{"normalTexturePath","C:/image.png"}},nlohmann::json{{"textureTexCoords",{0,0.5,0,0}}}})Check(!MaterialLoader::Parse("materials/a.json",bad).success,"invalid v4 accepted");
    auto invalid=j;invalid["normalScale"]=std::numeric_limits<float>::infinity();Check(!MaterialLoader::Parse("materials/a.json",invalid).success,"nonfinite factor accepted");
    const std::string diskKey="materials/pbr_textures_regression.temporary.json";std::string error;
    Check(MaterialLoader::Save(diskKey,material,&error),"atomic v4 Save");auto path=AssetPaths::ResolveAssetPath(diskKey);auto disk=MaterialLoader::Load(diskKey,path);Check(disk.success&&disk.data.normalScale==2,"v4 disk roundtrip");
    material.normalScale=std::numeric_limits<float>::quiet_NaN();Check(!MaterialLoader::Save(diskKey,material,&error),"invalid atomic Save accepted");Check(MaterialLoader::Load(diskKey,path).data.normalScale==2,"failed Save changed file");std::filesystem::remove(path);
    MaterialOverrides overrides;overrides.normalScale=0;overrides.occlusionStrength=0;overrides.normalTexturePath="";overrides.textureTexCoords=std::array<int,4>{0,1,0,0};
    bool escaped=false;try{MaterialOverridesFromJson({{"normalTexturePath","../escape.png"}});}catch(...){escaped=true;}Check(escaped,"unsafe texture override path accepted");
    auto roundtrip=MaterialOverridesFromJson(MaterialOverridesToJson(overrides));ApplyMaterialOverrides(material,roundtrip);Check(material.normalScale==0&&material.occlusionStrength==0&&material.normalTexturePath.empty()&&material.textureTexCoords[1]==1,"explicit zero/empty override");
    auto scene=AssetPaths::ResolveAssetOutputPath("scenes/pbr_textures_regression.temporary.json");EcsDemoEntityConfig config;config.materialOverrides=overrides;
    Check(SceneSerializer::SaveEntityConfigs(scene,{config},&error),"v4 scene Save");std::vector<EcsDemoEntityConfig> configs;Check(SceneSerializer::LoadEntityConfigs(scene,configs,&error)&&configs[0].materialOverrides.normalScale==0,"v4 scene roundtrip");std::filesystem::remove(scene);
    auto quad=Quad();TangentSpace::Generate(quad);for(auto& v:quad.vertices)Check(std::abs(v.tangent[0]-1)<1e-6&&std::abs(v.tangent[1])+std::abs(v.tangent[2])<1e-6&&v.tangent[3]==1,"analytic +X/-Y quad basis");
    auto mirrored=Quad();for(auto& v:mirrored.vertices)v.uv[0]=1-v.uv[0];TangentSpace::Generate(mirrored);for(auto& v:mirrored.vertices)Check(v.tangent[0]<-.999&&v.tangent[3]==-1,"mirrored UV sign");
    auto seam=Quad();seam.vertices[3].uv[0]=2;seam.vertices[3].uv[1]=0;TangentSpace::Generate(seam);Check(seam.vertices.size()==6&&seam.indices[0]!=seam.indices[3]&&seam.indices[2]!=seam.indices[4],"mirrored corners must split shared vertices");Check(seam.vertices[seam.indices[0]].tangent[3]==1&&seam.vertices[seam.indices[3]].tangent[3]==-1,"mirrored seam signs must not average");
    auto degenerate=Quad();for(auto& v:degenerate.vertices)v.uv[0]=v.uv[1]=0;TangentSpace::Generate(degenerate);for(auto& v:degenerate.vertices)Check(v.tangent[3]==0,"degenerate UV fallback");
    auto noNormal=Quad();for(auto& v:noNormal.vertices)std::fill_n(v.normal,3,0.f);TangentSpace::Generate(noNormal);for(auto& v:noNormal.vertices)Check(v.tangent[3]==0,"missing normal fallback");
    auto authored=Quad();for(auto& v:authored.vertices){v.tangent[0]=.6f;v.tangent[1]=.8f;v.tangent[3]=-1;}TangentSpace::Generate(authored);Check(authored.vertices[0].tangent[1]==.8f&&authored.vertices[0].tangent[3]==-1,"authored tangent overwritten");
    ResourceManager resources(nullptr);
    // Verify glTF defaults from a document which omits the factors, not from
    // the C++ material defaults (which intentionally remain legacy Lambert).
    const auto defaultsPath=AssetPaths::ResolveAssetOutputPath("models/pbr_defaults.temporary.gltf");
    Check(!std::filesystem::exists(defaultsPath),"temporary glTF already exists");
    struct Cleanup {std::filesystem::path p;~Cleanup(){std::error_code e;std::filesystem::remove(p,e);p+=".wmesh";std::filesystem::remove(p,e);}} cleanup{defaultsPath};
    nlohmann::json defaults;{std::ifstream input(AssetPaths::ResolveAssetPath("models/pbr_textures.gltf"));input>>defaults;}
    defaults["materials"][0]["pbrMetallicRoughness"].erase("metallicFactor");defaults["materials"][0]["pbrMetallicRoughness"].erase("roughnessFactor");defaults["materials"][0]["normalTexture"].erase("scale");defaults["materials"][0]["occlusionTexture"].erase("strength");
    {std::ofstream output(defaultsPath);output<<defaults.dump();}
    auto defaultMesh=resources.Load<MeshResource>("models/pbr_defaults.temporary.gltf");Check(defaultMesh->IsLoaded(),"glTF omitted-factor import");const auto& defaultData=defaultMesh->GetData().meshData;const auto& defaultMaterial=defaultData.materials[defaultData.submeshes[0].materialSlot];Check(defaultMaterial.shadingModel==ShadingModel::PBR&&defaultMaterial.metallic==1&&defaultMaterial.roughness==1&&defaultMaterial.normalScale==1&&defaultMaterial.occlusionStrength==1,"glTF factor defaults must be one");
    for(const char* key:{"models/pbr_textures.gltf","models/pbr_textures_embedded.gltf","models/pbr_textures_uv1.gltf"}) {
        auto mesh=resources.Load<MeshResource>(key);Check(mesh->IsLoaded(),"glTF PBR mesh load");const auto& data=mesh->GetData().meshData;Check(data.materials.size()&&data.submeshes.size(),"glTF slots");
        const auto& m=data.materials[data.submeshes[0].materialSlot];Check(m.shadingModel==ShadingModel::PBR&&m.metallic==1&&m.roughness==1&&m.occlusionStrength==.8f&&!m.normalTexturePath.empty()&&!m.metallicRoughnessTexturePath.empty(),"glTF MR factors/maps/scale/AO");
        for(const auto* image:{&m.texturePath,&m.normalTexturePath,&m.metallicRoughnessTexturePath,&m.occlusionTexturePath})Check(resources.Load<TextureResource>(*image)->IsLoaded(),"external/embedded image load");
        if(std::string(key).find("uv1")!=std::string::npos)Check(m.textureTexCoords[1]==1&&m.diagnostics.find("Unsupported UV")!=std::string::npos,"UV1 must be diagnosed, never alias UV0");
        int good=0;for(const auto& v:data.vertices)if(v.tangent[3]!=0){V n=Unit({v.normal[0],v.normal[1],v.normal[2]}),t=Unit({v.tangent[0],v.tangent[1],v.tangent[2]});Check(std::abs(Dot(n,t))<1e-5,"import T perpendicular to N");++good;}Check(good>1000,"generated/authored import basis unavailable");
        const bool bakedMirror=std::string(key).find("embedded")!=std::string::npos;
        auto found=std::find_if(data.vertices.begin(),data.vertices.end(),[&](const auto& v){return std::abs(v.position[0]-(bakedMirror?-1.5:1)*std::sqrt(.5))<1e-4&&std::abs(v.position[1]-(bakedMirror?.7:1)*std::sqrt(.5))<1e-4&&std::abs(v.position[2])<1e-4;});Check(found!=data.vertices.end(),"analytic glTF reference vertex");
        const auto& v=*found;V n=Unit({v.normal[0],v.normal[1],v.normal[2]}),t=Unit({v.tangent[0],v.tangent[1],v.tangent[2]});
        // phi=0 analytic tangent +Z; mirrored X node changes determinant only.
        if(std::string(key).find("uv1")==std::string::npos)Check(t[2]>.999&&std::abs(t[0])<1e-4,"node linear authored tangent transform");
        else Check(t[2]>.99&&std::abs(Dot(n,t))<1e-5,"generated curved tangent direction");
        if(std::string(key).find("embedded")!=std::string::npos){Check(v.tangent[3]==-1,"mirrored node authored w");
            auto diagonal=std::find_if(data.vertices.begin(),data.vertices.end(),[](const auto& v){return std::abs(v.position[0]+.75)<1e-4&&std::abs(v.position[1]-.7*std::sqrt(.5))<1e-4&&std::abs(v.position[2]-.6)<1e-4;});Check(diagonal!=data.vertices.end(),"diagonal baked reference vertex");
            V expected=Unit({1.5/std::sqrt(2.),0,1.2/std::sqrt(2.)});for(int i=0;i<3;++i)Check(std::abs(diagonal->tangent[i]-expected[i])<1e-5,"authored tangent must use node linear, not inverse transpose");
        }else Check(v.tangent[3]==1,"authored glTF w");
    }
    auto shared=resources.Load<MaterialResource>("materials/pbr_textures.material.json");auto same=resources.Load<MaterialResource>(shared->GetKey());Check(shared==same,"material cache identity");auto changed=shared->GetData();changed.normalScale=0;shared->ReplaceData(changed,ResourceLoadState::Loaded);Check(same->GetData().normalScale==0,"shared live map edit");resources.Reload<MaterialResource>(shared->GetKey());Check(same->GetData().normalScale==1,"map Revert/reload");
    std::cout<<"CPU v4 legacy/atomic/scene/overrides/cache, external/embedded glTF, UV0-only, authored/generated/mirrored/degenerate tangent checks passed\n";
}
#if defined(ENABLE_DX12)
static void Save(const std::string& name,const std::vector<std::uint8_t>& pixels){std::filesystem::create_directories("pbr-textures-readback");std::ofstream f("pbr-textures-readback/"+name+".ppm",std::ios::binary);f<<"P6\n320 240\n255\n";for(std::size_t i=0;i<pixels.size();i+=4)f.write(reinterpret_cast<const char*>(pixels.data()+i),3);}
static void Gpu(){
    GlfwWindow window;Check(window.Create(320,240,"PBR textures regression"),"window");glfwHideWindow(window.GetGlfwHandle());Dx12RenderAdapter adapter;Check(adapter.Initialize(&window),"DX12");ResourceManager resources(nullptr);
    auto shader=adapter.CreateShaderProgram(resources.Load<ShaderResource>("dx12/textured.hlsl")->GetData());Check(shader.IsValid()&&adapter.SupportsInstancing(shader),"textured ordinary/instance PSOs");
    auto quad=Quad();auto mesh=adapter.UploadMesh(quad);TextureData base,normal,mr,ao;base.pixels={128,192,64,255};normal.pixels={204,153,229,255};mr.pixels={51,102,204,255};ao.pixels={64,250,255,255};
    auto baseHandle=adapter.CreateTexture2D(base),normalHandle=adapter.CreateTexture2D(normal),mrHandle=adapter.CreateTexture2D(mr),aoHandle=adapter.CreateTexture2D(ao);
    RenderInstanceData data;data.model=data.mvp={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};data.normal=BuildNormalMatrix(data.model);data.tint={.7f,.8f,.6f,1};data.baseColor={.8f,.6f,.7f,1};data.surface={.7f,.8f,1,0};data.textureParameters={1,.7f,7,1};
    LightingSettings light;light.directional.direction={-.4f,.2f,1};light.directional.color={1,1,1};light.directional.intensity=.7f;light.ambientIntensity=.2f;V camera{0,0,-2};
    auto frame=[&](bool instanced){window.PollEvents();adapter.BeginFrame();adapter.Clear(.03f,.04f,.05f,1);adapter.BindShader(shader);adapter.BindTexture(0,baseHandle);adapter.BindTexture(1,normalHandle);adapter.BindTexture(2,mrHandle);adapter.BindTexture(3,aoHandle);adapter.SetLighting(BuildLightingConstants(light));adapter.SetCameraPosition({float(camera[0]),float(camera[1]),float(camera[2]),0});
        adapter.SetMaterialState({true,false});if(instanced)Check(adapter.DrawMeshInstanced(mesh,std::span(&data,1)),"instanced mapped draw");else{adapter.SetModelMatrix(data.model);adapter.SetNormalMatrix(data.normal);adapter.SetBaseColor(data.baseColor);adapter.SetSurfaceParameters(data.surface);adapter.SetTextureParameters(data.textureParameters);adapter.SetTestTransform(data.mvp.data());adapter.SetTestColor(data.tint[0],data.tint[1],data.tint[2],1);adapter.DrawMesh(mesh);}adapter.EndFrame();auto pixels=adapter.ReadBackFramePixels();adapter.Present();return pixels;};
    auto pair=[&](const char* name){auto pixels=frame(false);Check(pixels==frame(true),"mapped ordinary/instance pixels differ");unsigned mask=unsigned(data.textureParameters[2]);V n{},t{data.model[0],data.model[1],data.model[2]};for(int i=0;i<3;++i)n[i]=-data.normal[i*4+2];n=Unit(n);t=Unit(t);double projection=Dot(n,t);for(int i=0;i<3;++i)t[i]-=projection*n[i];t=Unit(t);
        V columns[3];for(int j=0;j<3;++j)columns[j]=Unit({data.model[j*4],data.model[j*4+1],data.model[j*4+2]});double sign=Dot(Cross(columns[0],columns[1]),columns[2])<0?-1:1;
        if(mask&1){auto b=Cross(n,t);for(auto& x:b)x*=sign;V map{(normal.pixels[0]/255.*2-1)*data.textureParameters[0],(normal.pixels[1]/255.*2-1)*data.textureParameters[0],normal.pixels[2]/255.*2-1};map=Unit(map);V mapped{};for(int i=0;i<3;++i)mapped[i]=t[i]*map[0]+b[i]*map[1]+n[i]*map[2];n=Unit(mapped);}
        V albedo{};for(int i=0;i<3;++i)albedo[i]=Decode(base.pixels[i]/255.)*Decode(data.tint[i])*data.baseColor[i];double metal=data.surface[0]*(mask&2?mr.pixels[2]/255.:1),rough=data.surface[1]*(mask&2?mr.pixels[1]/255.:1),occlusion=mask&4?1-data.textureParameters[1]+data.textureParameters[1]*ao.pixels[0]/255.:1;
        V L=Unit({-light.directional.direction[0],-light.directional.direction[1],-light.directional.direction[2]});
        for(int y:{60,120,180})for(int x:{80,120,160,200,240}){V p{(2*x+1)/320.-1,1-(2*y+1)/240.,.5},world{};for(int i=0;i<3;++i)world[i]=data.model[i]*p[0]+data.model[4+i]*p[1]+data.model[8+i]*p[2]+data.model[12+i];auto Vdir=Unit({camera[0]-world[0],camera[1]-world[1],camera[2]-world[2]});auto direct=BRDF(albedo,metal,rough,n,Vdir,L);
            for(int i=0;i<3;++i){double value=albedo[i];if(light.enabled){double intensity=light.directional.enabled?light.directional.intensity:0;value=data.surface[2]>.5?albedo[i]*(1-metal)*light.ambientIntensity*occlusion+direct[i]*intensity:albedo[i]*(light.ambientIntensity*occlusion+intensity*std::max(0.,Dot(n,L)));}int expected=int(std::lround(255*Encode(value))),actual=pixels[(y*320+x)*4+i];if(std::abs(expected-actual)>2){std::cerr<<name<<" x="<<x<<" y="<<y<<" channel="<<i<<" expected="<<expected<<" actual="<<actual<<'\n';throw std::runtime_error("mapped BRDF/channel/TBN oracle");}}
        }Save(name,pixels);return pixels;};
    for(unsigned mask:{0u,1u,2u,4u,7u}){data.textureParameters[2]=float(mask);pair(("maps-"+std::to_string(mask)).c_str());}
    data.textureParameters[2]=7;for(float scale:{0.f,.5f,2.f}){data.textureParameters[0]=scale;pair(("normal-scale-"+std::to_string(scale)).c_str());}data.textureParameters[0]=1;
    light.ambientIntensity=0;auto directAO=pair("ao-direct");data.textureParameters[2]=3;Check(pair("ao-direct-off")==directAO,"AO attenuated direct light/specular");data.textureParameters[2]=7;light.directional.intensity=0;light.ambientIntensity=.3f;pair("ambient-only");
    light.directional.intensity=.7f;light.ambientIntensity=.2f;data.surface[2]=0;pair("lambert-mapped");data.surface[2]=1;
    for(float sign:{1.f,-1.f}){float c=std::cos(.4f),s=std::sin(.4f);data.model={sign*2*c,0,-sign*2*s,0,0,.5f,0,0,3*s,0,3*c,0,0,0,0,1};data.normal=BuildNormalMatrix(data.model);pair(sign>0?"rotated-nonuniform":"rotated-mirrored");}
    light.enabled=false;pair("unlit-mapped");light.enabled=true;
    // The normal map must move a real specular lobe, not just change an average.
    data.model={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};data.normal=BuildNormalMatrix(data.model);data.surface={1,.2f,1,0};data.textureParameters={1,1,0,1};light.directional.direction={0,0,1};light.directional.intensity=.05f;light.ambientIntensity=0;
    auto off=pair("highlight-normal-off");auto oldNormalHandle=normalHandle;
    normal.pixels={140,128,255,255};normalHandle=adapter.CreateTexture2D(normal);data.textureParameters[2]=1;auto on=pair("highlight-normal-on");
    auto peakX=[](const auto& pixels){int maximum=0;for(int y=40;y<200;++y)for(int x=45;x<275;++x)maximum=std::max(maximum,int(pixels[(y*320+x)*4]));Check(maximum>30,"missing specular highlight");double sum=0,count=0;for(int y=40;y<200;++y)for(int x=45;x<275;++x)if(pixels[(y*320+x)*4]>=maximum-2){sum+=x;count++;}return sum/count;};
    double flatPeak=peakX(off),mappedPeak=peakX(on);Check(flatPeak-mappedPeak>25,"normal map failed to move highlight in +T direction");
    adapter.DestroyTexture(normalHandle);normalHandle=oldNormalHandle;normal.pixels={204,153,229,255};
    // G-only changes control lobe width. B=255 holds metallic=1 and R is ignored.
    auto oldMRHandle=mrHandle;data.surface={1,1,1,0};data.textureParameters[2]=2;light.directional.intensity=.5f;
    mr.pixels={47,51,255,255};mrHandle=adapter.CreateTexture2D(mr);auto sharp=pair("mr-sharp");adapter.DestroyTexture(mrHandle);
    mr.pixels={47,204,255,255};mrHandle=adapter.CreateTexture2D(mr);auto broad=pair("mr-broad");adapter.DestroyTexture(mrHandle);mrHandle=oldMRHandle;mr.pixels={51,102,204,255};
    auto area=[](const auto& pixels){int peak=0,count=0;for(int y=40;y<200;++y)for(int x=45;x<275;++x)peak=std::max(peak,int(pixels[(y*320+x)*4]));for(int y=40;y<200;++y)for(int x=45;x<275;++x)if(pixels[(y*320+x)*4]>=peak*.5)++count;return std::pair(peak,count);};
    auto a=area(sharp),b=area(broad);Check(a.first>b.first&&b.second>2*a.second,"MR G must lower and broaden specular lobe");std::cout<<"GPU normal highlight X "<<flatPeak<<" -> "<<mappedPeak<<"; MR peak/half-height area "<<a.first<<'/'<<a.second<<" -> "<<b.first<<'/'<<b.second<<'\n';
    light.directional.direction={-.4f,.2f,1};light.directional.intensity=.7f;light.ambientIntensity=.2f;data.surface={.7f,.8f,1,0};data.textureParameters={1,.7f,7,1};
    // One image bound simultaneously as sRGB Base Color and linear MR/AO data.
    auto originalMR=mrHandle,originalAO=aoHandle;mrHandle=aoHandle=baseHandle;mr=ao=base;pair("shared-image-semantic-views");mrHandle=originalMR;aoHandle=originalAO;
    // Existing loaders must reach real GPU sampling for colour AND data uses.
    const auto originals=std::array{baseHandle,normalHandle,mrHandle,aoHandle};
    for(const char* key:{"textures/pbr_control.jpg","textures/pbr_control.dds","textures/pbr_control_bc3.dds"}) {
        auto loaded=resources.Load<TextureResource>(key);Check(loaded->IsLoaded(),"JPEG/DDS full loader");
        base=normal=mr=ao=loaded->GetData().textureData;
        auto image=adapter.CreateTexture2D(base);baseHandle=normalHandle=mrHandle=aoHandle=image;
        data.model={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};data.normal=BuildNormalMatrix(data.model);
        pair(std::filesystem::path(key).stem().string().c_str());adapter.DestroyTexture(image);
    }
    baseHandle=originals[0];normalHandle=originals[1];mrHandle=originals[2];aoHandle=originals[3];
    auto originalMesh=mesh;auto degenerateUV=Quad();for(auto& v:degenerateUV.vertices)v.uv[0]=v.uv[1]=0;mesh=adapter.UploadMesh(degenerateUV);data.textureParameters[2]=0;auto geometric=frame(false);data.textureParameters[2]=1;Check(frame(false)==geometric&&frame(true)==geometric,"unavailable tangent basis must use geometric normal");adapter.DestroyMesh(mesh);
    auto missingN=Quad();for(auto& v:missingN.vertices)std::fill_n(v.normal,3,0.f);mesh=adapter.UploadMesh(missingN);data.textureParameters[2]=0;auto unlit=frame(false);data.textureParameters[2]=7;Check(frame(false)==unlit&&frame(true)==unlit,"missing normals must preserve unlit fallback with maps");adapter.DestroyMesh(mesh);mesh=originalMesh;
    // 180 distinct frame snapshots cross multiple 64KB CB pages. Two tables
    // are recorded repeatedly; deleting resources before submit and allocating
    // a replacement must not overwrite either CB memory or descriptor tables.
    adapter.BeginFrame();adapter.Clear(0,0,0,1);adapter.BindShader(shader);adapter.SetMaterialState({true,false});adapter.SetTestColor(1,1,1,1);adapter.SetBaseColor({1,1,1,1});adapter.SetSurfaceParameters({0,.5f,0,0});adapter.SetTextureParameters({1,1,0,1});adapter.SetModelMatrix(data.model);adapter.SetNormalMatrix(data.normal);
    light.directional.intensity=0;
    for(int i=0;i<180;++i){bool right=i%2;light.ambientIntensity=right?.8f:.1f;adapter.SetLighting(BuildLightingConstants(light));auto mvp=data.mvp;mvp[0]=.5f;mvp[12]=right?.45f:-.45f;adapter.SetTestTransform(mvp.data());adapter.BindTexture(0,right?mrHandle:baseHandle);adapter.DrawMesh(mesh);}
    Check(adapter.GetSubmissionStatistics().frameConstantUploadBytes==180*768,"frame CB snapshots were overwritten/reused");
    for(auto h:originals)adapter.DestroyTexture(h);adapter.DestroyShader(shader);adapter.DestroyMesh(mesh);
    TextureData replacement;replacement.pixels={255,0,255,255};auto replaced=adapter.CreateTexture2D(replacement);Check(replaced.IsValid(),"replacement allocation before submit");adapter.EndFrame();auto immutable=adapter.ReadBackFramePixels();adapter.Present();
    Check(std::abs(int(immutable[(120*320+80)*4])-int(std::lround(255*Encode(Decode(128/255.)*.1))))<=2,"recorded Lighting CBV changed before submit");
    Check(std::abs(int(immutable[(120*320+240)*4])-int(std::lround(255*Encode(Decode(51/255.)*.8))))<=2,"recorded descriptor table changed before submit");Save("immutable-bindings",immutable);adapter.DestroyTexture(replaced);
    light.directional.intensity=.7f;light.ambientIntensity=.2f;
    // Exercise the complete mesh/material/resource path, including embedded
    // images, independently loaded slots, map fallback and import tangents.
    ecs::RenderSystem render;ecs::World world;render.SetRenderAdapter(&adapter);render.SetResourceManager(&resources);render.SetLightingSettings(&light);render.SetCameraProjection(.8f,320.f/240,.01f,100);render.SetCameraTransform({0,0,-4},0,0);
    auto sceneFrame=[&](bool instanced,int count=1){render.SetInstancingEnabled(instanced);std::vector<std::uint8_t> pixels;for(int i=0;i<count;++i){adapter.BeginFrame();adapter.Clear(.03f,.04f,.05f,1);render.Update(world,0);adapter.EndFrame();pixels=adapter.ReadBackFramePixels();adapter.Present();}return pixels;};
    for(const char* key:{"models/pbr_textures.gltf","models/pbr_textures_embedded.gltf","models/pbr_textures_uv1.gltf"}) {
        world.Clear();auto loaded=resources.Load<MeshResource>(key);for(const auto& m:loaded->GetData().meshData.materials)for(const auto* path:{&m.texturePath,&m.normalTexturePath,&m.metallicRoughnessTexturePath,&m.occlusionTexturePath})if(!path->empty())resources.Load<TextureResource>(*path);
        for(int i=0;i<2;++i){auto e=world.CreateEntity();world.AddComponent<ecs::TransformComponent>(e);world.AddComponent<ecs::MeshRendererComponent>(e).meshPath=key;auto& m=world.AddComponent<ecs::MaterialComponent>(e);m.overrides.metallic=.4f;}
        auto ordinary=sceneFrame(false,12),instanced=sceneFrame(true);Check(ordinary==instanced,"imported PBR ordinary/instance");Check(adapter.GetSubmissionStatistics().drawCalls==1&&adapter.GetSubmissionStatistics().instancedDrawCalls==1,"imported maps batching");
        std::size_t visible=0;for(std::size_t i=0;i<ordinary.size();i+=4)if(ordinary[i]>30)++visible;Check(visible>1000,"imported geometry missing from GPU readback");Save(std::filesystem::path(key).stem().string(),ordinary);
        render.SetTextureMapMask(6);auto flat=sceneFrame(true);if(std::string(key).find("uv1")==std::string::npos)Check(flat!=ordinary,"imported normal map ignored");else Check(flat==ordinary,"unsupported UV1 sampled as UV0");render.SetTextureMapMask(7);
    }
    // Production ECS stress scene: mapped instances share the same upload in
    // main/shadow passes. AO remains in ambient while hard visibility scales
    // the complete direct BRDF exactly once.
    world.Clear();std::vector<ecs::Entity> entities;ecs::CreatePhysicsStressScene(world,entities,500);
    resources.Load<MeshResource>("models/validation_cube.obj");resources.Load<MaterialResource>("materials/stress.material.json");resources.Load<TextureResource>("defaults/texture");
    for(auto e:entities){auto& m=*world.GetComponent<ecs::MaterialComponent>(e);m.overrides.shadingModel=ShadingModel::PBR;m.overrides.metallic=.4f;m.overrides.roughness=.6f;m.overrides.normalTexturePath="textures/pbr_normal.png";m.overrides.metallicRoughnessTexturePath=m.overrides.occlusionTexturePath="textures/pbr_orm.png";}
    ShadowSettings shadows;shadows.enabled=true;shadows.pcf=false;shadows.edgeFade=0;render.SetShadowSettings(&shadows);render.SetCameraTransform({22,20,-12},std::atan2(-22.f,42.f),-.35f);render.SetCameraProjection(1.04719755f,320.f/240,.01f,150);
    light.directional.direction={-.4f,-1,.3f};light.directional.intensity=.85f;light.ambientIntensity=.25f;
    auto lit=sceneFrame(true,16);Check(adapter.GetSubmissionStatistics().drawCalls==1&&adapter.GetSubmissionStatistics().shadowDrawCalls==1,"500 mapped DX12 main/shadow batching");Check(adapter.GetSubmissionStatistics().uploadBytes==501*sizeof(RenderInstanceData),"shadow/main instance upload reused");
    Check(lit==sceneFrame(false),"500 mapped stress ordinary/instance equality");Save("stress-500-mapped",lit);
    shadows.enabled=false;auto noShadow=sceneFrame(true);light.directional.intensity=0;auto ambient=sceneFrame(true);light.directional.intensity=.85f;shadows.enabled=true;shadows.debugMode=1;auto mask=sceneFrame(true);shadows.debugMode=0;
    int dark=0,valid=0;for(std::size_t i=0;i<lit.size();i+=4){if(lit[i]==noShadow[i]&&lit[i+1]==noShadow[i+1]&&lit[i+2]==noShadow[i+2])continue;double visibility=mask[i]/255.;if(visibility<.01)++dark;for(int c=0;c<3;++c){double expected=Decode(ambient[i+c]/255.)+(Decode(noShadow[i+c]/255.)-Decode(ambient[i+c]/255.))*visibility;Check(std::abs(int(lit[i+c])-int(std::lround(255*Encode(expected))))<=3,"mapped shadow must affect direct once, preserve ambient AO");}++valid;}
    Check(dark>20&&valid>20,"mapped shadow readback lacks receivers");
    Save("stress-shadow-mask",mask);Save("stress-ambient-ao",ambient);
    auto map=resources.Reload<TextureResource>("textures/pbr_normal.png");Check(map->IsLoaded(),"normal texture reload");Check(sceneFrame(true,8)==lit,"normal map reload changed valid content");
    for(auto e:entities)world.GetComponent<ecs::MaterialComponent>(e)->overrides.normalTexturePath="textures/nonexistent-normal.png";
    resources.Load<TextureResource>("textures/nonexistent-normal.png");auto missing=sceneFrame(true,6);render.SetTextureMapMask(6);Check(sceneFrame(true)==missing,"missing normal should use geometric N only");render.SetTextureMapMask(7);
    world.DestroyEntity(entities[1]);sceneFrame(true);Check(adapter.GetSubmissionStatistics().renderedInstances==500,"mapped GPU delete entity");world.Clear();sceneFrame(true);Check(adapter.GetSubmissionStatistics().drawCalls==0&&adapter.GetSubmissionStatistics().shadowDrawCalls==0,"mapped GPU Clear stale draw");
    render.ReleaseGpuResources();world.Clear();Check(adapter.GetValidationErrorCount()==0,"D3D12 ERROR/CORRUPTION");adapter.Shutdown();
    std::cout<<"DX12 independent mapped BRDF/TBN/MR/AO readback, ambient-only/direct-only, shared image semantic views, external/embedded glTF and ordinary/instance equality passed\n";
}
#endif
int main(int argc,char** argv){try{Cpu();CpuScene();
#if defined(ENABLE_DX12)
    if(argc>1&&std::string(argv[1])=="--dx12")Gpu();
#endif
    return 0;}catch(const std::exception& e){std::cerr<<"PBR textures FAILED: "<<e.what()<<'\n';return 1;}}
