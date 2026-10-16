cmake_minimum_required(VERSION 3.20)
project(WhispEngine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENABLE_DX12 "Enable DirectX 12 backend" ON)
option(ENABLE_VULKAN "Enable Vulkan backend" ON)
option(ENABLE_TRACY "Enable Tracy profiler" ON)

include(FetchContent)
include(cmake/Lua.cmake)

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
  add_executable(CharacterMovementRegression tests/CharacterMovementRegression.cpp)
  target_link_libraries(CharacterMovementRegression PRIVATE Engine DearImGui ImGuizmo)
  if(TARGET Tracy::TracyClient)
    target_link_libraries(CharacterMovementRegression PRIVATE Tracy::TracyClient)
  endif()
  add_test(NAME CharacterMovementRegression COMMAND CharacterMovementRegression)
  add_executable(EditorEntityIconsRegression tests/EditorEntityIconsRegression.cpp)
  target_link_libraries(EditorEntityIconsRegression PRIVATE Engine DearImGui ImGuizmo)
  if(TARGET Tracy::TracyClient)
    target_link_libraries(EditorEntityIconsRegression PRIVATE Tracy::TracyClient)
  endif()
  add_test(NAME EditorEntityIconsRegression COMMAND EditorEntityIconsRegression)
  set_tests_properties(EditorEntityIconsRegression PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" RUN_SERIAL TRUE)
  if(ENABLE_DX12)
    target_compile_definitions(EditorEntityIconsRegression PRIVATE ENABLE_DX12=1)
    add_dependencies(EditorEntityIconsRegression MaterialShaderSources IblFixtures EditorPrimitiveFixtures)
    add_test(NAME EditorEntityIconsDx12Regression COMMAND EditorEntityIconsRegression --dx12)
    set_tests_properties(EditorEntityIconsDx12Regression PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" RUN_SERIAL TRUE ENVIRONMENT "WHISP_DX12_DEBUG=1")
  endif()
  add_executable(FogPostHotfixRegression tests/FogPostHotfixRegression.cpp)
  target_link_libraries(FogPostHotfixRegression PRIVATE Engine DearImGui ImGuizmo)
  if(TARGET Tracy::TracyClient)
    target_link_libraries(FogPostHotfixRegression PRIVATE Tracy::TracyClient)
  endif()
  add_test(NAME FogPostHotfixRegression COMMAND FogPostHotfixRegression)
  set_tests_properties(FogPostHotfixRegression PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" RUN_SERIAL TRUE)
  if(ENABLE_DX12)
    add_test(NAME FogPostHotfixDx12Regression COMMAND FogPostHotfixRegression --dx12)
    set_tests_properties(FogPostHotfixDx12Regression PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" RUN_SERIAL TRUE ENVIRONMENT "WHISP_DX12_DEBUG=1")
    target_compile_definitions(FogPostHotfixRegression PRIVATE ENABLE_DX12=1)
    add_dependencies(FogPostHotfixRegression MaterialShaderSources IblFixtures EditorPrimitiveFixtures)
  endif()
  add_executable(FogRegression tests/FogRegression.cpp)
  target_link_libraries(FogRegression PRIVATE Engine DearImGui ImGuizmo)
  if(TARGET Tracy::TracyClient)
    target_link_libraries(FogRegression PRIVATE Tracy::TracyClient)
  endif()
  add_test(NAME FogRegression COMMAND FogRegression)
  set_tests_properties(FogRegression PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" RUN_SERIAL TRUE)
  if(ENABLE_DX12)
    target_compile_definitions(FogRegression PRIVATE ENABLE_DX12=1)
    add_dependencies(FogRegression MaterialShaderSources IblFixtures EditorPrimitiveFixtures)
    add_test(NAME FogDx12Regression COMMAND FogRegression --dx12)
    set_tests_properties(FogDx12Regression PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" RUN_SERIAL TRUE ENVIRONMENT "WHISP_DX12_DEBUG=1")
  endif()
  add_executable(MultiSelectionRegression tests/MultiSelectionRegression.cpp)
  target_link_libraries(MultiSelectionRegression PRIVATE Engine DearImGui ImGuizmo)
  if(TARGET Tracy::TracyClient)
    target_link_libraries(MultiSelectionRegression PRIVATE Tracy::TracyClient)
  endif()
  add_test(NAME MultiSelectionRegression COMMAND MultiSelectionRegression)
  if(ENABLE_DX12)
    target_compile_definitions(MultiSelectionRegression PRIVATE ENABLE_DX12=1)
    add_dependencies(MultiSelectionRegression MaterialShaderSources IblFixtures EditorPrimitiveFixtures)
    add_test(NAME MultiSelectionDx12Regression COMMAND MultiSelectionRegression --dx12)
  endif()
  set_tests_properties(MultiSelectionRegression PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" RUN_SERIAL TRUE)
  if(ENABLE_DX12)
    set_tests_properties(MultiSelectionDx12Regression PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" RUN_SERIAL TRUE ENVIRONMENT "WHISP_DX12_DEBUG=1")
  endif()
  add_executable(LuaPhysicsEventsRegression tests/LuaPhysicsEventsRegression.cpp)
  add_dependencies(LuaPhysicsEventsRegression LuaGameplayAssets)
  target_link_libraries(LuaPhysicsEventsRegression PRIVATE Engine DearImGui ImGuizmo)
  if(TARGET Tracy::TracyClient)
    target_link_libraries(LuaPhysicsEventsRegression PRIVATE Tracy::TracyClient)
  endif()
  add_test(NAME LuaPhysicsEventsRegression COMMAND LuaPhysicsEventsRegression)
  set_tests_properties(LuaPhysicsEventsRegression PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" RUN_SERIAL TRUE)
  if(ENABLE_DX12)
    target_compile_definitions(LuaPhysicsEventsRegression PRIVATE ENABLE_DX12=1)
    add_dependencies(LuaPhysicsEventsRegression MaterialShaderSources IblFixtures EditorPrimitiveFixtures)
    add_test(NAME LuaPhysicsEventsDx12Regression COMMAND LuaPhysicsEventsRegression --dx12)
    set_tests_properties(LuaPhysicsEventsDx12Regression PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" RUN_SERIAL TRUE ENVIRONMENT "WHISP_DX12_DEBUG=1")
  endif()
  add_executable(LuaGameplayApiRegression tests/LuaGameplayApiRegression.cpp)
  add_dependencies(LuaGameplayApiRegression LuaGameplayAssets)
  target_link_libraries(LuaGameplayApiRegression PRIVATE Engine DearImGui ImGuizmo)
  if(TARGET Tracy::TracyClient)
    target_link_libraries(LuaGameplayApiRegression PRIVATE Tracy::TracyClient)
  endif()
  add_test(NAME LuaGameplayApiRegression COMMAND LuaGameplayApiRegression)
  set_tests_properties(LuaGameplayApiRegression PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" RUN_SERIAL TRUE)
  if(ENABLE_DX12)
    target_compile_definitions(LuaGameplayApiRegression PRIVATE ENABLE_DX12=1)
    add_dependencies(LuaGameplayApiRegression MaterialShaderSources IblFixtures EditorPrimitiveFixtures)
    add_test(NAME LuaGameplayApiDx12Regression COMMAND LuaGameplayApiRegression --dx12)
    set_tests_properties(LuaGameplayApiDx12Regression PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" RUN_SERIAL TRUE ENVIRONMENT "WHISP_DX12_DEBUG=1")
  endif()
  add_executable(LuaGameplayControllerRegression tests/LuaGameplayControllerRegression.cpp)
  add_dependencies(LuaGameplayControllerRegression LuaGameplayAssets)
  target_link_libraries(LuaGameplayControllerRegression PRIVATE Engine DearImGui ImGuizmo)
  if(TARGET Tracy::TracyClient)
    target_link_libraries(LuaGameplayControllerRegression PRIVATE Tracy::TracyClient)
  endif()
  add_test(NAME LuaGameplayControllerRegression COMMAND LuaGameplayControllerRegression)
  if(ENABLE_DX12)
    target_compile_definitions(LuaGameplayControllerRegression PRIVATE ENABLE_DX12=1)
    add_dependencies(LuaGameplayControllerRegression MaterialShaderSources IblFixtures EditorPrimitiveFixtures)
    add_test(NAME LuaGameplayControllerDx12Regression COMMAND LuaGameplayControllerRegression --dx12)
    set_tests_properties(LuaGameplayControllerRegression LuaGameplayControllerDx12Regression PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" RUN_SERIAL TRUE ENVIRONMENT "WHISP_DX12_DEBUG=1")
  endif()
  if(ENABLE_DX12)
    target_compile_definitions(CharacterMovementRegression PRIVATE ENABLE_DX12=1)
    add_test(NAME CharacterMovementDx12Regression COMMAND CharacterMovementRegression --dx12)
    set_tests_properties(CharacterMovementDx12Regression PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" RUN_SERIAL TRUE ENVIRONMENT "WHISP_DX12_DEBUG=1")
  endif()
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
add_custom_target(LuaGameplayAssets ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/engine/assets/scripts ${CMAKE_BINARY_DIR}/assets/scripts
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/engine/assets/prefabs ${CMAKE_BINARY_DIR}/assets/prefabs
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/assets/scenes
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/engine/assets/scenes/lua_wave_demo.json ${CMAKE_BINARY_DIR}/assets/scenes/lua_wave_demo.json)
add_dependencies(WhispEngine LuaGameplayAssets)
option(WHISP_BUILD_LUA_TESTS "Build embedded Lua gameplay regressions" ON)
if(WHISP_BUILD_LUA_TESTS)
  enable_testing()
  add_executable(LuaScriptingRegression tests/LuaScriptingRegression.cpp)
  target_link_libraries(LuaScriptingRegression PRIVATE Engine)
  add_test(NAME LuaScriptingRegression COMMAND LuaScriptingRegression)
  set_tests_properties(LuaScriptingRegression PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" TIMEOUT 90)
endif()
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
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/engine/shaders/dx12/fog.hlsl ${CMAKE_BINARY_DIR}/shaders/dx12/fog.hlsl
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders/dx12
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/engine/shaders/dx12/editor_visuals.hlsl ${CMAKE_BINARY_DIR}/shaders/dx12/editor_visuals.hlsl
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
add_custom_target(FogAssets ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/assets/scenes
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/engine/assets/scenes/fog_diagnostic.json ${CMAKE_BINARY_DIR}/assets/scenes/fog_diagnostic.json
)
add_dependencies(WhispEngine MaterialShaderSources FogAssets)
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

if(WHISP_BUILD_PHYSICS_TESTS)
    add_executable(Editor2Regression tests/Editor2Regression.cpp)
    target_link_libraries(Editor2Regression PRIVATE Engine)
    if(ENABLE_TRACY)
        target_link_libraries(Editor2Regression PRIVATE Tracy::TracyClient)
    endif()
    if(ENABLE_DX12)
        add_dependencies(Editor2Regression MaterialShaderSources IblFixtures PbrTextureFixtures)
    endif()
    add_test(NAME Editor2Regression COMMAND Editor2Regression)
    set_tests_properties(Editor2Regression PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    if(ENABLE_DX12)
        target_compile_definitions(Editor2Regression PRIVATE ENABLE_DX12=1)
        add_test(NAME Editor2Dx12Regression COMMAND Editor2Regression --dx12)
        set_tests_properties(Editor2Dx12Regression PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" ENVIRONMENT "WHISP_DX12_DEBUG=1" RUN_SERIAL TRUE)
    endif()
endif()

if(WHISP_BUILD_PHYSICS_TESTS AND ENABLE_DX12)
    add_executable(EditorVisualPolishRegression tests/EditorVisualPolishRegression.cpp)
    target_link_libraries(EditorVisualPolishRegression PRIVATE Engine)
    target_compile_definitions(EditorVisualPolishRegression PRIVATE ENABLE_DX12=1)
    if(ENABLE_TRACY)
        target_link_libraries(EditorVisualPolishRegression PRIVATE Tracy::TracyClient)
    endif()
    add_dependencies(EditorVisualPolishRegression MaterialShaderSources IblFixtures)
    add_test(NAME EditorVisualPolishRegression COMMAND EditorVisualPolishRegression)
    add_test(NAME EditorVisualPolishDx12Regression COMMAND EditorVisualPolishRegression --dx12)
    add_test(NAME EditorVisualPolishShaderFallback COMMAND EditorVisualPolishRegression --shader-fallback)
    set_tests_properties(EditorVisualPolishRegression EditorVisualPolishDx12Regression EditorVisualPolishShaderFallback PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" RUN_SERIAL TRUE ENVIRONMENT "WHISP_DX12_DEBUG=1")
endif()

if(WHISP_BUILD_PHYSICS_TESTS AND ENABLE_DX12)
    add_custom_target(EditorPrimitiveFixtures
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/engine/assets/models/primitives ${CMAKE_BINARY_DIR}/assets/models/primitives
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/engine/assets/materials/default_primitive.material.json ${CMAKE_BINARY_DIR}/assets/materials/default_primitive.material.json)
    add_custom_target(EditorLevelFixtures
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/engine/assets/environments/editor24_known_sun.hdr ${CMAKE_BINARY_DIR}/assets/environments/editor24_known_sun.hdr
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/engine/assets/environments/editor24_known_sun.sun.json ${CMAKE_BINARY_DIR}/assets/environments/editor24_known_sun.sun.json)
    add_dependencies(WhispEngine EditorLevelFixtures)
    add_executable(EditorLevelWorkflowRegression tests/EditorLevelWorkflowRegression.cpp)
    add_executable(ScalePlaneGizmoRegression tests/ScalePlaneGizmoRegression.cpp)
    target_link_libraries(ScalePlaneGizmoRegression PRIVATE Engine DearImGui ImGuizmo)
    target_compile_definitions(ScalePlaneGizmoRegression PRIVATE ENABLE_DX12=1)
    add_dependencies(ScalePlaneGizmoRegression MaterialShaderSources IblFixtures EditorPrimitiveFixtures)
    if(TARGET Tracy::TracyClient)
      target_link_libraries(ScalePlaneGizmoRegression PRIVATE Tracy::TracyClient)
    endif()
    add_test(NAME ScalePlaneGizmoRegression COMMAND ScalePlaneGizmoRegression)
    set_tests_properties(ScalePlaneGizmoRegression PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" RUN_SERIAL TRUE ENVIRONMENT "WHISP_DX12_DEBUG=1")
    target_link_libraries(EditorLevelWorkflowRegression PRIVATE Engine DearImGui ImGuizmo)
    target_compile_definitions(EditorLevelWorkflowRegression PRIVATE ENABLE_DX12=1)
    if(TARGET Tracy::TracyClient)
      target_link_libraries(EditorLevelWorkflowRegression PRIVATE Tracy::TracyClient)
    endif()
    add_dependencies(EditorLevelWorkflowRegression MaterialShaderSources IblFixtures EditorPrimitiveFixtures EditorLevelFixtures)
    add_test(NAME EditorLevelWorkflowRegression COMMAND EditorLevelWorkflowRegression)
    add_test(NAME EditorLevelWorkflowDx12Regression COMMAND EditorLevelWorkflowRegression --dx12)
    set_tests_properties(EditorLevelWorkflowRegression EditorLevelWorkflowDx12Regression PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" RUN_SERIAL TRUE ENVIRONMENT "WHISP_DX12_DEBUG=1")
    add_executable(EditorWorkflowRegression tests/EditorWorkflowRegression.cpp)
    target_link_libraries(EditorWorkflowRegression PRIVATE Engine DearImGui ImGuizmo)
    target_compile_definitions(EditorWorkflowRegression PRIVATE ENABLE_DX12=1)
    if(TARGET Tracy::TracyClient)
      target_link_libraries(EditorWorkflowRegression PRIVATE Tracy::TracyClient)
    endif()
    add_dependencies(EditorWorkflowRegression MaterialShaderSources IblFixtures EditorPrimitiveFixtures)
    add_test(NAME EditorWorkflowRegression COMMAND EditorWorkflowRegression)
    add_test(NAME EditorWorkflowDx12Regression COMMAND EditorWorkflowRegression --dx12)
    set_tests_properties(EditorWorkflowRegression EditorWorkflowDx12Regression PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" RUN_SERIAL TRUE ENVIRONMENT "WHISP_DX12_DEBUG=1")
    add_executable(EditorUsabilityRegression tests/EditorUsabilityRegression.cpp)
    target_link_libraries(EditorUsabilityRegression PRIVATE Engine DearImGui)
    target_compile_definitions(EditorUsabilityRegression PRIVATE ENABLE_DX12=1)
    if(ENABLE_TRACY)
        target_link_libraries(EditorUsabilityRegression PRIVATE Tracy::TracyClient)
    endif()
    add_dependencies(EditorUsabilityRegression MaterialShaderSources IblFixtures)
    add_test(NAME EditorUsabilityRegression COMMAND EditorUsabilityRegression)
    add_test(NAME EditorUsabilityDx12Regression COMMAND EditorUsabilityRegression --dx12)
    set_tests_properties(EditorUsabilityRegression EditorUsabilityDx12Regression PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" RUN_SERIAL TRUE ENVIRONMENT "WHISP_DX12_DEBUG=1")
    add_executable(AssetFileOperationsRegression tests/AssetFileOperationsRegression.cpp)
    target_link_libraries(AssetFileOperationsRegression PRIVATE Engine DearImGui ImGuizmo)
    target_compile_definitions(AssetFileOperationsRegression PRIVATE ENABLE_DX12=1)
    if(TARGET Tracy::TracyClient)
      target_link_libraries(AssetFileOperationsRegression PRIVATE Tracy::TracyClient)
    endif()
    add_test(NAME AssetFileOperationsRegression COMMAND AssetFileOperationsRegression)
    add_test(NAME AssetFileOperationsDx12Regression COMMAND AssetFileOperationsRegression --dx12)
    set_tests_properties(AssetFileOperationsRegression AssetFileOperationsDx12Regression PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" RUN_SERIAL TRUE ENVIRONMENT "WHISP_DX12_DEBUG=1")
endif()

if(WHISP_BUILD_PHYSICS_TESTS AND ENABLE_DX12)
    add_executable(Editor25Regression tests/Editor25Regression.cpp)
    target_link_libraries(Editor25Regression PRIVATE Engine DearImGui ImGuizmo)
    target_compile_definitions(Editor25Regression PRIVATE ENABLE_DX12=1)
    if(TARGET Tracy::TracyClient)
      target_link_libraries(Editor25Regression PRIVATE Tracy::TracyClient)
    endif()
    add_dependencies(Editor25Regression MaterialShaderSources IblFixtures EditorPrimitiveFixtures EditorLevelFixtures)
    add_test(NAME Editor25Regression COMMAND Editor25Regression)
    add_test(NAME Editor25Dx12Regression COMMAND Editor25Regression --dx12)
    set_tests_properties(Editor25Regression Editor25Dx12Regression PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" RUN_SERIAL TRUE ENVIRONMENT "WHISP_DX12_DEBUG=1")
endif()
