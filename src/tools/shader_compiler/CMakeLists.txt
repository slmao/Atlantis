# See Plan 0008 Section 1 for atlantis_shader_compiler's own dependency
# graph: it links neither RHI nor the RHI-integration target -- Tools
# never constructs a PipelineCreateParams or touches an RHI type.
#
# Mechanical deviation from Plan 0008 (no architectural effect): the
# process-execution/command-line logic below is factored into a small
# STATIC library, atlantis_shader_compiler_lib, rather than compiled
# directly into the atlantis_shader_compiler executable -- CMake cannot
# link a test executable against another executable's object files, and
# Plan 0008 Section 9 requires process_launch.cpp's own logic to be
# directly unit-testable (tests/tools/shader_compiler/CMakeLists.txt
# links this same library). This does not change the PUBLIC target
# graph Plan 0008's Verification Checklist inspects: no new externally-
# visible target gains an RHI or Atlantis::ShaderSystem+RHI dependency,
# and atlantis_shader_compiler itself still transitively links only
# Atlantis::ShaderSystem/Atlantis::Core, no RHI.
add_library(atlantis_shader_compiler_lib STATIC
  process_launch.cpp
  command_line_quoting.cpp
  compile_and_validate.cpp
)

target_include_directories(atlantis_shader_compiler_lib
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(atlantis_shader_compiler_lib
  PUBLIC
    Atlantis::ShaderSystem
    Atlantis::Core
  PRIVATE
    atlantis_compiler_warnings
)

add_executable(atlantis_shader_compiler
  main.cpp
)

target_link_libraries(atlantis_shader_compiler
  PRIVATE
    atlantis_shader_compiler_lib
    atlantis_compiler_warnings
)
