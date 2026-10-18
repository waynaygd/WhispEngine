#include "EditorLayer.h"
#include "../core/Application.h"
#include <imgui.h>
#include <imgui_internal.h>
namespace editor {
void EditorLayer::DrawHierarchyTransformInspector(Application& app){
    auto& world=app.GetWorld();
    auto cancel=[&]{if(m_HierarchyTransformDraft&&ImGui::GetActiveID()==m_HierarchyTransformDraft->item)ImGui::ClearActiveID();m_HierarchyTransformDraft.reset();};
    if(m_HierarchyTransformInvalidateInput){
        if(ImGui::GetCurrentContext()->ActiveIdWindow==ImGui::FindWindowByName("Hierarchy Transform"))ImGui::ClearActiveID();
        m_HierarchyTransformInvalidateInput=false;
    }
    if(m_HierarchyTransformDraft){
        const auto& before=m_HierarchyTransformDraft->before;
        const auto* local=world.GetComponent<ecs::TransformComponent>(before.entity);
        const auto* relationship=world.GetComponent<ecs::RelationshipComponent>(before.entity);
        const bool relationSame=bool(relationship)==before.relationship.has_value()&&(!relationship||(relationship->parent==before.relationship->parent&&relationship->order==before.relationship->order));
        if(app.IsEditorPlayMode()||m_Selection.items.size()!=1||m_Selection.active!=before.entity||!local||!SameLocalTransform(*local,before.local)||!relationSame)cancel();
    }
    const bool escape=m_HierarchyTransformDraft&&ImGui::IsKeyPressed(ImGuiKey_Escape,false);
    if(escape)cancel();
    ImGui::SetNextWindowPos(ImVec2(345,36),ImGuiCond_FirstUseEver);ImGui::SetNextWindowSize(ImVec2(420,520),ImGuiCond_FirstUseEver);
    const bool open=ImGui::Begin("Hierarchy Transform",nullptr,ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings);
    if(!open){cancel();ImGui::End();return;}
    std::string error;auto view=world.IsAlive(m_Selection.active)?ReadHierarchyTransformView(world,m_Selection.active,&error):std::optional<HierarchyTransformView>{};
    if(!view){cancel();ImGui::TextWrapped("%s",error.empty()?"Select an Entity with Transform in Hierarchy Tree":error.c_str());ImGui::End();return;}
    ImGui::PushID(std::to_string(world.GetIdentity()).c_str());ImGui::PushID(view->id.c_str());
    ImGui::TextWrapped("Entity: %s",view->name.c_str());ImGui::TextWrapped("SceneIdentity: %s",view->id.c_str());ImGui::TextWrapped("Parent: %s",view->parentName.c_str());
    ImGui::TextUnformatted("Local Transform");
    const bool readOnly=app.IsEditorPlayMode()||m_Selection.items.size()!=1;
    auto finish=[&]{if(!m_HierarchyTransformDraft)return;auto draft=std::move(*m_HierarchyTransformDraft);m_HierarchyTransformDraft.reset();EditHierarchyLocal(app,draft.before.entity,draft.value);};
    for(int field=0;field<3;++field){
        const auto authored=world.GetComponent<ecs::TransformComponent>(view->entity);
        auto local=m_HierarchyTransformDraft?m_HierarchyTransformDraft->value:*authored;
        auto value=field==0?local.position:field==1?local.rotation:local.scale;
        if(field==1)value={Degrees(value.x),Degrees(value.y),Degrees(value.z)};
        float values[3]={value.x,value.y,value.z};
        const char* labels[]={"Position","Rotation (degrees)","Scale"};const char* ids[]={"##LocalPosition","##LocalRotation","##LocalScale"};
        ImGui::TextUnformatted(labels[field]);ImGui::SetNextItemWidth(-1);ImGui::BeginDisabled(readOnly);
        const bool changed=ImGui::DragFloat3(ids[field],values,field==1?.5f:field==2?.02f:.1f,0,0,TransformFormat(m_TransformPrecision));
        const bool activated=ImGui::IsItemActivated(),deactivated=ImGui::IsItemDeactivatedAfterEdit();ImGui::EndDisabled();
        if(!readOnly&&!escape&&(activated||changed)){
            if(m_HierarchyTransformDraft&&m_HierarchyTransformDraft->field!=field)finish();
            if(!m_HierarchyTransformDraft){
                auto before=ecs::HierarchyEdit::Capture(world,{view->entity}).front();
                m_HierarchyTransformDraft=HierarchyTransformDraft{before,before.local,ImGui::GetActiveID(),field};
            }
            if(changed){ecs::Vec3 next{values[0],values[1],values[2]};
                if(field==1)next={Radians(next.x),Radians(next.y),Radians(next.z)};
                if(field==0)m_HierarchyTransformDraft->value.position=next;
                else if(field==1)m_HierarchyTransformDraft->value.rotation=next;
                else m_HierarchyTransformDraft->value.scale=next;
            }
        }
        if(!readOnly&&!escape&&deactivated&&m_HierarchyTransformDraft&&m_HierarchyTransformDraft->field==field)finish();
        if(m_HierarchyTransformDraft&&m_HierarchyTransformDraft->field==field&&!ImGui::IsAnyItemActive()&&!ImGui::IsMouseDown(0))finish();
    }
    if(m_HierarchyTransformDraft)ImGui::TextDisabled("Pending Local edit: release / Enter applies; Esc cancels");
    else ImGui::TextDisabled("Local is relative to Parent. World values are read-only.");
    if(readOnly)ImGui::TextDisabled("%s",app.IsEditorPlayMode()?"Play/Pause: authoring is read-only":"Multiple selection: active Entity is read-only");
    view=ReadHierarchyTransformView(world,m_Selection.active,&error);
    ImGui::Separator();ImGui::TextUnformatted("World Transform (read-only)");
    if(view){
        ImGui::Text("Position: %.9g, %.9g, %.9g",view->matrix[12],view->matrix[13],view->matrix[14]);
        if(view->worldTrs){const auto& t=*view->worldTrs;
            ImGui::Text("Rotation (degrees): %.6g, %.6g, %.6g",Degrees(t.rotation.x),Degrees(t.rotation.y),Degrees(t.rotation.z));
            ImGui::Text("Scale: %.9g, %.9g, %.9g",t.scale.x,t.scale.y,t.scale.z);
        }else ImGui::TextWrapped("World rotation/scale unavailable: affine shear or singular/non-TRS matrix. No lossless World TRS.");
    }
    ImGui::BeginDisabled(app.IsEditorPlayMode()||m_HierarchyTransformDraft.has_value());
    ImGui::BeginDisabled(!CanUndo());if(ImGui::SmallButton("Undo"))Undo(app);ImGui::EndDisabled();ImGui::SameLine();
    ImGui::BeginDisabled(!CanRedo());if(ImGui::SmallButton("Redo"))Redo(app);ImGui::EndDisabled();ImGui::EndDisabled();
    if(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)&&!ImGui::GetIO().WantTextInput&&!app.IsEditorPlayMode()&&!m_HierarchyTransformDraft&&!ImGui::IsDragDropActive()&&ImGui::GetIO().KeyCtrl){
        if(ImGui::IsKeyPressed(ImGuiKey_Z,false))Undo(app);if(ImGui::IsKeyPressed(ImGuiKey_Y,false))Redo(app);
    }
    if(!m_EditorError.empty())ImGui::TextWrapped("Edit rejected: %s",m_EditorError.c_str());
    ImGui::PopID();ImGui::PopID();ImGui::End();
}
}
