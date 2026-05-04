#pragma once

#include "MaterialResource.h"
#include "MeshResource.h"
#include "Resource.h"
#include "ShaderResource.h"
#include "TextureResource.h"
#include "loaders/MaterialLoader.h"
#include "loaders/MeshLoader.h"
#include "loaders/ShaderLoader.h"
#include "loaders/TextureLoader.h"
#include "../core/AssetPaths.h"
#include "../core/Logger.h"

#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <cstdint>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

template <typename T>
inline constexpr bool kUnsupportedResourceType = false;

class ResourceManager
{
public:
    ResourceManager();
    ~ResourceManager() = default;

    template <typename T>
    ResourceHandle<T> Load(const std::filesystem::path& path)
    {
        std::lock_guard<std::recursive_mutex> lock(m_Mutex);
        const std::string key = NormalizeKey<T>(path);
        if (key.empty())
        {
            const std::string error = "requested path does not map to a normalized runtime resource key";
            Logger::Get().Warn(
                "ResourceManager: invalid request [" + ResourceTypeName<T>() + "] -> using default. " + error);
            return CreateFallbackResource<T>("<invalid>", error);
        }

        if (auto cached = FindCachedResource<T>(key))
        {
            Logger::Get().Info("ResourceManager: cache hit [" + ResourceTypeName<T>() + "] key=" + key);
            return cached;
        }

        const ResourceLoadResult<T> result = InvokeLoader<T>(key);
        if (result.success)
        {
            auto resource = std::make_shared<Resource<T>>(
                key,
                std::move(result.data),
                ResourceLoadState::Loaded,
                false);

            GetCache<T>()[key] = resource;
            Logger::Get().Info("ResourceManager: loaded [" + ResourceTypeName<T>() + "] key=" + key);
            return resource;
        }

        auto fallback = CreateFallbackResource<T>(key, result.errorMessage);
        GetCache<T>()[key] = fallback;
        Logger::Get().Warn(
            "ResourceManager: load failed [" + ResourceTypeName<T>() + "] key=" + key +
            " -> using default. " + result.errorMessage);
        return fallback;
    }

    template <typename T>
    std::future<ResourceHandle<T>> LoadAsync(const std::filesystem::path& path)
    {
        std::lock_guard<std::recursive_mutex> lock(m_Mutex);
        const std::string key = NormalizeKey<T>(path);
        if (key.empty())
        {
            return MakeReadyFuture(
                CreateFallbackResource<T>("<invalid>", "async request path does not map to a normalized runtime resource key"));
        }

        if (auto cached = FindCachedResource<T>(key))
        {
            if (!cached->IsLoading())
                Logger::Get().Info("ResourceManager: async cache hit [" + ResourceTypeName<T>() + "] key=" + key);
            return MakeReadyFuture(cached);
        }

        auto resource = CreateLoadingResource<T>(key, "async load in progress");
        GetCache<T>()[key] = resource;
        StartAsyncLoad<T>(key, resource);
        Logger::Get().Info("ResourceManager: async load scheduled [" + ResourceTypeName<T>() + "] key=" + key);
        return MakeReadyFuture(resource);
    }

    template <typename T>
    ResourceHandle<T> Get(const std::filesystem::path& path)
    {
        std::lock_guard<std::recursive_mutex> lock(m_Mutex);
        const std::string key = NormalizeKey<T>(path);
        if (key.empty())
            return nullptr;

        return FindCachedResource<T>(key);
    }

    template <typename T>
    bool Has(const std::filesystem::path& path)
    {
        return Get<T>(path) != nullptr;
    }

    template <typename T>
    void Unload(const std::filesystem::path& path)
    {
        std::lock_guard<std::recursive_mutex> lock(m_Mutex);
        const std::string key = NormalizeKey<T>(path);
        if (key.empty())
            return;

        auto& cache = GetCache<T>();
        const std::size_t removed = cache.erase(key);
        if (removed > 0)
        {
            Logger::Get().Info("ResourceManager: unloaded [" + ResourceTypeName<T>() + "] key=" + key);
        }
    }

    template <typename T>
    void Clear()
    {
        std::lock_guard<std::recursive_mutex> lock(m_Mutex);
        auto& cache = GetCache<T>();
        const std::size_t count = cache.size();
        cache.clear();
        Logger::Get().Info(
            "ResourceManager: cleared [" + ResourceTypeName<T>() + "] cache entries=" + std::to_string(count));
    }

    void ClearAll();

    template <typename T>
    ResourceHandle<T> Reload(const std::filesystem::path& path)
    {
        std::lock_guard<std::recursive_mutex> lock(m_Mutex);
        const std::string key = NormalizeKey<T>(path);
        if (key.empty())
            return CreateFallbackResource<T>("<invalid>", "reload path does not map to a normalized key");

        const ResourceLoadResult<T> result = InvokeLoader<T>(key);
        auto& cache = GetCache<T>();
        auto existing = FindCachedResource<T>(key);
        if (result.success)
        {
            if (existing)
            {
                existing->ReplaceData(std::move(result.data), ResourceLoadState::Loaded, false);
                Logger::Get().Info("ResourceManager: reloaded [" + ResourceTypeName<T>() + "] key=" + key);
                return existing;
            }

            auto resource = std::make_shared<Resource<T>>(key, std::move(result.data), ResourceLoadState::Loaded, false);
            cache[key] = resource;
            Logger::Get().Info("ResourceManager: reloaded new [" + ResourceTypeName<T>() + "] key=" + key);
            return resource;
        }

        if (existing)
        {
            existing->ReplaceData(existing->GetData(), ResourceLoadState::Failed, true, result.errorMessage);
            Logger::Get().Warn("ResourceManager: reload failed [" + ResourceTypeName<T>() + "] key=" + key + ". " + result.errorMessage);
            return existing;
        }

        auto fallback = CreateFallbackResource<T>(key, result.errorMessage);
        cache[key] = fallback;
        return fallback;
    }

    template <typename T>
    void WatchForHotReload(const std::filesystem::path& path)
    {
        std::lock_guard<std::recursive_mutex> lock(m_Mutex);
        const std::string key = NormalizeKey<T>(path);
        if (key.empty())
            return;

        const std::filesystem::path resolvedPath = ResolvePathFromKey<T>(key);
        if (resolvedPath.empty() || !std::filesystem::exists(resolvedPath))
            return;

        HotReloadWatch watch;
        watch.key = key;
        watch.path = resolvedPath;
        watch.kind = ResourceKindFor<T>();
        watch.lastWriteTime = std::filesystem::last_write_time(resolvedPath);
        watch.fileSize =
            std::filesystem::is_regular_file(resolvedPath)
                ? std::filesystem::file_size(resolvedPath)
                : 0;
        m_HotReloadWatches[key + "#" + std::to_string(static_cast<int>(watch.kind))] = watch;
    }

    void PollHotReload();
    void PollAsyncLoads();

    template <typename T>
    ResourceHandle<T> GetDefault() const
    {
        return GetDefaultStorage<T>();
    }

private:
    template <typename T>
    using CacheMap = std::unordered_map<std::string, ResourceHandle<T>>;

    enum class ResourceKind
    {
        Mesh,
        Texture,
        Shader,
        Material
    };

    struct HotReloadWatch
    {
        std::string key;
        std::filesystem::path path;
        ResourceKind kind = ResourceKind::Mesh;
        std::filesystem::file_time_type lastWriteTime{};
        std::uintmax_t fileSize = 0;
    };

    struct AsyncTask
    {
        std::string token;
        std::future<void> future;
    };

    template <typename T>
    CacheMap<T>& GetCache()
    {
        if constexpr (std::is_same_v<T, MeshResource>)
            return m_MeshCache;
        else if constexpr (std::is_same_v<T, TextureResource>)
            return m_TextureCache;
        else if constexpr (std::is_same_v<T, ShaderResource>)
            return m_ShaderCache;
        else if constexpr (std::is_same_v<T, MaterialResource>)
            return m_MaterialCache;
        else
            static_assert(kUnsupportedResourceType<T>, "Unsupported resource type");
    }

    template <typename T>
    ResourceHandle<T> GetDefaultStorage() const
    {
        if constexpr (std::is_same_v<T, MeshResource>)
            return m_DefaultMesh;
        else if constexpr (std::is_same_v<T, TextureResource>)
            return m_DefaultTexture;
        else if constexpr (std::is_same_v<T, ShaderResource>)
            return m_DefaultShader;
        else if constexpr (std::is_same_v<T, MaterialResource>)
            return m_DefaultMaterial;
        else
            static_assert(kUnsupportedResourceType<T>, "Unsupported resource type");
    }

    template <typename T>
    std::string NormalizeKey(const std::filesystem::path& path) const
    {
        if constexpr (std::is_same_v<T, ShaderResource>)
            return AssetPaths::NormalizeShaderKey(path);
        else if constexpr (std::is_same_v<T, MeshResource> || std::is_same_v<T, TextureResource> || std::is_same_v<T, MaterialResource>)
            return AssetPaths::NormalizeAssetKey(path);
        else
            static_assert(kUnsupportedResourceType<T>, "Unsupported resource type");
    }

    template <typename T>
    std::filesystem::path ResolvePathFromKey(const std::string& key) const
    {
        if constexpr (std::is_same_v<T, ShaderResource>)
            return AssetPaths::ResolveShaderPath(key);
        else if constexpr (std::is_same_v<T, MeshResource> || std::is_same_v<T, TextureResource> || std::is_same_v<T, MaterialResource>)
            return AssetPaths::ResolveAssetPath(key);
        else
            static_assert(kUnsupportedResourceType<T>, "Unsupported resource type");
    }

    template <typename T>
    ResourceLoadResult<T> InvokeLoader(const std::string& key) const
    {
        const std::filesystem::path resolvedPath = ResolvePathFromKey<T>(key);

        if constexpr (std::is_same_v<T, MeshResource>)
            return MeshLoader::Load(key, resolvedPath);
        else if constexpr (std::is_same_v<T, TextureResource>)
            return TextureLoader::Load(key, resolvedPath);
        else if constexpr (std::is_same_v<T, ShaderResource>)
            return ShaderLoader::Load(key, resolvedPath);
        else if constexpr (std::is_same_v<T, MaterialResource>)
            return MaterialLoader::Load(key, resolvedPath);
        else
            static_assert(kUnsupportedResourceType<T>, "Unsupported resource type");
    }

    template <typename T>
    ResourceHandle<T> FindCachedResource(const std::string& key)
    {
        auto& cache = GetCache<T>();
        const auto it = cache.find(key);
        if (it == cache.end())
            return nullptr;

        return it->second;
    }

    template <typename T>
    ResourceHandle<T> CreateFallbackResource(const std::string& key, const std::string& errorMessage) const
    {
        const auto defaultResource = GetDefault<T>();
        T data = defaultResource != nullptr ? defaultResource->GetData() : T{};

        return std::make_shared<Resource<T>>(
            key,
            std::move(data),
            ResourceLoadState::Failed,
            true,
            errorMessage);
    }

    template <typename T>
    ResourceHandle<T> CreateDefaultResource(const std::string& key, T data) const
    {
        return std::make_shared<Resource<T>>(
            key,
            std::move(data),
            ResourceLoadState::Loaded,
            true);
    }

    template <typename T>
    ResourceHandle<T> CreateLoadingResource(const std::string& key, const std::string& message) const
    {
        const auto defaultResource = GetDefault<T>();
        T data = defaultResource != nullptr ? defaultResource->GetData() : T{};

        return std::make_shared<Resource<T>>(
            key,
            std::move(data),
            ResourceLoadState::Loading,
            true,
            message);
    }

    template <typename T>
    std::future<ResourceHandle<T>> MakeReadyFuture(ResourceHandle<T> resource)
    {
        std::promise<ResourceHandle<T>> promise;
        auto future = promise.get_future();
        promise.set_value(std::move(resource));
        return future;
    }

    template <typename T>
    std::string ResourceTypeName() const
    {
        if constexpr (std::is_same_v<T, MeshResource>)
            return "Mesh";
        else if constexpr (std::is_same_v<T, TextureResource>)
            return "Texture";
        else if constexpr (std::is_same_v<T, ShaderResource>)
            return "Shader";
        else if constexpr (std::is_same_v<T, MaterialResource>)
            return "Material";
        else
            static_assert(kUnsupportedResourceType<T>, "Unsupported resource type");
    }

    template <typename T>
    ResourceKind ResourceKindFor() const
    {
        if constexpr (std::is_same_v<T, MeshResource>)
            return ResourceKind::Mesh;
        else if constexpr (std::is_same_v<T, TextureResource>)
            return ResourceKind::Texture;
        else if constexpr (std::is_same_v<T, ShaderResource>)
            return ResourceKind::Shader;
        else if constexpr (std::is_same_v<T, MaterialResource>)
            return ResourceKind::Material;
        else
            static_assert(kUnsupportedResourceType<T>, "Unsupported resource type");
    }

    template <typename T>
    std::string MakeAsyncToken(const std::string& key) const
    {
        return key + "#" + ResourceTypeName<T>();
    }

    template <typename T>
    void StartAsyncLoad(const std::string& key, ResourceHandle<T> resource)
    {
        const std::string token = MakeAsyncToken<T>(key);
        if (m_PendingAsyncKeys.contains(token))
            return;

        m_PendingAsyncKeys.insert(token);
        m_AsyncTasks.push_back(
            AsyncTask
            {
                token,
                std::async(std::launch::async, [this, key, resource, token]()
                {
                    ResourceLoadResult<T> result;
                    std::string error;
                    try
                    {
                        result = InvokeLoader<T>(key);
                    }
                    catch (const std::exception& e)
                    {
                        error = e.what();
                    }
                    catch (...)
                    {
                        error = "unknown async load exception";
                    }

                    std::lock_guard<std::recursive_mutex> taskLock(m_Mutex);
                    auto cached = FindCachedResource<T>(key);
                    if (cached == nullptr || cached != resource)
                    {
                        m_PendingAsyncKeys.erase(token);
                        return;
                    }

                    if (error.empty() && result.success)
                    {
                        cached->ReplaceData(std::move(result.data), ResourceLoadState::Loaded, false);
                        Logger::Get().Info("ResourceManager: async load completed [" + ResourceTypeName<T>() + "] key=" + key);

                        if constexpr (std::is_same_v<T, MaterialResource>)
                        {
                            const auto& material = cached->GetData();
                            if (!material.shaderPath.empty())
                                (void)Load<ShaderResource>(material.shaderPath);
                            if (!material.texturePath.empty())
                                (void)LoadAsync<TextureResource>(material.texturePath);
                        }
                    }
                    else
                    {
                        const std::string finalError = error.empty() ? result.errorMessage : error;
                        T fallbackData = GetDefault<T>() != nullptr ? GetDefault<T>()->GetData() : T{};
                        cached->ReplaceData(std::move(fallbackData), ResourceLoadState::Failed, true, finalError);
                        Logger::Get().Warn(
                            "ResourceManager: async load failed [" + ResourceTypeName<T>() + "] key=" + key +
                            ". " + finalError);
                    }

                    m_PendingAsyncKeys.erase(token);
                })
            });
    }

    mutable std::recursive_mutex m_Mutex;
    CacheMap<MeshResource> m_MeshCache;
    CacheMap<TextureResource> m_TextureCache;
    CacheMap<ShaderResource> m_ShaderCache;
    CacheMap<MaterialResource> m_MaterialCache;
    std::unordered_map<std::string, HotReloadWatch> m_HotReloadWatches;
    std::vector<AsyncTask> m_AsyncTasks;
    std::unordered_set<std::string> m_PendingAsyncKeys;

    ResourceHandle<MeshResource> m_DefaultMesh;
    ResourceHandle<TextureResource> m_DefaultTexture;
    ResourceHandle<ShaderResource> m_DefaultShader;
    ResourceHandle<MaterialResource> m_DefaultMaterial;
};
