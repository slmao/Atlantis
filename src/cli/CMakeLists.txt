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

# Plan 0055 P10 (Spec 0055 ruling Q6, X-a; ADR-0106 D7): the `atlantis`
# executable -- a machine client of an attachable Runtime. Windows-only, like
# atlantis_runtime. It alone links Atlantis Remote's client half; the
# atlantis_cli library above keeps linking Atlantis::Connection only.
if(WIN32)
  add_executable(atlantis_cli_app app/main.cpp)
  set_target_properties(atlantis_cli_app PROPERTIES OUTPUT_NAME atlantis)
  target_link_libraries(atlantis_cli_app
    PRIVATE
      Atlantis::Cli
      Atlantis::RemoteClient
      atlantis_compiler_warnings
  )
endif()
