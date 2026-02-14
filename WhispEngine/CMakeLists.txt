cmake_minimum_required(VERSION 3.20)
project(WhispEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENABLE_DX12 "Enable DirectX 12 backend" ON)
option(ENABLE_VULKAN "Enable Vulkan backend" ON)

include(FetchContent)

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
)

FetchContent_MakeAvailable(glfw)

add_subdirectory(engine)

add_executable(WhispEngine WhispEngine.cpp)
target_link_libraries(WhispEngine PRIVATE Engine)

include(engine/shaders/Shaders.cmake)
setup_shaders(WhispEngine)

# копирование dll (если нужно)
if (WIN32 AND ENABLE_DX12)
  add_custom_command(TARGET WhispEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${CMAKE_SOURCE_DIR}/external/dxc/bin/x64/dxcompiler.dll
      $<TARGET_FILE_DIR:WhispEngine>/dxcompiler.dll
  )
  add_custom_command(TARGET WhispEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${CMAKE_SOURCE_DIR}/external/dxc/bin/x64/dxil.dll
      $<TARGET_FILE_DIR:WhispEngine>/dxil.dll
  )
endif()