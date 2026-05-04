#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

enum class ResourceLoadState
{
    Unloaded,
    Loading,
    Loaded,
    Failed
};

template <typename T>
class Resource
{
public:
    Resource() = default;

    Resource(std::string key, T data, ResourceLoadState state, bool usesFallback = false, std::string errorMessage = {})
        : m_Key(std::move(key))
        , m_Data(std::move(data))
        , m_State(state)
        , m_UsesFallback(usesFallback)
        , m_ErrorMessage(std::move(errorMessage))
    {
    }

    const std::string& GetKey() const { return m_Key; }
    const T& GetData() const { return m_Data; }
    T& GetData() { return m_Data; }
    std::uint64_t GetVersion() const { return m_Version; }

    ResourceLoadState GetLoadState() const { return m_State; }
    bool IsLoading() const { return m_State == ResourceLoadState::Loading; }
    bool IsLoaded() const { return m_State == ResourceLoadState::Loaded; }
    bool IsFailed() const { return m_State == ResourceLoadState::Failed; }
    bool UsesFallback() const { return m_UsesFallback; }
    bool IsUsable() const { return IsLoaded() || m_UsesFallback; }
    bool IsValid() const { return IsUsable(); }

    bool HasError() const { return !m_ErrorMessage.empty(); }
    const std::string& GetErrorMessage() const { return m_ErrorMessage; }

    void ReplaceData(T data, ResourceLoadState state, bool usesFallback = false, std::string errorMessage = {})
    {
        m_Data = std::move(data);
        m_State = state;
        m_UsesFallback = usesFallback;
        m_ErrorMessage = std::move(errorMessage);
        ++m_Version;
    }

private:
    std::string m_Key;
    T m_Data{};
    ResourceLoadState m_State = ResourceLoadState::Unloaded;
    bool m_UsesFallback = false;
    std::string m_ErrorMessage;
    std::uint64_t m_Version = 1;
};

template <typename T>
using ResourceHandle = std::shared_ptr<Resource<T>>;
