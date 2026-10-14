#pragma once
#include "../render/Lighting.h"
#include "../render/Environment.h"
#include "../render/Shadows.h"
#include <nlohmann/json.hpp>
// Authoring settings only. Renderer debug modes, instancing, interpolation and editor
// preferences are not serialized here. Primary ID is derived from ECS directional flags.
struct SceneSettings {
    LightingSettings lighting;
    EnvironmentSettings environment;
    ShadowSettings shadows;
    std::string primaryDirectional;
};
nlohmann::json SceneSettingsToJson(const SceneSettings&);
SceneSettings SceneSettingsFromJson(const nlohmann::json&);
