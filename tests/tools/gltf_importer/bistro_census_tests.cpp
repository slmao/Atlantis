#include <catch2/catch_test_macros.hpp>

#include <cgltf.h>

#include <filesystem>
#include <set>
#include <string>

// Plan 0037 Milestone 3: the Milestone 1 census, kept as a standing smoke test
// over the real fetched Bistro content (ctest label "content"). Mesh and
// material layers only; the scene-graph layer is added in Milestone 5.
// cgltf's implementation is provided by atlantis_gltf_importer_lib.

namespace {

struct CgltfData {
  cgltf_data* data = nullptr;
  ~CgltfData() {
    if (data != nullptr) cgltf_free(data);
  }
};

}  // namespace

TEST_CASE("Real Bistro glTF matches the Spec 0037 Investigation 1 census", "[bistro]") {
  const std::filesystem::path dir{ATLANTIS_BISTRO_CONTENT_DIR};
  const std::filesystem::path gltf = dir / "bistro.gltf";
  if (!std::filesystem::exists(gltf)) {
    SKIP("Bistro content not found at " << dir.string() << " -- run tools/content/fetch_bistro.ps1");
  }

  CgltfData guard;
  cgltf_options options{};
  REQUIRE(cgltf_parse_file(&options, gltf.string().c_str(), &guard.data) == cgltf_result_success);
  const std::string root = dir.string() + "/";
  REQUIRE(cgltf_load_buffers(&options, guard.data, root.c_str()) == cgltf_result_success);
  REQUIRE(cgltf_validate(guard.data) == cgltf_result_success);
  const cgltf_data& data = *guard.data;

  std::set<std::string> used;
  for (cgltf_size i = 0; i < data.extensions_used_count; ++i) used.insert(data.extensions_used[i]);
  CHECK(used == std::set<std::string>{"KHR_materials_pbrSpecularGlossiness", "KHR_materials_transmission",
                                      "MSFT_texture_dds"});
  CHECK(data.extensions_required_count == 0);

  CHECK(data.meshes_count == 551);
  std::size_t primitives = 0;
  std::size_t vertices = 0;
  std::size_t triangles = 0;
  std::size_t overU16 = 0;
  for (cgltf_size m = 0; m < data.meshes_count; ++m) {
    for (cgltf_size p = 0; p < data.meshes[m].primitives_count; ++p) {
      const cgltf_primitive& primitive = data.meshes[m].primitives[p];
      ++primitives;
      CHECK(primitive.type == cgltf_primitive_type_triangles);
      REQUIRE(primitive.indices != nullptr);
      REQUIRE(primitive.attributes_count > 0);
      const cgltf_size vertexCount = primitive.attributes[0].data->count;
      vertices += vertexCount;
      triangles += primitive.indices->count / 3;
      if (vertexCount > 65535) ++overU16;
    }
  }
  CHECK(primitives == 551);
  CHECK(vertices == 1'738'262);
  CHECK(triangles == 1'753'630);
  CHECK(overU16 == 3);

  CHECK(data.materials_count == 254);
  std::size_t specGloss = 0;
  std::size_t transmission = 0;
  for (cgltf_size i = 0; i < data.materials_count; ++i) {
    specGloss += data.materials[i].has_pbr_specular_glossiness ? 1 : 0;
    transmission += data.materials[i].has_transmission ? 1 : 0;
  }
  CHECK(specGloss == 234);
  CHECK(transmission == 18);
  CHECK(data.nodes_count == 5908);
  CHECK(data.textures_count == 343);
}
