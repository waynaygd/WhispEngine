#define NOMINMAX
#include "EditorTestPreferences.h"
#include "SyntheticEditorInput.h"
#include "core/Application.h"
#include "core/AssetPaths.h"
#include "resources/ResourceManager.h"
#include "scene/SceneSerializer.h"
#include "render/backends/dx12/Dx12RenderAdapter.h"
#include "platform/GlfwWindow.h"
#include <GLFW/glfw3.h>
#include <imgui_internal.h>
#include <iostream>
#include <fstream>

static void Check(bool value,const std::string& text){if(!value)throw std::runtime_error(text);}
static void Image(const std::string& name,const std::vector<std::uint8_t>& pixels,int width,int height){
    std::filesystem::create_directories("diagnostics/asset-browser");
    std::ofstream out("diagnostics/asset-browser/"+name+".ppm",std::ios::binary);
    out<<"P6\n"<<width<<" "<<height<<"\n255\n";
    for(std::size_t i=0;i<pixels.size();i+=4)out.write(reinterpret_cast<const char*>(pixels.data()+i),3);
}
static std::optional<ImRect> Card(std::uint64_t texture){
    for(auto* window:ImGui::GetCurrentContext()->Windows){
        if(!window->Active||std::string(window->Name).find("Asset Browser/Content")==std::string::npos)continue;
        const auto* draw=window->DrawList;
        for(const auto& command:draw->CmdBuffer)if(command.GetTexID()==ImTextureID(texture)&&command.ElemCount>=6){
            ImRect bounds({FLT_MAX,FLT_MAX},{-FLT_MAX,-FLT_MAX});
            for(unsigned i=0;i<6;++i)bounds.Add(draw->VtxBuffer[command.VtxOffset+draw->IdxBuffer[command.IdxOffset+i]].pos);
            bounds.ClipWith(ImRect({command.ClipRect.x,command.ClipRect.y},{command.ClipRect.z,command.ClipRect.w}));
            if(bounds.GetWidth()>0&&bounds.GetHeight()>0)return bounds;
        }
    }return {};
}
int main(){try{
    ScopedEditorPreferenceRestore preferences;std::string preferenceError;
    Check(editor::SavePreferences(editor::PreferencesPath(),editor::EditorPreferences{},&preferenceError),"isolated editor preferences: "+preferenceError);Application app;Check(app.Initialize(),"initialize editor");
    glfwHideWindow(dynamic_cast<GlfwWindow*>(app.GetWindow())->GetGlfwHandle());
    auto& ui=app.GetEditorLayer();auto* gpu=dynamic_cast<Dx12RenderAdapter*>(app.GetRenderAdapter());auto* resources=app.GetResourceManager();
    Check(gpu&&resources,"DX12 and ResourceManager");app.NewScene();app.SetEditorPlayMode(false);
    ui.SwitchWorkspace(editor::EditorWorkspace::LevelEditing);app.SetFrameLimit(3);Check(app.Run()==0,"activate Level workspace fixture");Check(ui.ResetCurrentWorkspace(app),"reset cached Level layout for pixel/placement checks");
    ui.ResetLayout();ui.SetAssetGrid(true);ui.SetAssetCardSize(120);ui.NavigateAssetFolder("models/primitives");
    app.SetFrameLimit(4);Check(app.Run()==0,"editor warmup");
    // Small real assets only: PNG, JPEG, compressed DDS, OBJ and glTF.
    std::vector<std::pair<ecs::AssetKind,std::string>> keys={
        {ecs::AssetKind::Texture,"textures/pbr_base.png"},{ecs::AssetKind::Texture,"textures/pbr_control.jpg"},
        {ecs::AssetKind::Texture,"textures/pbr_control_bc3.dds"},{ecs::AssetKind::Mesh,"models/primitives/cube.obj"},
        {ecs::AssetKind::Mesh,"models/primitives/sphere.obj"},{ecs::AssetKind::Mesh,"models/pbr_textures.gltf"}};
    std::vector<RenderTextureHandle> handles;
    for(auto& [kind,key]:keys){
        editor::EditorLayer::AssetThumbnail preview;
        for(unsigned frame=0;frame<240;++frame){
            resources->PumpFinalization(4);gpu->BeginFrame();gpu->Clear(0,0,0,1);ui.BeginAssetThumbnailFrame(app);
            preview=ui.RequestAssetThumbnail(app,kind,key);gpu->EndFrame();gpu->Present();
            Check(preview.state!="Failed",key+": "+preview.error);if(preview.state=="Ready")break;
        }
        Check(preview.state=="Ready"&&preview.texture.IsValid(),"thumbnail ready: "+key);
        auto pixels=gpu->ReadBackTexturePixels(preview.texture);Check(pixels.size()==std::size_t(preview.width)*preview.height*4,"readback dimensions");
        bool varied=false;for(std::size_t i=4;i<pixels.size();i+=4)if(pixels[i]!=pixels[0]||pixels[i+1]!=pixels[1]||pixels[i+2]!=pixels[2]){varied=true;break;}
        if(kind==ecs::AssetKind::Mesh){
            Check(varied,"actual geometry rather than blank thumbnail: "+key);
            unsigned occupied=0;for(std::size_t i=0;i<pixels.size();i+=4)if(pixels[i]>40||pixels[i+1]>40||pixels[i+2]>40)++occupied;
            Check(occupied>1000&&occupied<60000,"mesh framing and neutral background");
        }else{
            const auto& data=resources->Get<TextureResource>(key)->GetData().textureData;
            Check(std::abs(float(preview.width)/preview.height-float(data.width)/data.height)<.02f,"texture aspect ratio");
            const auto offset=(std::size_t(int(.5f*data.height/preview.height))*data.width+int(.5f*data.width/preview.width))*4;
            for(int c=0;c<4;++c)Check(pixels[c]==data.pixels[offset+c],"thumbnail pixels match decoded texture");
        }
        Image(std::filesystem::path(key).filename().string(),pixels,preview.width,preview.height);
        gpu->BeginFrame();gpu->Clear(0,0,0,1);ui.BeginAssetThumbnailFrame(app);
        Check(ui.RequestAssetThumbnail(app,kind,key).texture==preview.texture,"cached GPU handle reused");gpu->EndFrame();gpu->Present();handles.push_back(preview.texture);
    }
    Check(gpu->ReadBackTexturePixels(handles[3])!=gpu->ReadBackTexturePixels(handles[4]),"distinct model silhouettes");
    auto sphere=resources->Get<MeshResource>(keys[4].second);const auto originalSphere=sphere->GetData();auto shifted=originalSphere;
    for(auto& v:shifted.meshData.vertices){v.position[0]=v.position[0]*17+1000;v.position[1]=v.position[1]*17-800;v.position[2]=v.position[2]*17+300;}
    sphere->ReplaceData(std::move(shifted),ResourceLoadState::Loaded);
    gpu->BeginFrame();gpu->Clear(0,0,0,1);ui.BeginAssetThumbnailFrame(app);auto rebuilt=ui.RequestAssetThumbnail(app,ecs::AssetKind::Mesh,keys[4].second);gpu->EndFrame();gpu->Present();
    Check(rebuilt.state=="Ready"&&rebuilt.texture!=handles[4]&&!gpu->GetTexturePreviewId(handles[4]),"resource version invalidates and retires old thumbnail");
    auto shiftedPixels=gpu->ReadBackTexturePixels(rebuilt.texture);unsigned occupied=0;
    for(std::size_t i=0;i<shiftedPixels.size();i+=4)if(shiftedPixels[i]>40||shiftedPixels[i+1]>40||shiftedPixels[i+2]>40)++occupied;
    Check(occupied>5000&&occupied<40000,"camera fits scaled off-origin mesh bounds");
    sphere->ReplaceData(originalSphere,ResourceLoadState::Loaded);gpu->BeginFrame();gpu->Clear(0,0,0,1);ui.BeginAssetThumbnailFrame(app);handles[4]=ui.RequestAssetThumbnail(app,ecs::AssetKind::Mesh,keys[4].second).texture;gpu->EndFrame();gpu->Present();
    const std::string missing="textures/__asset_browser_missing__.png";resources->Load<TextureResource>(missing);
    gpu->BeginFrame();gpu->Clear(0,0,0,1);ui.BeginAssetThumbnailFrame(app);auto failed=ui.RequestAssetThumbnail(app,ecs::AssetKind::Texture,missing);
    Check(failed.state=="Failed"&&!failed.error.empty()&&!failed.texture.IsValid(),"failed state and diagnostic");gpu->EndFrame();gpu->Present();
    SyntheticEditorInput input;app.SetFrameLimit(1);ui.SelectAsset("","");ui.SetAssetSearch("cube");ui.SetAssetTypeFilter(ecs::AssetKind::Mesh);
    auto* browserWindow=ImGui::FindWindowByName("Asset Browser");Check(browserWindow,"browser fixture window");
    const auto oldDock=browserWindow->DockId;const auto oldPosition=browserWindow->Pos,oldSize=browserWindow->Size;
    // Card-size assertions need unoccluded image space, independent of the
    // restored user's dock width or clipping in a short bottom panel.
    ImGui::SetWindowDock(browserWindow,0,ImGuiCond_Always);ImGui::SetWindowPos(browserWindow,{0,400},ImGuiCond_Always);ImGui::SetWindowSize(browserWindow,{900,320},ImGuiCond_Always);
    ImGui::SetWindowFocus("Asset Browser");for(int i=0;i<8;++i)app.Run();
    auto cubeId=gpu->GetTexturePreviewId(handles[3]);auto cube=Card(cubeId);Check(cube.has_value(),"name search and mesh filter retain cube image");
    Check(gpu->SaveFrameImage("diagnostics/asset-browser/browser.ppm"),"browser screenshot");
    Check(!Card(gpu->GetTexturePreviewId(handles[4])),"name search hides sphere");
    const auto smallWidth=cube->GetWidth();ui.SetAssetCardSize(220);app.Run();app.Run();cube=Card(cubeId);Check(cube&&cube->GetWidth()>smallWidth+20,"card size changes image size");
    ui.SetAssetTypeFilter(ecs::AssetKind::Texture);app.Run();app.Run();Check(!Card(cubeId),"type filter removes mesh");
    ui.SetAssetTypeFilter(ecs::AssetKind::Mesh);ui.SetAssetCardSize(120);app.Run();app.Run();cube=Card(cubeId);Check(bool(cube),"restore mesh card");
    ImGui::SetWindowDock(browserWindow,oldDock,ImGuiCond_Always);ImGui::SetWindowPos(browserWindow,oldPosition,ImGuiCond_Always);ImGui::SetWindowSize(browserWindow,oldSize,ImGuiCond_Always);app.Run();app.Run();
    auto floor=ui.CreatePrimitive(app,"plane");app.GetWorld().GetComponent<ecs::TransformComponent>(floor)->scale={20,1,20};ui.ClearSelection();
    Check(app.SetEditorCameraPose({0,6,-8},0,-.55f),"placement camera above floor");for(int i=0;i<12;++i)app.Run();
    const auto surfaceDraws=app.GetRenderStatistics().visibleSubmeshes;
    const auto before=app.GetWorld().GetAliveCount();Check(before==1,"placement fixture contains only its owned floor");cube=Card(cubeId);Check(cube.has_value(),"drag uses current card bounds after layout restore");auto source=cube->GetCenter();
    SyntheticEditorInput::Position(source.x,source.y);app.Run();SyntheticEditorInput::Button(0,true);app.Run();SyntheticEditorInput::Position(source.x+10,source.y);app.Run();
    auto* payload=ImGui::GetDragDropPayload();Check(payload&&payload->IsDataType("WHISP_ASSET"),"card starts existing asset drag payload");
    auto viewport=ui.GetViewportBounds();const float x=(viewport[0]+viewport[2])*.5f,y=(viewport[1]+viewport[3])*.5f;
    SyntheticEditorInput::Position(x,y);app.Run();app.Run();
    Check(app.GetWorld().GetAliveCount()==before&&ui.GetPlacementPreview().ready,"drag preview is ready without an ECS entity");
    for(int i=0;i<8&&app.GetRenderStatistics().visibleSubmeshes<=surfaceDraws;++i)app.Run();
    Check(app.GetRenderStatistics().visibleSubmeshes>surfaceDraws&&ui.GetPlacementPreview().surface,"temporary model renders on the real floor Mesh");
    app.Run();auto ghostPixels=gpu->ReadBackViewportPixels();unsigned cyan=0;
    for(std::size_t i=0;i<ghostPixels.size();i+=4)cyan+=ghostPixels[i+1]>ghostPixels[i]*1.3f&&ghostPixels[i+2]>ghostPixels[i]*1.3f;
    Check(cyan>30,"preview renders visible cyan model pixels");
    Check(gpu->SaveFrameImage("diagnostics/asset-browser/placement-preview.ppm"),"placement preview screenshot");
    SyntheticEditorInput::Key(ImGuiKey_Escape,true);app.Run();SyntheticEditorInput::Key(ImGuiKey_Escape,false);SyntheticEditorInput::Button(0,false);app.Run();app.Run();
    Check(app.GetWorld().GetAliveCount()==before&&!ui.GetPlacementPreview().active,"Escape cancels preview and prevents creation");
    SyntheticEditorInput::Position(source.x,source.y);app.Run();SyntheticEditorInput::Button(0,true);app.Run();SyntheticEditorInput::Position(source.x+10,source.y);app.Run();SyntheticEditorInput::Position(x,y);app.Run();app.Run();SyntheticEditorInput::Button(0,false);app.Run();app.Run();
    Check(app.GetWorld().GetAliveCount()==before+1,"drop creates model in viewport");auto entity=ui.GetSelectedEntity();auto* mesh=app.GetWorld().GetComponent<ecs::MeshRendererComponent>(entity);
    Check(mesh&&mesh->meshPath==keys[3].second,"drop retains model asset key");
    Check(std::abs(app.GetWorld().GetComponent<ecs::TransformComponent>(entity)->position.y-.5f)<.002f,"real drag/drop rests cube bounds on floor");
    auto scene=SceneSerializer::CaptureWorld(app.GetWorld(),app.GetSceneSettings());ecs::World reopened;SceneSettings settings;std::string error;
    Check(SceneSerializer::RestoreWorld(scene,reopened,settings,&error),"placed model scene roundtrip: "+error);Check(SceneSerializer::CaptureWorld(reopened,settings)==scene,"scene content preserved");
    Check(gpu->GetValidationErrorCount()==0,"DX12 debug layer has no ERROR/CORRUPTION");ui.ReleaseAssetThumbnails(gpu);Check(!gpu->GetTexturePreviewId(handles[3]),"GPU thumbnails released");
    app.Shutdown();std::cout<<"Asset Browser: PNG/JPEG/DDS, OBJ/glTF pixels, fit, cache, errors, search/type/size, real drag/drop, scene roundtrip and GPU validation passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"ASSET BROWSER FAILED: "<<e.what()<<'\n';return 1;}}
