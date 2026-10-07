# Plan 0055 M1: Atlantis Remote -- the shared JSON (Connection), the wire codec
# (private: tests include src/remote/src directly), the session file, and the
# module's boundary scan (no OS type in public headers; OS code only in
# src/os/; nothing of Runtime, the ECS or the GPU stack).
add_executable(atlantis_remote_tests
  remote_json_tests.cpp
  remote_codec_tests.cpp
  remote_session_file_tests.cpp
  remote_boundary_tests.cpp
)

target_include_directories(atlantis_remote_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/remote/src)

target_link_libraries(atlantis_remote_tests
  PRIVATE
    Atlantis::RemoteClient
    Atlantis::RemoteServer
    atlantis_remote_wire
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

target_compile_definitions(atlantis_remote_tests PRIVATE
  ATLANTIS_REMOTE_SOURCE_DIR="${CMAKE_SOURCE_DIR}/src/remote"
)

catch_discover_tests(atlantis_remote_tests DISCOVERY_MODE PRE_TEST)
