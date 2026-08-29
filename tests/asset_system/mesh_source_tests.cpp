#include <atlantis/asset_system/mesh_source.h>

#include <catch2/catch_test_macros.hpp>

using namespace atlantis::asset_system;

namespace {

// Plan 0020 Section P2: every test below whose own intent is not about
// a specific normal value uses the same real, unit-length literal,
// "0.577350269 0.577350269 0.577350269" -- reused verbatim from Spec
// 0020 D5's own already-approved value, rather than inventing a new
// magic number.
constexpr std::string_view kValidTriangleSource =
    "atlantis_static_mesh_source_version: 3\n"
    "vertex_count: 3\n"
    "index_count: 3\n"
    "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0 0.577350269 0.577350269 0.577350269\n"
    "vertex: 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.0 0.577350269 0.577350269 0.577350269\n"
    "vertex: 0.0 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.577350269 0.577350269 0.577350269\n"
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
  CHECK(parsed.vertices[1].normalX == 0.577350269f);
  CHECK(parsed.vertices[1].normalY == 0.577350269f);
  CHECK(parsed.vertices[1].normalZ == 0.577350269f);
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
  // clamp is applied anywhere in the authoring round-trip. Plan 0020:
  // widened to 11 fields; each vertex's own normal uses a real,
  // unit-length, exactly-6-decimal-representable value (0.6/0.8/0.0
  // permutations, 0.36 + 0.64 == 1.0 exactly) -- deliberately not Spec
  // 0020 D5's own 0.577350269 literal here, since this test's own
  // subject is serializeMeshSource()'s std::to_string-based text
  // round-trip specifically, which -- unlike the real
  // cookStaticMesh()/loadStaticMeshAsset() path
  // (mesh_normal_round_trip_tests.cpp, which writes source text
  // directly, with no std::to_string involved) -- only preserves values
  // representable within std::to_string(float)'s own six-decimal-place
  // precision; a value needing more digits (0.577350269) would fail
  // this specific round-trip for a pre-existing, disclosed reason
  // unrelated to normal support itself (every other field here was
  // already chosen with this same constraint in mind).
  ParsedMeshSource source;
  source.vertices = {
      {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.6f, 0.8f, 0.0f},
      {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 2.0f, -1.5f, 0.0f, 0.6f, 0.8f},
      {0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 0.5f, 0.8f, 0.0f, 0.6f},
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
    CHECK(reparsed.value().vertices[i].normalX == source.vertices[i].normalX);
    CHECK(reparsed.value().vertices[i].normalY == source.vertices[i].normalY);
    CHECK(reparsed.value().vertices[i].normalZ == source.vertices[i].normalZ);
  }
  CHECK(reparsed.value().indices == source.indices);
}

TEST_CASE("parseMeshSource rejects the old, pre-UV0 version line", "[asset_system]") {
  // Plan 0017 Section D1/ADR-0058: version 1 (six-field, no UV0) is
  // rejected outright -- no dual-version reader. Unchanged by Plan 0020
  // (Plan Review Round 1 item 3): version 1 stays rejected under
  // version 3's own grammar exactly as it was under version 2's.
  const auto result = parseMeshSource("atlantis_static_mesh_source_version: 1\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::UnknownSourceVersion);
}

TEST_CASE("parseMeshSource rejects the old, pre-normal version line", "[asset_system]") {
  // Plan 0020 Section P1/ADR-0063: version 2 (eight-field, no normal)
  // is now also rejected outright, exactly like version 1 already was.
  const auto result = parseMeshSource("atlantis_static_mesh_source_version: 2\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::UnknownSourceVersion);
}

TEST_CASE("parseMeshSource rejects an unrecognized version line", "[asset_system]") {
  // Plan 0020 Plan Review Round 1 item 2: this literal must name a
  // value still genuinely unrecognized now that version 3 is the real,
  // accepted version -- "4" here, not "3".
  const auto result = parseMeshSource("atlantis_static_mesh_source_version: 4\n");
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
      "atlantis_static_mesh_source_version: 3\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0 0.577350269 0.577350269 0.577350269\n"
      "vertex: 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.0 0.577350269 0.577350269 0.577350269\n"
      "vertex: 0.0 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.577350269 0.577350269 0.577350269\n";
  const auto result = parseMeshSource(truncated);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::MissingField);
}

TEST_CASE("parseMeshSource rejects a wrong field-order line", "[asset_system]") {
  const std::string_view wrongOrder =
      "atlantis_static_mesh_source_version: 3\n"
      "index_count: 3\n"
      "vertex_count: 3\n";
  const auto result = parseMeshSource(wrongOrder);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::FieldOrderMismatch);
}

TEST_CASE("parseMeshSource rejects a malformed numeric token", "[asset_system]") {
  const std::string_view malformed =
      "atlantis_static_mesh_source_version: 3\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: not_a_number 0.0 0.0 1.0 0.0 0.0 0.0 0.0 0.577350269 0.577350269 0.577350269\n";
  const auto result = parseMeshSource(malformed);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::MalformedNumber);
}

TEST_CASE("parseMeshSource rejects a malformed UV token", "[asset_system]") {
  const std::string_view malformedUv =
      "atlantis_static_mesh_source_version: 3\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 not_a_number 0.0 0.577350269 0.577350269 0.577350269\n";
  const auto result = parseMeshSource(malformedUv);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::MalformedNumber);
}

TEST_CASE("parseMeshSource rejects a malformed normal token", "[asset_system]") {
  const std::string_view malformedNormal =
      "atlantis_static_mesh_source_version: 3\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0 not_a_number 0.577350269 0.577350269\n";
  const auto result = parseMeshSource(malformedNormal);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::MalformedNumber);
}

TEST_CASE("parseMeshSource rejects a double-space field separator", "[asset_system]") {
  // A double space splits into an extra empty token, changing the total
  // field count to 12 (not 11) -- caught by the field-count check
  // (CountMismatch) before per-field numeric parsing ever runs, not
  // MalformedNumber. Either error would correctly reject the line; this
  // asserts the actual, more specific one the field-count-first check
  // order produces.
  const std::string_view doubleSpace =
      "atlantis_static_mesh_source_version: 3\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: 0.0  0.0 0.0 1.0 0.0 0.0 0.0 0.0 0.577350269 0.577350269 0.577350269\n";
  const auto result = parseMeshSource(doubleSpace);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::CountMismatch);
}

TEST_CASE("parseMeshSource rejects a non-finite position/color float", "[asset_system]") {
  const std::string_view nonFinite =
      "atlantis_static_mesh_source_version: 3\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: nan 0.0 0.0 1.0 0.0 0.0 0.0 0.0 0.577350269 0.577350269 0.577350269\n";
  const auto result = parseMeshSource(nonFinite);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::NonFiniteFloat);
}

TEST_CASE("parseMeshSource rejects a non-finite UV float", "[asset_system]") {
  const std::string_view nonFiniteUv =
      "atlantis_static_mesh_source_version: 3\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 nan 0.0 0.577350269 0.577350269 0.577350269\n";
  const auto result = parseMeshSource(nonFiniteUv);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::NonFiniteFloat);
}

TEST_CASE("parseMeshSource rejects a non-finite normal float (NaN)", "[asset_system]") {
  // Plan 0020 Verification V4: NonFiniteFloat, not NonUnitNormal --
  // confirms the finiteness check runs before the magnitude check.
  const std::string_view nonFiniteNormal =
      "atlantis_static_mesh_source_version: 3\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0 nan 0.577350269 0.577350269\n";
  const auto result = parseMeshSource(nonFiniteNormal);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::NonFiniteFloat);
}

TEST_CASE("parseMeshSource rejects a non-finite normal float (Inf)", "[asset_system]") {
  const std::string_view infNormal =
      "atlantis_static_mesh_source_version: 3\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0 inf 0.0 0.0\n";
  const auto result = parseMeshSource(infNormal);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::NonFiniteFloat);
}

TEST_CASE("parseMeshSource rejects a zero-vector normal", "[asset_system]") {
  // Plan 0020 D3/V5b: finite, but lengthSquared == 0.0, far below 0.9801.
  const std::string_view zeroNormal =
      "atlantis_static_mesh_source_version: 3\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0\n";
  const auto result = parseMeshSource(zeroNormal);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::NonUnitNormal);
}

TEST_CASE("parseMeshSource rejects a grossly unnormalized normal (1, 1, 1)", "[asset_system]") {
  // lengthSquared == 3.0, far above 1.0201.
  const std::string_view unnormalized =
      "atlantis_static_mesh_source_version: 3\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0 1.0 1.0 1.0\n";
  const auto result = parseMeshSource(unnormalized);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::NonUnitNormal);
}

TEST_CASE("parseMeshSource rejects an extremely small non-zero normal", "[asset_system]") {
  // Finite, but lengthSquared is effectively 0 -- rejected by the same
  // check as the exact-zero case, no separate "near-zero" special case.
  const std::string_view tinyNormal =
      "atlantis_static_mesh_source_version: 3\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0 0.00000000000000000001 0.0 0.0\n";
  const auto result = parseMeshSource(tinyNormal);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::NonUnitNormal);
}

TEST_CASE("parseMeshSource accepts a normal with a -0.0 component", "[asset_system]") {
  // Plan 0020 D3: -0.0 is finite, and (-0.0)^2 == +0.0 exactly under
  // IEEE 754 -- behaves identically to +0.0 in this position.
  const std::string_view negativeZeroComponent =
      "atlantis_static_mesh_source_version: 3\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0 -0.0 0.816496581 0.577350269\n"
      "vertex: 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.0 0.577350269 0.577350269 0.577350269\n"
      "vertex: 0.0 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.577350269 0.577350269 0.577350269\n"
      "index: 0 1 2\n";
  const auto result = parseMeshSource(negativeZeroComponent);
  REQUIRE(result.isOk());
  CHECK(result.value().vertices[0].normalX == 0.0f);
}

TEST_CASE("parseMeshSource accepts a normal whose length-squared lands just above the lower inclusive boundary "
          "(0.99 0 0)",
          "[asset_system]") {
  // Plan 0020 Section P8: this is a Kind 3 (full parse-path
  // integration) case, not a Kind 1 exact-boundary one -- that claim
  // belongs to mesh_normal_validation_tests.cpp's own
  // isNormalLengthSquaredInTolerance(0.9801) case alone, which compares
  // the double literal 0.9801 against itself with no float involved.
  // Here, 0.99 is parsed as a float first: 0.99 has no exact binary32
  // representation, so the real parsed value is
  // 0.9900000095367431640625, whose exact double promotion squares to
  // 0.98010001888275156 -- about 1.9e-8 above the literal 0.9801, not
  // equal to it. This still correctly exercises "a real, parsed value
  // close to the lower bound is accepted," which is this test's own
  // actual, honest claim.
  const std::string_view lowerBoundary =
      "atlantis_static_mesh_source_version: 3\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0 0.99 0.0 0.0\n"
      "vertex: 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.0 0.577350269 0.577350269 0.577350269\n"
      "vertex: 0.0 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.577350269 0.577350269 0.577350269\n"
      "index: 0 1 2\n";
  const auto result = parseMeshSource(lowerBoundary);
  REQUIRE(result.isOk());
}

TEST_CASE("parseMeshSource accepts a normal whose length-squared lands just below the upper inclusive boundary "
          "(1.01 0 0)",
          "[asset_system]") {
  // Same Kind 3 caveat as the lower-boundary test above: 1.01 has no
  // exact binary32 representation either -- the real parsed value is
  // 1.0099999904632568359375, whose exact double promotion squares to
  // 1.0200999807357789, about 1.9e-8 below the literal 1.0201, not
  // equal to it. Still correctly accepted (comfortably inside the
  // inclusive range on this side too); the true exact-boundary claim
  // is Kind 1's alone (mesh_normal_validation_tests.cpp).
  const std::string_view upperBoundary =
      "atlantis_static_mesh_source_version: 3\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0 1.01 0.0 0.0\n"
      "vertex: 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.0 0.577350269 0.577350269 0.577350269\n"
      "vertex: 0.0 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.577350269 0.577350269 0.577350269\n"
      "index: 0 1 2\n";
  const auto result = parseMeshSource(upperBoundary);
  REQUIRE(result.isOk());
}

TEST_CASE("parseMeshSource rejects a vertex line with the wrong field count", "[asset_system]") {
  const std::string_view wrongFieldCount =
      "atlantis_static_mesh_source_version: 3\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: 0.0 0.0 0.0\n";
  const auto result = parseMeshSource(wrongFieldCount);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::CountMismatch);
}

TEST_CASE("parseMeshSource rejects a vertex line with the exact old, pre-normal field count "
          "(8 fields, normal fully omitted)",
          "[asset_system]") {
  // Plan 0020 Section P5: the direct analog, one version bump later, of
  // Plan 0017's own "exact old, pre-UV0 field count" boundary case --
  // a well-formed-looking line at exactly the *previous* format's own
  // complete field count.
  const std::string_view oldFieldCount =
      "atlantis_static_mesh_source_version: 3\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0\n";
  const auto result = parseMeshSource(oldFieldCount);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::CountMismatch);
}

TEST_CASE("parseMeshSource rejects a vertex line missing its UV0 and normal columns "
          "(6 fields, position and color only)",
          "[asset_system]") {
  const std::string_view missingUvAndNormal =
      "atlantis_static_mesh_source_version: 3\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: 0.0 0.0 0.0 1.0 0.0 0.0\n";
  const auto result = parseMeshSource(missingUvAndNormal);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::CountMismatch);
}

TEST_CASE("parseMeshSource rejects a vertex line missing two of its three normal columns (9 fields)",
          "[asset_system]") {
  const std::string_view missingTwoNormalComponents =
      "atlantis_static_mesh_source_version: 3\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0 0.577350269\n";
  const auto result = parseMeshSource(missingTwoNormalComponents);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::CountMismatch);
}

TEST_CASE("parseMeshSource rejects a vertex line missing one of its three normal columns (10 fields)",
          "[asset_system]") {
  const std::string_view missingOneNormalComponent =
      "atlantis_static_mesh_source_version: 3\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0 0.577350269 0.577350269\n";
  const auto result = parseMeshSource(missingOneNormalComponent);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::CountMismatch);
}

TEST_CASE("parseMeshSource rejects an index line with the wrong field count", "[asset_system]") {
  const std::string_view wrongIndexFieldCount =
      "atlantis_static_mesh_source_version: 3\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0 0.577350269 0.577350269 0.577350269\n"
      "vertex: 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.0 0.577350269 0.577350269 0.577350269\n"
      "vertex: 0.0 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.577350269 0.577350269 0.577350269\n"
      "index: 0 1\n";
  const auto result = parseMeshSource(wrongIndexFieldCount);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::CountMismatch);
}

TEST_CASE("parseMeshSource rejects an out-of-range index", "[asset_system]") {
  const std::string_view outOfRange =
      "atlantis_static_mesh_source_version: 3\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0 0.577350269 0.577350269 0.577350269\n"
      "vertex: 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.0 0.577350269 0.577350269 0.577350269\n"
      "vertex: 0.0 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.577350269 0.577350269 0.577350269\n"
      "index: 0 1 3\n";
  const auto result = parseMeshSource(outOfRange);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::IndexOutOfRange);
}

TEST_CASE("parseMeshSource rejects a negative index (unsigned-only parsing)", "[asset_system]") {
  const std::string_view negativeIndex =
      "atlantis_static_mesh_source_version: 3\n"
      "vertex_count: 3\n"
      "index_count: 3\n"
      "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0 0.577350269 0.577350269 0.577350269\n"
      "vertex: 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.0 0.577350269 0.577350269 0.577350269\n"
      "vertex: 0.0 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.577350269 0.577350269 0.577350269\n"
      "index: -1 1 2\n";
  const auto result = parseMeshSource(negativeIndex);
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::MalformedNumber);
}

TEST_CASE("parseMeshSource rejects index_count that is not a positive multiple of three", "[asset_system]") {
  const auto result = parseMeshSource(
      "atlantis_static_mesh_source_version: 3\n"
      "vertex_count: 3\n"
      "index_count: 4\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::IndexCountNotMultipleOfThree);
}

TEST_CASE("parseMeshSource rejects index_count of zero", "[asset_system]") {
  const auto result = parseMeshSource(
      "atlantis_static_mesh_source_version: 3\n"
      "vertex_count: 3\n"
      "index_count: 0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::IndexCountNotMultipleOfThree);
}

TEST_CASE("parseMeshSource rejects vertex_count of zero", "[asset_system]") {
  const auto result = parseMeshSource(
      "atlantis_static_mesh_source_version: 3\n"
      "vertex_count: 0\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == SourceParseError::VertexCountOutOfRange);
}

TEST_CASE("parseMeshSource rejects vertex_count above 65535", "[asset_system]") {
  const auto result = parseMeshSource(
      "atlantis_static_mesh_source_version: 3\n"
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
      "atlantis_static_mesh_source_version: 3\n"
      "vertex_count: 3\n"
      "index_count: 4000000002\n"
      "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0 0.577350269 0.577350269 0.577350269\n"
      "vertex: 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.0 0.577350269 0.577350269 0.577350269\n"
      "vertex: 0.0 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.577350269 0.577350269 0.577350269\n"
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
