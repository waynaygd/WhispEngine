#pragma once
#include "../ecs/World.h"
#include "../scene/SceneSettings.h"
#include <string>
#include <vector>
class ResourceManager;
namespace scripting {class LuaRuntime;}
namespace editor {
enum class ValidationSeverity {Error,Warning,Info};
const char* ValidationSeverityName(ValidationSeverity);
struct SceneValidationIssue {
    ValidationSeverity severity=ValidationSeverity::Error;
    std::string title,description;
    ecs::Entity entity{};
    std::string stableId,entityName,component,field,resource,recommendation;
    bool Matches(const std::string& query,int severity=-1)const;
    bool operator==(const SceneValidationIssue&)const=default;
};
// A manual snapshot, scoped to the exact World. No resource requests or script execution.
struct SceneValidationReport {
    bool validated=false;
    std::uint64_t world=0;
    std::string context;
    std::vector<SceneValidationIssue> issues;
    std::size_t Count(ValidationSeverity)const;
};
SceneValidationReport ValidateScene(const ecs::World&,const SceneSettings&,
    scripting::LuaRuntime* metadata=nullptr,ResourceManager* resources=nullptr);
}
