#include "EditorLayer.h"

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
bool LinearColorEdit(const char* label,float* linear,int count) {
    float display[4]={1,1,1,1};for(int i=0;i<3;++i)display[i]=LinearToSrgb(linear[i]);if(count==4)display[3]=linear[3];
    const bool changed=count==4?ImGui::ColorEdit4(label,display):ImGui::ColorEdit3(label,display);
    if(changed){for(int i=0;i<3;++i)linear[i]=SrgbToLinear(display[i]);if(count==4)linear[3]=display[3];}return changed;
}
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
    RefreshAssetLists(dt);
    const auto* lightingViewport=ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(lightingViewport->WorkPos.x+std::max(0.f,lightingViewport->WorkSize.x-330.f),lightingViewport->WorkPos.y+32.f),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(310,560),ImGuiCond_FirstUseEver);
    if(ImGui::Begin("Lighting")) {
        ImGui::PushItemWidth(150.f);
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
        if(ImGui::CollapsingHeader("Environment / IBL",ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& env=app.GetEnvironmentSettings();ImGui::Checkbox("IBL Enabled",&env.enabled);
            ImGui::InputText("HDR asset",&env.assetPath);
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
        ImGui::TextWrapped("World-space rays. Zero direction uses downward light. Settings persist for this editor session.");
    }
    ImGui::End();

    m_ViewportPixelWidth = 0;
    m_ViewportPixelHeight = 0;
    m_ViewportHovered = false;
    m_ViewportHasBounds = false;

    ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

    DrawMainMenu(app);
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
        ImGui::TextWrapped("Create replaces the demo. Load Scene restores it. Stop pauses; Restart restores the layout. Pair/contact counts are peak per substep.");
    }
    ImGui::End();
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

EditorLayer::EntitySnapshot EditorLayer::CaptureSelectedEntity(Application& app,ecs::Entity entity) const
{
    EntitySnapshot snapshot;
    snapshot.entity = entity.IsValid()?entity:m_SelectedEntity;

    const auto& world = app.GetWorld();
    if (!snapshot.entity.IsValid() || !world.IsAlive(snapshot.entity))
        return snapshot;

    if (const auto* component = world.GetComponent<ecs::TagComponent>(snapshot.entity))
        snapshot.tag = *component;
    if (const auto* component = world.GetComponent<ecs::TransformComponent>(snapshot.entity))
        snapshot.transform = *component;
    if (const auto* component = world.GetComponent<ecs::MeshRendererComponent>(snapshot.entity))
        snapshot.meshRenderer = *component;
    if (const auto* component = world.GetComponent<ecs::MaterialComponent>(snapshot.entity))
        snapshot.material = *component;
    if (const auto* component = world.GetComponent<ecs::RigidbodyComponent>(snapshot.entity))
        snapshot.rigidbody = *component;
    if (const auto* component = world.GetComponent<ecs::ColliderComponent>(snapshot.entity))
        snapshot.collider = *component;

    snapshot.alive=true;
    if(auto* p=world.GetComponent<ecs::DirectionalLightComponent>(snapshot.entity))snapshot.directional=*p;
    if(auto* p=world.GetComponent<ecs::PointLightComponent>(snapshot.entity))snapshot.point=*p;
    if(auto* p=world.GetComponent<ecs::SpotLightComponent>(snapshot.entity))snapshot.spot=*p;
    return snapshot;
}

ecs::Entity EditorLayer::RestoreSnapshot(Application& app, const EntitySnapshot& snapshot)
{
    auto& world = app.GetWorld();auto entity=snapshot.entity;
    if(!snapshot.alive){if(world.IsAlive(entity))app.DestroySceneEntity(entity);m_SelectedEntity={};return entity;}
    if(!world.IsAlive(entity)){
        if(snapshot.directional||snapshot.point||snapshot.spot){
            entity=app.CreateLightEntity(snapshot.directional?LightType::Directional:snapshot.point?LightType::Point:LightType::Spot);
            for(auto* stack:{&m_UndoStack,&m_RedoStack})for(auto& record:*stack)if(record.snapshot.entity==snapshot.entity)record.snapshot.entity=entity;
        }
        else return entity;
    }

    RestoreComponent(world, entity, snapshot.tag);
    RestoreComponent(world, entity, snapshot.transform);
    RestoreComponent(world, entity, snapshot.meshRenderer);
    RestoreComponent(world, entity, snapshot.material);
    RestoreComponent(world, entity, snapshot.rigidbody);
    RestoreComponent(world, entity, snapshot.collider);
    RestoreComponent(world,entity,snapshot.directional);RestoreComponent(world,entity,snapshot.point);RestoreComponent(world,entity,snapshot.spot);
    m_SelectedEntity=entity;return entity;
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
    auto inverse=CaptureSelectedEntity(app,record.snapshot.entity);
    inverse.entity=RestoreSnapshot(app,record.snapshot);
    m_RedoStack.push_back(UndoRecord{record.label,std::move(inverse)});
}

void EditorLayer::Redo(Application& app)
{
    if (m_RedoStack.empty())
        return;

    UndoRecord record = m_RedoStack.back();
    m_RedoStack.pop_back();
    auto inverse=CaptureSelectedEntity(app,record.snapshot.entity);
    inverse.entity=RestoreSnapshot(app,record.snapshot);
    m_UndoStack.push_back(UndoRecord{record.label,std::move(inverse)});
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
    if(ImGui::Button("Create Light"))ImGui::OpenPopup("CreateLight");
    if(ImGui::BeginPopup("CreateLight")){
        if(ImGui::MenuItem("Directional Light"))CreateLight(app,LightType::Directional);
        if(ImGui::MenuItem("Point Light"))CreateLight(app,LightType::Point);
        if(ImGui::MenuItem("Spot Light"))CreateLight(app,LightType::Spot);
        ImGui::EndPopup();
    }
    auto& world = app.GetWorld();
    if(ecs::GetLightParameters(world,m_SelectedEntity)){ImGui::SameLine();if(ImGui::Button("Delete Light"))DeleteSelectedLight(app);}
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

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(entities.size()));
    while (clipper.Step())
    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
    {
        const ecs::Entity entity = entities[row];
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
    LightType lightType;
    if(auto* light=ecs::GetLightParameters(world,m_SelectedEntity,&lightType)){
        if(ImGui::CollapsingHeader("Light",ImGuiTreeNodeFlags_DefaultOpen)){
            auto before=CaptureSelectedEntity(app);bool changed=ImGui::Checkbox("Enabled##light",&light->enabled);
            changed|=LinearColorEdit("Light Color",light->color.data(),3);
            changed|=ImGui::DragFloat("Intensity##light",&light->intensity,.05f,0,100000);
            if(lightType!=LightType::Directional){
                changed|=ImGui::DragFloat("Range",&light->range,.05f,.01f,10000);
                changed|=ImGui::DragFloat("Near radius",&light->attenuation,.01f,.01f,1000);
                ImGui::TextWrapped("Inverse-square with smooth range cutoff. Near radius softens the source.");
            }
            if(lightType==LightType::Spot){changed|=ImGui::SliderFloat("Inner half angle",&light->innerAngle,0,89);changed|=ImGui::SliderFloat("Outer half angle",&light->outerAngle,.1f,89);}
            changed|=ImGui::Checkbox("Cast Shadows##light",&light->castShadows);
            if(auto* d=world.GetComponent<ecs::DirectionalLightComponent>(m_SelectedEntity))changed|=ImGui::Checkbox("Prefer primary directional",&d->primary);
            else ImGui::TextUnformatted("Point/Spot shadows: unavailable (reserved flag)");
            if(lightType!=LightType::Point)ImGui::TextWrapped("Outgoing rays follow Transform local +Z; use Rotate.");
            if(changed){*light=SanitizeLight(*light);PushUndo("Light parameters",before);}
        }
    }

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
            if (DrawResourceCombo("Material", material->materialPath, m_MaterialAssets, true)) {
                material->shaderPath.clear();material->texturePath.clear();
                if(auto* mesh=world.GetComponent<ecs::MeshRendererComponent>(m_SelectedEntity)){mesh->shaderPath.clear();mesh->texturePath.clear();}
                PushUndo("Change Material", before);
            }
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

    ImGui::End();
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

    DrawLightGizmos(app,renderer,contentMin,viewportSize);
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
    if(m_SelectedAssetKind=="Material"&&ImGui::Button("Open Material")) {
        m_ShowMaterialEditor=true;
        if(m_MaterialDirty&&m_SelectedAsset!=m_EditingMaterial)m_MaterialError="Unsaved changes: Save or Revert before opening another asset.";
        else {m_EditingMaterial=m_SelectedAsset;m_MaterialError.clear();if(auto* resources=app.GetResourceManager()){resources->Load<MaterialResource>(m_EditingMaterial);resources->WatchForHotReload<MaterialResource>(m_EditingMaterial);}}
    }

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
            auto* material=world.GetComponent<ecs::MaterialComponent>(m_SelectedEntity);
            if(!material)material=&world.AddComponent<ecs::MaterialComponent>(m_SelectedEntity);
            if (material)
            {
                material->shaderPath.clear();material->texturePath.clear();material->overrides={};material->slotMaterials.clear();
                if(auto* mesh=world.GetComponent<ecs::MeshRendererComponent>(m_SelectedEntity)){mesh->shaderPath.clear();mesh->texturePath.clear();}
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
    ImGui::SetNextWindowSize(ImVec2(390,480),ImGuiCond_FirstUseEver);
    ImGui::Begin("Material Editor",&m_ShowMaterialEditor);
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
    ImGui::Separator();
    if(!component&&world.IsAlive(m_SelectedEntity)&&world.GetComponent<ecs::MeshRendererComponent>(m_SelectedEntity)&&ImGui::Button("Add Material Overrides")) {
        const auto before=CaptureSelectedEntity(app);component=&world.AddComponent<ecs::MaterialComponent>(m_SelectedEntity);PushUndo("Add Material Overrides",before);
    }
    if(component) {
        ImGui::TextUnformatted("Entity overrides (shared asset is unchanged)");
        auto before=CaptureSelectedEntity(app);
        bool changed=ImGui::ColorEdit4("Instance Tint",component->tint);
        const auto flag=[&](const char* label,std::optional<bool>& value,bool inherited) {
            ImGui::PushID(label);bool override=value.has_value();bool edit=ImGui::Checkbox("Override",&override);
            if(edit){if(override)value=true;else value.reset();}
            ImGui::SameLine();bool v=value.value_or(inherited);ImGui::BeginDisabled(!override);
            if(ImGui::Checkbox(label,&v)){value=v;edit=true;}ImGui::EndDisabled();ImGui::PopID();return edit;
        };
        const auto scalar=[&](const char* label,std::optional<float>& value,float inherited) {
            ImGui::PushID(label);bool active=value.has_value();bool edit=ImGui::Checkbox("Override",&active);
            if(edit){if(active)value=inherited;else value.reset();}
            ImGui::SameLine();float v=value.value_or(inherited);ImGui::BeginDisabled(!active);
            if(ImGui::SliderFloat(label,&v,0,1)){value=v;edit=true;}ImGui::EndDisabled();ImGui::PopID();return edit;
        };
        changed|=scalar("Metallic",component->overrides.metallic,0);
        changed|=scalar("Roughness",component->overrides.roughness,.5f);
        changed|=scalar("AO strength",component->overrides.occlusionStrength,1.f);
        bool normalOverride=component->overrides.normalScale.has_value();
        if(ImGui::Checkbox("Override normal scale",&normalOverride)){component->overrides.normalScale=normalOverride?std::optional<float>(1.f):std::nullopt;changed=true;}
        if(normalOverride){float value=*component->overrides.normalScale;if(ImGui::SliderFloat("Normal scale override",&value,0,4)){component->overrides.normalScale=value;changed=true;}}
        const auto textureOverride=[&](const char* label,std::optional<std::string>& key) {
            ImGui::PushID(label);bool enabled=key.has_value();
            if(ImGui::Checkbox("Override texture",&enabled)){key=enabled?std::optional<std::string>(""):std::nullopt;changed=true;}
            if(enabled){auto value=*key;if(DrawResourceCombo(label,value,m_TextureAssets,true)){key=value;changed=true;}}ImGui::PopID();
        };
        textureOverride("Normal",component->overrides.normalTexturePath);
        textureOverride("Metallic / roughness",component->overrides.metallicRoughnessTexturePath);
        textureOverride("Occlusion",component->overrides.occlusionTexturePath);
        bool shadingOverride=component->overrides.shadingModel.has_value();
        if(ImGui::Checkbox("Override shading model",&shadingOverride)){if(shadingOverride)component->overrides.shadingModel=ShadingModel::PBR;else component->overrides.shadingModel.reset();changed=true;}
        int model=int(component->overrides.shadingModel.value_or(ShadingModel::Lambert));const char* choices[]={"Lambert","PBR"};
        ImGui::BeginDisabled(!shadingOverride);if(ImGui::Combo("Instance shading",&model,choices,2)){component->overrides.shadingModel=ShadingModel(model);changed=true;}ImGui::EndDisabled();
        changed|=flag("Double Sided",component->overrides.doubleSided,false);
        changed|=flag("Cast Shadows",component->overrides.castShadows,true);
        changed|=flag("Receive Shadows",component->overrides.receiveShadows,true);
        if(ImGui::Button("Assign opened asset")&&!m_EditingMaterial.empty()) {
            component->materialPath=m_EditingMaterial;component->shaderPath.clear();component->texturePath.clear();component->overrides={};component->slotMaterials.clear();
            if(auto* mesh=world.GetComponent<ecs::MeshRendererComponent>(m_SelectedEntity)){mesh->shaderPath.clear();mesh->texturePath.clear();}changed=true;
        }
        ImGui::SameLine();if(ImGui::Button("Clear Overrides")){component->shaderPath.clear();component->texturePath.clear();component->overrides={};std::fill_n(component->tint,4,1.f);changed=true;}
        if(auto* renderer=world.GetComponent<ecs::MeshRendererComponent>(m_SelectedEntity)) {
            auto mesh=resources?resources->Get<MeshResource>(renderer->meshPath):nullptr;
            if(mesh)for(std::size_t slot=0;slot<mesh->GetData().meshData.materials.size();++slot) {
                component->slotMaterials.resize(mesh->GetData().meshData.materials.size());ImGui::PushID(int(slot));
                const auto label="Slot "+std::to_string(slot)+" ("+mesh->GetData().meshData.materials[slot].name+")";
                changed|=DrawResourceCombo(label.c_str(),component->slotMaterials[slot],m_MaterialAssets,true);ImGui::PopID();
            }
        }
        if(changed)PushUndo("Edit Material Instance",before);
    }
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
