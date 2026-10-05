add_library(atlantis_image_regression_support STATIC
  pixel_diff.cpp
  png_codec.cpp
  provenance.cpp
  golden_validity.cpp
  # Plan 0047 M3: a committed asset's Asset ID, from the catalog source.
  catalog_asset_id.cpp
)

target_include_directories(atlantis_image_regression_support
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(atlantis_image_regression_support
  PUBLIC
    Atlantis::Core
    Atlantis::AssetSystem
  PRIVATE
    Stb::Stb
    atlantis_compiler_warnings
)

target_compile_definitions(atlantis_image_regression_support PRIVATE
  ATLANTIS_ASSET_CATALOG_SOURCE_PATH="${CMAKE_SOURCE_DIR}/assets/asset_catalog.txt"
)

add_library(Atlantis::ImageRegressionSupport ALIAS atlantis_image_regression_support)
