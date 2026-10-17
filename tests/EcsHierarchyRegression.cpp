#define NOMINMAX
#include "EditorTestPreferences.h"
#include "ecs/Hierarchy.h"
#include "ecs/TransformAccess.h"
#include "ecs/ComponentRegistry.h"
#include "ecs/MutationQueue.h"
#include "ecs/PlayerSpawn.h"
#include "ecs/components/TagComponent.h"
#include "ecs/components/SceneIdentityComponent.h"
#include "ecs/components/LuaScriptComponent.h"
#include "ecs/components/LightComponent.h"
#include "scripting/LuaRuntime.h"
#include "scripting/Prefab.h"
#include "scene/SceneSerializer.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <set>
#include <source_location>
using namespace ecs;
using Json=nlohmann::json;
static int checks=0;
static void Check(bool value,const char* label,std::source_location where=std::source_location::current()){if(!value)throw std::runtime_error(std::string(label)+" at "+std::to_string(where.line()));++checks;}
static void Near(double a,double b,double tolerance=2e-4){Check(std::abs(a-b)<tolerance,"numeric world oracle");}
static void MatrixNear(const Matrix4& a,const Matrix4& b){for(int i=0;i<16;++i)Near(a[i],b[i]);}
static Entity Object(World& w,const std::string& id,Vec3 position={}){auto e=w.CreateEntity();w.AddComponent<SceneIdentityComponent>(e).value=id;w.AddComponent<TransformComponent>(e).position=position;w.AddComponent<TagComponent>(e).name=id;return e;}
static Entity Find(const World& w,const std::string& id){for(auto e:w.Entities())if(auto* value=w.GetComponent<SceneIdentityComponent>(e);value&&value->value==id)return e;return {};}
static Json Raw(const World& w){Json result={{"world",w.GetIdentity()},{"capacity",w.GetCapacity()},{"entities",Json::array()}};for(auto e:w.Entities())result["entities"].push_back({{"index",e.index},{"generation",e.generation},{"components",ComponentRegistry::Builtins().Capture(w,e)}});return result;}
static Json Canonical(Json scene){auto& nodes=scene["entities"];std::sort(nodes.begin(),nodes.end(),[](const auto& a,const auto& b){return a.at("id").template get<std::string>()<b.at("id").template get<std::string>();});return scene;}
template<class F>static void Rejected(World& w,F&& operation){const auto before=Raw(w);Check(!operation(),"operation rejected");Check(Raw(w)==before,"rejection preserves entire World identity/components/slots");}
template<class F>static void ThrowsUnchanged(World& w,F&& operation){const auto before=Raw(w);bool threw=false;try{operation();}catch(const std::exception&){threw=true;}Check(threw&&Raw(w)==before,"guard/exception preserves World");}
static void Graph(){
 World w(World::DataMode::HierarchyData);auto root=Object(w,"root"),a=Object(w,"a"),b=Object(w,"b"),c=Object(w,"c");std::string error;
 Check(SetParent(w,a,root,&error)&&SetParent(w,b,root,&error)&&SetParent(w,c,a,&error),"root child grandchild");
 Check(GetParent(w,a)==root&&GetParent(w,c)==a&&!GetParent(w,root).IsValid(),"parent lookup");Check(*GetChildren(w,root)==std::vector<Entity>{a,b},"append sibling order");
 Check(IsAncestor(w,root,c)==true&&IsDescendant(w,c,root)==true&&IsAncestor(w,c,root)==false,"ancestry");
 Rejected(w,[&]{return SetParent(w,root,root);});Rejected(w,[&]{return SetParent(w,root,c);});Rejected(w,[&]{return SetParent(w,a,{});});
 World foreign(World::DataMode::HierarchyData);auto wrong=Object(foreign,"wrong");Rejected(w,[&]{return SetParent(w,a,wrong);});Rejected(w,[&]{return SetParent(w,wrong,root);});
 Check(!GetChildren(w,wrong)&&!IsAncestor(w,wrong,root),"foreign queries rejected");
 auto g=HierarchyGraph::Build(w,&error);Check(bool(g),"valid graph");Check(*g->Closure({root,a,c})==std::vector<Entity>{root,a,c,b},"unique preorder closure");
 auto order=*g->Closure({root},true);auto position=[&](Entity e){return std::find(order.begin(),order.end(),e)-order.begin();};Check(position(c)<position(a)&&position(a)<position(root)&&position(b)<position(root),"children precede parents");
 w.GetComponent<RelationshipComponent>(b)->order=w.GetComponent<RelationshipComponent>(a)->order;Check(*GetChildren(w,root)==std::vector<Entity>{a,b},"tie order stable SceneIdentity");
 auto saved=*w.GetComponent<RelationshipComponent>(a);w.GetComponent<RelationshipComponent>(a)->parent="missing";ThrowsUnchanged(w,[&]{(void)w.CloneData();});Check(!ValidateHierarchy(w),"missing parent rejected");*w.GetComponent<RelationshipComponent>(a)=saved;
 w.GetComponent<RelationshipComponent>(a)->order=MaximumSiblingOrder+1;Check(!ValidateHierarchy(w),"invalid native sibling order");*w.GetComponent<RelationshipComponent>(a)=saved;
 auto oldOrder=w.GetComponent<RelationshipComponent>(c)->order;w.GetComponent<RelationshipComponent>(c)->order=MaximumSiblingOrder;Rejected(w,[&]{return SetParent(w,b,a);});w.GetComponent<RelationshipComponent>(c)->order=oldOrder;
 w.GetComponent<SceneIdentityComponent>(a)->value="root";Check(!ValidateHierarchy(w),"duplicate IDs rejected");w.GetComponent<SceneIdentityComponent>(a)->value=std::string("a\0bad",5);Check(!ValidateHierarchy(w),"embedded NUL identity rejected");w.GetComponent<SceneIdentityComponent>(a)->value="a";
 w.AddComponent<RelationshipComponent>(root).parent="c";Check(!ValidateHierarchy(w),"complete graph cycle rejected");w.RemoveComponent<RelationshipComponent>(root);
 World deep(World::DataMode::HierarchyData);Entity first,last;
 for(int i=0;i<4096;++i){auto id="node-"+std::to_string(i);auto e=Object(deep,id,{1,0,0});if(i)deep.AddComponent<RelationshipComponent>(e).parent="node-"+std::to_string(i-1);else first=e;last=e;}
 auto view=HierarchyGraph::Build(deep,&error);Check(view&&view->preorder.size()==4096,"deep iterative evaluation");Near((*ReadWorldMatrix(deep,last))[12],4096);Check(view->Closure({first,last})->size()==4096,"deep unique closure");
 World roots;auto r=Object(roots,"root"),child=Object(roots,"child");Rejected(roots,[&]{return SetParent(roots,child,r);});ThrowsUnchanged(roots,[&]{roots.AddComponent<RelationshipComponent>(child);});
 Check(!ComponentRegistry::Builtins().Add(roots,child,ComponentId("Relationship"),&error),"no public Inspector parenting component");
}
static void Transform(){
 World w(World::DataMode::HierarchyData);auto p=Object(w,"parent"),c=Object(w,"child",{2,0,0}),g=Object(w,"grandchild",{3,0,0});
 Check(SetParent(w,c,p)&&SetParent(w,g,c),"initial Keep World parenting");Near(w.GetComponent<TransformComponent>(g)->position.x,1);
 Check(TrySetWorldPosition(w,p,{10,0,0}),"parent world move");Near(ReadWorldPosition(w,c)->x,12);Near(ReadWorldPosition(w,g)->x,13);
 TransformComponent t;t.rotation.z=1.57079632679f;Check(TrySetWorldTransform(w,p,t),"parent world rotation");Near(ReadWorldPosition(w,c)->x,0);Near(ReadWorldPosition(w,c)->y,2);Near(ReadWorldPosition(w,g)->y,3);
 t.scale={2,2,2};Check(TrySetWorldTransform(w,p,t),"uniform scale inheritance");Near(ReadWorldPosition(w,g)->y,6);
 t.rotation={};t.scale={-2,2,2};Check(TrySetWorldTransform(w,p,t),"negative parent scale");Near(ReadWorldPosition(w,c)->x,-4);MatrixNear(ComposeTRS(*ReadWorldTransform(w,c)),*ReadWorldMatrix(w,c));
 auto before=*ReadWorldMatrix(w,c);auto other=Object(w,"other",{8,1,0});Check(SetParent(w,c,other),"Keep World reparent");MatrixNear(*ReadWorldMatrix(w,c),before);Check(ClearParent(w,c),"Keep World unparent");MatrixNear(*ReadWorldMatrix(w,c),before);
 Check(TrySetWorldScale(w,other,{0,1,1}),"unrelated singular root remains representable");Rejected(w,[&]{return SetParent(w,c,other);});
 World shear(World::DataMode::HierarchyData);auto s=Object(shear,"s"),leaf=Object(shear,"leaf",{2,0,0});Check(SetParent(shear,leaf,s),"shear fixture attach");
 auto local=*shear.GetComponent<TransformComponent>(leaf);local.rotation.z=.7853981633974483f;*shear.GetComponent<TransformComponent>(leaf)=local;
 Check(TrySetWorldScale(shear,s,{2,1,1}),"world composition may retain shear");auto world=*ReadWorldMatrix(shear,leaf);auto expected=MultiplyTransformMatrices(ComposeTRS(*shear.GetComponent<TransformComponent>(s)),ComposeTRS(local));MatrixNear(world,expected);
 Check(!ReadWorldTransform(shear,leaf)&&!ReadWorldRotation(shear,leaf)&&!ReadWorldScale(shear,leaf),"checked TRS rejects shear without losing matrix");Near(ReadWorldPosition(shear,leaf)->x,4);
 Rejected(shear,[&]{return ClearParent(shear,leaf);});Rejected(shear,[&]{return TrySetWorldRotation(shear,leaf,{0,0,0});});Rejected(shear,[&]{return TrySetWorldScale(shear,s,{0,1,1});});
 Check(TrySetWorldPosition(shear,leaf,{7,2,3}),"position write preserves affine linear part");auto moved=*ReadWorldMatrix(shear,leaf);for(int i=0;i<12;++i)Near(moved[i],world[i]);Near(moved[12],7);Near(moved[13],2);
 auto invalid=moved;invalid[0]=std::numeric_limits<double>::infinity();Rejected(shear,[&]{return TrySetWorldMatrix(shear,leaf,invalid);});
 World singular(World::DataMode::HierarchyData);auto zero=Object(singular,"zero"),x=Object(singular,"x");singular.GetComponent<TransformComponent>(zero)->scale={0,1,1};Rejected(singular,[&]{return SetParent(singular,x,zero);});
}
struct Fixture {
 std::string key="scripts/hierarchy-"+NewSceneIdentity()+"/test.lua";std::filesystem::path script=scripting::SafeAsset(key,".lua");
 Fixture(){Check(std::filesystem::create_directory(script.parent_path()),"unique owned fixture");}
 ~Fixture(){std::error_code error;std::filesystem::remove_all(script.parent_path(),error);}
 void Write(const char* text){std::ofstream out(script);out<<text;Check(bool(out),"write fixture");}
};
static void Persistence(){
 Fixture files;World w(World::DataMode::HierarchyData);auto p=Object(w,"parent",{3,1,0}),c=Object(w,"child",{5,2,0}),g=Object(w,"grandchild",{6,2,0}),external=Object(w,"outside");
 Check(SetParent(w,c,p)&&SetParent(w,g,c),"persist fixture");auto& lua=w.AddComponent<LuaScriptComponent>(c);lua.properties={{"target",{{"$entity","grandchild"}}},{"external",{{"$entity","outside"}}},{"text","parent"}};
 Check(TrySetWorldScale(w,p,{2,1,1}),"serialized shear parent");w.GetComponent<TransformComponent>(c)->rotation.z=.7853981633974483f;
 w.AddComponent<DirectionalLightComponent>(p);w.AddComponent<DirectionalLightComponent>(external);
 auto document=SceneSerializer::CaptureWorld(w);Check(document["version"]==4,"schema stays v4");
 World loaded(World::DataMode::HierarchyData);SceneSettings settings;std::string error;
 std::mt19937 random(42);std::shuffle(document["entities"].begin(),document["entities"].end(),random);
 Check(SceneSerializer::RestoreWorld(document,loaded,settings,&error),"two-pass arbitrary order load");Check(Canonical(SceneSerializer::CaptureWorld(loaded,settings))==Canonical(document),"local Transform/relations/Lua properties preserved");MatrixNear(*ReadWorldMatrix(w,c),*ReadWorldMatrix(loaded,Find(loaded,"child")));
 auto path=files.script.parent_path()/"scene.json";Check(SceneSerializer::SaveWorld(path,w,&error)&&SceneSerializer::LoadWorld(path,loaded,settings,&error),"actual disk Save/Load");
 auto bad=[&](auto change){auto broken=document;change(broken);const auto before=Raw(loaded);auto oldSettings=SceneSettingsToJson(settings);Check(!SceneSerializer::RestoreWorld(broken,loaded,settings,&error),"invalid graph load rejected");Check(Raw(loaded)==before&&SceneSettingsToJson(settings)==oldSettings,"invalid scene rollback includes settings and World identity");};
 auto node=[](Json& doc,const char* id)->Json&{for(auto& item:doc["entities"])if(item["id"]==id)return item;throw std::runtime_error("missing fixture node");};
 bad([&](auto& d){node(d,"child")["components"]["Relationship"]["parent"]="missing";});
 bad([&](auto& d){node(d,"parent")["components"]["Relationship"]={{"parent","grandchild"},{"order",0}};});
 bad([&](auto& d){d["entities"][0]["id"]=d["entities"][1]["id"];});
 for(Json order:{Json(-1),Json(1.5),Json("one"),Json(true),Json(std::numeric_limits<std::uint64_t>::max())})bad([&](auto& d){node(d,"child")["components"]["Relationship"]["order"]=order;});
 bad([&](auto& d){node(d,"child")["components"]["Relationship"]["extra"]=true;});bad([&](auto& d){node(d,"child")["components"]["Relationship"]["parent"]=4;});
 bad([&](auto& d){node(d,"parent")["components"]["Transform"]["scale"]={0,1,1};});
 bad([&](auto& d){node(d,"child")["components"].erase("Transform");});
 World rootOnly;Object(rootOnly,"safe");Rejected(rootOnly,[&]{return SceneSerializer::RestoreWorld(document,rootOnly,settings,&error);});Rejected(rootOnly,[&]{return SceneSerializer::LoadWorld(path,rootOnly,settings,&error);});
 std::vector<EcsDemoEntityConfig> configs;Check(!SceneSerializer::LoadEntityConfigs(path,configs,&error),"legacy flatten path explicitly rejects hierarchy");
 World::CloneMap map;auto clone=w.CloneData(&map);Check(clone->IsHierarchyData()&&!ReadWorldMatrix(*clone,c),"clone mode and foreign handle fence");MatrixNear(*ReadWorldMatrix(*clone,map.at(c)),*ReadWorldMatrix(w,c));Check(GetParent(*clone,map.at(c))==map.at(p),"clone IDs resolve within destination World");
 ThrowsUnchanged(rootOnly,[&]{rootOnly.ReplaceData(*clone);});auto old=Find(loaded,"child");loaded.ReplaceData(*clone);Check(!ReadWorldMatrix(loaded,old)&&Find(loaded,"child").IsValid(),"ReplaceData invalidates handles retains IDs");
 auto batch=Json::array();auto plan=PrepareHierarchyClone(w,{p,c},&error);Check(plan&&plan->sources.size()==3,"unique subtree clone plan");
 for(auto source:plan->sources){auto data=ComponentRegistry::Builtins().Capture(w,source);auto id=data["SceneIdentity"]["value"].get<std::string>();data.erase("SceneIdentity");if(auto* relation=w.GetComponent<RelationshipComponent>(source)){auto remapped=RemapRelationship(*relation,plan->identities);data["Relationship"]={{"parent",remapped.parent?Json(*remapped.parent):Json(nullptr)},{"order",remapped.order}};}batch.push_back({{"id",plan->identities.at(id)},{"components",data}});}
 std::reverse(batch.begin(),batch.end());auto before=SceneSerializer::CaptureWorld(w);Check(ComponentRegistry::Builtins().RestoreBatch(w,batch,&error),"atomic related batch restore arbitrary order");auto newChild=Find(w,plan->identities.at("child")),newParent=Find(w,plan->identities.at("parent"));Check(GetParent(w,newChild)==newParent,"parent references remap to cloned IDs");Check(w.GetComponent<LuaScriptComponent>(newChild)->properties["external"]["$entity"]=="outside","external typed references preserved");
 auto externalPlan=PrepareHierarchyClone(w,{newChild},&error);auto relation=RemapRelationship(*w.GetComponent<RelationshipComponent>(newChild),externalPlan->identities);Check(relation.parent==plan->identities.at("parent"),"parent outside clone closure remains external");
 auto invalidBatch=batch;invalidBatch[0]["components"]["Relationship"]={{"parent","missing"},{"order",0}};Rejected(w,[&]{return ComponentRegistry::Builtins().RestoreBatch(w,invalidBatch,&error);});
 auto after=SceneSerializer::CaptureWorld(w);for(int i=0;i<3;++i){Check(SceneSerializer::RestoreWorld(before,w,settings,&error),"transaction before graph restore");Check(w.GetAliveCount()==4,"undo removes complete cloned set");Check(SceneSerializer::RestoreWorld(after,w,settings,&error),"transaction after graph restore");Check(Canonical(SceneSerializer::CaptureWorld(w,settings))==Canonical(after),"redo stable IDs no partial graph");}
 World flat;auto flatObject=Object(flat,"flat",{4,5,6});flat.GetComponent<TransformComponent>(flatObject)->scale={0,-2,3};auto legacy=SceneSerializer::CaptureWorld(flat);Check(SceneSerializer::RestoreWorld(legacy,rootOnly,settings,&error),"old flat v4 zero/negative scale compatible");Check(SceneSerializer::CaptureWorld(rootOnly,settings)==legacy,"flat numerical serialization unchanged");
}
static void Lifecycle(){
 World w(World::DataMode::HierarchyData);auto p=Object(w,"p"),c=Object(w,"c"),g=Object(w,"g"),survivor=Object(w,"outside");Check(SetParent(w,c,p)&&SetParent(w,g,c),"delete fixture");
 MutationQueue queue;w.ForEach<TransformComponent>([&](Entity e,auto&){if(e==p){queue.Destroy(p);queue.Destroy(c);}Check(w.IsAlive(c)&&w.IsAlive(g),"deferred closure remains alive during iteration");});
 queue.Apply(w);Check(!w.IsAlive(p)&&!w.IsAlive(c)&&!w.IsAlive(g)&&w.IsAlive(survivor),"data-only deferred subtree deletion unique and complete");auto recycled=Object(w,"replacement");Check(recycled.index==p.index&&recycled.generation!=p.generation,"deleted root generation reused safely");Rejected(w,[&]{return SetParent(w,c,recycled);});Check(!w.DestroyEntity(p),"stale destroy does not delete replacement");
 queue.Destroy(recycled);World replacement(World::DataMode::HierarchyData);Object(replacement,"new");w.ReplaceData(replacement);queue.Apply(w);Check(w.GetAliveCount()==1&&Find(w,"new").IsValid(),"old World deferred command fenced");
 World markers(World::DataMode::HierarchyData);auto marker=Object(markers,"marker"),child=Object(markers,"game child");markers.AddComponent<PlayerStartComponent>(marker);Check(SetParent(markers,child,marker),"marker has data child");ThrowsUnchanged(markers,[&]{SpawnPlayer(markers);});Check(markers.IsAlive(child),"runtime marker cleanup cannot cascade data hierarchy");
 ThrowsUnchanged(markers,[&]{markers.UpdateSystems(0);});ThrowsUnchanged(markers,[&]{BorrowRootWorldTransform(markers,child);});
 Fixture files;files.Write("return {on_start=function(self) end}");World gameplay;auto alive=Object(gameplay,"alive");gameplay.AddComponent<LuaScriptComponent>(alive).scriptPath=files.key;scripting::LuaRuntime lua;lua.Start(gameplay);lua.Update(0);Check(lua.InstanceCount()==1,"root-only Lua remains available");ThrowsUnchanged(markers,[&]{lua.Start(markers);});Check(lua.InstanceCount()==1&&gameplay.IsAlive(alive),"rejected hierarchy Start preserves existing runtime");lua.Stop();
 files.Write("return {fields={target={type='entity'}},on_start=function(self) World.destroy(self.entity) end,on_destroy=function(self) World.set_position(self.target,Vec3.new(77,0,0)) end}");
 auto target=Object(gameplay,"target");gameplay.GetComponent<LuaScriptComponent>(alive)->properties={{"target",{{"$entity","target"}}}};lua.Start(gameplay);lua.Update(0);Check(!gameplay.IsAlive(alive)&&ReadWorldPosition(gameplay,target)->x==77&&lua.InstanceCount()==0,"existing protected Lua destroy lifecycle before deferred deletion");lua.Stop();
}
int main(int argc,char** argv){ScopedEditorPreferenceRestore preferences;try{std::string mode=argc>1?argv[1]:"all";if(mode=="all"||mode=="--graph")Graph();if(mode=="all"||mode=="--transform")Transform();if(mode=="all"||mode=="--persistence")Persistence();if(mode=="all"||mode=="--lifecycle")Lifecycle();std::cout<<"ECS hierarchy "<<mode<<": "<<checks<<" checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
