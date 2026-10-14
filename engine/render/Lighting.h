#pragma once
#include <array>
#include <cmath>

// Runtime render configuration, independent of graphics API and scene entities.
// Direction is the direction in which rays travel (world space).
struct DirectionalLight {
    std::array<float,3> direction{-.4f,-1.f,.3f};
    // Linear radiance chromaticity; editor controls convert display sRGB.
    std::array<float,3> color{1.f,.95f,.85f};
    float intensity=.85f;
    bool enabled=true;
    bool operator==(const DirectionalLight&)const=default;
};
struct LightingSettings {
    bool enabled=true;
    DirectionalLight directional;
    std::array<float,3> ambientColor{1,1,1};
    float ambientIntensity=.25f;
    bool operator==(const LightingSettings&)const=default;
};
struct LightingConstants {
    std::array<float,4> directionEnabled{};
    std::array<float,4> colorIntensity{};
    std::array<float,4> ambientIntensity{};
};
inline float FinitePositive(float v) { return std::isfinite(v) ? std::fmax(0.f,v) : 0.f; }
inline LightingConstants BuildLightingConstants(const LightingSettings& s) {
    LightingConstants c;
    double length=0;
    for(float v:s.directional.direction)length+=double(v)*v;
    const bool valid=std::isfinite(length)&&length>1e-16;
    for(int i=0;i<3;++i) {
        c.directionEnabled[i]=valid ? float(s.directional.direction[i]/std::sqrt(length)) : (i==1?-1.f:0.f);
        c.colorIntensity[i]=FinitePositive(s.directional.color[i]);
        c.ambientIntensity[i]=FinitePositive(s.ambientColor[i]);
    }
    c.directionEnabled[3]=s.enabled?1.f:0.f;
    c.colorIntensity[3]=s.directional.enabled?FinitePositive(s.directional.intensity):0.f;
    c.ambientIntensity[3]=FinitePositive(s.ambientIntensity);
    return c;
}
static_assert(sizeof(LightingConstants)==48);

// Three padded ROWS of inverse-transpose(model3x3), suitable for HLSL dot.
// Singular/non-finite transforms return zero: shader uses unlit fallback.
inline std::array<float,12> BuildNormalMatrix(const std::array<float,16>& m) {
    const double a=m[0],b=m[4],c=m[8],d=m[1],e=m[5],f=m[9],g=m[2],h=m[6],i=m[10];
    const double det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    std::array<float,12> n{};
    if(!std::isfinite(det)||det==0)return n;
    const double cofactor[9]={e*i-f*h,f*g-d*i,d*h-e*g,c*h-b*i,a*i-c*g,b*g-a*h,b*f-c*e,c*d-a*f,a*e-b*d};
    for(int row=0;row<3;++row)for(int col=0;col<3;++col) {
        const double v=cofactor[row*3+col]/det;
        if(!std::isfinite(v)||std::abs(v)>3.4e38)return {};
        n[row*4+col]=float(v);
    }
    return n;
}
