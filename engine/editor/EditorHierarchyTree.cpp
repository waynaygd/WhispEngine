#include "EditorLayer.h"
#include "../core/Application.h"
#include "../ecs/Hierarchy.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <chrono>
namespace editor {
void EditorLayer::DrawHierarchyTree(Application& app){
    const auto started=std::chrono::steady_clock::now();auto& world=app.GetWorld();
    constexpr const char* payloadType="WHISP_HIERARCHY_ENTITY";
    if(const auto* payload=ImGui::GetDragDropPayload();payload&&payload->IsDataType(payloadType)){
        if(!m_HierarchyDrag||m_HierarchyDrag->world!=world.GetIdentity()||app.IsEditorPlayMode()){
            ImGui::ClearDragDrop();m_HierarchyDrag.reset();m_HierarchyClick={};
        }
    }
    if(ImGui::IsKeyPressed(ImGuiKey_Escape)&&ImGui::IsDragDropActive()){
        ImGui::ClearDragDrop();m_HierarchyDrag.reset();m_HierarchyClick={};m_HierarchyDragCancelled=true;
    }
    ImGui::SetNextWindowPos(ImVec2(12,36),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320,520),ImGuiCond_FirstUseEver);
    const bool open=ImGui::Begin("Hierarchy Tree",nullptr,ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings);
    m_HierarchyFocused=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    m_HierarchyVisible.clear();m_HierarchyTreeRows.clear();PruneSelection(app);
    if(open){
        std::string error;const auto graph=ecs::HierarchyGraph::Build(world,&error);
        if(!graph)ImGui::TextWrapped("Hierarchy unavailable: %s",error.c_str());
        else{
            auto dropTarget=[&](ecs::Entity target){
                if(app.IsEditorPlayMode()||!ImGui::BeginDragDropTarget())return;
                bool preliminary=m_HierarchyDrag&&m_HierarchyDrag->world==world.GetIdentity();
                if(preliminary){
                    for(auto e:m_HierarchyDrag->entities)preliminary&=world.IsAlive(e);
                    if(target.IsValid())for(auto p=std::optional(graph->entities.at(target));p;p=graph->nodes[*p].parent)
                        if(std::find(m_HierarchyDrag->entities.begin(),m_HierarchyDrag->entities.end(),graph->nodes[*p].entity)!=m_HierarchyDrag->entities.end())preliminary=false;
                }
                if(auto* payload=ImGui::AcceptDragDropPayload(payloadType,ImGuiDragDropFlags_AcceptBeforeDelivery|(preliminary?0:ImGuiDragDropFlags_AcceptNoDrawDefaultRect))){
                    if(payload->IsPreview())ImGui::SetTooltip("%s",preliminary?"Release to move (Keep World); final constraints checked on drop":"Invalid target: stale source or selected subtree");
                    if(payload->IsDelivery()&&m_HierarchyDrag&&payload->DataSize==sizeof(ecs::Entity)){
                        const auto source=*static_cast<const ecs::Entity*>(payload->Data);
                        if(source==m_HierarchyDrag->source&&source.world==world.GetIdentity())
                            ReparentHierarchy(app,m_HierarchyDrag->entities,target);
                        m_HierarchyDrag.reset();m_HierarchyClick={};
                    }
                }ImGui::EndDragDropTarget();
            };
            if(m_HierarchyTree.world!=world.GetIdentity()){m_HierarchyTree={};m_HierarchyTree.world=world.GetIdentity();}
            if(m_HierarchyDrag&&!ImGui::GetIO().WantTextInput){
                if(auto it=graph->entities.find(m_HierarchyDrag->source);it!=graph->entities.end()){
                    const auto& node=graph->nodes[it->second];
                    if(!node.children.empty()&&m_HierarchyTree.filter.empty()){
                        if(ImGui::IsKeyPressed(ImGuiKey_LeftArrow,false))m_HierarchyTree.collapsed.insert(node.id);
                        if(ImGui::IsKeyPressed(ImGuiKey_RightArrow,false))m_HierarchyTree.collapsed.erase(node.id);
                    }
                }
            }
            ImGui::PushID(std::to_string(world.GetIdentity()).c_str());
            if(m_HierarchyFocused&&ImGui::GetIO().KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_F,false)){
                // SetKeyboardFocusHere deliberately ignores requests during DnD.
                // Explicit activation keeps the existing payload while editing
                // the real InputText widget; no custom text-input path.
                if(ImGui::IsDragDropActive())ImGui::ActivateItemByID(ImGui::GetID("##HierarchySearch"));
                else ImGui::SetKeyboardFocusHere();
            }
            ImGui::InputTextWithHint("##HierarchySearch","Search entities",&m_HierarchyTree.filter);
            ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.16f,.24f,.30f,1));
            ImGui::Button("Scene Root",ImVec2(-1,0));dropTarget({});ImGui::PopStyleColor();
            ImGui::BeginDisabled(app.IsEditorPlayMode());
            ImGui::BeginDisabled(!CanUndo());if(ImGui::SmallButton("Undo"))Undo(app);ImGui::EndDisabled();ImGui::SameLine();
            ImGui::BeginDisabled(!CanRedo());if(ImGui::SmallButton("Redo"))Redo(app);ImGui::EndDisabled();ImGui::EndDisabled();
            if(m_HierarchyFocused&&!ImGui::GetIO().WantTextInput&&!app.IsEditorPlayMode()&&!ImGui::IsDragDropActive()&&ImGui::GetIO().KeyCtrl){
                if(ImGui::IsKeyPressed(ImGuiKey_Z,false))Undo(app);
                if(ImGui::IsKeyPressed(ImGuiKey_Y,false))Redo(app);
            }
            ImGui::PopID();
            m_HierarchyTreeRows=BuildHierarchyTreeRows(world,*graph,m_HierarchyTree);
            for(const auto& row:m_HierarchyTreeRows)m_HierarchyVisible.push_back(row.entity);
            ImGui::BeginChild("Rows",ImVec2(0,-3*ImGui::GetTextLineHeightWithSpacing()),ImGuiChildFlags_None);
            ImGui::PushID(std::to_string(world.GetIdentity()).c_str());
            const float step=ImGui::GetFrameHeightWithSpacing();
            const float maximumIndent=std::max(0.f,ImGui::GetContentRegionAvail().x*.45f);
            ImGuiListClipper clipper;clipper.Begin(int(m_HierarchyTreeRows.size()),step);
            while(clipper.Step())for(int i=clipper.DisplayStart;i<clipper.DisplayEnd;++i){
                const auto& row=m_HierarchyTreeRows[i];const auto& node=graph->nodes[row.node];
                const auto* tag=world.GetComponent<ecs::TagComponent>(row.entity);
                const auto name=tag&&!tag->name.empty()?tag->name:node.id;
                const float indent=std::min(float(row.depth)*ImGui::GetStyle().IndentSpacing,maximumIndent);
                const auto depthLabel=float(row.depth)*ImGui::GetStyle().IndentSpacing>maximumIndent?"[depth "+std::to_string(row.depth)+"] ":std::string{};
                if(indent>0)ImGui::Indent(indent);ImGui::PushID(int(ImHashData(node.id.data(),node.id.size())));
                auto flags=ImGuiTreeNodeFlags_NoTreePushOnOpen|ImGuiTreeNodeFlags_OpenOnArrow|ImGuiTreeNodeFlags_OpenOnDoubleClick|ImGuiTreeNodeFlags_SpanAvailWidth|ImGuiTreeNodeFlags_FramePadding;
                if(!row.children)flags|=ImGuiTreeNodeFlags_Leaf;
                if(IsSelected(row.entity))flags|=ImGuiTreeNodeFlags_Selected;
                ImGui::SetNextItemOpen(!m_HierarchyTree.filter.empty()||!m_HierarchyTree.collapsed.contains(node.id),ImGuiCond_Always);
                const bool expanded=ImGui::TreeNodeEx("##Entity",flags,"%s %s%s%s",row.depth==0?"[Root]":row.children?"[Parent]":"[Leaf]",depthLabel.c_str(),name.c_str(),m_Selection.active==row.entity?" [Active]":"");
                if(ImGui::IsItemToggledOpen()&&row.children){m_HierarchyClick={};if(m_HierarchyTree.filter.empty()){if(expanded)m_HierarchyTree.collapsed.erase(node.id);else m_HierarchyTree.collapsed.insert(node.id);}}
                else if(ImGui::IsItemClicked(0))m_HierarchyClick=row.entity;
                if(ImGui::IsItemHovered()&&ImGui::IsMouseReleased(0)&&m_HierarchyClick==row.entity&&!ImGui::IsDragDropActive())
                    SelectHierarchyEntity(app,row.entity,ImGui::GetIO().KeyCtrl,ImGui::GetIO().KeyShift,m_HierarchyVisible);
                if(!app.IsEditorPlayMode()&&!ImGui::GetIO().KeyAlt&&!m_HierarchyDragCancelled&&ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover|ImGuiDragDropFlags_SourceNoHoldToOpenOthers)){
                    if(!m_HierarchyDrag){
                        m_HierarchyDrag=HierarchyDrag{world.GetIdentity(),row.entity,IsSelected(row.entity)?m_Selection.items:std::vector<ecs::Entity>{row.entity}};
                    }
                    m_HierarchyClick={};ImGui::SetDragDropPayload(payloadType,&m_HierarchyDrag->source,sizeof(ecs::Entity));
                    ImGui::Text("Move %zu entities (Keep World)",m_HierarchyDrag->entities.size());
                    ImGui::TextDisabled("Left/Right: collapse/expand source; Ctrl+F: search; Esc: cancel");ImGui::EndDragDropSource();
                }
                dropTarget(row.entity);
                if(ImGui::IsItemHovered())ImGui::SetTooltip("%s\nDepth: %zu",name.c_str(),row.depth);
                ImGui::PopID();if(indent>0)ImGui::Unindent(indent);
            }
            if(ImGui::IsWindowHovered()&&ImGui::IsMouseClicked(0)&&!ImGui::IsAnyItemHovered()&&!ImGui::GetIO().KeyCtrl&&!ImGui::GetIO().KeyShift)m_Selection.Clear();
            ImGui::PopID();ImGui::EndChild();
            if(!m_EditorError.empty()){ImGui::TextColored(ImVec4(1,.5f,.35f,1),"Drop/Undo rejected (hover for details)");if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",m_EditorError.c_str());}
            ImGui::TextDisabled("%zu visible / %zu entities, %zu selected",m_HierarchyTreeRows.size(),graph->nodes.size(),m_Selection.items.size());
            if(auto it=graph->entities.find(m_Selection.active);it!=graph->entities.end()){
                const auto& node=graph->nodes[it->second];auto* tag=world.GetComponent<ecs::TagComponent>(node.entity);
                ImGui::TextDisabled("Active: %s%s",tag&&!tag->name.empty()?tag->name.c_str():node.id.c_str(),std::find(m_HierarchyVisible.begin(),m_HierarchyVisible.end(),node.entity)==m_HierarchyVisible.end()?" (hidden)":"");
            }
        }
    }
    if(!ImGui::IsMouseDown(0)){m_HierarchyClick={};m_HierarchyDragCancelled=false;if(!ImGui::IsDragDropActive())m_HierarchyDrag.reset();}
    ImGui::End();m_Statistics.hierarchyMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
}
}
