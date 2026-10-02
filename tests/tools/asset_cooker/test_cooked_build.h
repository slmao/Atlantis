#pragma once

// Plan 0047 M4 (P13): a small real build for the assembly tests -- one
// mesh, texture, material and scene cooked in-process by runCookCommand(),
// with the catalog source, declarations list and fragment list the build
// would hand to --kind=assemble-catalog.

#include <cook_command.h>
#include "test_catalog_source.h"

#include <atlantis/asset_system/asset_guid.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace atlantis::tools::asset_cooker::test {

struct CookedBuild {
  std::filesystem::path catalogSource;
  std::filesystem::path declarations;
  std::filesystem::path fragmentList;
  std::filesystem::path outDir;  // where the cooks wrote, and where the catalog goes
  atlantis::asset_system::AssetGuid scene;
};

inline CookedBuild cookSmallBuild(const std::filesystem::path& dir, const std::filesystem::path& pngSource) {
  namespace fs = std::filesystem;
  using atlantis::asset_system::toString;
  const fs::path assets = dir / "assets";
  const auto write = [](const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
  };
  write(assets / "meshes/tri.mesh.txt",
        "atlantis_static_mesh_source_version: 3\n"
        "vertex_count: 3\n"
        "index_count: 3\n"
        "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0 0.577350269 0.577350269 0.577350269\n"
        "vertex: 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.0 0.577350269 0.577350269 0.577350269\n"
        "vertex: 0.0 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.577350269 0.577350269 0.577350269\n"
        "index: 0 1 2\n");
  fs::create_directories(assets / "textures");
  fs::copy_file(pngSource, assets / "textures/t.png", fs::copy_options::overwrite_existing);
  write(assets / "materials/m.material.txt",
        "atlantis_material_source_version: 10\n"
        "kind: unlit_textured\n"
        "texture: " + toString(testCatalogGuid("textures/t.png")) + "\n"
        "filter: linear\n"
        "address_mode: repeat\n");
  write(assets / "scenes/s.scene.txt",
        "atlantis_scene_source_version: 7\n"
        "node_count: 1\n"
        "active_camera: none\n"
        "node: node_id=1 guid=00470047-0000-4000-8000-000000000001 parent=none position=0.0 0.0 0.0 "
        "rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 mesh=" + toString(testCatalogGuid("meshes/tri.mesh.txt")) +
            " material=" + toString(testCatalogGuid("materials/m.material.txt")) + "\n");

  CookedBuild build;
  build.outDir = dir / "out";
  build.catalogSource = writeCatalogSourceCovering(assets, dir / "asset_catalog_source.txt");
  build.scene = testCatalogGuid("scenes/s.scene.txt");

  struct Cook {
    AssetKind kind;
    const char* source;
    const char* stamp;
    const char* artifact;
  };
  const Cook cooks[] = {
      {AssetKind::StaticMesh, "meshes/tri.mesh.txt", "", "meshes/tri.amesh"},
      {AssetKind::Texture, "textures/t.png", "t", "t.atex"},
      {AssetKind::Material, "materials/m.material.txt", "", "materials/m.amaterial"},
      {AssetKind::Scene, "scenes/s.scene.txt", "", "scenes/s.ascene"},
  };
  std::string fragments;
  for (const Cook& cook : cooks) {
    CookCommandRequest request;
    request.kind = cook.kind;
    request.sourcePath = (assets / cook.source).string();
    request.assetRoot = assets.string();
    request.outputDir = build.outDir.string();
    if (cook.stamp[0] != '\0') request.stampPath = (build.outDir / (std::string(cook.stamp) + ".stamp")).string();
    if (cook.kind == AssetKind::Texture) request.colorSpace = "unorm";
    request.catalogSourcePath = build.catalogSource.string();
    REQUIRE(runCookCommand(request) == 0);
    fragments += (build.outDir / (std::string(cook.artifact) + ".catalog.txt")).string() + "\n";
  }
  build.fragmentList = dir / "fragments.txt";
  write(build.fragmentList, fragments);
  build.declarations = dir / "declarations.txt";
  write(build.declarations,
        "mesh\tassets\tmeshes/tri.mesh.txt\n"
        "texture\tassets\ttextures/t.png\n"
        "material\tassets\tmaterials/m.material.txt\n"
        "scene\tassets\tscenes/s.scene.txt\n");
  return build;
}

}  // namespace atlantis::tools::asset_cooker::test
