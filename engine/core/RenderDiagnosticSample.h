#pragma once
#include "../ecs/systems/RenderSystem.h"
#include <fstream>
#include <iomanip>
#include <vector>
#include <string>
struct RenderDiagnosticSample {
    unsigned frame=0;
    bool instancing=false;
    double frameMs=0;
    ecs::RenderSystem::Statistics render;
    bool lighting=true,shadows=false,pcf=false;
    unsigned resolution=0;
    RenderGpuTimings gpu;
    int shadingModel=-1;
    unsigned textureMapMask=7,iblMask=0;
    EnvironmentDiagnostics environment;
    SceneLightDiagnostics lights;
};
inline bool SaveRenderDiagnostics(const std::string& path,const std::vector<RenderDiagnosticSample>& samples) {
    std::ofstream out(path);
    if(!out)return false;
    out<<"frame,instancing,frameMs,objects,drawCalls,instancedDrawCalls,instances,capacity,uploadBytes,bufferGrowths,gatherMs,prepareMs,batchMs,submitMs,lighting,shadows,pcf,resolution,shadowDraws,shadowInstancedDraws,casters,shadowPrepareMs,shadowCpuMs,gpuValid,shadowGpuMs,mainGpuMs,frameGpuMs,materialResolveMs,materialCacheHits,materialCacheMisses,submeshes,shadowCoverage,shadowDepthRange,shadingModel,textureMapMask,frameConstantUploadBytes,textureMemoryBytes,materialDescriptors,iblMask,environmentGpuBytes,environmentGeneration,environmentPreprocessMs,environmentUploadMs,outputDrawCalls,lightTotal,lightEnabled,lightCandidates,lightDirectional,lightPoint,lightSpot,tiled,tiles,lightIndices,lightsPerTile,lightMaxPerTile,lightOverflow,lightGatherMs,lightCullCpuMs,lightCullGpuMs,lightUploadBytes,lightGpuBytes\n"<<std::fixed<<std::setprecision(6);
    for(const auto& s:samples) {
        const auto& r=s.render;const auto& g=r.submitted;
        out<<s.frame<<','<<s.instancing<<','<<s.frameMs<<','<<r.visibleObjects<<','<<g.drawCalls<<','<<g.instancedDrawCalls<<','<<g.renderedInstances<<','<<g.instanceBufferCapacity<<','<<g.uploadBytes<<','<<g.instanceBufferGrowths<<','<<r.gatherMs<<','<<r.prepareMs<<','<<r.batchMs<<','<<r.submitMs<<','<<s.lighting<<','<<s.shadows<<','<<s.pcf<<','<<s.resolution<<','<<g.shadowDrawCalls<<','<<g.shadowInstancedDrawCalls<<','<<g.shadowCasters<<','<<r.shadowPrepareMs<<','<<r.shadowMs<<','<<s.gpu.valid<<','<<s.gpu.shadowMs<<','<<s.gpu.mainMs<<','<<s.gpu.frameMs<<','<<r.materialResolveMs<<','<<r.materialCacheHits<<','<<r.materialCacheMisses<<','<<r.visibleSubmeshes<<','<<g.shadowCoverage<<','<<g.shadowDepthRange<<','<<s.shadingModel<<','<<s.textureMapMask<<','<<g.frameConstantUploadBytes<<','<<g.textureMemoryBytes<<','<<g.materialDescriptors<<','<<s.iblMask<<','<<s.environment.gpuBytes<<','<<s.environment.generation<<','<<s.environment.preprocessMs<<','<<s.environment.uploadMs<<','<<g.outputDrawCalls<<','<<s.lights.total<<','<<s.lights.enabled<<','<<s.lights.influencing<<','<<s.lights.directional<<','<<s.lights.point<<','<<s.lights.spot<<','<<s.lights.optimized<<','<<s.lights.tiles<<','<<s.lights.indices<<','<<s.lights.averagePerTile<<','<<s.lights.maxPerTile<<','<<s.lights.overflow<<','<<s.lights.gatherMs<<','<<s.lights.cullingCpuMs<<','<<s.lights.cullingGpuMs<<','<<s.lights.uploadBytes<<','<<s.lights.gpuBytes<<'\n';
    }
    return out.good();
}
