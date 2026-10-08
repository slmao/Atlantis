# Plan 0057 P6 (Spec 0057 R8, ruling Q8; ADR-0110 D6): the real Gameplay SDK
# client, attached to `atlantis_runtime --listen` through Atlantis Remote's
# client half -- the second executable to link it, after `atlantis`. The
# Runtime does not link the SDK.
add_executable(atlantis_gameplay_demo
  main.cpp
)

target_link_libraries(atlantis_gameplay_demo
  PRIVATE
    Atlantis::GameplaySdk
    Atlantis::RemoteClient
    atlantis_compiler_warnings
)
