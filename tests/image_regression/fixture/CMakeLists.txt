add_library(atlantis_image_regression_fixture STATIC
  minimal_cube_fixture.cpp
  world_scene_fixture.cpp
  world_scene_loaded_fixture.cpp
  textured_quad_fixture.cpp
  material_demo_fixture.cpp
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
    # Plan 0018 Milestone 16 (Spec 0018 D12): material_demo_fixture.h's own
    # public API takes a atlantis::runtime::BootstrapConfig by const
    # reference -- this is the ONE explicitly-sanctioned exception to
    # RuntimeHost's usual "no other top-level module may depend on
    # Atlantis::RuntimeHost" boundary (the whole tests/image_regression/
    # tree is the sanctioned consumer here, not a new one); PUBLIC (not
    # PRIVATE, matching Atlantis::World's own placement just above for the
    # identical reason) so consumers of this library that #include
    # material_demo_fixture.h -- material_demo_gpu_tests.cpp and the
    # material_demo golden generator -- see BootstrapConfig's own include
    # path without a second, redundant direct link.
    Atlantis::RuntimeHost
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
