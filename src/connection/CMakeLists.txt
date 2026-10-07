# Plan 0054 P1 (Spec 0054 ruling Q4, M-b; ADR-0105 D3): Atlantis Connection --
# the RuntimeConnection interface, its InProcess endpoint over the Runtime
# World's operation boundary, and the client text forms. Depends on Core and
# Atlantis World's public access (and schema) headers only; depended on by
# Runtime, which owns the endpoint, and by clients (Atlantis CLI). Its sources'
# includes are checked by tests/cli/cli_boundary_tests.cpp.
add_library(atlantis_connection STATIC
  src/runtime_connection.cpp
  src/in_process_endpoint.cpp
  src/text.cpp
  src/json.cpp
)
add_library(Atlantis::Connection ALIAS atlantis_connection)

target_include_directories(atlantis_connection
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# PUBLIC: the interface's own value types (PropertyValue, Command, Event,
# tickets, AccessError) are Atlantis World's, named in this module's headers.
target_link_libraries(atlantis_connection
  PUBLIC
    Atlantis::World
  PRIVATE
    atlantis_compiler_warnings
)
