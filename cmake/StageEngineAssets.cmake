# Engine-owned editor resources, independent of test projects and test targets.
if(NOT IS_DIRECTORY "${ENGINE_ASSET_SOURCE_ROOT}/models/primitives" OR
   NOT EXISTS "${ENGINE_ASSET_SOURCE_ROOT}/scripts/FirstPersonController.lua")
  message(FATAL_ERROR "Missing engine built-in resources")
endif()
file(MAKE_DIRECTORY "${ENGINE_ASSET_OUTPUT_ROOT}")
file(COPY "${ENGINE_ASSET_SOURCE_ROOT}/" DESTINATION "${ENGINE_ASSET_OUTPUT_ROOT}"
  PATTERN "*.wmesh" EXCLUDE PATTERN "*.wtex" EXCLUDE)
