#include "Logger.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <sstream>

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

void Logger::Log(const std::string& level,const std::string& msg,const std::string& source,const std::string& callback,int line,const std::string& trace)
{
    std::lock_guard<std::mutex> lock(m_Mutex);

    if(m_RuntimeSummary.active&&(level=="ERROR"||(level=="WARN"&&source!="Runtime"))){
        auto bounded=[&](const std::string& text){if(text.size()<=MaxDiagnosticText)return text;++m_RuntimeSummary.truncated;auto tail="\n[truncated, hash="+std::to_string(std::hash<std::string>{}(text))+"]";return text.substr(0,MaxDiagnosticText-tail.size())+tail;};
        auto message=bounded(msg.substr(0,msg.find("\nstack traceback:"))),origin=bounded(source),call=bounded(callback),stack=bounded(trace);
        ++m_RuntimeSummary.total;
        auto it=std::find_if(m_RuntimeSummary.issues.begin(),m_RuntimeSummary.issues.end(),[&](auto& issue){return issue.level==level&&issue.source==origin&&issue.callback==call&&issue.message==message&&issue.line==line;});
        auto now=std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
        if(it!=m_RuntimeSummary.issues.end()){++it->count;it->lastTime=now;it->lastTrace=stack;it->lastSequence=m_Revision+1;}
        else if(m_RuntimeSummary.issues.size()<MaxRuntimeIssues)m_RuntimeSummary.issues.push_back({level,origin,call,message,stack,stack,line,1,m_Revision+1,m_Revision+1,now,now});
        else ++m_RuntimeSummary.dropped;
    }
    m_Messages.push_back({++m_Revision,level,msg});if(m_Messages.size()>1024)m_Messages.pop_front();
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);

    std::cout << "[" << level << "] " << msg << std::endl;

    if (m_File.is_open())
        m_File << "[" << level << "] "
        << std::put_time(std::localtime(&time), "%H:%M:%S")
        << " " << msg << std::endl;
}

std::vector<Logger::Entry> Logger::Messages()const{std::lock_guard lock(m_Mutex);return {m_Messages.begin(),m_Messages.end()};}
void Logger::ClearMessages(){std::lock_guard lock(m_Mutex);m_Messages.clear();++m_Revision;}

void Logger::BeginRuntimeSession(){std::lock_guard lock(m_Mutex);m_RuntimeSummary={};m_RuntimeSummary.session=++m_NextRuntimeSession;m_RuntimeSummary.active=true;}
void Logger::EndRuntimeSession(){std::lock_guard lock(m_Mutex);m_RuntimeSummary.active=false;}
void Logger::ClearRuntimeSession(){std::lock_guard lock(m_Mutex);m_RuntimeSummary={};}
Logger::RuntimeSummary Logger::RuntimeErrors()const{std::lock_guard lock(m_Mutex);return m_RuntimeSummary;}
void Logger::Diagnostic(const std::string& level,const std::string& source,const std::string& callback,const std::string& message,int line,const std::string& trace){Log(level,message,source,callback,line,trace);}
std::string Logger::RuntimeSummary::Copy(std::size_t index)const{
 std::ostringstream out;out<<"Runtime Session "<<session<<": "<<total<<" occurrences, "<<issues.size()<<" issues, "<<dropped<<" dropped, "<<truncated<<" truncated fields\n";
 for(std::size_t i=0;i<issues.size();++i)if(index==static_cast<std::size_t>(-1)||index==i){auto& e=issues[i];out<<"["<<e.level<<"] "<<e.source;if(e.line)out<<":"<<e.line;if(!e.callback.empty())out<<" ("<<e.callback<<")";out<<"\n"<<e.message<<"\nOccurrences: "<<e.count<<"; first/last Unix time: "<<std::fixed<<std::setprecision(3)<<e.firstTime<<" / "<<e.lastTime<<"\n"<<e.firstTrace<<"\n";if(e.lastTrace!=e.firstTrace)out<<"Last traceback:\n"<<e.lastTrace<<"\n";}
 return out.str();
}

bool Logger::RuntimeSessionActive()const{std::lock_guard lock(m_Mutex);return m_RuntimeSummary.active;}

std::uint64_t Logger::RuntimeSessionId()const{std::lock_guard lock(m_Mutex);return m_RuntimeSummary.session;}
