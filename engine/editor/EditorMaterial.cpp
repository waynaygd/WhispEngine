#include "EditorTheme.h"
#include "EditorLayer.h"
#include "InspectorControls.h"
#include "PrecisionInputs.h"
#include "EditorWidgets.h"
#include "../core/Application.h"
#include "../resources/ResourceManager.h"
#include "../resources/loaders/MaterialLoader.h"
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <chrono>
#include <cmath>
namespace editor {
namespace {
bool Equal(const MaterialResource& a,const MaterialResource& b){return MaterialLoader::Serialize(a)==MaterialLoader::Serialize(b);}
MeshData PreviewMesh(int kind){
    MeshData m;
    if(kind==0){const int rings=48,segments=64;for(int y=0;y<=rings;++y)for(int x=0;x<=segments;++x){float phi=3.14159265f*y/rings,theta=6.2831853f*x/segments;MeshVertex v;v.position[0]=v.normal[0]=std::sin(phi)*std::cos(theta);v.position[1]=v.normal[1]=std::cos(phi);v.position[2]=v.normal[2]=std::sin(phi)*std::sin(theta);v.uv[0]=float(x)/segments;v.uv[1]=float(y)/rings;v.tangent[0]=-std::sin(theta);v.tangent[2]=std::cos(theta);v.tangent[3]=1;m.vertices.push_back(v);}for(int y=0;y<rings;++y)for(int x=0;x<segments;++x){unsigned a=y*(segments+1)+x,b=a+segments+1;for(auto i:{a,a+1,b,a+1,b+1,b})m.indices.push_back(i);}}
    else {
        auto face=[&](std::array<float,3> n,std::array<float,3> u,std::array<float,3> v){unsigned base=unsigned(m.vertices.size());for(int y=0;y<2;++y)for(int x=0;x<2;++x){MeshVertex p;for(int i=0;i<3;++i){p.position[i]=(kind==2?0:n[i])+(2*x-1)*u[i]+(2*y-1)*v[i];p.normal[i]=n[i];p.tangent[i]=u[i];}p.tangent[3]=1;p.uv[0]=float(x);p.uv[1]=float(y);m.vertices.push_back(p);}for(unsigned i:{0,2,1,1,2,3})m.indices.push_back(base+i);};
        if(kind==2)face({0,1,0},{1,0,0},{0,0,1});else {face({0,0,-1},{1,0,0},{0,1,0});face({0,0,1},{-1,0,0},{0,1,0});face({1,0,0},{0,0,1},{0,1,0});face({-1,0,0},{0,0,-1},{0,1,0});face({0,1,0},{1,0,0},{0,0,1});face({0,-1,0},{1,0,0},{0,0,-1});}
    }return m;
}
}
EditorLayer::MaterialSession* EditorLayer::EnsureMaterialSession(Application& app){
    if(m_EditingMaterial.empty())return nullptr;auto* rm=app.GetResourceManager();auto resource=rm?rm->PeekIndexed<MaterialResource>(m_EditingMaterial):nullptr;
    if(!resource||!resource->IsLoaded()){m_MaterialError=resource?resource->GetErrorMessage():"Material is not loaded. Open a Material Document.";return nullptr;}
    auto& s=m_MaterialSessions[m_EditingMaterial];
    if(!s.initialized||(!m_MaterialDirty&&s.version!=resource->GetVersion())){
        // The watcher may observe our own Save. Identical data must not erase Undo.
        if(!s.initialized||!Equal(s.base,resource->GetData())){s.base=s.draft=resource->GetData();s.undo.clear();s.redo.clear();s.transaction.reset();}
        s.version=resource->GetVersion();s.initialized=true;
    }
    return &s;
}
void EditorLayer::FinishMaterialEdit(Application& app,bool cancel){
    auto found=m_MaterialSessions.find(m_EditingMaterial);if(found==m_MaterialSessions.end())return;auto& s=found->second;
    if(s.transaction){if(cancel)s.draft=*s.transaction;else if(!Equal(*s.transaction,s.draft)){s.undo.push_back({*s.transaction,s.draft});if(s.undo.size()>128)s.undo.erase(s.undo.begin());s.redo.clear();}s.transaction.reset();}
    m_MaterialDirty=!Equal(s.base,s.draft);if(auto* rm=app.GetResourceManager())rm->SetMaterialDirty(m_EditingMaterial,m_MaterialDirty);
}
void EditorLayer::UndoMaterialEdit(Application& app,bool redo){
    FinishMaterialEdit(app);auto* s=EnsureMaterialSession(app);if(!s)return;auto& from=redo?s->redo:s->undo;auto& to=redo?s->undo:s->redo;if(from.empty())return;auto change=from.back();from.pop_back();s->draft=redo?change.after:change.before;to.push_back(std::move(change));FinishMaterialEdit(app);
}
bool EditorLayer::SaveMaterialDraft(Application& app){
    FinishMaterialEdit(app);auto* s=EnsureMaterialSession(app);if(!s)return false;
    if(!MaterialLoader::Save(m_EditingMaterial,s->draft,&m_MaterialError)){m_EditorError=m_MaterialError;return false;}
    auto* rm=app.GetResourceManager();auto resource=rm->PeekIndexed<MaterialResource>(m_EditingMaterial);resource->ReplaceData(s->draft,ResourceLoadState::Loaded);s->base=s->draft;s->version=resource->GetVersion();m_MaterialDirty=false;rm->SetMaterialDirty(m_EditingMaterial,false);m_MaterialError.clear();return true;
}
bool EditorLayer::ReloadMaterialDraft(Application& app){
    auto* rm=app.GetResourceManager();auto loaded=MaterialLoader::Load(m_EditingMaterial,AssetPaths::ResolveAssetPath(m_EditingMaterial));
    if(!loaded.success){m_MaterialError=m_EditorError=loaded.errorMessage;return false;}
    auto& s=m_MaterialSessions[m_EditingMaterial];auto resource=rm->PeekIndexed<MaterialResource>(m_EditingMaterial);if(!resource){m_MaterialError="Material resource unavailable";return false;}
    resource->ReplaceData(loaded.data,ResourceLoadState::Loaded);s.base=s.draft=loaded.data;s.version=resource->GetVersion();s.initialized=true;s.undo.clear();s.redo.clear();s.transaction.reset();m_MaterialDirty=false;rm->SetMaterialDirty(m_EditingMaterial,false);m_MaterialError.clear();return true;
}
void EditorLayer::ReleaseMaterialEditor(IRenderAdapter* renderer){
    if(renderer){renderer->ReleaseMaterialPreview();for(auto mesh:m_MaterialPreviewMeshes)if(mesh.IsValid())renderer->DestroyMesh(mesh);if(m_MaterialWhite.IsValid())renderer->DestroyTexture(m_MaterialWhite);for(auto& [key,release]:m_MaterialGpuCleanup)release(renderer);}
    m_MaterialPreviewMeshes={};m_MaterialWhite={};m_MaterialGpuCleanup.clear();m_MaterialPreviewImage=0;
}
void EditorLayer::DrawMaterialEditor(Application& app){
    const bool document=ActiveDocumentIs(DocumentType::Material);if(!document){
        if(ImGui::Begin("Material Editor",&m_ShowMaterialEditor)){ImGui::TextWrapped("Open a Material Document to edit its independent draft and preview.");if(m_SelectedAssetKind=="Material"&&ImGui::Button("Open selected asset"))OpenDocument(app,m_SelectedAsset);}ImGui::End();return;
    }
    auto* session=EnsureMaterialSession(app);auto* rm=app.GetResourceManager();auto* renderer=app.GetRenderAdapter();
    if(!session){if(ImGui::Begin("Material Editor",&m_ShowMaterialEditor))ErrorText(m_MaterialError.c_str());ImGui::End();return;}auto& s=*session;
    const auto start=std::chrono::steady_clock::now();const auto probes=ProjectContext::LinkProbeCount();
    ImFont* font=nullptr;for(auto* f:ImGui::GetIO().Fonts->Fonts)if(std::strcmp(f->GetDebugName(),"Browser UI")==0){font=f;break;}if(font)ImGui::PushFont(font,0);
    if(ImGui::IsKeyPressed(ImGuiKey_Escape,false))FinishMaterialEdit(app,true);
    bool previewVisible=false;ImVec2 previewSize{};
    if(m_FocusMaterial){ImGui::SetNextWindowFocus();m_FocusMaterial=false;}
    const float dpi=ImGui::GetStyle().FontScaleDpi;ImGui::SetNextWindowSizeConstraints({320*dpi,200*dpi},{FLT_MAX,FLT_MAX});
    PrepareWorkspacePrimaryWindow();
    if(ImGui::Begin("Material Preview",nullptr,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse|(m_WorkspaceFocus?ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings:0))){
        ImGui::BeginDisabled(app.IsEditorPlayMode());
        if(ImGui::Button("Save Material"))SaveMaterialDraft(app);Inline("Revert / Reload");if(ImGui::Button("Revert / Reload"))ImGui::OpenPopup("Reload Material?");
        if(ImGui::BeginPopupModal("Reload Material?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::TextWrapped("Discard this draft and reload %s?",m_EditingMaterial.c_str());if(ImGui::Button("Discard")){if(ReloadMaterialDraft(app))ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();if(!m_MaterialError.empty())ErrorText(m_MaterialError.c_str());ImGui::EndPopup();}
        ImGui::EndDisabled();Inline("View");if(ImGui::Button("View"))ImGui::OpenPopup("Preview options");
        if(ImGui::BeginPopup("Preview options")){ImGui::SetNextItemWidth(150);ImGui::Combo("Mesh",&s.mesh,"Sphere\0Cube\0Plane\0");ImGui::SetNextItemWidth(150);ImGui::Combo("Lighting",&s.lighting,"Studio\0Neutral\0Unlit\0");ImGui::Checkbox("Dark background",&s.dark);if(ImGui::Button("Reset Camera")){s.yaw=.65f;s.pitch=.3f;s.distance=3.7f;s.panX=s.panY=0;}if(ImGui::Button("Focus Preview")){s.distance=s.mesh==1?4.8f:3.7f;s.panX=s.panY=0;}ImGui::TextWrapped("Drag LMB/RMB to orbit, wheel to zoom, MMB to pan.");ImGui::EndPopup();}
        const char* meshNames[]={"Sphere","Cube","Plane"};const char* lightNames[]={"Studio","Neutral","Unlit"};ImGui::TextWrapped("%s%s | %s / %s",m_EditingMaterial.c_str(),m_MaterialDirty?" *":"",meshNames[s.mesh],lightNames[s.lighting]);
        previewSize=ImGui::GetContentRegionAvail();previewSize.x=std::max(64.f,previewSize.x);previewSize.y=std::max(64.f,previewSize.y);const auto pos=ImGui::GetCursorScreenPos();ImGui::InvisibleButton("##PreviewOrbit",previewSize,ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonRight|ImGuiButtonFlags_MouseButtonMiddle);previewVisible=ImGui::IsItemVisible();
        if(!app.IsEditorPlayMode()&&ImGui::IsItemHovered()){s.distance=std::clamp(s.distance*std::exp(-ImGui::GetIO().MouseWheel*.12f),1.4f,15.f);}
        if(!app.IsEditorPlayMode()&&ImGui::IsItemActive()){const auto delta=ImGui::GetIO().MouseDelta;if(ImGui::IsMouseDown(2)){s.panX-=delta.x*.003f*s.distance;s.panY+=delta.y*.003f*s.distance;}else if(ImGui::IsMouseDragging(0)||ImGui::IsMouseDragging(1)){s.yaw+=delta.x*.008f;s.pitch=std::clamp(s.pitch+delta.y*.008f,-1.45f,1.45f);}}
        if(previewVisible){
            const auto updateStart=std::chrono::steady_clock::now();MaterialPreviewFrame frame;frame.material=s.draft;frame.width=unsigned(previewSize.x);frame.height=unsigned(previewSize.y);frame.yaw=s.yaw;frame.pitch=s.pitch;frame.distance=s.distance;frame.panX=s.panX;frame.panY=s.panY;frame.lighting=s.lighting;frame.dark=s.dark;
            if(!m_MaterialPreviewMeshes[s.mesh].IsValid())m_MaterialPreviewMeshes[s.mesh]=renderer->UploadMesh(PreviewMesh(s.mesh));frame.mesh=m_MaterialPreviewMeshes[s.mesh];
            if(!m_MaterialWhite.IsValid()){TextureData white;white.pixels={255,255,255,255};m_MaterialWhite=renderer->CreateTexture2D(white);}frame.textures[0]=m_MaterialWhite;
            auto shader=rm->PeekIndexed<ShaderResource>(s.draft.shaderPath);if(!shader&&!s.draft.shaderPath.empty()){rm->LoadAsync<ShaderResource>(s.draft.shaderPath);}else if(shader&&shader->IsLoaded()){
                const auto watch="watch:shader:"+s.draft.shaderPath;if(!m_MaterialGpuCleanup.contains(watch)){rm->WatchForHotReload<ShaderResource>(s.draft.shaderPath);m_MaterialGpuCleanup[watch]=[](auto*){};}
                auto& data=shader->GetData();if(data.gpuHandleVersion!=shader->GetVersion()){if(data.gpuHandle.IsValid())renderer->DestroyShader(data.gpuHandle);data.gpuHandle=renderer->CreateShaderProgram(data);data.gpuHandleVersion=shader->GetVersion();const auto handle=data.gpuHandle;m_MaterialGpuCleanup["shader:"+s.draft.shaderPath]=[shader,handle](auto* r){auto& d=shader->GetData();if(d.gpuHandle==handle){if(handle.IsValid())r->DestroyShader(handle);d.gpuHandle={};d.gpuHandleVersion=0;}};}frame.shader=data.gpuHandle;frame.colorPipeline=data.fragmentSource.find("PBR_COLOR_PIPELINE_V1")!=std::string::npos;
            }
            const std::string* maps[]={&s.draft.texturePath,&s.draft.normalTexturePath,&s.draft.metallicRoughnessTexturePath,&s.draft.occlusionTexturePath};
            for(int i=0;i<4;++i)if(!maps[i]->empty()&&s.draft.textureTexCoords[i]==0){auto texture=rm->PeekIndexed<TextureResource>(*maps[i]);if(!texture){rm->LoadAsync<TextureResource>(*maps[i]);continue;}if(!texture->IsLoaded()||texture->IsFailed())continue;const auto watch="watch:texture:"+*maps[i];if(!m_MaterialGpuCleanup.contains(watch)){rm->WatchForHotReload<TextureResource>(*maps[i]);m_MaterialGpuCleanup[watch]=[](auto*){};}auto& data=texture->GetData();if(data.gpuHandleVersion!=texture->GetVersion()){if(data.gpuHandle.IsValid())renderer->DestroyTexture(data.gpuHandle);data.gpuHandle=renderer->CreateTexture2D(data.textureData);data.gpuHandleVersion=texture->GetVersion();const auto handle=data.gpuHandle;m_MaterialGpuCleanup["texture:"+*maps[i]]=[texture,handle](auto* r){auto& d=texture->GetData();if(d.gpuHandle==handle){if(handle.IsValid())r->DestroyTexture(handle);d.gpuHandle={};d.gpuHandleVersion=0;}};}if(data.gpuHandle.IsValid())frame.textures[i]=data.gpuHandle;else m_MaterialError="Texture GPU upload failed; using neutral fallback: "+*maps[i];}
            m_MaterialUpdateMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-updateStart).count();try{m_MaterialPreviewImage=renderer->RenderMaterialPreview(frame);}catch(const std::exception& failure){m_MaterialPreviewImage=0;m_MaterialError=std::string("Preview rendering failed: ")+failure.what();}
            if(m_MaterialPreviewImage)ImGui::GetWindowDrawList()->AddImage((ImTextureID)m_MaterialPreviewImage,pos,{pos.x+previewSize.x,pos.y+previewSize.y});
            if(!frame.shader.IsValid())ImGui::GetWindowDrawList()->AddText({pos.x+12,pos.y+12},IM_COL32(255,180,120,255),"Shader loading / unavailable. See resource status and Console compiler diagnostics.");
            if(s.draft.surfaceMode!=MaterialSurfaceMode::Opaque)ImGui::GetWindowDrawList()->AddText({pos.x+12,pos.y+40},IM_COL32(255,180,120,255),"Unsupported surface mode: omitted by the existing renderer.");
        }
    }ImGui::End();
    if(!m_WorkspaceFocus&&m_ShowMaterialEditor){
    ImGui::SetNextWindowSizeConstraints({260*dpi,160*dpi},{FLT_MAX,FLT_MAX});
    if(ImGui::Begin("Material Editor",&m_ShowMaterialEditor)){
        ImGui::TextWrapped("Material Properties: %s",s.draft.name.c_str());ImGui::TextDisabled("Shader: %s",s.draft.shaderPath.c_str());ImGui::BeginDisabled(app.IsEditorPlayMode());
        auto row=[&](const char* label,auto draw){ImGui::PushID(label);inspector::PropertyRow(label);const auto before=s.draft;bool changed=draw();const bool active=ImGui::IsAnyItemActive();if(changed){if(!s.transaction)s.transaction=before;m_MaterialDirty=!Equal(s.base,s.draft);rm->SetMaterialDirty(m_EditingMaterial,m_MaterialDirty);if(!active)FinishMaterialEdit(app);}if(ImGui::IsItemDeactivatedAfterEdit())FinishMaterialEdit(app);ImGui::PopID();};
        auto section=[&](const char* name,auto draw,bool open=true){if(ImGui::TreeNodeEx(name,open?ImGuiTreeNodeFlags_DefaultOpen:0)){if(inspector::BeginProperties("Properties")){draw();ImGui::EndTable();}ImGui::TreePop();}};
        auto resource=[&](const char* label,std::string& key,ecs::AssetKind kind){row(label,[&]{ecs::PropertyDescriptor p;p.asset=kind;p.kind=ecs::PropertyKind::Asset;return DrawInspectorResource(p,key);});};
        section("Surface",[&]{row("Name",[&]{return ImGui::InputText("##value",&s.draft.name);});row("Base Color",[&]{return LinearColorEdit("##value",s.draft.baseColor,4);});resource("Base Texture",s.draft.texturePath,ecs::AssetKind::Texture);resource("Normal Texture",s.draft.normalTexturePath,ecs::AssetKind::Texture);resource("Metallic / Roughness Texture",s.draft.metallicRoughnessTexturePath,ecs::AssetKind::Texture);resource("AO Texture",s.draft.occlusionTexturePath,ecs::AssetKind::Texture);});
        section("Parameters",[&]{row("Shading Model",[&]{int v=int(s.draft.shadingModel);bool c=ImGui::Combo("##value",&v,"Lambert\0PBR (GGX)\0");s.draft.shadingModel=ShadingModel(v);return c;});row("Metallic",[&]{return PrecisionSliderFloat("##value",&s.draft.metallic,0,1);});row("Roughness",[&]{return PrecisionSliderFloat("##value",&s.draft.roughness,0,1);});row("Normal Scale",[&]{return PrecisionDragFloat("##value",&s.draft.normalScale,.01f,0,4);});row("AO Strength",[&]{return PrecisionSliderFloat("##value",&s.draft.occlusionStrength,0,1);});});
        section("UV",[&]{const char* labels[]={"Base UV Set","Normal UV Set","MR UV Set","AO UV Set"};for(int i=0;i<4;++i)row(labels[i],[&]{return PrecisionDragInt("##value",&s.draft.textureTexCoords[i],1,0,7);});},false);
        section("Shader",[&]{resource("Shader",s.draft.shaderPath,ecs::AssetKind::Shader);},false);
        section("Advanced",[&]{row("Double Sided",[&]{return ImGui::Checkbox("##value",&s.draft.doubleSided);});row("Cast Shadows",[&]{return ImGui::Checkbox("##value",&s.draft.castShadows);});row("Receive Shadows",[&]{return ImGui::Checkbox("##value",&s.draft.receiveShadows);});row("Surface Mode",[&]{int v=int(s.draft.surfaceMode);bool c=ImGui::Combo("##value",&v,"Opaque\0Masked (unsupported)\0Transparent (unsupported)\0");s.draft.surfaceMode=MaterialSurfaceMode(v);return c;});if(s.draft.surfaceMode==MaterialSurfaceMode::Masked)row("Alpha Cutoff",[&]{return PrecisionSliderFloat("##value",&s.draft.alphaCutoff,0,1);});},false);
        ImGui::TextWrapped("UV0 is supported; nonzero UV sets disable that map. Preview uses direct lighting and ambient; scene IBL, shadows and postprocessing are not previewed.");
        for(const auto& key:{s.draft.shaderPath,s.draft.texturePath,s.draft.normalTexturePath,s.draft.metallicRoughnessTexturePath,s.draft.occlusionTexturePath})if(!key.empty()){
            if(key==s.draft.shaderPath){auto r=rm->PeekIndexed<ShaderResource>(key);if(!r||r->IsLoading())ImGui::TextWrapped("Loading shader: %s",key.c_str());else if(r->HasError())ImGui::TextWrapped("%s: %s",key.c_str(),r->GetErrorMessage().c_str());}
            else {auto r=rm->PeekIndexed<TextureResource>(key);if(!r||r->IsLoading())ImGui::TextWrapped("Loading map: %s",key.c_str());else if(r->HasError())ImGui::TextWrapped("Missing map (neutral fallback): %s: %s",key.c_str(),r->GetErrorMessage().c_str());}
        }
        ImGui::EndDisabled();if(!m_MaterialError.empty())ErrorText(m_MaterialError.c_str());
        if(!ImGui::IsAnyItemActive())FinishMaterialEdit(app);

    }ImGui::End();}
    if(m_WorkspaceFocus)FinishMaterialEdit(app);if(font)ImGui::PopFont();m_MaterialPathProbes=ProjectContext::LinkProbeCount()-probes;(void)start;
}
}
