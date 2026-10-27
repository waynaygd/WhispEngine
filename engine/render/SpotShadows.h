#pragma once
#include "Shadows.h"
#include "SceneLights.h"
#include <optional>
inline constexpr unsigned MaxSpotShadows=4;
struct SpotShadowFrame {
    ShadowFrame shadow;
    unsigned lightIndex=0;
    float receiverBias=.002f,strength=1;
    bool pcf=true;
};
struct alignas(16) SpotShadowGpu {
    ShadowMatrix lightVP{};
    std::array<float,4> sampling{}; // inverse resolution, receiver bias, strength, radius
    std::array<float,4> config{};   // light index + 1 (zero disables), PCF, reserved
};
static_assert(sizeof(SpotShadowGpu)==96);
struct SpotShadowDiagnostics {
    unsigned requested=0,active=0,overBudget=0,failed=0,drawCalls=0;
    std::uint64_t mapBytes=0;
    double gpuMs=0;
    std::string error;
};
inline std::optional<SpotShadowFrame> BuildSpotShadowFrame(unsigned index,const GpuSceneLight& light,LightParameters parameters,ShadowVector referenceUp={0,1,0}){
    auto p=SanitizeLight(parameters);
    ShadowVector position{light.positionRange[0],light.positionRange[1],light.positionRange[2]},direction{light.directionType[0],light.directionType[1],light.directionType[2]};
    for(float c:position)if(!std::isfinite(c)||std::abs(c)>1e6f)return {};
    float length=ShadowDot(direction,direction);if(!std::isfinite(length)||length<1e-16f)return {};
    direction=ShadowUnit(direction);
    auto right=ShadowCross(referenceUp,direction);float rightLength=ShadowDot(right,right);
    if(!std::isfinite(rightLength)||rightLength<1e-12f)right=ShadowCross(std::abs(direction[1])>.95f?ShadowVector{0,0,1}:ShadowVector{0,1,0},direction);
    right=ShadowUnit(right);
    auto up=ShadowCross(direction,right);
    const float farPlane=p.range,nearPlane=std::clamp(farPlane*.001f,.001f,.05f);
    const float scale=1/std::tan(p.outerAngle*.0174532925199433f);
    SpotShadowFrame result;result.lightIndex=index;result.receiverBias=p.shadowBias;result.strength=p.shadowStrength;result.pcf=p.shadowPcf;
    auto& frame=result.shadow;frame.settings.resolution=p.shadowResolution;frame.settings.depthBias=0;frame.settings.slopeBias=1;
    frame.view={right[0],up[0],direction[0],0,right[1],up[1],direction[1],0,right[2],up[2],direction[2],0,-ShadowDot(right,position),-ShadowDot(up,position),-ShadowDot(direction,position),1};
    frame.projection={scale,0,0,0,0,scale,0,0,0,0,farPlane/(farPlane-nearPlane),1,0,0,-nearPlane*farPlane/(farPlane-nearPlane),0};
    frame.constants.lightVP=ShadowMultiply(frame.projection,frame.view);
    for(float v:frame.constants.lightVP)if(!std::isfinite(v))return {};
    return result;
}
inline bool SpotCasterIntersects(const SpotShadowFrame& frame,const ShadowCasterBounds& bounds){
    unsigned outside=63;
    for(int corner=0;corner<8;++corner){float clip[4];
        for(int row=0;row<4;++row){clip[row]=frame.shadow.constants.lightVP[12+row];for(int k=0;k<3;++k)clip[row]+=frame.shadow.constants.lightVP[k*4+row]*(corner&(1<<k)?bounds.maximum[k]:bounds.minimum[k]);}
        outside&=(clip[0]<-clip[3]?1:0)|(clip[0]>clip[3]?2:0)|(clip[1]<-clip[3]?4:0)|(clip[1]>clip[3]?8:0)|(clip[2]<0?16:0)|(clip[2]>clip[3]?32:0);
    }
    return outside==0;
}
