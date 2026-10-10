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
};
inline bool SaveRenderDiagnostics(const std::string& path,const std::vector<RenderDiagnosticSample>& samples) {
    std::ofstream out(path);
    if(!out)return false;
    out<<"frame,instancing,frameMs,objects,drawCalls,instancedDrawCalls,instances,capacity,uploadBytes,bufferGrowths,gatherMs,prepareMs,batchMs,submitMs\n"<<std::fixed<<std::setprecision(6);
    for(const auto& s:samples) {
        const auto& r=s.render;const auto& g=r.submitted;
        out<<s.frame<<','<<s.instancing<<','<<s.frameMs<<','<<r.visibleObjects<<','<<g.drawCalls<<','<<g.instancedDrawCalls<<','<<g.renderedInstances<<','<<g.instanceBufferCapacity<<','<<g.uploadBytes<<','<<g.instanceBufferGrowths<<','<<r.gatherMs<<','<<r.prepareMs<<','<<r.batchMs<<','<<r.submitMs<<'\n';
    }
    return out.good();
}
