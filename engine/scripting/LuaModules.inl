// Included only inside LuaRuntime::Impl. Uses the existing VM and safe lifecycle.
std::string Stamp(const std::string& key) {
  try { auto file=SafeAsset(key,".lua");return std::to_string(std::filesystem::last_write_time(file).time_since_epoch().count())+":"+std::to_string(std::filesystem::file_size(file)); }
  catch(...) {return "missing";}
}
std::string Source(const std::string& key) {
  watches[key]=Stamp(key);
  auto file=SafeAsset(key,".lua");
  if(!std::filesystem::is_regular_file(file))throw std::runtime_error("Lua source is not a regular file: "+key);
  if(std::filesystem::file_size(file)>256*1024)throw std::runtime_error("Script exceeds 256 KiB: "+key);
  std::ifstream in(file,std::ios::binary);if(!in)throw std::runtime_error("Cannot read Lua source: "+key);
  // Bound the read itself too, in case an external writer grows the file.
  std::string text(256*1024+1, char(0));in.read(text.data(),static_cast<std::streamsize>(text.size()));text.resize(static_cast<std::size_t>(in.gcount()));
  if(in.bad())throw std::runtime_error("Cannot read complete Lua source: "+key);
  if(text.size()>256*1024)throw std::runtime_error("Script exceeds 256 KiB: "+key);
  return text;
}
Module CompileModule(const std::string& key) {
  auto text=Source(key);
  if(auto i=codeCache.find(key);i!=codeCache.end()&&i->second.source==text)return i->second;
  auto loaded=lua.load(text,"@"+key,sol::load_mode::text);
  if(!loaded.valid()){sol::error error=loaded;throw std::runtime_error(error.what());}
  sol::protected_function fn=loaded;fn.push();std::string bytes;
  auto writer=[](lua_State*,const void* data,size_t size,void* out)->int {static_cast<std::string*>(out)->append(static_cast<const char*>(data),size);return 0;};
  int result=lua_dump(lua.lua_state(),writer,&bytes,0);lua_pop(lua.lua_state(),1);
  if(result)throw std::runtime_error("Cannot cache Lua module: "+key);
  if(codeCache.size()>=128)codeCache.clear();
  return codeCache[key]=Module{std::move(text),std::move(bytes)};
}
sol::table Import(const std::string& name,const std::shared_ptr<ModuleContext>& context) {
  auto key=LuaModuleKey(name);
  if(auto i=context->values.find(key);i!=context->values.end())return i->second;
  if(context->stack.size()>=16||std::find(context->stack.begin(),context->stack.end(),key)!=context->stack.end())
    throw std::runtime_error("Cyclic/deep module import: "+key);
  context->stack.push_back(key);
  try {
    const auto& available=context->snapshot?*context->snapshot:context->modules;
    if(!available.contains(key)){
      if(!context->collecting)throw std::runtime_error("Import must be declared at script/module top level: "+key);
      if(context->modules.size()>=64)throw std::runtime_error("Module graph exceeds 64 files");
      auto module=CompileModule(key);context->bytes+=module.source.size();
      if(context->bytes>2*1024*1024)throw std::runtime_error("Module graph exceeds 2 MiB");
      context->modules.emplace(key,std::move(module));
    }
    auto value=Evaluate(available.at(key).bytecode,key,context->env,context,true);
    context->values.emplace(key,value);context->stack.pop_back();return value;
  } catch(const std::exception& error){
    std::string chain;for(const auto& entry:context->stack)chain+=entry+" -> ";
    context->stack.pop_back();throw std::runtime_error(chain+error.what());
  }
}
void InstallImports(sol::environment& env,const std::shared_ptr<ModuleContext>& context) {
  if(&context->env!=&env)context->env=env;auto api=lua.create_table();std::weak_ptr<ModuleContext> weak=context;
  api.set_function("require",[this,weak](const std::string& name){
    auto context=weak.lock();if(!context)throw std::runtime_error("Module instance expired");
    return Import(name,context);
  });env["Script"]=api;
}
Program Candidate(const std::string& path) {
  rootDependencies[path].insert(path);
  Program candidate;candidate.source=Source(path);
  Impl validation;validation.codeCache=codeCache;
  auto context=std::make_shared<ModuleContext>();context->collecting=true;context->bytes=candidate.source.size();
  auto env=validation.Environment();context->stack.push_back(path);
  try {candidate.fields=Schema(validation.Evaluate(candidate.source,path,env,context));}
  catch(...) {for(auto& [key,stamp]:validation.watches){watches[key]=stamp;rootDependencies[path].insert(key);}throw;}
  for(auto& [key,stamp]:validation.watches)watches[key]=stamp;
  rootDependencies[path]={path};for(const auto& [key,stamp]:validation.watches)rootDependencies[path].insert(key);
  codeCache=std::move(validation.codeCache);candidate.modules=std::make_shared<const std::map<std::string,Module>>(std::move(context->modules));
  candidate.revision=programs.contains(path)?programs.at(path).revision+1:1;
  return candidate;
}
bool ReloadRoots(std::vector<std::string> roots) {
  std::set<std::string> expanded(roots.begin(),roots.end());bool added=true;
  while(added){added=false;for(const auto& group:failedGroups)
    if(std::any_of(group.begin(),group.end(),[&](const auto& root){return expanded.contains(root);}))
      for(const auto& root:group)added|=expanded.insert(root).second;}
  roots.assign(expanded.begin(),expanded.end());
  const auto previousDependencies=rootDependencies;
  std::map<std::string,Program> candidates;
  std::map<std::string,std::string> transactionStamps;
  bool sourceRaced=false;
  try {
    for(const auto& root:roots){
      auto candidate=Candidate(root);
      // Do not retire a live actor if a changed module schema rejects its saved overrides.
      for(const auto& [entity,instance]:instances)if(instance.path==root&&Alive(entity)){
        auto* component=world->GetComponent<ecs::LuaScriptComponent>(entity);
        if(!component||!component->enabled||component->scriptPath!=root)continue;
        const auto& overrides=component->properties;
        for(auto field=candidate.fields.begin();field!=candidate.fields.end();++field){
          const auto value=overrides.contains(field.key())?overrides.at(field.key()):field.value().at("default");
          if(!ValidValue(field.value(),value))throw std::runtime_error("Reload would invalidate override "+field.key()+" in "+root);
        }
      }

      auto record=[&](const std::string& key){
        auto stamp=watches.at(key);auto [at,inserted]=transactionStamps.emplace(key,stamp);
        if(!inserted&&at->second!=stamp){sourceRaced=true;throw std::runtime_error("Lua source changed during reload: "+key);}
      };
      record(root);for(const auto& [key,module]:*candidate.modules)record(key);
      candidates.emplace(root,std::move(candidate));
    }
    for(const auto& [key,stamp]:transactionStamps)if(Stamp(key)!=stamp){sourceRaced=true;throw std::runtime_error("Lua source changed during reload: "+key);}
  }
  catch(const std::exception& error){
    // Preserve old live graph edges, plus failed/new imports for recovery watches.
    for(const auto& [root,deps]:previousDependencies)rootDependencies[root].insert(deps.begin(),deps.end());
    std::erase_if(failedGroups,[&](const auto& group){return std::any_of(group.begin(),group.end(),[&](const auto& root){return expanded.contains(root);});});
    failedGroups.push_back(expanded);
    if(sourceRaced)for(const auto& [key,stamp]:transactionStamps)watches[key].clear();
    for(const auto& root:roots)Fail(root,"load",error.what());return false;
  }
  std::erase_if(failedGroups,[&](const auto& group){return std::any_of(group.begin(),group.end(),[&](const auto& root){return expanded.contains(root);});});
  for(auto& [root,candidate]:candidates){programs[root]=std::move(candidate);errors.erase(root);loadFailures.erase(root);}
  return true;
}
bool ReloadGraph(const std::string& changed) {
  Check();std::vector<std::string> roots;
  for(const auto& [root,deps]:rootDependencies)if(deps.contains(changed))roots.push_back(root);
  if(roots.empty())roots.push_back(changed);
  return ReloadRoots(roots);
}
void PollModules(float dt) {
  pollTime+=dt;if(pollTime<.5)return;pollTime=0;std::set<std::string> changed;
  for(auto& [key,stamp]:watches){auto now=Stamp(key);if(now!=stamp){stamp=std::move(now);changed.insert(key);}}
  std::vector<std::string> roots;
  for(const auto& [root,deps]:rootDependencies)if(std::any_of(changed.begin(),changed.end(),[&](const auto& key){return deps.contains(key);}))roots.push_back(root);
  // Bounded polling, never execute callbacks from a filesystem watcher thread.
  if(!roots.empty())ReloadRoots(roots);
}
