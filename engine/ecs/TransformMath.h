#pragma once
#include "RenderPose.h"
#include <array>
#include <optional>
namespace ecs {
using ImportMatrix=std::array<double,16>;
inline ImportMatrix IdentityMatrix(){ImportMatrix m{};m[0]=m[5]=m[10]=m[15]=1;return m;}
inline ImportMatrix MultiplyMatrix(const ImportMatrix& a,const ImportMatrix& b){
 ImportMatrix c{};for(int column=0;column<4;++column)for(int row=0;row<4;++row)for(int k=0;k<4;++k)c[column*4+row]+=a[k*4+row]*b[column*4+k];return c;
}
// Decompose Rz*Ry*Rx TRS. Reject shear and singular transforms rather than losing geometry.
// Preferred signs preserve existing negative axes during gizmo manipulation.
inline std::optional<TransformComponent> DecomposeTransform(const ImportMatrix& m,Vec3 signs={1,1,1}){
 for(double x:m)if(!std::isfinite(x)||std::abs(x)>1e8)return {};
 if(std::abs(m[3])+std::abs(m[7])+std::abs(m[11])+std::abs(m[15]-1)>1e-7)return {};
 double lengths[3]{};double r[9]{};double preferred[3]={signs.x,signs.y,signs.z};
 for(int c=0;c<3;++c){for(int j=0;j<3;++j)lengths[c]+=m[c*4+j]*m[c*4+j];lengths[c]=std::sqrt(lengths[c]);if(lengths[c]<1e-6||lengths[c]>10000)return {};}
 double determinant=m[0]*(m[5]*m[10]-m[6]*m[9])-m[4]*(m[1]*m[10]-m[2]*m[9])+m[8]*(m[1]*m[6]-m[2]*m[5]);
 for(int i=0;i<3;++i)if(preferred[i]<0)lengths[i]=-lengths[i];
 if((lengths[0]*lengths[1]*lengths[2]<0)!=(determinant<0))lengths[0]=-lengths[0];
 for(int c=0;c<3;++c)for(int j=0;j<3;++j)r[c*3+j]=m[c*4+j]/lengths[c];
 for(int a=0;a<3;++a)for(int b=a+1;b<3;++b){double dot=0;for(int j=0;j<3;++j)dot+=r[a*3+j]*r[b*3+j];if(std::abs(dot)>1e-5)return {};}
 TransformComponent t;t.position={float(m[12]),float(m[13]),float(m[14])};t.scale={float(lengths[0]),float(lengths[1]),float(lengths[2])};
 double y=std::asin(std::clamp(-r[2],-1.,1.)),x,z;
 if(std::abs(std::cos(y))>1e-6){x=std::atan2(r[5],r[8]);z=std::atan2(r[1],r[0]);}else{x=std::atan2(-r[7],r[4]);z=0;}
 t.rotation={float(x),float(y),float(z)};
 float rebuilt[16];BuildRenderModelMatrix(rebuilt,RenderPose::From(t));
 for(int i=0;i<16;++i)if(std::abs(rebuilt[i]-m[i])>2e-4*std::max(1.,std::abs(m[i])))return {};
 return t;
}
}
