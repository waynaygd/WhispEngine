function(setup_shaders target_name)

  # DX12 shaders (HLSL -> DXIL)
  if (WIN32 AND ENABLE_DX12)
    set(DXC_EXE ${CMAKE_SOURCE_DIR}/external/dxc/bin/x64/dxc.exe)

    set(DX12_SHADER_OUT_DIR ${CMAKE_BINARY_DIR}/shaders/dx12)
    file(MAKE_DIRECTORY ${DX12_SHADER_OUT_DIR})

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
      DEPENDS
        ${DX12_SHADER_OUT_DIR}/triangle_vs.dxil
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

    set(VK_SHADER_SRC_DIR ${CMAKE_SOURCE_DIR}/engine/shaders/vulkan)
    set(VK_VERT_SRC ${VK_SHADER_SRC_DIR}/triangle.vert)
    set(VK_FRAG_SRC ${VK_SHADER_SRC_DIR}/triangle.frag)

    # Генерим SPV в build/shaders/vulkan
    set(VK_SHADER_OUT_DIR ${CMAKE_BINARY_DIR}/shaders/vulkan)
    file(MAKE_DIRECTORY ${VK_SHADER_OUT_DIR})

    # Важно: имена должны совпадать с тем, что грузит VkRenderAdapter
    set(VK_VERT_SPV ${VK_SHADER_OUT_DIR}/triangle.vert.spv)
    set(VK_FRAG_SPV ${VK_SHADER_OUT_DIR}/triangle.frag.spv)

    add_custom_command(
      OUTPUT ${VK_VERT_SPV}
      COMMAND ${GLSLANG_VALIDATOR} -V ${VK_VERT_SRC} -o ${VK_VERT_SPV}
      DEPENDS ${VK_VERT_SRC}
      VERBATIM
    )

    add_custom_command(
      OUTPUT ${VK_FRAG_SPV}
      COMMAND ${GLSLANG_VALIDATOR} -V ${VK_FRAG_SRC} -o ${VK_FRAG_SPV}
      DEPENDS ${VK_FRAG_SRC}
      VERBATIM
    )

    add_custom_target(VulkanShaders ALL
      DEPENDS
        ${VK_VERT_SPV}
        ${VK_FRAG_SPV}
    )

    add_dependencies(${target_name} VulkanShaders)

    # Копируем рядом с exe, чтобы приложение грузило "shaders/vulkan/*.spv"
    add_custom_command(TARGET ${target_name} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory
              $<TARGET_FILE_DIR:${target_name}>/shaders/vulkan
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              ${VK_VERT_SPV}
              $<TARGET_FILE_DIR:${target_name}>/shaders/vulkan/triangle.vert.spv
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              ${VK_FRAG_SPV}
              $<TARGET_FILE_DIR:${target_name}>/shaders/vulkan/triangle.frag.spv
    )
  endif()

endfunction()
