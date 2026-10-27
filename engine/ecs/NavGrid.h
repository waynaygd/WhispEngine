#pragma once
#include "World.h"
#include "components/ColliderComponent.h"
#include "components/RigidbodyComponent.h"
#include "components/TransformComponent.h"
#include <queue>
#include <algorithm>
#include <cmath>
namespace ecs {
// Flat static-world navigation. Ground is excluded by foot height; triggers and
// dynamic actors never become baked obstacles. Four neighbours prevent corner cutting.
inline std::vector<Vec3> FindGridPath(const World& world,Vec3 start,Vec3 goal,Vec3 minimum,Vec3 maximum,float cell,float radius,Entity ignore={}){
 for(float n:{start.x,start.y,start.z,goal.x,goal.y,goal.z,minimum.x,minimum.z,maximum.x,maximum.z,cell,radius})if(!std::isfinite(n))throw std::runtime_error("Navigation: nonfinite argument");
 if(cell<.1f||cell>10||radius<0||radius>5||maximum.x<=minimum.x||maximum.z<=minimum.z)throw std::runtime_error("Navigation: invalid bounds/cell/radius");
 const double dw=std::ceil((maximum.x-minimum.x)/cell),dh=std::ceil((maximum.z-minimum.z)/cell);
 if(dw*dh>16384||dw<1||dh<1)throw std::runtime_error("Navigation: grid exceeds 16384 cells");int width=int(dw),height=int(dh),count=width*height;
 auto index=[&](Vec3 p){if(p.x<minimum.x||p.z<minimum.z||p.x>=maximum.x||p.z>=maximum.z)return -1;return int((p.z-minimum.z)/cell)*width+int((p.x-minimum.x)/cell);};
 auto point=[&](int i){return Vec3{minimum.x+(i%width+.5f)*cell,start.y,minimum.z+(i/width+.5f)*cell};};
 int from=index(start),to=index(goal);if(from<0||to<0)return {};
 std::vector<bool> blocked(count,false);
 for(auto e:world.Entities()){
  if(e==ignore)continue;auto* c=world.GetComponent<ColliderComponent>(e);auto* b=world.GetComponent<RigidbodyComponent>(e);if(!c||c->isTrigger||(b&&!b->isStatic)||!world.HasComponent<TransformComponent>(e))continue;
  auto p=world.WorldTransform(e).position;p.x+=c->offset.x;p.y+=c->offset.y;p.z+=c->offset.z;
  if(p.y+c->halfExtents.y<=start.y+.08f||p.y-c->halfExtents.y>start.y+1.8f)continue;
  // Physics boxes use world-aligned extents; include half-cell clearance so
  // the whole route segment, not just its centre, clears an obstacle.
  float padding=radius+cell*.5f;
  for(int i=0;i<count;++i){auto q=point(i);if(std::abs(q.x-p.x)<c->halfExtents.x+padding&&std::abs(q.z-p.z)<c->halfExtents.z+padding)blocked[i]=true;}
 }
 if(blocked[to])return {};blocked[from]=false;
 std::vector<int> cost(count,1000000),parent(count,-1);using Entry=std::pair<int,int>;std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> open;
 auto h=[&](int i){return std::abs(i%width-to%width)+std::abs(i/width-to/width);};cost[from]=0;open.push({h(from),from});
 while(!open.empty()){
  auto [score,i]=open.top();open.pop();if(score!=cost[i]+h(i))continue;if(i==to)break;
  for(auto [dx,dz]:{std::pair{1,0},{-1,0},{0,1},{0,-1}}){int x=i%width+dx,z=i/width+dz;if(x<0||x>=width||z<0||z>=height)continue;int next=z*width+x;if(blocked[next]||cost[next]<=cost[i]+1)continue;cost[next]=cost[i]+1;parent[next]=i;open.push({cost[next]+h(next),next});}
 }
 if(cost[to]==1000000)return {};std::vector<Vec3> path;for(int i=to;i!=from;i=parent[i])path.push_back(point(i));std::reverse(path.begin(),path.end());if(path.empty())path.push_back(goal);else path.back()=goal;return path;
}
struct ScreenRay{Vec3 origin,direction;};
}
