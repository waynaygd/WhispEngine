#include "Hierarchy.h"
#include "components/SceneIdentityComponent.h"
#include <algorithm>
#include <numeric>
#include <unordered_set>
namespace ecs {
namespace {
bool Fail(std::string* error,const std::string& message){if(error)*error="Hierarchy: "+message;return false;}
bool Editable(World& w,std::string* error){w.AssertOwnerThread();return w.IsHierarchyData()&&!w.IsIterating()?true:Fail(error,"mutation requires a data-only World at a phase boundary");}
bool Ancestor(const HierarchyGraph& g,std::size_t a,std::size_t b){for(auto p=g.nodes[b].parent;p;p=g.nodes[*p].parent)if(*p==a)return true;return false;}
}
std::optional<HierarchyGraph> HierarchyGraph::Build(const World& w,std::string* error,Entity overrideEntity,const TransformComponent* overrideLocal,const RelationshipComponent* overrideRelationship){
    w.AssertOwnerThread();
    if(!w.IsHierarchyData()){Fail(error,"graph access requires a data-only World");return {};}
    HierarchyGraph g;g.world=w.GetIdentity();const auto alive=w.Entities();
    // Iterative traversal is bounded by total live nodes, not a recursive depth.
    if(alive.size()>1000000){Fail(error,"graph node budget exceeded");return {};}
    std::unordered_map<std::string,std::size_t> identities;
    for(auto e:alive){
        const auto* id=w.GetComponent<SceneIdentityComponent>(e);
        if(!id||!ValidSceneIdentity(id->value)||!identities.emplace(id->value,g.nodes.size()).second){Fail(error,"missing, invalid or duplicate SceneIdentity");return {};}
        Node n;n.entity=e;n.id=id->value;
        if(auto* t=w.GetComponent<TransformComponent>(e))n.local=(e==overrideEntity&&overrideLocal)?*overrideLocal:*t;
        if(auto* r=(e==overrideEntity&&overrideRelationship)?overrideRelationship:w.GetComponent<RelationshipComponent>(e)){n.relationship=true;n.order=r->order;if(r->order>MaximumSiblingOrder){Fail(error,"invalid sibling order");return {};}}
        g.entities.emplace(e,g.nodes.size());g.nodes.push_back(std::move(n));
    }
    for(std::size_t i=0;i<g.nodes.size();++i){auto& n=g.nodes[i];
        if(auto* relation=(n.entity==overrideEntity&&overrideRelationship)?overrideRelationship:w.GetComponent<RelationshipComponent>(n.entity);relation&&relation->parent){
            auto parent=identities.find(*relation->parent);
            if(!ValidSceneIdentity(*relation->parent)||parent==identities.end()||parent->second==i){Fail(error,"missing/invalid/self parent");return {};}
            n.parent=parent->second;g.nodes[*n.parent].children.push_back(i);
            if(!n.local||!g.nodes[*n.parent].local){Fail(error,"parent and child require Transform");return {};}
        }else g.roots.push_back(i);
    }
    auto sort=[&](auto& list){std::sort(list.begin(),list.end(),[&](auto a,auto b){const auto& x=g.nodes[a];const auto& y=g.nodes[b];return x.order!=y.order?x.order<y.order:x.id<y.id;});};
    sort(g.roots);for(auto& n:g.nodes)sort(n.children);
    std::vector<std::size_t> pending(g.roots.rbegin(),g.roots.rend());
    while(!pending.empty()){
        const auto i=pending.back();pending.pop_back();auto& n=g.nodes[i];g.preorder.push_back(i);
        if(n.local){
            auto local=TryComposeTRS(*n.local);if(!local){Fail(error,"nonfinite local Transform");return {};}
            n.matrix=n.parent?MultiplyTransformMatrices(*g.nodes[*n.parent].matrix,*local):*local;
            if(!Finite(*n.matrix)){Fail(error,"nonfinite world composition");return {};}
            if((n.relationship||!n.children.empty())&&!InverseAffine(*n.matrix)){Fail(error,"singular/near-singular hierarchy Transform");return {};}
        }else if(n.relationship){Fail(error,"relationship requires Transform");return {};}
        pending.insert(pending.end(),n.children.rbegin(),n.children.rend());
    }
    if(g.preorder.size()!=g.nodes.size()){Fail(error,"cycle in parent graph");return {};}
    return g;
}
bool ValidateHierarchy(const World& w,std::string* error){return bool(HierarchyGraph::Build(w,error));}
std::optional<std::vector<Entity>> HierarchyGraph::Closure(const std::vector<Entity>& selected,bool postorder,std::string* error)const{
    std::vector<bool> included(nodes.size());
    for(auto e:selected){auto it=entities.find(e);if(it==entities.end()){Fail(error,"closure contains stale/foreign Entity");return {};}included[it->second]=true;}
    std::vector<Entity> result;
    for(auto i:preorder){included[i]=included[i]||(nodes[i].parent&&included[*nodes[i].parent]);if(included[i])result.push_back(nodes[i].entity);}
    if(postorder)std::reverse(result.begin(),result.end());return result;
}
Entity GetParent(const World& w,Entity e,std::string* error){auto g=HierarchyGraph::Build(w,error);if(!g)return {};auto i=g->entities.find(e);if(i==g->entities.end()){Fail(error,"invalid Entity");return {};}auto p=g->nodes[i->second].parent;return p?g->nodes[*p].entity:Entity{};}
std::optional<std::vector<Entity>> GetChildren(const World& w,Entity e,std::string* error){auto g=HierarchyGraph::Build(w,error);if(!g)return {};auto i=g->entities.find(e);if(i==g->entities.end()){Fail(error,"invalid Entity");return {};}std::vector<Entity> result;for(auto c:g->nodes[i->second].children)result.push_back(g->nodes[c].entity);return result;}
std::optional<bool> IsAncestor(const World& w,Entity a,Entity b,std::string* error){auto g=HierarchyGraph::Build(w,error);if(!g)return {};if(!g->entities.contains(a)||!g->entities.contains(b)){Fail(error,"invalid Entity");return {};}return Ancestor(*g,g->entities.at(a),g->entities.at(b));}
std::optional<Matrix4> EvaluateHierarchyMatrix(const World& w,Entity e,std::string* error){auto g=HierarchyGraph::Build(w,error);if(!g)return {};auto it=g->entities.find(e);if(it==g->entities.end()){Fail(error,"invalid Entity");return {};}return g->nodes[it->second].matrix;}
std::optional<TransformComponent> ReadHierarchyTransform(const World& w,Entity e){auto g=HierarchyGraph::Build(w);if(!g||!g->entities.contains(e))return {};const auto& n=g->nodes[g->entities.at(e)];if(!n.local||!n.matrix)return {};return n.parent?DecomposeTRS(*n.matrix,n.local->scale):n.local;}
namespace {
bool Reparent(World& w,Entity child,Entity parent,std::string* error){
    if(w.IsRetiringHierarchy())return Fail(error,"reparent during lifecycle retirement is not allowed");
    if(!Editable(w,error))return false;auto g=HierarchyGraph::Build(w,error);if(!g)return false;
    auto c=g->entities.find(child);if(c==g->entities.end()||!g->nodes[c->second].matrix)return Fail(error,"invalid child/Transform");
    std::optional<std::size_t> p;
    if(parent.IsValid()){auto found=g->entities.find(parent);if(found==g->entities.end()||!g->nodes[found->second].matrix)return Fail(error,"invalid/foreign parent or absent Transform");p=found->second;if(*p==c->second||Ancestor(*g,c->second,*p))return Fail(error,"self parent/cycle");}
    auto& n=g->nodes[c->second];if(n.parent==p)return true;
    Matrix4 local=*n.matrix;
    if(p){auto inverse=InverseAffine(*g->nodes[*p].matrix);if(!inverse)return Fail(error,"parent world matrix is not invertible");local=MultiplyTransformMatrices(*inverse,local);}
    auto next=DecomposeTRS(local,n.local->scale);if(!next)return Fail(error,"Keep World requires non-TRS/singular local Transform");
    std::uint64_t order=0;const auto& siblings=p?g->nodes[*p].children:g->roots;
    for(auto s:siblings)if(s!=c->second){if(g->nodes[s].order==MaximumSiblingOrder)return Fail(error,"sibling order exhausted");order=std::max(order,g->nodes[s].order+1);}
    RelationshipComponent relationship;relationship.order=order;if(p)relationship.parent=g->nodes[*p].id;
    if(!HierarchyGraph::Build(w,error,child,&*next,&relationship))return false;
    // All allocating/checked preparation precedes the two authoritative writes.
    if(auto* current=w.GetComponent<RelationshipComponent>(child))std::swap(*current,relationship);else w.AddComponent<RelationshipComponent>(child,std::move(relationship));
    *w.GetComponent<TransformComponent>(child)=*next;return true;
}
}
bool SetParent(World& w,Entity child,Entity parent,std::string* error){if(!parent.IsValid())return Fail(error,"SetParent needs a valid parent; use ClearParent");return Reparent(w,child,parent,error);}
bool ClearParent(World& w,Entity child,std::string* error){return Reparent(w,child,{},error);}
bool WriteHierarchyMatrix(World& w,Entity e,const Matrix4& matrix,std::string* error){
    if(!Editable(w,error))return false;auto g=HierarchyGraph::Build(w,error);if(!g||!g->entities.contains(e))return Fail(error,"invalid Entity/graph");const auto& n=g->nodes[g->entities.at(e)];if(!n.local||!n.matrix)return Fail(error,"absent Transform");
    auto local=matrix;if(n.parent){auto inverse=InverseAffine(*g->nodes[*n.parent].matrix);if(!inverse)return Fail(error,"singular parent");local=MultiplyTransformMatrices(*inverse,matrix);}
    auto next=DecomposeTRS(local,n.local->scale);if(!next)return Fail(error,"world write requires non-TRS local Transform");
    if(!HierarchyGraph::Build(w,error,e,&*next))return false;*w.GetComponent<TransformComponent>(e)=*next;return true;
}
bool WriteHierarchyTransform(World& w,Entity e,const TransformComponent& value,std::string* error){
    if(!Editable(w,error)||!Finite(value))return Fail(error,"invalid world Transform");auto g=HierarchyGraph::Build(w,error);if(!g||!g->entities.contains(e))return false;
    if(!g->nodes[g->entities.at(e)].parent){if(!w.HasComponent<TransformComponent>(e)||!HierarchyGraph::Build(w,error,e,&value))return false;*w.GetComponent<TransformComponent>(e)=value;return true;}
    return WriteHierarchyMatrix(w,e,ComposeTRS(value),error);
}
std::optional<HierarchyClonePlan> PrepareHierarchyClone(const World& w,const std::vector<Entity>& selected,std::string* error){auto g=HierarchyGraph::Build(w,error);if(!g)return {};auto closure=g->Closure(selected,false,error);if(!closure)return {};HierarchyClonePlan plan;plan.sources=std::move(*closure);std::unordered_set<std::string> used;for(const auto& node:g->nodes)used.insert(node.id);for(auto e:plan.sources){auto id=NewSceneIdentity();while(!used.insert(id).second)id=NewSceneIdentity();plan.identities.emplace(g->nodes[g->entities.at(e)].id,std::move(id));}return plan;}
RelationshipComponent RemapRelationship(RelationshipComponent value,const std::unordered_map<std::string,std::string>& identities){if(value.parent){auto found=identities.find(*value.parent);if(found!=identities.end())value.parent=found->second;}return value;}
}
