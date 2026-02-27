#include "Logger.h"
#include <iostream>
#include <chrono>
#include <iomanip>

Logger& Logger::Get()
{
    static Logger instance;
    return instance;
}

void Logger::Initialize(const std::string& filename)
{
    m_File.open(filename);
}

void Logger::Shutdown()
{
    if (m_File.is_open())
        m_File.close();
}

void Logger::Info(const std::string& msg)
{
    Log("INFO", msg);
}

void Logger::Warn(const std::string& msg)
{
    Log("WARN", msg);
}

void Logger::Error(const std::string& msg)
{
    Log("ERROR", msg);
}

void Logger::Log(const std::string& level, const std::string& msg)
{
    std::lock_guard<std::mutex> lock(m_Mutex);

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);

    std::cout << "[" << level << "] " << msg << std::endl;

    if (m_File.is_open())
        m_File << "[" << level << "] "
        << std::put_time(std::localtime(&time), "%H:%M:%S")
        << " " << msg << std::endl;
}
