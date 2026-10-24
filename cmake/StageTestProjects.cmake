# Compatibility fixtures for regressions, not a runtime project search path.
foreach(project IN ITEMS legacy_workspace interactive_room)
  if(NOT IS_DIRECTORY "${PROJECT_SOURCE_ROOT}/${project}/Assets")
    message(FATAL_ERROR "Missing standalone test project: ${project}")
  endif()
endforeach()
file(MAKE_DIRECTORY "${FIXTURE_OUTPUT_ROOT}/scenes")
file(COPY "${PROJECT_SOURCE_ROOT}/legacy_workspace/Assets/"
  DESTINATION "${FIXTURE_OUTPUT_ROOT}" PATTERN "*.wmesh" EXCLUDE PATTERN "*.wtex" EXCLUDE)
file(COPY "${PROJECT_SOURCE_ROOT}/legacy_workspace/Scenes/"
  DESTINATION "${FIXTURE_OUTPUT_ROOT}/scenes")
file(COPY "${PROJECT_SOURCE_ROOT}/interactive_room/Assets/"
  DESTINATION "${FIXTURE_OUTPUT_ROOT}" PATTERN "*.wmesh" EXCLUDE PATTERN "*.wtex" EXCLUDE)
file(COPY "${PROJECT_SOURCE_ROOT}/interactive_room/Scenes/variants/"
  DESTINATION "${FIXTURE_OUTPUT_ROOT}/scenes")
