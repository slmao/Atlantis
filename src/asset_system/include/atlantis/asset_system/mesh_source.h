#pragma once

#include <atlantis/asset_system/errors.h>
#include <atlantis/result.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace atlantis::asset_system {

// Plan 0012 Section D3: one parsed authoring-source mesh -- position
// xyz + colour rgb per vertex, std::uint16_t triangle indices. This is
// the authoring-facing representation (ADR-0035); cook() (Step 4)
// transforms it into the runtime artifact's own binary layout
// (mesh_artifact.h).
struct MeshSourceVertex {
  float positionX = 0.0f;
  float positionY = 0.0f;
  float positionZ = 0.0f;
  float colorR = 0.0f;
  float colorG = 0.0f;
  float colorB = 0.0f;
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

}  // namespace atlantis::asset_system
