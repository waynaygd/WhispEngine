#pragma once
#include "World.h"
#include "MathTypes.h"
#include <array>
#include <functional>
#include <variant>
#include <nlohmann/json.hpp>

namespace ecs {
using ComponentTypeId=std::uint64_t;
constexpr ComponentTypeId ComponentId(const char* text){
    ComponentTypeId h=14695981039346656037ull;while(*text){h^=static_cast<unsigned char>(*text++);h*=1099511628211ull;}return h;
}
enum class PropertyKind { Boolean,Integer,Number,String,Vector,Color,Asset,AssetList };
enum class AssetKind { None,Mesh,Texture,Material,Shader,Environment };
using PropertyValue=std::variant<std::monostate,bool,int,float,std::string,Vec3,std::array<float,4>,std::vector<std::string>>;
struct PropertyDescriptor {
    std::string key,label,tooltip;
    PropertyKind kind=PropertyKind::Number;
    AssetKind asset=AssetKind::None;
    float minimum=-1000000,maximum=1000000,speed=.05f;
    bool readOnly=false,nullable=false;
    unsigned colorComponents=4;
    std::vector<std::string> choices;
    std::function<PropertyValue(const World&,Entity)> get;
    std::function<void(World&,Entity,const PropertyValue&)> set;
};
struct ComponentDescriptor {
    ComponentTypeId id=0;
    std::string key,label,tooltip;
    bool removable=true,addable=true;
    std::function<bool(const World&,Entity)> has;
    std::function<void(World&,Entity)> add,remove,reset;
    std::function<nlohmann::json(const World&,Entity)> serialize;
    std::function<void(World&,Entity,const nlohmann::json&)> deserialize;
    std::vector<PropertyDescriptor> properties;
};
class ComponentRegistry {
    std::vector<ComponentDescriptor> components;
public:
    // Register extensions on the main thread during initialization, before rendering/loading scenes.
    static ComponentRegistry& Builtins();
    void Register(ComponentDescriptor component);
    const auto& Components()const{return components;}
    const ComponentDescriptor* Find(ComponentTypeId id)const;
    const ComponentDescriptor* Find(const std::string& key)const;
    bool Add(World&,Entity,ComponentTypeId,std::string* error=nullptr)const;
    bool Remove(World&,Entity,ComponentTypeId,std::string* error=nullptr)const;
    bool Reset(World&,Entity,ComponentTypeId,std::string* error=nullptr)const;
    bool Set(World&,Entity,ComponentTypeId,const std::string& property,const PropertyValue&,std::string* error=nullptr)const;
    bool Get(const World&,Entity,ComponentTypeId,const std::string& property,PropertyValue&,std::string* error=nullptr)const;
    nlohmann::json Capture(const World&,Entity)const;
    void Restore(World&,Entity,const nlohmann::json&)const;
    static bool ValidateAssetKey(const std::string&,AssetKind,std::string* error=nullptr);
};
}
