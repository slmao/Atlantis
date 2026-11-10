cmake_minimum_required(VERSION 3.24)
project(AtlantisSavedSceneIdentity LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build the existing producer's identity reader on the first saved-slot
# configure, without depending on a previously built Runtime or catalog.
set(root "${CMAKE_CURRENT_SOURCE_DIR}/../../../..")
include("${root}/cmake/CompilerWarnings.cmake")
add_library(Cgltf::Cgltf INTERFACE IMPORTED)
target_include_directories(Cgltf::Cgltf SYSTEM INTERFACE "${ATLANTIS_CGLTF_SOURCE}")
add_subdirectory("${root}/src/core" core)
add_subdirectory("${root}/src/asset_system" asset_system)
add_subdirectory(.. gltf_importer)
add_executable(saved_scene_identity main.cpp)
target_link_libraries(saved_scene_identity PRIVATE atlantis_gltf_importer_lib atlantis_compiler_warnings)
set_target_properties(saved_scene_identity PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<0:>")
