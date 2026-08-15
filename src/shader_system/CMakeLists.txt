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

# See Plan 0008 Section 1/6: the ONLY target in the repository with a
# compile-time dependency on both Atlantis::ShaderSystem and
# Atlantis::RHI. PUBLIC on both because its own public header
# (vertex_input_mapping.h) returns atlantis::rhi::VertexInputLayout,
# built from an atlantis::shader_system::ReflectionMetadata input -- any
# consumer of this header necessarily sees both types.
add_library(atlantis_shader_system_rhi_integration STATIC
  rhi_integration/src/vertex_input_mapping.cpp
)
add_library(Atlantis::ShaderSystemRhiIntegration ALIAS atlantis_shader_system_rhi_integration)

target_include_directories(atlantis_shader_system_rhi_integration
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/rhi_integration/include
)

target_link_libraries(atlantis_shader_system_rhi_integration
  PUBLIC
    Atlantis::ShaderSystem
    Atlantis::RHI
  PRIVATE
    atlantis_compiler_warnings
)

# Plan 0008 Section 7: a single shader pair's producer, as a stamp-based,
# all-or-nothing transaction. The OUTPUT is a single stamp file, not the
# four real artifacts -- CMake's own dependency graph only ever asks
# "does ${NAME}.stamp exist and is it newer than DEPENDS" to decide
# whether this custom command needs to (re)run; the four real files are
# BYPRODUCTS (per the official CMake documentation's own definition:
# files the command is expected to produce but whose own modification
# time is not the staleness signal). Callable by any directory processed
# after this one in the same configure run (root CMakeLists.txt's own
# add_subdirectory() ordering comment) -- e.g.
# shaders/minimal_renderer/CMakeLists.txt (Section 8).
function(atlantis_add_slang_shader_pair)
  set(options "")
  set(oneValueArgs NAME SOURCE VERTEX_ENTRY FRAGMENT_ENTRY OUTPUT_DIR EXPECTED_CONTRACT)
  set(multiValueArgs "")
  cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  set(stamp "${ARG_OUTPUT_DIR}/${ARG_NAME}.stamp")
  add_custom_command(
    OUTPUT "${stamp}"
    BYPRODUCTS
      "${ARG_OUTPUT_DIR}/${ARG_NAME}.vert.spv" "${ARG_OUTPUT_DIR}/${ARG_NAME}.vert.refl.json"
      "${ARG_OUTPUT_DIR}/${ARG_NAME}.frag.spv" "${ARG_OUTPUT_DIR}/${ARG_NAME}.frag.refl.json"
    COMMAND atlantis_shader_compiler
      --slangc-path=${ATLANTIS_SLANGC_EXECUTABLE}
      --spirv-val-path=${ATLANTIS_SPIRV_VAL_EXECUTABLE}
      --source=${ARG_SOURCE} --vertex-entry=${ARG_VERTEX_ENTRY} --fragment-entry=${ARG_FRAGMENT_ENTRY}
      --output-dir=${ARG_OUTPUT_DIR} --expected-contract=${ARG_EXPECTED_CONTRACT}
      --stamp=${stamp}
    DEPENDS ${ARG_SOURCE} atlantis_shader_compiler
    COMMENT "Compiling and validating Slang shader pair: ${ARG_NAME}"
    VERBATIM
  )
  add_custom_target(${ARG_NAME}_shaders ALL DEPENDS "${stamp}")
  set(ATLANTIS_${ARG_NAME}_SHADER_OUTPUT_DIR "${ARG_OUTPUT_DIR}" PARENT_SCOPE)
endfunction()

# Known, human-reviewed multi-config behavior (Plan 0008 Section 7/9's
# own sequential Debug/Release regression test, run against a real
# throwaway fixture during this Plan's own implementation): the FIRST
# time a second configuration (e.g. Release, after Debug has already
# built once in the same binary tree) builds this target, the shader
# pair is recompiled exactly once, even though neither the stamp nor
# SOURCE is actually stale. Root cause, confirmed by timestamp
# inspection: DEPENDS above includes the atlantis_shader_compiler
# TARGET (ADR-0029's "build the tool, then use it" idiom); that target
# is built per-configuration, so a new configuration's own first build
# of it produces a freshly-timestamped executable newer than the
# already-existing, configuration-independent stamp -- CMake's own
# dependency check correctly (per its own documented behavior) treats
# that as "a DEPENDS input is newer than OUTPUT" and reruns once.
# Verified NOT to recur on any later build of an already-built
# configuration (Release's second build, and Debug's own subsequent
# builds, are true no-ops) -- this is a one-time, self-resolving cost
# per newly-introduced configuration, not an ongoing staleness-tracking
# failure, and the published artifact remains single, shared, and
# correct across configurations throughout. Human Review confirmed this
# does not call ADR-0031's configuration-independent-artifact decision
# into question and does not require the "unsupported concurrent
# multi-config" escalation Section 12 defines for a genuine conflicting-
# rule symptom.
