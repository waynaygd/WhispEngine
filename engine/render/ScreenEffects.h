#pragma once
#include <algorithm>
#include <array>
#include <cmath>
// Display-referred artistic adjustments, after the existing exposure/tonemapper.
struct ScreenEffects {
    float saturation=1,contrast=1,brightness=0;
    std::array<float,4> tint{1,1,1,1};
    float vignetteIntensity=0,vignetteSmoothness=.5f,vignetteRoundness=1;
    float grainIntensity=0,grainScale=1;
    float aberrationIntensity=0,aberrationFalloff=2;
    bool operator==(const ScreenEffects&)const=default;
    bool Active()const {
        return saturation!=1||contrast!=1||brightness!=0||tint[0]!=1||tint[1]!=1||tint[2]!=1||
               vignetteIntensity>0||grainIntensity>0||aberrationIntensity>0;
    }
};
inline ScreenEffects SanitizeScreenEffects(ScreenEffects value){
    auto bounded=[](float v,float lo,float hi,float fallback){return std::isfinite(v)?std::clamp(v,lo,hi):fallback;};
    value.saturation=bounded(value.saturation,0,2,1);value.contrast=bounded(value.contrast,0,2,1);
    value.brightness=bounded(value.brightness,-1,1,0);
    for(auto& channel:value.tint)channel=bounded(channel,0,1,1);
    value.vignetteIntensity=bounded(value.vignetteIntensity,0,1,0);
    value.vignetteSmoothness=bounded(value.vignetteSmoothness,.01f,1,.5f);
    value.vignetteRoundness=bounded(value.vignetteRoundness,0,1,1);
    value.grainIntensity=bounded(value.grainIntensity,0,1,0);value.grainScale=bounded(value.grainScale,.5f,4,1);
    value.aberrationIntensity=bounded(value.aberrationIntensity,0,1,0);value.aberrationFalloff=bounded(value.aberrationFalloff,.1f,8,2);
    return value;
}
