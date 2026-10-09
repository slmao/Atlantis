# Plan 0057 P3 (ADR-0111 D1): the schema-to-C++ binding generator, host-only
# like the other tools (added inside the top level's if(NOT ANDROID)). The
# library is the pure generator (Core only); the executable feeds it World's
# table. Its output is committed, never generated at build time.
add_library(atlantis_sdk_codegen_lib STATIC
  generate_bindings.cpp
  # Plan 0058 P5 (ADR-0112 D3): the C# backend.
  generate_csharp_bindings.cpp
)

target_include_directories(atlantis_sdk_codegen_lib
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(atlantis_sdk_codegen_lib
  PUBLIC
    Atlantis::Core
  PRIVATE
    atlantis_compiler_warnings
)

add_executable(atlantis_sdk_codegen
  main.cpp
)

target_link_libraries(atlantis_sdk_codegen
  PRIVATE
    atlantis_sdk_codegen_lib
    Atlantis::World
    atlantis_compiler_warnings
)
