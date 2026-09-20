# cgltf (jkuhlmann/cgltf), the glTF 2.0 parser ADR-0082 selects for the
# offline glTF importer (Spec 0037, ADR-0084). Included only from the root
# CMakeLists.txt's if(NOT ANDROID) tools block: the importer is a host-only
# tool, so an Android configure never fetches this.
include_guard(GLOBAL)

include(FetchContent)

# Pinned per ADR-0006 to release v1.15's commit, fetched as a commit archive
# via URL + URL_HASH rather than GIT_TAG -- the same reason and shape as
# cmake/AtlantisStb.cmake (git smart-HTTP clones reset in this environment).
# The hash was measured against this exact archive (Plan 0037).
FetchContent_Declare(
  cgltf
  URL https://github.com/jkuhlmann/cgltf/archive/bbeb5b0b070ddacddac6852fb72143eb68454937.tar.gz
  URL_HASH SHA256=98b987d9a6b0443a830af5bcc019eb2e75d8f2d79988e0aec02db5c849c43ee4
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(cgltf)

# cgltf ships no top-level CMakeLists.txt (a single C99 header), so wrap it
# in a header-only INTERFACE target, matching Stb::Stb's shape. Exactly one
# translation unit per binary defines CGLTF_IMPLEMENTATION before including
# cgltf.h (ADR-0084: that TU lives in atlantis_gltf_importer_lib, linked
# PRIVATE). SYSTEM keeps the third-party implementation's MSVC C4996 (CRT
# fopen/strncpy deprecation) out of consumers' /W4 /WX builds.
add_library(cgltf INTERFACE)
target_include_directories(cgltf SYSTEM INTERFACE ${cgltf_SOURCE_DIR})
add_library(Cgltf::Cgltf ALIAS cgltf)
