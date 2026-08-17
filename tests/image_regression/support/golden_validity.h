#pragma once

#include "pixel_diff.h"
#include "provenance.h"

#include <atlantis/result.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>

namespace atlantis::image_regression {

enum class GoldenValidityError {
  MissingPngFile,
  MissingSidecarFile,
  PngDecodeFailed,              // wraps PngDecodeError::DecodeFailed
  ChannelCountMismatch,         // wraps PngDecodeError::ChannelCountMismatch
  UnsupportedBitDepth,          // wraps PngDecodeError::UnsupportedBitDepth
  SidecarMalformed,             // wraps ProvenanceParseError
  SidecarFormatExtentMismatch,  // sidecar's recorded format/extent != PNG's own decoded properties, or a
                                 // structurally mismatched sidecar/PNG pairing (naming convention violation)
};

struct ValidatedGolden {
  PixelBuffer pixels;
  Provenance provenance;
};

// The four-step check ADR-0042 fixes, matched here to a concrete,
// implementable form (Plan 0011 Section 2.4): (1) both pngPath and
// sidecarPath exist as files, and the PNG decodes
// (Err(MissingPngFile)/Err(MissingSidecarFile)/Err(PngDecodeFailed),
// each distinct); (2) decoded properties (channelsInFile == 4, not
// 16-bit) satisfy the RGBA8 contract; (3) the sidecar found alongside
// pngPath is structurally the one for it (same basename stem) and its
// own recorded format/extent matches the PNG's actual decoded
// width/height. Returns Err at the first failing step -- never
// partially populates ValidatedGolden on a failure path.
[[nodiscard]] atlantis::Result<ValidatedGolden, GoldenValidityError> loadAndValidateGolden(
    const std::filesystem::path& pngPath, const std::filesystem::path& sidecarPath);

enum class ArtifactWriteError { ActualPngWriteFailed, DiffPngWriteFailed };

// ADR-0042's own "Failure output" contract, the disk-writing half
// (computeDiffVisualization(), pixel_diff.h, is the pure pixel-math
// half). Writes <outputDir>/<goldenSlug>_actual.png and
// <outputDir>/<goldenSlug>_diff.png, creating outputDir if it does not
// exist. goldenSlug scopes the two output filenames by golden name
// (e.g. "minimal_cube_512x512_rgba8unorm") so a future multi-scene run
// cannot have one scene's failure artifacts overwrite another's. Always
// overwrites any pre-existing file at either path -- these are
// transient diagnostic artifacts, never protected by the "never
// overwrite a golden" rule, which applies only to
// tests/image_regression/goldens/ (ADR-0042's own golden-regeneration
// boundary, unaffected by this function).
[[nodiscard]] atlantis::Result<std::monostate, ArtifactWriteError> writeFailureArtifacts(
    const std::filesystem::path& outputDir, const std::string& goldenSlug, const PixelBuffer& actual,
    const PixelBuffer& golden);

}  // namespace atlantis::image_regression
