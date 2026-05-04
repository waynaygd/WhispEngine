#include "ResourceManager.h"

#include <chrono>

ResourceManager::ResourceManager()
{
    m_DefaultMesh = CreateDefaultResource<MeshResource>("defaults/mesh", MeshLoader::CreateDefault());
    m_DefaultTexture = CreateDefaultResource<TextureResource>("defaults/texture", TextureLoader::CreateDefault());
    m_DefaultShader = CreateDefaultResource<ShaderResource>("defaults/shader", ShaderLoader::CreateDefault());
    m_DefaultMaterial = CreateDefaultResource<MaterialResource>("defaults/material", MaterialLoader::CreateDefault());

    Logger::Get().Info("ResourceManager: initialized default mesh, texture, shader, and material resources");
}

void ResourceManager::ClearAll()
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    const std::size_t meshCount = m_MeshCache.size();
    const std::size_t textureCount = m_TextureCache.size();
    const std::size_t shaderCount = m_ShaderCache.size();
    const std::size_t materialCount = m_MaterialCache.size();

    m_MeshCache.clear();
    m_TextureCache.clear();
    m_ShaderCache.clear();
    m_MaterialCache.clear();
    m_HotReloadWatches.clear();
    m_PendingAsyncKeys.clear();

    Logger::Get().Info(
        "ResourceManager: cleared all caches mesh=" + std::to_string(meshCount) +
        " texture=" + std::to_string(textureCount) +
        " shader=" + std::to_string(shaderCount) +
        " material=" + std::to_string(materialCount));
}

void ResourceManager::PollAsyncLoads()
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);

    auto it = m_AsyncTasks.begin();
    while (it != m_AsyncTasks.end())
    {
        if (it->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            ++it;
            continue;
        }

        it->future.get();
        it = m_AsyncTasks.erase(it);
    }
}

void ResourceManager::PollHotReload()
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);

    for (auto& [watchKey, watch] : m_HotReloadWatches)
    {
        (void)watchKey;
        if (watch.path.empty() || !std::filesystem::exists(watch.path))
            continue;

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
        case ResourceKind::Mesh:
            (void)Reload<MeshResource>(watch.key);
            break;
        case ResourceKind::Texture:
            (void)Reload<TextureResource>(watch.key);
            break;
        case ResourceKind::Shader:
            (void)Reload<ShaderResource>(watch.key);
            break;
        case ResourceKind::Material:
            (void)Reload<MaterialResource>(watch.key);
            break;
        }

        Logger::Get().Info("ResourceManager: hot reload detected key=" + watch.key);
    }
}
