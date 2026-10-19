#pragma once
#include "../ecs/Entity.h"
#include <algorithm>
#include <cctype>
#include <deque>
#include <string>
namespace scripting {
enum class EventStatus { Queued, Processing, Success, Error, Skipped };
inline const char* EventStatusName(EventStatus s){
    switch(s){case EventStatus::Queued:return "Queued";case EventStatus::Processing:return "Processing";case EventStatus::Success:return "Success";case EventStatus::Error:return "Error";default:return "Skipped";}
}
struct EventEntity {
    ecs::Entity entity{}; // Value identity, never a pointer or a retained World.
    std::string name,stableId;
};
struct GameplayEvent {
    std::uint64_t sequence=0,session=0;
    double time=0,deliveryTime=0,handlerStart=-1,handlerEnd=-1;
    std::string name,kind,script,handler,payload,error;
    EventEntity source,target,other;
    EventStatus status=EventStatus::Queued;
};
struct GameplayEventFilter {
    std::string name,source,target,entity;
    int status=-1;
    bool errorsOnly=false;
    static bool Contains(const std::string& text,const std::string& query){return query.empty()||std::search(text.begin(),text.end(),query.begin(),query.end(),[](unsigned char a,unsigned char b){return std::tolower(a)==std::tolower(b);})!=text.end();}
    static std::string Identity(const EventEntity& e){return e.name+" "+e.stableId+" "+std::to_string(e.entity.world)+":"+std::to_string(e.entity.index)+":"+std::to_string(e.entity.generation);}
    bool Matches(const GameplayEvent& e)const{return Contains(e.name,name)&&Contains(Identity(e.source),source)&&Contains(Identity(e.target),target)&&(entity.empty()||Contains(Identity(e.source),entity)||Contains(Identity(e.target),entity))&&(status<0||int(e.status)==status)&&(!errorsOnly||e.status==EventStatus::Error);}
};
class GameplayEventHistory {
    std::deque<GameplayEvent> rows;
    std::size_t capacity=1024;
    std::uint64_t next=1,session=0;
    bool capturing=false,paused=false;
public:
    static std::string Bounded(const std::string& s,std::size_t limit){return s.size()<=limit?s:s.substr(0,limit-3)+"...";}
    bool Active()const{return capturing&&!paused;}
    bool Capturing()const{return capturing;}
    bool Paused()const{return paused;}
    void StartCapture(){capturing=true;paused=false;}
    void StopCapture(){capturing=false;paused=false;}
    void PauseCapture(bool value){paused=value;}
    void Clear(){rows.clear();}
    void BeginSession(){++session;}
    std::uint64_t Session()const{return session;}
    std::size_t Capacity()const{return capacity;}
    void SetCapacity(std::size_t n){capacity=std::clamp(n,std::size_t(128),std::size_t(8192));while(rows.size()>capacity)rows.pop_front();}
    const std::deque<GameplayEvent>& Rows()const{return rows;}
    const GameplayEvent* Find(std::uint64_t id)const{if(rows.empty()||id<rows.front().sequence||id>rows.back().sequence)return nullptr;return &rows[std::size_t(id-rows.front().sequence)];}
    std::uint64_t Add(GameplayEvent row){
        if(!Active())return 0;
        row.sequence=next++;row.session=session;
        row.name=Bounded(row.name,128);row.script=Bounded(row.script,1024);row.payload=Bounded(row.payload,2048);row.error=Bounded(row.error,4096);
        for(auto* e:{&row.source,&row.target,&row.other}){e->name=Bounded(e->name,256);e->stableId=Bounded(e->stableId,128);}
        if(rows.size()==capacity)rows.pop_front();rows.push_back(std::move(row));return rows.back().sequence;
    }
    void Update(std::uint64_t id,EventStatus status,double time,const std::string& script={},const std::string& error={}){
        if(!id||!Active())return;
        if(rows.empty()||id<rows.front().sequence||id>rows.back().sequence)return;
        auto& row=rows[std::size_t(id-rows.front().sequence)];row.status=status;row.deliveryTime=time;if(status==EventStatus::Processing)row.handlerStart=time;else if(row.handlerStart>=0)row.handlerEnd=time;row.script=Bounded(script,1024);row.error=Bounded(error,4096);
    }
    void EndSession(){if(Active())for(auto& row:rows)if(row.session==session&&row.status==EventStatus::Queued){row.status=EventStatus::Skipped;row.error="Play ended before delivery";}}
};
}
