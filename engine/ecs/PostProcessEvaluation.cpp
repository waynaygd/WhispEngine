#include "PostProcessEvaluation.h"
#include "components/SceneIdentityComponent.h"
#include "RenderPose.h"
#include <algorithm>
#include <tuple>
namespace ecs {
std::string ValidatePostProcessVolume(const PostProcessVolumeComponent& volume){
    auto range=[](float value,float lo,float hi){return std::isfinite(value)&&value>=lo&&value<=hi;};
    if(!range(volume.weight,0,1))return "Blend Weight must be finite in [0, 1].";
    if(!range(volume.blendRadius,0,10000))return "Blend Radius must be finite in [0, 10000].";
    for(float axis:{volume.extents.x,volume.extents.y,volume.extents.z})if(!range(axis,.001f,10000))return "Box half extents must be finite in [0.001, 10000].";
    if(volume.shape!=0)return "Only Box shape is supported.";
    if(volume.priority<-1000000||volume.priority>1000000)return "Priority must be in [-1000000, 1000000].";
#define PP_CHECK(name,target,lo,hi,section,label) if(volume.name&&!range(*volume.name,lo,hi))return std::string(section)+" / "+label+" is nonfinite or outside its supported range.";
    WHISP_PP_FLOATS(PP_CHECK)
#undef PP_CHECK
    if(volume.fogStart&&volume.fogEnd&&*volume.fogEnd<=*volume.fogStart)return "Fog End Distance must exceed Start Distance.";
    if(volume.fogMode&&(int(*volume.fogMode)<0||int(*volume.fogMode)>2))return "Invalid Fog Mode.";
    if(volume.fogQuality&&(int(*volume.fogQuality)<0||int(*volume.fogQuality)>2))return "Invalid Fog Quality.";
    for(const auto* color:{&volume.fogColor,&volume.scatteringColor})if(*color)for(float channel:**color)if(!range(channel,0,1))return "Fog colors must be finite in [0, 1].";
    if(volume.tint)for(float channel:*volume.tint)if(!range(channel,0,1))return "Color Tint must be finite in [0, 1].";
    return {};
}
std::optional<PostProcessBox> GetPostProcessBox(const World& world,Entity entity,const PostProcessVolumeComponent& volume){
    if(!world.IsAlive(entity)||!world.HasComponent<TransformComponent>(entity))return {};
    try{
        float m[16];BuildRenderModelMatrix(m,RenderPose::From(world.WorldTransform(entity)));
        PostProcessBox box;box.center={m[12],m[13],m[14]};const float extents[]={volume.extents.x,volume.extents.y,volume.extents.z};
        for(int axis=0;axis<3;++axis){float length=std::sqrt(m[axis*4]*m[axis*4]+m[axis*4+1]*m[axis*4+1]+m[axis*4+2]*m[axis*4+2]);
            if(!std::isfinite(length)||length<1e-6f)return {};box.axes[axis]={m[axis*4]/length,m[axis*4+1]/length,m[axis*4+2]/length};box.half[axis]=extents[axis]*length;if(!std::isfinite(box.half[axis]))return {};
        }
        for(float v:{box.center.x,box.center.y,box.center.z})if(!std::isfinite(v))return {};return box;
    }catch(...){return {};}
}
float PostProcessSpatialWeight(Vec3 camera,const PostProcessBox& box,float radius){
    const Vec3 delta{camera.x-box.center.x,camera.y-box.center.y,camera.z-box.center.z};float squared=0;
    for(int axis=0;axis<3;++axis){const auto& v=box.axes[axis];float excess=std::max(0.f,std::abs(delta.x*v.x+delta.y*v.y+delta.z*v.z)-box.half[axis]);squared+=excess*excess;}
    if(squared==0)return 1;if(radius<=0)return 0;
    float t=std::clamp(std::sqrt(squared)/radius,0.f,1.f);return 1-t*t*(3-2*t);
}
EvaluatedPostProcess EvaluatePostProcess(Vec3 camera,const World& world,const PostProcessSettings& base){
    EvaluatedPostProcess result;static_cast<PostProcessSettings&>(result)=base;
    if(!std::isfinite(camera.x)||!std::isfinite(camera.y)||!std::isfinite(camera.z))return result;
    struct Volume {Entity entity;const PostProcessVolumeComponent* value;std::string stableId;float weight;};std::vector<Volume> volumes;
    for(auto entity:world.Entities())if(auto* volume=world.GetComponent<PostProcessVolumeComponent>(entity)){
        if(!ValidatePostProcessVolume(*volume).empty()){++result.invalidVolumes;continue;}
        if(!volume->enabled||volume->weight==0)continue;float weight=volume->weight;
        if(!volume->global){auto box=GetPostProcessBox(world,entity,*volume);if(!box){++result.invalidVolumes;continue;}weight*=PostProcessSpatialWeight(camera,*box,volume->blendRadius);}
        if(weight<=0)continue;auto* id=world.GetComponent<SceneIdentityComponent>(entity);volumes.push_back({entity,volume,id?id->value:std::string(),weight});
    }
    std::sort(volumes.begin(),volumes.end(),[](const auto& a,const auto& b){return std::tie(a.value->priority,a.stableId,a.entity.index,a.entity.generation)<std::tie(b.value->priority,b.stableId,b.entity.index,b.entity.generation);});
    float activation=base.fog.enabled?1.f:0.f;bool activationOverride=false;
    for(const auto& entry:volumes){const auto& volume=*entry.value;float weight=entry.weight;++result.activeVolumes;
        auto blend=[&](float& target,float value){target+=(value-target)*weight;};
#define PP_BLEND(name,target,lo,hi,section,label) if(volume.name)blend(result.target,*volume.name);
        WHISP_PP_FLOATS(PP_BLEND)
#undef PP_BLEND
        if(volume.fogEnabled){blend(activation,*volume.fogEnabled?1.f:0.f);activationOverride=true;}
        auto color=[&](auto& target,const auto& value){if(value)for(int c=0;c<4;++c)blend(target[c],(*value)[c]);};color(result.fog.color,volume.fogColor);color(result.fog.scatteringColor,volume.scatteringColor);color(result.screenEffects.tint,volume.tint);
        if(weight>=.5f){if(volume.fogMode)result.fog.mode=*volume.fogMode;if(volume.fogQuality)result.fog.quality=*volume.fogQuality;if(volume.sceneLighting)result.fog.sceneLighting=*volume.sceneLighting;if(volume.toneMapping)result.environment.toneMapping=*volume.toneMapping;}
    }
    if(activationOverride){result.fog.enabled=activation>0;result.fog.density*=activation;}
    if(result.activeVolumes){result.fog.startDistance=std::min(result.fog.startDistance,9999.998f);result.fog.endDistance=std::max(result.fog.endDistance,result.fog.startDistance+.001f);}
    return result;
}
}
