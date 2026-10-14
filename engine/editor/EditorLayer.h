#pragma once

#include "../ecs/Entity.h"
#include "../ecs/ComponentRegistry.h"
#include "../scene/SceneSettings.h"
#include "EditorPreferences.h"
#include "InputRouting.h"
#include "AssetCatalog.h"
#include "../core/Logger.h"
#include "../ecs/components/LightComponent.h"
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
#include <unordered_map>

class Application;
class IRenderAdapter;
struct ImVec2;

namespace editor
{
class EditorLayer
{
public:
    struct Statistics {double uiMs=0,hierarchyMs=0,inspectorMs=0,assetBrowserMs=0;};
    const Statistics& GetStatistics()const{return m_Statistics;}
    EditorVisualSettings& GetVisualSettings(){return m_VisualSettings;}
    bool ProjectileTestEnabled()const{return m_ProjectileTestInput;}
    void SetProjectileTestInput(bool enabled){m_ProjectileTestInput=enabled;}
    void SetBackgroundOverride(bool enabled){m_BackgroundOverride=enabled;}
    void SetGridVisible(bool show){m_ShowGrid=show;}
    void SetSkyDiagnosticEnabled(bool enabled){m_SkyDiagnosticEnabled=enabled;}
    void SetDiagnosticViewport(int w,int h){m_DiagnosticWidth=w;m_DiagnosticHeight=h;}
    void SetAssetGrid(bool grid){m_AssetGrid=grid;}
    void ShowSceneBackground(){m_ShowSceneSettings=true;m_FocusSceneSettings=true;}
    void ShowVisualSettings(){m_ShowVisualSettings=true;m_FocusVisualSettings=true;}
    EditorVisualFrame BuildVisualFrame(const Application&)const;
    void Render(Application& app, IRenderAdapter* renderer, float dt);
    void InitializePreferences(Application&);
    void PersistPreferences(Application&);
    void ResetLayout(){m_ResetLayout=true;}
    void SetFixedDiagnosticLayout(bool fixed){m_FixedDiagnosticLayout=fixed;if(fixed){m_ShowStatistics=m_ShowMaterialEditor=true;}}
    InputOwner InputOwnership(bool right,bool left,bool camera,bool play)const{return RouteInput({m_ViewportFocused,m_ViewportHovered,m_InputTextActive,m_ModalActive,m_ToolActive,false,camera,right,left,play});}
    bool BlocksCamera()const{return m_ToolActive||m_InputTextActive||m_ModalActive;}
    bool OwnsViewportKeyboard()const{return m_ViewportFocused&&!BlocksCamera();}
    void SetLightGizmosVisible(bool v){m_ShowLightGizmos=v;}
    void EnterRuntime(const ecs::World::CloneMap&);
    void LeaveRuntime();
    void ResetHistory(){m_UndoStack.clear();m_RedoStack.clear();m_SelectedEntity={};m_Transaction.reset();m_PropertyDrafts.clear();m_Revision=m_SavedRevision=m_NextRevision=0;m_ExternalDirty=false;}
    void FinishTransaction(Application&);
    void CancelTransaction(Application&);
    bool HasPendingTransaction()const{return m_Transaction.has_value();}
    bool IsSceneDirty()const{return m_ExternalDirty||m_Revision!=m_SavedRevision||m_Transaction.has_value();}
    void MarkSaved(){m_SavedRevision=m_Revision;m_ExternalDirty=false;}
    void MarkDirty(){m_ExternalDirty=true;}
    ecs::Entity CreateEntity(Application&,const std::string& name="Entity");
    bool DeleteEntity(Application&,ecs::Entity);
    ecs::Entity DuplicateEntity(Application&,ecs::Entity);
    bool RenameEntity(Application&,ecs::Entity,const std::string&);
    bool AddComponent(Application&,ecs::Entity,ecs::ComponentTypeId);
    bool RemoveComponent(Application&,ecs::Entity,ecs::ComponentTypeId);
    bool ResetComponent(Application&,ecs::Entity,ecs::ComponentTypeId);
    bool AssignAsset(Application&,ecs::Entity,ecs::AssetKind,const std::string&);
    bool CreateMaterialAsset(Application&,const std::string&,std::string* error=nullptr);
    bool RenameMaterialAsset(Application&,const std::string&,const std::string&,bool confirmed,std::string* error=nullptr);
    bool DeleteMaterialAsset(Application&,const std::string&,bool confirmed,std::string* error=nullptr);
    bool SetProperty(Application&,ecs::Entity,ecs::ComponentTypeId,const std::string&,const ecs::PropertyValue&,bool transactional=true);
    void BeginSceneTransaction(Application&,const std::string& label="Scene settings");
    std::size_t UndoCount()const{return m_UndoStack.size();}
    const std::string& GetError()const{return m_EditorError;}
    ecs::Entity CreateLight(Application&,LightType);
    void DeleteSelectedLight(Application&);
    void SelectEntity(ecs::Entity e){m_SelectedEntity=e;}
    ecs::Entity GetSelectedEntity()const{return m_SelectedEntity;}
    void RecordEdit(Application& app,const std::string& label){PushUndoBeforeChange(app,label);}
    void UndoEdit(Application& app){Undo(app);}
    void RedoEdit(Application& app){Redo(app);}
    int GetViewportPixelWidth() const { return m_ViewportPixelWidth; }
    int GetViewportPixelHeight() const { return m_ViewportPixelHeight; }
    bool IsViewportHovered() const { return m_ViewportHovered; }
    bool IsPointInsideViewport(double x, double y) const;

private:
    struct EntitySnapshot {
        ecs::Entity entity{};
        std::uint64_t world=0;
        bool alive=false;
        nlohmann::json components=nlohmann::json::object();
        std::optional<SceneSettings> settings;
        std::vector<EntitySnapshot> related;
    };
    struct UndoRecord {std::string label;EntitySnapshot snapshot;std::uint64_t before=0,after=0;};
    struct PendingTransaction {std::string label;EntitySnapshot before;};
    std::optional<PendingTransaction> m_Transaction;
    std::uint64_t m_Revision=0,m_SavedRevision=0,m_NextRevision=0;
    bool m_ExternalDirty=false;
    EntitySnapshot CaptureLike(Application&,const EntitySnapshot&)const;
    void BeginTransaction(const std::string&,EntitySnapshot);
    void RemapHistory(ecs::Entity,ecs::Entity);
    void CommitUndo(Application&,const std::string&,EntitySnapshot);

    void RefreshAssetLists(float dt);
    void HandleEditorShortcuts(Application& app);
    void DrawMainMenu(Application& app);
    void DrawToolbar(Application&);
    void DrawEditorVisualSettings(Application&,IRenderAdapter*);
    void PrepareEditorSky(Application&,IRenderAdapter*);
    EditorVisualSettings m_VisualSettings;
    bool m_ShowVisualSettings=false,m_FocusVisualSettings=false;
    bool m_BackgroundOverride=false,m_ProjectileTestInput=false,m_SkyDiagnosticEnabled=true,m_FocusSceneSettings=false;
    int m_DiagnosticWidth=0,m_DiagnosticHeight=0;
    std::string m_BackgroundAssetDraft;
    bool m_BackgroundAssetEditing=false;
    std::uint64_t m_BackgroundDraftWorld=0;
    std::string m_RequestedSky;
    std::uint64_t m_FailedSkyVersion=0;
    void ConfigureDocking();
    void DrawConsole(Application&,IRenderAdapter*);
    void DrawSceneSettings(Application&,IRenderAdapter*,float dt);
    void DrawSceneDialogs(Application&);
    void DrawViewportOverlays(Application&,const ImVec2&,const ImVec2&);
    void DrawSceneHierarchy(Application& app);
    void DrawInspector(Application& app);
    void DrawStatistics(Application& app, float dt);
    void DrawViewport(Application& app, IRenderAdapter* renderer);
    void DrawAssetBrowser(Application& app);
    void DrawMaterialEditor(Application& app);
    void DrawLightGizmos(Application&,IRenderAdapter*,const ImVec2&,const ImVec2&);
    bool m_ShowLightGizmos=true,m_LightXray=false;
    void DrawGizmo(Application& app, const ImVec2& viewportMin, const ImVec2& viewportSize);
    EntitySnapshot CaptureSelectedEntity(Application& app,ecs::Entity entity={}) const;
    ecs::Entity RestoreSnapshot(Application& app, const EntitySnapshot& snapshot);

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
    std::string m_EditingMaterial,m_MaterialError,m_NewMaterialKey="materials/new.material.json";
    bool m_MaterialDirty=false;
    bool m_ShowDemoWindow = false;
    bool m_ShowSceneSettings=true,m_ShowConsole=true,m_ShowGrid=true,m_Snapping=false;
    bool m_ResetLayout=true,m_FixedDiagnosticLayout=false,m_ViewportFocused=false,m_ToolActive=false,m_InputTextActive=false,m_ModalActive=false;
    float m_SnapTranslation=1,m_SnapRotation=15,m_SnapScale=.1f;
    Statistics m_Statistics;
    AssetCatalog m_AssetCatalog;
    bool m_AssetGrid=false,m_AssetActionRequested=false;
    int m_AssetAction=0;
    std::string m_AssetActionSource,m_AssetActionKey="materials/new.material.json";
    std::string m_AssetDirectory;
    bool AssetReferenced(Application&,const std::string&)const;
    std::string m_HierarchyFilter,m_AssetFilter,m_ConsoleFilter,m_RenameBuffer,m_SceneDialogKey="scenes/new_scene.json";
    int m_AssetKindFilter=0,m_SceneDialogAction=0;
    ecs::Entity m_RenameTarget{};
    bool m_SceneDialogRequested=false,m_RenameRequested=false,m_ConsoleInfo=true,m_ConsoleWarning=true,m_ConsoleError=true;
    std::uint64_t m_ConsoleRevision=UINT64_MAX;
    std::vector<Logger::Entry> m_ConsoleMessages;
    std::vector<std::string> m_EnvironmentAssets;
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
    struct HistoryContext {ecs::Entity selected;std::vector<UndoRecord> undo,redo;std::uint64_t revision,saved,next;bool external;};
    std::optional<HistoryContext> m_EditHistory;
    std::vector<UndoRecord> m_UndoStack;
    std::vector<UndoRecord> m_RedoStack;
    bool m_GizmoUndoCaptured = false;
    std::string m_EditorError;
    std::unordered_map<unsigned,ecs::PropertyValue> m_PropertyDrafts;
    static constexpr std::size_t MaxUndoRecords = 128;
};
}
