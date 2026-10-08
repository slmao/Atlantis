# Plan 0057 P1 (ADR-0110 D1): Atlantis Gameplay SDK -- a client library over
# RuntimeConnection only. Its typed layer is generated from the schema by
# atlantis_sdk_codegen (src/tools/sdk_codegen/) and committed under
# include/atlantis/gameplay/generated/ (ADR-0111 D1): no build-time
# generation, so the library builds on Android unchanged (Plan 0057 J2's
# .gitattributes keeps the committed header LF).
add_library(atlantis_gameplay_sdk STATIC
  src/error.cpp
  # Plan 0057 M2: the reflective layer and the query batch.
  src/query_batch.cpp
  src/subscription.cpp
  src/transaction.cpp
  src/world.cpp
  # Plan 0057 M3: the typed layer's binding check, and one translation unit
  # that includes the generated World bindings, so their static_asserts are
  # compiled wherever the library is (Android included).
  src/compatibility.cpp
  src/generated_world.cpp
)
add_library(Atlantis::GameplaySdk ALIAS atlantis_gameplay_sdk)

target_include_directories(atlantis_gameplay_sdk
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(atlantis_gameplay_sdk
  PUBLIC
    Atlantis::Connection
  PRIVATE
    atlantis_compiler_warnings
)
