#pragma once

#include <atlantis/asset_system/errors.h>
#include <atlantis/result.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace atlantis::asset_system {

// Plan 0012 Section D3, extended by Plan 0017 Section D1/ADR-0058 and
// Plan 0020 Section P1/ADR-0063: one parsed authoring-source mesh --
// position xyz + colour rgb + UV0 uv + object-space normal xyz per
// vertex, std::uint16_t triangle indices. This is the authoring-facing
// representation (ADR-0035); cook() (Step 4) transforms it into the
// runtime artifact's own binary layout (mesh_artifact.h). UV0 and
// normal are both mandatory for every vertex -- there is no optional/
// variant vertex layout (ADR-0058's own Decision item 1, unchanged in
// kind by ADR-0063); a source lacking UV or normal columns is a parse
// error (SourceParseError::CountMismatch), never an implicit default.
struct MeshSourceVertex {
  float positionX = 0.0f;
  float positionY = 0.0f;
  float positionZ = 0.0f;
  float colorR = 0.0f;
  float colorG = 0.0f;
  float colorB = 0.0f;
  float uvU = 0.0f;
  float uvV = 0.0f;
  float normalX = 0.0f;
  float normalY = 0.0f;
  float normalZ = 0.0f;
};

struct ParsedMeshSource {
  std::vector<MeshSourceVertex> vertices;
  std::vector<std::uint16_t> indices;  // always a multiple of 3 in length
};

// Strict parse of Plan 0012 Section D3's authoring-source grammar:
// anchored-prefix field matching, fixed field order, std::from_chars for
// every numeric token (locale-independent, exception-free, never
// std::stof/atof/istringstream), exactly one ASCII space between
// fields. vertex_count/index_count are range-checked before any
// per-vertex/per-index line is read.
[[nodiscard]] atlantis::Result<ParsedMeshSource, SourceParseError> parseMeshSource(std::string_view text);

// Serializes back to the exact grammar parseMeshSource() accepts.
// cook() does not need this in its own pipeline (authoring source is
// hand-written, never generated) -- it exists for round-trip testing.
[[nodiscard]] std::string serializeMeshSource(const ParsedMeshSource& source);

// Plan 0020 Section P7: an internal seam, exposed for direct unit
// testing only -- not part of this module's own public contract.
// Mirrors atlantis::runtime::detail::checkForDuplicatesAndCollisions()'s
// own already-established precedent exactly (scene_manifest.h): a
// small piece of logic factored out specifically so a test can exercise
// it directly, without becoming part of parseMeshSource()'s/
// decodeMeshArtifact()'s own stable public contract. Both
// parseMeshSource() (this translation unit) and decodeMeshArtifact()
// (mesh_artifact.cpp, via its own existing #include of this header)
// call both functions below -- one shared implementation, never a
// per-translation-unit duplicate.
namespace detail {

// Computes a normal's own length-squared from its three
// already-finite-checked float components, via exact float-to-double
// promotion (always exact -- zero rounding) and a fixed, left-to-right
// summation order. Does NOT, on its own, guarantee its own returned
// value is bit-identical across every compiler/target for a given
// input -- fused-multiply-add (FMA) contraction of `a*a + b` is a
// compiler/ISA choice orthogonal to operand width, and this function
// neither forces nor forbids it via any pragma or compiler flag. See
// isNormalLengthSquaredInTolerance() below for why this does not
// matter for this format's own real accept/reject contract.
[[nodiscard]] double computeNormalLengthSquared(float x, float y, float z) noexcept;

// Spec 0020 D3's own exact tolerance: [0.9801, 1.0201] inclusive
// (0.99^2 / 1.01^2), a real +-1%-of-unit-length margin restated on the
// squared quantity. This +-1% margin is chosen to be, and remains,
// vastly wider than the largest possible discrepancy between an
// FMA-contracted and a separately-rounded computation of a three-term
// sum of squares (bounded by a small constant multiple of double's own
// machine epsilon, ~2.22e-16) -- no realistic authored normal changes
// its own accept/reject outcome based on whether a given
// compiler/target contracts multiply-add into an FMA instruction. This
// function does NOT guarantee bit-identical intermediate lengthSquared
// values across toolchains -- only that the accept/reject decision
// computeNormalLengthSquared() feeds into here is stable for any input
// not deliberately engineered to sit within about 1e-15 of the exact
// boundary, which no human-authored normal ever will.
[[nodiscard]] bool isNormalLengthSquaredInTolerance(double lengthSquared) noexcept;

}  // namespace detail

}  // namespace atlantis::asset_system
