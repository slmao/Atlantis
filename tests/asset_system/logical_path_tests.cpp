#include <atlantis/asset_system/logical_path.h>

#include <catch2/catch_test_macros.hpp>

using namespace atlantis::asset_system;

// Plan 0012 V1: every legal form normalizes as specified; every illegal
// form returns its own distinct LogicalPathError. The algorithm under
// test (logical_path.cpp) contains no #ifdef and never constructs a
// std::filesystem::path, so this single test binary's results are valid
// evidence for both Windows and a future Android build -- no
// platform-specific variant is needed.

TEST_CASE("normalizeLogicalPath accepts already-normalized paths unchanged", "[asset_system]") {
  auto result = normalizeLogicalPath("meshes/minimal_cube.mesh.txt");
  REQUIRE(result.isOk());
  CHECK(result.value() == "meshes/minimal_cube.mesh.txt");
}

TEST_CASE("normalizeLogicalPath normalizes backslashes to forward slashes", "[asset_system]") {
  auto result = normalizeLogicalPath("meshes\\minimal_cube.mesh.txt");
  REQUIRE(result.isOk());
  CHECK(result.value() == "meshes/minimal_cube.mesh.txt");
}

TEST_CASE("normalizeLogicalPath collapses redundant separators", "[asset_system]") {
  auto result = normalizeLogicalPath("meshes//minimal_cube.mesh.txt");
  REQUIRE(result.isOk());
  CHECK(result.value() == "meshes/minimal_cube.mesh.txt");
}

TEST_CASE("normalizeLogicalPath drops '.' segments", "[asset_system]") {
  auto result = normalizeLogicalPath("meshes/./minimal_cube.mesh.txt");
  REQUIRE(result.isOk());
  CHECK(result.value() == "meshes/minimal_cube.mesh.txt");
}

TEST_CASE("normalizeLogicalPath resolves '..' segments within the asset root", "[asset_system]") {
  auto result = normalizeLogicalPath("meshes/extra/../minimal_cube.mesh.txt");
  REQUIRE(result.isOk());
  CHECK(result.value() == "meshes/minimal_cube.mesh.txt");
}

TEST_CASE("normalizeLogicalPath drops a trailing slash", "[asset_system]") {
  auto result = normalizeLogicalPath("meshes/minimal_cube.mesh.txt/");
  REQUIRE(result.isOk());
  CHECK(result.value() == "meshes/minimal_cube.mesh.txt");
}

TEST_CASE("normalizeLogicalPath preserves case", "[asset_system]") {
  auto result = normalizeLogicalPath("Meshes/MinimalCube.MESH.txt");
  REQUIRE(result.isOk());
  CHECK(result.value() == "Meshes/MinimalCube.MESH.txt");
}

TEST_CASE("normalizeLogicalPath rejects an empty path", "[asset_system]") {
  auto result = normalizeLogicalPath("");
  REQUIRE(result.isErr());
  CHECK(result.error() == LogicalPathError::EmptyPath);
}

TEST_CASE("normalizeLogicalPath rejects a path that normalizes to nothing", "[asset_system]") {
  auto result = normalizeLogicalPath(".");
  REQUIRE(result.isErr());
  CHECK(result.error() == LogicalPathError::EmptyPath);
}

TEST_CASE("normalizeLogicalPath rejects an absolute (POSIX-style) path", "[asset_system]") {
  auto result = normalizeLogicalPath("/meshes/minimal_cube.mesh.txt");
  REQUIRE(result.isErr());
  CHECK(result.error() == LogicalPathError::AbsolutePathRejected);
}

TEST_CASE("normalizeLogicalPath rejects a UNC-style path", "[asset_system]") {
  auto result = normalizeLogicalPath("\\\\server\\share\\meshes\\minimal_cube.mesh.txt");
  REQUIRE(result.isErr());
  CHECK(result.error() == LogicalPathError::AbsolutePathRejected);
}

TEST_CASE("normalizeLogicalPath rejects a Windows drive-letter prefix with its own distinct error",
          "[asset_system]") {
  auto result = normalizeLogicalPath("C:\\meshes\\minimal_cube.mesh.txt");
  REQUIRE(result.isErr());
  CHECK(result.error() == LogicalPathError::DriveLetterRejected);
}

TEST_CASE("normalizeLogicalPath rejects a lowercase drive-letter prefix too", "[asset_system]") {
  auto result = normalizeLogicalPath("c:/meshes/minimal_cube.mesh.txt");
  REQUIRE(result.isErr());
  CHECK(result.error() == LogicalPathError::DriveLetterRejected);
}

TEST_CASE("normalizeLogicalPath rejects '..' that would escape the asset root", "[asset_system]") {
  auto result = normalizeLogicalPath("../minimal_cube.mesh.txt");
  REQUIRE(result.isErr());
  CHECK(result.error() == LogicalPathError::EscapesAssetRoot);
}

TEST_CASE("normalizeLogicalPath rejects '..' that would escape the root even after descending first",
          "[asset_system]") {
  auto result = normalizeLogicalPath("meshes/../../minimal_cube.mesh.txt");
  REQUIRE(result.isErr());
  CHECK(result.error() == LogicalPathError::EscapesAssetRoot);
}

TEST_CASE("normalizeLogicalPath rejects a non-ASCII byte", "[asset_system]") {
  // "caf\xC3\xA9.mesh.txt" -- UTF-8 for "café.mesh.txt"; Phase 1 is
  // ASCII-only by design (ADR-0044), not a Unicode-handling oversight.
  auto result = normalizeLogicalPath("meshes/caf\xC3\xA9.mesh.txt");
  REQUIRE(result.isErr());
  CHECK(result.error() == LogicalPathError::DisallowedCharacter);
}

TEST_CASE("normalizeLogicalPath rejects a colon that is not a drive-letter prefix", "[asset_system]") {
  auto result = normalizeLogicalPath("meshes/foo:bar.mesh.txt");
  REQUIRE(result.isErr());
  CHECK(result.error() == LogicalPathError::DisallowedCharacter);
}

TEST_CASE("normalizeLogicalPath is deterministic across repeated calls (platform-neutral by construction)",
          "[asset_system]") {
  const auto first = normalizeLogicalPath("meshes/extra/../minimal_cube.mesh.txt");
  const auto second = normalizeLogicalPath("meshes/extra/../minimal_cube.mesh.txt");
  REQUIRE(first.isOk());
  REQUIRE(second.isOk());
  CHECK(first.value() == second.value());
}
