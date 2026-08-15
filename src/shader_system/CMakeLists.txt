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
