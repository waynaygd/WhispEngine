#include "World.h"
#include "TransformMath.h"
#include "components/RigidbodyComponent.h"
#include <unordered_set>
#include <functional>
namespace ecs {
namespace {
ImportMatrix Matrix(const TransformComponent& t){float f[16];BuildRenderModelMatrix(f,RenderPose::From(t));ImportMatrix m;std::copy(f,f+16,m.begin());return m;}
ImportMatrix Inverse(const ImportMatrix& m){
    const double d=m[0]*(m[5]*m[10]-m[9]*m[6])-m[4]*(m[1]*m[10]-m[9]*m[2])+m[8]*(m[1]*m[6]-m[5]*m[2]);
    if(!std::isfinite(d)||std::abs(d)<1e-15)throw std::runtime_error("Hierarchy parent has singular scale");
    auto r=IdentityMatrix();r[0]=(m[5]*m[10]-m[9]*m[6])/d;r[4]=(m[8]*m[6]-m[4]*m[10])/d;r[8]=(m[4]*m[9]-m[8]*m[5])/d;
    r[1]=(m[9]*m[2]-m[1]*m[10])/d;r[5]=(m[0]*m[10]-m[8]*m[2])/d;r[9]=(m[8]*m[1]-m[0]*m[9])/d;
    r[2]=(m[1]*m[6]-m[5]*m[2])/d;r[6]=(m[4]*m[2]-m[0]*m[6])/d;r[10]=(m[0]*m[5]-m[4]*m[1])/d;
    for(int row=0;row<3;++row)r[12+row]=-(r[row]*m[12]+r[4+row]*m[13]+r[8+row]*m[14]);return r;
}
TransformComponent Decomposed(const ImportMatrix& matrix,Vec3 signs){auto t=DecomposeTransform(matrix,signs);if(!t)throw std::runtime_error("Hierarchy transform requires shear or singular/out-of-range scale; use uniform parent scale or compatible rotations");return *t;}
}
Entity World::Parent(Entity e)const{auto it=m_Parents.find(e);return IsAlive(e)&&it!=m_Parents.end()&&IsAlive(it->second)?it->second:Entity{};}
std::vector<Entity> World::Children(Entity e)const{std::vector<Entity> out;for(auto child:Entities())if(Parent(child)==e&&e.IsValid())out.push_back(child);return out;}
std::vector<Entity> World::Subtree(Entity e)const{
    std::vector<Entity> out;if(!IsAlive(e))return out;out.push_back(e);
    for(std::size_t i=0;i<out.size();++i){auto children=Children(out[i]);out.insert(out.end(),children.begin(),children.end());if(out.size()>GetAliveCount())throw std::runtime_error("Hierarchy cycle");}return out;
}
std::vector<Entity> World::HierarchyRoots(const std::vector<Entity>& entities)const{
    std::unordered_set<Entity,EntityHash> selected(entities.begin(),entities.end());std::vector<Entity> out;
    for(auto e:entities)if(IsAlive(e)){bool inherited=false;std::size_t depth=0;for(auto p=Parent(e);p.IsValid();p=Parent(p)){if(++depth>256)throw std::runtime_error("Hierarchy depth exceeds 256");if(selected.contains(p)){inherited=true;break;}}if(!inherited)out.push_back(e);}return out;
}
TransformComponent World::LocalTransform(Entity e)const{auto* t=GetComponent<TransformComponent>(e);if(!t)throw std::runtime_error("Entity has no Transform");return *t;}
TransformComponent World::WorldTransform(Entity e,const std::function<TransformComponent(Entity,const TransformComponent&)>& pose)const{
    auto localOf=[&](Entity entity){auto local=LocalTransform(entity);return pose?pose(entity,local):local;};
    const auto local=localOf(e);if(!Parent(e).IsValid())return local;
    auto result=Matrix(local);std::unordered_set<Entity,EntityHash> visited{e};
    for(auto p=Parent(e);p.IsValid();p=Parent(p)){if(!visited.insert(p).second||visited.size()>256)throw std::runtime_error("Hierarchy cycle/depth exceeds 256");result=MultiplyMatrix(Matrix(localOf(p)),result);}
    return Decomposed(result,local.scale);
}
bool World::SetLocalTransform(Entity e,const TransformComponent& t,std::string* error){return SetLocalTransforms({{e,t}},error);}
bool World::SetWorldTransform(Entity e,const TransformComponent& t,std::string* error){return SetWorldTransforms({{e,t}},error);}
bool World::SetLocalTransforms(const std::vector<std::pair<Entity,TransformComponent>>& values,std::string* error){return ApplyHierarchy({},values,false,error);}
bool World::SetWorldTransforms(const std::vector<std::pair<Entity,TransformComponent>>& values,std::string* error){return ApplyHierarchy({},values,true,error);}
bool World::SetParent(Entity e,Entity parent,bool keepWorld,std::string* error){return SetParents({{e,parent}},keepWorld,error);}
bool World::SetParents(const std::vector<std::pair<Entity,Entity>>& values,bool keepWorld,std::string* error){
    try{
        std::vector<std::pair<Entity,Entity>> changed;std::vector<std::pair<Entity,TransformComponent>> transforms;
        for(auto [e,p]:values){
            if(!IsAlive(e)||(p.IsValid()&&!IsAlive(p)))throw std::runtime_error("Parent belongs to a dead/stale/foreign World entity");
            // Script-only entities without Transform are valid roots. In particular,
            // restoring their history must not require a spatial component.
            if(Parent(e)==p)continue;
            changed.emplace_back(e,p);transforms.emplace_back(e,keepWorld?WorldTransform(e):LocalTransform(e));
        }
        return ApplyHierarchy(changed,transforms,keepWorld,error);
    }
    catch(const std::exception& ex){if(error)*error=ex.what();return false;}
}
bool World::ApplyHierarchy(const std::vector<std::pair<Entity,Entity>>& parents,const std::vector<std::pair<Entity,TransformComponent>>& transforms,bool worldSpace,std::string* error){
    try{
        AssertOwnerThread();auto proposed=m_Parents;
        for(auto [e,p]:parents){if(!IsAlive(e)||(p.IsValid()&&!IsAlive(p)))throw std::runtime_error("Parent belongs to a dead/stale/foreign World entity");if(p.IsValid())proposed[e]=p;else proposed.erase(e);}
        auto parent=[&](Entity e){auto it=proposed.find(e);return it==proposed.end()?Entity{}:it->second;};
        for(auto [e,p]:proposed){if(!GetComponent<TransformComponent>(e)||!GetComponent<TransformComponent>(p))throw std::runtime_error("Parent and Child require Transform");
            if(auto* rb=GetComponent<RigidbodyComponent>(e);rb&&!rb->isStatic&&rb->simulatePhysics)throw std::runtime_error("Dynamic Rigidbody must be a root entity. Use a static body or unparent first.");
            std::unordered_set<Entity,EntityHash> chain{e};for(auto a=p;a.IsValid();a=parent(a))if(!chain.insert(a).second||chain.size()>256)throw std::runtime_error("Parenting would create a cycle or exceed 256 levels");
        }
        std::unordered_map<Entity,TransformComponent,EntityHash> edits;
        for(const auto& [e,t]:transforms){LocalTransform(e);(void)Decomposed(Matrix(t),t.scale);edits[e]=t;}
        std::unordered_map<Entity,ImportMatrix,EntityHash> matrices;
        std::function<ImportMatrix(Entity)> resolve=[&](Entity e){
            if(auto it=matrices.find(e);it!=matrices.end())return it->second;
            auto it=edits.find(e);auto t=it==edits.end()?LocalTransform(e):it->second;auto m=Matrix(t);auto p=parent(e);
            if(p.IsValid()&&!(worldSpace&&it!=edits.end()))m=MultiplyMatrix(resolve(p),m);
            return matrices[e]=m;
        };
        // Validate the resulting descendants too, before publishing any local value.
        if(!proposed.empty())for(auto e:Entities())if(GetComponent<TransformComponent>(e))(void)Decomposed(resolve(e),LocalTransform(e).scale);
        if(worldSpace)for(auto& [e,t]:edits)if(auto p=parent(e);p.IsValid())t=Decomposed(MultiplyMatrix(Inverse(resolve(p)),Matrix(t)),LocalTransform(e).scale);
        for(const auto& [e,t]:edits)*GetComponent<TransformComponent>(e)=t;m_Parents=std::move(proposed);
        if(error)error->clear();return true;
    }catch(const std::exception& ex){if(error)*error=ex.what();return false;}
}
void World::ValidateHierarchy()const{for(const auto& [e,p]:m_Parents){if(!IsAlive(e)||!IsAlive(p))throw std::runtime_error("Dangling Parent");if(auto* rb=GetComponent<RigidbodyComponent>(e);rb&&!rb->isStatic&&rb->simulatePhysics)throw std::runtime_error("Dynamic Rigidbody must remain a root entity");(void)WorldTransform(e);}}
}
