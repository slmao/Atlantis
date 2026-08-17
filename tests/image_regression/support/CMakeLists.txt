add_library(atlantis_image_regression_support STATIC
  pixel_diff.cpp
  png_codec.cpp
  provenance.cpp
  golden_validity.cpp
)

target_include_directories(atlantis_image_regression_support
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(atlantis_image_regression_support
  PUBLIC
    Atlantis::Core
  PRIVATE
    Stb::Stb
    atlantis_compiler_warnings
)

add_library(Atlantis::ImageRegressionSupport ALIAS atlantis_image_regression_support)
