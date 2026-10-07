# Plan 0055 P1 (Spec 0055 ruling Q6, M-a; ADR-0106 D7): Atlantis Remote -- the
# loopback-TCP JSON-lines transport whose payload is RuntimeConnection and
# RuntimeControl calls. Two public halves: the client (RemoteConnection,
# RemoteControl; linked by the `atlantis` executable) and the server (linked by
# atlantis_runtime only). Both depend on Atlantis Connection only; Remote does
# not depend on Platform, Runtime or anything GPU-side (checked by
# tests/remote/remote_boundary_tests.cpp).
#
# atlantis_remote_wire is the module-private code both halves share: the wire
# codec, the line framing, the session-file format, and the OS socket layer.
# Like the Vulkan Backend's WSI code, the socket layer is one private header
# (src/os/socket.h) with one translation unit per OS, chosen here; no OS type
# reaches a public header.
if(WIN32)
  set(ATLANTIS_REMOTE_SOCKET_SOURCE src/os/socket_win32.cpp)
else()
  set(ATLANTIS_REMOTE_SOCKET_SOURCE src/os/socket_posix.cpp)
endif()

add_library(atlantis_remote_wire STATIC
  src/codec.cpp
  src/line_channel.cpp
  src/session_file.cpp
  ${ATLANTIS_REMOTE_SOCKET_SOURCE}
)
target_include_directories(atlantis_remote_wire
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(atlantis_remote_wire
  PUBLIC
    Atlantis::Connection
  PRIVATE
    atlantis_compiler_warnings
)
if(WIN32)
  target_link_libraries(atlantis_remote_wire PRIVATE ws2_32)
endif()

add_library(atlantis_remote_client STATIC
  src/remote_client.cpp
)
add_library(Atlantis::RemoteClient ALIAS atlantis_remote_client)
target_include_directories(atlantis_remote_client
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(atlantis_remote_client
  PUBLIC
    Atlantis::Connection
  PRIVATE
    atlantis_remote_wire
    atlantis_compiler_warnings
)

add_library(atlantis_remote_server STATIC
  src/remote_server.cpp
)
add_library(Atlantis::RemoteServer ALIAS atlantis_remote_server)
target_include_directories(atlantis_remote_server
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(atlantis_remote_server
  PUBLIC
    Atlantis::Connection
  PRIVATE
    atlantis_remote_wire
    atlantis_compiler_warnings
)
