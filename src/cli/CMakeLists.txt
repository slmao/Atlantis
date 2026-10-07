# Plan 0054 P1 (Spec 0054 ruling Q6, B-a; ADR-0105 D3): Atlantis CLI -- the
# minimal CLI client (the six commands and the per-frame script runner). It
# knows only a RuntimeConnection: it links Atlantis::Connection and nothing
# else, and includes no Runtime, ECS, Platform, RHI or Renderer header --
# both checked by tests/cli/cli_boundary_tests.cpp. Hosted in v1 by the
# atlantis_runtime executable (`--exec`, Spec 0054 ruling Q3).
add_library(atlantis_cli STATIC
  src/command.cpp
  src/script_runner.cpp
  # Plan 0055 P9 (Spec 0055 rulings Q3-Q6): the command tree, its JSON, and
  # the one-shot / REPL driver of the `atlantis` executable.
  src/command_layer.cpp
  src/invocation.cpp
)
add_library(Atlantis::Cli ALIAS atlantis_cli)

target_include_directories(atlantis_cli
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(atlantis_cli
  PUBLIC
    Atlantis::Connection
  PRIVATE
    atlantis_compiler_warnings
)
