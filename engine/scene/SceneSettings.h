#pragma once
#include "../render/Lighting.h"
#include "../render/Environment.h"
#include "../render/Shadows.h"
#include "../render/EditorVisuals.h"
#include <nlohmann/json.hpp>
// Authoring settings only. Renderer debug modes, instancing, interpolation and editor
// preferences are not serialized here. Primary ID is derived from ECS directional flags.
struct SceneSettings {
    LightingSettings lighting;
    EnvironmentSettings environment;
    ShadowSettings shadows;
    SceneBackground background;
    std::string primaryDirectional;
    bool operator==(const SceneSettings&)const=default;
};
nlohmann::json SceneSettingsToJson(const SceneSettings&);
SceneSettings SceneSettingsFromJson(const nlohmann::json&);
