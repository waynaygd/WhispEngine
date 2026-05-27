#include "EditorLayer.h"

#include "../core/Application.h"
#include "../ecs/World.h"
#include "../ecs/components/ColliderComponent.h"
#include "../ecs/components/LightComponent.h"
#include "../ecs/components/MaterialComponent.h"
#include "../ecs/components/MeshRendererComponent.h"
#include "../ecs/components/RigidbodyComponent.h"
#include "../ecs/components/TagComponent.h"
#include "../ecs/components/TransformComponent.h"
#include "../render/IRenderAdapter.h"
#include "../resources/ResourceManager.h"
#include "../core/AssetPaths.h"

#include <imgui.h>
#include <ImGuizmo.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

namespace
{
float* Vec3Data(ecs::Vec3& value)
{
    return &value.x;
}

const char* ColliderTypeName(ecs::ColliderType type)
{
    switch (type)
    {
    case ecs::ColliderType::Box:
        return "Box";
    case ecs::ColliderType::Sphere:
        return "Sphere";
    default:
        return "Unknown";
    }
}

float Dot(const ecs::Vec3& lhs, const ecs::Vec3& rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

ecs::Vec3 Cross(const ecs::Vec3& lhs, const ecs::Vec3& rhs)
{
    return ecs::Vec3{
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x
    };
}

float Length(const ecs::Vec3& value)
{
    return std::sqrt(Dot(value, value));
}

ecs::Vec3 Normalize(const ecs::Vec3& value)
{
    const float length = Length(value);
    if (length <= 0.0001f)
        return ecs::Vec3{};

    const float invLength = 1.0f / length;
    return ecs::Vec3{ value.x * invLength, value.y * invLength, value.z * invLength };
}

ecs::Vec3 BuildCameraForward(float yaw, float pitch)
{
    const float cosPitch = std::cos(pitch);
    return Normalize(ecs::Vec3{
        std::sin(yaw) * cosPitch,
        std::sin(pitch),
        std::cos(yaw) * cosPitch
    });
}

void SetIdentity(float* out16)
{
    for (int i = 0; i < 16; ++i)
        out16[i] = 0.0f;

    out16[0] = 1.0f;
    out16[5] = 1.0f;
    out16[10] = 1.0f;
    out16[15] = 1.0f;
}

void BuildViewMatrix(float* out16, const ecs::Vec3& cameraPosition, float yaw, float pitch)
{
    const ecs::Vec3 worldUp{ 0.0f, 1.0f, 0.0f };
    const ecs::Vec3 forward = BuildCameraForward(yaw, pitch);
    const ecs::Vec3 right = Normalize(Cross(worldUp, forward));
    const ecs::Vec3 up = Cross(forward, right);

    SetIdentity(out16);
    out16[0] = right.x;
    out16[1] = up.x;
    out16[2] = forward.x;
    out16[4] = right.y;
    out16[5] = up.y;
    out16[6] = forward.y;
    out16[8] = right.z;
    out16[9] = up.z;
    out16[10] = forward.z;
    out16[12] = -Dot(right, cameraPosition);
    out16[13] = -Dot(up, cameraPosition);
    out16[14] = -Dot(forward, cameraPosition);
}

void BuildPerspectiveMatrix(float* out16, float verticalFovRadians, float aspectRatio, float nearPlane, float farPlane)
{
    const float yScale = 1.0f / std::tan(verticalFovRadians * 0.5f);
    const float xScale = yScale / aspectRatio;
    const float depthRange = farPlane - nearPlane;

    for (int i = 0; i < 16; ++i)
        out16[i] = 0.0f;

    out16[0] = xScale;
    out16[5] = yScale;
    out16[10] = farPlane / depthRange;
    out16[11] = 1.0f;
    out16[14] = -(nearPlane * farPlane) / depthRange;
}

float RadiansToDegrees(float radians)
{
    return radians * 57.2957795f;
}

float DegreesToRadians(float degrees)
{
    return degrees * 0.0174532925f;
}

ImGuizmo::OPERATION ToGizmoOperation(int operation)
{
    switch (operation)
    {
    case 1:
        return ImGuizmo::ROTATE;
    case 2:
        return ImGuizmo::SCALE;
    default:
        return ImGuizmo::TRANSLATE;
    }
}

std::string ToLowerExtension(const std::filesystem::path& path)
{
    std::string extension = path.extension().generic_string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });
    return extension;
}

std::vector<std::string> ScanAssetKeys(
    const std::filesystem::path& root,
    const std::filesystem::path& subdirectory,
    const std::unordered_set<std::string>& extensions)
{
    std::vector<std::string> keys;
    if (root.empty())
        return keys;

    const std::filesystem::path scanRoot = subdirectory.empty() ? root : root / subdirectory;
    if (!std::filesystem::exists(scanRoot))
        return keys;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(scanRoot))
    {
        if (!entry.is_regular_file())
            continue;

        const std::string extension = ToLowerExtension(entry.path());
        if (!extensions.contains(extension))
            continue;

        std::error_code ec;
        const std::filesystem::path relative = std::filesystem::relative(entry.path(), root, ec);
        if (!ec && !relative.empty())
            keys.push_back(relative.generic_string());
    }

    std::sort(keys.begin(), keys.end());
    return keys;
}

bool DrawResourceCombo(
    const char* label,
    std::string& value,
    const std::vector<std::string>& options,
    bool allowNone)
{
    bool changed = false;
    const char* preview = value.empty() ? "<none>" : value.c_str();
    if (ImGui::BeginCombo(label, preview))
    {
        if (allowNone)
        {
            const bool selected = value.empty();
            if (ImGui::Selectable("<none>", selected))
            {
                value.clear();
                changed = true;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }

        if (!value.empty() && std::find(options.begin(), options.end(), value) == options.end())
        {
            ImGui::SeparatorText("Current");
            if (ImGui::Selectable(value.c_str(), true))
            {
                changed = true;
            }
            ImGui::SeparatorText("Assets");
        }

        for (const std::string& option : options)
        {
            const bool selected = option == value;
            if (ImGui::Selectable(option.c_str(), selected))
            {
                value = option;
                changed = true;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }

        ImGui::EndCombo();
    }
    return changed;
}

const char* FormatBytes(std::uint64_t bytes)
{
    static char buffer[64];
    const char* suffixes[] = { "B", "KB", "MB", "GB" };
    double value = static_cast<double>(bytes);
    int suffixIndex = 0;
    while (value >= 1024.0 && suffixIndex < 3)
    {
        value /= 1024.0;
        ++suffixIndex;
    }

    std::snprintf(buffer, sizeof(buffer), "%.2f %s", value, suffixes[suffixIndex]);
    return buffer;
}

template <typename T>
void RestoreComponent(ecs::World& world, ecs::Entity entity, const std::optional<T>& value)
{
    if (value.has_value())
    {
        if (auto* component = world.GetComponent<T>(entity))
            *component = *value;
        else
            world.AddComponent<T>(entity) = *value;
    }
    else
    {
        world.RemoveComponent<T>(entity);
    }
}

bool SaveMaterialFile(const std::string& materialKey, const ecs::MaterialComponent& material, std::string* outError)
{
    if (materialKey.empty())
    {
        if (outError != nullptr)
            *outError = "Material path is empty";
        return false;
    }

    const std::filesystem::path materialPath = AssetPaths::ResolveAssetOutputPath(materialKey);
    if (materialPath.empty())
    {
        if (outError != nullptr)
            *outError = "Could not resolve material output path";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(materialPath.parent_path(), ec);

    nlohmann::json json;
    json["name"] = materialPath.stem().string();
    json["shaderPath"] = material.shaderPath.empty() ? "dx12/textured.hlsl" : material.shaderPath;
    if (!material.texturePath.empty())
        json["texturePath"] = material.texturePath;
    json["baseColor"] = nlohmann::json::array({ material.tint[0], material.tint[1], material.tint[2], material.tint[3] });

    std::ofstream file(materialPath);
    if (!file.is_open())
    {
        if (outError != nullptr)
            *outError = "Could not write material file: " + materialPath.string();
        return false;
    }

    file << json.dump(2);
    return true;
}
}

namespace editor
{
bool EditorLayer::IsPointInsideViewport(double x, double y) const
{
    return m_ViewportHasBounds &&
        x >= static_cast<double>(m_ViewportMinX) &&
        y >= static_cast<double>(m_ViewportMinY) &&
        x <= static_cast<double>(m_ViewportMaxX) &&
        y <= static_cast<double>(m_ViewportMaxY);
}

void EditorLayer::Render(Application& app, IRenderAdapter* renderer, float dt)
{
    RefreshAssetLists(dt);

    m_ViewportPixelWidth = 0;
    m_ViewportPixelHeight = 0;
    m_ViewportHovered = false;
    m_ViewportHasBounds = false;

    ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

    DrawMainMenu(app);
    HandleEditorShortcuts(app);

    if (m_ShowHierarchy)
        DrawSceneHierarchy(app);
    if (m_ShowInspector)
        DrawInspector(app);
    if (m_ShowStatistics)
        DrawStatistics(app, dt);
    if (m_ShowViewport)
        DrawViewport(app, renderer);
    else
    {
        m_ViewportPixelWidth = 0;
        m_ViewportPixelHeight = 0;
    }
    if (m_ShowAssetBrowser)
        DrawAssetBrowser(app);
    if (m_ShowMaterialEditor)
        DrawMaterialEditor(app);
    if (m_ShowDemoWindow)
        ImGui::ShowDemoWindow(&m_ShowDemoWindow);
}

EditorLayer::EntitySnapshot EditorLayer::CaptureSelectedEntity(Application& app) const
{
    EntitySnapshot snapshot;
    snapshot.entity = m_SelectedEntity;

    const auto& world = app.GetWorld();
    if (!m_SelectedEntity.IsValid() || !world.IsAlive(m_SelectedEntity))
        return snapshot;

    if (const auto* component = world.GetComponent<ecs::TagComponent>(m_SelectedEntity))
        snapshot.tag = *component;
    if (const auto* component = world.GetComponent<ecs::TransformComponent>(m_SelectedEntity))
        snapshot.transform = *component;
    if (const auto* component = world.GetComponent<ecs::MeshRendererComponent>(m_SelectedEntity))
        snapshot.meshRenderer = *component;
    if (const auto* component = world.GetComponent<ecs::MaterialComponent>(m_SelectedEntity))
        snapshot.material = *component;
    if (const auto* component = world.GetComponent<ecs::RigidbodyComponent>(m_SelectedEntity))
        snapshot.rigidbody = *component;
    if (const auto* component = world.GetComponent<ecs::ColliderComponent>(m_SelectedEntity))
        snapshot.collider = *component;

    return snapshot;
}

void EditorLayer::RestoreSnapshot(Application& app, const EntitySnapshot& snapshot)
{
    auto& world = app.GetWorld();
    if (!snapshot.entity.IsValid() || !world.IsAlive(snapshot.entity))
        return;

    RestoreComponent(world, snapshot.entity, snapshot.tag);
    RestoreComponent(world, snapshot.entity, snapshot.transform);
    RestoreComponent(world, snapshot.entity, snapshot.meshRenderer);
    RestoreComponent(world, snapshot.entity, snapshot.material);
    RestoreComponent(world, snapshot.entity, snapshot.rigidbody);
    RestoreComponent(world, snapshot.entity, snapshot.collider);
    m_SelectedEntity = snapshot.entity;
}

void EditorLayer::PushUndo(const std::string& label, const EntitySnapshot& snapshot)
{
    if (!snapshot.entity.IsValid())
        return;

    m_UndoStack.push_back(UndoRecord{ label, snapshot });
    if (m_UndoStack.size() > MaxUndoRecords)
        m_UndoStack.erase(m_UndoStack.begin());
    m_RedoStack.clear();
}

void EditorLayer::PushUndoBeforeChange(Application& app, const std::string& label)
{
    PushUndo(label, CaptureSelectedEntity(app));
}

void EditorLayer::Undo(Application& app)
{
    if (m_UndoStack.empty())
        return;

    UndoRecord record = m_UndoStack.back();
    m_UndoStack.pop_back();
    m_RedoStack.push_back(UndoRecord{ record.label, CaptureSelectedEntity(app) });
    RestoreSnapshot(app, record.snapshot);
}

void EditorLayer::Redo(Application& app)
{
    if (m_RedoStack.empty())
        return;

    UndoRecord record = m_RedoStack.back();
    m_RedoStack.pop_back();
    m_UndoStack.push_back(UndoRecord{ record.label, CaptureSelectedEntity(app) });
    RestoreSnapshot(app, record.snapshot);
}

void EditorLayer::HandleEditorShortcuts(Application& app)
{
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || !io.KeyCtrl)
        return;

    if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
        Undo(app);
    if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
        Redo(app);
}

void EditorLayer::RefreshAssetLists(float dt)
{
    m_AssetRefreshTimer += dt;
    if (!m_AssetListsDirty && m_AssetRefreshTimer < 2.0f)
        return;

    m_AssetRefreshTimer = 0.0f;
    m_AssetListsDirty = false;

    const std::filesystem::path assetRoot = AssetPaths::ResolveAssetRoot();
    const std::filesystem::path shaderRoot = AssetPaths::ResolveShaderRoot();

    m_MeshAssets = ScanAssetKeys(assetRoot, "models", { ".obj", ".fbx", ".gltf", ".glb" });
    m_TextureAssets = ScanAssetKeys(assetRoot, "textures", { ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".dds" });
    m_MaterialAssets = ScanAssetKeys(assetRoot, "materials", { ".json", ".material" });
    m_ShaderAssets = ScanAssetKeys(shaderRoot, "", { ".hlsl" });
}

void EditorLayer::DrawMainMenu(Application& app)
{
    if (!ImGui::BeginMainMenuBar())
        return;

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("Save Scene"))
        {
            std::string error;
            (void)app.SaveCurrentScene(&error);
        }
        if (ImGui::MenuItem("Load Scene"))
        {
            std::string error;
            (void)app.LoadCurrentScene(&error);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit"))
    {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, CanUndo()))
            Undo(app);
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, CanRedo()))
            Redo(app);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View"))
    {
        ImGui::MenuItem("Scene Hierarchy", nullptr, &m_ShowHierarchy);
        ImGui::MenuItem("Inspector", nullptr, &m_ShowInspector);
        ImGui::MenuItem("Statistics", nullptr, &m_ShowStatistics);
        ImGui::MenuItem("Viewport", nullptr, &m_ShowViewport);
        ImGui::MenuItem("Asset Browser", nullptr, &m_ShowAssetBrowser);
        ImGui::MenuItem("Material Editor", nullptr, &m_ShowMaterialEditor);
        ImGui::Separator();
        ImGui::MenuItem("ImGui Demo", nullptr, &m_ShowDemoWindow);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Tools"))
    {
        if (ImGui::MenuItem("Spawn Entity"))
            app.SpawnGameplayEntity();
        if (ImGui::MenuItem("Fire Projectile"))
            app.SpawnPhysicsProjectile();
        if (ImGui::MenuItem("Toggle Debug Colliders"))
            app.ToggleDebugColliders();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help"))
    {
        ImGui::TextUnformatted("WhispEngine Editor");
        ImGui::TextUnformatted("Dear ImGui WYSIWYG tool layer");
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void EditorLayer::DrawSceneHierarchy(Application& app)
{
    ImGui::SetNextWindowSize(ImVec2(260.0f, 520.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Scene Hierarchy", &m_ShowHierarchy);

    auto& world = app.GetWorld();
    std::vector<ecs::Entity> entities;
    world.ForEach<ecs::TagComponent>(
        [&](ecs::Entity entity, ecs::TagComponent&)
        {
            entities.push_back(entity);
        });

    std::sort(
        entities.begin(),
        entities.end(),
        [](const ecs::Entity& lhs, const ecs::Entity& rhs)
        {
            return lhs.index < rhs.index;
        });

    if (!m_SelectedEntity.IsValid() || !world.IsAlive(m_SelectedEntity))
    {
        m_SelectedEntity = entities.empty() ? ecs::Entity{} : entities.front();
    }

    for (const ecs::Entity& entity : entities)
    {
        auto* tag = world.GetComponent<ecs::TagComponent>(entity);
        const std::string label =
            (tag != nullptr && !tag->name.empty())
                ? tag->name + "##" + std::to_string(entity.index)
                : "Entity " + std::to_string(entity.index);

        const bool selected = entity == m_SelectedEntity;
        if (ImGui::Selectable(label.c_str(), selected))
            m_SelectedEntity = entity;
    }



    ImGui::End();
}

void EditorLayer::DrawInspector(Application& app)
{
    ImGui::SetNextWindowSize(ImVec2(360.0f, 560.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Inspector", &m_ShowInspector);

    auto& world = app.GetWorld();
    if (!m_SelectedEntity.IsValid() || !world.IsAlive(m_SelectedEntity))
    {
        ImGui::TextUnformatted("No entity selected");
        ImGui::End();
        return;
    }

    ImGui::Text("Entity %u:%u", m_SelectedEntity.index, m_SelectedEntity.generation);

    if (auto* tag = world.GetComponent<ecs::TagComponent>(m_SelectedEntity))
    {
        if (ImGui::TreeNodeEx("Tag", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const EntitySnapshot before = CaptureSelectedEntity(app);
            if (ImGui::InputText("Name", &tag->name))
                PushUndo("Edit Tag", before);
            ImGui::TreePop();
        }
    }

    if (auto* transform = world.GetComponent<ecs::TransformComponent>(m_SelectedEntity))
    {
        if (ImGui::TreeNodeEx("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            EntitySnapshot before = CaptureSelectedEntity(app);
            if (ImGui::DragFloat3("Position", Vec3Data(transform->position), 0.01f))
                PushUndo("Edit Position", before);
            before = CaptureSelectedEntity(app);
            if (ImGui::DragFloat3("Rotation", Vec3Data(transform->rotation), 0.01f))
                PushUndo("Edit Rotation", before);
            before = CaptureSelectedEntity(app);
            if (ImGui::DragFloat3("Scale", Vec3Data(transform->scale), 0.01f, 0.001f, 100.0f))
                PushUndo("Edit Scale", before);
            ImGui::TreePop();
        }
    }

    if (auto* mesh = world.GetComponent<ecs::MeshRendererComponent>(m_SelectedEntity))
    {
        if (ImGui::TreeNodeEx("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen))
        {
            EntitySnapshot before = CaptureSelectedEntity(app);
            if (ImGui::Checkbox("Visible", &mesh->visible))
                PushUndo("Toggle Mesh Visibility", before);
            before = CaptureSelectedEntity(app);
            if (ImGui::Checkbox("Cast Shadows", &mesh->castShadows))
                PushUndo("Toggle Cast Shadows", before);
            before = CaptureSelectedEntity(app);
            if (ImGui::Checkbox("Receive Shadows", &mesh->receiveShadows))
                PushUndo("Toggle Receive Shadows", before);
            before = CaptureSelectedEntity(app);
            if (ImGui::ColorEdit4("Albedo Color", mesh->albedoColor))
                PushUndo("Edit Albedo", before);
            before = CaptureSelectedEntity(app);
            if (ImGui::DragFloat("Shininess", &mesh->shininess, 0.1f, 1.0f, 256.0f))
                PushUndo("Edit Shininess", before);
            before = CaptureSelectedEntity(app);
            if (ImGui::Checkbox("Use Texture", &mesh->useTexture))
                PushUndo("Toggle Use Texture", before);
            before = CaptureSelectedEntity(app);
            if (DrawResourceCombo("Mesh", mesh->meshPath, m_MeshAssets, false))
                PushUndo("Change Mesh", before);
            before = CaptureSelectedEntity(app);
            if (DrawResourceCombo("Texture", mesh->texturePath, m_TextureAssets, true))
                PushUndo("Change Texture", before);
            before = CaptureSelectedEntity(app);
            if (DrawResourceCombo("Shader", mesh->shaderPath, m_ShaderAssets, false))
                PushUndo("Change Shader", before);
            ImGui::TreePop();
        }
    }

    if (auto* material = world.GetComponent<ecs::MaterialComponent>(m_SelectedEntity))
    {
        if (ImGui::TreeNodeEx("Material", ImGuiTreeNodeFlags_DefaultOpen))
        {
            EntitySnapshot before = CaptureSelectedEntity(app);
            if (DrawResourceCombo("Material", material->materialPath, m_MaterialAssets, true))
                PushUndo("Change Material", before);
            before = CaptureSelectedEntity(app);
            if (DrawResourceCombo("Material Shader", material->shaderPath, m_ShaderAssets, true))
                PushUndo("Change Material Shader", before);
            before = CaptureSelectedEntity(app);
            if (DrawResourceCombo("Material Texture", material->texturePath, m_TextureAssets, true))
                PushUndo("Change Material Texture", before);
            before = CaptureSelectedEntity(app);
            if (ImGui::ColorEdit4("Tint", material->tint))
                PushUndo("Edit Material Tint", before);
            ImGui::TreePop();
        }
    }

    if (auto* rb = world.GetComponent<ecs::RigidbodyComponent>(m_SelectedEntity))
    {
        if (ImGui::TreeNodeEx("Rigidbody", ImGuiTreeNodeFlags_DefaultOpen))
        {
            EntitySnapshot before = CaptureSelectedEntity(app);
            if (ImGui::Checkbox("Use Gravity", &rb->useGravity))
                PushUndo("Toggle Gravity", before);
            before = CaptureSelectedEntity(app);
            if (ImGui::Checkbox("Static", &rb->isStatic))
                PushUndo("Toggle Static Rigidbody", before);
            before = CaptureSelectedEntity(app);
            if (ImGui::Checkbox("Simulate Physics", &rb->simulatePhysics))
                PushUndo("Toggle Physics Simulation", before);
            before = CaptureSelectedEntity(app);
            if (ImGui::DragFloat("Mass", &rb->mass, 0.05f, 0.001f, 1000.0f))
                PushUndo("Edit Rigidbody Mass", before);
            ImGui::Text("Velocity: %.3f, %.3f, %.3f", rb->velocity.x, rb->velocity.y, rb->velocity.z);
            ImGui::TreePop();
        }
    }

    if (auto* collider = world.GetComponent<ecs::ColliderComponent>(m_SelectedEntity))
    {
        if (ImGui::TreeNodeEx("Collider", ImGuiTreeNodeFlags_DefaultOpen))
        {
            int typeIndex = collider->type == ecs::ColliderType::Sphere ? 1 : 0;
            const char* types[] = { "Box", "Sphere" };
            EntitySnapshot before = CaptureSelectedEntity(app);
            if (ImGui::Combo("Type", &typeIndex, types, 2))
            {
                collider->type = typeIndex == 1 ? ecs::ColliderType::Sphere : ecs::ColliderType::Box;
                PushUndo("Change Collider Type", before);
            }
            ImGui::Text("Current: %s", ColliderTypeName(collider->type));
            before = CaptureSelectedEntity(app);
            if (ImGui::DragFloat3("Half Extents", Vec3Data(collider->halfExtents), 0.01f, 0.001f, 100.0f))
                PushUndo("Edit Collider Size", before);
            before = CaptureSelectedEntity(app);
            if (ImGui::DragFloat3("Offset", Vec3Data(collider->offset), 0.01f))
                PushUndo("Edit Collider Offset", before);
            before = CaptureSelectedEntity(app);
            if (ImGui::DragFloat("Restitution", &collider->restitution, 0.01f, 0.0f, 1.0f))
                PushUndo("Edit Restitution", before);
            before = CaptureSelectedEntity(app);
            if (ImGui::DragFloat("Friction", &collider->friction, 0.01f, 0.0f, 1.0f))
                PushUndo("Edit Friction", before);
            before = CaptureSelectedEntity(app);
            if (ImGui::Checkbox("Auto Fit From Mesh", &collider->autoFitFromMesh))
                PushUndo("Toggle Collider Auto Fit", before);
            ImGui::TreePop();
        }
    }

    if (auto* light = world.GetComponent<ecs::LightComponent>(m_SelectedEntity))
    {
        if (ImGui::TreeNodeEx("Light", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const char* types[] = { "Directional", "Point", "Spot" };
            int typeIndex = static_cast<int>(light->type);
            EntitySnapshot before = CaptureSelectedEntity(app);
            if (ImGui::Checkbox("Enabled", &light->enabled))
                PushUndo("Toggle Light", before);
            before = CaptureSelectedEntity(app);
            if (ImGui::Combo("Type", &typeIndex, types, 3))
            {
                light->type = static_cast<ecs::LightType>(typeIndex);
                PushUndo("Change Light Type", before);
            }
            before = CaptureSelectedEntity(app);
            if (ImGui::ColorEdit3("Color", Vec3Data(light->color)))
                PushUndo("Edit Light Color", before);
            before = CaptureSelectedEntity(app);
            if (ImGui::DragFloat("Intensity", &light->intensity, 0.05f, 0.0f, 50.0f))
                PushUndo("Edit Light Intensity", before);
            if (light->type != ecs::LightType::Directional)
            {
                before = CaptureSelectedEntity(app);
                if (ImGui::DragFloat("Range", &light->range, 0.1f, 0.1f, 200.0f))
                    PushUndo("Edit Light Range", before);
            }
            if (light->type == ecs::LightType::Spot)
            {
                before = CaptureSelectedEntity(app);
                if (ImGui::DragFloat("Inner Cone", &light->innerConeAngle, 0.1f, 1.0f, 89.0f))
                    PushUndo("Edit Inner Cone", before);
                before = CaptureSelectedEntity(app);
                if (ImGui::DragFloat("Outer Cone", &light->outerConeAngle, 0.1f, 1.0f, 89.0f))
                    PushUndo("Edit Outer Cone", before);
            }
            before = CaptureSelectedEntity(app);
            if (ImGui::Checkbox("Cast Shadows", &light->castsShadows))
                PushUndo("Toggle Light Shadows", before);
            if (light->castsShadows)
            {
                before = CaptureSelectedEntity(app);
                if (ImGui::DragFloat("Shadow Bias", &light->shadowBias, 0.0001f, 0.0f, 0.1f, "%.5f"))
                    PushUndo("Edit Shadow Bias", before);
                before = CaptureSelectedEntity(app);
                if (ImGui::DragFloat("Normal Bias", &light->normalBias, 0.0001f, 0.0f, 0.1f, "%.5f"))
                    PushUndo("Edit Normal Bias", before);
                before = CaptureSelectedEntity(app);
                if (ImGui::DragInt("Shadow Resolution", &light->shadowMapResolution, 64.0f, 128, 4096))
                    PushUndo("Edit Shadow Resolution", before);
            }
            ImGui::TreePop();
        }
    }



    ImGui::End();
}

void EditorLayer::DrawStatistics(Application& app, float dt)
{
    m_FpsTimer += dt;
    ++m_FpsFrames;
    if (m_FpsTimer >= 1.0f)
    {
        m_AverageFps = static_cast<float>(m_FpsFrames) / m_FpsTimer;
        m_FpsFrames = 0;
        m_FpsTimer = 0.0f;
    }

    auto& world = app.GetWorld();
    int meshRendererCount = 0;
    int colliderCount = 0;
    int directionalLights = 0;
    int pointLights = 0;
    int spotLights = 0;
    world.ForEach<ecs::MeshRendererComponent>(
        [&](ecs::Entity, ecs::MeshRendererComponent&)
        {
            ++meshRendererCount;
        });
    world.ForEach<ecs::ColliderComponent>(
        [&](ecs::Entity, ecs::ColliderComponent&)
        {
            ++colliderCount;
        });
    world.ForEach<ecs::LightComponent>(
        [&](ecs::Entity, ecs::LightComponent& light)
        {
            if (!light.enabled) return;
            if (light.type == ecs::LightType::Directional) ++directionalLights;
            else if (light.type == ecs::LightType::Point) ++pointLights;
            else ++spotLights;
        });

    ImGui::SetNextWindowSize(ImVec2(320.0f, 250.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Statistics", &m_ShowStatistics);
    ImGui::Text("Average FPS: %.1f", m_AverageFps);
    ImGui::Text("Frame time: %.3f ms (~%.1f FPS)", dt * 1000.0f, dt > 0.0f ? 1.0f / dt : 0.0f);
    ImGui::Text("Entities: %zu", world.GetAliveCount());
    ImGui::Text("Mesh renderers: %d", meshRendererCount);
    ImGui::Text("Colliders: %d", colliderCount);
    ImGui::Text("Active collisions: %zu", app.GetActiveCollisionCount());
    ImGui::Text("Directional lights: %d", directionalLights);
    ImGui::Text("Point lights: %d", pointLights);
    ImGui::Text("Spot lights: %d", spotLights);
    ImGui::Text("Gameplay entities: %zu", app.GetGameplayEntityCount());
    if (const ResourceManager* resources = app.GetResourceManager())
    {
        const ResourceManager::ResourceStats stats = resources->GetStats();
        ImGui::Separator();
        ImGui::Text("Resources: M %zu / T %zu / S %zu / Mat %zu",
            stats.meshCount,
            stats.textureCount,
            stats.shaderCount,
            stats.materialCount);
        ImGui::Text("Resource memory: %s", FormatBytes(stats.estimatedCpuBytes));
        ImGui::Text("Loading: %zu  Failed: %zu", stats.loadingCount, stats.failedCount);
    }

    ImGui::End();
}

void EditorLayer::DrawViewport(Application& app, IRenderAdapter* renderer)
{
    ImGui::SetNextWindowSize(ImVec2(760.0f, 520.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(420.0f, 280.0f), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::Begin("Viewport", &m_ShowViewport);

    const bool playMode = app.IsEditorPlayMode();
    if (ImGui::Button(playMode ? "Stop" : "Play"))
        app.SetEditorPlayMode(!playMode);
    ImGui::SameLine();
    ImGui::TextUnformatted(playMode ? "Play" : "Edit");
    ImGui::SameLine();
    bool litMode = app.IsLitShadingEnabled();
    if (ImGui::Checkbox("Lit", &litMode))
        app.SetLitShadingEnabled(litMode);

    ImGui::Separator();

    ImGui::RadioButton("Move", &m_GizmoOperation, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Rotate", &m_GizmoOperation, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Scale", &m_GizmoOperation, 2);

    ImGui::RadioButton("Local", &m_GizmoMode, 0);
    ImGui::SameLine();
    ImGui::RadioButton("World", &m_GizmoMode, 1);

    const ImVec2 contentMin = ImGui::GetCursorScreenPos();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 viewportSize(std::max(available.x, 1.0f), std::max(available.y, 1.0f));
    const ImVec2 contentMax(contentMin.x + viewportSize.x, contentMin.y + viewportSize.y);
    const ImVec2 framebufferScale = ImGui::GetIO().DisplayFramebufferScale;
    m_ViewportPixelWidth = std::max(1, static_cast<int>(std::lround(viewportSize.x * framebufferScale.x)));
    m_ViewportPixelHeight = std::max(1, static_cast<int>(std::lround(viewportSize.y * framebufferScale.y)));
    m_ViewportMinX = contentMin.x;
    m_ViewportMinY = contentMin.y;
    m_ViewportMaxX = contentMax.x;
    m_ViewportMaxY = contentMax.y;
    m_ViewportHasBounds = true;
    m_ViewportHovered = ImGui::IsMouseHoveringRect(contentMin, contentMax, false);

    const std::uint64_t viewportTextureId = renderer != nullptr ? renderer->GetViewportTextureId() : 0;
    if (viewportTextureId != 0)
    {
        ImGui::Image(
            ImTextureID(viewportTextureId),
            viewportSize,
            ImVec2(0.0f, 0.0f),
            ImVec2(1.0f, 1.0f));
    }
    else
    {
        ImGui::InvisibleButton("ViewportCanvas", viewportSize);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(
            contentMin,
            ImVec2(contentMin.x + viewportSize.x, contentMin.y + viewportSize.y),
            IM_COL32(18, 22, 28, 255));
        drawList->AddText(
            ImVec2(contentMin.x + 10.0f, contentMin.y + 10.0f),
            IM_COL32(220, 225, 235, 220),
            "Viewport render target will appear here");
    }

    DrawGizmo(app, contentMin, viewportSize);


    ImGui::End();
}

void EditorLayer::DrawAssetBrowser(Application& app)
{
    ImGui::SetNextWindowSize(ImVec2(430.0f, 360.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Asset Browser", &m_ShowAssetBrowser);

    auto drawAssetList =
        [&](const char* kind, const std::vector<std::string>& assets)
        {
            for (const std::string& asset : assets)
            {
                const bool selected = m_SelectedAssetKind == kind && m_SelectedAsset == asset;
                if (ImGui::Selectable(asset.c_str(), selected))
                {
                    m_SelectedAssetKind = kind;
                    m_SelectedAsset = asset;
                }
            }
        };

    if (ImGui::BeginTabBar("AssetTabs"))
    {
        if (ImGui::BeginTabItem("Meshes"))
        {
            drawAssetList("Mesh", m_MeshAssets);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Textures"))
        {
            drawAssetList("Texture", m_TextureAssets);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Materials"))
        {
            drawAssetList("Material", m_MaterialAssets);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Shaders"))
        {
            drawAssetList("Shader", m_ShaderAssets);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::Separator();
    ImGui::Text("Selected: %s", m_SelectedAsset.empty() ? "<none>" : m_SelectedAsset.c_str());

    auto& world = app.GetWorld();
    const bool canApply = !m_SelectedAsset.empty() && m_SelectedEntity.IsValid() && world.IsAlive(m_SelectedEntity);
    if (!canApply)
    {
        ImGui::BeginDisabled();
    }

    if (m_SelectedAssetKind == "Mesh")
    {
        if (ImGui::Button("Use Mesh"))
        {
            const EntitySnapshot before = CaptureSelectedEntity(app);
            if (auto* mesh = world.GetComponent<ecs::MeshRendererComponent>(m_SelectedEntity))
            {
                mesh->meshPath = m_SelectedAsset;
                PushUndo("Apply Mesh Asset", before);
            }
        }
    }
    else if (m_SelectedAssetKind == "Texture")
    {
        if (ImGui::Button("Use Texture"))
        {
            const EntitySnapshot before = CaptureSelectedEntity(app);
            if (auto* material = world.GetComponent<ecs::MaterialComponent>(m_SelectedEntity))
            {
                material->texturePath = m_SelectedAsset;
                PushUndo("Apply Texture Asset", before);
            }
            else if (auto* mesh = world.GetComponent<ecs::MeshRendererComponent>(m_SelectedEntity))
            {
                mesh->texturePath = m_SelectedAsset;
                PushUndo("Apply Texture Asset", before);
            }
        }
    }
    else if (m_SelectedAssetKind == "Material")
    {
        if (ImGui::Button("Use Material"))
        {
            const EntitySnapshot before = CaptureSelectedEntity(app);
            if (auto* material = world.GetComponent<ecs::MaterialComponent>(m_SelectedEntity))
            {
                material->materialPath = m_SelectedAsset;
                PushUndo("Apply Material Asset", before);
            }
        }
    }
    else if (m_SelectedAssetKind == "Shader")
    {
        if (ImGui::Button("Use Shader"))
        {
            const EntitySnapshot before = CaptureSelectedEntity(app);
            if (auto* material = world.GetComponent<ecs::MaterialComponent>(m_SelectedEntity))
            {
                material->shaderPath = m_SelectedAsset;
                PushUndo("Apply Shader Asset", before);
            }
            else if (auto* mesh = world.GetComponent<ecs::MeshRendererComponent>(m_SelectedEntity))
            {
                mesh->shaderPath = m_SelectedAsset;
                PushUndo("Apply Shader Asset", before);
            }
        }
    }
    else
    {
        ImGui::BeginDisabled();
        ImGui::Button("Use Asset");
        ImGui::EndDisabled();
    }

    if (!canApply)
    {
        ImGui::EndDisabled();
    }



    ImGui::End();
}

void EditorLayer::DrawMaterialEditor(Application& app)
{
    ImGui::SetNextWindowSize(ImVec2(380.0f, 300.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Material Editor", &m_ShowMaterialEditor);

    auto& world = app.GetWorld();
    if (!m_SelectedEntity.IsValid() || !world.IsAlive(m_SelectedEntity))
    {
        ImGui::TextUnformatted("No entity selected");
        ImGui::End();
        return;
    }

    auto* material = world.GetComponent<ecs::MaterialComponent>(m_SelectedEntity);
    if (material == nullptr)
    {
        ImGui::TextUnformatted("Selected entity has no Material component");
        ImGui::End();
        return;
    }

    if (const auto* tag = world.GetComponent<ecs::TagComponent>(m_SelectedEntity))
        ImGui::Text("Entity: %s", tag->name.c_str());
    else
        ImGui::Text("Entity: %u:%u", m_SelectedEntity.index, m_SelectedEntity.generation);

    EntitySnapshot before = CaptureSelectedEntity(app);
    if (DrawResourceCombo("Material Asset", material->materialPath, m_MaterialAssets, true))
        PushUndo("Edit Material Asset", before);

    before = CaptureSelectedEntity(app);
    if (DrawResourceCombo("Shader", material->shaderPath, m_ShaderAssets, true))
        PushUndo("Edit Material Shader", before);

    before = CaptureSelectedEntity(app);
    if (DrawResourceCombo("Texture", material->texturePath, m_TextureAssets, true))
        PushUndo("Edit Material Texture", before);

    before = CaptureSelectedEntity(app);
    if (ImGui::ColorEdit4("Base Color", material->tint))
        PushUndo("Edit Material Color", before);

    if (ImGui::Button("Load Asset Values"))
    {
        if (!material->materialPath.empty())
        {
            if (auto* resources = app.GetResourceManager())
            {
                const auto loaded = resources->Load<MaterialResource>(material->materialPath);
                if (loaded != nullptr && loaded->IsUsable())
                {
                    before = CaptureSelectedEntity(app);
                    const MaterialResource& data = loaded->GetData();
                    material->shaderPath = data.shaderPath;
                    material->texturePath = data.texturePath;
                    for (int i = 0; i < 4; ++i)
                        material->tint[i] = data.baseColor[i];
                    PushUndo("Load Material Asset Values", before);
                }
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Overrides"))
    {
        before = CaptureSelectedEntity(app);
        material->shaderPath.clear();
        material->texturePath.clear();
        material->tint[0] = 1.0f;
        material->tint[1] = 1.0f;
        material->tint[2] = 1.0f;
        material->tint[3] = 1.0f;
        PushUndo("Clear Material Overrides", before);
    }

    const bool canSaveMaterial = !material->materialPath.empty();
    if (!canSaveMaterial)
        ImGui::BeginDisabled();
    if (ImGui::Button("Save Material File"))
    {
        std::string error;
        if (SaveMaterialFile(material->materialPath, *material, &error))
        {
            if (auto* resources = app.GetResourceManager())
                (void)resources->Reload<MaterialResource>(material->materialPath);
        }
    }
    if (!canSaveMaterial)
        ImGui::EndDisabled();



    ImGui::End();
}

void EditorLayer::DrawGizmo(Application& app, const ImVec2& viewportMin, const ImVec2& viewportSize)
{
    if (viewportSize.x <= 1.0f || viewportSize.y <= 1.0f)
        return;

    auto& world = app.GetWorld();
    if (!m_SelectedEntity.IsValid() || !world.IsAlive(m_SelectedEntity))
        return;

    auto* transform = world.GetComponent<ecs::TransformComponent>(m_SelectedEntity);
    if (transform == nullptr)
        return;

    float view[16];
    float projection[16];
    const float aspectRatio = viewportSize.y > 0.0f ? viewportSize.x / viewportSize.y : 1.7777778f;
    BuildViewMatrix(view, app.GetCameraPosition(), app.GetCameraYaw(), app.GetCameraPitch());
    BuildPerspectiveMatrix(
        projection,
        app.GetCameraVerticalFovRadians(),
        aspectRatio,
        app.GetCameraNearPlane(),
        app.GetCameraFarPlane());

    float translation[3] = { transform->position.x, transform->position.y, transform->position.z };
    float rotation[3] = {
        RadiansToDegrees(transform->rotation.x),
        RadiansToDegrees(transform->rotation.y),
        RadiansToDegrees(transform->rotation.z)
    };
    float scale[3] = { transform->scale.x, transform->scale.y, transform->scale.z };
    float matrix[16];
    ImGuizmo::RecomposeMatrixFromComponents(translation, rotation, scale, matrix);
    const EntitySnapshot before = CaptureSelectedEntity(app);

    ImGuizmo::BeginFrame();
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(viewportMin.x, viewportMin.y, viewportSize.x, viewportSize.y);

    const ImGuizmo::MODE mode = m_GizmoMode == 0 ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
    if (ImGuizmo::Manipulate(view, projection, ToGizmoOperation(m_GizmoOperation), mode, matrix))
    {
        if (!m_GizmoUndoCaptured)
        {
            PushUndo("Transform Gizmo", before);
            m_GizmoUndoCaptured = true;
        }

        ImGuizmo::DecomposeMatrixToComponents(matrix, translation, rotation, scale);
        transform->position = ecs::Vec3{ translation[0], translation[1], translation[2] };
        transform->rotation = ecs::Vec3{
            DegreesToRadians(rotation[0]),
            DegreesToRadians(rotation[1]),
            DegreesToRadians(rotation[2])
        };
        transform->scale = ecs::Vec3{
            std::max(scale[0], 0.001f),
            std::max(scale[1], 0.001f),
            std::max(scale[2], 0.001f)
        };
    }
    else if (!ImGuizmo::IsUsing())
    {
        m_GizmoUndoCaptured = false;
    }
}
}
