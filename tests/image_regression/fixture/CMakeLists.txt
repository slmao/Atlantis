add_library(atlantis_image_regression_fixture STATIC
  minimal_cube_fixture.cpp
  world_scene_fixture.cpp
  world_scene_loaded_fixture.cpp
)

target_include_directories(atlantis_image_regression_fixture
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(atlantis_image_regression_fixture
  PUBLIC
    Atlantis::RHI
    Atlantis::Renderer
    Atlantis::ImageRegressionSupport
    # Plan 0014 Section D10: WorldSceneFixture's own public header names
    # atlantis::world::World in a public field type.
    Atlantis::World
  PRIVATE
    Atlantis::VulkanBackend
    Atlantis::RenderGraph
    Atlantis::ShaderSystem
    Atlantis::ShaderSystemRhiIntegration
    # Plan 0012 Step 6: this fixture is the composition root -- it
    # depends on Atlantis::AssetSystem to load CPU-side mesh data and
    # then calls the existing, unmodified createMesh() itself. Asset
    # System itself gains no dependency in the other direction.
    Atlantis::AssetSystem
    atlantis_compiler_warnings
)

add_library(Atlantis::ImageRegressionFixture ALIAS atlantis_image_regression_fixture)
