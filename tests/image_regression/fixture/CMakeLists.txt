add_library(atlantis_image_regression_fixture STATIC
  minimal_cube_fixture.cpp
)

target_include_directories(atlantis_image_regression_fixture
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(atlantis_image_regression_fixture
  PUBLIC
    Atlantis::RHI
    Atlantis::Renderer
    Atlantis::ImageRegressionSupport
  PRIVATE
    Atlantis::VulkanBackend
    Atlantis::RenderGraph
    Atlantis::ShaderSystem
    Atlantis::ShaderSystemRhiIntegration
    atlantis_compiler_warnings
)

add_library(Atlantis::ImageRegressionFixture ALIAS atlantis_image_regression_fixture)
