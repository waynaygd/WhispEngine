#pragma once
#include <string>
#include <fstream>
#include <mutex>
#include <deque>
#include <vector>
#include <cstdint>
#include <atomic>

class Logger
{
public:
    struct Entry {std::uint64_t sequence;std::string level,message;};
    static Logger& Get();
    std::vector<Entry> Messages()const;
    std::uint64_t Revision()const{return m_Revision.load();}
    void ClearMessages();

    void Initialize(const std::string& filename);
    void Shutdown();

    void Info(const std::string& msg);
    void Warn(const std::string& msg);
    void Error(const std::string& msg);

private:
    Logger() = default;
    void Log(const std::string& level, const std::string& msg);

private:
    std::ofstream m_File;
    mutable std::mutex m_Mutex;
    std::deque<Entry> m_Messages;
    std::atomic<std::uint64_t> m_Revision{0};
};
