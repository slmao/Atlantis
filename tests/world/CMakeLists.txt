add_executable(atlantis_world_tests
  entity_lifecycle_tests.cpp
  hierarchy_tests.cpp
  math_contract_tests.cpp
  update_transforms_tests.cpp
  camera_tests.cpp
  renderable_tests.cpp
  light_tests.cpp
  traversal_determinism_tests.cpp
  module_boundary_tests.cpp
  scene_instantiation_tests.cpp
)

target_link_libraries(atlantis_world_tests
  PRIVATE
    Atlantis::World
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

# Plan 0014 V16: an absolute, configure-time path to scan for forbidden
# includes -- does not depend on WORKING_DIRECTORY, matching
# tests/asset_system/CMakeLists.txt's own ATLANTIS_ASSET_SYSTEM_SOURCE_DIR
# precedent.
target_compile_definitions(atlantis_world_tests PRIVATE
  ATLANTIS_WORLD_SOURCE_DIR="${CMAKE_SOURCE_DIR}/src/world"
)

# Plan 0014 V28 (2026-08-23 Human Review Correction): real, compile-time
# WorldError enum-exhaustiveness protection for renderable_tests.cpp's
# own no-default canary switch requires MSVC's C4062 ("enumerator ...
# not handled"), which is off by default even at /W4 -- matching Plan
# 0013's own already-established src/runtime/CMakeLists.txt precedent
# for classifyPresentationError()/classifySubmitError(). Scoped to this
# target only; cmake/CompilerWarnings.cmake is deliberately not touched.
target_compile_options(atlantis_world_tests PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/w14062>
)

catch_discover_tests(atlantis_world_tests DISCOVERY_MODE PRE_TEST)
