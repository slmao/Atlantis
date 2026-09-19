# ADR-0084: atlantis_gltf_importer_lib (parsing/mapping/emission, directly
# unit-testable by tests/tools/gltf_importer/) + a thin executable,
# mirroring atlantis_asset_cooker. Links Atlantis::AssetSystem/Core
# PUBLIC; cgltf PRIVATE -- no public header includes cgltf.h (enforced by
# the include-scan test in tests/tools/gltf_importer/).
add_library(atlantis_gltf_importer_lib STATIC
  import_command.cpp
)

target_include_directories(atlantis_gltf_importer_lib
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(atlantis_gltf_importer_lib
  PUBLIC
    Atlantis::AssetSystem
    Atlantis::Core
  PRIVATE
    Cgltf::Cgltf
    atlantis_compiler_warnings
)

# Compile-time exhaustiveness for gltfImportErrorMessage()'s no-default
# switch -- same scoped MSVC C4062 opt-in as atlantis_asset_cooker_lib.
target_compile_options(atlantis_gltf_importer_lib PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/w14062>
)

add_executable(atlantis_gltf_importer main.cpp)
target_link_libraries(atlantis_gltf_importer
  PRIVATE
    atlantis_gltf_importer_lib
    atlantis_compiler_warnings
)
