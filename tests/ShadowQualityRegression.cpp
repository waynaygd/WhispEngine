#include "render/ColorPipeline.h"
#define NOMINMAX
#include "render/Shadows.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include "platform/GlfwWindow.h"
#include "resources/ResourceManager.h"
#include "ecs/RenderInterpolation.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace ecs;
static void Check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
static ShadowMatrix Model(Vec3 p={},Vec3 s={1,1,1},Vec3 r={}) {
    TransformComponent t;t.position=p;t.scale=s;t.rotation=r;
    ShadowMatrix m;BuildRenderModelMatrix(m.data(),RenderPose::From(t));return m;
}
static constexpr int W=320,H=240;
static void Save(const std::filesystem::path& path,const std::vector<std::uint8_t>& pixels) {
    std::ofstream f(path,std::ios::binary);f<<"P6\n"<<W<<' '<<H<<"\n255\n";
    for(std::size_t i=0;i<pixels.size();i+=4)f.write(reinterpret_cast<const char*>(pixels.data()+i),3);
}
int main(int argc,char** argv) {try {
    const bool baseline=argc>1&&std::string(argv[1])=="--baseline";
    const std::filesystem::path output=argc>2?argv[2]:"shadow-quality-readback";
    std::filesystem::create_directories(output);
    GlfwWindow window;Check(window.Create(W,H,"Shadow quality readback"),"window");
    Dx12RenderAdapter gpu;Check(gpu.Initialize(&window),"device");ResourceManager resources(nullptr);
    auto shader=gpu.CreateShaderProgram(resources.Load<ShaderResource>("dx12/textured.hlsl")->GetData());
    auto cube=gpu.UploadMesh(resources.Load<MeshResource>("models/validation_cube.obj")->GetData().meshData);
    MeshData plane;
    // Blue inclined plane y=.3*x+.2*z. Analytic normal; two triangles only.
    const float n=std::sqrt(1.f+.09f+.04f);
    for(auto p:{Vec3{-6,-3,-6},Vec3{-6,-.6f,6},Vec3{6,3,6},Vec3{6,.6f,-6}}) {
        MeshVertex v;v.position[0]=p.x;v.position[1]=p.y;v.position[2]=p.z;
        v.normal[0]=-.3f/n;v.normal[1]=1/n;v.normal[2]=-.2f/n;plane.vertices.push_back(v);
    }
    plane.indices={0,1,2,0,2,3};auto panel=gpu.UploadMesh(plane);
    TextureData white;white.width=white.height=1;white.pixels={255,255,255,255};auto texture=gpu.CreateTexture2D(white);
    LightingSettings light;light.directional.direction={.8f,-1,.4f};light.directional.color={1,1,1};light.directional.intensity=.75f;light.ambientIntensity=.2f;
    ShadowSettings shadows;shadows.coverage=48;shadows.depthRange=100;shadows.resolution=512;shadows.pcf=false;
    // The SAME original parameters are explicitly set for both baseline and new shader.
    shadows.depthBias=100;shadows.slopeBias=1;shadows.receiverBias=.0003f;shadows.normalBias=.0002f;
    ShadowMatrix view{1,0,0,0,0,0,-1,0,0,1,0,0,0,0,12,1};
    ShadowMatrix projection{1.f/8,0,0,0,0,1.f/6,0,0,0,0,1.f/25,0,0,0,0,1};
    ShadowMatrix vp=ShadowMultiply(projection,view);
    bool caster=false,fitted=false,doubleSided=true;ShadowVector focus{},cameraPosition{0,12,0};float cameraYaw=0,cameraPitch=-1.3f;
    RenderInstanceData receiver,box;
    auto pose=[&](RenderInstanceData& d,ShadowMatrix model,std::array<float,4> tint) {
        d.model=model;d.mvp=ShadowMultiply(vp,model);d.normal=BuildNormalMatrix(model);d.tint=tint;
    };
    auto reset=[&]{pose(receiver,Model(),{.15f,.4f,1,1});pose(box,Model({0,2,0}),{1,.5f,.2f,1});};reset();
    auto frame=[&](bool instanced) {
        window.PollEvents();gpu.BeginFrame();gpu.Clear(.03f,.04f,.05f,1);gpu.SetLighting(BuildLightingConstants(light));
        auto shadow=BuildDirectionalShadowFrame(BuildLightingConstants(light),shadows,focus);
        if(fitted)shadow=BuildCameraDirectionalShadowFrame(BuildLightingConstants(light),shadows,cameraPosition,cameraYaw,cameraPitch,1.04719755f,16.f/9,.01f,150);
        auto state=[&](const auto& d){gpu.SetMaterialState({doubleSided,true,!doubleSided&&d.model[0]<0});};
        if(gpu.BeginShadowPass(shadow)) {
            state(receiver);
            if(instanced)Check(gpu.DrawShadowMeshInstanced(panel,{&receiver,1}),"panel instancing");else gpu.DrawShadowMesh(panel,receiver);
            if(caster){state(box);if(instanced)Check(gpu.DrawShadowMeshInstanced(cube,{&box,1}),"box instancing");else gpu.DrawShadowMesh(cube,box);}
            gpu.EndShadowPass();
        }
        gpu.BeginMainPass();gpu.BindShader(shader);gpu.BindTexture(0,texture);gpu.SetMaterialState({true,true});
        auto draw=[&](RenderMeshHandle mesh,const RenderInstanceData& d) {
            state(d);
            if(instanced)Check(gpu.DrawMeshInstanced(mesh,{&d,1}),"main instancing");
            else {gpu.SetModelMatrix(d.model);gpu.SetNormalMatrix(d.normal);gpu.SetTestTransform(d.mvp.data());gpu.SetTestColor(d.tint[0],d.tint[1],d.tint[2],1);gpu.DrawMesh(mesh);}
        };draw(panel,receiver);if(caster)draw(cube,box);
        gpu.EndMainPass();gpu.EndFrame();auto image=gpu.ReadBackFramePixels();gpu.Present();return image;
    };
    auto pair=[&]{auto a=frame(false),b=frame(true);Check(a==b,"ordinary/instanced quality mismatch");return b;};
    auto acne=[&](const std::vector<std::uint8_t>& off,const std::vector<std::uint8_t>& on) {
        int affected=0,total=0;
        for(int y=35;y<H-35;++y)for(int x=60;x<W-60;++x) {
            auto p=(y*W+x)*4;if(off[p+2]>60){++total;if(on[p+2]+3<off[p+2])++affected;}
        }
        Check(total>10000,"inclined plane control ROI missing");return affected;
    };
    for(auto resolution:{512u,1024u,2048u,4096u})for(int filter:{0,3,5}) {
        shadows.resolution=resolution;shadows.pcf=filter!=0;shadows.filterTaps=filter==3?3:5;shadows.enabled=false;auto off=pair();shadows.enabled=true;auto on=pair();
        const auto name=std::to_string(resolution)+(filter==0?"-hard":filter==3?"-soft3":"-soft");
        Save(output/(name+"-off.ppm"),off);Save(output/(name+".ppm"),on);
        int stripes=acne(off,on);std::cout<<name<<" plane affected pixels="<<stripes<<'\n';
        if(!baseline)Check(stripes<10,"inclined control plane has self-shadow stripes");
    }
    shadows.resolution=512;shadows.pcf=false;
    shadows.depthBias=0;shadows.slopeBias=0;shadows.receiverBias=0;shadows.normalBias=0;
    shadows.enabled=false;auto zeroOff=pair();shadows.enabled=true;auto zeroOn=pair();
    Save(output/"zero-bias.ppm",zeroOn);std::cout<<"zero bias affected="<<acne(zeroOff,zeroOn)<<'\n';
    if(!baseline)Check(acne(zeroOff,zeroOn)<100,"receiver-plane depth correction failed with zero bias");
    if(!baseline) {
        shadows.receiverPlaneBias=false;auto uncorrected=pair();Save(output/"correction-off.ppm",uncorrected);
        Check(acne(zeroOff,uncorrected)>10000,"diagnostic correction OFF does not reproduce stripes");
        shadows.receiverPlaneBias=true;
        for(float normalBias:{0.f,.25f,1.f}) {
            shadows.normalBias=normalBias;auto corrected=pair();Check(acne(zeroOff,corrected)<10,"normal offset creates acne");
            Save(output/("normal-"+std::to_string(normalBias)+".ppm"),corrected);
        }
        shadows.normalBias=0;
        doubleSided=false;pose(receiver,Model({}, {-1,1,1}),{.15f,.4f,1,1});
        shadows.enabled=false;auto mirrorOff=pair();shadows.enabled=true;
        auto mirror=pair();Save(output/"mirror-off.ppm",mirrorOff);Save(output/"mirror.ppm",mirror);
        std::cout<<"mirrored plane affected="<<acne(mirrorOff,mirror)<<'\n';
        Check(acne(mirrorOff,mirror)<10,"mirrored inclined plane acne");
        Check(mirror[(H/2*W+W/2)*4+2]>60,"mirrored single-sided plane disappeared");
        doubleSided=true;reset();
    }
    shadows=ShadowSettings{};shadows.coverage=16;shadows.depthRange=30;shadows.resolution=512;
    caster=true;pose(receiver,Model({}, {1,.05f,1}),{.15f,.4f,1,1});
    shadows.enabled=false;auto off=pair();shadows.enabled=true;shadows.pcf=false;auto hard=pair();shadows.pcf=true;auto soft=pair();
    Save(output/"caster-off.ppm",off);Save(output/"caster-hard.ppm",hard);Save(output/"caster-soft.ppm",soft);
    int dark=0,fractional=0,hardFractional=0;double sumX=0;
    for(int y=30;y<H-30;++y)for(int x=40;x<W-40;++x) {
        auto p=(y*W+x)*4;
        if(off[p+2]>60&&hard[p+2]+40<off[p+2]) {++dark;sumX+=x;}
        if(off[p+2]>60&&soft[p+2]>hard[p+2]+4&&soft[p+2]+4<off[p+2])++fractional;
        if(off[p+2]>off[p]*2&&hard[p+2]>int(std::lround(255*LinearToSrgb(light.ambientIntensity)))+4&&hard[p+2]+4<off[p+2])++hardFractional;
    }
    std::cout<<"caster dark="<<dark<<" centroidX="<<sumX/std::max(dark,1)<<" soft edge="<<fractional<<'\n';
    Check(dark>60&&sumX/dark>W/2+10,"caster shadow geometric location incorrect");Check(fractional>4,"PCF edge missing");
    Check(hardFractional==0,"Hard shadow has artificial fractional filter footprint");
    if(!baseline) {
        fitted=true;
        int fixedLost=0;
        for(float yaw:{0.f,1.5f,3.f,4.5f}) {
            cameraYaw=yaw;shadows.cameraFitted=false;auto fixed=pair();
            Save(output/("fixed-camera-"+std::to_string(yaw)+".ppm"),fixed);
            if(fixed[(104*W+192)*4+2]>off[(104*W+192)*4+2]-3)++fixedLost;
            shadows.cameraFitted=true;
            cameraYaw=yaw;auto rotated=pair();Save(output/("camera-"+std::to_string(yaw)+".ppm"),rotated);
            Check(rotated[(104*W+192)*4+2]+40<off[(104*W+192)*4+2],"camera rotation loses nearby caster shadow");
        }
        Check(fixedLost>0,"fixed forward focus disappearance fixture did not reproduce");
        std::cout<<"fixed-focus lost shadow for "<<fixedLost<<"/4 camera orientations; fitted loses 0\n";
        for(float x:{-3.f,0.f,3.f}) {
            cameraPosition[0]=x;auto moved=pair();Check(moved[(104*W+192)*4+2]+40<off[(104*W+192)*4+2],"camera translation loses nearby shadow");
        }
        const auto topDown=vp;
        int cameraIndex=0;
        for(ShadowVector eye:{ShadowVector{8,10,-8},ShadowVector{-8,10,-8},ShadowVector{8,10,8}}) {
            const auto forward=ShadowUnit({-eye[0],-eye[1],-eye[2]});
            const auto right=ShadowUnit(ShadowCross({0,1,0},forward)),up=ShadowCross(forward,right);
            ShadowMatrix cameraView{right[0],up[0],forward[0],0,right[1],up[1],forward[1],0,right[2],up[2],forward[2],0,
                -ShadowDot(right,eye),-ShadowDot(up,eye),-ShadowDot(forward,eye),1};
            vp=ShadowMultiply(projection,cameraView);cameraPosition=eye;
            cameraYaw=std::atan2(forward[0],forward[2]);cameraPitch=std::asin(forward[1]);
            pose(receiver,receiver.model,receiver.tint);pose(box,box.model,box.tint);
            shadows.enabled=false;auto cameraOff=pair();shadows.enabled=true;auto cameraOn=pair();
            Save(output/("actual-camera-"+std::to_string(cameraIndex)+"-off.ppm"),cameraOff);
            Save(output/("actual-camera-"+std::to_string(cameraIndex++)+".ppm"),cameraOn);
            ShadowVector core{1.6f,0,.8f};float clip[2]{vp[12],vp[13]};
            for(int i=0;i<3;++i){clip[0]+=vp[i*4]*core[i];clip[1]+=vp[i*4+1]*core[i];}
            int cx=int((clip[0]*.5f+.5f)*W),cy=int((.5f-clip[1]*.5f)*H),changes=0;
            for(int y=std::max(0,cy-16);y<std::min(H,cy+16);++y)for(int x=std::max(0,cx-16);x<std::min(W,cx+16);++x) {
                auto p=(y*W+x)*4;if(cameraOff[p+2]>cameraOff[p]*2&&cameraOn[p+2]+20<cameraOff[p+2])++changes;
            }
            Check(changes>15,"actual camera view loses projected caster shadow region");
        }
        vp=topDown;pose(receiver,receiver.model,receiver.tint);pose(box,box.model,box.tint);
        fitted=false;shadows.pcf=false;
        // Contact geometry: caster touches the slightly inclined receiver.
        pose(box,Model({0,.515f,0}),{1,.5f,.2f,1});
        auto contact=pair();Save(output/"contact.ppm",contact);
        auto p=(H/2*W+174)*4;
        Check(contact[p+2]+40<off[p+2],"contact shadow detached from caster base");
        pose(box,Model({0,2,0}),{1,.5f,.2f,1});
        shadows.debugMode=1;auto visibility=pair();Save(output/"visibility.ppm",visibility);
        Check(visibility[(104*W+192)*4]<20,"visibility debug does not mark shadow");
        shadows.debugMode=2;shadows.coverage=6;auto coverage=pair();Save(output/"coverage.ppm",coverage);
        Check(coverage[(H/2*W+60)*4]>240&&coverage[(H/2*W+60)*4+1]<10,"coverage debug must distinguish outside");
        shadows.debugMode=0;shadows.coverage=16;
        for(float radius:{0.f,.5f,1.f,2.f}) {
            shadows.pcf=true;shadows.filterRadius=radius;auto filtered=pair();Save(output/("radius-"+std::to_string(radius)+".ppm"),filtered);
            if(radius==0)Check(filtered==hard,"zero-radius PCF must equal Hard");
        }
        shadows.pcf=false;shadows.filterRadius=1;
        shadows.coverage=4;shadows.edgeFade=.1f;
        const auto direction=ShadowUnit({.8f,-1,.4f});const auto right=ShadowUnit(ShadowCross({0,1,0},direction));
        int previous=0;
        for(float offset:{1.4f,1.7f,1.9f,2.1f}) {
            for(int i=0;i<3;++i)focus[i]=right[i]*offset;
            auto edge=pair();int value=edge[(104*W+192)*4+2];
            Check(value+3>=previous,"shadow edge fade is not monotonic");previous=value;
            Save(output/("edge-"+std::to_string(offset)+".ppm"),edge);
        }
        Check(previous>off[(104*W+192)*4+2]-3,"out-of-frustum receiver must become lit");
        focus={};shadows.coverage=16;shadows.edgeFade=.05f;
        // Bias recreation must retain previously recorded mirrored PSOs too.
        for(float rotation:{0.f,.4f,.9f}) {
            pose(box,Model({0,2,0},{1.8f,.7f,.6f},{.2f,rotation,.3f}),{1,.5f,.2f,1});
            auto rotated=pair();Save(output/("cube-"+std::to_string(rotation)+".ppm"),rotated);
        }
        pose(box,Model({0,2,0}),{1,.5f,.2f,1});
    }
    auto depth=gpu.ReadBackShadowDepth();std::ofstream f(output/"depth.pgm",std::ios::binary);f<<"P5\n512 512\n255\n";
    for(float d:depth){Check(std::isfinite(d)&&d>=0&&d<=1,"invalid shadow depth");unsigned char v=static_cast<unsigned char>(d*255);f.write(reinterpret_cast<char*>(&v),1);}f.close();
    int lightIndex=0;
    for(auto direction:{ShadowVector{.1f,-1,.1f},ShadowVector{1,-.15f,.3f},ShadowVector{-.5f,-1,-.2f}}) {
        pose(box,lightIndex==1?Model({0,.75f,0},{.5f,.5f,.5f}):Model({0,2,0}),{1,.5f,.2f,1});
        light.directional.direction=direction;shadows.enabled=false;auto off=pair();shadows.enabled=true;auto on=pair();
        if(lightIndex==1) {
            const float t=.75f/(-direction[1]+.015f*direction[0]+.01f*direction[2]);
            const int x=int((.5f+direction[0]*t/16)*W),y=int((.5f-direction[2]*t/12)*H);
            auto p=(y*W+x)*4;Check(on[p+2]+12<off[p+2],"low-angle caster misses analytic floor shadow location");
        }
        Save(output/("light-"+std::to_string(lightIndex)+"-off.ppm"),off);Save(output/("light-"+std::to_string(lightIndex++)+".ppm"),on);
    }
    Check(gpu.GetValidationErrorCount()==0,"DX12 ERROR/CORRUPTION");
    gpu.DestroyMesh(panel);gpu.DestroyMesh(cube);gpu.DestroyTexture(texture);gpu.DestroyShader(shader);gpu.Shutdown();
    std::cout<<"Shadow quality GPU readback passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Shadow quality FAILED: "<<e.what()<<'\n';return 1;}}
