#include "ResourceManager.h"
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
    m_FontCache.clear();m_UICache.clear();m_DirtyUI.clear();
    m_PostFxCache.clear();m_PostFxKeys.clear();m_PostFxScanned=false;
    m_MeshCache.clear();
    m_TextureCache.clear();
    m_EnvironmentCache.clear();
    m_ShaderCache.clear();
    m_MaterialCache.clear();
    m_DirtyMaterials.clear();
    m_HotReloadWatches.clear();
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
void ResourceManager::PollHotReload()
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    scripting::PollPrefabReload();
    std::vector<std::string> changedPostFx;
    const auto now=std::chrono::steady_clock::now();const bool checkFonts=now>=m_NextFontWatch;if(checkFonts)m_NextFontWatch=now+std::chrono::milliseconds(250);

    for (auto& [watchKey, watch] : m_HotReloadWatches)
    {
        (void)watchKey;
        if(watch.kind==ResourceKind::Font){if(!checkFonts)continue;++m_FontWatchChecks;}
        if (watch.path.empty() || !std::filesystem::exists(watch.path)){
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
