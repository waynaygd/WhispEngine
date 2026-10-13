#pragma once
#include "MeshData.h"
#include <array>
#include <cmath>
#include <algorithm>

namespace TangentSpace {
using V=std::array<double,3>;
inline V Sub(const float* a,const float* b){return {double(a[0])-b[0],double(a[1])-b[1],double(a[2])-b[2]};}
inline double Dot(V a,V b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
inline V Cross(V a,V b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
inline V Unit(V a){double l=std::sqrt(Dot(a,a));if(!std::isfinite(l)||l<1e-20)return {};for(auto& x:a)x/=l;return a;}
// UV-gradient tangents, weighted by corner angle. Opposite handedness corners
// receive separate vertices; no averaging across mirrored UV islands.
// Authored tangents remain authoritative. Degenerate corners stay unavailable.
inline void Generate(MeshData& mesh,std::size_t firstIndex=0,std::size_t indexCount=0) {
    if(!indexCount)indexCount=mesh.indices.size()-firstIndex;
    const auto originalCount=mesh.vertices.size();
    std::vector<int> orientation(originalCount,0);
    std::vector<std::array<std::uint32_t,2>> splits(originalCount,{UINT32_MAX,UINT32_MAX});
    for(std::size_t f=firstIndex;f+2<firstIndex+indexCount;f+=3) {
        const auto a=mesh.indices[f],b=mesh.indices[f+1],c=mesh.indices[f+2];
        if(a>=originalCount||b>=originalCount||c>=originalCount)continue;
        const auto& v0=mesh.vertices[a];const auto& v1=mesh.vertices[b];const auto& v2=mesh.vertices[c];
        double d=(double(v1.uv[0])-v0.uv[0])*(double(v2.uv[1])-v0.uv[1])-(double(v2.uv[0])-v0.uv[0])*(double(v1.uv[1])-v0.uv[1]);
        if(!std::isfinite(d)||std::abs(d)<1e-15)continue;
        int sign=d<0?-1:1;int side=sign<0?0:1;
        for(int k=0;k<3;++k) {
            auto& i=mesh.indices[f+k];if(mesh.vertices[i].tangent[3]!=0)continue;
            if(orientation[i]==0)orientation[i]=sign;
            if(orientation[i]==sign)continue;
            auto& split=splits[i][side];
            if(split==UINT32_MAX){split=static_cast<std::uint32_t>(mesh.vertices.size());auto copy=mesh.vertices[i];mesh.vertices.push_back(copy);}
            i=split;
        }
    }
    std::vector<V> ts(mesh.vertices.size()),bs(mesh.vertices.size());
    for(std::size_t f=firstIndex;f+2<firstIndex+indexCount;f+=3) {
        std::uint32_t ids[3]={mesh.indices[f],mesh.indices[f+1],mesh.indices[f+2]};
        if(*std::max_element(ids,ids+3)>=mesh.vertices.size())continue;
        const auto& a=mesh.vertices[ids[0]];const auto& b=mesh.vertices[ids[1]];const auto& c=mesh.vertices[ids[2]];
        auto e1=Sub(b.position,a.position),e2=Sub(c.position,a.position);
        double u1=double(b.uv[0])-a.uv[0],v1=double(b.uv[1])-a.uv[1],u2=double(c.uv[0])-a.uv[0],v2=double(c.uv[1])-a.uv[1],d=u1*v2-u2*v1;
        if(!std::isfinite(d)||std::abs(d)<1e-15||Dot(Cross(e1,e2),Cross(e1,e2))<1e-30)continue;
        V t{},bt{};for(int j=0;j<3;++j){t[j]=(e1[j]*v2-e2[j]*v1)/d;bt[j]=(e2[j]*u1-e1[j]*u2)/d;}t=Unit(t);bt=Unit(bt);
        for(int k=0;k<3;++k) {
            const auto& v=mesh.vertices[ids[k]];if(v.tangent[3]!=0)continue;
            auto x=Unit(Sub(mesh.vertices[ids[(k+1)%3]].position,v.position)),y=Unit(Sub(mesh.vertices[ids[(k+2)%3]].position,v.position));
            double angle=std::acos(std::clamp(Dot(x,y),-1.,1.));
            for(int j=0;j<3;++j){ts[ids[k]][j]+=t[j]*angle;bs[ids[k]][j]+=bt[j]*angle;}
        }
    }
    for(std::size_t i=0;i<mesh.vertices.size();++i) {
        auto& v=mesh.vertices[i];if(v.tangent[3]!=0)continue;
        V n=Unit({v.normal[0],v.normal[1],v.normal[2]}),t=ts[i];double projection=Dot(t,n);
        for(int j=0;j<3;++j)t[j]-=n[j]*projection;t=Unit(t);
        if(Dot(n,n)<.5||Dot(t,t)<.5||Dot(bs[i],bs[i])<1e-20)continue;
        for(int j=0;j<3;++j)v.tangent[j]=static_cast<float>(t[j]);
        v.tangent[3]=Dot(Cross(n,t),bs[i])<0?-1.f:1.f;
    }
}
}
