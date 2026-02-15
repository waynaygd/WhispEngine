add_library(Engine
  core/Application.cpp
  core/Time.cpp
  core/Logger.cpp
  game/StateMachine.cpp
  render/RenderFactory.cpp
  platform/GlfwWindow.cpp
)

target_sources(Engine PRIVATE
    game/states/LoadingState.cpp
    game/states/MenuState.cpp
    game/states/GameplayState.cpp
)

target_include_directories(Engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(Engine PUBLIC glfw)

target_include_directories(Engine PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/external
)

if (WIN32 AND ENABLE_DX12)
  target_compile_definitions(Engine PRIVATE ENABLE_DX12=1)
  target_sources(Engine PRIVATE
    render/backends/dx12/Dx12RenderAdapter.cpp
  )
  target_link_libraries(Engine PRIVATE d3d12 dxgi dxguid)
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
