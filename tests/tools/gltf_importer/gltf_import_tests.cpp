#include "gltf_test_builder.h"
#include "import_command.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atlantis/asset_system/mesh_artifact.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

// Plan 0037 Milestone 3: GPU-independent importer tests over tiny glTF files
// built in memory (gltf_test_builder.h). Offsets are .amesh's 60-byte vertex
// layout (mesh_artifact.h): color at 12, tangent at 44.

namespace fs = std::filesystem;
using atlantis::gltf_importer::GltfImportError;
using atlantis::gltf_importer::importGltfMeshes;

namespace {

constexpr std::size_t kColorOffset = 12;
constexpr std::size_t kTangentOffset = 44;

struct ImportRun {
  fs::path outputDir;
  atlantis::Result<atlantis::gltf_importer::GltfImportSummary, GltfImportError> result;
};

ImportRun runImport(const std::string& testName, const gltf_test::PrimitiveSpec& spec,
                    const std::string& outputName = "out") {
  const fs::path dir = gltf_test::freshDirectory(testName);
  const fs::path input = gltf_test::writeGltf(dir, spec);
  const fs::path outputDir = dir / outputName;
  return ImportRun{outputDir, importGltfMeshes(input, dir, outputDir, "t")};
}

atlantis::asset_system::DecodedMeshArtifactU32 decodeOnlyMesh(const fs::path& outputDir) {
  const auto decoded = atlantis::asset_system::decodeMeshArtifactU32(gltf_test::readBytes(outputDir / "t_mesh_0_0.amesh"));
  REQUIRE(decoded.isOk());
  return decoded.value();
}

bool reportContains(const atlantis::gltf_importer::GltfImportSummary& summary, const std::string& needle) {
  return std::any_of(summary.reportLines.begin(), summary.reportLines.end(),
                     [&](const std::string& line) { return line.find(needle) != std::string::npos; });
}

void requireNoOutput(const fs::path& outputDir) {
  CHECK_FALSE(fs::exists(outputDir));
  CHECK_FALSE(fs::exists(outputDir.parent_path() / (outputDir.filename().string() + ".importing")));
}

}  // namespace

TEST_CASE("8-, 16- and 32-bit index inputs decode to the same u32 indices", "[gltf_importer]") {
  for (const int componentType : {5121, 5123, 5125}) {
    auto spec = gltf_test::unitQuad();
    spec.indexComponentType = componentType;
    const ImportRun run = runImport("index_width_" + std::to_string(componentType), spec);
    REQUIRE(run.result.isOk());
    const auto mesh = decodeOnlyMesh(run.outputDir);
    CHECK(mesh.indices == std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3});
    CHECK(mesh.vertexBytes.size() == 4 * 60);
  }
}

TEST_CASE("A mesh with more than 65,535 vertices round-trips as .amesh schema 5", "[gltf_importer]") {
  constexpr std::uint32_t kColumns = 256;
  constexpr std::uint32_t kRows = 257;  // 65,792 vertices
  gltf_test::PrimitiveSpec spec;
  for (std::uint32_t y = 0; y < kRows; ++y) {
    for (std::uint32_t x = 0; x < kColumns; ++x) {
      spec.positions.insert(spec.positions.end(), {static_cast<float>(x), static_cast<float>(y), 0.0f});
      spec.normals.insert(spec.normals.end(), {0.0f, 0.0f, 1.0f});
      spec.uvs.insert(spec.uvs.end(), {static_cast<float>(x) / (kColumns - 1), static_cast<float>(y) / (kRows - 1)});
    }
  }
  for (std::uint32_t y = 0; y + 1 < kRows; ++y) {
    for (std::uint32_t x = 0; x + 1 < kColumns; ++x) {
      const std::uint32_t i = y * kColumns + x;
      spec.indices.insert(spec.indices.end(), {i, i + 1, i + kColumns + 1, i, i + kColumns + 1, i + kColumns});
    }
  }
  const ImportRun run = runImport("over_u16", spec);
  REQUIRE(run.result.isOk());
  CHECK(run.result.value().meshesOverU16Range == 1);
  const auto mesh = decodeOnlyMesh(run.outputDir);
  CHECK(mesh.vertexBytes.size() == static_cast<std::size_t>(kColumns) * kRows * 60);
  CHECK(mesh.indices == spec.indices);
  CHECK(*std::max_element(mesh.indices.begin(), mesh.indices.end()) == kColumns * kRows - 1);
}

TEST_CASE("Missing COLOR_0 imports as white", "[gltf_importer]") {
  const ImportRun run = runImport("color_absent", gltf_test::unitQuad());
  REQUIRE(run.result.isOk());
  const auto mesh = decodeOnlyMesh(run.outputDir);
  for (std::size_t v = 0; v < 4; ++v) {
    for (std::size_t c = 0; c < 3; ++c) CHECK(gltf_test::vertexFloat(mesh.vertexBytes, v, kColorOffset + 4 * c) == 1.0f);
  }
}

TEST_CASE("COLOR_0 as normalized VEC4 u8 keeps RGB and drops alpha, reported", "[gltf_importer]") {
  auto spec = gltf_test::unitQuad();
  spec.colorsRgbaU8 = {255, 0, 51, 7, 255, 0, 51, 7, 255, 0, 51, 7, 255, 0, 51, 7};
  const ImportRun run = runImport("color_vec4", spec);
  REQUIRE(run.result.isOk());
  CHECK(reportContains(run.result.value(), "COLOR_0 alpha dropped"));
  const auto mesh = decodeOnlyMesh(run.outputDir);
  CHECK(gltf_test::vertexFloat(mesh.vertexBytes, 2, kColorOffset + 0) == 1.0f);
  CHECK(gltf_test::vertexFloat(mesh.vertexBytes, 2, kColorOffset + 4) == 0.0f);
  CHECK(gltf_test::vertexFloat(mesh.vertexBytes, 2, kColorOffset + 8) == Catch::Approx(0.2f));
}

TEST_CASE("Unsupported or invalid primitives fail with distinct named errors and leave no output",
          "[gltf_importer]") {
  struct Case {
    const char* name;
    GltfImportError expected;
    gltf_test::PrimitiveSpec spec;
  };
  std::vector<Case> cases;
  {
    auto s = gltf_test::unitQuad();
    s.mode = 1;  // LINES
    cases.push_back({"non_triangles", GltfImportError::NonTrianglesMode, s});
  }
  {
    auto s = gltf_test::unitQuad();
    s.includeIndices = false;
    cases.push_back({"non_indexed", GltfImportError::NonIndexedPrimitive, s});
  }
  {
    auto s = gltf_test::unitQuad();
    s.includePosition = false;
    cases.push_back({"missing_position", GltfImportError::MissingPosition, s});
  }
  {
    auto s = gltf_test::unitQuad();
    s.uvs.clear();
    cases.push_back({"missing_texcoord", GltfImportError::MissingRequiredAttribute, s});
  }
  {
    auto s = gltf_test::unitQuad();
    s.indices[5] = 4;  // == vertex count
    cases.push_back({"index_out_of_range", GltfImportError::OutOfRangeIndex, s});
  }
  {
    auto s = gltf_test::unitQuad();
    s.truncatePositionViewBytes = 12;  // last position reaches past its bufferView
    cases.push_back({"accessor_out_of_range", GltfImportError::OutOfRangeAccessor, s});
  }
  {
    auto s = gltf_test::unitQuad();
    s.normals[5] = 2.0f;  // vertex 1 normal length 2
    cases.push_back({"non_unit_normal", GltfImportError::NonUnitNormal, s});
  }

  for (const Case& c : cases) {
    INFO(c.name);
    const ImportRun run = runImport(std::string("reject_") + c.name, c.spec);
    REQUIRE(run.result.isErr());
    CHECK(run.result.error() == c.expected);
    requireNoOutput(run.outputDir);
  }
}

TEST_CASE("Upstream TANGENT is discarded and replaced by regenerated tangents", "[gltf_importer]") {
  auto spec = gltf_test::unitQuad();
  spec.tangents = {0, 1, 0, -1, 0, 1, 0, -1, 0, 1, 0, -1, 0, 1, 0, -1};  // deliberately wrong
  const ImportRun run = runImport("tangent_discarded", spec);
  REQUIRE(run.result.isOk());
  CHECK(reportContains(run.result.value(), "upstream TANGENT discarded"));
  const auto mesh = decodeOnlyMesh(run.outputDir);
  for (std::size_t v = 0; v < 4; ++v) {
    CHECK(gltf_test::vertexFloat(mesh.vertexBytes, v, kTangentOffset + 0) == Catch::Approx(1.0f));
    CHECK(gltf_test::vertexFloat(mesh.vertexBytes, v, kTangentOffset + 4) == Catch::Approx(0.0f).margin(1e-6));
    CHECK(gltf_test::vertexFloat(mesh.vertexBytes, v, kTangentOffset + 12) == 1.0f);
  }
}

TEST_CASE("A failed import leaves no output or staging directory behind", "[gltf_importer]") {
  // The normal check runs after staging has been created, so this exercises
  // the staging cleanup rather than an early structural rejection.
  auto spec = gltf_test::unitQuad();
  spec.normals[11] = 0.5f;
  const ImportRun run = runImport("failed_import_clean", spec);
  REQUIRE(run.result.isErr());
  requireNoOutput(run.outputDir);
}

TEST_CASE("Importing the same input twice produces byte-identical output", "[gltf_importer]") {
  const fs::path dir = gltf_test::freshDirectory("determinism");
  const fs::path input = gltf_test::writeGltf(dir, gltf_test::unitQuad());
  REQUIRE(importGltfMeshes(input, dir, dir / "a", "t").isOk());
  REQUIRE(importGltfMeshes(input, dir, dir / "b", "t").isOk());
  std::vector<std::string> names;
  for (const auto& entry : fs::directory_iterator(dir / "a")) names.push_back(entry.path().filename().string());
  std::sort(names.begin(), names.end());
  CHECK(names == std::vector<std::string>{"import_report.txt", "t_mesh_0_0.amesh", "t_mesh_0_0.amesh.meta.txt"});
  for (const std::string& name : names) {
    INFO(name);
    CHECK(gltf_test::readBytes(dir / "a" / name) == gltf_test::readBytes(dir / "b" / name));
  }
}

TEST_CASE("A mirrored-UV handedness conflict is resolved by the vertex split", "[gltf_importer]") {
  // Two triangles sharing vertices 0 and 2: the first maps UVs with
  // handedness +1, the second mirrors them (handedness -1). Without the split
  // ADR-0073's generator rejects the whole mesh.
  gltf_test::PrimitiveSpec spec;
  spec.positions = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0};
  spec.normals = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
  spec.uvs = {0, 0, 1, 0, 1, 1, 2, 1};
  spec.indices = {0, 1, 2, 0, 2, 3};
  const ImportRun run = runImport("handedness_split", spec);
  REQUIRE(run.result.isOk());
  const auto& summary = run.result.value();
  CHECK(summary.splitVertices == 2);
  CHECK(summary.meshesSplit == 1);
  CHECK(summary.maxSplitGrowth == Catch::Approx(0.5));
  CHECK(reportContains(summary, "handedness split duplicated 2 of 4 vertices (+50.00%)"));

  const auto mesh = decodeOnlyMesh(run.outputDir);
  REQUIRE(mesh.vertexBytes.size() == 6 * 60);
  // Duplicates of vertices 0 and 2 are appended as 4 and 5; the mirrored
  // triangle is rewritten to use them.
  CHECK(mesh.indices == std::vector<std::uint32_t>{0, 1, 2, 4, 5, 3});
  const float expectedW[] = {1.0f, 1.0f, 1.0f, -1.0f, -1.0f, -1.0f};
  for (std::size_t v = 0; v < 6; ++v) {
    INFO("vertex " << v);
    CHECK(gltf_test::vertexFloat(mesh.vertexBytes, v, kTangentOffset + 12) == expectedW[v]);
  }
}
