#pragma once

#include "../ecs/Entity.h"
#include "../ecs/components/ColliderComponent.h"
#include "../ecs/components/MaterialComponent.h"
#include "../ecs/components/MeshRendererComponent.h"
#include "../ecs/components/RigidbodyComponent.h"
#include "../ecs/components/TagComponent.h"
#include "../ecs/components/TransformComponent.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class Application;
class IRenderAdapter;
struct ImVec2;

namespace editor
{
class EditorLayer
{
public:
    void Render(Application& app, IRenderAdapter* renderer, float dt);
    int GetViewportPixelWidth() const { return m_ViewportPixelWidth; }
    int GetViewportPixelHeight() const { return m_ViewportPixelHeight; }
    bool IsViewportHovered() const { return m_ViewportHovered; }
    bool IsPointInsideViewport(double x, double y) const;

private:
    struct EntitySnapshot
    {
        ecs::Entity entity{};
        std::optional<ecs::TagComponent> tag;
        std::optional<ecs::TransformComponent> transform;
        std::optional<ecs::MeshRendererComponent> meshRenderer;
        std::optional<ecs::MaterialComponent> material;
        std::optional<ecs::RigidbodyComponent> rigidbody;
        std::optional<ecs::ColliderComponent> collider;
    };

    struct UndoRecord
    {
        std::string label;
        EntitySnapshot snapshot;
    };

    void RefreshAssetLists(float dt);
    void HandleEditorShortcuts(Application& app);
    void DrawMainMenu(Application& app);
    void DrawSceneHierarchy(Application& app);
    void DrawInspector(Application& app);
    void DrawStatistics(Application& app, float dt);
    void DrawViewport(Application& app, IRenderAdapter* renderer);
    void DrawAssetBrowser(Application& app);
    void DrawMaterialEditor(Application& app);
    void DrawGizmo(Application& app, const ImVec2& viewportMin, const ImVec2& viewportSize);
    EntitySnapshot CaptureSelectedEntity(Application& app) const;
    void RestoreSnapshot(Application& app, const EntitySnapshot& snapshot);
    void PushUndo(const std::string& label, const EntitySnapshot& snapshot);
    void PushUndoBeforeChange(Application& app, const std::string& label);
    void Undo(Application& app);
    void Redo(Application& app);
    bool CanUndo() const { return !m_UndoStack.empty(); }
    bool CanRedo() const { return !m_RedoStack.empty(); }

    ecs::Entity m_SelectedEntity{};
    int m_GizmoOperation = 0;
    int m_GizmoMode = 0;
    bool m_ShowHierarchy = true;
    bool m_ShowInspector = true;
    bool m_ShowStatistics = true;
    bool m_ShowViewport = true;
    bool m_ShowAssetBrowser = true;
    bool m_ShowMaterialEditor = true;
    bool m_ShowDemoWindow = false;
    float m_FpsTimer = 0.0f;
    int m_FpsFrames = 0;
    float m_AverageFps = 0.0f;
    int m_ViewportPixelWidth = 0;
    int m_ViewportPixelHeight = 0;
    bool m_ViewportHovered = false;
    bool m_ViewportHasBounds = false;
    float m_ViewportMinX = 0.0f;
    float m_ViewportMinY = 0.0f;
    float m_ViewportMaxX = 0.0f;
    float m_ViewportMaxY = 0.0f;
    float m_AssetRefreshTimer = 0.0f;
    bool m_AssetListsDirty = true;
    std::vector<std::string> m_MeshAssets;
    std::vector<std::string> m_TextureAssets;
    std::vector<std::string> m_MaterialAssets;
    std::vector<std::string> m_ShaderAssets;
    std::string m_SelectedAsset;
    std::string m_SelectedAssetKind;
    std::vector<UndoRecord> m_UndoStack;
    std::vector<UndoRecord> m_RedoStack;
    bool m_GizmoUndoCaptured = false;
    static constexpr std::size_t MaxUndoRecords = 64;
};
}
