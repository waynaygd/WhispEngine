#pragma once
#include "SceneLights.h"

enum class FogMode { Distance, Height, Volumetric };
enum class FogQuality { Low, Medium, High };
struct FogSettings {
    bool enabled=true;
    FogMode mode=FogMode::Distance;
    std::array<float,4> color{.55f,.65f,.75f,1}; // linear RGB
    float density=1, startDistance=10, endDistance=100;
    float baseHeight=0, heightDensity=.04f, heightFalloff=.25f;
    std::array<float,4> scatteringColor{.8f,.85f,.9f,1}; // scattering albedo
    float extinction=.04f, anisotropy=.2f, maxDistance=150;
    FogQuality quality=FogQuality::Medium;
    bool sceneLighting=true;
    bool operator==(const FogSettings&)const=default;
};
inline unsigned FogSamples(FogQuality q){return q==FogQuality::Low?16:q==FogQuality::High?64:32;}
inline bool ValidFog(const FogSettings& s){
    auto range=[](float v,float lo,float hi){return std::isfinite(v)&&v>=lo&&v<=hi;};
    if(int(s.mode)<0||int(s.mode)>2||int(s.quality)<0||int(s.quality)>2)return false;
    for(auto c:s.color)if(!range(c,0,1))return false;
    for(auto c:s.scatteringColor)if(!range(c,0,1))return false;
    return range(s.density,0,1)&&range(s.startDistance,0,10000)&&range(s.endDistance,.001f,10000)&&s.endDistance>s.startDistance&&
        range(s.baseHeight,-10000,10000)&&range(s.heightDensity,0,10)&&range(s.heightFalloff,0,10)&&
        range(s.extinction,0,10)&&range(s.anisotropy,-.95f,.95f)&&range(s.maxDistance,.01f,10000);
}
inline double DistanceFogAmount(const FogSettings& s,double distance){return s.density*std::clamp((distance-s.startDistance)/(s.endDistance-s.startDistance),0.,1.);}
// Integral of exp(-falloff * max(y-base,0)) along a segment. Below base the
// density is constant; splitting at base avoids overflow looking down/up.
inline double HeightFogIntegral(double y0,double y1,double length,double base,double falloff){
    if(length<=0)return 0;
    double a=y0-base,b=y1-base;
    if(falloff<1e-8||std::max(a,b)<=0)return length;
    if(a*b<0){double f=std::abs(a)/(std::abs(a)+std::abs(b));return HeightFogIntegral(y0,base,length*f,base,falloff)+HeightFogIntegral(base,y1,length*(1-f),base,falloff);}
    a=std::max(0.,a)*falloff;b=std::max(0.,b)*falloff;
    if(std::abs(b-a)<1e-5)return length*std::exp(-(a+b)*.5);
    return length*(std::exp(-a)-std::exp(-b))/(b-a);
}
inline double FogPhase(double cosine,double g){return (1-g*g)/(12.566370614359172*std::pow(std::max(1e-5,1+g*g-2*g*cosine),1.5));}
struct FogFrame {FogSettings settings;LightCamera camera;bool active=false;unsigned activeCount=0,invalidCount=0;};
struct FogDiagnostics {
    bool supported=false,active=false;
    unsigned activeCount=0,samples=0,localLights=0;
    std::uint64_t gpuBytes=0;
    double cpuMs=0,gpuMs=0;
    std::string error="Fog is not supported by this render backend";
};
