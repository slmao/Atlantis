# Dear ImGui (ocornut/imgui), the editor's UI library (ADR-0107, Spec 0056
# ruling Q1). Included from the root CMakeLists.txt outside if(NOT ANDROID):
# Android compiles the editor library (Plan 0056 J11), so it needs ImGui too.
include_guard(GLOBAL)

include(FetchContent)

# Pinned per ADR-0006 to the v1.92.9b release tag's archive, fetched via URL +
# URL_HASH (the cmake/AtlantisStb.cmake / AtlantisCgltf.cmake shape). The hash
# was measured against this exact archive (Plan 0056 M1).
FetchContent_Declare(
  imgui
  URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.tar.gz
  URL_HASH SHA256=21d8a0a565e85dce943e375db00812c2f3f0ab21f3f0f7964e364a63422d7f99
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(imgui)

# Core only (Plan 0056 P3, J8): the four core sources. Nothing under backends/
# is compiled -- no stock Vulkan, Win32 or Android backend (ADR-0107) -- and
# imconfig.h is not edited; the one configuration is a compile definition.
# SYSTEM keeps third-party warnings out of consumers' /W4 /WX builds; the
# library itself is compiled without this repository's warning flags.
add_library(atlantis_imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
)
target_include_directories(atlantis_imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR})
target_compile_definitions(atlantis_imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
add_library(ImGui::ImGui ALIAS atlantis_imgui)
