#pragma once
#include "../scripting/GameplayEventHistory.h"
#include "../scripting/LuaInspection.h"

#include "../ecs/Entity.h"
#include "../ui/RuntimeUI.h"
#include "../ecs/ComponentRegistry.h"
#include "../scene/SceneSettings.h"
#include "../render/RenderResourceHandles.h"
#include "EditorPreferences.h"
#include "EditorDocuments.h"
#include "InputRouting.h"
#include "SelectionSet.h"
#include "GroupTransform.h"
#include "PlacementMath.h"
#include "../ecs/PlacementPreview.h"
#include "AssetCatalog.h"
#include "AssetNavigation.h"
#include "TransformPrecision.h"
#include "SceneCatalog.h"
#include "SceneValidation.h"
#include "../core/Logger.h"
#include "../ecs/components/LightComponent.h"
#include "../ecs/components/ColliderComponent.h"
#include "../ecs/components/MaterialComponent.h"
#include "../ecs/components/MeshRendererComponent.h"
#include "../ecs/components/RigidbodyComponent.h"
#include "../ecs/components/TagComponent.h"
#include "../ecs/components/TransformComponent.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>

class Application;
class IRenderAdapter;
struct ImVec2;
struct ImGuiContext;

namespace editor
{
class EditorLayer
{
public:
    bool OpenDocument(Application&,const std::string&);
    class CameraMouseFrame {
        ImGuiContext* context=nullptr;
        bool restoreMouse=false;
    public:
        explicit CameraMouseFrame(bool suppress);
        ~CameraMouseFrame();
        CameraMouseFrame(const CameraMouseFrame&)=delete;
        CameraMouseFrame& operator=(const CameraMouseFrame&)=delete;
    };
    CameraMouseFrame RouteCameraMouse(bool captured,bool buttonsDown,const ImVec2& pointer);
    bool IsViewportMaximized()const{return m_ViewportMaximized;}
    struct Statistics {double uiMs=0,hierarchyMs=0,inspectorMs=0,assetBrowserMs=0,lightGizmoMs=0;};
    const Statistics& GetStatistics()const{return m_Statistics;}
    EditorVisualSettings& GetVisualSettings(){return m_VisualSettings;}
    bool ProjectileTestEnabled()const{return m_ProjectileTestInput;}
    void SetProjectileTestInput(bool enabled){m_ProjectileTestInput=enabled;}
    void SetBackgroundOverride(bool enabled){m_BackgroundOverride=enabled;}
    void SetGridVisible(bool show){m_ShowGrid=show;}
    void SetGizmoOperation(int operation){m_GizmoOperation=std::clamp(operation,0,2);}
    void SetSnapping(bool enabled){m_Snapping=m_RotationSnapping=enabled;}
    void SetRotationSnapping(bool enabled){m_RotationSnapping=enabled;}
    bool IsRotationSnapping()const{return m_RotationSnapping;}
    void SetSkyDiagnosticEnabled(bool enabled){m_SkyDiagnosticEnabled=enabled;}
    void SetDiagnosticViewport(int w,int h){m_DiagnosticWidth=w;m_DiagnosticHeight=h;}
    void SetAssetGrid(bool grid){m_AssetGrid=grid;}
    void SetAssetCardSize(float size){if(std::isfinite(size))m_AssetCardSize=std::clamp(size,120.f,320.f);}
    void SetAssetSearch(const std::string& search){m_AssetFilter=search;}
    void SetAssetTypeFilter(ecs::AssetKind kind){m_AssetKindFilter=0;for(std::size_t i=0;i<m_AssetCatalog.Types().size();++i)if(m_AssetCatalog.Types()[i].kind==kind){m_AssetKindFilter=int(i)+1;break;}}
    void NavigateAssetFolder(const std::string& folder){auto key=AssetCatalog::DirectoryKey(std::filesystem::u8path(folder));m_AssetNavigation.Go(key);m_AssetDirectory=key;}
    const std::string& AssetFolder()const{return m_AssetDirectory;}
    const std::string& ImportTargetFolder()const{return m_ImportTargetFolder;}
    void BeginAssetImport(){m_ImportTargetFolder=m_AssetDirectory;m_ImportTarget.clear();m_ImportPackageName.clear();m_ImportPopup=true;m_ImportPreview.clear();}
    bool RotateSelectedQuarter(Application&,int axis,int turns,bool world);
    void SetPhysicsPanelVisible(bool v){m_ShowPhysicsStress=v;}
    bool IsPhysicsPanelVisible()const{return m_ShowPhysicsStress;}
    void SetTransformPrecision(int v){m_TransformPrecision=std::clamp(v,0,2);}
    void SetLightXrayMode(int v){m_LightXrayMode=std::clamp(v,0,2);}

    void ShowSceneBackground(){m_ShowSceneSettings=true;m_FocusSceneSettings=true;}
    void ShowVisualSettings(){m_ShowVisualSettings=true;m_FocusVisualSettings=true;}
    EditorVisualFrame BuildVisualFrame(const Application&)const;
    void Render(Application& app, IRenderAdapter* renderer, float dt);
    void InitializePreferences(Application&);
    bool PersistPreferences(Application&);
    bool ResolveProjectEdits(Application&,int decision,std::string* error);
    void ResetProjectState(IRenderAdapter*);
    void ProjectOpened(Application&);
    bool UIDraftsDirty()const{if(!m_DirtyUIStyles.empty())return true;for(auto& [key,s]:m_UIDocuments)if(s.dirty)return true;return false;}
    bool HasProjectEdits()const{return UIDraftsDirty()||m_PrefabHistory.has_value()||ProjectSettingsDirty()||m_MaterialDirty||(m_EditHistory?(m_EditHistory->external||m_EditHistory->revision!=m_EditHistory->saved):IsSceneDirty());}
    void ShowProjectSettings(Application&);
    void RevertProjectSettings(Application&);
    bool SaveProjectSettingsDraft(Application&);
    void ProjectSettingsSaved(Application&);
    bool ProjectSettingsDirty()const{return m_ProjectSettingsBase&&m_ProjectSettingsDraft!=m_ProjectSettingsBase->Settings();}
    ProjectSettings& ProjectSettingsDraft(){return m_ProjectSettingsDraft;}
    void ShowProjectBrowser(const std::string& error={}){m_ShowProjectBrowser=true;if(!error.empty())m_ProjectError=error;}
    void RequestProjectOpen(Application&,const std::string& descriptor,bool close=false);
    void RequestProjectExit(){m_ProjectExit=true;m_ProjectConfirm=true;}
    bool ProjectExitConfirmed()const{return m_ProjectExitConfirmed;}
    const auto& RecentProjects()const{return m_RecentProjects;}
    void ShowLuaRuntimeInspector(){m_ShowLuaRuntimeInspector=true;}
    void SetLuaInspectorAutoRefresh(bool value){m_LuaInspectorAuto=value;}
    void UpdateLuaRuntimeInspector(Application&,double now,bool refresh=false);
    const scripting::LuaInstanceSnapshot& GetLuaInspection()const{return m_LuaInspection;}
    std::uint64_t LuaInspectionRefreshes()const{return m_LuaInspectionRefreshes;}
    bool ShowEventsForEntity(Application&,ecs::Entity);
    bool InspectGameplayEventEntity(Application&,const scripting::EventEntity&);
    const scripting::GameplayEventFilter& GetGameplayEventFilter()const{return m_EventFilter;}
    void ShowSceneValidation(){m_ShowSceneValidation=true;}
    void ValidateCurrentScene(Application&);
    const SceneValidationReport& GetSceneValidation()const{return m_SceneValidation;}
    bool SelectValidationIssue(Application&,std::size_t,bool focus=false);
    bool SceneValidationContextMatches(Application&)const;
    void ShowGameplayEvents(){m_ShowGameplayEvents=true;}
    bool SelectGameplayEventEntity(Application&,const scripting::EventEntity&);
    EditorWorkspace GetWorkspace()const{return m_Workspace;}
    void SwitchWorkspace(EditorWorkspace value){if(int(value)>=0&&int(value)<int(WorkspaceKeys.size())){CaptureCurrentWorkspace();m_RequestedWorkspace=value;m_WorkspacePending=true;m_WorkspaceFirstApply=false;}}
    bool SaveCurrentWorkspace(Application&);
    bool ResetCurrentWorkspace(Application&);
    void ResetLayout(){m_ResetLayout=true;}
    void SetFixedDiagnosticLayout(bool fixed){m_FixedDiagnosticLayout=fixed;if(fixed){m_ShowStatistics=m_ShowMaterialEditor=true;}}
    InputOwner InputOwnership(bool right,bool left,bool camera,bool play)const{return RouteInput({m_ViewportFocused,m_ViewportHovered,m_InputTextActive,m_ModalActive,m_ToolActive&&!m_LightPointerCaptured,m_LightPointerCaptured||m_LightHandleHovered,camera,right,left,play});}
    bool BlocksCamera()const{return m_ToolActive||m_InputTextActive||m_ModalActive;}
    bool BlocksGameplayInput()const;
    bool OwnsViewportKeyboard()const{return m_ViewportFocused&&!BlocksCamera();}
    void SetLightGizmosVisible(bool v){m_ShowLightGizmos=v;}
    void EnterRuntime(const ecs::World::CloneMap&);
    void LeaveRuntime();
    void ResetHistory(){m_LuaInspection={};m_LuaInspectionCaptured=false;m_SceneValidation={};m_SelectedValidation=-1;m_SurfaceMove.reset();m_EditPivot=false;m_VertexSource.reset();m_VertexTarget.reset();m_VertexSourcePicking=false;m_BoxSelection.reset();m_SelectionLocked=false;m_BulkSession.reset();m_BulkUiSelection.clear();m_BulkText.clear();ExitIsolation();CancelPlacement();m_UndoStack.clear();m_RedoStack.clear();m_Selection.Clear();m_GroupTransform.reset();m_Transaction.reset();m_PropertyDrafts.clear();m_Revision=m_SavedRevision=m_NextRevision=0;m_ExternalDirty=false;m_DuplicateDrag=m_GizmoUndoCaptured=m_LightPointerCaptured=m_LightHandleHovered=m_ToolActive=false;m_CancelGizmoUntilRelease=true;m_PlaneScaleCaptured=false;}
    void FinishTransaction(Application&);
    void CancelTransaction(Application&);
    bool HasPendingTransaction()const{return m_Transaction.has_value();}
    bool IsSceneDirty()const{return m_ExternalDirty||m_Revision!=m_SavedRevision||m_Transaction.has_value();}
    void MarkSaved(){m_SavedRevision=m_Revision;m_ExternalDirty=false;}
    void MarkDirty(){m_ExternalDirty=true;}
    ecs::Entity CreateEntity(Application&,const std::string& name="Entity");
    ecs::Entity CreatePlayerStart(Application&);
    ecs::Entity CreateFog(Application&);
    ecs::Entity CreatePostProcessVolume(Application&,bool global);
    bool DeleteEntity(Application&,ecs::Entity);
    ecs::Entity DuplicateEntity(Application&,ecs::Entity);
    bool CopyEntity(Application&,ecs::Entity);
    ecs::Entity PasteEntity(Application&);
    ecs::Entity BeginDuplicateDrag(Application&);
    ecs::Entity CreatePrimitive(Application&,const std::string& type);
    ecs::Entity CreateModelEntity(Application&,const std::string& key,const std::string& name="Model");
    void ClearSelection(){m_Selection.Clear();m_PropertyDrafts.clear();}
    const std::vector<ecs::Entity>& GetSelectedEntities()const{return m_Selection.items;}
    bool IsSelected(ecs::Entity e)const{return m_Selection.Contains(e);}
    bool IsSelectionLocked()const{return m_SelectionLocked;}
    void SetSelectionLocked(bool value){m_SelectionLocked=value;if(value)m_BoxSelection.reset();}
    enum class SelectionOperation { Replace, Add, Subtract };
    void SelectViewportRectangle(Application&,std::array<float,4> rectangle,SelectionOperation);
    bool IsBoxSelecting()const{return m_BoxSelection&&m_BoxSelection->dragging;}
    void PruneSelection(Application&);
    void SelectViewportEntity(Application&,ecs::Entity,bool ctrl,bool shift);
    bool SaveBookmark(Application&,int slot);
    bool GoToBookmark(Application&,int slot);
    bool RenameBookmark(Application&,int slot,const std::string& name);
    bool DeleteBookmark(Application&,int slot);
    const SceneBookmarks* GetSceneBookmarks(const Application&)const;
    bool IsolateSelected(Application&);
    void ExitIsolation(){m_Isolation.clear();}
    bool IsIsolationActive()const{return !m_Isolation.empty();}
    const std::vector<ecs::Entity>& IsolationEntities()const{return m_Isolation;}
    bool EditorEntityVisible(const Application&,ecs::Entity)const;
    bool CanViewportSelect(const Application&,ecs::Entity)const;
    SelectionFilters& GetSelectionFilters(){return m_SelectionFilters;}
    ecs::Entity PickViewportMesh(Application&,ecs::Vec3 origin,ecs::Vec3 direction,float distance)const;
    void SelectHierarchyEntity(Application&,ecs::Entity,bool ctrl,bool shift,const std::vector<ecs::Entity>& order);
    void SelectAllEntities(Application&,const std::vector<ecs::Entity>& order);
    bool DeleteSelection(Application&);
    bool ReparentEntity(Application&,ecs::Entity child,ecs::Entity parent,bool keepWorld=true);
    ecs::Entity CreateChild(Application&,ecs::Entity parent);
    void FocusSelection(Application&);
    bool BeginGroupTransform(Application&,bool local);
    bool UpdateGroupTransform(Application&,const ImportMatrix&);
    void EndGroupTransform(Application&,bool cancel=false);
    enum class OpenDecision {Cancel,Discard,Save};
    bool ActivateDocument(Application&,const std::string&,OpenDecision decision=OpenDecision::Cancel);
    bool CloseDocument(Application&,const std::string&,OpenDecision decision=OpenDecision::Cancel);
    bool SaveActiveDocument(Application&);
    bool UndoActiveDocument(Application&,bool redo=false);
    bool CanUndoActiveDocument(bool redo=false)const;
    bool DocumentDirty(const EditorDocument&)const;
    const std::vector<EditorDocument>& Documents()const{return m_Documents;}
    const std::string& ActiveDocument()const{return m_ActiveDocument;}
    bool ActiveDocumentIs(DocumentType type)const{for(const auto& d:m_Documents)if(d.id==m_ActiveDocument)return d.type==type;return false;}
    bool IsWorkspaceFocused()const{return m_WorkspaceFocus;}
    void SetWorkspaceFocused(bool value){if(value==m_WorkspaceFocus)return;if(value)CaptureCurrentWorkspace();else {m_RequestedWorkspace=m_Workspace;m_WorkspacePending=true;m_WorkspaceFirstApply=false;}m_WorkspaceFocus=value;}
    void PrepareWorkspacePrimaryWindow();
    void ReleaseAssetPreview(IRenderAdapter*);
    struct AssetThumbnail {
        RenderTextureHandle texture;
        int width=0,height=0;
        std::uint64_t version=0,lastUsed=0;
        std::string state="Not requested",error;
        ecs::AssetKind kind=ecs::AssetKind::None;
    };
    void BeginAssetThumbnailFrame(Application&);
    const AssetThumbnail& RequestAssetThumbnail(Application&,ecs::AssetKind,const std::string&);
    void ReleaseAssetThumbnails(IRenderAdapter*);
    bool RequestRecentScene(Application&,const std::string&);
    bool IsGameView()const{return m_GameView;}
    void SetGameView(bool value){m_GameView=value;}
    std::vector<ecs::Entity> PlaceImportedScene(Application&,const std::string&,ecs::Vec3 point);
    std::vector<ecs::Entity> InstantiateImportedScene(Application&,const std::string&,ecs::Vec3 offset={});
    ecs::Entity PlaceModel(Application&,const std::string&,ecs::Vec3);
    ecs::Vec3 PlacementPoint(Application&,float u,float v)const;
    struct SurfaceHit {ecs::Entity entity;ecs::Vec3 position,normal;float distance=0;};
    struct PlacementPreview {
        std::string key,status;ecs::AssetKind kind=ecs::AssetKind::Mesh;
        ecs::TransformComponent transform;
        std::vector<ecs::PlacementRenderItem> items;
        bool active=false,ready=false,surface=false;
    };
    std::optional<SurfaceHit> RaycastPlacement(Application&,ecs::Vec3 origin,ecs::Vec3 direction,float distance,const std::vector<ecs::Entity>& ignore={})const;
    const PlacementPreview& UpdatePlacementPreview(Application&,const std::string&,float u,float v);
    const PlacementPreview& GetPlacementPreview()const{return m_PlacementPreview;}
    ecs::Entity ConfirmPlacement(Application&);
    void CancelPlacement(){m_PlacementPreview={};m_PrefabPlacement.reset();}
    bool DropSelectionToFloor(Application&);
    void SetEditPivot(bool enabled){m_EditPivot=enabled;}
    bool IsEditingPivot()const{return m_EditPivot;}
    bool SetEntityPivot(Application&,ecs::Entity,ecs::Vec3,bool finish=true);
    bool ResetEntityPivot(Application&,ecs::Entity);
    bool CenterEntityPivot(Application&,ecs::Entity);
    struct VertexReference {ecs::Entity entity;std::string mesh;std::uint64_t version=0;std::size_t index=0;};
    void SetVertexSnap(bool enabled){m_VertexSnapEnabled=enabled;if(!enabled){m_VertexSource.reset();m_VertexTarget.reset();}}
    bool IsVertexSnapEnabled()const{return m_VertexSnapEnabled;}
    bool SetVertexSnapSource(Application&,ecs::Entity,std::size_t vertex);
    std::optional<VertexReference> FindViewportVertex(Application&,float u,float v,bool source);
    std::optional<ecs::Vec3> VertexWorldPosition(Application&,const VertexReference&)const;
    bool SnapSelectionToVertex(Application&,const VertexReference&,bool finish=true);
    const std::string& GetVertexSnapStatus()const{return m_VertexSnapStatus;}
    void SetSurfaceOffset(float value){if(std::isfinite(value))m_SurfaceOffset=std::clamp(value,-1000.f,1000.f);}
    void SetSurfaceRotationOffset(float value){if(std::isfinite(value))m_SurfaceRotationOffset=std::clamp(value,-180.f,180.f);}
    float GetSurfaceOffset()const{return m_SurfaceOffset;}
    float GetSurfaceRotationOffset()const{return m_SurfaceRotationOffset;}
    void SetSurfaceSnap(bool value){m_SurfaceSnap=value;}
    void SetAlignToNormal(bool value){m_AlignToNormal=value;}
    bool OpenSceneGuarded(Application&,const std::string&,OpenDecision,std::string* error=nullptr);
    void RememberScene(const std::string&);
    const auto& RecentScenes()const{return m_RecentScenes;}
    bool OpenMaterial(Application&,const std::string&);

    bool RenameEntity(Application&,ecs::Entity,const std::string&);
    bool AddComponent(Application&,ecs::Entity,ecs::ComponentTypeId);
    bool RemoveComponent(Application&,ecs::Entity,ecs::ComponentTypeId);
    bool ResetComponent(Application&,ecs::Entity,ecs::ComponentTypeId);
    bool IsUIPreviewVisible()const{return m_ShowRuntimeUI;}
    void SetUIPreviewVisible(bool value){m_ShowRuntimeUI=value;}
    ecs::Entity CreateUICanvas(Application&,const std::string& document="");
    bool EditUIDocument(Application&,const std::string&,const ui::Document&,bool finish=true);
    bool SaveUIDocument(Application&,const std::string&);
    bool ReloadUIDocument(Application&,const std::string&);
    bool UndoUIDocument(Application&,const std::string&,bool redo=false);
    void DrawUICanvasInspector(Application&,ecs::Entity);
    bool OpenUIDesigner(Application&,const std::string&);
    const ui::Document& GetUIDraft(Application&,const std::string&);
    bool CancelUIDocumentEdit(Application&,const std::string&);
    bool WidgetCommand(Application&,const std::string& command,const std::string& argument="");
    void DrawUIDesigner(Application&);
    friend struct WidgetDesignerTestAccess;
    friend struct EditorDocumentTestAccess;
    void DrawUIDocumentProperties(Application&,const std::string&,bool hierarchy);
    void DrawUI4Properties(Application&,const std::string&,ui::Document&,ui::Widget&,bool&);
    bool EditUIStyle(Application&,const std::string&,const ui::StyleSheet&,bool finish=true);
    bool SaveUIStyle(Application&,const std::string&);
    bool UndoUIStyle(Application&,const std::string&,bool redo=false);
    bool UIDesignerOwnsKeyboard()const;
    bool AddCustomPostFx(Application&,ecs::Entity,const std::string&);
    void DrawCustomPostFxInspector(Application&,ecs::Entity);
    bool AssignAsset(Application&,ecs::Entity,ecs::AssetKind,const std::string&);
    bool CreateMaterialAsset(Application&,const std::string&,std::string* error=nullptr);
    std::string CreateLuaScriptAsset(Application&,const std::string& directory);
    bool CreatePrefab(Application&,const std::string& key);
    ecs::Entity InstantiatePrefab(Application&,const std::string& key,ecs::Vec3 position={},const ecs::TransformComponent* placement=nullptr,const nlohmann::json* prepared=nullptr);
    bool OpenPrefabEditor(Application&,const std::string& key);
    bool SavePrefabEditor(Application&);
    void RequestPrefabSave(){m_PrefabAction=1;m_PrefabConfirm=true;}
    bool ClosePrefabEditor(Application&,bool discard=false);
    bool ApplyPrefab(Application&,ecs::Entity);
    bool RevertPrefab(Application&,ecs::Entity);
    bool UpdatePrefabInstances(Application&,const std::string& key);
    const std::string& EditingPrefab()const{return m_EditingPrefab;}
    void DrawPrefabWorkflow(Application&);
    std::string CreatePrefabAsset(Application&,const std::string& directory);
    bool RenameMaterialAsset(Application&,const std::string&,const std::string&,bool confirmed,std::string* error=nullptr);
    bool DeleteMaterialAsset(Application&,const std::string&,bool confirmed,std::string* error=nullptr);
    bool RenameAssetFile(Application&,const std::string&,const std::string& name,bool confirmed,std::string* error=nullptr,bool acceptDynamicRisk=false);
    bool DeleteAssetFile(Application&,const std::string&,bool confirmed,std::string* error=nullptr,bool acceptDynamicRisk=false);
    bool MoveAssetFile(Application&,const std::string&,const std::string& destination,bool confirmed,std::string* error=nullptr,bool acceptDynamicRisk=false);
    std::vector<std::string> AssetFileBlockers(Application&,const std::string&)const;
    const std::string& SelectedAsset()const{return m_SelectedAsset;}
    void SelectAsset(const std::string& key,const std::string& kind){m_SelectedAsset=key;m_SelectedAssetKind=kind;}
    struct BulkValue {
        bool common=false;ecs::PropertyValue value;
        std::array<bool,4> mixed{};
        bool IsMixed()const{return std::any_of(mixed.begin(),mixed.end(),[](bool x){return x;});}
    };
    std::vector<ecs::ComponentTypeId> CommonInspectorComponents(Application&)const;
    BulkValue InspectBulkProperty(Application&,ecs::ComponentTypeId,const std::string&,bool world=false)const;
    bool SetBulkProperty(Application&,ecs::ComponentTypeId,const std::string&,const ecs::PropertyValue&,unsigned axes=15,bool world=false,bool relative=false,bool transactional=true);
    bool SetProperty(Application&,ecs::Entity,ecs::ComponentTypeId,const std::string&,const ecs::PropertyValue&,bool transactional=true);
    bool SetLuaEntityReference(Application&,ecs::Entity,const std::string& field,ecs::Entity target={});
    void BeginSceneTransaction(Application&,const std::string& label="Scene settings");
    std::size_t UndoCount()const{return m_UndoStack.size();}
    const std::string& GetError()const{return m_EditorError;}
    ecs::Entity CreateLight(Application&,LightType);
    void DeleteSelectedLight(Application&);
    void SelectEntity(ecs::Entity e){if(e!=m_Selection.active||m_Selection.items.size()>1)m_PropertyDrafts.clear();m_Selection.Replace(e);}
    void SelectEntityFromUser(Application& app,ecs::Entity e);
    ecs::Entity GetSelectedEntity()const{return m_Selection.active;}
    void RecordEdit(Application& app,const std::string& label){PushUndoBeforeChange(app,label);}
    void UndoEdit(Application& app){Undo(app);}
    void RedoEdit(Application& app){Redo(app);}
    int GetViewportPixelWidth() const { return m_ViewportPixelWidth; }
    int GetViewportPixelHeight() const { return m_ViewportPixelHeight; }
    bool IsViewportHovered() const { return m_ViewportHovered; }
    std::array<float,4> GetViewportBounds()const{return {m_ViewportMinX,m_ViewportMinY,m_ViewportMaxX,m_ViewportMaxY};}
    bool HasLightPointerCapture()const{return m_LightPointerCaptured;}
    bool HasPlaneScaleCapture()const{return m_PlaneScaleCaptured;}
    int GetCapturedScalePlane()const{return m_ScalePlane;}
    bool IsPointInsideViewport(double x, double y) const;

private:
    std::string BookmarkSceneKey(const Application&)const;
    bool DrawViewportTools(Application&);
    void HandleViewportToolShortcuts(Application&);
    std::map<std::string,SceneBookmarks> m_Bookmarks;
    SelectionFilters m_SelectionFilters;
    std::vector<ecs::Entity> m_Isolation;
    void DrawProjectSettings(Application&);
    bool m_ShowProjectSettings=false,m_FocusProjectSettings=false;
    int m_ProjectSettingsCategory=0;
    std::shared_ptr<const ProjectContext> m_ProjectSettingsBase;
    ProjectSettings m_ProjectSettingsDraft;
    SceneCatalog m_ProjectSettingsScenes;
    std::string m_ProjectSettingsError,m_ProjectSettingsNotice;
    struct PrefabFileSnapshot {std::string key;std::optional<nlohmann::json> contents,expected;};
    struct EntitySnapshot {
        ecs::Entity entity{},previousSelection{};
        std::uint64_t world=0;
        bool alive=false;
        std::optional<ecs::Entity> parent;
        nlohmann::json components=nlohmann::json::object();
        std::optional<SceneSettings> settings;
        std::optional<ecs::TransformComponent> transform;
        std::vector<EntitySnapshot> related;
        std::optional<SelectionSet> selection;
        std::vector<PrefabFileSnapshot> files;
        bool prefabBatch=false,bulk=false;
        std::string componentOnly;
    };
    struct UndoRecord {std::string label;EntitySnapshot snapshot;std::uint64_t before=0,after=0;};
    struct PendingTransaction {std::string label;EntitySnapshot before;};
    std::optional<PendingTransaction> m_Transaction;
    struct GroupSession {
        EntitySnapshot before;
        ecs::TransformComponent pivot;
        ImportMatrix current;
        std::vector<ecs::Entity> entities;
        std::vector<ecs::TransformComponent> originals;
    };
    std::optional<GroupSession> m_GroupTransform;
    EntitySnapshot CaptureSelection(Application&,bool transformsOnly=false)const;
    EntitySnapshot CaptureSubtree(Application&,ecs::Entity)const;
    ecs::Entity CreateCopies(Application&,nlohmann::json entities,std::size_t active,const std::string& label,bool dragging=false);
    nlohmann::json CaptureCopies(Application&,ecs::Entity)const;
    void DrawGroupGizmo(Application&,const float* view,const float* projection);
    std::vector<ecs::Entity> m_HierarchyVisible;
    bool m_InspectorWorldTransform=false,m_HierarchyKeepLocal=false;
    std::uint64_t m_Revision=0,m_SavedRevision=0,m_NextRevision=0;
    bool m_ExternalDirty=false;
    EntitySnapshot CaptureLike(Application&,const EntitySnapshot&)const;
    void BeginTransaction(const std::string&,EntitySnapshot);
    void RemapHistory(ecs::Entity,ecs::Entity);
    void CommitUndo(Application&,const std::string&,EntitySnapshot);

    void RefreshAssetLists(float dt);
    void HandleEditorShortcuts(Application& app);
    void DrawMainMenu(Application& app);
    void DrawProjectBrowser(Application&);
    bool ExecuteProjectAction(Application&,int decision);
    bool m_ShowProjectBrowser=false,m_ProjectConfirm=false,m_ProjectClose=false,m_ProjectExit=false,m_ProjectExitConfirmed=false;
    int m_ProjectTab=-1;
    std::string m_ProjectName="MyGame",m_ProjectLocation,m_ProjectDescriptor,m_PendingProject,m_ProjectError;
    std::vector<RecentProject> m_RecentProjects;
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
    EditorDocument& RegisterDocument(Application&,DocumentType,const std::string&);
    void DrawDocumentTabs(Application&);
    void DrawScriptDocument(Application&);
    void SyncSceneDocument(Application&);
    void CaptureSceneDocument(Application&);
    void StoreDocumentSession();
    void RestoreDocumentSession(Application&);
    bool ResolveDocumentEdits(Application&,const EditorDocument&,OpenDecision);
    std::vector<EditorDocument> m_Documents;
    std::string m_ActiveDocument,m_ObservedScene="<initial>",m_DocumentProject,m_PendingDocument,m_OpenDocumentKey;
    bool m_DocumentClose=false,m_DocumentPrompt=false,m_OpenDocumentPrompt=false,m_WorkspaceFocus=false,m_RestoreDocuments=true;
    nlohmann::json m_DocumentSessions=nlohmann::json::object();
    std::string m_ScriptText,m_ScriptKey;
    std::map<std::string,bool> CaptureWorkspaceVisibility();
    void ApplyWorkspaceVisibility(const std::map<std::string,bool>&);
    void WorkspaceDefaults();
    void BuildWorkspaceDocking(unsigned root,const ImVec2& size);
    bool CaptureCurrentWorkspace();
    void InitializeWorkspaces(const EditorPreferences&);
    void DrawWorkspaceMenu(Application&);
    void DrawConsole(Application&,IRenderAdapter*);
    void DrawGameplayEvents(Application&);
    void DrawSceneValidation(Application&);
    void DrawLuaRuntimeInspector(Application&);
    void DrawSceneSettings(Application&,IRenderAdapter*,float dt);
    void DrawSceneDialogs(Application&);
    void DrawViewportOverlays(Application&,const ImVec2&,const ImVec2&);
    void DrawSceneHierarchy(Application& app);
    void DrawInspector(Application& app);
    void DrawBulkInspector(Application&);
    void RestoreBulkSnapshot(Application&,const EntitySnapshot&);
    struct BulkSession {
        ecs::ComponentTypeId component=0;std::string property;bool world=false,relative=false;
        std::vector<ecs::Entity> selection,targets;
        std::vector<ecs::TransformComponent> basis;
    };
    std::optional<BulkSession> m_BulkSession;
    bool m_BulkRelative=false;
    std::vector<ecs::Entity> m_BulkUiSelection;
    std::unordered_map<unsigned,std::string> m_BulkText;
    void DrawStatistics(Application& app, float dt);
    void DrawViewport(Application& app, IRenderAdapter* renderer);
    void DrawAssetPreview(Application&);
    RenderTextureHandle m_AssetPreviewTexture;
    std::unordered_map<std::string,AssetThumbnail> m_AssetThumbnails;
    RenderShaderHandle m_ThumbnailShader;
    RenderTextureHandle m_ThumbnailWhite;
    std::uint64_t m_ThumbnailFrame=0,m_ThumbnailRevision=0,m_ThumbnailEpoch=0;
    unsigned m_ThumbnailRequests=0,m_ThumbnailUploads=0;
    std::string m_AssetPreviewKey,m_AssetMetadataKey,m_AssetMetadata;
    std::uint64_t m_AssetPreviewVersion=0,m_AssetMetadataVersion=0;
    void DrawAssetBrowser(Application& app);
    bool AssetBrowserOwnsKeyboard()const;
    void BeginAssetFileAction(Application&,int,const std::string&);
    void InvalidateAssetFileUi(Application&,const std::string&,const std::string& replacement);
    void DrawImportDialogs(Application& app);
    void DrawMaterialEditor(Application& app);
    void DrawLightGizmos(Application&,IRenderAdapter*,const ImVec2&,const ImVec2&);
    bool DrawViewportCreateMenu(Application&);
    bool DrawPlacementMenu(Application&);
    void DrawPlacementTarget(Application&,const ImVec2&,const ImVec2&);
    PlacementPreview m_PlacementPreview;
    struct PrefabPlacementPart {
        std::string mesh;ecs::TransformComponent relative;
        std::weak_ptr<void> owner;std::uint64_t version=0;
        PlacementBounds bounds;bool usable=false;
    };
    struct PrefabPlacementCache {
        std::string key,error;std::filesystem::path path;
        std::filesystem::file_time_type time{};std::uintmax_t size=0;
        nlohmann::json source;
        ecs::TransformComponent root;
        std::vector<ecs::TransformComponent> nodes;
        std::vector<PrefabPlacementPart> parts;
        PlacementBounds bounds;bool hasBounds=false;
    };
    struct SurfaceMoveSession {
        std::uint64_t world=0;std::vector<ecs::Entity> roots,ignored;
        std::vector<ecs::TransformComponent> originals;
        std::vector<PrefabPlacementPart> parts;
        ecs::TransformComponent base;PlacementBounds bounds;
    };
    std::optional<SurfaceMoveSession> m_SurfaceMove;
    void DrawSurfaceMove(Application&,const ImVec2&,const ImVec2&,bool afterGizmo);
    std::optional<PrefabPlacementCache> m_PrefabPlacement;
    const PlacementPreview& UpdatePrefabPlacementPreview(Application&,const std::string&,float,float);
    bool PositionPlacement(Application&,float,float,const PlacementBounds&,const ecs::TransformComponent&,const PlacementPoints& geometry={},const std::vector<ecs::Entity>& ignore={},PlacementPreview* output=nullptr);
    bool m_EditPivot=false;
    void DrawPivotGizmo(Application&,const float*,const float*,const ImVec2&,const ImVec2&);
    void DrawVertexSnap(Application&,const ImVec2&,const ImVec2&,bool afterGizmo);
    bool VertexMovesWithSelection(const Application&,ecs::Entity)const;
    bool m_VertexSnapEnabled=false,m_VertexSourcePicking=false;
    std::optional<VertexReference> m_VertexSource,m_VertexTarget;
    std::string m_VertexSnapStatus="Hold V + click a vertex of the selection, then drag Move Gizmo.";
    float m_SurfaceOffset=0,m_SurfaceRotationOffset=0;
    bool m_SurfaceSnap=true,m_AlignToNormal=false,m_PlacementCancelled=false;
    void DrawPostProcessGizmos(Application&,const ImVec2&,const ImVec2&);
    void DrawFogGizmos(Application&,const ImVec2&,const ImVec2&);
    void DrawEntityIcons(Application&,const ImVec2&,const ImVec2&);
    bool m_SpecialIconHovered=false;
    void DrawPlayerStartGizmos(Application&,const ImVec2&,const ImVec2&);
    bool m_ShowLightGizmos=true,m_LightXray=false;
    int m_LightXrayMode=1,m_TransformPrecision=1,m_RotateAxis=1;
    bool m_RotationSnapping=false,m_ShowPhysicsStress=false;
    AssetNavigation m_AssetNavigation;
    std::string m_ImportTargetFolder,m_ImportPackageName;

    bool DrawScalePlanes(Application&,const ImVec2&,const ImVec2&,const float* model);
    int m_ScalePlane=0;
    ecs::TransformComponent m_ScaleOriginal;
    std::array<float,2> m_ScaleMouse{},m_ScaleDirection{};
    void DrawGizmo(Application& app, const ImVec2& viewportMin, const ImVec2& viewportSize);
    EntitySnapshot CaptureSelectedEntity(Application& app,ecs::Entity entity={}) const;
    ecs::Entity RestoreSnapshot(Application& app, const EntitySnapshot& snapshot,EntitySnapshot* inverse=nullptr,ecs::World::CloneMap* remapped=nullptr);

    void PushUndoBeforeChange(Application& app, const std::string& label);
    void Undo(Application& app);
    void Redo(Application& app);
    bool CanUndo() const { return !m_UndoStack.empty(); }
    bool CanRedo() const { return !m_RedoStack.empty(); }

    struct BoxSelection {
        std::array<float,2> start;
        std::array<float,4> viewport;
        std::uint64_t world;
        SelectionOperation operation;
        bool dragging=false;
    };
    std::optional<BoxSelection> m_BoxSelection;
    bool m_SelectionLocked=false;
    SelectionSet m_Selection;
    int m_GizmoOperation = 0;
    int m_GizmoMode = 0;
    bool m_GameView=false,m_ProjectAssets=true,m_PlaneScaleCaptured=false;
    float m_AssetCardSize=180;
    struct UIDocumentEdit {std::string resourceKey;ui::Document draft;nlohmann::json disk;std::vector<nlohmann::json> undo,redo;std::optional<nlohmann::json> pending;std::string selected;ui::Pair preview{1920,1080};float zoom=.35f,panX=0,panY=0;bool viewSaved=false,fit=true;bool initialized=false,dirty=false;};
    std::map<std::string,UIDocumentEdit> m_UIDocuments;
    struct UIStyleEdit {ui::StyleSheet draft;nlohmann::json disk;std::vector<nlohmann::json> undo,redo;std::optional<nlohmann::json> pending;};
    std::map<std::string,UIStyleEdit> m_UIStyles;std::set<std::string> m_DirtyUIStyles;
    UIStyleEdit& UIStyleSession(Application&,const std::string&);
    std::string m_UI4Selection,m_UIStyleKey,m_UIStyleId,m_UIClipId,m_UIClipNameDraft,m_UIClipNameSource;

    UIDocumentEdit& UIDocumentSession(Application&,const std::string&);
    bool m_ShowRuntimeUI=true;
    bool m_ShowWidgetHierarchy=false,m_ShowCanvasDesigner=false,m_ShowWidgetInspector=false;
    std::string m_DesignDocument,m_WidgetSearch,m_WidgetRename,m_WidgetRenameId;std::vector<std::string> m_UIDesignerKeys;
    ui::Pair m_UIPreviewSize{1920,1080};float m_UIZoom=.35f,m_UIGridSize=16;
    struct UIDesignerPoint {float x=0,y=0;};
    UIDesignerPoint m_UIPan{},m_UICanvasOrigin{};bool m_UIFit=true,m_UIGrid=true,m_UISnap=false;
    struct UIWidgetDrag {std::string key,id;ui::Widget initial;ui::Rect start,parent;UIDesignerPoint mouse;int handle=-1;float screenScale=1;};
    std::optional<UIWidgetDrag> m_UIWidgetDrag;
    bool m_ShowHierarchy = true;
    bool m_ShowInspector = true;
    bool m_ShowStatistics = true;
    bool m_ShowViewport = true;
    bool m_ShowAssetBrowser = true;
    bool m_ShowMaterialEditor = true;
    std::string m_EditingMaterial,m_MaterialError,m_NewMaterialKey="materials/new.material.json";
    bool m_MaterialDirty=false;
    bool m_ShowDemoWindow = false;
    bool m_ShowLuaRuntimeInspector=false,m_LuaInspectorAuto=true,m_LuaInspectionCaptured=false;
    scripting::LuaInstanceSnapshot m_LuaInspection;
    std::string m_LuaVariableSearch;
    double m_LuaInspectionTime=0;
    std::uint64_t m_LuaInspectionRefreshes=0;
    bool m_ShowSceneValidation=false;
    SceneValidationReport m_SceneValidation;
    std::string m_ValidationSearch;
    int m_ValidationSeverity=0,m_SelectedValidation=-1;
    bool m_ShowGameplayEvents=false;
    scripting::GameplayEventFilter m_EventFilter;
    std::uint64_t m_SelectedGameplayEvent=0;
    bool m_EventFollow=false;
    bool m_ShowSceneSettings=true,m_ShowConsole=true,m_ShowGrid=true,m_Snapping=false;
    EditorWorkspace m_Workspace=EditorWorkspace::LevelEditing,m_RequestedWorkspace=EditorWorkspace::LevelEditing;
    std::map<std::string,WorkspaceLayout> m_Workspaces;
    bool m_WorkspacePending=true,m_WorkspaceFirstApply=true;
    bool m_ResetLayout=true,m_FixedDiagnosticLayout=false,m_ViewportFocused=false,m_ToolActive=false,m_InputTextActive=false,m_ModalActive=false;
    bool m_CameraMouseDrain=false,m_CameraMouseWasSuppressed=false;
    bool m_ViewportMaximized=false,m_FocusViewportWindow=false;
    const char* ViewportWindowName()const{return m_ViewportMaximized?"Viewport##Maximized":"Viewport";}
    float m_SnapTranslation=1,m_SnapRotation=15,m_SnapScale=.1f;
    Statistics m_Statistics;
    AssetCatalog m_AssetCatalog;
    bool m_AssetGrid=true,m_AssetActionRequested=false;
    int m_AssetAction=0,m_ModelImportMode=0;
    std::string m_AssetActionSource,m_AssetActionKey="materials/new.material.json";
    bool m_AssetActionFocus=false;
    std::filesystem::path m_AssetActionPath;
    std::filesystem::file_time_type m_AssetActionTime{};
    std::uintmax_t m_AssetActionSize=0;
    std::vector<std::string> m_AssetActionBlockers;
    std::vector<std::string> m_AssetActionWarnings;
    bool m_AssetDynamicRiskAccepted=false;
    std::string m_AssetDirectory;
    bool AssetReferenced(Application&,const std::string&)const;
    std::string m_HierarchyFilter,m_AssetFilter,m_ConsoleFilter,m_RenameBuffer,m_SceneDialogKey="scenes/new_scene.json",m_UnsavedSaveKey="scenes/untitled.json";
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
    struct HistoryContext {SelectionSet selection;std::vector<UndoRecord> undo,redo;std::uint64_t revision,saved,next;bool external;};
    std::optional<HistoryContext> m_EditHistory;
    std::optional<HistoryContext> m_PrefabHistory;
    std::string m_EditingPrefab,m_PrefabActionKey,m_PrefabDoubleClick,m_PrefabNewKey="prefabs/cabinet.prefab.json";
    nlohmann::json m_PrefabDiskBase;
    SceneSettings m_PrefabSavedSceneSettings;
    ecs::Entity m_PrefabRoot{};
    std::vector<ecs::Entity> m_PrefabIsolation;
    bool m_PrefabGameView=false;
    bool m_PrefabConfirm=false,m_PrefabCreatePopup=false;
    int m_PrefabAction=0;
    ecs::Entity m_PrefabActionEntity{};
    EntitySnapshot PrefabNodesSnapshot(Application&,const nlohmann::json&);
    bool WritePrefabAndUpdate(Application&,const std::string&,const nlohmann::json&,const nlohmann::json&,bool update);
    void RestorePrefabHistory();
    bool StartPrefabEditor(Application&,const std::string&,const nlohmann::json&,const nlohmann::json&);
    std::vector<UndoRecord> m_UndoStack;
    std::vector<UndoRecord> m_RedoStack;
    bool m_GizmoUndoCaptured = false;
    bool m_CancelGizmoUntilRelease=false,m_EscapeConsumed=false;
    bool m_DuplicateDrag=false,m_HierarchyFocused=false,m_LightPointerCaptured=false,m_LightHandleHovered=false,m_LightIconClicked=false;
    ecs::Entity m_DragOriginal{};
    nlohmann::json m_Clipboard;
    bool m_FocusMaterial=false;
    std::vector<std::string> m_RecentScenes;
    SceneCatalog m_SceneCatalog;
    std::string m_SceneSearch,m_NewFolderName="NewFolder",m_FolderParent,m_ImportSource,m_ImportRoot,m_ImportTarget="models/imported/model",m_ImportPreview;
    bool m_FolderPopup=false,m_ImportPopup=false;
    std::string m_EditorError;
    std::unordered_map<unsigned,ecs::PropertyValue> m_PropertyDrafts;
    static constexpr std::size_t MaxUndoRecords = 128;
};
}
