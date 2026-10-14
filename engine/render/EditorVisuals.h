#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <string>

enum class EditorBackground { Solid,Gradient,Cubemap };
enum class SunSyncMode {Independent,LightDrivesSky,SkyDrivesLight};
struct SceneBackground {
    SunSyncMode sunSync=SunSyncMode::Independent;
    bool sunMetadata=false;
    std::array<float,3> sunDirection{0,1,0};
    float sunRadius=.53f,sunIntensity=1;
    EditorBackground background=EditorBackground::Gradient;
    std::array<float,3> solid{.008f,.012f,.02f},top{.045f,.12f,.25f},horizon{.34f,.43f,.55f},bottom{.055f,.065f,.085f};
    float skyIntensity=1,gradientBlend=.65f,skyRotation=0;
    std::string cubemap="environments/studio.hdr";
    bool useIblEnvironment=false;
    bool operator==(const SceneBackground&)const=default;
};
struct EditorVisualSettings : SceneBackground {
    bool enabled=true;
    float minorSpacing=1,majorSpacing=10,gridOpacity=.4f,fadeDistance=180;
    bool operator==(const EditorVisualSettings&)const=default;
};
inline bool ValidEditorVisualSettings(const EditorVisualSettings& s){
    auto range=[](float x,float low,float high){return std::isfinite(x)&&x>=low&&x<=high;};
    if(int(s.background)<0||int(s.background)>2||!range(s.minorSpacing,.01f,1000)||!range(s.majorSpacing,s.minorSpacing,10000)||!range(s.gridOpacity,0,1)||!range(s.fadeDistance,1,100000)||!range(s.skyIntensity,0,32)||!range(s.gradientBlend,.1f,8)||!range(s.skyRotation,-36000,36000))return false;
    for(const auto* c:{&s.solid,&s.top,&s.horizon,&s.bottom})for(float v:*c)if(!range(v,0,32))return false;
    return true;
}
struct EditorVisualFrame {
    EditorVisualSettings settings;
    bool active=false,play=false,showGrid=true,backgroundEnabled=true;
    std::array<float,3> camera{0,4,-6},right{1,0,0},up{0,1,0},forward{0,0,1};
    std::array<float,3> sunDirection{0,1,0},sunColor{1,1,1};
    bool sunVisible=false;
    float tanHalfFov=.57735027f,aspect=1,nearPlane=.01f,farPlane=1000;
};
inline bool EditorGridVisible(const EditorVisualFrame& f){return f.active&&f.settings.enabled&&f.showGrid&&f.settings.gridOpacity>0&&!f.play;}
struct EditorVisualDiagnostics {
    unsigned gridDraws=0,backgroundDraws=0;
    double cpuMs=0;
    bool skyReady=false;
    std::string error;
};
