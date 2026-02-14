#pragma once
#include <string>
#include <fstream>
#include <mutex>

class Logger
{
public:
    static Logger& Get();

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
    std::mutex m_Mutex;
};
