#include "ResourceManager.h"
#include <set>
#include "../scripting/Prefab.h"
#include <tracy/Tracy.hpp>

ResourceManager::ResourceManager(JobSystem* jobSystem)
    : m_JobSystem(jobSystem)
{
    m_DefaultMesh = CreateDefaultResource<MeshResource>("defaults/mesh", MeshLoader::CreateDefault());
    m_DefaultEnvironment = CreateDefaultResource<EnvironmentResource>("defaults/environment", EnvironmentLoader::CreateDefault());
    m_DefaultTexture = CreateDefaultResource<TextureResource>("defaults/texture", TextureLoader::CreateDefault());
    m_DefaultShader = CreateDefaultResource<ShaderResource>("defaults/shader", ShaderLoader::CreateDefault());
    m_DefaultMaterial = CreateDefaultResource<MaterialResource>("defaults/material", MaterialLoader::CreateDefault());

    Logger::Get().Info("ResourceManager: initialized default mesh, texture, shader, and material resources");
}

ResourceManager::~ResourceManager()
{
    Shutdown();
}

void ResourceManager::ClearAll()
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    const std::size_t meshCount = m_MeshCache.size();
    const std::size_t textureCount = m_TextureCache.size();
    const std::size_t shaderCount = m_ShaderCache.size();
    const std::size_t materialCount = m_MaterialCache.size();

    scripting::ClearPrefabCache();
    m_DataCache.clear();m_FontCache.clear();m_UICache.clear();m_DirtyUI.clear();
    m_PostFxCache.clear();m_PostFxKeys.clear();m_PostFxScanned=false;
    m_MeshCache.clear();
    m_TextureCache.clear();
    m_EnvironmentCache.clear();
    m_ShaderCache.clear();
    m_MaterialCache.clear();
    m_DirtyMaterials.clear();
    m_HotReloadWatches.clear();
    m_MaterialUsers.clear();m_ManagedMaterialWatches.clear();
    m_ResolvedMaterials.clear();m_MaterialWatchHistory.clear();
    m_PendingAsyncKeys.clear();
    ++m_CacheEpoch;

    Logger::Get().Info(
        "ResourceManager: cleared all caches mesh=" + std::to_string(meshCount) +
        " texture=" + std::to_string(textureCount) +
        " shader=" + std::to_string(shaderCount) +
        " material=" + std::to_string(materialCount));
}

void ResourceManager::DrainProjectLoads()
{
    // Finalization may schedule material dependencies. Reach a fixed point before
    // publishing the next context so no worker resolves a key in the next project.
    for (;;) {
        if(m_JobSystem&&m_JobSystem->IsInitialized())m_JobSystem->WaitAll();
        PollAsyncLoads();
        const auto finalized=PumpFinalization(static_cast<std::size_t>(-1));
        if(finalized==0)break;
    }
}

void ResourceManager::PollAsyncLoads()
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);

    auto it = m_AsyncTasks.begin();

    while (it != m_AsyncTasks.end())
    {
        if (m_JobSystem != nullptr &&
            !m_JobSystem->IsComplete(it->task))
        {
            ++it;
            continue;
        }

        it = m_AsyncTasks.erase(it);
    }
}

void ResourceManager::StopWatchingAsset(const std::string& key){
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    std::erase_if(m_HotReloadWatches,[&](const auto& entry){return entry.second.key==key;});
}
void ResourceManager::SetMaterialUsage(const void* owner,const std::vector<std::string>& roots,const MaterialResource* draft){
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    if(roots.empty()&&!draft){m_MaterialUsers.erase(owner);RefreshMaterialWatches();return;}
    auto& usage=m_MaterialUsers[owner];usage.roots=roots;
    usage.draft=draft?std::optional<MaterialResource>(*draft):std::nullopt;
    RefreshMaterialWatches();
}
void ResourceManager::ClearMaterialUsage(const void* owner){
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    if(m_MaterialUsers.erase(owner))RefreshMaterialWatches();
}
void ResourceManager::RefreshMaterialWatches(){
    std::unordered_set<std::string> desired;
    const auto watch=[&](const std::string& key,ResourceKind kind){
        if(key.empty()||key.find('#')!=std::string::npos||key.starts_with("defaults/"))return;
        const auto token=key+"#"+std::to_string(int(kind));desired.insert(token);
        if(m_ManagedMaterialWatches.contains(token))return;
        try {
            const auto path=key.starts_with("@engine/")?AssetPaths::ResolveAssetPath(key):kind==ResourceKind::Shader?(key.starts_with("dx12/")||key.starts_with("vulkan/")?AssetPaths::ResolveShaderRoot()/std::filesystem::u8path(key):AssetPaths::ResolveAssetOutputPath(key)):AssetPaths::ResolveAssetOutputPath(key);
            if(path.empty())return;
            ManagedMaterialWatch w;w.file.key=key;w.file.kind=kind;w.file.path=path;
            std::error_code error;w.exists=std::filesystem::is_regular_file(path,error);
            if(w.exists){w.file.lastWriteTime=std::filesystem::last_write_time(path,error);w.file.fileSize=std::filesystem::file_size(path,error);}
            if(auto old=m_MaterialWatchHistory.find(token);old!=m_MaterialWatchHistory.end()){
                if(old->second.pending||old->second.exists!=w.exists||old->second.file.lastWriteTime!=w.file.lastWriteTime||old->second.file.fileSize!=w.file.fileSize){w.pending=true;w.changed=std::chrono::steady_clock::now();}
                m_MaterialWatchHistory.erase(old);
            }
            m_ManagedMaterialWatches.emplace(token,std::move(w));
        }catch(const std::exception& e){Logger::Get().Warn("Material watch '"+key+"': "+e.what());}
    };
    const auto dependencies=[&](const MaterialResource& m){
        watch(m.shaderPath,ResourceKind::Shader);
        for(const auto* key:{&m.texturePath,&m.normalTexturePath,&m.metallicRoughnessTexturePath,&m.occlusionTexturePath})watch(*key,ResourceKind::Texture);
    };
    for(const auto& [owner,usage]:m_MaterialUsers){
        for(const auto& raw:usage.roots){
            const auto key=AssetPaths::NormalizeAssetKey(raw);watch(key,ResourceKind::Material);
            auto material=FindCachedResource<MaterialResource>(key);
            if(material&&material->IsUsable()&&!material->IsLoading()){
                const auto resolved=ResolveMaterial(key,usage.draft?&*usage.draft:nullptr);dependencies(resolved.data);
                std::unordered_set<std::string> visited;auto parent=usage.draft?usage.draft->parentMaterial:material->GetData().parentMaterial;
                while(!parent.empty()&&visited.size()<32&&visited.insert(parent).second){watch(parent,ResourceKind::Material);auto p=FindCachedResource<MaterialResource>(parent);if(!p)break;parent=p->GetData().parentMaterial;}
            }
        }
        if(usage.draft&&usage.roots.empty())dependencies(*usage.draft);
    }
    std::erase_if(m_ManagedMaterialWatches,[&](const auto& item){if(desired.contains(item.first))return false;m_MaterialWatchHistory[item.first]=item.second;return true;});
}
ResourceLoadResult<MaterialResource> ResourceManager::ResolveMaterial(const std::string& rawKey,const MaterialResource* draft){
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    std::vector<std::string> chain;
    const auto failure=[](std::string error){ResourceLoadResult<MaterialResource> r;r.errorMessage=std::move(error);return r;};
    auto resolve=[&](auto&& self,const std::string& key,const MaterialResource* authored)->ResourceLoadResult<MaterialResource>{
        if(chain.size()>=32)return failure("Material inheritance exceeds maximum depth 32: "+key);
        if(auto cycle=std::find(chain.begin(),chain.end(),key);cycle!=chain.end()){
            std::set<std::string> members(cycle,chain.end());std::string message="Material inheritance cycle:";for(const auto& member:members)message+=" "+member;return failure(message);
        }
        chain.push_back(key);
        auto raw=FindCachedResource<MaterialResource>(key);if(!raw)raw=Load<MaterialResource>(key);
        ResourceLoadResult<MaterialResource> next;std::uint64_t parentGeneration=0;
        const auto& data=authored?*authored:raw->GetData();
        if(authored&&!data.parentMaterial.empty()){
            const auto validated=MaterialLoader::Parse(key,MaterialLoader::Serialize(data));
            if(!validated.success){chain.pop_back();return validated;}
        }
        if(!authored&&(!raw->IsLoaded()||data.placeholder))next=failure("Material '"+key+"': "+raw->GetErrorMessage());
        else if(data.parentMaterial.empty()){next.success=true;next.data=data;}
        else {
            auto parent=self(self,data.parentMaterial,nullptr);
            if(auto p=m_ResolvedMaterials.find(data.parentMaterial);p!=m_ResolvedMaterials.end())parentGeneration=p->second.generation;
            if(!parent.success)next=failure(parent.errorMessage);
            else if(authored)next=MaterialLoader::Merge(parent.data,data);
            else {
                auto found=m_ResolvedMaterials.find(key);
                if(found!=m_ResolvedMaterials.end()&&found->second.owner.lock()==raw&&found->second.rawVersion==raw->GetVersion()&&found->second.parentGeneration==parentGeneration&&found->second.result.success){chain.pop_back();return found->second.result;}
                next=MaterialLoader::Merge(parent.data,data);
            }
        }
        chain.pop_back();
        if(draft)return next; // An invalid unsaved parent must never poison scene caches.
        auto& entry=m_ResolvedMaterials[key];
        if(entry.owner.lock()!=raw)entry={};
        if(entry.rawVersion!=raw->GetVersion()||(next.success&&entry.parentGeneration!=parentGeneration)||entry.result.errorMessage!=next.errorMessage){
            // Preserve last-good resolved values, independently of raw authored data.
            if(!next.success&&!entry.result.data.placeholder)next.data=entry.result.data;
            if(!next.success&&(entry.result.errorMessage!=next.errorMessage||entry.rawVersion!=raw->GetVersion()))Logger::Get().Warn(next.errorMessage+"; using last-good resolved material or default fallback");
            entry.owner=raw;entry.rawVersion=raw->GetVersion();entry.parentGeneration=parentGeneration;entry.generation=++m_MaterialGeneration;entry.result=next;
        }
        return entry.result;
    };
    return resolve(resolve,AssetPaths::NormalizeAssetKey(rawKey),draft);
}
void ResourceManager::PollMaterialWatches(){
    const auto now=std::chrono::steady_clock::now();
    for(auto& [token,w]:m_ManagedMaterialWatches){
        std::error_code error;const bool exists=std::filesystem::is_regular_file(w.file.path,error);
        if(error==std::errc::no_such_file_or_directory)error.clear();
        const auto time=exists?std::filesystem::last_write_time(w.file.path,error):std::filesystem::file_time_type{};
        const auto size=exists?std::filesystem::file_size(w.file.path,error):0;
        if(error)continue;
        if(exists!=w.exists||time!=w.file.lastWriteTime||size!=w.file.fileSize){w.exists=exists;w.file.lastWriteTime=time;w.file.fileSize=size;w.changed=now;w.pending=true;w.dirtyWarning=false;continue;}
        if(!w.pending||now-w.changed<std::chrono::milliseconds(120))continue;
        if(w.file.kind==ResourceKind::Material&&m_DirtyMaterials.contains(w.file.key)){
            if(!w.dirtyWarning)Logger::Get().Warn("Material external reload pending; unsaved draft preserved: "+w.file.key);
            w.dirtyWarning=true;continue;
        }
        w.pending=false;
        if(w.file.kind==ResourceKind::Material)Reload<MaterialResource>(w.file.key);
        else if(w.file.kind==ResourceKind::Shader)Reload<ShaderResource>(w.file.key);
        else Reload<TextureResource>(w.file.key);
        if(auto manual=m_HotReloadWatches.find(token);manual!=m_HotReloadWatches.end()){manual->second.lastWriteTime=time;manual->second.fileSize=size;}
        Logger::Get().Info("Material dependency hot reload: "+w.file.key);
    }
}
void ResourceManager::PollHotReload()
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    scripting::PollPrefabReload();
    RefreshMaterialWatches();PollMaterialWatches();
    std::vector<std::string> changedPostFx;
    const auto now=std::chrono::steady_clock::now();const bool checkFonts=now>=m_NextFontWatch;if(checkFonts)m_NextFontWatch=now+std::chrono::milliseconds(250);

    for (auto& [watchKey, watch] : m_HotReloadWatches)
    {
        (void)watchKey;
        if(m_ManagedMaterialWatches.contains(watchKey))continue;
        if(watch.kind==ResourceKind::Font){if(!checkFonts)continue;++m_FontWatchChecks;}
        if (watch.path.empty() || !std::filesystem::exists(watch.path)){
            if(watch.kind==ResourceKind::Data&&watch.fileSize>0){watch.fileSize=0;(void)Reload<DataResource>(watch.key);}
            if(watch.kind==ResourceKind::Font&&watch.fileSize>0){watch.fileSize=0;(void)Reload<FontResource>(watch.key);}
            if(watch.kind==ResourceKind::UI&&watch.fileSize>0){watch.fileSize=0;(void)Reload<UIResource>(watch.key);}
            if(watch.kind==ResourceKind::PostFx&&watch.fileSize>0){watch.fileSize=0;(void)Reload<PostFxResource>(watch.key);}
            continue;
        }

        const auto currentWriteTime = std::filesystem::last_write_time(watch.path);
        const std::uintmax_t currentFileSize =
            std::filesystem::is_regular_file(watch.path)
                ? std::filesystem::file_size(watch.path)
                : 0;
        if (currentWriteTime == watch.lastWriteTime && currentFileSize == watch.fileSize)
            continue;

        watch.lastWriteTime = currentWriteTime;
        watch.fileSize = currentFileSize;
        switch (watch.kind)
        {
        case ResourceKind::Data:(void)Reload<DataResource>(watch.key);break;
        case ResourceKind::Font:(void)Reload<FontResource>(watch.key);break;
        case ResourceKind::UI:(void)Reload<UIResource>(watch.key);break;
        case ResourceKind::PostFx:(void)Reload<PostFxResource>(watch.key);changedPostFx.push_back(watch.key);break;
        case ResourceKind::Mesh:
            (void)Reload<MeshResource>(watch.key);
            break;
        case ResourceKind::Texture:
            (void)Reload<TextureResource>(watch.key);
            break;
        case ResourceKind::Shader:
            (void)Reload<ShaderResource>(watch.key);
            break;
        case ResourceKind::Environment:
            ReloadEnvironmentAsync(watch.key);
            break;
        case ResourceKind::Material:
            (void)Reload<MaterialResource>(watch.key);
            break;
        }

        Logger::Get().Info("ResourceManager: hot reload detected key=" + watch.key);
    }
    // Dependency paths can change in a manifest. Refresh after iteration so adding
    // the new shader watch never invalidates the watch-map traversal.
    for(const auto& key:changedPostFx)WatchForHotReload<PostFxResource>(key);
}

std::size_t ResourceManager::PumpFinalization(std::size_t maxItems)
{
    ZoneScopedN("ResourceFinalizePump");

    std::size_t processed = 0;

    while (processed < maxItems)
    {
        std::function<void()> finalizer;

        {
            std::lock_guard<std::mutex> lock(m_FinalizationMutex);

            if (m_FinalizationQueue.empty())
                break;

            finalizer = std::move(m_FinalizationQueue.front());
            m_FinalizationQueue.pop_front();
        }

        if (finalizer)
            finalizer();

        ++processed;
    }

    return processed;
}

void ResourceManager::Shutdown()
{
    {
        std::lock_guard<std::recursive_mutex> lock(m_Mutex);

        if (!m_AcceptAsyncLoads)
            return;

        m_AcceptAsyncLoads = false;
    }

    if (m_JobSystem != nullptr &&
        m_JobSystem->IsInitialized())
    {
        m_JobSystem->WaitAll();
    }

    for (;;)
    {
        std::size_t pendingCount = 0;

        {
            std::lock_guard<std::mutex> lock(
                m_FinalizationMutex);

            pendingCount =
                m_FinalizationQueue.size();
        }

        if (pendingCount == 0)
            break;

        PumpFinalization(pendingCount);
    }

    m_AsyncTasks.clear();
    m_PendingAsyncKeys.clear();

    Logger::Get().Info(
        "ResourceManager: async shutdown complete");
}

const std::vector<std::string>& ResourceManager::PostFxAssets(bool refresh){
    if(m_PostFxScanned&&!refresh)return m_PostFxKeys;m_PostFxScanned=true;m_PostFxKeys.clear();
    std::error_code error;auto root=AssetPaths::ResolveAssetRoot();
    for(std::filesystem::recursive_directory_iterator it(root,std::filesystem::directory_options::skip_permission_denied,error),end;it!=end;it.increment(error)){
        if(error){error.clear();continue;}if(it->is_symlink(error)){if(it->is_directory(error))it.disable_recursion_pending();continue;}
        auto key=AssetPaths::ToUtf8String(it->path().lexically_relative(root));std::replace(key.begin(),key.end(),char(92),char(47));if(it->is_regular_file(error)&&key.ends_with(".postfx.json")){m_PostFxKeys.push_back(key);if(!Get<PostFxResource>(key))Load<PostFxResource>(key);WatchForHotReload<PostFxResource>(key);}
    }
    std::sort(m_PostFxKeys.begin(),m_PostFxKeys.end());std::map<std::string,unsigned> ids;for(auto& key:m_PostFxKeys){auto r=Get<PostFxResource>(key);if(r&&r->GetData().definition)++ids[r->GetData().definition->id];}for(auto& [id,count]:ids)if(count>1)Logger::Get().Error("Duplicate Custom PostFX effect ID: "+id);return m_PostFxKeys;
}
bool ResourceManager::PostFxDuplicate(const std::string& id){unsigned count=0;for(auto& key:PostFxAssets()){auto r=Get<PostFxResource>(key);if(r&&r->GetData().definition&&r->GetData().definition->id==id)++count;}return count>1;}
std::string ResourceManager::FindPostFx(const std::string& id){std::string found;for(auto& key:PostFxAssets()){auto r=Get<PostFxResource>(key);if(r&&r->GetData().definition&&r->GetData().definition->id==id){if(!found.empty())return {};found=key;}}return found;}
void ResourceManager::ReloadPostFx(const std::string& key){Reload<PostFxResource>(key);WatchForHotReload<PostFxResource>(key);}
