#pragma once
#include "Lighting.h"
#include <algorithm>
#include <cstdint>
#include <span>

struct ShadowSettings {
    bool enabled=true, pcf=true;
    std::uint32_t resolution=2048;
    float coverage=48.f, focusDistance=32.f, depthRange=100.f;
    int depthBias=100;
    float slopeBias=1.f, receiverBias=.00002f, normalBias=.25f, strength=1.f;
    bool cameraFitted=true, receiverPlaneBias=true;
    float filterRadius=1.5f, edgeFade=.05f;
    int filterTaps=5; // 3: legacy point PCF, 5: continuous weighted tent PCF
    int debugMode=0; // 0 shaded, 1 visibility, 2 coverage (red outside, green inside)
    bool operator==(const ShadowSettings&) const=default;
};
enum class ShadowQuality {Low,Medium,High,Ultra,Custom};
inline void ApplyShadowQuality(ShadowSettings& s,ShadowQuality q){
    if(q==ShadowQuality::Custom)return;
    s.pcf=q!=ShadowQuality::Low;s.filterTaps=q==ShadowQuality::Medium?3:5;
    s.resolution=q==ShadowQuality::Low?512:q==ShadowQuality::Medium?1024:2048;
    s.filterRadius=q==ShadowQuality::Ultra?2.f:1.5f;
}
inline ShadowQuality GetShadowQuality(const ShadowSettings& s){
    for(int i=0;i<4;++i){auto copy=s;ApplyShadowQuality(copy,ShadowQuality(i));if(copy==s)return ShadowQuality(i);}return ShadowQuality::Custom;
}
inline std::uint64_t ShadowMapBytes(const ShadowSettings& s){return std::uint64_t(s.resolution)*s.resolution*4;}
using ShadowMatrix=std::array<float,16>;
using ShadowVector=std::array<float,3>;
inline float ShadowDot(ShadowVector a,ShadowVector b) {return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
inline ShadowVector ShadowCross(ShadowVector a,ShadowVector b) {
    return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
}
inline ShadowVector ShadowUnit(ShadowVector v) {
    const float length=std::sqrt(ShadowDot(v,v));
    for(auto& c:v)c/=length;return v;
}
inline ShadowMatrix ShadowMultiply(const ShadowMatrix& a,const ShadowMatrix& b) {
    ShadowMatrix r{};
    for(int c=0;c<4;++c)for(int row=0;row<4;++row)for(int k=0;k<4;++k)r[c*4+row]+=a[k*4+row]*b[c*4+k];
    return r;
}
inline float ShadowClamp(float value,float minimum,float maximum,float fallback) {
    return std::isfinite(value)?std::clamp(value,minimum,maximum):fallback;
}
inline ShadowSettings SanitizeShadowSettings(ShadowSettings s) {
    if(s.resolution!=512&&s.resolution!=1024&&s.resolution!=2048&&s.resolution!=4096)s.resolution=1024;
    s.coverage=ShadowClamp(s.coverage,2,500,48);s.focusDistance=ShadowClamp(s.focusDistance,0,500,32);
    s.depthRange=ShadowClamp(s.depthRange,2,1000,100);s.depthBias=std::clamp(s.depthBias,0,10000);
    s.slopeBias=ShadowClamp(s.slopeBias,0,8,1);s.receiverBias=ShadowClamp(s.receiverBias,0,.01f,.0003f);
    s.normalBias=ShadowClamp(s.normalBias,0,4,.25f);s.strength=ShadowClamp(s.strength,0,1,1);
    s.filterTaps=s.filterTaps==3?3:5;
    s.filterRadius=ShadowClamp(s.filterRadius,0,2,1);s.edgeFade=ShadowClamp(s.edgeFade,0,.25f,.05f);
    s.debugMode=std::clamp(s.debugMode,0,2);return s;
}
struct ShadowConstants {
    ShadowMatrix lightVP{};
    std::array<float,4> parameters{}; // enabled, strength, PCF enabled, debug mode
    std::array<float,4> sampling{};   // inverse resolution, receiver depth bias, normal offset in world units, filter radius
    std::array<float,4> coverage{};   // XY fade fraction, Z fade fraction, receiver-plane correction, reserved
};
static_assert(sizeof(ShadowConstants)==112);
struct ShadowFrame {
    ShadowSettings settings;
    ShadowMatrix view{}, projection{};
    ShadowConstants constants;
};
// LH view, column-major matrices, DX clip depth [0,1]. Light rays point forward.
// Snap the light-space XY center, not individual objects or physical poses.
inline ShadowFrame BuildDirectionalShadowFrame(const LightingConstants& light,ShadowSettings settings,ShadowVector focus) {
    ShadowFrame frame;frame.settings=SanitizeShadowSettings(settings);
    const auto& s=frame.settings;
    for(auto& c:focus)c=ShadowClamp(c,-1e6f,1e6f,0);
    ShadowVector forward{light.directionEnabled[0],light.directionEnabled[1],light.directionEnabled[2]};
    float len=ShadowDot(forward,forward);if(!std::isfinite(len)||len<1e-16f)forward={0,-1,0};else forward=ShadowUnit(forward);
    const ShadowVector up=std::abs(forward[1])>.95f?ShadowVector{0,0,1}:ShadowVector{0,1,0};
    const auto right=ShadowUnit(ShadowCross(up,forward)), vertical=ShadowCross(forward,right);
    const float texel=s.coverage/s.resolution;
    const float centerX=std::round(ShadowDot(right,focus)/texel)*texel;
    const float centerY=std::round(ShadowDot(vertical,focus)/texel)*texel;
    const float eyeZ=ShadowDot(forward,focus)-s.depthRange*.5f;
    frame.view={right[0],vertical[0],forward[0],0,right[1],vertical[1],forward[1],0,
                right[2],vertical[2],forward[2],0,-centerX,-centerY,-eyeZ,1};
    frame.projection={2/s.coverage,0,0,0,0,2/s.coverage,0,0,0,0,1/s.depthRange,0,0,0,0,1};
    frame.constants.lightVP=ShadowMultiply(frame.projection,frame.view);
    frame.constants.parameters={s.enabled&&light.directionEnabled[3]>.5f&&light.colorIntensity[3]>0&&s.strength>0?1.f:0.f,s.strength,s.pcf?1.f:0.f,float(s.debugMode)};
    frame.constants.sampling={1.f/s.resolution,s.receiverBias,s.normalBias*texel,s.filterRadius};
    frame.constants.coverage={s.edgeFade,s.edgeFade,s.receiverPlaneBias?1.f:0.f,float(s.filterTaps)};return frame;
}
// Fit a rotation-invariant sphere enclosing the near camera frustum. Quantize its
// radius (not the individual corners) so yaw/pitch cannot pump the projection scale.
// coverage is a minimum width; focusDistance is the fitted shadow distance.
inline ShadowFrame BuildCameraDirectionalShadowFrame(const LightingConstants& light,ShadowSettings settings,
    ShadowVector position,float yaw,float pitch,float fov,float aspect,float nearPlane,float farPlane) {
    settings=SanitizeShadowSettings(settings);
    yaw=ShadowClamp(yaw,-1e6f,1e6f,0);pitch=ShadowClamp(pitch,-1.57f,1.57f,0);
    const ShadowVector forward{std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
    float distance=settings.focusDistance;
    if(settings.cameraFitted) {
        nearPlane=ShadowClamp(nearPlane,.001f,100,.01f);
        farPlane=ShadowClamp(farPlane,nearPlane+.001f,10000,150);
        const float far=std::clamp(distance,nearPlane+.001f,farPlane);
        const float center=(nearPlane+far)*.5f;
        const float tangent=std::tan(ShadowClamp(fov,.05f,2.8f,1.04719755f)*.5f);
        aspect=ShadowClamp(aspect,.1f,10,1.7777778f);
        const float radius=std::ceil(std::sqrt((far-center)*(far-center)+far*far*tangent*tangent*(1+aspect*aspect)));
        settings.coverage=std::max(settings.coverage,2*radius);
        settings.coverage+=2*settings.coverage/settings.resolution; // snapping guard
        settings.depthRange=std::max(settings.depthRange,settings.coverage+20.f); // upstream caster margin
        distance=center;
    }
    ShadowVector focus=position;for(int i=0;i<3;++i)focus[i]+=forward[i]*distance;
    return BuildDirectionalShadowFrame(light,settings,focus);
}
struct RenderGpuTimings {
    bool valid=false;
    double shadowMs=0,mainMs=0,frameMs=0,outputMs=0,customPostFxMs=0,runtimeUIMs=0;
};

// Include upstream opaque casters whose light-space XY bounds overlap the receiver map.
// Camera fitting defines receivers, not the set of potential off-camera occluders.
struct ShadowCasterBounds {ShadowVector minimum{},maximum{};};
inline bool ExtendDirectionalCasterDepth(ShadowFrame& f,std::span<const ShadowCasterBounds> casters){
 if(!f.settings.cameraFitted||f.constants.parameters[0]<.5f)return false;
 float upstream=0;const float half=f.settings.coverage*.5f;
 for(const auto& b:casters){ShadowVector lo{INFINITY,INFINITY,INFINITY},hi{-INFINITY,-INFINITY,-INFINITY};
  for(int corner=0;corner<8;++corner){ShadowVector p;for(int k=0;k<3;++k)p[k]=corner&(1<<k)?b.maximum[k]:b.minimum[k];
   for(int row=0;row<3;++row){float v=f.view[12+row];for(int k=0;k<3;++k)v+=f.view[k*4+row]*p[k];lo[row]=std::min(lo[row],v);hi[row]=std::max(hi[row],v);}}
  if(hi[0]<-half||lo[0]>half||hi[1]<-half||lo[1]>half)continue;
  upstream=std::min(upstream,lo[2]);
 }
 if(!std::isfinite(upstream)||upstream>=0)return false;
 // Quantized and bounded to avoid projection pumping and unbounded precision loss.
 const float extension=std::min(4096.f,std::ceil(-upstream+1.f));const float oldRange=f.settings.depthRange;
 f.view[14]+=extension;f.settings.depthRange+=extension;
 // Preserve the original world-space fade width and receiver comparison offset.
 // Otherwise a long upstream extension would fade receivers near the unchanged far plane.
 f.constants.coverage[1]*=oldRange/f.settings.depthRange;f.constants.sampling[1]*=oldRange/f.settings.depthRange;
 f.projection[10]=1/f.settings.depthRange;f.constants.lightVP=ShadowMultiply(f.projection,f.view);return true;
}
