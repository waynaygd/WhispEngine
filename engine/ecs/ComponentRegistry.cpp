#include "ComponentRegistry.h"
#include "TransformMath.h"
#include "components/BoundsBounceComponent.h"
#include "components/ColliderComponent.h"
#include "components/LightComponent.h"
#include "components/MaterialComponent.h"
#include "components/MeshRendererComponent.h"
#include "components/RigidbodyComponent.h"
#include "components/SceneIdentityComponent.h"
#include "components/PrefabInstanceComponent.h"
#include "components/TagComponent.h"
#include "components/CharacterMovementComponent.h"
#include "components/PlayerStartComponent.h"
#include "components/FogComponent.h"
#include "components/PostProcessVolumeComponent.h"
#include "components/TransformComponent.h"
#include "components/VelocityComponent.h"
#include "components/PlayerComponent.h"
#include "components/LuaScriptComponent.h"
#include "../resources/MaterialSerialization.h"
#include "../scene/LightSerialization.h"
#include "../scripting/EntityReference.h"
#include <cmath>
#include <filesystem>
namespace ecs {
namespace {
using Json=nlohmann::json;
void Fail(const std::string& message){throw std::runtime_error(message);}
bool Checked(const std::function<void()>& operation,std::string* error){
    try{operation();return true;}catch(const std::exception& e){if(error)*error=e.what();return false;}
}
template<class T>struct Optional {static constexpr bool value=false;};
template<class T>struct Optional<std::optional<T>> {static constexpr bool value=true;using Type=T;};
template<class T>PropertyValue Value(const T& v){
    if constexpr(Optional<T>::value)return v?Value(*v):PropertyValue{};
    else if constexpr(std::is_enum_v<T>)return int(v);
    else return v;
}
template<class T>T Typed(const PropertyValue& v){
    if constexpr(Optional<T>::value){if(std::holds_alternative<std::monostate>(v))return {};return Typed<typename Optional<T>::Type>(v);}
    else if constexpr(std::is_enum_v<T>)return static_cast<T>(std::get<int>(v));
    else return std::get<T>(v);
}
Json ToJson(const PropertyValue& value){
    return std::visit([](const auto& v)->Json{using T=std::decay_t<decltype(v)>;
        if constexpr(std::is_same_v<T,std::monostate>)return nullptr;
        else if constexpr(std::is_same_v<T,Vec3>)return Json::array({v.x,v.y,v.z});
        else return v;
    },value);
}
PropertyValue FromJson(const Json& j,const PropertyDescriptor& p){
    if(j.is_null()&&p.nullable)return {};
    switch(p.kind){
    case PropertyKind::Boolean:return j.get<bool>();
    case PropertyKind::Integer:if(!j.is_number_integer())Fail(p.label+" must be an integer");return j.get<int>();
    case PropertyKind::Number:if(!j.is_number())Fail(p.label+" must be numeric");return j.get<float>();
    case PropertyKind::String:case PropertyKind::Asset:return j.get<std::string>();
    case PropertyKind::AssetList:return j.get<std::vector<std::string>>();
    case PropertyKind::Vector:if(!j.is_array()||j.size()!=3)Fail(p.label+" must have three values");return Vec3{j[0].get<float>(),j[1].get<float>(),j[2].get<float>()};
    case PropertyKind::Color:{if(!j.is_array()||j.size()<p.colorComponents||j.size()>4)Fail(p.label+" invalid color");std::array<float,4> c{0,0,0,1};for(unsigned i=0;i<j.size();++i)c[i]=j[i].get<float>();return c;}
    }Fail("Unknown property kind");return {};
}
void Validate(const PropertyDescriptor& p,const PropertyValue& v){
    if(std::holds_alternative<std::monostate>(v)){if(!p.nullable)Fail(p.label+" cannot be null");return;}
    auto number=[&](float x){if(!std::isfinite(x)||x<p.minimum||x>p.maximum)Fail(p.label+" must be finite in ["+std::to_string(p.minimum)+", "+std::to_string(p.maximum)+"]");};
    switch(p.kind){
    case PropertyKind::Boolean:(void)std::get<bool>(v);break;
    case PropertyKind::Number:number(std::get<float>(v));break;
    case PropertyKind::Integer:number(float(std::get<int>(v)));break;
    case PropertyKind::Vector:{auto x=std::get<Vec3>(v);number(x.x);number(x.y);number(x.z);break;}
    case PropertyKind::Color:for(auto x:std::get<std::array<float,4>>(v))number(x);break;
    case PropertyKind::String:if(std::get<std::string>(v).size()>1024)Fail(p.label+" is too long");break;
    case PropertyKind::Asset:{std::string error;if(!ComponentRegistry::ValidateAssetKey(std::get<std::string>(v),p.asset,&error))Fail(error);break;}
    case PropertyKind::AssetList:for(const auto& key:std::get<std::vector<std::string>>(v)){std::string error;if(!ComponentRegistry::ValidateAssetKey(key,p.asset,&error))Fail(error);}break;
    }
}
template<class T>ComponentDescriptor Descriptor(const char* key,const char* label){
    ComponentDescriptor d;d.key=key;d.label=label;d.id=ComponentId(key);
    d.has=[](const World& w,Entity e){return w.HasComponent<T>(e);};
    d.add=[](World& w,Entity e){w.AddComponent<T>(e);};
    d.remove=[](World& w,Entity e){w.RemoveComponent<T>(e);};
    d.reset=[](World& w,Entity e){*w.GetComponent<T>(e)=T{};};return d;
}
template<class C,class Owner,class V>PropertyDescriptor& Field(ComponentDescriptor& d,const char* key,const char* label,V Owner::*member,PropertyKind kind,float lo=-1000000,float hi=1000000,AssetKind asset=AssetKind::None){
    PropertyDescriptor p;p.key=key;p.label=label;p.tooltip=label;p.kind=kind;p.minimum=lo;p.maximum=hi;p.asset=asset;p.nullable=Optional<V>::value;
    p.get=[member](const World& w,Entity e){auto* c=w.GetComponent<C>(e);if(!c)Fail("Dead/foreign entity or absent component");return Value(c->*member);};
    p.set=[member](World& w,Entity e,const PropertyValue& v){auto* c=w.GetComponent<C>(e);if(!c)Fail("Dead/foreign entity or absent component");c->*member=Typed<V>(v);};
    d.properties.push_back(std::move(p));return d.properties.back();
}
template<class C,class V>PropertyDescriptor& Override(ComponentDescriptor& d,const char* key,const char* label,V MaterialOverrides::*member,PropertyKind kind,float lo,float hi,AssetKind asset=AssetKind::None){
    PropertyDescriptor p;p.key=key;p.label=label;p.tooltip="Optional per-entity override; asset file remains unchanged";p.kind=kind;p.minimum=lo;p.maximum=hi;p.asset=asset;p.nullable=true;
    p.get=[member](const World& w,Entity e){return Value(w.GetComponent<C>(e)->overrides.*member);};
    p.set=[member](World& w,Entity e,const PropertyValue& v){w.GetComponent<C>(e)->overrides.*member=Typed<V>(v);};d.properties.push_back(std::move(p));return d.properties.back();
}
void Complete(ComponentDescriptor& d){
    const auto properties=d.properties;const auto has=d.has;const auto add=d.add;const auto reset=d.reset;
    d.serialize=[properties](const World& w,Entity e){Json j=Json::object();for(const auto& p:properties)j[p.key]=ToJson(p.get(w,e));return j;};
    d.deserialize=[properties,has,add,reset](World& w,Entity e,const Json& j){
        if(!j.is_object())Fail("Component must be an object");if(!has(w,e))add(w,e);else reset(w,e);
        for(const auto& p:properties)if(j.contains(p.key)){auto v=FromJson(j.at(p.key),p);Validate(p,v);p.set(w,e,v);}
    };
}
template<class C>ComponentDescriptor Light(const char* key,const char* label){
    auto d=Descriptor<C>(key,label);
    Field<C>(d,"enabled","Enabled",&C::enabled,PropertyKind::Boolean);
    PropertyDescriptor color;color.key="color";color.label="Color";color.kind=PropertyKind::Color;color.minimum=0;color.maximum=10000;color.colorComponents=3;
    color.get=[](const World& w,Entity e){auto& c=w.GetComponent<C>(e)->color;return PropertyValue(std::array<float,4>{c[0],c[1],c[2],1});};
    color.set=[](World& w,Entity e,const PropertyValue& v){auto c=std::get<std::array<float,4>>(v);w.GetComponent<C>(e)->color={c[0],c[1],c[2]};};d.properties.push_back(color);
    Field<C>(d,"intensity","Intensity",&C::intensity,PropertyKind::Number,0,100000);
    if constexpr(!std::is_same_v<C,DirectionalLightComponent>){
        Field<C>(d,"range","Range",&C::range,PropertyKind::Number,.01f,10000);
        Field<C>(d,"attenuation","Near radius",&C::attenuation,PropertyKind::Number,.01f,1000).tooltip="Inverse-square attenuation with smooth range cutoff; near radius softens the source";
    }
    if constexpr(std::is_same_v<C,SpotLightComponent>){Field<C>(d,"innerAngle","Inner half angle",&C::innerAngle,PropertyKind::Number,0,89);Field<C>(d,"outerAngle","Outer half angle",&C::outerAngle,PropertyKind::Number,.1f,89);}
    Field<C>(d,"castShadows","Cast shadows",&C::castShadows,PropertyKind::Boolean).tooltip="Only primary Directional shadows are currently supported";
    if constexpr(std::is_same_v<C,DirectionalLightComponent>)Field<C>(d,"primary","Prefer primary",&C::primary,PropertyKind::Boolean);
    Complete(d);
    d.serialize=[](const World& w,Entity e){auto* c=w.GetComponent<C>(e);SceneLightDescription light;
        light.type=std::is_same_v<C,DirectionalLightComponent>?LightType::Directional:std::is_same_v<C,PointLightComponent>?LightType::Point:LightType::Spot;light.parameters=*c;
        if constexpr(std::is_same_v<C,DirectionalLightComponent>)light.primary=c->primary;return LightToJson(light);};
    d.deserialize=[](World& w,Entity e,const Json& j){auto v=LightFromJson(j);if(!w.HasComponent<C>(e))w.AddComponent<C>(e);auto& c=*w.GetComponent<C>(e);static_cast<LightParameters&>(c)=v.parameters;if constexpr(std::is_same_v<C,DirectionalLightComponent>)c.primary=v.primary;};return d;
}
ComponentRegistry Build(){
    ComponentRegistry r;
    auto script=Descriptor<LuaScriptComponent>("LuaScript","Lua Script");
    Field<LuaScriptComponent>(script,"scriptPath","Script asset",&LuaScriptComponent::scriptPath,PropertyKind::Asset,0,0,AssetKind::LuaScript);
    Field<LuaScriptComponent>(script,"enabled","Enabled",&LuaScriptComponent::enabled,PropertyKind::Boolean);
    Field<LuaScriptComponent>(script,"inputEnabled","Allow gameplay input",&LuaScriptComponent::inputEnabled,PropertyKind::Boolean);
    Field<LuaScriptComponent>(script,"cameraEnabled","Allow gameplay camera",&LuaScriptComponent::cameraEnabled,PropertyKind::Boolean);Complete(script);
    auto scriptSave=script.serialize;auto scriptLoad=script.deserialize;
    script.serialize=[scriptSave](const World& w,Entity e){auto j=scriptSave(w,e);j["properties"]=w.GetComponent<LuaScriptComponent>(e)->properties;return j;};
    script.deserialize=[scriptLoad](World& w,Entity e,const Json& j){
        auto values=j.value("properties",Json::object());if(!values.is_object()||values.size()>128)Fail("Lua properties must be a bounded object");
        for(auto i=values.begin();i!=values.end();++i){const auto& v=i.value();if(i.key().size()>128||!(v.is_boolean()||v.is_number()||v.is_string()||(v.is_array()&&v.size()==3)||scripting::ValidEntityReference(v)))Fail("Invalid Lua property: "+i.key());if(v.is_string()&&v.get_ref<const std::string&>().size()>1024)Fail("Lua string too long");if(v.is_array())for(const auto& x:v)if(!x.is_number()||!std::isfinite(x.get<double>()))Fail("Invalid Lua vector");if(v.is_number()&&!std::isfinite(v.get<double>()))Fail("Nonfinite Lua property");}
        scriptLoad(w,e,j);w.GetComponent<LuaScriptComponent>(e)->properties=std::move(values);
    };r.Register(script);
    auto tag=Descriptor<TagComponent>("Tag","Name");Field<TagComponent>(tag,"name","Name",&TagComponent::name,PropertyKind::String);Complete(tag);r.Register(tag);
    auto identity=Descriptor<SceneIdentityComponent>("SceneIdentity","Scene identity");identity.addable=identity.removable=false;Field<SceneIdentityComponent>(identity,"value","Persistent ID",&SceneIdentityComponent::value,PropertyKind::String).readOnly=true;Complete(identity);r.Register(identity);
    auto prefab=Descriptor<PrefabInstanceComponent>("PrefabInstance","Prefab instance");prefab.addable=prefab.removable=false;
    Field<PrefabInstanceComponent>(prefab,"asset","Source Prefab",&PrefabInstanceComponent::asset,PropertyKind::Asset,0,0,AssetKind::Prefab).readOnly=true;Complete(prefab);
    auto prefabSave=prefab.serialize;auto prefabLoad=prefab.deserialize;
    prefab.serialize=[prefabSave](const World& w,Entity e){auto j=prefabSave(w,e);j["members"]=w.GetComponent<PrefabInstanceComponent>(e)->members;return j;};
    prefab.deserialize=[prefabLoad](World& w,Entity e,const Json& j){
        const auto& members=j.at("members");if(!members.is_object()||members.empty())Fail("Invalid prefab member map");
        for(const auto& ref:members)if(!scripting::ValidEntityReference(ref)||ref.is_null())Fail("Invalid prefab member reference");
        prefabLoad(w,e,j);w.GetComponent<PrefabInstanceComponent>(e)->members=members;
    };r.Register(prefab);
    auto t=Descriptor<TransformComponent>("Transform","Transform");Field<TransformComponent>(t,"position","Position",&TransformComponent::position,PropertyKind::Vector);Field<TransformComponent>(t,"rotation","Rotation (rad)",&TransformComponent::rotation,PropertyKind::Vector);Field<TransformComponent>(t,"scale","Scale",&TransformComponent::scale,PropertyKind::Vector);Field<TransformComponent>(t,"pivot","Pivot Offset",&TransformComponent::pivot,PropertyKind::Vector);Complete(t);auto serializeTransform=t.serialize;t.serialize=[serializeTransform](const World& w,Entity e){auto j=serializeTransform(w,e);if(!HasPivot(w.LocalTransform(e)))j.erase("pivot");return j;};r.Register(t);
    auto mesh=Descriptor<MeshRendererComponent>("MeshRenderer","Mesh renderer");Field<MeshRendererComponent>(mesh,"meshPath","Mesh",&MeshRendererComponent::meshPath,PropertyKind::Asset,0,0,AssetKind::Mesh);Field<MeshRendererComponent>(mesh,"texturePath","Texture",&MeshRendererComponent::texturePath,PropertyKind::Asset,0,0,AssetKind::Texture);Field<MeshRendererComponent>(mesh,"shaderPath","Shader",&MeshRendererComponent::shaderPath,PropertyKind::Asset,0,0,AssetKind::Shader);Field<MeshRendererComponent>(mesh,"visible","Visible",&MeshRendererComponent::visible,PropertyKind::Boolean);Complete(mesh);r.Register(mesh);
    auto material=Descriptor<MaterialComponent>("Material","Material");Field<MaterialComponent>(material,"materialPath","Asset",&MaterialComponent::materialPath,PropertyKind::Asset,0,0,AssetKind::Material);Field<MaterialComponent>(material,"slotMaterials","Slots",&MaterialComponent::slotMaterials,PropertyKind::AssetList,0,0,AssetKind::Material);Field<MaterialComponent>(material,"shaderPath","Shader",&MaterialComponent::shaderPath,PropertyKind::Asset,0,0,AssetKind::Shader);Field<MaterialComponent>(material,"texturePath","Base texture",&MaterialComponent::texturePath,PropertyKind::Asset,0,0,AssetKind::Texture);
    PropertyDescriptor tint;tint.key="tint";tint.label="Tint";tint.kind=PropertyKind::Color;tint.minimum=0;tint.maximum=1;tint.get=[](const World& w,Entity e){auto& a=w.GetComponent<MaterialComponent>(e)->tint;return PropertyValue(std::array<float,4>{a[0],a[1],a[2],a[3]});};tint.set=[](World& w,Entity e,const PropertyValue& v){auto c=std::get<std::array<float,4>>(v);std::copy(c.begin(),c.end(),w.GetComponent<MaterialComponent>(e)->tint);};material.properties.push_back(tint);
    Override<MaterialComponent>(material,"override.metallic","Metallic",&MaterialOverrides::metallic,PropertyKind::Number,0,1);Override<MaterialComponent>(material,"override.roughness","Roughness",&MaterialOverrides::roughness,PropertyKind::Number,0,1);Override<MaterialComponent>(material,"override.normalScale","Normal scale",&MaterialOverrides::normalScale,PropertyKind::Number,0,16);Override<MaterialComponent>(material,"override.occlusionStrength","AO strength",&MaterialOverrides::occlusionStrength,PropertyKind::Number,0,1);
    Override<MaterialComponent>(material,"override.shadingModel","Shading",&MaterialOverrides::shadingModel,PropertyKind::Integer,0,1).choices={"Lambert","PBR"};
    Override<MaterialComponent>(material,"override.doubleSided","Double sided",&MaterialOverrides::doubleSided,PropertyKind::Boolean,0,1);Override<MaterialComponent>(material,"override.castShadows","Cast shadows",&MaterialOverrides::castShadows,PropertyKind::Boolean,0,1);Override<MaterialComponent>(material,"override.receiveShadows","Receive shadows",&MaterialOverrides::receiveShadows,PropertyKind::Boolean,0,1);
    Override<MaterialComponent>(material,"override.shaderPath","Shader override",&MaterialOverrides::shaderPath,PropertyKind::Asset,0,0,AssetKind::Shader);Override<MaterialComponent>(material,"override.texturePath","Base override",&MaterialOverrides::texturePath,PropertyKind::Asset,0,0,AssetKind::Texture);Override<MaterialComponent>(material,"override.normalTexturePath","Normal map",&MaterialOverrides::normalTexturePath,PropertyKind::Asset,0,0,AssetKind::Texture);Override<MaterialComponent>(material,"override.metallicRoughnessTexturePath","MR map",&MaterialOverrides::metallicRoughnessTexturePath,PropertyKind::Asset,0,0,AssetKind::Texture);Override<MaterialComponent>(material,"override.occlusionTexturePath","AO map",&MaterialOverrides::occlusionTexturePath,PropertyKind::Asset,0,0,AssetKind::Texture);
    Complete(material);auto materialSave=material.serialize;auto materialLoad=material.deserialize;auto materialProperties=material.properties;
    material.serialize=[materialSave](const World& w,Entity e){auto j=materialSave(w,e);for(auto i=j.begin();i!=j.end();)if(i.key().starts_with("override."))i=j.erase(i);else ++i;j["overrides"]=MaterialOverridesToJson(w.GetComponent<MaterialComponent>(e)->overrides);return j;};
    material.deserialize=[materialLoad,materialProperties](World& w,Entity e,const Json& j){materialLoad(w,e,j);if(j.contains("overrides"))w.GetComponent<MaterialComponent>(e)->overrides=MaterialOverridesFromJson(j.at("overrides"));for(const auto& p:materialProperties)Validate(p,p.get(w,e));};r.Register(material);
    auto post=Descriptor<PostProcessVolumeComponent>("PostProcessVolume","Post Process Volume");
    Field<PostProcessVolumeComponent>(post,"enabled","Enabled",&PostProcessVolumeComponent::enabled,PropertyKind::Boolean).section="Volume";
    Field<PostProcessVolumeComponent>(post,"global","Global / Unbound",&PostProcessVolumeComponent::global,PropertyKind::Boolean).section="Volume";
    Field<PostProcessVolumeComponent>(post,"priority","Priority",&PostProcessVolumeComponent::priority,PropertyKind::Integer).section="Volume";
    Field<PostProcessVolumeComponent>(post,"weight","Blend Weight",&PostProcessVolumeComponent::weight,PropertyKind::Number,0,1).section="Volume";
    Field<PostProcessVolumeComponent>(post,"blendRadius","Blend Radius (world units)",&PostProcessVolumeComponent::blendRadius,PropertyKind::Number,0,10000).section="Volume";
    Field<PostProcessVolumeComponent>(post,"extents","Box Half Extents",&PostProcessVolumeComponent::extents,PropertyKind::Vector,.001f,10000).section="Volume";
    Field<PostProcessVolumeComponent>(post,"shape","Shape",&PostProcessVolumeComponent::shape,PropertyKind::Integer,0,0).choices={"Box"};post.properties.back().section="Volume";
#define PP_REGISTER(name,target,lo,hi,sectionName,label) Field<PostProcessVolumeComponent>(post,#name,label,&PostProcessVolumeComponent::name,PropertyKind::Number,lo,hi).section=sectionName;
    WHISP_PP_FLOATS(PP_REGISTER)
#undef PP_REGISTER
    Field<PostProcessVolumeComponent>(post,"fogEnabled","Fog Enabled",&PostProcessVolumeComponent::fogEnabled,PropertyKind::Boolean).section="Fog";
    Field<PostProcessVolumeComponent>(post,"fogMode","Fog Mode",&PostProcessVolumeComponent::fogMode,PropertyKind::Integer,0,2).choices={"Distance","Height","Volumetric"};post.properties.back().section="Fog";
    Field<PostProcessVolumeComponent>(post,"fogColor","Fog Color",&PostProcessVolumeComponent::fogColor,PropertyKind::Color,0,1).section="Fog";post.properties.back().colorComponents=3;
    Field<PostProcessVolumeComponent>(post,"scatteringColor","Scattering Color",&PostProcessVolumeComponent::scatteringColor,PropertyKind::Color,0,1).section="Volumetric Fog";post.properties.back().colorComponents=3;
    Field<PostProcessVolumeComponent>(post,"fogQuality","Fog Quality",&PostProcessVolumeComponent::fogQuality,PropertyKind::Integer,0,2).choices={"Low","Medium","High"};post.properties.back().section="Volumetric Fog";
    Field<PostProcessVolumeComponent>(post,"sceneLighting","Scene Lighting",&PostProcessVolumeComponent::sceneLighting,PropertyKind::Boolean).section="Volumetric Fog";
    Field<PostProcessVolumeComponent>(post,"toneMapping","Tone Mapping",&PostProcessVolumeComponent::toneMapping,PropertyKind::Boolean).section="Environment";
    Field<PostProcessVolumeComponent>(post,"tint","Color Tint",&PostProcessVolumeComponent::tint,PropertyKind::Color,0,1).section="Color Grading";post.properties.back().colorComponents=3;
    Complete(post);auto loadPost=post.deserialize;post.deserialize=[loadPost](World& w,Entity e,const Json& j){loadPost(w,e,j);auto error=ValidatePostProcessVolume(*w.GetComponent<PostProcessVolumeComponent>(e));if(!error.empty())Fail(error);};
    for(auto& property:post.properties){auto set=property.set;property.set=[set](World& w,Entity e,const PropertyValue& value){auto& volume=*w.GetComponent<PostProcessVolumeComponent>(e);auto previous=volume;set(w,e,value);auto error=ValidatePostProcessVolume(volume);if(!error.empty()){volume=previous;Fail(error);}};}
    auto savePost=post.serialize;post.serialize=[savePost](const World& w,Entity e){auto j=savePost(w,e);j["customEffects"]=w.GetComponent<PostProcessVolumeComponent>(e)->customEffects;return j;};
    auto loadCustomPost=post.deserialize;post.deserialize=[loadCustomPost](World& w,Entity e,const Json& j){loadCustomPost(w,e,j);if(j.contains("customEffects")){if(!j["customEffects"].is_array()||j["customEffects"].size()>32)Fail("At most 32 Custom PostFX per Volume");w.GetComponent<PostProcessVolumeComponent>(e)->customEffects=j["customEffects"].get<std::vector<PostFxOverride>>();}};
    r.Register(post);
    auto body=Descriptor<RigidbodyComponent>("Rigidbody","Rigidbody");
    Field<RigidbodyComponent>(body,"velocity","Velocity",&RigidbodyComponent::velocity,PropertyKind::Vector);Field<RigidbodyComponent>(body,"acceleration","Acceleration",&RigidbodyComponent::acceleration,PropertyKind::Vector);Field<RigidbodyComponent>(body,"angularVelocity","Angular velocity",&RigidbodyComponent::angularVelocity,PropertyKind::Vector);Field<RigidbodyComponent>(body,"torque","Torque",&RigidbodyComponent::torque,PropertyKind::Vector);Field<RigidbodyComponent>(body,"mass","Mass",&RigidbodyComponent::mass,PropertyKind::Number,.00001f,1e9f);Field<RigidbodyComponent>(body,"useGravity","Gravity",&RigidbodyComponent::useGravity,PropertyKind::Boolean);Field<RigidbodyComponent>(body,"isStatic","Static",&RigidbodyComponent::isStatic,PropertyKind::Boolean);Field<RigidbodyComponent>(body,"simulatePhysics","Simulate",&RigidbodyComponent::simulatePhysics,PropertyKind::Boolean);Field<RigidbodyComponent>(body,"linearDampingMultiplier","Linear damping",&RigidbodyComponent::linearDampingMultiplier,PropertyKind::Number,0,100);Field<RigidbodyComponent>(body,"angularDampingMultiplier","Angular damping",&RigidbodyComponent::angularDampingMultiplier,PropertyKind::Number,0,100);Field<RigidbodyComponent>(body,"sleeping","Sleeping",&RigidbodyComponent::sleeping,PropertyKind::Boolean).readOnly=true;Field<RigidbodyComponent>(body,"sleepTimer","Sleep timer",&RigidbodyComponent::sleepTimer,PropertyKind::Number,0,1e9f).readOnly=true;Field<RigidbodyComponent>(body,"useAdvancedSphereStabilization","Sphere stabilization",&RigidbodyComponent::useAdvancedSphereStabilization,PropertyKind::Boolean);Field<RigidbodyComponent>(body,"lockRotation","Lock rotation",&RigidbodyComponent::lockRotation,PropertyKind::Boolean);Complete(body);r.Register(body);
    auto collider=Descriptor<ColliderComponent>("Collider","Collider");Field<ColliderComponent>(collider,"type","Shape",&ColliderComponent::type,PropertyKind::Integer,0,1).choices={"Box","Sphere"};Field<ColliderComponent>(collider,"halfExtents","Half extents",&ColliderComponent::halfExtents,PropertyKind::Vector,0,1e6f);Field<ColliderComponent>(collider,"offset","Offset",&ColliderComponent::offset,PropertyKind::Vector);Field<ColliderComponent>(collider,"restitution","Restitution",&ColliderComponent::restitution,PropertyKind::Number,-1,1);Field<ColliderComponent>(collider,"friction","Friction",&ColliderComponent::friction,PropertyKind::Number,-1,100);Field<ColliderComponent>(collider,"autoFitFromMesh","Fit from mesh",&ColliderComponent::autoFitFromMesh,PropertyKind::Boolean);Field<ColliderComponent>(collider,"isTrigger","Is trigger",&ColliderComponent::isTrigger,PropertyKind::Boolean);Complete(collider);r.Register(collider);
    auto velocity=Descriptor<VelocityComponent>("Velocity","Velocity");Field<VelocityComponent>(velocity,"linear","Linear",&VelocityComponent::linear,PropertyKind::Vector);Field<VelocityComponent>(velocity,"angular","Angular",&VelocityComponent::angular,PropertyKind::Vector);Complete(velocity);r.Register(velocity);
    auto bounds=Descriptor<BoundsBounceComponent>("BoundsBounce","Bounds bounce");Field<BoundsBounceComponent>(bounds,"minX","Min X",&BoundsBounceComponent::minX,PropertyKind::Number);Field<BoundsBounceComponent>(bounds,"maxX","Max X",&BoundsBounceComponent::maxX,PropertyKind::Number);Field<BoundsBounceComponent>(bounds,"minY","Min Y",&BoundsBounceComponent::minY,PropertyKind::Number);Field<BoundsBounceComponent>(bounds,"maxY","Max Y",&BoundsBounceComponent::maxY,PropertyKind::Number);Complete(bounds);r.Register(bounds);
    r.Register(Light<DirectionalLightComponent>("DirectionalLight","Directional light"));r.Register(Light<PointLightComponent>("PointLight","Point light"));r.Register(Light<SpotLightComponent>("SpotLight","Spot light"));
    auto fog=Descriptor<FogComponent>("Fog","Fog");
    Field<FogComponent>(fog,"enabled","Enabled",&FogComponent::enabled,PropertyKind::Boolean);
    Field<FogComponent>(fog,"mode","Mode",&FogComponent::mode,PropertyKind::Integer,0,2).choices={"Distance","Height","Volumetric"};
    Field<FogComponent>(fog,"color","Fog Color",&FogComponent::color,PropertyKind::Color,0,1).colorComponents=3;
    Field<FogComponent>(fog,"density","Intensity",&FogComponent::density,PropertyKind::Number,0,1).tooltip="Distance blend intensity; Height/Volumetric density multiplier";
    Field<FogComponent>(fog,"startDistance","Start Distance",&FogComponent::startDistance,PropertyKind::Number,0,10000).section="Distance";
    Field<FogComponent>(fog,"endDistance","End Distance",&FogComponent::endDistance,PropertyKind::Number,.001f,10000).section="Distance";
    Field<FogComponent>(fog,"baseHeight","Base Height",&FogComponent::baseHeight,PropertyKind::Number,-10000,10000).section="Medium";
    Field<FogComponent>(fog,"heightDensity","Height Density",&FogComponent::heightDensity,PropertyKind::Number,0,10).section="Height";
    Field<FogComponent>(fog,"heightFalloff","Height Falloff",&FogComponent::heightFalloff,PropertyKind::Number,0,10).section="Medium";
    Field<FogComponent>(fog,"scatteringColor","Scattering Color",&FogComponent::scatteringColor,PropertyKind::Color,0,1).section="Volumetric";fog.properties.back().colorComponents=3;
    Field<FogComponent>(fog,"extinction","Extinction",&FogComponent::extinction,PropertyKind::Number,0,10).section="Volumetric";
    Field<FogComponent>(fog,"anisotropy","Anisotropy",&FogComponent::anisotropy,PropertyKind::Number,-.95f,.95f).section="Volumetric";
    Field<FogComponent>(fog,"maxDistance","Max Distance / Sky",&FogComponent::maxDistance,PropertyKind::Number,.01f,10000);
    Field<FogComponent>(fog,"quality","Quality",&FogComponent::quality,PropertyKind::Integer,0,2).choices={"Low (16 steps)","Medium (32 steps)","High (64 steps)"};fog.properties.back().section="Volumetric";
    Field<FogComponent>(fog,"sceneLighting","Scene Lighting",&FogComponent::sceneLighting,PropertyKind::Boolean).section="Volumetric";
    Complete(fog);auto loadFog=fog.deserialize;
    fog.deserialize=[loadFog](World& w,Entity e,const Json& j){loadFog(w,e,j);if(!ValidFog(*w.GetComponent<FogComponent>(e)))Fail("Fog: invalid parameters (End Distance must exceed Start Distance)");};
    for(auto& property:fog.properties){auto set=property.set;property.set=[set](World& w,Entity e,const PropertyValue& v){auto& c=*w.GetComponent<FogComponent>(e);auto before=c;set(w,e,v);if(!ValidFog(c)){c=before;Fail("Fog: End Distance must exceed Start Distance; all parameters must be finite and within range");}};}
    r.Register(fog);
    auto playerStart = Descriptor<PlayerStartComponent>(
        "PlayerStart", "Player Start"
    );

    Field<PlayerStartComponent>(
        playerStart,
        "enabled",
        "Enabled",
        &PlayerStartComponent::enabled,
        PropertyKind::Boolean
    );

    Field<PlayerStartComponent>(playerStart,"controllerScript","Controller script",&PlayerStartComponent::controllerScript,PropertyKind::Asset,0,0,AssetKind::LuaScript);
    Field<PlayerStartComponent>(playerStart,"characterHeight","Character Height",&PlayerStartComponent::characterHeight,PropertyKind::Number,.2f,10.f).set=[](World& w,Entity e,const PropertyValue& v){
        auto& c=*w.GetComponent<PlayerStartComponent>(e);c.characterHeight=std::get<float>(v);c.eyeHeight=std::min(c.eyeHeight,c.characterHeight);
    };
    Field<PlayerStartComponent>(playerStart,"eyeHeight","Eye Height",&PlayerStartComponent::eyeHeight,PropertyKind::Number,0.f,10.f).set=[](World& w,Entity e,const PropertyValue& v){
        auto& c=*w.GetComponent<PlayerStartComponent>(e);const auto height=std::get<float>(v);if(height>c.characterHeight)Fail("Eye Height must not exceed Character Height");c.eyeHeight=height;
    };
    Complete(playerStart);
    r.Register(playerStart);

    auto player = Descriptor<PlayerComponent>(
        "Player", "Player"
    );

    Field<PlayerComponent>(
        player,
        "eyeHeight",
        "Eye Height",
        &PlayerComponent::eyeHeight,
        PropertyKind::Number,
        0.0f,
        10.0f
    );

    Complete(player);
    r.Register(player);

    auto movement =
        Descriptor<CharacterMovementComponent>(
            "CharacterMovement",
            "Character Movement"
        );

    Field<CharacterMovementComponent>(
        movement,
        "moveSpeed",
        "Move Speed",
        &CharacterMovementComponent::moveSpeed,
        PropertyKind::Number,
        0.0f,
        30.0f
    );

    Field<CharacterMovementComponent>(
        movement,
        "jumpSpeed",
        "Jump Speed",
        &CharacterMovementComponent::jumpSpeed,
        PropertyKind::Number,
        0.0f,
        20.0f
    );

    Complete(movement);
    r.Register(movement);

    return r;
}
}
ComponentRegistry& ComponentRegistry::Builtins(){static auto registry=Build();return registry;}
void ComponentRegistry::Register(ComponentDescriptor d){if(d.key.empty()||!d.has||!d.add||!d.remove||!d.reset||!d.serialize||!d.deserialize)Fail("Incomplete component descriptor");if(!d.id||d.id!=ComponentId(d.key.c_str())||Find(d.id)||Find(d.key))Fail("Duplicate/invalid stable component ID: "+d.key);for(auto& p:d.properties){
    p.internal=d.key=="SceneIdentity";
    p.advanced=p.key=="shaderPath"||p.key=="texturePath"||p.key=="autoFitFromMesh"||p.key=="offset"||p.key=="attenuation"||p.key=="linearDampingMultiplier"||p.key=="angularDampingMultiplier"||p.key=="acceleration"||p.key=="torque"||p.key=="angularVelocity"||p.readOnly;
    if(d.key=="Material"){
        p.advanced=false;const auto& k=p.key;
        p.section=k=="materialPath"||k=="override.shadingModel"?"Surface":k=="tint"||k=="texturePath"||k=="override.texturePath"?"Base Color":k=="override.metallic"||k=="override.roughness"||k=="override.metallicRoughnessTexturePath"?"Metallic / Roughness":k=="override.normalScale"||k=="override.normalTexturePath"?"Normal Map":k=="override.occlusionStrength"||k=="override.occlusionTexturePath"?"AO":k=="override.doubleSided"||k=="override.castShadows"||k=="override.receiveShadows"?"Rendering":"Advanced";
    }
}components.push_back(std::move(d));}
const ComponentDescriptor* ComponentRegistry::Find(ComponentTypeId id)const{for(const auto& d:components)if(d.id==id)return &d;return nullptr;}
const ComponentDescriptor* ComponentRegistry::Find(const std::string& key)const{for(const auto& d:components)if(d.key==key)return &d;return nullptr;}
bool ComponentRegistry::Add(World& w,Entity e,ComponentTypeId id,std::string* error)const{return Checked([&]{w.AssertOwnerThread();auto* d=Find(id);if(!w.IsAlive(e)||!d||!d->addable||d->has(w,e))Fail("Invalid entity/component add");
    if(id==ComponentId("Rigidbody")&&w.Parent(e).IsValid())Fail("Add Rigidbody at a root, set Static, then parent it");
    if(id==ComponentId("Rigidbody")&&w.HasComponent<TransformComponent>(e)&&HasPivot(w.LocalTransform(e)))Fail("Reset Pivot before adding Rigidbody, then choose Static before editing Pivot");
    const bool light=id==ComponentId("DirectionalLight")||id==ComponentId("PointLight")||id==ComponentId("SpotLight");if(light&&GetLightParameters(w,e))Fail("Entity already has a light component");
    if((light||id==ComponentId("MeshRenderer")||id==ComponentId("Rigidbody")||id==ComponentId("Collider")||id==ComponentId("PostProcessVolume"))&&!w.HasComponent<TransformComponent>(e))w.AddComponent<TransformComponent>(e);d->add(w,e);},error);}
bool ComponentRegistry::Remove(World& w,Entity e,ComponentTypeId id,std::string* error)const{return Checked([&]{w.AssertOwnerThread();auto* d=Find(id);if(!w.IsAlive(e)||!d||!d->removable||!d->has(w,e))Fail("Invalid entity/component remove");if(id==ComponentId("Transform")&&(w.Parent(e).IsValid()||!w.Children(e).empty()))Fail("Unparent hierarchy before removing Transform");if(id==ComponentId("Transform")&&(GetLightParameters(w,e)||w.HasComponent<MeshRendererComponent>(e)||w.HasComponent<RigidbodyComponent>(e)||w.HasComponent<ColliderComponent>(e)||w.HasComponent<PostProcessVolumeComponent>(e)))Fail("Remove dependent render/light/physics components before Transform");d->remove(w,e);},error);}
bool ComponentRegistry::Reset(World& w,Entity e,ComponentTypeId id,std::string* error)const{return Checked([&]{w.AssertOwnerThread();auto* d=Find(id);if(!w.IsAlive(e)||!d||!d->has(w,e)||!d->removable)Fail("Invalid component reset");if(id==ComponentId("Transform")){std::string message;if(!w.SetLocalTransform(e,TransformComponent{},&message))Fail(message);return;}if(id==ComponentId("Rigidbody")&&w.Parent(e).IsValid())Fail("Unparent before resetting a Rigidbody to dynamic defaults");if(id==ComponentId("Rigidbody")&&w.HasComponent<TransformComponent>(e)&&HasPivot(w.LocalTransform(e)))Fail("Reset Pivot before resetting Rigidbody to dynamic defaults");d->reset(w,e);},error);}
bool ComponentRegistry::Get(const World& w,Entity e,ComponentTypeId id,const std::string& key,PropertyValue& value,std::string* error)const{return Checked([&]{w.AssertOwnerThread();auto* d=Find(id);if(!w.IsAlive(e)||!d||!d->has(w,e))Fail("Dead/foreign entity or absent component");for(const auto& p:d->properties)if(p.key==key){value=p.get(w,e);return;}Fail("Unknown property: "+key);},error);}
bool ComponentRegistry::Set(World& w,Entity e,ComponentTypeId id,const std::string& key,const PropertyValue& v,std::string* error)const{return Checked([&]{w.AssertOwnerThread();auto* d=Find(id);if(!w.IsAlive(e)||!d||!d->has(w,e))Fail("Dead/foreign entity or absent component");const PropertyDescriptor* p=nullptr;for(const auto& item:d->properties)if(item.key==key)p=&item;if(!p||p->readOnly)Fail("Unknown/readonly property: "+key);Validate(*p,v);
    if(id==ComponentId("Transform")){auto t=w.LocalTransform(e);if(key=="position")t.position=std::get<Vec3>(v);else if(key=="rotation")t.rotation=std::get<Vec3>(v);else if(key=="scale")t.scale=std::get<Vec3>(v);else if(key=="pivot")t=RebasePivot(t,std::get<Vec3>(v));std::string message;if(!w.SetLocalTransform(e,t,&message))Fail(message);return;}
    auto previous=p->get(w,e);p->set(w,e,v);if(id==ComponentId("Rigidbody")){try{w.ValidateHierarchy();}catch(...){p->set(w,e,previous);throw;}}
    if(auto* light=GetLightParameters(w,e))*light=SanitizeLight(*light);if(auto* b=w.GetComponent<BoundsBounceComponent>(e)){if(b->minX>b->maxX)std::swap(b->minX,b->maxX);if(b->minY>b->maxY)std::swap(b->minY,b->maxY);}
},error);}
nlohmann::json ComponentRegistry::Capture(const World& w,Entity e)const{if(!w.IsAlive(e))Fail("Cannot capture foreign/dead entity");auto j=nlohmann::json::object();for(const auto& d:components)if(d.has(w,e))j[d.key]=d.serialize(w,e);return j;}
void ComponentRegistry::Restore(World& w,Entity e,const nlohmann::json& j)const{
    if(!w.IsAlive(e)||!j.is_object())Fail("Invalid entity snapshot");for(auto i=j.begin();i!=j.end();++i)if(!Find(i.key()))Fail("Unknown component: "+i.key());
    for(const auto& d:components){if(j.contains(d.key))d.deserialize(w,e,j.at(d.key));else if(d.has(w,e))d.remove(w,e);}
    // Old Player-only scenes stored speed here. Explicit CharacterMovement wins.
    if(j.contains("Player")&&j["Player"].contains("moveSpeed")&&(!j.contains("CharacterMovement")||!j["CharacterMovement"].contains("moveSpeed"))){
        auto legacy=j["Player"]["moveSpeed"].get<float>();if(!std::isfinite(legacy)||legacy<0||legacy>30)Fail("Invalid legacy player speed");
        if(!w.HasComponent<CharacterMovementComponent>(e))w.AddComponent<CharacterMovementComponent>(e);
        w.GetComponent<CharacterMovementComponent>(e)->moveSpeed=legacy;
    }
    if(w.HasComponent<TransformComponent>(e)==false&&(GetLightParameters(w,e)||w.HasComponent<MeshRendererComponent>(e)||w.HasComponent<RigidbodyComponent>(e)||w.HasComponent<ColliderComponent>(e)||w.HasComponent<PostProcessVolumeComponent>(e)))Fail("Render/light/physics entity requires Transform");
    int lights=int(w.HasComponent<DirectionalLightComponent>(e))+int(w.HasComponent<PointLightComponent>(e))+int(w.HasComponent<SpotLightComponent>(e));if(lights>1)Fail("Multiple light component types on one entity");
}
bool ComponentRegistry::ValidateAssetKey(const std::string& key,AssetKind kind,std::string* error){return Checked([&]{if(key.empty())return;
    const auto path=std::filesystem::u8path(key);if(key.size()>1024||path.is_absolute()||path.has_root_name()||key.find(':')!=std::string::npos||key.find('\\')!=std::string::npos)Fail("Use a portable relative resource key");for(auto& part:path)if(part=="..")Fail("Resource key cannot escape project root");
    std::string ext=path.extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
    bool valid=kind==AssetKind::None||(kind==AssetKind::PostFx&&key.ends_with(".postfx.json"))||(kind==AssetKind::LuaScript&&ext==".lua")||(kind==AssetKind::Prefab&&key.ends_with(".prefab.json"))||(kind==AssetKind::Mesh&&(ext==".obj"||ext==".gltf"||ext==".glb"||ext==".fbx"))||(kind==AssetKind::Texture&&(ext==".png"||ext==".dds"||ext==".jpg"||ext==".jpeg"||ext==".bmp"||ext==".tga"))||(kind==AssetKind::Material&&(ext==".json"||ext==".material"))||(kind==AssetKind::Shader&&ext==".hlsl")||(kind==AssetKind::Environment&&ext==".hdr")||key.starts_with("defaults/");if(!valid)Fail("Resource type does not match property");
},error);}
}

