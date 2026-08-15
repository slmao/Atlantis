# See Plan 0008 Section 1 for this target/dependency graph. Core-only:
# no RHI, no Vulkan, no Slang, no OS-process API anywhere in this
# target's sources or public headers.
add_library(atlantis_shader_system STATIC
  src/json_parser.cpp
  src/reflection_metadata.cpp
  src/reflection_loader.cpp
  src/slang_json_transform.cpp
  src/descriptor_contract.cpp
  src/command_line.cpp
  src/version_provenance.cpp
)
add_library(Atlantis::ShaderSystem ALIAS atlantis_shader_system)

target_include_directories(atlantis_shader_system
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(atlantis_shader_system
  PUBLIC
    Atlantis::Core
  PRIVATE
    atlantis_compiler_warnings
)
