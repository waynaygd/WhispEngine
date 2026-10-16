add_library(WhispEngineJson INTERFACE)
target_include_directories(WhispEngineJson INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

add_library(DearImGui STATIC
  ${CMAKE_SOURCE_DIR}/external/imgui/imgui.cpp
  ${CMAKE_SOURCE_DIR}/external/imgui/imgui_draw.cpp
  ${CMAKE_SOURCE_DIR}/external/imgui/imgui_demo.cpp
  ${CMAKE_SOURCE_DIR}/external/imgui/imgui_tables.cpp
  ${CMAKE_SOURCE_DIR}/external/imgui/imgui_widgets.cpp
  ${CMAKE_SOURCE_DIR}/external/imgui/misc/cpp/imgui_stdlib.cpp
  ${CMAKE_SOURCE_DIR}/external/imgui/backends/imgui_impl_glfw.cpp
  ${CMAKE_SOURCE_DIR}/external/imgui/backends/imgui_impl_dx12.cpp
 "ecs/components/PlayerStartComponent.h" "ecs/components/PlayerComponent.h")
target_include_directories(DearImGui PUBLIC
  ${CMAKE_SOURCE_DIR}/external/imgui
  ${CMAKE_SOURCE_DIR}/external/imgui/backends
)
target_link_libraries(DearImGui PUBLIC glfw d3d12)

add_library(ImGuizmo STATIC
  ${CMAKE_SOURCE_DIR}/external/imguizmo/src/ImGuizmo.cpp "ecs/components/PlayerStartComponent.h" "ecs/components/PlayerComponent.h")
target_include_directories(ImGuizmo PUBLIC
  ${CMAKE_SOURCE_DIR}/external/imguizmo/src
)
target_link_libraries(ImGuizmo PUBLIC DearImGui)

add_library(Engine
  scripting/LuaRuntime.cpp
  scripting/Prefab.cpp
  core/Application.cpp
  core/AssetDependencyValidation.cpp
  core/AssetPaths.cpp
  core/Time.cpp
  core/Logger.cpp
  ecs/World.cpp
  ecs/ComponentRegistry.cpp
  ecs/systems/BoundsBounceSystem.cpp
  ecs/systems/MotionSystem.cpp
  ecs/systems/PhysicsSystem.cpp
  ecs/systems/RenderSystem.cpp
  ecs/systems/SystemPipeline.cpp
  game/StateMachine.cpp
  jobs/JobSystem.cpp
  jobs/JobSystem.h
  resources/ResourceManager.cpp
  resources/loaders/MeshLoader.cpp
  resources/loaders/MaterialLoader.cpp
  resources/loaders/TextureLoader.cpp
  resources/loaders/EnvironmentLoader.cpp
  resources/loaders/ShaderLoader.cpp
  scene/SceneSerializer.cpp
  scene/SceneSettings.cpp
  editor/EditorLayer.cpp
  editor/EditorTransactions.cpp
  editor/EditorSelection.cpp
  editor/EditorGroupTransform.cpp
  editor/EditorInspector.cpp
  editor/EditorPreferences.cpp
  editor/EditorUi.cpp
  editor/EditorViewportCreate.cpp
  editor/EditorFogGizmos.cpp
  editor/EditorVisualSettings.cpp
  editor/AssetCatalog.cpp
  editor/EditorAssetPreview.cpp
  editor/EditorAssets.cpp
  editor/ScriptEditor.cpp
  editor/GltfSceneImport.cpp
  editor/EditorLevelWorkflow.cpp
  editor/EditorScalePlanes.cpp
  editor/AssetImport.cpp
  editor/EditorImportUi.cpp
  editor/LightGizmos.cpp
  render/RenderFactory.cpp
  platform/GlfwWindow.cpp
  platform/InputManager.cpp
 "ecs/components/PlayerStartComponent.h" "ecs/components/PlayerComponent.h")

target_sources(Engine PRIVATE
    game/states/LoadingState.cpp
    game/states/MenuState.cpp
    game/states/GameplayState.cpp
)

target_include_directories(Engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(Engine PUBLIC glfw WhispEngineJson PRIVATE WhispLua sol2::sol2)
target_link_libraries(
    Engine
    PRIVATE
        assimp
        StbImage
        WhispEngineJson
        DearImGui
        ImGuizmo
        enkiTS
)

target_include_directories(Engine PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/external
)

target_sources(Engine PRIVATE
    core/ConfigLoader.cpp
)


if (WIN32 AND ENABLE_DX12)
  target_compile_definitions(Engine PRIVATE ENABLE_DX12=1)
  target_sources(Engine PRIVATE
    render/backends/dx12/Dx12RenderAdapter.cpp
    render/backends/dx12/Dx12Shadows.cpp
    render/backends/dx12/Dx12EditorVisuals.cpp
    render/backends/dx12/Dx12Fog.cpp
    render/backends/dx12/Dx12Environment.cpp
    render/backends/dx12/Dx12SceneLights.cpp
  )
  target_link_libraries(Engine PRIVATE d3d12 dxgi dxguid d3dcompiler)
  target_include_directories(Engine PRIVATE ${CMAKE_SOURCE_DIR}/external/dxc/include)
  target_link_libraries(Engine PRIVATE ${CMAKE_SOURCE_DIR}/external/dxc/lib/x64/dxcompiler.lib)
endif()

if (ENABLE_VULKAN)
  find_package(Vulkan REQUIRED)
  target_compile_definitions(Engine PRIVATE ENABLE_VULKAN=1)
  target_sources(Engine PRIVATE
    render/backends/vulkan/VkRenderAdapter.cpp
  )
  target_link_libraries(Engine PRIVATE Vulkan::Vulkan)
endif()

if(WIN32)
  target_link_libraries(Engine PRIVATE shell32 version)
  target_link_libraries(Engine PRIVATE comdlg32)
endif()
