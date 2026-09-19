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
    Cgltf::Cgltf
  PRIVATE
    atlantis_compiler_warnings
)

add_executable(atlantis_gltf_importer main.cpp)
target_link_libraries(atlantis_gltf_importer
  PRIVATE
    atlantis_gltf_importer_lib
    atlantis_compiler_warnings
)
