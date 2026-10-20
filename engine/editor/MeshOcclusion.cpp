#include "MeshOcclusion.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace editor {
namespace {
using V=ecs::Vec3;
V Sub(V a,V b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
V Cross(V a,V b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
float Dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
V Vertex(const MeshData& mesh,unsigned i){auto& v=mesh.vertices[mesh.indices[i]];return {v.position[0],v.position[1],v.position[2]};}
V Minimum(V a,V b){return {std::min(a.x,b.x),std::min(a.y,b.y),std::min(a.z,b.z)};}
V Maximum(V a,V b){return {std::max(a.x,b.x),std::max(a.y,b.y),std::max(a.z,b.z)};}
float Axis(V v,int axis){return axis==0?v.x:axis==1?v.y:v.z;}
bool BoxHit(V lo,V hi,V origin,V direction,float maximum){
 float near=0,far=maximum;
 for(int k=0;k<3;++k){float p=Axis(origin,k),d=Axis(direction,k),minimum=Axis(lo,k),maximum=Axis(hi,k);
  if(std::abs(d)<1e-10f){if(p<minimum||p>maximum)return false;}
  else {float a=(minimum-p)/d,b=(maximum-p)/d;if(a>b)std::swap(a,b);near=std::max(near,a);far=std::min(far,b);}
 }
 return far>=near;
}
bool TriangleHit(const MeshData& mesh,unsigned i,V p,V d,float maximum){
 auto a=Vertex(mesh,i),e1=Sub(Vertex(mesh,i+1),a),e2=Sub(Vertex(mesh,i+2),a),h=Cross(d,e2);float determinant=Dot(e1,h);
 if(std::abs(determinant)<1e-10f)return false;float inv=1/determinant;auto delta=Sub(p,a);float u=inv*Dot(delta,h);if(u<0||u>1)return false;
 auto q=Cross(delta,e1);float v=inv*Dot(d,q);if(v<0||u+v>1)return false;float t=inv*Dot(e2,q);return t>=0&&t<maximum;
}
}
MeshOcclusion::MeshOcclusion(const MeshData& mesh){
 struct Triangle {V lo,hi;unsigned offset;};std::vector<Triangle> work;work.reserve(mesh.indices.size()/3);
 for(unsigned i=0;i+2<mesh.indices.size();i+=3){if(mesh.indices[i]>=mesh.vertices.size()||mesh.indices[i+1]>=mesh.vertices.size()||mesh.indices[i+2]>=mesh.vertices.size())continue;
  auto a=Vertex(mesh,i),b=Vertex(mesh,i+1),c=Vertex(mesh,i+2);work.push_back({Minimum(a,Minimum(b,c)),Maximum(a,Maximum(b,c)),i});
 }
 if(work.empty())return;nodes.reserve(work.size()/2);triangles.resize(work.size());
 auto build=[&](auto&& self,unsigned first,unsigned count)->unsigned{
  V lo{INFINITY,INFINITY,INFINITY},hi{-INFINITY,-INFINITY,-INFINITY};for(unsigned i=first;i<first+count;++i){lo=Minimum(lo,work[i].lo);hi=Maximum(hi,work[i].hi);}
  unsigned index=unsigned(nodes.size());nodes.push_back({lo,hi,first,count});
  if(count<=8){for(unsigned i=first;i<first+count;++i)triangles[i]=work[i].offset;return index;}
  auto extent=Sub(hi,lo);int axis=extent.y>extent.x?1:0;if(extent.z>Axis(extent,axis))axis=2;unsigned half=count/2;
  std::nth_element(work.begin()+first,work.begin()+first+half,work.begin()+first+count,[&](auto& a,auto& b){return Axis(a.lo,axis)+Axis(a.hi,axis)<Axis(b.lo,axis)+Axis(b.hi,axis);});
  unsigned left=self(self,first,half),right=self(self,first+half,count-half);nodes[index].count=0;nodes[index].left=left;nodes[index].right=right;return index;
 };build(build,0,unsigned(work.size()));
}
bool MeshOcclusion::AnyHit(const MeshData& mesh,V origin,V direction,float maximum,std::size_t* tests)const{
 if(nodes.empty()||maximum<=0)return false;std::array<unsigned,64> stack{};unsigned top=1;
 // Median splits guarantee a logarithmic depth, bounded by the unsigned triangle count.
 while(top){auto& node=nodes[stack[--top]];if(!BoxHit(node.lo,node.hi,origin,direction,maximum))continue;
  if(node.count){for(unsigned i=node.first;i<node.first+node.count;++i){if(tests)++*tests;if(TriangleHit(mesh,triangles[i],origin,direction,maximum))return true;}}
  else {stack[top++]=node.right;stack[top++]=node.left;}
 }return false;
}
}
