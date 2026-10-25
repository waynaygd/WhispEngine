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
    struct RuntimeIssue {
        std::string level,source,callback,message,firstTrace,lastTrace;
        int line=0;
        std::uint64_t count=0,firstSequence=0,lastSequence=0;
        double firstTime=0,lastTime=0;
    };
    struct RuntimeSummary {
        std::uint64_t session=0,total=0,dropped=0,truncated=0;
        bool active=false;
        std::vector<RuntimeIssue> issues;
        std::string Copy(std::size_t index=static_cast<std::size_t>(-1)) const;
    };
    static constexpr std::size_t MaxRuntimeIssues=256,MaxDiagnosticText=8192;
    void BeginRuntimeSession();
    void EndRuntimeSession();
    void ClearRuntimeSession();
    RuntimeSummary RuntimeErrors() const;
    bool RuntimeSessionActive() const;
    std::uint64_t RuntimeSessionId() const;
    // Structured diagnostics retain their source; legacy errors use Runtime as their category.
    void Diagnostic(const std::string& level,const std::string& source,const std::string& callback,
                    const std::string& message,int line=0,const std::string& trace={});
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
    void Log(const std::string& level, const std::string& msg,const std::string& source="Runtime",
             const std::string& callback={},int line=0,const std::string& trace={});
    RuntimeSummary m_RuntimeSummary;
    std::uint64_t m_NextRuntimeSession=0;

private:
    std::ofstream m_File;
    mutable std::mutex m_Mutex;
    std::deque<Entry> m_Messages;
    std::atomic<std::uint64_t> m_Revision{0};
};
