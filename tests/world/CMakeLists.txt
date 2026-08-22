add_executable(atlantis_world_tests
  entity_lifecycle_tests.cpp
  hierarchy_tests.cpp
  math_contract_tests.cpp
  update_transforms_tests.cpp
  module_boundary_tests.cpp
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

catch_discover_tests(atlantis_world_tests DISCOVERY_MODE PRE_TEST)
