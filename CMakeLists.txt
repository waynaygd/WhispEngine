cmake_minimum_required(VERSION 3.20)
project(WhispEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENABLE_DX12 "Enable DirectX 12 backend" ON)
option(ENABLE_VULKAN "Enable Vulkan backend" ON)
option(ENABLE_TRACY "Enable Tracy profiler" ON)

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

if (ENABLE_TRACY)
  set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
  set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)

  FetchContent_Declare(
    tracy
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG v0.14.1
    GIT_SHALLOW TRUE
  )
endif()

FetchContent_MakeAvailable(glfw assimp)

if (ENABLE_TRACY)
  FetchContent_MakeAvailable(tracy)
endif()

FetchContent_GetProperties(stb)
if (NOT stb_POPULATED)
  FetchContent_Populate(stb)
endif()

add_library(StbImage STATIC engine/third_party/stb_image_impl.cpp)
target_include_directories(StbImage PUBLIC ${stb_SOURCE_DIR})

add_subdirectory(engine)

option(WHISP_BUILD_PHYSICS_TESTS "Build headless rigid-body regression scenarios" OFF)
if (WHISP_BUILD_PHYSICS_TESTS)
  enable_testing()
  add_executable(PhysicsRegression
    tests/PhysicsRegression.cpp
    engine/ecs/World.cpp
    engine/ecs/systems/SystemPipeline.cpp
    engine/ecs/systems/PhysicsSystem.cpp)
  target_sources(PhysicsRegression PRIVATE
    engine/jobs/JobSystem.cpp
    engine/core/Logger.cpp
  )
  target_include_directories(PhysicsRegression PRIVATE engine)
  target_link_libraries(PhysicsRegression PRIVATE enkiTS)
  if (ENABLE_TRACY)
    target_link_libraries(PhysicsRegression PRIVATE Tracy::TracyClient)
  endif()
  add_test(NAME PhysicsRegression COMMAND PhysicsRegression)
  add_executable(ResourceRegression tests/ResourceRegression.cpp)
  target_link_libraries(ResourceRegression PRIVATE Engine)
  if (ENABLE_TRACY)
    target_link_libraries(ResourceRegression PRIVATE Tracy::TracyClient)
  endif()
  add_test(NAME ResourceRegression COMMAND ResourceRegression)
  add_executable(RenderInterpolationRegression tests/RenderInterpolationRegression.cpp)
  target_link_libraries(RenderInterpolationRegression PRIVATE Engine)
  if (ENABLE_TRACY)
    target_link_libraries(RenderInterpolationRegression PRIVATE Tracy::TracyClient)
  endif()
  add_test(NAME RenderInterpolationRegression COMMAND RenderInterpolationRegression)
  add_executable(GpuInstancingRegression tests/GpuInstancingRegression.cpp)
  target_link_libraries(GpuInstancingRegression PRIVATE Engine)
  if (ENABLE_TRACY)
    target_link_libraries(GpuInstancingRegression PRIVATE Tracy::TracyClient)
  endif()
  if (WIN32 AND ENABLE_DX12)
    target_compile_definitions(GpuInstancingRegression PRIVATE ENABLE_DX12=1)
  endif()
  add_test(NAME GpuInstancingRegression COMMAND GpuInstancingRegression)
  add_executable(LightingRegression tests/LightingRegression.cpp)
  target_link_libraries(LightingRegression PRIVATE Engine)
  if (ENABLE_TRACY)
    target_link_libraries(LightingRegression PRIVATE Tracy::TracyClient)
  endif()
  if (WIN32 AND ENABLE_DX12)
    target_compile_definitions(LightingRegression PRIVATE ENABLE_DX12=1)
  endif()
  add_test(NAME LightingRegression COMMAND LightingRegression)
  add_executable(MaterialSystemRegression tests/MaterialSystemRegression.cpp)
  target_link_libraries(MaterialSystemRegression PRIVATE Engine)
  if(ENABLE_TRACY)
    target_link_libraries(MaterialSystemRegression PRIVATE Tracy::TracyClient)
  endif()
  add_test(NAME MaterialSystemRegression COMMAND MaterialSystemRegression)
  if(WIN32 AND ENABLE_DX12)
    target_compile_definitions(MaterialSystemRegression PRIVATE ENABLE_DX12=1)
    add_test(NAME MaterialSystemDx12Regression COMMAND MaterialSystemRegression --dx12)
    set_tests_properties(MaterialSystemDx12Regression PROPERTIES ENVIRONMENT "WHISP_DX12_DEBUG=1" RUN_SERIAL TRUE)
  endif()
  add_executable(ShadowMappingRegression tests/ShadowMappingRegression.cpp)
  target_link_libraries(ShadowMappingRegression PRIVATE Engine)
  if(ENABLE_TRACY)
    target_link_libraries(ShadowMappingRegression PRIVATE Tracy::TracyClient)
  endif()
  if(WIN32 AND ENABLE_DX12)
    target_compile_definitions(ShadowMappingRegression PRIVATE ENABLE_DX12=1)
  endif()
  add_test(NAME ShadowMappingRegression COMMAND ShadowMappingRegression)
  if(WIN32 AND ENABLE_DX12)
    add_test(NAME ShadowMappingDx12Regression COMMAND ShadowMappingRegression --dx12)
    set_tests_properties(ShadowMappingDx12Regression PROPERTIES ENVIRONMENT "WHISP_DX12_DEBUG=1" RUN_SERIAL TRUE)
  endif()


endif()

add_executable(WhispEngine WhispEngine.cpp)
target_link_libraries(WhispEngine PRIVATE Engine)

if (ENABLE_TRACY)
    target_link_libraries(Engine PRIVATE Tracy::TracyClient)
endif()

set(ENKITS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ENKITS_BUILD_C_INTERFACE OFF CACHE BOOL "" FORCE)
set(ENKITS_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    enkits
    GIT_REPOSITORY https://github.com/dougbinks/enkiTS.git
    GIT_TAG v1.12
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(enkits)

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

# Source assets are also needed by backend-independent recorder regressions.
# Keep incremental HLSL edits synchronized without requiring an executable relink.
add_custom_target(MaterialShaderSources ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders/dx12
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/engine/shaders/dx12/textured.hlsl
    ${CMAKE_BINARY_DIR}/shaders/dx12/textured.hlsl
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/engine/shaders/dx12/textured_unlit.hlsl
    ${CMAKE_BINARY_DIR}/shaders/dx12/textured_unlit.hlsl
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/engine/shaders/dx12/shadow_depth.hlsl
    ${CMAKE_BINARY_DIR}/shaders/dx12/shadow_depth.hlsl
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/engine/shaders/dx12/light_cull.hlsl
    ${CMAKE_BINARY_DIR}/shaders/dx12/light_cull.hlsl
  VERBATIM)
add_dependencies(WhispEngine MaterialShaderSources)
if(TARGET GpuInstancingRegression)
  add_dependencies(GpuInstancingRegression MaterialShaderSources)
  add_dependencies(LightingRegression MaterialShaderSources)
endif()

if(TARGET ShadowMappingRegression)
  add_dependencies(ShadowMappingRegression MaterialShaderSources)
  if(WIN32 AND ENABLE_DX12)
    add_executable(ShadowQualityRegression tests/ShadowQualityRegression.cpp)
    target_link_libraries(ShadowQualityRegression PRIVATE Engine)
    if(ENABLE_TRACY)
      target_link_libraries(ShadowQualityRegression PRIVATE Tracy::TracyClient)
    endif()
    target_compile_definitions(ShadowQualityRegression PRIVATE ENABLE_DX12=1)
    add_dependencies(ShadowQualityRegression MaterialShaderSources)
    add_test(NAME ShadowQualityDx12Regression COMMAND ShadowQualityRegression)
    set_tests_properties(ShadowQualityDx12Regression PROPERTIES ENVIRONMENT "WHISP_DX12_DEBUG=1")
  endif()
endif()

if(TARGET MaterialSystemRegression)
  add_dependencies(MaterialSystemRegression MaterialShaderSources)
endif()

if(TARGET MaterialSystemRegression)
  add_executable(PbrCoreRegression tests/PbrCoreRegression.cpp)
  target_link_libraries(PbrCoreRegression PRIVATE Engine)
  if(ENABLE_TRACY)
    target_link_libraries(PbrCoreRegression PRIVATE Tracy::TracyClient)
  endif()
  add_dependencies(PbrCoreRegression MaterialShaderSources)
  add_test(NAME PbrCoreRegression COMMAND PbrCoreRegression)
  add_executable(PbrTexturesRegression tests/PbrTexturesRegression.cpp)
  target_link_libraries(PbrTexturesRegression PRIVATE Engine)
  if(ENABLE_TRACY)
    target_link_libraries(PbrTexturesRegression PRIVATE Tracy::TracyClient)
  endif()
  add_dependencies(PbrTexturesRegression MaterialShaderSources)
  add_test(NAME PbrTexturesRegression COMMAND PbrTexturesRegression)
  if(WIN32 AND ENABLE_DX12)
    target_compile_definitions(PbrTexturesRegression PRIVATE ENABLE_DX12=1)
    add_test(NAME PbrTexturesDx12Regression COMMAND PbrTexturesRegression --dx12)
    set_tests_properties(PbrTexturesDx12Regression PROPERTIES ENVIRONMENT "WHISP_DX12_DEBUG=1" RUN_SERIAL TRUE)
    target_compile_definitions(PbrCoreRegression PRIVATE ENABLE_DX12=1)
    add_test(NAME PbrCoreDx12Regression COMMAND PbrCoreRegression --dx12)
    set_tests_properties(PbrCoreDx12Regression PROPERTIES ENVIRONMENT "WHISP_DX12_DEBUG=1" RUN_SERIAL TRUE)
  endif()
endif()

# Keep generated stage-2 fixtures current even when the engine does not relink.
add_custom_target(PbrTextureFixtures ALL)
foreach(asset
    textures/pbr_base.png textures/pbr_normal.png textures/pbr_orm.png
    textures/pbr_control.jpg textures/pbr_control.dds textures/pbr_control_bc3.dds
    models/pbr_textures.gltf models/pbr_textures_embedded.gltf models/pbr_textures_uv1.gltf
    materials/pbr_textures.material.json)
  get_filename_component(asset_folder ${asset} DIRECTORY)
  add_custom_command(TARGET PbrTextureFixtures POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/assets/${asset_folder}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${CMAKE_SOURCE_DIR}/engine/assets/${asset} ${CMAKE_BINARY_DIR}/assets/${asset}
    VERBATIM)
endforeach()
add_dependencies(WhispEngine PbrTextureFixtures)
if(TARGET PbrTexturesRegression)
  add_dependencies(PbrTexturesRegression PbrTextureFixtures)
endif()

add_custom_target(IblFixtures ALL
 COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/engine/assets/environments ${CMAKE_BINARY_DIR}/assets/environments)
add_dependencies(WhispEngine IblFixtures)

if(ENABLE_DX12)
  add_executable(IblRegression tests/IblRegression.cpp)
  target_link_libraries(IblRegression PRIVATE Engine)
  target_compile_definitions(IblRegression PRIVATE ENABLE_DX12=1)
  if(ENABLE_TRACY)
    target_link_libraries(IblRegression PRIVATE Tracy::TracyClient)
  endif()
  add_dependencies(IblRegression IblFixtures MaterialShaderSources)
  add_test(NAME IblRegression COMMAND IblRegression)
  add_test(NAME IblDx12Regression COMMAND IblRegression --dx12)
  set_tests_properties(IblDx12Regression PROPERTIES ENVIRONMENT "WHISP_DX12_DEBUG=1" RUN_SERIAL TRUE)
endif()

if(ENABLE_DX12)
  add_executable(LightSystemRegression tests/LightSystemRegression.cpp)
  target_link_libraries(LightSystemRegression PRIVATE Engine)
  target_compile_definitions(LightSystemRegression PRIVATE ENABLE_DX12=1)
  if(ENABLE_TRACY)
    target_link_libraries(LightSystemRegression PRIVATE Tracy::TracyClient)
  endif()
  add_dependencies(LightSystemRegression IblFixtures MaterialShaderSources PbrTextureFixtures)
  add_test(NAME LightSystemRegression COMMAND LightSystemRegression)
  add_test(NAME LightSystemDx12Regression COMMAND LightSystemRegression --dx12)
  set_tests_properties(LightSystemDx12Regression PROPERTIES ENVIRONMENT "WHISP_DX12_DEBUG=1" RUN_SERIAL TRUE)
endif()
