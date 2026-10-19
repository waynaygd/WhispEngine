#pragma once
#include "../ecs/Entity.h"
#include "GameplayEventHistory.h"
#include <string>
#include <vector>
namespace scripting {
// Plain copied values only: no Lua registry references, pointers, or retained World.
enum class LuaInstanceState {Stopped,NoEntity,NoScript,Disabled,Pending,Running,Failed,Rejected,ReloadPending};
const char* LuaInstanceStateName(LuaInstanceState);
struct LuaInstanceInfo {
    ecs::Entity entity{};
    std::uint64_t session=0,serial=0,revision=0,programRevision=0;
    LuaInstanceState state=LuaInstanceState::Stopped;
    std::string path,lastCallback,lastError,loadError;
    EventStatus lastStatus=EventStatus::Skipped;
    bool CanRead()const{return state==LuaInstanceState::Running||state==LuaInstanceState::Failed;}
    bool SameInstance(const LuaInstanceInfo& other)const{
        return entity==other.entity&&session==other.session&&serial==other.serial&&revision==other.revision&&programRevision==other.programRevision&&path==other.path&&CanRead()==other.CanRead();
    }
};
struct LuaInspectedValue {
    std::string name,type,value;
    ecs::Entity entity{};
    std::vector<LuaInspectedValue> children;
    bool limited=false;
};
struct LuaInstanceSnapshot {
    LuaInstanceInfo info;
    std::vector<LuaInspectedValue> fields,definition;
    bool limited=false;
};
}
