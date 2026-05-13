add_library(WhispEngineJson INTERFACE)
target_include_directories(WhispEngineJson INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

add_library(Engine
  core/Application.cpp
  core/AssetDependencyValidation.cpp
  core/AssetPaths.cpp
  core/Time.cpp
  core/Logger.cpp
  ecs/World.cpp
  ecs/systems/BoundsBounceSystem.cpp
  ecs/systems/MotionSystem.cpp
  ecs/systems/PhysicsSystem.cpp
  ecs/systems/RenderSystem.cpp
  ecs/systems/SystemPipeline.cpp
  game/StateMachine.cpp
  resources/ResourceManager.cpp
  resources/loaders/MeshLoader.cpp
  resources/loaders/MaterialLoader.cpp
  resources/loaders/TextureLoader.cpp
  resources/loaders/ShaderLoader.cpp
  scene/SceneSerializer.cpp
  render/RenderFactory.cpp
  platform/GlfwWindow.cpp
  platform/InputManager.cpp
)

target_sources(Engine PRIVATE
    game/states/LoadingState.cpp
    game/states/MenuState.cpp
    game/states/GameplayState.cpp
)

target_include_directories(Engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(Engine PUBLIC glfw)
target_link_libraries(Engine PRIVATE assimp StbImage WhispEngineJson)

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
