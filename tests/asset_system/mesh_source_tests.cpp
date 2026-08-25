#include <atlantis/asset_system/mesh_source.h>

#include <catch2/catch_test_macros.hpp>

using namespace atlantis::asset_system;

namespace {

constexpr std::string_view kValidTriangleSource =
    "atlantis_static_mesh_source_version: 2\n"
    "vertex_count: 3\n"
    "index_count: 3\n"
    "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0\n"
    "vertex: 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.0\n"
    "vertex: 0.0 1.0 0.0 0.0 0.0 1.0 0.0 1.0\n"
    "index: 0 1 2\n";

}  // namespace

TEST_CASE("parseMeshSource parses a well-formed triangle", "[asset_system]") {
  const auto result = parseMeshSource(kValidTriangleSource);
  REQUIRE(result.isOk());
  const ParsedMeshSource& parsed = result.value();
  REQUIRE(parsed.vertices.size() == 3);
  REQUIRE(parsed.indices.size() == 3);
  CHECK(parsed.vertices[0].positionX == 0.0f);
  CHECK(parsed.vertices[1].positionX == 1.0f);
  CHECK(parsed.vertices[1].colorG == 1.0f);
  CHECK(parsed.vertices[1].uvU == 1.0f);
  CHECK(parsed.vertices[1].uvV == 0.0f);
  CHECK(parsed.vertices[2].uvU == 0.0f);
  CHECK(parsed.vertices[2].uvV == 1.0f);
  CHECK(parsed.indices[0] == 0);
  CHECK(parsed.indices[1] == 1);
  CHECK(parsed.indices[2] == 2);
}

TEST_CASE("parseMeshSource accepts a source with no trailing newline", "[asset_system]") {
  std::string noTrailingNewline(kValidTriangleSource);
  noTrailingNewline.pop_back();  // drop the final '\n'
  const auto result = parseMeshSource(noTrailingNewline);
  REQUIRE(result.isOk());
  CHECK(result.value().vertices.size() == 3);
}

TEST_CASE("parseMeshSource tolerates a single trailing \\r per line", "[asset_system]") {
  std::string crlf;
  for (char c : kValidTriangleSource) {
    if (c == '\n') crlf += '\r';
    crlf += c;
  }
  const auto result = parseMeshSource(crlf);
  REQUIRE(result.isOk());
  CHECK(result.value().vertices.size() == 3);
}

TEST_CASE("serializeMeshSource then parseMeshSource round-trips exactly-representable values, including UV outside "
          "[0, 1]",
          "[asset_system]") {
  // Plan 0017 V9: at least one UV value outside [0, 1], confirming no
  // clamp is applied anywhere in the authoring round-trip.
  ParsedMeshSource source;
  source.vertices = {
      {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f},
      {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 2.0f, -1.5f},
      {0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 0.5f},
  };
  source.indices = {0, 1, 2};

  const std::string text = serializeMeshSource(source);
  const auto reparsed = parseMeshSource(text);
  REQUIRE(reparsed.isOk());
  REQUIRE(reparsed.value().vertices.size() == source.vertices.size());
  for (std::size_t i = 0; i < source.vertices.size(); ++i) {
    CHECK(reparsed.value().vertices[i].positionX == source.vertices[i].positionX);
    CHECK(reparsed.value().vertices[i].positionY == source.vertices[i].positionY);
    CHECK(reparsed.value().vertices[i].positionZ == source.vertices[i].positionZ);
    CHECK(reparsed.value().vertices[i].colorR == source.vertices[i].colorR);
    CHECK(reparsed.value().vertices[i].colorG == source.vertices[i].colorG);
    CHECK(reparsed.value().vertices[i].colorB == source.vertices[i].colorB);
    CHECK(reparsed.value().vertices[i].uvU == source.vertices[i].uvU);
    CHECK(reparsed.value().vertices[i].uvV == source.vertices[i].uvV);
  }
  CHECK(reparsed.value().indices == source.indices);
}

TEST_CASE("parseMeshSource rejects the old, pre-UV0 version line", "[asset_system]") {
  // Plan 0017 Section D1/ADR-0058: version 1 (six-field, no UV0) is
  // rejected outright -- no dual-version reader.
  const auto result = parseMeshSource("atlantis_static_mesh_source_version: 1\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::UnknownSourceVersion);
}

TEST_CASE("parseMeshSource rejects an unrecognized version line", "[asset_system]") {
  const auto result = parseMeshSource("atlantis_static_mesh_source_version: 3\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::UnknownSourceVersion);
}

TEST_CASE("parseMeshSource rejects an empty file", "[asset_system]") {
  const auto result = parseMeshSource("");
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::UnknownSourceVersion);
}

TEST_CASE("parseMeshSource rejects a truncated file (missing index line)", "[asset_system]") {
  const std::string_view truncated =
      "atlantis_static_mesh_source_version: 2\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0\n"
      "vertex: 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.0\n"
      "vertex: 0.0 1.0 0.0 0.0 0.0 1.0 0.0 1.0\n";
  const auto result = parseMeshSource(truncated);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::MissingField);
}

TEST_CASE("parseMeshSource rejects a wrong field-order line", "[asset_system]") {
  const std::string_view wrongOrder =
      "atlantis_static_mesh_source_version: 2\n"
      "index_count: 3\n"
      "vertex_count: 3\n";
  const auto result = parseMeshSource(wrongOrder);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::FieldOrderMismatch);
}

TEST_CASE("parseMeshSource rejects a malformed numeric token", "[asset_system]") {
  const std::string_view malformed =
      "atlantis_static_mesh_source_version: 2\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: not_a_number 0.0 0.0 1.0 0.0 0.0 0.0 0.0\n";
  const auto result = parseMeshSource(malformed);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::MalformedNumber);
}

TEST_CASE("parseMeshSource rejects a malformed UV token", "[asset_system]") {
  const std::string_view malformedUv =
      "atlantis_static_mesh_source_version: 2\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 not_a_number 0.0\n";
  const auto result = parseMeshSource(malformedUv);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::MalformedNumber);
}

TEST_CASE("parseMeshSource rejects a double-space field separator", "[asset_system]") {
  // A double space splits into an extra empty token, changing the total
  // field count to 9 (not 8) -- caught by the field-count check
  // (CountMismatch) before per-field numeric parsing ever runs, not
  // MalformedNumber. Either error would correctly reject the line; this
  // asserts the actual, more specific one the field-count-first check
  // order produces.
  const std::string_view doubleSpace =
      "atlantis_static_mesh_source_version: 2\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: 0.0  0.0 0.0 1.0 0.0 0.0 0.0 0.0\n";
  const auto result = parseMeshSource(doubleSpace);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::CountMismatch);
}

TEST_CASE("parseMeshSource rejects a non-finite position/color float", "[asset_system]") {
  const std::string_view nonFinite =
      "atlantis_static_mesh_source_version: 2\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: nan 0.0 0.0 1.0 0.0 0.0 0.0 0.0\n";
  const auto result = parseMeshSource(nonFinite);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::NonFiniteFloat);
}

TEST_CASE("parseMeshSource rejects a non-finite UV float", "[asset_system]") {
  const std::string_view nonFiniteUv =
      "atlantis_static_mesh_source_version: 2\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 nan 0.0\n";
  const auto result = parseMeshSource(nonFiniteUv);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::NonFiniteFloat);
}

TEST_CASE("parseMeshSource rejects a vertex line with the wrong field count", "[asset_system]") {
  const std::string_view wrongFieldCount =
      "atlantis_static_mesh_source_version: 2\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: 0.0 0.0 0.0\n";
  const auto result = parseMeshSource(wrongFieldCount);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::CountMismatch);
}

TEST_CASE("parseMeshSource rejects a version-2-labeled vertex line still missing its UV0 columns (7 fields)",
          "[asset_system]") {
  // Plan 0017 Decision item 3: a version-2-labeled source whose vertex
  // line was not actually updated to 8 fields (UV0 partially or fully
  // omitted) is a hard parse failure, never an implicit (0, 0) default.
  const std::string_view missingUv =
      "atlantis_static_mesh_source_version: 2\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0\n";
  const auto result = parseMeshSource(missingUv);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::CountMismatch);
}

TEST_CASE("parseMeshSource rejects a version-2-labeled vertex line with the exact old, pre-UV0 field count "
          "(6 fields, UV0 fully omitted)",
          "[asset_system]") {
  // Same real-world mistake as above, at the other boundary: the
  // version line was bumped to 2 but the vertex line itself was left
  // completely untouched from the old 6-field (position + color only)
  // grammar.
  const std::string_view oldFieldCount =
      "atlantis_static_mesh_source_version: 2\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: 0.0 0.0 0.0 1.0 0.0 0.0\n";
  const auto result = parseMeshSource(oldFieldCount);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::CountMismatch);
}

TEST_CASE("parseMeshSource rejects an index line with the wrong field count", "[asset_system]") {
  const std::string_view wrongIndexFieldCount =
      "atlantis_static_mesh_source_version: 2\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0\n"
      "vertex: 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.0\n"
      "vertex: 0.0 1.0 0.0 0.0 0.0 1.0 0.0 1.0\n"
      "index: 0 1\n";
  const auto result = parseMeshSource(wrongIndexFieldCount);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::CountMismatch);
}

TEST_CASE("parseMeshSource rejects an out-of-range index", "[asset_system]") {
  const std::string_view outOfRange =
      "atlantis_static_mesh_source_version: 2\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0\n"
      "vertex: 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.0\n"
      "vertex: 0.0 1.0 0.0 0.0 0.0 1.0 0.0 1.0\n"
      "index: 0 1 3\n";
  const auto result = parseMeshSource(outOfRange);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::IndexOutOfRange);
}

TEST_CASE("parseMeshSource rejects a negative index (unsigned-only parsing)", "[asset_system]") {
  const std::string_view negativeIndex =
      "atlantis_static_mesh_source_version: 2\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0\n"
      "vertex: 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.0\n"
      "vertex: 0.0 1.0 0.0 0.0 0.0 1.0 0.0 1.0\n"
      "index: -1 1 2\n";
  const auto result = parseMeshSource(negativeIndex);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::MalformedNumber);
}

TEST_CASE("parseMeshSource rejects index_count that is not a positive multiple of three", "[asset_system]") {
  const auto result = parseMeshSource(
      "atlantis_static_mesh_source_version: 2\n"
      "vertex_count: 3\n"
      "index_count: 4\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::IndexCountNotMultipleOfThree);
}

TEST_CASE("parseMeshSource rejects index_count of zero", "[asset_system]") {
  const auto result = parseMeshSource(
      "atlantis_static_mesh_source_version: 2\n"
      "vertex_count: 3\n"
      "index_count: 0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::IndexCountNotMultipleOfThree);
}

TEST_CASE("parseMeshSource rejects vertex_count of zero", "[asset_system]") {
  const auto result = parseMeshSource(
      "atlantis_static_mesh_source_version: 2\n"
      "vertex_count: 0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::VertexCountOutOfRange);
}

TEST_CASE("parseMeshSource rejects vertex_count above 65535", "[asset_system]") {
  const auto result = parseMeshSource(
      "atlantis_static_mesh_source_version: 2\n"
      "vertex_count: 65536\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::VertexCountOutOfRange);
}

TEST_CASE("parseMeshSource rejects a huge index_count unsupported by the file's actual line count, without "
          "attempting a huge allocation",
          "[asset_system]") {
  // A malformed file declaring an implausibly large index_count (here,
  // divisible by 3, so it clears IndexCountNotMultipleOfThree) must be
  // rejected before parsed.indices.reserve(indexCount) ever runs --
  // otherwise a tiny file could trigger a multi-gigabyte allocation
  // attempt. If this regresses, this test either fails on
  // MissingField/CountMismatch mismatch or hangs/aborts the process
  // trying to allocate ~8GB for a handful of bytes of input.
  const std::string_view hugeIndexCount =
      "atlantis_static_mesh_source_version: 2\n"
      "vertex_count: 3\n"
      "index_count: 4000000002\n"
      "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0\n"
      "vertex: 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.0\n"
      "vertex: 0.0 1.0 0.0 0.0 0.0 1.0 0.0 1.0\n"
      "index: 0 1 2\n";
  const auto result = parseMeshSource(hugeIndexCount);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::MissingField);
}

TEST_CASE("parseMeshSource rejects trailing content after the final index line", "[asset_system]") {
  std::string withTrailing(kValidTriangleSource);
  withTrailing += "extra garbage line\n";
  const auto result = parseMeshSource(withTrailing);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::TrailingContent);
}
