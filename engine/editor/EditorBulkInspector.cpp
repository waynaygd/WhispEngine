#include "EditorLayer.h"
#include "SunSynchronization.h"
#include "../core/Application.h"
#include "../ecs/components/SceneIdentityComponent.h"
#include <set>
namespace editor {
namespace {
using Json=nlohmann::json;using PV=ecs::PropertyValue;
bool Near(float a,float b){return std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=1e-6f+1e-5f*std::max(std::abs(a),std::abs(b));}
std::array<bool,4> Difference(const PV& a,const PV& b,bool exact=false){
    auto same=[&](float x,float y){return exact?x==y:Near(x,y);};
    std::array<bool,4> out{};if(a.index()!=b.index()){out.fill(true);return out;}
    if(auto v=std::get_if<ecs::Vec3>(&a)){auto q=std::get<ecs::Vec3>(b);return {!same(v->x,q.x),!same(v->y,q.y),!same(v->z,q.z),false};}
    if(auto v=std::get_if<std::array<float,4>>(&a)){auto q=std::get<std::array<float,4>>(b);for(int i=0;i<4;++i)out[i]=!same((*v)[i],q[i]);return out;}
    if(auto v=std::get_if<float>(&a))out[0]=!same(*v,std::get<float>(b));
    else if(auto v=std::get_if<int>(&a))out[0]=*v!=std::get<int>(b);
    else if(auto v=std::get_if<bool>(&a))out[0]=*v!=std::get<bool>(b);
    else if(auto v=std::get_if<std::string>(&a))out[0]=*v!=std::get<std::string>(b);
    else if(auto v=std::get_if<std::vector<std::string>>(&a))out[0]=*v!=std::get<std::vector<std::string>>(b);
    return out;
}
bool Different(const PV& a,const PV& b){auto values=Difference(a,b,true);return std::any_of(values.begin(),values.end(),[](bool v){return v;});}
const ecs::PropertyDescriptor* Property(const ecs::ComponentDescriptor* d,const std::string& key){if(d)for(const auto& p:d->properties)if(p.key==key)return &p;return nullptr;}
PV TransformValue(const ecs::TransformComponent& t,const std::string& key){if(key=="position")return t.position;if(key=="rotation")return t.rotation;if(key=="scale")return t.scale;throw std::runtime_error("Unknown Transform field");}
PV Masked(const PV& old,const PV& requested,unsigned axes,bool relative,const std::string& key){
    if(auto v=std::get_if<ecs::Vec3>(&requested)){
        auto result=std::get<ecs::Vec3>(old);float* a[]={&result.x,&result.y,&result.z};const float b[]={v->x,v->y,v->z};
        for(int i=0;i<3;++i)if(axes&(1u<<i))*a[i]=relative?(key=="scale"?*a[i]*b[i]:*a[i]+b[i]):b[i];return result;
    }
    if(auto v=std::get_if<std::array<float,4>>(&requested)){
        auto result=std::get<std::array<float,4>>(old);for(int i=0;i<4;++i)if(axes&(1u<<i))result[i]=(*v)[i];return result;
    }
    return requested;
}
void Notify(Application& app,const std::vector<ecs::Entity>& entities){
    // A property edit must not wake/reset unrelated Rigidbody fields: they are
    // outside this component-only history record. Authoring physics is paused.
    for(auto e:entities)app.NotifyTeleport(e);if(auto* physics=app.GetPhysicsSystem())physics->ResetState();
}
void CheckDriven(Application& app,ecs::Entity e,const std::string& component,const std::string& property){
    const auto& b=app.GetSceneBackground();if(PrimarySun(app.GetWorld())!=e)return;
    if(component=="Transform"&&property=="rotation"&&b.sunSync==SunSyncMode::SkyDrivesLight&&b.sunMetadata)throw std::runtime_error("Sky Drives Light controls one of the selected rotations; choose Independent first");
    if(component=="DirectionalLight"&&(property=="color"||property=="intensity")&&b.background==EditorBackground::DayNight&&b.driveLightProperties)throw std::runtime_error("Day/Night controls a selected light; disable its light-property control first");
}
}
std::vector<ecs::ComponentTypeId> EditorLayer::CommonInspectorComponents(Application& app)const{
    std::vector<ecs::ComponentTypeId> result;if(m_Selection.items.empty())return result;
    for(const auto& d:ecs::ComponentRegistry::Builtins().Components())if(d.key!="SceneIdentity"&&std::all_of(m_Selection.items.begin(),m_Selection.items.end(),[&](auto e){return app.GetWorld().IsAlive(e)&&d.has(app.GetWorld(),e);}))result.push_back(d.id);return result;
}
EditorLayer::BulkValue EditorLayer::InspectBulkProperty(Application& app,ecs::ComponentTypeId component,const std::string& key,bool worldSpace)const{
    BulkValue out;auto* d=ecs::ComponentRegistry::Builtins().Find(component);auto* p=Property(d,key);if(!p||m_Selection.items.empty())return out;
    bool first=true;for(auto e:m_Selection.items){if(!app.GetWorld().IsAlive(e)||!d->has(app.GetWorld(),e))return {};
        auto value=worldSpace&&d->key=="Transform"?TransformValue(app.GetWorld().WorldTransform(e),key):p->get(app.GetWorld(),e);
        if(first){out.value=value;first=false;}else {auto diff=Difference(out.value,value);for(int i=0;i<4;++i)out.mixed[i]|=diff[i];}
    }out.common=true;if(p->kind==ecs::PropertyKind::Color&&p->colorComponents==3)out.mixed[3]=false;return out;
}
bool EditorLayer::SetBulkProperty(Application& app,ecs::ComponentTypeId component,const std::string& key,const PV& value,unsigned axes,bool worldSpace,bool relative,bool transactional){
    try{
        if(app.IsEditorPlayMode())throw std::runtime_error("Stop Play before bulk editing");
        auto& world=app.GetWorld();const auto& registry=ecs::ComponentRegistry::Builtins();auto* d=registry.Find(component);auto* p=Property(d,key);
        if(!p||p->internal||p->readOnly||d->key=="SceneIdentity"||d->key=="PrefabInstance"||p->kind==ecs::PropertyKind::AssetList)throw std::runtime_error("This field requires a single selection or is read-only");
        if(d->key=="DirectionalLight"&&key=="primary")throw std::runtime_error("Choose a single light to set the primary sun");
        if(relative&&d->key!="Transform")throw std::runtime_error("Relative editing is available for Transform only");
        if(!InspectBulkProperty(app,component,key,worldSpace).common)throw std::runtime_error("Every selected entity must have this component");
        if(!axes)return true;
        if(m_Transaction&&(!m_BulkSession||m_BulkSession->component!=component||m_BulkSession->property!=key||m_BulkSession->world!=worldSpace||m_BulkSession->relative!=relative||m_BulkSession->selection!=m_Selection.items||transactional))FinishTransaction(app);
        auto targets=relative?world.HierarchyRoots(m_Selection.items):m_Selection.items;
        BulkSession session{component,key,worldSpace,relative,m_Selection.items,targets,{}};
        if(d->key=="Transform")for(auto e:targets)session.basis.push_back(worldSpace?world.WorldTransform(e):world.LocalTransform(e));
        if(m_BulkSession)session=*m_BulkSession;
        EntitySnapshot before;before.world=world.GetIdentity();before.bulk=true;before.selection=m_Selection;
        std::vector<std::pair<ecs::Entity,ecs::TransformComponent>> transforms;
        struct ComponentEdit{ecs::Entity entity;Json before,after;};std::vector<ComponentEdit> edits;
        ecs::World validation;const auto test=validation.CreateEntity();bool changed=false;
        for(std::size_t i=0;i<targets.size();++i){auto e=targets[i];CheckDriven(app,e,d->key,key);
            EntitySnapshot item;item.entity=e;item.world=world.GetIdentity();item.alive=true;
            if(d->key=="Transform"){
                item.transform=world.LocalTransform(e);auto t=relative?session.basis[i]:(worldSpace?world.WorldTransform(e):world.LocalTransform(e));
                auto next=std::get<ecs::Vec3>(Masked(TransformValue(t,key),value,axes,relative,key));
                // Registry range/type checks are retained; the real World performs
                // the entire hierarchy edit atomically after all targets validate.
                d->deserialize(validation,test,{{"position",{t.position.x,t.position.y,t.position.z}},{"rotation",{t.rotation.x,t.rotation.y,t.rotation.z}},{"scale",{t.scale.x,t.scale.y,t.scale.z}}});
                std::string error;if(!registry.Set(validation,test,component,key,next,&error))throw std::runtime_error(error);
                if(key=="position")t.position=next;else if(key=="rotation")t.rotation=next;else t.scale=next;
                changed|=TransformMatrix(t)!=TransformMatrix(worldSpace?world.WorldTransform(e):world.LocalTransform(e));transforms.emplace_back(e,t);
            }else {
                item.componentOnly=d->key;auto original=d->serialize(world,e);item.components[d->key]=original;
                d->deserialize(validation,test,original);auto old=p->get(validation,test);auto next=Masked(old,value,axes,false,key);std::string error;
                if(!registry.Set(validation,test,component,key,next,&error))throw std::runtime_error(error);
                for(const auto& other:d->properties)if(other.key!=key&&Different(other.get(world,e),other.get(validation,test)))throw std::runtime_error("This value would also change "+other.label+"; edit one entity or choose a compatible value");
                auto after=d->serialize(validation,test);changed|=original!=after;edits.push_back({e,original,std::move(after)});
            }
            before.related.push_back(std::move(item));
        }
        if(!changed){m_EditorError.clear();return true;}
        if(d->key=="Transform"){
            if(!(worldSpace?world.SetWorldTransforms(transforms,&m_EditorError):world.SetLocalTransforms(transforms,&m_EditorError)))throw std::runtime_error(m_EditorError);
        }else {
            try{for(const auto& edit:edits)d->deserialize(world,edit.entity,edit.after);world.ValidateHierarchy();}
            catch(...){for(const auto& edit:edits)d->deserialize(world,edit.entity,edit.before);throw;}
        }
        Notify(app,targets);
        if(!m_Transaction){BeginTransaction("Bulk "+d->label+" / "+p->label,std::move(before));m_BulkSession=std::move(session);}
        if(transactional)FinishTransaction(app);m_EditorError.clear();return true;
    }catch(const std::exception& e){m_EditorError=e.what();return false;}
}
void EditorLayer::RestoreBulkSnapshot(Application& app,const EntitySnapshot& snapshot){
    auto& world=app.GetWorld();std::vector<std::pair<ecs::Entity,ecs::TransformComponent>> transforms;
    struct Edit{ecs::Entity entity;const ecs::ComponentDescriptor* descriptor;Json before,after;};std::vector<Edit> edits;std::vector<ecs::Entity> entities;
    ecs::World validation;auto test=validation.CreateEntity();
    for(const auto& item:snapshot.related){
        if(item.world!=world.GetIdentity()||!item.alive||!world.IsAlive(item.entity))throw std::runtime_error("Bulk history target no longer exists");entities.push_back(item.entity);
        if(item.transform){if(!world.HasComponent<ecs::TransformComponent>(item.entity))throw std::runtime_error("Bulk Transform target is missing");transforms.emplace_back(item.entity,*item.transform);}
        else {
            const auto* d=ecs::ComponentRegistry::Builtins().Find(item.componentOnly);if(!d||!d->has(world,item.entity))throw std::runtime_error("Bulk component no longer exists");
            const auto& desired=item.components.at(d->key);d->deserialize(validation,test,desired);edits.push_back({item.entity,d,d->serialize(world,item.entity),desired});
        }
    }
    try{
        for(const auto& edit:edits)edit.descriptor->deserialize(world,edit.entity,edit.after);world.ValidateHierarchy();
        std::string error;if(!transforms.empty()&&!world.SetLocalTransforms(transforms,&error))throw std::runtime_error(error);
    }catch(...){for(const auto& edit:edits)edit.descriptor->deserialize(world,edit.entity,edit.before);throw;}
    Notify(app,entities);if(snapshot.selection){m_Selection=*snapshot.selection;m_Selection.Prune(world);}m_PropertyDrafts.clear();m_BulkText.clear();
}
}
