function(setup_shaders target_name)

  # DX12 shaders (HLSL -> DXIL)
if (WIN32 AND ENABLE_DX12)
  set(DXC_EXE ${CMAKE_SOURCE_DIR}/external/dxc/bin/x64/dxc.exe)

  # Куда складываем сгенерённые DXIL (в build dir)
  set(DX12_SHADER_OUT_DIR ${CMAKE_BINARY_DIR}/shaders/dx12)
  file(MAKE_DIRECTORY ${DX12_SHADER_OUT_DIR})

  # Откуда берём исходник (в source dir)
  set(DX12_SHADER_SRC ${CMAKE_SOURCE_DIR}/engine/shaders/dx12/triangle.hlsl)

  add_custom_command(
    OUTPUT ${DX12_SHADER_OUT_DIR}/triangle_vs.dxil
    COMMAND ${DXC_EXE} -T vs_6_6 -E VSMain -O3
            -Fo ${DX12_SHADER_OUT_DIR}/triangle_vs.dxil
            ${DX12_SHADER_SRC}
    DEPENDS ${DX12_SHADER_SRC}
    VERBATIM
  )

  add_custom_command(
    OUTPUT ${DX12_SHADER_OUT_DIR}/triangle_ps.dxil
    COMMAND ${DXC_EXE} -T ps_6_6 -E PSMain -O3
            -Fo ${DX12_SHADER_OUT_DIR}/triangle_ps.dxil
            ${DX12_SHADER_SRC}
    DEPENDS ${DX12_SHADER_SRC}
    VERBATIM
  )

  add_custom_target(Dx12Shaders ALL
    DEPENDS ${DX12_SHADER_OUT_DIR}/triangle_vs.dxil
            ${DX12_SHADER_OUT_DIR}/triangle_ps.dxil
  )

  add_dependencies(${target_name} Dx12Shaders)
endif()


 # Vulkan shaders (GLSL -> SPIR-V)
if (ENABLE_VULKAN)
  if (NOT DEFINED ENV{VULKAN_SDK})
    message(FATAL_ERROR "VULKAN_SDK env var is not set. Install Vulkan SDK.")
  endif()

  set(GLSLANG_VALIDATOR "$ENV{VULKAN_SDK}/Bin/glslangValidator.exe")

  set(VK_SHADER_OUT_DIR ${CMAKE_BINARY_DIR}/shaders/vulkan)
  file(MAKE_DIRECTORY ${VK_SHADER_OUT_DIR})

  set(VK_VERT_SRC ${CMAKE_SOURCE_DIR}/engine/shaders/vulkan/triangle.vert)
  set(VK_FRAG_SRC ${CMAKE_SOURCE_DIR}/engine/shaders/vulkan/triangle.frag)

  add_custom_command(
    OUTPUT ${VK_SHADER_OUT_DIR}/triangle_vert.spv
    COMMAND ${GLSLANG_VALIDATOR} -V -o ${VK_SHADER_OUT_DIR}/triangle_vert.spv ${VK_VERT_SRC}
    DEPENDS ${VK_VERT_SRC}
    VERBATIM
  )

  add_custom_command(
    OUTPUT ${VK_SHADER_OUT_DIR}/triangle_frag.spv
    COMMAND ${GLSLANG_VALIDATOR} -V -o ${VK_SHADER_OUT_DIR}/triangle_frag.spv ${VK_FRAG_SRC}
    DEPENDS ${VK_FRAG_SRC}
    VERBATIM
  )

  add_custom_target(VulkanShaders ALL
    DEPENDS ${VK_SHADER_OUT_DIR}/triangle_vert.spv
            ${VK_SHADER_OUT_DIR}/triangle_frag.spv
  )

  add_dependencies(${target_name} VulkanShaders)
endif()

endfunction()
