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

set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_INJECT_DEBUG_POSTFIX OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  assimp
  GIT_REPOSITORY https://github.com/assimp/assimp.git
  GIT_TAG c35200e38ea8f058812b83de2ef32c6093b0ece2
)

FetchContent_Declare(
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG 31c1ad37456438565541f4919958214b6e762fb4
)

FetchContent_MakeAvailable(glfw assimp)

FetchContent_GetProperties(stb)
if (NOT stb_POPULATED)
  FetchContent_Populate(stb)
endif()

add_library(StbImage STATIC engine/third_party/stb_image_impl.cpp)
target_include_directories(StbImage PUBLIC ${stb_SOURCE_DIR})

add_subdirectory(engine)

add_executable(WhispEngine WhispEngine.cpp)
target_link_libraries(WhispEngine PRIVATE Engine)

include(engine/shaders/Shaders.cmake)
setup_shaders(WhispEngine)

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
  add_custom_command(TARGET WhispEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:WhispEngine>/shaders/dx12
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/engine/shaders/dx12/triangle.hlsl
            $<TARGET_FILE_DIR:WhispEngine>/shaders/dx12/triangle.hlsl
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/engine/shaders/dx12/textured.hlsl
            $<TARGET_FILE_DIR:WhispEngine>/shaders/dx12/textured.hlsl
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_BINARY_DIR}/shaders/dx12/triangle_vs.dxil
            $<TARGET_FILE_DIR:WhispEngine>/shaders/dx12/triangle_vs.dxil
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_BINARY_DIR}/shaders/dx12/triangle_ps.dxil
            $<TARGET_FILE_DIR:WhispEngine>/shaders/dx12/triangle_ps.dxil
  )
endif()

add_custom_command(TARGET WhispEngine POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          $<TARGET_FILE_DIR:WhispEngine>/engine/config
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_SOURCE_DIR}/engine/config/app.json
          $<TARGET_FILE_DIR:WhispEngine>/engine/config/app.json
  COMMAND ${CMAKE_COMMAND} -E make_directory
          $<TARGET_FILE_DIR:WhispEngine>/assets
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/engine/assets
          $<TARGET_FILE_DIR:WhispEngine>/assets
  COMMAND ${CMAKE_COMMAND} -E make_directory
          $<TARGET_FILE_DIR:WhispEngine>/shaders/vulkan
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_SOURCE_DIR}/engine/shaders/vulkan/triangle.vert
          $<TARGET_FILE_DIR:WhispEngine>/shaders/vulkan/triangle.vert
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_SOURCE_DIR}/engine/shaders/vulkan/triangle.frag
          $<TARGET_FILE_DIR:WhispEngine>/shaders/vulkan/triangle.frag
)
