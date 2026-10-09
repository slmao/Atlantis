# Plan 0058 P4 (J4): the GPU-free atlantis.remote/1 fixture server the C#
# tests attach to. Test-only; declared only when C# is enabled.
add_executable(atlantis_csharp_fixture_server
  main.cpp
)

# The codec is Atlantis Remote's private wire library (as tests/remote uses
# it), for --record's JSON lines.
target_include_directories(atlantis_csharp_fixture_server PRIVATE ${CMAKE_SOURCE_DIR}/src/remote/src)

target_link_libraries(atlantis_csharp_fixture_server
  PRIVATE
    Atlantis::RemoteServer
    atlantis_remote_wire
    atlantis_compiler_warnings
)
