#include "PrecisionInputs.h"
#include <cstring>
#include "TransformMath.h"
#include "ScalePlaneGeometry.h"
#include "SunSynchronization.h"
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
#include <imgui_internal.h>
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
    m_Statistics.hierarchyMs=m_Statistics.inspectorMs=m_Statistics.assetBrowserMs=m_Statistics.lightGizmoMs=0;
    const auto uiStart=std::chrono::steady_clock::now();
    struct FinishUi {Statistics& stats;std::chrono::steady_clock::time_point start;~FinishUi(){stats.uiMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}} finish{m_Statistics,uiStart};
    if(app.IsProjectBrowserActive()){
        m_ViewportPixelWidth=m_ViewportPixelHeight=0;
        m_ViewportHovered=m_ViewportHasBounds=m_ViewportFocused=m_HierarchyFocused=false;
        m_ToolActive=m_LightPointerCaptured=m_LightHandleHovered=false;
        m_InputTextActive=ImGui::GetIO().WantTextInput;m_ModalActive=true;
        DrawProjectBrowser(app);
        return;
    }
    RefreshAssetLists(dt);
    PruneSelection(app);
    if(m_Transaction&&!m_Transaction->before.settings&&m_Transaction->before.entity.IsValid()&&m_Transaction->before.entity!=m_Selection.active)FinishTransaction(app);
    m_InputTextActive=ImGui::GetIO().WantTextInput;m_ModalActive=ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel);
    if(m_LightPointerCaptured&&!ImGui::IsMouseDown(0)){FinishTransaction(app);m_LightPointerCaptured=false;}
    m_EscapeConsumed=false;
    if((ImGui::GetIO().AppFocusLost||ImGui::IsKeyPressed(ImGuiKey_Escape,false))&&m_Transaction){CancelTransaction(app);m_PropertyDrafts.clear();m_CancelGizmoUntilRelease=true;m_LightPointerCaptured=false;m_EscapeConsumed=true;}
    // F11 changes only transient presentation. Keep the authored dock tree alive.
    if(!ImGui::GetIO().WantTextInput&&!m_ModalActive&&!ImGui::GetIO().AppFocusLost&&ImGui::IsKeyPressed(ImGuiKey_F11,false)){
        if(HasPendingTransaction())CancelTransaction(app);
        m_LightPointerCaptured=m_PlaneScaleCaptured=m_ToolActive=false;m_CancelGizmoUntilRelease=true;
        ImGuizmo::Enable(false);ImGuizmo::Enable(true);ImGui::ClearActiveID();
        m_ViewportMaximized=!m_ViewportMaximized;m_FocusViewportWindow=true;
    }
    if(m_ViewportMaximized){
        m_HierarchyFocused=false;
        ImGui::DockSpace(ImHashStr("WhispEngine.Editor.Dockspace"),ImVec2(0,0),ImGuiDockNodeFlags_KeepAliveOnly);
        DrawProjectBrowser(app);DrawProjectSettings(app);DrawViewport(app,renderer);HandleEditorShortcuts(app);return;
    }
    ConfigureDocking();DrawToolbar(app);


        m_ViewportPixelWidth = 0;
    m_ViewportPixelHeight = 0;
    m_ViewportHovered = false;
    m_ViewportHasBounds = false;
    m_ViewportFocused=false;m_HierarchyFocused=false;
    DrawMainMenu(app);DrawSceneDialogs(app);DrawProjectBrowser(app);
    if(app.IsProjectBrowserActive())return;
    DrawProjectSettings(app);
    if(m_ShowSceneSettings)DrawSceneSettings(app,renderer,dt);
    if(m_ShowVisualSettings)DrawEditorVisualSettings(app,renderer);
    PrepareEditorSky(app,renderer);
    if(m_ShowConsole)DrawConsole(app,renderer);
    ImGui::SetNextWindowSize(ImVec2(340, 330), ImGuiCond_FirstUseEver);
    if(m_ShowPhysicsStress){
    if (ImGui::Begin("Physics Stress Test",&m_ShowPhysicsStress)) {
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
    }

    if (m_ShowHierarchy)
        DrawSceneHierarchy(app);
    if (m_ShowInspector)
        DrawInspector(app);
    if (m_ShowStatistics)
        DrawStatistics(app, dt);
    m_InputTextActive=ImGui::IsAnyItemActive();m_ModalActive=ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel);
    if (m_ShowViewport)
        DrawViewport(app, renderer);
    else
    {
        m_ViewportPixelWidth = 0;
        m_ViewportPixelHeight = 0;
    }
    // Re-evaluate after widgets: a popup/text field may have opened this frame.
    m_ModalActive=ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel);
    m_InputTextActive=ImGui::GetIO().WantTextInput||ImGui::IsAnyItemActive();
    HandleEditorShortcuts(app);
    if (m_ShowAssetBrowser)
        DrawAssetBrowser(app);
    if (m_ShowMaterialEditor)
        DrawMaterialEditor(app);
    if (m_ShowDemoWindow)
        ImGui::ShowDemoWindow(&m_ShowDemoWindow);
}

EditorLayer::CameraMouseFrame::CameraMouseFrame(bool suppress){
    if(!suppress||!ImGui::GetCurrentContext())return;
    context=ImGui::GetCurrentContext();auto& io=ImGui::GetIO();
    restoreMouse=(io.ConfigFlags&ImGuiConfigFlags_NoMouse)==0;
    // GLFW cursor-disabled coordinates are virtual camera deltas, not UI input.
    // Remove queued mouse events before NewFrame, preserving keyboard/focus events.
    for(int i=context->InputEventsQueue.Size-1;i>=0;--i){
        auto type=context->InputEventsQueue[i].Type;
        if(type>=ImGuiInputEventType_MousePos&&type<=ImGuiInputEventType_MouseViewport)context->InputEventsQueue.erase(context->InputEventsQueue.Data+i);
    }
    io.ClearInputMouse();
    if(context->ActiveIdSource==ImGuiInputSource_Mouse)ImGui::ClearActiveID();
    context->MovingWindow=nullptr;
    // Scoped to this camera-owned frame; suppress backend fallback hover too.
    io.ConfigFlags|=ImGuiConfigFlags_NoMouse;
}
EditorLayer::CameraMouseFrame::~CameraMouseFrame(){
    if(restoreMouse&&context==ImGui::GetCurrentContext())ImGui::GetIO().ConfigFlags&=~ImGuiConfigFlags_NoMouse;
}
EditorLayer::CameraMouseFrame EditorLayer::RouteCameraMouse(bool captured,bool buttonsDown,const ImVec2& pointer){
    const bool suppress=captured||m_CameraMouseDrain;
    // Swallow the release frame and any secondary button held across release.
    m_CameraMouseDrain=captured||(m_CameraMouseDrain&&buttonsDown);
    if(!suppress&&m_CameraMouseWasSuppressed&&ImGui::GetCurrentContext())ImGui::GetIO().AddMousePosEvent(pointer.x,pointer.y);
    m_CameraMouseWasSuppressed=suppress;
    return CameraMouseFrame(suppress);
}

bool EditorLayer::BlocksGameplayInput()const{
    if(m_ToolActive||m_ModalActive)return true;
    if(!ImGui::GetCurrentContext())return false;
    if(ImGui::GetIO().WantTextInput||ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel))return true;
    const auto& g=*ImGui::GetCurrentContext();
    if(!g.ActiveId)return false;
    // Clicking an Image activates the window's background MoveId. It is the
    // viewport requesting gameplay focus, not an Inspector/text widget.
    auto* viewport=ImGui::FindWindowByName(ViewportWindowName());
    return !viewport||g.ActiveIdWindow!=viewport||
        (g.ActiveId!=viewport->MoveId&&g.ActiveId!=viewport->GetID("ViewportCanvas"));
}

void EditorLayer::HandleEditorShortcuts(Application& app){
    const auto& io=ImGui::GetIO();if(io.WantTextInput||m_ModalActive||m_EscapeConsumed)return;
    // Asset file shortcuts are handled by the browser after its widgets. Never
    // let a Delete intended for that window reach the entity command path.
    if(AssetBrowserOwnsKeyboard()&&(ImGui::IsKeyPressed(ImGuiKey_Delete,false)||ImGui::IsKeyPressed(ImGuiKey_F2,false)))return;
    if(ImGui::IsKeyPressed(ImGuiKey_Escape,false)&&!app.IsEditorPlayMode()){
        if(HasPendingTransaction()){CancelTransaction(app);m_LightPointerCaptured=false;m_CancelGizmoUntilRelease=true;}else if((m_ViewportFocused||m_HierarchyFocused)&&!m_InputTextActive&&!app.IsCameraControlActive())ClearSelection();return;
    }
    const bool entityKeys=!app.IsEditorPlayMode()&&(m_ViewportFocused||m_HierarchyFocused)&&!m_InputTextActive&&!m_ToolActive&&!app.IsCameraControlActive();
    if(entityKeys){if(io.KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_C,false))CopyEntity(app,m_Selection.active);if(io.KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_V,false))PasteEntity(app);if(ImGui::IsKeyPressed(ImGuiKey_Delete,false))DeleteSelection(app);if(m_HierarchyFocused&&io.KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_A,false))SelectAllEntities(app,m_HierarchyVisible);}
    if(io.KeyCtrl){
        if(ImGui::IsKeyPressed(ImGuiKey_Z,false))Undo(app);
        if(ImGui::IsKeyPressed(ImGuiKey_Y,false))Redo(app);
        if(ImGui::IsKeyPressed(ImGuiKey_S,false)){if(io.KeyShift){m_SceneDialogAction=3;m_SceneDialogRequested=true;}else{std::string error;if(!app.SaveCurrentScene(&error)){m_EditorError=error;Logger::Get().Error(error);}}}
        if(ImGui::IsKeyPressed(ImGuiKey_D,false))DuplicateEntity(app,m_Selection.active);
    }
    if(io.KeyAlt&&!io.KeyCtrl&&!io.KeyShift&&(m_ViewportFocused||app.IsViewportMouseCaptured())&&!BlocksGameplayInput()&&!ImGuizmo::IsUsing()&&!ImGui::IsMouseDown(0)&&!ImGui::IsMouseDown(1)){
        if(ImGui::IsKeyPressed(ImGuiKey_3,false))app.SetViewportUnlit(false);
        if(ImGui::IsKeyPressed(ImGuiKey_4,false))app.SetViewportUnlit(true);
    }
    if(!EditorHotkeysAllowed(app.IsEditorPlayMode(),OwnsViewportKeyboard(),io.WantCaptureKeyboard,app.IsCameraControlActive()))return;
    if(!io.KeyCtrl&&!io.KeyAlt&&!m_InputTextActive&&ImGui::IsKeyPressed(ImGuiKey_G,false))m_GameView=!m_GameView;
    if(ImGui::IsKeyPressed(ImGuiKey_F,false))app.FocusEntity(m_Selection.active);
    if(ImGui::IsKeyPressed(ImGuiKey_W,false))m_GizmoOperation=0;
    if(ImGui::IsKeyPressed(ImGuiKey_E,false))m_GizmoOperation=1;
    if(ImGui::IsKeyPressed(ImGuiKey_R,false))m_GizmoOperation=2;

}

void EditorLayer::RefreshAssetLists(float dt)
{
    m_AssetRefreshTimer += dt;
    if (!m_AssetListsDirty)
        return;

    m_AssetRefreshTimer = 0.0f;
    m_AssetListsDirty = false;

    try{m_AssetCatalog.Refresh();m_MeshAssets.clear();m_TextureAssets.clear();m_MaterialAssets.clear();m_ShaderAssets.clear();m_EnvironmentAssets.clear();
        for(const auto& row:m_AssetCatalog.Entries())switch(m_AssetCatalog.Types()[row.type].kind){
            case ecs::AssetKind::Mesh:m_MeshAssets.push_back(row.key);break;case ecs::AssetKind::Texture:m_TextureAssets.push_back(row.key);break;case ecs::AssetKind::Material:m_MaterialAssets.push_back(row.key);break;case ecs::AssetKind::Shader:m_ShaderAssets.push_back(row.key);break;case ecs::AssetKind::Environment:m_EnvironmentAssets.push_back(row.key);break;default:break;
        }
    }catch(const std::exception& error){m_EditorError=error.what();}

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
    const bool maximized=m_ViewportMaximized;
    if(maximized){
        auto* viewport=ImGui::GetMainViewport();ImGui::SetNextWindowPos(viewport->Pos);ImGui::SetNextWindowSize(viewport->Size);
        ImGui::SetNextWindowViewport(viewport->ID);ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0.f);
    }else{
        ImGui::SetNextWindowSize(ImVec2(760.0f, 520.0f), ImGuiCond_Appearing);
        ImGui::SetNextWindowSizeConstraints(ImVec2(420.0f, 280.0f), ImVec2(FLT_MAX, FLT_MAX));
    }
    if(m_FocusViewportWindow){ImGui::SetNextWindowFocus();m_FocusViewportWindow=false;}
    ImGui::Begin(ViewportWindowName(),maximized?nullptr:&m_ShowViewport,ImGuiWindowFlags_NoNavInputs|(maximized?(ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBringToFrontOnFocus):0));

    const bool playMode = app.IsEditorPlayMode();
    if(!maximized){
    DrawViewportCreateMenu(app);
    ImGui::SameLine();
    if (ImGui::Button(playMode ? "Stop" : "Play"))
        app.SetEditorPlayMode(!playMode);
    ImGui::SameLine();
    ImGui::TextUnformatted(playMode ? "Play" : "Edit");
    ImGui::SameLine();ImGui::SetNextItemWidth(85*ImGui::GetStyle().FontScaleDpi);
    int shading=app.IsViewportUnlit()?1:0;
    if(ImGui::Combo("##ViewportShading",&shading,"Lit\0Unlit\0"))app.SetViewportUnlit(shading==1);
    if(!playMode){
        if(ImGui::GetWindowWidth()>520*ImGui::GetStyle().FontScaleDpi)ImGui::SameLine();
        ImGui::Checkbox("Game View (G)",&m_GameView);
    }

    ImGui::Separator();
    }

    if(m_FixedDiagnosticLayout&&!maximized)ImGui::Dummy(ImVec2(1,ImGui::GetFrameHeightWithSpacing()*2-ImGui::GetStyle().ItemSpacing.y));
    const ImVec2 contentMin = ImGui::GetCursorScreenPos();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 viewportSize(std::max(available.x, 1.0f), std::max(available.y, 1.0f));
    const ImVec2 contentMax(contentMin.x + viewportSize.x, contentMin.y + viewportSize.y);
    const ImVec2 framebufferScale = ImGui::GetIO().DisplayFramebufferScale;
    m_ViewportPixelWidth = std::max(1, static_cast<int>(std::lround(viewportSize.x * framebufferScale.x)));
    m_ViewportPixelHeight = std::max(1, static_cast<int>(std::lround(viewportSize.y * framebufferScale.y)));
    if(m_DiagnosticWidth>1&&m_DiagnosticHeight>1){m_ViewportPixelWidth=m_DiagnosticWidth;m_ViewportPixelHeight=m_DiagnosticHeight;}
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

    if(!playMode&&!m_ModalActive&&!m_ToolActive&&!m_LightPointerCaptured&&!m_PlaneScaleCaptured&&!app.IsCameraControlActive()&&!ImGui::IsMouseDown(1)&&ImGui::BeginDragDropTarget()){
        if(const auto* payload=ImGui::AcceptDragDropPayload("WHISP_ASSET"))if(payload->DataSize==sizeof(AssetPayload)){
            const auto& asset=*static_cast<const AssetPayload*>(payload->Data);
            if((asset.kind==ecs::AssetKind::Mesh||asset.kind==ecs::AssetKind::None)&&std::memchr(asset.key,0,sizeof(asset.key))){
                auto mouse=ImGui::GetMousePos();auto point=PlacementPoint(app,(mouse.x-contentMin.x)/viewportSize.x,(mouse.y-contentMin.y)/viewportSize.y);if(asset.kind==ecs::AssetKind::Mesh)PlaceModel(app,asset.key,point);else if(std::string(asset.key).ends_with(".wscene"))PlaceImportedScene(app,asset.key,point);
            }
        }ImGui::EndDragDropTarget();
    }
    m_ViewportHovered=ImGui::IsItemHovered();
    if(maximized){
        // The image already registered the full content bounds. Create is the
        // final layout item; the remaining helpers only submit draw-list data.
        // Do not restore the post-image cursor (it includes ItemSpacing below
        // CursorMaxPos and would extend the window without submitting an item).
        ImGui::SetCursorScreenPos(ImVec2(contentMin.x+8,contentMin.y+8));
        if(DrawViewportCreateMenu(app))m_ViewportHovered=false;
    }
    if(m_ViewportHovered&&ImGui::IsMouseClicked(1))ImGui::SetWindowFocus();
    if(m_ViewportHovered&&ImGui::IsMouseClicked(0))ImGui::SetWindowFocus();
    m_LightHandleHovered=m_LightIconClicked=false;
    m_SpecialIconHovered=false;
    // Specialized markers retain visual and picking priority over fallback icons.
    ImDrawListSplitter iconLayers;
    auto* iconDraw=ImGui::GetWindowDrawList();
    iconLayers.Split(iconDraw,2);
    iconLayers.SetCurrentChannel(iconDraw,1);
    DrawLightGizmos(app,renderer,contentMin,viewportSize);
    DrawPlayerStartGizmos(app,contentMin,viewportSize);
    DrawFogGizmos(app,contentMin,viewportSize);
    iconLayers.SetCurrentChannel(iconDraw,0);
    DrawEntityIcons(app,contentMin,viewportSize);
    iconLayers.Merge(iconDraw);
    ImGui::GetWindowDrawList()->PushClipRect(contentMin,ImVec2(contentMin.x+viewportSize.x,contentMin.y+viewportSize.y),true);
    if(!m_LightIconClicked&&!m_LightPointerCaptured&&!m_LightHandleHovered)DrawGizmo(app, contentMin, viewportSize);
    ImGui::GetWindowDrawList()->PopClipRect();
    DrawViewportOverlays(app,contentMin,viewportSize);
    m_ToolActive=ImGuizmo::IsUsing()||(m_Transaction&&!m_Transaction->before.settings);
    ImGui::End();
    if(maximized)ImGui::PopStyleVar(2);
}

void EditorLayer::DrawMaterialEditor(Application& app)
{
    if(m_FocusMaterial){ImGui::SetNextWindowFocus();ImGui::SetNextWindowCollapsed(false);m_FocusMaterial=false;}
    ImGui::SetNextWindowSize(ImVec2(390,480),ImGuiCond_FirstUseEver);
    if(!ImGui::Begin("Material Editor",&m_ShowMaterialEditor)){ImGui::End();return;}
    if(app.IsEditorPlayMode())ImGui::TextWrapped("Stop Play before modifying shared material assets.");
    ImGui::BeginDisabled(app.IsEditorPlayMode());
    auto* resources=app.GetResourceManager();auto& world=app.GetWorld();
    auto* component=world.GetComponent<ecs::MaterialComponent>(m_Selection.active);
    const auto open=[&](const std::string& key) {
        if(key.starts_with("@engine/")){m_MaterialError="Built-in material is read-only. Create a project material to customize it.";return;}
        if(m_MaterialDirty&&key!=m_EditingMaterial){m_MaterialError="Unsaved changes: Save or Revert before opening another asset.";return;}
        m_EditingMaterial=key;m_MaterialError.clear();
        if(resources&&!key.empty()) {resources->Load<MaterialResource>(std::filesystem::u8path(key));resources->WatchForHotReload<MaterialResource>(std::filesystem::u8path(key));}
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
            if(CreateMaterialAsset(app,m_NewMaterialKey,&m_MaterialError)){open(AssetPaths::NormalizeAssetKey(m_NewMaterialKey));m_AssetListsDirty=true;}}
    }
    ImGui::Separator();ImGui::Text("Asset: %s%s",m_EditingMaterial.c_str(),m_MaterialDirty?" *":"");
    auto resource=resources?resources->Get<MaterialResource>(std::filesystem::u8path(m_EditingMaterial)):nullptr;
    if(resource&&resource->IsLoaded()) {
        auto data=resource->GetData();bool changed=false;
        changed|=ImGui::InputText("Name",&data.name);
        changed|=DrawResourceCombo("Shader",data.shaderPath,m_ShaderAssets,false);
        changed|=DrawResourceCombo("Base texture (sRGB)",data.texturePath,m_TextureAssets,true);changed|=AcceptAssetDrop(ecs::AssetKind::Texture,data.texturePath);
        changed|=DrawResourceCombo("Normal texture (linear)",data.normalTexturePath,m_TextureAssets,true);changed|=AcceptAssetDrop(ecs::AssetKind::Texture,data.normalTexturePath);
        changed|=DrawResourceCombo("MR texture (G rough / B metal)",data.metallicRoughnessTexturePath,m_TextureAssets,true);changed|=AcceptAssetDrop(ecs::AssetKind::Texture,data.metallicRoughnessTexturePath);
        changed|=DrawResourceCombo("AO texture (R)",data.occlusionTexturePath,m_TextureAssets,true);changed|=AcceptAssetDrop(ecs::AssetKind::Texture,data.occlusionTexturePath);
        changed|=PrecisionSliderFloat("Normal scale",&data.normalScale,0,4);
        changed|=PrecisionSliderFloat("AO strength",&data.occlusionStrength,0,1);
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
        changed|=PrecisionSliderFloat("Metallic",&data.metallic,0,1);
        changed|=PrecisionSliderFloat("Roughness",&data.roughness,0,1);
        if(data.shadingModel==ShadingModel::PBR)ImGui::TextWrapped("PBR uses direct GGX and optional scene IBL. Map values use their existing color semantics.");
        ImGui::TextUnformatted("Base Color factor is linear. BRDF roughness minimum: 0.04.");
        int mode=int(data.surfaceMode);const char* modes[]={"Opaque","Masked (unsupported)","Transparent (unsupported)"};
        if(ImGui::Combo("Surface Mode",&mode,modes,3)){data.surfaceMode=MaterialSurfaceMode(mode);changed=true;}
        if(data.surfaceMode!=MaterialSurfaceMode::Opaque)ImGui::TextWrapped("This surface mode is preserved in the asset but omitted from main and shadow passes.");
        if(data.surfaceMode==MaterialSurfaceMode::Masked)changed|=PrecisionSliderFloat("Alpha Cutoff",&data.alphaCutoff,0,1);
        changed|=ImGui::Checkbox("Double Sided",&data.doubleSided);
        changed|=ImGui::Checkbox("Cast Shadows",&data.castShadows);
        changed|=ImGui::Checkbox("Receive Shadows",&data.receiveShadows);
        if(changed) {
            resource->ReplaceData(std::move(data),ResourceLoadState::Loaded);m_MaterialDirty=true;
            resources->SetMaterialDirty(m_EditingMaterial,true);
        }
        if(ImGui::Button("Save")) {
            if(MaterialLoader::Save(m_EditingMaterial,resource->GetData(),&m_MaterialError)){m_MaterialDirty=false;resources->SetMaterialDirty(m_EditingMaterial,false);resources->Reload<MaterialResource>(std::filesystem::u8path(m_EditingMaterial));}
        }
        ImGui::SameLine();if(ImGui::Button("Revert"))ImGui::OpenPopup("Discard unsaved material edits?");
        if(ImGui::BeginPopupModal("Discard unsaved material edits?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Revert this shared asset to its saved file?");
            if(ImGui::Button("Revert to file")) {resources->SetMaterialDirty(m_EditingMaterial,false);auto reloaded=resources->Reload<MaterialResource>(std::filesystem::u8path(m_EditingMaterial));
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
    // Ctrl/Shift clicks belong to selection, even when an object lies under a
    // shared handle. Snap remains controlled by the existing toolbar settings.
    if((ImGui::GetIO().KeyCtrl||ImGui::GetIO().KeyShift)&&ImGui::IsMouseClicked(0)&&!ImGuizmo::IsUsing())return;
    if(m_ModalActive){if(m_GizmoUndoCaptured)CancelTransaction(app);m_GizmoUndoCaptured=false;m_CancelGizmoUntilRelease=true;ImGuizmo::Enable(false);return;}
    if((app.IsCameraControlActive()||ImGui::IsMouseDown(1))&&!ImGuizmo::IsUsing())return;
    if (app.IsEditorPlayMode() || m_GameView || viewportSize.x <= 1.0f || viewportSize.y <= 1.0f)
        return;

    if(m_CancelGizmoUntilRelease){ImGuizmo::Enable(false);if(ImGui::IsMouseDown(0))return;m_CancelGizmoUntilRelease=false;}ImGuizmo::Enable(true);
    auto& world = app.GetWorld();
    if (!m_Selection.active.IsValid() || !world.IsAlive(m_Selection.active))
        return;

    auto* transform = world.GetComponent<ecs::TransformComponent>(m_Selection.active);
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

    ImGuizmo::SetGizmoSizeClipSpace(ScaleGizmoClipSize);
    if(m_Selection.items.size()>1){DrawGroupGizmo(app,view,projection);return;}
    const bool planeOwnsPointer=!ImGuizmo::IsUsing()&&DrawScalePlanes(app,viewportMin,viewportSize,matrix);
    const ImGuizmo::MODE mode = m_GizmoMode == 0 ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
    float snapValue=m_GizmoOperation==0?m_SnapTranslation:m_GizmoOperation==1?m_SnapRotation:m_SnapScale;float snap[3]={snapValue,snapValue,snapValue};
    float originalMatrix[16];std::copy(matrix,matrix+16,originalMatrix);
    // Manipulate combines drawing and input. Its IsHoveringWindow gate must be
    // false while a custom plane owns the pointer; moving MousePos alone is not
    // enough (a nearly parallel ray can still select an axis). BeginFrame's
    // NoInputs helper window is distinct from our viewport draw-list owner.
    auto& context=*ImGui::GetCurrentContext();auto* hoveredWindow=context.HoveredWindow;
    if(planeOwnsPointer)context.HoveredWindow=ImGui::FindWindowByName("gizmo");
    const bool manipulated=ImGuizmo::Manipulate(view, projection, ToGizmoOperation(m_GizmoOperation), mode, matrix,nullptr,(m_GizmoOperation==1?m_RotationSnapping:m_Snapping)?snap:nullptr);
    context.HoveredWindow=hoveredWindow;
    bool matrixChanged=false;for(int i=0;i<16;++i)matrixChanged|=std::abs(matrix[i]-originalMatrix[i])>1e-6f;
    // ImGuizmo's previous delta may report "modified" on a later stationary drag.
    // Duplicate only when the target matrix actually changes (including snapped movement).
    const bool gestureMoved=ImGuizmo::IsUsing()&&ImGui::IsMouseDragging(0,.5f*ImGui::GetStyle().FontScaleDpi);
    if (manipulated&&matrixChanged&&gestureMoved)
    {
        if (!m_GizmoUndoCaptured)
        {
            if(m_GizmoOperation==0&&ImGui::GetIO().KeyAlt){auto copy=BeginDuplicateDrag(app);if(!copy.IsValid())return;transform=world.GetComponent<ecs::TransformComponent>(copy);}
            else PushUndoBeforeChange(app,"Transform Gizmo");
            m_GizmoUndoCaptured = true;
        }

        ImportMatrix result;std::copy(matrix,matrix+16,result.begin());
        if(auto decomposed=DecomposeTransform(result,transform->scale)){*transform=*decomposed;app.NotifyEditorMutation(m_Selection.active);}else m_EditorError="Transform requires shear/singular scale; use Local scale handles";
    }
    else if (!ImGuizmo::IsUsing())
    {
        if(m_GizmoUndoCaptured){FinishTransaction(app);m_DuplicateDrag=false;}
        m_GizmoUndoCaptured = false;
    }
}
}

namespace editor {
void EditorLayer::DrawSceneSettings(Application& app,IRenderAdapter* renderer,float dt){
    if(m_FocusSceneSettings&&ImGui::GetFrameCount()>2){ImGui::SetNextWindowFocus();m_FocusSceneSettings=false;}
    ImGui::SetNextWindowSizeConstraints(ImVec2(270,180),ImVec2(FLT_MAX,FLT_MAX));
    if(!ImGui::Begin("Scene Settings",&m_ShowSceneSettings)){ImGui::End();return;}
    auto beforeSettings=app.GetSceneSettings();
    {auto settings=beforeSettings;const auto project=app.GetProject();
    if(project){
        ImGui::TextUnformatted("Anti-Aliasing");
        bool override=settings.antiAliasingOverride;
        if(ImGui::Checkbox("Scene Override##AntiAliasing",&override)){
            BeginSceneTransaction(app,"Anti-aliasing inheritance");
            if(override)settings.antiAliasing=app.GetEffectiveAntiAliasing();
            settings.antiAliasingOverride=override;app.SetSceneSettings(settings);FinishTransaction(app);beforeSettings=app.GetSceneSettings();
        }
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Unchecked: follow Project Settings. Checked: store this scene's own AA mode. Existing scenes keep their previous mode as an override.");
        ImGui::TextWrapped("Project Default: %s | Source: %s",AntiAliasingName(project->settings.antiAliasing),settings.antiAliasingOverride?"Scene Override":"Project Default");
    }
    int aa=int(app.GetEffectiveAntiAliasing());ImGui::BeginDisabled(project&&!settings.antiAliasingOverride);
    if(project)ImGui::SetNextItemWidth(-FLT_MIN);
    if(ImGui::Combo(project?"##SceneAntiAliasing":"Anti-Aliasing",&aa,"OFF\0FXAA Low\0FXAA High\0")){BeginSceneTransaction(app,"Anti-aliasing");settings.antiAliasing=AntiAliasingMode(aa);settings.antiAliasingOverride=true;app.SetSceneSettings(settings);FinishTransaction(app);beforeSettings=app.GetSceneSettings();}
    ImGui::EndDisabled();if(project)ImGui::Text("Effective AA: %s",AntiAliasingName(app.GetEffectiveAntiAliasing()));
    if(renderer){auto d=renderer->GetAntiAliasingDiagnostics();ImGui::Text("Effective samples: %u (post-process AA), intermediate %.2f MiB",d.effectiveSamples,d.intermediateBytes/1048576.);ImGui::Text("HDR/output GPU %.3f ms",renderer->GetGpuTimings().outputMs);}}

    {
        ImGui::PushItemWidth(std::max(90.f,ImGui::GetContentRegionAvail().x*.43f));
        if(ImGui::CollapsingHeader("Scene Background",ImGuiTreeNodeFlags_DefaultOpen)){
            auto settings=app.GetSceneSettings();auto& b=settings.background;const auto previousHdr=b.useIblEnvironment?settings.environment.assetPath:b.cubemap;bool changed=false;int mode=int(b.background);
            if(ImGui::BeginTable("BackgroundProperties",2,ImGuiTableFlags_SizingStretchProp)){
                ImGui::TableSetupColumn("Property",ImGuiTableColumnFlags_WidthFixed,90*ImGui::GetStyle().FontScaleDpi);ImGui::TableSetupColumn("Value",ImGuiTableColumnFlags_WidthStretch);
                auto row=[&](const char* label,auto control){ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(label);ImGui::TableSetColumnIndex(1);ImGui::SetNextItemWidth(-1);control();};
                row("Mode",[&]{changed|=ImGui::Combo("##bgMode",&mode,"Solid\0Gradient\0HDR sky\0Procedural Day/Night\0");});if(b.background!=EditorBackground(mode)&&mode==3&&b.sunSync==SunSyncMode::SkyDrivesLight)b.sunSync=SunSyncMode::LightDrivesSky;b.background=EditorBackground(mode);
                row("Intensity",[&]{changed|=PrecisionSliderFloat("##skyIntensity",&b.skyIntensity,0,8);});
                if(b.background==EditorBackground::Solid)row("Color",[&]{changed|=LinearColorEdit("##bgSolid",b.solid.data(),3,ImGuiColorEditFlags_NoInputs);});
                else if(b.background==EditorBackground::Gradient){row("Top",[&]{changed|=LinearColorEdit("##bgTop",b.top.data(),3,ImGuiColorEditFlags_NoInputs);});row("Horizon",[&]{changed|=LinearColorEdit("##bgHorizon",b.horizon.data(),3,ImGuiColorEditFlags_NoInputs);});row("Ground",[&]{changed|=LinearColorEdit("##bgBottom",b.bottom.data(),3,ImGuiColorEditFlags_NoInputs);});row("Blend",[&]{changed|=PrecisionSliderFloat("##bgBlend",&b.gradientBlend,.1f,4);});}
                else if(b.background==EditorBackground::Cubemap){row("Use IBL",[&]{changed|=ImGui::Checkbox("##bgUseIbl",&b.useIblEnvironment);if(ImGui::IsItemHovered())ImGui::SetTooltip("Share the IBL HDR asset and rotation. Visible sky intensity stays independent.");});ImGui::BeginDisabled(b.useIblEnvironment);row("HDR asset",[&]{
                    const auto world=app.GetWorld().GetIdentity();if(!m_BackgroundAssetEditing||m_BackgroundDraftWorld!=world){m_BackgroundAssetDraft=b.cubemap;m_BackgroundDraftWorld=world;}
                    bool edited=ImGui::InputText("##bgAsset",&m_BackgroundAssetDraft);m_BackgroundAssetEditing=ImGui::IsItemActive();edited|=AcceptAssetDrop(ecs::AssetKind::Environment,m_BackgroundAssetDraft);
                    std::string error;const bool valid=ecs::ComponentRegistry::ValidateAssetKey(m_BackgroundAssetDraft,ecs::AssetKind::Environment,&error);
                    if(edited&&valid){b.cubemap=m_BackgroundAssetDraft;changed=true;}if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",m_BackgroundAssetDraft.c_str());if(!valid)ImGui::TextWrapped("%s",error.c_str());
                });row("Rotation",[&]{changed|=PrecisionSliderFloat("##bgRotation",&b.skyRotation,-180,180,"%.1f deg");});ImGui::EndDisabled();}
                ImGui::EndTable();
            }
            if(b.background==EditorBackground::DayNight){changed|=LinearColorEdit("Zenith color",b.top.data(),3);changed|=LinearColorEdit("Horizon tint",b.horizon.data(),3);changed|=LinearColorEdit("Ground tint",b.bottom.data(),3);changed|=LinearColorEdit("Night color",b.nightColor.data(),3);changed|=PrecisionSliderFloat("Horizon glow",&b.horizonGlow,0,8);changed|=PrecisionSliderFloat("Sunset warmth",&b.sunsetWarmth,0,4);changed|=PrecisionSliderFloat("Night intensity",&b.nightIntensity,0,4);changed|=ImGui::Checkbox("Drive Light Properties",&b.driveLightProperties);if(b.driveLightProperties)changed|=PrecisionSliderFloat("Daylight intensity",&b.dayLightIntensity,0,8);ImGui::TextWrapped("Artist-friendly sky follows Primary Directional elevation/azimuth. No physical atmosphere or dynamic IBL convolution.");if(settings.environment.enabled)ImGui::TextWrapped("Warning: IBL uses a static HDRI and may not match time of day.");}
            ImGui::Text("Sun Source: %s",b.background==EditorBackground::Cubemap?"HDRI radiance / optional sun glow":"Primary Directional / sun glow");
            auto primarySun=PrimarySun(app.GetWorld());if(primarySun.IsValid()){auto* tag=app.GetWorld().GetComponent<ecs::TagComponent>(primarySun);auto rays=ecs::LightDirection(ecs::RenderPose::From(*app.GetWorld().GetComponent<ecs::TransformComponent>(primarySun)));ImGui::TextWrapped("Primary Directional Light: %s",tag?tag->name.c_str():"Directional");ImGui::Text("Outgoing rays %.3f %.3f %.3f",rays.x,rays.y,rays.z);ImGui::Text("Sun elevation %.1f / azimuth %.1f deg",std::asin(std::clamp(-rays.y,-1.f,1.f))*57.2957795f,std::atan2(-rays.x,-rays.z)*57.2957795f);}else ImGui::TextWrapped("No enabled Primary Directional; Day/Night preview uses default midday.");
            if(previousHdr!=(b.useIblEnvironment?settings.environment.assetPath:b.cubemap)){b.sunMetadata=false;if(b.sunSync==SunSyncMode::SkyDrivesLight)b.sunSync=SunSyncMode::Independent;ImGui::TextWrapped("HDR selection changed: confirm explicit Sun Direction for this asset.");}
            int sync=int(b.sunSync);changed|=ImGui::Combo("Sync Mode",&sync,"Independent\0Light Drives Sky (glow)\0Sky Drives Light (metadata)\0");b.sunSync=SunSyncMode(sync);
            changed|=ImGui::Checkbox("Use Explicit Sun Direction",&b.sunMetadata);
            if(b.sunMetadata)changed|=PrecisionDragFloat3("HDR Sun Direction (sky local)",b.sunDirection.data(),.01f,-1,1,"%.6g",ImGuiSliderFlags_NoRoundToFormat);
            changed|=ImGui::Checkbox("Sun glow visible",&b.visibleSunDisc);
            if(b.background==EditorBackground::Cubemap){if(!b.sunMetadata)ImGui::TextWrapped("Warning: HDRI has no confirmed sun metadata. Set HDR Sun Direction explicitly; no brightest-pixel detection is performed.");ImGui::TextWrapped("HDRI may already contain a sun. Light Drives Sky moves only the procedural disc, not pixels inside the HDR map; enabling both can show two suns.");}
            if(b.sunSync==SunSyncMode::LightDrivesSky||b.background==EditorBackground::DayNight){changed|=PrecisionDragFloat("Sun radiance",&b.sunIntensity,.01f,0,32,"%.6g",ImGuiSliderFlags_NoRoundToFormat);changed|=PrecisionDragFloat("Glow radius (deg)",&b.sunGlowRadius,.1f,.01f,60);changed|=PrecisionSliderFloat("Glow intensity",&b.sunGlowIntensity,0,4);ImGui::TextWrapped("Glow follows the primary light. Use an HDR without a baked sun to avoid duplicate suns. Legacy disc diameter remains readable in saved scenes.");}
            if(changed){BeginSceneTransaction(app,"Sky / Sun settings");std::string error;if(!app.SetSceneSettings(settings,&error))m_EditorError=error;else m_EditorError.clear();}
            if(!m_EditorError.empty())ImGui::TextWrapped("%s",m_EditorError.c_str());
            ImGui::TextDisabled("Shared by Edit / Play");
        }
        auto& lighting=app.GetLightingSettings();
        ImGui::Checkbox("Lighting enabled", &lighting.enabled);
        const auto sceneLights=app.GetLightDiagnostics();
        if(ImGui::TreeNode("Render diagnostics")){
        ImGui::Text("Lights %u / enabled %u / frustum %u",sceneLights.total,sceneLights.enabled,sceneLights.influencing);
        ImGui::Text("D/P/S %u/%u/%u",sceneLights.directional,sceneLights.point,sceneLights.spot);
        bool tiled=app.GetLightRenderingMode()==LightRenderingMode::Tiled;
        if(ImGui::Checkbox("Tiled Forward+",&tiled))app.SetLightRenderingMode(tiled?LightRenderingMode::Tiled:LightRenderingMode::Forward);
        ImGui::Checkbox("Show Light Gizmos",&m_ShowLightGizmos);ImGui::SameLine();ImGui::Combo("Occlusion",&m_LightXrayMode,"Visible Only\0Selected X-Ray\0Always X-Ray\0");
        ImGui::Text("Tiles %u / indices %u",sceneLights.tiles,sceneLights.indices);
        ImGui::Text("Avg %.1f / max %u / overflow %u",sceneLights.averagePerTile,sceneLights.maxPerTile,sceneLights.overflow);
        ImGui::Text("Gather %.3f / cull CPU %.3f / GPU %.3f ms",sceneLights.gatherMs,sceneLights.cullingCpuMs,sceneLights.cullingGpuMs);
        if(renderer){const auto t=renderer->GetGpuTimings();if(t.valid)ImGui::Text("Main GPU %.3f / frame GPU %.3f ms",t.mainMs,t.frameMs);}
        ImGui::Text("CPU frame %.3f ms / light upload %llu B",dt*1000.f,static_cast<unsigned long long>(sceneLights.uploadBytes));
        if(!sceneLights.error.empty())ImGui::TextWrapped("%s",sceneLights.error.c_str());
        ImGui::TreePop();}
        if(ImGui::TreeNode("Legacy Directional (fallback)")){
        if(sceneLights.primaryEcs)ImGui::TextWrapped("Scene primary %u:%u. Edit its Inspector/rotation.",sceneLights.primaryIndex,sceneLights.primaryGeneration);
        ImGui::BeginDisabled(sceneLights.primaryEcs);
        ImGui::Checkbox("Enabled", &lighting.directional.enabled);
        PrecisionDragFloat3("Direction (rays)",lighting.directional.direction.data(),.02f);
        LinearColorEdit("Color",lighting.directional.color.data(),3);
        PrecisionSliderFloat("Intensity",&lighting.directional.intensity,0.f,3.f);
        ImGui::EndDisabled();
        ImGui::TreePop();}
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
            PrecisionDragFloat("Environment Intensity",&env.intensity,.05f,0,10000);
            PrecisionSliderFloat("Environment Rotation",&env.rotationDegrees,-180,180);
            PrecisionSliderFloat("Exposure (EV)",&env.exposure,-8,8);
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
        PrecisionSliderFloat("Ambient Intensity",&lighting.ambientIntensity,0.f,1.f);
        if(ImGui::CollapsingHeader("Shadows (DX12)",ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& shadows=app.GetShadowSettings();
        ImGui::Checkbox("Shadows enabled",&shadows.enabled);
        if(ImGui::Button("Shadow quality demo"))app.SetupShadowQualityDemoScene();
        int quality=int(GetShadowQuality(shadows));if(ImGui::Combo("Quality preset",&quality,"Low\0Medium\0High\0Ultra\0Custom\0"))ApplyShadowQuality(shadows,ShadowQuality(quality));
        ImGui::Text("Depth map: %.1f MiB",ShadowMapBytes(shadows)/1048576.);
        int filtering=shadows.pcf?(shadows.filterTaps==5?2:1):0;
        if(ImGui::Combo("Filtering",&filtering,"Hard (point)\0Legacy PCF 3x3\0Smooth tent 5x5\0")){shadows.pcf=filtering!=0;shadows.filterTaps=filtering==2?5:3;}
        if(shadows.pcf)PrecisionSliderFloat("Radius (texels)",&shadows.filterRadius,0,2);
        ImGui::Checkbox("Fit near camera frustum",&shadows.cameraFitted);
        if(ImGui::Button("Restore shadow defaults"))shadows=ShadowSettings{};
        const unsigned resolutions[]={512,1024,2048,4096};
        int choice=2;for(int i=0;i<4;++i)if(shadows.resolution==resolutions[i])choice=i;
        if(ImGui::Combo("Resolution",&choice,"512\0" "1024\0" "2048\0" "4096\0"))shadows.resolution=resolutions[choice];
        PrecisionSliderFloat(shadows.cameraFitted?"Minimum coverage":"Coverage",&shadows.coverage,2,150);

        PrecisionSliderInt("Constant bias",&shadows.depthBias,0,1000);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("D32 rasterizer units; positive bias moves stored depth away from light.");
        PrecisionSliderFloat("Slope Bias",&shadows.slopeBias,0,4);
        if(ImGui::TreeNode("Advanced shadow settings")) {
            PrecisionSliderFloat(shadows.cameraFitted?"Shadow distance":"Focus distance",&shadows.focusDistance,0,100);
            PrecisionSliderFloat("Depth range",&shadows.depthRange,10,200);
            PrecisionSliderFloat("Receiver Bias",&shadows.receiverBias,0,.003f,"%.5f");
            PrecisionSliderFloat("Normal (texels)",&shadows.normalBias,0,2,"%.2f");
            ImGui::Checkbox("Receiver plane correction",&shadows.receiverPlaneBias);
            PrecisionSliderFloat("Coverage edge fade",&shadows.edgeFade,0,.2f);
            ImGui::Combo("Shadow debug",&shadows.debugMode,"Shaded\0Visibility\0Coverage\0");
            ImGui::TreePop();
        }
        PrecisionSliderFloat("Shadow Strength",&shadows.strength,0,1);
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
    beforeSettings.lighting.enabled=afterSettings.lighting.enabled;beforeSettings.shadows.debugMode=afterSettings.shadows.debugMode;
    if(beforeSettings!=afterSettings&&!m_Transaction){EntitySnapshot before;before.world=app.GetWorld().GetIdentity();before.settings=beforeSettings;BeginTransaction("Scene settings",std::move(before));}
    if(m_Transaction&&m_Transaction->before.settings&&!ImGui::IsAnyItemActive())FinishTransaction(app);

}
}
