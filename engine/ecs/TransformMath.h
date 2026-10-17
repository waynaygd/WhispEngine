#pragma once
#include "components/TransformComponent.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
namespace ecs {
struct RenderQuaternion { float x=0, y=0, z=0, w=1; };
inline RenderQuaternion NormalizeQuaternion(RenderQuaternion q) {
    const float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    if(n<1e-12f) return {};
    return {q.x/n,q.y/n,q.z/n,q.w/n};
}
// Matches the existing model matrix Rz * Ry * Rx, column vectors.
inline RenderQuaternion RenderRotation(Vec3 r) {
    const float x=std::sin(r.x*.5f), X=std::cos(r.x*.5f);
    const float y=std::sin(r.y*.5f), Y=std::cos(r.y*.5f);
    const float z=std::sin(r.z*.5f), Z=std::cos(r.z*.5f);
    return NormalizeQuaternion({x*Y*Z-X*y*z,X*y*Z+x*Y*z,X*Y*z-x*y*Z,X*Y*Z+x*y*z});
}
inline RenderQuaternion RenderSlerp(RenderQuaternion a,RenderQuaternion b,float t) {
    a=NormalizeQuaternion(a); b=NormalizeQuaternion(b);
    float d=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;
    if(d<0) { b={-b.x,-b.y,-b.z,-b.w}; d=-d; }
    d=std::clamp(d,-1.f,1.f);
    float p=1-t,q=t;
    if(d<.9995f) { const float angle=std::acos(d),s=std::sin(angle);p=std::sin((1-t)*angle)/s;q=std::sin(t*angle)/s; }
    return NormalizeQuaternion({p*a.x+q*b.x,p*a.y+q*b.y,p*a.z+q*b.z,p*a.w+q*b.w});
}
struct RenderPose {
    Vec3 position{},scale{1,1,1}; RenderQuaternion rotation{};
    static RenderPose From(const TransformComponent& t) { return {t.position,t.scale,RenderRotation(t.rotation)}; }
};
inline void BuildRenderModelMatrix(float* m,const RenderPose& p) {
    const auto q=p.rotation;
    m[0]=(1-2*(q.y*q.y+q.z*q.z))*p.scale.x; m[1]=2*(q.x*q.y+q.w*q.z)*p.scale.x; m[2]=2*(q.x*q.z-q.w*q.y)*p.scale.x; m[3]=0;
    m[4]=2*(q.x*q.y-q.w*q.z)*p.scale.y; m[5]=(1-2*(q.x*q.x+q.z*q.z))*p.scale.y; m[6]=2*(q.y*q.z+q.w*q.x)*p.scale.y; m[7]=0;
    m[8]=2*(q.x*q.z+q.w*q.y)*p.scale.z; m[9]=2*(q.y*q.z-q.w*q.x)*p.scale.z; m[10]=(1-2*(q.x*q.x+q.y*q.y))*p.scale.z; m[11]=0;
    m[12]=p.position.x;m[13]=p.position.y;m[14]=p.position.z;m[15]=1;
}
using Matrix4=std::array<double,16>;
inline Matrix4 IdentityTransformMatrix(){Matrix4 m{};m[0]=m[5]=m[10]=m[15]=1;return m;}
inline Matrix4 MultiplyTransformMatrices(const Matrix4& a,const Matrix4& b){
 Matrix4 c{};for(int column=0;column<4;++column)for(int row=0;row<4;++row)for(int k=0;k<4;++k)c[column*4+row]+=a[k*4+row]*b[column*4+k];return c;
}
// Decompose Rz*Ry*Rx TRS. Reject shear and singular transforms rather than losing geometry.
// Preferred signs preserve existing negative axes during gizmo manipulation.
inline std::optional<ecs::TransformComponent> DecomposeTRS(const Matrix4& m,ecs::Vec3 signs={1,1,1}){
 for(float sign:{signs.x,signs.y,signs.z})if(!std::isfinite(sign))return {};
 for(double x:m)if(!std::isfinite(x)||std::abs(x)>1e8)return {};
 if(std::abs(m[3])+std::abs(m[7])+std::abs(m[11])+std::abs(m[15]-1)>1e-7)return {};
 double lengths[3]{};double r[9]{};double preferred[3]={signs.x,signs.y,signs.z};
 for(int c=0;c<3;++c){for(int j=0;j<3;++j)lengths[c]+=m[c*4+j]*m[c*4+j];lengths[c]=std::sqrt(lengths[c]);if(lengths[c]<1e-6||lengths[c]>10000)return {};}
 double determinant=m[0]*(m[5]*m[10]-m[6]*m[9])-m[4]*(m[1]*m[10]-m[2]*m[9])+m[8]*(m[1]*m[6]-m[2]*m[5]);
 for(int i=0;i<3;++i)if(preferred[i]<0)lengths[i]=-lengths[i];
 if((lengths[0]*lengths[1]*lengths[2]<0)!=(determinant<0))lengths[0]=-lengths[0];
 for(int c=0;c<3;++c)for(int j=0;j<3;++j)r[c*3+j]=m[c*4+j]/lengths[c];
 for(int a=0;a<3;++a)for(int b=a+1;b<3;++b){double dot=0;for(int j=0;j<3;++j)dot+=r[a*3+j]*r[b*3+j];if(std::abs(dot)>1e-5)return {};}
 ecs::TransformComponent t;t.position={float(m[12]),float(m[13]),float(m[14])};t.scale={float(lengths[0]),float(lengths[1]),float(lengths[2])};
 double y=std::asin(std::clamp(-r[2],-1.,1.)),x,z;
 if(std::abs(std::cos(y))>1e-6){x=std::atan2(r[5],r[8]);z=std::atan2(r[1],r[0]);}else{x=std::atan2(-r[7],r[4]);z=0;}
 t.rotation={float(x),float(y),float(z)};
 float rebuilt[16];ecs::BuildRenderModelMatrix(rebuilt,ecs::RenderPose::From(t));
 for(int i=0;i<16;++i)if(std::abs(rebuilt[i]-m[i])>2e-4*std::max(1.,std::abs(m[i])))return {};
 return t;
}
inline bool Finite(Vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
inline bool Finite(const TransformComponent& t){return Finite(t.position)&&Finite(t.rotation)&&Finite(t.scale);}
inline bool Finite(const Matrix4& m){return std::all_of(m.begin(),m.end(),[](double v){return std::isfinite(v);});}
// Exact legacy compose path; checked callers validate before composing.
inline Matrix4 ComposeTRS(const TransformComponent& t){
    float values[16];BuildRenderModelMatrix(values,RenderPose::From(t));
    Matrix4 result;std::copy(values,values+16,result.begin());return result;
}
inline std::optional<Matrix4> TryComposeTRS(const TransformComponent& t){
    if(!Finite(t))return {};auto result=ComposeTRS(t);return Finite(result)?std::optional(result):std::nullopt;
}
// Inverts affine matrices, including shear. Invertible does not imply TRS.
// Reject a near-zero pivot (world units) and ill-conditioned relative pivots.
inline std::optional<Matrix4> InverseAffine(const Matrix4& m){
    if(!Finite(m)||std::abs(m[3])+std::abs(m[7])+std::abs(m[11])+std::abs(m[15]-1)>1e-7)return {};
    double rows[3][6]{};double norm=0;
    for(int r=0;r<3;++r){for(int c=0;c<3;++c){rows[r][c]=m[c*4+r];norm=std::max(norm,std::abs(rows[r][c]));}rows[r][r+3]=1;}
    for(int c=0;c<3;++c){
        int pivot=c;for(int r=c+1;r<3;++r)if(std::abs(rows[r][c])>std::abs(rows[pivot][c]))pivot=r;
        if(std::abs(rows[pivot][c])<std::max(1e-6,norm*1e-12))return {};
        if(pivot!=c)for(int k=0;k<6;++k)std::swap(rows[pivot][k],rows[c][k]);
        double scale=rows[c][c];for(int k=0;k<6;++k)rows[c][k]/=scale;
        for(int r=0;r<3;++r)if(r!=c){double factor=rows[r][c];for(int k=0;k<6;++k)rows[r][k]-=factor*rows[c][k];}
    }
    auto result=IdentityTransformMatrix();
    for(int r=0;r<3;++r){for(int c=0;c<3;++c)result[c*4+r]=rows[r][c+3];result[12+r]=-(rows[r][3]*m[12]+rows[r][4]*m[13]+rows[r][5]*m[14]);}
    if(!Finite(result))return {};
    for(const auto& product:{MultiplyTransformMatrices(m,result),MultiplyTransformMatrices(result,m)})
        for(int i=0;i<16;++i)if(std::abs(product[i]-(i%5==0?1.:0.))>1e-7)return {};
    return result;
}

}
