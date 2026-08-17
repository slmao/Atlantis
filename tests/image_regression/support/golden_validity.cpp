#include "golden_validity.h"

#include "png_codec.h"

#include <fstream>
#include <sstream>

namespace atlantis::image_regression {

namespace {

// Structural pairing check: the sidecar found alongside pngPath is the
// one for it -- sidecarPath's filename must be exactly
// "<pngPath's stem>.sidecar.txt" (Section 2.6's naming convention). A
// caller-supplied mismatched pair (e.g. by constructing paths
// incorrectly) is what this catches; it does not read either file.
[[nodiscard]] bool sidecarNameMatchesPng(const std::filesystem::path& pngPath,
                                          const std::filesystem::path& sidecarPath) {
  constexpr const char* kSidecarSuffix = ".sidecar.txt";
  const std::string pngStem = pngPath.stem().string();
  const std::string sidecarFilename = sidecarPath.filename().string();
  const std::size_t suffixLength = std::char_traits<char>::length(kSidecarSuffix);
  if (sidecarFilename.size() <= suffixLength) return false;
  if (sidecarFilename.compare(sidecarFilename.size() - suffixLength, suffixLength, kSidecarSuffix) != 0) {
    return false;
  }
  const std::string sidecarStem = sidecarFilename.substr(0, sidecarFilename.size() - suffixLength);
  return sidecarStem == pngStem;
}

[[nodiscard]] std::string readFileToString(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

}  // namespace

Result<ValidatedGolden, GoldenValidityError> loadAndValidateGolden(const std::filesystem::path& pngPath,
                                                                     const std::filesystem::path& sidecarPath) {
  // Step 1: existence, then decode (+ step 2's channel/bit-depth
  // contract, enforced inside decodePng() itself).
  if (!std::filesystem::exists(pngPath)) {
    return Result<ValidatedGolden, GoldenValidityError>::Err(GoldenValidityError::MissingPngFile);
  }
  if (!std::filesystem::exists(sidecarPath)) {
    return Result<ValidatedGolden, GoldenValidityError>::Err(GoldenValidityError::MissingSidecarFile);
  }

  auto decodeResult = decodePng(pngPath);
  if (decodeResult.isErr()) {
    switch (decodeResult.error()) {
      case PngDecodeError::FileNotFound:
        return Result<ValidatedGolden, GoldenValidityError>::Err(GoldenValidityError::MissingPngFile);
      case PngDecodeError::DecodeFailed:
        return Result<ValidatedGolden, GoldenValidityError>::Err(GoldenValidityError::PngDecodeFailed);
      case PngDecodeError::ChannelCountMismatch:
        return Result<ValidatedGolden, GoldenValidityError>::Err(GoldenValidityError::ChannelCountMismatch);
      case PngDecodeError::UnsupportedBitDepth:
        return Result<ValidatedGolden, GoldenValidityError>::Err(GoldenValidityError::UnsupportedBitDepth);
    }
    return Result<ValidatedGolden, GoldenValidityError>::Err(GoldenValidityError::PngDecodeFailed);
  }
  const DecodedPng& decoded = decodeResult.value();

  // Step 3, structural half: the sidecar/PNG pairing.
  if (!sidecarNameMatchesPng(pngPath, sidecarPath)) {
    return Result<ValidatedGolden, GoldenValidityError>::Err(GoldenValidityError::SidecarFormatExtentMismatch);
  }

  auto provenanceResult = parseGoldenProvenance(readFileToString(sidecarPath));
  if (provenanceResult.isErr()) {
    return Result<ValidatedGolden, GoldenValidityError>::Err(GoldenValidityError::SidecarMalformed);
  }
  const Provenance& provenance = provenanceResult.value();

  // Step 3, content half: sidecar's own recorded format/extent must
  // match the PNG's actual decoded properties.
  if (provenance.extentWidth != decoded.pixels.width || provenance.extentHeight != decoded.pixels.height) {
    return Result<ValidatedGolden, GoldenValidityError>::Err(GoldenValidityError::SidecarFormatExtentMismatch);
  }

  ValidatedGolden result;
  result.pixels = decoded.pixels;
  result.provenance = provenance;
  return Result<ValidatedGolden, GoldenValidityError>::Ok(std::move(result));
}

Result<std::monostate, ArtifactWriteError> writeFailureArtifacts(const std::filesystem::path& outputDir,
                                                                   const std::string& goldenSlug,
                                                                   const PixelBuffer& actual,
                                                                   const PixelBuffer& golden) {
  std::filesystem::create_directories(outputDir);

  const std::filesystem::path actualPath = outputDir / (goldenSlug + "_actual.png");
  if (encodePng(actualPath, actual).isErr()) {
    return Result<std::monostate, ArtifactWriteError>::Err(ArtifactWriteError::ActualPngWriteFailed);
  }

  const PixelBuffer diff = computeDiffVisualization(actual, golden);
  const std::filesystem::path diffPath = outputDir / (goldenSlug + "_diff.png");
  if (encodePng(diffPath, diff).isErr()) {
    return Result<std::monostate, ArtifactWriteError>::Err(ArtifactWriteError::DiffPngWriteFailed);
  }

  return Result<std::monostate, ArtifactWriteError>::Ok(std::monostate{});
}

}  // namespace atlantis::image_regression
