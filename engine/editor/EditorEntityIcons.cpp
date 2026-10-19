#include "EditorLayer.h"
#include "EntityIcons.h"
#include "LightGizmoMath.h"
#include "../core/Application.h"
#include <imgui.h>
#include <ImGuizmo.h>

namespace editor {
void EditorLayer::DrawEntityIcons(Application& app, const ImVec2& origin, const ImVec2& size) {
    if (app.IsEditorPlayMode() || m_GameView || !m_VisualSettings.enabled || size.x <= 1 || size.y <= 1) return;
    auto& world = app.GetWorld();
    const auto camera = GizmoCamera(app.GetCameraPosition(), app.GetCameraYaw(), app.GetCameraPitch(),
        app.GetCameraVerticalFovRadians(), size.x / size.y, app.GetCameraNearPlane(), app.GetCameraFarPlane());
    std::vector<EntityIconCandidate> icons;
    world.ForEach<ecs::TransformComponent>([&](ecs::Entity entity, const auto& transform) {
        if (!HasEntityIcon(world, entity)||!EditorEntityVisible(app,entity)) return;
        if (auto projected = ProjectGizmo(world.WorldTransform(entity).position, camera)) icons.push_back({entity, *projected});
    });
    std::sort(icons.begin(), icons.end(), NearerEntityIcon);
    const float dpi = ImGui::GetStyle().FontScaleDpi, radius = 11 * dpi;
    const auto mouse = ImGui::GetMousePos();
    ecs::Entity hit;
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
    // Far to near: the last diamond drawn is also the first hit tested.
    for (auto it = icons.rbegin(); it != icons.rend(); ++it) {
        const auto& icon = *it;
        ImVec2 p(origin.x + icon.projection[0] * size.x, origin.y + icon.projection[1] * size.y);
        const ImVec2 diamond[] = {{p.x, p.y-radius}, {p.x+radius, p.y}, {p.x, p.y+radius}, {p.x-radius, p.y}};
        draw->AddConvexPolyFilled(diamond, 4, IM_COL32(18, 24, 32, 240));
        draw->AddPolyline(diamond, 4, IsSelected(icon.entity) ? IM_COL32(255, 225, 80, 255) : IM_COL32(215, 225, 235, 255), ImDrawFlags_Closed, 1.8f*dpi);
        // A small inset cube; no font glyph or world-scale dependency.
        const ImU32 ink = IM_COL32(215, 225, 235, 255);
        draw->AddLine({p.x-4*dpi,p.y-2*dpi},{p.x,p.y-4*dpi},ink,dpi);
        draw->AddLine({p.x,p.y-4*dpi},{p.x+4*dpi,p.y-2*dpi},ink,dpi);
        draw->AddLine({p.x-4*dpi,p.y-2*dpi},{p.x,p.y},ink,dpi);
        draw->AddLine({p.x,p.y},{p.x+4*dpi,p.y-2*dpi},ink,dpi);
        draw->AddLine({p.x-4*dpi,p.y-2*dpi},{p.x-4*dpi,p.y+3*dpi},ink,dpi);
        draw->AddLine({p.x+4*dpi,p.y-2*dpi},{p.x+4*dpi,p.y+3*dpi},ink,dpi);
        draw->AddLine({p.x-4*dpi,p.y+3*dpi},{p.x,p.y+5*dpi},ink,dpi);
        draw->AddLine({p.x+4*dpi,p.y+3*dpi},{p.x,p.y+5*dpi},ink,dpi);
        draw->AddLine({p.x,p.y},{p.x,p.y+5*dpi},ink,dpi);
        if (CanViewportSelect(app,icon.entity)&&std::abs(mouse.x-p.x) + std::abs(mouse.y-p.y) <= radius) hit = icon.entity;
    }
    draw->PopClipRect();
    if (!hit.IsValid() || m_SpecialIconHovered || m_LightIconClicked || m_LightHandleHovered
        || app.IsCameraControlActive() || ImGui::IsMouseDown(1) || !m_ViewportHovered
        || m_InputTextActive || m_ModalActive || m_LightPointerCaptured || m_PlaneScaleCaptured
        || ImGuizmo::IsUsing() || ImGui::IsAnyItemActive()
        || (m_Selection.active.IsValid() && ImGuizmo::IsOver() && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift)) return;
    // Re-read the current World every frame: rename, deletion and Play/Stop never
    // leave a cached name or an Entity handle from another World behind.
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(EntityIconLabel(world, hit).c_str());
    ImGui::EndTooltip();
    if (ImGui::IsMouseClicked(0)) {
        SelectViewportEntity(app, hit, ImGui::GetIO().KeyCtrl, ImGui::GetIO().KeyShift);
        m_LightIconClicked = true;
    }
}
}
