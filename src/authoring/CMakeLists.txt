# Plan 0065 P2/P3 (Spec 0065 R1-R6, R9; ADR-0129 D1, ADR-0130 D2): Atlantis
# Authoring -- the authoring session that owns the document being edited, its
# InProcess endpoint implementing Atlantis Connection's AuthoringConnection,
# and the document's validation. Depends on Atlantis Asset System and Atlantis
# Connection only: no Runtime, World ECS, Platform, RHI, Renderer or OS header,
# checked by tests/authoring/authoring_boundary_tests.cpp. Depended on by the
# atlantis_runtime executable (the host, Plan 0065 M5) and its own tests;
# Android builds it, and its host creates no session.
add_library(atlantis_authoring STATIC
  src/authoring_session.cpp
  src/document_access.cpp
  src/document_validation.cpp
)
add_library(Atlantis::Authoring ALIAS atlantis_authoring)

target_include_directories(atlantis_authoring
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(atlantis_authoring
  PUBLIC
    Atlantis::AssetSystem
    Atlantis::Connection
  PRIVATE
    atlantis_compiler_warnings
)

# The exhaustive switches over the scene schema's constraints and the closed
# command and error sets are held complete by C4062 (off by default even at
# /W4; the src/asset_system precedent), which /WX makes an error.
target_compile_options(atlantis_authoring PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/w14062>
)
