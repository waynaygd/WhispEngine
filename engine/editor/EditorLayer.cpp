#include "EditorWidgets.h"
#include "EditorLayer.h"
#include "../scene/SceneSerializer.h"

#include "../core/Application.h"
#include "../ecs/World.h"
#include "../ecs/components/ColliderComponent.h"
#include "../ecs/components/MaterialComponent.h"
#include "../ecs/components/MeshRendererComponent.h"
#include "../ecs/components/RigidbodyComponent.h"
#include "../ecs/components/TagComponent.h"
#include "../ecs/components/TransformComponent.h"
#include "../render/IRenderAdapter.h"
#include "../render/ColorPipeline.h"
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
    ZoneScopedN("EditorLayer");
    m_Statistics.hierarchyMs=m_Statistics.inspectorMs=0;
    const auto uiStart=std::chrono::steady_clock::now();
    struct FinishUi {Statistics& stats;std::chrono::steady_clock::time_point start;~FinishUi(){stats.uiMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}} finish{m_Statistics,uiStart};
    RefreshAssetLists(dt);
    if(m_Transaction&&!m_Transaction->before.settings&&m_Transaction->before.entity!=m_SelectedEntity)FinishTransaction(app);
    m_InputTextActive=ImGui::GetIO().WantTextInput;m_ModalActive=ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel);
    if((ImGui::GetIO().AppFocusLost||ImGui::IsKeyPressed(ImGuiKey_Escape,false))&&m_Transaction){CancelTransaction(app);m_PropertyDrafts.clear();}
    ConfigureDocking();DrawToolbar(app);


        m_ViewportPixelWidth = 0;
    m_ViewportPixelHeight = 0;
    m_ViewportHovered = false;
    m_ViewportHasBounds = false;
    m_ViewportFocused=false;
    DrawMainMenu(app);DrawSceneDialogs(app);
    if(m_ShowSceneSettings)DrawSceneSettings(app,renderer,dt);
    if(m_ShowConsole)DrawConsole(app,renderer);
    ImGui::SetNextWindowSize(ImVec2(340, 330), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Physics Stress Test")) {
        static int selection = 2;
        constexpr int counts[] = {100, 250, 500, 1000};
        ImGui::Combo("Cubes", &selection, "100\0" "250\0" "500\0" "1000\0");
        if (ImGui::Button("Create")) app.SetupPhysicsStressScene(counts[selection]);
        ImGui::SameLine();
        if (ImGui::Button("Clear")) app.ClearPhysicsStressScene();
        ImGui::SameLine();
        if (ImGui::Button("Restart")) app.SetupPhysicsStressScene(counts[selection]);
        ImGui::Text("Stress entities (with floor): %zu", app.GetStressEntityCount());
        bool instancing=app.IsGpuInstancingEnabled();
        if(ImGui::Checkbox("GPU Instancing", &instancing))app.SetGpuInstancingEnabled(instancing);
        const auto& render=app.GetRenderStatistics();
        ImGui::Text("Visible ready objects: %zu", render.visibleObjects);
        ImGui::Text("Draws: %llu / Instanced: %llu / Instances: %llu",
            static_cast<unsigned long long>(render.submitted.drawCalls),static_cast<unsigned long long>(render.submitted.instancedDrawCalls),static_cast<unsigned long long>(render.submitted.renderedInstances));
        ImGui::Text("Instance batches: %llu / Capacity: %llu",static_cast<unsigned long long>(render.submitted.instancedDrawCalls),static_cast<unsigned long long>(render.submitted.instanceBufferCapacity));
        ImGui::Text("Scene upload: %llu bytes",static_cast<unsigned long long>(render.submitted.uploadBytes));
        ImGui::Text("Material resolve %.3f ms / hits %zu / misses %zu",render.materialResolveMs,render.materialCacheHits,render.materialCacheMisses);
        ImGui::Text("Gather %.3f / Prepare %.3f / Submit %.3f ms",render.gatherMs,render.prepareMs,render.submitMs);
        auto& interpolation = app.GetRenderInterpolation();
        bool interpolate = interpolation.IsEnabled();
        if(ImGui::Checkbox("Render Interpolation", &interpolate)) interpolation.SetEnabled(interpolate);
        ImGui::Text("Alpha: %.3f  Interpolated bodies: %zu", interpolation.Alpha(), interpolation.Count());
        bool physicsDebugPose = app.IsPhysicsDebugPose();
        if(ImGui::Checkbox("Physics Debug Pose (colliders)", &physicsDebugPose)) app.SetPhysicsDebugPose(physicsDebugPose);
        if (ImGui::Button(app.IsEditorPlayMode() ? "Stop##stress" : "Play##stress"))
            app.SetEditorPlayMode(!app.IsEditorPlayMode());
        if (auto* physics = app.GetPhysicsSystem()) {
            bool parallel = physics->IsParallel();
            if (ImGui::Checkbox("Parallel (unchecked = Serial)", &parallel)) physics->SetParallel(parallel);
            const auto& frame = app.GetPhysicsFrameStatistics();
            const auto& stats = frame.physics;
            ImGui::Text("Bodies: %zu  Sleeping: %zu", stats.bodies, stats.sleeping);
            ImGui::Text("Pairs: %zu  Contacts: %zu (%zu points)", stats.pairs, stats.contacts, stats.contactPoints);
            ImGui::Text("Substeps: %d  Velocity iterations: %d", stats.substeps, stats.solverIterations);
            ImGui::Text("Physics/frame: %.3f ms  Last tick: %.3f ms", stats.totalMs, frame.lastTickMs);
            ImGui::Text("60 Hz / max 4 ticks; this frame: %d", frame.ticks);
            ImGui::Text("Backlog: %.3f ms / Dropped: %.3f s", frame.accumulatorSeconds * 1000, frame.totalDroppedSeconds);
            ImGui::Text("Internal substeps/frame: %d  Active: %zu", frame.internalSubsteps, stats.activeBodies);
            ImGui::Text("Broadphase rebuild/reuse: %zu/%zu", stats.broadphaseRebuilds, stats.broadphaseReuses);
            ImGui::Text("Integrate %.2f / Broadphase %.2f ms", stats.integrateMs, stats.broadphaseMs);
            ImGui::Text("Narrowphase %.2f / Solver %.2f ms", stats.narrowphaseMs, stats.solverMs);
            ImGui::Text("Pose %.2f / Projection %.2f ms", stats.poseMs, stats.projectionMs);
        }
        const float frameDt = app.GetFrameDeltaTime();
        ImGui::Text("Frame: %.2f ms  FPS: %.1f", frameDt * 1000, frameDt > 0 ? 1 / frameDt : 0);
        ImGui::TextWrapped("Create/Restart replace the active scene. Pause freezes Runtime; Stop restores Editor World. Pair/contact counts are peak per substep.");
    }
    ImGui::End();

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
    HandleEditorShortcuts(app);
    if (m_ShowAssetBrowser)
        DrawAssetBrowser(app);
    if (m_ShowMaterialEditor)
        DrawMaterialEditor(app);
    if (m_ShowDemoWindow)
        ImGui::ShowDemoWindow(&m_ShowDemoWindow);
}

void EditorLayer::HandleEditorShortcuts(Application& app){
    const auto& io=ImGui::GetIO();if(io.WantTextInput||m_ModalActive)return;
    if(io.KeyCtrl){
        if(ImGui::IsKeyPressed(ImGuiKey_Z,false))Undo(app);
        if(ImGui::IsKeyPressed(ImGuiKey_Y,false))Redo(app);
        if(ImGui::IsKeyPressed(ImGuiKey_S,false)){if(io.KeyShift){m_SceneDialogAction=3;m_SceneDialogRequested=true;}else{std::string error;if(!app.SaveCurrentScene(&error)){m_EditorError=error;Logger::Get().Error(error);}}}
        if(ImGui::IsKeyPressed(ImGuiKey_D,false))DuplicateEntity(app,m_SelectedEntity);
    }
    if(!OwnsViewportKeyboard()||app.IsCameraControlActive())return;
    if(ImGui::IsKeyPressed(ImGuiKey_F,false))app.FocusEntity(m_SelectedEntity);
    if(ImGui::IsKeyPressed(ImGuiKey_W,false))m_GizmoOperation=0;
    if(ImGui::IsKeyPressed(ImGuiKey_E,false))m_GizmoOperation=1;
    if(ImGui::IsKeyPressed(ImGuiKey_R,false))m_GizmoOperation=2;
    if(ImGui::IsKeyPressed(ImGuiKey_Delete,false))DeleteEntity(app,m_SelectedEntity);
}

void EditorLayer::RefreshAssetLists(float dt)
{
    m_AssetRefreshTimer += dt;
    if (!m_AssetListsDirty)
        return;

    m_AssetRefreshTimer = 0.0f;
    m_AssetListsDirty = false;

    const std::filesystem::path assetRoot = AssetPaths::ResolveAssetRoot();
    const std::filesystem::path shaderRoot = AssetPaths::ResolveShaderRoot();

    m_MeshAssets = ScanAssetKeys(assetRoot, "models", { ".obj", ".fbx", ".gltf", ".glb" });
    m_TextureAssets = ScanAssetKeys(assetRoot, "textures", { ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".dds" });
    m_MaterialAssets = ScanAssetKeys(assetRoot, "materials", { ".json", ".material" });
    m_ShaderAssets = ScanAssetKeys(shaderRoot, "", { ".hlsl" });
    m_EnvironmentAssets=ScanAssetKeys(assetRoot,"environments",{".hdr"});
}

void EditorLayer::DrawStatistics(Application& app, float dt)
{
    dt = app.GetFrameDeltaTime();
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

    ImGui::SetNextWindowSize(ImVec2(280.0f, 170.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Statistics", &m_ShowStatistics);
    ImGui::Text("Average FPS: %.1f", m_AverageFps);
    ImGui::Text("Frame time: %.3f ms (~%.1f FPS)", dt * 1000.0f, dt > 0.0f ? 1.0f / dt : 0.0f);
    ImGui::Text("Entities: %zu", world.GetAliveCount());
    ImGui::Text("Mesh renderers: %d", meshRendererCount);
    ImGui::Text("Colliders: %d", colliderCount);
    ImGui::Text("Active collisions: %zu", app.GetActiveCollisionCount());
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

    ImGui::Separator();

    if(m_FixedDiagnosticLayout)ImGui::Dummy(ImVec2(1,ImGui::GetFrameHeightWithSpacing()*2-ImGui::GetStyle().ItemSpacing.y));
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
    m_ViewportFocused=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

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

    m_ViewportHovered=ImGui::IsItemHovered();
    if(m_ViewportHovered&&ImGui::IsMouseClicked(1))ImGui::SetWindowFocus();
    DrawViewportOverlays(app,contentMin,viewportSize);
    DrawLightGizmos(app,renderer,contentMin,viewportSize);
    DrawGizmo(app, contentMin, viewportSize);
    m_ToolActive=ImGuizmo::IsUsing()||(m_Transaction&&!m_Transaction->before.settings);
    ImGui::End();
}

void EditorLayer::DrawMaterialEditor(Application& app)
{
    ImGui::SetNextWindowSize(ImVec2(390,480),ImGuiCond_FirstUseEver);
    ImGui::Begin("Material Editor",&m_ShowMaterialEditor);
    if(app.IsEditorPlayMode())ImGui::TextWrapped("Stop Play before modifying shared material assets.");
    ImGui::BeginDisabled(app.IsEditorPlayMode());
    auto* resources=app.GetResourceManager();auto& world=app.GetWorld();
    auto* component=world.GetComponent<ecs::MaterialComponent>(m_SelectedEntity);
    const auto open=[&](const std::string& key) {
        if(m_MaterialDirty&&key!=m_EditingMaterial){m_MaterialError="Unsaved changes: Save or Revert before opening another asset.";return;}
        m_EditingMaterial=key;m_MaterialError.clear();
        if(resources&&!key.empty()) {resources->Load<MaterialResource>(key);resources->WatchForHotReload<MaterialResource>(key);}
    };
    if(m_SelectedAssetKind=="Material"&&ImGui::Button("Open selected asset"))open(m_SelectedAsset);
    if(component&&!component->materialPath.empty()) {ImGui::SameLine();if(ImGui::Button("Open entity material"))open(component->materialPath);}
    int comparison=app.GetShadingModelOverride()+1;const char* comparisonModes[]={"Material setting","Force Lambert","Force PBR"};
    if(ImGui::Combo("Scene shading comparison",&comparison,comparisonModes,3))app.SetShadingModelOverride(comparison-1);
    if(ImGui::Button("PBR spheres demo"))app.SetupPbrDemoScene();
    ImGui::SameLine();if(ImGui::Button("PBR textures demo"))app.SetupPbrTexturesDemoScene();
    unsigned mask=app.GetPbrTextureMask();const char* labels[]={"Normal mapping","MR maps","AO maps"};
    for(unsigned i=0;i<3;++i){bool enabled=(mask&(1u<<i))!=0;if(ImGui::Checkbox(labels[i],&enabled)){mask=enabled?mask|(1u<<i):mask&~(1u<<i);app.SetPbrTextureMask(mask);}if(i<2)ImGui::SameLine();}
    if(ImGui::Button("Material slots demo"))app.SetupMaterialDemoScene();
    ImGui::InputText("New asset key",&m_NewMaterialKey);
    if(ImGui::Button("Create Material")) {
        if(m_MaterialDirty)m_MaterialError="Unsaved changes: Save or Revert before creating an asset.";
        else if(std::filesystem::exists(AssetPaths::ResolveAssetPath(m_NewMaterialKey)))m_MaterialError="Asset already exists; open it instead.";
        else {MaterialResource data;data.name=std::filesystem::path(m_NewMaterialKey).stem().string();data.placeholder=false;
            if(MaterialLoader::Save(m_NewMaterialKey,data,&m_MaterialError)){open(AssetPaths::NormalizeAssetKey(m_NewMaterialKey));m_AssetListsDirty=true;}}
    }
    ImGui::Separator();ImGui::Text("Asset: %s%s",m_EditingMaterial.c_str(),m_MaterialDirty?" *":"");
    auto resource=resources?resources->Get<MaterialResource>(m_EditingMaterial):nullptr;
    if(resource&&resource->IsLoaded()) {
        auto data=resource->GetData();bool changed=false;
        changed|=ImGui::InputText("Name",&data.name);
        changed|=DrawResourceCombo("Shader",data.shaderPath,m_ShaderAssets,false);
        changed|=DrawResourceCombo("Base texture (sRGB)",data.texturePath,m_TextureAssets,true);
        changed|=DrawResourceCombo("Normal texture (linear)",data.normalTexturePath,m_TextureAssets,true);
        changed|=DrawResourceCombo("MR texture (G rough / B metal)",data.metallicRoughnessTexturePath,m_TextureAssets,true);
        changed|=DrawResourceCombo("AO texture (R)",data.occlusionTexturePath,m_TextureAssets,true);
        changed|=ImGui::SliderFloat("Normal scale",&data.normalScale,0,4);
        changed|=ImGui::SliderFloat("AO strength",&data.occlusionStrength,0,1);
        const char* slotLabels[]={"Base UV set","Normal UV set","MR UV set","AO UV set"};
        for(int i=0;i<4;++i) {
            changed|=ImGui::InputInt(slotLabels[i],&data.textureTexCoords[i]);data.textureTexCoords[i]=std::clamp(data.textureTexCoords[i],0,7);
            if(data.textureTexCoords[i]!=0)ImGui::TextUnformatted("UV0 only: this map is disabled.");
        }
        const std::string* maps[]={&data.texturePath,&data.normalTexturePath,&data.metallicRoughnessTexturePath,&data.occlusionTexturePath};
        for(const auto* key:maps)if(!key->empty()) {
            auto map=resources->Get<TextureResource>(*key);
            if(!map)(void)resources->LoadAsync<TextureResource>(*key);
            if(!map||map->IsLoading())ImGui::Text("Loading: %s",key->c_str());
            else if(map->IsFailed())ImGui::Text("Missing map, neutral fallback: %s",key->c_str());
        }
        changed|=LinearColorEdit("Asset Base Color",data.baseColor,4);
        int shading=int(data.shadingModel);const char* shadingModels[]={"Lambert","PBR (GGX)"};
        if(ImGui::Combo("Shading Model",&shading,shadingModels,2)){data.shadingModel=ShadingModel(shading);changed=true;}
        changed|=ImGui::SliderFloat("Metallic",&data.metallic,0,1);
        changed|=ImGui::SliderFloat("Roughness",&data.roughness,0,1);
        if(data.shadingModel==ShadingModel::PBR)ImGui::TextWrapped("PBR stage 1 has no IBL: metals receive direct light only; ambient is a diffuse approximation.");
        ImGui::TextUnformatted("Base Color factor is linear. BRDF roughness minimum: 0.04.");
        int mode=int(data.surfaceMode);const char* modes[]={"Opaque","Masked (unsupported)","Transparent (unsupported)"};
        if(ImGui::Combo("Surface Mode",&mode,modes,3)){data.surfaceMode=MaterialSurfaceMode(mode);changed=true;}
        if(data.surfaceMode!=MaterialSurfaceMode::Opaque)ImGui::TextWrapped("This surface mode is preserved in the asset but omitted from main and shadow passes.");
        if(data.surfaceMode==MaterialSurfaceMode::Masked)changed|=ImGui::SliderFloat("Alpha Cutoff",&data.alphaCutoff,0,1);
        changed|=ImGui::Checkbox("Double Sided",&data.doubleSided);
        changed|=ImGui::Checkbox("Cast Shadows",&data.castShadows);
        changed|=ImGui::Checkbox("Receive Shadows",&data.receiveShadows);
        if(changed) {
            resource->ReplaceData(std::move(data),ResourceLoadState::Loaded);m_MaterialDirty=true;
            resources->SetMaterialDirty(m_EditingMaterial,true);
        }
        if(ImGui::Button("Save")) {
            if(MaterialLoader::Save(m_EditingMaterial,resource->GetData(),&m_MaterialError)){m_MaterialDirty=false;resources->SetMaterialDirty(m_EditingMaterial,false);resources->Reload<MaterialResource>(m_EditingMaterial);}
        }
        ImGui::SameLine();if(ImGui::Button("Revert"))ImGui::OpenPopup("Discard unsaved material edits?");
        if(ImGui::BeginPopupModal("Discard unsaved material edits?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Revert this shared asset to its saved file?");
            if(ImGui::Button("Revert to file")) {resources->SetMaterialDirty(m_EditingMaterial,false);auto reloaded=resources->Reload<MaterialResource>(m_EditingMaterial);
                if(reloaded&&reloaded->IsLoaded()){m_MaterialDirty=false;m_MaterialError.clear();}else {m_MaterialError=reloaded?reloaded->GetErrorMessage():"Reload failed";resources->SetMaterialDirty(m_EditingMaterial,m_MaterialDirty);}
                ImGui::CloseCurrentPopup();}
            ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();ImGui::EndPopup();
        }
        if(!resource->GetData().diagnostics.empty())ImGui::TextWrapped("%s",resource->GetData().diagnostics.c_str());
        if(!resource->GetData().texturePath.empty()) {
            auto texture=resources->Get<TextureResource>(resource->GetData().texturePath);
            if(texture&&texture->HasError())ImGui::TextWrapped("Texture fallback: %s",texture->GetErrorMessage().c_str());
            else if(texture&&texture->IsLoading())ImGui::TextUnformatted("Texture loading asynchronously...");
        }
    } else if(resource&&resource->HasError())ImGui::TextWrapped("%s",resource->GetErrorMessage().c_str());
    if(!m_MaterialError.empty())ImGui::TextWrapped("%s",m_MaterialError.c_str());
    ImGui::Separator();ImGui::TextWrapped("Per-entity references and overrides are edited in Inspector. Asset disk editing has a separate Save/Revert domain.");
    ImGui::EndDisabled();ImGui::End();
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

    ImGuizmo::BeginFrame();
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(viewportMin.x, viewportMin.y, viewportSize.x, viewportSize.y);

    const ImGuizmo::MODE mode = m_GizmoMode == 0 ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
    float snapValue=m_GizmoOperation==0?m_SnapTranslation:m_GizmoOperation==1?m_SnapRotation:m_SnapScale;float snap[3]={snapValue,snapValue,snapValue};
    if (ImGuizmo::Manipulate(view, projection, ToGizmoOperation(m_GizmoOperation), mode, matrix,nullptr,m_Snapping?snap:nullptr))
    {
        if (!m_GizmoUndoCaptured)
        {
            PushUndoBeforeChange(app,"Transform Gizmo");
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
        if(m_GizmoUndoCaptured)FinishTransaction(app);
        m_GizmoUndoCaptured = false;
    }
}
}

namespace editor {
void EditorLayer::DrawSceneSettings(Application& app,IRenderAdapter* renderer,float dt){
    const auto beforeSettings=app.GetSceneSettings();
    ImGui::SetNextWindowSizeConstraints(ImVec2(270,180),ImVec2(FLT_MAX,FLT_MAX));
    if(ImGui::Begin("Scene Settings",&m_ShowSceneSettings)) {
        ImGui::PushItemWidth(std::max(90.f,ImGui::GetContentRegionAvail().x*.43f));
        auto& lighting=app.GetLightingSettings();
        ImGui::Checkbox("Lighting enabled", &lighting.enabled);
        const auto sceneLights=app.GetLightDiagnostics();
        ImGui::Text("Lights %u / enabled %u / frustum %u",sceneLights.total,sceneLights.enabled,sceneLights.influencing);
        ImGui::Text("D/P/S %u/%u/%u",sceneLights.directional,sceneLights.point,sceneLights.spot);
        bool tiled=app.GetLightRenderingMode()==LightRenderingMode::Tiled;
        if(ImGui::Checkbox("Tiled Forward+",&tiled))app.SetLightRenderingMode(tiled?LightRenderingMode::Tiled:LightRenderingMode::Forward);
        ImGui::Checkbox("Show Light Gizmos",&m_ShowLightGizmos);ImGui::SameLine();ImGui::Checkbox("X-ray",&m_LightXray);
        ImGui::Text("Tiles %u / indices %u",sceneLights.tiles,sceneLights.indices);
        ImGui::Text("Avg %.1f / max %u / overflow %u",sceneLights.averagePerTile,sceneLights.maxPerTile,sceneLights.overflow);
        ImGui::Text("Gather %.3f / cull CPU %.3f / GPU %.3f ms",sceneLights.gatherMs,sceneLights.cullingCpuMs,sceneLights.cullingGpuMs);
        if(renderer){const auto t=renderer->GetGpuTimings();if(t.valid)ImGui::Text("Main GPU %.3f / frame GPU %.3f ms",t.mainMs,t.frameMs);}
        ImGui::Text("CPU frame %.3f ms / light upload %llu B",dt*1000.f,static_cast<unsigned long long>(sceneLights.uploadBytes));
        if(!sceneLights.error.empty())ImGui::TextWrapped("%s",sceneLights.error.c_str());
        ImGui::SeparatorText("Legacy Directional (fallback)");
        if(sceneLights.primaryEcs)ImGui::TextWrapped("Scene primary %u:%u. Edit its Inspector/rotation.",sceneLights.primaryIndex,sceneLights.primaryGeneration);
        ImGui::BeginDisabled(sceneLights.primaryEcs);
        ImGui::Checkbox("Enabled", &lighting.directional.enabled);
        ImGui::DragFloat3("Direction (rays)",lighting.directional.direction.data(),.02f);
        LinearColorEdit("Color",lighting.directional.color.data(),3);
        ImGui::SliderFloat("Intensity",&lighting.directional.intensity,0.f,3.f);
        ImGui::EndDisabled();
        auto primary=SceneSerializer::PrimaryDirectionalId(app.GetWorld());
        if(ImGui::BeginCombo("Primary directional",primary.empty()?"Legacy fallback":primary.c_str())){
            for(auto e:app.GetWorld().Entities())if(auto* light=app.GetWorld().GetComponent<ecs::DirectionalLightComponent>(e)){
                if(!light->enabled)continue;auto* tag=app.GetWorld().GetComponent<ecs::TagComponent>(e);const auto label=(tag?tag->name:"Directional")+"##"+std::to_string(e.index);
                if(ImGui::Selectable(label.c_str(),SceneSerializer::EntitySceneId(app.GetWorld(),e)==primary)){
                    BeginSceneTransaction(app,"Choose primary directional");
                    app.GetWorld().ForEach<ecs::DirectionalLightComponent>([&](auto candidate,auto& d){d.primary=candidate==e;});FinishTransaction(app);
                }
            }ImGui::EndCombo();
        }
        if(ImGui::BeginDragDropTarget()){
            if(auto* payload=ImGui::AcceptDragDropPayload("WHISP_ENTITY"))if(payload->DataSize==sizeof(ecs::Entity)){
                auto e=*static_cast<const ecs::Entity*>(payload->Data);if(app.GetWorld().IsAlive(e)&&app.GetWorld().HasComponent<ecs::DirectionalLightComponent>(e)){
                    BeginSceneTransaction(app,"Choose primary directional");app.GetWorld().ForEach<ecs::DirectionalLightComponent>([&](auto candidate,auto& d){d.primary=candidate==e;});FinishTransaction(app);
                }
            }ImGui::EndDragDropTarget();
        }
        if(ImGui::CollapsingHeader("Environment / IBL",ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& env=app.GetEnvironmentSettings();ImGui::Checkbox("IBL Enabled",&env.enabled);
            ImGui::InputText("HDR asset",&env.assetPath);
            std::string dropped; if(AcceptAssetDrop(ecs::AssetKind::Environment,dropped))env.assetPath=dropped;
            ImGui::DragFloat("Environment Intensity",&env.intensity,.05f,0,10000);
            ImGui::SliderFloat("Environment Rotation",&env.rotationDegrees,-180,180);
            ImGui::SliderFloat("Exposure (EV)",&env.exposure,-8,8);
            ImGui::Checkbox("Tone Mapping (Reinhard)",&env.toneMapping);
            ImGui::Checkbox("Diffuse IBL",&env.diffuse);ImGui::Checkbox("Specular IBL",&env.specular);
            if(ImGui::Button("Reload HDR")&&app.GetResourceManager())app.GetResourceManager()->ReloadEnvironmentAsync(env.assetPath);
            auto status=renderer->GetEnvironmentDiagnostics();
            auto asset=app.GetResourceManager()?app.GetResourceManager()->Get<EnvironmentResource>(env.assetPath):nullptr;
            if(asset&&asset->IsLoading())ImGui::TextUnformatted("Loading / CPU preprocessing");
            else if(asset&&asset->HasError())ImGui::TextWrapped("Failed: %s (last ready environment retained)",asset->GetErrorMessage().c_str());
            else if(!asset&&env.enabled)ImGui::TextWrapped("Invalid or pending relative HDR key; last Ready environment retained");
            else ImGui::Text("%s",status.status.c_str());
            ImGui::Text("Cube %u / Irradiance %u / Mips %u / LUT %u",status.cubeResolution,status.irradianceResolution,status.mipCount,status.lutResolution);
            ImGui::Text("Environment GPU: %.2f MiB",status.gpuBytes/1048576.);
            ImGui::Text("CPU preprocess %.2f ms / upload %.2f ms",status.preprocessMs,status.uploadMs);
            if(!status.error.empty())ImGui::TextWrapped("%s",status.error.c_str());
            env=SanitizeEnvironment(env);
        }
        ImGui::SeparatorText("Ambient Lighting");
        LinearColorEdit("Ambient Color",lighting.ambientColor.data(),3);
        ImGui::SliderFloat("Ambient Intensity",&lighting.ambientIntensity,0.f,1.f);
        if(ImGui::CollapsingHeader("Shadows (DX12)",ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& shadows=app.GetShadowSettings();
        ImGui::Checkbox("Shadows enabled",&shadows.enabled);
        if(ImGui::Button("Shadow quality demo"))app.SetupShadowQualityDemoScene();
        int filtering=shadows.pcf?1:0;
        if(ImGui::Combo("Filtering",&filtering,"Hard (point)\0Soft (3 x 3)\0"))shadows.pcf=filtering==1;
        if(shadows.pcf)ImGui::SliderFloat("Radius (texels)",&shadows.filterRadius,0,2);
        ImGui::Checkbox("Fit near camera frustum",&shadows.cameraFitted);
        if(ImGui::Button("Restore shadow defaults"))shadows=ShadowSettings{};
        const unsigned resolutions[]={512,1024,2048,4096};
        int choice=2;for(int i=0;i<4;++i)if(shadows.resolution==resolutions[i])choice=i;
        if(ImGui::Combo("Resolution",&choice,"512\0" "1024\0" "2048\0" "4096\0"))shadows.resolution=resolutions[choice];
        ImGui::SliderFloat(shadows.cameraFitted?"Minimum coverage":"Coverage",&shadows.coverage,2,150);

        ImGui::SliderInt("Constant bias",&shadows.depthBias,0,1000);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("D32 rasterizer units; positive bias moves stored depth away from light.");
        ImGui::SliderFloat("Slope Bias",&shadows.slopeBias,0,4);
        if(ImGui::TreeNode("Advanced shadow settings")) {
            ImGui::SliderFloat(shadows.cameraFitted?"Shadow distance":"Focus distance",&shadows.focusDistance,0,100);
            ImGui::SliderFloat("Depth range",&shadows.depthRange,10,200);
            ImGui::SliderFloat("Receiver Bias",&shadows.receiverBias,0,.003f,"%.5f");
            ImGui::SliderFloat("Normal (texels)",&shadows.normalBias,0,2,"%.2f");
            ImGui::Checkbox("Receiver plane correction",&shadows.receiverPlaneBias);
            ImGui::SliderFloat("Coverage edge fade",&shadows.edgeFade,0,.2f);
            ImGui::Combo("Shadow debug",&shadows.debugMode,"Shaded\0Visibility\0Coverage\0");
            ImGui::TreePop();
        }
        ImGui::SliderFloat("Shadow Strength",&shadows.strength,0,1);
        if(renderer) {
            const auto stats=app.GetRenderStatistics().submitted;
            const auto times=renderer->GetGpuTimings();
            ImGui::Text("%s projection",shadows.cameraFitted?"Stabilized camera fit":"Fixed forward focus");
            if(stats.shadowResolution) {
                ImGui::Text("Effective coverage: %.2f",stats.shadowCoverage);
                ImGui::Text("Texel: %.5f world units",stats.shadowCoverage/stats.shadowResolution);
                ImGui::Text("Light near/far: 0 / %.2f",stats.shadowDepthRange);
            }
            ImGui::Text("Shadow draws: %llu",static_cast<unsigned long long>(stats.shadowDrawCalls));
            if(times.valid)ImGui::Text("Shadow GPU: %.3f ms",times.shadowMs);
        }
        if(renderer&&ImGui::TreeNode("Shadow map preview")) {
            const auto preview=renderer->GetShadowPreviewTextureId();
            if(preview)ImGui::Image((ImTextureID)preview,ImVec2(192,192));
            else ImGui::TextUnformatted("No shadow map available");
            ImGui::TreePop();
        }
        }
        ImGui::PopItemWidth();
        ImGui::TextWrapped("World-space rays. Zero direction uses downward light. Scene lighting is saved with the scene; diagnostic switches are temporary.");
    }
    ImGui::End();


    const auto afterSettings=app.GetSceneSettings();
    if(SceneSettingsToJson(beforeSettings)!=SceneSettingsToJson(afterSettings)&&!m_Transaction){EntitySnapshot before;before.world=app.GetWorld().GetIdentity();before.settings=beforeSettings;BeginTransaction("Scene settings",std::move(before));}
    if(m_Transaction&&m_Transaction->before.settings&&!ImGui::IsAnyItemActive())FinishTransaction(app);

}
}
